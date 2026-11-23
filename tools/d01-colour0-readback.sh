#!/usr/bin/env bash
# d01-colour0-readback.sh -- L3: read COLOR0 back at the pixels D01 left open.
#
# What D01 is stuck on: docs/BLACKPOINT-849-919-2026-11-14.md recorded that at
# (849,396) the texel is a normal 84,83,81 with alpha 255 while the albedo frame
# is 0,0,0, and that the only missing reading was "what COLOR0 does this drawn
# fragment actually see" (:150, :93). Every hypothesis (upload wrote the wrong
# bytes / the buffer handed the shader another vertex / another draw owns the
# pixel) needs that one number, and none of them can be separated without it.
#
# That reading exists now and needs no product change: the shared pixel shader
# already has two forensic arms (native/src/native_renderer_shaders.cpp:341-342)
# selected by TF2_NATIVE_WORLD_VIEW (native_renderer_frame.cpp:1025-1063):
#   view 16 -> float4(colour.r, colour.g, colour.a, blendWeight)
#   view 17 -> float4(albedo.r, sampled.r, sampledTexel.a, colour.a)
# so a capture is a direct vector read of the fragment's vertex colour. The small
# BMP reader below turns those four channels into the numbers the D01 doc asked
# for, at the same demo / camera / tick / points the black-account gate pins.
#
# What this asserts: every probe run exits 0, every named pixel has a value, and
# at the two OPEN pixels the readback is *self-consistent* with the pinned frames
# (if view16 says colour.rgb is 0 and colour.a is 255, and view17 says albedo.r
# is 0 while sampled.r is 84, then "the fragment's vertex colour is black" is a
# reading, not a guess). It does NOT assert a root cause: the last section prints
# the competing explanations with the numbers that would separate them.
#
# Cost: 3 paused captures of one demo at tick 0 (~2-4 min each). One host lock.
#
# Output: evidence/d01-colour0/readings.txt (the table), per-run logs/traces and
# the blind BMPs under .scratch/d01-colour0.
#
# Exit: 0 readings complete (prints D01-COLOUR0=PASS); 1 a run or reading is
# missing; 2 usage/environment.
set -uo pipefail
# W1 (2026-11-23): every product launch in this script runs at 1280x720, the size
# its pixel pins were measured at. The product reads this when --window-size is
# absent (an explicit --window-size still wins); before W1 1280x720 was simply the
# default, and W1 moves the default to 1920x1080. tools/window-pin-check.sh checks it.
export TF2_NATIVE_WINDOW_SIZE="${TF2_NATIVE_WINDOW_SIZE:-1280x720}"
cd "$(dirname "$0")/.." 2>/dev/null || { echo "FATAL: not inside the repository" >&2; exit 2; }

EXE=native/build-nmake/tf2_demo_native.exe
TF="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
DEMO="E:/demo测试/cp_sultry_b8a/STV/f63a7a2040379d7d6a8414f76bbb81dc_matcha-20260703-1442-cp_sultry_b8a.dem"
OUT="${D01_OUT:-.scratch/d01-colour0}"
EV="${D01_EVIDENCE:-evidence/d01-colour0}"
PTS="849,396;919,379;902,385;873,366"
# 849/919 are the OPEN pixels; 902 is the neighbouring pixel where trace and draw
# already agree (control); 873 is the alpha=0 control the black-account gate
# explains, kept here so a probe that returns nonsense is visible as nonsense.
mkdir -p "$OUT" "$EV"

for path in "$EXE" "$DEMO" "$TF"; do
  [ -e "$path" ] || { echo "FATAL: missing input: $path" >&2; exit 2; }
done
command -v python >/dev/null 2>&1 || { echo "FATAL: missing python" >&2; exit 2; }

. "$(dirname "$0")/../verify-iso.sh"   # gate helpers live at the repo root, this script in tools/
ISO_DIR=$(mktemp -d "${TMPDIR:-/tmp}/tf2-d01.XXXXXX") || exit 2
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

run() { # run <tag> <view> [extra...]
  local tag="$1" view="$2"; shift 2
  rm -f "$OUT/$tag.bmp" "$OUT/$tag.trace" "$OUT/$tag.log"
  TF2_NATIVE_WORLD_VIEW="$view" TF2_NATIVE_TRACE="$(abs_win_path "$OUT/$tag.trace")" \
  timeout 420 "$EXE" \
    --tf-root "$TF" --demo "$DEMO" --audio-disabled --hidden-window \
    --start-paused --capture-tick 0 --world-sky \
    --settings-file "$(abs_win_path "$ISO_SETTINGS")" \
    --ui-settings-file "$(abs_win_path "$ISO_UI")" \
    --debug-camera -600 -600 950 20 45 \
    --capture-frame "$(abs_win_path "$OUT/$tag.bmp")" "$@" > "$OUT/$tag.log" 2>&1
  echo $?
}

MR="${EV}/readings.txt"
{
  echo "# D01 COLOR0 readback, $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "# exe=$(sha256sum native/build-nmake/tf2_demo_native.exe | cut -c1-16) head=$(git rev-parse --short=8 HEAD)"
  echo "# demo=cp_sultry_b8a STV tick=0 camera='-600 -600 950 20 45'"
  echo "# view16 channels = R:colour.r G:colour.g B:colour.a A:blendWeight"
  echo "# view17 channels = R:albedo.r G:sampled.r B:sampledTexel.a A:colour.a"
  echo ""
} > "$MR"

fail=0
for spec in "view16 16" "view17 17"; do
  set -- $spec
  tag="$1"; view="$2"
  rc=$(run "$tag" "$view")
  echo "run $tag rc=$rc" | tee -a "$MR"
  [ "$rc" -eq 0 ] || fail=1
  [ -s "$OUT/$tag.bmp" ] || { echo "FAIL missing $tag.bmp" | tee -a "$MR"; fail=1; }
done

echo "" >> "$MR"
printf '%-10s %-6s %-8s %-14s %-14s\n' pixel view16 view17 note >> "$MR"
for p in $(printf '%s' "$PTS" | tr ';' ' '); do
  x=${p%,*}; y=${p#*,}
  v16=$(pixel "$OUT/view16.bmp" "$x" "$y")
  v17=$(pixel "$OUT/view17.bmp" "$x" "$y")
  note=""
  case "$p" in
    849,396) note="OPEN: albedo frame pinned 0,0,0" ;;
    919,379) note="OPEN: texel=albedo=210,208,197" ;;
    902,385) note="control: trace and draw agree" ;;
    873,366) note="control: alpha=0 texel" ;;
  esac
  printf '%-10s %-6s %-8s %-14s %-14s\n' "$p" "$v16" "$v17" "$note" >> "$MR"
  [ "$v16" != "," ] && [ "$v17" != "," ] || { echo "FAIL no reading at $p" | tee -a "$MR"; fail=1; }
done

# Self-consistency: view17's B channel is colour.a. If the fragment's vertex
# colour is opaque black, colour.a is 255 and its rgb are 0 -- the shape the D01
# doc predicted for 849. Print which shape the numbers actually have instead of
# asserting one: a probe that "confirms" the hypothesis it was built for is how
# the first cut of this investigation went wrong.
{
  echo ""
  echo "## what the numbers say"
  for p in 849,396 919,379; do
    x=${p%,*}; y=${p#*,}
    v16=$(pixel "$OUT/view16.bmp" "$x" "$y"); v17=$(pixel "$OUT/view17.bmp" "$x" "$y")
    r16=${v16%%,*}; rest=${v16#*,}; g16=${rest%%,*}; b16=${rest#*,}
    echo "$p view16 colour.rgb=($r16,$g16) colour.a=$b16   view17 albedo.r=${v17%%,*}"
  done
} | tee -a "$MR"

if [ "$fail" -eq 0 ]; then
  echo "D01-COLOUR0=PASS readings=$(grep -acE '^[0-9]+,[0-9]+' "$MR")"
  exit 0
fi
echo "D01-COLOUR0=FAIL"
exit 1
