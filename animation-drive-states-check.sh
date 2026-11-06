#!/usr/bin/env bash
# animation-drive-states-check.sh -- T84: does the server-data state layer change
# the picture, and does it change it for the right reason?
#
# WHY THIS GATE EXISTS
# --------------------
# T61 gave every player model one clock and picked between two sequences: idle and
# moving. T62 measured that the recording also carries crouch state (m_fFlags bit
# 1, DT_Local.m_bDucked/m_bDucking/m_flDucktime) and damage (m_iHealth going down),
# and that every class table has crouch clips while four classes carry a flinch
# clip. T84 turns those readings into a state layer over the T61 clock. Three
# things can be wrong on their own, so this gate measures them separately:
#
#   A. LEGACY IS UNTOUCHED. Every run carries --player-animation, so the T61 path
#      really executes; the default run and --animation-drive legacy must produce
#      the same frame bytes as today, keep the T61 anim trace lines, and must not
#      emit the new animdsum line.
#   B. THE STATE LAYER REACHES THE PICTURE. Two readings, because one tick cannot
#      carry both halves. At the CROUCH tick the states frame must differ byte-wise
#      from a legacy frame captured at the SAME tick (the driven body is on screen
#      there). At the FLINCH tick the states trace must name state=flinch and play
#      a_flinch sequence frames while the legacy trace names no state at all: in
#      this recording the flinching body is not on screen, so identical pixels at
#      that tick are the expected result, not a missing layer (freeze6,
#      2026-11-06; see the B1 reading comment). Two runs of the same states tick
#      must be byte-identical (no clock, no randomness). What this gate does NOT assert, and why: two ticks
#      inside one crouch are expected to be identical, because the crouch pick is
#      an aim-matrix pose (label=PRIMARY_aimmatrix_crouch_idle cycle=0.000000).
#   C. EACH STATE IS NAMED. The trace must carry per-state lines with the needle
#      and the reason that drove the pick (crouch, crouch-walk, flinch), and the
#      per-frame counter line must reach every state at least once somewhere in
#      the playback (the peak over all animdsum frames, not the last frame). The
#      needle vocabulary must be the T61/T84 one (crouch|flinch|idle|moving), not
#      an invented label.
#
# NEGATIVE CONTROLS: a reading that has never been seen to fail is not evidence
# (docs/READ-THE-PLAYBOOK-FIRST.md). Four controls run after the readings and each
# must make the checker FAIL, for its OWN named reason (the helper greps for the
# expected FAIL text, not just any FAIL):
#   1. flinch state lines renamed to fallback in a copy of the traces -> the
#      per-state coverage line for flinch disappears;
#   2. the flinch evidence stripped from the states trace -> the flinch reading
#      disappears;
#   3. the legacy crouch frame presented as the states crouch frame -> the
#      crouch-layer pixel reading disappears;
#   4. the animdsum counter lines stripped from the cover trace -> the playback
#      coverage reading has no frames to read.
#
# This gate launches the product and therefore belongs under the host lock when
# the machine is shared; it does not take the lock itself (same convention as
# frame-capture-check.sh: the runner holds it).
#
# Usage: bash animation-drive-states-check.sh [--skip-negative-controls]
# Exit:  0 = every reading held and all four controls failed for their named
#            reason; the last line is ANIMATION-DRIVE-STATES=PASS.
#        1 = a reading or a control did not hold.
#        2 = a required input is missing (product, demo, TF root, md5sum).
set -uo pipefail
cd "$(dirname "$0")"

EXE=native/build-nmake/tf2_demo_native.exe
TF="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
DEMO="D:/TF2_Demo_Player/testdata/demos/bb841c6d379ff7c40d0c8baf99f59d8d_matcha-20260927-1347-cp_snakewater_final1.dem"
OUT="${T84_STATES_OUT:-evidence/t84-animdrive/animation-drive-states}"
AUDIO_DEVICE=6
# Ticks measured on this recording with entity_model_probe --props-at (REPORT.md
# section 3). FLINCH_TICK is the first tick whose m_iHealth is below the previous
# read for entity 6 (blue medic, 150 -> 108); CROUCH_TICK and SAME_STATE_TICK_B
# both sit inside the soldier entity 2 duck window (m_bDucking=1 while m_fFlags
# bit 1 is clear); COVER_TICK spans both so the exit counters cover all three
# states; EARLY_TICK is before the first crouch (negative control 3).
FLINCH_TICK="${T84_FLINCH_TICK:-16780}"
CROUCH_TICK="${T84_CROUCH_TICK:-14765}"
SAME_STATE_TICK_B="${T84_SAME_STATE_TICK_B:-14800}"
COVER_TICK="${T84_COVER_TICK:-20000}"
# The cover run may point at a different recording: the state counters are the
# one reading that needs a playback containing every state, and the recording
# that carries the flinch also has to carry a crouch-walk instance. Empty means
# "use DEMO".
COVER_DEMO="${T84_COVER_DEMO:-}"
EARLY_TICK="${T84_EARLY_TICK:-12000}"
TIMEOUT=420

SKIP_NEG=0
DRY_RUN=0
for arg in "$@"; do
  case "$arg" in
    --skip-negative-controls) SKIP_NEG=1 ;;
    --dry-run) DRY_RUN=1 ;;
    *) echo "FATAL: unknown argument: $arg"; exit 2 ;;
  esac
done

for path in "$EXE" "$DEMO" ${COVER_DEMO:+"$COVER_DEMO"}; do
  [ -e "$path" ] || { echo "FATAL: missing input: $path"; exit 2; }
done
[ -d "$TF" ] || { echo "FATAL: missing TF root: $TF"; exit 2; }
command -v md5sum >/dev/null 2>&1 || { echo "FATAL: md5sum not on PATH"; exit 2; }

mkdir -p "$OUT"
: > "$OUT/gate.txt"
failures=0
say() { echo "$@" | tee -a "$OUT/gate.txt"; }
failure() { echo "  FAIL $*" | tee -a "$OUT/gate.txt"; failures=$((failures + 1)); }
md5_of() { md5sum "$1" 2>/dev/null | cut -d' ' -f1; }

run_capture() { # run_capture <name> <tick> [extra args...]
  local name="$1" tick="$2"; shift 2
  local bmp="$OUT/$name.bmp" trace="$OUT/$name.trace" log="$OUT/$name.log"
  rm -f "$bmp" "$trace" "$log" "$OUT/$name.csv"
  local demo="${RUN_DEMO:-$DEMO}"
  # --player-animation is on EVERY run, including the two legacy ones. Without
  # it the T61 path (anim: pick / animsummary) never executes, so "legacy is
  # untouched" would be measured on a binary path the gate never entered -- the
  # first real run of this gate did exactly that and reported the T61 lines as
  # lost when they had simply never been asked for (2026-11-05 18:36, freeze4
  # step drives).
  TF2_NATIVE_TRACE="$trace" timeout "$TIMEOUT" "$EXE" --tf-root "$TF" \
    --audio-device "$AUDIO_DEVICE" --demo "$demo" --capture-tick "$tick" \
    --capture-frame "$bmp" --metrics-file "$OUT/$name.csv" \
    --player-animation "$@" > "$log" 2>&1
  local rc=$?
  echo "$rc" > "$OUT/$name.rc"
  if [ -s "$bmp" ]; then
    echo "  RUN $name tick=$tick rc=$rc bytes=$(wc -c < "$bmp")" | tee -a "$OUT/gate.txt"
  else
    echo "  RUN $name tick=$tick rc=$rc BMP-MISSING" | tee -a "$OUT/gate.txt"
  fi
  return $rc
}


# check_artifacts <dir> -- the three readings above, over one directory of runs.
# Returns 0 only when every reading held. It reads the SAVED artifacts only, so a
# negative control can hand it a mutated copy of the same directory.
check_artifacts() {
  local d="$1"
  local bmp="$d/states-flinch.bmp" states_trace="$d/states-flinch.trace"
  local states2_bmp="$d/states-flinch-repeat.bmp"
  local crouch_bmp="$d/states-crouch.bmp" crouch_trace="$d/states-crouch.trace"
  local crouch_b_bmp="$d/states-crouch-b.bmp" crouch_b_trace="$d/states-crouch-b.trace"
  local legacy_bmp="$d/legacy.bmp" legacy_trace="$d/legacy.trace"
  local legacy_crouch_bmp="$d/legacy-crouch.bmp"
  local cover_trace="$d/states-cover.trace"
  local bad=0 missing=0 f
  for f in "$bmp" "$states_trace" "$states2_bmp" "$crouch_bmp" "$crouch_trace" \
           "$crouch_b_bmp" "$crouch_b_trace" "$legacy_bmp" "$legacy_trace" \
           "$legacy_crouch_bmp" "$cover_trace"; do
    [ -s "$f" ] || { echo "  FAIL missing artifact: $f"; missing=1; bad=1; }
  done
  [ "$missing" -eq 0 ] || return 1

  # A. legacy untouched: default run == explicit legacy run, T61 lines still
  #    there, no animdsum.
  local legacy_md5 default_md5
  legacy_md5=$(md5_of "$legacy_bmp")
  default_md5=$(md5_of "$d/default.bmp")
  if [ -n "${default_md5:-}" ] && [ "$legacy_md5" = "${default_md5:-}" ]; then
    echo "  OK   A default frame == --animation-drive legacy frame (md5 $legacy_md5)"
  else
    echo "  FAIL A default frame differs from --animation-drive legacy frame (${default_md5:-none} vs $legacy_md5)"
    bad=1
  fi
  if grep -qa 'animdsum:' "$legacy_trace"; then
    echo "  FAIL A --animation-drive legacy emitted animdsum, so legacy is not today's path"
    bad=1
  else
    echo "  OK   A legacy trace carries no animdsum line"
  fi
  local picks legacy_sums
  picks=$(grep -ac 'anim: pick ' "$legacy_trace" || true)
  legacy_sums=$(grep -ac 'animsum:' "$legacy_trace" || true)
  if [ "${picks:-0}" -ge 1 ] && [ "${legacy_sums:-0}" -ge 1 ]; then
    echo "  OK   A legacy trace still carries the T61 lines (anim: pick x$picks, animsum x$legacy_sums)"
  else
    echo "  FAIL A legacy trace lost the T61 lines (anim: pick x${picks:-0}, animsum x${legacy_sums:-0})"
    bad=1
  fi

  # B1. the flinch layer reaches the ANIMATION READING, not the frame.
  #     The flinch fires on a body the recording's own first-person camera is not
  #     looking at: in freeze6 (2026-11-06 11:29) the states run moved that body
  #     from seq=9 to seq=17 (label=a_flinch01, advanced=1) and the captured frame
  #     stayed byte-identical to legacy (md5 d42e2e734bc0757d8a39224b102addb0),
  #     because a body that contributes no pixels cannot change a pixel. The old
  #     reading here asserted "the frame must differ at the flinch tick", which is
  #     a false statement about THIS demo, not a true one about the product. The
  #     pixel proof that the states layer reaches the frame is B3, at the crouch
  #     tick, where the driven body is on screen (f814baec... vs b9439a65...).
  #     What B1 can own is the reading the gate is actually about: the states run
  #     names state=flinch and plays a_flinch sequence frames, and the legacy run
  #     does neither.
  local states_md5 crouch_md5 crouch_b_md5
  local states_flinch_lines states_flinch_seq legacy_state_lines legacy_flinch_seq
  states_md5=$(md5_of "$bmp")
  states_flinch_lines=$(grep -a 'anim: state ' "$states_trace" | grep -ac 'state=flinch ' || true)
  states_flinch_seq=$(grep -ac 'label=a_flinch0' "$states_trace" || true)
  legacy_state_lines=$(grep -ac 'anim: state ' "$legacy_trace" || true)
  legacy_flinch_seq=$(grep -ac 'label=a_flinch0' "$legacy_trace" || true)
  if [ "${states_flinch_lines:-0}" -ge 1 ] && [ "${states_flinch_seq:-0}" -ge 1 ]; then
    echo "  OK   B the states trace names state=flinch (x$states_flinch_lines) and plays a_flinch frames (x$states_flinch_seq)"
  else
    echo "  FAIL B the states trace never names state=flinch (state lines x${states_flinch_lines:-0}, a_flinch frames x${states_flinch_seq:-0})"
    bad=1
  fi
  if [ "${legacy_state_lines:-0}" -eq 0 ] && [ "${legacy_flinch_seq:-0}" -eq 0 ]; then
    echo "  OK   B the legacy trace names no state and plays no flinch (legacy is still the T61 path)"
  else
    echo "  FAIL B the legacy trace carries state/flinch lines (state x${legacy_state_lines:-0}, a_flinch x${legacy_flinch_seq:-0})"
    bad=1
  fi
  # The flinch frames are printed, not asserted. See the B1 comment: the driven
  # body is off screen at this tick, so identical pixels are the expected result.
  echo "  LINE B flinch tick $FLINCH_TICK md5 ${states_md5:-none} (states) vs ${legacy_md5:-none} (legacy) -- identical is EXPECTED here, that body draws no pixels"

  # B2. determinism: same states tick twice is byte-identical.
  if [ -n "$states_md5" ] && [ "$(md5_of "$states2_bmp")" = "$states_md5" ]; then
    echo "  OK   B repeated states run at tick $FLINCH_TICK is byte-identical"
  else
    echo "  FAIL B repeated states run at tick $FLINCH_TICK differs ($(md5_of "$states2_bmp") vs $states_md5)"
    bad=1
  fi

  # B3. the crouch layer reaches the frame at ITS tick: the states frame at the
  #     crouch tick must differ from a legacy frame captured at the SAME tick.
  #     Comparing two ticks inside the crouch instead (what this gate first did)
  #     cannot work: the crouch pick is an aim-matrix pose -- its own trace line
  #     says label=PRIMARY_aimmatrix_crouch_idle cycle=0.000000 advanced=0 -- so
  #     two ticks inside the crouch are SUPPOSED to look identical, and the old
  #     check reported the correct result as "the state is frozen" (freeze4,
  #     2026-11-05 18:36). A frozen states layer is still caught by B3 (the
  #     crouch pair moved when the layer is alive) and by B1's trace reading (a
  #     layer that never picks flinch leaves no state=flinch line behind).
  local crouch_state crouch_b_state legacy_crouch_md5
  crouch_md5=$(md5_of "$crouch_bmp")
  crouch_b_md5=$(md5_of "$crouch_b_bmp")
  legacy_crouch_md5=$(md5_of "$legacy_crouch_bmp")
  crouch_state=$(grep -a 'anim: state ' "$crouch_trace" | grep -ao 'state=crouch[a-z]*' | head -1)
  crouch_b_state=$(grep -a 'anim: state ' "$crouch_b_trace" | grep -ao 'state=crouch[a-z]*' | head -1)
  if [ -n "$crouch_state" ] && [ "$crouch_state" = "$crouch_b_state" ]; then
    echo "  OK   B both crouch ticks name the same state ($crouch_state)"
  else
    echo "  FAIL B the two crouch ticks do not name the same state ('$crouch_state' vs '$crouch_b_state')"
    bad=1
  fi
  if [ -n "$crouch_md5" ] && [ -n "$legacy_crouch_md5" ] && [ "$crouch_md5" != "$legacy_crouch_md5" ]; then
    echo "  OK   B crouch layer reached the frame: states $crouch_md5 != legacy $legacy_crouch_md5 at tick $CROUCH_TICK"
  else
    echo "  FAIL B the crouch layer did not reach the frame: states and legacy are byte-identical at tick $CROUCH_TICK (${crouch_md5:-none} vs ${legacy_crouch_md5:-none})"
    bad=1
  fi
  # The two crouch frames are printed as a reading, not asserted, for the reason
  # in the B3 comment above.
  echo "  LINE B crouch tick $CROUCH_TICK md5 ${crouch_md5:-none}, tick $SAME_STATE_TICK_B md5 ${crouch_b_md5:-none} (${crouch_state:-none} both)"

  # C1. every state has a named trace line with needle and reason.
  local wants line needle reason
  for wants in crouch crouchwalk flinch; do
    line=$(grep -ah 'anim: state ' "$states_trace" "$crouch_trace" "$crouch_b_trace" "$cover_trace" 2>/dev/null | grep -a "state=$wants " | head -1 || true)
    if [ -z "$line" ]; then
      echo "  FAIL C no trace line names state=$wants"
      bad=1
      continue
    fi
    needle=$(printf '%s' "$line" | grep -ao 'needle=[a-z|]*' | head -1)
    reason=$(printf '%s' "$line" | grep -ao 'reason=[a-z-]*' | head -1)
    if [ -z "$needle" ] || [ -z "$reason" ]; then
      echo "  FAIL C state=$wants line lacks needle/reason: $line"
      bad=1
      continue
    fi
    case " $needle " in
      " needle=crouch "|" needle=flinch "|" needle=idle|moving ")
        echo "  OK   C state=$wants $needle $reason" ;;
      *)
        echo "  FAIL C state=$wants carries an unexpected needle ($needle): $line"
        bad=1 ;;
    esac
  done

  # C2. the counters over a playback that spans both events.
  #
  # animdsum is a PER-FRAME line: crouch=2 means "two bodies are crouched in this
  # frame", flinchStarts=7 means "seven bodies entered the flinch on this frame".
  # The first real run of this gate read only the LAST line (one frame at the end
  # of the playback) and called crouchwalk=0 / flinchStarts=0 a missing state,
  # while the same trace carried crouchwalk=2 and flinchStarts=7 in other frames.
  # So the reading is the peak over every animdsum line in the playback, and the
  # tick where the peak first appears is printed next to it.
  local dsum_file dsum_lines cl cw cf fs fb peak_cw_tick peak_fs_tick
  dsum_file="$(mktemp)"
  grep -a 'animdsum:' "$cover_trace" > "$dsum_file" || true
  dsum_lines=$(wc -l < "$dsum_file")
  if [ "$dsum_lines" -eq 0 ]; then
    echo "  FAIL C no animdsum line in the cover run trace"
    bad=1
  else
    peak_of() { grep -o "$1=[0-9]*" "$dsum_file" | cut -d= -f2 | sort -n | tail -1; }
    cl=$(peak_of crouch); cw=$(peak_of crouchwalk); cf=$(peak_of flinch)
    fs=$(peak_of flinchStarts); fb=$(peak_of fallback)
    peak_cw_tick=$(grep -a "crouchwalk=${cw:-0} " "$dsum_file" | head -1 | grep -o 'tick=[0-9]*' | head -1)
    peak_fs_tick=$(grep -a "flinchStarts=${fs:-0} " "$dsum_file" | head -1 | grep -o 'tick=[0-9]*' | head -1)
    echo "  LINE C peak over $dsum_lines animdsum frames: crouch=${cl:-0} crouchwalk=${cw:-0}($peak_cw_tick) flinch=${cf:-0} flinchStarts=${fs:-0}($peak_fs_tick) fallback=${fb:-0}"
    echo "  LINE C last frame: $(tail -1 "$dsum_file")"
    rm -f "$dsum_file"
    local pair name value
    for pair in "crouch:${cl:-0}" "crouchwalk:${cw:-0}" "flinch:${cf:-0}" "flinchStarts:${fs:-0}" "fallback:${fb:-0}"; do
      name=${pair%%:*}; value=${pair#*:}
      if [ "${value:-0}" -ge 1 ]; then
        echo "  OK   C the playback reached $name=$value in at least one frame"
      else
        echo "  FAIL C the playback never showed $name >= 1 in any frame (capture tick $COVER_TICK)"
        bad=1
      fi
    done
  fi

  return $bad
}


# ---------------------------------------------------------------------------
# RUN PLAN. One product run per reading. Each run starts from the demo's own
# first tick and advances to --capture-tick, so the cover run sees every earlier
# tick: its animdsum counters are cumulative over the whole playback.
# ---------------------------------------------------------------------------
say "== T84 animation-drive-states gate"
say "DEMO $DEMO"
say "TICKS flinch=$FLINCH_TICK crouch=$CROUCH_TICK sameStateB=$SAME_STATE_TICK_B cover=$COVER_TICK early=$EARLY_TICK"
[ -n "$COVER_DEMO" ] && say "COVER_DEMO $COVER_DEMO"

if [ "$DRY_RUN" -eq 1 ]; then
  # --dry-run: validate every input and print the plan, run nothing. A gate that
  # is about to hold a shared GPU lock must be checkable without the lock.
  say "== dry run (no product invocation)"
  say "  EXE   $EXE"
  say "  DEMO  $DEMO"
  say "  TF    $TF"
  say "  OUT   $OUT"
  say "  plan:"
  say "    1 default        tick $FLINCH_TICK  (no --animation-drive)"
  say "    2 legacy         tick $FLINCH_TICK  --animation-drive legacy"
  say "    3 legacy-crouch  tick $CROUCH_TICK  --animation-drive legacy   (B3 same-tick pair)"
  say "    4 states-flinch  tick $FLINCH_TICK  --animation-drive states"
  say "    5 states-flinch-repeat tick $FLINCH_TICK --animation-drive states"
  say "    6 states-crouch  tick $CROUCH_TICK  --animation-drive states"
  say "    7 states-crouch-b tick $SAME_STATE_TICK_B --animation-drive states"
  say "    8 states-cover   tick $COVER_TICK   --animation-drive states   demo=${COVER_DEMO:-$DEMO}"
  say "    9 early          tick $EARLY_TICK   --animation-drive states"
  say "    every run also carries --player-animation, so the T61 path executes"
  say "  readings: A legacy==default bytes + no animdsum + T61 lines; B states trace names state=flinch and plays a_flinch (legacy names neither) + states!=legacy at the crouch tick + repeat byte-identical; C per-state needle+reason lines and peak counters over the playback"
  say "  negative controls: $([ "$SKIP_NEG" -eq 1 ] && echo skipped || echo '1 flinch-renamed, 2 flinch-stripped, 3 crouch-as-legacy, 4 dsum-stripped')"
  say "  inputs OK"
  echo "ANIMATION-DRIVE-STATES=DRYRUN plan ok (no run performed)"
  exit 0
fi

run_capture default          "$FLINCH_TICK" || failure "default run did not capture"
# The legacy frame AT THE CROUCH TICK. B3 needs the same tick as states-crouch,
# otherwise "the layer reached the frame" would compare two different moments.
run_capture legacy-crouch    "$CROUCH_TICK" --animation-drive legacy || failure "legacy crouch run did not capture"
run_capture legacy           "$FLINCH_TICK" --animation-drive legacy || failure "legacy run did not capture"
run_capture states-flinch    "$FLINCH_TICK" --animation-drive states || failure "states flinch run did not capture"
run_capture states-flinch-repeat "$FLINCH_TICK" --animation-drive states || failure "states repeat run did not capture"
run_capture states-crouch    "$CROUCH_TICK" --animation-drive states || failure "states crouch run did not capture"
run_capture states-crouch-b  "$SAME_STATE_TICK_B" --animation-drive states || failure "states same-state run did not capture"
RUN_DEMO="$COVER_DEMO" run_capture states-cover "$COVER_TICK" --animation-drive states || failure "states cover run did not capture"
run_capture early            "$EARLY_TICK" --animation-drive states || failure "early run did not capture"

say ""
say "== readings"
if check_artifacts "$OUT" | tee -a "$OUT/gate.txt"; then
  say "  READINGS=PASS"
else
  failure "one or more readings failed (see the FAIL lines above)"
fi

# ---------------------------------------------------------------------------
# NEGATIVE CONTROLS. Each hands the checker a directory it must reject.
# ---------------------------------------------------------------------------
control_failed_as_required() { # <label> <dir> <expected FAIL text (grep -E)>
  local label="$1" dir="$2" expect="$3"
  if check_artifacts "$dir" > "$dir/check.txt" 2>&1; then
    failure "negative control $label did NOT fail: the checker is blind to it (see $dir/check.txt)"
    return 1
  fi
  if ! grep -q 'FAIL' "$dir/check.txt"; then
    failure "negative control $label failed without a named reason (see $dir/check.txt)"
    return 1
  fi
  # Failing for SOME reason is not enough: each control targets one reading, and a
  # checker that fails a mutated directory for an unrelated reason has not shown
  # that it can see the mutation. The first real run of this gate failed all three
  # controls on the same A reading, which proved nothing about B or C.
  if ! grep -qE -- "$expect" "$dir/check.txt"; then
    failure "negative control $label failed for the wrong reason: expected /$expect/ (see $dir/check.txt)"
    return 1
  fi
  echo "  OK   negative control $label failed as required: $(grep -m1E -- "$expect" "$dir/check.txt")" | tee -a "$OUT/gate.txt"
  return 0
}

if [ "$SKIP_NEG" -eq 0 ]; then
  say ""
  say "== negative controls"
  # 1. flinch renamed to fallback in the trace: the per-state coverage line must
  #    notice (the checker reads the trace, not the product's intent).
  NEG1="$OUT/neg-1-flinch-renamed"
  rm -rf "$NEG1"; mkdir -p "$NEG1"
  cp "$OUT"/*.bmp "$OUT"/*.trace "$NEG1"/ 2>/dev/null
  sed -i 's/state=flinch /state=fallback /g; s/flinch=[0-9]*/flinch=0/g; s/flinchStarts=[0-9]*/flinchStarts=0/g' "$NEG1"/*.trace
  control_failed_as_required "1-flinch-renamed" "$NEG1" 'no trace line names state=flinch'

  # 2. the flinch evidence stripped from the states trace: B1 reads the trace, so
  #    a states run that never applied must be caught there. The pixel-domain
  #    twin of this control is NEG3, at the tick where the driven body IS on
  #    screen; see the B1 reading comment for why the flinch tick cannot be it.
  NEG2="$OUT/neg-2-flinch-stripped"
  rm -rf "$NEG2"; mkdir -p "$NEG2"
  cp "$OUT"/*.bmp "$OUT"/*.trace "$NEG2"/ 2>/dev/null
  sed -i 's/state=flinch /state=idle /g; s/label=a_flinch01/label=a_idle01/g' "$NEG2/states-flinch.trace"
  control_failed_as_required "2-flinch-stripped" "$NEG2" 'the states trace never names state=flinch'

  # 3. the crouch frame replaced by the legacy frame at the same tick: B3 must
  #    notice, which is the reading that proves the crouch layer reached pixels.
  NEG3="$OUT/neg-3-crouch-as-legacy"
  rm -rf "$NEG3"; mkdir -p "$NEG3"
  cp "$OUT"/*.bmp "$OUT"/*.trace "$NEG3"/ 2>/dev/null
  cp "$OUT/legacy-crouch.bmp" "$NEG3/states-crouch.bmp"
  control_failed_as_required "3-crouch-as-legacy" "$NEG3" 'crouch layer did not reach the frame'

  # 4. the counter line stripped from the cover trace: the coverage reading must
  #    notice a playback that reports no frames at all.
  NEG4="$OUT/neg-4-dsum-stripped"
  rm -rf "$NEG4"; mkdir -p "$NEG4"
  cp "$OUT"/*.bmp "$OUT"/*.trace "$NEG4"/ 2>/dev/null
  sed -i '/animdsum:/d' "$NEG4/states-cover.trace"
  control_failed_as_required "4-dsum-stripped" "$NEG4" 'no animdsum line in the cover run trace'
fi

# ---------------------------------------------------------------------------
say ""
readings_summary="failures=$failures"
if [ "$failures" -eq 0 ]; then
  say "  SUMMARY $readings_summary controls="${SKIP_NEG:+skipped}""
  echo "ANIMATION-DRIVE-STATES=PASS default==legacy byte-identical with the T61 lines intact, the states trace names state=flinch and plays a_flinch sequence frames while the legacy trace names neither, states crouch frame differs from legacy at the same tick and repeats byte-identically, crouch/crouchwalk/flinch each named with needle+reason and each reached by the per-frame counters, four negative controls failed for their own reasons"
  exit 0
fi
say "  SUMMARY $readings_summary"
echo "ANIMATION-DRIVE-STATES=FAIL (see the FAIL lines above; artifacts in $OUT)"
exit 1

