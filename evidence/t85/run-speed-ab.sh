#!/usr/bin/env bash
# T85 P0: the --playback-speed invariance A/B. WRITTEN FOR THE MERGE WINDOW, NOT RUN
# on task/t85-budget (lead m01857 item 3 + m01908 item 3).
#
# WHAT IT IS FOR. outline-check.sh asserts that the same capture tick rendered at
# speed=1 and at speed=8 is byte-identical ("--playback-speed does not change the
# frame"). On 2026-11-07 that assertion went red in a full lease and the cause was
# NOT the playback speed: the speed=1 bitmap was 2560x1417 while every other capture
# of the same lease was 1264x681 (both read out of the BMP header, 24 bpp). The
# speed=1 run is the long one on this fixture (measured: 999 s for one capture), and
# a 2560x1440 window minus a 23 px title bar is exactly 2560x1417, so the product's
# window was resized underneath the run -- WM_DPICHANGED at native/src/main.cpp:1407
# calls SetWindowPos with the suggested rect.
#
# TWO EXPLANATIONS, AND THEY MUST NOT BE CONFUSED:
#   (1) ENVIRONMENT: an external DPI/display event moves the window, and the frozen
#       release candidate shows the same thing on the same pair. Then this gate's
#       assertion is measuring the desktop, not the renderer, and the honest outcome
#       is a NAMED skip ("SKIP reason=dpi-changed from=... to=..."), never a silent
#       pass and never a deleted assertion.
#   (2) MY REGRESSION: the frozen candidate keeps one size and this branch does not.
#       Then the --window-size pin I added is wrong and must be fixed.
#
# WHAT IT MEASURES: four captures -- {frozen, mine} x {speed=1, speed=8} -- on the
# same demo/tick/camera/argv, and for each pair: the two BITMAP GEOMETRIES (read out
# of the BMP header, so it works for both binaries) and a byte comparison. Then one
# verdict token, whose reason line says which of the two worlds we are in:
#   SPEED-AB=PASS     both binaries: one size per pair, and mine is byte-identical
#   SPEED-AB=ENV      the frozen candidate ALSO changed size -> environment, skip it
#   SPEED-AB=FAIL     the frozen candidate held its size and mine did not
#   SPEED-AB=DIAG     sizes agree everywhere but the bytes do not; a human reads it
#
# USAGE (this launches the product four times; it MUST run inside the host lock):
#   bash /d/TF2_Native_Worktrees/_shared/hostlock.sh run <owner> \
#     bash evidence/t85/run-speed-ab.sh --ab-speed
# --ab-speed is required on purpose: a bare invocation prints this usage and exits 2,
# so nobody spends four product launches (and about 40 minutes of wall clock) by
# accident. OWNER: t85-budget wrote this file; the LEAD runs it.
set -uo pipefail
cd "$(dirname "$0")/../.." || exit 1

if [ "${1:-}" != "--ab-speed" ]; then
  # Only the COMMENT lines: a usage dump that also prints "set -uo pipefail" and the
  # cd line reads like the script started running and then refused.
  sed -n '2,44p' "$0" | sed -n 's/^# \{0,1\}//p'
  echo "REFUSED: pass --ab-speed to actually run it (four product launches)."
  exit 2
fi

TF="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
DEMO="D:/TF2_Demo_Player/testdata/demos/bb841c6d379ff7c40d0c8baf99f59d8d_matcha-20260927-1347-cp_snakewater_final1.dem"
DEMO_SHA=""
BEFORE="D:/TF2_Native_Worktrees/t79-integration/native/build-nmake/tf2_demo_native.exe"
AFTER="native/build-nmake/tf2_demo_native.exe"
OUT="${T85_SPEED_AB_OUT:-evidence/t85/speed-ab}"
# The fixture pair outline-check.sh itself uses (outline-check.sh:819-821):
TICK=${T85_SPEED_AB_TICK:-64250}
CAM="5699 -1611 150 14.4 -143.3"
SPEED_LO=1
SPEED_HI=8
# The speed=1 capture is the slow one by construction (T52 measured 999 s on this
# fixture); T52_SLOW_TIMEOUT in the gate is 2400.
RUN_TIMEOUT="${T85_SPEED_AB_TIMEOUT:-2400}"

mkdir -p "$OUT/inputs"
echo "SPEED-AB driver: $0 (sha256 $(sha256sum "$0" | cut -d' ' -f1))"
echo "pwd=$PWD branch=$(git rev-parse --abbrev-ref HEAD 2>/dev/null) head=$(git rev-parse HEAD 2>/dev/null)"
echo "lock: $(bash /d/TF2_Native_Worktrees/_shared/hostlock.sh status 2>/dev/null | tr '\n' ' ')"
for path in "$TF" "$DEMO" "$BEFORE" "$AFTER" "$TF/maps/cp_snakewater_final1.bsp"; do
  [ -e "$path" ] || { echo "SPEED-AB=MISSING reason=no such input: $path"; exit 2; }
done
echo "demo sha256=$(sha256sum "$DEMO" | cut -d' ' -f1) bytes=$(stat -c%s "$DEMO")"
echo "frozen exe sha256=$(sha256sum "$BEFORE" | cut -d' ' -f1) mtime=$(stat -c%y "$BEFORE")"
echo "branch exe sha256=$(sha256sum "$AFTER" | cut -d' ' -f1) mtime=$(stat -c%y "$AFTER")"

# SETTINGS ISOLATION (lead m01207): the product rewrites recentDemo0..7 and its
# window/bookmark state on exit, so every run gets its OWN copy and the real profile
# in $LOCALAPPDATA is never the input. Same rule as the gates' run_frame.
SEED_DIR="$OUT/inputs"
HOST_PROFILE="${LOCALAPPDATA:-$HOME/AppData/Local}/TF2 Demo Player"
for pair in "host-settings.ini:settings.ini" "host-ui.ini:ui.ini"; do
  seed_name="${pair%%:*}"; host_name="${pair##*:}"
  if [ -f "$HOST_PROFILE/$host_name" ]; then cp "$HOST_PROFILE/$host_name" "$SEED_DIR/$seed_name"
  else : > "$SEED_DIR/$seed_name"; fi
done

bmp_size() { # bmp_size <bmp> -- "WxH" from the BMP header (works for both binaries)
  [ -f "$1" ] || { echo "<missing>"; return 0; }
  local w h
  w=$(od -An -tu4 -j18 -N4 "$1" 2>/dev/null | tr -d ' ')
  h=$(od -An -tu4 -j22 -N4 "$1" 2>/dev/null | tr -d ' ')
  echo "${w:-?}x${h:-?}"
}
bmp_bytes() { stat -c%s "$1" 2>/dev/null || echo 0; }

run_case() { # run_case <tag> <exe> <speed> <pin:yes|no>
  local tag="$1" exe="$2" speed="$3" pin="$4"
  local bmp="$OUT/$tag.bmp" trace="$OUT/$tag.trace" log="$OUT/$tag.log"
  local settings="$OUT/inputs/$tag.settings.ini" ui="$OUT/inputs/$tag.ui.ini"
  cp "$SEED_DIR/host-settings.ini" "$settings"
  cp "$SEED_DIR/host-ui.ini" "$ui"
  rm -f "$bmp" "$trace" "$log"
  local -a args=(
    --tf-root "$TF" --hidden-window --audio-disabled --demo "$DEMO"
    --capture-tick "$TICK" --playback-speed "$speed" --capture-frame "$bmp"
    --settings-file "$settings" --ui-settings-file "$ui"
    --outline-vertex-extent --debug-camera $CAM
    --spectate-show-corpses --outline on
  )
  # THE PIN IS THE DIFFERENCE UNDER TEST, so it is only ever passed to a binary that
  # has the switch. The frozen candidate predates --window-size and ignores unknown
  # arguments (unrecognised switches are a known product debt), so handing it the
  # flag would look like an A/B of the flag while actually being an A/B of nothing.
  if [ "$pin" = "yes" ]; then args+=(--window-size 1280x720); fi
  echo "---- $tag ($(basename "$exe") speed=$speed pin=$pin) ----"
  echo "  argv: $exe ${args[*]}"
  TF2_NATIVE_TRACE="$trace" timeout "$RUN_TIMEOUT" "$exe" "${args[@]}" > "$log" 2>&1
  local rc=$?
  echo "  rc=$rc bmp_bytes=$(bmp_bytes "$bmp") bmp_size=$(bmp_size "$bmp")"
  # The branch binary prints the size it actually captured at; the frozen one does
  # not, which is why the BMP header above is the symmetric reading.
  echo "  trace capture: $(grep -a 'capture: frame ' "$trace" 2>/dev/null | tail -1)"
  echo "  trace window : $(grep -a 'window: created' "$trace" 2>/dev/null | tail -1)"
  echo "$tag rc=$rc size=$(bmp_size "$bmp") bytes=$(bmp_bytes "$bmp")" >> "$OUT/table.txt"
}

: > "$OUT/table.txt"
run_case before-1 "$BEFORE" "$SPEED_LO" "no"
run_case before-8 "$BEFORE" "$SPEED_HI" "no"
run_case after-1  "$AFTER"  "$SPEED_LO" "yes"
run_case after-8  "$AFTER"  "$SPEED_HI" "yes"

pair_same_size() { [ "$(bmp_size "$OUT/$1.bmp")" = "$(bmp_size "$OUT/$2.bmp")" ]; }
pair_same_bytes() { [ -f "$OUT/$1.bmp" ] && [ -f "$OUT/$2.bmp" ] && cmp -s "$OUT/$1.bmp" "$OUT/$2.bmp"; }
say_pair() { # say_pair <label> <a> <b>
  local label="$1" a="$2" b="$3" sz_by="differ" by="differ"
  pair_same_size "$a" "$b" && sz_by="same"
  pair_same_bytes "$a" "$b" && by="same"
  echo "  $label: size=$sz_by ($(bmp_size "$OUT/$a.bmp") vs $(bmp_size "$OUT/$b.bmp")) bytes=$by"
}

echo "==== pairs ===="
say_pair "frozen speed=1 vs speed=$SPEED_HI" before-1 before-8
say_pair "branch speed=1 vs speed=$SPEED_HI" after-1 after-8
WINDOW_CLIENT=$(grep -a 'window: created' "$OUT/after-8.trace" 2>/dev/null | tail -1 | sed -n 's/.*client=\([0-9]*x[0-9]*\).*/\1/p')
echo "  branch pinned window client=${WINDOW_CLIENT:-<none>} branch captured=$(bmp_size "$OUT/after-8.bmp")"

if ! pair_same_size before-1 before-8; then
  echo "SPEED-AB=ENV reason=the frozen candidate also changed capture size ($(bmp_size "$OUT/before-1.bmp") vs $(bmp_size "$OUT/before-8.bmp")); the window was moved by the desktop, not by the renderer. Gate should SKIP with that reason, not delete the assertion."
elif ! pair_same_size after-1 after-8; then
  echo "SPEED-AB=FAIL reason=the frozen candidate held ONE size and this branch did not ($(bmp_size "$OUT/after-1.bmp") vs $(bmp_size "$OUT/after-8.bmp")); the --window-size pin is wrong."
elif [ -n "${WINDOW_CLIENT:-}" ] && [ "$(bmp_size "$OUT/after-8.bmp")" != "$WINDOW_CLIENT" ]; then
  echo "SPEED-AB=FAIL reason=the capture ($(bmp_size "$OUT/after-8.bmp")) is not the client rect the window was created at ($WINDOW_CLIENT) even though the pair agrees."
elif pair_same_bytes after-1 after-8; then
  echo "SPEED-AB=PASS reason=one size per pair, and this branch's speed=$SPEED_LO and speed=$SPEED_HI frames are byte-identical at $(bmp_size "$OUT/after-8.bmp")"
else
  echo "SPEED-AB=DIAG reason=sizes agree everywhere but this branch's frames still differ byte for byte; read $OUT/table.txt and the two traces before concluding"
fi
echo "table: $OUT/table.txt"
cat "$OUT/table.txt"
