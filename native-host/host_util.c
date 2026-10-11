// Copyright 2026 Sylar.soluciones. SPDX-License-Identifier: MIT
#include "host_util.h"
#include <string.h>
#include <math.h>
#ifdef __ANDROID__
_Static_assert(ATOMIC_INT_LOCK_FREE==2,"Audio ring indices must be lock-free");
#endif

void sylar_pcm_reset(SylarPcmRing *r) {
    atomic_store(&r->read_index,0); atomic_store(&r->write_index,0);
}
size_t sylar_pcm_push(SylarPcmRing *r,const int16_t *data,size_t count) {
    uint32_t w=atomic_load_explicit(&r->write_index,memory_order_relaxed);
    uint32_t n=w-atomic_load_explicit(&r->read_index,memory_order_acquire);
    size_t take=count < SYLAR_PCM_CAPACITY-n ? count : SYLAR_PCM_CAPACITY-n;
    for (size_t i=0;i<take;i++) {
        size_t at=((w+(uint32_t)i)&(SYLAR_PCM_CAPACITY-1))*2;
        r->stereo[at]=data[i*2]; r->stereo[at+1]=data[i*2+1];
    }
    atomic_store_explicit(&r->write_index,w+(uint32_t)take,memory_order_release);
    return take;
}
size_t sylar_pcm_pull(SylarPcmRing *r,int16_t *data,size_t count) {
    uint32_t pos=atomic_load_explicit(&r->read_index,memory_order_relaxed);
    uint32_t n=atomic_load_explicit(&r->write_index,memory_order_acquire)-pos;
    size_t take=count<n ? count:n;
    for (size_t i=0;i<take;i++) {
        size_t at=((pos+(uint32_t)i)&(SYLAR_PCM_CAPACITY-1))*2;
        data[i*2]=r->stereo[at]; data[i*2+1]=r->stereo[at+1];
    }
    memset(data+take*2,0,(count-take)*2*sizeof(*data));
    atomic_store_explicit(&r->read_index,pos+(uint32_t)take,memory_order_release);
    return take;
}
uint32_t sylar_touch_mask(float x,float y,bool a,bool b,bool select,bool start) {
    uint32_t bits=(a?1u:0u)|(b?2u:0u)|(select?4u:0u)|(start?8u:0u);
    if (isfinite(x) && isfinite(y)) {
        if (y>0.3f) bits|=0x10; if (y< -0.3f) bits|=0x20;
        if (x< -0.3f) bits|=0x40; if (x>0.3f) bits|=0x80;
    }
    return bits;
}
void sylar_bgra_to_rgba(uint8_t *out,const uint8_t *in,size_t pixels) {
    for (size_t i=0;i<pixels;i++) {
        out[i*4]=in[i*4+2]; out[i*4+1]=in[i*4+1]; out[i*4+2]=in[i*4]; out[i*4+3]=255;
    }
}
double sylar_next_deadline(double previous,double now) {
    const double period=1.0/SYLAR_GAME_HZ;
    double next=previous+period;
    if (now-next>period*2.0) next=now+period;
    return next;
}
