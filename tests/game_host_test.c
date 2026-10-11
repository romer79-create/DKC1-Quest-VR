// Copyright 2026 Sylar.soluciones. SPDX-License-Identifier: MIT
#include "host_util.h"
#ifdef _WIN32
#include <windows.h>
#include <process.h>
#else
#include <pthread.h>
#include <sched.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#define CHECK(condition) do { if (!(condition)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#condition); exit(1); } } while(0)
static SylarPcmRing ring;
enum { TRANSFER_FRAMES=1000000 };
static void yield_thread(void) {
#ifdef _WIN32
    SwitchToThread();
#else
    sched_yield();
#endif
}
#ifdef _WIN32
static unsigned __stdcall producer(void *unused) {
#else
static void *producer(void *unused) {
#endif
    (void)unused; uint32_t index=0;
    while (index<TRANSFER_FRAMES) {
        int16_t data[37*2]; size_t count=TRANSFER_FRAMES-index;
        if (count>37) count=37;
        for (size_t i=0;i<count;i++) {
            int16_t value=(int16_t)((index+i)%32749);
            data[i*2]=value; data[i*2+1]=(int16_t)-value;
        }
        size_t written=sylar_pcm_push(&ring,data,count);
        index+=(uint32_t)written; if (!written) yield_thread();
    }
    return 0;
}
static void test_ring(void) {
    sylar_pcm_reset(&ring);
    int16_t in[]={11,-11,22,-22,33,-33},out[8];
    CHECK(sylar_pcm_push(&ring,in,3)==3);
    CHECK(sylar_pcm_pull(&ring,out,4)==3);
    CHECK(!memcmp(in,out,sizeof(in)) && out[6]==0 && out[7]==0);
    atomic_store(&ring.read_index,UINT32_MAX-1); atomic_store(&ring.write_index,UINT32_MAX-1);
    CHECK(sylar_pcm_push(&ring,in,3)==3);
    CHECK(sylar_pcm_pull(&ring,out,3)==3 && !memcmp(in,out,sizeof(in)));
    sylar_pcm_reset(&ring);
#ifdef _WIN32
    HANDLE thread=(HANDLE)_beginthreadex(NULL,0,producer,NULL,0,NULL); CHECK(thread);
#else
    pthread_t thread; CHECK(pthread_create(&thread,NULL,producer,NULL)==0);
#endif
    uint32_t index=0;
    while (index<TRANSFER_FRAMES) {
        int16_t samples[53*2]; size_t count=sylar_pcm_pull(&ring,samples,53);
        for (size_t i=0;i<count;i++) {
            int16_t expected=(int16_t)((index+i)%32749);
            CHECK(samples[i*2]==expected && samples[i*2+1]==-expected);
        }
        for (size_t i=count*2;i<106;i++) CHECK(samples[i]==0);
        index+=(uint32_t)count; if (!count) yield_thread();
    }
#ifdef _WIN32
    CHECK(WaitForSingleObject(thread,10000)==WAIT_OBJECT_0); CloseHandle(thread);
#else
    CHECK(pthread_join(thread,NULL)==0);
#endif
    CHECK(atomic_load(&ring.read_index)==atomic_load(&ring.write_index));
    int16_t full[SYLAR_PCM_CAPACITY*2]={0};
    CHECK(sylar_pcm_push(&ring,full,SYLAR_PCM_CAPACITY)==SYLAR_PCM_CAPACITY);
    CHECK(sylar_pcm_push(&ring,in,3)==0); sylar_pcm_reset(&ring);
    CHECK(sylar_pcm_pull(&ring,out,4)==0);
    for (int i=0;i<8;i++) CHECK(out[i]==0);
    puts("PASS PCM: wrap, overflow, underflow, reset, concurrent 1M stereo frames");
}
static void test_presentation(void) {
    const uint8_t bgra[]={0,0,255,0,255,0,0,16,0,255,0,127};
    const uint8_t expected[]={255,0,0,255,0,0,255,255,0,255,0,255}; uint8_t out[12];
    sylar_bgra_to_rgba(out,bgra,3); CHECK(!memcmp(out,expected,sizeof(out)));
    CHECK(sylar_touch_mask(0,0,false,false,false,false)==0);
    CHECK(sylar_touch_mask(-1,1,true,false,false,false)==0x51);
    CHECK(sylar_touch_mask(1,-1,false,true,true,true)==0xae);
    CHECK(sylar_touch_mask(NAN,NAN,true,false,false,false)==1);
    CHECK(sylar_touch_mask(0.29f,-0.29f,false,false,false,false)==0);
    double deadline=0;
    for (int i=0;i<60099;i++) deadline=sylar_next_deadline(deadline,deadline);
    CHECK(fabs(deadline-1000.00313048)<0.00001);
    CHECK(fabs(sylar_next_deadline(1,100)-100-1/SYLAR_GAME_HZ)<0.000001);
    puts("PASS presentation: BGRA colors/alpha, SNES mappings/diagonals/deadzone, 60.0988Hz cadence/debt");
}
int main(void) { test_ring(); test_presentation(); return 0; }
