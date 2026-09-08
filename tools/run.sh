#!/bin/sh
# Run a .KCC in MAME headless and drop screenshots into build/snap/<machine>/.
#
#   tools/run.sh [-m kc85_3|kc85_4] [-s seconds] [-p PLAN] [file.kcc]
#
# PLAN is passed to tools/mame.lua (see that file for the syntax) and defaults
# to a single screenshot just before the run ends. 50 frames = 1 second.
#
#   tools/run.sh -m kc85_4 -s 12 -p "300:snap,400:type:D,460:snap"
#   tools/run.sh -p "300:press::keyboard:KEY7|Cursor Up|6,400:peek:@_G:13"
#
# @name anywhere in the plan is replaced with that symbol's address from the
# link map, so a plan that watches a variable keeps working after a rebuild
# moves it.
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
MAME=${MAME:-/Applications/mame/mame}

MACHINE=kc85_3
SECONDS_TO_RUN=8
PLAN=
while getopts "m:s:p:" opt; do
    case "$opt" in
        m) MACHINE=$OPTARG ;;
        s) SECONDS_TO_RUN=$OPTARG ;;
        p) PLAN=$OPTARG ;;
        *) exit 2 ;;
    esac
done
shift $((OPTIND - 1))
KCC=${1:-$ROOT/dist/BOMB-SQUAD.KCC}

case "$MACHINE" in
    kc85_3) BIOS=caos31 ;;
    kc85_4) BIOS=caos42 ;;
    *) echo "unknown machine $MACHINE" >&2; exit 2 ;;
esac

# Resolve @symbol references against the link map.
MAP=${MAP:-$ROOT/dist/bomb-squad.map}
case "$PLAN" in
    *@*)
        [ -f "$MAP" ] || { echo "need $MAP to resolve @symbols; run make first" >&2; exit 2; }
        for sym in $(printf '%s' "$PLAN" | tr ',:|' '\n\n\n' | grep '^@' | sort -u); do
            addr=$(awk -v s="${sym#@}" '$1 == s { gsub(/\$/, "", $3); print $3; exit }' "$MAP")
            [ -n "$addr" ] || { echo "symbol ${sym#@} not found in $MAP" >&2; exit 2; }
            PLAN=$(printf '%s' "$PLAN" | sed "s/${sym}/${addr}/g")
        done
        ;;
esac

SNAP=$ROOT/build/snap
rm -rf "$SNAP/$MACHINE"
mkdir -p "$SNAP"

[ -d "$ROOT/.mame/roms/$MACHINE" ] || "$ROOT/tools/setup-roms.sh"

cd "$MAME_HOME" 2>/dev/null || cd "$(dirname "$MAME")"
BS_PLAN=$PLAN "$MAME" "$MACHINE" -bios "$BIOS" \
    -rompath "$ROOT/.mame/roms" \
    -quik "$KCC" \
    -autoboot_script "$ROOT/tools/mame.lua" \
    -snapshot_directory "$SNAP" \
    -cfg_directory "$ROOT/build/cfg" -nvram_directory "$ROOT/build/nvram" \
    -video none -sound none -nothrottle -str "$SECONDS_TO_RUN" 2>&1 | grep -v '^Average speed' || true
ls "$SNAP/$MACHINE" 2>/dev/null || echo "(no snapshots)"
