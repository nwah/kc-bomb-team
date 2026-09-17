# Bomb Squad — KC 85/3, KC 85/4 and the Z9001 (KC 85/1)

A port of *Bomb Squad* (Noah Burney, 2025), originally written in FastBASIC for
the Atari 8-bit as an entry in the NOMAM BASIC 10Liner contest.  One binary runs
on both the KC 85/3 and the KC 85/4; the machine is detected at startup.

There is also a standalone Z9001 (KC 85/1) build with its own `z9001/` source
tree.  Its single 40×24 text screen keeps the Atari single-screen layout the
original was written around — the bomb (status bar, countdown light and wire
panel) above and the defusal manual below — instead of splitting the two across
the KC's dual screens.  The shared game rules and bomb table live in
`common/` and are compiled into both targets unchanged.

Beep... beep... beep.  Consult the Bomb Defusal Manual, find the page that
matches the bomb on the right of the screen, and cut its wires in the right
order before the countdown runs out.

The title screen is a menu.  START begins a game, DEUTSCH puts the whole game
into German -- manual, status bar, key legend and all -- and becomes ENGLISH so
that it can be put back, and EXIT hands the machine back to CAOS.  The language
survives a restart from the CAOS menu, since it is the one setting kept in a
static with an initialiser rather than in the BSS the crt zeroes.

## Controls

| Action             | Keys                          |
|--------------------|-------------------------------|
| Choose a menu item | cursor up/down, `W` / `S`     |
| Flip the page      | cursor left/right, `A` / `D`  |
| Choose a wire      | cursor up/down, `W` / `S`     |
| Cut, or choose     | `SPACE` or `ENTER`            |

## Building

Needs [z88dk](https://z88dk.org) with the `kc` target.

    make

produces `dist/BOMB-SQUAD.KCC`, which loads at $1000 and so fits the /3's 16K
of RAM.  The KCC header carries a start address, so CAOS runs the game as soon
as it has loaded it, whether that is an emulator opening the file directly or a
`LOAD` from tape.

The game also stays in the CAOS menu across a RESET, so it can be started
again without being read back off tape.  CAOS builds its menu by scanning
memory for the pattern `$7F $7F`, a name and a `$01` terminator, and calls the
byte after the terminator when that name is chosen -- `BOMB`, here; the word sits in `hw.asm`
and jumps to the crt entry, which zeroes the BSS and resets the stack, so a
restart from the menu begins in the same state a fresh load does.  A RESET
leaves the RAM alone -- the /4's ROM reset path does not touch it at all, and
the /3's wipes memory only when the test at $E011 sends it down the cold start
path, which it does not here: that test reads `(ix+7)`, the crt keeps `ix` at
CAOS's own base of $01F0 throughout, and $01F7 is zero.  So the word is still
there to be found afterwards.

## Testing

`tools/run.sh` drives MAME headlessly, takes screenshots and can read memory
back out, so changes can be checked on both machines without a GUI:

    ./tools/run.sh -m kc85_3 -s 8 -p "300:snap"
    ./tools/run.sh -m kc85_4 -s 8 -p "300:snap"

Screenshots land in `build/snap/<machine>/`.  The `-p` plan is a comma
separated list of `<frame>:<action>` steps run against `tools/mame.lua`; 50
frames is one second.  Actions are `snap`, `quit`, `type:TEXT`,
`key:{CODE}`, `press:<port>|<field>|<frames>` (for keys the natural keyboard
cannot reach, such as the cursor keys), `peek:<hexaddr>:<len>`,
`nz:<from>:<to>`, `regs`, `pc:<hexaddr>` and `reset`.  MAME has no RESET
button for the KC and its own `soft_reset()` zeroes the RAM, which the real
button does not, so `reset` sends the CPU to the ROM's reset entry at $E000
and leaves the memory alone; that is what a resident menu entry has to
survive.  The keyboard does not come back afterwards, so `pc:` is how a menu
entry gets called in a test.  Writing `@symbol` instead of an address looks the symbol up
in the link map, so `peek:@_G:13` keeps watching the game state across
rebuilds that move it.  `python3 tools/analyze.py <png> <first row> <last row>`
prints a per-cell colour map of a screenshot, which is easier to check layout
against than the image itself.

MAME needs KC 85 ROMs.  `tools/setup-roms.sh` builds the two romsets out of the
images already bundled with JKCEMU (nothing copyrighted is stored in this
repository); it runs automatically the first time `tools/run.sh` needs it.
Point `JKCEMU_JAR` at your `jkcemu.jar` if it is not at `~/Retro/KC/jkcemu/`.

## How it works

`src/hw.asm` is the whole hardware layer and the only place either machine's
quirks appear.  The z88dk console is far too slow for a game — several
milliseconds per character — so the screen routines write the IRM directly.

The two machines organise that memory quite differently:

* On the **/4** the pixel and colour planes live at the same addresses in two
  banks swapped through port `$84`, a whole character column is eight
  consecutive bytes, and colour resolution is 8x1.
* On the **/3** both planes are separate regions of one 16K window, the
  scanlines of a character cell are scattered (`+$00 +$80 +$100 +$180`, then
  the same again from `+$20`), the leftmost 32 columns and the rightmost 8 are
  addressed by different formulas, and colour resolution is 8x4 — two colour
  bytes per cell.

`scr_setup()` tells them apart by asking CAOS (`FNPADR`) for the address of a
pixel one column in: only the /4 answers `$8100`.

Everything else in the layer is likewise checked against the hardware rather
than assumed:

* **Clock.** CTC channel 2 is fed a 50 Hz signal, so programming it as a
  counter with no interrupt turns `IN A,($8E)` into a free running clock.
  Measured against the emulator's frame counter it is exactly 1:1.
* **Sound.** CTC channels 0 and 1 drive the speaker and PIO port B bits 0-4 set
  the volume, active low — the same sequence CAOS's own `TON` uses, minus its
  duration timer.  The pitch works out at `f = 1750000 / (2 * prescaler * N)`,
  measured at 219 Hz for a time constant of 250 on the /16 prescaler.
* **Keyboard.** CAOS function `$0E` (query *with* acknowledgement) delivers each
  press once; `$0C` repeats the same key for as long as it is held.

Colours are raw KC attribute bytes: bit 7 blink, bits 6-3 one of sixteen
foreground colours, bits 2-0 one of eight darker backgrounds.  Hardware
blinking is switched off so the game can flash the countdown light itself.

## Layout of the source

| File | |
|---|---|
| `src/hw.asm`, `src/hw.h` | screen, clock and speaker; the only machine-specific code |
| `src/udg.c`, `src/udg.h` | the game's own 8x8 glyphs |
| `src/bombs.c`, `src/bombs.h` | the 48 bombs, generated by `tools/gen_bombs.py` from the packed table in the Atari original |
| `src/view.c`, `src/view.h` | everything that draws or makes a noise, and the English and German text |
| `src/game.c`, `src/game.h` | the state machine and the rules |
| `src/main.c` | startup |

The original annotated FastBASIC source is in
`~/Atari/10Liner/Bomb-Squad-Noah-Burney-2025/`; the bomb table and the
timing and scoring rules are carried over from it unchanged.

## Z9001 (KC 85/1)

A second, independent build lives in `z9001/` and shares `common/` with the KC
target.  `make z9001` produces `dist/bomb-squad-z9001`, a 8836-byte flat binary
loaded at `$0300` with the stack at `$4000`; `./tools/check-size.sh` confirms it
fits the Z9001's 16K of RAM (it leaves 6268 bytes free below the stack).  `MENU_EXIT`
restarts the machine, so the game cleanly hands the Z9001 straight back to BASIC.

### Layout

The Z9001 text mode is 40×24 cells of 8×8 pixels from a 256-glyph character
ROM: char RAM at `$EC00` and colour RAM at `$E800` (colour address = char address
− `$0400`), so the whole screen is written directly, never through the z88dk
console (far too slow).  The game fits the Atari original's single-screen layout
onto one KC screen — bomb panel above, defusal manual below — which is why it
needs its own `view.c`/`view.h` rather than reusing the KC split-screen one.

### Controls

| Action             | Keys                          |
|--------------------|-------------------------------|
| Choose a menu item | cursor up/down, `W` / `S`     |
| Flip the page      | cursor left/right, `A` / `D`  |
| Choose a wire      | cursor up/down, `W` / `S`     |
| Cut, or choose     | `SPACE` or `ENTER`            |

The keyboard is polled by reading `$0025` directly, so the game runs
interrupt-free and needs no CTC interrupt setup.  Sound is a busy-wait around a
2.4576 MHz cycle-accurate loop: a silent path ticks the 50 Hz clock, a tone path
toggles the beeper at port `$88` bit 7 in phase with CTC channel 0.

### Testing

The Z9001 has no tape/quickload device, so `tools/run.sh` injects the flat binary
into RAM with MAME's `load:`/`pc:` actions and steps a plan from
`tools/mame.lua`:

    ./tools/run.sh -m z9001 -s 8 -p "30:snap,100:type: ,300:snap"

A five-stage screenshot set (title, live bomb, cut wire, boom and defused) is
captured in `build/snap/z9001/`.  `peek:<addr>:<len>` reads memory back out for
assertions, e.g. `peek:EC00:16` for the status row.

### Source layout (Z9001)

| File | |
|---|---|
| `z9001/hw.h`, `z9001/hw.c`, `z9001/hw.asm` | scr_*, clk_ticks, snd_*, key_get; 50 Hz busy-wait and beeper toggle are in `hw.asm`, the C wrappers in `hw.c` |
| `z9001/view.h`, `z9001/view.c` | the Atari single-screen renderers and the English/German text |
| `z9001/main.c` | `#pragma output CRT_ORG_CODE 0x0300`; sets up the screen and runs the shared game |
| `common/bombs.c`, `common/game.c` | the 48 bombs (generated by `tools/gen_bombs.py`) and the state machine -- identical for both machines |
