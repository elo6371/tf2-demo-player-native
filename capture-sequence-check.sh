#!/usr/bin/env bash
# capture-sequence-check.sh -- N27 (2026-11-23): the product's frame sequence.
#
# WHY THIS EXISTS
#   Export needs frames that are a FUNCTION OF THE DEMO, not of the machine: the
#   draw cadence is wall clock, so a run that "captures while playing" produces a
#   set of ticks that changes with load. --capture-sequence <dir> <start> <count>
#   reuses the T27 tick hold: the cursor is pinned onto the armed tick, the capture
#   lands on the following draw, and only then is the next tick armed. This gate
#   makes that a reading:
#
#     1. the sequence itself: exactly <count> files, numbered from zero (that is
#        the name mf_export_probe reads), each the pinned frame size, and the
#        traced ticks are consecutive -- 1200, 1201, ... , not "whatever was drawn".
#     2. determinism: the same arguments twice produce byte-identical frames. A
#        sequence that depended on load would differ here.
#     3. the refusals: the sequence may not be mixed with --capture-frame or
#        --capture-tick, and a missing value refuses; each exits 13 rather than
#        silently choosing one of two answers.
#     4. the default path is unchanged: an ordinary --capture-frame run prints no
#        sequence lines at all.
#
# Usage: bash capture-sequence-check.sh
# Exit:  0 = every assertion held; 1 = not.
set -uo pipefail
# W1: the pixel/gate size. An explicit --window-size still wins in the product.
export TF2_NATIVE_WINDOW_SIZE="${TF2_NATIVE_WINDOW_SIZE:-1280x720}"
cd "$(dirname "$0")"
export LC_ALL=C

EXE=native/build-nmake/tf2_demo_native.exe
TF="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
# rc11: its referenced map is installed, so the frames are real geometry.
DEMO="D:/TF2_Demo_Player/testdata/demos/dfbce608c80a4d72a390c70c597f0377_match-20261101-1959-koth_bagel_rc11.dem"
OUT=.scratch/capture-sequence
START_TICK=1200
COUNT=8
FRAME_BYTES=2582406
RUN_TIMEOUT=420

fail=0
mkdir -p "$OUT"
rm -rf "$OUT"/a "$OUT"/b
mkdir -p "$OUT"/a "$OUT"/b

win() { cygpath -m "$1"; }

assert_eq() { # <label> <actual> <expected>
  if [ "${2:-<none>}" = "$3" ]; then
    echo "  OK   $1 = $3"
  else
    echo "  FAIL $1 = ${2:-<none>}, expected $3"; fail=1
  fi
}

assert_num_gt() { # <label> <actual> <bound>
  if [ -n "${2:-}" ] && [ "${2:-}" -gt "$3" ] 2>/dev/null; then
    echo "  OK   $1 = $2 (> $3)"
  else
    echo "  FAIL $1 = ${2:-<none>}, expected > $3"; fail=1
  fi
}

echo "=== 0/4 build (product) ==="
if bash build-target.sh tf2_demo_native > "$OUT/build.txt" 2>&1; then
  echo "  OK build"
else
  echo "  FAIL build (see $OUT/build.txt)"; tail -30 "$OUT/build.txt"; exit 1
fi
for path in "$EXE" "$TF" "$DEMO"; do
  [ -e "$path" ] || { echo "FATAL: missing input: $path"; exit 1; }
done

: > "$OUT/settings.ini"
: > "$OUT/ui.ini"

# run_seq <name> <dir> [extra args...]
run_seq() {
  local name=$1 dir=$2; shift 2
  local trace_shell="$OUT/$name.trace"
  local trace; trace="$(win "$PWD/$trace_shell")"
  rm -f "$trace_shell"
  TF2_NATIVE_TRACE="$trace" timeout -k 5 "$RUN_TIMEOUT" "$EXE" \
    --tf-root "$TF" --demo "$DEMO" --hidden-window --audio-disabled --playback-speed 8 \
    --capture-sequence "$(win "$PWD/$dir")" "$START_TICK" "$COUNT" \
    --settings-file "$(win "$PWD/$OUT/settings.ini")" \
    --ui-settings-file "$(win "$PWD/$OUT/ui.ini")" \
    "$@" > "$OUT/$name.log" 2>&1
  SEQ_RC=$?
  # The product trace is CRT text mode; normalise to LF before any reading.
  if [ -s "$trace_shell" ]; then
    tr -d "\r" < "$trace_shell" > "$trace_shell.lf" && mv "$trace_shell.lf" "$trace_shell"
  fi
}

echo "=== 1/4 the sequence (run a) ==="
run_seq a "$OUT/a"
assert_eq "sequence rc" "$SEQ_RC" "0"
assert_eq "sequence start line" "$(grep -c "capture: sequence start start=$START_TICK count=$COUNT" "$OUT/a.trace")" "1"
assert_eq "sequence frame lines" "$(grep -c "capture: sequence frame index=" "$OUT/a.trace")" "$COUNT"
assert_eq "sequence done line"  "$(grep -c "capture: sequence done written=$COUNT count=$COUNT ok=1" "$OUT/a.trace")" "1"
assert_eq "frame files" "$(ls "$OUT/a"/frame_*.bmp 2>/dev/null | wc -l)" "$COUNT"
# The frames must be the pinned size, i.e. the same pixels a --capture-frame run
# writes -- not a differently sized window that happens to exist.
size_bad=0
for f in "$OUT"/a/frame_*.bmp; do
  [ "$(wc -c < "$f")" = "$FRAME_BYTES" ] || size_bad=$((size_bad + 1))
done
assert_eq "frames of the pinned size" "$size_bad" "0"
# Consecutive ticks: collect the traced ticks in file order and compare with the
# arithmetic sequence the caller asked for. Read order is draw order here, so a
# skipped capture shows up as a hole instead of being averaged away.
# Both sides are joined and then stripped of the trailing separator: seq/tr
# leave one behind and the strings differ only by that space.
join_ticks() { tr "\n" " " | sed -e "s/ $"//; }
ticks=$(grep -o "capture: sequence frame index=[0-9]* tick=[0-9]*" "$OUT/a.trace" | sed -e 's/.*tick=//' | join_ticks)
expected=$(seq "$START_TICK" $((START_TICK + COUNT - 1)) | join_ticks)
assert_eq "traced ticks" "$ticks" "$expected"

echo "=== 2/4 determinism (run b, same arguments) ==="
run_seq b "$OUT/b"
assert_eq "second sequence rc" "$SEQ_RC" "0"
assert_eq "second sequence frames" "$(ls "$OUT/b"/frame_*.bmp 2>/dev/null | wc -l)" "$COUNT"
# Byte identity per frame: the tick hold makes the captured tick exact, so the
# same arguments must produce the same pixels even on a differently loaded run.
diff_frames=0
for f in "$OUT"/a/frame_*.bmp; do
  base=$(basename "$f")
  ha=$(sha256sum "$f" | cut -d" " -f1)
  hb=$(sha256sum "$OUT/b/$base" 2>/dev/null | cut -d" " -f1)
  [ "$ha" = "$hb" ] || { diff_frames=$((diff_frames + 1)); echo "  ..  $base differs"; }
done
assert_eq "frames identical across runs" "$diff_frames" "0"

echo "=== 3/4 refusals ==="
# run_refusal <label> <extra args...> -- always expects exit 13
run_refusal() {
  local label=$1; shift
  TF2_NATIVE_TRACE="$(win "$PWD/$OUT/refusal.trace")" timeout -k 5 120 "$EXE" \
    --tf-root "$TF" --demo "$DEMO" --hidden-window --audio-disabled --playback-speed 8 \
    --settings-file "$(win "$PWD/$OUT/settings.ini")" \
    --ui-settings-file "$(win "$PWD/$OUT/ui.ini")" \
    "$@" > "$OUT/refusal.log" 2>&1
  assert_eq "$label" "$?" "13"
}
run_refusal "with --capture-frame refuses" \
  --capture-sequence "$(win "$PWD/$OUT/a")" "$START_TICK" "$COUNT" \
  --capture-frame "$(win "$PWD/$OUT/refusal.bmp")"
run_refusal "with --capture-tick refuses" \
  --capture-sequence "$(win "$PWD/$OUT/a")" "$START_TICK" "$COUNT" --capture-tick "$START_TICK"
run_refusal "missing count refuses" \
  --capture-sequence "$(win "$PWD/$OUT/a")" "$START_TICK"
run_refusal "non-numeric count refuses" \
  --capture-sequence "$(win "$PWD/$OUT/a")" "$START_TICK" zero

echo "=== 4/4 the default capture path is unchanged ==="
TF2_NATIVE_TRACE="$(win "$PWD/$OUT/default.trace")" timeout -k 5 300 "$EXE" \
  --tf-root "$TF" --demo "$DEMO" --hidden-window --audio-disabled --playback-speed 8 \
  --capture-tick "$START_TICK" --capture-frame "$(win "$PWD/$OUT/default.bmp")" \
  --settings-file "$(win "$PWD/$OUT/settings.ini")" \
  --ui-settings-file "$(win "$PWD/$OUT/ui.ini")" > "$OUT/default.log" 2>&1
assert_eq "default capture rc" "$?" "0"
assert_num_gt "default frame bytes" "$(wc -c < "$OUT/default.bmp" 2>/dev/null)" "1000000"
assert_eq "sequence lines in a one-shot run" "$(grep -c "capture: sequence" "$OUT/default.trace")" "0"

if [ "$fail" -eq 0 ]; then
  echo "CAPTURE-SEQUENCE=PASS"
else
  echo "CAPTURE-SEQUENCE=FAIL"
fi
exit "$fail"
