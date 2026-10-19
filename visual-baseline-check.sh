#!/usr/bin/env bash
# visual-baseline-check.sh -- T10/T11: the visual correctness baseline gate.
#
# WHY THIS FILE EXISTS
# --------------------
# The release blocker was visual: the reference POV frame showed broken
# material blocks, a partial ViewModel and a world that never became a place.
# T10 fixed two root causes in the world region (the pvs clip clustering the
# observer's NORMALIZED point as map coordinates, and the --debug-camera
# diagnostic being cleared by the first frame's camera-mode transition).
# T11's flat-colour and depth-only captures then separated triangulation,
# displacement, camera and depth ordering, and fixed two more:
#
#   A. the pvs row table was read with one stride-8 offset table -- a
#      different cluster's row -- and decoded with a run-length walk over rows
#      this map stores uncompressed. Cluster 1032's pvs answered 1021 visible
#      clusters (garbage); the corrected reader answers 1692, which an
#      independent python decode of the vis lump reproduces. The clip was
#      hiding faces that were plainly in front of the camera.
#   B. the shipped winding is mixed and the two emission paths disagree:
#      displacement grids and brush fans store opposite conventions, so every
#      single global flip left half the map culled as back faces. Every
#      emitted triangle is now oriented against its face's own plane, with the
#      class side measured (47252 of 99312 triangles reversed); the flat frame
#      went from ~35% coverage to ~95%, and the residual clear-colour gap
#      matches the one hole the no-cull frame also has.
#
# What this gate asserts, in order:
#   1. the geometry probe's census of the reference map, pinned: triangle
#      total, displacement split, degenerate count, the material atlas
#      coverage, and the winding-agreement census,
#   2. the product's own reference capture (first-person, tick 2000, the
#      product's default vis+cull path), with the trace pinned: the
#      material-fetch counters, the vis line (cluster, pvs popcount, hidden
#      and drawn triangles, and the MAP-space eye the cluster was built from),
#      and the cull line,
#   3. measurable object presence in four world regions of the captured
#      frame -- mean luminance, luminance spread, background fraction and
#      gradient, each pinned with a margin. This is the object-visibility
#      acceptance: a region that empties to the clear colour, goes flat, or
#      loses its edges fails here, and "pixels changed" alone never passes.
#   4. (T17) the three populations of the world pass: the terrain's two blend
#      layers resolve and are weighted, the sky pass draws its six faces, the
#      lightmap term is on and addresses its own texels, and the two lightmap
#      pages agree about how dark the map's data is. These are the readings the
#      world visual closure was measured with, so a regression in any of them
#      is visible here instead of in a screenshot someone has to squint at.
#
# mutation: five source mutations, each of which must turn the forward pins
#   red, and each source must come back byte-identical:
#   M1 reverts the pvs row-table fix (the stride-8 single-table read) -- the
#      probe's visClusters pin must fall back off 1692,
#   M2 orients brush to the outward direction as well (mask 05) -- the flip
#      census and the eye first-hit oracle must both move,
#   M3 inverts the split so only brush is oriented (mask 02) -- the top-down
#      first-hit oracle must collapse,
#   M4 (T17) compares pakfile entry names byte for byte again -- the product's
#      material-fetch vmtMissing must climb back off 3,
#   M5 (T17) drops the 1/2.2 encode on the lightmap sample -- W1's mean
#      luminance must fall out of its pin, because that pin was measured with it.
#
# The reference capture deliberately does NOT pass --hidden-window: the pinned
# region numbers below were measured on a visible-window capture at
# 1264x681, and a hidden-window swapchain cannot be assumed to match. Every
# other gate convention (RUN_TAG'd scratch files, timeouts, byte-identical
# restores) is kept.
#
# Usage: bash visual-baseline-check.sh [--mutation]
# Exit:  0 = every assertion held (or, with --mutation, went red).
set -uo pipefail
cd "$(dirname "$0")"

PRODUCT=native/build-nmake/tf2_demo_native.exe
PROBE=native/build-nmake/world_geometry_probe.exe
TF="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
MAP=maps/pl_odyssey.bsp
DEMO="$TF/demos/autorecord_2026-07-02_13-26-46.dem"
PYTHON="${PYTHON:-python}"

RUN_TAG="$(date +%Y%m%d%H%M%S)-$$"
MODE="${1:-}"
SCRATCH=".scratch/visual-baseline.$RUN_TAG"
OUT="evidence/visual-baseline"
mkdir -p "$SCRATCH" "$OUT"

fail=0
assert_eq() { # assert_eq <name> <got> <want>
  if [ "$2" = "$3" ]; then echo "  OK $1 = $2"; else echo "  FAIL $1: got '$2' want '$3'"; fail=1; fi
}
assert_range() { # assert_range <name> <got> <low> <high>
  if awk -v v="$2" -v lo="$3" -v hi="$4" 'BEGIN{exit !(v>=lo && v<=hi)}'; then
    echo "  OK $1 = $2 in [$3,$4]"
  else
    echo "  FAIL $1: got '$2' outside [$3,$4]"; fail=1
  fi
}
assert_max() { # assert_max <name> <got> <ceiling>
  if awk -v v="$2" -v hi="$3" 'BEGIN{exit !(v<=hi)}'; then
    echo "  OK $1 = $2 <= $3"
  else
    echo "  FAIL $1: got '$2' above $3"; fail=1
  fi
}

capture() { # capture <bmp-path> <trace-path> [extra flags...]
  local bmp="$1" trace="$2"; shift 2
  local traceWin bmpWin attempt rc tick
  traceWin="$(cygpath -m "$PWD/$trace")"
  bmpWin="$(cygpath -m "$PWD/$bmp")"
  # T17: `--capture-tick N` means the first draw with `tick >= N`, and on this
  # recording that lands on 2001 or 2003 about half the time (measured 2026-10-19:
  # 8 of 13 attempts). A pin measured at tick 2000 cannot be compared with a
  # frame drawn at 2001 -- the camera moved -- so the capture retries until the
  # draw reports exactly 2000. Before this the gate passed or failed on which
  # side of the tick boundary the run happened to land.
  for attempt in 1 2 3 4 5 6; do
    rm -f "$bmp"
    TF2_NATIVE_TRACE="$traceWin" timeout 300 "$PRODUCT" --tf-root "$TF" --demo "$DEMO" \
      --capture-tick 2000 --capture-frame "$bmpWin" --audio-disabled \
      --viewmodel models/weapons/v_models/v_scattergun_scout.mdl --viewmodel-render companion \
      "$@" > "$SCRATCH/capture.$RUN_TAG.log" 2>&1
    rc=$?
    tick=$(grep -a "draw: begin tick=" "$trace" 2>/dev/null | tr -d '\r' | tail -1 \
      | sed -n 's/.*tick=\([-0-9]*\).*/\1/p')
    if [ "$rc" = 0 ] && [ "$tick" = "2000" ] && [ -s "$bmp" ]; then return 0; fi
    echo "  (capture attempt $attempt: rc=$rc tick=${tick:-none}, retrying for tick 2000)"
  done
  return 1
}

trace_field() { # trace_field <trace> <line-prefix> <key>
  grep "$2" "$1" 2>/dev/null | head -1 | grep -oE "$3=[-0-9.]+" | head -1 | cut -d= -f2
}

# ---------------------------------------------------------------------------
# 1. the geometry probe's census of the reference map
# ---------------------------------------------------------------------------
echo "== 1. geometry census (probe) =="
PROBE_TXT="$SCRATCH/probe.$RUN_TAG.txt"
"$PROBE" --tf-root "$TF" --map "$MAP" \
  --eye "-2423.6,5566.0,-1280.0" \
  --ray-down 96 --ray-eye "-2423.6,5566.0,-1280.0" 24 > "$PROBE_TXT" 2>&1
echo "$PROBE_TXT" > "$OUT/probe-path.txt"
cp "$PROBE_TXT" "$OUT/probe.txt"
assert_eq "probe status" "$(grep -oE 'status=[a-z]+' "$PROBE_TXT" | head -1 | cut -d= -f2)" "ok"
assert_eq "probe triangles" "$(grep -oE 'triangles=[0-9]+' "$PROBE_TXT" | head -1 | cut -d= -f2)" "99312"
assert_eq "probe displaced" "$(grep -oE ' displaced=[0-9]+' "$PROBE_TXT" | head -1 | tr -d ' ' | cut -d= -f2)" "46912"
assert_eq "probe degenerate" "$(grep -oE 'degenerate=[0-9]+' "$PROBE_TXT" | head -1 | cut -d= -f2)" "4790"
assert_eq "probe materials covered by atlas cap" "$(grep -oE 'trianglesCoveredByCap=[0-9]+' "$PROBE_TXT" | head -1 | cut -d= -f2)" "98011"
# The eye the product's vis line reports must be the cluster the probe
# computes from the same map coordinates: one number, two instruments.
assert_eq "probe eye cluster" "$(trace_field "$PROBE_TXT" 'geometry eye' 'cluster')" "1032"
assert_eq "probe eye pvs popcount" "$(trace_field "$PROBE_TXT" 'geometry eye' visClusters)" "1692"
# T17: the lightmap page, measured two ways so the two answers can be compared.
# `lightmap-uv` asks whether the uvs land on texels the packer wrote -- 0
# unwritten of 296490 corners means they do. `lightmap-tone` is what those
# texels contain read THROUGH the uvs; `lightmap-face-tone` is what the same
# rectangles contain read from the packer. They agree (31.5% vs 34.4% in the
# darkest bucket), so the 27% of triangles that draw dark are dark data in the
# shipped map. `lightmap-hdr-tone` asks the other lighting lump -- lump 53 says
# the same thing (5303 dark faces vs 5018), so the tonemapped page is not the
# cause either. A uv or page regression moves one of these six numbers.
assert_eq "probe lightmap uv atlas" \
  "$(grep -oE 'lightmap-uv atlas=[0-9]+x[0-9]+' "$PROBE_TXT" | head -1 | cut -d= -f2)" "4096x2251"
assert_eq "probe lightmap unwritten texels" \
  "$(grep -oE 'unwrittenTexels=[0-9]+' "$PROBE_TXT" | head -1 | cut -d= -f2)" "0"
assert_eq "probe lightmap dark triangles" \
  "$(grep -oE 'darkTriangles=[0-9]+' "$PROBE_TXT" | head -1 | cut -d= -f2)" "26844"
assert_eq "probe lightmap face tone" \
  "$(grep 'lightmap-face-tone' "$PROBE_TXT" | grep -oE 'darkFaces=[0-9]+ zeroFaces=[0-9]+' | head -1)" \
  "darkFaces=5018 zeroFaces=898"
assert_eq "probe lightmap hdr tone" \
  "$(grep 'lightmap-hdr-tone' "$PROBE_TXT" | grep -oE 'darkFaces=[0-9]+ zeroFaces=[0-9]+' | head -1)" \
  "darkFaces=5303 zeroFaces=940"
# T11 third round: the buffer ships ONE winding convention, not two, and the
# rule is a keep/flip choice per (class, side) cell. The shipped rule is mask 07:
# brush keeps the order the map stores, displacement is oriented to its face's
# side-aware outward direction. The readings below are that decision, and each
# one of them moves if the decision is undone -- see the mutations.
assert_eq "probe winding flip census" "$(grep -oE 'flippedTriangles=[0-9]+' "$PROBE_TXT" | head -1 | cut -d= -f2)" "8514"
# `misfaced` counts emitted triangles whose winding points away from the
# direction the face's OWN plane side bit calls its front. It reads 15126 now
# instead of 53724 because the displacement class is oriented to that side
# (`displacedMisfaced` 38614 -> 0); the remainder is brush/side-1, one of the
# two cells the two first-hit oracles disagree about (analysis/rule-decision.txt).
assert_eq "probe side-aware misfaced" "$(grep -oE 'misfaced=[0-9]+' "$PROBE_TXT" | head -1 | cut -d= -f2)" "15126"
assert_eq "probe side-aware misfaced displaced" "$(grep -oE 'displacedMisfaced=[0-9]+' "$PROBE_TXT" | head -1 | cut -d= -f2)" "0"
assert_eq "probe faces on the back of their plane" "$(grep -oE 'sideOneFaces=[0-9]+' "$PROBE_TXT" | head -1 | cut -d= -f2)" "5388"
# T11 third round: the two first-hit oracles. These are the numbers the rule was
# chosen by, and they are the numbers a regression has to move: the fraction of
# first-hit surfaces the shipped rule actually draws, from above (the top-down
# diagnostic camera) and from the eye (the acceptance camera). Before this round
# they read 0.311 and 0.820.
assert_eq "oracle top-down first hits drawn" \
  "$(grep 'geometry ray-down cells' "$PROBE_TXT" | grep -oE 'drawnByShippedRule=[0-9]+ of [0-9]+' | head -1)" \
  "drawnByShippedRule=3809 of 5381"
assert_eq "oracle eye first hits drawn" \
  "$(grep 'geometry ray-eye dirs' "$PROBE_TXT" | grep -oE 'shippedRuleKeeps=[0-9]+ of [0-9]+' | head -1)" \
  "shippedRuleKeeps=458 of 594"
assert_eq "rule table mask 07" \
  "$(grep 'geometry rule-table mask=07' "$PROBE_TXT" | grep -oE 'top=[0-9.]+ eye=[0-9.]+' | head -1)" \
  "top=0.708 eye=0.771"
# The per-class split of that disagreement: how many PVS-kept triangles have the
# eye on their outward side vs the other side. A class whose stored data agreed
# with its own plane would read outward for almost all of them.
assert_eq "census brush side0" "$(grep 'class=brush planeSide=0' "$PROBE_TXT" | grep -oE 'outward=[0-9]+ inward=[0-9]+')" "outward=10931 inward=5361"
assert_eq "census brush side1" "$(grep 'class=brush planeSide=1' "$PROBE_TXT" | grep -oE 'outward=[0-9]+ inward=[0-9]+')" "outward=4670 inward=3851"
assert_eq "census displacement side0" "$(grep 'class=displacement planeSide=0' "$PROBE_TXT" | grep -oE 'outward=[0-9]+ inward=[0-9]+')" "outward=23364 inward=15164"
assert_eq "census displacement side1" "$(grep 'class=displacement planeSide=1' "$PROBE_TXT" | grep -oE 'outward=[0-9]+ inward=[0-9]+')" "outward=4256 inward=4128"

# ---------------------------------------------------------------------------
# 2. the product's reference capture and its trace
# ---------------------------------------------------------------------------
echo "== 2. product reference capture (first-person, tick 2000, vis+cull) =="
REF_BMP="$SCRATCH/ref.$RUN_TAG.bmp"
REF_TRACE="$SCRATCH/ref.$RUN_TAG.trace"
capture "$REF_BMP" "$REF_TRACE"
assert_eq "product run rc" "$?" "0"
[ -s "$REF_TRACE" ] || { echo "  FAIL the product produced no trace"; fail=1; }
cp "$REF_TRACE" "$OUT/trace-ref.txt"

MATFETCH=$(grep -o "gpu: world material-fetch .*" "$REF_TRACE" | head -1)
[ -n "$MATFETCH" ] || { echo "  FAIL no material-fetch line"; fail=1; }
assert_eq "material fetch distinct" "$(printf '%s' "$MATFETCH" | grep -oE 'distinct=[0-9]+' | cut -d= -f2)" "108"
# T17: these four moved because the bsp pakfile lookup became case-insensitive
# (pl_odyssey stores lowercase entries, textdata names them uppercase). tiled
# 57->64 and fromPakfile 23->59 are 41 more materials that now find their vmt
# and their vtf; vtfTooLarge 43->54 is those materials reaching vtfs that are
# correctly refused for size; vmtMissing 29->3 and vtfMissing 2->0 are the ones
# that stopped being missing. `distinct` and `capped` must NOT move -- the set
# of materials did not change, only whether each one resolved.
assert_eq "material fetch tiled" "$(printf '%s' "$MATFETCH" | grep -oE 'tiled=[0-9]+' | cut -d= -f2)" "64"
assert_eq "material fetch fromPakfile" "$(printf '%s' "$MATFETCH" | grep -oE 'fromPakfile=[0-9]+' | cut -d= -f2)" "59"
assert_eq "material fetch vtfTooLarge" "$(printf '%s' "$MATFETCH" | grep -oE 'vtfTooLarge=[0-9]+' | cut -d= -f2)" "54"
assert_eq "material fetch vmtMissing" "$(printf '%s' "$MATFETCH" | grep -oE 'vmtMissing=[0-9]+' | cut -d= -f2)" "3"
assert_eq "material fetch vtfMissing" "$(printf '%s' "$MATFETCH" | grep -oE 'vtfMissing=[0-9]+' | cut -d= -f2)" "0"

VISLINE=$(grep -o "gpu: world vis .*" "$REF_TRACE" | head -1)
[ -n "$VISLINE" ] || { echo "  FAIL no world vis line"; fail=1; }
echo "  vis: $VISLINE"
assert_eq "vis clip applied" "$(printf '%s' "$VISLINE" | grep -oE 'clipApplied=[0-9]+' | cut -d= -f2)" "1"
assert_eq "vis camera cluster" "$(printf '%s' "$VISLINE" | grep -oE 'cameraCluster=[0-9-]+' | cut -d= -f2)" "1032"
# T11: 1692 is the corrected PVS row for cluster 1032 -- the same number the
# probe computes from the same eye, and the same number an independent python
# decode of the vis lump produces. The pre-fix reader answered 1021 because it
# stepped the compressed offset table by 8 bytes (bitofs[0][2c]) and ran a
# run-length walk over rows this map stores uncompressed.
assert_eq "vis pvs popcount" "$(printf '%s' "$VISLINE" | grep -oE 'visibleFromCamera=[0-9]+' | cut -d= -f2)" "1692"
assert_eq "vis hidden triangles" "$(printf '%s' "$VISLINE" | grep -oE 'hiddenTriangles=[0-9]+' | cut -d= -f2)" "22968"
assert_eq "vis drawn triangles" "$(printf '%s' "$VISLINE" | grep -oE 'drawnTriangles=[0-9]+' | cut -d= -f2)" "76344"
# The eye pin is THE fix's fingerprint: a map-scale position, three instruments
# agreeing (renderer inverse, clusterAt, probe). The mutation reverts it to a
# normalized point near the origin, which cannot match this line.
assert_eq "vis eye (map space)" \
  "$(printf '%s' "$VISLINE" | grep -oE 'eyeMap=\(-?[0-9.]+,-?[0-9.]+,-?[0-9.]+\)' | head -1)" \
  "eyeMap=(-2423.6,5566.0,-1280.0)"

CULLLINE=$(grep -o "gpu: world cull .*" "$REF_TRACE" | head -1)
assert_eq "world cull line" "$CULLLINE" "gpu: world cull backfaces=1 mode=cull-back"

GEOM=$(grep -o "gpu: world geometry .*" "$REF_TRACE" | head -1)
assert_eq "gpu displaced triangles" "$(printf '%s' "$GEOM" | grep -oE 'displacedTriangles=[0-9]+' | cut -d= -f2)" "46912"
assert_eq "gpu water triangles" "$(printf '%s' "$GEOM" | grep -oE 'waterTriangles=[0-9]+' | cut -d= -f2)" "300"

# ---------------------------------------------------------------------------
# 2b. T17: the world pass draws a place, not a set of holes
# ---------------------------------------------------------------------------
# Three populations were measured on this frame (evidence/t17-world-visual):
# pixels identical to the clear colour (nothing drawn), drawn pixels whose every
# channel is <32 (the light took them to zero), and the rest. The sky pass is
# the difference between the first population being 20.2% and 0.04%, and the
# blend fetch is the difference between the terrain drawing one layer and two.
echo "== 2b. T17 world visual closure =="
BLENDFETCH=$(grep -o "gpu: world blend-fetch .*" "$REF_TRACE" | head -1)
[ -n "$BLENDFETCH" ] || { echo "  FAIL no blend-fetch line"; fail=1; }
assert_eq "blend fetch declared" \
  "$(printf '%s' "$BLENDFETCH" | grep -oE 'declared=[0-9]+' | cut -d= -f2)" "9"
assert_eq "blend fetch resolved" \
  "$(printf '%s' "$BLENDFETCH" | grep -oE 'resolved=[0-9]+' | cut -d= -f2)" "9"
assert_eq "blend fetch refused" \
  "$(printf '%s' "$BLENDFETCH" | grep -oE 'refused=[0-9]+' | cut -d= -f2)" "0"
BLENDLINE=$(grep -o "gpu: world blend enabled=.*" "$REF_TRACE" | head -1)
assert_eq "blend tiles" \
  "$(printf '%s' "$BLENDLINE" | grep -oE 'tiles=[0-9]+' | cut -d= -f2)" "9"
assert_eq "blend weighted vertices" \
  "$(printf '%s' "$BLENDLINE" | grep -oE 'weightedVertices=[0-9]+' | cut -d= -f2)" "85536"
assert_eq "blend distinct weights" \
  "$(printf '%s' "$BLENDLINE" | grep -oE 'distinctWeights=[0-9]+' | cut -d= -f2)" "34"
# The sky pass carries no geometry of its own into the world vertex buffer, so
# "is the sky drawn" is exactly this line: 6 faces, 12 triangles, 36 vertices.
# A settings file that says `preset=0` turns it off and this goes red, which is
# the point -- the reference frame is not reproducible without the sky.
SKYLINE=$(grep -o "gpu: world sky-upload .*" "$REF_TRACE" | head -1)
[ -n "$SKYLINE" ] || { echo "  FAIL no sky-upload line"; fail=1; }
assert_eq "sky pass state" \
  "$(printf '%s' "$SKYLINE" | grep -oE 'state=[a-z-]+' | cut -d= -f2)" "active"
assert_eq "sky pass faces" \
  "$(printf '%s' "$SKYLINE" | grep -oE 'faces=[0-9]+' | cut -d= -f2)" "6"
assert_eq "sky pass vertices" \
  "$(printf '%s' "$SKYLINE" | grep -oE 'vertices=[0-9]+' | cut -d= -f2)" "36"
LIGHTLINE=$(grep -o "gpu: world lightmap status=.*" "$REF_TRACE" | head -1)
assert_eq "lightmap status" \
  "$(printf '%s' "$LIGHTLINE" | grep -oE 'status=[a-z-]+' | cut -d= -f2)" "active"
assert_eq "lightmap vertices" \
  "$(printf '%s' "$LIGHTLINE" | grep -oE 'vertices=[0-9]+' | cut -d= -f2)" "297936"
assert_eq "lightmap lightmapped vertices" \
  "$(printf '%s' "$LIGHTLINE" | grep -oE 'lightmapped=[0-9]+' | cut -d= -f2)" "296490"
assert_eq "lightmap term on" \
  "$(printf '%s' "$LIGHTLINE" | grep -oE 'lightmapEnabled=[0-9]+' | cut -d= -f2)" "1"

# ---------------------------------------------------------------------------
# 3. measurable object presence in four world regions
# ---------------------------------------------------------------------------
echo "== 3. region presence (the object-visibility acceptance) =="
REGIONS_TXT="$SCRATCH/regions.$RUN_TAG.txt"
"$PYTHON" frame-region-stats.py "$REF_BMP" \
  0,0,420,200 0,560,420,121 1100,100,164,200 600,0,64,60 > "$REGIONS_TXT" 2>&1
cp "$REGIONS_TXT" "$OUT/regions-ref.txt"
cat "$REGIONS_TXT"
regfield() { # regfield <region-line-key> <field>
  grep "$1" "$REGIONS_TXT" | head -1 | grep -oE "$2=[-0-9.]+" | head -1 | cut -d= -f2
}
# W1: the beige wall, top-left. W4: the ceiling slabs top-centre. These two
# carry measurable world presence and assert it (the object-visibility
# acceptance for what this task fixed).
# W2 (lit interior, bottom-left) and W3 (far wall, top-right) are OPEN-DEFECT
# guards, not presence assertions: the world-completeness defect (the
# displacement terrain still renders broken; T10's follow-up task) leaves both
# regions mostly background in the shipped state. They are pinned to today's
# readings so any reshuffle of what the world pass draws -- a vis, cull or
# winding regression -- moves them and fails here.
# T11 moved every one of these: with the pvs row table and the per-class
# winding fixed, the clear-colour fraction collapses to ~0 in all four
# regions (nearBackground is the geometry-presence number now), and W2/W3/W4
# are dark because their materials draw black -- the terrain/blend-material
# defect that belongs to its own task. The pins therefore guard coverage and
# texture spread, not brightness.
#
# T11 third round moved W3 and W4 again, and the direction is worth stating
# because it looks like a regression and is not one: the winding decision
# (DIAGNOSIS.md 5c) draws the surface that is actually nearest along the view
# ray, so the bright panel that used to fill W3 is gone -- it was a displacement
# surface drawn from behind. What is left there is the correct surface, and it is
# dark and flat because the material/lightmap path draws terrain dark (F2/T12).
# W4 gained contrast for the same reason. Both are re-pinned to today's readings;
# when T12 lands, these two pins are expected to move again and upward.
#
# T17 (2026-10-19) moved all four, and all four moved UP for two measured
# reasons, neither of which is a threshold being widened to fit:
#   * the lightmap is now composed in display space (pow 1/2.2 on the sample),
#     so every lit pixel is brighter -- W1 46..77 -> 86.3, W2 16..29 -> 72.8;
#   * the sky pass runs in the default path, so the regions that used to be
#     clear colour are now sky -- W3 8..14 -> 92.6 with std 70.3 (it is no longer
#     a flat hole; it is blue sky behind dark geometry), W4's gradient falls
#     (6.2..10.0 -> 4.37) because flat sky now sits between the ceiling slabs.
# `nearBackground` is no longer a hole measurement: the tool reads the frame's
# own top-left corner, which is now a wall, not clear colour. It is kept as an
# upper bound so a region that collapses to one colour still fails.
for spec in "x=0,y=0 73 99 53 80 0.05 7.7 12.8" \
            "x=0,y=560 62 84 19 28 0.03 1.2 2.0" \
            "x=1100,y=100 79 107 56 84 0.02 0.8 1.3" \
            "x=600,y=0 36 49 44 66 0.02 3.3 5.5"; do
  set -- $spec; KEY="$1"; shift
  mean=$(regfield "$KEY" meanLum); std=$(regfield "$KEY" stdLum)
  nearbg=$(regfield "$KEY" nearBackground); grad=$(regfield "$KEY" meanGradient)
  if [ -z "$mean" ]; then echo "  FAIL region $KEY produced no reading"; fail=1; continue; fi
  assert_range "region $KEY meanLum" "$mean" "$1" "$2"
  assert_range "region $KEY stdLum" "$std" "$3" "$4"
  assert_max  "region $KEY nearBackground" "$nearbg" "$5"
  assert_range "region $KEY meanGradient" "$grad" "$6" "$7"
done

if [ "$MODE" != "--mutation" ]; then
  echo "== verdict =="
  if [ "$fail" = 0 ]; then echo "PASS: the visual baseline held (probe + trace + four world regions)."
  else echo "FAIL: $fail assertion groups did not hold."; fi
  exit "$fail"
fi

# ---------------------------------------------------------------------------
# mutation mode: the forward sections above held; both mutations must go red
# ---------------------------------------------------------------------------
echo "== mutation mode =="
mfail=0
SRC_MAP="native/src/bsp_map.cpp"
SAVE_MAP="$SCRATCH/bsp_map.cpp.save"
cp "$SRC_MAP" "$SAVE_MAP"

# M1: revert the PVS row-table fix -- read one offset table with the old
# stride of 8 bytes (bitofs[0][2c]) and let the uncompressed rows go unused.
echo "-- M1: the vis offset table goes back to the stride-8 single-table read --"
"$PYTHON" - "$SRC_MAP" <<'PYEOF_INNER'
import sys
path = sys.argv[1]
src = open(path, encoding='utf-8').read()
old = """        const auto compressedOffset = read<std::int32_t>(vis, 4 + static_cast<std::size_t>(cluster) * 4u);
        const auto rawOffset = read<std::int32_t>(vis,
          4 + static_cast<std::size_t>(clusters) * 4u + static_cast<std::size_t>(cluster) * 4u);
        if (rawOffset < 0 && (compressedOffset < 0 || static_cast<std::size_t>(compressedOffset) >= vis.size())) offsetsOk = false;
        if (rawOffset >= 0 && static_cast<std::size_t>(rawOffset) >= vis.size()) offsetsOk = false;
        map.visOffsets[static_cast<std::size_t>(cluster)] = compressedOffset;
        map.visRawOffsets[static_cast<std::size_t>(cluster)] = rawOffset;"""
new = """        const auto compressedOffset = read<std::int32_t>(vis, 4 + static_cast<std::size_t>(cluster) * 8u);
        if (compressedOffset < 0 || static_cast<std::size_t>(compressedOffset) >= vis.size()) offsetsOk = false;
        map.visOffsets[static_cast<std::size_t>(cluster)] = compressedOffset;
        map.visRawOffsets[static_cast<std::size_t>(cluster)] = -1;"""
assert old in src, "M1 patch site not found"
open(path, 'w', encoding='utf-8', newline='').write(src.replace(old, new))
PYEOF_INNER
if [ $? != 0 ]; then echo "  FAIL M1 could not patch the source"; fail=1; fi
bash build-target.sh tf2_demo_native world_geometry_probe > "$SCRATCH/m1.$RUN_TAG.build" 2>&1
M1_PVS=$("$PROBE" --tf-root "$TF" --map "$MAP" --eye "-2423.6,5566.0,-1280.0" 2>/dev/null | grep -oE 'visClusters=[0-9]+' | cut -d= -f2)
if [ "$M1_PVS" = "1692" ]; then
  echo "  NOT-RED M1: the mutated pvs popcount still reads 1692"
  mfail=1
else
  echo "  RED M1 as required: pvs popcount = $M1_PVS (the stride-8 read answers another cluster's row)"
fi
cp "$SAVE_MAP" "$SRC_MAP"
if cmp -s "$SAVE_MAP" "$SRC_MAP"; then echo "  RESTORE=OK (M1 source byte-identical)"; else
  echo "  RESTORE=FAIL (M1: the source did not come back byte-identical)"; fail=1; fi
bash build-target.sh tf2_demo_native world_geometry_probe > "$SCRATCH/m1-restore.$RUN_TAG.build" 2>&1

# M2: the class split comes back -- brush is oriented to its face's side-aware
# outward direction too. That is mask 05 of the sixteen, the rule that draws the
# top-down view best (0.980) and the eye worst-but-one (0.325). The flip census
# and the eye oracle both have to move.
echo "-- M2: brush is oriented to the outward direction as well (mask 05) --"
sed -i 's/if (haveFaceOutward \&\& displacement) {/if (haveFaceOutward) {/' "$SRC_MAP"
if grep -q "haveFaceOutward && displacement" "$SRC_MAP"; then
  echo "  FAIL M2 could not patch the source"; mfail=1
else
  bash build-target.sh tf2_demo_native world_geometry_probe > "$SCRATCH/m2.$RUN_TAG.build" 2>&1
  M2_FLIP=$("$PROBE" --tf-root "$TF" --map "$MAP" 2>/dev/null | grep -oE 'flippedTriangles=[0-9]+' | head -1 | cut -d= -f2)
  M2_EYE=$("$PROBE" --tf-root "$TF" --map "$MAP" --ray-eye "-2423.6,5566.0,-1280.0" 24 2>/dev/null | grep -oE 'shippedRuleKeeps=[0-9]+ of [0-9]+' | head -1)
  if [ "$M2_FLIP" = "8514" ]; then
    echo "  NOT-RED M2: the mutated flip census still reads 8514"
    mfail=1
  else
    echo "  RED M2 as required: flip census = $M2_FLIP (was 8514), eye oracle = $M2_EYE (was 458 of 594)"
  fi
fi
cp "$SAVE_MAP" "$SRC_MAP"
if cmp -s "$SAVE_MAP" "$SRC_MAP"; then echo "  RESTORE=OK (M2 source byte-identical)"; else
  echo "  RESTORE=FAIL (M2: the source did not come back byte-identical)"; fail=1; fi
bash build-target.sh tf2_demo_native world_geometry_probe > "$SCRATCH/m2-restore.$RUN_TAG.build" 2>&1

# M3: the class split inverts -- only brush is oriented, displacement keeps the
# map's order. That is mask 02, which draws 0.017 of the top-down view's first
# hits: the mutation that proves the top-down oracle is wired to the class the
# rule names.
echo "-- M3: only brush is oriented (mask 02) --"
sed -i 's/if (haveFaceOutward \&\& displacement) {/if (haveFaceOutward \&\& !displacement) {/' "$SRC_MAP"
if grep -q "if (haveFaceOutward && !displacement) {" "$SRC_MAP"; then
  bash build-target.sh tf2_demo_native world_geometry_probe > "$SCRATCH/m3.$RUN_TAG.build" 2>&1
  M3_TOP=$("$PROBE" --tf-root "$TF" --map "$MAP" --ray-down 96 2>/dev/null | grep -oE 'drawnByShippedRule=[0-9]+ of [0-9]+' | head -1)
  M3_MISFACED=$("$PROBE" --tf-root "$TF" --map "$MAP" 2>/dev/null | grep -oE 'misfaced=[0-9]+' | head -1 | cut -d= -f2)
  if [ "$M3_TOP" = "drawnByShippedRule=3809 of 5381" ]; then
    echo "  NOT-RED M3: the top-down oracle still reads 3809 of 5381"
    mfail=1
  else
    echo "  RED M3 as required: top-down oracle = $M3_TOP (was 3809 of 5381), misfaced = $M3_MISFACED (was 15126)"
  fi
else
  echo "  FAIL M3 could not patch the source"; mfail=1
fi
cp "$SAVE_MAP" "$SRC_MAP"
if cmp -s "$SAVE_MAP" "$SRC_MAP"; then echo "  RESTORE=OK (M3 source byte-identical)"; else
  echo "  RESTORE=FAIL (M3: the source did not come back byte-identical)"; fail=1; fi
bash build-target.sh tf2_demo_native world_geometry_probe > "$SCRATCH/m3-restore.$RUN_TAG.build" 2>&1

# M4: the bsp pakfile lookup goes back to comparing bytes. pl_odyssey stores
# lowercase entries and textdata names them uppercase, so the materials that
# only exist in the pakfile stop resolving: `material-fetch vmtMissing` has to
# climb back off 3.
echo "-- M4: the pakfile lookup compares the stored name byte for byte again --"
SRC_MAT="native/src/world_material.cpp"
SAVE_MAT="$SCRATCH/world_material.cpp.save"
cp "$SRC_MAT" "$SAVE_MAT"
"$PYTHON" - "$SRC_MAT" <<'PYEOF_INNER'
import sys
path = sys.argv[1]
src = open(path, encoding='utf-8').read()
old = "if (normalizePakfileName(entryName) == wantedName) {"
new = "if (entryName == name) {"
assert old in src, "M4 patch site not found"
open(path, 'w', encoding='utf-8', newline='').write(src.replace(old, new))
PYEOF_INNER
if [ $? != 0 ]; then echo "  FAIL M4 could not patch the source"; mfail=1; fi
bash build-target.sh tf2_demo_native > "$SCRATCH/m4.$RUN_TAG.build" 2>&1
M4_BMP="$SCRATCH/m4.$RUN_TAG.bmp"; M4_TRACE="$SCRATCH/m4.$RUN_TAG.trace"
capture "$M4_BMP" "$M4_TRACE" > /dev/null 2>&1
M4_MISS=$(grep -o "gpu: world material-fetch .*" "$M4_TRACE" | head -1 | grep -oE 'vmtMissing=[0-9]+' | cut -d= -f2)
if [ "$M4_MISS" = "3" ]; then
  echo "  NOT-RED M4: the mutated lookup still reports vmtMissing=3"
  mfail=1
else
  echo "  RED M4 as required: vmtMissing = ${M4_MISS:-none} (was 3; the byte-for-byte lookup cannot see the lowercase entries)"
fi
cp "$SAVE_MAT" "$SRC_MAT"
if cmp -s "$SAVE_MAT" "$SRC_MAT"; then echo "  RESTORE=OK (M4 source byte-identical)"; else
  echo "  RESTORE=FAIL (M4: the source did not come back byte-identical)"; fail=1; fi
bash build-target.sh tf2_demo_native > "$SCRATCH/m4-restore.$RUN_TAG.build" 2>&1

# M5: the lightmap sample stops being encoded, so the frame goes back to
# multiplying a display-referred albedo by a linear light. W1's mean luminance
# has to fall out of its pin: that is the whole claim the shader change makes.
echo "-- M5: the lightmap sample is used as a raw multiplier again --"
SRC_REN="native/src/native_renderer.cpp"
SAVE_REN="$SCRATCH/native_renderer.cpp.save"
cp "$SRC_REN" "$SAVE_REN"
"$PYTHON" - "$SRC_REN" <<'PYEOF_INNER'
import sys
path = sys.argv[1]
src = open(path, encoding='utf-8').read()
old = "lightColour = pow(max(lightmapTexture.Sample(sampler0, lightUv).rgb, 0.0), 1.0 / 2.2);"
new = "lightColour = max(lightmapTexture.Sample(sampler0, lightUv).rgb, 0.0);"
assert old in src, "M5 patch site not found"
open(path, 'w', encoding='utf-8', newline='').write(src.replace(old, new))
PYEOF_INNER
if [ $? != 0 ]; then echo "  FAIL M5 could not patch the source"; mfail=1; fi
bash build-target.sh tf2_demo_native > "$SCRATCH/m5.$RUN_TAG.build" 2>&1
M5_BMP="$SCRATCH/m5.$RUN_TAG.bmp"; M5_TRACE="$SCRATCH/m5.$RUN_TAG.trace"
capture "$M5_BMP" "$M5_TRACE" > /dev/null 2>&1
M5_W1=$("$PYTHON" frame-region-stats.py "$M5_BMP" 0,0,420,200 2>/dev/null \
  | grep 'region x=0,y=0' | grep -oE 'meanLum=[-0-9.]+' | cut -d= -f2)
if awk -v v="$M5_W1" 'BEGIN{exit !(v>=73 && v<=99)}'; then
  echo "  NOT-RED M5: the un-encoded frame still reads W1 meanLum = $M5_W1 inside [73,99]"
  mfail=1
else
  echo "  RED M5 as required: W1 meanLum = ${M5_W1:-none} (was 86.3 in [73,99])"
fi
cp "$SAVE_REN" "$SRC_REN"
if cmp -s "$SAVE_REN" "$SRC_REN"; then echo "  RESTORE=OK (M5 source byte-identical)"; else
  echo "  RESTORE=FAIL (M5: the source did not come back byte-identical)"; fail=1; fi
bash build-target.sh tf2_demo_native > "$SCRATCH/m5-restore.$RUN_TAG.build" 2>&1

echo "== verdict =="
if [ "$fail" = 0 ] && [ "$mfail" = 0 ]; then
  echo "PASS: the forward baseline held, all five mutations went red, every source came back byte-identical."
  exit 0
fi
echo "FAIL: forward-fail=$fail mutation-not-red=$mfail"
exit 1
