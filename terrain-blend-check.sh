#!/usr/bin/env bash
# terrain-blend-check.sh -- assert that the reference map's terrain is drawn by
# its own WorldVertexTransition material, that the per-vertex weight from the
# displacement reaches the pixel shader, and that every one of those claims can
# go red.
#
# WHY THIS FILE EXISTS
# --------------------
# Before this round the terrain was not blended at all. `main.cpp` folded the
# second layer into the first on the CPU, using the *first texture's alpha* as
# the mask; on the reference map both layers are opaque DXT1, so that mask was 1
# everywhere and every displacement drew the second layer flat, ignoring the
# weight the map author painted. The reading that hid it was a count of
# materials that had resolved -- and every material did resolve. Resolving is
# not drawing, and a count of tiles is not a picture.
#
# WHAT IS ASSERTED, IN ORDER
#   1. the probe's reading of the map's own blend resources: which shader, which
#      two layers, which modulate, which of the three stores each came from, the
#      source and destination dimensions of every downscale, the weight
#      distribution the parser extracted from `CDispVert::m_flAlpha`, and the
#      (position, weight) pairing T11's winding swap must preserve
#   2. the probe is deterministic: two runs over the same BSP agree byte for byte
#   3. the refusal paths, executed: eight malformed fixtures and two controls,
#      each failure carrying the reason the product itself would print
#   4. the product's own reading of the same map: the atlas is bound (not
#      silently refused), the blend tiles are in it, and the weights on the
#      uploaded vertices are the map's weights and not a constant
#   4b. the atlas coverage: all three blend materials fit ahead of the cap, and
#      the texturable materials the cap still drops are counted, named and
#      asked whether the demo camera can see them
#   5. the pixels: terrain on != terrain off, terrain on == terrain repeat, and
#      -- the part counters cannot give -- the differing pixels lie inside the
#      terrain region. The region is measured, not assumed: one frame contains
#      the terrain and nothing else, one contains the background and nothing
#      else, and the region is their difference.
#   6. the mutations: four defects, each of which must turn the gate red, each
#      restored and proved byte-identical before the next one.
#
# BUILD FIRST
#   bash build-target.sh terrain_blend_probe tf2_demo_native
#
# EVERY PER-RUN FILE CARRIES A RUN TAG ($RUN_TAG). `rm -f` exits 0 on this host
# even when the delete guard refuses it, so "clear then read" is not a freshness
# guarantee and a reading taken off another run's bytes looks exactly like a
# reading taken off this one's.
#
# Usage: bash terrain-blend-check.sh [--mutation]
# Exit:  0 = every assertion held (or, with --mutation, went red).
set -uo pipefail
cd "$(dirname "$0")"

PROBE=native/build-nmake/terrain_blend_probe.exe
PRODUCT=native/build-nmake/tf2_demo_native.exe
TF="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
MAP="cp_snakewater_final1"
DEMO="D:/TF2_Demo_Player/testdata/demos/bb841c6d379ff7c40d0c8baf99f59d8d_matcha-20260927-1347-cp_snakewater_final1.dem"
# The material the reference map's displacements actually carry. Measured from
# the BSP's face list, not guessed from the VMT's name.
BLEND_MATERIAL="NATURE/BLENDGROUNDTOGRASS008"
# The device every run is pinned to. Not the default: a run that opens the
# system device makes noise on the wrong output.
AUDIO_DEVICE=6
# The paused tick the frames are taken at. Paused is what makes the repeat frame
# byte-identical rather than "the same scene a moment later".
TICK=0
SCRATCH="${TERRAIN_BLEND_SCRATCH:-.scratch/terrain-blend}"
OUT="${TERRAIN_BLEND_OUT:-evidence/terrain-blend}"
mkdir -p "$OUT" "$SCRATCH"

RUN_TAG="$(date +%Y%m%d%H%M%S)-$$"
MUTATION=0
[ "${1:-}" = "--mutation" ] && MUTATION=1

fail=0
for path in "$PROBE" "$PRODUCT" "$TF/maps/$MAP.bsp" "$DEMO"; do
  [ -e "$path" ] || { echo "FATAL: missing input: $path"; exit 1; }
done
command -v timeout >/dev/null 2>&1 || { echo "FATAL: missing tool: timeout"; exit 1; }
command -v cygpath >/dev/null 2>&1 || { echo "FATAL: missing tool: cygpath"; exit 1; }
command -v python >/dev/null 2>&1 || { echo "FATAL: missing tool: python"; exit 1; }

assert_eq() { # assert_eq <label> <actual> <expected>
  if [ "${2:-<none>}" = "$3" ]; then
    echo "  OK   $1 = $3"
  else
    echo "  FAIL $1 = ${2:-<none>}, expected $3"; fail=1
  fi
}
assert_ge() { # assert_ge <label> <actual> <floor>
  if [ -n "${2:-}" ] && [ "$2" -ge "$3" ] 2>/dev/null; then
    echo "  OK   $1 = $2 (>= $3)"
  else
    echo "  FAIL $1 = ${2:-<none>}, expected >= $3"; fail=1
  fi
}
assert_le() { # assert_le <label> <actual> <ceiling>
  if [ -n "${2:-}" ] && [ "$2" -le "$3" ] 2>/dev/null; then
    echo "  OK   $1 = $2 (<= $3)"
  else
    echo "  FAIL $1 = ${2:-<none>}, expected <= $3"; fail=1
  fi
}
# kv <lines> <key> -> the value of a probe reading. The probe emits one JSON
# object per line -- {"reading":"key","value":"value"} -- because a reading and
# its name have to travel together: a bare number in a column cannot be matched
# to a claim when the columns move.
kv() { printf '%s\n' "$1" | tr -d '\r' | sed -n "s|.*\"reading\":\"$2\",\"value\":\"\([^\"]*\)\".*|\1|p" | head -1; }
# rval <line> <key> -> the value of "key=value" in a space-separated product line
rval() { printf '%s\n' "$1" | tr -d '\r' | tr ' ' '\n' | sed -n "s/^$2=//p" | head -1; }
# rk <lines> <key> -> the value of a bare "key=value" line, which is what the
# region comparison emits. Kept separate from `kv` because the probe's lines are
# JSON and the region's are not, and one parser for both would silently read
# nothing from one of them -- which is exactly what happened the first time.
rk() { printf '%s\n' "$1" | tr -d '\r' | sed -n "s/^$2=//p" | head -1; }
# num <value> -> the integer part, so "0.000" can be compared as a number
num() { printf '%s' "${1:-}" | sed -n 's/^\(-\{0,1\}[0-9][0-9]*\).*/\1/p'; }

echo "=== 1/6 the blend resources, read off the map rather than off the VMTs ==="
# The probe walks the BSP's own displacement faces, takes the materials they
# name, and resolves each VMT's fields through the same loose -> VPK -> pakfile
# order the product uses. `--expect-blend` makes it fail loudly if the material
# it was pointed at is not the shader this round claims to implement.
REAL=$("$PROBE" --map "$TF/maps/$MAP.bsp" --tf-root "$TF" --expect-blend "$BLEND_MATERIAL" 2>&1)
REAL_RC=$?
echo "$REAL" > "$OUT/probe-$MAP.txt"
assert_eq "probe rc" "$REAL_RC" "0"
assert_eq "map" "$(kv "$REAL" map.name)" "$MAP"
# Truncation is reported rather than hidden: the product caps the triangle list,
# so the census below is a census of what the product can draw.
assert_eq "map truncated" "$(kv "$REAL" map.truncated)" "1"
assert_ge "displacement faces" "$(kv "$REAL" map.displacementFaces)" "1000"
assert_ge "distinct displacement materials" "$(kv "$REAL" disp.materials)" "20"
# The material itself: the shader is the one this round implements, the second
# layer exists, and the modulate exists. Three separate readings, because a
# material can name a second layer and a modulate and still be parsed as
# LightmappedGeneric by a broken reader.
assert_eq "shader" "$(kv "$REAL" "disp.material.$BLEND_MATERIAL.shader")" "WorldVertexTransition"
assert_eq "expected shader" "$(kv "$REAL" expect.shader)" "WorldVertexTransition"
assert_eq "first layer" "$(kv "$REAL" expect.baseTexture)" "Nature/dirtground010"
assert_eq "second layer" "$(kv "$REAL" expect.baseTexture2)" "Nature/grass_08"
assert_eq "modulate" "$(kv "$REAL" expect.blendModulate)" "Nature/grass_blendmask"
# The store each resource came from. All three are in the VPK on this map; the
# pakfile store is proved separately by the census below, which is why the two
# readings are both here: "this material's layers are in the VPK" and "the
# third store is reachable at all" are different claims.
assert_eq "store" "$(kv "$REAL" expect.store)" "vpk"
assert_eq "layer stores" "$(kv "$REAL" "blend.$BLEND_MATERIAL.layers")" "vpk/vpk/vpk"
# The downscale, as source -> destination dimensions. The task asks for this to
# be recorded, and it is recorded per resource rather than as a total.
assert_eq "layer dimensions" "$(kv "$REAL" "blend.$BLEND_MATERIAL.dims")" \
  "1024x1024/1024x1024/1024x1024"
assert_eq "declared layers" "$(kv "$REAL" blend.declared)" "3"
assert_eq "resolved layers" "$(kv "$REAL" blend.resolved)" "3"
assert_eq "declared modulaters" "$(kv "$REAL" blend.modulateDeclared)" "2"
assert_eq "resolved modulaters" "$(kv "$REAL" blend.modulateResolved)" "2"
# The third store. `_wvt_patch` materials exist only inside the BSP's own
# pakfile lump, so a reader that stops at the VPK loses 32 materials -- and
# those 32 are exactly the brush faces the compiler copied out of the blend
# materials, i.e. the terrain's own second half.
assert_ge "materials from the pakfile" "$(kv "$REAL" materials.pakfileOnly)" "1"
assert_ge "materials resolved" "$(kv "$REAL" materials.resolved)" "100"
# Where the blend terrain is. The gate does not need it to point a camera -- the
# demo's own view is used -- but it is the reading that says the 11300 blend
# triangles are a real part of the map and not a handful of faces, and it is
# what a reader needs to reproduce a frame by hand.
assert_ge "blend triangles" "$(kv "$REAL" disp.blendTriangles)" "1000"
BLEND_BOUNDS=$(kv "$REAL" disp.blendBounds)
case "$BLEND_BOUNDS" in
  [0-9-]*) echo "  OK   blend bounds = $BLEND_BOUNDS" ;;
  *) echo "  FAIL blend bounds = ${BLEND_BOUNDS:-<none>}"; fail=1 ;;
esac
echo "  ..   blend centre = $(kv "$REAL" disp.blendCentre)"
# The weights. `sentinelVertices=0` says every displacement vertex carried a
# readable alpha; `distinctWeights` and `weightRange` say the values are the
# map's painted gradient rather than a constant, which is the difference between
# a blend and a second layer drawn flat.
assert_ge "weighted vertices" "$(kv "$REAL" disp.weightedVertices)" "100000"
assert_eq "sentinel vertices" "$(kv "$REAL" disp.sentinelVertices)" "0"
assert_ge "distinct weights" "$(kv "$REAL" disp.distinctWeights)" "2"
assert_eq "weight range" "$(kv "$REAL" disp.weightRange)" "0.000..1.000"
# T12 post-merge: the weight rides on a vertex, and T11 reverses b and c to
# orient a displacement triangle. A swap that moves the positions without the
# weights draws a blend whose mask is mirrored inside every quad -- a picture
# that still blends, so no count above moves. `detached` is the only reading
# that sees it.
assert_ge "alpha pairings checked" "$(kv "$REAL" disp.alphaPairingChecked)" "100000"
assert_eq "alpha pairings detached" "$(kv "$REAL" disp.alphaPairingDetached)" "0"
# Kept for the mutation below: the count must not move when only the pairing
# does, or the assertion would be satisfied by a build that stopped checking.
FORWARD_ALPHA_PAIRS=$(kv "$REAL" disp.alphaPairingChecked)
# T12 step 8: which of the three unimplemented Source fields the terrain's own
# materials name. Measured rather than assumed, because the decision to leave
# them out has to point at the references that would have needed them.
assert_ge "materials declaring \$bumpmap2" "$(kv "$REAL" blend.bumpmap2Declared)" "1"
assert_ge "materials declaring %detailtype" "$(kv "$REAL" blend.detailtypeDeclared)" "1"
assert_ge "materials declaring \$ssbump" "$(kv "$REAL" blend.ssbumpDeclared)" "1"

echo
echo "=== 2/6 the probe is deterministic ==="
REAL2=$("$PROBE" --map "$TF/maps/$MAP.bsp" --tf-root "$TF" --expect-blend "$BLEND_MATERIAL" 2>&1)
echo "$REAL2" > "$SCRATCH/probe-repeat.$RUN_TAG.txt"
if cmp -s "$OUT/probe-$MAP.txt" "$SCRATCH/probe-repeat.$RUN_TAG.txt"; then
  echo "  OK   two probe runs agree byte for byte"
else
  echo "  FAIL two probe runs disagree"; diff "$OUT/probe-$MAP.txt" "$SCRATCH/probe-repeat.$RUN_TAG.txt" | head -10; fail=1
fi

# ---------------------------------------------------------------------------
# The six frames. One helper, because every assertion below is a comparison
# between two of them and the only thing that may differ is the switch.
# ---------------------------------------------------------------------------
run_product() { # run_product <name> [extra args...]
  local name="$1"; shift
  local trace="$SCRATCH/$name.$RUN_TAG.trace"
  local bmp="$SCRATCH/$name.$RUN_TAG.bmp"
  local csv="$SCRATCH/$name.$RUN_TAG.csv"
  local log="$SCRATCH/$name.$RUN_TAG.log"
  rm -f "$trace" "$bmp" "$csv" "$log"
  # cygpath -m gives the Windows form without the doubled backslashes the MSYS
  # layer rewrites; the trace path is read with GetEnvironmentVariableW.
  TF2_NATIVE_TRACE="$(cygpath -m "$PWD/$trace")" timeout 400 "$PRODUCT" \
    --tf-root "$TF" --audio-device "$AUDIO_DEVICE" --audio-disabled --hidden-window \
    --demo "$DEMO" --start-paused --capture-tick "$TICK" \
    --capture-frame "$(cygpath -m "$PWD/$bmp")" --metrics-file "$csv" "$@" \
    > "$log" 2>&1
  printf -v "RC_$name" '%s' "$?"
  printf -v "TRACE_$name" '%s' "$trace"
  printf -v "BMP_$name" '%s' "$bmp"
}

echo
echo "=== 3/6 the refusal paths, executed on malformed fixtures ==="
# A refusal counter of zero is not evidence that a refusal works. The reference
# map reaches none of them -- every layer resolves -- so `refused=0` was a
# reading about the map, not about the code. `--refusal-fixtures` hands the same
# resolver the malformed inputs it claims to reject. It is the same code the
# product runs: main.cpp calls resolveBlendLayer and resolveBlendModulate, so a
# reason proved here is the reason a map with broken assets would print.
REFUSAL=$("$PROBE" --refusal-fixtures 2>&1)
REFUSAL_RC=$?
echo "$REFUSAL" > "$OUT/refusal-fixtures.txt"
assert_eq "refusal fixtures rc" "$REFUSAL_RC" "0"
assert_eq "refusal cases" "$(kv "$REFUSAL" refusal.cases)" "8"
assert_eq "refusal cases refused" "$(kv "$REFUSAL" refusal.refused)" "8"
# Two controls. A fixture set in which everything is refused would satisfy the
# counts above while proving only that the resolver refuses everything; the
# controls say a well-formed layer and a well-formed modulate still resolve to
# tile-sized pixels.
assert_eq "control layer resolves" "$(kv "$REFUSAL" refusal.controlLayerResolved)" "1"
assert_eq "control modulate resolves" "$(kv "$REFUSAL" refusal.controlModulateResolved)" "1"
assert_eq "control layer pixels" "$(kv "$REFUSAL" refusal.controlLayerPixels)" "16384"
# Each reason by the phrase that makes it specific. A resolver returning one
# string for every failure would pass "refused=8" and fail this table.
while IFS='|' read -r rcase expect; do
  [ -z "$rcase" ] && continue
  got=$(kv "$REFUSAL" "refusal.$rcase.reason")
  case "$got" in
    *"$expect"*) echo "  OK   $rcase -> $got" ;;
    *) echo "  FAIL $rcase -> ${got:-<none>}, expected to contain: $expect"; fail=1 ;;
  esac
done <<'TABLE'
layer-base-unresolved|the first layer did not resolve
layer-second-unresolved|did not resolve from loose, VPK or pakfile
layer-not-vtf|is not a readable VTF
layer-decode-empty|decoded to nothing
layer-resample-failed|could not be resampled
modulate-no-second-layer|no usable second layer to modulate
modulate-unresolved|did not resolve from loose, VPK or pakfile
modulate-decode-empty|decoded to nothing or could not be resampled
TABLE

echo
echo "=== 4/6 the product's own reading of the same map ==="
run_product allon
run_product alloff --world-blend off
run_product allrep
run_product terron  --world-geometry terrain
run_product terroff --world-geometry terrain --world-blend off
run_product none    --world-geometry none
assert_eq "all/on rc"     "$RC_allon"  "0"
assert_eq "all/off rc"    "$RC_alloff" "0"
assert_eq "all/repeat rc" "$RC_allrep" "0"
assert_eq "terrain/on rc"  "$RC_terron"  "0"
assert_eq "terrain/off rc" "$RC_terroff" "0"
assert_eq "none rc"        "$RC_none"    "0"

BLEND_ON=$(grep -ao "gpu: world blend enabled=.*" "$TRACE_allon" 2>/dev/null | head -1)
BLEND_OFF=$(grep -ao "gpu: world blend enabled=.*" "$TRACE_alloff" 2>/dev/null | head -1)
ATLAS=$(grep -ao "gpu: world atlas .*" "$TRACE_allon" 2>/dev/null | head -1)
FETCH=$(grep -ao "gpu: world blend-fetch .*" "$TRACE_allon" 2>/dev/null | head -1)
GEOM_ON=$(grep -ao "gpu: world geometry=.*" "$TRACE_allon" 2>/dev/null | head -1)
GEOM_TER=$(grep -ao "gpu: world geometry=.*" "$TRACE_terron" 2>/dev/null | head -1)
GEOM_NONE=$(grep -ao "gpu: world geometry=.*" "$TRACE_none" 2>/dev/null | head -1)
VIS_ON=$(grep -ao "gpu: world vis .*" "$TRACE_allon" 2>/dev/null | head -1)
MATFETCH=$(grep -ao "gpu: world material-fetch .*" "$TRACE_allon" 2>/dev/null | head -1)
VIS_TER=$(grep -ao "gpu: world vis .*" "$TRACE_terron" 2>/dev/null | head -1)
if [ -n "$BLEND_ON" ] && [ -n "$BLEND_OFF" ] && [ -n "$ATLAS" ]; then
  printf '%s\n%s\n%s\n%s\n%s\n%s\n%s\n%s\n' "$BLEND_ON" "$BLEND_OFF" "$ATLAS" "$FETCH" \
    "$GEOM_ON" "$GEOM_TER" "$GEOM_NONE" "$MATFETCH" > "$OUT/product-readings.txt"
else
  echo "  FAIL a product run produced no blend reading, so $OUT/product-readings.txt is left as it was"; fail=1
fi
# The switch is on the reading, not assumed by the reader.
assert_eq "all/on switch" "$(rval "$GEOM_ON" blend)" "on"
assert_eq "all/on enabled" "$(rval "$BLEND_ON" enabled)" "1"
assert_eq "all/on terrainOnly" "$(rval "$BLEND_ON" terrainOnly)" "0"
# The atlas. This is the reading that was missing when the world was drawn with
# one material texture and every counter still looked reasonable: `create` was
# refusing the 67.6 MiB atlas against a 64 MiB guard, `uploadWorldGeometry`
# returned false, and the caller fell back to the single-texture overload.
assert_eq "atlas state" "$(rval "$ATLAS" state)" "bound"
assert_eq "atlas size" "$(rval "$ATLAS" size)" "4112"
assert_ge "atlas bytes" "$(rval "$ATLAS" bytes)" "67000000"
# The blend tiles, and the vertices that carry a weight. A tile with no weighted
# vertex would mean the material reached the atlas and no geometry asked for it.
assert_ge "blend tiles" "$(rval "$BLEND_ON" tiles)" "1"
  assert_ge "modulate tiles" "$(rval "$BLEND_ON" modulateTiles)" "1"
  # The order, as the product counted it: three materials declare a second layer
  # and were sorted ahead of the cap. The probe's census agrees, but this is the
  # reading the product itself produced for this frame, and it is what m6 moves.
  assert_eq "blend materials sorted first (product)" "$(rval "$MATFETCH" blendFirst)" "3"
  # The ordering's outcome, which a triangle-count order cannot produce: every
  # blend material is offered a tile before the cap cuts (rank <= 64, measured 3)
  # and none is dropped (measured 0).
  assert_le "last blend material's rank in the sort" "$(rval "$MATFETCH" blendLastRank)" "64"
  assert_eq "blend materials dropped by the cap (product)" "$(rval "$MATFETCH" blendCapped)" "0"
  # The forward value, kept so the mutation's "this must not move" assertions can
  # compare against what this run measured rather than against a number typed
  # into the script.
  FORWARD_MODULATE_TILES=$(rval "$BLEND_ON" modulateTiles)
  FORWARD_BLEND_RANK=$(rval "$MATFETCH" blendLastRank)
  FORWARD_BLEND_CAPPED=$(rval "$MATFETCH" blendCapped)
  FORWARD_MATERIAL_TILED=$(rval "$MATFETCH" tiled)
assert_eq "malformed layers" "$(rval "$BLEND_ON" malformedLayers)" "0"
assert_ge "weighted vertices on the GPU" "$(rval "$BLEND_ON" weightedVertices)" "1000"
assert_ge "modulated vertices on the GPU" "$(rval "$BLEND_ON" modulatedVertices)" "1000"
# The distribution, which is what a count cannot give: a constant written into
# every vertex would satisfy every count above and draw a flat second layer --
# the exact defect this round removed.
assert_ge "distinct weights on the GPU" "$(rval "$BLEND_ON" distinctWeights)" "2"
assert_eq "weight min on the GPU" "$(num "$(rval "$BLEND_ON" weightMin)")" "0"
assert_eq "weight max on the GPU" "$(num "$(rval "$BLEND_ON" weightMax)")" "1"
# The switch off must withhold the layer and nothing else: the tiles leave the
# atlas, the weights leave the vertices, and the fetch reports the skip as a
# skip rather than as a resolution failure.
assert_eq "all/off enabled" "$(rval "$BLEND_OFF" enabled)" "0"
assert_eq "all/off tiles" "$(rval "$BLEND_OFF" tiles)" "0"
assert_eq "all/off weighted vertices" "$(rval "$BLEND_OFF" weightedVertices)" "0"
assert_ge "all/off disabled layers" "$(rval "$FETCH" disabled)" "0"
OFF_FETCH=$(grep -ao "gpu: world blend-fetch .*" "$TRACE_alloff" 2>/dev/null | head -1)
assert_ge "all/off layers skipped by the switch" "$(rval "$OFF_FETCH" disabled)" "1"
assert_eq "all/off layers resolved" "$(rval "$OFF_FETCH" resolved)" "0"
# The terrain-only frame contains the terrain. Asserted on the uploaded weight
# count rather than on the vis reading: `--world-geometry terrain` deliberately
# bypasses the pvs index buffer (it was built from the full triangle list and
# would index past a filtered vertex buffer), so the vis line describes the clip
# rather than the upload. `weightedVertices` is the number of vertices the
# filtered buffer actually carries a weight for.
BLEND_TER=$(grep -ao "gpu: world blend enabled=.*" "$TRACE_terron" 2>/dev/null | head -1)
assert_eq "terrain-only switch" "$(rval "$GEOM_TER" geometry)" "terrain"
assert_eq "terrain-only isolates the frame" "$(rval "$BLEND_TER" terrainOnly)" "1"
assert_ge "terrain-only weighted vertices" "$(rval "$BLEND_TER" weightedVertices)" "1000"
assert_eq "none switch" "$(rval "$GEOM_NONE" geometry)" "none"
# And the terrain is a strict subset of the world, by the vis identity T1 pinned.
assert_ge "all triangles" "$(rval "$VIS_ON" inputTriangles)" "100000"

echo
echo "=== 4b/6 the atlas coverage: which materials the cap drops ==="
# The 64-tile atlas is a cap, and until this reading existed nobody had measured
# which materials it drops. On the reference map 148 materials are used, 64 take
# a tile, and the rest draw the fallback. Before this round the third blend
# material -- the COALMINES terrain -- was one of them, so part of the map's
# terrain was a flat foreign tile. The census reproduces the product's order
# (blend materials first, then triangle count) and asks the product's own camera
# cluster which of the dropped materials are in view, so `capped` here means the
# set the frame actually dropped.
COVER=$("$PROBE" --map "$TF/maps/$MAP.bsp" --tf-root "$TF" \
  --cluster "$(rval "$VIS_ON" cameraCluster)" 2>&1)
echo "$COVER" > "$OUT/atlas-coverage-$MAP.txt"
assert_eq "atlas census uses the product's cluster" \
  "$(kv "$COVER" atlas.eyeCluster)" "$(rval "$VIS_ON" cameraCluster)"
# All three WorldVertexTransition materials the map's displacements carry fit
# ahead of the cap. This is the reading that changed when the sort changed: the
# pre-merge census had one of the three past it.
assert_eq "blend materials ordered ahead of the cap" "$(kv "$COVER" atlas.blendFirst)" "3"
assert_eq "blend materials past the cap" "$(kv "$COVER" atlas.cappedBlendMaterials)" "0"
# The cap still drops texturable single-layer materials, and some are in view.
# Both are recorded rather than asserted away: the decision in the report is
# about these numbers (48 texturable, 41 in view on the pinned camera), and a
# future batching change has to move them.
assert_ge "texturable materials past the cap" "$(kv "$COVER" atlas.cappedTexturable)" "1"
assert_ge "texturable capped materials in view" "$(kv "$COVER" atlas.cappedTexturableVisible)" "1"

echo
echo "=== 5/6 the pixels, and where they are ==="
# The region, measured rather than assumed. `terroff` draws the terrain and
# nothing else (the sky and water passes are forced off in that mode), `none`
# draws nothing at all from the same camera, and the terrain region is the set
# of pixels where the two differ. No projection is recomputed and no clear
# colour is hardcoded, so a mask that overlays the picture is guaranteed by
# construction rather than by a convention that could be transposed.
REGION=$("python" - "$BMP_allon" "$BMP_alloff" "$BMP_allrep" "$BMP_terron" "$BMP_terroff" "$BMP_none" 2>&1 <<'PY'
import struct, sys

def load(path):
    d = open(path, 'rb').read()
    if d[:2] != b'BM':
        raise SystemExit('not a BMP: ' + path)
    off = struct.unpack_from('<I', d, 10)[0]
    w, h = struct.unpack_from('<ii', d, 18)
    bpp = struct.unpack_from('<H', d, 28)[0]
    comp = struct.unpack_from('<I', d, 30)[0]
    if bpp not in (24, 32) or comp != 0:
        raise SystemExit('unsupported BMP: %s bpp=%d comp=%d' % (path, bpp, comp))
    topdown = h < 0
    h = abs(h)
    stride = ((w * bpp // 8) + 3) // 4 * 4
    return {'w': w, 'h': h, 'bpp': bpp, 'stride': stride, 'off': off,
            'topdown': topdown, 'data': d}

def pixels(img):
    """Yield each pixel as a bytes object, row-major from the top-left."""
    step = img['bpp'] // 8
    for y in range(img['h']):
        row = y if img['topdown'] else (img['h'] - 1 - y)
        base = img['off'] + row * img['stride']
        for x in range(img['w']):
            p = base + x * step
            yield img['data'][p:p + 3]

names = ['allon', 'alloff', 'allrep', 'terron', 'terroff', 'none']
imgs = {n: load(p) for n, p in zip(names, sys.argv[1:6 + 1])}
w, h = imgs['allon']['w'], imgs['allon']['h']
for n, i in imgs.items():
    if (i['w'], i['h'], i['bpp']) != (w, h, imgs['allon']['bpp']):
        raise SystemExit('frame %s is %dx%d/%dbpp, allon is %dx%d/%dbpp'
                         % (n, i['w'], i['h'], i['bpp'], w, h, imgs['allon']['bpp']))

px = {n: list(pixels(i)) for n, i in imgs.items()}
total = w * h
# The terrain region: everything the terrain-only frame put on screen.
mask = [px['terroff'][i] != px['none'][i] for i in range(total)]
mask_count = sum(mask)
d_on_off = [i for i in range(total) if px['allon'][i] != px['alloff'][i]]
d_on_rep = sum(1 for i in range(total) if px['allon'][i] != px['allrep'][i])
d_terrain = [i for i in range(total) if px['terron'][i] != px['terroff'][i]]
inside = sum(1 for i in d_on_off if mask[i])
inside_terrain = sum(1 for i in d_terrain if mask[i])
# T12 post-merge: with the world drawn, a terrain pixel only shows its blend
# difference if no other geometry covers it. Every pixel the blender changed
# must therefore also be a pixel the terrain-only pair changed -- if the full
# frame changes where the terrain alone does not, something outside the terrain
# is being blended.
in_terrain_pair = sum(1 for i in d_on_off if px['terron'][i] != px['terroff'][i])
# And the shortfall itself, attributed: a terrain-only blend pixel that the
# composed frame does not change, at a pixel where the world frame differs from
# the terrain-only frame, is covered by world geometry. This is the reading that
# explains 3530 -> 470 without appealing to "the blend got weaker".
lost = 0
occluded = 0
occluded2 = 0
for i in d_terrain:
    if px['allon'][i] != px['alloff'][i]:
        continue
    lost += 1
    if px['allon'][i] != px['terron'][i]:
        occluded += 1
    # THE SECOND ARM. The one above asks "is the composed frame a different
    # colour from the terrain-only frame", which cannot answer anything when
    # both are black -- and after T29 seven pixels are exactly that. This arm
    # asks the question the attribution actually needs: is ANYTHING drawn here
    # in the composed frame, i.e. is the pixel not the background? `none` draws
    # nothing at all from the same camera, so a composed pixel that differs
    # from `none` has a surface on it, whatever colour that surface is.
    if px['allon'][i] != px['none'][i]:
        occluded2 += 1
print('frame=%dx%d' % (w, h))
print('pixels=%d' % total)
print('terrain_region=%d' % mask_count)
print('terrain_region_pct=%d' % (mask_count * 100 // total))
print('diff_on_off=%d' % len(d_on_off))
print('diff_on_off_in_region=%d' % inside)
print('diff_on_off_out_region=%d' % (len(d_on_off) - inside))
print('diff_on_repeat=%d' % d_on_rep)
print('diff_terrain_pair=%d' % len(d_terrain))
print('diff_terrain_pair_in_region=%d' % inside_terrain)
print('diff_on_off_in_terrain_pair=%d' % in_terrain_pair)
print('diff_terrain_pair_not_in_full=%d' % lost)
print('diff_terrain_pair_occluded=%d' % occluded)
print('diff_terrain_pair_covered=%d' % occluded2)
PY
)
echo "$REGION" > "$OUT/region.txt"
printf '%s\n' "$REGION" | sed 's/^/  /'
[ -n "$(rk "$REGION" frame)" ] || { echo "  FAIL the region comparison produced no reading"; fail=1; }
# The region has to be a real region. A mask covering nothing would make the
# containment assertion below trivially true; a mask covering everything would
# make it trivially true as well. The absolute floor is what carries the weight:
# on the pinned frame the terrain covers 13714 pixels, and a build that stopped
# submitting the terrain would read 0.
assert_ge "terrain region pixels" "$(rk "$REGION" terrain_region)" "5000"
assert_le "terrain region percent" "$(rk "$REGION" terrain_region_pct)" "95"
# The change itself, on the full frame. Measured 470 pixels on the integrated
# tree (post-T11), against 1930 on the pre-merge tree. The drop is not a blend
# defect: with only the terrain drawn the same pair changes 3530 pixels, and
# 3036 of the 3530 that disappear from the full frame are pixels where the world
# pass now draws over the terrain -- T11 fixed the winding, so more geometry is
# in front of the terrain band. The floor therefore guards "the blend still
# changes the composed frame at all"; the sharper claim, and the one that
# carries the blend correctness, is the terrain-only pair below at 3530.
assert_ge "pixels changed by the blend" "$(rk "$REGION" diff_on_off)" "400"
# And the claim the task actually asks for: the change is in the terrain.
OUTSIDE=$(rk "$REGION" diff_on_off_out_region)
INSIDE=$(rk "$REGION" diff_on_off_in_region)
if [ -n "$OUTSIDE" ] && [ -n "$INSIDE" ]; then
  # 99% of the changed pixels must lie inside the measured terrain region. The
  # residue is allowed because a pixel on the terrain's silhouette can be
  # covered by the world pass's own edge in one frame and not the other.
  if [ "$(( INSIDE * 100 ))" -ge "$(( (INSIDE + OUTSIDE) * 99 ))" ] && [ "$INSIDE" -gt 0 ]; then
    echo "  OK   $INSIDE of $(( INSIDE + OUTSIDE )) changed pixels are inside the terrain region"
  else
    echo "  FAIL only $INSIDE of $(( INSIDE + OUTSIDE )) changed pixels are inside the terrain region"; fail=1
  fi
else
  echo "  FAIL the region comparison did not report the containment counts"; fail=1
fi
# The same pair with the rest of the world removed: every pixel that changes is
# terrain by construction, so this is the sharper of the two claims. Measured
# 3530 on the integrated tree; the floor is 15% below that, which is where it
# stops being a restatement of the measurement.
assert_ge "pixels changed with only terrain drawn" "$(rk "$REGION" diff_terrain_pair)" "3000"
assert_eq "terrain pair changes are all in the region" \
  "$(rk "$REGION" diff_terrain_pair_in_region)" "$(rk "$REGION" diff_terrain_pair)"
# Every pixel the composed frame changes must be one the terrain-only pair also
# changed. This is what turns "the shortfall is occlusion" from an explanation
# into an assertion: a blend that leaked onto a wall would change a composed
# pixel that the terrain pair never touched.
assert_eq "composed blend changes are a subset of the terrain pair" \
  "$(rk "$REGION" diff_on_off_in_terrain_pair)" "$(rk "$REGION" diff_on_off)"
# The attribution of the shortfall, asserted rather than narrated. TWO ARMS,
# because one is not enough.
#
# The original test asked one question: of the terrain-only blend pixels the
# composed frame does not show, how many are at pixels where the composed frame
# differs from the terrain-only frame ("something else drew a different
# colour"). That reads 4,237 of 4,403 on the pre-T29 frames and 0 of 7 after
# T29 -- and it reads 0 not because nothing is in front of those 7 pixels but
# because the thing in front is BLACK and so is the blended terrain, so the
# "different colour" question has no answer there.
#
# The second arm asks the question the attribution actually needs and which
# does not depend on either surface's colour: is anything drawn at all? `none`
# draws nothing from this camera, so a composed pixel that is not `none` has a
# surface on it. That arm reads 7 of 7.
#
# Evidence for the 7 pixels specifically, from the product's own geometry
# oracle rather than from these frames:
#   evidence/terrain-blend/region-attribution.txt
# --world-hole-trace at the seven coordinates reports
#   drawnHits=7 of 7, face 9764 (WOOD/WALL020B) at depth 0.9722,
# and a --world-face-id frame decoded at the same coordinates reads 9764 at
# every one. Two instruments, computing it different ways, same answer:
# occlusion.
#
# Both arms are asserted. The first keeps its 90% floor for the case it was
# designed for (a blend that leaked onto a wall would fail it); the second is
# what carries the claim when the occluder is black.
LOST=$(rk "$REGION" diff_terrain_pair_not_in_full)
OCCLUDED=$(rk "$REGION" diff_terrain_pair_occluded)
COVERED=$(rk "$REGION" diff_terrain_pair_covered)
if [ -n "$LOST" ] && [ -n "$OCCLUDED" ] && [ -n "$COVERED" ] && [ "$LOST" -gt 0 ] \
   && { [ "$OCCLUDED" -ge "$(( LOST * 90 / 100 ))" ] || [ "$COVERED" -ge "$(( LOST * 90 / 100 ))" ]; }; then
  echo "  OK   $LOST terrain-pair pixels are not shown by the composed frame: $OCCLUDED are behind a different-coloured surface, $COVERED are behind a drawn surface (>= 90% by at least one arm)"
else
  echo "  FAIL of ${LOST:-<none>} missing terrain-pair pixels, only ${OCCLUDED:-<none>} differ from the terrain-only frame and only ${COVERED:-<none>} have any surface drawn on them"; fail=1
fi
# And the sharper statement for the colour-blind case: every lost pixel must be
# a pixel the composed frame actually drew something on. A lost pixel that is
# still the background colour would be a pixel the composed frame simply did
# not render, which is a different and much worse explanation than occlusion.
if [ -n "$LOST" ] && [ -n "$COVERED" ]; then
  if [ "$COVERED" -eq "$LOST" ]; then
    echo "  OK   every one of the $LOST is a pixel the composed frame drew on (not background)"
  else
    echo "  FAIL only $COVERED of $LOST lost pixels have a surface drawn on them; the rest are background"; fail=1
  fi
fi
# And the repeat.
assert_eq "all/on == all/repeat" "$(rk "$REGION" diff_on_repeat)" "0"

echo
# The frames themselves, under stable names and with their hashes. A region
# count without the picture it was taken from is a number a reader has to take
# on trust; these are the six frames every assertion above was computed from.
mkdir -p "$OUT/frames"
cp "$BMP_allon"   "$OUT/frames/all-on.bmp"
cp "$BMP_alloff"  "$OUT/frames/all-off.bmp"
cp "$BMP_allrep"  "$OUT/frames/all-repeat.bmp"
cp "$BMP_terron"  "$OUT/frames/terrain-on.bmp"
cp "$BMP_terroff" "$OUT/frames/terrain-off.bmp"
cp "$BMP_none"    "$OUT/frames/none.bmp"
( cd "$OUT/frames" && sha256sum ./*.bmp > ../frames.sha256 )
sed 's/^/  /' "$OUT/frames.sha256"

echo
if [ "$MUTATION" -eq 1 ]; then
  echo "=== 6/6 the mutations: six defects, each of which must turn this gate red ==="
  # The cheap half first: values the run must NOT produce.
  if [ "$(rval "$BLEND_ON" tiles)" = "0" ]; then
    echo "  FAIL the blend reached no tile while the switch was on"; exit 1
  fi
  if [ "$(rval "$BLEND_ON" distinctWeights)" = "1" ]; then
    echo "  FAIL every uploaded weight was the same value"; exit 1
  fi
  if [ "$(rk "$REGION" diff_on_repeat)" != "0" ]; then
    echo "  FAIL the repeat frame was not byte-identical before any mutation"; exit 1
  fi

  restore_source() { # restore_source <backup> <live> <name> [targets...]
    local backup="$1" live="$2" name="$3"; shift 3
    cp "$backup" "$live"
    if ! bash build-target.sh "$@" > "$SCRATCH/restore-$name.$RUN_TAG.build" 2>&1; then
      echo "  RESTORE-BUILD=FAIL ($name)"; return 1
    fi
    if ! cmp -s "$live" "$backup"; then
      echo "  RESTORE=FAIL ($name: the source did not come back byte-identical)"; return 1
    fi
    rm -f "$backup"
    if [ -e "$backup" ]; then
      echo "  RESTORE=FAIL ($name: the backup could not be removed: $backup)"; return 1
    fi
    return 0
  }
  patch_in() { # patch_in <file> <find> <replace> -- CRLF-aware, refuses a no-op
    python - "$1" "$2" "$3" <<'PY'
import io, sys
path, find, repl = sys.argv[1], sys.argv[2], sys.argv[3]
raw = io.open(path, 'rb').read()
crlf = b'\r\n' in raw
data = raw.decode('utf-8')
if crlf:
    find = find.replace('\n', '\r\n')
    repl = repl.replace('\n', '\r\n')
if find not in data:
    sys.exit('PATTERN NOT FOUND: ' + find[:70].replace('\r', '\\r').replace('\n', '\\n'))
io.open(path, 'wb').write(data.replace(find, repl, 1).encode('utf-8'))
PY
  }

  # ---- m1: the per-vertex alpha is never read ------------------------------
  # The parser still walks every displacement vertex; it just stops storing the
  # alpha at offset +16. Every weight becomes the sentinel, so the shader takes
  # the single-layer path and the map draws flat. Nothing else in the BSP read
  # changes, which is what makes this the sharpest test of the offset.
  echo "--- m1: CDispVert::m_flAlpha is not read (every weight becomes the sentinel)"
  M1_BACKUP="$SCRATCH/bsp_map.cpp.alpha.backup"
  cp native/src/bsp_map.cpp "$M1_BACKUP"
  patch_in native/src/bsp_map.cpp \
    '                gridAlpha[static_cast<std::size_t>(y * size + x)] = std::clamp(alpha, 0.0f, 1.0f);' \
    '                gridAlpha[static_cast<std::size_t>(y * size + x)] = -1.0f;' || {
      echo "  MUTATION-SETUP=FAIL (m1: the patch did not apply)"
      cp "$M1_BACKUP" native/src/bsp_map.cpp; echo "TERRAIN-BLEND-MUTATION=FAIL"; exit 1; }
  if ! bash build-target.sh terrain_blend_probe > "$SCRATCH/m1.build" 2>&1; then
    echo "  MUTATION-SETUP=FAIL (m1: the rebuild failed)"
    restore_source "$M1_BACKUP" native/src/bsp_map.cpp m1 terrain_blend_probe || true
    echo "TERRAIN-BLEND-MUTATION=FAIL"; exit 1
  fi
  M1=$("$PROBE" --map "$TF/maps/$MAP.bsp" --tf-root "$TF" 2>&1)
  M1_WEIGHTED=$(kv "$M1" disp.weightedVertices)
  M1_SENTINEL=$(kv "$M1" disp.sentinelVertices)
  if [ "$M1_WEIGHTED" = "0" ] && [ "$M1_SENTINEL" != "0" ]; then
    echo "  MUTATION-CAUGHT=PASS (m1: weightedVertices=0 sentinelVertices=$M1_SENTINEL)"
  else
    echo "  MUTATION-CAUGHT=FAIL (m1: weightedVertices=$M1_WEIGHTED sentinelVertices=$M1_SENTINEL)"
    restore_source "$M1_BACKUP" native/src/bsp_map.cpp m1 terrain_blend_probe || true
    echo "TERRAIN-BLEND-MUTATION=FAIL"; exit 1
  fi
  echo "$M1" > "$OUT/mutated-m1-no-vertex-alpha.txt"
  restore_source "$M1_BACKUP" native/src/bsp_map.cpp m1 terrain_blend_probe || {
    echo "TERRAIN-BLEND-MUTATION=FAIL"; exit 1; }

  # ---- m2: `$basetexture2` is not parsed -----------------------------------
  # The material still resolves its first layer and still reaches the atlas, so
  # every count that existed before this round stays green. The reading that
  # moves is the one this round added: the material stops being a blend
  # material at all.
  echo "--- m2: \$basetexture2 is never read (the terrain stops being a blend material)"
  M2_BACKUP="$SCRATCH/vmt_material.cpp.basetexture2.backup"
  cp native/src/vmt_material.cpp "$M2_BACKUP"
  patch_in native/src/vmt_material.cpp \
    '  material.baseTexture2 = scalar(root, "$basetexture2");' \
    '  // mutation: the second layer is never read' || {
      echo "  MUTATION-SETUP=FAIL (m2: the patch did not apply)"
      cp "$M2_BACKUP" native/src/vmt_material.cpp; echo "TERRAIN-BLEND-MUTATION=FAIL"; exit 1; }
  if ! bash build-target.sh terrain_blend_probe > "$SCRATCH/m2.build" 2>&1; then
    echo "  MUTATION-SETUP=FAIL (m2: the rebuild failed)"
    restore_source "$M2_BACKUP" native/src/vmt_material.cpp m2 terrain_blend_probe || true
    echo "TERRAIN-BLEND-MUTATION=FAIL"; exit 1
  fi
  M2=$("$PROBE" --map "$TF/maps/$MAP.bsp" --tf-root "$TF" 2>&1)
  M2_DECLARED=$(kv "$M2" blend.declared)
  M2_SHADER=$(kv "$M2" "disp.material.$BLEND_MATERIAL.shader")
  if [ "$M2_DECLARED" = "0" ] && [ "$M2_SHADER" = "WorldVertexTransition" ]; then
    echo "  MUTATION-CAUGHT=PASS (m2: declared=0 while the shader still reads WorldVertexTransition)"
  else
    echo "  MUTATION-CAUGHT=FAIL (m2: declared=$M2_DECLARED shader=$M2_SHADER)"
    restore_source "$M2_BACKUP" native/src/vmt_material.cpp m2 terrain_blend_probe || true
    echo "TERRAIN-BLEND-MUTATION=FAIL"; exit 1
  fi
  echo "$M2" > "$OUT/mutated-m2-no-second-layer.txt"
  restore_source "$M2_BACKUP" native/src/vmt_material.cpp m2 terrain_blend_probe || {
    echo "TERRAIN-BLEND-MUTATION=FAIL"; exit 1; }

  # ---- m3: the weights are a constant --------------------------------------
  # The layer is fetched, the tile is in the atlas, and every vertex is given
  # weight 1. Every count stays the same -- weightedVertices is unchanged -- and
  # the terrain draws the second layer flat. Only the distribution moves, which
  # is why it is a reading.
  echo "--- m3: every uploaded weight is forced to 1 (the blend draws the second layer flat)"
  M3_BACKUP="$SCRATCH/native_renderer.cpp.weight.backup"
  cp native/src/native_renderer.cpp "$M3_BACKUP"
  patch_in native/src/native_renderer.cpp \
    '        a.blend = triangle.aAlpha; b.blend = triangle.bAlpha; c.blend = triangle.cAlpha;' \
    '        a.blend = b.blend = c.blend = 1.0f;  // mutation: a constant, not the map weight' || {
      echo "  MUTATION-SETUP=FAIL (m3: the patch did not apply)"
      cp "$M3_BACKUP" native/src/native_renderer.cpp; echo "TERRAIN-BLEND-MUTATION=FAIL"; exit 1; }
  if ! bash build-target.sh tf2_demo_native > "$SCRATCH/m3.build" 2>&1; then
    echo "  MUTATION-SETUP=FAIL (m3: the rebuild failed)"
    restore_source "$M3_BACKUP" native/src/native_renderer.cpp m3 tf2_demo_native || true
    echo "TERRAIN-BLEND-MUTATION=FAIL"; exit 1
  fi
  run_product m3
  M3_LINE=$(grep -ao "gpu: world blend enabled=.*" "$TRACE_m3" 2>/dev/null | head -1)
  M3_DISTINCT=$(rval "$M3_LINE" distinctWeights)
  M3_WEIGHTED=$(rval "$M3_LINE" weightedVertices)
  M3_MIN=$(num "$(rval "$M3_LINE" weightMin)")
  if [ "$M3_DISTINCT" = "1" ] && [ "$M3_MIN" = "1" ] && [ "${M3_WEIGHTED:-0}" -gt 1000 ] 2>/dev/null; then
    echo "  MUTATION-CAUGHT=PASS (m3: distinctWeights=1 weightMin=1 while weightedVertices=$M3_WEIGHTED)"
  else
    echo "  MUTATION-CAUGHT=FAIL (m3: distinctWeights=$M3_DISTINCT weightMin=$M3_MIN weightedVertices=$M3_WEIGHTED)"
    restore_source "$M3_BACKUP" native/src/native_renderer.cpp m3 tf2_demo_native || true
    echo "TERRAIN-BLEND-MUTATION=FAIL"; exit 1
  fi
  echo "$M3_LINE" > "$OUT/mutated-m3-constant-weight.txt"
  restore_source "$M3_BACKUP" native/src/native_renderer.cpp m3 tf2_demo_native || {
    echo "TERRAIN-BLEND-MUTATION=FAIL"; exit 1; }

  # ---- m4: the modulate never reaches the shader ---------------------------
  # `$blendmodulatetexture` is fetched and blitted, and the per-vertex flag that
  # tells the shader to apply it is never set. The blend still happens, the
  # weights are still the map's, and the edges are hard where the author asked
  # for a soft transition. The reading is `modulatedVertices`.
  echo "--- m4: the blend-modulate flag never reaches a vertex (hard edges)"
  M4_BACKUP="$SCRATCH/native_renderer.cpp.modulate.backup"
  cp native/src/native_renderer.cpp "$M4_BACKUP"
  patch_in native/src/native_renderer.cpp \
    '        const float modulate = modulatedMaterials.count(triangle.material) ? 1.0f : 0.0f;' \
    '        const float modulate = 0.0f;  // mutation: the modulate is fetched and never applied' || {
      echo "  MUTATION-SETUP=FAIL (m4: the patch did not apply)"
      cp "$M4_BACKUP" native/src/native_renderer.cpp; echo "TERRAIN-BLEND-MUTATION=FAIL"; exit 1; }
  if ! bash build-target.sh tf2_demo_native > "$SCRATCH/m4.build" 2>&1; then
    echo "  MUTATION-SETUP=FAIL (m4: the rebuild failed)"
    restore_source "$M4_BACKUP" native/src/native_renderer.cpp m4 tf2_demo_native || true
    echo "TERRAIN-BLEND-MUTATION=FAIL"; exit 1
  fi
  run_product m4
  M4_LINE=$(grep -ao "gpu: world blend enabled=.*" "$TRACE_m4" 2>/dev/null | head -1)
  M4_MODULATED=$(rval "$M4_LINE" modulatedVertices)
  M4_WEIGHTED=$(rval "$M4_LINE" weightedVertices)
  M4_TILES=$(rval "$M4_LINE" modulateTiles)
  # `modulateTiles` must hold, and hold at the value this run's forward section
  # measured. The first version of this assertion hardcoded 1 while the map
  # carries two modulate tiles, so the case read CAUGHT=FAIL with the defect
  # plainly live (modulatedVertices=0) -- an assertion that fails while the
  # mutation works is a worse instrument than one that was never written.
  if [ "$M4_MODULATED" = "0" ] && [ "${M4_WEIGHTED:-0}" -gt 1000 ] 2>/dev/null \
     && [ "$M4_TILES" = "$FORWARD_MODULATE_TILES" ] && [ "$M4_TILES" -ge 1 ] 2>/dev/null; then
    echo "  MUTATION-CAUGHT=PASS (m4: modulateTiles=$M4_TILES modulatedVertices=0 weightedVertices=$M4_WEIGHTED)"
  else
    echo "  MUTATION-CAUGHT=FAIL (m4: modulateTiles=$M4_TILES modulatedVertices=$M4_MODULATED weightedVertices=$M4_WEIGHTED)"
    restore_source "$M4_BACKUP" native/src/native_renderer.cpp m4 tf2_demo_native || true
    echo "TERRAIN-BLEND-MUTATION=FAIL"; exit 1
  fi
  echo "$M4_LINE" > "$OUT/mutated-m4-no-modulate.txt"
  restore_source "$M4_BACKUP" native/src/native_renderer.cpp m4 tf2_demo_native || {
    echo "TERRAIN-BLEND-MUTATION=FAIL"; exit 1; }

  # ---- m5: the winding swaps the positions and not the weights ------------
  # T11 reverses b and c to orient a displacement triangle. If the alphas do not
  # travel with them, the author's mask is mirrored inside every quad: the blend
  # still happens, and every count above still reads the same. Only
  # `alphaPairingDetached` can see it, and this mutation is what makes that
  # counter evidence instead of a zero.
  echo "--- m5: the winding swap leaves the blend weights on the old corners"
  M5_BACKUP="$SCRATCH/bsp_map.cpp.alphaswap.backup"
  cp native/src/bsp_map.cpp "$M5_BACKUP"
  patch_in native/src/bsp_map.cpp \
    '            std::swap(bAlpha, cAlpha);' \
    '            // mutation: the weights stay on the corners they were read on' || {
      echo "  MUTATION-SETUP=FAIL (m5: the patch did not apply)"
      cp "$M5_BACKUP" native/src/bsp_map.cpp; echo "TERRAIN-BLEND-MUTATION=FAIL"; exit 1; }
  if ! bash build-target.sh terrain_blend_probe > "$SCRATCH/m5.build" 2>&1; then
    echo "  MUTATION-SETUP=FAIL (m5: the rebuild failed)"
    restore_source "$M5_BACKUP" native/src/bsp_map.cpp m5 terrain_blend_probe || true
    echo "TERRAIN-BLEND-MUTATION=FAIL"; exit 1
  fi
  M5=$("$PROBE" --map "$TF/maps/$MAP.bsp" --tf-root "$TF" 2>&1)
  M5_DETACHED=$(kv "$M5" disp.alphaPairingDetached)
  M5_CHECKED=$(kv "$M5" disp.alphaPairingChecked)
  # The count of checked pairs must not move: a build that stopped checking
  # would report detached=0 and satisfy the forward assertion by doing nothing.
  if [ "${M5_DETACHED:-0}" -gt 0 ] 2>/dev/null && [ "$M5_CHECKED" = "$FORWARD_ALPHA_PAIRS" ]; then
    echo "  MUTATION-CAUGHT=PASS (m5: detached=$M5_DETACHED of checked=$M5_CHECKED)"
  else
    echo "  MUTATION-CAUGHT=FAIL (m5: detached=$M5_DETACHED checked=$M5_CHECKED, forward checked=$FORWARD_ALPHA_PAIRS)"
    restore_source "$M5_BACKUP" native/src/bsp_map.cpp m5 terrain_blend_probe || true
    echo "TERRAIN-BLEND-MUTATION=FAIL"; exit 1
  fi
  echo "$M5" > "$OUT/mutated-m5-detached-alpha.txt"
  restore_source "$M5_BACKUP" native/src/bsp_map.cpp m5 terrain_blend_probe || {
    echo "TERRAIN-BLEND-MUTATION=FAIL"; exit 1; }

  # ---- m6: the atlas order ignores the second layer ------------------------
  # T19 may place materials in multiple atlas batches, so a wrong order no
  # longer has to push a blend past a hard 64-tile cap. The mutation must still
  # move the product's measured last-blend rank while preserving the declaration
  # count and total tiled count. The probe's census is a separate implementation
  # and cannot be used to prove this one.
  echo "--- m6: the atlas order ignores \$basetexture2 again"
  M6_BACKUP="$SCRATCH/main.cpp.blendfirst.backup"
  cp native/src/main.cpp "$M6_BACKUP"
  patch_in native/src/main.cpp \
    '          if (lb != rb) return lb;' \
    '          if (false) return lb;  // mutation: triangle count decides again' || {
      echo "  MUTATION-SETUP=FAIL (m6: the patch did not apply)"
      cp "$M6_BACKUP" native/src/main.cpp; echo "TERRAIN-BLEND-MUTATION=FAIL"; exit 1; }
  if ! bash build-target.sh tf2_demo_native > "$SCRATCH/m6.build" 2>&1; then
    echo "  MUTATION-SETUP=FAIL (m6: the rebuild failed)"
    restore_source "$M6_BACKUP" native/src/main.cpp m6 tf2_demo_native || true
    echo "TERRAIN-BLEND-MUTATION=FAIL"; exit 1
  fi
  run_product m6
  M6_LINE=$(grep -ao "gpu: world material-fetch .*" "$TRACE_m6" 2>/dev/null | head -1)
  M6_FIRST=$(rval "$M6_LINE" blendFirst)
  M6_TILED=$(rval "$M6_LINE" tiled)
  M6_CAPPED=$(rval "$M6_LINE" blendCapped)
  M6_RANK=$(rval "$M6_LINE" blendLastRank)
  # `blendFirst` does not move here -- it counts declarations, not positions.
  # The rank must move above the fresh forward rank; the other readings are
  # controls proving that this mutation changed ordering rather than coverage.
  if [ "$M6_FIRST" = "$(rval "$MATFETCH" blendFirst)" ] \
     && [ "${M6_RANK:-0}" -gt "${FORWARD_BLEND_RANK:-0}" ] 2>/dev/null \
     && [ "$M6_CAPPED" = "$FORWARD_BLEND_CAPPED" ] \
     && [ "$M6_TILED" = "$FORWARD_MATERIAL_TILED" ]; then
    echo "  MUTATION-CAUGHT=PASS (m6: blend rank $FORWARD_BLEND_RANK -> $M6_RANK, capped=$M6_CAPPED, tiled=$M6_TILED)"
  else
    echo "  MUTATION-CAUGHT=FAIL (m6: blendFirst=$M6_FIRST blendCapped=$M6_CAPPED rank=$M6_RANK tiled=$M6_TILED)"
    restore_source "$M6_BACKUP" native/src/main.cpp m6 tf2_demo_native || true
    echo "TERRAIN-BLEND-MUTATION=FAIL"; exit 1
  fi
  echo "$M6_LINE" > "$OUT/mutated-m6-no-blend-priority.txt"
  restore_source "$M6_BACKUP" native/src/main.cpp m6 tf2_demo_native || {
    echo "TERRAIN-BLEND-MUTATION=FAIL"; exit 1; }

  # The forward sections ran BEFORE the mutations, and both verdicts have to
  # hold: the four mutations going red, and the four sections above staying
  # green. A caught mutation over a red gate is not a pass.
  if [ "$fail" -ne 0 ]; then
    echo "  the mutation half caught its four cases, but a forward assertion above"
    echo "  did not hold -- a caught mutation over a red gate is not a pass"
    echo "TERRAIN-BLEND-MUTATION=FAIL"; exit 1
  fi
  echo "MUTATION-CAUGHT=PASS (the unread vertex alpha, the unparsed second layer,"
  echo "  the constant weight, the dropped modulate, the weights left behind by the"
  echo "  winding swap and the unordered atlas each go red; every source came back"
  echo "  byte-identical)"
  exit 0
fi

echo
if [ "$fail" -eq 0 ]; then
  echo "TERRAIN-BLEND=PASS"
else
  echo "TERRAIN-BLEND=FAIL"
fi
exit "$fail"
