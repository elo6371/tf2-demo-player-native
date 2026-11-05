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
#   A. LEGACY IS UNTOUCHED. The default run and --animation-drive legacy must
#      produce the same frame bytes as today, keep the T61 anim trace lines, and
#      must not emit the new animdsum line.
#   B. THE STATE LAYER REACHES THE FRAME. With --animation-drive states at a tick
#      where the server says crouch/flinch, the frame must differ byte-wise from
#      the legacy frame at the same tick; two runs of the same states tick must be
#      byte-identical (no clock, no randomness); two different ticks that both sit
#      in a crouch must still differ byte-wise (the same state twice still moves).
#   C. EACH STATE IS NAMED. The trace must carry per-state lines with the needle
#      and the reason that drove the pick (crouch, crouch-walk, flinch), and the
#      exit-time counter line must show all three non-zero over a playback that
#      spans them. The needle vocabulary must be the T61/T84 one
#      (crouch|flinch|idle|moving), not an invented label.
#
# NEGATIVE CONTROLS: a reading that has never been seen to fail is not evidence
# (docs/READ-THE-PLAYBOOK-FIRST.md). Three controls run first and each must make
# the checker FAIL with a named reason:
#   1. flinch state lines renamed to fallback in a copy of the trace -> coverage
#      fails (the checker reads the trace, not the product's intent);
#   2. the legacy frame presented as the states frame -> byte-difference fails;
#   3. a REAL product run at an early tick that has no crouch/flinch yet -> the
#      product's own counter line fails coverage.
#
# This gate launches the product and therefore belongs under the host lock when
# the machine is shared; it does not take the lock itself (same convention as
# frame-capture-check.sh: the runner holds it).
#
# Usage: bash animation-drive-states-check.sh [--skip-negative-controls]
# Exit:  0 = every reading held and all three controls failed for their named
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
  TF2_NATIVE_TRACE="$trace" timeout "$TIMEOUT" "$EXE" --tf-root "$TF" \
    --audio-device "$AUDIO_DEVICE" --demo "$demo" --capture-tick "$tick" \
    --capture-frame "$bmp" --metrics-file "$OUT/$name.csv" "$@" > "$log" 2>&1
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
  local cover_trace="$d/states-cover.trace"
  local bad=0 missing=0 f
  for f in "$bmp" "$states_trace" "$states2_bmp" "$crouch_bmp" "$crouch_trace" \
           "$crouch_b_bmp" "$crouch_b_trace" "$legacy_bmp" "$legacy_trace" "$cover_trace"; do
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

  # B1. the state layer reaches the frame.
  local states_md5 crouch_md5 crouch_b_md5
  states_md5=$(md5_of "$bmp")
  if [ -n "$states_md5" ] && [ "$states_md5" != "$legacy_md5" ]; then
    echo "  OK   B states frame differs from legacy at tick $FLINCH_TICK (md5 $states_md5 vs $legacy_md5)"
  else
    echo "  FAIL B states frame is byte-identical to legacy at tick $FLINCH_TICK (md5 ${states_md5:-none})"
    bad=1
  fi

  # B2. determinism: same states tick twice is byte-identical.
  if [ -n "$states_md5" ] && [ "$(md5_of "$states2_bmp")" = "$states_md5" ]; then
    echo "  OK   B repeated states run at tick $FLINCH_TICK is byte-identical"
  else
    echo "  FAIL B repeated states run at tick $FLINCH_TICK differs ($(md5_of "$states2_bmp") vs $states_md5)"
    bad=1
  fi

  # B3. same state twice still moves: two ticks inside the same crouch window
  #     carry the same state name and different frame bytes.
  local crouch_state crouch_b_state
  crouch_md5=$(md5_of "$crouch_bmp")
  crouch_b_md5=$(md5_of "$crouch_b_bmp")
  crouch_state=$(grep -a 'anim: state ' "$crouch_trace" | grep -ao 'state=crouch[a-z]*' | head -1)
  crouch_b_state=$(grep -a 'anim: state ' "$crouch_b_trace" | grep -ao 'state=crouch[a-z]*' | head -1)
  if [ -n "$crouch_state" ] && [ "$crouch_state" = "$crouch_b_state" ]; then
    echo "  OK   B both crouch ticks name the same state ($crouch_state)"
  else
    echo "  FAIL B the two crouch ticks do not name the same state ('$crouch_state' vs '$crouch_b_state')"
    bad=1
  fi
  if [ -n "$crouch_md5" ] && [ -n "$crouch_b_md5" ] && [ "$crouch_md5" != "$crouch_b_md5" ]; then
    echo "  OK   B crouch tick $CROUCH_TICK frame differs from $SAME_STATE_TICK_B inside the same state"
  else
    echo "  FAIL B crouch tick frames are byte-identical (${crouch_md5:-none}), so the state is frozen"
    bad=1
  fi

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

  # C2. the exit-time counters over a playback that spans both events.
  local dsum cl cw cf fs fb
  dsum=$(grep -a 'animdsum:' "$cover_trace" | tail -1)
  if [ -z "$dsum" ]; then
    echo "  FAIL C no animdsum line in the cover run trace"
    bad=1
  else
    echo "  LINE C $dsum"
    cl=$(printf '%s' "$dsum" | grep -o 'crouch=[0-9]*' | cut -d= -f2)
    cw=$(printf '%s' "$dsum" | grep -o 'crouchwalk=[0-9]*' | cut -d= -f2)
    cf=$(printf '%s' "$dsum" | grep -o 'flinch=[0-9]*' | cut -d= -f2)
    fs=$(printf '%s' "$dsum" | grep -o 'flinchStarts=[0-9]*' | cut -d= -f2)
    fb=$(printf '%s' "$dsum" | grep -o 'fallback=[0-9]*' | cut -d= -f2)
    local pair name value
    for pair in "crouch:${cl:-0}" "crouchwalk:${cw:-0}" "flinch:${cf:-0}" "flinchStarts:${fs:-0}" "fallback:${fb:-0}"; do
      name=${pair%%:*}; value=${pair#*:}
      if [ "${value:-0}" -ge 1 ]; then
        echo "  OK   C cover run counted $name=$value"
      else
        echo "  FAIL C cover run counted $name=0 (state never driven over ticks 0..$COVER_TICK)"
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
  say "    3 states-flinch  tick $FLINCH_TICK  --animation-drive states"
  say "    4 states-flinch-repeat tick $FLINCH_TICK --animation-drive states"
  say "    5 states-crouch  tick $CROUCH_TICK  --animation-drive states"
  say "    6 states-crouch-b tick $SAME_STATE_TICK_B --animation-drive states"
  say "    7 states-cover   tick $COVER_TICK   --animation-drive states   demo=${COVER_DEMO:-$DEMO}"
  say "    8 early          tick $EARLY_TICK   --animation-drive states"
  say "  readings: A legacy==default bytes + no animdsum + T61 lines; B states!=legacy, repeat identical, same-state-two-ticks differ; C per-state needle+reason lines and cover counters"
  say "  negative controls: $([ "$SKIP_NEG" -eq 1 ] && echo skipped || echo '1 flinch-renamed, 2 legacy-as-states, 3 early-window')"
  say "  inputs OK"
  echo "ANIMATION-DRIVE-STATES=DRYRUN plan ok (no run performed)"
  exit 0
fi

run_capture default          "$FLINCH_TICK" || failure "default run did not capture"
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
control_failed_as_required() { # control_failed_as_required <label> <dir>
  local label="$1" dir="$2"
  if check_artifacts "$dir" > "$dir/check.txt" 2>&1; then
    failure "negative control $label did NOT fail: the checker is blind to it (see $dir/check.txt)"
    return 1
  fi
  if ! grep -q 'FAIL' "$dir/check.txt"; then
    failure "negative control $label failed without a named reason (see $dir/check.txt)"
    return 1
  fi
  echo "  OK   negative control $label failed as required: $(grep -m1 'FAIL' "$dir/check.txt")" | tee -a "$OUT/gate.txt"
  return 0
}

if [ "$SKIP_NEG" -eq 0 ]; then
  say ""
  say "== negative controls"
  # 1. flinch renamed to fallback in the trace: coverage must notice.
  NEG1="$OUT/neg-1-flinch-renamed"
  rm -rf "$NEG1"; mkdir -p "$NEG1"
  cp "$OUT"/*.bmp "$OUT"/*.trace "$NEG1"/ 2>/dev/null
  sed -i 's/state=flinch /state=fallback /g; s/flinch=[0-9]*/flinch=0/g; s/flinchStarts=[0-9]*/flinchStarts=0/g' "$NEG1"/*.trace
  control_failed_as_required "1-flinch-renamed" "$NEG1"

  # 2. the legacy frame presented as the states frame: the byte difference must
  #    notice (this is the control that catches a states run that never applied).
  NEG2="$OUT/neg-2-legacy-as-states"
  rm -rf "$NEG2"; mkdir -p "$NEG2"
  cp "$OUT"/*.bmp "$OUT"/*.trace "$NEG2"/ 2>/dev/null
  cp "$OUT/legacy.bmp" "$NEG2/states-flinch.bmp"
  control_failed_as_required "2-legacy-as-states" "$NEG2"

  # 3. a real product run at an early tick: its own counters must fail coverage.
  NEG3="$OUT/neg-3-early-window"
  rm -rf "$NEG3"; mkdir -p "$NEG3"
  cp "$OUT"/*.bmp "$OUT"/*.trace "$NEG3"/ 2>/dev/null
  for t in states-flinch states-crouch states-crouch-b states-cover; do
    cp "$OUT/early.trace" "$NEG3/$t.trace"
  done
  control_failed_as_required "3-early-window" "$NEG3"
fi

# ---------------------------------------------------------------------------
say ""
readings_summary="failures=$failures"
if [ "$failures" -eq 0 ]; then
  say "  SUMMARY $readings_summary controls="${SKIP_NEG:+skipped}""
  echo "ANIMATION-DRIVE-STATES=PASS default==legacy byte-identical, states frame differs and is deterministic, crouch/crouchwalk/flinch each named with needle+reason and counted over the playback, negative controls failed as required"
  exit 0
fi
say "  SUMMARY $readings_summary"
echo "ANIMATION-DRIVE-STATES=FAIL (see the FAIL lines above; artifacts in $OUT)"
exit 1

