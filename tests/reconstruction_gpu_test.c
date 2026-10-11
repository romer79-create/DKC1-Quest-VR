// Copyright 2026 Sylar.soluciones. SPDX-License-Identifier: MIT
// Exercises actual packaged renderer against actual native first-level frames.
#include "closed_relief.h"
#include "maquette_math.h"
#include <EGL/egl.h>
#include <dlfcn.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"Live check failed: %s line %d EGL=%x GL=%x\n",#x,__LINE__,eglGetError(),glGetError());return 1;}}while(0)
static bool read_file(const char *path,void *data,size_t size){FILE *f=fopen(path,"rb");if(!f)return false;bool ok=fread(data,1,size,f)==size && fgetc(f)==EOF;return fclose(f)==0 && ok;}
static double now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec/1e9;}
int main(void){
 void *lib=dlopen("./libsylar_quest_game.so",RTLD_NOW|RTLD_LOCAL);if(!lib){fprintf(stderr,"dlopen: %s\n",dlerror());return 1;}
 bool(*init)(SylarRelief *,unsigned,unsigned,GLenum)=dlsym(lib,"sylar_relief_init");bool(*draw)(SylarRelief *,GLuint,const float *,const SylarFrame *,bool,bool)=dlsym(lib,"sylar_relief_draw");void(*destroy)(SylarRelief *)=dlsym(lib,"sylar_relief_destroy");void(*view)(float *,const float *,const float *)=dlsym(lib,"sylar_view");void(*multiply)(float *,const float *,const float *)=dlsym(lib,"sylar_multiply");bool(*projection)(float *,float,float,float,float,float,float)=dlsym(lib,"sylar_projection");void(*reconstruct)(SylarRelief *,bool)=dlsym(lib,"sylar_relief_reconstruction");CHECK(init && draw && destroy && view && multiply && projection && reconstruct);
 EGLDisplay display=eglGetDisplay(EGL_DEFAULT_DISPLAY);CHECK(display!=EGL_NO_DISPLAY && eglInitialize(display,NULL,NULL) && eglBindAPI(EGL_OPENGL_ES_API));const EGLint attrs[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_ES3_BIT,EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_NONE};EGLConfig cfg;EGLint count=0;CHECK(eglChooseConfig(display,attrs,&cfg,1,&count) && count==1);
 const EGLint ca[]={EGL_CONTEXT_CLIENT_VERSION,3,EGL_NONE},sa[]={EGL_WIDTH,1,EGL_HEIGHT,1,EGL_NONE};EGLContext context=eglCreateContext(display,cfg,EGL_NO_CONTEXT,ca);EGLSurface surface=eglCreatePbufferSurface(display,cfg,sa);CHECK(context!=EGL_NO_CONTEXT && surface!=EGL_NO_SURFACE && eglMakeCurrent(display,surface,surface,context));
 printf("GPU: %s\n",glGetString(GL_RENDERER));enum{W=960,H=720};SylarRelief renderer={0};CHECK(init(&renderer,W,H,GL_SRGB8_ALPHA8) && renderer.samples==4);
 GLuint target;glGenTextures(1,&target);glBindTexture(GL_TEXTURE_2D,target);glTexStorage2D(GL_TEXTURE_2D,1,GL_SRGB8_ALPHA8,W,H);uint8_t *pixels=malloc(W*H*4);SylarFrame *f=malloc(sizeof(*f));CHECK(pixels && f);
 unsigned fixtures=0,diddy=UINT32_MAX;for(unsigned k=0;k<12;k++){char name[64];snprintf(name,sizeof(name),"fixture-%u.bin",k);if(!read_file(name,f,sizeof(*f)))break;fixtures++;if(f->relief_pixels[1]>=300 && diddy==UINT32_MAX)diddy=k;}CHECK(fixtures>=6 && diddy!=UINT32_MAX);
 uint64_t hashes[48]={0};
 for(unsigned test=0;test<46;test++){
  unsigned fixture=test>=24 && test<36?test-24:test>=12 && test<24?test-12:test==38 || test==39?diddy:test>=5 && test<=7?diddy:0;char name[64];snprintf(name,sizeof(name),"fixture-%u.bin",fixture);CHECK(read_file(name,f,sizeof(*f)));
  CHECK(f->layers_valid && f->relief_valid);bool enabled=test!=0 && test!=8 && test!=10,smooth=test!=4 && test!=44;f->sequence=test+1;if(test==9 || test==10 || test==40)f->layers_valid=false;if(test==41)f->relief_valid=false;
  if(test==11 || test==42){destroy(&renderer);CHECK(init(&renderer,W,H,GL_SRGB8_ALPHA8));}
  const float eye[3]={test==2 || test==6 || test==36 || test==38?.032f:test==3 || test==7 || test==37 || test==39?.22f:-.032f,0,0},q[4]={0,0,0,1};float p[16],v[16],vp[16];CHECK(projection(p,-.66f,.66f,-.45f,.45f,.05f,20));view(v,eye,q);multiply(vp,p,v);
  reconstruct(&renderer,test>=24 && test!=43);
  double start=now();CHECK(draw(&renderer,target,vp,f,enabled,smooth));glFinish();double elapsed=now()-start;
  if(enabled && f->layers_valid && f->relief_valid){
   CHECK(renderer.relief && renderer.layered && renderer.count>10000 && renderer.maximum_depth>.03f && renderer.maximum_depth<=.13501f && renderer.minimum_depth<-.03f && renderer.anchor_error<.000001f);
   for(unsigned i=0;i<renderer.count*8;i++)CHECK(isfinite(renderer.mesh[i]));
   for(unsigned i=0;i<SYLAR_WIDTH*SYLAR_HEIGHT;i++){const uint8_t *composite=NULL;for(unsigned plane=0;plane<5;plane++){const uint8_t *part=renderer.plane_pixels+plane*SYLAR_RGBA_BYTES+i*4;if(part[3])composite=part;}CHECK(composite && !memcmp(composite,f->relief_background+i*4,3));}
   if(test==1 || (test>=12 && test<24)){snprintf(name,sizeof(name),test==1?"live-mesh.bin":"mesh-%u.bin",fixture);FILE *out=fopen(name,"wb");CHECK(out && fwrite(&renderer.count,sizeof(renderer.count),1,out)==1 && fwrite(renderer.mesh,8*sizeof(float),renderer.count,out)==renderer.count && !fclose(out));}
  }else CHECK(!renderer.relief && renderer.count==0);
  if(test>=24 && test<36){snprintf(name,sizeof(name),"reconstructed-mesh-%u.bin",fixture);FILE *mesh=fopen(name,"wb");CHECK(mesh && fwrite(&renderer.count,4,1,mesh)==1 && fwrite(renderer.mesh,8*sizeof(float),renderer.count,mesh)==renderer.count && !fclose(mesh));}
  glBindFramebuffer(GL_FRAMEBUFFER,renderer.resolve);glReadPixels(0,0,W,H,GL_RGBA,GL_UNSIGNED_BYTE,pixels);CHECK(glGetError()==GL_NO_ERROR);uint64_t hash=UINT64_C(14695981039346656037);unsigned foreground=0;
  for(unsigned i=0;i<W*H;i++){CHECK(pixels[i*4+3]==255);if(abs((int)pixels[i*4]-pixels[0])+abs((int)pixels[i*4+1]-pixels[1])+abs((int)pixels[i*4+2]-pixels[2])>18)foreground++;for(unsigned k=0;k<4;k++){hash^=pixels[i*4+k];hash*=UINT64_C(1099511628211);}}
  CHECK(foreground>150000);hashes[test]=hash;if(test<8 || (test>=24 && test<40)){snprintf(name,sizeof(name),"live-%u.rgba",test);FILE *out=fopen(name,"wb");CHECK(out && fwrite(pixels,1,W*H*4,out)==W*H*4 && !fclose(out));}
  printf("case=%u fixture=%u relief=%d layered=%d vertices=%u depth=%.6f anchor_error=%.9f hash=%016"PRIx64" elapsed_ms=%.3f\n",test,fixture,renderer.relief,renderer.layered,renderer.count,renderer.maximum_depth,renderer.anchor_error,hash,elapsed*1000);fflush(stdout);
 }
 CHECK(hashes[0]!=hashes[1] && hashes[1]!=hashes[2] && hashes[1]!=hashes[3] && hashes[1]!=hashes[4]);CHECK(hashes[5]!=hashes[6] && hashes[5]!=hashes[7]);CHECK(hashes[0]==hashes[8] && hashes[9]==hashes[10] && hashes[1]==hashes[11]);
 CHECK(hashes[24]!=hashes[1] && hashes[24]!=hashes[36] && hashes[24]!=hashes[37] && hashes[30]!=hashes[38] && hashes[30]!=hashes[39]);
 CHECK(hashes[40]==hashes[9] && hashes[41]==hashes[0] && hashes[42]==hashes[24] && hashes[43]==hashes[1] && hashes[44]==hashes[4] && hashes[45]==hashes[24]);
 // Meaningful texture oracles: constant colours, known dither mean, outline
 // contrast and exact fallback around transparent sprite boundaries.
 enum{TW=SYLAR_WIDTH*4,TH=SYLAR_HEIGHT*4};uint8_t *texture_pixels=malloc(TW*TH*4),*previous=malloc(TW*TH*4);CHECK(texture_pixels && previous);
 for(unsigned pattern=0;pattern<4;pattern++){
  memset(f,0,sizeof(*f));f->layers_valid=f->relief_valid=true;f->sequence=1000+pattern;
  for(unsigned y=0;y<SYLAR_HEIGHT;y++)for(unsigned x=0;x<SYLAR_WIDTH;x++){
   uint8_t *p=f->planes[0]+((size_t)y*SYLAR_WIDTH+x)*4;
   if(pattern==0){p[0]=82;p[1]=136;p[2]=190;p[3]=255;}
   if(pattern==1){p[0]=p[1]=p[2]=(x+y)&1?192:64;p[3]=255;}
   if(pattern==2){p[0]=p[1]=p[2]=x<SYLAR_WIDTH/2?0:255;p[3]=255;}
   if(pattern==3 && x==SYLAR_WIDTH/2 && y==SYLAR_HEIGHT/2){p[0]=255;p[1]=60;p[2]=30;p[3]=255;}
  }
  float p[16],v[16],vp[16];const float eye[3]={0,0,0},q[4]={0,0,0,1};CHECK(projection(p,-.66f,.66f,-.45f,.45f,.05f,20));view(v,eye,q);multiply(vp,p,v);
  reconstruct(&renderer,false);CHECK(draw(&renderer,target,vp,f,true,true));glFinish();glBindFramebuffer(GL_FRAMEBUFFER,renderer.resolve);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,renderer.planes[0],0);CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);glReadPixels(0,0,TW,TH,GL_RGBA,GL_UNSIGNED_BYTE,previous);
  reconstruct(&renderer,true);CHECK(draw(&renderer,target,vp,f,true,true));glFinish();glBindFramebuffer(GL_FRAMEBUFFER,renderer.resolve);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,renderer.planes[0],0);glReadPixels(0,0,TW,TH,GL_RGBA,GL_UNSIGNED_BYTE,texture_pixels);CHECK(glGetError()==GL_NO_ERROR);
  if(pattern==3)CHECK(!memcmp(previous,texture_pixels,TW*TH*4));
  for(unsigned y=12;y<TH-12;y++)for(unsigned x=12;x<TW-12;x++){
   const uint8_t *a=texture_pixels+((size_t)y*TW+x)*4;
   if(pattern==0){CHECK(abs((int)a[0]-82)<=1 && abs((int)a[1]-136)<=1 && abs((int)a[2]-190)<=1 && a[3]==255);}
   if(pattern==1){CHECK(abs((int)a[0]-128)<=1 && abs((int)a[1]-128)<=1 && abs((int)a[2]-128)<=1 && a[3]==255);}
   if(pattern==2 && abs((int)x-TW/2)>12){int value=x<TW/2?0:255;CHECK(abs((int)a[0]-value)<=1 && abs((int)a[1]-value)<=1 && abs((int)a[2]-value)<=1);}
  }
  printf("texture_oracle=%u PASS\n",pattern);fflush(stdout);
 }
 free(texture_pixels);free(previous);
 destroy(&renderer);glDeleteTextures(1,&target);CHECK(init(&renderer,1680,1760,GL_SRGB8_ALPHA8));glGenTextures(1,&target);glBindTexture(GL_TEXTURE_2D,target);glTexStorage2D(GL_TEXTURE_2D,1,GL_SRGB8_ALPHA8,1680,1760);CHECK(read_file("fixture-0.bin",f,sizeof(*f)));float p[16],v[16],vp[16];const float eye[3]={-.032f,0,0},q[4]={0,0,0,1};CHECK(projection(p,-.7f,.7f,-.75f,.75f,.05f,20));view(v,eye,q);multiply(vp,p,v);CHECK(draw(&renderer,target,vp,f,true,true));glFinish();CHECK(glGetError()==GL_NO_ERROR);
 reconstruct(&renderer,true);
 double samples[12];
 for(unsigned k=0;k<12;k++){
  char file[64];snprintf(file,sizeof(file),"fixture-%u.bin",k);CHECK(read_file(file,f,sizeof(*f)));f->sequence=100+k;double start=now();
  for(unsigned i=0;i<2;i++){float offset[3]={i?.032f:-.032f,0,0};view(v,offset,q);multiply(vp,p,v);CHECK(draw(&renderer,target,vp,f,true,true));}glFinish();samples[k]=(now()-start)*1000;
 }
 double total=0,maximum=0;for(unsigned k=0;k<12;k++){total+=samples[k];if(samples[k]>maximum)maximum=samples[k];}
 printf("Stereo workload 1680x1760: samples=12 mean_ms=%.3f maximum_ms=%.3f (not sustained app FPS)\n",total/12,maximum);
 destroy(&renderer);glDeleteTextures(1,&target);free(f);free(pixels);
 CHECK(eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT));CHECK(eglDestroySurface(display,surface) && eglDestroyContext(display,context) && eglTerminate(display));puts("Reconstruction GPU: PASS (actual native scenes, layered background, DK/Diddy, stereo, anchor, fallback and recreation)");return 0;
}
