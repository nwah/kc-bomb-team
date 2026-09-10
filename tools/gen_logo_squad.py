#!/usr/bin/env python3
"""Generate a SQUAD bottom row for the title logo, in place of TEAM.

Parked, not used by the build: the logo says BOMB TEAM.  This is here so
the SQUAD lettering does not have to be drawn twice if it is ever wanted.

The logo is a table of 8x8 boundary fragments (logo_cells[]) and a grid
that indexes them (logo_rows[8][19]) -- see the comments above both in
src/view.c.  BOMB is four letters four cells wide with a column between
them; SQUAD has to be five letters three cells wide to come to the same
nineteen, which makes it lighter than the word above it.  The letters are
assembled from the fragments the existing logo already has wherever they
fit, so D costs nothing new at all and the whole word costs ten fragments
-- 72 bytes once the one fragment only TEAM used is dropped.  That did not
fit under the /3's $4000 ceiling without also dropping the crt's unused
stdio streams (see the pragma in src/main.c).

Run it and paste both tables over the ones in src/view.c, which it reads
its starting vocabulary from:

    python3 tools/gen_logo_squad.py

Also update the comments there: the shapes are the B's wedge, the O's
circle, the A's diagonals, the S a backwards Z with two of its corners
taken off round by the same arcs that round the B, and the Q, U and D
bowls closed with those arcs too.  The Q's tail crosses its bowl's lower
right, which is the one fragment that is drawn rather than reused.
"""

import re

SRC = "src/view.c"

txt = open(SRC).read()
cellstxt = re.search(r"static const uint8_t logo_cells\[\] = \{(.*?)\};",
                     txt, re.S).group(1)
raw = [int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]{2})", cellstxt)]
old = [tuple(raw[i:i + 8]) for i in range(0, len(raw), 8)]

new = []


def mirror(cell):
    """A fragment flipped left to right."""
    out = []
    for byte in cell:
        r = 0
        for i in range(8):
            if byte & (0x80 >> i):
                r |= 1 << i
        out.append(r)
    return tuple(out)


def cell(bits):
    """Index of a fragment, adding it to the new ones if it is not there."""
    bits = tuple(bits)
    if bits in old:
        return old.index(bits)
    if bits not in new:
        new.append(bits)
    return len(old) + new.index(bits)


# The vocabulary already in logo_cells[], by what it draws.
BLANK, TL, TOP, ARC_TR, LV, RV, BL, BOT, ARC_BR, BR, TR, DIAG, TB, TRB = \
    0, 1, 2, 3, 10, 15, 18, 19, 20, 25, 26, 13, 27, 28

ARC_TL = cell(mirror(old[ARC_TR]))      # the B's arcs, the other way round
ARC_BL = cell(mirror(old[ARC_BR]))
TLB = cell([0xFF, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0xFF])
A_TOP = cell([0x18, 0x24, 0x42, 0x81, 0x00, 0x00, 0x00, 0x00])
A_L0 = cell([0x00, 0x00, 0x00, 0x00, 0x01, 0x02, 0x04, 0x08])
A_R0 = cell(mirror([0x00, 0x00, 0x00, 0x00, 0x01, 0x02, 0x04, 0x08]))
A_L1 = cell([0x10, 0x20, 0x40, 0x80, 0x80, 0x80, 0x80, 0x80])
A_R1 = cell(mirror([0x10, 0x20, 0x40, 0x80, 0x80, 0x80, 0x80, 0x80]))
Q_TAIL = cell([0x01, 0x01, 0x21, 0x12, 0x0A, 0x04, 0x1A, 0xE1])

LETTERS = {
    # A bar, a 45 degree stroke down to the right, a bar: a backwards Z,
    # with the top left and bottom right rounded so that it reads as an S.
    "S": [[ARC_TL, TB,     TRB],
          [DIAG,   DIAG,   BLANK],
          [BLANK,  DIAG,   DIAG],
          [TLB,    TB,     ARC_BR]],
    # The bowl, rounded at all four corners, with the tail across its foot.
    "Q": [[ARC_TL, TOP,    ARC_TR],
          [LV,     BLANK,  RV],
          [LV,     BLANK,  RV],
          [ARC_BL, BOT,    Q_TAIL]],
    # A rectangle on top, the bowl's rounded foot under it.
    "U": [[TL,     TOP,    TR],
          [LV,     BLANK,  RV],
          [LV,     BLANK,  RV],
          [ARC_BL, BOT,    ARC_BR]],
    # The wedge, as in TEAM but three cells wide.
    "A": [[A_L0,   A_TOP,  A_R0],
          [A_L1,   BLANK,  A_R1],
          [LV,     BLANK,  RV],
          [BL,     BOT,    BR]],
    # The B, with its top the mirror of its bottom.
    "D": [[TL,     TOP,    ARC_TR],
          [LV,     BLANK,  RV],
          [LV,     BLANK,  RV],
          [BL,     BOT,    ARC_BR]],
}

squad = [[0] * 19 for _ in range(4)]
for i, name in enumerate("SQUAD"):
    for r in range(4):
        for c in range(3):
            squad[r][i * 4 + c] = LETTERS[name][r][c]


def bits(i):
    if i == 0:
        return (0,) * 8
    return old[i] if i < len(old) else new[i - len(old)]


# BOMB stays as it is; everything TEAM used and SQUAD does not is dropped,
# and what is left is renumbered.
rowstxt = re.search(r"static const uint8_t logo_rows\[8\]\[19\] = \{(.*?)\};",
                    txt, re.S).group(1)
bomb = [[int(v) for v in re.findall(r"\d+", line)]
        for line in rowstxt.strip().splitlines() if "{" in line][:4]

table, remap = [(0,) * 8], {0: 0}
for row in bomb + squad:
    for i in row:
        if i not in remap:
            remap[i] = len(table)
            table.append(bits(i))
rows = [[remap[i] for i in row] for row in bomb + squad]

for r in range(4):
    for y in range(8):
        print("//  " + "".join(
            "".join("#" if bits(squad[r][c])[y] & (0x80 >> k) else "."
                    for k in range(8)) for c in range(19)))

print("\n/* %d fragments, %+d bytes against the %d TEAM needs. */"
      % (len(table), (len(table) - len(old)) * 8, len(old)))
print("static const uint8_t logo_cells[] = {")
for c in table:
    print("    " + ", ".join("0x%02X" % b for b in c) + ",")
print("};\n")
print("static const uint8_t logo_rows[8][19] = {")
for row in rows:
    print("    { " + ", ".join("%2d" % v for v in row) + " },")
print("};")
