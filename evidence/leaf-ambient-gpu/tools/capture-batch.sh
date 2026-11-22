#!/usr/bin/env bash
# T105/task-37 Half B (leaf37d): one lock acquisition covering the rebuild, the
# probe self-test, the five pins, the leaf trace capture and the second map.
# RUNS UNDER tools/lockrun.sh: nothing here may write evidence/ or the fixtures.
set -u
cd /d/TF2_Native_Worktrees/t105-leaf37b || exit 1
OUT=.scratch/leafgpu
EXE=native/build-nmake/tf2_demo_native.exe
PROBE=native/build-nmake/leaf_ambient_probe.exe
PROC="E:/demo测试/cp_process_f12/STV/1338f5de79ea343682c17dd514c980d9_matcha-20260927-1312-cp_process_f12.dem"
SUN="E:/demo测试/cp_sunshine/STV/SUNSHINE.dem"
mkdir -p "$OUT"

echo "=== touch edited sources (the edit tool preserves mtime; nmake reads mtime) ==="
touch native/src/native_renderer_frame.cpp native/src/native_renderer_entity.cpp native/include/native_renderer.h
stat -c '%y %n' native/src/native_renderer_frame.cpp native/src/native_renderer_entity.cpp native/include/native_renderer.h

echo "=== build tf2_demo_native leaf_ambient_probe ==="
bash build-target.sh tf2_demo_native leaf_ambient_probe
BRC=$?
echo "BUILD-RC=$BRC"
if [ "$BRC" -ne 0 ]; then echo "=== batch aborted: build failed ==="; exit "$BRC"; fi
grep -ci 'warning' .scratch/build-target.log | sed 's/^/BUILD-WARNING-LINES=/' || true

echo "=== self-test ==="
rm -f "$OUT/self-test.txt"
"$PROBE" --self-test > "$OUT/self-test.txt" 2>&1
echo "SELFTEST-RC=$?"
grep -c '"ok":true' "$OUT/self-test.txt" | sed 's/^/SELFTEST-PASSED=/'
grep -c '"ok":false' "$OUT/self-test.txt" | sed 's/^/SELFTEST-FAILED=/'
tail -n 3 "$OUT/self-test.txt"

run() {
  local name="$1"; shift
  rm -f "$OUT/$name.bmp"
  "$EXE" --demo "$PROC" --capture-tick 600 --capture-frame "$OUT/$name.bmp" --audio-disabled --hidden-window "$@" > "$OUT/$name.log" 2>&1
  local rc=$?
  local md5="-"
  [ -f "$OUT/$name.bmp" ] && md5=$(md5sum "$OUT/$name.bmp" | cut -d' ' -f1)
  echo "PIN name=$name rc=$rc md5=$md5 args=$*"
  grep -m1 'capture: lights' "$OUT/$name.log" || echo "PIN name=$name MISSING-lights-line"
  grep -m1 'capture: camera' "$OUT/$name.log" || echo "PIN name=$name MISSING-camera-line"
  grep -m1 'capture: model-lighting' "$OUT/$name.log" || echo "PIN name=$name MISSING-modellighting-line"
  grep -m1 'capture: leaf-ambient' "$OUT/$name.log" || echo "PIN name=$name MISSING-leafline"
  grep -m1 'lighting-trace: gate closed' "$OUT/$name.log" || true
  grep -m1 'renderer-invariant' "$OUT/$name.log" || true
}

run bare
run off --lighting off
run basic --lighting basic
run leaf --lighting leaf
run leaf-trace --lighting leaf --lighting-trace

echo "=== second map: cp_sunshine, leaf arm + trace ==="
rm -f "$OUT/sunshine-leaf.bmp"
"$EXE" --demo "$SUN" --capture-tick 600 --capture-frame "$OUT/sunshine-leaf.bmp" --audio-disabled --hidden-window --lighting leaf --lighting-trace > "$OUT/sunshine-leaf.log" 2>&1
echo "SUN-RC=$?"
[ -f "$OUT/sunshine-leaf.bmp" ] && md5sum "$OUT/sunshine-leaf.bmp" | sed 's/^/SUN-MD5=/'
grep -m1 'capture: lights' "$OUT/sunshine-leaf.log" || echo "SUN MISSING-lights-line"
grep -m1 'capture: camera' "$OUT/sunshine-leaf.log" || echo "SUN MISSING-camera-line"
grep -m1 'capture: leaf-ambient' "$OUT/sunshine-leaf.log" || echo "SUN MISSING-leafline"
grep -c 'lighting-leaf:' "$OUT/sunshine-leaf.log" | sed 's/^/SUN-TRACE-LEAF-LINES=/'
grep -m1 'renderer-invariant' "$OUT/sunshine-leaf.log" || true

echo "=== batch done ==="
