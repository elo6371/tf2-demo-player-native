#!/usr/bin/env bash
# world-entity-tint-check.sh -- T105: the gate for the double team tint.
#
# WHAT WAS WRONG
#   Every entity draw instance was given a team tint (native/src/main.cpp, draw
#   stage: team==2 -> (0.86,0.28,0.22), team==3 -> (0.28,0.52,0.86), else
#   (0.72,0.74,0.70)) and the world pixel shader multiplies vertex colour into the
#   texel (native_renderer_shaders.cpp:311
#       float3 albedo = lerp(colour.rgb, sampled * colour.rgb, colour.a);
#   with the entity pass carrying colour.a = 1). The skin family the material chain
#   resolves ALREADY bakes the team colour into the texture, so a painted model
#   drew texture x team colour and a Scout read as a solid red blob. Measured on
#   pl_upward_f12 tick 0: raw texel (131,41,46) drew as (113,11,10) =
#   (0.863, 0.268, 0.217) -- the tint, to three digits.
#
# THE FIX
#   A model with painted slots keeps the team tint only as the UNTEXTURED
#   fallback: the tint is white when the model's cache key has painted slots
#   (entityPaintedSlotCountByKey, filled by the paint stage and the attach site).
#
# WHAT THIS GATE ASSERTS (both halves -- a counter alone can be right while the
# pixels stay wrong, and pixels alone cannot say which path ran)
#   PART ONE -- counters:
#     normal run     : 'gpu: entity tint white=N fallback=0 mutation=0', N > 0
#     mutation run   : 'gpu: entity tint white=0 fallback=N mutation=1', N > 0
#   PART TWO -- pixels, every tint-touched pixel:
#     the mask is the NEGATIVE CONTROL's own population: a pixel whose colour
#     changes between the normal frame and the --entity-tint-mutation frame is a
#     pixel the team tint reached. That is measured, needs no hand-drawn box, and
#     cannot go vacuous (a product that tinted both frames would collapse the
#     mask, which the --min-mask guard turns into a FAIL). Every masked pixel of
#     the NORMAL frame must equal the RAW-TEXEL frame (--world-texel: sampled
#     texture, no tint, no lightmap) within TOL per channel; the mutation frame
#     must NOT -- that is the control, and if it stays green nothing is measured.
#     The --world-entity-coverage frame is captured too but only PRINTED: measured
#     on pl_upward_f12 tick 0 its sentinel holds 688,388 of 860,784 pixels because
#     the entity shader also draws the map's static props, and 5,506 of those
#     differ from the raw texel by up to 144 for fog, not for the tint. Asserting
#     equality over that population is red on a correct product.
#
# RUN
#   bash /d/TF2_Native_Worktrees/_shared/hostlock.sh run t105-worldtint \
#     bash world-entity-tint-check.sh
#   Already inside the shared lock? pass HOLD_LOCK=1 and name the owner in
#   EXPECT_OWNER (default t105-worldtint).
#
# COST: four product launches, one lock acquisition each (unless HOLD_LOCK=1).
set -uo pipefail
# W1 (2026-11-23): every product launch in this script runs at 1280x720, the size
# its pixel pins were measured at. The product reads this when --window-size is
# absent (an explicit --window-size still wins); before W1 1280x720 was simply the
# default, and W1 moves the default to 1920x1080. tools/window-pin-check.sh checks it.
export TF2_NATIVE_WINDOW_SIZE="${TF2_NATIVE_WINDOW_SIZE:-1280x720}"
cd "$(dirname "$0")"

EXE=native/build-nmake/tf2_demo_native.exe
TF="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
DEMO="E:/demo测试/pl_upward_f12/STV/a9630c6557468b24f0b8c2e7dab38ed8_match-20261030-2145-pl_upward_f12.dem"
TICK=${TINT_TICK:-0}
TOL=${TINT_TOL:-2}
OUT=${TINT_OUT:-.scratch/world-entity-tint}
LOCK=${HOSTLOCK:-/d/TF2_Native_Worktrees/_shared/hostlock.sh}
HOLD_LOCK=${HOLD_LOCK:-0}
EXPECT_OWNER=${EXPECT_OWNER:-t105-worldtint}

fail=0
assert_eq() {
  if [ "${2:-<none>}" = "$3" ]; then
    echo "  OK   $1 = $3"
  else
    echo "  FAIL $1 = ${2:-<none>}, expected $3"
    fail=1
  fi
}
assert_gt() {
  if [ -n "${2:-}" ] && [ "${2:-0}" -gt "$3" ] 2>/dev/null; then
    echo "  OK   $1 = $2 (> $3)"
  else
    echo "  FAIL $1 = ${2:-<none>}, expected > $3"
    fail=1
  fi
}
field() {
  printf '%s' "$1" | grep -oE "(^| )$2=[^ ]+" | head -1 | cut -d= -f2-
}

if [[ ! -x $EXE ]]; then echo "missing $EXE (run bash build-cmake.sh)" >&2; exit 2; fi
for input in "$TF" "$DEMO"; do
  if [[ ! -e $input ]]; then echo "missing input: $input" >&2; exit 2; fi
done
mkdir -p "$OUT"

abs_win_path() {
  case "$1" in
    /*|[A-Za-z]:*) cygpath -m "$1" ;;
    *) cygpath -m "$PWD/$1" ;;
  esac
}

. "$(dirname "$0")/verify-iso.sh"
# The PRODUCT REWRITES the settings file it is given (measured: the copy grew from
# the 44-line fixture to 129 lines after one run, sha256 206d2469... vs the
# fixture's 8a19760d...). A gate that reuses its OUT directory would therefore
# fail iso_assert_gate_settings on the second run and render the wrong world.
# Always seed fresh, and treat a failed seed as fatal instead of continuing.
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
  echo "HOLD_LOCK=1: captures run inside the held host lock (owner=$EXPECT_OWNER)"
fi

locked() {
  if [[ $HOLD_LOCK == 1 ]]; then
    touch "$HL_ROOT/host.lock/start" 2>/dev/null || true
    bash -c "$1"
    return $?
  fi
  bash "$LOCK" run t105-worldtint bash -c "$1"
}

# run <tag> [extra product args...]
run() {
  local tag=$1
  shift
  local trace frame csv
  trace=$(abs_win_path "$OUT/$tag.trace")
  frame=$(abs_win_path "$OUT/$tag.bmp")
  csv=$(abs_win_path "$OUT/$tag.csv")
  echo "RUN $tag: $*"
  locked "cd '$PWD' && TF2_NATIVE_TRACE='$trace' timeout 420 '$EXE' --tf-root '$TF' --demo '$DEMO' --audio-disabled --hidden-window --start-paused --playback-speed 8 --capture-tick $TICK --capture-frame '$frame' --metrics-file '$csv' --settings-file '$SETTINGS_FILE' --ui-settings-file '$UI_FILE' $* >'$OUT/$tag.log' 2>&1"
  local status=$?
  if [[ ! -s $OUT/$tag.bmp ]]; then
    echo "  FAIL $tag produced no frame" >&2
    fail=1
  fi
  return $status
}

echo "== PART ONE: the counters, from the product's own line"
run normal
# Match the SUMMARY line, not the per-key ledger rows that follow it
# ("gpu: entity tint fallback-key key=... instances=N"), which carry none of the
# white/fallback/mutation fields and made tail -1 read "<none>" on every assert.
normal_tint=$(grep -aE "gpu: entity tint white=" "$OUT/normal.trace" | tail -1)
echo "  reading: ${normal_tint:-<no entity tint line>}"
assert_gt "normal white instances" "$(field "$normal_tint" white)" 0
assert_eq "normal fallback instances" "$(field "$normal_tint" fallback)" 0
# Not drawn this frame (layer A deferred the mesh), so the tint cannot reach a
# pixel through it. Counted apart from fallback so the fallback number keeps
# meaning "a draw that happened and took the tint".
assert_gt "normal deferred instances" "$(field "$normal_tint" deferred)" 0
assert_eq "normal mutation flag" "$(field "$normal_tint" mutation)" 0

run mutation --entity-tint-mutation
mutation_tint=$(grep -aE "gpu: entity tint white=" "$OUT/mutation.trace" | tail -1)
echo "  reading: ${mutation_tint:-<no entity tint line>}"
assert_eq "mutation white instances" "$(field "$mutation_tint" white)" 0
assert_gt "mutation fallback instances" "$(field "$mutation_tint" fallback)" 0
assert_eq "mutation mutation flag" "$(field "$mutation_tint" mutation)" 1

echo "== PART TWO: the pixels, every masked pixel against the raw texel"
run texel --world-texel
run coverage --world-entity-coverage

if [[ -s $OUT/normal.bmp && -s $OUT/texel.bmp && -s $OUT/coverage.bmp && -s $OUT/mutation.bmp ]]; then
  pixels=$(python entity-tint-pixels.py "$OUT/normal.bmp" "$OUT/texel.bmp" "$OUT/mutation.bmp" "$OUT/coverage.bmp" --tol "$TOL" --min-mask 50000)
  pixel_status=$?
  echo "$pixels" | sed 's/^/  /'
  assert_gt "tint-touched pixels" "$(printf '%s' "$pixels" | grep -oE "^mask pixels=[0-9]+" | cut -d= -f2)" 50000
  normal_line=$(printf '%s' "$pixels" | grep -a "^normal-vs-texel ")
  assert_gt "normal pixels within tol" "$(field "$normal_line" withinTol)" 50000
  assert_eq "normal pixels over tol" "$(field "$normal_line" exceed)" 0
  mutation_line=$(printf '%s' "$pixels" | grep -a "^mutation-vs-texel ")
  assert_gt "mutation control pixels over tol" "$(field "$mutation_line" exceed)" 0
  if [[ $pixel_status -ne 0 ]]; then fail=1; fi
else
  echo "  FAIL a frame for the pixel comparison is missing"
  fail=1
fi

echo
if [[ $fail -ne 0 ]]; then
  echo "VERDICT world-entity-tint: FAIL"
  exit 1
fi
echo "VERDICT world-entity-tint: PASS (white tint on every painted model, pixels equal raw texel, mutation control red)"
exit 0
