#!/usr/bin/env bash
# stale-lock-selftest.sh -- static proof that verify-lock.sh cleans up after a
# SIGKILLed verify-all run and still refuses a live one.
#
# No product, no build, no host lock: the "owner" processes here are sleep(1)
# children of this script. The three failures that matter:
#   - a live owner must be refused (rc=2),
#   - a SIGKILLed owner must be reclaimed on the next acquire (the 22:56 case),
#   - an empty lock directory with no owner file must be reclaimed too.
#
# Usage: bash stale-lock-selftest.sh
# Exit: 0 all assertions held, 1 one did not.
# Token: STALE-LOCK-SELFTEST=PASS|FAIL assertions=N
set -uo pipefail
cd "$(dirname "$0")" || exit 2
. ./verify-lock.sh
VLOCK_HEARTBEAT_SECONDS=1
export VLOCK_HEARTBEAT_SECONDS

W=$(mktemp -d)
HOLDERS=()
cleanup() {
  local h
  for h in "${HOLDERS[@]:-}"; do [ -n "$h" ] && kill -9 "$h" 2>/dev/null; done
  rm -rf "$W"
}
trap cleanup EXIT INT TERM

failures=0
count=0
ok() { count=$((count + 1)); printf '  OK   %s\n' "$1"; }
bad() { count=$((count + 1)); failures=$((failures + 1)); printf '  FAIL %s\n' "$1"; }
expect_rc() { if [ "$2" = "$3" ]; then ok "$1 (rc=$3)"; else bad "$1 (rc=$2, expected $3)"; fi; }
expect_has() { if printf '%s' "$2" | grep -qaF "$3"; then ok "$1"; else bad "$1 (no '$3' in: $2)"; fi; }
expect_lacks() { if printf '%s' "$2" | grep -qaF "$3"; then bad "$1 (found '$3')"; else ok "$1"; fi; }
expect_file() { if [ -f "$2" ]; then ok "$1"; else bad "$1 (missing $2)"; fi; }

echo "=== stale-lock selftest (no product, no build) ==="

L="$W/lock"
out=$(vlock_acquire "$L" owner-a 900); rc=$?
expect_rc "L1 first acquire succeeds" "$rc" 0
expect_has "L1 acquire names the owner" "$out" "VERIFY-LOCK=ACQUIRED owner=owner-a"
expect_file "L1 owner file written" "$L/owner"
expect_file "L1 pid file written" "$L/pid"
expect_file "L1 heartbeat file written" "$L/heartbeat"
expect_has "L1 pid file holds this process" "$(cat "$L/pid")" "$$"

out=$(vlock_acquire "$L" owner-b 900); rc=$?
expect_rc "L2 live owner is refused" "$rc" 2
expect_has "L2 refusal is named" "$out" "VERIFY-LOCK=FAIL"
expect_has "L2 refusal names the live owner" "$out" "owner=owner-a"
expect_lacks "L2 live owner is not reclaimed" "$out" "STALE-LOCK=RECLAIMED"
vlock_release "$L" >/dev/null 2>&1

# L3: a holder killed with SIGKILL leaves the directory behind; the next acquire
# must reclaim it (this is the 22:56 incident, reproduced).
L3="$W/lock-killed"
bash -c '. ./verify-lock.sh; vlock_acquire "$1" holder-sigkill 900 >/dev/null 2>&1; sleep 300' _ "$L3" &
HOLDERS+=("$!")
sleep 1
expect_file "L3 killed-holder lock exists before the kill" "$L3/pid"
holder_pid=$(cat "$L3/pid")
kill -9 "$holder_pid" 2>/dev/null
sleep 2
out=$(vlock_acquire "$L3" owner-c 900); rc=$?
expect_rc "L3 SIGKILLed owner is reclaimed" "$rc" 0
expect_has "L3 reclaim is named" "$out" "STALE-LOCK=RECLAIMED owner=holder-sigkill"
expect_has "L3 reason is owner-dead" "$out" "reason=owner-dead"
vlock_release "$L3" >/dev/null 2>&1

# L4: live pid, stale heartbeat -> reclaimed on the heartbeat rule, not the pid.
L4="$W/lock-stale-hb"
mkdir -p "$L4"
printf 'owner-d\n' > "$L4/owner"
printf '%s\n' "$$" > "$L4/pid"
printf '%s\n' "$(( $(date +%s) - 1000 ))" > "$L4/started"
printf '%s\n' "$(( $(date +%s) - 1000 ))" > "$L4/heartbeat"
out=$(vlock_acquire "$L4" owner-e 900); rc=$?
expect_rc "L4 stale heartbeat is reclaimed" "$rc" 0
expect_has "L4 reason is heartbeat-timeout" "$out" "reason=heartbeat-timeout"
expect_has "L4 age is reported" "$out" "age="
vlock_release "$L4" >/dev/null 2>&1

# L5: the 22:56 shape exactly -- a lock directory with no files at all.
L5="$W/lock-empty"
mkdir -p "$L5"
out=$(vlock_acquire "$L5" owner-f 900); rc=$?
expect_rc "L5 empty lock directory is reclaimed" "$rc" 0
expect_has "L5 empty dir reason" "$out" "reason=no-owner-file"
expect_has "L5 empty dir pid reported as none" "$out" "pid=none"
vlock_release "$L5" >/dev/null 2>&1

# L6: release removes the directory and stops the heartbeat child.
L6="$W/lock-release"
vlock_acquire "$L6" owner-g 900 >/dev/null
hb=$(cat "$L6/heartbeat.pid" 2>/dev/null || printf '')
vlock_release "$L6" >/dev/null
if [ -d "$L6" ]; then bad "L6 release removes the directory"; else ok "L6 release removes the directory"; fi
sleep 2
if [ -n "$hb" ] && kill -0 "$hb" 2>/dev/null; then bad "L6 heartbeat child stops after release"; else ok "L6 heartbeat child stops after release"; fi

# L7: release from a process that does not own the lock must leave it alone.
L7="$W/lock-notmine"
mkdir -p "$L7"; printf 'someone-else\n' > "$L7/owner"; printf '%s\n' "$$" > "$L7/pid"
out=$(bash -c '. ./verify-lock.sh; vlock_release "$1"' _ "$L7" 2>&1)
expect_has "L7 non-owner release is skipped" "$out" "VERIFY-LOCK=RELEASE-SKIPPED"
if [ -d "$L7" ]; then ok "L7 foreign lock survives a bad release"; else bad "L7 foreign lock was deleted by a non-owner"; fi

echo
if [ "$failures" -ne 0 ]; then
  echo "STALE-LOCK-SELFTEST=FAIL assertions=$count failures=$failures"
  exit 1
fi
echo "STALE-LOCK-SELFTEST=PASS assertions=$count"
