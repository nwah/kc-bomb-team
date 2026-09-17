#include <stdint.h>
#include "hw.h"

/* Beep state shared with hw.asm. */
uint8_t g_reload;
uint8_t g_beeper;
uint8_t g_pitch;

/*
 * The screen is a flat 40x24 byte plane of character codes at $EC00 with a
 * parallel colour-attribute plane at $E800 (attr = char - $0400), so the two
 * march in lockstep and scr_* just walks both at once.
 */

void scr_setup(void)
{
    scr_cls(ATTR(FG_BLACK, BG_BLUE));
    snd_off();
    /* The game runs with interrupts off. MAME's z9001 natkeyboard posts keys
       straight into the BIOS buffer at $0025 regardless of the IE bit (a
       probe confirmed 'A' lands as 0x41 with EI both on and off), so there
       is no need to let the scanner run, and keeping the frame wait a pure
       busy loop is what keeps the fuse rock-steady at 50 Hz. */
}

void scr_cls(uint8_t attr)
{
    uint16_t i;
    for (i = 0; i < (uint16_t)(SCR_COLS * SCR_ROWS); i++) {
        VRAM[i] = (uint8_t)' ';
        CRAM[i] = attr;
    }
}

void scr_attr(uint8_t col, uint8_t row, uint8_t len, uint8_t attr)
{
    volatile uint8_t *c = CRAM + CELL(row, col);
    while (len--) *c++ = attr;
}

void scr_fill(uint8_t col, uint8_t row, uint8_t len, uint8_t ch, uint8_t attr)
{
    volatile uint8_t *v = VRAM + CELL(row, col);
    volatile uint8_t *c = CRAM + CELL(row, col);
    while (len--) {
        *v++ = ch;
        *c++ = attr;
    }
}

void scr_putc(uint8_t col, uint8_t row, uint8_t ch, uint8_t attr)
{
    volatile uint8_t *v = VRAM + CELL(row, col);
    volatile uint8_t *c = CRAM + CELL(row, col);
    *v = ch;
    *c = attr;
}

void scr_puts(uint8_t col, uint8_t row, const char *s, uint8_t attr)
{
    volatile uint8_t *v = VRAM + CELL(row, col);
    volatile uint8_t *c = CRAM + CELL(row, col);
    while (*s) {
        *v++ = (uint8_t)*s++;
        *c++ = attr;
    }
}

uint8_t clk_ticks(void)
{
    /* A free-running decrement so elapsed = (earlier - later) & 0xff, the way
       game.c's tick() expects the KC CTC counter to behave. It advances once
       per ~20 ms frame. */
    static uint8_t ticks = 0;
    hal_wait_frame();
    return --ticks;
}

void snd_tone(uint16_t pitch, uint8_t volume)
{
    (void)volume;
    if (pitch == 0) {
        snd_off();
        return;
    }
    g_pitch = (uint8_t)(pitch & 0x7f);
    /* Toggles per frame = 5000/pitch. The beeper flips twice per toggle, so
       the reload count that gives that many flips in one frame is
       pitch * HAL_TUNE_T / 5000. */
    g_reload = (uint8_t)(((uint32_t)pitch * HAL_TUNE_T) / 5000u);
    if (g_reload == 0) g_reload = 1;
    g_beeper = 0;            /* start low; the frame loop raises it on the beat */
    hal_beeper_off();
}

void snd_off(void)
{
    g_reload = 0;
    g_pitch = 0;
    hal_beeper_off();
}

uint8_t key_get(void)
{
    /* The BIOS keyboard scanner leaves one ASCII byte in $0025; it is not
       auto-cleared, so consume it here to deliver each press once. */
    volatile uint8_t *key = (volatile uint8_t *)0x0025u;
    uint8_t k = *key;
    if (k) *key = 0;
    return k;
}
