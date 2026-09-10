;
;       Bomb Squad - KC 85/3 and KC 85/4 low level layer
;
;       Everything in here talks to the hardware directly:
;
;         - the screen, because the z88dk console needs several milliseconds
;           per character, far too slow to redraw a page of the manual,
;         - CTC channel 2, run as a free counter on the machine's 50 Hz line
;           to give the game a clock,
;         - CTC channels 0 and 1 plus PIO port B for the speaker, programmed
;           the same way the CAOS TON routine does it.
;
;       The two machines lay the IRM out differently, so each screen routine
;       has a /3 and a /4 path chosen from _kc85_4.  On the /4 the pixel and
;       colour planes share addresses in two banks swapped through port $84;
;       on the /3 they are separate regions of one 16K window and the colour
;       resolution is 8x4, i.e. two colour bytes per character cell.
;
;       NB. zcc passes -mz80_ixiy, so the assembler swaps ix and iy.  Neither
;       is named here: the CAOS port shadow cell is addressed absolutely and
;       nothing else needs an index register.
;

                INCLUDE "target/kc/def/caos.def"
                INCLUDE "zcc_opt.def"           ; for CRT_ORG_CODE

; CAOS keeps shadow copies of the write-only ports in the block at $01F0.
defc    SHADOW_84 = $01F1       ; KC85/4 only: pixel/colour bank select

defc    CTC0  = $8C             ; speaker, channel 1
defc    CTC1  = $8D             ; speaker, channel 2
defc    CTC2  = $8E             ; clocked at 50 Hz, used here as the game clock
defc    PIO_A = $88             ; bit 2 pages the IRM in
defc    PIO_B = $89             ; bits 0-4 volume (inverted), 5-6 RAM8, 7 blink

defc    IRM_PIXELS = $8000      ; 320x256 pixels, both machines
defc    IRM_PIXEL_LEN = $2800
defc    IRM_COLOUR_23 = $A800   ; /3 only; the /4 mirrors the pixel plane
defc    IRM_COLOUR_23_LEN = $0A00

                SECTION code_user

                PUBLIC  _scr_setup
                PUBLIC  _scr_cls
                PUBLIC  _scr_fill_callee
                PUBLIC  _scr_puts_callee
                PUBLIC  _scr_attr_callee
                PUBLIC  _scr_glyph_callee
                PUBLIC  _clk_ticks
                PUBLIC  _snd_tone_callee
                PUBLIC  _snd_off
                PUBLIC  _kc85_4

                EXTERN  _font_8x8_zx_system
                EXTERN  _udg_font

; ---------------------------------------------------------------------------
; CAOS menu word
;
; CAOS builds its menu by scanning memory for the pattern $7F $7F followed by
; a name and a $01 terminator, and calls the byte after the terminator when
; that name is chosen.  A RESET clears the system area at $0000-$01FF and the
; screen but leaves the rest of the RAM alone, so a word anywhere in the
; loaded image keeps the game in the menu afterwards: it can be started again
; without being read back off tape.
;
; The word points at the crt entry rather than at main(), so a restart from
; the menu zeroes the BSS, resets the stack and re-saves the CAOS interrupt
; vectors exactly as a fresh load does.  Only statics with an initialiser
; survive, and view_init() puts those back itself.
; ---------------------------------------------------------------------------
menu_word:
                defb    $7F,$7F
                defm    "BOMB"
                defb    $01
                jp      CRT_ORG_CODE

; ---------------------------------------------------------------------------
; void scr_setup(void)
;
; Page the IRM in, work out which machine we are on, silence the speaker and
; start the clock.
; ---------------------------------------------------------------------------
_scr_setup:
                in      a,(PIO_A)       ; IRM visible at $8000
                set     2,a
                out     (PIO_A),a

                ; FNPADR maps a screen position (l = byte column, h = pixel
                ; row) to its IRM address.  Both machines put the top left at
                ; $8000, but only the /4 keeps a whole column together, so
                ; column 1 answers $8100 there and $8001 on a /2 or /3.
                ld      hl,$0001
                call    PV1
                defb    FNPADR
                ld      a,h
                cp      $81
                ld      a,0
                jr      nz,setup_type
                inc     a
setup_type:
                ld      (_kc85_4),a
                or      a
                jr      z,setup_sound
                ld      a,(SHADOW_84)   ; display and edit image 0, no hicolor,
                and     $F0             ; CPU on the pixel bank
                or      $08
                ld      (SHADOW_84),a
                out     ($84),a
setup_sound:

                call    _snd_off

                ; CTC 2 as a counter on the 50 Hz input with no interrupt, so
                ; that reading the channel gives a byte falling 50 times a
                ; second.  CAOS only uses this channel to time TON, which we
                ; do not call.
                ld      a,$47
                out     (CTC2),a
                xor     a               ; time constant 0 means 256
                out     (CTC2),a
                ret

; ---------------------------------------------------------------------------
; uint8_t clk_ticks(void)
;
; The CTC counts down, so elapsed 50ths are (previous - current) & 255.  That
; wraps after 5.1 seconds, comfortably longer than anything we wait for.
; ---------------------------------------------------------------------------
_clk_ticks:
                in      a,(CTC2)
                ld      l,a
                ld      h,0
                ret

; ---------------------------------------------------------------------------
; void snd_tone(uint16_t pitch, uint8_t volume)
;
; pitch is a CTC time constant in the low byte with bit 0 of the high byte
; selecting the /256 prescaler; zero silences the channel.  volume runs 0..31.
; Both speaker channels get the same note.  Modelled on CAOS TON, minus the
; duration timer - the game times its own sounds.
; ---------------------------------------------------------------------------
_snd_tone_callee:
                pop     hl              ; return address
                pop     bc              ; volume
                ex      (sp),hl         ; pitch, leaving the return address
                push    bc
                push    hl
                ld      c,CTC0
                call    tone_channel
                pop     hl
                ld      c,CTC1
                call    tone_channel
                pop     bc              ; volume back
set_volume:
                ld      a,c
                and     $1F
                xor     $1F             ; the volume bits are active low
                or      $01             ; bit 7 left clear: no hardware blink
                ld      c,a
                in      a,(PIO_B)
                and     $60             ; leave the RAM8 control bits alone
                or      c
                out     (PIO_B),a
                ret

_snd_off:
                ld      hl,0
                ld      c,CTC0
                call    tone_channel
                ld      hl,0
                ld      c,CTC1
                call    tone_channel
                ld      c,0
                jr      set_volume

; Program the CTC channel in c from hl.  Preserves bc.
tone_channel:
                ld      a,l
                and     a
                ld      l,3             ; reset and stop, no time constant
                jr      z,tone_write
                ld      l,a
                ld      a,$07           ; timer, /16, time constant follows
                bit     0,h
                jr      z,tone_control
                or      $20             ; /256 instead
tone_control:
                out     (c),a
tone_write:
                out     (c),l
                ret

; ---------------------------------------------------------------------------
; Address arithmetic.  Reads the cell from cur_col / cur_row, returns hl.
; Clobbers af and bc only.
; ---------------------------------------------------------------------------
pix_addr:
                ld      a,(_kc85_4)
                or      a
                ld      a,(cur_col)
                jr      z,pix_addr_23
                add     $80             ; /4: the column picks the page and
                ld      h,a             ; the cell is eight bytes in a row
                ld      a,(cur_row)
                add     a
                add     a
                add     a
                ld      l,a
                ret
pix_addr_23:
                sub     32
                jr      nc,pix_addr_rhs
                ld      c,a             ; c = column - 32, i.e. col | $E0
                ld      a,(cur_row)     ; left 32 columns: $8000 + (row & ~1)*256
                and     $FE
                add     $80
                ld      h,a
                ld      a,(cur_row)
                rrca                    ; odd rows sit $40 further in
                rrca
                and     $40
                add     c
                add     32
                ld      l,a
                ret
pix_addr_rhs:
                ld      c,a             ; right 8 columns, based at $A000
                ld      a,(cur_row)
                rrca
                rrca
                and     %00000110
                add     $A0
                ld      h,a
                ld      a,(cur_row)
                rlca
                rlca
                and     %00011000
                ld      b,a
                ld      a,(cur_row)
                rrca
                rrca
                and     $40
                add     b
                add     c
                ld      l,a
                ret

; Colour plane; the /4 shares the pixel addresses in the other bank.
col_addr:
                ld      a,(_kc85_4)
                or      a
                jr      nz,pix_addr
                ld      a,(cur_col)
                sub     32
                jr      nc,col_addr_rhs
                ld      c,a
                ld      a,(cur_row)     ; left columns: $A800 + row*64
                rrca
                rrca
                ld      b,a
                and     $3F
                add     $A8
                ld      h,a
                ld      a,b
                and     $C0
                add     c
                add     32
                ld      l,a
                ret
col_addr_rhs:
                ld      c,a             ; right columns, based at $B000
                ld      a,(cur_row)
                rrca
                rrca
                rrca
                and     $03
                rrca                    ; row/8 in bit 0, its low bit in bit 7
                ld      b,a
                and     $01
                add     $B0
                ld      h,a
                ld      a,b
                and     $80
                ld      b,a
                ld      a,(cur_row)
                rlca
                rlca
                and     %00011000
                add     b
                ld      b,a
                ld      a,(cur_row)
                rrca
                rrca
                and     $40
                add     b
                add     c
                ld      l,a
                ret

; ---------------------------------------------------------------------------
; Bank selection.  Both return immediately on a /3.
; ---------------------------------------------------------------------------
bank_pixel:
                ld      a,(_kc85_4)
                or      a
                ret     z
                ld      a,(SHADOW_84)
                res     1,a
                jr      bank_out
bank_colour:
                ld      a,(_kc85_4)
                or      a
                ret     z
                ld      a,(SHADOW_84)
                set     1,a
bank_out:
                ld      (SHADOW_84),a
                out     ($84),a
                ret

; ---------------------------------------------------------------------------
; Draw one cell.  hl = cell address, de = eight bytes of bitmap; de is left
; pointing just past them.  Clobbers af, b, hl.
; ---------------------------------------------------------------------------
put_cell:
                ld      a,(_kc85_4)
                or      a
                jr      z,put_cell_23
                ld      b,8
put_cell_4:
                ld      a,(de)
                ld      (hl),a
                inc     de
                inc     l
                djnz    put_cell_4
                ret
put_cell_23:
                ; Scanlines 0-3 step $80 from the cell address; scanlines 4-7
                ; step $80 from the cell address plus $20.
                push    hl
                call    put_cell_23_half
                pop     hl
                ld      a,l
                add     $20
                ld      l,a
                jr      nc,put_cell_23_half
                inc     h
put_cell_23_half:
                ld      b,4
put_cell_23_line:
                ld      a,(de)
                ld      (hl),a
                inc     de
                ld      a,l
                add     $80
                ld      l,a
                jr      nc,put_cell_23_next
                inc     h
put_cell_23_next:
                djnz    put_cell_23_line
                ret

; ---------------------------------------------------------------------------
; Colour one cell.  hl = colour address, attribute from cur_attr - it cannot
; travel in a register because col_addr needs bc.  Clobbers af, bc, hl.
; ---------------------------------------------------------------------------
put_attr:
                ld      a,(cur_attr)
                ld      c,a
                ld      a,(_kc85_4)
                or      a
                ld      a,c
                jr      z,put_attr_23
                ld      b,8             ; the /4 colours each scanline
put_attr_4:
                ld      (hl),a
                inc     l
                djnz    put_attr_4
                ret
put_attr_23:
                ld      (hl),a          ; the /3 colours 8x4 blocks
                ld      a,l
                add     $20
                ld      l,a
                jr      nc,put_attr_23b
                inc     h
put_attr_23b:
                ld      (hl),c
                ret

; ---------------------------------------------------------------------------
; Bitmap for the character in a.  Codes below 32 come from the game's own
; glyphs, the rest from the ROM-style 8x8 font which starts at code 32.
; Returns the address in de.
; ---------------------------------------------------------------------------
char_bitmap:
                ld      l,a
                ld      h,0
                cp      32
                jr      nc,char_bitmap_font
                add     hl,hl
                add     hl,hl
                add     hl,hl
                ld      de,_udg_font
                add     hl,de
                ex      de,hl
                ret
char_bitmap_font:
                ld      a,l
                sub     32
                ld      l,a
                add     hl,hl
                add     hl,hl
                add     hl,hl
                ld      de,_font_8x8_zx_system
                add     hl,de
                ex      de,hl
                ret

; ---------------------------------------------------------------------------
; void scr_fill(uint8_t col, uint8_t row, uint8_t len, uint8_t ch,
;               uint8_t attr)
;
; Repeat one character across a run of cells.
; ---------------------------------------------------------------------------
_scr_fill_callee:
                pop     hl              ; return address
                pop     bc              ; attribute
                ld      a,c
                ld      (cur_attr),a
                pop     bc              ; character
                ld      a,c
                ld      (cur_char),a
                pop     bc              ; length
                ld      a,c
                ld      (cur_len),a
                pop     bc              ; row
                ld      a,c
                ld      (cur_row),a
                ex      (sp),hl         ; column, leaving the return address
                ld      a,l
                ld      (cur_col),a
                ld      (cur_col0),a

                ld      a,(cur_len)
                and     a
                ret     z

                call    bank_pixel
                ld      a,(cur_char)    ; one character, so one bitmap lookup
                call    char_bitmap
                ld      (cur_src),de
                ld      a,(cur_len)
                ld      b,a
fill_pixels:
                push    bc
                ld      de,(cur_src)
                call    pix_addr
                call    put_cell
                ld      hl,cur_col
                inc     (hl)
                pop     bc
                djnz    fill_pixels

                ld      a,(cur_col0)
                ld      (cur_col),a
                call    bank_colour
                ld      a,(cur_len)
                ld      b,a
fill_attrs:
                push    bc
                call    col_addr
                call    put_attr
                ld      hl,cur_col
                inc     (hl)
                pop     bc
                djnz    fill_attrs
                jp      bank_pixel

; ---------------------------------------------------------------------------
; void scr_puts(uint8_t col, uint8_t row, const char *s, uint8_t attr)
; ---------------------------------------------------------------------------
_scr_puts_callee:
                pop     hl              ; return address
                pop     bc              ; attribute
                ld      a,c
                ld      (cur_attr),a
                pop     bc              ; string
                ld      (cur_src),bc
                pop     bc              ; row
                ld      a,c
                ld      (cur_row),a
                ex      (sp),hl         ; column
                ld      a,l
                ld      (cur_col),a
                ld      (cur_col0),a

                ; Count the string so the colour pass knows how far to go.
                ld      hl,(cur_src)
                ld      b,0
puts_count:
                ld      a,(hl)
                and     a
                jr      z,puts_counted
                inc     hl
                inc     b
                jr      puts_count
puts_counted:
                ld      a,b
                ld      (cur_len),a
                and     a
                ret     z

                call    bank_pixel
                ld      a,(cur_len)
                ld      b,a
puts_pixels:
                push    bc
                ld      hl,(cur_src)
                ld      a,(hl)
                inc     hl
                ld      (cur_src),hl
                call    char_bitmap
                call    pix_addr
                call    put_cell
                ld      hl,cur_col
                inc     (hl)
                pop     bc
                djnz    puts_pixels

                ld      a,(cur_col0)
                ld      (cur_col),a
                call    bank_colour
                ld      a,(cur_len)
                ld      b,a
puts_attrs:
                push    bc
                call    col_addr
                call    put_attr
                ld      hl,cur_col
                inc     (hl)
                pop     bc
                djnz    puts_attrs
                jp      bank_pixel

; ---------------------------------------------------------------------------
; void scr_attr(uint8_t col, uint8_t row, uint8_t len, uint8_t attr)
;
; Recolour a run of cells without touching the pixels.
; ---------------------------------------------------------------------------
_scr_attr_callee:
                pop     hl              ; return address
                pop     bc              ; attribute
                ld      a,c
                ld      (cur_attr),a
                pop     bc              ; length
                ld      a,c
                ld      (cur_len),a
                pop     bc              ; row
                ld      a,c
                ld      (cur_row),a
                ex      (sp),hl         ; column
                ld      a,l
                ld      (cur_col),a

                ld      a,(cur_len)
                and     a
                ret     z
                ld      b,a
                call    bank_colour
attr_loop:
                push    bc
                call    col_addr
                call    put_attr
                ld      hl,cur_col
                inc     (hl)
                pop     bc
                djnz    attr_loop
                jp      bank_pixel

; ---------------------------------------------------------------------------
; void scr_glyph(uint8_t col, uint8_t row, const uint8_t *bitmap,
;                uint8_t attr)
;
; One cell straight from an eight byte bitmap, for the animated sprites.
; ---------------------------------------------------------------------------
_scr_glyph_callee:
                pop     hl              ; return address
                pop     bc              ; attribute
                ld      a,c
                ld      (cur_attr),a
                pop     bc              ; bitmap
                ld      (cur_src),bc
                pop     bc              ; row
                ld      a,c
                ld      (cur_row),a
                ex      (sp),hl         ; column
                ld      a,l
                ld      (cur_col),a

                call    bank_pixel
                ld      de,(cur_src)
                call    pix_addr
                call    put_cell
                call    bank_colour
                call    col_addr
                call    put_attr
                jp      bank_pixel

; ---------------------------------------------------------------------------
; void scr_cls(uint8_t attr)
;
; Blank the whole screen.  The pixel plane covers the same addresses on both
; machines; only the colour plane moves.
; ---------------------------------------------------------------------------
_scr_cls:
                ld      a,l
                ld      (cur_attr),a
                call    bank_pixel
                ld      hl,IRM_PIXELS
                ld      bc,IRM_PIXEL_LEN
                xor     a
                call    block_fill
                call    bank_colour
                ld      a,(_kc85_4)
                or      a
                ld      hl,IRM_PIXELS
                ld      bc,IRM_PIXEL_LEN
                jr      nz,cls_colour
                ld      hl,IRM_COLOUR_23
                ld      bc,IRM_COLOUR_23_LEN
cls_colour:
                ld      a,(cur_attr)
                call    block_fill
                jp      bank_pixel

; hl = start, bc = length, a = byte.
block_fill:
                ld      (hl),a
                ld      d,h
                ld      e,l
                inc     de
                dec     bc
                ldir
                ret

                SECTION bss_user

_kc85_4:        defb    0               ; non-zero on a KC 85/4 or /5
cur_col:        defb    0
cur_col0:       defb    0
cur_row:        defb    0
cur_len:        defb    0
cur_char:       defb    0
cur_attr:       defb    0
cur_src:        defw    0
