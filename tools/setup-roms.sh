#!/bin/sh
# Build MAME romsets for kc85_3 / kc85_4 out of the ROM images bundled with JKCEMU.
# Nothing copyrighted is checked into this repo; we just repackage what is already
# installed on this machine.
set -e
JAR="${JKCEMU_JAR:-$(dirname "$0")/../../jkcemu/jkcemu.jar}"
OUT="${1:-$(dirname "$0")/../.mame/roms}"

[ -f "$JAR" ] || { echo "jkcemu.jar not found at $JAR (set JKCEMU_JAR)" >&2; exit 1; }

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
unzip -o -j "$JAR" 'rom/kc85/*' -d "$TMP" >/dev/null

mkdir -p "$OUT/kc85_3" "$OUT/kc85_4"
cp "$TMP/basic_c000.bin"  "$OUT/kc85_3/basic_c0.853"   # CRC dfe34b08
cp "$TMP/caos31_e000.bin" "$OUT/kc85_3/caos__e0.853"   # CAOS 3.1
cp "$TMP/basic_c000.bin"  "$OUT/kc85_4/basic_c0.854"
cp "$TMP/caos42_c000.bin" "$OUT/kc85_4/caos__c0.854"   # CAOS 4.2
cp "$TMP/caos42_e000.bin" "$OUT/kc85_4/caos__e0.854"
echo "ROMs written to $OUT"
