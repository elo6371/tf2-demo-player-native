#!/usr/bin/env bash
# viewmodel-material-check.sh -- the ViewModel material resource contract:
# MDL texture slot -> $cdmaterials -> .vmt -> $basetexture -> .vtf -> decoded
# pixels, for the three ViewModel model families, must resolve -- and every way
# it can fail must be reported rather than passed over.
#
# WHY THIS EXISTS
#   T2 draws the first-person ViewModel in flat colour and hard-codes
#   `materialResolved = 0` in viewmodel.cpp, because nothing binds a VMT yet.
#   That is the honest state, but it is also an unmeasured one: nobody had
#   established whether the chain the product will need resolves at all for
#   dynamic arms (c_sniper_arms), old-style v_* models (v_scattergun_scout) and
#   static weapon bodies (c_scattergun, c_pro_rifle). This gate measures it on
#   the real install and pins every failure class on a synthetic fixture.
#
# WHAT IS ASSERTED, AND WHY EACH ONE CAN FAIL
#   Section 1 -- provenance. The fixtures are regenerated and compared byte for
#     byte, so the bytes under test cannot quietly drift from the generator.
#     A fixture that only exists on one machine is not an input.
#   Section 2 -- the real install. All four named models, read through the
#     product's own AssetRoot (loose files, then the VPK archives): every slot's
#     VMT, VTF and decoded pixels, with the dimensions, formats and feature
#     declarations named. The backslash reading is asserted explicitly because
#     the shipped v_scattergun.vmt carries `models\weapons\...` in
#     $basetexture and a resolver that ate the backslash would resolve nowhere.
#     The 2048 boundary is asserted from both sides: c_scattergun is 2048 and
#     must NOT be oversized, the fixture is 4096 and must be.
#   Section 3 -- the fixtures. Ten defect classes, each with its own reading:
#     missing VMT, missing VTF, wrong $cdmaterials (with the control that the
#     same stem exists under the correct directory), backslash paths, BC3
#     decode, oversized texture, a bare stem with no candidate path at all, a
#     texture path that escapes the asset root, and a VTF that parses but
#     refuses to decode as 2D (the cubemap flag). The cubemap fixture is the
#     one input that separates "the texture was read" from "the texture
#     produced pixels" -- without it a probe claiming pixels for every
#     parseable VTF would pass everything else.
#   Section 4 -- workload counts. models, slots and per-class totals are
#     asserted from the summary line, so a run that compared nothing cannot
#     read as a pass.
#
# WHAT IT DOES NOT SAY
#   It does not say any pixel reached a screen. There is no window, no device
#   and no demo here; the product still flat-shades the ViewModel
#   (`materialResolved = 0` in viewmodel.cpp is untouched by this task).
#   Resource resolution and pixel decoding are established; the product draw
#   pass wiring is not, and no human has compared a picture.
#
# Usage: bash viewmodel-material-check.sh [--mutation [name]]
#   --mutation          runs all six mutations. Each patches one coherent
#                       defect -- five in the fixture bytes, one in the probe
#                       source -- and requires this script to go red on a
#                       *named* assertion (not merely to exit non-zero):
#                         vmt-deleted          the positive fixture's VMT is
#                                              removed
#                         vtf-deleted          the positive fixture's VTF is
#                                              removed
#                         basetexture-broken   $basetexture names a texture
#                                              that does not exist
#                         cdmaterials-broken   the MDL's $cdmaterials points
#                                              at the wrong directory
#                         texture-size-tampered the VTF's width word is
#                                              changed under the gate
#                         probe-fakes-decode   the probe claims decoded pixels
#                                              for a VTF it did not decode
#   --mutation <name>   runs one of them.
# Exit:  0 = every assertion held (or, with a mutation, the checks went red).
#
# One run at a time: the script takes .scratch/viewmodel-material.lock with
# mkdir, because the probe-fakes-decode mutation rewrites a tracked source and
# rebuilds, and two concurrent runs would overwrite each other's patches and
# each other's evidence. mkdir is atomic, so a second caller fails before it
# builds anything.
set -uo pipefail
cd "$(dirname "$0")"

PROBE=native/build-nmake/viewmodel_material_probe.exe
TF="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
FIXTURE_DIR="evidence/viewmodel/fixtures/vmfix-tf"
GEN="evidence/viewmodel/fixtures/viewmodel-material-fixture-gen.py"
# Frozen witnesses (readings, mutation-failure records) are committed. The
# regenerated fixture copies are machine output and live under evidence/fast,
# which is where the plan puts gate output that must not be mistaken for input.
OUT="${VIEWMODEL_MATERIAL_OUT:-evidence/viewmodel}"
TRACE_DIR="${VIEWMODEL_MATERIAL_TRACE_DIR:-evidence/fast/viewmodel-material}"
mkdir -p "$OUT" "$TRACE_DIR"

PY="C:/Users/Administrator/.workbuddy-ai/binaries/python/versions/3.13.12/python.exe"
[ -x "$PY" ] || { echo "FATAL: missing interpreter: $PY"; exit 1; }
# The fixture root is handed to a native Windows binary, which does not
# translate MSYS paths: an unconverted "/d/..." is opened literally and every
# fixture reads as missing. Convert once.
ROOT=$(cygpath -m "$(pwd)" 2>/dev/null || pwd)
case "$ROOT" in
  /[a-zA-Z]/*) echo "FATAL: could not convert '$ROOT' to a Windows path"; exit 1 ;;
esac
FIXTURE_ROOT="$ROOT/$FIXTURE_DIR"

MUTATION=""
case "${1:-}" in
  --mutation) MUTATION="${2:-all}" ;;
  "") ;;
  *) echo "FATAL: unknown option: $1"; exit 1 ;;
esac
ALL_MUTATIONS=(vmt-deleted vtf-deleted basetexture-broken cdmaterials-broken texture-size-tampered probe-fakes-decode)
MUTATIONS=("${ALL_MUTATIONS[@]}")
if [ -n "$MUTATION" ] && [ "$MUTATION" != "all" ]; then
  case " ${ALL_MUTATIONS[*]} " in
    *" $MUTATION "*) MUTATIONS=("$MUTATION") ;;
    *) echo "FATAL: unknown mutation: $MUTATION (expected one of: ${ALL_MUTATIONS[*]})"; exit 1 ;;
  esac
fi

mkdir -p .scratch
LOCK_DIR=.scratch/viewmodel-material.lock
if ! mkdir "$LOCK_DIR" 2>/dev/null; then
  echo "VIEWMODEL-MATERIAL=FAIL (another viewmodel-material-check run owns $LOCK_DIR)"
  exit 2
fi
cleanup_lock() { rmdir "$LOCK_DIR" 2>/dev/null || true; }
trap cleanup_lock EXIT

# Missing input is a failure, not a skip.
for path in "$PROBE" "$TF" "$FIXTURE_DIR" "$GEN"; do
  [ -e "$path" ] || { echo "FATAL: missing input: $path"; exit 1; }
done

fail=0
ASSERTIONS=0
FAIL_LINES=()
# Set by sections 2 and 3 for the readings file; the sections run in order and
# each probe run replaces PROBE_LINES, so the file reads these rather than
# whatever the last section happened to run.
REAL_LINES=""
FIXTURE_LINES=""
SECTIONS="1 2 3 4 5"
assert_eq() { # assert_eq <label> <actual> <expected>
  # ${2-<none>}, NOT ${2:-<none>}: the ':' form also fires on an empty-but-set
  # argument, and an empty string is a legitimate reading here -- the
  # c_pro_rifle model's $cdmaterials is "" and vmfix_bare's is "" too.
  if [ "${2-<none>}" = "$3" ]; then
    echo "  OK   $1 = $3"; ASSERTIONS=$((ASSERTIONS + 1))
  else
    echo "  FAIL $1 = ${2-<none>}, expected $3"; fail=1; FAIL_LINES+=("$1: ${2-<none>} != $3")
  fi
}
assert_ge() { # assert_ge <label> <actual> <minimum>
  if [ "${2:-x}" -ge "$3" ] 2>/dev/null; then
    echo "  OK   $1 = $2 (>= $3)"; ASSERTIONS=$((ASSERTIONS + 1))
  else
    echo "  FAIL $1 = ${2:-<none>}, expected >= $3"; fail=1; FAIL_LINES+=("$1: ${2:-<none>} < $3")
  fi
}
assert_contains() { # assert_contains <label> <haystack> <needle>
  case "${2:-}" in
    *"$3"*) echo "  OK   $1 contains $3"; ASSERTIONS=$((ASSERTIONS + 1)) ;;
    *) echo "  FAIL $1 = ${2:-<none>}, expected it to contain $3"; fail=1
       FAIL_LINES+=("$1: ${2:-<none>} does not contain $3") ;;
  esac
}

# jfield <json> <name> -> the value of "name":<value>, "" if absent. String
# values come back unquoted; a helper that returned the quotes would make every
# string assertion read FAIL for a reason about the helper.
jfield() {
  printf '%s\n' "$1" | tr -d '\r' | tr ',' '\n' \
    | sed -n "s/.*\"$2\":\([^,}]*\).*/\1/p" | head -1 \
    | sed 's/^"//; s/"$//'
}

# model_line <model-path> <lines> -> the JSON line for that model.
model_line() {
  printf '%s\n' "$2" | tr -d '\r' | grep -F "\"model\":\"$1\"" | head -1
}

# run_probe <stderr-file> <args...> -> sets PROBE_LINES (stdout) and PROBE_RC.
run_probe() {
  local errfile="$1"; shift
  local out
  out=$("$PROBE" "$@" 2>"$errfile")
  PROBE_RC=$?
  PROBE_LINES=$(printf '%s\n' "$out" | tr -d '\r')
}

# ===========================================================================
# Section 1 -- the fixtures are reproducible from the generator
# ===========================================================================
section1() {
echo "=== 1/5 the fixture bytes are reproducible ==="
local regen="$TRACE_DIR/fixture-regen"
rm -rf "$regen"
"$PY" "$GEN" "$regen" > "$TRACE_DIR/fixture-gen.txt" 2>&1
if diff -r --strip-trailing-cr "$regen" "$FIXTURE_DIR" > "$TRACE_DIR/fixture-regen.diff" 2>&1; then
  echo "  OK   the committed fixtures are identical to a fresh generation"
  ASSERTIONS=$((ASSERTIONS + 1))
else
  echo "  FAIL the committed fixtures differ from a fresh generation:"
  sed 's/^/       | /' "$TRACE_DIR/fixture-regen.diff" | head -10
  fail=1; FAIL_LINES+=("fixture provenance: the committed fixtures are not reproducible")
fi
}

# ===========================================================================
# Section 2 -- the real install: four models, every slot end to end
# ===========================================================================
# model @ material_count @ width @ height @ format @ has_alpha @ has_bumpmap @
# has_phong @ has_selfillum @ cdmaterials (the "|"-joined list) @ decoded_bytes
# The four families the task names: dynamic arms, old-style v_*, static weapon
# bodies (a stock one and a workshop skin). Readings taken 2026-10-12 from the
# installed VPKs; a game update that moves them is a red here, on purpose.
# The entry delimiter is '@', not '|', because the cdmaterials field itself
# joins its entries with '|' and one delimiter cannot serve both.
REAL_MODELS=(
  "models/weapons/c_models/c_sniper_arms.mdl@3@1024@512@13@false@true@true@false@models/player/sniper/|@8388608"
  "models/weapons/c_models/c_scattergun.mdl@2@2048@2048@15@true@false@true@false@models/weapons/c_models/c_scattergun/|@33554432"
  "models/weapons/v_models/v_scattergun_scout.mdl@3@1024@1024@15@true@false@true@false@/models/weapons/v_scattergun/|@12582912"
  "models/workshop/weapons/c_models/c_pro_rifle/c_pro_rifle.mdl@2@1024@1024@15@true@false@true@false@@8388608"
)
section2() {
echo
echo "=== 2/5 the real install: four ViewModel model families ==="
local args=() entry line
for entry in "${REAL_MODELS[@]}"; do
  args+=(--model "${entry%%@*}")
done
run_probe "$TRACE_DIR/real-stderr.txt" --tf-root "$TF" "${args[@]}"
assert_eq "real install probe rc (all four models complete)" "$PROBE_RC" "0"
# Kept for the readings file: the sections run in order, and a later section's
# probe run must not overwrite the real install's lines.
REAL_LINES="$PROBE_LINES"
for entry in "${REAL_MODELS[@]}"; do
  IFS='@' read -r model mc width height fmt alpha bumpmap phong selfillum cdmaterials decoded <<<"$entry"
  line=$(model_line "$model" "$PROBE_LINES")
  if [ -z "$line" ]; then
    echo "  FAIL real install $model produced no report line"; fail=1
    FAIL_LINES+=("real install $model: no report line")
    continue
  fi
  echo "  $model"
  echo "    $line"
  assert_eq "real $model resolution" "$(jfield "$line" resolution)" "found-vpk"
  assert_eq "real $model material_count" "$(jfield "$line" material_count)" "$mc"
  assert_eq "real $model vmt_resolved" "$(jfield "$line" vmt_resolved)" "$mc"
  assert_eq "real $model vtf_resolved" "$(jfield "$line" vtf_resolved)" "$mc"
  assert_eq "real $model pixels_decoded" "$(jfield "$line" pixels_decoded)" "$mc"
  assert_eq "real $model missing_materials" "$(jfield "$line" missing_materials)" "0"
  assert_eq "real $model missing_textures" "$(jfield "$line" missing_textures)" "0"
  assert_eq "real $model invalid_paths" "$(jfield "$line" invalid_paths)" "0"
  assert_eq "real $model oversized_textures" "$(jfield "$line" oversized_textures)" "0"
  assert_eq "real $model complete" "$(jfield "$line" complete)" "true"
  assert_eq "real $model cdmaterials" "$(jfield "$line" cdmaterials)" "$cdmaterials"
  assert_eq "real $model texture_width" "$(jfield "$line" texture_width)" "$width"
  assert_eq "real $model texture_height" "$(jfield "$line" texture_height)" "$height"
  assert_eq "real $model texture_format" "$(jfield "$line" texture_format)" "$fmt"
  assert_eq "real $model has_alpha" "$(jfield "$line" has_alpha)" "$alpha"
  assert_eq "real $model has_bumpmap" "$(jfield "$line" has_bumpmap)" "$bumpmap"
  assert_eq "real $model has_phong" "$(jfield "$line" has_phong)" "$phong"
  assert_eq "real $model has_selfillum" "$(jfield "$line" has_selfillum)" "$selfillum"
  assert_eq "real $model decoded_bytes" "$(jfield "$line" decoded_bytes)" "$decoded"
done
# Every slot came out of a VPK, not a loose file: the materials live in the
# archives, and a reading sourced from loose files would mean the resolver
# found something else.
assert_eq "real install slots read from a VPK" \
  "$(grep -ac 'vtf_source=vpk' "$TRACE_DIR/real-stderr.txt")" "10"
# The shipped v_scattergun.vmt stores its $basetexture with backslash
# separators. The parser must treat them as path separators (it does); a
# resolver that consumed them as escapes would resolve nowhere and this line
# is the only witness that the real asset took the backslash route.
assert_contains "real v_scattergun took the backslash \$basetexture" \
  "$(grep -a 'v_scattergun_scout' "$TRACE_DIR/real-stderr.txt" | grep -a 'slot=0 ')" \
  'base=models\weapons\v_scattergun\v_scattergun'
# The 2048 side is inside the cap, not at it: c_scattergun is exactly 2048 and
# must not be flagged. The fixture is 4096 and must be. Together the two say
# the boundary is where the constant says it is, from both sides.
assert_eq "real c_scattergun is exactly at the 2048 cap and not oversized" \
  "$(jfield "$(model_line "models/weapons/c_models/c_scattergun.mdl" "$PROBE_LINES")" texture_width)" "2048"
}

# ===========================================================================
# Section 3 -- the fixtures: every failure class has a reading
# ===========================================================================
section3() {
echo
echo "=== 3/5 the fixtures: ten defect classes ==="
run_probe "$TRACE_DIR/fixture-stderr.txt" --fixture-root "$FIXTURE_ROOT"
# The set contains broken fixtures by design, so the probe must refuse it.
assert_eq "fixture set probe rc (the set is not all-resolvable)" "$PROBE_RC" "1"
FIXTURE_LINES="$PROBE_LINES"
local line

echo "-- the positive control --"
line=$(model_line "models/weapons/v_models/vmfix_ok.mdl" "$PROBE_LINES")
assert_eq "fixture vmfix_ok material_count" "$(jfield "$line" material_count)" "1"
assert_eq "fixture vmfix_ok materialResolved" "$(jfield "$line" vmt_resolved)" "1"
assert_eq "fixture vmfix_ok textureResolved" "$(jfield "$line" vtf_resolved)" "1"
assert_eq "fixture vmfix_ok pixelsDecoded" "$(jfield "$line" pixels_decoded)" "1"
assert_eq "fixture vmfix_ok missing_materials" "$(jfield "$line" missing_materials)" "0"
assert_eq "fixture vmfix_ok missing_textures" "$(jfield "$line" missing_textures)" "0"
assert_eq "fixture vmfix_ok invalid_paths" "$(jfield "$line" invalid_paths)" "0"
assert_eq "fixture vmfix_ok oversized_textures" "$(jfield "$line" oversized_textures)" "0"
assert_eq "fixture vmfix_ok complete" "$(jfield "$line" complete)" "true"
assert_eq "fixture vmfix_ok texture_width" "$(jfield "$line" texture_width)" "64"
assert_eq "fixture vmfix_ok texture_height" "$(jfield "$line" texture_height)" "64"
assert_eq "fixture vmfix_ok texture_format" "$(jfield "$line" texture_format)" "13"
assert_eq "fixture vmfix_ok has_alpha" "$(jfield "$line" has_alpha)" "false"
assert_eq "fixture vmfix_ok has_bumpmap" "$(jfield "$line" has_bumpmap)" "true"
assert_eq "fixture vmfix_ok has_phong" "$(jfield "$line" has_phong)" "true"
assert_eq "fixture vmfix_ok has_selfillum" "$(jfield "$line" has_selfillum)" "false"

echo "-- the VMT is absent --"
line=$(model_line "models/weapons/v_models/vmfix_no_vmt.mdl" "$PROBE_LINES")
assert_eq "fixture vmfix_no_vmt materialResolved" "$(jfield "$line" vmt_resolved)" "0"
assert_eq "fixture vmfix_no_vmt pixelsDecoded" "$(jfield "$line" pixels_decoded)" "0"
assert_eq "fixture vmfix_no_vmt missing_materials" "$(jfield "$line" missing_materials)" "1"
assert_eq "fixture vmfix_no_vmt complete" "$(jfield "$line" complete)" "false"

echo "-- the VTF is absent --"
line=$(model_line "models/weapons/v_models/vmfix_no_vtf.mdl" "$PROBE_LINES")
assert_eq "fixture vmfix_no_vtf materialResolved" "$(jfield "$line" vmt_resolved)" "1"
assert_eq "fixture vmfix_no_vtf textureResolved" "$(jfield "$line" vtf_resolved)" "0"
assert_eq "fixture vmfix_no_vtf missing_textures" "$(jfield "$line" missing_textures)" "1"
assert_eq "fixture vmfix_no_vtf pixelsDecoded" "$(jfield "$line" pixels_decoded)" "0"
assert_eq "fixture vmfix_no_vtf complete" "$(jfield "$line" complete)" "false"

echo "-- \$cdmaterials points at the wrong directory --"
# The control for the assertion above: the stem exists under the correct
# directory (it is the positive fixture's own material), so this reading is
# about the directory rule and not about a missing file. A resolver that
# ignored $cdmaterials and guessed the model's own directory would find it.
line=$(model_line "models/weapons/v_models/vmfix_bad_cdmaterials.mdl" "$PROBE_LINES")
assert_eq "fixture vmfix_bad_cdmaterials cdmaterials" "$(jfield "$line" cdmaterials)" "models/wrong/dir/"
assert_eq "fixture vmfix_bad_cdmaterials materialResolved" "$(jfield "$line" vmt_resolved)" "0"
assert_eq "fixture vmfix_bad_cdmaterials missing_materials" "$(jfield "$line" missing_materials)" "1"
assert_eq "fixture vmfix_bad_cdmaterials complete" "$(jfield "$line" complete)" "false"

echo "-- the VMT's \$basetexture uses backslash separators --"
# The real v_scattergun shape. The material is one directory away from the
# model and the path reaches it through backslashes only.
line=$(model_line "models/weapons/v_models/vmfix_backslash.mdl" "$PROBE_LINES")
assert_eq "fixture vmfix_backslash materialResolved" "$(jfield "$line" vmt_resolved)" "1"
assert_eq "fixture vmfix_backslash textureResolved" "$(jfield "$line" vtf_resolved)" "1"
assert_eq "fixture vmfix_backslash pixelsDecoded" "$(jfield "$line" pixels_decoded)" "1"
assert_eq "fixture vmfix_backslash texture_width" "$(jfield "$line" texture_width)" "64"
assert_eq "fixture vmfix_backslash complete" "$(jfield "$line" complete)" "true"
assert_contains "fixture vmfix_backslash stderr names the backslash base" \
  "$(grep -a 'vmfix_backslash' "$TRACE_DIR/fixture-stderr.txt" | grep -a 'slot=0 ')" \
  'base=models\weapons\v_models\vmfix\vmfix_backslash'

echo "-- the BC3 decode sample --"
line=$(model_line "models/weapons/v_models/vmfix_dxt5.mdl" "$PROBE_LINES")
assert_eq "fixture vmfix_dxt5 texture_format" "$(jfield "$line" texture_format)" "15"
assert_eq "fixture vmfix_dxt5 pixelsDecoded" "$(jfield "$line" pixels_decoded)" "1"
assert_eq "fixture vmfix_dxt5 has_alpha" "$(jfield "$line" has_alpha)" "true"
assert_eq "fixture vmfix_dxt5 has_selfillum" "$(jfield "$line" has_selfillum)" "true"
assert_eq "fixture vmfix_dxt5 has_phong" "$(jfield "$line" has_phong)" "false"
assert_eq "fixture vmfix_dxt5 complete" "$(jfield "$line" complete)" "true"

echo "-- the texture exceeds the side cap --"
# The pixels ARE decoded; the cap is a policy, and an over-cap texture is
# reported rather than dropped or silently accepted.
line=$(model_line "models/weapons/v_models/vmfix_oversized.mdl" "$PROBE_LINES")
assert_eq "fixture vmfix_oversized textureResolved" "$(jfield "$line" vtf_resolved)" "1"
assert_eq "fixture vmfix_oversized pixelsDecoded" "$(jfield "$line" pixels_decoded)" "1"
assert_eq "fixture vmfix_oversized texture_width" "$(jfield "$line" texture_width)" "4096"
assert_eq "fixture vmfix_oversized oversized_textures" "$(jfield "$line" oversized_textures)" "1"
assert_eq "fixture vmfix_oversized complete" "$(jfield "$line" complete)" "false"
assert_contains "fixture vmfix_oversized reason names the cap" "$(jfield "$line" reason)" "2048"

echo "-- the slot has no candidate path at all --"
# A bare stem with a single empty $cdmaterials entry. The VMT and VTF exist
# under the correct directory; the path rule is what fails.
line=$(model_line "models/weapons/v_models/vmfix_bare.mdl" "$PROBE_LINES")
assert_eq "fixture vmfix_bare cdmaterials" "$(jfield "$line" cdmaterials)" ""
assert_eq "fixture vmfix_bare invalid_paths" "$(jfield "$line" invalid_paths)" "1"
assert_eq "fixture vmfix_bare missing_materials" "$(jfield "$line" missing_materials)" "0"
assert_eq "fixture vmfix_bare complete" "$(jfield "$line" complete)" "false"
assert_contains "fixture vmfix_bare reason names the missing path" "$(jfield "$line" reason)" "no candidate"

echo "-- the texture path escapes the asset root --"
line=$(model_line "models/weapons/v_models/vmfix_escape.mdl" "$PROBE_LINES")
assert_eq "fixture vmfix_escape materialResolved" "$(jfield "$line" vmt_resolved)" "1"
assert_eq "fixture vmfix_escape textureResolved" "$(jfield "$line" vtf_resolved)" "0"
assert_eq "fixture vmfix_escape invalid_paths" "$(jfield "$line" invalid_paths)" "1"
assert_eq "fixture vmfix_escape complete" "$(jfield "$line" complete)" "false"
assert_contains "fixture vmfix_escape reason names the boundary" "$(jfield "$line" reason)" "outside the asset root"

echo "-- the VTF parses and refuses to decode as 2D --"
# The cubemap flag. vtf_resolved=1 with pixels_decoded=0 is the pair that
# separates "the texture was read" from "the texture produced pixels"; it is
# also the only fixture the fake-decode mutation can turn red.
line=$(model_line "models/weapons/v_models/vmfix_cubemap.mdl" "$PROBE_LINES")
assert_eq "fixture vmfix_cubemap textureResolved" "$(jfield "$line" vtf_resolved)" "1"
assert_eq "fixture vmfix_cubemap pixelsDecoded" "$(jfield "$line" pixels_decoded)" "0"
assert_eq "fixture vmfix_cubemap missing_textures" "$(jfield "$line" missing_textures)" "1"
assert_eq "fixture vmfix_cubemap complete" "$(jfield "$line" complete)" "false"
assert_contains "fixture vmfix_cubemap reason names the decoder refusal" \
  "$(grep -a 'vmfix_cubemap' "$TRACE_DIR/fixture-stderr.txt" | grep -a 'slot=0 ')" "cubemap"
}

# ===========================================================================
# Section 4 -- workload counts from the summary line
# ===========================================================================
# 10 fixtures, 10 slots; 3 complete (ok, backslash, dxt5). The per-class totals
# are pinned so a run that silently stopped reading halfway cannot pass: every
# number below is a count of comparisons actually made.
section4() {
echo
echo "=== 4/5 workload counts ==="
local summary
summary=$(printf '%s\n' "$PROBE_LINES" | tr -d '\r' | grep -F '"summary":true' | head -1)
assert_eq "fixture summary models" "$(jfield "$summary" models)" "10"
assert_eq "fixture summary complete" "$(jfield "$summary" complete)" "3"
assert_eq "fixture summary slots" "$(jfield "$summary" slots)" "10"
assert_eq "fixture summary vmt_resolved" "$(jfield "$summary" vmt_resolved)" "7"
assert_eq "fixture summary vtf_resolved" "$(jfield "$summary" vtf_resolved)" "5"
assert_eq "fixture summary pixels_decoded" "$(jfield "$summary" pixels_decoded)" "4"
assert_eq "fixture summary missing_materials" "$(jfield "$summary" missing_materials)" "2"
assert_eq "fixture summary missing_textures" "$(jfield "$summary" missing_textures)" "2"
assert_eq "fixture summary invalid_paths" "$(jfield "$summary" invalid_paths)" "2"
assert_eq "fixture summary oversized_textures" "$(jfield "$summary" oversized_textures)" "1"
}

# ===========================================================================
# Section 5 -- the probe refuses a missing model instead of passing it
# ===========================================================================
# "The model was not there" is a reading, not a crash and not a pass: rc must
# be 1, the report must name the model missing, and the summary must not count
# it as complete. This is the shape a resolver that returned success on an
# empty slot list would fail.
section5() {
echo
echo "=== 5/5 a missing model is refused ==="
run_probe "$TRACE_DIR/missing-model-stderr.txt" --tf-root "$TF" \
  --model models/weapons/v_models/v_ada_does_not_exist.mdl
assert_eq "missing model probe rc" "$PROBE_RC" "1"
local line
line=$(printf '%s\n' "$PROBE_LINES" | tr -d '\r' | grep -F '"model":"models/weapons/v_models/v_ada_does_not_exist.mdl"' | head -1)
assert_eq "missing model resolution" "$(jfield "$line" resolution)" "missing"
assert_eq "missing model complete" "$(jfield "$line" complete)" "false"
assert_contains "missing model reason names the absence" "$(jfield "$line" reason)" "not in the loose files or any archive"
local summary
summary=$(printf '%s\n' "$PROBE_LINES" | tr -d '\r' | grep -F '"summary":true' | head -1)
assert_eq "missing model summary complete" "$(jfield "$summary" complete)" "0"
}

run_sections() {
  local section
  for section in $SECTIONS; do
    "section$section"
  done
}

# ===========================================================================
# mutation support
# ===========================================================================
SOURCE=""
BACKUP=""

# The restore for a fixture mutation is a REGENERATION, not a backup copy.
# This is not a stylistic choice: the tree backup this replaces went wrong in
# a way only a tree copy can. The sandbox intercepts bulk deletes, so the
# `rm -rf` of the fixed backup path failed silently between two mutations;
# the next `cp -r fixtures backup` then copied the fixtures *into* the
# surviving backup directory, and the overlay restore wrote that nested copy
# back into the live tree -- one extra level per round, five rounds deep
# before the run was interrupted. A regenerate-based restore has no fixed
# path to fail to delete, no tree copy to mis-nest, and cannot inherit a
# mutated byte: the generator is the ground truth section 1 diffs against on
# every run.
restore_fixtures() {
  "$PY" "$GEN" "$FIXTURE_DIR" > "$TRACE_DIR/fixture-restore.txt" 2>&1
}
restore_if_patched() {
  if [ -n "${SOURCE:-}" ] && [ -f "${BACKUP:-/nonexistent}" ]; then
    cp "$BACKUP" "$SOURCE"
  fi
  # Only mutation mode mutates the fixtures; regenerating on every
  # non-mutation exit would rewrite committed bytes for no reason.
  if [ -n "$MUTATION" ]; then
    restore_fixtures
  fi
  rmdir "$LOCK_DIR" 2>/dev/null || true
}
trap restore_if_patched EXIT

# apply_mutation <name> -> patches the fixture bytes or the probe source, and
# leaves a backup behind for the restore path.
apply_mutation() {
  local name="$1"
  case "$name" in
    vmt-deleted|vtf-deleted|basetexture-broken|cdmaterials-broken|texture-size-tampered)
      # No backup is taken: the restore is a regeneration from the generator
      # (see restore_fixtures), which cannot mis-nest the way the tree copy
      # this replaces did.
      if ! "$PY" - "$name" "$FIXTURE_ROOT" <<'PYEOF'
import io
import sys

# Each mutation is one coherent defect in the fixture bytes. Every patch
# asserts its anchor matched exactly once: a pattern that did not match would
# mean the mutated bytes were never written, and "the mutation did not move
# the reading" would then be a statement about the wrong input.
name, root = sys.argv[1], sys.argv[2]
material_dir = root + "/materials/models/weapons/v_models/vmfix"
model_dir = root + "/models/weapons/v_models"

PATCHES = {
  # The positive fixture's VMT is removed.
  "vmt-deleted": [("remove", material_dir + "/vmfix_ok.vmt", None, None)],
  # The positive fixture's VTF is removed.
  "vtf-deleted": [("remove", material_dir + "/vmfix_ok.vtf", None, None)],
  # $basetexture names a texture that does not exist. The anchor carries the
  # quotes: the bare stem also appears as the prefix of $bumpmap's value, and
  # an unanchored replace would move both or match twice and refuse.
  "basetexture-broken": [(
    "replace", material_dir + "/vmfix_ok.vmt",
    "\"$basetexture\" \"models/weapons/v_models/vmfix/vmfix_ok\"",
    "\"$basetexture\" \"models/weapons/v_models/vmfix/vmfix_does_not_exist\"")],
  # The MDL's $cdmaterials string is rewritten in place. Same-or-shorter
  # length with null padding keeps every table offset in the header valid, so
  # the file still parses and only the directory is wrong.
  "cdmaterials-broken": [(
    "replace-null-padded", model_dir + "/vmfix_ok.mdl",
    b"models/weapons/v_models/vmfix/", b"models/wrong/dir/")],
  # The VTF's width word, 64 -> 32. The file stays internally consistent (a
  # 32x64 BC1 top mip needs fewer bytes than the 64x64 one the file carries),
  # so this moves the size reading without moving anything else.
  "texture-size-tampered": [(
    "patch-u16", material_dir + "/vmfix_ok.vtf", 16, 32)],
}

for kind, path, old, new in PATCHES[name]:
    if kind == "remove":
        import os
        os.remove(path)
        sys.stderr.write("removed %s\n" % path)
        continue
    raw = io.open(path, "rb").read()
    if kind == "patch-u16":
        offset, value = old, new
        current = raw[offset] | (raw[offset + 1] << 8)
        if current != 64:
            sys.stderr.write("width word was %d, expected 64\n" % current)
            raise SystemExit(1)
        patched = bytearray(raw)
        patched[offset] = value & 0xFF
        patched[offset + 1] = (value >> 8) & 0xFF
        io.open(path, "wb").write(bytes(patched))
        sys.stderr.write("patched %s width 64 -> %d\n" % (path, value))
        continue
    if kind == "replace-null-padded":
        start = raw.find(old)
        if start < 0 or raw.count(old) != 1:
            sys.stderr.write("anchor matched %d times at %d in %s\n"
                             % (raw.count(old), start, path))
            raise SystemExit(1)
        # The MDL stores the directory as a null-terminated string inside a
        # fixed region; write the new text and null-fill the remainder of the
        # old text so no later byte moves.
        end = raw.find(b"\x00", start)
        if end < 0:
            sys.stderr.write("cd string is not null-terminated in %s\n" % path)
            raise SystemExit(1)
        patched = bytearray(raw)
        patched[start:end] = new + b"\x00" * (end - start - len(new))
        io.open(path, "wb").write(bytes(patched))
        sys.stderr.write("patched %s cd string\n" % path)
        continue
    text = raw.decode("utf-8")
    if text.count(old) != 1:
        sys.stderr.write("anchor matched %d times in %s\n" % (text.count(old), path))
        raise SystemExit(1)
    io.open(path, "wb", ).write(text.replace(old, new).encode("utf-8"))
    sys.stderr.write("patched %s\n" % path)
PYEOF
      then
        echo "FATAL: the $name mutation did not apply; the mutated run would prove nothing"
        restore_fixtures
        exit 1
      fi
      ;;
    probe-fakes-decode)
      SOURCE="native/tools/viewmodel_material_probe.cpp"
      BACKUP=".scratch/viewmodel-material-probe.bak"
      cp "$SOURCE" "$BACKUP"
      if ! "$PY" - "$SOURCE" <<'PYEOF'
import io
import sys

# The probe claims decoded pixels for every VTF that parses. The honest line
# refuses an empty decode; the mutation removes the refusal, so the cubemap
# fixture -- a VTF that parses and cannot be decoded as 2D -- reads as
# complete. Every other fixture still fails for its own reason, which is what
# makes the named assertion the only thing that moved.
path = sys.argv[1]
raw = io.open(path, "rb").read()
crlf = b"\r\n" in raw
text = raw.decode("utf-8")
if crlf:
    text = text.replace("\r\n", "\n")
old = """    const auto rgba = texture.decodeRgba(vtfRead.bytes, &decodeError);
    if (rgba.empty()) {"""
new = """    const auto rgba = texture.decodeRgba(vtfRead.bytes, &decodeError);
    if (false) {  // MUTATION: pixels are claimed without decoding"""
if text.count(old) != 1:
    sys.stderr.write("anchor matched %d times in %s\n" % (text.count(old), path))
    raise SystemExit(1)
text = text.replace(old, new)
if crlf:
    text = text.replace("\n", "\r\n")
io.open(path, "wb").write(text.encode("utf-8"))
sys.stderr.write("patched %s\n" % path)
PYEOF
      then
        echo "FATAL: the probe-fakes-decode mutation did not apply"
        cp "$BACKUP" "$SOURCE"
        SOURCE=""; BACKUP=""
        exit 1
      fi
      ;;
  esac
}

# ===========================================================================
# the driver
# ===========================================================================
if [ -z "$MUTATION" ]; then
  BUILD_TOKEN=$(sha256sum "$PROBE" 2>/dev/null | cut -c1-16)
  run_sections
  {
    echo "# viewmodel-material-check.sh readings ($(date -u +%Y-%m-%dT%H:%M:%SZ))"
    echo "# probe: $PROBE (token $BUILD_TOKEN)"
    echo "# tf root: $TF"
    echo "# fixture root: $FIXTURE_DIR"
    echo "# assertions that held: $ASSERTIONS"
    echo "# failures: $fail"
    echo "--- real install (stdout) ---"
    printf '%s\n' "${REAL_LINES:-<not run>}"
    echo "--- real install (per-slot stderr) ---"
    cat "$TRACE_DIR/real-stderr.txt" 2>/dev/null
    echo "--- fixtures (stdout) ---"
    printf '%s\n' "${FIXTURE_LINES:-<not run>}"
    echo "--- fixtures (per-slot stderr) ---"
    cat "$TRACE_DIR/fixture-stderr.txt" 2>/dev/null
  } > "$OUT/material-readings.txt"
  echo
  echo "assertions_ok=$ASSERTIONS"
  if [ "$fail" -eq 0 ]; then
    echo "VIEWMODEL-MATERIAL=PASS"
    exit 0
  fi
  echo "VIEWMODEL-MATERIAL=FAIL"
  exit 1
fi

# ---- mutation mode -------------------------------------------------------
# One mutation at a time: patch, rebuild when a source moved, run the section
# it can move, and require a *named* assertion to go red. "The script exited
# non-zero" is not the claim -- a mutation that broke the build, or that
# reddened an unrelated assertion, would satisfy that and prove nothing about
# the check under test.
declare -A EXPECT_RED=(
  [vmt-deleted]="fixture vmfix_ok missing_materials"
  [vtf-deleted]="fixture vmfix_ok missing_textures"
  [basetexture-broken]="fixture vmfix_ok missing_textures"
  [cdmaterials-broken]="fixture vmfix_ok missing_materials"
  [texture-size-tampered]="fixture vmfix_ok texture_width"
  [probe-fakes-decode]="fixture vmfix_cubemap pixelsDecoded"
)
declare -A MUTATION_SECTIONS=(
  [vmt-deleted]="3"
  [vtf-deleted]="3"
  [basetexture-broken]="3"
  [cdmaterials-broken]="3"
  [texture-size-tampered]="3"
  [probe-fakes-decode]="3"
)
declare -A MUTATION_TARGETS=(
  [vmt-deleted]=""
  [vtf-deleted]=""
  [basetexture-broken]=""
  [cdmaterials-broken]=""
  [texture-size-tampered]=""
  [probe-fakes-decode]="viewmodel_material_probe"
)
HASHES_BEFORE=$(sha256sum native/tools/viewmodel_material_probe.cpp \
  evidence/viewmodel/fixtures/viewmodel-material-fixture-gen.py 2>/dev/null)
MUTATION_RESULTS=()
for mutation in "${MUTATIONS[@]}"; do
  echo
  echo "###################################################################"
  echo "=== mutation: $mutation ==="
  # A mutation that is not fully described by the tables is a bug in this file,
  # not a mutation result. Named here, before anything is built.
  if [ -z "${MUTATION_SECTIONS[$mutation]:-}" ] || [ -z "${EXPECT_RED[$mutation]:-}" ]; then
    echo "FATAL: mutation '$mutation' is not fully described by the tables"
    exit 1
  fi
  fail=0; ASSERTIONS=0; FAIL_LINES=(); SECTIONS="${MUTATION_SECTIONS[$mutation]}"
  apply_mutation "$mutation"
  if [ -n "${MUTATION_TARGETS[$mutation]}" ]; then
    bash build-target.sh ${MUTATION_TARGETS[$mutation]} > "$TRACE_DIR/mutation-$mutation-build.txt" 2>&1
    build_rc=$?
    echo "mutation build rc=$build_rc"
    if [ "$build_rc" -ne 0 ]; then
      echo "  FAIL the mutated build did not compile; the mutation proves nothing"
      tail -5 "$TRACE_DIR/mutation-$mutation-build.txt" | sed 's/^/       | /'
      MUTATION_RESULTS+=("$mutation: BUILD-FAILED")
      if [ -n "${SOURCE:-}" ] && [ -f "${BACKUP:-/nonexistent}" ]; then
        cp "$BACKUP" "$SOURCE"; SOURCE=""; BACKUP=""
        bash build-target.sh ${MUTATION_TARGETS[$mutation]} > "$TRACE_DIR/mutation-$mutation-restore.txt" 2>&1
      fi
      restore_fixtures
      continue
    fi
  fi
  run_sections
  # Restore first, then judge: a mutation run that ended with a patched tree
  # would poison every later reading in this file.
  if [ -n "${SOURCE:-}" ] && [ -f "${BACKUP:-/nonexistent}" ]; then
    cp "$BACKUP" "$SOURCE"; SOURCE=""; BACKUP=""
    if [ -n "${MUTATION_TARGETS[$mutation]}" ]; then
      bash build-target.sh ${MUTATION_TARGETS[$mutation]} > "$TRACE_DIR/mutation-$mutation-restore.txt" 2>&1
      restore_rc=$?
      echo "restore build rc=$restore_rc"
      if [ "$restore_rc" -ne 0 ]; then
        echo "MUTATION-CAUGHT=FAIL ($mutation: the restore build failed; the tree is not back)"
        MUTATION_RESULTS+=("$mutation: RESTORE-FAILED")
        continue
      fi
    fi
  fi
  restore_fixtures
  RED_NAMED=0
  if [ "${#FAIL_LINES[@]}" -gt 0 ]; then
    for line in "${FAIL_LINES[@]}"; do
      case "$line" in
        *"${EXPECT_RED[$mutation]}"*) RED_NAMED=1 ;;
      esac
    done
  fi
  if [ "$fail" -eq 1 ] && [ "$RED_NAMED" -eq 1 ]; then
    {
      echo "# the $mutation mutation was caught: these assertions went red"
      printf '%s\n' "${FAIL_LINES[@]}"
    } > "$OUT/material-mutation-failure-$mutation.txt"
    echo "MUTATION-CAUGHT=PASS ($mutation turns this step red on '${EXPECT_RED[$mutation]}')"
    MUTATION_RESULTS+=("$mutation: CAUGHT")
  else
    echo "MUTATION-CAUGHT=FAIL ($mutation: fail=$fail, named-assertion-red=$RED_NAMED; expected '${EXPECT_RED[$mutation]}' to go red)"
    if [ "${#FAIL_LINES[@]}" -gt 0 ]; then
      printf '%s\n' "${FAIL_LINES[@]}" | sed 's/^/       | /' | head -10
    else
      echo "       | no assertion went red at all"
    fi
    MUTATION_RESULTS+=("$mutation: NOT-CAUGHT")
  fi
done
SOURCE=""; BACKUP=""

echo
echo "=== the mutated inputs must be back where they started ==="
HASHES_AFTER=$(sha256sum native/tools/viewmodel_material_probe.cpp \
  evidence/viewmodel/fixtures/viewmodel-material-fixture-gen.py 2>/dev/null)
RESTORED=1
if [ "$HASHES_BEFORE" != "$HASHES_AFTER" ]; then
  echo "  FAIL a mutated source was not restored:"
  diff <(printf '%s\n' "$HASHES_BEFORE") <(printf '%s\n' "$HASHES_AFTER") | sed 's/^/       | /'
  MUTATION_RESULTS+=("restore: SOURCE-NOT-RESTORED")
  RESTORED=0
fi
# The fixture bytes: regenerate is the ground truth the provenance section
# already pins, so a live tree that matches a fresh generation is a tree that
# is back.
local_regen="$TRACE_DIR/post-mutation-regen"
rm -rf "$local_regen"
"$PY" "$GEN" "$local_regen" > /dev/null 2>&1
if diff -r --strip-trailing-cr "$local_regen" "$FIXTURE_DIR" > "$TRACE_DIR/post-mutation.diff" 2>&1; then
  echo "  OK   the fixture bytes are identical to a fresh generation"
else
  echo "  FAIL the fixture bytes were not restored:"
  sed 's/^/       | /' "$TRACE_DIR/post-mutation.diff" | head -10
  MUTATION_RESULTS+=("restore: FIXTURES-NOT-RESTORED")
  RESTORED=0
fi

echo
echo "=== mutation summary ==="
printf '%s\n' "${MUTATION_RESULTS[@]}" | sed 's/^/  /'
CAUGHT=$(printf '%s\n' "${MUTATION_RESULTS[@]}" | grep -c ': CAUGHT$')
if [ "$CAUGHT" -eq "${#MUTATIONS[@]}" ] && [ "$RESTORED" -eq 1 ]; then
  echo "VIEWMODEL-MATERIAL-MUTATION=PASS ($CAUGHT/${#MUTATIONS[@]} mutations caught, tree restored)"
  exit 0
fi
echo "VIEWMODEL-MATERIAL-MUTATION=FAIL ($CAUGHT/${#MUTATIONS[@]} caught, restored=$RESTORED)"
exit 1
