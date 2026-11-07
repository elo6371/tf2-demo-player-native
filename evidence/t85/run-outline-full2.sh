#!/usr/bin/env bash
# T85 P0 acceptance, LEASE 1b: rebuild (adds budget: instances submitted=) + full outline-check.
#
# Why a second lease: the first full run (evidence/t85/acceptance-outline/lease.log) was
# RED for two gate-side reasons, both fixed here:
#   * layer A had no readable line because trim_trace dropped every `budget:` line that
#     was not in its last-2000-line window (the prepare ledger is printed once, at
#     startup); the trimmer now keeps `budget:` lines.
#   * layer C compared accepted+refusals against `slots`, which is packed.size() (one row
#     per material run), not the submitted instance count. The product now publishes
#     `submitted=` (draws.size()) and the account closes there.
# The exe therefore has to be rebuilt in the SAME lease that runs the gate, so the
# gate never reads a stale binary.
#
# Everything below is evidence: driver/gate/exe/probe/demo sha256, branch/head, lock
# state before and after, build rc, gate rc, wall seconds, assertion counts and the
# budget ledgers the run actually printed.
set -uo pipefail
cd "$(dirname "$0")/../.." || exit 90
ROOT=$(pwd)
OUT=evidence/t85/acceptance-outline2
LOG="$OUT/lease.log"
rm -rf "$OUT"; mkdir -p "$OUT"
exec 3>&1
exec >> "$LOG" 2>&1

echo "=== T85 LEASE 1b rebuild + outline-check full $(date '+%F %T') ==="
echo "driver sha256=$(sha256sum "$0" | cut -d' ' -f1)"
echo "root=$ROOT"
echo "branch=$(git rev-parse --abbrev-ref HEAD) head=$(git rev-parse HEAD)"
echo "gate outline-check.sh sha256=$(sha256sum outline-check.sh | cut -d' ' -f1) lines=$(wc -l < outline-check.sh) asserts=$(grep -cE '^[[:space:]]*assert_(eq|ne|ge|le|lt|contains)\b' outline-check.sh)"
echo "main.cpp sha256=$(sha256sum native/src/main.cpp | cut -d' ' -f1)"
echo "exe-before sha256=$(sha256sum native/build-nmake/tf2_demo_native.exe | cut -d' ' -f1)"
echo "demo sha256=$(sha256sum D:/TF2_Demo_Player/testdata/demos/bb841c6d33018e2e9328a4cdd5d7ede0_match-20260924-2337-cp_snakewater_final1.dem 2>/dev/null | cut -d' ' -f1)"
echo "lock-before: $(bash /d/TF2_Native_Worktrees/_shared/hostlock.sh status 2>&1 | tr '\n' ' ')"

cleanup() {
  local status=$? tree n i
  echo "CLEANUP at ${SECONDS}s (status=${status})"
  tree=$(ps -eo pid=,ppid= | awk -v root=$$ '$2==root {print $1}')
  n=0
  for i in $(seq 1 6); do
    [ -z "${tree}" ] && break
    for p in ${tree}; do kill -TERM "$p" 2>/dev/null; n=$((n+1)); done
    sleep 1
    tree=$(ps -eo pid=,ppid= | awk -v set="${tree}" 'BEGIN{for(i=1;i<=split(set,a," ");i++) if(a[i]!="") owner[a[i]]=1} owner[$2]{print $1}')
  done
  echo "CLEANUP: sent TERM to N=${n} descendant(s)"
  exit "${status}"
}
trap cleanup EXIT
trap 'echo "INTERRUPTED by signal"; exit 130' INT TERM

START=${SECONDS}
echo "--- build: bash build-target.sh tf2_demo_native entity_overlay_probe ---"
timeout --kill-after=60 900 bash build-target.sh tf2_demo_native entity_overlay_probe
BUILD_RC=$?
echo "build rc=$BUILD_RC wall_seconds=$((SECONDS-START))"
echo "exe-after sha256=$(sha256sum native/build-nmake/tf2_demo_native.exe | cut -d' ' -f1) mtime=$(stat -c '%y' native/build-nmake/tf2_demo_native.exe)"
echo "probe sha256=$(sha256sum native/build-nmake/entity_overlay_probe.exe | cut -d' ' -f1)"
if [ "$BUILD_RC" != "0" ]; then echo "LEASE1B=FAIL (build rc=$BUILD_RC)"; tail -n 80 "$LOG" >&3; exit 1; fi

START=${SECONDS}
echo "--- gate: bash outline-check.sh (full, no --mutation) ---"
timeout --kill-after=60 2700 env OUTLINE_OUT="$OUT/outline" bash outline-check.sh
RC=$?
WALL=$((SECONDS-START))
echo "=== outline-check rc=$RC wall_seconds=$WALL ==="
echo "gate sha256 after=$(sha256sum outline-check.sh | cut -d' ' -f1)"

echo "--- assertion counts ---"
echo "OK lines in log: $(grep -c '^  OK   ' "$LOG")"
echo "FAIL lines in log: $(grep -c '^  FAIL' "$LOG")"
echo "verdict=$(grep -oE 'OUTLINE-CHECK=(PASS|FAIL|PARTIAL)[^ ]*' "$LOG" | tail -1)"
if [ "$(grep -c '^  FAIL' "$LOG")" != "0" ]; then echo "--- FAIL lines ---"; grep -n '^  FAIL' "$LOG"; fi

echo "--- the budget layers as printed by THIS run (on.trace) ---"
for pattern in 'budget: prepare' 'budget: meshTable' 'budget: instances' 'budget: draw' 'gpu: models done' 'attach: meshSource' 'attach: coverage' 'scene: weaponAttach'; do
  echo "[$pattern] $(grep -a "$pattern" "$OUT/outline/on.trace" 2>/dev/null | tail -1)"
done
echo "--- world fixture trace ---"
for pattern in 'budget: prepare' 'budget: meshTable' 'budget: instances' 'attach: coverage' 'scene: weaponAttach' 'gpu: models done'; do
  echo "[world $pattern] $(grep -a "$pattern" "$OUT/outline/world.trace" 2>/dev/null | tail -1)"
done
echo "--- speed invariance as measured this run ---"
echo "speed1 bmp=$(stat -c%s "$OUT/outline/speed1.bmp" 2>/dev/null || echo 0) on bmp=$(stat -c%s "$OUT/outline/on.bmp" 2>/dev/null || echo 0)"
grep -aE 'speed=1 rc=|CHANGES THE FRAME|\(cmp\)' "$LOG" || true

echo "lock-after: $(bash /d/TF2_Native_Worktrees/_shared/hostlock.sh status 2>&1 | tr '\n' ' ')"
PP=$(bash /d/TF2_Native_Worktrees/_shared/hostlock.sh status 2>&1 | sed -n 's/^product_processes=//p')
if [ "${PP:-x}" = "0" ]; then echo "LEASE1B-PRODUCT-PROCESSES=PASS"; else echo "LEASE1B-PRODUCT-PROCESSES=FAIL (product_processes=${PP:-unknown})"; fi
if [ "$RC" = "0" ] && [ "$(grep -c '^  FAIL' "$LOG")" = "0" ]; then echo "LEASE1B=PASS"; else echo "LEASE1B=FAIL"; fi
echo "tail:"
tail -n 100 "$LOG" >&3 2>/dev/null || true
