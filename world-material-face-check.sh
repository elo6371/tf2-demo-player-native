#!/usr/bin/env bash
# world-material-face-check.sh -- T21: the world material chain, measured at a
# pixel.
#
# WHY THIS FILE EXISTS
# --------------------
# The world pass reports its material fetch as counts: `distinct=108 tiled=85
# capped=0 vmtMissing=3 vmtNoBase=20`. Those numbers say how many materials got
# a tile. They cannot say that the tile under a wall is the wall's own texture,
# that the uv which reached the shader is the face's own uv, or that a texture
# larger than its tile was reduced instead of repeated. T19's own report left
# exactly that gap open, and a fresh visual-baseline run on the integrated tree
# then read W1/W2 outside their pins -- a frame-wide change with no named cause.
#
# This gate closes the gap with one instrument: the product's per-face trace
# (`--world-face-trace`) walks point -> front-most drawn triangle -> material ->
# VMT store -> VTF store -> tile rectangle -> the texel the shader fetches, and
# predicts the pixel the shader should write. `face-trace-verify.py` reads the
# same points out of a captured frame and compares:
#
#   --world-albedo   the material path alone, before lighting. A pixel here is
#                    the texel the tile holds, so the comparison isolates
#                    material selection, atlas layout and uv normalisation.
#   textured         albedo * pow(lightmap, 1/2.2) * lighting. This one also
#                    fails if the prediction drops a term the shader applies.
#
# WHAT IT ASSERTS
# ---------------
#  1. the fetch counters add up: distinct == tiled + capped + vmtMissing +
#     vmtNoBase + vtfMissing + decodeEmpty (a material that vanishes into no
#     counter is the defect T19 removed at the cap and this keeps removed),
#  2. every material the atlas holds has one row, and every row with a batch
#     says `tiled` -- the table is the names behind the count,
#  3. every traced tiled face's uv lands inside its own tile rectangle, every
#     traced untiled face names a refusal (not `tiled`), and the run exercised
#     both an oversized (downscaled) tile and a material the old 64-tile
#     ceiling would have dropped,
#  4. the albedo frame matches the trace at every compared point (tol 3), and
#     the textured frame matches it too (tol 6),
#  5. the same capture repeated is byte-identical,
#  6. named refusal counts are printed, weighted by pvs-visible triangles.
#  7. every declared patch wrapper has one ledger line whose outcome is one of
#     the four named ones and the outcomes add up to the wrappers declared (the
#     old "resolved == declared" compared a counter with the value it had just
#     been incremented to), and
#  8. the worldspawn material the product names resolves a base texture -- that
#     path is the single-material upload, which no atlas counter covers.
#
# mutation: six source mutations, each of which must turn a forward pin red,
# and each source must come back byte-identical:
#   M1 restores the pre-T19 uv normaliser (tile size where the source size
#      belongs) -- every downscaled texture repeats, the albedo comparison must
#      go red on the oversized points,
#   M2 drops the tile origin from the uv -- every material samples tile 0, the
#      albedo comparison must go red,
#   M3 drops the lightmap term from the prediction -- the textured comparison
#      must go red while albedo stays green (so the textured pin is not the
#      albedo pin twice),
#   M4 makes the tile table call a refused material `tiled` -- the table/name
#      assertion must go red.
#   M5 stops following the include -- every wrapper keeps its own root, so the
#      patch section has no ledger to close,
#   M6 puts the map material valid flag back to its default -- the title maptex
#      reading must fall back, which is the regression 788f95c shipped.
#
# Usage: bash world-material-face-check.sh [--mutation]
# Exit:  0 = every assertion held (or, with --mutation, every mutation went red
#            and every source came back byte-identical).
set -uo pipefail
cd "$(dirname "$0")"

PRODUCT=native/build-nmake/tf2_demo_native.exe
TF="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
DEMO="$TF/demos/autorecord_2026-07-02_13-26-46.dem"
PYTHON="${PYTHON:-python}"

RUN_TAG="$(date +%Y%m%d%H%M%S)-$$"
MODE="${1:-}"
SCRATCH=".scratch/world-material-face.$RUN_TAG"

# T86/D-22: the product writes settings.ini and ui.ini on every normal exit.
# Both flags below name per-run copies outside the tree: a gate must not edit
# tracked evidence or the machine profile. gate-launch-hygiene-check.sh asserts
# the static shape; this block is the runtime side of the same contract.
ISO_DIR="$(mktemp -d)"
ISO_DIR_WIN="$(cygpath -m "$ISO_DIR")"
ISO_SETTINGS="$ISO_DIR_WIN/settings.ini"
ISO_UI="$ISO_DIR_WIN/ui.ini"
trap 'rm -rf "${ISO_DIR:-}"' EXIT
HOST_PROFILE_DIR="${LOCALAPPDATA:-${HOME}/AppData/Local}/TF2 Demo Player"
if [ -f "$HOST_PROFILE_DIR/settings.ini" ]; then cp "$HOST_PROFILE_DIR/settings.ini" "$ISO_DIR/settings.ini"; else : > "$ISO_DIR/settings.ini"; echo "  ISO SEED absent: host profile settings.ini (product starts from defaults)" >&2; fi
if [ -f "$HOST_PROFILE_DIR/ui.ini" ]; then cp "$HOST_PROFILE_DIR/ui.ini" "$ISO_DIR/ui.ini"; else : > "$ISO_DIR/ui.ini"; fi

OUT="evidence/world-material-face"
mkdir -p "$SCRATCH" "$OUT"

fail=0
assert_eq() {
  if [ "$2" = "$3" ]; then echo "  OK $1 = $2"; else echo "  FAIL $1: got '$2' want '$3'"; fail=1; fi
}
assert_gt() {
  if [ "$2" -gt "$3" ]; then echo "  OK $1 = $2 > $3"; else echo "  FAIL $1: got $2, want > $3"; fail=1; fi
}
trace_field() { # trace_field <trace> <line-prefix> <key>
  grep "$2" "$1" 2>/dev/null | head -1 | grep -oE "$3=[-0-9.]+" | head -1 | cut -d= -f2
}

capture() { # capture <bmp> <trace> [extra flags...]
  local bmp="$1" trace="$2"; shift 2
  local traceWin bmpWin attempt rc tick
  traceWin="$(cygpath -m "$PWD/$trace")"
  bmpWin="$(cygpath -m "$PWD/$bmp")"
  for attempt in 1 2 3 4 5 6 7 8; do
    rm -f "$bmp"
    # T29: force-killable, for the load-time deadlock measured on this host.
    TF2_NATIVE_TRACE="$traceWin" timeout -k 5 150 "$PRODUCT" --settings-file "$ISO_SETTINGS" --ui-settings-file "$ISO_UI" --tf-root "$TF" --demo "$DEMO" \
      --hidden-window --capture-tick 2000 --capture-frame "$bmpWin" --audio-disabled \
      --viewmodel models/weapons/v_models/v_scattergun_scout.mdl --viewmodel-render companion \
      --world-water off --world-face-trace "$@" > "$SCRATCH/capture.$RUN_TAG.log" 2>&1
    rc=$?
    tick=$(grep -a "draw: begin tick=" "$trace" 2>/dev/null | tr -d '\r' | tail -1 \
      | sed -n 's/.*tick=\([-0-9]*\).*/\1/p')
    if [ "$rc" = 0 ] && [ "$tick" = "2000" ] && [ -s "$bmp" ]; then return 0; fi
    echo "  (capture attempt $attempt: rc=$rc tick=${tick:-none}, retrying for tick 2000)"
  done
  return 1
}

echo "== 0. build =="
bash build-target.sh tf2_demo_native > "$SCRATCH/build.$RUN_TAG.log" 2>&1
if [ $? -ne 0 ]; then
  echo "  FAIL build; see $SCRATCH/build.$RUN_TAG.log"
  tail -20 "$SCRATCH/build.$RUN_TAG.log"
  exit 1
fi
echo "  OK build"
EXE_HASH=$(sha256sum "$PRODUCT" | cut -d' ' -f1)
SOURCE_HASH=$(git hash-object native/src/main.cpp native/src/world_tile_atlas.cpp \
  native/include/native_renderer.h native/src/renderer_internal.h \
  native/src/native_renderer.cpp \
  native/src/native_renderer_camera.cpp native/src/native_renderer_capture.cpp \
  native/src/native_renderer_entity.cpp native/src/native_renderer_frame.cpp \
  native/src/native_renderer_overlay.cpp native/src/native_renderer_settings.cpp \
  native/src/native_renderer_viewmodel.cpp native/src/native_renderer_world.cpp \
  native/src/native_renderer_world_material.cpp | tr '\n' ' ')
HEAD_SHA=$(git rev-parse HEAD)
echo "  head=$HEAD_SHA exe=$EXE_HASH"
{
  echo "head=$HEAD_SHA"
  echo "exe_sha256=$EXE_HASH"
  echo "source_blob_hashes=$SOURCE_HASH"
} > "$OUT/identity.txt"

# ---------------------------------------------------------------------------
# 1. the reference frame, in the material path and in the textured path
# ---------------------------------------------------------------------------
echo "== 1. captures (tick 2000, the acceptance camera) =="
REF_BMP="$SCRATCH/ref.$RUN_TAG.bmp";   REF_TRACE="$SCRATCH/ref.$RUN_TAG.trace"
ALB_BMP="$SCRATCH/albedo.$RUN_TAG.bmp"; ALB_TRACE="$SCRATCH/albedo.$RUN_TAG.trace"
REP_BMP="$SCRATCH/repeat.$RUN_TAG.bmp"; REP_TRACE="$SCRATCH/repeat.$RUN_TAG.trace"
capture "$REF_BMP" "$REF_TRACE" || { echo "  FAIL textured capture"; exit 1; }
capture "$ALB_BMP" "$ALB_TRACE" --world-albedo || { echo "  FAIL albedo capture"; exit 1; }
capture "$REP_BMP" "$REP_TRACE" || { echo "  FAIL repeat capture"; exit 1; }
# The committed trace is the evidence a reader re-checks, not the whole log. Each
# full trace is 5-6 MB, is written once per captured frame (24k `draw: begin tick=`
# lines in one capture) and every line carries a timestamp, so committing it whole
# makes each run a 59k-line diff in which nothing is read. Keep exactly the lines
# this gate parses, plus the LAST frame's start line; the working trace keeps
# everything. Measured on m3's trace: 126,621 lines -> ~140.
trim_trace() { # trim_trace <src> <dst>
  { grep -a 'draw: begin tick=' "$1" | tail -1
    grep -a -E 'scene: camera|gpu: world material-fetch |gpu: world tile material=|gpu: world transform-refused material=|gpu: world refused material=|gpu: world face-trace point=|gpu: world face-trace-summary |gpu: world visible-refusal ' "$1"
  } > "$2"
  printf 'trace %s kept %s of %s lines\n' "$(basename "$2")" "$(wc -l < "$2")" "$(wc -l < "$1")" >> "$OUT/trace-trim.txt"
}
trim_trace "$REF_TRACE" "$OUT/trace-textured.txt"
trim_trace "$ALB_TRACE" "$OUT/trace-albedo.txt"
if cmp -s "$REF_BMP" "$REP_BMP"; then
  echo "  OK repeat capture byte-identical ($(sha256sum "$REF_BMP" | cut -d' ' -f1))"
else
  echo "  FAIL repeat capture differs"; fail=1
fi
echo "  frame=$(sha256sum "$REF_BMP" | cut -d' ' -f1) albedoFrame=$(sha256sum "$ALB_BMP" | cut -d' ' -f1)"

# ---------------------------------------------------------------------------
# 2. the fetch counters describe a complete partition
# ---------------------------------------------------------------------------
echo "== 2. material fetch: every distinct material has one outcome =="
MATFETCH=$(grep -o "gpu: world material-fetch .*" "$REF_TRACE" | head -1)
[ -n "$MATFETCH" ] || { echo "  FAIL no material-fetch line"; exit 1; }
DISTINCT=$(printf '%s' "$MATFETCH" | grep -oE 'distinct=[0-9]+' | cut -d= -f2)
TILED=$(printf '%s' "$MATFETCH" | grep -oE 'tiled=[0-9]+' | cut -d= -f2)
CAPPED=$(printf '%s' "$MATFETCH" | grep -oE 'capped=[0-9]+' | cut -d= -f2)
VMTMISS=$(printf '%s' "$MATFETCH" | grep -oE 'vmtMissing=[0-9]+' | cut -d= -f2)
VMTNOBASE=$(printf '%s' "$MATFETCH" | grep -oE 'vmtNoBase=[0-9]+' | cut -d= -f2)
VTFMISS=$(printf '%s' "$MATFETCH" | grep -oE 'vtfMissing=[0-9]+' | cut -d= -f2)
DECODEEMPTY=$(printf '%s' "$MATFETCH" | grep -oE 'decodeEmpty=[0-9]+' | cut -d= -f2)
# T59: patch wrappers are a fifth outcome, not a flavour of no-basetexture. A
# wrapper is a material whose only $basetexture lives in the material it includes,
# so before T59 every one of them was counted as a material that declares no base
# texture at all -- which read as a map defect and was in fact a parser gap.
PATCHDECL=$(printf '%s' "$MATFETCH" | grep -oE 'patchDeclared=[0-9]+' | cut -d= -f2)
PATCHRES=$(printf '%s' "$MATFETCH" | grep -oE 'patchResolved=[0-9]+' | cut -d= -f2)
PATCHUNP=$(printf '%s' "$MATFETCH" | grep -oE 'patchUnparsable=[0-9]+' | cut -d= -f2)
TRANSFORM=$(printf '%s' "$MATFETCH" | grep -oE 'transformRefused=[0-9]+' | cut -d= -f2)
echo "  $MATFETCH"
assert_eq "fetch partitions distinct" "$((TILED + CAPPED + VMTMISS + VMTNOBASE + VTFMISS + DECODEEMPTY + TRANSFORM))" "$DISTINCT"
assert_gt "materials tiled" "$TILED" 0
# The atlas is handed exactly the materials the fetch tiled, so the table has
# one row per tiled material and no others. A material that disappears between
# the fetch and the plan is the silent drop this task exists to make impossible.
TILE_ROWS=$(grep -c "gpu: world tile material=" "$REF_TRACE")
TILE_TILED=$(grep "gpu: world tile material=" "$REF_TRACE" | grep -c "reason=tiled")
assert_eq "tile table rows" "$TILE_ROWS" "$TILED"
assert_eq "tile table tiled rows" "$TILE_TILED" "$TILED"
# Every tiled row carries the numbers the trace and the uv need.
BAD_ROWS=$(grep "gpu: world tile material=" "$REF_TRACE" | grep "reason=tiled" \
  | grep -cE "batch=-1|tile=-1|size=0x")
assert_eq "tile rows with a rectangle" "$BAD_ROWS" "0"

# ---------------------------------------------------------------------------
# 2b. patch wrappers: the include is followed, and what cannot be followed is named
# ---------------------------------------------------------------------------
# A patch VMT is the map's own per-face wrapper: header patch, an include naming
# the material that actually has the texture, and a replace block of per-face
# overrides. pl_odyssey ships 19 of them. Following the include is what turns the
# 20 no-basetexture refusals of the T44 account into the 2 that are genuinely
# texture-less (the water shader, whose surface is drawn by the water pass).
echo "== 2b. patch wrappers: include followed, the rest refused by name =="
assert_gt "patch wrappers declared" "$PATCHDECL" 0
assert_eq "patch wrappers that could not be resolved" "$PATCHUNP" "0"
# The ledger: every declared wrapper must appear once, with an outcome a reader can
# name. "resolved == declared" alone is a tautology -- main.cpp counts a wrapper
# resolved as soon as its include is read, before the transform check and before
# tiling -- so the closure below is the assertion that carries weight.
PATCH_LINES=$(grep -c "gpu: world patch material=" "$REF_TRACE")
assert_eq "declared wrappers with one ledger line each" "$PATCH_LINES" "$PATCHDECL"
PATCH_TILED=$(grep "gpu: world patch material=" "$REF_TRACE" | grep -c "outcome=tiled")
assert_gt "wrappers that reached the atlas" "$PATCH_TILED" 0
PATCH_TRANSFORM=$(grep "gpu: world patch material=" "$REF_TRACE" | grep -c "outcome=basetexture-transform")
assert_eq "wrappers refused for their transform, one per transform refusal" "$PATCH_TRANSFORM" "$TRANSFORM"
PATCH_UNRES=$(grep "gpu: world patch material=" "$REF_TRACE" | grep -c "outcome=patch-unresolved")
assert_eq "wrappers whose include failed, one per unparsable wrapper" "$PATCH_UNRES" "$PATCHUNP"
PATCH_NOBASE=$(grep "gpu: world patch material=" "$REF_TRACE" | grep -c "outcome=no-basetexture")
PATCH_OTHER=$(grep "gpu: world patch material=" "$REF_TRACE" | grep -vcE "outcome=(tiled|basetexture-transform|patch-unresolved|no-basetexture)")
PATCH_UNNAMED=$(grep "gpu: world patch material=" "$REF_TRACE" | grep -c "outcome=not-reached")
assert_eq "wrappers left without an outcome" "$PATCH_UNNAMED" "0"
assert_eq "wrappers whose outcome is none of the four named ones" "$PATCH_OTHER" "0"
assert_eq "the wrapper outcomes add up to the wrappers declared" \
  "$((PATCH_TILED + PATCH_TRANSFORM + PATCH_UNRES + PATCH_NOBASE + PATCH_OTHER))" "$PATCHDECL"
# A wrapper whose material asks for a non-identity basetexturetransform is drawn
# by TF2 with its uv re-mapped. The world pass samples the face texinfo axes, so
# this pass cannot put that texture where TF2 does; it must refuse by name rather
# than draw it axis-aligned and call the picture correct.
assert_eq "materials refused for a non-identity basetexturetransform" "$TRANSFORM" "4"
TRANSFORM_LINES=$(grep -c "gpu: world transform-refused material=" "$REF_TRACE")
assert_eq "transform refusals traced, one per material" "$TRANSFORM_LINES" "$TRANSFORM"
TRANSFORM_NAMED=$(grep "gpu: world transform-refused material=" "$REF_TRACE" | grep -c "rotate 45")
assert_eq "transform refusals that print the transform they refused" "$TRANSFORM_NAMED" "$TRANSFORM"
# The fourteen materials that exist only inside an include must now be tiled: a
# resolved wrapper that still reaches the atlas with no batch is the defect
# wearing the fix.
PATCHED_ROWS=$(grep "gpu: world tile material=" "$REF_TRACE" | grep -cE "metalchrome002|floor_tile_004")
assert_eq "materials that only exist through their include, now tiled" "$PATCHED_ROWS" "14"
# And the refusal that must survive: the water shader genuinely has no base
# texture, so a fix that made IT tiled would be a fix that draws something false.
WATER_REFUSED=$(grep "gpu: world refused material=" "$REF_TRACE" | grep "reason=no-basetexture" | grep -c "water_well")
assert_eq "water shader materials still refused as no-basetexture" "$WATER_REFUSED" "2"
# The map material: the demo names a worldspawn material and the single-material
# upload path resolves its base texture from it. That path is only reached when the
# atlas is absent, so no counter in the atlas path covers it -- the window title is
# where the product prints the reading, and "maptex: fallback" is what a dropped
# resolution looks like.
MAPMATEX=$(grep -a -m1 "maptex: " "$REF_TRACE" | sed -E "s/.*(mapmat: [^,]+).*(maptex: [^]]+)\].*/\1 \2/")
printf '%s\n' "$MAPMATEX" > "$OUT/map-material.txt"
MAPMAT=$(printf '%s' "$MAPMATEX" | sed -n -E "s/^mapmat: (.*) maptex: .*$/\1/p")
MAPTEX=$(printf '%s' "$MAPMATEX" | sed -n -E "s/^.* maptex: (.*)$/\1/p")
if [ -n "$MAPMAT" ] && [ "$MAPMAT" != "none" ]; then
  echo "  OK the demo's map material is named ($MAPMAT)"
else
  echo "  FAIL the demo's map material is not named (read: '$MAPMAT')"; fail=1
fi
case "$MAPTEX" in
  [0-9]*x[0-9]*) echo "  OK the map material resolves a base texture ($MAPTEX)" ;;
  *) echo "  FAIL the map material resolved no base texture (read: '$MAPTEX')"; fail=1 ;;
esac

# ---------------------------------------------------------------------------
# 3. the face trace: the chain has no hole, and both cases were exercised
# ---------------------------------------------------------------------------
echo "== 3. face trace: every visible face has a named outcome =="
FACES=$(grep -c "gpu: world face-trace point=" "$REF_TRACE")
SUMMARY=$(grep -o "gpu: world face-trace-summary .*" "$REF_TRACE" | head -1)
[ -n "$SUMMARY" ] || { echo "  FAIL no face-trace summary"; fail=1; }
echo "  $SUMMARY"
SUMMARY_POINTS=$(printf '%s' "$SUMMARY" | grep -oE 'points=[0-9]+' | cut -d= -f2)
assert_eq "face trace lines" "$FACES" "$SUMMARY_POINTS"
assert_gt "faces with a drawn triangle" "$(printf '%s' "$SUMMARY" | grep -oE 'hits=[0-9]+' | cut -d= -f2)" 0
assert_gt "oversized (downscaled) faces traced" "$(printf '%s' "$SUMMARY" | grep -oE 'oversized=[0-9]+' | cut -d= -f2)" 0
assert_gt "faces the old 64-tile order would have dropped" "$(printf '%s' "$SUMMARY" | grep -oE 'beyondOldCap=[0-9]+' | cut -d= -f2)" 0
# A tiled face whose uv landed outside its own rectangle is a wrong tile; an
# untiled face whose reason says `tiled` is a name that does not exist.
OUTSIDE=$(grep "gpu: world face-trace point=" "$REF_TRACE" | grep -v "tile=-1/-1" | grep -c "uvInside=0")
assert_eq "tiled faces with their uv inside their tile" "$OUTSIDE" "0"
LYING=$(grep "gpu: world face-trace point=" "$REF_TRACE" | grep "tile=-1/-1" | grep -c "reason=tiled")
assert_eq "untiled faces claiming a tile" "$LYING" "0"
# Every sampled texel is a real sample: `texel=-` on a tiled face means the CPU
# could not reproduce what the shader fetched, which is the failure mode this
# trace exists to expose.
NOTEXEL=$(grep "gpu: world face-trace point=" "$REF_TRACE" | grep -v "tile=-1/-1" | grep -c "texel=-")
assert_eq "tiled faces without a sampled texel" "$NOTEXEL" "0"

# ---------------------------------------------------------------------------
# 4. the pixel is the texel: albedo and textured comparisons
# ---------------------------------------------------------------------------
echo "== 4. the captured pixel is the predicted one =="
"$PYTHON" face-trace-verify.py --trace "$REF_TRACE" --frame "$REF_BMP" --mode textured \
  --min-points 12 > "$SCRATCH/face-textured.$RUN_TAG.txt" 2>&1
TEXTURED_RC=$?
cp "$SCRATCH/face-textured.$RUN_TAG.txt" "$OUT/face-textured.txt"
tail -1 "$SCRATCH/face-textured.$RUN_TAG.txt"
assert_eq "textured face comparison" "$TEXTURED_RC" "0"
"$PYTHON" face-trace-verify.py --trace "$ALB_TRACE" --frame "$ALB_BMP" --mode albedo \
  --min-points 12 > "$SCRATCH/face-albedo.$RUN_TAG.txt" 2>&1
ALBEDO_RC=$?
cp "$SCRATCH/face-albedo.$RUN_TAG.txt" "$OUT/face-albedo.txt"
tail -1 "$SCRATCH/face-albedo.$RUN_TAG.txt"
assert_eq "albedo face comparison" "$ALBEDO_RC" "0"

# The rival hypothesis. The trace carries a second prediction for the same
# pixel, computed through the uv the renderer used BEFORE this task -- the tile
# size where the source size belongs -- and nothing else changed. A green
# albedo pin above and a green rival here would say the pin discriminates
# nothing, so the rival must be rejected here and must be accepted under M1
# below, where the old uv is actually in the build.
"$PYTHON" face-trace-verify.py --trace "$ALB_TRACE" --frame "$ALB_BMP" --mode albedo-old \
  --min-points 12 > "$SCRATCH/face-albedo-old.$RUN_TAG.txt" 2>&1
RIVAL_RC=$?
cp "$SCRATCH/face-albedo-old.$RUN_TAG.txt" "$OUT/face-albedo-rival.txt"
tail -1 "$SCRATCH/face-albedo-old.$RUN_TAG.txt"
if [ "$RIVAL_RC" = "0" ]; then
  echo "  FAIL the rival uv also matched this frame, so the albedo pin proves nothing"; fail=1
else
  echo "  OK the rival uv is rejected on the fixed build"
fi
# ...and the rejection has to be measured on points where the two predictions
# actually differ. `discriminating` counts those; a run where it is 0 would
# reject the rival on no evidence at all.
DISCRIM=$(printf '%s' "$SUMMARY" | grep -oE 'discriminating=[0-9]+' | cut -d= -f2)
assert_gt "points where the two uv predictions differ" "$DISCRIM" 0

# ---------------------------------------------------------------------------
# 5. the frame's own numbers, recorded (not asserted against the old pins)
# ---------------------------------------------------------------------------
# T21 does not re-pin W1/W2: the point of the task is to explain the delta, and
# a gate that asserted the new numbers would be asserting the symptom. They are
# written down next to the frame so the report's before/after claim has a
# machine-readable row per run, and so a future change to them is visible.
echo "== 5. region readings (recorded) =="
"$PYTHON" frame-region-stats.py "$REF_BMP" \
  0,0,420,200 0,560,420,121 1100,100,164,200 600,0,64,60 > "$SCRATCH/regions.$RUN_TAG.txt" 2>&1
cp "$SCRATCH/regions.$RUN_TAG.txt" "$OUT/regions.txt"
cat "$SCRATCH/regions.$RUN_TAG.txt"
grep "gpu: world visible-refusal " "$REF_TRACE" > "$OUT/refusals.txt" || true
cat "$OUT/refusals.txt"

if [ "$MODE" != "--mutation" ]; then
  echo "== verdict =="
  if [ "$fail" = 0 ]; then
    echo "PASS: the material chain held from BSP name to screen pixel (fetch partition, tile table, face chain, albedo and textured pixels, repeat frame)."
    exit 0
  fi
  echo "FAIL: $fail assertion groups did not hold."
  exit 1
fi

# ---------------------------------------------------------------------------
# mutation mode: forward sections held; each mutation must go red
# ---------------------------------------------------------------------------
echo "== mutation mode =="
mfail=0
m1_red=0; m2_red=0; m3_red=0; m4_red=0; m5_red=0; m6_red=0   # one flag per mutant; a mutant may print two reds
SRC_REN="native/src/native_renderer_world.cpp"
SRC_MAIN="native/src/main.cpp"
SRC_VMT="native/src/vmt_material.cpp"
SAVE_VMT="$SCRATCH/vmt_material.cpp.save"
SAVE_REN="$SCRATCH/native_renderer.cpp.save"
SAVE_MAIN="$SCRATCH/main.cpp.save"
cp "$SRC_REN" "$SAVE_REN"
cp "$SRC_MAIN" "$SAVE_MAIN"
cp "$SRC_VMT" "$SAVE_VMT"
restore_sources() {
  cp "$SAVE_REN" "$SRC_REN"
  cp "$SAVE_MAIN" "$SRC_MAIN"
  cp "$SAVE_VMT" "$SRC_VMT"
  if cmp -s "$SAVE_REN" "$SRC_REN" && cmp -s "$SAVE_MAIN" "$SRC_MAIN" && cmp -s "$SAVE_VMT" "$SRC_VMT"; then
    echo "  RESTORE=OK (all three sources byte-identical)"
  else
    echo "  RESTORE=FAIL (a source did not come back byte-identical)"; fail=1
  fi
  bash build-target.sh tf2_demo_native > "$SCRATCH/restore.$RUN_TAG.build" 2>&1
}

# M1: the pre-T19 uv normaliser -- the tile size where the source size belongs.
echo "-- M1: the uv is normalised by the tile size, not the source size --"
"$PYTHON" - "$SRC_REN" <<'PYEOF_INNER'
import sys
path = sys.argv[1]
src = open(path, encoding='utf-8').read()
old = """      a.u = worldTileCoord(triangle.au, t.sourceWidth, t.x, t.width, batch.width);
      a.v = worldTileCoord(triangle.av, t.sourceHeight, t.y, t.height, batch.height);
      b.u = worldTileCoord(triangle.bu, t.sourceWidth, t.x, t.width, batch.width);
      b.v = worldTileCoord(triangle.bv, t.sourceHeight, t.y, t.height, batch.height);
      c.u = worldTileCoord(triangle.cu, t.sourceWidth, t.x, t.width, batch.width);
      c.v = worldTileCoord(triangle.cv, t.sourceHeight, t.y, t.height, batch.height);"""
new = """      a.u = worldTileCoord(triangle.au, t.width, t.x, t.width, batch.width);
      a.v = worldTileCoord(triangle.av, t.height, t.y, t.height, batch.height);
      b.u = worldTileCoord(triangle.bu, t.width, t.x, t.width, batch.width);
      b.v = worldTileCoord(triangle.bv, t.height, t.y, t.height, batch.height);
      c.u = worldTileCoord(triangle.cu, t.width, t.x, t.width, batch.width);
      c.v = worldTileCoord(triangle.cv, t.height, t.y, t.height, batch.height);"""
assert old in src, "M1 patch site not found"
open(path, 'w', encoding='utf-8', newline='').write(src.replace(old, new))
PYEOF_INNER
if [ $? != 0 ]; then echo "  FAIL M1 could not patch"; mfail=1; fi
bash build-target.sh tf2_demo_native > "$SCRATCH/m1.$RUN_TAG.build" 2>&1 || { echo "  FAIL M1 build"; mfail=1; }
M1_BMP="$SCRATCH/m1.$RUN_TAG.bmp"; M1_TRACE="$SCRATCH/m1.$RUN_TAG.trace"
if capture "$M1_BMP" "$M1_TRACE" --world-albedo; then
  "$PYTHON" face-trace-verify.py --trace "$M1_TRACE" --frame "$M1_BMP" --mode albedo \
    --min-points 12 > "$SCRATCH/m1-face.$RUN_TAG.txt" 2>&1
  M1_RC=$?
  M1_OVER=$(grep -c "status=MISMATCH.*src=" "$SCRATCH/m1-face.$RUN_TAG.txt")
  if [ "$M1_RC" = "0" ]; then
    echo "  NOT-RED M1: the reverted uv still matched the albedo frame"; mfail=1
  else
    m1_red=1; echo "  RED M1 as required: $M1_OVER points mismatched ($(grep 'FACE-CHECK' "$SCRATCH/m1-face.$RUN_TAG.txt" | tail -1))"
  fi
  if cmp -s "$M1_BMP" "$ALB_BMP"; then
    echo "  NOT-RED M1: the reverted uv produced a byte-identical frame"; mfail=1
  else
    m1_red=1; echo "  RED M1 as required: the frame moved too"
  fi
  # The other half of the discrimination. Under this mutation the OLD uv is in
  # the build, so the rival prediction is the one the frame must agree with.
  # Red here would mean the instrument is not measuring the normaliser at all,
  # only noticing that something moved.
  "$PYTHON" face-trace-verify.py --trace "$M1_TRACE" --frame "$M1_BMP" --mode albedo-old \
    --min-points 12 > "$SCRATCH/m1-face-rival.$RUN_TAG.txt" 2>&1
  M1_RIVAL_RC=$?
  if [ "$M1_RIVAL_RC" != "0" ]; then
    echo "  FAIL M1 control: the reverted uv does not match its own prediction"
    echo "       $(grep 'FACE-CHECK' "$SCRATCH/m1-face-rival.$RUN_TAG.txt" | tail -1)"
    mfail=1
  else
    echo "  OK M1 control: the reverted uv matches the rival prediction --"
    echo "     $(grep 'FACE-CHECK' "$SCRATCH/m1-face-rival.$RUN_TAG.txt" | tail -1)"
  fi
else
  echo "  FAIL M1 capture"; mfail=1
fi
restore_sources

# M2: the tile origin is dropped, so every material samples tile 0's block.
echo "-- M2: the atlas uv loses the tile origin --"
"$PYTHON" - "$SRC_REN" <<'PYEOF_INNER'
import sys
path = sys.argv[1]
src = open(path, encoding='utf-8').read()
old = """      a.u = worldTileCoord(triangle.au, t.sourceWidth, t.x, t.width, batch.width);
      a.v = worldTileCoord(triangle.av, t.sourceHeight, t.y, t.height, batch.height);
      b.u = worldTileCoord(triangle.bu, t.sourceWidth, t.x, t.width, batch.width);
      b.v = worldTileCoord(triangle.bv, t.sourceHeight, t.y, t.height, batch.height);
      c.u = worldTileCoord(triangle.cu, t.sourceWidth, t.x, t.width, batch.width);
      c.v = worldTileCoord(triangle.cv, t.sourceHeight, t.y, t.height, batch.height);"""
new = """      a.u = worldTileCoord(triangle.au, t.sourceWidth, 0u, t.width, batch.width);
      a.v = worldTileCoord(triangle.av, t.sourceHeight, 0u, t.height, batch.height);
      b.u = worldTileCoord(triangle.bu, t.sourceWidth, 0u, t.width, batch.width);
      b.v = worldTileCoord(triangle.bv, t.sourceHeight, 0u, t.height, batch.height);
      c.u = worldTileCoord(triangle.cu, t.sourceWidth, 0u, t.width, batch.width);
      c.v = worldTileCoord(triangle.cv, t.sourceHeight, 0u, t.height, batch.height);"""
assert old in src, "M2 patch site not found"
open(path, 'w', encoding='utf-8', newline='').write(src.replace(old, new))
PYEOF_INNER
if [ $? != 0 ]; then echo "  FAIL M2 could not patch"; mfail=1; fi
bash build-target.sh tf2_demo_native > "$SCRATCH/m2.$RUN_TAG.build" 2>&1 || { echo "  FAIL M2 build"; mfail=1; }
M2_BMP="$SCRATCH/m2.$RUN_TAG.bmp"; M2_TRACE="$SCRATCH/m2.$RUN_TAG.trace"
if capture "$M2_BMP" "$M2_TRACE" --world-albedo; then
  "$PYTHON" face-trace-verify.py --trace "$M2_TRACE" --frame "$M2_BMP" --mode albedo \
    --min-points 12 > "$SCRATCH/m2-face.$RUN_TAG.txt" 2>&1
  M2_RC=$?
  if [ "$M2_RC" = "0" ]; then
    echo "  NOT-RED M2: dropping the tile origin still matched"; mfail=1
  else
    m2_red=1; echo "  RED M2 as required: $(grep 'FACE-CHECK' "$SCRATCH/m2-face.$RUN_TAG.txt" | tail -1)"
  fi
else
  echo "  FAIL M2 capture"; mfail=1
fi
restore_sources

# M3: the prediction drops the lightmap term. Albedo must stay green -- that is
# what makes the textured pin a separate statement rather than the same one.
echo "-- M3: the predicted pixel drops the lightmap term --"
"$PYTHON" - "$SRC_MAIN" <<'PYEOF_INNER'
import sys
path = sys.argv[1]
src = open(path, encoding='utf-8').read()
old = """        value *= light[c] * lighting;"""
new = """        value *= lighting;"""
assert old in src, "M3 patch site not found"
open(path, 'w', encoding='utf-8', newline='').write(src.replace(old, new, 1))
PYEOF_INNER
if [ $? != 0 ]; then echo "  FAIL M3 could not patch"; mfail=1; fi
bash build-target.sh tf2_demo_native > "$SCRATCH/m3.$RUN_TAG.build" 2>&1 || { echo "  FAIL M3 build"; mfail=1; }
M3_BMP="$SCRATCH/m3.$RUN_TAG.bmp"; M3_TRACE="$SCRATCH/m3.$RUN_TAG.trace"
M3_ALB_BMP="$SCRATCH/m3-albedo.$RUN_TAG.bmp"; M3_ALB_TRACE="$SCRATCH/m3-albedo.$RUN_TAG.trace"
if capture "$M3_BMP" "$M3_TRACE" && capture "$M3_ALB_BMP" "$M3_ALB_TRACE" --world-albedo; then
  "$PYTHON" face-trace-verify.py --trace "$M3_TRACE" --frame "$M3_BMP" --mode textured \
    --min-points 12 > "$SCRATCH/m3-face.$RUN_TAG.txt" 2>&1
  M3_RC=$?
  "$PYTHON" face-trace-verify.py --trace "$M3_ALB_TRACE" --frame "$M3_ALB_BMP" --mode albedo \
    --min-points 12 > "$SCRATCH/m3-albedo.$RUN_TAG.txt" 2>&1
  M3_ALB_RC=$?
  if [ "$M3_RC" = "0" ]; then
    echo "  NOT-RED M3: the unlit prediction still matched the textured frame"; mfail=1
  else
    m3_red=1; echo "  RED M3 as required: $(grep 'FACE-CHECK' "$SCRATCH/m3-face.$RUN_TAG.txt" | tail -1)"
  fi
  if [ "$M3_ALB_RC" != "0" ]; then
    echo "  FAIL M3 control: the albedo comparison also went red, so the two pins are not independent"; mfail=1
  else
    echo "  OK M3 control: albedo stayed green"
  fi
else
  echo "  FAIL M3 capture"; mfail=1
fi
restore_sources

# M4: the table calls a refused material tiled. There is no refused material on
# this map (the plan refuses none), so the lie is injected on the row the plan
# marked `tiled`: the row's batch is set to -1 while its reason stays `tiled`.
# The gate's row assertion is "every tiled row has a rectangle", which is the
# same statement read the other way.
echo "-- M4: a tiled row loses its rectangle but keeps its name --"
"$PYTHON" - "$SRC_REN" <<'PYEOF_INNER'
import sys
path = sys.argv[1]
src = open(path, encoding='utf-8').read()
old = """    reading.reason = plan.slots[index].batch >= 0
      ? "tiled" : worldTileRefusalName(plan.refusals[index]);"""
new = """    reading.reason = "tiled";
    if (plan.slots[index].batch >= 0 && plan.slots[index].tile == 0) reading.batch = -1;"""
assert old in src, "M4 patch site not found"
open(path, 'w', encoding='utf-8', newline='').write(src.replace(old, new))
PYEOF_INNER
if [ $? != 0 ]; then echo "  FAIL M4 could not patch"; mfail=1; fi
bash build-target.sh tf2_demo_native > "$SCRATCH/m4.$RUN_TAG.build" 2>&1 || { echo "  FAIL M4 build"; mfail=1; }
M4_BMP="$SCRATCH/m4.$RUN_TAG.bmp"; M4_TRACE="$SCRATCH/m4.$RUN_TAG.trace"
if capture "$M4_BMP" "$M4_TRACE"; then
  M4_BAD=$(grep "gpu: world tile material=" "$M4_TRACE" | grep "reason=tiled" | grep -cE "batch=-1|tile=-1|size=0x")
  M4_ROWS=$(grep -c "gpu: world tile material=" "$M4_TRACE")
  M4_TILED=$(grep "gpu: world tile material=" "$M4_TRACE" | grep -c "reason=tiled")
  if [ "$M4_BAD" = "0" ] && [ "$M4_ROWS" = "$M4_TILED" ]; then
    echo "  NOT-RED M4: the table still reads $M4_TILED tiled rows, none malformed"; mfail=1
  else
    m4_red=1; echo "  RED M4 as required: $M4_BAD rows claim a tile without a batch ($M4_TILED of $M4_ROWS say tiled)"
  fi
else
  echo "  FAIL M4 capture"; mfail=1
fi
restore_sources

echo "-- M5: the include is no longer followed --"
"$PYTHON" - "$SRC_VMT" <<'PYEOF_INNER'
import sys
path = sys.argv[1]
src = open(path, encoding='utf-8', newline='').read()
old = 'if (lower(node.shader) != "patch") return true;'
new = 'if (true) return true;  // M5: a patch keeps its own root, the defect this task removes'
assert src.count(old) == 1, 'M5 patch site not found'
open(path, 'w', encoding='utf-8', newline='').write(src.replace(old, new))
PYEOF_INNER
if [ $? != 0 ]; then echo "  FAIL M5 could not patch"; mfail=1; fi
bash build-target.sh tf2_demo_native > "$SCRATCH/m5.$RUN_TAG.build" 2>&1 || { echo "  FAIL M5 build"; mfail=1; }
M5_BMP="$SCRATCH/m5.$RUN_TAG.bmp"; M5_TRACE="$SCRATCH/m5.$RUN_TAG.trace"
if capture "$M5_BMP" "$M5_TRACE"; then
  M5_LINE=$(grep -o "gpu: world material-fetch .*" "$M5_TRACE" | head -1)
  M5_DECL=$(printf '%s' "$M5_LINE" | grep -oE "patchDeclared=[0-9]+" | cut -d= -f2)
  M5_TILED=$(printf '%s' "$M5_LINE" | grep -oE "tiled=[0-9]+" | cut -d= -f2)
  M5_NOBASE=$(printf '%s' "$M5_LINE" | grep -oE "vmtNoBase=[0-9]+" | cut -d= -f2)
  M5_PATCHED=$(grep "gpu: world tile material=" "$M5_TRACE" | grep -cE "metalchrome002|floor_tile_004")
  if [ "$M5_DECL" = "0" ] || [ "$M5_TILED" = "$TILED" ] || [ "$M5_PATCHED" != "0" ]; then
    m5_red=1; echo "  RED M5 as required: patchDeclared=$M5_DECL (want > 0), tiled=$M5_TILED (want < $TILED), include-only rows=$M5_PATCHED (want 0), vmtNoBase=$M5_NOBASE"
  else
    echo "  NOT-RED M5: the fetch still reads patchDeclared=$M5_DECL tiled=$M5_TILED -- the patch section proves nothing"; mfail=1
  fi
else
  echo "  FAIL M5 capture"; mfail=1
fi
restore_sources

echo "-- M6: the map material stops being marked valid --"
# The single-material upload path (main.cpp, the worldspawn material) only resolves
# its base texture when VmtMaterial::valid is true; the atlas path never reads it,
# so no counter in the atlas path can see this field go missing. That is exactly how
# 788f95c dropped "material.valid = true" without one gate noticing. This mutation
# puts the field back to its default and requires the title's maptex reading to say
# "fallback", i.e. the forward assertion above is load-bearing.
"$PYTHON" - "$SRC_VMT" <<'PYEOF_INNER'
import sys
path = sys.argv[1]
src = open(path, encoding='utf-8', newline='').read()
old = '  material.valid = true;'
new = '  material.valid = false;  // M6: the field the atlas path never reads'
assert src.count(old) == 1, 'M6 patch site not found'
open(path, 'w', encoding='utf-8', newline='').write(src.replace(old, new))
PYEOF_INNER
if [ $? != 0 ]; then echo "  FAIL M6 could not patch"; mfail=1; fi
bash build-target.sh tf2_demo_native > "$SCRATCH/m6.$RUN_TAG.build" 2>&1 || { echo "  FAIL M6 build"; mfail=1; }
M6_BMP="$SCRATCH/m6.$RUN_TAG.bmp"; M6_TRACE="$SCRATCH/m6.$RUN_TAG.trace"
if capture "$M6_BMP" "$M6_TRACE"; then
  M6_MAPMATEX=$(grep -a -m1 "maptex: " "$M6_TRACE" | sed -E 's/.*(mapmat: [^,]+).*(maptex: [^]]+)\].*/\1 \2/')
  M6_MAPTEX=$(printf '%s' "$M6_MAPMATEX" | sed -n -E 's/^.* maptex: (.*)$/\1/p')
  printf '%s\n' "$M6_MAPMATEX" > "$OUT/mutated-m6-map-material.txt"
  if [ "$M6_MAPTEX" = "$MAPTEX" ]; then
    echo "  NOT-RED M6: the map material still resolves ($M6_MAPTEX, same as the forward run) -- the maptex assertion proves nothing"; mfail=1
  else
    m6_red=1; echo "  RED M6 as required: the map material no longer resolves (maptex: $M6_MAPTEX, forward read $MAPTEX)"
  fi
else
  echo "  FAIL M6 capture"; mfail=1
fi
restore_sources
echo "== verdict =="
mred=$((m1_red + m2_red + m3_red + m4_red + m5_red + m6_red))
if [ "$mred" != "6" ]; then
  echo "  FAIL only $mred of the 6 mutations went red -- the suite does not count what it claims"
  mfail=1
fi
if [ "$fail" = 0 ] && [ "$mfail" = 0 ]; then
  echo "PASS: the forward material chain held, all $mred of 6 mutations went red, every source came back byte-identical."
  exit 0
fi
echo "FAIL: forward-fail=$fail mutation-not-red=$mfail"
exit 1
