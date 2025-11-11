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
#include "fdidisk.h"

// BIOS loader prototype
static int loadBios(const char *filename, BYTE *memory, int sizerm);

static inline uint8_t AL(uint8_t v) { return (uint8_t)~v; } // active-low helper

// BIOS data komt nu uit colecobios.c en adambios.c (gedeclareerd in coleco.h)

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

FDIDisk Disks[MAX_DISKS] = {};
FDIDisk Tapes[MAX_TAPES] = {};

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
        emul2->cardsize = (DWORD)size;
        emul2->cardcrc = CRC32Block(ROM_Memory, emul2->cardsize);

        // --- Verificatie (Header check) ---
        p = (ROM_Memory[0]==0x55)&&(ROM_Memory[1]==0xAA)? ROM_Memory
            : (ROM_Memory[0]==0xAA)&&(ROM_Memory[1]==0x55)? ROM_Memory
            : (ROM_Memory[0]==0x66)&&(ROM_Memory[1]==0x99)? ROM_Memory
                                                                 : NULL;

        // Check magic header for Magecarts if not yet found at beginning (for 64K eeprom roms)
        adrlastbank = (size&~0x3FFF)-0x4000;

        //if (adrlastbank < 0) adrlastbank = 0;

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

    }
    else
    {
        fclose(fRomfile); return(retf);
    }

    emul2->romCartridgeType = coleco_megacart ? ROMCARTRIDGEMEGA : ROMCARTRIDGESTD;

    fclose(fRomfile);
    return ROM_LOAD_PASS;
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
///---------------------------------------------------------------------------
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
        MemoryMap[3] = (coleco_port20 & 0x02) ? BIOS_Memory + 0x8000  // Smartwriter
                                              : BIOS_Memory + 0x6000; // EOS
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
    // -> Onboard RAM
    if ((coleco_port60 & 0x0C) == 0x00)
    {
        adam_ram_hi = 1;
        adam_ram_hi_exp = 0;
        MemoryMap[4] = RAM_Memory + 0x8000;
        MemoryMap[5] = RAM_Memory + 0xA000;
        MemoryMap[6] = RAM_Memory + 0xC000;
        MemoryMap[7] = RAM_Memory + 0xE000;
    }
    // -> Expanded RAM
    else if ((coleco_port60 & 0x0C) == 0x08)
    {
        adam_128k_mode = 1;
        adam_ram_hi = 0;
        adam_ram_hi_exp = 1;
        MemoryMap[4] = RAM_Memory + 0x18000;
        MemoryMap[5] = RAM_Memory + 0x1A000;
        MemoryMap[6] = RAM_Memory + 0x1C000;
        MemoryMap[7] = RAM_Memory + 0x1E000;
    }
    // Nothing else exists so just return 0xFF
    else
    {
        adam_ram_hi = 0;
        adam_ram_hi_exp = 0;
    }

    qDebug() << "[MAP] ADAM port60=0x" << Qt::hex << int(coleco_port60)
             << " Map0->" << (void*)MemoryMap[0]
             << " (expect BIOS_Memory+0x0000)";

    qDebug().noquote()
        << "[BASE] BIOS=" << static_cast<void*>(BIOS_Memory)
        << " RAM=" << static_cast<void*>(RAM_Memory);

    qDebug().noquote()
        << "[MAP] port60=" << Qt::hex << int(coleco_port60)
        << " Map0->" << static_cast<void*>(MemoryMap[0])
        << " (expect BIOS+0x0000 when port60&3==0)";

    if (resetAdamNet)  ResetPCB(); // Nodig #include "adamnet.h"
}
// --------------------------------------------------------------------------

void coleco_setupsgm(void)
{
    // Super DK mag nooit SGM hebben
    if (sgm_neverenable) return;
    if (emul2->currentMachineType == MACHINEADAM) return;

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

    // Coleco: zorg dat BIOS in RAM staat voor hacks (CPU fetcht straks uit BIOS_Memory)
    if (emul2->currentMachineType != MACHINEADAM)
    {
        // ⬇️ Gebruik de BIOS die je eerder al in BIOS_Memory hebt geladen
        memcpy(RAM_Memory, BIOS_Memory, 0x2000);                  // CHANGED

        // Hacks (50/60Hz + nodelay)
        RAM_Memory[0x0069] = emul2->hackbiospal ? 50 : 60;
        if (emul2->biosnodelay) {
            RAM_Memory[159*32+17] = 0x00; // NOP
            RAM_Memory[159*32+18] = 0x00; // NOP
            RAM_Memory[159*32+19] = 0x00; // NOP
        }
    }

    // Randomize 0x6000-0x7FFF (NetPlay-consistentie); ok om te laten
    if (emul2->currentMachineType != MACHINEADAM) {
        for (i=0;i<0x2000;i++)
            RAM_Memory[i+0x6000] = rand() % 256;
    }

    sgm_enable     = 0;
    sgm_firstwrite = 1;
    sgm_low_addr   = 0xFFFF;   // CHANGED: 0xFFFF = “BIOS actief” marker i.p.v. 0x2000
    sgm_neverenable= 0;

    // Init SGM/ADAM-poorten
    coleco_port53 = 0x00;
    coleco_port60 = (emul2->currentMachineType == MACHINEADAM) ? 0x00 : 0x0F;
    coleco_port20 = 0x00;

    // ADAM memory init
    if (emul2->currentMachineType == MACHINEADAM)
    {
        adam_ram_lo = adam_ram_hi = adam_ram_lo_exp = adam_ram_hi_exp = 0;
        adam_128k_mode = 0; // 64K basis
        // Nog NIET mappen hier; doen we onderaan één keer met resetAdamNet=true
    }
    else
    {
        // Coleco start uit BIOS_Memory @ 0000-1FFF
        MemoryMap[0] = BIOS_Memory + 0x0000;                      // CONFIRM
    }

    // Backup-type autodetectie
    switch (emul2->cardcrc)
    {
    case 0x62DACF07: emul2->typebackup = EEP24C256; break;
    case 0xDDDD1396: emul2->typebackup = EEP24C08;  break;
    case 0xFEE15196:
    case 0x1053F610:
    case 0x60D6FD7D:
    case 0x37A9F237: emul2->typebackup = EEPSRAM;   break;
    case 0xEF25AF90:
    case 0xC2E7F0E0: sgm_neverenable = 1;           break;
    }

    // VDP reset
    if (emul2->F18A) f18a_reset(); else tms9918_reset();
    tms.ScanLines = emul2->NTSC ? TMS9918_LINES : TMS9929_LINES;
    if (emul2->F18A && f18a.Row30) tms.ScanLines += 27;

    // PSG’s
    sn76489_init(Clock, SampleRate);
    ay8910_init(Clock, SampleRate);

    // EEPROM reset
    if (emul2->typebackup != NOBACKUP && emul2->typebackup != EEPSRAM) {
        c24xx_reset(SRAM_Memory, emul2->typebackup==EEP24C08 ? C24XX_24C08 : C24XX_24C256);
    }

    // CPU reset
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
    if (emul2->currentMachineType == MACHINEADAM) {
        coleco_setadammemory(true);  // resetAdamNet = true, mapt EOS/Writer/OS7 correct
    } else {
        coleco_setupsgm();           // laat port53/port60 regels bepalen of low 8K BIOS/RAM is
    }
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
        // Coleco/Phoenix: standaard BIOS+cart
        // MOET ALTIJD 0x0F (BIOS AAN) zijn bij een reset!
        coleco_port60 = 0x0F;
        coleco_writeport(0x60, coleco_port60, nullptr);

        // Stel de SGM hardware poort (0x53) wel alvast in
        coleco_port53 = emul2->SGM ? 0x01 : 0x00;
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

        //MemoryMap[6] = ROM_Memory + ((coleco_megasize-1)<<14);
        //MemoryMap[7] = MemoryMap[6] + 0x2000;
    }
    else
    {
        if (emul2->currentMachineType != MACHINEADAM)
        {
            coleco_setupsgm();
        }        // Standaard 32K cart mapping
        // MemoryMap[4] = ROM_Memory + 0x0000;
        // MemoryMap[5] = ROM_Memory + 0x2000;
        // MemoryMap[6] = ROM_Memory + 0x4000;
        // MemoryMap[7] = ROM_Memory + 0x6000;
    }

    // --- VOEG DIT BLOK TOE ---
    // De VDP (en F18A) MOET OOK gereset worden. Anders start
    // de Adam BIOS terwijl de VDP nog in Coleco-modus staat.
    if (emul2->F18A) f18a_reset(); else tms9918_reset();
    tms.ScanLines = emul2->NTSC ? TMS9918_LINES : TMS9929_LINES;
    if (emul2->F18A && f18a.Row30) tms.ScanLines += 27;
    // --- EINDE TOEVOEGING ---

    z80_reset();
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
    z80_reset();
    tms9918_reset();

    //coleco_reset_and_restart_bios();  // zet BIOS op 0x0000 + reset VDP/CPU/PSG
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
    emul2->romCartridgeType = ROMCARTRIDGENONE;

    if (emul2->F18A) f18agpu_init();

    memset(ROM_Memory,  0xFF, MAX_CART_SIZE  * 1024);
    memset(RAM_Memory,  0xFF, MAX_RAM_SIZE   * 1024);
    memset(BIOS_Memory, 0xFF, MAX_BIOS_SIZE  * 1024); // ← BIOS vooraf leegmaken

    if (emul2->currentMachineType == MACHINEADAM)
    {
        // --- COLECO.ROM (OS7) in BIOS @ 0xA000..0xBFFF (8KB) ---
        if (strcmp(emul2->colecobios, "Internal") != 0)
        {
            if (!loadBios(emul2->colecobios, BIOS_Memory + 0xA000, 0x2000))
                memcpy(BIOS_Memory + 0xA000, colecobios_rom, 0x2000);
        }
        else
        {
            memcpy(BIOS_Memory + 0xA000, colecobios_rom, 0x2000);
        }

        // --- EOS.ROM (ADAM EOS) in BIOS @ 0x8000..0x9FFF (8KB) ---
        if (strcmp(emul2->adameos, "Internal") != 0)
        {
            if (!loadBios(emul2->adameos, BIOS_Memory + 0x8000, 0x2000))
                memcpy(BIOS_Memory + 0x8000, adambios_eos, 0x2000);
        }
        else
        {
            memcpy(BIOS_Memory + 0x8000, adambios_eos, 0x2000);
        }

        // --- WRITER.ROM (SmartWriter) in BIOS @ 0x0000..0x7FFF (32KB) ---
        if (strcmp(emul2->adamwriter, "Internal") != 0)
        {
            if (!loadBios(emul2->adamwriter, BIOS_Memory + 0x0000, 0x8000))
                memcpy(BIOS_Memory + 0x0000, adambios_writer, 0x8000);
        }
        else
        {
            memcpy(BIOS_Memory + 0x0000, adambios_writer, 0x8000);
        }

        memcpy(RAM_Memory + 0x0000, BIOS_Memory + 0x0000, 0x8000);

    }
    else
    {
        // --- ColecoVision BIOS @ 0x0000..0x1FFF (8KB) ---
        if (strcmp(emul2->colecobios, "Internal") != 0)
        {
            if (!loadBios(emul2->colecobios, BIOS_Memory, 0x2000))
                memcpy(BIOS_Memory, colecobios_rom, 0x2000);
        }
        else
        {
            memcpy(BIOS_Memory, colecobios_rom, 0x2000);
        }

        // Kopie naar RAM voor BIOS-hacks (wordt overschreven als SGM low-RAM actief is)
        memcpy(RAM_Memory, BIOS_Memory, 0x2000);

        // Hacks
        RAM_Memory[0x0069] = emul2->hackbiospal ? 50 : 60; // 50/60 Hz
        if (emul2->biosnodelay) {
            RAM_Memory[159*32+17] = 0x00;
            RAM_Memory[159*32+18] = 0x00;
            RAM_Memory[159*32+19] = 0x00;
        }
    }

    qDebug() << "[INIT] WRITER[0000]=" << Qt::hex << BIOS_Memory[0]
             << " EOS[8000]=" << Qt::hex << BIOS_Memory[0x8000]
             << " OS7[A000]=" << Qt::hex << BIOS_Memory[0xA000];

    // Verwijder alle Adam-media
    for (i = 0; i < MAX_DISKS;  ++i) EjectFDI(&Disks[i]);
    for (i = 0; i < MAX_TAPES;  ++i) EjectFDI(&Tapes[i]);

    // Reset & palet
    coleco_reset();
    coleco_setpalette(emul2->palette);
}

//---------------------------------------------------------------------------
// Switch banks. Up to 512K of the Colecovision Mega Cart ROM can be stored
void megacart_bankswitch(BYTE bank)
{
    // Only if the bank was changed...
    if (coleco_megabank != bank)
    {
        MemoryMap[6] = ROM_Memory + ((unsigned int) bank * 0x4000);
        MemoryMap[7] = MemoryMap[6] + 0x2000;
        coleco_megabank = bank;
    }
}
//---------------------------------------------------------------------------
// coleco.cpp
void coleco_WriteByte(unsigned int Address, int Data)
{
    // --- ADAM MODUS ---
    if (emul2->currentMachineType == MACHINEADAM)
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

        // Expanded RAM (heeft NOOIT PCB-toegang)
        else if ((Address < 0x8000) && adam_ram_lo_exp)
        {
            RAM_Memory[0x10000 + Address] = (BYTE)Data;
        }
        else if ((Address >= 0x8000) && adam_ram_hi_exp)
        {
            RAM_Memory[0x10000 + Address] = (BYTE)Data;
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
        if (emul2->typebackup==EEPSRAM)
        {
            RAM_Memory[Address+0x800]=Data;
            return;
        }
    }

    // Cartridges, containing EEPROM
    else if (((emul2->currentMachineType != MACHINEADAM) && (emul2->typebackup==EEP24C08)) || (emul2->typebackup==EEP24C256) )
    {
        if ((Address == 0xFF90) || (Address == 0xFFA0) || (Address == 0xFFB0))
        {
            megacart_bankswitch((Address>>4) & coleco_megacart);
        }

        switch(Address)
        {
        case 0xFFC0: c24xx_write(c24.Pins&~C24XX_SCL);return;
        case 0xFFD0: c24xx_write(c24.Pins|C24XX_SCL);return;
        case 0xFFE0: c24xx_write(c24.Pins&~C24XX_SDA);return;
        case 0xFFF0: c24xx_write(c24.Pins|C24XX_SDA);return;
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
    // ADAM mag NOOIT de cart-hotspots zien
    if (emul2->currentMachineType != MACHINEADAM && coleco_megacart) {
        if (Address >= 0xFFC0) {
            megacart_bankswitch(Address & coleco_megacart);
            return coleco_megabank;
        }
    }

    // ADAM mag ook geen EEPROM-bitje “kapen”
    if (emul2->currentMachineType != MACHINEADAM &&
        ((emul2->typebackup==EEP24C08)||(emul2->typebackup==EEP24C256)) &&
        Address==0xFF80)
    {
        return c24xx_read();
    }

    // AdamNet side-effect + ALTIJD geheugenbyte teruggeven
    if (emul2->currentMachineType == MACHINEADAM && PCBTable[Address]) {
        (void)ReadPCB(Address); // side-effect only
    }
    return *(MemoryMap[Address>>13] + (Address & 0x1FFF));
}
//---------------------------------------------------------------------------
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
    bool resetadam = 0;

    Address &= 0xFF; // 8-bit poort adres

    switch(Address & 0xE0)
    {
    case 0x00: // 0x00 - 0x1F: Unused
        break;

    case 0x20: // 0x20 - 0x3F: AdamNet Control
        resetadam=(coleco_port20 & 1) && ((Data & 1) == 0);
        coleco_port20=Data;
        if (emul2->currentMachineType == MACHINEADAM) coleco_setadammemory(resetadam);
        else if(emul2->SGM) coleco_setupsgm();
        break;

    case 0x60: // 0x60 - 0x7F: Memory Control
        coleco_port60=Data;
        if (emul2->currentMachineType == MACHINEADAM) coleco_setadammemory(resetadam);
        else if (emul2->SGM) coleco_setupsgm();
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


    case 0x80: // 0x80 - 0x9F: Controller 1 Write (Mode Select)
        coleco_io_write((uint8_t)Address, (uint8_t)Data);
        break;
    case 0xA0: // 0xA0 - 0xBF: VDP Write (Video Display Processor)
        coleco_updatetms=1;
        if((Address & 0x01)==0) // Even addresses
        { tms9918_writedata(Data); }
        else // Odd addresses
        { tms9918_writectrl(Data); }
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
    // NU-
    case 0x40: // 0x40 - 0x5F: Printer Status / SGM Sound Read
        // --- NIEUWE, ROBUUSTE LOGICA ---
        if (emul2->currentMachineType == MACHINEADAM)
        {
            // --- Adam Modus ---
            if (Address == 0x40) {
                return 0xFF; // Printer = OK (Fix 7)
            }

            // Adam mag NOOIT de SGM-poorten lezen.
            // Blokkeer 0x52 (SGM Read) en alle andere
            // poorten in deze 0x40-0x5F range.
            return idleDataBus; // (0xFF)
        }
        else
        {
            // --- Coleco Modus ---
            // SGM AY-3-8910 Sound Chip (Port 0x52 = read data)
            // Deze mag ALLEEN in Coleco-modus worden gelezen.
            if (Address == 0x52)
            {
                return(ay8910_read());
            }
            // (Geen printer op Coleco, dus we vallen door)
        }
        break; // Val door naar 'return idleDataBus'

    case 0x60: // 0x60 - 0x7F: Memory Control Read
        if (emul2->currentMachineType == MACHINEADAM)
        {
            return(coleco_port60);
        }
        break;

    case 0x99:
        return ReadStatus9918();

    case 0xA0: // 0xA0 - 0xBF: VDP Read (Video Display Processor)
        if ((Address & 0x01) == 0) // Even addresses: 0xA0, 0xA2... 0xBE (DATA READ)
            return /*(emul2->F18A ? f18a_readdata() :*/ tms9918_readdata(); //);
        else // Odd addresses: 0xA1, 0xA3... 0xBF (STATUS READ)
            return /*(emul2->F18A ? f18a_readctrl() :*/ tms9918_readctrl(); //);

    case 0xE0: // 0xE0..0xE3 brede controller-reads (A1 = pad)
    case 0xFC: // smal: pad 1
    case 0xFF: // smal: pad 2
         return coleco_io_read((uint8_t)Address); // alleen doorsturen
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


    /*
      NMI is edge-insensitive op Z80: bij level-hoog verlaat de CPU HALT zodra de lijn actief is.
      Door NMI hoog te laten totdat de game het VBlank-statusbit wist (ack), garanderen we dat de CPU het nooit mist —
      ook niet als je per scanline maar weinig opcodes draait of de puls net tussen twee calls viel.
      Zodra de game de TMS-status leest in zijn ISR, wordt tms.SR & TMS9918_STAT_VBLANK 0 → we clearen NMI → alles zoals hardware.
.   */

    static int nmi_active = 0;

    // --- VDP update ---
    if (emul2->F18A) f18a_loop();
    else tms9918_loop();

    // --- NMI level-driven interrupt ---
    // Level: NMI = aan als (IRQ enable in R1) EN (VBlank-bit in SR)
    const bool vdp_irq_level =
        ( (tms.VR[1] & TMS9918_REG1_IRQ) != 0 ) &&
        ( (tms.SR    & TMS9918_STAT_VBLANK) != 0 );

    if (vdp_irq_level) {
        if (!nmi_active) {
            z80_set_irq_line(INPUT_LINE_NMI, ASSERT_LINE);
            nmi_active = 1;
        }
    }
    else {
        if (nmi_active) {
            z80_set_irq_line(INPUT_LINE_NMI, CLEAR_LINE);
            nmi_active = 0;
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

void coleco_eject_disk(int drive) {
    EjectFDI(&Disks[drive]);
}
void coleco_eject_tape(int drive) {
    EjectFDI(&Tapes[drive]);
}

} // extern "C"
// --- Einde Z80 Callbacks ---
