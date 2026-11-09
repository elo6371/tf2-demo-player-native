#!/usr/bin/env bash
# hud-live-check.sh -- the T97 gate: the live readout (match clock, team score, the
# spectator target's health/class/team) is really drawn in the frame as glyphs.
#   * a --hud-live run paints it -- and that is exactly what a plain interactive
#     launch does by default;
#   * a --hud-off run paints none of it;
#   * an EVIDENCE run (--capture-frame) that passes NEITHER flag paints none of it
#     either, and its bitmap is byte-identical to the --hud-off bitmap.
# The third line is the load-bearing one for this repository: every pixel gate here
# compares its captured frame against a pin committed in the tree, so a readout that
# is on by default must be provably ABSENT from a stock evidence frame.
#
# THE THREE-STATE MODE (lead ruling 2026-11-09)
#   --hud-off                      never draw          reason=cli-hud-off
#   --hud-live                     always draw         mode=forced
#   neither + --capture-frame      suppress            reason=evidence-run
#   neither, interactive launch    draw                mode=interactive
#   --hud-text-test outranks all four (the T67 probe owns the primitive list).
#   HOW "a normal launch draws it" IS SUPPORTED -- HONESTLY. It is NOT proven by a
#   screenshot: under the hostlock rules this harness may not leave a visible
#   window open. It rests on three readings: (1) the resolved mode is printed on
#   every drawing line and on the suppression line, so all three states are
#   observable in a trace; (2) the forced path is verified down to the pixels by
#   sections 1-3; (3) the suppressed path is verified to be byte-identical to the
#   --hud-off frame (section 4). What that does not prove -- that an unattended
#   interactive window renders it -- is stated as unverified in the report.
#
# WHY THIS EXISTS
#   T67 proved the HUD pass can rasterise a FIXED probe list under
#   --hud-text-test; that is a drawing test, not a product feature. T97 makes the
#   ordinary frame carry live data: the round clock (CTeamRoundTimer, falling back
#   to the playback clock), the team scores (CTeam.m_iScore) and the spectator
#   target's health/class/team (readPlaybackHud). This gate pins what a human
#   would otherwise have to eyeball: the numbers come from the entity stream, the
#   glyphs land on the published region pixel for pixel, the text is rebuilt only
#   when a reading changes, and the player name is NEVER invented.
#
# KNOWN WEAKNESSES (independent audit 2026-11-09, FINDINGS; F4/F5/F6 accepted, not yet
# fixed -- do not read these as assertions they are not):
#   F4  Some readings are checked against literals of THIS recording's tick 0
#       (the 10:00 PAUSED clock, the two dashes in the score line). They pin the
#       shape of unknown values, not the mapping from entity props to numbers.
#   F5  The control-flow identities (calls==updates+skips, published==updates) prove
#       internal consistency of the counters, not that a frame really skipped work;
#       the pixel half is what carries that claim (sections 2-3).
#   F6  hudtext ordinal>=1 is one directional: it shows the pass ran at least once,
#       not that it ran exactly once per frame.
#
# WHAT IT ASSERTS
#   A. an ordinary paused run publishes one live reading whose clock/score/target
#      lines have the published shape, whose glyph count equals the text length,
#      whose region is the published rect, and whose hudtext readings agree with
#      the publish line (no claim without a draw);
#   B. the captured pixels: every lit glyph pixel is white, every unlit pixel
#      equals the --hud-off frame bit for bit, nothing outside the region differs,
#      and the text is verified against the font table compiled into the binary;
#   C. --hud-off publishes enabled=0 reason=cli-hud-off, draws no text quads and
#      no HUD primitives, and its frame differs from the live frame (the flag
#      removes a visible feature rather than doing nothing);
#   C'. the same capture command WITHOUT --hud-live publishes enabled=0
#      reason=evidence-run once, and its bitmap is byte-identical to the C frame --
#      i.e. a stock evidence run is unchanged by the feature being on by default;
#   D. two independent processes at the same tick write the same frame bytes
#      (the reading is deterministic, not a random sample);
#   E. an ADVANCING run (no --start-paused, capture at a later tick) publishes a
#      different, smaller clock and rebuilds the text a bounded number of times
#      while skipping the unchanged frames -- "the data refreshes" and "it does
#      not re-upload every frame" are both readings;
#   F. a resized window (--window-size) keeps the same region and the same text:
#      the panel is anchored in pixels, not in window fractions;
#   G. the player name is '?' with reason=no-userinfo: the product has no
#      svc_UserInfo decoder (t63 evidence/t63-sourcetv/FIELD_TABLE.md:43,
#      t68 evidence/t68-events/GAME-EVENT-SPEC.md:435, t70 evidence/t70-t72/
#      DESIGN.md:1126), so a name here would be a fabrication and this gate reds.
#
# NEGATIVE CONTROLS (--mutation)
#   m1 drop-text   the composed lines are emptied before publishing  -> A/B red
#   m2 freeze      the change key is pinned, so later frames republish
#                  nothing                                            -> E red
#   m3 fake-name   the name is invented instead of '?'                -> G red
#   (m1's and m3's recursive runs also re-check C': a broken readout must not
#    change what a suppressed evidence frame looks like, and it must not be
#    possible to satisfy the red by disabling the feature.)
#   Each mutation is scored against the green forward run; the source is restored
#   byte for byte (cmp) and the restored build must reproduce the forward reading.
#
# SINGLE INSTANCE
#   Every product launch is --hidden-window --audio-disabled. This script does NOT
#   take the host lock (a gate that did would deadlock inside a locked run); the
#   caller holds it. --mutation rebuilds, so it needs the lock too.
#
# Usage: bash hud-live-check.sh [--mutation]
# Exit:  0 = every assertion held.

set -uo pipefail
cd "$(dirname "$0")"
export LC_ALL=C

EXE=native/build-nmake/tf2_demo_native.exe
EXE_ABS="$(cygpath -m "$PWD/$EXE")"
TF="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
DEMO="D:/TF2_Demo_Player/testdata/demos/bb841c6d379ff7c40d0c8baf99f59d8d_matcha-20260927-1347-cp_snakewater_final1.dem"
FONT=native/src/hud_font.cpp
RUN_TAG="$(date +%Y%m%d%H%M%S)-$$"
OUT="$(printenv HUD_LIVE_CHECK_OUT)"
[ -z "$OUT" ] && OUT=evidence/t97-hud
SCRATCH="$(printenv HUD_LIVE_CHECK_SCRATCH)"
[ -z "$SCRATCH" ] && SCRATCH=".scratch/t97-hudlive/$RUN_TAG"
RUN_TIMEOUT=400
# Tick 0 + a frozen camera: the paused frames are compared pixel by pixel, so a
# moving camera would make every frame differ everywhere.
CAMERA_ARGS="--debug-camera -3498.225 1021.852 1327.843 70.529329 134.999352"
TICK=0
# The advance capture is a later tick on purpose: on this recording tick 0 is the
# pre-round freeze, where the round timer is paused at its full 10:00 and neither
# score nor a live target exists yet, so a run that never leaves tick 0 cannot
# show that the reading refreshes at all. 9000 is two and a quarter minutes into
# the match; --playback-speed keeps that affordable (see section 5).
ADVANCE_TICK=9000
# 8x is a wall-clock device, not a tested property: the readout text is a function
# of the demo tick, so what is judged below is identical at 1x and would only take
# two and a half minutes longer to reach.
ADVANCE_SPEED=8
# The per-run tallies (calls/updates/skips/published) come from a `hud: live summary`
# line written by std::atexit. That only exists if the product leaves main() through
# a normal return, which is what main.cpp does (no ExitProcess/_exit/quick_exit in the
# tree). The assertion below is the proof: if that line is missing the gate goes red
# rather than explaining the absence away.
AUDIO_DEVICE=6
# The published region is derived from the longest line: x=24 y=24, cell 16px,
# line height 22. The gate recomputes it from the published text rather than
# trusting the number the product printed.
REGION_X=24
REGION_Y=24
CELL=16
LINE=22

MUTATION=0
MUTATION_CASE=""
# --sections=1,2,5 limits the forward run: the mutation driver only needs the
# sections whose assertions the injected defect is supposed to red.
SECTIONS=""
for arg in "$@"; do
  case "$arg" in
    --mutation) MUTATION=1 ;;
    --case=*) MUTATION_CASE="$(echo "$arg" | cut -d= -f2)" ;;
    --sections=*) SECTIONS="$(echo "$arg" | cut -d= -f2)" ;;
    *) echo "unknown argument: $arg"; exit 2 ;;
  esac
done

fail=0
for path in "$EXE_ABS" "$DEMO" "$TF" "$FONT"; do
  [ -e "$path" ] || { echo "FATAL: missing input: $path"; exit 1; }
done

PY="C:/Users/Administrator/.workbuddy-ai/binaries/python/versions/3.13.12/python.exe"
[ -x "$PY" ] || PY="C:/Users/Administrator/AppData/Local/Programs/Python/Python310/python.exe"
[ -x "$PY" ] || { echo "FATAL: no python interpreter found"; exit 1; }
for tool in cygpath timeout cmp; do
  command -v "$tool" >/dev/null 2>&1 || { echo "FATAL: missing tool: $tool"; exit 1; }
done

mkdir -p "$OUT" "$SCRATCH"

val() { if [ -z "$1" ]; then echo "<none>"; else echo "$1"; fi; }
slen() { printf '%s' "$1" | wc -c | tr -d ' '; }
assert_eq() {
  if [ "$(val "$2")" = "$3" ]; then
    echo "  OK   $1 = $3"
  else
    echo "  FAIL $1 = $(val "$2"), expected $3"; fail=1
  fi
}
assert_ge() {
  if [ -n "$2" ] && [ "$2" -ge "$3" ] 2>/dev/null; then
    echo "  OK   $1 = $2 (>= $3)"
  else
    echo "  FAIL $1 = $(val "$2"), expected >= $3"; fail=1
  fi
}
assert_le() {
  if [ -n "$2" ] && [ "$2" -le "$3" ] 2>/dev/null; then
    echo "  OK   $1 = $2 (<= $3)"
  else
    echo "  FAIL $1 = $(val "$2"), expected <= $3"; fail=1
  fi
}
assert_ne() {
  if [ -n "$2" ] && [ "$2" != "$3" ]; then
    echo "  OK   $1 = $2 ( != $3 )"
  else
    echo "  FAIL $1 = $(val "$2"), expected something else than $3"; fail=1
  fi
}

want() { # want <n>: is section n in the requested set (default: all)?
  [ -z "$SECTIONS" ] && return 0
  echo ",$SECTIONS," | grep -q ",$1,"
}

run_capture() { # run_capture <name> [extra args...]
  local name="$1"; shift
  local trace="$SCRATCH/$name.trace"
  local bmp="$SCRATCH/$name.bmp"
  local csv="$SCRATCH/$name.csv"
  local log="$SCRATCH/$name.log"
  local settings="$SCRATCH/$name.settings.ini"
  local uisettings="$SCRATCH/$name.ui.ini"
  rm -f "$trace" "$bmp" "$csv" "$log" "$settings" "$uisettings"
  # --hud-live by default: sections 1-3 and 5-7 exist to prove the readout on
  # purpose. NO_LIVE=1 is only for the two runs that must NOT have it -- --hud-off
  # and the suppression control, which is the same command minus this flag.
  local liveArgs="--hud-live"
  if [ "${NO_LIVE:-0}" = "1" ]; then liveArgs=""; fi
  TF2_NATIVE_TRACE="$(cygpath -m "$PWD/$trace")" timeout "$RUN_TIMEOUT" "$EXE" \
    --tf-root "$TF" --audio-device "$AUDIO_DEVICE" --audio-disabled --hidden-window \
    --settings-file "$(cygpath -m "$PWD/$settings")" \
    --ui-settings-file "$(cygpath -m "$PWD/$uisettings")" \
    --demo "$DEMO" --capture-frame "$(cygpath -m "$PWD/$bmp")" \
    --metrics-file "$(cygpath -m "$PWD/$csv")" \
    $liveArgs $CAMERA_ARGS "$@" > "$log" 2>&1
  printf -v "RC_$name" '%s' "$?"
}

# --- reading helpers -------------------------------------------------------
capture_size() { # capture_size <trace>: the WxH the product says it captured
  grep -a 'capture: frame width=' "$1" | tail -1     | grep -oE 'width=[0-9]+ height=[0-9]+' | sed 's/width=//; s/ height=/x/'
}

cmp_frames() { # cmp_frames <a> <b>: identical | different | READ-FAILED
  # cmp -s exits 2 on a read error, and the shell idiom "cmp -s a b && .. || .."
  # files that under "different" (or under "identical", depending on which side
  # of the || it sits on). A missing bitmap would then pass a comparison about
  # pixels. Only 0 and 1 mean anything here; everything else is a hard failure.
  if [ ! -r "$1" ] || [ ! -r "$2" ] || [ ! -s "$1" ] || [ ! -s "$2" ]; then
    echo READ-FAILED; return
  fi
  cmp -s "$1" "$2"
  case $? in
    0) echo identical ;;
    1) echo different ;;
    *) echo READ-FAILED ;;
  esac
}

live_publish() { # every publish line; the exit summary is a different record
  grep -a 'hud: live ' "$1" | grep -av 'hud: live summary '
}
live_field() { # live_field <trace> <name>: <name> on the LAST publish line
  live_publish "$1" | tail -1 | grep -oE "[ ]$2=[A-Za-z0-9._-]+" | head -1 | cut -d= -f2
}
live_text() { # live_text <trace> <n>: the quoted text of line n on the last publish line
  live_publish "$1" | tail -1 | grep -oE "text$2=\"[^\"]*\"" | sed 's/^text[0-9]="//; s/"$//'
}
live_region() {
  live_publish "$1" | tail -1 | grep -oE 'region=[-0-9]+,[-0-9]+,[0-9]+,[0-9]+' | tail -1 | sed 's/region=//'
}
live_count() {
  live_publish "$1" | wc -l | tr -d ' '
}
# The exit-time tally. The publish line's skips= is a snapshot taken when the text
# changed, so a run that publishes once reads skips=0 however many frames it
# skipped -- reading the skip half of the rule from there measures nothing. This
# line is written from std::atexit (main_hud.cpp: hudLiveSummaryAtExit) and is the
# only record of what the run did on every frame.
live_summary() {
  grep -a 'hud: live summary ' "$1" | tail -1
}
live_summary_field() { # live_summary_field <trace> <name>
  live_summary "$1" | grep -oE "[ ]$2=[A-Za-z0-9._-]+" | head -1 | cut -d= -f2
}
hudtext_field() {
  grep -a 'hudtext:' "$1" | tail -1 | grep -oE "$2=[-0-9]+" | head -1 | cut -d= -f2
}
hud_draw_field() {
  grep -a 'hud: draw' "$1" | tail -1 | grep -oE "$2=[-0-9]+" | head -1 | cut -d= -f2
}
clock_seconds() { # clock_seconds "TIME M:SS..." -> seconds, empty when not a clock
  echo "$1" | grep -oE '^TIME [0-9]+:[0-9][0-9]' | head -1 | sed 's/^TIME //' \
    | awk -F: '{ if (NF==2) print $1*60+$2 }'
}

# verify_live_pixels <off.bmp> <on.bmp> <font.cpp> <x> <y> <text1> <text2> <text3>
#
# Exits 0 only when the drawn text is exactly the font table's pixels at the
# published origin: lit pixels white, unlit pixels equal to the HUD-off frame,
# nothing outside the region changed. It reads kFont8x8 out of hud_font.cpp so the
# check is against the same bytes the binary compiled.
verify_live_pixels() {
  "$PY" - "$@" <<'PYEOF'
import struct, sys, re

off_path, on_path, font_path = sys.argv[1], sys.argv[2], sys.argv[3]
ox, oy = int(sys.argv[4]), int(sys.argv[5])
lines = sys.argv[6:9]
CELL, LH, SCALE = 16, 22, 2

def read_bmp(path):
    data = open(path, 'rb').read()
    if data[:2] != b'BM':
        raise SystemExit('not a BMP: ' + path)
    off = struct.unpack_from('<I', data, 10)[0]
    w, h = struct.unpack_from('<ii', data, 18)
    bpp = struct.unpack_from('<H', data, 28)[0]
    if bpp != 24:
        raise SystemExit('expected 24bpp: ' + path)
    stride = (w * 3 + 3) & ~3
    rows = []
    for y in range(h):
        base = off + (h - 1 - y) * stride
        rows.append(data[base:base + w * 3])
    return rows, w, h

def rgb(rows, x, y):
    p = rows[y][x * 3:x * 3 + 3]
    return (p[2], p[1], p[0])

on, w, h = read_bmp(on_path)
off, w2, h2 = read_bmp(off_path)
if (w, h) != (w2, h2):
    raise SystemExit('frame size differs: %dx%d vs %dx%d' % (w, h, w2, h2))

src = open(font_path, encoding='utf-8').read()
m = re.search(r'kFont8x8[^\n]*=\s*\{(.*?)\n\};', src, re.S)
if not m:
    raise SystemExit('font table not found in ' + font_path)
values = [int(v, 16) for v in re.findall(r'0x([0-9A-Fa-f]{2})', m.group(1))]
if len(values) != 95 * 8:
    raise SystemExit('font table has %d rows, expected 760' % len(values))
glyphs = [values[i * 8:(i + 1) * 8] for i in range(95)]

rx, ry = ox, oy
rw = max(len(l) for l in lines) * CELL if lines else 0
rh = (len(lines) - 1) * LH + 8 * SCALE
print('frame=%dx%d' % (w, h))
print('region=%d,%d,%d,%d' % (rx, ry, rw, rh))

outside = inside = 0
minx, miny, maxx, maxy = w, h, -1, -1
for y in range(h):
    a, b = on[y], off[y]
    for x in range(w):
        if a[x * 3:x * 3 + 3] != b[x * 3:x * 3 + 3]:
            if rx <= x < rx + rw and ry <= y < ry + rh:
                inside += 1
            else:
                outside += 1
            if x < minx: minx = x
            if x > maxx: maxx = x
            if y < miny: miny = y
            if y > maxy: maxy = y
print('outsideDiff=%d' % outside)
print('insideDiff=%d' % inside)
print('diffBBox=%d,%d,%d,%d' % (minx, miny, maxx, maxy))

bad = 0
cells = 0
for li, line in enumerate(lines):
    gy = ry + li * LH
    for ci, ch in enumerate(line):
        code = ord(ch)
        if code < 0x20 or code > 0x7e:
            bad += 1
            continue
        rows8 = glyphs[code - 0x20]
        gx = rx + ci * CELL
        cells += 1
        for r in range(8):
            for c in range(8):
                bit = (rows8[r] >> c) & 1
                for dy in range(SCALE):
                    for dx in range(SCALE):
                        x, y = gx + c * SCALE + dx, gy + r * SCALE + dy
                        if bit:
                            if rgb(on, x, y) != (255, 255, 255):
                                bad += 1
                        else:
                            if rgb(on, x, y) != rgb(off, x, y):
                                bad += 1
print('glyphCells=%d' % cells)
print('glyphBad=%d' % bad)

ok = outside == 0 and inside > 0 and bad == 0
print('verifyOk=%d' % (1 if ok else 0))
sys.exit(0 if ok else 1)
PYEOF
}

# Readings that later sections publish; declared empty so --sections=1,2 stays
# valid under `set -u` and the evidence file prints <none> instead of crashing.
A_T1=""; A_SRC=""; A_UPDATES=""; A_SKIPS=""; A_LINES=""
R_T1=""; R_T3=""; R_REGION=""; LIVE2_T1=""
L_MODE=""; SUP_ENABLED=""; SUP_REASON=""; SUP_LINE=""; SUP_FRAME=""

echo "=== T97 HUD live-reading gate: forward ==="
echo "  exe: $(stat -c '%y %s bytes' "$EXE" 2>/dev/null || echo '<missing>')"
echo "  out: $OUT"
echo "  scratch: $SCRATCH"
echo

# ---------------------------------------------------------------------------
echo "=== 1 the live reading is published ==="
run_capture live --start-paused --capture-tick "$TICK"
NO_LIVE=1 run_capture off --start-paused --capture-tick "$TICK" --hud-off
assert_eq "live exit code" "$RC_live" "0"
assert_eq "hud-off exit code" "$RC_off" "0"

LIVE_N=$(live_count "$SCRATCH/live.trace")
L_ENABLED=$(live_field "$SCRATCH/live.trace" enabled)
L_MODE=$(live_field "$SCRATCH/live.trace" mode)
assert_eq "a --hud-live run declares the forced mode" "$L_MODE" "forced"
L_T1=$(live_text "$SCRATCH/live.trace" 1)
L_T2=$(live_text "$SCRATCH/live.trace" 2)
L_T3=$(live_text "$SCRATCH/live.trace" 3)
L_SRC=$(live_field "$SCRATCH/live.trace" source)
L_GLYPHS=$(live_field "$SCRATCH/live.trace" glyphs)
L_REGION=$(live_region "$SCRATCH/live.trace")
L_NAME=$(live_field "$SCRATCH/live.trace" name)
L_REASON=$(live_field "$SCRATCH/live.trace" nameReason)
L_PUBREASON=$(live_field "$SCRATCH/live.trace" reason)
echo "  reading: text1=\"$(val "$L_T1")\" text2=\"$(val "$L_T2")\" text3=\"$(val "$L_T3")\" source=$(val "$L_SRC")"
assert_ge "an ordinary run publishes at least one live reading" "$LIVE_N" "1"
assert_eq "the live reading is enabled" "$L_ENABLED" "1"
# G: the name is never invented. A name here would be a fabrication.
assert_eq "the reading declares the name unavailable" "$L_NAME" "unavailable"
assert_eq "the reading names the reason it has no name" "$L_REASON" "no-userinfo"
# Why the line exists, as opposed to why the name is a question mark: the publish
# line exists because the reading changed, and it says so in its own field.
assert_eq "every publish says why it was published" "$L_PUBREASON" "text-changed"
echo "$L_T3" | grep -qE '^NAME \?  ' \
  && echo "  OK   text3 prints 'NAME ?' rather than a made-up name" \
  || { echo "  FAIL text3 does not print an honest unknown name: \"$(val "$L_T3")\""; fail=1; }
# The clock is either a real M:SS or an honest '?': never a blank or a zero stand-in.
assert_ge "the clock line is not empty" "$(slen "$L_T1")" "6"
echo "$L_T1" | grep -qE '^TIME ([0-9]+:[0-9][0-9]|\?)( PAUSED)?$' \
  && echo "  OK   text1 has the published clock shape" \
  || { echo "  FAIL text1 has no clock shape: \"$(val "$L_T1")\""; fail=1; }
echo "$L_T2" | grep -qE '^RED ([0-9]+|\?)  BLU ([0-9]+|\?)$' \
  && echo "  OK   text2 has the published score shape" \
  || { echo "  FAIL text2 has no score shape: \"$(val "$L_T2")\""; fail=1; }
echo "$L_T3" | grep -qE '^NAME (\?|[^ ]+)  HP ([0-9]+|\?)  [A-Z?]*  [A-Z?]*$' \
  && echo "  OK   text3 has the published target shape" \
  || { echo "  FAIL text3 has no target shape: \"$(val "$L_T3")\""; fail=1; }
assert_eq "the clock names its source" "$([ -n "$L_SRC" ] && echo yes)" "yes"
# glyphs is a reading, so it must agree with the text it claims to publish.
EXPECT_GLYPHS=$(( $(slen "$L_T1") + $(slen "$L_T2") + $(slen "$L_T3") ))
assert_eq "the published glyph count matches the published text" "$L_GLYPHS" "$EXPECT_GLYPHS"
EXPECT_W=$(slen "$L_T1")
[ "$(slen "$L_T2")" -gt "$EXPECT_W" ] && EXPECT_W=$(slen "$L_T2")
[ "$(slen "$L_T3")" -gt "$EXPECT_W" ] && EXPECT_W=$(slen "$L_T3")
assert_eq "the published region is the panel anchored at 24,24" "$L_REGION" "24,24,$((EXPECT_W * CELL)),60"

# ---------------------------------------------------------------------------
echo "=== 2 the product draws exactly the text it published ==="
D_ORDINAL=$(hud_draw_field "$SCRATCH/live.trace" ordinal)
# NB: hud: draw drawn= counts RECT primitives only (native_renderer_frame.cpp:1256),
# so a readout made purely of text must be read from hudtext's drawn=, which
# counts every primitive the pass actually covered. Using the rect counter here
# would have asserted "0 primitives were drawn" about a frame full of glyphs.
T_DRAWN_PRIM=$(hudtext_field "$SCRATCH/live.trace" drawn)
T_PRIM=$(hudtext_field "$SCRATCH/live.trace" primitives)
T_QUADS=$(hudtext_field "$SCRATCH/live.trace" textQuads)
T_GLYPHS=$(hudtext_field "$SCRATCH/live.trace" glyphs)
T_UNMAPPED=$(hudtext_field "$SCRATCH/live.trace" unmapped)
assert_eq "the live frame draws every primitive it published" "$T_DRAWN_PRIM" "$T_PRIM"
assert_ge "the HUD pass runs after the world" "$D_ORDINAL" "1"
assert_ge "the text primitives exist" "$T_PRIM" "1"
# hudTextQuads_ counts TEXT PRIMITIVES that drew a glyph, not glyphs
# (native_renderer_frame.cpp:1237-1238 increments it once per primitive, inside
# the text branch; the per-glyph counter is hudGlyphCount_). The readout publishes
# three lines, so three text primitives -- and in a run with no imported layout the
# pass holds nothing else, which is why primitives must equal textQuads here.
assert_eq "the readout publishes three text primitives" "$T_QUADS" "3"
assert_eq "every drawn primitive is a text primitive" "$T_PRIM" "$T_QUADS"
assert_eq "the hudtext glyph count matches the publish line" "$T_GLYPHS" "$EXPECT_GLYPHS"
assert_eq "every published codepoint has a glyph" "$T_UNMAPPED" "0"

# ---------------------------------------------------------------------------
echo "=== 3 the captured pixels are the published text and nothing else ==="
VERIFY_OUT=$(verify_live_pixels "$SCRATCH/off.bmp" "$SCRATCH/live.bmp" "$FONT" \
  "$REGION_X" "$REGION_Y" "$L_T1" "$L_T2" "$L_T3" 2>&1)
VERIFY_RC=$?
echo "$VERIFY_OUT" | sed 's/^/  | /'
V_OUTSIDE=$(echo "$VERIFY_OUT" | grep -oE 'outsideDiff=[0-9]+' | cut -d= -f2)
V_INSIDE=$(echo "$VERIFY_OUT" | grep -oE 'insideDiff=[0-9]+' | cut -d= -f2)
V_BAD=$(echo "$VERIFY_OUT" | grep -oE 'glyphBad=[0-9]+' | cut -d= -f2)
V_OK=$(echo "$VERIFY_OUT" | grep -oE 'verifyOk=[0-9]+' | cut -d= -f2)
assert_eq "the pixel verifier exits clean" "$VERIFY_RC" "0"
assert_eq "nothing is painted outside the published region" "$V_OUTSIDE" "0"
assert_ge "the published text paints pixels" "$V_INSIDE" "100"
assert_eq "every lit and unlit pixel matches the font table" "$V_BAD" "0"
assert_eq "the pixel verifier's verdict" "$V_OK" "1"

# The verifier's own negative controls. A pixel check that has only ever been run
# on the frame it was written for proves nothing: it could be printing a constant.
# Two ways to be wrong, one for each half of the rule.
# (a) The same expectation against a frame with no text: the inside-diff must be 0
#     and the verdict red, because "the text paints pixels" is a claim about pixels.
NEG1_OUT=$(verify_live_pixels "$SCRATCH/off.bmp" "$SCRATCH/off.bmp" "$FONT" \
  "$REGION_X" "$REGION_Y" "$L_T1" "$L_T2" "$L_T3" 2>&1)
NEG1_RC=$?
NEG1_INSIDE=$(echo "$NEG1_OUT" | grep -oE 'insideDiff=[0-9]+' | cut -d= -f2)
NEG1_OK=$(echo "$NEG1_OUT" | grep -oE 'verifyOk=[0-9]+' | cut -d= -f2)
echo "  negative control (a): rc=$NEG1_RC insideDiff=$(val "$NEG1_INSIDE") verifyOk=$(val "$NEG1_OK")"
assert_ne "the verifier rejects a frame that paints no text" "$NEG1_RC" "0"
assert_eq "the verifier still counts zero text pixels on that frame" "$NEG1_INSIDE" "0"

# (b) The real frame against a deliberately wrong expectation: every lit pixel the
#     wrong text claims must be a mismatch, so glyphBad is non-zero and red.
NEG2_OUT=$(verify_live_pixels "$SCRATCH/off.bmp" "$SCRATCH/live.bmp" "$FONT" \
  "$REGION_X" "$REGION_Y" "TIME 99:99" "$L_T2" "$L_T3" 2>&1)
NEG2_RC=$?
NEG2_BAD=$(echo "$NEG2_OUT" | grep -oE 'glyphBad=[0-9]+' | cut -d= -f2)
NEG2_OUTSIDE=$(echo "$NEG2_OUT" | grep -oE 'outsideDiff=[0-9]+' | cut -d= -f2)
# Printed as glyphBad/outsideDiff on purpose: a red that came from pixels OUTSIDE the
# published region would be a region defect, not a text defect, and the two must not
# be told apart by reading the source. glyphBad>=1 with outsideDiff=0 is the shape
# that says the refusal is about glyph pixels.
echo "  negative control (b): rc=$NEG2_RC glyphBad=$(val "$NEG2_BAD") outsideDiff=$(val "$NEG2_OUTSIDE")"
assert_ne "the verifier rejects a frame that disagrees with its expectation" "$NEG2_RC" "0"
assert_ge "the disagreement is counted as mismatched glyph pixels" "$NEG2_BAD" "1"
assert_eq "the disagreement stays inside the published region" "$NEG2_OUTSIDE" "0"

# ---------------------------------------------------------------------------
echo "=== 4 --hud-off removes the feature and nothing else ==="
OFF_ENABLED_LINES=$(grep -ac 'hud: live .* enabled=1' "$SCRATCH/off.trace")
OFF_REASON=$(live_field "$SCRATCH/off.trace" reason)
OFF_QUADS=$(hudtext_field "$SCRATCH/off.trace" textQuads)
OFF_PRIM=$(hudtext_field "$SCRATCH/off.trace" primitives)
OFF_DRAWN=$(hudtext_field "$SCRATCH/off.trace" drawn)
OFF_RECTS=$(hud_draw_field "$SCRATCH/off.trace" drawn)
assert_eq "a --hud-off run publishes no enabled reading" "$OFF_ENABLED_LINES" "0"
assert_eq "a --hud-off run says why it is off" "$OFF_REASON" "cli-hud-off"
assert_eq "a --hud-off run draws no text quads" "$OFF_QUADS" "0"
assert_eq "a --hud-off run draws no HUD primitives" "$OFF_PRIM" "0"
assert_eq "a --hud-off run draws no primitives at all" "$OFF_DRAWN" "0"
assert_eq "a --hud-off run draws no layout rects" "$OFF_RECTS" "0"
# The two frames must differ (the feature is visible) -- a --hud-off frame that
# happens to equal the live frame would mean nothing was drawn at all.
OFF_VS_LIVE=$(cmp_frames "$SCRATCH/off.bmp" "$SCRATCH/live.bmp")
assert_eq "the hud-off frame is readable and differs from the live frame" "$OFF_VS_LIVE" "different"

# The suppression control (lead ruling 2026-11-09). The SAME command that produced
# the live frame, minus --hud-live, must say reason=evidence-run and leave a bitmap
# byte-identical to the --hud-off one. This is the only evidence in this tree that
# the default-by-default readout cannot deform an evidence frame, and it is a frame
# comparison rather than an argument about intent.
NO_LIVE=1 run_capture suppress --start-paused --capture-tick "$TICK"
assert_eq "suppressed run exit code" "$RC_suppress" "0"
SUP_ENABLED=$(grep -ac 'hud: live .* enabled=1' "$SCRATCH/suppress.trace")
SUP_REASON=$(live_field "$SCRATCH/suppress.trace" reason)
SUP_LINE=$(grep -ac 'hud: live enabled=0 reason=evidence-run drawn=0' "$SCRATCH/suppress.trace")
SUP_FRAME=$(cmp_frames "$SCRATCH/suppress.bmp" "$SCRATCH/off.bmp")
assert_eq "an evidence run without --hud-live publishes no enabled reading" "$SUP_ENABLED" "0"
assert_eq "an evidence run says why the readout is off" "$SUP_REASON" "evidence-run"
assert_eq "the suppression is announced once, with a drawn=0" "$SUP_LINE" "1"
assert_eq "a suppressed evidence frame is byte-identical to a --hud-off frame" "$SUP_FRAME" "identical"

# ---------------------------------------------------------------------------
if want 5; then
echo "=== 5 the reading refreshes as the demo advances, and only then ==="
# --playback-speed is what makes this section affordable and observable at once.
# The recording plays at 66.67 ticks/s, so tick 9000 at 1x is two and a half
# minutes of wall clock; at 8x it is seventeen seconds. Nothing about the reading
# depends on the wall clock: the text is a function of the demo tick, and the
# captured frame is still the first draw with tick >= the requested one.
run_capture advance --capture-tick "$ADVANCE_TICK" --playback-speed "$ADVANCE_SPEED"
assert_eq "advancing run exit code" "$RC_advance" "0"
A_T1=$(live_text "$SCRATCH/advance.trace" 1)
A_SRC=$(live_field "$SCRATCH/advance.trace" source)
A_LINES=$(live_count "$SCRATCH/advance.trace")
# The tally comes from the exit summary, never from the publish line: that line is
# a snapshot written when the text changed, so its skips= is not a run total.
A_CALLS=$(live_summary_field "$SCRATCH/advance.trace" calls)
A_UPDATES=$(live_summary_field "$SCRATCH/advance.trace" updates)
A_SKIPS=$(live_summary_field "$SCRATCH/advance.trace" skips)
A_PUBLISHED=$(live_summary_field "$SCRATCH/advance.trace" published)
A_SUMMARY_LINES=$(live_summary "$SCRATCH/advance.trace" | wc -l | tr -d ' ')
A_REASONED=$(live_publish "$SCRATCH/advance.trace" | grep -ac ' reason=text-changed ')
P_CLOCK=$(clock_seconds "$L_T1")
A_CLOCK=$(clock_seconds "$A_T1")
echo "  clock: paused=$(val "$L_T1") advancing=$(val "$A_T1")"
assert_ne "the clock reading changes when the demo advances" "$A_T1" "$L_T1"
if [ -n "$P_CLOCK" ] && [ -n "$A_CLOCK" ]; then
  if [ "$A_CLOCK" -lt "$P_CLOCK" ]; then
    echo "  OK   the advancing clock is smaller ($A_CLOCK < $P_CLOCK)"
  else
    echo "  FAIL the advancing clock did not count down ($A_CLOCK vs $P_CLOCK)"; fail=1
  fi
else
  echo "  (clock is '?' on one side, the countdown check does not apply)"
fi
# The run published a tally at exit, and that tally is internally consistent:
# every call either changed the text or skipped, and every change left one line.
# Five structural invariants and nothing else. No ratio, no cap, no count that
# depends on the frame rate or on how often this recording changes its timer:
# those numbers move with the machine and would one day red a correct build.
assert_eq "the advancing run published an exit tally" "$A_SUMMARY_LINES" "1"
assert_ge "the tally counts every frame the readout ran on" "$A_CALLS" "100"
assert_eq "every call is either an update or a skip" "$A_CALLS" "$(( ${A_UPDATES:-0} + ${A_SKIPS:-0} ))"
assert_eq "every rebuilt line is a published line" "$A_LINES" "$A_PUBLISHED"
assert_eq "each published line is one update" "$A_PUBLISHED" "$A_UPDATES"
assert_eq "every published line says why it was published" "$A_REASONED" "$A_PUBLISHED"
assert_ge "the text is rebuilt when a reading changes" "$A_UPDATES" "2"
assert_ge "unchanged frames are skipped instead of re-uploaded" "$A_SKIPS" "1"
# The first and the last publish must disagree about the clock: a readout that
# republishes the same text is not refreshing, whatever its counters say.
A_FIRST_T1=$(live_publish "$SCRATCH/advance.trace" | head -1 | grep -oE 'text1="[^"]*"' | sed 's/^text1="//; s/"$//')
echo "  first/last published clock: first="$(val "$A_FIRST_T1")" last="$(val "$A_T1")""
assert_ne "the clock text differs between the first and the last publish" "$A_FIRST_T1" "$A_T1"
fi

# ---------------------------------------------------------------------------
if want 6; then
echo "=== 6 the panel is anchored in pixels, not in window fractions ==="
run_capture resized --start-paused --capture-tick "$TICK" --window-size 800x600
assert_eq "resized run exit code" "$RC_resized" "0"
R_T1=$(live_text "$SCRATCH/resized.trace" 1)
R_T3=$(live_text "$SCRATCH/resized.trace" 3)
R_REGION=$(live_region "$SCRATCH/resized.trace")
# F1 (audit 2026-11-09): "the region did not move" is only evidence if the window
# actually moved. Two runs that both came out 784x561 would pass the anchored-region
# assertion while proving nothing, so the capture size is read back from the product
# and required to differ before the invariance is claimed.
CAP_SIZE_A=$(capture_size "$SCRATCH/live.trace")
CAP_SIZE_B=$(capture_size "$SCRATCH/resized.trace")
echo "  capture size: default=$(val "$CAP_SIZE_A") resized=$(val "$CAP_SIZE_B")"
assert_ne "the resized run captured at a different window size" "$CAP_SIZE_B" "$CAP_SIZE_A"
assert_eq "the region does not move when the window is resized" "$R_REGION" "$L_REGION"
assert_eq "the clock text does not change with the window size" "$R_T1" "$L_T1"
assert_eq "the target text does not change with the window size" "$R_T3" "$L_T3"
fi

# ---------------------------------------------------------------------------
if want 7; then
echo "=== 7 two independent processes write the same frame ==="
run_capture live2 --start-paused --capture-tick "$TICK"
assert_eq "second process exit code" "$RC_live2" "0"
cmp -s "$SCRATCH/live.bmp" "$SCRATCH/live2.bmp" \
  && echo "  OK   the two processes wrote byte-identical frames" \
  || { echo "  FAIL the two processes disagree on the frame"; fail=1; }
LIVE2_T1=$(live_text "$SCRATCH/live2.trace" 1)
assert_eq "the second process publishes the same clock" "$LIVE2_T1" "$L_T1"
fi

# ---------------------------------------------------------------------------
# Evidence: keep the readings, not the 2.5 MB bitmaps.
{
  echo "exe=$EXE"
  echo "exe_sha256=$(sha256sum "$EXE" | cut -d' ' -f1)"
  echo "gate_sha256=$(sha256sum "$0" | cut -d' ' -f1)"
  echo "demo_sha256=$(sha256sum "$DEMO" | cut -d' ' -f1)"
  echo "tick=$TICK advance_tick=$ADVANCE_TICK"
  echo "reading1=$L_T1"
  echo "reading2=$L_T2"
  echo "reading3=$L_T3"
  echo "source=$L_SRC glyphs=$L_GLYPHS region=$L_REGION"
  echo "publishes=$LIVE_N"
  echo "advance_text1=$A_T1 advance_updates=$A_UPDATES advance_skips=$A_SKIPS advance_source=$A_SRC"
  echo "name=$L_NAME reason=$L_REASON"
  echo "resized_region=$R_REGION"
  echo "pixels: outside=$V_OUTSIDE inside=$V_INSIDE glyphBad=$V_BAD"
  echo "hud_off: quads=$OFF_QUADS primitives=$OFF_PRIM drawn=$OFF_DRAWN rects=$OFF_RECTS"
  echo "mode=$L_MODE suppress_reason=$SUP_REASON suppress_enabled_lines=$SUP_ENABLED suppress_frame=$SUP_FRAME"
} > "$OUT/readings.txt"
echo
echo "  readings written to $OUT/readings.txt"

# ---------------------------------------------------------------------------
# Negative controls. A mutation nobody notices is an assertion that does not
# test what it claims, so each case must red a NAMED assertion above; the source
# is then restored byte for byte and the restored build must reproduce the
# forward green, or the scoring itself is untrustworthy.
# ---------------------------------------------------------------------------
if [ "$MUTATION" -eq 1 ]; then
  echo
  echo "=== mutation suite ==="
  if [ "$fail" -ne 0 ]; then
    echo "REFUSING to score mutations: the forward run above is already red, so"
    echo "'the mutation was caught' would describe a gate that never worked."
    exit 2
  fi
  SRC=native/src/main_hud.cpp
  ORIG="$SCRATCH/main_hud.cpp.orig"
  cp "$SRC" "$ORIG"
  # F3 (audit 2026-11-09): this suite rewrites a source file in a worktree other
  # lanes share. An interrupt (ctrl-c, timeout, a killed job) must not leave the
  # patched file behind, so every exit path restores from the copy first and says
  # which bytes are on disk now.
  mut_restore() {
    if [ -f "$ORIG" ]; then cp "$ORIG" "$SRC"; fi
    echo "  source restored sha=$(sha256sum "$SRC" | cut -d' ' -f1)"
  }
  trap 'mut_restore' EXIT INT TERM
  mut_fail=0

  patch_case() { # patch_case <case>: one surgical edit, exactly-once match
    "$PY" - "$SRC" "$1" <<'PYEOF'
import sys
path, case = sys.argv[1], sys.argv[2]
edits = {
    'm1-drop-text': [(
        '  const HudLiveText text = hudLiveCompose(playback, target, reading);',
        '  HudLiveText text = hudLiveCompose(playback, target, reading);\n'
        '  text.time.clear(); text.score.clear(); text.target.clear();  // MUTATION m1')],
    'm2-freeze': [(
        '  if (key == g_hudLiveLastKey) {',
        '  if (g_hudLiveLastKey.size() > 0) {  // MUTATION m2: first reading frozen')],
    'm3-fake-name': [(
        '  text.target = "NAME ?";',
        '  text.target = "NAME Player";  // MUTATION m3: an invented name'),
        ('name=unavailable"', 'name=Player"')],
}
if case not in edits:
    raise SystemExit('unknown mutation case: ' + case)
src = open(path, encoding='utf-8').read()
for old, new in edits[case]:
    hits = src.count(old)
    if hits != 1:
        raise SystemExit('%s: pattern occurs %d times, expected 1: %r' % (case, hits, old))
    src = src.replace(old, new, 1)
open(path, 'w', encoding='utf-8', newline='').write(src)
print('patched', case)
PYEOF
  }

  for case in m1-drop-text m2-freeze m3-fake-name; do
    echo
    echo "--- mutation $case ---"
    cp "$ORIG" "$SRC"
    echo "  pre-mutation source sha=$(sha256sum "$SRC" | cut -d' ' -f1)"
    if ! patch_case "$case"; then
      echo "  FAIL $case: the patch did not apply"; mut_fail=1; continue
    fi
    if ! bash build-target.sh tf2_demo_native > "$SCRATCH/build-$case.log" 2>&1; then
      echo "  FAIL $case: the mutated tree does not build (see $SCRATCH/build-$case.log)"
      mut_fail=1; cp "$ORIG" "$SRC"; continue
    fi
    mut_sections=""
    expect=""
    case "$case" in
      m1-drop-text) mut_sections="--sections=1,2,3,4"
                    expect="FAIL text1 has no clock shape" ;;
      m2-freeze)    mut_sections="--sections=1,2,5"
                    expect="FAIL the clock reading changes when the demo advances" ;;
      m3-fake-name) mut_sections="--sections=1"
                    expect="FAIL the reading declares the name unavailable" ;;
    esac
    mout="$SCRATCH/mut-$case.txt"
    HUD_LIVE_CHECK_OUT="$SCRATCH/mut-$case-out" \
      HUD_LIVE_CHECK_SCRATCH="$SCRATCH/mut-$case-scratch" \
      bash "$0" "$mut_sections" > "$mout" 2>&1
    mut_rc=$?
    reds=$(grep -ac '^  FAIL ' "$mout")
    if [ "$mut_rc" -ne 0 ] && grep -qF "$expect" "$mout"; then
      echo "  MUTATION-RED $case: rc=$mut_rc, $reds named assertion(s) red"
      grep -a '^  FAIL ' "$mout" | sed 's/^/    /' | head -8
    else
      echo "  FAIL $case: NOT CAUGHT (rc=$mut_rc, expected '$expect'); log $mout"
      mut_fail=1
    fi
    cp "$ORIG" "$SRC"
    if cmp -s "$ORIG" "$SRC"; then
      echo "  RESTORED-IDENTICAL $SRC ($(sha256sum "$SRC" | cut -d' ' -f1))"
    else
      echo "  FAIL $case: restore is not byte-identical"; mut_fail=1
    fi
  done

  echo
  echo "--- restored build ---"
  if ! bash build-target.sh tf2_demo_native > "$SCRATCH/build-restored.log" 2>&1; then
    echo "  FAIL the restored tree does not build"; mut_fail=1
  else
    HUD_LIVE_CHECK_OUT="$SCRATCH/restored-out" \
      HUD_LIVE_CHECK_SCRATCH="$SCRATCH/restored-scratch" \
      bash "$0" > "$SCRATCH/restored.txt" 2>&1
    rrc=$?
    if [ "$rrc" -eq 0 ] && grep -q 'HUD-LIVE-CHECK=PASS' "$SCRATCH/restored.txt"; then
      echo "  OK   the restored build reproduces the forward reading"
    else
      echo "  FAIL the restored build does not reproduce the forward reading (rc=$rrc)"
      grep -a '^  FAIL ' "$SCRATCH/restored.txt" | head -8 | sed 's/^/    /'
      mut_fail=1
    fi
  fi
  if [ "$mut_fail" -eq 0 ]; then
    echo "MUTATION-SUITE=PASS"
  else
    echo "MUTATION-SUITE=FAIL"; fail=1
  fi
fi

if [ "$fail" -eq 0 ]; then
  echo "HUD-LIVE-CHECK=PASS"
  exit 0
fi
echo "HUD-LIVE-CHECK=FAIL"
exit 1
