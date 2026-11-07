#!/usr/bin/env bash
# T85 P0: BEFORE/AFTER for the three non-target weapon world models that the frozen
# candidate refused with E_OUTOFMEMORY (t62-matrix/RUNS-2026-11-04.md §26.3:
# "Three non-target weapons are refused with E_OUTOFMEMORY 0x8007000E:
#  c_sapper_xmas, c_shogun_warhorn, c_flamethrower").
#
# The recording is the one that measurement was taken on: the Highlander STV
# pl_upward_f12 demo, where 19 player bodies are on screen at once, so the attach
# path supplies more weapon world models than the 96-entry entity-mesh table holds.
# BEFORE is the frozen candidate's own binary executed read-only; AFTER is this
# worktree's binary. Everything is written here.
#
# Usage: bash evidence/t85/run-upward-attach.sh [tag]   (hostlock wraps the call)
set -uo pipefail
cd "$(dirname "$0")/../.."
TF="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
DEMO="C:/Users/Administrator/Downloads/a9630c6557468b24f0b8c2e7dab38ed8_match-20261030-2145-pl_upward_f12.dem"
CLASS="${1:-pyro}"
case "$CLASS" in
  pyro)     ENT=3;  TICK=30001; OX=1957.0; OY=-125.1;  OZ=94.8 ;;
  heavy)    ENT=11; TICK=24001; OX=1609.4; OY=-1293.8; OZ=80.7 ;;
  spy)      ENT=18; TICK=24001; OX=955.4;  OY=-1255.4; OZ=349.5 ;;
  engineer) ENT=8;  TICK=24001; OX=1889.4; OY=288.9;   OZ=400.0 ;;
  sniper)   ENT=15; TICK=20001; OX=-958.4; OY=-1496.8; OZ=267.6 ;;
  *) echo "unknown class $CLASS"; exit 2 ;;
esac
SPEED=32
# Inputs are COPIES under evidence/t85/inputs/, never the tracked originals: the
# product rewrites recentDemo0..7 in --ui-settings-file on exit (native_ui.cpp:356
# rememberDemo -> :137 persist), which dirtied a tracked file once already.
# See D:/_shared/t85-hygiene/EVIDENCE-SETTINGS-DRIFT.md.
SETTINGS="evidence/t85/inputs/upward-settings.json"
UI="evidence/t85/inputs/upward-ui.json"
OUT="evidence/t85/upward-$CLASS"
BASE_EXE="D:/TF2_Native_Worktrees/t79-integration/native/build-nmake/tf2_demo_native.exe"
NEW_EXE="native/build-nmake/tf2_demo_native.exe"
EYEX=$(awk -v x="$OX" 'BEGIN { printf "%.1f", x - 250 }')
EYEY="$OY"
EYEZ=$(awk -v z="$OZ" 'BEGIN { printf "%.1f", z + 58 }')
mkdir -p "$OUT"

for path in "$TF" "$DEMO" "$BASE_EXE" "$NEW_EXE" "$SETTINGS" "$UI"; do
  [ -e "$path" ] || { echo "FATAL: missing input: $path"; exit 2; }
done
echo "class=$CLASS ent=$ENT tick=$TICK origin=$OX,$OY,$OZ eye=$EYEX,$EYEY,$EYEZ speed=$SPEED"
echo "demo sha256=$(sha256sum "$DEMO" | cut -d' ' -f1)"
echo "before exe sha256=$(sha256sum "$BASE_EXE" | cut -d' ' -f1)"
echo "after  exe sha256=$(sha256sum "$NEW_EXE" | cut -d' ' -f1)"

run_one() { # run_one <tag> <exe>
  local tag="$1" exe="$2"
  rm -f "$OUT/$tag.bmp" "$OUT/$tag.trace" "$OUT/$tag.log"
  TF2_NATIVE_TRACE="$OUT/$tag.trace" timeout 900 "$exe" \
    --tf-root "$TF" --hidden-window --audio-disabled \
    --settings-file "$SETTINGS" --ui-settings-file "$UI" \
    --demo "$DEMO" --capture-tick "$TICK" --playback-speed "$SPEED" \
    --capture-frame "$OUT/$tag.bmp" \
    --debug-camera "$EYEX" "$EYEY" "$EYEZ" 4.1 0.0 \
    --player-animation --player-weapon-attach on \
    > "$OUT/$tag.log" 2>&1
  local rc=$?
  echo "$tag rc=$rc bmp=$([ -f "$OUT/$tag.bmp" ] && echo yes || echo no) trace=$(stat -c %s "$OUT/$tag.trace" 2>/dev/null || echo 0)"
}

summarise() { # summarise <tag>
  local tag="$1"
  local t="$OUT/$tag.trace"
  echo "---- $tag ----"
  echo "  attach refusals        = $(grep -a -c 'attach: mesh result=refused' "$t" 2>/dev/null || echo 0)"
  echo "  E_OUTOFMEMORY 0x8007000E = $(grep -a -c '0x8007000E' "$t" 2>/dev/null || echo 0)"
  echo "  attach refused paths:"
  grep -a 'attach: mesh result=refused' "$t" 2>/dev/null | sed -n 's/.*path=\([^ ]*\).*/    \1/p' | sort | uniq -c | head -10
  echo "  $(grep -a 'attach: meshSource' "$t" 2>/dev/null | tail -1)"
  echo "  $(grep -a 'scene: weaponAttach ' "$t" 2>/dev/null | tail -1)"
  echo "  last meshTable : $(grep -a 'budget: meshTable' "$t" 2>/dev/null | tail -1)"
  echo "  last instances : $(grep -a 'budget: instances' "$t" 2>/dev/null | tail -1)"
  echo "  last draw      : $(grep -a 'budget: draw' "$t" 2>/dev/null | tail -1)"
  grep -a 'budget: \|attach: meshSource\|attach: mesh result=refused' "$t" 2>/dev/null | tail -40 > "$OUT/$tag.ledger.txt"
  echo "  ledger: $OUT/$tag.ledger.txt ($(wc -l < "$OUT/$tag.ledger.txt") lines)"
}

MODE="${2:-both}"
case "$MODE" in
  both|before) run_one before "$BASE_EXE"; summarise before ;;
esac
case "$MODE" in
  both|after) run_one after "$NEW_EXE"; summarise after ;;
esac
