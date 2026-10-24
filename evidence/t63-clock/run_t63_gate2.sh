#!/usr/bin/env bash
# T63-F1 gate run 2: POV demo A5 (SourceTV cmdinfo is all zeros), then forward
# gate, mutations, and the two downstream gates. Runs under hostlock.
set -uo pipefail
cd /d/TF2_Native_Worktrees/t63-clock
LOG=evidence/t63-clock
mkdir -p "$LOG"
echo "=== t63 gate run2 $(date +%FT%T) ==="
bash build-target.sh tf2_demo_native demo_open_probe entity_overlay_probe projectile_track_probe > "$LOG/build2.log" 2>&1
BUILD_RC=$?
echo "BUILD_RC=$BUILD_RC"
if [ "$BUILD_RC" -ne 0 ]; then tail -25 "$LOG/build2.log"; exit 1; fi
sha256sum native/build-nmake/tf2_demo_native.exe native/build-nmake/demo_open_probe.exe | tee "$LOG/binaries2.sha256"
PROBE=native/build-nmake/demo_open_probe.exe
EXE=native/build-nmake/tf2_demo_native.exe
TF="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
P1="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf/demos/autorecord_2026-09-10_14-04-13.dem"
"$PROBE" --scan --server-tick-at 600 "$P1" > "$LOG/p1-scan.txt" 2>&1
echo "P1_SCAN_RC=$?"
grep -a -E "recording=|first_data_entry_tick=|server_tick_at_600=|camera_track=" "$LOG/p1-scan.txt"
E=$(grep -a -o "first_data_entry_tick=[0-9-]*" "$LOG/p1-scan.txt" | tail -1 | cut -d= -f2)
echo "P1_ENTRY=$E"
if [ -z "$E" ]; then echo "P1_ENTRY_MISSING=1"; exit 3; fi
"$PROBE" --scan --camera-tick "$((E + 600))" "$P1" > "$LOG/p1-camera.txt" 2>&1
echo "P1_CAMERA_RC=$?"
grep -a "camera_" "$LOG/p1-camera.txt"
rm -f "$LOG/a5-emp.trace" "$LOG/a5-emp.bmp"
TF2_NATIVE_TRACE=$(cygpath -m "$PWD/$LOG/a5-emp.trace") timeout 900 "$EXE" --tf-root "$TF" --hidden-window --audio-disabled --settings-file "$(cygpath -m "$PWD/$LOG/a5-emp.settings.ini")" --ui-settings-file "$(cygpath -m "$PWD/$LOG/a5-emp.ui.ini")" --demo "$P1" --playback-speed 8 --capture-tick 600 --capture-frame "$(cygpath -m "$PWD/$LOG/a5-emp.bmp")" > "$LOG/a5-emp.log" 2>&1
echo "PRODUCT_RC=$?"
grep -a "scene: clock tick=600\|scene: camera mode=" "$LOG/a5-emp.trace" | tail -4
ORIGIN=$(grep -a -o "camera_origin=[^ ]*" "$LOG/p1-camera.txt" | tail -1 | cut -d= -f2)
EYE=$(grep -a "scene: camera mode=" "$LOG/a5-emp.trace" | tail -1 | grep -a -o "eye=[^ ]*" | cut -d= -f2)
echo "ORIGIN=$ORIGIN"
echo "EYE=$EYE"
EMP_RC=0
awk -v a="$ORIGIN" -v b="$EYE" 'BEGIN{ if(a==""||b==""){print "EMPIRICAL_MISSING"; exit 2} split(a,x,","); split(b,y,","); d=0; for(i=1;i<=3;i++){e=x[i]-y[i]; if(e<0)e=-e; if(e>d)d=e} printf "EMPIRICAL_MAXDIFF=%.3f\n", d; exit (d>0.5)?3:0 }' || EMP_RC=$?
echo "EMPIRICAL_RC=$EMP_RC"
if [ "$EMP_RC" -ne 0 ]; then echo "A5_EMPIRICAL_MISMATCH=1 stopping before gate"; exit 4; fi
bash demo-clock-check.sh > "$LOG/gate-forward2.log" 2>&1
GATE_RC=$?
echo "GATE_FORWARD_RC=$GATE_RC"
tail -45 "$LOG/gate-forward2.log"
if [ "$GATE_RC" -ne 0 ]; then echo "GATE_FORWARD_RED=1 skip mutations"; exit 5; fi
bash demo-clock-check.sh --mutation > "$LOG/gate-mutation2.log" 2>&1
MUT_RC=$?
echo "GATE_MUTATION_RC=$MUT_RC"
tail -55 "$LOG/gate-mutation2.log"
bash outline-check.sh > "$LOG/outline-check2.log" 2>&1
OUTLINE_RC=$?
echo "OUTLINE_RC=$OUTLINE_RC"
tail -25 "$LOG/outline-check2.log"
bash trajectory-overlay-check.sh > "$LOG/trajectory-check2.log" 2>&1
TRAJ_RC=$?
echo "TRAJ_RC=$TRAJ_RC"
tail -25 "$LOG/trajectory-check2.log"
echo "=== done $(date +%FT%T) ==="
