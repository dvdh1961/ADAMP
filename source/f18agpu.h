/* ADAMP_EMU  - A Windows ColecoVision emulator.
 * Copyright (C) 2025 DannyVdH
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
 * f18agpu.h
 *
 * Based on js99'er - TI-99/4A emulator written in JavaScript
 *  written by Rasmus Moustgaard <rasmus.moustgaard@gmail.com>
 *
 */
//---------------------------------------------------------------------------

#ifndef f18agpuH
#define f18agpuH
//---------------------------------------------------------------------------
#include "emu.h"

#define F18AGPU_SPEED_DIVIDER       1.2
#define F18AGPU_CYCLES_PER_FRAME    (1250000/F18AGPU_SPEED_DIVIDER) // Speed is approximately 25 times that of the normal CPU
#define F18AGPU_CYCLES_PER_SCANLINE (4000/F18AGPU_SPEED_DIVIDER)

// Constants
#define F18AGPU_SRC                 0
#define F18AGPU_DST                 1
#define F18AGPU_POSTINC2            0x80
#define F18AGPU_POSTINC1            0x40
#define F18AGPU_BIT_LGT             0x8000
#define F18AGPU_BIT_AGT             0x4000
#define F18AGPU_BIT_EQ              0x2000
#define F18AGPU_BIT_C               0x1000
#define F18AGPU_BIT_OV              0x0800
#define F18AGPU_BIT_OP              0x0400
#define F18AGPU_BIT_X               0x0200

// Assignment masks
// #define F18AGPU_maskEQ_LGT            F18AGPU_BIT_EQ | F18AGPU_BIT_LGT;
#define F18AGPU_MSKLGT_AGT_EQ       (F18AGPU_BIT_LGT | F18AGPU_BIT_AGT | F18AGPU_BIT_EQ)
#define F18AGPU_MSKLGT_AGT_EQ_OP    (F18AGPU_BIT_LGT | F18AGPU_BIT_AGT | F18AGPU_BIT_EQ | F18AGPU_BIT_OP)
#define F18AGPU_MSKLGT_AGT_EQ_OV    (F18AGPU_BIT_LGT | F18AGPU_BIT_AGT | F18AGPU_BIT_EQ | F18AGPU_BIT_OV)

// carry here used for INC and NEG only
#define F18AGPU_MSKLGT_AGT_EQ_OV_C  (F18AGPU_BIT_LGT | F18AGPU_BIT_AGT | F18AGPU_BIT_EQ | F18AGPU_BIT_OV | F18AGPU_BIT_C)

// Functions
#define ADDPC(x)                            { f18agpu.PC+=(x); f18agpu.PC&=0xfffe; } // force the PC to always be 15-bit, can't store a 16-bit PC

#define F18AGPUADDCYCLE(n)                  f18agpu.cycles+=n
/*
#define FormatIV { D=(in&0x03c0)>>6; Ts=(in&0x0030)>>4; S=(in&0x000f); B=(D<9); fixS(); }           // No destination (CRU ops)

*/
#define F18AGPUFORMATI                  { f18agpu.Td=(f18agpu.in&0x0c00)>>10; f18agpu.Ts=(f18agpu.in&0x0030)>>4; f18agpu.D=(f18agpu.in&0x03c0)>>6; f18agpu.S=(f18agpu.in&0x000f); f18agpu.B=(f18agpu.in&0x1000)>>12; f18agpu_fixs(); }
#define F18AGPUFormatII                 { f18agpu.D=(f18agpu.in&0x00ff); }
#define F18AGPUFormatIII                { f18agpu.Td=0; f18agpu.Ts=(f18agpu.in&0x0030)>>4; f18agpu.D=(f18agpu.in&0x03c0)>>6; f18agpu.S=(f18agpu.in&0x000f); f18agpu.B=0; f18agpu_fixs(); }
#define F18AGPUFormatV                  { f18agpu.D=(f18agpu.in&0x00f0)>>4; f18agpu.S=(f18agpu.in&0x000f); f18agpu.S=f18agpu.WP+(f18agpu.S<<1); }
#define F18AGPUFormatVI                 { f18agpu.Ts=(f18agpu.in&0x0030)>>4; f18agpu.S=f18agpu.in&0x000f; f18agpu.B=0; f18agpu_fixs(); } // No destination (single argument instructions)
#define F18AGPUFormatVII                {}  // no argument
#define F18AGPUFORMATVIII_0             { f18agpu.D=(f18agpu.in&0x000f); f18agpu.D=f18agpu.WP+(f18agpu.D<<1); }
#define F18AGPUFORMATVIII_1             { f18agpu.D=(f18agpu.in&0x000f); f18agpu.D=f18agpu.WP+(f18agpu.D<<1); f18agpu.S=f18agpu_readword(f18agpu.PC); ADDPC(2); }
#define F18AGPUFORMATIX                 { f18agpu.D=(f18agpu.in&0x03c0)>>6; f18agpu.Ts=(f18agpu.in&0x0030)>>4; f18agpu.S=(f18agpu.in&0x000f); f18agpu.B=0; f18agpu_fixs(); }              // No destination here (dest calc'd after call) (DIV, MUL, XOP)

// Group gets
#define F18AGPUGET_LGT                  ( f18agpu.ST & F18AGPU_BIT_LGT )  // Logical Greater than: >0x0000
#define F18AGPUGET_AGT                  ( f18agpu.ST & F18AGPU_BIT_AGT )   // Arithmetic Greater than: >0x0000 and <0x8000
#define F18AGPUGET_EQ                   ( f18agpu.ST & F18AGPU_BIT_EQ ) // Equal: ==0x0000
#define F18AGPUGET_C                    ( f18agpu.ST & F18AGPU_BIT_C )  // Carry: carry occurred during operation
#define F18AGPUGET_OV                   ( f18agpu.ST & F18AGPU_BIT_OV ) // Overflow: overflow occurred during operation
#define F18AGPUGET_OP                   ( f18agpu.ST & F18AGPU_BIT_OP )  // Odd parity: word has odd number of '1' bits

// Group sets
#define F18AGPUSET_LGT                  { f18agpu.ST |= F18AGPU_BIT_LGT; }  // Logical Greater than: >0x0000
#define F18AGPUSET_AGT                  { f18agpu.ST |= F18AGPU_BIT_AGT; }   // Arithmetic Greater than: >0x0000 and <0x8000
#define F18AGPUSET_EQ                   { f18agpu.ST |= F18AGPU_BIT_EQ; }   // Equal: ==0x0000
#define F18AGPUSET_C                    { f18agpu.ST |= F18AGPU_BIT_C; }   // Carry: carry occurred during operation
#define F18AGPUSET_OV                   { f18agpu.ST |= F18AGPU_BIT_OV; }   // Overflow: overflow occurred during operation
#define F18AGPUSET_OP                   { f18agpu.ST |= F18AGPU_BIT_OP; }   // Odd parity: word has odd number of '1' bits
#define F18AGPUSET_X                    { f18agpu.ST |= F18AGPU_BIT_X; }   // Executing 'X' statement

// Group clears
#define F18AGPURESETEQ                  { f18agpu.ST &= 0xdfff; }
#define F18AGPURESETOV                  { f18agpu.ST &= 0xf7ff; }

#define F18AGPURESETEQ_LGT              { f18agpu.ST &= 0x5fff; }
#define F18AGPURESETLGT_AGT_EQ          { f18agpu.ST &= 0x1fff; }
#define F18AGPURESETLGT_AGT_EQ_OP       { f18agpu.ST &= 0x1bff; }
#define F18AGPURESETEQ_LGT_AGT_OV       { f18agpu.ST &= 0x17ff; }
#define F18AGPURESETEQ_LGT_AGT_C        { f18agpu.ST &= 0x0fff; }
#define F18AGPURESETEQ_LGT_AGT_C_OV     { f18agpu.ST &= 0x07ff; }
#define F18AGPURESETEQ_LGT_AGT_C_OV_OP  { f18agpu.ST &= 0x03ff; }

//---------------------------------------------------------------------------

typedef struct {
    BYTE    cpuIdle;                    // CPU is working or not
    unsigned short WP;                  // Workspace address
    unsigned short PC;                  // Program Counter
    unsigned short flagX;               // Set during an 'X' instruction, 0 if not active, else address of PC after the X (ignoring arguments if any)
    unsigned short ST;                  // Status register
    unsigned short Ts,Td,D,S,B;         // Opcode interpretation
    BYTE nPostInc[2];                   // Register number to increment, ORd with 0x80 for 2, or 0x40 for 1

    int cycles;                         // Counters
    unsigned short in;                  // current instruction
} tF18AGPU;

extern tF18AGPU f18agpu;
//---------------------------------------------------------------------------

extern void f18agpu_li(void);
extern void f18agpu_ai(void);
extern void f18agpu_andi(void);
extern void f18agpu_ori(void);
extern void f18agpu_ci(void);
extern void f18agpu_stwp(void);
extern void f18agpu_stst(void);
extern void f18agpu_lwpi(void);
extern void f18agpu_limi(void);
extern void f18agpu_idle(void);
extern void f18agpu_rset(void);
extern void f18agpu_rtwp(void);
extern void f18agpu_ckon(void);
extern void f18agpu_ckof(void);
extern void f18agpu_lrex(void);
extern void f18agpu_blwp(void);
extern void f18agpu_b(void);
extern void f18agpu_x(void);
extern void f18agpu_clr(void);
extern void f18agpu_neg(void);
extern void f18agpu_inv(void);
extern void f18agpu_inc(void);
extern void f18agpu_inct(void);
extern void f18agpu_dec(void);
extern void f18agpu_dect(void);
extern void f18agpu_bl(void);
extern void f18agpu_swpb(void);
extern void f18agpu_seto(void);
extern void f18agpu_abs(void);
extern void f18agpu_sra(void);
extern void f18agpu_srl(void);
extern void f18agpu_sla(void);
extern void f18agpu_src(void);
extern void f18agpu_jmp(void);
extern void f18agpu_jlt(void);
extern void f18agpu_jle(void);
extern void f18agpu_jeq(void);
extern void f18agpu_jhe(void);
extern void f18agpu_jgt(void);
extern void f18agpu_jne(void);
extern void f18agpu_jnc(void);
extern void f18agpu_joc(void);
extern void f18agpu_jno(void);
extern void f18agpu_jl(void);
extern void f18agpu_jh(void);
extern void f18agpu_jop(void);
extern void f18agpu_sbo(void);
extern void f18agpu_sbz(void);
extern void f18agpu_tb(void);
extern void f18agpu_coc(void);
extern void f18agpu_czc(void);
extern void f18agpu_xor(void);
extern void f18agpu_xop(void);
extern void f18agpu_ldcr(void);
extern void f18agpu_stcr(void);
extern void f18agpu_mpy(void);
extern void f18agpu_div(void);
extern void f18agpu_szc(void);
extern void f18agpu_szcb(void);
extern void f18agpu_s(void);
extern void f18agpu_sb(void);
extern void f18agpu_c(void);
extern void f18agpu_cb(void);
extern void f18agpu_a(void);
extern void f18agpu_ab(void);
extern void f18agpu_mov(void);
extern void f18agpu_movb(void);
extern void f18agpu_soc(void);
extern void f18agpu_socb(void);

// F18A specific opcodes
extern void f18agpu_callf18(void);
extern void f18agpu_retf18(void);
extern void f18agpu_pushf18(void);
extern void f18agpu_slcf18(void);
extern void f18agpu_popf18(void);
extern void f18agpu_bad(void);

extern void f18agpu_initopcode0(unsigned short in);
extern void f18agpu_initopcode02(unsigned short in);
extern void f18agpu_initopcode03(unsigned short in);
extern void f18agpu_initopcode04(unsigned short in);
extern void f18agpu_initopcode05(unsigned short in);
extern void f18agpu_initopcode06(unsigned short in);
extern void f18agpu_initopcode07(unsigned short in);
extern void f18agpu_initopcode1(unsigned short in);
extern void f18agpu_initopcode2(unsigned short in);
extern void f18agpu_initopcode3(unsigned short in);

//---------------------------------------------------------------------------
extern void f18agpu_fixs(void);
extern void f18agpu_fixd(void);
extern void f18agpu_postIncrement(BYTE nWhich);
extern void f18agpu_intreset(void);
extern void f18agpu_setpc(unsigned short value);

extern BYTE f18agpu_readbyte(unsigned short addr);
extern void f18agpu_writebyte(unsigned short addr, BYTE value);

extern void f18agpu_writeword(unsigned short addr, unsigned short value);
extern unsigned short f18agpu_readword(unsigned short addr);

extern void f18agpu_init(void);

extern void f18agpu_reset(void);
extern void f18agpu_execute(int cycles);

#endif
