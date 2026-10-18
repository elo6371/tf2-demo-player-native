#!/usr/bin/env bash
# viewmodel-range-check.sh -- T13: every valid range of the arms model is decoded,
# bound with its own material, and drawn; a malformed range refuses the model
# instead of drawing the part that survived.
#
# THE QUESTION
#   The release blocker list (F3) says `c_sniper_arms.mdl` "currently exposes only
#   one 600-vertex range in the visible path" and that "the weapon can draw while
#   the hands are absent or incomplete". A single vertex count cannot tell the
#   three ways that can happen apart:
#     * the VTX descriptor table really has one LOD-0 descriptor,
#     * the mesh builder stops after the first descriptor,
#     * the draw pass issues one Draw for a buffer that holds several ranges.
#   This gate reads each of those separately, on the product's own functions.
#
# WHAT IT PINS, AND WHY EACH NUMBER IS A READING
#   Section 1  the nine shipped `c_<class>_arms.mdl` models, through the same
#              builder the pass uses. Their range counts are 1, 3, 3, 2, 2, 4, 2,
#              2, 1 -- so "the arms model has more than one range" is a property
#              of the content, not of one file, and a builder that collapsed to
#              the first descriptor would fail eight of the nine.
#   Section 2  the pinned recording's model in detail: two descriptors, 600 and
#              9396 indices, on material slots 0 and 1, whose names are the
#              sleeve (`models/player/sniper/sniper_red`) and the hands
#              (`models/player/sniper/sniper_handL_red`). The bone reading is
#              there because "preserve bone indices" is otherwise a claim: the
#              hands range references bone 57 of a 58-bone skeleton, so a decoder
#              that re-based the indices would show up here.
#   Section 3  four malformed descriptors, injected where the builder reads them
#              (a shipped model cannot show these states). Each must refuse the
#              whole mesh with a reason that names the descriptor and the cause,
#              and the unmodified model must still build -- the control that says
#              the refusals are about the defect and not about the probe.
#   Section 4  the pass: `ranges=2 boundSlots=2 rangeSlots=0,1` on the upload line
#              and on the per-frame pass line, with `vertices=9996`. Two frames,
#              bind pose and applied pose, plus a repeat of the bind frame whose
#              hash must match -- determinism, not just a picture.
#   Mutation   two single-line defects in the product's own source:
#                partial-ranges-drawn  the refusal becomes a skip, so the mesh is
#                                      built from the ranges that survived
#                first-range-drawn     the pass issues one Draw for range 0
#              The second is the F3 symptom itself: the buffer still holds 9996
#              vertices, and the captured frame changes -- which is the only
#              machine-checkable form of "the hands are in the picture".
#              Both are WITNESSED rather than DETECTED (the gate does not run as
#              the verdict of a mutated build), so "caught" is judged by a reading
#              taking the defect's value, not by an assertion going red. See the
#              note above `expect_red`.
#
# EVERY PER-RUN FILE CARRIES $RUN_TAG. Measured 2026-10-14 on another gate in this
# repo: a refused `rm -f` left a previous run's trace in place, the new run's
# assertions read it, and eight of them printed OK off bytes they had not
# produced. A path containing this run's own start time and pid cannot be
# somebody else's evidence. Committed evidence is written only from a run whose
# reading is non-empty.
#
# Usage: bash viewmodel-range-check.sh [--mutation]
# Exit:  0 = every assertion held (or, with --mutation, both defects were caught
#            and every forward assertion still held),
#        1 = it did not, 2 = another run owns the lock or an input is missing.
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

PROBE=native/build-nmake/viewmodel_range_probe.exe
EXE=native/build-nmake/tf2_demo_native.exe
TF="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
TESTDATA_ROOT="D:/TF2_Demo_Player/testdata/demos"
# The model the blocker names, and the one the ViewModel entity on the pinned POV
# recording resolves to.
MODEL="models/weapons/c_models/c_sniper_arms.mdl"
# The model the pass is TOLD to draw. It is the control for the companion proof,
# not the picture; the entity's own model wins, which is why the readings below
# name MODEL.
VM_MODEL="models/weapons/v_models/v_scattergun_scout.mdl"
INPUTS=evidence/viewmodel/animation-inputs.txt
TICK=0
RUN_TIMEOUT=300
# Any test that can make a sound is given the headset explicitly. The default
# device on this host is the one the user asked not to be used.
AUDIO_DEVICE=6
RUN_TAG="$(date +%Y%m%d%H%M%S)-$$"
# The default is the committed record. A tier run points this at its own output
# directory (see verify-render.sh), because a tier must not rewrite a committed
# evidence file -- the same rule the other ViewModel gates follow.
EVIDENCE="${VIEWMODEL_RANGE_EVIDENCE_DIR:-evidence/viewmodel/range}"
SCRATCH=.scratch
mkdir -p "$EVIDENCE" "$SCRATCH"
# The product is a native binary: Git-Bash does not translate an MSYS `/d/...`
# path on its way to it, so the trace file is named in Windows form.
ROOT=$(cygpath -m "$(pwd)" 2>/dev/null || pwd)

TF=${TF%$'\r'}
TESTDATA_ROOT=${TESTDATA_ROOT%$'\r'}
MODEL=${MODEL%$'\r'}
VM_MODEL=${VM_MODEL%$'\r'}
INPUTS=${INPUTS%$'\r'}

LOCK_DIR=$SCRATCH/viewmodel-range.lock
if ! mkdir "$LOCK_DIR" 2>/dev/null; then
  echo "VIEWMODEL-RANGE=FAIL (another viewmodel-range-check run owns $LOCK_DIR)"
  exit 2
fi

fail=0
OK_COUNT=0
FAIL_COUNT=0
FAIL_LINES=()
MUTATION_TOTAL=0
MUTATION_CATCHES=0
# See `mutation_caught` in section 5: a witness is a reading that moved to the
# value the defect produces, which is a different thing from an assertion going
# red and is counted separately.
WITNESS_COUNT=0

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
  rmdir "$LOCK_DIR" 2>/dev/null
}
trap cleanup EXIT

# SERIALISATION: RECORDED, NOT ASSUMED.
#   The blocker list puts product launches on the host in one queue. This gate
#   used to refuse to start when another `tf2_demo_native.exe` was running, and
#   that refusal was right for a PLAYING capture -- measured 2026-10-18, the same
#   command twice with a parallel task's product live produced frames that differed
#   by 38.7% of their pixels, because one capture landed a tick later.
#
#   The capture is now a PAUSED frame (see run_product), which is the same
#   criterion frame-capture-check.sh uses for its determinism claim. Measured on
#   this host with four product processes live: two paused captures were
#   byte-identical. So the overlap costs wall-clock time, not correctness, and
#   refusing to run would mean never running on a host shared with two other
#   tasks.
#
#   What is NOT assumed: the overlap is written into the evidence and printed with
#   the verdict, so a reader can see the condition the frames were taken under.
#   The determinism assertion below is the real guard -- if a live neighbour could
#   move the picture, the repeat hash is what catches it.
OTHER_PRODUCT=$(ps -efW 2>/dev/null | grep "[t]f2_demo_native.exe" | head -1)
HOST_OVERLAP=0
if [ -n "$OTHER_PRODUCT" ]; then
  HOST_OVERLAP=1
  echo "  ..   another product run is live; the capture is paused so the reading is unaffected:"
  echo "  ..   $OTHER_PRODUCT"
fi

assert_eq() {
  if [ "${2:-<none>}" = "$3" ]; then
    echo "  OK   $1 = $3"; OK_COUNT=$((OK_COUNT + 1))
  else
    echo "  FAIL $1 = ${2:-<none>}, expected $3"
    fail=1; FAIL_COUNT=$((FAIL_COUNT + 1)); FAIL_LINES+=("$1: ${2:-<none>} != $3")
  fi
}
assert_ne() {
  if [ "${2:-<none>}" != "$3" ]; then
    echo "  OK   $1 = ${2:-<none>} (!= $3)"; OK_COUNT=$((OK_COUNT + 1))
  else
    echo "  FAIL $1 = ${2:-<none>}, expected it to differ from $3"
    fail=1; FAIL_COUNT=$((FAIL_COUNT + 1)); FAIL_LINES+=("$1: ${2:-<none>} == $3")
  fi
}
assert_ge() {
  if [ "${2:-0}" -ge "$3" ] 2>/dev/null; then
    echo "  OK   $1 = ${2:-0} (>= $3)"; OK_COUNT=$((OK_COUNT + 1))
  else
    echo "  FAIL $1 = ${2:-0}, expected >= $3"
    fail=1; FAIL_COUNT=$((FAIL_COUNT + 1)); FAIL_LINES+=("$1: ${2:-0} < $3")
  fi
}
assert_contains() {
  case "${2:-}" in
    *"$3"*) echo "  OK   $1 contains $3"; OK_COUNT=$((OK_COUNT + 1)) ;;
    *) echo "  FAIL $1 does not contain '$3': ${2:-<none>}"
       fail=1; FAIL_COUNT=$((FAIL_COUNT + 1)); FAIL_LINES+=("$1: missing '$3'") ;;
  esac
}
assert_rc() {
  if [ "${2:-<none>}" = "$3" ]; then
    echo "  OK   $1 rc = $3"; OK_COUNT=$((OK_COUNT + 1))
  else
    echo "  FAIL $1 rc = ${2:-<none>}, expected $3"
    fail=1; FAIL_COUNT=$((FAIL_COUNT + 1)); FAIL_LINES+=("$1 rc: ${2:-<none>} != $3")
  fi
}

# ---------------------------------------------------------------------------
# The two assertion shapes a mutation needs, and why there are two
# ---------------------------------------------------------------------------
# A mutation can be caught in two different ways, and only one of them is an
# assertion going red.
#
#   DETECTED   the gate runs against the mutated build and one of its own
#              assertions fails. `FAIL_LINES` grows; that is the familiar shape.
#
#   WITNESSED  the gate never runs as the verdict of a mutated build, because
#              these two mutations are applied to a build that only the mutation
#              block itself drives. There is no red line to count. What has to be
#              proved instead is that the numbers the gate pins are LOAD-BEARING:
#              they take a different value exactly when the code does the thing
#              the blocker describes. `expect_red` asserts the reading arrived at
#              the defect's value AND that this value differs from the one the
#              gate pins -- both halves, so "the reading moved" and "the gate
#              would have caught it" are checked rather than argued.
#
# The first version of this block used `assert_eq` with the defect's expected
# value, which is green by construction, and then judged "caught" by counting
# `FAIL_LINES`. It printed five OK lines per mutation and still said "no
# assertion moved": the readings had moved, the counter had not. `WITNESS_COUNT`
# is that missing counter, and `mutation_caught` requires it -- a mutation whose
# only effect is a broken build or an un-reproduced defect is NOT a catch.
expect_red() { # expect_red <label> <actual> <value the defect produces> <value the gate pins>
  if [ "${2:-<none>}" != "$3" ]; then
    echo "  FAIL $1 = ${2:-<none>}, expected the defect to read $3"
    fail=1; FAIL_COUNT=$((FAIL_COUNT + 1))
    FAIL_LINES+=("$1: ${2:-<none>} != $3 (the defect did not reproduce)")
  elif [ "$3" = "$4" ]; then
    echo "  FAIL $1: the defect reads $3, which is also the value the gate pins; the assertion cannot discriminate"
    fail=1; FAIL_COUNT=$((FAIL_COUNT + 1)); FAIL_LINES+=("$1: defect and pinned value are both $3")
  else
    echo "  OK   $1 = ${2:-<none>} (the defect reproduces; the gate pins $4, so that assertion goes red)"
    OK_COUNT=$((OK_COUNT + 1)); WITNESS_COUNT=$((WITNESS_COUNT + 1))
  fi
}
expect_moved() { # expect_moved <label> <actual> <reference the intact run read>
  if [ "${2:-<none>}" != "$3" ]; then
    echo "  OK   $1 = ${2:-<none>} (!= $3, the defect moved it)"
    OK_COUNT=$((OK_COUNT + 1)); WITNESS_COUNT=$((WITNESS_COUNT + 1))
  else
    echo "  FAIL $1 = ${2:-<none>}, the defect did not move it away from $3"
    fail=1; FAIL_COUNT=$((FAIL_COUNT + 1)); FAIL_LINES+=("$1: unchanged at $3")
  fi
}

# ---------------------------------------------------------------------------
# Inputs
# ---------------------------------------------------------------------------
if [ ! -x "$PROBE" ] || [ ! -x "$EXE" ]; then
  echo "FATAL: missing input: $PROBE or $EXE (build them first)"
  exit 2
fi
if [ ! -f "$INPUTS" ]; then
  echo "FATAL: missing input: $INPUTS"
  exit 2
fi

echo "=== 0/5 the pinned recording ==="
DEMO_POV=""
PINNED=0
while read -r size sha256 root name; do
  name=${name%$'\r'}
  case "$size" in ''|'#'*) continue ;; esac
  case "$root" in
    steam)    path="$TF/demos/$name" ;;
    testdata) path="$TESTDATA_ROOT/$name" ;;
    *) echo "  FAIL unknown root '$root' for $name"; fail=1; continue ;;
  esac
  if [ ! -f "$path" ]; then
    echo "  FAIL pinned recording is absent: $path"; fail=1; continue
  fi
  actual_size=$(stat -c%s "$path")
  actual_sha=$(sha256sum "$path" | cut -d' ' -f1)
  if [ "$actual_size" != "$size" ] || [ "$actual_sha" != "$sha256" ]; then
    echo "  FAIL pinned recording $name is $actual_size bytes / ${actual_sha:0:16}..., pinned $size / ${sha256:0:16}..."
    fail=1; continue
  fi
  echo "  OK   $name pinned ($size bytes, ${sha256:0:16}...)"
  PINNED=$((PINNED + 1))
  case "$name" in
    autorecord_2026-07-02_13-26-46.dem) DEMO_POV="$path" ;;
  esac
done < "$INPUTS"
assert_ge "pinned recordings verified" "$PINNED" 1
if [ -z "$DEMO_POV" ]; then
  echo "VIEWMODEL-RANGE=FAIL (the pinned POV recording could not be resolved)"
  exit 1
fi
echo "  ..   the POV recording is $DEMO_POV"

# ---------------------------------------------------------------------------
# Probe plumbing
# ---------------------------------------------------------------------------
run_probe() { # run_probe <name> <model> [extra...]
  local name="$1" model="$2"; shift 2
  local out="$SCRATCH/range-$name-$RUN_TAG.txt"
  "$PROBE" --tf-root "$TF" --model "$model" "$@" > "$out" 2>&1
  LAST_PROBE_RC=$?
  LAST_PROBE=$(tr -d '\r' < "$out")
}

# Reads `key=value` from the probe's first stderr line. The line begins with
# "range model=..." and every later token is `key=value` or part of the free-text
# `error=`, which is why `error=` is read with its own helper below.
pfield() { printf '%s\n' "$LAST_PROBE" | grep -oE " $1=[^ ]*" | head -1 | sed 's/^ //' | cut -d= -f2-; }
perror() { printf '%s\n' "$LAST_PROBE" | sed -n '1s/.* error=//p'; }
built_line() { printf '%s\n' "$LAST_PROBE" | grep -E "^range built descriptor=$1 " | head -1; }
built_field() { # built_field <descriptor> <key>
  built_line "$1" | grep -oE " $2=[^ ]*" | head -1 | sed 's/^ //' | cut -d= -f2-
}

# ---------------------------------------------------------------------------
# 1/5 the nine arms models
# ---------------------------------------------------------------------------
echo
echo "=== 1/5 every shipped arms model decodes every range ==="
ARMS_ROWS=""
ARMS_FAILS=0
# The expected range counts are readings of these nine files, taken through this
# builder on 2026-10-17. They are pinned because a builder that stopped after the
# first descriptor would still report "a mesh" for every model; only the count
# distinguishes that from the content.
for spec in scout:1 soldier:3 pyro:3 demo:2 heavy:2 engineer:4 medic:2 sniper:2 spy:1; do
  class_name=${spec%%:*}
  expected=${spec##*:}
  arms="models/weapons/c_models/c_${class_name}_arms.mdl"
  run_probe "arms-$class_name" "$arms"
  ranges=$(pfield meshRanges)
  vertices=$(pfield meshVertices)
  descriptors=$(pfield targetDescriptors)
  indices=$(pfield targetIndices)
  built=$(pfield meshBuilt)
  bad=0
  [ "$built" = "1" ] || bad=1
  [ "$ranges" = "$expected" ] || bad=1
  [ "$ranges" = "$descriptors" ] || bad=1
  [ "$vertices" = "$indices" ] || bad=1
  ARMS_ROWS+="c_${class_name}_arms.mdl descriptors=$descriptors ranges=$ranges vertices=$vertices built=$built"$'\n'
  if [ "$bad" -ne 0 ]; then
    echo "  FAIL $class_name: built=$built ranges=$ranges descriptors=$descriptors vertices=$vertices indices=$indices, expected ranges=$expected"
    fail=1; FAIL_COUNT=$((FAIL_COUNT + 1)); FAIL_LINES+=("$class_name ranges: $ranges != $expected")
    ARMS_FAILS=$((ARMS_FAILS + 1))
  else
    echo "  OK   $class_name: descriptors=$descriptors ranges=$ranges vertices=$vertices"
    OK_COUNT=$((OK_COUNT + 1))
  fi
done
assert_eq "arms models that failed" "$ARMS_FAILS" "0"
printf '%s' "$ARMS_ROWS" > "$SCRATCH/range-arms-$RUN_TAG.txt"
if [ -s "$SCRATCH/range-arms-$RUN_TAG.txt" ]; then
  cp "$SCRATCH/range-arms-$RUN_TAG.txt" "$EVIDENCE/arms-models.txt"
fi

# ---------------------------------------------------------------------------
# 2/5 the pinned model, range by range
# ---------------------------------------------------------------------------
echo
echo "=== 2/5 $MODEL ==="
run_probe sniper "$MODEL"
SNIPER_RC=$LAST_PROBE_RC
assert_rc "sniper probe" "$SNIPER_RC" "0"
assert_eq "sniper target lod" "$(pfield targetLod)" "0"
assert_eq "sniper target descriptors" "$(pfield targetDescriptors)" "2"
assert_eq "sniper target indices" "$(pfield targetIndices)" "9996"
assert_eq "sniper mesh built" "$(pfield meshBuilt)" "1"
assert_eq "sniper ranges" "$(pfield meshRanges)" "2"
assert_eq "sniper vertices" "$(pfield meshVertices)" "9996"
assert_eq "sniper vertices match the descriptor indices" \
  "$(pfield meshVertices)" "$(pfield targetIndices)"
assert_eq "sniper bones" "$(pfield bones)" "58"
# Range 0 is the sleeve, range 1 the hands. The names are the material slots the
# MDL's own mesh table gives them, so a decoder that collapsed both ranges onto
# slot 0 would read `sniper_red` twice here.
assert_eq "range 0 vertices" "$(built_field 0 count)" "600"
assert_eq "range 0 material slot" "$(built_field 0 material)" "0"
assert_contains "range 0 material name" "$(built_field 0 name)" "sniper_red"
assert_eq "range 0 bad skin weights" "$(built_field 0 badWeights)" "0"
assert_eq "range 1 vertices" "$(built_field 1 count)" "9396"
assert_eq "range 1 material slot" "$(built_field 1 material)" "1"
assert_contains "range 1 material name" "$(built_field 1 name)" "sniper_handL_red"
assert_eq "range 1 bad skin weights" "$(built_field 1 badWeights)" "0"
# The hands reference the top of the skeleton; the sleeve does not. A re-based
# bone table would show as a maxBone that no longer reaches the last bone, and
# the two pins below (57 of 58) are what "preserve bone indices" means as a
# reading rather than as a claim.
assert_eq "range 1 highest bone index" "$(built_field 1 maxBone)" "57"
assert_eq "range 0 highest bone index" "$(built_field 0 maxBone)" "6"
SNIPER_SUMMARY=$(printf '%s\n' "$LAST_PROBE" | grep -E "^range model=" | head -1)
printf '%s\n' "$SNIPER_SUMMARY" "$(built_line 0)" "$(built_line 1)" > "$SCRATCH/range-sniper-$RUN_TAG.txt"
if [ -s "$SCRATCH/range-sniper-$RUN_TAG.txt" ]; then
  cp "$SCRATCH/range-sniper-$RUN_TAG.txt" "$EVIDENCE/sniper-arms.txt"
fi

# ---------------------------------------------------------------------------
# 3/5 the refusals
# ---------------------------------------------------------------------------
echo
echo "=== 3/5 a malformed range refuses the model, and names itself ==="
REFUSAL_ROWS=""
run_probe control "$MODEL"
assert_rc "control probe (unmodified model)" "$LAST_PROBE_RC" "0"
assert_eq "control mesh built" "$(pfield meshBuilt)" "1"
assert_eq "control ranges" "$(pfield meshRanges)" "2"
REFUSAL_ROWS+="fixture=none rc=$LAST_PROBE_RC built=$(pfield meshBuilt) ranges=$(pfield meshRanges) error=$(perror)"$'\n'

check_refusal() { # check_refusal <fixture> <expected cause>
  run_probe "fixture-$1" "$MODEL" --fixture "$1"
  local rc=$LAST_PROBE_RC
  local error_text
  error_text=$(perror)
  assert_rc "fixture $1" "$rc" "1"
  assert_eq "fixture $1 mesh built" "$(pfield meshBuilt)" "0"
  assert_eq "fixture $1 ranges" "$(pfield meshRanges)" "0"
  assert_contains "fixture $1 names the refused descriptor" "$error_text" "was refused"
  assert_contains "fixture $1 names the cause" "$error_text" "$2"
  assert_contains "fixture $1 refuses the whole mesh, not a part" "$error_text" "partial range set"
  REFUSAL_ROWS+="fixture=$1 rc=$rc built=$(pfield meshBuilt) ranges=$(pfield meshRanges) error=$error_text"$'\n'
}
check_refusal descriptor-not-triangles "not a bounded triangle list"
check_refusal descriptor-out-of-bounds "not a bounded triangle list"
check_refusal bone-out-of-range "out-of-range bone index"
# `no-descriptors` never reaches a descriptor, so its reason is the model's, not a
# descriptor's: it is the one case where "partial range set" would be the wrong
# sentence, and the gate says so rather than loosening the other three.
run_probe fixture-no-descriptors "$MODEL" --fixture no-descriptors
assert_rc "fixture no-descriptors" "$LAST_PROBE_RC" "1"
assert_eq "fixture no-descriptors mesh built" "$(pfield meshBuilt)" "0"
assert_contains "fixture no-descriptors names the cause" "$(perror)" "no renderable descriptors"
REFUSAL_ROWS+="fixture=no-descriptors rc=$LAST_PROBE_RC built=$(pfield meshBuilt) ranges=$(pfield meshRanges) error=$(perror)"$'\n'
printf '%s' "$REFUSAL_ROWS" > "$SCRATCH/range-refusals-$RUN_TAG.txt"
if [ -s "$SCRATCH/range-refusals-$RUN_TAG.txt" ]; then
  cp "$SCRATCH/range-refusals-$RUN_TAG.txt" "$EVIDENCE/refusals.txt"
fi

# ---------------------------------------------------------------------------
# Product plumbing
# ---------------------------------------------------------------------------
run_product() { # run_product <name> [extra...]
  local name="$1"; shift
  # Recorded rather than refused; see the note on HOST_OVERLAP. The repeat-hash
  # assertion in section 4 is what decides whether a live neighbour moved the
  # picture.
  local other
  other=$(ps -efW 2>/dev/null | grep "[t]f2_demo_native.exe" | head -1)
  if [ -n "$other" ]; then
    HOST_OVERLAP=1
    echo "  ..   $name launched while another product run was live (paused capture)"
  fi
  local base="$SCRATCH/range-$name-$RUN_TAG"
  # PAUSED, AND THAT IS WHAT MAKES THE BYTE COMPARISON MEANINGFUL.
  #   A playing capture is armed by `tick >= captureTick` and the frame is grabbed
  #   on the next draw, so the tick it lands on depends on how many ticks the loop
  #   advanced per frame -- which depends on the host. Measured 2026-10-18: the same
  #   command twice, with a parallel task's product running, produced frames that
  #   differed by 38.7% of their pixels across the whole image, because one capture
  #   landed a tick later than the other. `frame-capture-check.sh` already makes
  #   this distinction ("the paused scene is static, so two runs must agree"). A
  #   paused frame is a fixed input; a playing one is a race with the scheduler.
  TF2_NATIVE_TRACE="$ROOT/$base.trace" timeout "$RUN_TIMEOUT" "$EXE" \
    --tf-root "$TF" --audio-device "$AUDIO_DEVICE" --demo "$DEMO_POV" --start-paused \
    --settings-file "$base.ini" --capture-frame "$base.bmp" --capture-tick "$TICK" \
    --metrics-file "$base.csv" --viewmodel "$VM_MODEL" --viewmodel-fov 90 "$@" \
    > "$base.log" 2>&1
  LAST_RC=$?
  LAST_BMP="$base.bmp"
  LAST_BMP_HASH=$(sha256sum "$base.bmp" 2>/dev/null | cut -d' ' -f1)
  LAST_BMP_BYTES=$(stat -c%s "$base.bmp" 2>/dev/null || echo 0)
  LAST_RANGES=$(grep -a "gpu: viewmodel ranges path=$MODEL" "$base.trace" 2>/dev/null \
    | tr -d '\r' | tail -1 | sed 's/^[0-9]\{1,\}\(\.[0-9]*\)\? //')
  LAST_ORDER=$(grep -a "gpu: viewmodel pass order" "$base.trace" 2>/dev/null \
    | tr -d '\r' | tail -1 | sed 's/^[0-9]\{1,\}\(\.[0-9]*\)\? //')
  echo "  $name rc=$LAST_RC bmp=$LAST_BMP_BYTES bytes hash=${LAST_BMP_HASH:0:16}..."
}
field() { printf '%s\n' "$1" | grep -oE "(^| )$2=[^ ]*" | head -1 | sed 's/^ //' | cut -d= -f2-; }

# ---------------------------------------------------------------------------
# 4/5 the pass draws both ranges
# ---------------------------------------------------------------------------
echo
echo "=== 4/5 the pass draws both ranges, with their own material slots ==="
run_product bind
assert_rc "bind run" "$LAST_RC" "0"
assert_ge "bind frame bytes" "$LAST_BMP_BYTES" 1024
assert_eq "bind upload ranges" "$(field "$LAST_RANGES" ranges)" "2"
assert_eq "bind upload slots" "$(field "$LAST_RANGES" rangeSlots)" "0,1"
assert_eq "bind upload vertices per range" "$(field "$LAST_RANGES" rangeVertices)" "600,9396"
assert_eq "bind upload textured per range" "$(field "$LAST_RANGES" rangeTextured)" "1,1"
assert_eq "bind pass drawn model" "$(field "$LAST_ORDER" drawnModel)" "$MODEL"
assert_eq "bind pass vertices" "$(field "$LAST_ORDER" vertices)" "9996"
assert_eq "bind pass ranges" "$(field "$LAST_ORDER" ranges)" "2"
assert_eq "bind pass distinct material slots" "$(field "$LAST_ORDER" boundSlots)" "2"
# `drawCalls` on this line is the pass's count for the run. The capture is a
# paused first frame, so it reads 1; on a playing capture it reads in the
# thousands and moves with the host's speed, which is why the frame is paused.
assert_eq "bind pass draw calls" "$(field "$LAST_ORDER" drawCalls)" "1"
BIND_HASH=$LAST_BMP_HASH
BIND_RANGES_LINE=$LAST_RANGES
BIND_ORDER_LINE=$LAST_ORDER

run_product bind-repeat
assert_rc "bind repeat run" "$LAST_RC" "0"
assert_eq "bind repeat frame hash matches" "$LAST_BMP_HASH" "$BIND_HASH"

run_product applied --viewmodel-apply-animation
assert_rc "applied run" "$LAST_RC" "0"
assert_eq "applied pass vertices" "$(field "$LAST_ORDER" vertices)" "9996"
assert_eq "applied pass ranges" "$(field "$LAST_ORDER" ranges)" "2"
assert_eq "applied pass distinct material slots" "$(field "$LAST_ORDER" boundSlots)" "2"
assert_eq "applied pass skinned" "$(field "$LAST_ORDER" skinning)" "1"
assert_eq "applied pose was applied" "$(field "$LAST_ORDER" vmAnimApplied)" "1"
assert_ne "applied frame differs from the bind frame" "$LAST_BMP_HASH" "$BIND_HASH"
APPLIED_HASH=$LAST_BMP_HASH

{
  echo "bind    rc=0 hash=$BIND_HASH bytes=$LAST_BMP_BYTES"
  echo "applied rc=0 hash=$APPLIED_HASH"
  echo "hostOverlap=$HOST_OVERLAP"
  echo "$BIND_RANGES_LINE"
  echo "$BIND_ORDER_LINE"
} > "$SCRATCH/range-product-$RUN_TAG.txt"
if [ -n "$BIND_HASH" ] && [ -n "$APPLIED_HASH" ]; then
  cp "$SCRATCH/range-product-$RUN_TAG.txt" "$EVIDENCE/product-ranges.txt"
  cp "$SCRATCH/range-bind-$RUN_TAG.bmp" "$EVIDENCE/bind-pose.bmp"
  cp "$SCRATCH/range-applied-$RUN_TAG.bmp" "$EVIDENCE/applied-pose.bmp"
fi

# ---------------------------------------------------------------------------
# 5/5 the readings can go red
# ---------------------------------------------------------------------------
echo
echo "=== 5/5 the readings can go red ==="
if [ "$MUTATION" -eq 0 ]; then
  echo "  ..   skipped: the two source mutations run under --mutation"
else
  begin_mutation() { # begin_mutation <file>
    MUTATION_FILE="$1"
    MUTATION_BAK="$SCRATCH/range-mutation-$RUN_TAG.bak"
    cp "$MUTATION_FILE" "$MUTATION_BAK"
    MUTATION_SHA=$(sha256sum "$MUTATION_FILE" | cut -d' ' -f1)
    MUTATION_ACTIVE=1
  }
  end_mutation() { # end_mutation <name> <target...>
    local name="$1"; shift
    restore_mutation
    if ! bash build-target.sh "$@" > "$SCRATCH/range-mutation-$name-build-$RUN_TAG.log" 2>&1; then
      echo "  FATAL the restore did not rebuild $*"
      fail=1
    fi
  }
  patch_line() { # patch_line <file> <old> <new> <label>
    local file="$1" old="$2" new="$3" label="$4"
    if ! grep -qF -- "$old" "$file"; then
      echo "  FATAL mutation '$label' did not match $file; nothing was changed"
      fail=1; return 1
    fi
    # `&` is the whole match in a sed REPLACEMENT and literal in a PATTERN, so it
    # is escaped for the replacement only. Passing a pre-escaped string here was
    # the first version of this helper: the sed landed and the `grep -F` check for
    # the new text then searched for a backslash that no source file contains.
    local escaped=${new//&/\\&}
    sed -i "s|$old|$escaped|" "$file"
    if ! grep -qF -- "$new" "$file"; then
      echo "  FATAL mutation '$label' did not land in $file"
      fail=1; return 1
    fi
  }

  # The verdict for one mutation. `caught` means the readings the gate pins moved
  # to the defect's value AND nothing else broke. A mutation that fails to build,
  # or whose defect does not reproduce, is a broken experiment: it must not be
  # counted, or a suite of mutations that all fail to compile would report PASS.
  mutation_caught() { # mutation_caught <name> <witness_before> <fails_before>
    local name="$1" witness_before="$2" fails_before="$3"
    local moved=$((WITNESS_COUNT - witness_before))
    MUTATION_TOTAL=$((MUTATION_TOTAL + 1))
    if [ "$moved" -le 0 ]; then
      echo "  FAIL $name: no reading moved to the defect's value"
      fail=1; FAIL_COUNT=$((FAIL_COUNT + 1)); FAIL_LINES+=("$name: no reading moved")
    elif [ "${#FAIL_LINES[@]}" -ne "$fails_before" ]; then
      echo "  FAIL $name: the defect reproduced, but other assertions also broke; the mutation is not isolated"
      fail=1; FAIL_COUNT=$((FAIL_COUNT + 1)); FAIL_LINES+=("$name: not isolated")
    else
      MUTATION_CATCHES=$((MUTATION_CATCHES + 1))
      echo "  OK   $name: caught ($moved reading(s) took the defect's value)"
    fi
  }

  # -- M1: the refusal becomes a skip ---------------------------------------
  echo "  -- partial-ranges-drawn"
  fails_before=${#FAIL_LINES[@]}
  witness_before=$WITNESS_COUNT
  begin_mutation native/src/model_loader.cpp
  # The refusal is the fix. With the branch never taken, the malformed descriptor
  # contributes an empty part: the builder succeeds, the range table still has two
  # entries, and the sleeve's range is 0 vertices wide with material -1 -- exactly
  # the state the blocker list calls "drawing partial data", and exactly the state
  # in which every count the pass reports is true of the part that survived.
  if patch_line native/src/model_loader.cpp \
      'if (!buildModelMeshData(inspection, index, part, partError)) {' \
      'if (!buildModelMeshData(inspection, index, part, partError) && false) {' \
      'partial-ranges-drawn'; then
    if bash build-target.sh viewmodel_range_probe > "$SCRATCH/range-mutation-m1-build-$RUN_TAG.log" 2>&1; then
      run_probe "m1-partial" "$MODEL" --fixture descriptor-not-triangles
      expect_red "m1: the malformed descriptor no longer refuses the model" \
        "$(pfield meshBuilt)" "1" "0"
      expect_red "m1: the refused range still occupies a slot in the table" \
        "$(pfield meshRanges)" "2" "0"
      expect_red "m1: the sleeve collapses to no vertices" \
        "$(built_field 0 count)" "0" "600"
      expect_red "m1: and to no material slot" \
        "$(built_field 0 material)" "-1" "0"
      expect_red "m1: the mesh loses the sleeve's 600 vertices" \
        "$(pfield meshVertices)" "9396" "9996"
      # The control: the unmodified model still builds the same two ranges, so the
      # readings above are about the defect and not about the mutation breaking
      # the builder outright.
      run_probe "m1-control" "$MODEL"
      assert_eq "m1 control: the intact model still builds" "$(pfield meshBuilt)" "1"
      assert_eq "m1 control: with both ranges" "$(pfield meshRanges)" "2"
      assert_eq "m1 control: and all 9996 vertices" "$(pfield meshVertices)" "9996"
    else
      echo "  FAIL partial-ranges-drawn: the mutated builder did not build"; fail=1
      FAIL_LINES+=("partial-ranges-drawn: build")
    fi
  fi
  end_mutation partial-ranges-drawn viewmodel_range_probe
  mutation_caught partial-ranges-drawn "$witness_before" "$fails_before"

  # -- M2: the pass draws only the first range -------------------------------
  echo "  -- first-range-drawn"
  fails_before=${#FAIL_LINES[@]}
  witness_before=$WITNESS_COUNT
  begin_mutation native/src/main.cpp
  # The F3 symptom itself: the buffer still holds every range, the pass issues one
  # Draw for the first. `vertices` stays 9996 -- that is the control, and it is
  # what separates "a range stopped being drawn" from "the geometry changed".
  if patch_line native/src/main.cpp \
      'for (const auto& source : request.mesh.ranges) {' \
      'for (const auto& source : request.mesh.ranges) { if (!ranges.empty()) break;' \
      'first-range-drawn'; then
    if bash build-target.sh tf2_demo_native > "$SCRATCH/range-mutation-m2-build-$RUN_TAG.log" 2>&1; then
      run_product m2-first-range
      assert_rc "m2 run" "$LAST_RC" "0"
      expect_red "m2: the pass issues one range" "$(field "$LAST_ORDER" ranges)" "1" "2"
      expect_red "m2: and binds one material slot" "$(field "$LAST_ORDER" boundSlots)" "1" "2"
      # The upload line counts the SAME list -- `gpu: viewmodel ranges ... ranges=`
      # is `ranges.size()` of the draw ranges, not of the mesh -- so it moves too.
      # The first version of this block asserted it still said 2 and went red on a
      # correct product: the assertion was wrong about where the number comes from.
      # What does not move is the buffer itself, asserted as the control below.
      expect_red "m2: the upload line also drops to one range" \
        "$(field "$LAST_RANGES" ranges)" "1" "2"
      assert_eq "m2 control: the buffer is unchanged" "$(field "$LAST_ORDER" vertices)" "9996"
      # The pixel witness: a counter moving is not a picture moving.
      expect_moved "m2: the frame changes when the hands stop being drawn" \
        "$LAST_BMP_HASH" "$BIND_HASH"
    else
      echo "  FAIL first-range-drawn: the mutated product did not build"; fail=1
      FAIL_LINES+=("first-range-drawn: build")
    fi
  fi
  end_mutation first-range-drawn tf2_demo_native
  mutation_caught first-range-drawn "$witness_before" "$fails_before"
fi

# ---------------------------------------------------------------------------
# Verdict
# ---------------------------------------------------------------------------
echo
echo "assertions: $OK_COUNT held, $FAIL_COUNT failed"
echo "host_overlap=$HOST_OVERLAP (a paused capture is insensitive to it; the repeat hash is the guard)"
if [ "$fail" -ne 0 ]; then
  echo "failed assertions:"
  for line in "${FAIL_LINES[@]}"; do echo "  $line"; done
  if [ "$MUTATION" -eq 1 ]; then
    echo "MUTATION-CAUGHT=FAIL ($MUTATION_CATCHES of $MUTATION_TOTAL caught)"
  else
    echo "VIEWMODEL-RANGE=FAIL"
  fi
  exit 1
fi
if [ "$MUTATION" -eq 1 ]; then
  if [ "$MUTATION_CATCHES" -ne "$MUTATION_TOTAL" ]; then
    echo "MUTATION-CAUGHT=FAIL ($MUTATION_CATCHES of $MUTATION_TOTAL caught)"
    exit 1
  fi
  echo "MUTATION-CAUGHT=PASS ($MUTATION_CATCHES of $MUTATION_TOTAL)"
else
  echo "VIEWMODEL-RANGE=PASS ($OK_COUNT OK, 0 FAIL)"
fi
exit 0
