#!/usr/bin/env bash
# weapon-body-check.sh -- the static weapon body, the second draw request.
#
# WHAT THIS GATE IS FOR
#   The modern TF2 first person is a COMPOSITE: the ViewModel entity names a pair
#   of arms through its own m_nModelIndex, and the weapon ENTITY that entity's
#   m_hWeapon slot points at names the static weapon body through
#   DT_BaseCombatWeapon.m_iWorldModelIndex. T2 drew the arms. This round adds the
#   body, as a second, independent draw request next to them.
#
#   A round that adds a draw can fail in two directions, and the gate has to be
#   able to see both. It can fail by NOT drawing (the body is resolved but never
#   submitted) and it can fail by drawing the WRONG thing -- and the wrong thing
#   here is specific: a weapon carries m_nModelIndex and m_iWorldModelIndex next
#   to each other in the send table, the wrong one still resolves to a real,
#   drawable file (the arms), and drawing it puts a second pair of hands in front
#   of the camera while every count-based reading stays green. That is exactly
#   what the world pass did before cd36db1.
#
# WHAT EACH SECTION ESTABLISHES
#   Section 0  the input is pinned by size and sha256, so a changed recording is
#              a FAIL rather than a silently different measurement.
#   Section 1  the lookup's refusals, on fixtures. The shipped recording reaches
#              the success path and nothing else, so the negatives -- a zero
#              world index, an index that resolves to the arms, a weapon entity
#              named but absent from the snapshot, a ViewModel entity carrying no
#              weapon slot -- are constructed one at a time, each as the accepted
#              fixture with exactly one field changed. Each negative is paired
#              with a control that the change cannot move.
#   Section 2  the same chain on the game's own files: three weapon bodies whose
#              materials resolve all the way to a .vtf, and the recording's own
#              ViewModel entity -> weapon entity -> model path.
#   Section 3  the product: TWO draw requests, counted separately. The arms'
#              counter and the body's counter both have to move, and the reading
#              has to name the model the entity's own index named.
#   Section 4  the picture. The body's draw has to change pixels, and the change
#              has to be attributable to the body: two runs of the same
#              configuration are byte-identical (so the renderer is
#              deterministic), and the difference appears only when the body is
#              switched off. Without the determinism pair, "the body changed the
#              picture" would be a claim about a difference that could have come
#              from anywhere.
#
#              The frames are taken at ONE tick -- the tick the runs REQUEST, not
#              wherever the first one happened to land. `--capture-tick N` means
#              "at or past N", so the landing tick is a fact about machine load:
#              nine runs at 2000 landed on 2000 seven times and on 2001 twice, and
#              two ticks of a moving camera differ by 399659 bytes -- more than
#              the body itself. So each frame is taken again until it lands on the
#              requested tick, and every comparison below re-reads that tick out
#              of the run's own trace instead of assuming it.
#   Section 5  the body is built once, not per frame. A per-frame MDL parse or
#              mesh upload is the defect the plan names, and it would look
#              identical to a correct run in every picture.
#   Section 6  mutation (--mutation only): four defects, each requiring a NAMED
#              assertion to go red, a NAMED control to hold, and the source
#              restored byte-for-byte with a successful rebuild.
#
# Usage: bash weapon-body-check.sh [--mutation]
#   --mutation reintroduces each defect in the product's own source, rebuilds,
#   and requires the named reading to move while the named control holds. It
#   prints MUTATION-CAUGHT=... rather than the ordinary verdict, because under
#   --mutation the sections are supposed to be red.
#
#   The restore is a byte copy rather than `git checkout`, and that is deliberate:
#   `git checkout` reverts to HEAD, so a run on a tree with uncommitted work would
#   destroy that work while claiming to restore it. The byte copy is also the
#   stronger claim -- every mutation's restore is verified against the hash the
#   file had before the patch, and the rebuild that follows is asserted, not
#   assumed.
#
# Exit:  0 = every assertion held (or, under --mutation, all four were caught),
#        1 = it did not, 2 = another run owns the lock or a required input is gone.
#
# One run at a time: the script writes frames and traces under one directory and
# launches the product several times, so two concurrent runs overwrite each
# other's evidence. mkdir is atomic, so a second caller fails before it runs
# anything.
set -uo pipefail
cd "$(dirname "$0")"

# ---------------------------------------------------------------------------
# Arguments
# ---------------------------------------------------------------------------
MUTATION=0
while [ $# -gt 0 ]; do
  case "$1" in
    --mutation) MUTATION=1 ;;
    *) echo "unknown argument: $1"; exit 2 ;;
  esac
  shift
done

EXE=native/build-nmake/tf2_demo_native.exe
PROBE=native/build-nmake/weapon_body_probe.exe
TF="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
# The POV recording viewmodel-animation-check.sh and viewmodel-companion-check.sh
# both pin, and the one whose tick 16306 the chain was measured on. It is pinned
# here by size and sha256 rather than found by a glob: the Steam demos directory
# is LIVE -- recordings land in it while a round is being worked on -- and "the
# input is not here" and "the input is clean" must not read the same.
POV_DEMO="$TF/demos/autorecord_2026-07-02_13-26-46.dem"
POV_SIZE=21856667
POV_SHA=c8a58501d222bdd9aec6db92d2782931a29a13d963a87364d603263e6dab091e

# The model the pass is TOLD to draw with --viewmodel. It is the control, not the
# picture: the body comes from the recording's own entity chain.
VM_MODEL="models/weapons/v_models/v_scattergun_scout.mdl"
# The composite the recording's ViewModel entity names (its arms), the weapon
# body its weapon entity names, and a second real weapon body for the material
# chain, so "weapons resolve" is not one file's accident.
ARM_MODEL="models/weapons/c_models/c_sniper_arms.mdl"
WEAPON_MODEL="models/workshop/weapons/c_models/c_pro_rifle/c_pro_rifle.mdl"
MEDIGUN_MODEL="models/weapons/c_models/c_medigun/c_medigun.mdl"
# The entity indices and the world model index the POV recording carries at tick
# 16306, measured 2026-10-12: ViewModel entity 1077, weapon entity 1008
# (CTFSniperRifle), m_iWorldModelIndex 533 -> c_pro_rifle.mdl. Pinned because
# they are what "the body came from the entity's own index" means.
EXPECT_VIEWMODEL_ENTITY=1077
EXPECT_WEAPON_ENTITY=1008
EXPECT_WORLD_MODEL_INDEX=533
EXPECT_WEAPON_CLASS=CTFSniperRifle
# The OTHER index on that same weapon entity, m_nModelIndex (== m_iViewModelIndex),
# measured 2026-10-12 at tick 2000: 1032, which resolves to the arms. m1 reads it
# where the world index belongs, so this is the value the reading has to report
# once the slot is swapped -- and reporting it is what makes the swapped slot a
# measured fact rather than an inference from the refusal.
EXPECT_WEAPON_VIEW_MODEL_INDEX=1032
# The tick the frames are taken at: early enough that the recording has settled
# and the chain is live on it, and reached in about 90 s of playback.
CAPTURE_TICK=2000
TRACE_DIR=evidence/fast/weapon-body

# Git may check this script out with CRLF; strip the carriage return from path
# variables before passing them to native tools or filesystem tests.
TF=${TF%$'\r'}
POV_DEMO=${POV_DEMO%$'\r'}
EXE=${EXE%$'\r'}
PROBE=${PROBE%$'\r'}
VM_MODEL=${VM_MODEL%$'\r'}
ARM_MODEL=${ARM_MODEL%$'\r'}
WEAPON_MODEL=${WEAPON_MODEL%$'\r'}
TRACE_DIR=${TRACE_DIR%$'\r'}

mkdir -p .scratch
LOCK_DIR=.scratch/weapon-body.lock
if ! mkdir "$LOCK_DIR" 2>/dev/null; then
  echo "WEAPON-BODY=FAIL (another weapon-body-check run owns $LOCK_DIR)"
  exit 2
fi

# ---------------------------------------------------------------------------
# Source mutation plumbing
# ---------------------------------------------------------------------------
MUTATION_FILE=""
MUTATION_BAK=".scratch/weapon-body-mutation.bak"
MUTATION_ORIG_SHA=""
MUTATION_ACTIVE=0

restore_mutation() {
  if [ "$MUTATION_ACTIVE" -eq 1 ] && [ -n "$MUTATION_FILE" ] && [ -f "$MUTATION_BAK" ]; then
    cp "$MUTATION_BAK" "$MUTATION_FILE"
  fi
}
on_exit() {
  restore_mutation
  rmdir "$LOCK_DIR" 2>/dev/null || true
}
trap on_exit EXIT

PY=""
for candidate in \
  "C:/Users/Administrator/.workbuddy-ai/binaries/python/versions/3.13.12/python.exe" \
  "C:/Users/Administrator/.workbuddy/binaries/python/versions/3.13.12/python.exe" \
  python; do
  if command -v "$candidate" >/dev/null 2>&1 || [ -x "$candidate" ]; then PY="$candidate"; break; fi
done
[ -n "$PY" ] || { echo "FATAL: no python interpreter found for the mutation patcher"; exit 2; }

# The trace path is handed to a native Windows binary, which does not translate
# MSYS paths: "/d/..." is opened literally and the trace silently never opens,
# which reads exactly like "the product produced no body". Convert once.
ROOT=$(cygpath -m "$(pwd)" 2>/dev/null || pwd)
case "$ROOT" in
  /[a-zA-Z]/*) echo "FATAL: could not convert '$ROOT' to a Windows path"; exit 2 ;;
esac
mkdir -p "$TRACE_DIR"

# ---------------------------------------------------------------------------
# Assertions and trace readers
# ---------------------------------------------------------------------------
# Audio: the device is opened during startup and pausing does not stop it, so
# every launch names it. The default device is the wrong one on this machine.
# The window is hidden and the audio disabled -- the instrumentation
# quiet-capture-tests added -- because this gate takes frames and nothing it
# measures depends on either: measured 2026-10-12, a hidden run's frame is
# byte-identical to a visible one, and section 4 asserts exactly that, so the two
# switches cannot quietly become inputs.
AUDIO_DEVICE=6
RUN_TIMEOUT=400

fail=0
FAIL_LINES=()
assert_eq() {
  if [ "${2:-<none>}" = "$3" ]; then
    echo "  OK   $1 = $3"
  else
    echo "  FAIL $1 = ${2:-<none>}, expected $3"; fail=1; FAIL_LINES+=("$1: ${2:-<none>} != $3")
  fi
}
assert_ne() {
  if [ -n "${2:-}" ] && [ "${2:-}" != "$3" ]; then
    echo "  OK   $1 = $2 (!= $3)"
  else
    echo "  FAIL $1 = ${2:-<none>}, expected anything but $3"; fail=1; FAIL_LINES+=("$1: ${2:-<none>} == $3")
  fi
}
assert_ge() {
  if [ "${2:-x}" -ge "$3" ] 2>/dev/null; then
    echo "  OK   $1 = $2 (>= $3)"
  else
    echo "  FAIL $1 = ${2:-<none>}, expected >= $3"; fail=1; FAIL_LINES+=("$1: ${2:-<none>} < $3")
  fi
}
assert_gt() {
  if [ "${2:-x}" -gt "$3" ] 2>/dev/null; then
    echo "  OK   $1 = $2 (> $3)"
  else
    echo "  FAIL $1 = ${2:-<none>}, expected > $3"; fail=1; FAIL_LINES+=("$1: ${2:-<none>} <= $3")
  fi
}
# The same, for the readings the product prints as decimals (a mesh radius).
# `-gt` is integer-only and silently fails on `27.53`, which reads exactly like a
# radius of zero.
assert_gt_float() {
  if awk -v a="${2:-}" -v b="$3" 'BEGIN { exit !(a > b) }' 2>/dev/null; then
    echo "  OK   $1 = $2 (> $3)"
  else
    echo "  FAIL $1 = ${2:-<none>}, expected > $3"; fail=1; FAIL_LINES+=("$1: ${2:-<none>} <= $3")
  fi
}
assert_contains() {
  case "${2:-}" in
    *"$3"*) echo "  OK   $1 contains $3" ;;
    *) echo "  FAIL $1 = ${2:-<none>}, expected to contain $3"
       fail=1; FAIL_LINES+=("$1: ${2:-<none>} missing $3") ;;
  esac
}
assert_not_contains() {
  case "${2:-}" in
    *"$3"*) echo "  FAIL $1 = ${2:-<none>}, expected NOT to contain $3"
            fail=1; FAIL_LINES+=("$1: ${2:-<none>} contains $3") ;;
    *) echo "  OK   $1 does not contain $3" ;;
  esac
}

# field <line> <name> -> the value of name=<value> in a trace line, "" if absent.
field() {
  printf '%s\n' "$1" | tr ' ' '\n' | sed -n "s/^$2=//p" | head -1
}
# byte_diff <a> <b> -> how many byte positions differ, or -1 if a file is missing.
byte_diff() {
  if [ ! -s "$1" ] || [ ! -s "$2" ]; then echo -1; return; fi
  cmp -l "$1" "$2" 2>/dev/null | wc -l | tr -d ' '
}

# ---------------------------------------------------------------------------
# Runs
# ---------------------------------------------------------------------------
LAST_RC=0
LAST_READING=""
LAST_PROBE_FILE=""
LAST_PROBE_JSON=""

# The two instrumentation switches quiet-capture-tests added: this gate takes
# frames and nothing it measures depends on a visible window or an open audio
# device. `quiet-flags` is a variable rather than a constant because section 4
# has to be able to run WITHOUT them once -- otherwise "the switches do not change
# the picture" would be an assumption inside every other reading here.
QUIET_FLAGS="--hidden-window --audio-disabled"

run_product() { # run_product <name> [extra...]
  local name="$1"; shift
  local bmp="$TRACE_DIR/$name.bmp"
  rm -f "$bmp" "$TRACE_DIR/$name.csv"
  TF2_NATIVE_TRACE="$ROOT/$TRACE_DIR/$name.trace" timeout "$RUN_TIMEOUT" "$EXE" \
    --tf-root "$TF" --audio-device "$AUDIO_DEVICE" $QUIET_FLAGS \
    --demo "$POV_DEMO" \
    --settings-file "$TRACE_DIR/$name-settings.ini" \
    --capture-frame "$bmp" --capture-tick "$CAPTURE_TICK" \
    --metrics-file "$TRACE_DIR/$name.csv" \
    --viewmodel "$VM_MODEL" --viewmodel-fov 90 "$@" > "$TRACE_DIR/$name.log" 2>&1
  LAST_RC=$?
  LAST_READING=$(grep -a "gpu: viewmodel weaponBodyReading" "$TRACE_DIR/$name.trace" 2>/dev/null \
    | tr -d '\r' | tail -1 | sed 's/^[0-9]\{1,\}\(\.[0-9]*\)\? //')
  echo "  $name rc=$LAST_RC bmp=$(stat -c%s "$bmp" 2>/dev/null || echo 0)"
}

run_probe() { # run_probe <name> [extra...]  -- JSON on stdout, checks on stderr
  local name="$1"; shift
  LAST_PROBE_FILE="$TRACE_DIR/$name.json"
  "$PROBE" "$@" > "$LAST_PROBE_FILE" 2> "$TRACE_DIR/$name.txt"
  LAST_RC=$?
  LAST_PROBE_JSON=$(cat "$LAST_PROBE_FILE" 2>/dev/null)
  echo "  $name rc=$LAST_RC checks_ok=$(grep -c '^  OK' "$TRACE_DIR/$name.txt" 2>/dev/null || true) checks_failed=$(grep -c '^  FAIL' "$TRACE_DIR/$name.txt" 2>/dev/null || true)"
}
# probe_field <json-file> <key> -- one top-level key out of the probe's JSON.
# Parsed rather than scanned: one of the values is itself a comma-separated list
# (`refusalsExercised` is spelled `name:count,name:count`), so a scan that split
# on commas would cut that value in half and every membership assertion over it
# would then be a statement about its first element.
#
# Values are re-encoded through json.dumps so the shell sees the same spelling the
# file does: Python prints its booleans as `True`, and an assertion expecting the
# JSON `true` would fail on a correct run -- which is how this read the first time.
probe_field() {
  "$PY" -c 'import json, sys
try:
    value = json.load(open(sys.argv[1], encoding="utf-8")).get(sys.argv[2], "")
    print(value if isinstance(value, str) else json.dumps(value))
except Exception:
    print("")' "$1" "$2"
}

# capture_tick_of <name> -> the tick the frame was actually taken at.
#
# `--capture-tick N` asks for the FIRST draw whose tick is at or past N, so where
# it lands is a fact about how much wall time the frames before it took, not a
# constant. Measured on this machine: four runs at --capture-tick 2000 landed on
# 2000, 2000, 2000 and 2002. Two ticks of a moving camera is a different picture
# -- 399659 differing bytes, honestly measured before this was understood -- and
# that difference would have been read as "the body changed the picture" in a
# comparison that could not attribute it. So every comparison here names the tick
# it was taken at and requires the other side to match it.
capture_tick_of() {
  grep -a "draw: begin tick=" "$TRACE_DIR/$1.trace" 2>/dev/null | tr -d '\r' | tail -1 \
    | sed -n 's/.*draw: begin tick=\([-0-9]*\).*/\1/p'
}
# run_product_until_tick <name> <wanted-tick> [extra...]
#
# Every frame in this gate is aligned on the REQUESTED tick, not on wherever the
# first frame happened to land, and the difference is what made the first version
# of this gate fail honestly: it anchored on the first run's landing spot (2001
# that time) while `run_product` kept asking for 2000, so the comparison frames
# could never match and three retries burned on a condition that could not change.
#
# Measured on this machine at --capture-tick 2000: nine runs landed 2000, 2000,
# 2000, 2000, 2000, 2000, 2000, 2001, 2001. 2000 is the mode, so it is the anchor
# and the retry moves a ~78% per-attempt hit rate to ~99.95% over five attempts.
# When a frame does land elsewhere it is dropped and the whole run repeats: the
# retry is a retry of the OBSERVING CONDITION, not of a measurement, and the
# assertion after each call re-reads the tick rather than trusting this function.
run_product_until_tick() {
  local name="$1" wanted="$2"; shift 2
  local attempt got
  for attempt in 1 2 3 4 5; do
    run_product "$name" "$@"
    got=$(capture_tick_of "$name")
    if [ "$got" = "$wanted" ]; then
      echo "  ..   $name: frame landed on tick $got (attempt $attempt)"
      return 0
    fi
    echo "  ..   $name: frame landed on tick ${got:-<none>}, wanted $wanted (attempt $attempt)"
  done
  echo "  ..   $name: gave up after 5 attempts"
  return 1
}

# ---------------------------------------------------------------------------
# Section 0 -- pinned input
# ---------------------------------------------------------------------------
echo "=== 0/6 pinned input ==="
[ -f "$EXE" ] || { echo "FATAL: missing $EXE"; exit 2; }
[ -f "$PROBE" ] || { echo "FATAL: missing $PROBE"; exit 2; }
POV_ACTUAL_SIZE=$(stat -c%s "$POV_DEMO" 2>/dev/null || echo 0)
assert_eq "the recording's size" "$POV_ACTUAL_SIZE" "$POV_SIZE"
POV_ACTUAL_SHA=$(sha256sum "$POV_DEMO" 2>/dev/null | cut -d' ' -f1)
assert_eq "the recording's sha256" "$POV_ACTUAL_SHA" "$POV_SHA"

# ---------------------------------------------------------------------------
# Section 1 -- the lookup's refusals, on fixtures
# ---------------------------------------------------------------------------
echo
echo "=== 1/6 the refusals, one at a time ==="
run_probe selftest --self-test
assert_eq "the probe's self-test rc" "$LAST_RC" "0"
assert_eq "the self-test reports ok" "$(probe_field "$LAST_PROBE_FILE" ok)" "true"
assert_eq "the self-test reports itself as such" "$(probe_field "$LAST_PROBE_FILE" selfTest)" "true"
# The battery's size, pinned: a check that stopped being constructed would
# otherwise just do nothing, and a battery that silently shrank is the same
# reading as one that passed.
assert_eq "checks that ran (the battery is pinned)" "$(probe_field "$LAST_PROBE_FILE" checksPassed)" "57"
assert_eq "checks that failed" "$(probe_field "$LAST_PROBE_FILE" checksFailed)" "0"
# Every refusal in the enum was constructed. Read as a set, not as a count: a
# count of 12 would also be reached by twelve spellings of the same verdict.
REFUSALS=$(probe_field "$LAST_PROBE_FILE" refusalsExercised)
for refusal in none worldModelIndexZero worldModelNotDeclared worldModelIndexOutOfRange \
               worldModelIsArms weaponAbsent weaponPastSnapshot noWeaponSlot \
               emptyWeaponHandle noViewModelEntity viewModelEntityAbsent noWorldModelSlot; do
  assert_contains "the self-test constructed the refusal" "$REFUSALS" "$refusal:"
done

# ---------------------------------------------------------------------------
# Section 2 -- the same chain on the game's own files
# ---------------------------------------------------------------------------
echo
echo "=== 2/6 the chain on the game's own files ==="
run_probe real --tf-root "$TF" --model "$WEAPON_MODEL" --model "$MEDIGUN_MODEL" \
  --model "$ARM_MODEL" --demo "$POV_DEMO" --tick 16306
assert_eq "the probe's real-file rc" "$LAST_RC" "0"
assert_eq "three weapon-side models resolved" "$(probe_field "$LAST_PROBE_FILE" materialModelsTested)" "3"
assert_eq "and every one of their material slots reached a texture" \
  "$(probe_field "$LAST_PROBE_FILE" materialVtfResolved)" \
  "$(probe_field "$LAST_PROBE_FILE" materialSlots)"
assert_ge "work count -- material slots walked" "$(probe_field "$LAST_PROBE_FILE" materialSlots)" "3"
assert_eq "the recording's own ViewModel entity was selected" \
  "$(probe_field "$LAST_PROBE_FILE" viewModelEntity)" "$EXPECT_VIEWMODEL_ENTITY"
assert_eq "its m_hWeapon names the weapon entity" \
  "$(probe_field "$LAST_PROBE_FILE" weaponEntity)" "$EXPECT_WEAPON_ENTITY"
assert_eq "the weapon entity's class was resolved from the schema" \
  "$(probe_field "$LAST_PROBE_FILE" weaponClass)" "$EXPECT_WEAPON_CLASS"
assert_eq "the index that produced the path is the weapon's own m_iWorldModelIndex" \
  "$(probe_field "$LAST_PROBE_FILE" worldModelIndex)" "$EXPECT_WORLD_MODEL_INDEX"
assert_eq "and it resolves to the weapon body" \
  "$(probe_field "$LAST_PROBE_FILE" worldModelPath)" "$WEAPON_MODEL"
assert_eq "no refusal fired on the recording" "$(probe_field "$LAST_PROBE_FILE" refusal)" "none"

# ---------------------------------------------------------------------------
# Section 3 -- the product issues two draw requests
# ---------------------------------------------------------------------------
echo
echo "=== 3/6 the product's two draw requests ==="
run_product_until_tick body-on "$CAPTURE_TICK"
assert_eq "body-on rc" "$LAST_RC" "0"
# The benchmark tick is the REQUESTED one, and every other frame in this gate is
# taken at the same request, so the picture comparisons below are comparisons of
# the body and not of two moments. Anchoring on this run's own landing spot
# instead -- the first version of this gate did -- makes the anchor a coin flip
# that the other runs cannot be asked to match, which is how it failed.
assert_eq "the benchmark frame landed on the requested tick" \
  "$(capture_tick_of body-on)" "$CAPTURE_TICK"
ON_ARM_DRAWS=$(field "$LAST_READING" armDrawCalls)
ON_WEAPON_DRAWS=$(field "$LAST_READING" weaponDrawCalls)
assert_ge "work count -- the arms were drawn" "$ON_ARM_DRAWS" "1"
assert_ge "work count -- the body was drawn" "$ON_WEAPON_DRAWS" "1"
# The two counters are equal on a run where both drew on every frame, and that
# equality is the statement: two draws per frame, not one. A single Draw call
# wearing two labels would read (n, n) as well, which is why section 6 removes
# the second Draw and requires this pair to separate.
assert_eq "the body's draw call count matches the arms' (one each per frame)" \
  "$ON_WEAPON_DRAWS" "$ON_ARM_DRAWS"
assert_eq "the reading names the index the body came from" \
  "$(field "$LAST_READING" weaponModelIndex)" "$EXPECT_WORLD_MODEL_INDEX"
assert_eq "and the path it resolved to" "$(field "$LAST_READING" weaponModelPath)" "$WEAPON_MODEL"
assert_not_contains "the body's model is not the arms composite" \
  "$(field "$LAST_READING" weaponModelPath)" "_arms.mdl"
assert_eq "the body's weapon entity is reported alongside it" \
  "$(field "$LAST_READING" weaponEntity)" "$EXPECT_WEAPON_ENTITY"
assert_eq "the body's weapon class is reported alongside it" \
  "$(field "$LAST_READING" weaponClass)" "$EXPECT_WEAPON_CLASS"
assert_eq "no fallback fired" "$(field "$LAST_READING" weaponFallback)" "-"
assert_eq "the pass reports its own draw reason" "$(field "$LAST_READING" weaponReason)" "drawn"
assert_eq "the body's centre lands on screen" "$(field "$LAST_READING" weaponVisible)" "1"
assert_gt "work count -- the body's mesh has vertices" \
  "$(field "$LAST_READING" weaponVertices)" "0"
assert_gt_float "the body's mesh has a measured radius of its own" \
  "$(field "$LAST_READING" weaponMeshRadius)" "0"

run_product_until_tick body-off "$CAPTURE_TICK" --viewmodel-weapon-body off
assert_eq "the body-off frame was taken at the same tick as the benchmark" \
  "$(capture_tick_of body-off)" "$CAPTURE_TICK"
assert_eq "body-off rc" "$LAST_RC" "0"
assert_eq "with the body switched off, its draw call count is zero" \
  "$(field "$LAST_READING" weaponDrawCalls)" "0"
assert_ge "control -- the arms are still drawn with the body off" \
  "$(field "$LAST_READING" armDrawCalls)" "1"
assert_eq "and the reading names the switch rather than a lookup failure" \
  "$(field "$LAST_READING" weaponFallback)" "off"
assert_eq "the off run reports no model index it never looked for" \
  "$(field "$LAST_READING" weaponModelIndex)" "0"

# ---------------------------------------------------------------------------
# Section 4 -- the picture
# ---------------------------------------------------------------------------
echo
echo "=== 4/6 the picture ==="
# The determinism pair first, because it is what licenses section 4's other
# assertion. Two runs of the SAME configuration have to be byte-identical; if
# they are not, a difference between `on` and `off` could come from anywhere and
# the comparison below would prove nothing.
run_product_until_tick body-on-repeat "$CAPTURE_TICK"
assert_eq "the repeat frame was taken at the same tick" \
  "$(capture_tick_of body-on-repeat)" "$CAPTURE_TICK"
assert_eq "the repeat run rc" "$LAST_RC" "0"
ON_OFF_DIFF=$(byte_diff "$TRACE_DIR/body-on.bmp" "$TRACE_DIR/body-off.bmp")
ON_REPEAT_DIFF=$(byte_diff "$TRACE_DIR/body-on.bmp" "$TRACE_DIR/body-on-repeat.bmp")
assert_eq "two runs of the same configuration, at the same tick, are byte-identical" \
  "$ON_REPEAT_DIFF" "0"
# And the same question for the window switch this gate runs under: it must not be
# an input. Every other run here is hidden; this one is not, and it has to produce
# the same bytes -- otherwise "the window was hidden" would be an unmeasured
# variable sitting inside every reading the gate makes. Only the window switch is
# lifted: audio stays disabled, because what it would test is a different code
# path this gate has no reading for, and the recording's audio would be audible
# for ninety seconds to no purpose.
QUIET_FLAGS="--audio-disabled"
run_product_until_tick body-on-visible "$CAPTURE_TICK"
QUIET_FLAGS="--hidden-window --audio-disabled"
assert_eq "the visible-window control run rc" "$LAST_RC" "0"
assert_eq "the visible frame was taken at the same tick" \
  "$(capture_tick_of body-on-visible)" "$CAPTURE_TICK"
assert_eq "a visible run produces the same picture as a hidden one" \
  "$(byte_diff "$TRACE_DIR/body-on.bmp" "$TRACE_DIR/body-on-visible.bmp")" "0"
assert_gt "switching the body off changes the picture" "$ON_OFF_DIFF" "0"
assert_ge "and by more than a rounding difference" "$ON_OFF_DIFF" "1000"
echo "  ..   byte_diff(on, off)=$ON_OFF_DIFF byte_diff(on, on-repeat)=$ON_REPEAT_DIFF"

# ---------------------------------------------------------------------------
# Section 5 -- the body is built once, not per frame
# ---------------------------------------------------------------------------
echo
echo "=== 5/6 the body is not rebuilt per frame ==="
BODY_BUILDS=$(grep -ac "gpu: viewmodel weaponBody model=" "$TRACE_DIR/body-on.trace" 2>/dev/null || true)
BODY_LINES=$(grep -ac "scene: weaponBody entity=" "$TRACE_DIR/body-on.trace" 2>/dev/null || true)
assert_eq "the body's MDL was parsed and its mesh uploaded exactly once" "$BODY_BUILDS" "1"
assert_ge "work count -- scene updates that resolved the chain" "$BODY_LINES" "1"
# The arms' own request, for the same reason and as the control: both slots are
# built once per distinct path, so a run that rebuilt either would show up here.
ARM_REQUESTS=$(grep -ac "gpu: viewmodel request path=" "$TRACE_DIR/body-on.trace" 2>/dev/null || true)
assert_eq "control -- the arms' model was built exactly once too" "$ARM_REQUESTS" "1"

# ---------------------------------------------------------------------------
# Section 6 -- four defects, each caught by a named reading and a named control
# ---------------------------------------------------------------------------
echo
echo "=== 6/6 the readings can go red ==="
if [ "$MUTATION" -eq 0 ]; then
  echo "  ..   skipped: the four source mutations run under --mutation"
fi

patch_source() { # patch_source <find> <replace> <label>
  "$PY" - "$MUTATION_FILE" "$1" "$2" "$3" <<'PYEOF' || { echo "  FAIL $3: the mutation did not land"; fail=1; FAIL_LINES+=("$3: patch"); return 1; }
import io, sys
path, find, repl, label = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4]
raw = io.open(path, 'rb').read()
crlf = b'\r\n' in raw
data = raw.decode('utf-8')
if crlf:
    find = find.replace('\n', '\r\n'); repl = repl.replace('\n', '\r\n')
hits = data.count(find)
if hits != 1:
    sys.exit('the %s anchor matched %d times, expected exactly 1' % (label, hits))
io.open(path, 'wb').write(data.replace(find, repl, 1).encode('utf-8'))
PYEOF
}

begin_mutation() { # begin_mutation <file>
  MUTATION_FILE="$1"
  cp "$MUTATION_FILE" "$MUTATION_BAK"
  MUTATION_ORIG_SHA=$(git hash-object "$MUTATION_FILE")
  MUTATION_ACTIVE=1
}
end_mutation() { # end_mutation <label> <targets...> -- restore, verify, rebuild
  local label="$1"; shift
  cp "$MUTATION_BAK" "$MUTATION_FILE"
  MUTATION_ACTIVE=0
  local sha; sha=$(git hash-object "$MUTATION_FILE" 2>/dev/null || true)
  if [ "$sha" = "$MUTATION_ORIG_SHA" ]; then
    echo "  OK   $label: source restored byte-for-byte (${sha:0:12})"
  else
    echo "  FAIL $label: source was NOT restored byte-for-byte"
    fail=1; FAIL_LINES+=("$label: restore")
  fi
  if bash build-target.sh "$@" > ".scratch/weapon-body-restore-$label.log" 2>&1; then
    echo "  OK   $label: the restored tree rebuilt"
  else
    echo "  FAIL $label: the restored tree did not rebuild"
    fail=1; FAIL_LINES+=("$label: restore build")
  fi
}

if [ "$MUTATION" -eq 1 ]; then
  MUTATION_CATCHES=0
  MUTATION_TOTAL=0

  # ---- m1: the index that does not belong ---------------------------------
  # m_iWorldModelIndex read where m_nModelIndex belongs: the defect cd36db1
  # fixed on the world pass. Note what the wrong slot actually does on this
  # fixture -- measured, after the first version of this section asserted the
  # opposite and went red: the slot it now reads names the arms for every
  # weapon, so the arms refusal keeps firing, on all of them, including the
  # cases that are supposed to be accepted. The signature is upstream of that:
  # a chain that resolved stops resolving, and the path handed back is no longer
  # the one m_iWorldModelIndex names. An assertion about the arms refusal would
  # have been an assertion this mutation cannot move.
  echo
  echo "  -- m1 index-swapped"
  fails_before=${#FAIL_LINES[@]}
  begin_mutation native/src/entity_model.cpp
  patch_source '  const std::int64_t worldModelIndex =
    readIntSlot(weaponState, "m_iWorldModelIndex", hasWorldModelIndex);' \
    '  // MUTATION: the other index slot on the same weapon, which names the arms
  const std::int64_t worldModelIndex =
    readIntSlot(weaponState, "m_nModelIndex", hasWorldModelIndex);' \
    "m1" \
    && {
      if bash build-target.sh weapon_body_probe tf2_demo_native > ".scratch/weapon-body-mutation-m1.log" 2>&1; then
        run_probe mutation-m1 --self-test
        assert_ne "m1: the battery goes red" "$(probe_field "$LAST_PROBE_FILE" checksFailed)" "0"
        assert_contains "m1: the chain that resolved no longer does" \
          "$(cat "$TRACE_DIR/mutation-m1.txt")" "FAIL the fixture's own chain (ViewModel -> m_hWeapon -> weapon -> m_iWorldModelIndex) resolves"
        assert_contains "m1: and the path handed back is not the one the index names" \
          "$(cat "$TRACE_DIR/mutation-m1.txt")" "FAIL the resolved path is the one m_iWorldModelIndex names"
        # The control: the walk that FOUND the weapon entity is upstream of the
        # slot this mutation changes, so it is the reading the defect cannot move.
        assert_contains "m1: control -- the weapon entity is still the one m_hWeapon names" \
          "$(cat "$TRACE_DIR/mutation-m1.txt")" "OK   the resolved weapon entity is the one m_hWeapon points at"
        # And the product, where "a wrong index is refused outright, never drawn"
        # becomes a picture. Measured 2026-10-12 under this mutation: the reading
        # reports 1032 -- the same entity's m_nModelIndex, the arms -- refuses it
        # as worldModelIsArms, issues no body draw call, and produces a frame that
        # is byte-identical to the one --viewmodel-weapon-body off produces. A
        # refusal that quietly drew something in its place could not do the last.
        run_product_until_tick mutation-m1 "$CAPTURE_TICK"
        assert_ne "m1: the index the reading reports is no longer the world model index" \
          "$(field "$LAST_READING" weaponModelIndex)" "$EXPECT_WORLD_MODEL_INDEX"
        assert_eq "m1: it is the value of the other index on that same entity" \
          "$(field "$LAST_READING" weaponModelIndex)" "$EXPECT_WEAPON_VIEW_MODEL_INDEX"
        assert_eq "m1: and it is refused, not drawn" \
          "$(field "$LAST_READING" weaponDrawCalls)" "0"
        assert_eq "m1: the refusal is named in the reading" \
          "$(field "$LAST_READING" weaponFallback)" "worldModelIsArms"
        assert_ge "m1: control -- the arms are still drawn" \
          "$(field "$LAST_READING" armDrawCalls)" "1"
        assert_eq "m1: and the picture is the body-off picture -- nothing was drawn in its place" \
          "$(byte_diff "$TRACE_DIR/mutation-m1.bmp" "$TRACE_DIR/body-off.bmp")" "0"
      else
        echo "  FAIL m1: the mutated tree did not build"; fail=1; FAIL_LINES+=("m1: build")
      fi
      MUTATION_TOTAL=$((MUTATION_TOTAL + 1))
      if [ "${#FAIL_LINES[@]}" -eq "$fails_before" ]; then
        MUTATION_CATCHES=$((MUTATION_CATCHES + 1))
        echo "  OK   m1: target red, control held"
      fi
      end_mutation m1 weapon_body_probe tf2_demo_native
    }

  # ---- m2: the arms comparison removed ------------------------------------
  # The refusal that exists for the defect signature is turned off. Now the wrong
  # index draws: the pass is handed the arms as a weapon body. Every count stays
  # green -- one more draw request, a real model, materials that resolve -- which
  # is exactly why the assertion has to be about WHICH model, not how many.
  echo
  echo "  -- m2 arms-check-removed"
  fails_before=${#FAIL_LINES[@]}
  begin_mutation native/src/entity_model.cpp
  patch_source '  if (!armModelPath.empty()
      && normalizeModelPath(found->second) == normalizeModelPath(armModelPath)) {' \
    '  if (false   // MUTATION: the arms comparison is removed
      && normalizeModelPath(found->second) == normalizeModelPath(armModelPath)) {' \
    "m2" \
    && {
      if bash build-target.sh weapon_body_probe tf2_demo_native > ".scratch/weapon-body-mutation-m2.log" 2>&1; then
        run_probe mutation-m2 --self-test
        assert_ne "m2: the battery goes red" "$(probe_field "$LAST_PROBE_FILE" checksFailed)" "0"
        assert_contains "m2: the arms refusal is gone" \
          "$(cat "$TRACE_DIR/mutation-m2.txt")" "FAIL a world index that resolves to the arms refuses as the arms"
        assert_contains "m2: and the refusal no longer withholds the arms path" \
          "$(cat "$TRACE_DIR/mutation-m2.txt")" "FAIL the refusal does NOT hand back the arms path -- the invariant"
        # The control: the substitution case is a DIFFERENT branch of the same
        # rule (the index is accepted, the path reported as-is) and the removal
        # cannot move it.
        assert_contains "m2: control -- the substitution case is still accepted" \
          "$(cat "$TRACE_DIR/mutation-m2.txt")" "OK   a world index naming another, real weapon body is accepted (the chain cannot judge the pairing)"
        # The product is the other control, and it is the one that attributes the
        # red: the recording's own chain never reaches the deleted check -- its
        # world index (533) resolves to a real weapon body, not to the arms -- so
        # every reading the recording produces has to be unchanged. A red probe
        # beside an unchanged product says the red came from the deleted
        # comparison, not from something wider that the rebuild broke everywhere.
        run_product mutation-m2
        assert_eq "m2: control -- the recording's own path never went through the removed check" \
          "$(field "$LAST_READING" weaponModelPath)" "$WEAPON_MODEL"
        assert_eq "m2: control -- so the recording still draws its own body" \
          "$(field "$LAST_READING" weaponDrawCalls)" "$(field "$LAST_READING" armDrawCalls)"
      else
        echo "  FAIL m2: the mutated tree did not build"; fail=1; FAIL_LINES+=("m2: build")
      fi
      MUTATION_TOTAL=$((MUTATION_TOTAL + 1))
      if [ "${#FAIL_LINES[@]}" -eq "$fails_before" ]; then
        MUTATION_CATCHES=$((MUTATION_CATCHES + 1))
        echo "  OK   m2: target red, control held"
      fi
      end_mutation m2 weapon_body_probe tf2_demo_native
    }

  # ---- m3: the second Draw call removed -----------------------------------
  # The mesh is resolved, the state is enabled, the reason is written -- and the
  # submission is gone, together with the line that counted it. The first version
  # of this patch left the counter behind and asserted that the two counters would
  # separate, which is the opposite of what leaving the counter behind does; it
  # is the removal of BOTH lines that separates them.
  #
  # Both halves are then asserted, and they say the same thing from two sides:
  # the counter separates from the arms' (so the pair was never one Draw wearing
  # two labels), and the frame comes out byte-identical to the one the
  # --viewmodel-weapon-body off switch produces (so what went missing is exactly
  # the geometry, with no state difference riding along).
  echo
  echo "  -- m3 second-draw-removed"
  fails_before=${#FAIL_LINES[@]}
  begin_mutation native/src/native_renderer.cpp
  patch_source '    context_->Draw(viewModelWeaponVertexCount_, 0);
    ++viewModelWeaponDrawCallCount_;' \
    '    // MUTATION: the body is resolved but never submitted, and the line that
    // counted the submission went with it' \
    "m3" \
    && {
      if bash build-target.sh tf2_demo_native > ".scratch/weapon-body-mutation-m3.log" 2>&1; then
        run_product_until_tick mutation-m3 "$CAPTURE_TICK"
        assert_eq "m3: the frame was taken at the requested tick" \
          "$(capture_tick_of mutation-m3)" "$CAPTURE_TICK"
        assert_ne "m3: the body's draw call count separates from the arms'" \
          "$(field "$LAST_READING" weaponDrawCalls)" "$(field "$LAST_READING" armDrawCalls)"
        assert_eq "m3: no body draw call was issued" \
          "$(field "$LAST_READING" weaponDrawCalls)" "0"
        # The controls, and they are the ones the defect cannot touch: the arms
        # still draw, and every OTHER reading about the body is still healthy.
        # That is the point -- the defect is invisible except in the count.
        assert_ge "m3: control -- the arms are still drawn" \
          "$(field "$LAST_READING" armDrawCalls)" "1"
        assert_eq "m3: control -- the body's model is still resolved" \
          "$(field "$LAST_READING" weaponModelPath)" "$WEAPON_MODEL"
        assert_eq "m3: control -- and its state still reports drawn" \
          "$(field "$LAST_READING" weaponReason)" "drawn"
        assert_eq "m3: and the picture is the body-off picture -- what went missing is the geometry" \
          "$(byte_diff "$TRACE_DIR/mutation-m3.bmp" "$TRACE_DIR/body-off.bmp")" "0"
      else
        echo "  FAIL m3: the mutated renderer did not build"; fail=1; FAIL_LINES+=("m3: build")
      fi
      MUTATION_TOTAL=$((MUTATION_TOTAL + 1))
      if [ "${#FAIL_LINES[@]}" -eq "$fails_before" ]; then
        MUTATION_CATCHES=$((MUTATION_CATCHES + 1))
        echo "  OK   m3: target red, control held"
      fi
      end_mutation m3 tf2_demo_native
    }

  # ---- m4: the material chain reports success it did not measure ----------
  # The chain is short-circuited to "every slot resolved". A missing material is
  # then indistinguishable from a present one in the reading, which is the state
  # the counts exist to prevent.
  echo
  echo "  -- m4 material-chain-shortcut"
  fails_before=${#FAIL_LINES[@]}
  begin_mutation native/src/viewmodel.cpp
  patch_source '  ViewModelMaterialReading reading;
  reading.slots = inspection.metadata.textureCandidates.size();' \
    '  ViewModelMaterialReading reading;
  reading.slots = inspection.metadata.textureCandidates.size();
  // MUTATION: claim every slot resolved without walking the chain
  reading.named = reading.slots;
  reading.vmtResolved = reading.slots;
  reading.vtfResolved = reading.slots;
  return reading;' \
    "m4" \
    && {
      if bash build-target.sh weapon_body_probe > ".scratch/weapon-body-mutation-m4.log" 2>&1; then
        run_probe mutation-m4 --tf-root "$TF" --model "$WEAPON_MODEL"
        assert_ne "m4: the battery goes red" "$(probe_field "$LAST_PROBE_FILE" checksFailed)" "0"
        assert_contains "m4: a slot whose material is not installed no longer lowers the count" \
          "$(cat "$TRACE_DIR/mutation-m4.txt")" "FAIL a slot whose material is not installed lowers the vmt count by exactly one"
        # The control: the slot count still comes from the MDL, which the
        # short-circuit does not replace.
        assert_contains "m4: control -- the slot count is still measured" \
          "$(cat "$TRACE_DIR/mutation-m4.txt")" "OK   the reading counts every slot the MDL declares"
      else
        echo "  FAIL m4: the mutated probe did not build"; fail=1; FAIL_LINES+=("m4: build")
      fi
      MUTATION_TOTAL=$((MUTATION_TOTAL + 1))
      if [ "${#FAIL_LINES[@]}" -eq "$fails_before" ]; then
        MUTATION_CATCHES=$((MUTATION_CATCHES + 1))
        echo "  OK   m4: target red, control held"
      fi
      end_mutation m4 weapon_body_probe
    }

  # ---- after every restore ------------------------------------------------
  run_product_until_tick restored-body-on "$CAPTURE_TICK"
  assert_eq "after every restore, the body draws again" \
    "$(field "$LAST_READING" weaponDrawCalls)" "$(field "$LAST_READING" armDrawCalls)"
  assert_eq "after every restore, the body's model is its own again" \
    "$(field "$LAST_READING" weaponModelPath)" "$WEAPON_MODEL"
  assert_eq "after every restore, the picture is the one the first run produced" \
    "$(byte_diff "$TRACE_DIR/body-on.bmp" "$TRACE_DIR/restored-body-on.bmp")" "0"

  printf 'mutation summary: caught=%s of %s\n' "$MUTATION_CATCHES" "$MUTATION_TOTAL"
  assert_eq "mutations that were caught" "$MUTATION_CATCHES" "4"
  assert_eq "mutations that ran" "$MUTATION_TOTAL" "4"
  if [ "$fail" -eq 0 ]; then
    echo "MUTATION-CAUGHT=PASS (4 defects, each with its own named reading and control)"
    exit 0
  fi
  echo "MUTATION-CAUGHT=FAIL ($MUTATION_CATCHES/$MUTATION_TOTAL caught)"
  printf 'failed assertions:\n'
  printf '  %s\n' "${FAIL_LINES[@]}"
  exit 1
fi

# ---------------------------------------------------------------------------
# The verdict
# ---------------------------------------------------------------------------
echo
if [ "$fail" -eq 0 ]; then
  echo "WEAPON-BODY=PASS (two draw requests, the body from the entity's own index,"
  echo "                 the picture attributable to it, and one build per model)"
  exit 0
fi
echo "WEAPON-BODY=FAIL"
printf 'failed assertions:\n'
printf '  %s\n' "${FAIL_LINES[@]}"
exit 1
