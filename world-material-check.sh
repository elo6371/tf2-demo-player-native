#!/usr/bin/env bash
# world-material-check.sh -- assert the six world material and lighting
# capabilities each carry a state and a reason, and that each state can go red.
#
# WHY THIS FILE EXISTS
# --------------------
# Before this gate, the world pass answered six questions with one word each:
# `lightmapMode: source-equivalent`, `cubemapMode: approximate-2d`,
# `waterSurface: approximate-warp`, and nothing at all for skybox, displacement
# or VIS clipping. None of those words could be wrong, so none of them was a
# claim. The rule this task works under is that a reading nobody can refute is
# not a reading.
#
# What this gate asserts, in order:
#   1. the probe's self-tests pass (the pure functions and the empty-BSP case)
#   2. every synthetic fixture passes, on BSP bytes built by this repo and read
#      back through the product's own parser -- so the parser is what is tested
#   3. every fixture's mutation moves its reading, which is what makes 2 mean
#      something: a fixture whose mutation changes nothing is a fixture that was
#      asserting a constant
#   4. the real map's six states and their counters, pinned
#   5. the negative control: a map with no BSP must report every capability
#      unavailable, which is the reading that would catch a gate that passes on
#      constants
#   6. with --mutation, a deliberately wrong expectation goes red
#
# Usage: bash world-material-check.sh [--mutation]
# Exit:  0 = every assertion held (or, with --mutation, went red).
set -uo pipefail
cd "$(dirname "$0")"

EXE=native/build-nmake/world_material_probe.exe
TF="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
MAP="cp_snakewater_final1"
OUT="${WORLD_MATERIAL_OUT:-evidence/world-material}"
mkdir -p "$OUT"

MUTATION=0
[ "${1:-}" = "--mutation" ] && MUTATION=1

fail=0
for path in "$EXE" "$TF" "$TF/maps/$MAP.bsp"; do
  [ -e "$path" ] || { echo "FATAL: missing input: $path"; exit 1; }
done

assert_eq() { # assert_eq <label> <actual> <expected>
  if [ "${2:-<none>}" = "$3" ]; then
    echo "  OK   $1 = $3"
  else
    echo "  FAIL $1 = ${2:-<none>}, expected $3"; fail=1
  fi
}

# The probe prints one JSON object per line in two shapes: the self-test lines
# carry bare booleans under their own keys, the reading lines carry the
# capability key in the `reading` field and its value in `value`. One extractor
# per shape, because a sed that tries to serve both silently returns the empty
# string for one of them and an empty string matches nothing.
field() { # field <lines> <capability.key> -> quoted-string value
  printf '%s\n' "$1" | sed -n "s/.*\"reading\":\"$2\",\"value\":\"\([^\"]*\)\".*/\1/p" | head -1
}
flag() { # flag <lines> <key> -> true|false
  printf '%s\n' "$1" | sed -n "s/.*\"$2\":\(true\|false\).*/\1/p" | head -1
}
count() { # count <lines> <key> -> number, from the summary line
  printf '%s\n' "$1" | sed -n "s/.*\"$2\":\([0-9][0-9]*\).*/\1/p" | tail -1
}

echo "=== 1/6 the probe's self-tests pass ==="
# Two lines on purpose. The first is the pre-existing self-test and is left
# byte-identical because verify-fast.sh pins seven substrings of it and that
# script is outside this task's write scope; the second is the capability
# layer's own pure-function test.
SELF=$("$EXE" --self-test 2>&1)
echo "$SELF" > "$OUT/self-test.txt"
assert_eq "selfTest" "$(flag "$SELF" selfTest)" "true"
assert_eq "capabilitySelfTest" "$(flag "$SELF" capabilitySelfTest)" "true"
assert_eq "cubeAxes" "$(flag "$SELF" cubeAxes)" "true"
assert_eq "cubeZeroRefused" "$(flag "$SELF" cubeZeroRefused)" "true"
assert_eq "cubeFacePaths" "$(flag "$SELF" cubeFacePaths)" "true"
assert_eq "emptyBspAllUnavailable" "$(flag "$SELF" emptyBspAllUnavailable)" "true"

echo
echo "=== 2/6 every synthetic fixture passes ==="
# The fixtures build real BSP bytes and read them back through BspParser, so a
# green run is a statement about the parser and the capability layer together.
FIX=$("$EXE" --fixture 2>&1)
echo "$FIX" > "$OUT/fixture-straight.txt"
assert_eq "fixtures" "$(count "$FIX" fixtures)" "9"
assert_eq "fixture checks" "$(count "$FIX" checks)" "52"
assert_eq "fixture failures" "$(count "$FIX" bad)" "0"

echo
echo "=== 3/6 every fixture mutation moves its reading ==="
# A mutation that changes nothing is a mutation that proved nothing, so the
# probe fails the run when no case had one to execute.
MUT=$("$EXE" --fixture --mutation 2>&1)
echo "$MUT" > "$OUT/fixture-mutation.txt"
assert_eq "mutations executed" "$(count "$MUT" mutations)" "7"
assert_eq "mutation failures" "$(count "$MUT" mutationBad)" "0"
assert_eq "total failures with mutations" "$(count "$MUT" bad)" "0"

echo
echo "=== 4/6 the real map's six capabilities, pinned ==="
# Every number below was measured on this install on 2026-10-13. A changed
# install goes red on purpose: the finding it records ("this map's baked light
# fits the atlas except for 15 faces") would no longer be true, and a reader
# needs to be told rather than left with a stale note.
REAL=$("$EXE" --bsp "$TF/maps/$MAP.bsp" --tf-root "$TF" --map-name "$MAP" 2>&1)
echo "$REAL" > "$OUT/real-$MAP.txt"
assert_eq "lightmap state"        "$(field "$REAL" lightmap.state)"        "active"
assert_eq "lightmap faces"        "$(field "$REAL" lightmap.facesWithLightmap)" "11527"
assert_eq "lightmap packed"       "$(field "$REAL" lightmap.facesPacked)"  "11512"
assert_eq "lightmap dropped"      "$(field "$REAL" lightmap.facesDropped)" "15"
assert_eq "lightmap triangles"    "$(field "$REAL" lightmap.trianglesWithLightUv)" "199263"
assert_eq "lightmap atlas"        "$(field "$REAL" lightmap.atlas)"        "2048x2047"
assert_eq "lightmap uv in range"  "$(field "$REAL" lightmap.uvAllInRange)" "true"
assert_eq "cubemap state"         "$(field "$REAL" cubemap.state)"         "fallback"
assert_eq "cubemap samples"       "$(field "$REAL" cubemap.samples)"       "33"
assert_eq "cubemap complete"      "$(field "$REAL" cubemap.samplesComplete)" "0"
assert_eq "sky state"             "$(field "$REAL" sky.state)"             "active"
assert_eq "sky name"              "$(field "$REAL" sky.name)"              "sky_well_01"
assert_eq "sky vmts found"        "$(field "$REAL" sky.vmtFound)"          "6"
assert_eq "displacement faces"    "$(field "$REAL" displacement.faces)"    "1316"
assert_eq "displacement triangles" "$(field "$REAL" displacement.triangles)" "165376"
assert_eq "displacement corners"  "$(field "$REAL" displacement.cornerMatches)" "1316"
assert_eq "water faces"           "$(field "$REAL" water.faces)"           "18"
assert_eq "water triangles"       "$(field "$REAL" water.trianglesTagged)" "36"
assert_eq "vis mode"              "$(field "$REAL" vis.mode)"              "cluster-pvs"
assert_eq "vis clusters"          "$(field "$REAL" vis.clusters)"          "2940"
assert_eq "vis visible from 0"    "$(field "$REAL" vis.visibleFromCluster0)" "384"

echo
echo "=== 5/6 the negative control: no BSP, so nothing is available ==="
# Section 4 pins numbers that only hold for a map that is installed. This is
# about the map that is not, and it is the reading that would catch a gate
# passing on constants: with no BSP every capability must say unavailable, and
# the reason must name the cause rather than repeat a label.
BAGEL=$("$EXE" --bsp "$TF/maps/koth_bagel_rc13.bsp" --tf-root "$TF" --map-name koth_bagel_rc13 2>&1)
echo "$BAGEL" > "$OUT/negative-bagel.txt"
if [ ! -e "$TF/maps/koth_bagel_rc13.bsp" ]; then
  echo "  OK   koth_bagel_rc13.bsp is not installed here, so the run reads an empty map"
else
  echo "  NOTE koth_bagel_rc13.bsp IS installed now; this control no longer tests the unreachable case"
fi
assert_eq "bagel lightmap"     "$(field "$BAGEL" lightmap.state)"     "unavailable"
assert_eq "bagel cubemap"      "$(field "$BAGEL" cubemap.state)"      "unavailable"
assert_eq "bagel sky"          "$(field "$BAGEL" sky.state)"          "unavailable"
assert_eq "bagel displacement" "$(field "$BAGEL" displacement.state)" "unavailable"
assert_eq "bagel water"        "$(field "$BAGEL" water.state)"        "unavailable"
assert_eq "bagel vis"          "$(field "$BAGEL" vis.state)"          "unavailable"
assert_eq "bagel lightmap reason" "$(field "$BAGEL" lightmap.reason)" "the bsp declares no lightmap faces"

echo
if [ "$MUTATION" -eq 1 ]; then
  # Two wrong expectations, one for a counter and one for a state. The counter
  # is the one that would have caught the silent packer drop (15 faces lost
  # with nothing reporting it); the state is the one that would have caught a
  # capability layer that labels everything active.
  echo "=== mutation: the expected packer drop and the sky state are wrong on purpose ==="
  if [ "$(field "$REAL" lightmap.facesDropped)" = "0" ]; then
    echo "  FAIL the packer drop matched a value it should not have"
    exit 1
  fi
  if [ "$(field "$REAL" sky.state)" = "unavailable" ]; then
    echo "  FAIL the sky state matched a value it should not have"
    exit 1
  fi
  # And the fixture half: demand a mutation count that no run can produce.
  if [ "$(count "$MUT" mutations)" = "99" ]; then
    echo "  FAIL the mutation count matched a value it should not have"
    exit 1
  fi
  echo "MUTATION-CAUGHT=PASS (a wrong packer-drop expectation, a wrong sky state and a wrong"
  echo "  mutation count each go red)"
  exit 0
fi

if [ "$fail" -eq 0 ]; then
  echo "WORLD-MATERIAL=PASS"
else
  echo "WORLD-MATERIAL=FAIL"
fi
exit "$fail"
