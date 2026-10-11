// Copyright 2026 Sylar.soluciones. SPDX-License-Identifier: MIT
#pragma once
#include "host_util.h"
#include "frame_packet.h"
typedef struct SylarGame SylarGame;
enum SylarGameStatus { SYLAR_LOADING, SYLAR_READY, SYLAR_ROM_MISSING,
    SYLAR_ROM_INVALID, SYLAR_SAVE_ERROR, SYLAR_CORE_ERROR };
SylarGame *sylar_game_start(const char *private_files);
void sylar_game_state(SylarGame *game,bool focused,uint32_t inputs);
enum SylarGameStatus sylar_game_copy(SylarGame *game,uint8_t *rgba);
// Pauses and releases the XR attachment; the engine survives in this process.
void sylar_game_detach(SylarGame *game);
void sylar_game_diorama(SylarGame *game,bool enabled);
void sylar_game_relief(SylarGame *game,bool enabled);
enum SylarGameStatus sylar_game_frame(SylarGame *game,SylarFrame *frame);
