#!/usr/bin/env bash
# lockrun.sh <owner> -- <command...>
# Runs one build/capture window under the shared host lock (verify-lock.sh).
# The lock is self-healing: a dead owner or a stale heartbeat is reclaimed.
set -uo pipefail
cd "$(dirname "$0")/.." || exit 2
. ./verify-lock.sh
OWNER="${1:?usage: lockrun.sh <owner> -- <cmd...>}"
shift
[ "${1:-}" = "--" ] && shift
[ "$#" -ge 1 ] || { echo "usage: lockrun.sh <owner> -- <cmd...>" >&2; exit 2; }
LOCK_DIR="$PWD/.scratch/verify-all.lock"
vlock_acquire "$LOCK_DIR" "$OWNER" || exit 2
trap 'vlock_release "$LOCK_DIR"' EXIT

# N03/D40: the product writes --settings-file back on exit, so a caller that
# points it at a tracked fixture edits the repository. Every run under this lock
# checks the fixtures before and after and fails loudly if they moved.
# evidence/ is in the same class: world-material-semantics-check.sh rewrites its
# checked-in readings before it decides the run failed, so a red run left eleven
# tracked evidence files modified (measured 2026-11-14, D49).
FIXTURES="native/tests/fixtures evidence"
before=$(git status --porcelain -- $FIXTURES 2>/dev/null)
"$@"
rc=$?
after=$(git status --porcelain -- $FIXTURES 2>/dev/null)
if [ "$before" != "$after" ]; then
  echo "LOCKRUN=TRACKED-DIRTY $FIXTURES changed during this run (D40/D49):" >&2
  printf '%s\n' "$after" >&2
  echo "  restore with: git checkout -- $FIXTURES" >&2
  exit 97
fi
exit $rc
