#!/usr/bin/env bash
# probe_attach.sh -- one class, one frame: is the held weapon visible AT the body?
# Usage: bash probe_attach.sh <class> <ent> <tick> <originX> <originY> <originZ> [yaw]
# Writes .scratch/t83-probe/<class>/ and prints the ledger lines and the pixel
# readings. Must run under the shared host lock (it launches a capture).
set -uo pipefail
cd "$(dirname "$0")"
CLASS=$1; ENT=$2; TICK=$3; OX=$4; OY=$5; OZ=$6; YAW=${7:-45}
PRODUCT=native/build-nmake/tf2_demo_native.exe
PY=C:/Users/Administrator/.dsh/dsh-runtimes/dsh-primary-runtime/dependencies/python/python.exe
BASE=evidence/t62-matrix/class-pixels

# T86/D-22: the product writes settings.ini and ui.ini on every normal exit.
# Both flags below name per-run copies outside the tree: a gate must not edit
# tracked evidence or the machine profile. gate-launch-hygiene-check.sh asserts
# the static shape; this block is the runtime side of the same contract.
ISO_DIR="$(mktemp -d)"
ISO_DIR_WIN="$(cygpath -m "$ISO_DIR")"
ISO_SETTINGS="$ISO_DIR_WIN/settings.ini"
ISO_UI="$ISO_DIR_WIN/ui.ini"
trap 'rm -rf "${ISO_DIR:-}"' EXIT
if [ -f "$BASE/settings.json" ]; then cp "$BASE/settings.json" "$ISO_DIR/settings.ini"; else echo "  ISO SEED absent: settings=$BASE/settings.json (product starts from defaults)" >&2; fi
if [ -f "$BASE/ui.json" ]; then cp "$BASE/ui.json" "$ISO_DIR/ui.ini"; else echo "  ISO SEED absent: ui=$BASE/ui.json (product starts from defaults)" >&2; fi

TF=${T83_WEAPON_TF_ROOT:-D:/SteamLibrary/steamapps/common/Team Fortress 2/tf}
DEMO=${T83_WEAPON_DEMO:-D:/TF2_Demo_Player/testdata/demos/bb841c6d379ff7c40d0c8baf99f59d8d_matcha-20260927-1347-cp_snakewater_final1.dem}
D=.scratch/t83-probe/$CLASS
mkdir -p "$D"
EYEX=$(awk -v x="$OX" -v a="$YAW" 'BEGIN { printf "%.1f", x - 250 * cos(a * 0.01745329252) }')
EYEY=$(awk -v y="$OY" -v a="$YAW" 'BEGIN { printf "%.1f", y - 250 * sin(a * 0.01745329252) }')
EYEZ=$(awk -v z="$OZ" 'BEGIN { printf "%.1f", z + 58 }')
echo "probe $CLASS ent=$ENT tick=$TICK eye=$EYEX,$EYEY,$EYEZ yaw=$YAW"
TRACE=$D/probe.trace
TF2_NATIVE_TRACE="$TRACE" timeout 1200 "$PRODUCT" \
  --tf-root "$TF" --hidden-window --audio-disabled \
  --settings-file "$ISO_SETTINGS" --ui-settings-file "$ISO_UI" \
  --demo "$DEMO" --capture-tick "$TICK" --playback-speed 8 \
  --capture-frame "$D/probe.bmp" \
  --debug-camera "$EYEX" "$EYEY" "$EYEZ" 4.1 "$YAW" \
  --player-animation --world-entity-id --player-weapon-attach on \
  > "$D/probe.log" 2>&1
rc=$?
echo "rc=$rc"
grep -a 'scene: weaponAttach ' "$TRACE" | tail -2
grep -a 'weaponAttach: ' "$TRACE" | tail -6
grep -a 'entity: id=' "$TRACE" | grep -E 'attach=1|models/player/' | head -20 > "$D/ledger.txt"
{
  echo "tag=probe class=$CLASS rc=$rc"
  grep -a 'scene: weaponAttach ' "$TRACE" | tail -1
  grep -a 'weaponAttach: ' "$TRACE" | tail -6
  cat "$D/ledger.txt"
} > "$D/probe.summary"
rm -f "$TRACE"
sha256sum "$D/probe.bmp" 2>/dev/null
"$PY" t83-weapon-pixels.py "$D/probe.bmp" "$D/probe.summary" "$ENT"
echo "probe-pixels rc=$?"
