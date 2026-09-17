#include <stdint.h>
#include "hw.h"
#include "game.h"

/*
 * Where the whole thing loads, and where it begins: $0300. Below that is the
 * Z9001's zero page / system workspace ($0000-$00FF) and the BIOS keyboard
 * buffer at $0025 that key_get() reads, all of which the game must keep
 * untouched. $0300 is also the crt's load address, so the binary that comes
 * out of zcc drops straight into RAM at $0300 ready for MAME to inject.
 *
 * $4000 is the top of the 16 K the Z9001 has fitted, so the stack grows down
 * from there; check-size.sh makes sure the code stops at least 512 bytes
 * below it (__tail <= $3E00), leaving the ISR a clean runway.
 */
#pragma output CRT_ORG_CODE = 0x0300
#pragma output nostreams

/*
 * The game never uses stdio -- every character on screen goes through hw.c,
 * because the z88dk console is far too slow for it -- so the streams the crt
 * would otherwise set up are so much dead weight, and on a 16 K machine
 * there is room for none of it.
 */

void main(void)
{
    scr_setup();
    game_run();
}
