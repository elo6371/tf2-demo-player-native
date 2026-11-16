#!/usr/bin/env bash
# =============================================================================
# minimap-hud-check.sh -- E9 gate: the minimap panel is drawn, it is drawn in the
# right place, it is drawn the right way round, and it is inert when not asked for.
#
# WHAT IT RUNS. The product, on a pl_upward_f12 demo, twice: once with
# --hud-live --hud-minimap and once with --hud-live alone. The two captured frames
# are the evidence: everything outside the panel must be pixel-identical, and inside
# the panel every ring, the viewer dot and the arrow are sampled at the place this
# script computes for them, from its OWN copy of the map table (section "the gate's
# own copy" below). Nothing here is read back from native/include/minimap.h: a gate
# that imported the product's own constants would agree with any bug that was
# transcribed consistently, and T65 is exactly that failure.
#
# ASSERTIONS
#   A1  identity: the exe and the four patched sources did not move during the run;
#       both captures exist; the readout says it is on.
#   A2  the panel is painted: at least 20000 of its 25600 pixels differ from the
#       frame without the switch.
#   A3  and ONLY the panel is painted: zero differing pixels outside the panel, and
#       the panel rectangle in the trace equals the one recomputed from the bitmap.
#   A4  the border is not the background: the four border bands are materially
#       lighter than the interior, so the panel is a framed box, not a dark smear.
#   A5  every control point has a ring exactly where the table says: 5x5 windows at
#       the computed centres, on pl_upward_f12 (four neutral caps). In the
#       secondary sunshine section the rings must carry their team colours.
#   A6  the rings are NOT mirrored vertically: the y-flipped position of cap_1 has
#       no ring. This is the assertion a forgotten yFlip dies on.
#   A7  the rings are NOT mirrored horizontally: the x-mirrored position of cap_2
#       has no ring.
#   A8  the viewer is drawn at the camera eye, in its own colour.
#   A9  the arrow points where the camera looks: three green dots ahead of the
#       viewer, none of them behind it.
#   A9b the same eye with the camera turned 180 degrees: the dots move to the other
#       side. A9 alone cannot tell a heading from three dots in a fixed direction.
#   A10 an OLD exe that predates --hud-minimap treats the switch as unknown, draws
#       nothing and produces the same bytes as its own no-flag run (negative
#       control; needs MINIMAP_CHECK_OLD_EXE and a build of the base commit).
#   A11 a map with no tabulated bounds draws NOTHING and says why: a byte-identical
#       frame plus reason=bounds-not-tabulated.
#   A12 three product mutations are caught: no yFlip (A5/A6 red), the arrow sign
#       flipped (A9 "ahead" red -- flipping the sign puts the dots behind, so the
#       ahead assertion is the one that must fail, not the absence one), the panel
#       anchored left instead of right (A2 bbox red).
#
# EIGHT WAYS THIS COULD BE VACUOUSLY GREEN, AND WHAT STOPS EACH
#   V1 "the trace says it drew" is treated as drawing.       -> A2 is a pixel count
#   V2 "nothing outside the panel changed" is true of a frame where nothing was
#      drawn at all, so it is never asserted alone.          -> A2 insideDiff floor
#   V3 the panel rectangle is read out of the trace and then used to judge the
#      trace (self-reference).                              -> A3 recomputes it from
#                                                              the bitmap width and
#                                                              the layout rule
#   V4 a tick with no camera and no viewer spins the whole panel empty and every
#      "absent" assertion passes.                          -> A5/A8/A9 require the
#                                                              shapes to be PRESENT
#   V5 an evidence run suppresses the readout, so the panel is never drawn and the
#      two frames are equal.                              -> A1 asserts enabled=1
#                                                              and A2 requires a diff
#   V6 every sampled pixel is background, so "no ring here" is trivially true.
#                                                          -> A5 requires a ring at
#                                                              the true position
#   V7 the sampling tolerance is widened until anything counts.
#                                                          -> +/-1 pixel boxes, and
#                                                              the absence checks use
#                                                              the same boxes
#   V8 the pixel expectations are transcribed from the T65 1024-pixel canvas.
#                                                          -> the table here is the
#                                                              160-pixel panel and
#                                                              A3 cross-checks the
#                                                              rectangle against the
#                                                              bitmap
#
# USAGE
#   bash minimap-hud-check.sh [options]
#     --sections=1,2,3   run only these sections (comma separated; default all)
#     --mutation         also run A12: patch a source, rebuild, expect the gate red,
#                        restore, rebuild, expect the exe bytes back. Needs the
#                        serial host lock; ~10 minutes and four builds.
#     --old-exe=PATH     the pre-E9 product binary A10 compares against
#     --selftest-pixels  no product, no build: synthesise frames with the same
#                        drawing rules and prove the pixel half goes red when the
#                        rings are flipped or the panel is missing
#     --print-out        print OUT, SCRATCH and the exit code
#
# ENVIRONMENT
#   MINIMAP_CHECK_OUT       where the frames, traces and logs land (default: this
#                           lane's evidence directory, never evidence/ in the repo)
#   MINIMAP_CHECK_SCRATCH   WORK directory; MUST be an MSYS path (/d/...) or a
#                           relative one -- a D:/ path would be prefixed with the
#                           repository root and every read would fail (that cost a
#                           whole window in T126)
#   MINIMAP_CHECK_OLD_EXE   same as --old-exe
#   MINIMAP_CHECK_DEMO_SUNSHINE
#                           the cp_sunshine recording (default: the user's install at
#                           $TF/demos/SUNSHINE.dem); override when the same recording
#                           lives outside the install tree
#
# EXIT 0 only when every assertion that ran held. A skipped control is counted and
# printed; it never silently counts as a pass.
# =============================================================================
set -uo pipefail
cd "$(dirname "$0")" || exit 2
export LC_ALL=C

EXE=native/build-nmake/tf2_demo_native.exe
EXE_ABS="$(cygpath -m "$PWD/$EXE")"
TF="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
# pl_upward_f12: four neutral caps, and -- the reason it is the primary demo -- the
# y-flipped and x-mirrored positions of its caps all land INSIDE the panel, so A6
# and A7 have something to look at. On cp_sunshine the flipped positions fall off
# the panel and those two assertions would pass by having nothing to check (V6).
DEMO="D:/TF2_Demo_Player/testdata/demos/a9630c6557468b24f0b8c2e7dab38ed8_match-20261030-2145-pl_upward_f12.dem"
# The canonical home of this recording is the user's TF install. The environment
# variable overrides only where the bytes are read from; the default is unchanged
# (lead ruling 2026-11-16). No assertion, camera, threshold or section list below changed.
DEMO_SUNSHINE="${MINIMAP_CHECK_DEMO_SUNSHINE:-$TF/demos/SUNSHINE.dem}"
# koth_bagel_rc13 is NOT koth_bagel_rc11: it is a different BSP, its bounds were
# never measured, and that is A11's whole point.
DEMO_UNLISTED="D:/TF2_Demo_Player/testdata/demos/4a9bfb9276509d0ec5f5fdc722a95b17_match-20260927-0239-koth_bagel_rc13.dem"
# x y z pitch yaw: the eye is world (0,-2048); with yaw 33 that is 16 px from the
# nearest cap, so a ring and the viewer dot cannot be confused for one another, and
# the three arrow dots ahead land on background.
CAMERA_ARGS="--debug-camera 0 -2048 512 0 33"
# The same comma-separated numbers the analyser is handed, so a change to the camera
# is one edit and the product and the pixel half cannot drift apart.
EYE_X=0.0
EYE_Y=-2048.0
YAW=33.0
# The same eye with the camera turned around. A9b re-runs the heading test here:
# if the dots were a constant rather than a function of the camera, they would stay
# where A9 found them and A9b would find them ahead when they must be behind.
YAW_BACK=213.0
TICK=0
RUN_TIMEOUT=400
AUDIO_DEVICE=6
# Isolated settings: the product writes both files back on exit, and the checked-in
# fixture is the one every other gate here starts from.
FIXTURE_SETTINGS="native/tests/fixtures/gate-settings.ini"
RUN_TAG="$(date +%Y%m%d%H%M%S)-$$"

SECTIONS="1,2,3,4,5,6,7,8,9,10,11,12"
MUTATION=0
SELFTEST_PIXELS=0
PRINT_OUT=0
OLD_EXE="${MINIMAP_CHECK_OLD_EXE:-}"

for arg in "$@"; do
  case "$arg" in
    --mutation) MUTATION=1 ;;
    --sections=*) SECTIONS="${arg#--sections=}" ;;
    --old-exe=*) OLD_EXE="${arg#--old-exe=}" ;;
    --selftest-pixels) SELFTEST_PIXELS=1 ;;
    --print-out) PRINT_OUT=1 ;;
    -h|--help) sed -n "3,60p" "$0"; exit 0 ;;
    *) echo "unknown argument: $arg" >&2; exit 2 ;;
  esac
done

OUT="${MINIMAP_CHECK_OUT:-}"
if [ -z "$OUT" ]; then
  # D-65: a mutation run must not overwrite the frames a reading run published, so
  # it defaults under the scratch tree unless it was given an explicit OUT.
  if [ "$MUTATION" -eq 1 ]; then
    echo "note: --mutation without MINIMAP_CHECK_OUT: writing into the scratch tree (D-65)"
    OUT=""
  else
    OUT="/d/_shared/EXEC-2026-11-13/E/workflow-auditor/minimap-gate/${RUN_TAG}"
  fi
elif [ "$MUTATION" -eq 1 ]; then
  echo "note: --mutation with an explicit OUT: this run will publish its frames there"
fi

SCRATCH="${MINIMAP_CHECK_SCRATCH:-.scratch/e9-minimap/$RUN_TAG}"
case "$SCRATCH" in
  /*) ;;
  *) SCRATCH="$PWD/$SCRATCH" ;;
esac
if [ -z "$OUT" ]; then
  OUT="$SCRATCH/out"
fi
mkdir -p "$OUT" "$SCRATCH" || exit 2

# ---- tools -----------------------------------------------------------------
need() { command -v "$1" >/dev/null 2>&1 || { echo "FATAL: $1 is not on PATH" >&2; exit 2; }; }
need cygpath; need timeout; need cmp; need sha256sum; need awk; need sed

PY=""
for candidate in \
  "C:/Users/Administrator/.workbuddy-ai/binaries/python/versions/3.13.12/python.exe" \
  "C:/Users/Administrator/AppData/Local/Programs/Python/Python310/python.exe" \
  python python3; do
  if [ -x "$candidate" ] || command -v "$candidate" >/dev/null 2>&1; then PY="$candidate"; break; fi
done
[ -n "$PY" ] || { echo "FATAL: no python found" >&2; exit 2; }

ANALYZER="$SCRATCH/minimap_pixels.py"

# ---- the pixel half --------------------------------------------------------
# Written out on every run so the gate is one file. It reads 24bpp BMPs the way
# hud-live-check.sh does (bottom-up rows, 4-byte row padding) and answers three
# questions: what changed, where a ring is, and where a ring is NOT.
cat > "$ANALYZER" <<'PYEOF'
import json, math, struct, sys

PANEL = 160
INSET = 8
MARGIN = 4
MARGIN_BOTTOM = 4

# The gate's OWN copy of the map table (T135 measurement). Deliberately not read
# from native/include/minimap.h: a gate that trusted the product's table would
# agree with any transposition made consistently in both.
MAPS = {
  "pl_upward_f12": dict(
    bounds=(-3072.0, -4096.0, 4096.0, 3072.0),
    points=[("cap_1", 1, 0, 1024.0, -1536.0),
            ("cap_2", 2, 0, 1536.0, 1088.0),
            ("cap_3", 3, 0, -1024.0, 624.0),
            ("cap_4", 4, 0, 512.0, -384.0)]),
  "cp_sunshine": dict(
    bounds=(-11008.0, 128.0, -256.0, 10368.0),
    points=[("cp_blu1", 0, 3, -7632.0, 8940.0),
            ("cp_blu2", 1, 3, -7704.0, 6824.0),
            ("cp_3", 2, 0, -5632.01, 5248.0),
            ("cp_red2", 3, 2, -3560.0, 3672.0),
            ("cp_red1", 4, 2, -3632.0, 1556.0)]),
}

def read_bmp(path):
    data = open(path, "rb").read(2048)
    if len(data) < 54 or data[0:2] != b"BM":
        sys.exit("not a BMP: " + path)
    offset = struct.unpack_from("<I", data, 10)[0]
    width, height = struct.unpack_from("<ii", data, 18)
    depth = struct.unpack_from("<H", data, 28)[0]
    if depth != 24 or width <= 0 or height <= 0:
        sys.exit("expected a 24bpp BMP, got depth %d (%dx%d): %s" % (depth, width, height, path))
    stride = ((width * 3) + 3) & ~3
    need = offset + stride * height
    raw = open(path, "rb").read(need)
    if len(raw) < need:
        sys.exit("truncated BMP: " + path)
    rows = [raw[offset + stride * r: offset + stride * (r + 1)] for r in range(height)]
    return width, height, rows

def rgb(rows, w, h, x, y):
    x = int(x); y = int(y)
    if x < 0 or y < 0 or x >= w or y >= h:
        return None
    row = rows[h - 1 - y]
    b, g, r = row[x * 3], row[x * 3 + 1], row[x * 3 + 2]
    return (r, g, b)

# Colour CLASSES, not exact values: the shader converts float colour to 8 bits its
# own way, and a gate that pinned the byte would be a gate that breaks on a rounding
# change that no user can see. The bands are wide enough to survive that and narrow
# enough to keep the classes disjoint.
def classify(c):
    if c is None:
        return "outside"
    r, g, b = c
    if r < 45 and g < 45 and b < 55:
        return "bg"
    if g > 190 and r < 140 and b < 190:
        return "viewer"
    if r > 150 and g < 110 and b < 110:
        return "red"
    if b > 140 and r < 120 and g < 150:
        return "blu"
    if r > 195 and g > 190 and b > 175:
        return "neutral"
    if 100 <= r <= 195 and abs(r - g) < 30 and abs(g - b) < 30:
        return "border"
    return "other"

def panel_rect(w, h):
    return (w - INSET - MARGIN - PANEL, INSET + MARGIN, PANEL, PANEL)

# The projection the panel must use: equal aspect, centred, y flipped. Written out
# here in full rather than imported, so A3/A5 compare two independent computations.
def projection(bounds, size=PANEL):
    minx, miny, maxx, maxy = bounds
    spanx = maxx - minx
    spany = maxy - miny
    scale = min(size / spanx, size / spany)
    offx = (size - spanx * scale) / 2.0 - minx * scale
    offy = (size - spany * scale) / 2.0 + maxy * scale
    return scale, offx, offy

def project(p, scale, offx, offy, flip=True):
    px = p[0] * scale + offx
    py = (offy - p[1] * scale) if flip else (offy + p[1] * scale)
    return px, py

# A 5x5 window counts as "this shape is here" at 20 of its 25 pixels: a 4-pixel
# radius disc covers the whole window when it is centred on it (half-diagonal 2.83),
# and a window on plain background sees 0. 20 leaves room for one clipped row.
def window_counts(rows, w, h, cx, cy, radius=2):
    counts = {}
    for dy in range(-radius, radius + 1):
        for dx in range(-radius, radius + 1):
            klass = classify(rgb(rows, w, h, round(cx) + dx, round(cy) + dy))
            counts[klass] = counts.get(klass, 0) + 1
    return counts

def count(counts, klass):
    return counts.get(klass, 0)

def cmd_panel(a_with, a_without):
    w, h, rows_w = read_bmp(a_with)
    w2, h2, rows_n = read_bmp(a_without)
    if (w, h) != (w2, h2):
        sys.exit("frame sizes differ: %dx%d vs %dx%d" % (w, h, w2, h2))
    x0, y0, pw, ph = panel_rect(w, h)
    inside = 0
    outside = 0
    minx = miny = 10 ** 9
    maxx = maxy = -1
    for y in range(h):
        for x in range(w):
            if rgb(rows_w, w, h, x, y) != rgb(rows_n, w, h, x, y):
                if x0 <= x < x0 + pw and y0 <= y < y0 + ph:
                    inside += 1
                else:
                    outside += 1
                minx = min(minx, x); maxx = max(maxx, x)
                miny = min(miny, y); maxy = max(maxy, y)
    border = classify(rgb(rows_w, w, h, x0 + pw // 2, y0 + 1))
    interior = classify(rgb(rows_w, w, h, x0 + pw // 2, y0 + ph // 2 + 60))
    print("PANEL w=%d h=%d x=%d y=%d pw=%d ph=%d" % (w, h, x0, y0, pw, ph))
    print("DIFF inside=%d outside=%d" % (inside, outside))
    print("BBOX x0=%d y0=%d x1=%d y1=%d" % (minx, miny, maxx, maxy))
    print("BORDER class=%s" % border)
    print("INTERIOR class=%s" % interior)
    print("BRIGHT border=%d interior=%d" % (luma(rgb(rows_w, w, h, x0 + pw // 2, y0 + 1)),
                                              luma(rgb(rows_w, w, h, x0 + pw // 2, y0 + ph // 2 + 60))))

def luma(c):
    if c is None:
        return -1
    return (c[0] * 299 + c[1] * 587 + c[2] * 114) // 1000

def cmd_rings(path, mapname):
    w, h, rows = read_bmp(path)
    x0, y0, pw, ph = panel_rect(w, h)
    scale, offx, offy = projection(MAPS[mapname]["bounds"])
    print("PANEL x=%d y=%d pw=%d ph=%d" % (x0, y0, pw, ph))
    print("PROJ scale=%.9f offx=%.6f offy=%.6f" % (scale, offx, offy))
    for name, index, team, qx, qy in MAPS[mapname]["points"]:
        px, py = project((qx, qy), scale, offx, offy, True)
        nflipx, nflipy = project((qx, qy), scale, offx, offy, False)
        mirx = 2.0 * (pw / 2.0) - px
        c = window_counts(rows, w, h, x0 + px, y0 + py)
        cf = window_counts(rows, w, h, x0 + nflipx, y0 + nflipy)
        cm = window_counts(rows, w, h, x0 + mirx, y0 + py)
        print("RING name=%s index=%d team=%d px=%.4f py=%.4f ax=%.4f ay=%.4f"
              " neutral=%d red=%d blu=%d bg=%d inside=%d" % (
                  name, index, team, px, py, x0 + px, y0 + py,
                  count(c, "neutral"), count(c, "red"), count(c, "blu"), count(c, "bg"),
                  1 if (0 <= px <= pw and 0 <= py <= ph) else 0))
        print("NOFLIP name=%s px=%.4f py=%.4f neutral=%d red=%d blu=%d bg=%d" % (
              name, nflipx, nflipy, count(cf, "neutral"), count(cf, "red"),
              count(cf, "blu"), count(cf, "bg")))
        print("MIRROR name=%s px=%.4f py=%.4f neutral=%d red=%d blu=%d bg=%d" % (
              name, mirx, py, count(cm, "neutral"), count(cm, "red"),
              count(cm, "blu"), count(cm, "bg")))

def cmd_viewer(path, mapname, eyex, eyey, yaw):
    w, h, rows = read_bmp(path)
    x0, y0, pw, ph = panel_rect(w, h)
    scale, offx, offy = projection(MAPS[mapname]["bounds"])
    px, py = project((eyex, eyey), scale, offx, offy, True)
    c5 = window_counts(rows, w, h, x0 + px, y0 + py, 2)
    c3 = window_counts(rows, w, h, x0 + px, y0 + py, 1)
    print("VIEWER px=%.4f py=%.4f ax=%.4f ay=%.4f green5=%d green3=%d bg5=%d inside=%d" % (
          px, py, x0 + px, y0 + py, count(c5, "viewer"), count(c3, "viewer"),
          count(c5, "bg"), 1 if (0 <= px <= pw and 0 <= py <= ph) else 0))
    rad = math.radians(yaw)
    fx, fy = math.cos(rad), -math.sin(rad)
    for step in (4.0, 5.0, 6.0):
        ax = px + step * fx
        ay = py + step * fy
        ahead = window_counts(rows, w, h, x0 + ax, y0 + ay, 2)
        # The dot behind the viewer is sampled in a 3x3 window on purpose: a 5x5
        # window four pixels away reaches the viewer's own 3x3 square, and then a
        # "no green behind" assertion would be red for the wrong reason.
        behind = window_counts(rows, w, h, x0 + px - step * fx, y0 + py - step * fy, 1)
        print("ARROW step=%.1f px=%.4f py=%.4f green5=%d ax=%.4f ay=%.4f" % (
              step, ax, ay, count(ahead, "viewer"), x0 + ax, y0 + ay))
        print("ARROWBACK step=%.1f px=%.4f py=%.4f green3=%d" % (
              step, px - step * fx, py - step * fy, count(behind, "viewer")))

def cmd_selftest():
    # Synthesise two frames that follow the same drawing rules as the product and
    # check that this analyzer says the right thing about each. No product, no
    # build: the point is to prove the pixel half can go RED, not that the panel
    # looks nice.
    w, h = 320, 240
    def new_frame():
        rows = [bytearray(b"\x20\x20\x20" * w) for _ in range(h)]
        return rows
    def put(rows, x, y, c):
        x = int(x); y = int(y)
        if 0 <= x < w and 0 <= y < h:
            rows[h - 1 - y][x * 3] = c[2]
            rows[h - 1 - y][x * 3 + 1] = c[1]
            rows[h - 1 - y][x * 3 + 2] = c[0]
    def fill(rows, x0, y0, pw, ph_, c):
        for yy in range(int(y0), int(y0 + ph_)):
            for xx in range(int(x0), int(x0 + pw)):
                put(rows, xx, yy, c)
    def disc(rows, cx, cy, r, c):
        for yy in range(-int(math.ceil(r)), int(math.ceil(r)) + 1):
            for xx in range(-int(math.ceil(r)), int(math.ceil(r)) + 1):
                if (xx * xx + yy * yy) ** 0.5 <= r:
                    put(rows, cx + xx, cy + yy, c)
    def draw(rows, flip=True, with_panel=True):
        if not with_panel:
            return
        x0, y0, pw, ph_ = panel_rect(w, h)
        fill(rows, x0, y0, pw, ph_, (13, 15, 20))
        fill(rows, x0, y0, pw, 2, (158, 163, 168))
        fill(rows, x0, y0 + ph_ - 2, pw, 2, (158, 163, 168))
        fill(rows, x0, y0, 2, ph_, (158, 163, 168))
        fill(rows, x0 + pw - 2, y0, 2, ph_, (158, 163, 168))
        scale, offx, offy = projection(MAPS["pl_upward_f12"]["bounds"])
        for name, index, team, qx, qy in MAPS["pl_upward_f12"]["points"]:
            px, py = project((qx, qy), scale, offx, offy, flip)
            disc(rows, x0 + px, y0 + py, 4, (235, 230, 217))
        vx, vy = project((0.0, -2048.0), scale, offx, offy, True)
        fill(rows, x0 + vx - 1.5, y0 + vy - 1.5, 3, 3, (26, 255, 77))
        rad = math.radians(33.0)
        fx, fy = math.cos(rad), -math.sin(rad)
        for step in (4.0, 5.0, 6.0):
            fill(rows, x0 + vx + step * fx - 1, y0 + vy + step * fy - 1, 2, 2, (26, 255, 77))
    def write(path, rows):
        stride = ((w * 3) + 3) & ~3
        body = bytearray()
        # rows[0] is already the file's first (bottom) row: see put()/rgb().
        for row in rows:
            body += row
            body += b"\x00" * (stride - w * 3)
        header = b"BM" + struct.pack("<IHHI", 54 + len(body), 0, 0, 54)
        info = struct.pack("<IiiHHIIiiII", 40, w, h, 1, 24, 0, len(body), 2835, 2835, 0, 0)
        open(path, "wb").write(header + info + bytes(body))
    a = new_frame(); draw(a, True)
    b = new_frame()
    c = new_frame(); draw(c, False)
    write(sys.argv[2] + "/self-on.bmp", a)
    write(sys.argv[2] + "/self-off.bmp", b)
    write(sys.argv[2] + "/self-flipped.bmp", c)
    print("SELFTEST frames=3 dir=" + sys.argv[2])

if __name__ == "__main__":
    command = sys.argv[1]
    if command == "panel":
        cmd_panel(sys.argv[2], sys.argv[3])
    elif command == "rings":
        cmd_rings(sys.argv[2], sys.argv[3])
    elif command == "viewer":
        cmd_viewer(sys.argv[2], sys.argv[3], float(sys.argv[4]), float(sys.argv[5]), float(sys.argv[6]))
    elif command == "selftest":
        cmd_selftest()
    else:
        sys.exit("unknown command " + command)
PYEOF


# ---- assertions ----------------------------------------------------------
fail=0
ok=0
skipped_controls=0
val() { if [ -z "$1" ]; then echo "<none>"; else echo "$1"; fi; }
pass() { echo "  OK   $1"; ok=$((ok + 1)); }
fails=0
fail_one() { echo "  FAIL $1"; fails=$((fails + 1)); fail=1; }
assert_eq() {
  if [ "$(val "$2")" = "$3" ]; then pass "$1 = $3";
  else fail_one "$1 = $(val "$2"), expected $3"; fi
}
assert_ne() {
  if [ -n "$2" ] && [ "$2" != "$3" ]; then pass "$1 = $2 (not $3)";
  else fail_one "$1 = $(val "$2"), expected anything but $3"; fi
}
assert_ge() {
  if [ -n "$2" ] && [ "$2" -ge "$3" ] 2>/dev/null; then pass "$1 = $2 (>= $3)";
  else fail_one "$1 = $(val "$2"), expected >= $3"; fi
}
assert_le() {
  if [ -n "$2" ] && [ "$2" -le "$3" ] 2>/dev/null; then pass "$1 = $2 (<= $3)";
  else fail_one "$1 = $(val "$2"), expected <= $3"; fi
}
assert_close() { # assert_close <what> <got> <want> <tol>
  local got="$2" want="$3" tol="$4"
  if [ -n "$got" ] && awk -v a="$got" -v b="$want" -v t="$tol" \
      'BEGIN { d = a - b; if (d < 0) d = -d; exit !(d <= t) }'; then
    pass "$1 = $got (within $tol of $want)"
  else
    fail_one "$1 = $(val "$got"), expected $want +/- $tol"
  fi
}
want() { [ -z "$SECTIONS" ] && return 0; echo ",$SECTIONS," | grep -q ",$1,"; }

# ---- readings -------------------------------------------------------------
# One line per question: the gate reads the SAME line twice (once as text for the
# number, once through the pixel analyser) so a shape can never be judged on the
# product's word alone.
trace_tail() { grep -a "$2" "$1" 2>/dev/null | tail -n 1; }
field_of() { printf '%s\n' "$1" | sed -n "s/.*[[:space:]]$2=\([^[:space:]]*\).*/\1/p"; }
minimap_line() { trace_tail "$1" "minimap: hud state="; }
cp_line() { grep -a "minimap: cp name=$2 " "$1" 2>/dev/null | tail -n 1; }
capture_dims() { trace_tail "$1" "capture: frame width=" | grep -oE 'width=[0-9]+ height=[0-9]+' | sed 's/width=//; s/ height=/x/'; }

run_python() { "$PY" "$ANALYZER" "$@"; }

# ---- capture --------------------------------------------------------------
# Every run gets its own directory and its own settings pair: the product writes
# both files back on exit, and a shared pair would let one run decide another's
# HUD state. NO_MINIMAP=1 / NO_LIVE=1 / ALT_DEMO=... are the only knobs; they
# exist so the A/B pair differs by exactly one flag.
run_capture() { # run_capture <name> [extra args...]
  local name="$1"; shift
  local trace="$SCRATCH/$name.trace"
  local bmp="$SCRATCH/$name.bmp"
  local csv="$SCRATCH/$name.csv"
  local log="$SCRATCH/$name.log"
  local settings="$SCRATCH/$name.settings.ini"
  local uisettings="$SCRATCH/$name.ui.ini"
  rm -f "$trace" "$bmp" "$csv" "$log" "$settings" "$uisettings"
  if [ -f "$FIXTURE_SETTINGS" ]; then cp "$FIXTURE_SETTINGS" "$settings"; else : > "$settings"; fi
  local live="--hud-live"
  [ "${NO_LIVE:-0}" = "1" ] && live=""
  local mini="--hud-minimap"
  [ "${NO_MINIMAP:-0}" = "1" ] && mini=""
  local demo="$DEMO"
  [ -n "${ALT_DEMO:-}" ] && demo="$ALT_DEMO"
  local exe="$EXE"
  [ -n "${ALT_EXE:-}" ] && exe="$ALT_EXE"
  local cam="$CAMERA_ARGS"
  [ -n "${ALT_CAMERA:-}" ] && cam="$ALT_CAMERA"
  TF2_NATIVE_TRACE="$(cygpath -m "$trace")" timeout "$RUN_TIMEOUT" "$exe" \
    --tf-root "$TF" --audio-device "$AUDIO_DEVICE" --audio-disabled --hidden-window \
    --settings-file "$(cygpath -m "$settings")" \
    --ui-settings-file "$(cygpath -m "$uisettings")" \
    --demo "$demo" --capture-frame "$(cygpath -m "$bmp")" \
    --metrics-file "$(cygpath -m "$csv")" \
    $live $mini $cam "$@" > "$log" 2>&1
  printf -v "RC_$name" '%s' "$?"
  local rcvar="RC_$name"
  echo "RUN $name rc=${!rcvar} trace=$trace bmp=$bmp"
}

# ---- A1: the switch exists and the panel publishes itself ------------------
section_a1() {
  want 1 || return 0
  echo "== A1  the flag publishes a panel, and the reading describes it"
  assert_eq "the forward run exited cleanly" "${RC_on:-<unset>}" 0
  assert_eq "the control run (same argv minus --hud-minimap) exited cleanly" \
    "${RC_offbase:-<unset>}" 0
  local on_line off_line
  on_line="$(minimap_line "$SCRATCH/on.trace")"
  off_line="$(minimap_line "$SCRATCH/offbase.trace")"
  assert_ne "the reading exists when the flag is on" "$on_line" ""
  assert_eq "no minimap line at all when the flag is absent" "$off_line" "<none>"
  assert_eq "the panel is ready on a tabulated map" "$(field_of "$on_line" state)" ready
  assert_eq "the reason says why it is ready" "$(field_of "$on_line" reason)" ok
  assert_eq "the map is the demo's map" "$(field_of "$on_line" map)" pl_upward_f12
  assert_eq "the panel has four control points to draw" "$(field_of "$on_line" cps)" 4
  assert_eq "all four were placed inside the panel" "$(field_of "$on_line" drawn)" 4
  assert_eq "the viewer is inside the panel" "$(field_of "$on_line" viewerInside)" 1
  assert_eq "three heading dots were placed" "$(field_of "$on_line" arrowDots)" 3
  # 1 background + 4 border strips + 4 rings + 1 observer + 3 heading dots.
  assert_eq "the panel handed over thirteen shapes" "$(field_of "$on_line" shapes)" 13
  assert_eq "the run also wrote a capture" "$([ -s "$SCRATCH/on.bmp" ] && echo yes)" yes
  assert_eq "the control run wrote a capture too" "$([ -s "$SCRATCH/offbase.bmp" ] && echo yes)" yes
  echo "A1-READING $(echo "$on_line" | sed 's/^[0-9.]* //')"
}

# ---- A2: the panel is the only thing that changed -------------------------
# 1280x720 window minus the frame is 1264x681 on this host; the panel is 160x160
# at (1092,12). 25600 pixels is the whole interior, and the measured delta is all
# of it, so the bound below means "essentially the entire panel".
section_a2() {
  want 2 || return 0
  echo "== A2  only the panel changed, and it changed completely"
  local out
  out="$(run_python panel "$SCRATCH/on.bmp" "$SCRATCH/offbase.bmp")" || { fail_one "the pixel analyser failed"; return; }
  printf '%s\n' "$out" | sed 's/^/  | /'
  A2_INSIDE="$(printf '%s\n' "$out" | sed -n 's/^DIFF inside=\([0-9]*\) outside=.*/\1/p')"
  A2_OUTSIDE="$(printf '%s\n' "$out" | sed -n 's/^DIFF inside=[0-9]* outside=\([0-9]*\).*/\1/p')"
  A2_PANEL="$(printf '%s\n' "$out" | sed -n 's/^PANEL w=\([0-9]*\) h=\([0-9]*\) x=\([0-9]*\) y=\([0-9]*\) pw=\([0-9]*\) ph=\([0-9]*\).*/\1 \2 \3 \4 \5 \6/p')"
  set -- $A2_PANEL
  A2_W="${1:-}"; A2_H="${2:-}"; A2_X="${3:-}"; A2_Y="${4:-}"; A2_PW="${5:-}"; A2_PH="${6:-}"
  assert_eq "both frames are the same size" "$([ -n "$A2_W" ] && echo yes)" yes
  assert_eq "the panel is 160 pixels wide" "$A2_PW" 160
  assert_eq "the panel is 160 pixels tall" "$A2_PH" 160
  assert_eq "the panel sits at the top-right corner (W-172)" "$A2_X" "$((A2_W - 172))"
  assert_eq "the panel starts at the 8+4 inset" "$A2_Y" 12
  # A2b: the PANEL line above is computed by panel_rect() from the frame size
  # alone, so on its own it asserts the SPEC and would still say "top-right" if
  # the product drew the panel in the other corner. The BBOX is measured from the
  # two frames, so these four assertions are the positional half of A2: the
  # differently-painted pixels must occupy exactly the rectangle the spec names.
  # Added for A12 m3-anchor (2026-11-14): anchoring the panel left left the
  # pixel-count assertions as the only red, which is a side effect, not the
  # defect. With the panel anchored left, the first red is now "the changed
  # pixels start at the panel's left edge".
  A2_BBOX="$(printf '%s\n' "$out" | sed -n 's/^BBOX x0=\(-*[0-9]*\) y0=\(-*[0-9]*\) x1=\(-*[0-9]*\) y1=\(-*[0-9]*\).*/\1 \2 \3 \4/p')"
  set -- $A2_BBOX
  A2_BX0="${1:-}"; A2_BY0="${2:-}"; A2_BX1="${3:-}"; A2_BY1="${4:-}"
  assert_eq "the changed pixels start at the panel's left edge" "$A2_BX0" "$A2_X"
  assert_eq "the changed pixels start at the panel's top edge" "$A2_BY0" "$A2_Y"
  assert_eq "the changed pixels end at the panel's right edge" "$A2_BX1" "$((A2_X + A2_PW - 1))"
  assert_eq "the changed pixels end at the panel's bottom edge" "$A2_BY1" "$((A2_Y + A2_PH - 1))"
  assert_ge "pixels changed inside the panel" "$A2_INSIDE" 20000
  assert_le "pixels changed inside the panel" "$A2_INSIDE" 25600
  # A shape that hung over the panel edge would paint on the world, because the HUD
  # pass scissors to the union of the primitives and not to this rectangle (V2).
  assert_eq "pixels changed outside the panel" "$A2_OUTSIDE" 0
}

# ---- A3: the panel the product published is the panel it drew --------------
section_a3() {
  want 3 || return 0
  echo "== A3  the published rectangle is where the pixels are"
  local on_line out
  on_line="$(minimap_line "$SCRATCH/on.trace")"
  out="$(run_python panel "$SCRATCH/on.bmp" "$SCRATCH/offbase.bmp")" || { fail_one "the pixel analyser failed"; return; }
  local pub bbox pane
  pub="$(field_of "$on_line" panel)"
  bbox="$(printf '%s\n' "$out" | sed -n 's/^BBOX x0=\(-*[0-9]*\) y0=\(-*[0-9]*\) x1=\(-*[0-9]*\) y1=\(-*[0-9]*\).*/\1 \2 \3 \4/p')"
  # A3 recomputes the panel rectangle here instead of inheriting A2's, so a
  # --sections=3 run judges the same thing a full run does.
  pane="$(printf '%s\n' "$out" | sed -n 's/^PANEL w=\([0-9]*\) h=\([0-9]*\) x=\([0-9]*\) y=\([0-9]*\) pw=\([0-9]*\) ph=\([0-9]*\).*/\1 \2 \3 \4 \5 \6/p')"
  set -- $pane
  local bw="${1:-0}" bh="${2:-0}" ax="${3:-}" ay="${4:-}" apw="${5:-}" aph="${6:-}"
  # The rectangle below is computed from the bitmap size, never read from the product.
  assert_eq "the reading and the bitmap agree on the panel rectangle" \
    "$pub" "$ax,$ay,$apw,$aph"
  assert_eq "the panel is at the top-right corner of the bitmap" "$ax" "$((bw - 172))"
  assert_eq "the panel starts at the 8+4 inset" "$ay" 12
  assert_eq "the panel is 160 pixels wide" "$apw" 160
  assert_eq "the panel is 160 pixels tall" "$aph" 160
  set -- $bbox
  local bx0="${1:-}" by0="${2:-}" bx1="${3:-}" by1="${4:-}"
  assert_eq "the changed pixels start inside the panel (x)" "$([ -n "$bx0" ] && [ "$bx0" -ge "$ax" ] && echo yes)" yes
  assert_eq "the changed pixels start inside the panel (y)" "$([ -n "$by0" ] && [ "$by0" -ge "$ay" ] && echo yes)" yes
  assert_eq "the changed pixels end inside the panel (x)" "$([ -n "$bx1" ] && [ "$bx1" -le "$((ax + apw - 1))" ] && echo yes)" yes
  assert_eq "the changed pixels end inside the panel (y)" "$([ -n "$by1" ] && [ "$by1" -le "$((ay + aph - 1))" ] && echo yes)" yes
  # The capture the product wrote and the frame the analyser read must be one frame.
  local dims
  dims="$(capture_dims "$SCRATCH/on.trace")"
  assert_eq "the trace and the bitmap agree on the captured frame" "$dims" "${bw}x${bh}"
}

# ---- A4: the panel has a frame you can see --------------------------------
section_a4() {
  want 4 || return 0
  echo "== A4  border and background are different colours"
  local out
  out="$(run_python panel "$SCRATCH/on.bmp" "$SCRATCH/offbase.bmp")" || { fail_one "the pixel analyser failed"; return; }
  local border interior bright
  border="$(printf '%s\n' "$out" | sed -n 's/^BORDER class=\(.*\)$/\1/p')"
  interior="$(printf '%s\n' "$out" | sed -n 's/^INTERIOR class=\(.*\)$/\1/p')"
  bright="$(printf '%s\n' "$out" | sed -n 's/^BRIGHT border=\([0-9]*\) interior=\([0-9]*\).*/\1 \2/p')"
  set -- $bright
  echo "  | border=$border interior=$interior luma=${1:-?}/${2:-?}"
  assert_eq "the top edge classifies as the border colour" "$border" border
  assert_eq "the middle of the panel classifies as the background" "$interior" bg
  assert_ge "border luminance beats the interior by at least 40" "$(( ${1:-0} - ${2:-0} ))" 40
}

# ---- A5: every objective is drawn, in its team colour ---------------------
section_a5() {
  want 5 || return 0
  echo "== A5  the four control points are rings where the reading says"
  local on_line out
  on_line="$(minimap_line "$SCRATCH/on.trace")"
  assert_eq "the panel lists the map's four objectives" "$(field_of "$on_line" cps)" 4
  out="$(run_python rings "$SCRATCH/on.bmp" pl_upward_f12)" || { fail_one "the pixel analyser failed"; return; }
  printf '%s\n' "$out" | grep '^RING ' | sed 's/^/  | /'
  local name
  for name in cap_1 cap_2 cap_3 cap_4; do
    local line reading ring
    line="$(cp_line "$SCRATCH/on.trace" "$name")"
    assert_eq "$name was published with drawn=1" "$(field_of "$line" drawn)" 1
    assert_eq "$name is inside the panel" "$(field_of "$line" inside)" 1
    # Two independent computations of the same projection must agree: the product
    # prints px,py, the analyser recomputes them from the map table (V3).
    reading="$(printf '%s\n' "$line" | sed -n 's/.*px=\(-*[0-9.]*\),py=\(-*[0-9.]*\).*/\1 \2/p')"
    set -- $reading
    ring="$(printf '%s\n' "$out" | grep "^RING name=$name ")"
    assert_ne "$name has a pixel row" "$ring" ""
    assert_close "$name ring x agrees with the reading" "$(field_of "$ring" px)" "${1:-}" 0.05
    assert_close "$name ring y agrees with the reading" "$(field_of "$ring" py)" "${2:-}" 0.05
    # Upward's objectives are neutral: all 25 pixels of a 5x5 window are neutral.
    assert_ge "$name ring is drawn at that pixel" "$(field_of "$ring" neutral)" 20
  done
  # Team colours need a map that has them; upward has none, so one more frame.
  echo "-- A5b team colours (cp_sunshine: two BLU, two RED, one neutral)"
  local sline sout blu red neutral
  sline="$(minimap_line "$SCRATCH/sunshine.trace")"
  assert_eq "the sunshine frame is a ready panel" "$(field_of "$sline" state)" ready
  sout="$(run_python rings "$SCRATCH/sunshine.bmp" cp_sunshine)" || { fail_one "the pixel analyser failed"; return; }
  printf '%s\n' "$sout" | grep '^RING ' | sed 's/^/  | /'
  blu="$(printf '%s\n' "$sout" | grep '^RING name=cp_blu1 ')"
  red="$(printf '%s\n' "$sout" | grep '^RING name=cp_red1 ')"
  neutral="$(printf '%s\n' "$sout" | grep '^RING name=cp_3 ')"
  assert_ge "cp_blu1 is drawn in the BLU band" "$(field_of "$blu" blu)" 20
  assert_ge "cp_red1 is drawn in the RED band" "$(field_of "$red" red)" 20
  assert_ge "cp_3 is drawn in the neutral band" "$(field_of "$neutral" neutral)" 20
}

# ---- A6: the y axis is flipped, and the wrong place is empty --------------
# A ring drawn without the flip lands at offsetY + y*scale, which is NOT the mirror
# of the right answer: for cap_1 that is (91.43, 34.29) against (91.43, 102.86).
# The two points, and the nearest true ring, are 15 pixels apart, so a 5x5 window
# can tell "drawn" from "not drawn" without a tolerance.
section_a6() {
  want 6 || return 0
  echo "== A6  the projection flips y"
  local out
  out="$(run_python rings "$SCRATCH/on.bmp" pl_upward_f12)" || { fail_one "the pixel analyser failed"; return; }
  printf '%s\n' "$out" | grep -E '^(RING|NOFLIP) name=cap_1 ' | sed 's/^/  | /'
  local ring flip
  ring="$(printf '%s\n' "$out" | grep '^RING name=cap_1 ')"
  flip="$(printf '%s\n' "$out" | grep '^NOFLIP name=cap_1 ')"
  assert_ge "cap_1 is drawn at the flipped position" "$(field_of "$ring" neutral)" 20
  assert_eq "nothing is drawn at the unflipped position" "$(field_of "$flip" neutral)" 0
  assert_ge "the unflipped position is inside the panel (so this is not vacuous)" "$(field_of "$flip" bg)" 20
}

# ---- A7: the x axis is not mirrored ----------------------------------------
section_a7() {
  want 7 || return 0
  echo "== A7  the projection does not mirror x"
  local out
  out="$(run_python rings "$SCRATCH/on.bmp" pl_upward_f12)" || { fail_one "the pixel analyser failed"; return; }
  printf '%s\n' "$out" | grep -E '^(RING|MIRROR) name=cap_2 ' | sed 's/^/  | /'
  local ring mirror
  ring="$(printf '%s\n' "$out" | grep '^RING name=cap_2 ')"
  mirror="$(printf '%s\n' "$out" | grep '^MIRROR name=cap_2 ')"
  assert_ge "cap_2 is drawn at the projected position" "$(field_of "$ring" neutral)" 20
  assert_eq "nothing is drawn at the mirrored position" "$(field_of "$mirror" neutral)" 0
  assert_ge "the mirrored position is inside the panel (so this is not vacuous)" "$(field_of "$mirror" bg)" 20
}

# ---- A8: the observer is drawn where the camera is ------------------------
section_a8() {
  want 8 || return 0
  echo "== A8  the observer dot sits on the camera's world position"
  local on_line out
  on_line="$(minimap_line "$SCRATCH/on.trace")"
  out="$(run_python viewer "$SCRATCH/on.bmp" pl_upward_f12 "$EYE_X" "$EYE_Y" "$YAW")" || { fail_one "the pixel analyser failed"; return; }
  printf '%s\n' "$out" | grep '^VIEWER ' | sed 's/^/  | /'
  local viewer
  viewer="$(printf '%s\n' "$out" | grep '^VIEWER ')"
  assert_eq "the observer is inside the panel" "$(field_of "$viewer" inside)" 1
  # The dot is 3x3, so a 3x3 window on its centre is nine green pixels.
  # A 3x3 square on half-pixel boundaries covers 2 or 3 columns, so the 5x5 window
  assert_close "the product and the analyser agree on the observer x" "$(field_of "$on_line" viewerPx)" "$(field_of "$viewer" px)" 0.05
  assert_close "the product and the analyser agree on the observer y" "$(field_of "$on_line" viewerPy)" "$(field_of "$viewer" py)" 0.05
}

# ---- A9: the heading points forward ---------------------------------------
# yaw 33 degrees means north-east, and the panel has +y DOWN, so the heading is
# (cos, -sin) in panel space. The unflipped heading (cos, +sin) puts the dots on the
# other side of the observer, where this check looks and must find nothing. The
# behind window is 3x3 because a 5x5 window four pixels out reaches the observer.
section_a9() {
  want 9 || return 0
  echo "== A9  the heading dots point along the camera yaw"
  local out
  out="$(run_python viewer "$SCRATCH/on.bmp" pl_upward_f12 "$EYE_X" "$EYE_Y" "$YAW")" || { fail_one "the pixel analyser failed"; return; }
  printf '%s\n' "$out" | grep -E '^(ARROW|ARROWBACK)' | sed 's/^/  | /'
  local step
  for step in 4.0 5.0 6.0; do
    local ahead behind
    ahead="$(printf '%s\n' "$out" | grep "^ARROW step=$step ")"
    behind="$(printf '%s\n' "$out" | grep "^ARROWBACK step=$step ")"
    assert_ge "a heading dot ${step}px ahead" "$(field_of "$ahead" green5)" 1
    assert_eq "no heading dot ${step}px behind" "$(field_of "$behind" green3)" 0
  done

  # A9b: the same eye, the camera turned 180 degrees. The dots must move to the other
  # side. This is what separates "the heading is drawn from the camera" from "the
  # heading is three green dots in a fixed direction", which A9 alone cannot tell.
  echo "== A9b  the heading follows the camera, not a constant"
  local back
  back="$(run_python viewer "$SCRATCH/yawback.bmp" pl_upward_f12 "$EYE_X" "$EYE_Y" "$YAW_BACK")" \
    || { fail_one "the pixel analyser failed on the turned camera"; return; }
  printf '%s\n' "$back" | grep -E '^(ARROW|ARROWBACK)' | sed 's/^/  | /'
  for step in 4.0 5.0 6.0; do
    local ahead2 behind2
    ahead2="$(printf '%s\n' "$back" | grep "^ARROW step=$step ")"
    behind2="$(printf '%s\n' "$back" | grep "^ARROWBACK step=$step ")"
    assert_ge "a heading dot ${step}px ahead of the turned camera" "$(field_of "$ahead2" green5)" 1
    assert_eq "no heading dot ${step}px behind the turned camera" "$(field_of "$behind2" green3)" 0
  done
}

# ---- A10: an exe that predates the flag draws nothing ---------------------
# The old binary does not know --hud-minimap. main.cpp has no "unknown argument"
# error (it counts them and keeps going), so the honest expectation is: the flag is
# dropped, no reading appears, and the frame is byte-for-byte the frame without it.
section_a10() {
  want 10 || return 0
  echo "== A10  the exe from before this patch ignores the flag"
  if [ -z "$OLD_EXE" ] || [ ! -f "$OLD_EXE" ]; then
    echo "  SKIP no --old-exe given, or the binary is not there: $(val "$OLD_EXE")"
    skipped_controls=$((skipped_controls + 1))
    return 0
  fi
  ALT_EXE="$OLD_EXE" run_capture oldon --start-paused
  ALT_EXE="$OLD_EXE" NO_MINIMAP=1 run_capture oldoff --start-paused
  assert_eq "the old exe exits cleanly with the unknown flag" "${RC_oldon:-<unset>}" 0
  assert_eq "the old exe exits cleanly without it" "${RC_oldoff:-<unset>}" 0
  assert_eq "the old exe publishes no minimap reading" "$(minimap_line "$SCRATCH/oldon.trace")" "<none>"
  cmp -s "$SCRATCH/oldon.bmp" "$SCRATCH/oldoff.bmp"
  assert_eq "the old exe draws the same frame with or without the flag" "$?" 0
}

# ---- A11: an untabulated map draws nothing and says why -------------------
# rc13 is not one of the seven maps this patch carries data for. The panel must NOT
# appear, the reading must name the reason, and the frame must be identical to the
# same run without the flag -- "we have no bounds" is not a licence to guess.
section_a11() {
  want 11 || return 0
  echo "== A11  an untabulated map draws no panel and names the reason"
  local line
  line="$(minimap_line "$SCRATCH/unlisted.trace")"
  assert_eq "the unlisted run exits cleanly" "${RC_unlisted:-<unset>}" 0
  assert_eq "the unlisted map publishes a reading" "$(field_of "$line" state)" unavailable
  assert_eq "the reading names the reason" "$(field_of "$line" reason)" bounds-not-tabulated
  assert_eq "the reading names the map it refused" "$(field_of "$line" map)" koth_bagel_rc13
  assert_eq "no shapes were drawn" "$(field_of "$line" shapes)" 0
  assert_eq "no objectives were placed" "$(field_of "$line" drawn)" 0
  cmp -s "$SCRATCH/unlisted.bmp" "$SCRATCH/unlisted_off.bmp"
  assert_eq "the frame is the same as with no flag at all" "$?" 0
}

# ---- pixel self-test: the analyser can go red, offline --------------------
# No product, no build, no lock: three synthesised frames that follow the same
# drawing rules. It proves the analyser discriminates, including the two
# "wrong place must be empty" judgements that A6 and A7 depend on. It does NOT
# prove the product draws any of this -- that is what sections 1-11 are for.
pixel_selftest() {
  local dir="$SCRATCH/selftest" okc=0 badc=0
  mkdir -p "$dir"
  echo "== pixel self-test (no product, no build, no lock)"
  if ! run_python selftest "$dir" > "$dir/selftest.log" 2>&1; then
    echo "  FAIL the analyser could not write its synthetic frames"
    echo "SELFTEST-PIXELS=FAIL checks=0 failed=1"; return 1
  fi
  local check_eq check_ge
  check_eq() { if [ "$2" = "$3" ]; then echo "  OK   $1 = $3"; okc=$((okc + 1));
    else echo "  FAIL $1 = $(val "$2"), expected $3"; badc=$((badc + 1)); fi; }
  check_ge() { if [ -n "$2" ] && [ "$2" -ge "$3" ] 2>/dev/null; then echo "  OK   $1 = $2 (>= $3)"; okc=$((okc + 1));
    else echo "  FAIL $1 = $(val "$2"), expected >= $3"; badc=$((badc + 1)); fi; }
  local out
  out="$(run_python panel "$dir/self-on.bmp" "$dir/self-off.bmp")"
  printf '%s\n' "$out" | sed 's/^/  | /'
  check_eq "a drawn panel changes its whole interior" \
    "$(printf '%s\n' "$out" | sed -n 's/^DIFF inside=\([0-9]*\) outside=.*/\1/p')" 25600
  check_eq "and nothing outside it" \
    "$(printf '%s\n' "$out" | sed -n 's/^DIFF inside=[0-9]* outside=\([0-9]*\).*/\1/p')" 0
  local rings flipped
  rings="$(run_python rings "$dir/self-on.bmp" pl_upward_f12)"
  flipped="$(run_python rings "$dir/self-flipped.bmp" pl_upward_f12)"
  check_ge "cap_1 ring where the flipped projection puts it" \
    "$(field_of "$(printf '%s\n' "$rings" | grep '^RING name=cap_1 ')" neutral)" 20
  check_eq "nothing at the unflipped position" \
    "$(field_of "$(printf '%s\n' "$rings" | grep '^NOFLIP name=cap_1 ')" neutral)" 0
  check_eq "nothing at the mirrored position" \
    "$(field_of "$(printf '%s\n' "$rings" | grep '^MIRROR name=cap_2 ')" neutral)" 0
  # And the other way round: the SAME analyser, on a frame drawn without the flip,
  # must say the flip position is empty and the unflipped one is occupied. Without
  # this pair, "neutral=0 at the wrong place" could be true of any two frames.
  check_eq "the unflipped frame has nothing at the flipped position" \
    "$(field_of "$(printf '%s\n' "$flipped" | grep '^RING name=cap_1 ')" neutral)" 0
  check_ge "the unflipped frame draws at the unflipped position" \
    "$(field_of "$(printf '%s\n' "$flipped" | grep '^NOFLIP name=cap_1 ')" neutral)" 20
  local viewer
  viewer="$(run_python viewer "$dir/self-on.bmp" pl_upward_f12 "$EYE_X" "$EYE_Y" "$YAW")"
  check_ge "the observer dot is green" "$(field_of "$(printf '%s\n' "$viewer" | grep '^VIEWER ')" green5)" 6
  local step
  for step in 4.0 5.0 6.0; do
    check_ge "a heading dot ${step}px ahead" \
      "$(field_of "$(printf '%s\n' "$viewer" | grep "^ARROW step=$step ")" green5)" 1
    check_eq "no heading dot ${step}px behind" \
      "$(field_of "$(printf '%s\n' "$viewer" | grep "^ARROWBACK step=$step ")" green3)" 0
  done
  if [ "$badc" -eq 0 ]; then
    echo "SELFTEST-PIXELS=PASS checks=$okc"
    return 0
  fi
  echo "SELFTEST-PIXELS=FAIL checks=$okc failed=$badc"
  return 1
}

# ---- A12: three injected defects, each must turn the gate red -------------
# A mutation that is not caught means the gate is decorative. Each case mutates one
# line, rebuilds, re-runs only the sections that judge that line, and requires both a
# non-zero exit AND a red line naming the intended assertion. The source is restored
# by byte copy and compared, and the final rebuild must reproduce the forward exe.
#
# THE EXPECTED REASON MUST BE THE ASSERTION THAT NAMES THE DEFECT
#   2026-11-14: the first A12 run caught all three mutations but judged FAIL, because
#   m2 and m3 named the wrong red line. Both were harness errors, and fixing them
#   needed more than editing a string:
#     m2-heading flipped the arrow sign. That moves the dots BEHIND the camera, so
#     "a heading dot Npx ahead" is the assertion that must fail; "no heading dot Npx
#     behind" is the assertion that then PASSES. The old expectation named the
#     assertion the defect satisfies, so no run could ever match it.
#     m3-anchor moved the panel to the other corner. Section A2's PANEL rectangle is
#     computed from the frame size by panel_rect(), so its "top-right" line asserts
#     the spec, not the drawing, and stays green; only the pixel counts moved. A2 now
#     also compares the measured BBOX with that rectangle, which is the positional
#     assertion the mutation must trip. See A2b in section_a2. m3 keeps --sections=2
#     because that is the section whose assertions are supposed to judge the anchor.
mutation_one() {
  local id="$1" file="$2" from="$3" to="$4" sections="$5" expect="$6"
  local backup="$SCRATCH/mut-$id.bak"
  echo "-- $id  $file: '$from' -> '$to'"
  cp "$file" "$backup" || { fail_one "$id could not back $file up"; return 1; }
  sed -i "s|$from|$to|" "$file"
  if ! grep -qF -- "$to" "$file"; then
    cp "$backup" "$file"; fail_one "$id was not applied: the pattern is not in $file"; return 1
  fi
  if ! bash build-target.sh tf2_demo_native > "$SCRATCH/mut-$id-build.log" 2>&1; then
    cp "$backup" "$file"
    fail_one "$id did not build (mut-$id-build.log)"
    return 1
  fi
  local grc
  MINIMAP_CHECK_OUT="$SCRATCH/mut-$id/out" MINIMAP_CHECK_SCRATCH="$SCRATCH/mut-$id/scratch" \
    bash "$0" --sections="$sections" > "$SCRATCH/mut-$id-gate.log" 2>&1
  grc=$?
  cp "$backup" "$file"
  if ! cmp -s "$backup" "$file"; then fail_one "$id restore is not byte-identical"; return 1; fi
  local reds hits
  reds="$(grep -c '^  FAIL' "$SCRATCH/mut-$id-gate.log" 2>/dev/null || true)"
  hits="$(grep '^  FAIL' "$SCRATCH/mut-$id-gate.log" 2>/dev/null | grep -cF -- "$expect" || true)"
  echo "   MUTATION $id rc=$grc red=$reds expected=$hits (log mut-$id-gate.log)"
  if [ "$grc" -eq 0 ]; then fail_one "$id was NOT caught: the gate stayed green"; return 1; fi
  if [ "${hits:-0}" -eq 0 ]; then
    fail_one "$id went red, but not on \"$expect\" -- the red is for the wrong reason"
    return 1
  fi
  pass "$id was caught on \"$expect\""
}

section_a12() {
  want 12 || return 0
  if [ "$MUTATION" -ne 1 ]; then
    echo "== A12  (skipped: needs --mutation, which builds)"
    return 0
  fi
  echo "== A12  three injected defects must turn this gate red"
  if [ "$fail" -ne 0 ]; then
    fail_one "A12 refuses to judge mutations over a red forward run"
    return 0
  fi
  local fwd
  fwd="$(sha256sum "$EXE" | awk '{print $1}')"
  echo "   forward exe sha256=$fwd"
  mutation_one m1-yflip \
    native/include/minimap.h \
    "if (projection.yFlip) {" \
    "if (false) {" \
    "6" \
    "cap_1 is drawn at the flipped position"
  mutation_one m2-heading \
    native/src/main_hud.cpp \
    "const float forwardY = -static_cast<float>(std::sin(radians));" \
    "const float forwardY = static_cast<float>(std::sin(radians));" \
    "9" \
    "a heading dot 4.0px ahead"
  mutation_one m3-anchor \
    native/include/hud_panel.h \
    "spec.anchor = 1;" \
    "spec.anchor = 0;" \
    "2" \
    "the changed pixels start at the panel's left edge"
  echo "   rebuilding the forward tree"
  if ! bash build-target.sh tf2_demo_native > "$SCRATCH/mut-restore-build.log" 2>&1; then
    fail_one "the restore rebuild failed (mut-restore-build.log)"; return 0
  fi
  local now
  now="$(sha256sum "$EXE" | awk '{print $1}')"
  if [ "$now" = "$fwd" ]; then
    pass "the restored build reproduces the forward exe ($fwd)"
  else
    fail_one "the restored build is $now, not $fwd -- the source did not come back"
  fi
}

# ---- main ------------------------------------------------------------------
if [ "$SELFTEST_PIXELS" -eq 1 ]; then
  pixel_selftest
  exit $?
fi

echo "MINIMAP-HUD-CHECK $(date -u +%Y-%m-%dT%H:%M:%SZ)"
HEAD_SHA="$(git rev-parse HEAD 2>/dev/null || echo none)"
GATE_SHA="$(sha256sum "$0" | awk '{print $1}')"
EXE_SHA="$(sha256sum "$EXE" 2>/dev/null | awk '{print $1}')"
echo "  repo=$PWD"
echo "  head=$HEAD_SHA"
echo "  exe=$EXE_ABS sha256=$(val "$EXE_SHA")"
echo "  gate=$0 sha256=$GATE_SHA"
echo "  demo=$DEMO"
echo "  scratch=$SCRATCH"
echo "  out=$OUT"

# Identity is written down before anything runs, so a later reader can tie the
# readings below to one binary, one gate file and one demo.
{
  echo "gate_sha256=$GATE_SHA"
  echo "gate_path=$PWD/$0"
  echo "head=${HEAD_SHA}"
  echo "exe=$(cygpath -m "$PWD/$EXE")"
  echo "exe_sha256=$(val "$EXE_SHA")"
  echo "exe_bytes=$(wc -c < "$EXE" 2>/dev/null | tr -d ' ')"
  echo "demo=$DEMO"
  echo "demo_sha256=$(sha256sum "$DEMO" 2>/dev/null | awk '{print $1}')"
  echo "demo_secondary=$DEMO_SUNSHINE"
  echo "demo_unlisted=$DEMO_UNLISTED"
  echo "camera_args=$CAMERA_ARGS"
  echo "camera_args_turned=--debug-camera 0 -2048 512 0 213"
  echo "yaw=$YAW yaw_back=$YAW_BACK"
  echo "sections=$SECTIONS"
  echo "mutation=$MUTATION"
  echo "old_exe=$(val "$OLD_EXE")"
} > "$OUT/identity.txt"

if [ "$PRINT_OUT" -eq 1 ]; then
  echo "PRINT-OUT=ok"
  exit 0
fi

for path in "$EXE" "$DEMO" "$DEMO_SUNSHINE" "$DEMO_UNLISTED"; do
  if [ ! -f "$path" ]; then echo "FATAL: missing input $path" >&2; exit 2; fi
done

need_pair=0
for n in 1 2 3 4 5 6 7 8 9; do want "$n" && need_pair=1; done
if [ "$need_pair" -eq 1 ]; then
  run_capture on
  NO_MINIMAP=1 run_capture offbase
fi
if want 5; then
  ALT_DEMO="$DEMO_SUNSHINE" run_capture sunshine
fi
if want 9; then
  # One more frame, for A9b: same eye, camera turned around.
  ALT_CAMERA="--debug-camera 0 -2048 512 0 213" run_capture yawback
fi
if want 10; then
  echo "  (A10 runs the old binary twice; see the SKIP line if it is not supplied)"
fi
if want 11; then
  ALT_DEMO="$DEMO_UNLISTED" run_capture unlisted
  ALT_DEMO="$DEMO_UNLISTED" NO_MINIMAP=1 run_capture unlisted_off
fi

echo
section_a1
section_a2
section_a3
section_a4
section_a5
section_a6
section_a7
section_a8
section_a9
section_a10
section_a11
section_a12

EXE_SHA_AFTER="$(sha256sum "$EXE" 2>/dev/null | awk '{print $1}')"
if [ "$MUTATION" -ne 1 ] && [ -n "$EXE_SHA" ] && [ "$EXE_SHA_AFTER" != "$EXE_SHA" ]; then
  fail_one "the exe changed during the run ($EXE_SHA -> $EXE_SHA_AFTER)"
fi

echo
{
  echo "gate_sha256=$GATE_SHA"
  echo "head=$HEAD_SHA"
  echo "exe_sha256_before=$(val "$EXE_SHA")"
  echo "exe_sha256_after=$(val "$EXE_SHA_AFTER")"
  for name in on offbase sunshine yawback oldon oldoff unlisted unlisted_off; do
    rcvar="RC_$name"
    echo "run.$name.rc=$(val "$(eval echo \"\${$rcvar:-}\")")"
  done
  echo "assertions_ok=$ok"
  echo "assertions_failed=$fails"
  echo "skipped_controls=$skipped_controls"
} > "$OUT/readings.txt"

if [ "$fail" -eq 0 ]; then
  echo "MINIMAP-HUD=PASS assertions=$((ok + fails)) skipped_controls=$skipped_controls"
else
  echo "MINIMAP-HUD=FAIL assertions=$((ok + fails)) failed=$fails skipped_controls=$skipped_controls"
fi
exit "$fail"
