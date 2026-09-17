#!/bin/sh
# Run a build in MAME headless and drop screenshots into build/snap/<machine>/.
#
#   tools/run.sh [-m kc85_3|kc85_4|z9001] [-s seconds] [-p PLAN] [file]
#
# KC85: a .KCC is loaded with -quik under the right BIOS.
# Z9001: the machine has no quickload, so the flat binary at $0300 is poked
#        into RAM via tools/mame.lua's load: action and entered with pc:.
#
# PLAN is passed to tools/mame.lua (see that file for the syntax) and defaults
# to a single screenshot just before the run ends. 50 frames = 1 second.
#
#   tools/run.sh -m kc85_4 -s 12 -p "300:snap,400:type:D,460:snap"
#   tools/run.sh -m z9001 -s 14 -p "50:snap,90:snap,150:snap,210:snap,300:snap"
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
        -p) PLAN=$OPTARG ;;
        p) PLAN=$OPTARG ;;
        *) exit 2 ;;
    esac
done
shift $((OPTIND - 1))

case "$MACHINE" in
    kc85_3)
        BIOS=caos31
        KCC=${1:-$ROOT/dist/BOMB-SQUAD.KCC}
        MAP=$ROOT/dist/bomb-squad.map
        BS_PLAN="$PLAN"
        ;;
    kc85_4)
        BIOS=caos42
        KCC=${1:-$ROOT/dist/BOMB-SQUAD.KCC}
        MAP=$ROOT/dist/bomb-squad.map
        BS_PLAN="$PLAN"
        ;;
    z9001)
        # No -quik, no -bios: poke the flat binary into RAM and jump to it.
        KCC=${1:-$ROOT/dist/bomb-squad-z9001}
        MAP=$ROOT/dist/bomb-squad-z9001.map
        BIOS=
        ;;
    *) echo "unknown machine $MACHINE" >&2; exit 2 ;;
esac

if [ "$MACHINE" = "z9001" ]; then
    BS_PLAN="1:load:${KCC}:300,2:pc:300"
    [ -n "$PLAN" ] && BS_PLAN="${BS_PLAN},${PLAN}"
    BS_PLAN="${BS_PLAN},99999:quit"
fi

# Resolve @symbol references against the link map.
case "$BS_PLAN" in
    *@*)
        [ -f "$MAP" ] || { echo "need $MAP to resolve @symbols; run make first" >&2; exit 2; }
        for sym in $(printf '%s' "$BS_PLAN" | tr ',:|' '\n\n\n' | grep '^@' | sort -u); do
            addr=$(awk -v s="${sym#@}" '$1 == s { gsub(/\$/, "", $3); print $3; exit }' "$MAP")
            [ -n "$addr" ] || { echo "symbol ${sym#@} not found in $MAP" >&2; exit 2; }
            BS_PLAN=$(printf '%s' "$BS_PLAN" | sed "s/${sym}/${addr}/g")
        done
        ;;
esac

SNAP=$ROOT/build/snap
rm -rf "$SNAP/$MACHINE"
mkdir -p "$SNAP"

[ -d "$ROOT/.mame/roms/$MACHINE" ] || "$ROOT/tools/setup-roms.sh"

cd "$MAME_HOME" 2>/dev/null || cd "$(dirname "$MAME")"
export BS_PLAN
if [ -n "$BIOS" ]; then
    "$MAME" "$MACHINE" -bios "$BIOS" \
        -rompath "$ROOT/.mame/roms" \
        -quik "$KCC" \
        -autoboot_script "$ROOT/tools/mame.lua" \
        -snapshot_directory "$SNAP" \
        -cfg_directory "$ROOT/build/cfg" -nvram_directory "$ROOT/build/nvram" \
        -video none -sound none -nothrottle -str "$SECONDS_TO_RUN" 2>&1 | grep -v '^Average speed' || true
else
    "$MAME" "$MACHINE" \
        -rompath "$ROOT/.mame/roms" \
        -autoboot_script "$ROOT/tools/mame.lua" \
        -snapshot_directory "$SNAP" \
        -cfg_directory "$ROOT/build/cfg" -nvram_directory "$ROOT/build/nvram" \
        -video none -sound none -nothrottle -str "$((SECONDS_TO_RUN + 2))" 2>&1 | grep -v '^Average speed' || true
fi
ls "$SNAP/$MACHINE" 2>/dev/null || echo "(no snapshots)"
