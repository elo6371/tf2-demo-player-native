#!/usr/bin/env bash
# world-near-clip-check.sh -- T29: the world pass must draw the space the camera
# stands in, not only what lies beyond a clip plane written in the wrong units.
#
# WHY THIS FILE EXISTS
# --------------------
# uploadWorldGeometry scales the map into a normalized box: one box unit is
# worldHorizontalScale_ MAP units (measured on pl_odyssey: 1.8/horizontalSpan =
# 8.182e-05, so one box unit is 12,222 map units). The world projection's near
# plane was the bare constant 0.05 in that box, which is 0.05 / 8.182e-05 =
# 611 map units. Everything nearer than 611 units was clipped away, and the
# tick-2000 reference frame showed only geometry beyond 1,494 map units:
#
#   * an independent CPU ray cast from the demo eye (all 57k BSP triangles, no
#     product code) hits a FRONT-facing surface at a median of 183 map units and
#     within 64 units in 906 of 4000 directions -- the camera is in a room;
#   * the nearest point of any triangle the frame actually drew is 1494 map
#     units from the eye (903 drawn faces, minimum point-to-triangle distance),
#     so the room was not in the picture;
#   * 183,571 px (21.3%) of the frame are drawn but black (luminance < 16), and
#     140,960 of those are back faces of far geometry whose lightmap rect is
#     zero in both the LDR (lump 8) and HDR (lump 53) pages.
#
# The fix states the plane in MAP units (4.0) and converts it through the same
# transform the vertices went through, then publishes the number it used.
#
# WHAT IT ASSERTS
#   1. the reading the matrix was built from: gpu: world clip nearMapUnits is
#      the map-unit near plane (in [3,5]), near == nearMapUnits * horizontalScale
#      (the same number in box units), far unchanged, state=published;
#   2. the pixel proof, from the SAME 28 named points the material gate traces:
#      at least PIN_NEAR_POINTS of them are now closer to the eye than the old
#      611-map-unit clip. Measured: pre-fix 0 of 28 (they were clipped), post-fix
#      18 of 28. This is the assertion that says "the room is in the picture";
#      it is a distance read off the product's own depth, not a brightness.
#   3. the four pinned regions re-measured; W4 is the control and must read
#      EXACTLY what it read before the fix (mean 42.9, std 55.55, nearBlack
#      0.6784): the clip moved the near half of the frame, it did not touch the
#      patch of sky between the slabs that W4 has always measured.
#      The frame's total dark population went 183,571 -> 208,630: it did NOT
#      fall, and the gate does not claim it did. The clip was showing the far
#      side of the map; it now shows the room, and the room's left wall is in
#      the map's own shadow. What changed is WHICH surfaces are dark, and the
#      reading is pinned as a stability range so a lightmap change moves it.
#   4. the control: every material, lightmap, sky, blend and visibility reading
#      the world pass printed before the fix is IDENTICAL -- the fix changed the
#      projection, and it must not have changed one material resolution, one
#      lightmap texel, one sky face or one pvs decision;
#   5. no trace line is missing (a run that does not print the reading fails).
#
# mutation: the near plane goes back to the box-unit constant 0.05. The clip
#   reading must leave [3,5], the near-point count must fall to 0 of 28 and W1's
#   mean must leave its pin -- so the mutation is caught by the product's depth
#   at named pixels and by the frame, not only by a number the gate could have
#   printed from a constant. The source is restored byte for byte and the
#   restored build must reproduce the pinned frame byte for byte.
#
# Usage: bash world-near-clip-check.sh [--mutation]
# Exit:  0 = every assertion held (or, with --mutation, the mutation went red
#            and the source came back byte-identical).
set -uo pipefail
cd "$(dirname "$0")"

PRODUCT=native/build-nmake/tf2_demo_native.exe
TF="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
DEMO="$TF/demos/autorecord_2026-07-02_13-26-46.dem"
PYTHON="${PYTHON:-python}"

RUN_TAG="$(date +%Y%m%d%H%M%S)-$$"
MODE="${1:-}"
SCRATCH=".scratch/world-near-clip.$RUN_TAG"
OUT="evidence/t29-world-visual"
mkdir -p "$SCRATCH" "$OUT"

fail=0
assert_eq() {
  if [ "$2" = "$3" ]; then echo "  OK $1 = $2"; else echo "  FAIL $1: got '$2' want '$3'"; fail=1; fi
}
assert_ne() {
  if [ "$2" != "$3" ]; then echo "  OK $1 = $2 (was $3)"; else echo "  FAIL $1: still '$2'"; fail=1; fi
}
assert_range() {
  if awk -v v="$2" -v lo="$3" -v hi="$4" 'BEGIN{exit !(v>=lo && v<=hi)}'; then
    echo "  OK $1 = $2 in [$3,$4]"
  else
    echo "  FAIL $1: got '$2' outside [$3,$4]"; fail=1
  fi
}
assert_max() {
  if awk -v v="$2" -v hi="$3" 'BEGIN{exit !(v<=hi)}'; then
    echo "  OK $1 = $2 <= $3"
  else
    echo "  FAIL $1: got '$2' above $3"; fail=1
  fi
}
ckfield() { # ckfield <text> <key>
  printf '%s' "$1" | grep -oE "(^| )$2=[^ ]+" | head -1 | cut -d= -f2-
}

# The pins. Measured on 2026-10-20 with this tree, hidden window, tick 2000;
# every one of them is a reading this task changed and the numbers are in the
# report. They are pins, not floors: a frame that drifts off them fails here.
PIN_NEAR_MAP_LO=3.0
PIN_NEAR_MAP_HI=5.0
# 18 of the 28 named points are inside the old clip after the fix; the pre-fix
# build had 0. 12 is a floor with room for a point landing on a very near
# triangle, not a widened pin: the mutation reads 0.
PIN_NEAR_POINTS_MIN=12
# The frame's dark population, pinned as a range (see the header: it did not
# fall, and this task does not claim it did). Pre-fix 183,571, post-fix 208,630.
PIN_DARK_LO=195000
PIN_DARK_HI=225000
# Region pins, re-measured after the fix (the frame-region-stats tool reads BMP
# rows in file order, so its y is measured from the frame's file row 0; the
# numbers are the tool's own coordinates and are comparable with every earlier
# pin in visual-baseline-check.sh).
PIN_W1=18.8
PIN_W1_STD=21.9
PIN_W2=57.0
PIN_W2_STD=24.2
PIN_W3=133.9
PIN_W3_STD=31.0
# The control region: unchanged by the fix, asserted exactly.
PIN_W4=42.9
PIN_W4_STD=55.55
PIN_W4_BLACK=0.6784
PIN_W2_NEARBG=0.20

capture() { # capture <bmp> <trace> [extra flags...]
  local bmp="$1" trace="$2"; shift 2
  local traceWin bmpWin want=2000 target=2000 attempt rc tick tickDisplay
  traceWin="$(cygpath -m "$PWD/$trace")"
  bmpWin="$(cygpath -m "$PWD/$bmp")"
  # The draw whose tick is first >= target writes the file, so the last
  # `draw: begin tick=` is the captured tick. The gain-1 target controller moves
  # the target by the observed error (T27): under load the draw cadence and the
  # tick cadence disagree, and a fixed retry can never hit.
  for attempt in 1 2 3 4 5 6; do
    rm -f "$bmp"
    TF2_NATIVE_TRACE="$traceWin" timeout -k 5 150 "$PRODUCT" --tf-root "$TF" \
      --demo "$DEMO" --capture-tick "$target" --capture-frame "$bmpWin" \
      --hidden-window --audio-disabled \
      --viewmodel models/weapons/v_models/v_scattergun_scout.mdl --viewmodel-render companion \
      --world-face-trace \
      "$@" > "$SCRATCH/capture.log" 2>&1
    rc=$?
    tick=$(grep -a "draw: begin tick=" "$trace" 2>/dev/null | tr -d '\r' | tail -1 \
      | sed -n 's/.*tick=\([-0-9]*\).*/\1/p')
    if [ "$rc" = 0 ] && [ "$tick" = "$want" ] && [ -s "$bmp" ]; then
      [ "$attempt" -gt 1 ] && echo "  (capture pinned attempt $attempt: target $target drew $tick)"
      return 0
    fi
    tickDisplay="$tick"; [ -z "$tickDisplay" ] && tickDisplay=none
    echo "  (capture attempt $attempt: rc=$rc target=$target tick=$tickDisplay)"
    case "$tick" in
      ''|*[!0-9]*) ;;
      *) target=$((target + want - tick)); [ "$target" -lt 0 ] && target=0 ;;
    esac
  done
  return 1
}

rfield() { # rfield <stats-file> <region-key> <field>
  grep "region $2" "$1" | head -1 | grep -oE "$3=[-0-9.]+" | head -1 | cut -d= -f2
}

# ---------------------------------------------------------------------------
# 0. build the product from this tree
# ---------------------------------------------------------------------------
echo "== 0. build =="
bash build-target.sh tf2_demo_native > "$SCRATCH/build.txt" 2>&1
if [ $? -ne 0 ]; then echo "  FAIL build (see $SCRATCH/build.txt)"; tail -10 "$SCRATCH/build.txt"; exit 1; fi
echo "  OK build"
EXE_HASH=$(sha256sum "$PRODUCT" | cut -d' ' -f1)
HEAD_SHA=$(git rev-parse HEAD)
SOURCE_HASH=$(git hash-object native/src/native_renderer.cpp native/include/native_renderer.h native/src/main.cpp | tr '\n' ' ')
echo "  head=$HEAD_SHA exe=$EXE_HASH"
{
  echo "head=$HEAD_SHA"
  echo "exe_sha256=$EXE_HASH"
  echo "source_blob_hashes=$SOURCE_HASH"
} > "$OUT/identity.txt"

# ---------------------------------------------------------------------------
# 1. the reading the projection was built from
# ---------------------------------------------------------------------------
echo "== 1. clip reading =="
REF_BMP="$SCRATCH/ref.bmp"; REF_TRACE="$SCRATCH/ref.trace"
if ! capture "$REF_BMP" "$REF_TRACE"; then echo "  FAIL the reference capture did not land on tick 2000"; exit 1; fi
CLIP=$(grep -a "gpu: world clip " "$REF_TRACE" | head -1 | tr -d '\r')
[ -n "$CLIP" ] || { echo "  FAIL no gpu: world clip line"; exit 1; }
echo "  $CLIP"
NEAR_MAP=$(ckfield "$CLIP" nearMapUnits)
NEAR_BOX=$(ckfield "$CLIP" near)
FAR_BOX=$(ckfield "$CLIP" far)
SCALE=$(ckfield "$CLIP" horizontalScale)
assert_range "near plane in map units" "$NEAR_MAP" "$PIN_NEAR_MAP_LO" "$PIN_NEAR_MAP_HI"
# The two numbers must agree: the box-unit near plane the matrix took, times the
# transform, is the map-unit plane the line prints. A gate that pinned only the
# map-unit reading could pass on a matrix built from something else.
if awk -v a="$NEAR_BOX" -v b="$NEAR_MAP" -v s="$SCALE" 'BEGIN{exit !(b>0 && s>0 && (a/b-s < 1e-6) && (s-a/b < 1e-6))}'; then
  echo "  OK near box units = nearMapUnits * horizontalScale ($NEAR_BOX = $NEAR_MAP * $SCALE)"
else
  echo "  FAIL near box units disagree with the published transform: near=$NEAR_BOX nearMapUnits=$NEAR_MAP scale=$SCALE"; fail=1
fi
assert_eq "far plane unchanged" "$FAR_BOX" "100.000000"
assert_eq "transform published" "$(ckfield "$CLIP" state)" "published"

# ---------------------------------------------------------------------------
# 2. the same camera, same tick: pixels
# ---------------------------------------------------------------------------
echo "== 2. pixel evidence (tick 2000, hidden window) =="
"$PYTHON" frame-region-stats.py "$REF_BMP" \
  0,0,420,200 0,560,420,121 1100,100,164,200 600,0,64,60 > "$SCRATCH/regions.txt" 2>&1
cat "$SCRATCH/regions.txt"
cp "$SCRATCH/regions.txt" "$OUT/regions-after.txt"
# The trace itself is 3.5 MB of per-frame lines and the claim is about pixels;
# what is kept is the excerpt the gate reads plus the hash of the whole file, so
# the raw trace stays on disk and can still be checked against these numbers.
{
  grep -a "gpu: world clip " "$REF_TRACE" | head -1
  grep -a "gpu: world material-fetch " "$REF_TRACE" | head -1
  grep -a "gpu: world lightmap " "$REF_TRACE" | head -1
  grep -a "gpu: world sky-upload " "$REF_TRACE" | head -1
  grep -a "gpu: world blend enabled=" "$REF_TRACE" | head -1
  grep -a "gpu: world vis " "$REF_TRACE" | head -1
  grep -a "gpu: world face-trace-summary" "$REF_TRACE" | head -1
} | tr -d '\r' > "$OUT/readings.txt"
sha256sum "$REF_TRACE" "$REF_BMP" > "$OUT/frame-hashes.txt"
cp "$REF_BMP" "$OUT/ref-after.bmp"
"$PYTHON" bmp-to-png.py "$REF_BMP" "$OUT/ref-after.png" > /dev/null 2>&1 || true
DARK=$("$PYTHON" - "$REF_BMP" <<'PYEOF_INNER'
import struct, sys
d = open(sys.argv[1], "rb").read()
off = struct.unpack_from("<I", d, 10)[0]
w, h = struct.unpack_from("<ii", d, 18)
flip = h < 0
h = abs(h)
stride = (w * 3 + 3) // 4 * 4
dark = 0
for y in range(h):
    src = y if flip else h - 1 - y
    base = off + src * stride
    for x in range(w):
        b, g, r = d[base+x*3], d[base+x*3+1], d[base+x*3+2]
        if 0.299*r + 0.587*g + 0.114*b < 16.0:
            dark += 1
print(dark)
PYEOF_INNER
)
echo "  dark population lum<16: $DARK (pre-fix frame: 183571)"
assert_range "dark pixels (stability pin)" "$DARK" "$PIN_DARK_LO" "$PIN_DARK_HI"
# The pixel proof: how many of the 28 named points are now closer than the 611
# map units the old clip removed. trace-near-points.py converts the depth the
# frame trace prints at each named pixel back through the near plane the draw
# published. Pre-fix trace: 0 of 28 (none was allowed to be nearer); post-fix:
# 18 of 28.
DIST=$("$PYTHON" trace-near-points.py "$REF_TRACE" "$NEAR_BOX" "$SCALE" 611.0)
NEAR_POINTS=$(printf '%s' "$DIST" | cut -d' ' -f1)
TRACED=$(printf '%s' "$DIST" | cut -d' ' -f2)
MIN_DIST=$(printf '%s' "$DIST" | cut -d' ' -f3)
echo "  named points closer than the old 611-unit clip: $NEAR_POINTS of $TRACED (nearest $MIN_DIST map units; pre-fix 0 of 28)"
if [ -z "$NEAR_POINTS" ]; then NEAR_POINTS=0; fi
if [ "$NEAR_POINTS" -ge "$PIN_NEAR_POINTS_MIN" ]; then
  echo "  OK near-point population = $NEAR_POINTS >= $PIN_NEAR_POINTS_MIN"
else
  echo "  FAIL near-point population: got '$NEAR_POINTS' want >= $PIN_NEAR_POINTS_MIN"; fail=1
fi
for spec in "x=0,y=0 $PIN_W1 $PIN_W1_STD W1" "x=0,y=560 $PIN_W2 $PIN_W2_STD W2" \
            "x=1100,y=100 $PIN_W3 $PIN_W3_STD W3"; do
  set -- $spec
  key="$4"
  mean=$(rfield "$SCRATCH/regions.txt" "$1" meanLum)
  std=$(rfield "$SCRATCH/regions.txt" "$1" stdLum)
  if [ -z "$mean" ]; then echo "  FAIL region $key produced no reading"; fail=1; continue; fi
  assert_range "region $key meanLum" "$mean" "$(awk -v v="$2" 'BEGIN{printf "%.1f", v-2.0}')" "$(awk -v v="$2" 'BEGIN{printf "%.1f", v+2.0}')"
  assert_range "region $key stdLum" "$std" "$(awk -v v="$3" 'BEGIN{printf "%.1f", v-3.0}')" "$(awk -v v="$3" 'BEGIN{printf "%.1f", v+3.0}')"
done
# W4 is the control: same pixels before and after, asserted exactly.
assert_eq "control W4 meanLum (exact)" "$(rfield "$SCRATCH/regions.txt" x=600,y=0 meanLum)" "$PIN_W4"
assert_eq "control W4 stdLum (exact)" "$(rfield "$SCRATCH/regions.txt" x=600,y=0 stdLum)" "$PIN_W4_STD"
assert_eq "control W4 nearBlack (exact)" "$(rfield "$SCRATCH/regions.txt" x=600,y=0 nearBlack)" "$PIN_W4_BLACK"
NEARBG=$(rfield "$SCRATCH/regions.txt" x=0,y=560 nearBackground)
assert_max "region W2 nearBackground" "$NEARBG" "$PIN_W2_NEARBG"

# ---------------------------------------------------------------------------
# 3. the control: nothing about material/lightmap/sky/pvs may have moved
# ---------------------------------------------------------------------------
echo "== 3. control: the readings the projection fix must not have touched =="
MAT=$(grep -a "gpu: world material-fetch " "$REF_TRACE" | head -1 | tr -d '\r')
LIGHT=$(grep -a "gpu: world lightmap " "$REF_TRACE" | head -1 | tr -d '\r')
SKY=$(grep -a "gpu: world sky-upload " "$REF_TRACE" | head -1 | tr -d '\r')
BLEND=$(grep -a "gpu: world blend enabled=" "$REF_TRACE" | head -1 | tr -d '\r')
VIS=$(grep -a "gpu: world vis " "$REF_TRACE" | head -1 | tr -d '\r')
for pair in "material-fetch distinct 108" "material-fetch tiled 85" \
            "material-fetch fromPakfile 85" "material-fetch vmtMissing 3" \
            "material-fetch vtfMissing 0" "material-fetch vtfTooLarge 70" \
            "lightmap status active" "lightmap vertices 297936" \
            "lightmap lightmapped 296490" "lightmap lightmapEnabled 1" \
            "sky-upload state active" "sky-upload faces 6" "sky-upload vertices 36" \
            "blend tiles 10" "blend weightedVertices 85920" \
            "vis drawnTriangles 76344" "vis hiddenTriangles 22968" "vis visibleFromCamera 1692"; do
  set -- $pair
  case "$1" in
    material-fetch) line="$MAT" ;;
    lightmap) line="$LIGHT" ;;
    sky-upload) line="$SKY" ;;
    blend) line="$BLEND" ;;
    vis) line="$VIS" ;;
  esac
  assert_eq "control $1 $2" "$(ckfield "$line" "$2")" "$3"
done

if [ "$MODE" != "--mutation" ]; then
  echo "== verdict =="
  if [ "$fail" = 0 ]; then
    echo "PASS: the world pass draws the space the camera stands in (near clip in map units, pixels re-pinned, material/sky/pvs readings unchanged)."
  else
    echo "FAIL: $fail assertion groups did not hold."
  fi
  exit "$fail"
fi

# ---------------------------------------------------------------------------
# mutation: the near plane goes back to the box-unit constant
# ---------------------------------------------------------------------------
echo "== mutation mode =="
mfail=0
SRC_REN="native/src/native_renderer.cpp"
SAVE_REN="$SCRATCH/native_renderer.cpp.save"
cp "$SRC_REN" "$SAVE_REN"
echo "-- M1: the near plane is the box-unit constant 0.05 again --"
"$PYTHON" - "$SRC_REN" <<'PYEOF_INNER'
import sys
path = sys.argv[1]
src = open(path, encoding='utf-8').read()
old = """    const float worldNearPlane = (std::isfinite(worldHorizontalScale_) && worldHorizontalScale_ > 0.0f)
      ? std::max(worldHorizontalScale_ * kWorldNearMapUnits, 1.0e-6f) : 0.05f;"""
new = """    const float worldNearPlane = (std::isfinite(worldHorizontalScale_) && worldHorizontalScale_ > 0.0f)
      ? 0.05f : 0.05f;"""
assert old in src, "M1 patch site not found"
open(path, 'w', encoding='utf-8', newline='').write(src.replace(old, new))
PYEOF_INNER
if [ $? != 0 ]; then echo "  FAIL M1 could not patch the source"; fail=1; fi
bash build-target.sh tf2_demo_native > "$SCRATCH/m1.build" 2>&1
if [ $? -ne 0 ]; then echo "  FAIL M1 build"; fail=1; fi
M1_BMP="$SCRATCH/m1.bmp"; M1_TRACE="$SCRATCH/m1.trace"
capture "$M1_BMP" "$M1_TRACE" || { echo "  FAIL M1 capture"; fail=1; }
M1_CLIP=$(grep -a "gpu: world clip " "$M1_TRACE" | head -1 | tr -d '\r')
M1_NEAR=$(ckfield "$M1_CLIP" nearMapUnits)
if awk -v v="$M1_NEAR" -v lo="$PIN_NEAR_MAP_LO" -v hi="$PIN_NEAR_MAP_HI" 'BEGIN{exit !(v>=lo && v<=hi)}'; then
  echo "  NOT-RED M1: the mutated near plane still reads nearMapUnits=$M1_NEAR inside [$PIN_NEAR_MAP_LO,$PIN_NEAR_MAP_HI]"
  mfail=1
else
  echo "  RED M1 as required: nearMapUnits = $M1_NEAR (the box-unit constant)"
fi
# W1 is the region that moved furthest (18.8 -> 75.3 when the clip is back);
# W2 barely moves (57.0 -> 56.5), so it is not the region this mutation is
# judged on. The pixel assertion is the pair "W1 left its pin" AND "no named
# point is nearer than the clip", so a coincidental mean cannot pass it.
"$PYTHON" frame-region-stats.py "$M1_BMP" 0,0,420,200 0,560,420,121 > "$SCRATCH/m1-region.txt" 2>&1
M1_W1=$(rfield "$SCRATCH/m1-region.txt" x=0,y=0 meanLum)
if awk -v v="$M1_W1" -v pin="$PIN_W1" 'BEGIN{exit !(v < pin - 2.0 || v > pin + 2.0)}'; then
  echo "  RED M1 as required: W1 meanLum = $M1_W1 (pinned $PIN_W1)"
else
  echo "  NOT-RED M1: W1 meanLum = $M1_W1 still inside the pin around $PIN_W1"; mfail=1
fi
# The same depth instrument as the forward run: the mutated build clips at 611
# map units again, so none of the named points may be nearer than that.
M1_NEAR_BOX=$(ckfield "$M1_CLIP" near)
M1_SCALE=$(ckfield "$M1_CLIP" horizontalScale)
M1_DIST=$("$PYTHON" trace-near-points.py "$M1_TRACE" "$M1_NEAR_BOX" "$M1_SCALE" 611.0)
M1_POINTS=$(printf '%s' "$M1_DIST" | cut -d' ' -f1)
if [ "$M1_POINTS" -eq 0 ]; then
  echo "  RED M1 as required: named points nearer than 611 map units = 0 (the clip is back)"
else
  echo "  NOT-RED M1: $M1_POINTS named points still read nearer than the clip"; mfail=1
fi
cp "$SAVE_REN" "$SRC_REN"
if cmp -s "$SAVE_REN" "$SRC_REN"; then echo "  RESTORE=OK (M1 source byte-identical)"; else
  echo "  RESTORE=FAIL (M1: the source did not come back byte-identical)"; fail=1; fi
bash build-target.sh tf2_demo_native > "$SCRATCH/m1-restore.build" 2>&1
if [ $? -ne 0 ]; then echo "  FAIL the restored build"; fail=1; fi
R_BMP="$SCRATCH/restored.bmp"; R_TRACE="$SCRATCH/restored.trace"
capture "$R_BMP" "$R_TRACE" || { echo "  FAIL the restored capture"; fail=1; }
if [ -s "$R_BMP" ] && cmp -s "$REF_BMP" "$R_BMP"; then
  echo "  PASS the restored build reproduces the pinned reference frame byte for byte"
else
  echo "  FAIL the restored build did not reproduce the pinned frame"; fail=1
fi

echo "== verdict =="
if [ "$fail" = 0 ] && [ "$mfail" = 0 ]; then
  echo "PASS: the forward reading held, the near-clip mutation went red on both the reading and the pixel, and the restored build reproduces the frame."
  exit 0
fi
echo "FAIL: forward-fail=$fail mutation-not-red=$mfail"
exit 1
