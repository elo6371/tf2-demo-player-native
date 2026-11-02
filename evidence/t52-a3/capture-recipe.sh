#!/bin/bash
# T52 A3 pixel evidence: animated silhouette vs bind silhouette at one fixed
# camera/tick. Determinism control included (two runs, identical args).
cd /d/TF2_Native_Worktrees/t79-integration || exit 1
OUT=/d/_shared/t52-a3/a3pix
mkdir -p "$OUT"
DEMO=D:/TF2_Demo_Player/testdata/demos/bb841c6d379ff7c40d0c8baf99f59d8d_matcha-20260927-1347-cp_snakewater_final1.dem
CAM="5699 -1611 150 14.4 -143.3"
run() {
  local name="$1" tick="$2" anim="$3"
  rm -f "$OUT/$name.bmp" "$OUT/$name.trace"
  TF2_NATIVE_TRACE="$OUT/$name.trace" timeout 900 native/build-nmake/tf2_demo_native.exe \
    --tf-root "D:/SteamLibrary/steamapps/common/Team Fortress 2/tf" --hidden-window --audio-disabled \
    --demo "$DEMO" --capture-tick "$tick" --playback-speed 8 --capture-frame "$OUT/$name.bmp" \
    --outline-vertex-extent --outline on --debug-camera $CAM $anim > "$OUT/$name.log" 2>&1
  local rc=$?
  echo "$name tick=$tick anim='$anim' rc=$rc bmp=$(stat -c%s "$OUT/$name.bmp" 2>/dev/null || echo none)"
  grep 'outline: draw' "$OUT/$name.trace" 2>/dev/null | tail -1 | grep -o 'silhouettes=[0-9]* submitted=[0-9]* poseWorld=[0-9]* poseBind=[0-9]*'
}
run on1 64250 --player-animation
run on2 64258 --player-animation
run bind1 64250 --no-player-animation
run bind_d 64250 --no-player-animation
