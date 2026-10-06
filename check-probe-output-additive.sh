#!/usr/bin/env bash
# check-probe-output-additive.sh -- prove the stream-recording probe change moved
# no existing reading.
#
# Why: entity_protocol_probe.cpp gained one output line (recording_stream=... and
# the svc_ServerInfo signals). The nine stored reports under evidence/final/ were
# produced by the previous binary. Rather than re-run the 15-minute oracle gate to
# show nothing regressed, diff the counter block directly: with the new line
# removed, every byte of every report must match.
#
# A change that is genuinely additive proves it, and a change that quietly moved a
# counter fails here. Run this whenever the probe's output is touched.
#
# Usage: bash check-probe-output-additive.sh
set -uo pipefail

cd "$(dirname "$0")"
PROBE=native/build-nmake/entity_protocol_probe.exe
DIR=evidence/final
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

DEMOS=(
  "ashville73|work/protocol-rescue-20261005/73.dem"
  "bagel|testdata/demos/bagel.dem"
  "comp|testdata/demos/comp.dem"
  "decal|testdata/demos/decal.dem"
  "protocol23|testdata/demos/protocol23.dem"
  "saytext2|testdata/demos/saytext2.dem"
  "short2024|testdata/demos/short-2024.dem"
  "small|testdata/demos/small.dem"
  "snakewater|testdata/demos/snakewater.dem"
)

fail=0
printf '%-12s %-12s %s\n' demo verdict note
for entry in "${DEMOS[@]}"; do
  name=${entry%%|*}
  path=${entry#*|}
  stored="$DIR/$name.txt"
  if [ ! -f "$stored" ]; then
    printf '%-12s %-12s %s\n' "$name" "SKIP" "no stored report at $stored"
    continue
  fi
  if [ ! -f "$path" ]; then
    printf '%-12s %-12s %s\n' "$name" "SKIP" "demo not present: $path"
    continue
  fi
  "$PROBE" "$path" > "$TMP/$name.new" 2>/dev/null
  # Drop exactly the lines the change introduced, normalise line endings, compare.
  grep -v '^recording_stream=' "$TMP/$name.new" | tr -d '\r' > "$TMP/$name.stripped"
  tr -d '\r' < "$stored" > "$TMP/$name.old"
  if diff -q "$TMP/$name.stripped" "$TMP/$name.old" >/dev/null; then
    added=$(grep -c '^recording_stream=' "$TMP/$name.new")
    printf '%-12s %-12s %s\n' "$name" "IDENTICAL" "added_lines=$added"
  else
    printf '%-12s %-12s %s\n' "$name" "DRIFTED" "see diff below"
    diff "$TMP/$name.old" "$TMP/$name.stripped" | head -12
    fail=1
  fi
done

echo
if [ "$fail" -eq 0 ]; then
  echo "PROBE-OUTPUT-ADDITIVE=PASS"
else
  echo "PROBE-OUTPUT-ADDITIVE=FAIL"
fi
exit "$fail"
