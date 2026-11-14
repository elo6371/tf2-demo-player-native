#!/usr/bin/env bash
# capture-cp-traces.sh -- one product launch per tabulated map, capturing the
# minimap trace that carries the per-control-point lines.
#
# Why: D43 says the minimap scale was never independently verified. Tier 1 of
# n14_verify_trace.py re-derives the projection from the stated convention and
# matches the product's numbers, but tier 2 -- "are the drawn control points the
# BSP's own team_control_point origins?" -- needs a trace that actually contains
# "minimap: cp name=..." lines. The G1 log (minimap-a12-post.log) has none, so
# tier 2 has been UNSUPPORTED on every attempt so far.
#
# This script produces one such trace per map, so tier 2 can run over all seven
# instead of one. Every launch is silent (--audio-disabled) and hidden-window.
set -uo pipefail
cd "$(dirname "$0")/../.." || exit 2
export LC_ALL=C
EXE="${EXE:-native/build-nmake/tf2_demo_native.exe}"
TF="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
OUT="${OUT:-D:/_shared/EXEC-2026-11-15/n14/captures}"
FIXTURE_SETTINGS="native/tests/fixtures/gate-settings.ini"
CAMERA_ARGS="${CAMERA_ARGS:---debug-camera 0 -2048 512 0 33}"
mkdir -p "$OUT" || exit 2

MAPS=(cp_process_f12 cp_sunshine cp_gullywash_f9 cp_sultry_b8a koth_product_final koth_bagel_rc11 pl_upward_f12)
declare -A DEMO
DEMO[cp_process_f12]="E:/demo测试/cp_process_f12/STV/1338f5de79ea343682c17dd514c980d9_matcha-20260927-1312-cp_process_f12.dem"
DEMO[cp_sunshine]="E:/demo测试/cp_sunshine/STV/SUNSHINE.dem"
DEMO[cp_gullywash_f9]="E:/demo测试/cp_gullywash_f9/STV/gully4454.dem"
DEMO[cp_sultry_b8a]="E:/demo测试/cp_sultry_b8a/STV/f63a7a2040379d7d6a8414f76bbb81dc_matcha-20260703-1442-cp_sultry_b8a.dem"
DEMO[koth_product_final]="E:/demo测试/koth_product_final/STV/prodcutscout.dem"
DEMO[koth_bagel_rc11]="E:/demo测试/koth_bagel_rc11/STV/dfbce608c80a4d72a390c70c597f0377_match-20261101-1959-koth_bagel_rc11.dem"
DEMO[pl_upward_f12]="E:/demo测试/pl_upward_f12/STV/a9630c6557468b24f0b8c2e7dab38ed8_match-20261030-2145-pl_upward_f12.dem"

run_one() { # run_one <map>
  local map="$1"
  local trace="$OUT/$map.trace" bmp="$OUT/$map.bmp" log="$OUT/$map.log"
  local settings="$OUT/$map.settings.ini" uisettings="$OUT/$map.ui.ini"
  rm -f "$trace" "$bmp" "$log" "$settings" "$uisettings"
  if [ -f "$FIXTURE_SETTINGS" ]; then cp "$FIXTURE_SETTINGS" "$settings"; else : > "$settings"; fi
  TF2_NATIVE_TRACE="$(cygpath -m "$trace")" timeout 400 "$EXE" \
    --tf-root "$TF" --audio-device 6 --audio-disabled --hidden-window \
    --settings-file "$(cygpath -m "$settings")" \
    --ui-settings-file "$(cygpath -m "$uisettings")" \
    --demo "${DEMO[$map]}" --capture-frame "$(cygpath -m "$bmp")" \
    --metrics-file "$(cygpath -m "$OUT/$map.csv")" \
    --hud-live --hud-minimap $CAMERA_ARGS > "$log" 2>&1
  local rc=$?
  local cps
  cps=$(grep -ac "minimap: cp name=" "$trace" 2>/dev/null || echo 0)
  echo "RUN map=$map rc=$rc cplines=$cps trace=$trace"
}

{
  echo "exe=$(cygpath -m "$PWD/$EXE")"
  # D53: never pipe a value through awk on this host -- the worktree awk keeps CR as an
  # ordinary byte while the Git awk strips it, so identical bytes compare differently
  # depending on PATH. cut + tr is byte-explicit.
  echo "exe_sha256=$(sha256sum "$EXE" | cut -d' ' -f1 | tr -d '\r')"
  echo "exe_bytes=$(stat -c %s "$EXE")"
  echo "head=$(git rev-parse HEAD 2>/dev/null || echo none)"
  echo "camera=$CAMERA_ARGS"
  echo "started=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
} > "$OUT/identity.txt"

for map in "${MAPS[@]}"; do
  run_one "$map"
done

echo "CP-TRACES-DONE out=$OUT"
