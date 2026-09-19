#include <stdint.h>
#include "hw.h"
#include "game.h"

/*
 * Where the whole thing loads, and where the CAOS menu word in hw.asm jumps
 * to when the game is started from the menu. Named here rather than left to
 * the crt's default so that the two cannot drift apart.
 *
 * $0200 is the first byte of user RAM, where KC 85 software conventionally
 * loads; the CAOS menu word jumps to it, and a RESET keeps the game in the
 * menu.  The stack lives below it and never gets more than about eighty
 * bytes deep.
 */
#pragma output CRT_ORG_CODE = 0x0200

/*
 * The game never uses stdio -- every character on screen goes through
 * hw.asm, because the z88dk console is far too slow for it -- so the
 * streams the crt would otherwise set up are so much dead weight, and on
 * the /3 dead weight is space the binary cannot spare: it has to end before
 * $4000.
 */
#pragma output nostreams

/*
 * Non-blocking read of the CAOS keyboard buffer; 0 when nothing is
 * waiting. CAOS wants its own base register in iy, which the assembler
 * turns into ix because zcc compiles with -mz80_ixiy.
 *
 * Uses CAOS function 0x0E (KBDZ, "query with acknowledgement") rather than
 * 0x0C: 0x0E consumes the key so each press is delivered exactly once,
 * with CAOS's own typematic repeat giving hold-to-repeat for free. 0x0C
 * would hand back the same key over and over for as long as it is held.
 */
uint8_t key_get(void)
{
#asm
    push    iy
    ld      iy,$01f0
    call    $f003
    defb    $0e
    pop     iy
    ld      h,0
    ld      l,0
    ret     nc
    ld      l,a
#endasm
}

void main(void)
{
    scr_setup();
    game_run();
}
