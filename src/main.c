#include <stdint.h>
#include "hw.h"
#include "game.h"

/*
 * Where the whole thing loads, and where the CAOS menu word in hw.asm jumps
 * to when the game is started from the menu. Named here rather than left to
 * the crt's default so that the two cannot drift apart.
 */
#pragma output CRT_ORG_CODE = 0x1000

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
