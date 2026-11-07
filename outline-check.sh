#!/usr/bin/env bash
# outline-check.sh -- T52: team-coloured x-ray player outlines.
#
# WHY THIS EXISTS
# ---------------
# T52 makes four claims, and every one of them is a statement that could be
# true for the wrong reason:
#
#   1. IDENTITY. "The outline is on that player, and that player is RED." A
#      frame that contains red pixels says nothing about which entity they
#      belong to. So the gate asserts the PRODUCT's own sample: at every
#      sampled tick, each row carries an entity index, a class name, a team
#      value and a pose, and the draw list it built joins back to those rows by
#      entity index. Those readings come from entity_overlay_probe, which calls
#      the same sampleOverlayEntities and buildOutlineDrawList the product does.
#
#   2. NOTHING ELSE CHANGED. "The overlay does not change world geometry or
#      ordinary visibility" is asserted as: with the overlay on, every pixel
#      OUTSIDE the drawn silhouette's bounds is byte-identical to the same tick
#      with the overlay off, and the pass draws in a slot before the ViewModel's --
#      the ViewModel pass. A byte count would pass for an overlay that had
#      shifted the whole world by one pixel; a bound cannot.
#
#   3. TEAM COLOURS, DIRECTIONALLY. The red player's region must be
#      red-dominant AND contain no blue-dominant pixels, and the blue player's
#      region the mirror image. Exchanging the two colours fails both halves at
#      once, which is what makes that mutation mean something.
#
#   4. DEPTH AWARENESS. The depth-tested control is not "fewer pixels changed".
#      In a region where the frame's own depth buffer says world geometry is in
#      front of the player, the x-ray frame must show team-coloured pixels and
#      the depth-tested frame must show NONE -- and must be byte-identical to
#      the overlay-off frame there.
#
# COVERAGE
#   The five inputs the task names are covered by two layers, because only one
#   of them can be a picture:
#     probe --fixtures   visible, unknown team, despawn, and no players at all
#                        are asserted as data (a player with team 0 must land in
#                        the unknown bucket and must fail BOTH team predicates)
#     live frames        visible and occluded are asserted as pictures, using a
#                        demo whose camera holds two players at different depths
#
# THE TICK LIST
#   The sampled ticks and the capture tick have to land inside the C++ entity
#   history's RETAINED WINDOW. Outside it the accessor answers "unavailable",
#   the product fills its scene states from the same accessor and gets none, and
#   a frame at that tick therefore has no player in it at all. That is what the
#   inherited tick list (1001..2001) was measuring: six ticks, six "the tick
#   precedes every retained snapshot", zero rows. The probe measures the window
#   and prints it (--find-ticks); the ticks below are inside it.
#
# MUTATIONS (--mutation), each rebuilt and each required to turn a reading red
#   m1 swap the two team colours        -> team: red_wrong_team/blue_wrong_team
#   m2 depth-test only                  -> depth: occluded_xray drops to 0, and
#                                          the published x-ray depth TEST flips
#   m3 overlay always on and leaks      -> the OFF frame gains team pixels
#   m5 the overlay writes depth         -> the published depth WRITE flips
#   m6 capture lands near, not at, tick -> the speed=1 and speed=default frames differ
#   m4 move the pass after ViewModel    -> orderOk=0 and ordinal != the fixed build's
# Each mutation restores the source and the restore is verified with cmp.
#
# SECTION 0 runs the two Python readers on synthetic frames and traces and
# requires every reading to answer BOTH ways (correct input passes, the defect
# each mutation reintroduces fails). It needs no GPU, so it also runs while the
# machine is busy -- which is where it was written.
#
# All product runs go through the host lock and carry --hidden-window
# --audio-disabled: builds, launches and captures are single-instance here.
#
# Usage: bash outline-check.sh [--mutation] [--skip-live]
#   --skip-live  run only the probe layer (no product launch); the gate says so
#                in its verdict rather than reporting a silent pass.
# Exit: 0 = every asserted criterion held.
set -uo pipefail
cd "$(dirname "$0")"

EXE=native/build-nmake/tf2_demo_native.exe
PROBE=native/build-nmake/entity_overlay_probe.exe
TF="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
# The demo whose map is installed on this machine (koth_bagel_rc13 is not), so
# the frame is a real world with real players rather than the fallback quad.
DEMO="D:/TF2_Demo_Player/testdata/demos/bb841c6d379ff7c40d0c8baf99f59d8d_matcha-20260927-1347-cp_snakewater_final1.dem"
OUT="${OUTLINE_OUT:-evidence/outline}"
# Cygwin truncates a drive-qualified path once it sits behind $PWD: a trace
# named "$PWD/D:/out/x.trace" is written to a directory called "D:" and every
# reader of it comes back empty. Measured on 2026-11-03: an absolute
# OUTLINE_OUT produced "sed: can't read D:/_shared/a2-outline-final/on.trace"
# while all four bmps were written correctly, because the frames are opened by
# the prefix as given and only the trace was routed through $PWD. Normalise the
# root ONCE, here, so every consumer of OUT sees one form.
case "$OUT" in
  /*|[A-Za-z]:*) OUT="$(cygpath -m "$OUT")" ;;
  *) OUT="$(cygpath -m "$PWD/$OUT")" ;;
esac
# absolute_out() is the only place a path under OUT is converted, so a caller
# cannot reintroduce the double prefix by hand.
absolute_out() { # absolute_out <shell path under OUT> -- native Windows path
  local path="$1"
  case "$path" in
    /*|[A-Za-z]:*) cygpath -m "$path" ;;
    *) cygpath -m "$PWD/$path" ;;
  esac
}
mkdir -p "$OUT"

# ---------------------------------------------------------------------------
# SETTINGS ISOLATION (lead m01207, 2026-11-07). The product writes its settings
# file and its UI file on EVERY exit whether or not the run changed anything
# (main.cpp:11877-11893 saves bookmarks/key binds and saveSettings(persistent);
# native_ui.cpp:137-139 rewrites recentDemo0..7). With no --settings-file /
# --ui-settings-file it writes the REAL profile under
# %LOCALAPPDATA%/TF2 Demo Player. So every launch in this gate is handed its own
# copy under $OUT/inputs, seeded byte-for-byte from the host profile so the
# pinned pixel fixtures keep exactly the renderer settings they were measured on,
# and the product writes the copies instead of the user's files.
SETTINGS_SEED_DIR="$OUT/inputs"
mkdir -p "$SETTINGS_SEED_DIR"
HOST_PROFILE_DIR="${LOCALAPPDATA:-${HOME}/AppData/Local}/TF2 Demo Player"
[ -f "$HOST_PROFILE_DIR/settings.ini" ] && cp "$HOST_PROFILE_DIR/settings.ini" "$SETTINGS_SEED_DIR/host-settings.ini" 2>/dev/null
[ -f "$HOST_PROFILE_DIR/ui.ini" ] && cp "$HOST_PROFILE_DIR/ui.ini" "$SETTINGS_SEED_DIR/host-ui.ini" 2>/dev/null
seed_run_files() { # seed_run_files <prefix>: per-run copies, so two runs never share an MRU
  RUN_SETTINGS="$SETTINGS_SEED_DIR/${1##*/}.settings.ini"
  RUN_UI="$SETTINGS_SEED_DIR/${1##*/}.ui.ini"
  if [ -f "$SETTINGS_SEED_DIR/host-settings.ini" ]; then cp "$SETTINGS_SEED_DIR/host-settings.ini" "$RUN_SETTINGS"; else : > "$RUN_SETTINGS"; fi
  if [ -f "$SETTINGS_SEED_DIR/host-ui.ini" ]; then cp "$SETTINGS_SEED_DIR/host-ui.ini" "$RUN_UI"; else : > "$RUN_UI"; fi
}

MUTATION=0
SKIP_LIVE=0
# WORLD_ONLY is set here and read down at the "T85 P0" section below. It was read
# there but never set here until 2026-11-07: the flag was documented in the
# section comment, the section existed, and turning it on was impossible -- the
# parser answered "unknown argument: --world-only" and exited 2. mutate.sh m14
# then read rc=2 as a refusal but could not find its token, so the mutation was
# recorded MUTATION-GREEN (no named assertion went red) even though the gate's
# behaviour under the mutant was never exercised. Fixing it here is what makes
# m14 a real red-green test again.
WORLD_ONLY=0
for arg in "$@"; do
  case "$arg" in
    --mutation) MUTATION=1 ;;
    --skip-live) SKIP_LIVE=1 ;;
    --world-only) WORLD_ONLY=1 ;;
    *) echo "unknown argument: $arg" >&2; exit 2 ;;
  esac
done

PY="C:/Users/Administrator/.workbuddy-ai/binaries/python/versions/3.13.12/python.exe"

fail=0
assert_eq() { # assert_eq <label> <actual> <expected>
  if [ "${2:-<none>}" = "$3" ]; then
    echo "  OK   $1 = $3"
  else
    echo "  FAIL $1 = ${2:-<none>}, expected $3"; fail=1
  fi
}
assert_ge() { # assert_ge <label> <actual> <minimum>
  if [ -n "${2:-}" ] && [ "$2" -ge "$3" ] 2>/dev/null; then
    echo "  OK   $1 = $2 (>= $3)"
  else
    echo "  FAIL $1 = ${2:-<none>}, expected >= $3"; fail=1
  fi
}
assert_le() { # assert_le <label> <actual> <maximum>
  if [ -n "${2:-}" ] && [ "$2" -le "$3" ] 2>/dev/null; then
    echo "  OK   $1 = $2 (<= $3)"
  else
    echo "  FAIL $1 = ${2:-<none>}, expected <= $3"; fail=1
  fi
}
assert_lt() { # assert_lt <label> <actual> <maximum, exclusive>
  if [ -n "${2:-}" ] && [ "$2" -lt "$3" ] 2>/dev/null; then
    echo "  OK   $1 = $2 (< $3)"
  else
    echo "  FAIL $1 = ${2:-<none>}, expected < $3"; fail=1
  fi
}
assert_contains() { # assert_contains <label> <haystack> <needle>
  case "${2:-}" in
    *"${3:-}"*) echo "  OK   $1" ;;
    *) echo "  FAIL $1 (wanted: ${3:-<none>})"; fail=1 ;;
  esac
}

# ---------------------------------------------------------------------------
# T85 P0 (lead m00816, option a): THE THREE NAMED WEAPON WORLD MODELS.
#
# WHY THIS RUN EXISTS. The defect this task is about is not "the budget is
# small": it is that three SPECIFIC weapon world models were refused with
# E_OUTOFMEMORY on a recording where nineteen player bodies are on screen --
#   models/weapons/c_models/c_flamethrower/c_flamethrower.mdl
#   models/weapons/c_models/c_sapper/c_sapper_xmas.mdl
#   models/workshop_partner/weapons/c_models/c_shogun_warhorn/c_shogun_warhorn.mdl
# (evidence/mutation/weaponworldmodel-m10/*-dump.txt, and reproduced in
#  evidence/t85/upward-pyro/before.trace). A ledger that says refused=0 is not
#  the same claim as "those three were supplied": the first can be true on a
#  frame where none of them was ever requested. So this gate names them.
#
# WHY IT IS A SECOND RECORDING. The outline fixture above (cp_snakewater, two
# players at two depths) never holds nineteen bodies and cannot exhaust the
# table by construction; the failure needs the Highlander STV pl_upward_f12
# recording in testdata that every t62-matrix measurement used. An extra
# product launch is the price of asserting the actual defect instead of a
# proxy for it.
#
# THE TICK DOMAIN (lead m00789): --capture-tick plays past the target and
# clamps the cursor back, so a trace line's leading seconds are PLAYBACK time,
# not "what a user sees at that tick". Everything asserted below is a MESH
# UPLOAD reading (supplied / refused / table capacity), which is a property of
# the run, not of the captured frame. No pixel claim is made from this run.
#
# WHAT REPLACED WHAT. Before this task outline-check.sh pinned the world pass
# at "gpu: models done 59 uploaded=59" (old lines 857-859) and asserted the
# cap-64/cap-96 pair (old 1842-1860). Those readings stood for "the fixed
# budget happened to be enough on the outline fixture" -- they could not see
# a refusal at all. The assertions below are the same concern expressed as an
# account: the named models are SUPPLIED, no refusal names them, the OOM code
# does not appear, and every candidate is admitted or deferred by priority.
# The pinned path is the repo convention (D:/TF2_Demo_Player/testdata/demos/),
# the same directory third-person-weapon-check.sh:85, player-body-appearance-check.sh:42,
# class-pixels-check.sh:102 and player-animation-check.sh:48 read. The copy under
# Downloads is byte-identical (sha256 7e047aff...dc4a3, 75,663,754 B) but a gate may
# not depend on a user Downloads folder; the sha256 below is what makes the fixture
# an identity rather than a path.
T85_WORLD_DEMO="${T85_WORLD_DEMO:-D:/TF2_Demo_Player/testdata/demos/a9630c6557468b24f0b8c2e7dab38ed8_match-20261030-2145-pl_upward_f12.dem}"
T85_WORLD_TICK="${T85_WORLD_TICK:-30001}"
T85_WORLD_SETTINGS=evidence/t85/inputs/upward-settings.json
T85_WORLD_UI=evidence/t85/inputs/upward-ui.json
# The pyro fixture as pinned by evidence/t62-matrix/class-pixels-upward/targets.txt
# (ent 3 at tick 30001, origin 1957.0,-125.1,94.8; the camera sits 250 units
# back on -x and 58 up, yaw 0, pitch 4.1).
T85_WORLD_EYE="1707.0 -125.1 152.8 4.1 0.0"
# THE ONLY PLACE IN THIS GATE THAT CAN START THE PRODUCT. Both fixtures build
# their own argv and hand it to this one function, so --hidden-window and
# --audio-disabled are checked on the FINAL argv of every run: a caller cannot
# forget them, and a second launch site cannot appear without the LAUNCH_SITES
# guard below going red (the audio incident: the flag was in the file, just not
# at every call site, so coverage was not per-site correctness).
launch_product() { # launch_product <prefix> <final argv...>
  local prefix="$1"; shift
  case " $* " in
    *" --audio-disabled "*) : ;;
    *) echo "REFUSED: launch_product was handed an argv without --audio-disabled" >&2
       echo "99"
       return 0 ;;
  esac
  TF2_NATIVE_TRACE="$(absolute_out "$prefix.trace")" \
    timeout "${PRODUCT_TIMEOUT:-600}" "$EXE" "$@" > "$prefix.log" 2>&1
  echo "$?"
}
run_world_fixture() { # run_world_fixture <prefix>
  local prefix="$1"
  rm -f "$prefix.bmp" "$prefix.pgm" "$prefix.trace" "$prefix.log"
  local -a args=(
    --tf-root "$TF" --hidden-window --audio-disabled
    --settings-file "$T85_WORLD_SETTINGS" --ui-settings-file "$T85_WORLD_UI"
    --demo "$T85_WORLD_DEMO" --capture-tick "$T85_WORLD_TICK" --playback-speed 32
    --capture-frame "$prefix.bmp"
    --window-size 1280x720
    --debug-camera $T85_WORLD_EYE
    --player-animation --player-weapon-attach on
  )
  # The world fixture keeps its FULL trace (no trim): the named-model readings are
  # upload lines spread over playback time, and a trimmer tuned for the outline
  # fixture's summary lines is a filter this section does not need.
  PRODUCT_TIMEOUT="${T85_WORLD_TIMEOUT:-900}" launch_product "$prefix" "${args[@]}"
}
assert_world_fixture() { # assert_world_fixture <prefix> -- the named-model account
  local prefix="$1"
  # THE FIXTURE PROVES ITS OWN IDENTITY (lead m00911). A gate that reads a demo by
  # path and skips when the path is empty is the "SKIP-everything additive check"
  # this repo has shipped before (GATE-DESIGN-TRAPS.md). So: the demo must hash to
  # the pinned file and the map must be installed at the size measured on this host.
  # Missing inputs make these RED, they do not make the section absent.
  local demo_sha bsp_path bsp_bytes
  demo_sha=$(sha256sum "$T85_WORLD_DEMO" 2>/dev/null | cut -d" " -f1)
  bsp_path="$TF/maps/pl_upward_f12.bsp"
  bsp_bytes=$(stat -c%s "$bsp_path" 2>/dev/null || echo 0)
  echo "  world fixture: demo=$T85_WORLD_DEMO sha256=${demo_sha:-<missing>}"
  echo "  world fixture: map=$bsp_path bytes=${bsp_bytes:-0}"
  assert_eq "named world models: the upward fixture demo is the pinned file" "${demo_sha:-<missing>}" "7e047aff1f7743915e355eac907a95dc3d981aa0fae5591b6b15346fb77dc4a3"
  assert_eq "named world models: the upward map is the installed pl_upward_f12 BSP" "${bsp_bytes:-0}" "25252306"
  # Local copy of trace_field: this section also runs from --world-only, which
  # exits long before the file's own reader is defined.
  wfield() { # wfield <trace> <line-match> <field>
    grep -a "$2" "$1" 2>/dev/null | grep -a "$3=" | tail -1 | tr " " "\n" | grep -a "^$3=" | cut -d= -f2
  }
if [ "$SKIP_LIVE" -eq 1 ]; then
  echo "  named world models: SKIPPED (--skip-live; no product launch)"
else
  WORLD_RC=$(run_world_fixture "$prefix")
  WTRACE="$prefix.trace"
  echo "  world rc=$WORLD_RC bmp=$(stat -c%s "$prefix.bmp" 2>/dev/null || echo 0) trace=$(stat -c%s "$WTRACE" 2>/dev/null || echo 0) bytes"
  assert_eq "named world models: the upward multiply-held-weapons run rc" "$WORLD_RC" "0"
  assert_ge "named world models: the run captured a frame" "$(stat -c%s "$prefix.bmp" 2>/dev/null || echo 0)" "1"
  # THE NAMED DELIVERABLES. Each of the three is a separate reading: a build
  # that supplies two of them is a build that still drops one.
  W_FLAME=$(grep -a -c 'attach: mesh result=supplied path=models/weapons/c_models/c_flamethrower/c_flamethrower.mdl' "$WTRACE" 2>/dev/null || true)
  W_WARHORN=$(grep -a -c 'attach: mesh result=supplied path=models/workshop_partner/weapons/c_models/c_shogun_warhorn/c_shogun_warhorn.mdl' "$WTRACE" 2>/dev/null || true)
  W_SAPPER=$(grep -a -c 'attach: mesh result=supplied path=models/weapons/c_models/c_sapper/c_sapper_xmas.mdl' "$WTRACE" 2>/dev/null || true)
  W_R3=$(grep -a -c 'attach: mesh result=refused.*c_flamethrower\.mdl\|attach: mesh result=refused.*c_shogun_warhorn\.mdl\|attach: mesh result=refused.*c_sapper_xmas\.mdl' "$WTRACE" 2>/dev/null || true)
  W_OOM=$(grep -a -c '0x8007000E' "$WTRACE" 2>/dev/null || true)
  echo "  named world models supplied: flamethrower=${W_FLAME:-0} warhorn=${W_WARHORN:-0} sapper=${W_SAPPER:-0}; refused-naming-any=${W_R3:-0}; OOM-lines=${W_OOM:-0}"
  assert_ge "named world models: c_flamethrower's world mesh was supplied (it used to OOM)" "${W_FLAME:-0}" "1"
  assert_ge "named world models: c_shogun_warhorn's world mesh was supplied (it used to OOM)" "${W_WARHORN:-0}" "1"
  assert_eq "named world models: no refusal line names any of the three" "${W_R3:-<none>}" "0"
  assert_eq "named world models: E_OUTOFMEMORY 0x8007000E never appears in the trace" "${W_OOM:-<none>}" "0"
  # THE ACCOUNT BEHIND IT. The three are delivered because the prepare pass
  # spends its 64 slots by PRIORITY, not first-come: on this recording the
  # candidate set is larger than the budget (so the budget binds) and the
  # weapons it keeps are the ones nineteen bodies are holding.
  W_CAND=$(wfield "$WTRACE" "budget: prepare" candidates)
  W_ADMIT=$(wfield "$WTRACE" "budget: prepare" admitted)
  W_DEFER=$(wfield "$WTRACE" "budget: prepare" deferred)
  W_FAILED=$(wfield "$WTRACE" "budget: prepare" failed)
  W_CAP=$(wfield "$WTRACE" "budget: prepare" capacity)
  W_RESIDENT=$(wfield "$WTRACE" "budget: meshTable" resident)
  W_MCAP=$(wfield "$WTRACE" "budget: meshTable" capacity)
  W_REFUSALS=$(wfield "$WTRACE" "budget: meshTable" capacityRefusals)
  W_EVICT=$(wfield "$WTRACE" "budget: meshTable" evictions)
  W_INST_ACCEPT=$(wfield "$WTRACE" "budget: instances" accepted)
  W_INST_SLOTS=$(wfield "$WTRACE" "budget: instances" slots)
  echo "  world layer A: capacity=${W_CAP:-<none>} candidates=${W_CAND:-<none>} admitted=${W_ADMIT:-<none>} deferred=${W_DEFER:-<none>} failed=${W_FAILED:-<none>}"
  echo "  world layer B: capacity=${W_MCAP:-<none>} resident=${W_RESIDENT:-<none>} evictions=${W_EVICT:-<none>} capacityRefusals=${W_REFUSALS:-<none>}"
  assert_ge "named world models: the run saw a candidate set" "${W_CAND:-0}" "1"
  assert_eq "named world models: every candidate is admitted or deferred" "$(( ${W_ADMIT:-0} + ${W_DEFER:-0} ))" "${W_CAND:-<none>}"
  assert_eq "named world models: the prepare budget is 64 (not raised)" "${W_CAP:-<none>}" "64"
  # deferred >= 1 is the evidence that the budget BINDS on this recording: a
  # build that fits everything into 64 would pass every other assertion here
  # for the uninteresting reason, and could not have failed before the fix.
  assert_ge "named world models: the budget actually binds (some candidates are deferred)" "${W_DEFER:-0}" "1"
  assert_eq "named world models: no admitted model failed to build" "${W_FAILED:-<none>}" "0"
  assert_le "named world models: the table never exceeds its bound" "${W_RESIDENT:-<none>}" "${W_MCAP:-0}"
  assert_eq "named world models: the table refused nothing at the shipped bound" "${W_REFUSALS:-<none>}" "0"
  assert_eq "named world models: no resident mesh was evicted at the shipped bound" "${W_EVICT:-<none>}" "0"
  # COVERAGE BESIDE CHURN (lead m01475). "attach: meshSource supplied" counts only
  # the meshes the attach had to BUILD; a weapon the world pass already prepared is
  # resolved from that cache and just as present in the hand. A regression that
  # quietly stopped drawing ten weapons while the attach refused nothing would look
  # exactly like a cheaper prepare pass, and "refused==0" would wave it through. So
  # the request books are read too: every request leaves through one named exit
  # (balance below), and uploaded+resolvedFromWorld is how many weapon world models
  # the frame actually had.
  W_COV_LINES=$(grep -a -c "attach: coverage requested=" "$WTRACE" 2>/dev/null || true)
  W_COV_REQ=$(wfield "$WTRACE" "attach: coverage" requested)
  W_COV_RESOLVED=$(wfield "$WTRACE" "attach: coverage" resolvedFromWorld)
  W_COV_UPLOADED=$(wfield "$WTRACE" "attach: coverage" uploaded)
  W_COV_REFUSED=$(wfield "$WTRACE" "attach: coverage" refused)
  W_COV_RETRY=$(wfield "$WTRACE" "attach: coverage" retrySkipped)
  W_COV_UNRES=$(wfield "$WTRACE" "attach: coverage" unresolvedPath)
  # Balance check over EVERY line, not only the last: awk fields once split on [= ]
  # are 4=requested 6=resolvedFromWorld 8=uploaded 10=refused 12=retrySkipped
  # 14=unresolvedPath.
  W_COV_BAD=$(grep -a -o "attach: coverage requested=[0-9]* resolvedFromWorld=[0-9]* uploaded=[0-9]* refused=[0-9]* retrySkipped=[0-9]* unresolvedPath=[0-9]*" "$WTRACE" 2>/dev/null | awk -F'[= ]' '{ if ($4 != $6+$8+$10+$12+$14) bad++ } END { print bad+0 }')
  echo "  world attach coverage: lines=${W_COV_LINES:-<none>} requested=${W_COV_REQ:-<none>} resolvedFromWorld=${W_COV_RESOLVED:-<none>} uploaded=${W_COV_UPLOADED:-<none>} refused=${W_COV_REFUSED:-<none>} retrySkipped=${W_COV_RETRY:-<none>} unresolvedPath=${W_COV_UNRES:-<none>} balance-violations=${W_COV_BAD:-<none>}"
  assert_ge "named world models: the attach reports its request accounting" "${W_COV_LINES:-0}" "1"
  assert_eq "named world models: every attach request left through a named exit (coverage balances)" "${W_COV_BAD:-<none>}" "0"
  assert_ge "named world models: the hand's weapon world models are covered (uploaded + resolved from prepare)" "$(( ${W_COV_UPLOADED:-0} + ${W_COV_RESOLVED:-0} ))" "1"
  # The mesh-source ledger and the coverage ledger must agree on the upload count;
  # if they ever disagree, one of the two lines is stale.
  assert_eq "named world models: the coverage ledger agrees with the mesh-source ledger on uploads" "${W_COV_UPLOADED:-<none>}" "$(wfield "$WTRACE" "attach: meshSource" supplied)"
  echo "  world layer C: accepted=${W_INST_ACCEPT:-<none>} slots=${W_INST_SLOTS:-<none>}"
  assert_ge "named world models: the instance list has slots" "${W_INST_SLOTS:-0}" "1"
  assert_le "named world models: the accepted instances fit the instance list" "${W_INST_ACCEPT:-<none>}" "${W_INST_SLOTS:-0}"
  # WHY THE INSTANCE REJECTS ARE NOT A DEFECT (lead m01484). "rejected=" used to be a
  # single missingMesh bucket, so the after-fix reading (37) looked like 37 broken
  # instances. The buckets are separated at the source: an instance whose model the
  # budget deliberately deferred is deferredNotResident; only an instance that SHOULD
  # exist and does not is missingMesh (m8-class defect). Both are asserted, so a
  # regression that turns deferrals into holes cannot hide in the bigger number.
  # These two live inside the rejected= list ("missingMesh:0,deferredNotResident:37"),
  # i.e. key:value, so wfield (which reads key=value) cannot see them.
  W_INST_MISSING=$(grep -a "budget: instances" "$WTRACE" 2>/dev/null | tail -1 | grep -a -o "missingMesh:[0-9]*" | cut -d: -f2)
  W_INST_DEFERRED=$(grep -a "budget: instances" "$WTRACE" 2>/dev/null | tail -1 | grep -a -o "deferredNotResident:[0-9]*" | cut -d: -f2)
  W_ATTACH_HELD=$(wfield "$WTRACE" "scene: weaponAttach" held)
  W_ATTACH_DRAWN=$(wfield "$WTRACE" "scene: weaponAttach" drawn)
  echo "  world layer C split: missingMesh=${W_INST_MISSING:-<none>} deferredNotResident=${W_INST_DEFERRED:-<none>}; hand held=${W_ATTACH_HELD:-<none>} drawn=${W_ATTACH_DRAWN:-<none>}"
  assert_eq "named world models: nothing that should exist is missing its mesh (missingMesh)" "${W_INST_MISSING:-<none>}" "0"
  assert_ge "named world models: deferred instances are counted as deferred, not dropped (deferredNotResident)" "${W_INST_DEFERRED:-0}" "1"
  assert_ge "named world models: the frame has weapon-holding bodies" "${W_ATTACH_HELD:-0}" "1"
  assert_eq "named world models: every weapon-holding body got its world mesh (held == drawn)" "${W_ATTACH_DRAWN:-<none>}" "${W_ATTACH_HELD:-<none>}"
fi
}

# T85 P0 (lead m00816): --world-only runs ONLY the named world-model section, one
# product launch. It exists so the behavioural mutation (prepare priority sort
# reverted to first-come-first-served) can be recorded green -> red -> green
# against THIS gate's own assertions without paying for the six other launches
# the full suite needs. It is a debugging/evidence mode, not the gate's verdict.
#
# The two flags contradict each other: under --skip-live the section echoes
# "SKIPPED" and asserts nothing, so a combined run would print a PASS token with
# no reading behind it. That is the same class of trap as the equal-to-equal
# assertions this section replaces, so the combination is REFUSED (exit 2) and
# its token says SKIPPED-LIVE rather than PASS.
if [ "${WORLD_ONLY:-0}" = "1" ] && [ "$SKIP_LIVE" = "1" ]; then
  echo "  --world-only and --skip-live contradict: --world-only exists to RUN the live section"
  echo "OUTLINE-CHECK=SKIPPED-LIVE (--world-only refused: nothing to run)"
  exit 2
fi
if [ "${WORLD_ONLY:-0}" = "1" ]; then
  assert_world_fixture "$OUT/world-only"
  echo
  if [ "$fail" = "1" ]; then
    echo "OUTLINE-CHECK=FAIL (--world-only: the named world-model section)"
    exit 1
  fi
  echo "OUTLINE-CHECK=PASS (--world-only: the named world-model section only)"
  exit 0
fi

# An unknown command is a failed reading. Until 2026-10-29 this gate called
# assert_contains at line 270 without ever defining it: bash printed "command
# not found", the call returned 127, nothing set fail, and the suite went on to
# print OUTLINE-CHECK=PASS with that assertion never evaluated.
#
# bash runs this handler in a subshell, so it cannot set fail in the parent --
# it records the name in a file instead, and the end of the run reads that file
# and turns the verdict red. A missing command that leaves no trace is exactly
# the defect this gate shipped once already.
HARNESS_UNKNOWN_LOG="${TMPDIR:-/tmp}/outline-check-unknown-commands.$$.log"
: > "$HARNESS_UNKNOWN_LOG"
command_not_found_handle() {
  printf '%s\n' "${1:-<empty>}" >> "$HARNESS_UNKNOWN_LOG" 2>/dev/null
  echo "  FAIL harness: '${1:-<empty>}' is not a defined command, so the reading it guards was never taken" >&2
  return 127
}

# newer_than <reference> <path>... -- the files strictly newer than the
# reference, one path per line (at most four). A gate that measures a binary
# older than the tree it sits in is measuring a build of some other source.
newer_than() {
  local ref="$1"; shift
  find "$@" -type f -newer "$ref" 2>/dev/null | head -4
}

# depsources <target> -- the source files CMake compiles into that target, taken
# from the configured build tree's own DependInfo.cmake. This is the dependency
# record, not a guess from a directory: the first version of this guard compared
# both binaries against every file under native/tools, and native/tools holds
# sources of other targets (projectile_track_probe.cpp among them), so an edit to
# one of those reddened a run whose binaries were not built from it. NMAKE keeps
# no header dependencies (every .obj.d is empty), so the source list is the only
# per-target dependency data that exists.
depsources() {
  local dep="${TF2_BUILD_DIR:-native/build-nmake}/CMakeFiles/$1.dir/DependInfo.cmake"
  [ -f "$dep" ] || return 1
  awk -F'"' '/\.obj"/ && ($2 ~ /\.(cpp|c|cc|cxx)$/) {print $2}' "$dep"
}

# stale_reason <exe> <target> -- why that binary cannot be trusted for this tree,
# or nothing at all. Two conditions, both taken from the build itself:
#   headers: build-target.sh:124-133 treats a header newer than .headers-stamp as
#            reason to touch every source, because header dependencies are not
#            tracked; a stale stamp means this binary predates a header edit.
#   sources: the target's own DependInfo list, compared against the binary.
# Directories are never scanned: a source that some other target compiles says
# nothing about this one.
stale_reason() {
  local exe="$1" target="$2" build stamp new srcs
  build="${TF2_BUILD_DIR:-native/build-nmake}"
  stamp="$build/.headers-stamp"
  [ -f "$exe" ] || { echo "no binary at $exe"; return 0; }
  [ -f "$stamp" ] || { echo "no header stamp at $stamp, so build-target.sh would rebuild every source"; return 0; }
  new=$(find native/include native/src native/tools native/third_party \
          -type f \( -name '*.h' -o -name '*.hpp' \) -newer "$stamp" 2>/dev/null | head -2)
  if [ -n "$new" ]; then
    echo "headers are newer than $stamp, which is how build-target.sh decides to rebuild:"
    printf '         %s\n' $new
    return 0
  fi
  srcs=$(depsources "$target") || {
    echo "no DependInfo for $target under $build, so the sources behind this binary are unknown"
    return 0
  }
  new=$(for srcs in $srcs; do [ "$srcs" -nt "$exe" ] && echo "$srcs"; done | head -2)
  if [ -n "$new" ]; then
    echo "sources are newer than the binary, so it is a build of some other tree:"
    printf '         %s\n' $new
    return 0
  fi
}

# SECTION -1: the harness checks itself before it measures anything. A helper
# that cannot go red is a comment that runs, and a PASS from a harness that
# never evaluated one of its own assertions is not a reading at all.
echo "=== -1/6 the harness itself: every assert_* it calls is defined and can go red ==="
harness_broken=0
# Static scan of this script's own source: every assert_* name it mentions has
# to be a defined function. This is the check that would have gone red on
# 2026-10-29 while the suite printed PASS.
for _fn in $(grep -oE '\bassert_[a-z_]+' "$0" | sort -u); do
  if declare -F "$_fn" >/dev/null 2>&1; then
    echo "  OK   the harness defines $_fn"
  else
    echo "  FAIL the harness calls $_fn but never defines it, so that reading is not taken"
    fail=1; harness_broken=1
  fi
done
# THE LAUNCH GUARD IS ITSELF TESTED. launch_product is the only place that can
# start the product, and its whole job is refusing an argv that lost
# --audio-disabled. The refusal path returns BEFORE the launch, so this test
# starts nothing: it is the negative control for the guard that used to be a
# comment (2026-10-22 audio incident).
_lp_refuse=$( launch_product "$OUT/launch-guard-selftest" --tf-root "$TF" --hidden-window 2>"$OUT/launch-guard-selftest.stderr" )
case "$_lp_refuse" in
  99) echo "  OK   launch_product refuses an argv without --audio-disabled (returns 99)" ;;
  *) echo "  FAIL launch_product accepted an argv without --audio-disabled (returned ${_lp_refuse:-<none>})"
     fail=1; harness_broken=1 ;;
esac
if grep -q 'REFUSED: launch_product' "$OUT/launch-guard-selftest.stderr" 2>/dev/null; then
  echo "  OK   launch_product prints why it refused (not a silent drop)"
else
  echo "  FAIL launch_product refused without printing a reason"; fail=1; harness_broken=1
fi
_self_red=$( ( fail=0; assert_contains "self-test, absent sentence" "alpha beta" "gamma"; echo "fail=$fail" ) )
case "$_self_red" in
  *"fail=1") echo "  OK   assert_contains goes red when the sentence is absent" ;;
  *) echo "  FAIL assert_contains stayed green on an absent sentence"
     fail=1; harness_broken=1 ;;
esac
_self_green=$( ( fail=0; assert_contains "self-test, present sentence" "alpha beta gamma" "beta"; echo "fail=$fail" ) )
case "$_self_green" in
  *"fail=0") echo "  OK   assert_contains goes green when the sentence is present" ;;
  *) echo "  FAIL assert_contains rejected a sentence that is present"
     fail=1; harness_broken=1 ;;
esac
# The unknown-command path: the handler cannot set fail from its subshell, so
# what is tested is the trace it leaves, which the verdict reads below.
_before_unknown=$(wc -l < "$HARNESS_UNKNOWN_LOG" 2>/dev/null || echo 0)
( outline_harness_never_defines_this_command ) >/dev/null 2>&1
_after_unknown=$(wc -l < "$HARNESS_UNKNOWN_LOG" 2>/dev/null || echo 0)
if [ "${_after_unknown:-0}" -gt "${_before_unknown:-0}" ] 2>/dev/null; then
  echo "  OK   an undefined command leaves the trace that reddens the verdict"
else
  echo "  FAIL an undefined command left no trace, so the verdict could stay green"
  fail=1; harness_broken=1
fi
: > "$HARNESS_UNKNOWN_LOG"
# The binary is part of what is being measured, and a binary older than its own
# source is a reading of some other tree. This is not hypothetical: the mutation
# phase rebuilds after every case and the forward phase does not build at all, so
# the exe a mutation run leaves behind is a mutant, and the next run's forward
# phase measured it -- 2026-10-29, four depth assertions red with an untouched
# source tree. Both directions are proved here so the guard cannot itself be a
# comment that runs.
_st_dir=$(mktemp -d "${TMPDIR:-/tmp}/outline-stale.XXXXXX")
: > "$_st_dir/fake.exe"; : > "$_st_dir/new.cpp"
touch -t 202001010000 "$_st_dir/fake.exe" 2>/dev/null
if [ -n "$(newer_than "$_st_dir/fake.exe" "$_st_dir")" ]; then
  echo "  OK   a source newer than the binary is reported as stale"
else
  echo "  FAIL a newer source went unreported, so a stale binary would be measured"
  fail=1; harness_broken=1
fi
touch -t 209901010000 "$_st_dir/fake.exe" 2>/dev/null
if [ -z "$(newer_than "$_st_dir/fake.exe" "$_st_dir")" ]; then
  echo "  OK   a binary at least as new as its sources passes the same guard"
else
  echo "  FAIL a fresh binary was called stale, so the guard would redden an honest run"
  fail=1; harness_broken=1
fi
rm -rf "$_st_dir"
# THE SCOPE OF THE GUARD IS PART OF THE GUARD. entity_overlay_probe does not
# compile native/tools/projectile_track_probe.cpp. The first version of this
# check walked every file under native/tools, so an edit to another target's
# tool would have reddened a run whose binary never included it -- and the
# rebuild hint it printed could not have cleared that red either.
_probe_deps=$(depsources entity_overlay_probe)
if [ -z "$_probe_deps" ]; then
  echo "  FAIL no DependInfo for entity_overlay_probe, so its own source list cannot be checked"
  fail=1; harness_broken=1
else
  if printf '%s\n' "$_probe_deps" | grep -q 'projectile_track_probe\.cpp$'; then
    echo "  FAIL the list read for entity_overlay_probe names projectile_track_probe.cpp, a source of another target"
    fail=1; harness_broken=1
  else
    echo "  OK   the dependency list is scoped to its target: entity_overlay_probe does not compile projectile_track_probe.cpp"
  fi
  if printf '%s\n' "$_probe_deps" | grep -q 'native/src/demo_header\.cpp$'; then
    echo "  OK   the same list names demo_header.cpp, a source that target does compile"
  else
    echo "  FAIL the list read for entity_overlay_probe does not name demo_header.cpp, so it is not that target's list"
    fail=1; harness_broken=1
  fi
fi
for _pair in "$EXE:tf2_demo_native" "$PROBE:entity_overlay_probe"; do
  _why=$(stale_reason "${_pair%:*}" "${_pair##*:}")
  if [ -z "$_why" ]; then
    echo "  OK   ${_pair%:*} is not older than the sources its target compiles"
  else
    echo "  FAIL ${_pair%:*} cannot be trusted for this tree, so this run would measure a binary the tree does not describe:"
    printf '%s\n' "$_why"
    echo "         rebuild first: bash build-target.sh tf2_demo_native entity_overlay_probe"
    fail=1; harness_broken=1
  fi
done
if [ "$harness_broken" != "0" ]; then
  echo
  echo "OUTLINE-CHECK=FAIL"
  echo "  the harness is broken, so nothing measured below this line can be believed."
  exit 1
fi

# v <file> <key> -- the value of the LAST "<key>=<value>" token on a report file.
# The gate reads the probe's own sentence instead of inventing a key, so the keys
# carry spaces; the leading anchor stops "rows" from matching "ticksWithRows",
# and "last" wins because a run's final line is the one that describes the frame.
v() {
  local file="$1" key="$2" line
  # Exactly two arguments. An unquoted multi-word key arrives as three, and the
  # reading then comes back empty -- which reads as "the probe printed nothing"
  # instead of as a caller mistake. It said exactly that during the dry run.
  if [ "$#" -ne 2 ]; then
    echo "v() wants <file> <key>; multi-word keys must be quoted (got $#)" >&2
    return 1
  fi
  [ -f "$file" ] || return 0
  line=$(grep -a -o -E "(^| )${key}=[^ ]+" "$file" 2>/dev/null | tail -1)
  printf '%s' "$line" | sed -e 's/^ //' -e "s/^${key}=//"
}

# Missing input is a failure, not a skip: a gate that quietly SKIPs when the
# binary or the demo is absent reads as green while measuring nothing.
missing=0
for path in "$PROBE" "$TF"; do
  [ -e "$path" ] || { echo "FATAL: missing input: $path"; missing=1; }
done
# HOW MANY PLACES IN THIS FILE CAN START THE PRODUCT. One, and it is the guarded
# helper launch_product(). A second launch site added later would not inherit the
# guard, which is precisely the failure mode of the audio incident: the flag was
# present in the file, just not at every call site. Coverage is not per-site
# correctness. (T85 P0 added a SECOND FIXTURE, not a second launch site: the
# upward world-model run goes through launch_product like every other run, which
# is why this count is still 1.)
LAUNCH_SITES=$(grep -c 'timeout.*"\$EXE"' "$0")
if [ "$LAUNCH_SITES" != "1" ]; then
  echo "FATAL: this gate has $LAUNCH_SITES product launch sites; every launch must go through launch_product() so --audio-disabled cannot be dropped"
  exit 1
fi
if [ "$SKIP_LIVE" = "0" ]; then
  for path in "$EXE" "$DEMO"; do
    [ -e "$path" ] || { echo "FATAL: missing input: $path"; missing=1; }
  done
  [ -x "$PY" ] || { echo "FATAL: missing interpreter: $PY"; missing=1; }
fi
[ "$missing" = "0" ] || exit 1

echo "=== 0/6 the readers: offline, both ways (no GPU, no demo, no lock) ==="
# Every number sections 4-6 assert comes from one of the two Python readers, and
# a reader that always prints the same thing turns a gate green without measuring
# anything. This run hands them frames and traces in the product's shape and
# requires each reading to answer BOTH ways: correct input passes, and the defect
# the matching mutation reintroduces fails. It needs no GPU, which is why it can
# run before the machine is free.
OFFLINE_READER="$PWD/evidence/outline/outline-offline-selftest.py"
if [ -x "$PY" ] && [ -e "$OFFLINE_READER" ]; then
  "$PY" "$OFFLINE_READER" > "$OUT/offline-selftest.txt" 2>&1
  OFFLINE_RC=$?
  sed "s/^/  /" "$OUT/offline-selftest.txt"
  assert_eq "offline reader selftest" "$(v "$OUT/offline-selftest.txt" OFFLINE-SELFTEST)" "PASS"
  assert_eq "offline reader selftest exit" "$OFFLINE_RC" "0"
else
  echo "  FATAL: the offline reader selftest is missing; the readings below"
  echo "  would be numbers nobody has ever seen move."
  exit 1
fi

echo
echo "=== 1/6 the contract's own invariants (no GPU, no demo) ==="
# Every predicate the later sections rely on is asserted here first, so a
# failure below can be attributed to the frame and not to the predicate.
"$PROBE" --self-test > "$OUT/probe-self-test.txt" 2>&1
SELF_RC=$?
cat "$OUT/probe-self-test.txt"
assert_eq "probe self-test exit" "$SELF_RC" "0"
assert_eq "self-test failures" "$(v "$OUT/probe-self-test.txt" failures)" "0"

echo
echo "=== 2/6 the five coverage cases, as data ==="
# visible / occluded are asserted as PICTURES in section 4; what this layer
# pins is that the sample and the draw list agree about identity, team and
# pose, and that the three edge cases are transitions or empty sets rather
# than absences.
"$PROBE" --fixtures > "$OUT/probe-fixtures.txt" 2>&1
FIX_RC=$?
cat "$OUT/probe-fixtures.txt"
assert_eq "probe fixtures exit" "$FIX_RC" "0"
assert_eq "fixture cases" "$(v "$OUT/probe-fixtures.txt" "fixtures cases")" "5"
assert_eq "fixture failures" "$(v "$OUT/probe-fixtures.txt" failures)" "0"

if [ "$SKIP_LIVE" = "1" ]; then
  echo
  echo "=== 3/6..6/6 SKIPPED (--skip-live) ==="
  echo "  NOT VERIFIED: no product launch, so no frame, no ordinal and no depth"
  echo "  assertion was taken. This run proves the contract and the fixtures only."
  echo
  if [ "$fail" = "0" ]; then
    echo "OUTLINE-CHECK=PARTIAL (probe layers only; product layers NOT VERIFIED)"
    exit 0
  fi
  echo "OUTLINE-CHECK=FAIL"
  exit 1
fi

echo
echo "=== 3/6 a real recording: identity, team and pose at every sampled tick ==="
# The tick set is DERIVED FROM THIS DEMO, not pinned in the file.
#
# A fixed list is a constant that assumes one recording. 2026-10-23 the default was
# 1000..2000 while this demo retains server ticks 74481..74671 (serverTickOrigin
# 10252, so playback 64229..64419): every request answered "the tick precedes every
# retained snapshot", rows=0, and five assertions about players were really
# assertions about an empty sample. The window is asked for, not remembered.
#
# T52_TICKS survives as an override, but it is a DIAGNOSTIC PIN for reproducing one
# round, not the daily path. Pinning a tick list is how the stale list above stayed
# alive: every reading in the report passed one explicitly, so the default was never
# exercised.
#
# The discovery run is asserted rather than assumed:
#   * the probe exits 1 when it measured nothing, by design -- a run that samples
#     tick 0 always does. The exact stderr sentence is pinned, because "exited 1"
#     alone is also what a crash after printing looks like;
#   * its "demo history" line must be BYTE-IDENTICAL to the one the measuring run
#     prints below (cmp), which is what makes this the same window read twice
#     rather than a second, plausibly-similar code path;
#   * a doctored history line missing retainedFirst= must make the guard go red,
#     or the guard is decoration.
window_field() { # window_field <file> <name>
  sed -n "s/^demo history .*$2=\([0-9]*\).*/\1/p" "$1" 2>/dev/null | head -1
}
window_usable() { # window_usable <file> -- three fields readable and last > first
  local a b c
  a=$(window_field "$1" retainedFirst)
  b=$(window_field "$1" retainedLast)
  c=$(window_field "$1" serverTickOrigin)
  [ -n "$a" ] && [ -n "$b" ] && [ -n "$c" ] && [ "$b" -gt "$a" ] 2>/dev/null
}
"$PROBE" --demo "$DEMO" --ticks 0 > "$OUT/probe-window.txt" 2>&1
WINDOW_RC=$?
WINDOW_OK=1
if window_usable "$OUT/probe-window.txt"; then
  echo "  OK   window discovery reported a usable history line"
else
  echo "  FAIL window discovery reported no usable history line"; fail=1; WINDOW_OK=0
fi
assert_eq "window discovery exit is the probe measured-nothing verdict" "$WINDOW_RC" "1"
assert_contains "window discovery says why it measured nothing" \
  "$(cat "$OUT/probe-window.txt" 2>/dev/null)" \
  "FAIL no player was sampled at any requested tick"
# Self-test: the same guard must REJECT a file that lacks the field it claims to
# need. A guard that cannot go red is a comment that runs.
printf 'demo history packets=1 retainedLast=2 serverTickOrigin=3\n' > "$OUT/window-doctored.txt"
if window_usable "$OUT/window-doctored.txt"; then
  echo "  FAIL window self-test: a line without retainedFirst= was accepted"; fail=1
else
  echo "  OK   window self-test: a line without retainedFirst= is rejected"
fi

RETAINED_FIRST=$(window_field "$OUT/probe-window.txt" retainedFirst)
RETAINED_LAST=$(window_field "$OUT/probe-window.txt" retainedLast)
ORIGIN=$(window_field "$OUT/probe-window.txt" serverTickOrigin)
FIRST=$(( ${RETAINED_FIRST:-0} - ${ORIGIN:-0} ))
LAST=$(( ${RETAINED_LAST:-0} - ${ORIGIN:-0} ))
STEP=$(( (LAST - FIRST) / 6 )); [ "$STEP" -ge 1 ] 2>/dev/null || STEP=1
if [ -n "${T52_TICKS:-}" ]; then
  TICKS="$T52_TICKS"
  echo "  ticks: pinned by T52_TICKS=$TICKS (diagnostic pin for one round, not the daily path)"
elif [ "$WINDOW_OK" -eq 1 ]; then
  TICKS=""
  for n in 0 1 2 3 4 5; do TICKS="${TICKS:+$TICKS,}$((FIRST + n * STEP))"; done
  echo "  window: server ${RETAINED_FIRST}..${RETAINED_LAST} origin=$ORIGIN -> playback $FIRST..$LAST step=$STEP"
else
  TICKS="0"
  echo "  FAIL no tick set: the window was unreadable and T52_TICKS was not set"
  fail=1
fi
echo "  ticks=$TICKS"
"$PROBE" --demo "$DEMO" --ticks "$TICKS" > "$OUT/probe-demo.txt" 2>&1
DEMO_RC=$?
grep -a "^demo summary" "$OUT/probe-demo.txt" > "$OUT/probe-demo-summary.txt" 2>/dev/null
cat "$OUT/probe-demo-summary.txt"
assert_eq "probe demo exit" "$DEMO_RC" "0"
# The history line the discovery run printed and the one this run printed must be
# the same bytes: that is what makes the derived ticks a reading of the same window
# these samples were answered against.
grep -a "^demo history" "$OUT/probe-window.txt" > "$OUT/window-discovery.txt" 2>/dev/null
grep -a "^demo history" "$OUT/probe-demo.txt"   > "$OUT/window-measuring.txt" 2>/dev/null
assert_eq "the discovery run and the measuring run report the same history line" \
  "$(cmp -s "$OUT/window-discovery.txt" "$OUT/window-measuring.txt" && echo same || echo different)" "same"
# The rows the identity claim rests on. `rows` is how many player-rows were
# sampled in total: a demo whose snapshots hold no player would make every
# later assertion vacuous, so the count is a criterion and not a diagnostic.
assert_ge "sampled player rows" "$(v "$OUT/probe-demo-summary.txt" rows)" "1"
# Every sampled row must carry a team value and a pose: an outline drawn for a
# row with no origin would be a silhouette at the world origin.
ROWS=$(v "$OUT/probe-demo-summary.txt" rows)
WITH_TEAM=$(v "$OUT/probe-demo-summary.txt" withTeam)
WITH_POSE=$(v "$OUT/probe-demo-summary.txt" withPose)
assert_eq "rows carrying a team slot" "${WITH_TEAM:-<none>}" "${ROWS:-<none>}"
assert_eq "rows carrying a pose" "${WITH_POSE:-<none>}" "${ROWS:-<none>}"
# Both teams must be present, or the colour assertions in section 4 have only
# one side to compare and a colour swap could not be detected.
assert_ge "red player rows" "$(v "$OUT/probe-demo-summary.txt" red)" "1"
assert_ge "blue player rows" "$(v "$OUT/probe-demo-summary.txt" blue)" "1"
# The identity claim is about EVERY sampled tick, so it is counted per tick: a
# tick that produced no player row is a tick this gate can claim nothing about,
# and six requested ticks with five answered is a claim about five.
# Derived from the tick list, not hardcoded: the list is pinned in this file but
# the count is what the list actually asks for, so shortening the list cannot
# quietly weaken the assertion and lengthening it cannot break it.
EXPECTED_TICKS=$(printf "%s" "$TICKS" | tr "," "\n" | grep -c .)
assert_eq "sampled ticks that produced player rows" "$(v "$OUT/probe-demo-summary.txt" ticksWithRows)" "$EXPECTED_TICKS"
# The team mapping, per row: every row carries the wire value it classified, and
# the classification agrees with it. If the demo carried no usable team value,
# this is the reading that says so instead of section 4 drawing colours anyway.
assert_eq "sampled rows with no team value" "$(v "$OUT/probe-demo-summary.txt" rowsMissingTeam)" "0"
assert_eq "sampled rows with no pose" "$(v "$OUT/probe-demo-summary.txt" rowsMissingPose)" "0"
assert_eq "rows whose team classification disagrees with m_iTeamNum" \
  "$(v "$OUT/probe-demo-summary.txt" teamMappingMismatches)" "0"
# Every per-row line must carry the fields the attribution rests on (entity,
# class, team, pose), or the counts above are about lines that identify nothing.
IDENT_ROWS=$(grep -a -c -E "^tick requested=[0-9]+ resolved=[0-9]+ fidelity=[a-zA-Z]+ stale=-?[0-9]+ entity=[0-9]+ class=[^ ]+ player=[01] team=[a-z]+ raw=-?[0-9]+ hasTeam=[01] origin=" "$OUT/probe-demo.txt")
TICK_ROWS=$(grep -a -c "^tick requested=" "$OUT/probe-demo.txt")
assert_eq "per-row lines carrying identity, team and pose" "${IDENT_ROWS:-0}" "${TICK_ROWS:-0}"
assert_ge "per-row identity lines read" "${TICK_ROWS:-0}" "1"

# Fidelity is reported, never assumed. On this recording the C++ history
# answers most ticks from a checkpoint, and T53 measured the worst staleness at
# 1057 ticks. The identity claim survives that (it is a statement about the
# snapshot the frame was drawn from); a per-tick trajectory claim would not.
echo "  fidelity exact=$(v "$OUT/probe-demo-summary.txt" exact)"
echo "  fidelity checkpoint=$(v "$OUT/probe-demo-summary.txt" checkpointFallback)"
echo "  fidelity unavailable=$(v "$OUT/probe-demo-summary.txt" unavailable)"
# CAPABILITY FIRST. The identity claim is only as good as the accessor answering
# it: the C++ entity history retains a WINDOW, not the whole recording, and a
# tick outside it answers "unavailable" with no rows. Measured on this demo
# before the gate was pointed at the window: six requested ticks, six
# "the tick precedes every retained snapshot", zero rows. So a tick the history
# cannot answer is a failure of the claim, not a sampling detail.
assert_eq "sampled ticks the entity history could answer" "$(v "$OUT/probe-demo-summary.txt" unavailable)" "0"
# Two readings on purpose. "unavailable" counts ticks the history could NOT answer;
# "ticksWithRows" (asserted above against the requested count) counts the ones it
# could and that produced players. A sampling point outside the retained window
# moves the first; an empty sample moves the second. One reading cannot say both,
# and deleting either would trade one invisible failure for another.
assert_eq "sampled ticks that produced player rows (own reading)" \
  "$(v "$OUT/probe-demo-summary.txt" ticksWithRows)" "$EXPECTED_TICKS"

echo
echo "=== 4/6 the frames: nothing else changed, both colours, both directions ==="
# Three runs at the SAME tick, differing only in the switch under test:
#   off     --outline off
#   on      --outline on
#   tested  --outline on --outline-depth tested (the depth-awareness control)
# The capture tick used to be the FIRST tick inside the retained window derived
# above, and that was right while the window was fixed: the frame followed the
# recording. THE FIXTURE IS PINNED NOW (T52 A3, 2026-11-02), because the derived
# tick moved when an unrelated reading moved -- serverTickOrigin went 10252 ->
# 10320, so the derived playback tick went 64229 -> 64161 -- and 64161 is a frame
# the camera no longer looks at the players on (measured on the draw lines: the
# captured group has 11 items, every one onScreen=0, extent=0, box=0). That single
# drift collapsed the region, attribution and depth readings below in one go.
# A fixture that moves when an unrelated reading moves is not a fixture, so the
# tick and the camera are pinned TOGETHER as a pair: 64250 is a frame on which the
# measured draw group has 11 players on screen with a real vertex extent, and the
# eye is the one the silhouette evidence in evidence/t52-a3/ was captured with.
# The window-derived value is still printed, so drift stays visible; T52_TICK and
# T52_CAM keep the diagnostic override they always had.
CAPTURE_TICK=64250
TICK="${T52_TICK:-$CAPTURE_TICK}"
CAM_EYE="${T52_CAM:-5699 -1611 150 14.4 -143.3}"
echo "  capture tick=$TICK (pinned fixture tick 64250; window-derived $FIRST; T52_TICK pins it)"
echo "  capture camera=$CAM_EYE (pinned free-look eye; T52_CAM pins it)"
# The frame under test is a BIND-mesh fixture. The silhouette the region readings
# compare is the vertex extent, which the renderer publishes for a non-skinned
# instance only (native/src/native_renderer_frame.cpp:830-836 refuses to derive a
# vertex box from a skinned instance: the CPU holds bind positions, so such a box
# would describe a pose nobody drew). The ANIMATED silhouette is covered by
# evidence/t52-a3/A3-REPORT.md, at this same camera and tick, and passing the flag
# here keeps the gate's statement and the evidence's statement from being read as
# one another.
# ANIMATION IS OFF BY OMISSION, NOT BY A FLAG (T85 P0, 2026-11-07).
# The product has no --no-player-animation switch: its parser knows only
# --player-animation (main.cpp:2807) and playerAnimationEnabled defaults to false
# (main.cpp:2125). The old "ANIM_ARGS=(--no-player-animation)" was therefore
# silently ignored by the product -- the frame was the bind mesh for the right
# reason, but a flag that does not exist cannot carry that reason, and the next
# reader has no way to tell a spelling mistake from a measurement. The array stays
# EMPTY: nothing below turns animation on, which is the honest spelling.
ANIM_ARGS=()
echo "  capture animation=off (bind mesh; the animated silhouette is A3 evidence)"
# CORPSES ON, and this is a fixture decision about what THIS FRAME CAN MEASURE.
#
# The pinned tick is near the end of the recording, where the product's corpse rule
# (T66 rule 1) has already dropped most of the players out of the world pass: at
# 64250 nineteen of the twenty samples are entities the pass no longer draws, so the
# frame carries ONE body. A fixture that hides corpses therefore cannot measure the
# silhouette-to-body join at all, and the rounds that ran that way were green while
# thirteen of their silhouettes outlined entities the picture did not contain
# (measured 2026-11-02: submitted=15, poseWorld=2, poseBind=13). With corpses shown
# the same tick and camera carry fourteen silhouettes of both teams -- 6 red, 6 blue,
# 2 unknown, 5 rejected noMesh -- and every one of them is joined to a drawn body.
# Section 5b then re-runs this frame with the rule ON, which is what turns "the
# silhouette follows the body" into an assertion instead of a sentence.
CORPSE_ARGS=(--spectate-show-corpses)
echo "  capture corpse rule=off (--spectate-show-corpses: the frame has bodies to outline)"
# Two things had to change here before a capture could ever happen, and both were
# found by reading the product instead of the gate:
#   * --start-paused freezes g_playback.tick at 0, so the one-shot capture armed
#     at tick N waits forever for `tick >= N` and the run times out with no BMP.
#     That matches what the earlier runs left behind: no captures at all.
#   * reaching a tick near the end of a ~64000-tick recording costs 16 minutes at
#     1x, so --playback-speed fast-forwards the cursor. The captured frame is
#     identical: the cursor is HELD at that tick and the scene is resolved from
#     the recording, not accumulated frame by frame.
# 8x, not 16x: the invariance assertion below compares speed=1 with the DEFAULT
# speed, so the default has to be a speed whose frames are measured -- a third
# value that no assertion covers would be the same trust-me as no assertion.
SPEED="${T52_SPEED:-8}"
# SLOW-CAPTURE TIMEOUT (2026-10-24). The speed=1 capture advances the playback
# cursor one tick per rendered frame, so it costs about 28 minutes on this host
# (measured: 23 minutes to 384 MB of trace, ~30 to the end). It used to sit on a
# hard 1800 s, which is the same number as its own duration: a slightly slower
# machine fails the gate for being slower, not for being wrong. 2400 s keeps the
# failure mode (a hang still fails) and drops the coincidence. Override with
# T52_SLOW_TIMEOUT when the host is busy.
# TRACE SIZE (2026-10-24). A capture writes every frame's item lines, so one
# speed=1 trace reached 501,926,433 bytes (478.7 MiB), and two of them sat in
# evidence/outline/ after the run -- gitignored, but half a gigabyte each, and the
# speed=1 capture is also the slow one. Every reader in this file takes the LAST
# line matching its pattern (outline_value/trace_field both end in `tail -1`), and
# the capture frame is the tail of the file, so the raw stream can be reduced to:
#   * every summary line (outline: draw ...), in order;
#   * every gpu:/window:/scene:/title: line, in order;
#   * the last TRACE_KEEP_TAIL lines, which is where the capture frame lives.
# The reduction is skipped when it would remove the summary lines it depends on:
# a trimmed trace that cannot answer the readers is worse than a large one.
trim_trace() { # trim_trace <prefix>
  local prefix="$1" raw="$1.trace" kept="$1.trace.kept"
  [ -f "$raw" ] || return 0
  local before after
  before=$(stat -c %s "$raw" 2>/dev/null || echo 0)
  # Only what a reader in this file actually asks for. Measured on a real
  # 501,926,433-byte trace: outline: item 266 MB, scene: 48 MB, outline: draw
  # 17 MB, everything else 30 KB. The item lines that matter are the capture
  # frame (the tail); the rest are per-frame repeats whose last copy is the one
  # every helper reads.
  { grep -a "outline: draw" "$raw" | tail -n 50 || true; } > "$kept"
  { grep -aE "gpu:|window:|hud: |logstf: " "$raw" || true; } >> "$kept"
  # T85 P0: the budget ledgers are the account the four-layer assertions read, and
  # they print ONCE (layer A, at startup) or on the captured frame (layers C and D,
  # and the attach) -- both outside the tail window. Measured on the first full run:
  # "budget: prepare" = 0 occurrences after trimming, so seven layer-A assertions
  # read <none> and failed against a product that was working. Same for the frame's
  # own size reading, which the speed block compares.
  { grep -aE "budget:|capture:|attach: |scene: weaponAttach" "$raw" || true; } >> "$kept"
  { grep -a "scene: begin tick=" "$raw" | tail -n 5 || true; } >> "$kept"
  { grep -a "outline: item" "$raw" | tail -n 200 || true; } >> "$kept"
  { grep -a "title: " "$raw" | tail -n 5 || true; } >> "$kept"
  tail -n "${TRACE_KEEP_TAIL:-2000}" "$raw" >> "$kept"
  if ! grep -aq "outline: draw" "$kept"; then
    echo "  trace $(basename "$prefix"): NOT trimmed (summary lines did not survive); raw kept" >&2
    rm -f "$kept"; return 0
  fi
  mv "$kept" "$raw"
  after=$(stat -c %s "$raw" 2>/dev/null || echo 0)
  printf "  trace %s: %s -> %s bytes\n" "$(basename "$prefix")" "$before" "$after" >&2
}
run_frame() { # run_frame <prefix> [args...]
  local prefix="$1"; shift
  # .pgm included: --capture-depth writes it beside the frame, and a stale one from
  # an earlier run would make a capture that failed to write look like evidence.
  rm -f "$prefix.bmp" "$prefix.pgm" "$prefix.trace" "$prefix.log"
  local trace
  trace="$(absolute_out "$prefix.trace")"
  seed_run_files "$prefix"
  # The speed=1 capture below is the slow one by construction (a tick near the end
  # of the recording costs its playback position in wall clock), so the timeout is
  # a knob per call rather than a number that has to fit the slowest run.
  # THIS IS THE ONLY PRODUCT LAUNCH IN THIS GATE, and the argument list is built
  # here so that --hidden-window and --audio-disabled cannot be forgotten by a
  # caller. The check below is on the FINAL argv, not on what the caller passed:
  # a guard that inspects the caller's flags protects nothing when the helper is
  # the thing that builds them.
  #   WHY IT IS HERE: on 2026-10-22 a companion gate ran the product with the
  #   default audio device during its CONTROL phase and played twenty seconds of
  #   demo audio at system volume. Every automated reading was green. The only
  #   detector was a person in the next room.
  local -a productArgs=(
    --tf-root "$TF" --hidden-window --audio-disabled --demo "$DEMO"
    --capture-tick "$TICK" --playback-speed "$SPEED" --capture-frame "$prefix.bmp"
    # T85 P0: the OUTER window rect is pinned, so the capture geometry is a constant
    # of the run instead of something a DPI event can move mid-flight. Measured on
    # 2026-11-07: a speed=1 capture came out 2560x1417 while its neighbours were
    # 1264x681, which turns a byte comparison of two frames into a comparison of two
    # window sizes. Part of the launch this helper builds, so a caller cannot forget
    # it, and the size is asserted below rather than assumed.
    --window-size 1280x720
    --settings-file "$RUN_SETTINGS" --ui-settings-file "$RUN_UI"
    # T52 (2026-10-23): the region assertions compare pixels against regions, so the
    # region has to bound what the pass DREW. Without this the product publishes the
    # entity AABB and the drawn triangles stick out of it by a 1-6 px fringe, so the
    # assertion measures its own ruler instead of the overlay. Part of the launch this
    # helper builds, so a caller cannot forget it.
    --outline-vertex-extent
    # T52 A3: the pinned fixture camera. It is part of the launch this helper
    # builds, not a caller's flag: a camera that a caller can forget is the same
    # failure that produced the empty capture frame in the first place.
    --debug-camera $CAM_EYE
    "${ANIM_ARGS[@]}"
    "${CORPSE_ARGS[@]}"
    "$@"
  )
  case " ${productArgs[*]} " in
    *" --audio-disabled "*) : ;;
    *) echo "REFUSED: run_frame builds a product launch without --audio-disabled" >&2
       echo "99"
       return 0 ;;
  esac
  # The launch itself is launch_product -- the gate's single product launch site --
  # so this helper cannot bypass the --audio-disabled check even if its own argv
  # assembly changes later.
  local rc
  rc=$(PRODUCT_TIMEOUT="${RUN_TIMEOUT:-600}" launch_product "$prefix" "${productArgs[@]}")
  trim_trace "$prefix"
  echo "$rc"
}
OFF_RC=$(run_frame "$OUT/off" --outline off)
ON_RC=$(run_frame "$OUT/on" --outline on)
TESTED_RC=$(run_frame "$OUT/tested" --outline on --outline-depth tested)
# T85 P0 review B2: the third-person attach's ledger ("attach: meshSource") is
# printed ONLY when the attach is on, and none of the runs above sets that flag --
# so reading it out of on.trace was a reading of a line that cannot exist, and the
# assertion would have been red on every build. This run carries the flag. Outline
# is OFF on purpose: the reading is about the entity-mesh budget, not about
# silhouettes, and off keeps this run from touching the overlay's pixel fixtures.
# The FLAGS matter here, and one of them was wrong twice:
#   * a BARE --player-weapon-attach is refused by the product on purpose
#     (main.cpp: "A bare switch with no value is not 'on'"), so the first version of
#     this line turned the attach OFF and read a ledger that could not exist;
#   * --player-animation is what gives the held weapon a hold point (default is
#     off; --no-player-animation in ANIM_ARGS is not even a recognised switch), and
#     without a pose there is nothing to attach.
ATTACH_RC=$(run_frame "$OUT/attach" --outline off --player-weapon-attach on --player-animation)
echo "  att rc=$ATTACH_RC bmp=$(stat -c%s "$OUT/attach.bmp" 2>/dev/null || echo 0)"
assert_eq "attach: the third-person attach run rc" "$ATTACH_RC" "0"
echo "  off rc=$OFF_RC bmp=$(stat -c%s "$OUT/off.bmp" 2>/dev/null || echo 0)"
echo "  on  rc=$ON_RC bmp=$(stat -c%s "$OUT/on.bmp" 2>/dev/null || echo 0)"
echo "  tst rc=$TESTED_RC bmp=$(stat -c%s "$OUT/tested.bmp" 2>/dev/null || echo 0)"
assert_eq "quiet off run rc" "$OFF_RC" "0"
assert_eq "quiet on run rc" "$ON_RC" "0"
assert_eq "quiet tested run rc" "$TESTED_RC" "0"

echo
echo "  -- speed invariance: the harness knob may not change the frame --"
# `--playback-speed` exists so a capture pinned near the end of the recording does
# not cost sixteen minutes of wall clock. That is only legitimate if the SPEED is
# not a rendering parameter: the same capture tick at speed=1 and at the default
# speed has to produce byte-identical frames. This is the assertion behind the
# contract sentence; mutation m6 is its proof that the comparison can go red.
#
# THE PRECONDITION IS ONE OF ITS OWN READINGS. A cross-speed comparison can only
# blame the speed if captures at the SAME speed and tick are byte-identical, and
# this gate measures exactly that above: the two independent overlay-off runs
# (`off` and `off2`) must differ by 0 bytes. That defect is documented -- see
# problems/2026-10-12-capture-tick-frame-nondeterminism.md, where the same
# product landed on ticks 200/201/202 for one requested tick before the cursor
# hold was added. Without the off/off2 reading, a difference here would be that
# old defect and this block would be attributing it to the speed.
SPEED1_START=$(date +%s)
SPEED1_RC=$(RUN_TIMEOUT="${T52_SLOW_TIMEOUT:-2400}" SPEED=1 run_frame "$OUT/speed1" --outline on)
SPEED1_TOOK=$(( $(date +%s) - SPEED1_START ))
echo "  speed=1 rc=$SPEED1_RC bmp=$(stat -c%s "$OUT/speed1.bmp" 2>/dev/null || echo 0) took=${SPEED1_TOOK}s"
assert_eq "speed=1 capture rc" "$SPEED1_RC" "0"
# THE COMPARISON IS ONLY ABOUT SPEED IF BOTH FRAMES HAVE THE SAME SHAPE. The speed=1
# run is the long one (a tick near the end of the recording costs its playback
# position in wall clock), which is exactly the window in which a display or DPI
# event can resize the window under it. Measured 2026-11-07 on this fixture: the
# speed=1 capture was 2560x1417 against 1264x681 for every other run of the same
# lease, and the byte comparison above reported it as "--playback-speed CHANGES THE
# FRAME". The product now pins the outer rect with --window-size and prints the size
# it actually captured at, so a difference here is a NAMED geometry failure instead
# of being mis-filed as a rendering one.
capture_size_of() { # capture_size_of <trace> -- the frame's "WxH", or nothing
  grep -a "capture: frame " "$1" 2>/dev/null | tail -1 |
    sed -n 's/.*width=\([0-9]*\) height=\([0-9]*\).*/\1x\2/p'
}
SPEED1_SIZE=$(capture_size_of "$OUT/speed1.trace")
ON_SIZE=$(capture_size_of "$OUT/on.trace")
echo "  frame size: speed=1 ${SPEED1_SIZE:-<none>} speed=$SPEED ${ON_SIZE:-<none>}"
assert_eq "the speed=1 capture and the speed=$SPEED capture have the SAME frame size" \
  "${SPEED1_SIZE:-<none>}" "${ON_SIZE:-<none>}"
if cmp -s "$OUT/speed1.bmp" "$OUT/on.bmp"; then
  echo "  OK   the frame is byte-identical at speed=1 and speed=${SPEED} (cmp)"
else
  echo "  FAIL --playback-speed CHANGES THE FRAME: the speed=1 capture and the speed=${SPEED}"
  echo "  capture of the same tick differ byte for byte. The harness knob is then a"
  echo "  rendering parameter and every reading taken at a speed is suspect."
  fail=1
fi

# The product's own account of the pass, read out of each trace.
outline_value() { # outline_value <trace> <field>
  # NO LINE-START ANCHOR. The product writes every trace line behind an elapsed
  # timestamp -- "33234.9 outline: draw enabled=1 ..." -- so `grep "^outline: draw"`
  # matches NOTHING and every field comes back empty. Empty is not zero and not
  # "absent": it makes `assert_eq field 0` fail and `assert_eq field 1` fail too,
  # and against a field the assertion expected to be absent it would have passed
  # silently. The fixture check below is what keeps this honest.
  grep -a "outline: draw" "$1" 2>/dev/null | tail -1 | tr " " "\n" | grep -a "^$2=" | cut -d= -f2
}
# THE HELPER IS TESTED BEFORE IT IS TRUSTED. The fixture is the product's own line
# shape, timestamp prefix included, and the helper has to read a known value out of
# it. Without this, a helper that reads nothing at all reports <none> for every
# field and the gate explains nothing about why.
printf "12.3 outline: draw enabled=1 depth=xray depthTest=0 depthWrite=0 drew=1 submitted=3\n" \
  > "$OUT/helper-selftest.trace"
# The same reader for the one-time startup lines (gpu: ...), which are printed ONCE
# per process and carry k=v fields just like the outline line.
trace_field() { # trace_field <trace> <line-match> <field>
  # The LAST line carrying the field, not the first line carrying the prefix.
  # Measured 2026-10-23: the startup prints `gpu: overlay meshes prepared=9 of 9`
  # (a progress line) BEFORE `gpu: overlay meshes prepared=9 uploaded=9 rejected=0`
  # (the reading), so -m1 matched the progress line, found no uploaded= or
  # rejected= token, and the cap A/B pair reported <none> -- "no reading" read as
  # "reading of zero" is exactly the failure this file keeps meeting. A field that
  # is printed twice is resolved to its LAST value, which is the settled one.
  grep -a "$2" "$1" 2>/dev/null | grep -a "$3=" | tail -1 | tr " " "\n" | grep -a "^$3=" | cut -d= -f2
}
# The reader needs its own proof, because it is the thing that fails silently: a
# trace where a progress line and the reading share a prefix, and the field only
# exists on the SECOND line.
printf "1.0 gpu: overlay meshes prepared=0 of 9\n2.0 gpu: overlay meshes prepared=9 uploaded=9 rejected=0\n" \
  > "$OUT/helper-tracefield.trace"
HELPER_UPLOADED=$(trace_field "$OUT/helper-tracefield.trace" "gpu: overlay meshes prepared=" uploaded)
if [ "$HELPER_UPLOADED" = "9" ]; then
  echo "  OK   trace_field skips a prefix-sharing line that lacks the field"
else
  echo "  FAIL trace_field took the wrong line: got '${HELPER_UPLOADED:-<empty>}' instead of 9"
  fail=1
fi
HELPER_SUBMITTED=$(outline_value "$OUT/helper-selftest.trace" submitted)
if [ "$HELPER_SUBMITTED" = "3" ]; then
  echo "  OK   outline_value reads the product line shape (timestamp prefix, k=v tokens)"
else
  echo "  FAIL outline_value cannot read a timestamped trace line: got '${HELPER_SUBMITTED:-<empty>}' instead of 3"
  fail=1
fi
# The two player regions, as pixel boxes, come from the PASS, not from this
# script guessing: the trace prints every submitted item with its entity, its
# team and its projected position, and this loop turns each into a box using
# the same map-to-box transform the renderer uses.
echo "  -- the pass, from the product's own trace --"
for field in enabled depth drew ordinal vmOrdinal orderOk silhouettes submitted red blue unknown noMesh noOrigin disabled samples presses owner refused; do
  echo "  on.$field=$(outline_value "$OUT/on.trace" "$field")"
done
assert_eq "overlay enabled in the on run" "$(outline_value "$OUT/on.trace" enabled)" "1"
assert_eq "overlay drew in the on run" "$(outline_value "$OUT/on.trace" drew)" "1"
assert_ge "silhouettes drawn (on)" "$(outline_value "$OUT/on.trace" silhouettes)" "1"
assert_ge "silhouettes submitted (on)" "$(outline_value "$OUT/on.trace" submitted)" "1"
# WHERE THE POSE CAME FROM, as a reading rather than a remark. Two sources exist
# (the world pass's mesh for this frame, or the overlay's own bind-pose upload)
# and the item records which one answered. The claim this pair makes is exactly
# one sentence: every submitted silhouette has a declared source, and the number
# that says "bind" is the number that goes to zero on the day the world pass
# starts drawing player bodies -- a gate that REQUIRED bind > 0 would go red on
# the day the product got better, so this one does not.
POSE_WORLD=$(outline_value "$OUT/on.trace" poseWorld)
POSE_BIND=$(outline_value "$OUT/on.trace" poseBind)
SUBMITTED_ON=$(outline_value "$OUT/on.trace" submitted)
echo "  on.poseWorld=${POSE_WORLD:-<none>} on.poseBind=${POSE_BIND:-<none>}"
assert_eq "every submitted silhouette declares a pose source" \
  "$(( ${POSE_WORLD:-0} + ${POSE_BIND:-0} ))" "${SUBMITTED_ON:-0}"
# AND EVERY SILHOUETTE HAS A BODY. The pair above is satisfied by "I did not know
# where this entity is" as easily as by "here it is": an item whose entity index
# matches no draw in this frame is still SUBMITTED, it just gets no bone range and no
# pose source. That is exactly the defect this reading was added for (T52 A3b,
# 2026-11-02): the overlay was publishing thirteen silhouettes for players the world
# pass had dropped as corpses, and the pose split reported it as a skeleton problem.
# poseJoinMissed counts items that matched no draw; zero is the contract.
assert_eq "silhouettes with no body behind them (overlay on)" \
  "$(outline_value "$OUT/on.trace" poseJoinMissed)" "0"
# THE WORLD PASS'S MODEL SET, AS A BUDGET READING AND NOT AS A PINNED NUMBER.
#
# Until T85 P0 (2026-11-07) this block asserted uploaded == 59, "measured before the
# outline uploaded anything". That 59 was a coincidence of the reference recording,
# and the defect this task fixes is exactly that the set was whatever sorted first:
# main.cpp now admits candidates into a bounded 64-slot prepare budget BY DELIVERY
# PRIORITY and prints the account on one line ("budget: prepare"). Pinning 59 again
# would pin the old first-come-first-served cut -- the defect itself -- and would go
# red the day a held weapon's world model is admitted ahead of a prop, which is the
# improvement. What IS a contract, and is asserted below:
#   * layer A's capacity is 64 and was NOT raised (the handoff rejects "raise the
#     constant" as a fix: that moves the cut instead of bounding it);
#   * every candidate is admitted or deferred -- none vanishes, so the line is an
#     account and not a sample;
#   * admitted never exceeds capacity;
#   * built equals the count the world pass itself publishes, and every built mesh
#     is uploaded (no silent drop between layer A and layer B).
# The absolute count is PRINTED and not asserted: a recording with a different
# entity set is allowed to prepare a different number of models.
PREP_CAP=$(trace_field "$OUT/on.trace" "budget: prepare" capacity)
PREP_CANDIDATES=$(trace_field "$OUT/on.trace" "budget: prepare" candidates)
PREP_ADMITTED=$(trace_field "$OUT/on.trace" "budget: prepare" admitted)
PREP_BUILT=$(trace_field "$OUT/on.trace" "budget: prepare" built)
PREP_DEFERRED=$(trace_field "$OUT/on.trace" "budget: prepare" deferred)
PREP_FAILED=$(trace_field "$OUT/on.trace" "budget: prepare" failed)
WORLD_MODELS_PREPARED=$(sed -n "s/.*gpu: models done \([0-9][0-9]*\) uploaded=.*/\1/p" "$OUT/on.trace" | head -1)
WORLD_MODELS_UPLOADED=$(trace_field "$OUT/on.trace" "gpu: models done" uploaded)
echo "  layer A: capacity=${PREP_CAP:-<none>} candidates=${PREP_CANDIDATES:-<none>} admitted=${PREP_ADMITTED:-<none>} built=${PREP_BUILT:-<none>} deferred=${PREP_DEFERRED:-<none>} failed=${PREP_FAILED:-<none>}"
echo "  world models: prepared=${WORLD_MODELS_PREPARED:-<none>} uploaded=${WORLD_MODELS_UPLOADED:-<none>}"
# PRESENCE BEFORE EQUALITY. assert_eq passes when BOTH sides are unreadable (both
# fall back to <none>), so an equal-to-equal assertion on two readings that come
# from the same missing trace line is a green light with no reading behind it.
# Every ledger this block reads gets its own "the line exists and carries a value"
# assertion first; the equalities below are only meaningful after it.
assert_ge "layer A: the prepare ledger line is present in the trace" "$(grep -a -c 'budget: prepare' "$OUT/on.trace" || true)" "1"
assert_ge "layer A: the prepare ledger read a candidate set (a number, not <none>)" "${PREP_CANDIDATES:-0}" "1"
assert_ge "world pass published a model count (gpu: models done)" "${WORLD_MODELS_PREPARED:-0}" "1"
assert_eq "layer A: the prepare budget is 64 (not raised -- a bound, not a moved cut)" "${PREP_CAP:-<none>}" "64"
assert_eq "layer A: every candidate is admitted or deferred (the ledger accounts for all of them)" "$(( ${PREP_ADMITTED:-0} + ${PREP_DEFERRED:-0} ))" "${PREP_CANDIDATES:-<none>}"
assert_le "layer A: admitted never exceeds the budget" "${PREP_ADMITTED:-<none>}" "${PREP_CAP:-0}"
# The world pass publishes its own built count ("gpu: models done N") and the
# prepare ledger publishes built/failed. Comparing the two BUILT numbers was the
# same expression read twice (both are preparedEntityMeshes.size()), so it is
# replaced by the account that can actually fail: every admitted model is either
# built or its build failure is counted, and nothing is admitted and then lost.
assert_eq "layer A: every admitted model is built or its failure is counted (no admission vanishes)" "$(( ${PREP_BUILT:-0} + ${PREP_FAILED:-0} ))" "${PREP_ADMITTED:-<none>}"
assert_eq "layer A: no admitted world model failed to build" "${PREP_FAILED:-<none>}" "0"
assert_eq "layer A -> B: every prepared world model is uploaded (nothing silently dropped)" "${WORLD_MODELS_UPLOADED:-<none>}" "${WORLD_MODELS_PREPARED:-<none>}"
# LAYERS B, C AND D EACH GET THEIR OWN BOUND AND THEIR OWN LINE.
# Before this task the chain read as one number (96) in three different places and
# none of them published what it held. Now: the cache has a capacity and a resident
# count, this frame's instance list has a capacity and an accepted count, and the
# draw loop has a budget and a drawn count. Each is asserted against its OWN line,
# so "the frame is short" can be attributed to a layer instead of to a constant.
MESH_CAP=$(trace_field "$OUT/on.trace" "budget: meshTable" capacity)
MESH_RESIDENT=$(trace_field "$OUT/on.trace" "budget: meshTable" resident)
MESH_EVICTIONS=$(trace_field "$OUT/on.trace" "budget: meshTable" evictions)
MESH_REFUSALS=$(trace_field "$OUT/on.trace" "budget: meshTable" capacityRefusals)
MESH_REUPLOADS=$(trace_field "$OUT/on.trace" "budget: meshTable" reuploads)
INST_CAP=$(trace_field "$OUT/on.trace" "budget: instances" capacity)
INST_ACCEPTED=$(trace_field "$OUT/on.trace" "budget: instances" accepted)
INST_SLOTS=$(trace_field "$OUT/on.trace" "budget: instances" slots)
INST_SUBMITTED=$(trace_field "$OUT/on.trace" "budget: instances" submitted)
INST_MISSING=$(grep -a "budget: instances" "$OUT/on.trace" | grep -ao "missingMesh:[0-9]*" | tail -1 | cut -d: -f2)
DRAW_CAP=$(trace_field "$OUT/on.trace" "budget: draw" capacity)
DRAW_PLAYERS=$(trace_field "$OUT/on.trace" "budget: draw" playerDrawn)
DRAW_PROPS=$(trace_field "$OUT/on.trace" "budget: draw" propDrawn)
DRAW_ATTACH=$(trace_field "$OUT/on.trace" "budget: draw" attachDrawn)
DRAW_DROPPED_PLAYERS=$(trace_field "$OUT/on.trace" "budget: draw" droppedPlayers)
DRAW_DROPPED_PROPS=$(trace_field "$OUT/on.trace" "budget: draw" droppedProps)
# The attach ledger comes from the DEDICATED attach run, not from on.trace.
# Review B2: "attach: meshSource" is only printed when --player-weapon-attach is
# on, and on.trace never passes it; reading it there was an assertion with no
# possible reading behind it (both sides <none> every time).
INST_DEFERRED=$(grep -a "budget: instances" "$OUT/on.trace" | grep -ao "deferredNotResident:[0-9]*" | tail -1 | cut -d: -f2)
INST_BAD=$(grep -a "budget: instances" "$OUT/on.trace" | grep -ao "badMesh:[0-9]*" | tail -1 | cut -d: -f2)
INST_NONFINITE=$(grep -a "budget: instances" "$OUT/on.trace" | grep -ao "nonFinite:[0-9]*" | tail -1 | cut -d: -f2)
INST_BUDGET=$(grep -a "budget: instances" "$OUT/on.trace" | grep -ao "budget:[0-9]*" | tail -1 | cut -d: -f2)
DRAW_PLAYER_BUDGET=$(trace_field "$OUT/on.trace" "budget: draw" playerBudget)
DRAW_PROP_BUDGET=$(trace_field "$OUT/on.trace" "budget: draw" propBudgetAfterAttach)
DRAW_ATTACH_RESERVED=$(trace_field "$OUT/on.trace" "budget: draw" attachReserved)
ATTACH_SUPPLIED=$(trace_field "$OUT/attach.trace" "attach: meshSource" supplied)
ATTACH_REFUSED=$(trace_field "$OUT/attach.trace" "attach: meshSource" refused)
echo "  layer B: capacity=${MESH_CAP:-<none>} resident=${MESH_RESIDENT:-<none>} evictions=${MESH_EVICTIONS:-<none>} capacityRefusals=${MESH_REFUSALS:-<none>} reuploads=${MESH_REUPLOADS:-<none>}"
echo "  layer C: capacity=${INST_CAP:-<none>} submitted=${INST_SUBMITTED:-<none>} accepted=${INST_ACCEPTED:-<none>} slots=${INST_SLOTS:-<none>} rejectedMissingMesh=${INST_MISSING:-<none>} deferredNotResident=${INST_DEFERRED:-<none>} badMesh=${INST_BAD:-<none>} nonFinite=${INST_NONFINITE:-<none>} budget=${INST_BUDGET:-<none>}"
echo "  layer D: capacity=${DRAW_CAP:-<none>} players=${DRAW_PLAYERS:-<none>} props=${DRAW_PROPS:-<none>} attach=${DRAW_ATTACH:-<none>} droppedPlayers=${DRAW_DROPPED_PLAYERS:-<none>} droppedProps=${DRAW_DROPPED_PROPS:-<none>}"
echo "  attach mesh source: supplied=${ATTACH_SUPPLIED:-<none>} refused=${ATTACH_REFUSED:-<none>}"
# Same rule as layer A: presence first, so "resident <= capacity" cannot be 0 <= 0
# on two absent readings (the :-0 fallbacks below make that comparison pass).
assert_ge "layer B: the meshTable ledger line is present in the trace" "$(grep -a -c 'budget: meshTable' "$OUT/on.trace" || true)" "1"
assert_ge "layer B: the meshTable ledger names a capacity" "${MESH_CAP:-0}" "1"
assert_ge "layer C: the instances ledger line is present in the trace" "$(grep -a -c 'budget: instances' "$OUT/on.trace" || true)" "1"
assert_ge "layer C: the instances ledger names a capacity" "${INST_CAP:-0}" "1"
assert_ge "layer C: the instances ledger names how many instances were submitted" "${INST_SUBMITTED:-0}" "1"
assert_ge "layer D: the draw ledger line is present in the trace" "$(grep -a -c 'budget: draw' "$OUT/on.trace" || true)" "1"
assert_ge "layer D: the draw ledger names a capacity" "${DRAW_CAP:-0}" "1"
assert_ge "attach: the meshSource ledger line is present in the attach run's trace" "$(grep -a -c 'attach: meshSource' "$OUT/attach.trace" || true)" "1"
# Review B2 follow-up: presence is not enough. The attach run must have SUPPLIED
# something -- a ledger that only ever reports refused=0 supplied=0 is the old
# "silently empty hands" with better spelling.
assert_ge "attach: the mesh source actually supplied a held-weapon world model" "${ATTACH_SUPPLIED:-0}" "1"
assert_eq "layer B: the cache bound is 96 (kept -- raising it is the rejected fix)" "${MESH_CAP:-<none>}" "96"
assert_le "layer B: resident never exceeds the bound" "${MESH_RESIDENT:-<none>}" "${MESH_CAP:-0}"
assert_eq "layer B: no capacity refusal at the shipped bound (the held weapons fit)" "${MESH_REFUSALS:-<none>}" "0"
assert_eq "layer B: no key was re-uploaded (an attach mesh is uploaded once)" "${MESH_REUPLOADS:-<none>}" "0"
assert_eq "layer C: the instance bound is published as its own number" "${INST_CAP:-<none>}" "96"
assert_eq "layer C: every draw's mesh exists (an evicted key never reaches the instance list)" "${INST_MISSING:-<none>}" "0"
# A CLOSED ACCOUNT OVER SUBMITTED INSTANCES, not over slots.
#
# The first version of this block compared the accepted+refused sum against
# slots, and the first full run measured 96 against 236. Both numbers are real;
# they count different things. `slots` is packed.size() -- one instance ROW per
# material run -- while a model with three runs is ONE submitted instance. So the
# ledger now publishes `submitted` (draws.size()) and the account closes there:
# every instance handed to layer C is either drawn or refused for one of the four
# NAMED reasons. A submitted instance that is neither is a silent drop.
assert_eq "layer C: every submitted instance is accepted or refused for a named reason" \
  "$(( ${INST_ACCEPTED:-0} + ${INST_MISSING:-0} + ${INST_DEFERRED:-0} + ${INST_BAD:-0} + ${INST_NONFINITE:-0} + ${INST_BUDGET:-0} ))" \
  "${INST_SUBMITTED:-<none>}"
# ...and the rows written must cover the accepted instances: a bucket writes at
# least one row, so slots >= accepted is a one-way bound that fails only if a
# drawn instance produced no geometry.
assert_ge "layer C: every accepted instance wrote at least one instance row" "${INST_SLOTS:-0}" "${INST_ACCEPTED:-0}"
assert_eq "layer C: no submitted instance was dropped for a budget reason at the shipped bound" "${INST_BUDGET:-<none>}" "0"
assert_eq "layer D: no player body was dropped by the draw budget" "${DRAW_DROPPED_PLAYERS:-<none>}" "0"
# drawn <= capacity was a code identity (the loop's own condition). The account
# that can fail: the three SUB-budgets the loop actually ran with add up to the
# one capacity, and the attach's reservation is taken out of the prop budget
# without touching the player budget (that is how a player body keeps its slot).
assert_eq "layer D: player + prop + attach sub-budgets add up to the one draw capacity" \
  "$(( ${DRAW_PLAYER_BUDGET:-0} + ${DRAW_PROP_BUDGET:-0} + ${DRAW_ATTACH_RESERVED:-0} ))" "${DRAW_CAP:-0}"
assert_le "layer D: no player draw exceeds its reserved budget" "${DRAW_PLAYERS:-<none>}" "${DRAW_PLAYER_BUDGET:-0}"
assert_le "layer D: no attach draw exceeds its reserved budget" "${DRAW_ATTACH:-<none>}" "${DRAW_ATTACH_RESERVED:-0}"
assert_eq "attach: no held-weapon world mesh was refused (the three non-target weapon models no longer OOM)" "${ATTACH_REFUSED:-<none>}" "0"
assert_world_fixture "$OUT/world"
# ONE LEASE, ONE FRAME SIZE (T85 P0, lead m01857). Every capture in this run was
# taken by a process that pinned its outer window rect, so every bitmap on disk must
# have the same geometry -- otherwise a byte comparison between two of them is
# measuring a resize. The second half is the cross-check that makes the first half
# mean something: the size a capture reports is the client rect the window was
# CREATED at, read from the product's own "window: created" line. Two readings that
# describe the same bitmap, compared; not a constant this host happens to have.
LEASE_SIZES=$(for trace in "$OUT"/*.trace; do capture_size_of "$trace"; done | sort -u)
LEASE_SIZE_COUNT=$(printf '%s\n' "$LEASE_SIZES" | grep -c . || true)
WINDOW_CLIENT=$(trace_field "$OUT/on.trace" "window: created" client)
echo "  lease frame sizes: n=${LEASE_SIZE_COUNT:-0} distinct=$(echo "$LEASE_SIZES" | tr '\n' ' ') window-client=${WINDOW_CLIENT:-<none>}"
assert_ge "frame size: every capture of this lease printed its frame size" "$LEASE_SIZE_COUNT" "1"
assert_eq "frame size: every capture in this lease came out at ONE size" "$LEASE_SIZE_COUNT" "1"
assert_eq "frame size: the captured frame is the size of the window it was created at" \
  "$LEASE_SIZES" "${WINDOW_CLIENT:-<none>}"
# The ordinal readings. ABSOLUTE NUMBERS ARE NOT THE CONTRACT.
#
# 6/7 was the pass-registry snapshot until 2026-10-23, when T53's projectile pass was
# merged ahead of this one and the same behaviour produced 7/8. The contract is
# RELATIVE -- the outline must draw before the pass that clears depth -- so these
# readings assert the relation and RECORD the snapshot. Three things are checked,
# and they fail for different defects:
#   1. the relation: the product's own orderOk=1 PLUS the gate's own
#      ordinal < vmOrdinal, so the gate is not merely echoing the product;
#   2. self-consistency: the number is read from the line that says owner=outline
#      and drew=1, and two captures of one build agree on it -- a number taken from
#      some other pass's line would pass (1) and fail here;
#   3. it can move: m4 relocates the pass and the ordinal must differ from this
#      build's, asserted in the mutation section.
FIXED_ORD=$(outline_value "$OUT/on.trace" ordinal)
assert_ge "overlay took a pass slot" "${FIXED_ORD:-0}" "1"
assert_eq "the ordinal is read from the overlay pass's own line" "$(outline_value "$OUT/on.trace" owner)" "outline"
assert_eq "the pass that took the slot is the pass that drew" "$(outline_value "$OUT/on.trace" drew)" "1"
# "vmOrdinal" is only set when drawViewModel() RUNS, and that pass returns early
# unless a viewmodel was requested and its mesh uploaded. A plain demo capture
# requests none, so on the "on" frame vmOrdinal is 0 and orderOk is 0 -- and that 0
# is CORRECT: "before a pass that did not run" is not a statement this frame can
# make. Reading it as a failure would be the same class of error as reading an
# empty grep as "the value is zero".
VM_ORD_ON=$(outline_value "$OUT/on.trace" vmOrdinal)
if [ "${VM_ORD_ON:-0}" -ge 1 ]; then
  assert_lt "pass order: overlay ordinal is before the viewModel slot" "${FIXED_ORD:-0}" "${VM_ORD_ON}"
  assert_eq "pass order (overlay before viewModel)" "$(outline_value "$OUT/on.trace" orderOk)" "1"
else
  echo "  NOT-PAIR on frame: the ViewModel pass did not run (vmOrdinal=0); the pair is"
  echo "  asserted on the dedicated --viewmodel capture below instead."
fi
# ...and here is that capture: the same tick, the same overlay, plus a viewmodel,
# which is the only state in which "the overlay draws before the pass that clears
# depth" is observable at all. This is a capture, not a mutation: nothing changes
# but the argument that makes the ViewModel pass exist.
run_frame "$OUT/vm" --outline on --viewmodel models/weapons/v_models/v_scattergun_scout.mdl
VM_ORD=$(outline_value "$OUT/vm.trace" vmOrdinal)
VM_OVERLAY_ORD=$(outline_value "$OUT/vm.trace" ordinal)
echo "  vm.ordinal=${VM_OVERLAY_ORD:-<none>} vm.vmOrdinal=${VM_ORD:-<none>} vm.orderOk=$(outline_value "$OUT/vm.trace" orderOk) vm.drew=$(outline_value "$OUT/vm.trace" drew) fixed.ordinal=${FIXED_ORD:-<none>}"
assert_ge "viewModel took a pass slot" "${VM_ORD:-0}" "1"
assert_eq "pass order (overlay before viewModel, with a viewmodel)" \
  "$(outline_value "$OUT/vm.trace" orderOk)" "1"
assert_lt "overlay ordinal is before the viewModel slot (gate's own comparison)" \
  "${VM_OVERLAY_ORD:-0}" "${VM_ORD:-0}"
assert_eq "overlay ordinal is the same slot in both captures of one build" \
  "${VM_OVERLAY_ORD:-<none>}" "${FIXED_ORD:-<none>}"
echo "  ordinal: absolute numbers are a registry snapshot, recorded not asserted (fixed=$FIXED_ORD vmOrdinal=$VM_ORD)"
assert_eq "overlay enabled in the off run" "$(outline_value "$OUT/off.trace" enabled)" "0"
assert_eq "overlay drew in the off run" "$(outline_value "$OUT/off.trace" drew)" "0"
# The toggle's effect is a COUNT: with the switch off, every sampled player is
# refused BECAUSE the overlay is off, so the frame cannot read as "there were
# no players".
assert_ge "players refused because the overlay is off" "$(outline_value "$OUT/off.trace" disabled)" "1"
assert_eq "overlay enabled in the tested run" "$(outline_value "$OUT/tested.trace" enabled)" "1"
assert_eq "tested run depth mode" "$(outline_value "$OUT/tested.trace" depth)" "depthTested"
# The two halves of the contract's depth statement, read back from the state
# OBJECTS the pass bound (GetDesc at init) rather than repeated from the lines
# that created them. "x-ray" with no depth test AND no depth write is what makes
# an outline visible through a wall without changing what the wall lets through
# afterwards; the second half is invisible in any screenshot.
assert_eq "x-ray: depth test" "$(outline_value "$OUT/on.trace" depthTest)" "0"
# And the same statement about the state that was BOUND, read from that state's
# own desc -- the field mutation m2 has to move.
assert_eq "x-ray: the bound state has the depth test off" "$(outline_value "$OUT/on.trace" stateBound)" "0"
# The write half, MEASURED 2026-10-22 22:51 and therefore no longer asserted as
# a zero: the pass is created with DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO
# (native_renderer.cpp:1327) and D3D11 reports DepthWriteMask = ALL for that same
# state (`stateWrite=1` in the draw line, printed from the GetDesc readback).
# Both readings are in the trace and they are both true: when DepthEnable is
# FALSE the runtime does not preserve the write mask, and the mask is inert --
# "no depth write" is enforced by DepthEnable = FALSE, not by the mask.
# So the register here is what the state says, and the CONTRACT sentence is
# reported as UNVERIFIED-BEHAVIOURALLY (report section 4.2) rather than passed:
# asserting depthWrite==0 would be asserting a field the runtime rewrites.
echo "  x-ray depth write, as D3D11 reports it: depthWrite=$(outline_value "$OUT/on.trace" depthWrite) stateWrite=$(outline_value "$OUT/on.trace" stateWrite)  (NOT asserted, see report 4.2)"
assert_eq "depth-tested control: depth test" "$(outline_value "$OUT/tested.trace" depthTest)" "1"
# The control still ASSERTS its write bit, and that is the paired reading which
# makes the x-ray result readable: the depth-tested state is created from the
# SAME descriptor with DepthEnable = TRUE, and D3D11 reports its mask as ZERO --
# so the runtime preserves the mask when the test is on and rewrites it to ALL
# when the test is off. Same code, same mask, two answers: the difference is the
# runtime, not the overlay.
assert_eq "depth-tested control: depth write" "$(outline_value "$OUT/tested.trace" depthWrite)" "0"
echo "  tested state: depthTest=$(outline_value "$OUT/tested.trace" depthTest) depthWrite=$(outline_value "$OUT/tested.trace" depthWrite)"

# The per-item rows, printed so a red run reports WHERE the pass put each
# silhouette instead of only how many it drew. "no players on screen" and "two
# players on screen but at the world origin" fail the same assertions and this
# is the line that tells them apart.
echo
echo "  -- per-item rows (on) --"
grep -a "outline: item" "$OUT/on.trace" 2>/dev/null | sed "s/^/    /" || true

echo
echo "  -- non-interference: only the silhouette's own pixels may differ --"
# `regions` is derived from the drawn items; every pixel outside them must be
# byte-identical between the on and off frames of the same tick.
REGIONS=$("$PY" "$PWD/outline-regions.py" --trace "$OUT/on.trace" --csv)
REGION_ARG=""
if [ -n "$REGIONS" ]; then REGION_ARG="regions=$REGIONS"; fi
echo "  regions=$REGIONS"
printf "%s" "$REGIONS" > "$OUT/regions.txt"
# Both tolerances are computed BEFORE the frame comparison, because the comparison
# takes the product's number as its own pass rule. Two readings of the same thing
# that disagree (a green assertion next to "VERDICT FAIL") is a shape of error this
# project has already paid for once.
TOLERANCE=$("$PY" "$PWD/outline-regions.py" --trace "$OUT/on.trace" --expand-tolerance)
echo "  tolerance: ${TOLERANCE:-<none>}  (the product's own shell, summed)"
TOL_VALUE=$(echo "$TOLERANCE" | grep -o "tolerance=[0-9.]*" | cut -d= -f2)
# The reader takes an integer; the product's number arrives formatted ("1134.000").
# One conversion, here, so the tolerance the comparison is given and the tolerance
# the assertion checks are the same number (2026-11-02: passing "1134.000" made the
# reader raise ValueError, print nothing, and the gate read the tolerance assertion
# as green off a process that had no verdict).
TOL_INT=$(awk -v t="${TOL_VALUE:-0}" 'BEGIN { printf "%d", t + 0 }')
TOL_MISSING=$(echo "$TOLERANCE" | grep -o "missing=[0-9]*" | cut -d= -f2)
BAND=$("$PY" "$PWD/outline-regions.py" --trace "$OUT/on.trace" --expand-band)
echo "  expand band: ${BAND:-<none>}  (0 < expandPx <= 4 is an outline)"
NI=$("$PY" outline-frame-check.py noninterference on="$OUT/on.bmp" off="$OUT/off.bmp" $REGION_ARG max_outside="${TOL_INT:-0}" 2>&1)
NI_RC=$?
echo "  $NI" | sed "s/^/    /"
# The reader's own verdict, before any number is read out of its output: a reader
# that exited non-zero printed neither a verdict nor trustworthy counts, and the
# awk comparison below would happily call the missing number zero.
assert_eq "the non-interference reader ran" "$NI_RC" "0"
assert_contains "the non-interference reader's own verdict" "$NI" "VERDICT PASS"
# PRECONDITION, written before the numbers it guards: two runs may only be
# compared once they are shown to be looking at the same scene. The rule is
# mechanical -- a scene mismatch is not confined to where a player stands, so it
# shows up as a difference spread across the frame, and the total is checked
# against a fraction of the frame rather than against a guess at how big an
# outline is. Measured 2026-10-23: the on/off pair differs by 484 pixels of
# 861k, i.e. the same camera saw the same world twice; a pair of runs captured
# 300 frames apart differs across the whole frame and fails here, with the
# reason stated, instead of being reported as "the overlay painted outside its
# boxes" -- which is the shape of error this file has already made once.
NI_OUT=$(echo "$NI" | grep -o "diff_outside=[0-9]*" | cut -d= -f2)
NI_IN=$(echo "$NI" | grep -o "diff_inside=[0-9]*" | cut -d= -f2)
NI_TOTAL=$(( ${NI_OUT:-0} + ${NI_IN:-0} ))
NI_LIMIT=$(( 1264 * 681 / 50 ))
if [ "$NI_TOTAL" -lt "$NI_LIMIT" ]; then
  echo "  OK   precondition: the two runs saw the same scene (${NI_TOTAL} differing pixels < 2% of the frame)"
else
  echo "  FAIL precondition: the two runs did not see the same scene (${NI_TOTAL} >= ${NI_LIMIT} differing pixels)"
  echo "       every region assertion below measures the camera, not the overlay, so this run proves nothing"
  fail=1
fi
# The tolerance is the PRODUCT'S number, not the gate's. Each silhouette publishes
# how many pixels its own shell may stick out (expand=, computed in the renderer from
# the expansion constant and that instance's model->world scale), so the allowed
# number of out-of-region pixels is the sum of (perimeter x expand) over the items.
# A constant written here would make the assertion "the overlay is not wrong by more
# than we guessed" instead of "the overlay is where the product says it is", and the
# next change to the expansion would not be reflected in it.
assert_eq "items whose shell expansion the product published" "${TOL_MISSING:-<none>}" "0"
# The shell thickness has to sit in a band, and the band is about what the picture
# MEANS, not about a tolerance: 1-2 px is an outline on a player ~4 px tall, past
# ~4 px it is a halo. This is the assertion the last round was missing -- the shell
# was 60% of a player height and nothing in the gate said "that is not an outline".
# It reads the product's own number, so it also guards the constant itself.
assert_eq "silhouettes whose shell the product did not publish" "$(echo "$BAND" | grep -o "missing=[0-9]*" | cut -d= -f2)" "0"
assert_eq "silhouettes with a shell of zero pixels (the outline is not drawn)" "$(echo "$BAND" | grep -o "below=[0-9]*" | cut -d= -f2)" "0"
assert_eq "silhouettes with a shell above the outline band (> 4 px)" "$(echo "$BAND" | grep -o "above=[0-9]*" | cut -d= -f2)" "0"
WITHIN=$(awk -v out="${NI_OUT:-0}" -v tol="${TOL_INT:-0}" 'BEGIN { printf "%d", ((out + 0) <= (tol + 0)) ? 1 : 0 }')
assert_eq "painted pixels outside every drawn region, within the product's own tolerance" "${WITHIN:-<none>}" "1"
assert_ge "differing pixels INSIDE the drawn regions" "$(echo "$NI" | grep -o "diff_inside=[0-9]*" | cut -d= -f2)" "1"

# ---- D1 attribution: which instance carries which entity, and where its paint is
# The pixel readings above say the boxes and the silhouettes disagree; they cannot
# say which side is wrong. These two readings are the attribution:
#   readback  -- the entity index carried by each DRAWN instance (read back out of
#                the buffer the draw call consumes), against the entity the CPU
#                published for that slot. A mismatch means the tables are not the
#                same tables and every box is a box for some other entity.
#   attr      -- per published item, the centroid of the nearest painted blob and
#                whether it falls inside the published box. Centroid inside the box
#                puts the error in the projection; centroid outside it puts the
#                error in the world-to-screen step that derives the box.
RB_TOTAL=$(grep -a -c "overlay: readback i=" "$OUT/on.trace" 2>/dev/null || true)
RB_BAD=$(grep -a "overlay: readback i=" "$OUT/on.trace" 2>/dev/null | grep -c "match=0" 2>/dev/null || true)
echo "  readback: drawn instances=${RB_TOTAL:-0} identity mismatches=${RB_BAD:-0}"
grep -a "overlay: readback i=" "$OUT/on.trace" 2>/dev/null | tail -4 | sed "s/^/    /"
# The transform the draw consumes, compared with the one the box was computed from.
# A mismatch here means the published box describes a different placement than the
# pixels; a match means the residue is in the projection or in the comparison itself.
ROWCHECK=$(grep -a "overlay: rowcheck " "$OUT/on.trace" | tail -1)
echo "  ${ROWCHECK:-<no rowcheck line>}"
ROWMATCH=$(echo "$ROWCHECK" | grep -o " match=[01]" | tr -d " ")
assert_eq "the instance row the draw consumes equals the one the box was built from" "${ROWMATCH:-<none>}" "match=1"
assert_ge "drawn instances read back from the GPU buffer" "${RB_TOTAL:-0}" "1"
assert_eq "each drawn instance carries the entity the CPU published for its slot" "${RB_BAD:-<none>}" "0"
ATTR=$("$PY" "$PWD/outline-regions.py" --attribute --trace "$OUT/on.trace" --on "$OUT/on.bmp" --off "$OUT/off.bmp" 2>/dev/null || true)
echo "  attribution: published item -> box -> centroid of the nearest painted blob"
printf "%s\n" "${ATTR:-}" | sed "/^$/d" | sed "s/^/    /"
ATTR_ROWS=$(printf "%s\n" "${ATTR:-}" | grep -c "^attr " 2>/dev/null || true)
ATTR_OUT=$(printf "%s\n" "${ATTR:-}" | grep -c "inside=0" 2>/dev/null || true)
assert_ge "published items with an attribution row" "${ATTR_ROWS:-0}" "1"
# The ruler is itself a reading: every published region must come from the projected
# VERTICES of the mesh the draw consumes (extent=1). If the product falls back to the
# entity AABB, or the switch is dropped from the launch, this goes red -- a region that
# claims to be the drawn extent but is the sampler hull would quietly re-open the
# question this fix closes.
EXTENT=$("$PY" "$PWD/outline-regions.py" --extent --trace "$OUT/on.trace" 2>/dev/null || true)
echo "  region source: ${EXTENT:-<none>}"
assert_ge "drawn items on the capture frame" "$(echo "$EXTENT" | grep -o "items=[0-9]*" | cut -d= -f2)" "1"
assert_eq "items whose region is the drawn extent" "$(echo "$EXTENT" | grep -o "extent1=[0-9]*" | cut -d= -f2)" "$(echo "$EXTENT" | grep -o "items=[0-9]*" | cut -d= -f2)"
echo "  attribution verdict: ${ATTR_OUT:-0} of ${ATTR_ROWS:-0} published boxes do NOT contain their nearest blob centroid"
# ---- the depth-stencil state, read back from the device -------------------
# The overlay must not write depth, and the reading must be wired to the state. Two
# assertions, because the failure modes are different: `agree` catches a reading whose
# source is not the device (it went red with both sides at 1 before this line existed),
# and the mask catches a state that really writes depth. A mutation that flips the mask
# moves the second; one that breaks the wiring moves the first.
STATE_ENABLE=$(outline_value "$OUT/on.trace" stateEnable)
STATE_MASK=$(outline_value "$OUT/on.trace" stateMask)
STATE_CREATED=$(outline_value "$OUT/on.trace" stateCreated)
STATE_AGREE=$(outline_value "$OUT/on.trace" agree)
STATE_WRITE=$(outline_value "$OUT/on.trace" depthWrite)
STATE_EFFECT=0
if [ "${STATE_ENABLE:-0}" -eq 1 ] && [ "${STATE_MASK:-0}" -ne 0 ]; then STATE_EFFECT=1; fi
echo "  depth-stencil: enable=${STATE_ENABLE:-?} mask=${STATE_MASK:-?} created=${STATE_CREATED:-?} agree=${STATE_AGREE:-?} depthWrite=${STATE_WRITE:-?} (enable&&mask = $STATE_EFFECT)"
assert_eq "the depth-stencil readback agrees with the description it was created from" "${STATE_AGREE:-<none>}" "1"
assert_eq "the x-ray state does not write depth" "${STATE_WRITE:-<none>}" "0"
assert_eq "the published depthWrite is the effect of the state (enable AND mask), not the mask alone" "${STATE_WRITE:-<none>}" "$STATE_EFFECT"
echo "  (0 outside => the boxes follow the paint and the error is in the projection;"
echo "   any outside => the box derivation, world->screen, is the defect -- see report 9.5)"


echo
echo "  -- team colour, both regions, both directions --"
TEAM_REGIONS=$("$PY" "$PWD/outline-regions.py" --trace "$OUT/on.trace" --team)
RED_REGION=$(echo "$TEAM_REGIONS" | grep -o "red=[0-9,]*" | cut -d= -f2)
BLUE_REGION=$(echo "$TEAM_REGIONS" | grep -o "blue=[0-9,]*" | cut -d= -f2)
echo "  red=$RED_REGION blue=$BLUE_REGION"
# Box height against ink height, side by side, because the D1 residue is a shape
# question before it is a threshold question: the published boxes are 1-4 px tall on
# this frame, so if the team-coloured ink is 10 px tall the box is not measuring the
# thing that was drawn. This line is the cheap first look; the itemdiag line carries
# the mesh and world heights behind it.
RED_H=$(echo "$RED_REGION" | awk -F, '{ if (NF==4) print $4-$2+1 }')
BLUE_H=$(echo "$BLUE_REGION" | awk -F, '{ if (NF==4) print $4-$2+1 }')
echo "  heights: published boxes hmin=$(echo "$EXTENT" | grep -o "hmin=[0-9]*" | cut -d= -f2) hmax=$(echo "$EXTENT" | grep -o "hmax=[0-9]*" | cut -d= -f2) | team ink red=${RED_H:-?} px blue=${BLUE_H:-?} px"
# A region that could not be derived would make the assertion below vacuous,
# so the gate refuses to pretend: no region is a FAILURE, not a skip.
if [ -z "$RED_REGION" ] || [ -z "$BLUE_REGION" ]; then
  echo "  FAIL no red and blue player regions could be derived from the trace"
  fail=1
else
  TEAM=$("$PY" outline-frame-check.py team frame="$OUT/on.bmp" red_region="$RED_REGION" blue_region="$BLUE_REGION" 2>&1)
  echo "  $TEAM" | sed "s/^/    /"
  assert_ge "red pixels in the red player's region" "$(echo "$TEAM" | grep -o "red_inside=[0-9]*" | cut -d= -f2)" "8"
  assert_ge "blue pixels in the blue player's region" "$(echo "$TEAM" | grep -o "blue_inside=[0-9]*" | cut -d= -f2)" "8"
  assert_eq "blue pixels in the RED player's region" "$(echo "$TEAM" | grep -o "red_wrong_team=[0-9]*" | cut -d= -f2)" "0"
  assert_eq "red pixels in the BLUE player's region" "$(echo "$TEAM" | grep -o "blue_wrong_team=[0-9]*" | cut -d= -f2)" "0"
fi

echo
echo "=== 5/6 depth awareness: the occluded region, three ways ==="
# The region is the part of the player boxes that the frame's OWN depth says is
# behind world geometry. It is derived from the depth buffer the capture took,
# not from a guess about where the map is, and the gate refuses to run the
# assertion on an empty region: "nothing was occluded" would make it vacuous.
# The depth capture is what "behind a wall" is measured against, so it is taken
# from the SAME tick and camera as the boxes rather than assumed from geometry.
#
# THE OCCLUSION FIXTURE IS A SECOND CAMERA (2026-11-02). The pinned camera above
# looks at the two clusters in the open, so at its tick no player is behind world
# geometry and the region came back empty for four rounds -- the one reading this
# gate still carried as NOT VERIFIED. The camera below (eye 5138 -1130 10, pitch
# 2.5, yaw -90, on the ground ~900 units from the same clusters, same tick) was
# found by scanning 8 positions: it sees 7 items, six of them in the open, and one
# (blue entity 8) behind the map's own geometry -- zero team-coloured pixels in
# its published box on the depth-tested frame, while the x-ray frame paints its
# neighbours with 291-600. The tick and the camera are pinned as a PAIR, for the
# reason the pinned fixture above documents: the tick moved 300 between rounds and
# a camera measured against the old tick silently missed every player.
#
# Four runs at that fixture, same tick, same window: x-ray (publishes the boxes
# and the "you can see through walls" frame), depth-tested, overlay-off (the
# comparison target for "draws nothing here"), and the depth capture. The capture
# writes BOTH the 8-bit --world-depth view whose saturation T26 asserts AND the
# 16-bit --capture-depth image the occlusion comparison needs.
DEPTH_CAM="${T52_DEPTH_CAM:-5138 -1130 10 2.5 -90}"
CAM_EYE_PINNED=$CAM_EYE
CAM_EYE=$DEPTH_CAM
echo "  occlusion fixture: tick $TICK, camera eye/pitch/yaw $DEPTH_CAM"
DEPTH_ON_RC=$(run_frame "$OUT/depthon" --outline on)
DEPTH_TESTED_RC=$(run_frame "$OUT/depthtested" --outline on --outline-depth tested)
DEPTH_OFF_RC=$(run_frame "$OUT/depthoff" --outline off)
DEPTH_RC=$(run_frame "$OUT/worlddepth" --outline off --world-depth --capture-depth "$OUT/worlddepth.pgm")
CAM_EYE=$CAM_EYE_PINNED
echo "  world-depth run rc=$DEPTH_RC (x-ray=$DEPTH_ON_RC depth-tested=$DEPTH_TESTED_RC off=$DEPTH_OFF_RC)"
assert_eq "occlusion fixture: the x-ray run rc" "$DEPTH_ON_RC" "0"
assert_eq "occlusion fixture: the depth-tested run rc" "$DEPTH_TESTED_RC" "0"
assert_eq "occlusion fixture: the overlay-off run rc" "$DEPTH_OFF_RC" "0"
# THE DEPTH IMAGE HAS TO BE PROVED BEFORE ONE PIXEL OF IT IS EVIDENCE.
#
# On 2026-10-23 this run was given a 600 s timeout, never reached the requested tick,
# wrote no BMP, the occluded region came back empty and this section fell to NOT
# VERIFIED -- so mutation m2 had nothing to move, for a reason the log never named.
# A capture that did not produce the depth it needs has not shown that the depth test
# works: that is RED, not "unproven". Four readings, in the order they can fail:
#   exists / arrived at the tick / is a scalar image / has geometry in it.
DEPTH_STATS=$("$PY" "$PWD/outline-regions.py" --depth-stats --depth "$OUT/worlddepth.bmp" 2>&1)
echo "  depth image: ${DEPTH_STATS:-<none>}"
depth_stat() { printf '%s' "${DEPTH_STATS:-}" | grep -o "$1=[^ ]*" | cut -d= -f2; }
DEPTH_TICK=$(grep -a "capture: armed" "$OUT/worlddepth.trace" 2>/dev/null | tail -1 | sed -n 's/.*frame_tick=\([0-9]*\).*/\1/p')
DEPTH_OK=1
assert_eq "world-depth run rc" "$DEPTH_RC" "0"
if [ "${DEPTH_TICK:-none}" = "$TICK" ]; then
  echo "  OK   the depth capture reached the requested tick ($TICK)"
else
  LAST_SCENE=$(grep -a "scene: begin tick=" "$OUT/worlddepth.trace" 2>/dev/null | tail -1 | sed -n 's/.*tick=\([0-9]*\).*/\1/p')
  echo "  FAIL capture never reached tick $TICK (last scene tick ${LAST_SCENE:-none};"
  echo "       capture armed frame_tick=${DEPTH_TICK:-none}, run rc=$DEPTH_RC)"
  fail=1; DEPTH_OK=0
fi
DEPTH_BYTES=$(depth_stat bytes)
DEPTH_NONGREY=$(depth_stat nonGreyPct)
DEPTH_NONZERO=$(depth_stat nonzeroPct)
DEPTH_UNIQUE=$(depth_stat uniqueSampled)
if [ "${DEPTH_BYTES:-0}" -ge 100000 ] 2>/dev/null; then
  echo "  OK   depth image bytes written = $DEPTH_BYTES (>= 100000)"
else
  echo "  FAIL depth image bytes written = ${DEPTH_BYTES:-<none>}, expected >= 100000"
  fail=1; DEPTH_OK=0
fi
case "${DEPTH_NONGREY:-1}" in
  0|0.0|0.00) echo "  OK   the depth image is a scalar image (0% non-grey)" ;;
  *) echo "  FAIL the depth image is not a depth image: ${DEPTH_NONGREY:-<none>}% of pixels carry colour"
     echo "       (the world shader writes one value into R, G and B; a colour frame of this scene is ~17%)"
     fail=1; DEPTH_OK=0 ;;
esac
if [ "${DEPTH_NONZERO%%.*}" -ge 1 ] 2>/dev/null; then
  echo "  OK   the depth image has geometry in it (${DEPTH_NONZERO}% of pixels are not black)"
else
  echo "  FAIL the depth image is blank: ${DEPTH_NONZERO:-<none>}% of pixels are not black, expected >= 1%"
  fail=1; DEPTH_OK=0
fi
if [ "${DEPTH_UNIQUE:-0}" -ge 2 ] 2>/dev/null; then
  echo "  OK   the depth image carries more than one sampled depth value ($DEPTH_UNIQUE)"
else
  echo "  FAIL the depth image is one value (${DEPTH_UNIQUE:-none} sampled): nothing was rendered"
  fail=1; DEPTH_OK=0
fi
# THE 8-BIT VIEW CANNOT ANSWER THE QUESTION, so the capture also writes the depth
# buffer itself. Everything past a few world units reads 255 in --world-depth (T26
# asserts exactly that saturation, so it has to stay), while the region this
# section needs is a wall ~900 units from the camera in front of a player ~1200
# away: 8 levels of answer, all of them 255. --capture-depth writes the D24S8
# buffer as 16-bit PGM -- 256x the resolution, ~177 steps across that gap -- and
# these three readings are what make the file evidence instead of a file.
PGM_STATS=$("$PY" "$PWD/outline-regions.py" --pgm-stats --depth-pgm "$OUT/worlddepth.pgm" 2>&1)
echo "  capture-depth image: ${PGM_STATS:-<none>}"
pgm_stat() { printf '%s' "${PGM_STATS:-}" | grep -o "$1=[^ ]*" | cut -d= -f2; }
PGM_BYTES=$(pgm_stat bytes)
PGM_WIDTH=$(pgm_stat width)
PGM_HEIGHT=$(pgm_stat height)
PGM_DISTINCT=$(pgm_stat distinct)
PGM_MIN=$(pgm_stat min)
PGM_MAX=$(pgm_stat max)
if [ "${PGM_BYTES:-0}" -ge 100000 ] 2>/dev/null; then
  echo "  OK   capture-depth bytes written = $PGM_BYTES (>= 100000)"
else
  echo "  FAIL capture-depth bytes written = ${PGM_BYTES:-<none>}, expected >= 100000"
  echo "       (a depth capture that wrote no 16-bit image cannot prove occlusion at all)"
  fail=1; DEPTH_OK=0
fi
if [ "${PGM_WIDTH:-0}" -ge 800 ] && [ "${PGM_HEIGHT:-0}" -ge 600 ] 2>/dev/null; then
  echo "  OK   capture-depth image size = ${PGM_WIDTH}x${PGM_HEIGHT}"
else
  echo "  FAIL capture-depth image size = ${PGM_WIDTH:-none}x${PGM_HEIGHT:-none}, expected the capture window"
  fail=1; DEPTH_OK=0
fi
if [ "${PGM_DISTINCT:-0}" -gt 256 ] 2>/dev/null; then
  echo "  OK   capture-depth carries $PGM_DISTINCT distinct depths (> 256, so it is the buffer and not the 8-bit view)"
else
  echo "  FAIL capture-depth carries ${PGM_DISTINCT:-none} distinct depths, expected > 256"
  echo "       (the 8-bit --world-depth view can only hold 256 values; a 'depth map' with"
  echo "       <= 256 of them is the saturated view wearing this file's name)"
  fail=1; DEPTH_OK=0
fi
if [ "${PGM_MAX:-0}" -gt "${PGM_MIN:-0}" ] 2>/dev/null; then
  echo "  OK   capture-depth spans ${PGM_MIN}..${PGM_MAX} (near surface to the clear plane)"
else
  echo "  FAIL capture-depth spans ${PGM_MIN:-none}..${PGM_MAX:-none}: one value, nothing was drawn"
  fail=1; DEPTH_OK=0
fi
OCCLUDED=""
if [ "$DEPTH_OK" -eq 0 ]; then
  echo "  FAIL depth section: the depth evidence is not usable, so the occluded region is"
  echo "       NOT measured. Red on purpose: a capture that did not arrive is not an"
  echo "       unproven claim, it is a measurement that did not happen."
else
  # The boxes come from the x-ray run of this fixture and the depth from the
  # capture of the same fixture; the 16-bit image is what the comparison uses.
  OCCLUDED=$("$PY" "$PWD/outline-regions.py" --trace "$OUT/depthon.trace" --occluded --depth-pgm "$OUT/worlddepth.pgm" 2> "$OUT/occluded.log")
  sed "s/^/    /" "$OUT/occluded.log" 2>/dev/null || true
fi
echo "  occluded region: ${OCCLUDED:-<none>}"
if [ "$DEPTH_OK" -eq 0 ]; then
  : # already red above; the region could not be measured
elif [ -z "$OCCLUDED" ]; then
  # The fixture was CHOSEN because a player is behind the map there, so an empty
  # region is no longer "unproven" -- it is the fixture not being what it says it
  # is, which is red. (Before 2026-11-02 this branch was NOT VERIFIED, truthfully:
  # the pinned camera looked at the clusters in the open.)
  echo "  FAIL no drawn player pixel is behind world geometry at the occlusion fixture"
  echo "       (tick $TICK, camera $DEPTH_CAM). That pair was picked because a player IS"
  echo "       behind the map there (blue entity 8, measured 2026-11-02), so an empty region"
  echo "       means the camera or the tick moved, or the depth capture did not line up."
  echo "       The item rows are printed as the evidence for what this frame contains:"
  grep -a "outline: item " "$OUT/depthon.trace" 2>/dev/null | tail -6 | sed "s/^/    /"
  fail=1
else
  DEPTH=$("$PY" outline-frame-check.py depth region="$OCCLUDED" \
    xray="$OUT/depthon.bmp" tested="$OUT/depthtested.bmp" off="$OUT/depthoff.bmp" 2>&1)
  echo "  $DEPTH" | sed "s/^/    /"
  assert_ge "team-coloured pixels drawn THROUGH the wall (x-ray)" "$(echo "$DEPTH" | grep -o "occluded_xray=[0-9]*" | cut -d= -f2)" "8"
  assert_eq "team-coloured pixels through the wall when depth-tested" "$(echo "$DEPTH" | grep -o "occluded_tested=[0-9]*" | cut -d= -f2)" "0"
  assert_eq "depth-tested frame vs overlay-off frame, in the occluded region" "$(echo "$DEPTH" | grep -o "occluded_tested_diff=[0-9]*" | cut -d= -f2)" "0"
fi

echo "=== 6/6 the toggle: one slot, one owner, foreign presses refused ==="
# The state machine is asserted in the probe (section 1), and the product's own
# counters are asserted here: the run acquired the slot as the outline and no
# foreign press was recorded, so nothing else changed the flag while the frame
# was measured.
assert_eq "overlay slot owner in the product" "$(outline_value "$OUT/on.trace" owner)" "outline"
assert_eq "foreign presses refused by the product" "$(outline_value "$OUT/on.trace" refused)" "0"
# The switch's OFF state, twice, in two independent processes. "The off frame" is
# the comparison target for every assertion above, so it has to be the same frame
# twice; comparing a file with itself proved nothing and used to sit here.
OFF2_RC=$(run_frame "$OUT/off2" --outline off)
assert_eq "second overlay-off run rc" "$OFF2_RC" "0"
OFF2_DIFF=$("$PY" outline-frame-check.py noninterference \
  on="$OUT/off2.bmp" off="$OUT/off.bmp" require_inside=0 2>&1)
assert_eq "two independent overlay-off frames differ by" "$(echo "$OFF2_DIFF" | grep -o "diff_outside=[0-9]*" | cut -d= -f2)" "0"
# And the off frame carries no team-coloured pixel inside the regions the overlay
# publishes: that is the switch's own pixel statement, and it is the reading
# mutation m3 has to move. FRAME-WIDE IS NOT THE SWITCH'S STATEMENT (T52 A3,
# 2026-11-02): on the pinned camera the map's own red brick reads 2872 red pixels,
# every one of them OUTSIDE every published box (measured on off.bmp with
# outline-frame-check.py teamregions: team_inside=0 team_outside=2872), so the old
# frame-wide assertion was measuring the map, not the toggle. The frame-wide count
# stays as a printed diagnostic; the assertion counts ink in the published regions.
OFF_TEAM=$("$PY" outline-frame-check.py teamcount frame="$OUT/off.bmp" 2>&1)
OFF_TEAM_INSIDE=$("$PY" outline-frame-check.py teamregions \
  frame="$OUT/off.bmp" regions_file="$OUT/regions.txt" 2>&1)
echo "  off frame team pixels (frame-wide, diagnostic; scene reds sit outside the boxes): $OFF_TEAM"
echo "  off frame team pixels inside the published regions: $OFF_TEAM_INSIDE"
assert_eq "team-coloured pixels inside the published regions in the overlay-off frame" \
  "$(echo "$OFF_TEAM_INSIDE" | grep -o "team_inside=[0-9]*" | cut -d= -f2)" "0"
echo
echo "=== 5b/6 the corpse rule: no silhouette for a body the world pass dropped ==="
# THE OTHER HALF OF THE SAME CONTRACT, measured on the same frame with the rule back
# on. The 5/6 assertions above are taken with corpses shown, because that is the only
# way this pinned tick has bodies to join; this run takes the flag away and asks what
# the overlay publishes when the world pass drops nineteen of the twenty samples.
# Three readings, one defect each:
#   removed>=1   -- the rule actually fires on this frame (if it stops firing the run
#                   is measuring a different frame, and the two assertions below
#                   would be green for the wrong reason);
#   submitted==kept -- what the overlay publishes is the set the rule kept, not the
#                   set it sampled: the defect was publishing the sampled set;
#   poseJoinMissed==0 -- and the silhouettes that survive still have bodies, which
#                   is the reading a "drop the filter" mutant moves to 18.
CORPSE_ARGS=()
CORPSE_RC=$(run_frame "$OUT/corpse" --outline on)
CORPSE_ARGS=(--spectate-show-corpses)
echo "  corpse-rule run rc=$CORPSE_RC bmp=$(stat -c%s "$OUT/corpse.bmp" 2>/dev/null || echo 0)"
assert_eq "corpse-rule run rc" "$CORPSE_RC" "0"
CORPSE_LINE=$(grep -a "overlay: corpseFilter removed=" "$OUT/corpse.trace" 2>/dev/null | tail -1)
CORPSE_REMOVED=$(printf "%s" "$CORPSE_LINE" | sed -n "s/.*removed=\([0-9]*\).*/\1/p")
CORPSE_KEPT=$(printf "%s" "$CORPSE_LINE" | sed -n "s/.*kept=\([0-9]*\).*/\1/p")
echo "  corpse rule: ${CORPSE_LINE:-<no corpseFilter line>}"
assert_ge "players the corpse rule removed from the silhouette set" "${CORPSE_REMOVED:-0}" "1"
assert_eq "silhouettes submitted == the samples the rule kept" \
  "$(outline_value "$OUT/corpse.trace" submitted)" "${CORPSE_KEPT:-none}"
assert_eq "silhouettes with no body behind them (corpse rule on)" \
  "$(outline_value "$OUT/corpse.trace" poseJoinMissed)" "0"


echo


# ---------------------------------------------------------------------------
# MUTATIONS
#
# Each case reintroduces ONE defect the feature removed, rebuilds, re-runs the
# readings that case is about, and requires the reading to move AWAY from the
# fixed-build value. Then the source is restored and the restore is verified
# with cmp -- a mutation whose restore is not byte-exact leaves a defect in the
# tree for whoever builds next, which is worse than not running it.
#
# The six cases and the reading each must turn red:
#   m1 exchange RED and BLUE        -> team: red_wrong_team OR blue_wrong_team
#   m2 depth-test only (no x-ray)   -> depth: occluded_xray drops from >0 to 0,
#                                      and the published x-ray depth TEST flips
#   m3 overlay always on and leaks  -> the OFF frame gains team pixels
#   m5 the overlay writes depth     -> the published x-ray depth WRITE flips
#   m6 captured tick depends on the
#      playback speed               -> the speed=1 and speed=default frames differ
#   m4 draw after the ViewModel     -> orderOk=0 and ordinal != the fixed build's
#   m7 no class model fallback      -> submitted drops to 0, noMesh rises to samples
#   m8 item keeps the bare path     -> silhouettes drawn drop to 0 while submitted stays >0
#   m9 bind mesh preferred over the
#      world mesh                   -> the probe self-test's ordering case fails
#
# ...and one PAIRED READING that is not a mutation (nothing is supposed to be red):
#   cap A/B  the renderer's mesh limit 64 vs 96 -> at 64 the overlay's class meshes
#            are refused (rejected>0, keys logged); at 96 rejected=0 and
#            uploaded==prepared. Without the pair, "raise the cap" is an
#            assumption rather than a measurement, and the refusal it fixes is
#            invisible in every other reading in this file.
# ---------------------------------------------------------------------------
if [ "$MUTATION" = "1" ]; then
  echo
  # ALIGNMENT: every mutation capture is launched with the SAME arguments as the
  # forward run it is compared with. The only difference is the mutated source.
  #   m1 --outline on    vs forward on      (colour only)
  #   m2 --outline on    vs forward on      (the x-ray depth state only; the
  #                                         picture it must then resemble is
  #                                         the forward depth-tested frame)
  #   m3 --outline off   vs forward off     (the toggle guard only)
  #   m5 --outline on    vs forward on      (the depth write mask only)
  #   m6 --outline on    vs forward on      (the captured tick only; the SPEED
  #                                         differs by design and is the
  #                                         assertion subject)
  #   m4 --outline on    vs forward on      (the call site only)
  #   m7 --outline on    vs forward on      (the class fallback only)
  #   m8 --outline on    vs forward on      (the key the item carries only)
  # A mutation that also changed a flag would move a reading for a reason the
  # mutation is not about. That is how a control phase elsewhere in this repo grew
  # an --audio-disabled omission while its author was busy describing the one
  # difference he had in mind.
  # The case count is written next to the cases that exist, not remembered: the list
  # grew from 8 to 12 (m-meta included) and a heading that says 8 while 12 run is
  # itself a reading that stopped matching the mechanism.
  echo "=== mutation: 14 cases (m1-m14) plus m-meta, each rebuilt and required to go red ==="
  MUT_SRC="native/src/entity_overlay.cpp"
  MUT_MAIN="native/src/main.cpp"
  MUT_RENDERER="native/src/native_renderer.cpp"
  MUT_FRAME="native/src/native_renderer_frame.cpp"
  MUT_ENTITY="native/src/native_renderer_entity.cpp"
  BACKUP_DIR="$OUT/mutation-backup"
  mkdir -p "$BACKUP_DIR"
  cp "$MUT_SRC" "$BACKUP_DIR/entity_overlay.cpp"
  cp "$MUT_MAIN" "$BACKUP_DIR/main.cpp"
  cp "$MUT_RENDERER" "$BACKUP_DIR/native_renderer.cpp"
  cp "$MUT_FRAME" "$BACKUP_DIR/native_renderer_frame.cpp"
  cp "$MUT_ENTITY" "$BACKUP_DIR/native_renderer_entity.cpp"
  mut_fail=0
  mut_invalid=0

  # Applies one literal replacement, and requires the pattern to occur EXACTLY
  # ONCE. A pattern that is not found is a hard error (the mutation would silently
  # not happen and the run would report "the reading did not move"), and so is a
  # pattern that matches twice: the second copy would be left mutated, the restore
  # would hide it, and the next mutation's reading would be taken on code this
  # section did not intend. The sources are checked for this before the run as
  # well; this is the in-run half of that rule.
  patch_in() { # patch_in <file> <find> <replace>
    "$PY" - "$1" "$2" "$3" <<'PYEOF'
import io, sys
path, find, repl = sys.argv[1], sys.argv[2], sys.argv[3]
raw = io.open(path, "rb").read()
crlf = b"\r\n" in raw
data = raw.decode("utf-8")
if crlf:
    find = find.replace("\n", "\r\n")
    repl = repl.replace("\n", "\r\n")
count = data.count(find)
if count == 0:
    sys.exit("PATTERN NOT FOUND: " + find[:70].replace("\r", "\\r").replace("\n", "\\n"))
if count != 1:
    sys.exit("PATTERN MATCHED " + str(count) + " TIMES, refusing to mutate: "
             + find[:70].replace("\r", "\\r").replace("\n", "\\n"))
io.open(path, "wb").write(data.replace(find, repl, 1).encode("utf-8"))
PYEOF
    if [ $? -ne 0 ]; then
      echo "  MUTATION-INVALID: the anchor above no longer matches the source, so no"
      echo "                    verdict after this point would be trustworthy. Sources"
      echo "                    are restored; the suite stops here."
      mut_fail=1
      restore_all || true
      exit 3
    fi
  }

  # The stop above is only worth having if it fires. Break the anchor on purpose in
  # a scratch copy and check three things: non-zero status, the message, and a file
  # that is byte-identical afterwards. Cheap enough to run every time. 2026-10-23:
  # m2 printed PATTERN NOT FOUND, was built and run unmutated anyway, and reported
  # MUTATION-GREEN. A green from a build that was never mutated is worse than a red:
  # a red gets read.
  patch_in_self_test() {
    local tmp rc=0 out expected
    tmp=$(mktemp)
    printf "const int keep = 1;\n" > "$tmp"
    expected=$(cksum < "$tmp")
    out=$( ( patch_in "$tmp" "const int gone = 1;" "const int gone = 2;" ) 2>&1 ) || rc=$?
    if [ "$rc" -eq 0 ]; then
      echo "  FAIL patch_in self-test: an anchor that does not exist reported success"; rm -f "$tmp"; return 1
    fi
    if ! printf '%s' "$out" | grep -q "PATTERN NOT FOUND"; then
      echo "  FAIL patch_in self-test: no PATTERN NOT FOUND message"; rm -f "$tmp"; return 1
    fi
    if ! printf '%s' "$out" | grep -q "MUTATION-INVALID"; then
      echo "  FAIL patch_in self-test: the failure did not say the case is invalid"; rm -f "$tmp"; return 1
    fi
    if [ "$(cksum < "$tmp")" != "$expected" ]; then
      echo "  FAIL patch_in self-test: the file changed despite the failure"; rm -f "$tmp"; return 1
    fi
    rm -f "$tmp"
    echo "  OK   patch_in stops the suite when an anchor no longer matches (rc=$rc, source untouched)"
    return 0
  }

  # The restore is a copy back plus a cmp, which is the same evidence
  # `git checkout` would give (bytes for bytes) without depending on the
  # tree's commit state.
  restore_all() {
    cp "$BACKUP_DIR/entity_overlay.cpp" "$MUT_SRC"
    cp "$BACKUP_DIR/main.cpp" "$MUT_MAIN"
    cp "$BACKUP_DIR/native_renderer.cpp" "$MUT_RENDERER"
    cp "$BACKUP_DIR/native_renderer_frame.cpp" "$MUT_FRAME"
    cp "$BACKUP_DIR/native_renderer_entity.cpp" "$MUT_ENTITY"
    local ok=1
    for pair in "entity_overlay.cpp:$MUT_SRC" "main.cpp:$MUT_MAIN" "native_renderer.cpp:$MUT_RENDERER" "native_renderer_frame.cpp:$MUT_FRAME" "native_renderer_entity.cpp:$MUT_ENTITY"; do
      local backup="${pair%%:*}" target="${pair##*:}"
      if cmp -s "$BACKUP_DIR/$backup" "$target"; then
        echo "  restored $(basename "$target") byte-identical (cmp)"
      else
        echo "  FAIL restore of $target is NOT byte-identical"; ok=0; mut_fail=1
      fi
    done
    return $((1 - ok))
  }

  rebuild_product() { # rebuild_product <label>
    if bash build-target.sh tf2_demo_native entity_overlay_probe > "$OUT/mutation-build.log" 2>&1; then
      echo "  built ($(grep -c "error C" "$OUT/mutation-build.log" || true) compiler errors)"
      return 0
    fi
    # A mutation that does not COMPILE is not a caught mutation: nothing was
    # measured, so nothing was proven. This is counted as a suite failure, never
    # as a red reading -- the distinction matters because "the build broke" is the
    # easiest way for a mutation suite to look like it works.
    echo "  MUTATION-INVALID $1: the mutated source does not compile, so the mutation"
    echo "  proved nothing. A compile failure is not a red reading (T54 lost a run to"
    echo "  exactly this: the mutation needed an #include before it could be measured)."
    grep -a -E "error C|fatal error|LNK[0-9]+" "$OUT/mutation-build.log" | head -6
    cp "$OUT/mutation-build.log" "$OUT/mutation-build.failed" 2>/dev/null
    mut_fail=1
    mut_invalid=$((mut_invalid + 1))
    return 1
  }

  # A mutation passes only if the reading is no longer the fixed-build value.
  must_move() { # must_move <label> <reading> <fixed> <observed>
    if [ -n "${4:-}" ] && [ "$4" != "$3" ]; then
      echo "  MUTATION-RED   $1: $2=$4 (fixed=$3)"
    else
      echo "  MUTATION-GREEN $1: $2=${4:-<none>} (fixed=$3) -- the mutation did NOT move the reading"
      mut_fail=1
    fi
  }

  # Before any case: prove the guard that stops an unapplied mutation actually
  # stops. Every verdict below is worth nothing if a rotted anchor can still
  # produce a green.
  echo
  echo "  -- helper self-tests --"
  patch_in_self_test || mut_fail=1

  # ---- the invalid-mutation guard, exercised on purpose -------------------
  # A green from a build that was never mutated is worse than a red, and this suite
  # reported exactly that once (m2, whose patch had silently stopped compiling for a
  # whole version). So the guard itself is a case: put a #error in the source, and
  # require the suite to call it MUTATION-INVALID, to count it as a failure, and to
  # leave the tree intact afterwards.
  echo
  echo "  -- m-meta: a mutant that does not compile is INVALID, not a green --"
  META_FAIL_BEFORE=$mut_fail
  META_INVALID_BEFORE=$mut_invalid
  printf "\n#error T52 meta self-test: this mutant must not compile\n" >> "$MUT_RENDERER"
  if rebuild_product m-meta > "$OUT/meta.log" 2>&1; then
    echo "  MUTATION-META FAIL: a source containing #error compiled, so the guard cannot work"
    mut_fail=1
  elif ! grep -q "MUTATION-INVALID" "$OUT/meta.log"; then
    echo "  MUTATION-META FAIL: the failure was not classified MUTATION-INVALID"
    sed "s/^/      /" "$OUT/meta.log" | head -4
    mut_fail=1
  elif [ "$mut_invalid" -le "$META_INVALID_BEFORE" ]; then
    echo "  MUTATION-META FAIL: an invalid mutant was not counted as a suite failure"
    mut_fail=1
  else
    echo "  OK   a source that does not compile reads MUTATION-INVALID and fails the run"
  fi
  # The failure was EXPECTED, so its accounting is put back: this case proves the
  # guard fires; it is not itself a broken mutation.
  mut_fail=$META_FAIL_BEFORE
  mut_invalid=$META_INVALID_BEFORE
  restore_all || true
  rebuild_product "restore after m-meta" || true

  # ---- m1: exchange the two team colours ---------------------------------
  echo
  echo "  -- m1: RED and BLUE exchanged --"
  # A real exchange in ONE patch, not two: applied one after the other, the first
  # would make both lines identical and the second would change the line the
  # first had just written -- which is not an exchange at all, it is a no-op that
  # would read green for the wrong reason. The 4-line block is contiguous, so it
  # is replaced whole: RED gets BLUE's values and BLUE gets RED's.
  patch_in "$MUT_SRC" \
"      colour.rgba[0] = 0.92f; colour.rgba[1] = 0.16f; colour.rgba[2] = 0.14f; colour.rgba[3] = 1.0f;
      return colour;
    case OverlayTeam::Blue:
      colour.rgba[0] = 0.16f; colour.rgba[1] = 0.42f; colour.rgba[2] = 0.95f; colour.rgba[3] = 1.0f;" \
"      colour.rgba[0] = 0.16f; colour.rgba[1] = 0.42f; colour.rgba[2] = 0.95f; colour.rgba[3] = 1.0f;
      return colour;
    case OverlayTeam::Blue:
      colour.rgba[0] = 0.92f; colour.rgba[1] = 0.16f; colour.rgba[2] = 0.14f; colour.rgba[3] = 1.0f;" || mut_fail=1
  if rebuild_product m1; then
    M1_RC=$(run_frame "$OUT/m1" --outline on)
    M1_TEAM=$("$PY" outline-frame-check.py team frame="$OUT/m1.bmp" \
      red_region="$RED_REGION" blue_region="$BLUE_REGION" 2>&1)
    echo "    $M1_TEAM" | sed "s/^/      /"
    M1_WRONG=$(echo "$M1_TEAM" | grep -o "red_wrong_team=[0-9]*" | cut -d= -f2)
    M1_WRONG_B=$(echo "$M1_TEAM" | grep -o "blue_wrong_team=[0-9]*" | cut -d= -f2)
    # Either direction moving is enough: the two colours were swapped, so the
    # red player's region now holds blue and the blue player's holds red.
    M1_MOVED=$(("${M1_WRONG:-0}" + "${M1_WRONG_B:-0}"))
    must_move "m1" "team pixels in the WRONG region" "0" "$M1_MOVED"
  fi
  # No rebuild here: the next mutation patches this restored source and
  # builds it, so a build now would prove the same thing twice and cost a
  # full compile. The restore itself is verified by cmp inside restore_all.
  restore_all || true

  # A mutation that did not apply must not be allowed to report a verdict.
  # patch_in already prints PATTERN NOT FOUND and sets mut_fail; 2026-10-23 m2
  # slid past it, was built and run unmutated, and reported MUTATION-GREEN --
  # an unapplied mutation wearing a pass. From here the case stops and says so.
  mut_applied() {
    if [ "$1" -ne 0 ]; then
      echo "  MUTATION-INVALID: the mutation did not apply, so this case proves nothing"
      return 1
    fi
    return 0
  }

  # ---- m2: depth-test only (the depth awareness the overlay exists for) --
  echo
  echo "  -- m2: the pass depth-tests everything, so walls hide the outlines --"
  # The anchor is the ternary that FEEDS the bound state, not the call that binds
  # it: 2026-10-23 the bind site was refactored to read the state into
  # `boundDepthState` so the pass could publish which state it actually bound,
  # and the old anchor silently stopped matching. patch_in printed PATTERN NOT
  # FOUND, the case was built and run unmutated anyway, and it reported GREEN --
  # an unapplied mutation wearing a pass. The anchor moves with the code; the
  # hard stop below is what keeps this from recurring.
  patch_in "$MUT_FRAME" \
"      ? overlayXRayDepthState_.Get() : overlayDepthTestedState_.Get();" \
"      ? overlayDepthTestedState_.Get() : overlayDepthTestedState_.Get();"
  M2_APPLIED=$?
  if ! mut_applied "$M2_APPLIED"; then
    mut_fail=1
  elif rebuild_product m2; then
    # The occlusion reading below compares this mutant's frame against the fixed
    # build's DEPTH FIXTURE camera, so the mutant has to be captured there too:
    # the same region numbers describe a different picture at the pinned camera.
    M2_CAM_EYE=$CAM_EYE
    CAM_EYE=$DEPTH_CAM
    M2_RC=$(run_frame "$OUT/m2" --outline on)
    CAM_EYE=$M2_CAM_EYE
    # m2 may only have a conclusion when section 5's depth evidence is usable. If the
    # depth capture did not arrive, saying "m2 has nothing to prove" would hide a
    # red behind a NOT VERIFIED; if it arrived but no player is behind geometry,
    # that IS undecidable and the item rows are printed as the evidence.
    if [ "${DEPTH_OK:-0}" -ne 1 ]; then
      echo "    M2-UNDECIDABLE depth=unusable: section 5 is red, so there is no region to move."
      echo "    m2 is NOT proven by this run -- the red above is the finding, not this line."
    elif [ -n "$OCCLUDED" ]; then
      M2_DEPTH=$("$PY" outline-frame-check.py depth region="$OCCLUDED" \
        xray="$OUT/m2.bmp" tested="$OUT/depthtested.bmp" off="$OUT/depthoff.bmp" 2>&1)
      echo "    $M2_DEPTH" | sed "s/^/      /"
      M2_XRAY=$(echo "$M2_DEPTH" | grep -o "occluded_xray=[0-9]*" | cut -d= -f2)
      # The fixed build draws team-coloured pixels THROUGH the wall here; the
      # mutation must lose them. A mutation that merely moved the silhouette
      # would leave this at a positive number and be reported green.
      must_move "m2" "team-coloured pixels through the wall" "$(echo "$DEPTH" | grep -o "occluded_xray=[0-9]*" | cut -d= -f2)" "${M2_XRAY:-0}"
    else
      echo "    M2-UNDECIDABLE depth=valid region=empty: the depth image is real, but no drawn"
      echo "    player pixel is behind world geometry at tick ${TICK}, so an occlusion reading"
      echo "    cannot move. The item rows are the evidence for that statement:"
      grep -a "outline: item " "$OUT/on.trace" 2>/dev/null | tail -6 | sed "s/^/      /"
    fi
    # The same mutation is the proof for the depth-TEST reading, and it does not
    # depend on a single pixel: the mutant binds its x-ray list to the
    # depth-tested state, so the published flag moves from 0 to 1 even on a frame
    # where nothing is occluded.
    # The per-frame depthTest is derived from the REQUESTED mode, so a mutant that
# binds the depth-tested state for the x-ray list cannot move it -- measured
# 2026-10-22: fixed=0, mutated=0, MUTATION-GREEN on an assertion that could never
# have moved. The reading that follows the pointer actually bound is stateBound.
must_move "m2" "x-ray depth test state actually bound" "$(outline_value "$OUT/on.trace" stateBound)" "$(outline_value "$OUT/m2.trace" stateBound)"
  fi
  restore_all || true

  # ---- m3: the overlay ignores the toggle and leaks -----------------------
  echo
  echo "  -- m3: the overlay is always on and ignores the switch --"
  # The defect has two halves: the builder keeps submitting while the switch is
  # off, AND the list is marked enabled. Either half alone draws nothing (the
  # renderer refuses a disabled list), which would leave this mutation reading
  # green for a reason that is not the feature working.
  patch_in "$MUT_SRC" \
"    if (!enabled) { ++list.rejectedDisabled; continue; }" \
"    if (!enabled) { ++list.rejectedDisabled; }" || mut_fail=1
  patch_in "$MUT_SRC" \
"  list.enabled = enabled;" \
"  list.enabled = true;  /* mutation m3: the switch no longer reaches the pass */" || mut_fail=1
  if rebuild_product m3; then
    M3_RC=$(run_frame "$OUT/m3" --outline off)
    M3_TEAM=$("$PY" outline-frame-check.py teamregions \
      frame="$OUT/m3.bmp" regions_file="$OUT/regions.txt" 2>&1)
    echo "    $M3_TEAM" | sed "s/^/      /"
    M3_INSIDE=$(echo "$M3_TEAM" | grep -o "team_inside=[0-9]*" | cut -d= -f2)
    # The reading is the switch's own pixels, counted the same way the fixed build
    # is counted (inside the published regions): a frame captured with --outline
    # off that leaked the overlay cannot leave those regions empty, and zero is what
    # the fixed build's off frame reads. The regions stay the FIXED capture's -- the
    # mutant's own trace is captured with --outline off and publishes no regions,
    # and taking the regions from the mutant would let it move the ruler as well.
    must_move "m3" "team-coloured pixels inside the published regions in the --outline off frame" "0" "${M3_INSIDE:-0}"
  fi
  restore_all || true

  # ---- m5: the overlay WRITES depth (the other half of the depth state) --
  echo
  echo "  -- m5: the overlay writes depth --"
  # The contract's second half: the pass must not write depth, or a later pass
  # sees a surface the outline invented. The ViewModel pass clears depth right
  # afterwards, so this defect cannot show in a screenshot -- the reading that
  # has to move is the published state, which is exactly why it is published.
  # The mutation changes what the state DOES, not just the mask: a state whose
  # DepthEnable is FALSE writes nothing whatever its mask, and the runtime reports
  # that mask as ALL anyway (measured 2026-10-23). Mutating the mask alone therefore
  # moved nothing and m5 read green for a year-shaped reason -- the reading was
  # publishing a mask rather than an effect. Both fields move together here.
  patch_in "$MUT_RENDERER" \
"    overlayDescription.DepthEnable = FALSE;
    overlayDescription.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;" \
"    overlayDescription.DepthEnable = TRUE;   /* mutation m5: the overlay writes depth */
    overlayDescription.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;  /* mutation m5 */" || mut_fail=1
  if rebuild_product m5; then
    M5_RC=$(run_frame "$OUT/m5" --outline on)
    echo "    m5 depthTest=$(outline_value "$OUT/m5.trace" depthTest) depthWrite=$(outline_value "$OUT/m5.trace" depthWrite)"
    # The fixed value is READ from the forward run, never written as a literal: the
# first version declared "0" while the fixed build itself publishes depthWrite=1,
# so the assertion went red with no mutation at all. Measured 2026-10-22: it
# reported MUTATION-RED m5 with both runs reading 1 -- no discriminating power.
# With the value taken from $OUT/on.trace the pair reads honestly: if the mutated
# run publishes the same number this case reports that the READING cannot prove
# the mask, which is a statement about the reading and not about the product.
M5_FIXED=$(outline_value "$OUT/on.trace" depthWrite)
must_move "m5" "x-ray depth write state" "$M5_FIXED" "$(outline_value "$OUT/m5.trace" depthWrite)"
  fi
  restore_all || true

  # ---- m6: the captured TICK depends on the playback speed --------------
  echo
  echo "  -- m6: the captured tick depends on the playback speed --"
  # The hold at the requested tick is what makes the captured frame independent of
  # how fast the cursor travelled. This mutation keeps a hold but moves it when the
  # speed is above 2x, which is the defect in its most direct form: the frame is
  # then a frame of a DIFFERENT tick at the fast speed, and the speed-invariance
  # comparison above must see it. Engineering the offset (rather than deleting the
  # hold) also keeps the mutation deterministic: whether the draw cadence happens
  # to land on the requested tick is luck, and a mutation that depends on luck
  # reports green for the wrong reason about one run in N.
  patch_in "$MUT_MAIN" \
"          g_playback.tick = static_cast<std::int32_t>(captureTick);" \
"          g_playback.tick = static_cast<std::int32_t>(captureTick) + (g_playback.speed > 2.0 ? 30 : 0);  /* mutation m6 */" || mut_fail=1
  if rebuild_product m6; then
    M6_SLOW=$(RUN_TIMEOUT="${T52_SLOW_TIMEOUT:-2400}" SPEED=1 run_frame "$OUT/m6-speed1" --outline on)
    M6_FAST=$(run_frame "$OUT/m6-default" --outline on)
    echo "    m6 speed=1 rc=$M6_SLOW speed=${SPEED} rc=$M6_FAST"
    if cmp -s "$OUT/m6-speed1.bmp" "$OUT/m6-default.bmp"; then
      echo "  MUTATION-GREEN m6: the two speeds still capture identical frames, so the"
      echo "  hold is not what pins the frame and the invariance assertion measures nothing"
      mut_fail=1
    else
      echo "  MUTATION-RED   m6: without the hold, speed=1 and speed=${SPEED} capture different frames"
    fi
  fi
  restore_all || true

  # ---- m7: the player-class model fallback is removed -------------------
  echo
  echo "  -- m7: the overlay stops falling back to the class model --"
  # THIS IS THE MUTATION FOR THE DEFECT THE FEATURE ACTUALLY SHIPPED WITH. On the
  # recording this was measured on, no player carries m_nModelIndex: 21 of 21
  # sampled players have identity, team and pose and none has a model, so without
  # the class fallback every row is rejected as noMesh -- which is what the pass
  # did in 778 of 778 frames before the fix. If removing the fallback does not take
  # submitted back to zero, the fallback is not what draws the silhouettes and the
  # fix proved nothing. The reading that must move is the pass's own count.
  patch_in "$MUT_SRC" \
"      const std::string fallback =
        EntityModelResolver::defaultPlayerModelPath(sample.playerClass);" \
"      const std::string fallback;  /* mutation m7: no class fallback */" || mut_fail=1
  if rebuild_product m7; then
    M7_RC=$(run_frame "$OUT/m7" --outline on)
    echo "    m7 submitted=$(outline_value "$OUT/m7.trace" submitted) noMesh=$(outline_value "$OUT/m7.trace" noMesh)"
    must_move "m7" "silhouettes submitted without the class fallback" \
      "$(outline_value "$OUT/on.trace" submitted)" "$(outline_value "$OUT/m7.trace" submitted)"
    must_move "m7" "players rejected as noMesh without the class fallback" \
      "$(outline_value "$OUT/on.trace" noMesh)" "$(outline_value "$OUT/m7.trace" noMesh)"
  fi
  restore_all || true

  # ---- m8: the item loses the key the mesh was UPLOADED under -----------
  echo
  echo "  -- m8: the silhouette carries the bare path, not the upload key --"
  # The other half of the same defect, and the one a pixel gate cannot see: the
  # sampler now resolves a path and the join accepts "<path>#class-fallback", but
  # the ITEM has to carry that key because the renderer looks the uploaded mesh up
  # by it. Keeping the bare path leaves submitted > 0 and draws nothing -- "the
  # list is right and the frame is empty", which is the exact shape of the defect
  # this section exists to catch. submitted staying positive is asserted as the
  # positive control: the mutation must empty the FRAME, not the list.
  patch_in "$MUT_SRC" \
"    item.cacheKey = meshKey;" \
"    item.cacheKey = sample.modelPath;  /* mutation m8: the upload key is dropped */" || mut_fail=1
  if rebuild_product m8; then
    M8_RC=$(run_frame "$OUT/m8" --outline on)
    echo "    m8 submitted=$(outline_value "$OUT/m8.trace" submitted) drew=$(outline_value "$OUT/m8.trace" drew) silhouettes=$(outline_value "$OUT/m8.trace" silhouettes)"
    assert_ge "m8: items still submitted (the frame is emptied, not the list)" \
      "$(outline_value "$OUT/m8.trace" submitted)" "1"
    must_move "m8" "silhouettes DRAWN when the item loses the upload key" \
      "$(outline_value "$OUT/on.trace" silhouettes)" "$(outline_value "$OUT/m8.trace" silhouettes)"
  fi
  # ---- m9: prefer the BIND mesh over the world mesh ----------------------
  echo
  echo "  -- m9: the bind mesh is preferred over the world mesh --"
  # The defect: the join asks its own uploaded mesh FIRST instead of the mesh the
  # world pass drew this frame. On the reference recording this mutant is
  # INVISIBLE in the product frame -- there is no player mesh in the world pass,
  # so every silhouette is bind-sourced either way -- and that is exactly why the
  # ordering lives where it can be observed: a self-test case that gives both
  # sources an answer and requires the world one to win. A mutation is judged by
  # whether it turns its assertion red, not by whether it changes a picture it
  # cannot change.
  patch_in "$MUT_SRC" \
"      if (world.find(sample.modelPath) != world.end()) {" \
"      if (fallback.find(classKey) != fallback.end()) { /* mutation m9: bind first */ meshKey = classKey; poseSource = OverlayPoseSource::Bind; } else if (world.find(sample.modelPath) != world.end()) {" || mut_fail=1
  if rebuild_product m9; then
    "$PROBE" --self-test > "$OUT/m9-self-test.txt" 2>&1
    M9_FAILURES=$(v "$OUT/m9-self-test.txt" failures)
    echo "    m9 self-test failures=${M9_FAILURES:-<none>} (fixed=0)"
    must_move "m9" "probe self-test failures when the bind mesh wins" "0" "${M9_FAILURES:-0}"
  fi
  restore_all || true

  # ---- budget boundary: move layer B's capacity and watch the policy fire --
  echo
  echo "  -- budget boundary: layer B's capacity as a mutation (96 -> 24) --"
  # This block replaces the old "cap A/B" pair, which read the SAME decision from
  # two hard-coded limits (64 and 96) and asserted that the smaller one refuses.
  # That pair was not a mutation and was not supposed to be red -- it was the price
  # of a decision made observable, and the observability is KEPT here: capacity,
  # resident, evictions, capacityRefusals and reuploads are printed on
  # "budget: meshTable", the per-instance rejection reasons on "budget: instances",
  # and the draw budget on "budget: draw". What this block ADDS is the boundary as a
  # mutation: the table is rebuilt 72 slots narrower than the shipped bound and a
  # NAMED assertion must move. On the reference recording the world pass prepares
  # in the high fifties and the overlay adds nine, so 24 slots cannot hold the set:
  # the policy must EVICT, and once nothing is evictable it must REFUSE with a
  # counter -- never overfill, never silently drop.
  patch_in "$MUT_ENTITY" \
"constexpr std::size_t kEntityModelMeshCapacity = 96;" \
"constexpr std::size_t kEntityModelMeshCapacity = 24;  /* mutation: the budget boundary, moved */" || mut_fail=1
  if rebuild_product budget24; then
    B24_RC=$(run_frame "$OUT/budget24" --outline on)
    B24_CAP=$(trace_field "$OUT/budget24.trace" "budget: meshTable" capacity)
    B24_RESIDENT=$(trace_field "$OUT/budget24.trace" "budget: meshTable" resident)
    B24_EVICTIONS=$(trace_field "$OUT/budget24.trace" "budget: meshTable" evictions)
    B24_REFUSALS=$(trace_field "$OUT/budget24.trace" "budget: meshTable" capacityRefusals)
    B24_MISSING=$(grep -a "budget: instances" "$OUT/budget24.trace" | grep -ao "missingMesh:[0-9]*" | tail -1 | cut -d: -f2)
    B24_DROPPED=$(trace_field "$OUT/budget24.trace" "budget: draw" droppedPlayers)
    B24_OVERLAY_REJECTED=$(trace_field "$OUT/budget24.trace" "gpu: overlay meshes prepared=" rejected)
    # Review (e): a count alone says "something moved". The shrink control has to
    # NAME the reason, so these are read from the policy's own per-victim line.
    B24_NAMED_EVICTIONS=$(grep -a -c 'budget: eviction key=' "$OUT/budget24.trace" || true)
    B24_FIRST_EVICTION=$(grep -a 'budget: eviction key=' "$OUT/budget24.trace" | head -1)
    echo "    at capacity 24: rc=$B24_RC resident=${B24_RESIDENT:-<none>} evictions=${B24_EVICTIONS:-<none>} capacityRefusals=${B24_REFUSALS:-<none>} instances-missingMesh=${B24_MISSING:-<none>} draw-droppedPlayers=${B24_DROPPED:-<none>} overlay-rejected=${B24_OVERLAY_REJECTED:-<none>}"
    echo "    named eviction reason: ${B24_FIRST_EVICTION:-<none>}"
    assert_eq "budget boundary: the product reports the capacity it was built with" "${B24_CAP:-<none>}" "24"
    assert_le "budget boundary: the cache never exceeds its capacity" "${B24_RESIDENT:-<none>}" "24"
    assert_ge "budget boundary: the policy EVICTS instead of overfilling" "${B24_EVICTIONS:-0}" "1"
    # A counter is not a reason. The policy publishes one line per victim with the
    # key it took and what it did to that key, so the boundary reading can name
    # WHAT left the table and why the path tables were invalidated.
    assert_ge "budget boundary: every eviction names the model key it took (no anonymous drop)" "${B24_NAMED_EVICTIONS:-0}" "1"
    assert_eq "budget boundary: the named eviction is a model path, not an index" \
      "$(printf '%s' "${B24_FIRST_EVICTION:-}" | grep -c 'key=models/' || true)" "1"
    assert_eq "budget boundary: the named eviction says it unpublished and will re-supply the path" \
      "$(printf '%s' "${B24_FIRST_EVICTION:-}" | grep -c 'action=unpublish,resupply' || true)" "1"
    must_move "budget24" "instances refused for a missing mesh when the table is 24 wide" \
      "${INST_MISSING:-0}" "${B24_MISSING:-0}"
  fi
  restore_all || true
  if rebuild_product "restore after the budget boundary"; then
    B96_RC=$(run_frame "$OUT/budget96" --outline on)
    B96_CAP=$(trace_field "$OUT/budget96.trace" "budget: meshTable" capacity)
    B96_RESIDENT=$(trace_field "$OUT/budget96.trace" "budget: meshTable" resident)
    B96_EVICTIONS=$(trace_field "$OUT/budget96.trace" "budget: meshTable" evictions)
    B96_REFUSALS=$(trace_field "$OUT/budget96.trace" "budget: meshTable" capacityRefusals)
    B96_MISSING=$(grep -a "budget: instances" "$OUT/budget96.trace" | grep -ao "missingMesh:[0-9]*" | tail -1 | cut -d: -f2)
    B96_PREPARED=$(trace_field "$OUT/budget96.trace" "gpu: overlay meshes prepared=" prepared)
    B96_UPLOADED=$(trace_field "$OUT/budget96.trace" "gpu: overlay meshes prepared=" uploaded)
    B96_REJECTED=$(trace_field "$OUT/budget96.trace" "gpu: overlay meshes prepared=" rejected)
    echo "    restored: rc=$B96_RC capacity=${B96_CAP:-<none>} resident=${B96_RESIDENT:-<none>} evictions=${B96_EVICTIONS:-<none>} capacityRefusals=${B96_REFUSALS:-<none>} instances-missingMesh=${B96_MISSING:-<none>}"
    echo "    restored overlay: prepared=${B96_PREPARED:-<none>} uploaded=${B96_UPLOADED:-<none>} rejected=${B96_REJECTED:-<none>}"
    assert_eq "shipped budget: capacity is the number the source declares" "${B96_CAP:-<none>}" "96"
    assert_eq "shipped budget: nothing was evicted at the shipped capacity" "${B96_EVICTIONS:-<none>}" "0"
    assert_eq "shipped budget: no capacity refusal at the shipped capacity" "${B96_REFUSALS:-<none>}" "0"
    assert_eq "shipped budget: no instance lost a missing mesh" "${B96_MISSING:-<none>}" "0"
    assert_eq "shipped budget: every prepared class mesh is uploaded" "${B96_REJECTED:-<none>}" "0"
    assert_eq "shipped budget: uploaded count matches prepared" "${B96_UPLOADED:-<none>}" "${B96_PREPARED:-<none>}"
  fi

  # ---- m4: draw the overlay AFTER the ViewModel pass ---------------------
  echo
  echo "  -- m4: the overlay draws after the ViewModel pass --"
  # The defect: the pass is MOVED after drawViewModel(), which clears the depth
  # buffer. Expressed as two literal replacements -- remove the call from its
  # slot, insert it after the ViewModel -- so the restore is still a byte copy
  # and the ordinal is taken where the pass actually ran, not faked with a +2.
  patch_in "$MUT_FRAME" \
"  drawOverlayPass();
  // T21: the depth the frame has after the world" \
"  // T21: the depth the frame has after the world" || mut_fail=1
  patch_in "$MUT_FRAME" \
"  drawViewModel();" \
"  drawViewModel();
  drawOverlayPass();  /* mutation m4: after the pass that clears depth */" || mut_fail=1
  if rebuild_product m4; then
    # WITH a viewmodel: the mutation moves the overlay after the ViewModel pass, and
    # the pair being asserted only exists when that pass runs. Without --viewmodel
    # both the mutant and the fixed build report vmOrdinal=0 and the case would
    # read GREEN -- a mutation that "did not move a reading" because the reading
    # was never taken. That is the same trap this file fixes elsewhere.
    M4_RC=$(run_frame "$OUT/m4" --outline on --viewmodel models/weapons/v_models/v_scattergun_scout.mdl)
    M4_ORDER=$(outline_value "$OUT/m4.trace" orderOk)
    M4_ORDINAL=$(outline_value "$OUT/m4.trace" ordinal)
    M4_VM=$(outline_value "$OUT/m4.trace" vmOrdinal)
    echo "    m4 ordinal=${M4_ORDINAL:-<none>} vmOrdinal=${M4_VM:-<none>} orderOk=${M4_ORDER:-<none>}"
    must_move "m4" "pass order (overlay before viewModel)" "1" "${M4_ORDER:-0}"
    # The number itself must move too: the mutant's ordinal is no longer the slot
    # the contract names, because the ordinal is read where the pass ran.
    must_move "m4" "overlay ordinal after the move" "${FIXED_ORD:-0}" "${M4_ORDINAL:-0}"
  fi
  restore_all; rebuild_product "restore after m4" || true

  # ---- m11: shift the readback index by one ---------------------------------
  echo
  echo "  -- m11: the readback reads the wrong instance --"
  # The attribution table is only a measurement if it can be wrong. The mutation
  # compares each drawn instance's identity against the NEXT slot's CPU record, which
  # is exactly the defect the table exists to catch (the tables holding the same
  # entities in a different order). If the gate still reads 0 mismatches, the table
  # is decoration.
  # The rows are printed by main.cpp from the renderer's readback table (the
  # renderer cannot call traceFmt: it is in an anonymous namespace in main.cpp).
  # So the mutation lives on the print, which is exactly what the gate reads.
  patch_in "$MUT_MAIN" \
"        identity[index].first == identity[index].second ? 1 : 0);" \
"        identity[(index + 1) % identity.size()].first == identity[index].second ? 1 : 0);" || mut_fail=1
  if rebuild_product m11; then
    M11_RC=$(run_frame "$OUT/m11" --outline on)
    M11_TOTAL=$(grep -a -c "overlay: readback i=" "$OUT/m11.trace" 2>/dev/null || true)
    M11_BAD=$(grep -a "overlay: readback i=" "$OUT/m11.trace" 2>/dev/null | grep -c "match=0" 2>/dev/null || true)
    echo "    m11 drawn=${M11_TOTAL:-0} identity mismatches=${M11_BAD:-0}"
    must_move "m11" "identity mismatches in the readback table" "0" "${M11_BAD:-0}"
  fi
  restore_all; rebuild_product "restore after m11" || true

  # ---- m12: shift the draw 3 px away from the region it publishes --------
  echo
  echo "  -- m12: the GPU draw is shifted 3 px, the box is not --"
  # A ruler is only a ruler if a broken overlay is caught by it. This shifts the
  # matrix the GPU draws with (the constant buffer) by 0.005 in NDC -- 3 px at 1264 --
  # and leaves the box the pass publishes computed from the UNSHIFTED mvpForOverlay.
  # If outsideDiff does not leave the fixed reading, the new region rule is soft and
  # this round proved nothing.
  patch_in "$MUT_FRAME" \
"          overlayConstants.mvp = frameConstants.mvp;" \
"          overlayConstants.mvp = frameConstants.mvp;
          /* mutation m12: shift only what the GPU draws; the published box keeps the
             unshifted matrix, so region and pixels move apart by 3 px. */
          for (int shiftRow = 0; shiftRow < 4; ++shiftRow) {
            overlayConstants.mvp.m[shiftRow][0] += 0.005f * frameConstants.mvp.m[shiftRow][3];
          }" || mut_fail=1
  if rebuild_product m12; then
    M12_RC=$(run_frame "$OUT/m12" --outline on)
    M12_NI=$("$PY" outline-frame-check.py noninterference on="$OUT/m12.bmp" off="$OUT/off.bmp" require_inside=0 2>&1)
    M12_OUT=$(echo "$M12_NI" | grep -o "diff_outside=[0-9]*" | cut -d= -f2)
    echo "    m12 outsideDiff=${M12_OUT:-<none>} (fixed build: ${NI_OUT:-<none>})"
    must_move "m12" "pixels outside every drawn region with the draw shifted 3 px" \
      "${NI_OUT:-0}" "${M12_OUT:-0}"
  fi
  # ---- m13: the shell DRAWN is ten times the shell DECLARED ----------------
  echo
  echo "  -- m13: the drawing expands ten times as far as the product declares --"
  # The mirror of the band: the gate reads the product's number, so the number has
  # to be the truth about the picture. Here the pass keeps publishing 1.5 px (the
  # band still passes) while the vertex shader offsets by 15: a gate that trusted
  # its own idea of how thick a shell is would see nothing wrong. The outside /
  # tolerance pair must fail, because the tolerance is built from the declaration.
  # (The declaration-side lie -- publish 0, draw 1.5 -- is m14 below.)
  patch_in "$MUT_FRAME" \
"            frameEye_[0], frameEye_[1], frameEye_[2], kOverlaySilhouetteExpandPixels);" \
"            frameEye_[0], frameEye_[1], frameEye_[2],
            10.0f * kOverlaySilhouetteExpandPixels);  /* mutation m13: 15 px shell */" || mut_fail=1
  if rebuild_product m13; then
    M13_RC=$(run_frame "$OUT/m13" --outline on)
    M13_NI=$("$PY" outline-frame-check.py noninterference on="$OUT/m13.bmp" off="$OUT/off.bmp" require_inside=0 2>&1)
    M13_OUT=$(echo "$M13_NI" | grep -o "diff_outside=[0-9]*" | cut -d= -f2)
    M13_TOL=$("$PY" "$PWD/outline-regions.py" --trace "$OUT/m13.trace" --expand-tolerance)
    M13_TOLV=$(echo "$M13_TOL" | grep -o "tolerance=[0-9.]*" | cut -d= -f2)
    echo "    m13 outsideDiff=${M13_OUT:-<none>} declared tolerance=${M13_TOLV:-<none>} (fixed build: ${NI_OUT:-<none>})"
    must_move "m13" "pixels outside every drawn region when the product declares no expansion" \
      "${NI_OUT:-0}" "${M13_OUT:-0}"
  fi
  # ---- m14: the product declares a zero shell while the pixels still expand --
  echo
  echo "  -- m14: the product publishes expandPx=0 while the shell still expands --"
  # The band and the tolerance both read the product's number. This mutation leaves
  # the RENDERER expanding (clip space, 1.5 px) and only lies about it, so a gate that
  # trusted its own idea of the shell would pass. The band must fail (zero is not an
  # outline) and so must the outside/tolerance pair.
  patch_in "$MUT_FRAME" \
"                reading.expandPixels = kOverlaySilhouetteExpandPixels;" \
"                reading.expandPixels = 0.0f;  /* mutation m14: declare no shell */" || mut_fail=1
  if rebuild_product m14; then
    M14_RC=$(run_frame "$OUT/m14" --outline on)
    M14_BAND=$("$PY" "$PWD/outline-regions.py" --trace "$OUT/m14.trace" --expand-band)
    M14_NI=$("$PY" outline-frame-check.py noninterference on="$OUT/m14.bmp" off="$OUT/off.bmp" require_inside=0 2>&1)
    M14_OUT=$(echo "$M14_NI" | grep -o "diff_outside=[0-9]*" | cut -d= -f2)
    M14_BELOW=$(echo "$M14_BAND" | grep -o "below=[0-9]*" | cut -d= -f2)
    M14_TOL=$("$PY" "$PWD/outline-regions.py" --trace "$OUT/m14.trace" --expand-tolerance)
    M14_TOLV=$(echo "$M14_TOL" | grep -o "tolerance=[0-9.]*" | cut -d= -f2)
    # The pixels do NOT move (the renderer still expands): what moves is whether the
    # gate accepts them. The product's declaration is the whole budget, so declaring
    # nothing turns the same 48 out-of-region pixels from allowed into rejected.
    M14_WITHIN=$(awk -v out="${M14_OUT:-0}" -v tol="${M14_TOLV:-0}" \
      'BEGIN { printf "%d", ((out + 0) <= (tol + 0)) ? 1 : 0 }')
    echo "    m14 $M14_BAND outsideDiff=${M14_OUT:-<none>} declared tolerance=${M14_TOLV:-<none>}" \
      "within=${M14_WITHIN:-<none>} (fixed: ${BAND:-<none>} within=${WITHIN:-<none>})"
    must_move "m14" "silhouettes the band refuses because the product declared zero" \
      "0" "${M14_BELOW:-0}"
    must_move "m14" "out-of-region pixels the gate still accepts once the shell is declared zero" \
      "${WITHIN:-1}" "${M14_WITHIN:-1}"
  fi
  restore_all; rebuild_product "restore after m14" || true
  restore_all; rebuild_product "restore after m13" || true
  restore_all; rebuild_product "restore after m12" || true

  # The restored tree must be the FIXED tree, and a byte-exact restore plus a
  # rebuild is only half of that: the binary has to read green again, or every
  # later reading in this worktree is a reading of the last mutant.
  R_RC=$(run_frame "$OUT/restored" --outline on --viewmodel models/weapons/v_models/v_scattergun_scout.mdl)
  assert_eq "restored build: overlay ordinal" "$(outline_value "$OUT/restored.trace" ordinal)" "${FIXED_ORD:-<none>}"
  assert_eq "restored build: pass order" "$(outline_value "$OUT/restored.trace" orderOk)" "1"
  assert_eq "restored build: overlay drew" "$(outline_value "$OUT/restored.trace" drew)" "1"


  # ---- m10: the colour frame offered as the depth map --------------------
  echo
  echo "  -- m10: a colour frame is not a depth map --"
  # Section 5 turns the depth capture into the occluded region, and that region is
  # the whole face of mutation m2. If any image were accepted as a depth map, a
  # colour frame would produce a plausible-looking region and m2 would be proven
  # against it. This case substitutes the frame for the depth map and requires the
  # section-5 readings to reject it: it is a GATE mutation (no rebuild, no product
  # launch), because the thing under test is the gate's own admission rule.
  cp "$OUT/on.bmp" "$OUT/m10-depth.bmp"
  M10_STATS=$("$PY" "$PWD/outline-regions.py" --depth-stats --depth "$OUT/m10-depth.bmp" 2>&1)
  echo "    m10 depth stats: ${M10_STATS:-<none>}"
  M10_NONGREY=$(printf '%s' "${M10_STATS:-}" | grep -o "nonGreyPct=[^ ]*" | cut -d= -f2)
  must_move "m10" "non-grey percent of a colour frame offered as depth (section 5 requires 0)" \
    "0" "${M10_NONGREY:-?}"
  # The region is printed but NOT asserted on: measured 2026-10-23, the colour frame
  # yields the same rectangle as the real depth (455,348,471,351), so a region
  # comparison would read GREEN on the substitution and prove nothing. The reading
  # that does separate them is the admission rule itself: 0% non-grey for a depth
  # capture, ~17% for the frame.
  M10_REGION=$("$PY" "$PWD/outline-regions.py" --trace "$OUT/on.trace" --occluded --depth "$OUT/m10-depth.bmp" 2>/dev/null)
  M10_FIXED=$("$PY" "$PWD/outline-regions.py" --trace "$OUT/on.trace" --occluded --depth "$OUT/worlddepth.bmp" 2>/dev/null)
  echo "    m10 region=${M10_REGION:-<none>}  real-depth region=${M10_FIXED:-<none>}"
  echo "    (the two regions coincide on this tick: the region comparison is not the reading that catches this)"

  # Where the binary on disk stands at the end of the suite. Every rebuilding case
  # restores the tree and rebuilds, and the restored-build assertions ran against
  # that rebuild; m10 is a gate mutation and rebuilds nothing. Said out loud because
  # the alternative is somebody forward-measuring a mutant and calling it the fixed
  # build -- 2026-10-23 the exe on disk was the m2 mutant while the sources were
  # clean, and only a timestamp comparison revealed it.
  echo
  echo "  SUITE-ARTIFACT: the binary on disk was rebuilt from the RESTORED tree by the"
  echo "                  last rebuilding case; the restored-build readings above were"
  echo "                  taken from it. A forward run still rebuilds before it measures."

  echo
  if [ "$mut_fail" = "0" ]; then
    echo "MUTATION-SUITE=ALL-RED-RESTORED"
  else
    echo "MUTATION-SUITE=FAIL (a mutation did not move a reading, or a restore was not byte-exact;"
    echo "  mutations that did not compile: $mut_invalid -- those proved nothing)"
    fail=1
  fi
else
  echo
  echo "mutation suite not requested (pass --mutation); the eight defects it"
  echo "reintroduces are NOT VERIFIED by this run."
fi

echo
# Last reading of all: did any command in this run never resolve? The handler at
# the top could not set fail (bash runs it in a subshell), so the names it left
# behind are read here. A run that called a command it never defined measured
# something other than what it claims, so the verdict cannot be green.
if [ -s "$HARNESS_UNKNOWN_LOG" ]; then
  echo "  FAIL harness: this run invoked undefined command(s): $(sort -u "$HARNESS_UNKNOWN_LOG" | tr '\n' ' ')"
  fail=1
fi
rm -f "$HARNESS_UNKNOWN_LOG" 2>/dev/null

if [ "$fail" = "0" ]; then
  echo "OUTLINE-CHECK=PASS"
else
  echo "OUTLINE-CHECK=FAIL"
fi
exit "$fail"
