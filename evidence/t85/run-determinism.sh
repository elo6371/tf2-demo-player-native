#!/usr/bin/env bash
# T85 P0 acceptance item 5: the same captured frame from two INDEPENDENT processes
# must agree. Two launches, identical argv (the pinned outline fixture: tick 64250,
# eye "5699 -1611 150 14.4 -143.3", speed 8), different output prefixes; the frames
# are compared byte-for-byte and the summary trace lines are compared line-for-line
# after the tag path is normalised away.
set -uo pipefail
cd "$(dirname "$0")/../.."
TF="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
DEMO="D:/TF2_Demo_Player/testdata/demos/bb841c6d379ff7c40d0c8baf99f59d8d_matcha-20260927-1347-cp_snakewater_final1.dem"
TICK=64250
SPEED=8
CAM="5699 -1611 150 14.4 -143.3"
OUT="evidence/t85/determinism"
EXE="native/build-nmake/tf2_demo_native.exe"
mkdir -p "$OUT"
# HYGIENE (lead m01207): the product rewrites the settings and UI files it is
# handed on EVERY exit (main.cpp saveSettings/bookmarks, native_ui.cpp MRU). Give
# each run its OWN copy under $OUT/inputs, seeded from the host profile, so the
# two runs share one input and neither writes the real profile.
SEED_DIR="$OUT/inputs"
mkdir -p "$SEED_DIR"
HOST_PROFILE_DIR="${LOCALAPPDATA:-${HOME}/AppData/Local}/TF2 Demo Player"
[ -f "$HOST_PROFILE_DIR/settings.ini" ] && cp "$HOST_PROFILE_DIR/settings.ini" "$SEED_DIR/host-settings.ini" 2>/dev/null
[ -f "$HOST_PROFILE_DIR/ui.ini" ] && cp "$HOST_PROFILE_DIR/ui.ini" "$SEED_DIR/host-ui.ini" 2>/dev/null
[ -e "$DEMO" ] || { echo "FATAL: missing demo $DEMO"; exit 2; }
echo "demo sha256=$(sha256sum "$DEMO" | cut -d' ' -f1)"
echo "exe  sha256=$(sha256sum "$EXE" | cut -d' ' -f1)"
run_one() { # run_one <tag>
  local tag="$1" raw="$OUT/$1.raw.trace"
  rm -f "$OUT/$tag.bmp" "$raw" "$OUT/$tag.log" "$OUT/$tag.trace"
  local run_settings="$SEED_DIR/$tag.settings.ini" run_ui="$SEED_DIR/$tag.ui.ini"
  if [ -f "$SEED_DIR/host-settings.ini" ]; then cp "$SEED_DIR/host-settings.ini" "$run_settings"; else : > "$run_settings"; fi
  if [ -f "$SEED_DIR/host-ui.ini" ]; then cp "$SEED_DIR/host-ui.ini" "$run_ui"; else : > "$run_ui"; fi
  # NO --no-player-animation: the product has no such switch (main.cpp knows only
  # --player-animation at :2807, and it defaults to false at :2125), so the old flag
  # was silently ignored. Animation stays off by omission -- which is the honest
  # spelling of the same frame, and it cannot be mistaken for a measurement.
  TF2_NATIVE_TRACE="$raw" timeout 1800 "$EXE" \
    --tf-root "$TF" --hidden-window --audio-disabled --demo "$DEMO" \
    --capture-tick "$TICK" --playback-speed "$SPEED" --capture-frame "$OUT/$tag.bmp" \
    --settings-file "$run_settings" --ui-settings-file "$run_ui" \
    --outline-vertex-extent --debug-camera $CAM \
    --outline on --player-weapon-attach on \
    > "$OUT/$tag.log" 2>&1
  local rc=$?
  { grep -aE "gpu: |budget: |overlay: |scene: |capture: |attach: |title: |window: " "$raw" || true;
    grep -a "outline: draw" "$raw" | tail -n 50 || true; } > "$OUT/$tag.trace"
  echo "$tag rc=$rc raw_bytes=$(stat -c%s "$raw" 2>/dev/null || echo 0) kept_lines=$(wc -l < "$OUT/$tag.trace")"
  rm -f "$raw"
}
run_one det-a
run_one det-b
echo "a bmp sha=$(sha256sum "$OUT/det-a.bmp" | cut -d' ' -f1) bytes=$(stat -c%s "$OUT/det-a.bmp")"
echo "b bmp sha=$(sha256sum "$OUT/det-b.bmp" | cut -d' ' -f1) bytes=$(stat -c%s "$OUT/det-b.bmp")"
if cmp -s "$OUT/det-a.bmp" "$OUT/det-b.bmp"; then echo "FRAME: IDENTICAL"; else echo "FRAME: DIFFER"; fi
for t in det-a det-b; do
  sed -e "s|$OUT/$t|TAG|g" -e "s|$t|TAG|g" "$OUT/$t.trace" > "$OUT/$t.trace.norm"
done
if cmp -s "$OUT/det-a.trace.norm" "$OUT/det-b.trace.norm"; then
  echo "TRACE: IDENTICAL ($(wc -l < "$OUT/det-a.trace.norm") lines)"
else
  echo "TRACE: DIFFER ($(diff "$OUT/det-a.trace.norm" "$OUT/det-b.trace.norm" | grep -c '^[<>]') differing lines)"
  diff "$OUT/det-a.trace.norm" "$OUT/det-b.trace.norm" | head -20
fi
echo "budget lines of det-a:"
grep -a "budget: " "$OUT/det-a.trace" | tail -4
