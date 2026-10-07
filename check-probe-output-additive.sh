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
# The world-model round (2026-10-07 evening) added ten keys to the end of the
# asset_refs= line. That line is stripped from claim 1 and frozen verbatim by
# claim 2, so refreshing added-lines.txt is what made those keys visible -- and a
# refresh is exactly the act that could hide a moved counter. Claim 3 exists for
# that: the pre-change values of every key that existed before the additions are
# frozen in asset-refs-pre-world-model.txt, the new keys are stripped back out of
# the current line, and the remainder must match the frozen subset byte for byte.
# Without claim 3 the only thing standing between a refresh and a silent
# regression would be the reader's memory of what the line used to say.
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

# Claim 3. The asset_refs= keys that existed before the world-model round were
# captured from the frozen added-lines.txt before the source was touched, so this
# file is a pre-change artifact and not something the current build can produce.
PRE=evidence/probe-baseline/asset-refs-pre-world-model.txt
# The keys the world-model round appended, by name, so that stripping them cannot
# also strip something older that happens to share a prefix.
NEW_KEYS='asset_world_model_index_known|asset_world_model_index_resolved|asset_world_model_index_zero|asset_world_model_index_unresolved|asset_world_model_index_out_of_range|asset_world_model_index_unresolved_max|asset_model_path_from_world_model|asset_model_path_world_model_only|asset_weapon_view_model_agrees|asset_weapon_view_model_zero|asset_weapon_view_model_differs'
# Two keys that predate the round and are expected to move anyway, for a reason
# that is itself a reading. `asset_model_path_known` and
# `asset_model_path_from_precache` count references that ended up with a path. The
# world route hands a path to references the m_nModelIndex route had nothing for,
# so those two counts rise by exactly asset_model_path_world_model_only -- the
# reference count whose only model identity is the world index (three weapons on
# snakewater: CTFKnife 382, CTFMinigun 428, CTFLunchBox 429, none of which carries
# an m_nModelIndex at all; zero on the other eight demos, where the world route
# only replaces a path the old route would have named). Excluding them from a byte
# comparison would be the same hole a refresh makes, so the exclusion is instead
# replaced by that identity, asserted per demo in claim 3b below. This is the
# pattern the earlier `recording=` correction used: named here, measured there.
# The stub-and-swap in claim 3b is what keeps the identity honest -- it fails if
# the two keys move by anything other than that count.
MOVING_KEYS='asset_model_path_known|asset_model_path_from_precache'
NEW_KEY_STRIP="s/ (${NEW_KEYS})=[^ ]*//g; s/ (${MOVING_KEYS})=[^ ]*//g"
# The same two keys as separate words for the per-demo loop. They cannot be looped
# over the alternation above: "${key}=[0-9]+" with a pipe in ${key} parses as
# "A|B=[0-9]+", whose first branch matches the bare key name, and the resulting
# non-numeric value then fails as an arithmetic variable ("asset_model_path_known:
# unbound variable") instead of as the movement it was supposed to measure.
MOVING_KEY_LIST='asset_model_path_known asset_model_path_from_precache'
# Used by 3b: the counter the movement must equal.
MOVEMENT_COUNTER='asset_model_path_world_model_only'

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

# The demo names, in the order the frozen files were collected in, so claim 3b can
# name the demo a movement happened on instead of printing a bare line number.
DEMO_NAMES=("${DEMOS[@]%%|*}")

# The pre-change artifact must actually predate the change: if it already carries
# a world-model key it was captured after the source was touched, and comparing
# against it would be new-vs-new. Same failure shape as STALE-BASE below, and the
# same answer -- a checked precondition, not a convention.
if grep -qE "(${NEW_KEYS})=" "$PRE"; then
  echo "FATAL: $PRE already carries a world-model key; it is not a pre-change artifact"
  exit 1
fi
sed -E "$NEW_KEY_STRIP" "$PRE" > "$TMP/assetrefs.pre.subset"

# Claim 3 is a comparison of the current asset_refs= line with the new keys
# stripped against the pre-change artifact, and both the refresh path and the
# reporting path need it, so it is one function rather than two copies.
subset_ok() {
  grep -E '^asset_refs=' "$TMP/added.new" | sed -E "$NEW_KEY_STRIP" > "$TMP/assetrefs.subset"
  # The frozen artifact is stripped with the same expression, or the comparison
  # would be between a line that still carries those keys and one that does not --
  # which is not a movement, it is two different questions.
  diff -q "$TMP/assetrefs.subset" "$TMP/assetrefs.pre.subset" >/dev/null
}

# Claim 3b: the two documented-moving keys moved by exactly the world route's
# count. Without this, 3a's exclusion would be exactly the promise this script
# exists to replace.
moving_keys_ok() {
  local old_line new_line name old_value new_value only delta moved=0
  # A missing demo makes the three columns misalign, and a misaligned comparison
  # would read as a movement. Refuse instead of guessing.
  local current_lines
  current_lines=$(grep -c -E '^asset_refs=' "$TMP/added.new")
  if [ "$current_lines" -ne "${#DEMOS[@]}" ] || [ "$(wc -l < "$PRE")" -ne "${#DEMOS[@]}" ]; then
    echo "  cannot pair lines: ${current_lines} current vs $(wc -l < "$PRE") frozen, expected ${#DEMOS[@]} each"
    return 1
  fi
  while IFS='|' read -r name old_line new_line; do
    [ -n "$name" ] || continue
    only=$(printf '%s' "$new_line" | grep -oE "${MOVEMENT_COUNTER}=[0-9]+" | cut -d= -f2)
    for key in $MOVING_KEY_LIST; do
      old_value=$(printf '%s' "$old_line" | grep -oE "(${key})=[0-9]+" | cut -d= -f2)
      new_value=$(printf '%s' "$new_line" | grep -oE "(${key})=[0-9]+" | cut -d= -f2)
      if [ -z "$old_value" ] || [ -z "$new_value" ] || [ -z "$only" ]; then
        echo "  $name ${key}: missing reading (old=${old_value:-<none>} new=${new_value:-<none>} ${MOVEMENT_COUNTER}=${only:-<none>})"
        return 1
      fi
      delta=$((new_value - old_value))
      if [ "$delta" -ne "$only" ]; then
        echo "  $name ${key}: moved ${old_value}->${new_value} (delta ${delta}) but only ${only} references exist whose sole model identity is the world index"
        return 1
      fi
      moved=$((moved + delta))
    done
  done < <(paste -d'|' <(printf '%s\n' "${DEMO_NAMES[@]}") \
                        <(grep -E '^asset_refs=' "$PRE") \
                        <(grep -E '^asset_refs=' "$TMP/added.new"))
  echo "  moved=$moved paths added by the world route, all accounted for"
  return 0
}

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
  # A refresh is the one act that can hide a moved counter inside the frozen line,
  # so it has to clear claim 3 first: the keys that existed before the additions
  # must still read what the pre-change artifact says.
  if ! subset_ok; then
    echo "refusing to refresh: the pre-world-model keys in asset_refs= moved"
    diff "$PRE" "$TMP/assetrefs.subset" | head -12
    echo "PROBE-OUTPUT-ADDITIVE=FAIL"
    exit 1
  fi
  if ! moving_keys_ok; then
    echo "refusing to refresh: the two movable keys did not move by the world route's count"
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

# Claim 3: the refresh that introduced the ten new keys did not move a key that
# predated them, and the two keys it was allowed to move moved by the world
# route's count and nothing else. This is the claim the refresh path above cannot
# wave through.
echo
if [ ! -s "$PRE" ]; then
  echo "pre-world-model-fields=MISSING $PRE is absent or empty"
  fail=1
elif [ "$(wc -l < "$PRE")" -ne "${#DEMOS[@]}" ]; then
  echo "pre-world-model-fields=COUNT ${PRE} has $(wc -l < "$PRE") lines, expected ${#DEMOS[@]}"
  fail=1
elif subset_ok; then
  echo "pre-world-model-fields=IDENTICAL ($(wc -l < "$PRE") asset_refs= lines, new keys stripped)"
  if moving_keys_ok; then
    echo "pre-world-model-movement=ACCOUNTED"
  else
    echo "pre-world-model-movement=UNACCOUNTED"
    fail=1
  fi
else
  echo "pre-world-model-fields=DRIFTED -- a key that predates the world-model round moved"
  diff "$PRE" "$TMP/assetrefs.subset" | head -12
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
