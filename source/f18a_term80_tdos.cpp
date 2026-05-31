#include "f18a_term80_tdos.h"
#include "f18a_term80.h"
#include "cv.h"

extern "C" {
#include "tms9928a.h"
}

#include <stdint.h>
#include <string.h>
#include <stdio.h>

/*
 * These mode flags already exist in the emulator core/UI.
 * For T-DOS both flags are expected to be true.
 */
extern bool m_cpm_enabled;
extern bool m_tdos_enabled;
extern tTMS9981A tms;

#define F18A_TDOS_ROWS 24u
#define F18A_TDOS_COLS 80u

/*
 * Keep this low enough to feel responsive, but high enough to avoid scanning
 * the T-DOS buffer on every opcode. This module can later be moved to a
 * frame/tick hook if desired.
 */
#define F18A_TDOS_SYNC_THROTTLE 900u

static uint8_t  g_active = 0;
static uint16_t g_buffer_addr = 0;
static uint32_t g_sync_throttle = 0;

static uint8_t g_shadow_valid = 0;
static uint8_t g_shadow_ch[F18A_TDOS_ROWS][F18A_TDOS_COLS];
static uint8_t g_shadow_fg[F18A_TDOS_ROWS][F18A_TDOS_COLS];
static uint8_t g_shadow_bg[F18A_TDOS_ROWS][F18A_TDOS_COLS];

static inline uint8_t z80rb_tdos(uint16_t a)
{
    return (uint8_t)coleco_ReadByte((int)a);
}

static inline uint16_t z80rw_tdos(uint16_t a)
{
    return (uint16_t)z80rb_tdos(a) |
           (uint16_t)((uint16_t)z80rb_tdos((uint16_t)(a + 1u)) << 8);
}

/*
 * Copy of ScreenWidget::CheckTDOS80BufferAddr(), without Qt dependencies.
 * Return value: start address of the T-DOS 80-column text buffer, or 0.
 */
static uint16_t tdos_check_80_buffer_addr(void)
{
    uint16_t addr = z80rw_tdos(0x0001u);
    addr = (uint16_t)(addr + 0x006Du - 3u);
    const uint16_t routinePtr = z80rw_tdos(addr);
    if (routinePtr == 0u || routinePtr > 0xFFEFu)
        return 0u;

    if (z80rb_tdos((uint16_t)(routinePtr + 0u))  != 0xF5u) return 0u;
    if (z80rb_tdos((uint16_t)(routinePtr + 1u))  != 0xC5u) return 0u;
    if (z80rb_tdos((uint16_t)(routinePtr + 2u))  != 0xD5u) return 0u;
    if (z80rb_tdos((uint16_t)(routinePtr + 3u))  != 0xCDu) return 0u;
    if (z80rb_tdos((uint16_t)(routinePtr + 6u))  != 0x30u) return 0u;
    if (z80rb_tdos((uint16_t)(routinePtr + 8u))  != 0xE1u) return 0u;
    if (z80rb_tdos((uint16_t)(routinePtr + 9u))  != 0x11u) return 0u;
    if (z80rb_tdos((uint16_t)(routinePtr + 12u)) != 0x01u) return 0u;
    if (z80rb_tdos((uint16_t)(routinePtr + 13u)) != 0x00u) return 0u;
    if (z80rb_tdos((uint16_t)(routinePtr + 14u)) != 0x04u) return 0u;
    if (z80rb_tdos((uint16_t)(routinePtr + 15u)) != 0xEDu) return 0u;
    if (z80rb_tdos((uint16_t)(routinePtr + 16u)) != 0xB0u) return 0u;

    /* Same VDP-mode guard as the existing ScreenWidget T-DOS path. */
    if ((tms.VR[0] & 0x02u) != 0x00u) return 0u;
    if ((tms.VR[1] & 0x18u) != 0x10u) return 0u;

    const uint16_t bufBase = z80rw_tdos((uint16_t)(routinePtr + 10u));
    return (uint16_t)(bufBase + 0x0400u);
}

/* Copy of ScreenWidget::GetTDOSNumLines(), without Qt dependencies. */
static int tdos_get_num_lines(void)
{
    uint16_t i = z80rw_tdos(0x0001u);
    i = (uint16_t)(i + 0x0064u - 3u);

    const uint16_t p = z80rw_tdos(i);
    const uint16_t q = z80rw_tdos((uint16_t)(p + 3u));
    int num = (int)z80rb_tdos(q) + 1;

    if (num < 0)  num = 0;
    if (num > 24) num = 24;
    return num;
}

static uint8_t tdos_display_char(uint8_t raw)
{
    uint8_t ch = (uint8_t)(raw & 0x7Fu);
    if (ch < 0x20u)
        ch = ' ';
    return ch;
}

static void tdos_put_cell_if_changed(unsigned int row,
                                     unsigned int col,
                                     uint8_t raw,
                                     uint8_t normalFg,
                                     uint8_t normalBg)
{
    if (row >= F18A_TDOS_ROWS || col >= F18A_TDOS_COLS)
        return;

    const uint8_t inverted = (raw & 0x80u) ? 1u : 0u;
    const uint8_t ch = tdos_display_char(raw);
    const uint8_t fg = inverted ? normalBg : normalFg;
    const uint8_t bg = inverted ? normalFg : normalBg;

    if (g_shadow_valid &&
        g_shadow_ch[row][col] == ch &&
        g_shadow_fg[row][col] == fg &&
        g_shadow_bg[row][col] == bg)
        return;

    f18a_term80_put_cell(row, col, ch, fg, bg);

    g_shadow_ch[row][col] = ch;
    g_shadow_fg[row][col] = fg;
    g_shadow_bg[row][col] = bg;
}

static void tdos_clear_row(unsigned int row, uint8_t fg, uint8_t bg)
{
    for (unsigned int col = 0u; col < F18A_TDOS_COLS; ++col)
        tdos_put_cell_if_changed(row, col, ' ', fg, bg);
}

static unsigned int tdos_guess_cursor_col(unsigned int row)
{
    if (row >= F18A_TDOS_ROWS)
        return 0u;

    for (int col = (int)F18A_TDOS_COLS - 1; col >= 0; --col) {
        if (g_shadow_ch[row][col] != ' ')
            return (col < (int)F18A_TDOS_COLS - 1) ? (unsigned int)(col + 1) : (unsigned int)col;
    }

    return 0u;
}

static void tdos_sync_buffer_to_term80(uint16_t bufAddr)
{
    /*
     * F18A T-DOS path based on ScreenWidget::read80ColumnVRAM().
     * Keep this as close as possible to the old working TMS 80C code:
     * - rows 0..numLines-1 come from the T-DOS 80-column RAM buffer
     * - rows 21..23 come from the current VDP nametable smartkey/status area
     * - row 22 gets the " T-DOS 80 " label, just like ScreenWidget did
     *
     * Do not reserve/shift row 0 here. The visual title/logo bar, if wanted,
     * is a ScreenWidget overlay and must not alter the TERM80 cell layout.
     */
    uint8_t fg = (uint8_t)((tms.VR[7] >> 4) & 0x0Fu);
    uint8_t bg = (uint8_t)(tms.VR[7] & 0x0Fu);

    /*
 * T-DOS/F18A 80C safety:
 * Soms staat VR7 niet bruikbaar voor de virtuele 80C buffer.
 * Zwart-op-zwart geeft dan een leeg scherm met enkel cursor.
 */
    if (fg == 0)
        fg = 15;   // wit

    if (bg == fg)
        bg = 1;    // zwart/donker als achtergrond

    const int numLines = tdos_get_num_lines();

    for (unsigned int row = 0u; row < F18A_TDOS_ROWS; ++row) {
        for (unsigned int col = 0u; col < F18A_TDOS_COLS; ++col)
            tdos_put_cell_if_changed(row, col, ' ', fg, bg);
    }

    for (int row = 0; row < numLines && row < (int)F18A_TDOS_ROWS; ++row) {
        for (unsigned int col = 0u; col < F18A_TDOS_COLS; ++col) {
            const uint8_t raw = z80rb_tdos((uint16_t)(bufAddr + ((uint16_t)row * F18A_TDOS_COLS) + col));
            tdos_put_cell_if_changed((unsigned int)row, col, raw, fg, bg);
        }
    }

    const unsigned int nameTableBase = ((unsigned int)(tms.VR[2] & 0x0Fu)) << 10;
    const uint8_t block = VDP_Memory[(nameTableBase + 21u * 40u + 0u) & 0x3FFFu];

    for (unsigned int row = 21u; row < 24u; ++row) {
        for (unsigned int col = 0u; col < 40u; ++col) {
            const uint8_t raw = VDP_Memory[(nameTableBase + row * 40u + col) & 0x3FFFu];
            tdos_put_cell_if_changed(row, col, raw, fg, bg);
        }

        for (unsigned int col = 40u; col < F18A_TDOS_COLS; ++col)
            tdos_put_cell_if_changed(row, col, block, fg, bg);
    }

    static const char tdosLabel[] = " T-DOS 80 ";
    for (unsigned int i = 0u; i < sizeof(tdosLabel) - 1u; ++i)
        tdos_put_cell_if_changed(22u, 67u + i, (uint8_t)tdosLabel[i], fg, bg);

    g_shadow_valid = 1u;

    /* Keep TERM80 cursor visible/blinking for T-DOS. */
    const unsigned int cursorRow = 20u;
    const unsigned int cursorCol = tdos_guess_cursor_col(cursorRow);
    f18a_term80_set_cursor(cursorRow, cursorCol);
    f18a_term80_show_cursor(1);
}

extern "C" void f18a_term80_tdos_reset(void)
{
    g_active = 0u;
    g_buffer_addr = 0u;
    g_sync_throttle = 0u;
    g_shadow_valid = 0u;
    memset(g_shadow_ch, 0, sizeof(g_shadow_ch));
    memset(g_shadow_fg, 0, sizeof(g_shadow_fg));
    memset(g_shadow_bg, 0, sizeof(g_shadow_bg));
}

extern "C" uint8_t f18a_term80_tdos_is_active(void)
{
    return g_active;
}

extern "C" uint16_t f18a_term80_tdos_buffer_addr(void)
{
    return g_buffer_addr;
}

extern "C" uint8_t f18a_term80_tdos_sync_now(void)
{
    if (!m_tdos_enabled) {
        if (g_active)
            f18a_term80_tdos_reset();
        return 0u;
    }

    const uint16_t bufAddr = tdos_check_80_buffer_addr();
    if (bufAddr == 0u) {
        if (g_active)
            f18a_term80_tdos_reset();
        return 0u;
    }

    if (!f18a_term80_is_enabled()) {
        f18a_term80_set_enabled(1);
        f18a_term80_clear();
        g_shadow_valid = 0u;
    }

    if (g_buffer_addr != bufAddr) {
        g_buffer_addr = bufAddr;
        g_shadow_valid = 0u;
    }

    g_active = 1u;
    tdos_sync_buffer_to_term80(bufAddr);
    return 1u;
}

extern "C" void f18a_term80_tdos_before_opcode(void)
{
    if (g_sync_throttle > 0u) {
        --g_sync_throttle;
        return;
    }

    g_sync_throttle = F18A_TDOS_SYNC_THROTTLE;
    (void)f18a_term80_tdos_sync_now();
}


/* -------------------------------------------------------------------------
 * Legacy CP/M80 virtual terminal code previously in f18a_tdos80.cpp/.h.
 * Kept here so f18a_tdos80.* can be removed from the project.
 * ------------------------------------------------------------------------- */

#include <string.h>

// Deze symbolen bestaan al in jouw emulator.
// We declareren ze hier extern zodat CP/M80 los staat van ScreenWidget/Qt.
extern bool m_cpm_enabled;
extern bool m_tdos_enabled;
extern unsigned char coleco_80col_enabled;
extern unsigned char RAM_Memory[];

// CP/M80 gebruikt 80 kolommen en houdt intern maximaal 23 tekstregels bij.
// Schermregel 0 is gereserveerd voor de titelbalk in ScreenWidget.
// Als smartkeys gevonden zijn, worden schermregels 22 en 23 daarvoor gebruikt.
static constexpr int CPM80_COLS = 80;
static constexpr int CPM80_ROWS = 23;
static constexpr int CPM80_PASTE_BUFFER_SIZE = 1024;
static constexpr int CPM80_ECHO_SUPPRESS_SIZE = 256;

// TMS9918 kleurindex: 1 = zwart, 15 = wit.
static const unsigned char CPM80_FG_COLOR = 1;

static unsigned char g_cpm80Screen[CPM80_ROWS][CPM80_COLS];
static unsigned char g_cpm80Color[CPM80_ROWS][CPM80_COLS];

static int g_cpm80X = 0;
static int g_cpm80Y = 0;

static bool g_cpm80Init = false;
static uint16_t g_cpm80ConoutAddr = 0;
static uint16_t g_cpm80ConstAddr  = 0;
static uint16_t g_cpm80ConinAddr  = 0;
static uint16_t g_cpm80LastWboot = 0;

// Laatste bekende prompt, gebruikt door Clear screen.
static char g_cpm80LastPrompt[3] = { 'A', '>', 0 };
static unsigned char g_cpm80PrevPrintable = 0;

// Handmatige kleur API, voor compatibiliteit met eerdere ScreenWidget versies.
static unsigned char g_fixedColorsEnabled = 0;
static unsigned char g_fixedFg = 15;
static unsigned char g_fixedBg = 1;

// Eenvoudige terminal escape parser.
// Veel CP/M terminal drivers gebruiken ESC '=' row+32 col+32 voor cursor positioning.
static bool g_cpm80Esc = false;
static int  g_cpm80EscState = 0;
static int  g_cpm80TmpRow = 0;

// Paste buffer die via CP/M BIOS CONIN wordt geleverd.
static unsigned char g_cpm80PasteBuffer[CPM80_PASTE_BUFFER_SIZE];
static int g_cpm80PasteHead = 0;
static int g_cpm80PasteTail = 0;

// Edit-state: na paste tonen we tekst visueel, maar CP/M krijgt hem pas bij Enter.
static bool g_cpm80PasteEditActive = false;
static unsigned char g_cpm80PasteEditLine[CPM80_ECHO_SUPPRESS_SIZE];
static int g_cpm80PasteEditLen = 0;

// Echo-filter: CP/M/BDOS kan de geplakte lijn nog eens echo'en.
// Omdat de paste-tekst al direct zichtbaar is gemaakt, onderdrukken we
// na Enter exact één mogelijke echo-sequentie:
//   [optionele CR/LF] [optionele prompt A>] commandotekst
// Daarna laten we alle echte output weer normaal door.
static unsigned char g_cpm80SuppressEcho[CPM80_ECHO_SUPPRESS_SIZE];
static int g_cpm80SuppressEchoLen = 0;
static int g_cpm80SuppressEchoPos = 0;
static bool g_cpm80SuppressEchoActive = false;
static int g_cpm80SuppressEchoPhase = 0; // 0=start/newline, 1=prompt-gt, 2=command

// BIOS-routine patching voor CONIN/CONST. Bij paste vervangen we tijdelijk de
// CP/M BIOS routine door: LD A,value ; RET. Daarna herstellen we de originele bytes.
struct Cpm80Patch {
    bool active = false;
    uint16_t addr = 0;
    unsigned char original[3] = {0,0,0};
};

static Cpm80Patch g_constPatch;
static Cpm80Patch g_coninPatch;

static bool cpm80_enabled_now()
{
    return m_cpm_enabled && !m_tdos_enabled && (coleco_80col_enabled != 0);
}

static void cpm80_restore_patch(Cpm80Patch& patch)
{
    if (!patch.active)
        return;

    RAM_Memory[patch.addr + 0] = patch.original[0];
    RAM_Memory[patch.addr + 1] = patch.original[1];
    RAM_Memory[patch.addr + 2] = patch.original[2];
    patch.active = false;
}

static void cpm80_patch_return_a(Cpm80Patch& patch, uint16_t addr, unsigned char value)
{
    if (!patch.active || patch.addr != addr) {
        cpm80_restore_patch(patch);
        patch.addr = addr;
        patch.original[0] = RAM_Memory[addr + 0];
        patch.original[1] = RAM_Memory[addr + 1];
        patch.original[2] = RAM_Memory[addr + 2];
        patch.active = true;
    }

    RAM_Memory[addr + 0] = 0x3E;   // LD A,n
    RAM_Memory[addr + 1] = value;
    RAM_Memory[addr + 2] = 0xC9;   // RET
}

static void cpm80_clear_paste_buffer()
{
    g_cpm80PasteHead = 0;
    g_cpm80PasteTail = 0;
}

static bool cpm80_paste_has_char()
{
    return g_cpm80PasteHead != g_cpm80PasteTail;
}

static void cpm80_paste_push(unsigned char ch)
{
    const int next = (g_cpm80PasteTail + 1) % CPM80_PASTE_BUFFER_SIZE;
    if (next == g_cpm80PasteHead)
        return;

    g_cpm80PasteBuffer[g_cpm80PasteTail] = ch;
    g_cpm80PasteTail = next;
}

static unsigned char cpm80_paste_pop()
{
    if (!cpm80_paste_has_char())
        return 0;

    const unsigned char ch = g_cpm80PasteBuffer[g_cpm80PasteHead];
    g_cpm80PasteHead = (g_cpm80PasteHead + 1) % CPM80_PASTE_BUFFER_SIZE;
    return ch;
}

static void cpm80_clear_suppress_echo_buffer()
{
    g_cpm80SuppressEchoLen = 0;
    g_cpm80SuppressEchoPos = 0;
    g_cpm80SuppressEchoActive = false;
    g_cpm80SuppressEchoPhase = 0;
    memset(g_cpm80SuppressEcho, 0, sizeof(g_cpm80SuppressEcho));
}

static bool cpm80_should_suppress_echo(unsigned char ch)
{
    if (!g_cpm80SuppressEchoActive || g_cpm80SuppressEchoLen <= 0)
        return false;

    const unsigned char prompt0 = (unsigned char)(g_cpm80LastPrompt[0] ? g_cpm80LastPrompt[0] : 'A');
    const unsigned char prompt1 = (unsigned char)(g_cpm80LastPrompt[1] ? g_cpm80LastPrompt[1] : '>');

    // Fase 0: direct na Enter mag CP/M eerst CR/LF geven, of meteen
    // de prompt/commandotekst opnieuw echo'en. Die eerste CR/LF hoort bij
    // de dubbele echo-regel en willen we niet tekenen, anders krijg je op
    // de volgende regel opnieuw A>DIR.
    if (g_cpm80SuppressEchoPhase == 0) {
        if (ch == 0x0D || ch == 0x0A)
            return true;

        // Optionele prompt vóór de commandotekst, bv. A>DIR.
        // Alleen als het commando zelf niet met dezelfde promptletter begint,
        // anders zou bv. A: verkeerd geïnterpreteerd kunnen worden.
        if (g_cpm80SuppressEcho[0] != prompt0 && ch == prompt0) {
            g_cpm80SuppressEchoPhase = 1;
            return true;
        }

        g_cpm80SuppressEchoPhase = 2;
        // Fall-through naar command-match voor hetzelfde teken.
    }

    // Fase 1: tweede promptteken '>' slikken.
    if (g_cpm80SuppressEchoPhase == 1) {
        if (ch == prompt1) {
            g_cpm80SuppressEchoPhase = 2;
            return true;
        }

        // Geen echte prompt: stop de filter. We hebben mogelijk één teken
        // te veel geslikt, maar dat is nog altijd minder erg dan blijvend
        // echte output verbergen.
        cpm80_clear_suppress_echo_buffer();
        return false;
    }

    // Fase 2: exact de geplakte commandotekst slikken.
    if (g_cpm80SuppressEchoPhase == 2) {
        if (g_cpm80SuppressEchoPos < g_cpm80SuppressEchoLen &&
            ch == g_cpm80SuppressEcho[g_cpm80SuppressEchoPos]) {
            ++g_cpm80SuppressEchoPos;

            if (g_cpm80SuppressEchoPos >= g_cpm80SuppressEchoLen) {
                // Echo volledig onderdrukt. Vanaf het volgende teken mag
                // echte command-output weer normaal zichtbaar worden.
                cpm80_clear_suppress_echo_buffer();
            }

            return true;
        }

        cpm80_clear_suppress_echo_buffer();
        return false;
    }

    cpm80_clear_suppress_echo_buffer();
    return false;
}

static void cpm80_clear_edit_state()
{
    g_cpm80PasteEditActive = false;
    g_cpm80PasteEditLen = 0;
    memset(g_cpm80PasteEditLine, 0, sizeof(g_cpm80PasteEditLine));
}

static void cpm80_put_char_internal(uint8_t ch);
static void cpm80_detect_smartkeys_internal(uint16_t biosBase);
static void cpm80_set_default_smartkeys();
static bool cpm80_current_smartkeys_are_default_exact();
static void cpm80_update_smartkey_visibility();
static int cpm80_visible_rows();

static char g_cpm80SmartKeys[6][13] = {
    "DIR",
    "ERA",
    "REN",
    "USER",
    "SAVE",
    "TYPE"
};

static bool g_cpm80SmartKeysDetected = false;
static bool g_cpm80SmartKeysVisible = false;
static uint16_t g_cpm80SmartKeysSourceAddr = 0;
static int g_cpm80SmartKeyRescanCounter = 0;

static int cpm80_visible_rows()
{
    return g_cpm80SmartKeysVisible ? 21 : 23;
}

static void cpm80_clear_internal()
{
    for (int y = 0; y < CPM80_ROWS; ++y) {
        for (int x = 0; x < CPM80_COLS; ++x) {
            g_cpm80Screen[y][x] = ' ';
            g_cpm80Color[y][x]  = CPM80_FG_COLOR;
        }
    }

    g_cpm80X = 0;
    g_cpm80Y = 0;
    g_cpm80Esc = false;
    g_cpm80EscState = 0;
    g_cpm80TmpRow = 0;
}

static void cpm80_clear_internal_full_state()
{
    cpm80_clear_internal();
    cpm80_clear_edit_state();
    cpm80_clear_paste_buffer();
    cpm80_clear_suppress_echo_buffer();
    cpm80_restore_patch(g_constPatch);
    cpm80_restore_patch(g_coninPatch);
}

static void cpm80_scroll_internal()
{
    const int rows = cpm80_visible_rows();

    if (rows <= 1)
        return;

    memmove(&g_cpm80Screen[0][0],
            &g_cpm80Screen[1][0],
            (rows - 1) * CPM80_COLS);

    memmove(&g_cpm80Color[0][0],
            &g_cpm80Color[1][0],
            (rows - 1) * CPM80_COLS);

    memset(&g_cpm80Screen[rows - 1][0], ' ', CPM80_COLS);
    memset(&g_cpm80Color[rows - 1][0], CPM80_FG_COLOR, CPM80_COLS);

    if (g_cpm80Y > 0)
        --g_cpm80Y;
}

static void cpm80_normalize_cursor()
{
    const int rows = cpm80_visible_rows();

    if (g_cpm80X < 0)
        g_cpm80X = 0;

    if (g_cpm80X >= CPM80_COLS) {
        g_cpm80X = 0;
        ++g_cpm80Y;
    }

    if (g_cpm80Y < 0)
        g_cpm80Y = 0;

    while (g_cpm80Y >= rows)
        cpm80_scroll_internal();
}

static void cpm80_track_prompt(unsigned char ch)
{
    if (ch >= 'A' && ch <= 'Z') {
        g_cpm80PrevPrintable = ch;
        return;
    }

    if (ch == '>' && g_cpm80PrevPrintable >= 'A' && g_cpm80PrevPrintable <= 'Z') {
        g_cpm80LastPrompt[0] = (char)g_cpm80PrevPrintable;
        g_cpm80LastPrompt[1] = '>';
        g_cpm80LastPrompt[2] = 0;
    }

    if (ch >= 32)
        g_cpm80PrevPrintable = ch;
}

static void cpm80_put_char_internal(uint8_t ch)
{
    if (g_cpm80Esc) {
        if (g_cpm80EscState == 1) {
            if (ch == '=') {
                g_cpm80EscState = 2;
                return;
            }

            g_cpm80Esc = false;
            g_cpm80EscState = 0;
        }
        else if (g_cpm80EscState == 2) {
            g_cpm80TmpRow = (ch >= 32) ? (ch - 32) : ch;
            g_cpm80EscState = 3;
            return;
        }
        else if (g_cpm80EscState == 3) {
            int col = (ch >= 32) ? (ch - 32) : ch;

            if (g_cpm80TmpRow >= 0 && g_cpm80TmpRow < cpm80_visible_rows())
                g_cpm80Y = g_cpm80TmpRow;

            if (col >= 0 && col < CPM80_COLS)
                g_cpm80X = col;

            g_cpm80Esc = false;
            g_cpm80EscState = 0;
            return;
        }
    }

    switch (ch) {
    case 0x1B: // ESC
        g_cpm80Esc = true;
        g_cpm80EscState = 1;
        return;

    case 0x08: // BS
    case 0x7F:
        if (g_cpm80X > 0)
            --g_cpm80X;

        cpm80_normalize_cursor();
        g_cpm80Screen[g_cpm80Y][g_cpm80X] = ' ';
        g_cpm80Color[g_cpm80Y][g_cpm80X] = CPM80_FG_COLOR;
        return;

    case 0x09: // TAB
        g_cpm80X = (g_cpm80X + 8) & ~7;
        cpm80_normalize_cursor();
        return;

    case 0x0A: // LF
        ++g_cpm80Y;
        cpm80_normalize_cursor();
        return;

    case 0x0D: // CR
        g_cpm80X = 0;
        return;

    case 0x0C: // FF / CLS
        cpm80_clear_internal();
        return;

    default:
        break;
    }

    if (ch >= 32) {
        cpm80_track_prompt(ch);
        cpm80_normalize_cursor();

        g_cpm80Screen[g_cpm80Y][g_cpm80X] = ch;
        g_cpm80Color[g_cpm80Y][g_cpm80X] = CPM80_FG_COLOR;

        ++g_cpm80X;
        cpm80_normalize_cursor();
    }
}

static uint16_t cpm80_bios_target(uint16_t entry)
{
    if (RAM_Memory[entry] == 0xC3) {
        return (uint16_t)(RAM_Memory[entry + 1] |
                         (RAM_Memory[entry + 2] << 8));
    }
    return entry;
}

static void cpm80_detect_bios_internal()
{
    if (!cpm80_enabled_now()) {
        g_cpm80ConoutAddr = 0;
        g_cpm80ConstAddr = 0;
        g_cpm80ConinAddr = 0;
        return;
    }

    if (RAM_Memory[0x0000] != 0xC3)
        return;

    uint16_t wboot = (uint16_t)(RAM_Memory[0x0001] | (RAM_Memory[0x0002] << 8));
    if (wboot < 3)
        return;

    if (wboot != g_cpm80LastWboot) {
        g_cpm80LastWboot = wboot;
        g_cpm80ConoutAddr = 0;
        g_cpm80ConstAddr = 0;
        g_cpm80ConinAddr = 0;
        cpm80_clear_internal_full_state();
        cpm80_set_default_smartkeys();
    }

    uint16_t biosBase = (uint16_t)(wboot - 3);

    g_cpm80ConstAddr  = cpm80_bios_target((uint16_t)(biosBase + 6));
    g_cpm80ConinAddr  = cpm80_bios_target((uint16_t)(biosBase + 9));
    g_cpm80ConoutAddr = cpm80_bios_target((uint16_t)(biosBase + 12));

    cpm80_detect_smartkeys_internal(biosBase);
}

static void cpm80_clear_internal_and_restore_prompt()
{
    const char prompt[3] = { g_cpm80LastPrompt[0] ? g_cpm80LastPrompt[0] : 'A',
                             g_cpm80LastPrompt[1] ? g_cpm80LastPrompt[1] : '>',
                             0 };

    cpm80_clear_internal();
    cpm80_put_char_internal((uint8_t)prompt[0]);
    cpm80_put_char_internal((uint8_t)prompt[1]);
}

extern "C" void cpm80_reset(void)
{
    cpm80_clear_internal_full_state();
    g_cpm80Init = false;
    g_cpm80ConoutAddr = 0;
    g_cpm80ConstAddr = 0;
    g_cpm80ConinAddr = 0;
    g_cpm80LastWboot = 0;
    g_cpm80LastPrompt[0] = 'A';
    g_cpm80LastPrompt[1] = '>';
    g_cpm80LastPrompt[2] = 0;
    cpm80_set_default_smartkeys();
}

extern "C" void cpm80_disable(void)
{
    cpm80_reset();
}

extern "C" void cpm80_clear_screen(void)
{
    cpm80_clear_internal_and_restore_prompt();
}

extern "C" void cpm80_set_fixed_colors(uint8_t enabled, uint8_t fg, uint8_t bg)
{
    g_fixedColorsEnabled = enabled ? 1 : 0;
    g_fixedFg = fg & 0x0F;
    g_fixedBg = bg & 0x0F;
}

extern "C" uint8_t cpm80_fixed_colors_enabled(void)
{
    return g_fixedColorsEnabled;
}

extern "C" uint8_t cpm80_get_fixed_fg(void)
{
    return g_fixedFg;
}

extern "C" uint8_t cpm80_get_fixed_bg(void)
{
    return g_fixedBg;
}

extern "C" void cpm80_queue_paste_text(const char* text)
{
    cpm80_clear_paste_buffer();
    cpm80_clear_suppress_echo_buffer();
    cpm80_clear_edit_state();

    if (!text)
        return;

    for (const char* p = text; *p && g_cpm80PasteEditLen < (CPM80_ECHO_SUPPRESS_SIZE - 1); ++p) {
        unsigned char ch = (unsigned char)*p;

        // Command-line paste: geen CR/LF meesturen. De gebruiker drukt Enter.
        if (ch == '\r' || ch == '\n')
            break;

        if (ch < 32)
            continue;

        g_cpm80PasteEditLine[g_cpm80PasteEditLen++] = ch;
        cpm80_put_char_internal(ch);       // direct zichtbaar tonen
    }

    g_cpm80PasteEditActive = (g_cpm80PasteEditLen > 0);
}

extern "C" uint8_t cpm80_paste_backspace(void)
{
    if (!g_cpm80PasteEditActive || g_cpm80PasteEditLen <= 0)
        return 0;

    --g_cpm80PasteEditLen;
    g_cpm80PasteEditLine[g_cpm80PasteEditLen] = 0;
    cpm80_put_char_internal(0x08);

    if (g_cpm80PasteEditLen == 0)
        g_cpm80PasteEditActive = false;

    return 1;
}

extern "C" uint8_t cpm80_paste_commit(void)
{
    if (!g_cpm80PasteEditActive || g_cpm80PasteEditLen <= 0)
        return 0;

    cpm80_clear_paste_buffer();
    cpm80_clear_suppress_echo_buffer();

    for (int i = 0; i < g_cpm80PasteEditLen; ++i) {
        cpm80_paste_push(g_cpm80PasteEditLine[i]);
        g_cpm80SuppressEcho[g_cpm80SuppressEchoLen++] = g_cpm80PasteEditLine[i];
    }

    cpm80_paste_push(0x0D); // CP/M command afsluiten

    // Activeer een robuuste echo-filter. We zetten bewust GEEN CR in deze
    // buffer: de filter slikt eventuele CR/LF vóór de dubbele echo zelf,
    // en laat na de commandotekst de echte output weer door.
    g_cpm80SuppressEchoActive = (g_cpm80SuppressEchoLen > 0);
    g_cpm80SuppressEchoPos = 0;
    g_cpm80SuppressEchoPhase = 0;

    cpm80_clear_edit_state();
    return 1;
}

extern "C" uint8_t cpm80_paste_pending(void)
{
    return (uint8_t)((g_cpm80PasteEditActive || cpm80_paste_has_char()) ? 1 : 0);
}

extern "C" void cpm80_before_opcode(uint16_t pc, uint8_t reg_c)
{
    if (!cpm80_enabled_now()) {
        g_cpm80ConoutAddr = 0;
        g_cpm80ConstAddr = 0;
        g_cpm80ConinAddr = 0;
        cpm80_restore_patch(g_constPatch);
        cpm80_restore_patch(g_coninPatch);
        return;
    }

    if (!g_cpm80Init) {
        cpm80_clear_internal();
        g_cpm80Init = true;
    }

    if (g_cpm80ConoutAddr == 0 || g_cpm80ConstAddr == 0 || g_cpm80ConinAddr == 0) {
        cpm80_detect_bios_internal();
    }
    else if (g_cpm80LastWboot >= 3 &&
             ((g_cpm80SmartKeyRescanCounter++ & 0x3F) == 0)) {
        // CP/M disks/tapes can install their own smartkey table after the
        // BIOS was already detected. Keep rescanning while we only have the
        // default labels, so disk-specific labels can replace them.
        if (!g_cpm80SmartKeysDetected || cpm80_current_smartkeys_are_default_exact())
            cpm80_detect_smartkeys_internal((uint16_t)(g_cpm80LastWboot - 3));

        // The disk/program can also toggle the smartkey rows on/off after boot.
        // Update this separately; do not use the VRAM bottom-row test here,
        // because it hides good disks in 80-column mode.
        cpm80_update_smartkey_visibility();
    }

    if (g_cpm80ConstAddr != 0 && pc == g_cpm80ConstAddr) {
        if (cpm80_paste_has_char())
            cpm80_patch_return_a(g_constPatch, g_cpm80ConstAddr, 0xFF);
        else
            cpm80_restore_patch(g_constPatch);
        return;
    }

    if (g_cpm80ConinAddr != 0 && pc == g_cpm80ConinAddr) {
        if (cpm80_paste_has_char()) {
            const unsigned char ch = cpm80_paste_pop();
            cpm80_patch_return_a(g_coninPatch, g_cpm80ConinAddr, ch);
        } else {
            cpm80_restore_patch(g_coninPatch);
        }
        return;
    }

    if (g_cpm80ConoutAddr != 0 && pc == g_cpm80ConoutAddr) {
        if (!cpm80_should_suppress_echo(reg_c))
            cpm80_put_char_internal(reg_c);
    }
}

extern "C" uint8_t cpm80_is_active(void)
{
    return (uint8_t)(cpm80_enabled_now() && g_cpm80Init);
}

extern "C" uint8_t cpm80_get_char(int row, int col)
{
    if (row < 0 || row >= cpm80_visible_rows() || col < 0 || col >= CPM80_COLS)
        return ' ';

    return g_cpm80Screen[row][col];
}

extern "C" uint8_t cpm80_get_color(int row, int col)
{
    if (row < 0 || row >= cpm80_visible_rows() || col < 0 || col >= CPM80_COLS)
        return CPM80_FG_COLOR;

    return g_cpm80Color[row][col];
}

extern "C" uint8_t cpm80_get_cursor_x(void)
{
    return (uint8_t)((g_cpm80X < 0) ? 0 :
                     (g_cpm80X >= CPM80_COLS ? CPM80_COLS - 1 : g_cpm80X));
}

extern "C" uint8_t cpm80_get_cursor_y(void)
{
    const int rows = cpm80_visible_rows();

    return (uint8_t)((g_cpm80Y < 0) ? 0 :
                     (g_cpm80Y >= rows ? rows - 1 : g_cpm80Y));
}

extern "C" uint8_t cpm80_has_smartkeys(void)
{
    return g_cpm80SmartKeysVisible ? 1 : 0;
}



// CP/M smartkey labels.
// Default/fallback is the ADAM CP/M hardware command set order:
// I=DIR, II=ERA, III=REN, IV=USER, V=SAVE, VI=TYPE.
//
// Important: do not hardcode disk-specific labels here.  ADAM CP/M stores
// the function-key display text in the BIOS as FUNCTION_KEY_TEXT_A:
//   6 slots x 10 bytes, padded with spaces.
// Some CP/M disks/tapes patch or replace that table with their own labels.
// Therefore we scan for this exact source-layout table and prefer a
// non-default table over the built-in default one.

static const char* cpm80_default_smartkeys[6] = {
    "DIR", "ERA", "REN", "USER", "SAVE", "TYPE"
};

static void cpm80_set_default_smartkeys()
{
    for (int i = 0; i < 6; ++i) {
        memset(g_cpm80SmartKeys[i], 0, sizeof(g_cpm80SmartKeys[i]));
        strncpy(g_cpm80SmartKeys[i], cpm80_default_smartkeys[i], sizeof(g_cpm80SmartKeys[i]) - 1);
    }

    g_cpm80SmartKeysDetected = false;
    g_cpm80SmartKeysVisible = false;
    g_cpm80SmartKeysSourceAddr = 0;
    g_cpm80SmartKeyRescanCounter = 0;
}

static bool cpm80_is_smartkey_char(unsigned char ch)
{
    if (ch >= 'A' && ch <= 'Z')
        return true;
    if (ch >= '0' && ch <= '9')
        return true;

    switch (ch) {
    case ' ':
    case '/':
    case '-':
    case '+':
    case ':':
    case '.':
        return true;
    default:
        break;
    }

    return false;
}

static bool cpm80_copy_label_from_slot(char* dst, const unsigned char* src, int slotLen)
{
    int start = 0;
    int end = slotLen;

    while (start < end && (src[start] == ' ' || src[start] == 0))
        ++start;

    while (end > start && (src[end - 1] == ' ' || src[end - 1] == 0))
        --end;

    const int len = end - start;

    // ADAM smartkey display labels are short.  This avoids normal text such
    // as "DISK LABEL" or "PRINTING" being mistaken for key labels.
    if (len < 2 || len > 6 || len >= 13)
        return false;

    for (int i = start; i < end; ++i) {
        if (!cpm80_is_smartkey_char(src[i]))
            return false;
    }

    memset(dst, 0, 13);
    memcpy(dst, src + start, len);
    dst[len] = 0;
    return true;
}

static bool cpm80_labels_equal(const char a[6][13], const char* const b[6])
{
    for (int i = 0; i < 6; ++i) {
        if (strcmp(a[i], b[i]) != 0)
            return false;
    }
    return true;
}

static bool cpm80_current_smartkeys_are_default_exact()
{
    for (int i = 0; i < 6; ++i) {
        if (strcmp(g_cpm80SmartKeys[i], cpm80_default_smartkeys[i]) != 0)
            return false;
    }
    return true;
}

static bool cpm80_labels_have_duplicates(const char labels[6][13])
{
    for (int i = 0; i < 6; ++i) {
        for (int j = i + 1; j < 6; ++j) {
            if (strcmp(labels[i], labels[j]) == 0)
                return true;
        }
    }
    return false;
}

static bool cpm80_labels_are_alphabet_chunks(const char labels[6][13])
{
    // Protect against generic RAM/text patterns like:
    // CDEF, GHIJ, KLMN, OPQR, STUV, WXYZ.
    int chunks = 0;

    for (int i = 0; i < 6; ++i) {
        const int len = (int)strlen(labels[i]);
        if (len < 3 || len > 4)
            continue;

        bool sequential = true;
        for (int j = 1; j < len; ++j) {
            if ((unsigned char)labels[i][j] != (unsigned char)(labels[i][j - 1] + 1)) {
                sequential = false;
                break;
            }
        }

        if (sequential)
            ++chunks;
    }

    return chunks >= 4;
}

static int cpm80_score_smartkey_table_10(uint16_t addr, char labels[6][13])
{
    int score = 0;
    int nonDefault = 0;
    int goodPadding = 0;
    int compactLengths = 0;

    for (int i = 0; i < 6; ++i) {
        const unsigned char* slot = &RAM_Memory[(addr + (i * 10)) & 0xFFFF];

        if (!cpm80_copy_label_from_slot(labels[i], slot, 10))
            return -100000;

        const int len = (int)strlen(labels[i]);
        if (len >= 3 && len <= 5)
            ++compactLengths;

        if (strcmp(labels[i], cpm80_default_smartkeys[i]) != 0)
            ++nonDefault;

        // FUNCTION_KEY_TEXT_A normally uses left/right spaces and padded slots.
        if (slot[0] == ' ' || slot[9] == ' ' || slot[9] == 0)
            ++goodPadding;
    }

    if (cpm80_labels_have_duplicates(labels))
        return -100000;

    if (cpm80_labels_are_alphabet_chunks(labels))
        return -100000;

    const bool isDefault = (nonDefault == 0);

    // Default CP/M labels are valid, but only as fallback.  A disk/tape-specific
    // table should always win if it has the same source layout.
    score += isDefault ? 50 : 250;
    score += compactLengths * 8;
    score += goodPadding * 5;

    // A table where only one label differs is possible, but less convincing.
    if (!isDefault && nonDefault >= 3)
        score += 80;
    else if (!isDefault)
        score += 25;

    return score;
}

static bool cpm80_find_best_smartkey_table_10(uint16_t startAddr,
                                              uint16_t endAddr,
                                              uint16_t* bestAddr,
                                              char bestLabels[6][13],
                                              int* bestScore)
{
    bool found = false;
    char labels[6][13];

    if (endAddr < startAddr)
        return false;

    for (uint32_t addr = startAddr; addr <= (uint32_t)endAddr - 60; ++addr) {
        const int score = cpm80_score_smartkey_table_10((uint16_t)addr, labels);
        if (score <= -100000)
            continue;

        if (!found || score > *bestScore) {
            found = true;
            *bestScore = score;
            *bestAddr = (uint16_t)addr;
            for (int i = 0; i < 6; ++i) {
                memset(bestLabels[i], 0, 13);
                strncpy(bestLabels[i], labels[i], 12);
            }
        }
    }

    return found;
}


static void cpm80_rotate_smartkey_labels_right(char labels[6][13])
{
    char last[13];
    memset(last, 0, sizeof(last));
    strncpy(last, labels[5], sizeof(last) - 1);

    for (int i = 5; i > 0; --i) {
        memset(labels[i], 0, 13);
        strncpy(labels[i], labels[i - 1], 12);
    }

    memset(labels[0], 0, 13);
    strncpy(labels[0], last, 12);
}

static bool cpm80_labels_are_default_left_rotated(const char labels[6][13])
{
    // Some CP/M function-key tables are detected one 10-byte slot too far.
    // In that case the visible order becomes:
    //   ERA, REN, USER, SAVE, TYPE, DIR
    // while the real display order must be:
    //   DIR, ERA, REN, USER, SAVE, TYPE
    for (int i = 0; i < 6; ++i) {
        const char* expected = cpm80_default_smartkeys[(i + 1) % 6];
        if (strcmp(labels[i], expected) != 0)
            return false;
    }
    return true;
}


static bool cpm80_find_fkey_default_ram_value(unsigned char* value)
{
    // ADAM CP/M source layout around FKEY_DEFAULT_RAM:
    //   FKEY_CURRENT_INDEX:   DB 0
    //   FKEY_DEFAULT_RAM:     DB 14H or 17H
    //   FKEY_CURRENT_HANDLER: DW FKEY_HANDLER_DEFAULT ($FC1A)
    //
    // So in loaded RAM we look for:
    //   [14/17] [1A] [FC]
    // This is safer than tableAddr + fixed offset, because custom disks/tapes
    // can relocate or replace the label table.
    for (uint32_t addr = 0; addr <= 0xFFFF - 3; ++addr) {
        const unsigned char v = RAM_Memory[addr & 0xFFFF];
        if (v != 0x14 && v != 0x17)
            continue;

        if (RAM_Memory[(addr + 1) & 0xFFFF] == 0x1A &&
            RAM_Memory[(addr + 2) & 0xFFFF] == 0xFC) {
            if (value)
                *value = v;
            return true;
        }
    }

    return false;
}

static void cpm80_update_smartkey_visibility()
{
    if (!g_cpm80SmartKeysDetected) {
        g_cpm80SmartKeysVisible = false;
        return;
    }

    unsigned char fkeyDefault = 0;
    if (cpm80_find_fkey_default_ram_value(&fkeyDefault)) {
        // In ADAM CP/M:
        //   0x14 = smartkey rows active
        //   0x17 = smartkey rows hidden / rows released
        g_cpm80SmartKeysVisible = (fkeyDefault == 0x14);
        return;
    }

    // Fallback: if the runtime flag cannot be found, keep old behaviour.
    // This prevents good disks from losing smartkeys simply because the flag
    // pattern was not present in their CP/M variant.
    g_cpm80SmartKeysVisible = true;
}

static void cpm80_apply_smartkey_labels(uint16_t addr, const char labels[6][13])
{
    char orderedLabels[6][13];
    memset(orderedLabels, 0, sizeof(orderedLabels));

    for (int i = 0; i < 6; ++i)
        strncpy(orderedLabels[i], labels[i], 12);

    // Fix tables that were detected one slot too late.  This keeps the normal
    // default table unchanged, but repairs the shifted order:
    //   ERA, REN, USER, SAVE, TYPE, DIR -> DIR, ERA, REN, USER, SAVE, TYPE
    if (cpm80_labels_are_default_left_rotated(orderedLabels))
        cpm80_rotate_smartkey_labels_right(orderedLabels);

    for (int i = 0; i < 6; ++i) {
        memset(g_cpm80SmartKeys[i], 0, sizeof(g_cpm80SmartKeys[i]));
        strncpy(g_cpm80SmartKeys[i], orderedLabels[i], sizeof(g_cpm80SmartKeys[i]) - 1);
    }

    g_cpm80SmartKeysDetected = true;
    g_cpm80SmartKeysSourceAddr = addr;
    cpm80_update_smartkey_visibility();
}

static void cpm80_detect_smartkeys_internal(uint16_t biosBase)
{
    // Once a non-default disk/tape table is found, keep it.  While we only
    // have the default labels, keep allowing a later disk/tape table to win.
    if (g_cpm80SmartKeysDetected && !cpm80_current_smartkeys_are_default_exact())
        return;

    uint16_t bestAddr = 0;
    int bestScore = -100000;
    char bestLabels[6][13];
    bool found = false;

    memset(bestLabels, 0, sizeof(bestLabels));

    // First scan near the loaded CP/M BIOS area.  This is where the source
    // FUNCTION_KEY_TEXT_A table normally lives after boot.
    const uint16_t nearStart = biosBase;
    const uint16_t nearEnd = 0xFFFF;

    found |= cpm80_find_best_smartkey_table_10(nearStart, nearEnd,
                                               &bestAddr, bestLabels, &bestScore);

    // Then scan the full RAM image as fallback.  We still score all candidates
    // and choose the best one, instead of taking the first default table.
    found |= cpm80_find_best_smartkey_table_10(0x0000, 0xFFFF,
                                               &bestAddr, bestLabels, &bestScore);

    if (found)
        cpm80_apply_smartkey_labels(bestAddr, bestLabels);
}

extern "C" const char* cpm80_get_smartkey_text(int index)
{
    if (index < 0 || index >= 6)
        return "";

    return g_cpm80SmartKeys[index];
}

extern "C" uint16_t cpm80_get_conout_addr(void)
{
    return g_cpm80ConoutAddr;
}


extern "C" int cpm80_get_visible_rows(void)
{
    return cpm80_visible_rows();
}

