#!/usr/bin/env bash
# Locked session 2: rebuild the probe after the slot-envelope fix and re-read
# every fixture and corpus demo. Read-only for tracked sources.
set -uo pipefail
cd /d/TF2_Native_Worktrees/t100-userinfo
LOG=evidence/t100-userinfo/logs
RAW=evidence/t100-userinfo/raw
PY=C:/Users/Administrator/.workbuddy-ai/binaries/python/versions/3.13.12/python.exe
PROBE=./native/build-nmake/demo_open_probe.exe
DEMOS="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf/demos"

mkdir -p "$LOG" "$RAW"
echo "start=$(date '+%Y-%m-%dT%H:%M:%S')"
echo "head=$(git rev-parse HEAD)"
echo "branch=$(git rev-parse --abbrev-ref HEAD)"
echo "host_owner=$(cat /d/TF2_Native_Worktrees/_shared/host.lock/owner 2>/dev/null | tr -d '\r')"

bash build-target.sh demo_open_probe > "$LOG/build-probe-2.log" 2>&1
build_rc=$?
echo "build_rc=$build_rc"
tail -4 "$LOG/build-probe-2.log"
if [ "$build_rc" -ne 0 ]; then echo "STOPPING: build failed"; exit "$build_rc"; fi
sha256sum "$PROBE"

echo "--- fixtures ---"
"$PY" evidence/t100-userinfo/gen_fixtures.py evidence/t100-userinfo/fixtures
for fixture in good truncated shortpayload badname; do
  "$PROBE" --userinfo-fixture "evidence/t100-userinfo/fixtures/userinfo-$fixture.demmsg" > "$RAW/fixture-$fixture.txt" 2>&1
  echo "fixture=$fixture rc=$?"
  head -c 900 "$RAW/fixture-$fixture.txt"
  echo
done

echo "--- review1 layout ---"
"$PROBE" --scan --userinfo --userinfo-dump 3 "$DEMOS/review1.dem" > "$RAW/review1-layout.txt" 2>&1
echo "layout_rc=$?"
head -1 "$RAW/review1-layout.txt" | tr ' ' '\n' | grep '^userinfo_' | tr '\n' ' '
echo
echo "rows:"
grep -c '^[0-9]' "$RAW/review1-layout.txt"
head -6 "$RAW/review1-layout.txt" | grep '^[0-9]'
echo "..."
tail -4 "$RAW/review1-layout.txt" | grep '^[0-9]'

echo "--- corpus ---"
for demo in 73 gullyscout proc2 processdemo prodcutscout review1 review12 review44 SUNSHINE; do
  "$PROBE" --scan --userinfo --userinfo-at "0,2000,60000,600000" "$DEMOS/$demo.dem" > "$RAW/$demo.txt" 2>&1
  rc=$?
  line=$(grep -m1 'userinfo_table_id=' "$RAW/$demo.txt" || true)
  printf 'demo=%s rc=%s ' "$demo" "$rc"
  printf '%s' "$line" | tr ' ' '\n' | grep -E '^(userinfo_(table_id|create|updates|entries|slots|samples|named|failures|unparsed|missing_userdata|empty_names|text_slot_mismatches|duplicate_userids|max_entries|max_userdata_bits|layout_mask))=' | tr '\n' ' '
  printf '\n'
done

echo "end=$(date '+%Y-%m-%dT%H:%M:%S')"
