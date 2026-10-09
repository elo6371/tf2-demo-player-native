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
# A frame not yet due was waited for in 1 ms slices instead of until it was due,
# and each slice re-ran the whole loop body for state that had not moved.
#
# What this step asserts (and what it deliberately does not)
# ---------------------------------------------------------
# The reading is the *wait itself*, not a derived rate. The loop records
# wait_calls and wait_ms_total at the one place it calls
# MsgWaitForMultipleObjectsEx, so mean_wait_ms answers "when the loop did wait,
# how long for" directly:
#
#   * fixed code: the wait is `clamp(ceil(remainingMs), 1, 16)`, so it lands near
#     the frame interval  (measured 8.97 ms at 65 fps)
#   * defective code: the wait is `min(remainingMs, 1)`, so it is 1.00 ms
#
# That gap is a fact about the code path, not about how fast the machine is.
#
# Why the derived rate is NOT the assertion
# -----------------------------------------
# The obvious reading -- main_loop_iterations per rendered frame -- turned out to
# be non-discriminating on this machine, and the reason is worth writing down
# because it is the same class of error this repo keeps finding: with a 120 Hz
# frame target (8.33 ms) and a loop body that itself costs several milliseconds,
# `elapsed` has already passed the target by the time the wait returns, in both
# versions. So both run ~2 iterations per frame and a "loop-per-frame <= 4"
# assertion passes for the defective build too. It was caught in
# `--mutation`, which is exactly what the mutation is for. loop-per-frame is
# printed here as a diagnostic and is deliberately not asserted on.
#
# The other two assertions are independent of the wait path and were already
# discriminating: the state-driven updaters must stay far below the loop count
# when nothing moves, and the metrics throttle must hold.
#
# Usage: bash idle-spin-check.sh [--mutation]
#   --mutation reintroduces the exact defect, rebuilds, and requires
#   mean_wait_ms to go red. It needs a committed tree, since `git checkout` is
#   the restore mechanism, and it rebuilds the binary on the way out -- leaving
#   the mutated exe in place would make the next straight run read the defect.
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

# How long each sample is allowed to run before the program is killed. The
# program has no "run for N seconds" flag, so the script starts it, sleeps, and
# kills it. Rates are computed from the first and last metrics row, so they do
# not depend on how long the load took -- only on the rows flushed while up.
IDLE_SECONDS=12
PLAY_SECONDS=12
# Wall-clock ceiling on the load, so a hung load is diagnosed rather than waited
# out. The program is killed at this point if it never reached the loop.
LOAD_CEILING=120

# run_sample <seconds> <csv> <log> [extra args...] -- start the program, wait for
# its first metrics row, let it run <seconds>, then stop it. Echoes the number of
# seconds spent loading so a slow start is visible in the log.
run_sample() {
  local seconds="$1" csv="$2" log="$3"; shift 3
  rm -f "$csv"
  "$EXE" --tf-root "$TF" --audio-device "$AUDIO_DEVICE" "$@" \
    --metrics-file "$csv" > "$log" 2>&1 &
  local pid=$!
  local waited=0
  while [ ! -s "$csv" ] && [ "$waited" -lt "$LOAD_CEILING" ]; do
    sleep 1; waited=$((waited + 1))
    kill -0 "$pid" 2>/dev/null || break
  done
  sleep "$seconds"
  kill "$pid" 2>/dev/null
  wait "$pid" 2>/dev/null
  echo "$waited"
}

# The delta between the first and last row of a sample, as a single line of
# numbers. Any script reading a counter through this cannot accidentally compare
# two absolute totals taken at different times.
sample_deltas() { # sample_deltas <csv> <counter...>
  "$PY" - "$@" <<'PYEOF'
import csv, sys
path, keys = sys.argv[1], sys.argv[2:]
rows = list(csv.DictReader(open(path)))
if len(rows) < 3:
    print(' '.join(['x'] * len(keys))); raise SystemExit(0)
a, b = rows[0], rows[-1]
dt = float(b['elapsed_seconds']) - float(a['elapsed_seconds'])
out = []
for k in keys:
    out.append(round(int(b[k]) - int(a[k]), 2))
print(' '.join(str(v) for v in out))
PYEOF
}

# Audio: name the device, or the gate beeps on the system default -- the wrong
# one on this machine. See frame-capture-check.sh.
AUDIO_DEVICE=6

echo "=== 1/3 an idle window waits for the frame, not a fixed 1 ms ==="
IDLE_CSV="$OUT/idle.csv"
# No demo and --start-paused: the cheapest scene the program can be in and the
# one with nothing to update, so it is the right place to measure idle behaviour.
LOAD_WAIT=$(run_sample "$IDLE_SECONDS" "$IDLE_CSV" "$OUT/idle.log" --start-paused)
echo "idle sample: ${IDLE_SECONDS}s after ${LOAD_WAIT}s of load"
if [ ! -s "$IDLE_CSV" ]; then
  echo "  FAIL no metrics were written to $IDLE_CSV"; fail=1
else
  read -r D_LOOP D_FRAME D_UI D_TITLE D_MET D_WAITCALLS D_WAITMS <<EOF
$(sample_deltas "$IDLE_CSV" main_loop_iterations rendered_frames ui_update_calls \
  title_update_calls metrics_write_calls wait_calls wait_ms_total)
EOF
  if [ "${D_FRAME:-x}" = "x" ] || [ "${D_FRAME:-0}" -le 0 ]; then
    echo "  FAIL the idle sample has too few rows to measure a rate"; fail=1
  else
    MEAN_WAIT=$(echo "$D_WAITMS $D_WAITCALLS" | awk '{printf "%.2f", ($2 > 0 ? $1 / $2 : -1)}')
    LOOP_PER_FRAME=$(echo "$D_LOOP $D_FRAME" | awk '{printf "%.2f", $1 / $2}')
    WAIT_PER_FRAME=$(echo "$D_WAITCALLS $D_FRAME" | awk '{printf "%.2f", $1 / $2}')
    UI_RATIO=$(echo "$D_UI $D_TITLE $D_LOOP" | awk '{printf "%.4f", ($1 + $2) / $3}')
    echo "loop_per_frame=$LOOP_PER_FRAME (diagnostic)  wait_calls=$D_WAITCALLS  wait_per_frame=$WAIT_PER_FRAME  mean_wait_ms=$MEAN_WAIT"
    echo "ui_update_calls=$D_UI  title_update_calls=$D_TITLE  metrics_write_calls=$D_MET"
    # The wait must have happened (not a busy poll) and must have been sized to
    # the frame gap. The fixed code lands near the 8.33 ms frame target; the
    # defect is pinned at exactly 1 ms, so 4 ms separates them with room.
    if [ "$MEAN_WAIT" != "-1" ] && [ "$(echo "$MEAN_WAIT 4" | awk '{printf "%d", ($1 >= $2)}')" = "1" ]; then
      echo "  OK   mean wait when the loop waited >= 4 ms ($MEAN_WAIT)"
    else
      echo "  FAIL mean wait when the loop waited = ${MEAN_WAIT} ms, expected >= 4 (a fixed 1 ms poll reads 1.00)"; fail=1
    fi
    # Nothing moves in an idle paused window, so neither updater should push more
    # than a handful of times across the whole sample.
    if [ "$(echo "$UI_RATIO 0.1" | awk '{printf "%d", ($1 < $2)}')" = "1" ]; then
      echo "  OK   (ui_update_calls + title_update_calls) / main_loop_iterations = $UI_RATIO < 0.1"
    else
      echo "  FAIL (ui_update_calls + title_update_calls) / main_loop_iterations = $UI_RATIO, expected < 0.1"; fail=1
    fi
    # writeMetrics has its own 1-second throttle; it must not have been defeated.
    if [ "$(echo "$D_MET" | awk '{printf "%d", ($1 <= 18)}')" = "1" ]; then
      echo "  OK   metrics_write_calls over ${IDLE_SECONDS}s <= 18 (about one a second)"
    else
      echo "  FAIL metrics_write_calls over ${IDLE_SECONDS}s = $D_MET, expected <= 18"; fail=1
    fi
  fi
fi

echo
echo "=== 2/3 the counters move when there is something to move for ==="
# Step 1 proves the updaters can stay quiet; it does not prove they still fire.
# A program that never updated anything would also pass it. So a demo is played
# and the same counters must now be non-zero -- the guard against "the fix was to
# stop updating".
SNAKE="D:/TF2_Demo_Player/testdata/demos/bb841c6d379ff7c40d0c8baf99f59d8d_matcha-20260927-1347-cp_snakewater_final1.dem"
PLAY_CSV="$OUT/play.csv"
[ -e "$SNAKE" ] || { echo "FATAL: missing input: $SNAKE"; exit 1; }
PLAY_LOAD_WAIT=$(run_sample "$PLAY_SECONDS" "$PLAY_CSV" "$OUT/play.log" --demo "$SNAKE")
echo "play sample: ${PLAY_SECONDS}s after ${PLAY_LOAD_WAIT}s of load"
if [ ! -s "$PLAY_CSV" ]; then
  echo "  FAIL no metrics were written to $PLAY_CSV"; fail=1
else
  PLAY_LAST=$("$PY" - "$PLAY_CSV" <<'PYEOF'
import csv, sys
rows = list(csv.DictReader(open(sys.argv[1])))
b = rows[-1] if rows else {}
print(int(b.get('tick', 0)), int(b.get('ui_update_calls', 0)), int(b.get('title_update_calls', 0)))
PYEOF
)
  set -- $PLAY_LAST
  LAST_TICK=$1; PLAY_UI=$2; PLAY_TITLE=$3
  echo "played to tick=$LAST_TICK  ui_update_calls=$PLAY_UI  title_update_calls=$PLAY_TITLE"
  if [ "${LAST_TICK:-0}" -gt 0 ]; then
    echo "  OK   playback advanced, so the run was not a frozen idle window"
  else
    echo "  FAIL playback never advanced (tick stayed 0)"; fail=1
  fi
  if [ "${PLAY_UI:-0}" -gt 0 ] && [ "${PLAY_TITLE:-0}" -gt 0 ]; then
    echo "  OK   both updaters fired while playing, so step 1 was not passed by never updating"
  else
    echo "  FAIL an updater never fired while playing -- step 1's quiet idle is suspect"; fail=1
  fi
fi

echo
echo "=== 3/3 the counters are in the metrics stream, not invented here ==="
# The numbers above are only evidence if the program emits them. The header is
# checked literally, because a gate that read a column the CSV does not carry
# would compare empty strings and could pass for the wrong reason.
HEADER=$(head -1 "$IDLE_CSV" 2>/dev/null)
for column in main_loop_iterations ui_update_calls title_update_calls metrics_write_calls wait_calls wait_ms_total; do
  case "$HEADER" in
    *"$column"*) echo "  OK   metrics header carries $column" ;;
    *) echo "  FAIL metrics header is missing $column: $HEADER"; fail=1 ;;
  esac
done

echo
if [ "$MUTATION" -eq 1 ]; then
  # Source-level counterfactual: put the defect back, rebuild, re-measure, and
  # require the mean-wait assertion to fail. The tree must be committed first
  # because the restore is `git checkout --`.
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
  # Restore both the source and the rebuilt binary: leaving the mutated exe would
  # make the next straight run read the defect and fail for the wrong reason.
  restore() {
    git checkout -- native/ >/dev/null 2>&1
    bash build-target.sh tf2_demo_native >/dev/null 2>&1
  }
  trap restore EXIT
  bash build-target.sh tf2_demo_native >/dev/null 2>&1
  [ -x "$EXE" ] || { echo "  FAIL the mutated build produced no $EXE"; exit 1; }
  MUT_CSV="$OUT/idle-mutated.csv"
  MUT_WAIT=$(run_sample "$IDLE_SECONDS" "$MUT_CSV" "$OUT/idle-mutated.log" --start-paused)
  read -r M_LOOP M_FRAME M_UI M_TITLE M_MET M_WAITCALLS M_WAITMS <<EOF
$(sample_deltas "$MUT_CSV" main_loop_iterations rendered_frames ui_update_calls \
  title_update_calls metrics_write_calls wait_calls wait_ms_total)
EOF
  if [ "${M_WAITCALLS:-x}" = "x" ] || [ "${M_WAITCALLS:-0}" -le 0 ]; then
    echo "MUTATION-CAUGHT=FAIL (the mutated run produced no usable sample)"
    exit 1
  fi
  M_MEAN_WAIT=$(echo "$M_WAITMS $M_WAITCALLS" | awk '{printf "%.2f", $1 / $2}')
  echo "mutated sample: ${IDLE_SECONDS}s after ${MUT_WAIT}s of load, mean_wait_ms=$M_MEAN_WAIT (fixed=$MEAN_WAIT)"
  # The mutated build must fail the same >= 4 ms assertion the fixed build passes.
  if [ "$(echo "$M_MEAN_WAIT 4" | awk '{printf "%d", ($1 >= $2)}')" = "0" ]; then
    echo "MUTATION-CAUGHT=PASS (the 1 ms wait reads $M_MEAN_WAIT, under the 4 ms floor)"
    exit 0
  fi
  echo "MUTATION-CAUGHT=FAIL (the defect still read $M_MEAN_WAIT, at or above the 4 ms floor -- the assertion does not discriminate)"
  exit 1
fi

if [ "$fail" -eq 0 ]; then
  echo "IDLE-SPIN=PASS"
else
  echo "IDLE-SPIN=FAIL"
fi
exit "$fail"
