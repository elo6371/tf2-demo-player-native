#!/usr/bin/env bash
# Short window: run the forward gate on the COMMITTED CLEAN tree (0 porcelain
# records). Every earlier evidence run was made on a dirty tree (15-48 records);
# step 8 of the verify-all chain will see a clean checkout, so this exercises the
# clean path once, by hand, before the chain does.
set -u
cd /d/TF2_Native_Worktrees/t90-identity || exit 9
LOG=.scratch/t90/gate-forward-clean.log
echo "=== clean-forward start $(date -u +%Y-%m-%dT%H:%M:%SZ) head=$(git rev-parse --short=12 HEAD)"
echo "records_before=$(git status --porcelain -uall | wc -l)"
sha256sum build-identity-check.sh
bash build-identity-check.sh --forward .scratch/t90/forward-clean > "$LOG" 2>&1
RC=$?
echo "gate_forward_clean_rc=$RC"
echo "--- last lines of $LOG"
tail -12 "$LOG"
echo "sha_after=$(sha256sum build-identity-check.sh)"
echo "records_after=$(git status --porcelain -uall | wc -l)"
echo "git_status_after:"; git status --porcelain -uall | head -20
echo "=== clean-forward done $(date -u +%Y-%m-%dT%H:%M:%SZ)"
exit "$RC"
