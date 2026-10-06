#!/usr/bin/env bash
# census-negative-test.sh -- prove CORPUS-CENSUS can fail.
#
# Why: the first version of corpus-census.py parsed only the `header=` line, so
# every counter came back None, the "must be zero" loop skipped them, and 24
# demos printed CORPUS-CENSUS=PASS while checking nothing. A verdict that cannot
# go red is not evidence. This script mutates the raw probe reports (no probe
# re-run, so it costs seconds) and asserts the verdict flips to FAIL.
#
# Usage: bash census-negative-test.sh
set -uo pipefail

cd "$(dirname "$0")"
PY="C:/Users/Administrator/.workbuddy-ai/binaries/python/versions/3.13.12/python.exe"
DEMOS="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf/demos"
SRC=evidence/corpus-calib
NEG=evidence/corpus-negative

fail=0
step() { printf '\n=== %s ===\n' "$1"; }
ok()   { printf '  OK   %s\n' "$1"; }
bad()  { printf '  FAIL %s\n' "$1"; fail=1; }

run() { # run <outdir>  -> echoes verdict
  "$PY" corpus-census.py --demos-dir "$DEMOS" --outdir "$1" --sample 24 --resume 2>&1
}

step "prepare: copy raw reports to $NEG"
rm -f "$NEG"/reports/*.txt 2>/dev/null
mkdir -p "$NEG/reports"
cp "$SRC"/reports/*.txt "$NEG/reports/"
BEFORE=$(cat "$NEG"/reports/*.txt | sha256sum | cut -d' ' -f1)
echo "  reports=$(ls "$NEG"/reports/*.txt | wc -l)  digest=$BEFORE"

step "baseline: unmodified reports must PASS"
OUT=$(run "$NEG")
echo "$OUT" | grep -E '^(clean|dirty|sum_packets)=' | sed 's/^/  /'
if grep -q '^CORPUS-CENSUS=PASS$' <<<"$OUT"; then ok "baseline PASS"; else bad "baseline not PASS"; fi

VICTIM=$(ls "$NEG"/reports/autorecord_*.txt | head -1)
echo "  victim=$VICTIM"

step "mutation A: entity_failures 0 -> 3 (must FAIL)"
cp "$VICTIM" "$VICTIM.orig"
sed -i 's/entity_failures=0/entity_failures=3/' "$VICTIM"
OUT=$(run "$NEG")
if grep -q '^CORPUS-CENSUS=FAIL$' <<<"$OUT"; then ok "verdict went red"; else bad "verdict stayed green"; fi
if grep -q 'entity_failures=3' <<<"$OUT"; then ok "failure names the counter"; else bad "failure does not name the counter"; fi
mv "$VICTIM.orig" "$VICTIM"

step "mutation B: remove malformed_packets entirely (must FAIL as missing:)"
cp "$VICTIM" "$VICTIM.orig"
sed -i 's/ malformed_packets=0//' "$VICTIM"
OUT=$(run "$NEG")
if grep -q '^CORPUS-CENSUS=FAIL$' <<<"$OUT"; then ok "verdict went red"; else bad "verdict stayed green"; fi
if grep -q 'missing:.*malformed_packets' <<<"$OUT"; then ok "reported as missing, not skipped"; else bad "missing field was not reported"; fi
mv "$VICTIM.orig" "$VICTIM"

step "mutation C: zero the packet count (must FAIL as no_packets_scanned)"
cp "$VICTIM" "$VICTIM.orig"
sed -i 's/packets_scanned=[0-9]*/packets_scanned=0/' "$VICTIM"
OUT=$(run "$NEG")
if grep -q 'no_packets_scanned' <<<"$OUT"; then ok "empty decode detected"; else bad "empty decode not detected"; fi
mv "$VICTIM.orig" "$VICTIM"

step "mutation D: POV classifier disagreement (must FAIL)"
# Pretend the header classifier mislabelled an autorecord_ file. The ground-truth
# check must catch it even though every counter is still zero.
cp "$VICTIM" "$VICTIM.orig"
sed -i 's/recording=POV/recording=SourceTV/' "$VICTIM"
OUT=$(run "$NEG")
if grep -q '^CORPUS-CENSUS=FAIL$' <<<"$OUT"; then ok "verdict went red"; else bad "verdict stayed green"; fi
if grep -q 'POV CLASSIFIER DISAGREEMENTS' <<<"$OUT"; then ok "disagreement reported"; else bad "disagreement not reported"; fi
mv "$VICTIM.orig" "$VICTIM"

step "mutation E: in-stream HLTV bit set but classifier still says POV (must FAIL)"
# This is the failure the stream-level fields exist to catch: an svc_ServerInfo
# that declares HLTV while the classifier keeps calling the demo POV.
cp "$VICTIM" "$VICTIM.orig"
sed -i 's/server_info_hltv=0/server_info_hltv=1/; s/source_tv_flag=0/source_tv_flag=1/' "$VICTIM"
OUT=$(run "$NEG")
if grep -q '^CORPUS-CENSUS=FAIL$' <<<"$OUT"; then ok "verdict went red"; else bad "verdict stayed green"; fi
if grep -q 'hltv_bit_ignored' <<<"$OUT"; then ok "reported as hltv_bit_ignored"; else bad "hltv bit not reported"; fi
mv "$VICTIM.orig" "$VICTIM"

step "mutation F: autorecord_ file reclassified as SourceTV on the stream path (must FAIL)"
cp "$VICTIM" "$VICTIM.orig"
sed -i 's/recording_stream=POV/recording_stream=SourceTV/' "$VICTIM"
OUT=$(run "$NEG")
if grep -q '^CORPUS-CENSUS=FAIL$' <<<"$OUT"; then ok "verdict went red"; else bad "verdict stayed green"; fi
if grep -q 'pov_ground_truth_violated' <<<"$OUT"; then ok "reported as pov_ground_truth_violated"; else bad "ground-truth violation not reported"; fi
mv "$VICTIM.orig" "$VICTIM"

step "mutation G: source_tv_flag disagrees with the HLTV bit (must FAIL)"
cp "$VICTIM" "$VICTIM.orig"
sed -i 's/source_tv_flag=0/source_tv_flag=1/' "$VICTIM"
OUT=$(run "$NEG")
if grep -q 'source_tv_flag=1' <<<"$OUT"; then ok "inconsistent flag reported"; else bad "inconsistent flag not reported"; fi
mv "$VICTIM.orig" "$VICTIM"

step "restore: reports must be byte-identical and PASS again"
AFTER=$(cat "$NEG"/reports/*.txt | sha256sum | cut -d' ' -f1)
if [ "$BEFORE" = "$AFTER" ]; then ok "digest unchanged ($AFTER)"; else bad "digest drifted: $BEFORE -> $AFTER"; fi
OUT=$(run "$NEG")
if grep -q '^CORPUS-CENSUS=PASS$' <<<"$OUT"; then ok "restored PASS"; else bad "restored run not PASS"; fi

printf '\n'
if [ "$fail" -eq 0 ]; then echo "CENSUS-NEGATIVE=PASS"; else echo "CENSUS-NEGATIVE=FAIL"; fi
exit "$fail"
