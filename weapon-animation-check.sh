#!/usr/bin/env bash
# weapon-animation-check.sh -- the *product* must skin models on the GPU from
# real demo animation input, and must say why when it cannot.
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
#      purpose -- a TF2 weapon sequence can carry one raw pose for its whole span,
#      in which case the first is true and the second is false, and that is an
#      asset property rather than a wiring gap. The step also asserts that the
#      weapon-class instance count is *reported* (its value is recorded, not
#      asserted): `targetPose=0` means "no weapon drawn" or "weapons stood still"
#      and the two have opposite fixes.
#   5. A named witness exists (a real model path, not "-") whose sampled frame
#      advances across the three ticks.
#   6. The picture, read against what the camera did. T34. This used to be
#      "three frames at three ticks, at least two byte-distinct", which reads as
#      "the picture follows the animation" and is not: a frame differs between
#      two ticks if EITHER the animation moved OR the camera did. On this demo
#      only the second can, so the assertion is now the pair
#        (a) the camera pose each tick resolved is READ out of the trace the
#            gate already writes (scene: camera ... eye= pitch= yaw=), and
#        (b) frames are required to be byte-identical when the camera did not
#            move, and to differ when it did.
#      Measured on this demo: the camera does not move (eye=0,0,0 at every one
#      of 5009 scene ticks; see T34-NEARCLIP-2026-10-20.md), so branch (b-i)
#      fires and the three frames are asserted IDENTICAL. That is a stronger
#      statement than the old "two differ" was: on a still camera any
#      difference would be the world or the animation moving, and the animation
#      moving is what sections 1-2 already measure per instance, per tick.
#      A demo whose camera does move gets branch (b-ii) automatically; the
#      choice is made from the reading, never from a flag or a demo name.
#   7. Every bind-pose instance carries a non-empty reason.
#   8. The standalone `--model` path skins with identity matrices and does NOT
#      deform the bind pose, while supplying a sequence must move it. That pair is
#      what separates "the branch runs" from "the branch moved the geometry".
#      T39 replaced the byte-exact bound with the rule the claim actually needs,
#      because the pair is not bit-exact BY DESIGN: the skinning shader evaluates
#      sum(w_i * M_i * v) with normalized weights, a different float expression
#      than the passthrough, so one sample sitting on a surface boundary can be
#      owned by the other triangle and take the neighbouring surface's colour
#      (measured one pixel, delta 59/255, on the final tree -- T39 report). The
#      rule, measured by bmp-silhouette-diff.py: outside a measured edge mask the
#      max channel delta <= 1 (strict; measured 0), every changed pixel lies
#      inside that mask (measured 1 of 1), the changed-pixel count is capped at 4
#      (measured 1; a moved transform changes thousands), the whole-frame max
#      delta has a ceiling of 128 so a big move cannot hide in the mask (measured
#      59), and the mask itself is a measured population (gradient >= 32 in the
#      control frame: 18,446 px = 2.14% of 860,784) so containment is not
#      vacuous. A control applies the same rule to the identity-vs-animated pair
#      -- a real pose change -- and requires it to be rejected.
#   9. The pose follows the tick, not just the cursor. The `--model` path is run
#      twice on a 100-frame game sequence at two ticks: the sampled frame index
#      must differ, the picture must differ by more than rounding could produce,
#      and tick 0 must render byte-identically to tick 100 (the index wraps at
#      the frame count rather than clamping). Without this, "a sequence moved the
#      geometry" is satisfied by any single-frame pose block, which is what most
#      TF2 weapon sequences are.
#  10. The probe's bind-pose mutation is discriminated: a normal run over a model
#      whose pose is known to move must read motion > 0, and the mutated run over
#      the same model must read 0 with at least one entity actually compared. A
#      mutation that "moved" a reading which was already zero proves nothing, so
#      both halves are asserted.
#  11. The probe's matrix-order mutation is caught (skinLands leaves ~1e-06).
#
# The demo must be on an *installed* map. `setEntityModelInstances` returns
# early when `worldBoundsValid_` is false, so a demo whose BSP is missing
# reports zero skinned instances for a reason that has nothing to do with
# animation. 73.dem (koth_bagel_rc13) is exactly that case on this machine and
# is therefore NOT the demo used here; cp_snakewater_final1 is installed.
#
# Inputs are all game-install or repo data: the demo, its BSP, and the two models
# (`models/player/scout.mdl`, `models/props_ui/bannerflag_comp_l.mdl`) read out of
# the game's own VPK. Nothing here depends on a loose custom model that only
# exists on one machine.
#
# Usage: bash weapon-animation-check.sh [--mutation | --mutation-model | --mutation-identity]
#   --mutation        forces bind pose inside buildAnimationSkinMatrices, rebuilds
#                     and requires this script to go red (the entity contract).
#   --mutation-model  replaces the identity matrices on the `--model` path with
#                     the model's inverse bind matrices -- the historical bug --
#                     and requires this script to go red (the single-model
#                     contract).
#   --mutation-identity  moves the identity matrices' model transform by 0.02 of
#                     the model's normalized box. The picture moves but no counter
#                     does; the silhouette rule must reject it, and the verdict is
#                     required to come from that rule rather than from an
#                     unrelated assertion.
#   --mutation-pose  moves the same model transform by 0.001 of the box instead --
#                     a sub-pixel rigid displacement of the bind pose (0.001 of the
#                     box is 0.22 px at the model's measured 223 px/unit scale
#                     bound). Every counter stays at its forward value; the
#                     silhouette rule must still reject the pair, which measures
#                     the rule's sensitivity floor.
#   The three `--model` mutations skip sections 1-3, which read the entity path and
#   cannot move because of them.
# Exit:  0 = every assertion held (or, with a mutation, the checks went red).
#
# One run at a time: the script takes .scratch/weapon-animation.lock with mkdir
# and refuses to start if another run owns it. It launches the product several
# times against the same frame and trace paths, so two concurrent runs overwrite
# each other's evidence; killing the wrapper is not enough, because the
# `timeout`+`tf2_demo_native` grandchildren survive it.
set -uo pipefail
cd "$(dirname "$0")"

EXE=native/build-nmake/tf2_demo_native.exe
PROBE=native/build-nmake/weapon_animation_probe.exe
TF="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
DEMO="D:/TF2_Demo_Player/testdata/demos/bb841c6d379ff7c40d0c8baf99f59d8d_matcha-20260927-1347-cp_snakewater_final1.dem"
MAP="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf/maps/cp_snakewater_final1.bsp"
# A model that ships with the game, so the gate does not depend on a loose
# custom model. It is read out of tf2_misc_*.vpk through the same archive set
# the entity path uses. It carries the 78-bone player skeleton, which is the
# shape the bind-pose assertion needs: identity skin matrices through the
# skinning branch must leave this skeleton's bind pose where it was.
#
# It is NOT a motion witness. Of its 25 sequences only two carry an animation
# binding at all -- `ref` (0) and `ragdoll` (1) -- and both are a single frame,
# because a TF2 player model includes its animations from a separate model
# rather than embedding them. One frame proves the branch composes a real pose;
# it cannot prove the pose follows the tick, so that claim is made on the model
# below instead of being implied by this one.
MODEL="models/player/scout.mdl"
MODEL_SEQUENCE=0
MODEL_TICK=30
# The motion witness: a game prop whose `wave` sequence has 100 frames, measured
# to advance its frame index with the tick (0/15/30/50 -> frame 0/15/30/50) and
# to wrap at 100 (tick 100 renders byte-identically to tick 0). This is the pair
# that separates "the cursor moved" from "the picture moved".
WAVE_MODEL="models/props_ui/bannerflag_comp_l.mdl"
WAVE_SEQUENCE=1
WAVE_FRAMES=100
WAVE_TICK_A=15
WAVE_TICK_B=50
# The only entity in the reference demo whose *pose* (not just its frame cursor)
# moves is this prop. `--models` is how the probe is aimed at it; the class-name
# heuristic the probe uses by default cannot see it, which is exactly why the
# bind-pose mutation read zero on both sides before this.
WITNESS_FILTER="props_ui/bannerflag_comp_l"

# Frozen artefacts (frames, summary, failure text) are committed. Traces and
# metrics CSVs are machine output -- ~150k lines for three ticks, and a CSV whose
# elapsed/fps/working-set columns differ on every run -- and live under
# evidence/fast, which is where the plan puts gate output that must not be
# mistaken for input.
OUT="${WEAPON_ANIMATION_OUT:-evidence/weapon-animation}"
TRACE_DIR="${WEAPON_ANIMATION_TRACE_DIR:-evidence/fast/weapon-animation}"
mkdir -p "$OUT" "$TRACE_DIR"

# Ticks are playback positions, not the demo's own tick numbering: 73.dem's
# entities start at tick 128592 while its playback position starts at 0, so a
# --capture-tick written from a demo tick would never be reached.
TICKS=(2000 4000 6000)

# T34: how far the camera may drift between two ticks and still count as
# "still". The demo resolves its eye as exactly 0,0,0 on every tick, so this
# bound does not sit near a measurement -- it exists so that a demo whose eye is
# merely *nearly* still is treated as still rather than flipping the branch on
# float noise.
CAMERA_STILL_EPS=0.5

# camera_pose <trace> -> "x,y,z pitch yaw src", or "" when the trace carries no
# camera reading. The pose is the LAST one in the trace: the capture is written
# at the last draw, so the last scene tick is the one the frame belongs to.
camera_pose() {
  local line eye pitch yaw src
  line=$(tr -d '\r' < "$1" 2>/dev/null | grep -a 'scene: camera mode=' | tail -1)
  [ -n "$line" ] || { printf ''; return 0; }
  eye=$(printf '%s\n' "$line" | grep -oE 'eye=[-0-9.,]+' | head -1 | cut -d= -f2)
  pitch=$(printf '%s\n' "$line" | grep -oE 'pitch=[-0-9.]+' | head -1 | cut -d= -f2)
  yaw=$(printf '%s\n' "$line" | grep -oE 'yaw=[-0-9.]+' | head -1 | cut -d= -f2)
  src=$(printf '%s\n' "$line" | grep -oE 'src=[A-Za-z-]+' | head -1 | cut -d= -f2)
  printf '%s %s %s %s' "$eye" "$pitch" "$yaw" "$src"
}

# camera_moved <pose-a> <pose-b> -> "<delta> moved|still|no-reading". Yaw is
# compared on the circle, so 359.9 -> 0.1 is a 0.2 degree move, not 359.8.
camera_moved() {
  "$PY" - "$1" "$2" "$CAMERA_STILL_EPS" <<'PYEOF_CAM'
import sys
a = sys.argv[1].replace(",", " ").split()
b = sys.argv[2].replace(",", " ").split()
eps = float(sys.argv[3])
if len(a) < 5 or len(b) < 5:
    print("0 no-reading")
    sys.exit(0)
d = max(abs(float(a[i]) - float(b[i])) for i in range(3))
dp = abs(float(a[3]) - float(b[3]))
dy = abs(float(a[4]) - float(b[4])) % 360.0
dy = min(dy, 360.0 - dy)
w = max(d, dp, dy)
print(("%.3f " % w) + ("moved" if w > eps else "still"))
PYEOF_CAM
}

MUTATION=0
MUTATION_TARGET=""
case "${1:-}" in
  --mutation)          MUTATION=1; MUTATION_TARGET="entity" ;;
  --mutation-model)    MUTATION=1; MUTATION_TARGET="model" ;;
  --mutation-identity) MUTATION=1; MUTATION_TARGET="identity" ;;
  --mutation-pose)     MUTATION=1; MUTATION_TARGET="pose" ;;
  "")               ;;
  *) echo "FATAL: unknown option: $1"; exit 1 ;;
esac

# One run owns the frames, the trace directory and the mutated source. Two
# callers sharing them produce a plausible but unusable report, and that is not
# hypothetical: on 2026-10-10 a killed run left `timeout`+`tf2_demo_native`
# grandchildren alive (killing the wrapper does not kill them), a second run
# started, and the two wrote the same tick paths -- the gate's own log stayed
# empty while the trace grew, which reads exactly like a hang. mkdir is atomic on
# the filesystem, so a second caller fails before it builds anything. The lock is
# removed only on normal shell exit; a stale lock after a killed process must be
# cleared deliberately rather than silently overlapped.
mkdir -p .scratch
LOCK_DIR=.scratch/weapon-animation.lock

# A LOCK NOBODY CAN INTERPRET FAILS FOREVER
#
# The comment above says a stale lock "must be cleared deliberately", and on
# 2026-10-22 nobody did: a run killed at 15:38:53 left this directory behind, and
# the 24-step acceptance chain failed step 22 five hours later with
# assertions_ok=0 -- zero assertions executed, because the gate never started.
# The reading looked like a product failure but nothing about the product had
# been measured, and the empty lock could not be told apart from a live one.
#
# So the lock now records who owns it and when it was born. A lock whose owner
# pid is gone, or that is older than WEAPON_LOCK_STALE_SECONDS, is removed and the
# run continues, saying so. A lock whose owner is alive is still honoured, and
# nothing is ever overlapped silently.
WEAPON_LOCK_STALE_SECONDS=${WEAPON_LOCK_STALE_SECONDS:-5400}
take_lock() {
  mkdir "$LOCK_DIR" 2>/dev/null || return 1
  { echo "pid=$$"; echo "started=$(date +%s)"; } > "$LOCK_DIR/owner" 2>/dev/null || true
  return 0
}
lock_owner_alive() {
  local f="$LOCK_DIR/owner" pid
  [ -f "$f" ] || return 1
  pid=$(sed -n "s/^pid=//p" "$f" 2>/dev/null | head -1)
  case "$pid" in ""|*[!0-9]*) return 1 ;; esac
  kill -0 "$pid" 2>/dev/null # 0 when a process with that pid exists
}
if ! take_lock; then
  now=$(date +%s)
  born=$(sed -n "s/^started=//p" "$LOCK_DIR/owner" 2>/dev/null | head -1)
  case "$born" in
    ""|*[!0-9]*)
      # No owner file at all -- exactly what the 15:38 lock looked like, because it
      # predates this change. Fall back to the directory's own mtime; defaulting to
      # "now" here would have made every pre-existing stale lock look freshly born
      # and the whole change would have proved nothing.
      born=$(stat -c %Y "$LOCK_DIR" 2>/dev/null || echo "$now") ;;
  esac
  age=$((now - born))
  if lock_owner_alive; then
    echo "WEAPON-ANIMATION=FAIL (another weapon-animation-check run owns $LOCK_DIR, owner alive, age=${age}s)"
    exit 2
  fi
  if [ "$age" -ge "$WEAPON_LOCK_STALE_SECONDS" ]; then
    rm -rf "$LOCK_DIR"
    echo "WEAPON-LOCK=STALE-REMOVED (no live owner, age=${age}s >= ${WEAPON_LOCK_STALE_SECONDS}s)"
    if ! take_lock; then
      echo "WEAPON-ANIMATION=FAIL (could not take $LOCK_DIR even after clearing a stale one)"
      exit 2
    fi
  else
    echo "WEAPON-ANIMATION=FAIL (no live owner but age=${age}s < ${WEAPON_LOCK_STALE_SECONDS}s; refusing to treat a young lock as stale)"
    exit 2
  fi
fi
trap 'rm -rf "$LOCK_DIR" 2>/dev/null || true' EXIT

# A mutation run writes into its own directory. It produces the same filenames
# as the normal run, so sharing one directory would overwrite the committed
# evidence with mutated output -- which is exactly the shape that makes a
# "the gate passed" claim unfalsifiable.
RUN_DIR="$OUT"
if [ "$MUTATION" -eq 1 ]; then
  RUN_DIR="$TRACE_DIR/mutation"
  mkdir -p "$RUN_DIR"
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
for path in "$EXE" "$PROBE" "$TF" "$DEMO" "$MAP" "$PY" bmp-pixel-diff.py bmp-silhouette-diff.py; do
  [ -e "$path" ] || { echo "FATAL: missing input: $path"; exit 1; }
done

# Audio: the device is opened during startup and pausing does not stop it, so
# every launch names it. The default device is the wrong one on this machine.
AUDIO_DEVICE=6
RUN_TIMEOUT=300

fail=0
FAIL_LINES=()
assert_eq() { # assert_eq <label> <actual> <expected>
  if [ "${2:-<none>}" = "$3" ]; then
    echo "  OK   $1 = $3"
  else
    echo "  FAIL $1 = ${2:-<none>}, expected $3"; fail=1; FAIL_LINES+=("$1: ${2:-<none>} != $3")
  fi
}
assert_ge() { # assert_ge <label> <actual> <minimum>
  if [ "${2:-x}" -ge "$3" ] 2>/dev/null; then
    echo "  OK   $1 = $2 (>= $3)"
  else
    echo "  FAIL $1 = ${2:-<none>}, expected >= $3"; fail=1; FAIL_LINES+=("$1: ${2:-<none>} < $3")
  fi
}
assert_ne() { # assert_ne <label> <actual> <forbidden>
  if [ -n "${2:-}" ] && [ "${2:-}" != "$3" ]; then
    echo "  OK   $1 = $2 (!= $3)"
  else
    echo "  FAIL $1 = ${2:-<none>}, expected anything but $3"; fail=1; FAIL_LINES+=("$1: ${2:-<none>} == $3")
  fi
}
assert_gt() { # assert_gt <label> <actual> <exclusive minimum>, float comparison
  local ok
  ok=$("$PY" -c "import sys
try: print(1 if float(sys.argv[1]) > float(sys.argv[2]) else 0)
except ValueError: print(0)" "${2:-nan}" "$3")
  if [ "$ok" = "1" ]; then
    echo "  OK   $1 = $2 (> $3)"
  else
    echo "  FAIL $1 = ${2:-<none>}, expected > $3"; fail=1; FAIL_LINES+=("$1: ${2:-<none>} <= $3")
  fi
}

# field <line> <name> -> the value of name=<value> in a trace line, "" if absent.
field() {
  printf '%s\n' "$1" | tr ' ' '\n' | sed -n "s/^$2=//p" | head -1
}
# jfield <json> <name> -> the value of "name":<value>, "" if absent.
jfield() {
  printf '%s\n' "$1" | tr ',' '\n' | sed -n "s/.*\"$2\":\([^,}]*\).*/\1/p" | head -1
}
# bmp_diff <label> <a> <b> <bounds...> -> PASS/FAIL of one pixel comparison.
# The exit code of the tool is the assertion: 0 held, 1 a bound failed, 2 the
# input could not be read. All three are reported, none are skipped.
bmp_diff() {
  local label="$1" a="$2" b="$3"; shift 3
  local line rc
  line=$("$PY" bmp-pixel-diff.py "$a" "$b" "$@" 2>&1)
  rc=$?
  echo "  $line"
  if [ "$rc" -eq 0 ]; then
    echo "  OK   $label"
  else
    echo "  FAIL $label (rc=$rc)"; fail=1; FAIL_LINES+=("$label: $line")
  fi
}
# silhouette_diff <label> <a> <b> <edge-frame> <bounds...> -> PASS/FAIL of the
# semantic "it may only differ on an edge" rule. The tool's exit code is the
# assertion, exactly like bmp_diff: 0 held, 1 a bound failed, 2 the input could
# not be read. All three are reported, none are skipped.
silhouette_diff() {
  local label="$1" a="$2" b="$3" edge="$4"; shift 4
  local line rc
  line=$("$PY" bmp-silhouette-diff.py "$a" "$b" --edge-frame "$edge" "$@" 2>&1)
  rc=$?
  echo "  $line"
  if [ "$rc" -eq 0 ]; then
    echo "  OK   $label"
  else
    echo "  FAIL $label (rc=$rc)"; fail=1; FAIL_LINES+=("$label: $line")
  fi
  return "$rc"
}

SOURCE=""
BACKUP=""
# The backup lives outside evidence/: it is a copy of a tracked source file, and
# a stale duplicate of a source file inside the evidence directory is exactly the
# kind of thing a later reader mistakes for an input.
mkdir -p .scratch

if [ "$MUTATION" -eq 1 ]; then
  if [ "$MUTATION_TARGET" = "entity" ]; then
    echo "=== mutation: buildAnimationSkinMatrices is forced to the bind pose ==="
    SOURCE=native/src/animation_binding.cpp
    BACKUP=".scratch/weapon-animation-mutation.bak"
    cp "$SOURCE" "$BACKUP"
    if ! "$PY" - "$SOURCE" <<'PYEOF'
import io, sys
# The source files are checked out with CRLF on this machine
# (core.autocrlf=true), so the patch works on an LF-normalised copy and restores
# the original line endings on write. mutate.sh's patch_in does the same thing
# for the same reason; a pattern written with bare '\n' against a CRLF tree
# matches nothing at all.
path = sys.argv[1]
raw = io.open(path, "rb").read()
crlf = b"\r\n" in raw
text = raw.decode("utf-8").replace("\r\n", "\n") if crlf else raw.decode("utf-8")
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
if crlf:
    text = text.replace("\n", "\r\n")
io.open(path, "wb").write(text.encode("utf-8"))
print("MUTATION-PATCHED")
PYEOF
    then
      echo "FATAL: the mutation did not apply; the mutated build would prove nothing"
      cp "$BACKUP" "$SOURCE"
      exit 1
    fi
  elif [ "$MUTATION_TARGET" = "model" ]; then
    echo "=== mutation: the --model identity matrices become the inverse bind matrices ==="
    SOURCE=native/src/main.cpp
    BACKUP=".scratch/weapon-animation-mutation-model.bak"
    cp "$SOURCE" "$BACKUP"
    if ! "$PY" - "$SOURCE" <<'PYEOF'
import io, sys
path = sys.argv[1]
raw = io.open(path, "rb").read()
crlf = b"\r\n" in raw
text = raw.decode("utf-8").replace("\r\n", "\n") if crlf else raw.decode("utf-8")
anchor = (
    "        identity.reserve(bindBoneCount);\n"
    "        for (std::size_t i = 0; i < bindBoneCount; ++i) {\n"
    "          std::array<float, 16> matrix{};\n"
    "          matrix[0] = 1.0f; matrix[5] = 1.0f; matrix[10] = 1.0f; matrix[15] = 1.0f;\n"
    "          identity.push_back(matrix);\n"
)
inject = (
    "        identity.reserve(bindBoneCount);\n"
    "        for (std::size_t i = 0; i < bindBoneCount; ++i) {\n"
    "          std::array<float, 16> matrix{};\n"
    "          matrix[0] = 1.0f; matrix[5] = 1.0f; matrix[10] = 1.0f; matrix[15] = 1.0f;\n"
    "          // MUTATION: upload the inverse bind matrix instead of identity, which\n"
    "          // is what this path did before it was fixed.\n"
    "          if (i < modelMetadata.bones.size() && modelMetadata.bones[i].poseToBoneValid) {\n"
    "            const auto& p = modelMetadata.bones[i].poseToBone;\n"
    "            matrix = { p[0], p[4], p[8], 0.0f, p[1], p[5], p[9], 0.0f,\n"
    "                       p[2], p[6], p[10], 0.0f, p[3], p[7], p[11], 1.0f };\n"
    "          }\n"
    "          identity.push_back(matrix);\n"
)
assert text.count(anchor) == 1, "standalone model identity anchor"
text = text.replace(anchor, inject)
if crlf:
    text = text.replace("\n", "\r\n")
io.open(path, "wb").write(text.encode("utf-8"))
print("MUTATION-PATCHED")
PYEOF
    then
      echo "FATAL: the mutation did not apply; the mutated build would prove nothing"
      cp "$BACKUP" "$SOURCE"
      exit 1
    fi
  elif [ "$MUTATION_TARGET" = "pose" ]; then
    echo "=== mutation: the --model identity matrices displace the pose by 0.001 of the box ==="
    SOURCE=native/src/main.cpp
    BACKUP=".scratch/weapon-animation-mutation-pose.bak"
    cp "$SOURCE" "$BACKUP"
    if ! "$PY" - "$SOURCE" <<'PYEOF'
import io, sys
path = sys.argv[1]
raw = io.open(path, "rb").read()
crlf = b"\r\n" in raw
text = raw.decode("utf-8").replace("\r\n", "\n") if crlf else raw.decode("utf-8")
anchor = (
    "        identity.reserve(bindBoneCount);\n"
    "        for (std::size_t i = 0; i < bindBoneCount; ++i) {\n"
    "          std::array<float, 16> matrix{};\n"
    "          matrix[0] = 1.0f; matrix[5] = 1.0f; matrix[10] = 1.0f; matrix[15] = 1.0f;\n"
    "          identity.push_back(matrix);\n"
)
inject = (
    "        identity.reserve(bindBoneCount);\n"
    "        for (std::size_t i = 0; i < bindBoneCount; ++i) {\n"
    "          std::array<float, 16> matrix{};\n"
    "          matrix[0] = 1.0f; matrix[5] = 1.0f; matrix[10] = 1.0f; matrix[15] = 1.0f;\n"
    "          // MUTATION (T48): displace the bind pose by a rigid thousandth of the\n"
    "          // normalized box. No counter, texture or resource reading changes;\n"
    "          // only the picture moves, so the silhouette rule must reject this\n"
    "          // pair and thereby measure its sensitivity floor.\n"
    "          matrix[12] = 0.001f;\n"
    "          identity.push_back(matrix);\n"
)
assert text.count(anchor) == 1, "standalone model identity matrix anchor"
text = text.replace(anchor, inject)
if crlf:
    text = text.replace("\n", "\r\n")
io.open(path, "wb").write(text.encode("utf-8"))
print("MUTATION-PATCHED")
PYEOF
    then
      echo "FATAL: the mutation did not apply; the mutated build would prove nothing"
      cp "$BACKUP" "$SOURCE"
      exit 1
    fi
  else
    echo "=== mutation: the --model identity matrices move the model transform ==="
    SOURCE=native/src/main.cpp
    BACKUP=".scratch/weapon-animation-mutation-identity.bak"
    cp "$SOURCE" "$BACKUP"
    if ! "$PY" - "$SOURCE" <<'PYEOF'
import io, sys
path = sys.argv[1]
raw = io.open(path, "rb").read()
crlf = b"\r\n" in raw
text = raw.decode("utf-8").replace("\r\n", "\n") if crlf else raw.decode("utf-8")
anchor = (
    "        identity.reserve(bindBoneCount);\n"
    "        for (std::size_t i = 0; i < bindBoneCount; ++i) {\n"
    "          std::array<float, 16> matrix{};\n"
    "          matrix[0] = 1.0f; matrix[5] = 1.0f; matrix[10] = 1.0f; matrix[15] = 1.0f;\n"
    "          identity.push_back(matrix);\n"
)
inject = (
    "        identity.reserve(bindBoneCount);\n"
    "        for (std::size_t i = 0; i < bindBoneCount; ++i) {\n"
    "          std::array<float, 16> matrix{};\n"
    "          matrix[0] = 1.0f; matrix[5] = 1.0f; matrix[10] = 1.0f; matrix[15] = 1.0f;\n"
    "          // MUTATION: a rigid move of the model transform through the identity\n"
    "          // matrices. It moves the picture but no pose counter, and no texture\n"
    "          // or resource reading changes; the silhouette rule must reject it.\n"
    "          matrix[12] = 0.02f;\n"
    "          identity.push_back(matrix);\n"
)
assert text.count(anchor) == 1, "standalone model identity matrix anchor"
text = text.replace(anchor, inject)
if crlf:
    text = text.replace("\n", "\r\n")
io.open(path, "wb").write(text.encode("utf-8"))
print("MUTATION-PATCHED")
PYEOF
    then
      echo "FATAL: the mutation did not apply; the mutated build would prove nothing"
      cp "$BACKUP" "$SOURCE"
      exit 1
    fi
  fi
  bash build-target.sh tf2_demo_native > "$RUN_DIR/mutation-build.txt" 2>&1
  BUILD_RC=$?
  echo "mutation build rc=$BUILD_RC"
  if [ "$BUILD_RC" -ne 0 ]; then
    echo "FATAL: the mutated build did not compile; the mutation proves nothing"
    cp "$BACKUP" "$SOURCE"
    exit 1
  fi
fi

# The two `--model` mutations rewrite the identity matrices on the standalone
# path and nothing else. Sections 1-3 read the *entity* path -- three demo
# playbacks, ~5 of this gate's 7 minutes -- and cannot move because of them. They
# are skipped rather than re-run: a mutation that costs as much as the gate it
# guards stops being run, and an unrun mutation is not a control.
ENTITY_SECTIONS=1
if [ "$MUTATION_TARGET" = "model" ] || [ "$MUTATION_TARGET" = "identity" ] || [ "$MUTATION_TARGET" = "pose" ]; then
  ENTITY_SECTIONS=0
fi

if [ "$ENTITY_SECTIONS" -eq 0 ]; then
  echo "=== 1-3/6 skipped: the --model mutation does not touch the entity path ==="
fi

# Declared unconditionally: readings.txt reads these whether or not sections 1-3
# ran, and an undeclared array under `set -u` is a crash rather than an empty
# reading.
ANIM_LINES=(); ADVANCES=(); POSE_CHANGES=(); WITNESS_FRAMES=(); WITNESS_POSE=()
HASHES=(); DISTINCT=0

if [ "$ENTITY_SECTIONS" -eq 1 ]; then
echo "=== 1/6 the product skins entity models on the GPU ==="
ANIM_LINES=()
ADVANCES=()
POSE_CHANGES=()
WITNESS_FRAMES=()
WITNESS_POSE=()
for tick in "${TICKS[@]}"; do
  TRACE="$TRACE_DIR/tick-$tick.trace"
  BMP="$RUN_DIR/tick-$tick.bmp"
  # The metrics CSV carries elapsed_seconds, fps and working-set bytes, so it is
  # a different file on every run. It is machine output like the trace, and it
  # lives with the trace; only the captured frame is a frozen witness.
  CSV="$TRACE_DIR/tick-$tick.bmp.csv"
  rm -f "$BMP" "$CSV"
  # --hidden-window/--audio-disabled: the host runs one capture at a time and the
  # window and audio device must not be visible to whoever else is logged in. T39
  # measured that adding them changes no pixel: the identity/control pair is
  # byte-identical under the flag pair and under the previous invocation (both
  # frame hashes recorded in T39-WEAPON-ANIMATION-2026-10-21.md).
  TF2_NATIVE_TRACE="$ROOT/$TRACE" timeout "$RUN_TIMEOUT" "$EXE" \
    --tf-root "$TF" --audio-device "$AUDIO_DEVICE" --hidden-window --audio-disabled \
    --demo "$DEMO" \
    --capture-frame "$BMP" --capture-tick "$tick" --metrics-file "$CSV" \
    > "$TRACE_DIR/tick-$tick.log" 2>&1
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
    FAIL_LINES+=("no 'scene: anim' line at tick $tick")
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
  # `targetPose=0` alone cannot be read: it means either "no weapon-class model
  # was drawn" or "weapons were drawn and stood still", and only the second is a
  # wiring defect. The count is asserted to be *present* -- its value is the
  # reading, and it is recorded in readings.txt rather than asserted, because a
  # demo with no weapon in the window is a content boundary and not a failure of
  # this step.
  assert_ne "the anim reading carries the weapon-class instance count at tick $tick" \
    "$(field "$line" targetInstances)" ""
  TARGET_INSTANCES=$(field "$line" targetInstances)
  echo "  ..   weapon-class instances in this update's draw list at tick $tick = ${TARGET_INSTANCES:-<none>}"
  if [ "${BIND_POSE:-0}" -gt 0 ] && [ -z "$REASON" ]; then
    echo "  FAIL $BIND_POSE instance(s) fell back to bind pose with no reason"; fail=1
    FAIL_LINES+=("bind-pose fallback without a reason at tick $tick")
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
    FAIL_LINES+=("no witness model at tick $tick")
  fi
  WITNESS_FRAMES+=("${W_FRAME:-}")
  WITNESS_POSE+=("${W_POSE:-}")
  ADVANCES+=("$(field "$line" advances)")
  POSE_CHANGES+=("$(field "$line" poseChanges)")
done

echo
echo "=== 2/6 the cursor and the pose advance with the tick ==="
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
  FAIL_LINES+=("frame cursor did not grow")
fi
if [ "${POSE_CHANGES[2]:-0}" -gt "${POSE_CHANGES[0]:-0}" ] 2>/dev/null; then
  echo "  OK   the pose change count grows with the tick"
else
  echo "  FAIL the pose change count did not grow (${POSE_CHANGES[0]:-?} -> ${POSE_CHANGES[2]:-?})"; fail=1
  FAIL_LINES+=("pose change count did not grow")
fi

echo
echo "=== 3/6 the frames, read against what the camera did ==="
# T34. The reading decides which assertion applies. It is not a flag and not a
# per-demo hardcode: the poses are read out of the three traces sections 1-2
# already produced, so a demo whose camera moves gets the "frames must differ"
# side without anyone editing this file.
HASHES=()
POSES=()
for tick in "${TICKS[@]}"; do
  BMP="$RUN_DIR/tick-$tick.bmp"
  POSE=$(camera_pose "$TRACE_DIR/tick-$tick.trace")
  POSES+=("$POSE")
  HASH=$("$PY" -c "import hashlib,sys;print(hashlib.sha256(open(sys.argv[1],'rb').read()).hexdigest())" "$BMP" 2>/dev/null || echo none)
  HASHES+=("$HASH")
  echo "  tick=$tick camera='$POSE' sha256=$HASH"
done
DISTINCT=$(printf '%s\n' "${HASHES[@]}" | sort -u | wc -l)
echo "  distinct frames = $DISTINCT of ${#HASHES[@]}"
# The camera reading must exist. A trace without it cannot decide either branch,
# and silently defaulting to "still" would let this section pass on a build that
# stopped publishing the pose.
CAM_READINGS=0
for index in 0 1 2; do
  if [ -n "${POSES[$index]}" ]; then CAM_READINGS=$((CAM_READINGS + 1)); fi
done
assert_eq "ticks that published a camera pose" "$CAM_READINGS" "3"
# The projection the frames were drawn through. The frame claim below rests on
# it: T29 put the near plane in map units (4.0) so the room the camera stands in
# is drawn at all. If it goes back to the box-unit constant that plane is ~603
# map units -- past the room -- and the camera looks at the far side of the map.
# This is the quantity world-near-clip-check.sh owns; it is re-read here because
# THIS gate's picture reading is only meaningful through it.
CLIP_LINE=$(tr -d '\r' < "$TRACE_DIR/tick-${TICKS[0]}.trace" 2>/dev/null | grep -a 'gpu: world clip ' | head -1)
NEAR_MAP=$(printf '%s\n' "$CLIP_LINE" | grep -oE 'nearMapUnits=[-0-9.]+' | head -1 | cut -d= -f2)
echo "  near plane: nearMapUnits=$NEAR_MAP (T29: 4.0 map units)"
if awk -v v="$NEAR_MAP" 'BEGIN{exit !(v >= 3.0 && v <= 5.0)}'; then
  echo "  OK   the frames were drawn through the T29 near plane (nearMapUnits=$NEAR_MAP in [3,5])"
else
  echo "  FAIL the frames were NOT drawn through the T29 near plane (nearMapUnits=${NEAR_MAP:-none}, want [3,5])"
  fail=1; FAIL_LINES+=("near plane: nearMapUnits=${NEAR_MAP:-none} outside [3,5]")
fi
CAM_SPREAD=$(camera_moved "${POSES[0]}" "${POSES[2]}")
CAM_MOVED=$(printf '%s' "$CAM_SPREAD" | awk '{print $2}')
echo "  camera across ticks ${TICKS[0]}..${TICKS[2]}: $CAM_SPREAD (eps=$CAMERA_STILL_EPS)"
assert_ne "the camera reading resolved a branch" "$CAM_MOVED" "no-reading"
if [ "$CAM_MOVED" = "still" ]; then
  # The camera did not move, so the only correct reading is that the frames are
  # one picture. This is the assertion the old "distinct >= 2" inverted, and it
  # is the whole of T34: on a still camera three DIFFERING frames are a world
  # difference being credited to the animation chain.
  echo "  ..   camera still across the three ticks: the frames must be ONE picture"
  assert_eq "frames of a still camera are one picture (distinct)" "$DISTINCT" "1"
  # The positive control: a still camera over a static world still has to show
  # the world, so the frame may not be a flat fill. 32 distinct colours over a
  # 1-in-49 sample is a floor a black or single-colour frame cannot reach and a
  # real frame clears by orders of magnitude (the measured value is printed on
  # the line immediately above the assertion).
  FRAME_COLOURS=$("$PY" - "$RUN_DIR/tick-${TICKS[0]}.bmp" <<'PYEOF_COLOURS'
import struct, sys
d = open(sys.argv[1], "rb").read()
off = struct.unpack_from("<I", d, 10)[0]
w, h = struct.unpack_from("<ii", d, 18)
stride = (abs(w) * 3 + 3) // 4 * 4
seen = set()
for y in range(0, abs(h), 7):
    base = off + y * stride
    for x in range(0, w, 7):
        seen.add(d[base + x * 3:base + x * 3 + 3])
print(len(seen))
PYEOF_COLOURS
)
  echo "  ..   distinct sampled colours in the tick ${TICKS[0]} frame: $FRAME_COLOURS"
  assert_ge "the still-camera frame is a picture and not a flat fill" "$FRAME_COLOURS" 32
else
  # The camera moved, so the frames MUST differ: that is what made the old
  # assertion mean what it said.
  echo "  ..   camera moved across the three ticks: the frames must differ"
  assert_ge "frames of a moving camera differ (distinct)" "$DISTINCT" 2
fi
fi

echo
echo "=== 4/6 the standalone --model path skins without deforming the bind pose ==="
# Two runs that differ only in the shader branch, and one that differs in the
# input pose. `--model-skinning 0` is the control: the same matrices are
# uploaded but the skinning branch is disabled, so it renders the vertex buffer
# exactly as the pipeline did before any of this existed. The pair is not
# bit-exact by design (the shader's weighted sum is a different float
# expression), so section 4 asserts the semantic rule below rather than byte
# equality; if the branch moved the geometry, thousands of interior and edge
# pixels would disagree, not one.
run_model() { # run_model <name> <model> <args...>
  local name="$1" model="$2"; shift 2
  local bmp="$RUN_DIR/model-$name.bmp"
  rm -f "$bmp"
  TF2_NATIVE_TRACE="$ROOT/$TRACE_DIR/model-$name.trace" timeout "$RUN_TIMEOUT" "$EXE" \
    --tf-root "$TF" --audio-device "$AUDIO_DEVICE" --hidden-window --audio-disabled \
    --model "$model" \
    --capture-frame "$bmp" "$@" > "$TRACE_DIR/model-$name.log" 2>&1
  echo "$bmp"
}
# skin_line <name> -> the `gpu: model skin` trace line, or a marker when the
# model never resolved. `--model` on a path that is not in the archive set is a
# silent no-op in the product, so a missing line is reported as such rather than
# left to read as an absent assertion.
skin_line() {
  local line
  line=$(grep -a 'gpu: model skin' "$TRACE_DIR/model-$1.trace" 2>/dev/null | tail -1)
  if [ -z "$line" ]; then
    line=$(grep -a 'gpu: model inspect' "$TRACE_DIR/model-$1.trace" 2>/dev/null | tail -1)
    [ -n "$line" ] && line="<no skin upload> $line"
  fi
  printf '%s' "$line"
}

MODEL_IDENTITY=$(run_model identity "$MODEL" --model-skinning 1)
MODEL_CONTROL=$(run_model control "$MODEL" --model-skinning 0)
MODEL_ANIMATED=$(run_model animated "$MODEL" --model-sequence "$MODEL_SEQUENCE" --model-tick "$MODEL_TICK")
WAVE_A=$(run_model wave-a "$WAVE_MODEL" --model-sequence "$WAVE_SEQUENCE" --model-tick "$WAVE_TICK_A")
WAVE_B=$(run_model wave-b "$WAVE_MODEL" --model-sequence "$WAVE_SEQUENCE" --model-tick "$WAVE_TICK_B")
WAVE_ZERO=$(run_model wave-zero "$WAVE_MODEL" --model-sequence "$WAVE_SEQUENCE" --model-tick 0)
WAVE_FULL=$(run_model wave-full "$WAVE_MODEL" --model-sequence "$WAVE_SEQUENCE" --model-tick "$WAVE_FRAMES")

IDENTITY_LINE=$(skin_line identity)
CONTROL_LINE=$(skin_line control)
ANIMATED_LINE=$(skin_line animated)
WAVE_A_LINE=$(skin_line wave-a)
WAVE_B_LINE=$(skin_line wave-b)
WAVE_FULL_LINE=$(skin_line wave-full)
echo "  identity: $IDENTITY_LINE"
echo "  control : $CONTROL_LINE"
echo "  animated: $ANIMATED_LINE"
echo "  wave-a  : $WAVE_A_LINE"
echo "  wave-b  : $WAVE_B_LINE"
if [ -z "$IDENTITY_LINE" ]; then
  echo "  FAIL the --model path never reached the skinning upload"; fail=1
  FAIL_LINES+=("--model path did not upload bone matrices")
fi
assert_eq "the --model path uploads identity matrices with skinning on" "$(field "$IDENTITY_LINE" source)" "identity"
assert_eq "the --model path enables the skinning branch" "$(field "$IDENTITY_LINE" skinning)" "1"
assert_eq "the --model path uploads every bone" "$(field "$IDENTITY_LINE" uploaded)" "$(field "$IDENTITY_LINE" bones)"
assert_eq "a supplied sequence produces an animated pose" "$(field "$ANIMATED_LINE" source)" "animated"
assert_eq "the animated run uploads every bone" "$(field "$ANIMATED_LINE" uploaded)" "$(field "$ANIMATED_LINE" bones)"
# The claim itself. Identity skin matrices through the skinning branch versus no
# skinning branch at all. It is NOT a byte-exact claim and never was: the skinning
# shader evaluates sum(w_i * M_i * v) with normalized weights, a different float
# expression than the passthrough, so a sample sitting on a surface boundary can
# be owned by the other triangle and take the neighbouring surface's colour. On
# this frame that is ONE pixel of 860,784, delta 59 of 255, on a measured edge
# (T39). The previous bound asked a raster tie to be a bit-identical rendering,
# which it cannot be, so it was replaced -- not widened -- by the rule the claim
# actually needs:
#   * outside the measured edge mask the rounding allowance still holds, strictly
#     (max channel delta <= 1; measured 0);
#   * every changed pixel lies inside the edge mask (measured 1 of 1);
#   * changed pixels are capped at 4 (measured 1; the real-pose control below and
#     the --mutation-identity case change thousands);
#   * the whole-frame max delta has a ceiling of 128, so a large move cannot hide
#     inside the mask (measured 59, the two surface colours at the edge).
# The mask is derived, not listed: pixels whose 4-neighbour maximum per-channel
# gradient in the control frame reaches 32. On this frame that selects 18,446 px
# = 2.14% of the frame (gradient population: 823,210 px at 0, 2,813 at 1, up to
# 222 at the strongest face boundary; the residual pixel measures 59). The
# threshold is a stated parameter rather than a natural break, so the measured
# population is printed on every run and bounded 1,000..86,078 -- an empty or
# whole-frame mask would make containment vacuous.
silhouette_diff "identity skinning changes nothing outside the measured edge mask" \
  "$MODEL_IDENTITY" "$MODEL_CONTROL" "$MODEL_CONTROL" \
  --edge-threshold 32 --max-interior-delta 1 --max-changed 4 \
  --max-global-delta 128 --min-mask 1000 --max-mask 86078
# The control that catches a real wrong pose: the same rule applied to the
# identity-vs-animated pair. A sampled sequence really moves the geometry (the
# positive half is asserted below), so the rule must REJECT this pair -- it
# changes interior pixels and thousands of edge pixels. The requirement is
# exactly rc=1 (a bound failed): rc=2 would mean the comparison could not be
# read, which is a gate failure, not a rejection by the rule.
SEM_POSE=$("$PY" bmp-silhouette-diff.py "$MODEL_IDENTITY" "$MODEL_ANIMATED" \
  --edge-frame "$MODEL_IDENTITY" --edge-threshold 32 --max-interior-delta 1 \
  --max-changed 4 --max-global-delta 128 --min-mask 1000 --max-mask 86078 2>&1)
SEM_POSE_RC=$?
echo "  $SEM_POSE"
if [ "$SEM_POSE_RC" -eq 1 ]; then
  echo "  OK   the silhouette rule rejects a real pose change (identity vs animated)"
else
  echo "  FAIL the silhouette rule did not reject a real pose change (rc=$SEM_POSE_RC)"
  fail=1; FAIL_LINES+=("the silhouette rule did not reject a real pose change: $SEM_POSE")
fi
# And the positive control: supplying a sequence must move the geometry, by a
# margin no rounding can produce.
bmp_diff "a sampled sequence moves the geometry" \
  "$MODEL_IDENTITY" "$MODEL_ANIMATED" --min-diff-percent 1 --min-delta 100
# The cursor-versus-picture pair, on a sequence that really has frames to move
# through. scout.mdl cannot carry this: its only two bound sequences are one
# frame each, so "a sequence moved the geometry" is satisfiable there by a
# static pose block -- the exact confusion this pair removes.
WAVE_A_FRAME=$(field "$WAVE_A_LINE" frame)
WAVE_B_FRAME=$(field "$WAVE_B_LINE" frame)
assert_eq "the witness sequence has $WAVE_FRAMES frames" "${WAVE_A_FRAME##*/}" "$WAVE_FRAMES"
assert_ne "the two ticks sample different frames" "$WAVE_A_FRAME" "$WAVE_B_FRAME"
bmp_diff "the sampled pose follows the tick" \
  "$WAVE_A" "$WAVE_B" --min-diff-percent 1 --min-delta 100
# Frame index modulo the frame count, not clamped: tick 0 and tick 100 must be
# the same picture. A sampler that clamped would render frame 99 at tick 100 and
# this would go red.
bmp_diff "the frame index wraps at the frame count" \
  "$WAVE_ZERO" "$WAVE_FULL" --max-delta 0

echo
echo "=== 5/6 the probe's bind-pose mutation is discriminated ==="
# The mutation replaces the sampled pose with the bind pose. To mean anything it
# needs a model whose pose actually moves, and a *pair* of runs: the normal run
# must read motion, the mutated run must read none. Reading only the mutated run
# is how this was previously reported as "caught" while both sides were zero --
# a mutation that moved a reading which never had a value.
PROBE_NORMAL=$(timeout "$RUN_TIMEOUT" "$PROBE" --tf-root "$TF" --demo "$DEMO" \
  --models "$WITNESS_FILTER" 2>/dev/null | tail -1)
PROBE_MUTATED=$(timeout "$RUN_TIMEOUT" "$PROBE" --tf-root "$TF" --demo "$DEMO" \
  --models "$WITNESS_FILTER" --mutation bindpose 2>/dev/null | tail -1)
echo "  normal : $PROBE_NORMAL"
echo "  mutated: $PROBE_MUTATED"
NORMAL_MOTION=$(jfield "$PROBE_NORMAL" motionWorst)
NORMAL_ENTITY=$(jfield "$PROBE_NORMAL" motionEntity)
NORMAL_COMPARED=$(jfield "$PROBE_NORMAL" entitiesCompared)
MUTATED_MOVED=$(jfield "$PROBE_MUTATED" mutationMoved)
MUTATED_MOTION=$(jfield "$PROBE_MUTATED" motionWorst)
MUTATED_COMPARED=$(jfield "$PROBE_MUTATED" entitiesCompared)
assert_gt "the normal run reads a pose change" "${NORMAL_MOTION:-0}" 0
assert_ge "the normal run compared at least one entity" "${NORMAL_COMPARED:-0}" 1
assert_eq "the mutated run reports the mutation as moved" "${MUTATED_MOVED:-none}" "true"
assert_ge "the mutated run compared at least one entity" "${MUTATED_COMPARED:-0}" 1
assert_eq "the mutated run reads no pose change" "${MUTATED_MOTION:-none}" "0"
if [ -n "$NORMAL_ENTITY" ]; then
  echo "  OK   the moving witness is $NORMAL_ENTITY"
else
  echo "  FAIL the normal run named no moving entity"; fail=1
  FAIL_LINES+=("probe named no moving entity")
fi

echo
echo "=== 6/6 the probe's matrix-order mutation is caught ==="
# The order mutation is the one the product readings cannot see (a wrong-but-live
# pose still advances), and the probe is the instrument that owns it: skinLands
# is the distance between a skinned bone and that bone's animated origin, so
# swapping the multiply order moves it from ~1e-06 to ~36.
PROBE_ORDER=$(timeout "$RUN_TIMEOUT" "$PROBE" --tf-root "$TF" --demo "$DEMO" --ticks 6 --mutation order 2>/dev/null | tail -1)
PROBE_LANDS=$(jfield "$PROBE_ORDER" skinLands)
echo "probe skinLands under --mutation order = ${PROBE_LANDS:-<none>}"
assert_gt "the order mutation moves skinLands off zero" "${PROBE_LANDS:-0}" 1.0

# The summary and the failure text are the two things worth keeping from a run:
# the readings a reader can check, and -- when something failed -- the exact
# line that failed rather than a re-run. The captured frames stay as frozen
# witnesses too; the traces and metrics CSVs do not (they are machine output,
# reproducible above, and would churn the committed tree on every run).
{
  echo "# weapon-animation-check.sh readings ($(date -u +%Y-%m-%dT%H:%M:%SZ))"
  echo "# demo: $DEMO"
  echo "# model: $MODEL sequence=$MODEL_SEQUENCE tick=$MODEL_TICK"
  echo "# mutation: ${MUTATION_TARGET:-none}"
  echo "ticks: ${TICKS[*]}"
  for index in 0 1 2; do
    echo "tick ${TICKS[$index]}: ${ANIM_LINES[$index]:-<none>}"
  done
  echo "model-identity: ${IDENTITY_LINE:-<none>}"
  echo "model-control: ${CONTROL_LINE:-<none>}"
  echo "model-animated: ${ANIMATED_LINE:-<none>}"
  echo "wave-a (tick $WAVE_TICK_A): ${WAVE_A_LINE:-<none>}"
  echo "wave-b (tick $WAVE_TICK_B): ${WAVE_B_LINE:-<none>}"
  echo "wave-full (tick $WAVE_FRAMES): ${WAVE_FULL_LINE:-<none>}"
  echo "probe-normal: ${PROBE_NORMAL:-<none>}"
  echo "probe-mutated-bindpose: ${PROBE_MUTATED:-<none>}"
  echo "probe-order: ${PROBE_ORDER:-<none>}"
  echo "distinct-frames: $DISTINCT/${#HASHES[@]}"
  echo "failures: $fail"
} > "$RUN_DIR/readings.txt"

if [ "$MUTATION" -eq 1 ]; then
  cp "$BACKUP" "$SOURCE"
  bash build-target.sh tf2_demo_native > "$RUN_DIR/mutation-restore-build.txt" 2>&1
  echo "restore build rc=$?"
  if [ "$fail" -eq 1 ]; then
    # Named for the mutation that produced it. Both mutations write into the same
    # default evidence directory when this script is run by hand, and a single
    # `mutation-failure.txt` meant the second run silently replaced the first --
    # so the committed record of what the entity mutation caught depended on
    # which mutation happened to run last.
    {
      echo "# the mutation was caught: these assertions went red with $MUTATION_TARGET mutated"
      printf '%s\n' "${FAIL_LINES[@]}"
    } > "$OUT/mutation-failure-$MUTATION_TARGET.txt"
    # The identity mutation exists to prove the silhouette rule is load-bearing, so
    # "something went red" is not enough: the named rule must be one of the red
    # assertions. A mutation caught only by an unrelated reading would leave the
    # rule untested while the run still said PASS.
    if [ "$MUTATION_TARGET" = "identity" ] || [ "$MUTATION_TARGET" = "pose" ]; then
      CAUGHT_BY_RULE=0
      for entry in "${FAIL_LINES[@]}"; do
        case "$entry" in
          "identity skinning changes nothing outside the measured edge mask"*) CAUGHT_BY_RULE=1 ;;
        esac
      done
      if [ "$CAUGHT_BY_RULE" -ne 1 ]; then
        echo "MUTATION-CAUGHT=FAIL (the identity mutation went red, but not on the silhouette rule)"
        exit 1
      fi
    fi
    echo "MUTATION-CAUGHT=PASS ($MUTATION_TARGET mutation turns this step red)"
    exit 0
  fi
  echo "MUTATION-CAUGHT=FAIL (the checks stayed green with the $MUTATION_TARGET mutation applied)"
  exit 1
fi

if [ "$fail" -eq 0 ]; then
  echo "WEAPON-ANIMATION=PASS"
else
  echo "WEAPON-ANIMATION=FAIL"
fi
exit "$fail"
