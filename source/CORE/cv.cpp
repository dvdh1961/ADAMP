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
 * Adam+ changes  DVDH1961
*/


#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <QDebug>
#include <QFile>
#include <QIODevice>
#include <QFileInfo>
#include <QDir>
#include <QCoreApplication>

#include "cv.h"
#include "cvbank.h"
#include "z80.h"
#include "psg_bridge.h"
#include "CORE/c24xx.h"
#include "6801/adnet_core.h"
#include "cvkpad.h"
#include "SOUND/snd_ay8910.h"
#include "disasm_bridge.h"
#include "BIOS/bios_coleco.h"
#include "BIOS/bios_adam.h"
#include "6801/fdidisk.h"
#include <errno.h>
#include <string.h>
#include "input_bridge.h"
#include "debug_bridge.h"
#include "GRAPH/tms9928a.h"
#include "GRAPH/f18a.h"
#include "GRAPH/f18a_term80.h"
#include "GRAPH/f18a_term80_cpm.h"
#include "GRAPH/f18a_term80_tdos.h"

// BIOS loader prototype
static int loadBios(const char *filename, BYTE *memory, int sizerm);
static void Out42(BYTE Val);
// BIOS data komt nu uit colecobios.c en adambios.c (gedeclareerd in coleco.h)
static int g_cpm_trace = 0;
#ifdef ADAMP_CPM_TRAP
//static bool g_cpm_memory_active = false;
#endif
static bool g_cpm_protection_active = false;
static int  g_cleanup_phase = 0; // 0=Wachten, 1=Injectie DB, 2=Injectie BF, 3=Klaar

static void adamp_vdp_trace(const char *event, unsigned value)
{
    static QFile traceFile(
        QCoreApplication::applicationDirPath() + "/ADAMP_VDP_TRACE.log");
    static unsigned count = 0;
    if (count++ >= 250000) return;

    if (!traceFile.isOpen())
    {
        if (!traceFile.open(QIODevice::WriteOnly |
                            QIODevice::Text |
                            QIODevice::Truncate))
            return;
    }

    char line[512];
    const int lineLength = std::snprintf(
        line, sizeof(line),
        "%s pc=%04X val=%02X key=%u addr=%04X mode=%u sr=%02X "
        "r0=%02X r1=%02X r2=%02X r3=%02X r4=%02X r5=%02X r6=%02X r7=%02X\n",
        event, (unsigned)Z80.pc.w.l, value & 0xFFu,
        (unsigned)tms.VKey, (unsigned)tms.VAddr, (unsigned)tms.Mode,
        (unsigned)tms.SR,
        tms.VR[0], tms.VR[1], tms.VR[2], tms.VR[3],
        tms.VR[4], tms.VR[5], tms.VR[6], tms.VR[7]);

    if (lineLength > 0)
        traceFile.write(line, lineLength < (int)sizeof(line) ? lineLength : (int)sizeof(line) - 1);
    traceFile.flush();
}

// Diagnostic snapshots, refreshed once per second while the TMS VDP is active.
// The files are written next to the emulator executable.
static void adamp_dump_machine_state()
{
    const QString base = QCoreApplication::applicationDirPath();

    QFile vramFile(base + "/ADAMP_VRAM.bin");
    if (vramFile.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        vramFile.write(reinterpret_cast<const char *>(VDP_Memory), 0x4000);
        vramFile.close();
    }

    QFile ramFile(base + "/ADAMP_RAM.bin");
    if (ramFile.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        // Include both the 64 KiB intrinsic RAM and the 64 KiB expansion RAM.
        ramFile.write(reinterpret_cast<const char *>(RAM_Memory),
                      MAX_RAM_SIZE * 1024);
        ramFile.close();
    }

    // Snapshot exactly what the Z80 sees, without using coleco_ReadByte()
    // (which has AdamNet read side effects at PCB addresses).
    QFile activeFile(base + "/ADAMP_ACTIVE_RAM.bin");
    if (activeFile.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        for (unsigned page = 0; page < 8; ++page)
            activeFile.write(
                reinterpret_cast<const char *>(MemoryMap[page]), 0x2000);
        activeFile.close();
    }

    QFile stateFile(base + "/ADAMP_STATE.txt");
    if (stateFile.open(QIODevice::WriteOnly |
                       QIODevice::Text |
                       QIODevice::Truncate))
    {
        char state[1024];
        const int length = std::snprintf(
            state, sizeof(state),
            "PC=%04X VDP_ADDR=%04X VDP_KEY=%u MODE=%u SR=%02X CUR_LINE=%u\n"
            "PORT20=%02X PORT60=%02X RAM_PAGE=%u RAM_PAGES=%u\n"
            "VR0=%02X VR1=%02X VR2=%02X VR3=%02X VR4=%02X VR5=%02X VR6=%02X VR7=%02X\n"
            "NAME=%04X PATTERN=%04X COLOR=%04X SPR_ATTR=%04X SPR_PATTERN=%04X\n"
            "MAP0=%p MAP1=%p MAP2=%p MAP3=%p MAP4=%p MAP5=%p MAP6=%p MAP7=%p\n",
            (unsigned)Z80.pc.w.l, (unsigned)tms.VAddr, (unsigned)tms.VKey,
            (unsigned)tms.Mode, (unsigned)tms.SR, (unsigned)tms.CurLine,
            (unsigned)coleco_port20, (unsigned)coleco_port60,
            (unsigned)RAMPage, (unsigned)RAMPages,
            tms.VR[0], tms.VR[1], tms.VR[2], tms.VR[3],
            tms.VR[4], tms.VR[5], tms.VR[6], tms.VR[7],
            (unsigned)(tms.ChrTab - VDP_Memory),
            (unsigned)(tms.ChrGen - VDP_Memory),
            (unsigned)(tms.ColTab - VDP_Memory),
            (unsigned)(tms.SprTab - VDP_Memory),
            (unsigned)(tms.SprGen - VDP_Memory),
            (void *)MemoryMap[0], (void *)MemoryMap[1],
            (void *)MemoryMap[2], (void *)MemoryMap[3],
            (void *)MemoryMap[4], (void *)MemoryMap[5],
            (void *)MemoryMap[6], (void *)MemoryMap[7]);

        if (length > 0)
            stateFile.write(state,
                length < (int)sizeof(state) ? length : (int)sizeof(state) - 1);
        stateFile.close();
    }
}

struct AdampCpuSample
{
    unsigned short pc;
    unsigned short sp;
    unsigned char bytes[4];
};

// Preserve the instruction history immediately before Recipe/SmartFILER
// unexpectedly enters the directory buffer at page zero.
static void adamp_cpu_crash_trace()
{
    static AdampCpuSample history[256];
    static unsigned historyPos = 0;
    static unsigned historyCount = 0;
    static int postTrigger = -1;
    static QFile traceFile;

    AdampCpuSample sample;
    sample.pc = Z80.pc.w.l;
    sample.sp = Z80.sp.w.l;
    for (unsigned i = 0; i < 4; ++i)
    {
        const unsigned address = (sample.pc + i) & 0xFFFFu;
        sample.bytes[i] = *(MemoryMap[address >> 13] + (address & 0x1FFF));
    }

    history[historyPos] = sample;
    historyPos = (historyPos + 1u) & 255u;
    if (historyCount < 256u) ++historyCount;

    // Restrict the trigger to the final Graphics-II application screen.
    if (postTrigger < 0 &&
        tms.Mode == 2 && tms.VR[1] == 0x48 && sample.pc < 0x0100)
    {
        traceFile.setFileName(
            QCoreApplication::applicationDirPath() + "/ADAMP_CPU_CRASH.log");
        if (traceFile.open(QIODevice::WriteOnly |
                           QIODevice::Text |
                           QIODevice::Truncate))
        {
            const unsigned start = (historyPos + 256u - historyCount) & 255u;
            for (unsigned n = 0; n < historyCount; ++n)
            {
                const AdampCpuSample &s = history[(start + n) & 255u];
                char line[96];
                const int len = std::snprintf(
                    line, sizeof(line),
                    "%s PC=%04X SP=%04X BYTES=%02X %02X %02X %02X\n",
                    (n + 1u == historyCount) ? "TRIGGER" : "BEFORE ",
                    (unsigned)s.pc, (unsigned)s.sp,
                    s.bytes[0], s.bytes[1], s.bytes[2], s.bytes[3]);
                if (len > 0) traceFile.write(line, len);
            }
            traceFile.flush();
            postTrigger = 512;
        }
    }
    else if (postTrigger > 0 && traceFile.isOpen())
    {
        char line[96];
        const int len = std::snprintf(
            line, sizeof(line),
            "AFTER   PC=%04X SP=%04X BYTES=%02X %02X %02X %02X\n",
            (unsigned)sample.pc, (unsigned)sample.sp,
            sample.bytes[0], sample.bytes[1], sample.bytes[2], sample.bytes[3]);
        if (len > 0) traceFile.write(line, len);
        if (--postTrigger == 0)
        {
            traceFile.flush();
            traceFile.close();
        }
    }
}

int breakpoints[MAX_BREAKPOINTS];
int breakpoint_count = 0;
bool coleco_opcode_mapper = false;

extern "C" BYTE adamnet_read_io(int address);
extern bool m_cpm_selected;

extern "C" void coleco_start_cpm_trace(int count)
{
    g_cpm_trace = count;
}

// 80-column mode flag
BYTE coleco_80col_enabled = 0;  // 0=40-col, 1=80-col


// ------------------------------------------------------------
// DKA trace - compile-time switch
// Zet op 1 om alleen DKA bank switching te loggen.
// Zet terug op 0 voor normale build.
// ------------------------------------------------------------
#define DKA_TRACE_BANKS 1

#if DKA_TRACE_BANKS
static int g_dkaBankTraceCount = 0;

static inline void DKA_TRACE_BANK(const QString& msg)
{
    if (g_dkaBankTraceCount < 250)
    {
        qDebug().noquote() << msg;
        g_dkaBankTraceCount++;
    }
}
#else
static inline void DKA_TRACE_BANK(const QString&)
{
}
#endif

// ---------------------------------------------------------------------------
// Active VDP selector
// ---------------------------------------------------------------------------
// 0 = original TMS9918/TMS9928 path
// 1 = separate F18A module path
//
// Important: tms9928a.c/h remain untouched. All switching happens here.
#ifndef COLECO_VDP_TMS
#define COLECO_VDP_TMS   0
#endif
#ifndef COLECO_VDP_F18A
#define COLECO_VDP_F18A  1
#endif

static int  g_vdpType = COLECO_VDP_TMS;
static bool g_f18a_irq_level = false;

static inline unsigned int vdp_current_scanlines()
{
    /*
     * Startup fix for direct F18A boot:
     * the UI defaults to NTSC, but emulator->NTSC can still be false during
     * the first VDP selection/reset. That made F18A start with PAL timing
     * until the user switched TMS -> F18A manually.
     *
     * Therefore: when F18A is active and NTSC was not explicitly pushed yet,
     * use NTSC as the safe default. TMS keeps the original PAL/NTSC logic.
     */
    if (!emulator)
        return TMS9918_LINES;

    if (emulator->NTSC)
        return TMS9918_LINES;

    if (g_vdpType == COLECO_VDP_F18A)
        return TMS9918_LINES;

    return TMS9929_LINES;
}

static inline void f18a_sync_timing()
{
    f18a_set_scanlines(vdp_current_scanlines());
}

void coleco_set_vdp_type(int vdpType)
{
    g_vdpType = (vdpType == COLECO_VDP_F18A) ? COLECO_VDP_F18A : COLECO_VDP_TMS;
    f18a_set_enabled(g_vdpType == COLECO_VDP_F18A);
    g_f18a_irq_level = false;

    /*
     * Belangrijk:
     * Bij startup moet F18A meteen de huidige NTSC/PAL timing krijgen.
     * Anders start F18A met default timing en wordt de snelheid pas correct
     * na TMS -> F18A switch.
     */
    f18a_sync_timing();

}

int coleco_get_vdp_type(void)
{
    return g_vdpType;
}

static inline bool coleco_vdp_is_f18a(void)
{
    return g_vdpType == COLECO_VDP_F18A;
}

static inline void vdp_reset_active(void)
{
    g_f18a_irq_level = false;

    if (g_vdpType == COLECO_VDP_F18A)
    {
        f18a_sync_timing();
        f18a_reset();
        return;
    }

    tms9918_reset();
     tms.ScanLines = emulator->NTSC ? TMS9918_LINES : TMS9929_LINES;
}

static inline void vdp_writedata_active(BYTE value)
{
    if (coleco_vdp_is_f18a())
    {
        f18a_writedata((unsigned char)value);
        return;
    }

    tms9918_writedata(value);
}

static inline void vdp_writectrl_active(BYTE value)
{

    if (coleco_vdp_is_f18a())
    {
        (void)f18a_writectrl((unsigned char)value);
        return;
    }

    adamp_vdp_trace("CTRL-B", value);
    tms9918_writectrl(value);
    adamp_vdp_trace("CTRL-A", value);
}

static inline BYTE vdp_readdata_active(void)
{
    if (coleco_vdp_is_f18a())
        return (BYTE)f18a_readdata();

    return tms9918_readdata();
}

static inline BYTE vdp_readctrl_active(void)
{
    BYTE value = 0;

    if (coleco_vdp_is_f18a())
    {
        /*
         * VDP status read clears the VDP interrupt condition.
         * Important: also clear the Z80 NMI line immediately.
         * Otherwise a stale/pending NMI can still fire after the game
         * already acknowledged the VDP interrupt.
         */
        value = (BYTE)f18a_readctrl();

        g_f18a_irq_level = false;
        z80_set_irq_line(INPUT_LINE_NMI, CLEAR_LINE);

        return value;
    }

    value = tms9918_readctrl();
    adamp_vdp_trace("STATUS", value);

    /*
     * Reading VDP status clears the VDP interrupt latch.
     * Clear the Z80 NMI line immediately as well.
     *
     * DKA bank 6 startup does:
     *   C010 IN A,($BF)
     *
     * If the emulator keeps NMI asserted after this read, an NMI can
     * hit during the C019-C025 VDP register loop and corrupt BC/DE/HL.
     */
    z80_set_irq_line(INPUT_LINE_NMI, CLEAR_LINE);

     return value;
}

static inline void vdp_loop_active(void)
{


    if (coleco_vdp_is_f18a())
    {
        /*
         * F18A CP/M 40-column centering correction.
         *
         * m_cpm_enabled is already set when a CP/M disk is merely loaded.
         * That is too early: Writer would already be shifted before RESET.
         * Therefore also require m_cpm_selected, which only becomes true
         * once the CP/M boot path is actually selected/active.
         *
         * TERM80/80C must never be shifted here.
         */
        const int f18a_cpm40_shift =
            (m_cpm_enabled &&
             m_cpm_selected &&
             !m_tdos_enabled &&
             !f18a_term80_is_enabled()) ? 1 : 0;

        f18a_set_cpm40_shift_left(f18a_cpm40_shift);

        g_f18a_irq_level = (f18a_loop() != 0);
        return;
    }

    tms9918_loop();
}

static inline bool vdp_irq_level_active(void)
{
    if (coleco_vdp_is_f18a())
        return g_f18a_irq_level;

    return ((tms.VR[1] & TMS9918_REG1_IRQ) != 0) &&
           ((tms.SR    & TMS9918_STAT_VBLANK) != 0);
}

extern "C" BYTE *EXP_Memory = 0;

void coleco_clear_debug_flags(void)
{
    // Reset cleanup state bij een volledige reset
    g_cleanup_phase = 0;
    g_cpm_protection_active = false;
}

void DebugUpdate(void)
{
    if (!emulator->stop && !emulator->singlestep)
    {
        uint16_t pc = Z80.pc.w.l;

        // 0) EXECUTE-breakpoints via DebugBridge (EXE / EXE + FLAG)
        if (DEBUG_BRIDGE.checkExecute(pc)) {
            emulator->stop = 1;
            return;
        }

        // 1) Complexe post-execution breakpoints (REG, MEM, FLAGS, CLK, ...)
        if (DEBUG_BRIDGE.checkPostExecutionBreakpoints()) {
            emulator->stop = 1;
            return;
        }

        // 2) Bestaande simpele execute-breakpoints (C-array) blijven werken
        for (int i = 0; i < breakpoint_count; i++) {
            if (pc == breakpoints[i]) {
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
bool g_adamCartridgeMode = false;

FDIDisk Disks[MAX_DISKS] = {};
FDIDisk Tapes[MAX_TAPES] = {};

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

#define Clock       3579545
#define SampleRate  44100

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
// update the 16 colors Coleco
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

//---------------------------------------------------------------------------
// 0 = Coleco, 1 = ADAM
//---------------------------------------------------------------------------
// 0 = Coleco, 1 = ADAM
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
// ================================================================================================
// Setup Adam based on Port60 (Adam Memory) and Port20 (AdamNet)
// Most of this hinges around Port60:
// xxxx xxNN  : Lower address space code.
//       00 = Onboard ROM.  Can be switched between EOS and SmartWriter by output to port 0x20
//       01 = Onboard RAM (lower 32K)
//       10 = Expansion RAM.  Bank switch chosen by port 0x42
//       11 = OS-7 and 24K RAM (ColecoVision mode)
//
// xxxx NNxx  : Upper address space code.
//       00 = Onboard RAM (upper 32K)
//       01 = Expansion ROM (those extra ROM sockets)
//       10 = Expansion RAM.  Bank switch chosen by port 0x42
//       11 = Cartridge ROM (ColecoVision mode).
//
// And Port20: bit 1 (0x02) to determine if EOS.ROM is present on top of WRITER.ROM
// ================================================================================================
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

    // ----------------------------------
    // Configure lower 32K of memory
    // ----------------------------------
    if ((coleco_port60 & 0x03) == 0x00) // WRITER.ROM (and possibly EOS.ROM)
    {
        adam_ram_lo = 0; adam_ram_lo_exp = 0;
        MemoryMap[0] = BIOS_Memory + 0x0000;
        MemoryMap[1] = BIOS_Memory + 0x2000;
        MemoryMap[2] = BIOS_Memory + 0x4000;
        MemoryMap[3] = (coleco_port20 & 0x02) ? BIOS_Memory + 0x8000  // EOS
                                              : BIOS_Memory + 0x6000; // Last block of SMARTWriter
    }
    else if ((coleco_port60 & 0x03) == 0x01) // Onboard (Intrinsic) RAM
    {
        adam_ram_lo = 1; adam_ram_lo_exp = 0;
        MemoryMap[0] = RAM_Memory + 0x0000;
        MemoryMap[1] = RAM_Memory + 0x2000;
        MemoryMap[2] = RAM_Memory + 0x4000;
        MemoryMap[3] = RAM_Memory + 0x6000;
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

    // ----------------------------------
    // Configure upper 32K of memory
    // ----------------------------------
    if ((coleco_port60 & 0x0C) == 0x00) // Onboard (Intrinsic) RAM
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
    else if ((coleco_port60 & 0x0C) == 0x08) // Expanded RAM
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

    if (m_cpm_enabled && !m_tdos_enabled) {
        // CP/M expects the upper 32K to stay in intrinsic RAM during loader/BIOS activity.
        // A tiny PC window (C800-CC00) is too fragile and lets the mapping flip away mid-boot.
        MemoryMap[4] = RAM_Memory + 0x8000;
        MemoryMap[5] = RAM_Memory + 0xA000;
        MemoryMap[6] = RAM_Memory + 0xC000;
        MemoryMap[7] = RAM_Memory + 0xE000;
    }

    if (resetAdamNet)  ResetPCB();
}

//---------------------------------------------------------------------------
static bool coleco_is_dka2018()
{
    return emulator && emulator->cardcrc == 0x45345709;
}

static void coleco_apply_dka2018_mapping()
{
    if (!coleco_is_dka2018())
        return;

    if (emulator->currentMachineType == MACHINEADAM)
        return;

    emulator->SGM = 1;
    emulator->romCartridgeType = ROMCARTRIDGEMEGA;
    cvbank_sgm_write_control_port(0x01);   // port53 = 1, sgm_enable = 1
    coleco_megacart = 0x07;

    coleco_megasize = 8;
    coleco_mega_layout = 2;
    coleco_megabank = 6;

    // $8000-$BFFF = fixed last bank, bank 7
    MemoryMap[4] = ROM_Memory + 0x1C000;
    MemoryMap[5] = ROM_Memory + 0x1E000;

    // $C000-$FFFF = boot bank, bank 6
    MemoryMap[6] = ROM_Memory + 0x18000;
    MemoryMap[7] = ROM_Memory + 0x1A000;


}
//---------------------------------------------------------------------------
void coleco_reset(void)
{
#if DKA_TRACE_BANKS
    g_dkaBankTraceCount = 0;
#endif

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

        if (emulator->biosnodelay)
        {
            RAM_Memory[159 * 32 + 17] = 0x00;
            RAM_Memory[159 * 32 + 18] = 0x00;
            RAM_Memory[159 * 32 + 19] = 0x00;
        }
    }

    if (emulator->currentMachineType != MACHINEADAM)
    {
        // Randomiseer alleen de eerste 1KB
        for (int i = 0; i < 0x0400; i++)
        {
            BYTE r = rand() % 256;

            // Spiegel deze waarde over het hele 8KB blok (0x6000-0x7FFF)
            for (int mirror = 0; mirror < 8; mirror++)
            {
                RAM_Memory[0x6000 + i + (mirror * 0x0400)] = r;
            }
        }
    }

    /*
     * Niet hier al definitief de cart mappen.
     * cvbank_sgm_reset_runtime_state(), cvbank_sgm_init_ports_for_current_machine()
     * en coleco_setupsgm() kunnen MemoryMap[4..7] opnieuw aanpassen.
     * De finale cartridge mapping gebeurt onderaan.
     */

    cvbank_sgm_reset_runtime_state();
    cvbank_sgm_init_ports_for_current_machine();

    // ADAM memory init
    if (emulator->currentMachineType == MACHINEADAM)
    {
        adam_ram_lo = 0;
        adam_ram_hi = 0;
        adam_ram_lo_exp = 0;
        adam_ram_hi_exp = 0;
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
    case 0x62DACF07:
        emulator->typebackup = EEP24C256; // Boxxle
        break;

    case 0xDDDD1396:
        emulator->typebackup = EEP24C08;
        break;

    case 0xFEE15196:
    case 0x1053F610:
    case 0x60D6FD7D:
    case 0x37A9F237:
        emulator->typebackup = EEPSRAM;
        break;

    case 0xEF25AF90:
    case 0xC2E7F0E0:
        cvbank_sgm_set_forced_disabled(1);
        break;
    }

    // VDP reset
    vdp_reset_active();

    // PSG's
    sn76489_init(Clock, SampleRate);
    ay8910_init(Clock, SampleRate);

    // EEPROM reset
    if (emulator->typebackup != NOBACKUP && emulator->typebackup != EEPSRAM)
    {
        c24xx_reset(
            SRAM_Memory,
            emulator->typebackup == EEP24C08 ? C24XX_24C08 : C24XX_24C256
            );
    }

    z80_reset();

    // Vars
    tStatesCount = 0;

    // Input init
    coleco_joymode = 0;
    coleco_joystat = 0x00000000;

    coleco_spinpos[0] = 0;
    coleco_spinpos[1] = 0;

    coleco_spinrecur[0] = 0;
    coleco_spinrecur[1] = 0;

    coleco_spinparam[0] = 0;
    coleco_spinparam[1] = 0;

    coleco_spinstate[0] = 0;
    coleco_spinstate[1] = 0;

    // --------------------------------------------------------------------
    // EIND-MAPPING
    // --------------------------------------------------------------------
    // Eerst SGM / ADAM mapping toepassen.
    // Daarna pas cartridge mapping opnieuw zetten.
    // Anders overschrijft SGM setup de cart mapping.
    // --------------------------------------------------------------------

    if (emulator->currentMachineType == MACHINEADAM)
    {
        coleco_setadammemory(true);
    }
    else
    {
        // Zet BIOS / SGM RAM mapping correct.
        coleco_setupsgm();

        /*
         * Belangrijk voor SGM cartridges zoals Castle.
         * emulator->SGM=true betekent: de cartridge heeft SGM nodig.
         * Zonder deze call blijft sgm_enable=0 en port53=00.
         */
        if (emulator->SGM)
        {
            cvbank_sgm_write_control_port(0x01);
        }

        // Daarna pas cartridge mapping herstellen.
        if (emulator->romCartridgeType == ROMCARTRIDGEOPCODE)
        {
            // Opcode Super Game Cart
            banking_apply_boot_mapping();

        }
        else if (coleco_megacart)
        {
            if (emulator->typebackup == EEP24C08 ||
                emulator->typebackup == EEP24C256)
            {
                // --------------------------------------------------------
                // Boxxle / EEPROM cart mapping
                // $8000-$BFFF = fixed bank 0
                // $C000-$FFFF = selectable bank, default bank 0
                // --------------------------------------------------------

                MemoryMap[4] = ROM_Memory + 0x0000;
                MemoryMap[5] = ROM_Memory + 0x2000;
                MemoryMap[6] = ROM_Memory + 0x0000;
                MemoryMap[7] = ROM_Memory + 0x2000;

                coleco_megabank = 0;

            }
            else
            {
                // --------------------------------------------------------
                // Gewone MegaCart / SGM MegaCart
                // Gearcoleco MegaCart model:
                // $8000-$BFFF = fixed last 16K bank
                // $C000-$FFFF = switchable bank 0
                // --------------------------------------------------------

                const unsigned int lastBase =
                    (unsigned int)coleco_megacart * 0x4000;

                MemoryMap[4] = ROM_Memory + lastBase;
                MemoryMap[5] = MemoryMap[4] + 0x2000;
                MemoryMap[6] = ROM_Memory + 0x0000;
                MemoryMap[7] = MemoryMap[6] + 0x2000;

                coleco_megabank = 0;

            }
        }
        else
        {
            /*
             * Standard Coleco / SGM cartridge mapping.
             * Dit moet NA coleco_setupsgm(), anders kan SGM setup de cart mapping
             * opnieuw overschrijven.
             *
             * Nodig voor gewone 32K/SGM ROMs zoals Castle.
             */
            MemoryMap[4] = ROM_Memory + 0x0000;  // $8000-$9FFF
            MemoryMap[5] = ROM_Memory + 0x2000;  // $A000-$BFFF
            MemoryMap[6] = ROM_Memory + 0x4000;  // $C000-$DFFF
            MemoryMap[7] = ROM_Memory + 0x6000;  // $E000-$FFFF

            coleco_megabank = 0;
            coleco_mega_layout = 0;

        }

        // Debug log PAS NA de finale mapping.
    }

    /*
     * DKA krijgt zijn speciale mapping pas helemaal op het einde.
     * Voor andere games doet deze functie niets.
     */
    coleco_apply_dka2018_mapping();

}
//---------------------------------------------------------------------------
void coleco_reset_and_restart_bios()
{
    // --------------------------------------------------------------------
    // 1. Hardware / machine memory mapping
    // --------------------------------------------------------------------
    // Eerst de basis BIOS / ADAM / SGM mapping zetten.
    // Daarna pas cartridge mapping opnieuw herstellen.
    // --------------------------------------------------------------------

    if (emulator->currentMachineType == MACHINEADAM)
    {
        coleco_port60 = g_adamCartridgeMode ? 0x0F : 0x00;
        coleco_setadammemory(true);
    }
    else
    {
        cvbank_sgm_init_ports_for_current_machine();
        coleco_setupsgm();

        /*
         * Belangrijk voor SGM cartridges.
         * Ook bij restart moet SGM effectief enabled blijven.
         */
        if (emulator->SGM)
        {
          cvbank_sgm_write_control_port(0x01);
        }
    }

    // --------------------------------------------------------------------
    // 2. Cartridge mapping opnieuw herstellen
    // --------------------------------------------------------------------

    if (emulator->currentMachineType != MACHINEADAM)
    {
        if (emulator->romCartridgeType == ROMCARTRIDGEOPCODE)
        {
            banking_apply_boot_mapping();

        }
        else if (coleco_is_dka2018())
        {
            coleco_apply_dka2018_mapping();
        }
        else if (coleco_megacart)
        {
            if (emulator->typebackup == EEP24C08 ||
                emulator->typebackup == EEP24C256)
            {
                // --------------------------------------------------------
                // Boxxle / EEPROM cart mapping
                // $8000-$BFFF = fixed bank 0
                // $C000-$FFFF = selectable bank, default bank 0
                // --------------------------------------------------------

                MemoryMap[4] = ROM_Memory + 0x0000;
                MemoryMap[5] = ROM_Memory + 0x2000;
                MemoryMap[6] = ROM_Memory + 0x0000;
                MemoryMap[7] = ROM_Memory + 0x2000;

                coleco_megabank = 0;

            }
            else
            {
                // --------------------------------------------------------
                // Gewone MegaCart / SGM MegaCart
                // Gearcoleco MegaCart model:
                // $8000-$BFFF = fixed last 16K bank
                // $C000-$FFFF = switchable bank 0
                // --------------------------------------------------------

                const unsigned int lastBase =
                    (unsigned int)coleco_megacart * 0x4000;

                MemoryMap[4] = ROM_Memory + lastBase;
                MemoryMap[5] = MemoryMap[4] + 0x2000;
                MemoryMap[6] = ROM_Memory + 0x0000;
                MemoryMap[7] = MemoryMap[6] + 0x2000;

                coleco_megabank = 0;

            }
        }
        else
        {
            /*
             * Standard Coleco / SGM cartridge mapping.
             * Dit moet ook bij reset/restart na coleco_setupsgm().
             */
            MemoryMap[4] = ROM_Memory + 0x0000;
            MemoryMap[5] = ROM_Memory + 0x2000;
            MemoryMap[6] = ROM_Memory + 0x4000;
            MemoryMap[7] = ROM_Memory + 0x6000;

            coleco_megabank = 0;
            coleco_mega_layout = 0;

        }

        // Debug log PAS NA de finale mapping.
    }

    // --------------------------------------------------------------------
    // 3. CP/M 80-column smartkey rescan
    // --------------------------------------------------------------------

    if (coleco_vdp_is_f18a() && m_cpm_enabled && !m_tdos_enabled)
    {
        f18a_term80_cpm_force_smartkey_rescan();
    }

    // --------------------------------------------------------------------
    // 4. Reset VDP + CPU
    // --------------------------------------------------------------------

    vdp_reset_active();
    z80_reset();
}
//---------------------------------------------------------------------------
void coleco_hide_current_vdp_sprites(void)
{
    if (!emulator || emulator->currentMachineType != MACHINEADAM)
        return;

    if (coleco_vdp_is_f18a())
    {
        f18a_hide_current_sprites();
        return;
    }

    if (!tms.SprTab)
        return;

    const int sat = int(tms.SprTab - VDP_Memory);

    if (sat < 0 || sat + 128 > 0x4000)
        return;

    VDP_Memory[sat + 0] = 0xD0; // einde sprite list
    VDP_Memory[sat + 1] = 0x00;
    VDP_Memory[sat + 2] = 0x00;
    VDP_Memory[sat + 3] = 0x00;
}
//---------------------------------------------------------------------------
void coleco_hardreset(void)
{
    if (emulator->romCartridgeType == ROMCARTRIDGEOPCODE) banking_supergamecart_saveflash();

    // 1) Maak de cartbuffer “open bus”: 0xFF
    memset(ROM_Memory, 0xFF, MAX_CART_SIZE * 1024);   // 512 KiB max. cartsize

    // 2) Reset megacart/bankswitch state
    banking_reset_state();

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
    vdp_reset_active();
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


    FILE *fbios = fopen(filename, "rb");
    if (!fbios) {
        qWarning() << "[BIOS] loadBios: fopen FAILED for" << filename
                   << "errno=" << errno << "(" << strerror(errno) << ")";
        return 0;
    }

    // File size debug
    fseek(fbios, 0, SEEK_END);
    //long fsize = ftell(fbios);
    fseek(fbios, 0, SEEK_SET);

    size_t bytes_read = fread((void*)memory, 1, (size_t)sizerm, fbios);
    fclose(fbios);


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
    }

    // Flags resetten (optioneel)
    if (index == 0) s_colecoBiosExternal = false;
    if (index == 1) s_eosBiosExternal    = false;
    if (index == 2) s_writerBiosExternal = false;

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
   // coleco_megasize = 2;
    //coleco_megacart = 0;
    //emulator->romCartridgeType = ROMCARTRIDGENONE;

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
//---------------------------------------------------------------------------
void coleco_WriteByte(unsigned int Address, int Data)
{
    Address &= 0xFFFF;
    Data    &= 0xFF;

    // ------------------------------------------------------------
    // ADAM MODUS
    // ------------------------------------------------------------
    if (emulator->currentMachineType == MACHINEADAM)
    {
        // Adam-geheugen heeft geen 1K Coleco mirror.
        // Het is een platte 64K/128K map.
        // We schrijven naar RAM en laten de PCB meeluisteren.

        if ((Address < 0x8000) && adam_ram_lo)
        {
            RAM_Memory[Address] = (BYTE)Data;

            if (PCBTable[Address])
                WritePCB(Address, Data);

            return;
        }

        if ((Address >= 0x8000) && adam_ram_hi)
        {
            RAM_Memory[Address] = (BYTE)Data;

            if (PCBTable[Address])
                WritePCB(Address, Data);

            return;
        }

        if (adam_ram_lo_exp || adam_ram_hi_exp)
        {
            // Expansion RAM via huidige MemoryMap.
            *(MemoryMap[Address >> 13] + (Address & 0x1FFF)) = (BYTE)Data;
            return;
        }

        // Schrijven naar ROM/open gebied in ADAM negeren.
        return;
    }

    // ------------------------------------------------------------
    // COLECO MODUS
    // ------------------------------------------------------------
    // SGM / standaard Coleco RAM banking wordt in cvbank afgehandeld.
    //
    // Belangrijk:
    // In SGM mode is $2000-$7FFF lineaire RAM.
    // In gewone Coleco mode wordt $6000-$7FFF gespiegeld als 1K RAM.
    //
    // Dit moet vóór EEPROM/MegaCart gebeuren.
    // ------------------------------------------------------------
    if (cvbank_sgm_write_ram(Address, Data))
        return;

    // ------------------------------------------------------------
    // SRAM backup carts
    // ------------------------------------------------------------
    if ((Address >= 0xE000) && (Address < 0xE800))
    {
        if (emulator->typebackup == EEPSRAM)
        {
            RAM_Memory[Address + 0x800] = (BYTE)Data;
            return;
        }
    }

    // ------------------------------------------------------------
    // EEPROM-backed carts, zoals Boxxle
    // ------------------------------------------------------------
    // Boxxle mag NIET door de gewone MegaCart hotspot-logica gaan.
    // Daarom staat dit vóór gewone MegaCart writes.
    // ------------------------------------------------------------
    if (emulator->currentMachineType != MACHINEADAM &&
        (emulator->typebackup == EEP24C08 ||
         emulator->typebackup == EEP24C256))
    {
        if ((Address == 0xFF90) ||
            (Address == 0xFFA0) ||
            (Address == 0xFFB0))
        {
            // Boxxle selecteert welke 16K bank zichtbaar is in $C000-$FFFF.
            int bank = (Address >> 4) & 0x03;

            MemoryMap[6] = ROM_Memory + (bank * 0x4000);
            MemoryMap[7] = MemoryMap[6] + 0x2000;

            return;
        }

        switch (Address)
        {
        case 0xFFC0:
            c24xx_write(c24.Pins & ~C24XX_SCL);
            return;

        case 0xFFD0:
            c24xx_write(c24.Pins | C24XX_SCL);
            return;

        case 0xFFE0:
            c24xx_write(c24.Pins & ~C24XX_SDA);
            return;

        case 0xFFF0:
            c24xx_write(c24.Pins | C24XX_SDA);
            return;

        default:
            // Andere writes naar EEPROM-cart ROM gebied negeren.
            return;
        }
    }

    // ------------------------------------------------------------
    // Opcode Super Game Cart / gewone MegaCart writes
    // ------------------------------------------------------------
    if (coleco_megacart)
    {
        if (emulator->romCartridgeType == ROMCARTRIDGEOPCODE)
        {
            if (Address >= 0x8000)
            {
                superGameCartWrite(Address, (BYTE)Data);
                return;
            }
        }
        else
        {
            // Gearcoleco / DKA MegaCart hotspot writes.
            if (Address >= 0xFFC0)
            {
                if (coleco_is_dka2018() || coleco_mega_layout == 2)
                {
                    /*
             * DKA 2018 write hotspot layout:
             * FFC0-FFC7 -> bank 0
             * FFC8-FFCF -> bank 1
             * FFD0-FFD7 -> bank 2
             * FFD8-FFDF -> bank 3
             * FFE0-FFE7 -> bank 4
             * FFE8-FFEF -> bank 5
             * FFF0-FFF7 -> bank 6
             * FFF8-FFFF -> bank 7
             */
                    BYTE bank = (BYTE)(((Address - 0xFFC0) >> 3) & (coleco_megasize - 1));

                    MemoryMap[6] = ROM_Memory + ((unsigned int)bank * 0x4000);
                    MemoryMap[7] = MemoryMap[6] + 0x2000;
                    coleco_megabank = bank;

                    return;
                }

                megacart_bankswitch((BYTE)(Address & coleco_megacart));
                return;
            }
        }
    }

    // Andere writes naar ROM/open bus negeren.
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
//---------------------------------------------------------------------------
BYTE coleco_ReadByte(int Address)
{
    Address &= 0xFFFF;

    // ------------------------------------------------------------
    // Opcode Super Game Cart read
    // ------------------------------------------------------------
    if (emulator &&
        emulator->currentMachineType != MACHINEADAM &&
        coleco_megacart &&
        emulator->romCartridgeType == ROMCARTRIDGEOPCODE)
    {
        return superGameCartRead(Address);
    }

    // ------------------------------------------------------------
    // MegaCart hotspot bank switching, READ based
    // ------------------------------------------------------------
    // Stabiele oude logica:
    //   1) eerst byte lezen uit huidige mapping
    //   2) daarna bank switchen
    //   3) oldValue teruggeven
    //
    // Belangrijk:
    // de read zelf geeft de byte terug die vóór de switch zichtbaar was.
    // De bankswitch is het side-effect.
    // ------------------------------------------------------------
    if (emulator &&
        emulator->currentMachineType != MACHINEADAM &&
        coleco_megacart &&
        emulator->typebackup != EEP24C08 &&
        emulator->typebackup != EEP24C256 &&
        emulator->romCartridgeType == ROMCARTRIDGEMEGA)
    {
        if (Address >= 0xFFC0)
        {
            BYTE oldValue = *(MemoryMap[Address >> 13] + (Address & 0x1FFF));
            BYTE bank = 0;

            if (coleco_mega_layout == 2)
            {
                // --------------------------------------------------------
                // DKA / 8-byte hotspot layout:
                //
                // FFC0-FFC7 -> bank 0
                // FFC8-FFCF -> bank 1
                // FFD0-FFD7 -> bank 2
                // FFD8-FFDF -> bank 3
                // FFE0-FFE7 -> bank 4
                // FFE8-FFEF -> bank 5
                // FFF0-FFF7 -> bank 6
                // FFF8-FFFF -> bank 7
                //
                // MAAR:
                // In DKA staat op $8090:
                //
                //   8090  LD A,($FFF4)
                //   8093  CALL $C000
                //
                // Daar wil de game duidelijk naar bank 4 schakelen
                // en daarna $C000 uitvoeren in die bank.
                //
                // Daarom alleen deze exacte situatie speciaal behandelen.
                // --------------------------------------------------------
                if (emulator &&
                    emulator->cardcrc == 0x45345709 &&
                    Address == 0xFFF4 &&
                    Z80.pc.w.l >= 0x8090 &&
                    Z80.pc.w.l <= 0x8093)
                {
                    bank = 4;

                }
                else
                {
                    bank = (BYTE)(((Address - 0xFFC0) >> 3) &
                                   (coleco_megasize - 1));
                }
            }
            else
            {
                // Gewone MegaCart layout
                bank = (BYTE)((Address - 0xFFC0) &
                               (coleco_megasize - 1));
            }


#if DKA_TRACE_BANKS
            if (emulator &&
                emulator->cardcrc == 0x45345709 &&
                coleco_megabank != bank)
            {
                DKA_TRACE_BANK(QString(
                                   "[DKA HOTSPOT READ] PC=%1 Address=%2 oldValue=%3 oldBank=%4 newBank=%5 layout=%6 M4=%7 M6=%8 RAM2000=%9 RAM2001=%10 RAM7002=%11 RAM7009=%12 RAM700A=%13")
                                   .arg(Z80.pc.w.l, 4, 16, QChar('0'))
                                   .arg(Address, 4, 16, QChar('0'))
                                   .arg(oldValue, 2, 16, QChar('0'))
                                   .arg(coleco_megabank)
                                   .arg(bank)
                                   .arg(coleco_mega_layout)
                                   .arg((MemoryMap[4] >= ROM_Memory &&
                                         MemoryMap[4] < ROM_Memory + MAX_CART_SIZE * 1024)
                                            ? QString::number((quintptr)(MemoryMap[4] - ROM_Memory), 16)
                                            : QString("NOT_ROM"))
                                   .arg((MemoryMap[6] >= ROM_Memory &&
                                         MemoryMap[6] < ROM_Memory + MAX_CART_SIZE * 1024)
                                            ? QString::number((quintptr)(MemoryMap[6] - ROM_Memory), 16)
                                            : QString("NOT_ROM"))
                                   .arg(RAM_Memory[0x2000], 2, 16, QChar('0'))
                                   .arg(RAM_Memory[0x2001], 2, 16, QChar('0'))
                                   .arg(RAM_Memory[0x7002], 2, 16, QChar('0'))
                                   .arg(RAM_Memory[0x7009], 2, 16, QChar('0'))
                                   .arg(RAM_Memory[0x700A], 2, 16, QChar('0')));
            }
#endif

            megacart_bankswitch(bank);

            return oldValue;
        }
    }

    // ------------------------------------------------------------
    // EEPROM-backed carts zoals Boxxle
    // ------------------------------------------------------------
    if (emulator &&
        emulator->currentMachineType != MACHINEADAM &&
        ((emulator->typebackup == EEP24C08) ||
         (emulator->typebackup == EEP24C256)) &&
        Address == 0xFF80)
    {
        return c24xx_read();
    }

    // ------------------------------------------------------------
    // ADAM AdamNet side-effect
    // ------------------------------------------------------------
    if (emulator &&
        emulator->currentMachineType == MACHINEADAM &&
        PCBTable[Address])
    {
        (void)ReadPCB(Address);
    }

    // ------------------------------------------------------------
    // Normale memory mapped read
    // ------------------------------------------------------------
    return *(MemoryMap[Address >> 13] + (Address & 0x1FFF));
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
//  Out42 (Expansion RAM Page Select) ---
static void Out42(BYTE Val)
{
    BYTE a = Val & RAMMask;

    if (RAMPages == 0) return;

    a = (BYTE)(a % RAMPages);       // wrap

    if (a != RAMPage) {
        RAMPage = a;
        coleco_setadammemory(false);
    }
}
//---------------------------------------------------------------------------
void coleco_writeport(int Address, int Data, int * /**tstates*/)
{
    bool resetadam = 0;

    Address &= 0xFF; // 8-bit poort adres (hou alleen de onderste 8 bits over van de integer)

    switch(Address & 0xE0) // poorten in blokken van 32 groeperen. (0x00/0x20/0x40/0x60/0x80/0xA0/0xC0/0xE0)
    {
    case 0x00: // 0x00 - 0x1F: Unused
        break;

    case 0x20: // 0x20 - 0x3F: AdamNet Control
        resetadam=(coleco_port20 & 1) && ((Data & 1) == 0);
        coleco_port20=Data;
        if (emulator->currentMachineType == MACHINEADAM) coleco_setadammemory(resetadam);
        else if(emulator->SGM) cvbank_sgm_apply_mapping();
        break;

    case 0x40: // 0x40-0x5F: Printer / SGM Sound / SGM Control
        if((emulator->currentMachineType == MACHINEADAM)&&(Address==0x40)) Printer(Data);
        else if ((emulator->currentMachineType == MACHINEADAM)&&Address==0x42) Out42(Data); // <<< NIEUW: EXPANSION RAM PAGE SELECT
        else if(emulator->SGM)
        {
            if(Address==0x53) { cvbank_sgm_write_control_port((BYTE)Data); }
            else if (Address==0x50) ay8910_write(0,Data); // Control data
            else if (Address==0x51) ay8910_write(1,Data); // Write data
        }
        break;

    case 0x60: // 0x60 - 0x7F: Memory Control
    {
        int v = Data & 0xFF;

        if (emulator->currentMachineType == MACHINEADAM) {
            coleco_port60 = v;
            coleco_setadammemory(resetadam);
        }
        else if (emulator->SGM) {
            cvbank_sgm_write_memory_port((BYTE)v);
        }
        else {
            coleco_port60 = v;
        }
        break;
    }

    case 0x80: // 0x80 - 0x9F: Controller 1 Write (Mode Select)
        coleco_io_write((uint8_t)Address, (uint8_t)Data);
        break;

    case 0xA0: // 0xA0 - 0xBF: VDP Write (Video Display Processor)
        coleco_updatetms=1;
        if((Address & 0x01)==0) // Even adres
        { vdp_writedata_active((BYTE)Data); }
        else // Oneven adres
        {

             vdp_writectrl_active((BYTE)Data);
            // Als we in C80 modus zitten, moeten we
            // soms specifieke register-instellingen forceren die
            // T-DOS verwacht voor een 80-koloms lineaire buffer.
            if (!coleco_vdp_is_f18a() && coleco_80col_enabled) {
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
BYTE coleco_readport(int Address, int * /*tstates*/)
{
    Address &= 0xFF;

    auto RET = [&](BYTE v) -> BYTE { return v; };

    // ADAM-specifieke poorten in Coleco-modus idle.
    if (emulator->currentMachineType != MACHINEADAM) {
        if ((Address & 0xE0) == 0x20 || (Address & 0xE0) == 0x60) {
            return RET(0xFF);
        }
    }

    switch(Address & 0xE0)
    {
    case 0x00:
        break;

    case 0x20:
        if (emulator->currentMachineType == MACHINEADAM)
            return RET(coleco_port20);
        break;

    case 0x40:
        if (emulator->currentMachineType == MACHINEADAM)
        {
            if (Address == 0x40)
                return RET(0xFF);

            if (Address == 0x42 || Address == 0x43)
                return RET(coleco_port20);

            if (coleco_80col_enabled && (Address == 0x52 || Address == 0x53))
                return RET(0x80);

            return RET(idleDataBus);
        }
        else
        {
            if (Address == 0x52)
                return RET(ay8910_read());
        }
        break;

    case 0x60:
        if (emulator->currentMachineType == MACHINEADAM)
            return RET(coleco_port60);
        break;

    case 0x80:
        if (Address == 0x98 || (Address & 0x01) == 0)
            return RET(vdp_readdata_active());
        else
            return RET(vdp_readctrl_active());

    case 0xA0:
        if ((Address & 0x01) == 0)
            return RET(vdp_readdata_active());
        else
            return RET(vdp_readctrl_active());

    case 0xC0:
        if (emulator->currentMachineType == MACHINEADAM)
            return RET(idleDataBus);
        break;

    case 0xE0:
    {
        // ADAM: E0-E3 blijft AdamNet.
        // Maar controller/keypad-poorten zoals FC/FF mogen NIET idle worden,
        // want ADAM games lezen daar de Coleco-style controller/keypad.
        if (emulator->currentMachineType == MACHINEADAM)
        {
            if (Address >= 0xE0 && Address <= 0xE3)
                return RET(adamnet_read_io(Address));

            if (Address == 0xFC || Address == 0xFF ||
                Address == 0xBE || Address == 0xBF)
            {
                return RET(coleco_io_read((uint8_t)Address));
            }

            return RET(idleDataBus);
        }

        BYTE digital_result = coleco_io_read((uint8_t)Address);

        /*
         * TEST DKA:
         * DKA verwacht op poort $FC een low nibble $09.
         * Zonder dit krijgt hij $7F, dus low nibble $0F,
         * waardoor I op 0 blijft en hij opnieuw naar C226 springt.
         */
        // if (emulator &&
        //     emulator->cardcrc == 0x45345709 &&
        //     emulator->currentMachineType != MACHINEADAM &&
        //     Address == 0xFC) // &&
        //     //Z80.pc.w.l == 0xC09B)
        // {
        //     return RET((BYTE)((digital_result & 0xF0) | 0x09));
        // }

        if ((Address & 0x02) == 0)
        {
            if (Address == 0xE0)
                return RET((BYTE)(coleco_spinpos[0] >> 8));

            if (Address == 0xE1)
            {
                uint8_t spinner_lsb = (uint8_t)(coleco_spinpos[0] & 0xFF);
                uint8_t digital_status = digital_result & 0x0F;
                uint8_t paddle_high_bits = (spinner_lsb & 0xF0);

                return RET(paddle_high_bits | digital_status);
            }
        }

        return RET(digital_result);
    }

    }

    return RET(idleDataBus);
}
//---------------------------------------------------------------------------
int coleco_contend(int /*Address*/, int /*states*/, int time) { return(time); }

//---------------------------------------------------------------------------
// --- Spinner input handler ---
// Deze functie ontvangt de analoge stickwaarde via de Qt slot.
void coleco_setSpinner(int player, int analogValue)
{
    if (player < 0 || player > 1) return; // enkel player 1 toelaten,

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
    if (MaxScanLen <= 0)
        MaxScanLen = 228;

    int CurScanLine_len = MaxScanLen;
    int tstotal = 0;

    /*
     * NMI state:
     *
     * Belangrijk voor deze Z80-core:
     * z80_set_irq_line(INPUT_LINE_NMI, ASSERT_LINE) mag maar één edge geven
     * zolang de VDP IRQ-level actief blijft.
     *
     * Dus:
     *   - ASSERT wanneer VDP IRQ-level actief wordt
     *   - NIET direct clearen na z80_checknmi()
     *   - CLEAR pas wanneer de VDP IRQ-level wegvalt
     */
    static int nmi_active = 0;

    if (!emulator->stop && !emulator->singlestep)
    {
        // Verwerk pending NMI aan het begin van de scanline.
        ts = z80_checknmi();

        CurScanLine_len -= ts;
        tstotal += ts;

        // CPU uitvoeren voor deze scanline.
        do
        {
            DebugUpdate();

            if (emulator->stop || emulator->singlestep)
                break;

            coleco_paddle();

            DEBUG_BRIDGE.setCurrentOpcodeStartPC(Z80.pc.w.l);

            if (coleco_get_vdp_type() == COLECO_VDP_F18A)
            {
                if (m_cpm_enabled && m_tdos_enabled)
                {
                    f18a_term80_tdos_before_opcode();
                }
                else
                {
                    f18a_term80_cpm_before_opcode(
                        (uint16_t)Z80.pc.w.l,
                        (uint8_t)Z80.bc.b.l
                        );
                }
            }
            else
            {
                cpm80_before_opcode(
                    (uint16_t)Z80.pc.w.l,
                    (uint8_t)Z80.bc.b.l
                    );
            }

            adamp_cpu_crash_trace();
            ts = z80_do_opcode();

            CurScanLine_len -= ts;
            frametstates   += ts;
            tStatesCount   += ts;
            tstotal        += ts;

        } while (CurScanLine_len > 0 &&
                 !emulator->stop &&
                 !emulator->singlestep);
    }

    // ------------------------------------------------------------
    // VDP scanline update
    // ------------------------------------------------------------
    vdp_loop_active();
    if (!coleco_vdp_is_f18a() && tms.CurLine == TMS9918_END_LINE)
    {
        static unsigned diagnosticFrame = 0;
        adamp_vdp_trace("FRAME", 0);
        if ((++diagnosticFrame % 60u) == 0u)
            adamp_dump_machine_state();
    }

    const bool vdp_irq_level = vdp_irq_level_active();

    // ------------------------------------------------------------
    // VDP IRQ -> Z80 NMI
    // ------------------------------------------------------------
    if (!emulator->stop && !emulator->singlestep)
    {
        if (vdp_irq_level)
        {
            if (!nmi_active)
            {
                z80_set_irq_line(INPUT_LINE_NMI, ASSERT_LINE);
                nmi_active = 1;
            }
        }
        else
        {
            if (nmi_active)
            {
                z80_set_irq_line(INPUT_LINE_NMI, CLEAR_LINE);
                nmi_active = 0;
            }
        }
    }

    // ------------------------------------------------------------
    // ADAM / CP/M drive cache maintenance
    // ------------------------------------------------------------
    if (m_cpm_enabled && !m_tdos_enabled)
    {
        adam_drive_local_cache_check();
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
extern "C" {
int* sn76489_get_regs();
 unsigned char*  ay8910_get_regs();
void sn76489_restore_reg(int r, uint8_t val);
void ay8910_set_reg(int reg, uint8_t val);
}

//---------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------------
extern "C" {
// --- Z80 CPU Callbacks ---

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

     if (coleco_get_vdp_type() == COLECO_VDP_F18A) {
          if (m_cpm_enabled && m_tdos_enabled)
                   f18a_term80_tdos_before_opcode();
         else
                   f18a_term80_cpm_before_opcode((uint16_t)Z80.pc.w.l, (uint8_t)Z80.bc.b.l);
        }
     else {
        cpm80_before_opcode((uint16_t)Z80.pc.w.l, (uint8_t)Z80.bc.b.l);
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

void coleco_set_bios_paths(const char* coleco_path, const char* eos_path, const char* writer_path)
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

// Zorg dat deze signatures exact matchen met z80.h
extern unsigned int cpu_readmem16(unsigned int address);
extern void cpu_writemem16(unsigned int address, unsigned int value);

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
void z80_wrapper_write(unsigned int address, unsigned char value)
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

unsigned char z80_wrapper_read(unsigned int address)
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

int coleco_virtual_cpm_diskboot(const char* cpmTapeDdpPath,
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
            return 1;
        }
    }

    // 2) Disk image laden/mounten (dit wordt straks A:)
    if (diskDskPath && *diskDskPath)
    {
        // ChangeDisk retourneert 1 bij succes, 0 bij fout
        if (!ChangeDisk((byte)diskDrive, diskDskPath))
        {
            return 2;
        }
    }

    // 4) Hard reset zodat de bestaande CP/M boot flow (die bij jou al werkt) afgaat
    coleco_reset();

    return 0;
}
// --- Einde extern C ----------------------------------------------------------------
}
