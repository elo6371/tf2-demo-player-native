#!/usr/bin/env bash
# Pinned-pose capture for the displacement-parametrisation fix (task-5 / T105).
#
# Why a script and not three hand-typed commands: the before/after comparison is
# only honest if both arms are captured with byte-identical arguments, in the
# same directory layout, on the same poses. The poses are the ones the world
# hole/sky work already pinned (W-1), so a reading here can be compared with the
# numbers in docs/WORLD-HOLES-2026-11-17.md without re-deriving a camera.
#
# For every pose it writes three views because the fix touches world geometry and
# the sky pass paints the holes the geometry leaves behind:
#   <pose>.bmp        the frame a player sees (sky on, compiled default settings)
#   <pose>-depth.bmp  scalar depth view (--world-depth); no geometry => RGB sum <= 12
#   <pose>-nosky.bmp  the same frame with the sky pass off (--no-world-sky)
# With those three, "enclosed clear pixel" and the newer "sky through a crack"
# reading are both computable without guessing what colour means what.
#
# usage: world-disp-param-capture.sh <product-exe> <outdir> [--parallel N]
set -u

EXE="${1:-}"
OUT="${2:-}"
if [ -z "$EXE" ] || [ -z "$OUT" ]; then
  echo "usage: $0 <product-exe> <outdir>" >&2
  exit 2
fi
if [ ! -f "$EXE" ]; then
  echo "no such product: $EXE" >&2
  exit 2
fi
mkdir -p "$OUT" || exit 2

TF="/d/SteamLibrary/steamapps/common/Team Fortress 2/tf"
UP="/e/demo测试/pl_upward_f12/STV/a9630c6557468b24f0b8c2e7dab38ed8_match-20261030-2145-pl_upward_f12.dem"
PR="/e/demo测试/cp_process_f12/STV/1338f5de79ea343682c17dd514c980d9_matcha-20260927-1312-cp_process_f12.dem"
KOTH="/e/demo测试/koth_product_final/STV/prodcutscout.dem"

# Empty settings files = compiled defaults, i.e. a fresh install (preset 0, sky
# on, no bookmarks). A populated settings file would pin a quality preset and
# turn a geometry reading into a preset reading.
ISO="$OUT/iso"
mkdir -p "$ISO" || exit 2
: > "$ISO/settings.ini"
: > "$ISO/ui.ini"

win() { if command -v cygpath >/dev/null 2>&1; then cygpath -m "$1"; else printf '%s' "$1"; fi; }

capture() { # <name> <demo> <tick> [extra product args...]
  local name="$1" demo="$2" tick="$3"; shift 3
  TF2_NATIVE_TRACE="$(win "$PWD/$OUT/$name.trace")" \
  timeout 420 "$EXE" \
    --tf-root "$TF" --demo "$demo" --capture-tick "$tick" \
    --capture-frame "$(win "$PWD/$OUT/$name.bmp")" \
    --settings-file "$(win "$PWD/$ISO/settings.ini")" \
    --ui-settings-file "$(win "$PWD/$ISO/ui.ini")" \
    --audio-disabled --hidden-window --world-water off "$@" \
    > "$OUT/$name.log" 2>&1
  local rc=$?
  local winding
  winding="$(grep -a 'map: geometry winding keepMapWinding' "$OUT/$name.trace" 2>/dev/null | tail -1 | sed 's/^ *[0-9.]* //')"
  echo "$name rc=$rc bmp=$(stat -c %s "$OUT/$name.bmp" 2>/dev/null) $winding"
  return 0
}

echo "== capture arm: $EXE"
echo "== output: $OUT"
for pose in up0 up1800 pr600 koth0; do
  case "$pose" in
    up0)    demo="$UP";   tick=0 ;;
    up1800) demo="$UP";   tick=1800 ;;
    pr600)  demo="$PR";   tick=600 ;;
    koth0)  demo="$KOTH"; tick=0 ;;
  esac
  # A metrics csv on the base view only: the counters it carries are tick
  # independent, and eleven identical csv files would just be clutter.
  capture "$pose" "$demo" "$tick" --metrics-file "$(win "$PWD/$OUT/$pose.csv")"
  capture "$pose-depth" "$demo" "$tick" --world-depth
  case "$pose" in
    up0|up1800|pr600) capture "$pose-nosky" "$demo" "$tick" --no-world-sky ;;
  esac
done
echo "== capture arm done"
