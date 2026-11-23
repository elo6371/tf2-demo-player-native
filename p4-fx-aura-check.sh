#!/usr/bin/env bash
# p4-fx-aura-check.sh -- P4 pixel evidence: the Uber aura and the Uber body tint
# change pixels on a frame whose camera looks straight at an ubered player.
#
# WHY THIS SHAPE (2026-11-23; the earlier 0-byte "evidence" was three separate
# mistakes, all recorded in _shared/p4-frames-2026-11-23/DIAGNOSIS-2026-11-23.md):
#   * the historical uber-on.ini / uber-off.ini had IDENTICAL content and no fx
#     key at all -- the fx switch is CLI only (--gameplay-fx / --gameplay-fx-parts)
#     so those pairs compared a configuration against itself;
#   * one "big diff" compared a 2,582,406-byte frame with a 5,946,246-byte frame,
#     i.e. two different window settings, not two different renders;
#   * the pl_upward POV demo can never show it: an entity snapshot resolves to a
#     retained checkpoint (median gap 3152 ticks there), the only ubered players
#     are entities 12/15 while the POV player is entity 9, and the demo camera
#     cannot be moved off him (--spectate-target is a no-op on a POV recording).
# The recipe that does work, and that this gate pins:
#   fixture  koth_bagel_rc11 STV (its map IS installed; rc13's is not, so rc13
#            frames are black and every diff on them is meaningless)
#   tick     5298 -> server 31966; the resolved snapshot is 31963, inside the
#            uber window 31537-32014 from .scratch/p4/census-rc11.txt
#   camera   --debug-camera, 200 units east of the aura sprite the product
#            reports itself ('fx: sprite x=10.59 y=-188.53 z=312.03 r=38.00'),
#            20 units above it, looking west and slightly down
#   pair     identical command lines except --gameplay-fx on --gameplay-fx-parts uber
#   so the only difference between the two frames is the fx pass. Frames drawn
#   from the same configuration are byte-identical elsewhere (the TEMP-SAME /
#   FRAME-SAME gate), which is what lets a non-zero byte count carry weight.
#
# ARTIFACTS: .scratch/p4/gate/{aura-off,aura-on}.{bmp,trace,log}
set -u
cd "$(dirname "$0")" || exit 2
export LC_ALL=C
export TF2_NATIVE_WINDOW_SIZE="${TF2_NATIVE_WINDOW_SIZE:-1280x720}"
OUT=.scratch/p4/gate
DEMO="D:/TF2_Demo_Player/testdata/demos/dfbce608c80a4d72a390c70c597f0377_match-20261101-1959-koth_bagel_rc11.dem"
TF="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
EXE=native/build-nmake/tf2_demo_native.exe
TICK=5298
checks=0
fails=0
ok() { checks=$((checks + 1)); echo "  PASS $1"; }
bad() { checks=$((checks + 1)); fails=$((fails + 1)); echo "  FAIL $1"; }
assert_eq() { if [ "$2" = "$3" ]; then ok "$1 ($3)"; else bad "$1 (want $3 got $2)"; fi; }
assert_ge() { if [ "$2" -ge "$3" ] 2>/dev/null; then ok "$1 ($2 >= $3)"; else bad "$1 (want >= $3 got $2)"; fi; }
assert_le() { if [ "$2" -le "$3" ] 2>/dev/null; then ok "$1 ($2 <= $3)"; else bad "$1 (want <= $3 got $2)"; fi; }
win() { cygpath -m "$1"; }
count() { grep -a -c "$2" "$1" 2>/dev/null || true; }
pvalue() { grep -a -o "$2=[0-9]*" "$1" 2>/dev/null | tail -1 | cut -d= -f2; }

rm -rf "$OUT"
mkdir -p "$OUT"
[ -f "$DEMO" ] || { echo "P4-FX-AURA=FAIL (missing fixture $DEMO)"; exit 1; }
[ -f "$TF/tf2_misc_dir.vpk" ] || { echo "P4-FX-AURA=FAIL (missing TF2 install $TF)"; exit 1; }

echo "=== 0/3 build the product ==="
if bash build-target.sh tf2_demo_native > "$OUT/build.txt" 2>&1; then
  ok "the product builds"
else
  bad "the product builds"
  tail -20 "$OUT/build.txt"
  echo "P4-FX-AURA=FAIL checks=$checks fails=$fails"
  exit 1
fi

capture() {
  name="$1"; shift
  TF2_NATIVE_TRACE="$(win "$OUT/$name.trace")" "$EXE" \
    --tf-root "$TF" --demo "$DEMO" \
    --hidden-window --audio-disabled --playback-speed 8 \
    --settings-file "$(win "$OUT/$name.ini")" --ui-settings-file "$(win "$OUT/$name-ui.ini")" \
    --capture-tick "$TICK" --capture-frame "$(win "$OUT/$name.bmp")" \
    --debug-camera 210.59 -188.53 332.03 8 180 "$@" > "$OUT/$name.log" 2>&1
  return $?
}

echo "=== 1/3 capture the pair (fx off, fx on) ==="
capture aura-off
assert_eq "the fx-off run exits 0" "$?" "0"
capture aura-on --gameplay-fx on --gameplay-fx-parts uber
assert_eq "the fx-on run exits 0" "$?" "0"

echo "=== 2/3 both runs draw the intended frame ==="
assert_eq "the fx-on run reached server tick 31966" "$(pvalue "$OUT/aura-on.trace" serverTick)" "31966"
assert_eq "the fx-off run reached server tick 31966" "$(pvalue "$OUT/aura-off.trace" serverTick)" "31966"
assert_ge "the fx-on run captured from a checkpoint snapshot" "$(count "$OUT/aura-on.trace" "scene: snapshot status=1")" "1"
assert_ge "the fx-off run captured from a checkpoint snapshot" "$(count "$OUT/aura-off.trace" "scene: snapshot status=1")" "1"
assert_le "the fx-on snapshot is at most 8 ticks stale" "$(pvalue "$OUT/aura-on.trace" stale)" "8"
assert_le "the fx-off snapshot is at most 8 ticks stale" "$(pvalue "$OUT/aura-off.trace" stale)" "8"
assert_ge "the debug camera is the draw camera" "$(count "$OUT/aura-on.trace" "src=debug")" "1"
assert_ge "the fx-on run computed an aura" "$(count "$OUT/aura-on.trace" "fx: list .*auras=1")" "1"
assert_ge "the fx-on run reports the sprite position" "$(count "$OUT/aura-on.trace" "fx: sprite i=0 x=10.59 y=-188.53 z=312.03 r=38.00")" "1"
assert_ge "the fx-on run reports an additive 16% sprite" "$(count "$OUT/aura-on.trace" "blend=0 a=0.160")" "1"
assert_eq "the fx-off run computed no fx at all" "$(count "$OUT/aura-off.trace" "fx: ")" "0"

echo "=== 3/3 the two frames differ ==="
assert_eq "both frames are the same size" "$(stat -c %s "$OUT/aura-off.bmp")" "$(stat -c %s "$OUT/aura-on.bmp")"
cmp -l "$OUT/aura-off.bmp" "$OUT/aura-on.bmp" > "$OUT/diffbytes.txt" 2>/dev/null
assert_ge "the delta channel bytes differ in many places" "$(wc -l < "$OUT/diffbytes.txt")" "1000"

if [ "$fails" -eq 0 ]; then
  echo "P4-FX-AURA=PASS checks=$checks"
  exit 0
fi
echo "P4-FX-AURA=FAIL checks=$checks fails=$fails"
exit 1
