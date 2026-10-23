#!/usr/bin/env bash
# trajectory-overlay-check.sh -- T53 gate.
#
# WHAT IT ASSERTS
# ---------------
# NEXT-TASKS-2026-10-21.md:583-589 asks for a projectile trajectory overlay that
# "must not assume constant velocity if the demo provides observed projectile
# positions that capture gravity, collision, or deflection", with all-player and
# selected-player filters, sampling/performance tiers, a ~0.5 s linger, and an
# explicit no-projectile case with a performance reading.
#
# THE RULES. Thresholds were fixed before the data; a miss is a finding.
#   R1  lifetimes == the oracle reach count (2201 = 1228+774+152+47)
#   R2  lifetimes != distinctSlots (slot reuse is split, not merged)
#   R3  rows are tick non-decreasing (the binary search precondition)
#   R4  a contiguous 40..300 tick flight exists with constant-velocity error
#       >= 100 map units (the task's own floor), AND the subject really is a
#       flight -- R4b alone is satisfied by a resting series whose error is
#       bigger purely because it lasted longer (measured: a 2438-tick rest reads
#       40,818 against the worst real flight's 7,408)
#   R5  the two shortcuts disagree, or swapping them is undetectable
#   R6  the chord distribution, reported WITH its denominator
#   R7  query < 100 comparisons per call over > 100,000 rows
#   R8  append median <= 0.5 ms and max <= 5 ms
#   R9  build median < 1 ms against a 4.1667 ms 240 Hz frame
#   R10 all-players vs selected-player differ, and the filter excludes rows
#   R11 Low quality submits fewer points than High on the same tick
#   R12 linger: points inside the window, none past it
#   R13 the no-projectile case reports NO-PROJECTILE
#
# THE DOCTORED-INPUT SELF-TEST, AND WHY IT COMES FIRST
# ---------------------------------------------------
# Every rule above reads a NUMBER OUT OF A TEXT REPORT with sed. So a rule can
# go red for two completely different reasons: the product produced a wrong
# number, or the parser failed to read a correct one. Those need different
# repairs, and from the outside they look identical -- a FAIL line.
#
# T52 lost a mutation this way on the same day: a broken parse and a broken
# product both read as "pass", because the two readings happened to agree.
#
# So before any demo runs, the SAME parser and the SAME assertions are run
# against a COPY of a real report with ONE field changed to a wrong value. Every
# doctored case must (a) fail and (b) name the rule the change targets. If a
# doctored case passes, the rule is only testing that a field exists, and the
# gate says so and stops.
#
# MUTATIONS (--mutation). Each runs and must be caught by a NAMED rule. A
# mutation that merely fails to build is not a caught mutation.
#   m2  the acceptance subject taken as the LONGEST series -- the defect that
#       measured duration and called it curvature (40,818 units on a resting
#       sticky bomb). Caught by R4b.
#   m4  the append order reversed, which breaks R3. Caught because the search
#       then returns 257,880 rows for a tick that holds 9: wrong rows, not none.
#
# Usage: bash trajectory-overlay-check.sh [--mutation]
# Exit:  0 = every assertion held (with --mutation: every mutation was caught)
set -uo pipefail
cd "$(dirname "$0")"

PROBE=native/build-nmake/projectile_track_probe.exe
TF="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
BAGEL="D:/TF2_Demo_Player/testdata/demos/4a9bfb9276509d0ec5f5fdc722a95b17_match-20260927-0239-koth_bagel_rc13.dem"
OUT="${TRAJECTORY_OUT:-evidence/trajectory}"
ORACLE_SPAWNS=2201
MUTATION=0
[ "${1:-}" = "--mutation" ] && MUTATION=1
mkdir -p "$OUT"

# ---------------------------------------------------------------------------
# THE JUDGE. One function, used by the doctored-input self-test AND the real run,
# so what the self-test exercises is exactly what judges the product.
#
# It takes a report path and a label, prints one OK/FAIL line per rule, and
# returns the number of failures. Parsing lives INSIDE it for that reason: a
# second copy of the sed expressions used only by the self-test would test the
# copy.
# ---------------------------------------------------------------------------
judge() {
  local file="$1" label="${2:-live}"
  local fails=0
  # NOT local ok() { ... }. Bash does not allow declaring a function with the
  # local keyword -- it parses as a variable assignment followed by a function and
  # dies with syntax error near unexpected token "(". These need to see and mutate
  # the caller's fails, which plain shell functions do because bash scopes
  # variables dynamically.
  ok() { echo "  OK   $1"; }
  no() { echo "  FAIL $1"; fails=$((fails + 1)); }

  local LIFETIMES SLOTS INVARIANT FLIGHT_MAX FLIGHT_CHORD CANDIDATES
  local CHORD_MEDIAN CHORD_P90 CHORD_ABOVE ROWS COMPARISONS
  local APPEND_MEDIAN APPEND_MAX BUILD_MEDIAN ALL_POINTS SEL_POINTS SEL_FILTERED
  local LOW_POINTS INSIDE PAST EMPTY_MARK
  LIFETIMES=$(sed -n 's/^series: lifetimes=\([0-9]*\).*/\1/p' "$file" | head -1)
  SLOTS=$(sed -n 's/.*distinctSlots=\([0-9]*\).*/\1/p' "$file" | head -1)
  INVARIANT=$(sed -n 's/^invariant: tickNonDecreasing=\([a-z]*\).*/\1/p' "$file" | head -1)
  FLIGHT_MAX=$(sed -n 's/^flightSubject:.*maxError=\([0-9]*\).*/\1/p' "$file" | head -1)
  FLIGHT_CHORD=$(sed -n 's/^flightSubject:.*chordMax=\([0-9]*\).*/\1/p' "$file" | head -1)
  CANDIDATES=$(sed -n 's/^flights: candidates=\([0-9]*\).*/\1/p' "$file" | head -1)
  FLIGHT_SAMPLES=$(sed -n 's/^flightSubject:.*samples=\([0-9]*\).*/\1/p' "$file" | head -1)
  CHORD_MEDIAN=$(sed -n 's/^distribution: chordMedian=\([0-9.]*\).*/\1/p' "$file" | head -1)
  CHORD_P90=$(sed -n 's/.*chordP90=\([0-9.]*\).*/\1/p' "$file" | head -1)
  CHORD_ABOVE=$(sed -n 's/^distribution: chordAtLeast100=\([0-9]*\).*/\1/p' "$file" | head -1)
  ROWS=$(sed -n 's/^track: rows=\([0-9]*\).*/\1/p' "$file" | head -1)
  COMPARISONS=$(sed -n 's/^cost: .*queryComparisonsPerCall<=\([0-9]*\).*/\1/p' "$file" | head -1)
  APPEND_MEDIAN=$(sed -n 's/^appendCost: .*medianMs=\([0-9.]*\).*/\1/p' "$file" | head -1)
  APPEND_MAX=$(sed -n 's/^appendCost: .*maxMs=\([0-9.]*\).*/\1/p' "$file" | head -1)
  BUILD_MEDIAN=$(sed -n 's/^buildCost: .*medianMs=\([0-9.]*\).*/\1/p' "$file" | head -1)
  ALL_POINTS=$(sed -n 's/^overlay: .*allPoints=\([0-9]*\).*/\1/p' "$file" | head -1)
  SEL_POINTS=$(sed -n 's/^overlay: .*selectedPoints=\([0-9]*\).*/\1/p' "$file" | head -1)
  SEL_FILTERED=$(sed -n 's/^overlay: .*selectedFilteredOut=\([0-9]*\).*/\1/p' "$file" | head -1)
  LOW_POINTS=$(sed -n 's/^overlay: .*lowPoints=\([0-9]*\).*/\1/p' "$file" | head -1)
  INSIDE=$(sed -n 's/^overlay: .*insideWindowPoints=\([0-9]*\).*/\1/p' "$file" | head -1)
  PAST=$(sed -n 's/^overlay: .*flightPointsPastLinger=\([0-9]*\).*/\1/p' "$file" | head -1)
  EMPTY_MARK=$(grep -c "NO-PROJECTILE" "$file" || true)

  [ "${LIFETIMES:-0}" -eq "$ORACLE_SPAWNS" ] \
    && ok "R1 lifetimes=$LIFETIMES == oracle spawns $ORACLE_SPAWNS (1228+774+152+47)" \
    || no "R1 lifetimes=${LIFETIMES:-EMPTY} != oracle $ORACLE_SPAWNS"

  [ "${LIFETIMES:-0}" -gt 0 ] && [ "${SLOTS:-0}" -gt 0 ] && [ "${LIFETIMES:-0}" -ne "${SLOTS:-0}" ] \
    && ok "R2 $LIFETIMES lifetimes over $SLOTS slots, reuse split" \
    || no "R2 lifetimes=${LIFETIMES:-EMPTY} slots=${SLOTS:-EMPTY}: slot reuse merged"

  [ "$INVARIANT" = "true" ] \
    && ok "R3 rows tick non-decreasing (binary search sound)" \
    || no "R3 tick order broken; the binary search returns wrong rows"

  [ "${CANDIDATES:-0}" -ge 1 ] \
    && ok "R4a $CANDIDATES contiguous 40..300 tick flights" \
    || no "R4a no contiguous flight to assert on"
  [ "${FLIGHT_MAX:-0}" -ge 100 ] \
    && ok "R4b constant-velocity error $FLIGHT_MAX >= 100 map units" \
    || no "R4b constant-velocity error ${FLIGHT_MAX:-EMPTY} < 100"

  # R4c: THE SUBJECT MUST BE A FLIGHT, NOT MERELY AN ERRORFUL SERIES.
  #
  # R4b alone is satisfied by ANY series with a big number -- and a sticky bomb
  # resting on the ground for 2438 ticks produces a BIGGER number (40,818) than the
  # most curved actual flight (7,408), because it measures DURATION. A gate that
  # only tested ">= 100" would accept the very defect it exists to catch.
  #
  # So the subject shape is asserted too: a flight is 40..300 ticks with one
  # observation per tick, exactly the candidate filter. Mutation m2 selects the
  # longest series instead, and it fails HERE rather than at R4b.
  if [ -n "${FLIGHT_SAMPLES:-}" ] && [ "${FLIGHT_SAMPLES:-0}" -ge 40 ] \
     && [ "${FLIGHT_SAMPLES:-0}" -le 300 ]; then
    ok "R4c subject is a $FLIGHT_SAMPLES-tick flight (40..300), not a resting series"
  else
    no "R4c subject has ${FLIGHT_SAMPLES:-EMPTY} samples; a flight is 40..300 -- this measures duration, not curvature"
  fi

  [ "${FLIGHT_MAX:-0}" -gt "${FLIGHT_CHORD:-0}" ] \
    && ok "R5 shortcuts disagree ($FLIGHT_MAX vs chord $FLIGHT_CHORD)" \
    || no "R5 shortcuts coincide or invert"

  # WHY -gt 0 AND NOT -ge 0.
  #
  # The first version tested ">= 0", and 0 satisfies it. So a report in which NOT
  # ONE flight was measurably curved -- the distribution entirely straight -- passed
  # the rule whose whole purpose is to establish that curved paths exist. The
  # default value (:-0) made it worse: an UNREADABLE field and a genuinely ZERO
  # field were the same number, and that number was benign.
  #
  # This is the "absence read as a benign value" family: the same shape as
  # summary = {} discarding the append-cost limit, and as the checkpoint fallback
  # turning a vanished projectile into a present one. The count of curved flights
  # must be POSITIVE, and it must be a readable number, and it must not exceed the
  # candidate count it is drawn from.
  if [ "${CANDIDATES:-0}" -gt 0 ] && [ -n "${CHORD_MEDIAN:-}" ] && [ -n "${CHORD_P90:-}" ] \
     && [ -n "${CHORD_ABOVE:-}" ] && [ "${CHORD_ABOVE:-0}" -gt 0 ] \
     && [ "${CHORD_ABOVE:-0}" -le "${CANDIDATES:-0}" ]; then
    ok "R6 $CHORD_ABOVE of $CANDIDATES flights are measurably curved (median=$CHORD_MEDIAN p90=$CHORD_P90)"
  else
    no "R6 no curved flight to report, or the distribution is unreadable (above=${CHORD_ABOVE:-EMPTY} of ${CANDIDATES:-EMPTY}, median=${CHORD_MEDIAN:-EMPTY} p90=${CHORD_P90:-EMPTY})"
  fi

  [ "${ROWS:-0}" -gt 100000 ] && [ "${COMPARISONS:-9999}" -lt 100 ] \
    && ok "R7 $COMPARISONS comparisons per call over $ROWS rows" \
    || no "R7 comparisons=${COMPARISONS:-EMPTY} over rows=${ROWS:-EMPTY}: a scan"

  awk -v m="${APPEND_MEDIAN:-99}" 'BEGIN{exit !(m <= 0.5)}' \
    && ok "R8a append median ${APPEND_MEDIAN}ms <= 0.5ms" \
    || no "R8a append median ${APPEND_MEDIAN:-EMPTY}ms > 0.5ms"
  awk -v m="${APPEND_MAX:-99}" 'BEGIN{exit !(m <= 5.0)}' \
    && ok "R8b append max ${APPEND_MAX}ms <= 5ms" \
    || no "R8b append max ${APPEND_MAX:-EMPTY}ms > 5ms"

  awk -v m="${BUILD_MEDIAN:-99}" 'BEGIN{exit !(m < 1.0)}' \
    && ok "R9 build median ${BUILD_MEDIAN}ms < 1ms (240Hz frame is 4.1667ms)" \
    || no "R9 build median ${BUILD_MEDIAN:-EMPTY}ms >= 1ms"

  [ "${ALL_POINTS:-0}" -gt 0 ] && [ "${SEL_POINTS:-0}" -gt 0 ] && [ "${ALL_POINTS:-0}" -ne "${SEL_POINTS:-0}" ] \
    && ok "R10 all=$ALL_POINTS vs selected=$SEL_POINTS differ from the same tick" \
    || no "R10 the player filter does not change the result (${ALL_POINTS:-EMPTY} vs ${SEL_POINTS:-EMPTY})"
  [ "${SEL_FILTERED:-0}" -gt 0 ] \
    && ok "R10b the selected filter excluded $SEL_FILTERED rows" \
    || no "R10b the selected filter excluded nothing"

  [ "${LOW_POINTS:-0}" -gt 0 ] && [ "${LOW_POINTS:-0}" -lt "${ALL_POINTS:-0}" ] \
    && ok "R11 low=$LOW_POINTS < high=$ALL_POINTS points on the same tick" \
    || no "R11 the quality tier does not change the point count (${LOW_POINTS:-EMPTY} vs ${ALL_POINTS:-EMPTY})"

  [ "${INSIDE:-0}" -gt 0 ] && [ "${PAST:-1}" -eq 0 ] \
    && ok "R12 linger: $INSIDE points inside the window, $PAST past it" \
    || no "R12 linger: inside=${INSIDE:-EMPTY} past=${PAST:-EMPTY}"

  [ "${EMPTY_MARK:-0}" -ge 1 ] \
    && ok "R13 the no-projectile case is reported explicitly" \
    || no "R13 the no-projectile case did not report"

  echo "$fails"
}

# ---------------------------------------------------------------------------
# THE DOCTORED-INPUT SELF-TEST.
#
# Each case copies the real report, changes ONE field, and requires the judged
# output to contain a FAIL naming the rule that field feeds. Passing means the
# parser and the assertion both work on a value that is present and WRONG --
# which is the case a live run cannot produce on demand.
# ---------------------------------------------------------------------------
doctored_selftest() {
  local real="$1" work="$OUT/doctored"
  local fails=0
  mkdir -p "$work"
  echo "=== DOCTORED-INPUT SELF-TEST (the parser, before the product) ==="
  echo "  source report: $real"

  # case | sed expression | rule that must go red | what the change means
  doctor() {
    local name="$1" expr="$2" want_rule="$3" why="$4"
    local copy="$work/$name.txt"
    sed "$expr" "$real" > "$copy"
    if cmp -s "$real" "$copy"; then
      echo "  FAIL doctored/$name did not change the report; the case proves nothing ($why)"
      fails=$((fails + 1))
      return
    fi
    local judged
    judged=$(judge "$copy" "doctored/$name")
    local body
    body=$(printf '%s\n' "$judged" | sed '$d')   # drop the trailing fail count
    if printf '%s\n' "$body" | grep -q "^  FAIL $want_rule"; then
      echo "  OK   doctored/$name -> $want_rule went red ($why)"
    else
      echo "  FAIL doctored/$name did NOT make $want_rule go red ($why)"
      printf '%s\n' "$body" | sed 's/^/       /'
      fails=$((fails + 1))
    fi
  }

  # R1: a lifetime count that is present but wrong.
  doctor "r1-lifetimes" 's/^series: lifetimes=2201/series: lifetimes=2200/' "R1" "lifetimes 2201 -> 2200"
  # R3: the invariant present but false.
  doctor "r3-invariant" 's/^invariant: tickNonDecreasing=true/invariant: tickNonDecreasing=false/' \
    "R3" "tickNonDecreasing true -> false"
  # R4b: a flight error below the floor.
  doctor "r4b-flightmax" 's/maxError=7408/maxError=99/' "R4b" "constant-velocity error 7408 -> 99"
  # R4c: the subject's shape, present and wrong. This is the field mutation m2
  # moves to 2438 indirectly; here it is moved directly.
  # NOTE the sed ADDRESS (/^flightSubject:/) rather than a backreference before the
  # digits. '\12438' is parsed as backreference 12438 or 124, not as '\1' followed by
  # '2438', so the first attempt silently produced the wrong line and the case
  # reported a parse miss. An address restricts the substitution without needing a
  # backreference at all.
  doctor "r4c-flightshape" '/^flightSubject:/s/samples=[0-9]*/samples=2438/' \
    "R4c" "subject samples 214 -> 2438 (a resting series, not a flight)"
  # R7: a comparison count that is a scan again.
  doctor "r7-comparisons" 's/queryComparisonsPerCall<=36/queryComparisonsPerCall<=257880/' \
    "R7" "comparisons 36 -> 257880 (a scan)"
  # R8a: an append median over the rule.
  doctor "r8a-appendmedian" 's/^appendCost: packets=\([0-9]*\) medianMs=[0-9.]*/appendCost: packets=\1 medianMs=0.9000/' "R8a" "append median 0.006 -> 0.9"
  # R9: a build that would eat the frame.
  doctor "r9-buildmedian" 's/^buildCost: frames=\([0-9]*\) medianMs=[0-9.]*/buildCost: frames=\1 medianMs=2.5000/' "R9" "build median 0.009 -> 2.5"
  # R10: a filter that returns the same count for both settings.
  doctor "r10-filter" 's/allPoints=109/allPoints=34/' "R10" "all vs selected made equal"
  # R11: a quality tier that changes nothing.
  doctor "r11-quality" 's/lowPoints=26/lowPoints=109/' "R11" "low points made equal to high"
  # R12: a trail that survives past the linger window.
  doctor "r12-linger" 's/flightPointsPastLinger=0/flightPointsPastLinger=77/' \
    "R12" "points past the linger window 0 -> 77"
  # R13: the empty case silently not reported.
  doctor "r13-empty" '/NO-PROJECTILE/d' "R13" "the NO-PROJECTILE marker deleted"
  # R6: an unreadable distribution (the field present but blank).
  doctor "r6-blankp90" 's/chordP90=532.5/chordP90=/' "R6" "p90 blanked"
  # R6, SECOND CASE: the field is present and readable, but ZERO flights are
  # curved. This is the case ">= 0" used to pass, and it cannot be caught by the
  # blank-field case above -- a zero is not a blank.
  doctor "r6-zero-curved" 's/chordAtLeast100=667/chordAtLeast100=0/' \
    "R6" "curved flights 667 -> 0 (a present, readable, benign-looking zero)"

  echo "  doctored failures: $fails"
  return "$fails"
}

# ---------------------------------------------------------------------------
echo "=== 0/4 precondition ==="
[ -x "$PROBE" ] || { echo "FATAL: missing binary: $PROBE"; exit 1; }
[ -e "$TF" ] || { echo "FATAL: missing tf root: $TF"; exit 1; }
[ -e "$BAGEL" ] || { echo "FATAL: missing demo: $BAGEL"; exit 1; }

echo "=== 1/4 closed-form self-test ==="
"$PROBE" --self-test > "$OUT/selftest.txt" 2>&1
SELF=$?
cat "$OUT/selftest.txt"
ST_FAIL=0
grep -q "constant-velocity fixture: max=144.000" "$OUT/selftest.txt" \
  && echo "  OK   constant-velocity closed form 144" \
  || { echo "  FAIL constant-velocity fixture did not read 144"; ST_FAIL=1; }
grep -q "control (straight line): max=0.000" "$OUT/selftest.txt" \
  && echo "  OK   straight-line control reads 0" \
  || { echo "  FAIL the straight-line control is not 0"; ST_FAIL=1; }
grep -q "chord fixture: max=40.000" "$OUT/selftest.txt" \
  && echo "  OK   chord closed form 40" \
  || { echo "  FAIL chord fixture did not read 40"; ST_FAIL=1; }
grep -q "slot-reuse fixture: series=2" "$OUT/selftest.txt" \
  && echo "  OK   slot reuse splits into two series" \
  || { echo "  FAIL slot reuse is being merged"; ST_FAIL=1; }
[ "$SELF" -eq 0 ] && echo "  OK   self-test exit 0" || { echo "  FAIL self-test exited $SELF"; ST_FAIL=1; }

echo
echo "=== 2/4 demo readings (bagel) ==="
"$PROBE" --tf-root "$TF" --demo "$BAGEL" --top 3 > "$OUT/demo.txt" 2>&1
DEMORC=$?
cat "$OUT/demo.txt"
[ "$DEMORC" -eq 0 ] || ST_FAIL=1

echo
DOCTORED_FAILS=0
doctored_selftest "$OUT/demo.txt" || DOCTORED_FAILS=$?

echo
echo "=== 3/4 judge rules (live) ==="
LIVE=$(judge "$OUT/demo.txt" live)
LIVE_FAILS=$(printf '%s\n' "$LIVE" | tail -1)
printf '%s\n' "$LIVE" | sed '$d'

echo
echo "=== 4/4 known limitation, counted ==="
OWNER_REUSED=$(sed -n 's/^ownerReuse: .*slotsWithMultipleLifetimes=\([0-9]*\).*/\1/p' "$OUT/demo.txt" | head -1)
if [ "${OWNER_REUSED:-0}" -gt 0 ]; then
  echo "  OK   LIMITATION $OWNER_REUSED owner slots carry more than one lifetime (stated in the report)"
else
  echo "  OK   LIMITATION no owner slots reused on this recording"
fi

if [ "$MUTATION" -eq 1 ]; then
  echo
  echo "=== MUTATION MODE ==="
  MUT_FAILS=0

  # m2: the acceptance subject becomes the LONGEST series. The defect: a resting
  # sticky bomb's constant-velocity error measures duration, not curvature, so
  # R4b loses its meaning and the subject stops being a flight at all.
  "$PROBE" --tf-root "$TF" --demo "$BAGEL" --top 3 --subject-longest > "$OUT/m2.txt" 2>&1
  M2J=$(judge "$OUT/m2.txt" m2)
  M2BODY=$(printf '%s\n' "$M2J" | sed '$d')
  printf '%s\n' "$M2BODY" | sed 's/^/  /'
  # R4b is NOT the rule that catches m2, and discovering that was worth doing:
  # the resting sticky bomb's error (40,818) is LARGER than the real flight's
  # (7,408), so "error >= 100" passes on the mutated subject. The rule that
  # catches it is R4c, which is about the subject's SHAPE. This is why m2 had to
  # be RUN rather than argued about.
  if printf '%s\n' "$M2BODY" | grep -q "^  FAIL R4c"; then
    echo "  CATCH m2: the longest-series subject fails R4c (2438 samples is a rest, not a flight)"
  else
    echo "  MISS  m2: taking the longest series did NOT fail R4c"
    MUT_FAILS=$((MUT_FAILS + 1))
  fi
  if grep -q "subject=longest" "$OUT/m2.txt"; then
    echo "  CATCH m2: the probe reports the mutated subject selection"
  else
    echo "  MISS  m2: the probe did not report the mutation"
    MUT_FAILS=$((MUT_FAILS + 1))
  fi

  # m4: the append order reversed, which breaks R3. The search then returns
  # WRONG ROWS rather than none -- which is why R3 is a rule and not a comment.
  "$PROBE" --tf-root "$TF" --demo "$BAGEL" --reverse-append-order > "$OUT/m4.txt" 2>&1
  cat "$OUT/m4.txt"
  if grep -q "m4.after: tickNonDecreasing=false" "$OUT/m4.txt"; then
    echo "  CATCH m4: the tick-order invariant went red"
  else
    echo "  MISS  m4: reversing the append did not break the invariant"
    MUT_FAILS=$((MUT_FAILS + 1))
  fi
  EXP=$(sed -n 's/^m4.search: .*expectedRows=\([0-9]*\).*/\1/p' "$OUT/m4.txt" | head -1)
  FND=$(sed -n 's/^m4.search: .*foundRows=\([0-9]*\).*/\1/p' "$OUT/m4.txt" | head -1)
  if [ "${EXP:-0}" -gt 0 ] && [ "${FND:-0}" -ne "${EXP:-0}" ]; then
    echo "  CATCH m4: the search returned $FND rows for a tick holding $EXP -- wrong rows, not none"
  else
    echo "  MISS  m4: the search did not visibly lie (expected=${EXP:-?} found=${FND:-?})"
    MUT_FAILS=$((MUT_FAILS + 1))
  fi

  echo
  if [ "$DOCTORED_FAILS" -ne 0 ]; then
    echo "MUTATION-INVALID (the doctored-input self-test is broken; fix that first)"
    exit 1
  fi
  if [ "$LIVE_FAILS" -ne 0 ]; then
    echo "MUTATION-INVALID (a live rule fails; that is a product finding, not a mutation result)"
    exit 1
  fi
  if [ "$MUT_FAILS" -ne 0 ]; then
    echo "MUTATION-NOT-CAUGHT ($MUT_FAILS of 4 checks missed)"
    exit 1
  fi
  echo "MUTATION-CAUGHT"
  exit 0
fi

echo
TOTAL=$((ST_FAIL + LIVE_FAILS + DOCTORED_FAILS))
if [ "$TOTAL" -ne 0 ]; then
  echo "TRAJECTORY-OVERLAY=FAIL (selftest=$ST_FAIL live=$LIVE_FAILS doctored=$DOCTORED_FAILS)"
  exit 1
fi
echo "TRAJECTORY-OVERLAY=PASS"
exit 0
