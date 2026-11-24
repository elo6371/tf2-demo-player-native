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
#     5. the MP4 itself (N27 step 2): the same BMPs go through
#        native/src/demo_export.cpp -- the tf2_core member the product export
#        switch calls -- and the finished file is then re-read by an INDEPENDENT
#        IMFSourceReader pass (video_export --verify). Frame count, stream count,
#        first/last timestamp, monotonicity, duration, a byte-identical repeat
#        export and the refusals are asserted on the READER numbers, not the
#        writer. A file the writer believes in but no decoder can walk would
#        pass none of this.
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
# 1512, not 1200: in this demo ticks 1200..1500 carry no audible audio (every
# sound their events reference lives in pak01_dir.vpk, which this machine does
# not have), so an audio track cut there would be 23040 bytes of zeros and prove
# nothing. The first non-silent block starts at tick 1512, which makes the
# audio section assert a DECODED PEAK above zero.
START_TICK=1512
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

echo "=== 0/8 build (product) ==="
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

echo "=== 1/8 the sequence (run a) ==="
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

echo "=== 2/8 determinism (run b, same arguments) ==="
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

echo "=== 3/8 refusals ==="
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

echo "=== 4/8 the default capture path is unchanged ==="
TF2_NATIVE_TRACE="$(win "$PWD/$OUT/default.trace")" timeout -k 5 300 "$EXE" \
  --tf-root "$TF" --demo "$DEMO" --hidden-window --audio-disabled --playback-speed 8 \
  --capture-tick "$START_TICK" --capture-frame "$(win "$PWD/$OUT/default.bmp")" \
  --settings-file "$(win "$PWD/$OUT/settings.ini")" \
  --ui-settings-file "$(win "$PWD/$OUT/ui.ini")" > "$OUT/default.log" 2>&1
assert_eq "default capture rc" "$?" "0"
assert_num_gt "default frame bytes" "$(wc -c < "$OUT/default.bmp" 2>/dev/null)" "1000000"
assert_eq "sequence lines in a one-shot run" "$(grep -c "capture: sequence" "$OUT/default.trace")" "0"


echo "=== 5/8 the MP4 export (N27 step 2) ==="
if bash build-target.sh video_export > "$OUT/build-export.txt" 2>&1; then
  echo "  OK build video_export"
else
  echo "  FAIL build video_export (see $OUT/build-export.txt)"; tail -30 "$OUT/build-export.txt"; exit 1
fi
EXPORT_EXE=native/build-nmake/video_export.exe
EXPORT_DIR=$OUT/export
mkdir -p "$EXPORT_DIR"
rm -f "$EXPORT_DIR"/*.mp4 "$EXPORT_DIR"/*.log
# The pinned window is 1264x681 -- an ODD height, which H.264 4:2:0 cannot take.
# No --size is passed: the encoder takes the frame size from the first frame,
# crops one row and must SAY so in its trace (asserted below), instead of the
# size silently changing. An explicit odd size is a refusal (section 7/8).
ENC_W=1264
ENC_H=680
export_encode() { # <out> [extra args...]
  local out=$1; shift
  "$EXPORT_EXE" --frames "$OUT/a" --frame-count "$COUNT" \
    --out "$out" "$@" > "$EXPORT_DIR/encode.log" 2>&1
}
# field <log> <key>: the first "key=value" token in a log.
field() { grep -o "$2=[^ ]*" "$1" | head -1 | cut -d= -f2; }

export_encode "$EXPORT_DIR/first.mp4"
assert_eq "export rc" "$?" "0"
assert_num_gt "mp4 bytes" "$(wc -c < "$EXPORT_DIR/first.mp4" 2>/dev/null)" "1000"
assert_eq "export result" "$(grep -c "video-export: result=ok" "$EXPORT_DIR/encode.log")" "1"
assert_eq "export trace frame lines" "$(grep -c "video-export: frame=" "$EXPORT_DIR/encode.log")" "2"
assert_eq "export video-frames" "$(field "$EXPORT_DIR/encode.log" "video-frames")" "$COUNT"
# mvhd plus, per track, tkhd and mdhd: 2 + 2*2 = 6 fields carry wall-clock time.
assert_eq "export normalised fields" "$(field "$EXPORT_DIR/encode.log" "normalized-fields")" "6"

# The independent read-back. Every assertion below is on a decoder number.
"$EXPORT_EXE" --verify "$EXPORT_DIR/first.mp4" > "$EXPORT_DIR/verify.log" 2>&1
assert_eq "verify rc" "$?" "0"
assert_eq "verify result" "$(grep -c "video-export: verify result=ok" "$EXPORT_DIR/verify.log")" "1"
assert_eq "decoded video streams" "$(field "$EXPORT_DIR/verify.log" "video-streams")" "1"
assert_eq "decoded audio streams (none requested)" "$(field "$EXPORT_DIR/verify.log" "audio-streams")" "0"
assert_eq "decoded video frames" "$(field "$EXPORT_DIR/verify.log" "video-frames")" "$COUNT"
assert_eq "decoded width" "$(field "$EXPORT_DIR/verify.log" "width")" "$ENC_W"
assert_eq "decoded height" "$(field "$EXPORT_DIR/verify.log" "height")" "$ENC_H"
# One demo tick = 150000 x 100 ns = 15 ms and the writer is fed exactly that
# cadence (the sink refuses a non-integral frame rate), so frame n sits at
# n * 150000 -- not "about 60 fps".
assert_eq "first decoded ts" "$(field "$EXPORT_DIR/verify.log" "first-ts")" "0"
# The mp4 muxer quantises the frame duration into its own timescale, so the
# decoder can report the last sample a rounding step off PT(7)*150000. Inside
# one frame is the same cadence; the duration assertion below pins the total.
last_ts=$(field "$EXPORT_DIR/verify.log" "last-ts")
exact_last=$(( (COUNT - 1) * 150000 ))
if [ -n "$last_ts" ] && [ "$last_ts" -ge $((exact_last - 5000)) ] && [ "$last_ts" -le $((exact_last + 5000)) ]; then
  echo "  OK   last decoded ts = $last_ts (within 5000 of $exact_last)"
else
  echo "  FAIL last decoded ts = ${last_ts:-<none>}, expected within 5000 of $exact_last"; fail=1
fi
assert_eq "timestamps monotonic" "$(field "$EXPORT_DIR/verify.log" "monotonic")" "1"
dur_ms=$(field "$EXPORT_DIR/verify.log" "duration-ms")
expected_ms=$(( COUNT * 15 ))
if [ -n "$dur_ms" ] && [ "$dur_ms" -ge $((expected_ms - 3)) ] && [ "$dur_ms" -le $((expected_ms + 3)) ]; then
  echo "  OK   duration-ms = $dur_ms (within 3 of $expected_ms)"
else
  echo "  FAIL duration-ms = ${dur_ms:-<none>}, expected within 3 of $expected_ms"; fail=1
fi
# The crop note is part of the reading: if the encoder had accepted 681 the gate
# would have proven nothing about odd sizes.
assert_eq "odd-height crop note" "$(grep -c "note=source height 681 is odd" "$EXPORT_DIR/encode.log")" "1"

echo "=== 6/8 repeat export is byte-identical ==="
export_encode "$EXPORT_DIR/second.mp4"
assert_eq "second export rc" "$?" "0"
h_first=$(sha256sum "$EXPORT_DIR/first.mp4" | cut -d" " -f1)
h_second=$(sha256sum "$EXPORT_DIR/second.mp4" | cut -d" " -f1)
assert_eq "mp4 sha256 across exports" "$h_second" "$h_first"

echo "=== 7/8 refusals on the export path ==="
# Existing target without --overwrite: refused, and the trace names the code so a
# caller can tell "already exists" from "media foundation unavailable".
"$EXPORT_EXE" --frames "$OUT/a" --frame-count "$COUNT" --size "${ENC_W}x${ENC_H}" \
  --out "$EXPORT_DIR/first.mp4" > "$EXPORT_DIR/refuse.log" 2>&1
assert_eq "existing target rc" "$?" "1"
assert_eq "existing target code" "$(field "$EXPORT_DIR/refuse.log" "code")" "already-exists"
# An odd dimension is a refused argument, not a silently cropped frame.
"$EXPORT_EXE" --frames "$OUT/a" --frame-count "$COUNT" --size 1263x680 \
  --out "$EXPORT_DIR/odd.mp4" > "$EXPORT_DIR/odd.log" 2>&1
assert_eq "odd width rc" "$?" "2"
assert_eq "odd width detail" "$(grep -c "output size must be even" "$EXPORT_DIR/odd.log")" "1"
assert_eq "odd width wrote nothing" "$(ls "$EXPORT_DIR"/odd.mp4 2>/dev/null | wc -l)" "0"
# A missing audio source is refused before the sink opens: nothing is created,
# and no .partial is left behind for a later run to trip over.
"$EXPORT_EXE" --frames "$OUT/a" --frame-count "$COUNT" --out "$EXPORT_DIR/noaudio.mp4" \
  --audio "$EXPORT_DIR/does-not-exist.pcm" --audio-rate 48000 --audio-channels 2 \
  > "$EXPORT_DIR/noaudio.log" 2>&1
assert_eq "missing audio rc" "$?" "1"
assert_eq "missing audio refused" "$(grep -c "video-export: result=fail" "$EXPORT_DIR/noaudio.log")" "1"
assert_eq "missing audio wrote nothing" "$(ls "$EXPORT_DIR"/noaudio.mp4 2>/dev/null | wc -l)" "0"
assert_eq "no partial files left" "$(ls "$EXPORT_DIR"/*.partial 2>/dev/null | wc -l)" "0"

echo "=== 8/8 the audio track (demo mix -> PCM -> AAC -> decoded) ==="
# The product has no 48 kHz mixing stage: AudioPlayer hands each sound to winmm
# at the resource native rate and the DEVICE converts and sums. To get an
# offline PCM stream the export can take, export_audio_mix_probe re-runs the
# product parse/resolve chain and AudioEventScheduler, then sums and resamples
# deterministically to 48 kHz -- exactly 720 frames per demo tick, which is what
# makes tick n sit at sample n*720 on the audio side too.
if bash native/tools/export_audio_mix_probe.sh > "$OUT/build-audio.txt" 2>&1; then
  echo "  OK   build export_audio_mix_probe"
else
  echo "  FAIL build export_audio_mix_probe (see $OUT/build-audio.txt)"
  tail -30 "$OUT/build-audio.txt"; fail=1
fi
AUDIO_EXE=.scratch/build-audio-mix/export_audio_mix_probe.exe
AUDIO_PCM=$EXPORT_DIR/audio.pcm
if [ -x "$AUDIO_EXE" ]; then
  "$AUDIO_EXE" "$DEMO" "$START_TICK" "$COUNT" "$(win "$PWD/$AUDIO_PCM")" \
    > "$EXPORT_DIR/audio.log" 2>&1
  assert_eq "mix probe rc" "$?" "0"
fi
# 720 frames/tick * 2 channels * 2 bytes * COUNT.
assert_eq "mixed PCM bytes" "$(wc -c < "$AUDIO_PCM" 2>/dev/null)" "$(( COUNT * 2880 ))"
# Pinned digest: this is the exact byte stream of the recorded 1512-window run,
# so a resampler or scheduler change shows up here rather than as a diff in the
# encoded file, where two changes could cancel out.
assert_eq "mixed PCM sha256" "$(sha256sum "$AUDIO_PCM" 2>/dev/null | cut -d" " -f1)" \
  "a98c83d365ed453e1b731a6562b50f924f46be0e8f34a3e129555fee715817f7"

"$EXPORT_EXE" --frames "$OUT/a" --frame-count "$COUNT" \
  --out "$EXPORT_DIR/av.mp4" --audio "$(win "$PWD/$AUDIO_PCM")" \
  --audio-rate 48000 --audio-channels 2 > "$EXPORT_DIR/av-encode.log" 2>&1
assert_eq "audio export rc" "$?" "0"
assert_eq "audio export result" "$(grep -c "video-export: result=ok" "$EXPORT_DIR/av-encode.log")" "1"
assert_eq "audio export video-frames" "$(field "$EXPORT_DIR/av-encode.log" "video-frames")" "$COUNT"
assert_eq "audio export audio-frames" "$(field "$EXPORT_DIR/av-encode.log" "audio-frames")" "$(( COUNT * 720 ))"
# Two tracks means two sets of mvhd/tkhd/mdhd timestamps (2 + 2*2 = 6 fields).
# Two tracks: mvhd + 2 * (tkhd + mdhd) = 10 fields.
assert_eq "audio export normalised fields" "$(field "$EXPORT_DIR/av-encode.log" "normalized-fields")" "10"

"$EXPORT_EXE" --verify "$EXPORT_DIR/av.mp4" > "$EXPORT_DIR/av-verify.log" 2>&1
assert_eq "audio verify rc" "$?" "0"
assert_eq "audio verify result" "$(grep -c "video-export: verify result=ok" "$EXPORT_DIR/av-verify.log")" "1"
assert_eq "decoded video streams (with audio)" "$(field "$EXPORT_DIR/av-verify.log" "video-streams")" "1"
assert_eq "decoded audio streams" "$(field "$EXPORT_DIR/av-verify.log" "audio-streams")" "1"
assert_eq "decoded video frames (with audio)" "$(field "$EXPORT_DIR/av-verify.log" "video-frames")" "$COUNT"
assert_eq "decoded audio rate" "$(field "$EXPORT_DIR/av-verify.log" "audio-rate")" "48000"
assert_eq "decoded audio channels" "$(field "$EXPORT_DIR/av-verify.log" "audio-channels")" "2"
# AAC carries 1024-sample frames, so 5760 input frames come back as 6144: the
# track is real and its length is the padded length, not the input length.
assert_eq "decoded audio frames" "$(field "$EXPORT_DIR/av-verify.log" "audio-frames")" "6144"
# The point of starting at 1512: the decoded samples must be non-zero. A silent
# track passes every count above, so this is the assertion that proves the mix
# reached the muxer.
assert_num_gt "decoded audio peak" "$(field "$EXPORT_DIR/av-verify.log" "audio-peak")" "0"
# AAC priming/padding makes the container a few ms longer than COUNT*15; the
# video side keeps the exact tick cadence asserted in 5/8.
av_dur=$(field "$EXPORT_DIR/av-verify.log" "duration-ms")
if [ -n "$av_dur" ] && [ "$av_dur" -ge $(( expected_ms - 3 )) ] && [ "$av_dur" -le $(( expected_ms + 20 )) ]; then
  echo "  OK   a/v duration-ms = $av_dur (within -3/+20 of $expected_ms)"
else
  echo "  FAIL a/v duration-ms = ${av_dur:-<none>}, expected $(( expected_ms - 3 ))..$(( expected_ms + 20 ))"; fail=1
fi
# Same inputs must give the same container, audio included.
"$EXPORT_EXE" --frames "$OUT/a" --frame-count "$COUNT" \
  --out "$EXPORT_DIR/av-second.mp4" --audio "$(win "$PWD/$AUDIO_PCM")" \
  --audio-rate 48000 --audio-channels 2 > "$EXPORT_DIR/av-encode-2.log" 2>&1
assert_eq "second audio export rc" "$?" "0"
assert_eq "a/v mp4 sha256 across exports" \
  "$(sha256sum "$EXPORT_DIR/av-second.mp4" | cut -d" " -f1)" \
  "$(sha256sum "$EXPORT_DIR/av.mp4" | cut -d" " -f1)"
if [ "$fail" -eq 0 ]; then
  echo "CAPTURE-SEQUENCE=PASS"
else
  echo "CAPTURE-SEQUENCE=FAIL"
fi
exit "$fail"
