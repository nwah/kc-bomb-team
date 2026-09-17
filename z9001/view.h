#ifndef VIEW_H
#define VIEW_H

#include <stdint.h>
#include "bombs.h"

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

/* Rows. Row 0 is the blue status bar and row 23 is the blue prompt bar;
   row 1 is a black margin. Rows 2..14 hold the bomb (a lamp and the timer
   up top, the wires in the box beneath, scissors at the left), rows 15..21
   hold the manual, and row 22 is the credit / page-number line. */
#define ROW_STATUS      0
#define ROW_PROMPT      23
#define CREDIT_ROW      22

/* Bomb. The lamp and the countdown readout sit in the top corner of the
   box; wire i runs across the box on WIRE_ROW(i), and a cut severs the band
   at the scissors, which sit in the middle of it. */
#define LIGHT_ROW       2
#define LIGHT_COL       37
#define TIMER_ROW       2
#define TIMER_COL       32
#define TIMER_W         4          /* wide enough for "SAFE" */

#define WIRE_ROW(i)     (4 + 2 * (i))   /* 4,6,8,10,12,14 */
#define WIRE_COL0       6
#define WIRE_W          31        /* cols 6..36 */
#define SCISSORS_COL    31        /* the scissors sit in the band */
#define CUT_COL_L       (SCISSORS_COL - 1)
#define CUT_COL_R       (SCISSORS_COL + 1)

#define WIRE_BOX_LEFT   5
#define WIRE_BOX_RIGHT  38
#define WIRE_BOX_TOP    3
#define WIRE_BOX_BOTTOM 14

/* The title text shares row 2 with the lamp/timer -- it is only drawn on the
   title screen and is wiped by view_book_rise() when a game starts. */
#define TITLE_ROW       2

/* The manual: a full-width panel, white on black, below the bomb. Row 15 is
   its rule, row 16 its heading, and rows 17..22 the six wire entries -- one
   per cell row, enough for the widest bomb. */
#define BOOK_COL0       0
#define BOOK_W          40
#define BOOK_ROW0       15      /* rule / cover top */
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

/* Title screen menu: three items drawn one to a row, the chosen one
   picked out in yellow. MENU_ITEMS sizes the language table's menu[] and
   is used by game.c to track the selection. */
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
extern void view_boom(uint16_t score);
extern void view_defused(void);

#endif /* VIEW_H */
