#!/usr/bin/env bash
# Host-lock retry wrapper under the owner name this task was told to use.
set -u
cd /d/TF2_Native_Worktrees/t105-leaf37b || exit 1
for i in $(seq 1 180); do
  bash /d/_shared/tf2-hostlock/hostlock.sh leaf37d -- bash tools/lockrun.sh leaf37d -- "$@"
  rc=$?
  [ "$rc" -eq 2 ] || exit "$rc"
  echo "RETRY owner=leaf37d attempt=$i rc=2"
  sleep 10
done
echo "RETRY-EXHAUSTED owner=leaf37d"
exit 2
