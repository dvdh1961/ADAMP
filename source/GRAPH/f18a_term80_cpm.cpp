#include "f18a_term80_cpm.h"

#include "f18a.h"
#include "f18a_term80.h"

#include <stdint.h>
#include <string.h>

extern bool m_cpm_enabled;
extern bool m_tdos_enabled;
extern unsigned char coleco_80col_enabled;
extern unsigned char RAM_Memory[];

static uint16_t g_conout_addr = 0;
static uint16_t g_last_wboot = 0;
static int g_active = 0;
static int g_term_enabled_by_cpm = 0;
static uint8_t g_smartkeys_detected = 0;
static uint8_t g_smartkeys_visible = 0;
static uint16_t g_smartkeys_source_addr = 0;
static uint16_t g_smartkey_rescan_counter = 0;
static uint8_t g_smartkeys_scanned_once = 0;

static char g_smartkey_text[6][13] = {
    "DIR", "ERA", "REN", "USER", "SAVE", "TYPE"
};

static const char* f18a_cpm80_default_smartkeys[6] = {
    "DIR", "ERA", "REN", "USER", "SAVE", "TYPE"
};

static int cpm_enabled_now(void)
{
    return m_cpm_enabled && !m_tdos_enabled && (coleco_80col_enabled != 0);
}

static uint16_t bios_target(uint16_t entry)
{
    if (RAM_Memory[entry] == 0xC3) {
        return (uint16_t)(RAM_Memory[entry + 1] |
                         ((uint16_t)RAM_Memory[entry + 2] << 8));
    }
    return entry;
}

static void deactivate_if_needed(void)
{
    g_conout_addr = 0;
    g_last_wboot = 0;
    g_active = 0;
    g_smartkeys_detected = 0;
    g_smartkeys_visible = 0;
    g_smartkeys_source_addr = 0;
    g_smartkey_rescan_counter = 0;
    g_smartkeys_scanned_once = 0;
    for (int i = 0; i < 6; ++i) {
        memset(g_smartkey_text[i], 0, sizeof(g_smartkey_text[i]));
        strncpy(g_smartkey_text[i], f18a_cpm80_default_smartkeys[i], sizeof(g_smartkey_text[i]) - 1);
    }
    f18a_term80_set_smartkeys_visible(0);

    /* Only switch TERM80 off if this CP/M bridge switched it on. */
    if (g_term_enabled_by_cpm && !f18a_is_80col_selftest_enabled()) {
        f18a_term80_set_enabled(0);
    }
    g_term_enabled_by_cpm = 0;
}

static void f18a_cpm80_set_default_smartkeys(void)
{
    for (int i = 0; i < 6; ++i) {
        memset(g_smartkey_text[i], 0, sizeof(g_smartkey_text[i]));
        strncpy(g_smartkey_text[i], f18a_cpm80_default_smartkeys[i], sizeof(g_smartkey_text[i]) - 1);
    }

    g_smartkeys_detected = 0;
    g_smartkeys_visible = 0;
    g_smartkeys_source_addr = 0;
    g_smartkey_rescan_counter = 0;
    g_smartkeys_scanned_once = 0;
    f18a_term80_set_smartkeys_visible(0);
}

static int f18a_cpm80_is_smartkey_char(unsigned char ch)
{
    if (ch >= 'A' && ch <= 'Z')
        return 1;
    if (ch >= '0' && ch <= '9')
        return 1;

    switch (ch) {
    case ' ':
    case '/':
    case '-':
    case '+':
    case ':':
    case '.':
        return 1;
    default:
        break;
    }

    return 0;
}

static int f18a_cpm80_copy_label_from_slot(char* dst, const unsigned char* src, int slotLen)
{
    int start = 0;
    int end = slotLen;

    while (start < end && (src[start] == ' ' || src[start] == 0))
        ++start;

    while (end > start && (src[end - 1] == ' ' || src[end - 1] == 0))
        --end;

    const int len = end - start;

    if (len < 2 || len > 6 || len >= 13)
        return 0;

    for (int i = start; i < end; ++i) {
        if (!f18a_cpm80_is_smartkey_char(src[i]))
            return 0;
    }

    memset(dst, 0, 13);
    memcpy(dst, src + start, len);
    dst[len] = 0;
    return 1;
}

static int f18a_cpm80_labels_have_duplicates(const char labels[6][13])
{
    for (int i = 0; i < 6; ++i) {
        for (int j = i + 1; j < 6; ++j) {
            if (strcmp(labels[i], labels[j]) == 0)
                return 1;
        }
    }
    return 0;
}

static int f18a_cpm80_labels_are_alphabet_chunks(const char labels[6][13])
{
    int chunks = 0;

    for (int i = 0; i < 6; ++i) {
        const int len = (int)strlen(labels[i]);
        if (len < 3 || len > 4)
            continue;

        int sequential = 1;
        for (int j = 1; j < len; ++j) {
            if ((unsigned char)labels[i][j] != (unsigned char)(labels[i][j - 1] + 1)) {
                sequential = 0;
                break;
            }
        }

        if (sequential)
            ++chunks;
    }

    return chunks >= 4;
}

static int f18a_cpm80_current_smartkeys_are_default_exact(void)
{
    for (int i = 0; i < 6; ++i) {
        if (strcmp(g_smartkey_text[i], f18a_cpm80_default_smartkeys[i]) != 0)
            return 0;
    }
    return 1;
}

static int f18a_cpm80_score_smartkey_table_10(uint16_t addr, char labels[6][13])
{
    int score = 0;
    int nonDefault = 0;
    int goodPadding = 0;
    int compactLengths = 0;

    for (int i = 0; i < 6; ++i) {
        const unsigned char* slot = &RAM_Memory[(addr + (i * 10)) & 0xFFFF];

        if (!f18a_cpm80_copy_label_from_slot(labels[i], slot, 10))
            return -100000;

        const int len = (int)strlen(labels[i]);
        if (len >= 3 && len <= 5)
            ++compactLengths;

        if (strcmp(labels[i], f18a_cpm80_default_smartkeys[i]) != 0)
            ++nonDefault;

        if (slot[0] == ' ' || slot[9] == ' ' || slot[9] == 0)
            ++goodPadding;
    }

    if (f18a_cpm80_labels_have_duplicates(labels))
        return -100000;

    if (f18a_cpm80_labels_are_alphabet_chunks(labels))
        return -100000;

    const int isDefault = (nonDefault == 0);

    score += isDefault ? 50 : 250;
    score += compactLengths * 8;
    score += goodPadding * 5;

    if (!isDefault && nonDefault >= 3)
        score += 80;
    else if (!isDefault)
        score += 25;

    return score;
}

static int f18a_cpm80_find_best_smartkey_table_10(uint16_t startAddr,
                                                  uint16_t endAddr,
                                                  uint16_t* bestAddr,
                                                  char bestLabels[6][13],
                                                  int* bestScore)
{
    int found = 0;
    char labels[6][13];

    if (endAddr < startAddr)
        return 0;

    for (uint32_t addr = startAddr; addr <= (uint32_t)endAddr - 60u; ++addr) {
        const int score = f18a_cpm80_score_smartkey_table_10((uint16_t)addr, labels);
        if (score <= -100000)
            continue;

        if (!found || score > *bestScore) {
            found = 1;
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

static void f18a_cpm80_rotate_smartkey_labels_right(char labels[6][13])
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

static int f18a_cpm80_labels_are_default_left_rotated(const char labels[6][13])
{
    for (int i = 0; i < 6; ++i) {
        const char* expected = f18a_cpm80_default_smartkeys[(i + 1) % 6];
        if (strcmp(labels[i], expected) != 0)
            return 0;
    }
    return 1;
}

static int f18a_cpm80_find_fkey_default_ram_value(unsigned char* value)
{
    for (uint32_t addr = 0; addr <= 0xFFFFu - 3u; ++addr) {
        const unsigned char v = RAM_Memory[addr & 0xFFFF];
        if (v != 0x14 && v != 0x17)
            continue;

        if (RAM_Memory[(addr + 1) & 0xFFFF] == 0x1A &&
            RAM_Memory[(addr + 2) & 0xFFFF] == 0xFC) {
            if (value)
                *value = v;
            return 1;
        }
    }

    return 0;
}

static void f18a_cpm80_update_smartkey_visibility(void)
{
    if (!g_smartkeys_detected) {
        g_smartkeys_visible = 0;
        f18a_term80_set_smartkeys_visible(0);
        return;
    }

    unsigned char fkeyDefault = 0;
    if (f18a_cpm80_find_fkey_default_ram_value(&fkeyDefault)) {
        g_smartkeys_visible = (fkeyDefault == 0x14) ? 1u : 0u;
        f18a_term80_set_smartkeys_visible(g_smartkeys_visible);
        return;
    }

    g_smartkeys_visible = 1;
    f18a_term80_set_smartkeys_visible(1);
}

static void f18a_cpm80_apply_smartkey_labels(uint16_t addr, const char labels[6][13])
{
    char orderedLabels[6][13];
    memset(orderedLabels, 0, sizeof(orderedLabels));

    for (int i = 0; i < 6; ++i)
        strncpy(orderedLabels[i], labels[i], 12);

    if (f18a_cpm80_labels_are_default_left_rotated(orderedLabels))
        f18a_cpm80_rotate_smartkey_labels_right(orderedLabels);

    for (int i = 0; i < 6; ++i) {
        memset(g_smartkey_text[i], 0, sizeof(g_smartkey_text[i]));
        strncpy(g_smartkey_text[i], orderedLabels[i], sizeof(g_smartkey_text[i]) - 1);
    }

    g_smartkeys_detected = 1;
    g_smartkeys_source_addr = addr;
    f18a_cpm80_update_smartkey_visibility();
}

static void f18a_cpm80_detect_smartkeys_internal(uint16_t biosBase)
{
    if (g_smartkeys_detected && !f18a_cpm80_current_smartkeys_are_default_exact()) {
        f18a_cpm80_update_smartkey_visibility();
        return;
    }

    uint16_t bestAddr = 0;
    int bestScore = -100000;
    char bestLabels[6][13];
    int found = 0;

    memset(bestLabels, 0, sizeof(bestLabels));

    found |= f18a_cpm80_find_best_smartkey_table_10(biosBase, 0xFFFF,
                                                    &bestAddr, bestLabels, &bestScore);

    found |= f18a_cpm80_find_best_smartkey_table_10(0x0000, 0xFFFF,
                                                    &bestAddr, bestLabels, &bestScore);

    if (found)
        f18a_cpm80_apply_smartkey_labels(bestAddr, bestLabels);
    else
        f18a_cpm80_update_smartkey_visibility();
}

extern "C" uint8_t f18a_term80_cpm_has_smartkeys(void)
{
    return g_smartkeys_visible ? 1u : 0u;
}

extern "C" const char* f18a_term80_cpm_get_smartkey_text(int index)
{
    if (index < 0 || index >= 6)
        return "";

    return g_smartkey_text[index];
}


extern "C" void f18a_term80_cpm_force_smartkey_rescan(void)
{
    /*
     * A CP/M reset or disk change can keep TERM80 active while the
     * smartkey cache still belongs to the previous disk/BIOS image.
     * Force the next real CP/M CONOUT to rescan labels and visibility.
     */
    f18a_cpm80_set_default_smartkeys();
}

extern "C" void f18a_term80_cpm_reset(void)
{
    deactivate_if_needed();
}

extern "C" uint8_t f18a_term80_cpm_is_active(void)
{
    return (uint8_t)g_active;
}

static void ensure_term80_ready(void)
{
    if (f18a_is_80col_selftest_enabled())
        return;

    if (!f18a_term80_is_enabled()) {
        f18a_term80_set_enabled(1);
        /* Keep the currently selected TERM80 FG/BG colors. */
        f18a_term80_clear();
        g_term_enabled_by_cpm = 1;
    }
}

static void detect_bios(void)
{
    if (!cpm_enabled_now()) {
        deactivate_if_needed();
        return;
    }

    if (RAM_Memory[0x0000] != 0xC3)
        return;

    const uint16_t wboot = (uint16_t)(RAM_Memory[0x0001] |
                                    ((uint16_t)RAM_Memory[0x0002] << 8));
    if (wboot < 3)
        return;

    /*
     * Important:
     * When 80 columns is already enabled before pressing RESET, this hook
     * runs during the early CP/M loader too. At that moment the BIOS jump
     * vector at address 0000h can still point to temporary boot code.
     *
     * The old code detected CONOUT only once. If it detected that temporary
     * vector, it never updated to the final CP/M BIOS CONOUT address, so the
     * terminal stayed in 40-column/normal VDP output until the user toggled
     * 80 columns after CP/M had already booted.
     *
     * Therefore we keep watching WBOOT. When WBOOT changes, recompute CONOUT.
     * Do NOT enable/clear TERM80 here; only enable it when a real CONOUT call
     * is actually reached below.
     */
    if (wboot != g_last_wboot) {
        g_last_wboot = wboot;
        g_conout_addr = 0;
        g_active = 0;
        f18a_cpm80_set_default_smartkeys();
    }

    const uint16_t bios_base = (uint16_t)(wboot - 3);
    const uint16_t conout = bios_target((uint16_t)(bios_base + 12));

    if (conout != g_conout_addr) {
        g_conout_addr = conout;
        g_active = 0;
        f18a_cpm80_set_default_smartkeys();
    }
}

extern "C" void f18a_term80_cpm_before_opcode(uint16_t pc, uint8_t reg_c)
{
    if (!cpm_enabled_now()) {
        deactivate_if_needed();
        return;
    }

    if (f18a_is_80col_selftest_enabled())
        return;

    /*
     * Always keep detecting while 80-col is enabled. This is what makes the
     * flow work when 80 columns is ON before disk reset: the CP/M WBOOT/BIOS
     * vectors can change during boot, so a one-shot detect is not enough.
     */
    detect_bios();

    if (g_conout_addr != 0 && pc == g_conout_addr) {
        ensure_term80_ready();
        g_active = 1;

        /*
         * Smartkey detection is expensive because it scans RAM for label
         * tables. Do it once when CP/M TERM80 really starts producing
         * console output, not on every opcode.
         *
         * Visibility is cheap enough to refresh occasionally, but still only
         * on CONOUT calls. This keeps 80-column typing responsive.
         */
        if (!g_smartkeys_scanned_once && g_last_wboot >= 3) {
            const uint16_t biosBase = (uint16_t)(g_last_wboot - 3);
            f18a_cpm80_detect_smartkeys_internal(biosBase);
            g_smartkeys_scanned_once = 1;
            g_smartkey_rescan_counter = 0;
        } else if (g_smartkeys_detected && ((++g_smartkey_rescan_counter & 0x7F) == 0)) {
            f18a_cpm80_update_smartkey_visibility();
        }

        f18a_term80_put_char(reg_c);
    }
}

#include "f18a_term80_tdos.h"

extern "C" void f18a_term80_from_cpm80(void)
{
    f18a_term80_set_enabled(1);

    const uint8_t fg = 15;
    const uint8_t bg = 1;

    for (int row = 0; row < 23; ++row) {
        for (int col = 0; col < 80; ++col) {
            uint8_t ch = cpm80_get_char(row, col);

            if (ch < 32)
                ch = ' ';

            f18a_term80_put_cell(row, col, ch, fg, bg);
        }
    }

    f18a_term80_set_cursor(cpm80_get_cursor_y(), cpm80_get_cursor_x());
    f18a_term80_show_cursor(1);
}
