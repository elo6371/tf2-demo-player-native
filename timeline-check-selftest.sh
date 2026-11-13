#!/usr/bin/env bash
# timeline-check-selftest.sh -- prove the resume wait is a condition, not a frame.
#
# WHY THIS EXISTS
#   timeline-check.sh section 3 used to read the state on "the next frame" after
#   resume and require delta in [1, 2]. On 2026-11-08 that assertion went red on
#   the first run of the mutation baseline (resume -> state 9.0 ms apart, tick
#   15 ms at 1x, cursor unmoved) and green on the final baseline of the same tree
#   (50.3 ms apart, cursor +1). A gate that randomises on frame pacing is not a
#   gate. The fix asks the product for one state per frame (20 samples) and waits
#   for the first sample that moved; the upper bound comes from wall time, not
#   from a frame count.
#
#   This selftest feeds the gate three MOCKED trace files through a mock product
#   executable (TIMELINE_EXE override). No real product runs and no frame is
#   drawn, so it costs a second and can run anywhere:
#     delayed  the first 4 samples still show the pause tick, the 5th moves. The
#              gate must PASS and the OK line must name sample 5/20 -- a gate that
#              read only the first state would have failed here.
#     never    all 20 samples show the pause tick. The gate must FAIL and name
#              the sample cap (this is the "awaited, then timed out" branch).
#     replay   the first sample jumps 14 ticks, as a player repaying paused wall
#              time would. The gate must FAIL the no-replay clause.
#
# Exit: 0 = the wait logic behaved as designed in all three cases; 1 = otherwise.
# Run: bash timeline-check-selftest.sh   (no lock, no product, no GPU)

set -uo pipefail
cd "$(dirname "$0")"

TMP=$(mktemp -d)
cleanup_tmp() { [ -n "${TMP:-}" ] && rm -rf "$TMP"; }
trap cleanup_tmp EXIT INT TERM

mkdir -p "$TMP/tf" "$TMP/out"
: > "$TMP/demo.dem"

MOCK="$TMP/mock-product.sh"
cat > "$MOCK" <<'MOCKEOF'
#!/usr/bin/env bash
# Mock product for timeline-check-selftest.sh: writes the trace section 3 parses.
# TIMELINE_MOCK_CASE selects which sample sequence to print.
trace="${TF2_NATIVE_TRACE:?mock product needs TF2_NATIVE_TRACE}"
case_name="${TIMELINE_MOCK_CASE:-delayed}"
{
  # Timestamps are traceNowMs(): MILLISECONDS since process start with one decimal
  # (native/src/main_trace.cpp:40). Keep the mock in the product's unit so a gate
  # that forgets the ms->s conversion fails the selftest instead of the field.
  echo "19621.3 camera: accept step=1 cmd=pause result=ok detail=paused"
  echo "19621.3 expect what=same-tick marked=16 actual=16 match=1"
  echo "19621.3 camera: accept step=4 cmd=resume result=ok detail=playing"
  i=1
  while [ "$i" -le 20 ]; do
    ts=$(awk -v i="$i" 'BEGIN { printf "%.1f", 19621.3 + i * 9 }')
    case "$case_name" in
      delayed)
        if [ "$i" -le 4 ]; then cursor=16; else cursor=$((16 + i - 4)); fi ;;
      never)
        cursor=16 ;;
      replay)
        if [ "$i" -eq 1 ]; then cursor=30; else cursor=$((29 + i)); fi ;;
      *)
        echo "mock product: unknown case $case_name" >&2; exit 2 ;;
    esac
    echo "$ts playback: state phase=playing cursor=$cursor end=64352 paused=0 reverse=0 speed=1.000000 rate=66.666669 firstPacketTick=1 origin=1 pending=0 lastRequest=2 lastStatus=applied lastFrameRequest=0 boundary=0 requests=2 landings=0 rejected=0"
    i=$((i + 1))
  done
} > "$trace"
exit 0
MOCKEOF
chmod +x "$MOCK"

count=0
failures=0
ok() { count=$((count + 1)); echo "  OK   $1"; }
bad() { count=$((count + 1)); failures=$((failures + 1)); echo "  FAIL $1"; }

run_case() { # run_case <name> <expected-rc> <grep-that-must-be-present>
  local name="$1" want_rc="$2" want_text="$3" rc log out
  out="$TMP/out-$name"
  log="$TMP/$name.log"
  TIMELINE_MOCK_CASE="$name" \
    TIMELINE_EXE="$MOCK" \
    TIMELINE_TF="$TMP/tf" \
    TIMELINE_DEMO="$TMP/demo.dem" \
    TIMELINE_OUT="$out" \
    TIMELINE_SECTIONS=3 \
    bash timeline-check.sh --ignore-floor > "$log" 2>&1
  rc=$?
  if [ "$rc" -eq "$want_rc" ]; then
    ok "$name: gate exit $rc as expected"
  else
    bad "$name: gate exit $rc, expected $want_rc (log $log)"
  fi
  if grep -q "$want_text" "$log"; then
    ok "$name: log names \"$want_text\""
  else
    bad "$name: log does not contain \"$want_text\" (log $log)"
  fi
}

echo "=== timeline-check-selftest: the resume wait is a condition ==="

run_case delayed 0 'TIMELINE-CHECK=PASS'
run_case delayed 0 'floor: SKIPPED (--ignore-floor'
run_case delayed 0 'after sample 5/20'
run_case delayed 0 'wall time allowed 5 ticks'
run_case never 1 'no state sample moved past 16 in 20 sample(s)'
run_case replay 1 'does not replay paused wall time (delta=14'

# The floor must still fire when it is not ignored: a one-section mock run reports
# far fewer than 24 assertions, and without --ignore-floor that must be a FAIL
# that names the floor -- otherwise "the mock passes" would only measure a switch.
out="$TMP/out-floor"; log="$TMP/floor.log"
TIMELINE_MOCK_CASE=delayed TIMELINE_EXE="$MOCK" TIMELINE_TF="$TMP/tf" \
  TIMELINE_DEMO="$TMP/demo.dem" TIMELINE_OUT="$out" TIMELINE_SECTIONS=3 \
  bash timeline-check.sh > "$log" 2>&1
rc=$?
if [ "$rc" -eq 1 ]; then ok "floor: a one-section run is refused without --ignore-floor"; else bad "floor: a one-section run is refused without --ignore-floor (rc=$rc)"; fi
if grep -q 'only [0-9]* assertions ran; the floor is 24' "$log"; then ok "floor: the refusal names the floor"; else bad "floor: the refusal names the floor (log $log)"; fi

echo
if [ "$failures" -eq 0 ]; then
  echo "TIMELINE-CHECK-SELFTEST=PASS assertions=$count"
  exit 0
fi
echo "TIMELINE-CHECK-SELFTEST=FAIL assertions=$count failures=$failures"
exit 1
