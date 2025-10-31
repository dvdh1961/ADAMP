/* EmulTwo  - A Windows Colecovision emulator.
 * Copyright (C) 2018-2019 Alekmaul
 *
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
 *
 * sn76489.h
 *
 */
#ifndef _SN76489_H
#define _SN76489_H

// --------------------------------------------------------------------------------------------
// The SN76489 has 8 "registers":
// 4 x 4 bit volume registers,
// 3 x 10 bit tone registers and
// 1 x 3 bit noise register.
// --------------------------------------------------------------------------------------------
#define MAX_OUTPUT     	0x7fff
#define SNSTEP          0x8000 //0x10000

#define	FB_WNOISE      	0x12000
#define FB_PNOISE      	0x08000

#define NG_PRESET      	0x8000

typedef struct  {
    int SampleRate;
    int UpdateStep;
    int VolTable[16];
    int Register[8];
    int Volume[4];
    int Period[4];
    int Count[4];
    int Output[4];
    int LastRegister;
    int RNG;
} sn76496;

#ifdef __cplusplus
extern "C" sn76496 sn;
extern "C" void sn76489_update(short *buffer, unsigned int length);
extern "C" void sn76489_write(int data);
extern "C" void sn76489_reset(int clock, int sample_rate);
extern "C" void sn76489_init(int clock, int sample_rate);
extern "C" void sn76489_config(int mute, int boost, int volume, int feedback);
#else
extern sn76496 sn;
extern void sn76489_update(short *buffer, unsigned int length);
extern void sn76489_write(int data);
extern void sn76489_reset(int clock, int sample_rate);
extern void sn76489_init(int clock, int sample_rate);
extern void sn76489_config(int mute, int boost, int volume, int feedback);
#endif

#endif

