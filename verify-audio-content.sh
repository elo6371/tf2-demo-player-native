#!/usr/bin/env bash
# verify-audio-content.sh -- T31: the focused forward gate for MP3 *content*.
#
# WHY THIS EXISTS
#   verify-audio.sh proves the audio subsystem's identities and that its probes
#   stay silent. What it cannot prove is that the MP3 chain reaches the output
#   path for content a recording actually schedules: its MP3 readings come from
#   a synthetic index table (sections 2c/2d in audio_scheduler_probe), and its
#   player run uses the pov recording, which schedules no world-kind MP3 at all.
#   T7 found and fixed the decoder gap; this gate is what watches the fixed chain
#   end to end on the one pinned recording that exercises it.
#
# WHAT IT ASSERTS
#   2/5  the content map of the pinned cp_well recording: exactly two MP3 events
#        are *scheduled* (world kind, resolved, with a named decoder status and
#        duration) while the same recording's MP3 voice clips stay filtered.
#        Every field of both scheduled rows is pinned: tick, source-table index,
#        normalized path, status, duration, source table and decoder detail.
#   3/5  a product run under the null sink over the same tick window 5/5 uses:
#        two "audio: mp3" per-event lines, both "skipped", and the four MP3
#        counters reading seen=2 played=0 skipped=2 failed=0. The per-event lines
#        are joined to 2/5 by tick and sound index, so what the run saw is proven
#        to be the events the map named, not two arbitrary MP3s.
#   4/5  the negative fixtures: MP3 bytes with no frame, a stream that decodes to
#        zero PCM, an MPEG Layer II stream, and the no-codec mapping. Each is a
#        *named* refusal, and the refusals are shown to be counted by the
#        scheduler and never to reach the sink (seen=5, played=2, missing=3,
#        sinkCalls=2).
#   5/5  --device only: the same window and the same two events on real device 6,
#        where both lines must read "played" and every device open must have
#        succeeded. Comparing both runs' mp3 lines with the outcome stripped is
#        the assertion that the window and the events are the same in both.
#
# WHAT IT DOES NOT PROVE
#   * that a human heard anything: only 5/5 opens a device and this file records
#     counters, not listening;
#   * music / temp_entity / pcf content: no pinned recording schedules one, so
#     those categories are NOT VERIFIED in the report and are not asserted here;
#   * any device other than device 6.
#
# THE NOISE CONTRACT
#   Sections 1-4 open no audio device. The 3/5 player run passes --audio-disabled
#   and its own summary has to read attempts=0 opens_ok=0; the 4/5 probe prints
#   device=0 playbackCalls=0. Both player runs also pass --hidden-window, and the
#   trace's window line has to read hidden=1 window_visible=0, so this gate cannot
#   put a window over the user's work. Section 5/5 is the opt-in audible
#   acceptance: the demo plays through device 6 for the pinned window.
#
# Usage: bash verify-audio-content.sh [--mutation] [--no-build] [--device]
#   --mutation perturbs each captured output and requires the assertions to go
#   red, so the lists are known to discriminate rather than assumed to.
#   --device   also runs the audible device-6 acceptance on the same window.
# Exit:  0 = every assertion held (and, with --mutation, every flip was caught).
set -uo pipefail
cd "$(dirname "$0")"

OUT="${VERIFY_AUDIO_CONTENT_OUT:-evidence/fast/audio-content}"
mkdir -p "$OUT"

MUTATION=0
BUILD=1
DEVICE=0
for arg in "$@"; do
  case "$arg" in
    --mutation) MUTATION=1 ;;
    --no-build) BUILD=0 ;;
    --device) DEVICE=1 ;;
    *) echo "unknown argument: $arg"; exit 2 ;;
  esac
done

B=native/build-nmake
TF_DIR="${TF:-D:/SteamLibrary/steamapps/common/Team Fortress 2/tf}"
INPUTS=evidence/t31-audio-content/content-inputs.txt
# The zero point of the pinned window: the two scheduled MP3 events in the
# pinned recording sit at relative ticks 4650 and 7675, so one run to 7700
# covers both, and the null-sink and device runs use the same number on purpose.
WIN_TICK=7700

# The pinned record of the two scheduled MP3 events (2/5), as
# tick|index|kind|status|seconds|srctable|path. Two rows, ordered by tick.
EXPECTED_MP3_CORE='4650|6615|world|resolved|1.4|soundprecache|sound/ambient_mp3/water/water_splash1.mp3
7675|6616|world|resolved|1.6|soundprecache|sound/ambient_mp3/water/water_splash2.mp3'
# The decoder detail of the same two rows, as path|channels|rate|bits|bytes.
# The leading "decoded '" is what says the bytes were decoded rather than merely
# accepted, and it is stripped by the extractor rather than asserted here: a row
# that read "resolved '" would fail this comparison instead of passing it.
EXPECTED_MP3_DETAIL='sound/ambient_mp3/water/water_splash1.mp3|1|44100|16|122112
sound/ambient_mp3/water/water_splash2.mp3|1|44100|16|140544'
# The same recording's filtered MP3 rows: voice/announcer clips that decode but
# are never played. Pinned as a count so "the filter stopped classifying" and
# "the recording changed" are both visible here.
EXPECTED_FILTERED_MP3_ROWS=190
# The per-event record the product prints, as
# tick|index|name|pcmBytes|seconds|outcome.
EXPECTED_NULL_SEGMENTS='4650|6615|ambient_mp3/water/water_splash1.mp3|122112|1.384|inaudible
7675|6616|ambient_mp3/water/water_splash2.mp3|140544|1.593|skipped'
EXPECTED_DEVICE_SEGMENTS='4650|6615|ambient_mp3/water/water_splash1.mp3|122112|1.384|inaudible
7675|6616|ambient_mp3/water/water_splash2.mp3|140544|1.593|played'

ok=0
bad=0

say_ok() { ok=$((ok + 1)); printf '  OK   %s\n' "$1"; }
say_bad() { bad=$((bad + 1)); printf '  FAIL %s\n' "$1"; }

jval() { # jval <json-file> <key> -> first numeric value or empty
  grep -o "\"$2\":[0-9]*" "$1" | head -1 | cut -d: -f2
}

fld() { # fld <text> <key> -> the token after key=, or empty
  printf '%s' "$1" | grep -o "$2=[^ ]*" | head -1 | cut -d= -f2
}

# ---------------------------------------------------------------- section 1/5
if [ "$BUILD" -eq 1 ]; then
  echo "=== 1/5 incremental build (the targets this gate reads) ==="
  # winmm_sink_probe is in the list because 5/5 enumerates devices with it
  # before it opens device 6: a section that cannot tell "device 6 is absent"
  # from "the enumerator was never built" is the failure this list prevents.
  bash build-target.sh audio_scheduler_probe sound_content_map_probe winmm_sink_probe tf2_demo_native
  rc_build=$?
  echo "build_rc=$rc_build"
  [ "$rc_build" -eq 0 ] || { echo "VERIFY-AUDIO-CONTENT=FAIL (build)"; exit 1; }
else
  echo "=== 1/5 incremental build (skipped: --no-build) ==="
fi

echo
echo "=== 1/5 pinned inputs ==="
DEMO=""
if [ ! -s "$INPUTS" ]; then
  say_bad "inputs: $INPUTS is missing (the pinned recording is the input, not a convenience)"
else
  while read -r want_size want_hash want_root want_name; do
    case "$want_size" in ''|'#'*) continue ;; esac
    want_name=$(printf '%s' "$want_name" | tr -d '\r')
    if [ "$want_root" = "steam" ]; then demo_path="$TF_DIR/demos/$want_name"
    else demo_path="D:/TF2_Demo_Player/testdata/demos/$want_name"; fi
    label="input[$(printf '%s' "$want_name" | cut -c1-28)]"
    if [ ! -f "$demo_path" ]; then
      say_bad "$label (absent: $demo_path)"; continue
    fi
    got_size=$(stat -c %s "$demo_path" 2>/dev/null || echo 0)
    if [ "$got_size" != "$want_size" ]; then
      say_bad "$label (wrong size: $got_size, pinned $want_size)"; continue
    fi
    got_hash=$(sha256sum "$demo_path" | cut -d' ' -f1)
    if [ "$got_hash" != "$want_hash" ]; then
      say_bad "$label (wrong hash: $got_hash)"; continue
    fi
    say_ok "$label (pinned input verified: size $want_size, sha256 $(printf '%s' "$want_hash" | cut -c1-12)...)"
    DEMO="$demo_path"
  done < "$INPUTS"
fi
if [ -z "$DEMO" ]; then
  say_bad "inputs: no usable pinned recording, so nothing below can be read"
  echo
  echo "VERIFY-AUDIO-CONTENT=FAIL (no pinned input)"
  exit 1
fi

# ---------------------------------------------------------------- section 2/5
mp3_core_rows() { # <map> -> tick|index|kind|status|seconds|srctable|path
  awk -F'\t' '!/^#/ && $4 == "resolved" && $7 ~ /\.mp3$/ && ($3 == "weapon" || $3 == "footstep" || $3 == "uber" || $3 == "world") { print $1"|"$2"|"$3"|"$4"|"$5"|"$6"|"$7 }' "$1"
}

mp3_detail_rows() { # <map> -> path|channels|rate|bits|bytes
  awk -F'\t' '!/^#/ && $4 == "resolved" && $7 ~ /\.mp3$/ && ($3 == "weapon" || $3 == "footstep" || $3 == "uber" || $3 == "world") { print $7"|"$9 }' "$1" \
    | sed -E "s/^([^|]+)[|]decoded '[^']*' channels=([0-9]+) rate=([0-9]+) bits=([0-9]+) bytes=([0-9]+)$/\1|\2|\3|\4|\5/"
}

mp3_filtered_count() { # <map> -> count of resolved filtered MP3 rows
  awk -F'\t' '!/^#/ && $4 == "resolved" && $7 ~ /\.mp3$/ && ($3 == "voice" || $3 == "announcer" || $3 == "music")' "$1" | grep -c . || true
}

content_checks() { # content_checks <label> <map> <json>
  local label="$1" map="$2" json="$3"
  local core details filtered count mapLines scheduled
  count=$(mp3_core_rows "$map" | grep -c . || true)
  core=$(mp3_core_rows "$map")
  details=$(mp3_detail_rows "$map")
  filtered=$(mp3_filtered_count "$map")
  mapLines=$(jval "$json" mapLines)
  scheduled=$(jval "$json" scheduled)

  if [ "$count" = "2" ]; then
    say_ok "$label (two MP3 events are scheduled, not just present)"
  else
    say_bad "$label (scheduled MP3 events: $count, pinned 2)"
  fi
  if [ "$core" = "$EXPECTED_MP3_CORE" ]; then
    say_ok "$label (both scheduled rows pinned: tick/index/kind/status/seconds/srctable/path)"
  else
    say_bad "$label (scheduled MP3 rows moved; got: $(printf '%s' "$core" | tr '\n' ';'))"
  fi
  if [ "$details" = "$EXPECTED_MP3_DETAIL" ]; then
    say_ok "$label (both decoded: named channels/rate/bits/bytes match the pinned decode)"
  else
    say_bad "$label (decoder detail moved; got: $(printf '%s' "$details" | tr '\n' ';'))"
  fi
  if [ "$filtered" = "$EXPECTED_FILTERED_MP3_ROWS" ]; then
    say_ok "$label (filtered MP3 rows: $filtered, pinned)"
  else
    say_bad "$label (filtered MP3 rows: $filtered, pinned $EXPECTED_FILTERED_MP3_ROWS)"
  fi
  if [ -n "$mapLines" ] && [ -n "$scheduled" ] && [ "$mapLines" = "$scheduled" ]; then
    say_ok "$label (mapping lines == scheduled == $scheduled)"
  else
    say_bad "$label (mapping lines $mapLines != scheduled $scheduled)"
  fi
}

echo
echo "=== 2/5 the content map on the pinned recording: was an MP3 actually scheduled? ==="
CONTENT_MAP="$OUT/content-map.tsv"
CONTENT_JSON="$OUT/content.json"
"$B/sound_content_map_probe.exe" --demo "$DEMO" --tf-root "$TF_DIR" --map-out "$CONTENT_MAP" > "$CONTENT_JSON" 2>&1
rc_content=$?
if [ "$rc_content" -eq 0 ] && grep -qF '"status":"ok"' "$CONTENT_JSON"; then
  say_ok "content: the map ran (rc=0)"
  content_checks "content" "$CONTENT_MAP" "$CONTENT_JSON"
else
  say_bad "content: the map failed (rc=$rc_content): $(head -2 "$CONTENT_JSON" | tr '\n' ' ')"
fi

# ---------------------------------------------------------------- section 3/5
mp3_segments() { # <trace> -> tick|index|name|pcmBytes|seconds|outcome per event line
  grep -a 'audio: mp3 ' "$1" | sed -e 's/^.*audio: mp3 tick=//' -e 's/ index=/|/' -e 's/ name=/|/' \
    -e 's/ pcmBytes=/|/' -e 's/ rate=[0-9]* channels=[0-9]* bits=[0-9]* seconds=/|/' -e 's/ outcome=/|/'
}

trace_window_hidden() { # <trace> -> "hidden=N window_visible=M" from the window line
  grep -a 'mode: window_created' "$1" | tail -1 | grep -o 'window_visible=[0-9]* hidden=[0-9]*' | tail -1
}

null_checks() { # null_checks <label> <trace> <map>
  local label="$1" trace="$2" map="$3"
  local summary mode_line window_line n segs seen played skipped failed inaud
  mode_line=$(grep -a 'mode: summary' "$trace" | tail -1)
  window_line=$(trace_window_hidden "$trace")
  summary=$(grep -a 'audio: summary' "$trace" | tail -1)
  n=$(grep -ac 'audio: mp3 ' "$trace" || true)
  segs=$(mp3_segments "$trace")
  seen=$(fld "$summary" soundEventsMp3Seen)
  played=$(fld "$summary" soundEventsMp3Played)
  skipped=$(fld "$summary" soundEventsMp3Skipped)
  failed=$(fld "$summary" soundEventsMp3Failed)
  inaud=$(fld "$summary" soundEventsMp3Inaudible)

  if [ "$n" = "2" ]; then
    say_ok "$label (two per-event MP3 records over the window)"
  else
    say_bad "$label (per-event MP3 records: $n, pinned 2)"
  fi
  if [ "$segs" = "$EXPECTED_NULL_SEGMENTS" ]; then
    say_ok "$label (both events pinned: the in-range one is skipped by the null sink, the out-of-range one is named inaudible)"
  else
    say_bad "$label (per-event records moved; got: $(printf '%s' "$segs" | tr '\n' ';'))"
  fi
  if [ -n "$seen" ] && [ -n "$played" ] && [ -n "$skipped" ] && [ -n "$failed" ] \
     && [ -n "$inaud" ] && [ "$seen" = "2" ] && [ "$played" = "0" ] && [ "$skipped" = "1" ] \
     && [ "$failed" = "0" ] && [ "$inaud" = "1" ] \
     && [ "$seen" -eq $((played + skipped + failed + inaud)) ]; then
    say_ok "$label (MP3 accounting: seen=$seen = played=$played + skipped=$skipped + failed=$failed + inaudible=$inaud)"
  else
    say_bad "$label (MP3 accounting: seen=$seen played=$played skipped=$skipped failed=$failed inaudible=$inaud)"
  fi
  # The map join: the run's (tick,index) pairs must be exactly the map's
  # scheduled-MP3 pairs. Without this the two per-event lines could be any two
  # MP3s and every other assertion in this function would still pass.
  local trace_keys map_keys
  trace_keys=$(printf '%s\n' "$segs" | cut -d'|' -f1,2 | sed 's/|/:/')
  map_keys=$(mp3_core_rows "$map" | awk -F'|' '{print $1":"$2}')
  if [ -n "$trace_keys" ] && [ "$trace_keys" = "$map_keys" ]; then
    say_ok "$label (window joined to the map: $(printf '%s' "$trace_keys" | tr '\n' ' '))"
  else
    say_bad "$label (window does not match the map: run [$(printf '%s' "$trace_keys" | tr '\n' ' ')] map [$(printf '%s' "$map_keys" | tr '\n' ' ')])"
  fi
  if printf '%s' "$mode_line" | grep -qF 'audio_mode=disabled' \
     && [ "$(fld "$mode_line" audio_device_open_attempts)" = "0" ] \
     && [ "$(fld "$mode_line" audio_device_opens_ok)" = "0" ] \
     && [ "$(fld "$mode_line" events_played)" = "0" ]; then
    say_ok "$label (null sink: attempts=0 opens_ok=0 played=0 -- it never asked for a device)"
  else
    say_bad "$label (null sink device counters: $mode_line)"
  fi
  if [ "$window_line" = "window_visible=0 hidden=1" ]; then
    say_ok "$label (the window was created hidden and never shown)"
  else
    say_bad "$label (window not hidden: $window_line)"
  fi
  local voiceSkipped
  voiceSkipped=$(fld "$summary" soundEventsSkippedVoice)
  if [ -n "$voiceSkipped" ] && [ "$voiceSkipped" -ge 1 ]; then
    say_ok "$label (filtered kinds skipped in the same window: $voiceSkipped)"
  else
    say_bad "$label (no filtered events skipped: $voiceSkipped)"
  fi
}

echo
echo "=== 3/5 the product under the null sink, on the same window 5/5 uses ==="
NULL_TRACE="$OUT/null-trace.txt"
NULL_BMP="$OUT/null-frame.bmp"
rm -f "$NULL_TRACE"
TF2_NATIVE_TRACE="$NULL_TRACE" timeout 300 "$B/tf2_demo_native.exe" --demo "$DEMO" \
  --hidden-window --audio-disabled --audio-device 6 --capture-tick "$WIN_TICK" \
  --capture-frame "$NULL_BMP" > "$OUT/null-stdout.txt" 2>&1
rc_null=$?
if [ "$rc_null" -eq 124 ]; then
  say_bad "null run: exceeded 300s (a stray process may hold the window; check tasklist for tf2_demo_native.exe)"
elif [ "$rc_null" -ne 0 ] || [ ! -s "$NULL_TRACE" ]; then
  say_bad "null run failed (rc=$rc_null): $(tail -3 "$OUT/null-stdout.txt" | tr '\n' ' ')"
else
  say_ok "null run: trace produced (rc=0)"
  null_checks "null" "$NULL_TRACE" "$CONTENT_MAP"
fi
grep -a 'audio: mp3 ' "$NULL_TRACE" > "$OUT/mp3-events-null.txt" 2>/dev/null || true

# ---------------------------------------------------------------- section 4/5
scheduler_checks() { # scheduler_checks <label> <file>
  local label="$1" file="$2" e
  while IFS= read -r e; do
    case "$e" in ''|'#'*) continue ;; esac
    local needle what
    needle=$(printf '%s' "$e" | cut -d'|' -f1)
    what=$(printf '%s' "$e" | cut -d'|' -f2-)
    if [ -s "$file" ] && grep -qF -- "$needle" "$file"; then
      say_ok "$label: $what"
    else
      say_bad "$label: $what (wanted: $needle)"
    fi
  done <<'EOF'
device=0 playbackCalls=0|the refusal probe opened no device
mp3PlaybackSeen=5|five events reached the playback decision
mp3PlaybackPlayed=2|the playable WAV and the real MP3 both played
mp3PlaybackMissing=3|the three refused MP3s are counted missing, not dropped
mp3PlaybackSinkCalls=2|only the two playable events reached the sink
mp3PlaybackRefused=3|the resolver refused exactly three of them
mp3PlaybackMp3Seen=1|the codec provenance says the playable event came from MP3
mp3PlaybackMp3Played=1|and that event was the one played
mp3PlaybackMp3Failed=0|no decoded MP3 failed the output path
mp3PlaybackRefusalNames=probe_no_frame.mp3:invalid_wav,probe_sync_only.mp3:invalid_wav,probe_layer2.mp3:unsupported_format|each refusal keeps its own name
mp3PlaybackObserved=probe_play_wav.wav:played,probe_play_mp3.mp3:played|the observer saw the two playable events in order
mp3NoFrameStatus=invalid_wav|MP3-shaped bytes with no frame are invalid
mp3SilentStatus=invalid_wav|a stream that decodes to zero PCM is refused
mp3Layer2Status=unsupported_format|a Layer II stream is refused by name
mp3MapNoDecoderStatus=unsupported_format|a machine with no codec reports a capability gap
mutationsCaught=11 mutationsTotal=11|every internal flip toggles a pinned reading
EOF
}

echo
echo "=== 4/5 the negative fixtures: named refusals at the playback boundary ==="
"$B/audio_scheduler_probe.exe" > "$OUT/scheduler.txt" 2>&1
rc_sched=$?
if [ "$rc_sched" -eq 0 ]; then
  say_ok "scheduler: the refusal probe held its contract (rc=0)"
else
  say_bad "scheduler: the refusal probe failed (rc=$rc_sched): $(grep -a 'FAIL' "$OUT/scheduler.txt" | head -2 | tr '\n' ' ')"
fi
scheduler_checks "scheduler" "$OUT/scheduler.txt"

# ---------------------------------------------------------------- section 5/5
device_checks() { # device_checks <label> <device-trace> <null-trace> <map>
  local label="$1" trace="$2" null_trace="$3" map="$4"
  local mode_line summary window_line n segs null_segs eventsSeen eventsPlayed
  local mp3seen mp3played mp3skipped mp3failed mp3inaud
  mode_line=$(grep -a 'mode: summary' "$trace" | tail -1)
  summary=$(grep -a 'audio: summary' "$trace" | tail -1)
  window_line=$(trace_window_hidden "$trace")
  n=$(grep -ac 'audio: mp3 ' "$trace" || true)
  segs=$(mp3_segments "$trace")
  null_segs=$(mp3_segments "$null_trace")
  eventsSeen=$(fld "$mode_line" events_seen)
  eventsPlayed=$(fld "$mode_line" events_played)
  mp3seen=$(fld "$summary" soundEventsMp3Seen)
  mp3played=$(fld "$summary" soundEventsMp3Played)
  mp3skipped=$(fld "$summary" soundEventsMp3Skipped)
  mp3failed=$(fld "$summary" soundEventsMp3Failed)
  mp3inaud=$(fld "$summary" soundEventsMp3Inaudible)

  if [ "$n" = "2" ]; then
    say_ok "$label (two per-event MP3 records on the real device)"
  else
    say_bad "$label (per-event MP3 records: $n, pinned 2)"
  fi
  if [ "$segs" = "$EXPECTED_DEVICE_SEGMENTS" ]; then
    say_ok "$label (the in-range event played on device 6; the out-of-range one is still named inaudible)"
  else
    say_bad "$label (per-event records moved; got: $(printf '%s' "$segs" | tr '\n' ';'))"
  fi
  # The same four-counter identity as the null run, with the one value that must
  # differ: played=1 / skipped=0 instead of played=0 / skipped=1. That difference
  # IS the device-vs-null accounting this task asks for; the events are the same.
  if [ -n "$mp3seen" ] && [ -n "$mp3played" ] && [ -n "$mp3skipped" ] \
     && [ -n "$mp3failed" ] && [ -n "$mp3inaud" ] \
     && [ "$mp3seen" = "2" ] && [ "$mp3played" = "1" ] && [ "$mp3skipped" = "0" ] \
     && [ "$mp3failed" = "0" ] && [ "$mp3inaud" = "1" ] \
     && [ "$mp3seen" -eq $((mp3played + mp3skipped + mp3failed + mp3inaud)) ]; then
    say_ok "$label (MP3 accounting: seen=$mp3seen = played=$mp3played + skipped=$mp3skipped + failed=$mp3failed + inaudible=$mp3inaud)"
  else
    say_bad "$label (MP3 accounting: seen=$mp3seen played=$mp3played skipped=$mp3skipped failed=$mp3failed inaudible=$mp3inaud)"
  fi
  # Same window, same events, different outcome: strip the outcome column and
  # the two runs' records have to be identical. This is what makes "the null
  # sink and the device agree" a claim about the same events rather than about
  # two numbers that happen to be two.
  local segs_no_outcome null_no_outcome
  segs_no_outcome=$(printf '%s\n' "$segs" | sed 's/|[a-z]*$//')
  null_no_outcome=$(printf '%s\n' "$null_segs" | sed 's/|[a-z]*$//')
  if [ -n "$segs_no_outcome" ] && [ "$segs_no_outcome" = "$null_no_outcome" ]; then
    say_ok "$label (same window and events as the null run: only the outcome differs)"
  else
    say_bad "$label (null and device runs saw different events)"
  fi
  local att opens calls
  att=$(fld "$mode_line" audio_device_open_attempts)
  opens=$(fld "$mode_line" audio_device_opens_ok)
  calls=$(fld "$mode_line" audio_device_opened)
  if printf '%s' "$mode_line" | grep -qF 'audio_mode=device' \
     && [ -n "$att" ] && [ "$att" -ge 1 ] && [ -n "$opens" ] && [ "$opens" -eq "$att" ] \
     && [ -n "$eventsPlayed" ] && [ "$eventsPlayed" -ge 1 ] && [ "$eventsPlayed" -le "$opens" ] \
     && [ "$calls" = "$att" ]; then
    say_ok "$label (device 6: attempts=$att opens_ok=$opens events_played=$eventsPlayed, every open succeeded)"
  else
    say_bad "$label (device run counters: $mode_line)"
  fi
  if [ "$window_line" = "window_visible=0 hidden=1" ]; then
    say_ok "$label (the window was created hidden and never shown)"
  else
    say_bad "$label (window not hidden: $window_line)"
  fi
}

DEV_TRACE="$OUT/device-trace.txt"
if [ "$DEVICE" -eq 1 ]; then
  echo
  echo "=== 5/5 device acceptance: the same window on real device 6 (AUDIBLE) ==="
  "$B/winmm_sink_probe.exe" --list-devices > "$OUT/winmm-devices.txt" 2>&1
  DEV_LINE=$(head -1 "$OUT/winmm-devices.txt")
  DEV_COUNT=$(printf '%s' "$DEV_LINE" | grep -o 'device_count=[0-9]*' | cut -d= -f2)
  if [ -z "$DEV_COUNT" ] || [ "$DEV_COUNT" -lt 7 ]; then
    say_bad "device acceptance: device 6 does not exist (device_count=$DEV_COUNT); rerun --list-devices and name the intended device"
  elif [ ! -s "$NULL_TRACE" ]; then
    say_bad "device acceptance: the null-sink trace is missing, so the two runs cannot be compared"
  else
    rm -f "$DEV_TRACE"
    TF2_NATIVE_TRACE="$DEV_TRACE" timeout 300 "$B/tf2_demo_native.exe" --demo "$DEMO" \
      --hidden-window --audio-device 6 --capture-tick "$WIN_TICK" \
      --capture-frame "$OUT/device-frame.bmp" > "$OUT/device-stdout.txt" 2>&1
    rc_dev=$?
    if [ "$rc_dev" -eq 124 ]; then
      say_bad "device 6 run: exceeded 300s (check tasklist for tf2_demo_native.exe)"
    elif [ "$rc_dev" -ne 0 ] || [ ! -s "$DEV_TRACE" ]; then
      say_bad "device 6 run failed (rc=$rc_dev): $(tail -3 "$OUT/device-stdout.txt" | tr '\n' ' ')"
    else
      say_ok "device 6 run: trace produced (rc=0)"
      device_checks "device" "$DEV_TRACE" "$NULL_TRACE" "$CONTENT_MAP"
      grep -a 'audio: mp3 ' "$DEV_TRACE" > "$OUT/mp3-events-device.txt" 2>/dev/null || true
    fi
  fi
else
  echo
  echo "=== 5/5 device acceptance (skipped: pass --device to run the audible device-6 acceptance) ==="
fi

echo
echo "assertions_ok=$ok assertions_bad=$bad"
echo "captures: map=$CONTENT_MAP trace=$NULL_TRACE bmp=$NULL_BMP"
if [ -s "$NULL_TRACE" ]; then
  echo "null trace sha256: $(sha256sum "$NULL_TRACE" | cut -d' ' -f1)"
fi

# ---------------------------------------------------------------- mutation pass
mrc=0
m_caught=0
m_total=0
if [ "$MUTATION" -eq 1 ]; then
  echo
  echo "=== mutation: perturb each capture and require its assertions to go red ==="
  mutate_checks() { # mutate_checks <name> <file> <sed-expr> <mode>
    local name="$1" file="$2" expr="$3" mode="$4"
    local copy="$OUT/mutated-$name.txt"
    if [ ! -s "$file" ]; then
      echo "  MUTATION-SETUP=FAIL ($name: no capture to perturb)"
      mrc=1; m_total=$((m_total + 1)); return
    fi
    cp "$file" "$copy"
    sed -i "$expr" "$copy"
    if cmp -s "$file" "$copy"; then
      echo "  MUTATION-SETUP=FAIL ($name: the perturbation changed nothing)"
      mrc=1; m_total=$((m_total + 1)); return
    fi
    local saved_ok=$ok saved_bad=$bad
    ok=0; bad=0
    case "$mode" in
      content) content_checks "content[mutated:$name]" "$copy" "$CONTENT_JSON" >> "$OUT/mutated-$name.log" 2>&1 ;;
      null) null_checks "null[mutated:$name]" "$copy" "$CONTENT_MAP" >> "$OUT/mutated-$name.log" 2>&1 ;;
      device) device_checks "device[mutated:$name]" "$copy" "$NULL_TRACE" "$CONTENT_MAP" >> "$OUT/mutated-$name.log" 2>&1 ;;
      scheduler) scheduler_checks "scheduler[mutated:$name]" "$copy" >> "$OUT/mutated-$name.log" 2>&1 ;;
      *)
        echo "  MUTATION-SETUP=FAIL ($name: unknown checker $mode)"
        mrc=1; ok=$saved_ok; bad=$saved_bad; m_total=$((m_total + 1)); return
        ;;
    esac
    local mok=$ok mbad=$bad
    ok=$saved_ok; bad=$saved_bad
    m_total=$((m_total + 1))
    if [ "$mbad" -ge 1 ]; then
      m_caught=$((m_caught + 1))
      printf '  MUTATION-CAUGHT=PASS (%s: %s of %s assertions went red)\n' "$name" "$mbad" "$((mok + mbad))"
    else
      printf '  MUTATION-CAUGHT=FAIL (%s: no assertion moved)\n' "$name"
      mrc=1
    fi
  }

  # The content map: every field the 2/5 section pins is perturbed on its own, so
  # "the row is the reading" is checked field by field instead of as a blob.
  mutate_checks content-seconds "$CONTENT_MAP" 's/\t1\.4\t/\t9.9\t/' content
  mutate_checks content-path "$CONTENT_MAP" 's#sound/ambient_mp3/water/water_splash2\.mp3#sound/ambient_mp3/water/water_splash_typo.mp3#' content
  mutate_checks content-tick "$CONTENT_MAP" 's/^7675\t6616/7676\t6616/' content
  mutate_checks content-detail "$CONTENT_MAP" 's/bytes=122112/bytes=122113/' content
  mutate_checks content-filtered "$CONTENT_MAP" 's/\tvoice\tresolved\t/\tvoice\tinvalid_wav\t/' content

  # The null-sink run: the per-event record, the four counters, the identity and
  # the quiet-device contract each get their own flip.
  mutate_checks null-outcome "$NULL_TRACE" 's/outcome=skipped/outcome=played/' null
  # The named refusal must stay named: turning the out-of-range event's
  # "inaudible" into "skipped" is the collapse this task's product change exists
  # to prevent, so the gate has to notice it.
  mutate_checks null-inaudible-name "$NULL_TRACE" 's/outcome=inaudible/outcome=skipped/' null
  mutate_checks null-skips "$NULL_TRACE" 's/soundEventsMp3Skipped=1/soundEventsMp3Skipped=0/' null
  mutate_checks null-inaudible-count "$NULL_TRACE" 's/soundEventsMp3Inaudible=1/soundEventsMp3Inaudible=0/' null
  mutate_checks null-plays "$NULL_TRACE" 's/soundEventsMp3Played=0/soundEventsMp3Played=1/' null
  mutate_checks null-bytes "$NULL_TRACE" 's/pcmBytes=122112/pcmBytes=1/' null
  mutate_checks null-attempts "$NULL_TRACE" 's/audio_device_open_attempts=0/audio_device_open_attempts=1/' null
  mutate_checks null-hidden "$NULL_TRACE" 's/hidden=1/hidden=0/' null

  # The refusal probe: a named refusal stopping being named, a refusal being
  # dropped, and the codec provenance disappearing are three different defects.
  mutate_checks scheduler-refused "$OUT/scheduler.txt" 's/mp3PlaybackRefused=3/mp3PlaybackRefused=0/' scheduler
  mutate_checks scheduler-missing "$OUT/scheduler.txt" 's/mp3PlaybackMissing=3/mp3PlaybackMissing=2/' scheduler
  mutate_checks scheduler-sink "$OUT/scheduler.txt" 's/mp3PlaybackSinkCalls=2/mp3PlaybackSinkCalls=3/' scheduler
  mutate_checks scheduler-noframe "$OUT/scheduler.txt" 's/mp3NoFrameStatus=invalid_wav/mp3NoFrameStatus=resolved/' scheduler
  mutate_checks scheduler-provenance "$OUT/scheduler.txt" 's/mp3PlaybackMp3Seen=1/mp3PlaybackMp3Seen=0/' scheduler
  mutate_checks scheduler-refusalnames "$OUT/scheduler.txt" 's/probe_no_frame.mp3:invalid_wav/probe_no_frame.mp3:resolved/' scheduler
  mutate_checks scheduler-observer "$OUT/scheduler.txt" 's/mp3PlaybackObserved=probe_play_wav.wav:played,probe_play_mp3.mp3:played/mp3PlaybackObserved=probe_play_wav.wav:played/' scheduler

  if [ "$DEVICE" -eq 1 ]; then
    mutate_checks device-outcome "$DEV_TRACE" 's/outcome=played/outcome=skipped/' device
    mutate_checks device-inaudible "$DEV_TRACE" 's/outcome=inaudible/outcome=played/' device
    mutate_checks device-mp3played "$DEV_TRACE" 's/soundEventsMp3Played=1/soundEventsMp3Played=0/' device
    mutate_checks device-opens "$DEV_TRACE" 's/audio_device_opens_ok=[0-9]*/audio_device_opens_ok=0/' device
    mutate_checks device-attempts "$DEV_TRACE" 's/audio_device_open_attempts=[0-9]*/audio_device_open_attempts=0/' device
  else
    echo "  (device mutations skipped: run with --device to capture them)"
  fi
  echo "mutations_caught=$m_caught mutations_total=$m_total"
fi

echo
if [ "$bad" -eq 0 ] && [ "$mrc" -eq 0 ]; then
  echo "VERIFY-AUDIO-CONTENT=PASS (assertions_ok=$ok mutations=$m_caught/$m_total)"
  exit 0
fi
echo "VERIFY-AUDIO-CONTENT=FAIL (assertions_ok=$ok assertions_bad=$bad mutations=$m_caught/$m_total)"
exit 1
