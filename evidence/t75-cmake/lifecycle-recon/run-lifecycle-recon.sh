#!/usr/bin/env bash
# T75 lifecycle reconciliation: one locked window runs the forward gate, then the
# mutation gate against the same out-dir (the mutation run needs the forward run's
# artifacts). This is the committed copy of the runner used for
# evidence/t75-cmake/T75-LIFECYCLE-RECON.md (2026-10-28 22:55:19 - 23:00:07);
# two deliberate differences from the .scratch copy that produced those readings:
#   1. the tree is located from this script instead of the hardcoded worktree path;
#   2. it exits with the first non-zero gate rc (the run's copy printed the rcs and
#      always exited 0 -- both gates were 0 that time).
set -uo pipefail
cd "$(dirname "$0")/../../.." || exit 2
OUT="${LIFECYCLE_OUT:-.scratch/t75-lifecycle-recon/out}"
rm -rf "$OUT"; mkdir -p "$OUT"
echo "START $(date '+%Y-%m-%d %H:%M:%S')"
bash install-lifecycle-check.sh --forward "$OUT" > "$OUT/forward-gate.log" 2>&1
FRC=$?
echo "FORWARD-RC=$FRC $(date '+%Y-%m-%d %H:%M:%S')"
bash install-lifecycle-check.sh --mutation "$OUT" > "$OUT/mutation-gate.log" 2>&1
MRC=$?
echo "MUTATION-RC=$MRC $(date '+%Y-%m-%d %H:%M:%S')"
echo "--- forward tail ---"; tail -6 "$OUT/forward-gate.log"
echo "--- mutation tail ---"; tail -8 "$OUT/mutation-gate.log"
echo "END $(date '+%Y-%m-%d %H:%M:%S')"
if [ "$FRC" != "0" ]; then exit "$FRC"; fi
exit "$MRC"
