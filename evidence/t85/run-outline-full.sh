#!/usr/bin/env bash
# T85 P0 acceptance, LEASE 1 of 2: outline-check full (no --mutation).
#
# Why it is its own lease (lead m01584): the full outline-check is ~25 min on this
# host, and a batch that also ran third-person/body/determinism would sit on the
# machine lock for ~45 min. The exe does not change between the two leases, so
# splitting loses no coverage: this lease runs the FINAL binary.
#
# Everything below is evidence: driver sha256, gate sha256, exe sha256+mtime,
# branch/head, the lock state before and after, rc, assertion counts and the
# budget ledgers actually printed by this run.
set -uo pipefail
cd "$(dirname "$0")/../.." || exit 90
ROOT=$(pwd)
OUT=evidence/t85/acceptance-outline
LOG="$OUT/lease.log"
rm -rf "$OUT"; mkdir -p "$OUT"
exec 3>&1
exec >> "$LOG" 2>&1

echo "=== T85 LEASE 1/2 outline-check full $(date '+%F %T') ==="
echo "driver sha256=$(sha256sum "$0" | cut -d' ' -f1)"
echo "root=$ROOT"
echo "branch=$(git rev-parse --abbrev-ref HEAD) head=$(git rev-parse HEAD)"
echo "gate outline-check.sh sha256=$(sha256sum outline-check.sh | cut -d' ' -f1) lines=$(wc -l < outline-check.sh) asserts=$(grep -cE '^[[:space:]]*assert_(eq|ne|ge|le|lt|contains)\b' outline-check.sh)"
echo "exe sha256=$(sha256sum native/build-nmake/tf2_demo_native.exe | cut -d' ' -f1) mtime=$(stat -c '%y' native/build-nmake/tf2_demo_native.exe)"
echo "probe sha256=$(sha256sum native/build-nmake/entity_overlay_probe.exe | cut -d' ' -f1)"
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
timeout --kill-after=60 2700 env OUTLINE_OUT="$OUT/outline" bash outline-check.sh
RC=$?
WALL=$((SECONDS-START))
echo "=== outline-check rc=$RC wall_seconds=$WALL ==="
echo "gate sha256 after=$(sha256sum outline-check.sh | cut -d' ' -f1)"

echo "--- assertion counts ---"
echo "OK lines in log: $(grep -c '^  OK   ' "$LOG")"
echo "FAIL lines in log: $(grep -c '^  FAIL' "$LOG")"
echo "assertions-ok=$(grep -o 'assertions OK=[0-9]*' "$LOG" | tail -1)"
echo "assertions-fail=$(grep -o 'FAIL=[0-9]*' "$LOG" | tail -1)"
echo "verdict=$(grep -oE 'OUTLINE-CHECK=(PASS|FAIL|PARTIAL)[^ ]*' "$LOG" | tail -1)"

echo "--- the four budget layers as printed by THIS run ---"
for pattern in 'budget: prepare' 'budget: meshTable' 'budget: instances' 'budget: draw' 'gpu: models done' 'attach: meshSource' 'attach: coverage' 'scene: weaponAttach'; do
  echo "[$pattern] $(grep -a "$pattern" "$OUT/outline/on.trace" 2>/dev/null | tail -1)"
done

echo "lock-after: $(bash /d/TF2_Native_Worktrees/_shared/hostlock.sh status 2>&1 | tr '\n' ' ')"
PP=$(bash /d/TF2_Native_Worktrees/_shared/hostlock.sh status 2>&1 | sed -n 's/^product_processes=//p')
if [ "${PP:-x}" = "0" ]; then echo "LEASE1-PRODUCT-PROCESSES=PASS"; else echo "LEASE1-PRODUCT-PROCESSES=FAIL (product_processes=${PP:-unknown})"; fi
if [ "$RC" = "0" ] && [ "$(grep -c '^  FAIL' "$LOG")" = "0" ]; then echo "LEASE1=PASS"; else echo "LEASE1=FAIL"; fi
echo "tail:"
tail -n 120 "$LOG" >&3 2>/dev/null || true
