#!/usr/bin/env bash
# Queued locked command: hand off to the full driver. Kept as a separate file so
# this one is never rewritten while the queue is holding a ticket.
set -uo pipefail
cd /d/TF2_Native_Worktrees/t100-userinfo
exec bash evidence/t100-userinfo/logs/driver-full.sh
