#include <stdint.h>
#include "hw.h"
#include "udg.h"
#include "bombs.h"
#include "view.h"

/*
 * Screen and speaker only -- no game rules live here. See view.h for the
 * public API and the screen layout constants.
 */

/* ------------------------------------------------------------------- */
/* Language table.                                                     */
/* ------------------------------------------------------------------- */

/* Every string the player can read, one struct per language, so
   view_lang_toggle() swaps the entire UI with a single pointer flip
   instead of an if/else at every place text is drawn. */
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
    { "Red", "Red striped", "Blue", "Blue striped", "Green", "Green striped" },
    { "no", "1st", "2nd", "3rd", "4th", "5th" },
    "\x06\x07 page   \x08\x09 wire   SPACE cut",
    "\x08\x09 choose   SPACE select",
    "Press any key to continue",
    "BOOM!", "FINAL SCORE ", "PRESS SPACE",
    { "START", "DEUTSCH", "EXIT" }
};

/* German has no umlauts here on purpose: text is drawn with the 8x8 ZX
   system font, which has no ae/oe/ue as single glyphs, so the ae/oe/ue
   digraphs stand in for them instead. */
static const Lang lang_de = {
    "PUNKTE ", "GESCHAFFT ", "FREI", "BOMBEN-HANDBUCH",
    { "DAS", "BOMBEN", "HANDBUCH" }, "48 SEITEN",
    "NR.", "DRAHT", " Draehte",
    { "Rot", "Rot gestr.", "Blau", "Blau gestr.", "Gruen", "Gruen gestr." },
    { "nie", "1.", "2.", "3.", "4.", "5." },
    "\x06\x07 Seite  \x08\x09 Draht  LEER schneiden",
    "\x08\x09 Auswahl   LEER waehlen",
    "Weiter mit beliebiger Taste",
    "BUMM!", "PUNKTE GESAMT ", "LEERTASTE",
    { "START", "ENGLISH", "ENDE" }
};

/* The language in force. A DATA static, deliberately NOT reset by
   view_init(): a restart from the CAOS menu re-enters through the crt,
   which zeroes the BSS but leaves an initialised static as it was left,
   so a language chosen before a machine RESET is still in force when the
   game is restarted from the CAOS menu. */
static const Lang *L = &lang_en;

void view_lang_toggle(void)
{
    L = (L == &lang_en) ? &lang_de : &lang_en;
}

/* ------------------------------------------------------------------- */
/* Small local helpers.                                                */
/* ------------------------------------------------------------------- */

static uint8_t str_len(const char *s)
{
    uint8_t n = 0;
    while (s[n]) n++;
    return n;
}

/* Prints v as a fixed-width, zero-padded decimal field. width <= 6. */
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

/* Widths of the manual entry's text columns, set by their longest entries:
   "Green striped" for the name, "1st".."5th" (and "no") for the order. */
#define NAME_WIDTH   13
#define ORDER_WIDTH   3

/* The page currently drawn, or PAGE_NONE when something else has been drawn
   over the manual and the next view_manual() has to lay it out again. */
#define PAGE_NONE   0xFF
static uint8_t drawn_page = PAGE_NONE;

/* How many wire rows the drawn page filled, so a flip only has to blank the
   rows the new page does not reach - usually none, since most pages either
   side of a flip have the same number of wires. */
static uint8_t drawn_wires;

/* Writes s at (col,row) and blanks the rest of the field, so a shorter
   string covers whatever was there before without a separate clearing pass. */
static void put_field(uint8_t col, uint8_t row, const char *s, uint8_t width,
                      uint8_t attr)
{
    uint8_t len = str_len(s);
    scr_puts(col, row, s, attr);
    if (len < width) {
        scr_fill((uint8_t)(col + len), row, (uint8_t)(width - len), G_BLANK,
                 attr);
    }
}

/* Wire colour helpers. colour is 0..5: odd = striped, colour>>1 picks the
   base hue (0 red, 1 blue, 2 green). */
static uint8_t wire_bg(uint8_t colour)
{
    switch (colour >> 1) {
        case 0:  return BG_RED;
        case 1:  return BG_BLUE;
        default: return BG_GREEN;
    }
}

static uint8_t wire_fg(uint8_t colour)
{
    switch (colour >> 1) {
        case 0:  return FG_RED;
        case 1:  return FG_BLUE;
        default: return FG_GREEN;
    }
}

static uint8_t wire_glyph(uint8_t colour)
{
    return (colour & 1) ? G_STRIPE : G_BLANK;
}

static uint8_t wire_attr(uint8_t colour)
{
    return (uint8_t)(((colour & 1) ? FG_WHITE : FG_BLACK) | wire_bg(colour));
}

/* Fills the whole book -- cover or page -- with one background colour, so
   the cover (red) and a manual page (white) can share the same wipe before
   their own text goes on top. */
static void book_paint(uint8_t attr)
{
    uint8_t row;
    for (row = BOOK_ROW0; row <= BOOK_ROW1; row++) {
        scr_fill(BOOK_COL0, row, BOOK_W, G_BLANK, attr);
    }
}

/* Centres s within the book's own width, not the whole screen, so the
   cover and page headings sit over the book itself rather than over the
   bomb beside it. */
static void book_centre(uint8_t row, const char *s, uint8_t attr)
{
    uint8_t len = str_len(s);
    uint8_t col = (uint8_t)(BOOK_COL0 + (BOOK_W - len) / 2);
    scr_puts(col, row, s, attr);
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
    /* A restart from the CAOS menu re-enters through the crt, which zeroes
       the BSS but leaves statics that have an initialiser holding whatever
       the last game left in them, so put those back by hand. */
    drawn_page = PAGE_NONE;
    drawn_wires = 0;

    scr_cls(FG_BLACK | BG_BLACK);
}

/* The block of pages behind the cover, down the book's right-hand side.
   It is the one thing that makes the book read as a book rather than as a
   coloured panel, and G_PAGE_V rules it with lines rather than dithering it
   so the leaves are legible as leaves. The strip is offset one column right
   of the cover and mitred at its top, where the block tapers away.
   There is no matching foot: the book hangs off the bottom of the screen,
   so the strip simply runs to the last row it has and is cut off there. */
static void book_edges(void)
{
    uint8_t right = (uint8_t)(BOOK_COL0 + BOOK_W);
    uint8_t attr  = FG_BLACK | BG_WHITE;
    uint8_t r;

    scr_fill(right, BOOK_ROW0, 1, G_PAGE_TR, attr);
    for (r = (uint8_t)(BOOK_ROW0 + 1); r <= BOOK_ROW1; r++) {
        scr_fill(right, r, 1, G_PAGE_V, attr);
    }
}

/* A rectangle outline, corners and all. The bomb is flat black now rather
   than a lit moulding, so its edges have nothing to catch the light and
   have to be stated as a line instead; this draws one such border, and the
   corner glyphs carry the stubs that make the four sides meet. c0,r0 and
   c1,r1 are the outline's own cells, inclusive. */
static void outline(uint8_t c0, uint8_t r0, uint8_t c1, uint8_t r1,
                     uint8_t attr)
{
    uint8_t span = (uint8_t)(c1 - c0 - 1);
    uint8_t r;

    scr_fill(c0, r0, 1, G_CORNER_TL, attr);
    scr_fill((uint8_t)(c0 + 1), r0, span, G_RULE, attr);
    scr_fill(c1, r0, 1, G_CORNER_TR, attr);
    for (r = (uint8_t)(r0 + 1); r < r1; r++) {
        scr_fill(c0, r, 1, G_LINE_V, attr);
        scr_fill(c1, r, 1, G_LINE_V, attr);
    }
    scr_fill(c0, r1, 1, G_CORNER_BL, attr);
    scr_fill((uint8_t)(c0 + 1), r1, span, G_RULE, attr);
    scr_fill(c1, r1, 1, G_CORNER_BR, attr);
}

/* The bundle the box is strapped to: four sticks of dynamite standing on
   end side by side, each three columns wide, running the full height of
   the play area so they show above and below the box. Every cell down a
   stick uses the same three glyphs -- a stick of dynamite is a paper tube
   with flat ends, so no end caps are needed. The three columns carry
   different attributes on purpose: see udg.c for why the tube's shading
   has to come from the attributes rather than from one dither. Where two
   sticks meet, one tube's shadow side lands against the next one's dark
   rim, which is what separates them. */
static void bomb_dynamite(void)
{
    uint8_t r, c;

    for (r = 3; r <= 28; r++) {
        for (c = 26; c < 38; c = (uint8_t)(c + 3)) {
            scr_fill(c, r, 1, G_TNT_L, FG_ORANGE | BG_RED);
            scr_fill((uint8_t)(c + 1), r, 1, G_TNT_M, FG_BLACK | BG_RED);
            scr_fill((uint8_t)(c + 2), r, 1, G_TNT_R, FG_BLACK | BG_RED);
        }
    }

    /* The fuse, slung from the centre of one stick's foot to the centre of
       the next -- three sags across the four sticks, so it runs from the
       first centre (col 27) to the last (col 36) and no cord hangs off
       either end of the bundle. Each sag but the first opens on a junction
       cell, where the sag before it lands in the same centre. The sticks
       are cut two rows short of where they used to end to leave it room. */
    for (c = 27; c < 36; c = (uint8_t)(c + 3)) {
        scr_fill(c, 29, 1, c == 27 ? G_FUSE_A : G_FUSE_J, FG_WHITE | BG_BLACK);
        scr_fill((uint8_t)(c + 1), 29, 1, G_FUSE_B, FG_WHITE | BG_BLACK);
        scr_fill((uint8_t)(c + 2), 29, 1, G_FUSE_C, FG_WHITE | BG_BLACK);
    }
    scr_fill(36, 29, 1, G_FUSE_D, FG_WHITE | BG_BLACK);
}

/* The box strapped across the bundle: a black rectangle painted straight
   over the dynamite, with its own border, the wire compartment's and the
   countdown readout's drawn as plain outlines inside it. It is wider than
   the bundle, so the sticks show through only above and below it.
   The compartment is drawn to its minimum height -- its walls sit directly
   on rows 11 and 23, so the well is exactly the eleven rows the wires
   themselves occupy. What that buys is the room above it for the panel:
   the lamp and the readout down the left, the keypad down the right.
   The lamp and the digits paint themselves, so their cells are left clear
   here. */
static void bomb_box(void)
{
    uint8_t attr = FG_WHITE | BG_BLACK;
    uint8_t r, c;

    for (r = 6; r <= 25; r++) scr_fill(24, r, 16, G_BLANK, FG_BLACK | BG_BLACK);
    outline(24, 6, 39, 25, attr);

    /* A dead keypad, four across and three down, hard against the
       compartment's right-hand wall and squared up with the readout's panel
       opposite it. The keys do nothing -- the bomb is defused at the wires
       -- and are here to make the box look like something that was built.
       G_BUTTON is inset a pixel all round, so even packed shoulder to
       shoulder each one reads as its own key. */
    for (r = 8; r <= 10; r++) {
        for (c = 33; c <= 36; c++) scr_fill(c, r, 1, G_BUTTON, attr);
    }

    outline(26, 8, 31, 10, attr);      /* the readout's panel */
    outline(26, 11, 37, 23, attr);     /* the wire compartment */
}

void view_frame(void)
{
    /* Everything below is painted from scratch on a black screen, which is
       simpler than blanking the gaps -- row 1, the column between the book
       and the bomb, row 29 -- one at a time; view_frame() is only ever
       called from view_title(). */
    scr_cls(FG_BLACK | BG_BLACK);

    scr_fill(0, ROW_STATUS, 40, G_BLANK, FG_WHITE | BG_BLUE);

    /* The bomb, right: the bundle first, then the box over the top of it. */
    bomb_dynamite();
    bomb_box();

    /* One blue bar along the bottom, in the same blue as the status bar:
       both belong to the game rather than to anything on the table, and the
       colour is what says so. It also cuts the book and the dynamite off
       cleanly at the bottom of the screen. */
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

    scr_fill(WIRE_COL0, row, WIRE_COL_LEN, wire_glyph(colour), wire_attr(colour));

    if (severed) {
        uint8_t fg = wire_fg(colour);
        scr_fill(CUT_COL_L, row, 1, G_CUT_L, (uint8_t)(fg | BG_BLACK));
        scr_fill(SCISSORS_COL, row, 1, G_BLANK, FG_BLACK | BG_BLACK);
        scr_fill(CUT_COL_R, row, 1, G_CUT_R, (uint8_t)(fg | BG_BLACK));
    }
}

void view_bomb(const Bomb *b)
{
    uint8_t i;

    for (i = 0; i < b->num_wires; i++) view_wire(b, i, 0);
    for (; i < MAX_WIRES; i++) {
        scr_fill(WIRE_COL0, WIRE_ROW(i), WIRE_COL_LEN, G_BLANK, FG_BLACK | BG_BLACK);
    }
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
    scr_glyph(SCISSORS_COL, row, &udg_font[G_SCISSORS * 8], (uint8_t)(FG_WHITE | bg));
}

void view_cut_anim(const Bomb *b, uint8_t cut_mask, uint8_t idx)
{
    uint8_t row = WIRE_ROW(idx);
    uint8_t severed = (uint8_t)((cut_mask >> idx) & 1);
    uint8_t bg = severed ? BG_BLACK : wire_bg(b->colour[idx]);
    uint8_t attr = (uint8_t)(FG_WHITE | bg);

    scr_glyph(SCISSORS_COL, row, &udg_font[G_SCISSORS_SHUT * 8], attr);
    snd_tone(CUT_PITCH, 20);
    view_wait(1);
    snd_off();
    view_wait(3);
    scr_glyph(SCISSORS_COL, row, &udg_font[G_SCISSORS * 8], attr);
}

void view_light(uint8_t state)
{
    /* Two lamps, one state: whichever the state names is lit and the other
       is painted black, which is what the box behind it already is. That
       keeps the pair consistent however it is called -- a tick lights the
       red one and clears the green, and defusing does the reverse. */
    uint8_t red   = (uint8_t)(state == LIGHT_RED   ? FG_RED   : FG_BLACK);
    uint8_t green = (uint8_t)(state == LIGHT_GREEN ? FG_GREEN : FG_BLACK);

    scr_fill(LIGHT_COL, LIGHT_ROW, 1, G_LAMP, (uint8_t)(red | BG_BLACK));
    scr_fill(LIGHT_COL, SAFE_ROW,  1, G_LAMP, (uint8_t)(green | BG_BLACK));
}

/* ------------------------------------------------------------------- */
/* Status bar.                                                         */
/* ------------------------------------------------------------------- */

void view_status(uint16_t score, uint8_t defused)
{
    /* The score sits flush left, the defused count flush right against
       column 40. The label moves left with its own length so a longer
       word still leaves the 2-digit field the room it needs, but that
       field's own column is a constant 38: label col + label len is
       40 - 2 whatever the label's length, since that is exactly what
       "right-aligned against 40" means. */
    uint8_t attr = FG_WHITE | BG_BLUE;

    scr_puts(0, ROW_STATUS, L->score, attr);
    put_udec(str_len(L->score), ROW_STATUS, score, 6, attr);

    scr_puts((uint8_t)(38 - str_len(L->defused)), ROW_STATUS, L->defused, attr);
    put_udec(38, ROW_STATUS, defused, 2, attr);
}

void view_timer(uint8_t ticks)
{
    /* The field is four wide so SAFE fits, so the two digits are centred in
       it and the whole field is wiped first -- otherwise the S and the E of
       a previous SAFE would still be sitting either side of them. */
    uint8_t attr = FG_RED | BG_BLACK;

    scr_fill(TIMER_COL, TIMER_ROW, TIMER_W, G_BLANK, attr);
    put_udec((uint8_t)(TIMER_COL + 1), TIMER_ROW, ticks, 2, attr);
}

/* ------------------------------------------------------------------- */
/* Manual.                                                             */
/* ------------------------------------------------------------------- */

void view_book_rise(void)
{
    /* A row at a time from the foot of the screen upwards, which both puts
       the book on the table and clears the title's logo off it -- the two
       occupy the same columns, so the rising panel is its own wipe. There
       is no timed delay: the screen routines take long enough over
       twenty-seven rows to read as a sweep on their own. The page block
       goes up with it rather than after it, so the book has a thickness the
       whole way rather than gaining one at the end. */
    uint8_t attr = FG_WHITE | BG_RED;
    uint8_t right = (uint8_t)(BOOK_COL0 + BOOK_W);
    uint8_t r;

    for (r = BOOK_ROW1; r >= BOOK_ROW0; r--) {
        scr_fill(BOOK_COL0, r, BOOK_W, G_BLANK, attr);
        scr_fill(right, r, 1, r == BOOK_ROW0 ? G_PAGE_TR : G_PAGE_V,
                 FG_BLACK | BG_WHITE);
    }
    drawn_page = PAGE_NONE;
}

void view_manual(uint8_t page)
{
    uint8_t attr = FG_BLACK | BG_WHITE;
    uint8_t row, i, n, full;
    const Bomb *b;
    char nb[8];

    /* Paging is the thing the player does most, so redraw as little as
       possible: only the page number and the wire list actually change.
       Everything else -- the heading, the "/48", the rule -- is repainted
       only when the page has been overwritten by something else, or when
       the cover is involved, since that has its own layout. */
    full = (uint8_t)(drawn_page == PAGE_NONE || drawn_page == 0 || page == 0);

    if (full) {
        if (page == 0) {
            uint8_t cover = FG_WHITE | BG_RED;

            book_paint(cover);
            book_edges();
            scr_fill(2, 12, 18, G_RULE, cover);
            book_centre(15, L->cover[0], cover);
            book_centre(17, L->cover[1], cover);
            book_centre(19, L->cover[2], cover);
            scr_fill(2, 22, 18, G_RULE, cover);
            book_centre(25, L->pages, cover);
            drawn_page = 0;
            return;
        }

        book_paint(attr);
        book_edges();
        scr_puts(18, 5, "/48", attr);
        scr_puts(1, 7, L->manual, attr);
        scr_fill(1, 8, 20, G_RULE, attr);
        scr_puts(1, 12, L->col_order, attr);
        scr_puts(8, 12, L->col_wire, attr);
        scr_fill(1, 13, 20, G_RULE, attr);
    }

    /* The folio sits alone in the page's top right corner, above the
       heading, the way a book's own page number does; the word "PAGE" would
       only be telling the reader what a number in that corner already is. */
    put_udec(16, 5, page, 2, attr);

    b = &bombs[page - 1];
    n = b->num_wires;
    nb[0] = (char)('0' + n);
    nb[1] = 0;
    scr_puts(1, 10, nb, attr);
    if (full) scr_puts(2, 10, L->wires, attr);

    /* Fixed width fields, so a shorter entry covers the longer one the
       previous page left in its place and no clearing pass is needed. */
    for (i = 0; i < n; i++) {
        uint8_t c = b->colour[i];

        row = ENTRY_ROW(i);
        put_field(1, row, L->order[b->order[i]], ORDER_WIDTH, attr);
        scr_fill(5, row, 2, wire_glyph(c), wire_attr(c));
        put_field(8, row, L->colour[c], NAME_WIDTH, attr);
    }
    /* A full draw cleared the page, so only a partial one has leftovers. */
    if (!full) {
        for (; i < drawn_wires; i++) {
            scr_fill(1, ENTRY_ROW(i), 20, G_BLANK, attr);
        }
    }

    drawn_page = page;
    drawn_wires = n;
}

void view_prompt(const char *s)
{
    /* The bar is never blank: with nothing else to say it carries the keys,
       so the one line does the work the title screen and the legend used to
       need a row each for. */
    uint8_t attr = FG_WHITE | BG_BLUE;

    scr_fill(0, ROW_PROMPT, 40, G_BLANK, attr);
    view_message(ROW_PROMPT, s ? s : L->legend, attr);
}

void view_message(uint8_t row, const char *s, uint8_t attr)
{
    uint8_t len = str_len(s);
    uint8_t col = (uint8_t)((40 - len) / 2);
    scr_puts(col, row, s, attr);
}

/* ------------------------------------------------------------------- */
/* Title, boom, defuse.                                                */
/* ------------------------------------------------------------------- */

/* The lettering is drawn as outlines, so what this table holds is not the
   letters but their boundary: every cell some edge of them passes through.
   The shapes were traced from four-by-four cell silhouettes -- the B a
   wedge with its top narrower than its foot, the M and the A cut from
   diagonals, the T and the E stepped, and the O a true circle rather than
   the octagon a grid of cells can manage -- and then deduplicated, which is
   why so few cells cover the whole logo. They live here rather than in
   udg_font[] because scr_glyph() takes a bitmap directly, and the font's
   codes stop at 32. */
static const uint8_t logo_cells[] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
    0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xE0, 0x18, 0x04, 0x02, 0x02, 0x01, 0x01, 0x01,
    0x00, 0x00, 0x00, 0x01, 0x02, 0x04, 0x08, 0x10,
    0x00, 0x1F, 0xE0, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0xF8, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x80, 0x40, 0x20, 0x10, 0x08,
    0x80, 0xC0, 0xA0, 0x90, 0x88, 0x84, 0x82, 0x81,
    0x01, 0x03, 0x05, 0x09, 0x11, 0x21, 0x41, 0x81,
    0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
    0x20, 0x20, 0x20, 0x40, 0x40, 0x40, 0x40, 0x40,
    0x04, 0x04, 0x04, 0x02, 0x02, 0x02, 0x02, 0x02,
    0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01,
    0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80,
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x40, 0x40, 0x40, 0x40, 0x40, 0x20, 0x20, 0x20,
    0x02, 0x02, 0x02, 0x02, 0x02, 0x04, 0x04, 0x04,
    0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0xFF,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF,
    0x01, 0x01, 0x01, 0x02, 0x02, 0x04, 0x18, 0xE0,
    0x10, 0x08, 0x04, 0x02, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0xE0, 0x1F, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x07, 0xF8, 0x00,
    0x08, 0x10, 0x20, 0x40, 0x80, 0x00, 0x00, 0x00,
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0xFF,
    0xFF, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF,
    0xFF, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0xFF,
};

/* The logo a cell at a time: four letters four cells wide with a column
   between them, indexing logo_cells[]. Cell 0 is blank and is skipped --
   the screen behind it is already black. */
static const uint8_t logo_rows[8][19] = {
    {  1,  2,  3,  0,  0,  4,  5,  6,  7,  0,  8,  0,  0,  9,  0,  1,  2,  3,  0 },
    { 10,  0,  0,  3,  0, 11,  0,  0, 12,  0, 10, 13, 14, 15,  0, 10,  0,  0,  3 },
    { 10,  0,  0, 15,  0, 16,  0,  0, 17,  0, 10,  0,  0, 15,  0, 10,  0,  0, 15 },
    { 18, 19, 19, 20,  0, 21, 22, 23, 24,  0, 18, 19, 19, 25,  0, 18, 19, 19, 20 },
    {  1,  2,  2, 26,  0,  1, 26,  0,  0,  0,  0, 14, 13,  0,  0,  8,  0,  0,  9 },
    { 18,  0,  0, 25,  0, 10,  0, 26,  0,  0, 14,  0,  0, 13,  0, 10, 13, 14, 15 },
    {  0, 10, 15,  0,  0, 10,  0, 0,  26,  0, 10,  0,  0, 15,  0, 10,  0,  0, 15 },
    {  0, 18, 25,  0,  0, 18, 19, 19, 25,  0, 18, 19, 19, 25,  0, 18, 19, 19, 25 },
};

/* The lamp inside the O: a disc sixteen pixels across, small enough to sit
   clear of the ring around it. */
static const uint8_t logo_lamp[] = {
    0x00, 0x0F, 0x1F, 0x3F, 0x7F, 0x7F, 0x7F, 0x7F,
    0x00, 0xF0, 0xF8, 0xFC, 0xFE, 0xFE, 0xFE, 0xFE,
    0x7F, 0x7F, 0x7F, 0x7F, 0x3F, 0x1F, 0x0F, 0x00,
    0xFE, 0xFE, 0xFE, 0xFE, 0xFC, 0xF8, 0xF0, 0x00,
};

/* Where the logo sits: BOMB on LOGO_ROW, TEAM two rows under it, and the
   lamp two cells square in the middle of the second letter. */
#define LOGO_ROW    9
#define LOGO_COL    1
#define LAMP_COL    7
#define LAMP_ROW    (LOGO_ROW + 1)

static void logo_draw(void)
{
    uint8_t attr = FG_WHITE | BG_BLACK;
    uint8_t r, c, i;

    for (r = 0; r < 8; r++) {
        /* The two words come out of one table, two rows apart. */
        uint8_t y = (uint8_t)(LOGO_ROW + r + (r >= 4 ? 2 : 0));
        for (c = 0; c < 19; c++) {
            i = logo_rows[r][c];
            if (i) scr_glyph(LOGO_COL + c, y, &logo_cells[i * 8], attr);
        }
    }
}

void view_logo_light(uint8_t on)
{
    /* Off paints the same disc in black, which is what the inside of the O
       already is. */
    uint8_t attr = (uint8_t)((on ? FG_RED : FG_BLACK) | BG_BLACK);
    uint8_t c1 = (uint8_t)(LAMP_COL + 1);
    uint8_t r1 = (uint8_t)(LAMP_ROW + 1);

    scr_glyph(LAMP_COL, LAMP_ROW, &logo_lamp[0], attr);
    scr_glyph(c1, LAMP_ROW, &logo_lamp[8], attr);
    scr_glyph(LAMP_COL, r1, &logo_lamp[16], attr);
    scr_glyph(c1, r1, &logo_lamp[24], attr);
}

/* Row for menu item i: under the logo, inside the book's columns
   (BOOK_COL0..BOOK_W), which are black on the title screen and are wiped
   by view_book_rise() the moment a game starts. The credit line sits at
   the foot of the screen instead, so the menu has the whole middle of
   the left-hand side to itself. */
#define MENU_ROW(i) (21 + 2 * (i))
#define CREDIT_ROW  29

void view_title_menu(uint8_t selected)
{
    /* Counted down rather than up: each row stands on its own (blanked
       and redrawn independently), so the direction cannot be seen on
       screen, and the compare-against-zero this way round is cheaper. */
    uint8_t i = MENU_ITEMS, row, attr;
    const char *s;

    while (i-- > 0) {
        row = MENU_ROW(i);
        s = L->menu[i];

        /* Blanked first, so a shorter item -- ENGLISH for DEUTSCH, say --
           covers a longer one left there by the other language. */
        scr_fill(BOOK_COL0, row, BOOK_W, G_BLANK, FG_WHITE | BG_BLACK);

        attr = FG_WHITE | BG_BLACK;
        if (i == selected) {
            attr = FG_YELLOW | BG_BLACK;
            scr_glyph((uint8_t)(BOOK_COL0 + 5, row, &udg_font[G_RIGHT * 8], attr);
        }
        book_centre(row, s, attr);
    }
}

void view_title(void)
{
    /* The left-hand side carries the logo instead of the book: view_frame()
       leaves it black and does not draw the page block, which view_manual()
       puts up when the first page is turned to. */
    uint8_t attr = FG_WHITE | BG_BLACK;

    view_frame();
    view_bomb(&bombs[22]);
    view_light(LIGHT_RED);

    logo_draw();
    view_logo_light(0);

    /* Name and year on one line immediately above the instruction bar,
       where a book's colophon goes: the middle of the page belongs to the
       menu. */
    book_centre(CREDIT_ROW, "Noah Burney 2026", attr);
    view_title_menu(MENU_START);
    drawn_page = PAGE_NONE;

    view_prompt(L->title_keys);
}

void view_boom(uint16_t score)
{
    /* Indexed by the step counter, so the blast runs from the end of the
       table back to its start: a white flash cooling through yellow and red
       into the dark. */
    static const uint8_t fade[16] = {
        FG_BLACK  | BG_BLACK,  FG_BLACK  | BG_BLACK,
        FG_BLACK  | BG_RED,    FG_BLACK  | BG_RED,
        FG_RED    | BG_BLACK,  FG_RED    | BG_BLACK,
        FG_RED    | BG_RED,    FG_ORANGE | BG_RED,
        FG_ORANGE | BG_RED,    FG_YELLOW | BG_RED,
        FG_YELLOW | BG_RED,    FG_YELLOW | BG_YELLOW,
        FG_WHITE  | BG_YELLOW, FG_WHITE  | BG_YELLOW,
        FG_WHITE  | BG_WHITE,  FG_WHITE  | BG_WHITE
    };
    uint8_t i, row, hot, attr;
    const char *label;
    uint8_t labellen, col;

    /* Two at a time. The table holds every colour twice, so stepping over
       the odd entries drops the repeats rather than the sweep: the blast
       runs through the same white-hot-to-dark ramp in half the time. */
    for (i = 16; i > 0; i = (uint8_t)(i - 2)) {
        hot = fade[i - 1];
        for (row = 0; row < 32; row++) scr_attr(0, row, 40, hot);
        snd_tone((uint16_t)(BOOM_PITCH - i), (uint8_t)(i + 15));
        view_wait(2);
    }
    snd_off();
    scr_cls(FG_BLACK | BG_BLACK);

    attr = FG_WHITE | BG_BLACK;
    view_message(14, L->boom, (uint8_t)(FG_RED | BG_BLACK));

    label = L->final;
    labellen = str_len(label);
    col = (uint8_t)((40 - (labellen + 6)) / 2);
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

    /* Left lit, with the readout saying so: the bomb's own panel reports
       the result, so nothing has to be drawn over the rest of the screen
       and nothing has to be repainted afterwards. */
    view_light(LIGHT_GREEN);
    scr_puts(TIMER_COL, TIMER_ROW, L->safe, FG_RED | BG_BLACK);
}

void view_continue(void)
{
    view_prompt(L->cont);
}

void view_exit(void)
{
    /* CAOS writes its own prompt over whatever is already on screen when
       control returns to it, so a clean CAOS-coloured screen is what stops
       the game's graphics showing through underneath that prompt. */
    snd_off();
    scr_cls(FG_WHITE | BG_BLUE);
}
