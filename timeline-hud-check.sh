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
  printf '%s\n' "$1" | grep -oE "$2=[^ ]*" | tail -1 | cut -d= -f2
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

echo "=== 0/7 build ==="
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

echo "=== 1/7 the composer on cp_snakewater, default window ==="
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
assert_eq "strip y" "$(sfield "$strip" y)" "652"
assert_eq "strip w" "$(sfield "$strip" w)" "1256"
# A3 grew the strip by the exception lane: 12 px of lane above the kill band, so the
# band keeps its 8 px height and the labels stay on the first 12 px.
assert_eq "strip h" "$(sfield "$strip" h)" "56"
assert_eq "track left" "$(sfield "$strip" trackX0)" "18"
assert_eq "track right" "$(sfield "$strip" trackX1)" "1262"
assert_eq "track top" "$(sfield "$strip" trackY)" "690"
assert_eq "track height" "$(sfield "$strip" trackH)" "8"
assert_eq "exception lane top" "$(sfield "$strip" laneY)" "672"
assert_eq "exception lane height" "$(sfield "$strip" laneH)" "6"
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

echo "=== 2/7 the playhead and a resize ==="
timeout 420 "$PROBE" --scan --timeline-hud --playhead 30000 --client 1600x900 "$(win "$DEMO")" > "$OUT/probe-big.txt" 2>&1
assert_eq "big probe rc" "$?" "0"
tr -d "\r" < "$OUT/probe-big.txt" > "$OUT/probe-big.lf" && mv "$OUT/probe-big.lf" "$OUT/probe-big.txt"
strip2=$(grep -a "^timeline-hud " "$OUT/probe-big.txt" | tail -1)
echo "  ..   $strip2"
assert_eq "big strip visible" "$(sfield "$strip2" visible)" "1"
assert_eq "big strip w" "$(sfield "$strip2" w)" "1576"
assert_eq "big strip y" "$(sfield "$strip2" y)" "832"
assert_eq "big playhead visible" "$(lfield "$strip2" playheadVisible)" "1"
assert_eq "playhead server tick" "$(lfield "$strip2" playhead)" "$((30000 + offset))"
px=$(lfield "$strip2" playheadX)
assert_gt "playhead x inside the track" "$px" "$(( $(sfield "$strip2" trackX0) - 1 ))"
assert_le "playhead x inside the track (right)" "$px" "$(( $(sfield "$strip2" trackX1) + 1 ))"

echo "=== 3/7 the minimap panel keeps the strip from drawing over it ==="
timeout 420 "$PROBE" --scan --timeline-hud --client 900x600 --avoid 0,430,220,170 "$(win "$DEMO")" > "$OUT/probe-avoid.txt" 2>&1
assert_eq "avoid probe rc" "$?" "0"
tr -d "\r" < "$OUT/probe-avoid.txt" > "$OUT/probe-avoid.lf" && mv "$OUT/probe-avoid.lf" "$OUT/probe-avoid.txt"
strip3=$(grep -a "^timeline-hud " "$OUT/probe-avoid.txt" | tail -1)
echo "  ..   $strip3"
assert_eq "avoid strip visible" "$(sfield "$strip3" visible)" "1"
assert_gt "strip starts right of the panel" "$(sfield "$strip3" x)" "227"
assert_le "strip stays inside the client" "$(( $(sfield "$strip3" x) + $(sfield "$strip3" w) ))" "888"

echo "=== 4/7 a second map proves both stripe colours ==="
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

echo "=== 5/7 the product draws the strip its analysis measured ==="
TF2_NATIVE_TRACE="$(win "$OUT/on.trace")" timeout 600 "$EXE" \
  --tf-root "$TF" --demo "$DEMO" --hidden-window --audio-disabled --playback-speed 8 \
  --settings-file "$(win "$OUT/on.ini")" --ui-settings-file "$(win "$OUT/on-ui.ini")" \
  --analysis-scan --hud-live --capture-tick 1200 --capture-frame "$(win "$OUT/on.bmp")" \
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
assert_eq "product strip y" "$(sfield "$online" y)" "$((clienth - 68))"
assert_eq "product exception blocks == probe" "$(nfield "$online" anomalies)" "$anom"
assert_eq "product hints == probe" "$(nfield "$online" hints)" "$hints"
assert_eq "product lane marks reconcile with the report" \
  "$(( $(nfield "$online" anomalies) + $(nfield "$online" hints) + $(nfield "$online" before) + $(nfield "$online" after) ))" \
  "$(( $(nfield "$head" anomalies) + $(nfield "$head" hints) ))"
assert_eq "product strip w" "$(sfield "$online" w)" "$((clientw - 24))"
assert_eq "capture frame exists" "$([ -s "$OUT/on.bmp" ] && echo 1 || echo 0)" "1"
framew=$(od -An -tu4 -j18 -N4 "$OUT/on.bmp" | tr -d ' ')
frameh=$(od -An -tu4 -j22 -N4 "$OUT/on.bmp" | tr -d ' ')
assert_eq "frame width == client width" "$framew" "$clientw"
assert_eq "frame height == client height" "$frameh" "$clienth"

echo "=== 6/7 pixels: are the marks in the frame? ==="
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
  trackY=$((onlineY + 38))
  trackX0=$((onlineX + 6))
  trackX1=$((onlineX + onlineW - 6))
# The strip's own playhead column is not in the product trace line (only the probe
# prints it), so derive it from the same arithmetic: playback 1200 + the offset the
# analysis measured, across the report's first..last server tick.
serverTick=$((1200 + offset))
playheadX=$((trackX0 + (serverTick - first) * (trackX1 - trackX0) / (last - first)))
echo "  ..   sampling rows around trackY=$trackY, playhead column ~$playheadX, tick $serverTick"
laneY=$((onlineY + 20))
span=$((trackX1 - trackX0))
"$PY" "$ANALYZER" "$(win "$OUT/on.bmp")" on_stripe "$((trackX0)),$((trackY - 3)),$span,3" > "$OUT/on-stripe.txt" 2>&1
"$PY" "$ANALYZER" "$(win "$OUT/on.bmp")" on_above "$((trackX0)),$((trackY - 8)),$span,8" > "$OUT/on-above.txt" 2>&1
"$PY" "$ANALYZER" "$(win "$OUT/on.bmp")" on_below "$((trackX0)),$((trackY + 9)),$span,12" > "$OUT/on-below.txt" 2>&1
"$PY" "$ANALYZER" "$(win "$OUT/on.bmp")" on_playhead "$((playheadX - 3)),$((trackY)),7,$((onlineH - 38))" > "$OUT/on-playhead.txt" 2>&1
"$PY" "$ANALYZER" "$(win "$OUT/on.bmp")" on_lane "$((trackX0)),$((laneY)),$span,7" > "$OUT/on-lane.txt" 2>&1
"$PY" "$ANALYZER" "$(win "$OUT/off.bmp")" off_stripe "$((trackX0)),$((trackY - 3)),$span,3" > "$OUT/off-stripe.txt" 2>&1
"$PY" "$ANALYZER" "$(win "$OUT/off.bmp")" off_above "$((trackX0)),$((trackY - 8)),$span,8" > "$OUT/off-above.txt" 2>&1
"$PY" "$ANALYZER" "$(win "$OUT/off.bmp")" off_below "$((trackX0)),$((trackY + 9)),$span,12" > "$OUT/off-below.txt" 2>&1
"$PY" "$ANALYZER" "$(win "$OUT/off.bmp")" off_playhead "$((playheadX - 3)),$((trackY)),7,$((onlineH - 38))" > "$OUT/off-playhead.txt" 2>&1
"$PY" "$ANALYZER" "$(win "$OUT/off.bmp")" off_lane "$((trackX0)),$((laneY)),$span,7" > "$OUT/off-lane.txt" 2>&1
cat "$OUT"/on-stripe.txt "$OUT"/on-above.txt "$OUT"/on-below.txt "$OUT"/on-playhead.txt "$OUT"/on-lane.txt | sed 's/^/  ..   on  /'
cat "$OUT"/off-stripe.txt "$OUT"/off-above.txt "$OUT"/off-below.txt "$OUT"/off-playhead.txt "$OUT"/off-lane.txt | sed 's/^/  ..   off /'
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
fi

echo "=== 7/7 determinism ==="
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
