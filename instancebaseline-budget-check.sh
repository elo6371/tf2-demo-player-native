#!/usr/bin/env bash
# instancebaseline-budget-check.sh -- pin the instancebaseline retention cap.
#
# WHY THIS EXISTS
#   The demo string-table decoder used to retain every instancebaseline entry
#   the payload declared. Two synthetic demos show the old behaviour:
#     over-1025.dem        -> instance_baselines=1025
#     over-global-2048.dem -> instance_baselines=2048
#   Both packets are otherwise well formed, so the old decoder accepted them.
#   The fix caps the map at 1024 entries and rejects a baseline packet that
#   declares more than 1024 entries before allocating entries for it.
#
# WHAT THIS GATE ASSERTS
#   The three fixtures' sha256, then the probe's named counters:
#     cap-1024       -> rc=0, packets=1, malformed=0, baselines=1024
#     over-1025      -> rc=0, packets=1, malformed=1, baselines=0
#     over-global    -> rc=0, packets=2, malformed=1, baselines=1024
#     bagel rc13     -> rc=0, malformed=0, baselines=126, misses=16, applied=10035
#   The bagel row is the no-regression control: the cap must not reject a real
#   demo's 126 baselines.
#
# WHAT THIS GATE DOES NOT CLAIM
#   It does not claim the product path uses this decoder (the product's own
#   entity path is gated elsewhere), that the 64 MiB byte cap is reachable by a
#   fixture (the 1024 entry cap is tighter: 1024 * 65535 < 64 MiB), or that the
#   cap is wired into verify-all/verify-fast. It is chain-external (D-103).
#
# NEGATIVE CONTROL
#   Pass --probe <pre-fix entity_protocol_probe.exe>. The old binary must fail
#   over-1025 (actual 1025) and over-global (actual 2048); the gate exits 1.
#   That is the red half of the evidence. It is not a separate mode.
#
# INDEPENDENT BY DESIGN
#   It stages only its own output under .scratch/instancebaseline-budget and
#   reads committed fixtures. It does not acquire hostlock (hostlock is not
#   reentrant); the caller must hold it and this script refuses with exit 9.
#
# Usage:
#   bash /d/TF2_Native_Worktrees/_shared/hostlock.sh run <owner> \
#     env INSTANCEBASELINE_BUDGET_OWNER=<owner> bash instancebaseline-budget-check.sh
#
# Options:
#   --probe PATH     entity_protocol_probe.exe to test
#   --fixtures DIR   fixture directory (default testdata/fixtures/instancebaseline-budget)
#   --bagel PATH     bagel rc13 demo used by the no-regression row
#   --out DIR        scratch directory (default .scratch/instancebaseline-budget)
#   --keep           accepted for compatibility; the scratch tree is always retained
#
# Env:
#   INSTANCEBASELINE_SKIP_ONE=1  selftest only: skip the last assertion; the
#                                gate must then FAIL because 19 < 20.
#
# Exit: 0 = 20/20 assertions held; 1 = an assertion failed; 9 = lock not held.

set -uo pipefail
cd "$(dirname "$0")" || exit 9
export LC_ALL=C

PROBE="native/build-nmake/entity_protocol_probe.exe"
FIXTURES="testdata/fixtures/instancebaseline-budget"
BAGEL="D:/TF2_Demo_Player/testdata/demos/4a9bfb9276509d0ec5f5fdc722a95b17_match-20260927-0239-koth_bagel_rc13.dem"
OUT=".scratch/instancebaseline-budget"
KEEP=0
HOSTLOCK="${HOSTLOCK:-/d/TF2_Native_Worktrees/_shared/hostlock.sh}"
OWNER="${INSTANCEBASELINE_BUDGET_OWNER:-}"

while [ "$#" -gt 0 ]; do
  case "$1" in
    --probe) PROBE="$2"; shift 2 ;;
    --fixtures) FIXTURES="$2"; shift 2 ;;
    --bagel) BAGEL="$2"; shift 2 ;;
    --out) OUT="$2"; shift 2 ;;
    --keep) KEEP=1; shift ;;
    *) echo "instancebaseline-budget-check: unknown argument: $1" >&2; exit 2 ;;
  esac
done

if [ -z "$OWNER" ]; then
  echo "INSTANCEBASELINE-BUDGET=FAIL lock=not-held reason=owner-not-set" >&2
  echo "  run it as: bash $HOSTLOCK run <owner> env INSTANCEBASELINE_BUDGET_OWNER=<owner> bash $0" >&2
  exit 9
fi
if [ ! -f "$HOSTLOCK" ]; then
  echo "INSTANCEBASELINE-BUDGET=FAIL lock=not-held reason=hostlock-script-missing path=$HOSTLOCK"
  exit 9
fi
lock_status="$(bash "$HOSTLOCK" status 2>&1)"
held="$(printf '%s\n' "$lock_status" | sed -n 's/^LOCKED owner=\([^ ]*\).*/\1/p' | head -1)"
if [ "$held" != "$OWNER" ]; then
  echo "INSTANCEBASELINE-BUDGET=FAIL lock=not-held owner=$OWNER held=${held:-none}" >&2
  echo "$lock_status" | sed 's/^/  lock: /' >&2
  exit 9
fi

rm -rf "$OUT"
mkdir -p "$OUT"

ASSERTIONS=0
FAILED=0
EXPECTED_ASSERTIONS=20
pass() { ASSERTIONS=$((ASSERTIONS + 1)); printf 'OK %s\n' "$*"; }
fail() { ASSERTIONS=$((ASSERTIONS + 1)); FAILED=$((FAILED + 1)); printf 'FAIL %s\n' "$*"; }
assert_eq() {
  local name="$1" actual="$2" expected="$3"
  if [ "$actual" = "$expected" ]; then
    pass "$name=$actual"
  else
    fail "$name expected=$expected actual=$actual"
  fi
}
file_sha() {
  local path="$1"
  if [ ! -f "$path" ]; then printf 'missing'; return; fi
  sha256sum "$path" | cut -d' ' -f1
}
field() {
  local key="$1" path="$2"
  awk -v k="$key" '{
    for (i = 1; i <= NF; ++i) {
      if (index($i, k "=") == 1) { sub("^" k "=", "", $i); print $i; exit }
    }
  }' "$path"
}
run_case() {
  local name="$1" demo="$2"
  local rc=0
  "$PROBE" "$demo" > "$OUT/$name.txt" 2> "$OUT/$name.err" || rc=$?
  printf '%s\n' "$rc" > "$OUT/$name.rc"
  cp -p "$demo" "$OUT/$name.dem" 2>/dev/null || true
}
finish() {
  echo
  if [ "$FAILED" -eq 0 ] && [ "$ASSERTIONS" -ge "$EXPECTED_ASSERTIONS" ]; then
    echo "INSTANCEBASELINE-BUDGET=PASS ($ASSERTIONS assertions)"
    echo "out=$OUT (retained after PASS)"
    exit 0
  fi
  if [ "$FAILED" -ne 0 ]; then
    echo "INSTANCEBASELINE-BUDGET=FAIL ($FAILED/$ASSERTIONS failed)"
  else
    echo "INSTANCEBASELINE-BUDGET=FAIL ($ASSERTIONS assertions, expected at least $EXPECTED_ASSERTIONS; an assertion block was skipped)"
  fi
  exit 1
}

echo "head=$(git rev-parse HEAD 2>/dev/null || echo unknown)"
echo "head_short=$(git rev-parse --short=12 HEAD 2>/dev/null || echo unknown)"
echo "tree_dirty=$(git status --porcelain 2>/dev/null | wc -l | tr -d ' ')"
echo "expected_assertions=$EXPECTED_ASSERTIONS"
echo "probe: $PROBE"
if [ ! -f "$PROBE" ]; then
  echo "INSTANCEBASELINE-BUDGET=FAIL probe=missing path=$PROBE"
  exit 1
fi
echo "probe_sha256=$(sha256sum "$PROBE" | cut -d' ' -f1)"
echo "probe_bytes=$(stat -c %s "$PROBE" 2>/dev/null || echo unknown)"
echo "fixtures: $FIXTURES"
echo "bagel: $BAGEL"
echo

CAP="$FIXTURES/cap-1024.dem"
OVER="$FIXTURES/over-1025.dem"
GLOBAL="$FIXTURES/over-global-2048.dem"
echo "fixture_sha256.cap-1024=$(file_sha "$CAP")"
echo "fixture_sha256.over-1025=$(file_sha "$OVER")"
echo "fixture_sha256.over-global-2048=$(file_sha "$GLOBAL")"
echo "bagel_sha256=$(file_sha "$BAGEL")"

# 1-3: the fixtures themselves.
assert_eq "fixture.cap-1024.sha256" "$(file_sha "$CAP")" "5174bd8c71fcda009de249e7fb008b78e212a52e48ca16b3894d0e6dca377acc"
assert_eq "fixture.over-1025.sha256" "$(file_sha "$OVER")" "070789953a7d0536887cee551b2ee4c48f169db5d2b80c29ca9e657b309c494f"
assert_eq "fixture.over-global-2048.sha256" "$(file_sha "$GLOBAL")" "ae6fedfe6a364ee3c02a3feb7c453d1e517cb8762b2d3d89297f9e3e5bf68596"

if [ ! -f "$CAP" ] || [ ! -f "$OVER" ] || [ ! -f "$GLOBAL" ]; then
  echo "INSTANCEBASELINE-BUDGET=FAIL reason=fixture-missing"
  finish
fi

# 4-7: cap-1024 is the positive control; it must still be accepted in full.
run_case cap-1024 "$CAP"
assert_eq "cap-1024.rc" "$(cat "$OUT/cap-1024.rc")" "0"
assert_eq "cap-1024.packets" "$(field packets_scanned "$OUT/cap-1024.txt")" "1"
assert_eq "cap-1024.malformed" "$(field malformed_packets "$OUT/cap-1024.txt")" "0"
assert_eq "cap-1024.instance_baselines" "$(field instance_baselines "$OUT/cap-1024.txt")" "1024"

# 8-11: one entry over the cap must be rejected as a malformed table, not
# retained. The old decoder reports 1025 here.
run_case over-1025 "$OVER"
assert_eq "over-1025.rc" "$(cat "$OUT/over-1025.rc")" "0"
assert_eq "over-1025.packets" "$(field packets_scanned "$OUT/over-1025.txt")" "1"
assert_eq "over-1025.malformed" "$(field malformed_packets "$OUT/over-1025.txt")" "1"
assert_eq "over-1025.instance_baselines" "$(field instance_baselines "$OUT/over-1025.txt")" "0"

# 12-15: a second packet that would grow the map past the cap must stop at
# 1024 retained entries, not 2048. The first packet is still accepted.
run_case over-global-2048 "$GLOBAL"
assert_eq "over-global-2048.rc" "$(cat "$OUT/over-global-2048.rc")" "0"
assert_eq "over-global-2048.packets" "$(field packets_scanned "$OUT/over-global-2048.txt")" "2"
assert_eq "over-global-2048.malformed" "$(field malformed_packets "$OUT/over-global-2048.txt")" "1"
assert_eq "over-global-2048.instance_baselines" "$(field instance_baselines "$OUT/over-global-2048.txt")" "1024"

# 16-20: the real bagel demo is the no-regression control.
run_case bagel "$BAGEL"
assert_eq "bagel.rc" "$(cat "$OUT/bagel.rc")" "0"
assert_eq "bagel.malformed" "$(field malformed_packets "$OUT/bagel.txt")" "0"
assert_eq "bagel.instance_baselines" "$(field instance_baselines "$OUT/bagel.txt")" "126"
assert_eq "bagel.baseline_misses" "$(field baseline_misses "$OUT/bagel.txt")" "16"
if [ "${INSTANCEBASELINE_SKIP_ONE:-0}" = "1" ]; then
  echo "  selftest: INSTANCEBASELINE_SKIP_ONE=1 -> last assertion deliberately skipped (the floor must FAIL)"
else
  assert_eq "bagel.baseline_applied" "$(field baseline_applied "$OUT/bagel.txt")" "10035"
fi

finish
