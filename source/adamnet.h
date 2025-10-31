/* EmulTwo  - A Windows Colecovision emulator.
 * Copyright (C) 2018-2023 Alekmaul
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
 * adampcb.cpp
 *
 * Based on   PCB emulation (C) Marat Fayzullin 1994-2021
 *
*/
#ifndef ADAMNET_H
#define ADAMNET_H

#ifdef __cplusplus
extern "C" {
#endif

/** Adam Key Codes *******************************************/
#define KEY_CONTROL    CON_CONTROL
#define KEY_SHIFT      CON_SHIFT
#define KEY_CAPS       CON_CAPS
#define KEY_ESC        27
#define KEY_BS         8   // + SHIFT = 184
#define KEY_TAB        9   // + SHIFT = 185
#define KEY_ENTER      13 
#define KEY_QUOTE      '\''
#define KEY_BQUOTE     '`'
#define KEY_BSLASH     '\\'
#define KEY_COMMA      ','
#define KEY_DOT        '.'
#define KEY_SLASH      '/'
#define KEY_ASTERISK   '*'
#define KEY_HOME       128
#define KEY_F1         129 // + SHIFT = 137 
#define KEY_F2         130 // + SHIFT = 138
#define KEY_F3         131 // + SHIFT = 139
#define KEY_F4         132 // + SHIFT = 140
#define KEY_F5         133 // + SHIFT = 141
#define KEY_F6         134 // + SHIFT = 142
#define KEY_WILDCARD   144 // + SHIFT = 152
#define KEY_UNDO       145 // + SHIFT = 153
#define KEY_MOVE       146 // + SHIFT = 154 (COPY)
#define KEY_STORE      147 // + SHIFT = 155 (FETCH)
#define KEY_INS        148 // + SHIFT = 156
#define KEY_PRINT      149 // + SHIFT = 157
#define KEY_CLEAR      150 // + SHIFT = 158
#define KEY_DEL        151 // + SHIFT = 159, + CTRL = 127
#define KEY_UP         160 // + CTRL = 164, + HOME = 172
#define KEY_RIGHT      161 // + CTRL = 165, + HOME = 173
#define KEY_DOWN       162 // + CTRL = 166, + HOME = 174
#define KEY_LEFT       163 // + CTRL = 167, + HOME = 175
#define KEY_DIAG_NE    168
#define KEY_DIAG_SE    169
#define KEY_DIAG_SW    170
#define KEY_DIAG_NW    171

    
/** Special Key Codes ****************************************/
/** Modifiers returned by GetKey() and WaitKey().           **/
/*************************************************************/
#define CON_KEYCODE  0x03FFFFFF /* Key code                  */
#define CON_MODES    0xFC000000 /* Mode bits, as follows:    */
#define CON_CLICK    0x04000000 /* Key click (LiteS60 only)  */
#define CON_CAPS     0x08000000 /* CapsLock held             */
#define CON_SHIFT    0x10000000 /* SHIFT held                */
#define CON_CONTROL  0x20000000 /* CONTROL held              */
#define CON_ALT      0x40000000 /* ALT held                  */
#define CON_RELEASE  0x80000000 /* Key released (going up)   */

#define CON_F1       0xEE
#define CON_F2       0xEF
#define CON_F3       0xF0
#define CON_F4       0xF1
#define CON_F5       0xF2
#define CON_F6       0xF3
#define CON_F7       0xF4
#define CON_F8       0xF5
#define CON_F9       0xF6
#define CON_F10      0xF7
#define CON_F11      0xF8
#define CON_F12      0xF9
#define CON_LEFT     0xFA
#define CON_RIGHT    0xFB
#define CON_UP       0xFC
#define CON_DOWN     0xFD
#define CON_OK       0xFE
#define CON_EXIT     0xFF


#ifndef BYTE_TYPE_DEFINED
#define BYTE_TYPE_DEFINED
typedef unsigned char byte;
#endif

#ifndef WORD_TYPE_DEFINED
#define WORD_TYPE_DEFINED
typedef unsigned short word;
#endif

extern byte PCBTable[];
extern byte HoldingBuf[];
extern byte  io_busy, KBDStatus, LastKey, DiskID;
extern word savedBUF, savedLEN, PCBAddr;


/** ReadPCB() ************************************************/
/** Read value from a given PCB or DCB address.             **/
/*************************************************************/
void ReadPCB(word A);

/** WritePCB() ***********************************************/
/** Write value to a given PCB or DCB address.              **/
/*************************************************************/
void WritePCB(word A,byte V);

/** ResetPCB() ***********************************************/
/** Reset PCB and attached hardware.                        **/
/*************************************************************/
void ResetPCB(void);

/** PutKBD() *************************************************/
/** Add a new key to the keyboard buffer.                   **/
/*************************************************************/
void PutKBD(unsigned int Key);


void AdamCheckFlushCache(void);

#ifdef __cplusplus
}
#endif
#endif /* ADAMNET_H */



