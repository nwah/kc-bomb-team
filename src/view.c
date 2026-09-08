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
    scr_cls(FG_BLACK | BG_BLACK);
}

void view_frame(void)
{
    uint8_t r;

    scr_fill(0, ROW_STATUS, 40, G_BLANK, FG_BLACK | BG_WHITE);
    scr_fill(0, ROW_FRAME_TOP, 40, G_BLANK, FG_BLACK | BG_WHITE);
    scr_fill(0, ROW_FRAME_BOT, 40, G_BLANK, FG_BLACK | BG_WHITE);

    for (r = ROW_INTERIOR0; r <= ROW_INTERIOR1; r++) {
        scr_fill(0, r, 40, G_BLANK, FG_BLACK | BG_BLACK);
        scr_attr(0, r, 1, FG_BLACK | BG_WHITE);
        scr_attr(39, r, 1, FG_BLACK | BG_WHITE);
    }

    scr_fill(0, ROW_GAP, 40, G_BLANK, FG_BLACK | BG_BLACK);

    for (r = ROW_MANUAL0; r <= ROW_MANUAL1; r++) {
        scr_fill(0, r, 40, G_BLANK, FG_BLACK | BG_WHITE);
    }
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
    uint8_t fg, r;

    switch (state) {
        case LIGHT_RED:   fg = FG_RED;   break;
        case LIGHT_GREEN: fg = FG_GREEN; break;
        default:          fg = FG_BLACK; break;
    }
    for (r = 0; r < LIGHT_H; r++) {
        scr_fill(LIGHT_COL, (uint8_t)(LIGHT_ROW + r), LIGHT_W, G_SOLID,
                 (uint8_t)(fg | BG_BLACK));
    }
}

/* ------------------------------------------------------------------- */
/* Status bar.                                                         */
/* ------------------------------------------------------------------- */

void view_status(uint16_t score, uint8_t defused)
{
    uint8_t attr = FG_BLACK | BG_WHITE;

    scr_puts(0, ROW_STATUS, "SCORE ", attr);
    put_udec(6, ROW_STATUS, score, 6, attr);
    scr_puts(30, ROW_STATUS, "DEFUSED ", attr);
    put_udec(38, ROW_STATUS, defused, 2, attr);
}

/* ------------------------------------------------------------------- */
/* Manual.                                                             */
/* ------------------------------------------------------------------- */

void view_manual(uint8_t page)
{
    static const char *colour_name[6] = {
        "Red", "Red striped", "Blue", "Blue striped", "Green", "Green striped"
    };
    static const char *order_text[6] = {
        "leave it", "cut 1st", "cut 2nd", "cut 3rd", "cut 4th", "cut 5th"
    };
    uint8_t attr = FG_BLACK | BG_WHITE;
    uint8_t row, i, n;
    const Bomb *b;
    char nb[2];

    /* Clear the whole body of the page: not just what a previous page left
       behind, but also the title screen's banner, which sits in here. */
    for (row = 18; row <= 30; row++) scr_fill(0, row, 40, G_BLANK, attr);

    if (page == 0) {
        view_message(22, "BOMB DEFUSAL MANUAL", attr);
        return;
    }

    scr_puts(2, 18, "BOMB DEFUSAL MANUAL", attr);
    put_udec(33, 18, page, 2, attr);
    scr_puts(35, 18, "/48", attr);

    scr_fill(2, 19, 36, G_RULE, attr);

    b = &bombs[page - 1];
    n = b->num_wires;
    nb[0] = (char)('0' + n);
    nb[1] = 0;
    scr_puts(2, 20, nb, attr);
    scr_puts(3, 20, " wires", attr);

    for (i = 0; i < n; i++) {
        row = (uint8_t)(22 + i);
        scr_fill(2, row, 2, wire_glyph(b->colour[i]), wire_attr(b->colour[i]));
        scr_puts(5, row, colour_name[b->colour[i]], attr);
        scr_puts(22, row, order_text[b->order[i]], attr);
    }

    scr_puts(2, 30, "\x06\x07 page  \x08\x09 wire   SPACE cut", attr);
}

void view_prompt(const char *s)
{
    uint8_t attr = FG_BLACK | BG_WHITE;
    scr_fill(0, ROW_MANUAL1, 40, G_BLANK, attr);
    if (s) view_message(ROW_MANUAL1, s, attr);
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

void view_title(void)
{
    uint8_t attr = FG_BLACK | BG_WHITE;
    uint8_t row;

    view_frame();
    view_bomb(&bombs[22]);
    view_light(LIGHT_RED);

    for (row = ROW_MANUAL0; row <= ROW_MANUAL1; row++)
        scr_fill(0, row, 40, G_BLANK, attr);

    /* The banner in reverse video, the way the original had it. */
    scr_fill(13, 20, 14, G_BLANK, FG_WHITE | BG_BLACK);
    view_message(20, " BOMB SQUAD ", FG_WHITE | BG_BLACK);

    view_message(23, "Noah Burney 2025", attr);
    view_message(25, kc85_4 ? "KC 85/4" : "KC 85/3", attr);
    view_message(28, "PRESS SPACE TO START", attr);
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

    for (i = 16; i > 0; i--) {
        hot = fade[i - 1];
        for (row = 0; row <= ROW_MANUAL1; row++) scr_attr(0, row, 40, hot);
        snd_tone((uint16_t)(BOOM_PITCH - i), (uint8_t)(i + 15));
        view_wait(2);
    }
    snd_off();
    scr_cls(FG_BLACK | BG_BLACK);

    attr = FG_WHITE | BG_BLACK;
    view_message(14, "BOOM!", (uint8_t)(FG_RED | BG_BLACK));

    label = "FINAL SCORE ";
    labellen = str_len(label);
    col = (uint8_t)((40 - (labellen + 6)) / 2);
    scr_puts(col, 18, label, attr);
    put_udec((uint8_t)(col + labellen), 18, score, 6, attr);

    view_message(22, "PRESS SPACE", attr);
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
}
