#!/usr/bin/env bash
# T63-F1 task-10 step 1+2: build the fixed tree and produce probe-only diagnosis
# readings. Runs under hostlock; releases on exit. Writes only evidence/t63-clock.
set -uo pipefail
ROOT=/d/TF2_Native_Worktrees/t63-clock
EV=$ROOT/evidence/t63-clock
HL=/d/TF2_Native_Worktrees/_shared/hostlock.sh
mkdir -p "$EV"
S1="D:/TF2_Demo_Player/testdata/demos/bb841c6d379ff7c40d0c8baf99f59d8d_matcha-20260927-1347-cp_snakewater_final1.dem"
S5="D:/TF2_Demo_Player/testdata/demos/4a9bfb9276509d0ec5f5fdc722a95b17_match-20260927-0239-koth_bagel_rc13.dem"
echo "=== acquire $(date -Is) ==="
bash "$HL" acquire t63-clock 3600 || { echo "ACQUIRE FAILED"; exit 1; }
trap 'echo "=== release $(date -Is) ==="; bash "$HL" release t63-clock' EXIT
cd "$ROOT" || exit 1
echo "=== build $(date -Is) ==="
bash build-target.sh demo_open_probe tf2_demo_native > "$EV/build.log" 2>&1
rc=$?
if [ $rc -ne 0 ]; then echo "BUILD FAILED rc=$rc"; tail -60 "$EV/build.log"; exit 1; fi
echo "build ok"
PROBE="$ROOT/native/build-nmake/demo_open_probe.exe"
ls -l "$PROBE" || { echo "PROBE MISSING"; exit 1; }
run() { local name=$1; shift; echo "--- $name ---"; "$PROBE" "$@" > "$EV/$name.txt" 2> "$EV/$name.err"; echo "rc=$? $(wc -c < "$EV/$name.txt") bytes"; }
run diag-S1-scan --scan "$S1"
run diag-S1-snapshot-30319 --scan --snapshot 30319 "$S1"
run diag-S1-camera-601 --scan --camera-tick 601 "$S1"
run diag-S1-camera-602 --scan --camera-tick 602 "$S1"
run diag-S5-scan --scan "$S5"
run diag-S5-snapshot-1 --scan --snapshot 1 "$S5"
run diag-S5-snapshot-56148 --scan --snapshot 56148 "$S5"
run diag-S5-snapshot-129277 --scan --snapshot 129277 "$S5"
run diag-S5-camera-601 --scan --camera-tick 601 "$S5"
echo "=== done $(date -Is) ==="
