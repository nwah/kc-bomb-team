#!/bin/sh
# Fail the build if a binary's code and data run into the stack.
#
#   tools/check-size.sh BINARY [ORG [RESERVE]]
#
# Every machine here has 16K of RAM at the bottom of the address space, up to
# $3FFF, and everything the linker placed -- up to the end of the BSS, __tail
# in the map -- has to stay below the top of it.  The Z9001 build loads at
# $0300 and puts its stack at $4000, growing down, so it has to keep RESERVE
# bytes (512, the default) clear beneath the top for it.  The KC 85 build
# loads at $0200 and its stack lives below that, so it need only end before
# $4000: pass ORG 0200 and RESERVE 0.  ORG is only used in the message.
set -e
MAP=$1.map
ORG=${2:-0300}
STACK=${3:-512}
tail=$(awk '$1 == "__tail" { gsub(/\$/, "", $3); print $3; exit }' "$MAP")
[ -n "$tail" ] || { echo "no __tail in $MAP" >&2; exit 1; }
end=$((0x$tail))
limit=$((0x4000 - STACK))
printf '%s: $%s-$%04X, %d bytes free below the %s\n' "$1" "$ORG" "$end" \
    $((limit - end)) "$([ "$STACK" -eq 0 ] && echo 'end of 16K' || echo stack)"
[ "$end" -le "$limit" ] || { echo "$1 is too big for 16K" >&2; exit 1; }
