;
;       Bomb Squad - Z9001 / KC 85/1.10 low level layer
;
;       Hardware touched here:
;
;         - the beeper, toggled through PIO1 port A bit 7 (port $88) inside a
;           cycle-exact 50 Hz frame, so the fuse still burns true while a note
;           plays,
;         - a cycle-exact 20 ms frame wait that is the whole game clock,
;         - the screen lives in C (hw.c); it just pokes char RAM $EC00 and
;           colour RAM $E800 directly.
;
;       No interrupts are used. The Z9001's only ticking source (BOS's 1 Hz
;       second tick, driven by the CTC chain) is too coarse for a clock, and a
;       read of the CTC returns its interrupt-vector register, not a counter,
;       so there is nothing to sample. The frame wait is busy-wait on purpose:
;       it is the clock.
;
;       zcc compiles with -mz80_ixiy; no index register is named here.

                INCLUDE "zcc_opt.def"

BEEPER          equ 0x88

; Tuned with tools/probe against MAME's 50.00 Hz frame (see probe runs).
; One 50 Hz frame is 49152 T-states at 2.4576 MHz.
;
; Silent path: a bare 16-bit decrement loop, no beeper work -- this is what
;   clk_ticks() runs for the fuse, so it has to be right on the nose.
; Tone path:   the same shape but with a beeper countdown folded in, so the
;   note keeps flipping while a sound is playing. A few percent slower under
;   a note, which is fine -- the fuse counts calls, not wall time.
T_B_NT          equ 1610
T_B_TONE        equ 1130

                SECTION code_user
                PUBLIC  _hal_wait_frame
                PUBLIC  _hal_beeper_off
                ; Beep state lives in hw.c; hw.z80 only reads (reload) and
                ; writes (beeper level) it.
                EXTERN  _g_reload
                EXTERN  _g_beeper

; void hal_wait_frame(void)
;
;   One 20 ms tick. With no note playing the loop is pure delay; with a note
;   (g_reload != 0) the beeper is flipped when the reload counter under-runs,
;   restarting it, so the note's pitch tracks g_reload without a second timer.
_hal_wait_frame:
                ld a,(_g_reload)
                or a
                jr nz,hal_tone

                ; --- silent: 16-bit busy wait, tuned to 50 Hz ---
                ld bc,T_B_NT
hal_silent:     dec bc
                ld a,b
                or c
                jr nz,hal_silent
                ret

                ; --- tone: 16-bit busy wait + beeper countdown ---
hal_tone:       ld e,a                ; e = reload
                ld bc,T_B_TONE
hal_tloop:      dec e
                jr nz,hal_no_toggle
                ld a,(_g_reload)      ; reload count
                ld e,a
                ld a,(_g_beeper)
                xor 0x80              ; flip bit 7
                out (BEEPER),a
                ld (_g_beeper),a
hal_no_toggle:  dec bc
                ld a,b
                or c
                jr nz,hal_tloop
                ret

; void hal_beeper_off(void)
;
;   Force the speaker quiet now. The frame loop only drives the beeper while a
;   note is playing, so between notes it holds its last level.
_hal_beeper_off:
                xor a
                out (BEEPER),a
                ret
