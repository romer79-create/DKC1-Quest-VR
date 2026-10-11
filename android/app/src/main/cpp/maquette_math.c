// Copyright 2026 Sylar.soluciones. SPDX-License-Identifier: MIT
#include "maquette_math.h"
#include <math.h>
#include <string.h>
void sylar_identity(float m[16]){memset(m,0,16*sizeof(float));m[0]=m[5]=m[10]=m[15]=1;}
void sylar_multiply(float out[16],const float a[16],const float b[16]){
    float v[16];
    for(unsigned col=0;col<4;col++)for(unsigned row=0;row<4;row++){
        v[col*4+row]=0;for(unsigned k=0;k<4;k++)v[col*4+row]+=a[k*4+row]*b[col*4+k];
    }
    memcpy(out,v,sizeof(v));
}
void sylar_transform(float m[16],const float t[3],const float q[4],const float s[3]){
    const float x=q[0],y=q[1],z=q[2],w=q[3];sylar_identity(m);
    m[0]=(1-2*(y*y+z*z))*s[0];m[1]=2*(x*y+z*w)*s[0];m[2]=2*(x*z-y*w)*s[0];
    m[4]=2*(x*y-z*w)*s[1];m[5]=(1-2*(x*x+z*z))*s[1];m[6]=2*(y*z+x*w)*s[1];
    m[8]=2*(x*z+y*w)*s[2];m[9]=2*(y*z-x*w)*s[2];m[10]=(1-2*(x*x+y*y))*s[2];
    m[12]=t[0];m[13]=t[1];m[14]=t[2];
}
void sylar_view(float out[16],const float t[3],const float q[4]){
    const float inverse_q[4]={-q[0],-q[1],-q[2],q[3]},zero[3]={0,0,0},one[3]={1,1,1};
    sylar_transform(out,zero,inverse_q,one);
    for(unsigned row=0;row<3;row++)out[12+row]=-(out[row]*t[0]+out[4+row]*t[1]+out[8+row]*t[2]);
}
bool sylar_projection(float m[16],float left,float right,float down,float up,float near_z,float far_z){
    float l=tanf(left),r=tanf(right),d=tanf(down),u=tanf(up);
    if(!isfinite(l+r+d+u) || r<=l || u<=d || near_z<=0 || far_z<=near_z)return false;
    memset(m,0,16*sizeof(float));m[0]=2/(r-l);m[5]=2/(u-d);
    m[8]=(r+l)/(r-l);m[9]=(u+d)/(u-d);
    m[10]=-(far_z+near_z)/(far_z-near_z);m[11]=-1;
    m[14]=-2*far_z*near_z/(far_z-near_z);return true;
}
void sylar_normal(float out[9],const float m[16]){
    const float a=m[0],b=m[4],c=m[8],d=m[1],e=m[5],f=m[9],g=m[2],h=m[6],i=m[10];
    const float det=a*(e*i-f*h)-b*(d*i-f*g)+c*(d*h-e*g);
    out[0]=(e*i-f*h)/det;out[1]=(c*h-b*i)/det;out[2]=(b*f-c*e)/det;
    out[3]=(f*g-d*i)/det;out[4]=(a*i-c*g)/det;out[5]=(c*d-a*f)/det;
    out[6]=(d*h-e*g)/det;out[7]=(b*g-a*h)/det;out[8]=(a*e-b*d)/det;
}
