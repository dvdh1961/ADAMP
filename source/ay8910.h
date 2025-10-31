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
 * ay8910.h
 *
 */
#ifndef _AY8910_H
#define _AY8910_H

#define AYMAX_OUTPUT 0x7fff
#define AYSTEP 0x8000

// register id's
#define AY_AFINE	(0)
#define AY_ACOARSE	(1)
#define AY_BFINE	(2)
#define AY_BCOARSE	(3)
#define AY_CFINE	(4)
#define AY_CCOARSE	(5)
#define AY_NOISEPER	(6)
#define AY_ENABLE	(7)
#define AY_AVOL		(8)
#define AY_BVOL		(9)
#define AY_CVOL		(10)
#define AY_EFINE	(11)
#define AY_ECOARSE	(12)
#define AY_ESHAPE	(13)
#define AY_PORTA	(14)
#define AY_PORTB	(15)

typedef struct {
  int Channel;
  int SampleRate;
  unsigned int (*PortAread)(void);
  unsigned int (*PortBread)(void);
  void (*PortAwrite)(unsigned int offset,unsigned int data);
  void (*PortBwrite)(unsigned int offset,unsigned int data);
  int register_latch;
  unsigned char Regs[16];
  unsigned int UpdateStep;
  int PeriodA,PeriodB,PeriodC,PeriodN,PeriodE;
  signed int CountA,CountB,CountC,CountN,CountE;
  unsigned int VolA,VolB,VolC,VolE;
  unsigned char EnvelopeA,EnvelopeB,EnvelopeC;
  unsigned char OutputA,OutputB,OutputC,OutputN;
  signed char CountEnv;
  unsigned char Hold,Alternate,Attack,Holding;
  int RNG;
  unsigned int VolTable[32];
} ay8910;

#ifdef __cplusplus
extern "C" ay8910 ay;
extern  "C" void ay8910_update(short *buffer, unsigned int length);
extern "C" void ay8910_write(int a ,int data);
extern "C" unsigned short ay8910_read(void);
extern "C" void ay8910_reset(void);
extern "C" void ay8910_init(int clock, int sample_rate);
#else
extern ay8910 ay;
extern void ay8910_update(short *buffer, unsigned int length);
extern void ay8910_write(int a ,int data);
extern unsigned short ay8910_read(void);
extern void ay8910_reset(void);
extern void ay8910_init(int clock, int sample_rate);
#endif

#endif

