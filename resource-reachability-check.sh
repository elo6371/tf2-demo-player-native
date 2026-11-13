#!/usr/bin/env bash
# resource-reachability-check.sh -- assert that the resources a demo scene needs
# are actually reachable, and that the frame-capture gate is not passing by
# accident on a picture that has nothing to do with the demo.
#
# Why this exists
# ---------------
# The frame-capture step (verify-all.sh step 16) asserts four things: a frame
# comes back, its BMP header is self-consistent, the same static scene twice is
# byte-identical, and a demo scene differs from the paused one. It never asserts
# what is *in* the demo frame. On this machine that gap was already hiding a real
# defect: `koth_bagel_rc13`'s BSP is not installed, so the renderer fell through
# to its six-vertex full-screen quad textured with the fallback `texture_` (the
# vgui spray decal), and step 16 accepted it because a spray decal is still "not
# the paused scene".
#
# The readings below are the ones that separate "the demo scene rendered" from
# "the capture is scene-sensitive for an unrelated reason":
#   * which of the five archives main.cpp names actually opened
#     (main.cpp skips a missing one silently -- no error, no counter)
#   * whether the demo's own BSP is reachable at all
#   * how many of that BSP's materials resolve to decodable pixels
#   * how many survive main.cpp's 512x512 atlas cap, and how many triangles
#     those materials cover -- resolvable is not the same as drawable
#
# Usage: bash resource-reachability-check.sh [--mutation]
#   --mutation runs the assertions against a deliberately wrong expectation
#   (the archive count) and requires this script to go red.
# Exit: 0 = every assertion held (or, with --mutation, went red).
set -uo pipefail
cd "$(dirname "$0")"

EXE=native/build-nmake/resource_reachability_probe.exe
TF="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
PY="C:/Users/Administrator/.workbuddy-ai/binaries/python/versions/3.13.12/python.exe"
OUT="${RESOURCE_CHECK_OUT:-evidence/resource-reachability}"
mkdir -p "$OUT"

MUTATION=0
[ "${1:-}" = "--mutation" ] && MUTATION=1

fail=0
for path in "$EXE" "$TF" "$PY"; do
  [ -e "$path" ] || { echo "FATAL: missing input: $path"; exit 1; }
done

assert_eq() { # assert_eq <label> <actual> <expected>
  if [ "${2:-<none>}" = "$3" ]; then
    echo "  OK   $1 = $3"
  else
    echo "  FAIL $1 = ${2:-<none>}, expected $3"; fail=1
  fi
}
field() { # field <json> <key> -> value
  "$PY" -c "
import json,sys
d=json.loads(sys.argv[1])
print(d[sys.argv[2]])
" "$1" "$2"
}

# probe <mapStem> <outFile> -> echoes the JSON, writes it to <outFile>
probe() {
  local stem="$1" out="$2"
  local json
  json=$(timeout 600 "$EXE" "$TF" "$stem" 2>&1) && echo "$json" > "$out"
  echo "$json"
}

echo "=== 1/5 the archives main.cpp names are all present, or the reason is named ==="
# main.cpp opens five named archives and silently drops the ones that fail. The
# count is asserted, and the names that failed are asserted to be the ones we
# actually expect to be missing on this install -- so a *second* archive
# disappearing is caught rather than folded into the first.
SNAKE=$(probe cp_snakewater_final1 "$OUT/snakewater.json")
OPENED=$(field "$SNAKE" archivesOpened)
MISSING=$(field "$SNAKE" missingNames)
ENTRIES=$(field "$SNAKE" totalEntries)
echo "archivesOpened=$OPENED missingNames=$MISSING totalEntries=$ENTRIES"
assert_eq "archives opened" "$OPENED" "4"
assert_eq "missing archive (by name)" "$MISSING" "pak01_dir.vpk"

echo
echo "=== 2/5 the demo map's own BSP is reachable, and its materials resolve ==="
# A BSP that is not reachable is the difference between "the scene drew untextured"
# and "the scene did not draw at all" -- the bagel frame is the second case and
# nothing before this step could tell them apart.
BSP_BYTES=$(field "$SNAKE" bspBytes)
BSP_SOURCE=$(field "$SNAKE" bspSource)
MATERIALS=$(field "$SNAKE" distinctMaterials)
RESOLVED=$(field "$SNAKE" materialsWithDecodableVtf)
echo "bspSource=$BSP_SOURCE bspBytes=$BSP_BYTES distinctMaterials=$MATERIALS materialsWithDecodableVtf=$RESOLVED"
if [ "${BSP_BYTES:-0}" -gt 0 ]; then
  echo "  OK   the demo map's BSP is reachable"
else
  echo "  FAIL the demo map's BSP is unreachable, so no scene was drawn"; fail=1
fi
assert_eq "BSP source" "$BSP_SOURCE" "loose"
if [ "${RESOLVED:-0}" -gt 0 ]; then
  echo "  OK   $RESOLVED materials resolve to decodable pixels"
else
  echo "  FAIL no material of this map resolves to pixels"; fail=1
fi

echo
echo "=== 3/5 resolvable is not drawable: the 512 cap is measured, not assumed ==="
# main.cpp builds the world atlas from at most 64 materials at or below 512x512.
# Most of this map's textures are 1024x1024, so the number that predicts whether
# texturing can be visible at all is the triangle share, not the material count.
ELIGIBLE=$(field "$SNAKE" atlasEligible)
REJECTED=$(field "$SNAKE" oversizedRejected)
TOTAL_TRI=$(field "$SNAKE" totalTriangles)
COVERED=$(field "$SNAKE" trianglesCoveredByAtlas)
echo "atlasEligible=$ELIGIBLE oversizedRejected=$REJECTED triangles=$COVERED/$TOTAL_TRI"
assert_eq "atlas-eligible materials" "$ELIGIBLE" "7"
assert_eq "oversized materials rejected by the 512 cap" "$REJECTED" "104"
assert_eq "triangles the atlas can texture" "$COVERED" "30731"

echo
echo "=== 4/5 the negative control: the map the old gate used is unreachable ==="
# Everything above is about a map that works. This is about the map that does not,
# and it is the reading that would have caught the original defect: step 16 used
# koth_bagel_rc13, and on this machine that map has no BSP at all. If a future
# install adds it this assertion goes red on purpose -- the finding it records
# ("step 16's demo had no scene here") would no longer be true, and a reader needs
# to be told rather than left with a stale note.
BAGEL=$(probe koth_bagel_rc13 "$OUT/bagel.json")
BAGEL_BSP=$(field "$BAGEL" bspBytes)
echo "bagel bspBytes=$BAGEL_BSP (expected 0: this map is not installed here)"
assert_eq "bagel BSP bytes (the unreachable case)" "$BAGEL_BSP" "0"

echo
echo "=== 5/5 the counterfactual: is the 512 cap or the wiring what loses the map? ==="
# Section 3 measures how much the *current* cap can texture (15.4%). It does not
# say whether the loss is the cap's fault or the wiring's. This holds the install
# and the parse fixed and varies only the cap, so the answer is a reading rather
# than an argument. If coverage leaps when the cap rises, the fix is one integer;
# if it stays flat, the cap is innocent and the wiring is what to change.
CAP1024=$("$PY" -c "
import json,sys
print(json.loads(sys.argv[1])['triangleCoverageByCap']['1024'])
" "$SNAKE")
CAP_UNCAP=$("$PY" -c "
import json,sys
print(json.loads(sys.argv[1])['triangleCoverageByCap']['uncapped'])
" "$SNAKE")
echo "triangles covered at cap 1024=$CAP1024 (vs 30731 at 512, of $TOTAL_TRI)"
assert_eq "triangles at a 1024 cap" "$CAP1024" "193875"
assert_eq "triangles with no cap (a real install gap remains)" "$CAP_UNCAP" "199227"
if [ "${CAP1024:-0}" -gt "${COVERED:-0}" ]; then
  echo "  OK   raising the cap 512 -> 1024 recovers geometry (the cap is the bottleneck)"
else
  echo "  FAIL raising the cap recovered nothing (the cap is not the bottleneck)"; fail=1
fi

echo
if [ "$MUTATION" -eq 1 ]; then
  # The archive count is the reading that would have caught the silent skip, so
  # the mutation targets it: demand five archives and require the check to notice
  # that only four opened. A gate whose central reading cannot go red is a gate
  # that would have passed this defect too.
  echo "=== mutation: the expected archive count is wrong on purpose ==="
  if [ "$OPENED" = "5" ]; then
    echo "  FAIL the archive count matched a value it should not have"
    exit 1
  fi
  echo "MUTATION-CAUGHT=PASS (a wrong archive-count expectation goes red)"
  exit 0
fi

if [ "$fail" -eq 0 ]; then
  echo "RESOURCE-REACHABILITY=PASS"
else
  echo "RESOURCE-REACHABILITY=FAIL"
fi
exit "$fail"
