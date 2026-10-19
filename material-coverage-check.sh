#!/usr/bin/env bash
# material-coverage-check.sh -- assert that the world pass covers the materials
# the reference map actually uses, that every material it cannot cover is named,
# and that neither claim can go green while the picture is wrong.
#
# WHY THIS FILE EXISTS
# --------------------
# Before this round `uploadWorldGeometry` stopped at a fixed 64 tiles: the 65th
# material onwards hit `if (accepted.size() >= kMaxTiles) continue;` and was
# dropped with no counter, no name and no reading. On cp_snakewater_final1 that
# was 51 of 148 materials -- 44 of them visible in the reference frame's own pvs,
# 106983 visible triangles, one of them (MEDIEVAL/PLASTERWALL002A) 80256
# triangles of the map's walls. Every counter the pass had stayed green: the
# atlas was bound, the fetch reported `distinct=148 tiled=64`, and the number
# that would have said "51 materials were skipped" did not exist.
#
# So the gate is built around the three things a count of tiles cannot give:
#
#   1. the plan, as a table -- every material with a batch and a tile, or a
#      refusal with a name from a closed set. `plan.accepted + plan.refused`
#      must equal the number of materials the fetch handed over: a material that
#      is neither tiled nor refused is the defect this round removed.
#   2. the picture, as a control -- the same build with the budget cut so only
#      one batch fits reproduces the pre-T19 frame *byte for byte* (measured:
#      65934ede... on this host, against a build of the base commit e67630b).
#      That makes the full frame's difference the coverage difference and
#      nothing else, and it is why the gate asserts the refused set equals the
#      set of materials that only the second batch admitted.
#   3. the state, at the draw -- t0 is bound through one function, and a weapon
#      draw that finds a world atlas there (or a world draw that finds a weapon
#      texture) is counted. Both are asserted 0 on a frame that draws both
#      passes, and one mutation makes the number move.
#
# WHAT IS ASSERTED, IN ORDER
#   1. the probe's own plan: one material, 148 materials across 3 batches, the
#      budget refusal, duplicate and malformed refusals, the uv, the blit
#   2. the fixture: 16 synthetic materials tile and each samples its own block
#   3. the real map: 148 distinct, 112 with pixels, 0 refused, 2 batches, and
#      the four unsupported shader features each refused by name
#   4. the product's reading of the same frame: capped=0, the tile table, the
#      visible count per material, the isolation counters, determinism
#   5. the blend switch off: the albedo batching does not move, the blend tiles
#      leave
#   6. a frame that draws the weapon as well: the isolation counters still 0
#   7. the control: the budget cut to one batch keeps the 64 largest surfaces,
#      names the other 48 as budget refusals, and the frame loses exactly what
#      those 48 carried
#   8. the mutations (--mutation): six defects, each of which must go red
#
# BUILD FIRST
#   bash build-target.sh tf2_demo_native material_coverage_probe
#
# EVERY PER-RUN FILE CARRIES A RUN TAG ($RUN_TAG). `rm -f` exits 0 on this host
# even when the delete guard refuses it, so "clear then read" is not a freshness
# guarantee and a reading taken off another run's bytes looks exactly like a
# reading taken off this one's.
#
# Usage: bash material-coverage-check.sh [--mutation]
# Exit:  0 = every assertion held (or, with --mutation, went red).
set -uo pipefail
cd "$(dirname "$0")"

PROBE=native/build-nmake/material_coverage_probe.exe
PRODUCT=native/build-nmake/tf2_demo_native.exe
TF="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
MAP="cp_snakewater_final1"
DEMO="D:/TF2_Demo_Player/testdata/demos/bb841c6d379ff7c40d0c8baf99f59d8d_matcha-20260927-1347-cp_snakewater_final1.dem"
# A frame in which the weapon pass runs as well, so "the atlas did not leak into
# the weapon" is a statement about a draw that happened.
POV_DEMO="$TF/demos/autorecord_2026-07-02_13-26-46.dem"
POV_TICK=2000
VM_MODEL="models/weapons/v_models/v_scattergun_scout.mdl"
# The map's largest surface: 80256 triangles, every one of them in the reference
# frame's pvs. Under the old name-sorted fetch order it sat at index 71, behind
# the 64-tile ceiling; under the corrected coverage order it is the first tile.
# Named rather than counted, so the gate fails on the coverage claim itself and
# not merely on a total.
BEYOND_CAP_MATERIAL="MEDIEVAL/PLASTERWALL002A"
AUDIO_DEVICE=6
TICK=0
SCRATCH="${MATERIAL_COVERAGE_SCRATCH:-.scratch/material-coverage}"
OUT="${MATERIAL_COVERAGE_OUT:-evidence/material-coverage}"
mkdir -p "$OUT" "$SCRATCH"

RUN_TAG="$(date +%Y%m%d%H%M%S)-$$"
MUTATION=0
[ "${1:-}" = "--mutation" ] && MUTATION=1

fail=0
for path in "$PROBE" "$PRODUCT" "$TF/maps/$MAP.bsp" "$DEMO" "$POV_DEMO"; do
  [ -e "$path" ] || { echo "FATAL: missing input: $path"; exit 1; }
done
command -v timeout >/dev/null 2>&1 || { echo "FATAL: missing tool: timeout"; exit 1; }
command -v cygpath >/dev/null 2>&1 || { echo "FATAL: missing tool: cygpath"; exit 1; }
command -v python >/dev/null 2>&1 || { echo "FATAL: missing tool: python"; exit 1; }
command -v sha256sum >/dev/null 2>&1 || { echo "FATAL: missing tool: sha256sum"; exit 1; }

assert_eq() { # assert_eq <label> <actual> <expected>
  if [ "${2:-<none>}" = "$3" ]; then
    echo "  OK   $1 = $3"
  else
    echo "  FAIL $1 = ${2:-<none>}, expected $3"; fail=1
  fi
}
assert_ge() { # assert_ge <label> <actual> <floor>
  if [ -n "${2:-}" ] && [ "$2" -ge "$3" ] 2>/dev/null; then
    echo "  OK   $1 = $2 (>= $3)"
  else
    echo "  FAIL $1 = ${2:-<none>}, expected >= $3"; fail=1
  fi
}
assert_le() { # assert_le <label> <actual> <ceiling>
  if [ -n "${2:-}" ] && [ "$2" -le "$3" ] 2>/dev/null; then
    echo "  OK   $1 = $2 (<= $3)"
  else
    echo "  FAIL $1 = ${2:-<none>}, expected <= $3"; fail=1
  fi
}
# kv <lines> <key> -> the value of a probe reading. The probe emits one JSON
# object per line -- {"reading":"key","value":"value"} -- because a reading and
# its name have to travel together.
kv() { printf '%s\n' "$1" | tr -d '\r' | sed -n "s|.*\"reading\":\"$2\",\"value\":\"\([^\"]*\)\".*|\1|p" | head -1; }
# ckdetail <lines> <check> -> the "detail" of one named self-test check. The
# self-test speaks a different shape -- {"check":..,"ok":..,"detail":..} -- and
# its detail carries the exact numbers, so the gate can pin them without the
# probe having to print every value twice.
ckdetail() { printf '%s\n' "$1" | tr -d '\r' | grep -F "\"check\":\"$2\"" | sed -n 's/.*"detail":"\([^"]*\)".*/\1/p' | head -1; }
# rval <line> <key> -> the value of "key=value" in a space-separated product line
rval() { printf '%s\n' "$1" | tr -d '\r' | tr ' ' '\n' | sed -n "s/^$2=//p" | head -1; }
# rk <lines> <key> -> the value of a bare "key=value" line, which is what the
# frame comparison emits.
rk() { printf '%s\n' "$1" | tr -d '\r' | sed -n "s/^$2=//p" | head -1; }
# num <value> -> the integer part, so "0.000" can be compared as a number
num() { printf '%s' "${1:-}" | sed -n 's/^\(-\{0,1\}[0-9][0-9]*\).*/\1/p'; }
# check_count <lines> <label> -> "n of n checks held", so a probe that printed
# nothing cannot read as a pass.
check_count() { # check_count <lines> <label> <minimum how many it must print>
  local total ok
  total=$(printf '%s\n' "$1" | grep -c '"check"')
  ok=$(printf '%s\n' "$1" | grep -c '"ok":true')
  assert_ge "$2 checks held" "$ok" "$total"
  assert_ge "$2 check count" "$total" "$3"
}

echo "=== 1/7 the probe's own plan, on synthetic inputs ==="
SELF=$("$PROBE" --self-test 2>&1); SELF_RC=$?
echo "$SELF" > "$OUT/self-test.txt"
assert_eq "self-test rc" "$SELF_RC" "0"
check_count "$SELF" "self-test" "20"
# The numbers the plan has to produce for 148 materials. Stated as readings
# rather than left to the probe's own thresholds: if the batching rule changes,
# this line and the probe both have to be changed on purpose.
assert_eq "148 materials, batches" "$(ckdetail "$SELF" plan.148.batches)" "batches=3"
assert_eq "148 materials, first atlas" "$(ckdetail "$SELF" plan.148.first)" "4112x4112"
assert_eq "148 materials, last atlas" "$(ckdetail "$SELF" plan.148.last)" "20 tiles 2570x2056"
assert_eq "budget refusal count" "$(ckdetail "$SELF" budget.refused)" "accepted=64 budget=84"

echo
echo "=== 2/7 the fixture: 16 materials, each sampling its own block ==="
FIX=$("$PROBE" --fixture 2>&1); FIX_RC=$?
echo "$FIX" > "$OUT/fixture.txt"
assert_eq "fixture rc" "$FIX_RC" "0"
check_count "$FIX" "fixture" "2"
assert_eq "fixture tiled" "$(kv "$FIX" fixture.tiled)" "16"
assert_eq "fixture refused" "$(kv "$FIX" fixture.refused)" "0"
# The sharp one: every material's centre pixel resolves to that material's own
# colour. A uv that is off by one tile, or a blit that writes the wrong block,
# moves this to 15 without moving any count above.
assert_eq "fixture uv matches tile" "$(kv "$FIX" fixture.uvMatchesTile)" "16"
assert_eq "fixture malformed refused" "$(kv "$FIX" fixture.refusedAfterBad)" "1"

echo
echo "=== 3/7 the real map, read off the BSP ==="
REAL=$("$PROBE" --map "$TF/maps/$MAP.bsp" --tf-root "$TF" --expect-material "$BEYOND_CAP_MATERIAL" 2>&1)
REAL_RC=$?
echo "$REAL" > "$OUT/probe-$MAP.txt"
assert_eq "probe rc" "$REAL_RC" "0"
assert_eq "distinct materials" "$(kv "$REAL" map.distinctMaterials)" "148"
assert_eq "materials with pixels" "$(kv "$REAL" plan.tiled)" "112"
# The claim this round is built on: nothing the planner was handed is dropped.
# 148 - 112 = 36 materials the *fetch* refused earlier (4 missing VMT, 31 with no
# $basetexture, 1 missing VTF); the planner itself refuses none.
assert_eq "materials refused by the planner" "$(kv "$REAL" plan.refused)" "0"
assert_eq "batches" "$(kv "$REAL" plan.batches)" "2"
assert_eq "budget refusals" "$(kv "$REAL" plan.refusedBudget)" "0"
assert_eq "malformed refusals" "$(kv "$REAL" plan.refusedMalformed)" "0"
assert_eq "duplicate refusals" "$(kv "$REAL" plan.refusedDuplicate)" "0"
# The number the old ceiling would have produced, measured against the same
# material list: 48 materials were behind the 64-tile wall on this map.
assert_eq "materials beyond the old ceiling" "$(kv "$REAL" plan.beyondOldCap)" "48"
assert_eq "expected material is tiled" "$(kv "$REAL" expect.reason)" "tiled"
# Coverage order puts the map's largest surface first, so the probe's slot and
# the product's tile line have to name the same batch for the same material.
assert_eq "expected material is in batch 0" "$(kv "$REAL" expect.batch)" "0"
# The four shader features the task names. Each is refused with a reason, not
# dropped: `declared` is what the VMTs ask for, and the reason says what the
# pass would have to grow to honour it.
assert_eq "features: \$bumpmap declared" "$(kv "$REAL" feature.bumpmap.declared)" "50"
assert_eq "features: \$bumpmap2 declared" "$(kv "$REAL" feature.bumpmap2.declared)" "1"
assert_eq "features: \$ssbump declared" "$(kv "$REAL" feature.ssbump.declared)" "41"
assert_ge "features: \$detail declared" "$(kv "$REAL" feature.detail.declared)" "40"
B2_REASON=$(kv "$REAL" feature.bumpmap2.reason)
case "$B2_REASON" in
  *refused*) echo "  OK   \$bumpmap2 is refused by name" ;;
  *) echo "  FAIL \$bumpmap2 has no refusal: ${B2_REASON:-<none>}"; fail=1 ;;
esac
SS_REASON=$(kv "$REAL" feature.ssbump.reason)
case "$SS_REASON" in
  *SSBump*) echo "  OK   \$ssbump is refused by name" ;;
  *) echo "  FAIL \$ssbump has no refusal: ${SS_REASON:-<none>}"; fail=1 ;;
esac

# ---------------------------------------------------------------------------
# The product runs. One helper, because every assertion below is a comparison
# between two of them and the only thing that may differ is the switch.
# ---------------------------------------------------------------------------
PAUSE=1  # 0 for a demo that has to run to the tick: --start-paused holds a pov
         # demo on tick 0, and --capture-tick 2000 then waits forever (measured:
         # rc=124 after 400s with the trace still counting `loop ... tick=0`).
run_product() { # run_product <name> <demo> <tick> [extra args...]
  local name="$1" demo="$2" tick="$3"; shift 3
  local trace="$SCRATCH/$name.$RUN_TAG.trace"
  local bmp="$SCRATCH/$name.$RUN_TAG.bmp"
  local log="$SCRATCH/$name.$RUN_TAG.log"
  local pause_args=()
  [ "$PAUSE" -eq 1 ] && pause_args=(--start-paused)
  TF2_NATIVE_TRACE="$(cygpath -m "$PWD/$trace")" timeout 400 "$PRODUCT" \
    --tf-root "$TF" --audio-device "$AUDIO_DEVICE" --audio-disabled --hidden-window \
    --demo "$demo" "${pause_args[@]}" --capture-tick "$tick" \
    --capture-frame "$(cygpath -m "$PWD/$bmp")" "$@" \
    --metrics-file "$(cygpath -m "$PWD/$SCRATCH/$name.$RUN_TAG.csv")" \
    > "$log" 2>&1
  printf -v "RC_$name" '%s' "$?"
  printf -v "TRACE_$name" '%s' "$trace"
  printf -v "BMP_$name" '%s' "$bmp"
}
# tiles_of <trace> -> "material batch" pairs, one per tiled material
tiles_of() { grep -ao "gpu: world tile material=.*" "$1" 2>/dev/null | sed 's/.*material=\([^ ]*\) .*batch=\([-0-9]*\).*/\1 \2/' | tr -d '\r' | awk '$2 >= 0'; }
# refused_of <trace> -> the same "material batch" pairs for the rows that carry a
# named refusal instead of a tile (batch=-1). A material without a tile is not
# absent from the table; it is a row with a reason.
refused_of() { grep -ao "gpu: world tile material=.*" "$1" 2>/dev/null | sed 's/.*material=\([^ ]*\) .*batch=\([-0-9]*\).*/\1 \2/' | tr -d '\r' | awk '$2 < 0'; }

echo
echo "=== 4/7 the product's own reading of the reference frame ==="
run_product full "$DEMO" "$TICK"
assert_eq "full rc" "$RC_full" "0"
FETCH=$(grep -ao "gpu: world material-fetch .*" "$TRACE_full" 2>/dev/null | head -1)
PLAN=$(grep -ao "gpu: world atlas-plan .*" "$TRACE_full" 2>/dev/null | head -1)
ISO=$(grep -ao "gpu: world isolation .*" "$TRACE_full" 2>/dev/null | head -1)
ATLAS=$(grep -ao "gpu: world atlas .*" "$TRACE_full" 2>/dev/null | head -1)
VIS=$(grep -ao "gpu: world vis .*" "$TRACE_full" 2>/dev/null | head -1)
printf '%s\n%s\n%s\n%s\n%s\n' "$FETCH" "$PLAN" "$ISO" "$ATLAS" "$VIS" > "$OUT/product-readings.txt"
# The ceiling, as the reading it should have been all along.
assert_eq "materials tiled" "$(rval "$FETCH" tiled)" "112"
assert_eq "materials dropped by a ceiling" "$(rval "$FETCH" capped)" "0"
assert_eq "the planner refused nothing" "$(rval "$PLAN" refused)" "0"
assert_eq "batches bound" "$(rval "$PLAN" batches)" "2"
assert_eq "budget refusals" "$(rval "$PLAN" budget)" "0"
assert_eq "atlas state" "$(rval "$ATLAS" state)" "bound"
# `bytes` is still the largest single atlas (the 67.6 MiB texture the terrain
# gate has always asserted); the batching shows up in `atlases` and `totalBytes`
# so neither reading has to be reinterpreted.
assert_ge "largest atlas bytes" "$(rval "$ATLAS" bytes)" "67000000"
assert_eq "atlases bound" "$(rval "$ATLAS" atlases)" "2"
assert_ge "atlas bytes in total" "$(rval "$ATLAS" totalBytes)" "119000000"
# The tile table: 112 materials, 48 of them in the batch the old ceiling could
# not have reached. The fetch order is coverage order, so batch 0 is the 64
# largest surfaces and batch 1 the 48 smallest -- the opposite of what the old
# comparator did, which sorted by name and put the map's largest material at
# index 71 (measured: MEDIEVAL/PLASTERWALL002A, 80256 triangles, behind the
# ceiling). Both halves of that are asserted below, because "no silent drop"
# has to hold for the small surfaces the old cap threw away *and* for the large
# one the old order would have thrown away first.
TILES=$(tiles_of "$TRACE_full")
echo "$TILES" > "$OUT/tile-table-$MAP.txt"
assert_eq "tile table rows" "$(printf '%s\n' "$TILES" | grep -c .)" "112"
assert_eq "tiles in batch 0" "$(printf '%s\n' "$TILES" | awk '$2==0' | grep -c .)" "64"
assert_eq "tiles in batch 1" "$(printf '%s\n' "$TILES" | awk '$2==1' | grep -c .)" "48"
VISIBLE_B1=$(grep -ao "gpu: world tile material=.*batch=1 " "$TRACE_full" | grep -c "visible=0 ")
assert_le "batch-1 tiles with no visible triangle" "$VISIBLE_B1" "6"
# The large surface: coverage order keeps it in the first atlas, and it is in
# the picture, not merely resident.
BEYOND_LINE=$(grep -ao "gpu: world tile material=$BEYOND_CAP_MATERIAL .*" "$TRACE_full" | head -1)
assert_eq "$BEYOND_CAP_MATERIAL batch" "$(rval "$BEYOND_LINE" batch)" "0"
assert_ge "$BEYOND_CAP_MATERIAL visible triangles" "$(rval "$BEYOND_LINE" visible)" "10000"
# The small surfaces: the 48 materials behind the old ceiling are visible too,
# and together they carry the pixels the control below takes away.
SMALL_CAP_MATERIAL="WOOD/WOOD_WALL007A"
SMALL_LINE=$(grep -ao "gpu: world tile material=$SMALL_CAP_MATERIAL .*" "$TRACE_full" | head -1)
assert_eq "$SMALL_CAP_MATERIAL batch" "$(rval "$SMALL_LINE" batch)" "1"
assert_ge "$SMALL_CAP_MATERIAL visible triangles" "$(rval "$SMALL_LINE" visible)" "100"
B1_VISIBLE_SUM=$(grep -ao "gpu: world tile material=.*batch=1 " "$TRACE_full" \
  | sed 's/.*visible=\([0-9]*\) .*/\1/' | awk '{s+=$1} END {print s+0}')
assert_ge "batch-1 visible triangles" "$B1_VISIBLE_SUM" "1500"
# The state leaving one pass and arriving at another. Both counters are read at
# the draw, after the bind, so a pass that forgot to bind its own texture leaves
# the previous pass's view in t0 and moves the number.
assert_eq "weapon draws holding a world atlas" "$(rval "$ISO" weaponDrawsWithWorldAtlas)" "0"
assert_eq "world draws holding a weapon texture" "$(rval "$ISO" worldDrawsWithWeaponTexture)" "0"
assert_eq "batch draws" "$(rval "$ISO" batchDraws)" "2"
assert_eq "batch draws with a texture" "$(rval "$ISO" texturedBatchDraws)" "2"
assert_ge "slot-0 binds" "$(rval "$ISO" slot0Binds)" "2"
# The pvs is not what this round changed: the same 189128 triangles are drawn.
assert_eq "triangles drawn" "$(rval "$VIS" drawnTriangles)" "189128"
# Two runs of the same build, byte for byte.
run_product full2 "$DEMO" "$TICK"
if cmp -s "$BMP_full" "$BMP_full2"; then
  echo "  OK   two runs produced the same frame byte for byte"
else
  echo "  FAIL two runs produced different frames"; fail=1
fi
( cd "$SCRATCH" && sha256sum "full.$RUN_TAG.bmp" ) | sed 's/^/  ..   /'
# A second frame with the pvs clip off. The index buffer built at upload time is
# what the pass draws in that mode, and it is the buffer m5 mutates; without a
# frame that takes this path, a wrong index base moves no pixel and the mutation
# reads as "not caught" for a reason that has nothing to do with the defect.
run_product noclip "$DEMO" "$TICK" --no-world-vis-clip
assert_eq "unclipped rc" "$RC_noclip" "0"
NOCLIP_VIS=$(grep -ao "gpu: world vis .*" "$TRACE_noclip" 2>/dev/null | head -1)
NOCLIP_PLAN=$(grep -ao "gpu: world atlas-plan .*" "$TRACE_noclip" 2>/dev/null | head -1)
assert_eq "unclipped frame is unclipped" "$(rval "$NOCLIP_VIS" clipApplied)" "0"
assert_eq "unclipped tiles" "$(rval "$NOCLIP_PLAN" tiled)" "112"
assert_eq "unclipped batches" "$(rval "$NOCLIP_PLAN" batches)" "2"

echo
echo "=== 5/7 the blend switch off: the albedo batching must not move ==="
run_product noblend "$DEMO" "$TICK" --world-blend off
assert_eq "blend off rc" "$RC_noblend" "0"
PLAN_OFF=$(grep -ao "gpu: world atlas-plan .*" "$TRACE_noblend" 2>/dev/null | head -1)
assert_eq "blend off batches" "$(rval "$PLAN_OFF" batches)" "2"
assert_eq "blend off tiles" "$(rval "$PLAN_OFF" tiled)" "112"
assert_eq "blend off blend batches" "$(rval "$PLAN_OFF" blendBatches)" "0"

echo
echo "=== 6/7 a frame that draws the weapon as well ==="
PAUSE=0
run_product weapon "$POV_DEMO" "$POV_TICK" --viewmodel "$VM_MODEL" --viewmodel-fov 90
PAUSE=1
assert_eq "weapon frame rc" "$RC_weapon" "0"
ISO_W=$(grep -ao "gpu: world isolation .*" "$TRACE_weapon" 2>/dev/null | head -1)
PASSES=$(grep -ao "gpu: viewmodel pass order .*" "$TRACE_weapon" 2>/dev/null | head -1)
DRAWN=$(grep -ao "gpu: viewmodel materials .*drawnSlot=.*" "$TRACE_weapon" 2>/dev/null | head -1)
echo "$ISO_W" > "$OUT/isolation-weapon.txt"
# The weapon pass has to have drawn something, and drawn after the world, or the
# two counters below are statements about a frame in which nothing happened.
assert_eq "weapon drew after the world" "$(rval "$PASSES" afterWorld)" "1"
assert_ge "weapon draw calls" "$(rval "$PASSES" drawCalls)" "1"
assert_eq "weapon frame: weapon draws holding a world atlas" "$(rval "$ISO_W" weaponDrawsWithWorldAtlas)" "0"
assert_eq "weapon frame: world draws holding a weapon texture" "$(rval "$ISO_W" worldDrawsWithWeaponTexture)" "0"
assert_ge "weapon frame: slot-0 binds" "$(rval "$ISO_W" slot0Binds)" "2"

echo
echo "=== 7/7 the control: one batch, and the picture it has to reproduce ==="
# The budget is cut so that only the first batch fits. That is not a defect: it
# is the pre-T19 tile set, arrived at through the new planner. Everything the
# batching added must disappear together, and the frame must fall back to the
# picture the base commit produced -- measured on this host at
# 65934eded901e43e6c98f28132760d0b4cb3777c2b5c65fe372ea4a75243da7c.
CONTROL_BACKUP="$SCRATCH/world_tile_atlas.h.control.backup"
cp native/include/world_tile_atlas.h "$CONTROL_BACKUP"
CONTROL_HASH=$(git hash-object native/include/world_tile_atlas.h)
patch_in() { # patch_in <file> <find> <replace> -- CRLF-aware, refuses a no-op
  python - "$1" "$2" "$3" <<'PY'
import io, sys
path, find, repl = sys.argv[1], sys.argv[2], sys.argv[3]
raw = io.open(path, 'rb').read()
crlf = b'\r\n' in raw
data = raw.decode('utf-8')
if crlf:
    find = find.replace('\n', '\r\n')
    repl = repl.replace('\n', '\r\n')
if find not in data:
    sys.exit('PATTERN NOT FOUND: ' + find[:70].replace('\r', '\\r').replace('\n', '\\n'))
io.open(path, 'wb').write(data.replace(find, repl, 1).encode('utf-8'))
PY
}
restore_source() { # restore_source <backup> <live> <name> <expected-hash> [targets...]
  local backup="$1" live="$2" name="$3" expected="$4"; shift 4
  cp "$backup" "$live"
  if [ "$(git hash-object "$live")" != "$expected" ]; then
    echo "  RESTORE=FAIL ($name: the source did not come back byte-identical)"; return 1
  fi
  if ! bash build-target.sh "$@" > "$SCRATCH/restore-$name.$RUN_TAG.build" 2>&1; then
    echo "  RESTORE-BUILD=FAIL ($name)"; return 1
  fi
  return 0
}
if ! patch_in native/include/world_tile_atlas.h \
    '  std::uint64_t budgetBytes = 384ull * 1024ull * 1024ull;' \
    '  std::uint64_t budgetBytes = 210ull * 1024ull * 1024ull;  // control: one batch only'; then
  echo "  CONTROL-SETUP=FAIL (the budget patch did not apply)"; cp "$CONTROL_BACKUP" native/include/world_tile_atlas.h
  echo "MATERIAL-COVERAGE=FAIL"; exit 1
fi
if ! bash build-target.sh tf2_demo_native material_coverage_probe > "$SCRATCH/control.build" 2>&1; then
  echo "  CONTROL-SETUP=FAIL (the rebuild failed)"
  restore_source "$CONTROL_BACKUP" native/include/world_tile_atlas.h control "$CONTROL_HASH" tf2_demo_native material_coverage_probe || true
  echo "MATERIAL-COVERAGE=FAIL"; exit 1
fi
CONTROL_PLAN=$("$PROBE" --map "$TF/maps/$MAP.bsp" --tf-root "$TF" 2>&1)
echo "$CONTROL_PLAN" > "$OUT/control-one-batch.txt"
run_product control "$DEMO" "$TICK"
CONTROL_LINE=$(grep -ao "gpu: world atlas-plan .*" "$TRACE_control" 2>/dev/null | head -1)
assert_eq "control rc" "$RC_control" "0"
assert_eq "control tiles" "$(rval "$CONTROL_LINE" tiled)" "64"
assert_eq "control refused" "$(rval "$CONTROL_LINE" refused)" "48"
assert_eq "control refusals are budget refusals" "$(rval "$CONTROL_LINE" budget)" "48"
assert_eq "control batches" "$(rval "$CONTROL_LINE" batches)" "1"
# The refusal has to name the same 48 materials the second batch admitted, or
# "bounded batching added these" is a claim about a total rather than about a
# set. `comm -23` is a set difference: in the full run's batch 1, not in the
# control's tile table.
printf '%s\n' "$TILES" | awk '$2==1 {print $1}' | sort > "$SCRATCH/beyond-cap.$RUN_TAG.txt"
tiles_of "$TRACE_control" | awk '{print $1}' | sort > "$SCRATCH/control-tiles.$RUN_TAG.txt"
comm -23 "$SCRATCH/beyond-cap.$RUN_TAG.txt" "$SCRATCH/control-tiles.$RUN_TAG.txt" \
  > "$SCRATCH/control-refused.$RUN_TAG.txt"
assert_eq "materials the control refused" "$(grep -c . "$SCRATCH/control-refused.$RUN_TAG.txt")" "48"
# ...and every one of them is a named refusal in the table, not a missing row.
CONTROL_REFUSED_ROWS=$(refused_of "$TRACE_control" | grep -c .)
CONTROL_BUDGET_ROWS=$(grep -ao "gpu: world tile .*" "$TRACE_control" | grep -c "reason=atlas-budget")
assert_eq "control rows that name a refusal" "$CONTROL_REFUSED_ROWS" "48"
assert_eq "control rows refused for budget" "$CONTROL_BUDGET_ROWS" "48"
# And the picture: the coverage difference is visible, not merely allocated.
DELTA=$("python" - "$BMP_full" "$BMP_control" 2>&1 <<'PY'
import struct, sys

def load(path):
    d = open(path, 'rb').read()
    if d[:2] != b'BM':
        raise SystemExit('not a BMP: ' + path)
    off = struct.unpack_from('<I', d, 10)[0]
    w, h = struct.unpack_from('<ii', d, 18)
    bpp = struct.unpack_from('<H', d, 28)[0]
    if bpp not in (24, 32):
        raise SystemExit('unsupported BMP: %s bpp=%d' % (path, bpp))
    return {'w': w, 'h': abs(h), 'bpp': bpp, 'off': off, 'topdown': h < 0, 'd': d}

a, b = load(sys.argv[1]), load(sys.argv[2])
if (a['w'], a['h'], a['bpp']) != (b['w'], b['h'], b['bpp']):
    raise SystemExit('frames differ in shape')
w, h, step = a['w'], a['h'], a['bpp'] // 8
stride = ((w * a['bpp'] // 8) + 3) // 4 * 4
diff = 0
worst = 0
for y in range(h):
    ra = a['off'] + (y if a['topdown'] else h - 1 - y) * stride
    rb = b['off'] + (y if b['topdown'] else h - 1 - y) * stride
    if a['d'][ra:ra + w * step] == b['d'][rb:rb + w * step]:
        continue
    for x in range(w):
        p = x * step
        m = max(abs(a['d'][ra + p] - b['d'][rb + p]),
                abs(a['d'][ra + p + 1] - b['d'][rb + p + 1]),
                abs(a['d'][ra + p + 2] - b['d'][rb + p + 2]))
        if m:
            diff += 1
            if m > worst:
                worst = m
print('pixels=%d' % (w * h))
print('changed=%d' % diff)
print('max_channel_delta=%d' % worst)
PY
)
echo "$DELTA" > "$OUT/coverage-delta.txt"
printf '%s\n' "$DELTA" | sed 's/^/  /'
assert_ge "pixels the extra batch changes" "$(rk "$DELTA" changed)" "5000"
assert_ge "largest channel delta" "$(rk "$DELTA" max_channel_delta)" "32"
restore_source "$CONTROL_BACKUP" native/include/world_tile_atlas.h control "$CONTROL_HASH" tf2_demo_native material_coverage_probe || {
  echo "MATERIAL-COVERAGE=FAIL"; exit 1; }

echo
if [ "$MUTATION" -eq 1 ]; then
  echo "=== the mutations: six defects, each of which must turn this gate red ==="
  # The cheap half first: values the forward run must NOT produce.
  if [ "$(rval "$FETCH" capped)" != "0" ]; then
    echo "  FAIL a material was dropped by a ceiling before any mutation"; exit 1
  fi
  if [ "$(rval "$PLAN" refused)" != "0" ]; then
    echo "  FAIL the planner refused a material before any mutation"; exit 1
  fi
  if [ "$(rval "$ISO" weaponDrawsWithWorldAtlas)" != "0" ]; then
    echo "  FAIL t0 leaked across a pass before any mutation"; exit 1
  fi

  # ---- m1: the budget stops the batching after one batch --------------------
  # This is the old ceiling, rebuilt out of new parts: one atlas, 64 tiles, the
  # other 48 materials untextured. Every count that existed before this round
  # stays green -- the atlas is bound, the fetch reports no cap -- and the
  # coverage claim is what goes red. Already run above as the control; the
  # defect is the *forward* assertion that tiled=112, which the control's 64
  # fails.
  echo "--- m1: the budget admits one batch (the old 64-tile ceiling, rebuilt)"
  if [ "$(rval "$CONTROL_LINE" tiled)" = "112" ]; then
    echo "  MUTATION-CAUGHT=FAIL (m1: the control still tiled every material)"
    echo "MATERIAL-COVERAGE-MUTATION=FAIL"; exit 1
  fi
  if [ "$(rval "$CONTROL_LINE" tiled)" = "64" ] && [ "$(rval "$CONTROL_LINE" refused)" = "48" ]; then
    echo "  MUTATION-CAUGHT=PASS (m1: tiled=64 refused=48 while the forward run read 112/0)"
  else
    echo "  MUTATION-CAUGHT=FAIL (m1: tiled=$(rval "$CONTROL_LINE" tiled) refused=$(rval "$CONTROL_LINE" refused))"
    echo "MATERIAL-COVERAGE-MUTATION=FAIL"; exit 1
  fi

  # ---- m2: the half-texel in the uv is dropped -----------------------------
  # The sampler sits at the texel centre; without the +0.5 every tile samples
  # its neighbour's last column and the seam between two materials moves by one
  # texel. No count moves, no draw call moves, and the tiles are all still
  # allocated -- the picture is the only thing that says so, which is why the
  # probe checks this on the uv rather than on a pixel.
  echo "--- m2: worldTileCoord loses its half-texel (every tile samples its edge)"
  M2_BACKUP="$SCRATCH/world_tile_atlas.cpp.uv.backup"
  cp native/src/world_tile_atlas.cpp "$M2_BACKUP"
  M2_HASH=$(git hash-object native/src/world_tile_atlas.cpp)
  if ! patch_in native/src/world_tile_atlas.cpp \
      '  return (static_cast<float>(offset) + wrapped * static_cast<float>(extent) / size + 0.5f)' \
      '  return (static_cast<float>(offset) + wrapped * static_cast<float>(extent) / size)'; then
    echo "  MUTATION-SETUP=FAIL (m2: the patch did not apply)"; cp "$M2_BACKUP" native/src/world_tile_atlas.cpp
    echo "MATERIAL-COVERAGE-MUTATION=FAIL"; exit 1
  fi
  if ! bash build-target.sh material_coverage_probe > "$SCRATCH/m2.build" 2>&1; then
    echo "  MUTATION-SETUP=FAIL (m2: the rebuild failed)"
    restore_source "$M2_BACKUP" native/src/world_tile_atlas.cpp m2 "$M2_HASH" material_coverage_probe || true
    echo "MATERIAL-COVERAGE-MUTATION=FAIL"; exit 1
  fi
  M2=$("$PROBE" --self-test 2>&1); M2_RC=$?
  M2_UV=$(ckdetail "$M2" uv.origin)
  SELF_UV=$(ckdetail "$SELF" uv.origin)
  if [ "$M2_RC" != "0" ] && [ "$M2_UV" != "$SELF_UV" ]; then
    echo "  MUTATION-CAUGHT=PASS (m2: self-test rc=$M2_RC, uv.origin=$M2_UV against $SELF_UV)"
  else
    echo "  MUTATION-CAUGHT=FAIL (m2: self-test rc=$M2_RC, uv.origin=$M2_UV)"
    restore_source "$M2_BACKUP" native/src/world_tile_atlas.cpp m2 "$M2_HASH" material_coverage_probe || true
    echo "MATERIAL-COVERAGE-MUTATION=FAIL"; exit 1
  fi
  echo "$M2" > "$OUT/mutated-m2-no-half-texel.txt"
  restore_source "$M2_BACKUP" native/src/world_tile_atlas.cpp m2 "$M2_HASH" material_coverage_probe || {
    echo "MATERIAL-COVERAGE-MUTATION=FAIL"; exit 1; }

  # ---- m3: a weapon range is drawn with the world's atlas ------------------
  # The bleed this round's isolation counter exists for: the weapon pass binds
  # t0 to the world atlas instead of the weapon's own material, so the gun is
  # painted with a wall. `worldDrawsWithWeaponTexture` stays 0 and every draw
  # count stays the same; only the cross-pass counter moves.
  echo "--- m3: a weapon range binds the world atlas at t0 (the gun samples a wall)"
  M3_BACKUP="$SCRATCH/native_renderer.cpp.leak.backup"
  cp native/src/native_renderer.cpp "$M3_BACKUP"
  M3_HASH=$(git hash-object native/src/native_renderer.cpp)
  if ! patch_in native/src/native_renderer.cpp \
      '          bindSlot0(view, "weapon-range");' \
      '          bindSlot0(worldAtlasTextures_.empty() ? view : worldAtlasTextures_.front().view(), "weapon-range");'; then
    echo "  MUTATION-SETUP=FAIL (m3: the patch did not apply)"; cp "$M3_BACKUP" native/src/native_renderer.cpp
    echo "MATERIAL-COVERAGE-MUTATION=FAIL"; exit 1
  fi
  if ! bash build-target.sh tf2_demo_native > "$SCRATCH/m3.build" 2>&1; then
    echo "  MUTATION-SETUP=FAIL (m3: the rebuild failed)"
    restore_source "$M3_BACKUP" native/src/native_renderer.cpp m3 "$M3_HASH" tf2_demo_native || true
    echo "MATERIAL-COVERAGE-MUTATION=FAIL"; exit 1
  fi
  run_product m3 "$POV_DEMO" "$POV_TICK" --viewmodel "$VM_MODEL" --viewmodel-fov 90
  M3_ISO=$(grep -ao "gpu: world isolation .*" "$TRACE_m3" 2>/dev/null | head -1)
  M3_LEAK=$(rval "$M3_ISO" weaponDrawsWithWorldAtlas)
  if [ "${M3_LEAK:-0}" -gt 0 ] 2>/dev/null; then
    echo "  MUTATION-CAUGHT=PASS (m3: weaponDrawsWithWorldAtlas=$M3_LEAK while the forward run read 0)"
  else
    echo "  MUTATION-CAUGHT=FAIL (m3: weaponDrawsWithWorldAtlas=${M3_LEAK:-<none>})"
    restore_source "$M3_BACKUP" native/src/native_renderer.cpp m3 "$M3_HASH" tf2_demo_native || true
    echo "MATERIAL-COVERAGE-MUTATION=FAIL"; exit 1
  fi
  echo "$M3_ISO" > "$OUT/mutated-m3-weapon-leak.txt"
  restore_source "$M3_BACKUP" native/src/native_renderer.cpp m3 "$M3_HASH" tf2_demo_native || {
    echo "MATERIAL-COVERAGE-MUTATION=FAIL"; exit 1; }

  # ---- m4: $bumpmap2 is dropped instead of refused -------------------------
  # The task's forth step: a feature that is not implemented must be refused by
  # name. Here the reader simply stops counting it, so the material reads as if
  # it never asked -- which is the failure mode "silently ignored" looks like.
  echo "--- m4: \$bumpmap2 stops being counted (an unimplemented layer is ignored)"
  M4_BACKUP="$SCRATCH/world_material_features.cpp.bumpmap2.backup"
  cp native/src/world_material_features.cpp "$M4_BACKUP"
  M4_HASH=$(git hash-object native/src/world_material_features.cpp)
  if ! patch_in native/src/world_material_features.cpp \
      '    if (!vmt.bumpMap2.empty()) {' \
      '    if (false) {  // mutation: $bumpmap2 is neither applied nor refused'; then
    echo "  MUTATION-SETUP=FAIL (m4: the patch did not apply)"; cp "$M4_BACKUP" native/src/world_material_features.cpp
    echo "MATERIAL-COVERAGE-MUTATION=FAIL"; exit 1
  fi
  if ! bash build-target.sh material_coverage_probe > "$SCRATCH/m4.build" 2>&1; then
    echo "  MUTATION-SETUP=FAIL (m4: the rebuild failed)"
    restore_source "$M4_BACKUP" native/src/world_material_features.cpp m4 "$M4_HASH" material_coverage_probe || true
    echo "MATERIAL-COVERAGE-MUTATION=FAIL"; exit 1
  fi
  M4=$("$PROBE" --map "$TF/maps/$MAP.bsp" --tf-root "$TF" 2>&1)
  M4_DECLARED=$(kv "$M4" feature.bumpmap2.declared)
  if [ "$M4_DECLARED" = "0" ] && [ "$(kv "$REAL" feature.bumpmap2.declared)" = "1" ]; then
    echo "  MUTATION-CAUGHT=PASS (m4: declared=0 after the mutation, 1 before)"
  else
    echo "  MUTATION-CAUGHT=FAIL (m4: declared=$M4_DECLARED)"
    restore_source "$M4_BACKUP" native/src/world_material_features.cpp m4 "$M4_HASH" material_coverage_probe || true
    echo "MATERIAL-COVERAGE-MUTATION=FAIL"; exit 1
  fi
  echo "$M4" > "$OUT/mutated-m4-silent-bumpmap2.txt"
  restore_source "$M4_BACKUP" native/src/world_material_features.cpp m4 "$M4_HASH" material_coverage_probe || {
    echo "MATERIAL-COVERAGE-MUTATION=FAIL"; exit 1; }

  # ---- m5: the batch index is built from triangles, not vertices -----------
  # The refactor's own failure mode: `triangle * 3` becomes `triangle`, so every
  # index points into the middle of some other triangle. All 112 tiles are still
  # allocated, all 2 batches are still drawn, the plan is unchanged, and the map
  # turns into a hash of stretched polygons. Only the frame says so.
  echo "--- m5: the index buffer is built from triangle numbers (geometry tears)"
  M5_BACKUP="$SCRATCH/native_renderer.cpp.index.backup"
  cp native/src/native_renderer.cpp "$M5_BACKUP"
  M5_HASH=$(git hash-object native/src/native_renderer.cpp)
  if ! patch_in native/src/native_renderer.cpp \
      '      const auto base = static_cast<std::uint32_t>(triangle * 3u);' \
      '      const auto base = static_cast<std::uint32_t>(triangle);  // mutation: no *3'; then
    echo "  MUTATION-SETUP=FAIL (m5: the patch did not apply)"; cp "$M5_BACKUP" native/src/native_renderer.cpp
    echo "MATERIAL-COVERAGE-MUTATION=FAIL"; exit 1
  fi
  if ! bash build-target.sh tf2_demo_native > "$SCRATCH/m5.build" 2>&1; then
    echo "  MUTATION-SETUP=FAIL (m5: the rebuild failed)"
    restore_source "$M5_BACKUP" native/src/native_renderer.cpp m5 "$M5_HASH" tf2_demo_native || true
    echo "MATERIAL-COVERAGE-MUTATION=FAIL"; exit 1
  fi
  run_product m5 "$DEMO" "$TICK" --no-world-vis-clip
  M5_PLAN=$(grep -ao "gpu: world atlas-plan .*" "$TRACE_m5" 2>/dev/null | head -1)
  if cmp -s "$BMP_noclip" "$BMP_m5"; then
    echo "  MUTATION-CAUGHT=FAIL (m5: the frame is unchanged, so the index base is not what draws it)"
    restore_source "$M5_BACKUP" native/src/native_renderer.cpp m5 "$M5_HASH" tf2_demo_native || true
    echo "MATERIAL-COVERAGE-MUTATION=FAIL"; exit 1
  fi
  if [ "$(rval "$M5_PLAN" tiled)" = "112" ] && [ "$(rval "$M5_PLAN" batches)" = "2" ]; then
    echo "  MUTATION-CAUGHT=PASS (m5: tiled=112 batches=2 unchanged, the frame moved)"
  else
    echo "  MUTATION-CAUGHT=FAIL (m5: the plan moved too, so this did not isolate the index)"
    restore_source "$M5_BACKUP" native/src/native_renderer.cpp m5 "$M5_HASH" tf2_demo_native || true
    echo "MATERIAL-COVERAGE-MUTATION=FAIL"; exit 1
  fi
  ( cd "$SCRATCH" && sha256sum "m5.$RUN_TAG.bmp" "noclip.$RUN_TAG.bmp" ) | sed 's/^/  ..   /'
  restore_source "$M5_BACKUP" native/src/native_renderer.cpp m5 "$M5_HASH" tf2_demo_native || {
    echo "MATERIAL-COVERAGE-MUTATION=FAIL"; exit 1; }

  # ---- m6: the clip ignores the batch table --------------------------------
  # The other half of the same question. The frame is clipped, so the indices
  # come from `setWorldVisDrawList`; putting every surviving index in batch 0
  # leaves the plan at 112 tiles / 2 batches, keeps every tile allocated, and
  # paints batch 1's materials with batch 0's atlas. Texture selection and
  # index grouping have to agree, and only the picture says whether they do.
  echo "--- m6: the clipped indices all land in batch 0 (batch 1 samples batch 0's atlas)"
  M6_BACKUP="$SCRATCH/native_renderer.cpp.regroup.backup"
  cp native/src/native_renderer.cpp "$M6_BACKUP"
  M6_HASH=$(git hash-object native/src/native_renderer.cpp)
  if ! patch_in native/src/native_renderer.cpp \
      '    perBatch[static_cast<std::size_t>(batch)].push_back(index);' \
      '    perBatch[0].push_back(index);  // mutation: the clip ignores the batch table'; then
    echo "  MUTATION-SETUP=FAIL (m6: the patch did not apply)"; cp "$M6_BACKUP" native/src/native_renderer.cpp
    echo "MATERIAL-COVERAGE-MUTATION=FAIL"; exit 1
  fi
  if ! bash build-target.sh tf2_demo_native > "$SCRATCH/m6.build" 2>&1; then
    echo "  MUTATION-SETUP=FAIL (m6: the rebuild failed)"
    restore_source "$M6_BACKUP" native/src/native_renderer.cpp m6 "$M6_HASH" tf2_demo_native || true
    echo "MATERIAL-COVERAGE-MUTATION=FAIL"; exit 1
  fi
  run_product m6 "$DEMO" "$TICK"
  M6_PLAN=$(grep -ao "gpu: world atlas-plan .*" "$TRACE_m6" 2>/dev/null | head -1)
  if cmp -s "$BMP_full" "$BMP_m6"; then
    echo "  MUTATION-CAUGHT=FAIL (m6: the frame is unchanged, so the regroup is not what draws it)"
    restore_source "$M6_BACKUP" native/src/native_renderer.cpp m6 "$M6_HASH" tf2_demo_native || true
    echo "MATERIAL-COVERAGE-MUTATION=FAIL"; exit 1
  fi
  if [ "$(rval "$M6_PLAN" tiled)" = "112" ] && [ "$(rval "$M6_PLAN" batches)" = "2" ]; then
    echo "  MUTATION-CAUGHT=PASS (m6: tiled=112 batches=2 unchanged, the frame moved)"
  else
    echo "  MUTATION-CAUGHT=FAIL (m6: the plan moved too, so this did not isolate the regroup)"
    restore_source "$M6_BACKUP" native/src/native_renderer.cpp m6 "$M6_HASH" tf2_demo_native || true
    echo "MATERIAL-COVERAGE-MUTATION=FAIL"; exit 1
  fi
  ( cd "$SCRATCH" && sha256sum "m6.$RUN_TAG.bmp" "full.$RUN_TAG.bmp" ) | sed 's/^/  ..   /'
  restore_source "$M6_BACKUP" native/src/native_renderer.cpp m6 "$M6_HASH" tf2_demo_native || {
    echo "MATERIAL-COVERAGE-MUTATION=FAIL"; exit 1; }

  if [ "$fail" -ne 0 ]; then
    echo "  the mutation half caught its six cases, but a forward assertion above"
    echo "  did not hold -- a caught mutation over a red gate is not a pass"
    echo "MATERIAL-COVERAGE-MUTATION=FAIL"; exit 1
  fi
  echo "MUTATION-CAUGHT=PASS (the one-batch budget, the dropped half-texel, the"
  echo "  weapon range bound to the world atlas, the ignored \$bumpmap2, the"
  echo "  triangle-numbered index and the regroup that ignored the batch table each"
  echo "  go red; every source came back byte-identical)"
  exit 0
fi

echo
if [ "$fail" -eq 0 ]; then
  echo "MATERIAL-COVERAGE=PASS"
else
  echo "MATERIAL-COVERAGE=FAIL"
fi
exit "$fail"
