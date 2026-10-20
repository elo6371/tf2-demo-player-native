#!/usr/bin/env bash
# collect.sh -- T31: copy the frozen evidence out of the git-ignored run
# directories into evidence/t31-audio-content/, derive the census summary, and
# write SHA256SUMS over everything that is committed.
#
# WHY A SCRIPT
#   The gate's own output directory (evidence/fast/audio-content) and the run
#   logs (.scratch/t31) are git-ignored machine output: they are overwritten by
#   every run, which is exactly why they must not be committed. What is
#   committed is the *reading* the report cites, copied here once, with a hash
#   list so the copy can be checked later. The .tsv/.jsonl/.txt files are the
#   evidence; the .bmp frames and full traces stay on disk.
#
# Run after the gate sequences, from the repo root or anywhere (it cds itself).
set -uo pipefail
cd "$(dirname "$0")/../.."

OUT=evidence/fast/audio-content
SCR=.scratch/t31
E=evidence/t31-audio-content
mkdir -p "$E"

copy_if_present() { # <src> <dst> <what>
  if [ -s "$1" ]; then
    cp "$1" "$2"
    echo "copied  $2  ($(wc -l < "$2") lines, $(stat -c %s "$2") bytes)"
  else
    echo "MISSING $1  <-- $3"
  fi
}

# 1. The content map for the pinned recording, full (the two scheduled MP3 rows
#    are in it, plus every filtered MP3 row the 2/5 section counts).
copy_if_present "$OUT/content-map.tsv" "$E/content-well.tsv" "content map"

# 2. The per-event MP3 records of the two player runs, exactly as printed.
copy_if_present "$OUT/mp3-events-null.txt" "$E/mp3-events-null.txt" "null-sink per-event record"
copy_if_present "$OUT/mp3-events-device.txt" "$E/mp3-events-device.txt" "device per-event record"

# 3. The readings the two runs are accepted on, one line per trace kind, no
#    per-frame noise.
{
  grep -a 'mode: window_created' "$OUT/null-trace.txt" | tail -1
  grep -a 'mode: summary' "$OUT/null-trace.txt" | tail -1
  grep -a 'audio: summary' "$OUT/null-trace.txt" | tail -1
} > "$E/null-run-readings.txt" 2>/dev/null || echo "MISSING $OUT/null-trace.txt" > "$E/null-run-readings.txt"
{
  grep -a 'mode: window_created' "$OUT/device-trace.txt" | tail -1
  grep -a 'mode: summary' "$OUT/device-trace.txt" | tail -1
  grep -a 'audio: summary' "$OUT/device-trace.txt" | tail -1
} > "$E/device-run-readings.txt" 2>/dev/null || echo "MISSING $OUT/device-trace.txt" > "$E/device-run-readings.txt"
{
  head -1 "$OUT/winmm-devices.txt"
} > "$E/device-enumeration.txt" 2>/dev/null || true

# 4. The refusal probe's readings line: the one line the 4/5 section greps.
grep -a 'mp3PlaybackSeen=' "$OUT/scheduler.txt" | tail -1 > "$E/refusal-probe-readings.txt" 2>/dev/null || true

# 5. The census of MP3 files in the shipped sound archives: index-only, counts
#    by directory, so "which MP3 content exists at all" is a reading and not an
#    impression. Derived from the vpk_query listings in .scratch/t31/.
{
  echo "# T31 MP3 census of the shipped sound archives (VPK index only, no decode)."
  echo "# command: native/build-nmake/vpk_query.exe \"\$TF\" sound .mp3"
  echo "# The five archives vpk_query opens: pak01, tf2_misc, tf2_textures,"
  echo "# tf2_sound_misc, tf2_sound_vo_english."
  if [ -s "$SCR/vpk-mp3-all.txt" ]; then
    echo "total_mp3_entries=$(grep -c '^    sound/' "$SCR/vpk-mp3-all.txt" || true)"
    echo "# entries per top-level directory under sound/ (count, directory):"
    grep '^    sound/' "$SCR/vpk-mp3-all.txt" | awk -F/ '{ n[$2]++ } END { for (k in n) printf "%-28s %d\n", k, n[k] }' | sort -k2,2nr
    echo "# per-archive line from vpk_query:"
    grep -E '^[a-z0-9_]+\.vpk:' "$SCR/vpk-mp3-all.txt" || true
  else
    echo "MISSING $SCR/vpk-mp3-all.txt"
  fi
} > "$E/mp3-census.txt"

# 5b. The scheduler's own per-tick accounting around the two MP3 ticks. This is
#     what separates "counted somewhere" from "dropped": at tick 4650 events_seen
#     rises by two and eventsMissingResource by two (the splash and its partner
#     are counted), while at 7675 events_seen rises by two and missing does not.
{
  echo "# scheduler advance lines around the two scheduled MP3 ticks (null-sink run)"
  echo "# columns: tick cursor seen played skippedVoice missing dup"
  grep -a 'audio: advance tick=464[89]\|audio: advance tick=465[0-2]' "$OUT/null-trace.txt" 2>/dev/null
  grep -a 'audio: advance tick=767[3-7]' "$OUT/null-trace.txt" 2>/dev/null
} > "$E/window-advance-lines.txt"

# 6. The run logs, copied whole: the mutation verdicts live in them. The
#    "first run" log is kept on purpose: it is the run that found the out-of-range
#    splash, i.e. the reading this round's product change came from (mp3Seen=1
#    with the second event counted missing instead of named).
copy_if_present "$SCR/gate-forward-2.log" "$E/gate-forward-first-run.log" "the run that found the inaudible splash"
copy_if_present "$SCR/gate-mutation-1.log" "$E/gate-mutation.log" "forward + mutation log (null sink)"
copy_if_present "$SCR/gate-device-1.log" "$E/gate-device.log" "forward + device + mutation log"
copy_if_present "$SCR/source-mutation.log" "$E/source-mutation.log" "source mutation + restore log"
copy_if_present "$SCR/verify-audio-mutation.log" "$E/verify-audio-mutation.log" "existing gate: --mutation"
copy_if_present "$SCR/verify-audio-device.log" "$E/verify-audio-device.log" "existing gate: --device --mutation"
# (corpus-scan-7.jsonl is written straight into this directory by the scan, so
# there is nothing to copy for it.)

# 7. Hashes of every committed evidence file (the report cites them).
(
  cd "$E"
  # The readings only: the recipe scripts and this README are tracked by git and
  # are not evidence about the product, so hashing them would blur the list.
  files=$(ls -1 | grep -v '^SHA256SUMS$' | grep -v '^collect.sh$' \
    | grep -v '^source-mutation.sh$' | grep -v '^README.md$' | grep -v '^\.' | sort)
  sha256sum $files > SHA256SUMS
  echo "SHA256SUMS:"; cat SHA256SUMS
)
echo "COLLECT=DONE"
