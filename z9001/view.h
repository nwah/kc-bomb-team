#ifndef VIEW_H
#define VIEW_H

#include <stdint.h>
#include "bombs.h"
#include "scores.h"

/*
 * Everything that touches the screen or the speaker lives here. game.c
 * calls into this module but never pokes at screen coordinates itself, and
 * this module never knows a game rule (score, cut order, timers).
 *
 * Z9001 / KC 85/1.10 flavour: a single 40x24 text screen, laid out the way
 * the original Atari version was -- the bomb lives in the top half and the
 * defusal manual in the bottom one, rather than the KC85's left/right
 * split. There is no user-defined glyph set; the built-in ROM font is used
 * directly, so every G_* symbol below is a plain character code.
 */

/* Rows. Row 0 is the status bar, white on black, and row 23 the prompt bar,
   white on blue, on every screen; on the title screen the status bar
   carries the credit at its left end and the version at its right. The
   field between them is black. Row 1 is a margin, rows 2..14 hold the bomb
   (row 14 only its bottom edge) and rows 15..22 hold the manual -- or, on
   the title screen, the logo and the menu. */
#define ROW_STATUS      0
#define ROW_PROMPT      23

/* Bomb. Two sticks of dynamite lie across the whole width of the screen, one
   on top of the other, and a narrow box (cols 6..33, rows 2..13) is strapped
   over their middles, its black inside hiding the sticks between its sides
   and leaving their ends poking out either side. A thin line separates the
   sticks, on a row of its own, and a half block of black beside the box's
   left side is its shadow. The box looks one column and one row deep: its
   back face's right and bottom edges show at col 34 and on row 14. The
   box's top and bottom rows stay black, so the sticks stand clear of its
   outline. In the last column a white lead ties the middles of the two
   sticks together. */
#define STICK_ROW0      4               /* the top stick's first row */
#define STICK_H         4               /* rows to a stick */
#define STICK_SEAM      (STICK_ROW0 + STICK_H)  /* the line between them */
#define STICK_ROWS      (2 * STICK_H + 1)       /* rows 3..11 in all */

/* The lead runs down column LEAD_COL from LEAD_REACH rows above the seam to
   as many below it, and turns in at each end for LEAD_LEN columns. */
#define LEAD_COL        39
#define LEAD_REACH      2
#define LEAD_LEN        2

/* Inside the box the wires run across the left part, and the right part is a
   panel holding the lamp, the countdown readout and a dummy keypad. Wire i
   runs on WIRE_ROW(i), and a cut severs the band at the scissors, which sit
   in the middle of it. */
#define WIRE_ROW(i)     (3 + 2 * (i))   /* 3,5,7,9,11,13 */
#define WIRE_COL0       7
#define WIRE_W          15        /* cols 8..21 */
#define SCISSORS_COL    14        /* the scissors sit in the band */
#define SCISSORS_W      2         /* ">B" open, "=B" closed */
#define CUT_COL_L       (SCISSORS_COL - 1)
#define CUT_COL_R       (SCISSORS_COL - 1 + SCISSORS_W)

#define WIRE_BOX_LEFT   5
#define WIRE_BOX_RIGHT  32
#define WIRE_BOX_TOP    2
#define WIRE_BOX_BOTTOM 13

/* The wire compartment is walled off from the panel by a white line down
   WALL_COL, so it is a box of its own. The panel is cols 25..31 -- the
   readout and lamp share the top wire's row and the keypad fills the rows
   below them. */
#define WALL_COL        24
#define LIGHT_ROW       3
#define LIGHT_COL       31
#define TIMER_ROW       3
#define TIMER_COL       25
#define TIMER_W         4          /* wide enough for "SAFE" */

/* A dummy 3x4 keypad, drawn as a table of square buttons with the chargen's
   box-drawing pieces, the buttons sharing their borders and the bottom-right
   two merged into one tall ENTER button. The table itself is the art in
   view.c; it is 7 cols by 9 rows, so this puts it on cols 25..31 and rows
   4..12. Purely decoration -- nothing reads it. */
#define KEYPAD_COL      25
#define KEYPAD_ROW      4

/* The title screen's lower half: the four-row block-graphics logo sits on
   rows 16..19 under the box, and the menu is a single line on row 21
   with a blank row either side. The name prompt replaces the menu on that
   same line: its label, then the field to the right of it. All of it is
   wiped along with the rest of the panel by view_book_rise() when a game
   starts. */
#define TITLE_ROW       16
#define NAME_ROW        21
#define MENU_ROW        21

/* The manual: a full-width panel, black on white, below the bomb. Row 15
   holds only the page number, row 16 the heading, and rows 17..22 the six
   wire entries -- one per cell row, enough for the widest bomb. The last
   BOOK_EDGE_W columns are the edge of the pages beneath, white with a "|"
   in each and a solid diagonal wedge at the top, each column starting a
   row lower than the one before it, on every page; the cover is red over
   all the rest, and the page number sits just inside the edge. */
#define BOOK_COL0       0
#define BOOK_W          40
#define BOOK_EDGE_W     2
#define BOOK_ROW0       15      /* page number / cover top */
#define BOOK_ROW1       22
#define ENTRY_ROW(i)    (17 + (i))   /* 17..22 */
#define ENTRY_COL_ORDER 1
#define ORDER_WIDTH      3
#define ENTRY_COL_SWATCH 4
#define ENTRY_COL_NAME   7
#define ENTRY_NAME_W     13

/* Two lamps, one state: whichever the state names is lit and the other is
   painted black, which is what the box behind it already is. */
#define LIGHT_OFF       0
#define LIGHT_RED       1
#define LIGHT_GREEN     2

/* Sound pitches. The Z9001 has no CTC audio, so these are abstract note
   values handed to snd_tone() as pitch * HAL_TUNE_T / 5000; the relative
   ordering (snip low, tick high, boom low, chime mid) is what matters and
   is the same one the KC85 builds its rules around, so the values match it
   and the shared game.c needs no per-target pitch table. */
#define TICK_PITCH      55
#define CUT_PITCH       0x011F
#define BEEP_PITCH      83
#define BOOM_PITCH      0x0164

/* Non-blocking read of the keyboard buffer; 0 when nothing is waiting.
   Implemented in hw.c. */
extern uint8_t key_get(void);

/* Waits `ticks` fiftieths of a second, spinning on clk_ticks(). */
extern void view_wait(uint8_t ticks);

/* One-time setup: clears the screen. Called after scr_setup(). */
extern void view_init(void);

/* Title screen menu: three items side by side on MENU_ROW, the chosen one
   picked out in yellow and bracketed by "<" and ">". MENU_ITEMS sizes the
   language table's menu[] and is used by game.c to track the selection. */
#define MENU_START  0
#define MENU_LANG   1
#define MENU_EXIT   2
#define MENU_ITEMS  3

/* Title screen: a sample bomb and the BOMB SQUAD logo. Does not wait for
   input; the caller blinks the logo lamp via view_logo_light() while it
   waits. */
extern void view_title(void);
extern void view_title_menu(uint8_t selected);
extern void view_lang_toggle(void);
extern void view_continue(void);
extern void view_exit(void);
extern void view_logo_light(uint8_t on);

/* Bomb casing frame and the manual/status backgrounds. */
extern void view_frame(void);
extern void view_bomb(const Bomb *b);
extern void view_wire(const Bomb *b, uint8_t idx, uint8_t severed);
extern void view_scissors(const Bomb *b, uint8_t cut_mask, uint8_t old_idx,
                          uint8_t new_idx);
extern void view_cut_anim(const Bomb *b, uint8_t cut_mask, uint8_t idx);
extern void view_light(uint8_t state);
extern void view_status(uint16_t score, uint8_t defused);
extern void view_timer(uint8_t ticks);
extern void view_book_rise(void);
extern void view_manual(uint8_t page);
extern void view_prompt(const char *s);
extern void view_message(uint8_t row, const char *s, uint8_t attr);
/* Name prompt, drawn over the title screen where the menu was: the label,
   then `name` (NUL-terminated, at most NAME_LEN long, already uppercase) in
   a NAME_LEN-wide field with a cursor after the last character, then a key
   legend on the prompt bar. game.c calls it again after every keystroke, so
   it must redraw the whole field each time and wipe what a longer name left
   behind. */
extern void view_name(const char *name);

/* Boom sequence: a white flash cooling to black under a descending tone,
   then BOOM! and the final score at the top of the screen. Does not wait for
   input and does not draw the "press a key" line -- view_scores() follows it
   on the same screen. */
extern void view_boom(uint16_t score);

/* High-score table, drawn below the BOOM! banner: a heading, HS_ENTRIES rows
   of rank, name and score (an empty slot shows dashes), and the "press a
   key" line. Row `hilite` is picked out in a different colour -- it is the
   score the player just made -- or pass 0xFF for none. */
extern void view_scores(const HiScore *table, uint8_t hilite);
extern void view_defused(void);

#endif /* VIEW_H */
