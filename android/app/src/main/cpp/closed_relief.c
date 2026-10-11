// Copyright 2026 Sylar.soluciones. SPDX-License-Identifier: MIT
// Live closed silhouettes on the original backend's true 16:9 scene.
#include "closed_relief.h"
#include "reconstruction_shader.h"
#include <android/log.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#define LIMIT 300000u
#define STRIDE 8u
#define CROP 96
#define SURFACE (CROP*2)
#define GRID (SURFACE+1)
#define WIDTH SYLAR_WIDTH
#define HEIGHT SYLAR_HEIGHT
#define SCENE_HEIGHT 1.9125f
#define SCENE_WIDTH (SCENE_HEIGHT*(float)WIDTH*7.f/(6.f*(float)HEIGHT))
#define SCENE_DEPTH 2.55f
static const char vs[]="#version 300 es\nlayout(location=0)in vec3 position;layout(location=1)in vec3 normal;layout(location=2)in vec2 uv;uniform mat4 view_projection;out vec3 n;out vec2 t;void main(){n=normal;t=uv;gl_Position=view_projection*vec4(position,1);}";
static const char fs[]="#version 300 es\nprecision highp float;in vec3 n;in vec2 t;uniform sampler2D image;uniform float lighting;out vec4 color;void main(){vec2 size=vec2(textureSize(image,0));vec2 pixel=t*size;vec2 footprint=max(fwidth(pixel),vec2(.0001));vec2 center=floor(pixel)+.5;vec2 sample_at=center+clamp((fract(pixel)-.5)/min(footprint,vec2(1)),-.5,.5);vec4 p=texture(image,sample_at/size);if(p.a<.05)discard;vec3 normal=normalize(n);float key=max(dot(normal,normalize(vec3(-.5,.75,.8))),0.);float fill=max(dot(normal,normalize(vec3(.8,.25,-.5))),0.);vec3 light=vec3(.58)+vec3(.46,.43,.39)*key+vec3(.12,.14,.19)*fill;color=vec4(p.rgb*mix(vec3(1),light,lighting),1);}";

static const char epx_vs[]="#version 300 es\nvoid main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);gl_Position=vec4(p*2.-1.,0,1);}";
static const char epx_fs[]="#version 300 es\nprecision highp float;precision highp int;uniform sampler2D image;out vec4 color;vec4 read_at(ivec2 p){return texelFetch(image,clamp(p,ivec2(0),textureSize(image,0)-1),0);}void main(){ivec2 dst=ivec2(gl_FragCoord.xy),p=dst/2;vec4 e=read_at(p),b=read_at(p+ivec2(0,-1)),d=read_at(p+ivec2(-1,0)),f=read_at(p+ivec2(1,0)),h=read_at(p+ivec2(0,1));color=e;if(!all(equal(b,h)) && !all(equal(d,f))){bool left=(dst.x&1)==0,top=(dst.y&1)==0;if(left && top && all(equal(d,b)))color=d;else if(!left && top && all(equal(b,f)))color=f;else if(left && !top && all(equal(d,h)))color=d;else if(!left && !top && all(equal(h,f)))color=f;}}";

static GLuint shader(GLenum type,const char *source){
 GLuint s=glCreateShader(type);glShaderSource(s,1,&source,NULL);glCompileShader(s);GLint ok=0;glGetShaderiv(s,GL_COMPILE_STATUS,&ok);
 if(!ok){char log[2048];glGetShaderInfoLog(s,sizeof(log),NULL,log);__android_log_print(ANDROID_LOG_ERROR,"SylarRelief","Shader: %s",log);glDeleteShader(s);return 0;}return s;
}
static void texture(GLuint *out){glGenTextures(1,out);glBindTexture(GL_TEXTURE_2D,*out);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);}
static void attributes(void){
 glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,STRIDE*sizeof(float),(void *)0);glEnableVertexAttribArray(0);
 glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,STRIDE*sizeof(float),(void *)(3*sizeof(float)));glEnableVertexAttribArray(1);
 glVertexAttribPointer(2,2,GL_FLOAT,GL_FALSE,STRIDE*sizeof(float),(void *)(6*sizeof(float)));glEnableVertexAttribArray(2);
}
bool sylar_relief_init(SylarRelief *r,unsigned width,unsigned height,GLenum format){
 memset(r,0,sizeof(*r));r->width=width;r->height=height;
 r->mesh=malloc(LIMIT*STRIDE*sizeof(float));r->scratch=malloc(WIDTH*HEIGHT*16);r->upscaled=malloc(WIDTH*HEIGHT*64);
 r->distance=malloc(SURFACE*SURFACE*sizeof(uint16_t));r->height_field=malloc(GRID*GRID*sizeof(float));
 r->plane_pixels=malloc(5*SYLAR_RGBA_BYTES);r->crop=malloc(CROP*CROP*4);r->surface=malloc(SURFACE*SURFACE*4);
 if(!r->mesh || !r->scratch || !r->upscaled || !r->distance || !r->height_field || !r->plane_pixels || !r->crop || !r->surface)return false;
 GLuint v=shader(GL_VERTEX_SHADER,vs),f=shader(GL_FRAGMENT_SHADER,fs);if(!v || !f){glDeleteShader(v);glDeleteShader(f);return false;}
 r->program=glCreateProgram();glAttachShader(r->program,v);glAttachShader(r->program,f);glLinkProgram(r->program);glDeleteShader(v);glDeleteShader(f);GLint ok=0;glGetProgramiv(r->program,GL_LINK_STATUS,&ok);if(!ok)return false;
 r->vp=glGetUniformLocation(r->program,"view_projection");r->texture_uniform=glGetUniformLocation(r->program,"image");r->lighting=glGetUniformLocation(r->program,"lighting");
 glGenVertexArrays(1,&r->vao);glBindVertexArray(r->vao);glGenBuffers(1,&r->vertices);glBindBuffer(GL_ARRAY_BUFFER,r->vertices);attributes();glGenBuffers(1,&r->screen);
 texture(&r->background);texture(&r->sprite);for(unsigned i=0;i<5;i++)texture(&r->planes[i]);

 GLuint ev=shader(GL_VERTEX_SHADER,epx_vs),ef=shader(GL_FRAGMENT_SHADER,epx_fs);if(!ev || !ef){glDeleteShader(ev);glDeleteShader(ef);return false;}
 r->epx_program=glCreateProgram();glAttachShader(r->epx_program,ev);glAttachShader(r->epx_program,ef);glLinkProgram(r->epx_program);glDeleteShader(ev);glDeleteShader(ef);glGetProgramiv(r->epx_program,GL_LINK_STATUS,&ok);if(!ok)return false;
 texture(&r->epx_source);glBindTexture(GL_TEXTURE_2D,r->epx_source);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);glTexImage2D(GL_TEXTURE_2D,0,GL_SRGB8_ALPHA8,WIDTH,HEIGHT,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
 texture(&r->epx_mid);glBindTexture(GL_TEXTURE_2D,r->epx_mid);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);glTexImage2D(GL_TEXTURE_2D,0,GL_SRGB8_ALPHA8,WIDTH*2,HEIGHT*2,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);

 GLuint rv=shader(GL_VERTEX_SHADER,epx_vs),rf=glCreateShader(GL_FRAGMENT_SHADER);if(!rv || !rf)return false;
 glShaderSource(rf,SYLAR_RECONSTRUCTION_PARTS,sylar_reconstruction_glsl,NULL);glCompileShader(rf);glGetShaderiv(rf,GL_COMPILE_STATUS,&ok);
 if(!ok){char log[2048];glGetShaderInfoLog(rf,sizeof(log),NULL,log);__android_log_print(ANDROID_LOG_ERROR,"SylarRelief","Reconstruct shader: %s",log);glDeleteShader(rv);glDeleteShader(rf);return false;}
 r->reconstruction_program=glCreateProgram();glAttachShader(r->reconstruction_program,rv);glAttachShader(r->reconstruction_program,rf);glLinkProgram(r->reconstruction_program);glDeleteShader(rv);glDeleteShader(rf);glGetProgramiv(r->reconstruction_program,GL_LINK_STATUS,&ok);if(!ok)return false;
 texture(&r->reconstruction_source);glBindTexture(GL_TEXTURE_2D,r->reconstruction_source);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,WIDTH,HEIGHT,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
 glGenFramebuffers(1,&r->epx_framebuffer);

 GLint max=0;glGetIntegerv(GL_MAX_SAMPLES,&max);r->samples=max>=4?4:1;
 glGenFramebuffers(1,&r->framebuffer);glBindFramebuffer(GL_FRAMEBUFFER,r->framebuffer);
 glGenRenderbuffers(1,&r->depth);glBindRenderbuffer(GL_RENDERBUFFER,r->depth);glRenderbufferStorageMultisample(GL_RENDERBUFFER,r->samples,GL_DEPTH_COMPONENT24,width,height);glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,r->depth);
 glGenRenderbuffers(1,&r->color);glBindRenderbuffer(GL_RENDERBUFFER,r->color);glRenderbufferStorageMultisample(GL_RENDERBUFFER,r->samples,format,width,height);glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_RENDERBUFFER,r->color);
 if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)return false;
 glGenFramebuffers(1,&r->resolve);glBindFramebuffer(GL_FRAMEBUFFER,0);glBindVertexArray(0);
 __android_log_print(ANDROID_LOG_INFO,"SylarRelief","Live closed relief: %ux%u, %ux MSAA; background 3.20m, ground/actors 2.55m; 135mm half-depth; 342x224 true widescreen, 3.407m x 1.913m; sharp GPU EPX4.",width,height,r->samples);return glGetError()==GL_NO_ERROR;
}
static const uint8_t *pixel(const uint8_t *src,int w,int h,int x,int y){x=x<0?0:x>=w?w-1:x;y=y<0?0:y>=h?h-1:y;return src+((size_t)y*w+x)*4;}
static bool equal(const uint8_t *a,const uint8_t *b){return !memcmp(a,b,4);}
static void epx(const uint8_t *src,uint8_t *dst,int w,int h){
 for(int y=0;y<h;y++)for(int x=0;x<w;x++){
  const uint8_t *e=pixel(src,w,h,x,y),*b=pixel(src,w,h,x,y-1),*d=pixel(src,w,h,x-1,y),*f=pixel(src,w,h,x+1,y),*h0=pixel(src,w,h,x,y+1);const uint8_t *p[4]={e,e,e,e};
  if(!equal(b,h0) && !equal(d,f)){if(equal(d,b))p[0]=d;if(equal(b,f))p[1]=f;if(equal(d,h0))p[2]=d;if(equal(h0,f))p[3]=f;}
  for(unsigned i=0;i<4;i++)memcpy(dst+((size_t)(y*2+(i>>1))*w*2+x*2+(i&1))*4,p[i],4);
 }
}
static void upload(SylarRelief *r,GLuint tex,const uint8_t *source,bool smooth){
 unsigned w=smooth?WIDTH*4:WIDTH,h=smooth?HEIGHT*4:HEIGHT;
 const GLuint ids[7]={r->background,r->sprite,r->planes[0],r->planes[1],r->planes[2],r->planes[3],r->planes[4]};unsigned slot=0;while(slot<7 && ids[slot]!=tex)slot++;if(slot==7){r->upload_failed=true;return;}
 glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,tex);
 glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,smooth?GL_LINEAR:GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,smooth?GL_LINEAR:GL_NEAREST);
 if(r->texture_width[slot]!=w){glTexImage2D(GL_TEXTURE_2D,0,GL_SRGB8_ALPHA8,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);r->texture_width[slot]=w;}
 if(!smooth){glTexSubImage2D(GL_TEXTURE_2D,0,0,0,w,h,GL_RGBA,GL_UNSIGNED_BYTE,source);return;}
 if(r->reconstruction_active){glBindTexture(GL_TEXTURE_2D,r->reconstruction_source);glTexSubImage2D(GL_TEXTURE_2D,0,0,0,WIDTH,HEIGHT,GL_RGBA,GL_UNSIGNED_BYTE,source);}
 glBindTexture(GL_TEXTURE_2D,r->epx_source);glTexSubImage2D(GL_TEXTURE_2D,0,0,0,WIDTH,HEIGHT,GL_RGBA,GL_UNSIGNED_BYTE,source);
 glBindFramebuffer(GL_FRAMEBUFFER,r->epx_framebuffer);glDisable(GL_DEPTH_TEST);glDisable(GL_BLEND);glDisable(GL_SCISSOR_TEST);glDisable(GL_CULL_FACE);glDisable(GL_DITHER);glUseProgram(r->epx_program);glUniform1i(glGetUniformLocation(r->epx_program,"image"),0);glBindVertexArray(r->vao);
 for(unsigned pass=0;pass<2;pass++){
  GLuint output=pass?tex:r->epx_mid,input=pass?r->epx_mid:r->epx_source;unsigned factor=pass?4:2;
  glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,output,0);
  if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE){r->upload_failed=true;break;}
  if(!pass && r->reconstruction_active){glUseProgram(r->reconstruction_program);glUniform1i(glGetUniformLocation(r->reconstruction_program,"image"),0);glUniform1i(glGetUniformLocation(r->reconstruction_program,"native_image"),1);glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,r->reconstruction_source);glActiveTexture(GL_TEXTURE0);}
  else{glUseProgram(r->epx_program);glUniform1i(glGetUniformLocation(r->epx_program,"image"),0);}
  glViewport(0,0,WIDTH*factor,HEIGHT*factor);glBindTexture(GL_TEXTURE_2D,input);glDrawArrays(GL_TRIANGLES,0,3);
 }
 glBindFramebuffer(GL_FRAMEBUFFER,0);
}
static bool occupied(const SylarRelief *r,int w,int h,int x,int y){return x>=0 && y>=0 && x<w && y<h && r->surface[((size_t)y*w+x)*4+3]>127;}
static void fields(SylarRelief *r,int w,int h){
 for(int y=0;y<h;y++)for(int x=0;x<w;x++){size_t i=(size_t)y*w+x;unsigned a=x?r->distance[i-1]:0,b=y?r->distance[i-w]:0;r->distance[i]=occupied(r,w,h,x,y)?(uint16_t)(1+(a<b?a:b)):0;}
 for(int y=h-1;y>=0;y--)for(int x=w-1;x>=0;x--){size_t i=(size_t)y*w+x;if(!r->distance[i])continue;unsigned a=x+1<w?r->distance[i+1]:0,b=y+1<h?r->distance[i+w]:0,d=1+(a<b?a:b);if(d<r->distance[i])r->distance[i]=(uint16_t)d;}
 memset(r->height_field,0,GRID*GRID*sizeof(float));
 for(int y=1;y<h;y++)for(int x=1;x<w;x++){unsigned d=UINT16_MAX;for(int yy=y-1;yy<=y;yy++)for(int xx=x-1;xx<=x;xx++){unsigned value=r->distance[yy*w+xx];if(value<d)d=value;}r->height_field[y*GRID+x]=.015f+.120f*sqrtf(fminf((float)d/18.f,1.f));}
}
static void emit(SylarRelief *r,int w,int h,int x,int y,int cx,int cy,int left,int top,bool back,const float *side){
 float xx=(float)x,yy=(float)y;unsigned a=occupied(r,w,h,x-1,y-1),b=occupied(r,w,h,x,y-1),c=occupied(r,w,h,x-1,y),d=occupied(r,w,h,x,y);
 if(a+b+c+d==2 && a==d){xx+=cx>=x?.12f:-.12f;yy+=cy>=y?.12f:-.12f;}
 float height=r->height_field[y*GRID+x]*(back?-1.f:1.f),gx=0,gy=0;
 if(!side){gx=(r->height_field[y*GRID+x+1]-r->height_field[y*GRID+x-1])/(2*(SCENE_WIDTH/(WIDTH*2.f)));gy=(r->height_field[(y-1)*GRID+x]-r->height_field[(y+1)*GRID+x])/(2*(SCENE_HEIGHT/(HEIGHT*2.f)));}
 float normal[3]={-gx,-gy,back?-1.f:1.f};if(side)memcpy(normal,side,sizeof(normal));float n=sqrtf(normal[0]*normal[0]+normal[1]*normal[1]+normal[2]*normal[2]);
 float native_x=(float)left+xx*.5f,native_y=(float)top+yy*.5f,comp=(2.55f-height)/2.55f;float *p=r->mesh+(size_t)r->count++*STRIDE;
 p[0]=SCENE_WIDTH*(native_x/WIDTH-.5f)*comp;p[1]=SCENE_HEIGHT*(.5f-native_y/HEIGHT)*comp;p[2]=-2.55f+height;for(unsigned k=0;k<3;k++)p[k+3]=normal[k]/n;p[6]=native_x/WIDTH;p[7]=native_y/HEIGHT;
 float error=fabsf(p[0]/(-p[2])*2.55f-SCENE_WIDTH*(native_x/WIDTH-.5f))+fabsf(p[1]/(-p[2])*2.55f-SCENE_HEIGHT*(.5f-native_y/HEIGHT));if(error>r->anchor_error)r->anchor_error=error;
 if(height>r->maximum_depth)r->maximum_depth=height;if(height<r->minimum_depth)r->minimum_depth=height;
}
static void face(SylarRelief *r,int w,int h,const int points[4][3],int cx,int cy,int left,int top,const float *normal,bool reverse){
 const unsigned index[6]={0,1,2,0,2,3};for(unsigned k=0;k<6;k++){unsigned j=index[reverse?(k%3==1?k+1:k%3==2?k-1:k):k];emit(r,w,h,points[j][0],points[j][1],cx,cy,left,top,points[j][2]!=0,normal);}
}
static bool build_kong(SylarRelief *r,const SylarFrame *f,unsigned kong){
 int left=WIDTH,right=0,top=HEIGHT,bottom=0;for(int y=0;y<HEIGHT;y++)for(int x=0;x<WIDTH;x++)if(f->relief_owner[y*WIDTH+x]==kong){if(x<left)left=x;if(x+1>right)right=x+1;if(y<top)top=y;if(y+1>bottom)bottom=y+1;}
 if(right<=left || bottom<=top)return true;left-=2;top-=2;int nw=right-left+2,nh=bottom-top+2;if(nw>CROP || nh>CROP)return false;memset(r->crop,0,(size_t)nw*nh*4);
 for(int y=0;y<nh;y++)for(int x=0;x<nw;x++){int ax=left+x,ay=top+y;if(ax>=0 && ax<WIDTH && ay>=0 && ay<HEIGHT && f->relief_owner[ay*WIDTH+ax]==kong)memcpy(r->crop+((size_t)y*nw+x)*4,f->relief_sprite+((size_t)ay*WIDTH+ax)*4,4);}
 epx(r->crop,r->surface,nw,nh);int w=nw*2,h=nh*2;fields(r,w,h);const float normals[4][3]={{-1,0,0},{1,0,0},{0,1,0},{0,-1,0}};
 for(int y=1;y<h-1;y++)for(int x=1;x<w-1;x++)if(occupied(r,w,h,x,y)){
  if(r->count+36>LIMIT)return false;const int front[4][3]={{x,y,0},{x,y+1,0},{x+1,y+1,0},{x+1,y,0}},back[4][3]={{x,y,1},{x,y+1,1},{x+1,y+1,1},{x+1,y,1}};
  face(r,w,h,front,x,y,left,top,NULL,false);face(r,w,h,back,x,y,left,top,NULL,true);
  if(!occupied(r,w,h,x-1,y)){const int p[4][3]={{x,y,0},{x,y+1,0},{x,y+1,1},{x,y,1}};face(r,w,h,p,x,y,left,top,normals[0],false);}
  if(!occupied(r,w,h,x+1,y)){const int p[4][3]={{x+1,y,0},{x+1,y+1,0},{x+1,y+1,1},{x+1,y,1}};face(r,w,h,p,x,y,left,top,normals[1],true);}
  if(!occupied(r,w,h,x,y-1)){const int p[4][3]={{x,y,0},{x+1,y,0},{x+1,y,1},{x,y,1}};face(r,w,h,p,x,y,left,top,normals[2],true);}
  if(!occupied(r,w,h,x,y+1)){const int p[4][3]={{x,y+1,0},{x+1,y+1,0},{x+1,y+1,1},{x,y+1,1}};face(r,w,h,p,x,y,left,top,normals[3],false);}
 }
 return true;
}
static void scene_planes(SylarRelief *r,const SylarFrame *f,bool relief){
 memcpy(r->plane_pixels,f->planes,5*SYLAR_RGBA_BYTES);
 if(relief)for(unsigned i=0;i<WIDTH*HEIGHT;i++)if(f->relief_owner[i]){
  int highest=-1;for(unsigned k=0;k<5;k++)if(r->plane_pixels[k*SYLAR_RGBA_BYTES+i*4+3])highest=(int)k;if(highest>=0)memset(r->plane_pixels+highest*SYLAR_RGBA_BYTES+i*4,0,4);
  const uint8_t *under=NULL;for(unsigned k=0;k<5;k++){const uint8_t *p=r->plane_pixels+k*SYLAR_RGBA_BYTES+i*4;if(p[3])under=p;}const uint8_t *expected=f->relief_background+i*4;
  if(!under || memcmp(under,expected,3)){
   int match=-1;for(unsigned k=0;k<5;k++){const uint8_t *p=r->plane_pixels+k*SYLAR_RGBA_BYTES+i*4;if(p[3] && !memcmp(p,expected,3))match=(int)k;}
   if(match>=0){for(unsigned k=(unsigned)match+1;k<5;k++)memset(r->plane_pixels+k*SYLAR_RGBA_BYTES+i*4,0,4);}else memcpy(r->plane_pixels+3*SYLAR_RGBA_BYTES+i*4,expected,4);
  }
 }
 r->plane_mask=0;for(unsigned k=0;k<5;k++){for(unsigned i=0;i<WIDTH*HEIGHT;i++)if(r->plane_pixels[k*SYLAR_RGBA_BYTES+i*4+3]){r->plane_mask|=1u<<k;break;}if(r->plane_mask&(1u<<k))upload(r,r->planes[k],r->plane_pixels+k*SYLAR_RGBA_BYTES,r->smooth);}
}
static void plane(SylarRelief *r,GLuint image,float distance){
 float half=SCENE_WIDTH*.5f*distance/SCENE_DEPTH,height=SCENE_HEIGHT*.5f*distance/SCENE_DEPTH;const float v[]={-half,height,-distance,0,0,1,0,0, half,height,-distance,0,0,1,1,0, half,-height,-distance,0,0,1,1,1, -half,height,-distance,0,0,1,0,0, half,-height,-distance,0,0,1,1,1, -half,-height,-distance,0,0,1,0,1};
 glBindBuffer(GL_ARRAY_BUFFER,r->screen);glBufferData(GL_ARRAY_BUFFER,sizeof(v),v,GL_STREAM_DRAW);attributes();glBindTexture(GL_TEXTURE_2D,image);glDrawArrays(GL_TRIANGLES,0,6);
}
bool sylar_relief_draw(SylarRelief *r,GLuint target,const float vp[16],const SylarFrame *f,bool enabled,bool smooth){
 bool relief=enabled && f->relief_valid && f->layers_valid;
 bool reconstruction=r->reconstruction_enabled && f->relief_valid && f->layers_valid;
 if(!r->cached || r->reconstruction_active!=reconstruction || r->sequence!=f->sequence || r->relief!=relief || r->smooth!=smooth || r->layered!=f->layers_valid){
  r->reconstruction_active=reconstruction;r->upload_failed=false;r->count=0;r->maximum_depth=0;r->minimum_depth=0;r->anchor_error=0;if(relief && (!build_kong(r,f,1) || !build_kong(r,f,2)))relief=false;if(!relief){r->count=0;r->maximum_depth=0;r->minimum_depth=0;}
  r->smooth=smooth;r->layered=f->layers_valid;r->relief=relief;if(r->layered)scene_planes(r,f,relief);else upload(r,r->background,f->rgba,smooth);if(relief)upload(r,r->sprite,f->relief_sprite,smooth);
  glBindBuffer(GL_ARRAY_BUFFER,r->vertices);glBufferData(GL_ARRAY_BUFFER,(GLsizeiptr)r->count*STRIDE*sizeof(float),r->mesh,GL_DYNAMIC_DRAW);r->cached=true;r->sequence=f->sequence;
 }
 if(r->upload_failed)return false;
 glBindFramebuffer(GL_FRAMEBUFFER,r->framebuffer);glViewport(0,0,r->width,r->height);glDisable(GL_SCISSOR_TEST);glDisable(GL_BLEND);glDisable(GL_CULL_FACE);glDisable(GL_DITHER);glEnable(GL_DEPTH_TEST);glDepthFunc(GL_LEQUAL);
 glClearColor(.012f,.020f,.030f,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);glUseProgram(r->program);glUniformMatrix4fv(r->vp,1,GL_FALSE,vp);glUniform1i(r->texture_uniform,0);glUniform1f(r->lighting,0);glActiveTexture(GL_TEXTURE0);glBindVertexArray(r->vao);
 if(r->layered){for(unsigned k=0;k<5;k++)if(r->plane_mask&(1u<<k))plane(r,r->planes[k],k?2.55f:3.20f);}else plane(r,r->background,2.55f);
 if(r->relief){glUniform1f(r->lighting,1);glBindBuffer(GL_ARRAY_BUFFER,r->vertices);attributes();glBindTexture(GL_TEXTURE_2D,r->sprite);glDrawArrays(GL_TRIANGLES,0,r->count);}
 glBindFramebuffer(GL_READ_FRAMEBUFFER,r->framebuffer);glBindFramebuffer(GL_DRAW_FRAMEBUFFER,r->resolve);glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,target,0);if(glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)return false;
 glBlitFramebuffer(0,0,r->width,r->height,0,0,r->width,r->height,GL_COLOR_BUFFER_BIT,GL_NEAREST);glBindVertexArray(0);glBindFramebuffer(GL_FRAMEBUFFER,0);glFlush();return glGetError()==GL_NO_ERROR;
}
void sylar_relief_destroy(SylarRelief *r){
 glDeleteProgram(r->reconstruction_program);glDeleteTextures(1,&r->reconstruction_source);
 glDeleteProgram(r->epx_program);glDeleteFramebuffers(1,&r->epx_framebuffer);glDeleteTextures(1,&r->epx_source);glDeleteTextures(1,&r->epx_mid);
 glDeleteProgram(r->program);glDeleteVertexArrays(1,&r->vao);glDeleteBuffers(1,&r->vertices);glDeleteBuffers(1,&r->screen);glDeleteTextures(1,&r->background);glDeleteTextures(1,&r->sprite);glDeleteTextures(5,r->planes);glDeleteRenderbuffers(1,&r->depth);glDeleteRenderbuffers(1,&r->color);glDeleteFramebuffers(1,&r->framebuffer);glDeleteFramebuffers(1,&r->resolve);
 free(r->mesh);free(r->scratch);free(r->upscaled);free(r->distance);free(r->height_field);free(r->plane_pixels);free(r->crop);free(r->surface);memset(r,0,sizeof(*r));
}

void sylar_relief_reconstruction(SylarRelief *r,bool enabled){if(r->reconstruction_enabled!=enabled){r->reconstruction_enabled=enabled;r->cached=false;}}
