#!/usr/bin/env bash
# viewmodel-robustness-check.sh -- T30: real multi-range / multi-LOD / damaged-
# companion models render correctly, or are refused by name.
#
# THE QUESTION
#   The ViewModel gates before this one each pinned one axis on one or two
#   models: viewmodel-range-check.sh pinned the nine c_*_arms range counts and
#   four malformed LOD-0 descriptors; viewmodel-presentation-check.sh pinned the
#   partition of one non-sniper model (v_scattergun_scout) and one body override;
#   viewmodel-animation-check.sh pinned the animation contract on the sniper.
#   T30 asks the cross-product of those axes over REAL installed inputs: does a
#   single-range model still draw, does a multi-range model bind a DIFFERENT VTF
#   per range, is a LOD-0 selection accounted for, and does a model whose
#   companion set is incomplete produce a NAMED, deterministic refusal instead
#   of a partial draw?
#
# WHAT IT PINS, AND WHY EACH NUMBER IS A READING
#   Section 1  the installed inventory, freshly measured in this worktree: the
#              nine arms models' range counts, the chosen weapon models' range,
#              bone and texture-slot counts, and the census of how many c_/v_
#              weapon models the install ships. The FULL sweep (every model) is
#              a separate committed artifact, model-inventory-sweep.sh ->
#              evidence/t30/inventory.tsv, because re-reading 100 models inside
#              every mutation run would cost minutes and buy nothing.
#   Section 2  the three real inputs at the probe level:
#                single-range   c-scattergun (one LOD-0 descriptor, one VTF)
#                multi-range    c-heavy_arms (two ranges, two different VTFs)
#                damaged        a REAL model's own bytes materialised loose with
#                               one companion omitted -- the shipped install has
#                               no such model, so the omission IS the input
#              plus the LOD-0 behaviour: a malformed LOD-0 descriptor is refused
#              by name, and an absent LOD-0 is reported as lodFallback=1 to the
#              next declared LOD instead of being silently treated as LOD 0.
#   Section 3  the product's own lines on the fixed tick, hidden window: the
#              bind/applied pair, the bone table coverage and the applied
#              transform extent, the single-range and multi-range body requests,
#              and the fixture-root refusals (a missing VVD and a missing VTX
#              must both be the SAME named refusal).
#   Section 4  pixels, not counters: the ViewModel's own coverage mask is the
#              difference between a ViewModel frame and the world-only frame at
#              the same tick; the pose change is judged inside that mask; the
#              multi-range model's forced-slot frames must partition its own
#              coverage; and bmp-mask-contact.py must find the arms still
#              touching the weapon body in the bind and the applied frame.
#   Section 5  four source mutations, one at a time, each restored byte for byte:
#              wrong range geometry, wrong material slot, a missing animation
#              companion silently treated as usable, and the bone table read as
#              its own transpose. Each is judged by a reading the forward gate
#              pins, and the restore is proven by sha256 before the rebuild.
#
# Usage: bash viewmodel-robustness-check.sh [--mutation]
# Exit:  0 = every assertion held (or, under --mutation, every defect was
#            caught and the tree was restored),
#        1 = it did not, 2 = another run owns the lock or an input is missing.
#
# One run at a time: mkdir is atomic, so a second caller fails before it runs
# anything. Every per-run file carries $RUN_TAG, so a refused rm cannot leave a
# previous run's frame in place for this run's assertions to read (the failure
# viewmodel-range-check.sh documents).
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

PROBE=native/build-nmake/model_robustness_probe.exe
EXE=native/build-nmake/tf2_demo_native.exe
CENSUS=native/build-nmake/vpk_query.exe
TF="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
INPUTS=evidence/viewmodel/animation-inputs.txt
ARMS_MODEL=models/weapons/c_models/c_sniper_arms.mdl
VM_MODEL=models/weapons/v_models/v_scattergun_scout.mdl
# The three real inputs. The paths and the numbers below were read through the
# probe on the pinned install (evidence/t30/inventory.tsv) and are pinned here
# so a content change cannot pass as the old model.
# single-range: the soldier's rocket launcher. One LOD-0 range, one VTF, four
# declared LODs, ten attachments -- a real non-sniper weapon whose whole mesh is
# one texture slot.
SINGLE_MODEL=models/weapons/c_models/c_rocketlauncher/c_rocketlauncher.mdl
SINGLE_RANGES=1
SINGLE_VERTICES=20748
SINGLE_VTF=c_rocketlauncher.vtf
# multi-range with different per-range VTFs: the medic's syringe gun. Two LOD-0
# ranges on slots 0 and 1, resolving to two different VTFs (the body and its
# glass), four bones, three attachments. Not a sniper model and not the pinned
# scattergun.
MULTI_MODEL=models/weapons/c_models/c_syringegun/c_syringegun.mdl
MULTI_RANGES=2
MULTI_VERTICES=25143
MULTI_VTFS=c_syringegun.vtf,c_syringegun_glass.vtf
# the LOD model: the same rocket launcher declares four LODs; removing LOD 0 must
# be reported as a fallback to LOD 1 (one descriptor, 13476 indices) rather than
# drawn as if it were LOD 0.
LOD_MODEL=models/weapons/c_models/c_rocketlauncher/c_rocketlauncher.mdl
LOD_COUNT=4
LOD_L1_DESC=1
TICK=0
POSE_SEQUENCE=1
RUN_TIMEOUT=300
RUN_TAG=$(date +%Y%m%d%H%M%S)-$$
EVIDENCE=${VIEWMODEL_ROBUSTNESS_EVIDENCE_DIR:-evidence/t30}
RUN_DIR=.scratch/t30/run-$RUN_TAG
mkdir -p "$EVIDENCE" "$RUN_DIR"
ROOT=$(cygpath -m "$(pwd)" 2>/dev/null || pwd)
LOCK_DIR=.scratch/viewmodel-robustness.lock
if ! mkdir "$LOCK_DIR" 2>/dev/null; then
  # A run that dies from a shell error still runs its EXIT trap, but the trap's
  # rmdir cannot remove a directory that still holds the pid file -- so the first
  # forward attempt left a lock and the next acquisition was refused before any
  # capture ("another run owns ..."), wasting the whole host-lock window. The lock
  # now records its owner pid and a lock whose owner is gone is reclaimed before
  # anything is run; the old code could not tell a live run from a dead one.
  holder=$(cat "$LOCK_DIR/pid" 2>/dev/null | tr -d '\r')
  if [ -n "$holder" ] && ! kill -0 "$holder" 2>/dev/null; then
    echo "  ..   reclaiming the lock of a dead run (pid $holder)"
    rm -rf "$LOCK_DIR"
  fi
  if ! mkdir "$LOCK_DIR" 2>/dev/null; then
    echo "VIEWMODEL-ROBUSTNESS=FAIL (another viewmodel-robustness-check run owns $LOCK_DIR, pid ${holder:-unknown})"
    exit 2
  fi
fi
echo $$ > "$LOCK_DIR/pid"

fail=0; OK_COUNT=0; FAIL_COUNT=0; FAIL_LINES=()
MUTATION_TOTAL=0; MUTATION_CATCHES=0; WITNESS_COUNT=0
MUTATION_FILE=""; MUTATION_BAK=""; MUTATION_SHA=""; MUTATION_ACTIVE=0
HOST_OVERLAP=0

pass() { echo "  OK   $1"; OK_COUNT=$((OK_COUNT + 1)); }
fail_line() {
  echo "  FAIL $1"; fail=1; FAIL_COUNT=$((FAIL_COUNT + 1)); FAIL_LINES+=("$1")
}
assert_eq() {
  if [ "${2:-<none>}" = "$3" ]; then pass "$1 = ${2:-<none>}"; else fail_line "$1 = ${2:-<none>}, expected $3"; fi
}
assert_ne() {
  if [ "${2:-<none>}" != "$3" ]; then pass "$1 = ${2:-<none>} (!= $3)"; else fail_line "$1 = ${2:-<none>}, expected it to differ from $3"; fi
}
assert_ge() {
  if [ -z "${2:-}" ]; then fail_line "$1 is missing, expected >= $3"
  elif awk -v a="$2" -v b="$3" 'BEGIN { exit !(a >= b) }'; then pass "$1 = $2 (>= $3)"
  else fail_line "$1 = $2, expected >= $3"; fi
}
assert_gt() {
  if [ -z "${2:-}" ]; then fail_line "$1 is missing, expected > $3"
  elif awk -v a="$2" -v b="$3" 'BEGIN { exit !(a > b) }'; then pass "$1 = $2 (> $3)"
  else fail_line "$1 = $2, expected > $3"; fi
}
assert_le() {
  if [ -z "${2:-}" ]; then fail_line "$1 is missing, expected <= $3"
  elif awk -v a="$2" -v b="$3" 'BEGIN { exit !(a <= b) }'; then pass "$1 = $2 (<= $3)"
  else fail_line "$1 = $2, expected <= $3"; fi
}
assert_contains() {
  case "${2:-}" in *"$3"*) pass "$1" ;; *) fail_line "$1 (no '$3' in: ${2:-<none>})" ;; esac
}

# A reading from a trace line: strip the millisecond prefix, take the last line
# matching the pattern, extract key=value. Field parsing reads the first match so
# a tool that prints two lines cannot let the second one answer (T18 section 8.4).
reading() { # reading <trace> <line-pattern> <key>
  tr -d '\r' < "$1" 2>/dev/null | grep -a "$2" | tail -1 \
    | sed 's/^[0-9]\{1,\}\(\.[0-9]*\)\? //' | grep -o "$3=[^ ]*" | head -1 | cut -d= -f2-
}
reading_line() { # reading_line <trace> <line-pattern>
  tr -d '\r' < "$1" 2>/dev/null | grep -a "$2" | tail -1 \
    | sed 's/^[0-9]\{1,\}\(\.[0-9]*\)\? //'
}
reason_of() { # the free-text reason is the last field on the line
  printf '%s\n' "$1" | sed 's/.* reason=//'
}
mask_field() { printf '%s\n' "$1" | head -1 | grep -o "$2=[0-9.-]*" | head -1 | cut -d= -f2-; }
mask_pair_field() { printf '%s\n' "$1" | head -1 | grep -o "$2=([^)]*)" | head -1; }

release_lock() { rm -rf "$LOCK_DIR" 2>/dev/null; }
restore_mutation() {
  [ "$MUTATION_ACTIVE" -eq 1 ] || return 0
  cp "$MUTATION_BAK" "$MUTATION_FILE"
  if ! cmp -s "$MUTATION_BAK" "$MUTATION_FILE"; then
    echo "  FATAL the mutation backup could not be restored to $MUTATION_FILE"; fail=1
  fi
  local now
  now=$(sha256sum "$MUTATION_FILE" | cut -d' ' -f1)
  if [ "$now" != "$MUTATION_SHA" ]; then
    echo "  FATAL $MUTATION_FILE is $now, was $MUTATION_SHA before the mutation"; fail=1
  else
    echo "  ..   $MUTATION_FILE restored byte-for-byte ($now)"
  fi
  rm -f "$MUTATION_BAK" 2>/dev/null
  MUTATION_ACTIVE=0
}
cleanup() { restore_mutation; release_lock; }
trap cleanup EXIT

# ---------------------------------------------------------------------------
# Inputs
# ---------------------------------------------------------------------------
if [ ! -x "$PROBE" ] || [ ! -x "$EXE" ] || [ ! -x "$CENSUS" ]; then
  echo "FATAL: missing binary: build model_robustness_probe tf2_demo_native vpk_query first"
  exit 2
fi
for tool in bmp-diff-mask.py bmp-mask-contact.py bmp-pixel-diff.py bmp-to-png.py patch-source.py; do
  if [ ! -f "$tool" ]; then echo "FATAL: missing tool: $tool"; exit 2; fi
done
if [ ! -f "$INPUTS" ]; then echo "FATAL: missing input: $INPUTS"; exit 2; fi
# A source tree that is already carrying an uncommitted product edit would make
# every capture below unattributable, and the mutation restore would replace the
# edit while claiming to restore it.
DIRTY=$(git status --porcelain -- native/ 2>/dev/null)
if [ -n "$DIRTY" ]; then
  echo "FATAL: native/ is not clean before the run:"; printf '%s\n' "$DIRTY"; exit 2
fi

echo "=== 0/6 the pinned recording, models and binaries ==="
DEMO=""; PINNED=0
while read -r size sha256 root name; do
  name=${name%$'\r'}
  case "$size" in ''|'#'*) continue ;; esac
  [ "$root" = steam ] || continue
  path="$TF/demos/$name"
  if [ ! -f "$path" ]; then fail_line "pinned recording is absent: $path"; continue; fi
  actual_size=$(stat -c%s "$path")
  actual_sha=$(sha256sum "$path" | cut -d' ' -f1)
  if [ "$actual_size" != "$size" ] || [ "$actual_sha" != "$sha256" ]; then
    fail_line "pinned recording $name is $actual_size/${actual_sha:0:16}..., pinned $size/${sha256:0:16}..."; continue
  fi
  pass "$name pinned ($size bytes)"
  PINNED=$((PINNED + 1))
  DEMO="$path"
done < "$INPUTS"
assert_eq "pinned recordings verified" "$PINNED" "1"
case "$DEMO" in
  *autorecord_2026-07-02_13-26-46.dem) pass "the POV recording is the pinned one" ;;
  *) fail_line "the resolved recording is not the pinned POV demo: $DEMO" ;;
esac
PROBE_HASH=$(sha256sum "$PROBE" | cut -d' ' -f1)
EXE_HASH=$(sha256sum "$EXE" | cut -d' ' -f1)
pass "probe sha256 recorded ($PROBE_HASH)"
pass "executable sha256 recorded ($EXE_HASH)"
OTHER_PRODUCT=$(ps -efW 2>/dev/null | grep "[t]f2_demo_native.exe" | head -1)
if [ -n "$OTHER_PRODUCT" ]; then HOST_OVERLAP=1; echo "  ..   another product run is live; the captures are paused:"; echo "  ..   $OTHER_PRODUCT"; fi

# ---------------------------------------------------------------------------
# Probe plumbing: one reading per model, named refusals on stderr
# ---------------------------------------------------------------------------
PROBE_RC=0; PROBE_ERR=""
probe_run() { # probe_run <name> <root> <model> [extra...]
  local name="$1" root="$2" model="$3"; shift 3
  "$PROBE" --tf-root "$root" --model "$model" "$@" > "$RUN_DIR/probe-$name.json" 2> "$RUN_DIR/probe-$name.txt"
  PROBE_RC=$?
  PROBE_ERR=$(tr -d '\r' < "$RUN_DIR/probe-$name.txt")
}
pfield() { printf '%s\n' "$PROBE_ERR" | grep -m1 '^robust model=' | grep -oE " $1=[^ ]*" | head -1 | sed 's/^ //' | cut -d= -f2-; }
pline_field() { # pline_field <descriptor> <key>
  printf '%s\n' "$PROBE_ERR" | grep -E "^robust range descriptor=$1 " | head -1 \
    | grep -oE " $2=[^ ]*" | head -1 | sed 's/^ //' | cut -d= -f2-
}
refusal_text() { printf '%s\n' "$PROBE_ERR" | grep '^robust refusal' | head -1 | sed 's/^robust refusal //'; }
probe_error() { printf '%s\n' "$PROBE_ERR" | grep -m1 '^robust model=' | sed 's/.* error=//'; }
companion_reason() { printf '%s\n' "$PROBE_ERR" | grep '^robust companion' | tail -1 | sed 's/.* reason=//'; }

MAT_ERR=""
probe_materialize() { # probe_materialize <name> <model> <dir> <drop-or-empty>
  local name="$1" model="$2" dir="$3" drop="$4"
  local args=(--tf-root "$TF" --model "$model" --materialize "$dir" --materialize-only)
  [ -n "$drop" ] && args+=(--drop "$drop")
  "$PROBE" "${args[@]}" > "$RUN_DIR/materialize-$name.json" 2> "$RUN_DIR/materialize-$name.txt"
  MAT_RC=$?
  MAT_ERR=$(tr -d '\r' < "$RUN_DIR/materialize-$name.txt")
}
mfield() { printf '%s\n' "$MAT_ERR" | grep -m1 '^robust materialize' | grep -oE " $1=[^ ]*" | head -1 | sed 's/^ //' | cut -d= -f2-; }

# ---------------------------------------------------------------------------
# Product plumbing
# ---------------------------------------------------------------------------
run_product() { # run_product <name> [extra args...]
  local name="$1"; shift
  rm -f "$RUN_DIR/$name.bmp" "$RUN_DIR/$name.trace"
  TF2_NATIVE_TRACE="$ROOT/$RUN_DIR/$name.trace" timeout "$RUN_TIMEOUT" "$EXE" \
    --tf-root "$TF" --audio-device "" --demo "$DEMO" \
    --hidden-window --audio-disabled \
    --settings-file "$RUN_DIR/$name-settings.ini" \
    --capture-frame "$RUN_DIR/$name.bmp" --capture-tick "$TICK" \
    --metrics-file "$RUN_DIR/$name.csv" --viewmodel-fov 90 --start-paused "$@" > "$RUN_DIR/$name.log" 2>&1
  LAST_RC=$?
  LAST_BMP="$RUN_DIR/$name.bmp"; LAST_TRACE="$RUN_DIR/$name.trace"
  LAST_BMP_BYTES=$(stat -c%s "$LAST_BMP" 2>/dev/null || echo 0)
  LAST_BMP_HASH=$(sha256sum "$LAST_BMP" 2>/dev/null | cut -d' ' -f1)
  echo "  $name rc=$LAST_RC bmp=$LAST_BMP_BYTES hash=${LAST_BMP_HASH:0:16}..."
}
run_fixture_product() { # run_fixture_product <name> <root> <model>
  local name="$1" root="$2" model="$3"
  rm -f "$RUN_DIR/$name.bmp" "$RUN_DIR/$name.trace"
  TF2_NATIVE_TRACE="$ROOT/$RUN_DIR/$name.trace" timeout "$RUN_TIMEOUT" "$EXE" \
    --tf-root "$root" --audio-device "" --start-paused --hidden-window --audio-disabled \
    --settings-file "$RUN_DIR/$name-settings.ini" \
    --viewmodel "$model" --viewmodel-fov 90 \
    --capture-frame "$RUN_DIR/$name.bmp" --capture-tick "$TICK" \
    --metrics-file "$RUN_DIR/$name.csv" > "$RUN_DIR/$name.log" 2>&1
  LAST_RC=$?
  LAST_BMP="$RUN_DIR/$name.bmp"; LAST_TRACE="$RUN_DIR/$name.trace"
  LAST_BMP_BYTES=$(stat -c%s "$LAST_BMP" 2>/dev/null || echo 0)
  LAST_BMP_HASH=$(sha256sum "$LAST_BMP" 2>/dev/null | cut -d' ' -f1)
  LAST_READING=$(grep -a 'viewModelPath=' "$LAST_TRACE" 2>/dev/null | tr -d '\r' | tail -1 \
    | sed 's/^[0-9]\{1,\}\(\.[0-9]*\)\? //')
  echo "  $name rc=$LAST_RC bmp=$LAST_BMP_BYTES hash=${LAST_BMP_HASH:0:16}..."
  [ -n "$LAST_READING" ] && echo "  $LAST_READING"
}

# ---------------------------------------------------------------------------
# Section 1 -- the installed inventory, measured in this worktree
# ---------------------------------------------------------------------------
echo "=== 1/6 installed models: ranges, slots, companions, animation source ==="
C_CENSUS=$("$CENSUS" "$TF" models/weapons/c_models .mdl 2>/dev/null | grep -c "^    ")
V_CENSUS=$("$CENSUS" "$TF" models/weapons/v_models .mdl 2>/dev/null | grep -c "^    ")
echo "  census: c_ weapon models=$C_CENSUS, v_ viewmodels=$V_CENSUS"
assert_ge "c_ weapon models in the install" "$C_CENSUS" 20
assert_eq "v_ viewmodels in the install" "$V_CENSUS" "37"

# The nine arms models, through the probe. The expected range counts are the
# content's own property; a builder that collapsed to one descriptor would move
# eight of the nine, which is why the set and not one model is asserted.
ARMS_ROWS=""; ARMS_BAD=0
for spec in scout:1 soldier:3 pyro:3 demo:2 heavy:2 engineer:4 medic:2 sniper:2 spy:1; do
  cls=${spec%%:*}; expected=${spec##*:}
  probe_run "arms-$cls" "$TF" "models/weapons/c_models/c_${cls}_arms.mdl"
  rc=$PROBE_RC; built=$(pfield meshBuilt); ranges=$(pfield meshRanges); desc=$(pfield targetDesc)
  verts=$(pfield meshVertices); idx=$(pfield targetIdx)
  ARMS_ROWS+="c_${cls}_arms.mdl rc=$rc built=$built descriptors=$desc ranges=$ranges vertices=$verts indices=$idx"$'\n'
  if [ "$rc" = "0" ] && [ "$built" = "1" ] && [ "$ranges" = "$expected" ] && [ "$verts" = "$idx" ] && [ "$desc" = "$ranges" ]; then
    pass "c_${cls}_arms.mdl: $ranges ranges, $verts vertices"
  else
    fail_line "c_${cls}_arms.mdl: rc=$rc built=$built descriptors=$desc ranges=$ranges (expected $expected) vertices=$verts indices=$idx"
    ARMS_BAD=$((ARMS_BAD + 1))
  fi
done
assert_eq "arms models that failed" "$ARMS_BAD" "0"
printf '%s' "$ARMS_ROWS" > "$RUN_DIR/inventory-arms.txt"

# The three chosen models and the two models the pixel section draws, on the
# same probe. Every number here is read from the file through the product's own
# loader, so a content change shows up as a named difference, not as a shape.
probe_run single "$TF" "$SINGLE_MODEL"
SINGLE_RC=$PROBE_RC
assert_eq "single-range input rc" "$SINGLE_RC" "0"
assert_eq "single-range input ranges" "$(pfield meshRanges)" "$SINGLE_RANGES"
assert_eq "single-range input target descriptors" "$(pfield targetDesc)" "$SINGLE_RANGES"
assert_eq "single-range input vertices" "$(pfield meshVertices)" "$SINGLE_VERTICES"
assert_eq "single-range input distinct range textures" "$(pfield distinctRangeTextures)" "$SINGLE_RANGES"
assert_eq "single-range input selected LOD" "$(pfield targetLod)" "0"
assert_eq "single-range input lod fallback" "$(pfield lodFallback)" "0"
assert_eq "single-range input refusal count" "$(printf '%s\n' "$PROBE_ERR" | grep -c '^robust refusal')" "0"
assert_contains "single-range input bound VTF" "$(pline_field 0 vtf)" "$SINGLE_VTF"
assert_eq "single-range input bad skin weights" "$(pline_field 0 badWeights)" "0"
SINGLE_SUMMARY=$(printf '%s\n' "$PROBE_ERR" | grep -m1 '^robust model=')
SINGLE_RANGE_LINE=$(printf '%s\n' "$PROBE_ERR" | grep -m1 '^robust range descriptor=0 ')

probe_run multi "$TF" "$MULTI_MODEL"
MULTI_RC=$PROBE_RC
assert_eq "multi-range input rc" "$MULTI_RC" "0"
assert_eq "multi-range input ranges" "$(pfield meshRanges)" "$MULTI_RANGES"
assert_eq "multi-range input target descriptors" "$(pfield targetDesc)" "$MULTI_RANGES"
assert_eq "multi-range input vertices" "$(pfield meshVertices)" "$MULTI_VERTICES"
assert_eq "multi-range input distinct range materials" "$(pfield distinctRangeMaterials)" "$MULTI_RANGES"
assert_eq "multi-range input distinct range textures" "$(pfield distinctRangeTextures)" "$MULTI_RANGES"
assert_eq "multi-range input selected LOD" "$(pfield targetLod)" "0"
assert_eq "multi-range input refusal count" "$(printf '%s\n' "$PROBE_ERR" | grep -c '^robust refusal')" "0"
# Every range must resolve to its own texture: a decoder that bound slot 0 twice
# would still report two ranges, and only the vtfStatus lines separate them.
MULTI_RANGE_LINES=$(printf '%s\n' "$PROBE_ERR" | grep '^robust range descriptor=')
MULTI_RESOLVED=$(printf '%s\n' "$MULTI_RANGE_LINES" | grep -c 'vtfStatus=resolved')
assert_eq "multi-range input ranges whose VTF resolved" "$MULTI_RESOLVED" "$MULTI_RANGES"
MULTI_BADWEIGHTS=$(printf '%s\n' "$MULTI_RANGE_LINES" | grep -o 'badWeights=[0-9]*' | cut -d= -f2 | awk '{s+=$1} END {print s+0}')
assert_eq "multi-range input vertices with bad skin weights" "$MULTI_BADWEIGHTS" "0"
MULTI_VTF_LIST=$(printf '%s\n' "$MULTI_RANGE_LINES" | sed -n 's/.* vtf=\([^ ]*\) .*/\1/p' | sed 's#.*/##' | paste -sd,)
assert_eq "multi-range input per-range VTFs, in range order" "$MULTI_VTF_LIST" "$MULTI_VTFS"
MULTI_BONES=$(pfield bones)
MULTI_MAXBONE=$(printf '%s\n' "$MULTI_RANGE_LINES" | sed -n 's/.* maxBone=\([0-9]*\) .*/\1/p' | sort -n | tail -1)
if [ -n "$MULTI_MAXBONE" ] && [ "$MULTI_MAXBONE" -lt "$MULTI_BONES" ]; then
  pass "multi-range input every range's highest bone is inside the skeleton ($MULTI_MAXBONE < $MULTI_BONES)"
else
  fail_line "multi-range input highest bone $MULTI_MAXBONE is not inside a $MULTI_BONES-bone skeleton"
fi
MULTI_SUMMARY=$(printf '%s\n' "$PROBE_ERR" | grep -m1 '^robust model=')

probe_run lod "$TF" "$LOD_MODEL"
assert_eq "multi-LOD input rc" "$PROBE_RC" "0"
assert_eq "multi-LOD input declared LODs" "$(pfield lodCount)" "$LOD_COUNT"
assert_eq "multi-LOD input selected LOD" "$(pfield targetLod)" "0"
assert_eq "multi-LOD input lod fallback" "$(pfield lodFallback)" "0"
LOD_SUMMARY=$(printf '%s\n' "$PROBE_ERR" | grep -m1 '^robust model=')
probe_run animation-source "$TF" "$ARMS_MODEL"
assert_eq "pinned arms input animation source resolves" "$(pfield animationInput)" "ok"
assert_eq "pinned arms input animation companion is separate" "$(pfield separateAnimSource)" "1"
assert_contains "pinned arms input animation companion path" "$(pfield animPath)" "c_sniper_animations.mdl"
assert_contains "pinned arms input animation companion is declared" "$(pfield animPath)" "models/weapons/c_models/"
PRIOR_BOUND=$(pfield boundSeqs)
assert_gt "pinned arms input sequences bound by name" "$PRIOR_BOUND" 0
assert_contains "pinned arms input bind matrices cover the skeleton" "$(pfield bindBones)" "/"

{
  echo "# T30 inventory readings: curated sweep ($(date -u +%Y-%m-%dT%H:%M:%SZ))"
  echo "# probe: $PROBE (sha256 $PROBE_HASH)"
  echo "# tf root: $TF"
  echo "# command: model_robustness_probe --tf-root <tf> --model <each>"
  echo "# full install sweep: evidence/t30/inventory.tsv (model-inventory-sweep.sh)"
  echo "census c_weapon_models=$C_CENSUS v_viewmodels=$V_CENSUS"
  printf '%s' "$ARMS_ROWS"
  echo "$SINGLE_SUMMARY"
  echo "$SINGLE_RANGE_LINE"
  echo "$MULTI_SUMMARY"
  printf '%s\n' "$MULTI_RANGE_LINES"
  echo "$LOD_SUMMARY"
} > "$EVIDENCE/inventory.txt"

# ---------------------------------------------------------------------------
# Section 2 -- the three real inputs, and the damaged companion that is the input
# ---------------------------------------------------------------------------
echo "=== 2/6 three real inputs: single-range, multi-range, damaged companion ==="
# ---- (a) the damaged companion is a REAL model with one file omitted --------
# The shipped install has no model with an incomplete companion set, so the
# missing-companion input is made by materialising the real bytes and dropping
# one file. The bytes are still the shipped model's; only the set is damaged.
probe_materialize vm-ok "$VM_MODEL" "$RUN_DIR/fixture-vm-ok" ""
assert_eq "fixture vm-ok materialised" "$MAT_RC" "0"
assert_eq "fixture vm-ok omits nothing" "$(mfield dropped)" "-"
assert_ge "fixture vm-ok files written" "$(mfield written)" 3
probe_materialize vm-no-vvd "$VM_MODEL" "$RUN_DIR/fixture-vm-no-vvd" vvd
assert_eq "fixture vm-no-vvd materialised" "$MAT_RC" "0"
assert_eq "fixture vm-no-vvd omits the VVD" "$(mfield dropped)" "vvd"
probe_materialize vm-no-vtx "$VM_MODEL" "$RUN_DIR/fixture-vm-no-vtx" vtx
assert_eq "fixture vm-no-vtx omits the VTX" "$(mfield dropped)" "vtx"
probe_materialize arms-ok "$ARMS_MODEL" "$RUN_DIR/fixture-arms-ok" ""
assert_eq "fixture arms-ok materialised" "$MAT_RC" "0"
ARMS_ANIMATION=$(mfield animations)
assert_contains "fixture arms-ok carries the declared animation companion" "$ARMS_ANIMATION" "c_sniper_animations.mdl"
probe_materialize arms-no-anim "$ARMS_MODEL" "$RUN_DIR/fixture-arms-no-anim" animations
assert_eq "fixture arms-no-anim materialised" "$MAT_RC" "0"
assert_contains "fixture arms-no-anim omits the declared animation companion" "$(mfield animations)" "c_sniper_animations.mdl"

# ---- (b) the probe on the complete fixture: the real bytes still load --------
probe_run fixture-vm-ok "$RUN_DIR/fixture-vm-ok" "$VM_MODEL"
assert_eq "probe on the complete fixture rc" "$PROBE_RC" "0"
assert_eq "probe on the complete fixture resolution" "$(pfield resolution)" "found-loose"
assert_eq "probe on the complete fixture companions" "$(pfield companions)" "1"
assert_eq "probe on the complete fixture mesh built" "$(pfield meshBuilt)" "1"
assert_eq "probe on the complete fixture ranges" "$(pfield meshRanges)" "2"
probe_run fixture-vm-no-vvd "$RUN_DIR/fixture-vm-no-vvd" "$VM_MODEL"
assert_eq "probe on the no-VVD fixture resolution" "$(pfield resolution)" "found-loose"
assert_eq "probe on the no-VVD fixture mdl present" "$(pfield mdlFound)" "1"
assert_eq "probe on the no-VVD fixture vvd absent" "$(pfield vvd)" "0"
assert_eq "probe on the no-VVD fixture companions incomplete" "$(pfield companions)" "0"
assert_eq "probe on the no-VVD fixture refuses the mesh" "$(pfield meshBuilt)" "0"
assert_contains "probe on the no-VVD fixture names a refusal" "$(refusal_text)" "renderable resource set"
probe_run fixture-vm-no-vtx "$RUN_DIR/fixture-vm-no-vtx" "$VM_MODEL"
assert_eq "probe on the no-VTX fixture vtx absent" "$(pfield vtx)" "0"
assert_eq "probe on the no-VTX fixture companions incomplete" "$(pfield companions)" "0"

# ---- (c) the animation companion: present vs absent -------------------------
probe_run fixture-arms-ok "$RUN_DIR/fixture-arms-ok" "$ARMS_MODEL"
assert_eq "probe on the complete arms fixture rc" "$PROBE_RC" "0"
assert_eq "probe on the complete arms fixture companions" "$(pfield companions)" "1"
assert_eq "probe on the complete arms fixture animation source" "$(pfield animationInput)" "ok"
assert_eq "probe on the complete arms fixture separate animation source" "$(pfield separateAnimSource)" "1"
assert_contains "probe on the complete arms fixture animation path" "$(pfield animPath)" "c_sniper_animations.mdl"
assert_gt "probe on the complete arms fixture bound sequences" "$(pfield boundSeqs)" 0
ARMS_BIND_BONES=$(pfield bindBones)
assert_eq "probe on the complete arms fixture bind matrices cover the skeleton" "${ARMS_BIND_BONES%%/*}" "58"
assert_eq "probe on the complete arms fixture skeleton is 58 bones" "${ARMS_BIND_BONES##*/}" "58"
assert_gt "probe on the complete arms fixture animation set is larger than the stub" "$(pfield animSeqs)" 1
assert_gt "probe on the complete arms fixture bound sequences" "$(pfield boundSeqs)" 1
probe_run fixture-arms-no-anim "$RUN_DIR/fixture-arms-no-anim" "$ARMS_MODEL"
# T6's route-1 rule: a model whose own table carries even a stub animation is
# considered to animate itself, and the declared companion is an OPTIONAL
# improvement. So an absent animation companion is not a refusal -- it is a
# DEGRADATION to the base model's stub table, and the readings have to name it:
# separateAnimSource drops 1 -> 0, the animation count drops from the companion's
# set to the stub's one, and the bound-sequence count follows. That is the
# deterministic, named reading this gate pins; the report states that the
# product still draws a stub pose in this state, which is a limitation, not a
# pass.
assert_eq "probe on the no-animation-companion fixture rc" "$PROBE_RC" "0"
assert_eq "probe on the no-animation-companion fixture mesh still builds" "$(pfield meshBuilt)" "1"
assert_eq "probe on the no-animation-companion fixture ranges" "$(pfield meshRanges)" "2"
assert_eq "probe on the no-animation-companion fixture is not a separate source" "$(pfield separateAnimSource)" "0"
assert_eq "probe on the no-animation-companion fixture degrades to the stub table" "$(pfield animSeqs)" "1"
assert_eq "probe on the no-animation-companion fixture binds only the stub sequence" "$(pfield boundSeqs)" "1"
assert_eq "probe on the no-animation-companion fixture animation path is the model itself" "$(pfield animPath)" "$ARMS_MODEL"

# ---- (d) LOD-0: a malformed LOD-0 refuses, an absent LOD-0 is a named fallback --
probe_run lod-malformed "$TF" "$LOD_MODEL" --fixture descriptor-not-triangles
assert_eq "malformed LOD-0 descriptor rc" "$PROBE_RC" "1"
assert_eq "malformed LOD-0 descriptor builds no mesh" "$(pfield meshBuilt)" "0"
assert_contains "malformed LOD-0 descriptor refusal names the descriptor" "$(refusal_text)" "was refused"
assert_contains "malformed LOD-0 descriptor refusal names the cause" "$(refusal_text)" "not a bounded triangle list"
probe_run lod-absent "$TF" "$LOD_MODEL" --fixture lod0-absent
assert_eq "absent LOD-0 falls back instead of refusing" "$PROBE_RC" "0"
assert_eq "absent LOD-0 selected LOD" "$(pfield targetLod)" "1"
assert_eq "absent LOD-0 is reported as a fallback" "$(pfield lodFallback)" "1"
assert_eq "absent LOD-0 still builds the mesh" "$(pfield meshBuilt)" "1"
assert_eq "absent LOD-0 range count is the next LOD's" "$(pfield targetDesc)" "$LOD_L1_DESC"

# ---------------------------------------------------------------------------
# Section 3 -- the product on the pinned recording, hidden window, paused tick
# ---------------------------------------------------------------------------
echo "=== 3/6 the product: bind/applied pose, single and multi range bodies ==="
BIND_HASH=""; APPLIED_HASH=""; MULTI_HASH=""; WORLD_HASH=""
BIND_ORDER=""; APPLIED_ORDER=""; MULTI_MATERIAL=""; SINGLE_MATERIAL=""

if [ "$MUTATION" -eq 1 ]; then
  echo "  -- mutation baselines from the unmodified tree"
  run_product bind --viewmodel "$VM_MODEL" --viewmodel-sequence "$POSE_SEQUENCE"
  [ "$LAST_RC" -eq 0 ] || fail_line "baseline bind exited $LAST_RC"
  BIND_HASH=$LAST_BMP_HASH; BIND_TRACE=$LAST_TRACE; BIND_BMP=$LAST_BMP
  run_product world --viewmodel-fov 90
  [ "$LAST_RC" -eq 0 ] || fail_line "baseline world exited $LAST_RC"
  WORLD_HASH=$LAST_BMP_HASH; WORLD_BMP=$LAST_BMP
  run_product applied --viewmodel "$VM_MODEL" --viewmodel-sequence "$POSE_SEQUENCE" --viewmodel-apply-animation
  [ "$LAST_RC" -eq 0 ] || fail_line "baseline applied exited $LAST_RC"
  APPLIED_HASH=$LAST_BMP_HASH; APPLIED_BMP=$LAST_BMP
  run_product multi --viewmodel "$VM_MODEL" --viewmodel-sequence "$POSE_SEQUENCE" --viewmodel-weapon-model "$MULTI_MODEL"
  [ "$LAST_RC" -eq 0 ] || fail_line "baseline multi exited $LAST_RC"
  MULTI_HASH=$LAST_BMP_HASH
  MULTI_TRACE=$LAST_TRACE
  MULTI_KEYS_ACTUAL=$(reading "$MULTI_TRACE" 'gpu: viewmodel weaponBodyMaterial' keys)
else
  run_product bind --viewmodel "$VM_MODEL" --viewmodel-sequence "$POSE_SEQUENCE"
  [ "$LAST_RC" -eq 0 ] || fail_line "bind run exited $LAST_RC"
  assert_ge "bind frame bytes" "$LAST_BMP_BYTES" 1024
  BIND_HASH=$LAST_BMP_HASH; BIND_TRACE=$LAST_TRACE; BIND_BMP=$LAST_BMP
  assert_eq "bind request status" "$(reading "$BIND_TRACE" 'gpu: viewmodel request' status)" "ok"
  assert_eq "bind request companions" "$(reading "$BIND_TRACE" 'gpu: viewmodel request' companions)" "1"
  assert_eq "bind request mesh ready" "$(reading "$BIND_TRACE" 'gpu: viewmodel request' meshReady)" "1"
  assert_eq "bind arms ranges" "$(reading "$BIND_TRACE" 'gpu: viewmodel ranges' ranges)" "2"
  assert_eq "bind arms range slots" "$(reading "$BIND_TRACE" 'gpu: viewmodel ranges' rangeSlots)" "0,1"
  assert_eq "bind arms range vertices" "$(reading "$BIND_TRACE" 'gpu: viewmodel ranges' rangeVertices)" "600,9396"
  assert_eq "bind arms both ranges textured" "$(reading "$BIND_TRACE" 'gpu: viewmodel ranges' rangeTextured)" "1,1"
  BIND_ORDER=$(reading_line "$BIND_TRACE" 'gpu: viewmodel pass order')
  assert_eq "bind one ViewModel draw call" "$(reading "$BIND_TRACE" 'gpu: viewmodel pass order' drawCalls)" "1"
  assert_eq "bind drawn model is the entity's" "$(reading "$BIND_TRACE" 'gpu: viewmodel pass order' drawnModel)" "$ARMS_MODEL"
  assert_eq "bind pass ranges" "$(reading "$BIND_TRACE" 'gpu: viewmodel pass order' ranges)" "2"
  assert_eq "bind pass distinct material slots" "$(reading "$BIND_TRACE" 'gpu: viewmodel pass order' boundSlots)" "2"
  assert_eq "bind no pose applied" "$(reading "$BIND_TRACE" 'gpu: viewmodel pass order' vmAnimApplied)" "0"
  assert_eq "bind no skinning" "$(reading "$BIND_TRACE" 'gpu: viewmodel pass order' skinning)" "0"
  assert_eq "bind animation input resolves" "$(reading "$BIND_TRACE" 'gpu: viewmodel pass order' animationInput)" "ok"

  run_product applied --viewmodel "$VM_MODEL" --viewmodel-sequence "$POSE_SEQUENCE" --viewmodel-apply-animation
  [ "$LAST_RC" -eq 0 ] || fail_line "applied run exited $LAST_RC"
  APPLIED_HASH=$LAST_BMP_HASH; APPLIED_TRACE=$LAST_TRACE; APPLIED_BMP=$LAST_BMP
  APPLIED_ORDER=$(reading_line "$APPLIED_TRACE" 'gpu: viewmodel pass order')
  assert_eq "applied pose applied" "$(reading "$APPLIED_TRACE" 'gpu: viewmodel pass order' vmAnimApplied)" "1"
  assert_eq "applied pass skins" "$(reading "$APPLIED_TRACE" 'gpu: viewmodel pass order' skinning)" "1"
  assert_eq "applied bone table covers the skeleton" "$(reading "$APPLIED_TRACE" 'gpu: viewmodel pass order' boneTableCoverage)" "58/58"
  assert_eq "applied sequence is inside the drawn model's table" "$(reading "$APPLIED_TRACE" 'gpu: viewmodel pass order' sequenceInRange)" "1"
  assert_eq "applied pose samples a frame" "$(reading "$APPLIED_TRACE" 'gpu: viewmodel pass order' bindPose)" "0"
  assert_eq "applied one ViewModel draw call" "$(reading "$APPLIED_TRACE" 'gpu: viewmodel pass order' drawCalls)" "1"
  assert_eq "applied model matches the entity's" "$(reading "$APPLIED_TRACE" 'gpu: viewmodel pass order' entityModelMatchesDrawn)" "1"
  POSE_VERTICES=$(reading "$APPLIED_TRACE" 'gpu: viewmodel pose extent' vertices)
  POSE_POSED=$(reading "$APPLIED_TRACE" 'gpu: viewmodel pose extent' posedVertices)
  assert_eq "pose extent is valid" "$(reading "$APPLIED_TRACE" 'gpu: viewmodel pose extent' valid)" "1"
  assert_eq "pose extent vertices equal the mesh's" "$POSE_VERTICES" "9996"
  assert_eq "every vertex was posed" "$POSE_POSED" "$POSE_VERTICES"
  assert_eq "no posed vertex is non-finite" "$(reading "$APPLIED_TRACE" 'gpu: viewmodel pose extent' nonFinite)" "0"
  POSE_DISPLACEMENT=$(reading "$APPLIED_TRACE" 'gpu: viewmodel pose extent' maxDisplacement)
  POSE_RADIUS=$(reading "$APPLIED_TRACE" 'gpu: viewmodel pose extent' bindRadius)
  POSE_SHIFT=$(reading "$APPLIED_TRACE" 'gpu: viewmodel pose extent' centerShift)
  assert_gt "the pose moves geometry" "$POSE_DISPLACEMENT" 0.01
  assert_le "the pose moves no vertex further than the model is wide" "$POSE_DISPLACEMENT" "$POSE_RADIUS"
  assert_le "the pose does not move the framing anchor" "$POSE_SHIFT" 5
  assert_ne "the applied frame differs from the bind frame" "$APPLIED_HASH" "$BIND_HASH"
  # The camera and the framing are the control: if the pose moved the camera, the
  # frame difference above would not be a pose difference.
  BIND_CAMERA=$(reading_line "$BIND_TRACE" 'gpu: viewmodel frame')
  APPLIED_CAMERA=$(reading_line "$APPLIED_TRACE" 'gpu: viewmodel frame')
  assert_eq "the camera is byte-identical between bind and applied" "$APPLIED_CAMERA" "$BIND_CAMERA"
  BIND_CENTRE=$(reading_line "$BIND_TRACE" 'gpu: viewmodel centre viewSpace')
  APPLIED_CENTRE=$(reading_line "$APPLIED_TRACE" 'gpu: viewmodel centre viewSpace')
  assert_eq "the placement centre is byte-identical between bind and applied" "$APPLIED_CENTRE" "$BIND_CENTRE"

  run_product repeat --viewmodel "$VM_MODEL" --viewmodel-sequence "$POSE_SEQUENCE" --viewmodel-apply-animation
  assert_eq "the applied frame repeats byte for byte" "$LAST_BMP_HASH" "$APPLIED_HASH"

  run_product world --viewmodel-fov 90
  [ "$LAST_RC" -eq 0 ] || fail_line "world run exited $LAST_RC"
  WORLD_HASH=$LAST_BMP_HASH; WORLD_BMP=$LAST_BMP
  assert_ne "the world-only frame differs from the ViewModel frame" "$WORLD_HASH" "$BIND_HASH"

  run_product bodyoff --viewmodel "$VM_MODEL" --viewmodel-sequence "$POSE_SEQUENCE" --viewmodel-weapon-body off
  [ "$LAST_RC" -eq 0 ] || fail_line "body-off run exited $LAST_RC"
  BODYOFF_BMP=$LAST_BMP
  assert_eq "body-off: the body is switched off, not refused" "$(reading "$LAST_TRACE" 'gpu: viewmodel weaponBodyReading' weaponVisible)" "0"

  run_product applyoff --viewmodel "$VM_MODEL" --viewmodel-sequence "$POSE_SEQUENCE" --viewmodel-apply-animation --viewmodel-weapon-body off
  [ "$LAST_RC" -eq 0 ] || fail_line "applied body-off run exited $LAST_RC"
  APPLYOFF_BMP=$LAST_BMP

  # ---- the single-range real model, drawn as the weapon body ----------------
  run_product single --viewmodel "$VM_MODEL" --viewmodel-sequence "$POSE_SEQUENCE" --viewmodel-weapon-model "$SINGLE_MODEL"
  [ "$LAST_RC" -eq 0 ] || fail_line "single-range body run exited $LAST_RC"
  SINGLE_BMP=$LAST_BMP; SINGLE_TRACE=$LAST_TRACE
  SINGLE_MATERIAL=$(reading_line "$SINGLE_TRACE" 'gpu: viewmodel weaponBodyMaterial')
  assert_contains "single-range body model is the pinned one" "$(reading "$SINGLE_TRACE" 'gpu: viewmodel weaponBody' path)" "$SINGLE_MODEL"
  assert_eq "single-range body request ok" "$(reading "$SINGLE_TRACE" 'gpu: viewmodel weaponBody' status)" "ok"
  assert_eq "single-range body mesh ready" "$(reading "$SINGLE_TRACE" 'gpu: viewmodel weaponBody' meshReady)" "1"
  assert_eq "single-range body vertices" "$(reading "$SINGLE_TRACE" 'gpu: viewmodel weaponBody' vertices)" "$SINGLE_VERTICES"
  assert_eq "single-range body is an override" "$(reading "$SINGLE_TRACE" 'gpu: viewmodel weaponBodyMaterial' override)" "1"
  assert_eq "single-range body ranges" "$(reading "$SINGLE_TRACE" 'gpu: viewmodel weaponBodyMaterial' ranges)" "$SINGLE_RANGES"
  assert_eq "single-range body textured ranges" "$(reading "$SINGLE_TRACE" 'gpu: viewmodel weaponBodyMaterial' texturedRanges)" "$SINGLE_RANGES"
  assert_eq "single-range body fallback ranges" "$(reading "$SINGLE_TRACE" 'gpu: viewmodel weaponBodyMaterial' fallbackRanges)" "0"
  assert_eq "single-range body distinct slots" "$(reading "$SINGLE_TRACE" 'gpu: viewmodel weaponBodyMaterial' boundSlots)" "$SINGLE_RANGES"
  assert_contains "single-range body keys name its own VTF" "$(reading "$SINGLE_TRACE" 'gpu: viewmodel weaponBodyMaterial' keys)" "$SINGLE_VTF"
  assert_ge "single-range body draw calls" "$(reading "$SINGLE_TRACE" 'gpu: viewmodel weaponBodyReading' weaponDrawCalls)" 1
  assert_eq "single-range body visible" "$(reading "$SINGLE_TRACE" 'gpu: viewmodel weaponBodyReading' weaponVisible)" "1"

  # ---- the multi-range real model, drawn as the weapon body ------------------
  run_product multi --viewmodel "$VM_MODEL" --viewmodel-sequence "$POSE_SEQUENCE" --viewmodel-weapon-model "$MULTI_MODEL"
  [ "$LAST_RC" -eq 0 ] || fail_line "multi-range body run exited $LAST_RC"
  MULTI_BMP=$LAST_BMP; MULTI_TRACE=$LAST_TRACE; MULTI_HASH=$LAST_BMP_HASH
  MULTI_MATERIAL=$(reading_line "$MULTI_TRACE" 'gpu: viewmodel weaponBodyMaterial')
  assert_contains "multi-range body model is the pinned one" "$(reading "$MULTI_TRACE" 'gpu: viewmodel weaponBody' path)" "$MULTI_MODEL"
  assert_eq "multi-range body request ok" "$(reading "$MULTI_TRACE" 'gpu: viewmodel weaponBody' status)" "ok"
  assert_eq "multi-range body mesh ready" "$(reading "$MULTI_TRACE" 'gpu: viewmodel weaponBody' meshReady)" "1"
  assert_eq "multi-range body vertices" "$(reading "$MULTI_TRACE" 'gpu: viewmodel weaponBody' vertices)" "$MULTI_VERTICES"
  assert_eq "multi-range body is an override" "$(reading "$MULTI_TRACE" 'gpu: viewmodel weaponBodyMaterial' override)" "1"
  assert_eq "multi-range body ranges" "$(reading "$MULTI_TRACE" 'gpu: viewmodel weaponBodyMaterial' ranges)" "$MULTI_RANGES"
  assert_eq "multi-range body textured ranges" "$(reading "$MULTI_TRACE" 'gpu: viewmodel weaponBodyMaterial' texturedRanges)" "$MULTI_RANGES"
  assert_eq "multi-range body fallback ranges" "$(reading "$MULTI_TRACE" 'gpu: viewmodel weaponBodyMaterial' fallbackRanges)" "0"
  assert_eq "multi-range body distinct slots" "$(reading "$MULTI_TRACE" 'gpu: viewmodel weaponBodyMaterial' boundSlots)" "$MULTI_RANGES"
  MULTI_KEYS_ACTUAL=$(reading "$MULTI_TRACE" 'gpu: viewmodel weaponBodyMaterial' keys)
  MULTI_KEYS_BASENAMES=$(printf '%s' "$MULTI_KEYS_ACTUAL" | tr ',' '\n' | sed 's#.*/##' | paste -sd,)
  assert_eq "multi-range body keys name one different VTF per range" "$MULTI_KEYS_BASENAMES" "$MULTI_VTFS"
  assert_ge "multi-range body draw calls" "$(reading "$MULTI_TRACE" 'gpu: viewmodel weaponBodyReading' weaponDrawCalls)" 1
  assert_eq "multi-range body visible" "$(reading "$MULTI_TRACE" 'gpu: viewmodel weaponBodyReading' weaponVisible)" "1"
  assert_ne "the multi-range body frame differs from the single-range one" "$MULTI_HASH" "$(sha256sum "$SINGLE_BMP" | cut -d' ' -f1)"

  run_product mslot0 --viewmodel "$VM_MODEL" --viewmodel-sequence "$POSE_SEQUENCE" --viewmodel-weapon-model "$MULTI_MODEL" --viewmodel-weapon-material-slot 0
  [ "$LAST_RC" -eq 0 ] || fail_line "multi-range forced slot 0 run exited $LAST_RC"
  MSLOT0_BMP=$LAST_BMP
  assert_eq "forced body slot 0 is reported" "$(reading "$LAST_TRACE" 'gpu: viewmodel weaponBodyMaterial' slotOverride)" "0"
  assert_eq "forced body slot 0 binds one slot" "$(reading "$LAST_TRACE" 'gpu: viewmodel weaponBodyMaterial' boundSlots)" "1"
  run_product mslot1 --viewmodel "$VM_MODEL" --viewmodel-sequence "$POSE_SEQUENCE" --viewmodel-weapon-model "$MULTI_MODEL" --viewmodel-weapon-material-slot 1
  [ "$LAST_RC" -eq 0 ] || fail_line "multi-range forced slot 1 run exited $LAST_RC"
  MSLOT1_BMP=$LAST_BMP
  assert_eq "forced body slot 1 is reported" "$(reading "$LAST_TRACE" 'gpu: viewmodel weaponBodyMaterial' slotOverride)" "1"
  assert_eq "forced body slot 1 binds one slot" "$(reading "$LAST_TRACE" 'gpu: viewmodel weaponBodyMaterial' boundSlots)" "1"

  # ---- the fixture-root refusals: a real model with one companion missing ---
  run_fixture_product fixture-ok "$RUN_DIR/fixture-vm-ok" "$VM_MODEL"
  FIXTURE_OK_HASH=$LAST_BMP_HASH; FIXTURE_OK_BMP=$LAST_BMP
  assert_eq "fixture complete: request status" "$(reading "$LAST_TRACE" 'gpu: viewmodel request' status)" "ok"
  assert_eq "fixture complete: request mesh ready" "$(reading "$LAST_TRACE" 'gpu: viewmodel request' meshReady)" "1"
  assert_contains "fixture complete: the real model draws" "$LAST_READING" "resourceStatus=ok"
  assert_gt "fixture complete: draw counter" "$(printf '%s\n' "$LAST_READING" | sed -n 's/.* drawn=\([0-9]*\) reason=.*/\1/p')" 0
  assert_eq "fixture complete: the pass drew" "$(reason_of "$LAST_READING")" "drawn"
  run_fixture_product fixture-no-vvd "$RUN_DIR/fixture-vm-no-vvd" "$VM_MODEL"
  FIXTURE_NOVVD_HASH=$LAST_BMP_HASH; FIXTURE_NOVVD_BMP=$LAST_BMP
  assert_eq "fixture no-VVD: request status" "$(reading "$LAST_TRACE" 'gpu: viewmodel request' status)" "failed"
  assert_eq "fixture no-VVD: the refusal is named" "$(reason_of "$LAST_READING")" "viewmodel companions are missing"
  assert_contains "fixture no-VVD: the resource reading names the missing VVD" "$LAST_READING" "vvd=0"
  assert_eq "fixture no-VVD: nothing is drawn" "$(printf '%s\n' "$LAST_READING" | sed -n 's/.* drawn=\([0-9]*\) reason=.*/\1/p')" "0"
  assert_eq "fixture no-VVD: no range table is published" "$(grep -ac 'gpu: viewmodel ranges' "$LAST_TRACE")" "0"
  assert_ne "fixture no-VVD: the refusal changes the picture" "$FIXTURE_NOVVD_HASH" "$FIXTURE_OK_HASH"
  run_fixture_product fixture-no-vtx "$RUN_DIR/fixture-vm-no-vtx" "$VM_MODEL"
  assert_eq "fixture no-VTX: request status" "$(reading "$LAST_TRACE" 'gpu: viewmodel request' status)" "failed"
  assert_eq "fixture no-VTX: the refusal is the same named one" "$(reason_of "$LAST_READING")" "viewmodel companions are missing"
  assert_contains "fixture no-VTX: the resource reading names the missing VTX" "$LAST_READING" "vtx=0"
  assert_eq "fixture no-VTX: nothing is drawn" "$(printf '%s\n' "$LAST_READING" | sed -n 's/.* drawn=\([0-9]*\) reason=.*/\1/p')" "0"
fi

# ---------------------------------------------------------------------------
# Section 4 -- pixels: the ViewModel's own coverage, containment and contact
# ---------------------------------------------------------------------------
echo "=== 4/6 pixels: coverage mask, pose containment, contact, body partition ==="
COVER_LINE=""; BODY_LINE=""; ARMS_LINE=""; POSE_LINE=""; CONTACT_BIND_LINE=""; CONTACT_APPLIED_LINE=""
MS0_LINE=""; MS1_LINE=""; MU_LINE=""; OVERLAP_LINE=""; SINGLE_COVER_LINE=""
if [ "$MUTATION" -eq 0 ]; then
  # The mask is the ViewModel's own coverage: a pixel where the ViewModel frame and
  # the world-only frame differ. Everything the pose is allowed to change must be
  # inside it, which is what makes 'outside <= 64' the claim rather than a box.
  COVER_LINE=$(python bmp-diff-mask.py "$BIND_BMP" "$WORLD_BMP" --mask "$BIND_BMP" "$WORLD_BMP" 2>&1 | tail -1)
  COVER_PX=$(mask_field "$COVER_LINE" changed)
  assert_ge "the ViewModel has its own coverage" "$COVER_PX" 15000
  COVER_BBOX=$(mask_pair_field "$COVER_LINE" bbox)
  echo "  ..   coverage $COVER_LINE"

  # The body is the difference between the body-on bind frame and the body-off
  # frame: same tick, same everything else. The arms are the difference between
  # the body-off frame and the world-only frame. Both must land inside the
  # composite's coverage, or every later subtraction is measuring two things.
  BODY_LINE=$(python bmp-diff-mask.py "$BIND_BMP" "$BODYOFF_BMP" --mask "$BIND_BMP" "$WORLD_BMP" 2>&1 | tail -1)
  ARMS_LINE=$(python bmp-diff-mask.py "$BODYOFF_BMP" "$WORLD_BMP" --mask "$BIND_BMP" "$WORLD_BMP" 2>&1 | tail -1)
  BODY_PX=$(mask_field "$BODY_LINE" changed)
  ARMS_PX=$(mask_field "$ARMS_LINE" changed)
  assert_ge "the weapon body's own coverage" "$BODY_PX" 500
  assert_ge "the arms' own coverage" "$ARMS_PX" 3000
  assert_eq "the body does not change anything outside the composite" "$(mask_field "$BODY_LINE" outside)" "0"
  assert_eq "the arms do not change anything outside the composite" "$(mask_field "$ARMS_LINE" outside)" "0"

  # The pose change, judged inside the ViewModel's coverage. The tolerance is 64
  # pixels because the silhouette legitimately moves by a pixel at its edge; the
  # measured value is recorded, not rounded away.
  POSE_LINE=$(python bmp-diff-mask.py "$BIND_BMP" "$APPLIED_BMP" --mask "$BIND_BMP" "$WORLD_BMP" 2>&1 | tail -1)
  POSE_PX=$(mask_field "$POSE_LINE" changed)
  POSE_OUTSIDE=$(mask_field "$POSE_LINE" outside)
  assert_ge "the pose moves real pixels" "$POSE_PX" 5000
  assert_le "the pose stays inside the ViewModel's coverage" "$POSE_OUTSIDE" 64
  POSE_BBOX=$(mask_pair_field "$POSE_LINE" bbox)
  echo "  ..   pose $POSE_LINE"

  # Contact: the arms' mask against the weapon body's mask, found by
  # bmp-mask-contact.py. A hand that leaves the grip keeps its pixel count and
  # loses this number; that is why it is a reading and not a bounding box.
  CONTACT_BIND_LINE=$(python bmp-mask-contact.py --mask "$BODYOFF_BMP" "$WORLD_BMP" --body "$BIND_BMP" "$BODYOFF_BMP" --radius 8 2>&1 | tail -1)
  CONTACT_APPLIED_LINE=$(python bmp-mask-contact.py --mask "$APPLYOFF_BMP" "$WORLD_BMP" --body "$BIND_BMP" "$BODYOFF_BMP" --radius 8 2>&1 | tail -1)
  BIND_NEAR=$(printf '%s\n' "$CONTACT_BIND_LINE" | grep -o 'nearFraction=[0-9.]*' | cut -d= -f2-)
  APPLIED_NEAR=$(printf '%s\n' "$CONTACT_APPLIED_LINE" | grep -o 'nearFraction=[0-9.]*' | cut -d= -f2-)
  BIND_DIST=$(printf '%s\n' "$CONTACT_BIND_LINE" | grep -o 'contactDistance=[0-9.-]*' | cut -d= -f2-)
  APPLIED_DIST=$(printf '%s\n' "$CONTACT_APPLIED_LINE" | grep -o 'contactDistance=[0-9.-]*' | cut -d= -f2-)
  assert_ge "contact: the arms mask has pixels (bind)" "$(mask_field "$CONTACT_BIND_LINE" maskPixels)" 3000
  assert_ge "contact: the arms touch the body (bind)" "$BIND_NEAR" 0.2
  assert_ge "contact: the touch distance is measured (bind)" "$BIND_DIST" 0
  assert_le "contact: the touch distance is small (bind)" "$BIND_DIST" 8
  assert_ge "contact: the arms mask has pixels (applied)" "$(mask_field "$CONTACT_APPLIED_LINE" maskPixels)" 3000
  assert_ge "contact: the posed arms still touch the body" "$APPLIED_NEAR" 0.2
  assert_ge "contact: the posed touch distance is measured" "$APPLIED_DIST" 0
  assert_le "contact: the posed touch distance is small" "$APPLIED_DIST" 8
  echo "  ..   contact bind: $CONTACT_BIND_LINE"
  echo "  ..   contact applied: $CONTACT_APPLIED_LINE"

  # The multi-range body's own partition: forcing slot 0 and slot 1 must change
  # disjoint sets of the body's pixels, and the two changes must add up exactly
  # to the difference between the two forced frames. This is the per-range
  # texture claim as a picture.
  MS0_LINE=$(python bmp-diff-mask.py "$MULTI_BMP" "$MSLOT0_BMP" --mask "$MULTI_BMP" "$BODYOFF_BMP" 2>&1 | tail -1)
  MS1_LINE=$(python bmp-diff-mask.py "$MULTI_BMP" "$MSLOT1_BMP" --mask "$MULTI_BMP" "$BODYOFF_BMP" 2>&1 | tail -1)
  MU_LINE=$(python bmp-diff-mask.py "$MSLOT0_BMP" "$MSLOT1_BMP" --mask "$MULTI_BMP" "$BODYOFF_BMP" 2>&1 | tail -1)
  MS0_PX=$(mask_field "$MS0_LINE" changed)
  MS1_PX=$(mask_field "$MS1_LINE" changed)
  MU_PX=$(mask_field "$MU_LINE" changed)
  assert_ge "forced slot 0 changes the body" "$MS0_PX" 100
  assert_ge "forced slot 1 changes the body" "$MS1_PX" 100
  assert_eq "forced slot 0 changes nothing outside the body" "$(mask_field "$MS0_LINE" outside)" "0"
  assert_eq "forced slot 1 changes nothing outside the body" "$(mask_field "$MS1_LINE" outside)" "0"
  assert_eq "the two forced changes add up to their difference" "$((MS0_PX + MS1_PX))" "$MU_PX"
  OVERLAP_LINE=$(python bmp-diff-mask.py "$MULTI_BMP" "$MSLOT0_BMP" --mask "$MULTI_BMP" "$MSLOT1_BMP" 2>&1 | tail -1)
  assert_eq "no body pixel is owned by both ranges" "$(mask_field "$OVERLAP_LINE" inside)" "0"
  echo "  ..   partition: slot0=$MS0_PX slot1=$MS1_PX union=$MU_PX"

  # The single-range body is on screen at all: a mask from the weapon body's own
  # absence. Without this the 'single range renders' claim would rest on counters.
  SINGLE_COVER_LINE=$(python bmp-diff-mask.py "$SINGLE_BMP" "$BODYOFF_BMP" --mask "$SINGLE_BMP" "$BODYOFF_BMP" 2>&1 | tail -1)
  assert_ge "the single-range body covers pixels" "$(mask_field "$SINGLE_COVER_LINE" changed)" 300
fi

# ---------------------------------------------------------------------------
# Section 5 -- the four mutations
# ---------------------------------------------------------------------------
begin_mutation() { # begin_mutation <file>
  MUTATION_FILE="$1"
  MUTATION_BAK="$RUN_DIR/mutation-$RUN_TAG.bak"
  cp "$MUTATION_FILE" "$MUTATION_BAK"
  MUTATION_SHA=$(sha256sum "$MUTATION_FILE" | cut -d' ' -f1)
  MUTATION_ACTIVE=1
}
end_mutation() { # end_mutation <name> <target...>
  local name="$1"; shift
  restore_mutation
  if ! bash build-target.sh "$@" > "$RUN_DIR/mutation-$name-build-back-$RUN_TAG.log" 2>&1; then
    fail_line "the restore did not rebuild $*"
  fi
}
patch_doc() { # patch_doc <file>  (the patch document arrives on stdin)
  local file="$1"
  python patch-source.py "$file"
}
expect_red() { # expect_red <label> <actual> <the value the defect produces> <the value the gate pins>
  if [ "${2:-<none>}" != "$3" ]; then
    fail_line "$1 = ${2:-<none>}, expected the defect to read $3"
  elif [ "$3" = "$4" ]; then
    fail_line "$1: the defect reads $3, which is also what the gate pins; it cannot discriminate"
  else
    echo "  OK   $1 = ${2:-<none>} (defect reproduces; the gate pins $4, so that assertion goes red)"
    OK_COUNT=$((OK_COUNT + 1)); WITNESS_COUNT=$((WITNESS_COUNT + 1))
  fi
}
expect_moved() { # expect_moved <label> <actual> <reference the intact run read>
  if [ "${2:-<none>}" != "$3" ]; then
    echo "  OK   $1 = ${2:-<none>} (!= $3, the defect moved it)"
    OK_COUNT=$((OK_COUNT + 1)); WITNESS_COUNT=$((WITNESS_COUNT + 1))
  else
    fail_line "$1 = ${2:-<none>}, the defect did not move it away from $3"
  fi
}
mutation_caught() { # mutation_caught <name> <witness_before> <fails_before>
  local name="$1" witness_before="$2" fails_before="$3"
  local moved=$((WITNESS_COUNT - witness_before))
  MUTATION_TOTAL=$((MUTATION_TOTAL + 1))
  if [ "$moved" -le 0 ]; then
    fail_line "$name: no reading moved to the defect's value"
  elif [ "${#FAIL_LINES[@]}" -ne "$fails_before" ]; then
    fail_line "$name: the defect reproduced, but other assertions also broke; the mutation is not isolated"
  else
    MUTATION_CATCHES=$((MUTATION_CATCHES + 1))
    echo "  OK   $name: caught ($moved reading(s) took the defect's value)"
  fi
}

if [ "$MUTATION" -eq 1 ]; then
  echo "  -- m1: the wrong range's geometry is drawn"
  fails_before=${#FAIL_LINES[@]}; witness_before=$WITNESS_COUNT
  begin_mutation native/src/main.cpp
  if patch_doc native/src/main.cpp <<'PATCH'
      range.firstVertex = source.firstVertex;
      range.vertexCount = source.vertexCount;
%%
      range.firstVertex = request.mesh.ranges.front().firstVertex;
      range.vertexCount = request.mesh.ranges.front().vertexCount;
PATCH
  then
    if bash build-target.sh tf2_demo_native > "$RUN_DIR/mutation-m1-build-$RUN_TAG.log" 2>&1; then
      run_product m1 --viewmodel "$VM_MODEL" --viewmodel-sequence "$POSE_SEQUENCE" --viewmodel-weapon-model "$MULTI_MODEL"
      expect_moved "m1: the wrong range geometry moves the body frame" "$LAST_BMP_HASH" "$MULTI_HASH"
      assert_eq "m1 control: every range is still drawn" "$(reading "$LAST_TRACE" 'gpu: viewmodel weaponBodyMaterial' ranges)" "$MULTI_RANGES"
      assert_eq "m1 control: the vertex buffer is unchanged" "$(reading "$LAST_TRACE" 'gpu: viewmodel weaponBody' vertices)" "$MULTI_VERTICES"
    else
      fail_line "m1: the mutated product did not build"
    fi
  fi
  end_mutation m1 tf2_demo_native
  mutation_caught m1 "$witness_before" "$fails_before"

  echo "  -- m2: every range reads the slot next to the one it names"
  fails_before=${#FAIL_LINES[@]}; witness_before=$WITNESS_COUNT
  begin_mutation native/src/main.cpp
  if patch_doc native/src/main.cpp <<'PATCH'
      const std::int32_t slot = slotOverride >= 0
        ? slotOverride : source.materialSlot;
%%
      const std::int32_t slot = slotOverride >= 0
        ? slotOverride : source.materialSlot + 1;
PATCH
  then
    if bash build-target.sh tf2_demo_native > "$RUN_DIR/mutation-m2-build-$RUN_TAG.log" 2>&1; then
      run_product m2 --viewmodel "$VM_MODEL" --viewmodel-sequence "$POSE_SEQUENCE" --viewmodel-weapon-model "$MULTI_MODEL"
      expect_moved "m2: the body's key list moves to the wrong slot" "$(reading "$LAST_TRACE" 'gpu: viewmodel weaponBodyMaterial' keys)" "$MULTI_KEYS_ACTUAL"
      expect_moved "m2: the wrong slot changes the picture" "$LAST_BMP_HASH" "$MULTI_HASH"
      assert_eq "m2 control: the range count is unchanged" "$(reading "$LAST_TRACE" 'gpu: viewmodel weaponBodyMaterial' ranges)" "$MULTI_RANGES"
    else
      fail_line "m2: the mutated product did not build"
    fi
  fi
  end_mutation m2 tf2_demo_native
  mutation_caught m2 "$witness_before" "$fails_before"

  echo "  -- m3: a missing companion stops being detected (the VVD is ignored)"
  fails_before=${#FAIL_LINES[@]}; witness_before=$WITNESS_COUNT
  begin_mutation native/src/model_loader.cpp
  if patch_doc native/src/model_loader.cpp <<'PATCH'
  result.companionSetComplete = result.mdlFound && result.vvdFound
      && (result.dx90VtxFound || result.dx80VtxFound || result.swVtxFound);
%%
  result.companionSetComplete = result.mdlFound
      && (result.dx90VtxFound || result.dx80VtxFound || result.swVtxFound);
PATCH
  then
    if bash build-target.sh model_robustness_probe tf2_demo_native > "$RUN_DIR/mutation-m3-build-$RUN_TAG.log" 2>&1; then
      probe_run m3 "$RUN_DIR/fixture-vm-no-vvd" "$VM_MODEL"
      expect_red "m3: the no-VVD fixture is reported as a complete companion set" "$(pfield companions)" "1" "0"
      run_fixture_product m3 "$RUN_DIR/fixture-vm-no-vvd" "$VM_MODEL"
      expect_moved "m3: the missing companion no longer produces its named refusal" "$(reason_of "$LAST_READING")" "viewmodel companions are missing"
    else
      fail_line "m3: the mutated product did not build"
    fi
  fi
  end_mutation m3 model_robustness_probe tf2_demo_native
  mutation_caught m3 "$witness_before" "$fails_before"

  echo "  -- m4: the bone table is read as its own transpose"
  fails_before=${#FAIL_LINES[@]}; witness_before=$WITNESS_COUNT
  begin_mutation native/src/animation_binding.cpp
  if patch_doc native/src/animation_binding.cpp <<'PATCH'
  return {
    m[0], m[4], m[8], 0.0f,
    m[1], m[5], m[9], 0.0f,
    m[2], m[6], m[10], 0.0f,
    m[3], m[7], m[11], 1.0f
  };
%%
  return {
    m[0], m[1], m[2], 0.0f,
    m[4], m[5], m[6], 0.0f,
    m[8], m[9], m[10], 0.0f,
    m[3], m[7], m[11], 1.0f
  };
PATCH
  then
    if bash build-target.sh tf2_demo_native > "$RUN_DIR/mutation-m4-build-$RUN_TAG.log" 2>&1; then
      run_product m4 --viewmodel "$VM_MODEL" --viewmodel-sequence "$POSE_SEQUENCE" --viewmodel-apply-animation
      M4_POSE=$(python bmp-diff-mask.py "$BIND_BMP" "$LAST_BMP" --mask "$BIND_BMP" "$WORLD_BMP" 2>&1 | tail -1)
      M4_OUTSIDE=$(mask_field "$M4_POSE" outside)
      if awk -v a="$M4_OUTSIDE" 'BEGIN { exit !(a > 64) }'; then
        echo "  OK   m4: the transposed bone table throws pixels outside the coverage = $M4_OUTSIDE (> 64; the gate pins <= 64)"
        OK_COUNT=$((OK_COUNT + 1)); WITNESS_COUNT=$((WITNESS_COUNT + 1))
      else
        fail_line "m4: outside=$M4_OUTSIDE, the transposed bone table did not move the picture"
      fi
      assert_eq "m4 control: the pose is still applied" "$(reading "$LAST_TRACE" 'gpu: viewmodel pass order' vmAnimApplied)" "1"
      assert_gt "m4 control: the pose extent still moves geometry (the reading cannot see this defect)" "$(reading "$LAST_TRACE" 'gpu: viewmodel pose extent' maxDisplacement)" 0.01
    else
      fail_line "m4: the mutated product did not build"
    fi
  fi
  end_mutation m4 tf2_demo_native
  mutation_caught m4 "$witness_before" "$fails_before"

  # ---- restoration: the restored build must reproduce the reference readings --
  echo "  -- restoration"
  if ! bash build-target.sh tf2_demo_native model_robustness_probe > "$RUN_DIR/mutation-restore-build-$RUN_TAG.log" 2>&1; then
    fail_line "the restored tree did not rebuild"
  fi
  run_product restored --viewmodel "$VM_MODEL" --viewmodel-sequence "$POSE_SEQUENCE" --viewmodel-apply-animation
  assert_eq "restored: the applied frame reproduces the baseline" "$LAST_BMP_HASH" "$APPLIED_HASH"
  probe_run restored-anim "$RUN_DIR/fixture-arms-no-anim" "$ARMS_MODEL"
  assert_eq "restored: the animation companion refusal returns" "$(pfield animationInput)" "anim-source-missing"
  DIRTY_NATIVE=$(git status --porcelain -- native/)
  assert_eq "the product sources are clean after the mutations" "$DIRTY_NATIVE" ""
fi

# ---------------------------------------------------------------------------
# Section 6 -- evidence and verdict
# ---------------------------------------------------------------------------
echo
echo "=== 6/6 evidence and verdict ==="
if [ "$MUTATION" -eq 0 ] && [ "$fail" -eq 0 ]; then
  cp "$BIND_BMP" "$EVIDENCE/bind-pose.bmp"
  cp "$APPLIED_BMP" "$EVIDENCE/applied-pose.bmp"
  cp "$MULTI_BMP" "$EVIDENCE/weapon-body-multi-range.bmp"
  python bmp-to-png.py "$APPLIED_BMP" "$EVIDENCE/inspect-applied.png" >/dev/null 2>&1 || true
  {
    echo "# viewmodel-robustness-check.sh readings ($(date -u +%Y-%m-%dT%H:%M:%SZ))"
    echo "# branch: $(git rev-parse --abbrev-ref HEAD) commit: $(git rev-parse HEAD)"
    echo "# tf root: $TF"
    echo "# recording: $DEMO (pinned by $INPUTS)"
    echo "# tick: $TICK  pose sequence: $POSE_SEQUENCE  capture: paused, hidden window, audio disabled"
    echo "# command: bash viewmodel-robustness-check.sh"
    echo "probe_sha256=$PROBE_HASH"
    echo "executable_sha256=$EXE_HASH"
    echo "host_overlap=$HOST_OVERLAP"
    echo "inventory_c_weapon_models=$C_CENSUS inventory_v_viewmodels=$V_CENSUS"
    echo "single_model=$SINGLE_MODEL ranges=$SINGLE_RANGES vertices=$SINGLE_VERTICES vtf=$SINGLE_VTF"
    echo "multi_model=$MULTI_MODEL ranges=$MULTI_RANGES vertices=$MULTI_VERTICES vtfs=$MULTI_VTFS"
    echo "lod_model=$LOD_MODEL declared_lods=$LOD_COUNT lod0_malformed=refused lod0_absent=fallback_to_lod_1"
    echo "arms_model=$ARMS_MODEL arms_ranges=2 arms_vertices=9996 animation=companion_c_sniper_animations"
    echo ''
    echo "[frames]"
    echo "bind=$BIND_HASH bytes=$(stat -c%s "$BIND_BMP")"
    echo "applied=$APPLIED_HASH"
    echo "applied_repeat=identical"
    echo "world=$WORLD_HASH"
    echo "multi_range_body=$MULTI_HASH"
    echo ''
    echo "[camera]"
    echo "$BIND_CAMERA"
    echo ''
    echo "[pose]"
    echo "$(reading_line "$APPLIED_TRACE" 'gpu: viewmodel pose extent')"
    echo ''
    echo "[masks]"
    echo "coverage: $COVER_LINE"
    echo "body: $BODY_LINE"
    echo "arms: $ARMS_LINE"
    echo "pose: $POSE_LINE $POSE_BBOX"
    echo "contact_bind: $CONTACT_BIND_LINE"
    echo "contact_applied: $CONTACT_APPLIED_LINE"
    echo "partition_slot0: $MS0_LINE"
    echo "partition_slot1: $MS1_LINE"
    echo "partition_union: $MU_LINE"
    echo "partition_overlap: $OVERLAP_LINE"
    echo "single_body_coverage: $SINGLE_COVER_LINE"
    echo ''
    echo "[product readings]"
    echo "$BIND_ORDER"
    echo "$APPLIED_ORDER"
    echo "$SINGLE_MATERIAL"
    echo "$MULTI_MATERIAL"
  } > "$EVIDENCE/summary.txt"
  pass "evidence written to $EVIDENCE"
fi

echo
echo "assertions: $OK_COUNT held, $FAIL_COUNT failed"
echo "host_overlap=$HOST_OVERLAP (a paused capture is insensitive to it; the repeat hash is the guard)"
if [ "$fail" -ne 0 ]; then
  echo "failed assertions:"
  for line in "${FAIL_LINES[@]}"; do echo "  $line"; done
  if [ "$MUTATION" -eq 1 ]; then
    echo "MUTATION-CAUGHT=FAIL ($MUTATION_CATCHES of $MUTATION_TOTAL caught)"
  else
    echo "VIEWMODEL-ROBUSTNESS=FAIL"
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
  echo "VIEWMODEL-ROBUSTNESS=PASS ($OK_COUNT assertions, 0 failures)"
fi
exit 0
