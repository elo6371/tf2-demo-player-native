#!/usr/bin/env bash
# T85 P0: before/after for the three weapon world models that used to fail upload
# with E_OUTOFMEMORY (c_flamethrower, c_sapper_xmas, c_shogun_warhorn).
#
# BEFORE is the frozen candidate's own binary (t79-integration, e15577b) executed
# read-only: nothing is written into that worktree, every output lands here.
# AFTER is this worktree's binary built from the T85 budget change.
#
# Usage: bash evidence/t85/run-weapon-oom.sh   (hostlock wraps the whole thing)
set -uo pipefail
cd "$(dirname "$0")/../.."
TF="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
DEMO="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf/demos/autorecord_2026-07-02_13-26-46.dem"
TICK=55418
OUT="evidence/t85/oom"
BASE_EXE="D:/TF2_Native_Worktrees/t79-integration/native/build-nmake/tf2_demo_native.exe"
NEW_EXE="native/build-nmake/tf2_demo_native.exe"
# Copies, not the tracked originals (the product rewrites recentDemo on exit and
# dirtied a tracked ui.json once): see D:/_shared/t85-hygiene/EVIDENCE-SETTINGS-DRIFT.md.
SETTINGS="evidence/t85/inputs/class-pixels-settings.json"
UI="evidence/t85/inputs/class-pixels-ui.json"
mkdir -p "$OUT"

for path in "$TF" "$DEMO" "$BASE_EXE" "$NEW_EXE"; do
  [ -e "$path" ] || { echo "FATAL: missing input: $path"; exit 2; }
done
echo "demo sha256=$(sha256sum "$DEMO" | cut -d' ' -f1)"
echo "before exe=$BASE_EXE sha256=$(sha256sum "$BASE_EXE" | cut -d' ' -f1)"
echo "after  exe=$NEW_EXE sha256=$(sha256sum "$NEW_EXE" | cut -d' ' -f1)"

run_one() { # run_one <tag> <exe>
  local tag="$1" exe="$2"
  rm -f "$OUT/$tag.bmp" "$OUT/$tag.trace" "$OUT/$tag.log"
  TF2_NATIVE_TRACE="$OUT/$tag.trace" timeout 1800 "$exe" \
    --tf-root "$TF" --hidden-window --audio-disabled \
    --settings-file "$SETTINGS" --ui-settings-file "$UI" \
    --demo "$DEMO" --capture-tick "$TICK" --playback-speed 32 \
    --capture-frame "$OUT/$tag.bmp" --player-weapon-attach on \
    > "$OUT/$tag.log" 2>&1
  local rc=$?
  echo "$tag rc=$rc"
}

summarise() { # summarise <tag>
  local tag="$1" t="$OUT/$tag.trace"
  echo "---- $tag ----"
  echo "  trace bytes=$(stat -c %s "$t" 2>/dev/null || echo 0)"
  echo "  capture frame present=$([ -f "$OUT/$tag.bmp" ] && echo yes || echo no)"
  echo "  attach refusals = $(grep -a -c 'attach: mesh result=refused' "$t" 2>/dev/null || echo 0)"
  echo "  E_OUTOFMEMORY lines = $(grep -a -c '0x8007000E' "$t" 2>/dev/null || echo 0)"
  echo "  attach supplied = $(grep -a -c 'attach: mesh result=supplied' "$t" 2>/dev/null || echo 0)"
  for m in c_flamethrower/c_flamethrower.mdl c_sapper/c_sapper_xmas.mdl c_shogun_warhorn/c_shogun_warhorn.mdl; do
    local sup ref
    sup=$(grep -a -c "attach: mesh result=supplied path=models/.*$m" "$t" 2>/dev/null || echo 0)
    ref=$(grep -a -c "attach: mesh result=refused.*$m" "$t" 2>/dev/null || echo 0)
    echo "  $m supplied=$sup refused=$ref"
  done
  echo "  last meshTable:  $(grep -a 'budget: meshTable' "$t" 2>/dev/null | tail -1)"
  echo "  last prepare:    $(grep -a 'budget: prepare' "$t" 2>/dev/null | tail -1)"
  echo "  last instances:  $(grep -a 'budget: instances' "$t" 2>/dev/null | tail -1)"
  echo "  last draw:       $(grep -a 'budget: draw' "$t" 2>/dev/null | tail -1)"
  echo "  last weaponAttach: $(grep -a 'scene: weaponAttach' "$t" 2>/dev/null | tail -1)"
  grep -a 'budget: prepare\|budget: meshTable\|budget: instances\|budget: draw\|attach: meshSource\|attach: mesh result=refused' "$t" 2>/dev/null \
    | tail -40 > "$OUT/$tag.ledger.txt"
  echo "  ledger excerpt: $OUT/$tag.ledger.txt ($(wc -l < "$OUT/$tag.ledger.txt") lines)"
}

run_one before "$BASE_EXE"
summarise before
run_one after "$NEW_EXE"
summarise after
