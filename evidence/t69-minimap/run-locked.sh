#!/usr/bin/env bash
# run-locked.sh -- everything T69 needs the HOST for, in ONE lock window.
#
# WHY ONE WINDOW
#   The lock queue is shared with three other tasks. Acquiring once and doing
#   build -> gate -> mutation suite -> final baseline -> release keeps this task
#   from re-queuing five times, and keeps every rebuild of the product inside a
#   serialized window (the mutation suite rebuilds minimap_probe nine times).
#
# Run it THROUGH hostlock, not directly:
#   HOSTLOCK_TIMEOUT=7200 bash /d/TF2_Native_Worktrees/_shared/hostlock.sh run t69-minimap bash evidence/t69-minimap/run-locked.sh
set -uo pipefail
cd "$(dirname "$0")/../.." || exit 2
LOGS=evidence/t69-minimap/logs
mkdir -p "$LOGS"

{
  echo "HEAD=$(git rev-parse HEAD)"
  echo "BRANCH=$(git rev-parse --abbrev-ref HEAD)"
  echo "DATE=$(date '+%Y-%m-%d %H:%M:%S %z')"
  git status --porcelain
} > "$LOGS/identity.txt" 2>&1
cat "$LOGS/identity.txt"

echo "=== step 1: build minimap_probe ==="
bash build-target.sh minimap_probe > "$LOGS/build.log" 2>&1
BRC=$?
tail -5 "$LOGS/build.log"
echo "BUILD_RC=$BRC"
if [ "$BRC" -ne 0 ]; then
  echo "T69-LOCKED-RUN=FAIL reason=build"
  exit 1
fi

echo "=== step 2: gate baseline ==="
bash minimap-check.sh > "$LOGS/gate-baseline.log" 2>&1
GRC=$?
tail -4 "$LOGS/gate-baseline.log"
echo "GATE_RC=$GRC"
if [ "$GRC" -ne 0 ]; then
  echo "T69-LOCKED-RUN=FAIL reason=gate-baseline"
  exit 1
fi

echo "=== step 3: mutation suite ==="
bash evidence/t69-minimap/run-mutations.sh > "$LOGS/mutations.log" 2>&1
MRC=$?
grep -E '^(BASELINE=|PATCH-OK|MUTATED-BUILD-RC|MUTATION-|RESTORE-|BASELINE-RESTORED|BASELINE-BROKEN|mutations_caught)' "$LOGS/mutations.log"
echo "MUTATION_RC=$MRC"

echo "=== step 4: final baseline after all restores ==="
bash build-target.sh minimap_probe > "$LOGS/final-build.log" 2>&1
FRC=$?
bash minimap-check.sh > "$LOGS/gate-final.log" 2>&1
GRC2=$?
tail -3 "$LOGS/gate-final.log"
echo "FINAL_BUILD_RC=$FRC FINAL_GATE_RC=$GRC2"

if [ "$MRC" -eq 0 ] && [ "$FRC" -eq 0 ] && [ "$GRC2" -eq 0 ]; then
  echo "T69-LOCKED-RUN=PASS"
  exit 0
fi
echo "T69-LOCKED-RUN=FAIL reason=mutation_or_final"
exit 1
