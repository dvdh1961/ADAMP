#include "f18a_term80.h"
#include "f18a.h"

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
