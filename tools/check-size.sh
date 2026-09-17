#!/bin/sh
# Fail the build if a binary's code and data run into the stack.
#
# Every Z9001 has 16K of RAM, $0000-$3FFF.  The game loads at $0300 and puts
# its stack at the top, $4000, so everything the linker placed -- up to the
# end of the BSS, __tail in the map -- has to stay clear of the room the
# stack needs.
set -e
MAP=$1.map
STACK=512
tail=$(awk '$1 == "__tail" { gsub(/\$/, "", $3); print $3; exit }' "$MAP")
[ -n "$tail" ] || { echo "no __tail in $MAP" >&2; exit 1; }
end=$((0x$tail))
limit=$((0x4000 - STACK))
printf '%s: $0300-$%04X, %d bytes free below the stack\n' "$1" "$end" $((limit - end))
[ "$end" -le "$limit" ] || { echo "$1 is too big for 16K" >&2; exit 1; }
