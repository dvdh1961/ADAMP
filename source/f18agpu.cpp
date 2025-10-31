/* EmulTwo  - A Windows ColecoVision emulator.
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
 * f18agpu.cpp
 *
 * Based on js99'er - TI-99/4A emulator written in JavaScript
 *  written by Rasmus Moustgaard <rasmus.moustgaard@gmail.com>
 *
 */
//---------------------------------------------------------------------------
#include <cstdlib>
#include <vcl.h>
#pragma hdrstop

#include "f18agpu.h"

#include "tms9928a.h"
#include "f18a.h"

//---------------------------------------------------------------------------
#define F18AGPU_PRELOADSIZE     (128/2)*7+(48/2)
const char F18AGPU_PRELOAD[] = {
    "020F47FE100D4036405A409440B440FAFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF0CA0411C034004C1D0603F000971C0214006069010F7C0203F02C0603F04C0A0"
    "3F06D0E03F011305D010DC40060216FD1003DC70060216FD045B0D0B06A040B40F0BC1C7131604C0D02060040A30C0C004C102020400CC01060216FD04C0D020"
    "415106C00A30A0030CA041AED8204151B000045BD820411A3F00020041D6C8003F0202004006C8003F0402004010C8003F06045B04C7D0203F011313C0204118"
    "06000CA041520204000502053F02020641428DB51603060416FC1009060016F11009C0203F020CA04152804014030CA0419A0547D807B000045B0D0B06A040B4"
    "0F0BC1C71304C0203F0C0CA041AE045B050000000000000000000000020041100201411502020B0003A032023230323032303600020200063631060216FD03C0"
    "0C0020202020202020202020000000000000880041181A03C06041180C000D000A4002010B00A020411617010581A0604114020341420202001003A0320106C1"
    "3201320006C0320036003633060216FD03C00F00C06041180C0002003F000201414202020008CC31060216FD0C000201414CD0A0415006C2D0A0414F02030B00"
    "03A0320332313231323136013630060216FD03C00C000340"
};

#pragma package(smart_init)

tF18AGPU f18agpu;

unsigned short wStatusLookup[0x10000];
unsigned short bStatusLookup[0x100];

typedef void (*F18AGPUFctn)(void);			// function pointers to opcode

F18AGPUFctn opcodef18agpu[0x10000];            // 64K opcode possibles ...

// ----------------------------------------------------------------------------------------

void f18agpu_initopcode0(unsigned short in)
{
    unsigned short x;

    x=(in&0x0f00)>>8;

    switch(x)
    {
    case 2: f18agpu_initopcode02(in);       break;
    case 3: f18agpu_initopcode03(in);       break;
    case 4: f18agpu_initopcode04(in);       break;
    case 5: f18agpu_initopcode05(in);       break;
    case 6: f18agpu_initopcode06(in);       break;
    case 7: f18agpu_initopcode07(in);       break;
    case 8: opcodef18agpu[in]=&f18agpu_sra;    break;
    case 9: opcodef18agpu[in]=&f18agpu_srl;    break;
    case 10:opcodef18agpu[in]=&f18agpu_sla;    break;
    case 11:opcodef18agpu[in]=&f18agpu_src;    break;
    default: opcodef18agpu[in]=&f18agpu_bad; break;
    }
}

void f18agpu_initopcode02(unsigned short in)
{
    unsigned short x;

    x=(in&0x00e0)>>4;

    switch(x)
    {
    case 0: opcodef18agpu[in]=&f18agpu_li; break;
    case 2: opcodef18agpu[in]=&f18agpu_ai; break;
    case 4: opcodef18agpu[in]=&f18agpu_andi; break;
    case 6: opcodef18agpu[in]=&f18agpu_ori;    break;
    case 8: opcodef18agpu[in]=&f18agpu_ci; break;
    case 10:opcodef18agpu[in]=&f18agpu_stwp; break;
    case 12:opcodef18agpu[in]=&f18agpu_stst; break;
    case 14:opcodef18agpu[in]=&f18agpu_lwpi; break;
    default: opcodef18agpu[in]=&f18agpu_bad; break;
    }
}

void f18agpu_initopcode03(unsigned short in)
{
    WORD x;

    x=(in&0x00e0)>>4;

    switch(x)
    {
    case 0: opcodef18agpu[in]=&f18agpu_limi; break;
    case 4: opcodef18agpu[in]=&f18agpu_idle; break;
    case 6: opcodef18agpu[in]=&f18agpu_rset; break;
    case 8: opcodef18agpu[in]=&f18agpu_rtwp; break;
    case 10:opcodef18agpu[in]=&f18agpu_ckon; break;
    case 12:opcodef18agpu[in]=&f18agpu_ckof; break;
    case 14:opcodef18agpu[in]=&f18agpu_lrex; break;
    default: opcodef18agpu[in]=&f18agpu_bad; break;
    }
}

void f18agpu_initopcode04(unsigned short in)
{
    unsigned short x;

    x=(in&0x00c0)>>4;

    switch(x)
    {
    case 0: opcodef18agpu[in]=&f18agpu_blwp; break;
    case 4: opcodef18agpu[in]=&f18agpu_b;  break;
    case 8: opcodef18agpu[in]=&f18agpu_x;  break;
    case 12:opcodef18agpu[in]=&f18agpu_clr;    break;
    default: opcodef18agpu[in]=&f18agpu_bad; break;
    }
}

void f18agpu_initopcode05(unsigned short in)
{
    unsigned short x;

    x=(in&0x00c0)>>4;

    switch(x)
    {
    case 0: opcodef18agpu[in]=&f18agpu_neg;    break;
    case 4: opcodef18agpu[in]=&f18agpu_inv;    break;
    case 8: opcodef18agpu[in]=&f18agpu_inc;    break;
    case 12:opcodef18agpu[in]=&f18agpu_inct; break;
    default: opcodef18agpu[in]=&f18agpu_bad; break;
    }
}

void f18agpu_initopcode06(unsigned short in)
{
    unsigned short x;

    x=(in&0x00c0)>>4;

    switch(x)
    {
    case 0: opcodef18agpu[in]=&f18agpu_dec;    break;
    case 4: opcodef18agpu[in]=&f18agpu_dect; break;
    case 8: opcodef18agpu[in]=&f18agpu_bl; break;
    case 12:opcodef18agpu[in]=&f18agpu_swpb; break;
    default: opcodef18agpu[in]=&f18agpu_bad; break;
    }
}

void f18agpu_initopcode07(unsigned short in)
{
    unsigned short x;

    x=(in&0x00c0)>>4;

    switch(x)
    {
    case 0: opcodef18agpu[in]=&f18agpu_seto; break;
    case 4: opcodef18agpu[in]=&f18agpu_abs;    break;
    default: opcodef18agpu[in]=&f18agpu_bad; break;
    }
}

void f18agpu_initopcode1(unsigned short in)
{
    unsigned short x;

    x=(in&0x0f00)>>8;

    switch(x)
    {
    case 0: opcodef18agpu[in]=&f18agpu_jmp;    break;
    case 1: opcodef18agpu[in]=&f18agpu_jlt;    break;
    case 2: opcodef18agpu[in]=&f18agpu_jle;    break;
    case 3: opcodef18agpu[in]=&f18agpu_jeq;    break;
    case 4: opcodef18agpu[in]=&f18agpu_jhe;    break;
    case 5: opcodef18agpu[in]=&f18agpu_jgt;    break;
    case 6: opcodef18agpu[in]=&f18agpu_jne;    break;
    case 7: opcodef18agpu[in]=&f18agpu_jnc;    break;
    case 8: opcodef18agpu[in]=&f18agpu_joc;    break;
    case 9: opcodef18agpu[in]=&f18agpu_jno;    break;
    case 10:opcodef18agpu[in]=&f18agpu_jl; break;
    case 11:opcodef18agpu[in]=&f18agpu_jh; break;
    case 12:opcodef18agpu[in]=&f18agpu_jop;    break;
    case 13:opcodef18agpu[in]=&f18agpu_sbo;    break;
    case 14:opcodef18agpu[in]=&f18agpu_sbz;    break;
    case 15:opcodef18agpu[in]=&f18agpu_tb; break;
    default:opcodef18agpu[in]=&f18agpu_bad;  break;
    }
}

void f18agpu_initopcode2(unsigned short in)
{
    unsigned short x;

    x=(in&0x0c00)>>8;

    switch(x)
    {
    case 0: opcodef18agpu[in]=&f18agpu_coc; break;
    case 4: opcodef18agpu[in]=&f18agpu_czc; break;
    case 8: opcodef18agpu[in]=&f18agpu_xor; break;
    case 12:opcodef18agpu[in]=&f18agpu_xop; break;
    default: opcodef18agpu[in]=&f18agpu_bad; break;
    }
}

void f18agpu_initopcode3(unsigned short in)
{
    unsigned short x;

    x=(in&0x0c00)>>8;

    switch(x)
    {
    case 0: opcodef18agpu[in]=&f18agpu_ldcr; break;
    case 4: opcodef18agpu[in]=&f18agpu_stcr; break;
    case 8: opcodef18agpu[in]=&f18agpu_mpy;    break;
    case 12:opcodef18agpu[in]=&f18agpu_div;    break;
    default:opcodef18agpu[in]=&f18agpu_bad;  break;
    }
}
// ----------------------------------------------------------------------------------------

// Processor initialization
void f18agpu_init(void)
{
    int i;
    unsigned short in;
    BYTE x,z;

    f18agpu.WP = 0xF000; // Place workspace in an unused part of the memory space
    for (i = 0; i < 32; i++) {
        VDP_Memory[f18agpu.WP + i] = 0;
    }

    // Lookup tables
    // ---------------
    // Opcode table
    /*
         Formats:

         0   1  2  3  4  5  6  7      8  9  10 11 12 13 14 15
             +------------------------------------------------+
         1   | Opcode | B | Td |  RegNr     | Ts |    RegNr   |
             +--------+---+----+------------+----+------------+
         2   |  Opcode               |      Displacement      |
             +-----------------------+------------------------+
         3   |  Opcode         |  RegNr     | Ts |    RegNr   |
             +-----------------+------------+----+------------+
         4   |  Opcode         |  Count     | Ts |    RegNr   |
             +-----------------+------------+----+------------+
         5   |  Opcode               |  Count    |    RegNr   |
             +-----------------------+-----------+------------+
         6   |  Opcode                      | Ts |    RegNr   |
             +------------------------------+----+------------+
         7   |  Opcode                         |0| 0| 0| 0| 0 |
             +---------------------------------+-+--+--+--+---+
         8   |  Opcode                         |0|    RegNr   |
             +---------------------------------+-+------------+
         9   |  Opcode         |   Reg/Nr   | Ts |    RegNr   |
             +-----------------+------------+----+------------+
    */
    for (i=0; i<65536; i++)
    {
        in=(WORD)i;

        x=(in&0xf000)>>12;
        switch(x)
        {
        case 0: f18agpu_initopcode0(in);        break;  // 0000 xxxxxxxxxxxx
        case 1: f18agpu_initopcode1(in);        break;  // 0001 xxxxxxxxxxxx
        case 2: f18agpu_initopcode2(in);        break;  // 0010 xxxxxxxxxxxx
        case 3: f18agpu_initopcode3(in);        break;  // 0011 xxxxxxxxxxxx
        case 4: opcodef18agpu[in]=&f18agpu_szc; break;
        case 5: opcodef18agpu[in]=&f18agpu_szcb; break;
        case 6: opcodef18agpu[in]=&f18agpu_s;  break;
        case 7: opcodef18agpu[in]=&f18agpu_sb; break;
        case 8: opcodef18agpu[in]=&f18agpu_c;  break;
        case 9: opcodef18agpu[in]=&f18agpu_cb; break;
        case 10:opcodef18agpu[in]=&f18agpu_a;  break;
        case 11:opcodef18agpu[in]=&f18agpu_ab; break;
        case 12:opcodef18agpu[in]=&f18agpu_mov;    break;
        case 13:opcodef18agpu[in]=&f18agpu_movb; break;
        case 14:opcodef18agpu[in]=&f18agpu_soc;    break;
        case 15:opcodef18agpu[in]=&f18agpu_socb; break;
        default:opcodef18agpu[in]=&f18agpu_bad;  break;
        }
    }
    // new opcodes
    // CALL 0C80 - 0000 1100 10Ts SSSS
    for (int idx=0x0C80; idx<=0x0CBF; idx++) {
        opcodef18agpu[idx] = &f18agpu_callf18;
    }

    // RET  0C00 - 0000 1100 0000 0000
    opcodef18agpu[0x0c00] = &f18agpu_retf18;

    // PUSH 0D00 - 0000 1101 00Ts SSSS
    for (int idx=0x0D00; idx<=0x0D3F; idx++)
    {
        opcodef18agpu[idx]=&f18agpu_pushf18;
    }

    // POP  0F00 - 0000 1111 00Td DDDD
    for (int idx=0x0F00; idx<=0x0f3F; idx++) {
        opcodef18agpu[idx]=&f18agpu_popf18;
    }

    // SLC  0E00 - 0000 1110 00Ts SSSS
    for (int idx=0x0E00; idx<=0x0E3F; idx++) {
        opcodef18agpu[idx]=&f18agpu_slcf18;
    }

#if 0
    // Modified opcodes
    
    // IDLE = IDLE     Forces the GPU state machine to the idle state, restart with a trigger from host
    opcode[0x0340] = &CPU9900::op_idleF18;

    //TODO: be smart about these later
    for (int idx=0; idx<0xffff; idx++) {
        // XOP  = PIX       The new dedicated pixel plotting instruction
        if (opcode[idx] == &CPU9900::op_xop) opcode[idx]=&CPU9900::op_pixF18;

        // CKON = SPI !CE Sets the chip enable line to the SPI Flash ROM low (enables the ROM)
        if (opcode[idx] == &CPU9900::op_ckon) opcode[idx]=&CPU9900::op_csonF18;

        // CKOF = SPI CE  Sets the chip enable line to the SPI Flash ROM high (disables the ROM)
        if (opcode[idx] == &CPU9900::op_ckof) opcode[idx]=&CPU9900::op_csoffF18;

        // LDCR = SPI OUT Writes a byte (always a byte operation) to the SPI Flash ROM
        if (opcode[idx] == &CPU9900::op_ldcr) opcode[idx]=&CPU9900::op_spioutF18;

        // STCR = SPI IN  Reads a byte (always a byte operation) from the SPI Flash ROM
        if (opcode[idx] == &CPU9900::op_stcr) opcode[idx]=&CPU9900::op_spiinF18;

        // RTWP = RTWP     Modified, does not use R13, only performs R14->PC, R15->status flags
        if (opcode[idx] == &CPU9900::op_rtwp) opcode[idx]=&CPU9900::op_rtwpF18;

        // Unimplemented
        if (opcode[idx] == &CPU9900::op_sbo) opcode[idx]=&CPU9900::op_bad;
        if (opcode[idx] == &CPU9900::op_sbz) opcode[idx]=&CPU9900::op_bad;
        if (opcode[idx] == &CPU9900::op_tb) opcode[idx]=&CPU9900::op_bad;
        if (opcode[idx] == &CPU9900::op_blwp) opcode[idx]=&CPU9900::op_bad;
        if (opcode[idx] == &CPU9900::op_stwp) opcode[idx]=&CPU9900::op_bad;
        if (opcode[idx] == &CPU9900::op_lwpi) opcode[idx]=&CPU9900::op_bad;
        if (opcode[idx] == &CPU9900::op_limi) opcode[idx]=&CPU9900::op_bad;
        if (opcode[idx] == &CPU9900::op_rset) opcode[idx]=&CPU9900::op_bad;
        if (opcode[idx] == &CPU9900::op_lrex) opcode[idx]=&CPU9900::op_bad;
    }
#endif

    // Word status lookup table
    for (i = 0; i < 0x10000; i++)
    {
        wStatusLookup[i] = 0;
        if (i > 0) wStatusLookup[i] |= F18AGPU_BIT_LGT; // LGT
        if ((i > 0) && (i < 0x8000)) wStatusLookup[i] |= F18AGPU_BIT_AGT; // AGT
        if (i == 0) wStatusLookup[i] |= F18AGPU_BIT_EQ; // EQ
        if (i == 0) wStatusLookup[i] |= F18AGPU_BIT_C; // C
        if (i == 0x8000) wStatusLookup[i] |= F18AGPU_BIT_OV; // OV
    }

    // Byte status lookup table
    for (i = 0; i < 0x100; i++) {
        x = (i & 0xFF);
        bStatusLookup[i] = 0;
        if (i > 0) bStatusLookup[i] |= F18AGPU_BIT_LGT; // LGT
        if ((i > 0) && (i < 0x80)) bStatusLookup[i] |= F18AGPU_BIT_AGT; // AGT
        if (i == 0) bStatusLookup[i] |= F18AGPU_BIT_EQ; // EQ
        if (i == 0) bStatusLookup[i] |= F18AGPU_BIT_C; // C
        if (i == 0x80) bStatusLookup[i] |= F18AGPU_BIT_OV; // OV

        // OP
        for (z = 0; x != 0; x = (x & (x - 1)) & 0xFF) {
            z++;						// black magic?
        }
        if ((z & 1) != 0) bStatusLookup[i] |= F18AGPU_BIT_OP;		    // set bit if an odd number
    }

/*
    // Flash RAM
    var that = this;
    f18agpu.flash = new F18AFlash(function (restored) {
        that.flashLoaded = restored;
        that.reset();
    });
*/
};
// ----------------------------------------------------------------------------------------

// Interrupt reset the F18A GPU
void f18agpu_intreset(void)
{
    f18agpu.cpuIdle = 1;

    // Internal registers
    f18agpu.PC = 0x4000;
    f18agpu.ST = 0x01C0;
    f18agpu.flagX = 0;

    // Operands
    f18agpu.Ts = 0;
    f18agpu.Td = 0;
    f18agpu.D = 0;
    f18agpu.S = 0;
    f18agpu.B = 0;
    f18agpu.nPostInc[0]=f18agpu.nPostInc[1]=0;

    // Counters
    f18agpu.cycles = 0;
}
// ----------------------------------------------------------------------------------------

// Reset the F18A GPU
void f18agpu_reset(void)
{
    int i;
    unsigned short hexVal;
    char shexval[2];

    f18agpu_intreset();

//f18agpu.flash.reset();
    // Load and execute GPU code
    //if (f18agpu.flashLoaded)
    //{
        for (i = 0; i < F18AGPU_PRELOADSIZE; i++)
        {
            shexval[0]=F18AGPU_PRELOAD[i*2];
            shexval[1]=F18AGPU_PRELOAD[i*2+1];
            hexVal=(int)strtoul(shexval, NULL, 16);
            VDP_Memory[0x4000 + i] = hexVal;
        }
    //}
    //f18agpu_setpc(0x4000);
};
// ----------------------------------------------------------------------------------------

void f18agpu_setpc(unsigned short value)
{
    f18agpu.PC =value;
    f18agpu.cpuIdle = 0;
};

// ----------------------------------------------------------------------------------------
// force the PC to always be 15-bit, can't store a 16-bit PC
void f18agpu_addpc(signed short value)
{
    f18agpu.PC = (f18agpu.PC + value) & 0xFFFE;
};

// ----------------------------------------------------------------------------------------

void f18agpu_writeword(unsigned short addr, unsigned short value)
{
    f18agpu_writebyte(addr, (BYTE) ((value & 0xFF00) >> 8) );
    f18agpu_writebyte(addr + 1, (BYTE) (value & 0x00FF) );
};
// ----------------------------------------------------------------------------------------

void f18agpu_writebyte(unsigned short addr, BYTE value)
{
    BYTE colno,dmacopy,dmasrcByte;
    unsigned short dmasrc,dmadst,dmawidth,dmaheight,dmastride,x,y;
    signed short dmadiff;
    signed char dmadir;

    // GPU register
    if (addr >= f18agpu.WP) { // GPU register
        VDP_Memory[addr] = value;
    }
    else if (addr < 0x4000) { // VRAM
        VDP_Memory[addr] = value;
//    f18agpu.f18a.redrawRequired = true;
    }
    else if (addr < 0x5000) { // GRAM
        VDP_Memory[addr & 0x47FF] = value;
    }
    else if (addr < 0x6000) { // PRAM
        colno = ((addr & 0x7F) >> 1)*3;
        if ((addr & 1) == 0) {
            cv_palette[colno+0] = (value & 0x0F) * 17; // MSB
        }
        else
        {
            cv_palette[colno+1] = ((value & 0xF0) >> 4) * 17; // LSB
            cv_palette[colno+2] = (value & 0x0F) * 17;
        }
        f18a.PalRecalc = 1;
    }
    else if (addr < 0x7000) // VREG >6000->603F
    {
        WriteF18A(addr & 0x3F, value);
    }
    else if (addr < 0x8000) // Scanline and blanking
    {
            //  Read only
    }
    else if (addr < 0x9000) { // DMA
        if ((addr & 0xF) == 8) {
            // Trigger DMA
            dmasrc = (VDP_Memory[0x8000] << 8) | VDP_Memory[0x8001];
            dmadst = (VDP_Memory[0x8002] << 8) | VDP_Memory[0x8003];
            dmawidth = VDP_Memory[0x8004];
            // if (width === 0) { width = 0x100; }
            dmaheight = VDP_Memory[0x8005];
            // if (height === 0) { height = 0x100; }
            dmastride = VDP_Memory[0x8006];
            // if (stride === 0) { stride = 0x100; }
            dmadir = (VDP_Memory[0x8007] & 0x02) == 0 ? 1 : -1;
            dmadiff = dmadir * (dmastride - dmawidth);
            dmacopy = (VDP_Memory[0x8007] & 0x01) == 0 ? 1 : 0;
            dmasrcByte = VDP_Memory[dmasrc];
            if (dmacopy) {
                for (y = 0; y < dmaheight; y++) {
                    for (x = 0; x < dmawidth; x++) {
                        VDP_Memory[dmadst] = VDP_Memory[dmasrc];
                        dmasrc += dmadir;
                        dmadst += dmadir;
                    }
                    dmasrc += dmadiff;
                    dmadst += dmadiff;
                }
            }
            else {
                for (y = 0; y < dmaheight; y++) {
                    for (x = 0; x < dmawidth; x++) {
                        VDP_Memory[dmadst] = dmasrcByte;
                        dmadst += dmadir;
                    }
                    dmadst += dmadiff;
                }
            }
            F18AGPUADDCYCLE(dmawidth * dmaheight); // ?
            //f18agpu.f18a.redrawRequired = true;
        }
        else { //   -- DMA              @ >8000 to >8xx7 (1000 xxxx xxxx 0111) --
            // Setup
            VDP_Memory[addr & 0x800F] = value;
        }
    }
    else if (addr < 0xA000) {
    }
    else if (addr < 0xB000)  {//   -- F18A version     @ >A000 to >Axxx (1010 xxxx xxxx xxxx) --
        //  Read only
    }
    else if (addr < 0xC000) { // //   -- GPU status data  @ >B000 to >Bxxx (1011 xxxx xxxx xxxx) --
        VDP_Memory[0xB000] = value & 0x7F; // 7 least significant bits, goes to an enhanced status register for the host CPU to read
    }
};
// ----------------------------------------------------------------------------------------

unsigned short f18agpu_readword(unsigned short addr)
{
    return ( (f18agpu_readbyte(addr) << 8) | f18agpu_readbyte(addr+1) );
};
// ----------------------------------------------------------------------------------------

BYTE f18agpu_readbyte(unsigned short addr)
{
    BYTE color;

    if (addr >= f18agpu.WP) { // GPU register
        return VDP_Memory[addr];
    }
    if (addr < 0x4000) { // VRAM
        return VDP_Memory[addr];
    }
    if (addr < 0x5000) { // GRAM
        return VDP_Memory[addr & 0x47FF];
    }
    if (addr < 0x6000) { // PRAM
        color = ((addr & 0x7F) >> 1)*3;
        if ((addr & 1) == 0) {
            return (cv_palette[color] / 17); // MSB
        }
        else {
            return ( ((cv_palette[color+1] / 17) << 4) | (cv_palette[color+2] / 17)); // LSB
        }
    }
    if (addr < 0x7000) { // VREG >6000->603F
        return f18a.VDPR[addr & 0x3F];
    }
    if (addr < 0x8000) { // Scanline and blanking
        if ((addr & 1) == 0) {
            // Current scanline
            return 0x00; // TODO return f18agpu.f18a.getCurrentScanline();
        }
        else  {
            // Blanking
            return 0x00; // TODO return f18agpu.f18a.getBlanking();
        }
    }
    if (addr < 0x9000) { // DMA
        // TODO: can you read the DMA?
        return 0;
    }
    else if (addr < 0xA000) { // Unused
        return 0;
    }
    else if (addr < 0xB000) { // Version
        return F18A_VERSION;
    }
    else if (addr < 0xC000) { // Status data
        return 0; // Write only
    }

    return 0;
};
// ----------------------------------------------------------------------------------------

void f18agpu_execute(int cycles)
{
    int startCycles=f18agpu.cycles;

    while (!f18agpu.cpuIdle && (f18agpu.cycles - startCycles < cycles) )
    {
        // Execute instruction
        f18agpu.in = f18agpu_readword(f18agpu.PC);
        f18agpu.PC = (f18agpu.PC + 2) & 0xFFFF;
        (*opcodef18agpu[f18agpu.in])();
    }
};
// ----------------------------------------------------------------------------------------
void f18agpu_postIncrement(BYTE nWhich)
{
    BYTE i;
    unsigned short t2, nTmpVal;
    int tmpCycles;

    if (f18agpu.nPostInc[nWhich]) {
        i = f18agpu.nPostInc[nWhich] & 0xf;
        t2 = f18agpu.WP + (i << 1);

        tmpCycles = f18agpu.cycles;
        nTmpVal = f18agpu_readword(t2);	// We need to reread this value, but the memory access can't count for cycles
        f18agpu.cycles = tmpCycles;

        f18agpu_writeword(t2, (nTmpVal + ((f18agpu.nPostInc[nWhich] & F18AGPU_POSTINC2) != 0 ? 2 : 1)) & 0xFFFF);
        f18agpu.nPostInc[nWhich] = 0;
    }
};

// ----------------------------------------------------------------------------------------

// Get addresses for the destination and source arguments
// Note: the format code letters are the official notation from Texas instruments.
// See their TMS9900 documentation for details.
// (Td, Ts, D, S, B, etc)
// Note that some format codes set the destination type (Td) to
// '4' in order to skip unneeded processing of the Destination address
void f18agpu_fixs(void)
{
    unsigned short cycles = 0;
    int temp, t2;

    // source type
    switch (f18agpu.Ts)
    {
    case 0: // register	(R1) Address is the address of the register
        f18agpu.S = f18agpu.WP + (f18agpu.S << 1);
        break;
    case 1: // register indirect (*R1) Address is the contents of the register
        f18agpu.S = f18agpu_readword(f18agpu.WP + (f18agpu.S << 1));
        cycles += 4;
        break;
    case 2:
        if (f18agpu.S != 0) {
            // indexed (@>1000(R1))	Address is the contents of the argument plus the contents of the register
            f18agpu.S = f18agpu_readword(f18agpu.PC) + f18agpu_readword(f18agpu.WP + (f18agpu.S << 1));
        }
        else {
            // symbolic	 (@>1000) Address is the contents of the argument
            f18agpu.S = f18agpu_readword(f18agpu.PC);
        }
        ADDPC(2);
        cycles += 8;
        break;
    case 3: // do the increment after the opcode is done with the source
        f18agpu.nPostInc[F18AGPU_SRC] = f18agpu.S | (f18agpu.B == 1 ? F18AGPU_POSTINC1 : F18AGPU_POSTINC2);
        t2 = f18agpu.WP + (f18agpu.S << 1);
        temp = f18agpu_readword(t2);
        f18agpu.S = temp;
        // (add 1 if byte, 2 if word) (*R1+) Address is the contents of the register, which
        // register indirect autoincrement is incremented by 1 for byte or 2 for word ops
        cycles += f18agpu.B == 1 ? 6 : 8;
        break;
    }
    F18AGPUADDCYCLE(cycles);
};
// ----------------------------------------------------------------------------------------
void f18agpu_fixd(void)
{
    unsigned short cycles = 0;
    int temp, t2;

    // destination type
    // same as the source types
    switch (f18agpu.Td)
    {
    case 0: // register
        f18agpu.D = f18agpu.WP + (f18agpu.D << 1);
        break;
    case 1: // register indirect
        f18agpu.D = f18agpu_readword(f18agpu.WP + (f18agpu.D << 1));
        cycles += 4;
        break;
    case 2:
        if (f18agpu.D != 0) {
            // indexed
            f18agpu.D = f18agpu_readword(f18agpu.PC) + f18agpu_readword(f18agpu.WP + (f18agpu.D << 1));
        }
        else {
            // symbolic
            f18agpu.D = f18agpu_readword(f18agpu.PC);
        }
        ADDPC(2);
        cycles += 8;
        break;
    case 3:
        // do the increment after the opcode is done with the dest
        f18agpu.nPostInc[F18AGPU_DST] = f18agpu.D | (f18agpu.B == 1 ? F18AGPU_POSTINC1 : F18AGPU_POSTINC2);
        // (add 1 if byte, 2 if word)
        t2 = f18agpu.WP + (f18agpu.D << 1);
        temp = f18agpu_readword(t2);
        f18agpu.D = temp;
        // register indirect autoincrement
        cycles += f18agpu.B == 1 ? 6 : 8;
        break;
    }
    F18AGPUADDCYCLE(cycles);
};

// ----------------------------------------------------------------------------------------

// all 9900 / F18A opcodes
// Load Immediate: LI src, imm
void f18agpu_li(void)
{
    F18AGPUADDCYCLE(12);

    F18AGPUFORMATVIII_1;       // read immediate
    f18agpu_writeword(f18agpu.D, f18agpu.S);     // write register

    F18AGPURESETLGT_AGT_EQ;
    f18agpu.ST |= wStatusLookup[f18agpu.S] & F18AGPU_MSKLGT_AGT_EQ;
};

// Add Immediate: AI src, imm
void f18agpu_ai(void)
{
    unsigned short x1,x3;

    F18AGPUADDCYCLE(14);

    F18AGPUFORMATVIII_1;       // read immediate
    x1 = f18agpu_readword(f18agpu.D);   // read dest

    x3 = (x1 + f18agpu.S) & 0xFFFF;
    f18agpu_writeword(f18agpu.D, x3); // write dest

    F18AGPURESETEQ_LGT_AGT_C_OV;
    f18agpu.ST |= wStatusLookup[x3] & F18AGPU_MSKLGT_AGT_EQ;

    if (x3 < x1) F18AGPUSET_C;
    if (((x1 & 0x8000) == (f18agpu.S & 0x8000)) && ((x3 & 0x8000) != (f18agpu.S & 0x8000))) F18AGPUSET_OV;
};

// AND Immediate: ANDI src, imm
void f18agpu_andi(void)
{
    unsigned short x1,x2;

    F18AGPUADDCYCLE(14);

    F18AGPUFORMATVIII_1;       // read immediate

    x1 = f18agpu_readword(f18agpu.D);       // read dest
    x2 = x1 & f18agpu.S;
    f18agpu_writeword(f18agpu.D, x2);          // write dest

    F18AGPURESETLGT_AGT_EQ;
    f18agpu.ST |= wStatusLookup[x2] & F18AGPU_MSKLGT_AGT_EQ;
};

// OR Immediate: ORI src, imm
void f18agpu_ori(void)
{
    unsigned short x1,x2;

    F18AGPUADDCYCLE(14);

    F18AGPUFORMATVIII_1;       // read immediate

    x1 = f18agpu_readword(f18agpu.D);      // read dest
    x2 = x1 | f18agpu.S;
    f18agpu_writeword(f18agpu.D, x2);        // write dest

    F18AGPURESETLGT_AGT_EQ;
    f18agpu.ST |= wStatusLookup[x2] & F18AGPU_MSKLGT_AGT_EQ;
};

// Compare Immediate: CI src, imm
void f18agpu_ci(void)
{
    unsigned short x3;

    F18AGPUADDCYCLE(14);

    F18AGPUFORMATVIII_1;       // read source
    x3 = f18agpu_readword(f18agpu.D);    // read dest

    F18AGPURESETLGT_AGT_EQ;
    if (x3 > f18agpu.S) F18AGPUSET_LGT;
    if (x3 == f18agpu.S) F18AGPUSET_EQ;
    if ((x3 & 0x8000) == (f18agpu.S & 0x8000)) {
        if (x3 > f18agpu.S) F18AGPUSET_AGT;
    }
    else {
        if ((f18agpu.S & 0x8000) != 0) F18AGPUSET_AGT;
    }
};

// STore Workspace Pointer: STWP src
// Copy the workspace pointer to memory
void f18agpu_stwp(void)
{
    // Base cycles: 8
    // 2 memory accesses:
    //  Read instruction (already done)
    //  Write dest
    F18AGPUADDCYCLE(8);

    F18AGPUFORMATVIII_0;
    f18agpu_writeword(f18agpu.D,f18agpu.WP);   // write dest
};

// STore STatus: STST src
// Copy the status register to memory
void f18agpu_stst(void)
{
    // Base cycles: 8
    // 2 memory accesses:
    //  Read instruction (already done)
    //  Write dest
    F18AGPUADDCYCLE(8);

    F18AGPUFORMATVIII_0;
    f18agpu_writeword(f18agpu.D, f18agpu.ST);   // write dest
};

// Load Workspace Pointer Immediate: LWPI imm
// changes the Workspace Pointer
void f18agpu_lwpi(void)
{
    // Base cycles: 10
    // 2 memory accesses:
    //  Read instruction (already done)
    //  Read immediate
    F18AGPUADDCYCLE(10);

    F18AGPUFORMATVIII_1;   // read immediate
    f18agpu.WP=f18agpu.S&0xfffe;
};

// Load Interrupt Mask Immediate: LIMI imm
// Sets the CPU interrupt mask
void f18agpu_limi(void)
{
    F18AGPUADDCYCLE(16);

    F18AGPUFORMATVIII_1;   // read immediate
    f18agpu.ST=(f18agpu.ST&0xfff0)|(f18agpu.S&0xf);
};

// This sets A0-A2 to 010, and pulses CRUCLK until an interrupt is received.
void f18agpu_idle(void)
{
    F18AGPUFormatVII;

    f18agpu.cpuIdle=1;
    F18AGPUADDCYCLE(12);
};

// This will set A0-A2 to 011 and pulse CRUCLK (so not emulated)
// However, it does have an effect, it zeros the interrupt mask
void f18agpu_rset(void)
{
    F18AGPUADDCYCLE(12);

    F18AGPUFormatVII;
    f18agpu.ST&=0xfff0;
};

// ReTurn with Workspace Pointer: RTWP
// The matching return for BLWP, see BLWP for description
// F18A Modified, does not use R13, only performs R14->PC, R15->status flags
void f18agpu_rtwp(void)
{
    F18AGPUADDCYCLE(14);

    F18AGPUFormatVII;

    f18agpu.ST = f18agpu_readword(f18agpu.WP + 30); // R15
    f18agpu.PC = f18agpu_readword(f18agpu.WP + 28); // R14
    f18agpu.WP = f18agpu_readword(f18agpu.WP + 26); // WP<-R13 -- needs to be last!
};

// This is the SPI_EN instruction of the F18A GPU
// This will set A0-A2 to 101 and pulse CRUCLK (so not emulated)
void f18agpu_ckon(void)
{
    F18AGPUADDCYCLE(12);

    F18AGPUFormatVII;
    //f18agpu.flash.enable();
};

// This is the SPI_DS instruction of the F18A GPU
// This will set A0-A2 to 110 and pulse CRUCLK (so not emulated)
void f18agpu_ckof(void)
{
    F18AGPUADDCYCLE(12);

    F18AGPUFormatVII;
    //f18agpu.flash.disable();
};

// This will set A0-A2 to 111 and pulse CRUCLK
void f18agpu_lrex(void)
{
    F18AGPUADDCYCLE(12);

    F18AGPUFormatVII;
};

// Branch and Load Workspace Pointer: BLWP src
// A context switch. The src address points to a 2 word table.
// the first word is the new workspace address, the second is
// the address to branch to. The current Workspace Pointer,
// Program Counter (return address), and Status register are
// stored in the new R13, R14 and R15, respectively
// Return is performed with RTWP
void f18agpu_blwp(void)
{
    F18AGPUADDCYCLE(26);

    F18AGPUFormatVI;
/*
        // Not implemented
    if (0 == GetReturnAddress()) {
        SetReturnAddress(PC);
    }
    x1=WP;
    SetWP(ROMWORD(S));      // read WP
    WRWORD(WP+26,x1);       // write WP->R13
    WRWORD(WP+28,PC);       // write PC->R14
    WRWORD(WP+30,ST);       // write ST->R15
    SetPC(ROMWORD(S+2));    // read PC

*/
};

// Branch: B src
// Unconditional absolute branch
void f18agpu_b(void)
{
    F18AGPUADDCYCLE(8);

    F18AGPUFormatVI;
    f18agpu.PC = f18agpu.S;
    f18agpu_postIncrement(F18AGPU_SRC);

};

// eXecute: X src
// The argument is interpreted as an instruction and executed
void f18agpu_x(void)
{
    unsigned short xInstr;

    if (f18agpu.flagX != 0) {
            //f18agpu.log.info("Recursive X instruction!");
    }
    F18AGPUADDCYCLE(8 - 4);	        // For X, add this time to the execution time of the instruction found at the source address, minus 4 clock cycles and 1 memory access.

    F18AGPUFormatVI;
    xInstr = f18agpu_readword(f18agpu.S);
    f18agpu_postIncrement(F18AGPU_SRC);	// does this go before or after the eXecuted instruction??
        // skip_interrupt=1;	    // (ends up having no effect because we call the function inline, but technically still correct)

    f18agpu.flagX = f18agpu.PC;	    // set flag and save true post-X address for the JMPs (AFTER X's operands but BEFORE the instruction's operands, if any)
    (*opcodef18agpu[xInstr])();

    f18agpu.flagX = 0;			    // clear flag
};

// CLeaR: CLR src
// sets word to 0
void f18agpu_clr(void)
{
    F18AGPUADDCYCLE(10);

    F18AGPUFormatVI;
    f18agpu_writeword(f18agpu.S, 0);    // write source
};

// NEGate: NEG src
void f18agpu_neg(void)
{
    unsigned short x1;

    F18AGPUADDCYCLE(12);

    F18AGPUFormatVI;
    x1 = f18agpu_readword(f18agpu.S);          // read source

    x1 = ((~x1) + 1) & 0xFFFF;
    f18agpu_writeword(f18agpu.S, x1);      // write source
    f18agpu_postIncrement(F18AGPU_SRC);

    F18AGPURESETEQ_LGT_AGT_C_OV;
    f18agpu.ST |= wStatusLookup[x1] & F18AGPU_MSKLGT_AGT_EQ_OV_C;
};

// INVert: INV src
void f18agpu_inv(void)
{
    unsigned short x1;

    F18AGPUADDCYCLE(10);

    F18AGPUFormatVI;

    x1 = f18agpu_readword(f18agpu.S);       // read source
    x1 = (~x1) & 0xFFFF;
    f18agpu_writeword(f18agpu.S, x1);       // write source
    f18agpu_postIncrement(F18AGPU_SRC);

    F18AGPURESETLGT_AGT_EQ;
    f18agpu.ST |= wStatusLookup[x1] & F18AGPU_MSKLGT_AGT_EQ;
};

// INCrement: INC src
void f18agpu_inc(void)
{
    unsigned short x1;

    F18AGPUADDCYCLE(10);

    F18AGPUFormatVI;
    x1 = f18agpu_readword(f18agpu.S);           // read source

    x1 = (x1 + 1) & 0xFFFF;
    f18agpu_writeword(f18agpu.S, x1);           // write source
    f18agpu_postIncrement(F18AGPU_SRC);

    F18AGPURESETEQ_LGT_AGT_C_OV;
    f18agpu.ST |= wStatusLookup[x1] & F18AGPU_MSKLGT_AGT_EQ_OV_C;
};

// INCrement by Two: INCT src
void f18agpu_inct(void)
{
    unsigned short x1;

    F18AGPUADDCYCLE(10);

    F18AGPUFormatVI;
    x1 = f18agpu_readword(f18agpu.S);       // read source

    x1 = (x1 + 2) & 0xFFFF;
    f18agpu_writeword(f18agpu.S, x1);          // write source
    f18agpu_postIncrement(F18AGPU_SRC);

    F18AGPURESETEQ_LGT_AGT_C_OV;
    f18agpu.ST |= wStatusLookup[x1] & F18AGPU_MSKLGT_AGT_EQ;

    if (x1 < 2) F18AGPUSET_C;
    if ((x1 == 0x8000) || (x1 == 0x8001)) F18AGPUSET_OV;
};

// DECrement: DEC src
void f18agpu_dec(void)
{
    unsigned short x1;

    F18AGPUADDCYCLE(10);

    F18AGPUFormatVI;
    x1 = f18agpu_readword(f18agpu.S);        // read source

    x1 = (x1 - 1) & 0xFFFF;
    f18agpu_writeword(f18agpu.S, x1);          // write source
    f18agpu_postIncrement(F18AGPU_SRC);

    F18AGPURESETEQ_LGT_AGT_C_OV;
    f18agpu.ST |= wStatusLookup[x1] & F18AGPU_MSKLGT_AGT_EQ;

    if (x1 != 0xffff) F18AGPUSET_C;
    if (x1 == 0x7fff) F18AGPUSET_OV;
};

// DECrement by Two: DECT src
void f18agpu_dect(void)
{
    unsigned short x1;

    F18AGPUADDCYCLE(10);

    F18AGPUFormatVI;
    x1 = f18agpu_readword(f18agpu.S);    // read source

    x1 = (x1 - 2) & 0xFFFF;
    f18agpu_writeword(f18agpu.S, x1);           // write source
    f18agpu_postIncrement(F18AGPU_SRC);

    F18AGPURESETEQ_LGT_AGT_C_OV;
    f18agpu.ST |= wStatusLookup[x1] & F18AGPU_MSKLGT_AGT_EQ;

    if (x1 < 0xfffe) F18AGPUSET_C;
    if ((x1 == 0x7fff) || (x1 == 0x7ffe)) F18AGPUSET_OV;
};

// Branch and Link: BL src
// Essentially a subroutine jump - return address is stored in R11
// Note there is no stack, and no official return function.
// A return is simply B *R11. Some assemblers define RT as f18agpu.
void f18agpu_bl(void)
{
    F18AGPUADDCYCLE(12);

    F18AGPUFormatVI;

    f18agpu_writeword(f18agpu.WP + 22, f18agpu.PC);    // write return
    f18agpu.PC = f18agpu.S;
    f18agpu_postIncrement(F18AGPU_SRC);
};

// SWaP Bytes: SWPB src
// swap the high and low bytes of a word
void f18agpu_swpb(void)
{
    unsigned short x1,x2;

    F18AGPUADDCYCLE(10);

    F18AGPUFormatVI;
    x1 = f18agpu_readword(f18agpu.S);        // read source

    x2 = ((x1 & 0xff) << 8) | (x1 >> 8);
    f18agpu_writeword(f18agpu.S, x2);        // write source
    f18agpu_postIncrement(F18AGPU_SRC);
};

// SET to One: SETO src
// sets word to 0xffff
void f18agpu_seto(void)
{
    F18AGPUADDCYCLE(10);

    F18AGPUFormatVI;
    f18agpu_writeword(f18agpu.S, 0xffff);      // write source
    f18agpu_postIncrement(F18AGPU_SRC);
};

// ABSolute value: ABS src
void f18agpu_abs(void)
{
    unsigned short x1,x2;

    F18AGPUADDCYCLE(12);

    F18AGPUFormatVI;
    x1 = f18agpu_readword(f18agpu.S);    // read source

    if (x1 & 0x8000) {
        x2 = ((~x1) + 1) & 0xFFFF;	// if negative, make positive
        f18agpu_writeword(f18agpu.S, x2);   // write source
        F18AGPUADDCYCLE(2);
    }
    f18agpu_postIncrement(F18AGPU_SRC);

    F18AGPURESETEQ_LGT_AGT_C_OV;
    f18agpu.ST |= wStatusLookup[x1] & F18AGPU_MSKLGT_AGT_EQ_OV;
};

// Shift Right Arithmetic: SRA src, dst
// For the shift instructions, a count of '0' means use the
// value in register 0. If THAT is zero, the count is 16.
// The arithmetic operations preserve the sign bit
void f18agpu_sra(void)
{
    unsigned short x1,x3,x4;
    int x2;

    F18AGPUADDCYCLE(12);

    F18AGPUFormatV;
    if (f18agpu.D == 0) {
        f18agpu.D = f18agpu_readword(f18agpu.WP) & 0xf;       // read R0
        if (f18agpu.D == 0) f18agpu.D = 16;
        F18AGPUADDCYCLE(8);
    }
    x1 = f18agpu_readword(f18agpu.S);                // read source
    x4 = x1 & 0x8000;
    x3 = 0;

    for (x2 = 0; x2 < f18agpu.D; x2++) {
        x3 = x1 & 1;   // save carry
        x1 = x1 >> 1;  // shift once
        x1 = x1 | x4;  // extend sign bit
    }
    f18agpu_writeword(f18agpu.S, x1);        // write source

    F18AGPURESETEQ_LGT_AGT_C;
    f18agpu.ST |= wStatusLookup[x1] & F18AGPU_MSKLGT_AGT_EQ;

    if (x3 != 0) F18AGPUSET_C;

    F18AGPUADDCYCLE(2 * f18agpu.D);
};

// Shift Right Logical: SRL src, dst
// The logical shifts do not preserve the sign
void f18agpu_srl(void)
{
    unsigned short x1,x3;
    int x2;

    F18AGPUADDCYCLE(12);

    F18AGPUFormatV;
    if (f18agpu.D == 0) {
        f18agpu.D = f18agpu_readword(f18agpu.WP) & 0xf;       // read R0
        if (f18agpu.D == 0) f18agpu.D = 16;
        F18AGPUADDCYCLE(8);
    }
    x1 = f18agpu_readword(f18agpu.S);             // read source
    x3 = 0;

    for (x2 = 0; x2 < f18agpu.D; x2++) {
        x3 = x1 & 1;
        x1= x1 >> 1;
    }
    f18agpu_writeword(f18agpu.S, x1);

    F18AGPURESETEQ_LGT_AGT_C;
    f18agpu.ST |= wStatusLookup[x1] & F18AGPU_MSKLGT_AGT_EQ;

    if (x3 != 0) F18AGPUSET_C;

    F18AGPUADDCYCLE(2 * f18agpu.D);
};

// Shift Left Arithmetic: SLA src, dst
void f18agpu_sla(void)
{
    unsigned short x1,x3,x4;
    int x2;

    F18AGPUADDCYCLE(12);

    F18AGPUFormatV;

    if (f18agpu.D == 0) {
        f18agpu.D = f18agpu_readword(f18agpu.WP) & 0xf;      // read R0
        if (f18agpu.D == 0) f18agpu.D = 16;
        F18AGPUADDCYCLE(8);
    }
    x1 = f18agpu_readword(f18agpu.S);                // read source
    x4 = x1 & 0x8000;
    F18AGPURESETEQ_LGT_AGT_C_OV;

    x3=0;
    for (x2 = 0; x2 < f18agpu.D; x2++) {
        x3 = x1 & 0x8000;
        x1 = x1 << 1;
        if ((x1 & 0x8000) != x4) F18AGPUSET_OV;
    }
    x1 = x1 & 0xFFFF;
    f18agpu_writeword(f18agpu.S , x1);          // write source

    f18agpu.ST |= wStatusLookup[x1] & F18AGPU_MSKLGT_AGT_EQ;

    if (x3 != 0) F18AGPUSET_C;

    F18AGPUADDCYCLE(2 * f18agpu.D);
};

// Shift Right Circular: SRC src, dst
// Circular shifts pop bits off one end and onto the other
// The carry bit is not a part of these shifts, but it set
// as appropriate
void f18agpu_src(void)
{
    BYTE cycles;
    //unsigned short x1,x2,x4;
    int x2;
    unsigned short x1,x4;

    F18AGPUADDCYCLE(12);

    F18AGPUFormatV;

    cycles = 0;
    if (f18agpu.D == 0) {
        f18agpu.D = f18agpu_readword(f18agpu.WP) & 0xf;         // read R0
        if (f18agpu.D == 0) f18agpu.D=16;
        F18AGPUADDCYCLE(8);
    }
    x1 = f18agpu_readword(f18agpu.S);                      // read source
    for (x2 = 0; x2 < f18agpu.D; x2++) {
        x4 = x1 & 0x1;
        x1 = x1 >> 1;
        if (x4 != 0) {
            x1 = x1 | 0x8000;
        }
    }
    f18agpu_writeword(f18agpu.S, x1);    // write source

    F18AGPURESETEQ_LGT_AGT_C;
    f18agpu.ST |= wStatusLookup[x1] & F18AGPU_MSKLGT_AGT_EQ;

    if (x4 != 0) F18AGPUSET_C;

    F18AGPUADDCYCLE(2 * f18agpu.D);
};

// JuMP: JMP dsp
// (unconditional)
void f18agpu_jmp(void)
{
    F18AGPUADDCYCLE(10);

    F18AGPUFormatII;
    if (f18agpu.flagX != 0) {
        f18agpu.PC = f18agpu.flagX;	// Update offset - it's relative to the X, not the opcode
    }
    if ((f18agpu.D & 0x80) != 0) {
        f18agpu.D = 128 - (f18agpu.D & 0x7f);
        f18agpu_addpc(-(f18agpu.D + f18agpu.D));
    }
    else {
        f18agpu_addpc(f18agpu.D + f18agpu.D);
    }

};

// Jump if Less Than: JLT dsp
void f18agpu_jlt(void)
{
    F18AGPUADDCYCLE(8);

    F18AGPUFormatII;

    if ((F18AGPUGET_AGT == 0) && (F18AGPUGET_EQ == 0)) {
        if (f18agpu.flagX != 0) {
            f18agpu.PC = f18agpu.flagX;	// Update offset - it's relative to the X, not the opcode
        }
        if ((f18agpu.D & 0x80) != 0) {
            f18agpu.D = 128 - (f18agpu.D & 0x7f);
            f18agpu_addpc(-(f18agpu.D + f18agpu.D));
        }
        else {
            f18agpu_addpc(f18agpu.D + f18agpu.D);
        }
        F18AGPUADDCYCLE(10-8);
    }
};

// Jump if Low or Equal: JLE dsp
void f18agpu_jle(void)
{
    F18AGPUADDCYCLE(8);

    F18AGPUFormatII;

    if ((F18AGPUGET_LGT == 0) || (F18AGPUGET_EQ != 0)) {
        if (f18agpu.flagX != 0) {
            f18agpu.PC = f18agpu.flagX;	// Update offset - it's relative to the X, not the opcode
        }
        if ((f18agpu.D & 0x80) != 0) {
            f18agpu.D = 128 - (f18agpu.D & 0x7f);
            f18agpu_addpc(-(f18agpu.D + f18agpu.D));
        }
        else {
            f18agpu_addpc(f18agpu.D + f18agpu.D);
        }
        F18AGPUADDCYCLE(10-8);
    }
};

// Jump if equal: JEQ dsp
// Conditional relative branch. The displacement is a signed byte representing
// the number of words to branch
void f18agpu_jeq(void)
{
    F18AGPUADDCYCLE(8);

    F18AGPUFormatII;

    if (F18AGPUGET_EQ != 0) {
        if (f18agpu.flagX != 0) {
            f18agpu.PC = f18agpu.flagX;	// Update offset - it's relative to the X, not the opcode
        }
        if ((f18agpu.D & 0x80) != 0) {
            f18agpu.D = 128 - (f18agpu.D & 0x7f);
            f18agpu_addpc(-(f18agpu.D + f18agpu.D));
        } else {
            f18agpu_addpc(f18agpu.D + f18agpu.D);
        }
        F18AGPUADDCYCLE(10-8);
    }
};

// Jump if High or Equal: JHE dsp
void f18agpu_jhe(void)
{
    F18AGPUADDCYCLE(8);

    F18AGPUFormatII;

    if ((F18AGPUGET_LGT != 0) || (F18AGPUGET_EQ != 0)) {
        if (f18agpu.flagX != 0) {
            f18agpu.PC = f18agpu.flagX;	// Update offset - it's relative to the X, not the opcode
        }
        if ((f18agpu.D & 0x80) != 0) {
            f18agpu.D = 128 - (f18agpu.D & 0x7f);
            f18agpu_addpc(-(f18agpu.D + f18agpu.D));
        } else {
            f18agpu_addpc(f18agpu.D + f18agpu.D);
        }
        F18AGPUADDCYCLE(10-8);
    }
};

// Jump if Greater Than: JGT dsp
void f18agpu_jgt(void)
{
    F18AGPUADDCYCLE(8);

    F18AGPUFormatII;

    if (F18AGPUGET_AGT != 0)  {
        if (f18agpu.flagX != 0) {
            f18agpu.PC = f18agpu.flagX;	// Update offset - it's relative to the X, not the opcode
        }
        if ((f18agpu.D & 0x80) != 0) {
            f18agpu.D = 128-(f18agpu.D & 0x7f);
            f18agpu_addpc(-(f18agpu.D + f18agpu.D));
        } else {
            f18agpu_addpc(f18agpu.D + f18agpu.D);
        }
        F18AGPUADDCYCLE(10-8);
    }
};

// Jump if Not Equal: JNE dsp
void f18agpu_jne(void)
{
    F18AGPUADDCYCLE(8);

    F18AGPUFormatII;

    if (F18AGPUGET_EQ == 0)  {
        if (f18agpu.flagX != 0) {
            f18agpu.PC = f18agpu.flagX;	// Update offset - it's relative to the X, not the opcode
        }
        if ((f18agpu.D & 0x80) != 0) {
            f18agpu.D = 128 - (f18agpu.D & 0x7f);
            f18agpu_addpc(-(f18agpu.D + f18agpu.D));
        } else {
            f18agpu_addpc(f18agpu.D + f18agpu.D);
        }
        F18AGPUADDCYCLE(10-8);
    }
};

// Jump if No Carry: JNC dsp
void f18agpu_jnc(void)
{
    F18AGPUADDCYCLE(8);

    F18AGPUFormatII;
    if (F18AGPUGET_C == 0) {
        if (f18agpu.flagX != 0) {
            f18agpu.PC = f18agpu.flagX;	// Update offset - it's relative to the X, not the opcode
        }

        if ((f18agpu.D & 0x80) != 0) {
            f18agpu.D = 128 - (f18agpu.D & 0x7f);
            f18agpu_addpc(-(f18agpu.D + f18agpu.D));
        } else {
            f18agpu_addpc(f18agpu.D + f18agpu.D);
        }
        F18AGPUADDCYCLE(10-8);
    }
};

// Jump On Carry: JOC dsp
void f18agpu_joc(void)
{
    F18AGPUADDCYCLE(8);

    F18AGPUFormatII;
    if (F18AGPUGET_C != 0) {
        if (f18agpu.flagX != 0) {
            f18agpu.PC = f18agpu.flagX;	// Update offset - it's relative to the X, not the opcode
        }

        if ((f18agpu.D & 0x80) != 0) {
            f18agpu.D = 128 - (f18agpu.D & 0x7f);
            f18agpu_addpc(-(f18agpu.D + f18agpu.D));
        } else {
            f18agpu_addpc(f18agpu.D + f18agpu.D);
        }
        F18AGPUADDCYCLE(10-8);
    }
};

// Jump if No Overflow: JNO dsp
void f18agpu_jno(void)
{
    F18AGPUADDCYCLE(8);

    F18AGPUFormatII;
    if (F18AGPUGET_OV == 0) {
        if (f18agpu.flagX != 0) {
            f18agpu.PC = f18agpu.flagX;	// Update offset - it's relative to the X, not the opcode
        }

        if ((f18agpu.D & 0x80) != 0) {
            f18agpu.D = 128 - (f18agpu.D & 0x7f);
            f18agpu_addpc(-(f18agpu.D + f18agpu.D));
        } else {
            f18agpu_addpc(f18agpu.D + f18agpu.D);
        }
        F18AGPUADDCYCLE(10-8);
    }
};

// Jump if Low: JL dsp
void f18agpu_jl(void)
{
    F18AGPUADDCYCLE(8);

    F18AGPUFormatII;
    if ((F18AGPUGET_LGT == 0) && (F18AGPUGET_EQ == 0)) {
        if (f18agpu.flagX != 0) {
            f18agpu.PC = f18agpu.flagX;	// Update offset - it's relative to the X, not the opcode
        }

        if ((f18agpu.D & 0x80) != 0) {
            f18agpu.D = 128 - (f18agpu.D & 0x7f);
            f18agpu_addpc(-(f18agpu.D + f18agpu.D));
        } else {
            f18agpu_addpc(f18agpu.D + f18agpu.D);
        }
        F18AGPUADDCYCLE(10-8);
    }
};

// Jump if High: JH dsp
void f18agpu_jh(void)
{
    F18AGPUADDCYCLE(8);

    F18AGPUFormatII;
    if ((F18AGPUGET_LGT != 0) && (F18AGPUGET_EQ == 0)) {
        if (f18agpu.flagX != 0) {
            f18agpu.PC = f18agpu.flagX;	// Update offset - it's relative to the X, not the opcode
        }

        if ((f18agpu.D & 0x80) != 0) {
            f18agpu.D = 128 - (f18agpu.D & 0x7f);
            f18agpu_addpc(-(f18agpu.D + f18agpu.D));
        } else {
            f18agpu_addpc(f18agpu.D + f18agpu.D);
        }
        F18AGPUADDCYCLE(10-8);
    }
};

// Jump on Odd Parity: JOP dsp
void f18agpu_jop(void)
{
    F18AGPUADDCYCLE(8);
    F18AGPUFormatII;

    if (F18AGPUGET_OP != 0) {
        if (f18agpu.flagX != 0) {
            f18agpu.PC = f18agpu.flagX;	// Update offset - it's relative to the X, not the opcode
        }
        if ((f18agpu.D & 0x80) != 0) {
            f18agpu.D = 128 - (f18agpu.D & 0x7f);
            f18agpu_addpc(-(f18agpu.D + f18agpu.D));
        }
        else {
            f18agpu_addpc(f18agpu.D + f18agpu.D);
        }
        F18AGPUADDCYCLE(10-8);
    }
};

// Set Bit On: SBO src
// Sets a bit in the CRU
void f18agpu_sbo(void)
{
    unsigned short addval;

    F18AGPUADDCYCLE(12);

    F18AGPUFormatII;
    addval=f18agpu_readword((f18agpu.WP+24)>>1) & 0xFFF; // read R12
    if ((f18agpu.D & 0x80) != 0) {
        addval -= 128 - (f18agpu.D & 0x7f);
    }
    else {
        addval+=f18agpu.D;
    }
    // not implemented wcru(add,1);
};

// Set Bit Zero: SBZ src
// Zeros a bit in the CRU
void f18agpu_sbz(void)
{
    unsigned short addval;

    F18AGPUADDCYCLE(12);

    F18AGPUFormatII;
    addval=f18agpu_readword((f18agpu.WP+24)>>1) & 0xFFF; // read R12
    if ((f18agpu.D & 0x80) != 0) {
        addval -= 128 - (f18agpu.D & 0x7f);
    }
    else {
        addval+=f18agpu.D;
    }
    // not implemented wcru(add,0);
};

// Test Bit: TB src
// Tests a CRU bit
void f18agpu_tb(void)
{
    unsigned short addval;

    F18AGPUADDCYCLE(12);

    F18AGPUFormatII;
    addval=f18agpu_readword((f18agpu.WP+24)>>1) & 0xFFF; // read R12
    if ((f18agpu.D & 0x80) != 0) {
        addval -= 128 - (f18agpu.D & 0x7f);
    }
    else {
        addval+=f18agpu.D;
    }
    // not implemented if (rcru(add)) set_EQ; else reset_EQ;
};

// Compare Ones Corresponding: COC src, dst
// Basically comparing against a mask, if all set bits in the src match
// set bits in the dest (mask), the equal bit is set
void f18agpu_coc(void)
{
    unsigned short x1,x2,x3;

    F18AGPUADDCYCLE(14);

    F18AGPUFormatIII;
    x1 = f18agpu_readword(f18agpu.S);               // read source
    f18agpu_postIncrement(F18AGPU_SRC);

    f18agpu_fixd();
    x2 = f18agpu_readword(f18agpu.D);            // read dest
    x3 = x1 & x2;
    if (x3 == x1)
        F18AGPUSET_EQ
    else
        F18AGPURESETEQ
};

// Compare Zeros Corresponding: CZC src, dst
// The opposite of COC. Each set bit in the dst (mask) must
// match up with a zero bit in the src to set the equals flag
void f18agpu_czc(void)
{
    unsigned short x1,x2,x3;

    F18AGPUADDCYCLE(14);

    F18AGPUFormatIII;
    x1 = f18agpu_readword(f18agpu.S);                   // read srouce
    f18agpu_postIncrement(F18AGPU_SRC);

    f18agpu_fixd();
    x2 = f18agpu_readword(f18agpu.D);                   // read dest

    x3 = x1 & x2;
    if (x3 == 0)
        F18AGPUSET_EQ
    else
        F18AGPURESETEQ
};

// eXclusive OR: XOR src, dst
void f18agpu_xor(void)
{
    unsigned short x1,x2,x3;

    F18AGPUADDCYCLE(14);

    F18AGPUFormatIII;
    x1 = f18agpu_readword(f18agpu.S);           // read source
    f18agpu_postIncrement(F18AGPU_SRC);

    f18agpu_fixd();
    x2 = f18agpu_readword(f18agpu.D);           // read dest

    x3 = (x1 ^ x2) & 0xFFFF;
    f18agpu_writeword(f18agpu.D, x3);       // write dest

    F18AGPURESETLGT_AGT_EQ;
    f18agpu.ST |= wStatusLookup[x3] & F18AGPU_MSKLGT_AGT_EQ;
};

// eXtended OPeration: XOP src ???
// The CPU maintains a jump table starting at 0x0040, containing BLWP style
// jumps for each operation. In addition, the new R11 gets a copy of the address of
// the source operand.
// Apparently not all consoles supported both XOP 1 and 2 (depends on the ROM?)
// so it is probably rarely, if ever, used on the TI99.
//
// In the F18A GPU this is the PIX instruction
// Format: MAxxRWCE xxOOxxPP
// M - 1 = calculate the effective address for GM2 instead of the new bitmap layer
//     0 = use the remainder of the bits for the new bitmap layer pixels
// A - 1 = retrieve the pixel's effective address instead of setting a pixel
//     0 = read or set a pixel according to the other bits
// R - 1 = read current pixel into PP, only after possibly writing PP
//     0 = do not read current pixel into PP
// W - 1 = do not write PP 0 = write PP to current pixel
// C - 1 = compare OO with PP according to E, and write PP only if true
//     0 = always write
// E - 1 = only write PP if current pixel is equal to OO
//     0 = only write PP if current pixel is not equal to OO
// OO - pixel to compare to existing pixel
// PP - new pixel to write, and previous pixel when reading
void f18agpu_xop(void)
{
    int addr,pixOffset,bitShift;
    unsigned short x1,x2,newPix;
    BYTE write,pixByte,mask,invMask,pix,comp;

    F18AGPUFORMATIX;

    f18agpu.D = f18agpu.WP + (f18agpu.D << 1);
    x1 = f18agpu_readword(f18agpu.S);
    x2 = f18agpu_readword(f18agpu.D);
    addr = 0;
    if ((x2 & 0x8000) != 0) {
        // calculate BM2 address:
        // 00PYYYYY00000YYY +
        //     0000XXXXX000
        // ------------------
        // 00PY YYYY XXXX XYYY
        //
        // Note: Bitmap GM2 address /includes/ the offset from VR4 (pattern table), so to use
        // it for both pattern and color tables, put the pattern table at >0000
        addr = (((f18a.VDPR[4] & 0x04) != 0) ? 0x2000 : 0) |	// P
                ((x1 & 0x00F8) << 5) |						            // YYYYY
                ((x1 & 0xF800) >> 8) |						            // XXXXX
                (x1 & 0x0007);  							            // YYY
    }
    else {
        // Calculate bitmap layer address
        // f18agpu.log.info("Plot(" + ((x1 & 0xFF00) >> 8) + ", " + (x1 & 0x00FF) + ")");
        pixOffset = ((x1 & 0xFF00) >> 8) + (x1 & 0x00FF) * f18a.bitmapWidth;
        addr = f18a.bitmapBaseAddr + (pixOffset >> 2);
    }

    // Only parse the other bits if M and A are zero
    if ((x2 & 0xc000) == 0) {
        pixByte = f18agpu_readbyte(addr);	    // Get the byte
        bitShift = (pixOffset & 0x0003) << 1;
        mask = 0xC0 >> bitShift;
        pix = (pixByte & mask) >> (6 - bitShift);
        write = (x2 & 0x0400) == 0;		            // Whether to write
        // TODO: are C and E dependent on W being set? I am assuming yes.
        if (write && (x2 & 0x0200) != 0) {		        // C - compare active (only important if we are writing anyway?)
            comp = (pix == ((x2 & 0x0030) >> 4));	    // Compare the pixels
            if ((x2 & 0x0100) != 0) {
                // E is set, comparison must be true
                if (!comp) {
                    write = false;
                }
            }
            else {
                // E is clear, comparison must be false
                if (comp) {
                    write = false;
                }
            }
        }
        if (write) {
            newPix = (x2 & 0x0003) << (6 - bitShift);	// New pixel
            invMask = (~mask) & 0xFF;
            f18agpu_writebyte(addr, (pixByte & invMask) | newPix);
        }
        if ((x2 & 0x0800) != 0) {
            // Read is set, so save the original read pixel color in PP
            x2 = (x2 & 0xFFFC) | pix;
            f18agpu_writeword(f18agpu.D, x2);		    // Write it back
        }
    }
    else {
        // User only wants the address
        f18agpu_writeword(f18agpu.D, addr);
    }

    // Only the source address can be post-inc
    f18agpu_postIncrement(F18AGPU_SRC);

    F18AGPUADDCYCLE(10);
};

// This is the SPI_OUT instruction of the F18A GPU
void f18agpu_ldcr(void)
{
//f18agpu.flash.writeByte(f18agpu_readbyte(f18agpu.S));
    f18agpu_postIncrement(F18AGPU_SRC);
    F18AGPUADDCYCLE(10);
};

// This is the SPI_IN instruction of the F18A GPU
void f18agpu_stcr(void)
{
    //f18agpu_writebyte(f18agpu.S, f18agpu.flash.readByte());
    f18agpu_postIncrement(F18AGPU_SRC);
    F18AGPUADDCYCLE(10);
};

// MultiPlY: MPY src, dst
// Multiply src by dest and store 32-bit result
// Note: src and dest are unsigned.
void f18agpu_mpy(void)
{
    //unsigned short x1,x3;
    unsigned __int32 x3;
    unsigned short x1;

    F18AGPUFORMATIX;

    x1 = f18agpu_readword(f18agpu.S);
    f18agpu_postIncrement(F18AGPU_SRC);

    f18agpu.D = f18agpu.WP + (f18agpu.D << 1);
    x3 = f18agpu_readword(f18agpu.D);          // read dest
    x3 = x3 * x1;
    f18agpu_writeword(f18agpu.D,(x3 >> 16) & 0xFFFF);  // write dest MSW
    f18agpu_writeword(f18agpu.D + 2,(x3 & 0xFFFF));    // write dest LSW

    F18AGPUADDCYCLE(52);
};

// DIVide: DIV src, dst
// Dest, a 2 word number, is divided by src. The result is stored as two words at the dst:
// the first is the whole number result, the second is the remainder
    // ST4 (OV) is to be set:
    // Base cycles: 16
    // 3 memory accesses:
    //  Read instruction (already done)
    //  Read source MSW
    //  Read dest

    // ST4 (OV) is not to be set:
    // Base cycles: 92 - 124
    // 6 memory accesses:
    //  Read instruction (already done)
    //  Read source MSW
    //  Read dest
    //  Read source LSW
    //  Write dest quotient
    //  Write dest remainder

    // Sussing out the cycle count. It is likely a shift and test
    // approach, due to the 16 bit cycle variance. We know the divisor
    // is larger than the most significant word, ie: the total output
    // of the first 16 bits of the 32-bit result MUST be >0000, so
    // the algorithm likely starts with the first bit of the LSW, and
    // shifts through up to 16 cycles, aborting early if the remaining
    // value is smaller than the divisor (or if it's zero, but that
    // would early out in far fewer cases).
    //
    // For example (using 4 bits / 2 bits = 2 bits):
    //  DDDD
    // / VV
    //  ---
    //    1 (subtract from DDDx if set)
    //
    //  0DDD
    // /  VV
    //   ---
    //     2
    //
    // 12 are the bits in the result, and DDD goes to the remainder
    //
    // Proofs:
    // 8/2 -> Overflow (10xx >= 10)
    // 4/2 -> Ok (01xx < 10) -> 010x / 10 = '1' -> 010-10=0 -> 10 > 00 so early out, remaining bits 0 -> 10, remainder 0, 1 clock
    // 1/1 -> Ok (00xx < 01) -> 000x / 01 = '0' -> x001 / 01 = '1' -> 001-01=0 -> 01>00 so finished (either way) -> 01, remainder 1, 2 clocks
    // 5/2 -> Ok (01xx < 10) -> 010 / 10 = '1' -> 010-10=0 -> 10 > 001 so done -> 10, remainder 1, 1 clock
    //
    // TODO: We should be able to prove on real hardware that the early out works like this (greater than, and not 0) with a few choice test cases
void f18agpu_div(void)
{
    unsigned __int32 x3;
    unsigned short x1,x2;
    //unsigned short x1,x2,x3;

    F18AGPUFORMATIX;

    x2 = f18agpu_readword(f18agpu.S);     // read source MSW
    f18agpu_postIncrement(F18AGPU_SRC);

    f18agpu.D = f18agpu.WP + (f18agpu.D << 1);
    x3 = f18agpu_readword(f18agpu.D);       // read dest

    // E/A: When the source operand is greater than the first word of the destination
    // operand, normal division occurs. If the source operand is less than or equal to
    // the first word of the destination operand, normal division results in a quotient
    // that cannot be represented in a 16-bit word. In this case, the computer sets the
    // overflow status bit, leaves the destination operand unchanged, and cancels the
    // division operation.
    if (x2 > x3) {		// x2 can not be zero because they're unsigned
        x3 = (x3 << 16) | f18agpu_readword(f18agpu.D + 2);      // read source LSW
#if 0
        x1 = x3 / x2;
        f18agpu_writeword(f18agpu.D, x1 & 0xFFFF);
        x1 = x3 % x2;
        f18agpu_writeword(f18agpu.D + 2, x1 & 0xFFFF);
#else
        // lets try it the iterative way, should be able to afford it
        // tested with 10,000,000 random combinations, should be accurate :)
        unsigned __int32 mask = (0xFFFF8000);   // 1 extra bit, as noted above
        unsigned __int32 divisor = (x2<<15);    // slide up into place
        int cnt = 16;                           // need to fill 16 bits
        x1 = 0;                                 // initialize quotient, remainder will end up in x3 LSW
        while (x2 <= x3) {
            x1<<=1;
            if ((x3&mask) >= divisor) {
                x1|=1;
                x3-=divisor;
            }
            mask>>=1;
            divisor>>=1;
            --cnt;
            F18AGPUADDCYCLE(1);
        }
        if (cnt < 0) {
            //debug_write("Warning: Division bug. Send to Tursi if you can.");
        }
        while (cnt-- > 0) {
            // handle the early-out case
            x1<<=1;
        }
        f18agpu_writeword(f18agpu.D, x1 & 0xFFFF); // write dest quotient
        f18agpu_writeword(f18agpu.D + 2, x3 & 0xFFFF); // write dest remainder
#endif

        F18AGPURESETOV;
        F18AGPUADDCYCLE(92);		// This is not accurate. (Up to 124 "depends on the partial quotient after each clock cycle during execution")
    }
    else {
        F18AGPUSET_OV;	// division wasn't possible - change nothing
        F18AGPUADDCYCLE(16);
    }
};

// Set Zeros Corresponding: SZC src, dst
// Zero all bits in dest that are zeroed in src
void f18agpu_szc(void)
{
    unsigned short x1,x2,x3;

    F18AGPUFORMATI;

    x1 = f18agpu_readword(f18agpu.S);
    f18agpu_postIncrement(F18AGPU_SRC);

    f18agpu_fixd();
    x2 = f18agpu_readword(f18agpu.D);
    x3 = (~x1) & x2;
    f18agpu_writeword(f18agpu.D, x3);
    f18agpu_postIncrement(F18AGPU_DST);

    F18AGPURESETLGT_AGT_EQ;
    f18agpu.ST |= wStatusLookup[x3] & F18AGPU_MSKLGT_AGT_EQ;

    F18AGPUADDCYCLE(14);
};

// Set Zeros Corresponding, Byte: SZCB src, dst
void f18agpu_szcb(void)
{
    BYTE x1,x2,x3;

    F18AGPUFORMATI;

    x1 = f18agpu_readbyte(f18agpu.S);
    f18agpu_postIncrement(F18AGPU_SRC);

    f18agpu_fixd();
    x2 = f18agpu_readbyte(f18agpu.D);
    x3 = (~x1) & x2;
    f18agpu_writebyte(f18agpu.D, x3);
    f18agpu_postIncrement(F18AGPU_DST);

    F18AGPURESETLGT_AGT_EQ_OP;
    f18agpu.ST |= bStatusLookup[x3] & F18AGPU_MSKLGT_AGT_EQ_OP;

    F18AGPUADDCYCLE(14);
};

// Subtract: S src, dst
void f18agpu_s(void)
{
    unsigned short x1,x2,x3;

    F18AGPUFORMATI;

    x1 = f18agpu_readword(f18agpu.S);
    f18agpu_postIncrement(F18AGPU_SRC);

    f18agpu_fixd();
    x2 = f18agpu_readword(f18agpu.D);
    x3 = (x2 - x1) & 0xFFFF;
    f18agpu_writeword(f18agpu.D, x3);
    f18agpu_postIncrement(F18AGPU_DST);

    F18AGPURESETEQ_LGT_AGT_C_OV;
    f18agpu.ST |= wStatusLookup[x3] & F18AGPU_MSKLGT_AGT_EQ;

    // any number minus 0 sets carry.. Tursi's theory is that converting 0 to the two's complement
    // is causing the carry flag to be set.
    if ((x3 < x2) || (x1 == 0)) F18AGPUSET_C;
    if (((x1 & 0x8000) != (x2 & 0x8000)) && ((x3 & 0x8000) != (x2 & 0x8000))) F18AGPUSET_OV;

    F18AGPUADDCYCLE(14);
};

// Subtract Byte: SB src, dst
void f18agpu_sb(void)
{
    BYTE x1,x2,x3;

    F18AGPUFORMATI;

    x1 = f18agpu_readbyte(f18agpu.S);
    f18agpu_postIncrement(F18AGPU_SRC);

    f18agpu_fixd();
    x2 = f18agpu_readbyte(f18agpu.D);
    x3 = (x2 - x1) & 0xFF;
    f18agpu_writebyte(f18agpu.D, x3);
    f18agpu_postIncrement(F18AGPU_DST);

    F18AGPURESETEQ_LGT_AGT_C_OV_OP;
    f18agpu.ST |= bStatusLookup[x3] & F18AGPU_MSKLGT_AGT_EQ_OP;

    // any number minus 0 sets carry.. Tursi's theory is that converting 0 to the two's complement
    // is causing the carry flag to be set.
    if ((x3 < x2) || (x1 == 0)) F18AGPUSET_C;
    if (((x1 & 0x80) != (x2 & 0x80)) && ((x3 & 0x80) != (x2 & 0x80))) F18AGPUSET_OV;

    F18AGPUADDCYCLE(14);
};

// Compare words: C src, dst
void f18agpu_c(void)
{
    unsigned short x3,x4;

    F18AGPUFORMATI;

    x3 = f18agpu_readword(f18agpu.S);
    f18agpu_postIncrement(F18AGPU_SRC);

    f18agpu_fixd();
    x4 = f18agpu_readword(f18agpu.D);
    f18agpu_postIncrement(F18AGPU_DST);

    F18AGPURESETLGT_AGT_EQ;
    if (x3 > x4) F18AGPUSET_LGT;
    if (x3 == x4) F18AGPUSET_EQ;
    if ((x3 & 0x8000) == (x4 & 0x8000)) {
        if (x3 > x4) F18AGPUSET_AGT;
    }
    else {
        if ((x4 & 0x8000) != 0) F18AGPUSET_AGT;
    }

    F18AGPUADDCYCLE(14);
};

// CompareBytes: CB src, dst
void f18agpu_cb(void)
{
    BYTE x3,x4; // unsigned short x3,x4;

    F18AGPUFORMATI;

    x3 = f18agpu_readbyte(f18agpu.S);
    f18agpu_postIncrement(F18AGPU_SRC);

    f18agpu_fixd();
    x4 = f18agpu_readbyte(f18agpu.D);
    f18agpu_postIncrement(F18AGPU_DST);

    F18AGPURESETLGT_AGT_EQ_OP;
    if (x3 > x4) F18AGPUSET_LGT;
    if (x3 == x4) F18AGPUSET_EQ;
    if ((x3 & 0x80) == (x4 & 0x80)) {
        if (x3 > x4) F18AGPUSET_AGT;
    }
    else {
        if ((x4 & 0x80) != 0) F18AGPUSET_AGT;
    }
    f18agpu.ST |= bStatusLookup[x3] & F18AGPU_BIT_OP;

    F18AGPUADDCYCLE(14);
};

// Add words: A src, dst
void f18agpu_a(void)
{
    unsigned short x1,x2,x3;

    F18AGPUFORMATI;

    x1 = f18agpu_readword(f18agpu.S);
    f18agpu_postIncrement(F18AGPU_SRC);

    f18agpu_fixd();
    x2 = f18agpu_readword(f18agpu.D);
    x3 = (x2 + x1) & 0xFFFF;
    f18agpu_writeword(f18agpu.D, x3);
    f18agpu_postIncrement(F18AGPU_DST);

    F18AGPURESETEQ_LGT_AGT_C_OV;	// We come out with either EQ or LGT, never both
    f18agpu.ST |= wStatusLookup[x3] & F18AGPU_MSKLGT_AGT_EQ;

    if (x3 < x2) F18AGPUSET_C;	// if it wrapped around, set carry
    if (((x1 & 0x8000) == (x2 & 0x8000)) && ((x3 & 0x8000) != (x2 & 0x8000))) F18AGPUSET_OV; // if it overflowed or underflowed (signed math), set overflow

    F18AGPUADDCYCLE(14);
};

// Add bytes: A src, dst
void f18agpu_ab(void)
{
    BYTE x1,x2,x3;

    F18AGPUFORMATI;

    x1 = f18agpu_readbyte(f18agpu.S);
    f18agpu_postIncrement(F18AGPU_SRC);

    f18agpu_fixd();
    x2 = f18agpu_readbyte(f18agpu.D);
    x3 = (x2 + x1) & 0xFF;
    f18agpu_writebyte(f18agpu.D, x3);
    f18agpu_postIncrement(F18AGPU_DST);

    F18AGPURESETEQ_LGT_AGT_C_OV;	// We come out with either EQ or LGT, never both
    f18agpu.ST |= bStatusLookup[x3] & F18AGPU_MSKLGT_AGT_EQ_OP;

    if (x3 < x2) F18AGPUSET_C;	// if it wrapped around, set carry
    if (((x1 & 0x80) == (x2 & 0x80)) && ((x3 & 0x80) != (x2 & 0x80))) F18AGPUSET_OV;  // if it overflowed or underflowed (signed math), set overflow

    F18AGPUADDCYCLE(14);
};

// MOVe words: MOV src, dst
void f18agpu_mov(void)
{
    unsigned short x1;

    F18AGPUFORMATI;

    x1 = f18agpu_readword(f18agpu.S);
    f18agpu_postIncrement(F18AGPU_SRC);
    f18agpu_fixd();

    f18agpu_writeword(f18agpu.D, x1);
    f18agpu_postIncrement(F18AGPU_DST);

    F18AGPURESETLGT_AGT_EQ;
    f18agpu.ST |= wStatusLookup[x1] & F18AGPU_MSKLGT_AGT_EQ;

    F18AGPUADDCYCLE(14);
};

// MOVe Bytes: MOVB src, dst
void f18agpu_movb(void)
{
    BYTE x1;

    F18AGPUFORMATI;

    x1 = f18agpu_readbyte(f18agpu.S);
    f18agpu_postIncrement(F18AGPU_SRC);
    f18agpu_fixd();

    f18agpu_writebyte(f18agpu.D, x1);
    f18agpu_postIncrement(F18AGPU_DST);

    F18AGPURESETLGT_AGT_EQ_OP;
    f18agpu.ST |= bStatusLookup[x1] & F18AGPU_MSKLGT_AGT_EQ_OP;

    F18AGPUADDCYCLE(14);
};

// Set Ones Corresponding: SOC src, dst
// Essentially performs an OR - setting all the bits in dst that
// are set in src
void f18agpu_soc(void)
{
    unsigned short x1,x2,x3;

    F18AGPUFORMATI;

    x1 = f18agpu_readword(f18agpu.S);
    f18agpu_postIncrement(F18AGPU_SRC);

    f18agpu_fixd();
    x2 = f18agpu_readword(f18agpu.D);
    x3 = x1 | x2;
    f18agpu_writeword(f18agpu.D, x3);
    f18agpu_postIncrement(F18AGPU_DST);

    F18AGPURESETLGT_AGT_EQ;
    f18agpu.ST |= wStatusLookup[x3] & F18AGPU_MSKLGT_AGT_EQ;

    F18AGPUADDCYCLE(14);
};

void f18agpu_socb(void)
{
    BYTE x1,x2,x3;

    F18AGPUFORMATI;

    x1 = f18agpu_readbyte(f18agpu.S);
    f18agpu_postIncrement(F18AGPU_SRC);

    f18agpu_fixd();
    x2 = f18agpu_readbyte(f18agpu.D);
    x3 = x1 | x2;
    f18agpu_writebyte(f18agpu.D, x3);
    f18agpu_postIncrement(F18AGPU_DST);

    F18AGPURESETLGT_AGT_EQ_OP;
    f18agpu.ST |= bStatusLookup[x3] & F18AGPU_MSKLGT_AGT_EQ_OP;

    F18AGPUADDCYCLE(14);
};

// F18A specific opcodes
void f18agpu_callf18(void)
{
    unsigned short x2;

    F18AGPUFormatVI;

    x2 = f18agpu_readword(f18agpu.WP + 30);	// get R15
    f18agpu_writeword(x2, f18agpu.PC);
    f18agpu.PC=f18agpu.S;
    x2 -=2;
    f18agpu_writeword(f18agpu.WP + 30,x2); // update R15
    f18agpu_postIncrement(F18AGPU_SRC);

    F18AGPUADDCYCLE(8);
};

void f18agpu_retf18(void)
{
    unsigned short x1;

    F18AGPUFormatVII;

    x1 = f18agpu_readword(f18agpu.WP + 30);	// get R15
    x1+=2;
    f18agpu.PC=f18agpu_readword(x1);          // get PC
    f18agpu_writeword(f18agpu.WP + 30,x1); // update R15

    F18AGPUADDCYCLE(8);
};

void f18agpu_pushf18(void)
{
    unsigned short x1,x2;

    F18AGPUFormatVI;

    x1 = f18agpu_readword(f18agpu.S);
    x2 = f18agpu_readword(f18agpu.WP + 30); // get R15
    f18agpu_writeword(x2, x1);               // Push the word on the stack
    x2 -= 2;                                    // the stack pointer post-decrements (per Matthew)
    f18agpu_writeword(f18agpu.WP + 30, x2);		// update R15
    f18agpu_postIncrement(F18AGPU_SRC);

    F18AGPUADDCYCLE(8);
};

void f18agpu_slcf18(void)
{
    BYTE cycles;
    // unsigned short x1,x2,x4;
    unsigned short x1,x4;
    int x2;

    F18AGPUFormatVI;

    cycles = 0;
    if (f18agpu.D == 0) {
        f18agpu.D = f18agpu_readword(f18agpu.WP) & 0xf;
        if (f18agpu.D == 0) f18agpu.D=16;
        cycles += 8;
    }
    x1 = f18agpu_readword(f18agpu.S);
    for (x2 = 0; x2 < f18agpu.D; x2++) {
        x4 = x1 & 0x8000;
        x1 = x1 << 1;
        if (x4 != 0) {
            x1 = x1 | 1;
        }
    }
    f18agpu_writeword(f18agpu.S, x1);

    F18AGPURESETEQ_LGT_AGT_C;
    f18agpu.ST |= wStatusLookup[x1] & F18AGPU_MSKLGT_AGT_EQ;

    if (x4 != 0) F18AGPUSET_C;

    F18AGPUADDCYCLE(cycles + 12 + 2 * f18agpu.D);
};

void f18agpu_popf18(void)
{
    unsigned short x1,x2;

    F18AGPUFormatVI;               // S is really D in this one...

    x2 = f18agpu_readword(f18agpu.WP + 30);	// get R15

    // POP the word from the stack
    // the stack pointer post-decrements (per Matthew)
    x2 += 2;                                    // so here we pre-increment!
    x1 = f18agpu_readword(x2);
    f18agpu_writeword(f18agpu.S, x1);
    f18agpu_writeword(f18agpu.WP + 30, x2);		// update R15
    f18agpu_postIncrement(F18AGPU_SRC);

    F18AGPUADDCYCLE(8);
};

void f18agpu_bad(void)
{
    // Base cycles: 6
    // 1 memory accesses:
    //  Read instruction (already done)

    F18AGPUADDCYCLE(6);
};

