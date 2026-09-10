#include <stdint.h>
#include "udg.h"

/*
 * The game's own 8x8 glyphs.  Codes below 32 are looked up here by the screen
 * routines in hw.asm; everything from 32 up comes from the built-in font.
 */
const uint8_t udg_font[] = {
    /* G_BLANK */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    /* G_SOLID */
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    /* G_STRIPE - 45 degree hatch on an eight pixel period, so it tiles
       seamlessly both across a wire and down the manual's swatches.  Two
       pixels of every eight, which leaves the wire's own colour dominant;
       a wider stripe washes the colour out and the colour is what the
       player has to match against the manual. */
    0xC0, 0x60, 0x30, 0x18, 0x0C, 0x06, 0x03, 0x81,
    /* G_SCISSORS - open, pointing right at the wire, after the sprite in the
       Atari original */
    0x63, 0x35, 0x1F, 0x08, 0x1F, 0x35, 0x63, 0x00,
    /* G_SCISSORS_SHUT */
    0x00, 0x03, 0x35, 0xFE, 0xFE, 0x35, 0x03, 0x00,
    /* G_RULE - a rule across the middle of the cell */
    0x00, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00, 0x00,
    /* G_LEFT */
    0x00, 0x10, 0x30, 0x7F, 0x30, 0x10, 0x00, 0x00,
    /* G_RIGHT */
    0x00, 0x08, 0x0C, 0xFE, 0x0C, 0x08, 0x00, 0x00,
    /* G_UP */
    0x00, 0x18, 0x3C, 0x7E, 0x18, 0x18, 0x18, 0x00,
    /* G_DOWN */
    0x00, 0x18, 0x18, 0x18, 0x7E, 0x3C, 0x18, 0x00,
    /* G_CUT_L - the frayed left end of a cut wire */
    0xF8, 0xFC, 0xFE, 0xFE, 0xFC, 0xF8, 0xF0, 0xE0,
    /* G_CUT_R - and its right end */
    0x1F, 0x3F, 0x7F, 0x7F, 0x3F, 0x1F, 0x0F, 0x07,
    /* G_PAGE_V - the block of pages behind the cover, seen down the book's
       fore-edge: a paper ground ruled with three black lines. The pages
       stack across the book's thickness, so their edges read as lines
       running the length of that strip. A dither gives the same average
       tone, but noise reads as texture and only lines read as leaves of
       paper. Drawn FG_BLACK on BG_WHITE, so a set pixel is the gap between
       two pages and a clear one is the paper itself.
       There is no matching glyph for the foot of the book, because the book
       is drawn hanging off the bottom of the screen and its foot is never
       in view. */
    0x49, 0x49, 0x49, 0x49, 0x49, 0x49, 0x49, 0x49,
    /* G_PAGE_TR - the same, mitred at 45 degrees where the block tapers
       away at the top of the fore-edge: paper inside the cut, solid black
       outside it, so the corner reads against the background. */
    0xFF, 0x7F, 0x7F, 0x5F, 0x4F, 0x4F, 0x4B, 0x49,
    /* G_TNT_L, G_TNT_M, G_TNT_R - the three columns of a stick of dynamite
       standing on end, as a vertical tube, lit from the left.
       The KC's two reds are almost the same colour -- $d00000 and $a00000 --
       so a tube dithered from one into the other has no tonal range at all
       and comes out flat. The range has to come from the three columns
       carrying different attributes instead: G_TNT_L is drawn FG_ORANGE on
       BG_RED, so its set pixels are the highlight running down the tube's
       lit edge, while G_TNT_M and G_TNT_R are drawn FG_BLACK on BG_RED, so
       their set pixels are the shadow deepening towards the right. Read
       across, that is dark red, orange, dark red, then black, which is
       enough of a ramp to look like a cylinder.
       The shading runs across the tube, so it is dithered down the rows
       instead: each pixel column holds its tone as a duty cycle over the
       eight rows, staggered column to column so the mid tones do not fall
       into horizontal bands. Every cell down the stick can therefore use
       the same glyph, and the flat ends need no caps.
       G_TNT_R's shadow stops at five pixels in eight rather than running all
       the way to black. The stick stands against a black background, so a
       shadow any deeper than that takes its right-hand edge with it and the
       tube comes out looking two columns wide instead of three. */
    0x3A, 0x5C, 0x3E, 0x3D, 0x38, 0x5C, 0x3E, 0x1C,
    0x0A, 0x04, 0x22, 0x01, 0x08, 0x05, 0x02, 0x11,
    0xAA, 0x55, 0xAE, 0x55, 0xAB, 0x5D, 0x2A, 0x57,
    /* G_LINE_V, G_CORNER_TL, G_CORNER_TR, G_CORNER_BL, G_CORNER_BR - the
       rest of a rectangle outline; G_RULE is already its horizontal side.
       The bomb is drawn as flat black now rather than as a lit moulding, so
       its edges have nothing to catch the light and have to be stated as a
       line instead. Both lines run down the middle of their cell -- the
       horizontal on pixel row 3, the vertical on pixel column 3 -- and each
       corner carries the two stubs that meet there, so the four join up
       without a break whatever size the rectangle is. */
    0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10,
    0x00, 0x00, 0x00, 0x1F, 0x10, 0x10, 0x10, 0x10,
    0x00, 0x00, 0x00, 0xF0, 0x10, 0x10, 0x10, 0x10,
    0x10, 0x10, 0x10, 0x1F, 0x00, 0x00, 0x00, 0x00,
    0x10, 0x10, 0x10, 0xF0, 0x00, 0x00, 0x00, 0x00,
    /* G_BUTTON - one blank keypad button, a hollow square inset a pixel all
       round so a row of them reads as separate keys rather than as a grid,
       even where two sit side by side. */
    0x00, 0x7E, 0x42, 0x42, 0x42, 0x42, 0x7E, 0x00,
    /* G_LAMP - the countdown lamp, a filled disc the same six pixels across
       as G_BUTTON's square. Solid rather than hollow is what keeps it a
       lamp and not another key. */
    0x00, 0x3C, 0x7E, 0x7E, 0x7E, 0x7E, 0x3C, 0x00,
    /* G_FUSE_A, G_FUSE_B, G_FUSE_C, G_FUSE_J, G_FUSE_D - one sag of the
       fuse looped between the feet of the sticks, cut into cells. The cord
       hangs from the centre of one stick to the centre of the next rather
       than from edge to edge, which is where a fuse would actually be tied,
       and a stick's centre falls in the middle of a cell -- so a sag starts
       and ends on a half cell and needs five glyphs rather than three.
       A begins one at a half cell, B and C carry it across the two whole
       cells between, and D ends one. J is D and A in the same cell: at
       every centre but the first and the last, one sag ends exactly where
       the next begins. The curve is a parabola seven pixels deep, which is
       as far as it can fall and still be one cell tall. */
    0x08, 0x04, 0x02, 0x01, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x80, 0x60, 0x18, 0x07,
    0x00, 0x00, 0x00, 0x00, 0x01, 0x06, 0x18, 0xE0,
    0x18, 0x24, 0x42, 0x81, 0x00, 0x00, 0x00, 0x00,
    0x10, 0x20, 0x40, 0x80, 0x00, 0x00, 0x00, 0x00,
};
