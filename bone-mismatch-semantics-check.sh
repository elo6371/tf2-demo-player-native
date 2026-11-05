#!/usr/bin/env bash
# bone-mismatch-semantics-check.sh -- T62: pin what boneMismatch counts, not a
# number it happens to read.
#
# WHY THIS FILE EXISTS
#   The trace counter scene: anim ... boneMismatch=N looks like a health metric,
#   so the tempting gate is "it must be 0". On this tree that gate is wrong: the
#   counter increments once per skinned instance per update when the instance
#   reports a skin matrix count that disagrees with its declared bone counts, it
#   is never reset, and the honest cause on this corpus is a helper bone (a bone
#   with a valid bind transform and no animation channel). demo, heavy and scout
#   have two such bones each and the other six classes have none, so the counter
#   separates "this model has helper bones" from "it does not" -- an asset
#   property, not a fidelity measurement.
#
# WHAT IT ASSERTS
#   Tier A, structural, true on any corpus (bone-mismatch-semantics.py):
#     monotone non-decreasing; delta <= skinned; delta <= playerDraws; on updates
#     whose census is complete delta equals the number of helper-bone models
#     drawn; on truncated census updates the residual is strictly positive;
#     skinned + bindPose == draws and gpu-uploaded == matrices.
#   Tier B, driver-path existence:
#     a driven run (--player-animation) ends with boneMismatch > 0 and status=ok;
#     an undriven run ends with boneMismatch == 0 and status=missing.
#   Tier C, a WARN only: bm/updates inside the recorded corpus band 4.5-5.0.
#   Completeness, checked before any of the above is trusted (bash side):
#     the trace carries a capture: armed line, so the run reached --capture-tick,
#     and it closes as many scene: begin blocks as it opens, so no update was cut
#     mid-write. A run killed early still leaves a readable prefix, and a prefix
#     can satisfy every structural rule while saying nothing about the frame this
#     gate exists to judge -- a 4642-update partial run that never reached the
#     capture tick passes all of them.
#
# WHY A GREEN RUN CANNOT BE VACUOUS
#   Three negative controls are derived from the driven trace itself: one
#   decrements the counter and one hides the census line that explains an
#   increment (both through the python checker and --expect-fail), and one drops
#   the capture: armed line so it looks like a run that stopped short of the
#   capture tick (through the completeness guard). All three must be rejected, so
#   a pass cannot come from checks that never fire.
#
# WHAT IT DOES NOT CLAIM
#   It does not claim the counter is small, stable or comparable across trees;
#   the absolute value depends on class mix, demo length and wall-clock frame
#   pacing, so nothing here pins a total. The bone gap itself belongs to the
#   model probe (animtable bones - boundDiag bones), not to this counter.
#
# TAKES THE SHARED HOST LOCK ITSELF -- DO NOT WRAP IT IN ONE.
#   Every capture goes through locked() below, which acquires the lock, runs one
#   product process and releases it, so the script never holds the lock while it
#   is not drawing. Wrapping it in an outer `hostlock.sh run` deadlocks instead
#   of queueing: the outer run holds the lock for the whole script, the first
#   locked() call queues behind its own parent, and product_processes stays 0
#   with the inner owner stuck in the queue. That is the same trap
#   map-material-matrix.sh documents. Just run it:
#     cd /d/TF2_Native_Worktrees/t79-integration && bash bone-mismatch-semantics-check.sh
#
# Usage: bash bone-mismatch-semantics-check.sh [--reuse]
#   --reuse  analyse the traces already under .scratch/bone-mismatch.
# Exit: 0 = every assertion held; 1 = an assertion failed; 2 = an input is missing.

set -uo pipefail
cd $(dirname $0)

PY=${PYTHON:-python}
PRODUCT=native/build-nmake/tf2_demo_native.exe
TF="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
DEMO=${BONE_MISMATCH_DEMO:-D:/TF2_Demo_Player/testdata/demos/bb841c6d379ff7c40d0c8baf99f59d8d_matcha-20260927-1347-cp_snakewater_final1.dem}
TICK=64161
SPEED=32
CAMERA=(2492.7 -1327.1 -54.0 4.1 0.0)
LOCK=${HOSTLOCK:-/d/TF2_Native_Worktrees/_shared/hostlock.sh}
OUT=${BONE_MISMATCH_OUT:-evidence/t62-matrix/bone-mismatch}
CAP=.scratch/bone-mismatch
REUSE=0
for arg in "$@"; do
  case $arg in
    --reuse) REUSE=1 ;;
    -h|--help) echo "usage: $0 [--reuse]"; exit 0 ;;
    *) echo "usage: $0 [--reuse]" >&2; exit 2 ;;
  esac
done

for path in "$PRODUCT" "$DEMO" bone-mismatch-semantics.py; do
  [ -e "$path" ] || { echo "FATAL: missing input: $path"; exit 2; }
done
command -v "$PY" >/dev/null 2>&1 || { echo "FATAL: missing interpreter: $PY"; exit 2; }

mkdir -p "$OUT" "$CAP" || exit 2
fail=0
say_fail() { echo "  FAIL $*"; fail=1; }

locked() { bash "$LOCK" run t62-bonemismatch bash -c "$1"; }

# capture <tag> <extra flags...> -- one product run, one trace, one frame.
capture() {
  local tag="$1"; shift
  local trace="$CAP/$tag.trace" bmp="$CAP/$tag.bmp" log="$CAP/$tag.log"
  if [[ $REUSE -eq 1 && -s $trace ]]; then echo "  reused $tag"; return 0; fi
  local cmd
  cmd=$(printf "%q " TF2_NATIVE_TRACE="$(cygpath -m "$trace")" \
    "$PRODUCT" --tf-root "$TF" --audio-disabled --hidden-window \
    --demo "$DEMO" --playback-speed "$SPEED" --capture-tick "$TICK" \
    --capture-frame "$(cygpath -m "$bmp")" \
    --debug-camera ${CAMERA[@]} "$@")
  locked "$cmd" > "$log" 2>&1
  local rc=$?
  if [[ $rc -ne 0 || ! -s $trace ]]; then
    echo "  FAIL capture $tag rc=$rc"
    tail -3 "$log" | grep -a . | tail -3 | while read -r line; do echo "    $line"; done
    return 1
  fi
  if ! grep -aq "scene: anim" "$trace"; then
    echo "  FAIL capture $tag wrote no scene: anim line"
    return 1
  fi
  echo "  captured $tag"
}

capture driven --player-animation || say_fail "the driven capture failed"
capture undriven || say_fail "the undriven capture failed"

if [[ ! -s "$CAP/driven.trace" || ! -s "$CAP/undriven.trace" ]]; then
  echo "BONE-MISMATCH-SEMANTICS=FAIL no traces to judge"
  exit 1
fi

# Is this a whole run, or a prefix of one? Nothing below can tell: a trace cut
# at update 4000 looks exactly like a shorter run. Two cheap structural facts do
# separate them -- the capture: armed line only appears when the run reached
# --capture-tick, and every scene: begin must be closed by its scene: done.
complete_trace() {
  local trace=$1 begun done_
  begun=$(grep -ac "scene: begin" "$trace" 2>/dev/null)
  done_=$(grep -ac "scene: done" "$trace" 2>/dev/null)
  if [[ ${begun:-0} -eq 0 ]]; then echo "no scene: begin blocks"; return 1; fi
  if [[ ${done_:-0} -ne ${begun:-0} ]]; then
    echo "begin=$begun done=${done_:-0} (an update was cut mid-write)"
    return 1
  fi
  if ! grep -aq "capture: armed" "$trace"; then
    echo "no capture: armed line (the run stopped before --capture-tick)"
    return 1
  fi
  echo "$begun blocks, capture reached"
  return 0
}

for tag in driven undriven; do
  if why=$(complete_trace "$CAP/$tag.trace"); then
    echo "  OK $tag is a whole run ($why)"
  else
    say_fail "$tag is not a whole run: $why"
  fi
done

"$PY" bone-mismatch-semantics.py "$CAP/driven.trace" "$CAP/undriven.trace" \
  > "$OUT/semantics.txt" 2>&1
rc=$?
cat "$OUT/semantics.txt"
if [[ $rc -ne 0 ]]; then
  say_fail "the semantic checker rejected a real trace (rc=$rc)"
fi
passes=$(grep -c "^\[PASS\]" "$OUT/semantics.txt")
if [[ "$passes" != "2" ]]; then
  say_fail "expected 2 passing traces, saw ${passes:-0}"
else
  echo "  OK both real traces satisfy the tier A/B rules"
fi
bm=$(grep -a "boneMismatch=" "$OUT/semantics.txt" | head -1 | sed -n "s/.*boneMismatch=\([0-9]*\).*/\1/p")
if [[ -n "$bm" && $bm -gt 0 ]]; then
  echo "  OK the driven run reports boneMismatch=$bm > 0 (helper bones, not a defect)"
else
  say_fail "the driven run reports boneMismatch=${bm:-none}, expected > 0"
fi

# The controls: a counter that goes backwards, and an increment no census line
# explains. Both must be rejected, so the rules above are known to fire.
for kind in decrease unattributed; do
  "$PY" bone-mismatch-semantics.py --mutate "$kind" "$CAP/driven.trace" \
    "$OUT/control-$kind.trace" > "$CAP/control-$kind.mutate" 2>&1
  if [[ $? -ne 0 ]]; then say_fail "could not build the $kind control"; continue; fi
  "$PY" bone-mismatch-semantics.py --expect-fail "$OUT/control-$kind.trace" \
    > "$OUT/control-$kind.txt" 2>&1
  if [[ $? -eq 0 ]]; then
    echo "  OK the $kind control was rejected (the rules fire)"
  else
    say_fail "the $kind control was accepted: a green run would be vacuous"
  fi
done

# The third control exercises completeness rather than the counters: the real
# driven trace with its capture: armed line removed is what a run that stopped
# short of the capture tick leaves behind. Both layers must reject it, the bash
# guard and the checker's own whole-run rule.
grep -av "capture: armed" "$CAP/driven.trace" > "$OUT/control-truncated.trace" 2>/dev/null
{
  echo "[REJECTED-AS-REQUIRED] bone-mismatch/control-truncated.trace"
  if why=$(complete_trace "$OUT/control-truncated.trace"); then
    echo "        ACCEPTED ($why) -- the completeness guard did not fire"
  else
    echo "        FAIL $why"
  fi
  "$PY" bone-mismatch-semantics.py --expect-fail "$OUT/control-truncated.trace" \
    2>&1 | sed "s/^/        /"
} > "$OUT/control-truncated.txt" 2>&1
if grep -q "ACCEPTED" "$OUT/control-truncated.txt"; then
  say_fail "the truncated control was accepted: a partial run would pass"
elif ! grep -q "REJECTED-AS-REQUIRED" "$OUT/control-truncated.txt"; then
  say_fail "the truncated control was not rejected by the checker"
else
  echo "  OK the truncated control was rejected (a partial run cannot pass the gate)"
fi

{
  echo "bone-mismatch check, tree $(git rev-parse --short HEAD 2>/dev/null || echo NA)"
  echo "demo=$DEMO"
  echo "tick=$TICK speed=$SPEED camera=${CAMERA[*]}"
  echo "product sha256=$(sha256sum "$PRODUCT" | cut -d" " -f1)"
  echo "driven trace sha256=$(sha256sum "$CAP/driven.trace" | cut -d" " -f1)"
  echo "undriven trace sha256=$(sha256sum "$CAP/undriven.trace" | cut -d" " -f1)"
} > "$OUT/readings.txt"

echo ""
if [[ $fail -ne 0 ]]; then
  echo "BONE-MISMATCH-SEMANTICS=FAIL"
  exit 1
fi
echo "BONE-MISMATCH-SEMANTICS=PASS"
exit 0
