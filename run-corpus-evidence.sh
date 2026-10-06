#!/usr/bin/env bash
# run-corpus-evidence.sh -- the two long corpus runs, in order.
#
#   1. oracle cross-check on a stratified sample of the real corpus
#      (independent Rust implementation, ~20 min at --sample 40)
#   2. entity_protocol_probe over every demo in the corpus
#      (~4 h for 1643 demos / 40.2 GB at 3 workers)
#
# They run one after the other on purpose. Both drive the same probe binary at
# several hundred MB of RSS per concurrent demo, and the machine had 7.7 GB free
# when this was written. Overlapping them risks an allocation failure, which the
# census would faithfully record as a failed demo -- a false red that costs more
# to diagnose than the 20 minutes saved.
#
# The census uses --resume, so re-running this after an interruption picks up
# where it stopped instead of starting over.
#
# Usage: bash run-corpus-evidence.sh [oracle-sample]
# Exit:  0 = both runs passed.
set -uo pipefail
cd "$(dirname "$0")"

PY="C:/Users/Administrator/.workbuddy-ai/binaries/python/versions/3.13.12/python.exe"
DEMOS="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf/demos"
SAMPLE="${1:-40}"
rc_all=0

echo "=== 1/2 oracle cross-check on a corpus sample (n=$SAMPLE) ==="
"$PY" oracle-corpus-check.py --sample "$SAMPLE" --workers 2 > evidence/oracle-corpus.log 2>&1
tail -18 evidence/oracle-corpus.log
if grep -q 'ORACLE-CORPUS=PASS' evidence/oracle-corpus.log; then
  echo "ORACLE-CORPUS=PASS"
else
  echo "ORACLE-CORPUS=FAIL"; rc_all=1
fi

echo
echo "=== 2/2 full corpus census ==="
"$PY" corpus-census.py --demos-dir "$DEMOS" --outdir evidence/corpus-full \
  --workers 3 --timeout 1800 --resume > evidence/corpus-full.log 2>&1
tail -30 evidence/corpus-full.log
if grep -q 'CORPUS-CENSUS=PASS' evidence/corpus-full.log; then
  echo "CORPUS-CENSUS=PASS"
else
  echo "CORPUS-CENSUS=FAIL"; rc_all=1
fi

echo
if [ "$rc_all" -eq 0 ]; then echo "CORPUS-EVIDENCE=PASS"; else echo "CORPUS-EVIDENCE=FAIL"; fi
exit "$rc_all"
