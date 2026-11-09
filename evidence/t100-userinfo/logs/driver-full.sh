#!/usr/bin/env bash
# One locked session for task-52: build the probe, take the layout reading, run
# every corpus demo through the userinfo report, and run the synthetic fixtures.
# Everything that launches an exe happens here, inside the host lock.
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
echo "owner=t100-userinfo"
echo "host_owner_file=$(cat /d/TF2_Native_Worktrees/_shared/host.lock/owner 2>/dev/null | tr -d '\r')"

bash build-target.sh demo_open_probe > "$LOG/build-probe-1.log" 2>&1
build_rc=$?
echo "build_rc=$build_rc"
tail -6 "$LOG/build-probe-1.log"
if [ "$build_rc" -ne 0 ]; then
  echo "STOPPING: build failed"
  exit "$build_rc"
fi
ls -l native/build-nmake/demo_open_probe.exe
sha256sum native/build-nmake/demo_open_probe.exe

echo "--- layout reading (review1, six raw blobs) ---"
"$PROBE" --scan --userinfo --userinfo-dump 6 "$DEMOS/review1.dem" > "$RAW/review1-layout.txt" 2>&1
echo "layout_probe_rc=$?"
head -c 4500 "$RAW/review1-layout.txt"
echo
grep -v '^path=' "$RAW/review1-layout.txt" | head -20

echo "--- fixtures ---"
"$PY" evidence/t100-userinfo/gen_fixtures.py evidence/t100-userinfo/fixtures
for fixture in good truncated shortpayload badname; do
  "$PROBE" --userinfo-fixture "evidence/t100-userinfo/fixtures/userinfo-$fixture.demmsg" \
    > "$RAW/fixture-$fixture.txt" 2>&1
  echo "fixture=$fixture rc=$?"
  head -c 1200 "$RAW/fixture-$fixture.txt"
  echo
done

echo "--- corpus ---"
for demo in 73 gullyscout proc2 processdemo prodcutscout review1 review12 review44 SUNSHINE; do
  "$PROBE" --scan --userinfo --userinfo-at "0,2000,60000,600000" "$DEMOS/$demo.dem" \
    > "$RAW/$demo.txt" 2>&1
  rc=$?
  line=$(head -c 200000 "$RAW/$demo.txt" | head -1)
  entries=$(printf '%s' "$line" | tr ' ' '\n' | grep -c '^userinfo_' || true)
  printf 'demo=%s rc=%s userinfo_fields=%s\n' "$demo" "$rc" "$entries"
  printf '%s' "$line" | tr ' ' '\n' | grep -E '^(userinfo_(table_id|create|updates|entries|slots|named|failures|unparsed|empty_names|name_mismatches|duplicate_userids|layout_mask))=' | tr '\n' ' '
  printf '\n'
done

echo "end=$(date '+%Y-%m-%dT%H:%M:%S')"
