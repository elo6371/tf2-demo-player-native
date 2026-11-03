#!/usr/bin/env bash
set -u
cd /d/TF2_Native_Worktrees/t79-integration
EXE=native/build-nmake/tf2_demo_native.exe
TF="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
DEMO="D:/TF2_Demo_Player/testdata/demos/bb841c6d379ff7c40d0c8baf99f59d8d_matcha-20260927-1347-cp_snakewater_final1.dem"
OUT=D:/_shared/camfix2
TICK=2000
mkdir -p /d/_shared/camfix2
run() { local tag="$1"; shift; TF2_NATIVE_TRACE="$OUT/$tag.trace" timeout -k 5 300 "$EXE" --tf-root "$TF" --demo "$DEMO" --hidden-window --audio-disabled --capture-frame "$OUT/$tag.bmp" --capture-tick "$TICK" "$@" >/dev/null 2>&1; echo "$tag rc=$?"; }
run R
EYE=$(grep -a "scene: camera" "$OUT/R.trace" | tail -1 | sed -n 's/.*eye=\([^ ]*\) pitch=\([^ ]*\) yaw=\([^ ]*\).*/\1 \2 \3/p' | tr ',' ' ')
echo "R eye: $EYE"
# shellcheck disable=SC2086
run D --debug-camera $EYE
run D2 --debug-camera 0 0 1000 0 0
for t in R D D2; do
  echo "== $t camera:"; grep -a "scene: camera" "$OUT/$t.trace" | tail -1
  echo "== $t capture:"; grep -a "capture: armed" "$OUT/$t.trace" | tail -1
done
echo "== diffs"
node /d/_shared/camfix/bmpdiff.mjs "$OUT/R.bmp" "$OUT/D.bmp" | sed 's/^/R_vs_D /'
node /d/_shared/camfix/bmpdiff.mjs "$OUT/R.bmp" "$OUT/D2.bmp" | sed 's/^/R_vs_D2 /'
echo DONE
