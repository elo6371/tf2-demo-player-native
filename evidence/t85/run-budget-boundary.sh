#!/usr/bin/env bash
# T85 P0 acceptance: the budget BOUNDARY as a measurement, not as an assumption.
# Patch layer B's capacity 96 -> 24, rebuild, run the pinned outline fixture, and
# publish what the product says about itself at the smaller bound: capacity,
# resident, evictions, capacityRefusals, instances missingMesh, draw drops.
# Then restore the source BYTE-EXACTLY (cmp), rebuild and publish the same readings
# at 96. The restore is the point: a boundary probe that leaves the tree mutated is
# worse than no probe.
set -uo pipefail
cd "$(dirname "$0")/../.."
TF="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
DEMO="D:/TF2_Demo_Player/testdata/demos/bb841c6d379ff7c40d0c8baf99f59d8d_matcha-20260927-1347-cp_snakewater_final1.dem"
TICK=64250
SPEED=8
CAM="5699 -1611 150 14.4 -143.3"
OUT="evidence/t85/budget-boundary"
SRC="native/src/native_renderer_entity.cpp"
EXE="native/build-nmake/tf2_demo_native.exe"
mkdir -p "$OUT"
BK="$OUT/native_renderer_entity.cpp.bak"
cp "$SRC" "$BK"
echo "src before sha256=$(sha256sum "$SRC" | cut -d' ' -f1)"
restore() {
  cp "$BK" "$SRC"
  if cmp -s "$BK" "$SRC"; then echo "restore: byte-exact (sha256=$(sha256sum "$SRC" | cut -d' ' -f1))"; else echo "restore: FAILED"; fi
}
trap restore EXIT

run_one() { # run_one <tag>
  local tag="$1"
  rm -f "$OUT/$tag.bmp" "$OUT/$tag.raw.trace" "$OUT/$tag.log" "$OUT/$tag.trace"
  TF2_NATIVE_TRACE="$OUT/$tag.raw.trace" timeout 1800 "$EXE" \
    --tf-root "$TF" --hidden-window --audio-disabled --demo "$DEMO" \
    --capture-tick "$TICK" --playback-speed "$SPEED" --capture-frame "$OUT/$tag.bmp" \
    --outline-vertex-extent --debug-camera $CAM --no-player-animation \
    --outline on --player-weapon-attach on \
    > "$OUT/$tag.log" 2>&1
  local rc=$?
  { grep -aE "gpu: |budget: |overlay: |scene: |capture: |attach: " "$OUT/$tag.raw.trace" || true; } > "$OUT/$tag.trace"
  rm -f "$OUT/$tag.raw.trace"
  echo "$tag rc=$rc"
}
report() { # report <tag>
  local tag="$1" t="$OUT/$tag.trace"
  echo "---- $tag ----"
  grep -a "budget: meshTable" "$t" | tail -1
  grep -a "budget: instances" "$t" | tail -1
  grep -a "budget: draw" "$t" | tail -1
  grep -a "budget: prepare" "$t" | tail -1
  grep -a "gpu: overlay meshes prepared=" "$t" | tail -1
  echo "  instances missingMesh = $(grep -a "budget: instances" "$t" | grep -ao "missingMesh:[0-9]*" | tail -1 | cut -d: -f2)"
  echo "  attach refused        = $(grep -a -c "attach: mesh result=refused" "$t" || true)"
}

# --- capacity 24 ------------------------------------------------------------
python - "$SRC" <<'PY' || { echo "PATCH-INVALID: anchor not found exactly once"; exit 3; }
import io,sys
p=sys.argv[1]; s=io.open(p,encoding="utf-8").read()
old="constexpr std::size_t kEntityModelMeshCapacity = 96;"
new="constexpr std::size_t kEntityModelMeshCapacity = 24;  /* boundary probe: the budget, moved */"
n=s.count(old)
if n!=1:
    print("anchor count=%d" % n); sys.exit(1)
io.open(p,"w",encoding="utf-8",newline="").write(s.replace(old,new))
PY
echo "cap24 src sha256=$(sha256sum "$SRC" | cut -d' ' -f1)"
bash build-target.sh tf2_demo_native > "$OUT/build-cap24.log" 2>&1 || { echo "BUILD FAILED (cap24)"; tail -20 "$OUT/build-cap24.log"; exit 4; }
echo "cap24 exe sha256=$(sha256sum "$EXE" | cut -d' ' -f1)"
run_one cap24
report cap24

# --- restore ---------------------------------------------------------------
restore
trap - EXIT
cmp -s "$BK" "$SRC" || { echo "RESTORE FAILED: refusing to continue"; exit 5; }
bash build-target.sh tf2_demo_native > "$OUT/build-cap96.log" 2>&1 || { echo "BUILD FAILED (restore)"; tail -20 "$OUT/build-cap96.log"; exit 4; }
echo "cap96 exe sha256=$(sha256sum "$EXE" | cut -d' ' -f1)"
run_one cap96
report cap96
