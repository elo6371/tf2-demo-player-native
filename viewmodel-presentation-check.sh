#!/usr/bin/env bash
# viewmodel-presentation-check.sh -- T22: the composite is a picture of a hand on
# a weapon, and each of the three claims has to be measured on its own.
#
# THE QUESTION
#   Three tasks each proved one half of the first-person composite, and each said
#   so in its own report:
#
#     T13  the sniper's arms are two ranges (600-vertex sleeve, 9396-vertex hand)
#          on two material slots, and both are drawn.
#     T14  the weapon body is two ranges on two slots, and each range binds the
#          resource its own slot names.
#     T18  the applied pose is a picture, not a counter: the camera did not move,
#          the bone table is read correctly, and the frame changes inside the
#          ViewModel's own coverage.
#
#   What none of them measured is the composite: whether the hand is still ON the
#   weapon after the pose, and whether each range's own albedo is what the pixels
#   show. T22's step 2 is explicit that `skinning=1` and `drawCalls>0` are not
#   visual acceptance, so this gate never reads a pass as evidence for a picture.
#
# THE MEASUREMENT, IN ONE SENTENCE
#   Force slot k onto every range of one model and subtract: the pixels that move
#   belong to the ranges that do NOT natively use slot k. Two ranges, two forced
#   frames, and the two difference sets are disjoint and add up to the difference
#   between the forced frames exactly. That is per-range material binding measured
#   in pixels, with no counter in the argument.
#
#   The same subtraction gives the HAND MASK (force the sleeve's slot onto the
#   hand) and the BODY MASK (draw the body, then not). Contact between the two
#   masks is then a picture-space reading of "the hand is on the weapon": the
#   posed hand keeps its distance-1 contact with the body, keeps 99.99% of its
#   pixels inside the bind hand's mask, and the body's own stock below the hand
#   does not move by one byte.
#
# WHAT IS *NOT* CLAIMED
#   The weapon body is static: it is drawn from its own request and the pose does
#   not transform it. The pose moves the hand 6.35 px of a 1264-px frame, which
#   leaves the grip coherent at this tick and is not evidence about a playing
#   sequence. That is recorded as NOT VERIFIED in the delivery report, not argued
#   away here.
#
# Usage: bash viewmodel-presentation-check.sh [--mutation]
# Exit:  0 = every assertion held (with --mutation: every defect was caught and
#            every forward reading still held), 1 = it did not,
#        2 = another run owns the lock or an input is missing.
set -uo pipefail
cd "$(dirname "$0")"

MUTATION=0
while [ $# -gt 0 ]; do
  case "$1" in
    --mutation) MUTATION=1 ;;
    *) echo "unknown argument: $1"; exit 2 ;;
  esac
  shift
done

EXE=native/build-nmake/tf2_demo_native.exe
TF="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
INPUTS=evidence/viewmodel/animation-inputs.txt
VM_MODEL="models/weapons/v_models/v_scattergun_scout.mdl"
ARMS_MODEL="models/weapons/c_models/c_sniper_arms.mdl"
BODY_OVERRIDE="models/weapons/c_models/c_tfc_sniperrifle/c_tfc_sniperrifle.mdl"
TICK=0
POSE_SEQUENCE=1
RUN_TIMEOUT=300
RUN_TAG="$(date +%Y%m%d%H%M%S)-$$"
EVIDENCE="${VIEWMODEL_PRESENTATION_EVIDENCE_DIR:-evidence/viewmodel/presentation}"
SCRATCH=.scratch
RUN_DIR="$SCRATCH/viewmodel-presentation-$RUN_TAG"
mkdir -p "$EVIDENCE" "$SCRATCH" "$RUN_DIR"
ROOT=$(cygpath -m "$(pwd)" 2>/dev/null || pwd)

TF=${TF%$'\r'}
INPUTS=${INPUTS%$'\r'}
VM_MODEL=${VM_MODEL%$'\r'}
ARMS_MODEL=${ARMS_MODEL%$'\r'}
BODY_OVERRIDE=${BODY_OVERRIDE%$'\r'}

LOCK_DIR=$SCRATCH/viewmodel-presentation.lock
release_lock() { rm -f "$LOCK_DIR/pid" 2>/dev/null; rmdir "$LOCK_DIR" 2>/dev/null; }
# The lock records its owner and reclaims a lock whose owner is gone. A killed
# run cannot remove its own lock (bash does not run an EXIT trap for a signal it
# has no handler for), and a stale lock reads like concurrency when it is really
# a corpse -- the lesson viewmodel-apply-check.sh recorded on 2026-10-19.
if [ -d "$LOCK_DIR" ]; then
  lock_owner=$(cat "$LOCK_DIR/pid" 2>/dev/null)
  if [ -n "$lock_owner" ] && kill -0 "$lock_owner" 2>/dev/null; then
    echo "VIEWMODEL-PRESENTATION=FAIL (pid $lock_owner is running and owns $LOCK_DIR)"
    exit 2
  fi
  echo "  ..   removing the lock left by pid ${lock_owner:-<unknown>}; that run is gone"
  release_lock
fi
if ! mkdir "$LOCK_DIR" 2>/dev/null; then
  echo "VIEWMODEL-PRESENTATION=FAIL (another viewmodel-presentation-check run owns $LOCK_DIR)"
  exit 2
fi
echo $$ > "$LOCK_DIR/pid"

fail=0
OK_COUNT=0
FAIL_COUNT=0
FAIL_LINES=()
MUTATION_TOTAL=0
MUTATION_CATCHES=0
# The reading variables are declared empty so the evidence block cannot print a
# value from an earlier section as if this run had produced it.
COVER=""; COVER_APPLY=""; COVER_K=""; ARMS_COVER_LINE=""; K_COVER_LINE=""
HAND_LINE=""; SLEEVE_LINE=""; UNION_LINE=""; K_WEAPON_LINE=""; K_HANDS_LINE=""; K_UNION_LINE=""
BODY_SCOPE_LINE=""; BODY_RIFLE_LINE=""; BODY_UNION_LINE=""; BODY_POSE=""
BIND_HAND=""; APPLY_HAND=""; HAND_INSIDE=""; POSE=""

pass() { echo "  OK   $1"; OK_COUNT=$((OK_COUNT + 1)); }
fail_line() {
  echo "  FAIL $1"
  fail=1; FAIL_COUNT=$((FAIL_COUNT + 1)); FAIL_LINES+=("$1")
}
assert_eq() { # assert_eq <label> <actual> <expected>
  if [ "${2:-<none>}" = "$3" ]; then pass "$1 = $(printf '%s' "${2:-<none>}")"
  else fail_line "$1 = ${2:-<none>}, expected $3"; fi
}
assert_le() { # assert_le <label> <actual> <upper bound>
  if [ -z "${2:-}" ]; then fail_line "$1 is missing, expected <= $3"
  elif awk -v a="$2" -v b="$3" 'BEGIN { exit !(a <= b) }'; then pass "$1 = $2 (<= $3)"
  else fail_line "$1 = $2, expected <= $3"; fi
}
assert_ge() { # assert_ge <label> <actual> <lower bound>
  if [ -z "${2:-}" ]; then fail_line "$1 is missing, expected >= $3"
  elif awk -v a="$2" -v b="$3" 'BEGIN { exit !(a >= b) }'; then pass "$1 = $2 (>= $3)"
  else fail_line "$1 = $2, expected >= $3"; fi
}
assert_gt() { # assert_gt <label> <actual> <lower bound>
  if [ -z "${2:-}" ]; then fail_line "$1 is missing, expected > $3"
  elif awk -v a="$2" -v b="$3" 'BEGIN { exit !(a > b) }'; then pass "$1 = $2 (> $3)"
  else fail_line "$1 = $2, expected > $3"; fi
}
assert_contains() { # assert_contains <label> <haystack> <needle>
  case "${2:-}" in
    *"$3"*) pass "$1" ;;
    *) fail_line "$1 (the line does not contain '$3')" ;;
  esac
}

reading() { # reading <trace> <pattern> <key>
  tr -d '\r' < "$1" 2>/dev/null | grep -a "$2" | tail -1 \
    | grep -o "$3=[^ ]*" | tail -1 | cut -d= -f2-
}
reading_line() { # reading_line <trace> <pattern>
  tr -d '\r' < "$1" 2>/dev/null | grep -a "$2" | tail -1 | sed 's/^[0-9.]\{1,\} //'
}
# bmp-diff-mask prints `outside=` twice once its reason clause spells the bound,
# so every parse here reads the FIELD section only (T18's §8.4). Field parsing is
# also why the numbers are read with `head -1`: a tool that prints two lines would
# otherwise let the second one silently answer.
mask_field() { # mask_field <line> <key>
  printf '%s\n' "$1" | head -1 | grep -o "$2=[0-9.-]*" | head -1 | cut -d= -f2-
}
mask_pair_field() { # mask_pair_field <line> <key>  (values like (x=1..2, y=3..4))
  printf '%s\n' "$1" | head -1 | grep -o "$2=([^)]*)" | head -1
}
mask_tuple_field() { # mask_tuple_field <line> <key>  (values like (685.58,359.80))
  printf '%s\n' "$1" | head -1 | grep -o "$2=([^)]*)" | head -1 | cut -d= -f2- | tr -d '()'
}

# ---------------------------------------------------------------------------
# Section 0 -- pinned inputs
# ---------------------------------------------------------------------------
echo "=== 0/7 the pinned recording and the model files ==="
if [ ! -x "$EXE" ]; then
  echo "FATAL: missing input: $EXE (build it first)"
  release_lock
  exit 2
fi
for tool in bmp-diff-mask.py bmp-mask-contact.py bmp-pixel-diff.py bmp-crop-grid.py bmp-to-png.py; do
  if [ ! -f "$tool" ]; then
    echo "FATAL: missing input: $tool"
    release_lock
    exit 2
  fi
done
DEMO=""
PINNED=0
while read -r size sha256 root name; do
  name=${name%$'\r'}
  case "$size" in ''|'#'*) continue ;; esac
  [ "$root" = "steam" ] || continue
  path="$TF/demos/$name"
  if [ ! -f "$path" ]; then
    fail_line "pinned recording is absent: $path"; continue
  fi
  actual_size=$(stat -c%s "$path")
  actual_sha=$(sha256sum "$path" | cut -d' ' -f1)
  if [ "$actual_size" != "$size" ] || [ "$actual_sha" != "$sha256" ]; then
    fail_line "pinned recording $name is $actual_size/$actual_sha, pinned $size/$sha256"; continue
  fi
  pass "$name pinned ($size bytes, ${sha256:0:16}...)"
  PINNED=$((PINNED + 1))
  DEMO="$path"
done < "$INPUTS"
assert_eq "pinned recordings verified" "$PINNED" "1"
if [ -z "$DEMO" ]; then
  echo "VIEWMODEL-PRESENTATION=FAIL (no recording)"
  release_lock
  exit 1
fi
# The models live inside the shipped VPKs, so "the file is on disk" is the wrong
# question; whether they resolve is asserted from the product's own readings.
FORWARD_EXE_HASH=$(sha256sum "$EXE" | cut -d' ' -f1)
pass "executable hash recorded ($FORWARD_EXE_HASH)"

# A live neighbour cannot move a PAUSED capture (measured by viewmodel-range-
# check.sh: byte-identical with four product processes live) but it can make the
# run slower, so it is recorded rather than refused.
OTHER_PRODUCT=$(ps -efW 2>/dev/null | grep "[t]f2_demo_native.exe" | head -1)
HOST_OVERLAP=0
if [ -n "$OTHER_PRODUCT" ]; then
  HOST_OVERLAP=1
  echo "  ..   another product run is live; these captures are paused, so the reading is unaffected:"
  echo "  ..   $OTHER_PRODUCT"
fi

# ---------------------------------------------------------------------------
# Section 1 -- the captures
# ---------------------------------------------------------------------------
# One function, one flag set. Every frame in this gate differs from every other
# by the flags below and by nothing else, which is what makes the subtractions
# attributable. `--viewmodel-fov 90` is fixed here rather than at a call site for
# the same reason.
run_product() { # run_product <name> [extra args...]
  local name="$1"; shift
  rm -f "$RUN_DIR/$name.bmp" "$RUN_DIR/$name.trace"
  TF2_NATIVE_TRACE="$ROOT/$RUN_DIR/$name.trace" timeout "$RUN_TIMEOUT" "$EXE" \
    --tf-root "$TF" --audio-device "" --demo "$DEMO" \
    --hidden-window --audio-disabled \
    --settings-file "$RUN_DIR/$name-settings.ini" \
    --capture-frame "$RUN_DIR/$name.bmp" --capture-tick "$TICK" \
    --metrics-file "$RUN_DIR/$name.csv" \
    --viewmodel-fov 90 --start-paused "$@" > "$RUN_DIR/$name.log" 2>&1
  local rc=$?
  local size
  size=$(stat -c%s "$RUN_DIR/$name.bmp" 2>/dev/null || echo 0)
  echo "  $name rc=$rc bmp=$size"
  [ "$rc" -eq 0 ] || fail_line "$name exited $rc"
  [ "$size" -gt 0 ] || fail_line "$name produced no frame"
}

# The three material-effect switches are OFF for the partition frames and ON for
# the presentation frames. Why: forcing a slot changes the pass's *global* effect
# constants as well as the per-range albedo, so with effects on the same two
# subtractions overlap by 5620 pixels (measured). With them off the sets are
# disjoint and add up exactly, which is the claim. The presentation frame keeps
# the product's natural look and is never used for the partition.
FX="--viewmodel-phong off --viewmodel-bump off --viewmodel-selfillum off"

echo
if [ "$MUTATION" -eq 1 ]; then
  echo "=== 1/7 --mutation: four reference captures from the unmodified tree ==="
  run_product bind    --viewmodel "$VM_MODEL" --viewmodel-sequence "$POSE_SEQUENCE"
  run_product world   --viewmodel-fov 90
  run_product bon     --viewmodel "$VM_MODEL" --viewmodel-sequence "$POSE_SEQUENCE" --viewmodel-weapon-model "$BODY_OVERRIDE"
  run_product bslot0  --viewmodel "$VM_MODEL" --viewmodel-sequence "$POSE_SEQUENCE" --viewmodel-weapon-model "$BODY_OVERRIDE" --viewmodel-weapon-material-slot 0
  run_product bslot1  --viewmodel "$VM_MODEL" --viewmodel-sequence "$POSE_SEQUENCE" --viewmodel-weapon-model "$BODY_OVERRIDE" --viewmodel-weapon-material-slot 1
else
  echo "=== 1/7 nineteen captures: presentation, pose, partition, body ==="
  # Presentation: the product's natural look, with the body the entity names.
  run_product bind     --viewmodel "$VM_MODEL" --viewmodel-sequence "$POSE_SEQUENCE"
  run_product apply    --viewmodel "$VM_MODEL" --viewmodel-sequence "$POSE_SEQUENCE" --viewmodel-apply-animation
  run_product repeat   --viewmodel "$VM_MODEL" --viewmodel-sequence "$POSE_SEQUENCE" --viewmodel-apply-animation
  # The two frames that define the body's own coverage: drawn, and not drawn.
  run_product bodyoff  --viewmodel "$VM_MODEL" --viewmodel-sequence "$POSE_SEQUENCE" --viewmodel-weapon-body off
  run_product bodyflat --viewmodel "$VM_MODEL" --viewmodel-sequence "$POSE_SEQUENCE" --viewmodel-weapon-flat-color
  # The control with no ViewModel pass at all. A pixel where this and another
  # frame differ is a pixel the composite covers.
  run_product world    --viewmodel-fov 90
  # Partition frames for the arms: effects off so the two differences are the
  # albedo and nothing else.
  run_product pbind    --viewmodel "$VM_MODEL" --viewmodel-sequence "$POSE_SEQUENCE" $FX
  run_product papply   --viewmodel "$VM_MODEL" --viewmodel-sequence "$POSE_SEQUENCE" $FX --viewmodel-apply-animation
  run_product pslot0   --viewmodel "$VM_MODEL" --viewmodel-sequence "$POSE_SEQUENCE" $FX --viewmodel-material-slot 0
  run_product pslot1   --viewmodel "$VM_MODEL" --viewmodel-sequence "$POSE_SEQUENCE" $FX --viewmodel-material-slot 1
  run_product papplyslot0 --viewmodel "$VM_MODEL" --viewmodel-sequence "$POSE_SEQUENCE" $FX --viewmodel-apply-animation --viewmodel-material-slot 0
  # The arms alone, so a change on the body cannot be counted as a change on the
  # arms. `world` above is the other half of that mask and is shared: the world
  # pass is byte-identical under every ViewModel flag (asserted in section 3).
  run_product pbodyoff --viewmodel "$VM_MODEL" --viewmodel-sequence "$POSE_SEQUENCE" $FX --viewmodel-weapon-body off
  # The complete first-person model: the shipped `v_scattergun_scout` is ONE mesh
  # of two ranges -- the scattergun (9723 indices) and the scout's hands (13446) --
  # on two different albedos. It cannot be posed on this recording (the entity
  # names a different model, so the product refuses the pose by design), and that
  # is exactly why it is here: it makes the range claim independent of the pose.
  run_product kbind    --viewmodel-render command --viewmodel "$VM_MODEL" --viewmodel-weapon-body off $FX
  run_product kslot0   --viewmodel-render command --viewmodel "$VM_MODEL" --viewmodel-weapon-body off $FX --viewmodel-material-slot 0
  run_product kslot1   --viewmodel-render command --viewmodel "$VM_MODEL" --viewmodel-weapon-body off $FX --viewmodel-material-slot 1
  # The same world with a completely different ViewModel flag set (command mode,
  # the three effect switches off, no `--viewmodel` at all). If this frame differs
  # from `world`, every subtraction below is measuring two things.
  run_product kworld   --viewmodel-render command --viewmodel-weapon-body off $FX
  # The body whose two slots resolve to two different textures. The recording's
  # own body has two slots on ONE atlas, so a wrong binding is invisible on it.
  run_product bon      --viewmodel "$VM_MODEL" --viewmodel-sequence "$POSE_SEQUENCE" --viewmodel-weapon-model "$BODY_OVERRIDE"
  run_product bflat    --viewmodel "$VM_MODEL" --viewmodel-sequence "$POSE_SEQUENCE" --viewmodel-weapon-model "$BODY_OVERRIDE" --viewmodel-weapon-flat-color
  run_product bslot0   --viewmodel "$VM_MODEL" --viewmodel-sequence "$POSE_SEQUENCE" --viewmodel-weapon-model "$BODY_OVERRIDE" --viewmodel-weapon-material-slot 0
  run_product bslot1   --viewmodel "$VM_MODEL" --viewmodel-sequence "$POSE_SEQUENCE" --viewmodel-weapon-model "$BODY_OVERRIDE" --viewmodel-weapon-material-slot 1
fi

# ---------------------------------------------------------------------------
# Section 2 -- the product's own lines: two draw requests, not one
# ---------------------------------------------------------------------------
echo
echo "=== 2/7 two independent draw requests, on the product's lines ==="
if [ "$MUTATION" -eq 0 ]; then
  BIND=$RUN_DIR/bind.trace
  APPLY=$RUN_DIR/apply.trace
  assert_eq "bind: one ViewModel draw call" "$(reading "$BIND" 'viewmodel pass order' drawCalls)" "1"
  assert_eq "bind: the drawn model is the entity's" "$(reading "$BIND" 'viewmodel pass order' drawnModel)" "$ARMS_MODEL"
  assert_eq "bind: no pose is applied" "$(reading "$BIND" 'viewmodel pass order' vmAnimApplied)" "0"
  assert_eq "apply: the pose is applied" "$(reading "$APPLY" 'viewmodel pass order' vmAnimApplied)" "1"
  assert_eq "apply: the pass skins" "$(reading "$APPLY" 'viewmodel pass order' skinning)" "1"
  assert_eq "apply: the bone table covers the skeleton" "$(reading "$APPLY" 'viewmodel pass order' boneTableCoverage)" "58/58"
  assert_eq "apply: the sequence is inside the drawn model's table" "$(reading "$APPLY" 'viewmodel pass order' sequenceInRange)" "1"
  assert_eq "apply: the pose samples a frame" "$(reading "$APPLY" 'viewmodel pass order' bindPose)" "0"
  assert_eq "arms: two ranges" "$(reading "$BIND" 'viewmodel pass order' ranges)" "2"
  assert_eq "arms: two distinct material slots bound" "$(reading "$BIND" 'viewmodel pass order' boundSlots)" "2"
  assert_eq "arms: the two ranges' slots, in range order" "$(reading "$BIND" 'gpu: viewmodel ranges' rangeSlots)" "0,1"
  assert_eq "arms: the two ranges' vertex counts" "$(reading "$BIND" 'gpu: viewmodel ranges' rangeVertices)" "600,9396"
  assert_eq "arms: both ranges are textured" "$(reading "$BIND" 'gpu: viewmodel ranges' rangeTextured)" "1,1"

  # The two requests must not be one request counted twice: the body's keys are
  # the body's own VTFs, in range order, and the first key is not the arms'.
  NAT_KEYS=$(reading "$BIND" 'viewmodel weaponBodyMaterial' keys)
  assert_eq "body: two ranges" "$(reading "$BIND" 'viewmodel weaponBodyMaterial' ranges)" "2"
  assert_eq "body: two textured ranges" "$(reading "$BIND" 'viewmodel weaponBodyMaterial' texturedRanges)" "2"
  assert_eq "body: two distinct slots bound" "$(reading "$BIND" 'viewmodel weaponBodyMaterial' boundSlots)" "2"
  assert_eq "body: no model override" "$(reading "$BIND" 'viewmodel weaponBodyMaterial' override)" "0"
  assert_contains "body: its own albedo is bound" "$NAT_KEYS" "c_pro_rifle.vtf"
  case "$NAT_KEYS" in
    *sniper_red.vtf*) fail_line "body: the arms' albedo is in the body's key list ($NAT_KEYS)" ;;
    *) pass "body: the arms' albedo is not in the body's key list" ;;
  esac
  assert_eq "body: the natural body's two slots share one atlas (why the override exists)" \
    "$(printf '%s' "$NAT_KEYS" | tr ',' '\n' | sort -u | wc -l | tr -d ' ')" "1"

  OVER_KEYS=$(reading "$RUN_DIR/bon.trace" 'viewmodel weaponBodyMaterial' keys)
  assert_eq "override body: two distinct albedos" \
    "$(printf '%s' "$OVER_KEYS" | tr ',' '\n' | sort -u | wc -l | tr -d ' ')" "2"
  assert_contains "override body: the rifle's albedo" "$OVER_KEYS" "c_tfc_sniperrifle.vtf"
  assert_contains "override body: the scope's albedo" "$OVER_KEYS" "c_sniperrifle_scope.vtf"
  assert_eq "override body: reported as an override" "$(reading "$RUN_DIR/bon.trace" 'viewmodel weaponBodyMaterial' override)" "1"
  assert_eq "override body: two slots bound" "$(reading "$RUN_DIR/bon.trace" 'viewmodel weaponBodyMaterial' boundSlots)" "2"

  # A forced slot must be visible as a forced slot in the readings, or an
  # overridden run could be read as a natural one.
  assert_eq "forced arms slot 0: reported" "$(reading "$RUN_DIR/pslot0.trace" 'viewmodel materialReadings' slotOverride)" "0"
  assert_eq "forced arms slot 1: reported" "$(reading "$RUN_DIR/pslot1.trace" 'viewmodel materialReadings' slotOverride)" "1"
  assert_eq "forced arms slot 0: one slot bound" "$(reading "$RUN_DIR/pslot0.trace" 'viewmodel pass order' boundSlots)" "1"
  assert_eq "forced body slot 0: reported" "$(reading "$RUN_DIR/bslot0.trace" 'viewmodel weaponBodyMaterial' slotOverride)" "0"
  assert_eq "forced body slot 1: reported" "$(reading "$RUN_DIR/bslot1.trace" 'viewmodel weaponBodyMaterial' slotOverride)" "1"
  assert_eq "forced body slot 0: both ranges take slot 0" "$(reading "$RUN_DIR/bslot0.trace" 'viewmodel weaponBodyMaterial' keys)" \
    "materials/models/weapons/c_items/c_tfc_sniperrifle.vtf,materials/models/weapons/c_items/c_tfc_sniperrifle.vtf"
  assert_eq "forced body slot 1: both ranges take slot 1" "$(reading "$RUN_DIR/bslot1.trace" 'viewmodel weaponBodyMaterial' keys)" \
    "materials/models/weapons/c_items/c_sniperrifle_scope.vtf,materials/models/weapons/c_items/c_sniperrifle_scope.vtf"
else
  echo "  ..   forward readings are asserted by the forward run; this run measures the defects"
fi

# ---------------------------------------------------------------------------
# Section 3 -- the frame: framing, determinism, and the world as a control
# ---------------------------------------------------------------------------
echo
echo "=== 3/7 framing on the captured frame, and the control that carries it ==="
if [ "$MUTATION" -eq 0 ]; then
  # The world frame does not depend on any ViewModel input. If it did, every
  # subtraction below would be measuring two things at once.
  assert_eq "the world pass is byte-identical under a different ViewModel flag set" \
    "$(sha256sum "$RUN_DIR/kworld.bmp" | cut -d' ' -f1)" \
    "$(sha256sum "$RUN_DIR/world.bmp" | cut -d' ' -f1)"
  COVER=$(python bmp-diff-mask.py "$RUN_DIR/bind.bmp" "$RUN_DIR/world.bmp" --mask "$RUN_DIR/bind.bmp" "$RUN_DIR/world.bmp" 2>&1 | tail -1)
  COVER_APPLY=$(python bmp-diff-mask.py "$RUN_DIR/apply.bmp" "$RUN_DIR/world.bmp" --mask "$RUN_DIR/apply.bmp" "$RUN_DIR/world.bmp" 2>&1 | tail -1)
  COVER_K=$(python bmp-diff-mask.py "$RUN_DIR/kbind.bmp" "$RUN_DIR/world.bmp" --mask "$RUN_DIR/kbind.bmp" "$RUN_DIR/world.bmp" 2>&1 | tail -1)
  echo "  ..   coverage: bind [$COVER]"
  echo "  ..   coverage: apply [$COVER_APPLY]"
  echo "  ..   coverage: command [$COVER_K]"

  COVER_PX=$(mask_field "$COVER" changed)
  APPLY_PX=$(mask_field "$COVER_APPLY" changed)
  K_PX=$(mask_field "$COVER_K" changed)
  assert_ge "the composite covers a weapon-sized region" "$COVER_PX" "8000"
  assert_le "the composite does not fill the frame" "$COVER_PX" "80000"
  # 25601 / (1264*681) = 2.97%: a first-person weapon at arm's length.
  awk -v n="$COVER_PX" 'BEGIN { printf "  ..   composite covers %.2f%% of the frame\n", n / (1264 * 681) * 100 }'
  assert_ge "the applied pose keeps the composite on screen" "$APPLY_PX" "8000"
  assert_le "the applied pose does not grow the composite" "$APPLY_PX" "$COVER_PX"

  # Not clipped: every edge has room. A camera inside the model fails this long
  # before it fails anything else (T13's close-up filled all four).
  BBOX=$(mask_pair_field "$COVER" bbox)
  X0=$(printf '%s' "$BBOX" | sed 's/.*x=\([0-9]*\)\.\..*/\1/')
  X1=$(printf '%s' "$BBOX" | sed 's/.*x=[0-9]*\.\.\([0-9]*\).*/\1/')
  Y0=$(printf '%s' "$BBOX" | sed 's/.*y=\([0-9]*\)\.\..*/\1/')
  Y1=$(printf '%s' "$BBOX" | sed 's/.*y=[0-9]*\.\.\([0-9]*\).*/\1/')
  echo "  ..   composite bbox: x=$X0..$X1 y=$Y0..$Y1"
  assert_ge "left margin" "$X0" "100"
  assert_ge "right margin" "$((1264 - 1 - X1))" "100"
  assert_ge "top margin" "$Y0" "100"
  assert_ge "bottom margin" "$((681 - 1 - Y1))" "100"

  # The applied frame is the same composition: the same top, the same left edge,
  # and a right edge that moved by the posed fingers (9 px), not by a reframing.
  ABOX=$(mask_pair_field "$COVER_APPLY" bbox)
  AX0=$(printf '%s' "$ABOX" | sed 's/.*x=\([0-9]*\)\.\..*/\1/')
  AX1=$(printf '%s' "$ABOX" | sed 's/.*x=[0-9]*\.\.\([0-9]*\).*/\1/')
  AY0=$(printf '%s' "$ABOX" | sed 's/.*y=\([0-9]*\)\.\..*/\1/')
  AY1=$(printf '%s' "$ABOX" | sed 's/.*y=[0-9]*\.\.\([0-9]*\).*/\1/')
  assert_le "the pose does not move the left edge" "$(awk -v a="$AX0" -v b="$X0" 'BEGIN{d=a-b; if(d<0)d=-d; print d}')" "12"
  assert_le "the pose does not move the right edge" "$(awk -v a="$AX1" -v b="$X1" 'BEGIN{d=a-b; if(d<0)d=-d; print d}')" "12"
  assert_le "the pose does not move the top edge" "$(awk -v a="$AY0" -v b="$Y0" 'BEGIN{d=a-b; if(d<0)d=-d; print d}')" "12"
  assert_le "the pose does not move the bottom edge" "$(awk -v a="$AY1" -v b="$Y1" 'BEGIN{d=a-b; if(d<0)d=-d; print d}')" "12"
  assert_le "the pose changes the composite's area by less than a tenth" \
    "$(awk -v a="$APPLY_PX" -v b="$COVER_PX" 'BEGIN{d=a-b; if(d<0)d=-d; print d / b}')" "0.10"

  # The framing rule from the product's own placement solve, re-read here so the
  # framing numbers in the evidence are the ones this run produced.
  FRAME_LINE=$(reading_line "$RUN_DIR/bind.trace" 'gpu: viewmodel frame eye=')
  DISTANCE=$(printf '%s\n' "$FRAME_LINE" | grep -o 'distance=[0-9.]*' | cut -d= -f2)
  RADIUS=$(printf '%s\n' "$FRAME_LINE" | grep -o 'radius=[0-9.]*' | cut -d= -f2)
  assert_le "the eye is outside the model (distance / radius)" \
    "$(awk -v d="$DISTANCE" -v r="$RADIUS" 'BEGIN { if (r <= 0) print 99; else print d / r }')" "2.30"
  assert_ge "the eye is not far from the model (distance / radius)" \
    "$(awk -v d="$DISTANCE" -v r="$RADIUS" 'BEGIN { if (r <= 0) print 0; else print d / r }')" "2.10"
  assert_eq "the camera and the framing are the same text on the bind and applied runs" \
    "$(reading_line "$RUN_DIR/apply.trace" 'gpu: viewmodel frame eye=')" "$FRAME_LINE"

  assert_eq "the applied frame repeats byte for byte" \
    "$(sha256sum "$RUN_DIR/repeat.bmp" | cut -d' ' -f1)" \
    "$(sha256sum "$RUN_DIR/apply.bmp" | cut -d' ' -f1)"

  # Control regions. The pose is allowed to move the hand; it is not allowed to
  # move the world above the composite or the stock below the hand.
  WORLDSTRIP=$(python bmp-pixel-diff.py "$RUN_DIR/pbind.bmp" "$RUN_DIR/papply.bmp" --max-diff-percent 0 --region 0,0,1264,220 2>&1 | grep 'region=0,0,1264,220')
  echo "  ..   world strip above the composite: [$WORLDSTRIP]"
  assert_eq "the pose does not touch the world above the composite" "$(mask_field "$WORLDSTRIP" differing)" "0"
  BELOW=$(python bmp-pixel-diff.py "$RUN_DIR/pbind.bmp" "$RUN_DIR/papply.bmp" --max-diff-percent 0 --region 0,420,1264,261 2>&1 | grep 'region=0,420,1264,261')
  echo "  ..   everything below y=420: [$BELOW]"
  assert_eq "the pose does not touch anything below the hand" "$(mask_field "$BELOW" differing)" "0"
else
  echo "  ..   framing assertions belong to the forward run; this run measures the defects"
fi

# ---------------------------------------------------------------------------
# Section 4 -- the arms' two ranges, attributed in pixels
# ---------------------------------------------------------------------------
echo
echo "=== 4/7 the arms: each range owns its own pixels and its own albedo ==="
if [ "$MUTATION" -eq 0 ]; then
  ARMS_COVER_LINE=$(python bmp-diff-mask.py "$RUN_DIR/pbodyoff.bmp" "$RUN_DIR/world.bmp" --mask "$RUN_DIR/pbodyoff.bmp" "$RUN_DIR/world.bmp" 2>&1 | tail -1)
  echo "  ..   arms-only coverage: [$ARMS_COVER_LINE]"
  HAND_LINE=$(python bmp-diff-mask.py "$RUN_DIR/pbind.bmp" "$RUN_DIR/pslot0.bmp" --mask "$RUN_DIR/pbodyoff.bmp" "$RUN_DIR/world.bmp" 2>&1 | tail -1)
  SLEEVE_LINE=$(python bmp-diff-mask.py "$RUN_DIR/pbind.bmp" "$RUN_DIR/pslot1.bmp" --mask "$RUN_DIR/pbodyoff.bmp" "$RUN_DIR/world.bmp" 2>&1 | tail -1)
  UNION_LINE=$(python bmp-diff-mask.py "$RUN_DIR/pslot0.bmp" "$RUN_DIR/pslot1.bmp" --mask "$RUN_DIR/pbodyoff.bmp" "$RUN_DIR/world.bmp" 2>&1 | tail -1)
  echo "  ..   hand range (slot 1 forced to slot 0): [$HAND_LINE]"
  echo "  ..   sleeve range (slot 0 forced to slot 1): [$SLEEVE_LINE]"
  echo "  ..   both forced frames against each other: [$UNION_LINE]"

  HAND_PX=$(mask_field "$HAND_LINE" changed)
  SLEEVE_PX=$(mask_field "$SLEEVE_LINE" changed)
  UNION_PX=$(mask_field "$UNION_LINE" changed)
  assert_ge "the hand range owns pixels" "$HAND_PX" "8000"
  assert_ge "the sleeve range owns pixels" "$SLEEVE_PX" "100"

  # Every pixel either subtraction found is on the arms, not on the body or the
  # world. `outside` is measured against the arms-only coverage.
  assert_eq "the hand's pixels are on the arms" "$(mask_field "$HAND_LINE" outside)" "0"
  assert_eq "the sleeve's pixels are on the arms" "$(mask_field "$SLEEVE_LINE" outside)" "0"

  # Disjoint: a pixel belongs to one range, so forcing the other range's albedo
  # leaves it alone. `outside == changed` is the arithmetic form of "intersection
  # is empty" for this tool, which reports one pair at a time.
  OVERLAP_A=$(python bmp-diff-mask.py "$RUN_DIR/pbind.bmp" "$RUN_DIR/pslot0.bmp" --mask "$RUN_DIR/pbind.bmp" "$RUN_DIR/pslot1.bmp" 2>&1 | tail -1)
  OVERLAP_B=$(python bmp-diff-mask.py "$RUN_DIR/pbind.bmp" "$RUN_DIR/pslot1.bmp" --mask "$RUN_DIR/pbind.bmp" "$RUN_DIR/pslot0.bmp" 2>&1 | tail -1)
  assert_eq "no pixel is owned by both ranges (hand vs sleeve)" "$(mask_field "$OVERLAP_A" inside)" "0"
  assert_eq "no pixel is owned by both ranges (sleeve vs hand)" "$(mask_field "$OVERLAP_B" inside)" "0"

  # Complete: the two sets add up to the difference between the two forced
  # frames, exactly. This is the claim a count of bound slots cannot make.
  assert_eq "the two ranges' pixel sets add up to the forced pair's difference" \
    "$(awk -v a="$HAND_PX" -v b="$SLEEVE_PX" 'BEGIN { print a + b }')" "$UNION_PX"
  assert_gt "forcing the two slots really produces two different pictures" "$UNION_PX" "1000"
else
  echo "  ..   partition assertions belong to the forward run; this run measures the defects"
fi

# ---------------------------------------------------------------------------
# Section 5 -- the complete model: gun and hands in one skinned mesh
# ---------------------------------------------------------------------------
echo
echo "=== 5/7 a model that is both: scattergun and scout hands, two albedos ==="
if [ "$MUTATION" -eq 0 ]; then
  K_COVER_LINE=$(python bmp-diff-mask.py "$RUN_DIR/kbind.bmp" "$RUN_DIR/world.bmp" --mask "$RUN_DIR/kbind.bmp" "$RUN_DIR/world.bmp" 2>&1 | tail -1)
  K_WEAPON_LINE=$(python bmp-diff-mask.py "$RUN_DIR/kbind.bmp" "$RUN_DIR/kslot0.bmp" --mask "$RUN_DIR/kbind.bmp" "$RUN_DIR/world.bmp" 2>&1 | tail -1)
  K_HANDS_LINE=$(python bmp-diff-mask.py "$RUN_DIR/kbind.bmp" "$RUN_DIR/kslot1.bmp" --mask "$RUN_DIR/kbind.bmp" "$RUN_DIR/world.bmp" 2>&1 | tail -1)
  K_UNION_LINE=$(python bmp-diff-mask.py "$RUN_DIR/kslot0.bmp" "$RUN_DIR/kslot1.bmp" --mask "$RUN_DIR/kbind.bmp" "$RUN_DIR/world.bmp" 2>&1 | tail -1)
  echo "  ..   model coverage: [$K_COVER_LINE]"
  echo "  ..   hands range (slot 1 forced to slot 0): [$K_WEAPON_LINE]"
  echo "  ..   weapon range (slot 0 forced to slot 1): [$K_HANDS_LINE]"
  echo "  ..   both forced frames against each other: [$K_UNION_LINE]"

  K_HANDS_PX=$(mask_field "$K_WEAPON_LINE" changed)
  K_WEAPON_PX=$(mask_field "$K_HANDS_LINE" changed)
  K_UNION_PX=$(mask_field "$K_UNION_LINE" changed)
  assert_ge "the hands range owns pixels" "$K_HANDS_PX" "2000"
  assert_ge "the weapon range owns pixels" "$K_WEAPON_PX" "2000"
  K_OVERLAP=$(python bmp-diff-mask.py "$RUN_DIR/kbind.bmp" "$RUN_DIR/kslot0.bmp" --mask "$RUN_DIR/kbind.bmp" "$RUN_DIR/kslot1.bmp" 2>&1 | tail -1)
  assert_eq "the two ranges of one mesh do not share a pixel" "$(mask_field "$K_OVERLAP" inside)" "0"
  assert_eq "they add up to the forced pair's difference" \
    "$(awk -v a="$K_HANDS_PX" -v b="$K_WEAPON_PX" 'BEGIN { print a + b }')" "$K_UNION_PX"
  # The model's own descriptor table says which range is which; the picture has
  # to agree that BOTH exist, not just that a total moved.
  assert_eq "the model's range table is two ranges" "$(reading "$RUN_DIR/kbind.trace" 'viewmodel pass order' ranges)" "2"
  assert_eq "the model's two ranges use two slots" "$(reading "$RUN_DIR/kbind.trace" 'viewmodel pass order' boundSlots)" "2"
  assert_eq "the model's ranges in order: the weapon, then the hands" \
    "$(reading "$RUN_DIR/kbind.trace" 'gpu: viewmodel ranges' rangeVertices)" "9723,13446"
else
  echo "  ..   partition assertions belong to the forward run; this run measures the defects"
fi

# ---------------------------------------------------------------------------
# Section 6 -- the weapon body's two slots, attributed in pixels
# ---------------------------------------------------------------------------
echo
echo "=== 6/7 the weapon body: two ranges, two albedos, two pixel sets ==="
# The body's coverage is the union of two pairs, for the reason T14 recorded: a
# pixel where the textured body happens to match the frame behind it is still a
# body pixel the flat control can change, and one pair would miss it.
BODY_MASK_ARGS=(--mask "$RUN_DIR/bon.bmp" "$RUN_DIR/bodyoff.bmp")
if [ "$MUTATION" -eq 0 ]; then
  BODY_MASK_ARGS+=(--mask "$RUN_DIR/bflat.bmp" "$RUN_DIR/bodyoff.bmp")
fi
BODY_SCOPE_LINE=$(python bmp-diff-mask.py "$RUN_DIR/bon.bmp" "$RUN_DIR/bslot0.bmp" "${BODY_MASK_ARGS[@]}" 2>&1 | tail -1)
BODY_RIFLE_LINE=$(python bmp-diff-mask.py "$RUN_DIR/bon.bmp" "$RUN_DIR/bslot1.bmp" "${BODY_MASK_ARGS[@]}" 2>&1 | tail -1)
BODY_UNION_LINE=$(python bmp-diff-mask.py "$RUN_DIR/bslot0.bmp" "$RUN_DIR/bslot1.bmp" "${BODY_MASK_ARGS[@]}" 2>&1 | tail -1)
echo "  ..   scope range (slot 1 forced to slot 0): [$BODY_SCOPE_LINE]"
echo "  ..   rifle range (slot 0 forced to slot 1): [$BODY_RIFLE_LINE]"
echo "  ..   both forced frames against each other: [$BODY_UNION_LINE]"

BODY_SCOPE_PX=$(mask_field "$BODY_SCOPE_LINE" changed)
BODY_RIFLE_PX=$(mask_field "$BODY_RIFLE_LINE" changed)
BODY_UNION_PX=$(mask_field "$BODY_UNION_LINE" changed)
if [ "$MUTATION" -eq 0 ]; then
  assert_ge "the scope range owns pixels" "$BODY_SCOPE_PX" "200"
  assert_ge "the rifle range owns pixels" "$BODY_RIFLE_PX" "2000"
  assert_eq "the body's two ranges do not share a pixel" \
    "$(python bmp-diff-mask.py "$RUN_DIR/bon.bmp" "$RUN_DIR/bslot0.bmp" --mask "$RUN_DIR/bon.bmp" "$RUN_DIR/bslot1.bmp" 2>&1 | tail -1 | grep -o 'inside=[0-9]*' | cut -d= -f2)" "0"
  assert_eq "the body's two pixel sets add up to the forced pair's difference" \
    "$(awk -v a="$BODY_SCOPE_PX" -v b="$BODY_RIFLE_PX" 'BEGIN { print a + b }')" "$BODY_UNION_PX"
  # Which range owns which region: the scope is the small range (444 vertices of
  # 21261) and sits on the upper half of the body's silhouette; a swap of the two
  # slots would move both numbers without moving their sum.
  assert_le "the scope's pixel set is the small one" "$BODY_SCOPE_PX" "3000"
  assert_ge "the rifle's pixel set is the large one" "$BODY_RIFLE_PX" "4000"
  assert_eq "the body override changes nothing above the composite" \
    "$(python bmp-pixel-diff.py "$RUN_DIR/bon.bmp" "$RUN_DIR/bslot0.bmp" --max-diff-percent 0 --region 0,0,1264,220 2>&1 | grep 'region=0,0,1264,220' | grep -o 'differing=[0-9]*' | cut -d= -f2)" "0"
  # T14's independence claim, re-measured from this side: the body's material
  # state is its own. A body-only override must not repaint the arms.
  assert_eq "a body-only slot override changes nothing outside the body" "$(mask_field "$BODY_SCOPE_LINE" outside)" "0"

  # The pose must not move the body: the body's coverage is the reference, and
  # the hand is the only thing allowed to change.
  BODY_POSE=$(python bmp-diff-mask.py "$RUN_DIR/pbind.bmp" "$RUN_DIR/papply.bmp" "${BODY_MASK_ARGS[@]}" 2>&1 | tail -1)
  echo "  ..   pose change against the body's coverage: [$BODY_POSE]"
  # The pose changes 9740 pixels; 259 of them are on the body's coverage (the
  # hand drawn in front of the rifle). A pose that moved the body would put
  # thousands there instead, and an unchanged body is what the composite is
  # built from -- see the report's NOT VERIFIED note about a playing sequence.
  assert_le "the pose change barely lands on the body" "$(mask_field "$BODY_POSE" inside)" "1000"
  assert_ge "the pose change is not a body change (most of it is off the body)" \
    "$(mask_field "$BODY_POSE" outside)" "5000"
else
  echo "  ..   partition assertions belong to the forward run; this run measures the defects"
fi

# ---------------------------------------------------------------------------
# Section 7 -- the pose: the hand is still on the weapon
# ---------------------------------------------------------------------------
echo
echo "=== 7/7 the posed hand, in the picture ==="
if [ "$MUTATION" -eq 0 ]; then
  BIND_HAND=$(python bmp-mask-contact.py --mask "$RUN_DIR/pbind.bmp" "$RUN_DIR/pslot0.bmp" \
    --body "$RUN_DIR/bind.bmp" "$RUN_DIR/bodyoff.bmp" --radius 2 --max-distance 8 2>&1 | tail -1)
  APPLY_HAND=$(python bmp-mask-contact.py --mask "$RUN_DIR/papply.bmp" "$RUN_DIR/papplyslot0.bmp" \
    --body "$RUN_DIR/bind.bmp" "$RUN_DIR/bodyoff.bmp" --radius 2 --max-distance 8 2>&1 | tail -1)
  echo "  ..   bind hand: [$BIND_HAND]"
  echo "  ..   posed hand: [$APPLY_HAND]"

  BIND_HAND_PX=$(mask_field "$BIND_HAND" maskPixels)
  APPLY_HAND_PX=$(mask_field "$APPLY_HAND" maskPixels)
  assert_ge "the bind hand's mask is large enough to judge" "$BIND_HAND_PX" "8000"
  assert_ge "the posed hand's mask is large enough to judge" "$APPLY_HAND_PX" "8000"

  # The pose moves the hand WITHOUT taking it off the weapon: contact is the same
  # distance (1 = touching along an edge), and the share of hand pixels within two
  # pixels of the body does not collapse.
  assert_eq "the bind hand touches the weapon" "$(mask_field "$BIND_HAND" contactDistance)" "1"
  assert_eq "the posed hand still touches the weapon" "$(mask_field "$APPLY_HAND" contactDistance)" "1"
  assert_ge "the posed hand keeps at least half the bind hand's contact" \
    "$(awk -v a="$(mask_field "$APPLY_HAND" nearFraction)" -v b="$(mask_field "$BIND_HAND" nearFraction)" 'BEGIN { if (b <= 0) print 0; else print a / b }')" "0.5"

  # ...and it stays where it was. 99.99% of the posed hand's pixels are inside
  # the bind hand's mask, and the centroid moves 6.35 px of 1264.
  HAND_INSIDE=$(python bmp-diff-mask.py "$RUN_DIR/papply.bmp" "$RUN_DIR/papplyslot0.bmp" \
    --mask "$RUN_DIR/pbind.bmp" "$RUN_DIR/pslot0.bmp" 2>&1 | tail -1)
  echo "  ..   posed hand against the bind hand's mask: [$HAND_INSIDE]"
  assert_le "the posed hand does not leave the bind hand's region" "$(mask_field "$HAND_INSIDE" outside)" "64"
  SHIFT=$(awk -v ax="$(mask_tuple_field "$APPLY_HAND" centroid | cut -d, -f1)" \
               -v ay="$(mask_tuple_field "$APPLY_HAND" centroid | cut -d, -f2)" \
               -v bx="$(mask_tuple_field "$BIND_HAND" centroid | cut -d, -f1)" \
               -v by="$(mask_tuple_field "$BIND_HAND" centroid | cut -d, -f2)" \
          'BEGIN { dx = ax - bx; dy = ay - by; print (dx * dx + dy * dy) ^ 0.5 }')
  assert_le "the hand's centroid moves less than 10 px" "$SHIFT" "10"

  # The pose change itself: large enough to be a pose, contained in the composite,
  # and not a camera move (section 3's edges and control strips are the other half).
  POSE=$(python bmp-diff-mask.py "$RUN_DIR/pbind.bmp" "$RUN_DIR/papply.bmp" \
    --mask "$RUN_DIR/bind.bmp" "$RUN_DIR/world.bmp" 2>&1 | tail -1)
  echo "  ..   pose change against the composite's coverage: [$POSE]"
  assert_gt "the pose changed the picture" "$(mask_field "$POSE" changed)" "1000"
  assert_le "the pose change is inside the ViewModel's coverage" "$(mask_field "$POSE" outside)" "64"
  # T18's shape of defect, expressed as a bound: a transposed bone read moves
  # every vertex and puts ~190k pixels outside this mask.
  assert_le "the pose change is not a re-skin of the frame" "$(mask_field "$POSE" changed)" "20000"
else
  echo "  ..   pose assertions belong to the forward run; this run measures the defects"
fi

# ---------------------------------------------------------------------------
# The mutations
# ---------------------------------------------------------------------------
MUTATION_FILE=""
MUTATION_BAK=""
MUTATION_SHA=""
MUTATION_ACTIVE=0

restore_mutation() {
  [ "$MUTATION_ACTIVE" -eq 1 ] || return 0
  cp "$MUTATION_BAK" "$MUTATION_FILE"
  if ! cmp -s "$MUTATION_BAK" "$MUTATION_FILE"; then
    echo "  FATAL the mutation backup could not be restored to $MUTATION_FILE"
    fail=1
  fi
  local now
  now=$(sha256sum "$MUTATION_FILE" | cut -d' ' -f1)
  if [ "$now" != "$MUTATION_SHA" ]; then
    echo "  FATAL $MUTATION_FILE is $now, was $MUTATION_SHA before the mutation"
    fail=1
  else
    echo "  ..   $MUTATION_FILE restored byte-for-byte ($now)"
  fi
  rm -f "$MUTATION_BAK" 2>/dev/null
  MUTATION_ACTIVE=0
}

cleanup() {
  restore_mutation
  release_lock
}
trap cleanup EXIT

apply_mutation() { # apply_mutation <file> <label>
  MUTATION_FILE="$1"
  MUTATION_BAK="$RUN_DIR/$(basename "$1").backup"
  cp "$1" "$MUTATION_BAK"
  MUTATION_SHA=$(sha256sum "$1" | cut -d' ' -f1)
  MUTATION_ACTIVE=1
  echo "  ..   mutation applied to $1"
}

# Text substitution through patch-source.py, which matches the file's own
# newline: native_renderer.cpp is LF and main.cpp is CRLF, and an LF needle
# against the CRLF file applies nothing and reads like a stale pattern (T18 §8.2).
patch_source() { python patch-source.py "$1"; }

rebuild() {
  if ! bash build-target.sh tf2_demo_native > "$RUN_DIR/rebuild.log" 2>&1; then
    echo "  FATAL the mutated tree did not build; see $RUN_DIR/rebuild.log"
    fail=1
    return 1
  fi
  return 0
}

mutant_capture() { # mutant_capture <name> [extra args...]
  run_product "$1" "${@:2}" > /dev/null 2>&1
  echo "  ..   $1 captured ($(stat -c%s "$RUN_DIR/$1.bmp" 2>/dev/null || echo 0) bytes)"
}

if [ "$MUTATION" -eq 1 ]; then
  echo
  echo "=== M the defects, one at a time ==="
  BASELINE_BSLOT0_HASH=$(sha256sum "$RUN_DIR/bslot0.bmp" | cut -d' ' -f1)
  BASELINE_SCOPE_PX=$(mask_field "$BODY_SCOPE_LINE" changed)
  BASELINE_RIFLE_PX=$(mask_field "$BODY_RIFLE_LINE" changed)
  BASELINE_KEYS=$(reading "$RUN_DIR/bon.trace" 'viewmodel weaponBodyMaterial' keys)
  assert_gt "baseline: the forced body frames differ" "$BASELINE_SCOPE_PX" "200"
  echo "  ..   baseline: scope=$BASELINE_SCOPE_PX rifle=$BASELINE_RIFLE_PX keys=$BASELINE_KEYS"

  # ---- m1: the new override is parsed, reported, and not used ----------------
  echo
  echo "-- m1: the body's slot override is reported but the range builder is not told"
  MUTATION_TOTAL=$((MUTATION_TOTAL + 1))
  apply_mutation native/src/main.cpp "override not applied"
  if printf '%s\n' 'viewModelWeaponBody.ranges = viewModelDrawRangesFor(request, viewModelWeaponFlatColor,
      viewModelWeaponMaterialSlotOverride);' '%%' \
     'viewModelWeaponBody.ranges = viewModelDrawRangesFor(request, viewModelWeaponFlatColor,
      -1);' | patch_source native/src/main.cpp; then
    if rebuild; then
      mutant_capture m1-bslot0 --viewmodel "$VM_MODEL" --viewmodel-sequence "$POSE_SEQUENCE" --viewmodel-weapon-model "$BODY_OVERRIDE" --viewmodel-weapon-material-slot 0
      M1_KEYS=$(reading "$RUN_DIR/m1-bslot0.trace" 'viewmodel weaponBodyMaterial' keys)
      M1_DIFF=$(python bmp-diff-mask.py "$RUN_DIR/bon.bmp" "$RUN_DIR/m1-bslot0.bmp" --mask "$RUN_DIR/bon.bmp" "$RUN_DIR/m1-bslot0.bmp" 2>&1 | tail -1)
      echo "  ..   m1 keys=$M1_KEYS diff=[$M1_DIFF]"
      M1_OK=1
      # The defect is that forcing slot 0 changes nothing: the picture is the
      # natural one and the keys are the natural pair.
      if [ "$(mask_field "$M1_DIFF" changed)" != "0" ]; then M1_OK=0; fi
      if [ "$M1_KEYS" != "$BASELINE_KEYS" ]; then M1_OK=0; fi
      # The control: it still reports the override it ignored, so this is a
      # wiring defect and not "the flag was never parsed".
      if [ "$(reading "$RUN_DIR/m1-bslot0.trace" 'viewmodel weaponBodyMaterial' slotOverride)" != "0" ]; then M1_OK=0; fi
      if [ "$M1_OK" -eq 1 ]; then
        pass "m1 caught: the forced frame equals the natural frame ($(mask_field "$M1_DIFF" changed) px, baseline $BASELINE_SCOPE_PX)"
        MUTATION_CATCHES=$((MUTATION_CATCHES + 1))
      else
        fail_line "m1 was not caught (changed=$(mask_field "$M1_DIFF" changed), keys=$M1_KEYS, slotOverride=$(reading "$RUN_DIR/m1-bslot0.trace" 'viewmodel weaponBodyMaterial' slotOverride))"
      fi
    fi
  fi
  restore_mutation
  rebuild || true

  # ---- m2: the natural per-range slot order is shifted by one --------------
  echo
  echo "-- m2: the natural body ranges read the slot next to the one they name"
  MUTATION_TOTAL=$((MUTATION_TOTAL + 1))
  apply_mutation native/src/main.cpp "slot off by one"
  if printf '%s\n' '      const std::int32_t slot = slotOverride >= 0
        ? slotOverride : source.materialSlot;' '%%' \
     '      const std::int32_t slot = slotOverride >= 0
        ? slotOverride : (source.materialSlot + 1) % 2;' | patch_source native/src/main.cpp; then
    if rebuild; then
      mutant_capture m2-bon --viewmodel "$VM_MODEL" --viewmodel-sequence "$POSE_SEQUENCE" --viewmodel-weapon-model "$BODY_OVERRIDE"
      M2_KEYS=$(reading "$RUN_DIR/m2-bon.trace" 'viewmodel weaponBodyMaterial' keys)
      echo "  ..   m2 keys=$M2_KEYS"
      M2_OK=1
      # The natural keys are now in the other order: the rifle range binds the
      # scope's albedo and the scope range binds the rifle's.
      if [ "$M2_KEYS" = "$BASELINE_KEYS" ]; then M2_OK=0; fi
      case "$M2_KEYS" in
        *c_sniperrifle_scope.vtf,c_tfc_sniperrifle.vtf*) ;;
        *) M2_OK=0 ;;
      esac
      if [ "$M2_OK" -eq 1 ]; then
        pass "m2 caught: the natural body binds its two albedos in the wrong order ($M2_KEYS)"
        MUTATION_CATCHES=$((MUTATION_CATCHES + 1))
      else
        fail_line "m2 was not caught (keys=$M2_KEYS, baseline=$BASELINE_KEYS)"
      fi
    fi
  fi
  restore_mutation
  rebuild || true

  # ---- m3: the body's override leaks into the arms -------------------------
  echo
  echo "-- m3: the body's slot override is read by the arms' range builder too"
  MUTATION_TOTAL=$((MUTATION_TOTAL + 1))
  apply_mutation native/src/main.cpp "material state leak"
  if printf '%s\n' '    const auto ranges = viewModelDrawRangesFor(request, viewModelFlatColor,
      viewModelMaterialSlotOverride);' '%%' \
     '    const auto ranges = viewModelDrawRangesFor(request, viewModelFlatColor,
      viewModelMaterialSlotOverride >= 0 ? viewModelMaterialSlotOverride
                                         : viewModelWeaponMaterialSlotOverride);' | patch_source native/src/main.cpp; then
    if rebuild; then
      mutant_capture m3-bslot0 --viewmodel "$VM_MODEL" --viewmodel-sequence "$POSE_SEQUENCE" --viewmodel-weapon-model "$BODY_OVERRIDE" --viewmodel-weapon-material-slot 0
      M3_LEAK=$(python bmp-diff-mask.py "$RUN_DIR/bon.bmp" "$RUN_DIR/m3-bslot0.bmp" --mask "$RUN_DIR/bon.bmp" "$RUN_DIR/bodyoff.bmp" 2>&1 | tail -1)
      M3_ARMS=$(reading "$RUN_DIR/m3-bslot0.trace" 'viewmodel materialReadings' slotOverride)
      echo "  ..   m3 body-offset reading=[$M3_LEAK] arms slotOverride=$M3_ARMS"
      M3_OK=1
      # The defect: a body-only override repaints the hands, so the difference
      # leaves the body's coverage.
      if [ "$(mask_field "$M3_LEAK" outside)" = "0" ]; then M3_OK=0; fi
      if [ "$M3_ARMS" != "0" ]; then M3_OK=0; fi
      if [ "$M3_OK" -eq 1 ]; then
        pass "m3 caught: the body override repainted the arms ($(mask_field "$M3_LEAK" outside) px outside the body)"
        MUTATION_CATCHES=$((MUTATION_CATCHES + 1))
      else
        fail_line "m3 was not caught (outside=$(mask_field "$M3_LEAK" outside), arms slotOverride=$M3_ARMS)"
      fi
    fi
  fi
  restore_mutation
  rebuild || true

  # ---- the restoration, judged on the frame ---------------------------------
  echo
  echo "-- restoration: the restored tree draws the baseline frame again"
  mutant_capture restored-bon --viewmodel "$VM_MODEL" --viewmodel-sequence "$POSE_SEQUENCE" --viewmodel-weapon-model "$BODY_OVERRIDE"
  assert_eq "the restored source draws the baseline body frame byte for byte" \
    "$(sha256sum "$RUN_DIR/restored-bon.bmp" | cut -d' ' -f1)" \
    "$(sha256sum "$RUN_DIR/bon.bmp" | cut -d' ' -f1)"
  assert_eq "the source tree is clean after the mutations" "$(git status --porcelain -- native/ | wc -l | tr -d ' ')" "0"
fi

# ---------------------------------------------------------------------------
# Evidence
# ---------------------------------------------------------------------------
echo
if [ "$fail" -eq 0 ] && [ "$MUTATION" -eq 0 ]; then
  {
    echo "# T22 viewmodel presentation gate, run $RUN_TAG"
    echo "executable_sha256=$FORWARD_EXE_HASH"
    echo "recording=autorecord_2026-07-02_13-26-46.dem"
    echo "tick=$TICK sequence=$POSE_SEQUENCE viewmodel=$VM_MODEL"
    echo "drawn_arms=$ARMS_MODEL drawn_body_override=$BODY_OVERRIDE"
    echo "host_overlap=$HOST_OVERLAP (1 = another worktree had a product process live; captures are paused)"
    echo
    echo "## frames"
    for frame in bind apply repeat bodyoff bodyflat world pbind papply pslot0 pslot1 \
                 papplyslot0 pbodyoff kbind kslot0 kslot1 kworld bon bflat bslot0 bslot1; do
      printf 'frame.%s.sha256=%s frame.%s.bytes=%s\n' "$frame" \
        "$(sha256sum "$RUN_DIR/$frame.bmp" | cut -d' ' -f1)" "$frame" \
        "$(stat -c%s "$RUN_DIR/$frame.bmp" 2>/dev/null || echo 0)"
    done
    echo
    echo "## coverage"
    echo "$COVER"
    echo "$COVER_APPLY"
    echo "$ARMS_COVER_LINE"
    echo "$K_COVER_LINE"
    echo
    echo "## ranges, attributed in pixels (effects off for the partition frames)"
    echo "arms hand:   $HAND_LINE"
    echo "arms sleeve: $SLEEVE_LINE"
    echo "arms union:  $UNION_LINE"
    echo "full hands:  $K_WEAPON_LINE"
    echo "full weapon: $K_HANDS_LINE"
    echo "full union:  $K_UNION_LINE"
    echo
    echo "## weapon body"
    echo "body scope:  $BODY_SCOPE_LINE"
    echo "body rifle:  $BODY_RIFLE_LINE"
    echo "body union:  $BODY_UNION_LINE"
    echo "pose vs body: $BODY_POSE"
    echo
    echo "## pose"
    echo "bind hand:   $BIND_HAND"
    echo "posed hand:  $APPLY_HAND"
    echo "hand containment: $HAND_INSIDE"
    echo "pose change: $POSE"
    echo
    echo "## product lines"
    for trace in bind apply bon bslot0 bslot1 pslot0 pslot1 kbind; do
      reading_line "$RUN_DIR/$trace.trace" 'viewmodel pass order'
      reading_line "$RUN_DIR/$trace.trace" 'viewmodel materialReadings'
      reading_line "$RUN_DIR/$trace.trace" 'viewmodel weaponBodyMaterial'
      reading_line "$RUN_DIR/$trace.trace" 'gpu: viewmodel frame eye='
      reading_line "$RUN_DIR/$trace.trace" 'gpu: viewmodel pose extent'
    done
    echo
    echo "## caveats"
    echo "paused_capture=1 (a playing capture lands on a scheduler-dependent tick)"
    echo "world_geometry=shattered (F1/T17): the composition is judged, not the map"
    echo "body_static=1 (the weapon body is not transformed by the pose; see T22 report)"
    echo "partition_effects_off=1 (forcing a slot moves the pass's global effect constants too)"
  } > "$EVIDENCE/summary.txt"

  cp "$RUN_DIR/bind.bmp" "$EVIDENCE/bind-pose.bmp"
  cp "$RUN_DIR/apply.bmp" "$EVIDENCE/applied-pose.bmp"
  cp "$RUN_DIR/world.bmp" "$EVIDENCE/world.bmp"
  cp "$RUN_DIR/pslot0.bmp" "$EVIDENCE/arms-slot0.bmp"
  cp "$RUN_DIR/pslot1.bmp" "$EVIDENCE/arms-slot1.bmp"
  cp "$RUN_DIR/kslot0.bmp" "$EVIDENCE/fullmodel-slot0.bmp"
  cp "$RUN_DIR/kslot1.bmp" "$EVIDENCE/fullmodel-slot1.bmp"
  cp "$RUN_DIR/bon.bmp" "$EVIDENCE/body-natural.bmp"
  cp "$RUN_DIR/bslot0.bmp" "$EVIDENCE/body-slot0.bmp"
  cp "$RUN_DIR/bslot1.bmp" "$EVIDENCE/body-slot1.bmp"

  python bmp-to-png.py "$RUN_DIR/bind.bmp" "$EVIDENCE/inspect-bind.png" > /dev/null 2>&1
  python bmp-to-png.py "$RUN_DIR/apply.bmp" "$EVIDENCE/inspect-applied.png" > /dev/null 2>&1
  python bmp-crop-grid.py "$EVIDENCE/inspect-bind-applied-4x.png" \
    --region 560,280,300,150 --scale 4 --label "bind | applied" \
    "$RUN_DIR/bind.bmp" "$RUN_DIR/apply.bmp" > /dev/null 2>&1
  python bmp-crop-grid.py "$EVIDENCE/inspect-arms-slots-4x.png" \
    --region 560,280,300,150 --scale 4 --label "natural | slot 0 forced | slot 1 forced" \
    "$RUN_DIR/pbind.bmp" "$RUN_DIR/pslot0.bmp" "$RUN_DIR/pslot1.bmp" > /dev/null 2>&1
  python bmp-crop-grid.py "$EVIDENCE/inspect-fullmodel-slots-4x.png" \
    --region 620,230,120,330 --scale 4 --label "natural | slot 0 | slot 1" \
    "$RUN_DIR/kbind.bmp" "$RUN_DIR/kslot0.bmp" "$RUN_DIR/kslot1.bmp" > /dev/null 2>&1
  python bmp-crop-grid.py "$EVIDENCE/inspect-body-slots-4x.png" \
    --region 640,220,100,370 --scale 4 --label "natural | slot 0 | slot 1" \
    "$RUN_DIR/bon.bmp" "$RUN_DIR/bslot0.bmp" "$RUN_DIR/bslot1.bmp" > /dev/null 2>&1
  echo "evidence written to $EVIDENCE"
fi

echo
if [ "$fail" -eq 0 ]; then
  if [ "$MUTATION" -eq 1 ]; then
    echo "MUTATION-CAUGHT=PASS ($MUTATION_CATCHES of $MUTATION_TOTAL defects)"
  fi
  echo "VIEWMODEL-PRESENTATION=PASS ($OK_COUNT assertions, $FAIL_COUNT failures)"
  exit 0
else
  echo "VIEWMODEL-PRESENTATION=FAIL ($FAIL_COUNT failed of $((OK_COUNT + FAIL_COUNT)))"
  for line in "${FAIL_LINES[@]}"; do echo "  - $line"; done
  exit 1
fi
