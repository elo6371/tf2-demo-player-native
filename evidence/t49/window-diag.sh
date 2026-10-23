#!/usr/bin/env bash
# window-diag.sh -- one diagnostic run, no build, control-shaped product launch.
#
# WHY: the three-phase run reported watchVisibleSamples=0 while the product said
# window_visible=1. Three explanations fit that pairwise-or-not at all:
#   1. the product's window is not a top-level window (EnumWindows cannot see it),
#   2. it is top-level but not visible while the guard is sampling,
#   3. it belongs to a process the guard is not watching -- or it does not exist yet
#      while the guard is up.
# This run separates them with readings: a 42 s guard (longer than the product's whole
# run) plus an independent dumper that lists every top-level window and every window
# the watched pid owns, without the visibility filter.
#
# Run only through the host lock:
#   bash /d/TF2_Native_Worktrees/_shared/hostlock.sh run t49-desk \
#     bash -c 'bash /d/TF2_Native_Test/evidence/t49/window-diag.sh'
set -uo pipefail
cd /d/TF2_Native_Test || exit 90

RUN="evidence/t49/diag-$(date +%Y%m%d-%H%M%S)"
mkdir -p "$RUN"
exec > >(tee -a "$RUN/diag.log") 2>&1

EXE=native/build-nmake/tf2_demo_native.exe
TF="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
BAGEL="D:/TF2_Demo_Player/testdata/demos/4a9bfb9276509d0ec5f5fdc722a95b17_match-20260927-0239-koth_bagel_rc13.dem"
OUT="$PWD/$RUN"

echo "DIAG-START $(date '+%Y-%m-%d %H:%M:%S')"
echo "guard: 42 s, watch=tf2_demo_native"
powershell -NoProfile -ExecutionPolicy Bypass -File desktop-guard.ps1 -Seconds 42 \
  -OutFile "$(cygpath -m "$OUT/guard.txt")" -Watch tf2_demo_native -Phase diag \
  > "$RUN/guard.log" 2>&1 &
gp=$!
sleep 1

echo "dumper: 30 s, every 2 s"
powershell -NoProfile -ExecutionPolicy Bypass -File .scratch/t49/window-dump.ps1 \
  -Out "$(cygpath -m "$OUT/windows.txt")" -Seconds 30 -Watch tf2_demo_native \
  > "$RUN/dump.log" 2>&1 &
dp=$!

echo "product: shown window (control configuration), --audio-disabled"
TF2_NATIVE_TRACE="$(cygpath -m "$OUT/product.trace")" timeout 180 "$EXE" \
  --tf-root "$TF" --capture-tick 400 --demo "$BAGEL" \
  --capture-frame "$OUT/product.bmp" --metrics-file "$OUT/product.csv" \
  --audio-disabled > "$RUN/product.log" 2>&1
prc=$?
echo "product rc=$prc"

wait "$gp" 2>/dev/null
kill "$dp" 2>/dev/null

echo "=== guard readings ==="
cat "$RUN/guard.txt" 2>/dev/null
echo "=== product trace, window lines ==="
grep -m1 'mode: window_created=' "$RUN/product.trace" 2>/dev/null
grep -m1 'mode: summary' "$RUN/product.trace" 2>/dev/null | cut -c1-160
echo "=== window dump: samples in which a tf2_demo_native window was seen ==="
grep -n 'OWNED\|WATCHED pids=\|### sample' "$RUN/windows.txt" 2>/dev/null | head -40
echo
echo "DIAG-DONE product_rc=$prc guard=$(grep -c . "$RUN/guard.txt" 2>/dev/null) run=$RUN"
