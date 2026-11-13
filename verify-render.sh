#!/usr/bin/env bash
# verify-render.sh -- tier 2 for the materials / VTF / BSP / rendering subsystem.
#
# WHY THIS EXISTS
#   See verify-entity.sh for the three-tier shape. This is the render half: the
#   gates that can see a material, a texture or a pixel. Everything expensive in
#   verify-all.sh that is *not* about rendering -- the corpus sample, the SourceTV
#   sweep, the entity gates -- is left out, so "the material work is done" can be
#   answered in ~13 min instead of ~85 (measured 2026-10-09: 5m27s straight,
#   5m45s with --mutation, for gates 1-5; viewmodel-contract measured 7m01s
#   straight on 2026-10-11 and is the expensive one, because it launches the
#   product a dozen times and takes its frames from each launch).
#
# WHAT IT DOES NOT SAY
#   It does not print VERIFY=PASS and is not a merge gate. In particular it says
#   nothing about entity decoding, audio, or SourceTV coverage.
#
# WHERE THE READINGS GO
#   Each gate below writes into its own committed `evidence/` directory by
#   default.
#   This tier redirects them into `evidence/fast/render/` and then *asserts* that
#   nothing under `evidence/` moved. The redirect matters most for frame-capture,
#   whose metrics CSVs carry wall-clock and memory readings and therefore differ
#   on every run; the captured BMPs, by contrast, come back byte-identical, which
#   is the property that gate exists to assert. weapon-animation is redirected for
#   the same reason: its metrics CSVs are per-run, and its committed witness
#   frames must not be rewritten by a tier run. That claim is checked against
#   `git status --porcelain evidence/` and not merely intended, which is why a
#   gate whose default output lands outside `evidence/` still gets a variable
#   here: the assertion is about the tree, and a redirect that is not named is a
#   redirect nobody verified.
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
#   4. weapon animation -- the models gate 3 paints must be skinned from real demo
#      animation input, on the GPU, and the pose must follow the tick rather than
#      merely advancing a frame cursor. Gate 3 can pass on a model whose vertices
#      never move: it asserts that the right *paint* reaches the draw, not that
#      the geometry does anything. This is the gate whose subject is the vertex
#      transform.
#   5. the material chain's synthetic layer -- VTF parse rejections (empty,
#      corrupt, oversized, cubemap-as-2d), the BSP defaults (an empty BSP is
#      rejected, no-lighting is the default), and the VMT feature mapping. This
#      is the parse layer under gates 1 and 2; it runs in milliseconds and needs
#      no assets.
#   6. viewmodel contract -- the first-person weapon is drawn through its own pass,
#      with its own projection, its own attachment chain and its own hand mirror,
#      and a missing resource disables that pass without taking the run down.
#      Gates 1-5 are all about the world: the world pass normalises geometry into
#      the map's unit box and writes depth with the world policy, so a weapon
#      routed through it would be scaled into the map and occluded by it. Nothing
#      above can see whether a ViewModel exists at all.
#   7. viewmodel animation -- the first-person model is drawn from the demo's own
#      animation input rather than held in its bind pose, or the refusal says so
#      out loud. Gate 6 draws the weapon but never asked whether the demo carries
#      a sequence for it; T2 reported `animationInput=missing` and nothing had
#      measured whether that was true. This gate measures it for the class that
#      does carry it (CTFViewModel), separates the two index spaces, reads the
#      owning entity from m_hOwner instead of guessing it, and asserts the
#      negative on a recording where the pairing does not resolve at all.
#   8. viewmodel companion -- gate 7's refutation was correct and left the picture
#      wrong: the entity carries a c_<class>_arms model whose sequence already
#      indexes THAT model's table, so nothing needed translating -- what was
#      missing was a proof that the pass draws the entity's own model. This gate
#      pins that proof (owner, m_nModelIndex, path equality, table membership,
#      bone count) at three ticks DERIVED from a history run that carry three
#      different sequences with three pairwise-different witness frames; keeps
#      gate 7's refusal in place and still red; and is honest about the one thing
#      it cannot show -- see below.
#   9. viewmodel range -- gate 8 proves the pass draws the entity's model; it does
#      not ask whether every range of that model is in the buffer. The arms model
#      is a composite: `c_sniper_arms.mdl` is a 600-index sleeve on material slot 0
#      and a 9396-index pair of hands on slot 1, and the blocker list's "only one
#      600-vertex range" is the sleeve alone. This gate pins the descriptor table,
#      the per-range material slots and bone indices, the four ways a descriptor
#      can be malformed (each must refuse the model, not draw the part that
#      survived), the per-frame `ranges`/`boundSlots` readings, and a repeat
#      capture whose hash must match.
#
# WHAT GATE 8 DOES NOT SAY, AND WHY THE HONESTY MATTERS
#   On both pinned recordings the entity carries sequences 1, 29 and 30, and the
#   model it names can pose sequence 0 and nothing else (its animation file holds
#   one descriptor, one frame). So the naturally-observed `ok` count is 0 and the
#   accepted branch is UNREACHABLE on this input; the gate reports
#   `sequenceAccepted=0` beside `sequenceInRange=` rather than blurring the two.
#   The accepted pose is therefore CONSTRUCTED with `--viewmodel-sequence 0`, read
#   out of the model itself, and every reading line for it carries
#   `sequenceOverride=0` so an overridden run cannot be mistaken for a natural
#   one. An input set that is a property of the machine is this repo's oldest
#   defect; naming the override is what keeps this gate out of that class.
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
#   --mutation replaces gates 1-3 with their perturbation runs, gate 6 with its
#   own five-mutation run, gate 7 with its own three-mutation run and gate 8 with
#   its own six. For gates 1-3 the perturbation replaces that gate's run
#   rather than adding one, so they cost about the same as the straight run
#   (5m45s against 5m27s) -- the +12 min this comment used to claim was never
#   measured. viewmodel-contract is the exception: each of its mutations rebuilds
#   the product and re-captures its frames, so it costs several times its straight
#   run and this tier is correspondingly slower with --mutation. viewmodel-animation
#   is in the same class: its --mutation rebuilds the five targets that link
#   viewmodel.cpp, turns the refusal red, restores the source and rebuilds again.
#   viewmodel-companion is the same again and larger: six mutations, four of them
#   against the probe's own fixtures and two against the render path, each with its
#   own rebuild, its own source-hash check and its own restore build.
# Exit:  0 = every gate passed.
set -uo pipefail
cd "$(dirname "$0")"

OUT="${VERIFY_RENDER_OUT:-evidence/fast/render}"
mkdir -p "$OUT"
# viewmodel-contract redirects its own two output paths. Its witness PNGs go
# where every other gate's output goes (see the `gate` function), and its traces
# -- a few MB of BMP per frame plus per-run metrics CSVs -- go under the same
# tier directory, so the committed evidence tree stays untouched. Exported rather
# than passed per-call because the `gate` helper sets only one variable.
export VIEWMODEL_CONTRACT_TRACE_DIR="$OUT/viewmodel"
# viewmodel-animation's machine products -- six BMP frames, their per-run metrics
# CSVs and a trace per launch -- go under the same tier directory for the same
# reason. It writes nothing into the committed tree: its committed record is the
# pin file evidence/viewmodel/animation-inputs.txt plus the chain's step log.
export VIEWMODEL_ANIMATION_TRACE_DIR="$OUT/viewmodel-animation"
# viewmodel-companion writes the same class of machine product -- a history trace,
# three witness BMPs, an accepted-pose pair and a JSON/text pair per probe run --
# and takes its frames from its own launches. Redirected for the same reason, and
# its own default (`evidence/fast/viewmodel-companion/`) would already have been
# out of the committed tree; naming it here keeps the tier's one claim -- that
# everything this run produces lands in $OUT -- literally true.
export VIEWMODEL_COMPANION_TRACE_DIR="$OUT/viewmodel-companion"
# viewmodel-range writes two captured frames, four probe dumps and the product's
# reading lines, and its default is the committed `evidence/viewmodel/range/`.
# Redirected for the same reason as the three above: a tier run must not rewrite a
# committed record.
export VIEWMODEL_RANGE_EVIDENCE_DIR="$OUT/viewmodel-range"

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
  # tf2_demo_native is the program frame-capture, entity-material,
  # weapon-animation and viewmodel-contract take their frames from;
  # entity_model_probe is entity-material's resolve-from-bytes half;
  # weapon_animation_probe is the instrument weapon-animation uses to separate a
  # moving pose from a moving cursor, and it is a separate binary because that
  # distinction needs a model filter the product does not have.
  # viewmodel_contract_probe and viewmodel_fov_probe are gate 6's instruments and
  # are built here rather than left to verify-fast: a tier script that needs a
  # binary its own build step does not produce only works on a tree where some
  # other script has already run, which is the failure this list exists to avoid.
  # (Measured 2026-10-11: without them gate 6 exits on its preflight's
  # `FATAL: missing input: native/build-nmake/viewmodel_contract_probe.exe` on a
  # tree where verify-fast had not run.)
  bash build-target.sh tf2_demo_native entity_model_probe resource_reachability_probe world_material_probe material_chain_probe weapon_animation_probe viewmodel_contract_probe viewmodel_fov_probe viewmodel_animation_probe viewmodel_range_probe
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
gate weapon-animation      weapon-animation-check.sh      'WEAPON-ANIMATION=(PASS|FAIL)'      WEAPON_ANIMATION_OUT
gate viewmodel-contract    viewmodel-contract-check.sh    'VIEWMODEL-CONTRACT=(PASS|FAIL)'    VIEWMODEL_CONTRACT_OUT
gate viewmodel-animation   viewmodel-animation-check.sh   'VIEWMODEL-ANIMATION=(PASS|FAIL)'   VIEWMODEL_ANIMATION_TRACE_DIR
gate viewmodel-companion   viewmodel-companion-check.sh   'VIEWMODEL-COMPANION=(PASS|FAIL)'   VIEWMODEL_COMPANION_TRACE_DIR
gate viewmodel-range       viewmodel-range-check.sh       'VIEWMODEL-RANGE=(PASS|FAIL)'       VIEWMODEL_RANGE_EVIDENCE_DIR

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
  echo "VERIFY-RENDER=PASS ($ok/10 gates, mutation=$MUTATION)"
  exit 0
fi
echo "VERIFY-RENDER=FAIL ($bad red)"
exit 1
