#!/usr/bin/env bash
# resource-discovery-check.sh -- T9 (2026-10-16): is the map search order real,
# and does the release gate still read a POV title?
#
# WHY THIS FILE EXISTS
#   T4 recorded two adjacent findings that this task owns:
#     (a) a demo whose BSP lives only in tf/download/maps was rejected, because
#         the product built exactly one path -- <tf>/maps/<map>.bsp -- and asked
#         the VPKs for the same string;
#     (b) the release gate's load_title matched the literal "SourceTV ticks=",
#         so a POV recording produced no ticks at all: not a parse error, an
#         ABSENT reading, which the end-tick assertion then read as 0. Every POV
#         case failed unconditionally and the failure looked like a stall.
#   Both are fixed where their contract lives: (a) in the product's lookup,
#   (b) in the gate's parser. This script is the reading that says so.
#
# WHY THE RUNS ARE SPLIT FROM THE JUDGING
#   Every reading here is a window title and a trace file, both belonging to a
#   product process that has to be launched and closed by PowerShell. This file
#   judges a directory that resource-discovery-capture.ps1 has already filled,
#   and refuses to judge one it did not see produced: with --forward it requires
#   the capture directory as an argument, so a stale directory cannot be read as
#   fresh evidence by accident.
#
# USAGE
#   bash resource-discovery-check.sh --forward <capture-dir>
#   bash resource-discovery-check.sh --mutation <capture-dir>
# Exit 0 = every assertion held (forward) / every mutation was caught (mutation).
#
# --mutation perturbs two real files and rebuilds:
#   1. map_lookup.cpp    -> drop the download/maps candidate
#   2. stability-release-check.sh -> restore the "SourceTV ticks=" coupling
#   A gate that has never been seen red is not known to be a gate. Both are
#   restored and compared byte-identical afterwards; a mutation that fails to be
#   caught is a FAIL, not a note.

set -uo pipefail

MODE="${1:---forward}"
DIR="${2:-}"
[ "$MODE" = "--forward" ] || [ "$MODE" = "--mutation" ] || { echo "usage: $0 --forward|--mutation <capture-dir>"; exit 2; }
[ -n "$DIR" ] || { echo "usage: $0 $MODE <capture-dir>"; exit 2; }
[ -d "$DIR" ] || { echo "no capture directory: $DIR"; exit 2; }

REPO="$(cd "$(dirname "$0")" && pwd)"
PROBE="$REPO/native/build-nmake/map_lookup_probe.exe"
GATE="$REPO/stability-release-check.sh"
BUILD="$REPO/native/build-nmake"

CHECKS=0
FAIL=0
CASES=0
MUTATIONS=0

sec() { echo; echo "=== $1 ==="; }
ok()   { echo "  OK   $1 = $2"; CHECKS=$((CHECKS + 1)); }
wrong() { echo "  FAIL $1 = $2, expected $3"; CHECKS=$((CHECKS + 1)); FAIL=$((FAIL + 1)); }
eq()   { if [ "$2" = "$3" ]; then ok "$1" "$2"; else wrong "$1" "$2" "$3"; fi; }
true_() { if [ "$2" = "1" ]; then ok "$1" "$2"; else wrong "$1" "$2" "1"; fi; }

# ---- one field out of the product's own reading -------------------------
# Taken from the trace line, not from the arguments the harness passed: the
# argument is what was asked for, the reading is what the product decided.
field() { # <trace file> <field name>
  sed -n "s/.*map: lookup .*\b$2=\([^ ]*\).*/\1/p" "$1" 2>/dev/null | head -1
}
reason_of() { sed -n 's/.*map: lookup .* reason=//p' "$1" 2>/dev/null | head -1; }
bsp_of() { # the BSP segment of the window title
  sed -n 's/.*BSP: \([^,]*\).*/\1/p' "$1" 2>/dev/null | head -1
}
tris_of() { # "99612 tris" -> 99612 ; "invalid/BSP header is invalid" -> ""
  case "$1" in *" tris") printf '%s' "${1% tris}";; *) printf '';; esac
}

# ---- the gate's own parser, not a copy of it ---------------------------
# load_title is lifted out of stability-release-check.sh at run time, so this
# script cannot drift from the release gate about what a title means.
declare -A T
if [ -f "$GATE" ]; then
  eval "$(awk '/^load_title\(\)/,/^}/' "$GATE")"
else
  echo "the release gate is absent: $GATE"; exit 2
fi
title_ticks() { # <case dir> -> sets TICKS, DEMO and LABEL
  T=(); load_title "$1" >/dev/null 2>&1 || true
  TICKS="${T[ticks]:-}"; DEMO="${T[demo]:-}"
  # The recording label is the SECOND field of the Demo segment, not the first:
  # the segment is "<map> <label> ticks=...". Reading the first field as the
  # label compares a map name against a list of labels and can never match.
  LABEL="$(printf '%s' "$DEMO" | awk '{print $2}')"
}

# ---------------------------------------------------------------- 1. order
sec "1/5 the search order, measured without the product"
if [ ! -x "$PROBE" ]; then echo "  FAIL probe not built: $PROBE"; exit 2; fi
PROBE_OUT="$(cd "$BUILD" && ./map_lookup_probe.exe 2>&1)"
PROBE_BAD="$(printf '%s\n' "$PROBE_OUT" | sed -n 's/.*"bad":\([0-9]*\).*/\1/p' | tail -1)"
PROBE_N="$(printf '%s\n' "$PROBE_OUT" | sed -n 's/.*"checks":\([0-9]*\).*/\1/p' | tail -1)"
printf '%s\n' "$PROBE_OUT" | sed 's/^/  /' | tail -4
# Pinned so the section cannot pass by shrinking: 26 checks is what the probe
# printed on the run this count was taken from, and a probe that silently drops
# the download-maps assertion would otherwise report a green empty run.
eq "map_lookup_probe bad" "$PROBE_BAD" "0"
eq "map_lookup_probe checks (pinned)" "$PROBE_N" "26"

# ------------------------------------------------------- 2. the three stores
sec "2/5 which store answered, and what the BSP parse made of it"
check_case() { # <dir> <expected store> <expected selected> <expect tris? 1|0>
  local d="$1" estore="$2" esel="$3" etris="$4" name
  name="$(basename "$d")"
  CASES=$((CASES + 1))
  local tr="$d/trace.txt" ti="$d/title-last.txt"
  [ -f "$tr" ] || { echo "  FAIL $name: no trace.txt"; FAIL=$((FAIL + 1)); return; }
  [ -f "$ti" ] || { echo "  FAIL $name: no title-last.txt"; FAIL=$((FAIL + 1)); return; }
  # Ownership first: the title is a window reading, and a window of this class
  # belonging to another process is not evidence about this run. The harness
  # found that out by producing one.
  local wpid cpid
  wpid="$(cat "$d/window-pid.txt" 2>/dev/null)"; cpid="$(cat "$d/child-pid.txt" 2>/dev/null)"
  if [ -n "$wpid" ] && [ -n "$cpid" ] && [ "$wpid" = "$cpid" ] && [ "$cpid" != "0" ]; then
    ok "$name title belongs to its own run" "pid=$cpid"
  else
    wrong "$name title belongs to its own run" "window=$wpid child=$cpid" "equal and non-zero"
  fi
  local store sel bytes reason bsp tris
  store="$(field "$tr" store)"; sel="$(field "$tr" selected)"
  bytes="$(field "$tr" bytes)"; reason="$(reason_of "$tr")"
  bsp="$(bsp_of "$ti")"; tris="$(tris_of "$bsp")"
  echo "  $name: store=$store selected=$sel bytes=$bytes"
  echo "  $name: reason=$(printf '%s' "$reason" | head -c 70)  BSP=$bsp"
  eq "$name store" "$store" "$estore"
  eq "$name selected" "$sel" "$esel"
  if [ "$etris" = "1" ]; then
    if [ -n "$tris" ] && [ "$tris" -gt 0 ] 2>/dev/null; then ok "$name bsp tris" "$tris"
    else wrong "$name bsp tris" "${bsp:-<none>}" "a positive triangle count"; fi
    if [ -n "$bytes" ] && [ "$bytes" -gt 0 ] 2>/dev/null; then ok "$name bytes" "$bytes"
    else wrong "$name bytes" "${bytes:-<none>}" "> 0"; fi
  else
    # The consequence has to be visible, not just absent: a map the search did
    # not find must leave the BSP parser with nothing to parse, and the title
    # says so in the product's own words.
    case "$bsp" in invalid*) ok "$name bsp" "invalid (reported)";; *) wrong "$name bsp" "${bsp:-<none>}" "invalid/...";; esac
    eq "$name bytes" "$bytes" "0"
  fi
  [ -n "$reason" ] && ok "$name reason present" "yes"
  printf '%s' "$reason" > "$d/reason.txt"
  printf '%s' "$bsp" > "$d/bsp.txt"
}

check_case "$DIR/installed-map"    "maps"          "maps/cp_snakewater_final1.bsp"      1
check_case "$DIR/download-gully"   "download-maps" "download/maps/cp_gullywash_f9.bsp"  1
check_case "$DIR/download-process" "download-maps" "download/maps/cp_process_f12.bsp"   1
check_case "$DIR/missing-map"      "none"          "-"                                  0

# ---- the hostile name: what actually happens, not what would be nice ----
# demo_header.cpp refuses a name containing a separator before the lookup ever
# runs (validMapName), so this case does NOT exercise planMapLookup: the demo is
# rejected at parse and the product loads its documented default instead. That is
# the behaviour worth pinning, and pinning anything else here would be a claim
# about a defence this reading does not reach.
sec "3/5 a name that is not a name"
CASES=$((CASES + 1))
TRV="$DIR/traversal-map/trace.txt"
if [ -f "$TRV" ]; then
  TNAME="$(field "$TRV" map)"
  TSEL="$(field "$TRV" selected)"
  echo "  traversal-map: map=$TNAME selected=$TSEL"
  # The hostile string must not appear in any lookup reading: if it did, the
  # product would be building a path out of it.
  if grep -q "t9_probe" "$TRV"; then wrong "traversal name reached the lookup" "yes" "no"
  else ok "traversal name reached the lookup" "no"; fi
  eq "traversal-map fallback map" "$TNAME" "2koth_abbey"
else
  echo "  FAIL traversal-map: no trace.txt"; FAIL=$((FAIL + 1))
fi

# ------------------------------------------------------- 4. the title parser
sec "4/5 the release gate's parser against the titles it was given"
# The product owns the title format (" ticks=<n>" after the recording label);
# the gate owns the tolerance. What is asserted is that the parser reads every
# label the classifier can emit, and that it reads nothing when there is no
# ticks= key to read -- an absent reading is exactly how the bug hid.
title_ticks "$DIR/installed-map"
echo "  installed-map: label=[$LABEL] map=[$(printf '%s' "$DEMO" | awk '{print $1}')] ticks=[$TICKS]"
eq "pov label read from a POV title" "$LABEL" "POV"
if [ -n "$TICKS" ] && [ "$TICKS" -gt 0 ] 2>/dev/null; then ok "pov ticks" "$TICKS"
else wrong "pov ticks" "${TICKS:-<unset>}" "a positive integer"; fi

# SourceTV has to keep working: widening the parser must not be a trade of one
# label for another.
title_ticks "$DIR/download-gully"
echo "  download-gully: label=[$LABEL] map=[$(printf '%s' "$DEMO" | awk '{print $1}')] ticks=[$TICKS]"
eq "sourcetv label still accepted" "$LABEL" "SourceTV"
if [ -n "$TICKS" ] && [ "$TICKS" -gt 0 ] 2>/dev/null; then ok "sourcetv ticks" "$TICKS"
else wrong "sourcetv ticks" "${TICKS:-<unset>}" "a positive integer"; fi

# Negative: a title with no Demo segment yields no reading, not a zero.
NEG="$DIR/parser-fixtures/no-ticks"
mkdir -p "$NEG"
printf 'TF2 Demo Player - TF2 资源未连接 [VPK entries: 0]\n' > "$NEG/title-last.txt"
title_ticks "$NEG"
eq "no-ticks title yields no demo" "$DEMO" ""
eq "no-ticks title yields no ticks" "$TICKS" ""

# ------------------------------------------------------------- 5. verdict
sec "5/5 verdict"
if [ "$MODE" = "--forward" ]; then
  echo "  cases=$CASES checks=$CHECKS mutations=0"
  if [ "$FAIL" -eq 0 ]; then echo "  RESOURCE-DISCOVERY-CHECK=PASS"; exit 0; fi
  echo "  RESOURCE-DISCOVERY-CHECK=FAIL fail=$FAIL"; exit 1
fi

# ================================================================== mutation
# Two perturbations, each aimed at a different section. Neither is a canned
# print: the source is edited, the affected target is rebuilt, and the section
# has to turn red.
MUT_DIR="$DIR/../$(basename "$DIR" | sed 's/-forward$//')-mutation"
mkdir -p "$MUT_DIR"

# nmake is not on PATH in this shell, so `cmake --build --target` fails with
# "Generator: build tool execution failed" and the mutation would be judged
# against a binary that was never rebuilt -- a green probe reading a stale
# executable, which is exactly the false pass a mutation pass exists to catch.
# These three are the same values build-cmake.sh sets, for the same reason it
# sets them: vcvars64.bat calls reg.exe, which this sandbox refuses.
MSVC_VER="14.44.35207"
SDK_VER="10.0.26100.0"
export PATH="/c/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/$MSVC_VER/bin/Hostx64/x64:/c/Program Files (x86)/Windows Kits/10/bin/$SDK_VER/x64:$PATH"
export INCLUDE="C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/$MSVC_VER/include;C:/Program Files (x86)/Windows Kits/10/Include/$SDK_VER/ucrt;C:/Program Files (x86)/Windows Kits/10/Include/$SDK_VER/shared;C:/Program Files (x86)/Windows Kits/10/Include/$SDK_VER/um;C:/Program Files (x86)/Windows Kits/10/Include/$SDK_VER/winrt"
export LIB="C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/$MSVC_VER/lib/x64;C:/Program Files (x86)/Windows Kits/10/Lib/$SDK_VER/ucrt/x64;C:/Program Files (x86)/Windows Kits/10/Lib/$SDK_VER/um/x64"
command -v nmake >/dev/null 2>&1 || { echo "  FAIL nmake is not on PATH; the mutation cannot be built"; exit 2; }
SRC_CPP="$REPO/native/src/map_lookup.cpp"
BAK_CPP="$MUT_DIR/map_lookup.cpp.orig"
cp "$SRC_CPP" "$BAK_CPP"

sec "M1 drop the download/maps candidate"
python - "$SRC_CPP" <<'PY'
import io, sys
p = sys.argv[1]
s = io.open(p, encoding='utf-8').read()
old = '  plan.candidates.push_back({"download/maps/" + file, "download-maps", false});\n'
if old not in s:
    sys.exit('M1 anchor absent')
io.open(p, 'w', encoding='utf-8', newline='').write(s.replace(old, '', 1))
print('  mutated: download/maps candidate removed')
PY
[ $? -eq 0 ] || { echo "  FAIL M1 could not be applied"; exit 1; }
cmake --build "$BUILD" --config Release --target map_lookup_probe > "$MUT_DIR/m1-build.log" 2>&1
M1_RC=$?
echo "  build rc=$M1_RC"
# The build has to succeed for the next line to mean anything: a mutation that
# never compiled leaves the old probe in place, and "bad=0" would then be a
# statement about the unmutated source.
eq "M1 build succeeded" "$M1_RC" "0"
M1_OUT="$(cd "$BUILD" && ./map_lookup_probe.exe 2>&1)"
M1_BAD="$(printf '%s\n' "$M1_OUT" | sed -n 's/.*"bad":\([0-9]*\).*/\1/p' | tail -1)"
M1_N="$(printf '%s\n' "$M1_OUT" | sed -n 's/.*"checks":\([0-9]*\).*/\1/p' | tail -1)"
MUTATIONS=$((MUTATIONS + 1))
if [ "$M1_BAD" != "0" ]; then ok "M1 caught by the probe" "bad=$M1_BAD of $M1_N"
else wrong "M1 caught by the probe" "bad=0 of $M1_N" "bad>0"; fi
cp "$SRC_CPP" "$MUT_DIR/map_lookup.cpp.mutated"
cp "$BAK_CPP" "$SRC_CPP"
cmp -s "$SRC_CPP" "$BAK_CPP" && ok "M1 source restored byte-identical" "yes" || wrong "M1 source restored byte-identical" "no" "yes"

sec "M2 restore the SourceTV-only tick parse"
BAK_SH="$MUT_DIR/stability-release-check.sh.orig"
cp "$GATE" "$BAK_SH"
python - "$GATE" <<'PY'
import io, sys
p = sys.argv[1]
s = io.open(p, encoding='utf-8').read()
old = '''  T[ticks]=""
  if [ -n "${T[demo]:-}" ]; then
    if [[ "${T[demo]}" =~ (^|[[:space:]])ticks=([0-9]+)([[:space:]]|$) ]]; then
      T[ticks]="${BASH_REMATCH[2]}"
    fi
  fi
'''
new = '''  T[ticks]=""
  if [[ "${T[demo]:-}" =~ SourceTV\\ ticks=([0-9]+) ]]; then
    T[ticks]="${BASH_REMATCH[1]}"
  fi
'''
if old not in s:
    sys.exit('M2 anchor absent')
io.open(p, 'w', encoding='utf-8', newline='').write(s.replace(old, new, 1))
print('  mutated: ticks parsed from the SourceTV label again')
PY
[ $? -eq 0 ] || { echo "  FAIL M2 could not be applied"; exit 1; }
eval "$(awk '/^load_title\(\)/,/^}/' "$GATE")"
title_ticks "$DIR/installed-map"
MUTATIONS=$((MUTATIONS + 1))
echo "  under M2: label=[$LABEL] map=[$(printf '%s' "$DEMO" | awk '{print $1}')] ticks=[${TICKS:-<absent>}]"
if [ -z "$TICKS" ]; then ok "M2 caught: the POV tick reading is gone" "absent"
else wrong "M2 caught: the POV tick reading is gone" "$TICKS" "absent"; fi
cp "$GATE" "$MUT_DIR/stability-release-check.sh.mutated"
cp "$BAK_SH" "$GATE"
cmp -s "$GATE" "$BAK_SH" && ok "M2 source restored byte-identical" "yes" || wrong "M2 source restored byte-identical" "no" "yes"

# The restore is only worth its name if the tree builds green again from it:
# a probe left holding the mutated object would make the next forward run a
# statement about a binary that does not match the source it was judged against.
sec "restore: rebuild and re-run the probe"
cmake --build "$BUILD" --target map_lookup_probe > "$MUT_DIR/restore-build.log" 2>&1
echo "  build rc=$?"
R_OUT="$(cd "$BUILD" && ./map_lookup_probe.exe 2>&1)"
R_BAD="$(printf '%s\n' "$R_OUT" | sed -n 's/.*"bad":\([0-9]*\).*/\1/p' | tail -1)"
R_N="$(printf '%s\n' "$R_OUT" | sed -n 's/.*"checks":\([0-9]*\).*/\1/p' | tail -1)"
eq "restored probe bad" "$R_BAD" "0"
eq "restored probe checks" "$R_N" "26"

echo
echo "  cases=$CASES checks=$CHECKS mutations=$MUTATIONS"
if [ "$FAIL" -eq 0 ]; then echo "  RESOURCE-DISCOVERY-CHECK-MUTATION=PASS (both mutations caught)"; exit 0; fi
echo "  RESOURCE-DISCOVERY-CHECK-MUTATION=FAIL fail=$FAIL"; exit 1
