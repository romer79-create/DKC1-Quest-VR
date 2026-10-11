// Copyright 2026 Sylar.soluciones. SPDX-License-Identifier: MIT
#pragma once
#include "host_util.h"
#include "frame_packet.h"
#include "snes/ppu.h"
typedef struct SylarLayers {
    uint8_t source[5][SYLAR_RGBA_BYTES]; // Pinned PPU ARGB/BGRA captures.
    uint8_t priority[5][SYLAR_WIDTH*SYLAR_HEIGHT];
    uint8_t winner[SYLAR_WIDTH*SYLAR_HEIGHT];
    uint8_t backdrop[SYLAR_RGBA_BYTES];
    uint8_t rgba[SYLAR_DEPTH_PLANES][SYLAR_RGBA_BYTES];
    uint32_t occupied[SYLAR_DEPTH_PLANES];
    uint32_t composite_mismatches;
    uint32_t unsupported_lines;
    uint8_t oam_owner[128];
    uint8_t obj_owner[SYLAR_WIDTH*SYLAR_HEIGHT];
    uint16_t obj_color[SYLAR_WIDTH*SYLAR_HEIGHT];
    uint8_t other_obj[SYLAR_RGBA_BYTES];
    uint8_t other_priority[SYLAR_WIDTH*SYLAR_HEIGHT];
    uint8_t relief_background[SYLAR_RGBA_BYTES];
    uint8_t relief_sprite[SYLAR_RGBA_BYTES];
    uint8_t relief_owner[SYLAR_WIDTH*SYLAR_HEIGHT];
    uint32_t relief_pixels[2];
    unsigned selected_oam[2];
} SylarLayers;
void sylar_layers_select(SylarLayers *layers);
void sylar_layers_observer(void (*observer)(const SylarLayers *));
// Read-only callbacks from a private, minimally adapted copy of pinned ppu.c.
void sylar_capture_bg_line(Ppu *ppu,unsigned source,unsigned y);
void sylar_capture_final_line(Ppu *ppu,unsigned y);
void sylar_relief_select(bool enabled);
void sylar_relief_obj_pixel(Ppu *ppu,unsigned y,int x,unsigned slot,uint16_t color);

// Center stock frames in the fixed wide packet without inventing side pixels.
void sylar_capture_raw_rgba(uint8_t *,const Ppu *);
