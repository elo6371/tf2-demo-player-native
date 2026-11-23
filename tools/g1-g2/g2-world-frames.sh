#!/usr/bin/env bash
# g2-world-frames.sh -- one world frame per first-release map, from the demo's
# own SourceTV camera at a fixed tick.
#
# WHY THIS CAMERA: the G1 minimap frames used --debug-camera 1200 units off a
# control point, which is a fine position for judging the panel but a bad one for
# judging the world -- two of the seven landed inside terrain. This run takes the
# camera from the recording instead (no --debug-camera) at tick 30000, and turns
# the HUD off so the frame is only the world. It must NOT start paused: a paused
# demo sits at tick 0 forever (measured: 14062 frames at tick=0 and the capture
# never fired), so the run plays at 8x and the armed capture fires at the first
# frame whose tick is at or past TICK.
#
# It is a SCREENING frame: it can show "geometry renders here" or "it does not".
# It is not seven-map acceptance (that needs POV and STV per map, named objects,
# control points, sky and a control region).
set -uo pipefail
# W1 (2026-11-23): every product launch in this script runs at 1280x720, the size
# its pixel pins were measured at. The product reads this when --window-size is
# absent (an explicit --window-size still wins); before W1 1280x720 was simply the
# default, and W1 moves the default to 1920x1080. tools/window-pin-check.sh checks it.
export TF2_NATIVE_WINDOW_SIZE="${TF2_NATIVE_WINDOW_SIZE:-1280x720}"
cd "$(dirname "$0")/../.." || exit 2
export LC_ALL=C
TF="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
EXE=native/build-nmake/tf2_demo_native.exe
OUT="${OUT:-/d/_shared/EXEC-2026-11-14/G2/world-frames}"
TICK="${TICK:-30000}"
mkdir -p "$OUT" || exit 2

demo_for() {
  case "$1" in
    cp_process_f12)      echo "E:/demo测试/cp_process_f12/STV/1338f5de79ea343682c17dd514c980d9_matcha-20260927-1312-cp_process_f12.dem" ;;
    cp_gullywash_f9)     echo "E:/demo测试/cp_gullywash_f9/STV/gully4454.dem" ;;
    cp_sultry_b8a)       echo "E:/demo测试/cp_sultry_b8a/STV/f63a7a2040379d7d6a8414f76bbb81dc_matcha-20260703-1442-cp_sultry_b8a.dem" ;;
    koth_product_final)  echo "E:/demo测试/koth_product_final/STV/prodcutscout.dem" ;;
    koth_bagel_rc11)     echo "E:/demo测试/koth_bagel_rc11/STV/dfbce608c80a4d72a390c70c597f0377_match-20261101-1959-koth_bagel_rc11.dem" ;;
    pl_upward_f12)       echo "E:/demo测试/pl_upward_f12/STV/a9630c6557468b24f0b8c2e7dab38ed8_match-20261030-2145-pl_upward_f12.dem" ;;
    cp_sunshine)         echo "E:/demo测试/cp_sunshine/STV/SUNSHINE.dem" ;;
  esac
}

{
  echo "exe_sha256=$(sha256sum $EXE | awk '{print $1}')"
  echo "head=$(git rev-parse HEAD)"
  echo "tick=$TICK"
} > "$OUT/identity.txt"

ALL="cp_process_f12 cp_gullywash_f9 cp_sultry_b8a koth_product_final koth_bagel_rc11 pl_upward_f12 cp_sunshine"
for m in ${MAP_ONLY:-$ALL}; do
  d="$(demo_for "$m")"
  [ -f "$d" ] || { echo "SKIP $m missing demo"; continue; }
  rm -f "$OUT/$m.bmp" "$OUT/$m.trace"
  # COPY THE FIXTURE. The product writes the settings file back on exit, so
  # pointing --settings-file at native/tests/fixtures/gate-settings.ini mutates a
  # tracked file (measured 2026-11-14: 118 lines added, the 10 pinned values
  # unchanged; restored with git checkout and re-verified key by key).
  cp native/tests/fixtures/gate-settings.ini "$OUT/$m.settings.ini" || exit 2
  TF2_NATIVE_TRACE="$(cygpath -m "$OUT/$m.trace")" timeout 400 "$EXE" \
    --tf-root "$TF" --audio-device 6 --audio-disabled --hidden-window \
    --settings-file "$(cygpath -m "$OUT/$m.settings.ini")" \
    --ui-settings-file "$(cygpath -m "$OUT/$m.ui.ini")" \
    --demo "$d" --playback-speed 8 --capture-tick "$TICK" \
    --hud-off --capture-frame "$(cygpath -m "$OUT/$m.bmp")" > "$OUT/$m.log" 2>&1
  echo "WORLD $m rc=$?"
done
echo "WORLD-FRAMES-DONE out=$OUT"
