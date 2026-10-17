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
#
# mutation: two source mutations, each of which must turn the forward pins
#   red, and each source must come back byte-identical:
#   M1 reverts the pvs row-table fix (the stride-8 single-table read) -- the
#      probe's visClusters pin must fall back off 1692,
#   M2 flips the per-class winding sign -- the flip census must move off
#      47252, and with it what the cull mode removes.
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
  local traceWin
  traceWin="$(cygpath -m "$PWD/$trace")"
  local bmpWin
  bmpWin="$(cygpath -m "$PWD/$bmp")"
  TF2_NATIVE_TRACE="$traceWin" timeout 300 "$PRODUCT" --tf-root "$TF" --demo "$DEMO" \
    --capture-tick 2000 --capture-frame "$bmpWin" --audio-disabled \
    --viewmodel models/weapons/v_models/v_scattergun_scout.mdl --viewmodel-render companion \
    "$@" > "$SCRATCH/capture.$RUN_TAG.log" 2>&1
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
  --eye "-2423.6,5566.0,-1280.0" > "$PROBE_TXT" 2>&1
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
# T11: the PVS row table fix. The count of triangles the emitter reversed to
# put each class on the side its own stored data uses. 47252 of 99312 is the
# measured split: displacement grids and brush faces ship opposite winding
# conventions, which is why every single global flip left half the map culled.
assert_eq "probe winding flip census" "$(grep -oE 'flippedTriangles=[0-9]+' "$PROBE_TXT" | head -1 | cut -d= -f2)" "47252"

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
assert_eq "material fetch tiled" "$(printf '%s' "$MATFETCH" | grep -oE 'tiled=[0-9]+' | cut -d= -f2)" "57"
assert_eq "material fetch fromPakfile" "$(printf '%s' "$MATFETCH" | grep -oE 'fromPakfile=[0-9]+' | cut -d= -f2)" "23"
assert_eq "material fetch vtfTooLarge" "$(printf '%s' "$MATFETCH" | grep -oE 'vtfTooLarge=[0-9]+' | cut -d= -f2)" "43"

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
for spec in "x=0,y=0 46 77 52 88 0.10 7.9 13.7" \
            "x=0,y=560 16 29 10 18 0.10 0.6 1.1" \
            "x=1100,y=100 29 50 26 49 0.10 0.5 0.95" \
            "x=600,y=0 7 14 25 46 0.05 3.5 7.5"; do
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

# M2: flip the per-class winding sign -- the displacement/brush split goes
# back to one side, which is the state that left half the map culled.
echo "-- M2: the per-class winding sign flips --"
sed -i 's/const float classSign = displacement ? 1.0f : -1.0f;/const float classSign = displacement ? -1.0f : 1.0f;/' "$SRC_MAP"
if grep -q "const float classSign = displacement ? -1.0f : 1.0f;" "$SRC_MAP"; then
  bash build-target.sh tf2_demo_native world_geometry_probe > "$SCRATCH/m2.$RUN_TAG.build" 2>&1
  M2_FLIP=$("$PROBE" --tf-root "$TF" --map "$MAP" 2>/dev/null | grep -oE 'flippedTriangles=[0-9]+' | head -1 | cut -d= -f2)
  if [ "$M2_FLIP" = "47252" ]; then
    echo "  NOT-RED M2: the mutated flip census still reads 47252"
    mfail=1
  else
    echo "  RED M2 as required: flip census = $M2_FLIP (was 47252)"
  fi
else
  echo "  FAIL M2 could not patch the source"; mfail=1
fi
cp "$SAVE_MAP" "$SRC_MAP"
if cmp -s "$SAVE_MAP" "$SRC_MAP"; then echo "  RESTORE=OK (M2 source byte-identical)"; else
  echo "  RESTORE=FAIL (M2: the source did not come back byte-identical)"; fail=1; fi
bash build-target.sh tf2_demo_native world_geometry_probe > "$SCRATCH/m2-restore.$RUN_TAG.build" 2>&1

echo "== verdict =="
if [ "$fail" = 0 ] && [ "$mfail" = 0 ]; then
  echo "PASS: the forward baseline held, both mutations went red, both sources came back byte-identical."
  exit 0
fi
echo "FAIL: forward-fail=$fail mutation-not-red=$mfail"
exit 1
