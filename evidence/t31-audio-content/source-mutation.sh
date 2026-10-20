#!/usr/bin/env bash
# source-mutation.sh -- T31: one real source mutation, its capture by the
# forward gate, and the byte-for-byte restore that reproduces the reference.
#
# WHAT IS MUTATED, AND WHY THIS LINE
#   native/src/audio_resource.cpp, the single line that tags decoded PCM as MP3:
#       out.codec = SoundCodec::Mp3;
#   Removing it is the defect shape "the decoder works and the product cannot
#   say what it decoded": the MP3 events still decode, still reach the output
#   path and still play, so *no total counter moves*. The only readings that
#   move are the ones this task added -- the per-event "audio: mp3" record and
#   the four MP3 counters -- which is exactly the gap T31's gate exists to
#   close. A mutation that a total could see would not test the new instrument.
#
# WHY A SOURCE MUTATION AND NOT ONLY CAPTURE PERTURBATION
#   verify-audio-content.sh --mutation perturbs captured files. That proves the
#   assertions discriminate the bytes; it does not prove the assertions are
#   attached to the product. This script rebuilds the product with the defect,
#   requires the forward gate to fail on a *named* assertion, restores the
#   source byte for byte, rebuilds, and requires the same gate to pass and to
#   reproduce the same per-event reading.
#
# ORDER OF OPERATIONS (each one is printed with its exit code)
#   1. hash the source, keep a copy as the restore source
#   2. delete the one line, prove the file changed
#   3. rebuild only the affected targets, run the forward gate -> must FAIL
#   4. run audio_scheduler_probe alone -> must exit 1 and name the codec check
#   5. restore from the copy, prove the hash is identical to step 1
#   6. rebuild, run the forward gate -> must PASS
#   7. compare the restored run's per-event record with the reference capture
#      taken before the mutation -> must be identical
#
# This script runs builds and product launches, so it is only ever invoked
# inside one hostlock run (see the report). It writes nothing outside
# .scratch/t31/source-mutation/.
set -uo pipefail
cd "$(dirname "$0")/../.."

SRC=native/src/audio_resource.cpp
WORK=.scratch/t31/source-mutation
OUT=evidence/fast/audio-content
mkdir -p "$WORK"

rc_all=0
step() { printf '\n=== %s ===\n' "$1"; }

step "1. hash the source, keep the restore copy, keep the reference reading"
h_before=$(sha256sum "$SRC" | cut -d' ' -f1)
cp "$SRC" "$WORK/audio_resource.cpp.before"
# The reference per-event record has to be taken HERE, before anything is
# rebuilt: step 6 reruns the gate, which overwrites $OUT/mp3-events-null.txt. The
# first version of this script copied it in step 6 and therefore compared the
# restored run against the *mutated* run's empty file -- a comparison that could
# only ever read NOT-COMPARED. Copied here it is the reading the mutant has to
# destroy and the restored build has to reproduce.
cp "$OUT/mp3-events-null.txt" "$WORK/reference-mp3-events.txt" 2>/dev/null || true
if [ -s "$WORK/reference-mp3-events.txt" ]; then
  echo "reference=$(sha256sum "$WORK/reference-mp3-events.txt" | cut -d' ' -f1) ($(wc -l < "$WORK/reference-mp3-events.txt") lines)"
else
  echo "reference=MISSING (run the forward gate once before this recipe; step 7 needs it)"
fi
echo "src=$SRC"
echo "sha256_before=$h_before"
echo "copy=$WORK/audio_resource.cpp.before"

step "2. the mutation: delete the MP3 provenance line"
sed -i '/out\.codec = SoundCodec::Mp3;/d' "$SRC"
if cmp -s "$SRC" "$WORK/audio_resource.cpp.before"; then
  echo "MUTATION-SETUP=FAIL (the line was not there to delete)"
  exit 1
fi
h_mutated=$(sha256sum "$SRC" | cut -d' ' -f1)
echo "sha256_mutated=$h_mutated"
grep -c 'out.codec = SoundCodec::Mp3;' "$SRC" || echo "mp3_provenance_lines=0"

step "3. rebuild the affected targets and run the forward gate (expect FAIL)"
bash build-target.sh audio_scheduler_probe sound_content_map_probe tf2_demo_native > "$WORK/build-mutated.log" 2>&1
rc_build_mutated=$?
echo "build_rc=$rc_build_mutated"
tail -2 "$WORK/build-mutated.log"
if [ "$rc_build_mutated" -ne 0 ]; then
  echo "RESULT=FAIL (the mutated tree did not build; a build failure is not a caught mutation)"
  exit 1
fi
VERIFY_AUDIO_CONTENT_OUT="$OUT" bash verify-audio-content.sh --no-build > "$WORK/gate-mutated.log" 2>&1
rc_gate_mutated=$?
echo "gate_rc=$rc_gate_mutated"
grep -a '^  FAIL' "$WORK/gate-mutated.log" | head -6
tail -2 "$WORK/gate-mutated.log"
if [ "$rc_gate_mutated" -eq 0 ]; then
  echo "MUTATION-CAUGHT=FAIL (the forward gate passed on the mutated product)"
  rc_all=1
elif ! grep -aqF 'per-event MP3 records' "$WORK/gate-mutated.log"; then
  echo "MUTATION-CAUGHT=PARTIAL (the gate failed, but not on the per-event MP3 assertion)"
  grep -a '^  FAIL' "$WORK/gate-mutated.log" | head -6
  rc_all=1
else
  echo "MUTATION-CAUGHT=PASS (the forward gate failed on the per-event MP3 record)"
fi

step "4. the probe alone on the mutated build (expect exit 1, naming the codec)"
native/build-nmake/audio_scheduler_probe.exe > "$WORK/probe-mutated.stdout" 2> "$WORK/probe-mutated.stderr"
rc_probe_mutated=$?
echo "probe_rc=$rc_probe_mutated"
grep -a 'the playable MP3 is attributed' "$WORK/probe-mutated.stderr" | head -2
if [ "$rc_probe_mutated" -eq 1 ]; then
  echo "PROBE-CAUGHT=PASS (the probe's contract refused the mutated build)"
else
  echo "PROBE-CAUGHT=FAIL (probe_rc=$rc_probe_mutated)"
  rc_all=1
fi

step "5. restore the source byte for byte"
cp "$WORK/audio_resource.cpp.before" "$SRC"
h_restored=$(sha256sum "$SRC" | cut -d' ' -f1)
echo "sha256_restored=$h_restored"
if [ "$h_restored" = "$h_before" ] && cmp -s "$SRC" "$WORK/audio_resource.cpp.before"; then
  echo "RESTORE=BYTE-IDENTICAL ($h_restored)"
else
  echo "RESTORE=FAIL (before=$h_before restored=$h_restored)"
  rc_all=1
fi

step "6. rebuild and run the forward gate again (expect PASS)"
bash build-target.sh audio_scheduler_probe sound_content_map_probe tf2_demo_native > "$WORK/build-restored.log" 2>&1
rc_build_restored=$?
echo "build_rc=$rc_build_restored"
if [ "$rc_build_restored" -ne 0 ]; then
  echo "RESULT=FAIL (the restored tree did not build)"
  exit 1
fi
VERIFY_AUDIO_CONTENT_OUT="$OUT" bash verify-audio-content.sh --no-build > "$WORK/gate-restored.log" 2>&1
rc_gate_restored=$?
echo "gate_rc=$rc_gate_restored"
tail -2 "$WORK/gate-restored.log"
if [ "$rc_gate_restored" -eq 0 ]; then
  echo "REFERENCE-REPRODUCED=PASS (the restored build passes the same gate)"
else
  echo "REFERENCE-REPRODUCED=FAIL (gate_rc=$rc_gate_restored)"
  rc_all=1
fi

step "7. compare the restored per-event record with the pre-mutation reference"
# The raw captured lines start with the trace's own elapsed-milliseconds prefix
# ("65897.6 audio: mp3 ..."), which is wall clock and therefore different on every
# run. It is not part of the record the gate reads -- mp3_segments in
# verify-audio-content.sh strips it with the same expression -- so the comparison
# normalises it away on both sides before diffing. The first version of this step
# diffed the raw lines and reported DIFFERS on two runs whose every field was
# identical; that is a comparison of timestamps, not of readings.
normalise() { sed -e 's/^[0-9][0-9.]* audio: mp3/audio: mp3/' "$1"; }
if [ -s "$WORK/reference-mp3-events.txt" ] && [ -s "$OUT/mp3-events-null.txt" ]; then
  normalise "$WORK/reference-mp3-events.txt" > "$WORK/reference-mp3-events.norm"
  normalise "$OUT/mp3-events-null.txt" > "$WORK/restored-mp3-events.norm"
  echo "reference_normalised=$(sha256sum "$WORK/reference-mp3-events.norm" | cut -d' ' -f1)"
  echo "restored_normalised=$(sha256sum "$WORK/restored-mp3-events.norm" | cut -d' ' -f1)"
  if diff -q "$WORK/reference-mp3-events.norm" "$WORK/restored-mp3-events.norm" > /dev/null; then
    echo "PER-EVENT-RECORD=IDENTICAL (after stripping the trace's elapsed prefix: $(wc -l < "$WORK/restored-mp3-events.norm") lines)"
  else
    echo "PER-EVENT-RECORD=DIFFERS"
    diff "$WORK/reference-mp3-events.norm" "$WORK/restored-mp3-events.norm" | head -6
    rc_all=1
  fi
else
  echo "PER-EVENT-RECORD=NOT-COMPARED (a reference capture is missing)"
  rc_all=1
fi

step "summary"
echo "source_sha256_before=$h_before"
echo "source_sha256_mutated=$h_mutated"
echo "source_sha256_restored=$h_restored"
echo "gate_rc_mutated=$rc_gate_mutated gate_rc_restored=$rc_gate_restored"
if [ "$rc_all" -eq 0 ]; then
  echo "SOURCE-MUTATION=PASS"
  exit 0
fi
echo "SOURCE-MUTATION=FAIL"
exit 1
