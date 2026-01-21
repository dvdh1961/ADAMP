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
 *
 * Based on   PCB emulation (C) Marat Fayzullin 1994-2021
 *
*/
#ifndef ADAMNET_H
#define ADAMNET_H

#include <stdint.h> // Nodig voor uint8_t
#include <cstring>
#include <atomic>
#include "fdidisk.h"

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

/** RAM Access Macro *****************************************/
#define RAM(A)         (RAM_Memory[A])

/** PCB Field Offsets ****************************************/
#define PCB_CMD_STAT   0
#define PCB_BA_LO      1
#define PCB_BA_HI      2
#define PCB_MAX_DCB    3
#define PCB_SIZE       4

/** DCB Field Offsets ****************************************/
#define DCB_CMD_STAT   0
#define DCB_BA_LO      1
#define DCB_BA_HI      2
#define DCB_BUF_LEN_LO 3
#define DCB_BUF_LEN_HI 4
#define DCB_SEC_NUM_0  5
#define DCB_SEC_NUM_1  6
#define DCB_SEC_NUM_2  7
#define DCB_SEC_NUM_3  8
#define DCB_DEV_NUM    9
#define DCB_RETRY_LO   14
#define DCB_RETRY_HI   15
#define DCB_ADD_CODE   16
#define DCB_MAXL_LO    17
#define DCB_MAXL_HI    18
#define DCB_DEV_TYPE   19
#define DCB_NODE_TYPE  20
#define DCB_SIZE       21
#define DCB_SEC_LO     4
#define DCB_SEC_HI     5

/** PCB Commands *********************************************/
#define CMD_PCB_IDLE   0x00
#define CMD_PCB_SYNC1  0x01
#define CMD_PCB_SYNC2  0x02
#define CMD_PCB_SNA    0x03
#define CMD_PCB_RESET  0x04
#define CMD_PCB_WAIT   0x05

/** DCB Commands *********************************************/
#define CMD_RESET      0x00
#define CMD_STATUS     0x01
#define CMD_ACK        0x02
#define CMD_CLEAR      0x03
#define CMD_RECEIVE    0x04
#define CMD_CANCEL     0x05
#define CMD_SEND       0x06
#define CMD_NACK       0x07

#define CMD_SOFT_RESET 0x02
#define CMD_WRITE      0x03
#define CMD_READ       0x04
#define CMD_FORMAT     0x05

/** Response Codes *******************************************/
#define RSP_STATUS     0x80
#define RSP_ACK        0x90
#define RSP_CANCEL     0xA0
#define RSP_SEND       0xB0
#define RSP_NACK       0xC0

#define AN_STAT_DIF    0x01
#define AN_STAT_DOE    0x02

#define DCB_DAT_LEN_LO 0x01  // Offset voor Data Length Low Byte
#define DCB_DAT_LEN_HI 0x02  // Offset voor Data Length High Byte

#ifndef BYTE_TYPE_DEFINED
#define BYTE_TYPE_DEFINED
typedef unsigned char byte;
#endif

#ifndef WORD_TYPE_DEFINED
#define WORD_TYPE_DEFINED
typedef unsigned short word;
#endif

extern std::atomic<bool> g_diskSoundActive;
extern std::atomic<bool> g_tapeSoundActive;

/** Gedeelde variabelen (C++ Linkage) ************************/
extern byte PCBTable[];
extern byte HoldingBuf[4096];
extern word io_busy;
extern word PCBAddr;
extern bool m_cpm_enabled;
extern bool m_tdos_enabled;
extern bool m_cpm_status;
extern byte last_command_read;
extern byte io_show_status;
extern byte KBDStatus, LastKey, DiskID;
extern word savedBUF, savedLEN;

#define DELAY_IO 10

// Gebruik extern voor de tabellen om 'conflicting declaration' te voorkomen
extern const byte InterleaveTable[8];

/** Prototypes (C Linkage) ***********************************/
#ifdef __cplusplus
extern "C" {
#endif

void adamnet_force_writer(uint8_t sc);
void adamnet_queue_key(uint8_t key_code);

byte ChangeDisk(byte N, const char *FileName);
byte ChangeTape(byte N, const char *FileName);

void SetDCB(byte Dev,byte Offset,byte Value);
void SetPCB(word Offset,byte Value);
byte GetPCB(word Offset);
word GetPCBBase(void);
word GetMaxDCB(void);
byte GetDCB(byte Dev,byte Offset);
word GetDCBBase(byte Dev);
word GetDCBLen(byte Dev);
unsigned int GetDCBSector(byte Dev);
uint8_t adamnet_dequeue_key(void);
int adamnet_is_key_available(void);
int IsPCB(word A);
void MovePCB(word NewAddr,byte MaxDCB);
void ReportDevice(byte Dev,word MsgSize,byte IsBlock);
byte GetKBD();
void UpdateKBD(byte Dev,int V);
void UpdatePRN(byte Dev,int V);
void AdamFlushCache(void);
void ReadPCB(word A);
void WritePCB(word A,byte V);
void ResetPCB(void);
void PutKBD(unsigned int Key);

// Prototypes voor de specifieke implementaties
void UpdateDSK_EOS(byte N, byte Dev, int V);
void UpdateTAP_EOS(byte N, byte Dev, int V);
void UpdateDCB_EOS(byte Dev, int V);

void UpdateDSK_CPM(byte N, byte Dev, int V);
void UpdateTAP_CPM(byte N, byte Dev, int V);
void UpdateDCB_CPM(byte Dev, int V);

unsigned char adamnet_read_io(int Address);

#ifdef __cplusplus
}
#endif

#endif // ADAMNET_H
