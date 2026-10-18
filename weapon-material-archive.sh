#!/usr/bin/env bash
# weapon-material-archive.sh -- package evidence/weapon-material for review.
#
# The gate (weapon-material-check.sh) leaves one trace per run, and a trace is
# ~3.7 MB of per-frame lines. What a reviewer needs from it is small and named:
# the body's model line, its material reading, the arms' reading, and the tick the
# frame was taken at. This script writes exactly those into <run>.readings.txt,
# next to the frames the gate already wrote, and records a sha256 for every
# artefact so a later run can say "same bytes" or "not the same run".
#
# It reads, it does not re-run. Safe to call while nothing is drawing; it refuses
# to guess if a trace is missing.
#
# `natural-on-before-fix.*` is the one file set this script does not produce: it
# was captured from the base tree before any T14 edit and copied in by hand, which
# is why the report can say the recording's frame did not move. Everything else
# here is written by the gate on every run.
#
# Usage: bash weapon-material-archive.sh
# Exit:  0 = every expected run had a trace and readings were written,
#        1 = at least one expected run is missing (its readings file is still
#            written, with a `missing` line, so the gap is visible rather than
#            absent).
set -uo pipefail
cd "$(dirname "$0")"
DIR=evidence/weapon-material

# The runs the gate makes. `mutation-*` only exist after a --mutation run, so they
# are listed separately: their absence is reported, not treated as a failure of a
# forward-only tree.
FORWARD_RUNS=(natural-on override-on missing-body override-on-repeat override-flat override-off)
MUTATION_RUNS=(mutation-baseline mutation-m1 mutation-m2 mutation-m3 mutation-restored)

missing=0

readings_for() { # readings_for <run>
  local run="$1" trace="$DIR/$1.trace" out="$DIR/$1.readings.txt"
  if [ ! -s "$trace" ]; then
    printf 'run=%s status=missing (no trace)\n' "$run" > "$out"
    echo "  ..   $run: no trace"
    return 1
  fi
  {
    printf 'run=%s\n' "$run"
    printf 'trace_bytes=%s\n' "$(stat -c%s "$trace")"
    printf 'capture_tick=%s\n' \
      "$(grep -a "draw: begin tick=" "$trace" | tr -d '\r' | tail -1 \
        | sed -n 's/.*draw: begin tick=\([-0-9]*\).*/\1/p')"
    printf -- '-- the body: model, mesh and material state --\n'
    grep -a "gpu: viewmodel weaponBody " "$trace" | tr -d '\r' | sed 's/^[0-9.]* //'
    grep -a "gpu: viewmodel weaponBodyReading " "$trace" | tr -d '\r' | sed 's/^[0-9.]* //'
    grep -a "gpu: viewmodel weaponBodyMaterial " "$trace" | tr -d '\r' | sed 's/^[0-9.]* //'
    printf -- '-- the arms: same fields, the control --\n'
    grep -a "gpu: viewmodel request path=" "$trace" | tr -d '\r' | sed 's/^[0-9.]* //'
    printf -- '-- what the frame itself says --\n'
    grep -a "gpu: frame capture" "$trace" | tr -d '\r' | sed 's/^[0-9.]* //'
  } > "$out"
  echo "  ..   $run: $(wc -l < "$out" | tr -d ' ') lines"
  return 0
}

echo "=== readings ==="
for run in "${FORWARD_RUNS[@]}"; do
  readings_for "$run" || missing=1
done
for run in "${MUTATION_RUNS[@]}"; do
  if [ -s "$DIR/$run.trace" ]; then
    readings_for "$run" || missing=1
  else
    echo "  ..   $run: not in this tree (forward-only run)"
  fi
done

echo "=== hashes ==="
# Frames first: they are the claim that two runs agree. The traces are hashed but
# NOT committed -- they are ~3.7 MB each and reproducible from a run, so the hash
# is the receipt and the committed readings file is the extract.
#
# Paths are spelled from the repo root and the prefix is stripped afterwards,
# rather than cd-ing into $DIR: a `cd` inside the group would leave the shell
# somewhere else, and the second run of this script would then write its hash
# file inside the evidence directory it was hashing.
{
  for pattern in bmp trace json txt; do
    for file in "$DIR"/*."$pattern"; do
      [ -f "$file" ] || continue
      case "$file" in *.readings.txt) continue ;; esac
      sha256sum "$file"
    done
  done
} > "$DIR/artifacts.sha256"
# The paths are repo-relative on purpose: `sha256sum -c evidence/weapon-material/artifacts.sha256`
# from the repo root verifies every artefact in place. Stripping them to basenames
# would look tidier and would make the receipt uncheckable without a cd.
if [ -s "$DIR/artifacts.sha256" ]; then
  echo "  ..   $(wc -l < "$DIR/artifacts.sha256" | tr -d ' ') artefacts hashed"
else
  echo "  FAIL nothing was hashed -- $DIR is empty"
  exit 1
fi

if [ "$missing" -eq 1 ]; then
  echo "ARCHIVE=PARTIAL (a forward run is missing its trace)"
  exit 1
fi
echo "ARCHIVE=OK"
