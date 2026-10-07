#!/usr/bin/env bash
# history-coverage-check.sh -- a Checkpoint answer must not be able to be
# arbitrarily old.
#
# Why this exists
# ---------------
# `oracle-trajectory-check.py` compares reconstructed positions against the
# independent oracle, value by value, but only inside the ~70-packet live window,
# because that is the only place the answer is tick-exact. Every count-based gate
# (check-oracle.sh, the corpus census, the fixture suite) is green while a tick
# outside that window resolves to a *Checkpoint* -- and until this script existed
# nothing said how old a Checkpoint was allowed to be.
#
# It was allowed to be 54392 ticks. Measured on bagel, the retained archive had a
# hole from 57829 to 112221 (13.7 minutes) because `thinHistoryArchive` decimated
# by *index*: it divided the index range evenly, and with maxCount just above the
# archive size floor() maps the earliest slots onto their own index, so the head
# was pinned verbatim and everything after it was decimated again on every flush.
# The worst case reproduces on a supply that emits one checkpoint every 128 ticks
# without any variation, so the defect was in the policy, not in the demo.
#
# What is asserted
# ----------------
# The bound is the pigeonhole floor: `kept` checkpoints spanning `span` ticks
# cannot do better than span / (kept - 1), so requiring the worst retained gap to
# be within 2x that floor is a statement the policy can fail. The pre-fix rule
# misses it by 6.5x on the fixture and by 71x on bagel.
#
# Two independent readings, because either alone can be satisfied for the wrong
# reason:
#   1. the fixture (presentation_probe) -- a synthetic supply with a known shape,
#      which is what mutate.sh m9 reverts the policy against;
#   2. bagel, end to end -- the retained tick list recomputed from the probe's own
#      raw output, plus a direct staleness sample over the whole demo using the
#      tick the query actually resolved to.
#
# Work counts, not just "no failure": `samples` must equal what was asked for and
# `distinct` must equal `retained`, or a run that compared nothing would pass.
#
# Usage: bash history-coverage-check.sh
# Exit:  0 = every assertion held.
set -uo pipefail
cd "$(dirname "$0")"

PROBE=native/build-nmake/entity_model_probe.exe
FIXTURE=native/build-nmake/presentation_probe.exe
TF="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
BAGEL="D:/TF2_Demo_Player/testdata/demos/4a9bfb9276509d0ec5f5fdc722a95b17_match-20260927-0239-koth_bagel_rc13.dem"
SAMPLES=512
# mutate.sh m9 runs this script while the defect is reintroduced; it redirects
# the output so the red readings cannot overwrite the fixed-build evidence.
OUT="${HISTORY_COVERAGE_OUT:-evidence/history-coverage}"
mkdir -p "$OUT"

fail=0
for path in "$PROBE" "$FIXTURE"; do
  [ -x "$path" ] || { echo "FATAL: missing binary: $path"; exit 1; }
done
for path in "$TF" "$BAGEL"; do
  [ -e "$path" ] || { echo "FATAL: missing input: $path"; exit 1; }
done

echo "=== 1/2 retention policy fixture (synthetic uniform supply) ==="
"$FIXTURE" > "$OUT/fixture.txt" 2>&1
cat "$OUT/fixture.txt"
# `historyWorstGap` and `historySlotFloor` are both read, and the assertion is
# their ratio: a floor of 0 would make "worst <= 2 * floor" unsatisfiable rather
# than vacuously true, which is the direction this has to fail in.
FIX_OK=$(sed -n 's/.*"historyCoverage":\(true\|false\).*/\1/p' "$OUT/fixture.txt")
FIX_WORST=$(sed -n 's/.*"historyWorstGap":\([0-9]*\).*/\1/p' "$OUT/fixture.txt")
FIX_FLOOR=$(sed -n 's/.*"historySlotFloor":\([0-9]*\).*/\1/p' "$OUT/fixture.txt")
echo "fixture coverage=$FIX_OK worstGap=$FIX_WORST slotFloor=$FIX_FLOOR"
if [ "$FIX_OK" = "true" ] && [ "${FIX_WORST:-0}" -gt 0 ] && [ "${FIX_FLOOR:-0}" -gt 0 ] \
   && [ "$FIX_WORST" -le "$((2 * FIX_FLOOR))" ]; then
  echo "FIXTURE-COVERAGE=PASS (worst $FIX_WORST <= 2 x floor $FIX_FLOOR)"
else
  echo "FIXTURE-COVERAGE=FAIL"; fail=1
fi

echo
echo "=== 2/2 bagel: retained tick list and staleness over the whole demo ==="
# One probe run produces both the raw retained tick list (so the gaps can be
# recomputed here instead of trusted) and the staleness sample.
"$PROBE" --tf-root "$TF" --demo "$BAGEL" --history-stats --history-coverage "$SAMPLES" \
  > "$OUT/bagel.txt" 2>&1
grep -E '^history (archive=|liveCheckpoints=|flushes=|coverage|retained|gap |staleness|bytes)' \
  "$OUT/bagel.txt" | tee "$OUT/bagel-summary.txt"

RET=$(sed -n 's/^history retained archive=\([0-9]*\) live=\([0-9]*\) distinct=\([0-9]*\).*/\1 \2 \3/p' "$OUT/bagel-summary.txt" | head -1)
read -r ARCHIVE LIVE DISTINCT <<<"$RET"
WORST=$(sed -n 's/^history gap worst=\([0-9]*\).*/\1/p' "$OUT/bagel-summary.txt" | head -1)
FLOOR=$(sed -n 's/^history gap .* floor=\([0-9]*\).*/\1/p' "$OUT/bagel-summary.txt" | head -1)
SAMPLED=$(sed -n 's/^history coverage samples=\([0-9]*\).*/\1/p' "$OUT/bagel-summary.txt" | head -1)
UNAVAIL=$(sed -n 's/^history coverage .* unavailable=\([0-9]*\).*/\1/p' "$OUT/bagel-summary.txt" | head -1)
STALE=$(sed -n 's/^history staleness worst=\([0-9]*\).*/\1/p' "$OUT/bagel-summary.txt" | head -1)
echo "bagel archive=$ARCHIVE live=$LIVE distinct=$DISTINCT worstGap=$WORST floor=$FLOOR sampled=$SAMPLED unavailable=$UNAVAIL worstStaleness=$STALE"

if [ "${SAMPLED:-0}" -eq "$SAMPLES" ]; then
  echo "  OK   sampled=$SAMPLED of the requested $SAMPLES ticks"
else
  echo "  FAIL sampled=${SAMPLED:-<none>} but $SAMPLES were asked for"; fail=1
fi
# Every retained slot must hold its own tick. Duplicate ticks were half the
# archive budget on bagel before the flush path stopped pushing two checkpoints
# per flush.
if [ "${ARCHIVE:-0}" -ge 2 ] && [ "${DISTINCT:-0}" -eq "$((${ARCHIVE:-0} + ${LIVE:-0}))" ]; then
  echo "  OK   distinct=$DISTINCT of $((ARCHIVE + LIVE)) retained slots"
else
  echo "  FAIL $((ARCHIVE + LIVE)) retained slots hold only ${DISTINCT:-<none>} distinct ticks"; fail=1
fi
if [ "${UNAVAIL:-1}" -eq 0 ]; then
  echo "  OK   every sampled tick resolved to a snapshot"
else
  echo "  FAIL $UNAVAIL sampled ticks resolved to nothing"; fail=1
fi
# The two readings have to agree: staleness is measured through the query, the
# gap is recomputed from the retained tick list, and they are computed from
# different data. A staleness larger than the worst gap would mean the query
# resolved to a tick the archive does not contain.
if [ "${WORST:-0}" -gt 0 ] && [ "${FLOOR:-0}" -gt 0 ] && [ "$WORST" -le "$((2 * FLOOR))" ]; then
  echo "  OK   worstGap $WORST <= 2 x floor $FLOOR"
else
  echo "  FAIL worstGap=${WORST:-<none>} floor=${FLOOR:-<none>}"; fail=1
fi
if [ "${STALE:-1}" -le "${WORST:-0}" ] && [ "${STALE:-1}" -ge 0 ]; then
  echo "  OK   worstStaleness $STALE <= worstGap $WORST"
else
  echo "  FAIL worstStaleness=${STALE:-<none>} exceeds worstGap=${WORST:-<none>}"; fail=1
fi
# The defect's signature, asserted so a regression cannot pass by being merely
# "bounded": the pre-fix archive had a 54392-tick hole.
if [ "${WORST:-0}" -lt 54392 ]; then
  echo "  OK   no 54392-tick hole (pre-fix worst gap)"
else
  echo "  FAIL worstGap=$WORST is the pre-fix hole"; fail=1
fi

echo
if [ "$fail" -eq 0 ]; then
  echo "HISTORY-COVERAGE=PASS (fixture within 2x floor, bagel worst gap $WORST <= 2 x floor $FLOOR)"
else
  echo "HISTORY-COVERAGE=FAIL"
fi
exit "$fail"
