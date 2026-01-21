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
 * coleco.cpp
 *
 * Based on emulation by Marat Fayzullin in 2017-2019
 * Redesign DVdH 2025
*/


#include <cstdio>  // Nodig voor FILE operaties
#include <cstdlib> // Nodig voor rand(), malloc, free
#include <cstring> // Nodig voor memset, memcpy, strcmp, memcmp
#include <QDebug>
#include <QFile>
#include <QIODevice>
#include <QFileInfo>

#include "coleco.h"
#include "utils.h" // Bevat CRC32Block en pad functies

#include "z80.h"
#include "psg_bridge.h"
#include "tms9928a.h"
#include "c24xx.h"    // Nodig voor EEPROM/SRAM
#include "adamnet.h"  // Nodig voor AdamNet functies (PCB)
#include "keypad.h"
#include "ay8910.h"
#include "disasm_bridge.h"
#include "bios_coleco.h"
#include "bios_adam.h"
#include "fdidisk.h"
#include <errno.h>
#include <string.h>

#include "input_bridge.h"
#include "debug_bridge.h"

static uint8_t g_tdos80_shadow[80*24];
static std::atomic<bool> g_tdos80_shadow_dirty{false};

// BIOS loader prototype
static int loadBios(const char *filename, BYTE *memory, int sizerm);
static void Out42(BYTE Val);
// BIOS data komt nu uit colecobios.c en adambios.c (gedeclareerd in coleco.h)
static int g_cpm_trace = 0;
#ifdef ADAMP_CPM_TRAP
static bool g_cpm_memory_active = false;
#endif
static bool g_cpm_protection_active = false;
static int  g_cleanup_phase = 0; // 0=Wachten, 1=Injectie DB, 2=Injectie BF, 3=Klaar

int breakpoints[MAX_BREAKPOINTS];
int breakpoint_count = 0;

extern "C" BYTE adamnet_read_io(int address);

extern "C" void coleco_start_cpm_trace(int count)
{
    g_cpm_trace = count;
}

// 80-column mode flag                                                      │
BYTE coleco_80col_enabled = 0;  // 0=40-col, 1=80-col                       │

void coleco_clear_debug_flags(void)
{
    // Reset cleanup state bij een volledige reset
    g_cleanup_phase = 0;
    g_cpm_protection_active = false;
    qDebug() << "[CPM] Debug flags cleared, cleanup phase reset to 0";
}

void DebugUpdate(void)
{
    // // 1. Normale Breakpoints
    // if (!emulator->stop && !emulator->singlestep) {
    //     if (DEBUG_BRIDGE.checkExecute(Z80.pc.w.l)) {
    //         qDebug() << "[BP] Execute breakpoint hit at PC:" << Qt::hex << Z80.pc.w.l;
    //         emulator->stop = 1;
    //         return;
    //     }
    //     if (DEBUG_BRIDGE.checkPostExecutionBreakpoints()) {
    //         qDebug() << "[BP] Post-execution breakpoint hit at PC:" << Qt::hex << Z80.pc.w.l;
    //         emulator->stop = 1;
    //         return;
    //     }
    //     extern int breakpoint_count;
    //     for (int i = 0; i < breakpoint_count; i++) {
    //         if (Z80.pc.w.l == breakpoints[i]) {
    //             qDebug() << "[BP] Standard breakpoint" << i << "hit at PC:" << Qt::hex << Z80.pc.w.l;
    //             emulator->stop = 1;
    //             return;
    //         }
    //     }
    // }

    if (!emulator->stop && !emulator->singlestep)
    {
        uint16_t pc = Z80.pc.w.l;


        // 0) EXECUTE-breakpoints via DebugBridge (EXE / EXE + FLAG)
        if (DEBUG_BRIDGE.checkExecute(pc)) {
            qDebug() << "[BP] EXECUTE HIT via DebugBridge at PC="
                     << QString::number(pc, 16).rightJustified(4, '0');
            emulator->stop = 1;
            return;
        }

        // 1) Complexe post-execution breakpoints (REG, MEM, FLAGS, CLK, ...)
        if (DEBUG_BRIDGE.checkPostExecutionBreakpoints()) {
            qDebug() << "[BP] HIT via DebugBridge at PC="
                     << QString::number(pc, 16).rightJustified(4, '0');
            emulator->stop = 1;
            return;
        }

        // 2) Bestaande simpele execute-breakpoints (C-array) blijven werken
        for (int i = 0; i < breakpoint_count; i++) {
            if (pc == breakpoints[i]) {
                qDebug() << "[BP] HIT at PC="
                         << QString::number(pc, 16).rightJustified(4, '0')
                         << "idx" << i;
                emulator->stop = 1;
                return;
            }
        }
    }

}


//---------------------------------------------------------------------------
// Globale variabelen (definities)
BYTE cv_display[TVW_*TVH_];
BYTE cv_palette[16*4*3];
int cv_pal32[16*4];

BYTE ROM_Memory[MAX_CART_SIZE * 1024];
BYTE RAM_Memory[MAX_RAM_SIZE * 1024];
BYTE BIOS_Memory[MAX_BIOS_SIZE * 1024];
BYTE SRAM_Memory[MAX_EEPROM_SIZE*1024];
BYTE VDP_Memory[0x10000];

BYTE *MemoryMap[8];

unsigned int sgm_low_addr;
BYTE sgm_neverenable;
BYTE sgm_enable;
BYTE sgm_firstwrite;

BYTE coleco_port20;
BYTE coleco_port60;
BYTE coleco_port53;

BYTE adam_ram_lo;
BYTE adam_ram_hi;
BYTE adam_ram_lo_exp;
BYTE adam_ram_hi_exp;
BYTE adam_128k_mode;

BYTE coleco_megabank;
BYTE coleco_megasize;
BYTE coleco_megacart;

BYTE coleco_joymode;
unsigned int coleco_joystat;

int coleco_spinpos[2];
unsigned int coleco_spinrecur[2];
unsigned int coleco_spinparam[2];
unsigned int coleco_spinstate[2];

int tstates,frametstates;
int tStatesCount;

int coleco_updatetms=0;

FDIDisk Disks[MAX_DISKS] = {};
FDIDisk Tapes[MAX_TAPES] = {};

// In coleco.cpp (bij de globale variabelen, rond lijn 21)
// --- Expansion RAM Variabelen (Definities) ---
// Deze variabelen moeten vroeg gedefinieerd worden om zichtbaarheid te garanderen.
BYTE RAMPages = 2;     // Standaard 2 pagina's = 128KB expansie (naast de 64KB basis)
BYTE RAMPage = 0;      // Huidig geselecteerde Expansion RAM pagina (0 tot RAMPages-1)
BYTE RAMMask = 0xFF;   // Masker voor RAMPages (0xFF om alle bits te maskeren voor de modulo-actie)

static volatile bool s_colecoBiosExternal = false;
static volatile bool s_eosBiosExternal = false;
static volatile bool s_writerBiosExternal = false;

static const char* s_external_coleco_bios_path = NULL;
static const char* s_external_eos_bios_path = NULL;
static const char* s_external_writer_bios_path = NULL;

// De status van de geladen BIOS-bestanden.
// Index 0: Coleco/OS7; 1: EOS; 2: Writer.
// Gebruik int (0=Internal/Fail, 1=External/Success) voor stabiele communicatie.
int g_bios_status_int[3] = {0, 0, 0};

static BYTE idleDataBus = 0xFF;

// Variabelen voor Z80 debug/state
static int lastMemoryReadAddrLo = 0, lastMemoryReadAddrHi = 0;
static int lastMemoryWriteAddrLo = 0, lastMemoryWriteAddrHi = 0;
static BYTE lastMemoryReadValueLo = 0, lastMemoryReadValueHi = 0;
static BYTE lastMemoryWriteValueLo = 0, lastMemoryWriteValueHi = 0;

//const unsigned char TMS9918A_palette[6*16*3] = { /* ... (palette data blijft hetzelfde) ... */ };
// 6 banken × 16 kleuren × RGB
const unsigned char TMS9918A_palette[6*16*3] = {
    // Coleco palette
    24,24,24, 0,0,0, 33,200,66, 94,220,120, 84,85,237, 125,118,252, 212,82,77, 66,235,245,
    252,85,84, 255,121,120, 212,193,84, 230,206,128, 33,176,59, 201,91,186, 204,204,204, 255,255,255,

    // Adam palette
    0,  0,  0,    0,  0,  0,   71,183, 59,  124,207,111,   93, 78,255,  128,114,255,  182, 98, 71,   93,200,237,
    215,107, 72,  251,143,108,  195,205, 65,  211,218,118, 62,159, 47,  182,100,199,  204,204,204,  255,255,255,

    // TMS9918 Palette
    24,24,24, 0,8,0, 0,241,1, 50,251,65, 67,76,255, 112,110,255, 238,75,28, 9,255,255,
    255,78,31, 255,112,65, 211,213,0, 228,221,52, 0,209,0, 219,79,211, 193,212,190, 244,255,241,

    // black and white
    0,  0,  0,    0,  0,  0,  136,136,136,  172,172,172, 102,102,102,  134,134,134,  120,120,120,  172,172,172,
    136,136,136,  172,172,172,  187,187,187,  205,205,205, 118,118,118,  135,135,135,  204,204,204,  255,255,255,

    // Green scales
    0,  0,  0,    0,  0,  0,    0,118,  0,   43,153, 43, 0, 81,  0,    0,118,  0,   43, 81, 43,   43,153, 43,
    43, 81, 43,   43,118, 43,   43,153, 43,   43,187, 43, 43, 81, 43,   43,118, 43,   43,221, 43,    0,255,  0,

    // Ambre scale
    0,  0,  0,    0,  0,  0,  118, 81, 43,  153,118,  0, 81, 43,  0,  118, 81,  0,   81, 43,  0,  187,118, 43,
    118, 81,  0,  153,118, 43,  187,118, 43,  221,153,  0, 118, 81,  0,  153,118, 43,  221,153,  0,  255,187,  0
};

//---------------------------------------------------------------------------
#define DBG_PRINTF(fmt, ...) qDebug().noquote().nospace() << QString().asprintf(fmt, __VA_ARGS__)

#define Clock       3579545
#define SampleRate  44100

//-----------------------------------------------------------------------------------------------------


//-----------------------------------------------------------------------------------------------------
// Get tms vram adress
unsigned short coleco_gettmsaddr(BYTE whichaddr, BYTE mode, BYTE y)
{
    unsigned short result = 0; // Initialiseer

    switch (whichaddr)
    {
    case CHRMAP:
        result = (unsigned short)(tms.ChrTab-VDP_Memory); // Cast naar ushort
        break;
    case CHRGEN:
        result = (unsigned short)(tms.ChrGen-VDP_Memory); // Cast naar ushort
        if ((mode == 2) && (y>= 0x80) )
        {
            switch (tms.VR[4]&3) {
            case 0: break;
            case 1: result+=0x1000; break;
            case 2: break; //PGT-=0x800; break;
            case 3: result+=0x1000; break; //PGT+=0x800; break;
            }
        }
        else if ((tms.VR[4]&0x02) && (mode ==2) && (y>= 0x40))
        {
            result+=0x800;
        }
        break;
    case CHRCOL:
        (unsigned short)(tms.ColTab-VDP_Memory); // Cast naar ushort
        if ((mode == 2) && (y>= 0x80) )
        {
            switch (tms.VR[3]&0x60) {
            case 0: break;
            case 0x20: result+=0x1000; break;
            case 0x40: break; //CLT-=0x800; break;
            case 0x60: result+=0x1000; break; //CLT+=0x800; break;
            }
        }
        else if ((tms.VR[3]&0x40) && (mode ==2) && (y>= 0x40))
        {
            result+=0x800;
        }
        break;
    case SPRATTR:
        result = (unsigned short)(tms.SprTab-VDP_Memory); // Cast naar ushort
        break;
    case SPRGEN:
        result = (unsigned short)(tms.SprGen-VDP_Memory); // Cast naar ushort
        break;
    case VRAM:
        result = 0;
        break;
    case CHRMAP2:
        result = 0;
        break;
    case CHRCOL2:
        result = 0;
        break;
    }

    return result;
}

//---------------------------------------------------------------------------
// Get tms value of vram adress
BYTE coleco_gettmsval(BYTE whichaddr, unsigned short addr, BYTE mode, BYTE y)
{
    BYTE result=0;
    unsigned short base_addr; // Hulpvariabele

    switch (whichaddr)
    {
    case CHRMAP:
        base_addr = (unsigned short)(tms.ChrTab-VDP_Memory);
        result = VDP_Memory[base_addr + addr];
        break;
    case CHRGEN:
        base_addr = (unsigned short)(tms.ChrGen-VDP_Memory);
        switch(mode) {
        case 0:
        case 1:
            break;
        case 2:
            if (y>= 0x80) {
                switch (tms.VR[4]&3) {
                case 1: case 3: base_addr+=0x1000; break;
                }
            } else if ((tms.VR[4]&0x02) && (y>= 0x40)) {
                base_addr+=0x800;
            }
            break;
        }
        result = VDP_Memory[base_addr + addr];
        break;
    case CHRCOL:
        base_addr = (unsigned short)(tms.ColTab-VDP_Memory);
            switch(mode) {
            case 0: case 1: addr>>=3; break;
            case 2:
                if (y>= 0x80){
                    switch (tms.VR[3]&0x60) {
                    case 0x20: case 0x60: base_addr+=0x1000; break;
                    }
                } else if ((tms.VR[3]&0x40) && (y>= 0x40)){
                    base_addr+=0x800;
                }
                break;
            }
        result = VDP_Memory[base_addr + addr];
        break;
    case SPRATTR:
        base_addr = (unsigned short)(tms.SprTab-VDP_Memory);
        result = VDP_Memory[base_addr + addr];
        break;
    case SPRGEN:
        base_addr = (unsigned short)(tms.SprGen-VDP_Memory);
        result = VDP_Memory[base_addr + addr];
        break;
    case VRAM:
        result = VDP_Memory[addr];
        break;
    case SGMRAM:
        result = RAM_Memory[addr];
        break;
    case RAM:
        result = RAM_Memory[0x6000+addr];
        break;
    case EEPROM:
        result = SRAM_Memory[addr];
        break;
    case SRAM:
        result = RAM_Memory[0xE000+addr]; // Correctie: Base address is E000? Origineel was E800
        break;
    }

    return result;
}

//---------------------------------------------------------------------------
// Set a value
void coleco_setval(BYTE whichaddr, unsigned short addr, BYTE y)
{
    switch (whichaddr)
    {
    case VRAM:
        VDP_Memory[addr] = y;
        break;
    case SGMRAM:
        RAM_Memory[addr] = y;
        break;
    case RAM:
        addr&=0x03FF;
        RAM_Memory[0x6000+addr]=RAM_Memory[0x6400+addr]=
        RAM_Memory[0x6800+addr]=RAM_Memory[0x6C00+addr]=
        RAM_Memory[0x7000+addr]=RAM_Memory[0x7400+addr]=
        RAM_Memory[0x7800+addr]=RAM_Memory[0x7C00+addr]=y;
        break;
    case ROM:
        // Dit lijkt incorrect, je zou niet naar ROM moeten kunnen schrijven.
        // Misschien bedoeld voor RAM overlay in ADAM mode? Voorlopig genegeerd.
        // RAM_Memory[addr]=y;
        break;
    case EEPROM:
        SRAM_Memory[addr]=y;
        break;
    case SRAM:
        addr&=0x07FF;
        RAM_Memory[0xE000+addr]=y; // Correctie: Base address is E000?
        break;
    }
}

//---------------------------------------------------------------------------
BYTE coleco_loadcart(char *filename)
{
    long size; // Gebruik long voor ftell resultaat
    int adrlastbank, j;
    BYTE  *p;
    BYTE retf = ROM_LOAD_FAIL;
    FILE *fRomfile = NULL;

    fRomfile = fopen(filename, "rb");
    if (fRomfile == NULL)
        return(retf);

    // Ensure our rom buffer is clear (0xFF to simulate unused memory on ROM/EE though probably 0x00 would be fine too)
    memset(ROM_Memory, 0xFF, (MAX_CART_SIZE * 1024));

    fseek(fRomfile, 0, SEEK_END);
    size = ftell(fRomfile);

    // validate size
    if (size <= (MAX_CART_SIZE * 1024))
    {
        fseek(fRomfile, 0, SEEK_SET);
        if (fread((void*) ROM_Memory, 1, size, fRomfile) != (size_t)size) {
            fclose(fRomfile);
            return retf;
        }

        // Init megacart info
        coleco_megacart = 0x00;
        coleco_megasize = 2; // Standaard 32K

        // Keep initial cartridge CRC (may change after SRAM writes) and do CRC for special games
        emulator->cardsize = (DWORD)size;
        emulator->cardcrc = CRC32Block(ROM_Memory, emulator->cardsize);

        // --- Verificatie (Header check) ---
        p = (ROM_Memory[0]==0x55)&&(ROM_Memory[1]==0xAA)? ROM_Memory
            : (ROM_Memory[0]==0xAA)&&(ROM_Memory[1]==0x55)? ROM_Memory
            : (ROM_Memory[0]==0x66)&&(ROM_Memory[1]==0x99)? ROM_Memory
                                                                 : NULL;

        // Check magic header for Magecarts if not yet found at beginning (for 64K eeprom roms)
        adrlastbank = (size&~0x3FFF)-0x4000;

        if (p==NULL)
        {
            p = (ROM_Memory[adrlastbank]==0x55)&&(ROM_Memory[adrlastbank+1]==0xAA)? ROM_Memory
                : (ROM_Memory[adrlastbank]==0xAA)&&(ROM_Memory[adrlastbank+1]==0x55)? ROM_Memory
                                                                                             : NULL;
        }
        if (p==NULL) { fclose(fRomfile); return(ROM_VERIFY_FAIL); }

        // Point to ram address
        p = RAM_Memory+0x8000;

        // Do we fit within the standard 32K Colecovision Cart ROM memory space?
        if (size <= 32*1024)
        {
            memcpy(p, ROM_Memory, size);
        }
        // No - must be Mega Cart (MC) Bankswitched!!
        else
        {
            // Force load of the first bank when asked to bankswitch
            coleco_megabank = 199;

            // Pad to the nearest 16kB and find number of 16kB pages
            size = ((size+0x3FFF)&~0x3FFF)>>14;

            // Round page number up to the nearest power of two
            for(j=2;j<size;j<<=1);

            // Set new MegaROM size
            size = j<<14;
            coleco_megasize = j;

            // Calculate size
            if (size == (64  * 1024)) coleco_megacart = 0x03;
            else if (size == (128 * 1024)) coleco_megacart = 0x07;
            else if (size == (256 * 1024)) coleco_megacart = 0x0F;
            else /*if (size == (512 * 1024)) */ coleco_megacart = 0x1F; // max 512

            // For MegaCart, we map highest 16K bank into fixed ROM
            memcpy(p,ROM_Memory+(coleco_megacart<<14),0x4000);
            memcpy(p+0x4000,ROM_Memory,0x4000);
        }

        if (emulator->cardcrc == 0x62DACF07) {  // CRC van Boxxle
            BYTE *fixedBank = RAM_Memory + 0x8000;  // vaste 16K bank in RAM

            // Veiligheid: check of er echt een HALT staat
            if (fixedBank[0x00AA] == 0x76) {       // 0x80AA - 0x8000 = 0x00AA
                fixedBank[0x00AA] = 0x00;          // NOP
                qDebug() << "[BOXXLE] Patched HALT at 0x80AA -> NOP";
            } else {
                qDebug() << "[BOXXLE] Unexpected opcode at 0x80AA:" << Qt::hex << int(fixedBank[0x00AA]);
            }
        }


    }
    else
    {
        fclose(fRomfile); return(retf);
    }

    emulator->romCartridgeType = coleco_megacart ? ROMCARTRIDGEMEGA : ROMCARTRIDGESTD;

    return ROM_LOAD_PASS;
}

//---------------------------------------------------------------------------
// update the 16 colors Coleco
void coleco_setpalette(int palette) {
    int index, idxpal;

        idxpal=palette*3*16;
        for (index=0;index<16*3;index+=3) {
            cv_palette[index] = TMS9918A_palette[idxpal+index];
            cv_palette[index+1] = TMS9918A_palette[idxpal+index+1];
            cv_palette[index+2] = TMS9918A_palette[idxpal+index+2];
        }
        RenderCalcPalette(cv_palette,16);
}
///---------------------------------------------------------------------------
// 0 = Coleco/Phoenix, 1 = ADAM
void coleco_set_machine_type(int isAdam)
{
    if (isAdam) {
        emulator->currentMachineType = MACHINEADAM;
    } else {
        emulator->currentMachineType = MACHINECOLECO;
    }
}

//---------------------------------------------------------------------------
// Calculate the 32-bit palette from the 8-bit RGB values
// (Deze functie ontbrak in de originele broncode)
void RenderCalcPalette(BYTE *cv_palette_out, unsigned long nbcolors)
{
    unsigned long i;
    int r, g, b;
    // Zorg ervoor dat we niet buiten de grenzen van cv_pal32 gaan
    if (nbcolors > (sizeof(cv_pal32) / sizeof(cv_pal32[0]))) {
        nbcolors = sizeof(cv_pal32) / sizeof(cv_pal32[0]);
    }

    for (i = 0; i < nbcolors; ++i) {
        // Lees R, G, B waarden uit de input array
        r = cv_palette_out[i * 3 + 0];
        g = cv_palette_out[i * 3 + 1];
        b = cv_palette_out[i * 3 + 2];

        // Creëer een 32-bit integer in 0x00RRGGBB formaat
        cv_pal32[i] = (r << 16) | (g << 8) | b;
    }
}

//---------------------------------------------------------------------------
void coleco_setadammemory(bool resetAdamNet)
{
    if (emulator->currentMachineType != MACHINEADAM) return;

    // NIEUW: Bereken de basis-offset voor Exp. RAM als deze geselecteerd is
    // RAMPage wordt gezet door Out42. 0xFF is de marker voor een ongeldige/niet-bestaande pagina.
    unsigned int exp_ram_offset = 0;
    if (RAMPage != 0xFF) {
        // 0x10000 (64KB) is de start van de expansie RAM na de 64KB basis
        // (RAMPage is 0, 1, 2, ... voor de 64KB blokken)
        exp_ram_offset = 0x10000 + ((unsigned int)RAMPage * 0x10000);
    }

    // Configure lower 32K of memory (0x0000 - 0x7FFF)
    if ((coleco_port60 & 0x03) == 0x00) // WRITER/EOS ROM
    {
        adam_ram_lo = 0; adam_ram_lo_exp = 0;
        MemoryMap[0] = BIOS_Memory + 0x0000; MemoryMap[1] = BIOS_Memory + 0x2000;
        MemoryMap[2] = BIOS_Memory + 0x4000;
        MemoryMap[3] = (coleco_port20 & 0x02) ? BIOS_Memory + 0x8000  // Smartwriter
                                              : BIOS_Memory + 0x6000; // EOS
    }
    else if ((coleco_port60 & 0x03) == 0x01) // Onboard RAM
    {
        adam_ram_lo = 1; adam_ram_lo_exp = 0;
        MemoryMap[0] = RAM_Memory + 0x0000; MemoryMap[1] = RAM_Memory + 0x2000;
        MemoryMap[2] = RAM_Memory + 0x4000; MemoryMap[3] = RAM_Memory + 0x6000;
    }
    // Expanded RAM (Cruciale Fix: Gebruik exp_ram_offset)
    else if ((coleco_port60 & 0x03) == 0x02)
    {
        adam_128k_mode = 1; adam_ram_lo = 0; adam_ram_lo_exp = 1;
        if (RAMPage != 0xFF) {
            MemoryMap[0] = RAM_Memory + exp_ram_offset + 0x0000;
            MemoryMap[1] = RAM_Memory + exp_ram_offset + 0x2000;
            MemoryMap[2] = RAM_Memory + exp_ram_offset + 0x4000;
            MemoryMap[3] = RAM_Memory + exp_ram_offset + 0x6000;
        } else {
            // Expansie RAM niet aanwezig of ongeldige pagina gekozen
            MemoryMap[0] = MemoryMap[1] = MemoryMap[2] = MemoryMap[3] = RAM_Memory;
        }
    }
    else if ((coleco_port60 & 0x03) == 0x03) // Coleco BIOS + RAM
    {
        adam_ram_lo = 1; adam_ram_lo_exp = 0;
        MemoryMap[0] = BIOS_Memory + 0xA000; MemoryMap[1] = RAM_Memory + 0x2000;
        MemoryMap[2] = RAM_Memory + 0x4000; MemoryMap[3] = RAM_Memory + 0x6000;
    }
    // Niets anders bestaat (Val door naar standaard RAM)
    else
    {
        adam_ram_lo = 0; adam_ram_lo_exp = 0;
        MemoryMap[0] = RAM_Memory + 0x0000; MemoryMap[1] = RAM_Memory + 0x2000;
        MemoryMap[2] = RAM_Memory + 0x4000; MemoryMap[3] = RAM_Memory + 0x6000;
    }

    // Configure upper 32K of memory (0x8000 - 0xFFFF)
    // -> Onboard RAM (case 0x00)
    if ((coleco_port60 & 0x0C) == 0x00)
    {
        adam_ram_hi = 1;
        adam_ram_hi_exp = 0;
        MemoryMap[4] = RAM_Memory + 0x8000;
        MemoryMap[5] = RAM_Memory + 0xA000;
        MemoryMap[6] = RAM_Memory + 0xC000;
        MemoryMap[7] = RAM_Memory + 0xE000;
    }
    // -> Expanded ROM (case 0x04) is niet geïmplementeerd in deze code, negeer.
    // -> Expanded RAM (Cruciale Fix: Gebruik exp_ram_offset)
    else if ((coleco_port60 & 0x0C) == 0x08)
    {
        adam_128k_mode = 1;
        adam_ram_hi = 0;
        adam_ram_hi_exp = 1;
        if (RAMPage != 0xFF) {
            MemoryMap[4] = RAM_Memory + exp_ram_offset + 0x8000;
            MemoryMap[5] = RAM_Memory + exp_ram_offset + 0xA000;
            MemoryMap[6] = RAM_Memory + exp_ram_offset + 0xC000;
            MemoryMap[7] = RAM_Memory + exp_ram_offset + 0xE000;
        } else {
            // Expansie RAM niet aanwezig of ongeldige pagina gekozen
            MemoryMap[4] = MemoryMap[5] = MemoryMap[6] = MemoryMap[7] = RAM_Memory + 0x8000;
        }
    }
    // -> Cartridge ROM (case 0x0C)
    else if ((coleco_port60 & 0x0C) == 0x0C)
    {
        adam_ram_hi = 0;
        adam_ram_hi_exp = 0;
        // Gebruik de bestaande MegaCart logica voor slot 4-7 mapping
        MemoryMap[4] = RAM_Memory + 0x8000; // Cartridge is gemapt op 0x8000 in RAM_Memory
        MemoryMap[5] = RAM_Memory + 0xA000;
        MemoryMap[6] = RAM_Memory + 0xC000;
        MemoryMap[7] = RAM_Memory + 0xE000;
    }
    // Nothing else exists so just return 0xFF
    else
    {
        adam_ram_hi = 0;
        adam_ram_hi_exp = 0;
        MemoryMap[4] = RAM_Memory + 0x8000;
        MemoryMap[5] = RAM_Memory + 0xA000;
        MemoryMap[6] = RAM_Memory + 0xC000;
        MemoryMap[7] = RAM_Memory + 0xE000;
    }

    uint16_t pc = Z80.pc.w.l;
    if (pc >= 0xC800 && pc < 0xCC00) {
        // CP/M loader + stack lives in C000..FFFF -> force it to RAM to keep stack stable
        MemoryMap[6] = RAM_Memory + 0xC000;
        MemoryMap[7] = RAM_Memory + 0xE000;
    }

    if (resetAdamNet)  ResetPCB();
}
//---------------------------------------------------------------------------
void coleco_setupsgm(void)
{
    // Super DK mag nooit SGM hebben
    if (sgm_neverenable) return;
    if (emulator->currentMachineType == MACHINEADAM) return;

    // Port 53 bit 0 bepaalt SGM memory enable
    sgm_enable = (coleco_port53 & 0x01) ? 1:0;

    // Clear SGM RAM bij de eerste keer (alleen 24K, 0x2000-0x7FFF)
    if (sgm_enable && sgm_firstwrite)
    {
        memset(RAM_Memory+0x2000, 0x00, 0x6000);
        sgm_firstwrite = 0;
    }

    // Port 60 bit 1 (0x02) bepaalt BIOS (1) of 8K SGM RAM (0)
    if (coleco_port60 & 0x02)
    {
        // BIOS is AAN
        if (sgm_low_addr != 0xFFFF) // 0xFFFF = marker voor BIOS
        {
            sgm_low_addr = 0xFFFF;
            MemoryMap[0] = BIOS_Memory + 0x0000;
        }
    }
    else
    {
        // BIOS is UIT, map 8K SGM RAM
        sgm_enable = 1; // Forceren, zoals in emultwo
        if (sgm_low_addr != 0x0000) // 0x0000 = marker voor RAM
        {
            MemoryMap[0] = RAM_Memory + 0x0000;
            sgm_low_addr = 0x0000;
        }
    }

    // OPMERKING: We raken 0x2000-0xFFFF (Blok 1-7) NIET AAN.
    // Dit laat de "copy-to-RAM" mapping van coleco_reset (751)
    // intact, waardoor de SGM >32K crash wordt voorkomen.
}
//---------------------------------------------------------------------------
void coleco_reset(void)
{
    int i;

    // Init memory pages (plat 64K RAM)
    MemoryMap[0] = RAM_Memory + 0x0000;
    MemoryMap[1] = RAM_Memory + 0x2000;
    MemoryMap[2] = RAM_Memory + 0x4000;
    MemoryMap[3] = RAM_Memory + 0x6000;
    MemoryMap[4] = RAM_Memory + 0x8000;
    MemoryMap[5] = RAM_Memory + 0xA000;
    MemoryMap[6] = RAM_Memory + 0xC000;
    MemoryMap[7] = RAM_Memory + 0xE000;

    if (emulator->currentMachineType != MACHINEADAM)
    {
        memcpy(RAM_Memory, BIOS_Memory, 0x2000);

        // Hacks (50/60Hz + nodelay)
        RAM_Memory[0x0069] = emulator->hackbiospal ? 50 : 60;
        if (emulator->biosnodelay) {
            RAM_Memory[159*32+17] = 0x00;
            RAM_Memory[159*32+18] = 0x00;
            RAM_Memory[159*32+19] = 0x00;
         }
    }

    // Randomize 0x6000-0x7FFF (NetPlay-consistentie); ok om te laten
    if (emulator->currentMachineType != MACHINEADAM) {
        for (i=0;i<0x2000;i++)
            RAM_Memory[i+0x6000] = rand() % 256;
    }

    sgm_enable     = 0;
    sgm_firstwrite = 1;
    sgm_low_addr   = 0xFFFF;
    sgm_neverenable= 0;

    // Init SGM/ADAM-poorten
    coleco_port53 = 0x00;
    coleco_port60 = (emulator->currentMachineType == MACHINEADAM) ? 0x00 : 0x0F;
    coleco_port20 = 0x00;

    // ADAM memory init
    if (emulator->currentMachineType == MACHINEADAM)
    {
        adam_ram_lo = adam_ram_hi = adam_ram_lo_exp = adam_ram_hi_exp = 0;
        adam_128k_mode = 0; // 64K basis
    }
    else
    {
        MemoryMap[0] = BIOS_Memory + 0x0000;
    }

    // Backup-type autodetectie
    emulator->typebackup = NOBACKUP;
    switch (emulator->cardcrc)
    {
    case 0x62DACF07: emulator->typebackup = EEP24C256; break; // Boxxle
    case 0xDDDD1396: emulator->typebackup = EEP24C08;  break;
    case 0xFEE15196:
    case 0x1053F610:
    case 0x60D6FD7D:
    case 0x37A9F237: emulator->typebackup = EEPSRAM;   break;
    case 0xEF25AF90:
    case 0xC2E7F0E0: sgm_neverenable = 1;           break;
    }

    // VDP reset
    tms9918_reset();
    tms.ScanLines = emulator->NTSC ? TMS9918_LINES : TMS9929_LINES;

    // PSG’s
    sn76489_init(Clock, SampleRate);
    ay8910_init(Clock, SampleRate);

    // EEPROM reset
    if (emulator->typebackup != NOBACKUP && emulator->typebackup != EEPSRAM) {
        c24xx_reset(SRAM_Memory, emulator->typebackup==EEP24C08 ? C24XX_24C08 : C24XX_24C256);
    }

    z80_reset();

    // Vars
    tStatesCount = 0;

    // Input init
    coleco_joymode = 0;
    coleco_joystat = 0x00000000;
    coleco_spinpos[0]=coleco_spinpos[1]=0;
    coleco_spinrecur[0]=coleco_spinrecur[1]=0;
    coleco_spinparam[0]=coleco_spinparam[1]=0;
    coleco_spinstate[0]=coleco_spinstate[1]=0;

    // EIND-mapping: exact één keer, afhankelijk van machine
    if (emulator->currentMachineType == MACHINEADAM) {
        coleco_setadammemory(true);  // resetAdamNet = true, mapt EOS/Writer/OS7 correct
    } else {
        coleco_setupsgm();           // laat port53/port60 regels bepalen of low 8K BIOS/RAM is
    }
}

//---------------------------------------------------------------------------
void coleco_reset_and_restart_bios()
{

    // 1) Defaults per machine
    if (emulator->currentMachineType == MACHINEADAM) {
        // ADAM: geen SGM; memory wordt door 0x60 bits gestuurd
        emulator->SGM = false;
        coleco_port53 = 0x00;
        coleco_writeport(0x53, coleco_port53, nullptr);

        // ADAM default memorycontrol: WRITER/EOS + cart in hoge 32K
        // (pas aan indien jouw setadammemory iets anders verwacht)
        coleco_port60 = 0x00;
        coleco_writeport(0x60, coleco_port60, nullptr);

        // 2) Bouw ADAM-mapping op
        coleco_setadammemory(/*resetAdamNet=*/true);
    } else {
        // Coleco/Phoenix: standaard BIOS+cart
        // MOET ALTIJD 0x0F (BIOS AAN) zijn bij een reset!
        coleco_port60 = 0x0F;
        coleco_writeport(0x60, coleco_port60, nullptr);

        // Stel de SGM hardware poort (0x53) wel alvast in
        coleco_port53 = emulator->SGM ? 0x01 : 0x00;
        coleco_writeport(0x53, coleco_port53, nullptr);

        // 2) Bouw Coleco-mapping op (die nu 0x0F respecteert)
        coleco_setupsgm();
     }

    // 3) Cartridge pages voor de zekerheid opnieuw (met respect voor megacarts)
    if (coleco_megacart)
    {
        // Forceer bank her-evaluatie
        coleco_megabank = 199;
        megacart_bankswitch(0); // Zet bank 0 op 0x8000
    }
    else
    {
        if (emulator->currentMachineType != MACHINEADAM)
        {
            coleco_setupsgm();
        }        // Standaard 32K cart mapping
    }

    tms9918_reset();
    tms.ScanLines = emulator->NTSC ? TMS9918_LINES : TMS9929_LINES;

    z80_reset();
}
//---------------------------------------------------------------------------
void coleco_hardreset(void)
{
    qDebug().noquote() << QString("[RESET] CORE HARDRESET, PC=%1 SP=%2 cpm=%3")
        .arg(Z80.pc.w.l, 4, 16, QChar('0'))
        .arg(Z80.sp.w.l, 4, 16, QChar('0'));

    // 1) Maak de cartbuffer “open bus”: 0xFF
    memset(ROM_Memory, 0xFF, MAX_CART_SIZE * 1024);   // 512 KiB max. cartsize

    // 2) Reset megacart/bankswitch state
    coleco_megacart = 0;
    coleco_megasize = 2;   // standaard 32 KiB mapping (veilig default)
    coleco_megabank = 0;

    // 3) Re-map ROM-gebied (0x8000-0xFFFF) naar onze (lege) ROM_Memory
    //    Slots 4..7 zijn respectievelijk 0x8000, 0xA000, 0xC000, 0xE000
    MemoryMap[4] = ROM_Memory + 0x0000;
    MemoryMap[5] = ROM_Memory + 0x2000;
    MemoryMap[6] = ROM_Memory + 0x4000;
    MemoryMap[7] = ROM_Memory + 0x6000;

    // 4) (Aanrader) CPU en VDP netjes resetten zodat BIOS meteen beeld kan geven
    //    en de jump niet in “oude” cartcode terechtkomt.
    //    Als je “BIOS only” wil laten draaien:
    z80_reset();
    tms9918_reset();
    coleco_reset_and_restart_bios();  // zet BIOS op 0x0000 + reset VDP/CPU/PSG
}

//---------------------------------------------------------------------------
static int bios_external_ok(const char* path, int needBytes)
{
    if (!path || !path[0]) return 0;

    FILE* f = fopen(path, "rb");
    if (!f) return 0;

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fclose(f);

    return (sz >= needBytes) ? 1 : 0;
}

// Altijd alle 3 controleren, onafhankelijk van machine type
void coleco_probe_bios_status_all(void)
{
    // 0=coleco/os7 (8KB), 1=eos (8KB), 2=writer (32KB)
    g_bios_status_int[0] = bios_external_ok(s_external_coleco_bios_path, 0x2000);
    g_bios_status_int[1] = bios_external_ok(s_external_eos_bios_path,    0x2000);
    g_bios_status_int[2] = bios_external_ok(s_external_writer_bios_path, 0x8000);

    // optioneel: sync je flags
    s_colecoBiosExternal = (g_bios_status_int[0] != 0);
    s_eosBiosExternal    = (g_bios_status_int[1] != 0);
    s_writerBiosExternal = (g_bios_status_int[2] != 0);
}
//---------------------------------------------------------------------------

int loadBios(const char *filename, BYTE *memory, int sizerm)
{
    if (!filename || !filename[0]) {
        qWarning() << "[BIOS] loadBios: empty filename";
        return 0;
    }

  //  qDebug() << "[BIOS] loadBios: trying:" << filename << "need bytes:" << sizerm;

    FILE *fbios = fopen(filename, "rb");
    if (!fbios) {
        qWarning() << "[BIOS] loadBios: fopen FAILED for" << filename
                   << "errno=" << errno << "(" << strerror(errno) << ")";
        return 0;
    }

    // File size debug
    fseek(fbios, 0, SEEK_END);
    long fsize = ftell(fbios);
    fseek(fbios, 0, SEEK_SET);
  //  qDebug() << "[BIOS] loadBios: file size =" << fsize;

    size_t bytes_read = fread((void*)memory, 1, (size_t)sizerm, fbios);
    fclose(fbios);

  //  qDebug() << "[BIOS] loadBios: bytes_read =" << (long long)bytes_read;

    if (bytes_read != (size_t)sizerm) {
        qWarning() << "[BIOS] loadBios: READ SIZE MISMATCH. Expected" << sizerm
                   << "got" << (long long)bytes_read;
        return 0;
    }

    return 1;
}
//---------------------------------------------------------------------------
static void loadSingleBios(const char* externalPath,
                           const unsigned char* internalData,
                           size_t size,
                           BYTE* dest,
                           const char* name,
                           int index)
{
    // Default: internal
    g_bios_status_int[index] = 0;

    const bool hasPath = (externalPath && externalPath[0] != '\0');
    const QString extPath = QString::fromLocal8Bit(externalPath);
    QFileInfo fi(extPath);

    bool externalValid = false;
    //qint64 extSize = -1;

    if (hasPath) {
        //extSize = fi.exists() ? fi.size() : -1;
        externalValid = fi.exists() && fi.isFile() && (fi.size() >= (qint64)size);
        g_bios_status_int[index] = externalValid ? 1 : 0;
    }

    // --- extern Loaded ---
    if (externalValid) {
        if (loadBios(externalPath, dest, (int)size))
          {
            qDebug() << "[BIOS] USE External" << fi.fileName() << "ROM.";

            // flags optioneel
            if (index == 0) s_colecoBiosExternal = true;
            if (index == 1) s_eosBiosExternal    = true;
            if (index == 2) s_writerBiosExternal = true;
            return;
          }
        qWarning() << "[BIOS] FOUT: Extern bestand leek geldig, maar loadBios faalde voor" << name
                   << "(permissions/lock/read?).";
    }
    // --- intern loaded ---
    if (!externalValid && internalData) {
        memcpy(dest, internalData, size);
        qDebug() << "[BIOS] USE Internal" << name << "ROM.";
    }

    // Flags resetten (optioneel)
    if (index == 0) s_colecoBiosExternal = false;
    if (index == 1) s_eosBiosExternal    = false;
    if (index == 2) s_writerBiosExternal = false;

    //qDebug() << "[BIOS] Controle:" << name << "startbyte=0x" << Qt::hex << (int)dest[0];
}
//---------------------------------------------------------------------------
void coleco_load_bios(void)
{
    // 0) Reset ALLE status (source of truth)
    g_bios_status_int[0] = 0; // Coleco / OS7
    g_bios_status_int[1] = 0; // EOS
    g_bios_status_int[2] = 0; // Writer

    // (optioneel, maar handig als je elders nog die flags gebruikt)
    s_colecoBiosExternal = false;
    s_eosBiosExternal    = false;
    s_writerBiosExternal = false;

    emulator->bios_loaded = false;

    // 1) MEMORY CLEAR
    memset(BIOS_Memory, 0xFF, MAX_BIOS_SIZE   * 1024);
    memset(SRAM_Memory, 0xFF, MAX_EEPROM_SIZE * 1024);

    // 2) BIOS LAAD LOGICA
    if (emulator->currentMachineType == MACHINEADAM)
    {
        // OS7 (8KB, 0xA000), Index 0
        loadSingleBios(s_external_coleco_bios_path, colecobios_rom, 0x2000,
                       BIOS_Memory + 0xA000, "COLECO / OS7", 0);

        // EOS (8KB, 0x8000), Index 1
        loadSingleBios(s_external_eos_bios_path, adambios_eos, 0x2000,
                       BIOS_Memory + 0x8000, "EOS", 1);

        // WRITER (32KB, 0x0000), Index 2
        loadSingleBios(s_external_writer_bios_path, adambios_writer, 0x8000,
                       BIOS_Memory + 0x0000, "WRITER", 2);

        // Als interne ROM's bestaan, is er altijd een BIOS aanwezig (extern of fallback intern)
        emulator->bios_loaded = true;
    }
    else
    {
        // COLECOVISION BIOS (8KB), Index 0
        loadSingleBios(s_external_coleco_bios_path, colecobios_rom, 0x2000,
                       BIOS_Memory, "Coleco", 0);

        emulator->bios_loaded = true;
    }

    // 3) Sync (optioneel) flags met de échte status-array
    s_colecoBiosExternal = (g_bios_status_int[0] != 0);
    s_eosBiosExternal    = (g_bios_status_int[1] != 0);
    s_writerBiosExternal = (g_bios_status_int[2] != 0);

}
//---------------------------------------------------------------------------
void coleco_base_init(void)
{
    z80_init();
    tStatesCount = 0;
    coleco_megasize = 2;
    coleco_megacart = 0;
    emulator->romCartridgeType = ROMCARTRIDGENONE;

    memset(ROM_Memory,  0xFF, MAX_CART_SIZE  * 1024);
    memset(RAM_Memory,  0xFF, MAX_RAM_SIZE   * 1024);
    //memset(BIOS_Memory, 0xFF, MAX_BIOS_SIZE  * 1024); // ← BIOS vooraf leegmaken
    //memset(SRAM_Memory, 0xFF, MAX_EEPROM_SIZE * 1024);

    //coleco_load_bios();


    // Verwijder alle Adam-media
    for (int i = 0; i < MAX_DISKS;  ++i) EjectFDI(&Disks[i]);
    for (int i = 0; i < MAX_TAPES;  ++i) EjectFDI(&Tapes[i]);

    // Reset & palet
    coleco_reset();
    coleco_setpalette(emulator->palette);
}

//---------------------------------------------------------------------------
void coleco_initialise(void)
{
    // 1. Eerst de veilige basiscomponenten initialiseren
    coleco_base_init();

    // Initialize 80-column mode to off
         coleco_80col_enabled = 0;

    // 2. Laad BIOS (met fallback) en stel de statusvlaggen in
    // Deze functie doet de memset() van BIOS_Memory en laadt de ROMs.
    coleco_load_bios();

    // 3. Na de BIOS-lading: herstart de CPU met de nieuwe mapping
    // Dit zorgt ervoor dat de Z80 start op 0x0000 met de geladen BIOS data
    coleco_reset_and_restart_bios();
}
//---------------------------------------------------------------------------

// Switch banks. Up to 512K of the Colecovision Mega Cart ROM can be stored
void megacart_bankswitch(BYTE bank)
{
    // Only if the bank was changed...
    if (coleco_megabank != bank)
    {
        qDebug() << "[MEGA] switch to bank" << (int)bank
                 << " (mask=0x" << Qt::hex << (int)coleco_megacart << ")";

        MemoryMap[6] = ROM_Memory + ((unsigned int) bank * 0x4000);
        MemoryMap[7] = MemoryMap[6] + 0x2000;
        coleco_megabank = bank;
    }
}
//---------------------------------------------------------------------------
void coleco_WriteByte(unsigned int Address, int Data)
{
    // --- ADAM MODUS ---
    if (emulator->currentMachineType == MACHINEADAM)
    {

        // Adam-geheugen heeft GEEN 1K-spiegel.
        // Het is een platte 64K/128K map.
        // We schrijven naar RAM EN laten de PCB meeluisteren.

        if ((Address < 0x8000) && adam_ram_lo)
        {
            RAM_Memory[Address] = (BYTE)Data;
            if (PCBTable[Address]) WritePCB(Address, Data);
        }
        else if ((Address >= 0x8000) && adam_ram_hi)
        {
            RAM_Memory[Address] = (BYTE)Data;
            if (PCBTable[Address]) WritePCB(Address, Data);
        }

        // else if (adam_ram_lo_exp || adam_ram_hi_exp)
        // {
        //     // Schrijf altijd via MemoryMap zodat RAMPage/port60 mapping klopt
        //     *(MemoryMap[Address >> 13] + (Address & 0x1FFF)) = (BYTE)Data;
        // }
        else if (adam_ram_lo_exp || adam_ram_hi_exp)
        {
            // chrijf de data naar het Expansion RAM
            *(MemoryMap[Address >> 13] + (Address & 0x1FFF)) = (BYTE)Data;
        }
        return; // ADAM-pad afgehandeld
    }

    // --- COLECO MODUS (else) ---
    else
    {
        // SGM RAM
        if (sgm_enable)
        {
            if (Address < 0x2000 && sgm_low_addr == 0x0000)
            {
                RAM_Memory[Address] = Data;
                return; // Klaar
            }
            else if (Address >= 0x2000 && Address < 0x8000)
            {
                RAM_Memory[Address] = Data; // Schrijf naar 24K SGM RAM
                return; // Klaar
            }
        }
        // Standaard 1K RAM (gespiegeld)
        else if((Address>0x5FFF)&&(Address<0x8000))
        {
            Address&=0x03FF;
            RAM_Memory[0x6000+Address]=RAM_Memory[0x6400+Address]=
                RAM_Memory[0x6800+Address]=RAM_Memory[0x6C00+Address]=
                RAM_Memory[0x7000+Address]=RAM_Memory[0x7400+Address]=
                RAM_Memory[0x7800+Address]=RAM_Memory[0x7C00+Address]=Data;
            return; // Klaar
        }
    }

    // --- STAP 2: Hardware/Cartridge-write (Gedeeld door Adam & Coleco) ---
    // Als de code hier komt, was het geen schrijf-actie naar RAM.
    // Het moet dus hardware zijn (SRAM, EEPROM, of Megacart).

    // Allow SRAM
    if ((Address >= 0xE000) && (Address < 0xE800))
    {
        if (emulator->typebackup==EEPSRAM)
        {
            RAM_Memory[Address+0x800]=Data;
            return;
        }
    }

    // Cartridges, containing EEPROM
    else if (((emulator->currentMachineType != MACHINEADAM) && (emulator->typebackup==EEP24C08)) || (emulator->typebackup==EEP24C256) )
    {
        if ((Address == 0xFF90) || (Address == 0xFFA0) || (Address == 0xFFB0))
        {
            qDebug() << "[BOXXLE] bankswitch write @"
                     << Qt::hex << Address
                     << "data=" << Data
                     << "mask=0x" << int(coleco_megacart);
            megacart_bankswitch((Address>>4) & coleco_megacart);
        }

        switch(Address)
        {
        case 0xFFC0: qDebug() << "[BOXXLE] EEPROM SCL low";c24xx_write(c24.Pins&~C24XX_SCL);return;
        case 0xFFD0: qDebug() << "[BOXXLE] EEPROM SCL high";c24xx_write(c24.Pins|C24XX_SCL);return;
        case 0xFFE0: qDebug() << "[BOXXLE] EEPROM SDA low";c24xx_write(c24.Pins&~C24XX_SDA);return;
        case 0xFFF0: qDebug() << "[BOXXLE] EEPROM SDA high";c24xx_write(c24.Pins|C24XX_SDA);return;
        }
        return; // Belangrijk: return ook als het geen van de cases was
    }

    // Handle Megacart Hot Spot writes
    else if (Address >= 0xFFC0)
    {
        megacart_bankswitch(Address & coleco_megacart);
        return;
    }
}

//---------------------------------------------------------------------------
void coleco_setbyte(int Address, int Data) { coleco_WriteByte(Address, Data); } // Debugger
//---------------------------------------------------------------------------
void coleco_writebyte(unsigned int Address, int Data) { // Vanuit Z80
    lastMemoryWriteAddrLo = lastMemoryWriteAddrHi; lastMemoryWriteAddrHi = Address;
    lastMemoryWriteValueLo = lastMemoryWriteValueHi; lastMemoryWriteValueHi = Data;
    coleco_WriteByte(Address, Data);
}
//---------------------------------------------------------------------------
BYTE coleco_ReadByte(int Address)
{
    // --- Megacart read-hotspots ---
    // Bij lezen >= 0xFFC0 eerst bankswitchen,
    // maar GEEN rare waarden teruggeven
    if (emulator->currentMachineType != MACHINEADAM && coleco_megacart) {
        if (Address >= 0xFFC0) {
            megacart_bankswitch(Address & coleco_megacart);
        }
    }

    // --- EEPROM data-byte ---
    if (emulator->currentMachineType != MACHINEADAM &&
        ((emulator->typebackup==EEP24C08)||(emulator->typebackup==EEP24C256)) &&
        Address==0xFF80)
    {
        return c24xx_read();
    }

    // AdamNet side-effect + ALTIJD geheugenbyte teruggeven
    if (emulator->currentMachineType == MACHINEADAM && PCBTable[Address]) {
        (void)ReadPCB(Address); // side-effect only
    }

    // Uiteindelijk ALTIJD de echte byte uit het geheugen teruggeven
    return *(MemoryMap[Address>>13] + (Address & 0x1FFF));
}
//---------------------------------------------------------------------------
// Z80 geheugenlees-hook
BYTE coleco_getbyte(int Address)
{
    // 1. Voer EERST de pure hardwarelees-actie uit om de Data te verkrijgen.
    // De implementatie van coleco_ReadByte moet al de geheugenmapping bevatten.
    BYTE Data = coleco_ReadByte(Address);
    return Data;
}
//---------------------------------------------------------------------------

BYTE coleco_readoperandbyte(int Address) { return coleco_ReadByte(Address); } // Z80 Operand
BYTE coleco_readbyte(int Address) { // Vanuit Z80 met logging
    lastMemoryReadAddrLo = lastMemoryReadAddrHi; lastMemoryReadAddrHi = Address;
    BYTE byte = coleco_ReadByte(Address);
    lastMemoryReadValueLo = lastMemoryReadValueHi; lastMemoryReadValueHi = byte;
    return byte;
}
BYTE coleco_opcode_fetch(int Address) { return coleco_ReadByte(Address); } // Z80 Opcode

//---------------------------------------------------------------------------
void coleco_set_ram_page(int page)
{
    page &= 0x03;                 // ADAM expansion pages are 0..3
    Out42((BYTE)page);            // reuse existing mapping logic
}
//---------------------------------------------------------------------------
// --- NIEUWE FUNCTIE: Out42 (Expansion RAM Page Select) ---
static void Out42(BYTE Val)
{
    // Val is de gewenste pagina (bits 0-7)
    BYTE a;
    a = Val & RAMMask; // Maskeren met het ingestelde masker

    // Controleer of de gevraagde pagina geldig is.
    if (a >= RAMPages) {
        a = 0xFF; // Ongeldig, 0xFF gebruiken als marker
    }

    if (a != RAMPage)
    {
        RAMPage = a;
        // Bij wijziging moet de mapping onmiddellijk worden bijgewerkt
        coleco_setadammemory(false); // Her-map het geheugen zonder ADAMNet te resetten

        qDebug() << "[MEM] Expansion RAM page" << (RAMPage == 0xFF ? "INVALID" : QString::number(RAMPage)) << "selected.";
    }
}
//---------------------------------------------------------------------------
//---------------------------------------------------------------------------
void coleco_writeport(int Address, int Data, int * /**tstates*/)
{
    bool resetadam = 0;

    Address &= 0xFF; // 8-bit poort adres

    switch(Address & 0xE0)
    {
    case 0x00: // 0x00 - 0x1F: Unused
        break;

    case 0x20: // 0x20 - 0x3F: AdamNet Control
        resetadam=(coleco_port20 & 1) && ((Data & 1) == 0);
        coleco_port20=Data;
        if (emulator->currentMachineType == MACHINEADAM) coleco_setadammemory(resetadam);
        else if(emulator->SGM) coleco_setupsgm();
        break;

    case 0x60: // 0x60 - 0x7F: Memory Control
    {
        int v = Data & 0xFF;

        coleco_port60 = v;

        if (emulator->currentMachineType == MACHINEADAM) coleco_setadammemory(resetadam);
        else if (emulator->SGM) coleco_setupsgm();
        break;
    }

    case 0x40: // 0x40-0x5F: Printer / SGM Sound / SGM Control
        if((emulator->currentMachineType == MACHINEADAM)&&(Address==0x40)) Printer(Data);
        else if ((emulator->currentMachineType == MACHINEADAM)&&Address==0x42) Out42(Data); // <<< NIEUW: EXPANSION RAM PAGE SELECT
        else if(emulator->SGM)
        {
            if(Address==0x53) { coleco_port53 = Data; coleco_setupsgm(); }
            else if (Address==0x50) ay8910_write(0,Data); // Control data
            else if (Address==0x51) ay8910_write(1,Data); // Write data
        }
        break;


    case 0x80: // 0x80 - 0x9F: Controller 1 Write (Mode Select)
        coleco_io_write((uint8_t)Address, (uint8_t)Data);
        break;
    case 0xA0: // 0xA0 - 0xBF: VDP Write (Video Display Processor)
        coleco_updatetms=1;
        if((Address & 0x01)==0) // Even addresses
        { tms9918_writedata(Data); }
        else // Odd addresses
        { tms9918_writectrl(Data);
            // CRUCIALE FIX: Als we in C80 modus zitten, moeten we
            // soms specifieke register-instellingen forceren die
            // T-DOS verwacht voor een 80-koloms lineaire buffer.
            if (coleco_80col_enabled) {
                // Forceer Text Mode (Mode 0) maar met aangepaste timing/breedte
                // Dit zorgt dat de interne 'tms' structuur de juiste tabellen kiest.
                tms.Mode = 0;
            }
        }
        break;
    case 0xC0: // 0xC0 - 0xDF: Controller 2 Write (Mode Select)
        coleco_io_write((uint8_t)Address, (uint8_t)Data);
        break;

    case 0xE0: // 0xE0 - 0xFF: Sound Chip Write (SN76489)
        sn76489_write((uint8_t)Data);
        break;
    }
}

//---------------------------------------------------------------------------
BYTE coleco_readport(int Address, int * /*tstates*/) // Interne leesfunctie poort
{
    Address &= 0xFF; // 8-bit poort adres

    switch(Address & 0xE0)
    {
    case 0x00: // 0x00 - 0x1F: Unused
        break;

    case 0x20: // 0x20 - 0x3F: AdamNet Control Read
        if (emulator->currentMachineType == MACHINEADAM)
        {
            return(coleco_port20);
        }
        break;

    case 0x40: // 0x40 - 0x5F: Printer Status / SGM Sound Read
        if (emulator->currentMachineType == MACHINEADAM)
        {
            // --- Adam Modus ---
            if (Address == 0x40) {
                // Printer Status Read (Ready/OK)
                return 0xFF;
            }

            if (Address == 0x42 || Address == 0x43) {
                // RAM Page Select status/read (teruggeven wat er op poort 0x20 staat, of een vaste waarde)
                return coleco_port20;
            }

            // C80/Eve-kaart detectie: Veel 80-koloms software leest poort 0x52 of 0x53
            // om de aanwezigheid van de hardware te verifiëren.
            if (coleco_80col_enabled && (Address == 0x52 || Address == 0x53)) {
                return 0x80; // Signaleer aan T-DOS dat de 80-koloms hardware 'aan' staat
            }

            // Blokkeer 0x52 (SGM Read) en alle andere
            // poorten in deze 0x40-0x5F range.
            return idleDataBus;
        }
        else
        {
            // --- Coleco Modus ---
            // SGM AY-3-8910 Sound Chip (Port 0x52 = read data)
            if (Address == 0x52)
            {
                return(ay8910_read());
            }
        }
        break;

    case 0x60: // 0x60 - 0x7F: Memory Control Read
        if (emulator->currentMachineType == MACHINEADAM)
        {
            return(coleco_port60);
        }
        break;

    case 0x80: // 0x80 - 0x9F: VDP Status/Data (Poort 0x99, 0x98 in Coleco)
        if (Address == 0x98 || (Address & 0x01) == 0) // Even addresses: 0x98, 0xA0, 0xA2... (DATA READ)
            return tms9918_readdata();

        // Odd addresses: 0x99, 0xA1, 0xA3... (STATUS READ)
        else
            return tms9918_readctrl();

    case 0xA0: // 0xA0 - 0xBF: VDP Status/Data
        if ((Address & 0x01) == 0) // Even addresses: 0xA0, 0xA2... (DATA READ)
            return tms9918_readdata();
        else // Odd addresses: 0xA1, 0xA3... (STATUS READ)
            return tms9918_readctrl();

    case 0xC0: // 0xC0 - 0xDF: Controller 2 Read
        // In Adam modus, dit bereik is primair voor AdamNet (Host Adapter)
        if (emulator->currentMachineType == MACHINEADAM)
        {
            // Dit is GEEN AdamNet Host Adapter Status poort, val door naar idle.
            // AdamNet I/O status zit in 0xE0-0xE3
            return idleDataBus;
        }
        // In Coleco modus, lees controller 2 (indien nodig)
        break; // Val door naar Controller Read in 0xE0-0xFF gebied (vaak 0xE2/0xE3)

    case 0xE0: // 0xE0 - 0xFF: Controller 1/2, Sound Write
    {

        // In coleco_readport:
        if (Address == 0xFC || Address == 0xFF) {
            return coleco_io_read(Address); // Forceer joystick uitlezing
        }

        if (emulator->currentMachineType == MACHINEADAM)
        {
            // *** CRUCIALE FIX VOOR CP/M ***
            // In ADAM-modus heeft 0xE0-0xE3 HOGERE prioriteit dan de controllers
            // en moet het de status/data van de ADAMNET-randapparatuur teruggeven.
            if (Address >= 0xE0 && Address <= 0xE3)
            {
                // Roept adamnet_read_io aan, die de status leest
                // en de kritieke Data-In Full vlag wist (0x01).
                return adamnet_read_io(Address);
            }
            // Overige E0-FF adressen in Adam-modus vallen door naar idle.
            return idleDataBus;
        }

        // --- Coleco/SGM Controller/Paddle Logica ---
        // Vraag eerst de basis digitale/keypad status op van de keypad-module.
        uint8_t digital_result = coleco_io_read((uint8_t)Address);

        // --- Analoge/Spinner Override (ALLEEN VOOR POORT 0xE0/0xE1 VAN CONTROLLER 1) ---

        if ((Address & 0x02) == 0) // Dit is Controller 1 (Pad 0)
        {
            if (Address == 0xE0)
            {
                // 1. MSB van de 16-bit spinner positie teruggeven (Hoge Byte).
                return (BYTE)(coleco_spinpos[0] >> 8);
            }

            if (Address == 0xE1)
            {
                // 1. LSB van de 16-bit spinner positie (Lage Byte).
                uint8_t spinner_lsb = (uint8_t)(coleco_spinpos[0] & 0xFF);

                // 2. Haal de lage 4 bits (de digitale status) uit het resultaat van coleco_io_read.
                uint8_t digital_status = digital_result & 0x0F;

                // 3. Combineer: gebruik de hoge 4 bits van de LSB (voor de 10-bit paddle)
                // en de lage 4 bits van de digitale status.
                uint8_t paddle_high_bits = (spinner_lsb & 0xF0);

                return paddle_high_bits | digital_status;
            }
        }

        // Als het 0xE2, 0xE3 (Controller 2) of een andere leesactie is,
        // retourneren we het digitale resultaat.
        return digital_result;
    }
    }
    return idleDataBus; // Geen geldige poort
}
//---------------------------------------------------------------------------
int coleco_contend(int /*Address*/, int /*states*/, int time) { return(time); }

//---------------------------------------------------------------------------
// --- Spinner input handler ---
// Deze functie ontvangt de analoge stickwaarde via de Qt slot.
void coleco_setSpinner(int player, int analogValue)
{
    if (player < 0 || player > 1) return;

    // De ruwe analoge waarde wordt al in ib_analog_x1 gezet door de bridge,
    // maar we kunnen dit ook direct gebruiken voor directe pad-connectie.
    // (We kiezen ervoor om ib_analog_x1 te gebruiken in de update-loop
    // voor consistentie met de emulatie-thread).

    // Voor nu: we slaan de waarde direct op in de emulatie-variabelen.
    // Dit overschrijft de lees-cyclus in coleco_do_scanline als je het hier doet.
    // Best is om DIT NIET TE DOEN, en de core dit zelf uit de bridge te laten lezen
    // in de `coleco_do_scanline` of een vergelijkbare periodieke functie.
    (void)analogValue; // Markeer als ongebruikt om warnings te vermijden
    // coleco_spinpos[player] = analogValue; // -> NIET DOEN HIER.
}

//---------------------------------------------------------------------------
// coleco.cpp

void coleco_paddle(void)
{
    static int s_pulse_counter = 0;
    const int PULSE_THRESHOLD = 512;
    const int ANALOG_DEAD_ZONE = 8000;

    const int SPINNER_SCALING_FACTOR = 64;

    if (ib_paddle_mode == 0) {
        s_pulse_counter = 0;
        ib_set_joy1_dir(IB_LEFT, 0);
        ib_set_joy1_dir(IB_RIGHT, 0);
        return;
    }

    const int16_t analogX = ib_analog_x1;
    ib_set_joy1_dir(IB_LEFT, 0);
    ib_set_joy1_dir(IB_RIGHT, 0);

    if (qAbs(analogX) > ANALOG_DEAD_ZONE)
    {
        int movement = analogX / SPINNER_SCALING_FACTOR;
        int absMovement = qAbs(movement);

        if (absMovement > (PULSE_THRESHOLD - 10)) absMovement = PULSE_THRESHOLD - 10;

        s_pulse_counter += absMovement;

        if (s_pulse_counter >= PULSE_THRESHOLD)
        {
            if (movement < 0) {
                ib_set_joy1_dir(IB_RIGHT, 1);
            } else {
                ib_set_joy1_dir(IB_LEFT, 1);
            }
            s_pulse_counter -= PULSE_THRESHOLD;
        }
        coleco_push_direction_from_bridge(0);
    }
    else
    {
        s_pulse_counter = 0;
        coleco_push_direction_from_bridge(0);
    }
}

//---------------------------------------------------------------------------
int coleco_do_scanline(void)
{
    int ts = 0;
    int MaxScanLen = machine.tperscanline;
    if (MaxScanLen <= 0) MaxScanLen = 228;

    int CurScanLine_len = MaxScanLen;
    int tstotal = 0;

     if (!emulator->stop && !emulator->singlestep)
    {
        ts = z80_checknmi(); // NMI check at start of line
        CurScanLine_len -= ts;
        tstotal += ts;

        do {
            DebugUpdate();
            if (emulator->stop || emulator->singlestep)
                break;

            // --- PADDLE/ANALOGE UPDATE ---
            coleco_paddle();

            DEBUG_BRIDGE.setCurrentOpcodeStartPC(Z80.pc.w.l);
            ts = z80_do_opcode();
            CurScanLine_len -= ts;

            frametstates += ts;
            tStatesCount += ts;
            tstotal += ts;

        } while (CurScanLine_len > 0 &&
                 !emulator->stop &&
                 !emulator->singlestep);
    }

    // --- VDP + NMI level logic ---
    // De NMI-logica is nu gescheiden van de Z80-opcode-executie.

    static int nmi_active = 0;

    tms9918_loop();

    const bool vdp_irq_level =
        ((tms.VR[1] & TMS9918_REG1_IRQ) != 0) &&
        ((tms.SR    & TMS9918_STAT_VBLANK) != 0);

    if (!emulator->stop && !emulator->singlestep) {
        if (vdp_irq_level) {
            if (!nmi_active) {
                z80_set_irq_line(INPUT_LINE_NMI, ASSERT_LINE);
                nmi_active = 1;
            }
        } else {
            if (nmi_active) {
                z80_set_irq_line(INPUT_LINE_NMI, CLEAR_LINE);
                nmi_active = 0;
            }
        }
    }

    return tstotal;
}

//---------------------------------------------------------------------------
void Printer(BYTE V) // Dummy Printer functie
{
    // VCL: printviewer->SendPrint(V);
    (void)V; // Markeer als ongebruikt
}

//---------------------------------------------------------------------------
BYTE coleco_savestate(char *filename)
{
    BYTE stateheader[25] = "adamp state\032\1\0\0\0\0\0\0\0\0\0";
    // BYTE *statebuf = NULL; // Gebruik direct schrijven naar bestand
    FILE *fstatefile = NULL;

    // Vul CRC in header
    DWORD crc = emulator->cardcrc;
    stateheader[18] = crc & 0xFF; stateheader[19] = (crc>>8)&0xFF;
    stateheader[20] = (crc>>16)&0xFF; stateheader[21] = (crc>>24)&0xFF;

    fstatefile = fopen(filename,"wb");
    if(!fstatefile) return(0); // VCL: MessageBox

    // Schrijf header
    if (fwrite(stateheader, 1, 24, fstatefile) != 24) { fclose(fstatefile); return(0); } // VCL: MessageBox

    // Schrijf globale variabelen (individueel!)
    fwrite(&coleco_megasize, sizeof(coleco_megasize), 1, fstatefile);
    fwrite(&coleco_megacart, sizeof(coleco_megacart), 1, fstatefile);
    fwrite(&coleco_megabank, sizeof(coleco_megabank), 1, fstatefile);
    // ... voeg hier ALLE relevante globale vars toe ...
    fwrite(&coleco_port20, sizeof(coleco_port20), 1, fstatefile);
    fwrite(&coleco_port60, sizeof(coleco_port60), 1, fstatefile);
    fwrite(&coleco_port53, sizeof(coleco_port53), 1, fstatefile);
    fwrite(&coleco_joymode, sizeof(coleco_joymode), 1, fstatefile);
    fwrite(&coleco_joystat, sizeof(coleco_joystat), 1, fstatefile);
    // ... timers, etc. ...

    // Schrijf CPU state
    fwrite(&Z80, sizeof(Z80), 1, fstatefile);
    // Schrijf VDP state
    fwrite(&tms, sizeof(tms), 1, fstatefile);

    // Schrijf Sound states
    //fwrite(&sn, sizeof(sn), 1, fstatefile);
    //if (emulator->SGM) fwrite(&ay, sizeof(ay), 1, fstatefile);

    // Versie zodat we later kunnen uitbreiden
    uint8_t snd_ver = 1;
    fwrite(&snd_ver, 1, 1, fstatefile);

    int clk = (int)(Clock);              // Coleco PSG clock, ~223721 Hz
    int sr  = SampleRate;                // jouw gekozen sample rate
    int sgm = 0;                         // SGM uitgeschakeld (geen AY-chip hier)

    fwrite(&clk, sizeof(clk), 1, fstatefile);
    fwrite(&sr,  sizeof(sr),  1, fstatefile);
    fwrite(&sgm, sizeof(sgm), 1, fstatefile);

    // NB: we serialiseren geen interne chipregisters. Bij load
    // initialiseren we schoon via sb_init/sb_reset.

    // Schrijf Geheugen
    fwrite(RAM_Memory, 1, MAX_RAM_SIZE*1024, fstatefile);
    fwrite(SRAM_Memory, 1, MAX_EEPROM_SIZE*1024, fstatefile);
    fwrite(VDP_Memory, 1, 0x10000, fstatefile); // Sla VDP memory op

    fclose(fstatefile);
    return(1);
}

static inline bool fread_one(FILE* f, void* dst, size_t elemsz) {
    return std::fread(dst, elemsz, 1, f) == 1;
}

BYTE coleco_loadstate(char *filename)
{
    BYTE stateheader[24];
    // VCL: unsigned int statesave[32]; // Problematisch
    // BYTE *statebuf = NULL; // Lees direct uit bestand
    FILE *fstatefile = NULL;
    //DWORD saved_crc = 0;

    fstatefile = fopen(filename,"rb");
    if(!fstatefile) return(0); // VCL: MessageBox

    if (fread(stateheader, 1, 24, fstatefile) != 24) { fclose(fstatefile); return(0); } // VCL: MessageBox
    if (memcmp(stateheader,"adamp state\032\1\0\0",17) != 0) { fclose(fstatefile); return(0); } // VCL: MessageBox

    // Lees CRC uit header
    //saved_crc = stateheader[18] | (stateheader[19]<<8) | (stateheader[20]<<16) | (stateheader[21]<<24);

    // TODO: Laad ROM hier gebaseerd op state info (emulator->currentrom?) en vergelijk CRC

    // Lees globale variabelen (individueel!)
    if (!fread_one(fstatefile, &coleco_megasize, sizeof(coleco_megasize))) {
        // optioneel: qDebug() of perror voor logging
        std::fclose(fstatefile);
        return 0; // state load failed
    }
    if (!fread_one(fstatefile, &coleco_megacart, sizeof(coleco_megacart))) {
        // optioneel: qDebug() of perror voor logging
        std::fclose(fstatefile);
        return 0; // state load failed
    }
    if (!fread_one(fstatefile, &coleco_megabank, sizeof(coleco_megabank))) {
        // optioneel: qDebug() of perror voor logging
        std::fclose(fstatefile);
        return 0; // state load failed
    }
    // ... lees ALLE relevante globale vars in dezelfde volgorde als save ...
    if (!fread_one(fstatefile, &coleco_port20, sizeof(coleco_port20))) {
        // optioneel: qDebug() of perror voor logging
        std::fclose(fstatefile);
        return 0; // state load failed
    }
    if (!fread_one(fstatefile, &coleco_port60, sizeof(coleco_port60))) {
        // optioneel: qDebug() of perror voor logging
        std::fclose(fstatefile);
        return 0; // state load failed
    }
    if (!fread_one(fstatefile, &coleco_port53, sizeof(coleco_port53))) {
        // optioneel: qDebug() of perror voor logging
        std::fclose(fstatefile);
        return 0; // state load failed
    }
    if (!fread_one(fstatefile, &coleco_joymode, sizeof(coleco_joymode))) {
        // optioneel: qDebug() of perror voor logging
        std::fclose(fstatefile);
        return 0; // state load failed
    }
    if (!fread_one(fstatefile, &coleco_joystat, sizeof(coleco_joystat))) {
        // optioneel: qDebug() of perror voor logging
        std::fclose(fstatefile);
        return 0; // state load failed
    }
    // ... timers, etc. ...


    // Lees CPU state (init eerst!)
    z80_init();
    if (!fread_one(fstatefile, &Z80, sizeof(Z80))) {
        // optioneel: qDebug() of perror voor logging
        std::fclose(fstatefile);
        return 0; // state load failed
    }
    // Lees VDP state
    if (!fread_one(fstatefile, &tms, sizeof(tms))) {
        // optioneel: qDebug() of perror voor logging
        std::fclose(fstatefile);
        return 0; // state load failed
    }

    // Lees Sound states
    //fread(&sn, sizeof(sn), 1, fstatefile);
    //if (emulator->SGM) fread(&ay, sizeof(ay), 1, fstatefile); // Lees alleen AY als SGM actief was
    uint8_t snd_ver = 0;
    if (fread(&snd_ver, 1, 1, fstatefile) != 1) { fclose(fstatefile); return 0; }

    int clk = 0, sr = 0, sgm = 0;
    if (fread(&clk, sizeof(clk), 1, fstatefile) != 1) { fclose(fstatefile); return 0; }
    if (fread(&sr,  sizeof(sr),  1, fstatefile) != 1) { fclose(fstatefile); return 0; }
    if (fread(&sgm, sizeof(sgm), 1, fstatefile) != 1) { fclose(fstatefile); return 0; }

    // --- Sound (PSG) herstellen ---
    if (clk <= 0) clk = (emulator->NTSC ? CLOCK_NTSC : CLOCK_PAL);
    if (sr  <= 0) sr  = SampleRate;

    emulator->SGM = sgm ? 1 : 0;
    // Init onze PSG-bridge met juiste OUT-rate en standaard PSG-clock
    sn76489_init(Clock, SampleRate);
    sn76489_reset(Clock, SampleRate);

    // Lees Geheugen
    fread(RAM_Memory, 1, MAX_RAM_SIZE*1024, fstatefile);
    fread(SRAM_Memory, 1, MAX_EEPROM_SIZE*1024, fstatefile);
    fread(VDP_Memory, 1, 0x10000, fstatefile); // Laad VDP memory

    // Herstel memory map pointers gebaseerd op geladen state (cruciaal!)
    // Moet gebaseerd zijn op geladen coleco_port60, coleco_port20, sgm_enable etc.
    if (emulator->currentMachineType == MACHINEADAM) coleco_setadammemory(false);
    else coleco_setupsgm();
    if (coleco_megacart) megacart_bankswitch(coleco_megabank); // Herstel bank

    fclose(fstatefile);
    return(1);
}


// --- Z80 CPU Callbacks ---
//
extern "C" {

// 8-bit geheugen-callback (voor opcodes)
unsigned int cpu_readmem16(unsigned int address)
{
    return (unsigned int)coleco_readbyte(address);
    // unsigned char value = coleco_readbyte(address);

    // DEBUG_BRIDGE.checkMemAccess(
    //     BreakpointType::BP_READ,
    //     (uint16_t)address,
    //     value
    //     );

    // return (unsigned int)value;
}

// 8-bit geheugen-callback
void cpu_writemem16(unsigned int address, unsigned int value)
{
    // if (((uint16_t)address) == 0x6040) {
    //     qDebug() << "[WM16-HIT-6040] val=" << QString::number(value & 0xFF, 16);
    // }

    // //coleco_writebyte(address, (BYTE)value);
    // DEBUG_BRIDGE.checkMemAccess(
    //     BreakpointType::BP_WRITE,
    //     (uint16_t)address,
    //     (uint8_t)(value & 0xFF));

    coleco_writebyte(address, (BYTE)(value & 0xFF));
}

// 16-bit poort-callback (DEZE VEROORZAAKTE DE FOUT)
unsigned int cpu_readport16(unsigned int port)
{
    // Een Z80 leest nog steeds 8-bits van een 16-bit poortadres
    return (unsigned int)coleco_readport(port, &tstates);
}

// 16-bit poort-callback
void cpu_writeport16(unsigned int port, unsigned int value)
{
    // Een Z80 schrijft nog steeds 8-bits naar een 16-bit poortadres
    coleco_writeport(port, (BYTE)value, &tstates);
}

byte coleco_load_disk(int drive, const char *filename) {
    // ChangeDisk (uit adamnet.cpp) retourneert 1 (succes) of 0 (mislukt)
    // We draaien dit om voor consistentie (0 = succes)
    return ChangeDisk((byte)drive, filename) ? 0 : 1;
}
byte coleco_load_tape(int drive, const char *filename) {

    return ChangeTape((byte)drive, filename) ? 0 : 1;
}

int coleco_save_disk(int drive, const char *filename) {
    return SaveFDI(&Disks[drive], filename, FMT_ADMDSK);
}
int coleco_save_tape(int drive, const char *filename) {
    return SaveFDI(&Tapes[drive], filename, FMT_DDP);
}

bool coleco_check_for_bios_failure();

void coleco_eject_disk(int drive) {
    EjectFDI(&Disks[drive]);
}
void coleco_eject_tape(int drive) {
    EjectFDI(&Tapes[drive]);
}

// 8-bit poort-callback
unsigned char cpu_readport(unsigned int port)
{
    BYTE value = coleco_readport(port, &tstates);
    // BREAKPOINT CHECK: BP_IO_IN
    //if (DEBUG_BRIDGE.checkIOAccess(BreakpointType::BP_IO_IN, port, value,Z80.pc.w.l)) {
    //    z80_exec = 0;
    //}
    return value;
}

// 8-bit poort-callback
void cpu_writeport(unsigned int port, unsigned char value)
{
    // BREAKPOINT CHECK: BP_IO_OUT
    //if (DEBUG_BRIDGE.checkIOAccess(BreakpointType::BP_IO_OUT, port, value,Z80.pc.w.l)) {
    //    z80_exec = 0;
    //}

    coleco_writeport(port, value, &tstates);
}

// Functie die 1 CPU-stap uitvoert (verondersteld een Z80.c wrapper)
int coleco_cpu_execute_one_step() {
    DEBUG_BRIDGE.setCurrentOpcodeStartPC(Z80.pc.w.l);
    // 1. EXECUTE Breakpoint Check (VOOR de instructie)
    if (DEBUG_BRIDGE.checkExecute(Z80.pc.w.l)) {
        z80_exec = 0; // Stop de CPU
        return 0; // 0 cycles uitgevoerd
    }

    // Voer de Z80 instructie uit
    int cycles = z80_do_opcode(); // Dit is de bestaande Z80 executie

    // 2. POST-EXECUTION Breakpoint Check (NA de instructie)
    // Controleert BP_CLOCK, BP_FLAG_VAL, BP_REG_VAL, BP_MEM_VAL
    if (DEBUG_BRIDGE.checkPostExecutionBreakpoints()) {
        z80_exec = 0; // Stop de CPU
    }

    return cycles;
}

// 8-bit geheugen-callback (READ)
unsigned char cpu_readbyte(unsigned int address)
{
    BYTE value = coleco_getbyte(address);
    return value;
}

// 8-bit geheugen-callback (WRITE)
// BREAKPOINT MEM
void cpu_writebyte(unsigned int address, unsigned char value)
{
    coleco_writebyte(address, (BYTE)value);
}

extern "C" void coleco_set_bios_paths(const char* coleco_path, const char* eos_path, const char* writer_path)
{
    // ... (de implementatie met s_external_* pointers)
    s_external_coleco_bios_path = coleco_path;
    s_external_eos_bios_path = eos_path;
    s_external_writer_bios_path = writer_path;
}

int coleco_get_bios_status(int index)
{
    if (index < 0 || index > 2)
        return 0;
    return g_bios_status_int[index];
}

} // extern "C"

// Zorg dat deze signatures exact matchen met z80.h
extern "C" {
extern unsigned int cpu_readmem16(unsigned int address);
extern void cpu_writemem16(unsigned int address, unsigned int value);
}

// Hulpfunctie voor adres normalisatie
static uint16_t normalize_coleco_address(uint16_t address) {
    if (emulator && emulator->currentMachineType != MACHINEADAM &&
        address >= 0x6000 && address < 0x8000) {
        return 0x6000 + (address & 0x03FF);
    }
    return address;
}

// Deze functies doen NU niets anders dan de debugger checken
// en daarna de originele emulator-functie aanroepen.

extern "C" void z80_wrapper_write(unsigned int address, unsigned char value)
{
    // 1. Debug Check
    uint16_t checkAddr = normalize_coleco_address(address);
    if (DEBUG_BRIDGE.checkMemAccess(BreakpointType::BP_WRITE, checkAddr, value)) {
        if (emulator) emulator->stop = 1;
        extern int z80_exec;
        z80_exec = 0;
    }

    // 2. Originele Schrijfactie (Veilig: checkt intern op ROM/RAM)
    cpu_writemem16(address, value);
}

extern "C" unsigned char z80_wrapper_read(unsigned int address)
{
    // 1. VOER DE NORMALE LEESACTIE UIT (Herstelt het zwarte beeld)
    // We roepen de emulator functie aan. Dit zorgt dat BIOS ROMs correct worden gelezen.
    // Dit crasht niet, zolang we het maar 1x doen en niet recursief.
    unsigned char value = (unsigned char)cpu_readmem16(address);

    // --- Debug Bridge (standaard) ---
    uint16_t checkAddr = normalize_coleco_address(address);
    if (DEBUG_BRIDGE.checkMemAccess(BreakpointType::BP_READ, checkAddr, value)) {
        if (emulator) emulator->stop = 1;
        extern int z80_exec;
        z80_exec = 0;
    }
    return value;
}

extern "C" int coleco_virtual_cpm_diskboot(const char* cpmTapeDdpPath,
                                           const char* diskDskPath,
                                           int tapeDrive,
                                           int diskDrive)
{
    if (tapeDrive < 0) tapeDrive = 0;
    if (diskDrive < 0) diskDrive = 0;

    // 1) Tape CP/M image laden (optioneel, maar voor jouw use-case is dit net de magie)
    if (cpmTapeDdpPath && *cpmTapeDdpPath)
    {
        // ChangeTape retourneert 1 bij succes, 0 bij fout
        if (!ChangeTape((byte)tapeDrive, cpmTapeDdpPath))
        {
            qDebug().noquote() << QString("[CPM][VBOOT] Failed to load CP/M tape image: %1")
            .arg(cpmTapeDdpPath);
            return 1;
        }
    }

    // 2) Disk image laden/mounten (dit wordt straks A:)
    if (diskDskPath && *diskDskPath)
    {
        // ChangeDisk retourneert 1 bij succes, 0 bij fout
        if (!ChangeDisk((byte)diskDrive, diskDskPath))
        {
            qDebug().noquote() << QString("[CPM][VBOOT] Failed to load disk image: %1")
            .arg(diskDskPath);
            return 2;
        }
    }

    // 4) Hard reset zodat de bestaande CP/M boot flow (die bij jou al werkt) afgaat
    coleco_reset();

    qDebug() << "[CPM][VBOOT] Virtual disk boot armed:";
    qDebug() << "  tapeDrive=" << tapeDrive << " tape=" << (cpmTapeDdpPath ? cpmTapeDdpPath : "(null)");
    qDebug() << "  diskDrive=" << diskDrive << " disk=" << (diskDskPath ? diskDskPath : "(null)");
    return 0;
}
