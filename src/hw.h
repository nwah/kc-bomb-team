#ifndef HW_H
#define HW_H

#include <stdint.h>

/*
 * Direct access to the KC 85 hardware.  The implementation is in hw.asm; see
 * the comments there for how the two machines differ.
 */

/* Non-zero on a KC 85/4 (or /5).  Valid once scr_setup() has run. */
extern uint8_t kc85_4;

/*
 * Screen.  40x32 character cells.  An attribute is a raw KC colour byte:
 * bit 7 blink, bits 6-3 foreground, bits 2-0 background.  Character codes
 * below 32 come from udg_font[], the rest from the built-in 8x8 font.
 */
extern const uint8_t udg_font[];

extern void scr_setup(void);
extern void scr_cls(uint8_t attr) __z88dk_fastcall;
/*
 * The callee-cleanup entry points are named the way z88dk names its own, and
 * wrapped so callers can spell them without the suffix.
 */
extern void scr_fill_callee(uint8_t col, uint8_t row, uint8_t len, uint8_t ch,
                            uint8_t attr) __smallc __z88dk_callee;
extern void scr_puts_callee(uint8_t col, uint8_t row, const char *s,
                            uint8_t attr) __smallc __z88dk_callee;
extern void scr_attr_callee(uint8_t col, uint8_t row, uint8_t len,
                            uint8_t attr) __smallc __z88dk_callee;
extern void scr_glyph_callee(uint8_t col, uint8_t row, const uint8_t *bitmap,
                             uint8_t attr) __smallc __z88dk_callee;

#define scr_fill(c, r, n, ch, a)  scr_fill_callee(c, r, n, ch, a)
#define scr_puts(c, r, s, a)      scr_puts_callee(c, r, s, a)
#define scr_attr(c, r, n, a)      scr_attr_callee(c, r, n, a)
#define scr_glyph(c, r, b, a)     scr_glyph_callee(c, r, b, a)

/* Foreground colours (bits 6-3). */
#define FG_BLACK    0x00
#define FG_BLUE     0x08
#define FG_RED      0x10
#define FG_MAGENTA  0x18
#define FG_GREEN    0x20
#define FG_CYAN     0x28
#define FG_YELLOW   0x30
#define FG_WHITE    0x38
#define FG_VIOLET   0x48
#define FG_ORANGE   0x50
#define FG_PURPLE   0x58
#define FG_BLUEGRN  0x60
#define FG_LEAF     0x68
#define FG_YELLGRN  0x70

/* Background colours (bits 2-0); the KC only offers the dark half here. */
#define BG_BLACK    0x00
#define BG_BLUE     0x01
#define BG_RED      0x02
#define BG_MAGENTA  0x03
#define BG_GREEN    0x04
#define BG_CYAN     0x05
#define BG_YELLOW   0x06
#define BG_WHITE    0x07

/*
 * Clock.  A byte that falls 50 times a second, so elapsed fiftieths are
 * (earlier - later) & 0xff.  It wraps every 5.1 seconds.
 */
extern uint8_t clk_ticks(void);

/*
 * Speaker.  pitch is a CTC time constant in the low byte, with bit 0 of the
 * high byte selecting the slower prescaler; 0 is silence.  volume is 0..31.
 */
extern void snd_tone_callee(uint16_t pitch,
                            uint8_t volume) __smallc __z88dk_callee;
#define snd_tone(p, v)            snd_tone_callee(p, v)
extern void snd_off(void);

#endif /* HW_H */
