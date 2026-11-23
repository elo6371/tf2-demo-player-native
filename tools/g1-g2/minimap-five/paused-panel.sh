#!/usr/bin/env bash
# g1-paused-panel.sh -- the focused test for the paused-minimap defect.
#
# Defect (2026-11-14): with --start-paused and no --debug-camera, the first draw
# published state=ready shapes=14 and the second draw published
# reason=no-camera-pose shapes=0, so the captured frame had no panel and was
# byte-identical to the no-flag control.
#
# This runs the SAME map three ways:
#   a  --hud-minimap, no --debug-camera   -> must now draw the panel
#   b  no --hud-minimap, no --debug-camera -> control, must not
#   c  --hud-minimap, no --debug-camera, on a map whose demo has no camera at all
#      is not covered here: every demo in the corpus has a camera on its first
#      tick. c is the pre-fix binary (00ec8b99) as a negative control.
set -uo pipefail
# W1 (2026-11-23): every product launch in this script runs at 1280x720, the size
# its pixel pins were measured at. The product reads this when --window-size is
# absent (an explicit --window-size still wins); before W1 1280x720 was simply the
# default, and W1 moves the default to 1920x1080. tools/window-pin-check.sh checks it.
export TF2_NATIVE_WINDOW_SIZE="${TF2_NATIVE_WINDOW_SIZE:-1280x720}"
cd "$(dirname "$0")/../../.." || exit 2
export LC_ALL=C
EXE="${EXE:-native/build-nmake/tf2_demo_native.exe}"
TF="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
OUT="${OUT:-/d/_shared/EXEC-2026-11-14/G1/paused-panel}"
FIXTURE_SETTINGS="native/tests/fixtures/gate-settings.ini"
mkdir -p "$OUT" || exit 2

DEMO="E:/demo测试/cp_process_f12/STV/1338f5de79ea343682c17dd514c980d9_matcha-20260927-1312-cp_process_f12.dem"

run() { # run <name> [args...]
  local name="$1"; shift
  local trace="$OUT/$name.trace" bmp="$OUT/$name.bmp" log="$OUT/$name.log"
  rm -f "$trace" "$bmp" "$log"
  cp "$FIXTURE_SETTINGS" "$OUT/$name.settings.ini" 2>/dev/null || : > "$OUT/$name.settings.ini"
  TF2_NATIVE_TRACE="$(cygpath -m "$trace")" timeout 400 "$EXE" \
    --tf-root "$TF" --audio-device 6 --audio-disabled --hidden-window \
    --settings-file "$(cygpath -m "$OUT/$name.settings.ini")" \
    --ui-settings-file "$(cygpath -m "$OUT/$name.ui.ini")" \
    --demo "$DEMO" --capture-frame "$(cygpath -m "$bmp")" \
    --start-paused --hud-live "$@" > "$log" 2>&1
  echo "RUN $name rc=$?"
}

{
  echo "exe=$(cygpath -m "$PWD/$EXE")"
  echo "exe_sha256=$(sha256sum "$EXE" | awk '{print $1}')"
  echo "head=$(git rev-parse HEAD 2>/dev/null || echo none)"
} > "$OUT/identity.txt"

run on   --hud-minimap
run off

echo "PAUSED-PANEL-DONE out=$OUT"
