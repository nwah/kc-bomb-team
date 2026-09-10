#ifndef VIEW_H
#define VIEW_H

#include <stdint.h>
#include "bombs.h"

/*
 * Everything that touches the screen or the speaker lives here. game.c
 * calls into this module but never pokes at screen coordinates itself, and
 * this module never knows a game rule (score, cut order, timers).
 */

/* Rows. Row 0 is the status bar and row 1 stays black, a margin between it
   and the panels below. Rows 2..29 hold the book, on the left, and the
   bomb, on the right, separated by a black column 23; row 29 is their
   shared depth/blank row below the casing. */
#define ROW_STATUS      0
#define ROW_PROMPT      31

/* The bomb: a bundle of dynamite in columns 26..37 with a black box
   strapped across it, outlined in columns 24..39 (see bomb_dynamite() and
   bomb_box() in view.c for how they are drawn). Wire i sits on row 12 +
   2*i, in the wire well that runs columns 27..36 and is exactly as tall as
   the wires themselves. */
#define WIRE_ROW(i)     (12 + 2 * (i))
#define WIRE_COL0       27
#define WIRE_COL_LEN    10
#define SCISSORS_COL    31
/* A cut takes the three columns the scissors sit in the middle of: the
   frayed ends either side and the gap itself. */
#define CUT_COL_L       (SCISSORS_COL - 1)
#define CUT_COL_R       (SCISSORS_COL + 1)

/* Two lamps, a cell each, stacked down the margin beside the readout's
   panel: the countdown's red one and the safe green one below it. Neither
   is ringed, so an unlit lamp is black on black and the box simply shows
   nothing there. */
#define LIGHT_COL       25
#define LIGHT_ROW       7
#define SAFE_ROW        8

/* The countdown readout: a four-cell field in its own outlined panel below
   the lamps and above the wire compartment -- wide enough for the word the
   bomb ends on, with the two digits centred in it while it is still
   counting. bomb_box() draws the panel around it. */
#define TIMER_COL       27
#define TIMER_W         4
#define TIMER_ROW       9

/* The book: columns 0..21, rows BOOK_ROW0..BOOK_ROW1 hold the cover (page
   0) or a manual page (1..48). It sits low enough that its foot is behind
   the instruction bar and off the bottom of the screen, so only the
   fore-edge down its right-hand side is ever drawn -- see book_edges().
   Entry i of a page sits on ENTRY_ROW(i). */
#define BOOK_COL0       0
#define BOOK_W          22
#define BOOK_ROW0       4
#define BOOK_ROW1       30
#define ENTRY_ROW(i)    (14 + 2 * (i))

#define LIGHT_OFF       0
#define LIGHT_RED       1
#define LIGHT_GREEN     2

/* Sound pitches, see hw.h for the pitch encoding: f = 1750000/(2*prescaler*N),
   i.e. f = 54688/N on the fast (/16) prescaler and f = 3418/N on the slow
   (/256) one (bit 0 of the high byte set). */
#define TICK_PITCH      55      /* ~1000 Hz tick as the clock counts down */
#define CUT_PITCH       0x011F  /* ~110 Hz snip while cutting, slow prescaler */
#define BEEP_PITCH      83      /* ~660 Hz defuse chime */
#define BOOM_PITCH      0x0164  /* base for the descending explosion tone,
                                    slow prescaler; N sweeps upward from here
                                    as the blast fades, so pitch falls */

/* Non-blocking read of the CAOS keyboard buffer; 0 when nothing is waiting.
   Implemented in main.c. */
extern uint8_t key_get(void);

/* Waits `ticks` fiftieths of a second, spinning on clk_ticks(). */
extern void view_wait(uint8_t ticks);

/* One-time setup: clears the screen. Call after scr_setup(). */
extern void view_init(void);

/* Title screen: sample bomb and the BOMB TEAM logo. Does not wait for
   input; the caller drives view_logo_light() while it waits. */
extern void view_title(void);

/* The red light inside the logo's O, on or off. The title screen's own
   wait loop blinks it, the way the bomb's lamp blinks on a tick. */
extern void view_logo_light(uint8_t on);

/* Draws the bomb casing frame and the manual/status page backgrounds. Does
   not draw wires, the light, or manual text -- callers draw those next. */
extern void view_frame(void);

/* Draws every wire of a bomb, unsevered, and blanks any leftover wire rows
   from a previous (larger) bomb below num_wires. */
extern void view_bomb(const Bomb *b);

/* Draws wire `idx` of bomb `b`; if severed is non-zero, draws it cut. */
extern void view_wire(const Bomb *b, uint8_t idx, uint8_t severed);

/* Moves the scissors from wire old_idx to wire new_idx, redrawing the wire
   left behind correctly whether or not it is severed (cut_mask has bit i
   set when wire i has been cut). Pass old_idx == 0xFF for the first draw,
   to skip erasing. */
extern void view_scissors(const Bomb *b, uint8_t cut_mask, uint8_t old_idx,
                           uint8_t new_idx);

/* Plays the wire-cutting animation on wire `idx` of bomb `b`. cut_mask says
   which wires are already severed, so the scissors keep the right backdrop
   when the player snips at one twice. */
extern void view_cut_anim(const Bomb *b, uint8_t cut_mask, uint8_t idx);

/* Countdown light: LIGHT_OFF / LIGHT_RED / LIGHT_GREEN. */
extern void view_light(uint8_t state);

/* Status bar: score and defused count. */
extern void view_status(uint16_t score, uint8_t defused);

/* Countdown readout: the number of ticks the fuse has left, not seconds --
   the interval between ticks halves as the fuse burns down, so ticks are
   what the player can actually count against the beeps. */
extern void view_timer(uint8_t ticks);

/* Slides the book up into place from the bottom of the screen, wiping away
   whatever the title screen left behind it. Leaves the panel filled but
   blank; the caller's next view_manual() lays the page out on top. */
extern void view_book_rise(void);

/* Renders manual page `page` (0 = cover, 1..48 = bombs[page-1]). */
extern void view_manual(uint8_t page);

/* Puts `s` on the blue bar along the bottom of the screen; pass a null
   pointer to put the key legend back, which is what that bar carries
   whenever there is nothing else to say. */
extern void view_prompt(const char *s);

/* Draws `s` horizontally centred on `row` in the given attribute. */
extern void view_message(uint8_t row, const char *s, uint8_t attr);

/* Boom sequence: a white flash cooling to black under a descending tone,
   then BOOM! and the final score. Does not wait for input. */
extern void view_boom(uint16_t score);

/* Defuse flourish: flashes the green lamp three times with a beep, then
   leaves it lit and the readout reading SAFE. Both stay that way until the
   next bomb's reset_timer() puts the countdown back. */
extern void view_defused(void);

#endif /* VIEW_H */
