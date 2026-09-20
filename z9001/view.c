#include <stdint.h>
#include "hw.h"
#include "bombs.h"
#include "view.h"
#include "version.h"

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
    const char *bomb;
    const char *colour[6];
    const char *order[6];
    const char *legend;
    const char *title_keys;
    const char *cont;
    const char *boom;
    const char *final;
    const char *press;
    const char *name_label;
    const char *name_keys;
    const char *hiscores;
    const char *menu[MENU_ITEMS];
} Lang;

static const Lang lang_en = {
    "SCORE ", "DEFUSED ", "SAFE", "BOMB DEFUSAL MANUAL",
    { "BOMB", "DEFUSAL", "MANUAL" }, "48 PAGES",
    "CUT", "WIRE", " WIRES", "BOMB ",
    { "RED", "RED STRIPED", "BLUE", "BLUE STRIPED",
      "GREEN", "GREEN STRIPED" },
    { "no", "1st", "2nd", "3rd", "4th", "5th" },
    "A/D PAGE  W/S WIRE  SPACE CUT",
    "A/D CHOOSE  SPACE START",
    "PRESS ANY KEY",
    "BOOM!", "FINAL SCORE ", "PRESS SPACE",
    "ENTER YOUR NAME", "TYPE NAME  DEL ERASE  ENTER OK", "HIGH SCORES",
    { "START", "DEUTSCH", "EXIT" }
};

static const Lang lang_de = {
    "PUNKTE ", "GESCHAFFT ", "FREI", "BOMBEN-HANDBUCH",
    { "DAS", "BOMBEN", "HANDBOOK" }, "48 SEITEN",
    "SCHNEIDE", "DRAHT", " DRAEHTE", "BOMBE ",
    { "ROT", "ROT GESTREIFT", "BLAU", "BLAU GESTREIFT",
      "GRUEN", "GRUEN GESTREIFT" },
    { "nie", "1.", "2.", "3.", "4.", "5." },
    "A/D SEITE  W/S DRAHT  LEER SCHNEIDEN",
    "A/D AUSWAHL  LEER STARTEN",
    "WEITER MIT TASTE",
    "BUMM!", "PUNKTE GESAMT ", "LEERTASTE",
    "DEIN NAME", "NAME TIPPEN  DEL LOESCHEN  ENTER OK", "BESTENLISTE",
    { "START", "ENGLISH", "ENDE" }
};

/* A DATA static, deliberately NOT reset by view_init(): a restart from the
   monitor (MENU_EXIT -> jp start) re-enters here through the crt, which
   zeroes the BSS but leaves an initialised static as it was left, so a
   language chosen before a RESET is still in force on the next run. The
   game starts in German. */
static const Lang *L = &lang_de;

void view_lang_toggle(void) { L = (L == &lang_en) ? &lang_de : &lang_en; }

/* ------------------------------------------------------------------- */
/* Glyphs and small helpers.                                           */
/* ------------------------------------------------------------------- */

/* Glyphs are read straight out of chargen.851 in the probe. Fills use the
   full-block glyph; the title logo and the bomb's box are drawn with the
   chargen's line-drawing pieces (the G_EDGE_* group below). */
#define G_BLANK     ' '
#define G_SOLID     0xFF        /* full block */
#define G_STRIPE    '/'         /* diagonal hatch for striped wires */
#define G_LINE_V    '|'
#define G_MARK_L    '<'         /* menu markers, either side of the chosen item */
#define G_MARK_R    '>'
#define G_HALF_L    0xB4        /* left half block */
#define G_HALF_R    0xB5        /* right half block */
#define G_WEDGE     0x8F        /* solid triangle, filled below-left */
#define G_CUT_L     G_HALF_L    /* severed wire's left end */
#define G_CUT_R     G_HALF_R    /* and its right end */
#define G_LAMP      0xFF        /* a filled disc the size of the lamp */
/* The outline pieces. Each is a thin line that runs along the edge of its
   cell, so the pieces join up with their neighbours. */
#define G_EDGE_T    0x9E        /* top edge */
#define G_EDGE_B    0xF8        /* bottom edge */
#define G_EDGE_L    0x9F        /* left edge */
#define G_EDGE_R    0xC0        /* right edge */
#define G_CORN_TL   0xC1        /* top and left edges */
#define G_CORN_TR   0x89        /* top and right edges */
#define G_CORN_BL   0x88        /* bottom and left edges */
#define G_CORN_BR   0xC8        /* bottom and right edges */
#define G_ARC_TL    0xAE        /* rounded corner, top-left */
#define G_ARC_TR    0xAD        /* rounded corner, top-right */
#define G_ARC_BL    0xAB        /* rounded corner, bottom-left */
#define G_ARC_BR    0xAC        /* rounded corner, bottom-right */
#define G_DIAG_R    0x90        /* diagonal, '/' */
#define G_DIAG_L    0x91        /* diagonal, '\' */
#define BG_FIELD    BG_BLACK                /* the screen behind everything */
#define ATTR_STATUS (FG_WHITE | BG_BLACK)   /* the top bar */
#define ATTR_PROMPT (FG_WHITE | BG_BLUE)    /* the bottom bar */
#define S_SCISSORS  ">B"        /* open, SCISSORS_W cells wide */
#define S_SCISSORS_SHUT "=B"    /* blades closed */
/* The chargen's box-drawing pieces, whose lines run through the middle of
   the cell and so join up with their neighbours; used for the keypad table. */
#define G_BOX_H     0xA0        /* horizontal line */
#define G_BOX_V     0xA1        /* vertical line */
#define G_BOX_TL    0xA8        /* top-left corner */
#define G_BOX_TR    0xA9        /* top-right corner */
#define G_BOX_BL    0xA7        /* bottom-left corner */
#define G_BOX_BR    0xAA        /* bottom-right corner */
#define G_BOX_TEE_D 0xA4        /* tee: left, right and down */
#define G_BOX_TEE_U 0xA2        /* tee: left, right and up */
#define G_BOX_TEE_R 0xA3        /* tee: up, down and right */
#define G_BOX_TEE_L 0xA5        /* tee: up, down and left */
#define G_BOX_CROSS 0xA6        /* all four ways */
#define G_BOX_ARC_DL 0x87       /* rounded corner: left and down */
#define G_BOX_ARC_UL 0x84       /* rounded corner: up and left */

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
   over the manual and the next view_manual() has to lay it out again, the
   edge of the pages included. */
#define PAGE_NONE 0xFF
static uint8_t drawn_page = PAGE_NONE;
static uint8_t drawn_wires;

/* Fills the page area of the manual panel -- all of it but the edge -- with
   one background, the common ground for a page and its entries. */
static void manual_paint(uint8_t attr)
{
    uint8_t row;
    for (row = BOOK_ROW0; row <= BOOK_ROW1; row++)
        scr_fill(BOOK_COL0, row, (uint8_t)(BOOK_W - BOOK_EDGE_W), G_BLANK,
                 attr);
}

/* A page's black borders, top and right, drawn with the thin outline pieces
   that run along the very edge of their cells, the same ones that outline the
   bomb: a line along the top row, turning down in a corner at the last column
   before the edge of the pages and running to the bottom of the panel. The
   cover has none. */
static void manual_border(void)
{
    uint8_t attr = FG_BLACK | BG_WHITE;
    uint8_t right = (uint8_t)(BOOK_W - BOOK_EDGE_W - 1);
    uint8_t row;

    scr_fill(BOOK_COL0, BOOK_ROW0, right, G_EDGE_T, attr);
    scr_putc(right, BOOK_ROW0, G_CORN_TR, attr);
    for (row = (uint8_t)(BOOK_ROW0 + 1); row <= BOOK_ROW1; row++)
        scr_putc(right, row, G_EDGE_R, attr);
}

/* The edge of the pages beneath the one showing: the last BOOK_EDGE_W columns
   of the panel, a black "|" on white in every row. The top is cut away
   diagonally to give the stack of pages some depth: each column starts with a
   solid white wedge, and each column starts one row lower than the one to
   its left, with the field showing above it. */
static void manual_edge(void)
{
    uint8_t col = (uint8_t)(BOOK_W - BOOK_EDGE_W);
    uint8_t i, row;

    for (row = BOOK_ROW0; row <= BOOK_ROW1; row++)
        scr_fill(col, row, BOOK_EDGE_W, G_LINE_V, FG_BLACK | BG_WHITE);

    for (i = 0; i < BOOK_EDGE_W; i++) {
        for (row = 0; row < i; row++)
            scr_putc((uint8_t)(col + i), (uint8_t)(BOOK_ROW0 + row), G_BLANK,
                     FG_WHITE | BG_FIELD);
        scr_putc((uint8_t)(col + i), (uint8_t)(BOOK_ROW0 + i), G_WEDGE,
                 FG_WHITE | BG_FIELD);
    }
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
    scr_cls(ATTR(FG_BLACK, BG_FIELD));
}

static void outline(uint8_t c0, uint8_t r0, uint8_t c1, uint8_t r1, uint8_t attr)
{
    uint8_t span = (uint8_t)(c1 - c0 - 1);
    uint8_t r;
    scr_fill(c0, r0, 1, G_CORN_TL, attr);
    scr_fill((uint8_t)(c0 + 1), r0, span, G_EDGE_T, attr);
    scr_fill(c1, r0, 1, G_CORN_TR, attr);
    for (r = (uint8_t)(r0 + 1); r < r1; r++) {
        scr_fill(c0, r, 1, G_EDGE_L, attr);
        scr_fill(c1, r, 1, G_EDGE_R, attr);
    }
    scr_fill(c0, r1, 1, G_CORN_BL, attr);
    scr_fill((uint8_t)(c0 + 1), r1, span, G_EDGE_B, attr);
    scr_fill(c1, r1, 1, G_CORN_BR, attr);
}

/* The keypad table, one string per row and KEYPAD_W by KEYPAD_H cells, drawn
   with the box-drawing pieces above. The buttons are the blank cells between
   the lines: three columns by four rows, square because a one-cell button
   with a line each side is as tall as it is wide. The bottom-right two are
   one tall ENTER button, the line between them left out.

       F Z L J     corners: top-left, top-right, bottom-left, bottom-right
       - |         horizontal and vertical lines
       T ^ > <     tees: down, up, right, left
       +           crossing

   and anything else is blank. */
#define KEYPAD_W    7
#define KEYPAD_H    9

static const char *const keypad[KEYPAD_H] = {
    "F-T-T-Z",
    "| | | |",
    ">-+-+-<",
    "| | | |",
    ">-+-+-<",
    "| | | |",
    ">-+-^-<",
    "| |   |",
    "L-^---J"
};

static uint8_t keypad_glyph(char ch)
{
    switch (ch) {
        case 'F': return G_BOX_TL;
        case 'Z': return G_BOX_TR;
        case 'L': return G_BOX_BL;
        case 'J': return G_BOX_BR;
        case '-': return G_BOX_H;
        case '|': return G_BOX_V;
        case 'T': return G_BOX_TEE_D;
        case '^': return G_BOX_TEE_U;
        case '>': return G_BOX_TEE_R;
        case '<': return G_BOX_TEE_L;
        case '+': return G_BOX_CROSS;
        default:  return G_BLANK;
    }
}

static void draw_keypad(void)
{
    char buf[KEYPAD_W + 1];
    uint8_t x, y;

    for (y = 0; y < KEYPAD_H; y++) {
        for (x = 0; x < KEYPAD_W; x++)
            buf[x] = (char)keypad_glyph(keypad[y][x]);
        buf[KEYPAD_W] = 0;
        scr_puts(KEYPAD_COL, (uint8_t)(KEYPAD_ROW + y), buf,
                 FG_WHITE | BG_BLACK);
    }
}

/* The wall that shuts the wire compartment in: a white line down WALL_COL
   from the top of the box to its bottom, which together with the box's own
   top, left and bottom sides makes the compartment a box of its own. It
   meets the top and bottom sides in corner pieces, so the sides carry on
   past it into the panel. */
static void draw_wall(void)
{
    uint8_t attr = FG_WHITE | BG_BLACK;
    uint8_t r;

    scr_putc(WALL_COL, WIRE_BOX_TOP, G_CORN_TL, attr);
    for (r = (uint8_t)(WIRE_BOX_TOP + 1); r < WIRE_BOX_BOTTOM; r++)
        scr_putc(WALL_COL, r, G_EDGE_L, attr);
    scr_putc(WALL_COL, WIRE_BOX_BOTTOM, G_CORN_BL, attr);
}

/* Is `row` one of the rows the dynamite covers? */
static uint8_t on_sticks(uint8_t row)
{
    return (uint8_t)(row >= STICK_ROW0 && row < STICK_ROW0 + STICK_ROWS);
}

/* The dynamite: STICK_ROWS rows of red right across the screen, split into
   two sticks by a thin black line on the STICK_SEAM row. A half block of
   black hugs the left side of the box as its shadow, broken by the seam,
   which runs on up to the box there. In the last column a white lead runs
   between the middles of the two sticks, turning in to each on a rounded
   corner and a short run of line. The box is drawn over the middle
   afterwards, so only the parts outside it show. */
static void draw_sticks(void)
{
    uint8_t attr = ATTR(FG_BLACK, BG_RED);
    uint8_t lead = FG_WHITE | BG_BLACK;
    uint8_t shadow = (uint8_t)(WIRE_BOX_LEFT - 1);
    uint8_t top = (uint8_t)(STICK_SEAM - LEAD_REACH);
    uint8_t bottom = (uint8_t)(STICK_SEAM + LEAD_REACH);
    uint8_t row, r;

    for (r = 0; r < STICK_ROWS; r++) {
        row = (uint8_t)(STICK_ROW0 + r);
        if (row == STICK_SEAM) {
            scr_fill(0, row, SCR_COLS-1, G_BOX_H, attr);
        } else {
            scr_fill(0, row, SCR_COLS-1, G_BLANK, attr);
        }
        scr_putc(shadow, row, G_HALF_R, attr);
    }

    /* The lead: down the last column between the two middles, the seam
       crossed, and a stub of line in from the column beside it at each. */
    for (row = top; row <= bottom; row++)
        scr_putc(LEAD_COL, row, G_BOX_V, lead);
    scr_putc(LEAD_COL, top, G_BOX_ARC_DL, lead);
    scr_putc(LEAD_COL, bottom, G_BOX_ARC_UL, lead);
    // scr_fill((uint8_t)(LEAD_COL - LEAD_LEN), top, LEAD_LEN, G_BOX_H, lead);
    // scr_fill((uint8_t)(LEAD_COL - LEAD_LEN), bottom, LEAD_LEN, G_BOX_H, lead);
}

/* The box's depth: it looks one column deep and one row deep, as if its back
   face were shifted down and to the right. Only the back face's right and
   bottom edges show, and a diagonal joins the front corners at the top right
   and the bottom left to them. At the bottom right the two back edges meet,
   and their corner stands in for the diagonal there. The right edge runs
   over the dynamite, so it takes that red for a background. */
static void draw_depth(void)
{
    uint8_t col = (uint8_t)(WIRE_BOX_RIGHT + 1);
    uint8_t row = (uint8_t)(WIRE_BOX_BOTTOM + 1);
    uint8_t r;

    scr_putc(col, WIRE_BOX_TOP, G_DIAG_L, FG_WHITE | BG_BLACK);
    for (r = (uint8_t)(WIRE_BOX_TOP + 1); r <= WIRE_BOX_BOTTOM; r++)
        scr_putc(col, r, G_EDGE_R,
                 (uint8_t)(FG_WHITE | BG_BLACK));

    scr_putc(WIRE_BOX_LEFT, row, G_DIAG_L, FG_WHITE | BG_BLACK);
    scr_fill((uint8_t)(WIRE_BOX_LEFT + 1), row,
             (uint8_t)(WIRE_BOX_RIGHT - WIRE_BOX_LEFT), G_EDGE_B,
             FG_WHITE | BG_BLACK);
    scr_putc(col, row, G_CORN_BR, FG_WHITE | BG_BLACK);
}

void view_frame(void)
{
    uint8_t r;

    /* The shared frame: the status bar, the bomb's case, and the prompt
       bar. The manual lives in the gap between the case and the prompt and
       is laid down later, by view_book_rise()/view_manual(). This is only
       ever called from view_title(), so it is the clean slate she paints on. */
    scr_cls(ATTR(FG_BLACK, BG_FIELD));
    scr_fill(0, ROW_STATUS, SCR_COLS, G_BLANK, ATTR_STATUS);
    draw_sticks();
    /* The box's inside is black, hiding the dynamite between its sides and
       giving every coloured wire band something dark to read against. */
    for (r = (uint8_t)(WIRE_BOX_TOP + 1); r < WIRE_BOX_BOTTOM; r++)
        scr_fill((uint8_t)(WIRE_BOX_LEFT + 1), r,
                 (uint8_t)(WIRE_BOX_RIGHT - WIRE_BOX_LEFT - 1), G_BLANK,
                 ATTR(FG_BLACK, BG_BLACK));
    outline(WIRE_BOX_LEFT, WIRE_BOX_TOP, WIRE_BOX_RIGHT, WIRE_BOX_BOTTOM,
            FG_WHITE | BG_BLACK);
    draw_wall();
    draw_depth();
    draw_keypad();      /* in the panel the wires never reach */
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
        scr_fill(SCISSORS_COL, row, SCISSORS_W, G_BLANK, FG_BLACK | BG_BLACK);
        scr_fill(CUT_COL_R, row, 1, G_CUT_R, (uint8_t)(fg | BG_BLACK));
    }
}

void view_bomb(const Bomb *b)
{
    uint8_t i;
    for (i = 0; i < b->num_wires; i++) view_wire(b, i, 0);
    for (; i < MAX_WIRES; i++) {
        /* The sixth wire's row is the box's bottom edge, so an empty slot
           there gets the border put back rather than a blank. */
        if (WIRE_ROW(i) == WIRE_BOX_BOTTOM)
            scr_fill(WIRE_COL0, WIRE_ROW(i), WIRE_W, G_EDGE_B,
                     FG_WHITE | BG_BLACK);
        else
            scr_fill(WIRE_COL0, WIRE_ROW(i), WIRE_W, G_BLANK,
                     FG_BLACK | BG_BLACK);
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
    scr_puts(SCISSORS_COL, row, S_SCISSORS, (uint8_t)(FG_WHITE | bg));
}

void view_cut_anim(const Bomb *b, uint8_t cut_mask, uint8_t idx)
{
    uint8_t row = WIRE_ROW(idx);
    uint8_t bg = (uint8_t)(((cut_mask >> idx) & 1) ? BG_BLACK
                                                     : wire_bg(b->colour[idx]));
    uint8_t attr = (uint8_t)(FG_WHITE | bg);

    scr_puts(SCISSORS_COL, row, S_SCISSORS_SHUT, attr);
    snd_tone(CUT_PITCH, 20);
    view_wait(1);
    snd_off();
    view_wait(3);
    scr_puts(SCISSORS_COL, row, S_SCISSORS, attr);
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
    uint8_t attr = ATTR_STATUS;
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
       book sweeping onto the table. The title's logo and menu sit inside the
       slot, so the sweep clears them; the credit and version share the
       status bar with the score, which they are wiped for. */
    uint8_t attr = FG_BLACK | BG_WHITE;
    uint8_t row;

    scr_fill(0, ROW_STATUS, SCR_COLS, G_BLANK, ATTR_STATUS);
    for (row = BOOK_ROW0; row <= BOOK_ROW1; row++) {
        scr_fill(BOOK_COL0, row, BOOK_W, G_BLANK, attr);
        view_wait(1);
    }
    drawn_page = PAGE_NONE;
}

void view_manual(uint8_t page)
{
    uint8_t attr = FG_BLACK | BG_WHITE;
    uint8_t row, i, n, full, hr, col;
    const Bomb *b;
    char nb[2];

    full = (uint8_t)(drawn_page != page);
    if (!full) return;

    /* Turning a page leaves the edge of the pages as it is; it only wants
       drawing when the book is new. */
    if (drawn_page == PAGE_NONE) manual_edge();
    manual_paint(attr);

    if (page == 0) {
        /* The cover is red from top to bottom, the edge of the pages
           beneath it left showing at the right. The title lines are centred
           on the red part. */
        uint8_t cover = FG_WHITE | BG_RED;
        uint8_t cw = (uint8_t)(BOOK_W - BOOK_EDGE_W);
        for (row = BOOK_ROW0; row <= BOOK_ROW1; row++)
            scr_fill(BOOK_COL0, row, cw, G_BLANK, cover);
        scr_puts((uint8_t)((cw - str_len(L->cover[0])) / 2),
                 (uint8_t)(BOOK_ROW0 + 1), L->cover[0], cover);
        scr_puts((uint8_t)((cw - str_len(L->cover[1])) / 2),
                 (uint8_t)(BOOK_ROW0 + 3), L->cover[1], cover);
        scr_puts((uint8_t)((cw - str_len(L->cover[2])) / 2),
                 (uint8_t)(BOOK_ROW0 + 5), L->cover[2], cover);
        // scr_puts((uint8_t)((cw - str_len(L->pages)) / 2),
        //          (uint8_t)(BOOK_ROW0 + 6), L->pages, cover);
        drawn_page = 0;
        drawn_wires = 0;
        return;
    }

    manual_border();

    /* The page number, in the top corner just inside the border. */
    put_udec((uint8_t)(BOOK_W - BOOK_EDGE_W - 3), BOOK_ROW0 + 1, page, 2, attr);

    b = &bombs[page - 1];
    n = b->num_wires;
    nb[0] = (char)('0' + n);
    nb[1] = 0;

    /* Heading: "BOMB NN(N WIRES)", or "BOMBE NN(N DRAEHTE)", the words
       coming from the language table so the labels' lengths set the layout. */
    hr = (uint8_t)(BOOK_ROW0 + 1);
    col = 1;
    scr_puts(col, hr, L->bomb, attr);
    col = (uint8_t)(col + str_len(L->bomb));
    put_udec(col, hr, page, 2, attr);
    col = (uint8_t)(col + 2);
    scr_puts(col++, hr, "(", attr);
    scr_puts(col++, hr, nb, attr);
    scr_puts(col, hr, L->wires, attr);
    col = (uint8_t)(col + str_len(L->wires));
    scr_puts(col, hr, ")", attr);

    for (i = 0; i < n; i++) {
        uint8_t c = b->colour[i];
        row = ENTRY_ROW(i);
        put_field(ENTRY_COL_ORDER, row, L->order[b->order[i]], ORDER_WIDTH, attr);
        /* The same band the wire is drawn as on the bomb, hatch and all. */
        scr_fill(ENTRY_COL_SWATCH, row, 2, wire_glyph(c), wire_attr(c));
        put_field(ENTRY_COL_NAME, row, L->colour[c], ENTRY_NAME_W, attr);
    }
    for (; i < drawn_wires; i++)
        scr_fill(BOOK_COL0, ENTRY_ROW(i), (uint8_t)(BOOK_W - BOOK_EDGE_W),
                 G_BLANK, attr);

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
    uint8_t attr = ATTR_PROMPT;
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

/* Blank cells between one item's markers and the next's. */
#define MENU_GAP    3

/* The menu is one line: each item sits in a cell as wide as its text plus a
   marker column either side, and the chosen one is drawn yellow with "<" and
   ">" in those columns. The cells stay put whichever item is chosen, and the
   whole line is centred. */
void view_title_menu(uint8_t selected)
{
    uint8_t total = (uint8_t)(MENU_GAP * (MENU_ITEMS - 1));
    uint8_t i, len, col, attr;
    const char *s;

    for (i = 0; i < MENU_ITEMS; i++)
        total = (uint8_t)(total + str_len(L->menu[i]) + 2);
    col = (uint8_t)((SCR_COLS - total) / 2);

    scr_fill(BOOK_COL0, MENU_ROW, BOOK_W, G_BLANK, FG_WHITE | BG_FIELD);
    for (i = 0; i < MENU_ITEMS; i++) {
        s = L->menu[i];
        len = str_len(s);
        attr = (uint8_t)((i == selected ? FG_YELLOW : FG_WHITE) | BG_FIELD);
        if (i == selected) {
            scr_putc(col, MENU_ROW, G_MARK_L, attr);
            scr_putc((uint8_t)(col + len + 1), MENU_ROW, G_MARK_R, attr);
        }
        scr_puts((uint8_t)(col + 1), MENU_ROW, s, attr);
        col = (uint8_t)(col + len + 2 + MENU_GAP);
    }
}

/* The name prompt takes over the menu's line under the logo -- the manual is
   about to be drawn over it anyway. The label and the field share the line,
   the field to the right of the label, and the two are centred together.
   Every call wipes and redraws the lot, so a name that has just shrunk leaves
   nothing behind, and the field is drawn out to its full width in dots so the
   player can see how much room there is. */
void view_name(const char *name)
{
    uint8_t attr = FG_WHITE | BG_FIELD;
    uint8_t len = str_len(name);
    uint8_t label = str_len(L->name_label);
    uint8_t col = (uint8_t)((SCR_COLS - (label + 1 + NAME_LEN)) / 2);
    uint8_t field = (uint8_t)(col + label + 1);

    scr_fill(BOOK_COL0, NAME_ROW, BOOK_W, G_BLANK, attr);

    scr_puts(col, NAME_ROW, L->name_label, attr);
    scr_fill(field, NAME_ROW, NAME_LEN, '.', attr);
    scr_puts(field, NAME_ROW, name, FG_YELLOW | BG_FIELD);
    if (len < NAME_LEN)
        scr_putc((uint8_t)(field + len), NAME_ROW, G_SOLID, FG_YELLOW | BG_FIELD);

    view_prompt(L->name_keys);
}

/* The BOMBTEAM logo, one string per row and exactly SCR_COLS wide, drawn as
   an outline. Each letter below stands for one of the G_* pieces above:

       F -  Z      top-left corner, top edge, top-right corner
       [    ]      left edge, right edge
       L _  J      bottom-left corner, bottom edge, bottom-right corner
       , ` * '     rounded corners: top-left, top-right, bottom-left,
                   bottom-right
       / A         the two diagonals, '/' and '\'

   and anything else is blank. */
#define LOGO_ROWS   4

static const char *const logo[LOGO_ROWS] = {
    "F-`  ,--` A  / F-`   F--Z FZ    /A  A  /",
    "[  ` [,`] [A/] [  `  L  J [ Z  /  A [A/]",
    "[  ] [*'] [  ] [  ]   []  [  Z [  ] [  ]",
    "L__' *__' L__J L__'   LJ  L__J L__J L__J"
};

static uint8_t logo_glyph(char ch)
{
    switch (ch) {
        case 'F':  return G_CORN_TL;
        case 'Z':  return G_CORN_TR;
        case 'L':  return G_CORN_BL;
        case 'J':  return G_CORN_BR;
        case '-':  return G_EDGE_T;
        case '_':  return G_EDGE_B;
        case '[':  return G_EDGE_L;
        case ']':  return G_EDGE_R;
        case ',':  return G_ARC_TL;
        case '`':  return G_ARC_TR;
        case '*':  return G_ARC_BL;
        case '\'': return G_ARC_BR;
        case '/':  return G_DIAG_R;
        case 'A':  return G_DIAG_L;
        default:   return G_BLANK;
    }
}

/* The outline is white on the blue screen; the inside of the O -- the two
   cells its outline encloses on each of its two middle rows -- is filled in
   red. */
#define LOGO_ATTR       (FG_WHITE | BG_FIELD)
#define LOGO_FILL_ATTR  (FG_RED | BG_FIELD)
#define LOGO_O_COL      6
#define LOGO_O_W        2

static void draw_logo(uint8_t row0)
{
    char buf[SCR_COLS + 1];
    uint8_t r, c;

    for (r = 0; r < LOGO_ROWS; r++) {
        for (c = 0; c < SCR_COLS; c++)
            buf[c] = (char)logo_glyph(logo[r][c]);
        buf[SCR_COLS] = 0;
        scr_puts(0, (uint8_t)(row0 + r), buf, LOGO_ATTR);
    }
    for (r = 1; r <= 2; r++)
        scr_fill(LOGO_O_COL, (uint8_t)(row0 + r), LOGO_O_W, G_SOLID,
                 LOGO_FILL_ATTR);
}

void view_title(void)
{
    /* The frame gives us the status bar, the bomb's case and the prompt bar;
       onto it the title paints a sample bomb, the blinking lamp and, where
       the manual will rise, the logo and the menu. */
    view_frame();
    view_bomb(&bombs[22]);
    view_light(LIGHT_RED);

    /* The status bar has nothing to report until a game starts, so the
       credit takes its left-hand end and the version its right-hand one;
       the score counter wipes both when the game begins. */
    scr_puts(0, ROW_STATUS, "NOAH BURNEY 2026", ATTR_STATUS);
    scr_puts((uint8_t)(40 - (sizeof VERSION - 1)), ROW_STATUS, VERSION,
             ATTR_STATUS);

    draw_logo(TITLE_ROW);
    view_logo_light(0);
    view_title_menu(MENU_START);

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

    view_message(1, L->boom, ATTR(FG_RED, BG_BLACK));

    label = L->final;
    labellen = str_len(label);
    col = (uint8_t)((SCR_COLS - (labellen + 6)) / 2);
    scr_puts(col, 3, label, attr);
    put_udec((uint8_t)(col + labellen), 3, score, 6, attr);
}

/* The table, under the banner view_boom() left at the top of the black
   screen. Each row reads " 1. NAME....  000123" -- rank, name in a
   NAME_LEN-wide field, score -- and the block as a whole is centred. */
#define HS_COL0     10
#define HS_ROW0     7

void view_scores(const HiScore *table, uint8_t hilite)
{
    uint8_t i, row, attr;
    char rank[4];
    const HiScore *e;

    view_message(5, L->hiscores, FG_WHITE | BG_BLACK);

    for (i = 0; i < HS_ENTRIES; i++) {
        e = &table[i];
        row = (uint8_t)(HS_ROW0 + i);
        attr = (uint8_t)((i == hilite ? FG_YELLOW : FG_WHITE) | BG_BLACK);
        /* 1..9 right-aligned, then 10. */
        rank[0] = ' ';
        rank[1] = (char)('1' + i);
        if (i == 9) {
            rank[0] = '1';
            rank[1] = '0';
        }
        rank[2] = '.';
        rank[3] = 0;
        scr_puts(HS_COL0, row, rank, attr);

        if (e->name[0]) {
            put_field((uint8_t)(HS_COL0 + 4), row, e->name, NAME_LEN, attr);
            put_udec((uint8_t)(HS_COL0 + 14), row, e->score, 6, attr);
        } else {
            scr_puts((uint8_t)(HS_COL0 + 4), row, "--------", attr);
            scr_puts((uint8_t)(HS_COL0 + 14), row, "------", attr);
        }
    }

    view_message(22, L->press, FG_WHITE | BG_BLACK);
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
