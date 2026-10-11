#!/usr/bin/env bash
# viewmodel-contract-check.sh -- the product must draw a first-person ViewModel
# through its own pass, with its own projection, its own attachment chain and its
# own hand mirror, and must say why when it cannot.
#
# WHY THIS EXISTS
#   T1 proved the world/entity path skins models on the GPU from real demo input.
#   None of that reaches the first-person weapon: the world pass normalises
#   geometry into the map's unit box and writes depth with the world policy, so a
#   weapon routed through it would be scaled into the map and occluded by it. The
#   ViewModel therefore needs its own buffers, projection, depth state and draw
#   order, and "the code has a drawViewModel()" is not evidence that any of that
#   happened. Every assertion below is a reading taken from a run.
#
# WHAT IS ASSERTED, AND WHY EACH ONE CAN FAIL
#   Section 1 -- the request contract, on `viewmodel_contract_probe`, which goes
#     through the same `buildViewModelRequest` the product uses:
#       * the positive fixture is a real game asset (`v_scattergun_scout.mdl` read
#         out of the installed VPK): every companion resolves, every attachment is
#         valid, the placement anchor is `weapon_bone`, contractOk is true.
#       * the synthetic fixtures under evidence/viewmodel/fixtures are a *pair*.
#         `v_fixture_ok` and `v_fixture_damaged` are the same bytes except one
#         int32 -- the attachment's bone index, 0 versus 99. Asserting only the
#         damaged one would be satisfied by a file the parser merely failed to
#         understand, so the control is what makes `invalidAttachments=1` mean
#         "the bone index was rejected". A damaged attachment must be recorded,
#         must NOT fail the request, and must not be used for placement.
#       * `v_fixture_noanchor` has one *valid* attachment named `muzzle`. The
#         selection rule must refuse it: "the first attachment" is not a placement
#         policy, and this fixture is the only input that says so.
#       * `v_fixture_nocompanion` must be refused before the MDL is inspected.
#       * a missing path and a non-`v_` path must be refused with distinct
#         reasons, and the FOV contract (default 90, clamp 40..120, legacy 80
#         migrates to 90, an explicit in-range value is kept) must hold.
#       * the fixture bytes must be reproducible from viewmodel-fixture-gen.py:
#         the generator is re-run and compared byte for byte, so an input that
#         lives only on this machine cannot quietly become the thing asserted.
#   Section 2 -- the product's own runtime reading and the pass order. `drawn` is
#     the pass's draw counter, and the two pass ordinals make "the ViewModel draws
#     after the world" a number instead of a claim about statement order. The
#     reading prints the pass FOV *and* the settings FOV, because those differ
#     whenever --viewmodel-fov overrides and reporting only one would hide which
#     one the pass used. Every run here is pointed at a settings file of its own
#     (`--settings-file`, under the cleared trace directory), so three FOV cases can
#     be stated as facts about the build: no stored value -> the compiled default 90,
#     a stored 60 -> 60, a stored legacy 80 -> 90. Without the override the first of
#     those would be a reading of `%LOCALAPPDATA%`, and every run would also rewrite
#     the user's real settings file on exit.
#   Section 3 -- the picture. The only assertions that can distinguish "a draw was
#     issued" from "the weapon is where it should be": the weapon must appear
#     against the same frame without it, FOV 40/90/120 must be three different
#     pictures, the left hand must be a different picture from the right, and the
#     clamp must reach the projection rather than only the printed number
#     (`--viewmodel-fov 10` must render byte-identically to `--viewmodel-fov 40`).
#     Before any of those, every compared frame must come from the same playback
#     tick: the capture point is a property of the run rather than of the request,
#     and a comparison across two ticks would move the *scene* rather than the
#     ViewModel.
#   Section 4 -- a missing or damaged resource disables the ViewModel pass, not
#     the run: rc=0, `drawn=0`, a reason, and a frame byte-identical to the same
#     scene with no ViewModel at all.
#   Section 5 -- the animation input. A ViewModel is not an entity, so nothing in
#     a demo drives its sequence and the honest state is `animationInput=missing`;
#     the pass then draws the model's bind pose. The pair that makes that a
#     reading rather than an excuse is T1's instrument reapplied: uploading one
#     identity matrix per bone must take the skinning branch (`skinning=1`,
#     `animationInput=bones:<n>`) and must leave the picture byte-identical, while
#     a real pose would move it. Without that pair, "the branch never ran" and
#     "the branch ran and changed nothing" are the same reading. The same section
#     exercises the *refusal*: a table that cannot cover the skeleton
#     (`--viewmodel-bone-limit 1` against a 45-bone mesh) must be refused, the pass
#     must still draw the bind pose (`drawn=1 skinning=0`), and `reason` must name
#     the numbers it compared. That refusal is unreachable from any other input.
#   Section 6 -- the witness frames. Each captured frame is written as a titled
#     PNG (map, demo tick, playback tick, model, FOV, hand, draw count) so the
#     committed evidence identifies itself instead of being a bare bitmap whose
#     provenance has to be reconstructed from a log. The two ticks are labelled
#     separately because they differ by 10252 on this demo and reporting either
#     one as "the demo tick" would put a wrong number in a committed file.
#
# WHAT IT DOES NOT SAY
#   It does not say the weapon looks like TF2. There are no materials yet -- the
#   pass flat-shades -- and no human has compared the placement against the game.
#   It says the weapon is placed by the attachment chain, projected with the
#   ViewModel FOV, mirrored on the left hand, drawn after the world, and that a
#   missing resource does not take the run down.
#
#   In particular it does *not* say the picture witnesses the whole chain. On the
#   only asset this machine can draw, the selected placement attachment's local
#   matrix is the identity (asserted, `alignmentIsIdentity`), so the A stage of
#   A -> H -> T and "A was skipped" render the same frame. H and T are witnessed
#   by the frames; A is witnessed by nothing but the code path.
#
# Usage: bash viewmodel-contract-check.sh [--mutation [name]]
#   --mutation          runs all five mutations in turn. Each patches one
#                       coherent defect, rebuilds, and requires this script to go
#                       red on a *named* assertion (not merely to exit non-zero):
#                         attachment-bone    the attachment's bone index is read
#                                            from the flags word
#                         fov-default        the default ViewModel FOV is 80 again;
#                                            reddens the probe's FOV contract *and*
#                                            the product's default-FOV reading, which
#                                            is why it runs sections 1 and 2 and
#                                            rebuilds the product
#                         hand-matrix        the left hand stops mirroring
#                         companion-missing  missing companions stop being noticed
#                         draw-skipped       the ViewModel pass is never called
#   --mutation <name>   runs one of them.
# Exit:  0 = every assertion held (or, with a mutation, the checks went red).
#
# HOW A FRAME IS KNOWN TO BE CURRENT
#   Every captured frame is stamped with a digest of the three binaries this gate runs
#   (`build_token`), and `ensure_product` reuses a frame only when the stamp matches.
#   This is not bookkeeping: the first mutation run deleted its frames with `rm`, a
#   sandbox bulk-delete guard refused those calls, the gate could not tell, and two
#   mutations compared a mutated build against a clean build's frames and read green.
#   A reused frame from a different build is the one failure mode that makes this
#   whole file report success while checking nothing, so freshness is decided by the
#   binary's identity and never by a delete having happened.
#
# One run at a time: the script takes .scratch/viewmodel-contract.lock with mkdir.
# It launches the product a dozen times against the same frame and trace paths,
# and a mutation run rewrites tracked sources; two concurrent runs would overwrite
# each other's evidence and each other's patch. mkdir is atomic, so a second
# caller fails before it builds anything.
set -uo pipefail
cd "$(dirname "$0")"

EXE=native/build-nmake/tf2_demo_native.exe
PROBE=native/build-nmake/viewmodel_contract_probe.exe
FOV_PROBE=native/build-nmake/viewmodel_fov_probe.exe
TF="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
DEMO="D:/TF2_Demo_Player/testdata/demos/bb841c6d379ff7c40d0c8baf99f59d8d_matcha-20260927-1347-cp_snakewater_final1.dem"
# The demo's map, for the witness titles only. The demo must be on an *installed*
# map: the world bounds gate the entity passes, and a demo whose BSP is missing
# renders the fallback quad for a reason that has nothing to do with the
# ViewModel. cp_snakewater_final1 is installed on this machine; koth_bagel_rc13,
# which several other gates use, is not.
DEMO_LABEL="cp_snakewater_final1"
MODEL="models/weapons/v_models/v_scattergun_scout.mdl"
# The synthetic fixtures, and the tf root they are read through. A root with no
# VPK in it forces the loose-file path, which is what makes the fixture bytes --
# rather than some archive entry -- the input under test. The absolute root is
# built below, after `cygpath`, because it is handed to a native Windows binary.
FIXTURE_DIR="evidence/viewmodel/fixtures"
FIXTURE_MODEL_DIR="models/weapons/v_models"

# Frozen witnesses (titled PNGs, readings, mutation-failure records) are
# committed. The BMPs, traces and metrics CSVs are machine output -- a 2.5 MB BMP
# per frame, and a CSV whose elapsed/working-set columns differ on every run --
# and live under evidence/fast, which is where the plan puts gate output that must
# not be mistaken for input.
OUT="${VIEWMODEL_CONTRACT_OUT:-evidence/viewmodel}"
TRACE_DIR="${VIEWMODEL_CONTRACT_TRACE_DIR:-evidence/fast/viewmodel}"
mkdir -p "$OUT" "$TRACE_DIR"

# --capture-tick is a *playback position*, not a demo tick: this demo's entities
# start at tick 10252 while its playback position starts at 0, so a capture
# request written from a demo tick would never be reached.
CAPTURE_TICK=0
# Audio: the device is opened during startup and pausing does not stop it, so
# every launch names it. The default device is the wrong one on this machine.
AUDIO_DEVICE=6
RUN_TIMEOUT=300

# Every frame this gate owns. `ensure_product` reuses a frame instead of capturing it
# twice, so "a frame is on disk" has to mean "this binary produced it" -- and that is
# decided by the token written beside it, never by file existence. The first version
# of this gate rested on `clear_frames` instead, and a bulk-delete guard that refuses
# `rm` above a per-turn count turned those deletes into silent no-ops; every
# `ensure_product` then reported `reused` and two mutations read green off another
# build's frames. A delete that can silently not happen cannot carry correctness.
FRAME_NAMES="novm vm90 vm40 vm120 vmleft vmident vmfov10 vmfov200 vmmissing vmdefault vmstored vmlegacy vmfdamaged vmfnoanchor vmfnocompanion vmshort"
clear_frames() {
  local name
  for name in $FRAME_NAMES; do
    # Best effort, and only tidiness: the token decides freshness, so a refused delete
    # costs disk space and nothing else. It is deliberately not the mechanism.
    rm -f "$TRACE_DIR/$name.bmp" "$TRACE_DIR/$name.csv" "$TRACE_DIR/$name.token"
    # The per-frame settings file goes too, so a frame that is meant to run with no
    # stored value cannot inherit one from the previous run of this gate.
    rm -f "$TRACE_DIR/$name-settings.ini"
  done
}

PY="C:/Users/Administrator/.workbuddy-ai/binaries/python/versions/3.13.12/python.exe"
[ -x "$PY" ] || { echo "FATAL: missing interpreter: $PY"; exit 1; }
# The trace path is handed to a native Windows binary, which does not translate
# MSYS paths: "/d/..." is opened literally and the trace silently never opens,
# which reads exactly like "the ViewModel pass produced no reading". Convert once.
ROOT=$(cygpath -m "$(pwd)" 2>/dev/null || pwd)
case "$ROOT" in
  /[a-zA-Z]/*) echo "FATAL: could not convert '$ROOT' to a Windows path"; exit 1 ;;
esac
# Built from the converted root for the same reason as the trace path: this value
# goes into `--tf-root`, and an MSYS "/d/..." argument is opened literally by the
# native binary, which reads exactly like "the model was not found". That is the
# failure this line had on its first run.
FIXTURE_ROOT="$ROOT/$FIXTURE_DIR/tf"

MUTATION=""
case "${1:-}" in
  --mutation) MUTATION="${2:-all}" ;;
  "") ;;
  *) echo "FATAL: unknown option: $1"; exit 1 ;;
esac
ALL_MUTATIONS=(attachment-bone fov-default hand-matrix companion-missing draw-skipped)
MUTATIONS=("${ALL_MUTATIONS[@]}")
if [ -n "$MUTATION" ] && [ "$MUTATION" != "all" ]; then
  case " ${ALL_MUTATIONS[*]} " in
    *" $MUTATION "*) MUTATIONS=("$MUTATION") ;;
    *) echo "FATAL: unknown mutation: $MUTATION (expected one of: ${ALL_MUTATIONS[*]})"; exit 1 ;;
  esac
fi

mkdir -p .scratch
LOCK_DIR=.scratch/viewmodel-contract.lock
if ! mkdir "$LOCK_DIR" 2>/dev/null; then
  echo "VIEWMODEL-CONTRACT=FAIL (another viewmodel-contract-check run owns $LOCK_DIR)"
  exit 2
fi

# Missing input is a failure, not a skip: a gate that quietly SKIPs when its
# binary or its demo is absent is the shape the P0 pass shipped four of.
for path in "$EXE" "$PROBE" "$FOV_PROBE" "$TF" "$DEMO" "$FIXTURE_ROOT" bmp-pixel-diff.py viewmodel-fixture-gen.py; do
  [ -e "$path" ] || { echo "FATAL: missing input: $path"; exit 1; }
done
command -v ffmpeg >/dev/null 2>&1 || { echo "FATAL: ffmpeg is needed to title the witness frames"; exit 1; }

# build_token -> a short digest of the three binaries this gate runs.
# `ensure_product` reuses a captured frame only when the frame carries the current
# token, so a frame can never be compared against a different build's output. The
# contents are hashed rather than the mtime because the question is "is this the same
# binary", and the token only ever has to agree with itself within one invocation --
# this build is not reproducible, and it does not need to be for this to work.
# Recomputed after every build in the mutation driver.
build_token() {
  cat "$EXE" "$PROBE" "$FOV_PROBE" 2>/dev/null | sha256sum | cut -c1-16
}

fail=0
ASSERTIONS=0
FAIL_LINES=()
# Which sections to run. The straight run runs all seven; a mutation narrows this to
# the sections it can move (see MUTATION_SECTIONS).
SECTIONS="1 2 3 4 5 6 7"
assert_eq() { # assert_eq <label> <actual> <expected>
  if [ "${2:-<none>}" = "$3" ]; then
    echo "  OK   $1 = $3"; ASSERTIONS=$((ASSERTIONS + 1))
  else
    echo "  FAIL $1 = ${2:-<none>}, expected $3"; fail=1; FAIL_LINES+=("$1: ${2:-<none>} != $3")
  fi
}
assert_ge() { # assert_ge <label> <actual> <minimum>
  if [ "${2:-x}" -ge "$3" ] 2>/dev/null; then
    echo "  OK   $1 = $2 (>= $3)"; ASSERTIONS=$((ASSERTIONS + 1))
  else
    echo "  FAIL $1 = ${2:-<none>}, expected >= $3"; fail=1; FAIL_LINES+=("$1: ${2:-<none>} < $3")
  fi
}
assert_contains() { # assert_contains <label> <haystack> <needle>
  case "${2:-}" in
    *"$3"*) echo "  OK   $1 contains $3"; ASSERTIONS=$((ASSERTIONS + 1)) ;;
    *) echo "  FAIL $1 = ${2:-<none>}, expected it to contain $3"; fail=1
       FAIL_LINES+=("$1: ${2:-<none>} does not contain $3") ;;
  esac
}
assert_gt() { # assert_gt <label> <actual> <exclusive minimum>, float comparison
  local ok
  ok=$("$PY" -c "import sys
try: print(1 if float(sys.argv[1]) > float(sys.argv[2]) else 0)
except ValueError: print(0)" "${2:-nan}" "$3")
  if [ "$ok" = "1" ]; then
    echo "  OK   $1 = $2 (> $3)"; ASSERTIONS=$((ASSERTIONS + 1))
  else
    echo "  FAIL $1 = ${2:-<none>}, expected > $3"; fail=1; FAIL_LINES+=("$1: ${2:-<none>} <= $3")
  fi
}

# field <line> <name> -> the value of name=<value> in a trace line, "" if absent.
# The CR strip is for the fresh-checkout case: core.autocrlf=true writes tracked
# files with CRLF there, and bash `read`/`sed` disagree about stripping CR, so the
# last field of a line can come back with a trailing byte that compares unequal
# for a reason that has nothing to do with the product.
field() {
  printf '%s\n' "$1" | tr -d '\r' | tr ' ' '\n' | sed -n "s/^$2=//p" | head -1
}
# jfield <json> <name> -> the value of "name":<value>, "" if absent. String
# values come back *unquoted*: the probe emits "hand":"left", and a helper that
# returned the quotes would make every assert_eq on a string field read FAIL for
# a reason that is about the helper, not the product.
jfield() {
  printf '%s\n' "$1" | tr -d '\r' | tr ',' '\n' \
    | sed -n "s/.*\"$2\":\([^,}]*\).*/\1/p" | head -1 \
    | sed 's/^"//; s/"$//'
}
# bmp_diff <label> <a> <b> <bounds...> -> PASS/FAIL of one pixel comparison.
# The tool's exit code is the assertion: 0 held, 1 a bound failed, 2 the input
# could not be read. All three are reported; none is skipped.
bmp_diff() {
  local label="$1" a="$2" b="$3"; shift 3
  local line rc
  line=$("$PY" bmp-pixel-diff.py "$a" "$b" "$@" 2>&1)
  rc=$?
  echo "  $line"
  if [ "$rc" -eq 0 ]; then
    echo "  OK   $label"; ASSERTIONS=$((ASSERTIONS + 1))
  else
    echo "  FAIL $label (rc=$rc)"; fail=1; FAIL_LINES+=("$label: $line")
  fi
}

# probe_run <name> <tf-root> <model> [extra args...] -> sets PROBE_JSON / PROBE_RC
probe_run() {
  local name="$1" root="$2" model="$3"; shift 3
  local out
  out=$("$PROBE" --tf-root "$root" --model "$model" "$@" 2>/dev/null)
  PROBE_RC=$?
  PROBE_JSON=$(printf '%s\n' "$out" | tr -d '\r' | head -1)
  echo "  $name: $PROBE_JSON"
}

# capture_tick <name> -> the *playback* tick of the frame that was captured, i.e.
# the tick of the first `draw: begin`. Not the tick of the metrics row and not a
# constant: the same command has landed on playback tick 0 and on playback tick
# 16 in this gate, because the first present happens whenever the loader is done.
capture_tick() {
  grep -a "draw: begin" "$TRACE_DIR/$1.trace" 2>/dev/null | tr -d '\r' | head -1 \
    | tr ' ' '\n' | sed -n 's/^tick=//p' | head -1
}
# capture_scene_tick <name> -> the *demo's* entity tick of the scene the captured
# frame was drawn from: the last `scene: begin` at or before the first
# `draw: begin`. The two ticks differ by 10252 on this demo, so a caption that named
# either one "the demo tick" would be wrong half the time.
#
# This used to be `tail -1` over every `scene: begin` in the file. That agrees with
# the definition today, because the product exits right after the capture and nothing
# follows the draw -- and it would stop agreeing, silently, the moment anything did:
# the reading would name a scene the frame was not drawn from. awk holds the last
# scene seen and prints it when the first draw arrives, which is the definition
# rather than a coincidence that currently holds.
capture_scene_tick() {
  awk '/ scene: begin / { s = $0 } / draw: begin / { print s; exit }' "$TRACE_DIR/$1.trace" 2>/dev/null \
    | tr -d '\r' | tr ' ' '\n' | sed -n 's/^sceneTick=//p' | head -1
}

# load_reading <name> -> re-reads the last trace lines of that run into LAST_*.
# Reading them from the trace rather than from the run's own stdout is what lets
# `ensure_product` reuse a frame and still leave the assertions something to read.
load_reading() {
  local name="$1"
  # The trace lines are prefixed with the milliseconds since start ("2126.1 ").
  # Stripping it here keeps `viewModelPath=` the first field of the reading, so
  # the printed line is the reading itself rather than a reading with a clock in
  # front of it.
  LAST_BMP="$TRACE_DIR/$name.bmp"
  LAST_READING=$(grep -a "viewModelPath=" "$TRACE_DIR/$name.trace" 2>/dev/null | tr -d '\r' | tail -1 \
    | sed 's/^[0-9]\{1,\}\(\.[0-9]*\)\? //')
  LAST_ORDER=$(grep -a "gpu: viewmodel pass order" "$TRACE_DIR/$name.trace" 2>/dev/null | tr -d '\r' | tail -1 \
    | sed 's/^[0-9]\{1,\}\(\.[0-9]*\)\? //')
  LAST_CENTRE=$(grep -a "gpu: viewmodel centre" "$TRACE_DIR/$name.trace" 2>/dev/null | tr -d '\r' | tail -1 \
    | sed 's/^[0-9]\{1,\}\(\.[0-9]*\)\? //')
  # Two different ticks, and conflating them would put a wrong number in a
  # committed caption. `tick=` is the *playback position* (what --capture-tick
  # names); `sceneTick=` on the `scene: begin` line is the *demo's* entity tick.
  # For this demo they differ by 10252: playback 0 is demo tick 10252.
  LAST_TICK=$(capture_tick "$name")
  LAST_SCENE_TICK=$(capture_scene_tick "$name")
}

# run_product <name> [args...]. Each run writes one frame and exits by itself, so
# the gate never has to guess when to kill it. LAST_* describe the *last* reading,
# which is the one written for the captured frame: the first reading of a run is
# written before the first scene update and legitimately says drawn=0.
run_product() {
  local name="$1"; shift
  local bmp="$TRACE_DIR/$name.bmp"
  rm -f "$bmp" "$TRACE_DIR/$name.csv"
  # `--settings-file` gives every frame a settings file of its own under the trace
  # directory, which is machine output and is cleared above. Two things follow: the
  # run never reads the user's real settings file, so `vmdefault` describes the build
  # rather than this machine's `%LOCALAPPDATA%`; and it never *writes* it either, which
  # the product does on exit for every run. A frame whose file does not exist is the
  # deliberate case -- the compiled defaults are then the input.
  TF2_NATIVE_TRACE="$ROOT/$TRACE_DIR/$name.trace" timeout "$RUN_TIMEOUT" "$EXE" \
    --tf-root "$TF" --audio-device "$AUDIO_DEVICE" --demo "$DEMO" \
    --settings-file "$TRACE_DIR/$name-settings.ini" \
    --capture-frame "$bmp" --capture-tick "$CAPTURE_TICK" \
    --metrics-file "$TRACE_DIR/$name.csv" "$@" > "$TRACE_DIR/$name.log" 2>&1
  LAST_RC=$?
  # Stamp the frame with the build that produced it. Written only when a frame was
  # actually captured, so a failed run cannot leave a token that makes a stale frame
  # from an older run look current.
  if [ "$LAST_RC" -eq 0 ] && [ -s "$bmp" ]; then
    printf '%s\n' "$BUILD_TOKEN" > "$TRACE_DIR/$name.token"
  fi
  load_reading "$name"
  echo "  $name rc=$LAST_RC bmp=$(stat -c%s "$bmp" 2>/dev/null || echo 0)"
  [ -n "$LAST_READING" ] && echo "  $LAST_READING"
  [ -n "$LAST_ORDER" ] && echo "  $LAST_ORDER"
}

# ensure_product <name> [args...] -- runs it only when this run has not already
# captured that frame *with the current build*. Sections share a few baselines (novm,
# vm90) so that a mutation which runs one section still has the frames it compares
# against; a comparison that failed because the file was never written would not be
# assertion under test. The reuse test is the token, not the file: see build_token.
ensure_product() {
  local name="$1"; shift
  if [ -f "$TRACE_DIR/$name.bmp" ] && [ -f "$TRACE_DIR/$name.token" ] \
     && [ "$(cat "$TRACE_DIR/$name.token")" = "$BUILD_TOKEN" ]; then
    echo "  $name reused (captured by this build, token $BUILD_TOKEN)"
    LAST_RC=0
    load_reading "$name"
    [ -n "$LAST_READING" ] && echo "  $LAST_READING"
    [ -n "$LAST_ORDER" ] && echo "  $LAST_ORDER"
    return 0
  fi
  if [ -f "$TRACE_DIR/$name.bmp" ]; then
    # Said out loud, because this line is what was missing when a mutation read green
    # off another binary's frame.
    echo "  ..   $name holds a frame from token $(cat "$TRACE_DIR/$name.token" 2>/dev/null || echo none), not $BUILD_TOKEN; re-capturing"
  fi
  run_product "$name" "$@"
}

# The fixture-root runs use no demo and --start-paused: they are about the
# ViewModel request being refused, and a demo would add a minute of VPK and scene
# work that cannot change the answer.
run_fixture_product() {
  local name="$1" model="$2"
  local bmp="$TRACE_DIR/$name.bmp"
  rm -f "$bmp" "$TRACE_DIR/$name.csv"
  TF2_NATIVE_TRACE="$ROOT/$TRACE_DIR/$name.trace" timeout "$RUN_TIMEOUT" "$EXE" \
    --tf-root "$FIXTURE_ROOT" --audio-device "$AUDIO_DEVICE" --start-paused \
    --settings-file "$TRACE_DIR/$name-settings.ini" \
    --viewmodel "$model" \
    --capture-frame "$bmp" --capture-tick "$CAPTURE_TICK" \
    --metrics-file "$TRACE_DIR/$name.csv" > "$TRACE_DIR/$name.log" 2>&1
  LAST_RC=$?
  if [ "$LAST_RC" -eq 0 ] && [ -s "$bmp" ]; then
    printf '%s\n' "$BUILD_TOKEN" > "$TRACE_DIR/$name.token"
  fi
  load_reading "$name"
  echo "  $name rc=$LAST_RC bmp=$(stat -c%s "$bmp" 2>/dev/null || echo 0)"
  [ -n "$LAST_READING" ] && echo "  $LAST_READING"
  [ -n "$LAST_ORDER" ] && echo "  $LAST_ORDER"
}

# witness <png-name> <bmp> <caption> -> a titled PNG in the committed evidence dir.
# The caption carries the map, the demo tick the frame was taken at, the model,
# the FOV, the hand and the draw count, so the committed file identifies itself. A
# frame with no caption is a bitmap whose provenance lives only in a log that the
# next run overwrites.
witness() {
  local name="$1" bmp="$2" caption="$3"
  local png="$OUT/$name.png"
  rm -f "$png"
  ffmpeg -y -loglevel error -i "$bmp" -vf \
    "drawtext=fontfile='C\:/Windows/Fonts/consola.ttf':text='$caption':x=10:y=8:fontsize=18:fontcolor=white:box=1:boxcolor=black@0.6" \
    -frames:v 1 "$png" > "$TRACE_DIR/$name.ffmpeg.log" 2>&1
  local rc=$?
  local size
  size=$(stat -c%s "$png" 2>/dev/null || echo 0)
  echo "  witness $name.png bytes=$size caption=\"$caption\""
  if [ "$rc" -eq 0 ] && [ "$size" -gt 2000 ]; then
    echo "  OK   witness frame $name.png was written"; ASSERTIONS=$((ASSERTIONS + 1))
  else
    echo "  FAIL witness frame $name.png was not written (rc=$rc size=$size)"; fail=1
    FAIL_LINES+=("witness $name.png: rc=$rc size=$size")
  fi
}

# ===========================================================================
# Section 1 -- the request contract
# ===========================================================================
section1() {
echo "=== 1/7 the request contract ==="
echo "-- the positive fixture is a real game asset --"
probe_run "positive $MODEL" "$TF" "$MODEL"
assert_eq "probe positive status" "$(jfield "$PROBE_JSON" status)" "ok"
assert_eq "probe positive resourceComplete" "$(jfield "$PROBE_JSON" resourceComplete)" "true"
assert_eq "probe positive invalidAttachments" "$(jfield "$PROBE_JSON" invalidAttachments)" "0"
assert_ge "probe positive validAttachments" "$(jfield "$PROBE_JSON" validAttachments)" 1
assert_eq "probe positive attachmentStatus" "$(jfield "$PROBE_JSON" attachmentStatus)" "all attachments valid"
assert_eq "probe positive alignmentName" "$(jfield "$PROBE_JSON" alignmentName)" "weapon_bone"
assert_eq "probe positive alignmentIndex" "$(jfield "$PROBE_JSON" alignmentIndex)" "0"
assert_ge "probe positive sequenceCount" "$(jfield "$PROBE_JSON" sequenceCount)" 1
assert_ge "probe positive bones" "$(jfield "$PROBE_JSON" bones)" 1
assert_eq "probe positive contractOk" "$(jfield "$PROBE_JSON" contractOk)" "true"
# The limit of what the witness frames can show. The chosen placement attachment's
# local matrix is the identity on this asset, so the A stage of the A -> H -> T
# chain cannot be separated from "A was skipped" by any picture: with A = I the two
# are the same frame. Recorded as an assertion so the gate states its own blind
# spot instead of leaving a reviewer to assume the frames cover the whole chain.
# The chain's witnesses are H (the left hand) and T (the FOV and the placement).
assert_eq "probe positive alignmentIsIdentity" "$(jfield "$PROBE_JSON" alignmentIsIdentity)" "true"
assert_eq "probe positive rc" "$PROBE_RC" "0"

echo "-- the synthetic fixture pair: one int32 apart --"
probe_run "fixture ok" "$FIXTURE_ROOT" "$FIXTURE_MODEL_DIR/v_fixture_ok.mdl"
assert_eq "probe v_fixture_ok status" "$(jfield "$PROBE_JSON" status)" "ok"
assert_eq "probe v_fixture_ok validAttachments" "$(jfield "$PROBE_JSON" validAttachments)" "1"
assert_eq "probe v_fixture_ok invalidAttachments" "$(jfield "$PROBE_JSON" invalidAttachments)" "0"
assert_eq "probe v_fixture_ok alignmentIndex" "$(jfield "$PROBE_JSON" alignmentIndex)" "0"
probe_run "fixture damaged" "$FIXTURE_ROOT" "$FIXTURE_MODEL_DIR/v_fixture_damaged.mdl"
assert_eq "probe v_fixture_damaged invalidAttachments" "$(jfield "$PROBE_JSON" invalidAttachments)" "1"
# No usable placement attachment means there is no A stage at all, which is the
# control for the assertion above: `alignmentIsIdentity=true` on the positive
# fixture says "A is the identity", and `false` here says "there is no A". A probe
# that always reported one value would fail one of the two.
assert_eq "probe v_fixture_damaged alignmentIsIdentity" "$(jfield "$PROBE_JSON" alignmentIsIdentity)" "false"
# A damaged attachment is recorded and skipped; it must not fail the request.
assert_eq "probe v_fixture_damaged status" "$(jfield "$PROBE_JSON" status)" "ok"
assert_eq "probe v_fixture_damaged alignmentIndex" "$(jfield "$PROBE_JSON" alignmentIndex)" "-1"
assert_contains "probe v_fixture_damaged attachmentStatus" "$(jfield "$PROBE_JSON" attachmentStatus)" "unusable"
assert_contains "probe v_fixture_damaged alignmentReason" "$(jfield "$PROBE_JSON" alignmentReason)" "uses the model transform"

echo "-- a valid attachment with the wrong name is not 'the first attachment' --"
probe_run "fixture noanchor" "$FIXTURE_ROOT" "$FIXTURE_MODEL_DIR/v_fixture_noanchor.mdl"
assert_eq "probe v_fixture_noanchor validAttachments" "$(jfield "$PROBE_JSON" validAttachments)" "1"
assert_eq "probe v_fixture_noanchor invalidAttachments" "$(jfield "$PROBE_JSON" invalidAttachments)" "0"
assert_eq "probe v_fixture_noanchor alignmentIndex" "$(jfield "$PROBE_JSON" alignmentIndex)" "-1"
assert_contains "probe v_fixture_noanchor alignmentReason" "$(jfield "$PROBE_JSON" alignmentReason)" "no placement attachment present"

echo "-- a missing companion is refused before the MDL is inspected --"
probe_run "fixture nocompanion" "$FIXTURE_ROOT" "$FIXTURE_MODEL_DIR/v_fixture_nocompanion.mdl"
assert_eq "probe v_fixture_nocompanion status" "$(jfield "$PROBE_JSON" status)" "failed"
assert_eq "probe v_fixture_nocompanion reason" "$(jfield "$PROBE_JSON" reason)" "viewmodel companions are missing"
assert_eq "probe v_fixture_nocompanion resourceComplete" "$(jfield "$PROBE_JSON" resourceComplete)" "false"
assert_eq "probe v_fixture_nocompanion fallbackStable" "$(jfield "$PROBE_JSON" fallbackStable)" "false"

echo "-- a missing path and a non-viewmodel path have distinct reasons --"
probe_run "missing path" "$TF" "models/weapons/v_models/v_ada_does_not_exist.mdl"
assert_eq "probe missing status" "$(jfield "$PROBE_JSON" status)" "failed"
assert_eq "probe missing reason" "$(jfield "$PROBE_JSON" reason)" "viewmodel was not found"
assert_eq "probe missing resolution" "$(jfield "$PROBE_JSON" resolution)" "missing"
probe_run "non-viewmodel path" "$TF" "models/player/scout.mdl"
assert_eq "probe non-viewmodel reason" "$(jfield "$PROBE_JSON" reason)" "path is not a viewmodel"

echo "-- the FOV contract --"
assert_eq "probe default FOV" "$(jfield "$PROBE_JSON" defaultFov)" "90"
assert_eq "probe FOV low" "$(jfield "$PROBE_JSON" fovLow)" "40"
assert_eq "probe FOV high" "$(jfield "$PROBE_JSON" fovHigh)" "120"
assert_eq "probe legacy FOV default" "$(jfield "$PROBE_JSON" legacyFovDefault)" "80"
assert_eq "probe legacy 80 migrates to 90" "$(jfield "$PROBE_JSON" migratedLegacyFov)" "90"
assert_eq "probe an explicit 101 is kept" "$(jfield "$PROBE_JSON" chosenFovKept)" "101"
probe_run "fov 90" "$TF" "$MODEL" --fov 90
assert_eq "probe fov 90 is kept" "$(jfield "$PROBE_JSON" fov)" "90"
assert_eq "probe fov 90 is not clamped" "$(jfield "$PROBE_JSON" fovClamped)" "false"
probe_run "fov 10" "$TF" "$MODEL" --fov 10
assert_eq "probe fov 10 clamps to 40" "$(jfield "$PROBE_JSON" fov)" "40"
assert_eq "probe fov 10 reports the clamp" "$(jfield "$PROBE_JSON" fovClamped)" "true"
probe_run "fov 200" "$TF" "$MODEL" --fov 200
assert_eq "probe fov 200 clamps to 120" "$(jfield "$PROBE_JSON" fov)" "120"

echo "-- the hand matrices, and that the mirror is the only difference --"
probe_run "hand right" "$TF" "$MODEL" --hand right
assert_eq "probe right hand X basis" "$(jfield "$PROBE_JSON" rightHandX)" "1"
assert_eq "probe hand mirror probe" "$(jfield "$PROBE_JSON" handProbePassed)" "true"
assert_eq "probe mirror touches only X" "$(jfield "$PROBE_JSON" mirrorOnlyX)" "true"
probe_run "hand left" "$TF" "$MODEL" --hand left
assert_eq "probe left hand is selected" "$(jfield "$PROBE_JSON" hand)" "left"
assert_eq "probe left hand X basis" "$(jfield "$PROBE_JSON" leftHandX)" "-1"

echo "-- the FOV contract probe --"
FOV_JSON=$("$FOV_PROBE" 2>/dev/null | tr -d '\r' | tail -1)
echo "  $FOV_JSON"
assert_eq "fov probe contractOk" "$(jfield "$FOV_JSON" contractOk)" "true"
assert_eq "fov probe probeValues" "$(jfield "$FOV_JSON" probeValues)" "9"
assert_eq "fov probe defaultFov" "$(jfield "$FOV_JSON" defaultFov)" "90"
assert_eq "fov probe legacyEightyMigrates" "$(jfield "$FOV_JSON" legacyEightyMigrates)" "true"

echo "-- the fixture bytes must be reproducible from the generator --"
GEN_DIR="$TRACE_DIR/fixture-regen"
rm -rf "$GEN_DIR"
"$PY" viewmodel-fixture-gen.py "$GEN_DIR/tf" > "$TRACE_DIR/fixture-gen.txt" 2>&1
if diff -r "$GEN_DIR/tf" "$FIXTURE_DIR/tf" > "$TRACE_DIR/fixture-regen.diff" 2>&1; then
  echo "  OK   the committed fixtures are byte-identical to a fresh generation"
  ASSERTIONS=$((ASSERTIONS + 1))
else
  echo "  FAIL the committed fixtures differ from a fresh generation:"
  sed 's/^/       | /' "$TRACE_DIR/fixture-regen.diff" | head -10
  fail=1; FAIL_LINES+=("fixture provenance: the committed fixtures are not reproducible")
fi
}

# ===========================================================================
# Section 2 -- the product's runtime reading and the pass order
# ===========================================================================
section2() {
echo
echo "=== 2/7 the product's runtime reading and the pass order ==="
ensure_product vm90 --viewmodel "$MODEL" --viewmodel-fov 90
assert_eq "product vm90 rc" "$LAST_RC" "0"
assert_contains "product vm90 path" "$LAST_READING" "viewModelPath=$MODEL"
assert_contains "product vm90 resourceStatus" "$LAST_READING" "resourceStatus=ok"
assert_contains "product vm90 attachmentStatus" "$LAST_READING" "attachmentStatus=ok(valid="
assert_ge "product vm90 sequenceCount" "$(field "$LAST_READING" sequenceCount)" 1
assert_eq "product vm90 fov" "$(field "$LAST_READING" fov)" "90.0"
assert_eq "product vm90 hand" "$(field "$LAST_READING" hand)" "right"
assert_ge "product vm90 drawn" "$(field "$LAST_READING" drawn)" 1
# The pass's depth policy, the reading the plan asks for next to `drawn`. It is a
# constant for this pass and is asserted anyway: a change to it should redden a gate
# rather than quietly change the picture. D3D11 cannot read a state object back, so
# the value comes from the description CreateDepthStencilState was handed.
assert_eq "product vm90 depthWrite" "$(field "$LAST_ORDER" depthWrite)" "1"
assert_eq "product vm90 depthFunc" "$(field "$LAST_ORDER" depthFunc)" "less_equal"
assert_eq "product vm90 reason" "${LAST_READING##*reason=}" "drawn"
assert_eq "product vm90 afterWorld" "$(field "$LAST_ORDER" afterWorld)" "1"
assert_gt "product vm90 viewModelPass" "$(field "$LAST_ORDER" viewModelPass)" "$(field "$LAST_ORDER" worldPass)"
assert_ge "product vm90 drawCalls" "$(field "$LAST_ORDER" drawCalls)" 1
assert_ge "product vm90 vertices" "$(field "$LAST_ORDER" vertices)" 3
assert_contains "product vm90 alignment" "$LAST_ORDER" "alignment=weapon_bone"
# `drawn=1` only says a draw was issued. The centre is computed from the same mvp
# the shader gets, so this is the reading that separates "the pass ran" from "the
# weapon was anywhere the viewport could show" -- the failure this pass already
# had once, when the whole model sat behind the near plane at clip w = -467.
assert_contains "product vm90 centre on screen" "$LAST_CENTRE" "onScreen=1"

# Three runs, and the reason there are three: "no --viewmodel-fov means 90" is only
# a statement about the build if the run had nothing stored to read, and "the stored
# value is used" is only a statement if a stored value was actually supplied. One
# relational assertion (`fov == settingsFov`, both off the same line) satisfied both
# shapes without pinning either, and read 40 on this machine because that is what the
# user's real settings file held.
echo "-- with no flag and no stored value the pass takes the compiled default --"
ensure_product vmdefault --viewmodel "$MODEL"
assert_eq "product vmdefault rc" "$LAST_RC" "0"
assert_eq "product vmdefault fov (no flag, no stored value -> compiled default)" \
  "$(field "$LAST_READING" fov)" "90.0"
assert_eq "product vmdefault settingsFov (nothing was stored to read)" \
  "$(field "$LAST_READING" settingsFov)" "90.0"

echo "-- a stored value is the source of truth, and an old 80 migrates --"
printf '[renderer]\nviewModelFov=60\n' > "$TRACE_DIR/vmstored-settings.ini"
ensure_product vmstored --viewmodel "$MODEL"
assert_eq "product vmstored rc" "$LAST_RC" "0"
assert_eq "product vmstored fov (a stored 60 is honoured, not overridden by 90)" \
  "$(field "$LAST_READING" fov)" "60.0"
printf '[renderer]\nviewModelFov=80\n' > "$TRACE_DIR/vmlegacy-settings.ini"
ensure_product vmlegacy --viewmodel "$MODEL"
assert_eq "product vmlegacy rc" "$LAST_RC" "0"
assert_eq "product vmlegacy fov (a stored legacy 80 migrates to 90)" \
  "$(field "$LAST_READING" fov)" "90.0"
}

# ===========================================================================
# Section 3 -- first-person projection and the hand mirror
# ===========================================================================
section3() {
echo
echo "=== 3/7 first-person projection and the hand mirror ==="
# The baseline: the same scene with no ViewModel at all. Without it, "the weapon
# is on screen" would be satisfied by any frame that differs from any other.
ensure_product novm
assert_eq "product novm rc" "$LAST_RC" "0"
ensure_product vm90 --viewmodel "$MODEL" --viewmodel-fov 90
ensure_product vm40 --viewmodel "$MODEL" --viewmodel-fov 40
assert_eq "product vm40 fov" "$(field "$LAST_READING" fov)" "40.0"
ensure_product vm120 --viewmodel "$MODEL" --viewmodel-fov 120
assert_eq "product vm120 fov" "$(field "$LAST_READING" fov)" "120.0"
ensure_product vmleft --viewmodel "$MODEL" --viewmodel-fov 90 --viewmodel-hand left
assert_eq "product vmleft hand" "$(field "$LAST_READING" hand)" "left"
# The clamp must reach the projection, not only the printed number: an
# out-of-range request has to render exactly what the boundary value renders.
ensure_product vmfov10 --viewmodel "$MODEL" --viewmodel-fov 10
assert_eq "product fov 10 is clamped to 40" "$(field "$LAST_READING" fov)" "40.0"
ensure_product vmfov200 --viewmodel "$MODEL" --viewmodel-fov 200
assert_eq "product fov 200 is clamped to 120" "$(field "$LAST_READING" fov)" "120.0"

# Every frame below is compared against another one, so they all have to come from a
# capture point whose world is the same. The capture point is a property of the run,
# not of the request: the product captures its frame at the *first* draw, and how far
# the world has advanced by then is decided by how long the loader took.
#
# On this demo that produces exactly two outcomes -- the first draw after one scene
# update (playback tick 0, sceneTick 10252) or after two (tick 16, sceneTick 10268).
# The frames are byte-identical between them, and that is measured rather than
# assumed: the clamp pairs below compare two *separate* runs, and in the run that
# recorded this evidence vmfov10 landed on 10268 while vm40 landed on 10252 and the
# two still differed by 0.0000%.
#
# The set is pinned, and a third value is a red rather than a tolerance to widen. The
# capture point is a *premise* of every comparison below: from a third point, a pair
# that must be byte-identical could differ for a reason that has nothing to do with
# the ViewModel, and the difference checks would be attributing scene motion to the
# pass. A new value means the frames have to be re-shown to agree first.
#
# This replaced an assertion that all seven frames report the same `tick=`. That
# counter varies run to run for the same command -- measured: one run reported
# `16 16 0 16 0 16 0` while every compared pair was byte-identical, and at
# --capture-tick 64 two runs reported different ticks and produced byte-identical
# BMPs. It was asserting equality of a label while claiming sameness of the world,
# and it reddened a run whose frames were provably identical pixel for pixel. The
# premise has to be checked -- but checked for the thing the comparison needs.
CAPTURE_SCENE_TICKS="10252 10268"
COMPARE_TICKS=""
COMPARE_SCENE_TICKS=""
COMPARE_POINTS_KNOWN=0
# `frame`, not `name`, and declared local. This loop lives in a section *function*,
# and a bash function's variables are global unless declared local -- so a bare
# `name` here reaches out and overwrites the mutation driver's own loop variable.
# The driver then looked up MUTATION_TARGETS[vmfov200] and died under `set -u` on
# the *restore* build, i.e. after the mutation had been applied and the sections
# had run, which is the worst possible moment to lose the run. The driver now
# iterates `mutation`, and this loop is local. Both halves are needed: a future
# section can reintroduce the collision without ever touching the driver.
local frame point allowed
for frame in novm vm90 vm40 vm120 vmleft vmfov10 vmfov200; do
  COMPARE_TICKS="$COMPARE_TICKS $(capture_tick "$frame")"
  point=$(capture_scene_tick "$frame")
  COMPARE_SCENE_TICKS="$COMPARE_SCENE_TICKS ${point:--}"
  for allowed in $CAPTURE_SCENE_TICKS; do
    if [ "$point" = "$allowed" ]; then COMPARE_POINTS_KNOWN=$((COMPARE_POINTS_KNOWN + 1)); fi
  done
done
echo "  ..   capture ticks:$(printf '%s' "$COMPARE_TICKS")"
echo "  ..   capture sceneTicks:$(printf '%s' "$COMPARE_SCENE_TICKS")  (measured points: $CAPTURE_SCENE_TICKS)"
# Counted, not asserted as "no failures": the reading has to say how many frames were
# checked, so that a frame whose trace never mentions a scene counts as unknown
# rather than as a silent pass.
assert_eq "all 7 compared frames came from a measured capture point" \
  "$COMPARE_POINTS_KNOWN" "7"

bmp_diff "the weapon appears against the frame without it" \
  "$TRACE_DIR/novm.bmp" "$TRACE_DIR/vm90.bmp" --min-diff-percent 0.5 --min-delta 100
bmp_diff "FOV 40 and FOV 90 are different pictures" \
  "$TRACE_DIR/vm90.bmp" "$TRACE_DIR/vm40.bmp" --min-diff-percent 3 --min-delta 100
bmp_diff "FOV 90 and FOV 120 are different pictures" \
  "$TRACE_DIR/vm90.bmp" "$TRACE_DIR/vm120.bmp" --min-diff-percent 0.5 --min-delta 100
bmp_diff "the left hand is a different picture from the right" \
  "$TRACE_DIR/vm90.bmp" "$TRACE_DIR/vmleft.bmp" --min-diff-percent 0.2 --min-delta 100
bmp_diff "a clamped FOV 10 renders exactly what FOV 40 renders" \
  "$TRACE_DIR/vmfov10.bmp" "$TRACE_DIR/vm40.bmp" --max-delta 0
bmp_diff "a clamped FOV 200 renders exactly what FOV 120 renders" \
  "$TRACE_DIR/vmfov200.bmp" "$TRACE_DIR/vm120.bmp" --max-delta 0
}

# ===========================================================================
# Section 4 -- a missing or damaged resource disables the pass, not the run
# ===========================================================================
section4() {
echo
echo "=== 4/7 a missing resource disables the ViewModel pass, not the run ==="
ensure_product novm
ensure_product vmmissing --viewmodel "models/weapons/v_models/v_ada_does_not_exist.mdl" --viewmodel-fov 90
assert_eq "product vmmissing rc" "$LAST_RC" "0"
assert_eq "product vmmissing drawn" "$(field "$LAST_READING" drawn)" "0"
assert_eq "product vmmissing reason" "${LAST_READING##*reason=}" "viewmodel was not found"
assert_contains "product vmmissing resourceStatus" "$LAST_READING" "resourceStatus=incomplete("
assert_eq "product vmmissing afterWorld" "$(field "$LAST_ORDER" afterWorld)" "0"
assert_eq "product vmmissing viewModelPass" "$(field "$LAST_ORDER" viewModelPass)" "0"
bmp_diff "a missing ViewModel leaves the frame alone" \
  "$TRACE_DIR/novm.bmp" "$TRACE_DIR/vmmissing.bmp" --max-delta 0

echo "-- the fixture root: a damaged attachment, an unplaceable name, a missing companion --"
# These three fixtures have a real MDL header, bone table and attachment table but
# no decodable mesh, so `drawn=0` here is about the mesh and not about the defect
# under test. The assertions that carry the defect are the ones about the
# *attachment table*: `invalid=1` for the damaged fixture (against the control in
# section 1, which is one int32 away and reads `invalid=0`), and `alignment=-` for
# the fixture whose only attachment is valid but misnamed. The fixture root also
# has to be a root the product accepts (materials/models/maps present), or the
# product drops `--tf-root` and every one of these reads `viewmodel was not found`.
run_fixture_product vmfdamaged "$FIXTURE_MODEL_DIR/v_fixture_damaged.mdl"
assert_eq "product fixture damaged rc" "$LAST_RC" "0"
assert_contains "product fixture damaged attachmentStatus" "$LAST_READING" "attachmentStatus=degraded("
assert_contains "product fixture damaged invalid count" "$LAST_READING" "invalid=1"
assert_eq "product fixture damaged drawn" "$(field "$LAST_READING" drawn)" "0"
assert_contains "product fixture damaged alignment" "$LAST_ORDER" "alignment=-"
run_fixture_product vmfnoanchor "$FIXTURE_MODEL_DIR/v_fixture_noanchor.mdl"
assert_eq "product fixture noanchor rc" "$LAST_RC" "0"
assert_contains "product fixture noanchor attachmentStatus" "$LAST_READING" "attachmentStatus=ok(valid=1"
assert_eq "product fixture noanchor alignment" "$(field "$LAST_ORDER" alignment)" "-"
run_fixture_product vmfnocompanion "$FIXTURE_MODEL_DIR/v_fixture_nocompanion.mdl"
assert_eq "product fixture nocompanion rc" "$LAST_RC" "0"
assert_eq "product fixture nocompanion drawn" "$(field "$LAST_READING" drawn)" "0"
assert_eq "product fixture nocompanion reason" "${LAST_READING##*reason=}" "viewmodel companions are missing"
assert_contains "product fixture nocompanion resourceStatus" "$LAST_READING" "resourceStatus=incomplete("
}

# ===========================================================================
# Section 5 -- the animation input is missing and the bind pose is drawn
# ===========================================================================
section5() {
echo
echo "=== 5/7 the animation input is missing and the bind pose is drawn ==="
ensure_product vm90 --viewmodel "$MODEL" --viewmodel-fov 90
assert_eq "product vm90 animationInput" "$(field "$LAST_ORDER" animationInput)" "missing"
assert_eq "product vm90 skinning" "$(field "$LAST_ORDER" skinning)" "0"
# `skinning` is what the pass did and `boneTableCoverage` is the pair it compared,
# so "no table at all" (0/0) and "a refused table" (1/45) cannot read the same.
assert_eq "product vm90 boneTableCoverage" "$(field "$LAST_ORDER" boneTableCoverage)" "0/0"
# The pair. Same scene, same FOV, and the only difference is that one identity
# matrix per bone is uploaded: the skinning branch must run (skinning=1, and the
# bone table must cover the whole skeleton -- the pass refuses a short one), and
# the picture must not move. A real pose would move it; a pass that ignored the
# bone table would leave `skinning` at 0 and this pair would collapse.
ensure_product vmident --viewmodel "$MODEL" --viewmodel-fov 90 --viewmodel-identity-bones
assert_eq "product vmident rc" "$LAST_RC" "0"
assert_eq "product vmident skinning" "$(field "$LAST_ORDER" skinning)" "1"
assert_contains "product vmident animationInput" "$(field "$LAST_ORDER" animationInput)" "bones:"
# The table covers the whole skeleton, which is why the pass skinned at all. Both
# halves are asserted: equality alone would also hold for two empty strings.
VMIDENT_COVERAGE=$(field "$LAST_ORDER" boneTableCoverage)
assert_gt "product vmident boneTableCoverage numerator" "${VMIDENT_COVERAGE%%/*}" 0
assert_eq "product vmident boneTableCoverage covers the skeleton" \
  "${VMIDENT_COVERAGE%%/*}" "${VMIDENT_COVERAGE##*/}"
assert_eq "product vmident reason" "${LAST_READING##*reason=}" "drawn"
bmp_diff "identity skinning does not deform the bind pose" \
  "$TRACE_DIR/vm90.bmp" "$TRACE_DIR/vmident.bmp" --max-delta 0

# The refusal. A table that cannot cover the skeleton must not be used: the pass
# falls back to the bind pose, keeps drawing, and names the shortfall in `reason`.
# Nothing else can reach that branch -- `--viewmodel-identity-bones` always
# uploads one matrix per bone, and a non-finite matrix takes the *disabled* path
# instead of this one -- so without --viewmodel-bone-limit the refusal is a branch
# no run has taken. `drawn=1` with `skinning=0` is the pair that says "the pass
# drew the bind pose because it refused the table" rather than "the pass did not
# run", and the reason names the numbers it compared.
ensure_product vmshort --viewmodel "$MODEL" --viewmodel-fov 90 --viewmodel-identity-bones --viewmodel-bone-limit 1
assert_eq "product vmshort rc" "$LAST_RC" "0"
assert_eq "product vmshort drawn" "$(field "$LAST_READING" drawn)" "1"
assert_eq "product vmshort skinning" "$(field "$LAST_ORDER" skinning)" "0"
# One matrix offered against a real skeleton: the numerator is the one that was
# uploaded and the denominator is the one the mesh needs.
VMSHORT_COVERAGE=$(field "$LAST_ORDER" boneTableCoverage)
assert_eq "product vmshort boneTableCoverage numerator" "${VMSHORT_COVERAGE%%/*}" "1"
assert_gt "product vmshort boneTableCoverage denominator" "${VMSHORT_COVERAGE##*/}" 1
assert_contains "product vmshort reason" "${LAST_READING##*reason=}" "the bone table covers 1 of"
assert_contains "product vmshort reason names the skeleton" "${LAST_READING##*reason=}" "bones; drawing the bind pose"
bmp_diff "a refused short bone table renders the same bind pose" \
  "$TRACE_DIR/vm90.bmp" "$TRACE_DIR/vmshort.bmp" --max-delta 0
}

# ===========================================================================
# Section 6 -- the witness frames
# ===========================================================================
section6() {
echo
echo "=== 6/7 the witness frames ==="
ensure_product vm90 --viewmodel "$MODEL" --viewmodel-fov 90
ensure_product vm40 --viewmodel "$MODEL" --viewmodel-fov 40
ensure_product vm120 --viewmodel "$MODEL" --viewmodel-fov 120
ensure_product vmleft --viewmodel "$MODEL" --viewmodel-fov 90 --viewmodel-hand left
ensure_product vmident --viewmodel "$MODEL" --viewmodel-fov 90 --viewmodel-identity-bones
ensure_product vmshort --viewmodel "$MODEL" --viewmodel-fov 90 --viewmodel-identity-bones --viewmodel-bone-limit 1
ensure_product vmmissing --viewmodel "models/weapons/v_models/v_ada_does_not_exist.mdl" --viewmodel-fov 90
load_reading vm90
# Both ticks, labelled: `demo_tick` is the demo's entity tick and `playback_tick`
# is the position --capture-tick named. Reporting only one of them under the name
# "demo tick" is how a caption ends up with a number that is true of nothing.
DEMO_TICK="${LAST_SCENE_TICK:-?}"
PLAYBACK_TICK="${LAST_TICK:-?}"
MODEL_LABEL=$(basename "$MODEL")
STAMP="$DEMO_LABEL demo_tick=$DEMO_TICK playback_tick=$PLAYBACK_TICK $MODEL_LABEL"
witness viewmodel-fov90-right "$TRACE_DIR/vm90.bmp" \
  "$STAMP fov=90 hand=right drawn=1"
witness viewmodel-default-fov90 "$TRACE_DIR/vmdefault.bmp" \
  "$STAMP no-fov-flag compiled-default-fov=90 drawn=1"
witness viewmodel-fov40-right "$TRACE_DIR/vm40.bmp" \
  "$STAMP fov=40 hand=right drawn=1"
witness viewmodel-fov120-right "$TRACE_DIR/vm120.bmp" \
  "$STAMP fov=120 hand=right drawn=1"
witness viewmodel-fov90-left "$TRACE_DIR/vmleft.bmp" \
  "$STAMP fov=90 hand=left drawn=1"
witness viewmodel-identity-bindpose "$TRACE_DIR/vmident.bmp" \
  "$STAMP fov=90 identity-bones skinning=1 bind-pose"
witness viewmodel-shortbonetable "$TRACE_DIR/vmshort.bmp" \
  "$STAMP fov=90 bone-limit=1 refused skinning=0 bind-pose"
witness viewmodel-absent "$TRACE_DIR/vmmissing.bmp" \
  "$DEMO_LABEL demo_tick=$DEMO_TICK playback_tick=$PLAYBACK_TICK no-viewmodel drawn=0 reason=viewmodel-was-not-found"
}

# ===========================================================================
# Section 7 -- the pass readings agree with themselves
# ===========================================================================
# Every `gpu: viewmodel pass order` line a run writes is read off the same state, so
# every line has to report the same `boneTableCoverage`. One of them did not:
# `%zu` was fed a `uint32_t`, and on x64 the upper half of that register is not
# zeroed, so 15 of the 16 frames this gate captures printed
# `boneTableCoverage=0/4598175219545276416` on every line but the last -- the low
# half was the correct bone count and the high half was the bit pattern of the double
# 0.25 left in the register. `vmident` printed `45/4598175219545276461`, which is the
# same defect with a non-zero low half, and that is what identified it.
#
# The gate read `tail -1`, so it never saw any of it, and every assertion about the
# bone count was green throughout. That is why this section is about the reading
# rather than about the value: "check the number" was already covered, and a number
# that is wrong on 15 of 16 frames is not covered by checking one of them.
#
# `novm` is the one frame that may carry no order line: it runs without --viewmodel,
# so the pass never runs and main.cpp prints nothing for it. That is excluded by name
# -- and the exclusion is *checked*, not assumed. An assertion below states that
# `novm`'s trace really has no order line, so if the pass ever starts printing one,
# the exclusion shows up as stale instead of quietly hiding a frame. Every other
# frame must report exactly one distinct value, and the number of frames examined is
# pinned, so a frame that dropped out of the loop is a red rather than an absence.
section7() {
  echo
  echo "=== 7/7 the pass readings agree with themselves ==="
  assert_eq "the frame with no ViewModel request is the frame with no order line" \
    "$(grep -ac 'viewmodel pass order' "$TRACE_DIR/novm.trace" 2>/dev/null)" "0"
  local frame values distinct checked=0 total=0
  for frame in $FRAME_NAMES; do
    total=$((total + 1))
    if [ "$frame" = novm ]; then continue; fi
    [ -s "$TRACE_DIR/$frame.trace" ] || continue
    values=$(grep -a "viewmodel pass order" "$TRACE_DIR/$frame.trace" | tr -d '\r' \
      | tr ' ' '\n' | sed -n 's/^boneTableCoverage=//p' | sort -u | tr '\n' ',')
    distinct=$(printf '%s' "$values" | tr ',' '\n' | grep -c .)
    checked=$((checked + 1))
    assert_eq "trace $frame reports one boneTableCoverage on every order line" \
      "$distinct" "1"
  done
  assert_eq "the reading-integrity section examined every frame that has a reading" \
    "$checked" "$((total - 1))"
}

run_sections() {
  local section
  for section in $SECTIONS; do
    "section$section"
  done
}

# ===========================================================================
# mutation support
# ===========================================================================
SOURCE=""
BACKUP=""
MUTATION_SOURCES="native/src/model_loader.cpp native/src/viewmodel.cpp native/src/native_renderer.cpp native/include/viewmodel.h"

restore_sources() {
  local file
  [ -n "$SOURCE" ] || return 0
  for file in $SOURCE; do
    [ -f "$BACKUP/$file" ] && cp "$BACKUP/$file" "$file"
  done
}
restore_if_patched() {
  if [ -n "${SOURCE:-}" ] && [ -n "${BACKUP:-}" ] && [ -d "${BACKUP:-/nonexistent}" ]; then
    restore_sources
  fi
  rmdir "$LOCK_DIR" 2>/dev/null || true
}
trap restore_if_patched EXIT

apply_mutation() { # apply_mutation <name> -> patches tracked sources
  local name="$1"
  case "$name" in
    attachment-bone)   SOURCE="native/src/model_loader.cpp" ;;
    fov-default)       SOURCE="native/include/viewmodel.h" ;;
    hand-matrix)       SOURCE="native/src/viewmodel.cpp" ;;
    companion-missing) SOURCE="native/src/model_loader.cpp native/src/viewmodel.cpp" ;;
    draw-skipped)      SOURCE="native/src/native_renderer.cpp" ;;
  esac
  BACKUP=".scratch/viewmodel-contract-$name.bak"
  rm -rf "$BACKUP"; mkdir -p "$BACKUP"
  local file
  for file in $SOURCE; do
    mkdir -p "$BACKUP/$(dirname "$file")"
    cp "$file" "$BACKUP/$file"
  done
  if ! "$PY" - "$name" <<'PYEOF'
import io, sys

# Each patch is one coherent defect. Every anchor is asserted to match exactly
# once: a pattern that did not match would mean the mutated build was never built,
# and "the mutation did not move the reading" would then be a statement about the
# wrong binary. The files are CRLF on a fresh checkout and LF here, so the patch
# works on a normalised copy and restores the original line endings on write.
PATCHES = {
  "attachment-bone": [(
    "native/src/model_loader.cpp",
    "      readAt(b, off, nameIndex); readAt(b, off + 4, flags); readAt(b, off + 8, bone);",
    "      readAt(b, off, nameIndex); readAt(b, off + 4, flags); readAt(b, off + 4, bone);"
    "  // MUTATION: the flags word is read as the bone index",
  )],
  "fov-default": [
    ("native/include/viewmodel.h",
     "inline constexpr float kDefaultViewModelFov = 90.0f;",
     "inline constexpr float kDefaultViewModelFov = 80.0f;  // MUTATION: the old default"),
    ("native/include/viewmodel.h",
     "inline constexpr int kDefaultViewModelFovInt = 90;",
     "inline constexpr int kDefaultViewModelFovInt = 80;  // MUTATION: the old default"),
  ],
  "hand-matrix": [(
    "native/src/viewmodel.cpp",
    "  matrix[0] = hand == ViewModelHand::Left ? -1.0f : 1.0f;",
    "  matrix[0] = 1.0f;  // MUTATION: the left hand stops mirroring",
  )],
  "companion-missing": [
    ("native/src/model_loader.cpp",
     "  result.companionSetComplete = result.mdlFound && result.vvdFound\n"
     "      && (result.dx90VtxFound || result.dx80VtxFound || result.swVtxFound);",
     "  result.companionSetComplete = result.mdlFound;  // MUTATION: companions are assumed present"),
    ("native/src/viewmodel.cpp",
     "  resources.companionsComplete = resources.mdlReadable && resources.vvdPresent && resources.vtxPresent;",
     "  resources.companionsComplete = resources.mdlReadable;  // MUTATION: companions are assumed present"),
  ],
  "draw-skipped": [(
    "native/src/native_renderer.cpp",
    "  drawViewModel();",
    "  // MUTATION: the ViewModel pass is never called.\n  // drawViewModel();",
  )],
}

name = sys.argv[1]
for path, old, new in PATCHES[name]:
    raw = io.open(path, "rb").read()
    crlf = b"\r\n" in raw
    text = raw.decode("utf-8")
    if crlf:
        text = text.replace("\r\n", "\n")
    if text.count(old) != 1:
        sys.stderr.write("anchor matched %d times in %s\n" % (text.count(old), path))
        raise SystemExit(1)
    text = text.replace(old, new)
    if crlf:
        text = text.replace("\n", "\r\n")
    io.open(path, "wb").write(text.encode("utf-8"))
    sys.stderr.write("patched %s\n" % path)
PYEOF
  then
    echo "FATAL: the mutation did not apply; the mutated build would prove nothing"
    restore_sources
    SOURCE=""; BACKUP=""
    exit 1
  fi
}

# ===========================================================================
# the driver
# ===========================================================================
if [ -z "$MUTATION" ]; then
  clear_frames
  BUILD_TOKEN=$(build_token)
  run_sections
  {
    echo "# viewmodel-contract-check.sh readings ($(date -u +%Y-%m-%dT%H:%M:%SZ))"
    echo "# demo: $DEMO"
    echo "# model: $MODEL"
    echo "# fixture root: $FIXTURE_DIR/tf"
    echo "# assertions that held: $ASSERTIONS"
    echo "# failures: $fail"
    # `frame` for the same reason as in section3: this block is top-level, so a
    # bare `name` here would sit in the global scope for the rest of the file.
    for frame in $FRAME_NAMES; do
      [ -f "$TRACE_DIR/$frame.trace" ] || continue
      # The trace prefix is the milliseconds since start. Stripped here so the
      # committed readings file is the readings and not readings with a clock in
      # front of them.
      echo "$frame: $(grep -a 'viewModelPath=' "$TRACE_DIR/$frame.trace" | tr -d '\r' | tail -1 | sed 's/^[0-9]\{1,\}\(\.[0-9]*\)\? //')"
      echo "$frame order: $(grep -a 'gpu: viewmodel pass order' "$TRACE_DIR/$frame.trace" | tr -d '\r' | tail -1 | sed 's/^[0-9]\{1,\}\(\.[0-9]*\)\? //')"
      echo "$frame centre: $(grep -a 'gpu: viewmodel centre' "$TRACE_DIR/$frame.trace" | tr -d '\r' | tail -1 | sed 's/^[0-9]\{1,\}\(\.[0-9]*\)\? //')"
    done
  } > "$OUT/readings.txt"
  echo
  echo "assertions_ok=$ASSERTIONS"
  if [ "$fail" -eq 0 ]; then
    echo "VIEWMODEL-CONTRACT=PASS"
    exit 0
  fi
  echo "VIEWMODEL-CONTRACT=FAIL"
  exit 1
fi

# ---- mutation mode -------------------------------------------------------
# One mutation at a time: patch, rebuild, run the sections it is expected to move,
# and require a *named* assertion to go red. "The script exited non-zero" is not
# the claim -- a mutation that broke the build, or that reddened an unrelated
# assertion, would satisfy that and prove nothing about the check under test.
# Each mutation runs only the sections it can move: the straight run is what
# proves the sections it does not run stay green, and a mutation that costs as
# much as the gate it guards stops being run.
declare -A EXPECT_RED=(
  [attachment-bone]="probe v_fixture_damaged invalidAttachments"
  [fov-default]="product vmdefault fov"
  [hand-matrix]="left hand is a different picture"
  [companion-missing]="companions are missing"
  [draw-skipped]="product vm90 drawn"
)
declare -A MUTATION_SECTIONS=(
  [attachment-bone]="1"
  [fov-default]="1 2"
  [hand-matrix]="1 3"
  [companion-missing]="1 4"
  [draw-skipped]="2 3"
)
declare -A MUTATION_TARGETS=(
  [attachment-bone]="viewmodel_contract_probe"
  [fov-default]="tf2_demo_native viewmodel_contract_probe viewmodel_fov_probe"
  [hand-matrix]="viewmodel_contract_probe tf2_demo_native"
  [companion-missing]="viewmodel_contract_probe tf2_demo_native"
  [draw-skipped]="tf2_demo_native"
)
HASHES_BEFORE=$(sha256sum $MUTATION_SOURCES 2>/dev/null)
MUTATION_RESULTS=()
# `mutation`, not `name`: the sections this loop runs are shell functions, and a
# bare loop variable inside a function writes to the global scope. The driver must
# not depend on section-local hygiene for its own loop variable -- that dependency
# is exactly what aborted the first mutation run, after the patch was applied and
# before the tree was put back.
for mutation in "${MUTATIONS[@]}"; do
  echo
  echo "###################################################################"
  echo "=== mutation: $mutation ==="
  # A mutation that is not fully described by the three tables is a bug in this
  # file, not a mutation result. Named here, before anything is built, so it cannot
  # surface as an unbound-variable abort halfway through the loop body.
  if [ -z "${MUTATION_SECTIONS[$mutation]:-}" ] || [ -z "${MUTATION_TARGETS[$mutation]:-}" ] \
     || [ -z "${EXPECT_RED[$mutation]:-}" ]; then
    echo "FATAL: mutation '$mutation' is not fully described by the tables"
    exit 1
  fi
  fail=0; ASSERTIONS=0; FAIL_LINES=(); SECTIONS="${MUTATION_SECTIONS[$mutation]}"
  # The frames must be re-captured with the mutated binary; a frame left over from
  # the previous run would make `ensure_product` reuse it and the mutation would
  # read green for the wrong reason.
  clear_frames
  apply_mutation "$mutation"
  bash build-target.sh ${MUTATION_TARGETS[$mutation]} > "$TRACE_DIR/mutation-$mutation-build.txt" 2>&1
  build_rc=$?
  echo "mutation build rc=$build_rc"
  if [ "$build_rc" -ne 0 ]; then
    echo "  FAIL the mutated build did not compile; the mutation proves nothing"
    tail -5 "$TRACE_DIR/mutation-$mutation-build.txt" | sed 's/^/       | /'
    restore_sources
    MUTATION_RESULTS+=("$mutation: BUILD-FAILED")
    continue
  fi
  # Recomputed here rather than once at the top: the whole point is that it changes
  # when the mutated binary lands, so the frames the sections capture are stamped with
  # the mutated build and cannot be mistaken for the clean one's -- or for the
  # previous mutation's.
  BUILD_TOKEN=$(build_token)
  run_sections
  restore_sources
  bash build-target.sh ${MUTATION_TARGETS[$mutation]} > "$TRACE_DIR/mutation-$mutation-restore-build.txt" 2>&1
  restore_rc=$?
  echo "restore build rc=$restore_rc"
  RED_NAMED=0
  if [ "${#FAIL_LINES[@]}" -gt 0 ]; then
    for line in "${FAIL_LINES[@]}"; do
      case "$line" in
        *"${EXPECT_RED[$mutation]}"*) RED_NAMED=1 ;;
      esac
    done
  fi
  if [ "$restore_rc" -ne 0 ]; then
    echo "MUTATION-CAUGHT=FAIL ($mutation: the restore build failed; the tree is not back)"
    MUTATION_RESULTS+=("$mutation: RESTORE-FAILED")
    continue
  fi
  if [ "$fail" -eq 1 ] && [ "$RED_NAMED" -eq 1 ]; then
    {
      echo "# the $mutation mutation was caught: these assertions went red"
      printf '%s\n' "${FAIL_LINES[@]}"
    } > "$OUT/mutation-failure-$mutation.txt"
    echo "MUTATION-CAUGHT=PASS ($mutation turns this step red on '${EXPECT_RED[$mutation]}')"
    MUTATION_RESULTS+=("$mutation: CAUGHT")
  else
    echo "MUTATION-CAUGHT=FAIL ($mutation: fail=$fail, named-assertion-red=$RED_NAMED; expected '${EXPECT_RED[$mutation]}' to go red)"
    if [ "${#FAIL_LINES[@]}" -gt 0 ]; then
      printf '%s\n' "${FAIL_LINES[@]}" | sed 's/^/       | /' | head -10
    else
      echo "       | no assertion went red at all"
    fi
    MUTATION_RESULTS+=("$mutation: NOT-CAUGHT")
  fi
done
SOURCE=""; BACKUP=""

echo
echo "=== the mutated sources must be back where they started ==="
HASHES_AFTER=$(sha256sum $MUTATION_SOURCES 2>/dev/null)
if [ "$HASHES_BEFORE" = "$HASHES_AFTER" ]; then
  echo "  OK   every mutated source is byte-identical to its pre-mutation state"
else
  # Counted into the summary rather than printed and forgotten: a mutation run
  # that left a patched file behind would make the *next* run's readings describe
  # a tree nobody chose, which is the failure this whole file is written against.
  echo "  FAIL a mutated source was not restored:"
  diff <(printf '%s\n' "$HASHES_BEFORE") <(printf '%s\n' "$HASHES_AFTER") | sed 's/^/       | /'
  MUTATION_RESULTS+=("restore: TREE-NOT-RESTORED")
fi

echo
echo "=== mutation summary ==="
printf '%s\n' "${MUTATION_RESULTS[@]}" | sed 's/^/  /'
CAUGHT=$(printf '%s\n' "${MUTATION_RESULTS[@]}" | grep -c ': CAUGHT$')
if [ "$CAUGHT" -eq "${#MUTATIONS[@]}" ]; then
  echo "VIEWMODEL-CONTRACT-MUTATION=PASS ($CAUGHT/${#MUTATIONS[@]} mutations caught)"
  exit 0
fi
echo "VIEWMODEL-CONTRACT-MUTATION=FAIL ($CAUGHT/${#MUTATIONS[@]} caught)"
exit 1
