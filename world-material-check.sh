#!/usr/bin/env bash
# world-material-check.sh -- assert the six world material and lighting
# capabilities each carry a state and a reason, and that each state can go red.
#
# WHY THIS FILE EXISTS
# --------------------
# Before this gate, the world pass answered six questions with one word each:
# `lightmapMode: source-equivalent`, `cubemapMode: approximate-2d`,
# `waterSurface: approximate-warp`, and nothing at all for skybox, displacement
# or VIS clipping. None of those words could be wrong, so none of them was a
# claim. The rule this task works under is that a reading nobody can refute is
# not a reading.
#
# What this gate asserts, in order:
#   1. the probe's self-tests pass (the pure functions, the empty-BSP case, and
#      -- since T1 -- the sky box's vertices and the pvs clip's indices)
#   2. every synthetic fixture passes, on BSP bytes built by this repo and read
#      back through the product's own parser -- so the parser is what is tested
#   3. every fixture's mutation moves its reading, which is what makes 2 mean
#      something: a fixture whose mutation changes nothing is a fixture that was
#      asserting a constant
#   4. the real map's six states and their counters, pinned -- including the
#      atlas width the packer chose and how many faces it could not fit
#   5. the negative control: a map with no BSP must report every capability
#      unavailable, which is the reading that would catch a gate that passes on
#      constants
#   6. the product's own trace ties the GPU-side lightmap count to the parse side,
#      and carries the sky box's vertex count and the clip's input/drawn pair
#   7. the product builds the map's cubemap and the env pass selects it
#   8. the two switches off: the sky is not drawn, the clip does not run, and the
#      drawn triangle count is the whole buffer
#   mutation: six wrong expectations, then the sky draw and the pvs clip deleted
#      from the product itself -- both must turn the gate red, and both sources
#      must come back byte-identical.
#
# EVERY PER-RUN FILE CARRIES A RUN TAG ($RUN_TAG), and it is not cosmetic.
#   Measured 2026-10-14: a `rm -f` was refused by the harness's delete guard,
#   the previous run's trace survived with a 10:19 timestamp inside a 10:30
#   run, and section 8's assertions then printed OK off bytes a different run
#   had produced -- while the run they were describing had failed. `rm -f`
#   exits 0 when it cannot remove a file, so "clear, then read" is not a
#   fresh-evidence guarantee here. A path carrying this run's own start time and
#   pid cannot be read as somebody else's evidence, and a run that produces no
#   reading now leaves the committed evidence file untouched and fails, rather
#   than writing an empty line over it.
#
# THE MUTATION PASS RUNS THE FORWARD SECTIONS FIRST, AND `fail` DECIDES IT.
#   The mutation branch used to `exit 0` unconditionally, so a FAIL printed by
#   any forward section still returned success. Both verdicts now have to hold:
#   the two mutations going red, and the eight sections holding.
#
# Usage: bash world-material-check.sh [--mutation]
# Exit:  0 = every assertion held (or, with --mutation, went red).
set -uo pipefail
cd "$(dirname "$0")"

EXE=native/build-nmake/world_material_probe.exe
PRODUCT=native/build-nmake/tf2_demo_native.exe
TF="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
MAP="cp_snakewater_final1"
DEMO="D:/TF2_Demo_Player/testdata/demos/bb841c6d379ff7c40d0c8baf99f59d8d_matcha-20260927-1347-cp_snakewater_final1.dem"
SCRATCH="${WORLD_MATERIAL_SCRATCH:-.scratch/world-material}"
OUT="${WORLD_MATERIAL_OUT:-evidence/world-material}"
mkdir -p "$OUT" "$SCRATCH"

# A run tag on every file this run writes. Measured 2026-10-14, mutation pass:
# the harness's delete guard refused one of this script's `rm -f` calls, the
# previous run's render-off.trace survived with a 10:19 timestamp inside a
# 10:30 run, and every assertion in that section then printed OK off bytes a
# different run had produced. `rm -f` exits 0 when it cannot remove a file it
# was not asked to create, so "clear then read" is not a fresh-evidence
# guarantee on this machine. A path carrying this run's own start time and pid
# cannot be read as somebody else's evidence, which makes the guard structural
# instead of assumed.
RUN_TAG="$(date +%Y%m%d%H%M%S)-$$"

MUTATION=0
[ "${1:-}" = "--mutation" ] && MUTATION=1

fail=0
for path in "$EXE" "$PRODUCT" "$TF" "$TF/maps/$MAP.bsp" "$DEMO"; do
  [ -e "$path" ] || { echo "FATAL: missing input: $path"; exit 1; }
done
command -v timeout >/dev/null 2>&1 || { echo "FATAL: missing tool: timeout"; exit 1; }
command -v cygpath >/dev/null 2>&1 || { echo "FATAL: missing tool: cygpath"; exit 1; }

assert_eq() { # assert_eq <label> <actual> <expected>
  if [ "${2:-<none>}" = "$3" ]; then
    echo "  OK   $1 = $3"
  else
    echo "  FAIL $1 = ${2:-<none>}, expected $3"; fail=1
  fi
}

# The probe prints one JSON object per line in two shapes: the self-test lines
# carry bare booleans under their own keys, the reading lines carry the
# capability key in the `reading` field and its value in `value`. One extractor
# per shape, because a sed that tries to serve both silently returns the empty
# string for one of them and an empty string matches nothing.
field() { # field <lines> <capability.key> -> quoted-string value
  printf '%s\n' "$1" | sed -n "s/.*\"reading\":\"$2\",\"value\":\"\([^\"]*\)\".*/\1/p" | head -1
}
flag() { # flag <lines> <key> -> true|false
  printf '%s\n' "$1" | sed -n "s/.*\"$2\":\(true\|false\).*/\1/p" | head -1
}
count() { # count <lines> <key> -> number, from the summary line
  printf '%s\n' "$1" | sed -n "s/.*\"$2\":\([0-9][0-9]*\).*/\1/p" | tail -1
}

echo "=== 1/8 the probe's self-tests pass ==="
# Two lines on purpose. The first is the pre-existing self-test and is left
# byte-identical because verify-fast.sh pins seven substrings of it and that
# script is outside this task's write scope; the second is the capability
# layer's own pure-function test.
SELF=$("$EXE" --self-test 2>&1)
echo "$SELF" > "$OUT/self-test.txt"
assert_eq "selfTest" "$(flag "$SELF" selfTest)" "true"
assert_eq "capabilitySelfTest" "$(flag "$SELF" capabilitySelfTest)" "true"
assert_eq "cubeAxes" "$(flag "$SELF" cubeAxes)" "true"
assert_eq "cubeZeroRefused" "$(flag "$SELF" cubeZeroRefused)" "true"
assert_eq "cubeSamplePath" "$(flag "$SELF" cubeSamplePath)" "true"
assert_eq "emptyBspAllUnavailable" "$(flag "$SELF" emptyBspAllUnavailable)" "true"
# T1 (2026-10-13): the third line. It drives evaluateSkyDrawRequest and
# clipWorldTrianglesByVis -- the two functions the renderer itself calls -- so
# these flags are statements about the product's drawing and its clipping, not
# about a test-only reimplementation of them.
assert_eq "worldDrawSelfTest" "$(flag "$SELF" worldDrawSelfTest)" "true"
assert_eq "skyBoxVertices" "$(flag "$SELF" skyBoxVertices)" "true"
assert_eq "skyBoxPlanes" "$(flag "$SELF" skyBoxPlanes)" "true"
assert_eq "skyBoxUvs" "$(flag "$SELF" skyBoxUvs)" "true"
assert_eq "skyPartialFallback" "$(flag "$SELF" skyPartialFallback)" "true"
assert_eq "skyNoSidesReason" "$(flag "$SELF" skyNoSidesReason)" "true"
assert_eq "skyDisabledReason" "$(flag "$SELF" skyDisabledReason)" "true"
assert_eq "visClipHides" "$(flag "$SELF" visClipHides)" "true"
assert_eq "visClipIndices" "$(flag "$SELF" visClipIndices)" "true"
assert_eq "visClipFollowsCamera" "$(flag "$SELF" visClipFollowsCamera)" "true"
assert_eq "visClipDisabled" "$(flag "$SELF" visClipDisabled)" "true"
assert_eq "visClipNeedsCamera" "$(flag "$SELF" visClipNeedsCamera)" "true"
assert_eq "visClipWithoutPvs" "$(flag "$SELF" visClipWithoutPvs)" "true"
assert_eq "atlasFitsAt2048" "$(flag "$SELF" atlasFitsAt2048)" "true"
assert_eq "atlasGrowsWhenFull" "$(flag "$SELF" atlasGrowsWhenFull)" "true"

echo
echo "=== 2/8 every synthetic fixture passes ==="
# The fixtures build real BSP bytes and read them back through BspParser, so a
# green run is a statement about the parser and the capability layer together.
FIX=$("$EXE" --fixture 2>&1)
echo "$FIX" > "$OUT/fixture-straight.txt"
# T8 (2026-10-16): 10 fixtures and 127 checks. The tenth is `skybox-six-distinct`,
# which gives each of the six sides its own flat colour and then asserts, per cube
# index, the direction, the suffix, both paths and one sampled pixel. It adds 37
# checks (6 x 6 + one "six different images") and is the only case in this file
# that can tell rt from dn: every counter below it -- vmtFound, texturesResolved,
# texturesDecoded, sizes, decodedBytes -- is unchanged by two suffixes trading
# places.
assert_eq "fixtures" "$(count "$FIX" fixtures)" "10"
assert_eq "fixture checks" "$(count "$FIX" checks)" "127"
assert_eq "fixture failures" "$(count "$FIX" bad)" "0"

echo
echo "=== 3/8 every fixture mutation moves its reading ==="
# A mutation that changes nothing is a mutation that proved nothing, so the
# probe fails the run when no case had one to execute.
MUT=$("$EXE" --fixture --mutation 2>&1)
echo "$MUT" > "$OUT/fixture-mutation.txt"
assert_eq "mutations executed" "$(count "$MUT" mutations)" "7"
assert_eq "mutation failures" "$(count "$MUT" mutationBad)" "0"
assert_eq "total failures with mutations" "$(count "$MUT" bad)" "0"
# Straight plus mutated assertions: 127 + 30. Pinned so a fixture that quietly
# stops asserting anything cannot pass by shrinking. The mutation count is still
# 7: `skybox-six-distinct` has no data mutation, because the defect it catches is
# a swap in the product's suffix table, which is a source mutation -- applied by
# world-render-reliability-check.sh, not by this file.
assert_eq "assertions with mutations" "$(count "$MUT" checks)" "157"

echo
echo "=== 4/8 the real map's six capabilities, pinned ==="
# Every number below was measured on this install on 2026-10-13. A changed
# install goes red on purpose: the finding it records ("this map's baked light
# fits the atlas except for 15 faces") would no longer be true, and a reader
# needs to be told rather than left with a stale note.
REAL=$("$EXE" --bsp "$TF/maps/$MAP.bsp" --tf-root "$TF" --map-name "$MAP" 2>&1)
echo "$REAL" > "$OUT/real-$MAP.txt"
assert_eq "lightmap state"        "$(field "$REAL" lightmap.state)"        "active"
assert_eq "lightmap faces"        "$(field "$REAL" lightmap.facesWithLightmap)" "11527"
# T1 (2026-10-13): these four numbers changed, and the change is the finding.
# The shelf packer used to be hard-coded to 2048, which is 2047 rows of usable
# height on this map -- 15 of the 11527 faces did not fit and were dropped with
# nothing reporting it. The width is chosen now, this map grows to 4096, and all
# 11527 pack. `facesDropped` staying 0 while `facesPacked` equals `facesWithLightmap`
# is the reading that would go red if the packer lost a face again.
assert_eq "lightmap packed"       "$(field "$REAL" lightmap.facesPacked)"  "11527"
assert_eq "lightmap dropped"      "$(field "$REAL" lightmap.facesDropped)" "0"
assert_eq "lightmap triangles"    "$(field "$REAL" lightmap.trianglesWithLightUv)" "199922"
assert_eq "lightmap atlas"        "$(field "$REAL" lightmap.atlas)"        "4096x1156"
assert_eq "lightmap atlas cap"    "$(field "$REAL" lightmap.atlasMaxWidth)" "4096"
assert_eq "lightmap atlas grew"   "$(field "$REAL" lightmap.atlasGrown)"   "true"
assert_eq "lightmap atlas reason" "$(field "$REAL" lightmap.atlasReason)"  "all 11527 faces fit the 4096-wide atlas"
assert_eq "lightmap uv in range"  "$(field "$REAL" lightmap.uvAllInRange)" "true"
# The cubemap reading changed on 2026-10-13 and the change is the finding: the
# probe used to look for six files per sample named c_<map>_<n><suffix>.vtf,
# which this install has never contained, so it read "0 of 33 complete" and
# called the capability a fallback. The files are one per sample --
# materials/maps/<map>/c<X>_<Y>_<Z>.vtf, named after the sample's own integer
# coordinates -- and TF2 ships them inside the BSP's pakfile, which is why they
# could not be found by listing the tf directory or the VPKs. All 33 now decode
# all six faces, and the file layout is the format's (for each mip, for each
# face), not the face-per-chain arrangement the first decoder assumed.
assert_eq "cubemap state"         "$(field "$REAL" cubemap.state)"         "active"
assert_eq "cubemap samples"       "$(field "$REAL" cubemap.samples)"       "33"
assert_eq "cubemap complete"      "$(field "$REAL" cubemap.samplesComplete)" "33"
assert_eq "cubemap faces decoded" "$(field "$REAL" cubemap.facesDecoded)"  "198"
assert_eq "cubemap from pakfile"  "$(field "$REAL" cubemap.fromPakfile)"   "33"
assert_eq "cubemap first path"    "$(field "$REAL" cubemap.firstPath)"     "materials/maps/cp_snakewater_final1/c-5135_2022_-15.vtf"
# Seven face slots per file where the header's rule declares six: the
# discrepancy is measured and reported rather than normalised away.
assert_eq "cubemap faces in file" "$(field "$REAL" cubemap.firstFacesInFile)" "7"
assert_eq "cubemap declared faces" "$(field "$REAL" cubemap.firstDeclaredFaces)" "6"
assert_eq "cubemap beyond six"    "$(field "$REAL" cubemap.facesBeyondSix)" "33"
# The pixels the decoder returned for the first sample's six faces. Counts alone
# cannot catch a decoder that walks the image section at the wrong stride: a
# misread file still yields six "faces" of plausible-looking bytes, so every
# count stays right while the pixels are ones nobody wrote. These two lines were
# measured twice on 2026-10-13 -- once with the format's layout and once with the
# face-per-chain layout the first decoder assumed -- and they differ, which is
# what makes them a check rather than a description.
assert_eq "cubemap face means"     "$(field "$REAL" cubemap.firstFaceMeans)" "62,45,56,58,51,54"
assert_eq "cubemap face checksums" "$(field "$REAL" cubemap.firstFaceChecksums)" "300ee24d,c9beee88,ed1d429a,02bc6fd5,5ecd73ed,ed94acc3"
assert_eq "sky state"             "$(field "$REAL" sky.state)"             "active"
assert_eq "sky name"              "$(field "$REAL" sky.name)"              "sky_well_01"
assert_eq "sky vmts found"        "$(field "$REAL" sky.vmtFound)"          "6"
# Six vmts is not a sky: the textures behind them are, and one of these is a
# 1x1 stub in the installed content (sky_well_01dn.vtf) while its five siblings
# are 512x256 and 512x512. Both the size range and the byte total are pinned, so
# "six faces decode" and "six faces decode to something the same size" cannot be
# confused for one another.
assert_eq "sky textures resolved" "$(field "$REAL" sky.texturesResolved)" "6"
assert_eq "sky textures decoded"  "$(field "$REAL" sky.texturesDecoded)"  "6"
assert_eq "sky largest face"      "$(field "$REAL" sky.firstSize)"        "512x256"
assert_eq "sky smallest face"     "$(field "$REAL" sky.smallestSize)"     "1x1"
assert_eq "sky decoded bytes"     "$(field "$REAL" sky.decodedBytes)"     "3145732"
# T8 (2026-10-16): the six-way pairing, on real content. Six vmts and six
# textures do not say which one is on which side of the box, and two suffixes
# trading places would leave every number above untouched. Measured on this
# install 2026-10-16: sky_well_01's centre pixels give four distinct values
# (rt 107, lf/bk/ft 104, up 89, dn 29 -- dn is the 1x1 stub this map ships).
for f in 0 1 2 3 4 5; do
  assert_eq "sky face $f suffix" "$(field "$REAL" sky.face$f.suffix)" \
    "$(printf '%s' "rt lf bk ft up dn" | cut -d' ' -f$((f+1)))"
  assert_eq "sky face $f direction" "$(field "$REAL" sky.face$f.dir)" \
    "$(printf '%s' "+X -X +Y -Y +Z -Z" | cut -d' ' -f$((f+1)))"
  assert_eq "sky face $f vmt" "$(field "$REAL" sky.face$f.vmt)" \
    "materials/skybox/sky_well_01$(printf '%s' "rt lf bk ft up dn" | cut -d' ' -f$((f+1))).vmt"
  assert_eq "sky face $f decoded" "$(field "$REAL" sky.face$f.decoded)" "true"
done
assert_eq "sky face 0 pixel" "$(field "$REAL" sky.face0.rgba)" "107,95,121,255"
assert_eq "sky face 4 pixel" "$(field "$REAL" sky.face4.rgba)" "89,91,137,255"
assert_eq "sky face 5 pixel" "$(field "$REAL" sky.face5.rgba)" "29,57,90,255"
assert_eq "displacement faces"    "$(field "$REAL" displacement.faces)"    "1316"
assert_eq "displacement triangles" "$(field "$REAL" displacement.triangles)" "165376"
assert_eq "displacement corners"  "$(field "$REAL" displacement.cornerMatches)" "1316"
assert_eq "water faces"           "$(field "$REAL" water.faces)"           "18"
assert_eq "water triangles"       "$(field "$REAL" water.trianglesTagged)" "36"
assert_eq "vis mode"              "$(field "$REAL" vis.mode)"              "cluster-pvs"
assert_eq "vis clusters"          "$(field "$REAL" vis.clusters)"          "2940"
assert_eq "vis visible from 0"    "$(field "$REAL" vis.visibleFromCluster0)" "384"

echo
echo "=== 5/8 the negative control: no BSP, so nothing is available ==="
# Section 4 pins numbers that only hold for a map that is installed. This is
# about the map that is not, and it is the reading that would catch a gate
# passing on constants: with no BSP every capability must say unavailable, and
# the reason must name the cause rather than repeat a label.
BAGEL=$("$EXE" --bsp "$TF/maps/koth_bagel_rc13.bsp" --tf-root "$TF" --map-name koth_bagel_rc13 2>&1)
echo "$BAGEL" > "$OUT/negative-bagel.txt"
if [ ! -e "$TF/maps/koth_bagel_rc13.bsp" ]; then
  echo "  OK   koth_bagel_rc13.bsp is not installed here, so the run reads an empty map"
else
  echo "  NOTE koth_bagel_rc13.bsp IS installed now; this control no longer tests the unreachable case"
fi
assert_eq "bagel lightmap"     "$(field "$BAGEL" lightmap.state)"     "unavailable"
assert_eq "bagel cubemap"      "$(field "$BAGEL" cubemap.state)"      "unavailable"
assert_eq "bagel sky"          "$(field "$BAGEL" sky.state)"          "unavailable"
assert_eq "bagel displacement" "$(field "$BAGEL" displacement.state)" "unavailable"
assert_eq "bagel water"        "$(field "$BAGEL" water.state)"        "unavailable"
assert_eq "bagel vis"          "$(field "$BAGEL" vis.state)"          "unavailable"
assert_eq "bagel lightmap reason" "$(field "$BAGEL" lightmap.reason)" "the bsp declares no lightmap faces"

echo
echo "=== 6/8 the render side lights what the parse side measured ==="
# Sections 1-6 all read the BSP through the probe. This one runs the product
# itself and reads the GPU-side pair back out of its trace, because the defect
# this section exists for lived exactly there: uploadWorldGeometry's atlas
# overload wrote the per-face light uv only for faces that hit the 64-tile
# texture atlas, so on this map 30731 of 199263 lightmapped triangles were
# drawn flat while lightmap.state still read active and the atlas was bound.
# Neither the probe nor the status enum could see it; only the vertex pair can.
#
# The load phase (VPK index, demo scan, model uploads) measured ~30 s on this
# machine, so 300 s is a generous ceiling rather than the norm.
PRODUCT_TRACE="$SCRATCH/render-lightmap.$RUN_TAG.trace"
PRODUCT_BMP="$SCRATCH/render-lightmap.$RUN_TAG.bmp"
PRODUCT_CSV="$SCRATCH/render-lightmap.$RUN_TAG.csv"
PRODUCT_LOG="$SCRATCH/render-lightmap.$RUN_TAG.log"
rm -f "$PRODUCT_TRACE" "$PRODUCT_BMP" "$PRODUCT_CSV" "$PRODUCT_LOG"
# The trace path is read with GetEnvironmentVariableW and opened by the program,
# so it needs the Windows form; cygpath -m gives it without the doubled
# backslashes the MSYS layer rewrites. --hidden-window and --audio-disabled keep
# the run quiet, exactly as quiet-capture-check.sh established they may.
TRACE_WIN="$(cygpath -m "$PWD/$PRODUCT_TRACE")"
PRODUCT_RC=$(TF2_NATIVE_TRACE="$TRACE_WIN" timeout 300 "$PRODUCT" --tf-root "$TF" \
  --audio-disabled --hidden-window --demo "$DEMO" --world-sky --capture-tick 0 \
  --capture-frame "$PRODUCT_BMP" --metrics-file "$PRODUCT_CSV" > "$PRODUCT_LOG" 2>&1; echo $?)
echo "product run rc=$PRODUCT_RC trace=$(stat -c%s "$PRODUCT_TRACE" 2>/dev/null || echo 0)"
assert_eq "product run rc" "$PRODUCT_RC" "0"
RENDER=$(grep -o "gpu: world lightmap .*" "$PRODUCT_TRACE" 2>/dev/null | head -1)
if [ -n "$RENDER" ]; then
  echo "$RENDER" > "$OUT/render-lightmap.txt"
else
  echo "  FAIL the render run produced no lightmap reading, so $OUT/render-lightmap.txt is left as it was"; fail=1
fi
rfield() { # rfield <key> -> number out of the render reading line
  printf '%s\n' "$RENDER" | grep -oE "$1=[0-9]+" | head -1 | cut -d= -f2
}
RENDER_STATUS=$(printf '%s\n' "$RENDER" | grep -oE "status=[a-z-]+" | head -1 | cut -d= -f2)
REAL_TRIS=$(field "$REAL" lightmap.trianglesWithLightUv)
assert_eq "render lightmap status" "$RENDER_STATUS" "active"
assert_eq "render atlas bound" "$(rfield atlas)" "1"
assert_eq "render vertices" "$(rfield vertices)" "600000"
# The identity, not a second pin: the GPU-side count must equal three times the
# parse-side triangle count from section 4. If the parse side changes, this
# follows it; if the renderer drops a face's light uv again, it does not.
assert_eq "render lightmapped = 3 x parse triangles" "$(rfield lightmapped)" "$((REAL_TRIS * 3))"
# And the map is not fully lit: 737 of its 200000 triangles carry no lightmap
# face, so lightmapped must stay strictly below vertices. A reading of
# lightmapped == vertices would mean unlit faces were being handed a uv.
# The displacement and water counts, taken while the vertex buffer was filled
# rather than from the parse: the two must agree, and if they do not, the
# renderer drew something other than what the capability layer measured.
GEOMETRY=$(grep -o "gpu: world geometry .*" "$PRODUCT_TRACE" 2>/dev/null | head -1)
if [ -n "$GEOMETRY" ]; then
  echo "$GEOMETRY" >> "$OUT/render-lightmap.txt"
else
  echo "  FAIL the render run produced no geometry reading, so it is not appended to $OUT/render-lightmap.txt"; fail=1
fi
gfield() { printf '%s\n' "$GEOMETRY" | grep -oE "$1=[0-9]+" | head -1 | cut -d= -f2; }
assert_eq "render displaced = parse displacement triangles" \
  "$(gfield displacedTriangles)" "$(field "$REAL" displacement.triangles)"
assert_eq "render water = parse water triangles" \
  "$(gfield waterTriangles)" "$(field "$REAL" water.trianglesTagged)"
# The VIS capability, as an applied clip rather than as an absence. Until this
# round the renderer uploaded the map as one buffer and tested no triangle
# against the pvs, and `clipApplied` was the literal 0 printed by main.cpp --
# a fact about the code, not about the map. The pass now draws an index buffer
# built by clipWorldTrianglesByVis from the observer's own cluster, so these are
# the numbers of the frame that was drawn.
VIS=$(grep -o "gpu: world vis .*" "$PRODUCT_TRACE" 2>/dev/null | head -1)
echo "$VIS" >> "$OUT/render-lightmap.txt"
vfield() { printf '%s\n' "$VIS" | grep -oE "$1=[0-9-]+" | head -1 | cut -d= -f2; }
# T10 moved these four pins: the cluster used to be computed from the
# observer's NORMALIZED point fed to clusterAt as map coordinates, which on
# snakewater answered 2287 for every camera -- the pin was asserting the bug.
# With Renderer::observerFocusMapWorld feeding the clip the map-space eye, the
# camera reads its real leaf (639), the pvs answers 611 visible clusters, and
# the hidden/drawn split moves accordingly (29609/170391 of 200000).
assert_eq "render vis camera cluster" "$(vfield cameraCluster)" "639"
assert_eq "render vis visible from camera" "$(vfield visibleFromCamera)" "611"
assert_eq "render vis clustered triangles" "$(vfield clusteredTriangles)" "34517"
assert_eq "render vis hidden triangles" "$(vfield hiddenTriangles)" "29609"
assert_eq "render vis clip applied" "$(vfield clipApplied)" "1"
assert_eq "render vis clip state" "$(printf '%s\n' "$VIS" | grep -oE 'state=[a-z]+' | head -1 | cut -d= -f2)" "active"
assert_eq "render vis input triangles" "$(vfield inputTriangles)" "200000"
assert_eq "render vis drawn triangles" "$(vfield drawnTriangles)" "170391"
# The identity, not a second pin: what the draw call was given plus what the pvs
# withheld has to be the whole map. A clip that hid triangles by not uploading
# them would satisfy the two numbers above and fail this one.
if [ "$(( $(vfield drawnTriangles) + $(vfield hiddenTriangles) ))" \
    -eq "$(( $(vfield inputTriangles) ))" ] 2>/dev/null; then
  echo "  OK   render vis drawn + hidden = input ($(vfield drawnTriangles) + $(vfield hiddenTriangles) = $(vfield inputTriangles))"
else
  echo "  FAIL render vis drawn + hidden != input ($(vfield drawnTriangles) + $(vfield hiddenTriangles) != $(vfield inputTriangles))"; fail=1
fi
# The skybox, as geometry. `skyboxVertices: 0` was the honest reading before this
# round and the state word said "active" anyway, because active meant "six
# textures decoded" -- the files, not the drawing. 36 vertices is a box.
SKY=$(grep -o "gpu: world sky .*" "$PRODUCT_TRACE" 2>/dev/null | head -1)
echo "$SKY" >> "$OUT/render-lightmap.txt"
sfield() { printf '%s\n' "$SKY" | grep -oE "$1=[0-9]+" | head -1 | cut -d= -f2; }
assert_eq "render sky state" "$(printf '%s\n' "$SKY" | grep -oE 'state=[a-z]+' | head -1 | cut -d= -f2)" "active"
assert_eq "render sky vertices" "$(sfield vertices)" "36"
assert_eq "render sky triangles" "$(sfield triangles)" "12"
assert_eq "render sky faces" "$(sfield faces)" "6"
assert_eq "render sky draws" "$(sfield draws)" "1"
assert_eq "render sky cube side" "$(sfield side)" "512"
RENDER_LIGHTMAPPED=$(rfield lightmapped)
RENDER_VERTICES=$(rfield vertices)
if [ "$RENDER_LIGHTMAPPED" -lt "$RENDER_VERTICES" ] 2>/dev/null; then
  echo "  OK   render lightmapped < vertices ($RENDER_LIGHTMAPPED < $RENDER_VERTICES)"
else
  echo "  FAIL render lightmapped >= vertices ($RENDER_LIGHTMAPPED >= $RENDER_VERTICES)"; fail=1
fi

echo "=== 7/8 the product builds the map's cubemap and the env pass selects it ==="
# The cube is built from the map's own pakfile rather than from anything the tf
# tree or the VPKs hold, so this is the one place the whole chain -- pakfile
# entry, VTF face layout, cube texture, shader branch -- runs end to end. Two
# runs of the same map with the same preset: the first with the material's own
# answer (no $envmap anywhere in this install's first materials, measured across
# all 68 maps), the second with --world-env-force, which is the diagnostic that
# makes the six-face branch executable at all.
#
# 2koth_abbey is the map because it is the product's no-demo default and its
# pakfile is LZMA-compressed, which is the other storage method this install
# uses: cp_snakewater_final1 is stored, abbey is deflated-not-stored, and a
# reader that handled only one of them would read one map and call the
# capability measured.
ENV_TRACE="$SCRATCH/render-cubemap-plain.$RUN_TAG.trace"
ENV_BMP="$SCRATCH/render-cubemap-plain.$RUN_TAG.bmp"
ENV_FORCED_TRACE="$SCRATCH/render-cubemap-forced.$RUN_TAG.trace"
ENV_FORCED_BMP="$SCRATCH/render-cubemap-forced.$RUN_TAG.bmp"
rm -f "$ENV_TRACE" "$ENV_BMP" "$ENV_FORCED_TRACE" "$ENV_FORCED_BMP"
ENV_TRACE_WIN="$(cygpath -m "$PWD/$ENV_TRACE")"
ENV_BMP_WIN="$(cygpath -m "$PWD/$ENV_BMP")"
ENV_FORCED_TRACE_WIN="$(cygpath -m "$PWD/$ENV_FORCED_TRACE")"
ENV_FORCED_BMP_WIN="$(cygpath -m "$PWD/$ENV_FORCED_BMP")"
ENV_RC=$(TF2_NATIVE_TRACE="$ENV_TRACE_WIN" timeout 300 "$PRODUCT" --tf-root "$TF" \
  --quality standard --audio-disabled --hidden-window --capture-tick 0 \
  --capture-frame "$ENV_BMP_WIN" --metrics-file "$SCRATCH/render-cubemap-plain.csv" > "$SCRATCH/render-cubemap-plain.log" 2>&1; echo $?)
ENV_FORCED_RC=$(TF2_NATIVE_TRACE="$ENV_FORCED_TRACE_WIN" timeout 300 "$PRODUCT" --tf-root "$TF" \
  --quality standard --world-env-force --audio-disabled --hidden-window --capture-tick 0 \
  --capture-frame "$ENV_FORCED_BMP_WIN" --metrics-file "$SCRATCH/render-cubemap-forced.csv" > "$SCRATCH/render-cubemap-forced.log" 2>&1; echo $?)
assert_eq "cubemap run rc" "$ENV_RC" "0"
assert_eq "cubemap forced run rc" "$ENV_FORCED_RC" "0"
ENV_LINE=$(grep -o "gpu: world cubemap .*" "$ENV_TRACE" 2>/dev/null | head -1)
ENV_FORCED_LINE=$(grep -o "gpu: world cubemap .*" "$ENV_FORCED_TRACE" 2>/dev/null | head -1)
if [ -n "$ENV_LINE" ] && [ -n "$ENV_FORCED_LINE" ]; then
  echo "$ENV_LINE" > "$OUT/render-cubemap.txt"
  echo "$ENV_FORCED_LINE" >> "$OUT/render-cubemap.txt"
else
  echo "  FAIL the cubemap runs produced no reading, so $OUT/render-cubemap.txt is left as it was"; fail=1
fi
efield() { # efield <line> <key> -> value; slashes are in the class for paths
  printf '%s\n' "$1" | grep -oE "$2=[a-zA-Z0-9_./-]+" | head -1 | cut -d= -f2
}
assert_eq "cubemap status" "$(efield "$ENV_LINE" status)" "six-face"
assert_eq "cubemap faces" "$(efield "$ENV_LINE" faces)" "6"
assert_eq "cubemap size" "$(efield "$ENV_LINE" size)" "32"
assert_eq "cubemap source" "$(efield "$ENV_LINE" source)" "materials/maps/2koth_abbey/c1316_-1018_238.vtf"
assert_eq "cubemap reason" "$(efield "$ENV_LINE" reason)" "all"
# The material's own answer, and the forced one. mode 0 here is the honest
# reading of a map whose first material declares no $envmap; mode 2 there is the
# cube being selected. A gate that only pinned the forced value could not tell
# whether the product's own choice was working.
assert_eq "cubemap plain mode" "$(efield "$ENV_LINE" mode)" "0"
assert_eq "cubemap plain declared" "$(efield "$ENV_LINE" declared)" "0"
assert_eq "cubemap forced mode" "$(efield "$ENV_FORCED_LINE" mode)" "2"
assert_eq "cubemap forced declared" "$(efield "$ENV_FORCED_LINE" declared)" "1"
# And the branch runs: with the mode selected the frame must move. This is
# supporting evidence, not the check -- the check is the mode above, which is
# read from the renderer. But a mode that changed nothing would mean the shader
# branch was dead, and a dead branch is what the wider constant buffer fixed:
# the buffer was 128 bytes while the struct grew to 144, so envFeatures read as
# zero and the two runs were byte-identical.
ENV_DIFF=0
if [ -f "$ENV_BMP" ] && [ -f "$ENV_FORCED_BMP" ]; then
  ENV_DIFF=$(cmp -l "$ENV_BMP" "$ENV_FORCED_BMP" 2>/dev/null | wc -l)
fi
if [ "$ENV_DIFF" -gt 1000 ] 2>/dev/null; then
  echo "  OK   the forced env mode moved the frame ($ENV_DIFF bytes differ)"
else
  echo "  FAIL the forced env mode left the frame unchanged ($ENV_DIFF bytes)"; fail=1
fi

echo
echo "=== 8/8 the two switches, off: the sky is not drawn and the clip does not run ==="
# Every assertion above is about a capability being on, and a gate that can only
# see the on state cannot tell an applied clip from a constant. So the same map
# and the same demo are run again with both switches off: the sky must report
# that it was not drawn (with the reason that says the caller turned it off,
# which is a different fact from "this map has no sky"), and the clip must draw
# the whole buffer. The pair of drawn counts is the evidence that the switch
# does something.
OFF_TRACE="$SCRATCH/render-off.$RUN_TAG.trace"
OFF_BMP="$SCRATCH/render-off.$RUN_TAG.bmp"
rm -f "$OFF_TRACE" "$OFF_BMP"
OFF_TRACE_WIN="$(cygpath -m "$PWD/$OFF_TRACE")"
OFF_BMP_WIN="$(cygpath -m "$PWD/$OFF_BMP")"
OFF_RC=$(TF2_NATIVE_TRACE="$OFF_TRACE_WIN" timeout 300 "$PRODUCT" --tf-root "$TF" \
  --audio-disabled --hidden-window --demo "$DEMO" --no-world-sky --no-world-vis-clip \
  --capture-tick 0 --capture-frame "$OFF_BMP_WIN" \
  --metrics-file "$SCRATCH/render-off.$RUN_TAG.csv" > "$SCRATCH/render-off.$RUN_TAG.log" 2>&1; echo $?)
assert_eq "switches-off run rc" "$OFF_RC" "0"
OFF_SKY=$(grep -o "gpu: world sky .*" "$OFF_TRACE" 2>/dev/null | head -1)
OFF_VIS=$(grep -o "gpu: world vis .*" "$OFF_TRACE" 2>/dev/null | head -1)
if [ -n "$OFF_SKY" ] && [ -n "$OFF_VIS" ]; then
  echo "$OFF_SKY" > "$OUT/render-off.txt"
  echo "$OFF_VIS" >> "$OUT/render-off.txt"
else
  # The run tag makes an empty here mean THIS run produced nothing, not that
  # another run did. Writing the empty line over the committed file would
  # destroy the evidence of a run that succeeded.
  echo "  FAIL the switches-off run produced no reading, so $OUT/render-off.txt is left as it was"; fail=1
fi
ofield() { printf '%s\n' "$OFF_VIS" | grep -oE "$1=[0-9-]+" | head -1 | cut -d= -f2; }
osky() { printf '%s\n' "$OFF_SKY" | grep -oE "$1=[0-9]+" | head -1 | cut -d= -f2; }
assert_eq "sky off state" "$(printf '%s\n' "$OFF_SKY" | grep -oE 'state=[a-z]+' | head -1 | cut -d= -f2)" "unavailable"
assert_eq "sky off vertices" "$(osky vertices)" "0"
assert_eq "sky off draws" "$(osky draws)" "0"
if case "$OFF_SKY" in *"disabled by the caller"*) true ;; *) false ;; esac; then
  echo "  OK   sky off reason names the caller's switch"
else
  echo "  FAIL sky off reason does not name the caller's switch"; fail=1
fi
assert_eq "clip off state" "$(printf '%s\n' "$OFF_VIS" | grep -oE 'state=[a-z]+' | head -1 | cut -d= -f2)" "disabled"
assert_eq "clip off applied" "$(ofield clipApplied)" "0"
assert_eq "clip off drawn = uploaded" "$(ofield drawnTriangles)" "$(( $(vfield inputTriangles) ))"
ON_DRAWN=$(vfield drawnTriangles)
OFF_DRAWN=$(ofield drawnTriangles)
if [ "$ON_DRAWN" -lt "$OFF_DRAWN" ] 2>/dev/null; then
  echo "  OK   the clip changes the geometry the world pass is given ($ON_DRAWN < $OFF_DRAWN)"
else
  echo "  FAIL the clip left the drawn geometry unchanged ($ON_DRAWN vs $OFF_DRAWN)"; fail=1
fi

echo
if [ "$MUTATION" -eq 1 ]; then
  # The cheap half: wrong expectations, one per reading that this round changed
  # or added. Each of these values is one the run must NOT produce; if one of
  # them matches, the assertion that reads it is a constant.
  echo "=== mutation: wrong expectations first, then two product mutations ==="
  if [ "$(field "$REAL" lightmap.facesDropped)" = "15" ]; then
    echo "  FAIL the packer drop matched the pre-fix value it should not have"
    exit 1
  fi
  if [ "$(field "$REAL" lightmap.atlas)" = "2048x2047" ]; then
    echo "  FAIL the atlas matched the pre-fix size it should not have"
    exit 1
  fi
  if [ "$(field "$REAL" sky.state)" = "unavailable" ]; then
    echo "  FAIL the sky state matched a value it should not have"
    exit 1
  fi
  # And the cubemap mode, the third one: the pre-force value is 0, and a gate
  # that pinned the single sample it happened to see would call that a pass.
  if [ "$(efield "$ENV_FORCED_LINE" mode)" = "0" ]; then
    echo "  FAIL the forced cubemap mode matched the value it replaced"
    exit 1
  fi
  # And the fixture half: demand a mutation count that no run can produce.
  if [ "$(count "$MUT" mutations)" = "99" ]; then
    echo "  FAIL the mutation count matched a value it should not have"
    exit 1
  fi
  # And the render half: the pre-fix vertex count. Before the else branch of
  # uploadWorldGeometry's atlas overload was fixed, the GPU side reported
  # 92193 lightmapped vertices -- the 30731 triangles that happened to hit the
  # 64-tile atlas. Expecting that value now must go red, which is what makes
  # the section 6 identity a check rather than a description.
  if [ "${RENDER_LIGHTMAPPED:-0}" = "92193" ]; then
    echo "  FAIL the render lightmapped count matched the pre-fix value it should not have"
    exit 1
  fi
  # T1: the two readings this round is about. `skyboxVertices: 0` and
  # `clipApplied: 0` were the honest values before the geometry was built and
  # before the clip ran; both must now be wrong, on the same lines the product
  # prints and the gate reads.
  if [ "$(sfield vertices)" = "0" ] || [ "$(sfield draws)" = "0" ]; then
    echo "  FAIL the sky draw matched the pre-fix zero it should not have"
    exit 1
  fi
  if [ "$(vfield clipApplied)" = "0" ]; then
    echo "  FAIL the clip reported itself unapplied"
    exit 1
  fi
  if [ "$(ofield drawnTriangles)" != "$(( $(vfield inputTriangles) ))" ]; then
    echo "  FAIL the switches-off run did not draw the whole buffer"
    exit 1
  fi
  # T8: the per-face sky pairing. "up" on the +Z face is the claim; "dn" there is
  # the swapped table this section's second product mutation installs, and the
  # pixel is dn's, so both readings have to refuse the values they would carry
  # under that swap. A gate that pinned only the suffixes would still be reading
  # the table it is checking.
  if [ "$(field "$REAL" sky.face4.suffix)" = "dn" ]; then
    echo "  FAIL the +Z face matched the swapped suffix it should not have"
    exit 1
  fi
  if [ "$(field "$REAL" sky.face5.suffix)" = "up" ]; then
    echo "  FAIL the -Z face matched the swapped suffix it should not have"
    exit 1
  fi
  if [ "$(field "$REAL" sky.face4.rgba)" = "29,57,90,255" ]; then
    echo "  FAIL the +Z face sampled the down texture's pixel"
    exit 1
  fi
  if [ "$(field "$REAL" sky.face0.dir)" = "-" ] || [ "$(field "$REAL" sky.face0.suffix)" = "-" ]; then
    echo "  FAIL the sky-face pairing fell back to a placeholder"
    exit 1
  fi

  # ---- Product mutation 1: the sky draw deleted ---------------------------
  # Not a perturbed string but the product itself: the condition that guards the
  # sky pass is ANDed with false, so the box is uploaded, the state reads Active
  # and the draw never happens. That is exactly the shape of the defect this
  # round fixes (a capability that reports itself while nothing is drawn), and
  # the reading that catches it has to be the draw count, not the state word --
  # so this check demands the mutated run report draws=0 AND state=active. A
  # mutation that deleted the upload instead would go red on the state too and
  # would not prove the count is what the gate is reading.
  #
  # The first attempt at this section forgot to point the product at a trace
  # file, and the empty trace made the sky check "pass" for the wrong reason --
  # the same class of defect as the capability that reports itself. Hence
  # run_mutated_product below: no trace, no conclusion.
  run_mutated_product() { # run_mutated_product <name> -> sets MUT_RC and MUT_TRACE
    local name="$1"
    MUT_TRACE="$SCRATCH/$name.$RUN_TAG.trace"
    rm -f "$MUT_TRACE" "$SCRATCH/$name.$RUN_TAG.bmp" "$SCRATCH/$name.$RUN_TAG.csv"
    TF2_NATIVE_TRACE="$(cygpath -m "$PWD/$MUT_TRACE")" \
      timeout 300 "$PRODUCT" --tf-root "$TF" --audio-disabled --hidden-window --demo "$DEMO" \
      --world-sky --capture-tick 0 --capture-frame "$SCRATCH/$name.$RUN_TAG.bmp" \
      --metrics-file "$SCRATCH/$name.$RUN_TAG.csv" > "$SCRATCH/$name.$RUN_TAG.log" 2>&1
    MUT_RC=$?
    if [ "$MUT_RC" != "0" ] || [ ! -s "$MUT_TRACE" ]; then
      echo "  MUTATION-SETUP=FAIL ($name: rc=$MUT_RC trace=$(stat -c%s "$MUT_TRACE" 2>/dev/null || echo missing))"
      return 1
    fi
    return 0
  }

  SKY_BACKUP="$SCRATCH/native_renderer.cpp.sky.backup"
  cp native/src/native_renderer.cpp "$SKY_BACKUP"
  sed -i 's|const bool hasSkyBox = worldSkyEnabled_ \&\& worldSkyVertexBuffer_|const bool hasSkyBox = false \&\& worldSkyEnabled_ \&\& worldSkyVertexBuffer_|' \
    native/src/native_renderer.cpp
  if cmp -s native/src/native_renderer.cpp "$SKY_BACKUP"; then
    echo "  MUTATION-SETUP=FAIL (sky_draw_deleted: the sed changed nothing)"
    cp "$SKY_BACKUP" native/src/native_renderer.cpp
    echo "WORLD-MATERIAL-MUTATION=FAIL"; exit 1
  fi
  if ! bash build-target.sh tf2_demo_native > "$SCRATCH/mutated-sky_draw_deleted.$RUN_TAG.build" 2>&1; then
    echo "  MUTATION-SETUP=FAIL (sky_draw_deleted: the rebuild failed)"
    cp "$SKY_BACKUP" native/src/native_renderer.cpp
    bash build-target.sh tf2_demo_native > "$SCRATCH/restore-sky.$RUN_TAG.build" 2>&1
    echo "WORLD-MATERIAL-MUTATION=FAIL"; exit 1
  fi
  if ! run_mutated_product mutated-sky_draw_deleted; then
    cp "$SKY_BACKUP" native/src/native_renderer.cpp
    bash build-target.sh tf2_demo_native > "$SCRATCH/restore-sky.$RUN_TAG.build" 2>&1
    echo "WORLD-MATERIAL-MUTATION=FAIL"; exit 1
  fi
  MUT_SKY=$(grep -o "gpu: world sky .*" "$MUT_TRACE" 2>/dev/null | head -1)
  MUT_SKY_DRAWN=$(printf '%s\n' "$MUT_SKY" | grep -oE "draws=[0-9]+" | head -1 | cut -d= -f2)
  MUT_SKY_STATE=$(printf '%s\n' "$MUT_SKY" | grep -oE "state=[a-z]+" | head -1 | cut -d= -f2)
  if [ "$MUT_SKY_DRAWN" = "0" ] && [ "$MUT_SKY_STATE" = "active" ]; then
    echo "  MUTATION-CAUGHT=PASS (sky_draw_deleted: draws=0 while the state still reads active)"
  else
    echo "  MUTATION-CAUGHT=FAIL (sky_draw_deleted: draws=$MUT_SKY_DRAWN state=$MUT_SKY_STATE, the gate would have stayed green)"
    cp "$SKY_BACKUP" native/src/native_renderer.cpp
    bash build-target.sh tf2_demo_native > "$SCRATCH/restore-sky.$RUN_TAG.build" 2>&1
    echo "WORLD-MATERIAL-MUTATION=FAIL"; exit 1
  fi
  echo "$MUT_SKY" > "$OUT/mutated-sky_draw_deleted.txt"
  cp "$SKY_BACKUP" native/src/native_renderer.cpp
  bash build-target.sh tf2_demo_native > "$SCRATCH/restore-sky.$RUN_TAG.build" 2>&1 \
    || { echo "  RESTORE-BUILD=FAIL (sky_draw_deleted)"; echo "WORLD-MATERIAL-MUTATION=FAIL"; exit 1; }
  if ! cmp -s native/src/native_renderer.cpp "$SKY_BACKUP"; then
    echo "  RESTORE=FAIL (sky_draw_deleted: the source did not come back byte-identical)"
    echo "WORLD-MATERIAL-MUTATION=FAIL"; exit 1
  fi
  # Only once the live source is back and identical: a backup left lying next to
  # the build is a second copy of a file that no longer has a job. The delete is
  # checked for the same reason the traces are -- `rm -f` can be refused on this
  # machine, and a refused delete of a source copy is worse than a refused
  # delete of a log.
  rm -f "$SKY_BACKUP"
  if [ -e "$SKY_BACKUP" ]; then
    echo "  RESTORE=FAIL (sky_draw_deleted: the source backup could not be removed: $SKY_BACKUP)"
    echo "WORLD-MATERIAL-MUTATION=FAIL"; exit 1
  fi

  # ---- Product mutation 2: the pvs clip deleted ---------------------------
  # applyWorldVisClip is made to always turn the clip off. The map still parses,
  # the pvs still answers, the renderer still draws -- the whole map. The gate's
  # section 8 pins exactly that state as the OFF case, so the reading that
  # catches this is the drawn count, and the mutation proves section 6 is not
  # reading a constant.
  CLIP_BACKUP="$SCRATCH/main.cpp.clip.backup"
  cp native/src/main.cpp "$CLIP_BACKUP"
  sed -i 's|const bool enabled = worldVisClipGiven ? worldVisClipRequested : true;|const bool enabled = false;|' \
    native/src/main.cpp
  if cmp -s native/src/main.cpp "$CLIP_BACKUP"; then
    echo "  MUTATION-SETUP=FAIL (vis_clip_deleted: the sed changed nothing)"
    cp "$CLIP_BACKUP" native/src/main.cpp
    echo "WORLD-MATERIAL-MUTATION=FAIL"; exit 1
  fi
  if ! bash build-target.sh tf2_demo_native > "$SCRATCH/mutated-vis_clip_deleted.$RUN_TAG.build" 2>&1; then
    echo "  MUTATION-SETUP=FAIL (vis_clip_deleted: the rebuild failed)"
    cp "$CLIP_BACKUP" native/src/main.cpp
    bash build-target.sh tf2_demo_native > "$SCRATCH/restore-clip.$RUN_TAG.build" 2>&1
    echo "WORLD-MATERIAL-MUTATION=FAIL"; exit 1
  fi
  if ! run_mutated_product mutated-vis_clip_deleted; then
    cp "$CLIP_BACKUP" native/src/main.cpp
    bash build-target.sh tf2_demo_native > "$SCRATCH/restore-clip.$RUN_TAG.build" 2>&1
    echo "WORLD-MATERIAL-MUTATION=FAIL"; exit 1
  fi
  MUT_CLIP=$(grep -o "gpu: world vis .*" "$MUT_TRACE" 2>/dev/null | head -1)
  MUT_APPLIED=$(printf '%s\n' "$MUT_CLIP" | grep -oE "clipApplied=[0-9]+" | head -1 | cut -d= -f2)
  MUT_DRAWN=$(printf '%s\n' "$MUT_CLIP" | grep -oE "drawnTriangles=[0-9]+" | head -1 | cut -d= -f2)
  MUT_STATE=$(printf '%s\n' "$MUT_CLIP" | grep -oE "state=[a-z]+" | head -1 | cut -d= -f2)
  if [ "$MUT_APPLIED" = "0" ] && [ "$MUT_DRAWN" = "$OFF_DRAWN" ] && [ "$MUT_STATE" = "disabled" ]; then
    echo "  MUTATION-CAUGHT=PASS (vis_clip_deleted: clipApplied=0, drawn=$MUT_DRAWN = the whole buffer)"
  else
    echo "  MUTATION-CAUGHT=FAIL (vis_clip_deleted: clipApplied=$MUT_APPLIED drawn=$MUT_DRAWN state=$MUT_STATE, the gate would have stayed green)"
    cp "$CLIP_BACKUP" native/src/main.cpp
    bash build-target.sh tf2_demo_native > "$SCRATCH/restore-clip.$RUN_TAG.build" 2>&1
    echo "WORLD-MATERIAL-MUTATION=FAIL"; exit 1
  fi
  echo "$MUT_CLIP" > "$OUT/mutated-vis_clip_deleted.txt"
  cp "$CLIP_BACKUP" native/src/main.cpp
  bash build-target.sh tf2_demo_native > "$SCRATCH/restore-clip.$RUN_TAG.build" 2>&1 \
    || { echo "  RESTORE-BUILD=FAIL (vis_clip_deleted)"; echo "WORLD-MATERIAL-MUTATION=FAIL"; exit 1; }
  if ! cmp -s native/src/main.cpp "$CLIP_BACKUP"; then
    echo "  RESTORE=FAIL (vis_clip_deleted: the source did not come back byte-identical)"
    echo "WORLD-MATERIAL-MUTATION=FAIL"; exit 1
  fi
  rm -f "$CLIP_BACKUP"
  if [ -e "$CLIP_BACKUP" ]; then
    echo "  RESTORE=FAIL (vis_clip_deleted: the source backup could not be removed: $CLIP_BACKUP)"
    echo "WORLD-MATERIAL-MUTATION=FAIL"; exit 1
  fi

  # The forward sections run BEFORE the mutations, and a FAIL printed by any of
  # them used to die here: the mutation branch exited 0 unconditionally, so
  # `bash world-material-check.sh --mutation` printed "FAIL switches-off run rc"
  # and still returned success. Measured 2026-10-14. The two mutations going
  # red is one verdict; the eight sections holding is another; both have to
  # hold for the mutation pass to be an acceptance.
  if [ "$fail" -ne 0 ]; then
    echo "  the mutation half caught its two cases, but a forward assertion above"
    echo "  did not hold -- a caught mutation over a red gate is not a pass"
    echo "WORLD-MATERIAL-MUTATION=FAIL"; exit 1
  fi
  echo "MUTATION-CAUGHT=PASS (six wrong expectations, the deleted sky draw and the"
  echo "  deleted pvs clip each go red; both sources came back byte-identical)"
  exit 0
fi

if [ "$fail" -eq 0 ]; then
  echo "WORLD-MATERIAL=PASS"
else
  echo "WORLD-MATERIAL=FAIL"
fi
exit "$fail"
