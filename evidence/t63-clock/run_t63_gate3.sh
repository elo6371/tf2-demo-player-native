#!/bin/bash
set -u
cd /d/TF2_Native_Worktrees/t63-clock || exit 1
OUT=evidence/t63-clock
mkdir -p "$OUT"
echo "=== build $(date -Is) ==="
bash build-target.sh tf2_demo_native demo_open_probe entity_overlay_probe projectile_track_probe > "$OUT/build3.log" 2>&1
BUILD_RC=$?
sha256sum native/build-nmake/tf2_demo_native.exe native/build-nmake/demo_open_probe.exe native/build-nmake/entity_overlay_probe.exe native/build-nmake/projectile_track_probe.exe > "$OUT/binaries3.sha256" 2>/dev/null
echo "BUILD_RC=$BUILD_RC"
echo "=== gate forward $(date -Is) ==="
bash demo-clock-check.sh > "$OUT/gate-forward3.log" 2>&1
GF=$?
echo "GATE_FORWARD_RC=$GF"
GM=99
if [ "$GF" = "0" ]; then
  echo "=== gate mutations $(date -Is) ==="
  bash demo-clock-check.sh --mutation > "$OUT/gate-mutation3.log" 2>&1
  GM=$?
  echo "GATE_MUTATION_RC=$GM"
else
  echo "GATE_MUTATION_RC=skipped-forward-red"
fi
echo "=== outline-check forward $(date -Is) ==="
bash outline-check.sh > "$OUT/outline-check3.log" 2>&1
ORC=$?
echo "OUTLINE_RC=$ORC"
echo "=== trajectory-overlay-check forward $(date -Is) ==="
bash trajectory-overlay-check.sh > "$OUT/trajectory-check3.log" 2>&1
TRC=$?
echo "TRAJ_RC=$TRC"
echo "=== done $(date -Is) ==="
echo "BUILD_RC=$BUILD_RC GATE_FORWARD_RC=$GF GATE_MUTATION_RC=$GM OUTLINE_RC=$ORC TRAJ_RC=$TRC"
echo "--- gate-forward3 tail ---"; tail -25 "$OUT/gate-forward3.log" 2>/dev/null
echo "--- outline tail ---"; tail -8 "$OUT/outline-check3.log" 2>/dev/null
echo "--- trajectory tail ---"; tail -8 "$OUT/trajectory-check3.log" 2>/dev/null
