#include "f18a_term80.h"
#include "f18a.h"
#include "video_bridge.h"

#include <stdint.h>
#include <string.h>

#define TERM80_DEFAULT_FG 15u
#define TERM80_DEFAULT_BG 1u
#define TERM80_TITLE_FG   15u
#define TERM80_TITLE_BG   4u

#define TERM80_FIRST_TEXT_ROW 1u
#define TERM80_LAST_TEXT_ROW  (F18A_TERM80_ROWS - 1u)
#define TERM80_CURSOR_BLINK_FRAMES 30u

typedef struct Term80Cell {
    unsigned char ch;
    unsigned char fg;
    unsigned char bg;
} Term80Cell;

static int g_term80_enabled = 0;
static unsigned int g_cursor_row = TERM80_FIRST_TEXT_ROW;
static unsigned int g_cursor_col = 0;
static unsigned char g_fg = TERM80_DEFAULT_FG;
static unsigned char g_bg = TERM80_DEFAULT_BG;
static int g_cursor_visible = 1;
static int g_cursor_blink_on = 1;
static unsigned int g_cursor_blink_counter = 0;
static int g_smartkeys_visible = 0;

static Term80Cell g_cells[F18A_TERM80_ROWS][F18A_TERM80_COLS];

static void term80_put_cell_raw(unsigned int row, unsigned int col,
                                unsigned char ch,
                                unsigned char fg,
                                unsigned char bg)
{
    if (row >= F18A_TERM80_ROWS || col >= F18A_TERM80_COLS)
        return;

    g_cells[row][col].ch = ch;
    g_cells[row][col].fg = fg & 0x0Fu;
    g_cells[row][col].bg = bg & 0x0Fu;

    f18a_80col_put_char(row, col, ch, fg, bg);
}

static void term80_redraw_cell(unsigned int row, unsigned int col)
{
    if (row >= F18A_TERM80_ROWS || col >= F18A_TERM80_COLS)
        return;

    const Term80Cell* c = &g_cells[row][col];
    f18a_80col_put_char(row, col, c->ch, c->fg, c->bg);
}

static void term80_restore_cursor_cell(void)
{
    if (g_cursor_row >= F18A_TERM80_ROWS || g_cursor_col >= F18A_TERM80_COLS)
        return;

    term80_redraw_cell(g_cursor_row, g_cursor_col);
}

static unsigned int term80_last_text_row_internal(void)
{
    return g_smartkeys_visible ? (F18A_TERM80_ROWS - 3u)
                               : TERM80_LAST_TEXT_ROW;
}

static void term80_clamp_cursor(void)
{
    if (g_cursor_row < TERM80_FIRST_TEXT_ROW)
        g_cursor_row = TERM80_FIRST_TEXT_ROW;

    if (g_cursor_row > term80_last_text_row_internal())
        g_cursor_row = term80_last_text_row_internal();

    if (g_cursor_col >= F18A_TERM80_COLS)
        g_cursor_col = F18A_TERM80_COLS - 1u;
}

static void term80_draw_cursor_cell(void)
{
    if (!g_term80_enabled || !g_cursor_visible || !g_cursor_blink_on)
        return;

    term80_clamp_cursor();

    const Term80Cell* c = &g_cells[g_cursor_row][g_cursor_col];

    /*
     * Cursor is an overlay: draw the existing cell inverse,
     * but do not change the shadow buffer.
     */
    f18a_80col_put_char(g_cursor_row, g_cursor_col,
                        c->ch ? c->ch : ' ',
                        c->bg,
                        c->fg);
}

static void term80_redraw_all(void)
{
    for (unsigned int r = 0; r < F18A_TERM80_ROWS; ++r) {
        for (unsigned int c = 0; c < F18A_TERM80_COLS; ++c) {
            term80_redraw_cell(r, c);
        }
    }
}

static void term80_draw_titlebar(void)
{
    // for (unsigned int col = 0; col < F18A_TERM80_COLS; ++col)
    //     term80_put_cell_raw(0u, col, ' ', TERM80_TITLE_FG, TERM80_TITLE_BG);

    // const char* title = "  ";
    // unsigned int col = 12u;

    // while (*title && col < F18A_TERM80_COLS) {
    //     term80_put_cell_raw(0u, col++, (unsigned char)*title++, TERM80_TITLE_FG, TERM80_TITLE_BG);
    // }
}

void f18a_term80_set_enabled(int enabled)
{
    const int was_enabled = g_term80_enabled;
    g_term80_enabled = enabled ? 1 : 0;
    f18a_set_80col_enabled(g_term80_enabled);

    if (g_term80_enabled && !was_enabled)
        f18a_term80_reset();
}

int f18a_term80_is_enabled(void)
{
    return g_term80_enabled;
}

void f18a_term80_reset(void)
{
    g_fg = TERM80_DEFAULT_FG;
    g_bg = TERM80_DEFAULT_BG;
    g_cursor_visible = 1;
    g_cursor_blink_on = 1;
    g_cursor_blink_counter = 0;

    f18a_term80_clear();
}

void f18a_term80_clear(void)
{
    const unsigned char clear_fg = g_fg & 0x0Fu;
    const unsigned char clear_bg = g_bg & 0x0Fu;

    for (unsigned int r = 0; r < F18A_TERM80_ROWS; ++r) {
        for (unsigned int c = 0; c < F18A_TERM80_COLS; ++c) {
            g_cells[r][c].ch = ' ';
            g_cells[r][c].fg = clear_fg;
            g_cells[r][c].bg = clear_bg;
        }
    }

    f18a_80col_clear(' ', clear_fg, clear_bg);
    term80_draw_titlebar();

    g_fg = clear_fg;
    g_bg = clear_bg;
    g_cursor_row = TERM80_FIRST_TEXT_ROW;
    g_cursor_col = 0;

    term80_draw_cursor_cell();
}

void f18a_term80_set_colors(unsigned char fg, unsigned char bg)
{
    g_fg = fg & 0x0Fu;
    g_bg = bg & 0x0Fu;
}

void f18a_term80_apply_colors(unsigned char fg, unsigned char bg)
{
    const unsigned char new_fg = fg & 0x0Fu;
    const unsigned char new_bg = bg & 0x0Fu;

    term80_restore_cursor_cell();

    g_fg = new_fg;
    g_bg = new_bg;

    for (unsigned int r = 0; r < F18A_TERM80_ROWS; ++r) {
        for (unsigned int c = 0; c < F18A_TERM80_COLS; ++c) {
            g_cells[r][c].fg = new_fg;
            g_cells[r][c].bg = new_bg;
            f18a_80col_put_char(r, c, g_cells[r][c].ch, new_fg, new_bg);
        }
    }

    g_cursor_blink_on = 1;
    g_cursor_blink_counter = 0;
    term80_draw_cursor_cell();
}

void f18a_term80_get_colors(unsigned char* fg, unsigned char* bg)
{
    if (fg)
        *fg = g_fg;
    if (bg)
        *bg = g_bg;
}

void f18a_term80_set_cursor(unsigned int row, unsigned int col)
{
    term80_restore_cursor_cell();

    g_cursor_row = row;
    g_cursor_col = col;
    term80_clamp_cursor();

    g_cursor_blink_on = 1;
    g_cursor_blink_counter = 0;
    term80_draw_cursor_cell();
}

void f18a_term80_get_cursor(unsigned int* row, unsigned int* col)
{
    if (row)
        *row = g_cursor_row;
    if (col)
        *col = g_cursor_col;
}

void f18a_term80_show_cursor(int show)
{
    term80_restore_cursor_cell();
    g_cursor_visible = show ? 1 : 0;
    g_cursor_blink_on = 1;
    g_cursor_blink_counter = 0;
    term80_draw_cursor_cell();
}

int f18a_term80_cursor_visible(void)
{
    return g_cursor_visible;
}

static void term80_scroll_up(void)
{
    term80_restore_cursor_cell();

    /* Scroll only the terminal text area. Row 0 is reserved for title/logo. */
    const unsigned int last_row = term80_last_text_row_internal();

    for (unsigned int r = TERM80_FIRST_TEXT_ROW; r < last_row; ++r) {
        memcpy(g_cells[r], g_cells[r + 1u], sizeof(g_cells[r]));
    }

    for (unsigned int c = 0; c < F18A_TERM80_COLS; ++c) {
        g_cells[last_row][c].ch = ' ';
        g_cells[last_row][c].fg = g_fg;
        g_cells[last_row][c].bg = g_bg;
    }

    term80_redraw_all();
    term80_draw_titlebar();

    g_cursor_row = term80_last_text_row_internal();
    g_cursor_col = 0;
}

static void term80_newline(void)
{
    term80_restore_cursor_cell();

    g_cursor_col = 0;
    if (++g_cursor_row > term80_last_text_row_internal())
        term80_scroll_up();

    term80_draw_cursor_cell();
}

void f18a_term80_delete_previous_char(void)
{
    if (!g_term80_enabled)
        return;

    term80_restore_cursor_cell();

    if (g_cursor_col > 0) {
        --g_cursor_col;
    } else if (g_cursor_row > TERM80_FIRST_TEXT_ROW) {
        --g_cursor_row;
        g_cursor_col = F18A_TERM80_COLS - 1u;
    }

    term80_put_cell_raw(g_cursor_row, g_cursor_col, ' ', g_fg, g_bg);
    term80_draw_cursor_cell();
}

void f18a_term80_put_char(unsigned char ch)
{
    if (!g_term80_enabled)
        return;

    switch (ch)
    {
    case '\r':
        term80_restore_cursor_cell();
        g_cursor_col = 0;
        term80_draw_cursor_cell();
        return;

    case '\n':
        term80_newline();
        return;

    case '\b':
    case 0x7Fu:
        /* Destructive backspace, as requested for interactive typing. */
        f18a_term80_delete_previous_char();
        return;

    case '\t':
        do {
            f18a_term80_put_char(' ');
        } while ((g_cursor_col & 7u) != 0u);
        return;

    case 0x0C: /* form feed / clear screen */
        f18a_term80_clear();
        return;

    default:
        break;
    }

    if (ch < 0x20u)
        return;

    term80_restore_cursor_cell();

    term80_put_cell_raw(g_cursor_row, g_cursor_col, ch, g_fg, g_bg);

    if (++g_cursor_col >= F18A_TERM80_COLS)
        term80_newline();
    else
        term80_draw_cursor_cell();
}


unsigned char f18a_term80_get_char(unsigned int row, unsigned int col)
{
    if (row >= F18A_TERM80_ROWS || col >= F18A_TERM80_COLS)
        return ' ';
    return g_cells[row][col].ch;
}

unsigned char f18a_term80_get_fg(unsigned int row, unsigned int col)
{
    if (row >= F18A_TERM80_ROWS || col >= F18A_TERM80_COLS)
        return g_fg;
    return g_cells[row][col].fg;
}

unsigned char f18a_term80_get_bg(unsigned int row, unsigned int col)
{
    if (row >= F18A_TERM80_ROWS || col >= F18A_TERM80_COLS)
        return g_bg;
    return g_cells[row][col].bg;
}

void f18a_term80_put_cell(unsigned int row, unsigned int col,
                          unsigned char ch,
                          unsigned char fg,
                          unsigned char bg)
{
    if (!g_term80_enabled)
        return;

    if (row >= F18A_TERM80_ROWS || col >= F18A_TERM80_COLS)
        return;

    term80_restore_cursor_cell();
    term80_put_cell_raw(row, col, ch, fg, bg);
    term80_draw_cursor_cell();
}

void f18a_term80_write_text(const char* text)
{
    if (!text)
        return;

    while (*text)
        f18a_term80_put_char((unsigned char)*text++);
}

void f18a_term80_write_at(unsigned int row, unsigned int col,
                          const char* text,
                          unsigned char fg,
                          unsigned char bg)
{
    if (!text)
        return;

    term80_restore_cursor_cell();

    const unsigned int old_row = g_cursor_row;
    const unsigned int old_col = g_cursor_col;
    const unsigned char old_fg = g_fg;
    const unsigned char old_bg = g_bg;

    g_cursor_row = row;
    g_cursor_col = col;
    term80_clamp_cursor();
    f18a_term80_set_colors(fg, bg);

    while (*text && g_cursor_row < F18A_TERM80_ROWS)
        f18a_term80_put_char((unsigned char)*text++);

    g_fg = old_fg;
    g_bg = old_bg;
    g_cursor_row = old_row;
    g_cursor_col = old_col;
    term80_clamp_cursor();

    term80_draw_cursor_cell();
}

void f18a_term80_set_smartkeys_visible(int visible)
{
    const int new_visible = visible ? 1 : 0;
    if (g_smartkeys_visible == new_visible)
        return;

    term80_restore_cursor_cell();
    g_smartkeys_visible = new_visible;
    term80_clamp_cursor();
    term80_draw_cursor_cell();
}

int f18a_term80_smartkeys_visible(void)
{
    return g_smartkeys_visible;
}

unsigned int f18a_term80_last_text_row(void)
{
    return term80_last_text_row_internal();
}

void f18a_term80_tick(void)
{
    if (!g_term80_enabled || !g_cursor_visible)
        return;

    if (++g_cursor_blink_counter < TERM80_CURSOR_BLINK_FRAMES)
        return;

    g_cursor_blink_counter = 0;

    term80_restore_cursor_cell();
    g_cursor_blink_on = !g_cursor_blink_on;
    term80_draw_cursor_cell();
}

void f18a_term80_demo_screen(void)
{
    f18a_term80_set_enabled(1);
    f18a_term80_clear();

    f18a_term80_write_at(1,  0, "ADAM+ F18A TERM80 - SEPARATE TERMINAL LAYER", 7, 1);
    f18a_term80_write_at(2,  0, "THIS DOES NOT USE cpm80.cpp AND DOES NOT DRAW THE OLD PNG OVERLAY", 15, 1);
    f18a_term80_write_at(4,  0, "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ01234567", 14, 1);
    f18a_term80_write_at(6,  0, "TERM80 API:", 10, 1);
    f18a_term80_write_at(7,  2, "f18a_term80_put_char()", 11, 1);
    f18a_term80_write_at(8,  2, "f18a_term80_write_text()", 11, 1);
    f18a_term80_write_at(9,  2, "f18a_term80_clear()", 11, 1);
    f18a_term80_write_at(10, 2, "f18a_term80_set_cursor()", 11, 1);
    f18a_term80_write_at(13, 0, "CP/M TEXT AREA STARTS ON ROW 1. ROW 0 IS RESERVED FOR THE PNG/TITLE BAR.", 12, 1);
    f18a_term80_write_at(15, 0, "BACKSPACE IS DESTRUCTIVE. CURSOR IS AN OVERLAY AND SHOULD NOT EAT A>.", 9, 1);
    f18a_term80_write_at(23, 0, "TERM80 READY.", 10, 1);
}


/* =============================================================================
 * The 80-column display
 * =============================================================================
 * Moved here from f18a.c. None of it is the VDP: the characters come from CP/M and
 * T-DOS through the terminal above, never from VRAM, and the picture is presented
 * straight to the video bridge. It lived in the renderer only because the renderer
 * was the thing that used to draw it, which made f18a.c load-bearing for CP/M under
 * any engine. The colours come from f18a.c's palette through the accessor the header
 * already published, but only entries 0-15 - the cell attributes are masked to a
 * nibble - and under pico9918-core nothing reprograms that palette, so in practice
 * these are the sixteen TMS defaults f18a_reset_80col_overlay() puts there.
 */

#define F18A_80COL_WIDTH   480
#define F18A_80COL_HEIGHT  192
#define F18A_80COL_COLS    F18A_TERM80_COLS
#define F18A_80COL_ROWS    F18A_TERM80_ROWS

static int g_f18a_80col_enabled = 0;
static int g_f18a_80col_selftest_enabled = 0;

/*
 * C7: internal 80x24 text buffer.
 * This is independent from normal TMS-compatible VRAM rendering.
 * CP/M/debug/terminal code writes here directly.
 */
static unsigned char g_80col_char[F18A_80COL_ROWS][F18A_80COL_COLS];
static unsigned char g_80col_fg  [F18A_80COL_ROWS][F18A_80COL_COLS];
static unsigned char g_80col_bg  [F18A_80COL_ROWS][F18A_80COL_COLS];
static int g_80col_buffer_initialized = 0;

/* The same 12-bit-to-ARGB expansion f18a.c's renderer does, over the same palette -
   reached through f18a_get_palette_entry() so this file needs nothing private. */
static inline uint32_t f18a_color(unsigned int idx)
{
    const unsigned short rgb = f18a_get_palette_entry(idx & 0x3Fu);
    const unsigned int r = (rgb >> 8) & 0x0Fu;
    const unsigned int g = (rgb >> 4) & 0x0Fu;
    const unsigned int b = rgb & 0x0Fu;
    return 0xFF000000u | (r * 17u << 16) | (g * 17u << 8) | (b * 17u);
}

static uint8_t f18a_font5x7(char ch, int row)
{
    /* compact built-in font, 5 pixels wide inside a 6-pixel cell */
    static const uint8_t space[7]      = {0x00,0x00,0x00,0x00,0x00,0x00,0x00};
    static const uint8_t excl[7]       = {0x04,0x04,0x04,0x04,0x04,0x00,0x04};
    static const uint8_t quote[7]      = {0x0A,0x0A,0x0A,0x00,0x00,0x00,0x00};
    static const uint8_t hash[7]       = {0x0A,0x1F,0x0A,0x0A,0x1F,0x0A,0x00};
    static const uint8_t dollar[7]     = {0x04,0x0F,0x14,0x0E,0x05,0x1E,0x04};
    static const uint8_t percent[7]    = {0x18,0x19,0x02,0x04,0x08,0x13,0x03};
    static const uint8_t amp[7]        = {0x0C,0x12,0x14,0x08,0x15,0x12,0x0D};
    static const uint8_t apost[7]      = {0x04,0x04,0x08,0x00,0x00,0x00,0x00};
    static const uint8_t lparen[7]     = {0x02,0x04,0x08,0x08,0x08,0x04,0x02};
    static const uint8_t rparen[7]     = {0x08,0x04,0x02,0x02,0x02,0x04,0x08};
    static const uint8_t star[7]       = {0x00,0x15,0x0E,0x1F,0x0E,0x15,0x00};
    static const uint8_t plus[7]       = {0x00,0x04,0x04,0x1F,0x04,0x04,0x00};
    static const uint8_t comma[7]      = {0x00,0x00,0x00,0x00,0x0C,0x04,0x08};
    static const uint8_t dash[7]       = {0x00,0x00,0x00,0x1F,0x00,0x00,0x00};
    static const uint8_t dot[7]        = {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C};
    static const uint8_t slash[7]      = {0x01,0x02,0x02,0x04,0x08,0x08,0x10};

    static const uint8_t colon[7]      = {0x00,0x0C,0x0C,0x00,0x0C,0x0C,0x00};
    static const uint8_t semicolon[7]  = {0x00,0x0C,0x0C,0x00,0x0C,0x04,0x08};
    static const uint8_t less[7]       = {0x02,0x04,0x08,0x10,0x08,0x04,0x02};
    static const uint8_t equal[7]      = {0x00,0x00,0x1F,0x00,0x1F,0x00,0x00};
    static const uint8_t greater[7]    = {0x10,0x08,0x04,0x02,0x04,0x08,0x10};
    static const uint8_t question[7]   = {0x0E,0x11,0x01,0x02,0x04,0x00,0x04};
    static const uint8_t atsign[7]     = {0x0E,0x11,0x17,0x15,0x17,0x10,0x0E};

    static const uint8_t lbrack[7]     = {0x0E,0x08,0x08,0x08,0x08,0x08,0x0E};
    static const uint8_t backslash[7]  = {0x10,0x08,0x08,0x04,0x02,0x02,0x01};
    static const uint8_t rbrack[7]     = {0x0E,0x02,0x02,0x02,0x02,0x02,0x0E};
    static const uint8_t caret[7]      = {0x04,0x0A,0x11,0x00,0x00,0x00,0x00};
    static const uint8_t underscore[7] = {0x00,0x00,0x00,0x00,0x00,0x00,0x1F};
    static const uint8_t grave[7]      = {0x08,0x04,0x02,0x00,0x00,0x00,0x00};
    static const uint8_t lbrace[7]     = {0x02,0x04,0x04,0x08,0x04,0x04,0x02};
    static const uint8_t pipe[7]       = {0x04,0x04,0x04,0x00,0x04,0x04,0x04};
    static const uint8_t rbrace[7]     = {0x08,0x04,0x04,0x02,0x04,0x04,0x08};
    static const uint8_t tilde[7]      = {0x00,0x00,0x08,0x15,0x02,0x00,0x00};

    static const uint8_t nums[10][7] = {
        /* 0 */ {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E},
        /* 1 */ {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E},
        /* 2 */ {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F},
        /* 3 */ {0x1F,0x02,0x04,0x02,0x01,0x11,0x0E},
        /* 4 */ {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02},
        /* 5 */ {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E},
        /* 6 */ {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E},
        /* 7 */ {0x1F,0x01,0x02,0x04,0x08,0x08,0x08},
        /* 8 */ {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E},
        /* 9 */ {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C}
    };

    static const uint8_t upper[26][7] = {
        /* A */ {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11},
        /* B */ {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E},
        /* C */ {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E},
        /* D */ {0x1E,0x11,0x11,0x11,0x11,0x11,0x1E},
        /* E */ {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F},
        /* F */ {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10},
        /* G */ {0x0E,0x11,0x10,0x17,0x11,0x11,0x0F},
        /* H */ {0x11,0x11,0x11,0x1F,0x11,0x11,0x11},
        /* I */ {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E},
        /* J */ {0x01,0x01,0x01,0x01,0x11,0x11,0x0E},
        /* K */ {0x11,0x12,0x14,0x18,0x14,0x12,0x11},
        /* L */ {0x10,0x10,0x10,0x10,0x10,0x10,0x1F},
        /* M */ {0x11,0x1B,0x15,0x15,0x11,0x11,0x11},
        /* N */ {0x11,0x19,0x15,0x13,0x11,0x11,0x11},
        /* O */ {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E},
        /* P */ {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10},
        /* Q */ {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D},
        /* R */ {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11},
        /* S */ {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E},
        /* T */ {0x1F,0x04,0x04,0x04,0x04,0x04,0x04},
        /* U */ {0x11,0x11,0x11,0x11,0x11,0x11,0x0E},
        /* V */ {0x11,0x11,0x11,0x11,0x11,0x0A,0x04},
        /* W */ {0x11,0x11,0x11,0x15,0x15,0x1B,0x11},
        /* X */ {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11},
        /* Y */ {0x11,0x11,0x0A,0x04,0x04,0x04,0x04},
        /* Z */ {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}
    };

    static const uint8_t lower[26][7] = {
        /* a */ {0x00,0x00,0x0E,0x01,0x0F,0x11,0x0F},
        /* b */ {0x10,0x10,0x16,0x19,0x11,0x11,0x1E},
        /* c */ {0x00,0x00,0x0E,0x10,0x10,0x11,0x0E},
        /* d */ {0x01,0x01,0x0D,0x13,0x11,0x11,0x0F},
        /* e */ {0x00,0x00,0x0E,0x11,0x1F,0x10,0x0E},
        /* f */ {0x06,0x08,0x08,0x1E,0x08,0x08,0x08},
        /* g */ {0x00,0x00,0x0F,0x11,0x0F,0x01,0x0E},
        /* h */ {0x10,0x10,0x16,0x19,0x11,0x11,0x11},
        /* i */ {0x04,0x00,0x0C,0x04,0x04,0x04,0x0E},
        /* j */ {0x02,0x00,0x06,0x02,0x02,0x12,0x0C},
        /* k */ {0x10,0x10,0x12,0x14,0x18,0x14,0x12},
        /* l */ {0x0C,0x04,0x04,0x04,0x04,0x04,0x0E},
        /* m */ {0x00,0x00,0x1A,0x15,0x15,0x11,0x11},
        /* n */ {0x00,0x00,0x16,0x19,0x11,0x11,0x11},
        /* o */ {0x00,0x00,0x0E,0x11,0x11,0x11,0x0E},
        /* p */ {0x00,0x00,0x1E,0x11,0x1E,0x10,0x10},
        /* q */ {0x00,0x00,0x0F,0x11,0x0F,0x01,0x01},
        /* r */ {0x00,0x00,0x16,0x19,0x10,0x10,0x10},
        /* s */ {0x00,0x00,0x0F,0x10,0x0E,0x01,0x1E},
        /* t */ {0x08,0x08,0x1E,0x08,0x08,0x09,0x06},
        /* u */ {0x00,0x00,0x11,0x11,0x11,0x13,0x0D},
        /* v */ {0x00,0x00,0x11,0x11,0x11,0x0A,0x04},
        /* w */ {0x00,0x00,0x11,0x11,0x15,0x15,0x0A},
        /* x */ {0x00,0x00,0x11,0x0A,0x04,0x0A,0x11},
        /* y */ {0x00,0x00,0x11,0x11,0x0F,0x01,0x0E},
        /* z */ {0x00,0x00,0x1F,0x02,0x04,0x08,0x1F}
    };

    if (row < 0 || row >= 7)
        return 0;

    if (ch >= '0' && ch <= '9') return nums[ch - '0'][row];
    if (ch >= 'A' && ch <= 'Z') return upper[ch - 'A'][row];
    if (ch >= 'a' && ch <= 'z') return lower[ch - 'a'][row];

    switch (ch) {
        case ' ': return space[row];
        case '!': return excl[row];
        case '"': return quote[row];
        case '#': return hash[row];
        case '$': return dollar[row];
        case '%': return percent[row];
        case '&': return amp[row];
        case '\'': return apost[row];
        case '(': return lparen[row];
        case ')': return rparen[row];
        case '*': return star[row];
        case '+': return plus[row];
        case ',': return comma[row];
        case '-': return dash[row];
        case '.': return dot[row];
        case '/': return slash[row];
        case ':': return colon[row];
        case ';': return semicolon[row];
        case '<': return less[row];
        case '=': return equal[row];
        case '>': return greater[row];
        case '?': return question[row];
        case '@': return atsign[row];
        case '[': return lbrack[row];
        case '\\': return backslash[row];
        case ']': return rbrack[row];
        case '^': return caret[row];
        case '_': return underscore[row];
        case '`': return grave[row];
        case '{': return lbrace[row];
        case '|': return pipe[row];
        case '}': return rbrace[row];
        case '~': return tilde[row];
        default:  return space[row];
    }
}

void f18a_term80_display_reset(void)
{
    g_80col_buffer_initialized = 0;
}

static void f18a_80col_init_buffer_once(void)
{
    if (g_80col_buffer_initialized)
        return;

    f18a_80col_clear(' ', 15, 1);
    g_80col_buffer_initialized = 1;
}

void f18a_80col_clear(unsigned char ch, unsigned char fg, unsigned char bg)
{
    for (unsigned int r = 0; r < F18A_80COL_ROWS; ++r)
    {
        for (unsigned int c = 0; c < F18A_80COL_COLS; ++c)
        {
            g_80col_char[r][c] = ch;
            g_80col_fg[r][c] = fg & 0x0Fu;
            g_80col_bg[r][c] = bg & 0x0Fu;
        }
    }

    g_80col_buffer_initialized = 1;
}

void f18a_80col_put_char(unsigned int row, unsigned int col,
                         unsigned char ch,
                         unsigned char fg,
                         unsigned char bg)
{
    if (row >= F18A_80COL_ROWS || col >= F18A_80COL_COLS)
        return;

    g_80col_char[row][col] = ch;
    g_80col_fg[row][col] = fg & 0x0Fu;
    g_80col_bg[row][col] = bg & 0x0Fu;
}

void f18a_80col_write_text(unsigned int row, unsigned int col,
                           const char* text,
                           unsigned char fg,
                           unsigned char bg)
{
    if (!text || row >= F18A_80COL_ROWS || col >= F18A_80COL_COLS)
        return;

    while (*text && col < F18A_80COL_COLS)
    {
        f18a_80col_put_char(row, col, (unsigned char)*text, fg, bg);
        ++text;
        ++col;
    }
}

static void f18a_80col_build_selftest_text(void)
{
    f18a_80col_clear(' ', 15, 1);

    f18a_80col_write_text(0,  0, "ADAM+ F18A 80 COLUMN TEXT BUFFER API - 480X192 INTERNAL", 7, 1);
    f18a_80col_write_text(1,  0, "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ01234567", 15, 1);
    f18a_80col_write_text(2,  0, "COL 00000000011111111112222222222333333333344444444445555555555666666666677777777", 10, 1);
    f18a_80col_write_text(3,  0, "COL 01234567890123456789012345678901234567890123456789012345678901234567890123456789", 11, 1);
    f18a_80col_write_text(4,  0, "ROW 04  THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG - 80 COLUMNS VISIBLE", 15, 1);
    f18a_80col_write_text(5,  0, "ROW 05  THIS SCREEN IS DRAWN FROM THE INTERNAL 80X24 TEXT BUFFER", 14, 1);
    f18a_80col_write_text(6,  0, "ROW 06  USE f18a_80col_put_char AND f18a_80col_write_text FOR CP/M LATER", 3, 1);
    f18a_80col_write_text(7,  0, "ROW 07  6X8 CELLS - 80 COLUMNS X 24 ROWS", 12, 1);
    f18a_80col_write_text(9,  0, "##########----------::::::::::++++++++++..........//////////##########", 9, 1);
    f18a_80col_write_text(11, 0, "ROW 11  NORMAL F18A GAME RENDERING IS UNCHANGED WHEN SELF TEST IS OFF", 15, 1);
    f18a_80col_write_text(14, 0, "ROW 14  NEXT STEP CAN CONNECT CP/M OUTPUT TO THIS TEXT BUFFER", 7, 1);
    f18a_80col_write_text(23, 0, "ROW 23  END OF F18A 80 COLUMN TEXT BUFFER TEST", 10, 1);
}

static void f18a_render_80col_textbuffer(void)
{
    f18a_80col_init_buffer_once();

    uint32_t line[F18A_80COL_WIDTH];

    for (int y = 0; y < F18A_80COL_HEIGHT; ++y)
    {
        const unsigned int text_row = (unsigned int)y >> 3;
        const int pixel_row = y & 7;

        for (unsigned int col = 0; col < F18A_80COL_COLS; ++col)
        {
            const unsigned char ch = g_80col_char[text_row][col];
            const uint32_t fg = f18a_color(g_80col_fg[text_row][col]);
            const uint32_t bg = f18a_color(g_80col_bg[text_row][col]);
            const uint8_t bits = f18a_font5x7((char)ch, pixel_row);
            const unsigned int x0 = col * 6u;

            for (unsigned int b = 0; b < 6u; ++b)
                line[x0 + b] = (b < 5u && (bits & (0x10u >> b))) ? fg : bg;
        }

        vb_present_scanline_ex(y, line, F18A_80COL_WIDTH);
    }

    vb_present_frame();
    video_set_dirty(1);
}

int f18a_present_80col_overlay(void)
{
    if (!g_f18a_80col_enabled)
        return 0;

    /* The self-test wants something on screen even before CP/M or T-DOS has written a
       character, so put the demo up while the buffer is still untouched. */
    if (g_f18a_80col_selftest_enabled)
    {
        if (!f18a_term80_is_enabled() || !g_80col_buffer_initialized)
            f18a_term80_demo_screen();
    }

    /* f18a_loop() blinks the cursor once per rendered frame. It is not running when
       another engine renders the VDP, so the tick comes from here instead. */
    if (f18a_term80_is_enabled())
        f18a_term80_tick();

    f18a_render_80col_textbuffer();
    return 1;
}

void f18a_set_80col_enabled(int enabled)
{
    g_f18a_80col_enabled = enabled ? 1 : 0;
}

int f18a_is_80col_enabled(void)
{
    return g_f18a_80col_enabled;
}

void f18a_set_80col_selftest_enabled(int enabled)
{
    g_f18a_80col_selftest_enabled = enabled ? 1 : 0;

    /*
     * C8-safe-2:
     * Self-test is the only UI option that should force the 480x192
     * 80-column renderer for now. When enabled, build the screen through
     * the separate TERM80 terminal layer. When disabled, return to normal
     * TMS-compatible F18A rendering.
     */
    if (g_f18a_80col_selftest_enabled)
    {
        f18a_term80_set_enabled(1);
        f18a_term80_demo_screen();
        g_f18a_80col_enabled = 1;
    }
    else
    {
        f18a_term80_set_enabled(0);
        g_f18a_80col_enabled = 0;
    }
}

int f18a_is_80col_selftest_enabled(void)
{
    return g_f18a_80col_selftest_enabled;
}
