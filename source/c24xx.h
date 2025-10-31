/* EmulTwo  - A Windows Colecovision emulator.
 * Copyright (C) 2014-2023 Alekmaul
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
 * c24xx.h
 *
 * Based on   C24XX emulation by Marat Fayzullin in 2017-2019
*/

#ifndef _C24XX_H
#define _C24XX_H

#define C24XX_CHIP    0x0F
#define C24XX_24C01   0x00 // 128 bytes   (7bit, 8/page)
#define C24XX_24C02   0x01 // 256 bytes   (8bit, 8/page)
#define C24XX_24C04   0x02 // 512 bytes   (1+8bit, 16/page)
#define C24XX_24C08   0x03 // 1024 bytes  (2+8bit, 16/page)
#define C24XX_24C16   0x04 // 2048 bytes  (3+8bit, 16/page)
#define C24XX_24C32   0x05 // 4096 bytes  (12bit, 32/page)
#define C24XX_24C64   0x06 // 8192 bytes  (13bit, 32/page)
#define C24XX_24C128  0x07 // 16384 bytes (14bit, 64/page)
#define C24XX_24C256  0x08 // 32768 bytes (15bit, 64/page)
#define C24XX_24C512  0x09 // 65536 bytes (16bit, 128/page)
#define C24XX_24C1024 0x0A // 131072 bytes (1+16bit, 256/page)
#define C24XX_DEBUG   0x10

#define C24XX_SDA     0x01
#define C24XX_SCL     0x02
#define C24XX_ADDR    0x1C
#define C24XX_A0      0x04
#define C24XX_A1      0x08
#define C24XX_A2      0x10

// Internal Chip States
#define RECV_CMD  0
#define RECV_ADDR 1
#define RECV_ADR2 2
#define RECV_DATA 3
#define SEND_DATA 4

typedef struct {
  unsigned int Flags;       // Chip type, etc
  unsigned int Addr;        // Current address
  int  Rsrvd;               // Reserved field
  unsigned char State;      // Current state
  unsigned char Cmd;        // Current command
  unsigned short Bits;      // In/out bits
  unsigned char Pins;       // Input pins
  unsigned char Out;        // Output pins
  unsigned char *Data;      // Pointer to data
} c24xx;

#ifdef __cplusplus
extern "C" c24xx c24;
extern "C" void c24xx_reset(unsigned char *Data,unsigned int Flags);
extern "C" unsigned int c24xx_getsize(void);
extern "C" unsigned char c24xx_read(void);
extern "C" unsigned char c24xx_write(unsigned char value);
#else
extern c24xx c24;
extern void c24xx_reset(unsigned char *Data,unsigned int Flags);
extern unsigned int c24xx_getsize(void);
extern unsigned char c24xx_read(void);
extern unsigned char c24xx_write(unsigned char value);

#endif


#endif
