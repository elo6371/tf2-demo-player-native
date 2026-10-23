#!/usr/bin/env bash
# handoff-probe.sh -- does the guard's foreground hand-off shorten the product's run?
#
# The control phase of run-20261023-104925 ended at tick 323 with no capture frame,
# while the forward phase of the same run reached tick 400 and captured. The only
# difference in the control phase is a visible product window and the guard handing it
# the foreground. This runs the same product twice, once with the hand-off and once
# without, and reports where each run stopped. No build, no gate assertions.
set -uo pipefail
cd /d/TF2_Native_Test || exit 90
OUT=.scratch/fullscreen-quiet
mkdir -p "$OUT"
EXE=native/build-nmake/tf2_demo_native.exe
TF="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
BAGEL="D:/TF2_Demo_Player/testdata/demos/4a9bfb9276509d0ec5f5fdc722a95b17_match-20260927-0239-koth_bagel_rc13.dem"

run_variant() { # run_variant <name> <handOffMs>
  local name="$1" handoff="$2"
  local pfx="$OUT/probe-$name"
  rm -f "$pfx.trace" "$pfx.bmp" "$pfx.csv" "$pfx.guard.txt"
  powershell -NoProfile -ExecutionPolicy Bypass -File desktop-guard.ps1 \
    -Seconds 30 -OutFile "$(cygpath -m "$pfx.guard.txt")" -Watch tf2_demo_native \
    -HandOffMs "$handoff" -Phase "probe-$name" > "$pfx.guard.log" 2>&1 &
  local gpid=$!
  sleep 1
  TF2_NATIVE_TRACE="$(cygpath -m "$pfx.trace")" timeout 120 "$EXE" \
    --tf-root "$TF" --capture-tick 400 --demo "$BAGEL" \
    --capture-frame "$pfx.bmp" --metrics-file "$pfx.csv" --audio-disabled > "$pfx.log" 2>&1
  local rc=$?
  wait "$gpid" 2>/dev/null
  local last_tick last_iter cap bmp hand
  last_tick=$(grep 'loop n=' "$pfx.trace" 2>/dev/null | tail -1 | sed 's/.*tick=\([0-9-]*\).*/\1/')
  last_iter=$(grep 'loop n=' "$pfx.trace" 2>/dev/null | tail -1 | sed 's/.*n=\([0-9]*\).*/\1/')
  cap=$(grep -c 'capture: armed' "$pfx.trace" 2>/dev/null)
  bmp=$(stat -c %s "$pfx.bmp" 2>/dev/null || echo 0)
  hand=$(sed -n 's/^guard.handOffOk=\(.*\)/\1/p' "$pfx.guard.txt" 2>/dev/null)
  echo "PROBE name=$name handOffMs=$handoff rc=$rc last_tick=${last_tick:-none} last_iter=${last_iter:-none} capture_armed=${cap:-0} bmp_bytes=$bmp handOffOk=${hand:-ABSENT}"
}

run_variant with-handoff 3000
run_variant without-handoff 999999
echo "PROBE-DONE"
