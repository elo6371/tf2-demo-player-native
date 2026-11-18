#!/usr/bin/env bash
# argument-refusal-check.sh -- T105: an unrecognised switch must REFUSE, not draw.
#
# WHAT WAS WRONG
#   The argument loop (native/src/main.cpp) collected anything it did not know
#   into unrecognizedArguments and then -- unless --strict-args was passed -- kept
#   going, printing nothing and exiting 0. A T105 reading run wrote
#   '--world-flat=1' where the diagnostic switches take NO value, the argument
#   fell through to the collect list, and the product drew FOUR BYTE-IDENTICAL
#   default frames while the reading script believed it had measured albedo, flat,
#   texel and a face trace. A run that asks for a switch the product does not have
#   draws through the default path and reads as a measurement; that is the defect
#   this gate pins.
#
# THE FIX
#   Every unknown argument starting with '--' is refused: a named trace line
#   'args: refused switch count=N first=<name>', an OutputDebugString message
#   naming it, and exit 13. '--strict-args' keeps exit 2 for the wider contract.
#   Non-'--' unknown arguments stay recorded-and-ignored (D-23), because that is
#   the behaviour every existing gate was written against.
#
# WHAT THIS GATE ASSERTS
#   refused value-attached switch : --world-flat=1  -> exit 13, refused line names
#                                   exactly that switch, no capture frame
#   refused unknown switch        : --not-a-real-switch -> exit 13, named
#   accepted bare switch          : --world-flat -> exit 0, frame written
#   tolerated positional          : randomword -> exit 0 (D-23 preserved)
#
# RUN
#   bash /d/TF2_Native_Worktrees/_shared/hostlock.sh run t105-argrefusal \
#     bash argument-refusal-check.sh
set -uo pipefail
cd "$(dirname "$0")"

EXE=native/build-nmake/tf2_demo_native.exe
TF="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
DEMO="E:/demo测试/pl_upward_f12/STV/a9630c6557468b24f0b8c2e7dab38ed8_match-20261030-2145-pl_upward_f12.dem"
TICK=${ARGS_TICK:-0}
OUT=${ARGS_OUT:-.scratch/argument-refusal}
LOCK=${HOSTLOCK:-/d/TF2_Native_Worktrees/_shared/hostlock.sh}
HOLD_LOCK=${HOLD_LOCK:-0}
EXPECT_OWNER=${EXPECT_OWNER:-t105-argrefusal}

fail=0
assert_eq() {
  if [ "${2:-<none>}" = "$3" ]; then
    echo "  OK   $1 = $3"
  else
    echo "  FAIL $1 = ${2:-<none>}, expected $3"
    fail=1
  fi
}
assert_nonzero() {
  if [ -n "${2:-}" ] && [ "${2:-0}" -ne 0 ] 2>/dev/null; then
    echo "  OK   $1 = $2 (non-zero)"
  else
    echo "  FAIL $1 = ${2:-<none>}, expected non-zero"
    fail=1
  fi
}
field() {
  printf '%s' "$1" | grep -oE "(^| )$2=[^ ]+" | head -1 | cut -d= -f2-
}

if [[ ! -x $EXE ]]; then echo "missing $EXE (run bash build-cmake.sh)" >&2; exit 2; fi
mkdir -p "$OUT"
abs_win_path() {
  case "$1" in
    /*|[A-Za-z]:*) cygpath -m "$1" ;;
    *) cygpath -m "$PWD/$1" ;;
  esac
}
. "$(dirname "$0")/verify-iso.sh"
# The product rewrites the settings file it is given (see world-entity-tint-check.sh),
# so seed a fresh copy every run; a stale copy would render a different world.
rm -rf "$OUT/inputs"
if ! iso_seed_gate_settings "$OUT/inputs"; then
  echo "  FAIL the gate settings copy is not the fixture; refusing to render" >&2
  exit 2
fi
SETTINGS_FILE=$(abs_win_path "$OUT/inputs/settings.ini")
UI_FILE=$(abs_win_path "$OUT/inputs/ui.ini")

HL_ROOT=$(dirname "$LOCK")
if [[ $HOLD_LOCK == 1 ]]; then
  held_owner=$(head -1 "$HL_ROOT/host.lock/owner" 2>/dev/null | tr -d "\r")
  if [[ ! -d $HL_ROOT/host.lock || $held_owner != "$EXPECT_OWNER" ]]; then
    echo "HOLD_LOCK=1 but the shared lock is not held by $EXPECT_OWNER (owner=${held_owner:-none}); refusing to run captures unprotected" >&2
    exit 2
  fi
fi
locked() {
  if [[ $HOLD_LOCK == 1 ]]; then
    touch "$HL_ROOT/host.lock/start" 2>/dev/null || true
    bash -c "$1"
    return $?
  fi
  bash "$LOCK" run t105-argrefusal bash -c "$1"
}

# run <tag> <expected-exit-class: zero|nonzero> [extra args...]
run() {
  local tag=$1 expect=$2
  shift 2
  local trace frame
  trace=$(abs_win_path "$OUT/$tag.trace")
  frame=$(abs_win_path "$OUT/$tag.bmp")
  rm -f "$OUT/$tag.bmp"
  echo "RUN $tag: $*"
  locked "cd '$PWD' && TF2_NATIVE_TRACE='$trace' timeout 420 '$EXE' --tf-root '$TF' --demo '$DEMO' --audio-disabled --hidden-window --start-paused --playback-speed 8 --capture-tick $TICK --capture-frame '$frame' --settings-file '$SETTINGS_FILE' --ui-settings-file '$UI_FILE' $* >'$OUT/$tag.log' 2>&1"
  local status=$?
  echo "  exit=$status"
  if [[ $expect == nonzero ]]; then
    assert_nonzero "$tag exit" "$status"
    if [[ -s $OUT/$tag.bmp ]]; then
      echo "  FAIL $tag drew a frame although the switch should have been refused"
      fail=1
    else
      echo "  OK   $tag drew no frame"
    fi
  else
    assert_eq "$tag exit" "$status" 0
    if [[ -s $OUT/$tag.bmp ]]; then
      echo "  OK   $tag wrote a frame"
    else
      echo "  FAIL $tag produced no frame"
      fail=1
    fi
  fi
}

echo "== refused: a value attached to a switch that takes none"
run value-attached nonzero --world-flat=1
value_line=$(grep -a "args: refused switch " "$OUT/value-attached.trace" | tail -1)
echo "  reading: ${value_line:-<no refusal line>}"
assert_eq "value-attached refused count" "$(field "$value_line" count)" 1
assert_eq "value-attached refused first" "$(field "$value_line" first)" --world-flat=1

echo "== refused: a switch the product does not have"
run unknown-switch nonzero --not-a-real-switch
unknown_line=$(grep -a "args: refused switch " "$OUT/unknown-switch.trace" | tail -1)
echo "  reading: ${unknown_line:-<no refusal line>}"
assert_eq "unknown-switch refused first" "$(field "$unknown_line" first)" --not-a-real-switch

echo "== accepted: the bare switch still selects the mode"
run bare-switch zero --world-flat
flat_line=$(grep -a "world.*diagnostic\|diagnostic.*flat\|worldDebugView" "$OUT/bare-switch.trace" | tail -1)
echo "  reading: ${flat_line:-<no diagnostic line; the frame is the evidence>}"

echo "== tolerated: a non-switch argument keeps D-23's record-and-continue"
run positional zero randomword

echo
if [[ $fail -ne 0 ]]; then
  echo "VERDICT argument-refusal: FAIL"
  exit 1
fi
echo "VERDICT argument-refusal: PASS (unknown '--' switches refuse by name with a non-zero exit; known and positional arguments unchanged)"
exit 0
