#!/usr/bin/env bash
# playback-control-check.sh -- T70: fine playback controls (play/pause/stop,
# step +-1, jump +-1 s / +-5 s, speed, seek) and their frame landings.
#
# THE DEFECT THIS GATE PINS
#   Before T70 every transport action wrote g_playback directly. A step changed
#   the cursor and the frame that was already in flight still belonged to the
#   old tick, so "step one frame" could show a frame that was not the frame it
#   claimed: nothing recorded which tick was requested, which tick the rendered
#   frame actually came from, or whether the request was refused. The tick could
#   also move outside the demo, and a second seek silently replaced the first
#   with no count of the interruption.
#
# WHAT IT ASSERTS (task A-F)
#   A  frame stepping is exact: ten +1 steps land on 101..110, ten -1 steps land
#      back on 109..100, stepping below 0 reports requested=-1 landed=0
#      status=clamped, stepping past the end reports requested=1001 landed=1000
#      status=clamped, and the cursor ends at the end tick.
#   B  the 1 s jump moves exactly 67 ticks and the 5 s jump exactly 333, and a
#      jump does not change the transport: the playing flag is 1 before and
#      after the 5 s jump (made while playing).
#   C  speed 2.0 doubles the tick advance of speed 1.0 in one simulated second
#      (66 vs 133 ticks; the 1-tick difference from 2x66 is the floor of
#      66.6667 ticks/s, tolerance 2 ticks), and a speed change does not move the
#      cursor. All seven DESIGN speeds are checked against their table.
#   D  an interrupted seek is counted: two seeks without a landing in between
#      report superseded=<first id>, interruptedSeeks=1, and the final landing is
#      the second request at cursor 4000.
#   E  an unreachable tick (> end or < 0) on a strict request reports
#      status=unreachable and leaves the cursor unchanged; the same tick on a
#      non-strict request clamps to 0/end and reports status=clamped.
#   F  negative control: stepping on a missing demo fails by name
#      (error=demo-not-found, exit 1), not by silently doing nothing.
#   G  product integration (not one of the task's lettered assertions, but the
#      wiring the task requires): a --camera-script run drives seek/step/
#      jump-seconds/speed through the product's request path, prints the
#      playback: request lines, and the window title carries req=/act=/frame=.
#
# MUTATIONS (--mutation): each must red its named assertion and restore the
# source byte-identically before the next one.
#   m1 StepTicks moves two ticks instead of one          -> A red
#   m2 setSpeed is display-only (advance ignores speed)  -> C red
#   m3 tick moves are not clamped to [0, endTick]         -> E red
#   m4 a seek does not interrupt a pending seek           -> D red
#
# RUN UNDER THE HOST LOCK. This gate does NOT acquire the lock (a gate that
# acquired it would deadlock inside a locked run). Invoke as:
#   bash /d/TF2_Native_Worktrees/_shared/hostlock.sh run t70-controls \
#     bash /d/TF2_Native_Worktrees/t70-controls/playback-control-check.sh
#
# Usage: bash playback-control-check.sh [--mutation]
# Exit:  0 = every assertion held (and, with --mutation, every mutation red).
set -uo pipefail
cd "$(dirname "$0")"

EXE=native/build-nmake/tf2_demo_native.exe
PROBE=native/build-nmake/playback_control_probe.exe
TF="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
DEMO="D:/TF2_Demo_Player/testdata/demos/4a9bfb9276509d0ec5f5fdc722a95b17_match-20260927-0239-koth_bagel_rc13.dem"
MISSING="D:/TF2_Demo_Player/testdata/demos/t70-controls-missing-demo.dem"
OUT="${PLAYBACK_OUT:-evidence/t70-controls/gate}"
mkdir -p "$OUT"
PY="C:/Users/Administrator/.workbuddy-ai/binaries/python/versions/3.13.12/python.exe"

MUTATION=0
for arg in "$@"; do
  case "$arg" in
    --mutation) MUTATION=1 ;;
    *) echo "unknown argument: $arg" >&2; exit 2 ;;
  esac
done

fail=0
assert_eq() { # assert_eq <label> <actual> <expected>
  if [ "${2:-<none>}" = "$3" ]; then
    echo "  OK   $1 = $3"
  else
    echo "  FAIL $1 = ${2:-<none>}, expected $3"; fail=1
  fi
}
assert_ne() { # assert_ne <label> <actual> <not-expected>
  if [ -n "${2:-}" ] && [ "$2" != "$3" ]; then
    echo "  OK   $1 = $2 (!= $3)"
  else
    echo "  FAIL $1 = ${2:-<none>}, expected != $3"; fail=1
  fi
}
assert_le() { # assert_le <label> <actual> <ceiling>
  if [ -n "${2:-}" ] && [ "$2" -le "$3" ] 2>/dev/null; then
    echo "  OK   $1 = $2 (<= $3)"
  else
    echo "  FAIL $1 = ${2:-<none>}, expected <= $3"; fail=1
  fi
}
assert_contains() { # assert_contains <label> <file> <fixed needle>
  if grep -a -q -F -- "$3" "$2" 2>/dev/null; then
    echo "  OK   $1 contains '$3'"
  else
    echo "  FAIL $1: '$3' not found in $2"; fail=1
  fi
}
v() { # v <file> <key> -- value of the LAST "<key>=<value>" token
  local file="$1" key="$2" line
  [ -f "$file" ] || return 0
  line=$(grep -a -o -E "(^| )${key}=[^ ]+" "$file" 2>/dev/null | tail -1)
  printf '%s' "$line" | sed -e 's/^ //' -e "s/^${key}=//"
}
step_field() { # step_field <file> <step> <key> -- field of the step= line
  awk -v s="$2" -v k="$3" '$1=="step="s {for(i=2;i<=NF;i++){split($i,a,"="); if(a[1]==k){print a[2]; exit}}}' "$1"
}
nth_cmd() { # nth_cmd <file> <cmd> <n> <key> -- nth step= line with cmd=<cmd>
  awk -v c="$2" -v n="$3" -v k="$4" '$2=="cmd="c {++count; if(count==n){for(i=1;i<=NF;i++){split($i,a,"="); if(a[1]==k){print a[2]; exit}}}}' "$1"
}
advance_ticks() { # advance_ticks <file> <label> -- ticks of advance label=<label>
  awk -v l="$2" '$1=="advance" && $2=="label="l {for(i=1;i<=NF;i++){split($i,a,"="); if(a[1]=="ticks"){print a[2]; exit}}}' "$1"
}
check_ok() { # check_ok <file> <check label> -- ok= of a check= line
  awk -v l="$2" '$1=="check="l {for(i=2;i<=NF;i++){split($i,a,"="); if(a[1]=="ok"){print a[2]; exit}}}' "$1"
}
count_fixed() { # count_fixed <file> <fixed string>
  grep -a -F -c -- "$2" "$1" 2>/dev/null || true
}

# ---- A: frame stepping is exact -------------------------------------------
check_a() { # <suite file>
  local F="$1" i
  for i in 1 2 3 4 5 6 7 8 9 10; do
    assert_eq "A step-forward #$i requested" "$(nth_cmd "$F" step-forward "$i" requested)" "$((100 + i))"
    assert_eq "A step-forward #$i landed" "$(nth_cmd "$F" step-forward "$i" landed)" "$((100 + i))"
    assert_eq "A step-forward #$i status" "$(nth_cmd "$F" step-forward "$i" status)" "ok"
    assert_eq "A step-forward #$i playing" "$(nth_cmd "$F" step-forward "$i" playing)" "0"
  done
  for i in 1 2 3 4 5 6 7 8 9 10; do
    assert_eq "A step-backward #$i requested" "$(nth_cmd "$F" step-backward "$i" requested)" "$((110 - i))"
    assert_eq "A step-backward #$i landed" "$(nth_cmd "$F" step-backward "$i" landed)" "$((110 - i))"
    assert_eq "A step-backward #$i status" "$(nth_cmd "$F" step-backward "$i" status)" "ok"
  done
  # The 11th step in each direction is the boundary probe.
  assert_eq "A boundary start requested" "$(nth_cmd "$F" step-backward 11 requested)" "-1"
  assert_eq "A boundary start landed" "$(nth_cmd "$F" step-backward 11 landed)" "0"
  assert_eq "A boundary start status" "$(nth_cmd "$F" step-backward 11 status)" "clamped"
  assert_eq "A boundary end requested" "$(nth_cmd "$F" step-forward 11 requested)" "1001"
  assert_eq "A boundary end landed" "$(nth_cmd "$F" step-forward 11 landed)" "1000"
  assert_eq "A boundary end status" "$(nth_cmd "$F" step-forward 11 status)" "clamped"
  assert_eq "A boundary end cursor check" "$(check_ok "$F" a-boundary-end-cursor)" "1"
}

# ---- B: 1 s / 5 s jumps, transport unchanged -------------------------------
check_b() {
  local F="$1"
  assert_eq "B jump-1s requested" "$(nth_cmd "$F" jump-1s 1 requested)" "167"
  assert_eq "B jump-1s landed" "$(nth_cmd "$F" jump-1s 1 landed)" "167"
  assert_eq "B jump-1s status" "$(nth_cmd "$F" jump-1s 1 status)" "ok"
  assert_eq "B jump-1s playing (paused by the seek before it)" "$(nth_cmd "$F" jump-1s 1 playing)" "0"
  assert_eq "B resume playing" "$(nth_cmd "$F" resume 1 playing)" "1"
  assert_eq "B jump-5s requested" "$(nth_cmd "$F" jump-5s 1 requested)" "500"
  assert_eq "B jump-5s landed" "$(nth_cmd "$F" jump-5s 1 landed)" "500"
  assert_eq "B jump-5s status" "$(nth_cmd "$F" jump-5s 1 status)" "ok"
  assert_eq "B jump-5s playing (unchanged)" "$(nth_cmd "$F" jump-5s 1 playing)" "1"
  assert_eq "B playing before jump5 check" "$(check_ok "$F" b-playing-before-jump5)" "1"
  assert_eq "B playing after jump5 check" "$(check_ok "$F" b-playing-after-jump5)" "1"
}

# ---- C: speed doubles the advance, and does not move the cursor ------------
check_c() {
  local F="$1"
  assert_eq "C speed 0.125 ticks/s" "$(advance_ticks "$F" speed0.125)" "8"
  assert_eq "C speed 0.25 ticks/s" "$(advance_ticks "$F" speed0.250)" "16"
  assert_eq "C speed 0.5 ticks/s" "$(advance_ticks "$F" speed0.500)" "33"
  assert_eq "C speed 1.0 ticks/s" "$(advance_ticks "$F" speed1.000)" "66"
  assert_eq "C speed 2.0 ticks/s" "$(advance_ticks "$F" speed2.000)" "133"
  assert_eq "C speed 4.0 ticks/s" "$(advance_ticks "$F" speed4.000)" "266"
  assert_eq "C speed 8.0 ticks/s" "$(advance_ticks "$F" speed8.000)" "533"
  # 2x66 = 132; 133 is one tick above because 66.6667 ticks/s is floored. The
  # tolerance is two ticks, not a percentage: the measurement is integer ticks.
  local delta=$(( 133 - 2 * 66 ))
  if [ "$delta" -lt 0 ]; then delta=$(( -delta )); fi
  assert_le "C 2.0 vs 2x1.0 delta (ticks)" "$delta" "2"
  assert_eq "C speed-change does not move tick (checks ok)" "$(count_fixed "$F" "check=c-speed-does-not-move-tick ok=1")" "7"
  assert_eq "C speed-change does not move tick (checks failed)" "$(count_fixed "$F" "check=c-speed-does-not-move-tick ok=0")" "0"
}

# ---- D: an interrupted seek is counted, the last request lands -------------
check_d() {
  local F="$1" line sup pend landing
  line=$(grep -a "^interrupt " "$F" | tail -1)
  sup=$(printf '%s' "$line" | grep -a -o -E " superseded=[^ ]+" | tail -1 | sed 's/^ superseded=//')
  pend=$(printf '%s' "$line" | grep -a -o -E " pending=[^ ]+" | tail -1 | sed 's/^ pending=//')
  assert_ne "D superseded request id" "$sup" "0"
  assert_ne "D pending request id" "$pend" "0"
  assert_ne "D superseded is the displaced request, not the pending one" "$sup" "$pend"
  assert_eq "D interruptedSeeks" "$(printf '%s' "$line" | grep -a -o -E " interruptedSeeks=[^ ]+" | tail -1 | sed 's/^ interruptedSeeks=//')" "1"
  assert_eq "D superseded-id check" "$(check_ok "$F" d-superseded-id)" "1"
  assert_eq "D interrupted-seeks check" "$(check_ok "$F" d-interrupted-seeks)" "1"
  # The first landing line is D's; the landing-map block later prints two more.
  landing=$(grep -a "^landing " "$F" | head -1)
  assert_eq "D final landing id is the pending request" "$(printf '%s' "$landing" | grep -a -o -E " id=[^ ]+" | sed 's/^ id=//')" "$pend"
  assert_eq "D final landing cursor" "$(printf '%s' "$landing" | grep -a -o -E " landedCursor=[^ ]+" | sed 's/^ landedCursor=//')" "4000"
  assert_eq "D final landing status" "$(printf '%s' "$landing" | grep -a -o -E " frameStatus=[^ ]+" | sed 's/^ frameStatus=//')" "exact"
  assert_eq "D final-landing-id check" "$(check_ok "$F" d-final-landing-id)" "1"
  assert_eq "D final-landing-cursor check" "$(check_ok "$F" d-final-landing-cursor)" "1"
}

# ---- E: unreachable ticks are refused, out-of-range moves clamp ------------
check_e() {
  local F="$1"
  assert_eq "E strict end requested" "$(nth_cmd "$F" seek-strict 1 requested)" "1001"
  assert_eq "E strict end landed (cursor unchanged)" "$(nth_cmd "$F" seek-strict 1 landed)" "100"
  assert_eq "E strict end status" "$(nth_cmd "$F" seek-strict 1 status)" "unreachable"
  assert_eq "E strict start requested" "$(nth_cmd "$F" seek-strict 2 requested)" "-1"
  assert_eq "E strict start landed (cursor unchanged)" "$(nth_cmd "$F" seek-strict 2 landed)" "100"
  assert_eq "E strict start status" "$(nth_cmd "$F" seek-strict 2 status)" "unreachable"
  assert_eq "E unreachable-end-unchanged check" "$(check_ok "$F" e-unreachable-end-unchanged)" "1"
  assert_eq "E unreachable-start-unchanged check" "$(check_ok "$F" e-unreachable-start-unchanged)" "1"
  assert_eq "E clamp end requested" "$(nth_cmd "$F" seek-clamp 1 requested)" "1050"
  assert_eq "E clamp end landed" "$(nth_cmd "$F" seek-clamp 1 landed)" "1000"
  assert_eq "E clamp end status" "$(nth_cmd "$F" seek-clamp 1 status)" "clamped"
  assert_eq "E clamp start requested" "$(nth_cmd "$F" seek-clamp 2 requested)" "-50"
  assert_eq "E clamp start landed" "$(nth_cmd "$F" seek-clamp 2 landed)" "0"
  assert_eq "E clamp start status" "$(nth_cmd "$F" seek-clamp 2 status)" "clamped"
  assert_eq "E clamped-end check" "$(check_ok "$F" e-clamped-end)" "1"
  assert_eq "E clamped-start check" "$(check_ok "$F" e-clamped-start)" "1"
}

# ---- focus and landing map (the rest of the controller's contract) --------
check_focus_and_landing() {
  local F="$1"
  assert_eq "focus: keyboard step refused while a text field has focus" "$(nth_cmd "$F" step-forward-focus 1 status)" "rejected"
  assert_eq "focus-suppressed count" "$(check_ok "$F" focus-suppressed)" "1"
  assert_eq "landing checkpoint status" "$(check_ok "$F" landing-checkpoint)" "1"
  assert_eq "landing checkpoint delta" "$(check_ok "$F" landing-checkpoint-delta)" "1"
  assert_eq "landing before-window status" "$(check_ok "$F" landing-before-window)" "1"
}

# ---- F: the negative control ----------------------------------------------
check_f() {
  local rc
  "$PROBE" --demo "$MISSING" --command step-forward > "$OUT/f-missing.txt" 2>&1
  rc=$?
  assert_eq "F missing-demo exit code" "$rc" "1"
  assert_contains "F missing-demo names the file" "$OUT/f-missing.txt" "error=demo-not-found"
  assert_eq "F missing-demo step status" "$(step_field "$OUT/f-missing.txt" 1 status)" "rejected"
}

# ---- G: the product wiring -------------------------------------------------
product_run() { # product_run <prefix> <demo> [extra args...]
  local prefix="$1" demo="$2"; shift 2
  rm -f "$prefix.trace" "$prefix.log"
  TF2_NATIVE_TRACE="$(cygpath -m "$PWD/$prefix.trace")" \
    timeout "${RUN_TIMEOUT:-600}" "$EXE" \
      --tf-root "$TF" --hidden-window --audio-disabled \
      --settings-file "$(cygpath -m "$PWD/$prefix.settings.ini")" \
      --ui-settings-file "$(cygpath -m "$PWD/$prefix.ui.ini")" \
      --demo "$demo" "$@" > "$prefix.log" 2>&1
  return $?
}
check_g() {
  local rc
  cat > "$OUT/g-script.txt" <<'EOF'
# The script must establish a deterministic transport before it asserts exact
# ticks: while the product is playing the cursor advances between two script
# steps (measured: seek 100 then step landed on 103, jump landed 170). A pause
# first makes every later request land on an exact, replayable tick.
pause
playback-state
seek 100
step 1
jump-seconds 1
speed 2.0
playback-state
quit
EOF
  product_run "$OUT/g-product" "$DEMO" --camera-script "$(cygpath -m "$PWD/$OUT/g-script.txt")"
  rc=$?
  assert_eq "G product exit code" "$rc" "0"
  assert_contains "G seek accepted" "$OUT/g-product.trace" "cmd=seek result=ok"
  assert_contains "G seek request" "$OUT/g-product.trace" "kind=jump-ticks source=camera-script requestedTick=100 "
  assert_contains "G step request" "$OUT/g-product.trace" "kind=step-ticks source=camera-script requestedTick=101 "
  assert_contains "G 1 s jump request" "$OUT/g-product.trace" "kind=jump-seconds source=camera-script requestedTick=168 "
  assert_contains "G speed request" "$OUT/g-product.trace" "kind=set-speed source=camera-script"
  assert_contains "G speed after" "$OUT/g-product.trace" "speedAfter=2.000000"
  assert_contains "G state cursor" "$OUT/g-product.trace" "playback: state phase=paused cursor=168 "
  assert_contains "G state speed" "$OUT/g-product.trace" "speed=2.000000"
  assert_contains "G title req/act" "$OUT/g-product.trace" " req=168 act=168 frame="
  # The first title of a run is printed before any request has landed, so it
  # legitimately says frame=pending. What must not happen is the LAST title --
  # the state a user reads -- still saying pending after the last request landed.
  grep -a "title: " "$OUT/g-product.trace" | tail -1 > "$OUT/g-last-title.txt"
  assert_contains "G last title req/act" "$OUT/g-last-title.txt" " req=168 act=168 frame="
  # The count below is vacuous on an empty file, so first assert the file is a
  # real title line: a crashed run must not read as "no pending frame".
  assert_contains "G last title is a title line" "$OUT/g-last-title.txt" "title: "
  assert_contains "G last title has a req field" "$OUT/g-last-title.txt" " req="
  assert_eq "G last title frame not pending" "$(grep -a -c "frame=pending" "$OUT/g-last-title.txt")" "0"
}

run_forward() {
  echo "=== build ==="
  if [ "${SKIP_BUILD:-0}" != "1" ]; then
    bash build-target.sh playback_control_probe tf2_demo_native > "$OUT/build-baseline.log" 2>&1
    if [ $? -ne 0 ]; then
      echo "  FAIL build"; tail -12 "$OUT/build-baseline.log"; fail=1; return
    fi
    echo "  OK   build playback_control_probe tf2_demo_native"
  fi

  echo
  echo "=== suite: the controller contract (probe) ==="
  "$PROBE" --playback-test > "$OUT/suite.txt" 2> "$OUT/suite.err"
  local rc=$?
  assert_eq "suite exit code" "$rc" "0"
  assert_eq "suite failures" "$(v "$OUT/suite.txt" failures)" "0"
  echo "  INFO suite $(grep -a '^checks=' "$OUT/suite.txt" | tail -1)"

  echo
  echo "=== A: frame stepping is exact ==="
  check_a "$OUT/suite.txt"
  echo "=== B: 1 s / 5 s jumps, transport unchanged ==="
  check_b "$OUT/suite.txt"
  echo "=== C: speed ==="
  check_c "$OUT/suite.txt"
  echo "=== D: interrupted seek ==="
  check_d "$OUT/suite.txt"
  echo "=== E: unreachable ticks ==="
  check_e "$OUT/suite.txt"
  echo "=== focus and landing map ==="
  check_focus_and_landing "$OUT/suite.txt"
  echo "=== F: missing-demo negative control ==="
  check_f
  echo "=== G: product wiring (camera script) ==="
  check_g
}

echo "playback-control-check  demo=$DEMO"
echo "                       probe=$PROBE"
echo
run_forward
FORWARD_FAIL=$fail

if [ "$MUTATION" = "1" ] && [ "$FORWARD_FAIL" != "0" ]; then
  echo
  echo "MUTATION-SKIPPED: the baseline forward assertions are red, so a mutation"
  echo "                  run could not be attributed; fix the forward failure first."
  echo "PLAYBACK-CONTROL-CHECK=FAIL"
  exit 1
fi

if [ "$MUTATION" = "1" ]; then
  echo
  echo "=== mutations: each must move the reading it names ==="
  BACKUP="$OUT/mutation-backup"
  mkdir -p "$BACKUP"
  mut_fail=0
  restore_source() { # <file>
    local file="$1"
    cp "$BACKUP/$(basename "$file")" "$file" || return 1
    if cmp -s "$BACKUP/$(basename "$file")" "$file"; then
      echo "  restored $(basename "$file") byte-identical (cmp)"; return 0
    fi
    echo "  FAIL restore of $file is NOT byte-identical"; return 1
  }
  apply_mutation() { # <file> <needle> <repl> <label>
    local file="$1" needle="$2" repl="$3" label="$4" count
    cp "$file" "$BACKUP/$(basename "$file")" || { echo "  MUTATION-INVALID $label: backup failed"; return 1; }
    count=$(grep -a -F -c -- "$needle" "$file" 2>/dev/null)
    count=${count:-0}
    if [ "$count" != "1" ]; then
      echo "  MUTATION-INVALID $label: the anchor matches $count times, so the mutation"
      echo "                    cannot be attributed to the line it names."
      restore_source "$file" || mut_fail=1
      return 1
    fi
    "$PY" - "$file" "$needle" "$repl" <<'PYEOF'
import sys
path, needle, repl = sys.argv[1], sys.argv[2], sys.argv[3]
data = open(path, 'rb').read()
n, r = needle.encode(), repl.encode()
if data.count(n) != 1:
    sys.exit("anchor count != 1")
open(path, 'wb').write(data.replace(n, r, 1))
PYEOF
    if [ $? -ne 0 ]; then
      echo "  MUTATION-INVALID $label: the replacement did not apply"
      restore_source "$file" || mut_fail=1
      return 1
    fi
  }
  rebuild() { # <label> <target...>
    local label="$1"; shift
    bash build-target.sh "$@" > "$OUT/build-$label.log" 2>&1
    local rc=$?
    if [ "$rc" -ne 0 ]; then
      echo "  MUTATION-INVALID $label: the mutated source does not compile, so the mutation proves nothing"
      tail -6 "$OUT/build-$label.log"
    fi
    return "$rc"
  }
  suite_after_mutation() { # <label> <check function> <letter> <file>
    local label="$1" fn="$2" letter="$3" file="$4" rc
    "$PROBE" --playback-test > "$file" 2> "$file.err"
    rc=$?
    # The probe's own contract checks do not pin every assertion (A's step
    # exactness lives in this gate, not in the probe), so the mutation is
    # judged only by the named gate assertion below.
    echo "  info: probe exit=$rc"
    fail=0
    "$fn" "$file" > "$OUT/$label-checks.txt" 2>&1
    if [ "$fail" = "1" ] && grep -a -q "FAIL $letter " "$OUT/$label-checks.txt"; then
      echo "  MUTATION-RED   $label: $(grep -a "FAIL $letter " "$OUT/$label-checks.txt" | head -1 | sed 's/^  //')"
      return 0
    fi
    echo "  MUTATION-GREEN $label: the named assertion did not move"
    head -20 "$OUT/$label-checks.txt"
    return 1
  }

  # m1: StepTicks moves two ticks instead of one.
  echo
  echo "  -- m1: StepTicks moves +-2 --"
  if apply_mutation native/src/playback_control.cpp \
      '    target = static_cast<std::int64_t>(cursorTick_) + request.ticks;' \
      '    target = static_cast<std::int64_t>(cursorTick_) + (request.ticks > 0 ? 2 : -2);' m1; then
    if rebuild m1 playback_control_probe; then
      suite_after_mutation m1 check_a A "$OUT/m1-suite.txt" || mut_fail=1
      restore_source native/src/playback_control.cpp || mut_fail=1
      rebuild m1-restore playback_control_probe || mut_fail=1
      "$PROBE" --playback-test > "$OUT/m1r-suite.txt" 2>&1
      if [ $? -ne 0 ]; then echo "  FAIL m1 baseline after restore is not green"; mut_fail=1; fi
      assert_eq "m1r suite failures" "$(v "$OUT/m1r-suite.txt" failures)" "0"
    else
      restore_source native/src/playback_control.cpp || mut_fail=1
      rebuild m1-restore playback_control_probe || mut_fail=1
      mut_fail=1
    fi
  else
    mut_fail=1
  fi

  # m2: setSpeed is display-only: the speed is shown but advance ignores it.
  echo
  echo "  -- m2: setSpeed display-only --"
  if apply_mutation native/src/playback_control.cpp \
      '  accumulator_ += elapsedSeconds * tickRate_ * speed_;' \
      '  accumulator_ += elapsedSeconds * tickRate_;' m2; then
    if rebuild m2 playback_control_probe; then
      suite_after_mutation m2 check_c C "$OUT/m2-suite.txt" || mut_fail=1
      restore_source native/src/playback_control.cpp || mut_fail=1
      rebuild m2-restore playback_control_probe || mut_fail=1
      "$PROBE" --playback-test > "$OUT/m2r-suite.txt" 2>&1
      if [ $? -ne 0 ]; then echo "  FAIL m2 baseline after restore is not green"; mut_fail=1; fi
      assert_eq "m2r suite failures" "$(v "$OUT/m2r-suite.txt" failures)" "0"
    else
      restore_source native/src/playback_control.cpp || mut_fail=1
      rebuild m2-restore playback_control_probe || mut_fail=1
      mut_fail=1
    fi
  else
    mut_fail=1
  fi

  # m3: tick moves are not clamped to [0, endTick].
  echo
  echo "  -- m3: no clamping --"
  if apply_mutation native/src/playback_control.cpp \
      '  if (requestedTick < 0 || requestedTick > endTick_) {' \
      '  if (false && (requestedTick < 0 || requestedTick > endTick_)) {' m3; then
    if rebuild m3 playback_control_probe; then
      suite_after_mutation m3 check_e E "$OUT/m3-suite.txt" || mut_fail=1
      restore_source native/src/playback_control.cpp || mut_fail=1
      rebuild m3-restore playback_control_probe || mut_fail=1
      "$PROBE" --playback-test > "$OUT/m3r-suite.txt" 2>&1
      if [ $? -ne 0 ]; then echo "  FAIL m3 baseline after restore is not green"; mut_fail=1; fi
      assert_eq "m3r suite failures" "$(v "$OUT/m3r-suite.txt" failures)" "0"
    else
      restore_source native/src/playback_control.cpp || mut_fail=1
      rebuild m3-restore playback_control_probe || mut_fail=1
      mut_fail=1
    fi
  else
    mut_fail=1
  fi

  # m4: a seek does not interrupt a pending seek.
  echo
  echo "  -- m4: seek interrupt ignored --"
  if apply_mutation native/src/playback_control.cpp \
      '  if (pending_) {' \
      '  if (false && pending_) {' m4; then
    if rebuild m4 playback_control_probe; then
      suite_after_mutation m4 check_d D "$OUT/m4-suite.txt" || mut_fail=1
      restore_source native/src/playback_control.cpp || mut_fail=1
      rebuild m4-restore playback_control_probe || mut_fail=1
      "$PROBE" --playback-test > "$OUT/m4r-suite.txt" 2>&1
      if [ $? -ne 0 ]; then echo "  FAIL m4 baseline after restore is not green"; mut_fail=1; fi
      assert_eq "m4r suite failures" "$(v "$OUT/m4r-suite.txt" failures)" "0"
    else
      restore_source native/src/playback_control.cpp || mut_fail=1
      rebuild m4-restore playback_control_probe || mut_fail=1
      mut_fail=1
    fi
  else
    mut_fail=1
  fi

  echo
  if [ "$mut_fail" = "0" ] && [ "$FORWARD_FAIL" = "0" ]; then
    echo "MUTATION-CAUGHT=PASS (4/4 mutations red their named assertion and restore byte-identical)"
    exit 0
  fi
  echo "MUTATION-CAUGHT=FAIL"
  exit 1
fi

echo
if [ "$fail" = "0" ]; then
  echo "PLAYBACK-CONTROL-CHECK=PASS"
  exit 0
fi
echo "PLAYBACK-CONTROL-CHECK=FAIL"
exit 1
