// Copyright 2026 Sylar.soluciones. SPDX-License-Identifier: MIT
// Captures the pinned renderer's real colors and native priorities, never guest RAM.
#include "layer_capture.h"
#include "dkc1_game.h"
#include "dkc1_video.h"
#include "common_cpu_infra.h"
#include "common_rtl.h"
#include "snes/snes.h"
#include "dkc1_wram_gen.h"
#include <string.h>

enum { EXTRA=(SYLAR_WIDTH-256)/2 };
static const uint8_t black[4]={0,0,0,255};
static const uint8_t *raw_pixel(const Ppu *ppu,unsigned y,unsigned output_x){
    int x=(int)output_x-EXTRA+(int)ppu->extraLeftRight;
    int width=256+2*(int)ppu->extraLeftRight;
    if(x<0 || x>=width)return black;
    return ppu->renderBuffer+(size_t)y*ppu->renderPitch+(size_t)x*4;
}
void sylar_capture_raw_rgba(uint8_t *out,const Ppu *ppu){
    for(unsigned y=0;y<SYLAR_HEIGHT;y++)for(unsigned x=0;x<SYLAR_WIDTH;x++){
        const uint8_t *p=raw_pixel(ppu,y,x);size_t i=((size_t)y*SYLAR_WIDTH+x)*4;
        out[i]=p[2];out[i+1]=p[1];out[i+2]=p[0];out[i+3]=255;
    }
}
static _Thread_local SylarLayers *capture;
static _Thread_local void (*on_capture)(const SylarLayers *);
static _Thread_local bool relief_enabled;
void sylar_relief_select(bool enabled) { relief_enabled=enabled; }
static void palette_pixel(Ppu *ppu,uint8_t *out,uint16_t pixel) {
    unsigned color=ppu->cgram[pixel&255];
    out[0]=ppu->brightnessMult[(color>>10)&31];
    out[1]=ppu->brightnessMult[(color>>5)&31];
    out[2]=ppu->brightnessMult[color&31]; out[3]=255;
}
void sylar_relief_obj_pixel(Ppu *ppu,unsigned y,int x,unsigned slot,uint16_t color) {
    x+=EXTRA;
    if(!relief_enabled || !capture || y>=SYLAR_HEIGHT || x<0 || x>=SYLAR_WIDTH || slot>=128)return;
    size_t i=y*SYLAR_WIDTH+(unsigned)x;
    unsigned owner=capture->oam_owner[slot];
    capture->obj_owner[i]=(uint8_t)owner;capture->obj_color[i]=color;
    if(!owner){palette_pixel(ppu,capture->other_obj+i*4,color);capture->other_priority[i]=(uint8_t)(color>>12);}
}
static void select_kongs(Ppu *ppu,const uint8_t *ram) {
    // First capability: native Jungle Hijinxs. Other entrances remain stock.
    if(!relief_enabled || Dkc1WramU16(ram,0x0032)!=0 || Dkc1WramU16(ram,0x003e)!=0x16)return;
    for(unsigned actor=2;actor<=0x32;actor+=2){
        unsigned id=Dkc1Actor_SpriteIDLo(ram,actor);
        if(id!=1 && id!=2)continue;
        int ax=(int16_t)(Dkc1Actor_SprXPos(ram,actor)-Dkc1WramU16(ram,DKC1_WRAM_CameraX));
        unsigned properties=(Dkc1Actor_SprFlags(ram,actor)>>8)&0x3e;
        // Layer1YPos contains a tile-map bias and is not the actor camera Y.
        // Use actual OAM Y, as the pinned BabyKong ownership query does.
        if(ax< -32-EXTRA || ax>288+EXTRA)continue;
        bool ambiguous=false;
        for(unsigned other=2;other<=0x32;other+=2){
            unsigned oid=Dkc1Actor_SpriteIDLo(ram,other);
            if(!oid || oid==1 || oid==2)continue;
            if(((Dkc1Actor_SprFlags(ram,other)>>8)&0x3e)!=properties)continue;
            int dx=(int16_t)(Dkc1Actor_SprXPos(ram,other)-Dkc1Actor_SprXPos(ram,actor));
            if(dx> -80 && dx<80)ambiguous=true;
        }
        if(ambiguous)continue;
        unsigned count=0;uint8_t matches[128]={0};
        for(unsigned oam=0;oam<128;oam++){
            unsigned position=ppu->oam[oam*2],flags=ppu->oam[oam*2+1]>>8;
            int y=position>>8; if(y>=224)continue;
            int x=(position&255)|(((ppu->highOam[oam/4]>>((oam%4)*2))&1)<<8);if(x>=256)x-=512;
            if((flags&0x3e)==properties && x>=ax-48 && x<=ax+40 && y>=32){matches[oam]=1;count++;}
        }
        if(count<2 || count>24)continue;
        for(unsigned oam=0;oam<128;oam++)if(matches[oam]){
            if(capture->oam_owner[oam])capture->oam_owner[oam]=0;
            else capture->oam_owner[oam]=(uint8_t)id;
        }
        capture->selected_oam[id-1]=count;
    }
}
static unsigned depth_group(unsigned priority) {
    if (priority>=14) return 4;
    if (priority>=11) return 3;
    if (priority>=10) return 2;
    if (priority>=8) return 1;
    return 0;
}
static void rgba_pixel(uint8_t *dst,const uint8_t *bgra) {
    dst[0]=bgra[2]; dst[1]=bgra[1]; dst[2]=bgra[0]; dst[3]=bgra[3];
}
void sylar_layers_select(SylarLayers *layers) { capture=layers; }
void sylar_layers_observer(void (*observer)(const SylarLayers *)) { on_capture=observer; }
void sylar_capture_bg_line(Ppu *ppu,unsigned source,unsigned y) {
    if (!capture || source>=4 || !y || y>SYLAR_HEIGHT) return;
    uint8_t *dst=capture->priority[source]+(y-1)*SYLAR_WIDTH;
    const PpuZbufType *src=ppu->overlayBuffers[source].data+kPpuExtraLeftRight-EXTRA;
    for (unsigned x=0;x<SYLAR_WIDTH;x++) dst[x]=(uint8_t)(src[x]>>12);
}
void sylar_capture_final_line(Ppu *ppu,unsigned y) {
    if (!capture || !y || y>SYLAR_HEIGHT) return;
    if(!PPU_forcedBlank(ppu) && (PPU_mode(ppu)!=1 ||
        !(ppu->renderFlags & kPpuRenderFlags_NewRenderer) ||
        PPU_interlace(ppu) || PPU_pseudoHires(ppu) ||
        (ppu->mosaic && PPU_mosaicSize(ppu)>1))) capture->unsupported_lines++;
    size_t offset=(y-1)*SYLAR_WIDTH;
    uint32_t color=ppu->cgram[0];
    for (unsigned x=0;x<SYLAR_WIDTH;x++) {
        capture->winner[offset+x]=PPU_forcedBlank(ppu)?0:(uint8_t)(ppu->bgBuffers[0].data[x+kPpuExtraLeftRight-EXTRA]>>12);
        capture->priority[4][offset+x]=(uint8_t)(ppu->overlayBuffers[4].data[x+kPpuExtraLeftRight-EXTRA]>>12);
        uint8_t *base=capture->backdrop+(offset+x)*4;
        base[0]=ppu->brightnessMult[(color>>10)&31];
        base[1]=ppu->brightnessMult[(color>>5)&31];
        base[2]=ppu->brightnessMult[color&31]; base[3]=255;
        if(relief_enabled){
            size_t i=offset+x;
            const uint8_t *raw=raw_pixel(ppu,y-1,x);
            rgba_pixel(capture->relief_background+i*4,raw);
            unsigned owner=capture->obj_owner[i];
            if(owner && (ppu->screenEnabled[0]&16) &&
               ppu->bgBuffers[0].data[x+kPpuExtraLeftRight-EXTRA]==capture->obj_color[i]){
                uint8_t *background=capture->relief_background+i*4;
                unsigned best=0;rgba_pixel(background,base);
                for(unsigned source=0;source<4;source++){
                    const uint8_t *candidate=capture->source[source]+i*4;
                    unsigned priority=capture->priority[source][i];
                    if(candidate[3] && priority>=best){rgba_pixel(background,candidate);best=priority;}
                }
                if(capture->other_obj[i*4+3] && capture->other_priority[i]>best)
                    rgba_pixel(background,capture->other_obj+i*4);
                rgba_pixel(capture->relief_sprite+i*4,raw);
                capture->relief_sprite[i*4+3]=255;
                capture->relief_owner[i]=(uint8_t)owner;capture->relief_pixels[owner-1]++;
            }
        }
    }
}
extern void __real_Dkc1BabyKongPrepareFrame(Ppu *,const uint8_t *,int);
void __wrap_Dkc1BabyKongPrepareFrame(Ppu *ppu,const uint8_t *ram,int bias) {
    __real_Dkc1BabyKongPrepareFrame(ppu,ram,bias);
    if (!capture) return;
    // The ordinary preparation clears the inert mod's OBJ binding every frame.
    (void)PpuBindOverlaySurface(ppu,kPpuOverlaySource_Obj,capture->source[4],SYLAR_WIDTH*4);
    (void)PpuSetOverlayCapture(ppu,kPpuOverlaySource_Obj,-EXTRA,0,256+EXTRA,SYLAR_HEIGHT,0);
    (void)PpuSetOverlayOamRange(ppu,0,128);
    select_kongs(ppu,ram);
}
extern void __real_Dkc1DrawPpuFrame(void);
void __wrap_Dkc1DrawPpuFrame(void) {
    if (!capture) {
        PpuClearOverlayBindings(g_ppu);
        __real_Dkc1DrawPpuFrame(); return;
    }
    memset(capture,0,sizeof(*capture));
    for (unsigned source=0;source<4;source++) {
        (void)PpuBindOverlaySurface(g_ppu,(PpuOverlaySource)source,capture->source[source],SYLAR_WIDTH*4);
        (void)PpuSetOverlayCapture(g_ppu,(PpuOverlaySource)source,-EXTRA,0,256+EXTRA,SYLAR_HEIGHT,0);
    }
    __real_Dkc1DrawPpuFrame();
    sylar_bgra_to_rgba(capture->rgba[0],capture->backdrop,SYLAR_WIDTH*SYLAR_HEIGHT);
    for (size_t pixel=0;pixel<SYLAR_WIDTH*SYLAR_HEIGHT;pixel++) {
        unsigned best[SYLAR_DEPTH_PLANES]={0};
        for (unsigned source=0;source<5;source++) {
            const uint8_t *color=capture->source[source]+pixel*4;
            if (!color[3]) continue;
            unsigned priority=capture->priority[source][pixel], group=depth_group(priority);
            if (priority>=best[group]) { rgba_pixel(capture->rgba[group]+pixel*4,color); best[group]=priority; }
        }
        // Retain exact native color math/windows at the winning native depth.
        // Lower hidden planes retain their authored pixels for head parallax.
        unsigned winner=depth_group(capture->winner[pixel]);
        const uint8_t *raw=raw_pixel(g_ppu,(unsigned)(pixel/SYLAR_WIDTH),(unsigned)(pixel%SYLAR_WIDTH));
        if(relief_enabled){
            uint8_t *background=capture->relief_background+pixel*4;
            if(!capture->relief_owner[pixel])rgba_pixel(background,raw);
            // The PPU's fourth BGRA byte is padding, not an opacity channel.
            background[3]=255;
        }
        rgba_pixel(capture->rgba[winner]+pixel*4,raw);
        capture->rgba[winner][pixel*4+3]=255;
        for (unsigned plane=winner+1;plane<SYLAR_DEPTH_PLANES;plane++) memset(capture->rgba[plane]+pixel*4,0,4);
        const uint8_t *composite=capture->rgba[0]+pixel*4;
        for (unsigned plane=0;plane<SYLAR_DEPTH_PLANES;plane++) {
            const uint8_t *p=capture->rgba[plane]+pixel*4;
            if (p[3]) { capture->occupied[plane]++; composite=p; }
        }
        if (composite[0]!=raw[2] || composite[1]!=raw[1] || composite[2]!=raw[0]) capture->composite_mismatches++;
    }
    if (on_capture) on_capture(capture);
}
