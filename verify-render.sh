#!/usr/bin/env bash
# verify-render.sh -- tier 2 for the materials / VTF / BSP / rendering subsystem.
#
# WHY THIS EXISTS
#   See verify-entity.sh for the three-tier shape. This is the render half: the
#   gates that can see a material, a texture or a pixel. Everything expensive in
#   verify-all.sh that is *not* about rendering -- the corpus sample, the SourceTV
#   sweep, the entity gates -- is left out, so "the material work is done" can be
#   answered in ~10 min instead of ~85.
#
# WHAT IT DOES NOT SAY
#   It does not print VERIFY=PASS and is not a merge gate. In particular it says
#   nothing about entity decoding, audio, or SourceTV coverage.
#
# WHERE THE READINGS GO
#   All three gates below write into the committed `evidence/` directories by
#   default.
#   This tier redirects them into `evidence/fast/render/` and then *asserts* that
#   nothing under `evidence/` moved. The redirect matters most for frame-capture,
#   whose metrics CSVs carry wall-clock and memory readings and therefore differ
#   on every run; the captured BMPs, by contrast, come back byte-identical, which
#   is the property that gate exists to assert.
#
# THE GATES
#   1. resource reachability -- which of main.cpp's five archives actually opened
#      (a missing one is skipped *silently*, no error and no counter), whether the
#      demo map's BSP is reachable at all, how many of its materials resolve to
#      pixels, how many survive the 512x512 world-atlas cap, and how many
#      triangles those cover. This is the gate that separates "the demo scene
#      rendered" from "the capture came back as the fallback spray-decal quad".
#   2. frame capture -- the only gate that can say anything about the picture: a
#      frame is taken out of the running program, the BMP is checked against its
#      own size, the same static scene twice is byte-identical, and two demo
#      scenes differ from the paused one.
#   3. entity material -- a model drawn as an entity must be painted with its own
#      material, and the program must survive the tick at which entities appear.
#      Every gate above captures at `--capture-tick 0`, i.e. playback tick 16,
#      which is before this demo's first checkpoint: entities do not exist yet,
#      so a defect that only fires once they do is outside every gate's field of
#      view. On 2026-10-09 one did, and only this gate was there to see it.
#   4. the material chain's synthetic layer -- VTF parse rejections (empty,
#      corrupt, oversized, cubemap-as-2d), the BSP defaults (an empty BSP is
#      rejected, no-lighting is the default), and the VMT feature mapping. This
#      is the parse layer under gates 1 and 2; it runs in milliseconds and needs
#      no assets.
#
# WHY texture_quad_probe IS NOT HERE
#   `texture_quad_probe <file.vtf>` uploads a real VTF through D3D11 on a WARP
#   device and draws it -- the one reading that would say the GPU-side half of
#   the texture path works. It is deliberately not wired into this tier, because
#   its input would have to be a loose file from the installed game
#   (`tf/materials/vgui/logos/spray.vtf` is the only one on this machine) and an
#   input set that is a property of the machine is a defect this repo has already
#   paid for twice: step 9 sampled the live Steam directory and went red for the
#   right reason when two recordings landed, and step 5 had the same latent
#   defect and stayed green while producing unreproducible evidence. Wiring this
#   probe needs a VTF fixture *committed to this repository* first. Until then
#   the honest statement is "the probe exists and runs", not "the texture path is
#   verified".
#
# Usage: bash verify-render.sh [--mutation] [--no-build]
#   --mutation adds the perturbation runs of gates 1, 2 and 3 (~+12 min).
# Exit:  0 = every gate passed.
set -uo pipefail
cd "$(dirname "$0")"

OUT="${VERIFY_RENDER_OUT:-evidence/fast/render}"
mkdir -p "$OUT"

MUTATION=0
BUILD=1
for arg in "$@"; do
  case "$arg" in
    --mutation) MUTATION=1 ;;
    --no-build) BUILD=0 ;;
    *) echo "unknown argument: $arg"; exit 2 ;;
  esac
done
MARG=""
[ "$MUTATION" -eq 1 ] && MARG="--mutation"

# See verify-entity.sh: the committed evidence tree must come out of this run
# unchanged, and that is checked rather than intended.
EVIDENCE_BEFORE=$(git status --porcelain evidence/ 2>/dev/null | sort)

B=native/build-nmake

if [ "$BUILD" -eq 1 ]; then
  echo "=== 1/4 incremental build ==="
  # tf2_demo_native is the program frame-capture and entity-material take their
  # frames from; entity_model_probe is entity-material's resolve-from-bytes half.
  bash build-target.sh tf2_demo_native entity_model_probe resource_reachability_probe world_material_probe material_chain_probe
  rc_build=$?
  echo "build_rc=$rc_build"
  [ "$rc_build" -eq 0 ] || { echo "VERIFY-RENDER=FAIL (build)"; exit 1; }
else
  echo "=== 1/4 incremental build (skipped: --no-build) ==="
fi

ok=0
bad=0

gate() { # gate <label> <script> <pass-marker-regex> <out-env-var>
  local label="$1" script="$2" marker="$3" outvar="$4"
  local log="$OUT/$label.txt"
  # Redirected for the same reason as in verify-entity.sh: the gate's default OUT
  # is the committed evidence directory, and a tier run must not rewrite a
  # committed record. `frame-capture` is the one that bites here -- its metrics
  # CSVs carry wall-clock and memory readings, so they differ on every run.
  env "$outvar=$OUT/$label" bash "$script" $MARG > "$log" 2>&1
  local rc=$?
  # See verify-entity.sh: under --mutation these gates print only
  # `MUTATION-CAUGHT=PASS` and do not print their normal verdict line.
  local pattern="$marker"
  [ "$MUTATION" -eq 1 ] && pattern="MUTATION-CAUGHT=PASS|$marker"
  local line
  line=$(grep -oE "$pattern" "$log" | head -1)
  if [ "$rc" -eq 0 ] && [ -n "$line" ]; then
    ok=$((ok + 1)); printf '  OK   %-22s %s\n' "$label" "$line"
  else
    bad=$((bad + 1)); printf '  FAIL %-22s rc=%s marker=%s\n' "$label" "$rc" "${line:-<none>}"
    tail -6 "$log" | sed 's/^/       | /'
  fi
}

echo
echo "=== 2/4 the gates ==="
gate resource-reachability resource-reachability-check.sh 'RESOURCE-REACHABILITY=(PASS|FAIL)' RESOURCE_CHECK_OUT
gate frame-capture         frame-capture-check.sh         'FRAME-CAPTURE=(PASS|FAIL)'         FRAME_CAPTURE_OUT
gate entity-material       entity-material-check.sh       'ENTITY-MATERIAL=(PASS|FAIL)'       ENTITY_MATERIAL_OUT

echo
echo "=== 3/4 the material chain's synthetic layer ==="
CHAIN_OUT="$OUT/material-chain.txt"
"$B/material_chain_probe.exe" > "$CHAIN_OUT" 2>&1
rc_chain=$?
chain_ok=0
chain_bad=0
assert_chain() { # assert_chain <fixed string> <what>
  if grep -qF -- "$1" "$CHAIN_OUT"; then
    chain_ok=$((chain_ok + 1)); printf '  OK   %s\n' "$2"
  else
    chain_bad=$((chain_bad + 1)); printf '  FAIL %s (wanted: %s)\n' "$2" "$1"
  fi
}
# Only the synthetic keys are asserted. The probe's real-input keys
# (`explicitVtfDecoded`, `archiveOpen`, `realMaterials`, ...) read false/0 in this
# no-argument mode because no --vtf/--vmt/tf-root was given -- they are the
# "no input" state, not a failure, and asserting them here would be asserting a
# skip. The real-input path is covered by gate 1, which runs the probe against
# the installed VPK.
assert_chain '"emptyVtfRejected":true'         'an empty VTF is rejected'
assert_chain '"corruptVtfRejected":true'       'a corrupt VTF is rejected'
assert_chain '"oversizedVtfRejected":true'     'a >64 MB VTF is rejected before allocation'
assert_chain '"cubemapDecodeRejected":true'    'a cubemap is refused by the 2d decode path'
assert_chain '"cubemapDepth6Parsed":true'      'a depth-6 cubemap header parses'
assert_chain '"cubemapFaceDataOrdered":true'   'cubemap face data is ordered'
assert_chain '"emptyBspRejected":true'         'an empty BSP is rejected'
assert_chain '"noLightingDefault":true'        'no-lighting is the default'
assert_chain '"vmtFeatureMapping":true'        'VMT shader features map'
assert_chain '"waterFeatureMapping":true'      'water shader features map'
assert_chain '"cubemapMode":"approximate-2d"'  'cubemap mode is the honest 2d approximation'
assert_chain '"lightmapMode":"average-intensity"' 'lightmap mode is documented as an average'
if [ "$rc_chain" -ne 0 ]; then
  chain_bad=$((chain_bad + 1)); printf '  FAIL material_chain_probe exited %s\n' "$rc_chain"
fi
if [ "$chain_bad" -eq 0 ]; then
  ok=$((ok + 1)); printf '  ->   %-22s %s/%s synthetic keys held\n' material-chain "$chain_ok" "$((chain_ok + chain_bad))"
else
  bad=$((bad + 1)); printf '  ->   %-22s %s red\n' material-chain "$chain_bad"
fi

echo
echo "=== 4/4 the committed evidence tree must be untouched ==="
EVIDENCE_AFTER=$(git status --porcelain evidence/ 2>/dev/null | sort)
if [ "$EVIDENCE_BEFORE" = "$EVIDENCE_AFTER" ]; then
  ok=$((ok + 1)); printf '  OK   %-22s nothing under evidence/ moved (tier output stayed in %s)\n' evidence-tree "$OUT"
else
  bad=$((bad + 1)); printf '  FAIL %-22s this tier wrote into the committed evidence tree:\n' evidence-tree
  diff <(printf '%s\n' "$EVIDENCE_BEFORE") <(printf '%s\n' "$EVIDENCE_AFTER") \
    | grep '^[<>]' | sed 's/^/       | /'
fi

echo
echo "gates_ok=$ok gates_bad=$bad"
if [ "$bad" -eq 0 ]; then
  echo "VERIFY-RENDER=PASS ($ok/5 gates, mutation=$MUTATION)"
  exit 0
fi
echo "VERIFY-RENDER=FAIL ($bad red)"
exit 1
