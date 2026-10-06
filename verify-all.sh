#!/usr/bin/env bash
# verify-all.sh -- one command that reproduces the whole P0 evidence chain.
#
#   1. clean full Release build (21 exe) and check error/warning counts
#   2. census all nine local demos
#   3. run the 58 wire fixtures
#   4. cross-check against the independent Rust oracle
#   5. pin the recording type (POV vs SourceTV) of the nine oracle demos
#   6. prove the probe's stream-recording change moved no existing counter
#   7. prove the readings can go red (C++ mutation suite)
#   8. prove the corpus-census verdict can go red
#
# Anything that must be true for the delivery is asserted here, so a reviewer
# does not have to read nine reports. Every step writes its raw output to
# evidence/ before the assertion runs.
#
# Steps 5, 6 and 8 exist because the first P0 acceptance pass got three things
# wrong that no green reading caught:
#   * it reported all nine oracle demos as POV; five are SourceTV match demos
#     (their clientname is "SourceTV Demo"), so the SourceTV corpus was never
#     actually missing -- the classifier was.
#   * the corpus census printed PASS while parsing only the first line of each
#     report, so every "must be zero" field was absent and skipped.
#   * the probe gained an output line with nothing proving the old counters
#     were untouched.
#
# Usage: bash verify-all.sh
# Exit:  0 = every step passed.
set -uo pipefail
cd "$(dirname "$0")"

OUT=evidence/verify
mkdir -p "$OUT"
rc_all=0

step() { printf '\n=== %s ===\n' "$1"; }

step "1/8 clean full Release build"
bash build-cmake.sh > "$OUT/1-build.txt" 2>&1
tail -6 "$OUT/1-build.txt"
if grep -q 'cmake_build_rc=0' "$OUT/1-build.txt" && grep -q '^errors=0$' "$OUT/1-build.txt"; then
  echo "BUILD=PASS"
else
  echo "BUILD=FAIL"; rc_all=1
fi

step "2/8 nine-demo census"
bash run-demos.sh evidence/final | tee "$OUT/2-census.txt"
if [ "$(grep -c 'entity_failures=0 malformed_packets=0 unknown_message_packets=0' "$OUT/2-census.txt")" -eq 9 ]; then
  echo "CENSUS=PASS (9/9 demos at zero)"
else
  echo "CENSUS=FAIL"; rc_all=1
fi
# The message-type coverage assertion: no observed type may lack a decoder.
if [ "$(grep -c 'message_types_seen_without_decoder: <none>' evidence/final/*.txt | grep -c ':1$')" -eq 9 ]; then
  echo "COVERAGE=PASS (9/9 demos, every observed message type decoded)"
else
  echo "COVERAGE=FAIL"; rc_all=1
fi

step "3/8 wire fixtures"
native/build-nmake/entity_message_fixture_probe.exe > "$OUT/3-fixture.txt" 2>&1
grep -c '^PASS' "$OUT/3-fixture.txt" | sed 's/^/fixture_pass_count=/'
tail -1 "$OUT/3-fixture.txt"
if grep -q '^fixture_failures=0$' "$OUT/3-fixture.txt"; then
  echo "FIXTURE=PASS"
else
  echo "FIXTURE=FAIL"; rc_all=1
fi

step "4/8 oracle cross-check"
bash check-oracle.sh "D:/TF2_Demo_Player_Deliverable/tools/ent-oracle/target/release/ent-oracle.exe" evidence/final \
  | tee "$OUT/4-oracle.txt"
if grep -q 'ORACLE-GATE=PASS' "$OUT/4-oracle.txt"; then
  echo "ORACLE=PASS"
else
  echo "ORACLE=FAIL"; rc_all=1
fi

step "5/8 oracle demo recording types"
bash oracle-recording-types.sh | tee "$OUT/5-recording-types.txt"
if grep -q 'ORACLE-RECORDING-TYPES=PASS' "$OUT/5-recording-types.txt"; then
  echo "RECORDING-TYPES=PASS"
else
  echo "RECORDING-TYPES=FAIL"; rc_all=1
fi

step "6/8 probe output is additive"
bash check-probe-output-additive.sh | tee "$OUT/6-probe-additive.txt"
if grep -q 'PROBE-OUTPUT-ADDITIVE=PASS' "$OUT/6-probe-additive.txt"; then
  echo "PROBE-ADDITIVE=PASS"
else
  echo "PROBE-ADDITIVE=FAIL"; rc_all=1
fi

step "7/8 mutation suite (C++)"
bash mutate.sh > "$OUT/7-mutation.txt" 2>&1
tail -8 "$OUT/7-mutation.txt"
if grep -q 'MUTATION-SUITE=PASS' "$OUT/7-mutation.txt"; then
  echo "MUTATION=PASS"
else
  echo "MUTATION=FAIL"; rc_all=1
fi

step "8/8 census verdict is falsifiable"
bash census-negative-test.sh | tee "$OUT/8-census-negative.txt"
if grep -q 'CENSUS-NEGATIVE=PASS' "$OUT/8-census-negative.txt"; then
  echo "CENSUS-NEGATIVE=PASS"
else
  echo "CENSUS-NEGATIVE=FAIL"; rc_all=1
fi

printf '\n'
if [ "$rc_all" -eq 0 ]; then echo "VERIFY=PASS"; else echo "VERIFY=FAIL"; fi
exit "$rc_all"
