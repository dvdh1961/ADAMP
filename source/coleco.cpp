/* EmulTwo  - A Windows ColecoVision emulator.
 * Copyright (C) 2014-2023 Alekmaul
 * ... (licentie header blijft hetzelfde) ...
 */

// #include <vcl.h> // VCL: Verwijderd
#include <cstdio>  // Nodig voor FILE operaties
#include <cstdlib> // Nodig voor rand(), malloc, free
#include <cstring> // Nodig voor memset, memcpy, strcmp, memcmp
#include <QDebug>

#include "coleco.h"
#include "utils.h" // Bevat CRC32Block en pad functies

#include "z80.h"
#include "f18a.h"
#include "f18agpu.h"
#include "psg_bridge.h"
#include "tms9928a.h"
#include "c24xx.h"    // Nodig voor EEPROM/SRAM
#include "adamnet.h"  // Nodig voor AdamNet functies (PCB)
#include "keypad.h"
#include "ay8910.h"

#include "bios_coleco.h"
#include "bios_adam.h"

// BIOS loader prototype
static int loadBios(const char *filename, BYTE *memory, int sizerm);

static inline uint8_t AL(uint8_t v) { return (uint8_t)~v; } // active-low helper


// VCL: #include "printviewer_.h"
// VCL: #include "soundviewer_.h"
// VCL: #include "kbstatus_.h"

// BIOS data komt nu uit colecobios.c en adambios.c (gedeclareerd in coleco.h)

// Dummy DebugUpdate functie
// extern void DebugUpdate(void); // VCL: Afhankelijkheid verwijderd
void DebugUpdate(void) { /* Doe niets */ }

//---------------------------------------------------------------------------
// Globale variabelen (definities)
BYTE cv_display[TVW_F18A*TVH_F18A];
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

FDIDisk Disks[MAX_DISKS] = { 0 };
FDIDisk Tapes[MAX_TAPES] = { 0 };

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

// Get tms vram adress
unsigned short coleco_gettmsaddr(BYTE whichaddr, BYTE mode, BYTE y)
{
    unsigned short result = 0; // Initialiseer

    switch (whichaddr)
    {
    case CHRMAP:
        result = emul2->F18A ? f18a.ChrTab : (unsigned short)(tms.ChrTab-VDP_Memory); // Cast naar ushort
        break;
    case CHRGEN:
        result = emul2->F18A ? f18a.ChrGen : (unsigned short)(tms.ChrGen-VDP_Memory); // Cast naar ushort
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
        result = emul2->F18A ? f18a.ColTab : (unsigned short)(tms.ColTab-VDP_Memory); // Cast naar ushort
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
        result = emul2->F18A ? f18a.SprTab : (unsigned short)(tms.SprTab-VDP_Memory); // Cast naar ushort
        break;
    case SPRGEN:
        result = emul2->F18A ? f18a.SprGen : (unsigned short)(tms.SprGen-VDP_Memory); // Cast naar ushort
        break;
    case VRAM:
        result = 0;
        break;
    case CHRMAP2:
        result = f18a.ChrTab2; // Alleen relevant voor F18A
        break;
    case CHRCOL2:
        result = f18a.ColTab2; // Alleen relevant voor F18A
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
        base_addr = emul2->F18A ? f18a.ChrTab : (unsigned short)(tms.ChrTab-VDP_Memory);
        result = VDP_Memory[base_addr + addr];
        break;
    case CHRGEN:
        base_addr = emul2->F18A ? f18a.ChrGen : (unsigned short)(tms.ChrGen-VDP_Memory);
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
        base_addr = emul2->F18A ? f18a.ColTab : (unsigned short)(tms.ColTab-VDP_Memory);
        if (!emul2->F18A) {
            switch(mode) {
            case 0: case 1: addr>>=3; break; // Correctie: delen door 8 voor mode 0/1? Origineel was 6
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
        }
        result = VDP_Memory[base_addr + addr];
        break;
    case SPRATTR:
        base_addr = emul2->F18A ? f18a.SprTab : (unsigned short)(tms.SprTab-VDP_Memory);
        result = VDP_Memory[base_addr + addr];
        break;
    case SPRGEN:
        base_addr = emul2->F18A ? f18a.SprGen : (unsigned short)(tms.SprGen-VDP_Memory);
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
static inline void coleco_audio_init()
{
    // Voor Coleco/ADAM: PSG clock ≈ 3579545 / 16 Hz
    sn76489_init(Clock, SampleRate);
    sn76489_reset(Clock, SampleRate);
}

//---------------------------------------------------------------------------
// Load a rom file
BYTE coleco_loadcart(char *filename)
{
    long size; // Gebruik long voor ftell resultaat
    int adrlastbank, j;
    BYTE  *p;
    BYTE retf = ROM_LOAD_FAIL;
    FILE *fRomfile = NULL;

    fRomfile = fopen(filename, "rb");
    if (fRomfile == NULL) return(retf);

    memset(ROM_Memory, 0xFF, (MAX_CART_SIZE * 1024));

    fseek(fRomfile, 0, SEEK_END);
    size = ftell(fRomfile);
    if (size == -1L) { // Controleer op fout bij ftell
        fclose(fRomfile);
        return retf;
    }

    if (size <= (MAX_CART_SIZE * 1024))
    {
        fseek(fRomfile, 0, SEEK_SET);
        if (fread((void*) ROM_Memory, 1, size, fRomfile) != (size_t)size) { // Controleer returnwaarde fread
            fclose(fRomfile);
            return retf;
        }
        // fclose(fRomfile); // Sluit hier al? Nee, pas na controle header

        coleco_megacart = 0x00;
        coleco_megasize = 2;

        emul2->cardsize = (DWORD)size; // Cast size naar DWORD
        emul2->cardcrc = CRC32Block(ROM_Memory, emul2->cardsize);

        p = (ROM_Memory[0]==0x55)&&(ROM_Memory[1]==0xAA)? ROM_Memory // Check ROM_Memory, niet RAM_Memory
            : (ROM_Memory[0]==0xAA)&&(ROM_Memory[1]==0x55)? ROM_Memory
            : (ROM_Memory[0]==0x66)&&(ROM_Memory[1]==0x99)? ROM_Memory
                                                                 : NULL;

        adrlastbank = (size&~0x3FFF)-0x4000;
        if (adrlastbank < 0) adrlastbank = 0; // Voorkom negatieve index

        if (p==NULL && size > 0x4000) // Controleer alleen laatste bank als ROM groot genoeg is
        {
            p = (ROM_Memory[adrlastbank]==0x55)&&(ROM_Memory[adrlastbank+1]==0xAA)? ROM_Memory
                : (ROM_Memory[adrlastbank]==0xAA)&&(ROM_Memory[adrlastbank+1]==0x55)? ROM_Memory
                                                                                             : NULL;
        }

        if (p==NULL) { fclose(fRomfile); return(ROM_VERIFY_FAIL); }

        // Point naar de Z80 memory map locatie voor de cartridge
        p = RAM_Memory+0x8000; // Dit lijkt nog steeds VCL logica? Waar mapt de Z80 echt?
        // Aanname: Z80 ziet RAM array direct

        if (size <= 32*1024)
        {
            // De data is al geladen in ROM_Memory[0...size]
            // De pointers in MemoryMap[4-7] zijn al correct ingesteld
            // door coleco_reset(). We hoeven hier niets te doen.
        }
        else // Mega Cart
        {
            coleco_megabank = 199; // Forceer bankswitch bij start
            size = ((size+0x3FFF)&~0x3FFF); // Pad naar 16K grens
            for(j=2; (j<<14)<size; j<<=1); // Vind power of 2 voor aantal 16K banks
            coleco_megasize = j;

            if (coleco_megasize == 4) coleco_megacart = 0x03;      // 64K
            else if (coleco_megasize == 8) coleco_megacart = 0x07; // 128K
            else if (coleco_megasize == 16) coleco_megacart = 0x0F;// 256K
            else coleco_megacart = 0x1F;                           // 512K

            // Map laatste 16K bank (fixed) naar 0xC000-0xFFFF
            MemoryMap[6] = ROM_Memory + ((coleco_megasize-1)<<14);
            MemoryMap[7] = MemoryMap[6] + 0x2000;
            // Map eerste 16K bank (switched) naar 0x8000-0xBFFF initieel
            MemoryMap[4] = ROM_Memory;
            MemoryMap[5] = MemoryMap[4] + 0x2000;
            megacart_bankswitch(0); // Roep bankswitch aan om correct te mappen
        }
    }
    else
    {
        fclose(fRomfile); return(retf);
    }

    emul2->romCartridgeType = coleco_megacart ? ROMCARTRIDGEMEGA : ROMCARTRIDGESTD;

    fclose(fRomfile); // Sluit het bestand hier

    return ROM_LOAD_PASS; // Gebruik OK of PASS
}

//---------------------------------------------------------------------------
// update the 16 colors Coleco
void coleco_setpalette(int palette) {
    int index, idxpal;

    if (emul2->F18A==0) { // Gebruik bool direct
        idxpal=palette*3*16;
        for (index=0;index<16*3;index+=3) {
            cv_palette[index] = TMS9918A_palette[idxpal+index];
            cv_palette[index+1] = TMS9918A_palette[idxpal+index+1];
            cv_palette[index+2] = TMS9918A_palette[idxpal+index+2];
        }
        RenderCalcPalette(cv_palette,16);
    }
}

// 0 = Coleco/Phoenix, 1 = ADAM
void coleco_set_machine_type(int isAdam)
{
    // EmulTwo gebruikt emul2->currentMachineType en checkt overal tegen MACHINEADAM.
    // Elke waarde ≠ MACHINEADAM wordt als "Coleco" behandeld.
    // We zetten expliciet naar MACHINEADAM of naar 0 (Coleco).
    if (isAdam) {
        emul2->currentMachineType = MACHINEADAM;
    } else {
        emul2->currentMachineType = 0; // Coleco (elke niet-MACHINEADAM is Coleco)
    }
    // Let op: géén reset hier — bij opstart wil je dit vóór coleco_initialise() zetten.
    // Bij runtime switch doen we hard reset via de controller (zie hieronder).
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
    // TODO: Voeg eventueel extra F18A palette berekeningen toe indien nodig
}
//---------------------------------------------------------------------------
void coleco_setadammemory(bool resetAdamNet)
{
    if (emul2->currentMachineType != MACHINEADAM) return;

    // ... (Logica voor MemoryMap blijft hetzelfde) ...
    // Configure lower 32K of memory
    if ((coleco_port60 & 0x03) == 0x00) // WRITER/EOS ROM
    {
        adam_ram_lo = 0; adam_ram_lo_exp = 0;
        MemoryMap[0] = BIOS_Memory + 0x0000; MemoryMap[1] = BIOS_Memory + 0x2000;
        MemoryMap[2] = BIOS_Memory + 0x4000;
        MemoryMap[3] = (coleco_port20 & 0x02) ? BIOS_Memory + 0x8000 : BIOS_Memory + 0x6000; // EOS or WRITER
    }
    else if ((coleco_port60 & 0x03) == 0x01) // Onboard RAM
    {
        adam_ram_lo = 1; adam_ram_lo_exp = 0;
        MemoryMap[0] = RAM_Memory + 0x0000; MemoryMap[1] = RAM_Memory + 0x2000;
        MemoryMap[2] = RAM_Memory + 0x4000; MemoryMap[3] = RAM_Memory + 0x6000;
    }
    else if ((coleco_port60 & 0x03) == 0x03) // Coleco BIOS + RAM
    {
        adam_ram_lo = 1; adam_ram_lo_exp = 0;
        MemoryMap[0] = BIOS_Memory + 0xA000; MemoryMap[1] = RAM_Memory + 0x2000;
        MemoryMap[2] = RAM_Memory + 0x4000; MemoryMap[3] = RAM_Memory + 0x6000;
    }
    else // Expanded RAM
    {
        adam_128k_mode = 1; adam_ram_lo = 0; adam_ram_lo_exp = 1;
        MemoryMap[0] = RAM_Memory + 0x10000; MemoryMap[1] = RAM_Memory + 0x12000;
        MemoryMap[2] = RAM_Memory + 0x14000; MemoryMap[3] = RAM_Memory + 0x16000;
    }

    // Configure upper 32K of memory
    if ((coleco_port60 & 0x0C) == 0x00) // Onboard RAM
    {
        adam_ram_hi = 1; adam_ram_hi_exp = 0;
        MemoryMap[4] = RAM_Memory + 0x8000; MemoryMap[5] = RAM_Memory + 0xA000;
        MemoryMap[6] = RAM_Memory + 0xC000; MemoryMap[7] = RAM_Memory + 0xE000;
    }
    else if ((coleco_port60 & 0x0C) == 0x08) // Expanded RAM
    {
        adam_128k_mode = 1; adam_ram_hi = 0; adam_ram_hi_exp = 1;
        MemoryMap[4] = RAM_Memory + 0x18000; MemoryMap[5] = RAM_Memory + 0x1A000;
        MemoryMap[6] = RAM_Memory + 0x1C000; MemoryMap[7] = RAM_Memory + 0x1E000;
    }
    else // Cartridge or Expansion ROM (niet direct schrijfbaar naar RAM)
    {
        adam_ram_hi = 0; adam_ram_hi_exp = 0;
        // MemoryMap[4] t/m [7] worden niet aangepast, blijven ROM/Cartridge wijzen
    }


    if (resetAdamNet)  ResetPCB(); // Nodig #include "adamnet.h"
}
// --------------------------------------------------------------------------

void coleco_setupsgm(void)
{
    if (sgm_neverenable) return;
    if (emul2->currentMachineType == MACHINEADAM) return;

    sgm_enable = (coleco_port53 & 0x01) ? 1:0;

    if (sgm_enable && sgm_firstwrite)
    {
        memset(RAM_Memory+0x2000, 0x00, 0x6000); // 24K RAM clearen
        sgm_firstwrite = 0;
    }


    if (coleco_port60 & 0x02) {
        // BIOS op 0x0000
        if (sgm_low_addr != 0xFFFF) {
            sgm_low_addr = 0xFFFF;
            MemoryMap[0] = BIOS_Memory + 0x0000;
        }
    } else {
        // In Coleco-mode blijft 0x0000 BIOS, ook als bit1=0
        if (sgm_low_addr != 0xFFFF) {
            sgm_low_addr = 0xFFFF;
            MemoryMap[0] = BIOS_Memory + 0x0000;
        }
    }


    // De rest van het RAM (0x2000-0x7FFF) wordt altijd gemapt naar RAM als SGM enabled is
    if (sgm_enable) {
        MemoryMap[1] = RAM_Memory + 0x2000;
        MemoryMap[2] = RAM_Memory + 0x4000;
        MemoryMap[3] = RAM_Memory + 0x6000;
    } else {
        // Indien SGM niet enabled is, alleen 1K RAM + spiegels
        MemoryMap[1] = RAM_Memory + 0x6000; // Map 0x2000 naar RAM base
        MemoryMap[2] = RAM_Memory + 0x6000; // Map 0x4000 naar RAM base
        MemoryMap[3] = RAM_Memory + 0x6000; // Map 0x6000 naar RAM base
        // We moeten hier eigenlijk de mirroring van 0x6000-0x63FF regelen via read/write functies
    }

}

//---------------------------------------------------------------------------

void coleco_reset(void)
{
    int i;

    // Reset Memory Map naar standaard Coleco (BIOS + 1K RAM + Cart)
    MemoryMap[0] = BIOS_Memory + 0x0000; // BIOS
    MemoryMap[1] = RAM_Memory + 0x6000;  // Begin van 1K RAM (gespiegeld)
    MemoryMap[2] = RAM_Memory + 0x6000;  // Spiegel
    MemoryMap[3] = RAM_Memory + 0x6000;  // Spiegel
    MemoryMap[4] = ROM_Memory + 0x0000;
    MemoryMap[5] = ROM_Memory + 0x2000;
    MemoryMap[6] = ROM_Memory + 0x4000;
    MemoryMap[7] = ROM_Memory + 0x6000;

    // BIOS laden is verplaatst naar coleco_initialise

    // Clear RAM (1K standaard, 32K SGM, 64/128K ADAM)
    if (emul2->currentMachineType == MACHINEADAM) {
        memset(RAM_Memory, 0, (adam_128k_mode ? 128 : 64) * 1024);
    } else if (emul2->SGM) { // Gebruik bool direct
        memset(RAM_Memory, 0, 32 * 1024);
    } else {
        // Alleen de 1K RAM clearen
        for(i=0; i<0x400; ++i) RAM_Memory[0x6000+i] = rand()%256;
    }

    // SGM/ADAM specifieke reset
    sgm_enable = 0;
    sgm_firstwrite = 1;
    sgm_low_addr = 0xFFFF; // Markeer als BIOS initieel
    sgm_neverenable = 0;
    coleco_port53 = 0x00;
    coleco_port60 = ((emul2->currentMachineType == MACHINEADAM) ? 0x00 : 0x0F); // Adam start met ROM, Coleco met BIOS+RAM
    coleco_port20 = 0x00;

    if (emul2->currentMachineType == MACHINEADAM)
    {
        adam_ram_lo=adam_ram_hi=adam_ram_lo_exp=adam_ram_hi_exp=0;
        adam_128k_mode=0;
        coleco_setadammemory(true); // Reset AdamNet ook
    }
    else // Coleco mode setup
    {
        coleco_setupsgm(); // Stel SGM in op basis van poorten (nu default uit)
        // Memory map is al ingesteld voor standaard Coleco
    }


    // Cartridge specifieke setup
    switch (emul2->cardcrc)
    {
    case 0x62DACF07: emul2->typebackup=EEP24C256; break;
    case 0xDDDD1396: emul2->typebackup=EEP24C08; break;
    case 0xFEE15196: case 0x1053F610: case 0x60D6FD7D: case 0x37A9F237:
        emul2->typebackup=EEPSRAM;
        // Map SRAM naar E000-E7FF? MemoryMap[7] is al RAM+0xE000, maar wijst naar Cartridge.
        // Dit moet waarschijnlijk via de read/write functies.
        break;
    case 0xEF25AF90: case 0xC2E7F0E0: sgm_neverenable=1; break;
    default: emul2->typebackup = NOBACKUP; break; // Zet default op NOBACKUP
    }

    // Reset hardware
    if (emul2->F18A) f18a_reset(); else tms9918_reset(); // Gebruik F18A of TMS reset
    tms.ScanLines = emul2->NTSC ? TMS9918_LINES : TMS9929_LINES;
    if (emul2->F18A && f18a.Row30) tms.ScanLines += 27; // Pas aan voor F18A Row30 mode

    sn76489_init(Clock, SampleRate);
    sn76489_reset(Clock, SampleRate);

    // VCL: Sound->SoundPrepSmpTab(tms.ScanLines);
    if (emul2->typebackup != NOBACKUP && emul2->typebackup != EEPSRAM) { // Reset EEPROM, niet SRAM
        c24xx_reset(SRAM_Memory, emul2->typebackup==EEP24C08 ? C24XX_24C08 : C24XX_24C256); // C24XX types direct gebruiken
    } else if (emul2->typebackup == EEPSRAM) {
        // SRAM hoeft niet gereset te worden zoals EEPROM
    }

    z80_reset();

    tStatesCount = 0;
    frametstates = 0; // Reset frameteller ook

    coleco_joymode=0;
    coleco_joystat=0x00000000;
    coleco_spinpos[0]=coleco_spinpos[1]=0;
    coleco_spinrecur[0]=coleco_spinrecur[1]=0;
    coleco_spinparam[0]=coleco_spinparam[1]=0;
    coleco_spinstate[0]=coleco_spinstate[1]=0;
    // VCL gerelateerde controller flags:
    // coleco_steerwheel=machine.steerwheel ? 1 : 0;
    // coleco_rollercontrol=machine.rollercontrol ? 1 : 0;
    // coleco_superaction=machine.superaction ? 1 : 0;
}
//---------------------------------------------------------------------------

void coleco_reset_and_restart_bios()
{
    // 1) Defaults per machine
    if (emul2->currentMachineType == MACHINEADAM) {
        // ADAM: geen SGM; memory wordt door 0x60 bits gestuurd
        emul2->SGM = false;
        coleco_port53 = 0x00;
        coleco_writeport(0x53, coleco_port53, nullptr);

        // ADAM default memorycontrol: WRITER/EOS + cart in hoge 32K
        // (pas aan indien jouw setadammemory iets anders verwacht)
        coleco_port60 = 0x00;
        coleco_writeport(0x60, coleco_port60, nullptr);

        // 2) Bouw ADAM-mapping op
        coleco_setadammemory(/*resetAdamNet=*/true);
    } else {
        // Coleco/Phoenix: standaard BIOS+cart en SGM volgens emul2->SGM
        coleco_port60 = 0x0F;
        coleco_writeport(0x60, coleco_port60, nullptr);

        coleco_port53 = emul2->SGM ? 0x01 : 0x00;
        coleco_writeport(0x53, coleco_port53, nullptr);

        // 2) Bouw Coleco-mapping op
        coleco_setupsgm();
    }

    // 3) Cartridge pages voor de zekerheid opnieuw
    MemoryMap[4] = ROM_Memory + 0x0000;
    MemoryMap[5] = ROM_Memory + 0x2000;
    MemoryMap[6] = ROM_Memory + 0x4000;
    MemoryMap[7] = ROM_Memory + 0x6000;
}
//---------------------------------------------------------------------------
// coleco.h:  extern void coleco_hardreset(void);

void coleco_hardreset(void)
{
    // 1) Maak de cartbuffer “open bus”: 0xFF
    memset(ROM_Memory, 0xFF, MAX_CART_SIZE * 1024);   // 512 KiB max. cartsize

    // 2) Reset megacart/bankswitch state
    coleco_megacart = 0;
    coleco_megasize = 2;   // standaard 32 KiB mapping (veilig default)
    coleco_megabank = 0;

    // 3) (Optioneel) reset UI/state info als je ze hebt
    //    (comment weg als je die velden niet in jouw build hebt)
    // emul2->cardsize          = 0;
    // emul2->cardcrc           = 0;
    // emul2->romCartridgeType  = ROMCARTRIDGENONE;

    // 4) Re-map ROM-gebied (0x8000-0xFFFF) naar onze (lege) ROM_Memory
    //    Slots 4..7 zijn respectievelijk 0x8000, 0xA000, 0xC000, 0xE000
    MemoryMap[4] = ROM_Memory + 0x0000;
    MemoryMap[5] = ROM_Memory + 0x2000;
    MemoryMap[6] = ROM_Memory + 0x4000;
    MemoryMap[7] = ROM_Memory + 0x6000;

    // 5) (Aanrader) CPU en VDP netjes resetten zodat BIOS meteen beeld kan geven
    //    en de jump niet in “oude” cartcode terechtkomt.
    //    Als je “BIOS only” wil laten draaien:
    // coleco_reset_and_restart_bios();  // zet BIOS op 0x0000 + reset VDP/CPU/PSG
}

//---------------------------------------------------------------------------

// Helper for loading BIOS files (minimal version without VCL)
int loadBios(const char *filename, BYTE *memory, int sizerm)
{
    FILE *fbios = fopen(filename,"rb");
    if (!fbios) return 0;

    size_t bytes_read = fread((void*)memory, 1, sizerm, fbios);
    fclose(fbios);

    return (bytes_read == (size_t)sizerm); // Return 1 bij succes
}

//---------------------------------------------------------------------------

void coleco_initialise(void)
{
    int i;

    z80_init();
    tStatesCount = 0;

    coleco_megasize = 2;
    coleco_megacart = 0;
    emul2->romCartridgeType = ROMCARTRIDGENONE; // Gebruik Type enum

    if (emul2->F18A) f18agpu_init(); // Init GPU alleen als F18A actief is

    memset(ROM_Memory,0xFF,MAX_CART_SIZE * 1024);
    memset(RAM_Memory,0xFF,MAX_RAM_SIZE * 1024);
    memset(BIOS_Memory, 0xFF, MAX_BIOS_SIZE * 1024); // Init BIOS memory ook

    if (emul2->currentMachineType == MACHINEADAM)
    {
        bool bios_ok = true;
        if (strcmp(emul2->colecobios,"Internal") != 0) {
            if (!loadBios(emul2->colecobios, BIOS_Memory+0xA000, 0x2000)) bios_ok = false;
        } else {
            memcpy(BIOS_Memory+0xA000, colecobios_rom, 0x2000);
        }
        if (strcmp(emul2->adameos,"Internal") != 0) {
            if (!loadBios(emul2->adameos, BIOS_Memory+0x8000, 0x2000)) bios_ok = false;
        } else {
            memcpy(BIOS_Memory+0x8000, adambios_eos,0x2000);
        }
        if (strcmp(emul2->adamwriter,"Internal") != 0) {
            if (!loadBios(emul2->adamwriter, BIOS_Memory, 0x8000)) bios_ok = false;
        } else {
            memcpy(BIOS_Memory+0x0000, adambios_writer, 0x8000);
        }
        if (!bios_ok) {
            // VCL: Application->MessageBox("Error loading one or more ADAM BIOS files. Using internal defaults.", "BIOS Error", MB_OK | MB_ICONWARNING);
            // Laad defaults als backup
            memcpy(BIOS_Memory+0xA000, colecobios_rom, 0x2000);
            memcpy(BIOS_Memory+0x8000, adambios_eos,0x2000);
            memcpy(BIOS_Memory+0x0000, adambios_writer, 0x8000);
        }
    }
    else // ColecoVision
    {
        if (strcmp(emul2->colecobios,"Internal") != 0) {
            if (!loadBios(emul2->colecobios, BIOS_Memory, 0x2000)) {
                // VCL: Application->MessageBox("Can't open coleco bios, load default","Error", MB_OK | MB_ICONERROR);
                memcpy(BIOS_Memory, colecobios_rom, 0x2000);
            }
        } else {
            memcpy(BIOS_Memory, colecobios_rom, 0x2000);
        }

        // Kopieer BIOS ook naar RAM locatie voor hacks (zal overschreven worden indien SGM RAM actief)
        memcpy(RAM_Memory, BIOS_Memory, 0x2000);

        // Pas hacks toe op RAM kopie
        RAM_Memory[0x0069] = emul2->hackbiospal ? 50 : 60;
        if (emul2->biosnodelay) {
            RAM_Memory[0x1F51] = 0x00; // NOPs over delay loop
            RAM_Memory[0x1F52] = 0x00;
            RAM_Memory[0x1F53] = 0x00;
        }
    }

    for(i=0;i<MAX_DISKS;i++) EjectFDI(&Disks[i]);
    for(i=0;i<MAX_TAPES;i++) EjectFDI(&Tapes[i]);

    coleco_reset();
    coleco_setpalette(emul2->palette);
}
//---------------------------------------------------------------------------

void megacart_bankswitch(BYTE bank)
{
    bank &= coleco_megacart; // Maskeer met grootte
    if (coleco_megabank != bank)
    {
        // Alleen 0x8000 - 0xBFFF wordt geswitched
        MemoryMap[4] = ROM_Memory + ((unsigned int) bank * 0x4000);
        MemoryMap[5] = MemoryMap[4] + 0x2000;
        coleco_megabank = bank;
    }
}
//---------------------------------------------------------------------------

void coleco_WriteByte(unsigned int Address, int Data)
{
    // ADAM Memory Handling
    if(emul2->currentMachineType == MACHINEADAM)
    {
        // Check of huidig gemapt segment schrijfbaar RAM is
        if ((Address < 0x8000 && adam_ram_lo) || (Address >= 0x8000 && adam_ram_hi)) {
            RAM_Memory[Address] = Data;
        } else if ((Address < 0x8000 && adam_ram_lo_exp) || (Address >= 0x8000 && adam_ram_hi_exp)) {
            RAM_Memory[0x10000 + Address] = Data; // Expanded RAM
        }
        // ADAMNet check (alleen als RAM is gemapt op die locatie)
        if (PCBTable[Address] && ((Address < 0x8000 && adam_ram_lo) || (Address >= 0x8000 && adam_ram_hi))) {
            WritePCB(Address, Data);
        }
        return;
    }
    // ColecoVision Memory Handling
    else
    {
        // SGM RAM (0x2000-0x7FFF) of Standaard RAM (0x6000-0x7FFF gespiegeld)
        if (sgm_enable) {
            if ((Address >= sgm_low_addr) && (Address < 0x8000)) RAM_Memory[Address]=Data;
            return;
        } else if((Address >= 0x6000) && (Address < 0x8000)) {
            // Standaard 1K RAM met spiegels
            RAM_Memory[0x6000 + (Address & 0x03FF)] = Data;
            return;
        }

        // Cartridge / IO ruimte
        if (Address >= 0xE000) {

            // SRAM write (E000-E7FF schrijft naar E800-EFFF)
            // if (emul2->typebackup == EEPSRAM && Address < 0xE800) {
            //     RAM_Memory[Address + 0x800] = Data; // Map naar E800-EFFF
            //     return;
            // }
            // writes
            if (emul2->typebackup == EEPSRAM) {
                if (Address >= 0xE000 && Address < 0xE800) { RAM_Memory[Address + 0x800] = Data; return; }
                if (Address >= 0xE800 && Address < 0xF000) { RAM_Memory[Address] = Data; return; }
            }

            // EEPROM Control Lines (FFC0-FFF0)
            if (emul2->typebackup == EEP24C08 || emul2->typebackup == EEP24C256) {
                switch(Address & 0xFFF0) { // Maskeer laatste 4 bits
                case 0xFFC0: c24xx_write(c24.Pins & ~C24XX_SCL); return;
                case 0xFFD0: c24xx_write(c24.Pins | C24XX_SCL); return;
                case 0xFFE0: c24xx_write(c24.Pins & ~C24XX_SDA); return;
                case 0xFFF0: c24xx_write(c24.Pins | C24XX_SDA); return;
                }
            }
            // MegaCart Bankswitch Hotspots (FFC0-FFFF)
            if (coleco_megacart && Address >= 0xFFC0) {
                megacart_bankswitch(Address & 0x1F); // Gebruik onderste 5 bits
                return;
            }
        }
    }
    // VCL: if (machine.HaltWriteRom) { /* WarningForm call */ }
}

void coleco_setbyte(int Address, int Data) { coleco_WriteByte(Address, Data); } // Debugger
void coleco_writebyte(unsigned int Address, int Data) { // Vanuit Z80
    lastMemoryWriteAddrLo = lastMemoryWriteAddrHi; lastMemoryWriteAddrHi = Address;
    lastMemoryWriteValueLo = lastMemoryWriteValueHi; lastMemoryWriteValueHi = Data;
    coleco_WriteByte(Address, Data);
}

BYTE coleco_ReadByte(int Address) // Interne leesfunctie
{
    // MegaCart Bankswitch Hotspots (lezen geeft huidige bank terug)
    if (coleco_megacart && Address >= 0xFFC0) {
        megacart_bankswitch(Address & 0x1F); // Gebruik onderste 5 bits
        return coleco_megabank;
    }
    // EEPROM Read (FF80)
    else if ((emul2->typebackup==EEP24C08 || emul2->typebackup==EEP24C256) && Address==0xFF80) {
        return(c24xx_read() ? 0x01 : 0x00); // Geef 1 of 0 terug
    }
    // ADAMNet Read Check
    if ((emul2->currentMachineType == MACHINEADAM) && PCBTable[Address]) {
        // Lees alleen als het onderliggende geheugen RAM is
        if ((Address < 0x8000 && adam_ram_lo) || (Address >= 0x8000 && adam_ram_hi)) {
            ReadPCB(Address); // Deze functie returned void, de waarde wordt intern geüpdatet?
            // De return hieronder leest dan de (mogelijk gewijzigde) RAM waarde.
        }
    }

    // Expansion gap: open bus op echte ColecoVision
    if (!sgm_enable && emul2->currentMachineType != MACHINEADAM &&
        Address >= 0x2000 && Address < 0x6000) {
        return 0xFF; // of keep last bus als je dat modelleert
    }

    // Lees van de gemapte geheugenlocatie
    BYTE* page = MemoryMap[Address >> 13];

    // Speciale behandeling voor standaard Coleco RAM mirroring (als SGM uit staat)
    if (!sgm_enable && (Address >= 0x6000 && Address < 0x8000)) {
        return page[Address & 0x03FF]; // Lees van de basis 1K
    }

    // Speciale behandeling voor SRAM read (E800-EFFF)
    // if (emul2->typebackup == EEPSRAM && Address >= 0xE800) {
    //     return RAM_Memory[Address]; // Lees direct uit RAM array
    // }
    if (emul2->typebackup == EEPSRAM) {
        if (Address >= 0xE000 && Address < 0xE800) return RAM_Memory[Address + 0x800];
        if (Address >= 0xE800 && Address < 0xF000) return RAM_Memory[Address];
    }

    return page[Address & 0x1FFF];
}

BYTE coleco_getbyte(int Address) { return coleco_ReadByte(Address); } // Debugger
BYTE coleco_readoperandbyte(int Address) { return coleco_ReadByte(Address); } // Z80 Operand
BYTE coleco_readbyte(int Address) { // Vanuit Z80 met logging
    lastMemoryReadAddrLo = lastMemoryReadAddrHi; lastMemoryReadAddrHi = Address;
    BYTE byte = coleco_ReadByte(Address);
    lastMemoryReadValueLo = lastMemoryReadValueHi; lastMemoryReadValueHi = byte;
    return byte;
}
BYTE coleco_opcode_fetch(int Address) { return coleco_ReadByte(Address); } // Z80 Opcode

//---------------------------------------------------------------------------
void coleco_writeport(int Address, int Data, int * /**tstates*/)
{
    BYTE irq_status = 0;
    bool resetadam;

    Address &= 0xFF; // 8-bit poort adres

    // // FAST PATH: PSG op 0xFF
    // if (Address == 0xFF) {
    //     sn76489_write((uint8_t)Data);   // gewoon direct aanroepen
    //     // DBG_PRINTF("[PSG] W %02X", Data); // tijdelijk aan voor debug
    // }

    // // stuur eerst joystick/keypad select door naar nieuwe I/O
    // if ((Address & 0xE0)==0x80 || (Address & 0xE0)==0xC0) {
    //     coleco_io_write((uint8_t)Address, (uint8_t)Data);
    //     // ga NIET meer met coleco_joymode werken
    // }

    switch(Address & 0xE0)
    {
    case 0x00: // 0x00 - 0x1F: Unused
        break;
    case 0x20: // 0x20 - 0x3F: AdamNet Control
        resetadam = (coleco_port20 & 1) && ((Data & 1) == 0);
        coleco_port20=Data;
        if (emul2->currentMachineType == MACHINEADAM) coleco_setadammemory(resetadam);
        else if(emul2->SGM) coleco_setupsgm();
        break;
    case 0x40: // 0x40-0x5F: Printer / SGM Sound / SGM Control
        if((emul2->currentMachineType == MACHINEADAM)&&(Address==0x40)) Printer(Data);
        else if(emul2->SGM)
        {
            if(Address==0x53) { coleco_port53 = Data; coleco_setupsgm(); }
            else if (Address==0x50) ay8910_write(0,Data); // Control data
            else if (Address==0x51) ay8910_write(1,Data); // Write data
        }
        break;
    case 0x60: // 0x60 - 0x7F: Memory Control
        coleco_port60=Data;
        if (emul2->currentMachineType == MACHINEADAM) coleco_setadammemory(resetadam);
        else if (emul2->SGM) coleco_setupsgm();
        break;
    case 0x80: // 0x80 - 0x9F: Controller 1 Write (Mode Select)
        coleco_io_write((uint8_t)Address, (uint8_t)Data);
        break;
    case 0xA0: // 0xA0 - 0xBF: VDP Write (Video Display Processor)
        coleco_updatetms=1;
        if((Address & 0x01)==0) // Even addresses: 0xA0, 0xA2... 0xBE (VDP DATA)
        { // Data Port
            //if (emul2->F18A) f18a_writedata(Data);
            //else
                tms9918_writedata(Data);
        }
        else // Odd addresses: 0xA1, 0xA3... 0xBF (VDP CONTROL/REGISTER)
        { // Control Port
           //if (emul2->F18A) irq_status = f18a_writectrl(Data);
           //else
           if (tms9918_writectrl(Data))
                z80_set_irq_line(machine.interrupt, ASSERT_LINE);
           else
                z80_set_irq_line(machine.interrupt, CLEAR_LINE);
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
    case 0x00:
        break;

    case 0x20: // 0x20 - 0x3F: AdamNet Control Read
        if (emul2->currentMachineType == MACHINEADAM)
        {
            return(coleco_port20);
        }
        break;
    case 0x40: // 0x40 - 0x5F: Printer Status / SGM Sound Read
        if((emul2->currentMachineType == MACHINEADAM) && (Address==0x40))
        {
            return 0xFF;
        }
        // SGM AY-3-8910 Sound Chip (Port 0x52 = read data)
        else if (emul2->SGM && (Address == 0x52))
        {
            return(ay8910_read());
        }
        break;
    case 0x60: // 0x60 - 0x7F: Memory Control Read
        if (emul2->currentMachineType == MACHINEADAM)
        {
            return(coleco_port60);
        }
        break;

    case 0xA0: // 0xA0 - 0xBF: VDP Read (Video Display Processor)
        if ((Address & 0x01) == 0) // Even addresses: 0xA0, 0xA2... 0xBE (DATA READ)
            return /*(emul2->F18A ? f18a_readdata() :*/ tms9918_readdata(); //);
        else // Odd addresses: 0xA1, 0xA3... 0xBF (STATUS READ)
            return /*(emul2->F18A ? f18a_readctrl() :*/ tms9918_readctrl(); //);

    case 0xE0: // 0xE0..0xE3 brede controller-reads (A1 = pad)
    case 0xFC: // smal: pad 1
    case 0xFF: // smal: pad 2
        return coleco_io_read((uint8_t)Address); // 🔴 alleen doorsturen
    }

    return idleDataBus; // Geen geldige poort
}

//---------------------------------------------------------------------------
int coleco_contend(int /*Address*/, int /*states*/, int time) { return(time); } // Geen contentie gemodelleerd

//---------------------------------------------------------------------------
// do a Z80 instruction or frame
// Simuleert één scanline
int coleco_do_scanline(void)
{
    int ts;
    int MaxScanLen = machine.tperscanline;

    // VANGNET: als niet gezet, neem ~228 T-states per lijn (NTSC)
    if (MaxScanLen <= 0) MaxScanLen = 228;

    int CurScanLine_len = MaxScanLen;
    int tstotal = 0;

    ts = z80_checknmi(); // NMI check at start of line
    CurScanLine_len -= ts;
    tstotal += ts;

    do {
        ts = z80_do_opcode();
        CurScanLine_len -= ts;

        if (emul2->F18A) {
            // TODO: F18A GPU timing
        }

        frametstates += ts;
        tStatesCount += ts;
        tstotal += ts;

    } while (CurScanLine_len > 0 && !(emul2->stop) && !(emul2->singlestep));

    // VDP-lijn draaien en checken of we in VBlank komen
    BYTE vdp_irq = emul2->F18A ? f18a_loop() : tms9918_loop();


    /*
      NMI is edge-insensitive op Z80: bij level-hoog verlaat de CPU HALT zodra de lijn actief is.
      Door NMI hoog te laten totdat de game het VBlank-statusbit wist (ack), garanderen we dat de CPU het nooit mist —
      ook niet als je per scanline maar weinig opcodes draait of de puls net tussen twee calls viel.
      Zodra de game de TMS-status leest in zijn ISR, wordt tms.SR & TMS9918_STAT_VBLANK 0 → we clearen NMI → alles zoals hardware.
.   */
    static int nmi_active = 0;

    if (vdp_irq) {
        if (!nmi_active) {
            // Start VBlank → NMI level HOOG
            z80_set_irq_line(INPUT_LINE_NMI, ASSERT_LINE);
            nmi_active = 1;
        }
    } else {
        // Buiten het zichtbare VBlank-venster. Kijk of de game het VBlank-flag al heeft gelezen.
        if (nmi_active) {
            // Zodra het VBlank-bit uit de status is gewist (ACK), mogen we de NMI weer laag maken.
            if (!(tms.SR & TMS9918_STAT_VBLANK)) {
                z80_set_irq_line(INPUT_LINE_NMI, CLEAR_LINE);
                nmi_active = 0;
            }
            // Let op: als de game *nog* niet gelezen heeft, laten we NMI gewoon hoog;
            // de volgende scanlines blijven de CPU wakker houden tot de ISR status leest.
        }
    }

    // if (tms.CurLine == TMS9918_END_LINE) {

    //     DBG_PRINTF("[Z80] PC=%04X HALT=%u IFF1=%u IFF2=%u",
    //                (unsigned)Z80.pc.w.l,
    //                (unsigned)Z80.halt,
    //                (unsigned)Z80.iff1,
    //                (unsigned)Z80.iff2);
    // }
    return tstotal;
}

//---------------------------------------------------------------------------
void Printer(BYTE V) // Dummy Printer functie
{
    // VCL: printviewer->SendPrint(V);
    (void)V; // Markeer als ongebruikt
}

//---------------------------------------------------------------------------
// Save/Load State (Minimalistische versie, alleen compilatie focus)
// !! LET OP: Deze functies zijn zeer incompleet en zullen waarschijnlijk crashen !!
// !! Ze zijn hier alleen om compilatie mogelijk te maken. Logica moet later volledig herzien worden. !!

BYTE coleco_savestate(char *filename)
{
    BYTE stateheader[25] = "emultwo state\032\1\0\0\0\0\0\0\0\0\0";
    // BYTE *statebuf = NULL; // Gebruik direct schrijven naar bestand
    FILE *fstatefile = NULL;

    // Vul CRC in header
    DWORD crc = emul2->cardcrc;
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
    fwrite(&tms, sizeof(tms), 1, fstatefile); // TODO: F18A state?

    // Schrijf Sound states
    //fwrite(&sn, sizeof(sn), 1, fstatefile);
    //if (emul2->SGM) fwrite(&ay, sizeof(ay), 1, fstatefile);

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

// static inline bool fread_exact(FILE* f, void* dst, size_t bytes) {
//     return std::fread(dst, 1, bytes, f) == bytes;
// }
static inline bool fread_one(FILE* f, void* dst, size_t elemsz) {
    return std::fread(dst, elemsz, 1, f) == 1;
}

BYTE coleco_loadstate(char *filename)
{
    BYTE stateheader[24];
    // VCL: unsigned int statesave[32]; // Problematisch
    // BYTE *statebuf = NULL; // Lees direct uit bestand
    FILE *fstatefile = NULL;
    DWORD saved_crc = 0;

    fstatefile = fopen(filename,"rb");
    if(!fstatefile) return(0); // VCL: MessageBox

    if (fread(stateheader, 1, 24, fstatefile) != 24) { fclose(fstatefile); return(0); } // VCL: MessageBox
    if (memcmp(stateheader,"emultwo state\032\1\0\0",17) != 0) { fclose(fstatefile); return(0); } // VCL: MessageBox

    // Lees CRC uit header
    saved_crc = stateheader[18] | (stateheader[19]<<8) | (stateheader[20]<<16) | (stateheader[21]<<24);

    // TODO: Laad ROM hier gebaseerd op state info (emul2->currentrom?) en vergelijk CRC

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
    //if (emul2->SGM) fread(&ay, sizeof(ay), 1, fstatefile); // Lees alleen AY als SGM actief was
    uint8_t snd_ver = 0;
    if (fread(&snd_ver, 1, 1, fstatefile) != 1) { fclose(fstatefile); return 0; }

    int clk = 0, sr = 0, sgm = 0;
    if (fread(&clk, sizeof(clk), 1, fstatefile) != 1) { fclose(fstatefile); return 0; }
    if (fread(&sr,  sizeof(sr),  1, fstatefile) != 1) { fclose(fstatefile); return 0; }
    if (fread(&sgm, sizeof(sgm), 1, fstatefile) != 1) { fclose(fstatefile); return 0; }

    // --- Sound (PSG) herstellen ---
    if (clk <= 0) clk = (emul2->NTSC ? CLOCK_NTSC : CLOCK_PAL);
    if (sr  <= 0) sr  = SampleRate;

    emul2->SGM = sgm ? 1 : 0;
    // Init onze PSG-bridge met juiste OUT-rate en standaard PSG-clock
    sn76489_init(Clock, SampleRate);
    sn76489_reset(Clock, SampleRate);

    // Lees Geheugen
    fread(RAM_Memory, 1, MAX_RAM_SIZE*1024, fstatefile);
    fread(SRAM_Memory, 1, MAX_EEPROM_SIZE*1024, fstatefile);
    fread(VDP_Memory, 1, 0x10000, fstatefile); // Laad VDP memory

    // Herstel memory map pointers gebaseerd op geladen state (cruciaal!)
    // Moet gebaseerd zijn op geladen coleco_port60, coleco_port20, sgm_enable etc.
    if (emul2->currentMachineType == MACHINEADAM) coleco_setadammemory(false);
    else coleco_setupsgm();
    if (coleco_megacart) megacart_bankswitch(coleco_megabank); // Herstel bank

    fclose(fstatefile);
    return(1);
}
// --- Z80 CPU Callbacks ---
//
// Deze functies worden AANGEROEPEN DOOR z80.c om te praten
// met het Coleco-geheugen en de poorten.
// We moeten ze in 'extern "C"' plaatsen zodat de C-linker ze kan vinden.
//
extern "C" {

// 8-bit geheugen-callback (voor opcodes)
unsigned int cpu_readmem16(unsigned int address)
{
    return (unsigned int)coleco_readbyte(address);
}

// 8-bit geheugen-callback
void cpu_writemem16(unsigned int address, unsigned int value)
{
    coleco_writebyte(address, (BYTE)value);
}

// 8-bit poort-callback
unsigned char cpu_readport(unsigned int port)
{
    return coleco_readport(port, &tstates);
}

// 8-bit poort-callback
void cpu_writeport(unsigned int port, unsigned char value)
{
    coleco_writeport(port, value, &tstates);
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

} // extern "C"
// --- Einde Z80 Callbacks ---
