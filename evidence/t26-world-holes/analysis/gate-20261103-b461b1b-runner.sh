#!/usr/bin/env bash
set -u
cd /d/TF2_Native_Worktrees/t79-integration || exit 3
bash world-geometry-hole-check.sh > /d/_shared/b5-world-hole-final.log 2>&1
echo "WRAPPER-RC=$?"
exit 0
