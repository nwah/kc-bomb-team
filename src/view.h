#ifndef VIEW_H
#define VIEW_H

#include <stdint.h>
#include "bombs.h"

/*
 * Everything that touches the screen or the speaker lives here. game.c
 * calls into this module but never pokes at screen coordinates itself, and
 * this module never knows a game rule (score, cut order, timers).
 */

/* Rows. */
#define ROW_STATUS      0
#define ROW_FRAME_TOP   1
#define ROW_INTERIOR0   2
#define ROW_INTERIOR1   14
#define ROW_FRAME_BOT   15
#define ROW_GAP         16
#define ROW_MANUAL0     17
#define ROW_MANUAL1     31

/* Wire i sits at row 3 + 2*i, columns 2..29. */
#define WIRE_ROW(i)     (3 + 2 * (i))
#define WIRE_COL0       2
#define WIRE_COL_LEN    28
#define SCISSORS_COL    15
/* A cut takes the three columns the scissors sit in the middle of: the
   frayed ends either side and the gap itself. */
#define CUT_COL_L       (SCISSORS_COL - 1)
#define CUT_COL_R       (SCISSORS_COL + 1)

/* The countdown light: a 3x2 block at columns 33..35, rows 3..4. */
#define LIGHT_COL       33
#define LIGHT_ROW       3
#define LIGHT_W         3
#define LIGHT_H         2

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

/* Title screen: sample bomb and banner text. Does not wait for input. */
extern void view_title(void);

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

/* Renders manual page `page` (0 = cover, 1..48 = bombs[page-1]). */
extern void view_manual(uint8_t page);

/* Centres `s` on the manual page's bottom line, which view_manual() leaves
   alone; pass a null pointer to clear it again. */
extern void view_prompt(const char *s);

/* Draws `s` horizontally centred on `row` in the given attribute. */
extern void view_message(uint8_t row, const char *s, uint8_t attr);

/* Boom sequence: a white flash cooling to black under a descending tone,
   then BOOM! and the final score. Does not wait for input. */
extern void view_boom(uint16_t score);

/* Defuse flourish: flashes the light green three times with a beep. */
extern void view_defused(void);

#endif /* VIEW_H */
