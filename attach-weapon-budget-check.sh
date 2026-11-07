#!/usr/bin/env bash
# T85 P0 GATE: the three weapon world models that the frozen candidate refused with
# E_OUTOFMEMORY are ANCHORED BY NAME, and the frame that needs them is not short.
#
# Why this is a separate gate and not an outline-check assertion: outline-check runs
# the pinned outline fixture, which holds none of these weapons. The recording that
# reproduced the defect is the Highlander STV pl_upward_f12 demo used by the t62
# class-pixel matrix (evidence/t62-matrix/RUNS-2026-11-04.md section 26.3):
#   models/weapons/c_models/c_flamethrower/c_flamethrower.mdl
#   models/weapons/c_models/c_sapper/c_sapper_xmas.mdl
#   models/workshop_partner/weapons/c_models/c_shogun_warhorn/c_shogun_warhorn.mdl
# The frozen exe refused exactly those three with 0x8007000E; this gate requires
# that a build SUPPLY them by name, never refuse them, and account for every slot
# it spends doing it.
#
# NEGATIVE CONTROL (documented in docs/T85-BUDGET-DESIGN.md section 7): reverting
# the prepare pass's priority admission to first-come-first-served makes the named
# "supplied" assertions below go RED on this recording, because the attach then has
# to buy the same three models from a table already at its capacity. That is the
# proof that priority admission -- not a larger constant -- is the mechanism.
#
# Usage (always under the machine-wide lock):
#   bash /d/TF2_Native_Worktrees/_shared/hostlock.sh run <owner> bash attach-weapon-budget-check.sh
#
# DEBT ANCHOR: the last line is ATTACH-WEAPON-BUDGET=PASS|FAIL. ledger-recon's
# UNANCHORED list item 8.5-5 waits for that token, so it stays the last line and its
# spelling must not drift.
# Env: T85_ATTACH_EXE (default native/build-nmake/tf2_demo_native.exe)
#      T85_ATTACH_OUT (default evidence/t85/attach-budget)
set -uo pipefail
cd "$(dirname "$0")"

TF="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
# The testdata copy, not the Downloads one: no gate in this repo points at a user's
# Downloads folder (third-person-weapon-check.sh:85, player-body-appearance-check.sh:42,
# class-pixels-check.sh:102 and player-animation-check.sh:48 all read this directory).
# The two copies are byte-identical (sha256 below is the identity, not the path).
DEMO="D:/TF2_Demo_Player/testdata/demos/a9630c6557468b24f0b8c2e7dab38ed8_match-20261030-2145-pl_upward_f12.dem"
DEMO_SHA256="7e047aff1f7743915e355eac907a95dc3d981aa0fae5591b6b15346fb77dc4a3"
EXE="${T85_ATTACH_EXE:-native/build-nmake/tf2_demo_native.exe}"
OUT="${T85_ATTACH_OUT:-evidence/t85/attach-budget}"
SPEED=32
TICK=30001
# Copies, not the tracked originals: the product rewrites recentDemo0..7 in the
# --ui-settings-file on exit (native_ui.cpp:356 rememberDemo -> :137 persist).
# D-22: the machine must not be able to tell this gate ran. Both files live in a
# throwaway directory, never in evidence/ (a tracked/oracle path -- the launch-hygiene
# gate fails a launch that points its configuration at one).
ISO_DIR=${T85_ATTACH_ISO_DIR:-$(mktemp -d)}
SETTINGS="$ISO_DIR/settings.ini"
UI="$ISO_DIR/ui.ini"
: > "$SETTINGS"
: > "$UI"
# TICK DOMAIN, stated because it is easy to over-claim: --capture-tick is NOT a seek
# (main.cpp:9015-9019 clamps the cursor back after playback passes the tick). Every
# "supplied" line below is a PLAYBACK-domain event -- the model was built when the
# demo played past it -- not "what a user sees when they jump to tick 30001". This
# gate asserts supply/refusal accounting; pixels at that tick are a different gate's
# subject.
FACE="models/weapons/c_models/c_flamethrower/c_flamethrower.mdl"
SAPPER="models/weapons/c_models/c_sapper/c_sapper_xmas.mdl"
WARHORN="models/workshop_partner/weapons/c_models/c_shogun_warhorn/c_shogun_warhorn.mdl"

assertions=0
failures=0
assert_eq() { assertions=$((assertions + 1)); if [ "${2:-<none>}" = "${3:-}" ]; then echo "  PASS  $1 = $2"; else echo "  FAIL  $1 = ${2:-<none>} (expected $3)"; failures=$((failures + 1)); fi; }
assert_ge() { assertions=$((assertions + 1)); if [ "${2:-0}" -ge "${3:-1}" ] 2>/dev/null; then echo "  PASS  $1 >= $2 (>= $3)"; else echo "  FAIL  $1 = ${2:-<none>} (expected >= $3)"; failures=$((failures + 1)); fi; }
assert_le() { assertions=$((assertions + 1)); if [ "${2:-0}" -le "${3:-0}" ] 2>/dev/null; then echo "  PASS  $1 <= $2 (<= $3)"; else echo "  FAIL  $1 = ${2:-<none>} (expected <= $3)"; failures=$((failures + 1)); fi; }

trace_field() { # trace_field <trace> <line-match> <field>
  grep -a "$2" "$1" 2>/dev/null | grep -a "$3=" | tail -1 | tr ' ' '\n' | grep "^$3=" | cut -d= -f2
}
trace_present() { grep -a -c "$2" "$1" 2>/dev/null || true; }

for path in "$TF" "$DEMO" "$EXE" "$SETTINGS" "$UI"; do
  [ -e "$path" ] || { echo "FATAL: missing input: $path"; exit 2; }
done
mkdir -p "$OUT"

echo "=== attach weapon budget gate ==="
echo "  exe    = $EXE"
echo "  exe sha256    = $(sha256sum "$EXE" | cut -d' ' -f1)"
echo "  demo sha256   = $(sha256sum "$DEMO" | cut -d' ' -f1)"
echo "  gate sha256   = $(sha256sum "$0" | cut -d' ' -f1)"
echo "  settings file = $SETTINGS ($(sha256sum "$SETTINGS" | cut -d' ' -f1))"
echo "  command = $EXE --tf-root $TF --hidden-window --audio-disabled --settings-file $SETTINGS --ui-settings-file $UI --demo $DEMO --capture-tick $TICK --playback-speed $SPEED --capture-frame $OUT/frame.bmp --window-size 1280x720 --debug-camera 1707.0 -125.1 152.8 4.1 0.0 --player-animation --player-weapon-attach on"

rm -f "$OUT/frame.bmp" "$OUT/frame.trace" "$OUT/frame.log"
TF2_NATIVE_TRACE="$OUT/frame.trace" timeout 900 "$EXE" \
  --tf-root "$TF" --hidden-window --audio-disabled \
  --settings-file "$SETTINGS" --ui-settings-file "$UI" \
  --demo "$DEMO" --capture-tick "$TICK" --playback-speed "$SPEED" \
  --capture-frame "$OUT/frame.bmp" \
  --window-size 1280x720 \
  --debug-camera 1707.0 -125.1 152.8 4.1 0.0 \
  --player-animation --player-weapon-attach on \
  > "$OUT/frame.log" 2>&1
RC=$?
echo "  rc=$RC frame=$([ -s "$OUT/frame.bmp" ] && echo yes || echo no) trace=$(stat -c %s "$OUT/frame.trace" 2>/dev/null || echo 0) log=$OUT/frame.log"
[ -f "$OUT/frame.trace" ] || { echo "FATAL: no trace written"; exit 2; }

T="$OUT/frame.trace"
SUPPLIED=$(trace_field "$T" "attach: meshSource" supplied)
REFUSED=$(trace_field "$T" "attach: meshSource" refused)
FACE_SUPPLIED=$(grep -a -c "attach: mesh result=supplied path=$FACE " "$T" || true)
WARHORN_SUPPLIED=$(grep -a -c "attach: mesh result=supplied path=$WARHORN " "$T" || true)
SAPPER_SUPPLIED=$(grep -a -c "attach: mesh result=supplied path=$SAPPER " "$T" || true)
NAMED_REFUSALS=$(grep -a "attach: mesh result=refused" "$T" | grep -a -c -e "$FACE" -e "$SAPPER" -e "$WARHORN" || true)
OOM=$(grep -a -c "0x8007000E" "$T" || true)
PREP_CAP=$(trace_field "$T" "budget: prepare" capacity)
PREP_CANDIDATES=$(trace_field "$T" "budget: prepare" candidates)
PREP_ADMITTED=$(trace_field "$T" "budget: prepare" admitted)
PREP_DEFERRED=$(trace_field "$T" "budget: prepare" deferred)
PREP_FAILED=$(trace_field "$T" "budget: prepare" failed)
PREP_BUILT=$(trace_field "$T" "budget: prepare" built)
MESH_CAP=$(trace_field "$T" "budget: meshTable" capacity)
MESH_RESIDENT=$(trace_field "$T" "budget: meshTable" resident)
MESH_EVICTIONS=$(trace_field "$T" "budget: meshTable" evictions)
MESH_REFUSALS=$(trace_field "$T" "budget: meshTable" capacityRefusals)
INST_MISSING=$(grep -a "budget: instances" "$T" | grep -ao "missingMesh:[0-9]*" | tail -1 | cut -d: -f2)
INST_DEFERRED=$(grep -a "budget: instances" "$T" | grep -ao "deferredNotResident:[0-9]*" | tail -1 | cut -d: -f2)
DRAW_DROPPED=$(trace_field "$T" "budget: draw" droppedPlayers)
ATTACH_OVER=$(trace_field "$T" "scene: weaponAttach" overBudget)
GPU_PREPARED=$(sed -n "s/.*gpu: models done \([0-9][0-9]*\) uploaded=.*/\1/p" "$T" | head -1)
GPU_UPLOADED=$(trace_field "$T" "gpu: models done" uploaded)

echo "  attach: supplied=${SUPPLIED:-<none>} refused=${REFUSED:-<none>} named-refusals=${NAMED_REFUSALS:-<none>} E_OUTOFMEMORY=${OOM:-<none>}"
echo "  named supplied: flamethrower=${FACE_SUPPLIED:-<none>} warhorn=${WARHORN_SUPPLIED:-<none>} sapper_xmas=${SAPPER_SUPPLIED:-<none>} (sapper need not be held at this tick)"
echo "  layer A: capacity=${PREP_CAP:-<none>} candidates=${PREP_CANDIDATES:-<none>} admitted=${PREP_ADMITTED:-<none>} built=${PREP_BUILT:-<none>} deferred=${PREP_DEFERRED:-<none>} failed=${PREP_FAILED:-<none>}"
echo "  layer B: capacity=${MESH_CAP:-<none>} resident=${MESH_RESIDENT:-<none>} evictions=${MESH_EVICTIONS:-<none>} capacityRefusals=${MESH_REFUSALS:-<none>}"
echo "  layer C: missingMesh=${INST_MISSING:-<none>} deferredNotResident=${INST_DEFERRED:-<none>}"
echo "  layer D: droppedPlayers=${DRAW_DROPPED:-<none>} weaponAttach-overBudget=${ATTACH_OVER:-<none>}"
echo "  world models: prepared=${GPU_PREPARED:-<none>} uploaded=${GPU_UPLOADED:-<none>}"

# --- the fixture proves its own identity (a missing or swapped recording must RED) ---
DEMO_ACTUAL=$(sha256sum "$DEMO" 2>/dev/null | cut -d' ' -f1)
assert_eq "the fixture demo is the pinned file (sha256)" "${DEMO_ACTUAL:-<missing>}" "$DEMO_SHA256"

# --- priority: the reason the three models fit is the ADMISSION ORDER, not a bigger
# capacity. Nothing of player/weapon rank may be deferred while a prop was admitted:
# the prepare pass sorts by rank (main.cpp:3825-3828), so a deferred player or weapon
# model would mean that ordering is gone.
ADMITTED_BY=$(trace_field "$T" "budget: prepare" admittedByPriority)
DEFERRED_BY=$(trace_field "$T" "budget: prepare" deferredByPriority)
prio() { echo "$1" | tr ',' '\n' | grep "^$2:" | cut -d: -f2; }
ADM_PLAYER=$(prio "$ADMITTED_BY" player)
ADM_WEAPON=$(prio "$ADMITTED_BY" weapon)
DEF_PLAYER=$(prio "$DEFERRED_BY" player)
DEF_WEAPON=$(prio "$DEFERRED_BY" weapon)
echo "  priority: admittedByPriority=${ADMITTED_BY:-<none>}"
echo "  priority: deferredByPriority=${DEFERRED_BY:-<none>}"

assert_eq "run rc" "$RC" "0"
assert_eq "a frame was captured" "$([ -s "$OUT/frame.bmp" ] && echo yes || echo no)" "yes"
assert_ge "attach ledger is present" "$(trace_present "$T" "attach: meshSource")" "1"
assert_eq "the attach refused nothing" "${REFUSED:-<none>}" "0"
assert_ge "the attach supplied at least one world model" "${SUPPLIED:-0}" "1"
assert_ge "c_flamethrower was SUPPLIED by name" "${FACE_SUPPLIED:-0}" "1"
assert_ge "c_shogun_warhorn was SUPPLIED by name" "${WARHORN_SUPPLIED:-0}" "1"
assert_eq "no refused line names any of the three known OOM models" "${NAMED_REFUSALS:-<none>}" "0"
assert_eq "no E_OUTOFMEMORY on any upload (the defect's own signature)" "${OOM:-<none>}" "0"
assert_ge "layer A ledger is present" "$(trace_present "$T" "budget: prepare")" "1"
assert_eq "layer A capacity is still 64 (the fix is accounting, not a bigger constant)" "${PREP_CAP:-<none>}" "64"
assert_eq "layer A accounts for every candidate" "$(( ${PREP_ADMITTED:-0} + ${PREP_DEFERRED:-0} ))" "${PREP_CANDIDATES:-<none>}"
assert_eq "layer A admits nothing it cannot build or count" "$(( ${PREP_BUILT:-0} + ${PREP_FAILED:-0} ))" "${PREP_ADMITTED:-<none>}"
assert_ge "layer B ledger is present" "$(trace_present "$T" "budget: meshTable")" "1"
assert_eq "layer B capacity is still 96 (not raised)" "${MESH_CAP:-<none>}" "96"
assert_le "layer B resident fits its capacity" "${MESH_RESIDENT:-<none>}" "${MESH_CAP:-0}"
assert_eq "layer B evicted nothing on this recording" "${MESH_EVICTIONS:-<none>}" "0"
assert_eq "layer B refused nothing on this recording" "${MESH_REFUSALS:-<none>}" "0"
assert_eq "layer C lost no instance to a missing mesh" "${INST_MISSING:-<none>}" "0"
assert_eq "layer D dropped no player body" "${DRAW_DROPPED:-<none>}" "0"
assert_eq "every held weapon that wanted a slot got one" "${ATTACH_OVER:-<none>}" "0"
assert_ge "priority: player body models were admitted" "${ADM_PLAYER:-0}" "1"
assert_ge "priority: held-weapon world models were admitted" "${ADM_WEAPON:-0}" "1"
assert_eq "priority: no player body model was deferred by the budget" "${DEF_PLAYER:-<none>}" "0"
assert_eq "priority: no held-weapon world model was deferred by the budget" "${DEF_WEAPON:-<none>}" "0"
assert_ge "the prepare budget actually binds on this recording (deferred >= 1)" "${PREP_DEFERRED:-0}" "1"
assert_eq "the capture is written at the pinned window's client size" "$(trace_field "$T" "capture: frame" width)x$(trace_field "$T" "capture: frame" height)" "$(trace_field "$T" "window: created" client)"

echo "=== attach weapon budget gate: $assertions assertions, $failures failed ==="
if [ "$failures" -eq 0 ]; then
  echo "ATTACH-WEAPON-BUDGET=PASS"
  exit 0
fi
echo "ATTACH-WEAPON-BUDGET=FAIL"
exit 1
