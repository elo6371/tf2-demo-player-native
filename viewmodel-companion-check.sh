#!/usr/bin/env bash
# viewmodel-companion-check.sh -- T2.5: the companion proof, and the dynamic arms.
#
# THE QUESTION
#   Step 23 measured that the ViewModel entity's m_nSequence does NOT index the
#   model the pass drew: the entity names a c_<class>_arms model (43 sequences,
#   58 bones for the sniper) and the pass drew a v_<weapon>.mdl named on its own
#   command line (7 sequences, 45 bones). That refutation is correct and it left
#   the picture wrong. The fix is not a translation between the two index spaces
#   -- there is nothing to translate, the entity's sequence already indexes the
#   entity's OWN model -- it is a PROOF that the pass is drawing that model. This
#   gate pins the proof, the drawing, and the refusals that keep the proof honest.
#
# WHAT IT PINS, AND WHY EACH NUMBER IS A READING RATHER THAN A CLAIM
#   Section 1  the instrument. The probe's self-test, which drives the product's
#              own entry points, so agreement is tested against the product rather
#              than against a second copy of the rules. Its assertions are COUNTED
#              (an OK line count and a total), because a battery that silently
#              stopped running would otherwise look like a battery that passed.
#   Section 2  the proof on the entity's own model, on the pinned recording, at
#              three ticks DERIVED from a history run rather than hardcoded. The
#              three ticks carry three different sequences, so "the reading moves
#              across ticks" is a fact about this run and not about the tick
#              numbers chosen. Three witness frames, pairwise byte-different.
#   Section 3  the accepted pose, CONSTRUCTED because this recording cannot reach
#              it: the entity carries sequences 1, 29 and 30, and the model it
#              names can pose sequence 0 and nothing else. The sequence to use is
#              read out of the model itself and named on the command line, where
#              every reading line reports it as `sequenceOverride=`. This is the
#              only positively correct picture in the round and it is the reason
#              the branch is not left unobserved -- but it is an override, and it
#              is reported as one.
#   Section 4  the refusals are kept. The old path -- the pass told to draw a v_*
#              model while the entity names a c_*_arms model -- must refuse, must
#              name BOTH paths, and must leave the bind pose. This is the reading
#              the round exists to keep: the fix adds a proof, it does not remove
#              a refusal.
#   Section 5  the two models side by side, read from the models themselves: the
#              entity's own is a table of one-frame poses (43 sequences, one of
#              them posable), the control is a real multi-frame set (7 sequences,
#              all posable). The counts are what make section 2's `ok=0` a reading
#              rather than a gap.
#   Section 6  six mutations, each a defect the task names, each requiring a
#              NAMED assertion to go red, a NAMED control to hold, and the
#              source restored byte-for-byte with a successful rebuild.
#
# Usage: bash viewmodel-companion-check.sh [--mutation]
#   --mutation reintroduces each of the six defects in the product's own source,
#   rebuilds, and requires the named reading to move while the named control
#   holds. It prints MUTATION-CAUGHT=... rather than the ordinary verdict, because
#   under --mutation the sections are supposed to be red.
#
#   The restore is a byte copy rather than `git checkout`, and that is deliberate:
#   `git checkout` reverts to HEAD, so a run on a tree with uncommitted work would
#   destroy that work while claiming to restore it. The byte copy is also the
#   stronger claim -- every mutation's restore is verified against the hash the
#   file had before the patch, and the rebuild that follows is asserted, not
#   assumed.
# Exit:  0 = every assertion held (or, under --mutation, all six were caught),
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
PROBE=native/build-nmake/viewmodel_animation_probe.exe
TF="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
TESTDATA_ROOT="D:/TF2_Demo_Player/testdata/demos"
# The model the pass is TOLD to draw. It is no longer what the pass draws by
# default -- that is the entity's own model -- so this path is now the *control*
# for the refusal, not the picture.
VM_MODEL="models/weapons/v_models/v_scattergun_scout.mdl"
# The model the ViewModel ENTITY names on the pinned recording, and the one the
# pass must therefore draw.
ENTITY_MODEL="models/weapons/c_models/c_sniper_arms.mdl"
INPUTS=evidence/viewmodel/animation-inputs.txt
# Git may check this script out with CRLF; strip the carriage return from path
# variables before passing them to native tools or filesystem tests.
TF=${TF%$'\r'}
TESTDATA_ROOT=${TESTDATA_ROOT%$'\r'}
EXE=${EXE%$'\r'}
PROBE=${PROBE%$'\r'}
INPUTS=${INPUTS%$'\r'}
VM_MODEL=${VM_MODEL%$'\r'}
ENTITY_MODEL=${ENTITY_MODEL%$'\r'}

TRACE_DIR="${VIEWMODEL_COMPANION_TRACE_DIR:-evidence/fast/viewmodel-companion}"
mkdir -p "$TRACE_DIR"
mkdir -p .scratch
LOCK_DIR=.scratch/viewmodel-companion.lock
if ! mkdir "$LOCK_DIR" 2>/dev/null; then
  echo "VIEWMODEL-COMPANION=FAIL (another viewmodel-companion-check run owns $LOCK_DIR)"
  exit 2
fi

# ---------------------------------------------------------------------------
# Source mutation plumbing
# ---------------------------------------------------------------------------
MUTATION_FILE=""
MUTATION_BAK=".scratch/viewmodel-companion-mutation.bak"
MUTATION_ORIG_SHA=""
MUTATION_ACTIVE=0

restore_mutation() {
  [ "$MUTATION_ACTIVE" -eq 1 ] || return 0
  cp "$MUTATION_BAK" "$MUTATION_FILE"
  MUTATION_ACTIVE=0
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
# which reads exactly like "the product produced no animation". Convert once.
ROOT=$(cygpath -m "$(pwd)" 2>/dev/null || pwd)
case "$ROOT" in
  /[a-zA-Z]/*) echo "FATAL: could not convert '$ROOT' to a Windows path"; exit 2 ;;
esac
for path in "$EXE" "$PROBE" "$INPUTS" "$TF"; do
  [ -e "$path" ] || { echo "FATAL: missing input: $path"; exit 2; }
done

# ---------------------------------------------------------------------------
# Assertions and trace readers
# ---------------------------------------------------------------------------
# Audio: the device is opened during startup and pausing does not stop it, so
# every launch names it. The default device is the wrong one on this machine.
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
assert_contains() {
  case "${2:-}" in
    *"$3"*) echo "  OK   $1 contains $3" ;;
    *) echo "  FAIL $1 = ${2:-<none>}, expected to contain $3"
       fail=1; FAIL_LINES+=("$1: ${2:-<none>} missing $3") ;;
  esac
}

# field <line> <name> -> the value of name=<value> in a trace line, "" if absent.
field() {
  printf '%s\n' "$1" | tr ' ' '\n' | sed -n "s/^$2=//p" | head -1
}
# rsum <file> <key> <name> -> the value of name= on the last line whose prefix matches.
rsum() {
  grep -a "^$2" "$1" 2>/dev/null | tr -d '\r' | tail -1 | tr ' ' '\n' | sed -n "s/^$3=//p" | head -1
}
# byte_diff <a> <b> -> how many byte positions differ, or -1 if a file is missing.
byte_diff() {
  if [ ! -s "$1" ] || [ ! -s "$2" ]; then echo -1; return; fi
  cmp -l "$1" "$2" 2>/dev/null | wc -l | tr -d ' '
}

# ---------------------------------------------------------------------------
# Pinned inputs
# ---------------------------------------------------------------------------
# The Steam demos directory is LIVE -- recordings land in it while a round is
# being worked on -- so the recording this gate reads is pinned by size and
# sha256 in a committed file rather than found by a glob. A missing or changed
# file is a FAIL, not a skip: "the input is not here" and "the input is clean"
# must not read the same.
echo "=== 0/6 pinned inputs ==="
DEMO_POV=""
PINNED=0
while read -r size sha256 root name; do
  name=${name%$'\r'}
  case "$size" in ''|'#'*) continue ;; esac
  case "$root" in
    steam)    path="$TF/demos/$name" ;;
    testdata) path="$TESTDATA_ROOT/$name" ;;
    *) echo "  FAIL unknown root '$root' for $name"; fail=1; continue ;;
  esac
  if [ ! -f "$path" ]; then
    echo "  FAIL pinned recording is absent: $path"; fail=1; continue
  fi
  actual_size=$(stat -c%s "$path")
  actual_sha=$(sha256sum "$path" | cut -d' ' -f1)
  if [ "$actual_size" != "$size" ]; then
    echo "  FAIL pinned recording $name is $actual_size bytes, pinned $size"; fail=1; continue
  fi
  if [ "$actual_sha" != "$sha256" ]; then
    echo "  FAIL pinned recording $name has hash ${actual_sha:0:16}..., pinned ${sha256:0:16}..."; fail=1; continue
  fi
  echo "  OK   $name pinned ($size bytes, ${sha256:0:16}...)"
  PINNED=$((PINNED + 1))
  case "$name" in
    autorecord_2026-07-02_13-26-46.dem) DEMO_POV="$path" ;;
  esac
done < "$INPUTS"
assert_ge "pinned recordings verified" "$PINNED" 1
if [ -z "$DEMO_POV" ]; then
  echo "VIEWMODEL-COMPANION=FAIL (the pinned POV recording could not be resolved)"
  exit 1
fi
echo "  ..   the POV recording is $DEMO_POV"

# ---------------------------------------------------------------------------
# Section 1 -- the instrument
# ---------------------------------------------------------------------------
echo
echo "=== 1/6 the instrument: the probe's self-test ==="
run_selftest() { # run_selftest <name>
  local name="$1"
  timeout "$RUN_TIMEOUT" "$PROBE" --tf-root "$TF" --self-test \
    > "$TRACE_DIR/$name.json" 2> "$TRACE_DIR/$name.txt"
  LAST_SELFTEST_RC=$?
  LAST_SELFTEST="$TRACE_DIR/$name.txt"
  SELFTEST_FAILS=$(sed -n 's/^self-test failures=//p' "$LAST_SELFTEST" | tr -d '\r' | head -1)
  SELFTEST_OK=$(grep -ac "^  OK" "$LAST_SELFTEST" || true)
  SELFTEST_TOTAL=$(grep -acE "^  (OK|FAIL)" "$LAST_SELFTEST" || true)
  SELFTEST_SUMMARY=$(grep -a "^vmproof-summary" "$LAST_SELFTEST" | tr -d '\r' | tail -1)
}
run_selftest selftest
assert_eq "selftest rc" "$LAST_SELFTEST_RC" "0"
assert_eq "selftest failures" "$SELFTEST_FAILS" "0"
# Work counts, not just verdicts. `failures=0` alone is satisfied by a battery
# that stopped running after the first check, so the OK count AND the number of
# checks the battery ran are both pinned. The two together mean the same checks
# ran and all of them held.
assert_eq "selftest assertions that held" "$SELFTEST_OK" "92"
assert_eq "selftest checks that ran" "$SELFTEST_TOTAL" "92"
assert_eq "companion fixtures recorded" "$(field "$SELFTEST_SUMMARY" fixtures)" "19"
assert_eq "self-test ran the product cross-check" \
  "$(grep -ac "NOTE the product cross-check did not run" "$LAST_SELFTEST" || true)" "0"

# The two models the proof has to tell apart, measured rather than described.
# The companion is a table of one-frame poses; the v_* control is a multi-frame
# animation set. A witness hardcoded to either one's answer would read as "the
# cycle is ignored" on the other.
COMPANION_LINE=$(grep -a "^vmproof-companion" "$LAST_SELFTEST" | tr -d '\r' | tail -1)
CONTROL_LINE=$(grep -a "^vmproof-control" "$LAST_SELFTEST" | tr -d '\r' | tail -1)
RAW_LINE=$(grep -a "^vmproof-rawhdr" "$LAST_SELFTEST" | tr -d '\r' | tail -1)
RAW_BLEND=$(grep -a "^vmproof-rawblend" "$LAST_SELFTEST" | tr -d '\r' | tail -1)
assert_contains "the companion model is the entity's own" "$COMPANION_LINE" "c_soldier_arms.mdl"
assert_eq "companion sequences" "$(field "$COMPANION_LINE" sequences)" "54"
assert_eq "companion animation descriptors" "$(field "$COMPANION_LINE" animations)" "1"
assert_eq "companion sequences that reach a descriptor" "$(field "$COMPANION_LINE" animated)" "1"
assert_eq "companion sequences with more than one frame" "$(field "$COMPANION_LINE" multiFrame)" "0"
assert_eq "companion widest binding" "$(field "$COMPANION_LINE" maxFrames)" "1"
# The same reading taken from the file's own words, so the count above can be
# checked without re-running the decoder that produced it.
assert_eq "raw header animation count" "$(field "$RAW_LINE" animCount)" "1"
assert_eq "raw header sequence count" "$(field "$RAW_LINE" seqCount)" "54"
assert_eq "raw sequence descriptors with no blend block" "$(field "$RAW_BLEND" word56NonPositive)" "53"
assert_eq "control model sequences" "$(field "$CONTROL_LINE" sequences)" "7"
assert_eq "control model multi-frame sequences" "$(field "$CONTROL_LINE" multiFrame)" "6"
assert_eq "control widest binding" "$(field "$CONTROL_LINE" maxFrames)" "57"
assert_ge "the cycle witness has a live sequence" "$(field "$CONTROL_LINE" witness)" "0"

# ---------------------------------------------------------------------------
# Section 2 -- the proof, on the entity's own model, at three derived ticks
# ---------------------------------------------------------------------------
echo
echo "=== 2/6 the pass draws the entity's own model ==="
run_product() { # run_product <name> <demo> <tick> [extra...]
  local name="$1" demo="$2" tick="$3"; shift 3
  local bmp="$TRACE_DIR/$name.bmp"
  rm -f "$bmp" "$TRACE_DIR/$name.csv"
  TF2_NATIVE_TRACE="$ROOT/$TRACE_DIR/$name.trace" timeout "$RUN_TIMEOUT" "$EXE" \
    --tf-root "$TF" --audio-device "$AUDIO_DEVICE" --demo "$demo" \
    --settings-file "$TRACE_DIR/$name-settings.ini" \
    --capture-frame "$bmp" --capture-tick "$tick" \
    --metrics-file "$TRACE_DIR/$name.csv" \
    --viewmodel "$VM_MODEL" --viewmodel-fov 90 "$@" > "$TRACE_DIR/$name.log" 2>&1
  LAST_RC=$?
  load_run "$name"
  echo "  $name rc=$LAST_RC bmp=$(stat -c%s "$bmp" 2>/dev/null || echo 0)"
}
load_run() {
  local name="$1"
  LAST_ORDER=$(grep -a "gpu: viewmodel pass order" "$TRACE_DIR/$name.trace" 2>/dev/null \
    | tr -d '\r' | tail -1 | sed 's/^[0-9]\{1,\}\(\.[0-9]*\)\? //')
  LAST_VMANIM=$(grep -a "scene: vmAnim entity" "$TRACE_DIR/$name.trace" 2>/dev/null \
    | tr -d '\r' | tail -1 | sed 's/^[0-9]\{1,\}\(\.[0-9]*\)\? //')
  LAST_SUMMARY=$(grep -a "scene: vmAnim summary" "$TRACE_DIR/$name.trace" 2>/dev/null \
    | tr -d '\r' | tail -1 | sed 's/^[0-9]\{1,\}\(\.[0-9]*\)\? //')
}

# The history run, which is what makes the three witness ticks derived rather
# than chosen: it records the sequence history, and the first scene update of
# each distinct sequence is where a witness is worth taking. A hardcoded tick
# would be a reading about this recording's content with nothing saying how it
# was found; deriving it means a recording change reddens the derivation instead
# of silently comparing three frames that show the same pose.
run_product history "$DEMO_POV" 2000
assert_eq "history rc" "$LAST_RC" "0"
assert_ge "history observations" "$(field "$LAST_SUMMARY" observations)" 1000
assert_eq "history distinct sequences" \
  "$(awk '/scene: vmAnim entity/ { for (i = 1; i <= NF; ++i) if ($i ~ /^sequence=/) seen[substr($i, 10)] = 1 }
          END { print length(seen) }' "$TRACE_DIR/history.trace")" "3"
assert_eq "history sequence range" "$(field "$LAST_SUMMARY" range)" "1..30"
TICKS=$(awk '/scene: begin/ { t = $4; sub("tick=", "", t) }
             /scene: vmAnim entity/ { seq = "-"
               for (i = 1; i <= NF; ++i) if ($i ~ /^sequence=/) seq = substr($i, 10)
               if (!(seq in seen)) { seen[seq] = 1; print t } }' "$TRACE_DIR/history.trace" | head -3)
TICK_COUNT=$(printf '%s\n' "$TICKS" | grep -c . || true)
assert_eq "witness ticks derived" "$TICK_COUNT" "3"
T0=$(printf '%s\n' "$TICKS" | sed -n 1p)
T1=$(printf '%s\n' "$TICKS" | sed -n 2p)
T2=$(printf '%s\n' "$TICKS" | sed -n 3p)
echo "  ..   derived witness ticks: $T0 $T1 $T2"

run_product witness-a "$DEMO_POV" "$T0"
assert_eq "witness-a rc" "$LAST_RC" "0"
# The proof itself, on the pass's own line: the model whose mesh is in the vertex
# buffer is the model the entity's m_nModelIndex names.
assert_eq "witness-a drawn model" "$(field "$LAST_ORDER" drawnModel)" "$ENTITY_MODEL"
assert_eq "witness-a entity model matches the drawn model" \
  "$(field "$LAST_ORDER" entityModelMatchesDrawn)" "1"
assert_eq "witness-a sequence inside the entity's own table" "$(field "$LAST_ORDER" sequenceInRange)" "1"
assert_eq "witness-a model mismatches" "$(field "$LAST_ORDER" modelMismatch)" "0"
assert_eq "witness-a bone mismatches" "$(field "$LAST_ORDER" boneMismatch)" "0"
assert_eq "witness-a the pass drew" "$(field "$LAST_ORDER" drawCalls)" "1"
assert_eq "witness-a vertices" "$(field "$LAST_ORDER" vertices)" "600"
assert_eq "witness-a sequence" "$(field "$LAST_VMANIM" sequence)" "29"
assert_eq "witness-a owner is the view entity" "$(field "$LAST_VMANIM" owner)" "$(field "$LAST_VMANIM" viewEntity)"
assert_eq "witness-a model index is the entity's own" "$(field "$LAST_VMANIM" modelIndex)" "1032"
assert_contains "witness-a names the entity's own model" "$LAST_VMANIM" "$(basename "$ENTITY_MODEL")"
# Counted over every scene update of the run, not read off the last one: the pass
# took the proof on every frame it drew, so a proof that held once and failed
# 1453 times cannot hide behind the final line.
assert_eq "witness-a model matches, counted" \
  "$(field "$LAST_SUMMARY" modelMatches)" "$(field "$LAST_SUMMARY" observations)"
assert_eq "witness-a model mismatches, counted" "$(field "$LAST_SUMMARY" modelMismatch)" "0"
assert_eq "witness-a sequences in range, counted" \
  "$(field "$LAST_SUMMARY" sequenceInRange)" "$(field "$LAST_SUMMARY" observations)"

run_product witness-b "$DEMO_POV" "$T1"
assert_eq "witness-b rc" "$LAST_RC" "0"
assert_eq "witness-b sequence" "$(field "$LAST_VMANIM" sequence)" "1"
assert_eq "witness-b entity model matches the drawn model" \
  "$(field "$LAST_ORDER" entityModelMatchesDrawn)" "1"
assert_eq "witness-b model mismatches, counted" "$(field "$LAST_SUMMARY" modelMismatch)" "0"
assert_eq "witness-b sequences in range, counted" \
  "$(field "$LAST_SUMMARY" sequenceInRange)" "$(field "$LAST_SUMMARY" observations)"
assert_gt "witness-b observations exceed witness-a's" \
  "$(field "$LAST_SUMMARY" observations)" "$(field "$(grep -a 'scene: vmAnim summary' "$TRACE_DIR/witness-a.trace" | tr -d '\r' | tail -1 | sed 's/^[0-9]\{1,\}\(\.[0-9]*\)\? //')" observations)"
assert_ge "witness-b sequence changes" "$(field "$LAST_SUMMARY" changes)" 1

run_product witness-c "$DEMO_POV" "$T2" --viewmodel-apply-animation
assert_eq "witness-c rc" "$LAST_RC" "0"
assert_eq "witness-c sequence" "$(field "$LAST_VMANIM" sequence)" "30"
assert_eq "witness-c entity model matches the drawn model" \
  "$(field "$LAST_ORDER" entityModelMatchesDrawn)" "1"
assert_eq "witness-c model mismatches, counted" "$(field "$LAST_SUMMARY" modelMismatch)" "0"
assert_ge "witness-c sequence changes" "$(field "$LAST_SUMMARY" changes)" 2
# The three sequences this recording carries, and the fact that none of them is
# the one the model can pose (section 4). Kept as a reading of THIS run so the
# "not this recording" statement in section 4 cannot drift away from it.
WITNESS_RANGE=$(field "$LAST_SUMMARY" range)
WITNESS_NATURAL_OK=$(field "$LAST_SUMMARY" ok)
assert_eq "witness-c sequence range over the run" "$WITNESS_RANGE" "1..30"
# No pose is accepted anywhere on the natural run, and that is a reading rather
# than a gap: every scene update this run makes, each one inside the entity's own
# model's table, and not one of them naming a sequence that model can pose.
assert_eq "no natural run accepted a pose" "$WITNESS_NATURAL_OK" "0"
assert_eq "and every one was refused for the same measured reason" \
  "$(field "$LAST_SUMMARY" missing)" "$(field "$LAST_SUMMARY" observations)"
assert_eq "no override was in force" "$(field "$LAST_ORDER" sequenceOverride)" "-1"

# Three frames, pairwise different. Without this "the reading moves across ticks"
# would rest on three numbers in a log with nothing saying the picture changed.
AB=$(byte_diff "$TRACE_DIR/witness-a.bmp" "$TRACE_DIR/witness-b.bmp")
BC=$(byte_diff "$TRACE_DIR/witness-b.bmp" "$TRACE_DIR/witness-c.bmp")
AC=$(byte_diff "$TRACE_DIR/witness-a.bmp" "$TRACE_DIR/witness-c.bmp")
assert_gt "the frames of ticks a and b differ" "$AB" 0
assert_gt "the frames of ticks b and c differ" "$BC" 0
assert_gt "the frames of ticks a and c differ" "$AC" 0
echo "  ..   witness frames differ by $AB / $BC / $AC byte positions"

# ---------------------------------------------------------------------------
# 2b -- the pose branch, constructed instead of waited for
# ---------------------------------------------------------------------------
echo
echo "=== 3/6 the accepted pose, on the entity's own model ==="
# The recording carries sequences 1, 29 and 30, and the model the entity names
# can pose sequence 0 and nothing else -- so the acceptance branch is unreachable
# on this recording and NO amount of waiting for a tick would produce it. Leaving
# it unobserved would leave the only positively-correct picture in this round
# unobserved; claiming "accepted" without constructing the input would be worse.
# So the input is CONSTRUCTED and NAMED: the sequence to use is read out of the
# model itself (`firstAnimated`, the lowest index that reaches a descriptor) and
# passed on the command line, where every reading line reports it as
# `sequenceOverride=` so an overridden run cannot be mistaken for a natural one.
run_model_sequences() { # run_model_sequences <name> <model>
  timeout "$RUN_TIMEOUT" "$PROBE" --tf-root "$TF" --model-sequences "$2" \
    > "$TRACE_DIR/$1.json" 2> "$TRACE_DIR/$1.txt"
  LAST_MODELSEQ_RC=$?
  MODELSEQ_LINE=$(grep -a "^modelseq" "$TRACE_DIR/$1.txt" | tr -d '\r' | tail -1)
}
run_model_sequences entity-model "$ENTITY_MODEL"
assert_eq "entity model rc" "$LAST_MODELSEQ_RC" "0"
assert_eq "entity model sequences" "$(field "$MODELSEQ_LINE" sequences)" "43"
assert_eq "entity model bones" "$(field "$MODELSEQ_LINE" bones)" "58"
assert_eq "entity model sequences that reach a descriptor" "$(field "$MODELSEQ_LINE" animated)" "1"
POSE_SEQUENCE=$(field "$MODELSEQ_LINE" firstAnimated)
# Asserted rather than used: the number this run is built around must be a
# reading of the model, and if the model ever gets a posable sequence earlier
# than the one named here the assertion says so before the pose run reads `ok`
# for a reason nobody chose.
assert_eq "the only sequence the entity's own model can pose" "$POSE_SEQUENCE" "0"
assert_ne "and the recording never carries it" "$WITNESS_RANGE" "0..30"

run_product pose-accepted "$DEMO_POV" "$T0" --viewmodel-sequence "$POSE_SEQUENCE"
assert_eq "pose-accepted rc" "$LAST_RC" "0"
assert_eq "pose-accepted status" "$(field "$LAST_ORDER" vmAnimInput)" "ok"
assert_eq "pose-accepted entity model matches the drawn model" \
  "$(field "$LAST_ORDER" entityModelMatchesDrawn)" "1"
assert_eq "pose-accepted sequence inside the entity's own table" \
  "$(field "$LAST_ORDER" sequenceInRange)" "1"
assert_eq "pose-accepted sequence accepted" "$(field "$LAST_ORDER" sequenceAccepted)" "1"
assert_eq "pose-accepted leaves the bind pose" "$(field "$LAST_ORDER" bindPose)" "0"
assert_eq "pose-accepted poseAvailable" "$(field "$LAST_VMANIM" poseAvailable)" "1"
assert_eq "pose-accepted frame count is the model's own" "$(field "$LAST_VMANIM" frame)" "0/1"
assert_eq "pose-accepted names the override" "$(field "$LAST_ORDER" sequenceOverride)" "$POSE_SEQUENCE"
assert_eq "pose-accepted applied nothing by default" "$(field "$LAST_ORDER" vmAnimApplied)" "0"
assert_eq "pose-accepted skinned nothing by default" "$(field "$LAST_ORDER" skinning)" "0"
assert_eq "pose-accepted uploaded no table by default" "$(field "$LAST_ORDER" boneTableCoverage)" "0/0"

run_product pose-applied "$DEMO_POV" "$T0" --viewmodel-sequence "$POSE_SEQUENCE" --viewmodel-apply-animation
assert_eq "pose-applied rc" "$LAST_RC" "0"
assert_eq "pose-applied applied" "$(field "$LAST_ORDER" vmAnimApplied)" "1"
assert_eq "pose-applied skinned" "$(field "$LAST_ORDER" skinning)" "1"
COVERAGE=$(field "$LAST_ORDER" boneTableCoverage)
assert_eq "pose-applied bone table covers the whole skeleton" "${COVERAGE%%/*}" "${COVERAGE##*/}"
assert_eq "pose-applied bone table size is the model's own bone count" \
  "${COVERAGE%%/*}" "$(field "$MODELSEQ_LINE" bones)"
assert_eq "pose-applied every accepted pose was applied" \
  "$(field "$LAST_SUMMARY" applied)" "$(field "$LAST_SUMMARY" ok)"
assert_gt "pose-applied every observation was accepted" "$(field "$LAST_SUMMARY" ok)" 0
assert_eq "pose-applied refused nothing" "$(field "$LAST_SUMMARY" missing)" "0"
POSE_DIFF=$(byte_diff "$TRACE_DIR/pose-accepted.bmp" "$TRACE_DIR/pose-applied.bmp")
assert_gt "the applied pose changes the frame" "$POSE_DIFF" 0
echo "  ..   pose-accepted vs pose-applied: $POSE_DIFF byte positions differ"

# ---------------------------------------------------------------------------
# Section 3 -- the refusals are kept, not traded away
# ---------------------------------------------------------------------------
echo
echo "=== 4/6 the mismatch is still refused, and the pose is still bind ==="
# The pass is told to draw the v_* model, which is what the pass drew BEFORE this
# round. The entity's own model is a c_*_arms model, so the proof must refuse --
# and the refusal must name both paths, or "the proof fired" and "the proof fired
# for the right reason" would look the same.
run_product refusal "$DEMO_POV" "$T0" --viewmodel-render command
assert_eq "refusal rc" "$LAST_RC" "0"
assert_eq "refusal status" "$(field "$LAST_ORDER" vmAnimInput)" "modelMismatch"
assert_eq "refusal draws the model it was told to draw" \
  "$(field "$LAST_ORDER" drawnModel)" "$VM_MODEL"
assert_eq "refusal the entity's model does not match the drawn one" \
  "$(field "$LAST_ORDER" entityModelMatchesDrawn)" "0"
assert_eq "refusal the range question was never reached" "$(field "$LAST_ORDER" sequenceInRange)" "0"
assert_eq "refusal mismatches, counted" "$(field "$LAST_SUMMARY" modelMismatch)" "$(field "$LAST_SUMMARY" observations)"
assert_eq "refusal matches, counted" "$(field "$LAST_SUMMARY" modelMatches)" "0"
# No pose, no table, no skinning -- the refusal is not a partial outcome.
assert_eq "refusal leaves the bind pose" "$(field "$LAST_ORDER" bindPose)" "1"
assert_eq "refusal applies nothing" "$(field "$LAST_ORDER" vmAnimApplied)" "0"
assert_eq "refusal skins nothing" "$(field "$LAST_ORDER" skinning)" "0"
assert_eq "refusal has no bone table" "$(field "$LAST_ORDER" boneTableCoverage)" "0/0"
assert_eq "refusal poseAvailable" "$(field "$LAST_VMANIM" poseAvailable)" "0"
assert_contains "refusal names the drawn model" "$LAST_VMANIM" "$VM_MODEL"
assert_contains "refusal names the entity's own model" "$LAST_VMANIM" "$ENTITY_MODEL"

# ---------------------------------------------------------------------------
# Section 4 -- the two models, side by side
# ---------------------------------------------------------------------------
echo
echo "=== 5/6 the control model, and why one index decides the round ==="
# The entity's own model was read in 2b, because the pose run there is built on
# its `firstAnimated`. What is left for this section is the CONTROL: the model the
# pass used to draw, which is a genuine multi-frame animation set. The two counts
# together are the whole reason the mapping is not a translation.
run_model_sequences control-model "$VM_MODEL"
assert_eq "control model rc" "$LAST_MODELSEQ_RC" "0"
assert_eq "control model sequences" "$(field "$MODELSEQ_LINE" sequences)" "7"
assert_eq "control model sequences that reach a descriptor" "$(field "$MODELSEQ_LINE" animated)" "7"
assert_eq "control model bone count" "$(field "$MODELSEQ_LINE" bones)" "45"
# The one index that CAN pose, against the three the recording carries (section
# 2). Both numbers are measured; the sentence they add up to -- "the recording
# never selects a sequence this model can pose" -- is what makes the natural
# run's `ok=0` a reading rather than a gap.
assert_eq "the control's posable sequence is the same index" "$(field "$MODELSEQ_LINE" firstAnimated)" "0"
assert_ne "the two models do not share a table size" \
  "$(field "$MODELSEQ_LINE" sequences)" "43"

# ---------------------------------------------------------------------------
# Section 5 -- six defects, each caught by a named reading and a named control
# ---------------------------------------------------------------------------
echo
echo "=== 6/6 the readings can go red ==="
if [ "$MUTATION" -eq 0 ]; then
  echo "  ..   skipped: the six source mutations run under --mutation"
else
  MUTATION_CATCHES=0
  MUTATION_TOTAL=0
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
  if bash build-target.sh "$@" > ".scratch/companion-restore-$label.log" 2>&1; then
    echo "  OK   $label: the restored tree rebuilt"
  else
    echo "  FAIL $label: the restored tree did not rebuild"
    fail=1; FAIL_LINES+=("$label: restore build")
  fi
}

# mutation_case <name> <file> <find> <replace> <target-line> <control-ok-line> <check-name>
mutation_case() {
  local name="$1" file="$2" find="$3" repl="$4" target="$5" control="$6"
  echo
  echo "  -- $name"
  begin_mutation "$file"
  patch_source "$find" "$repl" "$name" || { end_mutation "$name" viewmodel_animation_probe; return; }
  if ! bash build-target.sh viewmodel_animation_probe > ".scratch/companion-mutation-$name.log" 2>&1; then
    echo "  FAIL $name: the mutated probe did not build"
    fail=1; FAIL_LINES+=("$name: build")
    end_mutation "$name" viewmodel_animation_probe
    return
  fi
  run_selftest "mutation-$name"
  local caught=0 control_held=0
  if [ "$SELFTEST_FAILS" != "0" ]; then caught=1; fi
  case "$(cat "$LAST_SELFTEST")" in
    *"FAIL $target"*) caught=$((caught + 1)) ;;
    *) echo "  FAIL $name: the target reading did not move: '$target'" ;;
  esac
  case "$(cat "$LAST_SELFTEST")" in
    *"OK   $control"*) control_held=1 ;;
    *) echo "  FAIL $name: the control moved: '$control'" ;;
  esac
  echo "  ..   failures=$SELFTEST_FAILS checks ran=$SELFTEST_TOTAL target='$target'"
  if [ "$caught" -eq 2 ] && [ "$control_held" -eq 1 ]; then
    MUTATION_CATCHES=$((MUTATION_CATCHES + 1))
    echo "  OK   $name: target red, control held"
  else
    fail=1; FAIL_LINES+=("$name: target red=$caught/2 control=$control_held")
  fi
  MUTATION_TOTAL=$((MUTATION_TOTAL + 1))
  assert_eq "$name: the battery still ran every check" "$SELFTEST_TOTAL" "92"
  end_mutation "$name" viewmodel_animation_probe
}

# THE CONTROL HAS TO BE A CHECK THE MUTATION CANNOT TOUCH, and the first draft of
# two of these got that wrong -- measured, not guessed: `mismatch-off` was paired
# with "the mismatch reading is reported as false" and `range-off` with "an
# out-of-range sequence is not reported as in range", and both of those readings
# are BY CONSTRUCTION the ones the mutation inverts. A control that the defect is
# defined to move is not a control; it is a third red reading wearing a control's
# label. So each mutation is now paired with the NEIGHBOURING branch of the same
# proof -- the branch the mutation does not touch -- which is the reading that
# says the change was surgical: `mismatch-off` leaves the range branch firing,
# `range-off` leaves the model proof firing. Both pairings were verified against
# the full list of checks each mutation actually turns red (5 for mismatch-off,
# 2 for range-off), not against the mutation's own target line alone.
if [ "$MUTATION" -eq 1 ]; then
  mutation_case mismatch-off native/src/viewmodel.cpp \
    '  if (!entityModelMatches) {' \
    '  if (false && !entityModelMatches) {   // MUTATION: the proof never fires' \
    'a drawn model that is not the entity'\''s own resolves to modelMismatch' \
    'one past the entity'\''s own table resolves to sequenceOutOfRange'
  mutation_case range-off native/src/viewmodel.cpp \
    '  if (static_cast<std::size_t>(input.sequence) >= drawnSequences) {' \
    '  if (false && static_cast<std::size_t>(input.sequence) >= drawnSequences) {   // MUTATION' \
    'one past the entity'\''s own table resolves to sequenceOutOfRange' \
    'a drawn model that is not the entity'\''s own resolves to modelMismatch'
  mutation_case bone-count-off native/src/viewmodel.cpp \
    '  if (sampled.skinMatrices.size() != drawn.bones.size()) {' \
    '  if (false && sampled.skinMatrices.size() != drawn.bones.size()) {   // MUTATION' \
    'a skeleton one bone longer resolves to boneMismatch' \
    'the accepted pose covers the drawn skeleton exactly'
  mutation_case model-index-zero-off native/src/viewmodel.cpp \
    '  if (input.modelIndex == 0) {' \
    '  if (false && input.modelIndex == 0) {   // MUTATION: zero is not a model' \
    'm_nModelIndex 0 resolves to missing, not to a model' \
    'the absent model index is named'
fi

# ---------------------------------------------------------------------------
# The verdict
# ---------------------------------------------------------------------------
echo
if [ "$MUTATION" -eq 1 ]; then
  # The two mutations that live in the render path rather than in the resolver
  # cannot be read through the probe's self-test, so they are run here.
  echo "=== render-path mutations ==="
  # The baseline is re-read here rather than reused from section 2, because the
  # mutation loop's controls are compared against it and a snapshot taken minutes
  # and several launches earlier would make "the control held" a statement about
  # the wrong run. Two readings are deliberately NOT snapshotted: the number of
  # scene updates (measured 1 or 2 at the same tick from one launch to the next --
  # see the note in the force-v-model arm) and the model-match count (it is the
  # work count itself, so pinning it would be pinning the thing under test).
  run_product baseline-companion "$DEMO_POV" "$T0"
  BASE_MISMATCH=$(field "$LAST_SUMMARY" modelMismatch)
  BASE_DRAWN=$(field "$LAST_ORDER" drawnModel)
  BASE_VERTICES=$(field "$LAST_ORDER" vertices)
  assert_eq "baseline draws the entity's own model" "$BASE_DRAWN" "$ENTITY_MODEL"

  for name in force-v-model owner-cleared; do
    echo "  -- $name"
    # These two cannot be read through the probe's self-test -- the defect lives in
    # the render path -- so "caught" is judged the same way the battery judges it:
    # the assertions below state the RED reading (the mismatch must fire, the proof
    # must stop) AND a control that the mutation cannot move, so a case that adds
    # no failure line is a case where the red reading appeared and the control
    # held. Counted, not asserted in prose, so the summary is a tally.
    local_fails_before=${#FAIL_LINES[@]}
    begin_mutation native/src/main.cpp
    case "$name" in
      force-v-model)
        patch_source '          companionDrawn ? viewModelCompanion.request : viewModelRequest;' \
          '          viewModelRequest;   // MUTATION: the companion proof stops choosing the model' \
          "$name" ;;
      owner-cleared)
        patch_source '          animationInput.hasOwner = readSlotValue("m_hOwner", ownerHandle);' \
          '          animationInput.hasOwner = false;   // MUTATION: the owner is dropped' \
          "$name" ;;
    esac
    if ! bash build-target.sh tf2_demo_native > ".scratch/companion-mutation-$name.log" 2>&1; then
      echo "  FAIL $name: the mutated renderer did not build"; fail=1; FAIL_LINES+=("$name: build")
      end_mutation "$name" tf2_demo_native
      continue
    fi
    run_product "mutation-$name" "$DEMO_POV" "$T0"
    case "$name" in
      force-v-model)
        # The defect the round names: the pass is asked to draw a model the entity
        # never named, and the entity's sequence would then be measured against
        # that model's table. The refusal that catches it must fire.
        assert_eq "$name: the pass's request names the model it was told to name" \
          "$(field "$LAST_ORDER" drawnModel)" "$VM_MODEL"
        assert_eq "$name: the entity's model no longer matches the drawn one" \
          "$(field "$LAST_ORDER" entityModelMatchesDrawn)" "0"
        assert_ne "$name: the mismatch now fires" \
          "$(field "$LAST_SUMMARY" modelMismatch)" "0"
        assert_eq "$name: the range question is never reached" \
          "$(field "$LAST_ORDER" sequenceInRange)" "0"
        # A SECOND consequence, asserted rather than tidied away: the mutated pass
        # issues no draw call at all. The vertex buffer holds the mesh the
        # companion path uploaded, the request now names the v_* path, and the
        # pass's own "is this state's mesh the one in the buffer" check turns it
        # off -- so `vertices` still reads the BUFFER's 600 and not a drawn count.
        # A mutation whose only trace was a label would be proof of nothing, which
        # is why the absent draw call is a reading here.
        assert_eq "$name: and the mutated pass issues no draw call at all" \
          "$(field "$LAST_ORDER" drawCalls)" "0"
        # The controls, and neither is a scene-update count: measured on this
        # recording, the number of scene updates before the capture tick reads 1
        # or 2 from one launch to the next (1 in the ordinary companion run, 2 in
        # the `--viewmodel-apply-animation`-alone run at the same tick, 2 here).
        # Pinning it made a real mutation look like a moved control. So the work
        # count is asserted as "at least one update happened" and the controls are
        # the two readings the mutation cannot move: the refusal still leaves the
        # bind pose, and no bone count was ever compared.
        assert_ge "$name: work count -- scene updates" \
          "$(field "$LAST_SUMMARY" observations)" 1
        assert_eq "$name: control -- the refusal still leaves the bind pose" \
          "$(field "$LAST_ORDER" bindPose)" "1"
        assert_eq "$name: control -- the bone question is never reached either" \
          "$(field "$LAST_ORDER" boneMismatch)" "0"
        ;;
      owner-cleared)
        # The owner is the first link in the proof. Dropping it must stop the
        # proof before the model question is asked.
        assert_eq "$name: the owner is gone, so the proof stops before any sequence is measured" \
          "$(field "$LAST_ORDER" sequenceInRange)" "0"
        assert_eq "$name: the model question is never reached" \
          "$(field "$LAST_SUMMARY" modelMatches)" "0"
        assert_contains "$name: the reason names the absent owner" "$LAST_VMANIM" "m_hOwner"
        # The controls. This mutation only stops the PROOF, so the picture is
        # untouched -- and that is a two-part claim: the same mesh is still in the
        # pass, and the pass still issues its draw call. (The scene-update count
        # that used to sit here was replaced for the reason given above; it read 1
        # against a baseline of 1 on this run, which is luck rather than a control.)
        assert_eq "$name: control -- the drawn request is still the entity's model" \
          "$(field "$LAST_ORDER" drawnModel)" "$ENTITY_MODEL"
        assert_eq "$name: control -- the same mesh is still drawn" \
          "$(field "$LAST_ORDER" vertices)" "$BASE_VERTICES"
        assert_eq "$name: control -- and the pass still issues its draw call" \
          "$(field "$LAST_ORDER" drawCalls)" "1"
        assert_ge "$name: work count -- scene updates" \
          "$(field "$LAST_SUMMARY" observations)" 1
        ;;
    esac
    MUTATION_TOTAL=$((MUTATION_TOTAL + 1))
    if [ "${#FAIL_LINES[@]}" -eq "$local_fails_before" ]; then
      MUTATION_CATCHES=$((MUTATION_CATCHES + 1))
      echo "  OK   $name: target red, control held"
    fi
    end_mutation "$name" tf2_demo_native
  done

  run_product restored-companion "$DEMO_POV" "$T0"
  assert_eq "after every restore, the pass draws the entity's own model again" \
    "$(field "$LAST_ORDER" drawnModel)" "$BASE_DRAWN"
  assert_eq "after every restore, the proof holds again" "$(field "$LAST_ORDER" entityModelMatchesDrawn)" "1"
  assert_eq "after every restore, the mismatch count is back to zero" \
    "$(field "$LAST_SUMMARY" modelMismatch)" "$BASE_MISMATCH"

  printf 'mutation summary: caught=%s of %s\n' "$MUTATION_CATCHES" "$MUTATION_TOTAL"
  assert_eq "mutations that were caught" "$MUTATION_CATCHES" "6"
  assert_eq "mutations that ran" "$MUTATION_TOTAL" "6"
  if [ "$fail" -eq 0 ]; then
    echo "MUTATION-CAUGHT=PASS (6 defects, each with its own named reading and control)"
    exit 0
  fi
  echo "MUTATION-CAUGHT=FAIL ($MUTATION_CATCHES/$MUTATION_TOTAL caught)"
  printf 'failed assertions:\n'
  printf '  %s\n' "${FAIL_LINES[@]}"
  exit 1
fi

if [ "$fail" -eq 0 ]; then
  echo "VIEWMODEL-COMPANION=PASS"
else
  echo "VIEWMODEL-COMPANION=FAIL"
  printf 'failed assertions:\n'
  printf '  %s\n' "${FAIL_LINES[@]}"
fi
exit "$fail"
