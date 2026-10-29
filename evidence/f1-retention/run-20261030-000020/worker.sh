#!/usr/bin/env bash
set -uo pipefail
RUN="$1"
cd /d/TF2_Native_Worktrees/candidate-final || exit 9
TF="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
DEMO="$TF/demos/autorecord_2026-07-23_12-57-17.dem"
V=/d/TF2_Native_Worktrees/_shared/t6-f1
RESULT="$RUN/result.txt"

echo "=== build" 
bash build-target.sh tf2_demo_native entity_protocol_probe projectile_track_probe entity_overlay_probe > "$RUN/build.log" 2>&1
BRC=$?
echo "BUILD_RC=$BRC" >> "$RESULT"
{ echo "EXES:"
  for e in native/build-nmake/tf2_demo_native.exe native/build-nmake/entity_protocol_probe.exe native/build-nmake/projectile_track_probe.exe native/build-nmake/entity_overlay_probe.exe; do
    printf "%s %s %s\n" "$(stat -c %s "$e")" "$(sha256sum "$e" | cut -d' ' -f1)" "$e"
  done
} > "$RUN/exes.txt"
cat "$RUN/exes.txt"
[ "$BRC" -eq 0 ] || { echo "ABORT: build failed"; exit 90; }

echo "=== product on 07-23"
S=$(date +%s%3N)
timeout -s KILL 300 native/build-nmake/tf2_demo_native.exe --tf-root "$TF" --hidden-window --audio-disabled --demo "$DEMO" --capture-tick 1 --playback-speed 8 --capture-frame "$RUN/big-07-23.bmp" --outline-vertex-extent > "$RUN/product-big.log" 2>&1
PRC=$?
E=$(date +%s%3N)
PW=$(( (E - S) / 1000 ))
BSZ=$(stat -c %s "$RUN/big-07-23.bmp" 2>/dev/null || echo none)
echo "product-07-23 rc=$PRC wall=${PW}s bmp=$BSZ" >> "$RESULT"
echo "product-07-23 rc=$PRC wall=${PW}s bmp=$BSZ"
tail -c 800 "$RUN/product-big.log"

echo "=== entity_protocol_probe on 07-23"
S=$(date +%s%3N)
timeout -s KILL 300 native/build-nmake/entity_protocol_probe.exe --summary "$DEMO" > "$RUN/entity-protocol-07-23.log" 2>&1
XRC=$?
E=$(date +%s%3N)
XW=$(( (E - S) / 1000 ))
echo "entity-protocol-07-23 rc=$XRC wall=${XW}s" >> "$RESULT"
echo "entity-protocol-07-23 rc=$XRC wall=${XW}s"
tail -c 600 "$RUN/entity-protocol-07-23.log"

echo "=== trajectory gate forward"
S=$(date +%s%3N)
TRAJECTORY_OUT="$RUN/trajectory-forward" timeout -s KILL 1800 bash trajectory-overlay-check.sh > "$RUN/trajectory-forward.log" 2>&1
FRC=$?
E=$(date +%s%3N)
echo "trajectory-forward rc=$FRC wall=$(( (E - S) / 1000 ))s" >> "$RESULT"
tail -c 700 "$RUN/trajectory-forward.log"

echo "=== trajectory gate mutation"
S=$(date +%s%3N)
TRAJECTORY_OUT="$RUN/trajectory-mutation" timeout -s KILL 3600 bash trajectory-overlay-check.sh --mutation > "$RUN/trajectory-mutation.log" 2>&1
MRC=$?
E=$(date +%s%3N)
echo "trajectory-mutation rc=$MRC wall=$(( (E - S) / 1000 ))s" >> "$RESULT"
tail -c 900 "$RUN/trajectory-mutation.log"

echo "=== post"
{
  echo "STATUS:"
  git status --porcelain
  echo "SOURCES:"
  sha256sum native/include/demo_header.h native/src/demo_header.cpp native/tools/projectile_track_probe.cpp trajectory-overlay-check.sh
} > "$RUN/post.txt"
cat "$RUN/post.txt"
echo "=== product processes"
tasklist 2>/dev/null | grep -i -E "tf2_demo_native|_probe\.exe" || echo none
echo "=== done"
cat "$RESULT"
