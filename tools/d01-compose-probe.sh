#!/usr/bin/env bash
# d01-compose-probe.sh -- D01 step 2: at the two OPEN pixels, read the colour the
# product path is about to write, and the terms that make it.
#
# Step 1 (tools/d01-colour0-readback.sh) answered "is COLOR0 there?" -- yes it is:
# at 849,396 the vertex colour is 70,70,70 and at 919,379 it is 255,255,255. So
# the black pixels are not missing vertex colour; something downstream is zero.
#
# This probe reads two diagnostic views that already exist (N06 probes 4 and 5 in
# native/src/native_renderer_shaders.cpp:615-625, selected by TF2_NATIVE_WORLD_VIEW
# per native_renderer_frame.cpp:1055-1057):
#   view 14 (1.62): RGB = albedo * lightingRgb -- the colour the product path is
#                   about to write, fog not yet blended in.
#   view 15 (1.52): R = diffuse, G = |Ng| (0 when the vertex carried no usable
#                   normal), B = lighting / 2.
#   view 13 (1.7):  the lightmap probe, for the same pixels.
#
# Decision rule, stated before the numbers arrive:
#   * view14 grey where the product frame is black  -> the value is fine and the
#     arithmetic AFTER the lightmap (fog/blend/output) zeroed it;
#   * view14 black                                  -> albedo * lighting is zero,
#     and view15 says which factor: diffuse 0 (dead normal) or lighting 0.
# Neither outcome is assumed here; the script prints the pixels and the reading.
#
# Recipe is identical to the black-account gate and step 1: paused demo, tick 0,
# world-sky on, debug camera -600 -600 950 20 45, so the frames are comparable.
#
# Exit: 0 every view captured and every point read (prints D01-COMPOSE=PASS);
#       1 a run or reading is missing; 2 usage/environment.
set -uo pipefail
cd "$(dirname "$0")/.." 2>/dev/null || { echo "FATAL: not inside the repository" >&2; exit 2; }

EXE=native/build-nmake/tf2_demo_native.exe
TF="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
DEMO="E:/demo测试/cp_sultry_b8a/STV/f63a7a2040379d7d6a8414f76bbb81dc_matcha-20260703-1442-cp_sultry_b8a.dem"
OUT="${D01C_OUT:-.scratch/d01-compose}"
EV="${D01C_EVIDENCE:-evidence/d01-compose}"
PTS="849,396;919,379;902,385;873,366"
VIEWS="${D01C_VIEWS:-14 15 13}"
mkdir -p "$OUT" "$EV"

for path in "$EXE" "$DEMO" "$TF"; do
  [ -e "$path" ] || { echo "FATAL: missing input: $path" >&2; exit 2; }
done
command -v python >/dev/null 2>&1 || { echo "FATAL: missing python" >&2; exit 2; }

. "$(dirname "$0")/../verify-iso.sh"
ISO_DIR=$(mktemp -d "${TMPDIR:-/tmp}/tf2-d01c.XXXXXX") || exit 2
ISO_SETTINGS="$ISO_DIR/settings.ini"
ISO_UI="$ISO_DIR/ui.ini"
iso_seed_gate_settings "$ISO_DIR" || { echo "FATAL: iso seed failed" >&2; exit 2; }
trap 'rm -rf "$ISO_DIR"' EXIT

abs_win_path() {
  case "$1" in
    /*|[A-Za-z]:*) cygpath -m "$1" ;;
    *)            cygpath -m "$PWD/$1" ;;
  esac
}

pixel() { # pixel <bmp> <x> <y> -> r,g,b
  python - "$(abs_win_path "$1")" "$2" "$3" <<'PY'
import struct, sys
path, x, y = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
d = open(path, 'rb').read()
off = struct.unpack_from('<I', d, 10)[0]
w = struct.unpack_from('<i', d, 18)[0]
h = struct.unpack_from('<i', d, 22)[0]
bpp = struct.unpack_from('<H', d, 28)[0]
stride = ((w * bpp + 31) // 32) * 4
row = (h - 1 - y) if h > 0 else y
o = off + row * stride + x * (bpp // 8)
print('%d,%d,%d' % (d[o+2], d[o+1], d[o]))
PY
}

run() { # run <tag> <view>
  local tag="$1" view="$2"
  rm -f "$OUT/$tag.bmp" "$OUT/$tag.trace" "$OUT/$tag.log"
  TF2_NATIVE_WORLD_VIEW="$view" TF2_NATIVE_TRACE="$(abs_win_path "$OUT/$tag.trace")" \
  timeout 420 "$EXE" \
    --tf-root "$TF" --demo "$DEMO" --audio-disabled --hidden-window \
    --start-paused --capture-tick 0 --world-sky \
    --settings-file "$(abs_win_path "$ISO_SETTINGS")" \
    --ui-settings-file "$(abs_win_path "$ISO_UI")" \
    --debug-camera -600 -600 950 20 45 \
    --capture-frame "$(abs_win_path "$OUT/$tag.bmp")" > "$OUT/$tag.log" 2>&1
  echo $?
}

MR="$EV/readings.txt"
{
  echo "# D01 compose probe, $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "# exe=$(sha256sum native/build-nmake/tf2_demo_native.exe | cut -c1-16) head=$(git rev-parse --short=8 HEAD)"
  echo "# demo=cp_sultry_b8a STV tick=0 camera='-600 -600 950 20 45'"
  echo "# view14 R,G,B = albedo*lighting (product colour before fog)"
  echo "# view15 R = diffuse   G = |Ng| (1 = usable normal)   B = lighting/2"
  echo "# view13 = the lightmap probe"
  echo ""
} > "$MR"

fail=0
for v in $VIEWS; do
  rc=$(run "view$v" "$v")
  echo "run view$v rc=$rc" | tee -a "$MR"
  [ "$rc" -eq 0 ] || fail=1
  [ -s "$OUT/view$v.bmp" ] || { echo "FAIL missing view$v.bmp" | tee -a "$MR"; fail=1; }
done

echo "" >> "$MR"
hdr="pixel"; for v in $VIEWS; do hdr="$hdr\tview$v"; done
printf "$hdr\tnote\n" >> "$MR"
for p in $(printf '%s' "$PTS" | tr ';' ' '); do
  x=${p%,*}; y=${p#*,}
  case "$p" in
    849,396) note="OPEN: black in the product frame" ;;
    919,379) note="OPEN: black in the product frame" ;;
    902,385) note="control: trace and draw agree" ;;
    873,366) note="control: alpha=0 texel" ;;
    *)       note="" ;;
  esac
  line="$p"
  for v in $VIEWS; do
    val=$(pixel "$OUT/view$v.bmp" "$x" "$y" 2>/dev/null || echo "?,?,?")
    line="$line\t$val"
  done
  printf '%b\t%s\n' "$line" "$note" >> "$MR"
done

echo "" >> "$MR"
echo "## how to read it" >> "$MR"
echo "view14 grey where the product frame is black -> the value is fine, arithmetic after" >> "$MR"
echo "  the lightmap zeroed it; view14 black -> albedo*lighting is zero, and view15 says" >> "$MR"
echo "  which factor (diffuse 0 = dead normal, lighting 0 = the lighting product)." >> "$MR"

cat "$MR"
if [ "$fail" -eq 0 ]; then echo "D01-COMPOSE=PASS"; exit 0; fi
echo "D01-COMPOSE=FAIL"; exit 1
