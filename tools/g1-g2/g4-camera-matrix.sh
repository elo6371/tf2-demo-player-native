#!/usr/bin/env bash
# g4-camera-matrix.sh -- POV vs STV on the same map, two ticks each (N12 pilot).
#
# WHAT IT IS FOR
#   Until 2026-11-14 the corpus had no pl_upward_f12 POV, so "does the product
#   put the camera where the recording says" could only be asked of one source at
#   a time. This runs both sources over the same map at the same two ticks and
#   records the camera readings the product itself prints, so the two can be
#   compared without a second tool.
#
# WHAT IT IS NOT
#   Not a camera acceptance: it does not check the eye against an independent
#   decoder, does not cover viewEntity/serial/userdata changes, and says nothing
#   about seek, follow-target switching or free-look. Those stay in N12.
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
OUT="${OUT:-/d/_shared/EXEC-2026-11-14/G4/camera-matrix}"
FIXTURE="native/tests/fixtures/gate-settings.ini"
mkdir -p "$OUT" || exit 2

POV="E:/demo测试/pl_upward_f12/POV/autorecord_2026-08-08_21-43-59.dem"
STV="E:/demo测试/pl_upward_f12/STV/a9630c6557468b24f0b8c2e7dab38ed8_match-20261030-2145-pl_upward_f12.dem"

{
  echo "exe_sha256=$(sha256sum $EXE | awk '{print $1}')"
  echo "head=$(git rev-parse HEAD)"
  echo "pov=$POV"
  echo "stv=$STV"
} > "$OUT/identity.txt"

run() { # run <tag> <demo> <tick>
  local tag="$1" demo="$2" tick="$3"
  cp "$FIXTURE" "$OUT/$tag.settings.ini" || exit 2
  rm -f "$OUT/$tag.bmp" "$OUT/$tag.trace"
  TF2_NATIVE_TRACE="$(cygpath -m "$OUT/$tag.trace")" timeout 400 "$EXE" \
    --tf-root "$TF" --audio-device 6 --audio-disabled --hidden-window \
    --settings-file "$(cygpath -m "$OUT/$tag.settings.ini")" \
    --ui-settings-file "$(cygpath -m "$OUT/$tag.ui.ini")" \
    --demo "$demo" --playback-speed 8 --capture-tick "$tick" --hud-off \
    --capture-frame "$(cygpath -m "$OUT/$tag.bmp")" > "$OUT/$tag.log" 2>&1
  echo "RUN $tag rc=$?"
}

run pov-t20000  "$POV" 20000
run pov-t40000  "$POV" 40000
run stv-t20000  "$STV" 20000
run stv-t40000  "$STV" 40000
echo "CAMERA-MATRIX-DONE out=$OUT"
