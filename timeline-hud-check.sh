#!/usr/bin/env bash
# I6 step 2 gate (2026-11-23): the match timeline strip drawn into the workspace HUD.
#
# What it pins:
#   1. the pure composer's geometry at the default window size, and that its kill
#      marks add up to the timeline's kills instead of being a separate count;
#   2. the playhead is placed from the PLAYBACK tick through the measured
#      server-to-entry offset, and a resize rescales the whole strip;
#   3. the minimap panel is given way to rather than drawn over;
#   4. a second map produces both stripe colours (snakewater is BLU 5-0, so it
#      cannot prove the RED stripe on its own);
#   5. the product draws the one strip its analysis measured, and only when asked;
#   6. pixels: the stripe, the kill ticks above and below the midline and the
#      playhead really are in the captured frame, and the frame from a run without
#      --analysis-scan does not carry them. A trace line is not drawing.
#   7. A3b: the per-tick row around ONE focused exception -- one cell per tick, its
#      arithmetic (a point inside the row answers the tick under it, not a proportional
#      guess along the axis), the hit test that decides whether a click is a seek at all,
#      and the product drawing that row when a focus is asked for.
#   8. A5: the review banner -- the five buttons' hit tests, the 0.25x lead, the 3-2-1
#      countdown, the hold, the two-second run and the wait, the automatic advance, the
#      restore on exit, and two captured frames (one with --hud-review, one without)
#      proving the banner is an overlay: they differ inside the bar and nowhere else.
#   9. H1: the broadcast overlay -- both scores and the round clock in a centred bar, a
#      roster per team with the medic first and a health bar per row, the followed player's
#      box -- and two captures proving that the overlay is painted and is the only thing
#      a run changes.
set -uo pipefail

cd "$(dirname "$0")" || exit 1
export LC_ALL=C
export TF2_NATIVE_WINDOW_SIZE="${TF2_NATIVE_WINDOW_SIZE:-1280x720}"

EXE=native/build-nmake/tf2_demo_native.exe
PROBE=native/build-nmake/demo_open_probe.exe
TF="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
DEMO="D:/TF2_Demo_Player/testdata/demos/bb841c6d379ff7c40d0c8baf99f59d8d_matcha-20260927-1347-cp_snakewater_final1.dem"
DEMO2="D:/TF2_Demo_Player/testdata/demos/a9630c6557468b24f0b8c2e7dab38ed8_match-20261030-2145-pl_upward_f12.dem"
OUT=".scratch/timeline-hud"

fail=0
mkdir -p "$OUT"
win() { cygpath -m "$1"; }

assert_eq() { # <label> <actual> <expected>
  if [ "${2:-<none>}" = "$3" ]; then
    echo "  OK   $1 = $3"
  else
    echo "  FAIL $1 = ${2:-<none>}, expected $3"
    fail=1
  fi
}
assert_gt() { # <label> <actual> <floor>
  if [ "${2:-x}" -gt "$3" ] 2>/dev/null; then
    echo "  OK   $1 = $2 (> $3)"
  else
    echo "  FAIL $1 = ${2:-<none>}, expected > $3"
    fail=1
  fi
}
assert_le() { # <label> <actual> <ceiling>
  if [ "${2:-x}" -le "$3" ] 2>/dev/null; then
    echo "  OK   $1 = $2 (<= $3)"
  else
    echo "  FAIL $1 = ${2:-<none>}, expected <= $3"
    fail=1
  fi
}
sfield() { # <line> <field>
  # Anchored on a word boundary, like nfield: unanchored, "y=[^ ]*" also matches the tail
  # of a token that merely ENDS in y, and tail -1 then returns the LAST such tail. A4's view
  # fields made that real: sfield y read -1 out of viewEntity=-1 (and 12 out of viewEntity=12
  # on the product line) while the field the gate wanted was there with the right value.
  printf '%s\n' "$1" | grep -oE "(^| )$2=[^ ]*" | tail -1 | sed 's/^ //' | cut -d= -f2
}
lfield() { # <line> <field> -- like sfield but keeps -1 and other dash-leading values
  printf '%s\n' "$1" | grep -oE "(^| )$2=[^ ]*" | tail -1 | sed 's/^ //' | cut -d= -f2
}
nfield() { # <line> <field> -- a NUMBER, matched on a word boundary. Used wherever the
  # value goes straight into $(( )): sfield's "x=[^ ]*" also matches inside a token like
  # client=1264x681, and arithmetic on "x681" kills the gate after it has done all the work.
  printf '%s\n' "$1" | grep -oE "(^| )$2=-?[0-9]+" | tail -1 | sed 's/^ //' | cut -d= -f2
}
need_number() { # <label> <value>
  if [ -z "\${2:-}" ]; then echo "  FAIL no number for $1"; fail=1; return 1; fi
  return 0
}

for path in "$TF" "$DEMO" "$DEMO2"; do
  [ -e "$path" ] || { echo "FATAL: missing input: $path"; exit 1; }
done

PY=""
for cand in \
  "C:/Users/Administrator/.workbuddy-ai/binaries/python/versions/3.13.12/python.exe" \
  "C:/Users/Administrator/AppData/Local/Programs/Python/Python310/python.exe" \
  python python3; do
  if command -v "$cand" > /dev/null 2>&1; then PY="$cand"; break; fi
done
[ -n "$PY" ] || { echo "FATAL: no python found" >&2; exit 2; }
ANALYZER="$OUT/strip_pixels.py"

cat > "$ANALYZER" <<'PYSRC'
# Reads a 24bpp BMP bottom-up and counts pixels within a tolerance of a colour in
# a named rectangle. All rectangles are in top-down client coordinates, the same
# space the HUD composes in; the file itself is bottom-up.
import struct, sys

def load(path):
    with open(path, "rb") as handle:
        data = handle.read()
    if data[:2] != b"BM":
        raise SystemExit("not a BMP: " + path)
    offset = struct.unpack_from("<I", data, 10)[0]
    width, height = struct.unpack_from("<ii", data, 18)
    depth = struct.unpack_from("<H", data, 28)[0]
    if depth != 24:
        raise SystemExit("expected 24bpp, got %d" % depth)
    stride = ((width * 3 + 3) // 4) * 4
    return data, offset, width, abs(height), stride, height > 0

targets = {
    "red":   (230, 51, 46),
    "blu":   (51, 107, 242),
    "white": (250, 250, 245),
    "mark":  (240, 66, 56),   # the A3 exception block (kAnomalyBlockColour)
    # A4. The compositor blends these over the strip panel, so the targets are the pixels the
    # frame actually carries, not the constants in timeline_hud.cpp.
    "alive": (73, 176, 98),   # kPresenceAliveColour over the panel
    "dead":  (107, 110, 120), # kPresenceDeadColour over the panel
    # Both of these are opaque as of A4's layout fix: over a translucent panel a
    # 0.55-alpha threshold line took its colour from the game pixels and matched one
    # colour on 23 of its 1,240 columns, so it could not be read or gated.
    "curve": (107, 133, 173), # kCurveColour at alpha 1.0
    "thresh": (153, 148, 133), # kCurveThresholdColour at alpha 1.0
}
tol = 10
data, offset, width, height, stride, bottom_up = load(sys.argv[1])
if len(sys.argv) != 4:
    raise SystemExit("usage: strip_pixels.py frame.bmp label rect")
label = sys.argv[2]
x, y, w, h = [int(round(float(v))) for v in sys.argv[3].split(",")]
counts = dict((name, 0) for name in targets)
for row in range(max(0, y), min(height, y + h)):
    file_row = (height - 1 - row) if bottom_up else row
    base_row = offset + file_row * stride
    for column in range(max(0, x), min(width, x + w)):
        base = base_row + column * 3
        blue, green, red = data[base], data[base + 1], data[base + 2]
        for name, target in targets.items():
            if (abs(red - target[0]) <= tol and abs(green - target[1]) <= tol
                    and abs(blue - target[2]) <= tol):
                counts[name] += 1
for name in targets:
    print("%s.%s=%d" % (label, name, counts[name]))
PYSRC

echo "=== 0/12 build ==="
TF2_IDENTITY_ALLOW_UNTRACKED=1 bash build-target.sh demo_open_probe tf2_demo_native > "$OUT/build.log" 2>&1
assert_eq "build rc" "$?" "0"
if [ "$fail" != "0" ]; then echo "TIMELINE-HUD=FAIL"; exit 1; fi
if grep -aqE "warning C[0-9]{4}|error C[0-9]{4}" "$OUT/build.log"; then
  echo "  FAIL the build printed a diagnostic"
  grep -aE "warning C[0-9]{4}|error C[0-9]{4}" "$OUT/build.log" | head -5
  fail=1
else
  echo "  OK   build log has no diagnostic"
fi

echo "=== 1/12 the composer on cp_snakewater, default window ==="
timeout 420 "$PROBE" --scan --motion-detect --timeline-hud "$(win "$DEMO")" > "$OUT/probe.txt" 2>&1
assert_eq "probe rc" "$?" "0"
tr -d "\r" < "$OUT/probe.txt" > "$OUT/probe.lf" && mv "$OUT/probe.lf" "$OUT/probe.txt"
head=$(grep -a "match-timeline valid=1" "$OUT/probe.txt" | tail -1)
rounds=$(sfield "$head" rounds)
kills=$(sfield "$head" kills)
offset=$(sfield "$head" offset)
assert_gt "timeline rounds" "$rounds" "0"
assert_gt "timeline kills" "$kills" "0"
strip=$(grep -a "^timeline-hud " "$OUT/probe.txt" | tail -1)
echo "  ..   $strip"
assert_eq "strip visible" "$(sfield "$strip" visible)" "1"
assert_eq "strip x" "$(sfield "$strip" x)" "12"
assert_eq "strip y" "$(sfield "$strip" y)" "632"
assert_eq "strip w" "$(sfield "$strip" w)" "1256"
# A3 grew the strip by the exception lane (12 px above the kill band, so the band keeps
# its 8 px height and the labels stay on the first 12 px); A3b grew it again by 8 px for
# the per-tick row, which could not use the old bottom slack because the BLU kill ticks
# already draw there. y moves with the height (the strip is anchored to the bottom edge).
assert_eq "strip h" "$(sfield "$strip" h)" "76"
assert_eq "track left" "$(sfield "$strip" trackX0)" "18"
assert_eq "track right" "$(sfield "$strip" trackX1)" "1262"
assert_eq "track top" "$(sfield "$strip" trackY)" "682"
assert_eq "track height" "$(sfield "$strip" trackH)" "8"
assert_eq "exception lane top" "$(sfield "$strip" laneY)" "652"
assert_eq "exception lane height" "$(sfield "$strip" laneH)" "6"
# A3b's row exists at a fixed place even when nothing is focused; what changes is what it
# draws (a base strip with no focus), not where it would draw it.
assert_eq "per-tick row top" "$(sfield "$strip" cellsY)" "700"
assert_eq "per-tick row height" "$(sfield "$strip" cellsH)" "6"
assert_eq "no focus unless asked" "$(nfield "$strip" focus)" "0"
assert_eq "unfocused row draws no cells" "$(nfield "$strip" cells)" "0"
# Every block the detector handed over is either drawn or counted as outside the axis,
# so the four numbers add up to the report's own list.
anom=$(nfield "$strip" anomalies)
hints=$(nfield "$strip" hints)
before=$(nfield "$strip" before)
after=$(nfield "$strip" after)
assert_gt "exception blocks" "$anom" "0"
assert_eq "blocks split into stalls and teleports" \
  "$(( $(nfield "$strip" stalls) + $(nfield "$strip" teleports) ))" "$anom"
assert_eq "lane marks reconcile with the report" "$((anom + hints + before + after))" \
  "$(( $(nfield "$head" anomalies) + $(nfield "$head" hints) ))"
assert_eq "bands == rounds" "$(sfield "$strip" bands)" "$rounds"
red=$(lfield "$strip" ticksRed)
blu=$(lfield "$strip" ticksBlu)
unknown=$(lfield "$strip" ticksUnknown)
assert_gt "RED kill ticks" "$red" "0"
assert_gt "BLU kill ticks" "$blu" "0"
assert_eq "red+blu+unknown == kills" "$((red + blu + unknown))" "$kills"
assert_eq "no playhead unless asked" "$(lfield "$strip" playheadVisible)" "0"
# One shape per thing drawn: the track, one band, its label and its winner stripe when it
# has one, one tick per kill, and the playhead when there is one. A kill that silently
# fell off the strip would break this even while every count above still agreed.
labelrows=$(grep -acE "^timeline-band .* label=1$" "$OUT/probe.txt")
stripes=$(grep -acE "^timeline-band .* winTeam=(2|3)( |$)" "$OUT/probe.txt")
assert_eq "shapes == track + lane + bands + labels + stripes + kills + marks" \
  "$(sfield "$strip" shapes)" \
  "$((1 + 1 + $(sfield "$strip" bands) + labelrows + stripes + kills + $(nfield "$strip" anomalies) + $(nfield "$strip" hints)))"
bandrows=$(grep -acE "^timeline-band index=" "$OUT/probe.txt")
assert_eq "one band row per band" "$bandrows" "$rounds"
bad_team=$(grep -aE "^timeline-band " "$OUT/probe.txt" | grep -avcE " winTeam=(2|3|-1)( |$)")
assert_eq "every band's winTeam is 2, 3 or -1" "$bad_team" "0"
awk -v left="$(sfield "$strip" trackX0)" -v right="$(sfield "$strip" trackX1)" '
  /^timeline-band / {
    x = -1; w = -1
    for (i = 1; i <= NF; ++i) { split($i, kv, "="); if (kv[1] == "x") x = kv[2]; if (kv[1] == "w") w = kv[2] }
    if (x < left || x + w > right + 1) { print "out of track: x=" x " w=" w; bad = 1 }
    if (nr > 0 && x < last_x) { print "bands out of order: " x " after " last_x; bad = 1 }
    last_x = x; nr++
  }
  END { exit bad ? 1 : 0 }' "$OUT/probe.txt" > "$OUT/band-check.txt" 2>&1
if [ "$?" = "0" ]; then
  echo "  OK   bands are ordered and inside the track"
else
  echo "  FAIL bands are ordered and inside the track"
  head -3 "$OUT/band-check.txt"
  fail=1
fi

echo "=== 2/12 the playhead and a resize ==="
timeout 420 "$PROBE" --scan --timeline-hud --playhead 30000 --client 1600x900 "$(win "$DEMO")" > "$OUT/probe-big.txt" 2>&1
assert_eq "big probe rc" "$?" "0"
tr -d "\r" < "$OUT/probe-big.txt" > "$OUT/probe-big.lf" && mv "$OUT/probe-big.lf" "$OUT/probe-big.txt"
strip2=$(grep -a "^timeline-hud " "$OUT/probe-big.txt" | tail -1)
echo "  ..   $strip2"
assert_eq "big strip visible" "$(sfield "$strip2" visible)" "1"
assert_eq "big strip w" "$(sfield "$strip2" w)" "1576"
assert_eq "big strip y" "$(sfield "$strip2" y)" "812"
assert_eq "big playhead visible" "$(lfield "$strip2" playheadVisible)" "1"
assert_eq "playhead server tick" "$(lfield "$strip2" playhead)" "$((30000 + offset))"
px=$(lfield "$strip2" playheadX)
assert_gt "playhead x inside the track" "$px" "$(( $(sfield "$strip2" trackX0) - 1 ))"
assert_le "playhead x inside the track (right)" "$px" "$(( $(sfield "$strip2" trackX1) + 1 ))"

echo "=== 3/12 the minimap panel keeps the strip from drawing over it ==="
timeout 420 "$PROBE" --scan --timeline-hud --client 900x600 --avoid 0,430,220,170 "$(win "$DEMO")" > "$OUT/probe-avoid.txt" 2>&1
assert_eq "avoid probe rc" "$?" "0"
tr -d "\r" < "$OUT/probe-avoid.txt" > "$OUT/probe-avoid.lf" && mv "$OUT/probe-avoid.lf" "$OUT/probe-avoid.txt"
strip3=$(grep -a "^timeline-hud " "$OUT/probe-avoid.txt" | tail -1)
echo "  ..   $strip3"
assert_eq "avoid strip visible" "$(sfield "$strip3" visible)" "1"
assert_gt "strip starts right of the panel" "$(sfield "$strip3" x)" "227"
assert_le "strip stays inside the client" "$(( $(sfield "$strip3" x) + $(sfield "$strip3" w) ))" "888"

echo "=== 4/12 a second map proves both stripe colours ==="
timeout 420 "$PROBE" --scan --motion-detect --timeline-hud "$(win "$DEMO2")" > "$OUT/probe-upward.txt" 2>&1
assert_eq "upward probe rc" "$?" "0"
tr -d "\r" < "$OUT/probe-upward.txt" > "$OUT/probe-upward.lf" && mv "$OUT/probe-upward.lf" "$OUT/probe-upward.txt"
head2=$(grep -a "match-timeline valid=1" "$OUT/probe-upward.txt" | tail -1)
stripu=$(grep -a "^timeline-hud " "$OUT/probe-upward.txt" | tail -1)
echo "  ..   $stripu"
assert_eq "upward bands == rounds" "$(sfield "$stripu" bands)" "$(sfield "$head2" rounds)"
red_bands=$(grep -acE "^timeline-band .*winTeam=2" "$OUT/probe-upward.txt")
blu_bands=$(grep -acE "^timeline-band .*winTeam=3" "$OUT/probe-upward.txt")
assert_gt "rounds won by RED" "$red_bands" "0"
assert_gt "rounds won by BLU" "$blu_bands" "0"
assert_eq "bands == RED + BLU + unknown" "$((red_bands + blu_bands + $(grep -acE "^timeline-band .*winTeam=-1" "$OUT/probe-upward.txt")))" "$(sfield "$stripu" bands)"
assert_gt "upward exception blocks" "$(nfield "$stripu" anomalies)" "0"
assert_eq "upward lane marks reconcile with the report" \
  "$(( $(nfield "$stripu" anomalies) + $(nfield "$stripu" hints) + $(nfield "$stripu" before) + $(nfield "$stripu" after) ))" \
  "$(( $(nfield "$head2" anomalies) + $(nfield "$head2" hints) ))"
assert_gt "upward RED kill ticks" "$(lfield "$stripu" ticksRed)" "0"
assert_gt "upward BLU kill ticks" "$(lfield "$stripu" ticksBlu)" "0"

echo "=== 5/12 the product draws the strip its analysis measured ==="
TF2_NATIVE_TRACE="$(win "$OUT/on.trace")" timeout 600 "$EXE" \
  --tf-root "$TF" --demo "$DEMO" --hidden-window --audio-disabled --playback-speed 8 \
  --settings-file "$(win "$OUT/on.ini")" --ui-settings-file "$(win "$OUT/on-ui.ini")" \
  --analysis-scan --hud-live --hud-timeline-focus 0 --hud-timeline-entity 12 \
  --capture-tick 1200 --capture-frame "$(win "$OUT/on.bmp")" \
  > "$OUT/on.log" 2>&1
assert_eq "product rc" "$?" "0"
tr -d "\r" < "$OUT/on.trace" > "$OUT/on.trace.lf"
assert_eq "product printed the strip once" "$(grep -ac "timeline: hud visible=1" "$OUT/on.trace.lf")" "1"
online=$(grep -a "timeline: hud visible=1" "$OUT/on.trace.lf" | tail -1)
echo "  ..   $online"
clientw=$(printf '%s\n' "$online" | grep -oE "client=[0-9]+x[0-9]+" | cut -d= -f2 | cut -dx -f1)
clienth=$(printf '%s\n' "$online" | grep -oE "client=[0-9]+x[0-9]+" | cut -d= -f2 | cut -dx -f2)
assert_gt "product client width" "$clientw" "0"
assert_eq "product rounds == probe rounds" "$(sfield "$online" rounds)" "$rounds"
assert_eq "product RED ticks == probe" "$(lfield "$online" red)" "$red"
assert_eq "product BLU ticks == probe" "$(lfield "$online" blu)" "$blu"
assert_eq "product unknown ticks == probe" "$(lfield "$online" unknown)" "$unknown"
assert_eq "product strip x" "$(sfield "$online" x)" "12"
assert_eq "product strip y" "$(sfield "$online" y)" "$((clienth - 88))"
assert_eq "product exception blocks == probe" "$(nfield "$online" anomalies)" "$anom"
assert_eq "product hints == probe" "$(nfield "$online" hints)" "$hints"
assert_eq "product lane marks reconcile with the report" \
  "$(( $(nfield "$online" anomalies) + $(nfield "$online" hints) + $(nfield "$online" before) + $(nfield "$online" after) ))" \
  "$(( $(nfield "$head" anomalies) + $(nfield "$head" hints) ))"
# A3b: the run asked for the first exception, so the product must have opened the row.
assert_eq "product per-tick focus is on" "$(nfield "$online" focus)" "1"
assert_gt "product draws the per-tick row" "$(nfield "$online" cells)" "0"
assert_gt "product row has anomalous cells" "$(nfield "$online" anomalyCells)" "0"
assert_eq "product strip w" "$(sfield "$online" w)" "$((clientw - 24))"
assert_eq "capture frame exists" "$([ -s "$OUT/on.bmp" ] && echo 1 || echo 0)" "1"
framew=$(od -An -tu4 -j18 -N4 "$OUT/on.bmp" | tr -d ' ')
frameh=$(od -An -tu4 -j22 -N4 "$OUT/on.bmp" | tr -d ' ')
assert_eq "frame width == client width" "$framew" "$clientw"
assert_eq "frame height == client height" "$frameh" "$clienth"

echo "=== 6/12 pixels: are the marks in the frame? ==="
TF2_NATIVE_TRACE="$(win "$OUT/off.trace")" timeout 600 "$EXE" \
  --tf-root "$TF" --demo "$DEMO" --hidden-window --audio-disabled --playback-speed 8 \
  --settings-file "$(win "$OUT/off.ini")" --ui-settings-file "$(win "$OUT/off-ui.ini")" \
  --hud-live --capture-tick 1200 --capture-frame "$(win "$OUT/off.bmp")" \
  > "$OUT/off.log" 2>&1
assert_eq "control product rc" "$?" "0"
tr -d "\r" < "$OUT/off.trace" > "$OUT/off.trace.lf"
assert_eq "control run printed no strip" "$(grep -ac "timeline: hud visible=" "$OUT/off.trace.lf")" "0"
assert_eq "control frame exists" "$([ -s "$OUT/off.bmp" ] && echo 1 || echo 0)" "1"

onlineY=$(nfield "$online" y)
onlineX=$(nfield "$online" x)
onlineW=$(nfield "$online" w)
onlineH=$(nfield "$online" h)
first=$(nfield "$head" first)
last=$(nfield "$head" last)
if need_number "product strip geometry" "$onlineY$onlineX$onlineW$onlineH" \
   && need_number "report tick window" "$first$last"; then
  trackY=$((onlineY + 50))
  trackX0=$((onlineX + 6))
  trackX1=$((onlineX + onlineW - 6))
# The strip's own playhead column is not in the product trace line (only the probe
# prints it), so derive it from the same arithmetic: playback 1200 + the offset the
# analysis measured, across the report's first..last server tick.
serverTick=$((1200 + offset))
playheadX=$((trackX0 + (serverTick - first) * (trackX1 - trackX0) / (last - first)))
echo "  ..   sampling rows around trackY=$trackY, playhead column ~$playheadX, tick $serverTick"
laneY=$((onlineY + 20))
cellsY=$((onlineY + 68))
span=$((trackX1 - trackX0))
"$PY" "$ANALYZER" "$(win "$OUT/on.bmp")" on_stripe "$((trackX0)),$((trackY - 3)),$span,3" > "$OUT/on-stripe.txt" 2>&1
"$PY" "$ANALYZER" "$(win "$OUT/on.bmp")" on_above "$((trackX0)),$((trackY - 8)),$span,8" > "$OUT/on-above.txt" 2>&1
"$PY" "$ANALYZER" "$(win "$OUT/on.bmp")" on_below "$((trackX0)),$((trackY + 9)),$span,12" > "$OUT/on-below.txt" 2>&1
"$PY" "$ANALYZER" "$(win "$OUT/on.bmp")" on_playhead "$((playheadX - 3)),$((trackY - 8)),7,24" > "$OUT/on-playhead.txt" 2>&1
"$PY" "$ANALYZER" "$(win "$OUT/on.bmp")" on_lane "$((trackX0)),$((laneY)),$span,7" > "$OUT/on-lane.txt" 2>&1
"$PY" "$ANALYZER" "$(win "$OUT/on.bmp")" on_cells "$((trackX0)),$((cellsY)),$span,7" > "$OUT/on-cells.txt" 2>&1
"$PY" "$ANALYZER" "$(win "$OUT/off.bmp")" off_stripe "$((trackX0)),$((trackY - 3)),$span,3" > "$OUT/off-stripe.txt" 2>&1
"$PY" "$ANALYZER" "$(win "$OUT/off.bmp")" off_above "$((trackX0)),$((trackY - 8)),$span,8" > "$OUT/off-above.txt" 2>&1
"$PY" "$ANALYZER" "$(win "$OUT/off.bmp")" off_below "$((trackX0)),$((trackY + 9)),$span,12" > "$OUT/off-below.txt" 2>&1
"$PY" "$ANALYZER" "$(win "$OUT/off.bmp")" off_playhead "$((playheadX - 3)),$((trackY - 8)),7,24" > "$OUT/off-playhead.txt" 2>&1
"$PY" "$ANALYZER" "$(win "$OUT/off.bmp")" off_lane "$((trackX0)),$((laneY)),$span,7" > "$OUT/off-lane.txt" 2>&1
"$PY" "$ANALYZER" "$(win "$OUT/off.bmp")" off_cells "$((trackX0)),$((cellsY)),$span,7" > "$OUT/off-cells.txt" 2>&1
cat "$OUT"/on-stripe.txt "$OUT"/on-above.txt "$OUT"/on-below.txt "$OUT"/on-playhead.txt "$OUT"/on-lane.txt "$OUT"/on-cells.txt | sed 's/^/  ..   on  /'
cat "$OUT"/off-stripe.txt "$OUT"/off-above.txt "$OUT"/off-below.txt "$OUT"/off-playhead.txt "$OUT"/off-lane.txt "$OUT"/off-cells.txt | sed 's/^/  ..   off /'
pix() { grep "^$1\.$3=" "$2" | cut -d= -f2; }
on_stripe_red=$(pix on_stripe "$OUT/on-stripe.txt" red)
on_stripe_blu=$(pix on_stripe "$OUT/on-stripe.txt" blu)
off_stripe_blu=$(pix off_stripe "$OUT/off-stripe.txt" blu)
on_above_red=$(pix on_above "$OUT/on-above.txt" red)
off_above_red=$(pix off_above "$OUT/off-above.txt" red)
on_below_blu=$(pix on_below "$OUT/on-below.txt" blu)
off_below_blu=$(pix off_below "$OUT/off-below.txt" blu)
on_playhead_white=$(pix on_playhead "$OUT/on-playhead.txt" white)
off_playhead_white=$(pix off_playhead "$OUT/off-playhead.txt" white)
on_lane_mark=$(pix on_lane "$OUT/on-lane.txt" mark)
off_lane_mark=$(pix off_lane "$OUT/off-lane.txt" mark)
# snakewater is 5-0 BLU, so every band's winner stripe is BLU: at least one per band.
assert_gt "BLU stripe pixels (one per band minimum)" "$on_stripe_blu" "$rounds"
assert_le "control frame has no BLU stripe" "$off_stripe_blu" "2"
assert_gt "RED kill marks above the midline" "$on_above_red" "$red"
# The stripe rows carry the RED ticks too (the tick is 7px tall and the stripe sits in its
# last 3 rows), so the red pixels there are one column per RED kill per row.
assert_gt "RED tick columns in the stripe rows" "$on_stripe_red" "$red"
assert_le "control frame has no RED kill marks" "$off_above_red" "2"
assert_gt "BLU kill marks below the midline" "$on_below_blu" "16"
assert_le "control frame has no BLU kill marks" "$off_below_blu" "2"
assert_gt "playhead is white at its column" "$on_playhead_white" "4"
assert_le "control frame has no playhead" "$off_playhead_white" "2"
# The exception lane is the one part of the strip the control run cannot have: the
# scan is off, so there is no lane base and no block. A block pixel here therefore
# proves the marks came from the analysis, not from the track's own colours.
assert_gt "exception blocks have pixels in the lane" "$on_lane_mark" "0"
assert_le "control frame has no exception blocks" "$off_lane_mark" "2"
# The per-tick row is the product's own drawing of the focused exception: same colour as
# the lane's blocks, so a mark pixel here can only come from the row being drawn.
on_cells_mark=$(pix on_cells "$OUT/on-cells.txt" mark)
off_cells_mark=$(pix off_cells "$OUT/off-cells.txt" mark)
assert_gt "per-tick row has pixels in the frame" "$on_cells_mark" "0"
assert_le "control frame has no per-tick row" "$off_cells_mark" "2"
fi

echo "=== 7/12 A4: the follow-only layers describe the followed player ==="
# The numbers are the detector's own: the curve reads census.turnRates, the same vector the
# amber hints come from, so a red bar and a hint cannot disagree. Entity 12 is a player the
# recording actually carries; the default probe follows nobody, which is the negative.
timeout 420 "$PROBE" --scan --motion-detect --match-timeline --timeline-hud --timeline-view 12 \
  --client 1280x720 "$(win "$DEMO")" > "$OUT/probe-view.txt" 2>&1
assert_eq "view probe rc" "$?" "0"
tr -d "\r" < "$OUT/probe-view.txt" > "$OUT/probe-view.lf" && mv "$OUT/probe-view.lf" "$OUT/probe-view.txt"
viewline=$(grep -a "^timeline-view " "$OUT/probe-view.txt" | tail -1)
viewstrip=$(grep -a "^timeline-hud " "$OUT/probe-view.txt" | tail -1)
echo "  ..   $viewline"
assert_eq "view is valid" "$(sfield "$viewline" valid)" "1"
assert_eq "view follows entity 12" "$(sfield "$viewline" entity)" "12"
assert_eq "presence runs" "$(sfield "$viewline" presence)" "22"
assert_eq "presence alive runs" "$(grep -ac "^timeline-presence .*state=1" "$OUT/probe-view.txt")" "21"
assert_eq "presence dead runs" "$(grep -ac "^timeline-presence .*state=2" "$OUT/probe-view.txt")" "1"
assert_eq "ticks the recording covers him alive" "$(sfield "$viewline" aliveTicks)" "50324"
assert_eq "ticks an observed death covers" "$(sfield "$viewline" deadTicks)" "286"
assert_eq "ticks with no image of him" "$(sfield "$viewline" noDataTicks)" "13736"
assert_eq "counted turn ticks" "$(sfield "$viewline" turns)" "23671"
assert_eq "turns over the detector threshold" "$(sfield "$viewline" turnsOver)" "12"
assert_eq "the sharpest turn" "$(sfield "$viewline" maxDeg)" "181"
assert_eq "no view while nobody is followed" "$(nfield "$strip" view)" "0"
assert_eq "no presence without a follow" "$(nfield "$strip" presenceRuns)" "0"
assert_eq "no curve without a follow" "$(nfield "$strip" turns)" "0"
assert_eq "presence band top is y+16" "$(nfield "$viewstrip" presenceY)" "$(( $(nfield "$viewstrip" y) + 16 ))"
assert_eq "presence band is 4px" "$(nfield "$viewstrip" presenceH)" "4"
assert_eq "curve band top is y+26" "$(nfield "$viewstrip" curveY)" "$(( $(nfield "$viewstrip" y) + 26 ))"
assert_eq "curve band is 12px" "$(nfield "$viewstrip" curveH)" "12"
assert_gt "the threshold line sits inside the curve band" \
  "$(nfield "$viewstrip" curveThresholdY)" "$(nfield "$viewstrip" curveY)"
# A4's layout fix exists because of this one relation: the curve band must end above the
# RED kill ticks, or an over-threshold bar and a RED kill would be the same red in the same
# rows (measured on the first version: both sat in rows 637-643 of the product frame).
assert_le "the curve band clears the red ticks" \
  "$(( $(nfield "$viewstrip" curveY) + $(nfield "$viewstrip" curveH) ))" \
  "$(( $(nfield "$viewstrip" trackY) - 8 ))"
assert_eq "the per-tick row did not move" "$(nfield "$viewstrip" cellsY)" "$(nfield "$strip" cellsY)"
assert_gt "the two layers add primitives" "$(nfield "$viewstrip" shapes)" "$(nfield "$strip" shapes)"

echo "=== 8/12 pixels: the two layers are in the frame ==="
if need_number "A4 pixel geometry" "$onlineY$trackX0$span"; then
  pix() { grep "^$1\.$3=" "$2" | cut -d= -f2; }
  presenceY=$((onlineY + 16))
  curveY=$((onlineY + 26))
  "$PY" "$ANALYZER" "$(win "$OUT/on.bmp")" on_presence \
    "$((trackX0)),$presenceY,$span,4" > "$OUT/on-presence.txt" 2>&1
  # The whole curve band, not the 3 rows above the track it used to share with the RED
  # ticks: the band is 12 rows tall and every bar is a 1 px column rising from its base.
  "$PY" "$ANALYZER" "$(win "$OUT/on.bmp")" on_bars \
    "$((trackX0)),$((curveY)),$span,12" > "$OUT/on-bars.txt" 2>&1
  "$PY" "$ANALYZER" "$(win "$OUT/on.bmp")" on_thresh \
    "$((trackX0)),$((curveY + 4)),$span,2" > "$OUT/on-thresh.txt" 2>&1
  "$PY" "$ANALYZER" "$(win "$OUT/on.bmp")" on_over \
    "$((trackX0)),$((curveY + 2)),$span,7" > "$OUT/on-over.txt" 2>&1
  "$PY" "$ANALYZER" "$(win "$OUT/off.bmp")" off_presence \
    "$((trackX0)),$presenceY,$span,4" > "$OUT/off-presence.txt" 2>&1
  cat "$OUT"/on-presence.txt "$OUT"/on-bars.txt "$OUT"/on-thresh.txt "$OUT"/on-over.txt \
    | sed "s/^/  ..   on  /"
  cat "$OUT"/off-presence.txt | sed "s/^/  ..   off /"
  on_alive=$(pix on_presence "$OUT/on-presence.txt" alive)
  on_dead=$(pix on_presence "$OUT/on-presence.txt" dead)
  on_curve=$(pix on_bars "$OUT/on-bars.txt" curve)
  on_thresh=$(pix on_thresh "$OUT/on-thresh.txt" thresh)
  on_over=$(pix on_over "$OUT/on-over.txt" mark)
  off_alive=$(pix off_presence "$OUT/off-presence.txt" alive)
  # Floors are from the frame these rows were calibrated on (cp_snakewater, tick 1200,
  # client 1264x681): alive 3852, dead 20. A green bar four rows tall across the whole axis
  # is ~3,850 px, so 3,000 is "the bar is there" and not a number tuned to one capture.
  assert_gt "alive presence has green pixels" "$on_alive" "3000"
  assert_gt "an observed death has grey pixels" "$on_dead" "8"
  assert_le "the control frame has no presence band" "$off_alive" "2"
  # Measured on the same frame with the opaque targets: curve 1017 over the whole 12 px band,
  # thresh 1216 in the two-row window (the line lands on one of the two rows and covers the
  # axis end to end), over 66 in the 7 rows a 181 degree bar reaches. The bars are 1 px wide
  # columns, so these are counts of columns, not areas. The pre-fix translucent threshold
  # matched a single colour on 23 of its 1,240 columns, which is why that target changed.
  assert_gt "curve bars sit above the track" "$on_curve" "700"
  assert_gt "the threshold line spans the axis" "$on_thresh" "900"
  assert_gt "over-threshold turns borrow the lane red" "$on_over" "40"
fi

echo "=== 9/12 A3b: the per-tick row and the click arithmetic ==="
# One focused run answers the row's geometry; a second (same flags, plus --hit) answers
# the hit test. Both are pure composer calls, so the two runs cannot disagree about layout.
timeout 420 "$PROBE" --scan --motion-detect --match-timeline --timeline-hud --focus 0 \
  "$(win "$DEMO")" > "$OUT/probe-focus.txt" 2>&1
assert_eq "focus probe rc" "$?" "0"
tr -d "\r" < "$OUT/probe-focus.txt" > "$OUT/probe-focus.lf" && mv "$OUT/probe-focus.lf" "$OUT/probe-focus.txt"
stripf=$(grep -a "^timeline-hud " "$OUT/probe-focus.txt" | tail -1)
headf=$(grep -a "match-timeline valid=1" "$OUT/probe-focus.txt" | tail -1)
echo "  ..   $stripf"
# Focusing the row must not move anything: it is drawn in space the strip already has.
assert_eq "focused strip y unchanged" "$(nfield "$stripf" y)" "632"
assert_eq "focused strip h unchanged" "$(nfield "$stripf" h)" "76"
assert_eq "focused row top unchanged" "$(nfield "$stripf" cellsY)" "700"
assert_eq "focus is on" "$(nfield "$stripf" focus)" "1"
anomrow=$(grep -aE "^timeline-anomaly " "$OUT/probe-focus.txt" | head -1)
anomstart=$(nfield "$anomrow" start)
anomend=$(nfield "$anomrow" end)
if need_number "the first anomaly's span" "$anomstart$anomend"; then
  expected_anom_cells=$((anomend - anomstart + 1))
  # One cell per tick of the exception itself, and 20 either side (section 9's 前后各
  # 20 tick). A row that missed a tick or widened the window fails here.
  assert_eq "one anomalous cell per anomalous tick" \
    "$(nfield "$stripf" anomalyCells)" "$expected_anom_cells"
  assert_eq "row spans 20 ticks either side of the exception" \
    "$(nfield "$stripf" cells)" "$((expected_anom_cells + 40))"
  cellfirst=$(grep -aE "^timeline-cell " "$OUT/probe-focus.txt" | head -1)
  assert_eq "row's first cell is 20 ticks before the exception" \
    "$(nfield "$cellfirst" tick)" "$((anomstart - 20))"
fi
cellorder=$(awk '
  /^timeline-cell / {
    tick = -1; x = -1
    for (i = 1; i <= NF; ++i) { split($i, kv, "="); if (kv[1] == "tick") tick = kv[2]; if (kv[1] == "x") x = kv[2] }
    if (nr > 0 && (tick != last_tick + 1 || x < last_x)) { print "broken at tick " tick; bad = 1 }
    last_tick = tick; last_x = x; nr++
  }
  END { if (nr == 0) { print "no cell rows"; bad = 1 } exit bad ? 1 : 0 }' "$OUT/probe-focus.txt" 2>&1)
if [ -z "$cellorder" ]; then
  echo "  OK   cells are one tick apart and left to right"
else
  echo "  FAIL cells are one tick apart and left to right"
  echo "$cellorder" | head -3
  fail=1
fi
# The row is the only shape a focus adds: everything A3 drew is still there, plus one
# rect per cell. A dropped mark under a focused strip would break this even while the
# unfocused section above still agreed.
labelrowsf=$(grep -acE "^timeline-band .* label=1$" "$OUT/probe-focus.txt")
stripesf=$(grep -acE "^timeline-band .* winTeam=(2|3)( |$)" "$OUT/probe-focus.txt")
assert_eq "focused shapes == unfocused shapes + cells" \
  "$(sfield "$stripf" shapes)" \
  "$((1 + 1 + $(sfield "$stripf" bands) + labelrowsf + stripesf + kills + $(nfield "$stripf" anomalies) + $(nfield "$stripf" hints) + $(nfield "$stripf" cells)))"
# Hit test: the point on the middle of an anomalous cell must answer that cell's tick --
# a row that answered with the axis position instead would land ~20 ticks off.
anomcell=$(grep -aE "^timeline-cell .* kind=anomaly " "$OUT/probe-focus.txt" | head -1)
cellx=$(nfield "$anomcell" x)
cellw=$(nfield "$anomcell" w)
celltick=$(nfield "$anomcell" tick)
cellcx=$((cellx + cellw / 2))
trackx0f=$(nfield "$stripf" trackX0)
trackx1f=$(nfield "$stripf" trackX1)
timeout 420 "$PROBE" --scan --motion-detect --match-timeline --timeline-hud --focus 0 \
  --hit "$cellcx,703" --hit "$trackx0f,686" --hit "5,5" \
  --hit "$((trackx1f + 40)),686" "$(win "$DEMO")" > "$OUT/probe-hit.txt" 2>&1
assert_eq "hit probe rc" "$?" "0"
tr -d "\r" < "$OUT/probe-hit.txt" > "$OUT/probe-hit.lf" && mv "$OUT/probe-hit.lf" "$OUT/probe-hit.txt"
hitcell=$(grep -a "^timeline-hit " "$OUT/probe-hit.txt" | sed -n 1p)
hitaxis=$(grep -a "^timeline-hit " "$OUT/probe-hit.txt" | sed -n 2p)
hitoff=$(grep -a "^timeline-hit " "$OUT/probe-hit.txt" | sed -n 3p)
hitright=$(grep -a "^timeline-hit " "$OUT/probe-hit.txt" | sed -n 4p)
echo "  ..   $hitcell"
assert_eq "a point on the row is inside the strip" "$(nfield "$hitcell" inside)" "1"
assert_eq "a point on the row answers the cell's tick" "$(nfield "$hitcell" tick)" "$celltick"
assert_eq "a point on the track is still a seek" "$(nfield "$hitaxis" inside)" "1"
assert_eq "the track's left edge answers the first tick" \
  "$(nfield "$hitaxis" tick)" "$(nfield "$headf" first)"
assert_eq "a point off the strip is not a seek" "$(nfield "$hitoff" inside)" "0"
assert_eq "a point right of the track is not a seek" "$(nfield "$hitright" inside)" "0"

echo "=== 10/12 A5: the review banner and the drive behind it ==="
# A5 (section 9.2, 逐条复查): the banner is a mode, not a decoration. What this pins is the
# whole loop -- the button arithmetic, the lead/countdown/hold/run/wait timings, the
# automatic advance, the restore on exit -- and then the only thing a trace cannot prove:
# that the banner is drawn INTO the frame as an overlay. Two captures differ inside the bar
# and nowhere else, so the strip, the world and the HUD are untouched by it.
timeout 420 "$PROBE" --scan --motion-detect --match-timeline --review-check "$(win "$DEMO")" \
  > "$OUT/review.txt" 2>&1
assert_eq "review probe rc" "$?" "0"
tr -d "\r" < "$OUT/review.txt" > "$OUT/review.lf" && mv "$OUT/review.lf" "$OUT/review.txt"
rcheck=$(grep -a "^review-check " "$OUT/review.txt" | tail -1)
rov=$(grep -a "^review-overlay " "$OUT/review.txt" | tail -1)
rmiss=$(grep -a "^review-miss " "$OUT/review.txt" | tail -1)
echo "  ..   $rcheck"
echo "  ..   $rov"
echo "  ..   $rmiss"
assert_eq "the review list is valid" "$(sfield "$rcheck" valid)" "1"
assert_gt "the review walks a list" "$(nfield "$rcheck" items)" "0"
assert_eq "the banner is parked" "$(nfield "$rov" active)" "1"
assert_eq "the banner has five buttons" "$(nfield "$rov" buttons)" "5"
assert_eq "the banner sits at the composer margin" "$(nfield "$rov" x),$(nfield "$rov" y)" "12,6"
assert_eq "the banner spans the client" "$(nfield "$rov" w)" "$((1280 - 24))"
assert_eq "the banner is one text row tall" "$(nfield "$rov" h)" "22"
assert_gt "the banner composed its primitives" "$(nfield "$rov" primitives)" "6"
rbuttons=$(grep -a "^review-button " "$OUT/review.txt")
assert_eq "five button lines" "$(printf '%s\n' "$rbuttons" | grep -ac "^review-button ")" "5"
btni=0
while [ "$btni" -lt 5 ]; do
  btline=$(printf '%s\n' "$rbuttons" | grep -a "^review-button index=$btni " | tail -1)
  assert_eq "button $btni hit-tests to itself" "$(nfield "$btline" hit)" "$btni"
  assert_eq "button $btni is 16 px tall" "$(nfield "$btline" h)" "16"
  btni=$((btni + 1))
done
prevbtn=$(printf '%s\n' "$rbuttons" | grep -a "^review-button index=0 " | tail -1)
exitbtn=$(printf '%s\n' "$rbuttons" | grep -a "^review-button index=4 " | tail -1)
# 4 gaps of 6 plus 4 buttons of 74 between the first and the last: the row is laid out from
# the right edge inwards, so this one subtraction pins all five x positions.
assert_eq "the buttons are laid out right to left" \
  "$(( $(nfield "$exitbtn" x) - $(nfield "$prevbtn" x) ))" "$((4 * 74 + 4 * 6))"
assert_eq "exit ends 8 px inside the bar" \
  "$(( $(nfield "$exitbtn" x) + $(nfield "$exitbtn" w) ))" \
  "$(( $(nfield "$rov" x) + $(nfield "$rov" w) - 8 ))"
# A click above the bar, or past the last button, must not be a button -- otherwise the row
# eats clicks meant for the strip and for the world.
assert_eq "a click above the bar is never a button" "$(nfield "$rmiss" above)" "$(nfield "$rmiss" limit)"
assert_eq "a click past the row is never a button" "$(nfield "$rmiss" past)" "$(nfield "$rmiss" limit)"
rsteps=$(grep -a "^review-step " "$OUT/review.txt")
firstpass=$(printf '%s\n' "$rsteps" | sed -n '1,/phase=wait/p')
rorder=$(printf '%s\n' "$firstpass" | sed -n 's/.*phase=\([a-z]*\).*/\1/p' | uniq | tr '\n' ',')
echo "  ..   phases: $rorder"
assert_eq "a pass is lead, countdown, hold, run, wait" "$rorder" "slow,countdown,hold,normal,wait,"
rcountdowns=$(printf '%s\n' "$firstpass" | sed -n 's/.*phase=countdown countdown=\([0-9]*\).*/\1/p' | tr '\n' ',')
assert_eq "the countdown counts 3, 2, 1" "$rcountdowns" "3,2,1,"
assert_between() { # <label> <value> <low> <high>
  if [ "${2:-x}" -ge "$3" ] 2>/dev/null && [ "${2:-x}" -le "$4" ] 2>/dev/null; then
    echo "  OK   $1 = $2 (in $3..$4)"
  else
    echo "  FAIL $1 = ${2:-<none>}, expected $3..$4"
    fail=1
  fi
}
rslow=$(printf '%s\n' "$firstpass" | grep -a "phase=countdown countdown=3" | tail -1)
rcd2=$(printf '%s\n' "$firstpass" | grep -a "phase=countdown countdown=2" | tail -1)
rcd1=$(printf '%s\n' "$firstpass" | grep -a "phase=countdown countdown=1" | tail -1)
rhold=$(printf '%s\n' "$firstpass" | grep -a "phase=hold" | tail -1)
rnorm=$(printf '%s\n' "$firstpass" | grep -a "phase=normal" | tail -1)
rwait=$(printf '%s\n' "$firstpass" | grep -a "phase=wait" | tail -1)
assert_between "the 0.25x lead is long enough to read" "$(nfield "$rslow" durationMs)" "4000" "9000"
assert_between "the countdown holds each digit" "$(nfield "$rcd2" durationMs)" "900" "1200"
assert_between "the last countdown digit holds" "$(nfield "$rcd1" durationMs)" "900" "1200"
# durationMs on a step line is the time spent in the phase being LEFT, so the hold's length
# arrives on the normal line and the run's length on the wait line.
assert_between "the hold sits on the item" "$(nfield "$rnorm" durationMs)" "900" "1200"
assert_between "the normal run plays two seconds" "$(nfield "$rwait" durationMs)" "1800" "2400"
# The three readings that ARE the pointing job: parked at 0.25x, sitting ON the item's tick,
# then released to full speed.
assert_eq "the lead is slow motion" "$(sfield "$rslow" speed)" "0.25"
assert_eq "the park is paused" "$(sfield "$rslow" paused)" "1"
assert_eq "the hold sits on the item's tick" "$(nfield "$rhold" tick)" "$(nfield "$rhold" start)"
assert_eq "the normal run is full speed" "$(sfield "$rnorm" speed)" "1"
assert_eq "the normal run is playing" "$(sfield "$rnorm" paused)" "0"
assert_gt "the wait outlives the item" "$(nfield "$rwait" tick)" "$(nfield "$rwait" end)"
rnav=$(grep -a "^review-nav " "$OUT/review.txt")
navnext=$(printf '%s\n' "$rnav" | grep -a "action=next" | tail -1)
navreplay=$(printf '%s\n' "$rnav" | grep -a "action=replay" | tail -1)
navauto=$(printf '%s\n' "$rnav" | grep -a "action=auto" | tail -1)
echo "  ..   $navnext"
echo "  ..   $navreplay"
assert_eq "next steps forward one item" "$(nfield "$navnext" index)" "$(( $(nfield "$navnext" from) + 1 ))"
assert_eq "next restarts from the lead" "$(sfield "$navnext" phase)" "slow"
assert_eq "replay keeps the item" "$(nfield "$navreplay" index)" "$(nfield "$navreplay" from)"
assert_eq "replay restarts from the lead" "$(sfield "$navreplay" phase)" "slow"
assert_eq "the auto toggle turns on" "$(sfield "$navauto" auto)" "1"
rauto=$(grep -a "^review-auto " "$OUT/review.txt" | tail -1)
echo "  ..   $rauto"
assert_eq "auto advances one item" "$(nfield "$rauto" to)" "$(( $(nfield "$rauto" from) + 1 ))"
assert_gt "auto lands on a real tick" "$(nfield "$rauto" ticks)" "0"
rrestore=$(grep -a "^review-restore " "$OUT/review.txt" | tail -1)
echo "  ..   $rrestore"
# Exit puts playback back on the tick the caller handed to openAt and leaves nothing drawn.
assert_eq "exit restores playback" "$(nfield "$rrestore" ok)" "1"
assert_eq "the restored tick is the saved one" "$(nfield "$rrestore" tick)" "$(nfield "$rrestore" saved)"
assert_eq "exit leaves the review idle" "$(sfield "$rrestore" active)" "0"
assert_eq "exit returns to the idle phase" "$(sfield "$rrestore" phase)" "idle"
TF2_NATIVE_TRACE="$(win "$OUT/review.trace")" timeout 600 "$EXE" \
  --tf-root "$TF" --demo "$DEMO" --hidden-window --audio-disabled --playback-speed 8 \
  --settings-file "$(win "$OUT/on.ini")" --ui-settings-file "$(win "$OUT/on-ui.ini")" \
  --analysis-scan --hud-live --hud-timeline-focus 0 --hud-timeline-entity 12 --hud-review 5 \
  --capture-tick 1200 --capture-frame "$(win "$OUT/review.bmp")" \
  > "$OUT/review-run.log" 2>&1
assert_eq "review capture rc" "$?" "0"
tr -d "\r" < "$OUT/review.trace" > "$OUT/review.trace.lf"
# The product's trace lines carry a leading timestamp, so these greps are NOT anchored --
# unlike the probe's own lines, which are.
assert_eq "the capture opened the banner once" "$(grep -ac "review active=1 " "$OUT/review.trace.lf")" "1"
rvarg=$(grep -a "review: active=1 reason=argument " "$OUT/review.trace.lf" | tail -1)
rvline=$(grep -a "review active=1 " "$OUT/review.trace.lf" | tail -1)
echo "  ..   $rvarg"
echo "  ..   $rvline"
assert_eq "the flag opened the banner on item 5" "$(nfield "$rvarg" index)" "5"
assert_eq "the banner sits on item 5" "$(nfield "$rvline" index)" "5"
assert_eq "the banner is waiting on the item" "$(sfield "$rvline" phase)" "wait"
assert_eq "the product banner spans the client" "$(nfield "$rvline" w)" "$((clientw - 24))"
assert_eq "the product banner is the composer's bar" \
  "$(nfield "$rvline" x),$(nfield "$rvline" y),$(nfield "$rvline" h)" "12,6,22"
assert_eq "the product banner has five buttons" "$(nfield "$rvline" buttons)" "5"
# The banner walks the marks the lane drew inside the axis. The lane's before/after columns
# are marks the axis clips, and the machine drops exactly those -- by design, not by bug.
onlane=$(grep -a "timeline: hud visible=1" "$OUT/on.trace.lf" | tail -1)
assert_eq "the banner walks the lane's in-axis marks" "$(nfield "$rvline" count)" \
  "$(( $(nfield "$onlane" anomalies) + $(nfield "$onlane" hints) ))"
# The control is the SAME run without --hud-review (section 5's frame): it must carry no
# banner at all, and the idle reading must draw no buttons.
assert_eq "the control run has no banner" "$(grep -ac "review active=1 " "$OUT/on.trace.lf")" "0"
ctlidle=$(grep -a "review active=0 " "$OUT/on.trace.lf" | tail -1)
echo "  ..   $ctlidle"
assert_eq "the control run draws no buttons" "$(nfield "$ctlidle" buttons)" "0"
cat > "$OUT/review-pix.py" <<'PYSRC'
import struct
import sys


def load(path):
    with open(path, "rb") as handle:
        data = handle.read()
    if data[:2] != b"BM":
        raise SystemExit("%s is not a BMP" % path)
    offset = struct.unpack_from("<I", data, 10)[0]
    width, height = struct.unpack_from("<ii", data, 18)
    bits = struct.unpack_from("<H", data, 28)[0]
    if bits not in (24, 32):
        raise SystemExit("%s is %d-bit; this helper reads 24 and 32" % (path, bits))
    stride = ((width * (bits // 8)) + 3) & ~3
    return width, abs(height), offset, stride, bits // 8, height > 0, data


def pixel(image, x, y):
    width, height, offset, stride, step, bottom_up, data = image
    row = (height - 1 - y) if bottom_up else y
    base = offset + row * stride + x * step
    return data[base + 2], data[base + 1], data[base]


mode = sys.argv[1]
if mode == "diff":
    a = load(sys.argv[2])
    b = load(sys.argv[3])
    x, y, w, h = (int(v) for v in sys.argv[4:8])
    total = outside = 0
    x0 = y0 = 1 << 30
    x1 = y1 = -1
    for row in range(min(a[1], b[1])):
        for col in range(min(a[0], b[0])):
            ra, ga, ba = pixel(a, col, row)
            rb, gb, bb = pixel(b, col, row)
            if abs(ra - rb) + abs(ga - gb) + abs(ba - bb) <= 3:
                continue
            total += 1
            if not (x <= col < x + w and y <= row < y + h):
                outside += 1
            if col < x0:
                x0 = col
            if col > x1:
                x1 = col
            if row < y0:
                y0 = row
            if row > y1:
                y1 = row
    print("diff.total=%d" % total)
    print("diff.outside=%d" % outside)
    print("diff.x0=%d" % (x0 if total else -1))
    print("diff.x1=%d" % x1)
    print("diff.y0=%d" % (y0 if total else -1))
    print("diff.y1=%d" % y1)
elif mode == "rect":
    image = load(sys.argv[2])
    x, y, w, h = (int(v) for v in sys.argv[3:7])
    count = white = bluish = reddish = 0
    sr = sg = sb = 0
    for row in range(y, min(y + h, image[1])):
        for col in range(x, min(x + w, image[0])):
            r, g, b = pixel(image, col, row)
            sr += r
            sg += g
            sb += b
            count += 1
            if r >= 190 and g >= 190 and b >= 190:
                white += 1
            if b > r + 30 and b > g + 10:
                bluish += 1
            if r > g + 30 and r > b + 10:
                reddish += 1
    if count == 0:
        raise SystemExit("empty rect")
    print("rect.count=%d" % count)
    print("rect.mean_r=%d" % round(sr / count))
    print("rect.mean_g=%d" % round(sg / count))
    print("rect.mean_b=%d" % round(sb / count))
    print("rect.white=%d" % white)
    print("rect.bluish=%d" % bluish)
    print("rect.reddish=%d" % reddish)
else:
    image = load(sys.argv[2])
    x, y = int(sys.argv[3]), int(sys.argv[4])
    r, g, b = pixel(image, x, y)
    print("at.r=%d" % r)
    print("at.g=%d" % g)
    print("at.b=%d" % b)
PYSRC
revexists=$([ -s "$OUT/review.bmp" ] && echo 1 || echo 0)
assert_eq "review frame exists" "$revexists" "1"
revw=$(od -An -tu4 -j18 -N4 "$OUT/review.bmp" | tr -d ' ')
assert_eq "the review frame is the control's size" "$revw" "$framew"
"$PY" "$OUT/review-pix.py" diff "$(win "$OUT/review.bmp")" "$(win "$OUT/on.bmp")" \
  12 6 "$((clientw - 24))" 22 > "$OUT/review-diff.txt" 2>&1
sed 's/^/  ..   /' "$OUT/review-diff.txt"
dnum() { grep "^diff.$1=" "$OUT/review-diff.txt" | cut -d= -f2; }
# The overlay claim, in one number: every pixel that differs between the two runs is inside
# the bar. Anything outside would mean opening the banner moved the world, the strip or the
# HUD -- so this is the assertion the capture exists for.
assert_eq "the banner is the only difference" "$(dnum outside)" "0"
assert_gt "the banner changed pixels" "$(dnum total)" "20000"
assert_eq "the banner starts at the margin" "$(dnum x0)" "12"
assert_eq "the banner ends at the far margin" "$(dnum x1)" "$((clientw - 13))"
assert_eq "the banner starts at the top row" "$(dnum y0)" "6"
assert_eq "the banner ends one row past its height" "$(dnum y1)" "27"
rsample() { # <label> <x> <y> <w> <h>
  "$PY" "$OUT/review-pix.py" rect "$(win "$OUT/review.bmp")" "$2" "$3" "$4" "$5" \
    > "$OUT/review-rect-$1.txt" 2>&1
  sed "s/^/  ..   $1 /" "$OUT/review-rect-$1.txt"
}
rrect() { grep "^rect.$2=" "$OUT/review-rect-$1.txt" | cut -d= -f2; }
# The bar's own two colours: a 1 px top edge and a translucent body. Both are means over a
# rect that carries no text, so they are stable to the pixel.
rsample edge 12 6 "$((clientw - 24))" 1
assert_between "the bar's top edge" "$(rrect edge mean_r)" "55" "85"
assert_between "the bar's top edge green" "$(rrect edge mean_g)" "60" "90"
rsample body 12 7 "$((clientw - 24))" 2
assert_between "the bar's body is dark" "$(rrect body mean_r)" "10" "22"
assert_between "the bar's body is dark green" "$(rrect body mean_g)" "12" "26"
# The buttons are the only saturated colour in the bar, and each states its job by hue: AUTO
# carries the mode tint, EXIT the destructive one, the three transport buttons stay neutral.
# The product prints every button's rect with the banner, so these samples read their own
# geometry instead of re-deriving it: the pixels then confirm what the trace claims.
rblines=$(grep -a "review-button " "$OUT/review.trace.lf")
assert_eq "the banner prints five button rects" "$(printf '%s\n' "$rblines" | grep -ac "review-button ")" "5"
rbtn() { printf '%s\n' "$rblines" | grep -a "review-button index=$1 " | tail -1; }
rprevbtn=$(rbtn 0)
rauto_btn=$(rbtn 3)
rexit_btn=$(rbtn 4)
echo "  ..   $rauto_btn"
rsample prev "$(nfield "$rprevbtn" x)" "$(nfield "$rprevbtn" y)" "$(nfield "$rprevbtn" w)" "$(nfield "$rprevbtn" h)"
rsample auto "$(nfield "$rauto_btn" x)" "$(nfield "$rauto_btn" y)" "$(nfield "$rauto_btn" w)" "$(nfield "$rauto_btn" h)"
rsample exit "$(nfield "$rexit_btn" x)" "$(nfield "$rexit_btn" y)" "$(nfield "$rexit_btn" w)" "$(nfield "$rexit_btn" h)"
assert_eq "the fourth button is auto" "$(sfield "$rauto_btn" label)" "AUTO"
assert_eq "the fifth button is exit" "$(sfield "$rexit_btn" label)" "EXIT"
assert_eq "exit ends 8 px inside the bar" \
  "$(( $(nfield "$rexit_btn" x) + $(nfield "$rexit_btn" w) ))" \
  "$(( $(nfield "$rvline" x) + $(nfield "$rvline" w) - 8 ))"
assert_le "a transport button stays neutral" "$(rrect prev bluish)" "5"
assert_le "a transport button is not red" "$(rrect prev reddish)" "5"
assert_gt "the auto button carries the mode tint" "$(rrect auto bluish)" "300"
assert_gt "the exit button is red" "$(rrect exit reddish)" "300"
# The sentence: the bar's near-white pixels against the control's, which has the same HUD
# behind it but no text. This is the one check that the item is NAMED, not merely boxed.
rsample band 12 6 "$((clientw - 24))" 22
"$PY" "$OUT/review-pix.py" rect "$(win "$OUT/on.bmp")" 12 6 "$((clientw - 24))" 22 \
  > "$OUT/review-rect-ctlband.txt" 2>&1
ctlband=$(grep "^rect.white=" "$OUT/review-rect-ctlband.txt" | cut -d= -f2)
assert_gt "the banner writes a sentence" "$(( $(rrect band white) - ctlband ))" "250"
echo "=== 11/12 H1: the broadcast overlay ==="
# H1 (section 7 of docs/UI-SHELL-DESIGN-2026-11-22.md): the overlay a broadcast picture has
# to state -- both scores and the round clock in one bar, a roster per team with a health bar
# on every row and the medic first because the charge is what a spectator watches, and the
# followed player's box -- plus the only thing a trace cannot prove: that it is painted, and
# that a run WITHOUT the switch changes nothing. The control is the same run, same tick,
# same flags, minus --hud-broadcast.
TF2_NATIVE_TRACE="$(win "$OUT/broadcast.trace")" timeout 600 "$EXE" \
  --tf-root "$TF" --demo "$DEMO" --hidden-window --audio-disabled --playback-speed 8 \
  --settings-file "$(win "$OUT/broadcast.ini")" --ui-settings-file "$(win "$OUT/broadcast-ui.ini")" \
  --analysis-scan --hud-live --hud-broadcast \
  --capture-tick 1200 --capture-frame "$(win "$OUT/broadcast.bmp")" \
  > "$OUT/broadcast-run.log" 2>&1
assert_eq "broadcast capture rc" "$?" "0"
tr -d "\r" < "$OUT/broadcast.trace" > "$OUT/broadcast.trace.lf"
bhud=$(grep -a "broadcast: hud active=1 " "$OUT/broadcast.trace.lf" | tail -1)
bobs=$(grep -a "broadcast: observe " "$OUT/broadcast.trace.lf" | tail -1)
# The trace carries one block per publish and the rows change with the tick, so the
# arithmetic below reads the LAST block -- the numbers the captured frame was drawn from.
# Counting the whole file would count every publish (this run publishes 57 times).
assert_gt "the overlay places a roster" "$(nfield "$bhud" rows)" "0"
brows=$(grep -a "broadcast: row " "$OUT/broadcast.trace.lf" | tail -"$(nfield "$bhud" rows)")
echo "  ..   $bhud"
echo "  ..   $bobs"
assert_eq "the switch turns the overlay on" "$(nfield "$bhud" active)" "1"
assert_eq "the overlay draws" "$(nfield "$bhud" visible)" "1"
assert_eq "the overlay is ready" "$(sfield "$bhud" reason)" "ready"
bsum() { # <line> <field> <index within the comma list>
  printf '%s\n' "$1" | grep -oE "(^| )$2=[0-9,-]+" | tail -1 | sed 's/^ //' | cut -d= -f2 | cut -d, -f"$3"
}
bclientw=$(printf '%s\n' "$bhud" | grep -oE "client=[0-9]+x[0-9]+" | cut -d= -f2 | cut -dx -f1)
bclienth=$(printf '%s\n' "$bhud" | grep -oE "client=[0-9]+x[0-9]+" | cut -d= -f2 | cut -dx -f2)
assert_eq "the overlay measures the control's client" "$bclientw" "$clientw"
barx=$(bsum "$bhud" bar 1); bary=$(bsum "$bhud" bar 2)
barw=$(bsum "$bhud" bar 3); barh=$(bsum "$bhud" bar 4)
hintx=$(bsum "$bhud" hint 1); hinty=$(bsum "$bhud" hint 2)
hintw=$(bsum "$bhud" hint 3); hinth=$(bsum "$bhud" hint 4)
blux=$(bsum "$bhud" columns 1); redx=$(bsum "$bhud" columns 2)
rowh=$(bsum "$bhud" row 1); rowgap=$(bsum "$bhud" row 2)
colw=$(nfield "$(printf '%s\n' "$brows" | head -1)" w)
# The bar is centred on the client and the hint hangs under it: the two things a director
# looks at first are placed from the frame, not from a magic pair of numbers.
bcentre=$(( 2 * barx + barw - bclientw ))
[ "$bcentre" -lt 0 ] && bcentre=$(( -bcentre ))
assert_le "the score bar is centred" "$bcentre" "2"
assert_gt "the hint sits under the bar" "$hinty" "$(( bary + barh - 1 ))"
assert_le "the hint does not float away from it" "$hinty" "$(( bary + barh + 12 ))"
# The two rosters hold the same margin: the columns are a pair, like the minimap's.
assert_eq "the rosters share a margin" "$(( bclientw - redx - colw ))" "$blux"
# Roster arithmetic: every player who made a row printed one, every row got exactly one class
# chip or its letter fallback, and both columns open with their medic.
assert_eq "the rows are the two rosters" "$(nfield "$bhud" rows)" \
  "$(( $(nfield "$bhud" blu) + $(nfield "$bhud" red) ))"
assert_eq "every placed row prints a line" "$(printf '%s\n' "$brows" | grep -ac "broadcast: row ")" \
  "$(nfield "$bhud" rows)"
assert_eq "the class chips account for every row" \
  "$(( $(nfield "$bhud" chips) + $(nfield "$bhud" chipFallbacks) ))" "$(nfield "$bhud" rows)"
assert_eq "each column opens with its medic" "$(nfield "$bhud" medic)" "2"
bbluefirst=$(printf '%s\n' "$brows" | grep -a "team=3 " | head -1)
bredfirst=$(printf '%s\n' "$brows" | grep -a "team=2 " | head -1)
assert_eq "the blue column starts with a medic" "$(nfield "$bbluefirst" medic)" "1"
assert_eq "the red column starts with a medic" "$(nfield "$bredfirst" medic)" "1"
assert_eq "the medics are those two first rows" \
  "$(printf '%s\n' "$brows" | grep -ac "medic=1 ")" "2"
# Row geometry: one pitch apart, all one width, and nothing straddles the two sides.
assert_eq "the rows share one width" \
  "$(printf '%s\n' "$brows" | grep -ac "w=$colw ")" "$(nfield "$bhud" rows)"
assert_eq "the blue column stays on the left" \
  "$(printf '%s\n' "$brows" | grep -a "team=3 " | grep -avc " x=$blux ")" "0"
assert_eq "the red column stays on the right" \
  "$(printf '%s\n' "$brows" | grep -a "team=2 " | grep -avc " x=$redx ")" "0"
# Rows are stacked, not piled: the second row starts exactly one pitch under the first.
bblue2=$(printf '%s\n' "$brows" | grep -a "team=3 " | sed -n 2p)
assert_eq "rows are one pitch apart" \
  "$(( $(nfield "$bblue2" y) - $(nfield "$bbluefirst" y) ))" "$(( rowh + rowgap ))"
# The followed player's box: bottom centre, below the rosters, inside the frame.
bobsx=$(nfield "$bobs" x); bobsy=$(nfield "$bobs" y)
obsw=$(nfield "$bobs" w); obsh=$(nfield "$bobs" h)
assert_gt "the box names a player" "$(nfield "$bobs" entity)" "0"
obscentre=$(( 2 * bobsx + obsw - bclientw ))
[ "$obscentre" -lt 0 ] && obscentre=$(( -obscentre ))
assert_le "the box is centred" "$obscentre" "2"
blast=$(printf '%s\n' "$brows" | tail -1)
assert_gt "the box sits under the rosters" "$bobsy" "$(( $(nfield "$blast" y) + rowh ))"
assert_le "the box stays in the frame" "$(( bobsy + obsh ))" "$bclienth"
# The control: the same run without the switch must publish nothing and paint nothing.
TF2_NATIVE_TRACE="$(win "$OUT/broadcast-control.trace")" timeout 600 "$EXE" \
  --tf-root "$TF" --demo "$DEMO" --hidden-window --audio-disabled --playback-speed 8 \
  --settings-file "$(win "$OUT/broadcast-control.ini")" \
  --ui-settings-file "$(win "$OUT/broadcast-control-ui.ini")" \
  --analysis-scan --hud-live \
  --capture-tick 1200 --capture-frame "$(win "$OUT/broadcast-control.bmp")" \
  > "$OUT/broadcast-control-run.log" 2>&1
assert_eq "broadcast control rc" "$?" "0"
tr -d "\r" < "$OUT/broadcast-control.trace" > "$OUT/broadcast-control.trace.lf"
assert_eq "a run without the switch publishes no overlay" \
  "$(grep -ac "broadcast: " "$OUT/broadcast-control.trace.lf")" "0"
cat > "$OUT/broadcast-pix.py" <<'PYSRC'
import struct
import sys


def load(path):
    with open(path, "rb") as handle:
        data = handle.read()
    if data[:2] != b"BM":
        raise SystemExit("%s is not a BMP" % path)
    offset = struct.unpack_from("<I", data, 10)[0]
    width, height = struct.unpack_from("<ii", data, 18)
    bits = struct.unpack_from("<H", data, 28)[0]
    if bits not in (24, 32):
        raise SystemExit("%s is %d-bit; this helper reads 24 and 32" % (path, bits))
    stride = ((width * (bits // 8)) + 3) & ~3
    return width, abs(height), offset, stride, bits // 8, height > 0, data


def pixel(image, x, y):
    width, height, offset, stride, step, bottom_up, data = image
    row = (height - 1 - y) if bottom_up else y
    base = offset + row * stride + x * step
    return data[base + 2], data[base + 1], data[base]


def inside(rects, col, row):
    for x, y, w, h in rects:
        if x <= col < x + w and y <= row < y + h:
            return True
    return False


mode = sys.argv[1]
if mode == "diff":
    a = load(sys.argv[2])
    b = load(sys.argv[3])
    rects = []
    for token in sys.argv[4:]:
        x, y, w, h = (int(v) for v in token.split(","))
        rects.append((x, y, w, h))
    total = outside = 0
    x0 = y0 = 1 << 30
    x1 = y1 = -1
    for row in range(min(a[1], b[1])):
        for col in range(min(a[0], b[0])):
            ra, ga, ba = pixel(a, col, row)
            rb, gb, bb = pixel(b, col, row)
            if abs(ra - rb) + abs(ga - gb) + abs(ba - bb) <= 3:
                continue
            total += 1
            if not inside(rects, col, row):
                outside += 1
            if col < x0:
                x0 = col
            if col > x1:
                x1 = col
            if row < y0:
                y0 = row
            if row > y1:
                y1 = row
    print("diff.total=%d" % total)
    print("diff.outside=%d" % outside)
    print("diff.x0=%d" % (x0 if total else -1))
    print("diff.x1=%d" % x1)
    print("diff.y0=%d" % (y0 if total else -1))
    print("diff.y1=%d" % y1)
else:
    image = load(sys.argv[2])
    x, y, w, h = (int(v) for v in sys.argv[3:7])
    count = white = bluish = reddish = greenish = 0
    for row in range(y, min(y + h, image[1])):
        for col in range(x, min(x + w, image[0])):
            r, g, b = pixel(image, col, row)
            count += 1
            if r >= 190 and g >= 190 and b >= 190:
                white += 1
            if b > r + 30 and b > g + 10:
                bluish += 1
            if r > g + 30 and r > b + 10:
                reddish += 1
            if g > r + 25 and g > b + 15:
                greenish += 1
    if count == 0:
        raise SystemExit("empty rect")
    print("rect.count=%d" % count)
    print("rect.white=%d" % white)
    print("rect.bluish=%d" % bluish)
    print("rect.reddish=%d" % reddish)
    print("rect.greenish=%d" % greenish)
PYSRC
bimg=$(win "$OUT/broadcast.bmp")
bctl=$(win "$OUT/broadcast-control.bmp")
# Every row of every column, both corridors, the hint and the box: the union of the rects
# the overlay ITSELF printed. A pixel outside this union would mean the overlay moved the
# world, the strip or the game's own HUD -- so outside=0 is the claim the pair exists for.
"$PY" "$OUT/broadcast-pix.py" diff "$bimg" "$bctl" \
  "$barx,$bary,$barw,$barh" "$hintx,$hinty,$hintw,$hinth" \
  "$blux,$(nfield "$bbluefirst" y),$colw,$(( $(nfield "$blast" y) + rowh - $(nfield "$bbluefirst" y) ))" \
  "$redx,$(nfield "$bredfirst" y),$colw,$(( $(nfield "$blast" y) + rowh - $(nfield "$bredfirst" y) ))" \
  "$bobsx,$bobsy,$obsw,$obsh" > "$OUT/broadcast-diff.txt" 2>&1
sed 's/^/  ..   /' "$OUT/broadcast-diff.txt"
bdnum() { grep "^diff.$1=" "$OUT/broadcast-diff.txt" | cut -d= -f2; }
assert_eq "the overlay is the only difference" "$(bdnum outside)" "0"
assert_gt "the overlay painted" "$(bdnum total)" "20000"
assert_eq "the overlay starts at the blue column" "$(bdnum x0)" "$blux"
assert_eq "the overlay ends at the red column's edge" "$(bdnum x1)" "$(( redx + colw - 1 ))"
assert_eq "the overlay starts at the bar" "$(bdnum y0)" "$bary"
assert_eq "the overlay ends at the followed player's box" "$(bdnum y1)" "$(( bobsy + obsh - 1 ))"
bsample() { # <label> <x> <y> <w> <h>
  "$PY" "$OUT/broadcast-pix.py" rect "$bimg" "$2" "$3" "$4" "$5" > "$OUT/broadcast-rect-$1.txt" 2>&1
  sed "s/^/  ..   $1 /" "$OUT/broadcast-rect-$1.txt"
}
brect() { grep "^rect.$2=" "$OUT/broadcast-rect-$1.txt" | cut -d= -f2; }
# The bar's team chips are the only saturated colour up there, one per side: that is the
# picture stating which half is which without reading a word.
bsample bluchip "$(( barx + 10 ))" "$(( bary + 4 ))" "22" "$(( barh - 8 ))"
bsample redchip "$(( barx + barw - 32 ))" "$(( bary + 4 ))" "22" "$(( barh - 8 ))"
assert_gt "the blue chip is blue" "$(brect bluchip bluish)" "30"
assert_le "the blue chip is not red" "$(brect bluchip reddish)" "4"
assert_gt "the red chip is red" "$(brect redchip reddish)" "30"
assert_le "the red chip is not blue" "$(brect redchip bluish)" "4"
# The clock is the one field in the bar that is always legible: white glyphs between the
# two scores, and a hint line under the bar naming what is being watched.
bsample clock "$(( barx + barw / 2 - 30 ))" "$(( bary + 4 ))" "60" "$(( barh - 8 ))"
assert_gt "the clock is written" "$(brect clock white)" "40"
bsample hint "$(( hintx + hintw / 2 - 75 ))" "$(( hinty - 2 ))" "150" "14"
assert_gt "the hint names the map" "$(brect hint white)" "80"
# The bar under a full-health row is green and covers its row; the bar under a damaged row
# is orange and stops where the health stops. Two samples, two colours, one width claim.
# Medic rows are excluded on purpose: since H1b their row-bottom bar is the MEDIGUN CHARGE
# (yellow while charging, white during an uber), so the health bars live on the other rows.
bfull=$(printf '%s\n' "$brows" | grep -a "medic=0 " | grep -a "fraction=1.000 " | grep -a "low=0 " | head -1)
blow=$(printf '%s\n' "$brows" | grep -a "medic=0 " | grep -a "low=1 " | head -1)
assert_gt "a row is at full health" "$(nfield "$bfull" health)" "0"
bsample full "$(( $(nfield "$bfull" x) + 15 ))" "$(( $(nfield "$bfull" y) + rowh - 6 ))" \
  "$(( colw - 27 ))" "4"
bsample low "$(( $(nfield "$blow" x) + 15 ))" "$(( $(nfield "$blow" y) + rowh - 6 ))" \
  "$(( colw - 27 ))" "4"
assert_gt "a full row's bar is green" "$(brect full greenish)" "150"
assert_gt "a damaged row's bar is orange" "$(brect low reddish)" "40"
# The followed player's box: a name is written in it, so the caption is not an empty plate.
bsample observe "$(( bobsx + 12 ))" "$(( bobsy + 3 ))" "$(( obsw - 24 ))" "14"
assert_gt "the box names its player" "$(brect observe white)" "40"
# H1b (design section 9.2 line 225): the medic's uber cell. The frame above has no uber in it --
# at tick 1200 nobody has charged -- so this pair of runs exists for the state the cell was
# asked for: playback tick 5800 is five seconds into RED medic entity 13's first uber, where
# the cell must state the seconds LEFT rather than the percentage, and the row's bar must turn
# from the charge's yellow to the uber's white and shrink with the countdown. The control is
# the same tick, same flags, without --hud-broadcast.
TF2_NATIVE_TRACE="$(win "$OUT/uber.trace")" timeout 600 "$EXE" \
  --tf-root "$TF" --demo "$DEMO" --hidden-window --audio-disabled --playback-speed 8 \
  --settings-file "$(win "$OUT/broadcast.ini")" --ui-settings-file "$(win "$OUT/broadcast-ui.ini")" \
  --analysis-scan --hud-live --hud-broadcast \
  --capture-tick 5800 --capture-frame "$(win "$OUT/uber.bmp")" \
  > "$OUT/uber-run.log" 2>&1
assert_eq "uber capture rc" "$?" "0"
tr -d "\r" < "$OUT/uber.trace" > "$OUT/uber.trace.lf"
uhud=$(grep -a "broadcast: hud active=1 " "$OUT/uber.trace.lf" | tail -1)
assert_gt "the uber run places a roster" "$(nfield "$uhud" rows)" "0"
urows=$(grep -a "broadcast: row " "$OUT/uber.trace.lf" | tail -"$(nfield "$uhud" rows)")
echo "  ..   $uhud"
assert_eq "the overlay knows an uber is firing" "$(nfield "$uhud" chargeActive)" "1"
uactive=$(printf '%s\n' "$urows" | grep -a "medic=1 " | grep -a "release=1 ")
assert_eq "one medic fires at the uber tick" "$(printf '%s\n' "$uactive" | grep -ac .)" "1"
echo "  ..   $uactive"
ufield() { # <line> <field> -- a FLOAT field, for the countdown's tenths
  printf '%s\n' "$1" | grep -oE "(^| )$2=[0-9.]+" | tail -1 | sed 's/^ //' | cut -d= -f2
}
ufbetween() { # <label> <value> <low> <high>
  if awk -v v="${2:-nan}" -v l="$3" -v h="$4" 'BEGIN{exit !(v >= l && v <= h)}'; then
    echo "  OK   $1 = $2 (in $3..$4)"
  else
    echo "  FAIL $1 = ${2:-<none>}, expected $3..$4"
    fail=1
  fi
}
# The countdown is read from the WHOLE trace, not the last publish: $urows holds only the
# block the capture frame was drawn from (one row per player), while the earlier publishes
# carry the release's first seconds. Grepping the file is what makes 'eight seconds' a fact.
ufirst=$(grep -a "broadcast: row " "$OUT/uber.trace.lf" | grep -a "entity=13 " | grep -a "release=1 " | head -1)
ulast=$(grep -a "broadcast: row " "$OUT/uber.trace.lf" | grep -a "entity=13 " | grep -a "release=1 " | tail -1)
assert_eq "the uber belongs to RED medic 13" "$(nfield "$uactive" entity)" "13"
echo "  ..   first $ufirst"
assert_between "the charge behind the uber" "$(nfield "$uactive" charge)" "70" "75"
assert_eq "a stock uber, not a kritz" "$(nfield "$uactive" chargeKind)" "0"
ufbetween "the uber starts at eight seconds" "$(ufield "$ufirst" chargeSec)" "7.5" "8.0"
ufbetween "the cell counts the seconds left" "$(ufield "$uactive" chargeSec)" "4.5" "5.5"
ureln=$(grep -a "broadcast: row " "$OUT/uber.trace.lf" | grep -ac "entity=13 .*release=1 ")
assert_gt "the countdown is published more than once" "$ureln" "1"
if awk -v a="$(ufield "$ufirst" chargeSec)" -v b="$(ufield "$ulast" chargeSec)" \
    'BEGIN{exit !(a > b)}'; then
  echo "  OK   the countdown comes down = $(ufield "$ufirst" chargeSec) -> $(ufield "$ulast" chargeSec)"
else
  echo "  FAIL the countdown comes down = $(ufield "$ufirst" chargeSec) -> $(ufield "$ulast" chargeSec)"
  fail=1
fi
uother=$(printf '%s\n' "$urows" | grep -a "medic=1 " | grep -a "release=0 ")
assert_eq "the other medic is not firing" "$(printf '%s\n' "$uother" | grep -ac .)" "1"
assert_between "the other medic still shows its charge" "$(nfield "$uother" charge)" "30" "40"
assert_eq "the other medic has no countdown" "$(nfield "$uother" chargeActive)" "0"
# The pixels, against the same tick without the switch: the white bar is the uber's own clock
# (200 of the 300 pixels in its 3 px band) and the yellow percentage beside the other medic's
# name is the charge the design keeps while a gun is still building. The control paints neither.
TF2_NATIVE_TRACE="$(win "$OUT/uber-control.trace")" timeout 600 "$EXE" \
  --tf-root "$TF" --demo "$DEMO" --hidden-window --audio-disabled --playback-speed 8 \
  --settings-file "$(win "$OUT/broadcast-control.ini")" \
  --ui-settings-file "$(win "$OUT/broadcast-control-ui.ini")" \
  --analysis-scan --hud-live \
  --capture-tick 5800 --capture-frame "$(win "$OUT/uber-control.bmp")" \
  > "$OUT/uber-control-run.log" 2>&1
assert_eq "uber control rc" "$?" "0"
tr -d "\r" < "$OUT/uber-control.trace" > "$OUT/uber-control.trace.lf"
assert_eq "the uber control publishes no overlay" \
  "$(grep -ac "broadcast: " "$OUT/uber-control.trace.lf")" "0"
uimg=$(win "$OUT/uber.bmp")
uimgc=$(win "$OUT/uber-control.bmp")
usample() { # <label> <x> <y> <w> <h> <image>
  "$PY" "$OUT/broadcast-pix.py" rect "$6" "$2" "$3" "$4" "$5" > "$OUT/uber-rect-$1.txt" 2>&1
  sed "s/^/  ..   $1 /" "$OUT/uber-rect-$1.txt"
}
usum() { grep "^rect.$2=" "$OUT/uber-rect-$1.txt" | cut -d= -f2; }
usample activecell 1214 278 40 15 "$uimg"
usample activebar 1088 292 100 3 "$uimg"
usample track 1192 292 55 3 "$uimg"
usample percentcell 150 279 32 14 "$uimg"
usample ctlcell 1214 278 40 15 "$uimgc"
usample ctlbar 1088 292 100 3 "$uimgc"
assert_gt "the uber cell is written white" "$(usum activecell white)" "15"
assert_gt "the uber bar is white and covers the seconds left" "$(usum activebar white)" "150"
assert_le "the bar stops where the seconds stop" "$(usum track white)" "2"
assert_gt "a charging gun keeps its yellow percentage" "$(usum percentcell reddish)" "20"
assert_le "the control paints no cell" "$(usum ctlcell white)" "2"
assert_le "the control paints no bar" "$(usum ctlbar white)" "2"
echo "=== 12/12 determinism ==="
timeout 420 "$PROBE" --scan --motion-detect --timeline-hud "$(win "$DEMO")" > "$OUT/probe2.txt" 2>&1
assert_eq "probe rerun rc" "$?" "0"
tr -d "\r" < "$OUT/probe2.txt" > "$OUT/probe2.lf" && mv "$OUT/probe2.lf" "$OUT/probe2.txt"
if cmp -s "$OUT/probe.txt" "$OUT/probe2.txt"; then
  echo "  OK   two probe runs are byte-identical"
else
  echo "  FAIL two probe runs differ"
  fail=1
fi

if [ "$fail" = "0" ]; then echo "TIMELINE-HUD=PASS"; exit 0; fi
echo "TIMELINE-HUD=FAIL"
exit 1
