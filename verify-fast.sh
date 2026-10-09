#!/usr/bin/env bash
# verify-fast.sh -- tier 1 of the tiered acceptance: the checks that run after
# every small change.
#
# WHY THIS EXISTS
#   Until 2026-10-09 the one-hour chain (verify-all.sh) was, in practice, the
#   only gate. That is backwards. Twelve of its nineteen steps decode whole
#   demos, run the independent oracle, capture frames, or mutate the source, and
#   not one of them can answer the question a small edit actually raises: did the
#   file still compile, and do the probes' own invariants still hold? Paying an
#   hour to learn that is how a round ends up with two acceptance runs in a day
#   and none in between.
#
#   This script answers that question in seconds. It builds the program and the
#   self-checking probes, then runs their --self-test entry points. Nothing here
#   reads a demo, opens a window, or touches an audio device.
#
# WHAT IS ASSERTED, AND WHY NOT JUST THE EXIT STATUS
#   Every probe below exits 0 when its self-test passes, so `rc=0` looks like a
#   sufficient assertion. It is not: this repo has four recorded cases of a gate
#   that printed PASS while checking nothing (a SKIP-everything additive check,
#   a fixture step that never asserted a fixture ran, a build step that never
#   asserted an exe was produced, a mutation step that never asserted a case
#   ran). The assertions here are therefore on *named values and work counts* --
#   `refs=11`, `cases=11`, `shapes=4`, `truncated_varint=1` -- so a probe that
#   loses a fixture, or a key that quietly disappears, reads red.
#
#   The counts are pinned as deliberate edits, the same convention as
#   verify-all.sh's `exe_count=23` and `fixture_pass_count=58`. When a fixture
#   legitimately grows, this list is updated on purpose and the diff says so.
#
# THE ARGUMENT-PARSING TRAP THIS SCRIPT DOCUMENTS
#   `item_schema_probe --self-test` is *not* a self-test. That probe takes a path
#   to items_game.txt as argv[1] and has no --self-test branch, so the flag is
#   read as a filename: it prints `error: cannot open items_game.txt: --self-test`
#   and **returns 0**. A caller that checked the exit status would record a
#   passing self-test that never ran. The real entry point is
#   `--wire-self-test`. This is written down here because "harmonise the flags"
#   is exactly the cleanup that would reintroduce it.
#
# USAGE
#   bash verify-fast.sh              # incremental build + the five self-tests
#   bash verify-fast.sh --mutation   # perturb each captured output and require
#                                    # the assertions to go red (discriminating
#                                    # power, not a second opinion)
#   bash verify-fast.sh --no-build   # skip the build; the probes are current
#
#   Mutation is off by default and is not needed after every edit: it is for when
#   the assertion logic itself changed, not when the code under it did.
#
# Exit: 0 = every target built and every assertion held.
set -uo pipefail
cd "$(dirname "$0")"

OUT="${VERIFY_FAST_OUT:-evidence/fast}"
mkdir -p "$OUT"

MUTATION=0
BUILD=1
for arg in "$@"; do
  case "$arg" in
    --mutation) MUTATION=1 ;;
    --no-build) BUILD=0 ;;
    *) echo "unknown argument: $arg"; exit 2 ;;
  esac
done

# 36 assertions across the five self-tests. Pinned for the same reason as the
# chain's per-step counts: an assertion list that quietly shrinks is not a gate.
FAST_ASSERTIONS=36

# The build is the first assertion of this tier and its failure is reported by
# build-target.sh's exit status; nothing else here can be trusted if it failed.
if [ "$BUILD" -eq 1 ]; then
  echo "=== 1/2 incremental build ==="
  # tf2_demo_native is included even though this tier never runs it: a change to
  # a shared .cpp can compile into a probe and still break the program, and the
  # whole point of a fast tier is to learn that now rather than in an hour.
  bash build-target.sh tf2_demo_native presentation_probe entity_model_probe \
    world_material_probe model_pose_probe item_schema_probe
  rc_build=$?
  echo "build_rc=$rc_build"
  if [ "$rc_build" -ne 0 ]; then echo "VERIFY-FAST=FAIL (build)"; exit 1; fi
else
  echo "=== 1/2 incremental build (skipped: --no-build) ==="
fi

echo
echo "=== 2/2 probe self-tests ==="

B=native/build-nmake
ok=0
bad=0

assert_line() { # assert_line <file> <fixed string> <what>
  local file="$1" needle="$2" what="$3"
  if [ ! -s "$file" ]; then
    bad=$((bad + 1)); printf '  FAIL %s (no output captured)\n' "$what"; return
  fi
  if grep -qF -- "$needle" "$file"; then
    ok=$((ok + 1)); printf '  OK   %s\n' "$what"
  else
    bad=$((bad + 1)); printf '  FAIL %s (wanted: %s)\n' "$what" "$needle"
  fi
}

run_expectations() { # run_expectations <file> <needle|what> ...
  local file="$1"; shift
  local e
  for e in "$@"; do assert_line "$file" "${e%%|*}" "${e#*|}"; done
}

# --- the expectation lists -------------------------------------------------
# Each entry is "<fixed string to find>|<what it means>". They are arrays so the
# --mutation path can re-run the *same* list against a perturbed capture: the
# mutation has to exercise the shipped assertions, not a parallel copy of them.

E_PRESENTATION=(
  '"selfTest":true|presentation: the self-test ran'
  '"viewMath":true|presentation: view math'
  '"recording":true|presentation: recording classification'
  '"projectileFields":true|presentation: projectile fields'
  '"fullSpanCheckpoint":true|presentation: full-span seek'
  '"historyCoverage":true|presentation: history coverage'
  '"historyWorstGap":4096|presentation: worst gap is a measured 4096, not a default'
  '"historySlotFloor":3565|presentation: pigeonhole floor is a measured 3565'
  '"cameraFormula":"source-pitch-down"|presentation: camera formula is the Source one'
  '"seekOutsideWindow":"checkpoint"|presentation: out-of-window seek answers Checkpoint'
)

E_ENTITY_MODEL=(
  'weapon-wiring-fixture refs=11|entity_model: weapon fixture saw 11 references'
  'armsWeapon=1|entity_model: the fixture still contains one weapon-naming-its-arms witness'
  'observer-focus-fixture cases=11|entity_model: observer fixture ran 11 cases'
  'follows=2 serial=867|entity_model: observer fixture resolved 2 follows, serial 867'
  'slot-freshness-fixture shapes=4 freshChosen=2|entity_model: slot fixture ran 4 shapes, 2 fresh chosen'
  'tickRoundTrip=4|entity_model: slot fixture round-tripped 4 ticks'
  '"ok":true|entity_model: the self-test verdict'
  '"selfTest":true|entity_model: the self-test ran'
)

E_WORLD_MATERIAL=(
  '"selfTest":true|world_material: the self-test ran'
  '"emptyBspRejected":true|world_material: an empty BSP is rejected'
  '"noLightingDefault":true|world_material: no-lighting is the default'
  '"lightmapNames":true|world_material: lightmap naming'
  '"skyPath":true|world_material: sky path'
  '"visUnavailableOpen":true|world_material: unavailable VIS is reported, not fatal'
  '"cubemapMode":"approximate-2d"|world_material: cubemap mode is the honest 2d approximation'
)

E_MODEL_POSE=(
  '"attachmentOutOfBoundsRejected":true|model_pose: out-of-bounds attachment rejected'
  '"bodyPartOutOfBoundsRejected":true|model_pose: out-of-bounds body part rejected'
  '"boneTableOutOfBoundsRejected":true|model_pose: out-of-bounds bone table rejected'
  '"bindPoseMutationRejected":true|model_pose: bind-pose mutation rejected'
  '"cpuSkinWeightMutationRejected":true|model_pose: CPU skin-weight mutation rejected'
  '"sequenceTableOutOfBoundsRejected":true|model_pose: out-of-bounds sequence table rejected'
  # false is the expected value here, and the name reads backwards on purpose:
  # the key answers "did the corrupt pose pass the preflight?". The probe returns
  # 0 only when both preflights rejected it, so `false` means the guard worked.
  '"corruptPosePreflight":false|model_pose: the corrupt pose did NOT pass the preflight'
)

E_ITEM_SCHEMA=(
  'truncated_varint=1|item_schema: one truncated-varint case ran'
  'truncated_length=1|item_schema: one truncated-length case ran'
  'valid=1|item_schema: one valid case ran'
  'status=pass|item_schema: the wire self-test verdict'
)

# --- run them --------------------------------------------------------------
"$B/presentation_probe.exe"  --self-test > "$OUT/presentation-probe.txt" 2>&1
"$B/entity_model_probe.exe"  --self-test > "$OUT/entity-model-probe.txt" 2>&1
"$B/world_material_probe.exe" --self-test > "$OUT/world-material-probe.txt" 2>&1
"$B/model_pose_probe.exe"    --self-test > "$OUT/model-pose-probe.txt" 2>&1
"$B/item_schema_probe.exe"   --wire-self-test > "$OUT/item-schema-probe.txt" 2>&1

echo "-- presentation_probe --self-test"
run_expectations "$OUT/presentation-probe.txt"  "${E_PRESENTATION[@]}"
echo "-- entity_model_probe --self-test"
run_expectations "$OUT/entity-model-probe.txt"  "${E_ENTITY_MODEL[@]}"
echo "-- world_material_probe --self-test"
run_expectations "$OUT/world-material-probe.txt" "${E_WORLD_MATERIAL[@]}"
echo "-- model_pose_probe --self-test"
run_expectations "$OUT/model-pose-probe.txt"    "${E_MODEL_POSE[@]}"
echo "-- item_schema_probe --wire-self-test"
run_expectations "$OUT/item-schema-probe.txt"   "${E_ITEM_SCHEMA[@]}"

echo "assertions_ok=$ok assertions_bad=$bad"

if [ "$MUTATION" -eq 1 ]; then
  echo
  echo "=== mutation: perturb each capture and require its assertions to go red ==="
  # Each mutation replaces a *required* value with a wrong one in a copy of the
  # real capture, then re-runs the shipped expectation list against the copy. The
  # sed is required to change the file: if the needle is already absent, the real
  # run is already red and this check would pass for the wrong reason, so that
  # case is reported as a setup failure instead.
  mrc=0
  mutate_case() { # mutate_case <name> <file> <sed-expression> <assertions...>
    local name="$1" file="$2" expr="$3"; shift 3
    local copy="$OUT/mutated-$name.txt"
    cp "$file" "$copy"
    sed -i "$expr" "$copy"
    if cmp -s "$file" "$copy"; then
      echo "  MUTATION-SETUP=FAIL ($name: the perturbation changed nothing; the real capture does not contain the needle)"
      mrc=1; return
    fi
    local saved_ok=$ok saved_bad=$bad
    ok=0; bad=0
    run_expectations "$copy" "$@" > "$OUT/mutated-$name.log" 2>&1
    local mok=$ok mbad=$bad
    ok=$saved_ok; bad=$saved_bad
    if [ "$mbad" -ge 1 ]; then
      printf '  MUTATION-CAUGHT=PASS (%s: %s of %s assertions went red)\n' "$name" "$mbad" "$((mok + mbad))"
    else
      printf '  MUTATION-CAUGHT=FAIL (%s: no assertion moved; they do not discriminate)\n' "$name"
      mrc=1
    fi
  }
  mutate_case presentation  "$OUT/presentation-probe.txt" 's/"selfTest":true/"selfTest":false/'  "${E_PRESENTATION[@]}"
  mutate_case entity_model  "$OUT/entity-model-probe.txt" 's/refs=11/refs=12/'                   "${E_ENTITY_MODEL[@]}"
  mutate_case world_material "$OUT/world-material-probe.txt" 's/"skyPath":true/"skyPath":false/'  "${E_WORLD_MATERIAL[@]}"
  mutate_case model_pose    "$OUT/model-pose-probe.txt" 's/"bindPoseMutationRejected":true/"bindPoseMutationRejected":false/' "${E_MODEL_POSE[@]}"
  mutate_case item_schema   "$OUT/item-schema-probe.txt" 's/valid=1/valid=0/'                   "${E_ITEM_SCHEMA[@]}"
  [ "$mrc" -eq 0 ] || { echo "VERIFY-FAST-MUTATION=FAIL"; exit 1; }
  echo "VERIFY-FAST-MUTATION=PASS (all five expectation lists can go red)"
fi

echo
if [ "$bad" -eq 0 ] && [ "$ok" -eq "$FAST_ASSERTIONS" ]; then
  echo "VERIFY-FAST=PASS (assertions_ok=$ok)"
  exit 0
fi
echo "VERIFY-FAST=FAIL (assertions_ok=$ok, assertions_bad=$bad, expected $FAST_ASSERTIONS assertions)"
exit 1
