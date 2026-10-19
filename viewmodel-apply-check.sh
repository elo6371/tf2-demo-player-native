#!/usr/bin/env bash
# viewmodel-apply-check.sh -- T18: the applied ViewModel pose is a picture, not a
# diagnostic reading.
#
# THE QUESTION
#   T13 proved the pose is applied (`skinning=1 vmAnimApplied=1`) and recorded, in
#   the same report, that the frame is a full-screen close-up of featureless
#   planes. Both statements were true at once, which means every reading this
#   project had about ViewModel skinning was a statement about the *branch*, not
#   about the geometry it produced.
#
#   T18's step 2 asks which input moved: camera matrix, model origin, FOV, hand
#   pose, sequence time, or attachment. This gate answers it with numbers that are
#   taken from the product's own frame, not re-derived:
#
#     the camera and the framing    identical strings on the bind and applied runs
#     the model origin              bind/posed box centre, from the packed vertices
#     the FOV and the near/far      published by the renderer's placement solve
#     the hand pose and attachment  the state the draw used
#     the sequence and its time     the effective sequence and the sampled frame
#     the geometry                  how far the pose moves each vertex, and where
#
# WHY THE MUTATIONS ARE THE POINT
#   The defect this round found was invisible to every counter in the tree. A
#   transposed bone-matrix read leaves `skinning=1`, `boneTableCoverage=58/58` and
#   `vmAnimApplied=1` all correct; it only moves geometry. And the instrument that
#   was supposed to prove the skinning path -- identity matrices -- cannot catch
#   it, because an identity matrix is its own transpose. So this gate judges the
#   PICTURE (inside the ViewModel's own coverage) and then proves that judgement
#   discriminates, by reintroducing the transposed read.
#
# EVERY PER-RUN FILE CARRIES $RUN_TAG, for the reason recorded in
# viewmodel-range-check.sh: a refused delete leaves the previous run's trace in
# place and the assertions read bytes they did not produce.
#
# Usage: bash viewmodel-apply-check.sh [--mutation]
# Exit:  0 = every assertion held (with --mutation: every defect was caught and
#            every forward assertion still held), 1 = it did not,
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
MODEL="models/weapons/c_models/c_sniper_arms.mdl"
VM_MODEL="models/weapons/v_models/v_scattergun_scout.mdl"
INPUTS=evidence/viewmodel/animation-inputs.txt
TICK=0
RUN_TIMEOUT=300
# The sequence the entity's own model can pose. Read from the gate's pinned
# evidence rather than written here twice: `viewmodel-animation-check.sh` derives
# it with the model probe, and a different model would move it.
POSE_SEQUENCE=1
RUN_TAG="$(date +%Y%m%d%H%M%S)-$$"
EVIDENCE="${VIEWMODEL_APPLY_EVIDENCE_DIR:-evidence/viewmodel/apply}"
SCRATCH=.scratch
RUN_DIR="$SCRATCH/viewmodel-apply-$RUN_TAG"
mkdir -p "$EVIDENCE" "$SCRATCH" "$RUN_DIR"
ROOT=$(cygpath -m "$(pwd)" 2>/dev/null || pwd)

TF=${TF%$'\r'}
MODEL=${MODEL%$'\r'}
VM_MODEL=${VM_MODEL%$'\r'}
INPUTS=${INPUTS%$'\r'}

LOCK_DIR=$SCRATCH/viewmodel-apply.lock
release_lock() { rm -f "$LOCK_DIR/pid" 2>/dev/null; rmdir "$LOCK_DIR" 2>/dev/null; }
# The lock records its owner. A run that is KILLED cannot remove it -- bash does
# not run the EXIT trap for a signal it has no handler for -- so a stale lock used
# to refuse every later run with "another run owns it", which reads like a
# concurrency problem and was really a corpse.
if [ -d "$LOCK_DIR" ]; then
  lock_owner=$(cat "$LOCK_DIR/pid" 2>/dev/null)
  if [ -n "$lock_owner" ] && kill -0 "$lock_owner" 2>/dev/null; then
    echo "VIEWMODEL-APPLY=FAIL (pid $lock_owner is running and owns $LOCK_DIR)"
    exit 2
  fi
  echo "  ..   removing the lock left by pid ${lock_owner:-<unknown>}; that run is gone"
  release_lock
fi
if ! mkdir "$LOCK_DIR" 2>/dev/null; then
  echo "VIEWMODEL-APPLY=FAIL (another viewmodel-apply-check run owns $LOCK_DIR)"
  exit 2
fi
echo $$ > "$LOCK_DIR/pid"

# The same kill leaves the MUTATED FILE in place: the backup beside it is the only
# record that the tree is not the tree. Measured 2026-10-19 -- an interrupted
# --mutation run left `dot(v, c0)` in native_renderer.cpp, and the next run would
# have captured "the applied frame" of a transposed read and filed it as evidence.
# The sweep runs before any capture, and it rebuilds when it had to put a file
# back: the executable left behind was linked from the mutated source, so it
# cannot be the binary these frames are attributed to.
MUTATION_SOURCES="native/src/main.cpp native/src/native_renderer.cpp native/src/viewmodel.cpp"
ORPHANS_RESTORED=0
for source in $MUTATION_SOURCES; do
  for candidate in "$SCRATCH"/viewmodel-apply-*/"$(basename "$source")".backup; do
    [ -f "$candidate" ] || continue
    if cmp -s "$candidate" "$source"; then
      echo "  ..   orphaned backup of $source is already identical; removing it"
    else
      cp "$candidate" "$source"
      if cmp -s "$candidate" "$source"; then
        echo "  ..   restored $source from an interrupted run ($(sha256sum "$source" | cut -d' ' -f1))"
        ORPHANS_RESTORED=$((ORPHANS_RESTORED + 1))
      else
        echo "VIEWMODEL-APPLY=FAIL (could not restore $source from $candidate)"
        release_lock
        exit 2
      fi
    fi
    rm -f "$candidate"
  done
done
if [ "$ORPHANS_RESTORED" -gt 0 ]; then
  echo "  ..   $ORPHANS_RESTORED file(s) came back from a killed run; rebuilding before any capture"
  if ! bash build-target.sh tf2_demo_native > "$SCRATCH/orphan-rebuild.log" 2>&1; then
    echo "VIEWMODEL-APPLY=FAIL (the tree did not rebuild after the orphan restore; see $SCRATCH/orphan-rebuild.log)"
    release_lock
    exit 2
  fi
  echo "  ..   rebuilt"
fi

fail=0
OK_COUNT=0
FAIL_COUNT=0
FAIL_LINES=()
MUTATION_TOTAL=0
MUTATION_CATCHES=0

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
assert_contains() { # assert_contains <label> <haystack> <needle>
  case "${2:-}" in
    *"$3"*) pass "$1" ;;
    *) fail_line "$1 (the line does not contain '$3')" ;;
  esac
}

# The last value of `key=` on the last line matching $2 in trace $1.
reading() { # reading <trace> <pattern> <key>
  tr -d '\r' < "$1" 2>/dev/null | grep -a "$2" | tail -1 \
    | grep -o "$3=[^ ]*" | tail -1 | cut -d= -f2-
}
# The whole line, timestamp stripped, so two runs can be compared as text.
reading_line() { # reading_line <trace> <pattern>
  tr -d '\r' < "$1" 2>/dev/null | grep -a "$2" | tail -1 | sed 's/^[0-9.]\{1,\} //'
}

# ---------------------------------------------------------------------------
# The mutation harness
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

# A patch applied as TEXT, not as a line-ending convention. The three mutated
# files do not agree with each other: main.cpp and viewmodel.cpp are CRLF,
# native_renderer.cpp is LF. The first run of this gate wrote all three patches
# with LF and reported the mismatch as "m3 did not apply ... pattern not found",
# which reads like a stale pattern and was really a stale assumption about the
# file.
#
# The work is done by patch-source.py, which reads the document from stdin and
# the file from argv. It cannot be a `python - <<'PY'` block with the document on
# stdin as well: with `-` the heredoc IS the program, so the document is consumed
# by the interpreter before the first character of it can be read -- the first
# version of this helper failed with "the patch document has no %% separator
# line" for exactly that reason.
#
# stdin: the needle, a %% line on its own, then the replacement. Both sides are
# matched with the file's own newline, so a snippet can be pasted from the source
# without knowing which convention that file uses.
patch_source() { # patch_source <file>
  python patch-source.py "$1"
}

# ---------------------------------------------------------------------------
# Section 0 -- pinned inputs
# ---------------------------------------------------------------------------
echo "=== 0/6 the pinned recording ==="
if [ ! -x "$EXE" ]; then
  echo "FATAL: missing input: $EXE (build it first)"
  release_lock
  exit 2
fi
if [ ! -f "$INPUTS" ]; then
  echo "FATAL: missing input: $INPUTS"
  release_lock
  exit 2
fi
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
  echo "VIEWMODEL-APPLY=FAIL (no recording)"
  exit 1
fi
# The models are NOT checked for existence on disk: both live inside the shipped
# VPKs, so "the file is here" is the wrong question and a loose-file test would
# fail on a correct install (T15's lookup chain is what resolves them). Whether
# they resolve is asserted in section 2, from the product's own `drawnModel`.
#
# The executable hash is recorded so a reader can say which binary drew these
# frames. It is deliberately NOT used to judge the restoration at the end of
# --mutation: the linker stamps a fresh signature into every link, so three
# consecutive builds of one unchanged tree produce three different hashes
# (measured 2026-10-19: 8e284755 / d599d33a / d6c787b1). The frame is the thing
# that is reproducible, and the restoration is judged on the frame instead.
FORWARD_EXE_HASH=$(sha256sum "$EXE" | cut -d' ' -f1)
pass "executable hash recorded ($FORWARD_EXE_HASH)"

# ---------------------------------------------------------------------------
# Section 1 -- six captures from one executable
# ---------------------------------------------------------------------------
# One function, six call sites: every frame in this gate differs from every other
# by the flags below and by nothing else. A capture helper that let one call site
# add `--viewmodel-fov` would produce frames this gate could not compare.
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
  LAST_RC=$?
  local size
  size=$(stat -c%s "$RUN_DIR/$name.bmp" 2>/dev/null || echo 0)
  echo "  $name rc=$LAST_RC bmp=$size"
  [ "$LAST_RC" -eq 0 ] || fail_line "$name exited $LAST_RC"
  [ "$size" -gt 0 ] || fail_line "$name produced no frame"
}

# This host is shared with other task worktrees, and one of them may be running its
# own product captures while this gate runs. viewmodel-range-check.sh measured the
# question and its answer is the policy here too: a PAUSED capture is not perturbed
# by a live neighbour (two paused captures were byte-identical with four product
# processes live), while a PLAYING capture is (38.7% of the pixels differed). Every
# capture in this gate is `--start-paused` at a fixed tick, so the overlap costs
# wall-clock time and not correctness -- but it is recorded rather than assumed,
# and section 5's repeat hash is what would catch it if the assumption broke.
OTHER_PRODUCT=$(ps -efW 2>/dev/null | grep "[t]f2_demo_native.exe" | head -1)
HOST_OVERLAP=0
if [ -n "$OTHER_PRODUCT" ]; then
  HOST_OVERLAP=1
  echo "  ..   another product run is live; these captures are paused, so the reading is unaffected:"
  echo "  ..   $OTHER_PRODUCT"
fi

echo
echo "=== 1/6 six captures: bind, applied, repeat, world-only, flat pair ==="
if [ "$MUTATION" -eq 1 ]; then
  # The reference frames the defects are judged against, captured from the tree as
  # it was before any mutation. A defect that only changes how a POSE is skinned
  # cannot change either of these: the bind run does not skin, and the world-only
  # run draws no ViewModel at all.
  echo "  ..   --mutation: capturing the bind and world-only references first"
  run_product bind  --viewmodel "$VM_MODEL" --viewmodel-sequence "$POSE_SEQUENCE"
  run_product world --viewmodel-fov 90
else
  run_product bind    --viewmodel "$VM_MODEL" --viewmodel-sequence "$POSE_SEQUENCE"
  run_product apply   --viewmodel "$VM_MODEL" --viewmodel-sequence "$POSE_SEQUENCE" --viewmodel-apply-animation
  run_product repeat  --viewmodel "$VM_MODEL" --viewmodel-sequence "$POSE_SEQUENCE" --viewmodel-apply-animation
  # The control the mask is built from: the same tick with no ViewModel pass at
  # all. A pixel where this frame and the bind frame differ is a pixel the
  # ViewModel covers.
  run_product world   --viewmodel-fov 90
  run_product flat-bind  --viewmodel "$VM_MODEL" --viewmodel-sequence "$POSE_SEQUENCE" --viewmodel-flat-color
  run_product flat-apply --viewmodel "$VM_MODEL" --viewmodel-sequence "$POSE_SEQUENCE" --viewmodel-flat-color --viewmodel-apply-animation
  for pair in "bind:$RUN_DIR/bind.bmp" "apply:$RUN_DIR/apply.bmp" "repeat:$RUN_DIR/repeat.bmp" \
              "world:$RUN_DIR/world.bmp" "flat-bind:$RUN_DIR/flat-bind.bmp" "flat-apply:$RUN_DIR/flat-apply.bmp"; do
    label=${pair%%:*}; path=${pair#*:}
    if [ -s "$path" ]; then pass "$label frame is non-empty"; else fail_line "$label frame is empty"; fi
  done
fi

# ---------------------------------------------------------------------------
# Section 2 -- the product's own readings
# ---------------------------------------------------------------------------
echo
echo "=== 2/6 the contract and the effect, on the product's lines ==="
if [ "$MUTATION" -eq 0 ]; then
  apply_pass="$RUN_DIR/apply.trace"
  bind_pass="$RUN_DIR/bind.trace"
  assert_eq "applied: vmAnimInput" "$(reading "$apply_pass" 'viewmodel pass order' vmAnimInput)" "ok"
  assert_eq "applied: vmAnimApplied" "$(reading "$apply_pass" 'viewmodel pass order' vmAnimApplied)" "1"
  assert_eq "applied: skinning" "$(reading "$apply_pass" 'viewmodel pass order' skinning)" "1"
  assert_eq "applied: bone table covers the skeleton" \
    "$(reading "$apply_pass" 'viewmodel pass order' boneTableCoverage)" "58/58"
  assert_eq "applied: the effect reading names the upload" \
    "$(reading "$apply_pass" 'viewmodel pass order' animationInput)" "bones:58"
  assert_eq "applied: the sequence is inside the drawn model's table" \
    "$(reading "$apply_pass" 'viewmodel pass order' sequenceInRange)" "1"
  assert_eq "bind: no pose is applied" "$(reading "$bind_pass" 'viewmodel pass order' vmAnimApplied)" "0"
  assert_eq "bind: the pass does not skin" "$(reading "$bind_pass" 'viewmodel pass order' skinning)" "0"

  # The model resolved through the shipped VPKs, and the pass drew both of its
  # ranges. Without these two, "the pose is applied" could be a statement about a
  # fallback model or about one of the two pieces.
  assert_eq "applied: the pass draws the entity's own arms model" \
    "$(reading "$apply_pass" 'viewmodel pass order' drawnModel)" "$MODEL"
  assert_eq "applied: the draw issued" "$(reading "$apply_pass" 'viewmodel pass order' drawCalls)" "1"
  assert_eq "applied: both ranges are drawn" "$(reading "$apply_pass" 'viewmodel pass order' ranges)" "2"
  assert_eq "applied: both ranges bound a material slot" \
    "$(reading "$apply_pass" 'viewmodel pass order' boundSlots)" "2"

  # The sequence actually sampled, next to the one the recording carries. T18's
  # step 2 asks for the sequence time; before this round the line printed the
  # pre-override value in `sequence=`, so an overridden run read
  # `sequence=29 sequenceOverride=1` -- two fields that contradict each other.
  assert_eq "applied: effective sequence" \
    "$(reading "$apply_pass" 'scene: vmAnim entity=' sequence)" "$POSE_SEQUENCE"
  assert_eq "applied: the recording's own sequence is still reported" \
    "$(reading "$apply_pass" 'scene: vmAnim entity=' entitySequence)" "29"
  assert_eq "applied: the override is reported" \
    "$(reading "$apply_pass" 'scene: vmAnim entity=' sequenceOverride)" "$POSE_SEQUENCE"
  assert_eq "applied: the pose was sampled from a one-frame sequence" \
    "$(reading "$apply_pass" 'scene: vmAnim entity=' frame)" "0/1"
fi

# ---------------------------------------------------------------------------
# Section 3 -- the camera and the framing did not move
# ---------------------------------------------------------------------------
echo
echo "=== 3/6 the camera, FOV, hand pose and attachment are the same on both runs ==="
if [ "$MUTATION" -eq 0 ]; then
  bind_frame_line=$(reading_line "$RUN_DIR/bind.trace" 'gpu: viewmodel frame eye=')
  apply_frame_line=$(reading_line "$RUN_DIR/apply.trace" 'gpu: viewmodel frame eye=')
  bind_centre_line=$(reading_line "$RUN_DIR/bind.trace" 'gpu: viewmodel centre viewSpace=')
  apply_centre_line=$(reading_line "$RUN_DIR/apply.trace" 'gpu: viewmodel centre viewSpace=')
  if [ -z "$bind_frame_line" ] || [ -z "$apply_frame_line" ]; then
    fail_line "the frame reading is missing from one of the traces"
  elif [ "$bind_frame_line" = "$apply_frame_line" ]; then
    pass "eye / forward / placement / fov / planes / hand / attachment are identical"
  else
    fail_line "the frame reading moved:
       bind:  $bind_frame_line
       applied: $apply_frame_line"
  fi
  if [ -n "$bind_centre_line" ] && [ "$bind_centre_line" = "$apply_centre_line" ]; then
    pass "the placement solve produced the same mesh centre clip position"
  else
    fail_line "the mesh centre clip position moved between bind and applied:
       bind:  ${bind_centre_line:-<missing>}
       applied: ${apply_centre_line:-<missing>}"
  fi
  # The numbers T18 asks for, asserted individually so a reader does not have to
  # diff two long lines to see which one changed.
  assert_eq "applied: FOV" "$(reading "$RUN_DIR/apply.trace" 'gpu: viewmodel frame eye=' fov)" "90.00"
  assert_eq "applied: hand matrix is the right-hand identity" \
    "$(reading "$RUN_DIR/apply.trace" 'gpu: viewmodel frame eye=' hand)" "(1,0,0,0,0,1,0,0,0,0,1,0)"
  assert_eq "applied: attachment" \
    "$(reading "$RUN_DIR/apply.trace" 'gpu: viewmodel frame eye=' attachment)" "weapon_bone"
  # distance = radius * 2.2 by construction, so the pair is one number and a rule.
  radius=$(reading "$RUN_DIR/apply.trace" 'gpu: viewmodel frame eye=' radius)
  distance=$(reading "$RUN_DIR/apply.trace" 'gpu: viewmodel frame eye=' distance)
  if [ -n "$radius" ] && [ -n "$distance" ]; then
    if awk -v r="$radius" -v d="$distance" 'BEGIN { exit !(d > 2.1*r && d < 2.3*r) }'; then
      pass "the placement distance is 2.2x the model radius ($distance vs $radius)"
    else
      fail_line "distance $distance is not 2.2x radius $radius"
    fi
  else
    fail_line "the framing numbers are missing"
  fi
fi

# ---------------------------------------------------------------------------
# Section 4 -- where the pose actually puts the geometry
# ---------------------------------------------------------------------------
# The reading that separated "the framing is wrong" from "the geometry is wrong".
# It is measured on the vertices that were uploaded and the matrices that were
# uploaded, so it describes this draw. Bounds are the model's own size, not tuned:
# a pose that moves a vertex further than the model's radius, or that moves the
# box centre by more than the radius, is tearing the mesh rather than posing it.
echo
echo "=== 4/6 the pose extent, in model space ==="
if [ "$MUTATION" -eq 0 ]; then
  extent_line="$RUN_DIR/apply.trace"
  bind_radius=$(reading "$extent_line" 'pose extent' bindRadius)
  posed_radius=$(reading "$extent_line" 'pose extent' posedRadius)
  shift=$(reading "$extent_line" 'pose extent' centerShift)
  max_disp=$(reading "$extent_line" 'pose extent' maxDisplacement)
  non_finite=$(reading "$extent_line" 'pose extent' nonFinite)
  posed_vertices=$(reading "$extent_line" 'pose extent' posedVertices)
  vertices=$(reading "$extent_line" 'pose extent' vertices)
  assert_eq "every vertex carried an in-range skin weight" "$posed_vertices" "$vertices"
  assert_eq "no posed vertex is non-finite" "$non_finite" "0"
  assert_eq "the extent reading is valid" "$(reading "$extent_line" 'pose extent' valid)" "1"
  assert_le "the pose moves the box centre by at most the model radius" "$shift" "$bind_radius"
  assert_le "the pose moves no vertex further than the model radius" "$max_disp" "$bind_radius"
  if [ -n "$bind_radius" ] && [ -n "$posed_radius" ]; then
    if awk -v b="$bind_radius" -v p="$posed_radius" 'BEGIN { exit !(p >= 0.5*b && p <= 2.0*b) }'; then
      pass "the posed box is within half to twice the bind box ($posed_radius vs $bind_radius)"
    else
      fail_line "posed radius $posed_radius is not within 0.5x..2x of bind $bind_radius"
    fi
  else
    fail_line "the extent radii are missing"
  fi
fi

# ---------------------------------------------------------------------------
# Section 5 -- the picture
# ---------------------------------------------------------------------------
echo
echo "=== 5/6 the frames: applied differs, repeat is identical, both stay on the ViewModel ==="
CONTAIN_OUTSIDE_MAX=64   # measured 2 on the intact build; see the evidence file
CONTAIN_CHANGED_MIN=1000 # measured 10097 (textured) / 5586 (flat); 10% of the lower

# The mask tool answers one question: does the difference between two frames stay
# inside the pixels the ViewModel covers? It is asked twice with OPPOSITE
# expectations -- the intact tree must stay inside, the transposed read must leave
# -- so the measurement and the judgement are separate. The first version folded
# them into one function, and the m2 catch (outside=169981 against a 64 bound, the
# most convincing reading this gate produced) was reported as two failures of the
# gate itself while MUTATION-CAUGHT already read PASS.
mask_measure() { # mask_measure <a> <b> <maskA> <maskB>
  local out fields
  out=$(python bmp-diff-mask.py "$1" "$2" --mask "$3" "$4" \
    --max-outside "$CONTAIN_OUTSIDE_MAX" --min-changed "$CONTAIN_CHANGED_MIN" 2>&1 | tail -1)
  REPORT_LINE=$out
  # Only the field section is parsed. On a FAIL the tool repeats the offending
  # number inside its `reason=` clause -- `... outside=169981 ... status=FAIL
  # reason=outside=169981 exceeds 64` -- so a bare `grep -o 'outside=[0-9]*'`
  # returns the number twice, joined by a newline. That two-line string then went
  # into `awk -v a=...`, which read it as 0 and reported "the defect did not
  # exceed 64" about a defect that had exceeded it by a factor of 2600. The cut at
  # ` reason=` is what makes each number appear exactly once; `head -1` keeps the
  # field even if a future verdict adds a second clause.
  fields=${out%% reason=*}
  REPORT_CHANGED=$(printf '%s' "$fields" | grep -o 'changed=[0-9]*' | head -1 | cut -d= -f2)
  REPORT_OUTSIDE=$(printf '%s' "$fields" | grep -o 'outside=[0-9]*' | head -1 | cut -d= -f2)
  REPORT_MASK_PIXELS=$(printf '%s' "$fields" | grep -o 'mask_pixels=[0-9]*' | head -1 | cut -d= -f2)
  REPORT_STATUS=$(printf '%s' "$fields" | grep -o 'status=[A-Z]*' | head -1 | cut -d= -f2)
}
mask_check() { # mask_check <label> <a> <b> <maskA> <maskB>: the pair MUST stay inside
  local label="$1"; shift
  mask_measure "$@"
  if [ "$REPORT_STATUS" = "PASS" ]; then
    pass "$label: $REPORT_CHANGED px changed, $REPORT_OUTSIDE outside a $REPORT_MASK_PIXELS px ViewModel mask"
  else
    fail_line "$label: ${REPORT_LINE:-the mask tool produced no verdict}"
  fi
}
mask_expect_red() { # mask_expect_red <label> <a> <b> <maskA> <maskB>: the pair MUST leave
  local label="$1"; shift
  mask_measure "$@"
  if [ -z "${REPORT_OUTSIDE:-}" ]; then
    fail_line "$label: the mask tool produced no verdict"
  elif ! awk -v a="$REPORT_OUTSIDE" -v b="$CONTAIN_OUTSIDE_MAX" 'BEGIN { exit !(a > b) }'; then
    fail_line "$label: $REPORT_OUTSIDE px outside, the defect did not exceed $CONTAIN_OUTSIDE_MAX"
  elif ! awk -v a="$REPORT_CHANGED" -v b="$CONTAIN_CHANGED_MIN" 'BEGIN { exit !(a > b) }'; then
    fail_line "$label: $REPORT_CHANGED px changed, the defect did not exceed $CONTAIN_CHANGED_MIN"
  else
    pass "$label: $REPORT_CHANGED px changed, $REPORT_OUTSIDE outside a $REPORT_MASK_PIXELS px ViewModel mask"
    MUTATION_CATCHES=$((MUTATION_CATCHES + 1))
  fi
}

if [ "$MUTATION" -eq 0 ]; then
  # Determinism first: two runs of the same command must agree byte for byte. A
  # pose captured from a playing loop is not a fixed input; these are paused.
  apply_hash=$(sha256sum "$RUN_DIR/apply.bmp" | cut -d' ' -f1)
  repeat_hash=$(sha256sum "$RUN_DIR/repeat.bmp" | cut -d' ' -f1)
  assert_eq "the applied frame repeats byte for byte" "$repeat_hash" "$apply_hash"
  bind_hash=$(sha256sum "$RUN_DIR/bind.bmp" | cut -d' ' -f1)
  if [ "$bind_hash" = "$apply_hash" ]; then
    fail_line "the applied frame is byte-identical to the bind frame: no pose is in the picture"
  else
    pass "the applied frame differs from the bind frame ($apply_hash vs $bind_hash)"
  fi
  mask_check "textured bind/applied" "$RUN_DIR/bind.bmp" "$RUN_DIR/apply.bmp" \
    "$RUN_DIR/world.bmp" "$RUN_DIR/bind.bmp"
  mask_check "flat bind/applied" "$RUN_DIR/flat-bind.bmp" "$RUN_DIR/flat-apply.bmp" \
    "$RUN_DIR/world.bmp" "$RUN_DIR/flat-bind.bmp"
fi

# ---------------------------------------------------------------------------
# Section 6 -- mutations
# ---------------------------------------------------------------------------
echo
echo "=== 6/6 mutations: each defect must move a reading this gate reads ==="
expect_red() { # expect_red <label> <actual> <value the defect produces> <value the gate pins>
  if [ "${2:-<none>}" != "$3" ]; then
    fail_line "$1 = ${2:-<none>}, expected the defect to read $3"
  elif [ "$3" = "$4" ]; then
    fail_line "$1: the defect reads $3, which is also the pinned value; the assertion cannot discriminate"
  else
    pass "$1 = $3 (the defect reproduces; the gate pins $4)"
  fi
}

if [ "$MUTATION" -eq 1 ]; then
  # --- m1: the pose is resolved but never uploaded ---------------------------
  echo "--- m1 skinning-skipped: --viewmodel-apply-animation resolves a pose and drops it ---"
  MUTATION_TOTAL=$((MUTATION_TOTAL + 1))
  apply_mutation native/src/main.cpp "skinning-skipped"
  # The upload call is handed an empty table instead of the matrices that were
  # just resolved. `uploadViewModelBones` refuses an empty table before it touches
  # the GPU (native_renderer.cpp:2891) and reports false, so the draw falls to the
  # bind branch. `poseAvailable` is untouched, which is the point: this defect is
  # not visible in the input reading, only in the effect reading and the picture.
  # Earlier this was a `sed` that appended `&& false`; `sed -i` also rewrites the
  # file's line endings, and the patch it produced had an unbalanced parenthesis
  # (error C2059 at main.cpp:5043). Both are gone with patch_source.
  if ! patch_source native/src/main.cpp <<'PATCH'
                   && renderer.uploadViewModelBones(outcome.skinMatrices)) {
%%
                   && renderer.uploadViewModelBones({})) {
PATCH
  then
    fail_line "m1 did not apply to native/src/main.cpp"
  else
    if bash build-target.sh tf2_demo_native > "$RUN_DIR/m1-build.log" 2>&1; then
      run_product m1-apply --viewmodel "$VM_MODEL" --viewmodel-sequence "$POSE_SEQUENCE" --viewmodel-apply-animation
      expect_red "m1 vmAnimApplied" "$(reading "$RUN_DIR/m1-apply.trace" 'viewmodel pass order' vmAnimApplied)" "0" "1"
      assert_eq "m1 the pose is still resolved before it is dropped" \
        "$(reading "$RUN_DIR/m1-apply.trace" 'viewmodel pass order' vmAnimInput)" "ok"
      expect_red "m1 skinning" "$(reading "$RUN_DIR/m1-apply.trace" 'viewmodel pass order' skinning)" "0" "1"
      m1_hash=$(sha256sum "$RUN_DIR/m1-apply.bmp" | cut -d' ' -f1)
      bind_hash=$(sha256sum "$RUN_DIR/bind.bmp" 2>/dev/null | cut -d' ' -f1)
      if [ -n "$bind_hash" ] && [ "$m1_hash" = "$bind_hash" ]; then
        pass "m1 the applied frame collapses onto the bind frame"
        MUTATION_CATCHES=$((MUTATION_CATCHES + 1))
      else
        fail_line "m1 the applied frame is $m1_hash, expected the bind frame's ${bind_hash:-<missing>}"
      fi
    else
      fail_line "m1 the mutated tree did not build"
    fi
  fi
  restore_mutation

  # --- m2: the bone table is read transposed --------------------------------
  # This is the T13 defect itself, written as the shortest expression of it: the
  # stored float4s ARE the columns of the Source matrix, so dotting the vertex
  # with each one in turn applies the transpose -- rotation inverted, translation
  # taken from the wrong component. The upload, the stride, the layout and the
  # shader's control flow are all untouched, which is why only a picture can see
  # it.
  echo "--- m2 bone-read-transposed: the columns are consumed as if they were rows ---"
  MUTATION_TOTAL=$((MUTATION_TOTAL + 1))
  apply_mutation native/src/native_renderer.cpp "bone-read-transposed"
  if ! patch_source native/src/native_renderer.cpp <<'PATCH'
  return float4(
    vertexPosition.x * c0.x + vertexPosition.y * c1.x + vertexPosition.z * c2.x + weightW * c3.x,
    vertexPosition.x * c0.y + vertexPosition.y * c1.y + vertexPosition.z * c2.y + weightW * c3.y,
    vertexPosition.x * c0.z + vertexPosition.y * c1.z + vertexPosition.z * c2.z + weightW * c3.z,
    weightW);
%%
  float4 v = float4(vertexPosition, weightW);
  return float4(dot(v, c0), dot(v, c1), dot(v, c2), weightW);
PATCH
  then
    fail_line "m2 did not apply to native/src/native_renderer.cpp"
  else
    if bash build-target.sh tf2_demo_native > "$RUN_DIR/m2-build.log" 2>&1; then
      run_product m2-apply --viewmodel "$VM_MODEL" --viewmodel-sequence "$POSE_SEQUENCE" --viewmodel-apply-animation
      # The catch and its count in one place: the transposed read must leave the
      # ViewModel's own coverage by more than the intact tree ever does.
      mask_expect_red "m2 applied against the bind ViewModel mask" "$RUN_DIR/bind.bmp" "$RUN_DIR/m2-apply.bmp" \
        "$RUN_DIR/world.bmp" "$RUN_DIR/bind.bmp"
    else
      fail_line "m2 the mutated tree did not build"
    fi
  fi
  restore_mutation

  # --- m3: the extent reading stops measuring -------------------------------
  echo "--- m3 extent-not-measured: the pose is applied and its extent is not measured ---"
  MUTATION_TOTAL=$((MUTATION_TOTAL + 1))
  apply_mutation native/src/viewmodel.cpp "extent-not-measured"
  # The measurement is skipped whenever a bone table is present -- which is the
  # only case it exists for. Written as a guard on `boneCount` rather than an
  # unconditional `return` so the rest of the function stays reachable and the
  # mutated build adds no dead-code warning to the log a reader inspects.
  # The first attempt used an LF needle against a CRLF file and silently applied
  # nothing; patch_source matches the file's own newline.
  if ! patch_source native/src/viewmodel.cpp <<'PATCH'
  Box bindBox;
  Box posedBox;
%%
  if (boneCount != 0) {
    extent.reason = "mutation: the extent was not measured";
    return extent;
  }
  Box bindBox;
  Box posedBox;
PATCH
  then
    fail_line "m3 did not apply to native/src/viewmodel.cpp"
  else
    if bash build-target.sh tf2_demo_native > "$RUN_DIR/m3-build.log" 2>&1; then
      run_product m3-apply --viewmodel "$VM_MODEL" --viewmodel-sequence "$POSE_SEQUENCE" --viewmodel-apply-animation
      expect_red "m3 pose extent valid" "$(reading "$RUN_DIR/m3-apply.trace" 'pose extent' valid)" "0" "1"
      # Both sides of this pair come from the mutated run, so the assertion needs
      # no vertex count written into the gate: the defect leaves the total intact
      # and reports none of them as posed.
      expect_red "m3 posed vertices" "$(reading "$RUN_DIR/m3-apply.trace" 'pose extent' posedVertices)" \
        "0" "$(reading "$RUN_DIR/m3-apply.trace" 'pose extent' vertices)"
      assert_contains "m3 the reading names the mutation" \
        "$(reading_line "$RUN_DIR/m3-apply.trace" 'pose extent')" "mutation: the extent was not measured"
      if [ "$(reading "$RUN_DIR/m3-apply.trace" 'pose extent' valid)" = "0" ]; then
        MUTATION_CATCHES=$((MUTATION_CATCHES + 1))
      fi
    else
      fail_line "m3 the mutated tree did not build"
    fi
  fi
  restore_mutation

  # --- m4: the camera is placed inside the mesh ------------------------------
  # T18's step 7 asks for a mutation of "skipped skinning OR a stale camera/model
  # transform". This is the second kind, and it is the symptom T13 could not
  # explain: the placement solve puts the eye at `radius * 2.2`, and at
  # `radius * 0.22` the eye is inside a 29.9-unit model looking at its own
  # interior. The pose is still applied, the bone table is still correct, the
  # frame is still deterministic -- only the framing moved -- so the readings that
  # separate this defect from a skinning defect are the ones the gate reads off
  # the `gpu: viewmodel frame` line. Nothing else in the tree can see it.
  echo "--- m4 camera-inside-mesh: the placement distance collapses to 0.22x the radius ---"
  MUTATION_TOTAL=$((MUTATION_TOTAL + 1))
  apply_mutation native/src/native_renderer.cpp "camera-inside-mesh"
  if ! patch_source native/src/native_renderer.cpp <<'PATCH'
  const float distance = radius * 2.2f;
%%
  const float distance = radius * 0.22f;
PATCH
  then
    fail_line "m4 did not apply to native/src/native_renderer.cpp"
  else
    if bash build-target.sh tf2_demo_native > "$RUN_DIR/m4-build.log" 2>&1; then
      run_product m4-apply --viewmodel "$VM_MODEL" --viewmodel-sequence "$POSE_SEQUENCE" --viewmodel-apply-animation
      m4_radius=$(reading "$RUN_DIR/m4-apply.trace" 'gpu: viewmodel frame eye=' radius)
      m4_distance=$(reading "$RUN_DIR/m4-apply.trace" 'gpu: viewmodel frame eye=' distance)
      if [ -z "$m4_radius" ] || [ -z "$m4_distance" ]; then
        fail_line "m4 the framing numbers are missing"
      elif awk -v d="$m4_distance" -v r="$m4_radius" 'BEGIN { exit !(d < r) }'; then
        pass "m4 the eye is inside the model: distance=$m4_distance against radius=$m4_radius (the gate pins 2.2x)"
        MUTATION_CATCHES=$((MUTATION_CATCHES + 1))
      else
        fail_line "m4 distance=$m4_distance radius=$m4_radius, the camera did not move inside the mesh"
      fi
    else
      fail_line "m4 the mutated tree did not build"
    fi
  fi
  restore_mutation

  # The restoration is only complete when the rebuilt binary draws what the tree
  # drew before the mutations. The executable hash cannot answer that question:
  # MSVC stamps a fresh signature into every link, so three consecutive builds of
  # one unchanged tree produced 8e284755 / d599d33a / d6c787b1 on 2026-10-19. The
  # first run of this gate asserted the opposite, on the assumption that a rebuild
  # is byte-identical, and reported "the restored source rebuilds the same
  # executable" as a failure that was really a property of the linker.
  #
  # The frame is reproducible here -- section 5 asserts two runs of one command
  # agree byte for byte -- so the restoration is judged by re-drawing the bind
  # frame and comparing it with the one captured before any mutation.
  echo "--- restoration: rebuild the restored tree and re-draw the bind frame ---"
  if bash build-target.sh tf2_demo_native > "$RUN_DIR/restore-build.log" 2>&1; then
    echo "  ..   restored executable $(sha256sum "$EXE" | cut -d' ' -f1) (the forward run's was $FORWARD_EXE_HASH"
    echo "  ..   and the two differ because the linker stamps each link; see the note above)"
    run_product restored-bind --viewmodel "$VM_MODEL" --viewmodel-sequence "$POSE_SEQUENCE"
    assert_eq "the restored source redraws the pre-mutation bind frame" \
      "$(sha256sum "$RUN_DIR/restored-bind.bmp" 2>/dev/null | cut -d' ' -f1)" \
      "$(sha256sum "$RUN_DIR/bind.bmp" 2>/dev/null | cut -d' ' -f1)"
  else
    fail_line "the restored tree did not build"
  fi
  if [ "$MUTATION_CATCHES" -eq "$MUTATION_TOTAL" ] && [ "$MUTATION_TOTAL" -gt 0 ]; then
    echo "MUTATION-CAUGHT=PASS ($MUTATION_CATCHES/$MUTATION_TOTAL)"
  else
    echo "MUTATION-CAUGHT=FAIL ($MUTATION_CATCHES/$MUTATION_TOTAL)"
    fail=1
  fi
fi

# ---------------------------------------------------------------------------
# Evidence
# ---------------------------------------------------------------------------
echo
if [ "$MUTATION" -eq 0 ]; then
  {
    echo "# T18 viewmodel applied-pose gate, run $RUN_TAG"
    echo "executable_sha256=$(sha256sum "$EXE" | cut -d' ' -f1)"
    echo "recording=$(basename "$DEMO")"
    echo "tick=$TICK sequence=$POSE_SEQUENCE model=$VM_MODEL drawn=$MODEL"
    echo "host_overlap=$HOST_OVERLAP (1 = another worktree had a product process live; captures are paused)"
    echo
    echo "## frames"
    for f in bind apply repeat world flat-bind flat-apply; do
      p="$RUN_DIR/$f.bmp"
      [ -f "$p" ] && echo "frame.$f.sha256=$(sha256sum "$p" | cut -d' ' -f1) frame.$f.bytes=$(stat -c%s "$p")"
    done
    echo
    echo "## applied run, product lines"
    for pattern in 'viewmodel pass order' 'scene: vmAnim entity=' 'gpu: viewmodel frame eye=' 'gpu: viewmodel centre viewSpace=' 'gpu: viewmodel pose extent'; do
      reading_line "$RUN_DIR/apply.trace" "$pattern"
    done
    echo
    echo "## bind run, the same lines that must not move"
    for pattern in 'viewmodel pass order' 'gpu: viewmodel frame eye=' 'gpu: viewmodel centre viewSpace='; do
      reading_line "$RUN_DIR/bind.trace" "$pattern"
    done
    echo
    echo "## mask containment (bounds: outside <= $CONTAIN_OUTSIDE_MAX, changed >= $CONTAIN_CHANGED_MIN)"
    python bmp-diff-mask.py "$RUN_DIR/bind.bmp" "$RUN_DIR/apply.bmp" \
      --mask "$RUN_DIR/world.bmp" "$RUN_DIR/bind.bmp" \
      --max-outside "$CONTAIN_OUTSIDE_MAX" --min-changed "$CONTAIN_CHANGED_MIN" 2>&1 | tail -1
    python bmp-diff-mask.py "$RUN_DIR/flat-bind.bmp" "$RUN_DIR/flat-apply.bmp" \
      --mask "$RUN_DIR/world.bmp" "$RUN_DIR/flat-bind.bmp" \
      --max-outside "$CONTAIN_OUTSIDE_MAX" --min-changed "$CONTAIN_CHANGED_MIN" 2>&1 | tail -1
    echo
    echo "## caveats"
    echo "paused_capture=1 (a playing capture lands on a scheduler-dependent tick)"
    echo "world_geometry=shattered (F1/T17): the composition is judged, not the map"
  } > "$EVIDENCE/summary.txt"
  cp "$RUN_DIR/bind.bmp" "$EVIDENCE/bind-pose.bmp"
  cp "$RUN_DIR/apply.bmp" "$EVIDENCE/applied-pose.bmp"
  # The two inspection crops the report refers to. Committed as PNG because a BMP
  # is 2.5 MB and the crop is the part a reader actually looks at; the BMPs the
  # hashes in summary.txt refer to are the full frames.
  python bmp-crop-grid.py "$EVIDENCE/inspect-bind-applied-3x.png" \
    --region 540,280,340,140 --scale 3 --label "bind / applied" \
    "$RUN_DIR/bind.bmp" "$RUN_DIR/apply.bmp" > /dev/null 2>&1
  python bmp-to-png.py "$RUN_DIR/apply.bmp" "$EVIDENCE/inspect-applied.png" > /dev/null 2>&1
  python bmp-to-png.py "$RUN_DIR/bind.bmp" "$EVIDENCE/inspect-bind.png" > /dev/null 2>&1
  echo "evidence written to $EVIDENCE"
fi

echo
if [ "$fail" -eq 0 ]; then
  echo "VIEWMODEL-APPLY=PASS ($OK_COUNT assertions, $FAIL_COUNT failures)"
  exit 0
else
  echo "VIEWMODEL-APPLY=FAIL ($FAIL_COUNT failed of $((OK_COUNT + FAIL_COUNT)))"
  for line in "${FAIL_LINES[@]}"; do echo "  - $line"; done
  exit 1
fi
