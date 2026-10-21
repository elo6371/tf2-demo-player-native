#!/usr/bin/env bash
# T45 hold B: qualify T35's nine poses by the material-normal PAIR, then capture
# the wide-area evidence at the pose that qualifies.
#
# WHY THE PAIR QUALIFIES A POSE (measured in hold A, 2026-10-21):
#   --debug-camera moves the render view but NOT the cluster cull: every pose's
#   trace carries the same "gpu: world vis" line (cameraCluster=1032,
#   drawnTriangles=76344, eyeMap=(-2423.6,5566.0,-1280.0), the observer camera),
#   and the same bump-control scope (44/53462) and specular scope (100/76231).
#   So the trace counters cannot tell two poses apart and the only pose-variant
#   measurement is the frame. Each pose is therefore ranked by the pixel
#   population its material-normal switch moves: S1w (--world-specular on) vs
#   S2w (same + --world-material-bump on), the S2w frames having been captured in
#   hold A with the same build and the same flags.
#
#   Ranking: a pose qualifies only if BOTH directions are nonzero (the two-sided
#   claim this task needs), then larger brighter+darker wins.
#
#   The final six captures are taken inside THIS hold so the pinned pair is
#   same-build: S1w/S2w (specular consumer), Aw/Aw2 (pose determinism and the
#   "the pose moved the view" control), Cw/Dw (dynamic-light consumer).
# Run only under the shared host lock.
set -uo pipefail
source "$(dirname "$0")/lib.sh" || exit 2
OUT=.scratch/t45/wide
SEL="$OUT/sel"
Q=.scratch/t45/qualify
mkdir -p "$OUT" "$SEL"
echo "=== T45 wide-select $(date '+%Y-%m-%dT%H:%M:%S') ==="
echo "HEAD=$(git rev-parse HEAD) exe=$(sha "$PRODUCT" 2>/dev/null || echo absent)"

POSES='
GREEK_GROUND 1330.366 -1145.770 -200.216 49.608522 7.794318
BEAM001 1552.000 3776.000 -714.667 0.0 -0.0
BEAM002 1157.333 4050.667 -640.000 90.0 180.0
IMETAL005 -2474.671 3925.333 -736.000 90.0 -180.0
IMETAL006 -1035.250 1299.250 1096.000 90.0 -180.0
METALDOOR001 -2906.667 3402.667 -672.000 90.0 180.0
WALL013 -3498.225 1021.852 1327.843 70.529329 134.999352
WALL017 -3865.216 1041.451 1216.000 -0.0 45.0
DIRTGROUND4 -2901.333 3008.000 -936.000 90.0 -180.0
'

TABLE="$OUT/sel-table.txt"
printf '%-13s %-8s %-8s %-8s %-8s %s\n' POSE PAIRPX UP DOWN MAX FRAME > "$TABLE"
echo "$POSES" | while read -r name ex ey ez pitch yaw; do
  [ -z "$name" ] && continue
  s2="$Q/S2w-$name.bmp"
  if [ ! -s "$s2" ]; then echo "MISSING hold-A S2w for $name -- cannot qualify it"; continue; fi
  capture "$SEL" "S1w-$name" 2000 --world-specular on \
    --debug-camera "$ex" "$ey" "$ez" "$pitch" "$yaw" || { echo "CAPTURE-FAIL S1w-$name"; continue; }
  s1="$SEL/S1w-$name.bmp"
  line=$(python bmp-pixel-diff.py "$s1" "$s2" --max-diff-percent 100 | head -1)
  sign=$(python bmp-delta-sign.py "$s1" "$s2" | head -1)
  px=$(printf '%s' "$line" | grep -oE 'differing=[0-9]+' | cut -d= -f2)
  mx=$(printf '%s' "$line" | grep -oE 'max_delta=[0-9]+' | cut -d= -f2)
  up=$(printf '%s' "$sign" | grep -oE 'brighter=[0-9]+' | cut -d= -f2)
  dn=$(printf '%s' "$sign" | grep -oE 'darker=[0-9]+' | cut -d= -f2)
  printf '%-13s %-8s %-8s %-8s %-8s %s\n' "$name" "${px:-?}" "${up:-?}" "${dn:-?}" "${mx:-?}" \
    "$(sha "$s1")" >> "$TABLE"
  printf 'SEL %s px=%s up=%s down=%s max=%s\n' "$name" "${px:-?}" "${up:-?}" "${dn:-?}" "${mx:-?}"
done
cat "$TABLE"

echo "== ranking =="
# A pose qualifies only if both directions are nonzero; then the largest total
# moved byte count wins, then the largest pixel count.
BEST=$(tail -n +2 "$TABLE" | awk '$3 > 0 && $4 > 0 { print ($3+$4)" "$3" "$4" "$2" "$1 }' \
  | sort -k1,1nr -k2,2nr -k4,4nr | head -1)
if [ -z "$BEST" ]; then
  echo "NO QUALIFYING POSE: no pose moved pixels in both directions (see table)."
  echo "=== DONE $(date '+%Y-%m-%dT%H:%M:%S') rc=4 ==="
  exit 4
fi
WNAME=$(printf '%s' "$BEST" | awk '{print $5}')
read -r _ WX WY WZ WP WYW <<< "$(echo "$POSES" | awk -v n="$WNAME" '$1==n')"
echo "CHOSEN $WNAME eye=($WX,$WY,$WZ) pitch=$WP yaw=$WYW ranking=$(printf '%s' "$BEST" | awk '{print "total="$1" up="$2" down="$3" px="$4}')"
CAM=(--debug-camera "$WX" "$WY" "$WZ" "$WP" "$WYW")

echo "== final captures at $WNAME =="
capture "$OUT" Aw  2000 "${CAM[@]}" || exit 1
capture "$OUT" Aw2 2000 "${CAM[@]}" || exit 1
capture "$OUT" S1w 2000 --world-specular on "${CAM[@]}" || exit 1
capture "$OUT" S2w 2000 --world-specular on --world-material-bump on "${CAM[@]}" || exit 1
capture "$OUT" Cw  2000 --world-dynamic-light on "${CAM[@]}" || exit 1
capture "$OUT" Dw  2000 --world-dynamic-light on --world-material-bump on "${CAM[@]}" || exit 1

REP="$OUT/readings.txt"
{
  echo "pose=$WNAME eye=($WX,$WY,$WZ) pitch=$WP yaw=$WYW tick=2000"
  echo "HEAD=$(git rev-parse HEAD)"
  echo "cull_note=the vis/bump/specular counters are the observer camera's cull (same for every pose); the pose is proven by the frames"
  echo "frame Aw $(sha "$OUT/Aw.bmp")"
  echo "frame Aw2 $(sha "$OUT/Aw2.bmp")"
  echo "frame S1w $(sha "$OUT/S1w.bmp")"
  echo "frame S2w $(sha "$OUT/S2w.bmp")"
  echo "frame Cw $(sha "$OUT/Cw.bmp")"
  echo "frame Dw $(sha "$OUT/Dw.bmp")"
  echo "camera $(vistline "$OUT/Aw.trace")"
  echo "apply_S2w $(applyline "$OUT/S2w.trace")"
  echo "apply_Dw $(applyline "$OUT/Dw.trace")"
  echo "lighting_S1w $(lightingline "$OUT/S1w.trace")"
  echo "lighting_S2w $(lightingline "$OUT/S2w.trace")"
  echo "lighting_Cw $(lightingline "$OUT/Cw.trace")"
  echo "lighting_Dw $(lightingline "$OUT/Dw.trace")"
  echo "specular_control $(grep -a -m1 'control=specular ' "$OUT/S2w.trace" | tr -d '\r')"
  echo "bump_control $(grep -a -m1 'control=bump kind=' "$OUT/S2w.trace" | tr -d '\r')"
  echo "-- pose proof --"
  if [ -s "$Q/A.bmp" ]; then
    echo "Aw_vs_A $(run_diff "$Q/A.bmp" "$OUT/Aw.bmp")"
  else
    echo "Aw_vs_A n/a (hold A's A.png frame is gone)"
  fi
  echo "Aw_vs_Aw2 $(run_diff "$OUT/Aw.bmp" "$OUT/Aw2.bmp")"
  echo "S2w_vs_holdA_S2w $(run_diff "$Q/S2w-$WNAME.bmp" "$OUT/S2w.bmp")"
  echo "-- pairs --"
  echo "S1w_vs_S2w $(run_diff "$OUT/S1w.bmp" "$OUT/S2w.bmp")"
  echo "S1w_vs_S2w_sign $(run_sign "$OUT/S1w.bmp" "$OUT/S2w.bmp" | tr '\n' ' ')"
  echo "Cw_vs_Dw $(run_diff "$OUT/Cw.bmp" "$OUT/Dw.bmp")"
  echo "Cw_vs_Dw_sign $(run_sign "$OUT/Cw.bmp" "$OUT/Dw.bmp" | tr '\n' ' ')"
} | tee "$REP"
echo "=== DONE $(date '+%Y-%m-%dT%H:%M:%S') rc=0 ==="
