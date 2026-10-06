#!/usr/bin/env bash
# check-probe-output-additive.sh -- prove the probe's new output lines moved no
# existing reading.
#
# Why: entity_protocol_probe.cpp gained output lines in two rounds -- first
# recording_stream=... (with the svc_ServerInfo signals) and index_state=... (with
# the truncated-tail classification), then sound_precache_entries=... and
# asset_refs=... (with the P1 precache tables and asset breakdown). The nine
# stored reports under evidence/probe-baseline/ were produced by the binary that
# predates them. Rather than re-run the 15-minute oracle gate to show nothing
# regressed, diff the counter block directly: with the new lines removed, every
# byte of every report must match.
#
# A change that is genuinely additive proves it; a change that quietly moved a
# counter fails here. Run this whenever the probe's output is touched.
#
# Two claims, both asserted:
#   1. no counter that existed before the additions moved (the stripped diff), and
#   2. the added lines themselves have not moved since they were introduced
#      (added-lines.txt, refreshed only as a deliberate act via --refresh-added).
# Without the second claim "stripped" would quietly mean "unverified", which is
# the same shape of mistake this whole chain exists to catch.
#
# Three ways this check has silently proved nothing, all now asserted against:
#
#   1. The first version used repo-relative demo paths ("testdata/demos/bagel.dem").
#      The isolated test tree has no testdata/ (it was imported as `git archive
#      d585af8 native`, so only native/ exists), so all nine entries printed SKIP
#      and the script still ended in PASS. A gate that passes with zero
#      comparisons is not a gate. `compared` is now counted and must equal the
#      number of demos; a missing input is a failure, not a skip.
#   2. If the baseline is refreshed with the current binary, the comparison
#      becomes new-vs-new and is trivially identical. The stored reports must
#      therefore still be free of the lines this script strips -- that is checked
#      explicitly (`STALE-BASE`). This is also why the baseline lives in
#      evidence/probe-baseline/ rather than evidence/final/: verify-all.sh step 2
#      regenerates evidence/final/ before step 7 runs, so pointing at it made all
#      nine demos read STALE-BASE and the step fail with compared=0.
#   3. With the paths fixed, five demos reported DRIFTED on the `recording=` field
#      alone: 226d119 fixed the header classifier, so the stored pre-fix reports
#      say `recording=POV (heuristic)` where the current binary says
#      `recording=SourceTV`. That is the intended correction, not counter drift,
#      and it is pinned by oracle-recording-types.sh. The field is normalised here
#      so this script keeps exactly one claim: no counter moved.
#
# Usage: bash check-probe-output-additive.sh
set -uo pipefail

cd "$(dirname "$0")"
PROBE=native/build-nmake/entity_protocol_probe.exe
# The frozen baseline, NOT evidence/final/. evidence/final/ is a live output
# directory: verify-all.sh step 2 runs run-demos.sh against it, so by the time
# step 7 runs the "stored" reports have already been overwritten by the current
# binary and the comparison would be new-vs-new. probe-baseline/ holds the nine
# reports exactly as committed before the stream-recording and truncated-tail
# output lines were added; it must only be refreshed as a deliberate act, and
# refreshing it means the STRIP list below needs re-deriving too.
DIR=evidence/probe-baseline
SRC=D:/TF2_Demo_Player
T=$SRC/.scratch/tf2-demo-parser/test_data

# Every prefix listed here is a line the gate stops looking at, so it may only
# name lines the stored reports predate.
#   recording_stream=  added with the stream-level recording verdict
#   index_state=       added with the truncated-tail classification
#   sound_precache_entries=  added by 8c6f06e with the precache table readings
#   asset_refs=              added by 8c6f06e with the asset reference breakdown
# The last two are the P1 additions. They are not left unverified by being
# stripped: their values are frozen separately in ADDED below, and the mutation
# suite proves they are load-bearing (m7 removes the modelprecache branch and
# both collapse, while sound_precache_entries must hold).
STRIP='^(recording_stream=|index_state=|sound_precache_entries=|asset_refs=)'

# The subset of stripped lines that gets its own frozen baseline. recording_stream=
# is deliberately NOT here: its value legitimately changed for the five SourceTV
# demos when 226d119 fixed the header classifier, and oracle-recording-types.sh
# pins it per demo against a hard-coded table. Freezing it here would mean either
# re-freezing on every classifier change or asserting a value we know is stale.
ADDED_STRIP='^(sound_precache_entries=|asset_refs=)'
ADDED=evidence/probe-baseline/added-lines.txt

# `recording=` is normalised rather than stripped-as-a-line: its *value* legitimately
# changed for the five SourceTV demos when 226d119 fixed the header classifier
# (`recording=POV (heuristic)` -> `recording=SourceTV`). The stored reports are the
# pre-226d119 baseline, so that field must not be compared here. The optional
# ` (heuristic)` suffix is part of the same field and is consumed too -- leaving it
# behind made the diff read as counter drift on exactly those five demos.
# The field is not left unverified: oracle-recording-types.sh pins recording=,
# recording_stream= and server_info_hltv= per demo against a hard-coded table. This
# script owns exactly one claim: no counter moved.
NORMALISE='s/recording=[^ ]+( \([^ )]+\))?/recording=<pinned-elsewhere>/'

[ -x "$PROBE" ] || { echo "FATAL: probe not found: $PROBE"; exit 1; }

DEMOS=(
  "bagel|$SRC/testdata/demos/4a9bfb9276509d0ec5f5fdc722a95b17_match-20260927-0239-koth_bagel_rc13.dem"
  "snakewater|$SRC/testdata/demos/bb841c6d379ff7c40d0c8baf99f59d8d_matcha-20260927-1347-cp_snakewater_final1.dem"
  "ashville73|$SRC/work/protocol-rescue-20261005/73.dem"
  "decal|$T/decal.dem"
  "protocol23|$T/protocol23.dem"
  "comp|$T/comp.dem"
  "saytext2|$T/saytext2.dem"
  "short2024|$T/short-2024.dem"
  "small|$T/small.dem"
)

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

REFRESH=0
[ "${1:-}" = "--refresh-added" ] && REFRESH=1

# The added lines are collected in DEMOS order, so the frozen file is a stable
# sequence rather than whatever order the shell happened to iterate.
: > "$TMP/added.new"

fail=0
compared=0
printf '%-12s %-12s %s\n' demo verdict note
for entry in "${DEMOS[@]}"; do
  name=${entry%%|*}
  path=${entry#*|}
  stored="$DIR/$name.txt"
  if [ ! -f "$stored" ]; then
    printf '%-12s %-12s %s\n' "$name" "MISSING" "no stored report at $stored"
    fail=1
    continue
  fi
  if [ ! -f "$path" ]; then
    printf '%-12s %-12s %s\n' "$name" "MISSING" "demo not present: $path"
    fail=1
    continue
  fi
  # The baseline must predate the lines we strip, otherwise this compares the new
  # binary against itself and cannot detect a moved counter.
  if grep -q -E "$STRIP" "$stored"; then
    printf '%-12s %-12s %s\n' "$name" "STALE-BASE" "$stored already contains a stripped prefix"
    fail=1
    continue
  fi
  "$PROBE" "$path" > "$TMP/$name.new" 2>/dev/null
  grep -E "$ADDED_STRIP" "$TMP/$name.new" | tr -d '\r' >> "$TMP/added.new"
  grep -v -E "$STRIP" "$TMP/$name.new" | tr -d '\r' | sed -E "$NORMALISE" > "$TMP/$name.stripped"
  tr -d '\r' < "$stored" | sed -E "$NORMALISE" > "$TMP/$name.old"
  if diff -q "$TMP/$name.stripped" "$TMP/$name.old" >/dev/null; then
    added=$(grep -c -E "$STRIP" "$TMP/$name.new")
    printf '%-12s %-12s %s\n' "$name" "IDENTICAL" "added_lines=$added"
    compared=$((compared + 1))
  else
    printf '%-12s %-12s %s\n' "$name" "DRIFTED" "see diff below"
    diff "$TMP/$name.old" "$TMP/$name.stripped" | head -12
    fail=1
  fi
done

if [ "$REFRESH" -eq 1 ]; then
  if [ "$fail" -ne 0 ]; then
    echo "refusing to refresh: the stripped diff is not clean, so the added lines"
    echo "would be frozen on top of an unrelated change"
    echo "PROBE-OUTPUT-ADDITIVE=FAIL"
    exit 1
  fi
  cp "$TMP/added.new" "$ADDED"
  echo "refreshed $ADDED from $(grep -c -E "$ADDED_STRIP" "$ADDED") lines"
  echo "PROBE-OUTPUT-ADDITIVE=PASS"
  exit 0
fi

# Claim 2: the added lines must still read what they read when they were
# introduced. A missing or empty frozen file is a failure, not a skip -- the
# whole point of this block is that "stripped" does not become "unverified".
echo
if [ ! -s "$ADDED" ]; then
  echo "added-lines=MISSING $ADDED is absent or empty"
  fail=1
elif diff -q "$TMP/added.new" "$ADDED" >/dev/null; then
  echo "added-lines=IDENTICAL ($(wc -l < "$ADDED") lines frozen)"
else
  echo "added-lines=DRIFTED -- the added lines moved since they were introduced"
  diff "$ADDED" "$TMP/added.new" | head -12
  fail=1
fi

echo
echo "compared=$compared/${#DEMOS[@]}"
if [ "$compared" -ne "${#DEMOS[@]}" ]; then
  echo "reason=only $compared of ${#DEMOS[@]} demos were actually compared"
  fail=1
fi
if [ "$fail" -eq 0 ]; then
  echo "PROBE-OUTPUT-ADDITIVE=PASS"
else
  echo "PROBE-OUTPUT-ADDITIVE=FAIL"
fi
exit "$fail"
