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
#              three EXPLICIT ticks inside the window the history run covers. This
#              recording holds one sequence for its whole window, so the ticks
#              cannot be told apart by the sequence they carry: they are told apart
#              by being three different, increasing points of the window, and each
#              witness is checked to have been asked for its own tick and to have
#              reached it. Three witness frames, pairwise byte-different. Whether
#              the reader FOLLOWS a sequence change is not askable here; that claim
#              is covered on the matcha recording, see
#              viewmodel-animation-check.sh:574-577.
#   Section 3  the accepted pose, CONSTRUCTED because this recording cannot reach
#              it: the recording carries one sequence (28), and the model the entity
#              names can pose sequence 1 and nothing else. The sequence to use is
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
# W1 (2026-11-23): every product launch in this script runs at 1280x720, the size
# its pixel pins were measured at. The product reads this when --window-size is
# absent (an explicit --window-size still wins); before W1 1280x720 was simply the
# default, and W1 moves the default to 1920x1080. tools/window-pin-check.sh checks it.
export TF2_NATIVE_WINDOW_SIZE="${TF2_NATIVE_WINDOW_SIZE:-1280x720}"
cd "$(dirname "$0")"

# ---------------------------------------------------------------------------
# Arguments
# ---------------------------------------------------------------------------
MUTATION=0
SELFTEST_RETRY=0
while [ $# -gt 0 ]; do
  case "$1" in
    --mutation) MUTATION=1 ;;
    --selftest-retry) SELFTEST_RETRY=1 ;;
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

# The trace path is handed to a native Windows binary, which does not translate
# MSYS paths: "/d/..." is opened literally and the trace silently never opens,
# which reads exactly like "the product produced no animation". Convert once.
# 2026-11-22: this must run before TRACE_PRODUCT_DIR is derived below. It used to
# sit ~40 lines later, so under `set -u` the relative-path arm (the one
# verify-all.sh step 24 takes) died with "ROOT: unbound variable" before any
# assertion ran.
ROOT=$(cygpath -m "$(pwd)" 2>/dev/null || pwd)
case "$ROOT" in
  /[a-zA-Z]/*) echo "FATAL: could not convert '$ROOT' to a Windows path"; exit 2 ;;
esac

TRACE_DIR="${VIEWMODEL_COMPANION_TRACE_DIR:-evidence/fast/viewmodel-companion}"
# The product opens TF2_NATIVE_TRACE with _wfopen_s on the literal environment
# value (native/src/main_trace.cpp:79-86): an MSYS /d/... path is read by Win32 as
# "\d\..." relative to the current drive and the open silently fails, so the trace
# would never appear while the capture still lands. Hand it a Windows path.
case "$TRACE_DIR" in
  [A-Za-z]:[\\/]*) TRACE_PRODUCT_DIR="$TRACE_DIR" ;;
  /*) TRACE_PRODUCT_DIR=$(cygpath -m "$TRACE_DIR") ;;
  *) TRACE_PRODUCT_DIR="$ROOT/$TRACE_DIR" ;;
esac
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
# trace_distinct_sequences <trace> -> how many distinct sequence= values the
# vmAnim entity lines carry. Counted from the trace itself so a copy of a trace can
# be forged in a control and has to move the number.
trace_distinct_sequences() {
  awk '/scene: vmAnim entity/ {
         for (i = 1; i <= NF; ++i) if ($i ~ /^sequence=/) seen[substr($i, 10)] = 1
       }
       END { print length(seen) }' "$1"
}
# trace_sequence_range <trace> -> "min..max" of the same values, "" if there is none.
trace_sequence_range() {
  awk '/scene: vmAnim entity/ {
         for (i = 1; i <= NF; ++i) if ($i ~ /^sequence=/) {
           v = substr($i, 10) + 0
           if (n == 0 || v < lo) lo = v
           if (n == 0 || v > hi) hi = v
           n++
         }
       }
       END { if (n == 0) print ""; else print lo ".." hi }' "$1"
}
# trace_first_tick / trace_last_tick <trace> -> the window the run covers, read off
# the scene: begin lines rather than assumed from --capture-tick.
trace_first_tick() {
  awk '/scene: begin/ { for (i = 1; i <= NF; ++i) if ($i ~ /^tick=/) { v = $i; sub("tick=", "", v); print v; exit } }' "$1"
}
trace_last_tick() {
  awk '/scene: begin/ { for (i = 1; i <= NF; ++i) if ($i ~ /^tick=/) { v = $i; sub("tick=", "", v); last = v } }
       END { print last }' "$1"
}
# witness_ticks_ok <start> <end> <t0> <t1> <t2> -> ok | not-numeric |
# outside-window | not-increasing. The three witness ticks are explicit, so the
# only thing that keeps them honest is that the selector can refuse: a tick
# outside the window and a repeated tick are the two mistakes it exists to catch
# (both are exercised as controls where the history window is known).
witness_ticks_ok() {
  case "$1$2$3$4$5" in ''|*[!0-9]*) echo "not-numeric"; return ;; esac
  if [ "$3" -lt "$1" ] || [ "$5" -gt "$2" ]; then echo "outside-window"; return; fi
  if [ "$3" -ge "$4" ] || [ "$4" -ge "$5" ]; then echo "not-increasing"; return; fi
  echo ok
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
    # N01 plan A: the archive root names the directory, not a tree, because the
    # replacement POV lives under a per-map folder there.
    archive-pov) path="E:/demo测试/pl_upward_f12/POV/$name" ;;
    *) echo "  FAIL unknown root '$root' for $name"; fail=1; continue ;;
  esac
  if [ ! -f "$path" ]; then
    echo "  FAIL pinned recording is absent: $path"; fail=1; continue
  fi
  actual_size=$(stat -c%s "$path")
  actual_sha=$(sha256sum "$path" </dev/null | cut -d' ' -f1)
  if [ "$actual_size" != "$size" ]; then
    echo "  FAIL pinned recording $name is $actual_size bytes, pinned $size"; fail=1; continue
  fi
  if [ "$actual_sha" != "$sha256" ]; then
    echo "  FAIL pinned recording $name has hash ${actual_sha:0:16}..., pinned ${sha256:0:16}..."; fail=1; continue
  fi
  echo "  OK   $name pinned ($size bytes, ${sha256:0:16}...)"
  PINNED=$((PINNED + 1))
  case "$name" in
    autorecord_2026-08-08_21-43-59.dem) DEMO_POV="$path" ;;
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
# T6 grew the battery to 96 checks (the adoption fixtures) and one more
# fixture in the report (companion-motion).
assert_eq "selftest assertions that held" "$SELFTEST_OK" "96"
assert_eq "selftest checks that ran" "$SELFTEST_TOTAL" "96"
assert_eq "companion fixtures recorded" "$(field "$SELFTEST_SUMMARY" fixtures)" "20"
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
# T6: the binding now adopts the declared c_soldier_animations.mdl, so this
# line reports the ADOPTED table -- 109 animations, 53 of the model's 54
# sequences bound by name, 52 of them multi-frame, the longest 89 frames. The
# base file's own stub numbers (1 animation, 54 sequences, 53 without a blend
# block) are still asserted below from the raw words of the drawn file.
assert_eq "companion animation descriptors" "$(field "$COMPANION_LINE" animations)" "109"
assert_eq "companion sequences that reach a descriptor" "$(field "$COMPANION_LINE" animated)" "53"
assert_eq "companion sequences with more than one frame" "$(field "$COMPANION_LINE" multiFrame)" "52"
assert_eq "companion widest binding" "$(field "$COMPANION_LINE" maxFrames)" "89"
# The same reading taken from the file's own words, so the count above can be
# checked without re-running the decoder that produced it.
assert_eq "raw header animation count" "$(field "$RAW_LINE" animCount)" "1"
assert_eq "raw header sequence count" "$(field "$RAW_LINE" seqCount)" "54"
assert_eq "raw sequence descriptors with no blend block" "$(field "$RAW_BLEND" word56NonPositive)" "53"
assert_eq "control model sequences" "$(field "$CONTROL_LINE" sequences)" "7"
assert_eq "control model multi-frame sequences" "$(field "$CONTROL_LINE" multiFrame)" "6"
assert_eq "control widest binding" "$(field "$CONTROL_LINE" maxFrames)" "57"
assert_ge "the cycle witness has a live sequence" "$(field "$CONTROL_LINE" witness)" "0"

# A BUSY PRODUCT IMAGE IS A HOST STATE, NOT A READING (T49).
#   Windows keeps an executable's image open until the last handle closes, so a
#   launch that starts a few seconds after the previous product process exited can
#   fail with rc=126 "Permission denied" before the program runs at all. Measured
#   on the T49 chain (2026-10-22 01:30): step 24's forward gate ended its last
#   launch at 01:30:25 and the mutation gate's first launch at 01:30:29 hit
#   exactly that; every launch before the mutation half's first rebuild failed, and
#   86 assertions went red for a reason that had nothing to do with the proof. The
#   same gate on the same tree with an idle image read 6/6 PASS and zero rc=126,
#   which is what identifies contention rather than a defect.
#
#   Two things are deliberately NOT used as the readiness signal, both measured on
#   this host on 2026-10-22:
#     * `cp "$EXE" /dev/null` succeeds even while the product is running, so a
#       read test says nothing about the loader's share mode;
#     * `"$EXE" --version` also succeeds while another instance runs, so an
#       exec probe does not reproduce the race either.
#   The only signal that matches the failure is the one in the failing log:
#   timeout(1) could not run the file. So the wait watches the process table (which
#   is what the contention comes from) and the retry is driven by the launcher's
#   own error text -- never by the exit code alone, because 126/127 are also what
#   timeout(1) returns for a launch it refused, and a product that exited 126 on
#   its own would not have written that line.
#
#   Everything that is not that shape -- a real product failure, a wrong exit code,
#   a crash -- still fails on the first attempt. At most one retry is allowed, so a
#   permanently unavailable image is reported as the failure it is.
EXE_WAIT_TIMEOUT=${VIEWMODEL_COMPANION_EXE_WAIT:-30}   # seconds
EXE_WAIT_INTERVAL=0.5
launch_denied() { # launch_denied <log> -> 0 when the launcher could not exec the exe
  grep -aqE "failed to run command .*: (Permission denied|No such file or directory)$" "$1" 2>/dev/null
}
product_processes() { # product_processes -> number of running product images
  # The COMMAND column only, not the whole line. `ps -W | grep tf2_demo_native`
  # also matches any process whose ARGUMENTS mention the binary -- including this
  # very script's own launcher line and, as it turned out, the stub the retry
  # self-test runs, which contains the denial message verbatim. That false positive
  # made the wait believe a product was alive when none was, and it is why the
  # self-test's "reported a retry" case read 0 on an otherwise healthy host.
  ps -W 2>/dev/null | awk '{print $NF}' | grep -ci 'tf2_demo_native' || true
}
wait_for_exe() { # wait_for_exe -> 0 when no product image is running any more
  local deadline count
  deadline=$(( $(date +%s) + EXE_WAIT_TIMEOUT ))
  while :; do
    count=$(product_processes)
    if [ "${count:-0}" -eq 0 ]; then return 0; fi
    if [ "$(date +%s)" -ge "$deadline" ]; then
      echo "  note: $count product process(es) still running after ${EXE_WAIT_TIMEOUT}s; running anyway"
      return 1
    fi
    sleep "$EXE_WAIT_INTERVAL"
  done
}

# ONE LAUNCH, WITH THE RETRY RULE (T49.1). The single-attempt launch that every
# reading above goes through lives here rather than inline in run_product, because
# a retry rule that is only reachable from a 90-minute chain run is a rule nobody
# has ever executed: the T49 review noted exactly that the busy-image retry was
# never itself tested. --selftest-retry drives THIS function with a stub command,
# so the code under test is the same code the chain runs.
#
# attempt_launch <label> <cmd...> -> sets LAUNCH_RC and LAST_LAUNCH_RETRIED
#   retries once, and only when the command's own log says the launcher could not
#   exec the file. The command's stdout+stderr must go to `<label>.log`; the
#   caller passes the argv, this function decides whether a second attempt happens.
LAUNCH_DIR=""
attempt_launch() { # attempt_launch <label> <cmd...>
  local label="$1"; shift
  local log="${LAUNCH_DIR:-$TRACE_DIR}/$label.log"
  local attempt waited=0
  LAST_LAUNCH_RETRIED=0
  for attempt in 1 2; do
    "$@" > "$log" 2>&1
    LAUNCH_RC=$?
    if launch_denied "$log" && [ "$attempt" -eq 1 ]; then
      wait_for_exe && waited=1
      continue
    fi
    break
  done
  LAST_LAUNCH_RETRIED=$waited
  return "$LAUNCH_RC"
}

# ---------------------------------------------------------------------------
# Section 1b -- the retry rule, driven with stubs (--selftest-retry)
# ---------------------------------------------------------------------------
# The busy-image retry added in T49 was, on review, never itself executed by
# anything: the only path to it is a chain run that happens to collide with a
# product image that is still being released, and a rule that can only fail in
# production is not a tested rule. This section drives attempt_launch -- the SAME
# function run_product uses -- with small shell stubs, so what is under test is
# the code the chain runs, not a copy of it.
#
# What is asserted, and why each one has to be here:
#   deny-then-ok    first attempt writes the launcher's Permission-denied line and
#                   exits 126; second succeeds. Must end rc=0 with exactly TWO
#                   attempts. Catches a retry that does not actually retry.
#   always-deny     every attempt is denied. Must end rc=126 with exactly TWO
#                   attempts -- one retry, never a loop -- and must take no longer
#                   than the documented bound. Catches an unbounded retry.
#   real-failure    the command fails with rc=3 and no launcher line. Must end
#                   rc=3 with exactly ONE attempt. Catches a retry that fires on
#                   the exit code instead of on the launcher's own message.
#   timeout         the wait itself is forced to expire (VIEWMODEL_COMPANION_EXE_WAIT=1
#                   with a stub that reports a live product process). The retry
#                   still happens (the rule is "wait, then try once more"), and
#                   wait_for_exe must return 1 and print its note. Catches a wait
#                   that silently returns success when it timed out.
# It runs on EVERY invocation, not behind a flag: a test that only runs when
# somebody remembers to pass --selftest-retry is not much better than the untested
# retry it replaces. --selftest-retry exists only to run this section ALONE
# (skipping the product launches), which is how it is developed and reviewed.
retry_selftest() {
  echo
  echo "=== 1b/6 the retry rule, driven with stubs ==="
  STUB_DIR=".scratch/companion-retry-selftest"
  rm -rf "$STUB_DIR"; mkdir -p "$STUB_DIR/traces"
  LAUNCH_DIR="$STUB_DIR/traces"
  RETRY_TIMEOUT_SAVED="$EXE_WAIT_TIMEOUT"
  # HERMETIC: the retry cases must not depend on whether the host happens to have a
  # product running. The first version ran against the real process table, so a
  # companion gate running elsewhere on the host made "deny-then-ok" read
  # reported-a-retry=0 (the wait timed out on a real product instead of settling),
  # which is a red about the environment and not about the rule. The process count
  # is stubbed to zero for these three cases and restored before the timeout case,
  # which needs the opposite.
  saved_processes="$(declare -f product_processes)"
  product_processes() { echo 0; }
  # A stub that writes the launcher's denial line on its first N successful
  # touches. It counts attempts in a file, so "exactly two attempts" is a fact
  # about the filesystem and not about the log being overwritten.
  stub_deny() { # stub_deny <counter> <denials>
    local counter="$1" denials="$2"
    # Split on purpose: this host's bash (5.3.20 msys) leaves $(( )) unexpanded when a
    # command substitution with a quoted word is nested in it (tools/check-nested-arith.sh).
    # The counter file can also hold something that is not a count, so it is guarded.
    local n raw
    raw=$(cat "$counter" 2>/dev/null || echo 0)
    case "$raw" in
      ''|*[!0-9]*) raw=0 ;;
    esac
    n=$(( raw + 1 )); echo "$n" > "$counter"
    if [ "$n" -le "$denials" ]; then
      printf "timeout: failed to run command 'tf2_demo_native.exe': Permission denied\n"
      return 126
    fi
    echo "stub product ran"
    return 0
  }
  stub_fail() { # a real product failure: no launcher line, own exit code
    printf "scene: anim ...\nreal product failure\n"
    return 3
  }

  # --- denial once, then success ---
  DENY_COUNTER="$STUB_DIR/denythenok.count"; : > "$DENY_COUNTER"
  attempt_launch deny-then-ok bash -c "$(declare -f stub_deny); stub_deny '$DENY_COUNTER' 1"
  DENY_ATTEMPTS=$(cat "$DENY_COUNTER" 2>/dev/null || echo 0)
  assert_eq "deny-then-ok: outcome" "$LAUNCH_RC" "0"
  assert_eq "deny-then-ok: attempts" "$DENY_ATTEMPTS" "2"
  assert_eq "deny-then-ok: reported a retry" "$LAST_LAUNCH_RETRIED" "1"

  # --- denied every time: exactly one retry, then the failure is reported ---
  ALWAYS_COUNTER="$STUB_DIR/always.count"; : > "$ALWAYS_COUNTER"
  started=$(date +%s)
  attempt_launch always-deny bash -c "$(declare -f stub_deny); stub_deny '$ALWAYS_COUNTER' 99"
  ended=$(date +%s)
  ALWAYS_ATTEMPTS=$(cat "$ALWAYS_COUNTER" 2>/dev/null || echo 0)
  assert_eq "always-deny: outcome is the launcher's rc" "$LAUNCH_RC" "126"
  assert_eq "always-deny: attempts (one retry, not a loop)" "$ALWAYS_ATTEMPTS" "2"
  if [ $(( ended - started )) -le $(( EXE_WAIT_TIMEOUT + 10 )) ]; then
    echo "  OK   always-deny: bounded by the wait ($(( ended - started ))s <= ${EXE_WAIT_TIMEOUT}+10s)"
  else
    echo "  FAIL always-deny: took $(( ended - started ))s, longer than the bound"
    fail=1; FAIL_LINES+=("always-deny: unbounded retry")
  fi

  # --- a real failure must not be retried ---
  FAIL_COUNTER="$STUB_DIR/realfail.count"; : > "$FAIL_COUNTER"
  attempt_launch real-failure bash -c "$(declare -f stub_fail); echo x > '$FAIL_COUNTER'; stub_fail"
  FAIL_ATTEMPTS=$(wc -l < "$FAIL_COUNTER" 2>/dev/null | tr -d ' ')
  assert_eq "real-failure: outcome" "$LAUNCH_RC" "3"
  assert_eq "real-failure: attempts (no retry on a real failure)" "${FAIL_ATTEMPTS:-0}" "1"
  assert_eq "real-failure: reported no retry" "$LAST_LAUNCH_RETRIED" "0"

  # --- the wait bound itself ---
  # The first version of this case asserted "wait_for_exe fails when asked to time
  # out", and it failed on review for a good reason: with no product running the
  # wait CORRECTLY returns success immediately, so the case was asserting the
  # opposite of the intended behaviour and would have gone red on a healthy host.
  # The bound is a property of the loop, so the loop's only input -- the process
  # count -- is what the stub replaces. It replaces it unconditionally rather than
  # only when the host happens to be idle, so this case runs identically during a
  # chain and on an idle machine.
  product_processes() { echo 1; }        # a consumer that never goes away
  started=$(date +%s)
  if EXE_WAIT_TIMEOUT=1 EXE_WAIT_INTERVAL=0.2 wait_for_exe; then
    echo "  FAIL timeout: the wait reported success while the process count was 1"
    fail=1; FAIL_LINES+=("timeout: wait returned success under contention")
  else
    echo "  OK   timeout: the wait gave up after its bound and named the condition"
  fi
  ended=$(date +%s)
  if [ $(( ended - started )) -ge 1 ]; then
    echo "  OK   timeout: it actually waited ($(( ended - started ))s >= 1s)"
  else
    echo "  FAIL timeout: it returned before the bound elapsed ($(( ended - started ))s)"
    fail=1; FAIL_LINES+=("timeout: returned before its bound")
  fi
  # Restore both the process count and the wait parameters the case borrowed.
  eval "$saved_processes"
  EXE_WAIT_TIMEOUT="$RETRY_TIMEOUT_SAVED"
  EXE_WAIT_INTERVAL=0.5
  echo "  ..   retry self-test complete"
}

# --selftest-retry runs ONLY this section: no product launch, no capture, so it
# is usable while another task holds the host lock. Every other invocation runs
# it as well (right here, before section 2), because a guard that is only
# exercised on request is the same untested guard this section was added for.
if [ "$SELFTEST_RETRY" -eq 1 ]; then
  retry_selftest
  if [ "$fail" -eq 0 ]; then echo "RETRY-SELFTEST=PASS"; exit 0; fi
  echo "RETRY-SELFTEST=FAIL"; exit 1
fi
retry_selftest

# ---------------------------------------------------------------------------
# Section 2 -- the proof, on the entity's own model, at three derived ticks
# ---------------------------------------------------------------------------
echo
echo "=== 2/6 the pass draws the entity's own model ==="
run_product() { # run_product <name> <demo> <tick> [extra...]
  local name="$1" demo="$2" tick="$3"; shift 3
  local bmp="$TRACE_DIR/$name.bmp"
  rm -f "$bmp" "$TRACE_DIR/$name.csv"
  LAUNCH_DIR="$TRACE_DIR"
  attempt_launch "$name" \
    env TF2_NATIVE_TRACE="$TRACE_PRODUCT_DIR/$name.trace" timeout "$RUN_TIMEOUT" "$EXE" \
      --tf-root "$TF" --audio-device "$AUDIO_DEVICE" --demo "$demo" \
      --settings-file "$TRACE_DIR/$name-settings.ini" \
      --capture-frame "$bmp" --capture-tick "$tick" \
      --metrics-file "$TRACE_DIR/$name.csv" \
      --viewmodel "$VM_MODEL" --viewmodel-fov 90 "$@"
  LAST_RC=$LAUNCH_RC
  [ "$LAST_LAUNCH_RETRIED" -eq 1 ] && echo "  note: $name retried once after the product image left the process table"
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
  # capture: armed is the product's own record of which tick it was asked for and
  # which frame it captured: the witnesses are checked against it below.
  LAST_ARMED=$(grep -a "capture: armed" "$TRACE_DIR/$name.trace" 2>/dev/null \
    | tr -d '\r' | tail -1 | sed 's/^[0-9]\{1,\}\(\.[0-9]*\)\? //')
}

# --- what this recording is, and what a history run can say about it ----------
# The recording this gate reads is the one PINNED in $INPUTS by size and sha256
# and verified in 0/6 above; this comment deliberately does NOT copy the sha -- a
# second copy would be a second statement of the same fact, free to rot. Today it
# resolves to autorecord_2026-08-08_21-43-59.dem, and its characterisation is:
#   - ONE sequence for the whole window: distinct=1, range 28..28, changes=0;
#   - the entity's recording-local index is 998 -- an index inside this
#     recording's entity table, NOT an index into the model's sequence table (the
#     model's own table is pinned in section 3); the model it names is
#     c_sniper_arms.mdl;
#   - the model can pose sequence 1 (section 3 reads that from the model itself).
# The pin this gate carried before (2026-10-12, 9cfc9139) said distinct=3,
# range=1..30 and modelIndex=1032: those were read off
# autorecord_2026-07-02_13-26-46.dem (21,856,667 B, sha c8a58501...), which is no
# longer on disk (docs/GATE-UNBLOCK-2026-11-15.md:9). They are re-pinned here
# because the RECORDING changed, not because the product moved: the proof itself
# (entity model == drawn model, counted over every observation) is asserted below
# and is green under both recordings.
#
# A single sequence means "does the reader FOLLOW a sequence change?" cannot be
# asked on this recording. That claim is covered in viewmodel-animation-check.sh
# :574-577 (pov ViewModel sequence changes >= 2 / pov distinct sequences >= 2, on
# the matcha recording: 627 changes over 41 sequences). The two files name each
# other so neither side can be dropped as "the other one tests it".
run_product history "$DEMO_POV" 2000
assert_eq "history rc" "$LAST_RC" "0"
assert_ge "history observations" "$(field "$LAST_SUMMARY" observations)" 1000
HIST_DISTINCT=$(trace_distinct_sequences "$TRACE_DIR/history.trace")
HIST_RANGE=$(trace_sequence_range "$TRACE_DIR/history.trace")
assert_eq "history distinct sequences" "$HIST_DISTINCT" "1"
assert_eq "history sequence range" "$HIST_RANGE" "28..28"
# Negative control for both pins: a forged second sequence in a COPY of the trace
# must move both readings, or the pins above could never fail. Offscreen, the
# product is untouched.
CONTROL_DIR=.scratch/viewmodel-companion-controls
mkdir -p "$CONTROL_DIR"
awk 'BEGIN { done = 0 }
     /scene: vmAnim entity/ && !done {
       for (i = 1; i <= NF; ++i) if ($i == "sequence=28") { $i = "sequence=29"; done = 1; break }
     }
     { print }' "$TRACE_DIR/history.trace" > "$CONTROL_DIR/history-second-sequence.trace"
assert_eq "control: a forged second sequence is counted" \
  "$(trace_distinct_sequences "$CONTROL_DIR/history-second-sequence.trace")" "2"
assert_eq "control: the forged trace's range moves to 28..29" \
  "$(trace_sequence_range "$CONTROL_DIR/history-second-sequence.trace")" "28..29"

# The three witness ticks are EXPLICIT points inside the window the history run
# covers, not positions derived from sequence changes -- there are no changes on
# this recording to derive them from. What is asserted is the mechanism: the ticks
# lie inside the window, are distinct and increasing, and each witness run is
# asked for its own tick and actually reaches it (checked per witness below).
WSTART=$(trace_first_tick "$TRACE_DIR/history.trace")
WEND=$(trace_last_tick "$TRACE_DIR/history.trace")
case "$WSTART$WEND" in
  ''|*[!0-9]*) assert_eq "the history window ticks are numeric" "$WSTART..$WEND" "numeric"
               WSTART=${WSTART:-0}; WEND=${WEND:-0} ;;
esac
assert_gt "the history window spans more than three ticks" "$(( WEND - WSTART ))" 3
T0=$WSTART
T1=$(( WSTART + (WEND - WSTART) / 3 ))
T2=$(( WSTART + (2 * (WEND - WSTART)) / 3 ))
assert_eq "the three witness ticks are distinct and inside the window" \
  "$(witness_ticks_ok "$WSTART" "$WEND" "$T0" "$T1" "$T2")" "ok"
# Controls: the same selector must refuse the two mistakes the mechanism exists to
# catch -- a tick outside the window and a repeated tick.
assert_eq "control: the tick selection rejects a tick outside the window" \
  "$(witness_ticks_ok "$WSTART" "$WEND" "$T0" "$T1" "$(( WEND + 1 ))")" "outside-window"
assert_eq "control: the tick selection rejects a repeated tick" \
  "$(witness_ticks_ok "$WSTART" "$WEND" "$T0" "$T0" "$T2")" "not-increasing"
echo "  ..   witness ticks (explicit, inside $WSTART..$WEND): $T0 $T1 $T2"

run_product witness-a "$DEMO_POV" "$T0"
assert_eq "witness-a rc" "$LAST_RC" "0"
# The tick mechanism, per witness: the run was asked for T0 and reached it. The
# request is read off the product's own capture: armed line rather than echoed from
# this script, so a launch that ignored --capture-tick would redden here.
WITNESS_A_TICK=$(field "$LAST_ARMED" requested)
assert_eq "witness-a was asked for its own tick" "$WITNESS_A_TICK" "$T0"
assert_ge "witness-a reached the tick it was asked for" "$(field "$LAST_ARMED" frame_tick)" "$T0"
# The proof itself, on the pass's own line: the model whose mesh is in the vertex
# buffer is the model the entity's m_nModelIndex names.
assert_eq "witness-a drawn model" "$(field "$LAST_ORDER" drawnModel)" "$ENTITY_MODEL"
assert_eq "witness-a entity model matches the drawn model" \
  "$(field "$LAST_ORDER" entityModelMatchesDrawn)" "1"
# 998 is this RECORDING's local entity index -- the entity table slot the demo puts
# this entity in -- not an index into c_sniper_arms.mdl's sequence table (that table
# is pinned in section 3). The assertion above says the pass drew the model this
# index names; this one says the index is the recording's. It was 1032 on the lost
# 07-02 recording (see the block above the history run).
assert_eq "witness-a model index is the recording's own entity" "$(field "$LAST_VMANIM" modelIndex)" "998"
assert_eq "witness-a sequence inside the entity's own table" "$(field "$LAST_ORDER" sequenceInRange)" "1"
assert_eq "witness-a model mismatches" "$(field "$LAST_ORDER" modelMismatch)" "0"
assert_eq "witness-a bone mismatches" "$(field "$LAST_ORDER" boneMismatch)" "0"
assert_eq "witness-a the pass drew" "$(field "$LAST_ORDER" drawCalls)" "1"
# 9996, not 600: `952ddf2` taught the mesh builder to decode every LOD-0
# descriptor, and this model has two -- a 600-index sleeve on material slot 0 and
# the 9396-index hands on slot 1. The pin below is the number the pass uploaded;
# `viewmodel-range-check.sh` is the gate that proves the two ranges and their two
# material slots are separate, because a single vertex count cannot say that.
assert_eq "witness-a vertices" "$(field "$LAST_ORDER" vertices)" "9996"
assert_eq "witness-a ranges" "$(field "$LAST_ORDER" ranges)" "2"
assert_eq "witness-a distinct material slots bound" "$(field "$LAST_ORDER" boundSlots)" "2"
# The witnesses are not pinned to the sequence values they sample: this recording
# holds ONE sequence for its whole window (pinned above), so a sequence number
# cannot tell two witnesses apart -- it would be the same number at all three ticks,
# and "the two witnesses sample different sequences" is unaskable here. What the
# proof needs is that the pass reports the entity's OWN index (no override, no
# remap), that each run is asked for its own tick and reaches it, and that the three
# captures are three different points of the demo's cycle (asserted on the frames at
# the end of this section).
WITNESS_A_SEQ=$(field "$LAST_VMANIM" sequence)
assert_eq "witness-a sequence is the entity's own index" \
  "$WITNESS_A_SEQ" "$(field "$LAST_VMANIM" entitySequence)"
assert_eq "witness-a owner is the view entity" "$(field "$LAST_VMANIM" owner)" "$(field "$LAST_VMANIM" viewEntity)"
# (the recording-local model index assertion sits next to entityModelMatchesDrawn
# above, where the two claims can be read together)
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
WITNESS_B_SEQ=$(field "$LAST_VMANIM" sequence)
# The distinguishing fact is the tick, not the sequence (one sequence on this
# recording). The pair is told apart by the request each run carries and by how far
# it actually played; both are readings off this run, not constants.
WITNESS_B_TICK=$(field "$LAST_ARMED" requested)
assert_ne "the first two witnesses were asked for different ticks" "$WITNESS_B_TICK" "$WITNESS_A_TICK"
assert_eq "witness-b was asked for its own tick" "$WITNESS_B_TICK" "$T1"
assert_ge "witness-b reached the tick it was asked for" "$(field "$LAST_ARMED" frame_tick)" "$T1"
assert_gt "witness-b played further into the window than witness-a" \
  "$(trace_last_tick "$TRACE_DIR/witness-b.trace")" "$(trace_last_tick "$TRACE_DIR/witness-a.trace")"
assert_eq "witness-b entity model matches the drawn model" \
  "$(field "$LAST_ORDER" entityModelMatchesDrawn)" "1"
assert_eq "witness-b model mismatches, counted" "$(field "$LAST_SUMMARY" modelMismatch)" "0"
assert_eq "witness-b sequences in range, counted" \
  "$(field "$LAST_SUMMARY" sequenceInRange)" "$(field "$LAST_SUMMARY" observations)"
assert_gt "witness-b observations exceed witness-a's" \
  "$(field "$LAST_SUMMARY" observations)" "$(field "$(grep -a 'scene: vmAnim summary' "$TRACE_DIR/witness-a.trace" | tr -d '\r' | tail -1 | sed 's/^[0-9]\{1,\}\(\.[0-9]*\)\? //')" observations)"
# No "witness-b sequence changes" assertion any more: this recording holds one
# sequence by construction (pinned above), so changes=0 is the characterisation, not
# a defect. The claim that the reader FOLLOWS a change moved to
# viewmodel-animation-check.sh:574-577, which runs it on the matcha recording where
# the cursor actually moves (627 changes over 41 sequences).

run_product witness-c "$DEMO_POV" "$T2" --viewmodel-apply-animation
assert_eq "witness-c rc" "$LAST_RC" "0"
WITNESS_C_SEQ=$(field "$LAST_VMANIM" sequence)
WITNESS_C_TICK=$(field "$LAST_ARMED" requested)
assert_ne "the last two witnesses were asked for different ticks" "$WITNESS_C_TICK" "$WITNESS_B_TICK"
assert_eq "witness-c was asked for its own tick" "$WITNESS_C_TICK" "$T2"
assert_ge "witness-c reached the tick it was asked for" "$(field "$LAST_ARMED" frame_tick)" "$T2"
assert_gt "witness-c played further into the window than witness-b" \
  "$(trace_last_tick "$TRACE_DIR/witness-c.trace")" "$(trace_last_tick "$TRACE_DIR/witness-b.trace")"
assert_eq "witness-c entity model matches the drawn model" \
  "$(field "$LAST_ORDER" entityModelMatchesDrawn)" "1"
assert_eq "witness-c model mismatches, counted" "$(field "$LAST_SUMMARY" modelMismatch)" "0"
# The range is a characterisation of the recording, re-pinned with the block above:
# this recording carries one sequence (28), and section 3 constructs the pose the
# model can actually reach (1) because the recording never carries it. Kept as a
# reading of THIS run so the "not this recording" statement in section 3 cannot
# drift away from it.
WITNESS_RANGE=$(field "$LAST_SUMMARY" range)
WITNESS_NATURAL_OK=$(field "$LAST_SUMMARY" ok)
assert_eq "witness-c sequence range over the run" "$WITNESS_RANGE" "28..28"
# T6 moved this pair. With the declared animations companion adopted, every
# natural observation binds BY NAME and is accepted; the pre-T6 readings (0
# accepted, every one refused for the same reason) are what the adoption removed,
# and the stub keeps its own refusal in section 4.
assert_eq "every natural run observation was accepted" \
  "$WITNESS_NATURAL_OK" "$(field "$LAST_SUMMARY" observations)"
assert_eq "and none was refused" "$(field "$LAST_SUMMARY" missing)" "0"
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
# This recording carries ONE sequence (28) for its whole window, and the model the
# entity names can pose sequence 1 and nothing else -- so the acceptance branch is
# unreachable
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
# 38 of 43, by name through the declared companion; the five unbound are `ref`
# and the four `rifolver_*` overlays (same figure the animation gate pins).
assert_eq "entity model sequences that reach a descriptor" "$(field "$MODELSEQ_LINE" animated)" "38"
POSE_SEQUENCE=$(field "$MODELSEQ_LINE" firstAnimated)
# Asserted rather than used: the number this run is built around must be a
# reading of the model, and if the model ever gets a posable sequence earlier
# than the one named here the assertion says so before the pose run reads `ok`
# for a reason nobody chose.
assert_eq "the first sequence the entity's own model can pose, by name" "$POSE_SEQUENCE" "1"
# Split (2026-11-16): the model's own table says 1; the recording carries 28. They
# are readings of two different objects, so they get two assertions instead of one
# comparison that would quietly assume the recording walks over the model's first
# posable sequence.
RECORDING_SEQUENCE=${HIST_RANGE%%..*}
assert_eq "the recording's carried sequence" "$RECORDING_SEQUENCE" "28"
assert_eq "and the natural run carries the recording's sequence" "${WITNESS_RANGE%%..*}" "$RECORDING_SEQUENCE"

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
# The one-frame reference pose renders identical to the bind pose -- measured,
# and a reading about that pose rather than about the flag -- so movement is
# measured on the real motion pair instead: the model's own 61-frame sequence 30,
# forced as an override, against the same sequence applied. (The recording itself
# carries sequence 28 and stays unapplied at :886 below, so the override is named on
# the command line and reported as one.)
POSE_DIFF=$(byte_diff "$TRACE_DIR/pose-accepted.bmp" "$TRACE_DIR/pose-applied.bmp")
echo "  ..   pose-accepted vs pose-applied: $POSE_DIFF byte positions differ (the reference pose equals the bind pose)"
run_product pose-motion "$DEMO_POV" 2000 --viewmodel-sequence 30 --viewmodel-apply-animation
assert_eq "pose-motion rc" "$LAST_RC" "0"
assert_eq "pose-motion applied" "$(field "$LAST_ORDER" vmAnimApplied)" "1"
assert_eq "pose-motion skinned" "$(field "$LAST_ORDER" skinning)" "1"
assert_eq "pose-motion frame count" "$(field "$LAST_VMANIM" frame | cut -d/ -f2)" "61"
MOTION_DIFF=$(byte_diff "$TRACE_DIR/history.bmp" "$TRACE_DIR/pose-motion.bmp")
assert_gt "the applied real animation changes the frame" "$MOTION_DIFF" 0
echo "  ..   history vs pose-motion: $MOTION_DIFF byte positions differ"

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
  assert_eq "$name: the battery still ran every check" "$SELFTEST_TOTAL" "96"
  end_mutation "$name" viewmodel_animation_probe
}

# 2026-11-22 (docs/GATE-FIXES-2026-11-22.md): mutations below use an always-false
# condition of the form `!(E) && (E)` instead of `false && E`. Since N23
# (2026-11-18) tf2_core and tf2_demo_native compile with /W4 /WX, and MSVC reports
# `false && E` as C4127 -> C2220, so the mutated build failed and the case proved
# nothing. `!(E) && (E)` is false at run time but not a constant expression.
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
    '  if (entityModelMatches && !entityModelMatches) {   // MUTATION: the proof never fires' \
    'a drawn model that is not the entity'\''s own resolves to modelMismatch' \
    'one past the entity'\''s own table resolves to sequenceOutOfRange'
  mutation_case range-off native/src/viewmodel.cpp \
    '  if (static_cast<std::size_t>(input.sequence) >= drawnSequences) {' \
    '  if (!(static_cast<std::size_t>(input.sequence) >= drawnSequences) && static_cast<std::size_t>(input.sequence) >= drawnSequences) {   // MUTATION' \
    'one past the entity'\''s own table resolves to sequenceOutOfRange' \
    'a drawn model that is not the entity'\''s own resolves to modelMismatch'
  mutation_case bone-count-off native/src/viewmodel.cpp \
    '  if (sampled.skinMatrices.size() != drawn.bones.size()) {' \
    '  if (!(sampled.skinMatrices.size() != drawn.bones.size()) && sampled.skinMatrices.size() != drawn.bones.size()) {   // MUTATION' \
    'a skeleton one bone longer resolves to boneMismatch' \
    'the accepted pose covers the drawn skeleton exactly'
  mutation_case model-index-zero-off native/src/viewmodel.cpp \
    '  if (input.modelIndex == 0) {' \
    '  if (!(input.modelIndex == 0) && input.modelIndex == 0) {   // MUTATION: zero is not a model' \
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
        # off -- so `vertices` still reads the BUFFER's 9996 and not a drawn count.
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
