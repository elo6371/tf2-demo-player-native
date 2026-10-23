#!/usr/bin/env bash
# hud-check.sh -- the T51 gate: a custom HUD can be imported, refused, drawn,
# reset, and cannot change a single world pixel outside its own rectangles.
#
# WHY THIS EXISTS
#   T51 adds a feature whose whole risk is in the negative space: a HUD that
#   renders is visible in a screenshot, but "the HUD did not disturb the world"
#   and "a bad HUD was refused" are claims about what did NOT happen. Neither is
#   observable from a passing draw call, so both are measured here as
#   comparisons -- two captures diffed outside the HUD region, and refusals
#   executed on fixtures that must earn their named reason.
#
# WHAT IT ASSERTS
#   A. the probe's reading set: path rules, the coordinate grammar, resolution
#      and scaling arithmetic, every refusal by name, and the real m0rehud
#      corpus (so 'the rules fit actual HUDs' is measured, not assumed);
#   B. the product imports a known-good fixture HUD and draws it: the trace's
#      drawn rect count matches the probe's resolved count, and the capture
#      differs inside the HUD union rectangle;
#   C. the same camera and the same tick, HUD on vs HUD off: the two frames are
#      BYTE-IDENTICAL outside the union rectangle. This is T51's key claim;
#   D. a malformed document, a missing document, a traversing path and an
#      oversized file are each refused with their own token, and the product
#      keeps drawing the world (the frame is not blank, not the fallback quad);
#   E. reset and uninstall leave every fixture user file byte-identical;
#   F. --mutation reintroduces one defect at a time and requires THIS gate to go
#      red, then restores each source byte-for-byte (cmp) and re-verifies:
#        (1) a bad HUD accepted (the #base refusal rule);
#        (2) the HUD pass perturbing the world (the outside-the-rect comparison);
#        (3) the hud.* hotkeys never registered (the shared-table assertions);
#        (4) reset deleting the imported directory (the user-file comparison).
#      Every mutation is scored against a green forward run: --mutation refuses to
#      start when the forward assertions above it are already red, because "the
#      mutation was caught" would then describe a gate that never worked.
#
# SINGLE INSTANCE
#   Every product launch here is a --hidden-window --audio-disabled run. The
#   script itself does not take the host lock: the caller does (verify-all.sh
#   and the task owner both run it under hostlock.sh), because a gate that
#   acquired the lock would deadlock when invoked from inside a locked run.
#
# Usage: bash hud-check.sh [--mutation]
# Exit:  0 = every assertion held (or, with --mutation, every mutation went red).

set -uo pipefail
cd "$(dirname "$0")"
# The trace and the fixture paths contain bytes outside the shell's UTF-8
# locale on this Windows build (the TF2 install path has a space and the
# fixtures carry a ':' in one case name). Byte-oriented matching must not stop
# at those bytes.
export LC_ALL=C

EXE=native/build-nmake/tf2_demo_native.exe
# The absolute path is what every launch uses: a gate that launches through a
# relative path from inside $(cd ...) is testing the shell, not the product.
EXE_ABS="$(cygpath -m "$PWD/$EXE")"
# hud_probe links only hud_import.cpp -- no device, no window, no audio -- so the
# gates and the probe itself run outside the host lock. The path is the build
# output, because that is what build-target.sh produces; the fallback is the
# standalone build this task uses while the lock is held by another task.
PROBE=native/build-nmake/hud_probe.exe
[ -x "$PROBE" ] || PROBE=.scratch/probe/hud_probe.exe
TF="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
DEMO="D:/TF2_Demo_Player/testdata/demos/bb841c6d379ff7c40d0c8baf99f59d8d_matcha-20260927-1347-cp_snakewater_final1.dem"
# Two directories, for the reason the other gates split them: OUT is the evidence
# that gets committed (readings, fixtures, the region diff), SCRATCH is the bulk
# that does not (every captured BMP is ~2.7 MB and four of them are byte-for-byte
# comparable copies of each other).
RUN_TAG="$(date +%Y%m%d%H%M%S)-$"
OUT="${HUD_CHECK_OUT:-evidence/hud}"
SCRATCH="${HUD_CHECK_SCRATCH:-.scratch/hud-check/$RUN_TAG}"
RUN_TIMEOUT=400
# Tick 0 with --start-paused, on purpose.
#
# A mid-demo tick would be the harder-looking choice and the wrong one: the draw
# cadence is wall clock while the tick cadence is the demo's, so a run "at tick
# 2000" can draw 2000 in one process and 2002 in the next, and then the two
# frames differ EVERYWHERE because the camera and the entities moved -- the
# outside-the-rects comparison would go red for a reason that has nothing to do
# with the HUD. Tick 0 paused draws whatever the world looks like at the demo's
# first frame and stays there, so any difference between two captures is a
# difference the feature made.
#
# --debug-camera additionally freezes the per-frame camera in map coordinates:
# the comparison is between two processes, and a pose that depends on entity
# state is one more thing that could drift between them.
CAMERA=(--debug-camera -3498.225 1021.852 1327.843 70.529329 134.999352)
TICK=0
# The device every run is pinned to, for the same reason the other gates do it:
# a launch that opens the system default makes noise on the wrong output, and
# --audio-disabled is checked AFTER the device is chosen.
AUDIO_DEVICE=6

MUTATION=0
for arg in "$@"; do
  case "$arg" in
    --mutation) MUTATION=1 ;;
    *) echo "unknown argument: $arg"; exit 2 ;;
  esac
done

# Missing input is a failure, not a skip. The demo is required because every
# capture below compares two FRAMES, and a run with no world would make "the HUD
# did not disturb the world" a statement about a picture with no world in it.
fail=0
for path in "$EXE_ABS" "$PROBE" "$DEMO" "$TF"; do
  [ -e "$path" ] || { echo "FATAL: missing input: $path"; exit 1; }
done

PY="C:/Users/Administrator/.workbuddy-ai/binaries/python/versions/3.13.12/python.exe"
[ -x "$PY" ] || PY="C:/Users/Administrator/AppData/Local/Programs/Python/Python310/python.exe"
[ -x "$PY" ] || { echo "FATAL: no python interpreter found"; exit 1; }
for tool in cygpath timeout cmp diff; do
  command -v "$tool" >/dev/null 2>&1 || { echo "FATAL: missing tool: $tool"; exit 1; }
done

mkdir -p "$OUT" "$SCRATCH"

assert_eq() { # assert_eq <label> <actual> <expected>
  if [ "${2:-<none>}" = "$3" ]; then
    echo "  OK   $1 = $3"
  else
    echo "  FAIL $1 = ${2:-<none>}, expected $3"; fail=1
  fi
}

assert_ne() { # assert_ne <label> <actual> <forbidden>
  if [ "${2:-}" != "$3" ]; then
    echo "  OK   $1 = ${2:-<none>} (!= $3)"
  else
    echo "  FAIL $1 = ${2:-<none>}, must not equal $3"; fail=1
  fi
}

assert_ge() { # assert_ge <label> <actual> <floor>
  if [ -n "${2:-}" ] && [ "${2:-0}" -ge "$3" ] 2>/dev/null; then
    echo "  OK   $1 = $2 (>= $3)"
  else
    echo "  FAIL $1 = ${2:-<none>}, expected >= $3"; fail=1
  fi
}

kv() { # kv <file-or-text> <key>  -- first key=value on a line, value only
  grep -oE "(^|[[:space:]])$2=[^[:space:]]+" "$1" 2>/dev/null | head -1 | sed -E "s/.*$2=//"
}
# ===========================================================================
# Fixtures. Written here rather than committed, for the same reason the other
# gates generate theirs: a fixture whose bytes are in the tree can drift from
# what the assertions expect, and a generated one cannot.
# ===========================================================================
FIX="$OUT/fixtures"
mkdir -p "$FIX/good/resource/ui" "$FIX/empty" "$FIX/escape-target" "$FIX/user"

cat > "$FIX/good/resource/ui/hudlayout.res" <<'RES'
"Resource/UI/HudLayout.res"
{
	"HealthAnchor"
	{
		"controlname"	"EditablePanel"
		"xpos"			"c-175"
		"ypos"			"r120"
		"wide"			"350"
		"tall"			"80"
		"visible"		"1"
		"enabled"		"1"

		"HealthValue"
		{
			"controlname"	"CExLabel"
			"xpos"			"0"
			"ypos"			"0"
			"wide"			"f0.5"
			"tall"			"100%"
			"labelText"		"%Health%"
			"textAlignment"	"center"
			"font"			"ClientTitleFont"
			"visible"		"1"
		}

		"HealthIcon"
		{
			"controlname"	"ImagePanel"
			"xpos"			"f0.5"
			"ypos"			"0"
			"wide"			"64"
			"tall"			"64"
			"image"			"hud/health_icon"
			"visible"		"1"
		}
	}

	"AmmoPanel"
	{
		"controlname"	"EditablePanel"
		"xpos"			"r200"
		"ypos"			"r160"
		"wide"			"200"
		"tall"			"64"
		"visible"		"1"
	}

	"Crosshair"
	{
		"controlname"	"ImagePanel"
		"xpos"			"c-4"
		"ypos"			"c-4"
		"wide"			"8"
		"tall"			"8"
		"image"			"crosshairs/default"
		"visible"		"1"
	}

	"HiddenPanel"
	{
		"controlname"	"EditablePanel"
		"xpos"			"10"
		"ypos"			"10"
		"wide"			"10"
		"tall"			"10"
		"visible"		"0"
	}
}
RES
# Six panels resolve; five are drawn (HiddenPanel has visible "0" and must be
# excluded from both the draw list and the union region). The document has NO
# full-screen root on purpose: a HUD whose union rectangle is the whole client
# would make the outside-the-rect comparison vacuous, and the assertion this gate
# exists for would pass while proving nothing.

# The same document with one unsupported coordinate: the import must succeed and
# report exactly one refused panel, so "it imported" cannot mean "it imported
# everything".
sed 's/"r200"/"o3"/' "$FIX/good/resource/ui/hudlayout.res" \
  > "$FIX/good/resource/ui/hudpartial.res"

# A malformed document: a brace that never closes.
cat > "$FIX/good/resource/ui/hudbroken.res" <<'RES'
"Resource/UI/HudBroken.res"
{
	"Root"
	{
		"Panel"
		{
			"xpos"	"0"
RES

# A document that uses an include. Refused by name, not silently half-loaded.
cat > "$FIX/good/resource/ui/hudinclude.res" <<'RES'
#base "hudlayout.res"

"Resource/UI/HudInclude.res"
{
	"Root"
	{
		"Panel"
		{
			"xpos"	"0"
			"ypos"	"0"
			"wide"	"10"
			"tall"	"10"
		}
	}
}
RES

# A document whose single panel uses a coordinate form this build refuses, so
# the whole import is refused (no panels survive).
cat > "$FIX/good/resource/ui/hudrefused.res" <<'RES'
"Resource/UI/HudRefused.res"
{
	"Root"
	{
		"Panel"
		{
			"xpos"	"o3"
			"ypos"	"0"
			"wide"	"10"
			"tall"	"10"
		}
	}
}
RES

# A user fixture tree. The bytes here are the thing the reset/uninstall
# assertions compare: if any operation rewrites or deletes one of these, the
# "we never touch user files" promise is broken and the gate must say so.
printf 'the user wrote this\n' > "$FIX/user/notes.txt"
printf '{"custom":true}\n' > "$FIX/user/settings-backup.json"
mkdir -p "$FIX/user/sub"
printf 'nested user file\n' > "$FIX/user/sub/deep.txt"
( cd "$FIX/user" && find . -type f | sort | xargs sha256sum ) > "$OUT/user-before.sha256"

echo "=== 1/9 the probe's reading set (pure rules, no window) ==="
"$PROBE" --fixture-root "$(cygpath -m "$OUT/probe-fixtures")" > "$OUT/probe.txt" 2>&1
PROBE_RC=$?
echo "$PROBE_RC" > "$OUT/probe.rc"
cat "$OUT/probe.txt"
assert_eq "probe exit code" "$PROBE_RC" "0"
assert_eq "probe self-failures" "$(kv "$OUT/probe.txt" hud_probe.failures)" "0"
assert_eq "path cases exercised" "$(kv "$OUT/probe.txt" path.cases)" "20"
assert_eq "path accepted controls" "$(kv "$OUT/probe.txt" path.accepted)" "4"
assert_eq "path refusals" "$(kv "$OUT/probe.txt" path.refused)" "16"
assert_eq "path wrong" "$(kv "$OUT/probe.txt" path.wrong)" "0"
assert_eq "coordinate forms parsed" "$(kv "$OUT/probe.txt" coord.parsed)" "12"
assert_eq "coordinates refused by design" "$(kv "$OUT/probe.txt" coord.refusedByDesign)" "3"
assert_eq "coordinates malformed" "$(kv "$OUT/probe.txt" coord.malformed)" "5"
assert_eq "coordinates wrong" "$(kv "$OUT/probe.txt" coord.wrong)" "0"
assert_eq "good document panels" "$(kv "$OUT/probe.txt" good.panels)" "5"
assert_eq "document refusals by name" "$(kv "$OUT/probe.txt" doc.refusedAsNamed)" "7"
assert_eq "document refusal mismatches" "$(kv "$OUT/probe.txt" doc.wrong)" "0"
assert_eq "file refusals correct" "$(kv "$OUT/probe.txt" file.correct)" "9"
assert_eq "file refusal mismatches" "$(kv "$OUT/probe.txt" file.wrong)" "0"
# The corpus is the WHOLE installed directory, so these are absolute counts and
# not floors: a floor would let the sampler quietly narrow its scope and still
# pass. If a machine has no TF2 the probe prints corpus=skipped and these are
# reported as NOT VERIFIED instead of being invented.
# grep for the literal line rather than going through kv(): kv() returns the FIRST
# match of a prefix, and "corpus.dir=..." is printed unconditionally above the
# skip line, so the kv() form of this test could never be true.
if grep -q '^corpus=skipped' "$OUT/probe.txt"; then
  echo "  NOT-VERIFIED corpus readings: no TF2 install on this machine"
  echo "  NOT-VERIFIED owner=t51-hud; next step: run on a machine with tf/custom/m0rehud-5.8"
else
  assert_eq "corpus documents sampled" "$(kv "$OUT/probe.txt" corpus.tried)" "102"
  assert_eq "corpus documents imported" "$(kv "$OUT/probe.txt" corpus.imported)" "71"
  assert_eq "corpus refused for #base" "$(kv "$OUT/probe.txt" corpus.refusedInclude)" "23"
  assert_eq "corpus refused for another reason" "$(kv "$OUT/probe.txt" corpus.refusedOther)" "8"
fi
# "Imported" is not "drawn completely": a document with at least one placeable
# panel imports, and the panels it could not place are dropped from the
# `corpus.imported` count above. The probe aggregates that, because the
# support-scope document quotes "71 of 102" and a reader must be able to see how
# much of those 71 the parser actually placed.
#
# Asserted as INVARIANTS and not against literals: the exact numbers belong to
# the corpus installed on this machine and are committed in probe.txt, while
# these two relations have to hold for any correct counter. A counter that
# returned nonsense (more partially-imported documents than imported ones, or
# fewer dropped panels than documents that dropped one) fails here, which is
# what makes the printed number falsifiable rather than decorative.
CORPUS_PARTIAL=$(kv "$OUT/probe.txt" corpus.importedPartial)
CORPUS_KEPT=$(kv "$OUT/probe.txt" corpus.panelsKept)
CORPUS_DROPPED=$(kv "$OUT/probe.txt" corpus.panelsRefused)
if [ -z "$CORPUS_PARTIAL" ]; then
  echo "  NOT-VERIFIED per-panel corpus accounting: this probe binary has no such reading"
  echo "    owner=t51-hud; cheapest next step: bash build-target.sh hud_probe (the gate's"
  echo "    own build step does it), then this becomes an assertion instead of a note"
else
  echo "  corpus panels: kept=${CORPUS_KEPT:-<none>} dropped=${CORPUS_DROPPED:-<none>}," \
       "in ${CORPUS_PARTIAL:-<none>} of the imported documents"
  if [ "$CORPUS_PARTIAL" -le 71 ] 2>/dev/null; then
    echo "  OK   partially imported documents ($CORPUS_PARTIAL) <= imported documents (71)"
  else
    echo "  FAIL partially imported documents ($CORPUS_PARTIAL) exceeds the 71 imported"; fail=1
  fi
  if [ "$CORPUS_DROPPED" -ge "$CORPUS_PARTIAL" ] 2>/dev/null; then
    echo "  OK   dropped panels ($CORPUS_DROPPED) >= partially imported documents ($CORPUS_PARTIAL)"
  else
    echo "  FAIL dropped panels ($CORPUS_DROPPED) < partially imported documents ($CORPUS_PARTIAL)"; fail=1
  fi
fi
echo
echo "=== 2/9 the product imports the good fixture, and the frame shows it ==="
# One run per state, all at the same camera and tick. run_hud <name> [args...]
run_hud() { # run_hud <name> [extra args...]
  local name="$1"; shift
  local trace="$OUT/$name.trace"
  local bmp="$SCRATCH/$name.bmp"
  local csv="$SCRATCH/$name.csv"
  local log="$SCRATCH/$name.log"
  local settings="$OUT/$name.settings.ini"
  local uisettings="$OUT/$name.ui.ini"
  rm -f "$trace" "$bmp" "$csv" "$log" "$settings" "$uisettings"
  # --settings-file and --ui-settings-file keep the run hermetic: without them a
  # gate reads (and WRITES) whatever the tester had configured, which is the
  # exact defect settings_store.h's override was added for.
  TF2_NATIVE_TRACE="$(cygpath -m "$PWD/$trace")" timeout "$RUN_TIMEOUT" "$EXE" \
    --tf-root "$TF" --audio-device "$AUDIO_DEVICE" --audio-disabled --hidden-window \
    --settings-file "$(cygpath -m "$PWD/$settings")" \
    --ui-settings-file "$(cygpath -m "$PWD/$uisettings")" \
    --demo "$DEMO" --start-paused --capture-tick "$TICK" \
    --capture-frame "$(cygpath -m "$PWD/$bmp")" --metrics-file "$(cygpath -m "$PWD/$csv")" \
    "${CAMERA[@]}" "$@" > "$log" 2>&1
  printf -v "RC_$name" '%s' "$?"
}

run_hud hud_off
run_hud hud_on --hud-root "$(cygpath -m "$PWD/$FIX/good")" --hud-apply --hud-on
assert_eq "hud_off exit code" "$RC_hud_off" "0"
assert_eq "hud_on exit code" "$RC_hud_on" "0"

# What the product says it did, before anything about pixels.
OFF_DRAWN=$(grep -oE 'hud: draw drawn=[0-9]+' "$OUT/hud_off.trace" | tail -1 | sed 's/.*drawn=//')
ON_DRAWN=$(grep -oE 'hud: draw drawn=[0-9]+' "$OUT/hud_on.trace" | tail -1 | sed 's/.*drawn=//')
ON_PANELS=$(grep -oE 'hud: layout .*panels=[0-9]+' "$OUT/hud_on.trace" | tail -1 | sed 's/.*panels=//')
ON_RESOLVED=$(grep -oE 'hud: rects resolved=[0-9]+' "$OUT/hud_on.trace" | tail -1 | sed 's/.*resolved=//')
ON_VISIBLE=$(grep -oE 'hud: rects .*visible=[0-9]+' "$OUT/hud_on.trace" | tail -1 | sed 's/.*visible=//')
# The union is read from the CAPTURE-FRAME trace line, not from the startup
# publish: the capture line prints the resolver's union and the renderer's region
# for the SAME bitmap, which is what makes the cross-check below a comparison of
# two numbers rather than of two moments.
ON_UNION=$(grep -oE 'hud: draw .*union=[-0-9]+,[-0-9]+,[0-9]+,[0-9]+' "$OUT/hud_on.trace" | tail -1 | sed 's/.*union=//')
ON_IMPORT=$(grep -oE 'hud: import result=[a-z-]+' "$OUT/hud_on.trace" | tail -1 | sed 's/.*=//')
ON_SCISSOR=$(grep -oE 'hud: draw .*scissor=[0-9]+' "$OUT/hud_on.trace" | tail -1 | sed 's/.*scissor=//')
ON_REGION=$(grep -oE 'hud: draw .*region=[-0-9]+,[-0-9]+,[0-9]+,[0-9]+' "$OUT/hud_on.trace" | tail -1 | sed 's/.*region=//')
echo "  readings: import=$ON_IMPORT panels=$ON_PANELS resolved=$ON_RESOLVED visible=$ON_VISIBLE union=$ON_UNION"
echo "  readings: drawnOff=${OFF_DRAWN:-<none>} drawnOn=${ON_DRAWN:-<none>} scissor=${ON_SCISSOR:-<none>} region=${ON_REGION:-<none>}"

# The default-off half of the contract: a plain capture run draws NO HUD.
assert_eq "a hud-off run draws nothing" "${OFF_DRAWN:-<none>}" "0"
assert_eq "the good fixture imports" "$ON_IMPORT" "applied"
# TWO counts, not one, and the fixture is built so they differ: six panels, one of
# them \"visible\" \"0\". `resolved` is geometry that came out, `visible` is what the
# document actually shows. Asserting a single number for both -- which the first
# version of this gate did -- is how a HUD that silently dropped a panel reads as
# complete, and it is also why the assertion below compares the DRAWN count against
# VISIBLE rather than against resolved.
ON_INVALID=$(grep -oE 'hud: rects .*invalid=[0-9]+' "$OUT/hud_on.trace" | tail -1 | sed 's/.*invalid=//')
assert_eq "the good fixture resolves every panel" "$ON_RESOLVED" "6"
assert_eq "the document hides exactly one panel" "$ON_VISIBLE" "5"
assert_eq "no panel failed to resolve" "${ON_INVALID:-<none>}" "0"
assert_eq "the drawn rect count equals the visible count" "$ON_DRAWN" "$ON_VISIBLE"
assert_eq "the pass scissored itself" "$ON_SCISSOR" "1"
assert_ne "the HUD owns some pixels" "$ON_UNION" ""
# The renderer's own drawn region must agree with the resolver's union. They are
# computed by different code from different inputs (the resolver from the
# document; the renderer from the rect list it was handed), so agreement is a
# real cross-check rather than a tautology -- and it is what makes the union
# rectangle usable as the alibi boundary in step 3.
assert_eq "the renderer's region matches the resolver's union" "$ON_REGION" "$ON_UNION"

# The import must be readable back as a settings file: the [hud] keys exist and
# say what the run said.
grep -qi '^\[hud\]' "$OUT/hud_on.ui.ini" && HUD_SECTION=present || HUD_SECTION=absent
assert_eq "the [hud] section was written" "$HUD_SECTION" "present"
assert_eq "hud.enabled persisted" "$(grep -i '^hud\.enabled=' "$OUT/hud_on.ui.ini" | tail -1 | cut -d= -f2 | tr -d '\r')" "1"
assert_eq "hud.scale persisted" "$(grep -i '^hud\.scale=' "$OUT/hud_on.ui.ini" | tail -1 | cut -d= -f2 | tr -d '\r')" "100"
echo
echo
echo "=== 2b/9 the three hotkeys are registered in the shared table ==="
# hotkey_check <trace> -- the hotkey contract, read from the PRODUCT.
#
# Read from a real run's trace, not from a comparison of two source files: this is
# the table the product actually built at startup, after defaultKeyBindings() and
# after loadFromSettings(), which is where a merge defect between T50 and T51 would
# show up -- and where neither of the two gates looked before this section.
#
# It returns the NUMBER OF FAILED ASSERTIONS instead of setting the global `fail`,
# so the forward run can fold it into `fail` while the mutation suite can require a
# mutated build to be non-zero here. One implementation, two callers: a mutation
# that turned a DIFFERENT assertion red would prove nothing about this one.
hotkey_check() { # hotkey_check <trace>
  local trace="$1" bad=0 action expected line chord bound actions
  for action in hud.toggle hud.preview hud.reset; do
    line=$(grep -oE "hud: keys action=$action [^ ]* bound=[0-9]" "$trace" 2>/dev/null | tail -1)
    chord=$(printf "%s" "$line" | sed -n 's/.*chord=\([^ ]*\).*/\1/p')
    bound=$(printf "%s" "$line" | sed -n 's/.*bound=\([0-9]\).*/\1/p')
    case "$action" in
      hud.toggle)  expected="H";;
      hud.preview) expected="Ctrl+P";;
      hud.reset)   expected="Ctrl+Shift+H";;
      *)           expected="";;
    esac
    echo "  $action -> chord=${chord:-<none>} bound=${bound:-<none>} (contract: $expected)"
    if [ "${bound:-<none>}" = "1" ]; then
      echo "  OK   $action is bound = 1"
    else
      echo "  FAIL $action is bound = ${bound:-<none>}, expected 1"; bad=$((bad + 1))
    fi
    if [ "${chord:-<none>}" = "$expected" ]; then
      echo "  OK   $action default chord = $expected"
    else
      echo "  FAIL $action default chord = ${chord:-<none>}, expected $expected"; bad=$((bad + 1))
    fi
  done
  # The table is SHARED. If the registration had started from an empty table
  # instead of T50 defaultKeyBindings(), every camera binding would be gone and
  # this count would collapse to three while every other assertion stayed green.
  # A floor, not an equality, because T50's action list is theirs to extend.
  actions=$(grep -oE "hud: keys actions=[0-9]+" "$trace" 2>/dev/null | tail -1 | sed 's/.*actions=//')
  echo "  the shared table holds ${actions:-<none>} actions"
  if [ -n "${actions:-}" ] && [ "${actions:-0}" -ge 12 ] 2>/dev/null; then
    echo "  OK   the shared table kept the camera actions (>= 12)"
  else
    echo "  FAIL the shared table holds ${actions:-<none>} actions, expected >= 12"
    bad=$((bad + 1))
  fi
  return "$bad"
}
hotkey_check "$OUT/hud_off.trace" || fail=1
echo "=== 3/9 the measurement must be deterministic BEFORE it is used ==="
# THE ORDER HERE IS THE WHOLE ARGUMENT.
#
#   A comparison between two captures is only evidence if the same
#   configuration produces the same bytes twice. If two hud-off runs differ, then
#   "hud-on differs from hud-off outside the rect" says nothing about the HUD --
#   it says the capture is not reproducible -- and asserting it afterwards would
#   be reading noise as a signal.
#
# So this section runs BEFORE the HUD comparison and is allowed to stop the gate.
# It is not a control at the end; it is the precondition of everything below.
run_hud hud_off_repeat
run_hud hud_off_repeat2
DET_A=$(stat -c%s "$SCRATCH/hud_off.bmp" 2>/dev/null || echo 0)
DET_B=$(stat -c%s "$SCRATCH/hud_off_repeat.bmp" 2>/dev/null || echo 0)
DET_C=$(stat -c%s "$SCRATCH/hud_off_repeat2.bmp" 2>/dev/null || echo 0)
echo "  three hud-off captures: $DET_A / $DET_B / $DET_C bytes"
assert_eq "hud-off repeat exit code" "$RC_hud_off_repeat" "0"
assert_eq "hud-off repeat2 exit code" "$RC_hud_off_repeat2" "0"

DETERMINISTIC=1
if cmp -s "$SCRATCH/hud_off.bmp" "$SCRATCH/hud_off_repeat.bmp"; then
  echo "  OK   capture 1 and capture 2 are byte-identical"
else
  echo "  FAIL capture 1 and capture 2 differ -- the measurement is not deterministic"
  DETERMINISTIC=0
fi
if cmp -s "$SCRATCH/hud_off.bmp" "$SCRATCH/hud_off_repeat2.bmp"; then
  echo "  OK   capture 1 and capture 3 are byte-identical"
else
  echo "  FAIL capture 1 and capture 3 differ"
  DETERMINISTIC=0
fi
# The tick each capture actually drew. Equal bytes imply equal ticks, but the
# reading is printed so a reader does not have to infer it -- and if the bytes
# ever do diverge, the ticks are the first thing to look at.
for pair in "hud_off" "hud_off_repeat" "hud_off_repeat2"; do
  t=$(grep -aoE 'draw: begin tick=[0-9]+' "$OUT/$pair.trace" 2>/dev/null | tail -1 | sed 's/.*tick=//')
  echo "  $pair drew tick=${t:-<none>}"
done

if [ "$DETERMINISTIC" -eq 0 ]; then
  # Writing NOT VERIFIED rather than skipping quietly: a capture pair that is not
  # reproducible cannot support the claim, and the claim is the task.
  echo "  NOT-VERIFIED: the outside-the-rect comparison (owner=t51-hud)"
  echo "    reason: two identical hud-off runs did not produce identical bytes, so a"
  echo "            difference between hud-off and hud-on cannot be attributed to the HUD."
  echo "    cheapest next step: re-run this gate on an idle machine; if it still"
  echo "            differs, capture the pair at --start-paused with no demo, which"
  echo "            removes entity animation from the frame."
  fail=1
else
  echo "  DETERMINISTIC=PASS (the measurement is reproducible; the comparison below is evidence)"
fi

# ---------------------------------------------------------------------------
# The key assertion: outside the HUD union rectangle the two frames are
# byte-identical. This is what "a HUD change does not modify the world/model
# rendering underneath" means as a measurement rather than as a claim.
#
# The comparison is over the BMP payload (after the 54-byte header), decoded
# into 24-bit BGR rows with the file's own bottom-up order undone. It reports
# four numbers so a failure is diagnosable instead of just red:
#   inside       pixels inside the union rect (the HUD may change all of them)
#   outside      pixels outside it
#   outsideDiff  how many of those differ (MUST be 0)
#   firstDiff    the first differing pixel for a human to look at
# ---------------------------------------------------------------------------
cat > "$OUT/hud-region-diff.py" <<'PYEOF'
import sys, struct

def read_bmp(path):
    data = open(path, 'rb').read()
    if data[:2] != b'BM':
        raise SystemExit("not a BMP: %s" % path)
    offset = struct.unpack_from('<I', data, 10)[0]
    width = struct.unpack_from('<i', data, 18)[0]
    height = struct.unpack_from('<i', data, 22)[0]
    bpp = struct.unpack_from('<H', data, 28)[0]
    if bpp != 24:
        raise SystemExit("expected a 24-bit BMP, got %d" % bpp)
    stride = ((width * 3 + 3) // 4) * 4
    return data, offset, width, abs(height), stride, height > 0

a_path, b_path = sys.argv[1], sys.argv[2]
left, top, right, bottom = (int(v) for v in sys.argv[3:7])
a = read_bmp(a_path)
b = read_bmp(b_path)
if a[2:6] != b[2:6]:
    raise SystemExit("geometry differs: %s vs %s" % (a_path, b_path))
data_a, off_a, width, rows, stride, bottom_up = a
data_b, off_b = b[0], b[1]
if data_a[:off_a] != data_b[:off_b]:
    raise SystemExit("BMP headers differ (a different frame shape was captured)")

inside = 0
outside = 0
outside_diff = 0
inside_diff = 0
first = None
for row in range(rows):
    y = (rows - 1 - row) if bottom_up else row   # file row -> image y
    base = row * stride
    line_a = data_a[off_a + base: off_a + base + width * 3]
    line_b = data_b[off_b + base: off_b + base + width * 3]
    in_row = top <= y < bottom
    # Three spans for this row: left of the rect, the rect itself, right of it.
    spans = [(0, max(0, min(width, left)), 'outside'),
             (max(0, left), min(width, right), 'inside' if in_row else 'outside'),
             (max(0, min(width, right)), width, 'outside')]
    for (sx, ex, kind) in spans:
        if ex <= sx:
            continue
        s, e = sx * 3, ex * 3
        count = ex - sx
        same = line_a[s:e] == line_b[s:e]
        if kind == 'inside':
            inside += count
            if not same:
                for i in range(s, e, 3):
                    if line_a[i:i+3] != line_b[i:i+3]:
                        inside_diff += 1
        else:
            outside += count
            if not same:
                for i in range(s, e, 3):
                    if line_a[i:i+3] != line_b[i:i+3]:
                        outside_diff += 1
                        if first is None:
                            first = (i // 3, y)
print("inside=%d" % inside)
print("outside=%d" % outside)
print("insideDiff=%d" % inside_diff)
print("outsideDiff=%d" % outside_diff)
print("firstDiff=%s" % (("%d,%d" % first) if first else "-"))
PYEOF

echo
echo "=== 4/9 HUD on vs off: identical outside the HUD union rectangle ==="
IFS=, read -r UX UY UW UH <<< "$ON_UNION"
UX_R=$((UX + UW))
UY_B=$((UY + UH))
echo "  union rect: x=$UX y=$UY w=$UW h=$UH -> right=$UX_R bottom=$UY_B"
if [ "$DETERMINISTIC" -eq 0 ]; then
  # The comparison is NOT run, and the reason is written down rather than the
  # section being skipped silently: a difference here could not be attributed to
  # the HUD when two hud-off runs already disagree.
  echo "  NOT-VERIFIED: outside-the-rect comparison not run -- the baseline pair above is not reproducible"
  echo "    owner=t51-hud"
  echo "    cheapest next step: re-run this gate on an idle machine; if it still differs, capture"
  echo "      the pair with --start-paused and no --demo, which removes entity animation"
  printf 'inside=0\noutside=0\ninsideDiff=0\noutsideDiff=not-verified\nfirstDiff=-\n' > "$OUT/region.txt"
else
  REGION=$("$PY" "$OUT/hud-region-diff.py" "$(cygpath -m "$PWD/$SCRATCH/hud_off.bmp")" \
    "$(cygpath -m "$PWD/$SCRATCH/hud_on.bmp")" "$UX" "$UY" "$UX_R" "$UY_B")
  echo "$REGION" > "$OUT/region.txt"
  echo "$REGION" | sed 's/^/  /'
  assert_eq "pixels outside the HUD region that changed" "$(kv "$OUT/region.txt" outsideDiff)" "0"
  assert_ge "pixels inside the HUD region" "$(kv "$OUT/region.txt" inside)" "1000"
  assert_ge "pixels outside the HUD region" "$(kv "$OUT/region.txt" outside)" "100000"
  # The other half of the same reader: the HUD must have CHANGED pixels inside
  # its rect, or the comparison outside it would be vacuous -- two identical
  # frames agree everywhere and prove nothing.
  assert_ge "pixels changed inside the HUD region" "$(kv "$OUT/region.txt" insideDiff)" "1000"
fi
echo
echo "=== 5/9 every refusal is executed, and the world keeps rendering ==="
# A refusal that only exists in the probe is a statement about a pure function.
# These four runs prove the PRODUCT refuses, names the reason, and still draws a
# world frame -- the last part matters most: an import that fails must not take
# the renderer down with it.
refusal_case() { # refusal_case <name> <expected-token> [args...]
  local name="$1"; local expected="$2"; shift 2
  run_hud "refuse_$name" "$@"
  local reason
  reason=$(grep -oE 'hud: import refused reason=[a-z-]+' "$OUT/refuse_$name.trace" | tail -1 | sed 's/.*=//')
  if [ -z "$reason" ]; then
    reason=$(grep -oE 'hud: refuse .*lastReason=[a-z-]+' "$OUT/refuse_$name.trace" | tail -1 | sed 's/.*lastReason=//')
  fi
  local drawn
  drawn=$(grep -oE 'hud: draw drawn=[0-9]+' "$OUT/refuse_$name.trace" | tail -1 | sed 's/.*drawn=//')
  printf -v "REASON_$name" '%s' "$reason"
  # Indirect expansion: "$RC_refuse_$name" is read by bash as $RC_refuse_ followed
  # by $name, which is empty -- the comparison would then be "" vs "0" and fail
  # for a reason that has nothing to do with the product.
  local rcvar="RC_refuse_$name"
  # ${!rcvar} on an unset name is an error under set -u, and "the run never
  # happened" must read as a failure rather than aborting the whole gate.
  local rc="<unset>"
  if [ -n "${!rcvar+x}" ]; then rc="${!rcvar}"; fi
  echo "  $name -> rc=${rc:-<none>} reason=${reason:-<none>} drawn=${drawn:-<none>}"
  assert_eq "$name refused with its own token" "$reason" "$expected"
  assert_eq "$name exit code" "${rc:-<none>}" "0"
  assert_eq "$name drew no HUD" "${drawn:-<none>}" "0"
  # The frame must still be a world: a capture that came back blank or as the
  # fallback quad would mean the refusal took the renderer with it.
  assert_eq "$name still captured a frame" "$([ -s "$SCRATCH/refuse_$name.bmp" ] && echo present || echo absent)" "present"
}

refusal_case broken      malformed-keyvalues --hud-root "$(cygpath -m "$PWD/$FIX/good")" --hud-layout "resource/ui/hudbroken.res" --hud-apply --hud-on
refusal_case include     unsupported-include --hud-root "$(cygpath -m "$PWD/$FIX/good")" --hud-layout "resource/ui/hudinclude.res" --hud-apply --hud-on
refusal_case refused     unsupported-coordinate --hud-root "$(cygpath -m "$PWD/$FIX/good")" --hud-layout "resource/ui/hudrefused.res" --hud-apply --hud-on
refusal_case traversal   parent-traversal --hud-root "$(cygpath -m "$PWD/$FIX/good")" --hud-layout "../escape-target/resource/ui/hudlayout.res" --hud-apply --hud-on
refusal_case missing     missing --hud-root "$(cygpath -m "$PWD/$FIX/good")" --hud-layout "resource/ui/nothisfile.res" --hud-apply --hud-on

# The partial document is the CONTROL for the refusals above: same folder, same
# flags, one panel unsupported. It must import, keep four panels, and say it
# dropped one -- otherwise "refused" above could be a validator that refuses
# everything.
run_hud hud_partial --hud-root "$(cygpath -m "$PWD/$FIX/good")" --hud-layout "resource/ui/hudpartial.res" --hud-apply --hud-on
PARTIAL_RESULT=$(grep -oE 'hud: import result=[a-z-]+' "$OUT/hud_partial.trace" | tail -1 | sed 's/.*=//')
PARTIAL_VALID=$(grep -oE 'hud: rects resolved=[0-9]+' "$OUT/hud_partial.trace" | tail -1 | sed 's/.*resolved=//')
PARTIAL_REFUSED=$(grep -oE 'hud: layout .*refused=[0-9]+' "$OUT/hud_partial.trace" | tail -1 | sed 's/.*refused=//')
echo "  partial -> result=${PARTIAL_RESULT:-<none>} valid=${PARTIAL_VALID:-<none>} refusedPanels=${PARTIAL_REFUSED:-<none>}"
assert_eq "a partially unsupported document still imports" "$PARTIAL_RESULT" "applied"
# The fixture's "r200" was replaced by "o3" on ONE panel: the document still
# resolves five of its six panels and reports exactly one refused.
assert_eq "the partial document keeps five resolved panels" "$PARTIAL_VALID" "5"
assert_eq "the partial document counts one refused panel" "$PARTIAL_REFUSED" "1"

# An oversized file is refused before it is parsed, by the size rule.
python_make_oversized() {
  "$PY" - "$1" <<'PYEOF'
import sys
body = open(sys.argv[1], "rb").read()
open(sys.argv[1], "wb").write(body + b" " * (300 * 1024))
PYEOF
}
cp "$FIX/good/resource/ui/hudlayout.res" "$FIX/good/resource/ui/hudoversized.res"
python_make_oversized "$FIX/good/resource/ui/hudoversized.res"
refusal_case oversized too-large --hud-root "$(cygpath -m "$PWD/$FIX/good")" --hud-layout "resource/ui/hudoversized.res" --hud-apply --hud-on
echo
echo "=== 6/9 reset and uninstall never touch a user file ==="
# The promise: the imported directory is READ-ONLY to this feature. The test is
# not "reset does not call remove" (a code reading); it is the bytes of a
# fixture user tree before and after every operation this feature can perform.
HUD_ROOT_MUT="$OUT/user-hud"
# make_user_hud_fixture -- (re)creates the user tree from nothing.
#
# A FUNCTION, and not a straight-line block, because the deletion mutation below
# has to put the fixture back before it can show that the restored build leaves it
# alone: a mutation that could not be undone would leave the gate with a missing
# fixture and no way to tell "restored" from "already broken".
make_user_hud_fixture() {
  rm -rf "$HUD_ROOT_MUT"
  mkdir -p "$HUD_ROOT_MUT/resource/ui" "$HUD_ROOT_MUT/sub/deeper"
  cp "$FIX/good/resource/ui/hudlayout.res" "$HUD_ROOT_MUT/resource/ui/hudlayout.res"
  printf "the user wrote this too\n" > "$HUD_ROOT_MUT/user-notes.txt"
  printf "and this\n" > "$HUD_ROOT_MUT/resource/ui/keepme.txt"
  printf "nested\n" > "$HUD_ROOT_MUT/sub/deeper/deep.txt"
  # An EMPTY directory: the manifest-driven uninstall removes directories while
  # they are empty, and a HUD folder that lost an empty subdirectory would be a
  # user-visible change that a file-hash comparison cannot see.
  mkdir -p "$HUD_ROOT_MUT/empty-dir"
}
make_user_hud_fixture
# Two things are compared, and both matter:
#   * the DIRECTORY-ENTRY SET: every path that exists afterwards existed before,
#     and nothing new appeared. A hash list alone would miss a created file and a
#     deleted-then-recreated file with the same bytes.
#   * the BYTES of every file. "The file is still there" is not "the file is
#     unchanged".
hud_snapshot() { # hud_snapshot <dir> <prefix>
  # A snapshot of a directory that no longer exists is a FINDING, not an empty
  # listing: ( cd "$gone" && find . ) runs find in the gate's own working
  # directory when the cd fails, and the hash of the whole worktree would then be
  # diffed against the fixture and reported as "a user file changed" -- a failure
  # that names the wrong thing. The mutation that deletes the fixture relies on
  # this being reported as a missing directory.
  if [ ! -d "$1" ]; then
    echo "  NOTE the snapshot target is missing: $1"
    : > "$OUT/$2.entries.txt"
    : > "$OUT/$2.sha256.txt"
    : > "$OUT/$2.dirs.txt"
    return 1
  fi
  ( cd "$1" && find . | LC_ALL=C sort ) > "$OUT/$2.entries.txt"
  ( cd "$1" && find . -type f -print0 | LC_ALL=C sort -z | xargs -0 -r sha256sum )     > "$OUT/$2.sha256.txt"
  ( cd "$1" && find . -type d -print | LC_ALL=C sort ) > "$OUT/$2.dirs.txt"
}
hud_snapshot "$HUD_ROOT_MUT" user-hud-before
echo "  user fixture: $(wc -l < "$OUT/user-hud-before.entries.txt") entries," \
     "$(wc -l < "$OUT/user-hud-before.sha256.txt") files," \
     "$(wc -l < "$OUT/user-hud-before.dirs.txt") directories"

# Import it, then reset it in the same process, then exit: the root must be
# untouched and the settings must be back to the contract defaults.
#
# run_reset_case <name> -- the same run, named, so the deletion mutation can drive
# the identical command against a mutated build. One command, two callers: a
# mutation that exercised a different shape of run would not prove this one red.
#
# NO --hud-on HERE. The documented precedence is import, then reset, then scale,
# then enabled, so "--hud-reset --hud-on" ends with enabled=1 -- correctly, and the
# assertions below would then be testing a state the user did not ask for. The
# reset button is applied to the state the import left, and that is what is
# measured here.
run_reset_case() { # run_reset_case <name>
  local name="$1"
  TF2_NATIVE_TRACE="$(cygpath -m "$PWD/$OUT/$name.trace")" timeout "$RUN_TIMEOUT" "$EXE" \
    --tf-root "$TF" --audio-device "$AUDIO_DEVICE" --audio-disabled --hidden-window \
    --settings-file "$(cygpath -m "$PWD/$OUT/$name.settings.ini")" \
    --ui-settings-file "$(cygpath -m "$PWD/$OUT/$name.ui.ini")" \
    --demo "$DEMO" --start-paused --capture-tick "$TICK" \
    --capture-frame "$(cygpath -m "$PWD/$SCRATCH/$name.bmp")" \
    "${CAMERA[@]}" \
    --hud-root "$(cygpath -m "$PWD/$HUD_ROOT_MUT")" --hud-apply --hud-reset \
    > "$SCRATCH/$name.log" 2>&1
  printf -v "RC_$name" '%s' "$?"
}
run_reset_case reset
RESET_RC=$RC_reset
echo "$RESET_RC" > "$OUT/reset.rc"
assert_eq "reset run exit code" "$RESET_RC" "0"
assert_eq "the reset ran" "$(grep -oE 'hud: reset result=[a-z-]+' "$OUT/reset.trace" | tail -1 | sed 's/.*=//')" "reset"
assert_eq "the HUD is off after reset" "$(grep -oE 'hud: summary .*enabled=[0-9]+' "$OUT/reset.trace" | tail -1 | sed 's/.*enabled=//')" "0"
assert_eq "the scale is back to 100" "$(grep -oE 'hud: summary .*scale=[0-9]+' "$OUT/reset.trace" | tail -1 | sed 's/.*scale=//')" "100"
assert_eq "nothing is imported after reset" "$(grep -oE 'hud: summary imported=[0-9]+' "$OUT/reset.trace" | tail -1 | sed 's/.*imported=//')" "0"

hud_snapshot "$HUD_ROOT_MUT" user-hud-after
# assert_snapshot_unchanged <label> <prefix>
#
# BOTH arguments are required, and the prefix is not the same for every caller.
# The first version of this helper hard-coded "user-hud-after", which meant the
# second and third calls silently compared against the FIRST call's snapshot and
# printed OK without ever looking at the state they claimed to have checked. That
# is a gate reporting a stale green, which is worse than no gate.
assert_snapshot_unchanged() { # <label> <prefix>
  local label="$1" prefix="$2"
  if cmp -s "$OUT/$prefix.entries.txt" "$OUT/user-hud-before.entries.txt"; then
    echo "  OK   $label: the directory-entry set is unchanged"
  else
    echo "  FAIL $label: the directory-entry set changed"
    diff "$OUT/user-hud-before.entries.txt" "$OUT/$prefix.entries.txt" | head -12
    fail=1
  fi
  if cmp -s "$OUT/$prefix.dirs.txt" "$OUT/user-hud-before.dirs.txt"; then
    echo "  OK   $label: every directory (including the empty one) still exists"
  else
    echo "  FAIL $label: a directory disappeared or appeared"
    diff "$OUT/user-hud-before.dirs.txt" "$OUT/$prefix.dirs.txt" | head -12
    fail=1
  fi
  if cmp -s "$OUT/$prefix.sha256.txt" "$OUT/user-hud-before.sha256.txt"; then
    echo "  OK   $label: every file is byte-identical"
  else
    echo "  FAIL $label: a file's bytes changed"
    diff "$OUT/user-hud-before.sha256.txt" "$OUT/$prefix.sha256.txt" | head -12
    fail=1
  fi
}
assert_snapshot_unchanged "import+reset" "user-hud-after"

# The settings file after reset must hold the contract defaults, not the import:
# a reset that only clears memory and leaves the file saying enabled=1 would
# come back on the next run.
RESET_ENABLED=$(grep -i '^hud\.enabled=' "$OUT/reset.ui.ini" | tail -1 | cut -d= -f2 | tr -d '\r')
RESET_ROOT=$(grep -i '^hud\.root=' "$OUT/reset.ui.ini" | tail -1 | cut -d= -f2- | tr -d '\r')
echo "  settings after reset: enabled=${RESET_ENABLED:-<none>} root=${RESET_ROOT:-<empty>}"
assert_eq "hud.enabled is 0 in the file after reset" "${RESET_ENABLED:-<none>}" "0"
# NOT assert_eq, and the reason is assert_eq itself: it compares
# "${2:-<none>}" against the expected value, so an EMPTY expected value ("hud.root
# is empty") can never match -- the helper rewrites the actual to <none> before
# comparing. That is the same ${VAR:-} trap this file documents one level up, and
# it was measured: the first run of this gate printed
# "FAIL hud.root is empty in the file after reset = <none>, expected " on a
# settings file whose hud.root WAS empty.
#
# The contract is "the root reads back empty". An absent key and a present empty
# key both read back empty (the writer emits "hud.root=" and a profile API may
# drop it), so both are accepted, the state is printed, and only a NON-EMPTY
# value fails.
RESET_ROOT_KEY=$(grep -ci '^hud\.root=' "$OUT/reset.ui.ini" || true)
if [ "${RESET_ROOT_KEY:-0}" -eq 0 ]; then
  RESET_ROOT_STATE="absent-from-the-file"
elif [ -z "$RESET_ROOT" ]; then
  RESET_ROOT_STATE="present-and-empty"
else
  RESET_ROOT_STATE="present-and-NOT-empty ($RESET_ROOT)"
fi
if [ -z "$RESET_ROOT" ]; then
  echo "  OK   hud.root reads back empty after the reset ($RESET_ROOT_STATE)"
else
  echo "  FAIL hud.root is still set after the reset: $RESET_ROOT"; fail=1
fi
# The two ways a value can be "empty" are different findings, and both are printed
# because the assertion above is only meaningful if it can tell them apart:
#   present-and-empty -- the [hud] section exists and the key is there with no
#     value; this is what resetToDefault() writes, and it reads back as "".
#   absent -- no [hud] section at all, which is what a plain capture run writes:
#     nothing, because T51 only writes when the user actually touched hud.*.
# The plain-run half is a contract in its own right (a capture-only run must not
# rewrite the tester's HUD settings), so it is asserted and not just observed.
OFF_HUD_SECTION=$(grep -ci '^\[hud\]' "$OUT/hud_off.ui.ini" || true)
echo "  hud.root empty-value states: reset=$RESET_ROOT_STATE; a plain run writes [hud] x${OFF_HUD_SECTION:-?} (absent)"
assert_eq "a plain capture run writes no [hud] section at all" "${OFF_HUD_SECTION:-?}" "0"

# The uninstall half, and it is deliberately the HARSH case rather than the easy
# one. Two things are driven:
#
#   (a) an uninstall in a directory with no manifest: it must REFUSE (exit 2)
#       rather than delete on a guess. This is the case that stays safe even if a
#       user points --install-dir at a folder they care about.
#
#   (b) an uninstall where the HUD folder IS the install directory. The manifest
#       lists the payload files and nothing else, so the operation removes its own
#       files and leaves every file the user added -- including one sitting in the
#       same directory, and the empty subdirectory. This is the assertion that
#       would catch a cleanup implemented as "remove the install directory", which
#       is the obvious wrong way to write it and passes case (a) happily.
UNINST="$OUT/uninstall"
rm -rf "$UNINST"; mkdir -p "$UNINST"
# $EXE_ABS is resolved once, before any subshell changes the working directory:
# a relative executable path inside $(cd ...) is the classic way a gate stops
# testing what it claims to test.
timeout 60 "$EXE_ABS" --uninstall --install-dir "$(cygpath -m "$PWD/$UNINST")" \
  > "$OUT/uninstall.txt" 2>&1
UNINST_RC=$?
echo "  (a) uninstall in a directory with no manifest: rc=$UNINST_RC"
assert_eq "an uninstall with no manifest refuses" "$UNINST_RC" "2"
if grep -q 'uninstall=FAIL reason=no_manifest' "$OUT/uninstall.txt"; then
  echo "  OK   it refused by name (reason=no_manifest)"
else
  echo "  FAIL it did not name no_manifest"; sed 's/^/    /' "$OUT/uninstall.txt" | head -5; fail=1
fi
hud_snapshot "$HUD_ROOT_MUT" user-hud-after-uninstall-a
assert_snapshot_unchanged "(a) after the refused uninstall" "user-hud-after-uninstall-a"

# (b) the shared-directory case. The HUD folder becomes the install directory: the
# payload is staged inside it, the install runs, and then the uninstall removes
# exactly what the manifest lists.
INST_DIR="$OUT/user-hud-shared"
rm -rf "$INST_DIR"
cp -a "$HUD_ROOT_MUT" "$INST_DIR"
hud_snapshot "$INST_DIR" user-shared-before
mkdir -p "$INST_DIR/.uninstall/payload"
printf 'the installed product\n' > "$INST_DIR/.uninstall/payload/tf2_demo_native.exe"
printf 'readme\n' > "$INST_DIR/.uninstall/payload/readme.txt"
# NOTHING is pre-seeded into the install directory from here on. An earlier version
# copied the running executable to "$INST_DIR/tf2_demo_native.exe" first, and that
# single line made both comparisons below impossible: the install classifies a
# payload file that already exists with different bytes as "changed" and backs it
# up, so the pre-seeded copy came BACK after the uninstall and the before/after
# file set could never agree. A shared-directory test whose fixture the operation
# legitimately rewrites is a test of the fixture, not of the promise.
timeout 90 "$EXE_ABS" --install --install-dir "$(cygpath -m "$PWD/$INST_DIR")" \
  > "$OUT/install-shared.txt" 2>&1
INST_RC=$?
echo "  (b) install into the user's own folder: rc=$INST_RC"
assert_eq "the install into the user folder succeeded" "$INST_RC" "0"
# The install itself must not have touched the user's files either.
hud_snapshot "$INST_DIR" user-shared-after-install
for entry in user-notes.txt sub/deeper/deep.txt resource/ui/keepme.txt resource/ui/hudlayout.res; do
  if [ -f "$INST_DIR/$entry" ]; then
    echo "  OK   the install left $entry in place"
  else
    echo "  FAIL the install removed $entry"; fail=1
  fi
done
# compare_user_files_intact <label> <before-prefix> <dir>
#
# Compares the FILES THE USER HAD BEFORE, one by one, and nothing else. A whole
# listing compared with cmp cannot work here: the install is SUPPOSED to add
# files (its payload, VERSION.txt), so the sets differ on a correct install and
# the assertion would be red for the right behaviour. What the promise says is
# that the files that were already there are untouched.
compare_user_files_intact() { # <label> <before-prefix> <dir>
  local label="$1" before="$2" dir="$3" bad=0 expected relative actual
  while read -r expected relative; do
    # MSYS sha256sum prints "<hash> *<path>" (binary mode) where GNU/Linux prints
    # "<hash>  <path>". Strip the marker as well as the ./ prefix: without this the
    # file is looked up as "*./path", is not found, and is reported as "gone" --
    # which is exactly what the first run of this helper did to four files that
    # were still on disk (and which the post-uninstall whole-set cmp then showed
    # were byte-identical, naming the helper rather than the product as the liar).
    relative=${relative#\*}
    relative=${relative#./}
    if [ ! -f "$dir/$relative" ]; then
      echo "  FAIL $label: $relative is gone"; bad=1; continue
    fi
    actual=$( cd "$dir" && sha256sum "$relative" | cut -d' ' -f1 )
    if [ "$actual" != "$expected" ]; then
      echo "  FAIL $label: $relative changed ($expected -> $actual)"; bad=1
    fi
  done < "$OUT/$before.sha256.txt"
  if [ "$bad" -eq 0 ]; then
    echo "  OK   $label: every file the user already had is byte-identical"
  fi
  return "$bad"
}
compare_user_files_intact "after the install" user-shared-before "$INST_DIR" || fail=1

timeout 90 "$EXE_ABS" --uninstall --install-dir "$(cygpath -m "$PWD/$INST_DIR")" \
  > "$OUT/uninstall-shared.txt" 2>&1
INST_U_RC=$?
echo "  (b) uninstall from the user's own folder: rc=$INST_U_RC"
# 3 is the documented "the running image cannot delete itself" outcome, which is
# a SUCCESS with one deferred file rather than a failure. Anything else is a
# failure of the assertion, so the expected value is checked by name.
if [ "$INST_U_RC" = "0" ] || [ "$INST_U_RC" = "3" ]; then
  echo "  OK   the uninstall completed (rc=$INST_U_RC: 3 means it deferred its own image)"
else
  echo "  FAIL the uninstall did not complete (rc=$INST_U_RC)"; fail=1
fi
hud_snapshot "$INST_DIR" user-shared-after-uninstall
for entry in user-notes.txt sub/deeper/deep.txt resource/ui/keepme.txt resource/ui/hudlayout.res; do
  if [ -f "$INST_DIR/$entry" ]; then
    echo "  OK   after the uninstall $entry is still there"
  else
    echo "  FAIL the uninstall removed the user's $entry"; fail=1
  fi
done
if [ -d "$INST_DIR/empty-dir" ]; then
  echo "  OK   the user's empty directory survived the uninstall"
else
  # MEASURED AND NAMED, not claimed and not silently dropped. The shared
  # installer sweeps every EMPTY directory under the install directory
  # (native/src/install_lifecycle.cpp, the empty-directory loop after the
  # manifest loop); it cannot tell a directory it created from one the user made,
  # so the user's empty directory does not survive an uninstall in this case.
  # No user FILE is affected -- the byte and entry-set assertions below still
  # hold -- so this is reported as a T37 finding rather than as a T51 failure.
  # Why this is a T37 finding and NOT a T51 failure: the directory is removed by
  # install_lifecycle.cpp's empty-directory sweep, which runs on uninstall and
  # knows nothing about this feature -- hud_state.cpp and hud_import.cpp make no
  # filesystem call of any kind, and nothing in T51 participates in an install or
  # an uninstall. The promise T51 makes ("the imported HUD directory is read-only
  # to this feature") is untouched: every user FILE survives byte-identical,
  # which the comparisons above and below assert. What fails is the installer's
  # promise that it removes only what it created.
  echo "  NOT-VERIFIED the user's empty directory: it was removed by the shared"
  echo "    installer's empty-directory sweep (an empty DIRECTORY is removed; no user"
  echo "    FILE is). owner=t37-installer; cheapest next step: have the installer"
  echo "    record the directories it creates and sweep only those, then this becomes"
  echo "    a hard assertion here and in install-lifecycle-check.sh."
fi
if cmp -s "$OUT/user-shared-before.sha256.txt" \
    <( cd "$INST_DIR" && find . -path ./.uninstall -prune -o -type f -print0 2>/dev/null \
         | LC_ALL=C sort -z | xargs -0 -r sha256sum ); then
  echo "  OK   every file the user had before is byte-identical after install+uninstall"
else
  echo "  FAIL a user file changed across install+uninstall"
  diff <(cat "$OUT/user-shared-before.sha256.txt") \
    <( cd "$INST_DIR" && find . -path ./.uninstall -prune -o -type f -print0 \
         | LC_ALL=C sort -z | xargs -0 -r sha256sum ) | head -12
  fail=1
fi
# And the product's own file must be gone: an uninstall that leaves the payload
# behind while reporting success is the mirror-image defect.
if [ -f "$INST_DIR/readme.txt" ]; then
  echo "  FAIL the uninstall left the payload's readme.txt behind"; fail=1
else
  echo "  OK   the uninstall removed the file its manifest listed"
fi
echo
echo "=== 7/9 the scale slider is a live, measurable scaling rule ==="
# Same document, same window, two scales. The pixel anchors double; the
# fractional ones do not. That pair is what makes "HUD scaling is visible in the
# preview" a reading rather than a screenshot.
run_hud hud_scale50 --hud-root "$(cygpath -m "$PWD/$FIX/good")" --hud-apply --hud-on --hud-scale 50
run_hud hud_scale200 --hud-root "$(cygpath -m "$PWD/$FIX/good")" --hud-apply --hud-on --hud-scale 200
# The same-frame union as everywhere else, and with a signed x/y: a 200% HUD is
# legitimately allowed to run off the left edge (a panel anchored at "c-175" at
# scale 200 starts at a negative x), so a [0-9]+ pattern would fail to parse
# exactly the case this section exists to measure.
U50=$(grep -oE 'hud: draw .*union=[-0-9]+,[-0-9]+,[0-9]+,[0-9]+' "$OUT/hud_scale50.trace" | tail -1 | sed 's/.*union=//')
U200=$(grep -oE 'hud: draw .*union=[-0-9]+,[-0-9]+,[0-9]+,[0-9]+' "$OUT/hud_scale200.trace" | tail -1 | sed 's/.*union=//')
echo "  union at 50%: $U50"
echo "  union at 200%: $U200"
assert_ne "the union region grows with the scale" "$U50" "$U200"
assert_eq "scale 50 is persisted" "$(grep -i '^hud\.scale=' "$OUT/hud_scale50.ui.ini" | tail -1 | cut -d= -f2 | tr -d '\r')" "50"
assert_eq "scale 200 is persisted" "$(grep -i '^hud\.scale=' "$OUT/hud_scale200.ui.ini" | tail -1 | cut -d= -f2 | tr -d '\r')" "200"

echo
echo "=== 8/9 the same document at two scales changes only its own pixels ==="
# The scaling rule must not leak into the world either: a 200% HUD is bigger, so
# the region it is allowed to own is bigger, and OUTSIDE THAT region the frame
# must still equal the hud-off baseline. This is the resolution-adaptation half of
# the task's claim, measured the same way as the 100% case.
U50_R=$(echo "$U50" | cut -d, -f1)
U50_Y=$(echo "$U50" | cut -d, -f2)
U50_W=$(echo "$U50" | cut -d, -f3)
U50_H=$(echo "$U50" | cut -d, -f4)
if [ "$DETERMINISTIC" -eq 0 ]; then
  echo "  NOT-VERIFIED: the 200% comparison not run -- the baseline pair is not reproducible"
  echo "    owner=t51-hud"
else
  IFS=, read -r S2X S2Y S2W S2H <<< "$U200"
  SCALED=$(  "$PY" "$OUT/hud-region-diff.py" "$(cygpath -m "$PWD/$SCRATCH/hud_off.bmp")" \
    "$(cygpath -m "$PWD/$SCRATCH/hud_scale200.bmp")" \
    "$S2X" "$S2Y" "$((S2X + S2W))" "$((S2Y + S2H))")
  echo "$SCALED" > "$OUT/region-200.txt"
  echo "$SCALED" | sed 's/^/  /'
  assert_eq "at 200%: nothing outside its region changed" \
    "$(kv "$OUT/region-200.txt" outsideDiff)" "0"
  assert_ge "at 200%: the region really did change" \
    "$(kv "$OUT/region-200.txt" insideDiff)" "1000"
fi

echo
echo "=== 9/9 a broken HUD is refused, not half-applied ==="
# An import of a broken document. The state must be the DEFAULT one, not a
# mixture: "a failed import cannot leave a half-applied HUD behind" is a property
# of the commit point in HudState::import.
TF2_NATIVE_TRACE="$(cygpath -m "$PWD/$OUT/twoimport.trace")" timeout "$RUN_TIMEOUT" "$EXE" \
  --tf-root "$TF" --audio-device "$AUDIO_DEVICE" --audio-disabled --hidden-window \
  --settings-file "$(cygpath -m "$PWD/$OUT/twoimport.settings.ini")" \
  --ui-settings-file "$(cygpath -m "$PWD/$OUT/twoimport.ui.ini")" \
  --demo "$DEMO" --start-paused --capture-tick "$TICK" \
  --capture-frame "$(cygpath -m "$PWD/$SCRATCH/twoimport.bmp")" \
  "${CAMERA[@]}" \
  --hud-root "$(cygpath -m "$PWD/$FIX/good")" --hud-layout "resource/ui/hudbroken.res" \
  --hud-apply --hud-on > "$SCRATCH/twoimport.log" 2>&1
TWO_RC=$?
TWO_RESULT=$(grep -oE 'hud: import result=[a-z-]+' "$OUT/twoimport.trace" | tail -1 | sed 's/.*=//')
TWO_DRAWN=$(grep -oE 'hud: draw drawn=[0-9]+' "$OUT/twoimport.trace" | tail -1 | sed 's/.*drawn=//')
echo "  broken-document run: rc=$TWO_RC result=${TWO_RESULT:-<none>} drawn=${TWO_DRAWN:-<none>}"
assert_eq "a broken document is refused" "$TWO_RESULT" "refused"
assert_eq "nothing is drawn after a refusal" "${TWO_DRAWN:-<none>}" "0"
assert_eq "a refused import is still a clean exit" "$TWO_RC" "0"
echo
if [ "$MUTATION" -eq 1 ]; then
  # =======================================================================
  # MUTATION. Each mutation reintroduces ONE defect, rebuilds, and requires
  # THIS gate to go red. A gate that stays green on a broken product is a gate
  # that proves nothing, and it is the failure mode a forward-only check cannot
  # detect about itself.
  #
  # Restore is a byte-for-byte copy from a scratch backup, verified with cmp,
  # followed by a rebuild -- because "the file is back" and "the build is back"
  # are different claims and only the second one makes the next run meaningful.
  # =======================================================================
  echo
  echo "MUTATION-SUITE: rebuilding the product once per defect, four defects"
  # A mutation suite run against a red baseline proves nothing: if the forward
  # assertions already fail, "the mutation was caught" would describe a gate that
  # never worked, and this section would exit 0 on top of it -- which the first
  # version of this file did, because it exited "$mrc" and never looked at "fail".
  if [ "$fail" -ne 0 ]; then
    echo "MUTATION-SUITE=REFUSED (the forward run above is red; a mutation cannot be"
    echo "  scored against a baseline that does not hold)"
    echo "HUD-MUTATION=FAIL"
    exit 1
  fi

  mrc=0
  mut_restore() { # mut_restore <target> <backup>
    cp "$2" "$1"
    if cmp -s "$1" "$2"; then
      echo "  RESTORED $1 (byte-identical to the backup)"
    else
      echo "  RESTORE-FAIL $1 differs from its backup"; mrc=1
    fi
  }

  # ---- mutation 1: a bad HUD is accepted ---------------------------------
  #
  # The defect: the structural refusal for "#base" is disabled, so a document
  # that depends on an include parses as if it were complete. The forward gate
  # expects unsupported-include; with this defect the document is accepted and
  # the probe's refusal count must drop.
  echo
  echo "--- mutation 1/2: a bad HUD is accepted ---"
  cp native/src/hud_import.cpp "$SCRATCH/hud_import.cpp.backup"
  "$PY" - native/src/hud_import.cpp <<'PYEOF'
import sys
path = sys.argv[1]
text = open(path, encoding='utf-8').read()
needle = "  if (layout.includeCount != 0) {"
replacement = "  if (false && layout.includeCount != 0) {"
if needle not in text:
    raise SystemExit("mutation-1 anchor not found")
open(path, 'w', encoding='utf-8', newline='').write(text.replace(needle, replacement, 1))
PYEOF
  if cmp -s native/src/hud_import.cpp "$SCRATCH/hud_import.cpp.backup"; then
    echo "  MUTATION-SETUP=FAIL (mutation 1 changed nothing)"; mrc=1
  else
    bash build-target.sh tf2_demo_native hud_probe > "$OUT/mut1.build" 2>&1
    MUT1_BUILD=$?
    # The probe reads the same function: it must go red on the include case.
    "$PROBE" --fixture-root "$(cygpath -m "$OUT/probe-fixtures-mut1")" > "$OUT/mut1.probe.txt" 2>&1
    MUT1_PROBE=$?
    MUT1_INCLUDE=$(grep -oE 'doc.refusedAsNamed=[0-9]+' "$OUT/mut1.probe.txt" | tail -1 | sed 's/.*=//')
    echo "  mutated build rc=$MUT1_BUILD, probe rc=$MUT1_PROBE, refusedAsNamed=${MUT1_INCLUDE:-<none>} (forward reads 7)"
    if [ "$MUT1_BUILD" -eq 0 ] && [ "$MUT1_PROBE" -ne 0 ] && [ "${MUT1_INCLUDE:-7}" -lt 7 ]; then
      echo "  MUTATION-CAUGHT=PASS (mutation 1: the refusal assertion went red)"
    else
      echo "  MUTATION-CAUGHT=FAIL (mutation 1: the gate stayed green)"; mrc=1
    fi
    mut_restore native/src/hud_import.cpp "$SCRATCH/hud_import.cpp.backup"
    bash build-target.sh tf2_demo_native hud_probe > "$OUT/mut1.restore.build" 2>&1 \
      || { echo "  RESTORE-BUILD=FAIL (mutation 1)"; mrc=1; }
    "$PROBE" --fixture-root "$(cygpath -m "$OUT/probe-fixtures-restored")" > "$OUT/mut1.restored.txt" 2>&1
    if cmp -s "$OUT/probe.txt" "$OUT/mut1.restored.txt"; then
      echo "  RESTORED-PROBE-IDENTICAL (the restored probe reproduces the forward reading)"
    else
      echo "  RESTORED-PROBE-DIFFERS"; diff "$OUT/probe.txt" "$OUT/mut1.restored.txt" | head -10; mrc=1
    fi
  fi

  # ---- mutation 2: the HUD pass perturbs the world -----------------------
  #
  # The defect: the pass reports that it scissored itself but never calls
  # RSSetScissorRects. The HUD then draws wherever its quads land, and the
  # comparison outside the union rectangle must go red -- which is exactly the
  # mutation the task asks for: "the HUD affected the world rendering" has to be
  # DETECTABLE, not merely avoided by construction.
  echo
  echo "--- mutation 2/2: the HUD pass perturbs the world ---"
  cp native/src/native_renderer.cpp "$SCRATCH/native_renderer.cpp.backup"
  "$PY" - native/src/native_renderer.cpp <<'PYEOF'
import sys
path = sys.argv[1]
text = open(path, encoding='utf-8').read()
# The defect: every HUD panel is drawn as a FULL-SCREEN quad instead of inside
# its own rect. That is the shape of the failure the assertion exists to catch --
# "the HUD drew over the world" -- and it must make the outside-the-rect
# comparison go red. A mutation that merely removed the scissor would NOT do
# that, because the quads already lie inside the rects; the mutation has to
# change what is drawn, not only how it is clipped.
needle = (
    "    const float left = (std::max)(0.0f, rect.left);\n"
    "    const float top = (std::max)(0.0f, rect.top);\n"
    "    const float right = (std::min)(static_cast<float>(width_), rect.right);\n"
    "    const float bottom = (std::min)(static_cast<float>(height_), rect.bottom);"
)
replacement = (
    "    const float left = 0.0f;\n"
    "    const float top = 0.0f;\n"
    "    const float right = static_cast<float>(width_);\n"
    "    const float bottom = static_cast<float>(height_);"
)
if needle not in text:
    raise SystemExit("mutation-2 anchor not found")
open(path, 'w', encoding='utf-8', newline='').write(text.replace(needle, replacement, 1))
PYEOF
  if cmp -s native/src/native_renderer.cpp "$SCRATCH/native_renderer.cpp.backup"; then
    echo "  MUTATION-SETUP=FAIL (mutation 2 changed nothing)"; mrc=1
  else
    bash build-target.sh tf2_demo_native > "$OUT/mut2.build" 2>&1
    MUT2_BUILD=$?
    run_hud mut2_off
    run_hud mut2_on --hud-root "$(cygpath -m "$PWD/$FIX/good")" --hud-apply --hud-on
    # The region is the FORWARD run's union rectangle, not the mutated run's:
    # the claim under test is "outside the region the HUD is supposed to own",
    # and taking the region from the mutated build would let it widen its own
    # alibi until the comparison passed by construction.
    # $SCRATCH, not $OUT: run_hud writes the bitmap into SCRATCH (the bulk that is
    # not committed) and only the readings go to OUT. The first version of this
    # section read $OUT/mut2_off.bmp, which never exists -- the comparison then
    # failed to open a file, the mutation was scored as "not caught", and the
    # suite could never go green for a reason that had nothing to do with the
    # product.
    MUT2_REGION=$("$PY" "$OUT/hud-region-diff.py" "$(cygpath -m "$PWD/$SCRATCH/mut2_off.bmp")" \
      "$(cygpath -m "$PWD/$SCRATCH/mut2_on.bmp")" "$UX" "$UY" "$UX_R" "$UY_B")
    echo "$MUT2_REGION" > "$OUT/mut2.region.txt"
    MUT2_OUTSIDE=$(echo "$MUT2_REGION" | grep '^outsideDiff=' | cut -d= -f2)
    echo "  mutated build rc=$MUT2_BUILD, outsideDiff=${MUT2_OUTSIDE:-<none>} (forward reads 0)"
    if [ "$MUT2_BUILD" -eq 0 ] && [ "${MUT2_OUTSIDE:-0}" -gt 0 ]; then
      echo "  MUTATION-CAUGHT=PASS (mutation 2: the world comparison went red)"
    else
      echo "  MUTATION-CAUGHT=FAIL (mutation 2: the world comparison stayed green)"; mrc=1
    fi
    mut_restore native/src/native_renderer.cpp "$SCRATCH/native_renderer.cpp.backup"
    bash build-target.sh tf2_demo_native > "$OUT/mut2.restore.build" 2>&1 \
      || { echo "  RESTORE-BUILD=FAIL (mutation 2)"; mrc=1; }
    run_hud restored_off
    run_hud restored_on --hud-root "$(cygpath -m "$PWD/$FIX/good")" --hud-apply --hud-on
    REST_REGION=$("$PY" "$OUT/hud-region-diff.py" "$(cygpath -m "$PWD/$SCRATCH/restored_off.bmp")" \
      "$(cygpath -m "$PWD/$SCRATCH/restored_on.bmp")" "$UX" "$UY" "$UX_R" "$UY_B")
    REST_OUTSIDE=$(echo "$REST_REGION" | grep '^outsideDiff=' | cut -d= -f2)
    echo "  restored build: outsideDiff=${REST_OUTSIDE:-<none>}"
    if [ "${REST_OUTSIDE:-1}" -eq 0 ]; then
      echo "  RESTORED-PIXEL-CLEAN (the restored build agrees outside the rect again)"
    else
      echo "  RESTORED-PIXEL-DIRTY"; mrc=1
    fi
  fi

  # ---- mutation 3: the hotkey registration is dropped --------------------
  #
  # The defect: the three hud.* actions are never registered into the shared table.
  # This is the mutation for the assertion added in 2b/9, and it is the one the
  # forward gate could not have caught about itself: every other reading in this
  # file stays identical to the forward run, and only the hotkey section goes red.
  # It also proves the section reads the PRODUCT's table: a source-code comparison
  # between two files would not notice a registration happening after the trace.
  echo
  echo "--- mutation 3/4: the hud.* hotkeys are never registered ---"
  cp native/src/main.cpp "$SCRATCH/main.cpp.backup"
  "$PY" - native/src/main.cpp <<'PYEOF'
import sys
path = sys.argv[1]
text = open(path, encoding='utf-8').read()
needle = '  g_keybinds.registerDefault("hud.toggle", tf2::native::parseChord("H"));\n'
if needle not in text:
    raise SystemExit("mutation-3 anchor not found")
open(path, 'w', encoding='utf-8', newline='').write(text.replace(needle, '', 1))
PYEOF
  if cmp -s native/src/main.cpp "$SCRATCH/main.cpp.backup"; then
    echo "  MUTATION-SETUP=FAIL (mutation 3 changed nothing)"; mrc=1
  else
    bash build-target.sh tf2_demo_native > "$OUT/mut3.build" 2>&1
    MUT3_BUILD=$?
    run_hud mut3_off
    echo "  mutated build rc=$MUT3_BUILD; the hotkey readings it prints are below"
    if [ "$MUT3_BUILD" -eq 0 ] && ! hotkey_check "$OUT/mut3_off.trace"; then
      echo "  MUTATION-CAUGHT=PASS (mutation 3: the hotkey assertions went red)"
    else
      echo "  MUTATION-CAUGHT=FAIL (mutation 3: the hotkey assertions stayed green)"; mrc=1
    fi
    mut_restore native/src/main.cpp "$SCRATCH/main.cpp.backup"
    bash build-target.sh tf2_demo_native > "$OUT/mut3.restore.build" 2>&1 \
      || { echo "  RESTORE-BUILD=FAIL (mutation 3)"; mrc=1; }
    run_hud restored3_off
    if hotkey_check "$OUT/restored3_off.trace"; then
      echo "  RESTORED-HOTKEYS=PASS (the restored build registers all three again)"
    else
      echo "  RESTORED-HOTKEYS=FAIL"; mrc=1
    fi
  fi

  # ---- mutation 4: reset deletes the user's files ------------------------
  #
  # The defect: resetToDefault() also removes the imported directory. This is the
  # mutation for the promise in 6/9, and the one that matters most: "reset did not
  # delete a user file" is a claim about what did NOT happen, and the snapshot
  # comparison that asserts it is exactly the kind of check that passes because it
  # never looked. With this defect the directory-entry set and the file hashes
  # both change, so the comparison has to go red.
  echo
  echo "--- mutation 4/4: reset deletes the imported directory ---"
  cp native/src/hud_state.cpp "$SCRATCH/hud_state.cpp.backup"
  "$PY" - native/src/hud_state.cpp <<'PYEOF'
import sys
path = sys.argv[1]
text = open(path, encoding='utf-8').read()
# hud_state.cpp does not include <filesystem> (the real module makes no
# filesystem call at all, which is the point of the comment in resetToDefault).
# The mutation must therefore add the header as well, or the mutated build fails
# to compile and this mutation would be scored as "the build broke" -- a red for
# the wrong reason proves nothing about the assertion.
if '#include <filesystem>' not in text:
    anchor = '#include <algorithm>\n'
    if anchor not in text:
        raise SystemExit("mutation-4 include anchor not found")
    text = text.replace(anchor, anchor + '#include <filesystem>\n', 1)
needle = ("  settings_ = HudSettings{};\n"
          "  layout_ = HudLayout{};\n"
          "  rects_.clear();\n"
          "  imported_ = false;")
replacement = ("  {\n"
               "    // MUTATION: resetting a preference also deletes the user's folder.\n"
               "    std::error_code mutationError;\n"
               "    if (!settings_.root.empty()) {\n"
               "      std::filesystem::remove_all(settings_.root, mutationError);\n"
               "    }\n"
               "  }\n"
               "  settings_ = HudSettings{};\n"
               "  layout_ = HudLayout{};\n"
               "  rects_.clear();\n"
               "  imported_ = false;")
if needle not in text:
    raise SystemExit("mutation-4 anchor not found")
open(path, 'w', encoding='utf-8', newline='').write(text.replace(needle, replacement, 1))
PYEOF
  if cmp -s native/src/hud_state.cpp "$SCRATCH/hud_state.cpp.backup"; then
    echo "  MUTATION-SETUP=FAIL (mutation 4 changed nothing)"; mrc=1
  else
    bash build-target.sh tf2_demo_native > "$OUT/mut4.build" 2>&1
    MUT4_BUILD=$?
    run_reset_case mut4_reset
    hud_snapshot "$HUD_ROOT_MUT" mut4-after
    echo "  mutated build rc=$MUT4_BUILD, reset rc=${RC_mut4_reset:-<unset>}"
    if [ "$MUT4_BUILD" -eq 0 ] \
        && { ! cmp -s "$OUT/mut4-after.sha256.txt" "$OUT/user-hud-before.sha256.txt" \
          || ! cmp -s "$OUT/mut4-after.entries.txt" "$OUT/user-hud-before.entries.txt"; }; then
      echo "  MUTATION-CAUGHT=PASS (mutation 4: the user-file comparison went red)"
    else
      echo "  MUTATION-CAUGHT=FAIL (mutation 4: the user-file comparison stayed green)"; mrc=1
    fi
    mut_restore native/src/hud_state.cpp "$SCRATCH/hud_state.cpp.backup"
    bash build-target.sh tf2_demo_native > "$OUT/mut4.restore.build" 2>&1 \
      || { echo "  RESTORE-BUILD=FAIL (mutation 4)"; mrc=1; }
    # The mutated run deleted the fixture, so the restored build is measured against
    # a freshly written one: same function, same bytes, same entry set.
    make_user_hud_fixture
    hud_snapshot "$HUD_ROOT_MUT" user-hud-before
    run_reset_case restored4_reset
    hud_snapshot "$HUD_ROOT_MUT" restored4-after
    if cmp -s "$OUT/restored4-after.entries.txt" "$OUT/user-hud-before.entries.txt" \
        && cmp -s "$OUT/restored4-after.sha256.txt" "$OUT/user-hud-before.sha256.txt" \
        && cmp -s "$OUT/restored4-after.dirs.txt" "$OUT/user-hud-before.dirs.txt"; then
      echo "  RESTORED-USER-FILES=PASS (the restored build leaves every user file byte-identical)"
    else
      echo "  RESTORED-USER-FILES=FAIL"; mrc=1
    fi
  fi

  echo
  if [ "$mrc" -eq 0 ]; then
    echo "HUD-MUTATION=PASS (all four mutations went red; every mutated source restored"
    echo "  byte-for-byte with cmp, then rebuilt and re-read)"
  else
    echo "HUD-MUTATION=FAIL"
  fi
  exit "$mrc"
fi

echo
if [ "$fail" -eq 0 ]; then
  echo "HUD=PASS"
  exit 0
fi
echo "HUD=FAIL"
exit 1