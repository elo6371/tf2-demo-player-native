#!/usr/bin/env bash
# viewmodel-animation-check.sh -- T2.5: the ViewModel animation input contract.
#
# THE QUESTION
#   T2 drew the ViewModel in its bind pose and reported `animationInput=missing`.
#   That string was honest but unmeasured: nothing had asked whether the demo
#   carries a ViewModel's animation state at all, and T1 had already answered "no"
#   for CTFPlayer. This gate answers it for the class that DOES carry it, and it
#   answers a second question T2 could not: does the state it carries index the
#   model the pass draws?
#
# WHAT IT PINS, AND WHY EACH NUMBER IS A READING RATHER THAN A CLAIM
#   Section 1  the instrument. The probe's self-test, which drives the product's
#              own path rule AND its own resolver, so agreement is tested against
#              the product rather than against a second copy of the rule.
#   Section 2  the input exists and moves. One line per class with one column per
#              field, because the three fields' supply differs and a single
#              "animation properties" count would hide exactly that.
#   Section 3  the two index spaces are different. The entity names
#              c_<class>_arms.mdl (43 sequences, 58 bones for the sniper) and the
#              pass draws v_<weapon>.mdl (7 sequences, 45 bones for the
#              scattergun). Measured, not assumed -- and the counts are read from
#              the models themselves. T6: the entity's own model declares
#              c_sniper_animations.mdl and 38 of its 43 sequences reach an
#              animation descriptor through it BY NAME (the five that do not:
#              `ref` and the four `rifolver_*` overlays); the base model's own
#              stub table can pose exactly one sequence.
#   Section 4  the pairing is READ, not guessed. svc_SetView names the view entity
#              and m_hOwner pairs it to a ViewModel entity. On the POV recording
#              that resolves (two entities); on the matcha recording it does NOT
#              (zero). Both are asserted, because "the pairing works" is true of
#              one recording rather than in general.
#   Section 5  the refusals and the acceptance, at the product level, from two
#              directions. (a) The pass is TOLD to draw the model on its own
#              command line -- what T2 always did -- while the entity names a
#              different model: the proof refuses, names BOTH paths, and leaves
#              the bind pose. (b) The same recording in the shipped mode: the
#              model matches, the sequence is inside the entity's own table, and
#              the pose RESOLVES through the animations companion the model
#              declares (T6) -- opt-in, so the default run resolves and applies
#              nothing. The pair is the contract: the right model is drawn AND
#              the input is understood, and neither statement is bought with the
#              other.
#   Section 6  the accepted pose, on real animation, with the stub kept as the
#              negative. T6 measured why the old round had to CONSTRUCT its
#              acceptance: the arms/player models carry a stub animation table
#              (c_sniper_arms: one 1-frame animation) and declare a
#              *_animations.mdl companion holding the real set (88 animations, 76
#              sequences), and the resolver now adopts the companion when it
#              binds STRICTLY MORE of the model's own sequences by name. The
#              natural run therefore accepts, and three constructed runs pin the
#              pieces apart: sequence 30 (61 frames) is the real animation,
#              sequence 0 (`ref`) is the stub with no companion binding and is
#              still refused by name, and the opt-in boundary is asserted in both
#              directions. Every override is reported as `sequenceOverride=`.
#   Section 7  five probe mutations, each with a named assertion that must go red
#              AND a control that must not move. These are runtime switches inside
#              the probe, so they cost nothing and run on every invocation; the
#              rebuilt, source-level half is `--mutation`, which neuters the
#              round's own model proof in the product's own source (T6 extended
#              it with a second patch that neuters the companion adoption).
#
# Usage: bash viewmodel-animation-check.sh [--mutation]
#   --mutation reintroduces the defect this round exists to remove -- the proof
#   that the pass is drawing the model the entity named -- rebuilds, and requires
#   the named readings to move while the named controls hold. It prints
#   MUTATION-CAUGHT=... rather than the ordinary verdict, because under --mutation
#   the sections are supposed to be red.
# Exit:  0 = every assertion held (or, under --mutation, the mutation was caught),
#        1 = it did not, 2 = another run owns the lock or the tree is dirty.
#
# One run at a time: the script writes frames and traces under one directory and
# launches the product several times, so two concurrent runs overwrite each
# other's evidence. mkdir is atomic, so a second caller fails before it runs
# anything. Killing the wrapper is not enough to release it -- `timeout` and
# `tf2_demo_native` survive as grandchildren -- so a stale lock is cleared
# deliberately rather than silently overlapped.
set -uo pipefail
cd "$(dirname "$0")"

# ---------------------------------------------------------------------------
# Arguments
# ---------------------------------------------------------------------------
# `--mutation` is the source-level half of section 7 (see its banner): one defect
# reintroduced in the product's own resolver, rebuilt, and the readings it must
# move are named there. It is wired rather than tolerated, because a gate whose
# `--mutation` is silently ignored would let `verify-render.sh --mutation` and
# the acceptance chain's step claim a check that never ran.
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
# The model the pass was TOLD to draw before this round, and the control the
# refusal is built on today. Seven sequences against the entity's own model's 43,
# which is why an accepted value on the OLD path was a coincidence of the two
# tables rather than a mapping -- and why the shipped pass now draws the entity's
# own model instead of this one. `--viewmodel-render command` is what restores
# this model as the drawn one, and it is the product-side negative case.
VM_MODEL="models/weapons/v_models/v_scattergun_scout.mdl"
# The model the ViewModel ENTITY names, and the one the shipped pass draws. The
# entity's m_nSequence indexes THIS model's table, so its size is the number the
# whole round turns on and it is read rather than written here. It cannot come
# from the product's `--viewmodel` flag -- that entry point still insists on a
# `v_`-prefixed path -- which is why the probe has a `--model-sequences` mode that
# reads a model of any class: "how many sequences does the model the entity names
# have" is the question that decides whether the two index spaces really differ.
ENTITY_MODEL="models/weapons/c_models/c_sniper_arms.mdl"
INPUTS=evidence/viewmodel/animation-inputs.txt
# Git may check this script out with CRLF; strip the carriage return from path
# variables before passing them to native tools or filesystem tests.
TF=${TF%$'\r'}
TESTDATA_ROOT=${TESTDATA_ROOT%$'\r'}
EXE=${EXE%$'\r'}
PROBE=${PROBE%$'\r'}
INPUTS=${INPUTS%$'\r'}

# Where the readings go. Every product this gate makes is a machine product -- a
# trace and two 2.5 MB witness frames per launch, plus per-run metrics CSVs -- so
# there is no committed output directory: the default is under the ignored
# evidence/fast tree, and the committed record is the pin file
# evidence/viewmodel/animation-inputs.txt plus the acceptance chain's step log.
TRACE_DIR="${VIEWMODEL_ANIMATION_TRACE_DIR:-evidence/fast/viewmodel-animation}"
mkdir -p "$TRACE_DIR"

mkdir -p .scratch
LOCK_DIR=.scratch/viewmodel-animation.lock
if ! mkdir "$LOCK_DIR" 2>/dev/null; then
  echo "VIEWMODEL-ANIMATION=FAIL (another viewmodel-animation-check run owns $LOCK_DIR)"
  exit 2
fi
# --mutation rebuilds the product, so it also takes the chain's build lock (the
# one mutate.sh and verify-all's build step share) and puts the source back on
# every exit path. `git checkout --` is the restore mechanism, which is why the
# tree has to be committed before this runs. The restore rebuilds every target
# that links viewmodel.cpp -- leaving even one of them mutated would hand a
# later gate a binary that disagrees with the tree it was built from.
MUTATION_SRC=native/src/viewmodel.cpp
# T6's second patch: the companion adoption lives in animation_binding.cpp, and
# the mutation neuters it too -- a mutation that only touched the model proof
# would leave the new binding unwatched.
MUTATION_SRC2=native/src/animation_binding.cpp
BUILD_LOCK_OWNED=0
PARENT_LOCK_HELD=0
MUTATION_ACTIVE=0
MUTATION_ORIGINAL_SHA=""
MUTATION_ORIGINAL_SHA2=""
restore_mutation() {
  [ "$MUTATION_ACTIVE" -eq 1 ] || return 0
  local rc=0
  git checkout -- "$MUTATION_SRC" "$MUTATION_SRC2" >/dev/null 2>&1
  if [ -n "$MUTATION_ORIGINAL_SHA" ]; then
    restored_sha=$(git hash-object "$MUTATION_SRC" 2>/dev/null || true)
    if [ "$restored_sha" != "$MUTATION_ORIGINAL_SHA" ]; then
      echo "FATAL: mutation source was not restored byte-for-byte"
      rc=1
    fi
  fi
  if [ -n "$MUTATION_ORIGINAL_SHA2" ]; then
    restored_sha2=$(git hash-object "$MUTATION_SRC2" 2>/dev/null || true)
    if [ "$restored_sha2" != "$MUTATION_ORIGINAL_SHA2" ]; then
      echo "FATAL: the second mutation source was not restored byte-for-byte"
      rc=1
    fi
  fi
  if ! bash build-target.sh tf2_demo_native viewmodel_fov_probe viewmodel_contract_probe \
      viewmodel_animation_probe animation_viewmodel_probe > .scratch/vm-anim-restore.log 2>&1; then
    echo "FATAL: the restore build failed -- the mutated binary may still be on disk"
    echo "       see .scratch/vm-anim-restore.log"
    rc=1
  fi
  if [ "$BUILD_LOCK_OWNED" -eq 1 ]; then
    rmdir .scratch/verify-all.lock 2>/dev/null || true
    BUILD_LOCK_OWNED=0
  fi
  MUTATION_ACTIVE=0
  return "$rc"
}
# A restore that failed must not be able to exit 0. The tree would be left with a
# mutated binary on disk, and a green run would hand it to the next gate -- the
# "failure looks like success" shape this project keeps finding. The message above
# says so out loud; this makes the exit status say so too.
on_exit() {
  local rc=0
  restore_mutation || rc=1
  rmdir "$LOCK_DIR" 2>/dev/null || true
  [ "$rc" -eq 0 ] || exit 1
}
trap on_exit EXIT

PY="C:/Users/Administrator/.workbuddy-ai/binaries/python/versions/3.13.12/python.exe"
[ -x "$PY" ] || { echo "FATAL: missing interpreter: $PY"; exit 1; }
# The trace path is handed to a native Windows binary, which does not translate
# MSYS paths: "/d/..." is opened literally and the trace silently never opens,
# which reads exactly like "the product produced no animation". Convert once.
ROOT=$(cygpath -m "$(pwd)" 2>/dev/null || pwd)
case "$ROOT" in
  /[a-zA-Z]/*) echo "FATAL: could not convert '$ROOT' to a Windows path"; exit 1 ;;
esac
for path in "$EXE" "$PROBE" "$INPUTS" "$TF"; do
  [ -e "$path" ] || { echo "FATAL: missing input: $path"; exit 1; }
done

# ---------------------------------------------------------------------------
# --mutation: reintroduce the defect the proof exists to catch
# ---------------------------------------------------------------------------
# The proof's second check -- "the model the pass draws is the model the entity's
# own m_nModelIndex names" -- is the line this whole round added. Neutering it
# restores exactly T2's behaviour: the pass draws the model named on its own
# command line, and the entity's sequence is measured against THAT model's table.
# On this recording that is 7 entries against a sequence of 29, so the reading
# degrades to `sequenceOutOfRange` and `past the entity's own model's 7 sequences`
# -- a refusal that names a table which is not the entity's model's, which is the
# precise thing the string's own wording claims it is not.
#
# The defect is not that an unsafe pose becomes available: the range check still
# refuses 29, and the bind pose is still what is drawn. The defect is that the
# pass can draw a model the entity never named while every reading says the input
# was understood -- including `entityModelMatchesDrawn`, which the neutered
# resolver falls through and asserts as true. So the three readings that must move
# are the status, the reason (it stops naming the entity's own model at all), and
# the model-match count; and the four controls that must NOT move are the pose
# availability (still a refusal, only mislabelled), the matcha pairing (no owner
# is ever resolved there), the probe's index-space verdict (computed from the two
# models' own tables, not through the resolver) and the probe's own battery, which
# must still run all 92 checks while 5 of them go red.
if [ "$MUTATION" -eq 1 ]; then
  if ! git diff --quiet -- native/; then
    echo "FATAL: --mutation needs a committed native/ tree (git checkout is the restore)"
    exit 2
  fi
  if [ "${VERIFY_ALL_LOCK_HELD:-0}" != "1" ]; then
    if ! mkdir .scratch/verify-all.lock 2>/dev/null; then
      echo "VIEWMODEL-ANIMATION=FAIL (a build owns .scratch/verify-all.lock)"
      exit 2
    fi
    BUILD_LOCK_OWNED=1
  else
    PARENT_LOCK_HELD=1
  fi
  MUTATION_ORIGINAL_SHA=$(git hash-object "$MUTATION_SRC")
  MUTATION_ORIGINAL_SHA2=$(git hash-object "$MUTATION_SRC2")
  "$PY" - "$MUTATION_SRC" "$MUTATION_SRC2" <<'PYEOF' || { echo "FATAL: the mutation did not land"; exit 1; }
import io, sys
patches = [
  (sys.argv[1],
   "  if (!entityModelMatches) {",
   "  // MUTATION: the companion proof never fires, so the pass may draw a model\n"
   "  // the entity did not name and measure the entity's sequence against it.\n"
   "  if (false && !entityModelMatches) {"),
  (sys.argv[2],
   "      if (nameBound > boundInBase(anims)) {",
   "      // MUTATION: the declared companion is never adopted, so a model whose\n"
   "      // own table is a stub keeps refusing every sequence it cannot pose.\n"
   "      if (false && nameBound > boundInBase(anims)) {"),
]
for path, find, repl in patches:
    raw = io.open(path, 'rb').read()
    crlf = b'\r\n' in raw
    data = raw.decode('utf-8')
    if crlf:
        find = find.replace('\n', '\r\n')
        repl = repl.replace('\n', '\r\n')
    hits = data.count(find)
    if hits != 1:
        sys.exit('the mutation anchor in %s matched %d times, expected exactly 1' % (path, hits))
    io.open(path, 'wb').write(data.replace(find, repl, 1).encode('utf-8'))
PYEOF
  MUTATION_ACTIVE=1
  if ! bash build-target.sh tf2_demo_native viewmodel_fov_probe viewmodel_contract_probe \
      viewmodel_animation_probe animation_viewmodel_probe > .scratch/vm-anim-mutation.log 2>&1; then
    echo "FATAL: the mutated build failed; see .scratch/vm-anim-mutation.log"
    exit 1
  fi
  echo "mutation: the companion proof AND the companion adoption are neutered (rebuilt)"
fi

# Audio: the device is opened during startup and pausing does not stop it, so
# every launch names it. The default device is the wrong one on this machine.
AUDIO_DEVICE=6
RUN_TIMEOUT=300

fail=0
FAIL_LINES=()
assert_eq() { # assert_eq <label> <actual> <expected>
  if [ "${2:-<none>}" = "$3" ]; then
    echo "  OK   $1 = $3"
  else
    echo "  FAIL $1 = ${2:-<none>}, expected $3"; fail=1; FAIL_LINES+=("$1: ${2:-<none>} != $3")
  fi
}
assert_ne() { # assert_ne <label> <actual> <forbidden>
  if [ -n "${2:-}" ] && [ "${2:-}" != "$3" ]; then
    echo "  OK   $1 = $2 (!= $3)"
  else
    echo "  FAIL $1 = ${2:-<none>}, expected anything but $3"; fail=1; FAIL_LINES+=("$1: ${2:-<none>} == $3")
  fi
}
assert_ge() { # assert_ge <label> <actual> <minimum>
  if [ "${2:-x}" -ge "$3" ] 2>/dev/null; then
    echo "  OK   $1 = $2 (>= $3)"
  else
    echo "  FAIL $1 = ${2:-<none>}, expected >= $3"; fail=1; FAIL_LINES+=("$1: ${2:-<none>} < $3")
  fi
}
assert_gt() { # assert_gt <label> <actual> <exclusive minimum>
  if [ "${2:-x}" -gt "$3" ] 2>/dev/null; then
    echo "  OK   $1 = $2 (> $3)"
  else
    echo "  FAIL $1 = ${2:-<none>}, expected > $3"; fail=1; FAIL_LINES+=("$1: ${2:-<none>} <= $3")
  fi
}
assert_contains() { # assert_contains <label> <haystack> <needle>
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
# rfield <file> <name> -> the value of name=<value> on the LAST matching reading
# line of a probe stderr file.
rfield() {
  grep -a "^$2 " "$1" 2>/dev/null | tr -d '\r' | tail -1 | tr ' ' '\n' | sed -n "s/^$3=//p" | head -1
}
# rline <file> <prefix> -> the last line in a probe stderr file starting with it.
rline() {
  grep -a "^$2 " "$1" 2>/dev/null | tr -d '\r' | tail -1
}
# rsum <file> <key> -> the value of key= on the *-summary line whose prefix matches.
rsum() {
  grep -a "^$2" "$1" 2>/dev/null | tr -d '\r' | tail -1 | tr ' ' '\n' | sed -n "s/^$3=//p" | head -1
}
# count_line <file> <literal prefix> -> how many lines start with it.
count_line() {
  grep -ac "^$2" "$1" 2>/dev/null || echo 0
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
# being worked on -- so the two recordings this gate reads are pinned by size and
# sha256 in a committed file rather than found by a glob. A missing or changed
# file is a FAIL, not a skip: "the input is not here" and "the input is clean"
# must not read the same. This is the rule step 9 of the acceptance chain learned
# the expensive way on 2026-10-08.
echo "=== 0/7 pinned inputs ==="
DEMO_POV=""
DEMO_MATCHA=""
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
    *matcha*)                           DEMO_MATCHA="$path" ;;
  esac
done < "$INPUTS"
assert_eq "pinned recordings verified" "$PINNED" "2"
for pair in "pov:$DEMO_POV" "matcha:$DEMO_MATCHA"; do
  label=${pair%%:*}; value=${pair#*:}
  if [ -z "$value" ]; then
    echo "  FAIL the $label recording was not identified by the pin file"; fail=1
  else
    echo "  OK   the $label recording is $value"
  fi
done
if [ -z "$DEMO_POV" ] || [ -z "$DEMO_MATCHA" ]; then
  echo "VIEWMODEL-ANIMATION=FAIL (a pinned recording could not be resolved)"
  exit 1
fi

# ---------------------------------------------------------------------------
# Section 1 -- the instrument
# ---------------------------------------------------------------------------
echo
echo "=== 1/7 the instrument: the probe's self-test ==="
SELFTEST_TXT="$TRACE_DIR/selftest.txt"
timeout "$RUN_TIMEOUT" "$PROBE" --tf-root "$TF" --self-test > /dev/null 2> "$SELFTEST_TXT"
SELFTEST_RC=$?
SELFTEST_OK=$(grep -c '^  OK   ' "$SELFTEST_TXT" || true)
SELFTEST_FAIL=$(grep -c '^  FAIL ' "$SELFTEST_TXT" || true)
SELFTEST_NOTES=$(grep -c 'NOTE the product cross-check did not run' "$SELFTEST_TXT" || true)
# The self-test drives the product's own entry points, so a run without a TF root
# tests only the probe's private copy of the rules. That is half a rule, and the
# probe says so out loud -- this asserts it did not have to.
if [ "$MUTATION" -eq 1 ]; then
  assert_eq "self-test rc" "$SELFTEST_RC" "1"
  # Measured, not derived: neutering the companion proof turns exactly five of the
  # battery's checks red -- the model-mismatch fixture's five claims (the status,
  # the bind pose, the absent-match reading, the never-reached range question, and
  # the reason naming both models). A mutated build must also SAY so in its exit
  # code, which is why `rc` is asserted in both modes.
  assert_eq "self-test failures" "$SELFTEST_FAIL" "5"
else
  assert_eq "self-test rc" "$SELFTEST_RC" "0"
  assert_eq "self-test failures" "$SELFTEST_FAIL" "0"
fi
# Pinned, because a gate that prints PASS while its assertions quietly disappear
# is not a gate. 96 checks in a healthy build: the T2.5 instrument's own readings
# (the path rule, the three-outcome range classifier, the series counters, the
# field-supply counter, and the product cross-check), the resolver contract branch
# by branch -- missing (3), absent sequence (2), out of range (4), accepted (5,
# including the by-name sequence label), bone mismatch (2), no animation data (1),
# unresolved drawn model (2) -- the companion proof's fixtures, the adopted
# companion's measured shape and reachability (T6: 53 of 54 sequences bound by
# name, 52 multi-frame, the longest 89 frames), and the cycle witness, which runs
# on the `v_*` control.
#
# Two DIFFERENT quantities, which is why there are two assertions and not one
# written twice: how many checks held, and how many checks the battery ran at all.
# The second is pinned in both modes to the same 96, and that is the point -- a
# mutation that made the battery stop halfway would move it, while 91 + 5 in
# mutation mode says the other 91 checks still ran and still held.
if [ "$MUTATION" -eq 1 ]; then
  assert_eq "self-test assertions" "$SELFTEST_OK" "91"
  assert_eq "self-test checks that ran" "$((SELFTEST_OK + SELFTEST_FAIL))" "96"
else
  assert_eq "self-test assertions" "$SELFTEST_OK" "96"
  assert_eq "self-test checks that ran" "$((SELFTEST_OK + SELFTEST_FAIL))" "96"
fi
assert_eq "self-test ran the product cross-check" "$SELFTEST_NOTES" "0"
# A work count for the contract half specifically: the self-test has to
# have driven at least one assertion per resolver branch, or the pinned
# total could be met by the instrument's own readings alone.
assert_ge "self-test covered every resolver branch" \
  "$(grep -c '^  OK   .*resolves to' "$SELFTEST_TXT" || true)" 5

# ---------------------------------------------------------------------------
# Section 2 -- the input exists, and it moves
# ---------------------------------------------------------------------------
echo
echo "=== 2/7 the demo carries a ViewModel's animation state ==="
POV_TXT="$TRACE_DIR/pov-probe.txt"
timeout "$RUN_TIMEOUT" "$PROBE" --tf-root "$TF" --demo "$DEMO_POV" \
  --drawn-model "$VM_MODEL" > "$TRACE_DIR/pov-probe.json" 2> "$POV_TXT"
assert_eq "pov probe rc" "$?" "0"
# One column per field, per target class. The positive control is the point: a
# rule that can only say "absent" measures nothing, and 197 classes DO carry
# m_nSequence on this recording.
assert_eq "pov CTFViewModel carries m_nSequence" \
  "$(rfield "$POV_TXT" 'vmfield class=CTFViewModel' sequence)" "1"
assert_eq "pov CTFViewModel carries m_flPlaybackRate" \
  "$(rfield "$POV_TXT" 'vmfield class=CTFViewModel' rate)" "1"
# The absence the task named as a field to wire: DT_BaseViewModel has no
# m_flCycle. It is asserted as a zero rather than described, so a protocol change
# that adds the slot turns this red instead of silently changing the contract.
assert_eq "pov CTFViewModel does NOT carry m_flCycle" \
  "$(rfield "$POV_TXT" 'vmfield class=CTFViewModel' cycle)" "0"
assert_eq "pov CTFPlayer carries m_nSequence (the T1 answer, re-measured)" \
  "$(rfield "$POV_TXT" 'vmfield class=CTFPlayer' sequence)" "0"
assert_eq "classes carrying m_nSequence" \
  "$(rsum "$POV_TXT" 'vmfield-summary' classesWithSequence)" "197"
assert_eq "classes carrying m_flCycle" \
  "$(rsum "$POV_TXT" 'vmfield-summary' classesWithCycle)" "195"
assert_eq "target classes found in the class table" \
  "$(rsum "$POV_TXT" 'vmfield-summary' targetClassesFound)" "3"
# The census reads EntityState::classId, not a property dump: a --props-at census
# reports ZERO ViewModel entities on a demo that has hundreds, because
# DT_BaseViewModel has no m_vecOrigin and the dump only prints entities that carry
# one. That is not a hypothesis; it happened while this probe was written.
assert_eq "pov CTFViewModel entities seen" \
  "$(rfield "$POV_TXT" 'vmcensus class=CTFViewModel' entities)" "18"
assert_ge "pov snapshots answered" "$(rsum "$POV_TXT" 'vmcensus-summary' ticksWithSnapshot)" 1
assert_ge "pov ViewModel observations" "$(rsum "$POV_TXT" 'vmanim-summary' observations)" 1
# The cursor moves. `changes` counts per-entity changes rather than samples, so a
# cursor that repeats cannot read as motion.
assert_ge "pov ViewModel sequence changes" "$(rsum "$POV_TXT" 'vmanim-summary' changes)" 2
assert_ge "pov distinct sequences" "$(rsum "$POV_TXT" 'vmanim-summary' distinctSequences)" 2
assert_eq "pov distinct playback rates" "$(rsum "$POV_TXT" 'vmanim-summary' distinctRates)" "5"
assert_eq "pov observed sequence range" \
  "$(rsum "$POV_TXT" 'vmanim-summary' minSequence),$(rsum "$POV_TXT" 'vmanim-summary' maxSequence)" "0,34"
# Every model the ViewModel entities name is a c_*_arms model, i.e. NOT the model
# the pass draws. Five distinct paths, none of them a v_* one.
assert_eq "pov distinct entity model paths" "$(rsum "$POV_TXT" 'vmanim-summary' modelPaths)" "5"
assert_eq "pov entity models that are v_* viewmodels" \
  "$(rsum "$POV_TXT" 'vmanim-summary' vStarModels)" "0"

# ---------------------------------------------------------------------------
# Section 3 -- two index spaces, measured from the models themselves
# ---------------------------------------------------------------------------
echo
echo "=== 3/7 the sequence the entity carried did not index the model T2 drew ==="
# This section is the FINDING the round rests on, kept as a reading after the fix
# rather than deleted by it: the two tables are different sizes, and the sequence
# the entity carried ran past the table T2 was drawing with. The measurement is
# taken between the two models, so it stays true whichever model the pass draws --
# what changed is that the pass no longer draws the wrong one.
DRAWN_TXT="$TRACE_DIR/model-drawn.txt"
ENTITY_TXT="$TRACE_DIR/model-entity.txt"
timeout "$RUN_TIMEOUT" "$PROBE" --tf-root "$TF" --model-sequences "$VM_MODEL" \
  > /dev/null 2> "$DRAWN_TXT"
assert_eq "drawn model sequence count" "$(rfield "$DRAWN_TXT" modelseq sequences)" "7"
assert_eq "drawn model bone count" "$(rfield "$DRAWN_TXT" modelseq bones)" "45"
timeout "$RUN_TIMEOUT" "$PROBE" --tf-root "$TF" --model-sequences "$ENTITY_MODEL" \
  > /dev/null 2> "$ENTITY_TXT"
# The product's `--viewmodel` flag refuses this path on purpose -- it insists on a
# `v_`-prefixed name -- which is why the probe has a mode for it: the question
# "how many sequences does the model the entity names have?" is unanswerable
# through that flag. (The shipped pass reads the same model, but from the entity's
# m_nModelIndex rather than from a command line, so no flag names it.)
assert_eq "entity model sequence count" "$(rfield "$ENTITY_TXT" modelseq sequences)" "43"
assert_eq "entity model bone count" "$(rfield "$ENTITY_TXT" modelseq bones)" "58"
assert_eq "entity model resolved" "$(rfield "$ENTITY_TXT" modelseq readable)" "1"
# How many of those 43 sequences can actually produce a pose, and which one is
# first. T6 moved both numbers: the entity's own model carries a stub table (one
# 1-frame animation, `@ref`) and declares c_sniper_animations.mdl, so the count
# below is the sequences bound BY NAME through that declared companion -- 38 of
# 43. The five that stay unbound are named in the report rather than folded into
# the count: `ref` (the stub's own label) and the four `rifolver_*` overlays.
assert_eq "entity model sequences that reach a descriptor" \
  "$(rfield "$ENTITY_TXT" modelseq animated)" "38"
assert_eq "entity model's first posable sequence" \
  "$(rfield "$ENTITY_TXT" modelseq firstAnimated)" "1"
assert_gt "the entity's table is larger than the drawn one" \
  "$(rfield "$ENTITY_TXT" modelseq sequences)" "$(rfield "$DRAWN_TXT" modelseq sequences)"
assert_gt "the observed sequences run past the drawn table" \
  "$(rsum "$POV_TXT" 'vmanim-summary' maxSequence)" "$(( $(rfield "$DRAWN_TXT" modelseq sequences) - 1 ))"
# And the two answers the probe keeps apart: 104 of 141 observed sequences are
# past the drawn table, 37 are inside it. Those 37 are the trap -- a value from
# the wrong table that happens to be legal for the right one -- and they are
# reported as their own count precisely so they are visible rather than flattering.
assert_eq "observed sequences inside the drawn table" \
  "$(rsum "$POV_TXT" 'vmmapping-summary' inRange)" "37"
assert_eq "observed sequences past the drawn table" \
  "$(rsum "$POV_TXT" 'vmmapping-summary' outOfRange)" "104"
assert_eq "entity model equals the drawn model" \
  "$(rsum "$POV_TXT" 'vmmapping-summary' entityModelMatchesDrawn)" "0"
assert_eq "index space verdict" "$(rsum "$POV_TXT" 'vmmapping-summary' indexSpace)" "mismatch"
# Snapshot for the --mutation verdict: the index-space verdict is computed in the
# probe from the two models' own tables, not through the resolver, so neutering
# the range branch must NOT move it.
MUT_INDEXSPACE=$(rsum "$POV_TXT" 'vmmapping-summary' indexSpace)

# ---------------------------------------------------------------------------
# Section 4 -- the pairing is read, and it does not always resolve
# ---------------------------------------------------------------------------
echo
echo "=== 4/7 the owning ViewModel entity is read, not guessed ==="
assert_eq "pov svc_SetView count" "$(rsum "$POV_TXT" 'vmlocal' setViewCount)" "1"
assert_eq "pov view entity" "$(rsum "$POV_TXT" 'vmlocal' viewEntity)" "18"
# Two, not one: Source's MAX_VIEWMODELS is 2, so "the ViewModel entity" is a
# choice rather than a lookup. The product's rule for that choice is asserted in
# section 5, where it names the rule it used.
assert_eq "pov ViewModel entities owned by the view entity" \
  "$(rsum "$POV_TXT" 'vmlocal' entitiesOwnedByViewEntity)" "2"
assert_ge "pov observations owned by the view entity" \
  "$(rsum "$POV_TXT" 'vmlocal' ownedByViewEntity)" 1

MATCHA_TXT="$TRACE_DIR/matcha-probe.txt"
timeout "$RUN_TIMEOUT" "$PROBE" --tf-root "$TF" --demo "$DEMO_MATCHA" \
  --drawn-model "$VM_MODEL" > "$TRACE_DIR/matcha-probe.json" 2> "$MATCHA_TXT"
assert_eq "matcha probe rc" "$?" "0"
# The negative half, and it is the more useful one: the same code path, the same
# class, and the pairing does not resolve. Without this the gate would assert a
# property of one recording as if it were a property of the mechanism.
assert_eq "matcha svc_SetView count" "$(rsum "$MATCHA_TXT" 'vmlocal' setViewCount)" "1"
assert_eq "matcha view entity" "$(rsum "$MATCHA_TXT" 'vmlocal' viewEntity)" "1"
assert_eq "matcha ViewModel entities owned by the view entity" \
  "$(rsum "$MATCHA_TXT" 'vmlocal' entitiesOwnedByViewEntity)" "0"
# A zero has to be earned: 30 ViewModel entities WERE in the snapshots, so the
# zero is "none of them names this player", not "nothing was looked at".
assert_ge "matcha ViewModel entities were seen" \
  "$(rfield "$MATCHA_TXT" 'vmcensus class=CTFViewModel' entities)" 1
assert_ge "matcha ViewModel observations" "$(rsum "$MATCHA_TXT" 'vmanim-summary' observations)" 1
assert_eq "matcha index space verdict" \
  "$(rsum "$MATCHA_TXT" 'vmmapping-summary' indexSpace)" "mismatch"

# ---------------------------------------------------------------------------
# The product runs
# ---------------------------------------------------------------------------
# load_run <name> -> LAST_* from that run's trace. The trace lines carry a
# milliseconds-since-start prefix, stripped here so the printed line is the
# reading rather than a reading with a clock in front of it.
LAST_RC=0
LAST_ORDER=""
LAST_VMANIM=""
LAST_SUMMARY=""
load_run() {
  local name="$1"
  LAST_ORDER=$(grep -a "gpu: viewmodel pass order" "$TRACE_DIR/$name.trace" 2>/dev/null \
    | tr -d '\r' | tail -1 | sed 's/^[0-9]\{1,\}\(\.[0-9]*\)\? //')
  LAST_VMANIM=$(grep -a "scene: vmAnim entity" "$TRACE_DIR/$name.trace" 2>/dev/null \
    | tr -d '\r' | tail -1 | sed 's/^[0-9]\{1,\}\(\.[0-9]*\)\? //')
  LAST_SUMMARY=$(grep -a "scene: vmAnim summary" "$TRACE_DIR/$name.trace" 2>/dev/null \
    | tr -d '\r' | tail -1 | sed 's/^[0-9]\{1,\}\(\.[0-9]*\)\? //')
}
# run_product <name> <demo> <capture-tick> [extra args...]
run_product() {
  local name="$1" demo="$2" tick="$3"; shift 3
  local bmp="$TRACE_DIR/$name.bmp"
  rm -f "$bmp" "$TRACE_DIR/$name.csv"
  TF2_NATIVE_TRACE="$ROOT/$TRACE_DIR/$name.trace" timeout "$RUN_TIMEOUT" "$EXE" \
    --tf-root "$TF" --audio-device "$AUDIO_DEVICE" --demo "$demo" \
    --hidden-window --audio-disabled \
    --settings-file "$TRACE_DIR/$name-settings.ini" \
    --capture-frame "$bmp" --capture-tick "$tick" \
    --metrics-file "$TRACE_DIR/$name.csv" \
    --viewmodel "$VM_MODEL" --viewmodel-fov 90 "$@" > "$TRACE_DIR/$name.log" 2>&1
  LAST_RC=$?
  load_run "$name"
  echo "  $name rc=$LAST_RC bmp=$(stat -c%s "$bmp" 2>/dev/null || echo 0)"
  [ -n "$LAST_VMANIM" ] && echo "  $LAST_VMANIM"
  [ -n "$LAST_ORDER" ] && echo "  $LAST_ORDER"
}

# ---------------------------------------------------------------------------
# Section 5 -- the refusal, on a real demo
# ---------------------------------------------------------------------------
echo
echo "=== 5/7 a model the entity did not name is refused, not drawn ==="
# Capture tick 0 is a *playback position*, not a demo tick: this recording's first
# packet is at demo tick 29368, so playback 0 already has entities in the
# snapshot. The pass itself needs no BSP -- the animation input is read from the
# entity snapshot, not from the world -- so the refusal is observable regardless
# of whether this demo's map is installed.
#
# (a) The pass is TOLD to draw the model on its own command line, which is what
#     T2 always did and what `--viewmodel-render command` restores. The entity
#     names a different model, so the proof must refuse and must name BOTH paths.
#     This is the round's negative case at the product level, and it is the only
#     reading `--mutation` moves.
run_product pov-refusal "$DEMO_POV" 0 --viewmodel-render command
assert_eq "pov refusal rc" "$LAST_RC" "0"
assert_eq "pov refusal draws what it was told to draw" "$(field "$LAST_ORDER" drawnModel)" "$VM_MODEL"
if [ "$MUTATION" -eq 1 ]; then
  assert_eq "pov refusal vmAnimInput" "$(field "$LAST_ORDER" vmAnimInput)" "sequenceOutOfRange"
  # The mutation's own signature, and the sharpest statement of what it removed:
  # with the proof neutered the resolver falls through and ASSERTS the match it was
  # supposed to check, so this reading goes from 0 to 1 on a frame where the pass
  # drew a model the entity never named. Neutering a checker does not merely stop
  # it refusing -- it makes it claim the opposite, which is why the reading is
  # pinned in both directions rather than branched away.
  assert_eq "pov refusal entity model does not match the drawn one" \
    "$(field "$LAST_ORDER" entityModelMatchesDrawn)" "1"
else
  assert_eq "pov refusal vmAnimInput" "$(field "$LAST_ORDER" vmAnimInput)" "modelMismatch"
  assert_eq "pov refusal entity model does not match the drawn one" \
    "$(field "$LAST_ORDER" entityModelMatchesDrawn)" "0"
fi
assert_eq "pov refusal the range question was never reached" \
  "$(field "$LAST_ORDER" sequenceInRange)" "0"
assert_eq "pov refusal vmAnimApplied" "$(field "$LAST_ORDER" vmAnimApplied)" "0"
assert_eq "pov refusal skinning" "$(field "$LAST_ORDER" skinning)" "0"
# No table behind a refusal, so the pair the pass would compare is 0/0 rather
# than the previous frame's upload.
assert_eq "pov refusal boneTableCoverage" "$(field "$LAST_ORDER" boneTableCoverage)" "0/0"
# The effect reading agrees with the contract reading here, because nothing
# skinned. The two diverge only when a pose IS applied (section 6).
if [ "$MUTATION" -eq 1 ]; then
  assert_eq "pov refusal animationInput" "$(field "$LAST_ORDER" animationInput)" "sequenceOutOfRange"
else
  assert_eq "pov refusal animationInput" "$(field "$LAST_ORDER" animationInput)" "modelMismatch"
fi
POV_READING=$(grep -a "viewModelPath=" "$TRACE_DIR/pov-refusal.trace" 2>/dev/null | tr -d '\r' | tail -1 \
  | sed 's/^[0-9]\{1,\}\(\.[0-9]*\)\? //')
assert_eq "pov refusal drawn" "$(field "$POV_READING" drawn)" "1"
# The entity the lookup selected and the state it carried are unchanged by the
# refusal: what the proof rejects is the drawing, not the input.
assert_eq "pov refusal entity" "$(field "$LAST_VMANIM" entity)" "1077"
assert_eq "pov refusal sequence" "$(field "$LAST_VMANIM" sequence)" "29"
# The absence, on the product's own line: the field the task named is not there.
assert_eq "pov refusal cyclePresent" "$(field "$LAST_VMANIM" cyclePresent)" "0"
assert_eq "pov refusal poseAvailable" "$(field "$LAST_VMANIM" poseAvailable)" "0"
assert_eq "pov refusal applied" "$(field "$LAST_VMANIM" applied)" "0"
if [ "$MUTATION" -eq 0 ]; then
  # The phrase is the modelMismatch reason's own, and it appears nowhere else on
  # the line -- so this is a check that the refusal was LABELLED as a mismatch,
  # not a check that the word "model" appears somewhere in the log.
  assert_contains "pov refusal reason names both paths" "$LAST_VMANIM" "the pass draws"
fi
assert_ge "pov refusal observations" "$(field "$LAST_SUMMARY" observations)" 1
if [ "$MUTATION" -eq 1 ]; then
  assert_eq "pov refusal mismatch count" "$(field "$LAST_SUMMARY" modelMismatch)" "0"
  assert_eq "pov refusal out-of-range count" \
    "$(field "$LAST_SUMMARY" outOfRange)" "$(field "$LAST_SUMMARY" observations)"
else
  assert_eq "pov refusal mismatch count" \
    "$(field "$LAST_SUMMARY" modelMismatch)" "$(field "$LAST_SUMMARY" observations)"
  assert_eq "pov refusal out-of-range count" "$(field "$LAST_SUMMARY" outOfRange)" "0"
fi
# Snapshot for the --mutation verdict (see the --mutation block above): these
# three are the readings a neutered proof must move.
MUT_POV_VMANIM=$(field "$LAST_ORDER" vmAnimInput)
MUT_POV_POSE=$(field "$LAST_VMANIM" poseAvailable)
MUT_POV_MISMATCH=$(field "$LAST_SUMMARY" modelMismatch)
MUT_POV_OBS=$(field "$LAST_SUMMARY" observations)
# The whole line, because the reason is several words and `field` splits on
# spaces: the mutation has to stop the reason saying which two models disagreed.
MUT_POV_LINE="$LAST_VMANIM"

# (b) The same recording in the shipped mode. The pass draws the entity's own
#     model, the sequence is measured against that model's own table, and the
#     pose RESOLVES -- by name, through the animations companion the model
#     declares (T6). Nothing is applied: the default run resolves and keeps the
#     bind pose, which is what the opt-in boundary means.
run_product pov-companion "$DEMO_POV" 0
assert_eq "pov companion rc" "$LAST_RC" "0"
assert_eq "pov companion draws the entity's own model" "$(field "$LAST_ORDER" drawnModel)" "$ENTITY_MODEL"
assert_eq "pov companion entity model matches the drawn model" \
  "$(field "$LAST_ORDER" entityModelMatchesDrawn)" "1"
assert_eq "pov companion sequence inside the entity's own table" \
  "$(field "$LAST_ORDER" sequenceInRange)" "1"
assert_eq "pov companion status" "$(field "$LAST_ORDER" vmAnimInput)" "ok"
assert_eq "pov companion no override was in force" "$(field "$LAST_ORDER" sequenceOverride)" "-1"
assert_eq "pov companion no model mismatch" "$(field "$LAST_SUMMARY" modelMismatch)" "0"
assert_eq "pov companion every observation matched" \
  "$(field "$LAST_SUMMARY" modelMatches)" "$(field "$LAST_SUMMARY" observations)"
assert_eq "pov companion every sequence was in range" \
  "$(field "$LAST_SUMMARY" sequenceInRange)" "$(field "$LAST_SUMMARY" observations)"
assert_eq "pov companion refused nothing for a range reason" "$(field "$LAST_SUMMARY" outOfRange)" "0"
# The pose the entity carried resolves to a real animation: sequence 29 binds by
# name to the companion's own clip for that label, 51 frames long. The base
# model's stub table could pose nothing here, so the frame count is the reading
# that says the adoption happened and reached a real clip.
assert_eq "pov companion pose is available" "$(field "$LAST_VMANIM" poseAvailable)" "1"
assert_eq "pov companion frame count is the companion's" \
  "$(field "$LAST_VMANIM" frame | cut -d/ -f2)" "51"
# Opt-in, in both fields: the resolver accepted and the pass applied nothing.
assert_eq "pov companion applies nothing by default" "$(field "$LAST_VMANIM" applied)" "0"
assert_eq "pov companion skins nothing by default" "$(field "$LAST_ORDER" skinning)" "0"
# Control for the --mutation verdict: the proof never fires on a matching pair, so
# neutering it must leave this count exactly where it was.
MUT_COMPANION_MATCHES=$(field "$LAST_SUMMARY" modelMatches)
MUT_COMPANION_OBS=$(field "$LAST_SUMMARY" observations)

# The other refusal, from the recording whose pairing does not resolve. Same
# status field, different reason, and it is the reason that says which of the two
# absences it was.
run_product matcha-pairing "$DEMO_MATCHA" 0
assert_eq "matcha pairing rc" "$LAST_RC" "0"
assert_eq "matcha pairing vmAnimInput" "$(field "$LAST_ORDER" vmAnimInput)" "missing"
assert_eq "matcha pairing vmAnimApplied" "$(field "$LAST_ORDER" vmAnimApplied)" "0"
assert_eq "matcha pairing skinning" "$(field "$LAST_ORDER" skinning)" "0"
assert_eq "matcha pairing entity" "$(field "$LAST_VMANIM" entity)" "-1"
assert_contains "matcha pairing names the absence" "$LAST_VMANIM" "no viewmodel entity owned by the view entity"
assert_contains "matcha pairing says nothing was there" "$LAST_VMANIM" "the snapshot holds no ViewModel entity"
assert_ge "matcha pairing observations" "$(field "$LAST_SUMMARY" observations)" 1
assert_eq "matcha pairing missing count" "$(field "$LAST_SUMMARY" missing)" "$(field "$LAST_SUMMARY" observations)"
# Control for the --mutation verdict: no owning entity is resolved before the
# model check runs, so this must stay -1 whatever the model check does.
MUT_MATCHA_ENTITY=$(field "$LAST_VMANIM" entity)

# ---------------------------------------------------------------------------
# Section 6 -- the accepted pose, on real animation, with the stub as the negative
# ---------------------------------------------------------------------------
echo
echo "=== 6/7 the accepted pose is on a real animation; the stub is the negative ==="
# WHAT CHANGED, AND WHY THIS SECTION IS SHAPED THIS WAY. Before T6 the model the
# entity names could pose sequence 0 -- a one-frame stub -- and nothing else, so
# an acceptance could only be CONSTRUCTED, and the natural run was asserted to
# accept nothing at all. T6 measured why: the arms/player models carry a stub
# animation table and declare a *_animations.mdl companion that holds the real
# set, and the resolver now adopts the companion when it binds STRICTLY MORE of
# the model's own sequences by name. So the natural run accepts, and the pieces
# are pinned apart instead of folded together: the natural history (all of its
# observations accepted, on the entity's own table), the real animation behind
# one specific sequence (30 -> 61 frames), the stub kept as its own refusal
# (sequence 0, `ref`, no companion binding), and the opt-in boundary asserted in
# both directions.
run_product pov-history "$DEMO_POV" 2000
assert_eq "pov history rc" "$LAST_RC" "0"
assert_eq "pov history draws the entity's own model" "$(field "$LAST_ORDER" drawnModel)" "$ENTITY_MODEL"
# Observation count is playback throughput, which varies with startup latency and
# the audio/render loop. The semantic checks below already require a non-empty
# history, a sequence change, and distinct sequence values; do not turn a machine
# speed sample into a correctness threshold.
assert_ge "pov history observations" "$(field "$LAST_SUMMARY" observations)" 1
assert_eq "pov history accepted every observation" \
  "$(field "$LAST_SUMMARY" ok)" "$(field "$LAST_SUMMARY" observations)"
assert_eq "pov history refused nothing" "$(field "$LAST_SUMMARY" missing)" "0"
assert_eq "pov history matched every observation" \
  "$(field "$LAST_SUMMARY" modelMatches)" "$(field "$LAST_SUMMARY" observations)"
assert_eq "pov history measured every sequence inside the entity's own table" \
  "$(field "$LAST_SUMMARY" sequenceInRange)" "$(field "$LAST_SUMMARY" observations)"
assert_eq "pov history no model mismatch" "$(field "$LAST_SUMMARY" modelMismatch)" "0"
assert_eq "pov history applied nothing without the flag" "$(field "$LAST_SUMMARY" applied)" "0"
POV_RANGE=$(field "$LAST_SUMMARY" range)
assert_eq "pov history sequence range" "$POV_RANGE" "1..30"
assert_ge "pov history sequence changes" "$(field "$LAST_SUMMARY" changes)" 1
# The number the acceptance rests on: the last observed sequence's animation.
# Sequence 30 binds BY NAME to the companion's 61-frame clip; the stub table the
# model carries itself could pose nothing here.
assert_eq "pov history frame count is the companion's" \
  "$(field "$LAST_VMANIM" frame | cut -d/ -f2)" "61"
# Mutations snapshot, taken here while LAST_SUMMARY is the history run: under the
# adoption-neutered mutation every natural observation goes back to refused, so
# this reading moves from observations to 0 while the model proof (a matching
# pair) is untouched.
MUT_HISTORY_OK=$(field "$LAST_SUMMARY" ok)
MUT_HISTORY_OBS=$(field "$LAST_SUMMARY" observations)

# The stub, kept as the negative the adoption must not paper over: sequence 0
# names the base model's own one-frame animation and no companion sequence
# carries that label, so the range question and the pose question answer
# differently -- in range, and refused by name.
run_product pov-stub "$DEMO_POV" 0 --viewmodel-sequence 0
assert_eq "stub sequence: rc" "$LAST_RC" "0"
assert_eq "stub sequence: inside the entity's own table" \
  "$(field "$LAST_ORDER" sequenceInRange)" "1"
assert_eq "stub sequence: refused" "$(field "$LAST_ORDER" vmAnimInput)" "missing"
assert_contains "stub sequence: refused by name" "$LAST_VMANIM" "sequence has no animation binding"
assert_eq "stub sequence: no pose" "$(field "$LAST_VMANIM" poseAvailable)" "0"
assert_eq "stub sequence: applied" "$(field "$LAST_VMANIM" applied)" "0"
assert_eq "stub sequence: the override is reported" \
  "$(field "$LAST_ORDER" sequenceOverride)" "0"

# The first sequence the model can pose, read out of the model rather than
# written here, is now the first one the companion binds by name (`r_handposes`
# -> the companion's one-frame `saxxy_pose`).
POSE_SEQUENCE=$(rfield "$ENTITY_TXT" modelseq firstAnimated)
assert_eq "the first sequence the entity's model can pose, by name" "$POSE_SEQUENCE" "1"
assert_eq "and the recording carries it" "${POV_RANGE%%..*}" "$POSE_SEQUENCE"

run_product pov-pose-bind "$DEMO_POV" 0 --viewmodel-sequence "$POSE_SEQUENCE"
assert_eq "accepted pose, default: rc" "$LAST_RC" "0"
assert_eq "accepted pose, default: vmAnimInput" "$(field "$LAST_ORDER" vmAnimInput)" "ok"
assert_eq "accepted pose, default: entity model matches the drawn model" \
  "$(field "$LAST_ORDER" entityModelMatchesDrawn)" "1"
assert_eq "accepted pose, default: sequence inside the entity's own table" \
  "$(field "$LAST_ORDER" sequenceInRange)" "1"
assert_eq "accepted pose, default: the override is reported" \
  "$(field "$LAST_ORDER" sequenceOverride)" "$POSE_SEQUENCE"
assert_eq "accepted pose, default: vmAnimApplied" "$(field "$LAST_ORDER" vmAnimApplied)" "0"
assert_eq "accepted pose, default: skinning" "$(field "$LAST_ORDER" skinning)" "0"
assert_eq "accepted pose, default: boneTableCoverage" "$(field "$LAST_ORDER" boneTableCoverage)" "0/0"
assert_eq "accepted pose, default: poseAvailable" "$(field "$LAST_VMANIM" poseAvailable)" "1"
assert_eq "accepted pose, default: applied" "$(field "$LAST_VMANIM" applied)" "0"
# This sequence binds by name to the companion's one-frame `saxxy_pose`, so the
# frame count is a reading about that animation rather than about the tick.
assert_eq "accepted pose, default: frame count" "$(field "$LAST_VMANIM" frame | cut -d/ -f2)" "1"

run_product pov-pose-apply "$DEMO_POV" 0 --viewmodel-sequence "$POSE_SEQUENCE" --viewmodel-apply-animation
assert_eq "accepted pose, applied: rc" "$LAST_RC" "0"
assert_eq "accepted pose, applied: vmAnimInput" "$(field "$LAST_ORDER" vmAnimInput)" "ok"
assert_eq "accepted pose, applied: vmAnimApplied" "$(field "$LAST_ORDER" vmAnimApplied)" "1"
assert_eq "accepted pose, applied: skinning" "$(field "$LAST_ORDER" skinning)" "1"
assert_eq "accepted pose, applied: applied" "$(field "$LAST_VMANIM" applied)" "1"
# The effect reading and the contract reading diverge here, which is why they are
# two fields: the pass skinned, so `animationInput` reports the effect.
assert_contains "accepted pose, applied: animationInput reports the effect" \
  "$(field "$LAST_ORDER" animationInput)" "bones:"
# The uploaded table against the skeleton the mesh declares, and against the
# skeleton the model file itself carries -- the second number is what makes the
# first one more than a self-consistency check.
COVERAGE=$(field "$LAST_ORDER" boneTableCoverage)
assert_gt "accepted pose, applied: coverage numerator" "${COVERAGE%%/*}" 0
assert_eq "accepted pose, applied: the table covers the whole skeleton" \
  "${COVERAGE%%/*}" "${COVERAGE##*/}"
assert_eq "accepted pose, applied: the table is the model's own bone count" \
  "${COVERAGE%%/*}" "$(rfield "$ENTITY_TXT" modelseq bones)"
assert_eq "accepted pose, applied: every accepted pose was applied" \
  "$(field "$LAST_SUMMARY" applied)" "$(field "$LAST_SUMMARY" ok)"
# The one-frame reference pose renders identical to the bind pose (measured: 0
# bytes differ). That is a reading about THE POSE, not about the flag -- so it is
# recorded rather than asserted, and the flag's effect is measured on the motion
# pair below, where the pose and the bind are different pictures.
POSE_DIFF=$(byte_diff "$TRACE_DIR/pov-pose-bind.bmp" "$TRACE_DIR/pov-pose-apply.bmp")
echo "  ..   pov-pose-bind vs pov-pose-apply: $POSE_DIFF byte positions differ (the reference pose equals the bind pose)"

# The real motion, end to end: sequence 30 (the last sequence the recording
# carries) binds by name to a 61-frame clip. The natural run above already
# captured it unapplied; this run applies it, and the pair must move the picture
# -- the assertion the old round could not make, because this pose did not exist
# to be drawn.
run_product pov-motion-apply "$DEMO_POV" 2000 --viewmodel-sequence 30 --viewmodel-apply-animation
assert_eq "real motion, applied: rc" "$LAST_RC" "0"
assert_eq "real motion, applied: vmAnimApplied" "$(field "$LAST_ORDER" vmAnimApplied)" "1"
assert_eq "real motion, applied: skinning" "$(field "$LAST_ORDER" skinning)" "1"
assert_eq "real motion, applied: frame count" "$(field "$LAST_VMANIM" frame | cut -d/ -f2)" "61"
MOTION_COVERAGE=$(field "$LAST_ORDER" boneTableCoverage)
assert_eq "real motion, applied: the table covers the whole skeleton" \
  "${MOTION_COVERAGE%%/*}" "${MOTION_COVERAGE##*/}"
MOTION_DIFF=$(byte_diff "$TRACE_DIR/pov-history.bmp" "$TRACE_DIR/pov-motion-apply.bmp")
assert_gt "the applied real animation changes the frame" "$MOTION_DIFF" 0
echo "  ..   pov-history vs pov-motion-apply: $MOTION_DIFF byte positions differ"

# ---------------------------------------------------------------------------
# Section 7 -- the probe's mutations, paired
# ---------------------------------------------------------------------------
echo
echo "=== 7/7 the readings can go red ==="
# Each mutation breaks a different reading, so the assertion that has to move is
# named per mutation -- and each is paired with a control that must NOT move, or
# a mutation that collapsed everything would look like five independent
# detections. These five are runtime switches inside the probe, so they cost
# nothing and run on every invocation. The rebuilt, source-level half is
# `--mutation`, whose verdict is printed at the bottom of this script.
mutation_run() { # mutation_run <name>
  local name="$1"
  timeout "$RUN_TIMEOUT" "$PROBE" --tf-root "$TF" --demo "$DEMO_POV" \
    --drawn-model "$VM_MODEL" --mutation "$name" \
    > "$TRACE_DIR/mutation-$name.json" 2> "$TRACE_DIR/mutation-$name.txt"
}
MUTATIONS="census-skip-viewmodel sequence-frozen field-supply-collapse cycle-invented model-class-flip"
MUTATION_MOVED=0
MUTATION_HELD=0
for mutation in $MUTATIONS; do
  mutation_run "$mutation"
  txt="$TRACE_DIR/mutation-$mutation.txt"
  case "$mutation" in
    census-skip-viewmodel)
      assert_eq "mutation census-skip-viewmodel: the class census collapses" \
        "$(rfield "$txt" 'vmcensus class=CTFViewModel' entities)" "0"
      assert_eq "mutation census-skip-viewmodel: the field supply does not move" \
        "$(rsum "$txt" 'vmfield-summary' classesWithSequence)" "197"
      MUTATION_MOVED=$((MUTATION_MOVED + 1)); MUTATION_HELD=$((MUTATION_HELD + 1))
      ;;
    sequence-frozen)
      assert_ne "mutation sequence-frozen: the cursor stops moving" \
        "$(rsum "$txt" 'vmanim-summary' changes)" "141"
      assert_eq "mutation sequence-frozen: the entity count does not move" \
        "$(rsum "$txt" 'vmanim-summary' entities)" "18"
      MUTATION_MOVED=$((MUTATION_MOVED + 1)); MUTATION_HELD=$((MUTATION_HELD + 1))
      ;;
    field-supply-collapse)
      assert_eq "mutation field-supply-collapse: no class carries the fields" \
        "$(rsum "$txt" 'vmfield-summary' classesWithSequence)" "0"
      assert_eq "mutation field-supply-collapse: the census does not move" \
        "$(rfield "$txt" 'vmcensus class=CTFViewModel' entities)" "18"
      MUTATION_MOVED=$((MUTATION_MOVED + 1)); MUTATION_HELD=$((MUTATION_HELD + 1))
      ;;
    cycle-invented)
      # The absence becomes a presence. This is the mutation that would catch a
      # gate whose "m_flCycle is not on the wire" claim came from a defaulted
      # zero rather than from the class table.
      assert_eq "mutation cycle-invented: the absent slot reads present" \
        "$(rfield "$txt" 'vmfield class=CTFViewModel' cycle)" "1"
      assert_eq "mutation cycle-invented: the sequence slot does not move" \
        "$(rfield "$txt" 'vmfield class=CTFViewModel' sequence)" "1"
      MUTATION_MOVED=$((MUTATION_MOVED + 1)); MUTATION_HELD=$((MUTATION_HELD + 1))
      ;;
    model-class-flip)
      assert_ne "mutation model-class-flip: the entity's model reads as a viewmodel" \
        "$(rsum "$txt" 'vmanim-summary' vStarModels)" "0"
      assert_eq "mutation model-class-flip: the change count does not move" \
        "$(rsum "$txt" 'vmanim-summary' changes)" "141"
      MUTATION_MOVED=$((MUTATION_MOVED + 1)); MUTATION_HELD=$((MUTATION_HELD + 1))
      ;;
  esac
done
# Work counts, not just verdicts: a loop that ran zero mutations would satisfy
# "no assertion failed".
assert_eq "probe mutations that moved a reading" "$MUTATION_MOVED" "5"
assert_eq "probe mutation controls that held" "$MUTATION_HELD" "5"

# ---------------------------------------------------------------------------
# The verdict
# ---------------------------------------------------------------------------
# Under --mutation the sections are *supposed* to be red, so the ordinary verdict
# would be wrong in both directions: `VIEWMODEL-ANIMATION=FAIL` would read as a
# broken gate, and `PASS` would mean the mutation did nothing. The mutation is
# caught only when the named readings moved AND the controls held, so a mutation
# that collapsed the run -- making everything red for the wrong reason -- cannot
# be counted as a detection. Two coherent edits make the mutation: the model
# proof is neutered (viewmodel.cpp) and the companion adoption is neutered
# (animation_binding.cpp, T6), so the moved readings cover both.
echo
if [ "$MUTATION" -eq 1 ]; then
  mut_red=0
  mut_hold=0
  # The reason is several words, so the whole line is matched rather than a field.
  # `the pass draws` is that reason's own phrase and appears nowhere else on the
  # line, so this asks whether the refusal was LABELLED as a mismatch rather than
  # whether the word "model" occurs somewhere in the log.
  mut_labelled=0
  case "${MUT_POV_LINE:-}" in
    *"the pass draws"*) mut_labelled=1 ;;
  esac
  printf 'mutation readings:\n'
  printf '  refusal vmAnimInput  = %s (fixed build: modelMismatch)\n' "${MUT_POV_VMANIM:-<none>}"
  printf '  refusal reason names both paths = %s (fixed build: 1)\n' "$mut_labelled"
  printf '  refusal modelMismatch = %s of %s observations (fixed build: all of them)\n' \
    "${MUT_POV_MISMATCH:-<none>}" "${MUT_POV_OBS:-<none>}"
  printf '  history ok           = %s of %s (fixed build: all of them; neutered: 0)\n' \
    "${MUT_HISTORY_OK:-<none>}" "${MUT_HISTORY_OBS:-<none>}"
  printf '  control poseAvailable   = %s (must stay 0: still a refusal, only mislabelled)\n' "${MUT_POV_POSE:-<none>}"
  printf '  control companion match = %s of %s (must not move: the proof cannot fire on a match)\n' \
    "${MUT_COMPANION_MATCHES:-<none>}" "${MUT_COMPANION_OBS:-<none>}"
  printf '  control matcha entity   = %s (must stay -1)\n' "${MUT_MATCHA_ENTITY:-<none>}"
  printf '  control index space     = %s (must stay mismatch)\n' "${MUT_INDEXSPACE:-<none>}"
  [ "${MUT_POV_VMANIM:-}" != "modelMismatch" ] && mut_red=$((mut_red + 1))
  [ "$mut_labelled" -eq 0 ] && mut_red=$((mut_red + 1))
  if [ -n "${MUT_POV_OBS:-}" ] && [ "${MUT_POV_MISMATCH:-}" != "$MUT_POV_OBS" ]; then
    mut_red=$((mut_red + 1))
  fi
  if [ -n "${MUT_HISTORY_OBS:-}" ] && [ "${MUT_HISTORY_OK:-}" != "$MUT_HISTORY_OBS" ]; then
    mut_red=$((mut_red + 1))
  fi
  [ "${MUT_POV_POSE:-x}" = "0" ] && mut_hold=$((mut_hold + 1))
  if [ -n "${MUT_COMPANION_OBS:-}" ] && [ "${MUT_COMPANION_MATCHES:-}" = "$MUT_COMPANION_OBS" ]; then
    mut_hold=$((mut_hold + 1))
  fi
  [ "${MUT_MATCHA_ENTITY:-}" = "-1" ] && mut_hold=$((mut_hold + 1))
  [ "${MUT_INDEXSPACE:-}" = "mismatch" ] && mut_hold=$((mut_hold + 1))
  echo "mutation_red=$mut_red/4 hold=$mut_hold/4"
  # `fail` is part of the condition and not decoration. Every section under
  # --mutation asserts the value it is SUPPOSED to read in that mode (section 1
  # pins the mutated self-test counts, section 4 expects the degraded status), so
  # a section assertion going red here is a defect in the gate, not evidence about
  # the mutation -- and a verdict that ignored it would report a caught mutation
  # on a run whose own checks did not hold. The four readings and the four
  # controls say the mutation was CAUGHT; `fail=0` says the instrument was still
  # measuring while it caught it.
  if [ "$fail" -eq 0 ] && [ "$mut_red" -eq 4 ] && [ "$mut_hold" -eq 4 ]; then
    echo "MUTATION-CAUGHT=PASS (the model proof AND the companion adoption are what carry the animation readings; every control held)"
    exit 0
  fi
  echo "MUTATION-CAUGHT=FAIL (red=$mut_red/4 hold=$mut_hold/4 fail=$fail)"
  if [ "$fail" -ne 0 ]; then
    printf 'failed assertions:\n'
    printf '  %s\n' "${FAIL_LINES[@]}"
  fi
  exit 1
fi
if [ "$fail" -eq 0 ]; then
  echo "VIEWMODEL-ANIMATION=PASS"
else
  echo "VIEWMODEL-ANIMATION=FAIL"
  printf 'failed assertions:\n'
  printf '  %s\n' "${FAIL_LINES[@]}"
fi
exit "$fail"
