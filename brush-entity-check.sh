#!/usr/bin/env bash
# brush-entity-check.sh -- N09 (2026-11-23): what the refused brush models are, and
# whether a demo can say they move.
#
# WHY THIS EXISTS
#   docs/UNFINISHED-AUDIT-2026-11-22.md §2.13: the product refuses to draw every brush
#   model whose entity class is not func_brush/func_wall/func_lod
#   (brushClassIsStatic in native/src/bsp_map.cpp) because the vertices are baked at the
#   compile-time pose. The audit's complaint was that the refusal is a claim nobody had
#   measured: "skippedMoving=84" reads the same whether those are 84 doors or 84
#   respawn-room volumes.
#
#   The audit prescribed the probe first, and native/tools/brush_entity_probe.cpp is it.
#   Part 1 runs it. Its answer on every demo the project keeps is candidates=0: no demo
#   networks a "*N" brush model through m_nModelIndex, so the DEMO cannot say which
#   refused brush models move. That negative is the answer to the audit's question and is
#   worth pinning -- the day a recording starts carrying such an entity, this flips.
#
#   Part 2 reads the only observable left: the product's own ledger, one
#   "gpu: world brush model refused ..." line per refused model, and checks it against
#   the counters it exists to explain. pl_upward_f12 today: total=173 drawn=100
#   skippedMoving=73 skippedUnknown=0, facesDrawn=1182 facesSkipped=476 brushFaces=1658.
#   The class mix is the finding -- func_door 14 (the refusal is right: a door moves),
#   plus 59 models whose classes cannot move at all (func_nobuild 17, trigger_multiple 10,
#   func_respawnroomvisualizer 8, func_regenerate 8, func_respawnroom 4,
#   func_physbox_multiplayer 4, trigger_hurt 3, func_nogrenades 2, trigger_once 1,
#   trigger_capture_area 1, dispenser_touch_trigger 1).
#
#   This gate does NOT touch the whitelist. Adding those 59 classes would add 476 faces
#   to every frame of every map -- an owner-visible rendering change, not a probe's call.
#   The gate pins the arithmetic and the class mix so that change, if it is ever made,
#   is measured rather than guessed.
#
# Usage: bash brush-entity-check.sh
# Exit:  0 = every assertion held; 1 = not.
set -uo pipefail
export TF2_NATIVE_WINDOW_SIZE="${TF2_NATIVE_WINDOW_SIZE:-1280x720}"
cd "$(dirname "$0")"
export LC_ALL=C

EXE=native/build-nmake/tf2_demo_native.exe
PROBE=native/build-nmake/brush_entity_probe.exe
TF="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
DEMO="D:/TF2_Demo_Player/testdata/demos/a9630c6557468b24f0b8c2e7dab38ed8_match-20261030-2145-pl_upward_f12.dem"
DEMO2="E:/demo测试/cp_sunshine/STV/SUNSHINE.dem"
OUT=.scratch/brush-entity
RUN_TIMEOUT=300

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

# LINE-scoped field read: many trace lines carry the same key (faces= is on the refusal
# rows and on the summary, for instance).
sfield() { # <line> <field>
  printf '%s\n' "$1" | grep -oE "$2=[^[:space:]]*" | tail -1 | cut -d= -f2
}

echo "=== 0/4 build (probe + product) ==="
if bash build-target.sh brush_entity_probe tf2_demo_native > "$OUT/build.txt" 2>&1; then
  echo "  OK build"
else
  echo "  FAIL build (see $OUT/build.txt)"; tail -30 "$OUT/build.txt"; exit 1
fi
for path in "$EXE" "$PROBE" "$TF" "$DEMO" "$DEMO2"; do
  [ -e "$path" ] || { echo "FATAL: missing input: $path"; exit 1; }
done

run_probe() { # <name> [args...]
  local name="$1"; shift
  "$PROBE" "$@" > "$OUT/$name.txt" 2>&1
  echo "$?"
}

summary() { grep -a "brush_entity_probe summary" "$1" | tail -1; }

echo "=== 1/4 the probe on pl_upward_f12 (the demo side) ==="
rc=$(run_probe upward "$DEMO")
assert_eq "probe rc" "$rc" "0"
line=$(summary "$OUT/upward.txt")
echo "  ..   $line"
assert_eq "probe map" "$(sfield "$line" map)" "pl_upward_f12"
assert_num_gt "snapshots asked" "$(sfield "$line" snapshots)" "50"
# The answer to the audit's question, today: the demo networks no brush entity at all.
assert_eq "candidates (networked *N brush entities)" "$(sfield "$line" candidates)" "0"
assert_eq "verdict" "$(sfield "$line" verdict)" "no-brush-entities"
assert_eq "moved" "$(sfield "$line" moved)" "0"

echo "=== 2/4 the probe's own controls ==="
rc=$(run_probe upward2 "$DEMO")
assert_eq "second run rc" "$rc" "0"
if cmp -s "$OUT/upward.txt" "$OUT/upward2.txt"; then
  echo "  OK   the probe is deterministic"
else
  echo "  FAIL the probe printed a different answer twice"; fail=1
fi
rc=$(run_probe second "$DEMO2")
assert_eq "second demo rc" "$rc" "0"
line=$(summary "$OUT/second.txt")
echo "  ..   $line"
assert_eq "second demo map" "$(sfield "$line" map)" "cp_sunshine"
assert_eq "second demo candidates" "$(sfield "$line" candidates)" "0"
rc=$(run_probe ticks1200 --ticks 1200 "$DEMO")
assert_eq "--ticks rc" "$rc" "0"
line=$(summary "$OUT/ticks1200.txt")
assert_eq "--ticks asks one snapshot" "$(sfield "$line" snapshots)" "1"
if grep -aq "brush_entity_probe query tick=1200" "$OUT/ticks1200.txt"; then
  echo "  OK   --ticks named the tick it asked"
else
  echo "  FAIL --ticks did not print its query row"; fail=1
fi
"$PROBE" > /dev/null 2>&1; assert_eq "no demo is a usage error" "$?" "2"
"$PROBE" --ticks abc "$DEMO" > /dev/null 2>&1; assert_eq "a bad tick is rejected" "$?" "2"

echo "=== 3/4 the product's refusal ledger (the map side) ==="
TF2_NATIVE_TRACE="$(win "$OUT/ledger.trace")" timeout "$RUN_TIMEOUT" "$EXE" \
  --tf-root "$TF" --demo "$DEMO" --hidden-window --audio-disabled --playback-speed 8 \
  --settings-file "$(win "$OUT/ledger.ini")" --ui-settings-file "$(win "$OUT/ledger-ui.ini")" \
  --capture-tick 1200 --capture-frame "$(win "$OUT/ledger.bmp")" > "$OUT/ledger.log" 2>&1
assert_eq "product rc" "$?" "0"
total_line=$(grep -a "gpu: world brush models total=" "$OUT/ledger.trace" | tail -1)
echo "  ..   $total_line"
total=$(sfield "$total_line" total)
drawn=$(sfield "$total_line" drawn)
skipped_moving=$(sfield "$total_line" skippedMoving)
skipped_unknown=$(sfield "$total_line" skippedUnknown)
faces_drawn=$(sfield "$total_line" facesDrawn)
faces_skipped=$(sfield "$total_line" facesSkipped)
brush_faces=$(sfield "$total_line" brushFaces)
refused=$(grep -ac "gpu: world brush model refused " "$OUT/ledger.trace")
assert_num_gt "refused models" "$refused" "0"
# The ledger is worth nothing unless it explains the counters exactly.
assert_eq "refused rows == skippedMoving + skippedUnknown" "$refused" "$((skipped_moving + skipped_unknown))"
assert_eq "drawn + refused == total" "$((drawn + refused))" "$total"
assert_eq "facesDrawn + facesSkipped == brushFaces" "$((faces_drawn + faces_skipped))" "$brush_faces"
assert_num_gt "faces the refusal leaves out" "$faces_skipped" "0"
bad=$(grep -a "gpu: world brush model refused " "$OUT/ledger.trace" \
  | grep -avc 'class=[a-zA-Z_0-9]* model=\*[0-9]* entity=[0-9]* firstface=[0-9]* faces=[0-9]* reason=moving')
assert_eq "rows missing a field" "$bad" "0"

echo "=== 4/4 the class mix (what the refusal is actually refusing) ==="
grep -ao "refused class=[^ ]*" "$OUT/ledger.trace" | sed 's/refused class=//' \
  | sort | uniq -c | sort -rn | sed 's/^/  ..   /'
doors=$(grep -a "gpu: world brush model refused " "$OUT/ledger.trace" | grep -ac "class=func_door ")
assert_num_gt "func_door refusals (these really move)" "$doors" "0"
not_doors=$(grep -a "gpu: world brush model refused " "$OUT/ledger.trace" | grep -avc "class=func_door ")
assert_num_gt "refusals outside func_door (the whitelist over-reaches)" "$not_doors" "0"
if grep -aq "gpu: world brush origin applied" "$OUT/ledger.trace"; then
  echo "  OK   the drawn brush models got their origin back"
else
  echo "  FAIL no brush-origin line; drawn brushes would sit at the model origin"; fail=1
fi

echo
if [ "$fail" = 0 ]; then
  echo "BRUSH-ENTITY=PASS"
else
  echo "BRUSH-ENTITY=FAIL"
fi
exit "$fail"
