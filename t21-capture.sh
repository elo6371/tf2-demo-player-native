#!/usr/bin/env bash
# t21-capture.sh <out-bmp> <out-trace> [extra product flags...]
#
# T21: one pinned frame, retried until the draw reports exactly tick 2000.
#
# WHY THIS FILE EXISTS
#   The gate needs the same frame many times: the fixed build, the pre-fix
#   control build, and four mutations. Every one of them has to be the same
#   camera or the comparison is not a comparison. `--capture-tick 2000` means
#   "the first draw with tick >= 2000", and on this recording that draw is
#   2001 or 2003 about half the time -- measured 2026-10-19, 8 of 13 attempts.
#   The camera has moved by then, so a pin measured at 2000 cannot be checked
#   against it. Retrying here keeps every capture in this task on one tick
#   instead of leaving it to luck per call site.
#
# Usage: bash t21-capture.sh .scratch/t21/fix.bmp .scratch/t21/fix.trace --world-albedo
# Exit:  0 = a frame was written by a draw that reported tick 2000; 1 = never.
set -uo pipefail
cd "$(dirname "$0")"

PRODUCT=native/build-nmake/tf2_demo_native.exe
TF="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
DEMO="$TF/demos/autorecord_2026-07-02_13-26-46.dem"
# --- isolated settings (D-22) ----------------------------------------------
# The product persists settings.ini and ui.ini under %LOCALAPPDATA%/TF Demo
# Player, so a run that does not redirect them shares state with the desktop
# player and with every other gate on the host -- and verify-release's machine
# guard compares that pair at the end of the chain, where one stray write turns
# the whole chain red. Each run gets a throwaway directory instead, seeded with
# the tracked gate fixture: an empty settings file is NOT "the defaults"
# (verify-iso.sh: preset falls back to 0 = Performance, which draws no sky), so a
# gate pointed at nothing would measure a different renderer than the frames it
# pins.
ISO_DIR=$(mktemp -d "${TMPDIR:-/tmp}/tf2-iso.XXXXXX") || {
  echo "FATAL: cannot create isolated settings directory" >&2; exit 2; }
. "$(dirname "$0")/verify-iso.sh"
iso_chain_exit "$ISO_DIR"
ISO_SETTINGS="$ISO_DIR/settings.ini"
ISO_UI="$ISO_DIR/ui.ini"
iso_seed_gate_settings "$ISO_DIR" || {
  echo "FATAL: cannot seed the isolated gate settings" >&2; exit 2; }

if [ "$#" -lt 2 ]; then
  echo "usage: bash t21-capture.sh <out.bmp> <out.trace> [flags...]" >&2
  exit 2
fi
BMP="$1"; TRACE="$2"; shift 2

BMP_WIN="$(cygpath -m "$PWD/$BMP")"
TRACE_WIN="$(cygpath -m "$PWD/$TRACE")"

for attempt in 1 2 3 4 5 6; do
  rm -f "$BMP" "$TRACE"
  TF2_NATIVE_TRACE="$TRACE_WIN" timeout 300 "$PRODUCT" --tf-root "$TF" --demo "$DEMO" \
    --settings-file "$ISO_SETTINGS" \
    --ui-settings-file "$ISO_UI" \
    --hidden-window --capture-tick 2000 --capture-frame "$BMP_WIN" --audio-disabled \
    --viewmodel models/weapons/v_models/v_scattergun_scout.mdl --viewmodel-render companion \
    "$@" > "${TRACE%.trace}.log" 2>&1
  rc=$?
  tick=$(grep -a "draw: begin tick=" "$TRACE" 2>/dev/null | tr -d '\r' | tail -1 \
    | sed -n 's/.*tick=\([-0-9]*\).*/\1/p')
  if [ "$rc" = 0 ] && [ "$tick" = "2000" ] && [ -s "$BMP" ]; then
    echo "T21-CAPTURE status=OK bmp=$BMP trace=$TRACE tick=2000 attempt=$attempt"
    exit 0
  fi
  echo "  (t21-capture attempt $attempt: rc=$rc tick=${tick:-none}, retrying for tick 2000)"
done
echo "T21-CAPTURE status=FAIL bmp=$BMP reason=no-tick-2000-draw"
exit 1
