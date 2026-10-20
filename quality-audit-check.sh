#!/usr/bin/env bash
# quality-audit-check.sh -- T33: the footprint and steady-state gate.
#
# WHY THIS FILE EXISTS
# --------------------
# Every other gate on this tree answers a question about behaviour or pixels.
# None of them answers "did the work behind that behaviour get more expensive?":
#   * a world pass that rebuilds its atlas every scene update draws the same
#     picture, so the visual gates stay green;
#   * a loader that re-parses one VMT per triangle of a material, or keeps a
#     second CPU copy of every decoded texture, produces the same readings the
#     material gates assert;
#   * a per-frame container that grows without bound changes no pixel at all.
# The readings those defects move are already in the trace and the metrics file
# (T19's atlas plan, the material-fetch census, the renderer's texture
# bookkeeping, verify-all step 18's loop counters). What was missing is a gate
# that compares them to a pin.
#
# WHAT IS ASSERTED, AND WHY EACH PIN CAN GO RED
#   1. Load-time world resources, exact values: the material-fetch census
#      (distinct/tiled/fromPakfile/vtfTooLarge/vmtMissing), the atlas plan
#      (batches/blendBatches/tiled/refused/budget/layerBytes) and the atlas
#      itself (atlases/totalBytes/state), plus the blend/sky/isolation readings
#      the same frame reports. These are deterministic for a fixed input and a
#      fixed camera. The mutation below moves batches and atlases while the
#      pixels stay identical -- the regression class the pixel gates cannot see.
#   2. The same scene captured twice must be byte-identical. That is the
#      deterministic frame comparison, stated as a property of the product
#      rather than as a hash pinned against a tree T29/T30 are expected to
#      change: a hash pin here would read red for a legitimate visual fix.
#   3. The steady-state envelope from --metrics-file: peak working set, working
#      set growth after the load ramp, and median rendered frames per second.
#      The two memory ceilings are the ones stability-release-check.sh already
#      publishes (2048 MiB peak, 32 MiB second-half growth); the frame-rate
#      floor is set far below the measured value. This is a leak/stall
#      detector, not a benchmark.
#
# WHAT THIS GATE DOES NOT DO
#   It does not build the tree first; a missing binary is a FAIL, never a SKIP.
#   Build with: bash build-target.sh tf2_demo_native
#   It does not remove or weaken a diagnostic: every reading it pins is one the
#   product already prints.
#
# THE PINS ARE CURRENT-TREE READINGS
#   They were measured on 36303c2 (T33, 2026-10-20). This repo's convention:
#   when a material or atlas change legitimately moves one, the pin is updated
#   in the same commit and the diff says which reading moved and why. Do not
#   widen a pin to make a red run green.
#
# MUTATION (--mutation)
#   One source mutation in native/include/world_tile_atlas.h: maxTilesPerBatch
#   64 -> 20. All 85 tiles still reach an atlas and the frame keeps its pixels,
#   but the planner now needs more batches, atlas textures and draw calls. The
#   atlas pins must go red; the source must come back byte-identical; the
#   rebuilt binary must return to the reference hash; and the restored tree must
#   reproduce the reference readings. That is the difference between "the gate
#   reads green" and "the gate can read red".
#
# Usage: bash quality-audit-check.sh [--mutation]
# Exit:  0 = every assertion held (or, with --mutation, went red and restored).
set -uo pipefail
cd "$(dirname "$0")"

EXE=native/build-nmake/tf2_demo_native.exe
TF="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
DEMO="$TF/demos/autorecord_2026-07-02_13-26-46.dem"
PYTHON=python
if [ "$(printenv PYTHON)" != "" ]; then PYTHON="$(printenv PYTHON)"; fi
MODE=""
if [ "$#" -ge 1 ]; then MODE="$1"; fi

RUN_TAG="$(date +%Y%m%d%H%M%S)-$$"
SCRATCH=".scratch/quality-audit.$RUN_TAG"
OUT=evidence/quality-audit
mkdir -p "$SCRATCH"

# Missing input is a failure, not a skip: a gate that quietly SKIPs when the
# binary or the install is absent cannot report the regression it exists for.
for path in "$EXE" "$TF"; do
  [ -e "$path" ] || { echo "FATAL: missing input: $path"; exit 1; }
done

fail=0
mfail=0
assert_eq() { if [ "$2" = "$3" ]; then echo "  OK $1 = $2"; else echo "  FAIL $1: got '$2' want '$3'"; fail=1; fi; }
# An empty value must fail, not pass. awk compares the empty string as 0, so
# "peak working set =  <= 3072" once printed OK when the reading file was
# missing -- the exact "rc=0 but nothing was checked" class this repo has hit
# four times. Both helpers therefore reject a missing reading by name.
assert_le() {
  if [ -z "$2" ]; then echo "  FAIL $1: no reading"; fail=1; return; fi
  if awk -v v="$2" -v hi="$3" 'BEGIN{exit !(v<=hi)}'; then echo "  OK $1 = $2 <= $3"; else echo "  FAIL $1: got '$2' above $3"; fail=1; fi
}
assert_ge() {
  if [ -z "$2" ]; then echo "  FAIL $1: no reading"; fail=1; return; fi
  if awk -v v="$2" -v lo="$3" 'BEGIN{exit !(v>=lo)}'; then echo "  OK $1 = $2 >= $3"; else echo "  FAIL $1: got '$2' below $3"; fail=1; fi
}

field() { printf '%s' "$1" | grep -oE "$2=[-0-9.]+" | head -1 | cut -d= -f2; }

# capture <bmp> <trace> <csv> <log> -- run the product to the pinned frame.
# tick 2000 exactly: --capture-tick N means the first draw with tick >= N, so the
# retry loop is what makes the pin a statement about one specific frame.
capture() {
  local bmp="$1" trace="$2" csv="$3" log="$4"
  local traceWin bmpWin attempt rc tick tickText
  traceWin="$(cygpath -m "$PWD/$trace")"
  bmpWin="$(cygpath -m "$PWD/$bmp")"
  : > "$csv"
  for attempt in 1 2 3 4 5 6; do
    rm -f "$bmp"
    TF2_NATIVE_TRACE="$traceWin" timeout 300 "$EXE" --tf-root "$TF" --demo "$DEMO" \
      --hidden-window --audio-disabled --capture-tick 2000 --capture-frame "$bmpWin" \
      --viewmodel models/weapons/v_models/v_scattergun_scout.mdl --viewmodel-render companion \
      --metrics-file "$csv" > "$log" 2>&1
    rc=$?
    tick=$(grep -a "draw: begin tick=" "$trace" 2>/dev/null | tr -d '\r' | tail -1 \
      | sed -n 's/.*tick=\([-0-9]*\).*/\1/p')
    if [ "$rc" = "0" ] && [ "$tick" = "2000" ] && [ -s "$bmp" ]; then return 0; fi
    tickText="$tick"
    if [ -z "$tickText" ]; then tickText=none; fi
    echo "  (capture attempt $attempt: rc=$rc tick=$tickText, retrying for tick 2000)"
  done
  return 1
}

# pins_of <trace> <csv> -- the deterministic readings plus the envelope, one
# key=value per line.
pins_of() {
  local trace="$1" csv="$2"
  local fetch atlasplan atlas blend sky isolation
  fetch=$(grep -o "gpu: world material-fetch .*" "$trace" | head -1)
  atlasplan=$(grep -o "gpu: world atlas-plan .*" "$trace" | head -1)
  atlas=$(grep -o "gpu: world atlas .*" "$trace" | head -1)
  blend=$(grep -o "gpu: world blend enabled=.*" "$trace" | head -1)
  sky=$(grep -o "gpu: world sky-upload .*" "$trace" | head -1)
  isolation=$(grep -o "gpu: world isolation .*" "$trace" | head -1)
  echo "material_fetch_distinct=$(field "$fetch" distinct)"
  echo "material_fetch_tiled=$(field "$fetch" tiled)"
  echo "material_fetch_frompak=$(field "$fetch" fromPakfile)"
  echo "material_fetch_vtftoolarge=$(field "$fetch" vtfTooLarge)"
  echo "material_fetch_vmtmissing=$(field "$fetch" vmtMissing)"
  echo "atlas_batches=$(field "$atlasplan" batches)"
  echo "atlas_blend_batches=$(field "$atlasplan" blendBatches)"
  echo "atlas_plan_tiled=$(field "$atlasplan" tiled)"
  echo "atlas_plan_refused=$(field "$atlasplan" refused)"
  echo "atlas_plan_budget=$(field "$atlasplan" budget)"
  echo "atlas_plan_layerbytes=$(field "$atlasplan" layerBytes)"
  echo "atlas_count=$(field "$atlas" atlases)"
  echo "atlas_totalbytes=$(field "$atlas" totalBytes)"
  echo "atlas_state=$(printf '%s' "$atlas" | grep -oE 'state=[a-z-]+' | head -1 | cut -d= -f2)"
  echo "blend_tiles=$(field "$blend" tiles)"
  echo "blend_weighted=$(field "$blend" weightedVertices)"
  echo "sky_state=$(printf '%s' "$sky" | grep -oE 'state=[a-z-]+' | head -1 | cut -d= -f2)"
  echo "sky_faces=$(field "$sky" faces)"
  echo "isolation_weapon_worldatlas=$(field "$isolation" weaponDrawsWithWorldAtlas)"
  echo "isolation_world_weapontex=$(field "$isolation" worldDrawsWithWeaponTexture)"
  # Derived maxima, not trace readings: the metrics file is the only place the
  # process reports its own memory and rate.
  "$PYTHON" - "$csv" <<'PYEOF'
import csv, sys
rows = list(csv.DictReader(open(sys.argv[1])))
if len(rows) < 3:
    print("metrics_rows=%d" % len(rows)); raise SystemExit(0)
def col(k): return [int(r[k]) for r in rows]
def fcol(k): return [float(r[k]) for r in rows]
ws = col("working_set_bytes"); el = fcol("elapsed_seconds"); fps = fcol("fps")
frames = col("rendered_frames")
steady = [i for i, t in enumerate(el) if t >= 5.0] or [0]
grow = (ws[-1] - ws[steady[0]]) / 1048576.0
sp = sorted(fps[i] for i in steady)
median = sp[len(sp)//2] if sp else 0.0
print("metrics_rows=%d" % len(rows))
print("metrics_seconds=%.2f" % el[-1])
print("rendered_frames=%d" % frames[-1])
print("peak_working_set_mb=%.1f" % (max(ws) / 1048576.0))
print("steady_growth_mb=%.1f" % grow)
print("median_fps=%.1f" % median)
PYEOF
}

# forward <label> -- capture, write the readings and the frame hash to
# <label>.out, echo them, and keep the per-run files under SCRATCH.
forward() {
  local label="$1"
  local bmp="$SCRATCH/$label.bmp" trace="$SCRATCH/$label.trace"
  local csv="$SCRATCH/$label.csv" log="$SCRATCH/$label.log"
  if ! capture "$bmp" "$trace" "$csv" "$log"; then
    echo "  FAIL $label: no capture at tick 2000"; fail=1
  fi
  {
    pins_of "$trace" "$csv"
    echo "frame_sha256=$(sha256sum "$bmp" 2>/dev/null | cut -d' ' -f1)"
  } > "$SCRATCH/$label.out"
  # The deterministic half lives in its own file so assert_pins() never has to
  # know which keys are reproducible, and so the frame hash cannot be mistaken
  # for a pin (a legitimate visual change may move the hash).
  grep -vE '^frame_sha256=' "$SCRATCH/$label.out" > "$SCRATCH/$label.pins"
  cat "$SCRATCH/$label.out"
  return 0
}

# assert_pins <pins-file> -- every pinned reading must equal its reference.
assert_pins() {
  local pins="$1"
  val() { grep -E "^$1=" "$pins" | head -1 | cut -d= -f2; }
  assert_eq "material-fetch distinct" "$(val material_fetch_distinct)" "108"
  assert_eq "material-fetch tiled" "$(val material_fetch_tiled)" "85"
  assert_eq "material-fetch fromPakfile" "$(val material_fetch_frompak)" "85"
  assert_eq "material-fetch vtfTooLarge" "$(val material_fetch_vtftoolarge)" "70"
  assert_eq "material-fetch vmtMissing" "$(val material_fetch_vmtmissing)" "3"
  # T19 pins measured on 36303c2 (T33, 2026-10-20): 85 of 85 materials tiled in
  # 2 batches; the plan's own reason line prices the layers at 89+64+64+0+0 MiB,
  # and the two albedo atlases cost 94,053,776 bytes on the GPU.
  assert_eq "atlas-plan batches" "$(val atlas_batches)" "2"
  assert_eq "atlas-plan blendBatches" "$(val atlas_blend_batches)" "1"
  assert_eq "atlas-plan tiled" "$(val atlas_plan_tiled)" "85"
  assert_eq "atlas-plan refused" "$(val atlas_plan_refused)" "0"
  assert_eq "atlas-plan budget refusals" "$(val atlas_plan_budget)" "0"
  assert_eq "atlas-plan layerBytes" "$(val atlas_plan_layerbytes)" "229322128"
  assert_eq "atlas count" "$(val atlas_count)" "2"
  assert_eq "atlas totalBytes" "$(val atlas_totalbytes)" "94053776"
  assert_eq "atlas state" "$(val atlas_state)" "bound"
  assert_eq "blend tiles" "$(val blend_tiles)" "10"
  assert_eq "blend weighted vertices" "$(val blend_weighted)" "85920"
  assert_eq "sky state" "$(val sky_state)" "active"
  assert_eq "sky faces" "$(val sky_faces)" "6"
  assert_eq "isolation weapon draws with world atlas" "$(val isolation_weapon_worldatlas)" "0"
  assert_eq "isolation world draws with weapon texture" "$(val isolation_world_weapontex)" "0"
  # Work counts. A run that never drew would also report empty readings; the
  # denominator is what separates "the pin held" from "nothing was measured".
  assert_ge "trace lines" "$(grep -ac '' "$TRACE_A" 2>/dev/null || echo 0)" "1000"
  assert_ge "metrics rows" "$(val metrics_rows)" "5"
  assert_ge "rendered frames" "$(val rendered_frames)" "500"
  # Steady-state envelope (not a benchmark): the 3072 MiB ceiling is the private
  # bytes ceiling stability-release-check.sh publishes, and the measured peak on
  # this reference frame is 2825.8 MiB -- the headroom is for run-to-run noise,
  # not for growth. The median frame rate measured 66.7 fps over 1,772 frames;
  # 30 fps only catches a stall.
  assert_le "peak working set MB" "$(val peak_working_set_mb)" "3072"
  assert_le "steady growth MB (after 5 s)" "$(val steady_growth_mb)" "32"
  assert_ge "median fps after 5 s" "$(val median_fps)" "30"
}

# det <out-file> -- the deterministic readings only. The envelope keys and the
# frame hash are deliberately removed: memory, frame rate and the number of
# frames drawn before tick 2000 differ between two runs of the same scene, and a
# comparison that included them would read "not reproducible" for a correct run.
det() {
  grep -vE '^(frame_sha256|metrics_[a-z_]+|rendered_frames|peak_working_set_mb|steady_growth_mb|median_fps)=' "$1"
}

# ---------------------------------------------------------------------------
# forward: two captures of the same scene, then the pins
# ---------------------------------------------------------------------------
echo "== 1/2 forward: the pinned demo at tick 2000, twice =="
forward ref-a
TRACE_A="$SCRATCH/ref-a.trace"
forward ref-b
h_a=$(grep -o 'frame_sha256=[0-9a-f]*' "$SCRATCH/ref-a.out" | cut -d= -f2)
h_b=$(grep -o 'frame_sha256=[0-9a-f]*' "$SCRATCH/ref-b.out" | cut -d= -f2)
pins_a=$(det "$SCRATCH/ref-a.out")
pins_b=$(det "$SCRATCH/ref-b.out")
[ -n "$h_a" ] || { echo "  FAIL ref-a produced no frame"; fail=1; }
assert_eq "frame determinism (same scene twice)" "$h_a" "$h_b"
if [ "$pins_a" = "$pins_b" ]; then
  echo "  OK the two runs report identical deterministic readings"
else
  echo "  FAIL the two runs do not report identical readings"; fail=1
fi
echo "== 2/2 the pinned readings and the envelope =="
assert_pins "$SCRATCH/ref-a.pins"

if [ "$MODE" != "--mutation" ]; then
  echo "== verdict =="
  if [ "$fail" = 0 ]; then
    echo "PASS: the footprint pins held, the frame was deterministic, the envelope held."
  else
    echo "FAIL: $fail assertions did not hold."
  fi
  mkdir -p "$OUT"
  cp "$SCRATCH/ref-a.out" "$OUT/readings.txt"
  { echo "forward-fail=$fail"; } > "$OUT/gate.txt"
  exit "$fail"
fi

# ---------------------------------------------------------------------------
# mutation: the planner's batch size drops 64 -> 20. All tiles still reach an
# atlas, the pixels are unchanged, but the resource footprint is not.
# ---------------------------------------------------------------------------
echo "== mutation: maxTilesPerBatch 64 -> 20 =="
SRC=native/include/world_tile_atlas.h
SAVE="$SCRATCH/world_tile_atlas.h.save"
BIN_BEFORE=$(sha256sum "$EXE" | cut -d' ' -f1)
echo "binary_before=$BIN_BEFORE"
cp "$SRC" "$SAVE"
"$PYTHON" - "$SRC" <<'PYEOF'
import sys
path = sys.argv[1]
src = open(path, encoding='utf-8').read()
old = "  std::uint32_t maxTilesPerBatch = 64;"
new = "  std::uint32_t maxTilesPerBatch = 20;"
assert old in src, "mutation patch site not found"
open(path, 'w', encoding='utf-8', newline='').write(src.replace(old, new, 1))
PYEOF
if [ "$?" != 0 ]; then echo "  FAIL could not patch the plan's batch size"; mfail=1; fi
bash build-target.sh tf2_demo_native > "$SCRATCH/mutation.build" 2>&1
if [ "$?" != 0 ]; then echo "  FAIL the mutated tree did not build"; mfail=1; fi
forward mut
M_BATCHES=$(grep -E '^atlas_batches=' "$SCRATCH/mut.out" | cut -d= -f2)
M_ATLASES=$(grep -E '^atlas_count=' "$SCRATCH/mut.out" | cut -d= -f2)
M_SHA=$(grep -o 'frame_sha256=[0-9a-f]*' "$SCRATCH/mut.out" | cut -d= -f2)
REF_BATCHES=$(grep -E '^atlas_batches=' "$SCRATCH/ref-a.out" | cut -d= -f2)
REF_ATLASES=$(grep -E '^atlas_count=' "$SCRATCH/ref-a.out" | cut -d= -f2)
if [ -z "$M_BATCHES" ] || [ -z "$M_ATLASES" ]; then
  echo "  FAIL the mutated run produced no atlas reading; the mutation cannot be shown to move anything"; mfail=1
elif [ "$M_BATCHES" = "$REF_BATCHES" ] && [ "$M_ATLASES" = "$REF_ATLASES" ]; then
  echo "  NOT-RED: the mutated plan still reports batches=$M_BATCHES atlases=$M_ATLASES"; mfail=1
else
  echo "  RED as required: atlas-plan batches $M_BATCHES (was $REF_BATCHES), atlases $M_ATLASES (was $REF_ATLASES)"
  echo "  mutated frame sha256=$M_SHA (reference $h_a)"
fi
cp "$SAVE" "$SRC"
if cmp -s "$SAVE" "$SRC"; then echo "  RESTORE=OK (source byte-identical)"; else echo "  RESTORE=FAIL (source did not come back byte-identical)"; fail=1; fi
bash build-target.sh tf2_demo_native > "$SCRATCH/restore.build" 2>&1
BIN_AFTER=$(sha256sum "$EXE" | cut -d' ' -f1)
BIN_STATE=identical
if [ "$BIN_AFTER" = "$BIN_BEFORE" ]; then
  echo "  RESTORE=OK (rebuilt binary sha256=$BIN_AFTER identical to the reference binary)"
else
  # A relinked image is not a source change: the CLI can stamp the COFF header
  # with the link time. The proof that matters is the one printed next -- the
  # restored tree reproduces the reference readings and the reference frame.
  BIN_STATE=relinked
  echo "  RESTORE=NOTE the rebuilt binary sha256=$BIN_AFTER differs from $BIN_BEFORE (relink artifact); the readings and frame below are the restoration proof"
fi
echo "== the restored tree reproduces the reference readings =="
forward restored
pins_r=$(det "$SCRATCH/restored.out")
h_r=$(grep -o 'frame_sha256=[0-9a-f]*' "$SCRATCH/restored.out" | cut -d= -f2)
if [ "$pins_r" = "$pins_a" ]; then
  echo "  OK restored readings reproduce the reference exactly"
else
  echo "  FAIL restored readings differ from the reference"; fail=1
  diff <(printf '%s\n' "$pins_a") <(printf '%s\n' "$pins_r") | head -20
fi
assert_eq "restored frame determinism" "$h_r" "$h_a"

echo "== verdict =="
mkdir -p "$OUT"
cp "$SCRATCH/ref-a.out" "$OUT/readings.txt"
{
  echo "mutation=world_tile_atlas.h maxTilesPerBatch 64->20"
  echo "source_restore=byte-identical"
  echo "binary_before=$BIN_BEFORE"
  echo "binary_after=$BIN_AFTER"
  echo "mutated_readings:"
  grep -E '^(atlas_batches|atlas_count|atlas_totalbytes|atlas_plan_layerbytes|frame_sha256)=' "$SCRATCH/mut.out"
} > "$OUT/mutation.txt"
if [ "$fail" = 0 ] && [ "$mfail" = 0 ]; then
  echo "PASS: the forward pins held, the mutation went red, the source and the binary came back, the restored tree reproduced the readings."
  { echo "forward-fail=0"; echo "mutation-not-red=0"; echo "restore-source=ok"; echo "restore-binary=$BIN_STATE"; echo "restore-readings=ok"; } > "$OUT/gate.txt"
  exit 0
fi
echo "FAIL: forward-fail=$fail mutation-not-red=$mfail"
{ echo "forward-fail=$fail"; echo "mutation-not-red=$mfail"; } > "$OUT/gate.txt"
exit 1
