#!/usr/bin/env bash
# weapon-material-check.sh -- the weapon body's own albedo, range by range.
#
# WHAT THIS GATE IS FOR
#   T14. The body is a model the weapon entity names (`c_pro_rifle.mdl` on the
#   pinned recording), and a weapon model is not one mesh: `c_pro_rifle.mdl`
#   draws two LOD-0 descriptors -- the rifle on material slot 0 and its scope on
#   slot 1 -- while the pass bound ONE texture for the whole buffer. On this
#   recording the two slots resolve to the SAME atlas, so the wrong binding is
#   invisible there. That is the shape of defect this project keeps finding: every
#   count is true and the picture is wrong the moment a weapon's slots differ.
#   So the gate states two properties:
#     * every range binds the material ITS OWN slot resolved, and
#     * a body whose slots resolve to different textures binds both of them.
#   The second one needs a body the recording does not carry, so the gate draws
#   `c_tfc_sniperrifle.mdl` -- a shipped sniper-rifle body whose two slots resolve
#   to two different VTFs -- through the same entity chain, with
#   `--viewmodel-weapon-model`. Every material line reports `override=` so an
#   overridden run can never be read as a natural one.
#
# WHAT EACH SECTION ESTABLISHES
#   0  the input is pinned by size and sha256, so a changed recording is a FAIL
#      rather than a silently different measurement.
#   1  the chain: weapon entity -> m_iWorldModelIndex -> MDL -> material slot ->
#      VMT -> VTF, read through the probe's runtime table (the resolver the
#      product binds from), for the recording's body and for the override body.
#   2  the product on the recording's body: two ranges, two slots bound, one
#      shared atlas -- the model's own content, stated rather than assumed.
#   3  the product on the override body: two ranges, two slots, TWO different
#      keys in range order.
#   3b a body that is not there: `fallback=buildFailed` with the path it could not
#      build and a reason, zero body draws, and a frame byte-identical to the
#      switched-off body's -- the refusal is a state with a name, not silence.
#   4  the picture: textured vs flat at one tick. The difference must be inside
#      the body's own coverage -- the union of the two body draws against the
#      body-off frame -- byte-identical across repeats, and zero in the world
#      strip above the composite. The coverage test is what makes "hands and
#      world unchanged" a measurement: a hand pixel is outside the body's
#      coverage, so a material change that reached it lands in `outside`.
#   5  the body's MDL is parsed and uploaded once, not per frame.
#   6  mutation (--mutation only): wrong-slot binding, missing texture binding,
#      material-state leakage. Each requires a NAMED reading to go red, a NAMED
#      control to hold, the frame to change, the source restored byte-for-byte
#      and the restored tree to rebuild.
#
# Usage: bash weapon-material-check.sh [--mutation]
#   --mutation prints MUTATION-CAUGHT=... rather than the ordinary verdict,
#   because under --mutation the forward readings are supposed to be red.
#
# Exit:  0 = every assertion held (or all three mutations were caught),
#        1 = it did not, 2 = another run owns the lock or an input is gone.
#
# One run at a time: the script launches the product several times and writes
# frames and traces under one directory. mkdir is atomic, so a second caller
# fails before it runs anything.
set -uo pipefail
cd "$(dirname "$0")"

# ---------------------------------------------------------------------------
# Arguments
# ---------------------------------------------------------------------------
MUTATION=0
while [ $# -gt 0 ]; do
  case "$1" in
    --mutation) MUTATION=1 ;;
    *) echo "unknown argument: $1"; exit 2 ;;
  esac
  shift
done

EXE=native/build-nmake/tf2_demo_native.exe
PROBE=native/build-nmake/weapon_body_probe.exe
TF="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
# The POV recording viewmodel-animation-check.sh, viewmodel-companion-check.sh and
# weapon-body-check.sh all pin, and the one the weapon chain was measured on.
POV_DEMO="$TF/demos/autorecord_2026-07-02_13-26-46.dem"
POV_SIZE=21856667
POV_SHA=c8a58501d222bdd9aec6db92d2782931a29a13d963a87364d603263e6dab091e

# The arms the pass is TOLD to draw. The arms are the control: their material
# state must not move when the body's does.
VM_MODEL="models/weapons/v_models/v_scattergun_scout.mdl"
# The body the recording's weapon entity names, and a second real body whose two
# material slots resolve to two different textures.
NATURAL_BODY="models/workshop/weapons/c_models/c_pro_rifle/c_pro_rifle.mdl"
OVERRIDE_BODY="models/weapons/c_models/c_tfc_sniperrifle/c_tfc_sniperrifle.mdl"
# A path this installation does not have. Section 3b asks for it to read the
# refusal, not to test the filesystem: what is being measured is that the run says
# `buildFailed` and why, rather than drawing a flat rifle and staying quiet.
MISSING_BODY="models/weapons/c_models/t14_no_such_body.mdl"
# Entity 1008 is a CTFSniperRifle and its m_iWorldModelIndex is 533, measured on
# 2026-10-12 and re-measured by this gate's probe on the final tree.
EXPECT_WEAPON_ENTITY=1008
EXPECT_WORLD_MODEL_INDEX=533
EXPECT_WEAPON_CLASS="CTFSniperRifle"
# The VTFs the two bodies' slots resolve to. The natural body's two slots share
# one atlas; the override body's do not, which is the whole reason it is here.
NATURAL_KEY="materials/models/workshop/weapons/c_models/c_pro_rifle/c_pro_rifle.vtf"
OVERRIDE_BODY_KEY="materials/models/weapons/c_items/c_tfc_sniperrifle.vtf"
OVERRIDE_SCOPE_KEY="materials/models/weapons/c_items/c_sniperrifle_scope.vtf"
# The arms' first range. The leakage mutation binds this where the body's own
# slot key belongs; the reading has to name it, or "the body used the hands'
# material" is an impression rather than a string.
ARM_FIRST_KEY="materials/models/player/sniper/sniper_red.vtf"
# The tick every frame is taken at. `--capture-tick N` lands on the FIRST draw at
# or past N, and on this machine that is 2000, 2001 or 2002 depending on load --
# two ticks of this recording differ by 1.5 million bytes, which is more than the
# body. Every frame is therefore retaken until it lands on the requested tick.
CAPTURE_TICK=2000
# The tick the probe reads the entity chain at. It is NOT the capture tick: the
# probe's snapshot lookup starts at the recording's first full snapshot, and
# asking it for tick 2000 answers "before-window" -- a fact about the probe's
# window, not about the chain. 16306 is the tick weapon-body-check.sh measured
# the same chain on (entity 1008, index 533), and the chain is the same one the
# frames at 2000 are drawn from.
CHAIN_TICK=16306
TRACE_DIR=evidence/weapon-material

# The textured/flat difference and the body's own coverage, measured on the final
# tree (2026-10-18, report section 4). The threshold is a floor, not the expected
# value: it is below the measurement so ordinary frame-to-frame jitter cannot
# redden it, and above zero so a material that never bound cannot pass it.
MIN_MATERIAL_PIXELS=5000
# The world strip above the composite. The body's own silhouette was measured to
# start at y=231 (x 659..707) on the final tree -- it is drawn edge-on, a
# pre-existing framing limitation this task records rather than fixes -- so the
# strip stops above it. A material change that reaches this strip is a leak into
# the world pass, not the body.
WORLD_STRIP="0,0,1264,220"

TRACE_DIR=${TRACE_DIR%$'\r'}
TF=${TF%$'\r'}
POV_DEMO=${POV_DEMO%$'\r'}
EXE=${EXE%$'\r'}
PROBE=${PROBE%$'\r'}

mkdir -p .scratch
LOCK_DIR=.scratch/weapon-material.lock
if ! mkdir "$LOCK_DIR" 2>/dev/null; then
  echo "WEAPON-MATERIAL=FAIL (another weapon-material-check run owns $LOCK_DIR)"
  exit 2
fi

# ---------------------------------------------------------------------------
# Source mutation plumbing
# ---------------------------------------------------------------------------
MUTATION_FILE=""
MUTATION_BAK=".scratch/weapon-material-mutation.bak"
MUTATION_ORIG_SHA=""
MUTATION_ACTIVE=0

restore_mutation() {
  if [ "$MUTATION_ACTIVE" -eq 1 ] && [ -n "$MUTATION_FILE" ] && [ -f "$MUTATION_BAK" ]; then
    cp "$MUTATION_BAK" "$MUTATION_FILE"
  fi
}
on_exit() {
  restore_mutation
  rmdir "$LOCK_DIR" 2>/dev/null || true
}
trap on_exit EXIT

PY=""
for candidate in \
  "C:/Users/Administrator/.workbuddy/binaries/python/versions/3.13.12/python.exe" \
  "C:/Users/Administrator/.workbuddy-ai/binaries/python/versions/3.13.12/python.exe" \
  python; do
  if command -v "$candidate" >/dev/null 2>&1 || [ -x "$candidate" ]; then PY="$candidate"; break; fi
done
[ -n "$PY" ] || { echo "FATAL: no python interpreter found"; exit 2; }

# The trace path is handed to a native Windows binary, which does not translate
# MSYS paths: "/d/..." is opened literally and the trace silently never opens.
ROOT=$(cygpath -m "$(pwd)" 2>/dev/null || pwd)
case "$ROOT" in
  /[a-zA-Z]/*) echo "FATAL: could not convert '$ROOT' to a Windows path"; exit 2 ;;
esac
mkdir -p "$TRACE_DIR"

# ---------------------------------------------------------------------------
# Assertions and trace readers
# ---------------------------------------------------------------------------
AUDIO_DEVICE=6
RUN_TIMEOUT=400

fail=0
FAIL_LINES=()
assert_eq() {
  if [ "${2:-<none>}" = "$3" ]; then
    echo "  OK   $1 = $3"
  else
    echo "  FAIL $1 = ${2:-<none>}, expected $3"; fail=1; FAIL_LINES+=("$1: ${2:-<none>} != $3")
  fi
}
assert_ne() {
  if [ -n "${2:-}" ] && [ "${2:-}" != "$3" ]; then
    echo "  OK   $1 = $2 (!= $3)"
  else
    echo "  FAIL $1 = ${2:-<none>}, expected anything but $3"; fail=1; FAIL_LINES+=("$1: ${2:-<none>} == $3")
  fi
}
assert_gt() {
  if [ "${2:-x}" -gt "$3" ] 2>/dev/null; then
    echo "  OK   $1 = $2 (> $3)"
  else
    echo "  FAIL $1 = ${2:-<none>}, expected > $3"; fail=1; FAIL_LINES+=("$1: ${2:-<none>} <= $3")
  fi
}
assert_ge() {
  if [ "${2:-x}" -ge "$3" ] 2>/dev/null; then
    echo "  OK   $1 = $2 (>= $3)"
  else
    echo "  FAIL $1 = ${2:-<none>}, expected >= $3"; fail=1; FAIL_LINES+=("$1: ${2:-<none>} < $3")
  fi
}
assert_contains() {
  case "${2:-}" in
    *"$3"*) echo "  OK   $1 contains $3" ;;
    *) echo "  FAIL $1 = ${2:-<none>}, expected to contain $3"
       fail=1; FAIL_LINES+=("$1: ${2:-<none>} missing $3") ;;
  esac
}
assert_not_contains() {
  case "${2:-}" in
    *"$3"*) echo "  FAIL $1 = ${2:-<none>}, expected NOT to contain $3"
            fail=1; FAIL_LINES+=("$1: ${2:-<none>} contains $3") ;;
    *) echo "  OK   $1 does not contain $3" ;;
  esac
}

# field <line> <name> -> the value of name=<value> in a trace line, "" if absent.
field() {
  printf '%s\n' "$1" | tr ' ' '\n' | sed -n "s/^$2=//p" | head -1
}
# byte_diff <a> <b> -> how many byte positions differ, or -1 if a file is missing.
byte_diff() {
  if [ ! -s "$1" ] || [ ! -s "$2" ]; then echo -1; return; fi
  cmp -l "$1" "$2" 2>/dev/null | wc -l | tr -d ' '
}
capture_tick_of() {
  grep -a "draw: begin tick=" "$TRACE_DIR/$1.trace" 2>/dev/null | tr -d '\r' | tail -1 \
    | sed -n 's/.*draw: begin tick=\([-0-9]*\).*/\1/p'
}

# ---------------------------------------------------------------------------
# Runs
# ---------------------------------------------------------------------------
LAST_RC=0
LAST_READING=""
LAST_MATERIAL=""

QUIET_FLAGS="--hidden-window --audio-disabled"

run_product() { # run_product <name> [extra...]
  local name="$1"; shift
  local bmp="$TRACE_DIR/$name.bmp"
  rm -f "$bmp" "$TRACE_DIR/$name.csv"
  TF2_NATIVE_TRACE="$ROOT/$TRACE_DIR/$name.trace" timeout "$RUN_TIMEOUT" "$EXE" \
    --tf-root "$TF" --audio-device "$AUDIO_DEVICE" $QUIET_FLAGS \
    --demo "$POV_DEMO" \
    --settings-file "$TRACE_DIR/$name-settings.ini" \
    --capture-frame "$bmp" --capture-tick "$CAPTURE_TICK" \
    --metrics-file "$TRACE_DIR/$name.csv" \
    --viewmodel "$VM_MODEL" --viewmodel-fov 90 "$@" > "$TRACE_DIR/$name.log" 2>&1
  LAST_RC=$?
  LAST_READING=$(grep -a "gpu: viewmodel weaponBodyReading" "$TRACE_DIR/$name.trace" 2>/dev/null \
    | tr -d '\r' | tail -1 | sed 's/^[0-9]\{1,\}\(\.[0-9]*\)\? //')
  LAST_MATERIAL=$(grep -a "gpu: viewmodel weaponBodyMaterial" "$TRACE_DIR/$name.trace" 2>/dev/null \
    | tr -d '\r' | tail -1 | sed 's/^[0-9]\{1,\}\(\.[0-9]*\)\? //')
  echo "  $name rc=$LAST_RC bmp=$(stat -c%s "$bmp" 2>/dev/null || echo 0)"
}

run_product_until_tick() { # run_product_until_tick <name> [extra...]
  # The wanted tick is always the gate's own CAPTURE_TICK; taking it from each
  # call site would let one section measure a different frame than another.
  local name="$1"; shift
  local wanted="$CAPTURE_TICK"
  local attempt got
  for attempt in 1 2 3 4 5 6 7 8; do
    run_product "$name" "$@"
    got=$(capture_tick_of "$name")
    if [ "$got" = "$wanted" ]; then
      echo "  ..   $name: frame landed on tick $got (attempt $attempt)"
      return 0
    fi
    echo "  ..   $name: frame landed on tick ${got:-<none>}, wanted $wanted (attempt $attempt)"
  done
  echo "  ..   $name: gave up after 8 attempts"
  fail=1; FAIL_LINES+=("$name: never landed on tick $wanted")
  return 1
}

run_probe() { # run_probe <name> [args...] -- JSON on stdout, checks on stderr
  local name="$1"; shift
  "$PROBE" "$@" > "$TRACE_DIR/$name.json" 2> "$TRACE_DIR/$name.txt"
  LAST_RC=$?
  echo "  $name rc=$LAST_RC checks_ok=$(grep -c '^  OK' "$TRACE_DIR/$name.txt" 2>/dev/null || true) checks_failed=$(grep -c '^  FAIL' "$TRACE_DIR/$name.txt" 2>/dev/null || true)"
}
probe_field() {
  "$PY" -c 'import json, sys
try:
    value = json.load(open(sys.argv[1], encoding="utf-8")).get(sys.argv[2], "")
    print(value if isinstance(value, str) else json.dumps(value))
except Exception:
    print("")' "$TRACE_DIR/$1.json" "$2"
}

# ---------------------------------------------------------------------------
# Section 0 -- the input
# ---------------------------------------------------------------------------
echo "=== 0/6 the input ==="
if [ ! -f "$POV_DEMO" ]; then
  echo "  FAIL the pinned recording is missing: $POV_DEMO"; exit 2
fi
assert_eq "the recording's size" "$(stat -c%s "$POV_DEMO")" "$POV_SIZE"
assert_eq "the recording's sha256" "$(sha256sum "$POV_DEMO" | cut -d' ' -f1)" "$POV_SHA"
assert_eq "the product binary exists" "$([ -x "$EXE" ] && echo yes || echo no)" "yes"
assert_eq "the probe binary exists" "$([ -x "$PROBE" ] && echo yes || echo no)" "yes"

# ---------------------------------------------------------------------------
# Section 1 -- the chain, on the recording and on the override body
# ---------------------------------------------------------------------------
echo
echo "=== 1/6 the chain (entity -> index -> MDL slot -> VMT -> VTF) ==="
run_probe natural --tf-root "$TF" --model "$NATURAL_BODY" --demo "$POV_DEMO" --tick "$CHAIN_TICK"
assert_eq "natural probe rc" "$LAST_RC" "0"
assert_eq "natural probe self-test failures" "$(probe_field natural checksFailed)" "0"
assert_eq "the weapon entity the recording names" "$(probe_field natural weaponEntity)" "$EXPECT_WEAPON_ENTITY"
assert_eq "its class" "$(probe_field natural weaponClass)" "$EXPECT_WEAPON_CLASS"
assert_eq "its m_iWorldModelIndex" "$(probe_field natural worldModelIndex)" "$EXPECT_WORLD_MODEL_INDEX"
assert_eq "the path that index resolves to" "$(probe_field natural worldModelPath)" "$NATURAL_BODY"
assert_eq "the natural body has two LOD-0 ranges" "$(probe_field natural rangeCount)" "2"
assert_eq "and its two slots resolve to one atlas" "$(probe_field natural rangeDistinctTextures)" "1"
assert_contains "the rifle's range resolves to its own VMT" \
  "$(cat "$TRACE_DIR/natural.txt")" "vmt=materials/models/workshop/weapons/c_models/c_pro_rifle/c_pro_rifle.vmt"
assert_contains "and its own VTF" \
  "$(cat "$TRACE_DIR/natural.txt")" "vtf=$NATURAL_KEY"
assert_contains "the scope's range resolves to its own VMT" \
  "$(cat "$TRACE_DIR/natural.txt")" "vmt=materials/models/workshop/weapons/c_models/c_pro_rifle/c_pro_rifle_scope.vmt"
assert_contains "and the shared VTF behind it (the model's content, not a binding)" \
  "$(cat "$TRACE_DIR/natural.txt")" "vtf=$NATURAL_KEY"

run_probe override --tf-root "$TF" --model "$OVERRIDE_BODY" --demo "$POV_DEMO" --tick "$CHAIN_TICK"
assert_eq "override probe rc" "$LAST_RC" "0"
assert_eq "override probe self-test failures" "$(probe_field override checksFailed)" "0"
assert_eq "the override body has two LOD-0 ranges" "$(probe_field override rangeCount)" "2"
assert_eq "and its two slots resolve to two different textures" \
  "$(probe_field override rangeDistinctTextures)" "2"
assert_contains "the override body's first range resolves to its own VTF" \
  "$(cat "$TRACE_DIR/override.txt")" "vtf=$OVERRIDE_BODY_KEY"
assert_contains "the override body's second range resolves to its own VTF" \
  "$(cat "$TRACE_DIR/override.txt")" "vtf=$OVERRIDE_SCOPE_KEY"

# ---------------------------------------------------------------------------
# Section 2 -- the product on the recording's own body
# ---------------------------------------------------------------------------
echo
echo "=== 2/6 the product, on the recording's body ==="
run_product_until_tick natural-on
assert_eq "natural-on rc" "$LAST_RC" "0"
assert_eq "the body's model is the one the entity named" \
  "$(field "$LAST_READING" weaponModelPath)" "$NATURAL_BODY"
assert_eq "the body resolved its two material slots" \
  "$(field "$LAST_READING" weaponMaterials)" "2/2/2/2"
assert_eq "the draw issued one call per LOD-0 range" "$(field "$LAST_MATERIAL" ranges)" "2"
assert_eq "both ranges bound a texture" "$(field "$LAST_MATERIAL" texturedRanges)" "2"
assert_eq "no range fell back" "$(field "$LAST_MATERIAL" fallbackRanges)" "0"
assert_eq "two distinct slots were bound" "$(field "$LAST_MATERIAL" boundSlots)" "2"
assert_eq "both keys are the shared atlas, in range order" \
  "$(field "$LAST_MATERIAL" keys)" "$NATURAL_KEY,$NATURAL_KEY"
assert_eq "no material was forced flat" "$(field "$LAST_MATERIAL" flatColor)" "0"
assert_eq "the run is natural (no model override)" "$(field "$LAST_MATERIAL" override)" "0"
assert_eq "the body's draw count matches the arms' (one each per frame)" \
  "$(field "$LAST_READING" weaponDrawCalls)" "$(field "$LAST_READING" armDrawCalls)"
assert_eq "the body's centre lands on screen" "$(field "$LAST_READING" weaponVisible)" "1"

# ---------------------------------------------------------------------------
# Section 3 -- the product on a body whose slots differ
# ---------------------------------------------------------------------------
echo
echo "=== 3/6 the product, on a body with two albedos ==="
run_product_until_tick override-on --viewmodel-weapon-model "$OVERRIDE_BODY"
assert_eq "override-on rc" "$LAST_RC" "0"
assert_eq "the body's model is the override" \
  "$(field "$LAST_READING" weaponModelPath)" "$OVERRIDE_BODY"
assert_eq "the override body resolved its two slots" \
  "$(field "$LAST_READING" weaponMaterials)" "2/2/2/2"
assert_eq "the draw issued one call per LOD-0 range" "$(field "$LAST_MATERIAL" ranges)" "2"
assert_eq "both ranges bound a texture" "$(field "$LAST_MATERIAL" texturedRanges)" "2"
assert_eq "no range fell back" "$(field "$LAST_MATERIAL" fallbackRanges)" "0"
assert_eq "two distinct slots were bound" "$(field "$LAST_MATERIAL" boundSlots)" "2"
assert_eq "each range bound ITS OWN slot's texture, in range order" \
  "$(field "$LAST_MATERIAL" keys)" "$OVERRIDE_BODY_KEY,$OVERRIDE_SCOPE_KEY"
assert_eq "the run reports its override" "$(field "$LAST_MATERIAL" override)" "1"
assert_eq "the body's draw count matches the arms'" \
  "$(field "$LAST_READING" weaponDrawCalls)" "$(field "$LAST_READING" armDrawCalls)"
assert_eq "the body's centre lands on screen" "$(field "$LAST_READING" weaponVisible)" "1"

# ---------------------------------------------------------------------------
# Section 3b -- a body that is not there
# ---------------------------------------------------------------------------
# The plan's fourth line: a missing resource gets an explicit fallback or refusal
# reason, not a silent flat rifle. The reason is read off the `scene: weaponBody`
# line, which is the contract that already names refusals for the world's models;
# the new material row is asserted only for what it adds (nothing was drawn).
echo
echo "=== 3b/6 a body that is not there ==="
run_product_until_tick missing-body --viewmodel-weapon-model "$MISSING_BODY"
assert_eq "a body that does not exist still produces a frame" "$LAST_RC" "0"
MISSING_SCENE=$(grep -a "scene: weaponBody " "$TRACE_DIR/missing-body.trace" 2>/dev/null \
  | tr -d '\r' | tail -1 | sed 's/^[0-9]\{1,\}\(\.[0-9]*\)\? //')
echo "  ..   $MISSING_SCENE"
assert_contains "the refusal names the fallback it took" "$MISSING_SCENE" "fallback=buildFailed"
assert_contains "and says which path it could not build" "$MISSING_SCENE" "path=$MISSING_BODY"
assert_contains "with a reason, not an empty field" "$MISSING_SCENE" \
  "reason=the weapon entity's own m_iWorldModelIndex names the weapon body"
assert_eq "no body draw was issued" "$(field "$LAST_READING" weaponDrawCalls)" "0"
assert_eq "the reading names the path it could not open" \
  "$(field "$LAST_READING" weaponModelPath)" "$MISSING_BODY"
assert_eq "the material row counts no range at all" "$(field "$LAST_MATERIAL" ranges)" "0"
assert_eq "and names no key" "$(field "$LAST_MATERIAL" keys)" "-"

# ---------------------------------------------------------------------------
# Section 4 -- the picture
# ---------------------------------------------------------------------------
echo
echo "=== 4/6 the picture ==="
run_product_until_tick override-on-repeat --viewmodel-weapon-model "$OVERRIDE_BODY"
assert_eq "repeat rc" "$LAST_RC" "0"
assert_eq "two runs of the same configuration, at the same tick, are byte-identical" \
  "$(byte_diff "$TRACE_DIR/override-on.bmp" "$TRACE_DIR/override-on-repeat.bmp")" "0"

run_product_until_tick override-flat --viewmodel-weapon-model "$OVERRIDE_BODY" \
  --viewmodel-weapon-flat-color
assert_eq "flat rc" "$LAST_RC" "0"
assert_eq "the flat control bound nothing" "$(field "$LAST_MATERIAL" texturedRanges)" "0"
assert_eq "and every range fell back" "$(field "$LAST_MATERIAL" fallbackRanges)" "2"
assert_eq "the control is reported as forced" "$(field "$LAST_MATERIAL" flatColor)" "1"

run_product_until_tick override-off --viewmodel-weapon-model "$OVERRIDE_BODY" \
  --viewmodel-weapon-body off
assert_eq "body-off rc" "$LAST_RC" "0"
assert_eq "with the body off, no range is drawn" "$(field "$LAST_MATERIAL" ranges)" "0"
assert_eq "and no draw call is issued" "$(field "$LAST_READING" weaponDrawCalls)" "0"

MATERIAL_DIFF=$(byte_diff "$TRACE_DIR/override-on.bmp" "$TRACE_DIR/override-flat.bmp")
BODY_DIFF=$(byte_diff "$TRACE_DIR/override-on.bmp" "$TRACE_DIR/override-off.bmp")
FLAT_BODY_DIFF=$(byte_diff "$TRACE_DIR/override-flat.bmp" "$TRACE_DIR/override-off.bmp")
assert_gt "texturing the body changes the picture" "$MATERIAL_DIFF" "0"
assert_gt "the body itself changes the picture when switched off" "$BODY_DIFF" "0"
assert_gt "the flat body also covers pixels the textured one may match" "$FLAT_BODY_DIFF" "0"
echo "  ..   byte_diff(on, flat)=$MATERIAL_DIFF byte_diff(on, off)=$BODY_DIFF byte_diff(flat, off)=$FLAT_BODY_DIFF"
# Section 3b's refusal, seen from the picture: a body that could not be built and a
# body that was switched off leave the same frame. The two differ in their reason
# string, which is the point -- "nothing was drawn" is the state, "buildFailed" is
# why, and neither is a rifle painted with whatever texture was bound last.
assert_eq "a body that is not there draws exactly what a switched-off body draws" \
  "$(byte_diff "$TRACE_DIR/missing-body.bmp" "$TRACE_DIR/override-off.bmp")" "0"
assert_gt "control -- and that frame is not the one with a body in it" \
  "$(byte_diff "$TRACE_DIR/missing-body.bmp" "$TRACE_DIR/natural-on.bmp")" "0"

MASK_LINE=$("$PY" bmp-diff-mask.py \
  "$TRACE_DIR/override-on.bmp" "$TRACE_DIR/override-flat.bmp" \
  --mask "$TRACE_DIR/override-on.bmp" "$TRACE_DIR/override-off.bmp" \
  --mask "$TRACE_DIR/override-flat.bmp" "$TRACE_DIR/override-off.bmp" \
  --max-outside 0 --min-changed "$MIN_MATERIAL_PIXELS")
MASK_RC=$?
echo "  ..   $MASK_LINE"
assert_eq "the material difference is inside the body's own coverage" "$MASK_RC" "0"
assert_contains "the mask report says so" "$MASK_LINE" "outside=0"
assert_contains "and the difference is not a rounding artefact" "$MASK_LINE" "status=PASS"
# The world strip above the composite. The mask test already covers this (the
# strip is outside the body's coverage), but naming it keeps the reading in the
# gate's own vocabulary rather than only in the tool's.
STRIP_LINE=$("$PY" bmp-pixel-diff.py "$TRACE_DIR/override-on.bmp" "$TRACE_DIR/override-flat.bmp" \
  --region "$WORLD_STRIP" --max-diff-percent 0)
STRIP_RC=$?
echo "  ..   $STRIP_LINE"
assert_eq "the world strip above the composite did not change" "$STRIP_RC" "0"

# ---------------------------------------------------------------------------
# Section 5 -- the body is built once, not per frame
# ---------------------------------------------------------------------------
echo
echo "=== 5/6 the body is not rebuilt per frame ==="
BODY_BUILDS=$(grep -ac "gpu: viewmodel weaponBody model=" "$TRACE_DIR/override-on.trace" 2>/dev/null || true)
assert_eq "the body's MDL was parsed and its mesh uploaded exactly once" "$BODY_BUILDS" "1"
BODY_READINGS=$(grep -ac "gpu: viewmodel weaponBodyMaterial " "$TRACE_DIR/override-on.trace" 2>/dev/null || true)
assert_ge "work count -- the material reading ran on every interval" "$BODY_READINGS" "1"
ARM_REQUESTS=$(grep -ac "gpu: viewmodel request path=" "$TRACE_DIR/override-on.trace" 2>/dev/null || true)
assert_eq "control -- the arms' model was built exactly once too" "$ARM_REQUESTS" "1"

# ---------------------------------------------------------------------------
# Section 6 -- three defects, each caught by a named reading and a named control
# ---------------------------------------------------------------------------
echo
echo "=== 6/6 the readings can go red ==="

patch_source() { # patch_source <find> <replace> <label>
  "$PY" - "$MUTATION_FILE" "$1" "$2" "$3" <<'PYEOF' || { echo "  FAIL $3: the mutation did not land"; fail=1; FAIL_LINES+=("$3: patch"); return 1; }
import io, sys
path, find, repl, label = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4]
raw = io.open(path, 'rb').read()
crlf = b'\r\n' in raw
data = raw.decode('utf-8')
if crlf:
    find = find.replace('\n', '\r\n'); repl = repl.replace('\n', '\r\n')
hits = data.count(find)
if hits != 1:
    sys.exit('the %s anchor matched %d times, expected exactly 1' % (label, hits))
io.open(path, 'wb').write(data.replace(find, repl, 1).encode('utf-8'))
PYEOF
}

begin_mutation() { # begin_mutation <file>
  MUTATION_FILE="$1"
  cp "$MUTATION_FILE" "$MUTATION_BAK"
  MUTATION_ORIG_SHA=$(git hash-object "$MUTATION_FILE")
  MUTATION_ACTIVE=1
}
end_mutation() { # end_mutation <label>
  local label="$1"
  cp "$MUTATION_BAK" "$MUTATION_FILE"
  MUTATION_ACTIVE=0
  local sha; sha=$(git hash-object "$MUTATION_FILE" 2>/dev/null || true)
  if [ "$sha" = "$MUTATION_ORIG_SHA" ]; then
    echo "  OK   $label: source restored byte-for-byte (${sha:0:12})"
  else
    echo "  FAIL $label: source was NOT restored byte-for-byte"
    fail=1; FAIL_LINES+=("$label: restore")
  fi
  if bash build-target.sh tf2_demo_native > ".scratch/weapon-material-restore-$label.log" 2>&1; then
    echo "  OK   $label: the restored tree rebuilt"
  else
    echo "  FAIL $label: the restored tree did not rebuild"
    fail=1; FAIL_LINES+=("$label: restore build")
  fi
}

if [ "$MUTATION" -eq 1 ]; then
  MUTATION_CATCHES=0
  MUTATION_TOTAL=0

  # The baseline the mutated frames are compared against. Captured here rather
  # than reused from a forward run: --mutation is a run of its own, and a
  # baseline frame left by a different invocation would not be the frame this
  # binary produces.
  echo "  -- baseline"
  run_product_until_tick mutation-baseline --viewmodel-weapon-model "$OVERRIDE_BODY"
  assert_eq "baseline rc" "$LAST_RC" "0"
  assert_eq "baseline keys" \
    "$(field "$LAST_MATERIAL" keys)" "$OVERRIDE_BODY_KEY,$OVERRIDE_SCOPE_KEY"
  cp "$TRACE_DIR/mutation-baseline.bmp" "$TRACE_DIR/mutation-baseline-keep.bmp"

  # ---- m1: wrong-slot binding ---------------------------------------------
  # Every range is collapsed onto the first range's slot and key: the shape of
  # "the mesh table said slot 0 for all of it", which is what the pass did
  # before the body had a range table. On this body it must reduce the bound
  # slots from two to one and replace the scope's key with the rifle's.
  echo
  echo "  -- m1 wrong-slot binding"
  MUTATION_TOTAL=$((MUTATION_TOTAL + 1))
  begin_mutation native/src/main.cpp
  patch_source \
'    viewModelWeaponBody.ranges = viewModelDrawRangesFor(request, viewModelWeaponFlatColor, -1);' \
'    viewModelWeaponBody.ranges = viewModelDrawRangesFor(request, viewModelWeaponFlatColor, -1);
            if (!viewModelWeaponBody.ranges.empty()) {
              for (auto& range : viewModelWeaponBody.ranges) {
                range.slot = viewModelWeaponBody.ranges.front().slot;
                range.materialKey = viewModelWeaponBody.ranges.front().materialKey;
              }
            }' m1 || true
  if bash build-target.sh tf2_demo_native > .scratch/weapon-material-m1-build.log 2>&1; then
    run_product_until_tick mutation-m1 --viewmodel-weapon-model "$OVERRIDE_BODY"
    M1_SLOTS=$(field "$LAST_MATERIAL" boundSlots)
    M1_KEYS=$(field "$LAST_MATERIAL" keys)
    M1_RANGES=$(field "$LAST_MATERIAL" ranges)
    M1_TEXTURED=$(field "$LAST_MATERIAL" texturedRanges)
    M1_FRAME=$(byte_diff "$TRACE_DIR/mutation-baseline-keep.bmp" "$TRACE_DIR/mutation-m1.bmp")
    caught_before=$fail
    assert_eq "m1: the bound slot count collapses to one" "$M1_SLOTS" "1"
    assert_eq "m1: every range now names the rifle's atlas" "$M1_KEYS" "$OVERRIDE_BODY_KEY,$OVERRIDE_BODY_KEY"
    assert_eq "m1: control -- the draw still issues one call per range" "$M1_RANGES" "2"
    assert_eq "m1: control -- both ranges still bound a texture" "$M1_TEXTURED" "2"
    assert_gt "m1: the picture changes with the wrong slot" "$M1_FRAME" "0"
    if [ "$fail" -eq "$caught_before" ]; then
      MUTATION_CATCHES=$((MUTATION_CATCHES + 1))
      echo "  OK   m1: caught"
    fi
  else
    echo "  FAIL m1: the mutated tree did not build"; fail=1; FAIL_LINES+=("m1: build")
  fi
  end_mutation m1

  # ---- m2: missing texture binding ----------------------------------------
  # The renderer still finds both keys, but only the first range binds one; the
  # second falls back to flat colour. This is the defect a "did the slot
  # resolve" reading cannot see: `weaponMaterials` still reads 2/2/2/2.
  echo
  echo "  -- m2 missing texture binding"
  MUTATION_TOTAL=$((MUTATION_TOTAL + 1))
  begin_mutation native/src/native_renderer.cpp
  patch_source \
'        const bool textured = cached != viewModelMaterialTextures_.end();
        weaponConstants.material = XMFLOAT4(textured ? 1.0f : 0.0f,' \
'        const bool textured = cached != viewModelMaterialTextures_.end()
          && viewModelWeaponTexturedRanges_ == 0;
        weaponConstants.material = XMFLOAT4(textured ? 1.0f : 0.0f,' m2 || true
  if bash build-target.sh tf2_demo_native > .scratch/weapon-material-m2-build.log 2>&1; then
    run_product_until_tick mutation-m2 --viewmodel-weapon-model "$OVERRIDE_BODY"
    M2_TEXTURED=$(field "$LAST_MATERIAL" texturedRanges)
    M2_FALLBACK=$(field "$LAST_MATERIAL" fallbackRanges)
    M2_KEYS=$(field "$LAST_MATERIAL" keys)
    M2_RANGES=$(field "$LAST_MATERIAL" ranges)
    M2_FRAME=$(byte_diff "$TRACE_DIR/mutation-baseline-keep.bmp" "$TRACE_DIR/mutation-m2.bmp")
    caught_before=$fail
    assert_eq "m2: one range lost its texture" "$M2_TEXTURED" "1"
    assert_eq "m2: and was counted as a fallback" "$M2_FALLBACK" "1"
    assert_eq "m2: the key list lost the scope's texture" "$M2_KEYS" "$OVERRIDE_BODY_KEY"
    assert_eq "m2: control -- both ranges were still drawn" "$M2_RANGES" "2"
    assert_gt "m2: the picture changes where the texture went missing" "$M2_FRAME" "0"
    if [ "$fail" -eq "$caught_before" ]; then
      MUTATION_CATCHES=$((MUTATION_CATCHES + 1))
      echo "  OK   m2: caught"
    fi
  else
    echo "  FAIL m2: the mutated tree did not build"; fail=1; FAIL_LINES+=("m2: build")
  fi
  end_mutation m2

  # ---- m3: material-state leakage from the hands --------------------------
  # The weapon's ranges bind the ARMS' first range key instead of their own:
  # the two draws share a texture cache, so this is what a reading with one
  # shared "current material" would produce. The body still draws two textured
  # ranges, so only the key list can tell it from a correct run.
  echo
  echo "  -- m3 material-state leakage"
  MUTATION_TOTAL=$((MUTATION_TOTAL + 1))
  begin_mutation native/src/native_renderer.cpp
  patch_source \
'        auto cached = range.materialKey.empty()
          ? viewModelMaterialTextures_.end()
          : viewModelMaterialTextures_.find(range.materialKey);' \
'        auto cached = range.materialKey.empty()
          ? viewModelMaterialTextures_.end()
          : viewModelMaterialTextures_.find(viewModelDrawRanges_.empty()
              ? range.materialKey : viewModelDrawRanges_.front().materialKey);' m3 || true
  if bash build-target.sh tf2_demo_native > .scratch/weapon-material-m3-build.log 2>&1; then
    run_product_until_tick mutation-m3 --viewmodel-weapon-model "$OVERRIDE_BODY"
    M3_KEYS=$(field "$LAST_MATERIAL" keys)
    M3_TEXTURED=$(field "$LAST_MATERIAL" texturedRanges)
    M3_RANGES=$(field "$LAST_MATERIAL" ranges)
    M3_FRAME=$(byte_diff "$TRACE_DIR/mutation-baseline-keep.bmp" "$TRACE_DIR/mutation-m3.bmp")
    caught_before=$fail
    assert_eq "m3: the body bound the sleeves' atlas twice" \
      "$M3_KEYS" "$ARM_FIRST_KEY,$ARM_FIRST_KEY"
    assert_not_contains "m3: the body no longer binds its own scope key" "$M3_KEYS" "$OVERRIDE_SCOPE_KEY"
    assert_eq "m3: control -- both ranges still bound a texture" "$M3_TEXTURED" "2"
    assert_eq "m3: control -- both ranges were still drawn" "$M3_RANGES" "2"
    assert_gt "m3: the picture changes when the hands' material leaks in" "$M3_FRAME" "0"
    if [ "$fail" -eq "$caught_before" ]; then
      MUTATION_CATCHES=$((MUTATION_CATCHES + 1))
      echo "  OK   m3: caught"
    fi
  else
    echo "  FAIL m3: the mutated tree did not build"; fail=1; FAIL_LINES+=("m3: build")
  fi
  end_mutation m3

  # ---- after every restore ------------------------------------------------
  run_product_until_tick mutation-restored --viewmodel-weapon-model "$OVERRIDE_BODY"
  assert_eq "after every restore, the keys are the body's own again" \
    "$(field "$LAST_MATERIAL" keys)" "$OVERRIDE_BODY_KEY,$OVERRIDE_SCOPE_KEY"
  assert_eq "after every restore, the picture is the one the baseline produced" \
    "$(byte_diff "$TRACE_DIR/mutation-baseline-keep.bmp" "$TRACE_DIR/mutation-restored.bmp")" "0"

  printf 'mutation summary: caught=%s of %s\n' "$MUTATION_CATCHES" "$MUTATION_TOTAL"
  assert_eq "mutations that were caught" "$MUTATION_CATCHES" "3"
  assert_eq "mutations that ran" "$MUTATION_TOTAL" "3"
  if [ "$fail" -eq 0 ]; then
    echo "MUTATION-CAUGHT=PASS (3 defects, each with its own named reading and control)"
    exit 0
  fi
  echo "MUTATION-CAUGHT=FAIL ($MUTATION_CATCHES/$MUTATION_TOTAL caught)"
  printf 'failed assertions:\n'
  printf '  %s\n' "${FAIL_LINES[@]}"
  exit 1
else
  # Section 6 is the only section that needs a rebuilt tree per defect, so it is
  # opt-in. Saying so here keeps the forward log from looking truncated.
  echo "  --   skipped: this run made no source mutations (pass --mutation to run them)"
fi

# ---------------------------------------------------------------------------
# The verdict
# ---------------------------------------------------------------------------
echo
if [ "$fail" -eq 0 ]; then
  echo "WEAPON-MATERIAL=PASS (every rendered range binds its own slot's albedo,"
  echo "                     the material change stays inside the body's coverage)"
  exit 0
fi
echo "WEAPON-MATERIAL=FAIL"
printf 'failed assertions:\n'
printf '  %s\n' "${FAIL_LINES[@]}"
exit 1
