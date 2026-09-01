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
 * Based on emulation by Marat Fayzullin in 2017-2019
 * Based on   PCB emulation (C) AlekMaul 2014-2023
 * coleco.h
 * Adam+ changes DVDH1961
 */

#ifndef coleco_h
#define coleco_h

#include "emu.h"
#include "6801/fdidisk.h"

#define MAX_CART_SIZE   512            // 512K of cart memory
#define MAX_RAM_SIZE    128
#define MAX_EEPROM_SIZE 32          // 32K of EEProm memory
#define MAX_BIOS_SIZE   64
#define MAXSTATESIZE    (MAX_CART_SIZE+MAX_RAM_SIZE+MAX_EEPROM_SIZE)

#define MAX_DISKS     4                         // Maximal number of disks
#define MAX_TAPES     4                        // Maximal number of tapes

#define MAX_BREAKPOINTS 100
extern int breakpoints[MAX_BREAKPOINTS];
extern int breakpoint_count;

// 80C specific resolutions
#define TVW_ 336
#define TVH_ 262
// Sound

enum
{
    CHRMAP=0,CHRGEN,SPRATTR,SPRGEN,CHRCOL,VRAM, SGMRAM, RAM, ROM, EEPROM, SRAM,CHRMAP2,CHRCOL2,
};

//---------------------------------------------------------------------------
extern const unsigned char TMS9918A_palette[6*16*3];

extern BYTE cv_palette[16*4*3];                     // Coleco display palette
extern BYTE cv_display[TVW_*TVH_];          // Coleco display buffer (max screen size)
extern int cv_pal32[16*4];                                  // Coleco display palette in 32 bits RGB

extern BYTE ROM_Memory[MAX_CART_SIZE * 1024];          // ROM Carts up to 512K
extern BYTE RAM_Memory[MAX_RAM_SIZE * 1024];            // RAM up to 128K (for the ADAM... )
extern BYTE BIOS_Memory[MAX_BIOS_SIZE * 1024];           // 64K To hold our BIOS and related OS memory
extern BYTE SRAM_Memory[MAX_EEPROM_SIZE*1024];  // SRAM up to 32K for the few carts which use it
extern BYTE VDP_Memory[0x10000];                                          // VDP video memory (64K for 80C support)

extern int tstates,frametstates;

extern BYTE coleco_port20;                             // Adam port 0x20-0x3F (AdamNet)
extern BYTE coleco_port60;                             // Adam port 0x60-0x7F (memory)
extern BYTE coleco_port53;                             // SGM port 0x53 (memory)

extern BYTE coleco_megabank;                      // selected bank for megacart
extern BYTE coleco_megasize;                        // mega cart rom size in 16kB pages
extern BYTE coleco_megacart;                        // <>0 if mega cart rom detected

extern unsigned int coleco_joystat;                // Joystick / Paddle management

extern int coleco_spinpos[2];                            // Spinner position
extern unsigned int coleco_spinrecur[2];      // Spinner generates INT when >=0x10000
extern unsigned int coleco_spinparam[2];    // Spinner value to add to spinrecur
extern unsigned int coleco_spinstate[2];      // Spinner status

extern FDIDisk Disks[MAX_DISKS];              // Adam disk drives
extern FDIDisk Tapes[MAX_TAPES];             // Adam tape drives

extern BYTE sgm_enable;
extern unsigned int sgm_low_addr;
extern BYTE sgm_neverenable;
extern BYTE sgm_firstwrite;

// --- Expansion RAM Variabelen (NIEUW) ---
extern BYTE RAMPages;                                   // Totaal aantal 64K expansiepagina's
extern BYTE RAMPage;                                     // Huidig geselecteerde Expansion RAM pagina
extern BYTE RAMMask;                                   // Masker voor RAMPages

// === 80-COLUMN MODE SUPPORT ===
extern BYTE coleco_80col_enabled;             // 0=disabled, 1=enabled
extern bool g_adamCartridgeMode;

#ifdef __cplusplus
extern "C" {
#endif

BYTE coleco_loadcart(char *filename);
BYTE coleco_savestate(char *filename);
BYTE coleco_loadstate(char *filename);

#ifdef __cplusplus
}
#endif
//---------------------------------------------------------------------------
extern unsigned short coleco_gettmsaddr(BYTE whichaddr, BYTE mode, BYTE y);
extern BYTE coleco_gettmsval(BYTE whichaddr, unsigned short addr, BYTE mode, BYTE y);

extern void coleco_initialise(void);
extern void coleco_setbyte(int Address, int Data);
extern void coleco_writebyte(unsigned int Address, int Data);
extern BYTE coleco_ReadByte(int Address);
extern void coleco_WriteByte(unsigned Address, int Data);
extern BYTE coleco_readoperandbyte(int Address);
extern BYTE coleco_readbyte(int Address);
extern BYTE coleco_getbyte(int Address);
extern BYTE coleco_opcode_fetch(int Address);
extern void coleco_writeport(int Address, int Data, int *tstates);
extern BYTE ReadInputPort(int Address, int *tstates);
extern BYTE coleco_readport(int Address, int *tstates);
extern int coleco_contend(int Address, int states, int time);
extern int coleco_do_scanline(void);
extern void coleco_paddle(void);
extern void adamnet_write_mapper(BYTE Data);

extern void coleco_setval(BYTE whichaddr, unsigned short addr, BYTE y);

extern void coleco_setpalette(int palette);

extern void coleco_reset(void);
extern void coleco_reset_and_restart_bios(void);
extern void coleco_hardreset(void);
extern void coleco_setupsgm(void);
extern void Printer(BYTE V);

extern void coleco_set_machine_type(int isAdam);
extern void coleco_setSpinner(int player, int analogValue);

extern BYTE *MemoryMap[8];

extern void coleco_hide_current_vdp_sprites();

#ifdef __cplusplus
extern "C" {
void RenderCalcPalette(BYTE *cv_palette_out, unsigned long nbcolors);
byte coleco_load_disk(int drive, const char *filename);
byte coleco_load_tape(int drive, const char *filename);
int  coleco_save_disk(int drive, const char *filename);
int  coleco_save_tape(int drive, const char *filename);
void coleco_eject_disk(int drive);
void coleco_eject_tape(int drive);
int coleco_get_bios_status(int index);
void coleco_clear_debug_flags(void);
void coleco_probe_bios_status_all(void);
void coleco_set_ram_page(int page);
void coleco_setadammemory(bool resetAdamNet);
int coleco_virtual_cpm_diskboot(const char* cpmTapeDdpPath,
                                const char* diskDskPath,
                                int tapeDrive,
                                int diskDrive);
}
#else
extern void RenderCalcPalette(BYTE *cv_palette_out, int nbcolors);
#endif


#ifndef COLECO_VDP_TMS
#define COLECO_VDP_TMS   0
#endif
#ifndef COLECO_VDP_F18A
#define COLECO_VDP_F18A  1
#endif
/* A PICO9918 answers every F18A feature test but is its own board, so it gets its
   own type. Test features with coleco_vdp_has_f18a(); comparing against
   COLECO_VDP_F18A now means "F18A and not a PICO9918". */
#ifndef COLECO_VDP_PICO9918
#define COLECO_VDP_PICO9918  2
#endif

/* Which implementation renders the VDP. Orthogonal to the type: the type is the
   chip emulated, the engine is whose code does it. */
#ifndef COLECO_VDP_ENGINE_LEGACY
#define COLECO_VDP_ENGINE_LEGACY    0
#endif
#ifndef COLECO_VDP_ENGINE_PICO9918
#define COLECO_VDP_ENGINE_PICO9918  1
#endif

int  coleco_get_vdp_engine(void);

void coleco_set_vdp_type(int vdpType);
int  coleco_get_vdp_type(void);

/* Non-zero for any VDP that answers the F18A feature set: an F18A or a PICO9918. */
int  coleco_vdp_has_f18a(void);

/* VDP state for the debugger and the viewers, from whichever engine is live. Use these
   rather than tms.VR[] / VDP_Memory[] / f18a_*: those are one engine's private state,
   and the renderer is not necessarily that engine. The poke reaches the live core, so
   the debugger's memory editor changes what is on screen. */
unsigned char coleco_vdp_read_register(unsigned char reg);
unsigned char coleco_vdp_peek_vram(unsigned int addr);
void          coleco_vdp_poke_vram(unsigned int addr, unsigned char value);

#endif
