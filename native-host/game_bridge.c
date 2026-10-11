// Copyright 2026 Sylar.soluciones. SPDX-License-Identifier: MIT
// Engine calls belong exclusively to the worker. AAudio consumes PCM only.
#define _POSIX_C_SOURCE 200809L
#include "game_bridge.h"
#include "layer_capture.h"
#include "common_cpu_infra.h"
#include "common_rtl.h"
#include "snes/snes.h"
#include "dkc1_game.h"
#include "dkc1_video.h"
#include "desktop_sram.h"
#include "verified_rom.h"
#include "host_report.h"
#include "spc_player.h"
#include <aaudio/AAudio.h>
#include <android/log.h>
#include <pthread.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define INFO(...) __android_log_print(ANDROID_LOG_INFO,"SylarGame",__VA_ARGS__)
#define ERROR(...) __android_log_print(ANDROID_LOG_ERROR,"SylarGame",__VA_ARGS__)
bool sylar_sram_directory(const char *directory);
struct SylarGame {
    pthread_t thread;
    pthread_mutex_t frame_mutex;
    pthread_mutex_t wake_mutex;
    pthread_cond_t wake;
    bool parked; // Protected by wake_mutex; audio/save shutdown has completed.
    _Atomic bool worker_running, focused, audio_error;
    _Atomic uint32_t input;
    _Atomic uint32_t pause_generation;
    _Atomic int status;
    _Atomic bool diorama;
    _Atomic bool relief;
    SylarLayers layer_capture;
    SylarFrame published;
    uint8_t bgra[SYLAR_RGBA_BYTES], rgba[SYLAR_RGBA_BYTES];
    SylarPcmRing pcm;
    AAudioStream *stream;
    int rate;
    double audio_fraction;
    char rom_path[1024], save_directory[1024];
    uint8_t *rom;
    Dkc1SramStore save;
    bool engine_initialized;
};
static _Thread_local jmp_buf *fatal_boundary;
// NativeActivity can be destroyed and recreated without ending the process.
// The pinned game has process-lifetime static CPU continuation state and no
// session_reset callback. Keep one engine worker per process, independently of XR.
static pthread_mutex_t attachment_mutex=PTHREAD_MUTEX_INITIALIZER;
static SylarGame *process_game;
static bool activity_attached;
static _Atomic bool fatal_seen;

// Legacy high-level SPC interface: the native SPC/DSP implementation owns DKC audio.
bool g_new_ppu=true;
static void legacy_initialize(SpcPlayer *p) { (void)p; }
static void legacy_upload(SpcPlayer *p,const uint8_t *bytes) { (void)p; (void)bytes; }
static SpcPlayer legacy_player={.initialize=legacy_initialize,.upload=legacy_upload};
SpcPlayer *g_spc_player=&legacy_player;
void NORETURN Die(const char *error) {
    ERROR("Core fatal: %s",error?error:"unknown"); atomic_store(&fatal_seen,true);
    if (fatal_boundary) longjmp(*fatal_boundary,1);
    abort();
}
// The callback never calls RtlRenderAudio; all APU accesses share one worker.
void RtlApuLock(void) {}
void RtlApuUnlock(void) {}
void host_report_init(const char *a,const char *b) { (void)a; (void)b; }
void host_report_set_output_directory(const char *a) { (void)a; }
void host_report_breadcrumb(const char *a,...) { (void)a; }
void host_report_fatal(const char *a) { atomic_store(&fatal_seen,true); ERROR("Core fatal: %s",a?a:"unknown"); }
int host_report_has_fatal(void) { return atomic_load(&fatal_seen); }
void host_report_dump_json(FILE *f) { (void)f; }
const char *host_report_write_minidump(void *a) { (void)a; return NULL; }
const char *host_report_preserve_crash_copy(const char *a) { (void)a; return NULL; }
void host_report_crash_test_tick(void) {}

static double monotonic(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t);
    return (double)t.tv_sec+(double)t.tv_nsec/1e9;
}
static void short_wait(double seconds) {
    // A short bound lets focus/exit interrupt pacing without touching engine state.
    if (seconds>0.005) seconds=0.005;
    if (seconds>0) { struct timespec t={0,(long)(seconds*1e9)}; nanosleep(&t,NULL); }
}
static aaudio_data_callback_result_t audio_data(AAudioStream *stream,void *user,void *out,int32_t frames) {
    (void)stream; SylarGame *g=user;
    if (frames<=0) return AAUDIO_CALLBACK_RESULT_CONTINUE;
    if (!atomic_load_explicit(&g->focused,memory_order_acquire))
        memset(out,0,(size_t)frames*2*sizeof(int16_t));
    else (void)sylar_pcm_pull(&g->pcm,out,(size_t)frames);
    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}
static void audio_error(AAudioStream *stream,void *user,aaudio_result_t error) {
    (void)stream; (void)error; SylarGame *g=user;
    atomic_store(&g->audio_error,true);
}
static void close_audio(SylarGame *g) {
    if (g->stream) {
        (void)AAudioStream_requestStop(g->stream);
        (void)AAudioStream_close(g->stream);
        g->stream=NULL;
    }
    sylar_pcm_reset(&g->pcm); g->audio_fraction=0;
}
static bool open_audio(SylarGame *g) {
    AAudioStreamBuilder *builder=NULL;
    if (AAudio_createStreamBuilder(&builder)!=AAUDIO_OK) return false;
    AAudioStreamBuilder_setDirection(builder,AAUDIO_DIRECTION_OUTPUT);
    AAudioStreamBuilder_setSharingMode(builder,AAUDIO_SHARING_MODE_SHARED);
    AAudioStreamBuilder_setPerformanceMode(builder,AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
    AAudioStreamBuilder_setFormat(builder,AAUDIO_FORMAT_PCM_I16);
    AAudioStreamBuilder_setChannelCount(builder,2);
    AAudioStreamBuilder_setSampleRate(builder,48000);
    AAudioStreamBuilder_setDataCallback(builder,audio_data,g);
    AAudioStreamBuilder_setErrorCallback(builder,audio_error,g);
    aaudio_result_t result=AAudioStreamBuilder_openStream(builder,&g->stream);
    AAudioStreamBuilder_delete(builder);
    if (result!=AAUDIO_OK || !g->stream) { g->stream=NULL; return false; }
    g->rate=AAudioStream_getSampleRate(g->stream);
    if (AAudioStream_getFormat(g->stream)!=AAUDIO_FORMAT_PCM_I16 ||
        AAudioStream_getChannelCount(g->stream)!=2 || g->rate<8000 || g->rate>192000) {
        close_audio(g); return false;
    }
    RtlSetAudioOutputRate((double)g->rate);
    atomic_store(&g->audio_error,false);
    if (AAudioStream_requestStart(g->stream)!=AAUDIO_OK) { close_audio(g); return false; }
    INFO("AAudio started: stereo PCM16 rate=%d",g->rate);
    return true;
}
static bool flush_save(SylarGame *g) {
    char error[256];
    if (Dkc1SramFlush(&g->save,g_sram,(size_t)g_sram_size,true,error,sizeof(error))) return true;
    ERROR("Private SRAM write failed; game paused.");
    atomic_store(&g->status,SYLAR_SAVE_ERROR); return false;
}
static void *worker(void *user) {
    SylarGame *g=user;
    jmp_buf boundary; fatal_boundary=&boundary;
    if (setjmp(boundary)) { atomic_store(&g->status,SYLAR_CORE_ERROR); goto done; }
    if (access(g->rom_path,F_OK)!=0) { atomic_store(&g->status,SYLAR_ROM_MISSING); goto done; }
    size_t rom_size=0; char error[256];
    // Environment overrides are deliberately disabled in this fixed-ROM host.
    unsetenv("DKC1_ALLOW_ROM_SHA256");
    g->rom=Dkc1ReadVerifiedRom(g->rom_path,&rom_size,error,sizeof(error));
    if (!g->rom) { atomic_store(&g->status,SYLAR_ROM_INVALID); goto done; }
    if (!sylar_sram_directory(g->save_directory)) { atomic_store(&g->status,SYLAR_SAVE_ERROR); goto done; }
    RtlSetSaveRoot(g->save_directory);
    RtlRegisterGame(Dkc1GameInfo());
    if (g_rtl_game_info->session_reset) g_rtl_game_info->session_reset();
    g_fail=false; g_watchdog_tripped=0; atomic_store(&fatal_seen,false);
    char panorama_path[1024];
    int panorama_length=snprintf(panorama_path,sizeof(panorama_path),"%s/../panorama.enabled",g->save_directory);
    bool panorama=false;
    if(panorama_length>0 && (size_t)panorama_length<sizeof(panorama_path)){FILE *option=fopen(panorama_path,"rb");if(option){panorama=fgetc(option)=='1';fclose(option);}}
    Dkc1VideoSetWidescreen(panorama);
    Dkc1VideoSetEdgePolicy(kDkc1EdgeBars);
    Dkc1VideoSetRom(g->rom,rom_size);
    if (!SnesInit(g->rom,(int)rom_size)) { atomic_store(&g->status,SYLAR_CORE_ERROR); goto done; }
    g->engine_initialized=true;
    Dkc1BeginDrawing(g->bgra,SYLAR_WIDTH*4);
    g->rate=48000; RtlSetAudioOutputRate((double)g->rate);
    if (!Dkc1SramLoad(&g->save,g_sram,(size_t)g_sram_size,error,sizeof(error))) {
        ERROR("Private SRAM load failed; original file retained."); atomic_store(&g->status,SYLAR_SAVE_ERROR); goto done;
    }
    atomic_store(&g->status,SYLAR_READY);
    INFO("Verified ROM loaded; panorama=%d source=%dx224, original game timing.",panorama,Dkc1VideoWidth());
    double deadline=monotonic(), retry_audio=0, report_at=deadline+5.0;
    bool was_active=false; unsigned frames=0, dropped_audio=0;
    uint32_t pause_generation=atomic_load(&g->pause_generation);
    for (;;) {
        uint32_t observed=atomic_load(&g->pause_generation);
        if (observed!=pause_generation) {
            pause_generation=observed; close_audio(g);
            if (!flush_save(g)) break;
            was_active=false;
        }
        if (!atomic_load(&g->focused)) {
            if (was_active) { close_audio(g); if (!flush_save(g)) break; }
            was_active=false;
            pthread_mutex_lock(&g->wake_mutex);
            while (!atomic_load(&g->focused)) {
                g->parked=true;
                pthread_cond_broadcast(&g->wake);
                pthread_cond_wait(&g->wake,&g->wake_mutex);
            }
            g->parked=false;
            pthread_mutex_unlock(&g->wake_mutex);
            continue;
        }
        double now=monotonic();
        if (!was_active) { deadline=now; was_active=true; retry_audio=0; }
        if (atomic_load(&g->audio_error)) { close_audio(g); atomic_store(&g->audio_error,false); retry_audio=now+1.0; }
        if (!g->stream && now>=retry_audio) {
            if (!open_audio(g)) { ERROR("AAudio unavailable; retrying. Game remains active."); retry_audio=now+2.0; }
        }
        if (now<deadline) { short_wait(deadline-now); continue; }
        // Recheck after setup/waits. A frame in progress finishes before pause.
        if (!atomic_load(&g->focused)) continue;
        (void)RtlRunFrame(atomic_load(&g->input));
        if (g_fail || g_watchdog_tripped || !Dkc1LastLleResult() || atomic_load(&fatal_seen)) {
            ERROR("Core stopped; incomplete frame not published or saved."); atomic_store(&g->status,SYLAR_CORE_ERROR); break;
        }
        bool diorama=atomic_load(&g->diorama);
        bool relief=atomic_load(&g->relief);
        sylar_layers_select((diorama || relief)?&g->layer_capture:NULL);
        sylar_relief_select(relief);
        Dkc1DrawPpuFrame();
        if (g_fail || atomic_load(&fatal_seen)) { atomic_store(&g->status,SYLAR_CORE_ERROR); break; }
        pthread_mutex_lock(&g->frame_mutex);
        sylar_capture_raw_rgba(g->rgba,g_ppu);
        memcpy(g->published.rgba,g->rgba,SYLAR_RGBA_BYTES);
        g->published.sequence++;
        g->published.relief_valid=relief && !g->layer_capture.composite_mismatches &&
            !g->layer_capture.unsupported_lines &&
            (g->layer_capture.relief_pixels[0]+g->layer_capture.relief_pixels[1]>=20);
        if(g->published.relief_valid){
            memcpy(g->published.relief_background,g->layer_capture.relief_background,SYLAR_RGBA_BYTES);
            memcpy(g->published.relief_sprite,g->layer_capture.relief_sprite,SYLAR_RGBA_BYTES);
            memcpy(g->published.relief_owner,g->layer_capture.relief_owner,sizeof(g->published.relief_owner));
            memcpy(g->published.relief_pixels,g->layer_capture.relief_pixels,sizeof(g->published.relief_pixels));
        }else memset(g->published.relief_pixels,0,sizeof(g->published.relief_pixels));
        g->published.layers_valid=diorama && !g->layer_capture.composite_mismatches && !g->layer_capture.unsupported_lines;
        if (g->published.layers_valid) {
            memcpy(g->published.planes,g->layer_capture.rgba,sizeof(g->published.planes));
            memcpy(g->published.occupied,g->layer_capture.occupied,sizeof(g->published.occupied));
        }
        pthread_mutex_unlock(&g->frame_mutex);
        g->audio_fraction+=(double)g->rate/SYLAR_GAME_HZ;
        int count=(int)g->audio_fraction; g->audio_fraction-=count;
        int16_t audio[3200*2]; // max 192kHz / 60.0988Hz < 3200
        RtlRenderAudio(audio,count,2);
        if (g->stream && sylar_pcm_push(&g->pcm,audio,(size_t)count)!=(size_t)count) dropped_audio++;
        if (!flush_save(g)) break;
        frames++; now=monotonic(); deadline=sylar_next_deadline(deadline,now);
        if (now>=report_at) { INFO("Host completed frames=%u PCM-overflow-frames=%u",frames,dropped_audio); frames=0; dropped_audio=0; report_at=now+5.0; }
    }
    if (atomic_load(&g->status)==SYLAR_READY) (void)flush_save(g);
done:
    atomic_store(&g->focused,false); close_audio(g);
    // Fatal boundaries never flush a partially completed guest frame.
    if (g_snes) { snes_free(g_snes); g_snes=NULL; g_snes_cpu=NULL; g_ppu=NULL; g_dma=NULL; g_sram=NULL; g_sram_size=0; }
    Dkc1VideoSetRom(NULL,0); free(g->rom); g->rom=NULL;
    fatal_boundary=NULL;
    pthread_mutex_lock(&g->wake_mutex);
    atomic_store(&g->worker_running,false);
    pthread_cond_broadcast(&g->wake);
    pthread_mutex_unlock(&g->wake_mutex);
    return NULL;
}
SylarGame *sylar_game_start(const char *path) {
    if (!path) return NULL;
    char rom_path[1024], save_directory[1024];
    int r=snprintf(rom_path,sizeof(rom_path),"%s/game.sfc",path);
    int s=snprintf(save_directory,sizeof(save_directory),"%s/saves",path);
    if (r<0 || s<0 || (size_t)r>=sizeof(rom_path) || (size_t)s>=sizeof(save_directory)) return NULL;
    pthread_mutex_lock(&attachment_mutex);
    if (activity_attached) { pthread_mutex_unlock(&attachment_mutex); return NULL; }
    if (process_game) {
        SylarGame *existing=process_game;
        if (strcmp(existing->rom_path,rom_path) || strcmp(existing->save_directory,save_directory)) {
            pthread_mutex_unlock(&attachment_mutex); return NULL;
        }
        activity_attached=true;
        INFO("Activity attached to preserved process game; status=%d",atomic_load(&existing->status));
        pthread_mutex_unlock(&attachment_mutex);
        return existing;
    }
    SylarGame *g=calloc(1,sizeof(*g));
    if (!g) goto failed;
    memcpy(g->rom_path,rom_path,(size_t)r+1); memcpy(g->save_directory,save_directory,(size_t)s+1);
    if (pthread_mutex_init(&g->frame_mutex,NULL)) goto free_game;
    if (pthread_mutex_init(&g->wake_mutex,NULL)) goto free_frame_mutex;
    if (pthread_cond_init(&g->wake,NULL)) goto free_wake_mutex;
    atomic_init(&g->status,SYLAR_LOADING);
    atomic_init(&g->worker_running,true); atomic_init(&g->focused,false);
    atomic_init(&g->audio_error,false); atomic_init(&g->input,0); atomic_init(&g->pause_generation,0);
    atomic_init(&g->diorama,false);
    atomic_init(&g->relief,false);
    sylar_pcm_reset(&g->pcm);
    if (pthread_create(&g->thread,NULL,worker,g)) {
        pthread_cond_destroy(&g->wake); goto free_wake_mutex;
    }
    process_game=g; activity_attached=true;
    INFO("Activity attached to new process game.");
    pthread_mutex_unlock(&attachment_mutex);
    return g;
free_wake_mutex:
    pthread_mutex_destroy(&g->wake_mutex);
free_frame_mutex:
    pthread_mutex_destroy(&g->frame_mutex);
free_game:
    free(g);
failed:
    pthread_mutex_unlock(&attachment_mutex);
    return NULL;
}
void sylar_game_state(SylarGame *g,bool focused,uint32_t inputs) {
    if (!g) return;
    atomic_store(&g->input,focused?inputs:0);
    bool was_focused=atomic_exchange_explicit(&g->focused,focused,memory_order_acq_rel);
    if (was_focused && !focused) atomic_fetch_add(&g->pause_generation,1);
    if (was_focused!=focused) {
        pthread_mutex_lock(&g->wake_mutex);
        pthread_cond_broadcast(&g->wake);
        pthread_mutex_unlock(&g->wake_mutex);
    }
}
enum SylarGameStatus sylar_game_copy(SylarGame *g,uint8_t *out) {
    if (!g) return SYLAR_CORE_ERROR;
    enum SylarGameStatus status=(enum SylarGameStatus)atomic_load(&g->status);
    if (status==SYLAR_READY) { pthread_mutex_lock(&g->frame_mutex); memcpy(out,g->rgba,SYLAR_RGBA_BYTES); pthread_mutex_unlock(&g->frame_mutex); }
    return status;
}
void sylar_game_detach(SylarGame *g) {
    if (!g) return;
    pthread_mutex_lock(&attachment_mutex);
    if (g!=process_game || !activity_attached) { pthread_mutex_unlock(&attachment_mutex); return; }
    sylar_game_state(g,false,0);
    // Complete the in-flight frame, close AAudio and flush changed SRAM before
    // releasing the activity. The parked worker blocks without polling or sound.
    pthread_mutex_lock(&g->wake_mutex);
    while (atomic_load(&g->worker_running) && !g->parked) pthread_cond_wait(&g->wake,&g->wake_mutex);
    pthread_mutex_unlock(&g->wake_mutex);
    activity_attached=false;
    INFO("Activity detached; process game paused and preserved.");
    pthread_mutex_unlock(&attachment_mutex);
}
void sylar_game_diorama(SylarGame *g,bool enabled) {
    if (g) atomic_store(&g->diorama,enabled);
}
void sylar_game_relief(SylarGame *g,bool enabled) {
    if (g) atomic_store(&g->relief,enabled);
}
enum SylarGameStatus sylar_game_frame(SylarGame *g,SylarFrame *frame) {
    if (!g || !frame) return SYLAR_CORE_ERROR;
    enum SylarGameStatus status=(enum SylarGameStatus)atomic_load(&g->status);
    if (status==SYLAR_READY) {
        pthread_mutex_lock(&g->frame_mutex); memcpy(frame,&g->published,sizeof(*frame)); pthread_mutex_unlock(&g->frame_mutex);
    }
    return status;
}
