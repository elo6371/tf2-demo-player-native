#!/usr/bin/env bash
# third-person-weapon-check.sh -- T83: every class the recording draws holds its
# own weapon in its hand, and only when the flag asks for it.
#
# WHY THIS EXISTS
#   "the frame changed when I turned the flag on" is not evidence. The world pass
#   already draws every weapon entity where the demo left it, so an on/off pair
#   can differ simply because a gun vanished from the floor while no gun appeared
#   in a hand. This gate therefore asks the product to paint each drawn instance
#   its own id (--world-entity-id) and measures masks:
#
#     1. OFF IS THE BASELINE. The attach-off frame uses exactly the flags
#        class-pixels-check.sh used for its animated frame (same demo, tick,
#        camera, settings, speed); its sha256 must equal that frame in
#        evidence/t62-matrix/class-pixels/<class>/animated.bmp. Same for the
#        identity frame, which must equal <class>/identity.bmp. If a default path
#        pixel moved, this fires before any count is quoted.
#     2. ON DIFFERS FROM OFF and ON twice is THE SAME FRAME. The replay advances
#        on wall-clock time, so two processes render a different number of steps
#        before the capture tick, and the animation clock is advanced by the chord
#        length of that polyline (delta = |dOrigin| / strideUnits, see
#        animation_clock.cpp), so a run can land on one of exactly TWO animation
#        phases. The phase is externally visible: every capture's summary carries
#        'animsum: ... maxDeltaCycle=<x>' and the two branches differ by more than
#        an order of magnitude (0.838676 vs 9.315151 on the Snakewater demo, 5+5
#        runs, one exception: the 64x playback speed, which is not a capture speed).
#        This gate therefore records the phase of EVERY capture and re-captures
#        (bounded, 3 attempts) until the frames it is about to compare share a
#        phase -- instead of comparing across phases, which is not a comparison.
#        Two attached captures that differ WITH THE SAME PHASE are a real
#        regression and still fail. Neither phase is 'the right one': a pixel
#        count is quotable INSIDE its own branch and must never be compared
#        across branches.
#     3. THE WEAPON IS AT THE BODY, measured on the identity capture: the ledger
#        line 'entity: ... attach=1 heldEnt=<player>' names the id of the instance
#        the attach path pushed, t83-weapon-pixels.py takes that id's mask and
#        requires it to touch the body mask, sit in the body's upper part and not
#        be a second body -- and requires the model-space hold point to be at hand
#        height, away from the model origin (0,0,0 is the FEET, the exact bug).
#     4. THE TRACE NAMES THE HOLD POINT: one 'weaponAttach: class=... source=...
#        bone=... weapon=...' line per placement and a per-frame
#        'scene: weaponAttach held=... drawn=...' ledger. The off run must contain
#        NEITHER: the feature has to be inert when the flag is absent.
#
# NEGATIVE CONTROLS (a gate that cannot fail proves nothing)
#   N1  the identity capture of the OFF run is fed to the pixel checker: with no
#       attach instance it must exit non-zero.
#   N2  the attach ledger line's id is rewritten to the BODY id (same picture):
#       the weapon mask becomes a body-sized blob and the checker must reject it
#       on its shape thresholds, not only on a missing line.
#   N3  '--player-weapon-attach bogus' must exit 13 with the refusal line, so a
#       typo cannot silently fall back to on|off and turn an A/B into two clones.
#
# PREREQUISITES (checked before the first capture, FATAL exit 2 if absent)
#   PRODUCT   native/build-nmake/tf2_demo_native.exe built from THIS tree with
#             --player-weapon-attach, --world-entity-id, --player-animation and
#             --debug-camera; its sha256 is printed and recorded in every row.
#   TF        the TF2 "tf" directory (game assets referenced by the demo).
#   DEMO      the Snakewater recording the class-pixels baseline was taken from.
#   PY        a Python with numpy (the pixel checker reads BMPs by hand).
#   BASELINE  evidence/t62-matrix/class-pixels -- settings.json, ui.json,
#             targets.txt and <class>/{animated,identity}.bmp.
# The baseline comparison mode is SHA EQUALITY against those files (mode 1).
# It is deliberately NOT a soft comparison: if a baseline file is missing this
# gate exits 2 rather than skipping the assertion, because "off changed nothing"
# is the one claim a reviewer must be able to trust. When the gate runs on a tree
# that does not carry evidence/t62-matrix (for example a fresh worktree), point
# T83_WEAPON_BASELINE at a copy -- do not weaken the check.
#
# MUST RUN UNDER THE SHARED HOST LOCK (it launches captures):
#   bash /d/TF2_Native_Worktrees/_shared/hostlock.sh run t83-weapon \
#     bash -c 'cd /d/TF2_Native_Worktrees/t83-weapon && bash third-person-weapon-check.sh'
#
# Usage: bash third-person-weapon-check.sh [--dry-run]
# Exit:  0 = PASS, 1 = FAIL, 2 = a required input is missing (FATAL).
set -uo pipefail
cd "$(dirname "$0")"

OUT=${T83_WEAPON_OUT:-evidence/t83-weapon/weapon-attach}
PRODUCT=${T83_WEAPON_PRODUCT:-native/build-nmake/tf2_demo_native.exe}
PY=${T83_WEAPON_PYTHON:-C:/Users/Administrator/.dsh/dsh-runtimes/dsh-primary-runtime/dependencies/python/python.exe}
BASELINE=${T83_WEAPON_BASELINE:-evidence/t62-matrix/class-pixels}
TARGETS=${T83_WEAPON_TARGETS:-$BASELINE/targets.txt}
TF=${T83_WEAPON_TF_ROOT:-D:/SteamLibrary/steamapps/common/Team Fortress 2/tf}
DEMO=${T83_WEAPON_DEMO:-D:/TF2_Demo_Player/testdata/demos/bb841c6d379ff7c40d0c8baf99f59d8d_matcha-20260927-1347-cp_snakewater_final1.dem}
SPEED=${T83_WEAPON_SPEED:-8}
CAMERA_OFFSET_X=-250
CAMERA_OFFSET_Z=58
CAMERA_PITCH=4.1
SIDE_DISTANCE=250
# The side view the weapon mask is measured from. It is a property of the
# RECORDING, not of the product: the same body and the same hold point project
# differently depending on where the barrel points, and a weapon measured
# end-on to the camera is a tall thin bar that no shape test can separate from a
# second body. Measured 2026-11-06 on one fixed frame per class (same product,
# same tick, only the yaw differs):
#   snakewater  demo/scout/soldier/medic  -> 45 deg 4/4 PASS, 225 deg 1/3 PASS
#   upward      pyro/heavy/spy/engineer/sniper -> 45 deg 3/5, 225 deg 5/5
# so the choice is made once per recording (this env var, printed with every
# capture) and never per class: picking a different yaw for one class until its
# pixels pass would be tuning the gate to the answer.
SIDE_YAW=${T83_WEAPON_SIDE_YAW:-45.0}

fail=0
say_fail() { echo "  FAIL $*"; fail=1; }
sha_of() {
  local f="$1"
  [ -f "$f" ] || { echo "<none>"; return 0; }
  sha256sum "$f" 2>/dev/null | cut -d' ' -f1
}

# --- ANIMATION PHASE: the quantity that decides whether two frames are comparable
# Measured, not assumed (2026-11-06, /d/_shared/b1/phase1, 5 runs per flag set on
# one fixed request -- demo bb841c6d tick 64161 speed 32):
#   off x5  -> maxDeltaCycle=0.838676 x5, sha e92bcfe0 = the class-pixels baseline
#   att x5  -> maxDeltaCycle=0.838676 x4 (sha fcc346f6) and 9.315151 x1 (sha 29f4e872)
#   smoke1  -> off landed on 9.315151 and its frame was e9d4c599, NOT the baseline
# The pairing frame<->phase is one-to-one and the phase is a property of the RUN,
# not of the request: the playback advances on wall-clock time and the animation
# clock integrates the chord length of however many steps that produced, so the
# same request can land on phase A or phase B. Comparing a phase-A frame with a
# phase-B frame measures the phase, not the product. Every capture below is
# therefore labelled with its phase, and a capture that has to match something is
# retried (bounded) until it lands on the same phase.
PHASE_ATTEMPTS=${T83_WEAPON_PHASE_ATTEMPTS:-4}
# phase_of_summary <summary file>: the maxDeltaCycle= field of the window's
# animsum line, or empty when the summary has none (static scene / older evidence).
phase_of_summary() {
  local f="$1"
  [ -f "$f" ] || return 0
  grep -a 'animsum:' "$f" 2>/dev/null | tail -1 | grep -o 'maxDeltaCycle=[0-9.]*' | tail -1
}
phase_of() { phase_of_summary "$1/$2.summary"; }
# want_phase <observed phase>: the observed phase when it is a real one, else
# '-', so a row whose first capture produced no animsum line is not retried
# against a value no run could ever print (static scenes have no animsum line).
want_phase() {
  case "${1:-}" in
    maxDeltaCycle=*) printf '%s' "$1" ;;
    *)               printf '%s' "-" ;;
  esac
}

# run_frame_until_phase <dir> <tag> <wantPhase|-> <tick> <eyeX> <eyeY> <eyeZ> <pitch> <yaw> <flags...>
# The same request repeated in a NEW process until it lands on <wantPhase>, the
# attempt budget runs out, or <wantPhase> is '-'. Leaves the last attempt's files
# in <dir> and prints ONE line on stdout:  rc frameTick sceneTick phase attempts
run_frame_until_phase() {
  local dir="$1" tag="$2" want="$3"; shift 3
  local attempt=1 out rc ft st ph
  while : ; do
    out=$(run_frame "$dir" "$tag" "$@")
    read -r rc ft st <<<"$out"
    ph=$(phase_of "$dir" "$tag")
    [ "$want" = "-" ] && break
    [ -z "$want" ] && break
    [ "$ph" = "$want" ] && break
    [ "$attempt" -ge "$PHASE_ATTEMPTS" ] && break
    attempt=$((attempt + 1))
    echo "  RETRY $tag: landed ${ph:-<no animsum line>}, wanted $want (attempt $attempt of $PHASE_ATTEMPTS)" >&2
  done
  printf '%s %s %s %s %s\n' "${rc:-1}" "${ft:-<none>}" "${st:-<none>}" "${ph:-<none>}" "$attempt"
}

for path in "$PRODUCT" "$TF" "$DEMO" "$PY" "$TARGETS" \
            "$BASELINE/settings.json" "$BASELINE/ui.json"; do
  [ -e "$path" ] || { echo "FATAL: missing input: $path"; exit 2; }
done
[ -x "$PRODUCT" ] || { echo "FATAL: not executable: $PRODUCT"; exit 2; }
mkdir -p "$OUT"

# HYGIENE (lead m01207): the product rewrites the settings/UI files it is handed
# on exit, so hand it COPIES inside the output directory, never the tracked
# baseline files. Byte-identical copies, so every reading is unchanged.
mkdir -p "$OUT/inputs"
cp "$BASELINE/settings.json" "$OUT/inputs/third-person-settings.ini" 2>/dev/null || true
cp "$BASELINE/ui.json" "$OUT/inputs/third-person-ui.ini" 2>/dev/null || true
TP_SETTINGS="$OUT/inputs/third-person-settings.ini"
TP_UI="$OUT/inputs/third-person-ui.ini"
PRODUCT_SHA=$(sha_of "$PRODUCT")

# --dry-run: prove the plan and the inputs without launching anything (no
# capture, no host lock needed). Same target file, same camera arithmetic as
# the real run, so a wrong target line or a missing input shows up here.
if [ "${1:-}" = "--dry-run" ]; then
  echo "=== T83 third-person weapon attach: DRY RUN (nothing is launched) ==="
  echo "  product=$PRODUCT"
  echo "  product sha256=$PRODUCT_SHA"
  echo "  baseline=$BASELINE targets=$TARGETS out=$OUT"
  echo "  settings=$BASELINE/settings.json ui=$BASELINE/ui.json"
  echo "  demo=$DEMO"
  echo "  tf=$TF speed=$SPEED"
  echo "  camera behind: eye=origin+($CAMERA_OFFSET_X,0,$CAMERA_OFFSET_Z) pitch=$CAMERA_PITCH yaw=0"
  echo "  camera side:   eye=origin-$SIDE_DISTANCE*(cos,sin) at ${SIDE_YAW} degrees pitch=$CAMERA_PITCH"
  planned=0
  while read -r CLASS ENT TICK ORIGIN_X ORIGIN_Y ORIGIN_Z; do
    case "$CLASS" in ''|'#'*) continue;; esac
    planned=$((planned + 1))
    eyeX=$(awk -v x="$ORIGIN_X" -v d="$CAMERA_OFFSET_X" 'BEGIN { printf "%.1f", x + d }')
    eyeY="$ORIGIN_Y"
    eyeZ=$(awk -v z="$ORIGIN_Z" -v d="$CAMERA_OFFSET_Z" 'BEGIN { printf "%.1f", z + d }')
    sideX=$(awk -v x="$ORIGIN_X" -v d="$SIDE_DISTANCE" -v a="$SIDE_YAW" 'BEGIN { printf "%.1f", x - d * cos(a * 0.01745329252) }')
    sideY=$(awk -v y="$ORIGIN_Y" -v d="$SIDE_DISTANCE" -v a="$SIDE_YAW" 'BEGIN { printf "%.1f", y - d * sin(a * 0.01745329252) }')
    echo "--- $CLASS: ent=$ENT tick=$TICK origin=$ORIGIN_X,$ORIGIN_Y,$ORIGIN_Z"
    echo "    off             eye=$eyeX,$eyeY,$eyeZ pitch=$CAMERA_PITCH yaw=0 flags=--player-animation"
    echo "    attached        eye=$eyeX,$eyeY,$eyeZ pitch=$CAMERA_PITCH yaw=0 flags=--player-animation --player-weapon-attach on"
    echo "    attached-2      eye=$eyeX,$eyeY,$eyeZ pitch=$CAMERA_PITCH yaw=0 flags=--player-animation --player-weapon-attach on"
    echo "    identity        eye=$eyeX,$eyeY,$eyeZ pitch=$CAMERA_PITCH yaw=0 flags=--player-animation --world-entity-id"
    echo "    identity-attached eye=$sideX,$sideY,$eyeZ pitch=$CAMERA_PITCH yaw=$SIDE_YAW flags=--player-animation --world-entity-id --player-weapon-attach on"
  done < "$TARGETS"
  if [ "$planned" -eq 0 ]; then
    echo "FATAL: no target rows in $TARGETS"
    exit 2
  fi
  echo "DRY-RUN cases=$planned frames=$((planned * 5)) inputs=ok"
  exit 0
fi

# run_frame <dir> <tag> <tick> <eyeX> <eyeY> <eyeZ> <pitch> <yaw> <flag...>
# Same request shape as class-pixels-check.sh's helper so the off frame is that
# gate's animated frame down to the settings file.
run_frame() {
  local dir="$1" tag="$2" tick="$3" eyeX="$4" eyeY="$5" eyeZ="$6" pitch="$7" yaw="$8"; shift 8
  local bmp="$dir/$tag.bmp" trace="$dir/$tag.trace"
  rm -f "$bmp" "$trace"
  TF2_NATIVE_TRACE="$trace" timeout 1200 "$PRODUCT" \
    --tf-root "$TF" --hidden-window --audio-disabled \
    --settings-file "$TP_SETTINGS" --ui-settings-file "$TP_UI" \
    --demo "$DEMO" --capture-tick "$tick" --playback-speed "$SPEED" \
    --capture-frame "$bmp" \
    --debug-camera "$eyeX" "$eyeY" "$eyeZ" "$pitch" "$yaw" \
    "$@" > "$dir/$tag.log" 2>&1
  local rc=$?
  local frameTick sceneTick begin_l armed_l
  frameTick=$(grep -a -o 'capture: armed requested=[0-9]* frame_tick=[0-9]*' "$trace" 2>/dev/null | tail -1 | sed 's/.*frame_tick=//')
  # THE CAPTURE FRAME IS A WINDOW, NOT THE LAST LINE.
  #   The replay prints one 'scene: begin ...' block per rendered frame and then a
  #   final 'capture: armed ...' line for the frame it writes out; everything
  #   after 'capture: armed' is teardown (no scene lines at all). Per-frame
  #   counters therefore have to be read from inside that last window. Reading
  #   'grep ... | tail -1' of a per-frame counter picks whatever the LAST frame of
  #   the REPLAY said, which on the upward recording is 10320 ticks after the
  #   capture and quotes a different moment of the match. This gate used to do
  #   exactly that for 'scene: anim' -- and 'scene: anim' does not even carry the
  #   scene tick: its tick= field is the animation clock, which advances only 28
  #   times over a 587-frame run. Cut the window out once, read everything from it.
  begin_l=$(grep -an ' scene: begin ' "$trace" 2>/dev/null | tail -1 | cut -d: -f1)
  armed_l=$(grep -an ' capture: armed ' "$trace" 2>/dev/null | tail -1 | cut -d: -f1)
  if [ -n "${begin_l:-}" ] && [ -n "${armed_l:-}" ] && [ "$armed_l" -ge "$begin_l" ]; then
    awk -v a="$begin_l" -v b="$armed_l" 'NR >= a && NR <= b' "$trace" > "$dir/$tag.capture.txt"
  else
    cp "$trace" "$dir/$tag.capture.txt"
    echo "  NOTE $tag: no scene-begin/capture-armed window in the trace; using the whole trace"
  fi
  sceneTick=$(grep -a 'scene: begin ' "$dir/$tag.capture.txt" 2>/dev/null | tail -1 | grep -o 'sceneTick=[0-9]*' | sed 's/sceneTick=//')
  # The attach ledger and the identity ledger are kept as small files: the trace
  # itself is hundreds of MB on a long replay, but these lines are the evidence
  # that the hold point came from a named bone and that the weapon instance was
  # given an id of its own. They are cut from the capture window, so one summary
  # means one frame and not one whole replay (the un-windowed form grew to
  # hundreds of megabytes of placement lines that all described other frames).
  grep -a -E 'weaponAttach:|scene: weaponAttach ' "$dir/$tag.capture.txt" > "$dir/$tag.weapon.txt" 2>/dev/null
  grep -a 'entity: id=' "$dir/$tag.capture.txt" > "$dir/$tag.entity.txt" 2>/dev/null
  {
    echo "tag=$tag rc=$rc frameTick=$frameTick sceneTick=$sceneTick"
    grep -a 'scene: begin ' "$dir/$tag.capture.txt" 2>/dev/null | tail -1
    grep -a 'scene: corpseFilter ' "$dir/$tag.capture.txt" 2>/dev/null | tail -1
    grep -a 'scene: setInstances ' "$dir/$tag.capture.txt" 2>/dev/null | tail -1
    grep -a 'scene: weaponAttach ' "$dir/$tag.capture.txt" 2>/dev/null | tail -1
    grep -a 'attach: meshSource ' "$dir/$tag.capture.txt" 2>/dev/null | tail -1
    # The animation phase of THIS frame (claim 2 above). One line, so a reader of
    # the summary can tell a cross-phase pair from a real regression without the
    # trace: it is the only quantity that separated the two branches.
    grep -a 'animsum:' "$dir/$tag.capture.txt" 2>/dev/null | tail -1
    grep -a 'weaponAttach: ' "$dir/$tag.capture.txt" 2>/dev/null
    cat "$dir/$tag.entity.txt" 2>/dev/null
    echo "frame: bmp=$tag.bmp sha256=$(sha_of "$bmp")"
    echo "frame: product sha256=$PRODUCT_SHA"
    echo "frame: request demo=$DEMO tick=$tick speed=$SPEED camera=$eyeX,$eyeY,$eyeZ,$pitch,$yaw flags=$*"
  } > "$dir/$tag.summary"
  # Diagnostics must go to stderr: stdout of run_frame is the only channel that
  # carries "rc frameTick sceneTick" back to the caller, which reads it with
  #   read -r rc ft st < <(run_frame ...)
  # An extra echo on stdout turned $rc into the string "window" and made every
  # rc / frame-tick / scene-tick assertion fail for a reason that has nothing to
  # do with the product.
  echo "window $tag: lines $begin_l..$armed_l of $trace ($(wc -l < "$dir/$tag.capture.txt" 2>/dev/null || echo 0))" >&2
  rm -f "$trace"
  printf '%s %s %s\n' "$rc" "$frameTick" "$sceneTick"
}

echo "=== T83 third-person weapon attach: per class, off is the baseline ==="
echo "  demo=$DEMO"
echo "  output=$OUT baseline=$BASELINE targets=$TARGETS speed=$SPEED"
echo "  product=$PRODUCT sha256=$PRODUCT_SHA"
echo

cases=0
attach_det_ok=0        # rows whose two attached captures agreed byte for byte INSIDE one phase
attach_ok=0            # rows that held a weapon AND painted it at the hand
refused_ok=0           # rows where the recording gives the class no weapon at all
baseline_ok=0
phase_ok=0             # rows whose every capture sat in the baseline frame's phase
pixels_ok=0
while read -r CLASS ENT TICK ORIGIN_X ORIGIN_Y ORIGIN_Z; do
  case "$CLASS" in ''|'#'*) continue;; esac
  dir="$OUT/$CLASS"
  mkdir -p "$dir"
  eyeX=$(awk -v x="$ORIGIN_X" -v d="$CAMERA_OFFSET_X" 'BEGIN { printf "%.1f", x + d }')
  eyeY="$ORIGIN_Y"
  eyeZ=$(awk -v z="$ORIGIN_Z" -v d="$CAMERA_OFFSET_Z" 'BEGIN { printf "%.1f", z + d }')
  sideX=$(awk -v x="$ORIGIN_X" -v d="$SIDE_DISTANCE" -v a="$SIDE_YAW" 'BEGIN { printf "%.1f", x - d * cos(a * 0.01745329252) }')
  sideY=$(awk -v y="$ORIGIN_Y" -v d="$SIDE_DISTANCE" -v a="$SIDE_YAW" 'BEGIN { printf "%.1f", y - d * sin(a * 0.01745329252) }')
  sideZ="$eyeZ"
  cases=$((cases + 1))
  echo "--- $CLASS: ent=$ENT tick=$TICK origin=$ORIGIN_X,$ORIGIN_Y,$ORIGIN_Z"
  echo "    behind eye=$eyeX,$eyeY,$eyeZ yaw=0 / side eye=$sideX,$sideY,$sideZ yaw=$SIDE_YAW"

  # The phase this row has to live in: the baseline frame's phase when the
  # baseline says (its .summary carries the same animsum line), otherwise no
  # requirement. Claiming "off is the baseline" is only possible inside one
  # phase, so the retry is what keeps that claim honest instead of comparing a
  # phase-A frame to a phase-B baseline and calling the difference a regression.
  off_want=$(phase_of_summary "$BASELINE/$CLASS/animated.summary")
  id_want=$(phase_of_summary "$BASELINE/$CLASS/identity.summary")
  read -r off_rc off_ft off_st off_ph off_try < <(run_frame_until_phase "$dir" off "$(want_phase "$off_want")" "$TICK" "$eyeX" "$eyeY" "$eyeZ" "$CAMERA_PITCH" 0.0 --player-animation)
  read -r on_rc on_ft on_st on_ph on_try < <(run_frame_until_phase "$dir" attached "$(want_phase "$off_ph")" "$TICK" "$eyeX" "$eyeY" "$eyeZ" "$CAMERA_PITCH" 0.0 --player-animation --player-weapon-attach on)
  read -r on2_rc on2_ft on2_st on2_ph on2_try < <(run_frame_until_phase "$dir" attached-2 "$(want_phase "$off_ph")" "$TICK" "$eyeX" "$eyeY" "$eyeZ" "$CAMERA_PITCH" 0.0 --player-animation --player-weapon-attach on)
  read -r id_rc id_ft id_st id_ph id_try < <(run_frame_until_phase "$dir" identity "$(want_phase "${id_want:-$off_ph}")" "$TICK" "$eyeX" "$eyeY" "$eyeZ" "$CAMERA_PITCH" 0.0 --player-animation --world-entity-id)
  read -r ia_rc ia_ft ia_st ia_ph ia_try < <(run_frame_until_phase "$dir" identity-attached "$(want_phase "$id_ph")" "$TICK" "$sideX" "$sideY" "$sideZ" "$CAMERA_PITCH" "$SIDE_YAW" --player-animation --world-entity-id --player-weapon-attach on)
  echo "    rc/landed: off=$off_rc/$off_ft attached=$on_rc/$on_ft attached-2=$on2_rc/$on2_ft identity=$id_rc/$id_ft identity-attached=$ia_rc/$ia_ft"
  echo "    phase: off=$off_ph($off_try) attached=$on_ph($on_try) attached-2=$on2_ph($on2_try) identity=$id_ph($id_try) identity-attached=$ia_ph($ia_try)"
  echo "    baseline phase: animated=${off_want:-<none>} identity=${id_want:-<none>}"
  # Every frame this row compares has to sit in one phase. A row that could not
  # get there inside the attempt budget is reported as what it is -- no quotable
  # reading -- rather than being compared anyway.
  if [ -n "${off_ph:-}" ] && [ "$off_ph" != "-" ] && [ "$off_ph" != "<none>" ]; then
    [ "$on_ph" = "$off_ph" ] || say_fail "$CLASS: attached landed on phase ${on_ph:-?} after $on_try attempt(s), off on $off_ph: a cross-phase pair is not a comparison"
    [ "$on2_ph" = "$off_ph" ] || say_fail "$CLASS: attached-2 landed on phase ${on2_ph:-?} after $on2_try attempt(s), off on $off_ph: a cross-phase pair is not a comparison"
    [ "$ia_ph" = "$id_ph" ] || say_fail "$CLASS: identity-attached landed on phase ${ia_ph:-?} after $ia_try attempt(s), identity on ${id_ph:-?}"
  fi
  if [ -n "${off_want:-}" ] && [ "$off_ph" != "$off_want" ]; then
    say_fail "$CLASS: off landed on phase ${off_ph:-?} after $off_try attempt(s), the baseline frame is $off_want: the baseline comparison is not available in this run"
  fi
  if [ -n "${id_want:-}" ] && [ "$id_ph" != "$id_want" ]; then
    say_fail "$CLASS: identity landed on phase ${id_ph:-?} after $id_try attempt(s), the baseline identity is $id_want: the baseline comparison is not available in this run"
  fi
  # A row counts as phase-pinned when off sits in the animated baseline's phase
  # and identity sits in the identity baseline's phase. Those two are the only
  # baselines this gate owns; everything else is compared inside the row.
  if { [ -z "${off_want:-}" ] || [ "$off_ph" = "$off_want" ]; } \
     && { [ -z "${id_want:-}" ] || [ "$id_ph" = "$id_want" ]; }; then
    echo "  OK   every capture sits in the phase its baseline was taken in (${off_ph:-none})"
    phase_ok=$((phase_ok + 1))
  fi

  for pair in "off:$off_rc" "attached:$on_rc" "attached-2:$on2_rc" "identity:$id_rc" "identity-attached:$ia_rc"; do
    [ "$(echo "$pair" | cut -d: -f2)" = "0" ] || say_fail "$CLASS: $(echo "$pair" | cut -d: -f1) capture exited $(echo "$pair" | cut -d: -f2)"
  done
  for pair in "off:$off_ft" "attached:$on_ft" "attached-2:$on2_ft" "identity:$id_ft" "identity-attached:$ia_ft"; do
    [ "$(echo "$pair" | cut -d: -f2)" = "$TICK" ] || say_fail "$CLASS: $(echo "$pair" | cut -d: -f1) frame landed tick $(echo "$pair" | cut -d: -f2), not $TICK"
  done
  [ "$off_st" = "$on_st" ] && [ "$off_st" = "$id_st" ] && [ "$off_st" = "$ia_st" ] \
    || say_fail "$CLASS: the passes resolved different scene ticks (off=$off_st on=$on_st id=$id_st idon=$ia_st)"

  OFF_SHA=$(sha_of "$dir/off.bmp")
  ON_SHA=$(sha_of "$dir/attached.bmp")
  ON2_SHA=$(sha_of "$dir/attached-2.bmp")
  ID_SHA=$(sha_of "$dir/identity.bmp")
  IA_SHA=$(sha_of "$dir/identity-attached.bmp")
  BASE_OFF_SHA=$(sha_of "$BASELINE/$CLASS/animated.bmp")
  BASE_ID_SHA=$(sha_of "$BASELINE/$CLASS/identity.bmp")
  echo "    sha off=$OFF_SHA"
  echo "    sha attached=$ON_SHA attached-2=$ON2_SHA"
  echo "    sha identity=$ID_SHA baseline(identity)=$BASE_ID_SHA"
  echo "    sha identity-attached=$IA_SHA"

  # 1. off, and the identity pass with the flag absent, are the baseline.
  if [ "$BASE_OFF_SHA" = "<none>" ]; then
    say_fail "$CLASS: no baseline animated.bmp at $BASELINE/$CLASS/animated.bmp, so 'off is unchanged' cannot be asserted"
  elif [ "$OFF_SHA" = "$BASE_OFF_SHA" ]; then
    echo "  OK   off frame is byte-identical to the class-pixels baseline"
    baseline_ok=$((baseline_ok + 1))
  else
    say_fail "$CLASS: off frame ($OFF_SHA) is NOT the class-pixels baseline ($BASE_OFF_SHA): the default path moved"
  fi
  if [ "$BASE_ID_SHA" = "<none>" ]; then
    say_fail "$CLASS: no baseline identity.bmp, so 'the entity-id path is unchanged' cannot be asserted"
  elif [ "$ID_SHA" = "$BASE_ID_SHA" ]; then
    echo "  OK   identity frame is byte-identical to the class-pixels identity"
  else
    say_fail "$CLASS: identity frame ($ID_SHA) is NOT the class-pixels identity ($BASE_ID_SHA): the entity-id path moved"
  fi

  # 0. what the attach ledger says this frame actually did. Parsed BEFORE the
  # on/off comparison, because "did this frame hold a weapon at all" decides
  # which assertion is the honest one: a recording in which the class carries no
  # active weapon has no weapon to attach, and demanding pixels from it would
  # only reward drawing something that should not be there.
  held=$(grep -a 'scene: weaponAttach ' "$dir/attached.summary" | tail -1 | grep -o 'held=[0-9]*' | tail -1 | sed 's/held=//')
  drawn=$(grep -a 'scene: weaponAttach ' "$dir/attached.summary" | tail -1 | grep -o 'drawn=[0-9]*' | tail -1 | sed 's/drawn=//')
  skipped=$(grep -a 'scene: weaponAttach ' "$dir/attached.summary" | tail -1 | grep -o 'skippedStandalone=[0-9]*' | tail -1 | sed 's/skippedStandalone=//')
  refused=$(grep -a 'scene: weaponAttach ' "$dir/attached.summary" | tail -1 | grep -o 'refused=[0-9]*' | tail -1 | sed 's/refused=//')
  first_refusal=$(grep -a 'scene: weaponAttach ' "$dir/attached.summary" | tail -1 | grep -o 'firstRefusal=[^ ]*' | tail -1 | sed 's/firstRefusal=//')
  echo "  attach ledger: held=${held:-?} drawn=${drawn:-?} refused=${refused:-?} skippedStandalone=${skipped:-?} firstRefusal=${first_refusal:-none}"

  # 2. determinism, asked of every row: one request, two processes, one answer --
  #    asked INSIDE one animation phase, which is the only place the question is
  #    well posed. Both captures above were retried until they landed on the same
  #    phase; the cross-phase pair is not a determinism finding but a phase
  #    difference, and it is reported above by the phase assertions.
  if [ "$ON_SHA" != "<none>" ] && [ "$ON_SHA" = "$ON2_SHA" ]; then
    echo "  OK   attached twice is byte-identical ($ON_SHA)"
    attach_det_ok=$((attach_det_ok + 1))
  elif [ -n "${on_ph:-}" ] && [ "$on_ph" != "$on2_ph" ]; then
    say_fail "$CLASS: the two attached captures landed on different animation phases (on=$on_ph/$ON_SHA on2=$on2_ph/$ON2_SHA) and the retry budget ran out: no quotable reading"
  else
    say_fail "$CLASS: the same attached capture twice is not byte-identical inside one phase (on=$on_ph/$ON_SHA on2=$on2_ph/$ON2_SHA): the rendering is not deterministic"
  fi

  if [ "${held:-0}" -ge 1 ] 2>/dev/null; then
    # POSITIVE ROW: this class had a weapon this frame. The flag must change the
    # picture, the trace must name the bone it hung the weapon on, and the
    # painted instance must be a weapon-sized blob at the hand.
    if [ "$ON_SHA" = "$OFF_SHA" ]; then
      say_fail "$CLASS: attached and off frames are byte-identical: the flag changed nothing"
    else
      echo "  OK   attached differs from off"
    fi
    if grep -qa 'weaponAttach' "$dir/off.summary"; then
      say_fail "$CLASS: the OFF run printed weapon-attach lines: the feature is not inert when the flag is absent"
    else
      echo "  OK   the off run prints no weapon-attach line"
    fi
    W_ATTACH=$(grep -a 'weaponAttach: class=' "$dir/attached.summary" | tail -3)
    if [ -n "$W_ATTACH" ]; then
      echo "  OK   attach placement lines:"
      printf '%s\n' "$W_ATTACH" | sed 's/^/      /'
    else
      say_fail "$CLASS: the attached run printed no weaponAttach: class= line: nothing was placed on a hold point"
    fi
    # Dedupe, measured in the picture rather than in the counter: the weapon
    # entity's id may appear exactly once and that one appearance must be the
    # attached instance. A second line saying ent=<weapon> would be the same gun
    # drawn twice in one frame.
    # The attach line must belong to THIS body: several players in one frame each
    # push their own attached instance, and "the last attach line" is not ours.
    weapon_ent=$(grep -a 'attach=1 heldEnt=' "$dir/identity-attached.summary" | grep -a "heldEnt=${ENT} " | tail -1 | grep -o ' ent=[0-9-]*' | head -1 | sed 's/^ ent=//')
    if [ -n "${weapon_ent:-}" ]; then
      dup=$(grep -a 'entity: id=' "$dir/identity-attached.summary" | grep -v 'attach=1' | grep -c "ent=${weapon_ent} ")
      if [ "${dup:-0}" = "0" ]; then
        echo "  OK   weapon entity ent=$weapon_ent is drawn only as the attached instance (no standalone twin)"
      else
        say_fail "$CLASS: weapon entity ent=$weapon_ent is drawn $dup time(s) standalone as well as attached: the same gun is on screen twice"
      fi
    else
      say_fail "$CLASS: no 'attach=1 heldEnt=' ledger line, so the attached instance cannot be identified"
    fi
    if [ "${held:-0}" -ge 1 ] 2>/dev/null && [ "${drawn:-0}" -ge 1 ] 2>/dev/null; then
      echo "  OK   this frame holds $held weapon(s), drew $drawn, and left ${skipped:-0} standalone draw(s) out"
    else
      say_fail "$CLASS: held=${held:-0} drawn=${drawn:-0}; the frame carries no attached weapon"
    fi

    # The weapon pixels are at the body (identity capture, side view).
    "$PY" t83-weapon-pixels.py "$dir/identity-attached.bmp" "$dir/identity-attached.summary" "$ENT" \
      > "$dir/weapon-pixels.txt" 2>&1
    px_rc=$?
    sed 's/^/    /' "$dir/weapon-pixels.txt"
    echo "  weapon-pixels rc=$px_rc"
    if [ "$px_rc" = "0" ]; then
      pixels_ok=$((pixels_ok + 1))
    else
      say_fail "$CLASS: the attach instance's pixels are not a weapon at this body (see above)"
    fi
    attach_ok=$((attach_ok + 1))
  elif [ "${refused:-0}" -ge 1 ] 2>/dev/null || [ -n "${first_refusal:-}" ]; then
    # REFUSAL ROW: the recording gives this class no active weapon at its tick
    # (an empty m_hActiveWeapon handle, a weapon entity that is not in the
    # snapshot, a world-model index of 0). The correct behaviour is exactly the
    # one the product's fallback promises: nothing drawn, a traced reason, and
    # the default path untouched. Asserting a pixel blob here would be asserting
    # a weapon that TF2 does not have in this frame.
    echo "  OK   refusal row: no weapon to attach (firstRefusal=${first_refusal:-none}); asserting the fallback"
    if [ "$ON_SHA" = "$OFF_SHA" ]; then
      echo "  OK   attached equals off, as a frame with no weapon must"
    else
      say_fail "$CLASS: the frame carries no weapon (held=${held:-0}, refused=${refused:-0}) yet attached differs from off"
    fi
    if grep -qa 'weaponAttach: class=' "$dir/attached.summary"; then
      say_fail "$CLASS: the attached run placed a hold point while reporting no weapon (held=${held:-0})"
    else
      echo "  OK   no placement line for a frame with no weapon"
    fi
    if grep -qa 'scene: weaponAttach ' "$dir/attached.summary"; then
      echo "  OK   the refusal is in the trace, not silent"
    else
      say_fail "$CLASS: no 'scene: weaponAttach' ledger line at all: the refusal is invisible"
    fi
    px_rc="refused"
    refused_ok=$((refused_ok + 1))
  else
    say_fail "$CLASS: neither a held weapon nor a traced refusal: the flag did nothing and said nothing"
    px_rc="none"
  fi

  # N1: the identity capture of the OFF run has no attach instance at all.
  "$PY" t83-weapon-pixels.py "$dir/identity.bmp" "$dir/identity.summary" "$ENT" \
    > "$dir/weapon-pixels-n1.txt" 2>&1
  n1_rc=$?
  echo "  N1 off identity capture into the checker rc=$n1_rc (must be non-zero)"
  if [ "$n1_rc" = "0" ]; then
    say_fail "$CLASS: N1 the pixel checker PASSED on a frame with no attached instance"
  fi

  # N2: point the attach ledger at the BODY id -- the mask becomes a body-sized
  # blob and the shape thresholds must reject it.
  n2_body=$(awk -v ent="$ENT" '
    /entity: id=/ && $0 !~ /attach=1/ {
      isbody = 0
      for (i = 1; i <= NF; i++) if ($i == "ent=" ent) isbody = 1
      if (isbody) for (j = 1; j <= NF; j++) if ($j ~ /^id=[0-9]+$/) body = substr($j, 4)
    }
    END { print body }' "$dir/identity-attached.summary")
  if [ -n "$n2_body" ]; then
    sed -E "s/entity: id=[0-9]+/entity: id=$n2_body/" "$dir/identity-attached.summary" > "$dir/n2-decoy.summary"
    "$PY" t83-weapon-pixels.py "$dir/identity-attached.bmp" "$dir/n2-decoy.summary" "$ENT" \
      > "$dir/weapon-pixels-n2.txt" 2>&1
    n2_rc=$?
    echo "  N2 attach id rewritten to the body id ($n2_body) rc=$n2_rc (must be non-zero)"
    if [ "$n2_rc" = "0" ]; then
      say_fail "$CLASS: N2 the pixel checker PASSED when the attach id was the body id"
    fi
  else
    n2_rc="skipped"
    say_fail "$CLASS: N2 could not find the body id in the identity ledger"
  fi

  {
    echo "frame: class=$CLASS ent=$ENT tick=$TICK"
    echo "frame: bmp off sha256=$OFF_SHA baseline=$BASE_OFF_SHA"
    echo "frame: bmp attached sha256=$ON_SHA attached-2 sha256=$ON2_SHA"
    echo "frame: bmp identity sha256=$ID_SHA baseline=$BASE_ID_SHA"
    echo "frame: bmp identity-attached sha256=$IA_SHA"
    echo "frame: attach held=$held drawn=$drawn skipped=$skipped pixels_rc=$px_rc n1_rc=$n1_rc n2_rc=$n2_rc"
    echo "frame: product sha256=$PRODUCT_SHA"
  } >> "$dir/weapon-pixels.txt"
  echo
done < "$TARGETS"

# N3, once: the flag rejects a value it cannot honour.
#
# The refusal is an exit code plus the absence of an artefact, not a line of
# stdout: every refused argument in native/src/main.cpp announces itself on the
# debugger channel (OutputDebugStringW, see the --outline, --model-*,
# --viewmodel-*, --logstf-* and this flag's own rejection block) and this
# program writes nothing to stdout or stderr at all. An earlier form of this
# control grepped the log for 'invalid --player-weapon-attach' and therefore
# could never pass -- it was asserting a channel the product has never used
# (first full run, 2026-11-05 19:49, log 0 bytes, rc 13). What is observable,
# and what actually protects the A/B pair, is that the refusal happens before
# anything is drawn: exit 13 and no frame written.
echo "--- N3 invalid --player-weapon-attach value"
N3_LOG="$OUT/negative-flag.log"
N3_BMP="$OUT/n3-refused.bmp"
rm -f "$N3_BMP"
"$PRODUCT" --tf-root "$TF" --hidden-window --audio-disabled \
  --settings-file "$TP_SETTINGS" --ui-settings-file "$TP_UI" \
  --demo "$DEMO" --capture-tick 0 --playback-speed "$SPEED" \
  --capture-frame "$N3_BMP" \
  --player-weapon-attach bogus > "$N3_LOG" 2>&1
n3_rc=$?
if [ "$n3_rc" = "13" ]; then
  echo "  OK   N3 --player-weapon-attach bogus exits 13 (the documented rejection code)"
else
  say_fail "N3 --player-weapon-attach bogus exited $n3_rc (want 13): a typo may be silently accepted"
fi
if [ ! -e "$N3_BMP" ]; then
  echo "  OK   the refused run wrote no frame: the typo never reached the render loop"
else
  say_fail "N3 the refused run still wrote $N3_BMP: the bad value was honoured somewhere"
fi

echo "==========================================================================="
# phase-pinned is the new counter and the reason the older one can be trusted:
# it counts rows whose five captures all landed on the phase the baseline frame
# was taken in, i.e. rows where "off is the baseline" and "on twice is the same
# frame" are statements about the product rather than about the replay clock.
echo "cases=$cases phase-pinned=$phase_ok attached-deterministic=$attach_det_ok off-is-baseline=$baseline_ok attach-row=$attach_ok refused-row=$refused_ok weapon-pixels=$pixels_ok"
if [ "$fail" = "0" ] && [ "$cases" -gt 0 ]; then
  echo "THIRD-PERSON-WEAPON=PASS"
else
  [ "$cases" -gt 0 ] || echo "  FAIL no target rows were read from $TARGETS"
  echo "THIRD-PERSON-WEAPON=FAIL (see the FAIL lines above)"
fi
[ "$fail" = "0" ] && [ "$cases" -gt 0 ]
