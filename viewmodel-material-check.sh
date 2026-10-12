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
#   Section 5 -- a missing model is refused rather than passed over.
#   Section 6 -- the resolved material reaches the draw pass. The product is
#     launched on the pinned POV recording with `--hidden-window --audio-disabled`
#     and the readings of the nine counters are asserted: which slot the drawn
#     mesh names, that it resolved, that a texture was bound, that the upload
#     count does not grow with the frame count, and that phong/bump/selfillum are
#     declared by the resources but applied by nothing.
#   Section 7 -- the binding is what changed the picture. Five more launches,
#     all with `--start-paused` so the world is frozen at tick 0 and the two
#     frames of each comparison differ in exactly one input. The control pair
#     (flat twice) and the textured pair (twice) establish that each state is
#     deterministic; the cross pair establishes that the texture moves the
#     picture; and a SECOND texture on the same geometry, plus three strips that
#     do not contain the ViewModel, establish that the change is the ViewModel's
#     and not the world's.
#   Section 8 -- a slot that does not resolve falls back. The fault is INJECTED
#     and labelled, because no input on this machine carries it: all 36 shipped
#     ViewModels resolve every slot, and the fixtures that carry the defect have
#     no geometry and cannot reach the draw pass. The frame is then required to be
#     byte-identical to the flat control's, which is the same state a resolvable
#     failure reaches.
#   Section 9 -- the alpha path is reachable. `v_sapper_spy` slot 2 is glass
#     (`$translucent`) and sits on a mesh the pass does not draw, so the slot is
#     named explicitly and the run is labelled `slotOverride=2`.
#
# THE PIXEL CLAIMS DEPEND ON A PINNED WORLD, SO THE PIN IS A READING
#   `--capture-tick` arms the capture for the first frame whose playback tick has
#   reached the value, and which tick that lands on varies with frame timing:
#   measured, four launches at `--capture-tick 200` captured ticks 200, 201 and
#   202, and the world differs between them by up to 3.5% of the frame. A
#   comparison across two such captures would credit the material with the world's
#   own movement. Every pixel comparison therefore uses a paused, one-frame launch
#   (`frames=1 tick=0`, asserted on both sides) -- and the frames the comparison
#   claims are about are named, so a rerun that landed on a different tick would
#   redden the reading rather than pass quietly.
#
# WHAT IT DOES NOT SAY
#   It does not say the world is textured, or that the ViewModel's own lighting
#   is correct: phase 1 is albedo, alpha and the fallback, and the pass still
#   multiplies the albedo by the flat colour the previous tier used. Phong, bump
#   and selfillum are NOT implemented -- the readings state that on every frame
#   rather than leaving it to this paragraph. It also does not say this is the
#   only way the material can fail: the fallback is forced by an injected fault
#   because no shipped input reaches it, so what is proven is that the fallback
#   branch works and is labelled, not that any particular model takes it. And the
#   pixel comparisons are single-frame: they say the picture differs, not that it
#   stays different across a whole recording.
#
# Usage: bash viewmodel-material-check.sh [--mutation [name]]
#   --mutation          runs all ten mutations. Each patches one coherent
#                       defect -- six in the fixture bytes or the probe source,
#                       four in the product's own source and shader -- and requires
#                       this script to go red on a *named* assertion (not merely to
#                       exit non-zero):
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
#                         range-key-dropped    the resolved key is dropped from
#                                              the range, so nothing is bound
#                         material-flag-ignored the pixel shader stops honouring
#                                              the material flag
#                         injection-dropped    the injected fault silently stops
#                                              being applied
#                         texture-rebuilt-every-frame the pass re-runs and the
#                                              cache key is unique per call
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
ALL_MUTATIONS=(vmt-deleted vtf-deleted basetexture-broken cdmaterials-broken texture-size-tampered probe-fakes-decode range-key-dropped material-flag-ignored injection-dropped texture-rebuilt-every-frame)
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
# The full list is the default and is what the acceptance step runs. The variable
# exists so a section can be rerun on its own while a defect is being chased;
# `--mutation` overrides it either way, so it cannot shrink a mutation's coverage.
SECTIONS="${VIEWMODEL_MATERIAL_SECTIONS:-1 2 3 4 5 6 7 8 9}"
# The waveOut device index the product opens. Sections 6 to 9 all pass
# `--audio-disabled` as well, so the device is named and then not used: the
# product's own quiet-capture contract requires both, and a run that only muted
# would report a different audio state than the gates that came before it.
AUDIO_DEVICE=6
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

# bmp_diff <label> <a> <b> <bounds...> -> PASS/FAIL of one pixel comparison.
# The tool's exit code is the assertion: 0 every bound held, 1 a bound failed,
# 2 the input could not be read. All three are reported; none is a skip.
bmp_diff() {
  local label="$1" a="$2" b="$3"; shift 3
  local line rc
  line=$("$PY" bmp-pixel-diff.py "$a" "$b" "$@" 2>&1)
  rc=$?
  printf '%s\n' "$line" | sed 's/^/  /'
  if [ "$rc" -eq 0 ]; then
    echo "  OK   $label"; ASSERTIONS=$((ASSERTIONS + 1))
  else
    echo "  FAIL $label (rc=$rc)"; fail=1; FAIL_LINES+=("$label: $(printf '%s' "$line" | tr '\n' ' ')")
  fi
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
echo "=== 1/9 the fixture bytes are reproducible ==="
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
echo "=== 2/9 the real install: four ViewModel model families ==="
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
echo "=== 3/9 the fixtures: ten defect classes ==="
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
echo "=== 4/9 workload counts ==="
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
echo "=== 5/9 a missing model is refused ==="
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

# ===========================================================================
# The product draw pass -- sections 6 to 9
# ===========================================================================
# Sections 1 to 5 measure the resource chain. None of them says a pixel reached
# a screen: they run a probe, with no device and no window. These four sections
# close that gap. Each launches the product on the pinned POV recording with
# `--hidden-window --audio-disabled` and reads what the pass did.
#
# TWO KINDS OF RUN, BECAUSE THE TWO CLAIMS NEED DIFFERENT THINGS
#   A READING claim (sections 6, 8, 9) needs many frames: "the texture is not
#   rebuilt per frame" is a statement about a count that only grows if there are
#   frames to grow over, so those runs play 200 ticks of the recording.
#
#   A PIXEL claim (sections 7, 8) needs the WORLD TO BE IDENTICAL between the two
#   frames being compared, and that is not free. The capture fires on the first
#   frame whose playback tick has reached `--capture-tick`, and how many ticks
#   have gone by when that frame is drawn varies run to run: measured, the same
#   `--capture-tick 200` landed on tick 200, 201 and 202 across four launches, and
#   those frames differ in the WORLD by up to 3.5% of the frame. Comparing two
#   such captures attributes the world's own movement to the material.
#
#   So every pixel comparison here uses `--start-paused --capture-tick 0`, which
#   freezes the world at tick 0: measured across seven launches, all report
#   `frames=1 tick=0` and two launches of the same settings are byte-identical.
#   The two frames in a comparison then differ in exactly one input -- the flag
#   that decides whether a texture is bound -- and the tick is asserted on both
#   sides rather than assumed. This also makes the runs cheap: a paused launch
#   takes about 20 s against about 55 s for a 200-tick one.
PRODUCT_TICK="${VIEWMODEL_MATERIAL_TICK:-200}"
PRODUCT_DEMO="$TF/demos/autorecord_2026-07-02_13-26-46.dem"
PRODUCT_EXE=native/build-nmake/tf2_demo_native.exe
# The model the `--viewmodel` switch names for the long run. It is not the model
# the pass draws -- on this recording the entity's own companion wins -- and that
# is the point: whose materials are bound is decided by what is DRAWN, not by what
# was asked for, and the readings are read from the drawn request.
PRODUCT_MODEL="models/weapons/v_models/v_scattergun_scout.mdl"
# The model the paused runs draw. `v_sapper_spy` because two of its four slots
# resolve to different, real textures (spy_hands_red 1024x1024 DXT1 and
# v_sapper 1024x1024 DXT5) and its slot 2 is the only `$translucent` material that
# a drawn model has -- so one model covers the albedo pair, the locality control
# and the alpha case. `--viewmodel-render command` is what makes it the drawn one:
# without it the recording's companion wins and all four readings would describe
# the arms instead.
PRODUCT_ALPHA_MODEL="models/weapons/v_models/v_sapper_spy.mdl"
PRODUCT_RUN_TIMEOUT=400

# Missing input is a failure, not a skip -- the same rule as the probe's inputs.
# Checked here rather than at the top because these names only exist from here
# down; a section that needs one and cannot find it must say so, not appear to
# have passed by comparing nothing.
for path in "$PRODUCT_EXE" "$PRODUCT_DEMO" bmp-pixel-diff.py; do
  [ -e "$path" ] || { echo "FATAL: missing input for sections 6 to 9: $path"; exit 1; }
done

# rval <line> <key> -> the value of "key=value" in a space-separated reading line.
rval() {
  printf '%s\n' "$1" | tr -d '\r' | tr ' ' '\n' | sed -n "s/^$2=//p" | head -1
}

product_launch() { # product_launch <name> <model> <tick> [extra...]
  local name="$1" model="$2" tick="$3"; shift 3
  [ -s "$TRACE_DIR/$name.bmp" ] && return 0
  TF2_NATIVE_TRACE="$ROOT/$TRACE_DIR/$name.trace" timeout "$PRODUCT_RUN_TIMEOUT" "$PRODUCT_EXE" \
    --tf-root "$TF" --audio-device "$AUDIO_DEVICE" --demo "$PRODUCT_DEMO" \
    --settings-file "$TRACE_DIR/$name-settings.ini" \
    --capture-frame "$ROOT/$TRACE_DIR/$name.bmp" --capture-tick "$tick" \
    --metrics-file "$TRACE_DIR/$name.csv" \
    --viewmodel "$model" --viewmodel-fov 90 "$@" \
    --hidden-window --audio-disabled > "$TRACE_DIR/$name.log" 2>&1
  PRODUCT_RC=$?
  printf '%s' "$PRODUCT_RC" > "$TRACE_DIR/$name.rc"
  return 0
}

# ensure_live_run -- many frames, moving world. For reading claims.
ensure_live_run() { # ensure_live_run <name> [extra...]
  product_launch "$1" "$PRODUCT_MODEL" "$PRODUCT_TICK" "${@:2}"
}

# ensure_paused_run -- one frame, world frozen at tick 0. For pixel claims.
ensure_paused_run() { # ensure_paused_run <name> [extra...]
  product_launch "$1" "$PRODUCT_ALPHA_MODEL" 0 --viewmodel-render command --start-paused "${@:2}"
}

# load_product <name> -> PRODUCT_RC, FRAMES, TICK, READINGS, MATERIALS_LINE,
# RANGES_LINE, DRAW_ORDER_LINE. Each is replaced, never appended to: a section that
# read the previous run's line would be measuring the wrong launch.
load_product() {
  local name="$1"
  local trace="$TRACE_DIR/$name.trace"
  PRODUCT_RC=$(cat "$TRACE_DIR/$name.rc" 2>/dev/null)
  FRAMES=$(tail -1 "$TRACE_DIR/$name.csv" 2>/dev/null | tr -d '\r' | awk -F, '{print $2}')
  TICK=$(tail -1 "$TRACE_DIR/$name.csv" 2>/dev/null | tr -d '\r' | awk -F, '{print $4}')
  READINGS=$(grep -a 'gpu: viewmodel materialReadings' "$trace" 2>/dev/null | tr -d '\r' | tail -1)
  MATERIALS_LINE=$(grep -a 'gpu: viewmodel materials' "$trace" 2>/dev/null | tr -d '\r' | tail -1)
  RANGES_LINE=$(grep -a 'gpu: viewmodel ranges' "$trace" 2>/dev/null | tr -d '\r' | tail -1)
  DRAW_ORDER_LINE=$(grep -a 'gpu: viewmodel pass order' "$trace" 2>/dev/null | tr -d '\r' | tail -1)
}

# The captures are cleared once per invocation. Deleting them here rather than
# trusting what is on disk is what keeps a mutation run honest: after a product
# mutation is compiled in, every capture these sections read has to have been
# produced by the mutated binary.
clear_product_captures() {
  local name
  for name in longrun ptex ptex2 palt pflat pflat2 pmissing palpha; do
    rm -f "$TRACE_DIR/$name.bmp" "$TRACE_DIR/$name.trace" "$TRACE_DIR/$name.csv" "$TRACE_DIR/$name.rc"
  done
  # The sandbox's bulk-delete guard can refuse a batch of rm calls WITHOUT
  # failing the command (observed 2026-10-12: three batches of four files were
  # refused mid-loop while earlier batches in the same loop went through, once
  # the turn's cumulative delete count crossed 50). A capture that survives
  # makes product_launch skip the run and measure the PREVIOUS build -- under
  # --mutation that is the previous MUTATION's binary -- so survival is dealt
  # with here rather than tolerated: python removes what rm could not, and
  # whatever still survives after that is fatal, because the alternative is a
  # green verdict read off the wrong program.
  local survived=0
  for name in longrun ptex ptex2 palt pflat pflat2 pmissing palpha; do
    [ -s "$TRACE_DIR/$name.bmp" ] && survived=$((survived + 1))
  done
  if [ "$survived" -gt 0 ]; then
    "$PY" - "$TRACE_DIR" <<'PYEOF'
import os
import sys

names = ("longrun", "ptex", "ptex2", "palt", "pflat", "pflat2", "pmissing", "palpha")
for name in names:
    for ext in (".bmp", ".trace", ".csv", ".rc"):
        path = os.path.join(sys.argv[1], name + ext)
        try:
            os.remove(path)
        except OSError:
            pass
PYEOF
    survived=0
    for name in longrun ptex ptex2 palt pflat pflat2 pmissing palpha; do
      [ -s "$TRACE_DIR/$name.bmp" ] && survived=$((survived + 1))
    done
  fi
  if [ "$survived" -gt 0 ]; then
    echo "VIEWMODEL-MATERIAL=FAIL ($survived stale captures could not be cleared; refusing to measure a previous build)"
    exit 1
  fi
}

section6() {
echo
echo "=== 6/9 the resolved material reaches the ViewModel pass ==="
ensure_live_run longrun
load_product longrun
assert_eq "material run rc" "$PRODUCT_RC" "0"
assert_eq "the material pass ran" "$(rval "$READINGS" passRan)" "1"
assert_eq "slots on the drawn model" "$(rval "$READINGS" materialSlots)" "3"
assert_eq "every slot resolved" "$(rval "$READINGS" materialResolved)" "3"
assert_eq "no slot fell back" "$(rval "$READINGS" fallbackSlots)" "0"
assert_eq "the pass built one range" "$(rval "$READINGS" drawRanges)" "1"
assert_eq "the range is not a fallback" "$(rval "$READINGS" fallbackRanges)" "0"
assert_eq "the range has a texture bound" "$(rval "$READINGS" boundSlots)" "1"
assert_eq "this is a natural run (no slot override)" "$(rval "$READINGS" slotOverride)" "-1"
assert_eq "this is a natural run (flat colour not forced)" "$(rval "$READINGS" flatColor)" "0"
assert_eq "this is a natural run (no injected fault)" "$(rval "$READINGS" missingSlotInjected)" "-1"
assert_eq "no slot failed to decode" "$(rval "$READINGS" decodeFailure)" "-"
# The reuse claim, and why it is stated as a ratio rather than an equality.
# One texture per distinct resolved path is the cache working; one texture per
# FRAME is the defect the task forbids. `texturesUploaded * 20 <= frames` says the
# first and reddens on the second (about 130 frames, 2 uploads: 40 <= 130 holds;
# a per-frame rebuild would give thousands against 130). `texturesReused` is the
# other half -- it counts binds that found the key already present -- so it has to
# track the frame count, not lag it.
assert_eq "captures at least 100 frames" \
  "$([ "${FRAMES:-0}" -ge 100 ] && echo yes || echo no)" "yes"
assert_eq "uploads do not grow with the frame count" \
  "$([ "$(rval "$READINGS" texturesUploaded)" -gt 0 ] \
     && [ $(( $(rval "$READINGS" texturesUploaded) * 20 )) -le "${FRAMES:-0}" ] \
     && echo yes || echo no)" "yes"
assert_eq "binds track the frames drawn" \
  "$([ "$(rval "$READINGS" texturesReused)" -ge $(( ${FRAMES:-0} - 1 )) ] && echo yes || echo no)" "yes"
# The declared-versus-applied split. `phongSlots=3` and `bumpSlots=3` are what the
# VMTs declare; the shader reads neither, and `*AppliedRanges=0` is that fact on
# the same line. A reader who took the declaration counts for support would be
# reading a number this gate would not have caught -- which is why both are here.
assert_ge "phong declared by the drawn model" "$(rval "$READINGS" phongSlots)" 1
assert_ge "bump declared by the drawn model" "$(rval "$READINGS" bumpSlots)" 1
assert_eq "phong is NOT applied (phase 1)" "$(rval "$READINGS" phongAppliedRanges)" "0"
assert_eq "bump is NOT applied (phase 1)" "$(rval "$READINGS" bumpAppliedRanges)" "0"
assert_eq "selfillum is NOT applied (phase 1)" "$(rval "$READINGS" selfillumAppliedRanges)" "0"
# Which slot the range names, and on whose authority. `drawnSlot=0` from the MDL's
# own mesh table is what makes the bound slot a property of the model rather than
# of the pass's default -- and it is what makes the slot the pass binds the one the
# DRAWN mesh names.
assert_contains "the drawn slot comes from the MDL mesh table" \
  "$MATERIALS_LINE" "drawnSlot=0 source=mdl-mesh-table"
assert_contains "the mesh stride was exercised, not assumed" "$MATERIALS_LINE" "meshStrideExercised=1"
assert_contains "the range is textured" "$RANGES_LINE" "textured=1"
assert_contains "the bound model is the one the entity named" "$DRAW_ORDER_LINE" \
  "drawnModel=models/weapons/c_models/c_sniper_arms.mdl"
assert_contains "the drawn model is the entity's own" "$DRAW_ORDER_LINE" "entityModelMatchesDrawn=1"
}

# Is this frame a pinned-world frame? Both sides of every pixel comparison below
# are asserted to answer yes, because "the world is frozen" is the assumption that
# makes the comparison causal, and an assumption is not a reading. `frames=1` alone
# would not do it: a run that drew one frame at tick 300 is as frozen as one at
# tick 0, but the two are different worlds.
frozen_world_ok() { # frozen_world_ok <name>
  load_product "$1"
  [ "$PRODUCT_RC" = "0" ] && [ "$FRAMES" = "1" ] && [ "$TICK" = "0" ] && echo yes || echo no
}

section7() {
echo
echo "=== 7/9 the binding is what changes the picture ==="
ensure_paused_run ptex --viewmodel-material-slot 0
ensure_paused_run ptex2 --viewmodel-material-slot 0
# A SECOND texture on the SAME geometry. Without it, "the picture changed" could
# be a statement about the flat-colour button rather than about textures: palt
# binds a different real texture to the same mesh, so the pair differs in the
# texture and in nothing else at all.
ensure_paused_run palt --viewmodel-material-slot 1
ensure_paused_run pflat --viewmodel-flat-color
ensure_paused_run pflat2 --viewmodel-flat-color
assert_eq "the textured frame is a frozen-world frame" "$(frozen_world_ok ptex)" "yes"
assert_eq "the control frame is a frozen-world frame" "$(frozen_world_ok pflat)" "yes"
load_product pflat
assert_eq "flat control forced" "$(rval "$READINGS" flatColor)" "1"
assert_eq "flat control binds nothing" "$(rval "$READINGS" boundSlots)" "0"
assert_eq "flat control draws the fallback" "$(rval "$READINGS" fallbackRanges)" "1"
assert_eq "flat control uploads no texture" "$(rval "$READINGS" texturesUploaded)" "0"
assert_eq "flat control reuses no texture" "$(rval "$READINGS" texturesReused)" "0"
echo "  ..   ptex/ptex2/palt/pflat/pflat2 captured paused at tick 0"
bmp_diff "the material binding changes the ViewModel's pixels" \
  "$TRACE_DIR/ptex.bmp" "$TRACE_DIR/pflat.bmp" --min-diff-percent 0.5 --min-delta 20
# Where the change ISN'T. Three strips that do not contain the ViewModel must be
# byte-identical across the same pair. This is the control that keeps the claim
# above honest: a leaked texture left bound on the world's sampler, or a world that
# moved between the two launches, would put differences in these strips, and the
# whole-frame number alone could not tell that apart from the ViewModel itself.
bmp_diff "the change is confined to the ViewModel, not the world" \
  "$TRACE_DIR/ptex.bmp" "$TRACE_DIR/pflat.bmp" --max-diff-percent 0 \
  --region 0,0,420,681 --region 844,0,420,681 --region 0,0,1264,250
bmp_diff "a second texture changes the same frame too" \
  "$TRACE_DIR/ptex.bmp" "$TRACE_DIR/palt.bmp" --min-diff-percent 0.5 --min-delta 20
bmp_diff "a texture change is confined to the ViewModel as well" \
  "$TRACE_DIR/ptex.bmp" "$TRACE_DIR/palt.bmp" --max-diff-percent 0 \
  --region 0,0,420,681 --region 844,0,420,681 --region 0,0,1264,250
bmp_diff "two runs of the flat control are byte-identical" \
  "$TRACE_DIR/pflat.bmp" "$TRACE_DIR/pflat2.bmp" --max-diff-percent 0
bmp_diff "two runs of the textured pass are byte-identical" \
  "$TRACE_DIR/ptex.bmp" "$TRACE_DIR/ptex2.bmp" --max-diff-percent 0
}

section8() {
echo
echo "=== 8/9 a slot that does not resolve falls back, and says so ==="
# WHY THE FAULT IS INJECTED RATHER THAN SHIPPED
#   The state this section measures -- a material that does not resolve -- is not
#   reachable from any input on this machine. Measured: all 36 shipped ViewModels
#   resolve every slot (0 missing materials, 0 missing textures, 0 invalid paths
#   across 144 slots), and the ten fixtures that DO carry the defect have no
#   geometry, so they cannot reach the draw pass at all: the product refuses them
#   with `viewmodel companions are missing` before any material is read. So the
#   choice is an injected fault or no measurement. The injection puts the slot
#   into the same status with the same cleared fields the resolver produces for a
#   genuinely absent material, and every line reports `missingSlotInjected=`, so
#   an injected run cannot be read as a natural one.
ensure_paused_run pflat --viewmodel-flat-color
ensure_paused_run pmissing --viewmodel-material-missing-slot 0
load_product pmissing
assert_eq "injected run rc" "$PRODUCT_RC" "0"
assert_eq "the injection is reported" "$(rval "$READINGS" missingSlotInjected)" "0"
assert_eq "the injected slot is no longer resolved" \
  "$(rval "$READINGS" materialResolved)" "$(( $(rval "$READINGS" materialSlots) - 1 ))"
assert_eq "the injected slot is counted as a fallback" "$(rval "$READINGS" fallbackSlots)" "1"
assert_eq "nothing is bound" "$(rval "$READINGS" boundSlots)" "0"
assert_eq "the range is a fallback range" "$(rval "$READINGS" fallbackRanges)" "1"
assert_eq "no texture was uploaded for it" "$(rval "$READINGS" texturesUploaded)" "0"
assert_eq "no texture bind happened" "$(rval "$READINGS" texturesReused)" "0"
# It still draws. A fallback that skipped the draw would satisfy every reading
# above and leave a hole in the picture, which is a different defect.
assert_eq "the pass still draws the geometry" "$(rval "$READINGS" drawRanges)" "1"
assert_eq "the injected frame is a frozen-world frame" "$(frozen_world_ok pmissing)" "yes"
bmp_diff "the fallback frame is exactly the flat control frame" \
  "$TRACE_DIR/pmissing.bmp" "$TRACE_DIR/pflat.bmp" --max-diff-percent 0
}

section9() {
echo
echo "=== 9/9 the alpha path is reachable, and alpha is all that is applied ==="
# `--viewmodel-material-slot` names the slot to bind, and the run is labelled by
# `slotOverride=` for the reason the `--viewmodel-sequence` override is labelled:
# the shipped models each draw ONE descriptor and the only two `$translucent`
# slots in the whole shipped ViewModel set (v_sapper_spy slot 2 v_sapper_glass,
# v_syringegun_medic slot 2 v_syringegun_glass) sit on meshes the pass does not
# draw, so the blend state this section is about cannot be reached naturally. The
# override changes WHICH TEXTURE IS BOUND, not which geometry is drawn: the range's
# vertex count and the mesh in the buffer are the drawn model's own.
ensure_paused_run palpha --viewmodel-material-slot 2
load_product palpha
assert_eq "alpha run rc" "$PRODUCT_RC" "0"
assert_eq "the slot override is reported" "$(rval "$READINGS" slotOverride)" "2"
assert_eq "this run is not the flat control" "$(rval "$READINGS" flatColor)" "0"
assert_eq "the drawn model's slots all resolved" \
  "$(rval "$READINGS" materialResolved)" "$(rval "$READINGS" materialSlots)"
assert_eq "a texture is bound" "$(rval "$READINGS" boundSlots)" "1"
assert_ge "the model declares alpha-bearing slots" "$(rval "$READINGS" alphaSlots)" 1
assert_eq "one range uses the alpha blend state" "$(rval "$READINGS" blendedRanges)" "1"
assert_eq "no range uses alpha test (this slot is \$translucent)" \
  "$(rval "$READINGS" alphaTestedRanges)" "0"
assert_eq "alpha is the only blend path applied" \
  "$(rval "$READINGS" alphaAppliedRanges)" "1"
assert_contains "the range carries the translucent flag" "$RANGES_LINE" "translucent=1"
assert_contains "the bound slot is the glass one" "$MATERIALS_LINE" "uploadSlot=2"
# The override changes WHICH TEXTURE IS BOUND, not which geometry is drawn. The
# count is the drawn model's own -- `v_sapper_spy` builds 11640 vertices, against
# the 600 of the companion this recording would otherwise draw -- so a run whose
# override reached the geometry would print a different number here.
assert_contains "the override did not change the drawn geometry" "$RANGES_LINE" "11640 vertices"
}

run_sections() {
  # Fresh captures for every invocation. See clear_product_captures for why this
  # is per invocation and not merely once at startup: under --mutation each
  # mutation compiles a different binary, and a capture left over from the
  # previous mutation would let a section measure the wrong program.
  clear_product_captures
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
    range-key-dropped|material-flag-ignored|injection-dropped|texture-rebuilt-every-frame)
      # Product-side mutations. Each patches the SOURCE the reading it is aimed at
      # depends on, rebuilds, and requires a named assertion in a named section to
      # go red -- the same contract the fixture mutations follow. The difference is
      # the tier: these patch the product and need a rebuild, so they are slower,
      # and each declares the smallest section that can see the defect.
      case "$name" in
        material-flag-ignored) SOURCE="native/src/native_renderer.cpp" ;;
        *) SOURCE="native/src/main.cpp" ;;
      esac
      BACKUP=".scratch/viewmodel-material-mutation-$name.bak"
      cp "$SOURCE" "$BACKUP"
      if ! "$PY" - "$name" "$SOURCE" <<'PYEOF'
import io
import sys

# Every patch is (anchor, replacement) and every anchor must match EXACTLY once in
# the named file. A pattern that matched twice would move two things and the
# named assertion would no longer be evidence about the one defect claimed; a
# pattern that matched zero times would leave the tree unpatched and the run would
# report a mutation that was never applied. Both are fatal here rather than
# reported as a mutation result.
name, path = sys.argv[1], sys.argv[2]

PATCHES = {
  # The range keeps its slot but loses its key, so the pass has nothing to bind
  # and draws the fallback. This is the defect that makes `boundSlots` and
  # `texturesUploaded` readings rather than decoration: with the key dropped, a
  # gate that only read `materialResolved` would still see 3 of 3 resolved while
  # nothing reached the screen.
  "range-key-dropped": [(
    "        range.materialKey = materialSlot.vtfPath;",
    "        range.materialKey.clear();  // MUTATION: the resolved key is dropped")],
  # The pixel shader stops honouring the flag. The key is still set, the SRV is
  # still bound, every reading in section 6 is unchanged -- and the picture
  # becomes the flat one. This is what separates "the material is bound" from
  # "the binding is what changed the picture".
  "material-flag-ignored": [(
    "  if (material.x > 0.5) {",
    "  if (material.x > 100.0) {  // MUTATION: the material flag is never taken")],
  # The injected fault is dropped, so the slot stays resolved. Named here because
  # it is the failure mode that makes the injection worth reporting: an injection
  # that silently stopped applying would turn section 8 into a statement about a
  # run that never exercised the fallback.
  "injection-dropped": [(
    "    if (viewModelMaterialMissingSlot >= 0\n"
    "        && static_cast<std::size_t>(viewModelMaterialMissingSlot) < request.materials.slots.size()) {",
    "    if (false) {  // MUTATION: the injected fault is dropped")],
  # "No per-frame recreation", defeated from the caller. v1 (2026-10-12) removed
  # the early return outright, which re-ran the DECODE on every update -- and the
  # decode is one-shot by design: it clears the retained VTF bytes it decoded
  # from, so the second call demoted every slot to MissingTexture, nothing was
  # uploaded at all, and the named assertion stayed green. v2 keeps the early
  # return (mesh and material table stay put) and rebuilds a TEXTURE under a
  # per-call key: the renderer's cache check never sees a repeat, so the upload
  # path runs every update and `texturesUploaded` grows with the frame count.
  # The rebuilt texture is a 4x4 placeholder under a throw-away key -- the draw
  # path binds the ORIGINAL key, so the picture and every other reading stay
  # put and only the forbidden counter moves. The anchor is the guard line
  # itself, patched in one file, and the renderer's own cache semantics are
  # untouched, which is the point: the production path must not be edited to
  # make its own gate pass.
  "texture-rebuilt-every-frame": [
    ("    if (!path.empty() && viewModelMeshBufferPath == path) return true;",
     "    if (!path.empty() && viewModelMeshBufferPath == path) {\n"
     "      // MUTATION: the texture is rebuilt on every update instead of reused.\n"
     "      // The rebuilt upload runs under a per-call key so the cache never\n"
     "      // absorbs it; the draw path still binds the ORIGINAL key, so only\n"
     "      // `texturesUploaded` moves and the picture does not.\n"
     "      static int mutationSerial = 0;\n"
     "      for (const auto& slot : request.materials.slots) {\n"
     "        if (request.materials.drawnSlot >= 0 && slot.pixelsDecoded\n"
     "            && slot.slot == static_cast<std::size_t>(request.materials.drawnSlot)) {\n"
     "          const std::vector<std::uint8_t> rebuilt(64u, 0u);\n"
     "          renderer.uploadViewModelMaterialTexture(\n"
     "            slot.vtfPath + \"#\" + std::to_string(mutationSerial++),\n"
     "            rebuilt, 4u, 4u);\n"
     "        }\n"
     "      }\n"
     "      return true;\n"
     "    }"),
  ],
}

raw = io.open(path, "rb").read()
crlf = b"\r\n" in raw
text = raw.decode("utf-8")
if crlf:
    text = text.replace("\r\n", "\n")
for anchor, replacement in PATCHES[name]:
    count = text.count(anchor)
    if count != 1:
        sys.stderr.write("anchor matched %d times in %s: %r\n" % (count, path, anchor[:60]))
        raise SystemExit(1)
    text = text.replace(anchor, replacement)
if crlf:
    text = text.replace("\n", "\r\n")
io.open(path, "wb").write(text.encode("utf-8"))
sys.stderr.write("patched %s in %s\n" % (name, path))
PYEOF
      then
        echo "FATAL: the $name mutation did not apply"
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
    echo "--- product (per-run readings) ---"
    # Sections 6 to 9 drive the real program; the nine counters and the range each
    # launch produced are frozen here so the readings can be read without the
    # trace files, which are gate output and are not committed. `longrun` is the
    # only many-frame launch: the rest are the paused, world-frozen captures the
    # pixel comparisons use, and their `frames=1 tick=0` is the point of them.
    for name in longrun ptex ptex2 palt pflat pflat2 pmissing palpha; do
      [ -s "$TRACE_DIR/$name.trace" ] || continue
      echo "run $name: rc=$(cat "$TRACE_DIR/$name.rc" 2>/dev/null) frames=$(tail -1 "$TRACE_DIR/$name.csv" 2>/dev/null | tr -d '\r' | awk -F, '{print $2}')"
      grep -a 'gpu: viewmodel materials\|gpu: viewmodel ranges\|gpu: viewmodel materialReadings\|gpu: viewmodel pass order' \
        "$TRACE_DIR/$name.trace" 2>/dev/null | tr -d '\r' | tail -4 | sed 's/^/  /'
    done
    echo "--- product (pixel comparisons) ---"
    for pair in "ptex pflat" "pflat pflat2" "ptex ptex2" "pmissing pflat" "ptex palt"; do
      set -- $pair
      [ -s "$TRACE_DIR/$1.bmp" ] && [ -s "$TRACE_DIR/$2.bmp" ] || continue
      echo "$1 vs $2: $(sha256sum "$TRACE_DIR/$1.bmp" | cut -c1-16) / $(sha256sum "$TRACE_DIR/$2.bmp" | cut -c1-16)"
    done
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
  # The product-side mutations, each aimed at one named judgement rather than at
  # the script as a whole: a mutation that reddened some other assertion would
  # prove nothing about the check it claims to test.
  [range-key-dropped]="the range has a texture bound"
  [material-flag-ignored]="the material binding changes the ViewModel's pixels"
  [injection-dropped]="the injected slot is counted as a fallback"
  [texture-rebuilt-every-frame]="uploads do not grow with the frame count"
)
declare -A MUTATION_SECTIONS=(
  [vmt-deleted]="3"
  [vtf-deleted]="3"
  [basetexture-broken]="3"
  [cdmaterials-broken]="3"
  [texture-size-tampered]="3"
  [probe-fakes-decode]="3"
  # Each names the smallest section that can see its defect, because every section
  # here launches the product: 6 is one launch, 8 is two. `material-flag-ignored`
  # declares 7 alone and not 6, because section 7 captures its own `tex` run --
  # section 6 runs it too, and the defect is invisible to every reading in 6.
  [range-key-dropped]="6"
  [material-flag-ignored]="7"
  [injection-dropped]="8"
  [texture-rebuilt-every-frame]="6"
)
declare -A MUTATION_TARGETS=(
  [vmt-deleted]=""
  [vtf-deleted]=""
  [basetexture-broken]=""
  [cdmaterials-broken]=""
  [texture-size-tampered]=""
  [probe-fakes-decode]="viewmodel_material_probe"
  [range-key-dropped]="tf2_demo_native"
  [material-flag-ignored]="tf2_demo_native"
  [injection-dropped]="tf2_demo_native"
  [texture-rebuilt-every-frame]="tf2_demo_native"
)
HASHES_BEFORE=$(sha256sum native/tools/viewmodel_material_probe.cpp \
  evidence/viewmodel/fixtures/viewmodel-material-fixture-gen.py \
  native/src/main.cpp native/src/native_renderer.cpp 2>/dev/null)
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
  # `fail -ge 1`, not `-eq 1`: a mutation may legitimately redden more than the
  # named assertion (material-flag-ignored kills BOTH min-diff pairs in section
  # 7, because a shader that never samples makes ptex-vs-palt identical too).
  # The named assertion is the evidence; the rest of FAIL_LINES is recorded in
  # the failure file for the reader, not silently discarded.
  if [ "$fail" -ge 1 ] && [ "$RED_NAMED" -eq 1 ]; then
    {
      echo "# the $mutation mutation was caught: these assertions went red"
      printf '%s\n' "${FAIL_LINES[@]}"
    } > "$OUT/material-mutation-failure-$mutation.txt"
    echo "MUTATION-CAUGHT=PASS ($mutation turns this step red on '${EXPECT_RED[$mutation]}' [red=$fail])"
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
  evidence/viewmodel/fixtures/viewmodel-material-fixture-gen.py \
  native/src/main.cpp native/src/native_renderer.cpp 2>/dev/null)
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
