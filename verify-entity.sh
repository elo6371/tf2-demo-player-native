#!/usr/bin/env bash
# verify-entity.sh -- tier 2 for the entity subsystem ("this module is done").
#
# WHY THIS EXISTS
#   Three tiers, by how much of the system a change can reach:
#
#     verify-fast.sh    after every small change      ~30 s   build + self-tests
#     verify-entity.sh  when the entity work is done  ~15 min this file
#     verify-all.sh     before merge / release        ~80 min the whole chain
#
#   The middle tier exists because the expensive end of the chain is not the
#   entity work -- it is the corpus sample, the SourceTV sweep, the frame
#   capture and the audio path, none of which can see an entity change. This
#   script runs every gate that *can*, plus one real demo against the oracle, so
#   "the entity half is finished" is a claim with evidence behind it and without
#   an hour of unrelated decoding in front of it.
#
# WHAT IT DOES NOT SAY
#   Nothing here speaks to rendering, materials, audio, SourceTV coverage, or
#   the corpus sample. verify-all.sh remains the merge gate; this is not a
#   substitute and does not print VERIFY=PASS.
#
# WHERE THE READINGS GO
#   Every gate writes its raw readings into `evidence/<gate-name>/` by default,
#   and those directories hold the readings the *chain* commits as a round's
#   evidence. This tier redirects all of them into `evidence/fast/entity/`
#   (each gate takes an override for exactly this reason; mutate.sh has always
#   used it), and then *asserts* that nothing under `evidence/` moved -- the
#   redirection is checked, not intended. Measured 2026-10-09: before the
#   redirect existed, one run of this tier rewrote seven tracked files and
#   truncated `weapon-world-model/pov-dump.txt` by 1327 lines.
#
# THE GATES, AND WHY EACH ONE IS HERE
#   1. the 58 wire fixtures -- the only bit-exact layer, and the only place a
#      delta / Preserve / Leave / Delete edge is constructed instead of waited
#      for. Seconds.
#   2. history coverage -- how old a Checkpoint answer is allowed to be. An
#      entity placed from a 54392-tick-old snapshot is placed *wrong*, and every
#      count in the chain stays green while it is.
#   3. weapon world model -- a weapon's render request must name the weapon, not
#      the arms holding it. The wrong index resolves to a real asset, so it
#      draws successfully.
#   4. observer focus -- the observer pair, resolved and bounded (the 45-unit
#      residual and the 541-point census).
#   5. slot freshness -- which of two same-named origin properties is the live
#      one is a property of the demo, not of rank.
#   6. property lookup -- the class sweep must not build a transform it discards.
#   7. one real demo against the independent oracle -- the decoder's counts on
#      bagel, re-derived by a second implementation in the same run.
#
#   Every gate below already pins its own assertion counts; this file asserts the
#   gate's verdict, its exit status, and the fixture work count (58), so a gate
#   that silently stops running its cases cannot read green here either.
#
# MUTATION IS OFF BY DEFAULT
#   Four of the five gates carry their own --mutation (weapon-world-model,
#   observer-focus, slot-freshness, entity-property-lookup);
#   history-coverage-check.sh has none and is run straight in both modes. Mutation
#   answers "can this criterion go red", which is a question about the assertion
#   logic -- run it when the assertion logic changed, not when the code under it
#   did. Pass --mutation to run the whole set with perturbations (~+9 min).
#
# Usage: bash verify-entity.sh [--mutation] [--no-build]
# Exit:  0 = every gate passed.
set -uo pipefail
cd "$(dirname "$0")"

OUT="${VERIFY_ENTITY_OUT:-evidence/fast/entity}"
mkdir -p "$OUT"

MUTATION=0
BUILD=1
for arg in "$@"; do
  case "$arg" in
    --mutation) MUTATION=1 ;;
    --no-build) BUILD=0 ;;
    *) echo "unknown argument: $arg"; exit 2 ;;
  esac
done
MARG=""
[ "$MUTATION" -eq 1 ] && MARG="--mutation"

# The committed evidence tree must come out of this run unchanged. Captured as a
# before/after pair rather than a plain `git status` so that unrelated evidence
# edits already in the working tree do not read as this tier's fault.
EVIDENCE_BEFORE=$(git status --porcelain evidence/ 2>/dev/null | sort)

B=native/build-nmake
ORACLE="D:/TF2_Demo_Player_Deliverable/tools/ent-oracle/target/release/ent-oracle.exe"
BAGEL="D:/TF2_Demo_Player/testdata/demos/4a9bfb9276509d0ec5f5fdc722a95b17_match-20260927-0239-koth_bagel_rc13.dem"
FIXTURE_COUNT=58

if [ "$BUILD" -eq 1 ]; then
  echo "=== 1/3 incremental build ==="
  bash build-target.sh entity_protocol_probe entity_message_fixture_probe entity_model_probe
  rc_build=$?
  echo "build_rc=$rc_build"
  [ "$rc_build" -eq 0 ] || { echo "VERIFY-ENTITY=FAIL (build)"; exit 1; }
else
  echo "=== 1/3 incremental build (skipped: --no-build) ==="
fi

ok=0
bad=0

gate() { # gate <label> <script> <pass-marker-regex> <out-env-var>
  local label="$1" script="$2" marker="$3" outvar="$4"
  local log="$OUT/$label.txt"
  # The gate's own raw readings are redirected into this tier's scratch directory.
  # Every gate writes to evidence/<its-name>/ by default, and those directories
  # hold the readings the *chain* commits as the round's evidence. Running a tier
  # gate against the defaults overwrites a committed record with a transient one:
  # measured on 2026-10-09, one entity-tier run rewrote seven tracked files under
  # evidence/ and truncated weapon-world-model/pov-dump.txt by 1327 lines before
  # it was caught. Every gate takes an override for exactly this reason; mutate.sh
  # has always used it.
  env "$outvar=$OUT/$label" bash "$script" $MARG > "$log" 2>&1
  local rc=$?
  # Under --mutation each of these gates exits 0 after printing
  # `MUTATION-CAUGHT=PASS` and does NOT print its normal verdict line, so the
  # expected marker has to widen -- otherwise every caught mutation would be
  # reported here as a failure. history-coverage-check.sh has no mutation branch
  # of its own and is run straight in both modes, which is why the straight
  # marker stays accepted too.
  local pattern="$marker"
  [ "$MUTATION" -eq 1 ] && pattern="MUTATION-CAUGHT=PASS|$marker"
  local line
  line=$(grep -oE "$pattern" "$log" | head -1)
  if [ "$rc" -eq 0 ] && [ -n "$line" ]; then
    ok=$((ok + 1)); printf '  OK   %-22s %s\n' "$label" "$line"
  else
    bad=$((bad + 1)); printf '  FAIL %-22s rc=%s marker=%s\n' "$label" "$rc" "${line:-<none>}"
    # Print the tail so a red gate is diagnosed here rather than by opening the
    # log. This repo's rule: a red reading is a question about the instrument
    # first, and that question needs the instrument's own output.
    tail -6 "$log" | sed 's/^/       | /'
  fi
}

echo
echo "=== 2/3 the gates ==="
gate history-coverage       history-coverage-check.sh        'HISTORY-COVERAGE=(PASS|FAIL)'       HISTORY_COVERAGE_OUT
gate weapon-world-model     weapon-world-model-check.sh      'WEAPON-WORLD-MODEL=(PASS|FAIL)'     WEAPON_WORLD_MODEL_OUT
gate observer-focus         observer-focus-check.sh          'OBSERVER-FOCUS=(PASS|FAIL)'         OBSERVER_FOCUS_OUT
gate slot-freshness         slot-freshness-check.sh          'SLOT-FRESHNESS=(PASS|FAIL)'         SLOT_FRESHNESS_OUT
gate entity-property-lookup entity-property-lookup-check.sh  'ENTITY-PROPERTY-LOOKUP=(PASS|FAIL)' ENTITY_PROPERTY_LOOKUP_OUT

echo
echo "=== 3/3 the wire fixtures and one real demo against the oracle ==="
"$B/entity_message_fixture_probe.exe" > "$OUT/wire-fixtures.txt" 2>&1
rc_fix=$?
fix_n=$(grep -c '^PASS' "$OUT/wire-fixtures.txt" || true)
# The pass count is asserted, not just `fixture_failures=0`: a probe that ran
# nothing also reports zero failures. 58 must stay equal to verify-all.sh step 3.
if [ "$rc_fix" -eq 0 ] && grep -q '^fixture_failures=0$' "$OUT/wire-fixtures.txt" \
   && [ "$fix_n" -eq "$FIXTURE_COUNT" ]; then
  ok=$((ok + 1)); printf '  OK   %-22s %s/%s fixtures\n' wire-fixtures "$fix_n" "$FIXTURE_COUNT"
else
  bad=$((bad + 1)); printf '  FAIL %-22s rc=%s pass_count=%s (want %s)\n' wire-fixtures "$rc_fix" "$fix_n" "$FIXTURE_COUNT"
  tail -3 "$OUT/wire-fixtures.txt" | sed 's/^/       | /'
fi

# The oracle directory is separate from evidence/final on purpose: check-oracle.sh
# truncates and rewrites `oracle-gate-summary.txt` in the directory it is given,
# and evidence/final/oracle-gate-summary.txt is the chain's committed step-4
# result. Pointing this tier at evidence/final would overwrite that record with a
# one-demo run.
ORACLE_DIR="$OUT/oracle-one-demo"
mkdir -p "$ORACLE_DIR"
"$B/entity_protocol_probe.exe" "$BAGEL" > "$ORACLE_DIR/bagel.txt" 2>&1
bash check-oracle.sh "$ORACLE" "$ORACLE_DIR" bagel > "$OUT/oracle-bagel.txt" 2>&1
rc_orc=$?
if [ "$rc_orc" -eq 0 ] && grep -q 'ORACLE-GATE=PASS' "$OUT/oracle-bagel.txt" \
   && grep -q 'compared=1 requested=1' "$OUT/oracle-bagel.txt"; then
  ok=$((ok + 1)); printf '  OK   %-22s bagel agrees with the oracle on all three counts\n' oracle-bagel
else
  bad=$((bad + 1)); printf '  FAIL %-22s rc=%s\n' oracle-bagel "$rc_orc"
  tail -6 "$OUT/oracle-bagel.txt" | sed 's/^/       | /'
fi

echo
echo "=== the committed evidence tree must be untouched ==="
# Asserted, not intended. Every gate in section 2 writes raw readings into
# evidence/<its-name>/ by default and those directories are committed; the
# redirection above is what keeps this tier out of them, and this check is what
# notices the day a new gate (or a new call site) forgets it.
EVIDENCE_AFTER=$(git status --porcelain evidence/ 2>/dev/null | sort)
if [ "$EVIDENCE_BEFORE" = "$EVIDENCE_AFTER" ]; then
  ok=$((ok + 1)); printf '  OK   %-22s nothing under evidence/ moved (tier output stayed in %s)\n' evidence-tree "$OUT"
else
  bad=$((bad + 1)); printf '  FAIL %-22s this tier wrote into the committed evidence tree:\n' evidence-tree
  diff <(printf '%s\n' "$EVIDENCE_BEFORE") <(printf '%s\n' "$EVIDENCE_AFTER") \
    | grep '^[<>]' | sed 's/^/       | /'
fi

echo
echo "gates_ok=$ok gates_bad=$bad"
if [ "$bad" -eq 0 ]; then
  # 8 counted items: the five gates, the wire fixtures, the one-demo oracle, and
  # the evidence-tree check.
  echo "VERIFY-ENTITY=PASS ($ok/8 gates, mutation=$MUTATION)"
  exit 0
fi
echo "VERIFY-ENTITY=FAIL ($bad red)"
exit 1
