#!/usr/bin/env bash
# world-sky3d-fog-check.sh -- I5 (2026-11-23): the 3D skybox can take the world's fog.
#
# WHY THIS EXISTS
#   docs/UNFINISHED-AUDIT-2026-11-22.md row 6 and §2.9 (c): the T105 3D skybox pass
#   uploads fogParams = (0,0,0,0), i.e. the skybox is drawn unfogged while the world
#   around it is fogged. On a fogged map the skybox is the one surface that keeps its
#   raw albedo, so the horizon stops matching the geometry in front of it.
#
#   The fix is one switch, --world-sky-3d-fog, because the numbers are not the sky's
#   to invent: a map has one env_fog_controller and the world pass already uploads its
#   start/end/maxdensity/colour. The switch only lets the sky's pass-level fog arm
#   open, using the same values and the same box-to-map conversion.
#
#   What this gate pins:
#     1. the world-fogged run with the switch OFF uploads the zero sentinels (so the
#        flag, not the fog, is what changes the sky);
#     2. the switch ON uploads real fog values and changes the pixels;
#     3. the same run twice is byte-identical (it is the map and the tick, not noise);
#     4. asking for sky fog while the world fog is refused does NOT fog the sky --
#        the guard is the world pass's own state, not the switch.
#
# Camera: --debug-camera 0 0 1500 -10 0 (as world-sky3d-check.sh) -- 1500 units above
# the map origin, 10 degrees up, looking toward pl_upward's sky_camera at 7168 -2048 0.
#
# Usage: bash world-sky3d-fog-check.sh
# Exit:  0 = every assertion held; 1 = not.
set -uo pipefail
export TF2_NATIVE_WINDOW_SIZE="${TF2_NATIVE_WINDOW_SIZE:-1280x720}"
cd "$(dirname "$0")"
export LC_ALL=C

EXE=native/build-nmake/tf2_demo_native.exe
TF="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
DEMO="D:/TF2_Demo_Player/testdata/demos/a9630c6557468b24f0b8c2e7dab38ed8_match-20261030-2145-pl_upward_f12.dem"
OUT=.scratch/world-sky3d-fog
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

# LINE-scoped field read: the same key appears on several trace lines (vertices= on
# the world-geometry line, enabled= on half a dozen others).
sfield() { # <line> <field>
  printf '%s\n' "$1" | grep -oE "$2=[^[:space:]]*" | tail -1 | cut -d= -f2
}

echo "=== 0/4 build (product) ==="
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

echo "=== 1/4 world fog on, sky fog off (the control) ==="
rc_off=$(run_case fogoff --world-fog on --world-sky-3d on)
assert_eq "sky-fog-off rc" "$rc_off" "0"
assert_eq "world fog is on" "$(grep -c "gpu: world fog enabled=1" "$OUT/fogoff.trace")" "1"
off_line=$(grep -a "sky3d: fog " "$OUT/fogoff.trace" | tail -1)
echo "  ..   $off_line"
assert_eq "sky fog requested" "$(sfield "$off_line" requested)" "0"
assert_eq "sky fog enabled" "$(sfield "$off_line" enabled)" "0"
assert_eq "sky3d pass on" "$(grep -c "sky3d: enabled=1 present=1" "$OUT/fogoff.trace")" "1"

echo "=== 2/4 world fog on, sky fog on (--world-sky-3d-fog) ==="
rc_on=$(run_case fogged --world-fog on --world-sky-3d on --world-sky-3d-fog)
assert_eq "sky-fog-on rc" "$rc_on" "0"
on_line=$(grep -a "sky3d: fog " "$OUT/fogged.trace" | tail -1)
echo "  ..   $on_line"
assert_eq "sky fog requested" "$(sfield "$on_line" requested)" "1"
assert_eq "sky fog enabled" "$(sfield "$on_line" enabled)" "1"
assert_eq "sky3d pass on" "$(grep -c "sky3d: enabled=1 present=1" "$OUT/fogged.trace")" "1"
assert_eq "world fog is on" "$(grep -c "gpu: world fog enabled=1" "$OUT/fogged.trace")" "1"

echo "=== 3/4 the frames ==="
size_off=$(stat -c %s "$OUT/fogoff.bmp" 2>/dev/null || echo 0)
size_on=$(stat -c %s "$OUT/fogged.bmp" 2>/dev/null || echo 0)
assert_eq "frame size equal" "$size_on" "$size_off"
assert_num_gt "frame size" "$size_off" "1000000"
if cmp -s "$OUT/fogoff.bmp" "$OUT/fogged.bmp"; then
  echo "  FAIL the sky's fog changed no pixel"; fail=1
else
  differing=$(cmp -l "$OUT/fogoff.bmp" "$OUT/fogged.bmp" | wc -l | tr -d ' ')
  assert_num_gt "differing bytes" "$differing" "1000"
fi
rc_again=$(run_case fogged2 --world-fog on --world-sky-3d on --world-sky-3d-fog)
assert_eq "repeat rc" "$rc_again" "0"
if cmp -s "$OUT/fogged.bmp" "$OUT/fogged2.bmp"; then
  echo "  OK   the same arguments produced the same frame"
else
  echo "  FAIL the same arguments produced a different frame"; fail=1
fi

echo "=== 4/4 the guard: sky fog asked for, world fog refused ==="
rc_guard=$(run_case guard --world-sky-3d on --world-sky-3d-fog)
assert_eq "guard rc" "$rc_guard" "0"
guard_line=$(grep -a "sky3d: fog " "$OUT/guard.trace" | tail -1)
echo "  ..   $guard_line"
assert_eq "guard sky fog requested" "$(sfield "$guard_line" requested)" "1"
assert_eq "guard sky fog enabled" "$(sfield "$guard_line" enabled)" "0"
if grep -aq "the world fog is off, so the sky has no fog to copy" "$OUT/guard.trace"; then
  echo "  OK   the guard names the reason"
else
  echo "  FAIL the guard did not name the reason"; fail=1
fi

echo
if [ "$fail" = 0 ]; then
  echo "WORLD-SKY3D-FOG=PASS"
else
  echo "WORLD-SKY3D-FOG=FAIL"
fi
exit "$fail"
