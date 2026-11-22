#!/usr/bin/env bash
# The probe source is modified against HEAD but nmake skipped it in the batch
# (its .obj was newer). Rebuild it from the committed-to-be text and re-run the
# self-test so the committed source is what produced the evidence.
set -u
cd /d/TF2_Native_Worktrees/t105-leaf37b || exit 1
OUT=.scratch/leafgpu
touch native/tools/leaf_ambient_probe.cpp
bash build-target.sh leaf_ambient_probe
echo "PROBE-BUILD-RC=$?"
echo "PROBE-WARNING-LINES=$(grep -ci 'warning' .scratch/build-target.log)"
stat -c 'PROBE-EXE %y %s' native/build-nmake/leaf_ambient_probe.exe
grep -c 'leaf_ambient_probe.cpp' .scratch/build-target.log | sed 's/^/PROBE-COMPILE-LINES=/'
native/build-nmake/leaf_ambient_probe.exe --self-test > "$OUT/self-test.txt" 2>&1
echo "SELFTEST-RC=$?"
echo "SELFTEST-PASSED=$(grep -c '\"ok\":true' "$OUT/self-test.txt")"
echo "SELFTEST-FAILED=$(grep -c '\"ok\":false' "$OUT/self-test.txt")"
tail -n 2 "$OUT/self-test.txt"
grep 'equalCubesEqualSlots' "$OUT/self-test.txt" | sed 's/^/CHECK /'
echo "PROBE-DONE"
