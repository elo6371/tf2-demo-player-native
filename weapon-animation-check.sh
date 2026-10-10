#!/usr/bin/env bash
# weapon-animation-check.sh -- the *product* must skin entity models on the GPU
# from real demo animation input, and must say why when it cannot.
#
# Why this exists
# ---------------
# Until this step the animation chain was proven only by a probe
# (weapon_animation_probe): a separate binary that reads a demo, samples the
# decoder and composes skin matrices. A probe passing says the maths works; it
# says nothing about whether the shipped program ever does it. The gap was
# concrete and named: `animation_decoder.cpp` was not compiled into
# `tf2_demo_native` at all, and `main.cpp` uploaded the inverse bind pose with
# skinning switched off. This step pins the product path itself.
#
# What is asserted, and why each one can fail
# -------------------------------------------
#   1. gpuSkin=<accepted>/<uploaded> with accepted >= 1 and uploaded >= 1.
#      The renderer accepts an instance only when its bone range fits inside the
#      uploaded buffer, so accepted < uploaded is a silent wrong-pose bug.
#   2. uploaded == the caller's `matrices` count, i.e. nothing was dropped.
#   3. boneMismatch=0: every skinned instance produced exactly as many matrices
#      as it declared valid bind bones.
#   4. advances and poseChanges are non-decreasing across three ticks, and
#      strictly greater at the last tick. `advances` is the frame cursor moving;
#      `poseChanges` is the uploaded pose actually changing. They are separate on
#      purpose -- a TF2 prop sequence can carry one raw pose for its whole span,
#      in which case the first is true and the second is false, and that is an
#      asset property rather than a wiring gap.
#   5. A named witness exists (a real model path, not "-") whose sampled frame
#      advances across the three ticks.
#   6. Three frames captured at three different ticks, at least two of which are
#      byte-distinct. This is the only assertion here that speaks to the picture.
#   7. Every bind-pose instance carries a non-empty reason.
#
# The demo must be on an *installed* map. `setEntityModelInstances` returns
# early when `worldBoundsValid_` is false, so a demo whose BSP is missing
# reports zero skinned instances for a reason that has nothing to do with
# animation. 73.dem (koth_bagel_rc13) is exactly that case on this machine and
# is therefore NOT the demo used here; cp_snakewater_final1 is installed.
#
# Usage: bash weapon-animation-check.sh [--mutation]
#   --mutation forces bind pose inside buildAnimationSkinMatrices, rebuilds, and
#   requires this script to go red -- the readings above must be able to fail.
# Exit:  0 = every assertion held (or, with --mutation, the checks went red).
set -uo pipefail
cd "$(dirname "$0")"

EXE=native/build-nmake/tf2_demo_native.exe
PROBE=native/build-nmake/weapon_animation_probe.exe
TF="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
DEMO="D:/TF2_Demo_Player/testdata/demos/bb841c6d379ff7c40d0c8baf99f59d8d_matcha-20260927-1347-cp_snakewater_final1.dem"
MAP="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf/maps/cp_snakewater_final1.bsp"
OUT="${WEAPON_ANIMATION_OUT:-evidence/weapon-animation}"
mkdir -p "$OUT"

# Ticks are playback positions, not the demo's own tick numbering: 73.dem's
# entities start at tick 128592 while its playback position starts at 0, so a
# --capture-tick written from a demo tick would never be reached.
TICKS=(2000 4000 6000)

MUTATION=0
[ "${1:-}" = "--mutation" ] && MUTATION=1

# A mutation run writes into its own directory. It produces the same filenames
# as the normal run, so sharing one directory would overwrite the committed
# evidence with mutated output -- which is exactly the shape that makes a
# "the gate passed" claim unfalsifiable.
if [ "$MUTATION" -eq 1 ]; then
  OUT="$OUT/mutation"
  mkdir -p "$OUT"
fi

PY="C:/Users/Administrator/.workbuddy-ai/binaries/python/versions/3.13.12/python.exe"
[ -x "$PY" ] || { echo "FATAL: missing interpreter: $PY"; exit 1; }
# The trace path is handed to a native Windows binary, which does not translate
# MSYS paths: "/d/..." is opened literally and the trace silently never opens,
# which reads exactly like "the product produced no animation". Convert once.
ROOT=$(cygpath -m "$(pwd)" 2>/dev/null || pwd)
case "$ROOT" in
  /[a-zA-Z]/*) echo "FATAL: could not convert '$ROOT' to a Windows path"; exit 1 ;;
esac
for path in "$EXE" "$PROBE" "$TF" "$DEMO" "$MAP"; do
  [ -e "$path" ] || { echo "FATAL: missing input: $path"; exit 1; }
done

# Audio: the device is opened during startup and pausing does not stop it, so
# every launch names it. The default device is the wrong one on this machine.
AUDIO_DEVICE=6
RUN_TIMEOUT=300

fail=0
assert_eq() { # assert_eq <label> <actual> <expected>
  if [ "${2:-<none>}" = "$3" ]; then
    echo "  OK   $1 = $3"
  else
    echo "  FAIL $1 = ${2:-<none>}, expected $3"; fail=1
  fi
}
assert_ge() { # assert_ge <label> <actual> <minimum>
  if [ "${2:-x}" -ge "$3" ] 2>/dev/null; then
    echo "  OK   $1 = $2 (>= $3)"
  else
    echo "  FAIL $1 = ${2:-<none>}, expected >= $3"; fail=1
  fi
}

# field <line> <name> -> the value of name=<value> in a trace line, "" if absent.
field() {
  printf '%s\n' "$1" | tr ' ' '\n' | sed -n "s/^$2=//p" | head -1
}

SOURCE=native/src/animation_binding.cpp
# The backup lives outside evidence/: it is a copy of a tracked source file, and
# a stale duplicate of a source file inside the evidence directory is exactly the
# kind of thing a later reader mistakes for an input.
BACKUP=".scratch/weapon-animation-mutation.bak"
mkdir -p .scratch

if [ "$MUTATION" -eq 1 ]; then
  echo "=== mutation: buildAnimationSkinMatrices is forced to the bind pose ==="
  cp "$SOURCE" "$BACKUP"
  "$PY" - "$SOURCE" <<'PYEOF'
import io, sys
path = sys.argv[1]
text = io.open(path, encoding="utf-8", newline="").read()
old_call = "  const auto sample = sampleAnimation(binding->anims, request.sequence, sampleTick, request.tickRate);"
new_call = "  auto sample = sampleAnimation(binding->anims, request.sequence, sampleTick, request.tickRate);"
anchor = "  result.frame = sample.frame;"
inject = (
    "  // MUTATION: ignore the sampled pose and upload the model's bind pose.\n"
    "  for (std::size_t i = 0; i < sample.localPosition.size() && i < binding->anims.bones.size(); ++i) {\n"
    "    sample.localPosition[i] = binding->anims.bones[i].position;\n"
    "    sample.localRotation[i] = binding->anims.bones[i].rotation;\n"
    "  }\n"
)
assert text.count(old_call) == 1, "call anchor"
assert text.count(anchor) == 1, "result anchor"
text = text.replace(old_call, new_call).replace(anchor, inject + anchor)
io.open(path, "w", encoding="utf-8", newline="").write(text)
print("MUTATION-PATCHED")
PYEOF
  bash build-target.sh tf2_demo_native > "$OUT/mutation-build.txt" 2>&1
  BUILD_RC=$?
  echo "mutation build rc=$BUILD_RC"
  if [ "$BUILD_RC" -ne 0 ]; then
    echo "FATAL: the mutated build did not compile; the mutation proves nothing"
    cp "$BACKUP" "$SOURCE"
    exit 1
  fi
fi

echo "=== 1/4 the product skins entity models on the GPU ==="
ANIM_LINES=()
ADVANCES=()
POSE_CHANGES=()
WITNESS_FRAMES=()
WITNESS_POSE=()
for tick in "${TICKS[@]}"; do
  TRACE="$OUT/tick-$tick.trace"
  BMP="$OUT/tick-$tick.bmp"
  rm -f "$BMP" "$BMP.csv"
  TF2_NATIVE_TRACE="$ROOT/$TRACE" timeout "$RUN_TIMEOUT" "$EXE" \
    --tf-root "$TF" --audio-device "$AUDIO_DEVICE" --demo "$DEMO" \
    --capture-frame "$BMP" --capture-tick "$tick" --metrics-file "$BMP.csv" \
    > "$OUT/tick-$tick.log" 2>&1
  rc=$?
  size=$(stat -c%s "$BMP" 2>/dev/null || echo 0)
  echo "tick=$tick rc=$rc bmp=$size"
  assert_eq "capture rc at tick $tick" "$rc" "0"
  line=$(grep "scene: anim " "$TRACE" 2>/dev/null | tail -1)
  witness=$(grep "scene: animwitness" "$TRACE" 2>/dev/null | tail -1)
  ANIM_LINES+=("$line")
  echo "  $line"
  echo "  $witness"
  if [ -z "$line" ]; then
    echo "  FAIL no 'scene: anim' line in the trace at tick $tick"; fail=1
    continue
  fi
  SKINNED=$(field "$line" skinned)
  MATRICES=$(field "$line" matrices)
  BONE_MISMATCH=$(field "$line" boneMismatch)
  BIND_POSE=$(field "$line" bindPose)
  REASON=${line##*reason=}
  GPU=$(field "$line" gpuSkin)
  GPU_ACCEPTED=${GPU%%/*}
  GPU_UPLOADED=${GPU##*/}
  assert_ge "skinned instances at tick $tick" "${SKINNED:-0}" 1
  assert_ge "bone matrices uploaded at tick $tick" "${MATRICES:-0}" 1
  assert_ge "GPU-accepted skinned instances at tick $tick" "${GPU_ACCEPTED:-0}" 1
  assert_eq "GPU bone matrices == caller matrices at tick $tick" "${GPU_UPLOADED:-none}" "${MATRICES:-x}"
  assert_eq "uploaded bones == valid bones at tick $tick" "${BONE_MISMATCH:-none}" "0"
  if [ "${BIND_POSE:-0}" -gt 0 ] && [ -z "$REASON" ]; then
    echo "  FAIL $BIND_POSE instance(s) fell back to bind pose with no reason"; fail=1
  else
    echo "  OK   bind-pose fallbacks carry a reason (${BIND_POSE:-0} instance(s))"
  fi
  # The witness line carries two witnesses; the `any=` one ends at ` target=`.
  # Cutting it out first is what keeps `frame=` from resolving to the target
  # witness's field, which the greedy match would otherwise do.
  ANY_PART=${witness#*animwitness any=}
  ANY_PART=${ANY_PART%% target=*}
  W_MODEL=${ANY_PART%% *}
  W_FRAME=$(printf '%s\n' "$ANY_PART" | tr ' ' '\n' | sed -n 's/^frame=\([0-9]*\)\/.*/\1/p' | head -1)
  W_POSE=$(printf '%s\n' "$ANY_PART" | tr ' ' '\n' | sed -n 's/^poseChanges=//p' | head -1)
  if [ -n "$W_MODEL" ] && [ "$W_MODEL" != "-" ]; then
    echo "  OK   witness at tick $tick is a named model: $W_MODEL"
  else
    echo "  FAIL no witness model at tick $tick -- nothing was caught moving"; fail=1
  fi
  WITNESS_FRAMES+=("${W_FRAME:-}")
  WITNESS_POSE+=("${W_POSE:-}")
  ADVANCES+=("$(field "$line" advances)")
  POSE_CHANGES+=("$(field "$line" poseChanges)")
done

echo
echo "=== 2/4 the cursor and the pose advance with the tick ==="
for index in 0 1 2; do
  echo "  tick ${TICKS[$index]}: advances=${ADVANCES[$index]:-?} poseChanges=${POSE_CHANGES[$index]:-?} witnessFrame=${WITNESS_FRAMES[$index]:-?} witnessPose=${WITNESS_POSE[$index]:-?}"
done
assert_ge "frame cursor advances by the last tick" "${ADVANCES[2]:-0}" 1
assert_ge "uploaded pose changes by the last tick" "${POSE_CHANGES[2]:-0}" 1
assert_ge "witness frame advances by the last tick" "${WITNESS_FRAMES[2]:-0}" 1
assert_ge "witness pose changes by the last tick" "${WITNESS_POSE[2]:-0}" 1
if [ "${ADVANCES[2]:-0}" -gt "${ADVANCES[0]:-0}" ] 2>/dev/null; then
  echo "  OK   the frame cursor count grows with the tick"
else
  echo "  FAIL the frame cursor count did not grow (${ADVANCES[0]:-?} -> ${ADVANCES[2]:-?})"; fail=1
fi
if [ "${POSE_CHANGES[2]:-0}" -gt "${POSE_CHANGES[0]:-0}" ] 2>/dev/null; then
  echo "  OK   the pose change count grows with the tick"
else
  echo "  FAIL the pose change count did not grow (${POSE_CHANGES[0]:-?} -> ${POSE_CHANGES[2]:-?})"; fail=1
fi

echo
echo "=== 3/4 three frames at three ticks, and the picture follows ==="
HASHES=()
for tick in "${TICKS[@]}"; do
  BMP="$OUT/tick-$tick.bmp"
  HASH=$("$PY" -c "import hashlib,sys;print(hashlib.sha256(open(sys.argv[1],'rb').read()).hexdigest())" "$BMP" 2>/dev/null || echo none)
  HASHES+=("$HASH")
  echo "  tick=$tick sha256=$HASH"
done
DISTINCT=$(printf '%s\n' "${HASHES[@]}" | sort -u | wc -l)
echo "distinct frames = $DISTINCT of ${#HASHES[@]}"
assert_ge "distinct captured frames" "$DISTINCT" 2

echo
echo "=== 4/4 the probe's matrix-order mutation is caught ==="
# The order mutation is the one the product readings cannot see (a wrong-but-live
# pose still advances), and the probe is the instrument that owns it: skinLands
# is the distance between a skinned bone and that bone's animated origin, so
# swapping the multiply order moves it from ~1e-06 to ~36.
PROBE_JSON=$(timeout "$RUN_TIMEOUT" "$PROBE" --tf-root "$TF" --demo "$DEMO" --ticks 6 --mutation order 2>/dev/null | tail -1)
PROBE_LANDS=$(printf '%s\n' "$PROBE_JSON" | tr ',' '\n' | sed -n 's/.*"skinLands":\([^,}]*\).*/\1/p')
echo "probe skinLands under --mutation order = ${PROBE_LANDS:-<none>}"
PROBE_CAUGHT=$("$PY" -c "
import sys
try:
    value = float('${PROBE_LANDS:-nan}')
except ValueError:
    value = float('nan')
print(1 if value > 1.0 else 0)
")
assert_eq "the order mutation moves skinLands off zero" "$PROBE_CAUGHT" "1"

echo
if [ "$MUTATION" -eq 1 ]; then
  cp "$BACKUP" "$SOURCE"
  bash build-target.sh tf2_demo_native > "$OUT/mutation-restore-build.txt" 2>&1
  echo "restore build rc=$?"
  if [ "$fail" -eq 1 ]; then
    echo "MUTATION-CAUGHT=PASS (forcing bind pose turns this step red)"
    exit 0
  fi
  echo "MUTATION-CAUGHT=FAIL (the checks stayed green with the pose forced to bind pose)"
  exit 1
fi

if [ "$fail" -eq 0 ]; then
  echo "WEAPON-ANIMATION=PASS"
else
  echo "WEAPON-ANIMATION=FAIL"
fi
exit "$fail"
