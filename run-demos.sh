#!/usr/bin/env bash
# run-demos.sh [outdir]  -- census every local demo with entity_protocol_probe.
# Prints a compact table and writes one full report per demo.
set -uo pipefail
cd "$(dirname "$0")"
OUT="${1:-evidence/$(date +%H%M%S)}"
mkdir -p "$OUT"
EXE=native/build-nmake/entity_protocol_probe.exe
SRC=D:/TF2_Demo_Player
T=$SRC/.scratch/tf2-demo-parser/test_data

run() { # name path
  "$EXE" "$2" > "$OUT/$1.txt" 2>&1
  local rc=$?
  printf '%-12s rc=%s  %s\n' "$1" "$rc" \
    "$(grep -oE 'entity_failures=[0-9]+' "$OUT/$1.txt" | head -1) $(grep -oE 'malformed_packets=[0-9]+' "$OUT/$1.txt" | head -1) $(grep -oE 'unknown_message_packets=[0-9]+' "$OUT/$1.txt" | head -1) $(grep -oE 'recording=[^ ]+' "$OUT/$1.txt" | head -1)"
}

run bagel      "$SRC/testdata/demos/4a9bfb9276509d0ec5f5fdc722a95b17_match-20260927-0239-koth_bagel_rc13.dem"
run snakewater "$SRC/testdata/demos/bb841c6d379ff7c40d0c8baf99f59d8d_matcha-20260927-1347-cp_snakewater_final1.dem"
run ashville73 "$SRC/work/protocol-rescue-20261005/73.dem"
run decal      "$T/decal.dem"
run protocol23 "$T/protocol23.dem"
run comp       "$T/comp.dem"
run saytext2   "$T/saytext2.dem"
run short2024  "$T/short-2024.dem"
run small      "$T/small.dem"
echo "outdir=$OUT"
