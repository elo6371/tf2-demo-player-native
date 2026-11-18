#!/usr/bin/env bash
# mf_export_verify.sh -- task A (N27 export precondition) verification chain.
#
# Proves, in order:
#   1 the probe can dump the exact frame/PCM artifact it encodes from
#   2 encoding from that artifact reproduces the frame sequence (per-frame SHA-256)
#   3 two exports are NOT byte-identical while container timestamps follow wall time
#   4 normalizing only container creation/modification times makes them identical
#   5 decoding the export returns the same video frame count and a PCM stream that
#     matches the source within AAC tolerance
#
# Usage: bash native/tools/mf_export_verify.sh <mf_export_probe.exe> <work-dir>
# Exit:  0 all checks passed, 1 a check failed, 2 bad usage.
set -uo pipefail
if [ "$#" -lt 2 ]; then
  echo "usage: bash native/tools/mf_export_verify.sh <mf_export_probe.exe> <work-dir>"
  exit 2
fi
EXE="$1"
BASE="$2"
if [ ! -f "$EXE" ]; then echo "VERIFY=FAIL probe not found: $EXE"; exit 2; fi
rm -rf "$BASE"
mkdir -p "$BASE/inputs" "$BASE/norm" "$BASE/decoded_raw" "$BASE/decoded_norm"
fail=0
check() {
  name="$1"; want="$2"; got="$3"
  if [ "$want" = "$got" ]; then echo "CHECK $name ok ($got)";
  else echo "CHECK $name FAILED want=$want got=$got"; fail=1; fi
}

echo "=== 1 dump inputs"
"$EXE" --dump-inputs "$BASE/inputs" > "$BASE/01_dump.log" 2>&1
check dump_rc 0 "$?"

echo "=== 2 external digest of the dumped artifact"
( cd "$BASE/inputs" && sha256sum frames.rgb24 pcm.raw ) > "$BASE/02_external.sha256"
probe_frames=$(grep -o 'framesSha256=[0-9a-f]*' "$BASE/01_dump.log" | head -1 | cut -d= -f2)
probe_pcm=$(grep -o 'pcmSha256=[0-9a-f]*' "$BASE/01_dump.log" | head -1 | cut -d= -f2)
external_frames=$(awk 'NR==1{print $1}' "$BASE/02_external.sha256")
external_pcm=$(awk 'NR==2{print $1}' "$BASE/02_external.sha256")
check frames_sha_probe_vs_sha256sum "$probe_frames" "$external_frames"
check pcm_sha_probe_vs_sha256sum "$probe_pcm" "$external_pcm"

echo "=== 3 encode twice, second start in a later wall-clock second"
"$EXE" --encode-dump "$BASE/inputs" --mp4 "$BASE/raw_a.mp4" > "$BASE/03_encode_a.log" 2>&1
check encode_a_rc 0 "$?"
sleep 2
"$EXE" --encode-dump "$BASE/inputs" --mp4 "$BASE/raw_b.mp4" > "$BASE/04_encode_b.log" 2>&1
check encode_b_rc 0 "$?"
for tag in frameShaAllMatch=yes pcmShaMatch=yes videoSamplesWrittenMatch=yes; do
  if grep -q "$tag" "$BASE/03_encode_a.log"; then echo "CHECK encode_a_$tag ok"; else echo "CHECK encode_a_$tag FAILED"; fail=1; fi
done

echo "=== 4 negative control: raw exports must differ"
if cmp -s "$BASE/raw_a.mp4" "$BASE/raw_b.mp4"; then
  echo "CHECK raw_exports_differ FAILED (still byte identical; did a second elapse between encodes?)"; fail=1
else
  echo "CHECK raw_exports_differ ok (diff bytes=$(cmp -l "$BASE/raw_a.mp4" "$BASE/raw_b.mp4" | wc -l))"
fi

echo "=== 5 normalize container timestamps (independent step)"
"$EXE" --normalize "$BASE/raw_a.mp4" "$BASE/norm/norm_a.mp4" > "$BASE/05_normalize_a.log" 2>&1
check normalize_a_rc 0 "$?"
"$EXE" --normalize "$BASE/raw_b.mp4" "$BASE/norm/norm_b.mp4" > "$BASE/06_normalize_b.log" 2>&1
check normalize_b_rc 0 "$?"
check normalize_fields_zeroed 5 "$(grep -o 'fieldsZeroed=[0-9]*' "$BASE/05_normalize_a.log" | cut -d= -f2)"
check normalize_bytes_zeroed 40 "$(grep -o 'bytesZeroed=[0-9]*' "$BASE/05_normalize_a.log" | cut -d= -f2)"
check normalize_verify_nonzero 0 "$(grep -o 'verify_nonzero_fields=[0-9]*' "$BASE/05_normalize_a.log" | cut -d= -f2)"
check normalize_touched_bytes 40 "$(cmp -l "$BASE/raw_a.mp4" "$BASE/norm/norm_a.mp4" | wc -l)"

echo "=== 6 positive control: normalized exports must be identical"
if cmp -s "$BASE/norm/norm_a.mp4" "$BASE/norm/norm_b.mp4"; then
  echo "CHECK normalized_exports_equal ok (sha256=$(sha256sum "$BASE/norm/norm_a.mp4" | cut -d' ' -f1))"
else
  echo "CHECK normalized_exports_equal FAILED"; fail=1
fi

echo "=== 7 decode read-back against the dump"
"$EXE" --decode "$BASE/raw_a.mp4" --dump-decoded "$BASE/decoded_raw" --expect-dump "$BASE/inputs" > "$BASE/07_decode_raw.log" 2>&1
check decode_raw_rc 0 "$?"
"$EXE" --decode "$BASE/norm/norm_a.mp4" --dump-decoded "$BASE/decoded_norm" --expect-dump "$BASE/inputs" > "$BASE/08_decode_norm.log" 2>&1
check decode_norm_rc 0 "$?"
for log in "$BASE/07_decode_raw.log" "$BASE/08_decode_norm.log"; do
  if grep -q 'ANSWER videoFramesMatch=yes audioSamplesMatch=yes pcmSimilar=yes' "$log"; then
    echo "CHECK decode_answer ok ($(basename "$log"))"
  else
    echo "CHECK decode_answer FAILED ($(basename "$log"))"; fail=1
  fi
done

if [ "$fail" -eq 0 ]; then echo "VERIFY=PASS"; else echo "VERIFY=FAIL"; fi
exit "$fail"
