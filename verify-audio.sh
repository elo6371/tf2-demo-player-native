#!/usr/bin/env bash
# verify-audio.sh -- tier 2 for the audio / effects subsystem.
#
# WHY THIS EXISTS
#   See verify-entity.sh for the three-tier shape. The audio path is the cheapest
#   of the three subsystems and the least covered: it was never wired into
#   verify-all.sh at all, so before this file its only record was the P0
#   acceptance document's prose. What is asserted here is a contract that is easy
#   to break silently -- that the probes run *without making a sound*.
#
# THE NOISE CONTRACT
#   This machine's default playback device is not the one a gate should be using
#   (the README carries the same warning for tf2_demo_native: every launch names
#   `--audio-device 6`). The probes avoid the question entirely by never asking
#   for a device:
#     * audio_effects_probe   decodes and schedules into an injected sink
#                             (`"device":"sink"`), no WinMM call
#     * audio_scheduler_probe reports `device=0 playbackCalls=0`
#     * winmm_sink_probe with no arguments is `mode=dry_run device_opened=0`
#   Those three readings are asserted here, so a later change that starts opening
#   a real device fails this gate instead of beeping in someone's ear at 3 a.m.
#   `winmm_sink_probe --device N` does open a real device; it is deliberately not
#   run by this script.
#
# WHAT IS NOT ASSERTED, AND WHY
#   `audio_effects_probe` reports `"realPcf":"skipped"` with `realOperators:0`
#   `realCommon:0` `realFunction:""` when no PCF file is passed, and
#   `audio_scheduler_probe` reports `reverseSeen=0` by construction. Asserting
#   those would be asserting that a skip happened, which is the failure mode this
#   repo calls "PASS while checking nothing". They are printed for the record and
#   left alone; the synthetic cases around them are what is pinned.
#
# Usage: bash verify-audio.sh [--mutation] [--no-build]
#   --mutation perturbs each captured output and requires the assertions to go
#   red, so the lists are known to discriminate rather than assumed to.
# Exit:  0 = every assertion held.
set -uo pipefail
cd "$(dirname "$0")"

OUT="${VERIFY_AUDIO_OUT:-evidence/fast/audio}"
mkdir -p "$OUT"

MUTATION=0
BUILD=1
for arg in "$@"; do
  case "$arg" in
    --mutation) MUTATION=1 ;;
    --no-build) BUILD=0 ;;
    *) echo "unknown argument: $arg"; exit 2 ;;
  esac
done

B=native/build-nmake

if [ "$BUILD" -eq 1 ]; then
  echo "=== 1/4 incremental build ==="
  bash build-target.sh audio_effects_probe audio_scheduler_probe winmm_sink_probe sound_timeline_census_probe tf2_demo_native
  rc_build=$?
  echo "build_rc=$rc_build"
  [ "$rc_build" -eq 0 ] || { echo "VERIFY-AUDIO=FAIL (build)"; exit 1; }
else
  echo "=== 1/4 incremental build (skipped: --no-build) ==="
fi

echo
echo "=== 2/4 the probes, dry ==="

ok=0
bad=0

assert_line() { # assert_line <file> <fixed string> <what>
  local file="$1" needle="$2" what="$3"
  if [ ! -s "$file" ]; then
    bad=$((bad + 1)); printf '  FAIL %s (no output captured)\n' "$what"; return
  fi
  if grep -qF -- "$needle" "$file"; then
    ok=$((ok + 1)); printf '  OK   %s\n' "$what"
  else
    bad=$((bad + 1)); printf '  FAIL %s (wanted: %s)\n' "$what" "$needle"
  fi
}

run_expectations() { # run_expectations <file> <needle|what> ...
  local file="$1"; shift
  local e
  for e in "$@"; do assert_line "$file" "${e%%|*}" "${e#*|}"; done
}

E_EFFECTS=(
  '"selfTest":true|effects: the self-test ran'
  '"delay":true|effects: delay line'
  '"kinds":true|effects: effect kinds'
  '"voiceFiltered":true|effects: voice filtering'
  '"spatial":true|effects: spatialisation'
  '"schedulerSink":true|effects: scheduling reaches the injected sink'
  '"missingKept":true|effects: a missing asset is kept, not dropped'
  '"hud":true|effects: HUD'
  '"particle":true|effects: particle'
  '"pcf":true|effects: PCF operators'
  '"pcfCorrupt":true|effects: a corrupt PCF is rejected'
  '"device":"sink"|effects: no real device was requested'
)

E_SCHEDULER=(
  'device=0 playbackCalls=0|scheduler: nothing was sent to a device'
  'sinkCalls=1|scheduler: the injected sink received the work'
  'queuedPcm=1|scheduler: one PCM buffer was queued'
  'accepted=8 rejected=1|scheduler: the queue limit accepted 8 and rejected 1'
  'vpkWav=1|scheduler: one real WAV resolved out of the VPK'
  'wavMalformed=3|scheduler: three malformed WAVs were rejected'
  'jumpSeen=2|scheduler: two timeline jumps exercised the scheduler'
  'recoveredSeen=2|scheduler: two recoveries'
  'missingPlayed=0|scheduler: a missing sound was never played'
)

E_SINK=(
  'mode=dry_run|sink: the dry-run mode was selected'
  'device_opened=0|sink: no device was opened'
  'playback_written=0|sink: nothing was written for playback'
  'status=pass|sink: the dry-run verdict'
)

"$B/audio_effects_probe.exe"   > "$OUT/audio-effects.txt" 2>&1
"$B/audio_scheduler_probe.exe" > "$OUT/audio-scheduler.txt" 2>&1
"$B/winmm_sink_probe.exe"      > "$OUT/winmm-sink.txt" 2>&1
"$B/winmm_sink_probe.exe" --list-devices > "$OUT/winmm-devices.txt" 2>&1

echo "-- audio_effects_probe"
run_expectations "$OUT/audio-effects.txt"   "${E_EFFECTS[@]}"
echo "-- audio_scheduler_probe"
run_expectations "$OUT/audio-scheduler.txt" "${E_SCHEDULER[@]}"
echo "-- winmm_sink_probe (no arguments = dry run)"
run_expectations "$OUT/winmm-sink.txt"      "${E_SINK[@]}"

echo "-- winmm_sink_probe --list-devices"
# Enumeration is asserted as a *consistency* between two counts rather than as a
# pinned number: `device_count` is a property of this machine (13 here) and
# pinning it would make the gate red on any other one, which is the same mistake
# as pinning a demo sample drawn from a live directory. What must hold anywhere
# is that the count is non-zero and that the probe printed exactly that many
# per-device lines.
DEV_LINE=$(head -1 "$OUT/winmm-devices.txt")
DEV_COUNT=$(printf '%s' "$DEV_LINE" | grep -oE 'device_count=[0-9]+' | cut -d= -f2)
DEV_ROWS=$(grep -c '^device=' "$OUT/winmm-devices.txt" || true)
if printf '%s' "$DEV_LINE" | grep -q 'mode=list_devices' && [ "${DEV_COUNT:-0}" -ge 1 ]; then
  ok=$((ok + 1)); printf '  OK   %s\n' "devices: enumeration ran and found ${DEV_COUNT}"
else
  bad=$((bad + 1)); printf '  FAIL devices: enumeration did not run (line: %s)\n' "$DEV_LINE"
fi
if [ "${DEV_COUNT:-0}" -eq "$DEV_ROWS" ] && [ "${DEV_COUNT:-0}" -ge 1 ]; then
  ok=$((ok + 1)); printf '  OK   %s\n' "devices: ${DEV_ROWS} per-device lines match device_count=${DEV_COUNT}"
else
  # The `-ge 1` matters: with an unparsed device_count the two sides are both 0
  # or both empty, and "0 == 0" would print OK while nothing was checked at all.
  # Measured on 2026-10-13: the probe missing from the build tree made
  # device_count empty, and this line reported OK. A comparison whose operands
  # can both be "nothing" has to prove they are not.
  bad=$((bad + 1)); printf '  FAIL devices: device_count=%s but %s per-device lines were printed\n' "${DEV_COUNT:-<unparsed>}" "$DEV_ROWS"
fi
if [ "${DEV_COUNT:-0}" -ge 1 ]; then
  ok=$((ok + 1)); printf '  OK   %s\n' "devices: at least one device exists on this machine"
else
  bad=$((bad + 1)); printf '  FAIL devices: device_count=0\n'
fi


echo
echo "=== 3/4 the timeline census on the pinned recordings ==="
# The player reports `events_seen`, `soundEventsRead` and `soundEventsScheduled`.
# This section reports what those numbers have to be, from an instrument that
# never opens a device and never touches the running player. Two independent
# readings that must agree is what turns "the cursor advanced and lost nothing"
# into something a gate can check.
#
# A missing or changed recording is a FAIL, not a SKIP: "the input is not here"
# and "the input is clean" must not read the same. See
# evidence/audio/timeline-inputs.txt.
INPUTS=evidence/audio/timeline-inputs.txt
TF_DIR="${TF:-D:/SteamLibrary/steamapps/common/Team Fortress 2/tf}"

# The checks live in functions so the mutation pass below re-runs exactly what
# the gate asserts. A mutation that re-implemented the assertion would be testing
# the copy, not the gate, and a copy drifts.
census_checks() { # census_checks <label> <census-json-file>
  local label="$1" file="$2"
  # Every decoded event is either placed on the timeline or counted as
  # unplaceable. Neither may be silently dropped.
  if grep -qF '"unplaceablePacketOrigin":0' "$file"; then
    ok=$((ok + 1)); printf '  OK   %s (no event precedes the playback origin)\n' "$label"
  else
    bad=$((bad + 1)); printf '  FAIL %s (events precede the origin: %s)\n' "$label" \
      "$(grep -o '"unplaceablePacketOrigin":[0-9]*' "$file")"
  fi
  # The scheduler's forward pass and the census histogram are computed by
  # different code over the same timeline. They must be equal at every sample
  # tick: unequal means events were lost or replayed.
  local fseen cumul
  fseen=$(grep -o '"forwardSeen":\[[^]]*\]' "$file" | sed 's/.*\[//;s/\]//')
  cumul=$(grep -o '"cumulative":\[[^]]*\]' "$file" | sed 's/.*\[//;s/\]//')
  if [ -n "$fseen" ] && [ "$fseen" = "$cumul" ]; then
    ok=$((ok + 1)); printf '  OK   %s (scheduler forward pass == census histogram: %s)\n' "$label" "$fseen"
  else
    bad=$((bad + 1)); printf '  FAIL %s (forwardSeen %s != cumulative %s)\n' "$label" "$fseen" "$cumul"
  fi
  # Re-presenting a consumed tick must add nothing, and must be counted.
  local sbd sad dta
  sbd=$(grep -o '"seenBeforeDuplicate":[0-9]*' "$file" | cut -d: -f2)
  sad=$(grep -o '"seenAfterDuplicate":[0-9]*' "$file" | cut -d: -f2)
  dta=$(grep -o '"duplicateTicksAfter":[0-9]*' "$file" | cut -d: -f2)
  if [ -n "$sbd" ] && [ "$sbd" = "$sad" ] && [ "${dta:-0}" -ge 1 ]; then
    ok=$((ok + 1)); printf '  OK   %s (a repeated tick replayed 0 events, guard fired %s time(s))\n' "$label" "$dta"
  else
    bad=$((bad + 1)); printf '  FAIL %s (repeated tick: before=%s after=%s guard=%s)\n' "$label" "$sbd" "$sad" "$dta"
  fi
  # Reverse timing: re-seed the cursor backwards and re-advance. The events
  # replayed must be exactly the events in the replayed window.
  local rr rs re
  rr=$(grep -o '"reverseRecoveriesAfter":[0-9]*' "$file" | cut -d: -f2)
  rs=$(grep -o '"replaySeen":[0-9]*' "$file" | cut -d: -f2)
  re=$(grep -o '"replayExpected":[0-9]*' "$file" | cut -d: -f2)
  if [ "${rr:-0}" -ge 1 ] && [ -n "$rs" ] && [ "$rs" = "$re" ] && [ "${re:-0}" -ge 1 ]; then
    ok=$((ok + 1)); printf '  OK   %s (reverse replayed exactly the window: %s events, recoveries %s)\n' "$label" "$rs" "$rr"
  else
    bad=$((bad + 1)); printf '  FAIL %s (reverse: seen=%s expected=%s recoveries=%s)\n' "$label" "$rs" "$re" "$rr"
  fi
}

if [ ! -s "$INPUTS" ]; then
  bad=$((bad + 1)); printf '  FAIL %s\n' "census: $INPUTS is missing (the pinned recordings are the input, not a convenience)"
else
  while read -r want_size want_hash want_root want_name; do
    # The skip tests the FIRST field: a comment line splits into four words, so
    # testing the last one misses every comment whose final word is not "#".
    # Measured 2026-10-13: it turned 24 comment lines into 24 missing-file FAILs.
    case "$want_size" in ''|'#'*) continue ;; esac
    want_name=${want_name%$'\r'}
    if [ "$want_root" = "steam" ]; then demo_path="$TF_DIR/demos/$want_name"
    else demo_path="D:/TF2_Demo_Player/testdata/demos/$want_name"; fi
    label="census[$(printf '%s' "$want_name" | cut -c1-28)]"
    if [ ! -f "$demo_path" ]; then
      bad=$((bad + 1)); printf '  FAIL %s (absent: %s)\n' "$label" "$demo_path"; continue
    fi
    got_size=$(stat -c %s "$demo_path" 2>/dev/null || echo 0)
    if [ "$got_size" != "$want_size" ]; then
      bad=$((bad + 1)); printf '  FAIL %s (wrong size: %s, pinned %s)\n' "$label" "$got_size" "$want_size"; continue
    fi
    got_hash=$(sha256sum "$demo_path" | cut -d' ' -f1)
    if [ "$got_hash" != "$want_hash" ]; then
      bad=$((bad + 1)); printf '  FAIL %s (wrong hash: %s)\n' "$label" "$got_hash"; continue
    fi
    ok=$((ok + 1)); printf '  OK   %s (pinned input verified: size %s, sha256 %s...)\n' "$label" "$want_size" "${want_hash:0:12}"

    census_out="$OUT/census-$(printf '%s' "$want_name" | cut -c1-12).txt"
    "$B/sound_timeline_census_probe.exe" --demo "$demo_path" > "$census_out" 2>&1
    rc_census=$?
    if [ "$rc_census" -ne 0 ] || ! grep -qF '"status":"ok"' "$census_out"; then
      bad=$((bad + 1)); printf '  FAIL %s (census probe rc=%s)\n' "$label" "$rc_census"; continue
    fi
    ok=$((ok + 1)); printf '  OK   %s (census ran)\n' "$label"
    census_checks "$label" "$census_out"
  done < "$INPUTS"
fi

echo
echo "=== 4/4 the player on a real recording, still --audio-disabled ==="
# The wiring end to end. The demo is real, the sink is device-free, and the
# numbers the player prints are cross-checked against the census above rather
# than pinned by hand -- a hand-typed expectation would still read green if the
# census and the player both drifted the same way.
player_checks() { # player_checks <label> <trace-file> <census-json-file>
  local label="$1" trace="$2" census="$3"
  local summary timeline_line mode_line
  summary=$(grep -a 'audio: summary' "$trace" | tail -1)
  timeline_line=$(grep -a 'audio: timeline' "$trace" | tail -1)
  mode_line=$(grep -a 'mode: summary' "$trace" | tail -1)

  local c_read c_sched c_origin c_pkt
  c_read=$(grep -o '"soundEvents":[0-9]*' "$census" | cut -d: -f2)
  c_sched=$(grep -o '"timelineSize":[0-9]*' "$census" | cut -d: -f2)
  c_origin=$(grep -o '"packetOrigin":-\?[0-9]*' "$census" | cut -d: -f2)
  c_pkt=$(grep -o '"firstPacketTick":-\?[0-9]*' "$census" | cut -d: -f2)

  local p_read p_sched p_origin p_pkt
  p_read=$(printf '%s' "$timeline_line" | grep -o 'read=[0-9]*' | cut -d= -f2)
  p_sched=$(printf '%s' "$timeline_line" | grep -o 'scheduled=[0-9]*' | cut -d= -f2)
  p_origin=$(printf '%s' "$summary" | grep -o 'soundOriginTick=-\?[0-9]*' | cut -d= -f2)
  p_pkt=$(printf '%s' "$summary" | grep -o 'firstPacketTick=-\?[0-9]*' | cut -d= -f2)

  # What the decoder produced and what survived the origin/name filter must be
  # the census's numbers, not a hand-typed pair.
  if [ -n "$c_read" ] && [ "$c_read" = "$p_read" ] && [ "$c_sched" = "$p_sched" ]; then
    ok=$((ok + 1)); printf '  OK   %s (read=%s scheduled=%s match the census)\n' "$label" "$p_read" "$p_sched"
  else
    bad=$((bad + 1)); printf '  FAIL %s (read=%s scheduled=%s, census says %s/%s)\n' "$label" "$p_read" "$p_sched" "$c_read" "$c_sched"
  fi
  # Both origins, because a demo carries two tick spaces and using one variable
  # for both emptied the scene once already.
  if [ -n "$c_origin" ] && [ "$c_origin" = "$p_origin" ] && [ "$c_pkt" = "$p_pkt" ]; then
    ok=$((ok + 1)); printf '  OK   %s (firstPacketTick=%s soundOriginTick=%s match the census)\n' "$label" "$p_pkt" "$p_origin"
  else
    bad=$((bad + 1)); printf '  FAIL %s (origins: packet=%s/%s sound=%s/%s)\n' "$label" "$p_pkt" "$c_pkt" "$p_origin" "$c_origin"
  fi
  # Silent mode must not count a play, and must not have asked for a device.
  if printf '%s' "$summary" | grep -qF 'soundEventsPlayed=0' \
     && printf '%s' "$mode_line" | grep -qF 'audio_device_opened=0'; then
    ok=$((ok + 1)); printf '  OK   %s (--audio-disabled played nothing and opened no device)\n' "$label"
  else
    bad=$((bad + 1)); printf '  FAIL %s (silent mode: %s | %s)\n' "$label" "$summary" "$mode_line"
  fi
  # The voice counter has to be live: voice events are kept in the timeline
  # flagged as skipped rather than refused before `add`, so this reading can
  # move. A zero here means the filter ran upstream and the counter is dead.
  local sv
  sv=$(printf '%s' "$summary" | grep -o 'soundEventsSkippedVoice=[0-9]*' | cut -d= -f2)
  if [ "${sv:-0}" -ge 1 ]; then
    ok=$((ok + 1)); printf '  OK   %s (soundEventsSkippedVoice=%s: the voice counter is live)\n' "$label" "$sv"
  else
    bad=$((bad + 1)); printf '  FAIL %s (soundEventsSkippedVoice=%s: a counter that can only read 0 is not a reading)\n' "$label" "${sv:-<unparsed>}"
  fi
  # The same-tick guard must have fired: the player advances once per rendered
  # frame while the timeline moves at the demo tick rate.
  local dt
  dt=$(printf '%s' "$summary" | grep -o 'duplicateTicks=[0-9]*' | cut -d= -f2)
  if [ "${dt:-0}" -ge 1 ]; then
    ok=$((ok + 1)); printf '  OK   %s (duplicateTicks=%s: the same-tick guard fired)\n' "$label" "$dt"
  else
    bad=$((bad + 1)); printf '  FAIL %s (duplicateTicks=%s: the guard was never exercised)\n' "$label" "${dt:-<unparsed>}"
  fi
  # A forward run must not report a backwards recovery.
  if printf '%s' "$summary" | grep -qF 'reverseRecoveries=0'; then
    ok=$((ok + 1)); printf '  OK   %s (reverseRecoveries=0 on a forward run)\n' "$label"
  else
    bad=$((bad + 1)); printf '  FAIL %s (a forward run reported a reverse recovery: %s)\n' "$label" "$summary"
  fi
  # The run must have scheduled something; a run that played nothing is
  # indistinguishable from one that wired nothing.
  local seen
  seen=$(printf '%s' "$mode_line" | grep -o 'events_seen=[0-9]*' | cut -d= -f2)
  if [ "${seen:-0}" -ge 1 ]; then
    ok=$((ok + 1)); printf '  OK   %s (events_seen=%s over the run)\n' "$label" "$seen"
  else
    bad=$((bad + 1)); printf '  FAIL %s (events_seen=%s: nothing reached the scheduler)\n' "$label" "${seen:-<unparsed>}"
  fi
}

DEMO_POV="$TF_DIR/demos/autorecord_2026-07-02_13-26-46.dem"
CENSUS_POV="$OUT/census-autorecord_2.txt"
if [ ! -f "$DEMO_POV" ]; then
  bad=$((bad + 1)); printf '  FAIL %s\n' "player: the pinned recording is absent ($DEMO_POV)"
elif [ ! -s "$CENSUS_POV" ]; then
  bad=$((bad + 1)); printf '  FAIL %s\n' "player: no census capture to compare against"
else
  TRACE="$OUT/player-trace.txt"
  rm -f "$TRACE"
  # --audio-device 6 is mandatory on any run that can reach a device: the default
  # is WAVE_MAPPER, which is not the device a gate should be using. With
  # --audio-disabled no device is opened at all, and the run asserts that.
  #
  # Bounded on purpose. Measured 2026-10-13: a player process left behind by an
  # earlier interrupted run held the window, and the next run never returned --
  # the gate hung instead of failing, which is worse than a red, because a hung
  # gate looks like a slow one. 124 is the timeout exit code and is reported as
  # itself rather than folded into "run failed".
  TF2_NATIVE_TRACE="$TRACE" timeout 180 "$B/tf2_demo_native.exe" --demo "$DEMO_POV" \
    --audio-disabled --audio-device 6 --capture-tick 120 \
    --capture-frame "$OUT/player-frame.bmp" > "$OUT/player-stdout.txt" 2>&1
  rc_player=$?
  if [ "$rc_player" -eq 124 ]; then
    bad=$((bad + 1)); printf '  FAIL %s\n' "player: run exceeded 180s (a stray process may hold the window; check tasklist for tf2_demo_native.exe)"
  elif [ "$rc_player" -ne 0 ] || [ ! -s "$TRACE" ]; then
    bad=$((bad + 1)); printf '  FAIL %s\n' "player: run failed (rc=$rc_player)"
  else
    ok=$((ok + 1)); printf '  OK   %s\n' "player: the run produced a trace (rc=0)"
    player_checks "player" "$TRACE" "$CENSUS_POV"
  fi
fi

echo
echo "assertions_ok=$ok assertions_bad=$bad"

if [ "$MUTATION" -eq 1 ]; then
  echo
  echo "=== mutation: perturb each capture and require its assertions to go red ==="
  mrc=0
  mutate_case() { # mutate_case <name> <file> <sed-expression> <assertions...>
    local name="$1" file="$2" expr="$3"; shift 3
    local copy="$OUT/mutated-$name.txt"
    cp "$file" "$copy"
    sed -i "$expr" "$copy"
    if cmp -s "$file" "$copy"; then
      echo "  MUTATION-SETUP=FAIL ($name: the perturbation changed nothing)"
      mrc=1; return
    fi
    local saved_ok=$ok saved_bad=$bad
    ok=0; bad=0
    run_expectations "$copy" "$@" > "$OUT/mutated-$name.log" 2>&1
    local mok=$ok mbad=$bad
    ok=$saved_ok; bad=$saved_bad
    if [ "$mbad" -ge 1 ]; then
      printf '  MUTATION-CAUGHT=PASS (%s: %s of %s assertions went red)\n' "$name" "$mbad" "$((mok + mbad))"
    else
      printf '  MUTATION-CAUGHT=FAIL (%s: no assertion moved)\n' "$name"
      mrc=1
    fi
  }
  mutate_case effects   "$OUT/audio-effects.txt"   's/"spatial":true/"spatial":false/'          "${E_EFFECTS[@]}"
  mutate_case scheduler "$OUT/audio-scheduler.txt" 's/playbackCalls=0/playbackCalls=1/'        "${E_SCHEDULER[@]}"
  mutate_case sink      "$OUT/winmm-sink.txt"      's/mode=dry_run/mode=explicit/'             "${E_SINK[@]}"

  # The census and player checkers, mutated the same way: perturb the captured
  # file and require the *same function* the gate calls to go red. Each case
  # targets one of the five things the wiring is accepted on, so a mutation that
  # moves nothing means that acceptance is not actually being checked.
  #
  # The checker is invoked on the mutated copy, never on the original: a mutation
  # that re-read the original would always pass and would prove nothing.
  mutate_checks() { # mutate_checks <name> <file> <sed-expression> <census|player>
    local name="$1" file="$2" expr="$3" kind="$4"
    local copy="$OUT/mutated-$name.txt"
    cp "$file" "$copy"
    sed -i "$expr" "$copy"
    if cmp -s "$file" "$copy"; then
      echo "  MUTATION-SETUP=FAIL ($name: the perturbation changed nothing)"
      mrc=1; return
    fi
    local saved_ok=$ok saved_bad=$bad
    ok=0; bad=0
    case "$kind" in
      census) census_checks "census[mutated]" "$copy" > "$OUT/mutated-$name.log" 2>&1 ;;
      player) player_checks "player[mutated]" "$copy" "$CENSUS_POV" > "$OUT/mutated-$name.log" 2>&1 ;;
      *) echo "  MUTATION-SETUP=FAIL ($name: unknown checker '$kind')"; mrc=1; ok=$saved_ok; bad=$saved_bad; return ;;
    esac
    local mok=$ok mbad=$bad
    ok=$saved_ok; bad=$saved_bad
    if [ "$mbad" -ge 1 ]; then
      printf '  MUTATION-CAUGHT=PASS (%s: %s of %s assertions went red)\n' "$name" "$mbad" "$((mok + mbad))"
    else
      printf '  MUTATION-CAUGHT=FAIL (%s: no assertion moved)\n' "$name"
      mrc=1
    fi
  }

  if [ -s "$CENSUS_POV" ]; then
    # "history must not be batch-played": an event that precedes the origin has
    # to be refused, and the census must report that none was.
    mutate_checks census-origin "$CENSUS_POV" 's/"unplaceablePacketOrigin":0/"unplaceablePacketOrigin":7/' census
    # "the cursor loses nothing": perturb one sample of the forward pass.
    mutate_checks census-forward "$CENSUS_POV" 's/"forwardSeen":\[746,750/"forwardSeen":[745,750/' census
    # "the same tick does not replay": make the repeat add an event.
    mutate_checks census-duplicate "$CENSUS_POV" 's/"seenAfterDuplicate":[0-9]*/"seenAfterDuplicate":1453/' census
    # "reverse timing is correct": make the reverse pass replay the wrong count.
    mutate_checks census-reverse "$CENSUS_POV" 's/"replaySeen":[0-9]*/"replaySeen":0/' census
  else
    echo "  MUTATION-SETUP=FAIL (no census capture: the census mutations cannot run)"
    mrc=1
  fi

  if [ -s "$OUT/player-trace.txt" ]; then
    # "a missing resource is not counted as played" plus the silent-mode device
    # contract, on the real run's trace.
    mutate_checks player-played  "$OUT/player-trace.txt" 's/soundEventsPlayed=0/soundEventsPlayed=3/' player
    mutate_checks player-device  "$OUT/player-trace.txt" 's/audio_device_opened=0/audio_device_opened=1/' player
    # The voice counter must be live: zeroing it has to fail.
    mutate_checks player-voice   "$OUT/player-trace.txt" 's/soundEventsSkippedVoice=[0-9]*/soundEventsSkippedVoice=0/' player
    # The origin must be the packet origin, not the signon tick.
    mutate_checks player-origin  "$OUT/player-trace.txt" 's/soundOriginTick=0/soundOriginTick=29368/' player
    mutate_checks player-read    "$OUT/player-trace.txt" 's/read=5563/read=5564/' player
  else
    echo "  MUTATION-SETUP=FAIL (no player trace: the player mutations cannot run)"
    mrc=1
  fi

  [ "$mrc" -eq 0 ] || { echo "VERIFY-AUDIO-MUTATION=FAIL"; exit 1; }
  echo "VERIFY-AUDIO-MUTATION=PASS (all expectation lists and checkers can go red)"
fi

echo
if [ "$bad" -eq 0 ]; then
  echo "VERIFY-AUDIO=PASS (assertions_ok=$ok)"
  exit 0
fi
echo "VERIFY-AUDIO=FAIL (assertions_bad=$bad)"
exit 1
