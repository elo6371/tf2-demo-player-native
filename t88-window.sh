#!/bin/bash
# t88-window.sh -- one held-lock window for the map-budget fix.
#
# WHY A WRAPPER
#   The shared host lock is the scarce resource here, and every rebuild and every
#   captured frame needs it. Running the steps inside ONE acquisition keeps the
#   window short and, more importantly, keeps them on the same binary: the gate
#   writes pins from the build that the pixel attribution then measures.
#
# WHAT IT RUNS (and what it prints, so the log is the evidence)
#   1. build-target.sh tf2_demo_native world_geometry_probe   (timed, rc gate)
#   2. world-geometry-budget-check.sh --re-pin=<why>          (B1-B6, H1)
#   3. world-geometry-budget-check.sh --pixel-attr            (face-id attribution)
#   4. cost readings: build seconds, peak WorkingSetSize and fps range from the
#      product's own metrics CSVs (columns by name, not by position)
#
# It must be started as:  bash hostlock.sh run t88-budget bash t88-window.sh
# HOLD_LOCK=1 is exported so the child scripts prove the lock is theirs instead
# of acquiring it a second time (that would deadlock).
set -uo pipefail
cd "$(dirname "$0")"
export HOLD_LOCK=1
OUTDIR=evidence/t88-maps
mkdir -p "$OUTDIR"
LOG=$OUTDIR/window-$(date +%Y%m%d-%H%M%S).log
SCRATCH=.scratch/t88-budget
PIXELS=evidence/t88-maps/pixels

cost_readings() {
  echo "-- cost: peak working set and fps from the metrics CSVs under $SCRATCH"
  local f
  for f in "$SCRATCH"/*/*.csv "$PIXELS"/*.csv; do
    [[ -s $f ]] || continue
    awk -F, 'NR==1 { for (i=1;i<=NF;i++) { if ($i=="WorkingSetSize") w=i; if ($i=="fps") p=i; if ($i=="renderedFrames") r=i } next }
      w && $w ~ /^[0-9]+$/ { if ($w+0 > maxw) maxw=$w+0; if (!minw || $w+0 < minw) minw=$w+0 }
      p && $p ~ /^[0-9.]+$/ { if ($p+0 > maxf) maxf=$p+0; if (!minf || $p+0 < minf) minf=$p+0 }
      r && $r ~ /^[0-9]+$/ { frames=$r+0 }
      END { if (maxw) printf "  %-46s peakWorkingSet=%d MiB (%d bytes) frames=%d fps=%s..%s\n", FILENAME, maxw/1048576, maxw, frames, (minf?minf:"-"), (maxf?maxf:"-") }' "$f"
  done
}

{
  echo "== window start $(date -Iseconds) owner=t88-budget pid=$$"
  t0=$(date +%s)
  echo "-- step build"
  bash build-target.sh tf2_demo_native world_geometry_probe
  build_rc=$?
  t1=$(date +%s)
  echo "build rc=$build_rc seconds=$((t1 - t0))"
  if [[ $build_rc -ne 0 ]]; then
    echo "== window end (build failed, nothing captured) $(date -Iseconds)"
    exit 1
  fi
  echo "-- step gate --re-pin"
  # --reuse: the 16:16 window already captured four traces on this exact binary
  # sha, and those captures are the ones being asserted; reusing them keeps the
  # window short without changing what is checked.
  bash world-geometry-budget-check.sh \
    --reuse \
    --re-pin="T88: capacity 400000 sized from measured demand (242361 worst map); func_illusionary restored to the world whitelist"
  gate_rc=$?
  echo "gate rc=$gate_rc"
  if [[ $gate_rc -ne 0 ]]; then
    cost_readings
    echo "== window end (gate failed, pixels not run) $(date -Iseconds)"
    exit 1
  fi
  echo "-- step pixel-attr"
  bash world-geometry-budget-check.sh --pixel-attr
  pix_rc=$?
  echo "pixels rc=$pix_rc"
  cost_readings
  echo "-- git status after the window (a mutation must not be left behind)"
  git status --porcelain
  echo "== window end $(date -Iseconds) build=$build_rc gate=$gate_rc pixels=$pix_rc"
  if [[ $pix_rc -ne 0 ]]; then exit 1; fi
} 2>&1 | tee "$LOG"
