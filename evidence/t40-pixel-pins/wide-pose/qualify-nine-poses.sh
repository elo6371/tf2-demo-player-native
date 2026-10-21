#!/usr/bin/env bash
# T45 hold A: build the focused targets, then qualify T35's nine-pose set as a
# wide-area material-normal camera at tick 2000.
#
# WHY: world-material-semantics-check.sh's tick-2000 specular pair S1/S2 is
# extinct after T29 (S1 == S2 byte-identical, pinned as a measured zero and
# printed NOT VERIFIED). T35 solved the nine poses with the probe but never
# captured S1/S2 there (the host queue starved it). This script takes the S2w
# frame of every pose -- one semantic switch is not being compared here, the
# frame exists to read the product's own bump.materials/tiles/vertices -- and
# prints the table the pose choice is made from. The pair itself (S1w vs S2w,
# one switch apart) is captured for the chosen pose only, in wide.sh.
#
# A/A2 are the gate's own reference flags, so A must hash to the committed
# tick-2000 pin 6bb4d9a7...: that is the exact-tick/camera/determinism control.
# Run only under the shared host lock.
set -uo pipefail
source "$(dirname "$0")/lib.sh" || exit 2
OUT=.scratch/t45/qualify
mkdir -p "$OUT"
echo "=== T45 qualify $(date '+%Y-%m-%dT%H:%M:%S') ==="
echo "HEAD=$(git rev-parse HEAD) branch=$(git rev-parse --abbrev-ref HEAD)"
echo "exe_before=$(sha "$PRODUCT" 2>/dev/null || echo absent)"

echo "== build =="
bash build-target.sh tf2_demo_native world_feature_probe > "$OUT/build.txt" 2>&1
rc=$?
echo "build_rc=$rc exe=$(sha "$PRODUCT" 2>/dev/null || echo absent) probe=$(sha "$PROBE" 2>/dev/null || echo absent)"
if [ "$rc" -ne 0 ]; then echo "BUILD FAILED see $OUT/build.txt"; grep -a -E "error|Error" "$OUT/build.txt" | head -20; exit 1; fi
git status --short native/ > "$OUT/status-native.txt" 2>&1

echo "== reference frame A and its repeat A2 (gate flags) =="
capture "$OUT" A 2000
capture "$OUT" A2 2000
echo "A  hash=$(sha "$OUT/A.bmp")  pin=6bb4d9a712e82ef4cf58929651cea1070d11c1ac082bdb25a6e6d7097e2f974b"
echo "A2 hash=$(sha "$OUT/A2.bmp")"
if cmp -s "$OUT/A.bmp" "$OUT/A2.bmp"; then echo "DETERMINISM=OK A==A2 byte-identical"; else echo "DETERMINISM=FAIL A != A2"; fi

# name eye_x eye_y eye_z pitch yaw  (T35's probe-derived set, .scratch/t35/analysis/camera)
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

TABLE="$OUT/pose-table.txt"
printf '%-13s %-6s %-6s %-8s %-10s %-10s %-26s %s\n' POSE TILES MATS VERT BUMPVIS SPECSCOPE EYEMAP FRAME > "$TABLE"
echo "$POSES" | while read -r name ex ey ez pitch yaw; do
  [ -z "$name" ] && continue
  capture "$OUT" "S2w-$name" 2000 --world-specular on --world-material-bump on \
    --debug-camera "$ex" "$ey" "$ez" "$pitch" "$yaw" || { echo "CAPTURE-FAIL $name"; continue; }
  trace="$OUT/S2w-$name.trace"
  apply=$(applyline "$trace")
  spec=$(grep -aF "gpu: world lighting-control control=specular " "$trace" | head -1 | tr -d '\r')
  # The visible population of the 44 material-normal materials: the bump control
  # row counts what the cull kept, which is the number a pose is chosen on.
  bumpr=$(grep -aF "gpu: world lighting-control control=bump kind=" "$trace" | head -1 | tr -d '\r')
  vis=$(vistline "$trace")
  line=$(lightingline "$trace")
  printf '%-13s %-6s %-6s %-8s %-10s %-10s %-26s %s\n' \
    "$name" "$(af "$trace" bump.tiles)" "$(af "$trace" bump.materials)" \
    "$(af "$trace" bump.vertices)" \
    "$(lfield "$bumpr" materials)/$(lfield "$bumpr" triangles)" \
    "$(lfield "$spec" materials)/$(lfield "$spec" triangles)" \
    "$(lfield "$vis" eyeMap)" "$(sha "$OUT/S2w-$name.bmp")" >> "$TABLE"
  printf 'SPEC-STATE %s %s | %s\n' "$name" "$(lfield "$line" specular)/$(lfield "$line" bump)" "$apply" >> "$OUT/pose-states.txt"
done
cat "$TABLE"
echo "=== DONE $(date '+%Y-%m-%dT%H:%M:%S') ==="
