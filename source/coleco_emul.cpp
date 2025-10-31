#include "coleco.h"
#include <cstring>
#include <cstdio>

// ====== Globals (mogen al elders bestaan; als ze elders al defined zijn, haal de 'extern' hier en laat alleen één definitie over) ======

#ifndef MAX_CART_SIZE
#define MAX_CART_SIZE 512   // in KB -> 512KB = 524288 bytes buffer
#endif

BYTE *MemoryMap[8];

// Ports / mapping state
BYTE coleco_port20 = 0x00;
BYTE coleco_port53 = 0x00;
BYTE coleco_port60 = 0x0F;

int  sgm_enable      = 0;
int  sgm_firstwrite  = 1;
int  sgm_low_addr    = 0x2000;
int  sgm_neverenable = 0;

int adam_ram_lo      = 0;
int adam_ram_hi      = 0;
int adam_ram_lo_exp  = 0;
int adam_ram_hi_exp  = 0;
int adam_128k_mode   = 0;

BYTE coleco_megacart = 0x00;
BYTE coleco_megabank = 0x00;
int  coleco_megasize = 2;

unsigned int coleco_joystat = 0;
BYTE         coleco_joymode = 0;

unsigned int lastMemoryWriteAddrLo   = 0;
unsigned int lastMemoryWriteAddrHi   = 0;
unsigned int lastMemoryWriteValueLo  = 0;
unsigned int lastMemoryWriteValueHi  = 0;


// ===== Helper: megacart bankswitch =====
// banks >32K. active window is 0x8000-0xBFFF, fixed bank at 0xC000-0xFFFF
void megacart_bankswitch(BYTE bank)
{
    bank &= coleco_megacart;
    if (coleco_megabank != bank)
    {
        // switchable bank goes in 0x8000-0xBFFF
        MemoryMap[4] = ROM_Memory + ((unsigned int)bank << 14);
        MemoryMap[5] = MemoryMap[4] + 0x2000;

        // fixed bank is last one, already mapped in [6] and [7] when cart loaded
        coleco_megabank = bank;
    }
}


// ===== Helper: ADAM mapping =====
void coleco_setadammemory(bool resetadam)
{
    // Deze functie zet Adam geheugenbanking op basis van coleco_port20, port60, etc.
    // Je had al een versie in je huidige coleco.cpp. Neem die en zet hem hier 1-op-1.
    // Belangrijk is dat hij MemoryMap[0..7] correct update voor MACHINEADAM.

    // TODO: copy jouw werkende coleco_setadammemory() code hierheen.
    (void)resetadam;
}


// ===== Helper: SGM mapping =====
void coleco_setupsgm(void)
{
    // SGM = Super Game Module extra RAM en BIOS override
    // De originele EmulTwo logica:
    //  - port53 & 1  => enable SGM RAM
    //  - port60 bits => control of BIOS zichtbaar blijft of RAM onderin komt
    //
    // Daarna stelt hij MemoryMap[0..3] opnieuw in.

    if (!emul2->SGM) {
        // SGM niet geactiveerd in UI -> standaard Coleco gedrag
        sgm_enable = 0;
        sgm_low_addr = 0x2000;
        MemoryMap[0] = BIOS_Memory;          // BIOS @0000
        MemoryMap[1] = RAM_Memory + 0x2000;  // RAM
        MemoryMap[2] = RAM_Memory + 0x4000;  // RAM
        MemoryMap[3] = RAM_Memory + 0x6000;  // RAM
        return;
    }

    // enable flag
    sgm_enable = (coleco_port53 & 0x01) ? 1 : 0;

    // port60 bit1 (0x02) vaak bepaalt of BIOS nog zichtbaar is
    bool biosVisible = (coleco_port60 & 0x02) != 0;

    if (sgm_enable)
    {
        // Extra RAM vanaf 0x2000 normaal, mogelijk vanaf 0x0000 als BIOS uit staat.
        sgm_low_addr = biosVisible ? 0x2000 : 0x0000;

        if (biosVisible) {
            // 0000-1FFF = BIOS
            MemoryMap[0] = BIOS_Memory;
        } else {
            // 0000-1FFF = RAM
            MemoryMap[0] = RAM_Memory;
        }

        // 2000-7FFF = RAM
        MemoryMap[1] = RAM_Memory + 0x2000;
        MemoryMap[2] = RAM_Memory + 0x4000;
        MemoryMap[3] = RAM_Memory + 0x6000;
    }
    else
    {
        // SGM disabled -> gewone ColecoVision layout
        sgm_low_addr = 0x2000;
        MemoryMap[0] = BIOS_Memory;
        MemoryMap[1] = RAM_Memory + 0x2000;
        MemoryMap[2] = RAM_Memory + 0x4000;
        MemoryMap[3] = RAM_Memory + 0x6000;
    }
}


// ===== CPU memory READ =====
BYTE coleco_ReadByte(unsigned int Address)
{
    // ADAM memory special cases
    if (emul2->currentMachineType == MACHINEADAM)
    {
        // Adam RAM hi/lo windows + AdamNet I/O
        // Dit moet overeenkomen met jouw bestaande Adam code.
        // TODO: copy jouw Adam leeslogica hierheen.

        if (PCBTable[Address]) {
            return ReadPCB(Address);
        }

        // fallback naar MemoryMap
        return MemoryMap[Address >> 13][Address & 0x1FFF];
    }

    // ColecoVision / SGM side
    if (Address >= 0xE000)
    {
        // EEPROM / SRAM / etc. leest vaak hier.
        // MegaCart bankswitch hotspot via READ:
        if (coleco_megacart && Address >= 0xFFC0)
        {
            BYTE newBank = (BYTE)(Address & coleco_megacart);
            megacart_bankswitch(newBank);
            return coleco_megabank;
        }

        // voor SRAM carts etc. kun je speciale handling hebben
        // maar standaard is gewoon lezen uit MemoryMap
    }

    return MemoryMap[Address >> 13][Address & 0x1FFF];
}


// ===== CPU memory WRITE =====
void coleco_WriteByte(unsigned int Address, int Data)
{
    // Track last writes
    lastMemoryWriteAddrLo   = lastMemoryWriteAddrHi;
    lastMemoryWriteAddrHi   = Address;
    lastMemoryWriteValueLo  = lastMemoryWriteValueHi;
    lastMemoryWriteValueHi  = (unsigned int)Data;

    // ADAM?
    if (emul2->currentMachineType == MACHINEADAM)
    {
        // Schrijf alleen als RAM gemapt is in deze bank
        // (dit komt uit je huidige coleco_WriteByte Adam-blok)
        // TODO: paste jouw Adam write code hier
        // inclusief WritePCB() voor AdamNet apparaten

        return;
    }

    // ColecoVision / SGM
    if (sgm_enable)
    {
        // SGM RAM vanaf sgm_low_addr tot 0x7FFF is writeable
        if ((Address >= (unsigned)sgm_low_addr) && (Address < 0x8000))
        {
            RAM_Memory[Address] = (BYTE)Data;
            return;
        }
    }
    else
    {
        // Standaard ColecoVision heeft 1KB RAM gespiegeld op 0x6000-0x7FFF
        if (Address >= 0x6000 && Address < 0x8000)
        {
            RAM_Memory[0x6000 + (Address & 0x03FF)] = (BYTE)Data;
            return;
        }
    }

    // High memory region
    if (Address >= 0xE000)
    {
        // SRAM cart writes (EEPSRAM): E000-E7FF map naar E800-EFFF
        if (emul2->typebackup == EEPSRAM)
        {
            if (Address >= 0xE000 && Address < 0xE800) {
                RAM_Memory[Address + 0x800] = (BYTE)Data;
                return;
            }
            if (Address >= 0xE800 && Address < 0xF000) {
                RAM_Memory[Address] = (BYTE)Data;
                return;
            }
        }

        // I2C EEPROM bitbang (EEP24C08 / EEP24C256)
        if (emul2->typebackup == EEP24C08 || emul2->typebackup == EEP24C256)
        {
            switch (Address & 0xFFF0)
            {
            case 0xFFC0: c24xx_write(c24.Pins & ~0x01 /*C24XX_SCL*/ ); return;
            case 0xFFD0: c24xx_write(c24.Pins |  0x01 /*C24XX_SCL*/ ); return;
            case 0xFFE0: c24xx_write(c24.Pins & ~0x02 /*C24XX_SDA*/ ); return;
            case 0xFFF0: c24xx_write(c24.Pins |  0x02 /*C24XX_SDA*/ ); return;
            }
        }

        // MegaCart bankswitch hotspot via WRITE
        if (coleco_megacart && Address >= 0xFFC0)
        {
            BYTE newBank = (BYTE)(Address & coleco_megacart);
            megacart_bankswitch(newBank);
            return;
        }
    }

    // else: writes to ROM/bios are ignored
}


// ===== Z80 OUT (write to I/O ports) =====
void coleco_writeport(int Address, int Data, int *Cycles)
{
    // NOTE: 'Cycles' kun je laten bestaan omdat je code dat verwacht.
    // Vaak wordt die niet gebruikt voor ColecoVision I/O timing.

    switch (Address & 0xE0)
    {
    case 0x80:
        // joystick row select
        coleco_joymode = 0;
        break;

    case 0xC0:
        // keypad column select
        coleco_joymode = 1;
        break;

    case 0xA0:
    {
        // VDP write: data or control
        // In originele EmulTwo wordt hier ook IRQ state gezet/cleared.
        // Jij hebt al tms9918_writedata / writectrl / f18a_... in je project.

        if (!(Address & 0x01)) {
            // data port
            if (emul2->F18A) f18a_writedata(Data);
            else             tms9918_writedata(Data);
        } else {
            // control port
            BYTE irq_status = emul2->F18A ? f18a_writectrl(Data)
                                          : tms9918_writectrl(Data);

            // Jij hebt waarschijnlijk een z80_set_irq_line variant.
            // irq_status != 0 betekent IRQ lijn asserten
            z80_set_irq_line(irq_status ? 1 /*ASSERT_LINE*/ : 0 /*CLEAR_LINE*/);
        }
        break;
    }

    case 0xE0:
        // PSG write (SN76489 noise/square)
        sn76489_write((BYTE)Data);
        break;

    case 0x40:
        // ADAM printer OR Super Game Module extra ports
        if ((emul2->currentMachineType == MACHINEADAM) && (Address == 0x40)) {
            Printer((BYTE)Data);
        } else if (emul2->SGM) {
            if (Address == 0x53) {
                coleco_port53 = (BYTE)Data;
                coleco_setupsgm();
            } else if (Address == 0x50) {
                // AY8910 register select
                ay8910_write(0, Data);
            } else if (Address == 0x51) {
                // AY8910 data write
                ay8910_write(1, Data);
            }
        }
        break;

    case 0x20:
    {
        bool resetadam = (
            emul2->currentMachineType == MACHINEADAM &&
            ((coleco_port20 & 1) && ((Data & 1) == 0))
            );

        coleco_port20 = (BYTE)Data;

        if (emul2->currentMachineType == MACHINEADAM) {
            coleco_setadammemory(resetadam);
        } else if (emul2->SGM) {
            coleco_setupsgm();
        }
        break;
    }

    case 0x60:
        coleco_port60 = (BYTE)Data;
        if (emul2->currentMachineType == MACHINEADAM) {
            coleco_setadammemory(false);
        } else if (emul2->SGM) {
            coleco_setupsgm();
        }
        break;
    }

    (void)Cycles;
}


// ===== Z80 IN (read from I/O ports) =====
BYTE ReadInputPort(int Address)
{
    switch (Address & 0xE0)
    {
    case 0xA0:
        // VDP read (data or status)
        if (Address & 0x01) {
            BYTE st = emul2->F18A ? f18a_readctrl() : tms9918_readctrl();
            // Sommige emu’s doen hier z80_set_irq_line(CLEAR_LINE) na status read.
            // Als jouw games blijven hangen in een waits-for-vblank loop,
            // dan moet je hier irq lijn droppen.
            z80_set_irq_line(0 /*CLEAR_LINE*/);
            return st;
        } else {
            return emul2->F18A ? f18a_readdata() : tms9918_readdata();
        }

    case 0xE0:
    {
        // Controller/keypad read
        // emuleert de 2 controllers multiplexed in coleco_joystat
        unsigned int sample =
            (Address & 0x02) ? (coleco_joystat >> 16) : coleco_joystat;

        if (coleco_joymode)
            sample >>= 8; // keypad half

        BYTE val = (~sample) & 0x7F; // active-low, 7 bits
        return val;
    }

    case 0x40:
        // SGM AY8910 read/status port 0x52
        if (emul2->SGM && (Address == 0x52)) {
            return (BYTE)ay8910_read();
        }
        if ((emul2->currentMachineType == MACHINEADAM) && (Address == 0x40)) {
            // ADAM printer status
            return 0xFF;
        }
        break;

    default:
        break;
    }

    // default open bus
    return 0xFF;
}


// ===== Reset alles =====
void coleco_reset(void)
{
    // 1. Init memory map pages as RAM (flat)
    MemoryMap[0] = RAM_Memory + 0x0000;
    MemoryMap[1] = RAM_Memory + 0x2000;
    MemoryMap[2] = RAM_Memory + 0x4000;
    MemoryMap[3] = RAM_Memory + 0x6000;
    MemoryMap[4] = RAM_Memory + 0x8000;
    MemoryMap[5] = RAM_Memory + 0xA000;
    MemoryMap[6] = RAM_Memory + 0xC000;
    MemoryMap[7] = RAM_Memory + 0xE000;

    // 2. Copy Coleco BIOS into RAM_Memory and map it at 0x0000 (unless ADAM)
    if (emul2->currentMachineType != MACHINEADAM)
    {
        // BIOS naar werkbuffer
        std::memcpy(RAM_Memory + 0x0000, BIOS_Memory, 0x2000);

        // PAL timing hack / bios delay hack
        RAM_Memory[0x0069] = emul2->hackbiospal ? 50 : 60;
        if (emul2->biosnodelay) {
            RAM_Memory[0x1F51] = 0x00;
            RAM_Memory[0x1F52] = 0x00;
            RAM_Memory[0x1F53] = 0x00;
        }

        // Map page0 to BIOS (Coleco boot ROM at 0000h)
        MemoryMap[0] = BIOS_Memory + 0x0000;
    }

    // 3. Map cartridge into 0x8000-0xFFFF
    // Standaard carts ≤32KB zitten gewoon lineair in ROM_Memory:
    MemoryMap[4] = ROM_Memory + 0x0000; // 0x8000
    MemoryMap[5] = ROM_Memory + 0x2000; // 0xA000
    MemoryMap[6] = ROM_Memory + 0x4000; // 0xC000
    MemoryMap[7] = ROM_Memory + 0x6000; // 0xE000

    // 4. Init ports / mapping state
    sgm_enable      = 0;
    sgm_firstwrite  = 1;
    sgm_low_addr    = 0x2000;
    sgm_neverenable = 0;

    coleco_port53   = 0x00;
    coleco_port20   = 0x00;
    coleco_port60   = (emul2->currentMachineType == MACHINEADAM) ? 0x00 : 0x0F;

    coleco_megacart = 0x00; // kan door loadcart overschreven zijn voor >32K
    coleco_megabank = 0x00;
    coleco_megasize = 2;

    adam_ram_lo      = 0;
    adam_ram_hi      = 0;
    adam_ram_lo_exp  = 0;
    adam_ram_hi_exp  = 0;
    adam_128k_mode   = 0;

    // Adam geheugen fix toepassen als je in Adam mode zit
    if (emul2->currentMachineType == MACHINEADAM) {
        coleco_setadammemory(false);
    } else if (emul2->SGM) {
        coleco_setupsgm();
    }

    // 5. Video chip reset
    if (emul2->F18A) f18a_reset();
    else             tms9918_reset();

    // scanline config
    tms.ScanLines = emul2->NTSC ? /*TMS9918_LINES*/ 262 : /*TMS9929_LINES*/ 313; // pas aan naar jouw defines
    // F18A 30-row mode extra lijnen
    // if (emul2->F18A && f18a.Row30) tms.ScanLines += 27;
    // TODO: gebruik jouw eigen F18A row30 flag als je die hebt.

    // 6. Audio init? (je had coleco_audio_init() in jouw versie)
    // TODO: roep jouw audio-init hier als nodig

    // 7. Z80 reset
    z80_reset();
}


// ===== ROM load =====
// Deze versie is de verbeterde versie die mirrort/padt,
// zie vorige bericht. Zelfde inhoud, maar nu in buslaag.
BYTE coleco_loadcart(char *filename)
{
    long size;
    FILE *fRomfile = std::fopen(filename, "rb");
    if (!fRomfile)
        return ROM_LOAD_FAIL;

    std::memset(ROM_Memory, 0xFF, (MAX_CART_SIZE * 1024)); // TODO: MAX_CART_SIZE extern laten bestaan

    std::fseek(fRomfile, 0, SEEK_END);
    size = std::ftell(fRomfile);
    if (size == -1L) {
        std::fclose(fRomfile);
        return ROM_LOAD_FAIL;
    }
    std::fseek(fRomfile, 0, SEEK_SET);

    if (size > (MAX_CART_SIZE * 1024)) {
        std::fclose(fRomfile);
        return ROM_LOAD_FAIL;
    }

    if (std::fread((void*)ROM_Memory, 1, size, fRomfile) != (size_t)size) {
        std::fclose(fRomfile);
        return ROM_LOAD_FAIL;
    }
    std::fclose(fRomfile);

    emul2->cardsize = (DWORD)size;
    emul2->cardcrc  = 0; // TODO: implement CRC32Block later // CRC32Block(ROM_Memory, emul2->cardsize); // TODO: declare CRC32Block somewhere

    coleco_megacart = 0x00;
    coleco_megasize = 2;
    coleco_megabank = 0x00;

    // mirror/pad naar 32K venster voor kleine carts
    if (size <= 0x2000) {
        // 8K -> repeat 4x
        for (int blk = 1; blk < 4; ++blk)
            std::memcpy(ROM_Memory + blk*0x2000, ROM_Memory, 0x2000);
    }
    else if (size <= 0x4000) {
        // 16K -> repeat twice
        std::memcpy(ROM_Memory + 0x4000, ROM_Memory, 0x4000);
    }
    else if (size <= 0x6000) {
        // 24K -> last block mirrored into 0xE000-0xFFFF
        std::memcpy(ROM_Memory + 0x6000, ROM_Memory + 0x4000, 0x2000);
    }
    else if (size <= 0x8000) {
        // 32K -> nothing
    }
    else {
        // Megacart flow
        long rounded = ((size + 0x3FFF) & ~0x3FFF);
        int j;
        for (j = 2; (j << 14) < rounded; j <<= 1) { /*power of two banks*/ }
        coleco_megasize = j;

        if      (coleco_megasize == 4)  coleco_megacart = 0x03;
        else if (coleco_megasize == 8)  coleco_megacart = 0x07;
        else if (coleco_megasize == 16) coleco_megacart = 0x0F;
        else                            coleco_megacart = 0x1F;

        // fixed bank = last 16K @ C000-FFFF
        MemoryMap[6] = ROM_Memory + ((coleco_megasize - 1) << 14);
        MemoryMap[7] = MemoryMap[6] + 0x2000;

        // switchable window bank0 @ 8000-BFFF
        MemoryMap[4] = ROM_Memory + (0 << 14);
        MemoryMap[5] = MemoryMap[4] + 0x2000;

        megacart_bankswitch(0);
    }

    emul2->romCartridgeType = coleco_megacart ? ROMCARTRIDGEMEGA : ROMCARTRIDGESTD;

    return ROM_LOAD_PASS;
}
