// Copyright 2026 Sylar.soluciones. SPDX-License-Identifier: MIT
// Small code-native 5x7 font: no game imagery or external assets.
#include "status_screen.h"
#include <string.h>
static const uint8_t alphabet[26][7]={
 {14,17,17,31,17,17,17},{30,17,17,30,17,17,30},{14,17,16,16,16,17,14},
 {30,17,17,17,17,17,30},{31,16,16,30,16,16,31},{31,16,16,30,16,16,16},
 {14,17,16,23,17,17,15},{17,17,17,31,17,17,17},{14,4,4,4,4,4,14},
 {7,2,2,2,2,18,12},{17,18,20,24,20,18,17},{16,16,16,16,16,16,31},
 {17,27,21,21,17,17,17},{17,25,21,19,17,17,17},{14,17,17,17,17,17,14},
 {30,17,17,30,16,16,16},{14,17,17,17,21,18,13},{30,17,17,30,20,18,17},
 {15,16,16,14,1,1,30},{31,4,4,4,4,4,4},{17,17,17,17,17,17,14},
 {17,17,17,17,17,10,4},{17,17,17,21,21,21,10},{17,17,10,4,10,17,17},
 {17,17,10,4,4,4,4},{31,1,2,4,8,16,31}};
static const uint8_t digits[10][7]={
 {14,17,19,21,25,17,14},{4,12,4,4,4,4,14},{14,17,1,2,4,8,31},
 {30,1,1,14,1,1,30},{2,6,10,18,31,2,2},{31,16,16,30,1,1,30},
 {14,16,16,30,17,17,14},{31,1,2,4,8,8,8},{14,17,17,14,17,17,14},
 {14,17,17,15,1,1,14}};
static void text(uint8_t *p,int y,const char *line,int scale) {
 int x=(SYLAR_WIDTH-(int)strlen(line)*6*scale)/2;
 for (;*line;line++,x+=6*scale) {
  const uint8_t *glyph=*line>='A' && *line<='Z'?alphabet[*line-'A']:
      (*line>='0' && *line<='9'?digits[*line-'0']:NULL);
  for (int row=0;row<7;row++) for (int col=0;col<5;col++) {
   bool ink=glyph?(glyph[row]&(1u<<(4-col)))!=0:(*line=='.' && row==6 && col==2);
   if (!ink) continue;
   for (int dy=0;dy<scale;dy++) for (int dx=0;dx<scale;dx++) {
    int px=x+col*scale+dx,py=y+row*scale+dy;
    if (px>=0 && px<SYLAR_WIDTH && py>=0 && py<SYLAR_HEIGHT) {
     size_t at=(size_t)(py*SYLAR_WIDTH+px)*4; p[at]=224; p[at+1]=236; p[at+2]=248;
    }
   }
  }
 }
}
void sylar_status_screen(uint8_t *p,enum SylarGameStatus status) {
 for (size_t i=0;i<SYLAR_WIDTH*SYLAR_HEIGHT;i++) {
  p[i*4]=12; p[i*4+1]=20; p[i*4+2]=34; p[i*4+3]=255;
 }
 text(p,24,"DKC1 QUEST VR",2);
 const char *title="CARGANDO";
 const char *detail="PREPARANDO EL JUEGO";
 if (status==SYLAR_ROM_MISSING) { title="FALTA LA ROM PRIVADA"; detail="CARGALA Y VOLVE A ABRIR LA APP"; }
 if (status==SYLAR_ROM_INVALID) { title="ROM NO COMPATIBLE"; detail="USA LA VERSION USA 1.0 VERIFICADA"; }
 if (status==SYLAR_SAVE_ERROR) { title="REVISAR GUARDADO"; detail="LA PARTIDA SE PAUSO"; }
 if (status==SYLAR_CORE_ERROR) { title="EL JUEGO SE DETUVO"; detail="CERRA LA APP PARA REINTENTAR"; }
 text(p,70,title,1); text(p,88,detail,1);
 text(p,122,"STICK IZQUIERDO PARA MOVER",1);
 text(p,139,"A SALTAR   B CORRER Y RODAR",1);
 text(p,156,"X CAMBIAR KONG   Y START",1);
 text(p,194,"UN PROYECTO DE SYLAR.SOLUCIONES",1);
}
