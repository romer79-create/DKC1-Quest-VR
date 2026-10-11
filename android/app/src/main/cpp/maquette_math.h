// Copyright 2026 Sylar.soluciones. SPDX-License-Identifier: MIT
#pragma once
#include <stdbool.h>
void sylar_identity(float m[16]);
void sylar_multiply(float out[16],const float a[16],const float b[16]);
void sylar_transform(float out[16],const float t[3],const float q[4],const float s[3]);
void sylar_view(float out[16],const float t[3],const float q[4]);
bool sylar_projection(float out[16],float left,float right,float down,float up,float near_z,float far_z);
void sylar_normal(float out[9],const float m[16]);
