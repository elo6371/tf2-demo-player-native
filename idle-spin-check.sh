#!/usr/bin/env bash
# idle-spin-check.sh -- the main loop must sleep until the next frame is due,
# not poll once a millisecond and re-run the whole body each time.
#
# Why this exists
# ---------------
# Every step before this one reads what the program *produced*: counters, decoded
# values, captured pixels, resolved resources. None of them could see the loop
# running hot, because a hot loop still produces the same output -- it just
# produces it while burning a core between frames. The concrete defect was one
# line:
#
#     const DWORD waitMs = static_cast<DWORD>(std::max(0.0, std::min(remainingMs, 1.0)));
#
# A pending frame 15 ms out was therefore waited for in fifteen 1-ms slices, and
# each slice ran the whole loop body: increment the tick, copy entity state,
# rebuild the title, push UI controls, poll process memory. At an idle 60-100 fps
# that is roughly 1000 iterations a second doing nothing, and the three updaters
# were called just as often for state that had not moved.
#
# What this step asserts (and what it deliberately does not)
# ---------------------------------------------------------
# It asserts the loop's *rate*, the work-per-frame ratio, and that the two
# state-driven updaters only run when their state actually moves. It does not
# assert a wall-clock budget: this is a shared machine, and a timing assertion
# tight enough to be meaningful would be flaky, while one loose enough to be
# stable would not catch a regression. The counters are ratios for that reason --
# loop-per-frame and update-per-loop -- not absolute times.
#
# The four readings:
#   * main_loop_iterations per rendered frame at idle, which the old code held
#     near 10 (1000 loops / 100 fps) and the fix holds near 2
#   * ui_update_calls and title_update_calls, which must be far below
#     main_loop_iterations when nothing is moving
#   * metrics_write_calls, which must stay near one per second (its own throttle)
#
# What the mutation does NOT rely on
# ----------------------------------
# The assertion above is stated as a ratio with a ceiling of 4 per frame, which
# is loose on purpose (measured 2.0, old behaviour ~10). A gate whose margin is
# so wide must show the *defect* breaks it, not a tighter number: `--mutation`
# therefore patches main.cpp back to `min(remainingMs, 1.0)`, rebuilds, and
# requires the same ceiling of 4 to go red. If it does not, the reading is
# tracking the threshold rather than the loop.
#
# Usage: bash idle-spin-check.sh [--mutation]
#   --mutation reintroduces the exact defect (waits at most 1 ms instead of
#   until the next frame), rebuilds, and requires the loop-per-frame assertion
#   to go red -- the assertion is then restored and the binary rebuilt. This is
#   a source-level counterfactual rather than a tightened expectation, because
#   only the real defect can show the reading tracks the behaviour and not the
#   threshold. It needs a committed tree, since `git checkout` restores.
# Exit: 0 = every assertion held (or, with --mutation, went red).
set -uo pipefail
cd "$(dirname "$0")"

EXE=native/build-nmake/tf2_demo_native.exe
MAINSRC=native/src/main.cpp
TF="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
PY="C:/Users/Administrator/.workbuddy-ai/binaries/python/versions/3.13.12/python.exe"
OUT="${IDLE_SPIN_OUT:-evidence/idle-spin}"
mkdir -p "$OUT"

MUTATION=0
[ "${1:-}" = "--mutation" ] && MUTATION=1

fail=0
# Missing input is a failure, not a skip. A gate that quietly SKIPs when the
# binary is absent cannot report the regression it exists to catch.
for path in "$EXE" "$TF" "$PY"; do
  [ -e "$path" ] || { echo "FATAL: missing input: $path"; exit 1; }
done

assert_le() { # assert_le <label> <actual> <ceiling>; floats, so via awk
  if [ "${2:-x}" != "x" ] && [ "$(echo "$2 $3" | awk '{printf "%d", ($1 <= $2)}')" = "1" ]; then
    echo "  OK   $1 = $2 (<= $3)"
  else
    echo "  FAIL $1 = ${2:-<none>}, expected <= $3"; fail=1
  fi
}

# Audio: name the device, or the gate beeps on the system default -- the wrong
# one on this machine. See frame-capture-check.sh for the same reasoning.
AUDIO_DEVICE=6
# How long each sample is allowed to run before the program is killed. The
# program has no "run for N seconds" flag, so the script starts it, sleeps, and
# kills it. The rate is computed from the first and last metrics row, so the
# numbers do not depend on how long the load took -- only on the rows that were
# flushed while the program was up.
IDLE_SECONDS=12
PLAY_SECONDS=12
# Wall-clock ceiling above the sample, so a hung load is diagnosed rather than
# waited out. The program is killed at LOAD_CEILING if it never got to the loop.
LOAD_CEILING=120

# run_sample <seconds> <csv> <log> [extra args...] -- start the program, let it
# run for <seconds>, then stop it. rc is reported so a crash is visible rather
# than silently producing a short sample.
run_sample() {
  local seconds="$1" csv="$2" log="$3"; shift 3
  rm -f "$csv"
  "$EXE" --tf-root "$TF" --audio-device "$AUDIO_DEVICE" "$@" \
    --metrics-file "$csv" > "$log" 2>&1 &
  local pid=$!
  local waited=0
  # Wait for the loop to come up: metrics rows only appear once the program is
  # past loading. Bounded by LOAD_CEILING so a load that never finishes fails
  # fast instead of sleeping out the whole budget.
  while [ ! -s "$csv" ] && [ "$waited" -lt "$LOAD_CEILING" ]; do
    sleep 1; waited=$((waited + 1))
    kill -0 "$pid" 2>/dev/null || break
  done
  sleep "$seconds"
  kill "$pid" 2>/dev/null
  wait "$pid" 2>/dev/null
  echo "$waited"
}

echo "=== 1/3 an idle window does not spin ==="
IDLE_CSV="$OUT/idle.csv"
# No demo and --start-paused: the cheapest scene the program can be in and the
# one with nothing to update, so it is the right place to measure idle behaviour.
LOAD_WAIT=$(run_sample "$IDLE_SECONDS" "$IDLE_CSV" "$OUT/idle.log" --start-paused)
echo "idle sample: ${IDLE_SECONDS}s after ${LOAD_WAIT}s of load"
if [ ! -s "$IDLE_CSV" ]; then
  echo "  FAIL no metrics were written to $IDLE_CSV"; fail=1
else
  read -r LOOP_PER_FRAME LOOP_PER_SEC UPDATE_PER_LOOP METRICS_PER_SEC ROWS <<EOF
$("$PY" - "$IDLE_CSV" <<'PYEOF'
import csv, sys
rows = list(csv.DictReader(open(sys.argv[1])))
if len(rows) < 3:
    print("x x x x", len(rows)); raise SystemExit(0)
a, b = rows[0], rows[-1]
dt = float(b['elapsed_seconds']) - float(a['elapsed_seconds'])
dloop = int(b['main_loop_iterations']) - int(a['main_loop_iterations'])
dframe = int(b['rendered_frames']) - int(a['rendered_frames'])
dui = int(b['ui_update_calls']) - int(a['ui_update_calls'])
dtit = int(b['title_update_calls']) - int(a['title_update_calls'])
dmet = int(b['metrics_write_calls']) - int(a['metrics_write_calls'])
if dt <= 0 or dframe <= 0:
    print("x x x x", len(rows)); raise SystemExit(0)
print(round(dloop / dframe, 2),
      round(dloop / dt, 1),
      round((dui + dtit) / dloop, 3),
      round(dmet / dt, 2),
      len(rows))
PYEOF
)
EOF
  echo "rows=$ROWS  loop_per_frame=$LOOP_PER_FRAME  loop_per_sec=$LOOP_PER_SEC  (ui+title)_per_loop=$UPDATE_PER_LOOP  metrics_per_sec=$METRICS_PER_SEC"
  # The old code waited at most 1 ms, so at ~100 fps it ran ~10 iterations per
  # frame. The fix waits until the frame is due, so an idle loop should be a
  # small multiple of the frame count -- 2 measured, 4 gives the machine room.
  LOOP_CEILING=4
  assert_le "main_loop_iterations per rendered frame" "$LOOP_PER_FRAME" "$LOOP_CEILING"
  # Nothing moves in an idle paused window, so neither updater should push more
  # than a handful of times across the whole sample -- ratio well under 0.1.
  UPDATE_RATIO=$(echo "$UPDATE_PER_LOOP" | awk '{printf "%d", ($1 < 0.1)}')
  if [ "$UPDATE_RATIO" = "1" ]; then
    echo "  OK   (ui_update_calls + title_update_calls) / main_loop_iterations < 0.1"
  else
    echo "  FAIL (ui_update_calls + title_update_calls) / main_loop_iterations = ${UPDATE_PER_LOOP}, expected < 0.1"; fail=1
  fi
  # writeMetrics has its own 1-second throttle; it must not have been defeated.
  METRICS_CEILING=$(echo "$METRICS_PER_SEC" | awk '{printf "%d", ($1 <= 1.5)}')
  if [ "$METRICS_CEILING" = "1" ]; then
    echo "  OK   metrics_write_calls per second <= 1.5"
  else
    echo "  FAIL metrics_write_calls per second = ${METRICS_PER_SEC}, expected <= 1.5"; fail=1
  fi
fi

echo
echo "=== 2/3 the counters move when there is something to move for ==="
# The idle case proves the updaters can stay quiet; it does not prove they still
# fire. A program that never updated anything would also pass step 1. So a demo
# is played for a few seconds and the same counters must now be non-zero -- the
# guard against "the fix was to stop updating".
SNAKE="D:/TF2_Demo_Player/testdata/demos/bb841c6d379ff7c40d0c8baf99f59d8d_matcha-20260927-1347-cp_snakewater_final1.dem"
PLAY_CSV="$OUT/play.csv"
PLAY_TICKS=0
if [ -e "$SNAKE" ]; then
  PLAY_LOAD_WAIT=$(run_sample "$PLAY_SECONDS" "$PLAY_CSV" "$OUT/play.log" --demo "$SNAKE")
  echo "play sample: ${PLAY_SECONDS}s after ${PLAY_LOAD_WAIT}s of load"
  if [ ! -s "$PLAY_CSV" ]; then
    echo "  FAIL no metrics were written to $PLAY_CSV"; fail=1
  else
    PLAY_TICKS=$("$PY" - "$PLAY_CSV" <<'PYEOF'
import csv, sys
rows = list(csv.DictReader(open(sys.argv[1])))
if not rows:
    print(0); raise SystemExit(0)
# Read the last row rather than a delta: any non-zero total shows the updaters
# ran, and the tick column shows playback actually advanced.
b = rows[-1]
print(int(b['ui_update_calls']) + int(b['title_update_calls']))
PYEOF
)
    LAST_TICK=$("$PY" - "$PLAY_CSV" <<'PYEOF'
import csv, sys
rows = list(csv.DictReader(open(sys.argv[1])))
print(int(rows[-1]['tick']) if rows else 0)
PYEOF
)
    echo "played to tick=$LAST_TICK  (ui_update_calls + title_update_calls) total=$PLAY_TICKS"
    if [ "${LAST_TICK:-0}" -gt 0 ]; then
      echo "  OK   playback advanced, so the run was not a frozen idle window"
    else
      echo "  FAIL playback never advanced (tick stayed 0)"; fail=1
    fi
    if [ "${PLAY_TICKS:-0}" -gt 0 ]; then
      echo "  OK   the updaters fired while playing, so step 1 was not passed by never updating"
    else
      echo "  FAIL the updaters never fired while playing -- step 1's quiet idle is suspect"; fail=1
    fi
  fi
else
  echo "FATAL: missing input: $SNAKE"; exit 1
fi

echo
echo "=== 3/3 the counters are in the metrics stream, not invented here ==="
# The numbers above are only evidence if the program emits them. The header is
# checked literally, because a gate that read a column the CSV does not carry
# would compare empty strings and could pass for the wrong reason.
HEADER=$(head -1 "$IDLE_CSV" 2>/dev/null)
for column in main_loop_iterations ui_update_calls title_update_calls metrics_write_calls; do
  case "$HEADER" in
    *"$column"*) echo "  OK   metrics header carries $column" ;;
    *) echo "  FAIL metrics header is missing $column: $HEADER"; fail=1 ;;
  esac
done

echo
if [ "$MUTATION" -eq 1 ]; then
  # Source-level counterfactual: put the defect back, rebuild, re-measure, and
  # require the assertion to fail. The tree must be committed first because the
  # restore is `git checkout --`, which would otherwise discard the fix.
  if ! git diff --quiet -- native/; then
    echo "refusing to run --mutation: native/ has uncommitted changes" >&2
    exit 2
  fi
  echo "=== mutation: main.cpp waits at most 1 ms again ==="
  "$PY" - "$MAINSRC" <<'PYEOF'
import io, sys
path = sys.argv[1]
raw = io.open(path, 'rb').read()
crlf = b'\r\n' in raw
data = raw.decode('utf-8')
find = "const double waitMs = std::clamp(std::ceil(remainingMs), 1.0, 16.0);"
repl = "const double waitMs = std::max(0.0, std::min(remainingMs, 1.0));"
if crlf:
    find = find.replace('\n', '\r\n'); repl = repl.replace('\n', '\r\n')
if find not in data:
    sys.exit('PATTERN NOT FOUND: the wait clamp is not where this script expects')
io.open(path, 'wb').write(data.replace(find, repl, 1).encode('utf-8'))
PYEOF
  # The restore must put both the source *and* the binary back: leaving the
  # mutated exe in place would make the next non-mutation run read the defect and
  # fail for a reason that has nothing to do with the code under test.
  restore() {
    git checkout -- native/ >/dev/null 2>&1
    bash build-target.sh tf2_demo_native >/dev/null 2>&1
  }
  trap restore EXIT
  bash build-target.sh tf2_demo_native >/dev/null 2>&1
  if [ ! -x "$EXE" ]; then
    echo "  FAIL the mutated build produced no $EXE"
    exit 1
  fi
  MUT_CSV="$OUT/idle-mutated.csv"
  MUT_WAIT=$(run_sample "$IDLE_SECONDS" "$MUT_CSV" "$OUT/idle-mutated.log" --start-paused)
  MUT_LOOP_PER_FRAME=$("$PY" - "$MUT_CSV" <<'PYEOF'
import csv, sys
rows = list(csv.DictReader(open(sys.argv[1])))
if len(rows) < 3:
    print("x"); raise SystemExit(0)
a, b = rows[0], rows[-1]
dt = float(b['elapsed_seconds']) - float(a['elapsed_seconds'])
dloop = int(b['main_loop_iterations']) - int(a['main_loop_iterations'])
dframe = int(b['rendered_frames']) - int(a['rendered_frames'])
print(round(dloop / dframe, 2) if dt > 0 and dframe > 0 else "x")
PYEOF
)
  echo "mutated sample: ${IDLE_SECONDS}s after ${MUT_WAIT}s of load, loop_per_frame=$MUT_LOOP_PER_FRAME (fixed=${LOOP_PER_FRAME})"
  # The tree is restored by the EXIT trap whether this passes or fails.
  if [ "${MUT_LOOP_PER_FRAME:-x}" = "x" ]; then
    echo "MUTATION-CAUGHT=FAIL (the mutated run produced no usable sample)"
    exit 1
  fi
  if [ "$(echo "$MUT_LOOP_PER_FRAME 4" | awk '{printf "%d", ($1 > $2)}')" = "1" ]; then
    echo "MUTATION-CAUGHT=PASS (the defect pushes loop-per-frame to ${MUT_LOOP_PER_FRAME}, above the 4 ceiling)"
    exit 0
  fi
  echo "MUTATION-CAUGHT=FAIL (the defect left loop-per-frame at ${MUT_LOOP_PER_FRAME}, still under 4 -- the assertion does not discriminate)"
  exit 1
fi

if [ "$fail" -eq 0 ]; then
  echo "IDLE-SPIN=PASS"
else
  echo "IDLE-SPIN=FAIL"
fi
exit "$fail"
