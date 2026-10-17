#!/usr/bin/env bash
# visual-baseline-check.sh -- T10: the visual correctness baseline gate.
#
# WHY THIS FILE EXISTS
# --------------------
# The release blocker was visual: the reference POV frame showed broken
# material blocks, a partial ViewModel and a world that never became a place.
# T10's staged diagnosis found and fixed two root causes in the world region:
#
#   A. the pvs clip asked `clusterAt` for the cluster of the observer's
#      NORMALIZED unit-box point as if it were MAP coordinates, so every
#      camera read as one fixed cluster (2017 on pl_odyssey) and the clip hid
#      an arbitrary half of the map from a place nobody was looking at. The
#      fix is Renderer::observerFocusMapWorld -- the exact inverse of
#      setObserverFocusWorld, living next to it.
#   B. the --debug-camera diagnostic was applied once after the world upload
#      and then cleared by the first frame's camera-mode transition, so every
#      "top-down" reading this task had taken before the fix was the default
#      orbit camera. The fix applies the diagnostic camera every frame.
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
#   M1 reverts the eye-space fix (observerFocusMapWorld returns the normalized
#      point again) -- the vis line's eyeMap and cluster pins must break,
#   M2 flips the world pass's back-face cull default off -- the cull line must
#      read backfaces=0.
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
assert_eq "probe triangles" "$(grep -oE 'triangles=[0-9]+' "$PROBE_TXT" | head -1 | cut -d= -f2)" "99612"
assert_eq "probe displaced" "$(grep -oE ' displaced=[0-9]+' "$PROBE_TXT" | head -1 | tr -d ' ' | cut -d= -f2)" "47232"
assert_eq "probe degenerate" "$(grep -oE 'degenerate=[0-9]+' "$PROBE_TXT" | head -1 | cut -d= -f2)" "4790"
assert_eq "probe materials covered by atlas cap" "$(grep -oE 'trianglesCoveredByCap=[0-9]+' "$PROBE_TXT" | head -1 | cut -d= -f2)" "98311"
# The eye the product's vis line reports must be the cluster the probe
# computes from the same map coordinates: one number, two instruments.
assert_eq "probe eye cluster" "$(trace_field "$PROBE_TXT" 'geometry eye' 'cluster')" "1032"
assert_eq "probe eye pvs popcount" "$(trace_field "$PROBE_TXT" 'geometry eye' visClusters)" "1021"

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
assert_eq "vis pvs popcount" "$(printf '%s' "$VISLINE" | grep -oE 'visibleFromCamera=[0-9]+' | cut -d= -f2)" "1021"
assert_eq "vis hidden triangles" "$(printf '%s' "$VISLINE" | grep -oE 'hiddenTriangles=[0-9]+' | cut -d= -f2)" "36298"
assert_eq "vis drawn triangles" "$(printf '%s' "$VISLINE" | grep -oE 'drawnTriangles=[0-9]+' | cut -d= -f2)" "63314"
# The eye pin is THE fix's fingerprint: a map-scale position, three instruments
# agreeing (renderer inverse, clusterAt, probe). The mutation reverts it to a
# normalized point near the origin, which cannot match this line.
assert_eq "vis eye (map space)" \
  "$(printf '%s' "$VISLINE" | grep -oE 'eyeMap=\(-?[0-9.]+,-?[0-9.]+,-?[0-9.]+\)' | head -1)" \
  "eyeMap=(-2423.6,5566.0,-1280.0)"

CULLLINE=$(grep -o "gpu: world cull .*" "$REF_TRACE" | head -1)
assert_eq "world cull line" "$CULLLINE" "gpu: world cull backfaces=1 mode=cull-back"

GEOM=$(grep -o "gpu: world geometry .*" "$REF_TRACE" | head -1)
assert_eq "gpu displaced triangles" "$(printf '%s' "$GEOM" | grep -oE 'displacedTriangles=[0-9]+' | cut -d= -f2)" "47232"
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
for spec in "x=0,y=0 51 86 48 89 0.27 9.2 19.1" \
            "x=0,y=560 48 86 49 90 0.72 2.8 5.6" \
            "x=1100,y=100 13 26 10 18 0.93 0.8 1.5" \
            "x=600,y=0 42 70 43 80 0.34 14.3 28.5"; do
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
SRC_RENDER="native/src/native_renderer.cpp"
SAVE_RENDER="$SCRATCH/native_renderer.cpp.save"
cp "$SRC_RENDER" "$SAVE_RENDER"

# M1: revert the eye-space fix -- observerFocusMapWorld hands back the
# normalized point, which is the defect the fix removed.
echo "-- M1: observerFocusMapWorld returns the normalized focus --"
"$PYTHON" - "$SRC_RENDER" <<'PYEOF'
import sys
path = sys.argv[1]
src = open(path, encoding='utf-8').read()
old = (
  "  x = (observerFocusX_ - 0.0f) / worldHorizontalScale_ + worldCenterX_;\n"
  "  y = (observerFocusY_ - 0.0f) / worldHorizontalScale_ + worldCenterY_;\n"
  "  z = (observerFocusZ_ - 0.1f) / 0.8f * worldSpanZ_ + worldMinZ_;\n")
new = (
  "  x = observerFocusX_;\n"
  "  y = observerFocusY_;\n"
  "  z = observerFocusZ_;\n")
assert old in src, "M1 patch site not found"
open(path, 'w', encoding='utf-8', newline='').write(src.replace(old, new))
PYEOF
if [ $? != 0 ]; then echo "  FAIL M1 could not patch the source"; fail=1; fi
bash build-target.sh tf2_demo_native > "$SCRATCH/m1.$RUN_TAG.build" 2>&1
M1_BMP="$SCRATCH/m1.$RUN_TAG.bmp"; M1_TRACE="$SCRATCH/m1.$RUN_TAG.trace"
capture "$M1_BMP" "$M1_TRACE"
M1_RC=$?
if [ "$M1_RC" != "0" ]; then echo "  M1: the mutated build could not run (rc=$M1_RC) -- red as required"; else
  M1_EYE=$(grep -o "gpu: world vis .*" "$M1_TRACE" | head -1 | grep -oE 'eyeMap=\(-?[0-9.]+,-?[0-9.]+,-?[0-9.]+\)' | head -1)
  M1_CLUSTER=$(printf '%s' "$(grep -o "gpu: world vis .*" "$M1_TRACE" | head -1)" | grep -oE 'cameraCluster=[0-9-]+' | cut -d= -f2)
  if [ "$M1_EYE" = "eyeMap=(-2423.6,5566.0,-1280.0)" ] || [ "$M1_CLUSTER" = "1032" ]; then
    echo "  NOT-RED M1: the mutated eye/cluster pins still hold (eye='$M1_EYE' cluster=$M1_CLUSTER)"
    mfail=1
  else
    echo "  RED M1 as required: eye='$M1_EYE' cluster=$M1_CLUSTER"
  fi
fi
cp "$SAVE_RENDER" "$SRC_RENDER"
if cmp -s "$SAVE_RENDER" "$SRC_RENDER"; then echo "  RESTORE=OK (M1 source byte-identical)"; else
  echo "  RESTORE=FAIL (M1: the source did not come back byte-identical)"; fail=1; fi
bash build-target.sh tf2_demo_native > "$SCRATCH/m1-restore.$RUN_TAG.build" 2>&1

# M2: flip the world pass's back-face cull default off. The governing default
# is the caller's apply-site expression (main.cpp re-states it every frame,
# which is what makes the member default unreachable), so that is the line the
# mutation moves.
echo "-- M2: the world cull apply-site default flips to false --"
SAVE_MAIN="$SCRATCH/main.cpp.save"
cp native/src/main.cpp "$SAVE_MAIN"
sed -i 's/renderer.setWorldBackfaceCull(worldCullGiven ? worldCullRequested : true)/renderer.setWorldBackfaceCull(worldCullGiven ? worldCullRequested : false)/' native/src/main.cpp
if grep -q "renderer.setWorldBackfaceCull(worldCullGiven ? worldCullRequested : false)" native/src/main.cpp; then
  bash build-target.sh tf2_demo_native > "$SCRATCH/m2.$RUN_TAG.build" 2>&1
  M2_BMP="$SCRATCH/m2.$RUN_TAG.bmp"; M2_TRACE="$SCRATCH/m2.$RUN_TAG.trace"
  capture "$M2_BMP" "$M2_TRACE"
  M2_RC=$?
  if [ "$M2_RC" != "0" ]; then echo "  M2: the mutated build could not run (rc=$M2_RC) -- red as required"; else
    M2_CULL=$(grep -o "gpu: world cull .*" "$M2_TRACE" | head -1)
    if [ "$M2_CULL" = "gpu: world cull backfaces=1 mode=cull-back" ]; then
      echo "  NOT-RED M2: the cull line still reads backfaces=1"
      mfail=1
    else
      echo "  RED M2 as required: $M2_CULL"
    fi
  fi
else
  echo "  FAIL M2 could not patch the source"; mfail=1
fi
cp "$SAVE_MAIN" native/src/main.cpp
if cmp -s "$SAVE_MAIN" native/src/main.cpp; then echo "  RESTORE=OK (M2 source byte-identical)"; else
  echo "  RESTORE=FAIL (M2: the source did not come back byte-identical)"; fail=1; fi
bash build-target.sh tf2_demo_native > "$SCRATCH/m2-restore.$RUN_TAG.build" 2>&1

echo "== verdict =="
if [ "$fail" = 0 ] && [ "$mfail" = 0 ]; then
  echo "PASS: the forward baseline held, both mutations went red, both sources came back byte-identical."
  exit 0
fi
echo "FAIL: forward-fail=$fail mutation-not-red=$mfail"
exit 1
