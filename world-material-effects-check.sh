#!/usr/bin/env bash
# world-material-effects-check.sh -- assert the T2 water surface and the three
# ViewModel advanced materials are drawn, not merely declared, and that each
# claim can go red.
#
# WHY THIS FILE EXISTS
# --------------------
# T1 proved the skybox and the pvs clip were geometry rather than file counts.
# T2 is the same shape one layer up: `waterSurface: approximate-warp`,
# `phongSlots: 3`, `bumpSlots: 3` and `selfillumSlots: 3` were all readings
# about *declarations*, and not one of them changed a pixel. A material that
# declares `$phong 1` and a shader that applies a specular lobe are different
# statements, and this gate is what tells them apart.
#
# WHAT IS ASSERTED, IN ORDER
#   1. the probe's self-tests, including the water warp's three properties
#      measured as properties (bounded over a grid, NaN-free, repeatable at a
#      fixed phase and different at a different one) and the six draw states
#   2. the water fixtures, and the water draw's counters on the real map
#   3. the effect reader fixtures, positive and negative, with their mutations
#   4. the water pixel claim on the real map: off != on, on == repeat
#   5. the three ViewModel effects on the pinned paused recording: each effect
#      must be declared, parsed and APPLIED, and turning it off must move the
#      frame; turning it back on must reproduce the frame exactly
#   6. the skybox and the pvs clip, re-run unchanged, as the proof that T2 did
#      not regress T1
#
# EVERY PER-RUN FILE CARRIES A RUN TAG ($RUN_TAG). The reason is measured rather
# than assumed: `rm -f` exits 0 when this machine's delete guard refuses it, so
# "clear, then read" is not a freshness guarantee, and a reading taken off
# another run's bytes looks exactly like a reading taken off this one's.
#
# Usage: bash world-material-effects-check.sh [--mutation]
# Exit:  0 = every assertion held (or, with --mutation, went red).
set -uo pipefail
cd "$(dirname "$0")"

EXE=native/build-nmake/world_material_probe.exe
VMEXE=native/build-nmake/viewmodel_material_probe.exe
PRODUCT=native/build-nmake/tf2_demo_native.exe
TF="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
MAP="cp_snakewater_final1"
DEMO="D:/TF2_Demo_Player/testdata/demos/bb841c6d379ff7c40d0c8baf99f59d8d_matcha-20260927-1347-cp_snakewater_final1.dem"
VM_DEMO="$TF/demos/autorecord_2026-07-02_13-26-46.dem"
VM_MODEL="models/weapons/v_models/v_sapper_spy.mdl"
# The device every run is pinned to. Not the default: a run that opens the
# system device makes noise on the wrong output, and the task's own rule is
# that every test that can make a sound names its device.
AUDIO_DEVICE=6
SCRATCH="${WORLD_MATERIAL_EFFECTS_SCRATCH:-.scratch/world-material-effects}"
OUT="${WORLD_MATERIAL_EFFECTS_OUT:-evidence/world-material-effects}"
mkdir -p "$OUT" "$SCRATCH"

RUN_TAG="$(date +%Y%m%d%H%M%S)-$$"
MUTATION=0
[ "${1:-}" = "--mutation" ] && MUTATION=1

fail=0
for path in "$EXE" "$VMEXE" "$PRODUCT" "$TF/maps/$MAP.bsp" "$DEMO" "$VM_DEMO"; do
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
assert_ge() { # assert_ge <label> <actual> <floor>
  if [ -n "${2:-}" ] && [ "$2" -ge "$3" ] 2>/dev/null; then
    echo "  OK   $1 = $2 (>= $3)"
  else
    echo "  FAIL $1 = ${2:-<none>}, expected >= $3"; fail=1
  fi
}
# flag <lines> <key> -> true|false, for the probe's bare-boolean self-test lines
flag() { printf '%s\n' "$1" | sed -n "s/.*\"$2\":\(true\|false\).*/\1/p" | head -1; }
# count <lines> <key> -> number, from a summary line
count() { printf '%s\n' "$1" | sed -n "s/.*\"$2\":\([0-9][0-9]*\).*/\1/p" | tail -1; }
# field <lines> <reading.key> -> the value of a probe reading line
field() { printf '%s\n' "$1" | sed -n "s/.*\"reading\":\"$2\",\"value\":\"\([^\"]*\)\".*/\1/p" | head -1; }
# rval <line> <key> -> the value of "key=value" in a space-separated product line
rval() { printf '%s\n' "$1" | tr -d '\r' | tr ' ' '\n' | sed -n "s/^$2=//p" | head -1; }
# rfield <line> <key> -> number out of a product reading line
rfield() { printf '%s\n' "$1" | grep -oE "$2=[0-9]+" | head -1 | cut -d= -f2; }

echo "=== 1/6 the probe's self-tests, including the water warp's properties ==="
SELF=$("$EXE" --self-test 2>&1)
echo "$SELF" > "$OUT/self-test.txt"
assert_eq "selfTest" "$(flag "$SELF" selfTest)" "true"
assert_eq "capabilitySelfTest" "$(flag "$SELF" capabilitySelfTest)" "true"
assert_eq "worldDrawSelfTest" "$(flag "$SELF" worldDrawSelfTest)" "true"
assert_eq "waterSelfTest" "$(flag "$SELF" waterSelfTest)" "true"
# The three properties the task names, each on its own line, so a failure says
# which property broke rather than that "water broke".
assert_eq "warp bounded" "$(flag "$SELF" warpBounded)" "true"
assert_eq "warp finite" "$(flag "$SELF" warpFinite)" "true"
assert_eq "warp NaN input" "$(flag "$SELF" warpNanIn)" "true"
assert_eq "warp NaN amplitude" "$(flag "$SELF" warpNanAmplitude)" "true"
assert_eq "warp negative amplitude" "$(flag "$SELF" warpNegativeAmplitude)" "true"
assert_eq "warp repeatable" "$(flag "$SELF" warpRepeatable)" "true"
assert_eq "warp moves with phase" "$(flag "$SELF" warpPhaseMoves)" "true"
assert_eq "zero amplitude is identity" "$(flag "$SELF" warpZeroIdentity)" "true"
# The bound, as a number. It must be at most the amplitude the pass uses (0.02),
# and it must be non-zero: a warp that never moves a uv would satisfy a bound of
# zero and change no pixel, which is the defect this whole gate is about.
WARP_DELTA=$(printf '%s\n' "$SELF" | sed -n 's/.*"warpMaxDelta":\([0-9.]*\).*/\1/p' | head -1)
if [ -n "$WARP_DELTA" ] && awk -v d="$WARP_DELTA" 'BEGIN{exit !(d>0 && d<=0.0200001)}'; then
  echo "  OK   warp max delta = $WARP_DELTA (inside the 0.02 bound and not zero)"
else
  echo "  FAIL warp max delta = ${WARP_DELTA:-<none>}, expected 0 < d <= 0.02"; fail=1
fi
for state in waterOffState waterEmptyState waterPartialState waterFallbackState \
             waterActiveState waterParams waterParamsBad; do
  assert_eq "$state" "$(flag "$SELF" "$state")" "true"
done

echo
echo "=== 2/6 the water fixtures, and the water draw on the real map ==="
WFIX=$("$EXE" --water-fixture 2>&1)
echo "$WFIX" > "$OUT/water-fixtures.txt"
assert_eq "water fixtures" "$(count "$WFIX" waterFixtures)" "6"
assert_eq "water fixture checks" "$(count "$WFIX" checks)" "42"
assert_eq "water fixture failures" "$(count "$WFIX" bad)" "0"
# The capability fixtures must still pass: this round added a fixture runner, it
# did not replace the one T1 and T3 depend on.
FIX=$("$EXE" --fixture 2>&1)
assert_eq "capability fixtures" "$(count "$FIX" fixtures)" "9"
assert_eq "capability fixture failures" "$(count "$FIX" bad)" "0"
# The map's own water, pinned. 18 faces and 36 triangles are the same numbers
# world-material-check.sh pins, and the point of repeating them here is that the
# water pass must be counting the same geometry the capability layer measured.
REAL=$("$EXE" --bsp "$TF/maps/$MAP.bsp" --tf-root "$TF" --map-name "$MAP" 2>&1)
echo "$REAL" > "$OUT/real-$MAP.txt"
assert_eq "water faces" "$(field "$REAL" water.faces)" "18"
assert_eq "water triangles" "$(field "$REAL" water.trianglesTagged)" "36"

echo
echo "=== 3/6 the effect readers, positive and negative, with their mutations ==="
EFIX=$("$VMEXE" --effects-fixture 2>&1)
echo "$EFIX" > "$OUT/effects-fixtures.txt"
assert_eq "effect fixtures" "$(count "$EFIX" effectsFixtures)" "14"
assert_eq "effect fixture checks" "$(count "$EFIX" checks)" "111"
assert_eq "effect fixture failures" "$(count "$EFIX" bad)" "0"
EMUT=$("$VMEXE" --effects-fixture --mutation 2>&1)
echo "$EMUT" > "$OUT/effects-fixtures-mutation.txt"
assert_eq "effect mutations executed" "$(count "$EMUT" mutations)" "14"
assert_eq "effect mutations not moving a reading" "$(count "$EMUT" mutationBad)" "0"
assert_eq "effect checks with mutations" "$(count "$EMUT" checks)" "125"
assert_eq "effect failures with mutations" "$(count "$EMUT" bad)" "0"

# ---------------------------------------------------------------------------
# The pixel claims. One helper, because the water and the three effects ask the
# same question of different switches: does turning the feature off change the
# frame, and does turning it back on reproduce it exactly?
# ---------------------------------------------------------------------------
run_product() { # run_product <name> <demo> [extra args...]
  local name="$1" demo="$2"; shift 2
  local trace="$SCRATCH/$name.$RUN_TAG.trace"
  local bmp="$SCRATCH/$name.$RUN_TAG.bmp"
  local csv="$SCRATCH/$name.$RUN_TAG.csv"
  local log="$SCRATCH/$name.$RUN_TAG.log"
  rm -f "$trace" "$bmp" "$csv" "$log"
  # cygpath -m gives the Windows form without the doubled backslashes the MSYS
  # layer rewrites; the trace path is read with GetEnvironmentVariableW.
  local rc
  rc=$(TF2_NATIVE_TRACE="$(cygpath -m "$PWD/$trace")" timeout 400 "$PRODUCT" \
    --tf-root "$TF" --audio-device "$AUDIO_DEVICE" --audio-disabled --hidden-window \
    --demo "$demo" --capture-tick 0 --capture-frame "$(cygpath -m "$PWD/$bmp")" \
    --metrics-file "$csv" "$@" > "$log" 2>&1; echo $?)
  PRODUCT_RC=$rc
  PRODUCT_TRACE=$trace
  PRODUCT_BMP=$bmp
  return 0
}

bmp_diff() { # bmp_diff <a> <b> -> differing byte count, or -1 when a file is missing
  if [ ! -s "$1" ] || [ ! -s "$2" ]; then echo "-1"; return; fi
  cmp -l "$1" "$2" 2>/dev/null | wc -l
}

echo
echo "=== 4/6 the water surface: off != on, on == repeat ==="
# The water is drawn by its own pass over its own buffer, so the switch must move
# the picture and nothing else. `--start-paused` pins the world at tick 0, so the
# only input that differs between the three runs is the water switch.
run_product water-off "$DEMO" --start-paused --world-sky --world-water off
WOFF_RC=$PRODUCT_RC; WOFF_TRACE=$PRODUCT_TRACE; WOFF_BMP=$PRODUCT_BMP
run_product water-on "$DEMO" --start-paused --world-sky --world-water on
WON_RC=$PRODUCT_RC; WON_TRACE=$PRODUCT_TRACE; WON_BMP=$PRODUCT_BMP
run_product water-repeat "$DEMO" --start-paused --world-sky --world-water on
WREP_RC=$PRODUCT_RC; WREP_TRACE=$PRODUCT_TRACE; WREP_BMP=$PRODUCT_BMP
assert_eq "water off run rc" "$WOFF_RC" "0"
assert_eq "water on run rc" "$WON_RC" "0"
assert_eq "water repeat run rc" "$WREP_RC" "0"
WATER_OFF=$(grep -ao "gpu: world water .*" "$WOFF_TRACE" 2>/dev/null | head -1)
WATER_ON=$(grep -ao "gpu: world water .*" "$WON_TRACE" 2>/dev/null | head -1)
if [ -n "$WATER_OFF" ] && [ -n "$WATER_ON" ]; then
  printf '%s\n%s\n' "$WATER_OFF" "$WATER_ON" > "$OUT/water-readings.txt"
else
  echo "  FAIL a water run produced no reading, so $OUT/water-readings.txt is left as it was"; fail=1
fi
assert_eq "water off state" "$(rval "$WATER_OFF" state)" "unavailable"
assert_eq "water off draws" "$(rval "$WATER_OFF" draws)" "0"
assert_eq "water off visible" "$(rval "$WATER_OFF" visible)" "0"
assert_eq "water on state" "$(rval "$WATER_ON" state)" "active"
assert_eq "water on draws" "$(rval "$WATER_ON" draws)" "1"
assert_eq "water on visible" "$(rval "$WATER_ON" visible)" "1"
assert_eq "water on warp applied" "$(rval "$WATER_ON" warp)" "1"
assert_eq "water on texture bound" "$(rval "$WATER_ON" textureBound)" "1"
assert_eq "water on faces" "$(rval "$WATER_ON" faces)" "18"
assert_eq "water on triangles" "$(rval "$WATER_ON" triangles)" "36"
# The two render targets, as the honest UNAVAILABLE the task's own rule allows
# when they cannot be built -- with a reason that names what is missing.
assert_eq "water reflection" "$(rval "$WATER_ON" reflection)" "unavailable"
assert_eq "water refraction" "$(rval "$WATER_ON" refraction)" "unavailable"
# And the pixel claim. A switch that moved only the counters would pass every
# assertion above and fail this one, which is the whole reason it is here.
WATER_DIFF=$(bmp_diff "$WOFF_BMP" "$WON_BMP")
WATER_REP=$(bmp_diff "$WON_BMP" "$WREP_BMP")
echo "  water off-vs-on differing bytes: $WATER_DIFF"
if [ "$WATER_DIFF" -gt 1000 ] 2>/dev/null; then
  echo "  OK   water-off != water-on ($WATER_DIFF bytes differ)"
else
  echo "  FAIL water-off == water-on ($WATER_DIFF bytes); the switch moved no pixel"; fail=1
fi
assert_eq "water on == water repeat" "$WATER_REP" "0"

echo
echo "=== 5/6 the ViewModel effects: declared AND applied, and each moves the frame ==="
# The model is v_sapper_spy and the slot is 0, which is the slot
# viewmodel-material-check.sh already pins as the drawn one. Its material
# declares `$phong 1` with a real exponent, `$bumpmap` with a real normal map,
# and no `$selfillum` -- measured, not assumed. Selfillum therefore needs the
# force, which is the same shape as --world-env-force and is named in the trace.
vm_run() { # vm_run <name> [extra args...]
  run_product "$1" "$VM_DEMO" --start-paused \
    --viewmodel "$VM_MODEL" --viewmodel-fov 90 --viewmodel-render command \
    --viewmodel-material-slot 0 "${@:2}"
}
vm_run vm-natural
VM_NAT_RC=$PRODUCT_RC; VM_NAT_TRACE=$PRODUCT_TRACE; VM_NAT_BMP=$PRODUCT_BMP
vm_run vm-phong-off --viewmodel-phong off
VM_PHOFF_BMP=$PRODUCT_BMP
vm_run vm-bump-off --viewmodel-bump off
VM_BUOFF_BMP=$PRODUCT_BMP
vm_run vm-selfillum-on --viewmodel-selfillum-force
VM_SELF_BMP=$PRODUCT_BMP
vm_run vm-selfillum-off --viewmodel-selfillum-force --viewmodel-selfillum off
VM_SELOFF_BMP=$PRODUCT_BMP
vm_run vm-natural-repeat
VM_REP_BMP=$PRODUCT_BMP
assert_eq "viewmodel natural run rc" "$VM_NAT_RC" "0"
EFFECTS=$(grep -ao "gpu: viewmodel effects .*" "$VM_NAT_TRACE" 2>/dev/null | head -1)
ETEX=$(grep -ao "gpu: viewmodel effects-texture .*" "$VM_NAT_TRACE" 2>/dev/null | head -1)
SWITCHES=$(grep -ao "gpu: viewmodel effects-switch .*" "$VM_NAT_TRACE" 2>/dev/null | head -1)
REASONS=$(grep -ao "gpu: viewmodel effects-reason .*" "$VM_NAT_TRACE" 2>/dev/null | head -1)
if [ -n "$EFFECTS" ]; then
  printf '%s\n%s\n%s\n%s\n' "$EFFECTS" "$ETEX" "$SWITCHES" "$REASONS" > "$OUT/viewmodel-effects.txt"
else
  echo "  FAIL the viewmodel run produced no effects reading, so $OUT/viewmodel-effects.txt is left as it was"; fail=1
fi
assert_eq "effects slot" "$(rval "$EFFECTS" slot)" "0"
# Phong: declared, parsed AND applied. Each of the three is a separate
# assertion, because "declared" is what the T2.5 gate already proved and
# "applied" is the one this round adds.
assert_eq "phong state" "$(rval "$EFFECTS" phongState)" "active"
assert_eq "phong declared" "$(rval "$EFFECTS" phongDeclared)" "1"
assert_eq "phong parsed" "$(rval "$EFFECTS" phongParsed)" "1"
assert_eq "phong uploaded" "$(rval "$EFFECTS" phongUploaded)" "1"
assert_eq "phong applied" "$(rval "$EFFECTS" phongApplied)" "1"
assert_eq "phong applied ranges" "$(rval "$EFFECTS" phongAppliedRanges)" "1"
assert_eq "phong fallback" "$(rval "$ETEX" phongFallback)" "0"
# The switches are read from the reasons line: the counters line ran past
# traceFmt's 512-byte buffer with them on it and dropped them silently.
assert_eq "phong switch is the material's" "$(rval "$SWITCHES" phongSwitch)" "material"
assert_eq "bump switch is the material's" "$(rval "$SWITCHES" bumpSwitch)" "material"
assert_eq "selfillum switch is the material's" "$(rval "$SWITCHES" selfillumSwitch)" "material"
assert_eq "selfillum force named" "$(rval "$SWITCHES" selfillumForce)" "0"
# Bump: same six, plus the four texture readings the task asks for by name.
assert_eq "bump state" "$(rval "$EFFECTS" bumpState)" "active"
assert_eq "bump declared" "$(rval "$EFFECTS" bumpDeclared)" "1"
assert_eq "bump parsed" "$(rval "$EFFECTS" bumpParsed)" "1"
assert_eq "bump uploaded" "$(rval "$EFFECTS" bumpUploaded)" "1"
assert_eq "bump applied" "$(rval "$EFFECTS" bumpApplied)" "1"
assert_eq "bump applied ranges" "$(rval "$EFFECTS" bumpAppliedRanges)" "1"
assert_eq "bump fallback" "$(rval "$ETEX" bumpFallback)" "0"
assert_eq "bump texture resolved" "$(rval "$ETEX" bumpTextureResolved)" "1"
assert_eq "bump texture uploaded" "$(rval "$ETEX" bumpTextureUploaded)" "1"
assert_eq "bump texture reused" "$(rval "$ETEX" bumpTextureReused)" "1"
# One upload for the whole run, however many frames drew: a constant value here
# with a non-zero reuse count is a cache, and a value that climbs is a rebuild.
assert_eq "bump texture uploads" "$(rval "$ETEX" bumpUploads)" "1"
# Selfillum: the material declares none, so the natural run must say so and the
# forced run must show the effect reaching a range.
assert_eq "selfillum state (natural)" "$(rval "$EFFECTS" selfillumState)" "unavailable"
assert_eq "selfillum declared (natural)" "$(rval "$EFFECTS" selfillumDeclared)" "0"
assert_eq "selfillum applied (natural)" "$(rval "$EFFECTS" selfillumApplied)" "0"
SELF_ON=$(grep -ao "gpu: viewmodel effects .*" "$SCRATCH/vm-selfillum-on.$RUN_TAG.trace" 2>/dev/null | head -1)
SELF_OFF=$(grep -ao "gpu: viewmodel effects .*" "$SCRATCH/vm-selfillum-off.$RUN_TAG.trace" 2>/dev/null | head -1)
assert_eq "selfillum state (forced)" "$(rval "$SELF_ON" selfillumState)" "active"
assert_eq "selfillum declared (forced)" "$(rval "$SELF_ON" selfillumDeclared)" "1"
assert_eq "selfillum applied (forced)" "$(rval "$SELF_ON" selfillumApplied)" "1"
assert_eq "selfillum applied ranges (forced)" "$(rval "$SELF_ON" selfillumAppliedRanges)" "1"
assert_eq "selfillum force named (forced run)" \
  "$(rval "$(grep -ao 'gpu: viewmodel effects-switch .*' "$SCRATCH/vm-selfillum-on.$RUN_TAG.trace" 2>/dev/null | head -1)" selfillumForce)" "1"
assert_eq "selfillum switch forced off" \
  "$(rval "$(grep -ao 'gpu: viewmodel effects-switch .*' "$SCRATCH/vm-selfillum-off.$RUN_TAG.trace" 2>/dev/null | head -1)" selfillumSwitch)" "off"
assert_eq "selfillum state (forced then off)" "$(rval "$SELF_OFF" selfillumState)" "declared"
assert_eq "selfillum applied (forced then off)" "$(rval "$SELF_OFF" selfillumApplied)" "0"
# The four pixel claims. Each effect off must move the frame; the repeat must
# reproduce it exactly. Only the pair proves the movement is the effect's.
PHONG_DIFF=$(bmp_diff "$VM_NAT_BMP" "$VM_PHOFF_BMP")
BUMP_DIFF=$(bmp_diff "$VM_NAT_BMP" "$VM_BUOFF_BMP")
SELF_DIFF=$(bmp_diff "$VM_NAT_BMP" "$VM_SELF_BMP")
# The control pair: the SAME force with the effect switched back off must
# reproduce the natural frame exactly. Comparing it against the forced-on frame
# instead would measure the force flag rather than the effect, which is the
# mistake this line exists to avoid.
SELF_SWITCH_DIFF=$(bmp_diff "$VM_NAT_BMP" "$VM_SELOFF_BMP")
VM_REPEAT_DIFF=$(bmp_diff "$VM_NAT_BMP" "$VM_REP_BMP")
echo "  phong  off-vs-on: $PHONG_DIFF bytes"
echo "  bump   off-vs-on: $BUMP_DIFF bytes"
echo "  selfillum off-vs-on: $SELF_DIFF bytes"
echo "  selfillum forced-then-off vs natural: $SELF_SWITCH_DIFF bytes"
for pair in "phong:$PHONG_DIFF" "bump:$BUMP_DIFF" "selfillum:$SELF_DIFF"; do
  name="${pair%%:*}"; value="${pair#*:}"
  if [ "$value" -gt 1000 ] 2>/dev/null; then
    echo "  OK   $name-off != $name-on ($value bytes differ)"
  else
    echo "  FAIL $name-off == $name-on ($value bytes); the branch changed no pixel"; fail=1
  fi
done
# The selfillum control: the same force with the effect switched back off must
# reproduce the natural frame exactly. Without this, the 35646 bytes above could
# be the force flag rather than the effect.
assert_eq "selfillum force off == natural" "$SELF_SWITCH_DIFF" "0"
assert_eq "viewmodel natural == repeat" "$VM_REPEAT_DIFF" "0"

echo
echo "=== 6/6 T1 did not regress: the skybox and the pvs clip, re-run unchanged ==="
# The T2 pass added a water draw to draw() and a second world buffer. This
# section is the proof that neither moved what T1 owns: the same map, the same
# demo, the same switches world-material-check.sh uses, and the same readings.
run_product t1-sky "$DEMO" --world-sky
T1_TRACE=$PRODUCT_TRACE
T1_SKY=$(grep -ao "gpu: world sky .*" "$T1_TRACE" 2>/dev/null | head -1)
T1_VIS=$(grep -ao "gpu: world vis .*" "$T1_TRACE" 2>/dev/null | head -1)
if [ -n "$T1_SKY" ] && [ -n "$T1_VIS" ]; then
  printf '%s\n%s\n' "$T1_SKY" "$T1_VIS" > "$OUT/t1-regression.txt"
else
  echo "  FAIL the T1 regression run produced no reading"; fail=1
fi
assert_eq "sky state" "$(rval "$T1_SKY" state)" "active"
assert_eq "sky vertices" "$(rval "$T1_SKY" vertices)" "36"
assert_eq "sky triangles" "$(rval "$T1_SKY" triangles)" "12"
assert_eq "sky faces" "$(rval "$T1_SKY" faces)" "6"
assert_eq "sky draws" "$(rval "$T1_SKY" draws)" "1"
assert_eq "vis clip applied" "$(rval "$T1_VIS" clipApplied)" "1"
assert_eq "vis input triangles" "$(rval "$T1_VIS" inputTriangles)" "200000"
assert_eq "vis drawn triangles" "$(rval "$T1_VIS" drawnTriangles)" "170244"
assert_eq "vis hidden triangles" "$(rval "$T1_VIS" hiddenTriangles)" "29756"
# The identity T1 pinned, restated here because the water pass is the first
# thing since T1 to add a draw to the same function: a water pass that split the
# world buffer or reused its index buffer would break this and nothing else.
if [ "$(( $(rval "$T1_VIS" drawnTriangles) + $(rval "$T1_VIS" hiddenTriangles) ))" \
    -eq "$(( $(rval "$T1_VIS" inputTriangles) ))" ] 2>/dev/null; then
  echo "  OK   vis drawn + hidden = input ($(rval "$T1_VIS" drawnTriangles) + $(rval "$T1_VIS" hiddenTriangles) = $(rval "$T1_VIS" inputTriangles))"
else
  echo "  FAIL vis drawn + hidden != input"; fail=1
fi

echo
if [ "$MUTATION" -eq 1 ]; then
  # The cheap half first: values the run must NOT produce. Each is the pre-T2
  # reading, and a gate that still matched one would be asserting phase 1.
  echo "=== mutation: wrong expectations first, then four product mutations ==="
  if [ "$(rval "$EFFECTS" phongAppliedRanges)" = "0" ] \
     && [ "$(rval "$EFFECTS" phongDeclared)" = "1" ]; then
    echo "  FAIL phong is declared and applied by nothing -- that is the phase-1 state"
    exit 1
  fi
  if [ "$(rval "$ETEX" bumpTextureUploaded)" = "0" ]; then
    echo "  FAIL the bump texture reported itself unuploaded while the effect applied"
    exit 1
  fi
  if [ "$(rval "$WATER_ON" draws)" = "0" ]; then
    echo "  FAIL the water reported draws=0 while the switch was on"
    exit 1
  fi
  if [ "$(rval "$WATER_ON" faces)" = "0" ]; then
    echo "  FAIL the water reported no faces on a map that has 18"
    exit 1
  fi
  if [ "$(count "$EMUT" mutations)" = "99" ]; then
    echo "  FAIL the effect mutation count matched a value it should not have"
    exit 1
  fi

  # ---- Product mutation 1: the water draw deleted --------------------------
  # The guard is ANDed with false, so the buffer is uploaded, the state reads
  # active and the Draw never happens. That is the exact shape of the defect the
  # first version of this pass had, and the reading that catches it has to be
  # the draw count rather than the state word.
  run_mutated() { # run_mutated <name> <demo> [extra args...]
    local name="$1" demo="$2"; shift 2
    local trace="$SCRATCH/$name.$RUN_TAG.trace"
    rm -f "$trace" "$SCRATCH/$name.$RUN_TAG.bmp" "$SCRATCH/$name.$RUN_TAG.csv"
    TF2_NATIVE_TRACE="$(cygpath -m "$PWD/$trace")" \
      timeout 400 "$PRODUCT" --tf-root "$TF" --audio-device "$AUDIO_DEVICE" \
      --audio-disabled --hidden-window --demo "$demo" --capture-tick 0 \
      --capture-frame "$SCRATCH/$name.$RUN_TAG.bmp" \
      --metrics-file "$SCRATCH/$name.$RUN_TAG.csv" "$@" \
      > "$SCRATCH/$name.$RUN_TAG.log" 2>&1
    MUT_RC=$?
    MUT_TRACE=$trace
    if [ "$MUT_RC" != "0" ] || [ ! -s "$MUT_TRACE" ]; then
      echo "  MUTATION-SETUP=FAIL ($name: rc=$MUT_RC trace=$(stat -c%s "$MUT_TRACE" 2>/dev/null || echo missing))"
      return 1
    fi
    return 0
  }
  restore_source() { # restore_source <backup> <live> <name>
    cp "$1" "$2"
    if ! bash build-target.sh tf2_demo_native > "$SCRATCH/restore-$3.$RUN_TAG.build" 2>&1; then
      echo "  RESTORE-BUILD=FAIL ($3)"; return 1
    fi
    if ! cmp -s "$2" "$1"; then
      echo "  RESTORE=FAIL ($3: the source did not come back byte-identical)"; return 1
    fi
    rm -f "$1"
    if [ -e "$1" ]; then
      echo "  RESTORE=FAIL ($3: the backup could not be removed: $1)"; return 1
    fi
    return 0
  }

  WATER_BACKUP="$SCRATCH/native_renderer.cpp.water.backup"
  cp native/src/native_renderer.cpp "$WATER_BACKUP"
  sed -i 's|if (worldWaterEnabled_ \&\& worldWaterVertexBuffer_ \&\& worldWaterVertexCount_ > 0|if (false \&\& worldWaterEnabled_ \&\& worldWaterVertexBuffer_ \&\& worldWaterVertexCount_ > 0|' \
    native/src/native_renderer.cpp
  if cmp -s native/src/native_renderer.cpp "$WATER_BACKUP"; then
    echo "  MUTATION-SETUP=FAIL (water_draw_deleted: the sed changed nothing)"
    cp "$WATER_BACKUP" native/src/native_renderer.cpp
    echo "WORLD-MATERIAL-EFFECTS-MUTATION=FAIL"; exit 1
  fi
  if ! bash build-target.sh tf2_demo_native > "$SCRATCH/mutated-water_draw_deleted.$RUN_TAG.build" 2>&1; then
    echo "  MUTATION-SETUP=FAIL (water_draw_deleted: the rebuild failed)"
    restore_source "$WATER_BACKUP" native/src/native_renderer.cpp water || true
    echo "WORLD-MATERIAL-EFFECTS-MUTATION=FAIL"; exit 1
  fi
  if ! run_mutated mutated-water-draw-deleted "$DEMO" --start-paused --world-sky --world-water on; then
    restore_source "$WATER_BACKUP" native/src/native_renderer.cpp water || true
    echo "WORLD-MATERIAL-EFFECTS-MUTATION=FAIL"; exit 1
  fi
  MUT_WATER=$(grep -ao "gpu: world water .*" "$MUT_TRACE" 2>/dev/null | head -1)
  MUT_WATER_DRAWS=$(rval "$MUT_WATER" draws)
  MUT_WATER_STATE=$(rval "$MUT_WATER" state)
  if [ "$MUT_WATER_DRAWS" = "0" ] && [ "$MUT_WATER_STATE" = "active" ]; then
    echo "  MUTATION-CAUGHT=PASS (water_draw_deleted: draws=0 while the state still reads active)"
  else
    echo "  MUTATION-CAUGHT=FAIL (water_draw_deleted: draws=$MUT_WATER_DRAWS state=$MUT_WATER_STATE, the gate would have stayed green)"
    restore_source "$WATER_BACKUP" native/src/native_renderer.cpp water || true
    echo "WORLD-MATERIAL-EFFECTS-MUTATION=FAIL"; exit 1
  fi
  echo "$MUT_WATER" > "$OUT/mutated-water_draw_deleted.txt"
  restore_source "$WATER_BACKUP" native/src/native_renderer.cpp water || {
    echo "WORLD-MATERIAL-EFFECTS-MUTATION=FAIL"; exit 1; }

  # ---- Product mutation 2: the warp disabled -------------------------------
  # The amplitude is forced to zero, which is the identity transform. The draw
  # still happens, the state still reads active, the counters are all unchanged
  # -- and the picture is the un-warped surface. The reading that catches it is
  # `warp=0`, and the pair (draws=1, warp=0) is the state the reading exists for.
  MAIN_WARP_BACKUP="$SCRATCH/main.cpp.warp.backup"
  cp native/src/main.cpp "$MAIN_WARP_BACKUP"
  sed -i 's|waterRequest.warpAmplitude = waterEnabled ? waterVertexWarpAmplitude : 0.0f;|waterRequest.warpAmplitude = 0.0f;|' native/src/main.cpp
  if cmp -s native/src/main.cpp "$MAIN_WARP_BACKUP"; then
    echo "  MUTATION-SETUP=FAIL (water_warp_disabled: the sed changed nothing)"
    cp "$MAIN_WARP_BACKUP" native/src/main.cpp
    echo "WORLD-MATERIAL-EFFECTS-MUTATION=FAIL"; exit 1
  fi
  if ! bash build-target.sh tf2_demo_native > "$SCRATCH/mutated-water_warp_disabled.$RUN_TAG.build" 2>&1; then
    echo "  MUTATION-SETUP=FAIL (water_warp_disabled: the rebuild failed)"
    restore_source "$MAIN_WARP_BACKUP" native/src/main.cpp warp || true
    echo "WORLD-MATERIAL-EFFECTS-MUTATION=FAIL"; exit 1
  fi
  if ! run_mutated mutated-water-warp-disabled "$DEMO" --start-paused --world-sky --world-water on; then
    restore_source "$MAIN_WARP_BACKUP" native/src/main.cpp warp || true
    echo "WORLD-MATERIAL-EFFECTS-MUTATION=FAIL"; exit 1
  fi
  MUT_WARP=$(grep -ao "gpu: world water .*" "$MUT_TRACE" 2>/dev/null | head -1)
  MUT_WARP_APPLIED=$(rval "$MUT_WARP" warp)
  MUT_WARP_DRAWS=$(rval "$MUT_WARP" draws)
  if [ "$MUT_WARP_APPLIED" = "0" ] && [ "$MUT_WARP_DRAWS" = "1" ]; then
    echo "  MUTATION-CAUGHT=PASS (water_warp_disabled: draws=1 warp=0)"
  else
    echo "  MUTATION-CAUGHT=FAIL (water_warp_disabled: draws=$MUT_WARP_DRAWS warp=$MUT_WARP_APPLIED, the gate would have stayed green)"
    restore_source "$MAIN_WARP_BACKUP" native/src/main.cpp warp || true
    echo "WORLD-MATERIAL-EFFECTS-MUTATION=FAIL"; exit 1
  fi
  echo "$MUT_WARP" > "$OUT/mutated-water_warp_disabled.txt"
  restore_source "$MAIN_WARP_BACKUP" native/src/main.cpp warp || {
    echo "WORLD-MATERIAL-EFFECTS-MUTATION=FAIL"; exit 1; }

  # ---- Product mutation 3: the Phong parameter upload deleted --------------
  # `phongFeatures.x` is forced to zero, so the branch never runs however much
  # the material declares. The material readings are all unchanged -- declared,
  # parsed, uploaded -- and only `applied` moves, which is the split this round
  # exists to make visible.
  PHONG_BACKUP="$SCRATCH/native_renderer.cpp.phong.backup"
  cp native/src/native_renderer.cpp "$PHONG_BACKUP"
  sed -i 's|const bool phongOn = effects.shaderReady \&\& effects.phong.declared \&\& effects.phong.parsed;|const bool phongOn = false;|' \
    native/src/native_renderer.cpp
  if cmp -s native/src/native_renderer.cpp "$PHONG_BACKUP"; then
    echo "  MUTATION-SETUP=FAIL (phong_upload_deleted: the sed changed nothing)"
    cp "$PHONG_BACKUP" native/src/native_renderer.cpp
    echo "WORLD-MATERIAL-EFFECTS-MUTATION=FAIL"; exit 1
  fi
  if ! bash build-target.sh tf2_demo_native > "$SCRATCH/mutated-phong_upload_deleted.$RUN_TAG.build" 2>&1; then
    echo "  MUTATION-SETUP=FAIL (phong_upload_deleted: the rebuild failed)"
    restore_source "$PHONG_BACKUP" native/src/native_renderer.cpp phong || true
    echo "WORLD-MATERIAL-EFFECTS-MUTATION=FAIL"; exit 1
  fi
  if ! run_mutated mutated-phong-upload-deleted "$VM_DEMO" --start-paused \
      --viewmodel "$VM_MODEL" --viewmodel-fov 90 --viewmodel-render command \
      --viewmodel-material-slot 0; then
    restore_source "$PHONG_BACKUP" native/src/native_renderer.cpp phong || true
    echo "WORLD-MATERIAL-EFFECTS-MUTATION=FAIL"; exit 1
  fi
  MUT_PHONG=$(grep -ao "gpu: viewmodel effects .*" "$MUT_TRACE" 2>/dev/null | head -1)
  MUT_PHONG_DECL=$(rval "$MUT_PHONG" phongDeclared)
  MUT_PHONG_APPLIED=$(rval "$MUT_PHONG" phongApplied)
  if [ "$MUT_PHONG_DECL" = "1" ] && [ "$MUT_PHONG_APPLIED" = "0" ]; then
    echo "  MUTATION-CAUGHT=PASS (phong_upload_deleted: declared=1 applied=0)"
  else
    echo "  MUTATION-CAUGHT=FAIL (phong_upload_deleted: declared=$MUT_PHONG_DECL applied=$MUT_PHONG_APPLIED, the gate would have stayed green)"
    restore_source "$PHONG_BACKUP" native/src/native_renderer.cpp phong || true
    echo "WORLD-MATERIAL-EFFECTS-MUTATION=FAIL"; exit 1
  fi
  echo "$MUT_PHONG" > "$OUT/mutated-phong_upload_deleted.txt"
  restore_source "$PHONG_BACKUP" native/src/native_renderer.cpp phong || {
    echo "WORLD-MATERIAL-EFFECTS-MUTATION=FAIL"; exit 1; }

  # ---- Product mutation 4: the normal-map bind deleted ---------------------
  # The normal map is not bound, so the branch is off and the surface is lit
  # flat. `bumpTextureUploaded` stays 1 -- the upload happened -- and `applied`
  # moves, which is what says the bind is the thing that matters rather than the
  # texture existing.
  BUMP_BACKUP="$SCRATCH/native_renderer.cpp.bump.backup"
  cp native/src/native_renderer.cpp "$BUMP_BACKUP"
  sed -i 's|const bool bumpOn = effects.shaderReady \&\& effects.bump.declared \&\& effects.bump.parsed|const bool bumpOn = false \&\& effects.shaderReady \&\& effects.bump.declared \&\& effects.bump.parsed|' \
    native/src/native_renderer.cpp
  if cmp -s native/src/native_renderer.cpp "$BUMP_BACKUP"; then
    echo "  MUTATION-SETUP=FAIL (bump_bind_deleted: the sed changed nothing)"
    cp "$BUMP_BACKUP" native/src/native_renderer.cpp
    echo "WORLD-MATERIAL-EFFECTS-MUTATION=FAIL"; exit 1
  fi
  if ! bash build-target.sh tf2_demo_native > "$SCRATCH/mutated-bump_bind_deleted.$RUN_TAG.build" 2>&1; then
    echo "  MUTATION-SETUP=FAIL (bump_bind_deleted: the rebuild failed)"
    restore_source "$BUMP_BACKUP" native/src/native_renderer.cpp bump || true
    echo "WORLD-MATERIAL-EFFECTS-MUTATION=FAIL"; exit 1
  fi
  if ! run_mutated mutated-bump-bind-deleted "$VM_DEMO" --start-paused \
      --viewmodel "$VM_MODEL" --viewmodel-fov 90 --viewmodel-render command \
      --viewmodel-material-slot 0; then
    restore_source "$BUMP_BACKUP" native/src/native_renderer.cpp bump || true
    echo "WORLD-MATERIAL-EFFECTS-MUTATION=FAIL"; exit 1
  fi
  MUT_BUMP=$(grep -ao "gpu: viewmodel effects .*" "$MUT_TRACE" 2>/dev/null | head -1)
  MUT_BUMP_UPLOADED=$(rval "$(grep -ao 'gpu: viewmodel effects-texture .*' "$MUT_TRACE" 2>/dev/null | head -1)" bumpTextureUploaded)
  MUT_BUMP_APPLIED=$(rval "$MUT_BUMP" bumpApplied)
  if [ "$MUT_BUMP_UPLOADED" = "1" ] && [ "$MUT_BUMP_APPLIED" = "0" ]; then
    echo "  MUTATION-CAUGHT=PASS (bump_bind_deleted: textureUploaded=1 applied=0)"
  else
    echo "  MUTATION-CAUGHT=FAIL (bump_bind_deleted: textureUploaded=$MUT_BUMP_UPLOADED applied=$MUT_BUMP_APPLIED, the gate would have stayed green)"
    restore_source "$BUMP_BACKUP" native/src/native_renderer.cpp bump || true
    echo "WORLD-MATERIAL-EFFECTS-MUTATION=FAIL"; exit 1
  fi
  echo "$MUT_BUMP" > "$OUT/mutated-bump_bind_deleted.txt"
  restore_source "$BUMP_BACKUP" native/src/native_renderer.cpp bump || {
    echo "WORLD-MATERIAL-EFFECTS-MUTATION=FAIL"; exit 1; }

  # The forward sections ran BEFORE the mutations, and both verdicts have to
  # hold: the four mutations going red, and the six sections above staying green.
  # A caught mutation over a red gate is not a pass.
  if [ "$fail" -ne 0 ]; then
    echo "  the mutation half caught its four cases, but a forward assertion above"
    echo "  did not hold -- a caught mutation over a red gate is not a pass"
    echo "WORLD-MATERIAL-EFFECTS-MUTATION=FAIL"; exit 1
  fi
  echo "MUTATION-CAUGHT=PASS (five wrong expectations, the deleted water draw, the"
  echo "  disabled warp, the deleted phong upload and the deleted normal-map bind"
  echo "  each go red; every source came back byte-identical)"
  exit 0
fi

echo
if [ "$fail" -eq 0 ]; then
  echo "WORLD-MATERIAL-EFFECTS=PASS"
else
  echo "WORLD-MATERIAL-EFFECTS=FAIL"
fi
exit "$fail"
