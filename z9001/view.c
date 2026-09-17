#include <stdint.h>
#include "hw.h"
#include "bombs.h"
#include "view.h"

/*
 * Screen and speaker only -- no game rules live here. See view.h for the
 * public API and the screen layout constants.
 *
 * This is the Z9001 take on the KC85 view: one 40x24 text screen, laid out
 * the way the original Atari version was (bomb up top, manual below) rather
 * than the KC85's left/right split. There is no user font to draw -- the
 * built-in ROM chargen is used directly -- so every G_* symbol is a plain
 * character code that gets poked into char RAM at $EC00.
 */

/* ------------------------------------------------------------------- */
/* Language table.                                                     */
/* ------------------------------------------------------------------- */

/* Every string the player can read, one struct per language, so
   view_lang_toggle() swaps the whole UI with a single pointer flip. The
   text is uppercase on purpose: the Z9001 chargen renders lowercase letters
   as inverse capitals, which none of these labels want to read as. */
typedef struct {
    const char *score;
    const char *defused;
    const char *safe;
    const char *manual;
    const char *cover[3];
    const char *pages;
    const char *col_order;
    const char *col_wire;
    const char *wires;
    const char *colour[6];
    const char *order[6];
    const char *legend;
    const char *title_keys;
    const char *cont;
    const char *boom;
    const char *final;
    const char *press;
    const char *menu[MENU_ITEMS];
} Lang;

static const Lang lang_en = {
    "SCORE ", "DEFUSED ", "SAFE", "BOMB DEFUSAL MANUAL",
    { "BOMB", "DEFUSAL", "MANUAL" }, "48 PAGES",
    "CUT", "WIRE", " wires",
    { "RED", "RED STRIPED", "BLUE", "BLUE STRIPED",
      "GREEN", "GREEN STRIPED" },
    { "no", "1st", "2nd", "3rd", "4th", "5th" },
    "A/D PAGE  W/S WIRE  SPACE CUT",
    "A/D CHOOSE  SPACE START",
    "PRESS ANY KEY",
    "BOOM!", "FINAL SCORE ", "PRESS SPACE",
    { "START", "DEUTSCH", "EXIT" }
};

static const Lang lang_de = {
    "PUNKTE ", "GESCHAFFT ", "FREI", "BOMBEN-HANDBUCH",
    { "DAS", "BOMBEN", "HANDBOOK" }, "48 SEITEN",
    "SCHNEIDE", "DRAHT", " Draehte",
    { "ROT", "ROT GESTR.", "BLAU", "BLAU GESTR.",
      "GRUEN", "GRUEN GESTR." },
    { "nie", "1.", "2.", "3.", "4.", "5." },
    "A/D SEITE  W/S DRAHT  LEER SCHNEIDEN",
    "A/D AUSWAHL  LEER STARTEN",
    "WEITER MIT TASTE",
    "BUMM!", "PUNKTE GESAMT ", "LEERTASTE",
    { "START", "ENGLISH", "ENDE" }
};

/* A DATA static, deliberately NOT reset by view_init(): a restart from the
   monitor (MENU_EXIT -> jp start) re-enters here through the crt, which
   zeroes the BSS but leaves an initialised static as it was left, so a
   language chosen before a RESET is still in force on the next run. */
static const Lang *L = &lang_en;

void view_lang_toggle(void) { L = (L == &lang_en) ? &lang_de : &lang_en; }

/* ------------------------------------------------------------------- */
/* Glyphs and small helpers.                                           */
/* ------------------------------------------------------------------- */

/* The chargen has no box-drawing set (0xC0-0xD7 are block/quarter-block
   elements), so rectangles use the ASCII border chars and fills use the
   full-block glyph. Read back straight out of chargen.851 in the probe. */
#define G_BLANK     ' '
#define G_SOLID     0xFF        /* full block */
#define G_STRIPE    '/'         /* diagonal hatch for striped wires */
#define G_RULE      '='         /* horizontal rule */
#define G_LINE_V    '|'
#define G_CORNER    '+'
#define G_RIGHT     '>'         /* menu cursor marker */
#define G_CUT_L     0xC1        /* left half -- severed wire's left end */
#define G_CUT_R     0xC0        /* right half */
#define G_LAMP      0xFF        /* a filled disc the size of the lamp */
#define G_SCISSORS  'X'         /* open, crossing over the wire */
#define G_SCISSORS_SHUT '#'     /* blades closed */

static uint8_t str_len(const char *s)
{
    uint8_t n = 0;
    while (s[n]) n++;
    return n;
}

/* Fixed-width zero-padded decimal, right-aligned in a `width` field. */
static void put_udec(uint8_t col, uint8_t row, uint16_t v, uint8_t width,
                     uint8_t attr)
{
    char buf[7];
    uint8_t i;
    for (i = width; i > 0; i--) {
        buf[i - 1] = (char)('0' + (v % 10));
        v /= 10;
    }
    buf[width] = 0;
    scr_puts(col, row, buf, attr);
}

/* Writes s at (col,row) and blanks the rest of a `width` field, so a shorter
   string always covers whatever the previous one left in its place. */
static void put_field(uint8_t col, uint8_t row, const char *s, uint8_t width,
                      uint8_t attr)
{
    uint8_t len = str_len(s);
    scr_puts(col, row, s, attr);
    if (len < width) scr_fill((uint8_t)(col + len), row,
                              (uint8_t)(width - len), G_BLANK, attr);
}

/* Wire colour helpers. colour is 0..5: odd = striped, colour>>1 picks the
   base hue (0 red, 1 blue, 2 green). On a coloured background a non-striped
   wire reads as a solid band (black on the colour), a striped one as a
   white hatch over it. */
static uint8_t wire_bg(uint8_t colour) {
    switch (colour >> 1) {
        case 0: return BG_RED;
        case 1: return BG_BLUE;
        default: return BG_GREEN;
    }
}
static uint8_t wire_fg(uint8_t colour) {
    switch (colour >> 1) {
        case 0: return FG_RED;
        case 1: return FG_BLUE;
        default: return FG_GREEN;
    }
}
static uint8_t wire_glyph(uint8_t colour) {
    return (uint8_t)((colour & 1) ? G_STRIPE : G_BLANK);
}
static uint8_t wire_attr(uint8_t colour) {
    return (uint8_t)(((colour & 1) ? FG_WHITE : FG_BLACK) | wire_bg(colour));
}

/* The page currently drawn, or PAGE_NONE when something else has been drawn
   over the manual and the next view_manual() has to lay it out again. */
#define PAGE_NONE 0xFF
static uint8_t drawn_page = PAGE_NONE;
static uint8_t drawn_wires;

/* Fills the whole manual panel with one background, the common ground for a
   page, its rule and its entries. */
static void manual_paint(uint8_t attr)
{
    uint8_t row;
    for (row = BOOK_ROW0; row <= BOOK_ROW1; row++)
        scr_fill(BOOK_COL0, row, BOOK_W, G_BLANK, attr);
}

/* ------------------------------------------------------------------- */
/* Timing.                                                             */
/* ------------------------------------------------------------------- */

void view_wait(uint8_t ticks)
{
    uint8_t start = clk_ticks();
    while ((uint8_t)(start - clk_ticks()) < ticks) ;
}

/* ------------------------------------------------------------------- */
/* Setup and layout.                                                   */
/* ------------------------------------------------------------------- */

void view_init(void)
{
    /* A restart re-enters through the crt, which zeroes BSS, so the statics
       here come back zeroed on their own -- unlike drawn_page's comment in
       the KC85, this one does not need a hand reset. */
    drawn_page = PAGE_NONE;
    drawn_wires = 0;
    scr_cls(ATTR(FG_BLACK, BG_BLUE));
}

static void outline(uint8_t c0, uint8_t r0, uint8_t c1, uint8_t r1, uint8_t attr)
{
    uint8_t span = (uint8_t)(c1 - c0 - 1);
    uint8_t r;
    scr_fill(c0, r0, 1, G_CORNER, attr);
    scr_fill((uint8_t)(c0 + 1), r0, span, G_RULE, attr);
    scr_fill(c1, r0, 1, G_CORNER, attr);
    for (r = (uint8_t)(r0 + 1); r < r1; r++) {
        scr_fill(c0, r, 1, G_LINE_V, attr);
        scr_fill(c1, r, 1, G_LINE_V, attr);
    }
    scr_fill(c0, r1, 1, G_CORNER, attr);
    scr_fill((uint8_t)(c0 + 1), r1, span, G_RULE, attr);
    scr_fill(c1, r1, 1, G_CORNER, attr);
}

void view_frame(void)
{
    /* The shared frame: the blue status bar, the bomb's case, and the prompt
       bar. The manual lives in the gap between the case and the prompt and
       is laid down later, by view_book_rise()/view_manual(). This is only
       ever called from view_title(), so it is the clean slate she paints on. */
    scr_cls(ATTR(FG_BLACK, BG_BLUE));
    scr_fill(0, ROW_STATUS, SCR_COLS, G_BLANK, FG_WHITE | BG_BLUE);
    outline(WIRE_BOX_LEFT, WIRE_BOX_TOP, WIRE_BOX_RIGHT, WIRE_BOX_BOTTOM,
            FG_WHITE | BG_BLACK);
    /* A black interior so every coloured wire band reads against it -- a
       solid blue wire on the blue screen background would otherwise vanish. */
    {
        uint8_t r;
        for (r = (uint8_t)(WIRE_BOX_TOP + 1); r < WIRE_BOX_BOTTOM; r++)
            scr_attr((uint8_t)(WIRE_BOX_LEFT + 1), r,
                     (uint8_t)(WIRE_BOX_RIGHT - WIRE_BOX_LEFT - 1),
                     ATTR(FG_BLACK, BG_BLACK));
    }
    scr_fill(0, BOOK_ROW0, BOOK_W, G_RULE, FG_BLACK | BG_BLUE);
    view_prompt(0);
    drawn_page = PAGE_NONE;
}

/* ------------------------------------------------------------------- */
/* The bomb.                                                           */
/* ------------------------------------------------------------------- */

void view_wire(const Bomb *b, uint8_t idx, uint8_t severed)
{
    uint8_t row = WIRE_ROW(idx);
    uint8_t colour = b->colour[idx];
    uint8_t fg = wire_fg(colour);

    scr_fill(WIRE_COL0, row, WIRE_W, wire_glyph(colour), wire_attr(colour));
    if (severed) {
        scr_fill(CUT_COL_L, row, 1, G_CUT_L, (uint8_t)(fg | BG_BLACK));
        scr_fill(SCISSORS_COL, row, 1, G_BLANK, FG_BLACK | BG_BLACK);
        scr_fill(CUT_COL_R, row, 1, G_CUT_R, (uint8_t)(fg | BG_BLACK));
    }
}

void view_bomb(const Bomb *b)
{
    uint8_t i;
    for (i = 0; i < b->num_wires; i++) view_wire(b, i, 0);
    for (; i < MAX_WIRES; i++)
        scr_fill(WIRE_COL0, WIRE_ROW(i), WIRE_W, G_BLANK, FG_BLACK | BG_BLACK);
}

void view_scissors(const Bomb *b, uint8_t cut_mask, uint8_t old_idx,
                   uint8_t new_idx)
{
    uint8_t row, bg, severed;

    if (old_idx != 0xFF && old_idx != new_idx) {
        severed = (uint8_t)((cut_mask >> old_idx) & 1);
        view_wire(b, old_idx, severed);
    }

    row = WIRE_ROW(new_idx);
    severed = (uint8_t)((cut_mask >> new_idx) & 1);
    bg = severed ? BG_BLACK : wire_bg(b->colour[new_idx]);
    scr_putc(SCISSORS_COL, row, G_SCISSORS, (uint8_t)(FG_WHITE | bg));
}

void view_cut_anim(const Bomb *b, uint8_t cut_mask, uint8_t idx)
{
    uint8_t row = WIRE_ROW(idx);
    uint8_t bg = (uint8_t)(((cut_mask >> idx) & 1) ? BG_BLACK
                                                     : wire_bg(b->colour[idx]));
    uint8_t attr = (uint8_t)(FG_WHITE | bg);

    scr_putc(SCISSORS_COL, row, G_SCISSORS_SHUT, attr);
    snd_tone(CUT_PITCH, 20);
    view_wait(1);
    snd_off();
    view_wait(3);
    scr_putc(SCISSORS_COL, row, G_SCISSORS, attr);
}

void view_light(uint8_t state)
{
    /* One lamp, one cell: red while counting, green when safe, black otherwise
       (which makes the cell read as empty -- an unlit lamp in an unlit panel). */
    uint8_t fg = (uint8_t)((state == LIGHT_RED) ? FG_RED
                          : (state == LIGHT_GREEN) ? FG_GREEN
                          : FG_BLACK);
    scr_fill(LIGHT_COL, LIGHT_ROW, 1, G_LAMP, (uint8_t)(fg | BG_BLACK));
}

void view_status(uint16_t score, uint8_t defused)
{
    uint8_t attr = FG_WHITE | BG_BLUE;
    scr_puts(0, ROW_STATUS, L->score, attr);
    put_udec(str_len(L->score), ROW_STATUS, score, 6, attr);
    scr_puts((uint8_t)(38 - str_len(L->defused)), ROW_STATUS, L->defused, attr);
    put_udec(38, ROW_STATUS, defused, 2, attr);
}

void view_timer(uint8_t ticks)
{
    /* TIMER_W is wide enough for SAFE, so the two digits are centred in it
       and the field wiped first, otherwise an earlier SAFE leaves its S and E
       sitting either side of the new count. */
    uint8_t attr = FG_RED | BG_BLACK;
    scr_fill(TIMER_COL, TIMER_ROW, TIMER_W, G_BLANK, attr);
    put_udec((uint8_t)(TIMER_COL + 1), TIMER_ROW, ticks, 2, attr);
}

/* ------------------------------------------------------------------- */
/* Manual.                                                             */
/* ------------------------------------------------------------------- */

void view_book_rise(void)
{
    /* Slide the manual down from the top of its slot one row at a time, the
       book sweeping onto the table. The title's text lives a row above the
       rule, so wiping from the rule up clears it too. */
    uint8_t attr = FG_BLACK | BG_WHITE;
    uint8_t row;

    scr_fill(0, 2, SCR_COLS, G_BLANK, ATTR(FG_BLACK, BG_BLUE));  /* title text */
    for (row = BOOK_ROW0; row <= BOOK_ROW1; row++) {
        scr_fill(BOOK_COL0, row, BOOK_W, G_BLANK, attr);
        view_wait(1);
    }
    drawn_page = PAGE_NONE;
}

void view_manual(uint8_t page)
{
    uint8_t attr = FG_BLACK | BG_WHITE;
    uint8_t row, i, n, full, hr;
    const Bomb *b;
    char nb[2];

    full = (uint8_t)(drawn_page != page);
    if (!full) return;

    manual_paint(attr);
    scr_fill(BOOK_COL0, BOOK_ROW0, BOOK_W, G_RULE, attr);
    put_udec((uint8_t)(BOOK_W - 2), BOOK_ROW0, page, 2, attr);     /* page # */

    if (page == 0) {
        uint8_t cover = FG_WHITE | BG_RED;
        scr_fill(BOOK_COL0, BOOK_ROW0, BOOK_W, G_BLANK, cover);
        scr_fill(BOOK_COL0, BOOK_ROW0, BOOK_W, G_RULE, cover);
        scr_puts((uint8_t)((BOOK_W - str_len(L->cover[0])) / 2),
                 (uint8_t)(BOOK_ROW0 + 1), L->cover[0], cover);
        scr_puts((uint8_t)((BOOK_W - str_len(L->cover[1])) / 2),
                 (uint8_t)(BOOK_ROW0 + 3), L->cover[1], cover);
        scr_puts((uint8_t)((BOOK_W - str_len(L->cover[2])) / 2),
                 (uint8_t)(BOOK_ROW0 + 5), L->cover[2], cover);
        scr_puts((uint8_t)((BOOK_W - str_len(L->pages)) / 2),
                 (uint8_t)(BOOK_ROW0 + 6), L->pages, cover);
        drawn_page = 0;
        drawn_wires = 0;
        return;
    }

    b = &bombs[page - 1];
    n = b->num_wires;
    nb[0] = (char)('0' + n);
    nb[1] = 0;

    /* Heading: "Bomb NN (N wires)". */
    hr = (uint8_t)(BOOK_ROW0 + 1);
    scr_puts(1, hr, "BOMB ", attr);
    put_udec(6, hr, page, 2, attr);
    scr_puts(8, hr, "(", attr);
    scr_puts(9, hr, nb, attr);
    scr_puts(10, hr, " WIRES)", attr);

    for (i = 0; i < n; i++) {
        uint8_t c = b->colour[i];
        row = ENTRY_ROW(i);
        put_field(ENTRY_COL_ORDER, row, L->order[b->order[i]], ORDER_WIDTH, attr);
        scr_fill(ENTRY_COL_SWATCH, row, 2, G_SOLID,
                 (uint8_t)(wire_fg(c) | BG_WHITE));
        put_field(ENTRY_COL_NAME, row, L->colour[c], ENTRY_NAME_W, attr);
    }
    for (; i < drawn_wires; i++)
        scr_fill(BOOK_COL0, ENTRY_ROW(i), BOOK_W, G_BLANK, attr);

    drawn_page = page;
    drawn_wires = n;
}

/* ------------------------------------------------------------------- */
/* Prompt and centred message.                                         */
/* ------------------------------------------------------------------- */

void view_prompt(const char *s)
{
    /* The bar is never blank: with nothing else to say it carries the keys,
       one line doing the work a title screen and a legend used to need. */
    uint8_t attr = FG_WHITE | BG_BLUE;
    scr_fill(0, ROW_PROMPT, SCR_COLS, G_BLANK, attr);
    view_message(ROW_PROMPT, s ? s : L->legend, attr);
}

void view_message(uint8_t row, const char *s, uint8_t attr)
{
    uint8_t len = str_len(s);
    scr_puts((uint8_t)((SCR_COLS - len) / 2), row, s, attr);
}

/* ------------------------------------------------------------------- */
/* Title, boom, defuse.                                                */
/* ------------------------------------------------------------------- */

/* The single countdown lamp that the title holds red and the bomb holds
   green on defuse -- one cell, lit by swapping its foreground colour. */
void view_logo_light(uint8_t on)
{
    scr_fill(LIGHT_COL, LIGHT_ROW, 1, G_LAMP,
             (uint8_t)((on ? FG_RED : FG_BLACK) | BG_BLACK));
}

#define MENU_ROW(i) (17 + 2 * (i))

void view_title_menu(uint8_t selected)
{
    uint8_t i = MENU_ITEMS, row, attr, len, col;
    const char *s;

    while (i-- > 0) {
        row = MENU_ROW(i);
        s = L->menu[i];
        len = str_len(s);
        col = (uint8_t)((SCR_COLS - len) / 2);
        scr_fill(BOOK_COL0, row, BOOK_W, G_BLANK, FG_WHITE | BG_BLUE);
        attr = FG_WHITE | BG_BLUE;
        if (i == selected) {
            attr = FG_YELLOW | BG_BLUE;
            scr_putc((uint8_t)(col - 1), row, G_RIGHT, attr);
        }
        scr_puts(col, row, s, attr);
    }
}

void view_title(void)
{
    /* The frame gives us the status bar, the bomb's case and the prompt bar;
       onto it the title paints a BOMBSQUAD heading, a sample bomb, the
       blinking lamp and the menu where the manual will rise. */
    uint8_t attr = FG_WHITE | BG_BLACK;

    view_frame();
    view_bomb(&bombs[22]);
    view_light(LIGHT_RED);

    scr_puts((uint8_t)((SCR_COLS - 9) / 2), TITLE_ROW, "BOMBSQUAD", attr);
    view_logo_light(0);
    view_title_menu(MENU_START);

    scr_fill(0, CREDIT_ROW, SCR_COLS, G_BLANK, attr);
    scr_puts((uint8_t)((SCR_COLS - 16) / 2), CREDIT_ROW,
             "NOAH BERNEY 2026", attr);

    view_prompt(L->title_keys);
}

void view_boom(uint16_t score)
{
    /* A white flash cooling through red to black under a descending tone,
       then BOOM! and the final score. The flash table runs white-hot to dark
       two entries at a time, so stepping back over it drops the repeats and
       keeps the ramp. */
    static const uint8_t fade[16] = {
        FG_BLACK  | BG_BLACK,  FG_BLACK  | BG_BLACK,
        FG_RED    | BG_BLACK,  FG_RED    | BG_BLACK,
        FG_YELLOW | BG_BLACK,  FG_YELLOW | BG_BLACK,
        FG_YELLOW | BG_YELLOW, FG_YELLOW | BG_YELLOW,
        FG_WHITE  | BG_BLACK,  FG_WHITE  | BG_BLACK,
        FG_WHITE  | BG_YELLOW, FG_WHITE  | BG_YELLOW,
        FG_WHITE  | BG_WHITE,  FG_WHITE  | BG_WHITE,
        FG_WHITE  | BG_WHITE,  FG_WHITE  | BG_WHITE
    };
    uint8_t i, row, hot;
    const char *label;
    uint8_t labellen, col;
    uint8_t attr = FG_WHITE | BG_BLACK;

    for (i = 16; i > 0; i = (uint8_t)(i - 2)) {
        hot = fade[i - 1];
        for (row = 0; row < SCR_ROWS; row++) scr_attr(0, row, SCR_COLS, hot);
        snd_tone((uint16_t)(BOOM_PITCH + i), 6);
        view_wait(1);
    }
    snd_off();
    scr_cls(ATTR(FG_BLACK, BG_BLACK));

    view_message(14, L->boom, ATTR(FG_RED, BG_BLACK));

    label = L->final;
    labellen = str_len(label);
    col = (uint8_t)((SCR_COLS - (labellen + 6)) / 2);
    scr_puts(col, 18, label, attr);
    put_udec((uint8_t)(col + labellen), 18, score, 6, attr);

    view_message(22, L->press, attr);
}

void view_defused(void)
{
    uint8_t i;
    for (i = 0; i < 3; i++) {
        view_wait(3);
        view_light(LIGHT_GREEN);
        snd_tone(BEEP_PITCH, 16);
        view_wait(2);
        view_light(LIGHT_OFF);
        snd_off();
    }
    view_light(LIGHT_GREEN);
    scr_puts(TIMER_COL, TIMER_ROW, L->safe, FG_RED | BG_BLACK);
}

void view_continue(void) { view_prompt(L->cont); }

void view_exit(void)
{
    snd_off();
    scr_cls(ATTR(FG_BLACK, BG_BLUE));
}
