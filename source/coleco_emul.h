#ifndef COLECO_BUS_H
#define COLECO_BUS_H

#include <stdint.h>

// Basis types
typedef uint8_t  BYTE;
typedef uint16_t WORD;
typedef uint32_t DWORD;

#define MAX_DISKS     4       /* Maximal number of disks     */
#define MAX_TAPES     4       /* Maximal number of tapes     */

// Forward declare globale emulatorstruct
// Jij hebt emulator als pointer naar TAdamP in coleco.cpp, dus:
struct TAdamP;
extern TAdamP *emulator;

// Cartridge load result codes (pas aan als jouw defines anders zijn)
#define ROM_LOAD_PASS      1
#define ROM_LOAD_FAIL      0
#define ROM_VERIFY_FAIL    2

// Cartridge type enums (als je die al ergens anders hebt, laat staan)
#define ROMCARTRIDGESTD    0
#define ROMCARTRIDGEMEGA   1

// Machines
#define MACHINECOLECO 0
#define MACHINEADAM   1

// backup types (EEPROM/SRAM)
#define NOBACKUP     0
#define EEPSRAM      1
#define EEP24C08     2
#define EEP24C256    3

// Extern geheugenbuffers - deze bestaan al in je project
// (in je huidige coleco.cpp gebruik je RAM_Memory, ROM_Memory, BIOS_Memory)
extern BYTE *RAM_Memory;
extern BYTE *ROM_Memory;
extern BYTE *BIOS_Memory;

// Memory map (8 pages van 8 KB = 64 KB totaal)
extern BYTE *MemoryMap[8];

// ===== Global hardware state =====

// SGM / Adam / ports
extern BYTE coleco_port20;
extern BYTE coleco_port53;
extern BYTE coleco_port60;

// Super Game Module state
extern int  sgm_enable;
extern int  sgm_firstwrite;
extern int  sgm_low_addr;
extern int  sgm_neverenable;

// Adam memory state
extern int adam_ram_lo;
extern int adam_ram_hi;
extern int adam_ram_lo_exp;
extern int adam_ram_hi_exp;
extern int adam_128k_mode;

// Megacart state
extern BYTE coleco_megacart;
extern BYTE coleco_megabank;
extern int  coleco_megasize;

// Controller state
extern unsigned int coleco_joystat;  // jouw project heeft dit al
extern BYTE         coleco_joymode;  // 0 = joystick rows, 1 = keypad columns

// Debug last write tracking (jij had deze al)
extern unsigned int lastMemoryWriteAddrLo;
extern unsigned int lastMemoryWriteAddrHi;
extern unsigned int lastMemoryWriteValueLo;
extern unsigned int lastMemoryWriteValueHi;

// ============= Public API we expose to rest of emulator =============

// laad een ROM in memory buffers en stel mapper info in
BYTE coleco_loadcart(char *filename);

// CPU memory access
BYTE coleco_ReadByte(unsigned int Address);
void coleco_WriteByte(unsigned int Address, int Data);

// Z80 IN / OUT
BYTE ReadInputPort(int Address);
void coleco_writeport(int Address, int Data, int *Cycles); // cycles ptr is zoals in jouw code

// systeem reset (memory map initialiseren, VDP/PSG resetten, Z80 resetten...)
void coleco_reset(void);

// helper voor bank switching van megacart
void megacart_bankswitch(BYTE bank);

// helper om SGM RAM mapping te updaten
void coleco_setupsgm(void);

// helper om ADAM geheugen mapping te updaten
void coleco_setadammemory(bool resetadam);

// ===== External hardware helpers we assume you already have =====

// TMS/F18A video chip access (je hebt dit al in coleco.cpp)
extern void tms9918_reset(void);
extern void f18a_reset(void);
extern BYTE tms9918_readctrl(void);
extern BYTE tms9918_readdata(void);
extern BYTE tms9918_writectrl(int Data);   // NOTE: originele code kan irq_status teruggeven
extern void tms9918_writedata(int Data);
extern BYTE f18a_readctrl(void);
extern BYTE f18a_readdata(void);
extern BYTE f18a_writectrl(int Data);      // idem voor F18A
extern void f18a_writedata(int Data);

// PSG
extern void sn76489_write(BYTE Data);

// AY8910 (SGM sound) - we mogen ze optioneel stubben
extern void ay8910_write(int port, int data);
extern unsigned int ay8910_read(void);

// I2C / EEPROM backup helpers
extern void c24xx_write(int pins);
extern struct { int Pins; } c24; // jouw code heeft zoiets

// Z80 CPU helpers
extern void z80_reset(void);
extern void z80_set_irq_line(int lineState); // ASSERT_LINE / CLEAR_LINE, pas aan naar jouw signatuur

// Printer / ADAMNet I/O (bij Adam)
extern void Printer(BYTE Data);
extern BYTE ReadPCB(unsigned int Address);
extern void WritePCB(unsigned int Address, BYTE Data);
//extern BYTE *PCBTable; // pointer/array in jouw code

// VRAM timing fix vars (je hebt tms struct / emulator->F18A etc.)
struct TmsState {
    int ScanLines;
};
extern struct TmsState tms;

// emulator fields we depend on
// (pas types aan als ze anders zijn in jouw struct)
struct TAdamP {
    int currentMachineType;   // MACHINECOLECO/MACHINEADAM
    int SGM;                  // bool-ish
    int F18A;                 // bool-ish
    int NTSC;                 // bool-ish
    int biosnodelay;          // bool-ish
    int hackbiospal;          // bool-ish
    DWORD cardsize;
    DWORD cardcrc;
    int typebackup;           // NOBACKUP / EEPSRAM / ...
    int romCartridgeType;     // ROMCARTRIDGESTD / ROMCARTRIDGEMEGA
};

#endif // COLECO_BUS_H
