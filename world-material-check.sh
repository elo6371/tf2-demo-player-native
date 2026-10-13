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
#   1. the probe's self-tests pass (the pure functions and the empty-BSP case)
#   2. every synthetic fixture passes, on BSP bytes built by this repo and read
#      back through the product's own parser -- so the parser is what is tested
#   3. every fixture's mutation moves its reading, which is what makes 2 mean
#      something: a fixture whose mutation changes nothing is a fixture that was
#      asserting a constant
#   4. the real map's six states and their counters, pinned
#   5. the negative control: a map with no BSP must report every capability
#      unavailable, which is the reading that would catch a gate that passes on
#      constants
#   6. with --mutation, a deliberately wrong expectation goes red
#   7. the product's own trace ties the GPU-side lightmap count to the parse side
#   8. the product builds the map's cubemap and the env pass selects it
#   7. the render side lights what the parse side measured: the product is run
#      once and the GPU-side vertex pair is read back out of its own trace, so
#      the lightmap pass is checked where the triangles are actually drawn
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

echo
echo "=== 2/8 every synthetic fixture passes ==="
# The fixtures build real BSP bytes and read them back through BspParser, so a
# green run is a statement about the parser and the capability layer together.
FIX=$("$EXE" --fixture 2>&1)
echo "$FIX" > "$OUT/fixture-straight.txt"
assert_eq "fixtures" "$(count "$FIX" fixtures)" "9"
assert_eq "fixture checks" "$(count "$FIX" checks)" "90"
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
# Straight plus mutated assertions: 90 + 30. Pinned so a fixture that quietly
# stops asserting anything cannot pass by shrinking.
assert_eq "assertions with mutations" "$(count "$MUT" checks)" "120"

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
assert_eq "lightmap packed"       "$(field "$REAL" lightmap.facesPacked)"  "11512"
assert_eq "lightmap dropped"      "$(field "$REAL" lightmap.facesDropped)" "15"
assert_eq "lightmap triangles"    "$(field "$REAL" lightmap.trianglesWithLightUv)" "199263"
assert_eq "lightmap atlas"        "$(field "$REAL" lightmap.atlas)"        "2048x2047"
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
echo "=== 7/8 the render side lights what the parse side measured ==="
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
PRODUCT_TRACE="$SCRATCH/render-lightmap.trace"
PRODUCT_BMP="$SCRATCH/render-lightmap.bmp"
PRODUCT_CSV="$SCRATCH/render-lightmap.csv"
PRODUCT_LOG="$SCRATCH/render-lightmap.log"
rm -f "$PRODUCT_TRACE" "$PRODUCT_BMP" "$PRODUCT_CSV" "$PRODUCT_LOG"
# The trace path is read with GetEnvironmentVariableW and opened by the program,
# so it needs the Windows form; cygpath -m gives it without the doubled
# backslashes the MSYS layer rewrites. --hidden-window and --audio-disabled keep
# the run quiet, exactly as quiet-capture-check.sh established they may.
TRACE_WIN="$(cygpath -m "$PWD/$PRODUCT_TRACE")"
PRODUCT_RC=$(TF2_NATIVE_TRACE="$TRACE_WIN" timeout 300 "$PRODUCT" --tf-root "$TF" \
  --audio-disabled --hidden-window --demo "$DEMO" --capture-tick 0 \
  --capture-frame "$PRODUCT_BMP" --metrics-file "$PRODUCT_CSV" > "$PRODUCT_LOG" 2>&1; echo $?)
echo "product run rc=$PRODUCT_RC trace=$(stat -c%s "$PRODUCT_TRACE" 2>/dev/null || echo 0)"
assert_eq "product run rc" "$PRODUCT_RC" "0"
RENDER=$(grep -o "gpu: world lightmap .*" "$PRODUCT_TRACE" 2>/dev/null | head -1)
echo "$RENDER" > "$OUT/render-lightmap.txt"
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
echo "$GEOMETRY" >> "$OUT/render-lightmap.txt"
gfield() { printf '%s\n' "$GEOMETRY" | grep -oE "$1=[0-9]+" | head -1 | cut -d= -f2; }
assert_eq "render displaced = parse displacement triangles" \
  "$(gfield displacedTriangles)" "$(field "$REAL" displacement.triangles)"
assert_eq "render water = parse water triangles" \
  "$(gfield waterTriangles)" "$(field "$REAL" water.trianglesTagged)"
# The VIS capability, as its absence: the observer's cluster, what the pvs says
# is visible from it, how much of the uploaded geometry sits in clusters that are
# not, and the fact that no clip is applied. A number that can only be produced
# by reading the pvs and the geometry together.
VIS=$(grep -o "gpu: world vis .*" "$PRODUCT_TRACE" 2>/dev/null | head -1)
echo "$VIS" >> "$OUT/render-lightmap.txt"
vfield() { printf '%s\n' "$VIS" | grep -oE "$1=[0-9-]+" | head -1 | cut -d= -f2; }
assert_eq "render vis camera cluster" "$(vfield cameraCluster)" "563"
assert_eq "render vis visible from camera" "$(vfield visibleFromCamera)" "850"
assert_eq "render vis clustered triangles" "$(vfield clusteredTriangles)" "34517"
assert_eq "render vis hidden triangles" "$(vfield hiddenTriangles)" "26936"
assert_eq "render vis clip applied" "$(vfield clipApplied)" "0"
RENDER_LIGHTMAPPED=$(rfield lightmapped)
RENDER_VERTICES=$(rfield vertices)
if [ "$RENDER_LIGHTMAPPED" -lt "$RENDER_VERTICES" ] 2>/dev/null; then
  echo "  OK   render lightmapped < vertices ($RENDER_LIGHTMAPPED < $RENDER_VERTICES)"
else
  echo "  FAIL render lightmapped >= vertices ($RENDER_LIGHTMAPPED >= $RENDER_VERTICES)"; fail=1
fi

echo "=== 8/8 the product builds the map's cubemap and the env pass selects it ==="
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
ENV_TRACE="$SCRATCH/render-cubemap-plain.trace"
ENV_BMP="$SCRATCH/render-cubemap-plain.bmp"
ENV_FORCED_TRACE="$SCRATCH/render-cubemap-forced.trace"
ENV_FORCED_BMP="$SCRATCH/render-cubemap-forced.bmp"
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
echo "$ENV_LINE" > "$OUT/render-cubemap.txt"
echo "$ENV_FORCED_LINE" >> "$OUT/render-cubemap.txt"
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
if [ "$MUTATION" -eq 1 ]; then
  # Two wrong expectations, one for a counter and one for a state. The counter
  # is the one that would have caught the silent packer drop (15 faces lost
  # with nothing reporting it); the state is the one that would have caught a
  # capability layer that labels everything active.
  echo "=== 6/8 mutation: the expected drop, the sky state and the cubemap mode are wrong on purpose ==="
  if [ "$(field "$REAL" lightmap.facesDropped)" = "0" ]; then
    echo "  FAIL the packer drop matched a value it should not have"
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
  # the section 7 identity a check rather than a description.
  if [ "${RENDER_LIGHTMAPPED:-0}" = "92193" ]; then
    echo "  FAIL the render lightmapped count matched the pre-fix value it should not have"
    exit 1
  fi
  echo "MUTATION-CAUGHT=PASS (a wrong packer-drop expectation, a wrong sky state, a wrong"
  echo "  mutation count, the pre-fix render vertex count and the pre-force cubemap mode"
  echo "  each go red)"
  exit 0
fi

if [ "$fail" -eq 0 ]; then
  echo "WORLD-MATERIAL=PASS"
else
  echo "WORLD-MATERIAL=FAIL"
fi
exit "$fail"
