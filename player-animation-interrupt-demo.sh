#!/usr/bin/env bash
# T61.1 review 3, P2 -- what an interrupt costs a mutation suite, and what the
# suites now do about it.
#
# The finding: both harnesses trapped EXIT/INT/TERM and restored the *sources*,
# but a case that had already rebuilt the probe or the product left a MUTANT
# binary behind. The next run's freshness check (F1: "the binary under test is
# newer than the sources it compiles") then sees a binary newer than the restored
# sources, so the tree passes freshness while running code that no longer exists
# on disk -- a baseline that is not a baseline.
#
# The fix, in both harnesses: on an interrupt the trap restores the sources, then
# removes the probe and the product and rebuilds them, printing
# INTERRUPT-RECOVERY=rebuilt; if that rebuild fails it prints
# INTERRUPT-RECOVERY=NOT-FOR-ACCEPTANCE and writes BINARIES-NOT-FOR-ACCEPTANCE.txt
# into the suite's output directory, so nothing downstream can mistake the tree
# for an acceptance baseline.
#
# This script is the reproducer. It starts one case of one suite in its own
# process group -- `set -m`, because a background job of a job-control-less shell
# inherits an ignored SIGINT and then the trap can never run -- waits for the
# mutant to be on disk and compiled, sends SIGINT (or SIGHUP with the second
# argument), and checks what is left.
#
#   usage: player-animation-interrupt-demo.sh [unit|pixel] [INT|HUP]
#     unit  -> player-animation-unit-mutations.sh u1   (mutates a header, rebuilds probe + product)
#     pixel -> player-animation-pixel-mutations.sh m12 (mutates main.cpp, rebuilds product)
#     INT   -> SIGINT (^C), wrapper must exit 130 -- the default
#     HUP   -> SIGHUP (closing the terminal), wrapper must exit 129; T61.1
#              review 4 asked for this path, which only INT/TERM used to cover
#
# Exit 0 means every post-condition held: wrapper rc 130 (INT) or 129 (HUP), the sources restored
# byte-identically (no TRAP-RESTORE-MISMATCH), INTERRUPT-RECOVERY=rebuilt in the
# suite log, and no BINARIES-NOT-FOR-ACCEPTANCE marker. The suite log is left in
# .scratch/interrupt-demo-<target>.log; run a capture gate afterwards if you want
# the forward proof that the rebuilt tree is green (the unit harness's own final
# baseline does exactly that in a normal run).
set -m

ROOT=$(cd "$(dirname "$0")" && pwd) || exit 9
cd "$ROOT" || exit 9

target=${1:-unit}
case "$target" in
  unit)
    suite=(bash player-animation-unit-mutations.sh u1)
    out=.scratch/interrupt-demo-out/unit
    ;;
  pixel)
    suite=(bash player-animation-pixel-mutations.sh m12)
    out=.scratch/interrupt-demo-out/pixel
    ;;
  *)
    echo "usage: $0 [unit|pixel] [INT|HUP]" >&2
    exit 9
    ;;
esac
mode=${2:-INT}
case "$mode" in
  INT) sig=INT; expect=130 ;;
  HUP) sig=HUP; expect=129 ;;
  *) echo "usage: $0 [unit|pixel] [INT|HUP]" >&2; exit 9 ;;
esac

log=".scratch/interrupt-demo-$target.log"
mkdir -p "$out" .scratch
: > "$log"

# Keep the demo's logs and captures out of the cited evidence: the harnesses read
# these two variables with the cited paths as defaults.
export T61_MUTATION_OUT="$out"
export T61_MUTATION_PIXEL_OUT="$out/pixels"

echo "=== interrupt demo: $target -- ${suite[*]}"
echo "=== logs and captures under $out; suite log $log"
"${suite[@]}" > "$log" 2>&1 &
child=$!

patched=0
for _ in $(seq 1 900); do
  if grep -q 'patched (backup' "$log"; then patched=1; break; fi
  kill -0 "$child" 2>/dev/null || break
  sleep 1
done
if [ "$patched" != "1" ]; then
  echo "DEMO-FAIL: the suite never patched a source (see $log)"
  kill "$child" 2>/dev/null
  wait "$child" 2>/dev/null
  exit 8
fi
# The patch is followed by a rebuild; signalling now lands while the mutant is
# being compiled, which is the worst case: a half-written binary AND mutant sources.
sleep 2
echo "=== signalling the suite shell (SIG$sig), the mutant is on disk"
date '+%H:%M:%S signal'
kill -"$sig" "$child"
wait "$child"
rc=$?
date '+%H:%M:%S done'
echo "WRAPPER-RC=$rc"

fail=0
if [ "$rc" != "$expect" ]; then echo "DEMO-FAIL: wrapper rc=$rc, expected $expect"; fail=1; fi
if grep -qE 'TRAP-RESTORE-(MISMATCH|MISSING-BACKUP|UNREADABLE-BACKUP)' "$log"; then
  echo "DEMO-FAIL: a restored source differs or a backup is unusable"; fail=1
fi
if ! grep -q '^INTERRUPT-RECOVERY=rebuilt' "$log"; then
  echo "DEMO-FAIL: no INTERRUPT-RECOVERY=rebuilt line"
  grep '^INTERRUPT-RECOVERY=' "$log" || true
  fail=1
fi
if [ -f "$out/BINARIES-NOT-FOR-ACCEPTANCE.txt" ]; then
  echo "DEMO-FAIL: $out/BINARIES-NOT-FOR-ACCEPTANCE.txt exists"
  fail=1
fi

echo "=== tail of the suite log:"
tail -6 "$log" | sed 's/^/    /'
echo "=== binaries:"
ls -l native/build-nmake/model_pose_probe.exe native/build-nmake/tf2_demo_native.exe 2>&1 | sed 's/^/    /'
if [ "$fail" = "0" ]; then
  echo "INTERRUPT-DEMO=PASS"
else
  echo "INTERRUPT-DEMO=FAIL"
fi
exit "$fail"
