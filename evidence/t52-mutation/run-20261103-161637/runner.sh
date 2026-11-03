#!/usr/bin/env bash
# A2 outline gate rerun after fixing the absolute-OUTLINE_OUT path bug.
# hostlock.sh run executes "$@" directly, so the log redirection lives here.
set -uo pipefail
exec > /d/_shared/a2-outline-final2.log 2>&1
cd /d/TF2_Native_Worktrees/t79-integration || { echo "FAIL cannot cd to tree"; exit 99; }
echo "WRAPPER-HEAD $(git rev-parse HEAD)"
echo "WRAPPER-BRANCH $(git rev-parse --abbrev-ref HEAD)"
echo "WRAPPER-STATUS $(git status --porcelain | tr '\n' '|')"
export OUTLINE_OUT="D:/_shared/a2-outline-final2"
set +e
bash outline-check.sh --mutation
rc=$?
echo "OUTLINE-EXIT rc=$rc"
exit $rc
