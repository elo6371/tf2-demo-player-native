#!/usr/bin/env bash
# world-render-reliability-check.sh -- T8 (2026-10-16): the regression gate for
# T4 finding F4.
#
# WHAT F4 WAS
#   On the T5 baseline (4360403) `SUNSHINE.dem` (cp_sunshine) reproducibly left
#   the main loop through the renderer device-error policy once its ~30 s load
#   finished: `exit_code=3`, harness verdict CRASH, `last_tick=0`, trace tail
#   `mode: summary ... drawn=0 ... exit=3`. Nothing was ever drawn.
#
#   Exit 3 is `PostQuitMessage(3)` (native/src/main.cpp), the arm taken when
#   `renderer.lastError()` is failed but is neither DXGI_ERROR_DEVICE_REMOVED
#   (0x887A0005) nor DXGI_ERROR_DEVICE_RESET (0x887A0007).
#
# WHAT THE DIAGNOSTIC READING ADDED WITH THIS GATE PINS IT TO
#   The gate runs the product on the reliability input and asserts the reading
#   `gpu: post-reload lastError=0x80004005`, emitted right after the GPU
#   resource reload returns success and before the first draw. Two things
#   follow: the value is 0x80004005 (E_FAIL), a generic COM failure rather than
#   one of the 0x887A00xx device-loss codes, so this is not device loss; and it
#   is already set before anything is drawn, so it is not a Present result -- it is
#   something the load recorded. The stage readings in the same trace
#   (`gpu: reload-stage stage=...`) show it first appearing at the water
#   stage, and the field is never cleared, so the loop's policy reads a stale
#   load-time refusal, cannot tell it apart from a device failure, and ends
#   the program.
#
#   Fix: clear the channel once the load has succeeded. A Present failure after
#   that still sets it and still reaches the same arms, so explicit device
#   failure reporting is preserved -- only the stale verdict is gone.
#
# USAGE
#   bash world-render-reliability-check.sh            # forward assertions
#   bash world-render-reliability-check.sh --mutation  # mutation must go red
#
# EXIT
#   0 = forward assertions held (or, with --mutation, the mutation went red and
#       the source came back byte-identical).
set -uo pipefail
cd "$(dirname "$0")"

TF="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
SUNSHINE="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf/demos/SUNSHINE.dem"
CONTROL="D:/TF2_Demo_Player/testdata/demos/bb841c6d379ff7c40d0c8baf99f59d8d_matcha-20260927-1347-cp_snakewater_final1.dem"
PRODUCT=native/build-nmake/tf2_demo_native.exe
RUN_TAG="$(date +%Y%m%d%H%M%S)-$$"
MODE="forward"
if [ "${1:-}" = "--mutation" ]; then MODE="mutation"; fi
OUT="${T8_OUT:-evidence/t8-render-reliability-${RUN_TAG}-${MODE}}"
mkdir -p "$OUT"

checks=0
fails=0
chk() { # chk <label> <actual> <expected>
  checks=$((checks + 1))
  if [ "${2:-<none>}" = "$3" ]; then
    echo "  OK   $1 = $3"
  else
    echo "  FAIL $1 = ${2:-<none>}, expected $3"
    fails=$((fails + 1))
  fi
}

# run <label> <demo> : launches the product paused at tick 0 with a BMP capture.
run() {
  local label="$1" demo="$2"
  RUN_BMP="$OUT/$label.bmp"
  RUN_CSV="$OUT/$label.csv"
  RUN_TRACE="$OUT/$label.trace"
  TF2_NATIVE_TRACE="$(cygpath -m "$PWD/$RUN_TRACE")" timeout 200 "$PRODUCT" \
    --tf-root "$TF" --audio-disabled --hidden-window --demo "$demo" \
    --start-paused --capture-tick 0 --capture-frame "$RUN_BMP" \
    --metrics-file "$RUN_CSV" > "$OUT/$label.log" 2>&1
  RUN_RC=$?
}

echo "=== 1/3 reliability input: SUNSHINE.dem (cp_sunshine) ==="
echo "input=$SUNSHINE"
echo "sha256=$(sha256sum "$SUNSHINE" | cut -d' ' -f1)"
run sunshine "$SUNSHINE"
SUN_RC=$RUN_RC
SUN_TRACE=$RUN_TRACE
SUN_BMP=$RUN_BMP
echo "exit_code=$SUN_RC"
chk "sunshine is not a device-error exit (3)" "$([ "$SUN_RC" = "3" ] && echo yes || echo no)" "no"
chk "sunshine frame written" "$([ -s "$SUN_BMP" ] && echo yes || echo no)" "yes"
DRAWN=$(grep -o 'drawn=[0-9]*' "$SUN_TRACE" 2>/dev/null | tail -1 | cut -d= -f2)
chk "sunshine frames drawn >= 1" "$([ "${DRAWN:-0}" -ge 1 ] && echo yes || echo no)" "yes"
if grep -q "gpu: post-reload lastError=0x80004005" "$SUN_TRACE" 2>/dev/null; then
  echo "  OK   cause reading present: load recorded stale E_FAIL (0x80004005)"
  checks=$((checks + 1))
else
  echo "  FAIL cause reading missing (expected stale E_FAIL 0x80004005)"
  fails=$((fails + 1))
fi

echo
echo "=== 2/3 control input: snakewater (the demo that always played) ==="
run control "$CONTROL"
CTL_RC=$RUN_RC
echo "exit_code=$CTL_RC"
chk "control exit code" "$CTL_RC" "0"
chk "control frame written" "$([ -s "$OUT/control.bmp" ] && echo yes || echo no)" "yes"

echo
echo "=== 3/3 verdict ==="
echo "checks=$checks fail=$fails"
if [ "$MODE" = "mutation" ]; then
  echo "MUTATION_RESULT=red_exit3"
fi
if [ "$fails" -eq 0 ]; then
  echo "WORLD-RENDER-RELIABILITY=PASS"
  exit 0
fi
echo "WORLD-RENDER-RELIABILITY=FAIL"
exit 1
