// Copyright 2026 Sylar.soluciones. SPDX-License-Identifier: MIT
#pragma once
#include <GLES3/gl3.h>
#include "frame_packet.h"
typedef struct SylarRelief {
 GLuint program,vao,vertices,screen,background,sprite,framebuffer,resolve,color,depth,planes[5];
 GLuint epx_program,epx_framebuffer,epx_source,epx_mid;
 unsigned texture_width[7];bool upload_failed;
 GLuint reconstruction_program,reconstruction_source;bool reconstruction_enabled,reconstruction_active;
 GLint vp,texture_uniform,lighting;unsigned width,height,samples,count,plane_mask;
 float *mesh,*height_field;uint16_t *distance;uint8_t *scratch,*upscaled,*plane_pixels,*crop,*surface;
 float maximum_depth,minimum_depth,anchor_error;uint64_t sequence;bool cached,relief,smooth,layered;
} SylarRelief;
bool sylar_relief_init(SylarRelief *,unsigned,unsigned,GLenum);
bool sylar_relief_draw(SylarRelief *,GLuint,const float[16],const SylarFrame *,bool,bool);
void sylar_relief_destroy(SylarRelief *);

void sylar_relief_reconstruction(SylarRelief *,bool);
