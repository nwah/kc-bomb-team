#ifndef HW_H
#define HW_H

#include <stdint.h>

/*
 * Direct access to the Z9001 hardware: 40x24 text mode (ROM chargen at the
 * 8.5" video RAM at $EC00, colour attributes at $E800 -- attr addr is char
 * addr - $0400), the single-bit beeper on PIO1 port A bit 7 (port $88), the
 * keyboard buffer byte at $0025, and a cycle-exact 50 Hz tick.
 *
 * Everything visible goes through view.c; game.c only touches this module
 * through the clock and the speaker.
 */

/* Z9001 attribute byte: bits 4-6 = foreground R/G/B, bits 0-2 = background
   R/G/B, bit 7 = blink. The palette has 16 entries; the ordering in memory
   is R(1) G(2) B(4) for the low nibble and R G B shifted up one for the
   high, so a colour nibble (0..7) has to be split to form a byte. */
#define ATTR(fg, bg) ((uint8_t)(((fg) & 0x70) | ((bg) & 0x07)))

#define FG_BLACK    0x00
#define FG_RED      0x10
#define FG_GREEN    0x20
#define FG_BLUE     0x40
#define FG_MAGENTA  0x50
#define FG_YELLOW   0x30
#define FG_CYAN     0x60
#define FG_WHITE    0x70

#define BG_BLACK    0x00
#define BG_RED      0x01
#define BG_GREEN    0x02
#define BG_BLUE     0x04
#define BG_MAGENTA  0x05
#define BG_YELLOW   0x03
#define BG_CYAN     0x06
#define BG_WHITE    0x07

/* Screen buffers. attr(col,row) == &CRAM[row*40+col], char there at VRAM[+same]. */
#define SCR_COLS 40
#define SCR_ROWS 24
#define VRAM ((volatile uint8_t *)0xEC00u)
#define CRAM ((volatile uint8_t *)0xE800u)
#define CELL(row, col) ((uint16_t)(row) * SCR_COLS + (col))

/* Beep state shared between hw.c and hw.asm. g_reload is 0 while the speaker
   is silent; any non-zero value is a reload count that _hal_wait_frame
   decrements and re-triggers the beeper with, so the note's pitch tracks it. */
extern uint8_t g_reload;
extern uint8_t g_beeper;
extern uint8_t g_pitch;

/* The number of phase steps _hal_wait_frame makes in one 20 ms frame. The
   reload fed to the beeper is derived from it in snd_tone(); keep this #define
   and the T_B_TONE equ in hw.asm in step. */
#define HAL_TUNE_T 1130u

/* One-time setup: clears the screen and silences the speaker. */
extern void scr_setup(void);

/* Screen primitives. col/row are 0-based text coordinates. */
extern void scr_cls(uint8_t attr);
extern void scr_fill(uint8_t col, uint8_t row, uint8_t len, uint8_t ch,
                     uint8_t attr);
extern void scr_puts(uint8_t col, uint8_t row, const char *s, uint8_t attr);
extern void scr_putc(uint8_t col, uint8_t row, uint8_t ch, uint8_t attr);
extern void scr_attr(uint8_t col, uint8_t row, uint8_t len, uint8_t attr);

/* Clock: a byte that falls 50 times a second. elapsed = (earlier - later)
   & 0xff, exactly as game.c's tick() expects of the KC's CTC counter. */
extern uint8_t clk_ticks(void);

/* Speaker. pitch is an arbitrary positive note value; 0 is silence. The
   caller's volume is ignored -- the beeper is on/off only -- but is kept
   in the signature so game.c, written for the KC's 5-bit DAC, is reusable
   unchanged. */
extern void snd_tone(uint16_t pitch, uint8_t volume);
extern void snd_off(void);

/* Non-blocking read of the keyboard buffer at $0025 (an ASCII byte the BIOS
   keyboard scanner leaves behind). 0 when nothing is waiting; otherwise the
   key, consumed (cleared to 0) so each press is delivered exactly once. */
extern uint8_t key_get(void);

/* Hal: exactly one 50 Hz tick with the beeper toggled in step if a tone is
   active. Called by clk_ticks(); exposed so a tuning probe can drive it. */
extern void hal_wait_frame(void);
extern void hal_beeper_off(void);

#endif /* HW_H */
