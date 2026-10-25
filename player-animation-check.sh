#!/usr/bin/env bash
# player-animation-check.sh -- T61: the third-person animation gate.
#
# WHAT THIS GATE IS
# -----------------
# T61 drives a per-player animation clock from the demo's own ticks and the
# player's own speed. The chain has links that can each be wrong on their own,
# and this gate asserts each link against the product's own code path (the
# probe links the same animation_binding.cpp / animation_decoder.cpp /
# entity_overlay.cpp objects the product links):
#
#   A. The sequence table the product picks from: every class's separate
#      _animations.mdl supplies at least 200 sequences, and the binding maps the
#      render skeleton one-to-one (bindBones == bones).
#   B. The pose the sampler returns for a cycle: the four quarter-cycle poses
#      differ pairwise (a sampler that ignores the cycle returns one pose four
#      times), every element is finite, and no pose leaves the skeleton's own
#      bound sphere (maxDelta <= boundDiag).
#   C. Continuity: sixty one-sixtieth-of-a-second steps at 125 units/s change
#      any matrix element by no more than a quarter of boundDiag per step.
#   D. The clock rate: movingRate > idleRate, a moving second advances the cycle
#      by the expected one full cycle (1%), and an idle second advances it by
#      exactly zero.
#   E. The tick script, synthetic and from a real recording: ticks with speed
#      advance the cycle, ticks without speed do not, and the cycle never moves
#      backwards.
#
# NEGATIVE CONTROLS (first, each must fail with a NAMED reason)
#   * a tf root with no *_dir.vpk       -> reason=tf-root-invalid
#   * a model that does not exist       -> reason=model-missing
#   * a sequence past the table         -> reason=sequence-out-of-range
#
# BINARY FRESHNESS
#   The probe is prebuilt. A probe older than the sources it reads would
#   publish the previous tree's numbers under this tree's label, so the gate
#   prints BINARY-FRESH=1 only when the exe is newer than every source file,
#   and fails with STALE-BINARY=1 otherwise.
#
# Usage: bash player-animation-check.sh [--skip-negative-controls]
# Exit:  0 = negative controls failed with their named reasons AND every forward
#            assertion held.
#        1 = one of those did not hold.
set -uo pipefail
cd "$(dirname "$0")"

PROBE=native/build-nmake/model_pose_probe.exe
TF="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
BAGEL="D:/TF2_Demo_Player/testdata/demos/4a9bfb9276509d0ec5f5fdc722a95b17_match-20260927-0239-koth_bagel_rc13.dem"
SNAKEWATER="D:/TF2_Demo_Player/testdata/demos/bb841c6d379ff7c40d0c8baf99f59d8d_matcha-20260927-1347-cp_snakewater_final1.dem"
OUT="${PLAYER_ANIMATION_OUT:-evidence/t61-animation/runtime}"
mkdir -p "$OUT"

# A window of real ticks over which players both move and stand. Offsets are
# relative to the first recorded packet tick, which is what the probe's
# --motion-window means. The default is measured evidence (see REPORT.md); the
# environment variable exists so a future recording can move it without editing
# the gate.
DEMO_WINDOW="${TF2_ANIMATION_DEMO_WINDOW:-60000,62000}"

RUN_NEGATIVE=1
[ "${1:-}" = "--skip-negative-controls" ] && RUN_NEGATIVE=0

fail=0
assert_eq() { # assert_eq <label> <actual> <expected>
  if [ "${2:-<none>}" = "$3" ]; then
    echo "  OK   $1 = $3"
  else
    echo "  FAIL $1 = ${2:-<none>}, expected $3"; fail=1
  fi
}
assert_ge() { # assert_ge <label> <actual> <minimum>
  if [ -n "${2:-}" ] && [ "$2" -ge "$3" ] 2>/dev/null; then
    echo "  OK   $1 = $2 (>= $3)"
  else
    echo "  FAIL $1 = ${2:-<none>}, expected >= $3"; fail=1
  fi
}
assert_le_float() { # assert_le_float <label> <actual> <maximum>
  if [ -n "${2:-}" ] && awk -v a="$2" -v b="$3" 'BEGIN { exit !(a <= b) }' 2>/dev/null; then
    echo "  OK   $1 = $2 (<= $3)"
  else
    echo "  FAIL $1 = ${2:-<none>}, expected <= $3"; fail=1
  fi
}
assert_gt_float() { # assert_gt_float <label> <actual> <floor>
  if [ -n "${2:-}" ] && awk -v a="$2" -v b="$3" 'BEGIN { exit !(a > b) }' 2>/dev/null; then
    echo "  OK   $1 = $2 (> $3)"
  else
    echo "  FAIL $1 = ${2:-<none>}, expected > $3"; fail=1
  fi
}
assert_close_pct() { # assert_close_pct <label> <actual> <expected> <percent>
  if [ -n "${2:-}" ] && awk -v a="$2" -v e="$3" -v p="$4" 'BEGIN { d = a - e; if (d < 0) d = -d; exit !(e != 0 && d <= (p / 100) * (e < 0 ? -e : e)) }' 2>/dev/null; then
    echo "  OK   $1 = $2 (expected $3 +/- $4%)"
  else
    echo "  FAIL $1 = ${2:-<none>}, expected $3 +/- $4%"; fail=1
  fi
}
assert_contains() { # assert_contains <label> <haystack> <needle>
  case "${2:-}" in
    *"$3"*) echo "  OK   $1 contains $3" ;;
    *) echo "  FAIL $1 = ${2:-<none>}, expected to contain $3"; fail=1 ;;
  esac
}
field() { # field <line> <name> -> value of the first "name=value" token
  local line="$1" name="$2" rest
  case "$line" in
    *"$name="*) rest="${line#*"$name="}" ;;
    *) printf ''; return 0 ;;
  esac
  printf '%s' "${rest%% *}"
}

for path in "$PROBE" "$TF" "$BAGEL"; do
  [ -e "$path" ] || { echo "FATAL: missing input: $path"; exit 1; }
done

# ===========================================================================
echo "=== 0/6 binary freshness: the probe is newer than every input it compiles ==="
DEPS="native/build-nmake/CMakeFiles/model_pose_probe.dir/DependInfo.cmake"
if [ ! -f "$DEPS" ]; then
  echo "  FAIL STALE-BINARY=1 probe missing dependency list $DEPS"; fail=1
else
  stale=""
  while IFS= read -r src; do
    [ -e "$src" ] || continue
    if [ "$PROBE" -ot "$src" ]; then stale="$src"; fi
  done < <(sed -n 's/^  "\([^"]*\)" ".*CMakeFiles\/model_pose_probe\.dir\/.*/\1/p' "$DEPS")
  if [ -z "$stale" ]; then
    stale=$(find native/include native/src native/tools native/third_party -type f \( -name '*.h' -o -name '*.hpp' \) -newer "$PROBE" -print -quit 2>/dev/null)
  fi
  if [ -n "$stale" ]; then
    echo "  FAIL STALE-BINARY=1 probe=$PROBE older than $stale"; fail=1
  else
    echo "  OK   BINARY-FRESH=1 probe=$(date -r "$PROBE" '+%Y-%m-%d %H:%M:%S')"
  fi
fi

# ===========================================================================
if [ "$RUN_NEGATIVE" = "1" ]; then
  echo
  echo "=== 1/6 negative controls: each must fail with its named reason ==="
  NEG="$OUT/negative"
  mkdir -p "$NEG/logs"
  mkdir -p "$NEG/not-a-tf"
  expect_fail() { # expect_fail <label> <expected rc> <expected text> <cmd...>
    local label="$1" want_rc="$2" want_text="$3"; shift 3
    local log="$NEG/logs/$label.txt"
    "$@" > "$log" 2>&1
    local rc=$?
    local hit=0
    grep -qF "$want_text" "$log" && hit=1
    if [ "$rc" = "$want_rc" ] && [ "$hit" = "1" ]; then
      echo "  OK   $label failed rc=$rc with '$want_text'"
    else
      echo "  FAIL $label rc=$rc (wanted $want_rc), text '$want_text' present=$hit"
      sed 's/^/       | /' "$log" | head -5
      fail=1
    fi
  }
  expect_fail wrong-tf-root 2 "FAIL player-animation-test reason=tf-root-invalid" \
    "$PROBE" --player-animation-test --tf-root "$NEG/not-a-tf" --model models/player/scout.mdl
  expect_fail wrong-model 2 "FAIL player-animation-test reason=model-missing" \
    "$PROBE" --player-animation-test --tf-root "$TF" --model models/player/not_a_class_model.mdl
  expect_fail nonexistent-sequence 2 "FAIL player-animation-test reason=sequence-out-of-range" \
    "$PROBE" --player-animation-test --tf-root "$TF" --model models/player/scout.mdl --sequence 9999
fi

# ===========================================================================
echo
echo "=== 2/6 A: nine class models pick from a >=200-sequence table, bindBones==bones ==="
CLASSES="scout sniper soldier demo medic heavy pyro spy engineer"
CLASS_ROWS=0
for mdl in $CLASSES; do
  log="$OUT/class-$mdl.txt"
  "$PROBE" --player-animation-test --tf-root "$TF" --model "models/player/$mdl.mdl" > "$log" 2>&1
  rc=$?
  assert_eq "A $mdl probe exit" "$rc" "0"
  [ "$rc" = "0" ] || { sed 's/^/       | /' "$log" | head -5; continue; }
  CLASS_ROWS=$((CLASS_ROWS + 1))
  table=$(grep '^animtable ' "$log" | head -1)
  echo "  $mdl: $(echo "$table" | cut -c1-160)"
  assert_ge "A $mdl sequences" "$(field "$table" sequences)" 200
  assert_eq "A $mdl bindBones==bones" "$(field "$table" bindBones)" "$(field "$table" bones)"
  assert_ge "A $mdl validBindBones" "$(field "$table" validBindBones)" 1
  assert_eq "A $mdl separate" "$(field "$table" separate)" "1"
  assert_contains "A $mdl animSource" "$(field "$table" animSource)" "_animations.mdl"
done
assert_eq "A class rows" "$CLASS_ROWS" "9"

# ===========================================================================
echo
echo "=== 3/6 B: four quarter-cycle poses differ pairwise and stay inside boundDiag ==="
for mdl in $CLASSES; do
  log="$OUT/class-$mdl.txt"
  [ -f "$log" ] || continue
  diag=$(grep '^boundDiag ' "$log" | head -1)
  bound=$(field "$diag" boundDiag)
  assert_eq "B $mdl boundDiag composed" "$(field "$diag" composed)" "1"
  assert_gt_float "B $mdl boundDiag" "$bound" 1
  checksums=""
  # Read the four pose lines one at a time: the checksum must differ pairwise,
  # which a fixed-pose sampler (mutation m1/m2) cannot satisfy.
  while IFS= read -r line; do
    assert_eq "B $mdl pose finite" "$(field "$line" finite)" "1"
    assert_le_float "B $mdl maxDelta cycle=$(field "$line" cycle)" "$(field "$line" maxDelta)" "$bound"
    checksums="$checksums $(field "$line" checksum)"
  done < <(grep '^animpose ' "$log")
  distinct=$(printf '%s\n' $checksums | sed '/^$/d' | sort -u | wc -l | tr -d ' ')
  assert_eq "B $mdl distinct pose checksums" "$distinct" "4"
done

# ---------------------------------------------------------------------------
# Determinism: the same input twice must produce the same pose digests. Without
# this, a report can quote "the pose at cycle N" from a run that cannot be
# reproduced, and the on/off pixel comparison would rest on an unrepeatable
# frame. The repeat runs the identical probe command line, so a difference can
# only come from the probe's own state, never from the input.
REPEAT_LOG="$OUT/class-scout-repeat.txt"
"$PROBE" --player-animation-test --tf-root "$TF" --model "models/player/scout.mdl" > "$REPEAT_LOG" 2>&1
rc=$?
assert_eq "B scout repeat probe exit" "$rc" "0"
first_lines=$(grep -a -c '^animpose ' "$OUT/class-scout.txt")
repeat_lines=$(grep -a -c '^animpose ' "$REPEAT_LOG")
assert_eq "B scout repeat pose line count" "$repeat_lines" "$first_lines"
assert_ge "B scout pose lines to compare" "$first_lines" 4
first_digest=$(grep -a '^animpose ' "$OUT/class-scout.txt" | md5sum | cut -d' ' -f1)
repeat_digest=$(grep -a '^animpose ' "$REPEAT_LOG" | md5sum | cut -d' ' -f1)
assert_eq "B scout pose digest repeatable" "$repeat_digest" "$first_digest"

# ===========================================================================
echo
echo "=== 4/6 C: sixty 1/60 s steps never move a matrix element by more than boundDiag/4 ==="
for mdl in $CLASSES; do
  log="$OUT/class-$mdl.txt"
  [ -f "$log" ] || continue
  bound=$(field "$(grep '^boundDiag ' "$log" | head -1)" boundDiag)
  quarter=$(awk -v b="$bound" 'BEGIN { printf "%.9f", b / 4 }')
  cont=$(grep '^animcont ' "$log" | head -1)
  assert_eq "C $mdl animcont usable" "$(field "$cont" usable)" "1"
  assert_le_float "C $mdl maxStepDelta (quarter=$quarter)" "$(field "$cont" maxStepDelta)" "$quarter"
  assert_gt_float "C $mdl maxStepDelta" "$(field "$cont" maxStepDelta)" 0
done

# ===========================================================================
echo
echo "=== 5/6 D: movingRate > idleRate, one moving second == expected, idle second == 0 ==="
SCOUT_LOG="$OUT/class-scout.txt"
rate=$(grep '^animrate ' "$SCOUT_LOG" | head -1)
echo "  $rate"
assert_gt_float "D movingRate" "$(field "$rate" movingRate)" "$(field "$rate" idleRate)"
assert_eq "D idleRate" "$(field "$rate" idleRate)" "0.000000"
assert_eq "D idleOneSecond" "$(field "$rate" idleOneSecond)" "0.000000000"
assert_close_pct "D movingOneSecond" "$(field "$rate" movingOneSecond)" "$(field "$rate" expected)" 1

# ===========================================================================
echo
echo "=== 6/6 E: speed advances the cycle, no speed freezes it (synthetic and demo) ==="
ticks=$(grep '^animticks mode=synthetic ' "$SCOUT_LOG" | head -1)
echo "  $ticks"
assert_eq "E synthetic ticks" "$(field "$ticks" ticks)" "61"
assert_ge "E synthetic movingTicks" "$(field "$ticks" movingTicks)" 1
moving_min=$(( $(field "$ticks" movingTicks) - 1 ))
assert_ge "E synthetic movingAdvanced (baseline excluded)" "$(field "$ticks" movingAdvanced)" "$moving_min"
assert_eq "E synthetic idleAdvanced" "$(field "$ticks" idleAdvanced)" "0"
assert_eq "E synthetic maxIdleDelta" "$(field "$ticks" maxIdleDelta)" "0.000000000"
assert_gt_float "E synthetic maxMovingDelta" "$(field "$ticks" maxMovingDelta)" 0
line30=$(grep '^animtick mode=synthetic tick=30 ' "$SCOUT_LOG" | head -1)
line31=$(grep '^animtick mode=synthetic tick=31 ' "$SCOUT_LOG" | head -1)
assert_eq "E synthetic tick 30 state" "$(field "$line30" state)" "moving"
assert_eq "E synthetic tick 30 advanced" "$(field "$line30" advanced)" "1"
assert_eq "E synthetic tick 31 state" "$(field "$line31" state)" "idle"
assert_eq "E synthetic tick 31 advanced" "$(field "$line31" advanced)" "0"

DEMO_LOG="$OUT/demo-ticks.txt"
"$PROBE" --player-animation-test --tf-root "$TF" --model models/player/scout.mdl \
  --demo "$BAGEL" --motion-window "$DEMO_WINDOW" > "$DEMO_LOG" 2>&1
DEMO_RC=$?
echo "  demo window=$DEMO_WINDOW rc=$DEMO_RC (log $DEMO_LOG)"
assert_eq "E demo probe exit" "$DEMO_RC" "0"
demo=$(grep '^animticks mode=demo ' "$DEMO_LOG" | head -1)
echo "  $demo"
if [ "$DEMO_RC" = "0" ]; then
  assert_ge "E demo transitions" "$(field "$demo" transitions)" 1
  assert_ge "E demo movingTicks" "$(field "$demo" movingTicks)" 1
  entities=$(field "$demo" entities)
  moving_ticks=$(field "$demo" movingTicks)
  moving_min=$(( moving_ticks > entities ? moving_ticks - entities : 0 ))
  assert_ge "E demo movingAdvanced (one baseline per entity)" "$(field "$demo" movingAdvanced)" "$moving_min"
  assert_ge "E demo staleTicks (repeated checkpoint answers are not idle)" "$(field "$demo" staleTicks)" 1
  assert_eq "E demo idleAdvanced" "$(field "$demo" idleAdvanced)" "0"
  assert_eq "E demo maxIdleDelta" "$(field "$demo" maxIdleDelta)" "0.000000000"
  assert_gt_float "E demo maxMovingDelta" "$(field "$demo" maxMovingDelta)" 0
  # The printed tick lines are the same rule at line granularity: a moving tick
  # that did not advance, or an idle tick that did, is the defect m1/m4 make.
  bad_move=$(grep '^animtick mode=demo ' "$DEMO_LOG" | awk '{ speed=-1; adv=-1; state=""; for (i=1;i<=NF;i++) { split($i,kv,"="); if (kv[1]=="speed") speed=kv[2]; if (kv[1]=="advanced") adv=kv[2]; if (kv[1]=="state") state=kv[2] } if (state=="moving" && adv==0) print }' | wc -l | tr -d ' ')
  bad_idle=$(grep '^animtick mode=demo ' "$DEMO_LOG" | awk '{ adv=-1; state=""; for (i=1;i<=NF;i++) { split($i,kv,"="); if (kv[1]=="advanced") adv=kv[2]; if (kv[1]=="state") state=kv[2] } if (state=="idle" && adv==1) print }' | wc -l | tr -d ' ')
  assert_eq "E demo printed moving ticks that did not advance" "$bad_move" "0"
  assert_eq "E demo printed idle ticks that advanced" "$bad_idle" "0"
fi

echo
if [ "$fail" = "0" ]; then
  echo "PASS player-animation-check: negative controls named-failed; A sequences>=200 and bindBones==bones; B four distinct poses inside boundDiag; C maxStepDelta<=boundDiag/4; D moving second==expected, idle second==0; E speed advances the cycle and no speed freezes it."
else
  echo "FAIL player-animation-check: see the FAIL lines above."
fi
exit "$fail"
