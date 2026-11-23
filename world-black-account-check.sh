#!/usr/bin/env bash
# world-black-account-check.sh -- F1-5: account for the named black pixels on
# cp_sultry_b8a with the product's own face trace, including tile alpha.
#
# WHY THIS EXISTS
#   I-20 (1,143,132) and I-21 (873,366 / 849,396 / 1110,136) were described as
#   "albedo is black" because the --world-albedo diagnostic writes RGB and does
#   not carry alpha. A translucent material with a transparent texel is not a
#   black material: its contribution is zero, and the pixel shows what is behind
#   it. The face trace now samples the tile buffer's alpha (`texelA=`) at named
#   screen points and `--world-face-trace-points` selects those points.
#
# WHAT THE CURRENT READINGS SAY (measured 2026-11-13, exe d5b9af67, camera
# --debug-camera -600 -600 950 20 45, tick 0):
#   * 1143,132 and 1110,136 are SULTRYPROPS/TREELINECARD, texel 255,255,255,
#     texelA=0. The sheet is fully transparent; I-20 is "transparent texel over
#     a dark background", not "a white texture drawn black".
#   * 873,366 is METAL/METALGRATE013A2, texel 0,0,0, texelA=0. I-21's black
#     albedo at this point is also a transparent texel, not an opaque black one.
#   * 849,396 is the same material but texelA=255 and the trace predicts
#     84,83,81 while the albedo frame reads 0,0,0. The `--world-texel` frame
#     reads the same 84,83,81, so the difference is the vertex-colour
#     modulation, not the texture or alpha. It is still an open vertex-colour
#     finding, and this gate keeps it visible instead of folding it into alpha.
#   * 919,379 predicts 82,81,79 but the texel and albedo frames both read
#     210,208,197; that point's face-trace UV disagrees with the drawn pass and
#     is pinned as an OPEN trace/draw discrepancy.
#
# This is a diagnostic pin, not a rendering-parity claim. It runs two product
# captures at one fixed camera and reads only product trace lines and the two
# frames' exact pixels.
set -uo pipefail
# W1 (2026-11-23): every product launch in this script runs at 1280x720, the size
# its pixel pins were measured at. The product reads this when --window-size is
# absent (an explicit --window-size still wins); before W1 1280x720 was simply the
# default, and W1 moves the default to 1920x1080. tools/window-pin-check.sh checks it.
export TF2_NATIVE_WINDOW_SIZE="${TF2_NATIVE_WINDOW_SIZE:-1280x720}"
cd "$(dirname "$0")"

EXE=native/build-nmake/tf2_demo_native.exe
TF="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
DEMO="E:/demo测试/cp_sultry_b8a/STV/f63a7a2040379d7d6a8414f76bbb81dc_matcha-20260703-1442-cp_sultry_b8a.dem"
OUT="${BLACK_ACCOUNT_OUT:-.scratch/world-black-account}"
PTS="1143,132;1110,136;873,366;849,396;902,385;919,379"
mkdir -p "$OUT"

for path in "$EXE" "$DEMO" "$TF"; do
  [ -e "$path" ] || { echo "WORLD-BLACK-ACCOUNT=FAIL reason=missing-input path=$path"; exit 1; }
done
command -v python >/dev/null 2>&1 || { echo "WORLD-BLACK-ACCOUNT=FAIL reason=missing-python"; exit 2; }

. "$(dirname "$0")/verify-iso.sh"
ISO_DIR=$(mktemp -d "${TMPDIR:-/tmp}/tf2-black.XXXXXX") || exit 2
ISO_SETTINGS="$ISO_DIR/settings.ini"
ISO_UI="$ISO_DIR/ui.ini"
iso_seed_gate_settings "$ISO_DIR" || { echo "WORLD-BLACK-ACCOUNT=FAIL reason=iso-seed"; exit 2; }
trap 'rm -rf "$ISO_DIR"' EXIT

abs_win_path() {
  case "$1" in
    /*|[A-Za-z]:*) cygpath -m "$1" ;;
    *)            cygpath -m "$PWD/$1" ;;
  esac
}
fail=0
assert_eq() {
  if [ "${2:-<none>}" = "$3" ]; then echo "  OK   $1 = $3";
  else echo "  FAIL $1 = ${2:-<none>}, expected $3"; fail=1; fi
}
field() { printf '%s' "$1" | grep -oE "(^| )$2=[^ ]+" | head -1 | cut -d= -f2-; }
line_for() { grep -a "world face-trace point=$1 " "$2" | head -1; }

run() { # run <tag> [extra...]
  local tag="$1"; shift
  rm -f "$OUT/$tag.bmp" "$OUT/$tag.trace" "$OUT/$tag.log"
  TF2_NATIVE_TRACE="$(abs_win_path "$OUT/$tag.trace")" timeout 420 "$EXE" \
    --tf-root "$TF" --demo "$DEMO" --audio-disabled --hidden-window \
    --start-paused --capture-tick 0 --world-sky \
    --settings-file "$(abs_win_path "$ISO_SETTINGS")" \
    --ui-settings-file "$(abs_win_path "$ISO_UI")" \
    --debug-camera -600 -600 950 20 45 \
    --world-face-trace --world-face-trace-points "$PTS" \
    --capture-frame "$(abs_win_path "$OUT/$tag.bmp")" "$@" > "$OUT/$tag.log" 2>&1
  echo $?
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

echo "=== the shared face trace, at the six named pixels ==="
RC_FACE=$(run face)
RC_ALBEDO=$(run albedo --world-albedo)
RC_TEXEL=$(run texel --world-texel)
assert_eq "face run rc" "$RC_FACE" "0"
assert_eq "albedo run rc" "$RC_ALBEDO" "0"
assert_eq "texel run rc" "$RC_TEXEL" "0"
for p in $(printf '%s' "$PTS" | tr ';' ' '); do
  [ -n "$(line_for "$p" "$OUT/face.trace")" ] || { echo "  FAIL missing face-trace line $p"; fail=1; }
done
assert_eq "face-trace lines" "$(grep -ac 'world face-trace point=' "$OUT/face.trace")" "6"

# I-20: both TREELINECARD points are transparent texels, and the frame is black.
for p in 1143,132 1110,136; do
  line=$(line_for "$p" "$OUT/face.trace")
  assert_eq "I-20 $p material" "$(field "$line" material)" "SULTRYPROPS/TREELINECARD"
  assert_eq "I-20 $p texel RGB" "$(field "$line" texel)" "255,255,255"
  assert_eq "I-20 $p texel alpha" "$(field "$line" texelA)" "0"
  assert_eq "I-20 $p frame pixel" "$(pixel "$OUT/face.bmp" "${p%,*}" "${p#*,}")" "0,0,0"
done
# The albedo diagnostic is RGB-only: at 1143 it is white, at 1110 it is black.
# Both are consistent with alpha=0; neither is a claim about the final colour.
assert_eq "I-20 1143 albedo frame" "$(pixel "$OUT/albedo.bmp" 1143 132)" "255,255,255"
assert_eq "I-20 1110 albedo frame" "$(pixel "$OUT/albedo.bmp" 1110 136)" "0,0,0"
# The texel view is the raw t0 sample. Where it differs from albedo, the
# difference is the vertex colour modulation, not the texture.
assert_eq "I-20 1110 texel frame" "$(pixel "$OUT/texel.bmp" 1110 136)" "255,255,255"

# I-21 at 873,366: transparent black texel, not an opaque black one.
line=$(line_for 873,366 "$OUT/face.trace")
assert_eq "I-21 873 material" "$(field "$line" material)" "METAL/METALGRATE013A2"
assert_eq "I-21 873 texel alpha" "$(field "$line" texelA)" "0"
assert_eq "I-21 873 frame pixel" "$(pixel "$OUT/face.bmp" 873 366)" "3,0,0"

# The remaining discrepancies: the trace's tile-buffer prediction and the
# albedo frame disagree at 849 and 919. They are pinned as an OPEN finding, not
# explained away by alpha.
line=$(line_for 849,396 "$OUT/face.trace")
assert_eq "open 849 texel alpha" "$(field "$line" texelA)" "255"
assert_eq "open 849 predicted albedo" "$(field "$line" albedo)" "84,83,81"
assert_eq "open 849 texel frame" "$(pixel "$OUT/texel.bmp" 849 396)" "84,83,81"
assert_eq "open 849 albedo frame" "$(pixel "$OUT/albedo.bmp" 849 396)" "0,0,0"
line=$(line_for 919,379 "$OUT/face.trace")
assert_eq "open 919 texel alpha" "$(field "$line" texelA)" "255"
assert_eq "open 919 predicted albedo" "$(field "$line" albedo)" "82,81,79"
assert_eq "open 919 texel frame" "$(pixel "$OUT/texel.bmp" 919 379)" "210,208,197"
assert_eq "open 919 albedo frame" "$(pixel "$OUT/albedo.bmp" 919 379)" "210,208,197"

echo
if [ "$fail" -eq 0 ]; then
  echo "WORLD-BLACK-ACCOUNT=PASS (alpha resolves I-20 and 873,366; 849/919 pinned open)"
  exit 0
fi
echo "WORLD-BLACK-ACCOUNT=FAIL"
exit 1
