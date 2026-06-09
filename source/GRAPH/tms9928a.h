/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * TMS9928A.H
 *
 * Based on   tms emulation (C) Marat Fayzullin 1994-2021
 *
 */

#ifndef _TMS9928A_H_
#define _TMS9928A_H_

#include "CORE/cv.h"

// 80C specific resolutions
#define TVW_TMS 256
#define TVH_TMS 192
#define TVH_DEF 192    // default height
#define TVH_ROW30 216   // 30 rows
#define TVH_ 262
#define TVW_ 336


#define TMS9918_WIDTH 256
#define TMS9918_HEIGHT 192

#define MAXSCREEN 3                   // Highest screen mode supported

#define TMS9918_BASE            10738635

#define TMS9918_REG1_RAM16K     0x80 // 1: 16kB VRAM (0=4kB)
#define TMS9918_REG1_SCREEN     0x40 // 1: Enable display
#define TMS9918_REG1_IRQ        0x20 // 1: IRQs on VBlanks
#define TMS9918_REG1_SPR16      0x02 // 1: 16x16 sprites (0=8x8)
#define TMS9918_REG1_BIGSPR     0x01 // 1: Magnify sprites x2

#define TMS9918_MAXSPRITES      4    // Max number of sprites

#define TMS9918_STAT_VBLANK     0x80 // 1: VBlank has occured
#define TMS9918_STAT_5THSPR     0x40 // 1: 5th sprite detected
#define TMS9918_STAT_OVRLAP     0x20 // 1: Sprites overlap
#define TMS9918_STAT_5THNUM     0x1F // Number of the 5th sprite

#define TMS9918_REG0_EXTVDP     0x01 // Enable external VDP

#define TMS9918_FRAMES          60
#define TMS9918_LINES           262
#define TMS9918_START_LINE      (3+13+27)
#define TMS9918_END_LINE        (TMS9918_START_LINE+192)
#define TMS9918_DRAWFRAMES      100  // Default % frames to draw */
#define TMS9918_LINE            (TMS9918_BASE/(3*TMS9918_FRAMES*TMS9918_LINES))

#define TMS9929_FRAMES          50
#define TMS9929_LINES           313
#define TMS9929_CLOCK           (TMS9918_BASE/2)
#define TMS9929_FRAME           (TMS9918_BASE/(2*50))
#define TMS9929_LINE            (TMS9918_BASE/(2*TMS9929_FRAMES*TMS9929_LINES))

#define TMS9918_Mode            (((tms.VR[0]&0x02)>>1)|(((tms.VR[1]&0x18)>>2)))
#define TMS9918_ExtVDP          (R[0]&TMS9918_REG0_EXTVDP)
#define TMS9918_VRAMMask        (tms.VR[1]&TMS9918_REG1_RAM16K ? 0x3FFF:0x0FFF)
#define TMS9918_VBlankON        (tms.VR[1]&TMS9918_REG1_IRQ)
#define TMS9918_Sprites16       (tms.VR[1]&TMS9918_REG1_SPR16)
#define TMS9918_ScreenON        (tms.VR[1]&TMS9918_REG1_SCREEN)

#define _mRGB32(r,g,b)          ( (r<<16) | (g<<8) | (b))

typedef struct {
  void (*Refresh)(unsigned char uY);
  unsigned char R2,R3,R4,R5,R6,M2,M3,M4,M5;
} tScrMode;

typedef struct {
    unsigned short DLatch;                                  // Data register latch
    unsigned char VKey;                                     // VDP address latch key
    unsigned short VAddr;                                   // Storage for VIDRAM addresses
    unsigned char VR[16];                                   // VDP registers
    unsigned char SR;                                       // VDP status register
    unsigned char Mode;                                     // Current screen mode
    unsigned char FGColor,BGColor;                          // Colors ForeGround and BackGround
    unsigned char *ChrGen,*ChrTab,*ColTab;                  // VDP tables (screens)
    unsigned char *SprGen,*SprTab;                          // VDP tables (sprites)
    unsigned int ColTabM,ChrGenM;                           // VDP tables mask
    unsigned short CurLine;                                 // Current scanline
    unsigned short ScanLines;                               // Scanlines per frame
    unsigned char MaxSprites;                               // Number of sprites/line
    unsigned char IdxPal[16];                               // Palette color index
    unsigned char UCount;                                   // Screen update counter
} tTMS9981A;
// ----------------------------------------------------------------------------------------
extern const tScrMode SCR[MAXSCREEN+1];

extern tTMS9981A tms;
extern WORD TVW,TVH;
// ----------------------------------------------------------------------------------------

#ifdef __cplusplus
extern "C" void tms9918_reset(void);
extern "C" unsigned char tms9918_loop(void);
extern "C" unsigned char Write9918(int iReg,unsigned char value);
extern "C" void tms9918_writedata(unsigned char value);
extern "C" unsigned char tms9918_readdata(void);
extern "C" unsigned char tms9918_writectrl(unsigned char value);
extern "C" unsigned char tms9918_readctrl(void);

extern "C" int ScanSprites(unsigned char Y,unsigned int *Mask);
extern "C" unsigned char CheckSprites(void);
extern "C" BYTE ReadStatus9918(void);

#else
extern void tms9918_reset(void);
extern unsigned char tms9918_loop(void);
extern unsigned char Write9918(int iReg,unsigned char value);
extern void tms9918_writedata(unsigned char value);
extern unsigned char tms9918_readdata(void);
extern unsigned char tms9918_writectrl(unsigned char value);
extern unsigned char tms9918_readctrl(void);

extern int ScanSprites(unsigned char Y,unsigned int *Mask);
extern unsigned char CheckSprites(void);
extern BYTE ReadStatus9918(void);
#endif


#endif
