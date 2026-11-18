#!/usr/bin/env bash
# g1-minimap-five.sh -- D08: capture a fixed-camera minimap frame on the five
# first-release maps that had none (Process, Gullywash, Sultry, Product, Bagel),
# plus Upward and Sunshine as controls the existing gate already covers.
#
# One product run WITH --hud-minimap and one WITHOUT, per map. The pair is what
# lets the pixel half say "only the panel changed". Nothing here rebuilds the
# product: it runs the binary that is on disk and records its hash.
#
# Outputs: $OUT/<map>.{trace,bmp,csv,log} and $OUT/<map>.off.{trace,bmp,...}
set -uo pipefail
cd "$(dirname "$0")/../../.." || exit 2
export LC_ALL=C

EXE=native/build-nmake/tf2_demo_native.exe
TF="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
OUT="${OUT:-/d/_shared/EXEC-2026-11-14/G1/minimap-five}"
FIXTURE_SETTINGS="native/tests/fixtures/gate-settings.ini"
AUDIO_DEVICE=6
RUN_TIMEOUT=400

mkdir -p "$OUT" || exit 2

map_demo() { # map_demo <map>
  case "$1" in
    cp_process_f12)      echo "E:/demo测试/cp_process_f12/STV/1338f5de79ea343682c17dd514c980d9_matcha-20260927-1312-cp_process_f12.dem" ;;
    cp_gullywash_f9)     echo "E:/demo测试/cp_gullywash_f9/STV/gully4454.dem" ;;
    cp_sultry_b8a)       echo "E:/demo测试/cp_sultry_b8a/STV/f63a7a2040379d7d6a8414f76bbb81dc_matcha-20260703-1442-cp_sultry_b8a.dem" ;;
    koth_product_final)  echo "E:/demo测试/koth_product_final/STV/prodcutscout.dem" ;;
    koth_bagel_rc11)     echo "E:/demo测试/koth_bagel_rc11/STV/dfbce608c80a4d72a390c70c597f0377_match-20261101-1959-koth_bagel_rc11.dem" ;;
    pl_upward_f12)       echo "E:/demo测试/pl_upward_f12/STV/a9630c6557468b24f0b8c2e7dab38ed8_match-20261030-2145-pl_upward_f12.dem" ;;
    cp_sunshine)         echo "$TF/demos/SUNSHINE.dem" ;;
  esac
}

map_camera() { # map_camera <map> -- 1200 units off control point 0, 300 above it
  # WHY OFFSET: the panel draws a viewer dot at the camera. A camera placed ON a
  # control point puts that dot on the ring, and the 5x5 ring window then reads
  # 16 ring pixels + 9 viewer pixels instead of 25 ring pixels -- the frame is
  # right and the sample is contaminated. 1200 units is 19-23 px on these maps,
  # so the dot and every ring are separate.
  case "$1" in
    cp_process_f12)      echo "-2340 -2480 880 0 0" ;;
    cp_gullywash_f9)     echo "2568 -1957 460 0 0" ;;
    cp_sultry_b8a)       echo "-1519 2692 944 0 0" ;;
    koth_product_final)  echo "-336 -1200 524 0 0" ;;
    koth_bagel_rc11)     echo "1200 -1200 756 0 0" ;;
    pl_upward_f12)       echo "2224 -2736 436 0 0" ;;
    cp_sunshine)         echo "-6432 7740 196 0 0" ;;
  esac
}

run_one() { # run_one <map> <suffix> [extra args...]
  local map="$1" suffix="$2"; shift 2
  local trace="$OUT/$map$suffix.trace" bmp="$OUT/$map$suffix.bmp"
  local csv="$OUT/$map$suffix.csv" log="$OUT/$map$suffix.log"
  local settings="$OUT/$map$suffix.settings.ini" ui="$OUT/$map$suffix.ui.ini"
  rm -f "$trace" "$bmp" "$csv" "$log" "$settings" "$ui"
  if [ -f "$FIXTURE_SETTINGS" ]; then cp "$FIXTURE_SETTINGS" "$settings"; else : > "$settings"; fi
  local cam="--debug-camera $(map_camera "$map")"
  [ "${NO_DEBUG_CAMERA:-0}" = "1" ] && cam=""
  TF2_NATIVE_TRACE="$(cygpath -m "$trace")" timeout "$RUN_TIMEOUT" "$EXE" \
    --tf-root "$TF" --audio-device "$AUDIO_DEVICE" --audio-disabled --hidden-window \
    --settings-file "$(cygpath -m "$settings")" \
    --ui-settings-file "$(cygpath -m "$ui")" \
    --demo "$(map_demo "$map")" --capture-frame "$(cygpath -m "$bmp")" \
    --metrics-file "$(cygpath -m "$csv")" \
    --start-paused --hud-live $cam "$@" > "$log" 2>&1
  echo "RUN $map$suffix rc=$? trace=$(basename "$trace")"
}

MAPS="${MAPS:-cp_process_f12 cp_gullywash_f9 cp_sultry_b8a koth_product_final koth_bagel_rc11 pl_upward_f12 cp_sunshine}"

{
  echo "exe=$(cygpath -m "$PWD/$EXE")"
  echo "exe_sha256=$(sha256sum "$EXE" | awk '{print $1}')"
  echo "exe_bytes=$(wc -c < "$EXE" | tr -d ' ')"
  echo "head=$(git rev-parse HEAD 2>/dev/null || echo none)"
  echo "maps=$MAPS"
} > "$OUT/identity.txt"

for m in $MAPS; do
  if [ ! -f "$(map_demo "$m")" ]; then echo "SKIP $m: no demo at $(map_demo "$m")"; continue; fi
  run_one "$m" "" --hud-minimap ${EXTRA:-}
  run_one "$m" ".off" ${EXTRA:-}
done

echo "MINIMAP-FIVE-CAPTURE-DONE out=$OUT"
