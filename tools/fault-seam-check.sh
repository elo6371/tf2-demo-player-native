#!/usr/bin/env bash
# fault-seam-check.sh -- pull the failure seams that the probes already implement
# but that no gate was calling, and require the failure to be NAMED and the valid
# path to survive it.
#
# WHY IT EXISTS (N23)
#   audio_effects_probe implements a whole waveOut failure harness -- nine injected
#   device opens, a reset that fails, an unprepare that fails, a cleanup failure --
#   and asserts on it. As of 2026-11-15 NOTHING IN THE TREE CALLED IT: grep for
#   --player-fault-selftest across every *.sh returned zero hits. The seam existed,
#   the assertions existed, and the readings existed; they just never gated a
#   commit. A seam nobody pulls is a comment.
#
# WHAT IT ASSERTS
#   1. the harness ran and reported success            (playerFaultSelfTest)
#   2. it actually injected work                       (fakeOpens, fakeResets,
#                                                       fakeUnprepares,
#                                                       fakeCleanupFailures)
#   3. every injected failure was NAMED, none swallowed (failedChecks == 0)
#   4. the real backend came back afterwards            (backendRestored)
#   5. the VALID path still works: the probe run with no fault flag returns 0
#      after the seam has fired -- a seam that leaves the backend broken would
#      trade a readable failure for a silent one.
#
# USAGE
#   bash tools/fault-seam-check.sh            # check
#   bash tools/fault-seam-check.sh --selftest # prove each assertion can go red
#
# EXIT: 0 = the seam fired, was named, and left a working backend; 1 = a finding;
#       2 = cannot run (no probe / no build tree).
#
# SCOPE NOTE (honest)
#   The D3D half of N23 -- --renderer-fault + the pixel assertion D48 asks for --
#   is NOT here. Its driver is the product exe, whose main.cpp carried a syntax
#   error from another instance for the whole of this batch; the resize selftest
#   also needs a window. Recorded in the E3b report rather than faked here.
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PROBE="${FAULT_PROBE:-$ROOT/native/build-nmake/audio_effects_probe.exe}"
MODE=check
case "${1:-}" in
  --selftest) MODE=selftest ;;
  -h|--help) sed -n "2,40p" "$0"; exit 0 ;;
  "") ;;
  *) echo "unknown argument: $1" >&2; exit 2 ;;
esac

field() { # field <json> <name> -> the raw value, unquoted
  printf "%s" "$1" | grep -o "\"$2\":[^,}]*" | head -1 | sed "s/^[^:]*://" | tr -d "\""
}

analyze() { # analyze <json> -> findings on stdout, rc 1 if any
  local json="$1" v fails=0
  is_true() { [ "$(field "$json" "$1")" = "true" ]; }
  at_least_one() { local n; n=$(field "$json" "$1"); case "$n" in ""|0) return 1 ;; *) return 0 ;; esac; }

  if ! is_true playerFaultSelfTest; then
    echo "  FAIL harness-did-not-report playerFaultSelfTest=$(field "$json" playerFaultSelfTest)"
    fails=1
  fi
  # Zero injected opens would mean the seam armed nothing and every assertion
  # below passed vacuously -- the exact shape of a gate that cannot fail.
  for counter in fakeOpens fakeResets fakeUnprepares fakeCleanupFailures; do
    if ! at_least_one "$counter"; then
      echo "  FAIL seam-injected-nothing $counter=$(field "$json" "$counter")"
      fails=1
    fi
  done
  v=$(field "$json" failedChecks)
  if [ "${v:-x}" != "0" ]; then
    echo "  FAIL failure-not-named failedChecks=$v failed=$(field "$json" failed)"
    fails=1
  fi
  if ! is_true backendRestored; then
    echo "  FAIL backend-left-broken backendRestored=$(field "$json" backendRestored)"
    fails=1
  fi
  return "$fails"
}

run_seam() { # run_seam -> echoes the probe line, rc of the probe
  local out rc
  out=$("$PROBE" --player-fault-selftest 2>&1)
  rc=$?
  printf "%s" "$out"
  return "$rc"
}

if [ "$MODE" = selftest ]; then
  # A check that cannot go red is not a check. Each case below is a real defect
  # shape this gate is supposed to catch; every one must produce a FAIL line.
  base='{"playerFaultSelfTest":true,"failedChecks":0,"failed":"","fakeOpens":9,"fakeCloses":0,"fakeResets":1,"fakeUnprepares":1,"fakeCleanupFailures":1,"deviceOpenCalls":9,"backendRestored":true}'
  good='"playerFaultSelfTest":true,"failedChecks":0,"failed":"","fakeOpens":9,"fakeCloses":0,"fakeResets":1,"fakeUnprepares":1,"fakeCleanupFailures":1,"deviceOpenCalls":9,"backendRestored":true'
  cases=(
    "clean-seam|{$good}|expect-pass"
    "failure-not-named|{\"playerFaultSelfTest\":true,\"failedChecks\":2,\"failed\":\"waveOutWrite\",\"fakeOpens\":9,\"fakeResets\":1,\"fakeUnprepares\":1,\"fakeCleanupFailures\":1,\"backendRestored\":true}|expect-fail"
    "backend-left-broken|{\"playerFaultSelfTest\":true,\"failedChecks\":0,\"failed\":\"\",\"fakeOpens\":9,\"fakeResets\":1,\"fakeUnprepares\":1,\"fakeCleanupFailures\":1,\"backendRestored\":false}|expect-fail"
    "seam-injected-nothing|{\"playerFaultSelfTest\":true,\"failedChecks\":0,\"failed\":\"\",\"fakeOpens\":0,\"fakeResets\":1,\"fakeUnprepares\":1,\"fakeCleanupFailures\":1,\"backendRestored\":true}|expect-fail"
    "harness-did-not-report|{\"playerFaultSelfTest\":false,\"failedChecks\":0,\"failed\":\"\",\"fakeOpens\":9,\"fakeResets\":1,\"fakeUnprepares\":1,\"fakeCleanupFailures\":1,\"backendRestored\":true}|expect-fail"
  )
  bad=0
  for case in "${cases[@]}"; do
    name=${case%%|*}; rest=${case#*|}; json=${rest%|*}; want=${rest##*|}
    if out=$(analyze "$json"); then got=expect-pass; else got=expect-fail; fi
    if [ "$got" = "$want" ]; then
      echo "  ok $name -> $got"
    else
      echo "  FAIL selftest case=$name want=$want got=$got"
      printf "%s\n" "$out" | sed "s/^/    /"
      bad=1
    fi
  done
  if [ "$bad" -ne 0 ]; then echo "FAULT-SEAM=SELFTEST-FAIL cases=${#cases[@]}"; exit 1; fi
  echo "FAULT-SEAM=SELFTEST-PASS cases=${#cases[@]}"
  exit 0
fi

[ -f "$PROBE" ] || { echo "FAULT-SEAM=FAIL reason=no-probe path=$PROBE" >&2; exit 2; }

line=$(run_seam); rc=$?
echo "FAULT-SEAM probe=audio_effects_probe.exe mtime=$(date -r "$PROBE" +%Y-%m-%dT%H:%M:%S)"
if [ "$rc" -ne 0 ]; then
  echo "FAULT-SEAM=FAIL reason=probe-rc rc=$rc line=$line"
  exit 1
fi
findings=$(analyze "$line")
if [ -n "$findings" ]; then
  printf "%s\n" "$findings"
  echo "FAULT-SEAM=FAIL reason=seam-findings line=$line"
  exit 1
fi

# The seam has now fired and cleaned up nine fake devices. The valid path has to
# still work: run the probe with no fault flag and require it to succeed.
"$PROBE" > /tmp/fault-seam-valid.txt 2>&1; valid_rc=$?
if [ "$valid_rc" -ne 0 ]; then
  echo "FAULT-SEAM=FAIL reason=valid-path-degraded after-seam rc=$valid_rc"
  head -3 /tmp/fault-seam-valid.txt | sed "s/^/  /"
  exit 1
fi
echo "FAULT-SEAM=PASS seams=winmm injected=9 named=0-unnamed backend-restored=yes valid-path=ok"