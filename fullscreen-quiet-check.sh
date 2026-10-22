#!/usr/bin/env bash
# fullscreen-quiet-check.sh -- proves that the capture path does not take the
# desktop away from a fullscreen application, and that the proof is sensitive.
#
# WHY THIS IS A GATE AND NOT A PROMISE
# ------------------------------------
# quiet-capture-check.sh already proves the product window is hidden, using the
# program's own IsWindowVisible() reading. That answers "is the product window
# hidden". It does NOT answer the question an operator actually asks: "if I am in
# a fullscreen game on this machine, does running your chain take me out of it".
# Those are different questions. A window can be created hidden and still take the
# foreground for a frame while it initialises; a process can steal activation
# without ever showing a window of its own.
#
# SO THIS FILE MEASURES THE DESKTOP, NOT THE PRODUCT
# --------------------------------------------------
# desktop-guard.ps1 puts a borderless topmost fullscreen window on the primary
# monitor -- a stand-in for whatever the operator has up -- and records, for the
# whole time it is up: how many times a DIFFERENT process held the foreground,
# how many times the guard was deactivated, whether the display mode changed, and
# the worst gap between two 20 ms samples.
#
# THREE PHASES, BECAUSE ONE PHASE PROVES NOTHING
# ---------------------------------------------
#   baseline  the guard alone, nothing else running. Whatever it records here is
#             the desktop's own noise and must not be charged to the product.
#   forward   the guard plus a capture run with --hidden-window --audio-disabled.
#             The claim: no MORE interference than the baseline.
#   control   the guard plus the same run WITHOUT --hidden-window. This is the
#             phase that proves the guard can see interference at all. If a shown
#             window does not disturb the guard, the forward reading is vacuous.
#
# The control phase is not a mutation: it runs on every invocation, because "the
# instrument works" is part of the claim and not a separate experiment.
#
# WHAT THIS DOES NOT PROVE
# ------------------------
# The guard is a WinForms borderless window, not a Direct3D exclusive-mode swap
# chain. It proves the desktop, the display mode and the foreground are not taken.
# Whether an exclusive-fullscreen game keeps its flip queue is NOT VERIFIED here.
#
# USAGE
#   bash fullscreen-quiet-check.sh              # forward
#   bash fullscreen-quiet-check.sh --mutation   # one targeted break per case
set -o pipefail

MUTATION=0
while [ $# -gt 0 ]; do
  case "$1" in
    --mutation) MUTATION=1 ;;
    *) echo "usage: $0 [--mutation]" >&2; exit 2 ;;
  esac
  shift
done

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT" || exit 1
OUT="$ROOT/.scratch/fullscreen-quiet"
mkdir -p "$OUT"
EXE="$ROOT/native/build-nmake/tf2_demo_native.exe"
GUARD="$ROOT/desktop-guard.ps1"
# The same inputs quiet-capture-check.sh uses, because a different demo would make
# the two gates measure different things.
TF="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
BAGEL="D:/TF2_Demo_Player/testdata/demos/4a9bfb9276509d0ec5f5fdc722a95b17_match-20260927-0239-koth_bagel_rc13.dem"
PINNED_TICK=400
RUN_TIMEOUT=180
GUARD_SECONDS=${GUARD_SECONDS:-16}

fail=0
ok_count=0
assert_eq() { # assert_eq <label> <got> <want>
  if [ "$2" = "$3" ]; then
    echo "  OK   $1 = $2"; ok_count=$((ok_count + 1))
  else
    echo "  FAIL $1 = $2 (want $3)"; fail=$((fail + 1))
  fi
}
assert_le() { # assert_le <label> <got> <limit>
  if [ "$2" -le "$3" ] 2>/dev/null; then
    echo "  OK   $1 = $2 (<= $3)"; ok_count=$((ok_count + 1))
  else
    echo "  FAIL $1 = $2 (want <= $3)"; fail=$((fail + 1))
  fi
}
assert_gt() { # assert_gt <label> <got> <floor>
  if [ "$2" -gt "$3" ] 2>/dev/null; then
    echo "  OK   $1 = $2 (> $3)"; ok_count=$((ok_count + 1))
  else
    echo "  FAIL $1 = $2 (want > $3)"; fail=$((fail + 1))
  fi
}
field() { # field <file> <key>
  grep -oE "$2=[^ ]*" "$1" 2>/dev/null | head -1 | cut -d= -f2
}

# The guard closes itself on its own timer, so a phase can never hang on it.
# Runs in the FOREGROUND. The caller backgrounds it and waits on $!, because a PID
# obtained through command substitution is not a child of this shell and `wait`
# on it returns at once -- which reads the guard's file before it is written and
# reports every reading as empty. That is exactly what the first run of this gate
# did, and it is the same class as the rest of this repo's gate bugs: the failure
# looked like a bad product instead of a broken harness.
start_guard() { # start_guard <prefix> <seconds>
  local prefix="$1" secs="$2"
  rm -f "$prefix.guard.txt"
  powershell -NoProfile -ExecutionPolicy Bypass -File "$GUARD" \
    -Seconds "$secs" -OutFile "$(cygpath -m "$prefix.guard.txt")" > "$prefix.guard.log" 2>&1
}

# One product run. All phases launch it identically, or the phases are not
# comparable and the comparison means nothing.
run_product() { # run_product <prefix> [extra args...]
  local prefix="$1"; shift
  # A capture run may never touch the audio device. This is enforced here rather
  # than remembered at every call site, because the one call site that forgot it
  # made noise the operator could hear from another room.
  case " $* " in
    *" --audio-disabled "*) : ;;
    *) echo "  REFUSED: run_product called without --audio-disabled ($prefix)" >&2; echo 99; return ;;
  esac
  rm -f "$prefix.bmp" "$prefix.csv" "$prefix.trace" "$prefix.log"
  local trace
  trace="$(cygpath -m "$PWD/$prefix.trace")"
  # NOT --start-paused. A paused scheduler holds the cursor at tick 0, so
  # --capture-tick N is a tick this run will never arrive at and the phase dies on
  # its timeout (the first run of this gate did exactly that: rc=124, empty
  # readings). quiet-capture-check.sh can pin the tick that way because it compares
  # two PAUSED runs; this gate needs the capture to actually happen.
  TF2_NATIVE_TRACE="$trace" timeout "$RUN_TIMEOUT" "$EXE" \
    --tf-root "$TF" --capture-tick "$PINNED_TICK" --demo "$BAGEL" \
    --capture-frame "$prefix.bmp" --metrics-file "$prefix.csv" "$@" > "$prefix.log" 2>&1
  echo "$?"
}

# The product's own IsWindowVisible answer goes to the TRACE ($TF2_NATIVE_TRACE),
# not to stdout: the "mode: window_created=... window_visible=..." line is a
# traceFmt line. Reading it from the .log returned the empty string, which then
# compared unequal to "0" and was reported as "the product says its window IS
# visible" -- a harness bug wearing the product's clothes.
win_visible() { # win_visible <prefix>  -> the product's own IsWindowVisible answer
  field "$1.trace" "window_visible"
}

# Did the PRODUCT take the desktop, or did something else on this machine happen
# to grab a frame while the phase was running? The count cannot tell those apart:
# this desktop has an unrelated application that occasionally takes the
# foreground, and a single blip during a 16 s phase would fail a count-based
# rule for a reason that has nothing to do with the product. The name can.
product_is_thief() { # product_is_thief <guard-file> -> 1 when tf2_demo_native is listed
  local names
  names=$(field "$1" "guard.stolenBy")
  case "$names" in
    *tf2_demo_native*) echo 1 ;;
    *) echo 0 ;;
  esac
}


# The display must be still before anything is measured across it. The first run
# of this gate caught modeBefore=1280x1024 against modeAfter=2560x1440 -- not the
# product's doing, the session was changing mode while the guard came up -- and a
# mode assertion taken across that window is worthless either way. Two samples a
# second apart have to agree, or the run refuses to start.
mode_stable() { # two EnumDisplaySettings samples one second apart must agree
  local a b
  a=$(powershell -NoProfile -ExecutionPolicy Bypass -File "$ROOT/.scratch/t49/mode-probe.ps1" 2>/dev/null | tr -d '\r')
  sleep 1
  b=$(powershell -NoProfile -ExecutionPolicy Bypass -File "$ROOT/.scratch/t49/mode-probe.ps1" 2>/dev/null | tr -d '\r')
  echo "  display first=${a:-unknown} second=${b:-unknown}"
  [ -n "$a" ] && [ "$a" = "$b" ]
}

echo "=== 0/3 the display must be still before anything is measured across it ==="
if mode_stable; then
  echo "  OK   the display mode was stable across a one second window"
  ok_count=$((ok_count + 1))
else
  echo "  REFUSED the display mode moved while this gate was starting; nothing measured here would mean anything"
  echo "FULLSCREEN-QUIET=REFUSED (unstable display)"; exit 3
fi
echo "=== 1/3 baseline: the guard alone, nothing else running ==="
start_guard "$OUT/baseline" "$GUARD_SECONDS" &
gb=$!
wait "$gb" 2>/dev/null
b_deact=$(field "$OUT/baseline.guard.txt" "guard.deactivations")
b_stolen=$(field "$OUT/baseline.guard.txt" "guard.foregroundStolenSamples")
b_mode=$(field "$OUT/baseline.guard.txt" "guard.modeChangedFields")
echo "  baseline deactivations=$b_deact stolenSamples=$b_stolen modeChangedFields=$b_mode"
echo "  mode before=$(field "$OUT/baseline.guard.txt" "guard.modeBefore") after=$(field "$OUT/baseline.guard.txt" "guard.modeAfter")"
assert_eq "the guard ran for the whole baseline window" "$(field "$OUT/baseline.guard.txt" "guard.seconds")" "$GUARD_SECONDS"
assert_eq "baseline changed no display field" "$b_mode" "0"
# The baseline is a READING, not a verdict. This desktop has other applications
# that take the foreground on their own schedule (431 samples in the first run),
# and failing the gate for that would be failing it for the machine, not the
# program. What the baseline must prove is only that the product was not running
# in it -- so the by-name rule is the right thing to assert here too.
echo "  baseline stolenBy=$(field "$OUT/baseline.guard.txt" "guard.stolenBy")"
assert_eq "the baseline did not contain the product" "$(product_is_thief "$OUT/baseline.guard.txt")" "0"
assert_eq "baseline was never deactivated" "$b_deact" "0"
echo "=== 2/3 forward: the same run with --hidden-window --audio-disabled ==="
start_guard "$OUT/forward" "$GUARD_SECONDS" &
gf=$!
sleep 1
fwd_rc=$(run_product "$OUT/forward" --hidden-window --audio-disabled)
wait "$gf" 2>/dev/null
f_deact=$(field "$OUT/forward.guard.txt" "guard.deactivations")
f_stolen=$(field "$OUT/forward.guard.txt" "guard.foregroundStolenSamples")
f_mode=$(field "$OUT/forward.guard.txt" "guard.modeChangedFields")
f_vis=$(win_visible "$OUT/forward")
echo "  forward rc=$fwd_rc deactivations=$f_deact stolenSamples=$f_stolen modeChangedFields=$f_mode window_visible=$f_vis"
echo "  stolenBy=$(field "$OUT/forward.guard.txt" "guard.stolenBy")"
assert_eq "the hidden run still captured a frame" "$fwd_rc" "0"
assert_eq "the product says its own window is not visible" "$f_vis" "0"
assert_eq "the hidden run never deactivated the guard" "$f_deact" "0"
# The claim that matters: the product itself never held the foreground. Other
# processes may blip (the baseline records the same ambient noise); what must not
# happen is that OUR process takes the desktop.
assert_eq "the hidden run never took the foreground as the product" "$(product_is_thief "$OUT/forward.guard.txt")" "0"
assert_eq "the hidden run changed no display field" "$f_mode" "0"

echo "=== 3/3 control: the same run with a SHOWN window, which must disturb it ==="
start_guard "$OUT/control" "$GUARD_SECONDS" &
gc=$!
sleep 1
# --audio-disabled on the CONTROL phase too. It was omitted, and the product opened
# the default playback device and played the demo's audio at system volume for the
# whole phase -- the operator heard it from another room. The control differs from
# the forward phase in exactly one dimension (whether the window is shown); an
# audio flag difference silently turned one run into a public-address test.
ctl_rc=$(run_product "$OUT/control" --audio-disabled)
wait "$gc" 2>/dev/null
c_deact=$(field "$OUT/control.guard.txt" "guard.deactivations")
c_stolen=$(field "$OUT/control.guard.txt" "guard.foregroundStolenSamples")
c_vis=$(win_visible "$OUT/control")
echo "  control rc=$ctl_rc deactivations=$c_deact stolenSamples=$c_stolen window_visible=$c_vis"
echo "  stolenBy=$(field "$OUT/control.guard.txt" "guard.stolenBy")"
assert_eq "the shown run also captured a frame" "$ctl_rc" "0"
assert_eq "the product says its own window IS visible" "$c_vis" "1"
# The sensitivity of the instrument. Without this pair the forward reading would
# be "quiet" on a machine where the measurement never worked.
assert_eq "a shown window is seen by the guard, by name" "$(product_is_thief "$OUT/control.guard.txt")" "1"
assert_gt "a shown window is seen by the guard (deactivations)" "$c_deact" "$b_deact"

echo
echo "fullscreen-quiet: assertions_ok=$ok_count failures=$fail"
if [ "$fail" -eq 0 ]; then echo "FULLSCREEN-QUIET=PASS"; else echo "FULLSCREEN-QUIET=FAIL"; fi
[ "$fail" -eq 0 ] || exit 1

if [ "$MUTATION" -eq 1 ]; then
  echo
  echo "=== mutation: the two readings that could make this gate vacuous ==="
  mfail=0

  # M1: run the forward phase with a SHOWN window. The gate must go red. If it
  # stays green the gate is not measuring what it claims to measure.
  echo "-- M1: forward phase with the window shown"
  start_guard "$OUT/m1" "$GUARD_SECONDS" &
  gm=$!
  sleep 1
  m1_rc=$(run_product "$OUT/m1" --audio-disabled)
  wait "$gm" 2>/dev/null
  m1_deact=$(field "$OUT/m1.guard.txt" "guard.deactivations")
  echo "   m1 deactivations=$m1_deact rc=$m1_rc"
  if [ "${m1_deact:-0}" -gt 0 ]; then
    echo "  OK   M1 caught: a shown window deactivates the guard where the hidden one did not"
  else
    echo "  FAIL M1 survived: a shown window left the guard alone"; mfail=$((mfail + 1))
  fi

  # M2: make the guard blind to who holds the foreground. The control phase must
  # then go red, which is what proves the control phase is load bearing.
  echo "-- M2: blind the guard's foreground counter"
  cp "$GUARD" "$OUT/guard.orig.ps1"
  sed -i 's/if (\$fgPid -ne \$script:myPid) {/if (\$false) {/' "$GUARD"
  if cmp -s "$GUARD" "$OUT/guard.orig.ps1"; then
    echo "  FAIL M2 could not be applied: the line it needs is not there"
    mfail=$((mfail + 1))
  else
    start_guard "$OUT/m2" "$GUARD_SECONDS" &
    gb2=$!
    sleep 1
    m2_rc=$(run_product "$OUT/m2" --audio-disabled)
    wait "$gb2" 2>/dev/null
    cp "$OUT/guard.orig.ps1" "$GUARD"
    if cmp -s "$GUARD" "$OUT/guard.orig.ps1"; then
      echo "  OK   the guard was restored byte for byte"
    else
      echo "  FAIL the guard was NOT restored"; mfail=$((mfail + 1))
    fi
    m2_stolen=$(field "$OUT/m2.guard.txt" "guard.foregroundStolenSamples")
    echo "   m2 stolenSamples=$m2_stolen rc=$m2_rc (blind guard, shown window)"
    # The claim is not "the blindness worked", it is "the gate goes red when the
    # instrument is blind". The control phase requires stolen > baseline; with a
    # blind guard that rule must now fail, and that failure IS the detection.
    assert_le "M2 the blind guard reports zero thieves" "$m2_stolen" "0"
    if [ "$(product_is_thief "$OUT/m2.guard.txt")" = "0" ]; then
      echo "  OK   M2 detected: with a blind guard the by-name rule reports 0 thieves, so the control phase cannot pass"
    else
      echo "  FAIL M2 survived: the control phase still looked satisfied"; mfail=$((mfail + 1))
    fi
  fi

  if [ "$mfail" -eq 0 ]; then echo "FULLSCREEN-QUIET-MUTATION=PASS"; else echo "FULLSCREEN-QUIET-MUTATION=FAIL"; exit 1; fi
fi

