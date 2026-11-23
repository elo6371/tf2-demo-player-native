#!/usr/bin/env bash
# sultry-translucent-atlas-check.sh -- I-23: a translucent material that the
# world atlas refuses for budget must get its own rescue batch, not a flat
# opaque fallback.
#
# WHY THIS FILE EXISTS
# --------------------
# planWorldTiles() walks priced batches in order and stops at the first batch
# that does not fit the 384 MiB budget. A translucent material in a refused
# batch gets no tile; the tiled alpha branch is never entered, the material is
# drawn by the untiled branch with alpha 0, and its trace row says
# reason=atlas-budget while the alpha row says mode=refused. On cp_sultry_b8a
# that was 10 materials / 42 triangles / 18 PVS-visible triangles.
#
# The fix (native/src/world_tile_atlas.cpp: worldTileRescueTranslucent) runs a
# second plan over exactly those materials, priced against what is left of the
# same budget, and appends its batch. The planner itself is unchanged.
#
# WHAT IT ASSERTS
#   1. the positive Sultry frame: batches 3 -> 4, tiled 192 -> 202,
#      refused 58 -> 48, budget 58 -> 48, translucentRefused 10 -> 0,
#      layerBytes 338170880 -> 350852288, atlases 3 -> 4, totalBytes
#      202902528 -> 215583936, draw calls 6 -> 7, blended triangles
#      1334 -> 1376, blended batches 3 -> 4;
#   2. the same ten materials now have batch=3, reason=tiled, and an alpha row
#      mode=blended reason=$translucent -- by name, not by count;
#   3. the negative frame, produced by the product's own off switch
#      --world-atlas-no-translucent-rescue, still prints translucentRefused=10
#      and puts the ten materials back on batch=-1 / mode=refused. That switch
#      is what makes the positive frame falsifiable: an always-on rescue would
#      make this run read 4/202 and fail, and a hard-coded counter would make
#      it read 0 and fail;
#   4. P=0, SUNSHINE.dem: refused=0 and translucentRefused=0, so the rescue
#      has no candidate and the planner's own readings are unchanged.
#
# N2=10 (a forced-empty 338170880 budget) is not built by this script: it
# needs a temporary header edit and rebuild, and its evidence is the separate
# forced-empty run. The off switch above is the in-tree falsifier.
#
# NOT ASSERTED HERE: the ten tile sizes. The reference pose's positive and
# negative frames are byte-identical (all 18 PVS-visible triangles are occluded
# or fully transparent at that camera), so a pixel assertion there would pin a
# camera accident. Section 4 adds a camera that does see the glass and pins the
# rescue's pixel difference there, with hole-trace material attribution.
#
# WRITES: the readings go under ${SULTRY_ATLAS_OUT:-evidence/sultry-translucent-atlas}.
# A run in a clean tree will dirty tracked evidence unless the caller redirects
# this (SULTRY_ATLAS_OUT=/path/outside/the/repo); copy readings to a lane dir
# before committing. The scratch captures go under SULTRY_ATLAS_SCRATCH.
#
# Usage: bash sultry-translucent-atlas-check.sh
# Exit:  0 = every assertion held, 1 = any failed.
set -uo pipefail
# W1 (2026-11-23): every product launch in this script runs at 1280x720, the size
# its pixel pins were measured at. The product reads this when --window-size is
# absent (an explicit --window-size still wins); before W1 1280x720 was simply the
# default, and W1 moves the default to 1920x1080. tools/window-pin-check.sh checks it.
export TF2_NATIVE_WINDOW_SIZE="${TF2_NATIVE_WINDOW_SIZE:-1280x720}"
cd "$(dirname "$0")"

PRODUCT=native/build-nmake/tf2_demo_native.exe
TF="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
DEMO=${SULTRY_DEMO:-}
if [ -z "$DEMO" ]; then
  DEMO='E:/demo测试/cp_sultry_b8a/STV/f63a7a2040379d7d6a8414f76bbb81dc_matcha-20260703-1442-cp_sultry_b8a.dem'
fi
P0_DEMO=${SULTRY_P0_DEMO:-}
if [ -z "$P0_DEMO" ]; then
  P0_DEMO='E:/demo测试/cp_sunshine/STV/SUNSHINE.dem'
fi
PTS='1220,174;734,87;1231,434;474,560;394,287;139,104;758,360;1143,132;873,366;579,520;849,396;1221,92;902,385;1110,136;660,326;960,289;919,379'

if [ ! -f "$PRODUCT" ]; then
  echo "SULTRY-TRANSLUCENT-ATLAS=FAIL (missing $PRODUCT; build tf2_demo_native first)"
  exit 1
fi
if [ ! -f "$DEMO" ]; then
  echo "SULTRY-TRANSLUCENT-ATLAS=FAIL (missing demo: $DEMO)"
  exit 1
fi
if [ ! -f "$P0_DEMO" ]; then
  echo "SULTRY-TRANSLUCENT-ATLAS=FAIL (missing P=0 demo: $P0_DEMO)"
  exit 1
fi

RUN_TAG="$(date +%Y%m%d%H%M%S)-$$"
SCRATCH="${SULTRY_ATLAS_SCRATCH:-.scratch/sultry-translucent-atlas.$RUN_TAG}"
OUT="${SULTRY_ATLAS_OUT:-evidence/sultry-translucent-atlas}"
mkdir -p "$SCRATCH" "$OUT"

# D-22: never read or write the machine profile. Every gate seeds the renderer
# configuration from the tracked fixture and fails closed if the copy differs.
. "$(dirname "$0")/verify-iso.sh"
ISO_DIR="$(mktemp -d "${TMPDIR:-/tmp}/tf2-iso.XXXXXX")" || {
  echo "SULTRY-TRANSLUCENT-ATLAS=FAIL (cannot create isolated settings directory)"; exit 2; }
iso_chain_exit "$ISO_DIR"
iso_seed_gate_settings "$ISO_DIR" || {
  echo "SULTRY-TRANSLUCENT-ATLAS=FAIL (cannot seed the gate settings fixture)"; exit 2; }
: > "$ISO_DIR/ui.ini"

fail=0
assert_eq() {
  if [ "$2" = "$3" ]; then echo "  OK $1 = $2"; else echo "  FAIL $1: got '$2' want '$3'"; fail=1; fi
}
field() { # field <text> <key>
  printf '%s' "$1" | grep -oE "(^| )$2=[^ ]+" | head -1 | cut -d= -f2-
}
# iso_line <trace> -> the `gpu: world isolation` reading this gate asserts on.
# 2026-11-22: native/src/main.cpp emits three lines with that prefix (requested=,
# indexCount=, and the draw-time one with slot0Binds=/batchDraws=). `head -1` of
# the bare prefix picked the first of them, which carries none of the asserted
# keys, so eight assertions read ''. Select the line by the key it must carry.
iso_line() {
  grep -ao "gpu: world isolation .*slot0Binds=.*" "$1" | head -1
}

# capture <dir> <demo> [extra product flags...]
capture() {
  local dir="$1" demo="$2"; shift 2
  mkdir -p "$dir"
  TF2_NATIVE_TRACE="$(cygpath -m "$dir/points.trace")" timeout 420 "$PRODUCT" \
    --tf-root "$TF" --demo "$demo" \
    --audio-disabled --hidden-window --start-paused --capture-tick 0 --world-sky \
    --settings-file "$(cygpath -m "$ISO_DIR/settings.ini")" \
    --ui-settings-file "$(cygpath -m "$ISO_DIR/ui.ini")" \
    --debug-camera -600 -600 950 20 45 \
    --world-hole-trace "$PTS" \
    "$@" \
    --capture-frame "$(cygpath -m "$dir/points.bmp")" > "$dir/points.log" 2>&1
  echo $?
}

# capture_camera <dir> <demo> <eyeX eyeY eyeZ> <pitch> <yaw> <hole-points> [extra...]
# Same isolation and product flags as capture(), but a camera chosen to see the
# rescued glass. The reference camera cannot: its 18 PVS-visible triangles are
# occluded or fully transparent, which is why its on/off frames are identical.
capture_camera() {
  local dir="$1" demo="$2" eye="$3" pitch="$4" yaw="$5" hole="$6"; shift 6
  mkdir -p "$dir"
  TF2_NATIVE_TRACE="$(cygpath -m "$dir/points.trace")" timeout 420 "$PRODUCT" \
    --tf-root "$TF" --demo "$demo" \
    --audio-disabled --hidden-window --start-paused --capture-tick 0 --world-sky \
    --settings-file "$(cygpath -m "$ISO_DIR/settings.ini")" \
    --ui-settings-file "$(cygpath -m "$ISO_DIR/ui.ini")" \
    --debug-camera $eye $pitch $yaw \
    --world-hole-trace "$hole" \
    "$@" \
    --capture-frame "$(cygpath -m "$dir/points.bmp")" > "$dir/points.log" 2>&1
  echo $?
}

MATERIALS='WATERGATE/ILLUSION_SEA maps/cp_sultry_b8a/glass/glasswindow001a_-1409_-44_640 maps/cp_sultry_b8a/glass/glasswindow001a_1346_12_640 maps/cp_sultry_b8a/glass/glasswindow001a_-713_984_704 maps/cp_sultry_b8a/glass/glasswindow001a_649_-1016_704 maps/cp_sultry_b8a/glass/glasswindow001b_-1394_1562_736 maps/cp_sultry_b8a/glass/glasswindow001b_1331_-1594_736 CP_MOUNTAINLAB/MISC/BALLOON maps/cp_sultry_b8a/glass/glasswindow002a_-2017_-483_704 maps/cp_sultry_b8a/glass/glasswindow002a_1954_450_704'

echo
echo "=== 1/4 the positive frame: the rescue batch exists ==="
POS="$SCRATCH/pos"
RC_POS=$(capture "$POS" "$DEMO")
TRACE_POS="$POS/points.trace"
assert_eq "positive rc" "$RC_POS" "0"
PLAN_POS=$(grep -ao "gpu: world atlas-plan .*" "$TRACE_POS" | head -1)
ATLAS_POS=$(grep -ao "gpu: world atlas .*" "$TRACE_POS" | head -1)
BLEND_POS=$(grep -ao "gpu: world blend enabled=.*" "$TRACE_POS" | head -1)
ISO_POS=$(iso_line "$TRACE_POS")
ALPHA_POS=$(grep -ao "gpu: world alpha frame .*" "$TRACE_POS" | head -1)
echo "$PLAN_POS" > "$OUT/positive-atlas-plan.txt"
echo "$ATLAS_POS" > "$OUT/positive-atlas.txt"
echo "$BLEND_POS" > "$OUT/positive-blend.txt"
echo "$ISO_POS" > "$OUT/positive-isolation.txt"
echo "$ALPHA_POS" > "$OUT/positive-alpha.txt"
assert_eq "atlas-plan batches" "$(field "$PLAN_POS" batches)" "4"
assert_eq "atlas-plan blendBatches" "$(field "$PLAN_POS" blendBatches)" "1"
assert_eq "atlas-plan tiled" "$(field "$PLAN_POS" tiled)" "202"
assert_eq "atlas-plan refused" "$(field "$PLAN_POS" refused)" "48"
assert_eq "atlas-plan budget" "$(field "$PLAN_POS" budget)" "48"
assert_eq "atlas-plan translucentRefused" "$(field "$PLAN_POS" translucentRefused)" "0"
assert_eq "atlas-plan layerBytes" "$(field "$PLAN_POS" layerBytes)" "350852288"
assert_eq "atlas atlases" "$(field "$ATLAS_POS" atlases)" "4"
assert_eq "atlas totalBytes" "$(field "$ATLAS_POS" totalBytes)" "215583936"
assert_eq "atlas size" "$(field "$ATLAS_POS" size)" "4112"
assert_eq "atlas bytes" "$(field "$ATLAS_POS" bytes)" "67634176"
assert_eq "atlas state" "$(field "$ATLAS_POS" state)" "bound"
assert_eq "world blend accepted" "$(field "$BLEND_POS" accepted)" "202"
assert_eq "isolation slot0Binds" "$(field "$ISO_POS" slot0Binds)" "7"
assert_eq "isolation batchDraws" "$(field "$ISO_POS" batchDraws)" "7"
assert_eq "isolation texturedBatchDraws" "$(field "$ISO_POS" texturedBatchDraws)" "7"
assert_eq "isolation weaponDrawsWithWorldAtlas" "$(field "$ISO_POS" weaponDrawsWithWorldAtlas)" "0"
assert_eq "isolation worldDrawsWithWeaponTexture" "$(field "$ISO_POS" worldDrawsWithWeaponTexture)" "0"
assert_eq "alpha blendedTriangles" "$(field "$ALPHA_POS" blendedTriangles)" "1376"
assert_eq "alpha cutoutTriangles" "$(field "$ALPHA_POS" cutoutTriangles)" "32"
assert_eq "alpha blendedBatches" "$(field "$ALPHA_POS" blendedBatches)" "4"
assert_eq "alpha blendState" "$(field "$ALPHA_POS" blendState)" "1"
assert_eq "alpha depthWriteOff" "$(field "$ALPHA_POS" depthWriteOff)" "1"

for m in $MATERIALS; do
  tile=$(grep -a "gpu: world tile material=$m " "$TRACE_POS" | head -1)
  alpha=$(grep -a "gpu: world alpha material=$m " "$TRACE_POS" | grep 'stage=consumed' | head -1)
  assert_eq "tile $m batch" "$(field "$tile" batch)" "3"
  assert_eq "tile $m reason" "$(field "$tile" reason)" "tiled"
  assert_eq "alpha $m mode" "$(field "$alpha" mode)" "blended"
  assert_eq "alpha $m reason" "$(field "$alpha" reason)" '$translucent'
done

echo
echo "=== 2/4 the negative frame: the off switch still names all ten ==="
NEG="$SCRATCH/neg"
RC_NEG=$(capture "$NEG" "$DEMO" --world-atlas-no-translucent-rescue)
TRACE_NEG="$NEG/points.trace"
assert_eq "negative rc" "$RC_NEG" "0"
PLAN_NEG=$(grep -ao "gpu: world atlas-plan .*" "$TRACE_NEG" | head -1)
ATLAS_NEG=$(grep -ao "gpu: world atlas .*" "$TRACE_NEG" | head -1)
ISO_NEG=$(iso_line "$TRACE_NEG")
ALPHA_NEG=$(grep -ao "gpu: world alpha frame .*" "$TRACE_NEG" | head -1)
echo "$PLAN_NEG" > "$OUT/negative-atlas-plan.txt"
echo "$ATLAS_NEG" > "$OUT/negative-atlas.txt"
echo "$ISO_NEG" > "$OUT/negative-isolation.txt"
echo "$ALPHA_NEG" > "$OUT/negative-alpha.txt"
assert_eq "negative atlas-plan batches" "$(field "$PLAN_NEG" batches)" "3"
assert_eq "negative atlas-plan blendBatches" "$(field "$PLAN_NEG" blendBatches)" "1"
assert_eq "negative atlas-plan tiled" "$(field "$PLAN_NEG" tiled)" "192"
assert_eq "negative atlas-plan refused" "$(field "$PLAN_NEG" refused)" "58"
assert_eq "negative atlas-plan budget" "$(field "$PLAN_NEG" budget)" "58"
assert_eq "negative atlas-plan translucentRefused" "$(field "$PLAN_NEG" translucentRefused)" "10"
assert_eq "negative atlas-plan layerBytes" "$(field "$PLAN_NEG" layerBytes)" "338170880"
assert_eq "negative atlas atlases" "$(field "$ATLAS_NEG" atlases)" "3"
assert_eq "negative atlas totalBytes" "$(field "$ATLAS_NEG" totalBytes)" "202902528"
assert_eq "negative isolation slot0Binds" "$(field "$ISO_NEG" slot0Binds)" "6"
assert_eq "negative isolation batchDraws" "$(field "$ISO_NEG" batchDraws)" "6"
assert_eq "negative isolation texturedBatchDraws" "$(field "$ISO_NEG" texturedBatchDraws)" "6"
assert_eq "negative alpha blendedTriangles" "$(field "$ALPHA_NEG" blendedTriangles)" "1334"
assert_eq "negative alpha blendedBatches" "$(field "$ALPHA_NEG" blendedBatches)" "3"

for m in $MATERIALS; do
  tile=$(grep -a "gpu: world tile material=$m " "$TRACE_NEG" | head -1)
  alpha=$(grep -a "gpu: world alpha material=$m " "$TRACE_NEG" | grep 'stage=consumed' | head -1)
  assert_eq "negative tile $m batch" "$(field "$tile" batch)" "-1"
  assert_eq "negative tile $m reason" "$(field "$tile" reason)" "atlas-budget"
  assert_eq "negative alpha $m mode" "$(field "$alpha" mode)" "refused"
done

echo
echo "=== 3/4 P=0: a map with no budget refusal has nothing to rescue ==="
P0="$SCRATCH/p0"
RC_P0=$(capture "$P0" "$P0_DEMO")
TRACE_P0="$P0/points.trace"
assert_eq "P=0 rc" "$RC_P0" "0"
PLAN_P0=$(grep -ao "gpu: world atlas-plan .*" "$TRACE_P0" | head -1)
ATLAS_P0=$(grep -ao "gpu: world atlas .*" "$TRACE_P0" | head -1)
ISO_P0=$(iso_line "$TRACE_P0")
ALPHA_P0=$(grep -ao "gpu: world alpha frame .*" "$TRACE_P0" | head -1)
echo "$PLAN_P0" > "$OUT/p0-atlas-plan.txt"
echo "$ATLAS_P0" > "$OUT/p0-atlas.txt"
echo "$ISO_P0" > "$OUT/p0-isolation.txt"
echo "$ALPHA_P0" > "$OUT/p0-alpha.txt"
assert_eq "P=0 atlas-plan batches" "$(field "$PLAN_P0" batches)" "3"
assert_eq "P=0 atlas-plan tiled" "$(field "$PLAN_P0" tiled)" "165"
assert_eq "P=0 atlas-plan refused" "$(field "$PLAN_P0" refused)" "0"
assert_eq "P=0 atlas-plan budget" "$(field "$PLAN_P0" budget)" "0"
assert_eq "P=0 atlas-plan translucentRefused" "$(field "$PLAN_P0" translucentRefused)" "0"
assert_eq "P=0 atlas-plan layerBytes" "$(field "$PLAN_P0" layerBytes)" "314921632"
assert_eq "P=0 atlas atlases" "$(field "$ATLAS_P0" atlases)" "3"
assert_eq "P=0 atlas totalBytes" "$(field "$ATLAS_P0" totalBytes)" "179653280"
# P=0 is about the planner having no candidate; the draw count follows the
# frame and is already pinned by the Sultry positive/negative cases.
assert_eq "P=0 alpha blendedTriangles" "$(field "$ALPHA_P0" blendedTriangles)" "977"
assert_eq "P=0 alpha cutoutTriangles" "$(field "$ALPHA_P0" cutoutTriangles)" "133"
assert_eq "P=0 alpha blendedBatches" "$(field "$ALPHA_P0" blendedBatches)" "3"

echo
echo "=== 4/4 pixel attribution: a camera that sees the rescued glass ==="
# F1-5. The reference camera cannot distinguish the two paths, so this section
# moves the camera to the glass window named glasswindow001a_649_-1016_704 and
# compares the same frame with the rescue on and off. Five of the six hole-trace
# points land on that material; the sixth is an opaque BRICK/WALL028 control, so
# the changed region is attributed to a named material rather than to "the
# picture moved".
PIX_ON="$SCRATCH/pix-on"
PIX_OFF="$SCRATCH/pix-off"
PIX_EYE="949 -1016 784"
PIX_HOLE="1100,300;1150,200;1200,100;1250,400;1050,500;1230,50"
RC_PIX_ON=$(capture_camera "$PIX_ON" "$DEMO" "$PIX_EYE" 15 180 "$PIX_HOLE")
RC_PIX_OFF=$(capture_camera "$PIX_OFF" "$DEMO" "$PIX_EYE" 15 180 "$PIX_HOLE" --world-atlas-no-translucent-rescue)
TRACE_PIX_ON="$PIX_ON/points.trace"
TRACE_PIX_OFF="$PIX_OFF/points.trace"
assert_eq "pixel positive rc" "$RC_PIX_ON" "0"
assert_eq "pixel negative rc" "$RC_PIX_OFF" "0"
PLAN_PIX_ON=$(grep -ao "gpu: world atlas-plan .*" "$TRACE_PIX_ON" | head -1)
PLAN_PIX_OFF=$(grep -ao "gpu: world atlas-plan .*" "$TRACE_PIX_OFF" | head -1)
ISO_PIX_ON=$(iso_line "$TRACE_PIX_ON")
ISO_PIX_OFF=$(iso_line "$TRACE_PIX_OFF")
ALPHA_PIX_ON=$(grep -ao "gpu: world alpha frame .*" "$TRACE_PIX_ON" | head -1)
ALPHA_PIX_OFF=$(grep -ao "gpu: world alpha frame .*" "$TRACE_PIX_OFF" | head -1)
echo "$PLAN_PIX_ON" > "$OUT/pixel-atlas-plan-on.txt"
echo "$PLAN_PIX_OFF" > "$OUT/pixel-atlas-plan-off.txt"
assert_eq "pixel on atlas-plan batches" "$(field "$PLAN_PIX_ON" batches)" "4"
assert_eq "pixel on atlas-plan translucentRefused" "$(field "$PLAN_PIX_ON" translucentRefused)" "0"
assert_eq "pixel off atlas-plan batches" "$(field "$PLAN_PIX_OFF" batches)" "3"
assert_eq "pixel off atlas-plan translucentRefused" "$(field "$PLAN_PIX_OFF" translucentRefused)" "10"
assert_eq "pixel on batchDraws" "$(field "$ISO_PIX_ON" batchDraws)" "7"
assert_eq "pixel off batchDraws" "$(field "$ISO_PIX_OFF" batchDraws)" "6"
assert_eq "pixel on blendedTriangles" "$(field "$ALPHA_PIX_ON" blendedTriangles)" "1376"
assert_eq "pixel off blendedTriangles" "$(field "$ALPHA_PIX_OFF" blendedTriangles)" "1334"
PIX_DIFF=$(python bmp-diff-bbox.py "$PIX_ON/points.bmp" "$PIX_OFF/points.bmp" 2>/dev/null)
echo "$PIX_DIFF" > "$OUT/pixel-diff.txt"
assert_eq "pixels changed by the rescue" "$(field "$PIX_DIFF" differing_pixels)" "84965"
# The six points, by name. The five glass points are the attribution; the
# BRICK/WALL028 point is the control that the probe is not just echoing the
# material under the whole bbox.
for p in 1100,300 1150,200 1200,100 1250,400 1230,50; do
  line=$(grep -a "world hole-trace point=$p " "$TRACE_PIX_ON" | head -1)
  assert_eq "pixel hole $p material" "$(field "$line" material)" "maps/cp_sultry_b8a/glass/glasswindow001a_649_-1016_704"
done
line=$(grep -a "world hole-trace point=1050,500 " "$TRACE_PIX_ON" | head -1)
assert_eq "pixel hole 1050,500 control material" "$(field "$line" material)" "BRICK/WALL028"
assert_eq "pixel hole points run" "$(grep -ac 'world hole-trace point=' "$TRACE_PIX_ON")" "6"

echo
if [ "$fail" -eq 0 ]; then
  echo "SULTRY-TRANSLUCENT-ATLAS=PASS"
  exit 0
fi
echo "SULTRY-TRANSLUCENT-ATLAS=FAIL"
exit 1
