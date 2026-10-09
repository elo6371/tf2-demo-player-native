#!/usr/bin/env bash
# entity-material-check.sh -- a model drawn as an entity must carry its own paint.
#
# WHY THIS EXISTS
# ---------------
# The entity draw block bound the map atlas to every model's t0, so a model was
# painted whatever the world atlas happened to hold at the model's texel. No
# reading in the chain could separate that from "the model has no paint": every
# counter was a count, and a count is the same number whether the SRV behind it
# is the right archive, the wrong archive, or nothing at all.
#
# It also closes a blind spot in the chain's field of view. Every gate that runs
# the program captures at `--capture-tick 0`, which is playback tick 16 -- before
# this demo's first checkpoint (scene tick 10320). Entities do not exist yet at
# tick 16. A defect that only fires once entities exist is invisible to a gate
# that leaves before they do, and on 2026-10-09 one did exactly that: the program
# died on the first drawable snapshot while every gate stayed green.
#
# THREE LAYERS, WEAKEST LAST
# --------------------------
# 1. synthetic -- the .mdl texture table -> VMT -> VTF chain, per model, counted
#    from bytes. Pins both defects this round found in the reader: the CD table
#    is a list of absolute file offsets (medic.mdl used to resolve an empty list)
#    and a bare texture stem belongs to the `$cdmaterials` directories, not to
#    the model's own directory (resupply_locker used to resolve 0 of 4 slots).
# 2. wiring -- `entityMaterials=N/M` out of the window title at a tick past the
#    entity boundary: instances whose draw range carries a model SRV, over
#    instances drawn. This is a work count, not a count of calls made, and it
#    has to be read at a tick where instances exist. Plus the per-model paint
#    ledger written by `--dump-entity-materials`.
# 3. pixel -- the model block in the captured frame, compared against a witness
#    captured at the same tick by the build that predates the binding. The
#    witness's block is atlas-painted, so "still equals the witness" is the
#    defect. This layer is 20 pixels wide because the demo's camera sits at the
#    world origin and only one instance is in view (see
#    .scratch/obs/entity-material-wiring-recon.md sections 3 and 4); it is real
#    but weak, and the two layers above carry the weight.
#
# THE MUTATION
# ------------
# `--mutation` replaces the fresh capture with the witness, i.e. puts the
# atlas-painted pixels back, and requires layer 3 to go red. That is the proof
# that layer 3 can fail. The equivalent source-level short-circuit -- material
# resolution returning no SRV at all -- is `m12` in mutate.sh, because that one
# needs a rebuild and a committed tree.
#
# Usage: bash entity-material-check.sh [--mutation]
# Exit:  0 = every assertion held (or, with --mutation, the checks went red).
set -uo pipefail
cd "$(dirname "$0")"

EXE=native/build-nmake/tf2_demo_native.exe
PROBE=native/build-nmake/entity_model_probe.exe
TF="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
# The demo whose map is installed: it produces a real world, so the model block
# in the frame is a model over world geometry rather than over the fallback quad.
DEMO="D:/TF2_Demo_Player/testdata/demos/bb841c6d379ff7c40d0c8baf99f59d8d_matcha-20260927-1347-cp_snakewater_final1.dem"
OUT="${ENTITY_MATERIAL_OUT:-evidence/entity-material}"
mkdir -p "$OUT"
# The witness is a committed input, not something this gate generates: the build
# it came from is gone, and a criterion that regenerates its own reference from
# the code under test cannot fail. It was captured 2026-10-09 19:29 from the
# tree at 665c0e9 -- before any draw call bound a model texture -- at playback
# tick 3001, the same tick this gate captures.
WITNESS="${ENTITY_MATERIAL_WITNESS:-evidence/entity-material/witness-tick3001.bmp}"

# The tick the capture is armed at. The capture lands on the first drawn frame at
# or after it, which is playback tick 3001: 2932 ticks past the boundary at which
# entities appear (tick 69), with a checkpoint snapshot resolved (stale=158).
CAPTURE_TICK=3000
# The rectangle the single visible instance paints into, measured on the witness
# and confirmed against the tick-0 capture (see the recon note, section 3). It is
# 6x5 pixels. Pinned rather than searched for: a criterion that finds the block
# by looking for "wherever the picture changed" would pass on any change at all.
BLOCK_X0=429; BLOCK_Y0=309; BLOCK_X1=434; BLOCK_Y1=313

MUTATION=0
[ "${1:-}" = "--mutation" ] && MUTATION=1

fail=0
assert_eq() { # assert_eq <label> <actual> <expected>
  if [ "${2:-<none>}" = "$3" ]; then
    echo "  OK   $1 = $3"
  else
    echo "  FAIL $1 = ${2:-<none>}, expected $3"; fail=1
  fi
}

# Missing input is a failure, not a skip: a gate that quietly SKIPs when the
# binary, the game or the demo is absent reads as green while measuring nothing.
for path in "$EXE" "$PROBE" "$TF" "$DEMO" "$WITNESS"; do
  [ -e "$path" ] || { echo "FATAL: missing input: $path"; exit 1; }
done

PY="C:/Users/Administrator/.workbuddy-ai/binaries/python/versions/3.13.12/python.exe"
[ -x "$PY" ] || { echo "FATAL: missing interpreter: $PY"; exit 1; }

echo "=== 1/4 the model's own paint resolves, per model, from bytes ==="
# check_model <mdl> <declared slots> <resolved slots> <cd directory that must appear>
# The probe prints one summary line per slot table and the CD list it resolved
# against; both are asserted, because the resolved count alone cannot say *which*
# rule produced it -- scout.mdl resolves under both the wrong rule and the right
# one (its only CD directory is its own), which is why the wrong rule survived.
check_model() {
  local mdl="$1" slots="$2" resolved="$3" cd="$4"
  local log="$OUT/probe-$(echo "$mdl" | tr '/' '_').txt"
  timeout 120 "$PROBE" --tf-root "$TF" --dump-model-textures "$mdl" > "$log" 2>&1
  local rc=$?
  local declared
  declared=$(grep -oE "model-textures path=$mdl declared=[0-9]+ read=[0-9]+" "$log" | head -1)
  local summary
  summary=$(grep -oE "model-material summary slots=[0-9]+ resolved=[0-9]+" "$log" | head -1)
  local cdline
  cdline=$(grep -oE "model-cdtextures count=[0-9]+ \[[^]]*\]" "$log" | head -1)
  echo "$mdl rc=$rc"
  echo "  $declared"
  echo "  $cdline"
  echo "  $summary"
  # Every field is space-terminated, so strip the prefix and then cut at the
  # first space: `##*field=` alone would keep every field that follows.
  local declared_n resolved_n
  declared_n=${declared#*declared=}; declared_n=${declared_n%% *}
  resolved_n=${summary#*resolved=}; resolved_n=${resolved_n%% *}
  assert_eq "$mdl declared slots" "${declared_n:-<none>}" "$slots"
  assert_eq "$mdl resolved slots" "${resolved_n:-<none>}" "$resolved"
  if [ -n "$cd" ]; then
    if [ "${cdline#*\[}" != "${cdline}" ] && printf '%s' "$cdline" | grep -qF "[$cd]"; then
      echo "  OK   $mdl resolved bare stems against [$cd]"
    else
      echo "  FAIL $mdl did not print the CD directory [$cd] (got: ${cdline:-<none>})"; fail=1
    fi
  fi
}
# 19 of medic's 21 slots resolve: the two that do not are the eyeball materials,
# whose VMTs carry no `$basetexture` because Source renders eyes procedurally.
# That is a reading, not a defect -- the assertion is on the exact pair.
check_model "models/player/medic.mdl"                    21 19 "models/player/medic/"
check_model "models/player/scout.mdl"                    17 15 "models/player/scout/"
check_model "models/props_gameplay/resupply_locker.mdl"   4  4 "models/props_gameplay/"
check_model "models/items/medkit_large.mdl"               1  1 "/models/items/"
check_model "models/props_gameplay/cap_point_base.mdl"    3  3 "models/props_gameplay/"

echo
echo "=== 2/4 the program survives the entity boundary and paints what it draws ==="
FRESH="$OUT/fresh-tick3001.bmp"
LEDGER="$OUT/ledger.tsv"
rm -f "$FRESH" "$LEDGER"
# The run is driven from python rather than from `timeout` because the
# instance-level counters exist only in the window title, and a shell `timeout`
# leaves no way to read them while the window is up. It polls the title of *its
# own* child process, so a stale window from another run cannot be mistaken for
# this one. It also has to read the title at a tick past the boundary: at the
# tick every other gate captures at, `entityModels` is 0 and layer 2 would be
# vacuous.
RUN_OUT=$("$PY" - "$EXE" 300 "$FRESH" "$LEDGER" "$TF" "$DEMO" "$CAPTURE_TICK" <<'PYEOF'
import ctypes
import ctypes.wintypes as wt
import re
import subprocess
import sys
import time

exe, timeout_s, bmp, ledger, tf, demo, tick = sys.argv[1:8]
proc = subprocess.Popen([exe, "--tf-root", tf, "--audio-device", "6", "--demo", demo,
                         "--capture-frame", bmp, "--capture-tick", tick,
                         "--dump-entity-materials", ledger])
u32 = ctypes.windll.user32


def title_of(pid):
    found = []

    @ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
    def cb(hwnd, _):
        owner = wt.DWORD()
        u32.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
        if owner.value != pid:
            return True
        n = u32.GetWindowTextLengthW(hwnd)
        if not n:
            return True
        buf = ctypes.create_unicode_buffer(n + 1)
        u32.GetWindowTextW(hwnd, buf, n + 1)
        if buf.value.startswith("TF2 Demo Player"):
            found.append(buf.value)
        return True

    u32.EnumWindows(cb, 0)
    return found[0] if found else ""


# The last title that carries the wiring counters, which is the last tick the
# program reached before it exited. `playback=` names the playback tick;
# `HUD[tick=` is the same number and `nettick=` is a different one, so the
# playback pair is what gets parsed.
last = ""
polls = 0
deadline = time.time() + float(timeout_s)
while True:
    text = title_of(proc.pid)
    if "entityMaterials=" in text:
        last = text
        polls += 1
    if proc.poll() is not None:
        break
    if time.time() > deadline:
        proc.kill()
        proc.wait()
        break
    time.sleep(0.1)
print("RC=" + str(proc.returncode))
print("TITLES_WITH_MATERIALS=" + str(polls))
print("TITLE=" + last)
for key in ("playback", "entityModels", "entityMaterials", "entityMaterialRanges",
            "entityMaterialModels", "entity"):
    m = re.search(key + "=([^ ]+)", last)
    print(key.upper() + "=" + (m.group(1) if m else ""))
PYEOF
)
echo "$RUN_OUT"
RUN_RC=$(printf '%s\n' "$RUN_OUT" | grep -oE '^RC=[0-9-]+' | head -1)
RUN_RC=${RUN_RC#RC=}
# rc 0 is "reached the tick, wrote the frame, left on its own". 124 is the
# internal timeout, 16 a capture failure, and 0xC0000409 (as a negative int, or
# -1073740791) the fast-fail this gate was written after.
assert_eq "the run reached tick $((CAPTURE_TICK + 1)) and exited on its own" "${RUN_RC:-<none>}" "0"
if [ ! -s "$FRESH" ]; then
  echo "  FAIL no frame came back from the run at tick $((CAPTURE_TICK + 1))"; fail=1
fi

# Playback tick, from the title the program last published.
TITLE_TICK=$(printf '%s\n' "$RUN_OUT" | grep -oE '^PLAYBACK=[0-9]+/[0-9]+' | head -1)
TITLE_TICK=${TITLE_TICK#PLAYBACK=}
TITLE_TICK=${TITLE_TICK%%/*}
if [ -n "$TITLE_TICK" ] && [ "$TITLE_TICK" -ge "$CAPTURE_TICK" ] 2>/dev/null; then
  echo "  OK   the title was read at playback tick $TITLE_TICK (>= $CAPTURE_TICK)"
else
  echo "  FAIL the title was never read past the capture tick (got: ${TITLE_TICK:-<none>})"; fail=1
fi

# The instance-level wiring count. `N/M` is textured instances over instances
# drawn, so it is asserted as an equality and a lower bound rather than as a
# number: the count moves with the tick (83/83 at tick 79, 95/95 at tick 3001)
# and pinning either would pin the snapshot, not the wiring.
MAT=$(printf '%s\n' "$RUN_OUT" | grep -oE '^ENTITYMATERIALS=[0-9]+/[0-9]+' | head -1)
MAT=${MAT#ENTITYMATERIALS=}
MAT_TEX=${MAT%%/*}
MAT_ALL=${MAT##*/}
echo "entityMaterials=$MAT"
if [ "${MAT_ALL:-0}" -gt 0 ] 2>/dev/null && [ "${MAT_TEX:-0}" = "${MAT_ALL:-x}" ]; then
  echo "  OK   every one of the $MAT_ALL drawn instances had a model SRV bound"
else
  echo "  FAIL entityMaterials=$MAT is not N/N with N > 0"; fail=1
fi
RANGES=$(printf '%s\n' "$RUN_OUT" | grep -oE '^ENTITYMATERIALRANGES=[0-9]+/[0-9]+' | head -1)
RANGES=${RANGES#ENTITYMATERIALRANGES=}
echo "entityMaterialRanges=$RANGES"
if [ "${RANGES%%/*}" != "" ] && [ "${RANGES%%/*}" = "${RANGES##*/}" ] && [ "${RANGES##*/}" -gt 0 ] 2>/dev/null; then
  echo "  OK   every one of the ${RANGES##*/} draw ranges carried a texture view"
else
  echo "  FAIL entityMaterialRanges=$RANGES is not N/N with N > 0"; fail=1
fi

# The per-model account behind those counters: one row per prepared model, and a
# summary line. 59 models is what this demo's world resolves to; the assertion
# that matters is that all three numbers agree.
if [ -s "$LEDGER" ]; then
  LEDGER_SUMMARY=$(grep -oE '^# summary models=[0-9]+ resolved=[0-9]+ uploaded=[0-9]+' "$LEDGER" | head -1)
  echo "$LEDGER_SUMMARY"
  LEDGER_ROWS=$(grep -c -v '^#' "$LEDGER")
  LEDGER_ROWS=$((LEDGER_ROWS - 1))
  LEDGER_MODELS=${LEDGER_SUMMARY#*models=}; LEDGER_MODELS=${LEDGER_MODELS%% *}
  LEDGER_RESOLVED=${LEDGER_SUMMARY#*resolved=}; LEDGER_RESOLVED=${LEDGER_RESOLVED%% *}
  LEDGER_UPLOADED=${LEDGER_SUMMARY#*uploaded=}
  assert_eq "ledger rows == models" "$LEDGER_ROWS" "$LEDGER_MODELS"
  assert_eq "every model resolved a material" "$LEDGER_RESOLVED" "$LEDGER_MODELS"
  assert_eq "every resolved model uploaded its texture" "$LEDGER_UPLOADED" "$LEDGER_MODELS"
  # And the models figure is the same one the title counts meshes for.
  TITLE_MODELS=$(printf '%s\n' "$RUN_OUT" | grep -oE '^ENTITYMODELS=[0-9]+' | head -1)
  echo "  entityModels=${TITLE_MODELS#ENTITYMODELS=}"
  LEDGER_MODELS_TITLE=$(printf '%s\n' "$RUN_OUT" | grep -oE '^ENTITYMATERIALMODELS=[0-9]+/[0-9]+' | head -1)
  LEDGER_MODELS_TITLE=${LEDGER_MODELS_TITLE#ENTITYMATERIALMODELS=}
  assert_eq "title materialModels denominator == ledger models" "${LEDGER_MODELS_TITLE##*/}" "$LEDGER_MODELS"
else
  echo "  FAIL no paint ledger was written ($LEDGER)"; fail=1
fi

echo
echo "=== 3/4 the model block is no longer painted with the world atlas ==="
# The comparison is restricted to the block on purpose. A whole-frame diff would
# also move when the camera, the world or the HUD changes, so it could not say
# which of those happened; the block diff only moves when the thing the model is
# painted with moves.
diff_block() { # diff_block <a.bmp> <b.bmp> -> "blockDiff frameDiff firstA firstB"
  "$PY" - "$1" "$2" "$BLOCK_X0" "$BLOCK_Y0" "$BLOCK_X1" "$BLOCK_Y1" <<'PYEOF'
import struct
import sys

a_path, b_path, x0, y0, x1, y1 = sys.argv[1:7]
x0, y0, x1, y1 = int(x0), int(y0), int(x1), int(y1)


def load(path):
    data = open(path, "rb").read()
    if data[:2] != b"BM":
        raise SystemExit(path + " is not a BMP")
    offset, = struct.unpack_from("<I", data, 10)
    width, height = struct.unpack_from("<ii", data, 18)
    bits, = struct.unpack_from("<H", data, 28)
    if bits != 24:
        raise SystemExit(path + " is not 24bpp")
    bottom_up = height > 0
    height = abs(height)
    stride = (width * 3 + 3) // 4 * 4
    rows = []
    for y in range(height):
        base = offset + y * stride
        rows.append([(data[base + x * 3 + 2], data[base + x * 3 + 1], data[base + x * 3])
                     for x in range(width)])
    if bottom_up:
        rows.reverse()
    return width, height, rows


wa, ha, a = load(a_path)
wb, hb, b = load(b_path)
if (wa, ha) != (wb, hb):
    raise SystemExit("size mismatch")
frame_diff = 0
block_diff = 0
first = None
for y in range(ha):
    for x in range(wa):
        if a[y][x] != b[y][x]:
            frame_diff += 1
            if x0 <= x <= x1 and y0 <= y <= y1:
                block_diff += 1
                if first is None:
                    first = (x, y, a[y][x], b[y][x])
print("blockDiff=%d frameDiff=%d first=%s" % (
    block_diff, frame_diff, ("%d,%d %s->%s" % first) if first else "-"))
PYEOF
}

FRESH_DIFF=$(diff_block "$WITNESS" "$FRESH")
echo "witness vs fresh: $FRESH_DIFF"
BLOCK_DIFF=${FRESH_DIFF#*blockDiff=}
BLOCK_DIFF=${BLOCK_DIFF%% *}
FRAME_DIFF=${FRESH_DIFF##*frameDiff=}
FRAME_DIFF=${FRAME_DIFF%% *}
echo "block pixels changed: ${BLOCK_DIFF:-?}, whole frame: ${FRAME_DIFF:-?}"
# 20 pixels in one 6x5 block is the measured signal. The floor is half of it so
# that a future instance landing half-outside the block does not read as red,
# while "the block still holds the atlas value" (0 changed pixels) does.
if [ "${BLOCK_DIFF:-0}" -ge 10 ] 2>/dev/null; then
  echo "  OK   the model block no longer holds the atlas-painted pixels"
else
  echo "  FAIL the model block still holds the atlas-painted pixels (${BLOCK_DIFF:-?} changed)"; fail=1
fi

echo
if [ "$MUTATION" -eq 1 ]; then
  # Put the atlas-painted pixels back and require layer 3 to notice. This is the
  # input half of the perturbation; the source half (material resolution returns
  # no SRV) is mutate.sh's m12.
  echo "=== mutation: the fresh capture is replaced by the atlas-painted witness ==="
  MUT="$OUT/mutated-tick3001.bmp"
  cp "$WITNESS" "$MUT"
  MUT_DIFF=$(diff_block "$WITNESS" "$MUT")
  echo "witness vs mutated: $MUT_DIFF"
  MUT_BLOCK=${MUT_DIFF#*blockDiff=}
  MUT_BLOCK=${MUT_BLOCK%% *}
  if [ "${MUT_BLOCK:-x}" = "0" ] && [ -n "${FRESH_DIFF:-}" ] && [ "${FRESH_DIFF#*blockDiff=0}" = "$FRESH_DIFF" ]; then
    echo "MUTATION-CAUGHT=PASS (layer 3 separates the atlas pixels from the model's own)"
    exit 0
  fi
  echo "MUTATION-CAUGHT=FAIL (layer 3 read ${MUT_BLOCK:-?} changed pixels on the atlas-painted input)"
  exit 1
fi

if [ "$fail" -eq 0 ]; then
  echo "ENTITY-MATERIAL=PASS"
else
  echo "ENTITY-MATERIAL=FAIL"
fi
exit "$fail"
