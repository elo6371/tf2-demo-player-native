#!/usr/bin/env bash
# verify-all.sh -- one command that reproduces the whole P0 evidence chain.
#
#   1. clean full Release build (23 exe) and check error/warning counts
#   2. census all nine local demos
#   3. run the 58 wire fixtures
#   4. cross-check against the independent Rust oracle (nine demos)
#   5. cross-check against the oracle on a sample drawn from the pinned
#      evidence/corpus-calib/demos.txt universe (not the live demos directory)
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
#      observer-focus-check.sh for the 45-unit residual that decision now rests on,
#      for the 2729 it read before step 14 moved it, and for the 2026-10-08
#      attribution: the camera there is a held deathcam, the pair only ever names the
#      recorder's own body, and so the demo cannot decide the wiring question)
#  14. prove the entity-property selection rule takes the slot a later packet
#      wrote, not the one its rank prefers (a player's origin arrives twice and
#      only one copy keeps updating; step 13's 2729 -> 45 is this step's work)
#  15. decode every real SourceTV recording this machine has -- all nine of them --
#      and assert zero failures on each, so the SourceTV half of the protocol has
#      more than one demo behind it
#  16. take a frame back out of the running program and prove the instrument can
#      fail -- that the file is a self-consistent BMP, that the same scene twice is
#      byte-identical, and that a different scene is not. Steps 1-15 can all be
#      green while the picture on screen is wrong; this is the first step that can
#      say anything about the picture at all, and it deliberately says nothing
#      about whether the picture is *right* -- only that a later claim about the
#      picture would be measurable.
#  17. name what the demo scene can actually load. Step 16 pins the instrument but
#      never asks what is in the frame; on this machine its demo (koth_bagel_rc13)
#      has no installed BSP, so the capture came back as the renderer's fallback
#      full-screen spray-decal quad and step 16 passed anyway. This step asserts
#      which of main.cpp's five hardcoded archives opened, whether the demo map's
#      BSP is reachable, how many of its materials resolve to pixels, and how many
#      survive the 512x512 world-atlas cap -- resolvable is not drawable.
#  18. read the loop's *rate* rather than its output. The main loop awaited the
#      next frame in 1 ms slices, so an idle window ran the whole body ~1000 times
#      a second, pushing a title and UI controls for state that had not moved; a
#      hot loop still produces correct output, which is why no step above could
#      see it. This step asserts main_loop_iterations per rendered frame, that the
#      two state-driven updaters stay far below the loop count when nothing moves,
#      and that the metrics throttle still holds.
#  19. prove the class-fallback sweep does not build a transform for an entity it
#      is about to discard. The sweep used to call extractTransform on every
#      player-named entity and only then ask whether the transform had a player
#      class; extractTransform scans the whole property map once per suffix, so a
#      discarded entity paid for all of it. The witness is a synthetic fixture --
#      the demos cannot see it, because every player entity on every demo already
#      has a render request and the sweep is cold -- and the assertion is that the
#      fixture's transform count drops while its instance count does not move.
#  20. prove a model drawn as an entity carries its own paint, at a tick where
#      entities exist. Two gaps in one step: the entity draw block bound the map
#      atlas to every model, so a model was painted with whatever the atlas held
#      at the model's texel and no count could tell that from "no paint"; and
#      every gate that runs the program left at playback tick 16, before this
#      demo's first checkpoint at scene tick 10320, so nothing in the chain had
#      ever seen an entity. On 2026-10-09 the program died on the first drawable
#      snapshot while all nineteen steps above stayed green. The step asserts the
#      resolution chain per model from bytes, the instance-level binding count
#      read out of the window title past the boundary, and the 20 pixels of the
#      one visible instance against a committed witness captured before the
#      binding existed.
#  21. measure whether a demo carries the animation state that per-entity skeletal
#      animation would need, before wiring any. The 2026-10-07 recon listed bone
#      animation as a wiring task -- compile the decoder in, open
#      uploadBoneMatrices(..., true), advance the sequence by tick -- which assumes
#      the demo says which sequence a player is playing. It does not: TF2 strips
#      m_nSequence / m_flCycle / m_flPlaybackRate from CTFPlayer's send table
#      (source-sdk-2013 declares them in DT_BaseAnimating but routes m_flCycle via
#      SendProxy_ClientSideAnimation, and c_baseanimating.cpp:1168 notes that
#      player entities do not network m_nSequence). The step asserts the absence
#      on three recordings, the positive control on the same run (197 classes DO
#      carry it -- a rule that can only say no measures nothing), and the
#      independent oracle's agreement. 23 assertions; m13 makes it go red.
#
# Anything that must be true for the delivery is asserted here, so a reviewer does
# not have to read twenty-one reports. Every step writes its raw output to
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
# 2026-10-08 added a seventh, found the expensive way: an input set that is a
# property of the machine. Step 9 used to run `--sample 24` over the live Steam
# demos directory; two recordings landed while the round was being worked on, the
# evenly spaced sample moved to neighbouring files, and 23 of the 24 frozen
# reports stopped being read -- so every mutation would have been inert and the
# step went red for the right reason. The set is now pinned by name in
# evidence/corpus-calib/demos.txt and the step asserts the 24-report count.
# Step 5 had the same latent defect -- its own `--sample` globbed the same live
# directory -- and would have produced non-reproducible evidence on any round
# where a recording landed mid-run (it in fact did, on 2026-10-09: the eight
# compared demos were not the eight the committed file named). It now reads the
# same pinned list and asserts the compared count.
#
# Step 15 is the eighth, and it is a coverage hole rather than a wrong reading: the
# pinned 24-demo calibration set holds one SourceTV recording, so of its 1361392
# packets 112175 were SourceTV. A whole-corpus header scan found nine SourceTV
# recordings, so the fix is bounded -- decode all nine (664 MB, 917543 packets,
# ~3.5 min) instead of running a four-hour census over 1634 POV files that share one
# code path with the five POV demos already in the oracle set. Each demo is checked
# for zero entity failures *and* for having been read end to end (index_tail_bytes
# equals the file size), because a probe that stopped early reports the same zeros.
#
# Cost: steps 1 and 5 dominate. Step 1 ~4 min, step 5 ~20 min at --sample 40
# (both now sample *within* the pinned 24-name list, so the same demo set is
# compared on every machine). The pinned list caps the reachable sample at 24.
# Step 10 adds ~2.5 min (the oracle walks the whole demo once per compared tick).
# Step 11 adds ~1.5 min (one bagel scan for the staleness sample).
# Step 12 adds ~4 min (one bagel scan, one POV scan, and one oracle run per
# packet in the live window -- 68 of them).
# Step 13 adds ~4 min (the gate twice: once straight, once with --mutation).
# Step 15 adds ~3.5 min (nine full decodes; --mutation reuses those dumps, because
# it perturbs the comparison and not the decode).
# Step 18 adds ~5 min (two 12 s idle/play samples, plus the mutation, which
# rebuilds tf2_demo_native once to put the 1 ms wait back and once to restore it).
# Step 19 adds ~1.5 min (four probe runs over bagel, one over the POV demo, plus
# the mutation, which rebuilds entity_model_probe once and once again to restore).
# Step 20 adds ~3.5 min (the gate twice: once straight, once with --mutation. Each
# run is a 57 s demo run to playback tick 3001 -- the program has to survive the
# snapshot boundary for any of the readings to exist -- plus five probe runs over
# the installed VPK and a 2.5 MB frame compare).
# Anything that must be true for the delivery is asserted here, so a reviewer does
# not have to read twenty reports. Every step writes its raw output to
# evidence/ before the assertion runs.
#
# Usage: bash verify-all.sh [--quick]     (--quick: oracle corpus sample of 8)
# Exit:  0 = every step passed.
set -uo pipefail
cd "$(dirname "$0")"

# A full chain owns the build tree and every path under evidence/verify. Two
# callers sharing those paths produce a plausible but unusable hybrid report:
# one run can overwrite another run's SourceTV output or mutation counts. mkdir
# is atomic on the filesystem, so a second caller fails before it starts a
# build. Remove the directory only on normal shell exit; a stale lock after a
# killed process must be cleared deliberately rather than silently overlapping.
LOCK_DIR=.scratch/verify-all.lock
if ! mkdir "$LOCK_DIR" 2>/dev/null; then
  echo "VERIFY-LOCK=FAIL (another verify-all run owns $LOCK_DIR)"
  exit 2
fi
trap 'rmdir "$LOCK_DIR" 2>/dev/null || true' EXIT

ORACLE_SAMPLE=40
[ "${1:-}" = "--quick" ] && ORACLE_SAMPLE=8

OUT=evidence/verify
mkdir -p "$OUT"
rc_all=0

step() { printf '\n=== %s ===\n' "$1"; }

step "1/21 clean full Release build"
bash build-cmake.sh > "$OUT/1-build.txt" 2>&1
tail -6 "$OUT/1-build.txt"
# exe_count pins that the build actually produced the targets: rc=0 with zero
# exes would otherwise read as a clean build. 24 is the count after the
# entity-material round and the skeleton_skin_probe target, so adding a target is
# an explicit edit here. (The pin read 23 while the tree built 24 between
# 00b6033 and this line: the probe target was added without the pin moving, so
# step 1 reported BUILD=FAIL on every chain run in that window. The warning
# count is deliberately NOT asserted -- the one C4457 in main.cpp predates all of
# this and only its line number moves.)
if grep -q 'cmake_build_rc=0' "$OUT/1-build.txt" \
   && grep -q '^errors=0$' "$OUT/1-build.txt" \
   && grep -q '^exe_count=24$' "$OUT/1-build.txt"; then
  echo "BUILD=PASS"
else
  echo "BUILD=FAIL"; rc_all=1
fi

step "2/21 nine-demo census"
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

step "3/21 wire fixtures"
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

step "4/21 oracle cross-check (nine demos)"
bash check-oracle.sh "D:/TF2_Demo_Player_Deliverable/tools/ent-oracle/target/release/ent-oracle.exe" evidence/final \
  | tee "$OUT/4-oracle.txt"
if grep -q 'ORACLE-GATE=PASS' "$OUT/4-oracle.txt"; then
  echo "ORACLE=PASS"
else
  echo "ORACLE=FAIL"; rc_all=1
fi

step "5/21 oracle cross-check on a corpus sample (n=$ORACLE_SAMPLE)"
# The input set is pinned, not sampled from the live directory. `sorted(glob())`
# plus evenly spaced sampling is a property of the machine: the live corpus grew
# from 1656 to 1658 recordings mid-round on 2026-10-08, every index moved, and the
# evidence file named eight demos the committed run had never seen. Step 9 hit the
# same seventh class first; both now read evidence/corpus-calib/demos.txt, so the
# sample is a function of a frozen file rather than of whatever is on disk. The
# sample is taken *within* that pinned list, so --quick and the full run stay
# reproducible and the full run can ask for more demos than the quick one.
PY="C:/Users/Administrator/.workbuddy-ai/binaries/python/versions/3.13.12/python.exe"
ORACLE_CORPUS_LIST="evidence/corpus-calib/demos.txt"
ORACLE_CORPUS_SET=$(grep -cv '^#' "$ORACLE_CORPUS_LIST")
"$PY" oracle-corpus-check.py --demos-list "$ORACLE_CORPUS_LIST" \
  --sample "$ORACLE_SAMPLE" --workers 2 > "$OUT/5-oracle-corpus.txt" 2>&1
tail -14 "$OUT/5-oracle-corpus.txt"
# Assert the size actually compared, capped at the pinned list, so this step cannot
# pass on zero demos and cannot silently fall back to globbing the live directory.
ORACLE_CORPUS_N=$(sed -n 's/^sampled=\([0-9][0-9]*\)$/\1/p' "$OUT/5-oracle-corpus.txt" | head -1)
ORACLE_CORPUS_WANT=$ORACLE_SAMPLE
[ "$ORACLE_CORPUS_WANT" -gt "$ORACLE_CORPUS_SET" ] && ORACLE_CORPUS_WANT=$ORACLE_CORPUS_SET
if grep -q 'ORACLE-CORPUS=PASS' "$OUT/5-oracle-corpus.txt" \
   && [ "${ORACLE_CORPUS_N:-0}" -eq "$ORACLE_CORPUS_WANT" ]; then
  echo "ORACLE-CORPUS=PASS (pinned_sample=$ORACLE_CORPUS_N of $ORACLE_CORPUS_SET pinned)"
else
  echo "ORACLE-CORPUS=FAIL (pinned_sample=${ORACLE_CORPUS_N:-0} want=$ORACLE_CORPUS_WANT of $ORACLE_CORPUS_SET)"; rc_all=1
fi
# The comparison itself must be able to reject a mismatch.
if "$PY" oracle-corpus-check.py --selftest 2>&1 | grep -q 'ORACLE-CORPUS-SELFTEST=PASS'; then
  echo "ORACLE-CORPUS-SELFTEST=PASS"
else
  echo "ORACLE-CORPUS-SELFTEST=FAIL"; rc_all=1
fi

step "6/21 oracle demo recording types"
bash oracle-recording-types.sh | tee "$OUT/6-recording-types.txt"
if grep -q 'ORACLE-RECORDING-TYPES=PASS' "$OUT/6-recording-types.txt"; then
  echo "RECORDING-TYPES=PASS"
else
  echo "RECORDING-TYPES=FAIL"; rc_all=1
fi

step "7/21 probe output is additive"
bash check-probe-output-additive.sh | tee "$OUT/7-probe-additive.txt"
if grep -q 'PROBE-OUTPUT-ADDITIVE=PASS' "$OUT/7-probe-additive.txt"; then
  echo "PROBE-ADDITIVE=PASS"
else
  echo "PROBE-ADDITIVE=FAIL"; rc_all=1
fi

step "8/21 mutation suite (C++)"
VERIFY_ALL_LOCK_HELD=1 bash mutate.sh > "$OUT/8-mutation.txt" 2>&1
tail -8 "$OUT/8-mutation.txt"
# Counting the verdict lines is what stops this step from passing on an empty
# suite: MUTATION-SUITE=PASS is also what a script that ran zero cases prints.
# 46 red + 6 hold is the current case count (m11 added three red assertions and
# one hold -- the hold is bagel's chosenStale, which must NOT move when the
# freshness term is deleted, or the gate would be passable by a rule that simply
# prefers NonLocal; m12 added three red assertions and one hold -- the reds are
# the gate's refusal, the instance counter and the ledger's uploaded count, and
# the hold is medic's resolved slot count, which must NOT move when only the
# upload is short-circuited, or the gate's parse layer and its binding layer
# would be one reading counted twice; m13 adds three red assertions and one hold
# for the animation-property positive control. m13 was run alone on 2026-10-10
# and the complete suite read 46/6/0/0/6. Adding a case is an explicit edit here,
# which is the
# point: a suite whose assertion count drifts silently is a suite that stopped
# testing. Any GREEN or BROKE line means a mutation did not move the reading it
# targets -- which is the one thing the suite exists to detect.
RED=$(grep -c '^MUTATION-RED' "$OUT/8-mutation.txt" || true)
HOLD=$(grep -c '^MUTATION-HOLD' "$OUT/8-mutation.txt" || true)
BROKE=$(grep -c '^MUTATION-BROKE' "$OUT/8-mutation.txt" || true)
GREEN=$(grep -c '^MUTATION-GREEN' "$OUT/8-mutation.txt" || true)
IDENT=$(grep -c '^RESTORED-IDENTICAL' "$OUT/8-mutation.txt" || true)
echo "mutation_red=$RED hold=$HOLD green=$GREEN broke=$BROKE restored_identical=$IDENT"
if grep -q 'MUTATION-SUITE=PASS' "$OUT/8-mutation.txt" \
   && [ "$RED" -eq 46 ] && [ "$HOLD" -eq 6 ] \
   && [ "$GREEN" -eq 0 ] && [ "$BROKE" -eq 0 ] && [ "$IDENT" -eq 6 ]; then
  echo "MUTATION=PASS"
else
  echo "MUTATION=FAIL"; rc_all=1
fi

step "9/21 census verdict is falsifiable"
bash census-negative-test.sh | tee "$OUT/9-census-negative.txt"
# The report count is asserted so this step cannot pass on an empty pinned set:
# CENSUS-NEGATIVE=PASS is also what a suite that mutated zero reports would
# print. 24 is the frozen calibration set (evidence/corpus-calib/demos.txt),
# because on 2026-10-08 the live `--sample 24` moved out from under the frozen
# reports and every mutation would have been inert.
NEG_REPORTS=$(sed -n 's/^  reports=\([0-9]*\)  digest=.*/\1/p' "$OUT/9-census-negative.txt" | head -1)
if grep -q 'CENSUS-NEGATIVE=PASS' "$OUT/9-census-negative.txt" && [ "${NEG_REPORTS:-0}" -eq 24 ]; then
  echo "CENSUS-NEGATIVE=PASS (pinned_reports=$NEG_REPORTS)"
else
  echo "CENSUS-NEGATIVE=FAIL (pinned_reports=${NEG_REPORTS:-0})"; rc_all=1
fi

step "10/21 reconstructed positions vs the independent oracle, per value"
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

step "11/21 entity history coverage (how old a Checkpoint answer may be)"
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

step "12/21 weapon world model (a weapon must not be drawn as its arms)"
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

step "13/21 observer focus (who a spectator is watching, and what it would cost)"
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
OB_RED=$(grep -c '^  FAIL ' "$OBMUT")
if grep -q 'MUTATION-CAUGHT=PASS' "$OBMUT" && [ "${OB_RED:-0}" -eq 3 ]; then
  echo "OBSERVER-FOCUS-MUTATION=PASS (3/3 perturbations caught, red_lines=$OB_RED)"
else
  echo "OBSERVER-FOCUS-MUTATION=FAIL (a perturbation went unnoticed)"; rc_all=1
fi

step "14/21 entity property slot freshness (the rule must take the later write)"
# Step 10 compares positions but only inside one demo's live window, and on that
# demo the rank rule happens to be right. A player's origin arrives in two slots --
# DT_TFLocalPlayerExclusive (full precision, sent to the owning client) and
# DT_TFNonLocalPlayerExclusive (quantized, sent to everyone else) -- and only one of
# them keeps being written. On the POV demo's entity 3 the Local slot was last
# written at 51596 and never again while the NonLocal slot was rewritten every
# checkpoint, so a rank-only rule resolved a coordinate frozen 2379 ticks back. No
# counting step can see that: the candidate count is the same either way. Step 14
# asserts the rule takes the later write, that the tie/unknown-tick fallback still
# produces the old deterministic order, and that the ranking and the public API the
# renderer calls agree. mutate.sh m11 deletes the freshness term and requires this
# step to refuse, with bagel held unchanged.
SLOTF="$OUT/14-slot-freshness.txt"
bash slot-freshness-check.sh > "$SLOTF" 2>&1
cat "$SLOTF"
SF_OK=$(grep -c '^  OK   ' "$SLOTF" || true)
SF_STALE=$(sed -n 's/^SLOT-FRESHNESS=PASS (.*POV chosenStale=\([0-9]*\).*/\1/p' "$SLOTF" | head -1)
# 24 is the current assertion count (7 fixture + 8 POV + 5 bagel + 4 blast radius),
# pinned for the same reason step 8 pins its mutation count: a gate that keeps
# printing PASS while its assertions quietly disappear is not a gate.
if grep -q 'SLOT-FRESHNESS=PASS' "$SLOTF" && [ "${SF_OK:-0}" -eq 24 ] \
   && [ "${SF_STALE:-x}" = "0" ]; then
  echo "SLOT-FRESHNESS=PASS (assertions_ok=$SF_OK, POV chosenStale=0)"
else
  echo "SLOT-FRESHNESS=FAIL (assertions_ok=${SF_OK:-0}, POV chosenStale=${SF_STALE:-none})"; rc_all=1
fi
SLOTMUT="$OUT/14-slot-freshness-mutation.txt"
bash slot-freshness-check.sh --mutation > "$SLOTMUT" 2>&1
if grep -q 'MUTATION-CAUGHT=PASS' "$SLOTMUT" && grep -qc '^  FAIL ' "$SLOTMUT"; then
  SF_RED=$(grep -c '^  FAIL ' "$SLOTMUT")
  echo "SLOT-FRESHNESS-MUTATION=PASS (2/2 perturbations caught, red_lines=$SF_RED)"
else
  echo "SLOT-FRESHNESS-MUTATION=FAIL (a perturbation went unnoticed)"; rc_all=1
fi

step "15/21 real SourceTV coverage (all nine recordings, end to end)"
# The pinned 24-demo calibration set carries one SourceTV recording, so almost all of
# the SourceTV-side evidence in steps 1-14 is bagel and snakewater. A whole-corpus
# header scan says there are nine SourceTV recordings on this machine, and this step
# decodes all of them. The assertion that matters is not just entity_failures=0 --
# that is also what a probe that read one packet would print -- but index_tail_bytes
# equal to the file's own byte size, which is only true if the whole file was indexed
# and walked.
STV="$OUT/15-source-tv-coverage.txt"
bash source-tv-coverage-check.sh > "$STV" 2>&1
cat "$STV"
STV_OK=$(grep -c '^  OK   ' "$STV" || true)
STV_DEMOS=$(sed -n 's/^demos_read=\([0-9][0-9]*\) .*/\1/p' "$STV" | head -1)
STV_PKTS=$(sed -n 's/^demos_read=[0-9][0-9]* packets_scanned=\([0-9][0-9]*\) .*/\1/p' "$STV" | head -1)
# 47 is the current assertion count (9 demos x 5 assertions + 2 work counters),
# pinned for the same reason steps 8 and 14 pin theirs.
if grep -q 'SOURCE-TV-COVERAGE=PASS' "$STV" && [ "${STV_OK:-0}" -eq 47 ] \
   && [ "${STV_DEMOS:-0}" -eq 9 ] && [ "${STV_PKTS:-0}" -ge 900000 ]; then
  echo "SOURCE-TV-COVERAGE=PASS (demos=$STV_DEMOS packets=$STV_PKTS assertions_ok=$STV_OK)"
else
  echo "SOURCE-TV-COVERAGE=FAIL (demos=${STV_DEMOS:-0} packets=${STV_PKTS:-0} assertions_ok=${STV_OK:-0})"; rc_all=1
fi
# Both compared values are nudged by one and have to go red, nine rows each: a
# perturbation that reddens everything or nothing would not show that the comparison
# is per-demo.
STVMUT="$OUT/15-source-tv-coverage-mutation.txt"
bash source-tv-coverage-check.sh --mutation > "$STVMUT" 2>&1
if grep -q 'MUTATION-CAUGHT=PASS' "$STVMUT" && grep -qc '^  FAIL ' "$STVMUT"; then
  STV_RED=$(grep -c '^  FAIL ' "$STVMUT")
  echo "SOURCE-TV-COVERAGE-MUTATION=PASS (2/2 perturbations caught, red_rows=$STV_RED)"
else
  echo "SOURCE-TV-COVERAGE-MUTATION=FAIL (a perturbation went unnoticed)"; rc_all=1
fi

step "16/21 frame capture (the picture can be taken out of the running program)"
# Steps 1-15 are all counters and decoded values. Not one of them can speak to
# whether the picture is right, and the gap is structural rather than a missing
# assertion: step 12's weapon drawn as its arms resolved to a real asset and drew
# successfully, so a wrong picture can be a well-formed picture and every count
# above stays green. This step does not judge the picture. It pins the instrument
# -- a frame can be taken at a chosen tick, the file is a BMP whose header agrees
# with its own size, the same static scene twice is byte-identical, and the demo
# scene differs from the paused one. Those four are the precondition for any later
# claim of the form "this change moved the picture rather than a counter".
#
# Nothing here is a SKIP: a missing binary, demo or interpreter exits non-zero, and
# the invalid-tick check asserts the refusal exit code 13, so a run that could not
# capture at all cannot report PASS by capturing nothing.
FRAMECAP="$OUT/16-frame-capture.txt"
bash frame-capture-check.sh > "$FRAMECAP" 2>&1
cat "$FRAMECAP"
FC_OK=$(grep -c '^  OK   ' "$FRAMECAP" || true)
# The assertion count is pinned for the same reason steps 8, 14 and 15 pin theirs:
# a gate that keeps printing PASS while its assertions quietly disappear is not a
# gate. 18 is the current count, and it is worth naming how it is arrived at
# because an earlier version of this line said 15 and the chain went red for
# exactly that reason -- the machine caught the author's miscount, not a defect:
# 2 exit codes (paused, paused-again) + 9 header identities + 1 non-flat colour
# + 1 positive extent + 1 determinism + 1 scene-sensitivity + 2 rejection
# + 2 for the second demo (differs from paused; and is shaded geometry rather than
#   a full-screen decal, added 2026-10-08 when the original demo was found to be
#   producing the fallback spray picture).
if grep -q 'FRAME-CAPTURE=PASS' "$FRAMECAP" && [ "${FC_OK:-0}" -eq 18 ]; then
  echo "FRAME-CAPTURE=PASS (assertions_ok=$FC_OK)"
else
  echo "FRAME-CAPTURE=FAIL (assertions_ok=${FC_OK:-0})"; rc_all=1
fi
# The content comparisons are what a later picture claim leans on, so the mutation
# flips one byte of the captured image and requires the hash comparison to notice.
FCMUT="$OUT/16-frame-capture-mutation.txt"
bash frame-capture-check.sh --mutation > "$FCMUT" 2>&1
if grep -q 'MUTATION-CAUGHT=PASS' "$FCMUT"; then
  echo "FRAME-CAPTURE-MUTATION=PASS (a one-byte change moves the content hash)"
else
  echo "FRAME-CAPTURE-MUTATION=FAIL (a one-byte change went unnoticed)"; rc_all=1
fi

step "17/21 resource reachability (what the demo scene can actually load)"
# Step 16 pins the instrument but never asks what is *in* the frame it takes. That
# gap was not hypothetical on this machine: the demo named in frame-capture-check.sh
# is koth_bagel_rc13, whose BSP is not installed here, so the renderer fell through
# to its six-vertex full-screen quad textured with the fallback `texture_` -- the
# vgui spray decal -- and step 16 called that "the demo scene differs from the paused
# scene" and passed. This step names the resource facts that tell the two apart:
# which of the five archives main.cpp hardcodes actually opened (it skips a missing
# one silently), whether the demo map's BSP is reachable at all, how many of its
# materials resolve to decodable pixels, and how many survive main.cpp's 512x512
# atlas cap -- because a material that resolves but is 1024x1024 is still not
# drawable, so the triangle share is the reading that predicts the picture.
#
# The readings are for cp_snakewater_final1, the one demo here whose map is installed
# (loose BSP). The bagel map is the negative case: its BSP is absent, which is what
# produced the spray-decal frame step 16 accepted.
RR="$OUT/17-resource-reachability.txt"
bash resource-reachability-check.sh > "$RR" 2>&1
cat "$RR"
RR_OK=$(grep -c '^  OK   ' "$RR" || true)
# Assertion count pinned for the same reason as steps 8, 14, 15 and 16. 12 is
# arrived at as: 1 archive count + 1 missing-archive name + 1 BSP reachable
# + 1 BSP source + 1 materials resolve + 3 for the 512 cap (eligible, rejected,
# triangles covered) + 1 negative control (the old gate's map has no BSP)
# + 3 for the counterfactual (triangles at a 1024 cap, triangles uncapped, and
# the check that raising the cap actually recovers geometry).
# An earlier version of this line said 9 while the gate emitted 8, and the chain
# went red for exactly that reason -- the same class of author miscount as step
# 16's first run, caught by the machine rather than by reading.
if grep -q 'RESOURCE-REACHABILITY=PASS' "$RR" && [ "${RR_OK:-0}" -eq 12 ]; then
  echo "RESOURCE-REACHABILITY=PASS (assertions_ok=$RR_OK)"
else
  echo "RESOURCE-REACHABILITY=FAIL (assertions_ok=${RR_OK:-0})"; rc_all=1
fi
# The archive count is the reading that would have caught the silent skip, so the
# mutation demands five archives and requires the check to notice that four opened.
RRMUT="$OUT/17-resource-reachability-mutation.txt"
bash resource-reachability-check.sh --mutation > "$RRMUT" 2>&1
if grep -q 'MUTATION-CAUGHT=PASS' "$RRMUT"; then
  echo "RESOURCE-REACHABILITY-MUTATION=PASS (a wrong archive-count expectation goes red)"
else
  echo "RESOURCE-REACHABILITY-MUTATION=FAIL (a wrong expectation went unnoticed)"; rc_all=1
fi

step "18/21 idle spin (the loop must sleep until the next frame is due)"
# Every step above reads what the program *produced*: counters, decoded values,
# resources, pixels. None of them could see the loop running hot between frames,
# because a hot loop produces the same output -- it just produces it while
# burning a core. The defect was one line (waiting at most 1 ms instead of until
# the frame was due), and at ~100 fps it meant ~1000 iterations a second, each
# one pushing a title and UI controls for state that had not moved. This step is
# the only one in the chain that reads the loop's rate rather than its output.
#
# The assertion is the wait itself: mean_wait_ms, which the fixed build reads at
# ~8.97 (its clamp is sized to the frame gap) and the defective build at exactly
# 1.00 (min(remainingMs, 1)). The mutation is a source-level counterfactual: it
# puts the 1 ms wait back, rebuilds, and requires the same >= 4 ms floor to go
# red.
#
# An earlier version of this step asserted main_loop_iterations per rendered
# frame instead, with a ceiling of 4 against a measured 2. That reading does not
# discriminate here -- with a 120 Hz target (8.33 ms) and a loop body costing
# several ms, elapsed has already passed the target when the wait returns in both
# builds, so both read 2.0 and the defective build passed the straight run. The
# chain caught it, but only through --mutation; the ratio is now a diagnostic and
# the wait is the assertion. This is the repo's own rule applied to itself: a
# reading whose margin is that wide has to be shown failing on the defect, not on
# a tightened threshold.
IDLE="$OUT/18-idle-spin.txt"
bash idle-spin-check.sh > "$IDLE" 2>&1
cat "$IDLE"
IDLE_OK=$(grep -c '^  OK   ' "$IDLE" || true)
# Assertion count pinned for the same reason as steps 8, 14, 15, 16 and 17: a
# gate that prints PASS while its assertions quietly disappear is not a gate.
# 11 is arrived at as: 3 in step 1 (mean wait floor, update ratio, metrics rate)
# + 2 in step 2 (playback advanced, both updaters fired) + 6 in step 3 (the six
# metric columns the assertions read). The idle sample also has to carry at least
# three metrics rows or the rate cannot be computed, and that is asserted inside
# the script (it fails) rather than counted here.
if grep -q 'IDLE-SPIN=PASS' "$IDLE" && [ "${IDLE_OK:-0}" -eq 11 ]; then
  echo "IDLE-SPIN=PASS (assertions_ok=$IDLE_OK)"
else
  echo "IDLE-SPIN=FAIL (assertions_ok=${IDLE_OK:-0})"; rc_all=1
fi
IDLEMUT="$OUT/18-idle-spin-mutation.txt"
bash idle-spin-check.sh --mutation > "$IDLEMUT" 2>&1
if grep -q 'MUTATION-CAUGHT=PASS' "$IDLEMUT"; then
  echo "IDLE-SPIN-MUTATION=PASS (the 1 ms wait reads 1.00, under the 4 ms floor)"
else
  echo "IDLE-SPIN-MUTATION=FAIL (the defect stayed at or above the floor)"; rc_all=1
fi

step "19/21 entity property lookup (the class sweep must not transform what it discards)"
# The class-fallback sweep in EntityModelResolver::buildInstances used to build a
# full transform for every player-named entity it walked, then throw the work away
# when the transform came back without a player class. extractTransform scans the
# entity's whole property map once per requested suffix, so the discarded entity
# paid for all of it. The fix asks the cheap question first -- one findProperty for
# m_iClass, the same suffix and value range extractTransform applies -- and only
# transforms what passes.
#
# The demos cannot witness this: on every demo in the corpus every player entity
# already carries a render request, so the sweep walks nothing past its `covered`
# check and the pre-check never fires. That is asserted (the demo sweep must read
# 0), and the witness is the probe's synthetic fixture, which now includes one
# entity in the shape the pre-check exists for -- player-named, no m_iClass. With
# the pre-check that fixture reads 6 transforms and 19 property comparisons; with
# it removed, 7 and 24. The instance count is 6 either way, which is the property
# that must not move: the pre-check has to spare work, not change the result.
#
# Usage: bash entity-property-lookup-check.sh [--mutation]
#   --mutation removes the pre-check, rebuilds and requires the fixture to read
#   the un-spared counts; the source is restored and the binary rebuilt on exit.
ENTITY_PROP="$OUT/19-entity-property-lookup.txt"
bash entity-property-lookup-check.sh > "$ENTITY_PROP" 2>&1
cat "$ENTITY_PROP"
ENTITY_PROP_OK=$(grep -c '^  OK   ' "$ENTITY_PROP" || true)
# Assertion count pinned for the same reason as steps 8, 14, 15, 16, 17 and 18: a
# gate that prints PASS while its assertions quietly disappear is not a gate.
# 10 is arrived at as: 4 in section 1 (fallbackScanned, classLookups, transforms,
# propertyComparisons) + 2 in section 2 (instanceCount, playerFallbacks) + 4 in
# section 3 (sweep-is-cold and instanceCount for each of two demos).
if grep -q 'ENTITY-PROPERTY-LOOKUP=PASS' "$ENTITY_PROP" && [ "${ENTITY_PROP_OK:-0}" -eq 10 ]; then
  echo "ENTITY-PROPERTY-LOOKUP=PASS (assertions_ok=$ENTITY_PROP_OK)"
else
  echo "ENTITY-PROPERTY-LOOKUP=FAIL (assertions_ok=${ENTITY_PROP_OK:-0})"; rc_all=1
fi
ENTITY_PROP_MUT="$OUT/19-entity-property-lookup-mutation.txt"
bash entity-property-lookup-check.sh --mutation > "$ENTITY_PROP_MUT" 2>&1
if grep -q 'MUTATION-CAUGHT=PASS' "$ENTITY_PROP_MUT"; then
  echo "ENTITY-PROPERTY-LOOKUP-MUTATION=PASS (the un-spared build reads 7 transforms)"
else
  echo "ENTITY-PROPERTY-LOOKUP-MUTATION=FAIL (the pre-check's absence moved nothing)"; rc_all=1
fi

step "20/21 entity material (a model must carry its own paint, past the entity boundary)"
ENTITY_MAT="$OUT/20-entity-material.txt"
bash entity-material-check.sh > "$ENTITY_MAT" 2>&1
cat "$ENTITY_MAT"
ENTITY_MAT_OK=$(grep -c '^  OK   ' "$ENTITY_MAT" || true)
# Assertion count pinned for the same reason as steps 8, 14, 15, 16, 17, 18 and
# 19: a gate that prints PASS while its assertions quietly disappear is not a
# gate. 24 is arrived at as: 3 per model in section 1 (declared slots, resolved
# slots, and the CD directory the stems were resolved against) x 5 models = 15,
# + 8 in section 2 (exit code, the tick the title was read at, entityMaterials,
# entityMaterialRanges, ledger row count, ledger resolved, ledger uploaded, and
# the title's model denominator against the ledger) + 1 in section 3.
if grep -q 'ENTITY-MATERIAL=PASS' "$ENTITY_MAT" && [ "${ENTITY_MAT_OK:-0}" -eq 24 ]; then
  echo "ENTITY-MATERIAL=PASS (assertions_ok=$ENTITY_MAT_OK)"
else
  echo "ENTITY-MATERIAL=FAIL (assertions_ok=${ENTITY_MAT_OK:-0})"; rc_all=1
fi
ENTITY_MAT_MUT="$OUT/20-entity-material-mutation.txt"
bash entity-material-check.sh --mutation > "$ENTITY_MAT_MUT" 2>&1
# The mutation puts the atlas-painted pixels back, so it is the one perturbation
# that targets the property this step is about. The source-level equivalent --
# material resolution returning no SRV at all -- is m12 in mutate.sh, which
# targets the instance-level counter instead of the pixels.
if grep -q 'MUTATION-CAUGHT=PASS' "$ENTITY_MAT_MUT"; then
  echo "ENTITY-MATERIAL-MUTATION=PASS (layer 3 reads the atlas pixels as the defect)"
else
  echo "ENTITY-MATERIAL-MUTATION=FAIL (the atlas-painted input was not caught)"; rc_all=1
fi

step "21/21 animation availability (a demo does not carry a player's sequence)"
ANIM_AVAIL="$OUT/21-animation-availability.txt"
bash animation-availability-check.sh > "$ANIM_AVAIL" 2>&1
cat "$ANIM_AVAIL"
# The assertion count is pinned for the same reason as steps 8 and 14-20: a gate
# that prints PASS while its assertions quietly disappear is not a gate. 23 is
# arrived at as: 5 per recording in section 1 (the class is found, plus the four
# property counts) x 3 recordings = 15, + 5 in section 2 (the rule can report
# presence, classesWithSequence, classesWithRate, classesWithCycle, and
# CTFWeaponBase carrying a slot) + 3 in section 3 (the oracle's DT_BaseAnimating
# block is non-empty, its animation-property count is 0, and its schema contains
# CTFPlayer).
ANIM_AVAIL_OK=$(grep -c '^  OK   ' "$ANIM_AVAIL" || true)
if grep -q 'ANIMATION-AVAILABILITY=PASS' "$ANIM_AVAIL" && [ "${ANIM_AVAIL_OK:-0}" -eq 23 ]; then
  echo "ANIMATION-AVAILABILITY=PASS (assertions_ok=$ANIM_AVAIL_OK)"
else
  echo "ANIMATION-AVAILABILITY=FAIL (assertions_ok=${ANIM_AVAIL_OK:-0})"; rc_all=1
fi
# The in-script mutation is the cheap half: it rewrites the captured output and
# requires each assertion's extractor to see the rewrite, which is what catches a
# regex that silently returns nothing (the ' pose=' field did exactly that on the
# first run). The rebuilt, source-level half is m13 in mutate.sh, which neuters
# the sweep and requires the positive control to collapse to 0.
ANIM_AVAIL_MUT="$OUT/21-animation-availability-mutation.txt"
ANIM_AVAILABILITY_OUT="$OUT/21-animation-availability-mutation-evidence" \
  bash animation-availability-check.sh --mutation > "$ANIM_AVAIL_MUT" 2>&1
if grep -q 'MUTATION-CAUGHT=PASS' "$ANIM_AVAIL_MUT"; then
  echo "ANIMATION-AVAILABILITY-MUTATION=PASS (every rewritten reading reached its extractor)"
else
  echo "ANIMATION-AVAILABILITY-MUTATION=FAIL (a reading was assumed rather than extracted)"; rc_all=1
fi

printf '\n'
if [ "$rc_all" -eq 0 ]; then echo "VERIFY=PASS"; else echo "VERIFY=FAIL"; fi
exit "$rc_all"

