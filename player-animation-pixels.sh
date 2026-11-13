#!/usr/bin/env bash
# player-animation-pixels.sh -- T61 section F: the animation drive reaches the
# PIXELS the user sees.
#
# WHY THIS GATE EXISTS, SEPARATELY FROM player-animation-check.sh
# --------------------------------------------------------------------
# Sections A-E of player-animation-check.sh assert on counters and trace lines.
# A counter can be green while the picture is a flat black silhouette -- that is
# exactly how the black player body survived T60 with every counter healthy.
# The claim here is the picture itself, so the evidence has to be pixels:
#
#   F1  the product binary is fresh (a stale binary would measure old code)
#   F2  the entity pass's own COVERAGE is measured, not assumed: with
#       --world-entity-coverage the entity pass writes exactly (255,0,255) and
#       no other pass can, so the mask is what the pass drew
#   F3  over that mask the product frame is a shaded body, not a flat fill
#       (distinct colours, mean luminance, pure-black share)
#   F4  the animation drive moves those pixels: with and without
#       --player-animation at the SAME tick, camera and settings, a measured
#       share of the masked pixels must differ. Identical frames mean the drive
#       reaches nothing the user sees, whatever the counters say.
#   F5  the chain model -> pick -> sample -> pixels is bound to ONE entity.
#       --world-entity-id paints each drawn instance a unique red and prints a
#       ledger line per instance, so a colour decodes to a body. The subject
#       body ledger line must agree with the pick for its OWN model in the
#       state it reports, every per-entity sample line must report the
#       sequence its own model pick resolved, and the pixels compared in F4
#       are restricted to that subject mask. Without F5 a single body drawing
#       the wrong clip hides behind another model legal clip in the F2-F4 sets.
#
# The mask is deliberately NOT a hardcoded screen box: a box keeps passing when
# the entity pass stops drawing there.
#
# MUST RUN UNDER THE SHARED HOST LOCK (it builds and runs captures):
#   bash /d/TF2_Native_Worktrees/_shared/hostlock.sh run t61-anim \
#     bash -c 'cd /d/TF2_Native_Worktrees/t61-anim && bash player-animation-pixels.sh'
#
# Usage: bash player-animation-pixels.sh
# Exit:  0 = PASS, 1 = FAIL, 2 = a required input is missing (FATAL).
set -uo pipefail
cd "$(dirname "$0")"

OUT=${T61_PIXEL_OUT:-evidence/t61-animation/pixels}
PRODUCT=${T61_PRODUCT:-native/build-nmake/tf2_demo_native.exe}
PY=${T61_PYTHON:-C:/Users/Administrator/.workbuddy-ai/binaries/python/versions/3.13.12/python.exe}
TF=${T61_TF_ROOT:-D:/SteamLibrary/steamapps/common/Team Fortress 2/tf}
DEMO=${T61_PIXEL_DEMO:-D:/TF2_Demo_Player/testdata/demos/bb841c6d379ff7c40d0c8baf99f59d8d_matcha-20260927-1347-cp_snakewater_final1.dem}
# The camera was chosen for this tick: it frames a player body large enough to
# see pixels on. Changing the tick without re-choosing the camera makes F3/F4
# measure the sky.
TICK=${T61_PIXEL_TICK:-64161}
SPEED=${T61_PIXEL_SPEED:-8}
CAMERA=(--debug-camera 5363.8 -1102.1 5.9 4.1 0.0)

fail=0
# --- isolated settings (D-22) ----------------------------------------------
# The product persists its settings and UI layout under %LOCALAPPDATA%/TF
# Demo Player. A gate that reads or writes there shares state with the desktop
# player and with every other gate on the host, so a previous run (or a human)
# can change what this gate measures. Each run gets a throwaway directory
# instead, and it is removed on exit.
ISO_DIR=$(mktemp -d "${TMPDIR:-/tmp}/tf2-iso.XXXXXX") || {
  echo "FATAL: cannot create isolated settings directory" >&2; exit 2; }
ISO_SETTINGS="$ISO_DIR/settings.ini"
ISO_UI="$ISO_DIR/ui.ini"
# Chain any EXIT trap already installed instead of replacing it: an overwritten
# trap would skip a mutation restore and leave a mutated build behind, which is
# the "failure looks like success" shape this suite forbids.
. "$(dirname "$0")/verify-iso.sh"
iso_chain_exit "$ISO_DIR"

say_fail() { echo "  FAIL $*"; fail=1; }

for path in "$PRODUCT" "$TF" "$DEMO" "$PY"; do
  [ -e "$path" ] || { echo "FATAL: missing input: $path"; exit 2; }
done
[ -x "$PRODUCT" ] || { echo "FATAL: not executable: $PRODUCT"; exit 2; }

mkdir -p "$OUT"

# ===========================================================================
echo "=== F1: the binary under test is newer than the sources it compiles ==="
stale=""
for src in native/src/native_renderer.cpp native/src/main.cpp native/src/entity_model.cpp \
           native/src/animation_clock.cpp native/src/animation_binding.cpp; do
  [ -e "$src" ] || continue
  if [ "$PRODUCT" -ot "$src" ]; then stale="$src"; fi
done
if [ -z "$stale" ]; then
  echo "  OK   binary is newer than every source that feeds the pixel path"
else
  say_fail "STALE-BINARY=1 $PRODUCT is older than $stale; a stale binary would measure old code"
fi

# ===========================================================================
echo
echo "=== F2/F3/F4: capture the coverage mask, the animated frame and the off frame ==="
# run_frame <tag> <flag...>  -- every argument except the tag is identical
# between the animated and the off run; the flag list is the ONLY difference.
run_frame() {
  local tag="$1"; shift
  local bmp="$OUT/$tag.bmp" trace="$OUT/$tag.trace"
  rm -f "$bmp" "$trace"
  TF2_NATIVE_TRACE="$trace" timeout 1200 "$PRODUCT" \
    --tf-root "$TF" --hidden-window --audio-disabled \
    --settings-file "$ISO_SETTINGS" --ui-settings-file "$ISO_UI" \
    --demo "$DEMO" --capture-tick "$TICK" --playback-speed "$SPEED" \
    --capture-frame "$bmp" "${CAMERA[@]}" "$@" > "$OUT/$tag.log" 2>&1
  local rc=$?
  # The A-B identity has to come from lines that exist in BOTH runs. The animsum
  # line does not: it is published only when --player-animation is on, so using it
  # to prove "the same moment" made the check pass on the on run and read <none>
  # on the off run -- a reading wired to the wrong mechanism. These two lines are
  # emitted by the capture path regardless of the animation flag:
  #   capture: armed requested=64161 frame_tick=64161 frames=...
  #   scene: anim ... tick=74481 ...
  local view frameTick sceneTick
  view=$(grep -a -o 'gpu: world debug view=[a-z-]*' "$trace" 2>/dev/null | tail -1 | sed 's/.*view=//')
  frameTick=$(grep -a -o 'capture: armed requested=[0-9]* frame_tick=[0-9]*' "$trace" 2>/dev/null | tail -1 | sed 's/.*frame_tick=//')
  sceneTick=$(grep -a 'scene: anim ' "$trace" 2>/dev/null | tail -1 | grep -o 'tick=[0-9]*' | tail -1 | sed 's/tick=//')
  # Keep the small facts, drop the 50-70 MB trace: the trace is evidence of a
  # run, not a deliverable, and one per case would fill the disk.
  {
    echo "tag=$tag rc=$rc view=$view frameTick=$frameTick sceneTick=$sceneTick"
    grep -a 'animsum:' "$trace" 2>/dev/null | tail -1
    # T61.1: one line per model, printed when the pick is resolved. The capped
    # "anim: ent=" lines cannot carry this -- they stop at eight bodies.
    grep -a 'anim: pick ' "$trace" 2>/dev/null
    # T61.1 step (b): the per-entity witness lines and, when the id view is
    # on, one ledger line per drawn instance. Both are what binds a label to
    # a body instead of to the whole scene.
    #
    # The witness line is emitted per body per scene update -- 55,938 lines in
    # one measured capture, of which only 31 are distinct once the timestamp
    # and the per-frame noise fields are dropped. Keeping the raw stream would
    # turn a 700 KB summary into a 6 MB one and make the join unreadable, so
    # the noise fields go and the rest is deduplicated. The fields kept are the
    # ones the join reads: identity, model, drive state, sequence, label.
    grep -a 'anim: ent=' "$trace" 2>/dev/null | sed -E 's/^[0-9.]+ //' | sed -E 's/ (speed|rate|cycle|delta|advanced|origin)=[^ ]*//g' | sort -u
    grep -a 'entity: id=' "$trace" 2>/dev/null
    grep -a 'gpu: world debug view=' "$trace" 2>/dev/null | tail -1
    grep -a 'capture: armed' "$trace" 2>/dev/null | tail -1
    grep -a 'scene: anim ' "$trace" 2>/dev/null | tail -1
  } > "$OUT/$tag.summary"
  rm -f "$trace"
  printf '%s %s %s %s\n' "${rc:-1}" "${view:-<none>}" "${frameTick:-<none>}" "${sceneTick:-<none>}"
}

read -r cov_rc cov_view cov_ft cov_st < <(run_frame coverage --world-entity-coverage)
echo "  coverage rc=$cov_rc view=$cov_view frameTick=$cov_ft sceneTick=$cov_st"
read -r on_rc on_view on_ft on_st < <(run_frame animated --player-animation)
echo "  animated rc=$on_rc view=$on_view frameTick=$on_ft sceneTick=$on_st"
read -r off_rc off_view off_ft off_st < <(run_frame off)
echo "  off     rc=$off_rc view=$off_view frameTick=$off_ft sceneTick=$off_st"
# Determinism: the same capture, run twice, must be byte-identical. A frame that
# cannot be reproduced is not a controlled A-B -- the on/off comparison below
# would then be one run's noise against another's.
read -r rep_rc rep_view rep_ft rep_st < <(run_frame animated2 --player-animation)
echo "  repeat  rc=$rep_rc view=$rep_view frameTick=$rep_ft sceneTick=$rep_st"
# The identity frame is the animation frame with each drawn instance painted a
# unique red, so a pixel can be traced back to the entity that owns it. Same
# tick, same camera, same drive flag: the ONLY difference is which pass value
# the entity draw is given, so the geometry is the animated frame geometry.
read -r id_rc id_view id_ft id_st < <(run_frame identity --player-animation --world-entity-id)
echo "  identity rc=$id_rc view=$id_view frameTick=$id_ft sceneTick=$id_st"

if [ "$cov_rc" != "0" ]; then say_fail "coverage capture exited $cov_rc (a missing frame is not a pass)"; fi
if [ "$on_rc" != "0" ];  then say_fail "animated capture exited $on_rc (a missing frame is not a pass)"; fi
if [ "$off_rc" != "0" ]; then say_fail "off capture exited $off_rc (a missing frame is not a pass)"; fi
if [ "$rep_rc" != "0" ]; then say_fail "repeat capture exited $rep_rc (a missing frame is not a pass)"; fi
if [ "$id_rc" != "0" ]; then say_fail "identity capture exited $id_rc (a missing frame is not a pass)"; fi
if [ "$id_view" != "entity-id" ]; then say_fail "the identity frame reports view=$id_view, not entity-id: the pixels would not be an id frame"; fi
# The identity frame is compared pixel-for-pixel against the animated frame, so it
# must be the SAME moment. Both readings are mandatory: an absent frame_tick used
# to pass the check below because the guard was `-n "$id_ft" && ...`, and a
# missing reading is not a match -- it is a capture that did not say which frame
# it wrote. scene tick is asserted separately because frame_tick alone does not
# prove the entity state was resolved at the same tick as the animated capture.
if [ "$id_ft" = "$TICK" ]; then
  echo "  OK   the identity frame landed the requested playback tick $TICK"
else
  say_fail "the identity frame landed tick '${id_ft:-<none>}', not $TICK: it is a different moment from the animated frame"
fi
if [ -n "$id_st" ] && [ "$id_st" = "$on_st" ]; then
  echo "  OK   the identity frame resolved the same scene tick as the animated frame ($id_st)"
else
  say_fail "identity scene tick '${id_st:-<none>}' does not match the animated frame '${on_st:-<none>}': the pixels would belong to a different entity state"
fi
if [ -n "$rep_ft" ] && [ "$rep_ft" = "$TICK" ] && [ -n "$on_st" ] && [ "$rep_st" = "$on_st" ]; then
  echo "  OK   the repeat capture landed the same playback and scene tick ($rep_ft/$rep_st)"
else
  say_fail "repeat capture landed tick '${rep_ft:-<none>}'/scene '${rep_st:-<none>}', expected $TICK/$on_st"
fi
if [ -s "$OUT/animated2.bmp" ] && cmp -s "$OUT/animated.bmp" "$OUT/animated2.bmp"; then
  echo "  OK   a repeat capture of the same moment is byte-identical ($(stat -c %s "$OUT/animated2.bmp") bytes)"
else
  say_fail "the repeat capture differs from the first: the frame is not reproducible, so the on/off pair is not a controlled A-B"
fi

[ "$cov_view" = "entity-coverage" ] && \
  echo "  OK   coverage frame really is the entity-coverage view" || \
  say_fail "coverage frame view='${cov_view:-<none>}', expected entity-coverage (the mask would belong to another pass)"
[ "$on_view" = "textured" ] && \
  echo "  OK   animated frame is the product view" || \
  say_fail "animated frame view='${on_view:-<none>}', expected the product view 'textured'"
[ "$off_view" = "textured" ] && \
  echo "  OK   off frame is the product view" || \
  say_fail "off frame view='${off_view:-<none>}', expected the product view 'textured'"

# The A-B is only an A-B if both frames describe the same moment. Two
# independent readings prove it, and both must be present -- an empty reading is a
# failure, not a match, because two missing values compare equal.
if [ -n "$on_ft" ] && [ "$on_ft" = "$off_ft" ] && [ "$on_ft" = "$TICK" ]; then
  echo "  OK   both runs captured the requested playback tick $TICK (frame_tick=$on_ft)"
else
  say_fail "frame_tick differs or is missing: animated='${on_ft:-<none>}' off='${off_ft:-<none>}' requested=$TICK"
fi
if [ -n "$on_st" ] && [ "$on_st" = "$off_st" ]; then
  echo "  OK   both runs resolved the same scene tick $on_st"
else
  say_fail "scene tick differs or is missing: animated='${on_st:-<none>}' off='${off_st:-<none>}': the A-B compares different moments"
fi
# The drive must actually have run in the animated capture: animsum is published
# only by the drive, so its absence means --player-animation reached nothing and
# the on/off pair below would be comparing two undriven frames.
if grep -q 'animsum: tick=' "$OUT/animated.summary" 2>/dev/null; then
  echo "  OK   the animated capture published an animsum line (the drive ran)"
else
  say_fail "the animated capture published no animsum line: the drive did not run, so the on/off pair proves nothing"
fi

# T61.1: the animator was handed an air-walk cycle for every moving player,
# because the needle "walk" was matched with std::string::find and therefore
# matched inside "airwalk". The pick is asserted on the run's own pick line --
# the product names the sequence it resolved, so this reads the decision, not a
# downstream consequence of it.
#
# The needle must not be a bare "airwalk": this script's output directory is
# named after the case and the trace records the settings path, so a case-blind
# substring test over the WHOLE log would find "airwalk" in a path and read as a
# defect that is not there. Labels are matched as whole lines, case-insensitively.
PICK_LINES=$(grep -ac 'anim: pick model=' "$OUT/animated.summary" 2>/dev/null || true)
if [ "${PICK_LINES:-0}" -ge 1 ]; then
  echo "  OK   the animated capture published $PICK_LINES per-model pick line(s)"
else
  say_fail "the animated capture published no 'anim: pick' line: the sequence decision was never recorded, so nothing below can be asserted"
fi

# Every resolved moving sequence must be a real cycle. Labels are read out of
# the pick line's own fields, one per line.
# Split the pick line into WORDS first, then keep only the token that starts with
# the wanted field name. A greedy "s/.*field=//" would run to end-of-line and
# pull the fields that FOLLOW the label into the value (movingLabel= is last, so
# it looked clean while idleLabel= silently carried " movingSeq= movingLabel=").
MOVING_PICKS=$(grep -a 'anim: pick ' "$OUT/animated.summary" 2>/dev/null | tr ' ' '\n' | sed -n 's/^movingLabel=//p' | grep -v '^$' | sort -u || true)
MOVING_COUNT=$(printf '%s\n' "$MOVING_PICKS" | grep -c . || true)
if [ "${MOVING_COUNT:-0}" -ge 1 ] && [ -n "$MOVING_PICKS" ]; then
  echo "  OK   moving picks resolved on $MOVING_COUNT distinct label(s): $(echo $MOVING_PICKS)"
else
  say_fail "no moving sequence was resolved in the animated capture: an empty pick list would make the label assertions vacuous"
fi
# The defect's signature: a label carrying "airwalk" as a token.
if printf '%s\n' "$MOVING_PICKS" | grep -qi 'airwalk'; then
  say_fail "a moving pick is an air-walk clip: $(printf '%s ' $MOVING_PICKS)[T61.1 regression -- the needle matched a substring again]"
else
  echo "  OK   no moving pick is an air-walk clip"
fi
# The fix must land on a locomotion cycle, not merely suppress a word: the
# whole-token rule alone left five classes on an arm-layer clip, so a pick that
# is not a run cycle is a different defect, not a passing result.
# Non-vacuity by construction: "every pick is a run cycle" is true of an EMPTY
# set, and the pipe below would also succeed on one. The universal quantifier is
# therefore nested inside a guard that demands the set be non-empty, so no path
# reaches the OK echo with nothing to check.
if [ "${MOVING_COUNT:-0}" -lt 1 ]; then
  say_fail "no moving pick to judge: an empty label set would satisfy every-is-a-run-cycle vacuously"
else
  NOT_RUN=$(printf '%s\n' "$MOVING_PICKS" | grep -v '^run_' | grep -c . || true)
  if [ "${NOT_RUN:-0}" -eq 0 ]; then
    echo "  OK   every moving pick is a run cycle"
  else
    say_fail "a moving pick is not a run cycle: $(printf '%s ' $(printf '%s\n' "$MOVING_PICKS" | grep -v '^run_'))[the rule must prefer the label whose first token is the needle]"
  fi
fi
# The idle side has the SAME defect shape -- "stand" matches inside
# "AttackStand", so every idle player holds a firing stance -- and it is
# deliberately neither fixed nor judged here. The task scope is the moving
# needle matching "airwalk"; an idle rule was implemented, reviewed as out of
# scope, and withdrawn. So the idle picks are collected to build the frame
# required set and NOT asserted on: an assertion that passed before the
# withdrawal and fails after it would be a scope change smuggled in as a
# test, and one that passed both ways would be measuring nothing.
IDLE_PICKS=$(grep -a 'anim: pick ' "$OUT/animated.summary" 2>/dev/null | tr ' ' '\n' | sed -n 's/^idleLabel=//p' | grep -v '^$' | sort -u || true)
# The frame must agree: the scene line carries the reason the rendered pose used,
# so a product that resolved a good label but played another clip is caught here.
# The rendered pose must be one of the sequences the product itself resolved.
# A test that only forbids the word "airwalk" would pass for any OTHER wrong clip
# -- the pick line could say run_PRIMARY while the frame draws a firing stance --
# so this is set membership, not a blacklist. WHICH set is required comes from
# animsum: it counts every player body by drive state in the same scene update,
# so when only one state is populated the rendered pose must be that state's
# pick. When both are populated the last skinned body could be either, and the
# required set is their union.
SCENE_REASON=$(grep -a 'scene: anim ' "$OUT/animated.summary" 2>/dev/null | tail -1 | sed -n 's/.*reason=//p' | tr -d ' ')
ANIMSUM=$(grep -a 'animsum: ' "$OUT/animated.summary" 2>/dev/null | tail -1)
SUM_MOVING=$(printf '%s' "$ANIMSUM" | tr ' ' '\n' | sed -n 's/^moving=//p')
SUM_IDLE=$(printf '%s' "$ANIMSUM" | tr ' ' '\n' | sed -n 's/^idle=//p')
if [ "${SUM_MOVING:-0}" -gt 0 ] && [ "${SUM_IDLE:-0}" -eq 0 ]; then
  REQUIRED_PICKS="$MOVING_PICKS"
elif [ "${SUM_IDLE:-0}" -gt 0 ] && [ "${SUM_MOVING:-0}" -eq 0 ]; then
  REQUIRED_PICKS="$IDLE_PICKS"
else
  REQUIRED_PICKS=$(printf '%s\n%s\n' "$MOVING_PICKS" "$IDLE_PICKS")
fi
REQUIRED_PICKS=$(printf '%s\n' "$REQUIRED_PICKS" | grep -v '^$' | sort -u || true)
if [ -z "$SCENE_REASON" ]; then
  say_fail "the animated capture carried no scene: anim reason= reading: the rendered sequence is unknown"
elif printf '%s' "$SCENE_REASON" | grep -qi 'airwalk'; then
  say_fail "the rendered scene reports reason=$SCENE_REASON: the frame is an air-walk pose"
elif [ "$(printf '%s\n' "$REQUIRED_PICKS" | grep -c . || true)" -lt 1 ]; then
  say_fail "no pick was resolved for the drive state animsum reports (moving=${SUM_MOVING:-?} idle=${SUM_IDLE:-?}), so the rendered pose cannot be judged against one"
elif printf '%s\n' "$REQUIRED_PICKS" | grep -qxF "$SCENE_REASON"; then
  echo "  OK   the rendered scene reports reason=$SCENE_REASON, one of the picks for the drive state it is in ($(printf '%s ' $REQUIRED_PICKS))"
else
  say_fail "the rendered scene reports reason=$SCENE_REASON, which is none of the picks for the drive state animsum reports moving=${SUM_MOVING:-?} idle=${SUM_IDLE:-?} ($(printf '%s ' $REQUIRED_PICKS)): the pick and the picture disagree"
fi

# ===========================================================================
# T61.1 step (b): bind model, pick, sample and pixels to ONE entity.
#
# The assertions above judge a MODEL: they collect every model pick into one
# set and ask whether the frame reason is a member. A single body that draws
# the wrong clip is invisible to that, because another model legal clip can
# stand in for it. This section asks the same question of one named body,
# using the id frame the run itself produced: a colour in that frame decodes
# to an entity, that entity ledger line names its own pick, its own sample
# lines state the sequence it actually sampled, and the pixels it owns are
# the pixels compared between the animated and the off frame.
echo
echo "=== F5: the per-entity chain ==="
if [ ! -s "$OUT/identity.bmp" ]; then
  say_fail "the identity frame is missing or empty: the per-entity chain cannot be read"
elif "$PY" t61-entityid.py "$OUT/identity.bmp" "$OUT/identity.summary" "$OUT/animated.bmp" "$OUT/off.bmp" > "$OUT/entityid.txt" 2>&1; then
  sed "s/^/  /" "$OUT/entityid.txt"
  echo "  OK   the id frame decodes to a named body whose own pick, own samples and own pixels agree"
else
  sed "s/^/  /" "$OUT/entityid.txt"
  ENTITY_FAILS=$(grep -c "^FAIL " "$OUT/entityid.txt" 2>/dev/null || true)
  if grep -q "^FAIL .*reuse an entity id" "$OUT/entityid.txt"; then
    # T61.1 review 3, P1: two bodies painted one colour is a refusal, not a
    # reason to fall back to the first ledger line. Named separately so a
    # mutant can demand THIS reading instead of the generic chain failure.
    say_fail "two bodies share one entity id: the ledger claims an id twice, so the id colour cannot name one body and the subject mask could be read from the wrong body"
  fi
  say_fail "the per-entity chain failed (see $OUT/entityid.txt): ${ENTITY_FAILS:-0} named reason(s)"
fi
for bmp in coverage animated off identity; do
  if [ -s "$OUT/$bmp.bmp" ]; then
    echo "  OK   $bmp.bmp exists ($(stat -c %s "$OUT/$bmp.bmp") bytes)"
  else
    say_fail "$bmp.bmp is missing or empty"
  fi
done

# ===========================================================================
echo
echo "=== F3/F4: the pixel assertions themselves ==="
PIXEL_LOG="$OUT/pixel-check.txt"
"$PY" t61-pixels.py "$OUT/coverage.bmp" "$OUT/animated.bmp" --off "$OUT/off.bmp" > "$PIXEL_LOG" 2>&1
PIXEL_RC=$?
sed 's/^/  /' "$PIXEL_LOG"
if [ "$PIXEL_RC" != "0" ]; then
  say_fail "t61-pixels.py exited $PIXEL_RC (see $PIXEL_LOG)"
fi
# The tool's own verdict must be present: an empty log would otherwise read as
# "no FAIL lines", which is the vacuous-pass shape this gate exists to avoid.
if grep -q '^PIXEL-CHECK=PASS$' "$PIXEL_LOG"; then
  echo "  OK   t61-pixels.py printed PIXEL-CHECK=PASS"
else
  say_fail "t61-pixels.py did not print PIXEL-CHECK=PASS (a missing verdict is not a pass)"
fi
bad=$(grep -c '^FAIL ' "$PIXEL_LOG" 2>/dev/null || true)
if [ "${bad:-0}" = "0" ]; then
  echo "  OK   no FAIL lines from the pixel tool"
else
  say_fail "$bad FAIL line(s) from the pixel tool"
fi

echo
if [ "$fail" = "0" ]; then
  echo "PASS player-animation-pixels: the entity pass's measured mask is a shaded body (distinct colours, luminance, black share) and the drive moves a measured share of those pixels between the on and off frames of the same scene tick, and every resolved moving pick is a run cycle rather than an air-walk clip, and the same chain is bound to one named entity (id, model, pick, sample, pixels)."
  # The machine verdict, for the same reason as player-animation-check.sh:
  # verify-all.sh greps a token, and matching the INNER tool's PIXEL-CHECK=PASS
  # (which t61-pixels.py prints) is not the same thing as this gate's verdict --
  # the inner line appears on runs where the gate's own assertions failed.
  echo "PLAYER-ANIMATION-PIXELS=PASS"
else
  echo "FAIL player-animation-pixels: see the FAIL lines above."
  echo "PLAYER-ANIMATION-PIXELS=FAIL"
fi
exit "$fail"
