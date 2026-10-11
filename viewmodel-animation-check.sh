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
#              the models themselves.
#   Section 4  the pairing is READ, not guessed. svc_SetView names the view entity
#              and m_hOwner pairs it to a ViewModel entity. On the POV recording
#              that resolves (two entities); on the matcha recording it does NOT
#              (zero). Both are asserted, because "the pairing works" is true of
#              one recording rather than in general.
#   Section 5  the product's refusal on a real demo, with the bind pose left in
#              place and no bone table behind it.
#   Section 6  the acceptance branch is REACHABLE and OPT-IN. It is reachable or
#              section 5 is asserting a constant with extra steps; it is opt-in
#              because the accepted value is a value that happened to land inside
#              the drawn model's table, and applying it would draw a scattergun
#              pose on a sniper rifle. The pair (vmAnimInput=ok, vmAnimApplied=0)
#              by default and a byte-different frame with the flag is the whole
#              statement.
#   Section 7  five probe mutations, each with a named assertion that must go red
#              AND a control that must not move. These are runtime switches inside
#              the probe, so they cost nothing and run on every invocation; the
#              rebuilt, source-level half is `--mutation`, which neuters the
#              resolver's range branch in the product's own source.
#
# Usage: bash viewmodel-animation-check.sh [--mutation]
#   --mutation reintroduces the defect the refusal path exists to catch -- the
#   resolver's range branch stops firing -- rebuilds, and requires the named
#   readings to move while the named controls hold. It prints MUTATION-CAUGHT=...
#   rather than the ordinary verdict, because under --mutation the sections are
#   supposed to be red.
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
# The model the pass draws. Seven sequences, which is what makes the entity's
# observed 0..34 mostly out of range -- and what makes an accepted value a
# coincidence of the tables rather than a mapping.
VM_MODEL="models/weapons/v_models/v_scattergun_scout.mdl"
# The model the ViewModel ENTITY names, read only to count its sequences. The
# product refuses a non-v_ path on purpose, which is why the probe has a mode for
# it: "how many sequences does the model the entity names have" is the question
# that decides whether the two index spaces really differ.
ENTITY_MODEL="models/weapons/c_models/c_sniper_arms.mdl"
INPUTS=evidence/viewmodel/animation-inputs.txt

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
BUILD_LOCK_OWNED=0
PARENT_LOCK_HELD=0
MUTATION_ACTIVE=0
MUTATION_ORIGINAL_SHA=""
restore_mutation() {
  [ "$MUTATION_ACTIVE" -eq 1 ] || return 0
  local rc=0
  git checkout -- "$MUTATION_SRC" >/dev/null 2>&1
  if [ -n "$MUTATION_ORIGINAL_SHA" ]; then
    restored_sha=$(git hash-object "$MUTATION_SRC" 2>/dev/null || true)
    if [ "$restored_sha" != "$MUTATION_ORIGINAL_SHA" ]; then
      echo "FATAL: mutation source was not restored byte-for-byte"
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
# --mutation: reintroduce the defect the refusal path exists to catch
# ---------------------------------------------------------------------------
# The range branch is the line that gives the refusal its NAME. With it in place
# the entity's sequence (0..34 on this recording) is measured against the drawn
# model's own seven-entry table, and the 104 of 141 observations that fall past it
# are refused as `sequenceOutOfRange`, with the drawn table named in the reason.
#
# Neutering it does NOT make an unsafe pose available -- and that is the finding,
# not a defect in the mutation. The T1 layer refuses an out-of-range sequence on
# its own, so the sample comes back as a bind pose and the status degrades to
# `missing`. What the branch owns is therefore the *label*, and the reading it
# degrades to is exactly what T2 shipped: not a wrong picture, but a refusal that
# does not say which refusal it was. So the three readings that must move are the
# status, the reason, and the out-of-range count; and the three controls that must
# NOT move are the pose availability (the refusal is still a refusal, only
# unlabelled), the matcha pairing (guarded before the range check) and the probe's
# index-space verdict (computed from the two models' own tables, not through the
# resolver).
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
  "$PY" - "$MUTATION_SRC" <<'PYEOF' || { echo "FATAL: the mutation did not land"; exit 1; }
import io, sys
path = sys.argv[1]
find = "  if (static_cast<std::size_t>(input.sequence) >= drawnSequences) {"
repl = ("  // MUTATION: the range branch never fires, so every sequence is accepted.\n"
        "  if (false && static_cast<std::size_t>(input.sequence) >= drawnSequences) {")
raw = io.open(path, 'rb').read()
crlf = b'\r\n' in raw
data = raw.decode('utf-8')
if crlf:
    find = find.replace('\n', '\r\n')
    repl = repl.replace('\n', '\r\n')
hits = data.count(find)
if hits != 1:
    sys.exit('the mutation anchor matched %d times, expected exactly 1' % hits)
io.open(path, 'wb').write(data.replace(find, repl, 1).encode('utf-8'))
PYEOF
  MUTATION_ACTIVE=1
  if ! bash build-target.sh tf2_demo_native viewmodel_fov_probe viewmodel_contract_probe \
      viewmodel_animation_probe animation_viewmodel_probe > .scratch/vm-anim-mutation.log 2>&1; then
    echo "FATAL: the mutated build failed; see .scratch/vm-anim-mutation.log"
    exit 1
  fi
  echo "mutation: the resolver's range branch is neutered (rebuilt)"
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
  assert_eq "self-test failures" "$SELFTEST_FAIL" "1"
else
  assert_eq "self-test rc" "$SELFTEST_RC" "0"
  assert_eq "self-test failures" "$SELFTEST_FAIL" "0"
fi
# Pinned, because a gate that prints PASS while its assertions quietly disappear
# is not a gate. 49 is: 23 for the T2.5 instrument's own readings (the path rule,
# the three-outcome range classifier, the series counters, the field-supply
# counter, and the product cross-check), + 26 for the resolver contract -- the
# drawn model resolving, its table and skeleton being non-empty, the binding
# resolving and matching the skeleton, and then every branch with its own
# assertion: missing (3), absent sequence (2), out of range (3), accepted (3),
# bone mismatch (2), no animation data (1), unresolved drawn model (2), and the
# cycle witness (4).
if [ "$MUTATION" -eq 1 ]; then
  assert_eq "self-test assertions" "$SELFTEST_OK" "48"
  assert_eq "self-test expected mutation failure" "$SELFTEST_FAIL" "1"
else
  assert_eq "self-test assertions" "$SELFTEST_OK" "49"
  assert_eq "self-test expected mutation failure" "$SELFTEST_FAIL" "0"
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
echo "=== 3/7 the sequence the entity carries does not index the drawn model ==="
DRAWN_TXT="$TRACE_DIR/model-drawn.txt"
ENTITY_TXT="$TRACE_DIR/model-entity.txt"
timeout "$RUN_TIMEOUT" "$PROBE" --tf-root "$TF" --model-sequences "$VM_MODEL" \
  > /dev/null 2> "$DRAWN_TXT"
assert_eq "drawn model sequence count" "$(rfield "$DRAWN_TXT" modelseq sequences)" "7"
assert_eq "drawn model bone count" "$(rfield "$DRAWN_TXT" modelseq bones)" "45"
timeout "$RUN_TIMEOUT" "$PROBE" --tf-root "$TF" --model-sequences "$ENTITY_MODEL" \
  > /dev/null 2> "$ENTITY_TXT"
# The product refuses this path on purpose, which is why the probe has a mode for
# it: the question "how many sequences does the model the entity names have?" is
# unanswerable through the product path.
assert_eq "entity model sequence count" "$(rfield "$ENTITY_TXT" modelseq sequences)" "43"
assert_eq "entity model bone count" "$(rfield "$ENTITY_TXT" modelseq bones)" "58"
assert_eq "entity model resolved" "$(rfield "$ENTITY_TXT" modelseq readable)" "1"
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
echo "=== 5/7 the product refuses an out-of-range sequence and keeps the bind pose ==="
# Capture tick 0 is a *playback position*, not a demo tick: this recording's first
# packet is at demo tick 29368, so playback 0 already has entities in the
# snapshot. The pass itself needs no BSP -- the animation input is read from the
# entity snapshot, not from the world -- so the refusal is observable regardless
# of whether this demo's map is installed.
run_product pov-refusal "$DEMO_POV" 0
assert_eq "pov refusal rc" "$LAST_RC" "0"
if [ "$MUTATION" -eq 1 ]; then
  assert_eq "pov refusal vmAnimInput" "$(field "$LAST_ORDER" vmAnimInput)" "missing"
else
  assert_eq "pov refusal vmAnimInput" "$(field "$LAST_ORDER" vmAnimInput)" "sequenceOutOfRange"
fi
assert_eq "pov refusal vmAnimApplied" "$(field "$LAST_ORDER" vmAnimApplied)" "0"
assert_eq "pov refusal skinning" "$(field "$LAST_ORDER" skinning)" "0"
# No table behind a refusal, so the pair the pass would compare is 0/0 rather
# than the previous frame's upload.
assert_eq "pov refusal boneTableCoverage" "$(field "$LAST_ORDER" boneTableCoverage)" "0/0"
# The effect reading agrees with the contract reading here, because nothing
# skinned. The two diverge only when a pose IS applied (section 6).
if [ "$MUTATION" -eq 1 ]; then
  assert_eq "pov refusal animationInput" "$(field "$LAST_ORDER" animationInput)" "missing"
else
  assert_eq "pov refusal animationInput" "$(field "$LAST_ORDER" animationInput)" "sequenceOutOfRange"
fi
POV_READING=$(grep -a "viewModelPath=" "$TRACE_DIR/pov-refusal.trace" 2>/dev/null | tr -d '\r' | tail -1 \
  | sed 's/^[0-9]\{1,\}\(\.[0-9]*\)\? //')
assert_eq "pov refusal drawn" "$(field "$POV_READING" drawn)" "1"
assert_eq "pov refusal entity" "$(field "$LAST_VMANIM" entity)" "1077"
assert_eq "pov refusal sequence" "$(field "$LAST_VMANIM" sequence)" "29"
# The absence, on the product's own line: the field the task named is not there.
assert_eq "pov refusal cyclePresent" "$(field "$LAST_VMANIM" cyclePresent)" "0"
assert_eq "pov refusal poseAvailable" "$(field "$LAST_VMANIM" poseAvailable)" "0"
assert_eq "pov refusal applied" "$(field "$LAST_VMANIM" applied)" "0"
assert_contains "pov refusal names the entity's own model" "$LAST_VMANIM" "c_sniper_arms.mdl"
if [ "$MUTATION" -eq 0 ]; then
  assert_contains "pov refusal reason names the drawn table" "$LAST_VMANIM" "past the drawn model's 7 sequences"
fi
assert_ge "pov refusal observations" "$(field "$LAST_SUMMARY" observations)" 1
if [ "$MUTATION" -eq 1 ]; then
  assert_eq "pov refusal out-of-range count" "$(field "$LAST_SUMMARY" outOfRange)" "0"
else
  assert_eq "pov refusal out-of-range count" "$(field "$LAST_SUMMARY" outOfRange)" "$(field "$LAST_SUMMARY" observations)"
fi
# Snapshot for the --mutation verdict (see the --mutation block above): these
# three are the readings a neutered range branch must move.
MUT_POV_VMANIM=$(field "$LAST_ORDER" vmAnimInput)
MUT_POV_POSE=$(field "$LAST_VMANIM" poseAvailable)
MUT_POV_OOR=$(field "$LAST_SUMMARY" outOfRange)
MUT_POV_OBS=$(field "$LAST_SUMMARY" observations)
# The whole line, because the reason is several words and `field` splits on
# spaces: the mutation has to stop the reason naming the drawn table.
MUT_POV_LINE="$LAST_VMANIM"

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
# range check runs, so this must stay -1 even with the range branch neutered.
MUT_MATCHA_ENTITY=$(field "$LAST_VMANIM" entity)

# ---------------------------------------------------------------------------
# Section 6 -- the acceptance branch is reachable, and opt-in
# ---------------------------------------------------------------------------
echo
echo "=== 6/7 the accepted pose is available but not applied by default ==="
# The tick is DERIVED, not hardcoded: the long run records the sequence history,
# and the first scene update whose status is ok is the one this section compares.
# A hardcoded tick would be a reading about this demo's content with nothing
# saying how it was found; deriving it means a demo change reddens the derivation
# instead of silently comparing two refusal frames.
run_product pov-history "$DEMO_POV" 2000
assert_eq "pov history rc" "$LAST_RC" "0"
OK_TICK=$(grep -a -E "scene: begin|scene: vmAnim entity" "$TRACE_DIR/pov-history.trace" | tr -d '\r' \
  | awk '/scene: begin/{ pt=$4 } /scene: vmAnim entity/{ if ($0 ~ /status=ok/) { sub("tick=", "", pt); print pt; exit } }')
if [ -z "$OK_TICK" ]; then
  echo "  FAIL no scene update reached an accepted pose, so the acceptance branch was never taken"
  fail=1; FAIL_LINES+=("ok tick: not derived")
  OK_TICK=0
else
  echo "  OK   the first accepted pose is at playback tick $OK_TICK (derived from the run)"
fi
assert_ge "pov history accepted poses" "$(field "$LAST_SUMMARY" ok)" 1
assert_eq "pov history applied poses without the flag" "$(field "$LAST_SUMMARY" applied)" "0"
if [ "$MUTATION" -eq 1 ]; then
  # The range branch is deliberately disabled in this mode; T1 still refuses
  # the sample, but the status degrades from sequenceOutOfRange to missing.
  assert_eq "pov history refusals" "$(field "$LAST_SUMMARY" outOfRange)" "0"
else
  assert_ge "pov history refusals" "$(field "$LAST_SUMMARY" outOfRange)" 1
fi
# Both statuses occur in one run on one recording. That is the coincidence trap
# made visible: the same entity's sequence lands inside the drawn table sometimes
# and outside it the rest of the time, and only one of those is a real refusal.
assert_ge "pov history sequence changes" "$(field "$LAST_SUMMARY" changes)" 1

run_product ok-bind "$DEMO_POV" "$OK_TICK"
assert_eq "accepted pose, default: rc" "$LAST_RC" "0"
assert_eq "accepted pose, default: vmAnimInput" "$(field "$LAST_ORDER" vmAnimInput)" "ok"
assert_eq "accepted pose, default: vmAnimApplied" "$(field "$LAST_ORDER" vmAnimApplied)" "0"
assert_eq "accepted pose, default: skinning" "$(field "$LAST_ORDER" skinning)" "0"
assert_eq "accepted pose, default: boneTableCoverage" "$(field "$LAST_ORDER" boneTableCoverage)" "0/0"
assert_eq "accepted pose, default: poseAvailable" "$(field "$LAST_VMANIM" poseAvailable)" "1"
assert_eq "accepted pose, default: applied" "$(field "$LAST_VMANIM" applied)" "0"
assert_ge "accepted pose, default: frame count" "$(field "$LAST_VMANIM" frame | cut -d/ -f2)" 2

run_product ok-apply "$DEMO_POV" "$OK_TICK" --viewmodel-apply-animation
assert_eq "accepted pose, applied: rc" "$LAST_RC" "0"
assert_eq "accepted pose, applied: vmAnimInput" "$(field "$LAST_ORDER" vmAnimInput)" "ok"
assert_eq "accepted pose, applied: vmAnimApplied" "$(field "$LAST_ORDER" vmAnimApplied)" "1"
assert_eq "accepted pose, applied: skinning" "$(field "$LAST_ORDER" skinning)" "1"
assert_eq "accepted pose, applied: applied" "$(field "$LAST_VMANIM" applied)" "1"
# The effect reading and the contract reading diverge here, which is why they are
# two fields: the pass skinned, so `animationInput` reports the effect.
assert_contains "accepted pose, applied: animationInput reports the effect" \
  "$(field "$LAST_ORDER" animationInput)" "bones:"
COVERAGE=$(field "$LAST_ORDER" boneTableCoverage)
assert_gt "accepted pose, applied: coverage numerator" "${COVERAGE%%/*}" 0
assert_eq "accepted pose, applied: the table covers the whole skeleton" \
  "${COVERAGE%%/*}" "${COVERAGE##*/}"
assert_eq "accepted pose, applied: every accepted pose was applied" \
  "$(field "$LAST_SUMMARY" applied)" "$(field "$LAST_SUMMARY" ok)"
# And the picture moves. Without this the flag would be a string, and "the pose
# was applied" would rest on a counter the caller wrote itself.
APPLY_DIFF=$(byte_diff "$TRACE_DIR/ok-bind.bmp" "$TRACE_DIR/ok-apply.bmp")
assert_gt "the applied pose changes the frame" "$APPLY_DIFF" 0
echo "  ..   ok-bind vs ok-apply: $APPLY_DIFF byte positions differ"

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
# caught only when the three named readings moved AND both controls held, so a
# mutation that collapsed the run -- making everything red for the wrong reason --
# cannot be counted as a detection.
echo
if [ "$MUTATION" -eq 1 ]; then
  mut_red=0
  mut_hold=0
  # The reason is several words, so the whole line is matched rather than a field.
  mut_labelled=0
  case "${MUT_POV_LINE:-}" in
    *"past the drawn model's 7 sequences"*) mut_labelled=1 ;;
  esac
  printf 'mutation readings:\n'
  printf '  refusal vmAnimInput  = %s (fixed build: sequenceOutOfRange)\n' "${MUT_POV_VMANIM:-<none>}"
  printf '  refusal reason names the drawn table = %s (fixed build: 1)\n' "$mut_labelled"
  printf '  refusal outOfRange   = %s of %s observations (fixed build: all of them)\n' \
    "${MUT_POV_OOR:-<none>}" "${MUT_POV_OBS:-<none>}"
  printf '  control poseAvailable = %s (must stay 0: still a refusal, only unlabelled)\n' "${MUT_POV_POSE:-<none>}"
  printf '  control matcha entity = %s (must stay -1)\n' "${MUT_MATCHA_ENTITY:-<none>}"
  printf '  control index space   = %s (must stay mismatch)\n' "${MUT_INDEXSPACE:-<none>}"
  [ "${MUT_POV_VMANIM:-}" != "sequenceOutOfRange" ] && mut_red=$((mut_red + 1))
  [ "$mut_labelled" -eq 0 ] && mut_red=$((mut_red + 1))
  if [ -n "${MUT_POV_OBS:-}" ] && [ "${MUT_POV_OOR:-}" != "$MUT_POV_OBS" ]; then
    mut_red=$((mut_red + 1))
  fi
  [ "${MUT_POV_POSE:-x}" = "0" ] && mut_hold=$((mut_hold + 1))
  [ "${MUT_MATCHA_ENTITY:-}" = "-1" ] && mut_hold=$((mut_hold + 1))
  [ "${MUT_INDEXSPACE:-}" = "mismatch" ] && mut_hold=$((mut_hold + 1))
  echo "mutation_red=$mut_red/3 hold=$mut_hold/3"
  if [ "$mut_red" -eq 3 ] && [ "$mut_hold" -eq 3 ]; then
    echo "MUTATION-CAUGHT=PASS (the range branch is what names the refusal; every control held)"
    exit 0
  fi
  echo "MUTATION-CAUGHT=FAIL (red=$mut_red/3 hold=$mut_hold/3)"
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
