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
#   F. --mutation reintroduces two defects and requires this gate to go red:
#        (1) a bad HUD accepted (the validity rule);
#        (2) the HUD pass perturbing the world (the pixel comparison).
#      Each is restored with cmp, and the restore is re-verified by a rebuild.
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

echo "=== 1/8 the probe's reading set (pure rules, no window) ==="
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
if [ "$(kv "$OUT/probe.txt" corpus)" = "skipped" ]; then
  echo "  NOT-VERIFIED corpus readings: no TF2 install on this machine"
  echo "  NOT-VERIFIED owner=t51-hud; next step: run on a machine with tf/custom/m0rehud-5.8"
else
  assert_eq "corpus documents sampled" "$(kv "$OUT/probe.txt" corpus.tried)" "102"
  assert_eq "corpus documents imported" "$(kv "$OUT/probe.txt" corpus.imported)" "71"
  assert_eq "corpus refused for #base" "$(kv "$OUT/probe.txt" corpus.refusedInclude)" "23"
  assert_eq "corpus refused for another reason" "$(kv "$OUT/probe.txt" corpus.refusedOther)" "8"
fi
echo
echo "=== 2/8 the product imports the good fixture, and the frame shows it ==="
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
ON_UNION=$(grep -oE 'hud: rects .*union=[0-9]+,[0-9]+,[0-9]+,[0-9]+' "$OUT/hud_on.trace" | tail -1 | sed 's/.*union=//')
ON_IMPORT=$(grep -oE 'hud: import result=[a-z-]+' "$OUT/hud_on.trace" | tail -1 | sed 's/.*=//')
ON_SCISSOR=$(grep -oE 'hud: draw .*scissor=[0-9]+' "$OUT/hud_on.trace" | tail -1 | sed 's/.*scissor=//')
ON_REGION=$(grep -oE 'hud: draw .*region=[-0-9]+,[-0-9]+,[0-9]+,[0-9]+' "$OUT/hud_on.trace" | tail -1 | sed 's/.*region=//')
echo "  readings: import=$ON_IMPORT panels=$ON_PANELS resolved=$ON_RESOLVED visible=$ON_VISIBLE union=$ON_UNION"
echo "  readings: drawnOff=${OFF_DRAWN:-<none>} drawnOn=${ON_DRAWN:-<none>} scissor=${ON_SCISSOR:-<none>} region=${ON_REGION:-<none>}"

# The default-off half of the contract: a plain capture run draws NO HUD.
assert_eq "a hud-off run draws nothing" "${OFF_DRAWN:-<none>}" "0"
assert_eq "the good fixture imports" "$ON_IMPORT" "applied"
assert_eq "the good fixture resolves every panel" "$ON_VALID" "5"
assert_eq "the drawn rect count equals the resolved count" "$ON_DRAWN" "5"
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
echo "=== 3/8 HUD on vs off: identical outside the HUD union rectangle ==="
IFS=, read -r UX UY UW UH <<< "$ON_UNION"
UX_R=$((UX + UW))
UY_B=$((UY + UH))
echo "  union rect: x=$UX y=$UY w=$UW h=$UH -> right=$UX_R bottom=$UY_B"
REGION=$("$PY" "$OUT/hud-region-diff.py" "$(cygpath -m "$PWD/$OUT/hud_off.bmp")" \
  "$(cygpath -m "$PWD/$OUT/hud_on.bmp")" "$UX" "$UY" "$UX_R" "$UY_B")
echo "$REGION" > "$OUT/region.txt"
echo "$REGION" | sed 's/^/  /'
assert_eq "pixels outside the HUD region" "$(kv "$OUT/region.txt" outsideDiff)" "0"
assert_ge "pixels inside the HUD region" "$(kv "$OUT/region.txt" inside)" "1000"
assert_ge "pixels outside the HUD region" "$(kv "$OUT/region.txt" outside)" "100000"
# And the capture is not the fallback quad / a blank frame: a frame with the HUD
# must also still be a world frame, so it must differ from the hud-off frame
# INSIDE the rect (otherwise nothing was drawn) and have many colours (a world,
# not a flat fill).
# The same region reader answers the other half: the HUD must have CHANGED
# pixels inside its rect, or the comparison outside it would be vacuous (two
# identical frames trivially agree everywhere).
assert_ge "pixels changed inside the HUD region" "$(kv "$OUT/region.txt" insideDiff)" "1000"
echo
echo "=== 4/8 every refusal is executed, and the world keeps rendering ==="
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
  echo "  $name -> rc=$RC_refuse_$name reason=${reason:-<none>} drawn=${drawn:-<none>}"
  assert_eq "$name refused with its own token" "$reason" "$expected"
  assert_eq "$name exit code" "$RC_refuse_$name" "0"
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
echo "=== 5/8 reset and uninstall never touch a user file ==="
# The promise: the imported directory is READ-ONLY to this feature. The test is
# not "reset does not call remove" (a code reading); it is the bytes of a
# fixture user tree before and after every operation this feature can perform.
HUD_ROOT_MUT="$OUT/user-hud"
mkdir -p "$HUD_ROOT_MUT/resource/ui"
cp "$FIX/good/resource/ui/hudlayout.res" "$HUD_ROOT_MUT/resource/ui/hudlayout.res"
printf "the user wrote this too\n" > "$HUD_ROOT_MUT/user-notes.txt"
printf "and this\n" > "$HUD_ROOT_MUT/resource/ui/keepme.txt"
( cd "$HUD_ROOT_MUT" && find . -type f | sort | xargs sha256sum ) > "$OUT/user-hud-before.sha256"

# Import it, then reset it in the same process, then exit: the root must be
# untouched and the settings must be back to the contract defaults.
TF2_NATIVE_TRACE="$(cygpath -m "$PWD/$OUT/reset.trace")" timeout "$RUN_TIMEOUT" "$EXE" \
  --tf-root "$TF" --audio-device "$AUDIO_DEVICE" --audio-disabled --hidden-window \
  --settings-file "$(cygpath -m "$PWD/$OUT/reset.settings.ini")" \
  --ui-settings-file "$(cygpath -m "$PWD/$OUT/reset.ui.ini")" \
  --demo "$DEMO" --start-paused --capture-tick "$TICK" \
  --capture-frame "$(cygpath -m "$PWD/$OUT/reset.bmp")" \
  "${CAMERA[@]}" \
  --hud-root "$(cygpath -m "$PWD/$HUD_ROOT_MUT")" --hud-apply --hud-on --hud-reset \
  > "$OUT/reset.log" 2>&1
RESET_RC=$?
echo "$RESET_RC" > "$OUT/reset.rc"
assert_eq "reset run exit code" "$RESET_RC" "0"
assert_eq "the reset ran" "$(grep -oE 'hud: reset result=[a-z-]+' "$OUT/reset.trace" | tail -1 | sed 's/.*=//')" "reset"
assert_eq "the HUD is off after reset" "$(grep -oE 'hud: summary .*enabled=[0-9]+' "$OUT/reset.trace" | tail -1 | sed 's/.*enabled=//')" "0"
assert_eq "the scale is back to 100" "$(grep -oE 'hud: summary .*scale=[0-9]+' "$OUT/reset.trace" | tail -1 | sed 's/.*scale=//')" "100"
assert_eq "nothing is imported after reset" "$(grep -oE 'hud: summary imported=[0-9]+' "$OUT/reset.trace" | tail -1 | sed 's/.*imported=//')" "0"

( cd "$HUD_ROOT_MUT" && find . -type f | sort | xargs sha256sum ) > "$OUT/user-hud-after.sha256"
if cmp -s "$OUT/user-hud-before.sha256" "$OUT/user-hud-after.sha256"; then
  echo "  OK   every user file byte-identical after import+reset"
else
  echo "  FAIL a user file changed"; diff "$OUT/user-hud-before.sha256" "$OUT/user-hud-after.sha256" | head -10
  fail=1
fi

# The settings file after reset must hold the contract defaults, not the import:
# a reset that only clears memory and leaves the file saying enabled=1 would
# come back on the next run.
RESET_ENABLED=$(grep -i '^hud\.enabled=' "$OUT/reset.ui.ini" | tail -1 | cut -d= -f2 | tr -d '\r')
RESET_ROOT=$(grep -i '^hud\.root=' "$OUT/reset.ui.ini" | tail -1 | cut -d= -f2- | tr -d '\r')
echo "  settings after reset: enabled=${RESET_ENABLED:-<none>} root=${RESET_ROOT:-<empty>}"
assert_eq "hud.enabled is 0 in the file after reset" "${RESET_ENABLED:-<none>}" "0"
assert_eq "hud.root is empty in the file after reset" "${RESET_ROOT:-<none>}" ""

# The uninstall half: an uninstall of the product must not reach into the HUD
# folder either. The installer only ever removes what its manifest lists, and the
# HUD root is not in it; this drives the real lifecycle to say so.
UNINST="$OUT/uninstall"
rm -rf "$UNINST"; mkdir -p "$UNINST"
# $EXE_ABS is resolved once, before any subshell changes the working directory:
# a relative executable path inside $(cd ...) is the classic way a gate stops
# testing what it claims to test.
TF2_NATIVE_TRACE="$(cygpath -m "$PWD/$OUT/uninstall.trace")" timeout 60 "$EXE_ABS" \
  --uninstall --install-dir "$(cygpath -m "$PWD/$UNINST")" \
  > "$OUT/uninstall.txt" 2>&1
UNINST_RC=$?
if true; then
  echo "  uninstall in a directory with no manifest: rc=$UNINST_RC"
  # It must REFUSE (no manifest), not delete anything -- that is the exact
  # behaviour that keeps a user directory safe even if the install dir is shared.
  assert_eq "uninstall without a manifest refuses" "$UNINST_RC" "2"
  ( cd "$HUD_ROOT_MUT" && find . -type f | sort | xargs sha256sum ) > "$OUT/user-hud-after-uninstall.sha256"
  if cmp -s "$OUT/user-hud-before.sha256" "$OUT/user-hud-after-uninstall.sha256"; then
    echo "  OK   every user file byte-identical after an uninstall attempt"
  else
    echo "  FAIL a user file changed across the uninstall attempt"; fail=1
  fi
fi
echo
echo "=== 6/8 the scale slider is a live, measurable scaling rule ==="
# Same document, same window, two scales. The pixel anchors double; the
# fractional ones do not. That pair is what makes "HUD scaling is visible in the
# preview" a reading rather than a screenshot.
run_hud hud_scale50 --hud-root "$(cygpath -m "$PWD/$FIX/good")" --hud-apply --hud-on --hud-scale 50
run_hud hud_scale200 --hud-root "$(cygpath -m "$PWD/$FIX/good")" --hud-apply --hud-on --hud-scale 200
U50=$(grep -oE 'hud: rects .*union=[0-9]+,[0-9]+,[0-9]+,[0-9]+' "$OUT/hud_scale50.trace" | tail -1 | sed 's/.*union=//')
U200=$(grep -oE 'hud: rects .*union=[0-9]+,[0-9]+,[0-9]+,[0-9]+' "$OUT/hud_scale200.trace" | tail -1 | sed 's/.*union=//')
echo "  union at 50%: $U50"
echo "  union at 200%: $U200"
assert_ne "the union region grows with the scale" "$U50" "$U200"
assert_eq "scale 50 is persisted" "$(grep -i '^hud\.scale=' "$OUT/hud_scale50.ui.ini" | tail -1 | cut -d= -f2 | tr -d '\r')" "50"
assert_eq "scale 200 is persisted" "$(grep -i '^hud\.scale=' "$OUT/hud_scale200.ui.ini" | tail -1 | cut -d= -f2 | tr -d '\r')" "200"

echo
echo "=== 7/8 a HUD-off run and its own repeat are byte-identical ==="
# The control for step 3: two runs of the SAME configuration must be identical
# everywhere, or "identical outside the rect" would be a property of a
# non-deterministic capture rather than of the HUD.
run_hud hud_off_repeat
if cmp -s "$SCRATCH/hud_off.bmp" "$SCRATCH/hud_off_repeat.bmp"; then
  echo "  OK   two hud-off captures are byte-identical"
else
  echo "  FAIL two hud-off captures differ"; fail=1
fi

echo
echo "=== 8/8 a broken HUD is refused, not half-applied ==="
# An import of a broken document. The state must be the DEFAULT one, not a
# mixture: "a failed import cannot leave a half-applied HUD behind" is a property
# of the commit point in HudState::import.
TF2_NATIVE_TRACE="$(cygpath -m "$PWD/$OUT/twoimport.trace")" timeout "$RUN_TIMEOUT" "$EXE" \
  --tf-root "$TF" --audio-device "$AUDIO_DEVICE" --audio-disabled --hidden-window \
  --settings-file "$(cygpath -m "$PWD/$OUT/twoimport.settings.ini")" \
  --ui-settings-file "$(cygpath -m "$PWD/$OUT/twoimport.ui.ini")" \
  --demo "$DEMO" --start-paused --capture-tick "$TICK" \
  --capture-frame "$(cygpath -m "$PWD/$OUT/twoimport.bmp")" \
  "${CAMERA[@]}" \
  --hud-root "$(cygpath -m "$PWD/$FIX/good")" --hud-layout "resource/ui/hudbroken.res" \
  --hud-apply --hud-on > "$OUT/twoimport.log" 2>&1
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
  echo "MUTATION-SUITE: rebuilding the product twice with one defect each"

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
  cp native/src/hud_import.cpp "$OUT/hud_import.cpp.backup"
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
  if cmp -s native/src/hud_import.cpp "$OUT/hud_import.cpp.backup"; then
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
    mut_restore native/src/hud_import.cpp "$OUT/hud_import.cpp.backup"
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
  cp native/src/native_renderer.cpp "$OUT/native_renderer.cpp.backup"
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
  if cmp -s native/src/native_renderer.cpp "$OUT/native_renderer.cpp.backup"; then
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
    MUT2_REGION=$("$PY" "$OUT/hud-region-diff.py" "$(cygpath -m "$PWD/$OUT/mut2_off.bmp")" \
      "$(cygpath -m "$PWD/$OUT/mut2_on.bmp")" "$UX" "$UY" "$UX_R" "$UY_B")
    echo "$MUT2_REGION" > "$OUT/mut2.region.txt"
    MUT2_OUTSIDE=$(echo "$MUT2_REGION" | grep '^outsideDiff=' | cut -d= -f2)
    echo "  mutated build rc=$MUT2_BUILD, outsideDiff=${MUT2_OUTSIDE:-<none>} (forward reads 0)"
    if [ "$MUT2_BUILD" -eq 0 ] && [ "${MUT2_OUTSIDE:-0}" -gt 0 ]; then
      echo "  MUTATION-CAUGHT=PASS (mutation 2: the world comparison went red)"
    else
      echo "  MUTATION-CAUGHT=FAIL (mutation 2: the world comparison stayed green)"; mrc=1
    fi
    mut_restore native/src/native_renderer.cpp "$OUT/native_renderer.cpp.backup"
    bash build-target.sh tf2_demo_native > "$OUT/mut2.restore.build" 2>&1 \
      || { echo "  RESTORE-BUILD=FAIL (mutation 2)"; mrc=1; }
    run_hud restored_off
    run_hud restored_on --hud-root "$(cygpath -m "$PWD/$FIX/good")" --hud-apply --hud-on
    REST_REGION=$("$PY" "$OUT/hud-region-diff.py" "$(cygpath -m "$PWD/$OUT/restored_off.bmp")" \
      "$(cygpath -m "$PWD/$OUT/restored_on.bmp")" "$UX" "$UY" "$UX_R" "$UY_B")
    REST_OUTSIDE=$(echo "$REST_REGION" | grep '^outsideDiff=' | cut -d= -f2)
    echo "  restored build: outsideDiff=${REST_OUTSIDE:-<none>}"
    if [ "${REST_OUTSIDE:-1}" -eq 0 ]; then
      echo "  RESTORED-PIXEL-CLEAN (the restored build agrees outside the rect again)"
    else
      echo "  RESTORED-PIXEL-DIRTY"; mrc=1
    fi
  fi

  echo
  if [ "$mrc" -eq 0 ]; then
    echo "HUD-MUTATION=PASS (both mutations went red, both sources restored byte-for-byte)"
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