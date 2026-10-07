#!/usr/bin/env bash
# verify-all.sh -- one command that reproduces the whole P0 evidence chain.
#
#   1. clean full Release build (21 exe) and check error/warning counts
#   2. census all nine local demos
#   3. run the 58 wire fixtures
#   4. cross-check against the independent Rust oracle (nine demos)
#   5. cross-check against the oracle on a stratified sample of the real corpus
#   6. pin the recording type (POV vs SourceTV) of the nine oracle demos
#   7. prove the probe's added output lines moved no existing counter, and that
#      the added lines themselves have not moved since they were introduced
#   8. prove the readings can go red (C++ mutation suite, P0 and P1 cases)
#   9. prove the corpus-census verdict can go red
#  10. compare reconstructed entity positions against the independent oracle
#      *value by value* (counts cannot see a value read from the wrong property)
#  11. bound how old an out-of-window entity answer is allowed to be (step 10
#      only sees the live window; nothing before this step said how far back a
#      Checkpoint answer may fall)
#  12. prove a weapon's render request names the weapon and not the hands that
#      hold it (m_nModelIndex is the first-person composite on a weapon entity;
#      the world pass has to use m_iWorldModelIndex)
#  13. resolve TF2's observer pair -- m_iObserverMode and m_hObserverTarget -- and
#      measure what believing it would cost (it is left unwired on purpose; see
#      observer-focus-check.sh for the 2729-unit reading that says why)
#
# Anything that must be true for the delivery is asserted here, so a reviewer
# does not have to read thirteen reports. Every step writes its raw output to
# evidence/ before the assertion runs.
#
# Step 10 exists because the P1 z defect was invisible to every count-based
# check: the decoder was reading the right bits, writing the right property, and
# the renderer was reading a different one, so every entity sat at z = 0 while
# steps 4-6 stayed green.
#
# Step 11 exists because "the answer is a Checkpoint" said nothing about how old
# the checkpoint was: the retention policy could pin the head of the archive and
# answer a tick 54392 ticks stale (13.7 minutes) while every count stayed green
# and step 10 stayed green too (it only compares inside the live window).
#
# Step 12 exists because a path can be wrong and still resolve. A weapon entity's
# m_nModelIndex names its first-person arms composite -- a real asset -- so a
# world pass driven by it draws a pair of hands where the weapon belongs, and
# every counting step above stays green while it does. The step compares the
# index against the oracle's own packet value and pins which route chose the
# path, on the fixture, on two demos, and end to end.
#
# Steps 5, 6, 7 and 9 exist because the first P0 acceptance pass got four things
# wrong that no green reading caught:
#   * it reported all nine oracle demos as POV; five are SourceTV match demos
#     (their clientname is "SourceTV Demo"), so the SourceTV corpus was never
#     actually missing -- the classifier was.
#   * the oracle gate only ever saw nine demos, so "POV keeps zero failures" had
#     no independent-implementation evidence on the 1634-demo POV corpus.
#   * the corpus census printed PASS while parsing only the first line of each
#     report, so every "must be zero" field was absent and skipped.
#   * the probe gained an output line with nothing proving the old counters
#     were untouched.
#
# A fifth class of error turned up while re-reading the chain: a step that passes
# without checking anything. check-probe-output-additive.sh printed SKIP for all
# nine demos (it used repo-relative paths, and the isolated tree has no testdata/)
# and still ended in PASS; the fixture step asserted fixture_failures=0 but not
# that any fixture ran; the build step asserted rc=0 but not that any exe was
# produced; the mutation step asserted MUTATION-SUITE=PASS but not that any case
# ran. Each of those now asserts a non-zero work count as well.
#
# The P1 pass added a sixth: a line that is *stripped* from a comparison is a line
# nothing verifies. check-probe-output-additive.sh now keeps a second frozen file
# for the lines it strips, so "additive" and "unverified" stop being the same word.
#
# Cost: steps 1 and 5 dominate. Step 1 ~4 min, step 5 ~20 min at --sample 40.
# Step 10 adds ~2.5 min (the oracle walks the whole demo once per compared tick).
# Step 11 adds ~1.5 min (one bagel scan for the staleness sample).
# Step 12 adds ~4 min (one bagel scan, one POV scan, and one oracle run per
# packet in the live window -- 68 of them).
# Step 13 adds ~4 min (the gate twice: once straight, once with --mutation).
#
# Usage: bash verify-all.sh [--quick]     (--quick: oracle corpus sample of 8)
# Exit:  0 = every step passed.
set -uo pipefail
cd "$(dirname "$0")"

ORACLE_SAMPLE=40
[ "${1:-}" = "--quick" ] && ORACLE_SAMPLE=8

OUT=evidence/verify
mkdir -p "$OUT"
rc_all=0

step() { printf '\n=== %s ===\n' "$1"; }

step "1/13 clean full Release build"
bash build-cmake.sh > "$OUT/1-build.txt" 2>&1
tail -6 "$OUT/1-build.txt"
# exe_count pins that the build actually produced the targets: rc=0 with zero
# exes would otherwise read as a clean build. 21 is the count after P0 added two
# probe targets, so adding a target is an explicit edit here.
if grep -q 'cmake_build_rc=0' "$OUT/1-build.txt" \
   && grep -q '^errors=0$' "$OUT/1-build.txt" \
   && grep -q '^exe_count=21$' "$OUT/1-build.txt"; then
  echo "BUILD=PASS"
else
  echo "BUILD=FAIL"; rc_all=1
fi

step "2/13 nine-demo census"
bash run-demos.sh evidence/final | tee "$OUT/2-census.txt"
if [ "$(grep -c 'entity_failures=0 malformed_packets=0 unknown_message_packets=0' "$OUT/2-census.txt")" -eq 9 ]; then
  echo "CENSUS=PASS (9/9 demos at zero)"
else
  echo "CENSUS=FAIL"; rc_all=1
fi
# The message-type coverage assertion: no observed type may lack a decoder.
if [ "$(grep -c 'message_types_seen_without_decoder: <none>' evidence/final/*.txt | grep -c ':1$')" -eq 9 ]; then
  echo "COVERAGE=PASS (9/9 demos, every observed message type decoded)"
else
  echo "COVERAGE=FAIL"; rc_all=1
fi

step "3/13 wire fixtures"
native/build-nmake/entity_message_fixture_probe.exe > "$OUT/3-fixture.txt" 2>&1
FIXTURES=$(grep -c '^PASS' "$OUT/3-fixture.txt" || true)
echo "fixture_pass_count=$FIXTURES"
tail -1 "$OUT/3-fixture.txt"
# fixture_failures=0 alone would also hold if the probe ran nothing, so the pass
# count is asserted too. 58 is the current fixture count; adding one is a
# deliberate edit here.
if grep -q '^fixture_failures=0$' "$OUT/3-fixture.txt" && [ "$FIXTURES" -eq 58 ]; then
  echo "FIXTURE=PASS ($FIXTURES/58)"
else
  echo "FIXTURE=FAIL"; rc_all=1
fi

step "4/13 oracle cross-check (nine demos)"
bash check-oracle.sh "D:/TF2_Demo_Player_Deliverable/tools/ent-oracle/target/release/ent-oracle.exe" evidence/final \
  | tee "$OUT/4-oracle.txt"
if grep -q 'ORACLE-GATE=PASS' "$OUT/4-oracle.txt"; then
  echo "ORACLE=PASS"
else
  echo "ORACLE=FAIL"; rc_all=1
fi

step "5/13 oracle cross-check on a corpus sample (n=$ORACLE_SAMPLE)"
PY="C:/Users/Administrator/.workbuddy-ai/binaries/python/versions/3.13.12/python.exe"
"$PY" oracle-corpus-check.py --sample "$ORACLE_SAMPLE" --workers 2 \
  > "$OUT/5-oracle-corpus.txt" 2>&1
tail -14 "$OUT/5-oracle-corpus.txt"
if grep -q 'ORACLE-CORPUS=PASS' "$OUT/5-oracle-corpus.txt"; then
  echo "ORACLE-CORPUS=PASS"
else
  echo "ORACLE-CORPUS=FAIL"; rc_all=1
fi
# The comparison itself must be able to reject a mismatch.
if "$PY" oracle-corpus-check.py --selftest 2>&1 | grep -q 'ORACLE-CORPUS-SELFTEST=PASS'; then
  echo "ORACLE-CORPUS-SELFTEST=PASS"
else
  echo "ORACLE-CORPUS-SELFTEST=FAIL"; rc_all=1
fi

step "6/13 oracle demo recording types"
bash oracle-recording-types.sh | tee "$OUT/6-recording-types.txt"
if grep -q 'ORACLE-RECORDING-TYPES=PASS' "$OUT/6-recording-types.txt"; then
  echo "RECORDING-TYPES=PASS"
else
  echo "RECORDING-TYPES=FAIL"; rc_all=1
fi

step "7/13 probe output is additive"
bash check-probe-output-additive.sh | tee "$OUT/7-probe-additive.txt"
if grep -q 'PROBE-OUTPUT-ADDITIVE=PASS' "$OUT/7-probe-additive.txt"; then
  echo "PROBE-ADDITIVE=PASS"
else
  echo "PROBE-ADDITIVE=FAIL"; rc_all=1
fi

step "8/13 mutation suite (C++)"
bash mutate.sh > "$OUT/8-mutation.txt" 2>&1
tail -8 "$OUT/8-mutation.txt"
# Counting the verdict lines is what stops this step from passing on an empty
# suite: MUTATION-SUITE=PASS is also what a script that ran zero cases prints.
# 37 red + 3 hold is the current case count (m10 added eight red assertions,
# all of them readings this round introduced), so adding a case is an explicit
# edit here. Any GREEN or BROKE line means a mutation did not move the reading
# it targets -- which is the one thing the suite exists to detect.
RED=$(grep -c '^MUTATION-RED' "$OUT/8-mutation.txt" || true)
HOLD=$(grep -c '^MUTATION-HOLD' "$OUT/8-mutation.txt" || true)
BROKE=$(grep -c '^MUTATION-BROKE' "$OUT/8-mutation.txt" || true)
GREEN=$(grep -c '^MUTATION-GREEN' "$OUT/8-mutation.txt" || true)
IDENT=$(grep -c '^RESTORED-IDENTICAL' "$OUT/8-mutation.txt" || true)
echo "mutation_red=$RED hold=$HOLD green=$GREEN broke=$BROKE restored_identical=$IDENT"
if grep -q 'MUTATION-SUITE=PASS' "$OUT/8-mutation.txt" \
   && [ "$RED" -eq 37 ] && [ "$HOLD" -eq 3 ] \
   && [ "$GREEN" -eq 0 ] && [ "$BROKE" -eq 0 ] && [ "$IDENT" -eq 6 ]; then
  echo "MUTATION=PASS"
else
  echo "MUTATION=FAIL"; rc_all=1
fi

step "9/13 census verdict is falsifiable"
bash census-negative-test.sh | tee "$OUT/9-census-negative.txt"
if grep -q 'CENSUS-NEGATIVE=PASS' "$OUT/9-census-negative.txt"; then
  echo "CENSUS-NEGATIVE=PASS"
else
  echo "CENSUS-NEGATIVE=FAIL"; rc_all=1
fi

step "10/13 reconstructed positions vs the independent oracle, per value"
# Steps 4-6 compare *counts* against the Rust oracle. Counts cannot see a value
# that is decoded correctly and then read from the wrong property -- which is
# exactly the P1 z defect: every counter stayed green while every player rendered
# at z = 0. This step compares values, entity by entity. It also asserts a work
# count (compared=), so a run that compares nothing cannot report PASS.
TRAJ_OUT="$OUT/10-trajectory.txt"
"$PY" oracle-trajectory-check.py --ticks 4 > "$TRAJ_OUT" 2>&1
TRAJ_RC=$?
"$PY" oracle-trajectory-check.py --ticks 2 --mutation > "$OUT/10-trajectory-mutation.txt" 2>&1
MUT_RC=$?
cat "$TRAJ_OUT"
COMPARED=$(sed -n 's/.*compared=\([0-9][0-9]*\).*/\1/p' "$TRAJ_OUT" | head -1)
if [ "$TRAJ_RC" -eq 0 ] && [ "${COMPARED:-0}" -ge 20 ] \
   && [ "$MUT_RC" -eq 0 ] && grep -q 'MUTATION-CAUGHT=PASS' "$OUT/10-trajectory-mutation.txt"; then
  echo "TRAJECTORY-ORACLE=PASS (compared=$COMPARED, mutation caught)"
else
  echo "TRAJECTORY-ORACLE=FAIL (rc=$TRAJ_RC compared=${COMPARED:-0} mutation_rc=$MUT_RC)"; rc_all=1
fi

step "11/13 entity history coverage (how old a Checkpoint answer may be)"
# Step 10 compares values only inside the ~70-packet live window, because that is
# the only place the answer is tick-exact. Everywhere else the answer falls back
# to a retained checkpoint, and nothing bounded how old that could be -- measured
# on bagel, the answer could be 54392 ticks (13.7 minutes) stale while every
# count-based step stayed green. history-coverage-check.sh asserts the pigeonhole
# bound (worst retained gap <= 2x span/(kept-1)) on a synthetic fixture and on
# bagel end to end, and the work count (sampled=512) so an empty run cannot pass.
COV="$OUT/11-history-coverage.txt"
bash history-coverage-check.sh > "$COV" 2>&1
cat "$COV"
if grep -q 'HISTORY-COVERAGE=PASS' "$COV" \
   && grep -qF 'OK   sampled=512 of the requested 512 ticks' "$COV"; then
  echo "HISTORY-COVERAGE=PASS"
else
  echo "HISTORY-COVERAGE=FAIL"; rc_all=1
fi

step "12/13 weapon world model (a weapon must not be drawn as its arms)"
# Step 10 compares values but only for positions; a model path is the other half
# of "the picture is right", and it fails differently. A weapon entity carries
# two indices: m_nModelIndex is the first-person composite (its class's c_*_arms
# model) and m_iWorldModelIndex is the weapon itself. Reading the first one is not
# an error the decoder can see -- it resolves, to a real asset -- so the wrong
# picture was reachable with every count above staying green. The gate pins the
# fixture (every branch by construction), both demo readings, the index against
# the oracle's own packet value, and the skip: no packet in the live window writes
# the property, so the count of packets that carry it must be zero, and the gate
# says so out loud instead of comparing nothing.
WSMODEL="$OUT/12-weapon-world-model.txt"
bash weapon-world-model-check.sh > "$WSMODEL" 2>&1
cat "$WSMODEL"
WS_FIXTURE=$(grep -c '^  OK   fixture' "$WSMODEL" || true)
WS_PACKETS=$(sed -n 's/.*packets checked=\([0-9][0-9]*\) .*/\1/p' "$WSMODEL" | head -1)
if grep -q 'WEAPON-WORLD-MODEL=PASS' "$WSMODEL" \
   && [ "$WS_FIXTURE" -eq 12 ] && [ "${WS_PACKETS:-0}" -gt 0 ]; then
  echo "WEAPON-WORLD-MODEL=PASS (fixture_ok=$WS_FIXTURE, live_window_packets=$WS_PACKETS)"
else
  echo "WEAPON-WORLD-MODEL=FAIL (fixture_ok=$WS_FIXTURE, live_window_packets=${WS_PACKETS:-0})"; rc_all=1
fi

step "13/13 observer focus (who a spectator is watching, and what it would cost)"
# A camera driven by the view entity alone is right for a live player and wrong
# for a spectator: TF2 names the subject in m_iObserverMode / m_hObserverTarget,
# and this pipeline read neither. Step 13 asserts the pair resolves, that it agrees
# with the oracle's own packet, and -- because the obvious wiring is wrong on this
# demo -- pins the distance it would move the camera, so the round's refusal to
# follow is a measurement rather than an opinion. Run with --mutation too: both
# compared values are nudged by one and have to go red.
OBFOCUS="$OUT/13-observer-focus.txt"
bash observer-focus-check.sh > "$OBFOCUS" 2>&1
cat "$OBFOCUS"
OB_OK=$(grep -c '^  OK   ' "$OBFOCUS" || true)
OB_DIV=$(sed -n 's/^camera-to-target distance = \([0-9]*\) units.*/\1/p' "$OBFOCUS" | head -1)
if grep -q 'OBSERVER-FOCUS=PASS' "$OBFOCUS" && [ "${OB_OK:-0}" -gt 0 ] \
   && [ "${OB_DIV:-0}" -gt 0 ]; then
  echo "OBSERVER-FOCUS=PASS (assertions_ok=$OB_OK, camera-to-target=${OB_DIV}u)"
else
  echo "OBSERVER-FOCUS=FAIL (assertions_ok=${OB_OK:-0}, camera-to-target=${OB_DIV:-0})"; rc_all=1
fi
# The gate's own mutation lives inside step 13 rather than in mutate.sh, because the
# case it guards did not ship: there is no source line to reinstate, so what has to
# be shown able to fail is the comparison itself.
OBMUT="$OUT/13-observer-focus-mutation.txt"
bash observer-focus-check.sh --mutation > "$OBMUT" 2>&1
if grep -q 'MUTATION-CAUGHT=PASS' "$OBMUT" && grep -qc '^  FAIL ' "$OBMUT"; then
  OB_RED=$(grep -c '^  FAIL ' "$OBMUT")
  echo "OBSERVER-FOCUS-MUTATION=PASS (perturbations caught, red_lines=$OB_RED)"
else
  echo "OBSERVER-FOCUS-MUTATION=FAIL (a perturbation went unnoticed)"; rc_all=1
fi

printf '\n'
if [ "$rc_all" -eq 0 ]; then echo "VERIFY=PASS"; else echo "VERIFY=FAIL"; fi
exit "$rc_all"

