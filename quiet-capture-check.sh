#!/usr/bin/env bash
# quiet-capture-check.sh -- the capture path must be runnable with the window
# hidden and with the audio device never opened, and it must produce the same
# frame.
#
# WHY THIS EXISTS
# ---------------
# A capture gate that opens a visible window takes the desktop away from whoever
# is using the machine, and one that opens the audio device makes noise on it.
# Both are reasons a chain gets run less often, and a chain that is run less
# often is a chain that stops being run. So the two test-only switches --
# `--hidden-window` and `--audio-disabled` -- exist to make a capture gate
# quiet, and this file is what says they are quiet *and* still real:
#
#   * the window state is checked with the program's own IsWindowVisible()
#     answer, not by trusting the flag, so "the window is hidden" is a reading;
#   * the audio state is checked with a process-wide count of waveOutOpen calls,
#     incremented at the call site so a call that failed still counts, so "no
#     device was opened" is a reading and not the absence of a crash;
#   * the frame is compared byte for byte against the same tick of the same
#     demo taken with the window shown and the device live, so "still real" is
#     a picture and not a counter.
#
# WHAT IT DOES NOT DO
# -------------------
# It does not replace frame-capture-check.sh and it does not judge the picture.
# It answers "is the quiet mode still the same capture"; determinism of the
# bytes across two runs of the same command is frame-capture-check.sh's subject.
#
# TWO PAIRS, BECAUSE ONE PAIR CANNOT ANSWER BOTH QUESTIONS
# -------------------------------------------------------
# Audio only advances while playback is running, and a running playback reaches
# a tick after a wall-clock interval that differs between two runs. So:
#
#   pinned  both runs are `--start-paused`, which pins the tick by
#           construction, and the pair can be held to equality: the same tick,
#           the same frame, the same metrics row, the same trace, drawn=1.
#           A paused scheduler does not advance, so this pair says nothing about
#           audio -- its audio readings are 0 on both sides and are not treated
#           as evidence about the switch.
#   live    both runs play for real to a capture tick of 600. This is where the
#           scheduler advances and the device is really asked for, so it is
#           where the audio contract is asserted, including the identities that
#           tie the two sides together: the quiet run must have *seen* exactly
#           what the normal run saw, must have played nothing, and must have
#           skipped exactly the events the normal run played. `drawn` and the
#           tick at which the capture fires are clock readings here; they are
#           reported and bounded, not compared.
#
# No demo means no audio events, and no audio events means the quiet run's
# `audio_device_opened=0` would be true for a reason that has nothing to do with
# the switch. The live pair therefore asserts the control side too: on this pair
# the normal run has to have opened the device and played something, or the
# quiet readings above are vacuous.
#
# WHERE THE ARTIFACTS GO
# ----------------------
# `.scratch/quiet-capture/`, not `evidence/`. The scratch tree is gitignored,
# and this task's file list does not include an evidence directory; leaving four
# bitmaps and four traces untracked in a tree whose `git status` is part of the
# handoff would be noise. Set QUIET_CAPTURE_OUT to write them somewhere else.
#
# Usage: bash quiet-capture-check.sh [--mutation]
#   --mutation first runs the control pairs above, then rebuilds the product
#   with one defect reintroduced at a time and requires this file's own readings
#   to go red, then restores the source byte-for-byte and rebuilds. Four cases:
#   the window flag ignored, the audio flag ignored, the quiet run's capture
#   never handed back, and the device-free sink answering `played` instead of
#   `skipped`. See the section header below for why the restore is a copy from
#   a scratch backup rather than `git checkout`.
# Exit: 0 = every assertion held (or, with --mutation, every mutation went red
#   and every control reading held); 1 = something did not.

set -uo pipefail
cd "$(dirname "$0")"

EXE=native/build-nmake/tf2_demo_native.exe
PROBE=native/build-nmake/quiet_capture_probe.exe
TF="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
BAGEL="D:/TF2_Demo_Player/testdata/demos/4a9bfb9276509d0ec5f5fdc722a95b17_match-20260927-0239-koth_bagel_rc13.dem"
OUT="${QUIET_CAPTURE_OUT:-.scratch/quiet-capture}"
mkdir -p "$OUT"

MUTATION=0
[ "${1:-}" = "--mutation" ] && MUTATION=1

# Missing input is a failure, not a skip: a quiet-capture gate that quietly
# SKIPs when the binary or the demo is absent would report success for a run
# that did no work, which is the shape this repository has already paid for.
fail=0
for path in "$EXE" "$PROBE" "$TF" "$BAGEL"; do
  [ -e "$path" ] || { echo "FATAL: missing input: $path"; exit 1; }
done

PY="C:/Users/Administrator/.workbuddy-ai/binaries/python/versions/3.13.12/python.exe"
[ -x "$PY" ] || { echo "FATAL: missing interpreter: $PY"; exit 1; }
for tool in cygpath timeout stat cmp; do
  command -v "$tool" >/dev/null 2>&1 || { echo "FATAL: missing tool: $tool"; exit 1; }
done

# The pinned tick is 0 because a paused run cannot leave it; the live tick is
# 600 because it is far enough in that the demo has been running for ~9 s of
# playback and hundreds of frames have been drawn, so "the capture came from a
# run that really ran" is not a claim about the first frame.
PINNED_TICK=0
LIVE_TICK=600
PINNED_CHECKS=39
LIVE_CHECKS=45

# How long a run gets before its capture is declared missing. The load phase
# (VPK index, demo scan, 52 model uploads) measured ~28 s on this machine, the
# live normal run's audio flush (2376 waveOutOpen calls) another ~22 s, and the
# live pair ~63 s end to end. 240 s is a generous multiple; a capture that needs
# more than this is a step to re-measure, not a step to wait out.
RUN_TIMEOUT=240
# The default is the wrong device to make noise on, so every launch names one
# explicitly, exactly as frame-capture-check.sh does.
AUDIO_DEVICE=6

assert_failed=0
assert_eq() { # assert_eq <label> <actual> <expected>
  if [ "${2:-<none>}" = "$3" ]; then
    echo "  OK   $1 = $3"
  else
    echo "  FAIL $1 = ${2:-<none>}, expected $3"; fail=1; assert_failed=$((assert_failed + 1))
  fi
}

assert_ge() { # assert_ge <label> <actual> <floor>
  if [ -n "${2:-}" ] && [ "${2:-0}" -ge "$3" ] 2>/dev/null; then
    echo "  OK   $1 = $2 (>= $3)"
  else
    echo "  FAIL $1 = ${2:-<none>}, expected >= $3"; fail=1; assert_failed=$((assert_failed + 1))
  fi
}

# Reads one field out of a probe report. The probe emits a single key=value
# line, so the first match is the only match.
pvalue() { # pvalue <probe-output> <field>
  grep -oE "$2=[^ ]*" "$1" 2>/dev/null | head -1 | cut -d= -f2
}

# Runs the program once. Kept in one place so both pairs, every mutation and the
# trace path all agree on how a run is launched.
run_one() { # run_one <output-prefix> [args...]
  local prefix="$1"; shift
  rm -f "$prefix.bmp" "$prefix.csv" "$prefix.trace" "$prefix.log"
  # TF2_NATIVE_TRACE is read with GetEnvironmentVariableW and opened by the
  # program, so it needs the Windows form of the path; cygpath -m gives it
  # without the doubled backslashes the MSYS layer rewrites.
  local trace
  trace="$(cygpath -m "$PWD/$prefix.trace")"
  TF2_NATIVE_TRACE="$trace" timeout "$RUN_TIMEOUT" "$EXE" \
    --tf-root "$TF" --audio-device "$AUDIO_DEVICE" "$@" \
    --capture-frame "$prefix.bmp" --metrics-file "$prefix.csv" > "$prefix.log" 2>&1
  echo "$?"
}

# rc 0 is "wrote a frame and left on its own"; 124 is the timeout, which means
# the frame never arrived, and 16 is the renderer's own "the capture failed"
# code. They are reported separately so a reader can tell them apart.
report_run() { # report_run <label> <prefix> <rc>
  echo "  $1 rc=$3 bmp=$(stat -c%s "$2.bmp" 2>/dev/null || echo 0) trace=$(stat -c%s "$2.trace" 2>/dev/null || echo 0)"
}

file_hash() { # file_hash <path>
  "$PY" -c "import hashlib,sys;print(hashlib.sha256(open(sys.argv[1],'rb').read()).hexdigest())" "$1"
}

# --- the two pairs ---------------------------------------------------------
# Each function runs its pair, runs the probe over it, prints the probe's line,
# and applies the assertions for that pair. They are separate functions because
# --mutation re-runs exactly one of them per case, and it must be the same code
# the control run used: a mutation checked by different assertions than the
# control is not a mutation of the same claim.

run_pinned_pair() { # run_pinned_pair <prefix-base>
  local base="$1"
  local pn="$OUT/$base-normal" pq="$OUT/$base-quiet"
  local pn_rc pq_rc
  pn_rc=$(run_one "$pn" --start-paused --capture-tick "$PINNED_TICK" --demo "$BAGEL")
  pq_rc=$(run_one "$pq" --start-paused --hidden-window --audio-disabled \
    --capture-tick "$PINNED_TICK" --demo "$BAGEL")
  report_run "pinned normal" "$pn" "$pn_rc"
  report_run "pinned quiet " "$pq" "$pq_rc"
  "$PROBE" --mode pinned --capture-tick "$PINNED_TICK" \
    --normal-trace "$pn.trace" --quiet-trace "$pq.trace" \
    --normal-metrics "$pn.csv" --quiet-metrics "$pq.csv" \
    --normal-bmp "$pn.bmp" --quiet-bmp "$pq.bmp" > "$OUT/$base-probe.txt" 2>&1
  local rc=$?
  echo "$rc" > "$OUT/$base-probe.rc"
  cat "$OUT/$base-probe.txt"
  assert_eq "pinned normal run rc" "$pn_rc" "0"
  assert_eq "pinned quiet run rc" "$pq_rc" "0"
  apply_pinned_assertions "$OUT/$base-probe.txt" "$rc" "$pn" "$pq" "$base"
}

apply_pinned_assertions() { # apply_pinned_assertions <probe-out> <probe-rc> <normal-prefix> <quiet-prefix> <label>
  local f="$1" prc="$2" pn="$3" pq="$4" tag="${5:-pinned}"
  assert_eq "$tag probe exit" "$prc" "0"
  assert_eq "$tag probe mode" "$(pvalue "$f" mode)" "pinned"
  assert_eq "$tag capture_tick" "$(pvalue "$f" capture_tick)" "$PINNED_TICK"

  # The required gate readings, quiet side.
  assert_eq "quiet window_created" "$(pvalue "$f" quiet.window_created)" "1"
  assert_eq "quiet window_visible" "$(pvalue "$f" quiet.window_visible)" "0"
  assert_eq "quiet audio_mode" "$(pvalue "$f" quiet.audio_mode)" "disabled"
  assert_eq "quiet audio_device_opened" "$(pvalue "$f" quiet.audio_device_opened)" "0"
  assert_eq "quiet drawn" "$(pvalue "$f" quiet.drawn)" "1"
  assert_eq "quiet capture_written" "$(pvalue "$f" quiet.capture_written)" "1"
  assert_eq "quiet metrics_written" "$(pvalue "$f" quiet.metrics_written)" "1"
  assert_eq "quiet exit" "$(pvalue "$f" quiet.exit)" "0"

  # The control side, so the quiet readings above are not 0 for a reason that
  # has nothing to do with the switch.
  assert_eq "normal window_created" "$(pvalue "$f" normal.window_created)" "1"
  assert_eq "normal window_visible" "$(pvalue "$f" normal.window_visible)" "1"
  assert_eq "normal drawn" "$(pvalue "$f" normal.drawn)" "1"
  assert_eq "normal capture_written" "$(pvalue "$f" normal.capture_written)" "1"
  assert_eq "normal exit" "$(pvalue "$f" normal.exit)" "0"

  # The frame, the metrics row and the trace, all three held to equality: this
  # is the pair where the tick is the same by construction.
  assert_eq "$tag bmp_identical" "$(pvalue "$f" bmp_identical)" "1"
  assert_eq "bmp_bytes == the bytes on disk" "$(pvalue "$f" bmp_bytes)" "$(stat -c%s "$pq.bmp" 2>/dev/null || echo -1)"
  assert_eq "$tag metrics tick equal" "$(pvalue "$f" metrics_last_tick_normal)" "$(pvalue "$f" metrics_last_tick_quiet)"
  assert_eq "$tag metrics rendered_frames equal" "$(pvalue "$f" metrics_last_frames_normal)" "$(pvalue "$f" metrics_last_frames_quiet)"
  assert_eq "$tag metrics_equal_count" "$(pvalue "$f" metrics_equal_count)" "2"
  assert_ge "$tag trace_lines_compared" "$(pvalue "$f" trace_lines_compared)" 50
  assert_eq "$tag trace_render_identical" "$(pvalue "$f" trace_render_identical)" "1"
  assert_eq "$tag trace_mismatch_line" "$(pvalue "$f" trace_mismatch_line)" "-1"
  assert_eq "$tag probe checks" "$(pvalue "$f" checks)" "$PINNED_CHECKS"
  assert_eq "$tag probe failed" "$(pvalue "$f" failed)" "0"
  assert_eq "$tag probe status" "$(pvalue "$f" status)" "pass"
}

run_live_pair() { # run_live_pair <prefix-base>
  local base="$1"
  local ln="$OUT/$base-normal" lq="$OUT/$base-quiet"
  local ln_rc lq_rc
  ln_rc=$(run_one "$ln" --capture-tick "$LIVE_TICK" --demo "$BAGEL")
  lq_rc=$(run_one "$lq" --hidden-window --audio-disabled \
    --capture-tick "$LIVE_TICK" --demo "$BAGEL")
  report_run "live normal" "$ln" "$ln_rc"
  report_run "live quiet " "$lq" "$lq_rc"
  "$PROBE" --mode live --capture-tick "$LIVE_TICK" \
    --normal-trace "$ln.trace" --quiet-trace "$lq.trace" \
    --normal-metrics "$ln.csv" --quiet-metrics "$lq.csv" \
    --normal-bmp "$ln.bmp" --quiet-bmp "$lq.bmp" > "$OUT/$base-probe.txt" 2>&1
  local rc=$?
  echo "$rc" > "$OUT/$base-probe.rc"
  cat "$OUT/$base-probe.txt"
  assert_eq "live normal run rc" "$ln_rc" "0"
  assert_eq "live quiet run rc" "$lq_rc" "0"
  apply_live_assertions "$OUT/$base-probe.txt" "$rc" "$ln" "$lq" "$base"
}

apply_live_assertions() { # apply_live_assertions <probe-out> <probe-rc> <normal-prefix> <quiet-prefix> <label>
  local f="$1" prc="$2" pn="$3" pq="$4" tag="${5:-live}"
  assert_eq "$tag probe exit" "$prc" "0"
  assert_eq "$tag probe mode" "$(pvalue "$f" mode)" "live"
  assert_eq "$tag capture_tick" "$(pvalue "$f" capture_tick)" "$LIVE_TICK"

  # The quiet side, same contract as the pinned pair, but reached with the
  # scheduler running and events to account for.
  assert_eq "quiet window_created" "$(pvalue "$f" quiet.window_created)" "1"
  assert_eq "quiet window_visible" "$(pvalue "$f" quiet.window_visible)" "0"
  assert_eq "quiet audio_mode" "$(pvalue "$f" quiet.audio_mode)" "disabled"
  assert_eq "quiet audio_device_opened" "$(pvalue "$f" quiet.audio_device_opened)" "0"
  assert_eq "quiet events_played" "$(pvalue "$f" quiet.events_played)" "0"
  assert_eq "quiet capture_written" "$(pvalue "$f" quiet.capture_written)" "1"
  assert_eq "quiet metrics_written" "$(pvalue "$f" quiet.metrics_written)" "1"
  assert_eq "quiet exit" "$(pvalue "$f" quiet.exit)" "0"

  # The control side has to have done the work, or the quiet readings are
  # vacuous: a device was opened, events were seen, events were played.
  assert_ge "normal audio_device_opened" "$(pvalue "$f" normal.audio_device_opened)" 1
  assert_ge "normal events_seen" "$(pvalue "$f" normal.events_seen)" 1
  assert_ge "normal events_played" "$(pvalue "$f" normal.events_played)" 1
  assert_ge "quiet events_seen" "$(pvalue "$f" quiet.events_seen)" 1

  # The accounting, tied across the two sides. Same timeline in, same seen,
  # same missing; the only difference is the last step, and `skipped` is the
  # answer that names it. Each side also closes on its own.
  local n_seen q_seen n_played q_played n_skipped q_skipped n_missing q_missing
  n_seen=$(pvalue "$f" normal.events_seen); q_seen=$(pvalue "$f" quiet.events_seen)
  n_played=$(pvalue "$f" normal.events_played); q_played=$(pvalue "$f" quiet.events_played)
  n_skipped=$(pvalue "$f" normal.events_skipped); q_skipped=$(pvalue "$f" quiet.events_skipped)
  n_missing=$(pvalue "$f" normal.events_missing); q_missing=$(pvalue "$f" quiet.events_missing)
  assert_eq "events_seen normal == quiet" "$q_seen" "$n_seen"
  assert_eq "events_missing normal == quiet" "$q_missing" "$n_missing"
  assert_eq "quiet events_skipped == normal events_played" "$q_skipped" "$n_played"
  assert_eq "quiet accounting closes (seen = played+skipped+missing)" \
    "$q_seen" "$(( ${q_played:-0} + ${q_skipped:-0} + ${q_missing:-0} ))"
  assert_eq "normal accounting closes (seen = played+skipped+missing)" \
    "$n_seen" "$(( ${n_played:-0} + ${n_skipped:-0} + ${n_missing:-0} ))"

  # The picture, and the fact that both runs really got to the requested tick.
  assert_eq "$tag bmp_identical" "$(pvalue "$f" bmp_identical)" "1"
  assert_eq "bmp_bytes == the bytes on disk" "$(pvalue "$f" bmp_bytes)" "$(stat -c%s "$pq.bmp" 2>/dev/null || echo -1)"
  assert_eq "$tag metrics_reach_count" "$(pvalue "$f" metrics_reach_count)" "2"
  assert_ge "normal metrics last tick" "$(pvalue "$f" metrics_last_tick_normal)" "$LIVE_TICK"
  assert_ge "quiet metrics last tick" "$(pvalue "$f" metrics_last_tick_quiet)" "$LIVE_TICK"
  assert_ge "normal draw tick" "$(pvalue "$f" normal.draw_tick)" "$LIVE_TICK"
  assert_ge "quiet draw tick" "$(pvalue "$f" quiet.draw_tick)" "$LIVE_TICK"
  assert_ge "$tag trace_lines_compared" "$(pvalue "$f" trace_lines_compared)" 50
  assert_eq "$tag trace_render_identical" "$(pvalue "$f" trace_render_identical)" "1"
  assert_eq "$tag probe checks" "$(pvalue "$f" checks)" "$LIVE_CHECKS"
  assert_eq "$tag probe failed" "$(pvalue "$f" failed)" "0"
  assert_eq "$tag probe status" "$(pvalue "$f" status)" "pass"
}

echo "=== 1/2 pinned pair: same demo, same tick (paused), same frame ==="
run_pinned_pair control-pinned

echo
echo "=== 2/2 live pair: the scheduler advances and the device is asked for ==="
run_live_pair control-live

control_fail=$fail
if [ "$MUTATION" -eq 0 ]; then
  echo
  if [ "$fail" -eq 0 ]; then
    echo "QUIET-CAPTURE=PASS"
  else
    echo "QUIET-CAPTURE=FAIL"
  fi
  exit "$fail"
fi

# --- mutation ---------------------------------------------------------------
# Each case reintroduces one defect this feature removed, rebuilds, re-runs the
# pair whose assertions cover it, and requires those assertions to go red. Then
# it restores the source and rebuilds, and the restore is checked by hash.
#
# The source is uncommitted while this runs -- it is the branch under test -- so
# `git checkout --` is not the restore mechanism here: it would throw away the
# feature being measured. The file is copied to the scratch tree first, and the
# restore is a copy back plus a sha256 comparison, which is the same evidence
# `git checkout` would have given (bytes for bytes) without depending on the
# tree's commit state.
echo
echo "=== mutation: 4 cases, each rebuilt and required to go red ==="

MUTABLE=native/src/main.cpp
BACKUP="$OUT/backup-main.cpp"
cp "$MUTABLE" "$BACKUP"
BASELINE_HASH="$(file_hash "$MUTABLE")"
echo "baseline $MUTABLE sha256=$BASELINE_HASH"

# Applies one textual replacement. The source tree is CRLF, so the patterns are
# written with LF and converted before matching. A pattern that is not found is
# a hard error: a mutation that silently did not happen would be reported as
# "the reading did not move", which is the false green this section exists to
# prevent.
patch_in() { # patch_in <file> <find> <replace>
  "$PY" - "$1" "$2" "$3" <<'PYEOF'
import io, sys
path, find, repl = sys.argv[1], sys.argv[2], sys.argv[3]
raw = io.open(path, 'rb').read()
crlf = b'\r\n' in raw
data = raw.decode('utf-8')
if crlf:
    find = find.replace('\n', '\r\n')
    repl = repl.replace('\n', '\r\n')
if find not in data:
    sys.exit('PATTERN NOT FOUND: ' + find[:70].replace('\r', '\\r').replace('\n', '\\n'))
io.open(path, 'wb').write(data.replace(find, repl, 1).encode('utf-8'))
PYEOF
}

restore_source() { cp "$BACKUP" "$MUTABLE"; }

rebuild() { # rebuild <target...>
  local log="$OUT/rebuild.log"
  if bash build-target.sh "$@" > "$log" 2>&1; then
    return 0
  fi
  printf 'MUTATION-BUILD-FAIL build-target.sh %s\n' "$*"
  grep -a -E "error C|fatal error|LNK[0-9]+" "$log" | head -10
  return 1
}

# expected to move away from the control reading, or the case did nothing
expect_moved() { # expect_moved <label> <field> <control-probe> <mutated-probe>
  local label="$1" field="$2" ctl="$3" got_file="$4" want got
  want="$(pvalue "$ctl" "$field")"
  got="$(pvalue "$got_file" "$field")"
  if [ -n "$got" ] && [ -n "$want" ] && [ "$got" != "$want" ]; then
    printf '  MUTATION-RED   %-32s %s: %s -> %s\n' "$label" "$field" "$want" "$got"
    mutation_red=$((mutation_red + 1))
  else
    printf '  MUTATION-GREEN %-32s %s did not move (control=%s got=%s)\n' \
      "$label" "$field" "${want:-<none>}" "${got:-<none>}"
    rc_all=1
  fi
}

# expected NOT to move: prove the mutation did not disturb the other signals
expect_held() { # expect_held <label> <field> <control-probe> <mutated-probe>
  local label="$1" field="$2" ctl="$3" got_file="$4" want got
  want="$(pvalue "$ctl" "$field")"
  got="$(pvalue "$got_file" "$field")"
  if [ -n "$got" ] && [ "$got" = "$want" ]; then
    printf '  MUTATION-HOLD  %-32s %s stays %s\n' "$label" "$field" "$got"
  else
    printf '  MUTATION-BROKE %-32s %s moved: %s -> %s\n' \
      "$label" "$field" "${want:-<none>}" "${got:-<none>}"
    rc_all=1
  fi
}

# Per-case expectations. The control reading each one is held against comes from
# this invocation's own control pair, not from a literal, so "the reading was
# <n> before and is <m> now" is a statement about two runs of the same script.
apply_expectations() { # apply_expectations <name> <control-probe> <mutated-probe>
  case "$1" in
    m1-ignore-hidden-window)
      # The window flag is ignored, so the window is really shown. Note what
      # does NOT move: the `hidden=` field in the trace is printed from the flag,
      # so it still says 1 while the window is visible. The reading that catches
      # this is IsWindowVisible(), not the flag.
      expect_moved "$1" quiet.window_visible "$2" "$3"
      expect_held  "$1" quiet.hidden "$2" "$3"
      expect_held  "$1" quiet.capture_written "$2" "$3"
      expect_held  "$1" quiet.exit "$2" "$3"
      expect_held  "$1" quiet.audio_device_opened "$2" "$3"
      expect_held  "$1" normal.window_visible "$2" "$3"
      ;;
    m2-ignore-audio-disabled)
      # The audio flag is ignored, so the device is opened. Same shape as m1:
      # the trace's `audio_mode=disabled` is printed from the flag and does not
      # move, which is why the reading that catches this is the product's own
      # call counter.
      expect_moved "$1" quiet.audio_device_opened "$2" "$3"
      expect_moved "$1" quiet.events_played "$2" "$3"
      expect_held  "$1" quiet.audio_mode "$2" "$3"
      expect_held  "$1" quiet.window_visible "$2" "$3"
      expect_held  "$1" quiet.capture_written "$2" "$3"
      expect_held  "$1" normal.audio_device_opened "$2" "$3"
      expect_held  "$1" normal.events_seen "$2" "$3"
      ;;
    m3-quiet-never-writes-capture)
      # The quiet run never hands its frame back: no bitmap on disk,
      # capture_written 0, and the program leaves through the renderer's own
      # failure code 16.
      expect_moved "$1" quiet.capture_written "$2" "$3"
      expect_moved "$1" quiet.exit "$2" "$3"
      expect_moved "$1" bmp_identical "$2" "$3"
      expect_held  "$1" quiet.window_visible "$2" "$3"
      expect_held  "$1" quiet.audio_device_opened "$2" "$3"
      expect_held  "$1" normal.capture_written "$2" "$3"
      ;;
    m4-disabled-sink-reports-played)
      # The device-free sink answers `played`. It still opens nothing, so the
      # device counter must not move -- the defect is in the accounting, and the
      # accounting is what has to catch it.
      expect_moved "$1" quiet.events_played "$2" "$3"
      expect_moved "$1" quiet.events_skipped "$2" "$3"
      expect_held  "$1" quiet.audio_device_opened "$2" "$3"
      expect_held  "$1" quiet.window_visible "$2" "$3"
      expect_held  "$1" quiet.capture_written "$2" "$3"
      expect_held  "$1" quiet.events_seen "$2" "$3"
      ;;
    *)
      echo "  MUTATION-NO-EXPECTATIONS $1"; rc_all=1
      ;;
  esac
}

# runs one case end to end: <name> <pair> <find> <replace>
mutation_case() {
  local name="$1" pair="$2" find="$3" repl="$4"
  local base probe_file control_probe
  echo
  echo "--- $name ---"
  if ! patch_in "$MUTABLE" "$find" "$repl"; then
    echo "  MUTATION-PATCH-FAIL the pattern was not found; nothing was measured"
    rc_all=1
    restore_source
    return
  fi
  if ! rebuild tf2_demo_native; then
    echo "  MUTATION-BUILD-FAIL the mutated product does not build"
    rc_all=1
    restore_source
    return
  fi
  if [ "$pair" = "pinned" ]; then
    base="mut-$name-pinned"
    control_probe="$OUT/control-pinned-probe.txt"
    run_pinned_pair "$base" > "$OUT/$base.log" 2>&1
  else
    base="mut-$name-live"
    control_probe="$OUT/control-live-probe.txt"
    run_live_pair "$base" > "$OUT/$base.log" 2>&1
  fi
  probe_file="$OUT/$base-probe.txt"
  local probe_rc
  probe_rc=$(cat "$OUT/$base-probe.rc" 2>/dev/null || echo -1)

  # The gate's own assertions, run for real against the mutated pair. They are
  # expected to fail; the count is the evidence that the *gate* noticed, on top
  # of the per-field expectations below.
  assert_failed=0
  fail=0
  if [ "$pair" = "pinned" ]; then
    apply_pinned_assertions "$probe_file" "$probe_rc" \
      "$OUT/$base-normal" "$OUT/$base-quiet" "$name"
  else
    apply_live_assertions "$probe_file" "$probe_rc" \
      "$OUT/$base-normal" "$OUT/$base-quiet" "$name"
  fi
  local gate_red=$assert_failed

  mutation_red=0
  apply_expectations "$name" "$control_probe" "$probe_file"
  printf '  probe: rc=%s checks=%s failed=%s first_fail=%s\n' \
    "${probe_rc:-<none>}" "$(pvalue "$probe_file" checks)" \
    "$(pvalue "$probe_file" failed)" "$(pvalue "$probe_file" first_fail)"
  printf '  GATE-ASSERTIONS-RED %s failed=%s\n' "$name" "$gate_red"
  printf '  MUTATION-RED-COUNT %s red=%s\n' "$name" "$mutation_red"
  if [ "$gate_red" -lt 1 ]; then
    echo "  MUTATION-GATE-DID-NOT-NOTICE $name"
    rc_all=1
  fi
  if [ "$mutation_red" -lt 1 ]; then
    echo "  MUTATION-CASE-DID-NOT-MOVE $name"
    rc_all=1
  fi
  mutation_total=$((mutation_total + mutation_red))

  restore_source
  local after
  after="$(file_hash "$MUTABLE")"
  if [ "$after" = "$BASELINE_HASH" ]; then
    echo "  RESTORE-BYTE-IDENTICAL=PASS $name sha256=$after"
  else
    echo "  RESTORE-BYTE-IDENTICAL=FAIL $name sha256=$after want=$BASELINE_HASH"
    rc_all=1; restore_ok=0
  fi
  if rebuild tf2_demo_native; then
    echo "  RESTORE-BUILD=PASS $name"
  else
    echo "  RESTORE-BUILD=FAIL $name"
    rc_all=1; restore_ok=0
  fi
}

rc_all=0
mutation_total=0
restore_ok=1

mutation_case m1-ignore-hidden-window pinned '  if (hiddenWindow) {
    // --hidden-window:' '  if (false) {
    // --hidden-window:'

mutation_case m2-ignore-audio-disabled live '  if (audioDisabled) {
    // --audio-disabled:' '  if (false) {
    // --audio-disabled:'

mutation_case m3-quiet-never-writes-capture pinned '        renderer.requestFrameCapture(captureFramePath.wstring());
        captureArmed = false;' '        if (!hiddenWindow) renderer.requestFrameCapture(captureFramePath.wstring());
        captureArmed = false;'

mutation_case m4-disabled-sink-reports-played live '      return tf2::native::AudioEventScheduler::PlaybackOutcome::skipped;' '      return tf2::native::AudioEventScheduler::PlaybackOutcome::played;'

# The source has to be exactly where it started, and it has to build.
echo
if [ "$restore_ok" -eq 1 ] && [ "$(file_hash "$MUTABLE")" = "$BASELINE_HASH" ]; then
  echo "SOURCE-RESTORED=PASS $MUTABLE sha256=$BASELINE_HASH"
else
  echo "SOURCE-RESTORED=FAIL $MUTABLE"
  rc_all=1
fi
echo "CONTROL-READINGS-HOLD=$([ "$control_fail" -eq 0 ] && echo PASS || echo FAIL)"
[ "$control_fail" -ne 0 ] && rc_all=1
echo "MUTATION-RED-COUNT-TOTAL=$mutation_total"

if [ "$rc_all" -eq 0 ] && [ "$mutation_total" -ge 4 ]; then
  echo "QUIET-CAPTURE-MUTATION=PASS"
  exit 0
fi
echo "QUIET-CAPTURE-MUTATION=FAIL"
exit 1
