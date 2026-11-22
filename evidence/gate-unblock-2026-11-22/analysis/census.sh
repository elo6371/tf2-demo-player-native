#!/usr/bin/env bash
# Census: which tracked *.sh still name the dead POV recording.
#
# The report's §6 claim ("27 个 *.sh") comes from this script. Two kinds of file
# are excluded so the number counts the repo, not this bundle:
#   ./.scratch/                      scratch is never committed
#   evidence/gate-unblock-2026-11-22 the copies of the generator itself
# Run from the worktree root:  bash evidence/.../analysis/census.sh > census.out
set -uo pipefail
ROOT=${1:-/d/TF2_Native_Worktrees/t105-gates}
cd "$ROOT" || exit 1
DEAD=autorecord_2026-07-02_13-26-46
FILES=$(grep -rl "$DEAD" --include="*.sh" . \
  | grep -v '^./.scratch/' \
  | grep -v '^./evidence/gate-unblock-2026-11-22/' \
  | sort)
echo "root $ROOT"
echo "files $(printf '%s\n' "$FILES" | grep -c .)"
for f in $FILES; do
  base=$(basename "$f")
  reg=$(grep -c -- "$base" verify-all.sh 2>/dev/null || true)
  echo "### $base  path=$f  registered_hits=$reg"
  grep -n -- "$DEAD" "$f" | sed 's/^/    /'
done
