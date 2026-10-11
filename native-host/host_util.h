// Copyright 2026 Sylar.soluciones. SPDX-License-Identifier: MIT
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>

enum { SYLAR_WIDTH=342, SYLAR_HEIGHT=224, SYLAR_RGBA_BYTES=342*224*4,
       SYLAR_PCM_CAPACITY=8192 };
#define SYLAR_GAME_HZ 60.098811862
typedef struct SylarPcmRing {
    _Atomic uint32_t read_index, write_index;
    int16_t stereo[SYLAR_PCM_CAPACITY*2];
} SylarPcmRing;
// Exactly one producer/consumer. Reset only with the audio callback stopped.
void sylar_pcm_reset(SylarPcmRing *ring);
size_t sylar_pcm_push(SylarPcmRing *ring, const int16_t *data, size_t frames);
size_t sylar_pcm_pull(SylarPcmRing *ring, int16_t *data, size_t frames);
uint32_t sylar_touch_mask(float x, float y, bool a, bool b, bool select, bool start);
void sylar_bgra_to_rgba(uint8_t *target, const uint8_t *source, size_t pixels);
// Rebase excessive debt instead of simulating time spent outside the app.
double sylar_next_deadline(double previous, double now);
