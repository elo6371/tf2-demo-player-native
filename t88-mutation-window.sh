#!/bin/bash
# t88-mutation-window.sh -- the two named mutations, inside one held lock.
#
# WHY SEPARATE FROM t88-window.sh
#   The first window establishes the pins and the picture; this one proves the
#   gate can go red. They are separate leases so a slow mutation rebuild cannot
#   make the evidence window miss its slot, and so the mutation logs stand alone
#   as the record of "this gate is not one that can only pass".
#
# WHAT IT RUNS
#   world-geometry-budget-check.sh --mutation  (capacity shrink, whitelist
#   rollback; each rebuilds, must go red at a NAMED assertion, restores the
#   source byte-identically, rebuilds, and must come back green)
#
# It must be started as:  bash hostlock.sh run t88-budget bash t88-mutation-window.sh
set -uo pipefail
cd "$(dirname "$0")"
export HOLD_LOCK=1
OUTDIR=evidence/t88-maps
mkdir -p "$OUTDIR"
LOG=$OUTDIR/mutation-window-$(date +%Y%m%d-%H%M%S).log
{
  echo "== mutation window start $(date -Iseconds) owner=t88-budget pid=$$"
  bash world-geometry-budget-check.sh --mutation
  mrc=$?
  echo "mutation rc=$mrc"
  echo "-- source state after the mutations (must show no mutation left behind)"
  git diff --stat native/src/main.cpp native/src/bsp_map.cpp
  echo "== mutation window end $(date -Iseconds) rc=$mrc"
  exit $mrc
} 2>&1 | tee "$LOG"
