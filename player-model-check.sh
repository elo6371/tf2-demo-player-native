#!/usr/bin/env bash
# player-model-check.sh -- T60: the player-model gate.
#
# WHAT THIS GATE IS NOW
# ---------------------
# T57 published three defects as readings and asserted only that the subject
# (nine class models) existed. T60 fixes them, so the readings become
# assertions, and the four negative controls that prove the reading path can go
# red run FIRST -- a forward assertion is only worth its green if the probe
# behind it has been shown to fail with a named reason.
#
#   A. LOD0 is not truncated: the vertex budget is derived from each model's own
#      LOD0 render-index count, so lod0Budget == lod0RenderIndices,
#      lod0Built == lod0RenderIndices, lod0Lost == 0 and lod0Truncated == 0 for
#      all nine classes.
#   B. Paint is per material slot and per skin family: every LOD0 material slot
#      resolves for red (family 0) and blu (family 1), paintedSlots is
#      2 x lod0MaterialSlots, the dominant body run is not a *_head texture, and
#      productPaintedSlots is the sum over the classes -- not the single slot-0
#      paint T57 measured.
#   C. The animation source is the separate <class>_animations.mdl: every class
#      reports separateAnimSource=1, a sequence count in the hundreds, and a path
#      ending in _animations.mdl (not _user_animations.mdl).
#
# NEGATIVE CONTROLS (first, each must fail with a NAMED reason)
#   * a tf root with no *_dir.vpk         -> "tf-root is not a valid AssetRoot"
#   * a file that is not a demo           -> "the demo header did not parse"
#   * a class table with eight rows       -> "class-table entries=8 expected=9"
#   * a class table naming a missing mdl  -> "class models did not resolve"
#
# BINARY FRESHNESS
#   The probes are prebuilt. A probe older than the sources it reads would
#   publish the previous tree's numbers under this tree's label, so the gate
#   prints BINARY-FRESH=1 only when each exe is newer than every source file,
#   and fails with STALE-BINARY=1 otherwise.
#
# Usage: bash player-model-check.sh [--skip-negative-controls]
# Exit:  0 = negative controls failed with their named reasons AND every forward
#            assertion held.
#        1 = one of those did not hold.
set -uo pipefail
cd "$(dirname "$0")"

PROBE=native/build-nmake/entity_model_probe.exe
OVERLAY=native/build-nmake/entity_overlay_probe.exe
ROBUST=native/build-nmake/model_robustness_probe.exe
TF="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
BAGEL="D:/TF2_Demo_Player/testdata/demos/4a9bfb9276509d0ec5f5fdc722a95b17_match-20260927-0239-koth_bagel_rc13.dem"
SNAKEWATER="D:/TF2_Demo_Player/testdata/demos/bb841c6d379ff7c40d0c8baf99f59d8d_matcha-20260927-1347-cp_snakewater_final1.dem"
OUT="${PLAYER_MODEL_OUT:-evidence/t57-player-model/runtime}"
mkdir -p "$OUT"

# The bagel recording is 112396 ticks long; six samples spread over 5%..95% of
# its length, plus one tick already measured to carry live players (64230 on
# snakewater). Offsets are relative to the first recorded packet tick, which is
# what entity_overlay_probe --ticks means.
BAGEL_TICKS="5000,20000,40000,60000,80000,100000"
SNAKEWATER_TICKS="64230"

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
assert_contains() { # assert_contains <label> <haystack> <needle>
  case "${2:-}" in
    *"$3"*) echo "  OK   $1 contains $3" ;;
    *) echo "  FAIL $1 = ${2:-<none>}, expected to contain $3"; fail=1 ;;
  esac
}
assert_not_contains() { # assert_not_contains <label> <haystack> <needle>
  case "${2:-}" in
    *"$3"*) echo "  FAIL $1 = ${2:-<none>}, must not contain $3"; fail=1 ;;
    *) echo "  OK   $1 does not contain $3" ;;
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

for path in "$PROBE" "$OVERLAY" "$ROBUST" "$TF" "$BAGEL" "$SNAKEWATER"; do
  [ -e "$path" ] || { echo "FATAL: missing input: $path"; exit 1; }
done

# ===========================================================================
echo "=== 0/4 binary freshness: each probe is newer than every input it compiles ==="
# The dependency set is derived from CMake's own DependInfo for the target, not
# hand-written: native/src holds translation units that only some tools compile
# (entity_overlay_probe does not compile model_loader.cpp), so comparing every
# exe against every shared source reported a stale binary for a file the exe
# never reads. Header dependencies are untracked by the build, so
# build-target.sh touches every source when any header under native/include,
# native/src, native/tools or native/third_party is newer than .headers-stamp;
# a fresh binary must therefore also be newer than all of those headers.
for pair in "probe:$PROBE:entity_model_probe" "overlay:$OVERLAY:entity_overlay_probe" "robust:$ROBUST:model_robustness_probe"; do
  label="${pair%%:*}"; rest="${pair#*:}"; exe="${rest%%:*}"; target="${rest#*:}"
  deps="native/build-nmake/CMakeFiles/$target.dir/DependInfo.cmake"
  if [ ! -f "$deps" ]; then
    echo "  FAIL STALE-BINARY=1 $label missing dependency list $deps"; fail=1; continue
  fi
  stale=""
  while IFS= read -r src; do
    [ -e "$src" ] || continue
    if [ "$exe" -ot "$src" ]; then stale="$src"; fi
  done < <(sed -n 's/^  "\(.*\)" "CMakeFiles.*/\1/p' "$deps")
  if [ -z "$stale" ]; then
    stale=$(find native/include native/src native/tools native/third_party -type f \( -name '*.h' -o -name '*.hpp' \) -newer "$exe" -print -quit 2>/dev/null)
  fi
  if [ -n "$stale" ]; then
    echo "  FAIL STALE-BINARY=1 $label exe=$exe older than $stale"; fail=1
  else
    echo "  OK   BINARY-FRESH=1 $label exe=$(date -r "$exe" '+%Y-%m-%d %H:%M:%S')"
  fi
done

# ===========================================================================
if [ "$RUN_NEGATIVE" = "1" ]; then
  echo
  echo "=== 1/4 negative controls: each must fail with its named reason ==="
  NEG="$OUT/negative"
  mkdir -p "$NEG/logs"
  # (a) a directory that is not a tf root: no *_dir.vpk, so AssetRoot is invalid.
  mkdir -p "$NEG/not-a-tf"
  # (b) a file that is not a demo.
  printf 'this is not a demo\n' > "$NEG/not-a-demo.dem"
  # (c) a class table with eight rows.
  cat > "$NEG/class-table-8.txt" <<'TABLE'
1 models/player/scout.mdl
2 models/player/sniper.mdl
3 models/player/soldier.mdl
4 models/player/demo.mdl
5 models/player/medic.mdl
6 models/player/heavy.mdl
7 models/player/pyro.mdl
8 models/player/spy.mdl
TABLE
  # (d) a class table that names a model which does not exist: the audit must go
  #     red on the subject, which is the assertion this gate really rests on.
  cat > "$NEG/class-table-missing-model.txt" <<'TABLE'
1 models/player/not_a_class_model.mdl
2 models/player/sniper.mdl
3 models/player/soldier.mdl
4 models/player/demo.mdl
5 models/player/medic.mdl
6 models/player/heavy.mdl
7 models/player/pyro.mdl
8 models/player/spy.mdl
9 models/player/engineer.mdl
TABLE

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
  expect_fail wrong-tf-root 2 "player-model-audit FAIL tf-root is not a valid AssetRoot" \
    "$PROBE" --tf-root "$NEG/not-a-tf" --player-model-audit
  expect_fail wrong-demo 2 "the demo header did not parse" \
    "$OVERLAY" --demo "$NEG/not-a-demo.dem" --ticks 0
  expect_fail class-table-8 2 "class-table entries=8 expected=9" \
    "$PROBE" --tf-root "$TF" --player-model-audit --class-table "$NEG/class-table-8.txt"
  expect_fail class-table-missing-model 1 "class models did not resolve" \
    "$PROBE" --tf-root "$TF" --player-model-audit --class-table "$NEG/class-table-missing-model.txt"
fi

# ===========================================================================
echo
echo "=== 2/4 nine class models: A LOD0 complete, B paint per slot and family ==="
AUDIT="$OUT/player-model-audit.txt"
"$PROBE" --tf-root "$TF" --player-model-audit > "$AUDIT" 2>&1
AUDIT_RC=$?
echo "  audit rc=$AUDIT_RC (log $AUDIT)"
ROWS=$(grep -c '^player-model class=' "$AUDIT" 2>/dev/null || echo 0)
assert_eq "class rows" "$ROWS" "9"
SUMMARY=$(grep '^player-model-audit summary ' "$AUDIT" | head -1)
assert_eq "audit exit" "$AUDIT_RC" "0"
for f in classes resolved renderable companionsComplete lodCountKnown; do
  assert_eq "summary $f" "$(field "$SUMMARY" "$f")" "9"
done
assert_ge "vvdLodsMin" "$(field "$SUMMARY" vvdLodsMin)" 1
assert_ge "vtxLodsMin" "$(field "$SUMMARY" vtxLodsMin)" 1

# --- A: no truncation, budget derived from the model -----------------------
echo "  --- A: LOD0 budget is the model's own render-index count ---"
assert_eq "A summary lod0Truncated" "$(field "$SUMMARY" lod0Truncated)" "0"
assert_eq "A summary lod0LostTotal" "$(field "$SUMMARY" lod0LostTotal)" "0"
assert_eq "A summary lod0SkippedTotal" "$(field "$SUMMARY" lod0SkippedTotal)" "0"
PAINTED_SUM=0
while read -r line; do
  c=$(field "$line" class)
  assert_eq "A $c lod0Budget" "$(field "$line" lod0Budget)" "$(field "$line" lod0RenderIndices)"
  assert_eq "A $c lod0Built" "$(field "$line" lod0Built)" "$(field "$line" lod0RenderIndices)"
  assert_eq "A $c lod0Lost" "$(field "$line" lod0Lost)" "0"
  assert_eq "A $c lod0Skipped" "$(field "$line" lod0Skipped)" "0"
done < <(grep '^player-model class=' "$AUDIT")

# --- B: per-slot, per-family paint ----------------------------------------
echo "  --- B: every LOD0 slot painted for red (0) and blu (1) ---"
assert_eq "B summary classesWithPaint" "$(field "$SUMMARY" classesWithPaint)" "9"
assert_eq "B summary paintedFamiliesMin" "$(field "$SUMMARY" paintedFamiliesMin)" "2"
assert_ge "B summary distinctPaintedStemsMin" "$(field "$SUMMARY" distinctPaintedStemsMin)" 1
assert_eq "B summary headOnBodyClasses" "$(field "$SUMMARY" headOnBodyClasses)" "0"
while read -r line; do
  c=$(field "$line" class)
  slots=$(field "$line" lod0MaterialSlots)
  assert_ge "B $c lod0MaterialSlots" "$slots" 1
  assert_eq "B $c paintedSlots" "$(field "$line" paintedSlots)" "$(( slots * 2 ))"
  assert_eq "B $c paintedFamilies" "$(field "$line" paintedFamilies)" "2"
  assert_eq "B $c headOnBody" "$(field "$line" headOnBody)" "0"
  assert_ge "B $c redBluDistinctSlots" "$(field "$line" redBluDistinctSlots)" 1
  # The count that separates "one texture for the whole model" (the T57 defect:
  # slot 0 painted everywhere) from a per-run bind: one texture per material slot.
  assert_eq "B $c distinctPaintedStems" "$(field "$line" distinctPaintedStems)" "$slots"
  PAINTED_SUM=$(( PAINTED_SUM + $(field "$line" paintedSlots) ))
done < <(grep '^player-model class=' "$AUDIT")
assert_eq "B summary productPaintedSlots" "$(field "$SUMMARY" productPaintedSlots)" "$PAINTED_SUM"
assert_ge "B summary productPaintedSlots" "$(field "$SUMMARY" productPaintedSlots)" 18

# ===========================================================================
echo
echo "=== 3/4 C: each class's animation source is the separate _animations.mdl ==="
ANIM="$OUT/player-model-anim.txt"
: > "$ANIM"
ANIM_ROWS=0
for mdl in scout sniper soldier demo medic heavy pyro spy engineer; do
  line=$("$ROBUST" --tf-root "$TF" --model "models/player/$mdl.mdl" 2>&1 | grep '^robust model=' | head -1)
  [ -n "$line" ] || { echo "  FAIL no robust line for models/player/$mdl.mdl"; fail=1; continue; }
  ANIM_ROWS=$((ANIM_ROWS + 1))
  echo "$line" >> "$ANIM"
  printf '  %-9s bones=%-4s animInput=%-14s animPath=%-46s animSeqs=%-4s bindBones=%s\n' \
    "$mdl" "$(field "$line" bones)" "$(field "$line" animationInput)" \
    "$(field "$line" animPath)" "$(field "$line" animSeqs)" "$(field "$line" bindBones)"
  assert_eq "C $mdl separateAnimSource" "$(field "$line" separateAnimSource)" "1"
  assert_ge "C $mdl animSeqs" "$(field "$line" animSeqs)" 200
  assert_contains "C $mdl animPath" "$(field "$line" animPath)" "_animations.mdl"
  assert_not_contains "C $mdl animPath" "$(field "$line" animPath)" "_user_animations.mdl"
  # The robust probe prints bindBones=<bones>/<materialized>; the second number
  # must equal the first, i.e. every bind-pose bone was materialized.
  assert_eq "C $mdl bindBones" "$(field "$line" bindBones)" "$(field "$line" bones)/$(field "$line" bones)"
done
assert_eq "animation rows" "$ANIM_ROWS" "9"

# ===========================================================================
echo
echo "=== 4/4 sampled player entities resolve to a model (bagel 6 ticks + snakewater 1) ==="
DEMO_LOG="$OUT/player-model-demo.txt"
: > "$DEMO_LOG"
"$OVERLAY" --demo "$BAGEL" --ticks "$BAGEL_TICKS" > "$OUT/player-model-demo-bagel.txt" 2>&1
BAGEL_RC=$?
cat "$OUT/player-model-demo-bagel.txt" >> "$DEMO_LOG"
"$OVERLAY" --demo "$SNAKEWATER" --ticks "$SNAKEWATER_TICKS" > "$OUT/player-model-demo-snakewater.txt" 2>&1
SW_RC=$?
cat "$OUT/player-model-demo-snakewater.txt" >> "$DEMO_LOG"
echo "  bagel rc=$BAGEL_RC snakewater rc=$SW_RC"
assert_eq "bagel probe exit" "$BAGEL_RC" "0"
assert_eq "snakewater probe exit" "$SW_RC" "0"
for tag in bagel snakewater; do
  line=$(grep '^demo models ' "$OUT/player-model-demo-$tag.txt" | head -1)
  echo "  $tag: ${line:-<no demo models line>}"
  assert_ge "$tag rows" "$(field "$line" rows)" 1
  assert_ge "$tag withModel" "$(field "$line" withModel)" 1
done

echo
if [ "$fail" = "0" ]; then
  echo "PASS player-model-check: negative controls named-failed; A lod0Truncated=0; B paintedSlots per slot x family; C separateAnimSource=1."
else
  echo "FAIL player-model-check: see the FAIL lines above."
fi
exit "$fail"
