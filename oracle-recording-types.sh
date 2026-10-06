#!/usr/bin/env bash
# oracle-recording-types.sh -- report the header/stream recording verdict for the
# nine oracle demos, and assert the two agree.
#
# The nine oracle demos are not all POV: five of them are SourceTV match demos
# whose clientname is "SourceTV Demo" (see demoformat.h: clientname is "Name of
# client who recorded the game"). The P0 acceptance run reported all nine as POV
# because the header classifier only looked at servername. This script pins the
# correct verdict per demo so a regression is visible as a diff, not a sentence.
#
# Usage: bash oracle-recording-types.sh
set -uo pipefail
cd "$(dirname "$0")"

PROBE=native/build-nmake/entity_protocol_probe.exe
SRC=D:/TF2_Demo_Player
T=$SRC/.scratch/tf2-demo-parser/test_data
OUT=evidence/oracle-recording-types.txt

DEMOS=(
  "bagel|$SRC/testdata/demos/4a9bfb9276509d0ec5f5fdc722a95b17_match-20260927-0239-koth_bagel_rc13.dem|SourceTV"
  "snakewater|$SRC/testdata/demos/bb841c6d379ff7c40d0c8baf99f59d8d_matcha-20260927-1347-cp_snakewater_final1.dem|SourceTV"
  "ashville73|$SRC/work/protocol-rescue-20261005/73.dem|SourceTV"
  "comp|$T/comp.dem|SourceTV"
  "saytext2|$T/saytext2.dem|SourceTV"
  "decal|$T/decal.dem|POV"
  "protocol23|$T/protocol23.dem|POV"
  "short2024|$T/short-2024.dem|POV"
  "small|$T/small.dem|POV"
)

[ -x "$PROBE" ] || { echo "FATAL: probe not found: $PROBE"; exit 1; }
mkdir -p evidence

fail=0
{
  printf '%-12s %-10s %-10s %-8s %-8s %-8s %s\n' \
    demo expect header stream hltv_bit replay_bit verdict
} | tee "$OUT"

for entry in "${DEMOS[@]}"; do
  name=$(printf '%s' "$entry" | cut -d'|' -f1)
  path=$(printf '%s' "$entry" | cut -d'|' -f2)
  expect=$(printf '%s' "$entry" | cut -d'|' -f3)
  if [ ! -f "$path" ]; then
    printf '%-12s %-10s %s\n' "$name" "$expect" "SKIP (missing $path)" | tee -a "$OUT"
    continue
  fi
  dump=$("$PROBE" "$path" 2>/dev/null)
  header=$(printf '%s' "$dump" | tr -d '\r' | sed -n 's/.* recording=\([^ ]*\).*/\1/p' | head -1)
  stream=$(printf '%s' "$dump" | tr -d '\r' | sed -n 's/.*recording_stream=\([A-Za-z]*\).*/\1/p' | head -1)
  hltv=$(printf '%s' "$dump" | tr -d '\r' | sed -n 's/.*server_info_hltv=\([0-9]*\).*/\1/p' | head -1)
  replay=$(printf '%s' "$dump" | tr -d '\r' | sed -n 's/.*server_info_replay_bit=\([0-9]*\).*/\1/p' | head -1)

  verdict=OK
  [ "$stream" = "$expect" ] || verdict="MISMATCH-stream"
  if [ "$expect" = "SourceTV" ] && [ "$header" != "SourceTV" ]; then verdict="MISMATCH-header"; fi
  [ "$verdict" = "OK" ] || fail=1
  printf '%-12s %-10s %-10s %-8s %-8s %-8s %s\n' \
    "$name" "$expect" "$header" "$stream" "$hltv" "$replay" "$verdict" | tee -a "$OUT"
done

echo | tee -a "$OUT"
if [ "$fail" -eq 0 ]; then
  echo "ORACLE-RECORDING-TYPES=PASS" | tee -a "$OUT"
else
  echo "ORACLE-RECORDING-TYPES=FAIL" | tee -a "$OUT"
fi
exit "$fail"
