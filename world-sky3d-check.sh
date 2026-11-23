#!/usr/bin/env bash
# world-sky3d-check.sh -- T105 acceptance (2026-11-23): the 3D skybox draws.
#
# WHY THIS EXISTS
#   docs/UNFINISHED-AUDIT-2026-11-22.md row 6 ("3D 天空盒") had one open question
#   that no reading answered: does --world-sky-3d put any pixel on screen? The
#   first real run on cp_snakewater printed "enabled=1 present=1 cluster=2643
#   vertices=0 triangles=0", and the tick-1200 pl_upward spawn frame is an indoor
#   close-up, so both looked like "the pass is dead". Neither was: snakewater's
#   sky_camera sits 3533 map units from the nearest triangle (that map has no 3D
#   skybox geometry at all), and the spawn frame simply has no sky in it.
#
#   On pl_upward_f12 at the same tick the upload is 5103 vertices / 1701
#   triangles / 2 batches, and with a camera that can see the sky the frame
#   changes by 16,269 bytes. Those are the two facts this gate pins:
#
#     1. the default frame is untouched (the pass stays off unless asked);
#     2. the on frame uploads geometry AND differs from it.
#
#   The camera is --debug-camera 0 0 1500 -10 0: 1500 units above the map origin
#   looking 10 degrees UP along +x, toward the sky camera the map authors at
#   7168 -2048 0. Any pose that sees sky would do; this one is above the geometry
#   so nothing opaque can fill the frame.
#
# Usage: bash world-sky3d-check.sh
# Exit:  0 = every assertion held; 1 = not.
set -uo pipefail
export TF2_NATIVE_WINDOW_SIZE="${TF2_NATIVE_WINDOW_SIZE:-1280x720}"
cd "$(dirname "$0")"
export LC_ALL=C

EXE=native/build-nmake/tf2_demo_native.exe
TF="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
# pl_upward_f12: the map this repo has always used for the 3D skybox reading.
DEMO="D:/TF2_Demo_Player/testdata/demos/a9630c6557468b24f0b8c2e7dab38ed8_match-20261030-2145-pl_upward_f12.dem"
OUT=.scratch/world-sky3d
RUN_TIMEOUT=300
CAMERA=(--debug-camera 0 0 1500 -10 0)

fail=0
mkdir -p "$OUT"
win() { cygpath -m "$1"; }   # never prefix $PWD: an absolute /d/... path stays usable

assert_eq() { # <label> <actual> <expected>
  if [ "${2:-<none>}" = "$3" ]; then
    echo "  OK   $1 = $3"
  else
    echo "  FAIL $1 = ${2:-<none>}, expected $3"; fail=1
  fi
}

assert_num_gt() { # <label> <actual> <bound>
  if [ -n "${2:-}" ] && [ "${2:-0}" -gt "$3" ] 2>/dev/null; then
    echo "  OK   $1 = $2 (> $3)"
  else
    echo "  FAIL $1 = ${2:-<none>}, expected > $3"; fail=1
  fi
}

# Field extraction is LINE-scoped on purpose: "vertices=" also appears on the
# world-geometry line, so a whole-file search reads the wrong number (measured:
# it returned 0 for a sky3d line that says vertices=5103).
sfield() { # <line> <field> -- the LAST value of the field on that one line
  printf '%s\n' "$1" | grep -oE "$2=[^[:space:]]*" | tail -1 | cut -d= -f2
}

echo "=== 0/3 build (product) ==="
if bash build-target.sh tf2_demo_native > "$OUT/build.txt" 2>&1; then
  echo "  OK build"
else
  echo "  FAIL build (see $OUT/build.txt)"; tail -30 "$OUT/build.txt"; exit 1
fi
for path in "$EXE" "$TF" "$DEMO"; do
  [ -e "$path" ] || { echo "FATAL: missing input: $path"; exit 1; }
done

run_case() { # <name> [extra args...]
  local name="$1"; shift
  TF2_NATIVE_TRACE="$(win "$OUT/$name.trace")" timeout "$RUN_TIMEOUT" "$EXE" \
    --tf-root "$TF" --demo "$DEMO" --hidden-window --audio-disabled --playback-speed 8 \
    --settings-file "$(win "$OUT/$name.ini")" --ui-settings-file "$(win "$OUT/$name-ui.ini")" \
    --capture-tick 1200 --capture-frame "$(win "$OUT/$name.bmp")" \
    "${CAMERA[@]}" "$@" > "$OUT/$name.log" 2>&1
  echo "$?"
}

echo "=== 1/3 the default frame (no --world-sky-3d) ==="
rc_default=$(run_case default)
assert_eq "default rc" "$rc_default" "0"
assert_eq "default sky3d enabled" "$(grep -c "sky3d: enabled=0" "$OUT/default.trace")" "1"
assert_num_gt "default camera src lines" "$(grep -c "scene: camera mode=free-look .*src=debug" "$OUT/default.trace")" "0"

echo "=== 2/3 the pass when asked (--world-sky-3d on) ==="
rc_on=$(run_case on --world-sky-3d on)
assert_eq "on rc" "$rc_on" "0"
on_line=$(grep -a "sky3d: enabled=" "$OUT/on.trace" | tail -1)
census_line=$(grep -a "sky3d: census " "$OUT/on.trace" | tail -1)
echo "  ..   $on_line"
echo "  ..   $census_line"
assert_eq "on sky3d enabled" "$(grep -c "sky3d: enabled=1 present=1" "$OUT/on.trace")" "1"
assert_num_gt "on vertices" "$(sfield "$on_line" vertices)" "0"
assert_num_gt "on triangles" "$(sfield "$on_line" triangles)" "0"
assert_num_gt "on batches" "$(sfield "$on_line" batches)" "0"
# The census is the measurement that separates "wrong selection" from "nothing
# near the camera": on this map every uploaded triangle is in the sky cluster.
assert_eq "census sameCluster == uploaded triangles" \
  "$(sfield "$census_line" sameCluster)" "$(sfield "$on_line" triangles)"

echo "=== 3/3 the frames ==="
size_default=$(stat -c %s "$OUT/default.bmp" 2>/dev/null || echo 0)
size_on=$(stat -c %s "$OUT/on.bmp" 2>/dev/null || echo 0)
assert_eq "frame size equal" "$size_on" "$size_default"
assert_num_gt "frame size" "$size_default" "1000000"
if cmp -s "$OUT/default.bmp" "$OUT/on.bmp"; then
  echo "  FAIL the 3D skybox changed no pixel"; fail=1
else
  differing=$(cmp -l "$OUT/default.bmp" "$OUT/on.bmp" | wc -l | tr -d ' ')
  assert_num_gt "differing bytes" "$differing" "1000"
fi

echo
if [ "$fail" = 0 ]; then
  echo "WORLD-SKY3D=PASS"
else
  echo "WORLD-SKY3D=FAIL"
fi
exit "$fail"
