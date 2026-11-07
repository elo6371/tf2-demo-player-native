#!/usr/bin/env bash
# T85 P0 (lead m01475): the mesh source's COVERAGE account, on the same recording,
# same class, same tick, same flags as evidence/t85/upward-pyro/{before,after}.trace.
#
# Why this script exists: "attach: meshSource supplied" fell 23 -> 13 between the
# frozen exe and this worktree's exe. supplied counts meshes the ATTACH stage had to
# BUILD; a model the world pass already prepared is resolvedFromWorld and just as
# present in the hand. The answer needs a ledger line, not arithmetic, so this run
# prints "attach: coverage requested=.. resolvedFromWorld=.. uploaded=.. refused=.."
# beside the existing "gpu: models done N uploaded=M".
#
# Usage (hostlock wraps it; it builds first):
#   bash /d/TF2_Native_Worktrees/_shared/hostlock.sh run t85-budget \
#     bash evidence/t85/run-coverage-after.sh
set -uo pipefail
cd "$(dirname "$0")/../.."
TF="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
DEMO="D:/TF2_Demo_Player/testdata/demos/a9630c6557468b24f0b8c2e7dab38ed8_match-20261030-2145-pl_upward_f12.dem"
SETTINGS="evidence/t85/inputs/upward-settings.json"
UI="evidence/t85/inputs/upward-ui.json"
OUT="evidence/t85/upward-pyro"
EXE="native/build-nmake/tf2_demo_native.exe"
ENT=3; TICK=30001; OX=1957.0; OY=-125.1; OZ=94.8; SPEED=32
mkdir -p "$OUT"

echo "driver sha256=$(sha256sum "$0" | cut -d' ' -f1)"
echo "branch=$(git rev-parse --abbrev-ref HEAD) head=$(git rev-parse HEAD)"
echo "demo sha256=$(sha256sum "$DEMO" | cut -d' ' -f1)"

echo "==== source patch: the coverage ledger (idempotent) ===="
if grep -q 'attachMeshResolvedFromWorld' native/src/main.cpp; then
  echo "patch: already applied (attachMeshResolvedFromWorld present)"
else
  echo "patch: main.cpp sha256 before=$(sha256sum native/src/main.cpp | cut -d' ' -f1)"
  python .scratch/patch-coverage.py native/src/main.cpp || { echo "COVERAGE-RUN=FAIL (patch)"; exit 1; }
  echo "patch: main.cpp sha256 after=$(sha256sum native/src/main.cpp | cut -d' ' -f1)"
fi

echo "==== build ===="
bash build-target.sh tf2_demo_native entity_overlay_probe > "$OUT/build-coverage.log" 2>&1
BUILD_RC=$?
echo "build rc=$BUILD_RC log=$OUT/build-coverage.log"
tail -3 "$OUT/build-coverage.log"
[ "$BUILD_RC" = 0 ] || { echo "COVERAGE-RUN=FAIL (build)"; exit 1; }
echo "exe sha256=$(sha256sum "$EXE" | cut -d' ' -f1) mtime=$(stat -c %y "$EXE")"

EYEX=$(awk -v x="$OX" 'BEGIN { printf "%.1f", x - 250 }')
EYEZ=$(awk -v z="$OZ" 'BEGIN { printf "%.1f", z + 58 }')

rm -f "$OUT/coverage.bmp" "$OUT/coverage.trace" "$OUT/coverage.log"
echo "==== run ===="
echo "argv: $EXE --tf-root $TF --hidden-window --audio-disabled --settings-file $SETTINGS --ui-settings-file $UI --demo $DEMO --capture-tick $TICK --playback-speed $SPEED --capture-frame $OUT/coverage.bmp --debug-camera $EYEX $OY $EYEZ 4.1 0.0 --player-animation --player-weapon-attach on"
TF2_NATIVE_TRACE="$OUT/coverage.trace" timeout 900 "$EXE" \
  --tf-root "$TF" --hidden-window --audio-disabled \
  --settings-file "$SETTINGS" --ui-settings-file "$UI" \
  --demo "$DEMO" --capture-tick "$TICK" --playback-speed "$SPEED" \
  --capture-frame "$OUT/coverage.bmp" \
  --debug-camera "$EYEX" "$OY" "$EYEZ" 4.1 0.0 \
  --player-animation --player-weapon-attach on \
  > "$OUT/coverage.log" 2>&1
RC=$?
T="$OUT/coverage.trace"
echo "rc=$RC bmp=$([ -f "$OUT/coverage.bmp" ] && echo yes || echo no) bmp_sha256=$(sha256sum "$OUT/coverage.bmp" 2>/dev/null | cut -d' ' -f1) trace=$(stat -c %s "$T" 2>/dev/null || echo 0)"

echo "==== layer A (prepare) ===="
grep -a 'budget: prepare' "$T" | tail -1
echo "==== world pass ===="
grep -a 'gpu: models done' "$T" | tail -1
echo "==== layer C (instances) ===="
grep -a 'budget: instances' "$T" | tail -1
echo "==== layer D (draw) ===="
grep -a 'budget: draw' "$T" | tail -1
echo "==== layer B (mesh table, settled) ===="
grep -a 'budget: meshTable' "$T" | tail -1
echo "==== attach coverage (the answer) ===="
grep -a 'attach: coverage' "$T" | tail -1
grep -a 'attach: meshSource' "$T" | tail -1
grep -a 'scene: weaponAttach ' "$T" | tail -1
echo "==== refusals / OOM ===="
echo "attach refused lines = $(grep -a -c 'attach: mesh result=refused' "$T" || true)"
echo "0x8007000E occurrences = $(grep -a -c '0x8007000E' "$T" || true)"
echo "budget: eviction lines = $(grep -a -c 'budget: eviction key=' "$T" || true)"
echo "==== coverage distribution over the recording ===="
grep -a -o 'attach: coverage requested=[0-9]* resolvedFromWorld=[0-9]* uploaded=[0-9]*' "$T" | sort | uniq -c | sort -rn | head -8
grep -a 'budget: \|attach: coverage\|attach: meshSource' "$T" | tail -60 > "$OUT/coverage.ledger.txt"
echo "ledger=$OUT/coverage.ledger.txt ($(wc -l < "$OUT/coverage.ledger.txt") lines)"

# Verdict: the coverage balance must hold on EVERY printed line, and no line may
# show a request that vanished without being named.
# fields when split on [= ]: 1 "attach:" 2 "coverage" 3 requested 4 value 5 resolvedFromWorld 6 v 7 uploaded 8 v 9 refused 10 v 11 retrySkipped 12 v 13 unresolvedPath 14 v
BAL=$(grep -a -o 'attach: coverage requested=[0-9]* resolvedFromWorld=[0-9]* uploaded=[0-9]* refused=[0-9]* retrySkipped=[0-9]* unresolvedPath=[0-9]*' "$T" | awk -F'[= ]' '{ if ($4 != $6+$8+$10+$12+$14) bad++ } END { print bad+0 }')
echo "coverage-balance-violations=$BAL"
echo "COVERAGE-RUN=FAIL" > /dev/null
if [ "$RC" = 0 ] && [ -f "$OUT/coverage.bmp" ] && [ "$BAL" = 0 ] && ! grep -aq 'budget: eviction key=' "$T"; then
  echo "COVERAGE-RUN=PASS"
  exit 0
fi
echo "COVERAGE-RUN=FAIL"
exit 1
