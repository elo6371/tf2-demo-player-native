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
  echo "=== 1/2 incremental build ==="
  bash build-target.sh audio_effects_probe audio_scheduler_probe winmm_sink_probe
  rc_build=$?
  echo "build_rc=$rc_build"
  [ "$rc_build" -eq 0 ] || { echo "VERIFY-AUDIO=FAIL (build)"; exit 1; }
else
  echo "=== 1/2 incremental build (skipped: --no-build) ==="
fi

echo
echo "=== 2/2 the probes, dry ==="

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
if [ "${DEV_COUNT:-0}" -eq "$DEV_ROWS" ]; then
  ok=$((ok + 1)); printf '  OK   %s\n' "devices: ${DEV_ROWS} per-device lines match device_count=${DEV_COUNT}"
else
  bad=$((bad + 1)); printf '  FAIL devices: device_count=%s but %s per-device lines were printed\n' "$DEV_COUNT" "$DEV_ROWS"
fi
if [ "${DEV_COUNT:-0}" -ge 1 ]; then
  ok=$((ok + 1)); printf '  OK   %s\n' "devices: at least one device exists on this machine"
else
  bad=$((bad + 1)); printf '  FAIL devices: device_count=0\n'
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
  [ "$mrc" -eq 0 ] || { echo "VERIFY-AUDIO-MUTATION=FAIL"; exit 1; }
  echo "VERIFY-AUDIO-MUTATION=PASS (all three expectation lists can go red)"
fi

echo
if [ "$bad" -eq 0 ]; then
  echo "VERIFY-AUDIO=PASS (assertions_ok=$ok)"
  exit 0
fi
echo "VERIFY-AUDIO=FAIL (assertions_bad=$bad)"
exit 1
