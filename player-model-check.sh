#!/usr/bin/env bash
# player-model-check.sh -- T57: the nine class models exist, load, and every
# sampled player entity resolves to one. Defects are PUBLISHED, not judged.
#
# WHY THIS GATE IS AN AUDIT AND NOT A VERDICT
# -------------------------------------------
# The T57 round measured three defects in the player-model chain:
#   1. the 12000-vertex budget truncates every class model's LOD0
#      (model_loader.cpp buildBindPoseMeshLod0 breaks at the cap, so the tail of
#      the bodypart list is never built);
#   2. the world pass binds one material -- textureCandidates.front(), slot 0 --
#      to a model that declares 8..31 slots (main.cpp), so every class is painted
#      with its slot-0 texture regardless of team or bodypart;
#   3. on the recorded demos a player entity may carry no m_nModelIndex and no
#      usable m_iClass, in which case it can only become a marker.
# None of those is fixed by this round. A gate that ASSERTED "not truncated /
# not slot0 / every entity has a model" would therefore be red on a tree that is
# working exactly as intended, and the first response to a red gate is to weaken
# it. So the split is explicit:
#   * SUBJECT EXISTENCE is asserted. Nine class models must resolve, be
#     renderable with their companion set complete, report a LOD count, and the
#     demos must sample players whose model resolves. If the subject is missing
#     the gate is red, because then nothing below it was measured.
#   * THE THREE DEFECTS are published as named readings with their counts. The
#     T60 fix gate turns them into assertions, and at that point the mutations
#     that make them red already exist (this gate's negative controls prove the
#     reading path can go red).
#
# NEGATIVE CONTROLS
# -----------------
# A reading that cannot fail is not a reading. Four controls run by default and
# each must fail with a NAMED reason:
#   * a tf root with no *_dir.vpk         -> "tf-root is not a valid AssetRoot"
#   * a file that is not a demo           -> "the demo header did not parse"
#   * a class table with eight rows       -> "class-table entries=8 expected=9"
#   * a class table naming a missing mdl  -> "class models did not resolve"
# If a control fails for the wrong reason, or does not fail at all, the gate is
# red: that is the one assertion here that is about the gate itself.
#
# Usage: bash player-model-check.sh [--skip-negative-controls]
# Exit:  0 = every existence assertion held, every defect reading was produced,
#            and every negative control failed with its named reason.
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
echo "=== 1/4 nine class models resolve, are renderable, and report a LOD count ==="
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

echo
echo "  --- published defect readings (not assertions; T60 turns these into assertions) ---"
echo "  lod0Truncated=$(field "$SUMMARY" lod0Truncated) lod0LostTotal=$(field "$SUMMARY" lod0LostTotal)"
echo "  productPaintedSlots=$(field "$SUMMARY" productPaintedSlots) slot0Resolved=$(field "$SUMMARY" slot0Resolved)"
printf '  %-9s %-28s %-6s %-6s %-10s %-10s %-8s %-6s %-6s %-6s %s\n' class model bones seqs lod0Idx lod0Built lod0Lost slots skinRef skinFam slot0
grep '^player-model class=' "$AUDIT" | while read -r line; do
  printf '  %-9s %-28s %-6s %-6s %-10s %-10s %-8s %-6s %-6s %-6s %s\n' \
    "$(field "$line" class)" "$(field "$line" path)" "$(field "$line" bones)" \
    "$(field "$line" sequences)" "$(field "$line" lod0RenderIndices)" \
    "$(field "$line" lod0Built)" "$(field "$line" lod0Lost)" \
    "$(field "$line" slotsUsedByMeshes)" "$(field "$line" skinRefs)" "$(field "$line" skinFamilies)" \
    "$(field "$line" slot0Resolved)"
done

# ===========================================================================
echo
echo "=== 2/4 each class's animation source resolves (separate _animations.mdl) ==="
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
done
assert_eq "animation rows" "$ANIM_ROWS" "9"

# ===========================================================================
echo
echo "=== 3/4 sampled player entities resolve to a model (bagel 6 ticks + snakewater 1) ==="
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
echo "  --- published: entities that can only become a marker ---"
grep -h '^demo models ' "$OUT"/player-model-demo-*.txt | sed 's/^/  /'
grep -h '^demo class ' "$OUT"/player-model-demo-*.txt | sed 's/^/  /'

# ===========================================================================
if [ "$RUN_NEGATIVE" = "1" ]; then
  echo
  echo "=== 4/4 negative controls: each must fail with its named reason ==="
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

echo
if [ "$fail" = "0" ]; then
  echo "PASS player-model-check: 9/9 class models loaded and renderable; sampled players resolved; defects published above."
else
  echo "FAIL player-model-check: see the FAIL lines above."
fi
exit "$fail"
