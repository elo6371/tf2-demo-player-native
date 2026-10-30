#!/usr/bin/env bash
# hud-text-check.sh -- the T67 gate: the HUD pass rasterises deterministic text,
# filled rects and dots in the D3D frame, and the panel container lays out
# without drawing anything.
#
# WHY THIS EXISTS
#   T51 proved a HUD can be imported and drawn as translucent blocks. T67 adds the
#   part a screenshot can lie about: the pass must draw EXACT pixels from a table
#   compiled into the binary (so "the text is right" is a comparison against the
#   table, not against a human's impression), it must draw nothing outside the
#   region it publishes, and a codepoint with no glyph must still be visible.
#
# WHAT IT ASSERTS
#   A. the panel container's pure layout rules (10 self-test cases: anchors,
#      stacking, collapsing, narrow clamping, dropping when it cannot fit);
#   B. a --hud-text-test run publishes exactly the fixed primitive list: four
#      primitives, one text quad, 22 glyphs, one unmapped codepoint, region
#      40,60,352,164, and a pass ordinal after the world;
#   C. the captured frame's pixels: every lit glyph pixel is white and every
#      unlit one equals the HUD-less frame bit for bit; the filled rect and both
#      dots carry their exact colours; the circle mask leaves its corners alone;
#      the fallback cell is a visible box; and NOTHING outside the published
#      region differs from the HUD-less frame;
#   D. --mutation reintroduces one defect at a time and requires THIS gate to go
#      red, then restores each source byte-for-byte (cmp) and re-verifies the
#      restored build reproduces the forward reading:
#        (1) the text advance is dropped, so every glyph lands on the first;
#        (2) the unmapped fallback is dropped, so a non-ASCII codepoint vanishes;
#        (3) a primitive escapes its rect as a full-screen quad, so pixels
#            outside the published region change;
#        (4) the circle mask is dropped, so a dot paints its bounding-box corners.
#      Every mutation is scored against a green forward run: --mutation refuses to
#      start when the forward assertions above it are already red, because "the
#      mutation was caught" would then describe a gate that never worked.
#
# SINGLE INSTANCE
#   Every product launch here is a --hidden-window --audio-disabled run. The
#   script itself does not take the host lock: the caller does (verify-all.sh and
#   the task owner both run it under hostlock.sh), because a gate that acquired
#   the lock would deadlock when invoked from inside a locked run.
#
# Usage: bash hud-text-check.sh [--mutation]
# Exit:  0 = every assertion held (or, with --mutation, every mutation went red).

set -uo pipefail
cd "$(dirname "$0")"
# The trace and the demo path contain bytes outside the shell's UTF-8 locale on
# this Windows build. Byte-oriented matching must not stop at those bytes.
export LC_ALL=C

EXE=native/build-nmake/tf2_demo_native.exe
EXE_ABS="$(cygpath -m "$PWD/$EXE")"
TF="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
DEMO="D:/TF2_Demo_Player/testdata/demos/bb841c6d379ff7c40d0c8baf99f59d8d_matcha-20260927-1347-cp_snakewater_final1.dem"
RUN_TAG="$(date +%Y%m%d%H%M%S)-$$"
# OUT is the evidence that gets committed (readings, traces, verification text);
# SCRATCH is the bulk that does not (each captured BMP is ~2.5 MB and every
# mutation writes three of them).
OUT="$(printenv HUD_TEXT_CHECK_OUT)"
[ -z "$OUT" ] && OUT=evidence/t67-hudtext
SCRATCH="$(printenv HUD_TEXT_CHECK_SCRATCH)"
[ -z "$SCRATCH" ] && SCRATCH=".scratch/t67-hudtext/$RUN_TAG"
RUN_TIMEOUT=400
# Tick 0 with --start-paused and a frozen camera, for the reason hud-check.sh
# spells out: two captures compared pixel by pixel must differ only by the
# feature, and a moving camera or a later tick would make them differ everywhere.
CAMERA_ARGS="--debug-camera -3498.225 1021.852 1327.843 70.529329 134.999352"
TICK=0
AUDIO_DEVICE=6
# The fixed probe list, in one place: the gate compares the trace and the pixels
# against these numbers, and the mutation scoring uses the same region.
EXPECT_REGION="40,60,352,164"
EXPECT_PRIMITIVES=4
EXPECT_TEXTQUADS=1
EXPECT_GLYPHS=22
EXPECT_UNMAPPED=1

MUTATION=0
for arg in "$@"; do
  case "$arg" in
    --mutation) MUTATION=1 ;;
    *) echo "unknown argument: $arg"; exit 2 ;;
  esac
done

fail=0
for path in "$EXE_ABS" "$DEMO" "$TF"; do
  [ -e "$path" ] || { echo "FATAL: missing input: $path"; exit 1; }
done

PY="C:/Users/Administrator/.workbuddy-ai/binaries/python/versions/3.13.12/python.exe"
[ -x "$PY" ] || PY="C:/Users/Administrator/AppData/Local/Programs/Python/Python310/python.exe"
[ -x "$PY" ] || { echo "FATAL: no python interpreter found"; exit 1; }
for tool in cygpath timeout cmp diff; do
  command -v "$tool" >/dev/null 2>&1 || { echo "FATAL: missing tool: $tool"; exit 1; }
done

mkdir -p "$OUT" "$SCRATCH"

val() { # val <string>: "<none>" when empty, so a missing reading is visible.
  if [ -z "$1" ]; then echo "<none>"; else echo "$1"; fi
}
assert_eq() { # assert_eq <label> <actual> <expected>
  if [ "$(val "$2")" = "$3" ]; then
    echo "  OK   $1 = $3"
  else
    echo "  FAIL $1 = $(val "$2"), expected $3"; fail=1
  fi
}
assert_ge() { # assert_ge <label> <actual> <floor>
  if [ -n "$2" ] && [ "$2" -ge "$3" ] 2>/dev/null; then
    echo "  OK   $1 = $2 (>= $3)"
  else
    echo "  FAIL $1 = $(val "$2"), expected >= $3"; fail=1
  fi
}

# The trace is the product's own reading of what it drew; the bitmap is the
# pixels. Both are taken from the same frame: --capture-frame makes the product
# write the bitmap and the trace lines for the frame it captured.
run_capture() { # run_capture <name> [extra args...]
  local name="$1"; shift
  local trace="$SCRATCH/$name.trace"
  local bmp="$SCRATCH/$name.bmp"
  local csv="$SCRATCH/$name.csv"
  local log="$SCRATCH/$name.log"
  local settings="$SCRATCH/$name.settings.ini"
  local uisettings="$SCRATCH/$name.ui.ini"
  rm -f "$trace" "$bmp" "$csv" "$log" "$settings" "$uisettings"
  # --settings-file and --ui-settings-file keep the run hermetic: without them a
  # gate reads (and WRITES) whatever the tester had configured.
  TF2_NATIVE_TRACE="$(cygpath -m "$PWD/$trace")" timeout "$RUN_TIMEOUT" "$EXE" \
    --tf-root "$TF" --audio-device "$AUDIO_DEVICE" --audio-disabled --hidden-window \
    --settings-file "$(cygpath -m "$PWD/$settings")" \
    --ui-settings-file "$(cygpath -m "$PWD/$uisettings")" \
    --demo "$DEMO" --start-paused --capture-tick "$TICK" \
    --capture-frame "$(cygpath -m "$PWD/$bmp")" --metrics-file "$(cygpath -m "$PWD/$csv")" \
    $CAMERA_ARGS "$@" > "$log" 2>&1
  printf -v "RC_${name//-/_}" '%s' "$?"
}

# hudtext_field <trace> <name>: the last "name=<number>" of the last hudtext line.
hudtext_field() {
  grep -a 'hudtext:' "$1" | tail -1 | grep -oE "$2=[-0-9]+" | head -1 | cut -d= -f2
}
# hudtext_region <trace>: the last region=x,y,w,h of the last hudtext line.
hudtext_region() {
  grep -a 'hudtext:' "$1" | tail -1 | grep -oE 'region=[-0-9]+,[-0-9]+,[0-9]+,[0-9]+' | tail -1 | sed 's/region=//'
}
# hud_draw_field <trace> <name>
hud_draw_field() {
  grep -a 'hud: draw' "$1" | tail -1 | grep -oE "$2=[-0-9]+" | head -1 | cut -d= -f2
}

# verify_pixels <off.bmp> <on.bmp> <hud_font.cpp> <x> <y> <w> <h>
#
# Prints machine-readable readings and exits 0 only when every forward pixel
# claim holds. It reads the 24bpp bottom-up BMPs the product writes and the font
# table straight out of hud_font.cpp, so "the glyph pixels match the table" is a
# comparison against the same bytes the binary compiled.
verify_pixels() {
  "$PY" - "$@" <<'PYEOF'
import struct, sys, re

off_path, on_path, font_path = sys.argv[1], sys.argv[2], sys.argv[3]
rx, ry, rw, rh = (int(v) for v in sys.argv[4:8])

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
    return (p[2], p[1], p[0])  # BMP stores BGR

on, w, h = read_bmp(on_path)
off, w2, h2 = read_bmp(off_path)
if (w, h) != (w2, h2):
    raise SystemExit('frame size differs: %dx%d vs %dx%d' % (w, h, w2, h2))
print('frame=%dx%d' % (w, h))
print('regionExpected=%d' % (1 if (rx, ry, rw, rh) == (40, 60, 352, 164) else 0))

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

def near(a, b, tol=2):
    return all(abs(x - y) <= tol for x, y in zip(a, b))

rect = rgb(on, 160, 140)          # inside the filled rect (40,120,240,40)
red = rgb(on, 72, 212)            # centre of the red dot (60,200,24,24)
blue = rgb(on, 132, 212)          # centre of the blue dot (120,200,24,24)
print('rectColour=%d,%d,%d' % rect)
print('redCentre=%d,%d,%d' % red)
print('blueCentre=%d,%d,%d' % blue)
corner_bad = 0
for (cx, cy) in [(60, 200), (83, 200), (60, 223), (83, 223)]:
    if rgb(on, cx, cy) != rgb(off, cx, cy):
        corner_bad += 1
print('circleCornerDiff=%d' % corner_bad)

src = open(font_path, encoding='utf-8').read()
m = re.search(r'kFont8x8[^\n]*=\s*\{(.*?)\n\};', src, re.S)
if not m:
    raise SystemExit('font table not found in ' + font_path)
values = [int(v, 16) for v in re.findall(r'0x([0-9A-Fa-f]{2})', m.group(1))]
if len(values) != 95 * 8:
    raise SystemExit('font table has %d rows, expected 760' % len(values))
glyphs = [values[i * 8:(i + 1) * 8] for i in range(95)]
mf = re.search(r'kFallbackRows[^=]*=\s*\{([^}]*)\}', src)
if not mf:
    raise SystemExit('fallback rows not found in ' + font_path)
fallback = [int(v, 16) for v in re.findall(r'0x([0-9A-Fa-f]{2})', mf.group(1))]

def glyph_bad(rows8, gx, gy, scale=2):
    lit_bad = unlit_bad = 0
    for r in range(8):
        for c in range(8):
            bit = (rows8[r] >> c) & 1
            for dy in range(scale):
                for dx in range(scale):
                    x, y = gx + c * scale + dx, gy + r * scale + dy
                    if bit:
                        if rgb(on, x, y) != (255, 255, 255):
                            lit_bad += 1
                    else:
                        if rgb(on, x, y) != rgb(off, x, y):
                            unlit_bad += 1
    return lit_bad + unlit_bad

bad = 0
for i, ch in enumerate('TF2 0:00'):
    bad += glyph_bad(glyphs[ord(ch) - 0x20], 40 + i * 16, 60)
print('glyphBad=%d' % bad)
print('glyphChecked=8')
fb_bad = glyph_bad(fallback, 40 + 21 * 16, 60)
print('fallbackBad=%d' % fb_bad)
print('fallbackBox=%d' % (1 if fb_bad == 0 else 0))

ok = (outside == 0 and inside >= 1000 and bad == 0 and fb_bad == 0
      and corner_bad == 0 and near(rect, (31, 71, 166))
      and near(red, (255, 0, 0)) and near(blue, (26, 102, 255))
      and (minx, miny, maxx, maxy) == (40, 60, 391, 223))
print('verifyOk=%d' % (1 if ok else 0))
sys.exit(0 if ok else 1)
PYEOF
}

echo "=== T67 HUD text gate: forward ==="
echo "  exe: $(stat -c '%y %s bytes' "$EXE" 2>/dev/null || echo '<missing>')"
echo "  out: $OUT"
echo "  scratch: $SCRATCH"
echo

# ---------------------------------------------------------------------------
echo "=== 1/4 the product draws the fixed primitive list, and only it ==="
run_capture hudtext_off
run_capture hudtext_on --hud-text-test
assert_eq "hud-text-off exit code" "$RC_hudtext_off" "0"
assert_eq "hud-text-on exit code" "$RC_hudtext_on" "0"

OFF_DRAWN=$(hud_draw_field "$SCRATCH/hudtext_off.trace" drawn)
OFF_ORDINAL=$(hud_draw_field "$SCRATCH/hudtext_off.trace" ordinal)
ON_DRAWN=$(hud_draw_field "$SCRATCH/hudtext_on.trace" drawn)
ON_RECTS=$(hud_draw_field "$SCRATCH/hudtext_on.trace" rects)
ON_SCISSOR=$(hud_draw_field "$SCRATCH/hudtext_on.trace" scissor)
ON_ORDINAL=$(hud_draw_field "$SCRATCH/hudtext_on.trace" ordinal)
ON_PRIMITIVES=$(hudtext_field "$SCRATCH/hudtext_on.trace" primitives)
ON_DRAWN_PRIM=$(hudtext_field "$SCRATCH/hudtext_on.trace" drawn)
ON_TEXTQUADS=$(hudtext_field "$SCRATCH/hudtext_on.trace" textQuads)
ON_GLYPHS=$(hudtext_field "$SCRATCH/hudtext_on.trace" glyphs)
ON_UNMAPPED=$(hudtext_field "$SCRATCH/hudtext_on.trace" unmapped)
ON_PASSORDINAL=$(hudtext_field "$SCRATCH/hudtext_on.trace" passOrdinal)
ON_REGION=$(hudtext_region "$SCRATCH/hudtext_on.trace")
OFF_PRIMITIVES=$(hudtext_field "$SCRATCH/hudtext_off.trace" primitives)
OFF_TEXTQUADS=$(hudtext_field "$SCRATCH/hudtext_off.trace" textQuads)
echo "  off: hud drawn=$(val "$OFF_DRAWN") ordinal=$(val "$OFF_ORDINAL") hudtext primitives=$(val "$OFF_PRIMITIVES") textQuads=$(val "$OFF_TEXTQUADS")"
echo "  on:  hud drawn=$(val "$ON_DRAWN") rects=$(val "$ON_RECTS") scissor=$(val "$ON_SCISSOR") ordinal=$(val "$ON_ORDINAL")"
echo "  on:  hudtext primitives=$(val "$ON_PRIMITIVES") drawn=$(val "$ON_DRAWN_PRIM") textQuads=$(val "$ON_TEXTQUADS") glyphs=$(val "$ON_GLYPHS") unmapped=$(val "$ON_UNMAPPED") passOrdinal=$(val "$ON_PASSORDINAL") region=$(val "$ON_REGION")"

# The default-off half: a plain capture run draws no HUD at all.
assert_eq "a hud-off run draws nothing" "$OFF_DRAWN" "0"
assert_eq "a hud-off run publishes no text primitives" "$OFF_PRIMITIVES" "0"
assert_eq "a hud-off run emits no text quads" "$OFF_TEXTQUADS" "0"
# The probe list is fixed, so every count is a named reading.
assert_eq "the probe publishes four primitives" "$ON_PRIMITIVES" "$EXPECT_PRIMITIVES"
assert_eq "all four primitives are drawn" "$ON_DRAWN_PRIM" "$EXPECT_PRIMITIVES"
assert_eq "the text primitive becomes one text quad" "$ON_TEXTQUADS" "$EXPECT_TEXTQUADS"
assert_eq "the text expands to 22 glyphs" "$ON_GLYPHS" "$EXPECT_GLYPHS"
assert_eq "one codepoint has no glyph and uses the fallback" "$ON_UNMAPPED" "$EXPECT_UNMAPPED"
assert_eq "the pass publishes the fixed region" "$ON_REGION" "$EXPECT_REGION"
assert_ge "the HUD pass runs after the world" "$ON_PASSORDINAL" "1"
# The T51 reading must stay coherent: one Rect primitive (the filled panel), so
# drawn rects = 1, and the pass scissored itself.
assert_eq "the filled rect is counted as a drawn rect" "$ON_RECTS" "1"
assert_eq "the pass applied its scissor" "$ON_SCISSOR" "1"

# ---------------------------------------------------------------------------
echo
echo "=== 2/4 the panel container's layout rules are pure logic ==="
PANEL_LINE=$(grep -a 'hudpanel:' "$SCRATCH/hudtext_off.trace" | tail -1)
echo "  reading: $(val "$PANEL_LINE")"
PANEL_CASES=$(echo "$PANEL_LINE" | grep -oE 'cases=[0-9]+' | head -1 | cut -d= -f2)
PANEL_PASS=$(echo "$PANEL_LINE" | grep -oE 'pass=[0-9]+' | head -1 | cut -d= -f2)
PANEL_FAIL=$(echo "$PANEL_LINE" | grep -oE 'fail=[0-9]+' | head -1 | cut -d= -f2)
assert_ge "the panel self-test ran cases" "$PANEL_CASES" "10"
assert_eq "every panel layout case passes" "$PANEL_FAIL" "0"
assert_eq "all cases were scored" "$PANEL_PASS" "$PANEL_CASES"

# ---------------------------------------------------------------------------
echo
echo "=== 3/4 the captured frame matches the font table and the probe colours ==="
verify_pixels "$SCRATCH/hudtext_off.bmp" "$SCRATCH/hudtext_on.bmp" \
  native/src/hud_font.cpp 40 60 352 164 > "$OUT/pixels.txt"
PIXELS_RC=$?
sed 's/^/  /' "$OUT/pixels.txt"
PIXELS_OUTSIDE=$(sed -n 's/^outsideDiff=//p' "$OUT/pixels.txt")
PIXELS_INSIDE=$(sed -n 's/^insideDiff=//p' "$OUT/pixels.txt")
PIXELS_GLYPHBAD=$(sed -n 's/^glyphBad=//p' "$OUT/pixels.txt")
PIXELS_FALLBACK=$(sed -n 's/^fallbackBox=//p' "$OUT/pixels.txt")
PIXELS_CORNER=$(sed -n 's/^circleCornerDiff=//p' "$OUT/pixels.txt")
PIXELS_BBOX=$(sed -n 's/^diffBBox=//p' "$OUT/pixels.txt")
PIXELS_REGIONEXP=$(sed -n 's/^regionExpected=//p' "$OUT/pixels.txt")
assert_eq "the pixel verifier holds" "$PIXELS_RC" "0"
assert_eq "nothing outside the published region changed" "$PIXELS_OUTSIDE" "0"
assert_ge "the region really did change" "$PIXELS_INSIDE" "1000"
assert_eq "every checked glyph pixel matches the table" "$PIXELS_GLYPHBAD" "0"
assert_eq "the unmapped codepoint draws the fallback box" "$PIXELS_FALLBACK" "1"
assert_eq "the circle mask leaves its bounding-box corners alone" "$PIXELS_CORNER" "0"
assert_eq "the changed pixels are exactly the published region" "$PIXELS_BBOX" "40,60,391,223"
assert_eq "the published region is the probe's fixed region" "$PIXELS_REGIONEXP" "1"

echo
if [ "$fail" -ne 0 ]; then
  echo "HUD-TEXT-CHECK=FAIL"
  [ "$MUTATION" -eq 1 ] && { echo "MUTATION-SUITE=REFUSED (forward red)"; exit 1; }
  exit 1
fi

# ---------------------------------------------------------------------------
if [ "$MUTATION" -eq 1 ]; then
  # =======================================================================
  # MUTATION. Each mutation reintroduces ONE defect, rebuilds, and requires
  # THIS gate to go red. A gate that stays green on a broken product is a gate
  # that proves nothing, and it is the failure mode a forward-only check cannot
  # detect about itself.
  #
  # Restore is a byte-for-byte copy from a scratch backup, verified with cmp,
  # followed by a rebuild -- because "the file is back" and "the build is back"
  # are different claims and only the second one makes the next run meaningful.
  #
  # The comparison region is the FORWARD run's region, never the mutated run's:
  # a mutated build that publishes a wider (or smaller) region would otherwise
  # widen its own alibi until the comparison passed by construction.
  # =======================================================================
  echo
  echo "MUTATION-SUITE: rebuilding the product once per defect, four defects"

  mrc=0
  MUT_FILE=""
  MUT_BACKUP=""
  cleanup() {
    if [ -n "$MUT_FILE" ] && [ -f "$MUT_BACKUP" ]; then
      cp "$MUT_BACKUP" "$MUT_FILE"
      echo "  CLEANUP: restored $MUT_FILE after an interrupted run (rebuild before reuse)"
    fi
  }
  trap cleanup EXIT INT TERM

  mut_restore() { # mut_restore <target> <backup>
    cp "$2" "$1"
    if cmp -s "$1" "$2"; then
      echo "  RESTORED $1 (byte-identical to the backup)"
      MUT_FILE=""
    else
      echo "  RESTORE-FAIL $1 differs from its backup"; mrc=1
    fi
  }

  mutation_begin() { # mutation_begin <file> <backup>: remember the pair for the trap.
    MUT_FILE="$1"
    MUT_BACKUP="$2"
  }

  # run_mutation_pixels <tag>: capture off+on for the mutated build and verify
  # against the FORWARD region. Prints the verifier output to OUT/<tag>.pixels.txt.
  run_mutation_pixels() {
    local tag="$1"
    run_capture "$tag-off"
    run_capture "$tag-on" --hud-text-test
    verify_pixels "$SCRATCH/$tag-off.bmp" "$SCRATCH/$tag-on.bmp" \
      native/src/hud_font.cpp 40 60 352 164 > "$OUT/$tag.pixels.txt" 2>&1
    local rc=$?
    sed 's/^/  /' "$OUT/$tag.pixels.txt"
    return $rc
  }

  # ---- mutation 1: the text advance is dropped ---------------------------
  #
  # The defect: penX never moves, so all 22 glyphs land on the first cell. The
  # trace still says glyphs=22 and textQuads=1, which is exactly why the gate
  # cannot stop at the counts: the frame's glyph pixels must go red.
  echo
  echo "--- mutation 1/4: the text advance is dropped ---"
  cp native/src/native_renderer_frame.cpp "$SCRATCH/native_renderer.cpp.backup"
  mutation_begin native/src/native_renderer_frame.cpp "$SCRATCH/native_renderer.cpp.backup"
  "$PY" - native/src/native_renderer_frame.cpp <<'PYEOF'
import sys
path = sys.argv[1]
text = open(path, encoding='utf-8').read()
needle = "        penX += glyphWidth;"
replacement = "        penX += 0.0f * glyphWidth;"
if needle not in text:
    raise SystemExit("mutation-1 anchor not found")
open(path, 'w', encoding='utf-8', newline='').write(text.replace(needle, replacement, 1))
PYEOF
  if cmp -s native/src/native_renderer_frame.cpp "$SCRATCH/native_renderer.cpp.backup"; then
    echo "  MUTATION-SETUP=FAIL (mutation 1 changed nothing)"; mrc=1
  else
    bash build-target.sh tf2_demo_native > "$OUT/mut1.build" 2>&1
    MUT_BUILD=$?
    if [ "$MUT_BUILD" -ne 0 ]; then
      echo "  MUTATION-INVALID m1: the mutated product does not build (rc=$MUT_BUILD)"
      mrc=1
    else
      run_mutation_pixels mut1
      MUT1_GLYPHBAD=$(sed -n 's/^glyphBad=//p' "$OUT/mut1.pixels.txt")
      MUT1_OUTSIDE=$(sed -n 's/^outsideDiff=//p' "$OUT/mut1.pixels.txt")
      MUT1_GLYPHS=$(hudtext_field "$SCRATCH/mut1-on.trace" glyphs)
      echo "  mutated readings: glyphs=$(val "$MUT1_GLYPHS") glyphBad=$(val "$MUT1_GLYPHBAD") outsideDiff=$(val "$MUT1_OUTSIDE") (forward glyphBad=0)"
      if [ -n "$MUT1_GLYPHBAD" ] && [ "$MUT1_GLYPHBAD" -gt 0 ] 2>/dev/null && [ "$MUT1_OUTSIDE" = "0" ]; then
        echo "  MUTATION-RED   m1: text advance dropped -- the frame no longer matches the table (glyphBad=$MUT1_GLYPHBAD)"
      else
        echo "  MUTATION-GREEN m1: the gate stayed green on a collapsed text run"; mrc=1
      fi
    fi
    mut_restore native/src/native_renderer_frame.cpp "$SCRATCH/native_renderer.cpp.backup"
    bash build-target.sh tf2_demo_native > "$OUT/mut1.restore.build" 2>&1 \
      || { echo "  RESTORE-BUILD=FAIL (mutation 1)"; mrc=1; }
    run_capture mut1-restored --hud-text-test
    REST1=$(grep -a 'hudtext:' "$SCRATCH/mut1-restored.trace" | tail -1 | sed -E 's/^[0-9]+([.][0-9]+)? //')
    FWD1=$(grep -a 'hudtext:' "$SCRATCH/hudtext_on.trace" | tail -1 | sed -E 's/^[0-9]+([.][0-9]+)? //')
    if [ "$REST1" = "$FWD1" ]; then
      echo "  RESTORED-READING-IDENTICAL (the restored build reproduces the forward hudtext line)"
    else
      echo "  RESTORED-READING-DIFFERS"; echo "    forward: $FWD1"; echo "    restored: $REST1"; mrc=1
    fi
  fi

  # ---- mutation 2: the unmapped fallback is dropped ----------------------
  #
  # The defect: a codepoint with no glyph is skipped instead of drawn with the
  # fallback cell. unmapped=0 in the trace and the fallback box disappears from
  # the frame; the gate must catch both.
  echo
  echo "--- mutation 2/4: the unmapped fallback is dropped ---"
  cp native/src/native_renderer_frame.cpp "$SCRATCH/native_renderer.cpp.backup"
  mutation_begin native/src/native_renderer_frame.cpp "$SCRATCH/native_renderer.cpp.backup"
  "$PY" - native/src/native_renderer_frame.cpp <<'PYEOF'
import sys
path = sys.argv[1]
text = open(path, encoding='utf-8').read()
needle = ("        if (cell < 0) {\n"
          "          cell = kHudFontFallbackCell;\n"
          "          ++hudUnmappedGlyphs_;\n"
          "        }")
replacement = ("        if (cell < 0) {\n"
               "          continue;\n"
               "        }")
if needle not in text:
    raise SystemExit("mutation-2 anchor not found")
open(path, 'w', encoding='utf-8', newline='').write(text.replace(needle, replacement, 1))
PYEOF
  if cmp -s native/src/native_renderer_frame.cpp "$SCRATCH/native_renderer.cpp.backup"; then
    echo "  MUTATION-SETUP=FAIL (mutation 2 changed nothing)"; mrc=1
  else
    bash build-target.sh tf2_demo_native > "$OUT/mut2.build" 2>&1
    MUT_BUILD=$?
    if [ "$MUT_BUILD" -ne 0 ]; then
      echo "  MUTATION-INVALID m2: the mutated product does not build (rc=$MUT_BUILD)"
      mrc=1
    else
      run_mutation_pixels mut2
      MUT2_UNMAPPED=$(hudtext_field "$SCRATCH/mut2-on.trace" unmapped)
      MUT2_GLYPHS=$(hudtext_field "$SCRATCH/mut2-on.trace" glyphs)
      MUT2_FALLBACK=$(sed -n 's/^fallbackBox=//p' "$OUT/mut2.pixels.txt")
      echo "  mutated readings: glyphs=$(val "$MUT2_GLYPHS") unmapped=$(val "$MUT2_UNMAPPED") fallbackBox=$(val "$MUT2_FALLBACK") (forward unmapped=1 fallbackBox=1)"
      if [ "$MUT2_UNMAPPED" != "$EXPECT_UNMAPPED" ] && [ "$MUT2_FALLBACK" = "0" ]; then
        echo "  MUTATION-RED   m2: fallback dropped -- unmapped=$MUT2_UNMAPPED and the fallback box is gone"
      else
        echo "  MUTATION-GREEN m2: the gate stayed green with the fallback removed"; mrc=1
      fi
    fi
    mut_restore native/src/native_renderer_frame.cpp "$SCRATCH/native_renderer.cpp.backup"
    bash build-target.sh tf2_demo_native > "$OUT/mut2.restore.build" 2>&1 \
      || { echo "  RESTORE-BUILD=FAIL (mutation 2)"; mrc=1; }
    run_capture mut2-restored --hud-text-test
    REST2=$(grep -a 'hudtext:' "$SCRATCH/mut2-restored.trace" | tail -1 | sed -E 's/^[0-9]+([.][0-9]+)? //')
    FWD2=$(grep -a 'hudtext:' "$SCRATCH/hudtext_on.trace" | tail -1 | sed -E 's/^[0-9]+([.][0-9]+)? //')
    if [ "$REST2" = "$FWD2" ]; then
      echo "  RESTORED-READING-IDENTICAL (the restored build reproduces the forward hudtext line)"
    else
      echo "  RESTORED-READING-DIFFERS"; echo "    forward: $FWD2"; echo "    restored: $REST2"; mrc=1
    fi
  fi

  # ---- mutation 3: a primitive escapes its rect -------------------------
  #
  # The defect: every non-text primitive is drawn as a FULL-SCREEN quad instead
  # of inside its own rect. That is the shape of "the HUD painted over the
  # world", and it must make the outside-the-published-region comparison red.
  # A mutation that only removed the scissor would NOT do that, because the
  # quads already lie inside the rects; the mutation has to change what is
  # drawn, not only how it is clipped.
  echo
  echo "--- mutation 3/4: a primitive escapes its rect ---"
  cp native/src/native_renderer_frame.cpp "$SCRATCH/native_renderer.cpp.backup"
  mutation_begin native/src/native_renderer_frame.cpp "$SCRATCH/native_renderer.cpp.backup"
  "$PY" - native/src/native_renderer_frame.cpp <<'PYEOF'
import sys
path = sys.argv[1]
text = open(path, encoding='utf-8').read()
needle = ("    const float left = (std::max)(0.0f, primitive.x);\n"
          "    const float top = (std::max)(0.0f, primitive.y);\n"
          "    const float right = (std::min)(static_cast<float>(width_), primitive.x + width);\n"
          "    const float bottom = (std::min)(static_cast<float>(height_), primitive.y + height);")
replacement = ("    const float left = 0.0f;\n"
               "    const float top = 0.0f;\n"
               "    const float right = static_cast<float>(width_);\n"
               "    const float bottom = static_cast<float>(height_);")
if needle not in text:
    raise SystemExit("mutation-3 anchor not found")
open(path, 'w', encoding='utf-8', newline='').write(text.replace(needle, replacement, 1))
PYEOF
  if cmp -s native/src/native_renderer_frame.cpp "$SCRATCH/native_renderer.cpp.backup"; then
    echo "  MUTATION-SETUP=FAIL (mutation 3 changed nothing)"; mrc=1
  else
    bash build-target.sh tf2_demo_native > "$OUT/mut3.build" 2>&1
    MUT_BUILD=$?
    if [ "$MUT_BUILD" -ne 0 ]; then
      echo "  MUTATION-INVALID m3: the mutated product does not build (rc=$MUT_BUILD)"
      mrc=1
    else
      run_mutation_pixels mut3
      MUT3_OUTSIDE=$(sed -n 's/^outsideDiff=//p' "$OUT/mut3.pixels.txt")
      MUT3_REGION=$(hudtext_region "$SCRATCH/mut3-on.trace")
      echo "  mutated readings: outsideDiff=$(val "$MUT3_OUTSIDE") region=$(val "$MUT3_REGION") (forward outsideDiff=0 region=$EXPECT_REGION)"
      if [ -n "$MUT3_OUTSIDE" ] && [ "$MUT3_OUTSIDE" -gt 0 ] 2>/dev/null; then
        echo "  MUTATION-RED   m3: a primitive escaped its rect -- $MUT3_OUTSIDE pixels outside the forward region changed"
      else
        echo "  MUTATION-GREEN m3: the gate stayed green with a full-screen primitive"; mrc=1
      fi
    fi
    mut_restore native/src/native_renderer_frame.cpp "$SCRATCH/native_renderer.cpp.backup"
    bash build-target.sh tf2_demo_native > "$OUT/mut3.restore.build" 2>&1 \
      || { echo "  RESTORE-BUILD=FAIL (mutation 3)"; mrc=1; }
    run_capture mut3-restored --hud-text-test
    REST3=$(grep -a 'hudtext:' "$SCRATCH/mut3-restored.trace" | tail -1 | sed -E 's/^[0-9]+([.][0-9]+)? //')
    FWD3=$(grep -a 'hudtext:' "$SCRATCH/hudtext_on.trace" | tail -1 | sed -E 's/^[0-9]+([.][0-9]+)? //')
    if [ "$REST3" = "$FWD3" ]; then
      echo "  RESTORED-READING-IDENTICAL (the restored build reproduces the forward hudtext line)"
    else
      echo "  RESTORED-READING-DIFFERS"; echo "    forward: $FWD3"; echo "    restored: $REST3"; mrc=1
    fi
  fi

  # ---- mutation 4: the circle mask is dropped ---------------------------
  #
  # The defect: the dot shader returns its colour for the whole quad instead of
  # discarding outside the disc, so a dot paints its bounding-box corners. The
  # corner comparison must go red; the centre stays the right colour, which is
  # why the gate checks the corners and not only the centre.
  echo
  echo "--- mutation 4/4: the circle mask is dropped ---"
  cp native/src/native_renderer.cpp "$SCRATCH/native_renderer.cpp.backup"
  mutation_begin native/src/native_renderer.cpp "$SCRATCH/native_renderer.cpp.backup"
  "$PY" - native/src/native_renderer.cpp <<'PYEOF'
import sys
path = sys.argv[1]
text = open(path, encoding='utf-8').read()
needle = "    if (dot(offset, offset) > 0.25) discard;"
replacement = "    if (false) discard;"
if needle not in text:
    raise SystemExit("mutation-4 anchor not found")
open(path, 'w', encoding='utf-8', newline='').write(text.replace(needle, replacement, 1))
PYEOF
  if cmp -s native/src/native_renderer.cpp "$SCRATCH/native_renderer.cpp.backup"; then
    echo "  MUTATION-SETUP=FAIL (mutation 4 changed nothing)"; mrc=1
  else
    bash build-target.sh tf2_demo_native > "$OUT/mut4.build" 2>&1
    MUT_BUILD=$?
    if [ "$MUT_BUILD" -ne 0 ]; then
      echo "  MUTATION-INVALID m4: the mutated product does not build (rc=$MUT_BUILD)"
      mrc=1
    else
      run_mutation_pixels mut4
      MUT4_CORNER=$(sed -n 's/^circleCornerDiff=//p' "$OUT/mut4.pixels.txt")
      echo "  mutated readings: circleCornerDiff=$(val "$MUT4_CORNER") (forward 0)"
      if [ -n "$MUT4_CORNER" ] && [ "$MUT4_CORNER" -gt 0 ] 2>/dev/null; then
        echo "  MUTATION-RED   m4: circle mask dropped -- $MUT4_CORNER dot corners are painted"
      else
        echo "  MUTATION-GREEN m4: the gate stayed green with a square dot"; mrc=1
      fi
    fi
    mut_restore native/src/native_renderer.cpp "$SCRATCH/native_renderer.cpp.backup"
    bash build-target.sh tf2_demo_native > "$OUT/mut4.restore.build" 2>&1 \
      || { echo "  RESTORE-BUILD=FAIL (mutation 4)"; mrc=1; }
    run_capture mut4-restored --hud-text-test
    REST4=$(grep -a 'hudtext:' "$SCRATCH/mut4-restored.trace" | tail -1 | sed -E 's/^[0-9]+([.][0-9]+)? //')
    FWD4=$(grep -a 'hudtext:' "$SCRATCH/hudtext_on.trace" | tail -1 | sed -E 's/^[0-9]+([.][0-9]+)? //')
    if [ "$REST4" = "$FWD4" ]; then
      echo "  RESTORED-READING-IDENTICAL (the restored build reproduces the forward hudtext line)"
    else
      echo "  RESTORED-READING-DIFFERS"; echo "    forward: $FWD4"; echo "    restored: $REST4"; mrc=1
    fi
  fi

  echo
  if [ "$mrc" -eq 0 ]; then
    echo "MUTATION-SUITE=ALL-RED-RESTORED (4/4 mutations red, every source restored byte-identically and rebuilt)"
    echo
    echo "HUD-TEXT-CHECK=PASS"
    exit 0
  fi
  echo "MUTATION-SUITE=FAIL"
  echo
  echo "HUD-TEXT-CHECK=FAIL"
  exit 1
fi

echo
echo "HUD-TEXT-CHECK=PASS"
exit 0
