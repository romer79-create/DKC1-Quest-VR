// Copyright 2026 Sylar.soluciones. SPDX-License-Identifier: MIT
#pragma once
#include "host_util.h"
#define SYLAR_DEPTH_PLANES 5
typedef struct SylarFrame {
    uint8_t rgba[SYLAR_RGBA_BYTES];
    uint8_t planes[SYLAR_DEPTH_PLANES][SYLAR_RGBA_BYTES];
    uint32_t occupied[SYLAR_DEPTH_PLANES];
    uint64_t sequence;
    bool layers_valid;
    uint8_t relief_background[SYLAR_RGBA_BYTES];
    uint8_t relief_sprite[SYLAR_RGBA_BYTES];
    uint8_t relief_owner[SYLAR_WIDTH*SYLAR_HEIGHT];
    uint32_t relief_pixels[2];
    bool relief_valid;
} SylarFrame;
