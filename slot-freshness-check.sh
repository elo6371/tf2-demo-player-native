#!/usr/bin/env bash
# slot-freshness-check.sh -- a player's origin arrives in more than one slot, and
# the slot the selection rule prefers is not always the fresh one.
#
# Why this exists
# ---------------
# `preferCandidate` (entity_model.cpp) ranks repeated properties by
# LocalPlayerExclusive -> others -> NonLocalPlayerExclusive, then by name. That is
# deterministic, which was the P1 fix -- but deterministic is not the same as
# right, and the difference is invisible to every count-based gate:
#
#   bagel, server tick 129277, entity 1
#     LocalPlayerExclusive   written at 129277  -> the rule picks a FRESH slot
#     NonLocalPlayerExclusive written at  56148  -> 73129 ticks stale, not chosen
#   POV autorecord_2026-07-02_13-26-46, entity 3
#     LocalPlayerExclusive   written at  51596 and never again -> frozen
#     NonLocalPlayerExclusive written at  53976 / 54623 / 55394 -> fresh
#
# Same rule, opposite outcomes, and on the POV demo the picked slot is the one
# that stopped updating: its value sits at 1511.459961,894.130005 while the
# recorded camera is at -1112.03,505.59,459.03 -- 2729 units away (that distance
# is asserted in observer-focus-check.sh). The pick is stale by 2380 ticks at
# 53976 and 3798 by 55394.
#
# So the missing ingredient is freshness, and freshness has to be recorded where
# the write happens. This round added only the recording
# (EntityPropertyValue::lastWriteTick) and this reading. It did NOT change
# `preferCandidate`: every existing probe counter is byte-identical
# (check-probe-output-additive.sh, 9/9 IDENTICAL), and the fixture below asserts
# the rule still prefers the Local slot.
#
# Why this is not yet a step of verify-all.sh
# -------------------------------------------
# This gate freezes the CURRENT reading, which is the defective one
# (chosenStale=3 on the POV demo). The acceptance chain asserts the state the
# delivery claims, so it should gain this step together with the rule change --
# at which point the POV expectation flips to chosenStale=0 and the bagel one
# stays 0. Until then, run it directly:
#
#   bash slot-freshness-check.sh            # expect SLOT-FRESHNESS=PASS
#   bash slot-freshness-check.sh --mutation # both perturbations must go red
#
# Exit: 0 = every assertion held (or, with --mutation, they went red).

set -uo pipefail
cd "$(dirname "$0")"

PROBE=native/build-nmake/entity_model_probe.exe
TF="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
BAGEL="D:/TF2_Demo_Player/testdata/demos/4a9bfb9276509d0ec5f5fdc722a95b17_match-20260927-0239-koth_bagel_rc13.dem"
POV="$TF/demos/autorecord_2026-07-02_13-26-46.dem"
OUT="${SLOT_FRESHNESS_OUT:-evidence/slot-freshness}"
mkdir -p "$OUT"

MUTATION=0
[ "${1:-}" = "--mutation" ] && MUTATION=1

fail=0
fails=0
ok=0
[ -x "$PROBE" ] || { echo "FATAL: missing binary: $PROBE"; exit 1; }
for path in "$TF" "$BAGEL" "$POV"; do
  [ -e "$path" ] || { echo "FATAL: missing input: $path"; exit 1; }
done

assert_eq() { # assert_eq <label> <actual> <expected>
  if [ "${2:-<none>}" = "$3" ]; then
    echo "  OK   $1 = $3"
    ok=$((ok + 1))
  else
    echo "  FAIL $1 = ${2:-<none>}, expected $3"; fail=1; fails=$((fails + 1))
  fi
}
# field <text> <key> -- one `key=value` token; the value runs to the next space.
field() { printf '%s' "$1" | sed -n "s/.* $2=\\([^ ]*\\).*/\\1/p" | head -1; }
# rank_field <dump> <rank> <key> -- <key>'s value on the line whose rank=<rank>.
# The chosen slot is always rank 0, so this reads the pick and its rival without
# assuming which slot name is which -- the whole point is that the names differ
# between the two demos. When a dump holds several ticks the LAST one wins: the
# POV assertions are about how stale the pick has become, which is the final tick.
rank_field() { sed -n "s/^ *rank=$2 .* $3=\\([^ ]*\\).*/\\1/p" "$1" | tail -1; }
rank_name()  { sed -n "s/^ *rank=$2 \\(([a-z]*)\\)\\? *name=\\([^ ]*\\).*/\\2/p" "$1" | tail -1; }

echo "=== 1/3 wiring fixture (the rule must still prefer Local; the field must round-trip) ==="
"$PROBE" --self-test > "$OUT/fixture.json" 2> "$OUT/fixture.txt"
grep '^slot-freshness-fixture' "$OUT/fixture.txt" > "$OUT/fixture-line.txt"
cat "$OUT/fixture-line.txt"
FL=$(cat "$OUT/fixture-line.txt")
# Exact, not bounded: the fixture is fixed, so each of these is a reading its
# design pins. A bound would let a branch stop firing as long as it stopped quietly.
assert_eq "fixture shapes" "$(field "$FL" shapes)" "2"
assert_eq "fixture ruleUnchanged (Local still wins)" "$(field "$FL" ruleUnchanged)" "2"
assert_eq "fixture tickRoundTrip (lastWriteTick survives)" "$(field "$FL" tickRoundTrip)" "2"
assert_eq "fixture freshChosen (bagel shape)" "$(field "$FL" freshChosen)" "1"
# If this reads 0 the fixture can no longer build the case the field exists for,
# and the demo assertions below would pass by being unable to fire.
assert_eq "fixture staleChosen (POV shape)" "$(field "$FL" staleChosen)" "1"

echo
echo "=== 2/3 POV entity 3 -- the rule picks the slot that stopped updating ==="
"$PROBE" --tf-root "$TF" --demo "$POV" --prop-candidates-at 53976,54747,55394 \
  --entity 3 --candidate-suffix m_vecOrigin > "$OUT/pov.json" 2> "$OUT/pov.txt"
cat "$OUT/pov.txt"
POV_SUM=$(grep '^prop-candidates-summary' "$OUT/pov.txt")
POV_STALE=$(field "$POV_SUM" chosenStale)
POV_WORST=$(field "$POV_SUM" worstChosenAge)
if [ "$MUTATION" -eq 1 ]; then
  POV_STALE=$((POV_STALE + 1))
  echo "mutation: expected chosenStale perturbed to $POV_STALE"
fi
# Every tick: the rule's pick was not the freshest candidate. A gate that only
# said "chosenStale > 0" would pass on one bad tick out of a hundred.
assert_eq "POV ticks where the rule picked a non-freshest slot" "$POV_STALE" "3"
assert_eq "POV candidates examined" "$(field "$POV_SUM" withTick)" "6"
assert_eq "POV worst chosen age (ticks)" "$POV_WORST" "3798"
# The pick itself, by name: Local, frozen at 51596 -- the same tick on all three
# queries, which is what "stopped updating" looks like in a number.
assert_eq "POV chosen slot" "$(rank_name "$OUT/pov.txt" 0)" "DT_TFLocalPlayerExclusive.m_vecOrigin"
assert_eq "POV chosen slot last write" "$(rank_field "$OUT/pov.txt" 0 lastWrite)" "51596"
assert_eq "POV chosen slot age at 55394" "$(rank_field "$OUT/pov.txt" 0 age)" "3798"
# And the slot it passed over was being written all along.
assert_eq "POV rival slot" "$(rank_name "$OUT/pov.txt" 1)" "DT_TFNonLocalPlayerExclusive.m_vecOrigin"
assert_eq "POV rival slot last write at 55394" "$(rank_field "$OUT/pov.txt" 1 lastWrite)" "55394"

echo
echo "=== 3/3 bagel entity 1 -- the same rule, and there it is right ==="
"$PROBE" --tf-root "$TF" --demo "$BAGEL" --prop-candidates-at 129277 \
  --entity 1 --candidate-suffix m_vecOrigin > "$OUT/bagel.json" 2> "$OUT/bagel.txt"
cat "$OUT/bagel.txt"
BAGEL_SUM=$(grep '^prop-candidates-summary' "$OUT/bagel.txt")
BAGEL_STALE=$(field "$BAGEL_SUM" chosenStale)
BAGEL_RIVAL_AGE=$(rank_field "$OUT/bagel.txt" 1 age)
if [ "$MUTATION" -eq 1 ]; then
  BAGEL_STALE=$((BAGEL_STALE + 1))
  echo "mutation: expected chosenStale perturbed to $BAGEL_STALE"
fi
# Zero here is what makes the POV reading a defect rather than a law: the rule is
# not "always wrong", it is wrong exactly where the Local slot goes quiet.
assert_eq "bagel ticks where the rule picked a non-freshest slot" "$BAGEL_STALE" "0"
assert_eq "bagel chosen slot" "$(rank_name "$OUT/bagel.txt" 0)" "DT_TFLocalPlayerExclusive.m_vecOrigin"
assert_eq "bagel chosen slot last write" "$(rank_field "$OUT/bagel.txt" 0 lastWrite)" "129277"
# The mirror image: here it is the slot the rule rejects that is stale. Without
# this line, "chosenStale=0 on bagel" would also be produced by a field that is
# never stamped on bagel at all.
assert_eq "bagel rival slot" "$(rank_name "$OUT/bagel.txt" 1)" "DT_TFNonLocalPlayerExclusive.m_vecOrigin"
assert_eq "bagel rival slot age (the stale one there)" "$BAGEL_RIVAL_AGE" "73129"

echo
if [ "$MUTATION" -eq 1 ]; then
  # Two perturbations were injected, one per demo, each feeding exactly one
  # assertion. Requiring exactly two red lines is what makes this a per-assertion
  # test: if a demo's chosenStale assertion had stopped reading anything, its
  # perturbation would go unnoticed and the count would come back at one.
  if [ "$fails" -eq 2 ]; then
    echo "MUTATION-CAUGHT=PASS (2/2 perturbations caught, red_lines=$fails)"
    exit 0
  fi
  echo "MUTATION-CAUGHT=FAIL (expected 2 red lines, saw $fails -- a perturbation went unnoticed)"
  exit 1
fi
if [ "$fail" -eq 0 ]; then
  echo "SLOT-FRESHNESS=PASS (assertions_ok=$ok, POV chosenStale=3 worstAge=3798, bagel chosenStale=0)"
  exit 0
fi
echo "SLOT-FRESHNESS=FAIL (assertions_ok=$ok)"
exit 1
