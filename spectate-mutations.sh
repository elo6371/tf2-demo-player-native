#!/usr/bin/env bash
# spectate-mutations.sh -- T66 mutation suite: every clause of the relay must have
# an assertion that goes red when the clause is broken.
#
# WHY EACH CASE RUNS ONLY SOME SECTIONS
# -------------------------------------
# A mutant is proved by the ASSERTION it breaks, not by how long the run took.
# Each case therefore names the gate sections that carry its assertion (section 5
# needs section 4's trace, so those two travel together). Everything else in the
# gate would re-confirm that the rest of the tree is healthy, which is what the
# final baseline run is for.
#
# EVERY CASE MUST
#   1. apply a change that compiles (a mutant that does not build proves nothing
#      about the gate, so a failed build is reported as MUTATION-INVALID, not as a
#      caught mutation),
#   2. make the gate exit non-zero,
#   3. redden the NAMED assertion named in its row, and
#   4. be restored byte-for-byte, after which the tree is rebuilt.
# The suite then runs the full gate one last time: a suite that ends on a dirty or
# broken tree has not proved anything about the tree either.
#
# THE FOUR BEHAVIOURS THE TASK NAMES, AND WHERE EACH IS PROVED
#   instant cut ........... m1, the handover window collapses to zero -> section 5
#   pause freeze .......... m6, the clock is taken from the demo tick -> section 8
#   dead-model remnant .... m2, hidesModel never hides anybody        -> section 4
#   no-target jump ........ m5, an empty roster aims at the origin    -> section 2
# Two further cases guard clauses the task states but does not list as mutations:
#   m3 a manual pick must not be turned into a handover by the auto rule -> section 6
#   m4 the demo's own observer target must not be an entity that is dead -> section 3
#
# Usage: bash spectate-mutations.sh [--skip-baseline]
# Exit:  0 = every mutant was caught by its named assertion and the final baseline
#            gate is green. 1 = one of those did not hold.
set -uo pipefail
cd "$(dirname "$0")"

OUT="${SPECTATE_MUT_OUT:-evidence/t66-sourcetv/mutations}"
RUN_BASELINE=1
[ "${1:-}" = "--skip-baseline" ] && RUN_BASELINE=0
mkdir -p "$OUT"

fail=0
invalid=0
caught=0
green=0

rebuild() { # rebuild <log-stem> -- both binaries the gate runs
  local stem="$1" rc=0
  bash build-target.sh spectate_probe > "$OUT/build-$stem-probe.log" 2>&1 || rc=$?
  local errors
  errors=$(grep -c 'error C' "$OUT/build-$stem-probe.log" 2>/dev/null || true)
  if [ "$rc" -ne 0 ] || [ "${errors:-0}" -ne 0 ]; then
    echo "  BUILD-FAIL spectate_probe rc=$rc errors=${errors:-0} (log $OUT/build-$stem-probe.log)"
    return 1
  fi
  bash build-target.sh tf2_demo_native > "$OUT/build-$stem-product.log" 2>&1 || rc=$?
  errors=$(grep -c 'error C' "$OUT/build-$stem-product.log" 2>/dev/null || true)
  if [ "$rc" -ne 0 ] || [ "${errors:-0}" -ne 0 ]; then
    echo "  BUILD-FAIL tf2_demo_native rc=$rc errors=${errors:-0} (log $OUT/build-$stem-product.log)"
    return 1
  fi
  echo "  BUILD-OK $stem (0 errors)"
  return 0
}

run_case() { # run_case <case> <sections> <required named reason>
  local tag="$1" sections="$2" required="$3"
  local log="$OUT/$tag-gate.log" gate_rc=0
  echo "--- mutant $tag"
  if ! python spectate-patchlib.py apply "$tag"; then
    echo "  MUTATION-INVALID $tag: the mutant could not be applied, so nothing was measured"; invalid=$((invalid + 1)); fail=1
    return
  fi
  if ! rebuild "$tag-apply"; then
    echo "  MUTATION-INVALID $tag: the mutant does not compile, so the gate never ran on it"
    invalid=$((invalid + 1)); fail=1
    python spectate-patchlib.py restore "$tag" >/dev/null 2>&1
    rebuild "$tag-restore" >/dev/null 2>&1
    return
  fi
  SPECTATE_SECTIONS="$sections" SPECTATE_OUT="$OUT/$tag" bash spectate-check.sh > "$log" 2>&1
  gate_rc=$?
  echo "  gate sections $sections exited $gate_rc"
  grep '  FAIL ' "$log" | sed 's/^/    /' | head -20
  if [ "$gate_rc" -eq 0 ]; then
    echo "  MUTATION-GREEN $tag: the gate stayed green, so nothing asserts this clause"
    green=$((green + 1)); fail=1
  else
    if grep -q "  FAIL .*$required" "$log"; then
      echo "  MUTATION-RED $tag: '$required'"
      caught=$((caught + 1))
    else
      echo "  MUTATION-MISS $tag: the gate went red but not on '$required'"
      fail=1
    fi
  fi
  python spectate-patchlib.py restore "$tag" >/dev/null 2>&1
  # The verify command exits 1 when the file is NOT applied -- which is exactly the
  # state a restore must produce. Piping that into grep -q under "set -o pipefail"
  # makes the pipeline return python's non-zero status, so a CORRECTLY restored file
  # read as RESTORE-FAIL while a file that still carried the mutant read as restored.
  # Read the text into a variable instead; do not pipe a command whose exit code
  # carries meaning through a filter.
  restore_state=$(python spectate-patchlib.py verify "$tag" 2>&1) || true
  case "$restore_state" in
    *applied=0*) echo "  RESTORED $tag byte-for-byte" ;;
    *) echo "  RESTORE-FAIL $tag: the file still carries the mutant ($restore_state)"; fail=1 ;;
  esac
  rebuild "$tag-restore" || fail=1
}

echo "=========================================================================="
echo "T66 mutation suite"
echo "=========================================================================="

# m1: the handover window collapses to zero -- an instant cut. Section 5 is the
# only section that can see this: the product still counts three handovers and
# still reports three deaths, so sections 3 and 4 stay green while the movement
# between players disappears.
run_case m1 "4,5" "handovers analysed"

# m2: hidesModel never hides anybody -- the corpse stays in the submitted set.
# Section 4 is where the product is asked how many instances it removed.
run_case m2 "4" "instances the corpse rule removed"

# m3: the manual wiring reverted -- the pick is stolen back by the automatic rule
# and animated. Section 6 asserts a manual pick produces no handover at all.
run_case m3 "6" "handovers that happened before the first death in the manual run"

# m4: the demo's own observer handle accepted without the alive test -- the camera
# follows a permanently dead spectator slot. Section 3 is the replay that counts
# how often the director was usable on this recording.
run_case m4 "3" "the director handle was never usable on this demo"

# m5: with nobody left to watch the camera jumps to the world origin, which the task
# forbids by name. The decision self-test in section 2 is where that clause is written
# down, so the case needs no product run at all.
run_case m5 "2" "the camera never falls back to the world origin"

# m6: the handover clock taken from the demo tick instead of the wall clock -- the
# pre-fix wiring that froze a handover whenever the recording was paused. Section 8 is
# the only section that runs the product with the clock stopped; sections 3-5 stay
# green on this mutant, which is why the pause clause needed a section of its own.
run_case m6 "8" "handover lines the product printed while the clock was frozen"

echo "=========================================================================="
if [ "$RUN_BASELINE" = "1" ]; then
  echo "--- final baseline: the restored tree must be green in every section"
  baseline_rc=0
  SPECTATE_OUT="$OUT/baseline" bash spectate-check.sh > "$OUT/baseline-gate.log" 2>&1 || baseline_rc=$?
  tail -3 "$OUT/baseline-gate.log" | sed 's/^/  /'
  if [ "$baseline_rc" -eq 0 ]; then
    echo "  BASELINE-GREEN"
  else
    echo "  BASELINE-FAIL: the restored tree does not pass its own gate"; fail=1
    grep '  FAIL ' "$OUT/baseline-gate.log" | sed 's/^/    /' | head -20
  fi
else
  echo "--- final baseline skipped (--skip-baseline): a skipped baseline is NOT RUN, not a pass"
fi

echo "=========================================================================="
echo "mutants caught:     $caught/4"
echo "mutants green:      $green (an unasserted clause)"
echo "mutants invalid:    $invalid (did not apply or did not compile)"
if [ "$fail" -eq 0 ] && [ "$caught" -eq 4 ]; then
  echo "SPECTATE-MUTATION=PASS: every clause of the relay reddened its own named assertion, the tree was restored byte-for-byte, and the final baseline gate is green."
else
  echo "SPECTATE-MUTATION=FAIL"
fi
exit "$fail"
