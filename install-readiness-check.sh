#!/usr/bin/env bash
# install-readiness-check.sh -- T32 (2026-10-20): can a fresh user install find
# the right resources, and does it fail clearly when they are absent or corrupt?
#
# WHY THIS GATE EXISTS, AND WHAT IT IS NOT
#   T9 and T15 built the lookup order and the archive reader; their gates measure
#   the developer tree and synthetic archives. Neither answers the T32 question,
#   which is about the package: does `cmake --install` produce a directory a
#   fresh user can run, does that installed binary pick the same stores, and what
#   happens when a resource is missing, empty or out of range? T9 explicitly did
#   NOT claim an installer from a staging directory, and this gate does not
#   either: it measures the staging rule and lists, by name, every installer
#   feature the tree still lacks (with its owner) as a release blocker.
#
# THE INSTALL DIRECTORY IS NOT THE DEVELOPER TREE
#   Every product run here uses <out>/install/tf2_demo_native.exe with the
#   install directory as its working directory. The build tree is only the
#   source `cmake --install` copies from, and the gate asserts the installed exe
#   is byte-identical to the built one. The synthetic store roots are built
#   fresh by install-readiness-fixtures.py from the pinned demo and map, so the
#   precedence readings cannot be satisfied by this machine real TF2 install.
#
# THREE KINDS OF FAILURE ARE KEPT APART
#   absent               a store simply does not have the name -> store=none
#   present-but-refused  a file is there and unreadable        -> vpk: refused
#   valid                the store answered                    -> store=<store>
#   The product change this gate pins is the middle one: before T32 the fixed
#   archive list was opened in silence, so a corrupt pak01_dir.vpk surfaced as
#   "map not found in maps, download/maps or a vpk" -- the wrong finding.
#
# USAGE
#   bash install-readiness-check.sh --forward <out-dir>
#   bash install-readiness-check.sh --mutation <out-dir>   (forward must have run)
# T47 (2026-10-21): COLD-CACHE DETERMINISM AND THE SEPARATE TITLE TIMEOUT
#   T43's cold run failed four assertions on first-launch/restart/explicit-root:
#   the title was still the window's load-time string (len=30, no "[VPK
#   entries:") while the same run's trace already reported resources: ...
#   tf_root=... valid=1. That is a title-capture timing failure, not a wrong
#   root decision, but a last-title assertion cannot tell the two apart. This
#   gate now (a) gives the title poll a documented bound (TITLE_BOUND_SECONDS),
#   (b) asserts the trace's root decision independently of the title, (c)
#   classifies trace-connects-but-title-never-settles as TITLE-CAPTURE-TIMEOUT
#   in the capture driver (never accepted as a connected title), and (d) runs a
#   SECOND first-launch/restart/explicit-root cycle from its own staged package
#   and its own settings directories and requires identical root-selection
#   decisions. A positive control in section 8b cuts the poll bound on purpose
#   and proves the timeout is reported separately.
#
# Exit 0 = every assertion held / every mutation was caught.

set -uo pipefail

MODE="${1:---forward}"
OUT_ARG="${2:-}"
if [ "$MODE" != "--forward" ] && [ "$MODE" != "--mutation" ]; then
  echo "usage: $0 --forward|--mutation <out-dir>"
  exit 2
fi
if [ -z "$OUT_ARG" ]; then
  echo "usage: $0 $MODE <out-dir>"
  exit 2
fi

REPO="$(cd "$(dirname "$0")" && pwd)"
cd "$REPO" || exit 2
mkdir -p "$OUT_ARG"
OUT="$(cd "$OUT_ARG" && pwd)"
mkdir -p "$OUT/logs"

BUILD="$REPO/native/build-nmake"
EXE="$BUILD/tf2_demo_native.exe"
SMOKE="$BUILD/native_install_smoke_probe.exe"
CHAIN="$BUILD/vpk_integrity_probe.exe"
TF_ROOT="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
REAL_DEMO="$TF_ROOT/demos/autorecord_2026-07-02_13-26-46.dem"
BSP_SOURCE="$TF_ROOT/maps/itemtest.bsp"
TF_ROOT_WIN="$(cygpath -w "$TF_ROOT" 2>/dev/null || printf "%s" "$TF_ROOT")"
REAL_DEMO_WIN="$(cygpath -w "$REAL_DEMO" 2>/dev/null || printf "%s" "$REAL_DEMO")"
CAPTURE_PS="$(cygpath -w "$REPO/install-readiness-capture.ps1" 2>/dev/null || printf "%s" "$REPO/install-readiness-capture.ps1")"
FIXTURES_PY="$(cygpath -w "$REPO/install-readiness-fixtures.py" 2>/dev/null || printf "%s" "$REPO/install-readiness-fixtures.py")"

# T47: the documented title poll bound, in seconds. The old value was 30 s,
# hardcoded at the capture call; T43's cold run hit it with the window still on
# its load-time title while the trace already said valid=1. The bound is a cap:
# title-capture stops polling as soon as the connected title appears, so a warm
# run costs its real load time, not this value.
#   120 s = 5.0x the slowest warm time-to-connected-title measured on this host
#   (24.0 s, T47-COLD-READINESS-2026-10-21.md) and 4x the old 30 s bound that the
#   cold run exceeded. The cold path itself cannot be reproduced on demand
#   without clearing system-wide caches (out of bounds), so the number is
#   anchored to the measured warm reading plus the forced-timeout control in
#   section 8b rather than to a cold measurement.
# A case that exceeds it is classified TITLE-CAPTURE-TIMEOUT in the capture
# driver and is NOT accepted as connected; the driver then aborts the remaining
# cases (CAPTURE-ABORT) instead of paying the bound thirteen times, and this gate
# stays red rather than lowering the value to make a title green.
TITLE_BOUND_SECONDS=120
# T47: aggregate ceiling on title-capture wall time per capture invocation, so a
# pathological host cannot push a forward run past the 30-minute host-lock hold.
# The measured warm forward capture spends ~7.5 minutes in title-capture (four
# connected lifecycle cases at ~21 s, the two unconnectable roots waiting out the
# bound, seven store cases at ~17 s); 600 s is that total plus ~35%. The ceiling
# counts capture time only. Reached -> CAPTURE-BUDGET-EXHAUSTED and a non-zero
# exit with the remaining cases named as skipped, never silently dropped.
CAPTURE_BUDGET_SECONDS=600
# The second cycle runs three connected cases only (~63 s warm); 240 s is ~3.8x.
CAPTURE_CYCLE_BUDGET_SECONDS=240

CHECKS=0
FAIL=0
MUTATIONS=0

sec() { echo; echo "=== $1 ==="; }
ok()   { echo "  OK   $1 = $2"; CHECKS=$((CHECKS + 1)); }
wrong() { echo "  FAIL $1 = $2, expected $3"; CHECKS=$((CHECKS + 1)); FAIL=$((FAIL + 1)); }
eq()   { if [ "$2" = "$3" ]; then ok "$1" "$2"; else wrong "$1" "$2" "$3"; fi; }
ne()   { if [ "$2" != "$3" ]; then ok "$1" "$2"; else wrong "$1" "$2" "not $3"; fi; }
has()  { case "$2" in *"$3"*) ok "$1" "yes";; *) wrong "$1" "no" "contains $3";; esac; }
hasnt(){ case "$2" in *"$3"*) wrong "$1" "yes" "absent";; *) ok "$1" "absent";; esac; }

trace_field() { # <trace> <key> -- the last value of key= on any line
  sed -n "s/.*[[:space:]]$2=\([^[:space:]]*\).*/\1/p" "$1" 2>/dev/null | tail -1
}
connected_root() { # <title-file> -- drive path before the VPK bracket, ASCII only
  sed -n "s/.*\([A-Za-z]:[^[]*\) \[VPK entries:.*/\1/p" "$1" 2>/dev/null | tail -1 | sed "s/[[:space:]]*$//"
}
title_refused() { # <title-file> -- the ", VPK refused: N" count
  sed -n "s/.*\[VPK entries: [0-9]*, VPK refused: \([0-9]*\).*/\1/p" "$1" 2>/dev/null | tail -1
}
bsp_segment() {
  sed -n "s/.*BSP: \([^,]*\).*/\1/p" "$1" 2>/dev/null | tail -1
}
tris_of() { case "$1" in *" tris") printf "%s" "${1% tris}";; *) printf "";; esac; }
norm_path() { local s="$1"; s="${s//\\//}"; printf "%s" "$s" | tr "A-Z" "a-z"; }

write_inventory() { # <install-dir> <file> -- relative path and size, stable order
  ( cd "$1" && find . -type f -printf "%P\t%s\n" | LC_ALL=C sort ) > "$2"
}

hash_list() { # <dir> -- "sha256  ./relative" for every file, stable order
  ( cd "$1" && find . -type f -print0 | LC_ALL=C sort -z | xargs -0 sha256sum )
}

run_capture() { # <install-dir> <out-dir> <cases> [only] [bound-seconds] [budget-seconds]
  local install="$1" outdir="$2" cases="$3" only="${4:-}" seconds="${5:-$TITLE_BOUND_SECONDS}" budget="${6:-$CAPTURE_BUDGET_SECONDS}"
  mkdir -p "$outdir"
  local args=(-NoProfile -ExecutionPolicy Bypass -File "$CAPTURE_PS"
    -InstallDir "$(cygpath -w "$install")"
    -FixtureDir "$(cygpath -w "$OUT/fixtures")"
    -OutDir "$(cygpath -w "$outdir")"
    -TfRoot "$TF_ROOT_WIN" -RealDemo "$REAL_DEMO_WIN"
    -Cases "$cases" -Seconds "$seconds" -BudgetSeconds "$budget")
  if [ -n "$only" ]; then args+=(-Only "$only"); fi
  powershell "${args[@]}" > "$outdir/capture.log" 2>&1
  local rc=$?
  grep -aE "^(CASE|cases=)" "$outdir/capture.log" | sed "s/^/  /"
  echo "  capture rc=$rc"
  return $rc
}

stage_install() { # <prefix> <log-name>
  local prefix="$1" name="$2" rc=0
  rm -rf "$prefix"
  cmake --install "$(cygpath -w "$BUILD")" --config Release --prefix "$(cygpath -w "$prefix")" \
    > "$OUT/logs/$name.log" 2>&1 || rc=$?
  echo "  stage $name rc=$rc prefix=$(basename "$prefix")"
  return $rc
}

case_dir() { printf "%s" "$OUT/cases/$1"; }
case_rc()  { sed "1s/^\xEF\xBB\xBF//" "$(case_dir "$1")/exit-code.txt" 2>/dev/null | tr -d "\r\n"; }
case_valid() { trace_field "$(case_dir "$1")/trace.txt" valid; }
case_store() { trace_field "$(case_dir "$1")/trace.txt" store; }
case_selected() { trace_field "$(case_dir "$1")/trace.txt" selected; }
case_connected() { connected_root "$(case_dir "$1")/title-last.txt"; }
case_bsp() { bsp_segment "$(case_dir "$1")/title-last.txt"; }
case_refused() { title_refused "$(case_dir "$1")/title-last.txt"; }
refused_reason() {
  sed -n "s/.*vpk: refused file=[^ ]* reason=\(.*\)/\1/p" "$(case_dir "$1")/trace.txt" 2>/dev/null | tail -1
}
archives_opened() { # <case> -- opened= value from the vpk summary line
  sed -n "s/.*vpk: archives opened=\([0-9]*\) refused=\([0-9]*\).*/\1/p" "$(case_dir "$1")/trace.txt" 2>/dev/null | tail -1
}
archives_refused() { # <case> -- refused= value from the vpk summary line
  sed -n "s/.*vpk: archives opened=\([0-9]*\) refused=\([0-9]*\).*/\2/p" "$(case_dir "$1")/trace.txt" 2>/dev/null | tail -1
}

# T47: directory-scoped readings, so the second cold-start cycle (a different
# cases root and a different staged package) is judged by the same functions as
# the first one. The decision tuple is the whole root reading:
#   exit code | trace valid | trace tf_root | connected root | VPK bracket
dir_rc()         { sed "1s/^\xEF\xBB\xBF//" "$1/exit-code.txt" 2>/dev/null | tr -d "\r\n"; }
dir_valid()      { sed -n "s/.*resources: .* valid=\([0-9]*\).*/\1/p" "$1/trace.txt" 2>/dev/null | tail -1; }
dir_trace_root() { sed -n "s/.*resources: .* tf_root=\(.*\) valid=[0-9].*/\1/p" "$1/trace.txt" 2>/dev/null | tail -1; }
dir_connected()  { connected_root "$1/title-last.txt"; }
dir_bracket()    { case "$(cat "$1/title-last.txt" 2>/dev/null)" in *"[VPK entries:"*) printf "yes";; *) printf "no";; esac; }
dir_decision()   { printf "%s|%s|%s|%s|%s" "$(dir_rc "$1")" "$(dir_valid "$1")" "$(norm_path "$(dir_trace_root "$1")")" "$(norm_path "$(dir_connected "$1")")" "$(dir_bracket "$1")"; }
readiness_class() { sed -n "s/^class=//p" "$1/readiness.txt" 2>/dev/null | tail -1; }
case_trace_root() { sed -n "s/.*resources: .* tf_root=\(.*\) valid=[0-9].*/\1/p" "$(case_dir "$1")/trace.txt" 2>/dev/null | tail -1; }
case_exe_path() { sed -n '1s/^\xEF\xBB\xBF//; 1s/^"//; 1s/"$//p' "$1/command.txt" 2>/dev/null | head -1; }
capture_failed()   { sed -n "s/.*failed=\([0-9]*\).*/\1/p" "$1/capture.log" 2>/dev/null | tail -1; }
capture_timeouts() { sed -n "s/.*titleTimeouts=\([0-9]*\).*/\1/p" "$1/capture.log" 2>/dev/null | tail -1; }
no_lifecycle_mode_runs() { # <root> -- command.txt files that asked for install modes
  find "$1" -name command.txt -print0 2>/dev/null | xargs -0 -r grep -lE -- '(^| )--(install|uninstall|version|install-status)( |$)' 2>/dev/null | wc -l | tr -d " "
}

product_processes() {
  powershell -NoProfile -Command "(Get-Process -Name tf2_demo_native -ErrorAction SilentlyContinue | Measure-Object).Count" 2>/dev/null | tr -d "[:space:]\r"
}

# ===================================================================== forward
if [ "$MODE" = "--forward" ]; then

sec "1/13 preconditions: pinned inputs and a build that matches the sources"
missing_inputs=0
for required in "$EXE" "$SMOKE" "$CHAIN" "$CAPTURE_PS" "$FIXTURES_PY"; do
  if [ ! -e "$required" ]; then wrong "input present ${required##*/}" "absent" "present"; missing_inputs=$((missing_inputs + 1)); fi
done
if [ "$missing_inputs" -gt 0 ]; then
  echo "  named failure: build the affected targets first:"
  echo "    bash build-target.sh tf2_demo_native native_install_smoke_probe vpk_integrity_probe"
  echo "  cases=0 checks=$CHECKS mutations=0"
  echo "  INSTALL-READINESS=FAIL"; exit 1
fi
for required in "$TF_ROOT" "$REAL_DEMO" "$BSP_SOURCE"; do
  eq "input present ${required##*/}" "$([ -e "$required" ] && echo yes || echo no)" "yes"
done
stale="$(find native/src native/include native/CMakeLists.txt -type f -newer "$EXE" 2>/dev/null | head -3)"
eq "product build is fresh against its sources" "$([ -z "$stale" ] && echo fresh || echo stale)" "fresh"
if [ -n "$stale" ]; then echo "  newer than the exe: $(printf "%s" "$stale" | tr "\n" " ")"; fi
eq "no product process before the run" "$(product_processes)" "0"

sec "2/13 stage a clean install with cmake --install"
INSTALL="$OUT/install"
stage_install "$INSTALL" install
eq "cmake --install rc" "$?" "0"
eq "install directory has the product" "$([ -x "$INSTALL/tf2_demo_native.exe" ] && echo yes || echo no)" "yes"

sec "3/13 the package: exact file list, sizes, hashes"
write_inventory "$INSTALL" "$OUT/package-files.txt"
# T37: the package is a LIFECYCLE package, not three loose files. The install
# rule now stages the payload it will lay down (<prefix>/.uninstall/payload/) and
# the VERSION.txt the product answers --version from, so the top-level list is the
# staged executables plus the payload directory's contents. Pinning the full list
# is deliberate: a file that appears in a released package without this line
# changing is exactly the drift this assertion exists to catch.
expected_package="$(printf ".uninstall/payload/install_smoke_probe.ps1\n.uninstall/payload/native_install_smoke_probe.exe\n.uninstall/payload/tf2_demo_native.exe\nVERSION.txt\ninstall_smoke_probe.ps1\nnative_install_smoke_probe.exe\ntf2_demo_native.exe")"
actual_package="$(cut -f1 "$OUT/package-files.txt")"
eq "package file list" "$actual_package" "$expected_package"
eq "package file count" "$(wc -l < "$OUT/package-files.txt" | tr -d " ")" "7"
eq "the payload repeats the three files the package stages at the top level" \
  "$(grep -c '^.uninstall/payload/' "$OUT/package-files.txt" | tr -d ' ')" "3"
eq "the package carries the version file the product reads first" \
  "$([ -f "$INSTALL/VERSION.txt" ] && echo yes || echo no)" "yes"
bad_sizes=0
while IFS="$(printf "\t")" read -r rel size; do
  [ -n "$rel" ] || continue
  if [ "${size:-0}" -gt 0 ]; then echo "  package $rel bytes=$size"; else bad_sizes=$((bad_sizes + 1)); echo "  package $rel bytes=$size (EMPTY)"; fi
done < "$OUT/package-files.txt"
eq "every packaged file is non-empty" "$bad_sizes" "0"
hash_list "$INSTALL" > "$OUT/package-hashes.txt"
eq "installed product is byte-identical to the built one" \
  "$(sha256sum "$INSTALL/tf2_demo_native.exe" | cut -d" " -f1)" \
  "$(sha256sum "$EXE" | cut -d" " -f1)"
hasnt "package carries no per-user settings file" "$(cat "$OUT/package-files.txt")" "settings.ini"
hasnt "package carries no cache directory" "$(cat "$OUT/package-files.txt")" "cache"

sec "4/13 runtime dependency closure of every installed executable"
DUMPBIN="$(ls -1 "/c/Program Files/Microsoft Visual Studio/2022/"*/VC/Tools/MSVC/*/bin/Hostx64/x64/dumpbin.exe 2>/dev/null | head -1)"
eq "dumpbin available for the import check" "$([ -n "$DUMPBIN" ] && echo yes || echo no)" "yes"
exe_imports() { # <exe> -- imported DLL names, lower case, sorted
  # MSYS2_ARG_CONV_EXCL keeps Git Bash from rewriting "/nologo" into a path: a
  # native exe receiving C:/Program Files/Git/nologo prints usage and no imports.
  MSYS2_ARG_CONV_EXCL="*" "$DUMPBIN" /nologo /dependents "$1" 2>/dev/null | sed -n "s/^[[:space:]]\{1,\}\([A-Za-z0-9_.-][A-Za-z0-9_.-]*\.dll\)[[:space:]]*$/\1/p" | tr "A-Z" "a-z" | sort -u
}
check_runtime_closure() { # <install> <exe> <label>
  local install="$1" exe="$2" label="$3" imports dll system=0 shipped="" missing="" msvc
  # The same exemption that keeps /nologo literal also keeps a POSIX path
  # literal, and a native exe resolves /d/... as <current drive>:\d\... .
  # So the exe is handed over as a Windows path and only the flags are exempt.
  imports="$(exe_imports "$(cygpath -w "$exe")")"
  eq "$label import list parsed" "$([ -n "$imports" ] && echo yes || echo no)" "yes"
  while IFS= read -r dll; do
    [ -n "$dll" ] || continue
    if [ -f "/c/Windows/System32/$dll" ]; then system=$((system + 1))
    elif [ -f "$install/$dll" ]; then shipped="$shipped $dll"
    else missing="$missing $dll"; fi
  done <<< "$imports"
  eq "$label runtime DLLs missing from the install" "${missing:-none}" "none"
  msvc="$(printf "%s\n" "$imports" | grep -E "^(vcruntime|msvcp|concrt|vccorlib)" || true)"
  eq "$label uses the statically linked MSVC runtime" "${msvc:-none}" "none"
  echo "  $label system=$(printf "%s\n" "$imports" | wc -l | tr -d " ") shipped=${shipped:-none}"
  printf "%s\n" "$imports" | sed "s/^/    import /" >> "$OUT/runtime-imports.txt"
}
: > "$OUT/runtime-imports.txt"
check_runtime_closure "$INSTALL" "$INSTALL/tf2_demo_native.exe" "tf2_demo_native.exe"
check_runtime_closure "$INSTALL" "$INSTALL/native_install_smoke_probe.exe" "native_install_smoke_probe.exe"

sec "5/13 the shipped layout script and the shipped smoke probe, run from the install"
SMOKE_LAYOUT_OUT="$(powershell -NoProfile -ExecutionPolicy Bypass -File "$(cygpath -w "$INSTALL/install_smoke_probe.ps1")" -InstallDir "$(cygpath -w "$INSTALL")" 2>&1)"
SMOKE_LAYOUT_RC=$?
printf "%s\n" "$SMOKE_LAYOUT_OUT" | sed "s/^/  /"
printf "%s\n" "$SMOKE_LAYOUT_OUT" > "$OUT/logs/install-smoke.log"
eq "install_smoke_probe.ps1 rc" "$SMOKE_LAYOUT_RC" "0"
has "install layout accepted by the shipped script" "$SMOKE_LAYOUT_OUT" "install_layout=ok"
has "the package still declares the TF2 assets unpackaged" "$SMOKE_LAYOUT_OUT" "tf2_assets_packaged=0"
PROBE_SMOKE_OUT="$("$INSTALL/native_install_smoke_probe.exe" 2>&1)"
PROBE_SMOKE_RC=$?
echo "  $PROBE_SMOKE_OUT"
eq "installed smoke probe rc" "$PROBE_SMOKE_RC" "0"
has "installed smoke probe readings" "$PROBE_SMOKE_OUT" "install_executable=1 tf_root_missing_prompt=1 warp_fallback=1"

sec "6/13 product store chain precedence, including the BSP pakfile (reused probe)"
# The installed package cannot stage a BSP-pakfile round trip on its own (that
# needs a map whose pakfile holds the material), so the chain order is read from
# the product own AssetStoreChain through the T15 probe rather than from a second
# implementation. The installed binary precedence reading is section 10.
CHAIN_OUT="$("$CHAIN" 2>&1)"
CHAIN_RC=$?
chain_value() {
  printf "%s\n" "$CHAIN_OUT" | sed -n "s/.*\"reading\":\"$1\",\"value\":\"\([^\"]*\)\".*/\1/p" | tail -1
}
printf "%s\n" "$CHAIN_OUT" | grep -a "vpk.precedence" | sed "s/^/  /" | head -8
eq "vpk_integrity_probe rc" "$CHAIN_RC" "0"
eq "chain: loose beats the VPKs" "$(chain_value vpk.precedence.loose-wins)" "loose"
eq "chain: first added archive answers" "$(chain_value vpk.precedence.directory-before-volume)" "vpk-directory"
eq "chain: pakfile answers last" "$(chain_value vpk.precedence.pakfile)" "pakfile"
eq "chain: pakfile cannot outrank loose" "$(chain_value vpk.precedence.pakfile-shadowed)" "loose"
eq "chain: an absent name is none, not a refusal" "$(chain_value vpk.precedence.absent)" "none"
has "chain probe summary" "$(chain_value vpk.summary)" "failures=0"

sec "7/13 clean-room store fixtures built from the pinned demo and map"
python "$FIXTURES_PY" --out "$(cygpath -w "$OUT/fixtures")" --demo "$REAL_DEMO_WIN" --bsp "$(cygpath -w "$BSP_SOURCE")" > "$OUT/logs/fixtures.log" 2>&1
FIXTURES_RC=$?
sed "s/^/  /" "$OUT/logs/fixtures.log" | head -12
eq "fixture build rc" "$FIXTURES_RC" "0"
eq "fixture root count" "$(ls -1 "$OUT/fixtures/roots" 2>/dev/null | wc -l | tr -d " ")" "8"
cp "$OUT/fixtures/inputs.sha256" "$OUT/fixtures-inputs.sha256" 2>/dev/null || true

sec "8/13 product runs: the installed binary, its own directory as cwd"
# T47: the staged binary is hashed again AT CAPTURE TIME, so every title/root
# reading below belongs to the binary section 3 hashed, not to a stale copy.
eq "staged binary hash at capture time" \
  "$(sha256sum "$INSTALL/tf2_demo_native.exe" | cut -d" " -f1)" \
  "$(sha256sum "$EXE" | cut -d" " -f1)"
CAPTURE_RC=0
run_capture "$INSTALL" "$OUT/cases" all || CAPTURE_RC=$?
eq "capture rc" "$CAPTURE_RC" "0"
eq "no title capture timeouts in the forward capture" "$(capture_timeouts "$OUT/cases")" "0"
eq "every forward case classified OK" "$(capture_failed "$OUT/cases")" "0"
eq "the capture launched the staged installed binary" \
  "$(norm_path "$(case_exe_path "$(case_dir first-launch)")")" \
  "$(norm_path "$(cygpath -w "$INSTALL/tf2_demo_native.exe")")"
# T47: the readiness captures never invoke a lifecycle mode. A stale installer
# or uninstaller executable (one left in an older staging directory) would show
# up here as a command line asking for --install/--uninstall/--version.
eq "no readiness run invoked an installer or uninstaller mode" "$(no_lifecycle_mode_runs "$OUT/cases")" "0"
eq "staged binary hash after the captures" \
  "$(sha256sum "$INSTALL/tf2_demo_native.exe" | cut -d" " -f1)" \
  "$(sha256sum "$EXE" | cut -d" " -f1)"

# Map/resource readings come from the lines that own them. `store=`, `bytes=` and
# `valid=` appear on other trace lines too (a lightmap byte count, a texture
# store); reading one of those as the map answer would be a reading about a
# different resource.
lookup_field() { # <case> <key> -- from the "map: lookup" line only
  sed -n "s/.*map: lookup .* $2=\([^ ]*\).*/\1/p" "$(case_dir "$1")/trace.txt" 2>/dev/null | tail -1
}
case_store()    { lookup_field "$1" store; }
case_selected() { lookup_field "$1" selected; }
case_bytes()    { lookup_field "$1" bytes; }
resources_valid() { sed -n "s/.*resources: .* valid=\([0-9]*\).*/\1/p" "$(case_dir "$1")/trace.txt" 2>/dev/null | tail -1; }
case_valid()    { resources_valid "$1"; }
case_requested() { sed -n "s/.*resources: requested=\(.*\) tf_root=.*/\1/p" "$(case_dir "$1")/trace.txt" 2>/dev/null | tail -1; }

sec "8b/13 positive control: a too-short poll bound is TITLE-CAPTURE-TIMEOUT, not a connected title"
# T47: the cold-cache classification must be provable on demand. Five seconds is
# shorter than this host's measured warm time-to-connected-title for the real
# root (recorded in T47-COLD-READINESS-2026-10-21.md), so the case is cut while
# the trace already reports valid=1 -- exactly the T43 shape. The control passes
# only if the capture names the timeout separately AND the title is not the
# connected form. If the product ever loaded inside five seconds the control
# would produce a bracket and fail here: that is the assertion, not a flake.
CONTROL_DIR="$OUT/cases-timeout-control"
rm -rf "$CONTROL_DIR"
CONTROL_RC=0
run_capture "$INSTALL" "$CONTROL_DIR" lifecycle first-launch 5 || CONTROL_RC=$?
eq "positive control: capture exits non-zero on a classified timeout" "$CONTROL_RC" "1"
eq "positive control: title timeouts counted" "$(capture_timeouts "$CONTROL_DIR")" "1"
eq "positive control: named TITLE-CAPTURE-TIMEOUT line" "$(grep -c '^TITLE-CAPTURE-TIMEOUT ' "$CONTROL_DIR/capture.log" | tr -d " ")" "1"
eq "positive control: trace still says the root connected" "$(dir_valid "$CONTROL_DIR/first-launch")" "1"
eq "positive control: no generic title-capture failure was involved" "$(grep -c 'TITLE-CAPTURE=FAIL' "$CONTROL_DIR/capture.log" | tr -d " ")" "0"
eq "positive control: title is not the connected form" "$(dir_bracket "$CONTROL_DIR/first-launch")" "no"
eq "positive control: no connected root was accepted" "$(dir_connected "$CONTROL_DIR/first-launch")" ""
eq "positive control: the case class is TITLE-CAPTURE-TIMEOUT" "$(readiness_class "$CONTROL_DIR/first-launch")" "TITLE-CAPTURE-TIMEOUT"

sec "9/13 first launch, close, restart, explicit root, missing root, relocation"
expected_root="$(norm_path "$TF_ROOT")"
eq "first-launch exit code" "$(case_rc first-launch)" "0"
eq "first-launch root valid" "$(case_valid first-launch)" "1"
eq "first-launch connected root" "$(norm_path "$(case_connected first-launch)")" "$expected_root"
has "first-launch title is a connected title" "$(cat "$(case_dir first-launch)/title-last.txt")" "[VPK entries:"
eq "first-launch refused VPK count" "$(case_refused first-launch)" ""
first_settings="$(case_dir first-launch)/settings.ini"
eq "first launch wrote its settings file" "$([ -f "$first_settings" ] && echo yes || echo no)" "yes"
eq "first launch saved the connected root" "$(norm_path "$(sed -n "s/^tfRoot=//p" "$first_settings" | tail -1)")" "$expected_root"
eq "restart exit code" "$(case_rc restart)" "0"
eq "restart reused the saved root" "$(norm_path "$(case_connected restart)")" "$expected_root"
eq "restart root valid" "$(case_valid restart)" "1"
eq "restart did not need a second settings file" "$([ -f "$(case_dir restart)/settings.ini" ] && echo yes || echo no)" "no"
eq "explicit-root exit code" "$(case_rc explicit-root)" "0"
eq "explicit-root connected root" "$(norm_path "$(case_connected explicit-root)")" "$expected_root"
eq "explicit-root root valid" "$(case_valid explicit-root)" "1"
# T47: the trace's root decision is asserted on its own, not inferred from the
# title. T43's cold run already had valid=1 here while the title was still the
# load-time string, so the two signals are kept separate for every case.
eq "first-launch trace root" "$(norm_path "$(case_trace_root first-launch)")" "$expected_root"
eq "restart trace root" "$(norm_path "$(case_trace_root restart)")" "$expected_root"
eq "explicit-root trace root" "$(norm_path "$(case_trace_root explicit-root)")" "$expected_root"
eq "first-launch readiness class" "$(readiness_class "$(case_dir first-launch)")" "OK"
eq "restart readiness class" "$(readiness_class "$(case_dir restart)")" "OK"
eq "explicit-root readiness class" "$(readiness_class "$(case_dir explicit-root)")" "OK"
eq "missing-root readiness class (trace and title agree: not connected)" "$(readiness_class "$(case_dir missing-root)")" "OK"
eq "empty-root readiness class (trace and title agree: not connected)" "$(readiness_class "$(case_dir empty-root)")" "OK"
eq "relocated readiness class" "$(readiness_class "$(case_dir relocated)")" "OK"
eq "missing-root exit code (reports, does not crash)" "$(case_rc missing-root)" "0"
eq "missing-root root invalid" "$(case_valid missing-root)" "0"
eq "missing-root connected root" "$(case_connected missing-root)" ""
hasnt "missing-root title claims no connected install" "$(cat "$(case_dir missing-root)/title-last.txt")" "[VPK entries:"
missing_requested="$(case_requested missing-root)"
eq "missing-root names the requested path" "$(norm_path "$missing_requested")" "$(norm_path "$(cygpath -w "$OUT/fixtures/roots/__no_such_root__")")"
has "missing-root title names the failed path" "$(norm_path "$(cat "$(case_dir missing-root)/title-last.txt")")" "$(norm_path "$(cygpath -w "$OUT/fixtures/roots/__no_such_root__")")"
eq "empty-root exit code (reports, does not crash)" "$(case_rc empty-root)" "0"
eq "empty-root root invalid" "$(case_valid empty-root)" "0"
eq "empty-root connected root" "$(case_connected empty-root)" ""
hasnt "empty-root title claims no connected install" "$(cat "$(case_dir empty-root)/title-last.txt")" "[VPK entries:"
eq "empty-root names the requested path" "$(norm_path "$(case_requested empty-root)")" "$(norm_path "$(cygpath -w "$OUT/fixtures/roots/empty")")"
eq "relocated exit code" "$(case_rc relocated)" "0"
eq "relocated connected root" "$(norm_path "$(case_connected relocated)")" "$expected_root"
eq "relocated root valid" "$(case_valid relocated)" "1"
hash_list "$INSTALL" > "$OUT/package-hashes-after-runs.txt"
eq "the product wrote nothing into its install directory" \
  "$(diff -q "$OUT/package-hashes.txt" "$OUT/package-hashes-after-runs.txt" >/dev/null 2>&1 && echo unchanged || echo changed)" "unchanged"
eq "the relocated copy is the same package" \
  "$(diff -q <(hash_list "$INSTALL") <(hash_list "$(case_dir relocated)/install") >/dev/null 2>&1 && echo same || echo different)" "same"

sec "9b/13 second cold-start cycle: its own staged package and settings directories"
# T47: the T43 failure was one run that never reached the connected title, not a
# wrong root decision. Determinism therefore needs a second complete cycle from
# a separate staging directory and fresh settings directories, and the decision
# tuple (exit | trace valid | trace tf_root | connected root | VPK bracket) must
# be identical between the cycles.
CYCLE2_INSTALL="$OUT/install-cycle2"
rm -rf "$CYCLE2_INSTALL" "$OUT/cases-cycle2"
stage_install "$CYCLE2_INSTALL" install-cycle2
eq "cycle-2 stage rc" "$?" "0"
eq "cycle-2 staged product is the built product" \
  "$(sha256sum "$CYCLE2_INSTALL/tf2_demo_native.exe" | cut -d" " -f1)" \
  "$(sha256sum "$EXE" | cut -d" " -f1)"
CYCLE2_RC=0
run_capture "$CYCLE2_INSTALL" "$OUT/cases-cycle2" cycle "" "$TITLE_BOUND_SECONDS" "$CAPTURE_CYCLE_BUDGET_SECONDS" || CYCLE2_RC=$?
eq "cycle-2 capture rc" "$CYCLE2_RC" "0"
eq "cycle-2 no title capture timeouts" "$(capture_timeouts "$OUT/cases-cycle2")" "0"
eq "cycle-2 every case classified OK" "$(capture_failed "$OUT/cases-cycle2")" "0"
expected_connected_decision="0|1|$expected_root|$expected_root|yes"
eq "cycle-2 first-launch decision" "$(dir_decision "$OUT/cases-cycle2/first-launch")" "$expected_connected_decision"
eq "cycle-2 restart decision" "$(dir_decision "$OUT/cases-cycle2/restart")" "$expected_connected_decision"
eq "cycle-2 explicit-root decision" "$(dir_decision "$OUT/cases-cycle2/explicit-root")" "$expected_connected_decision"
eq "two cold-start cycles agree: first-launch" "$(dir_decision "$OUT/cases-cycle2/first-launch")" "$(dir_decision "$OUT/cases/first-launch")"
eq "two cold-start cycles agree: restart" "$(dir_decision "$OUT/cases-cycle2/restart")" "$(dir_decision "$OUT/cases/restart")"
eq "two cold-start cycles agree: explicit-root" "$(dir_decision "$OUT/cases-cycle2/explicit-root")" "$(dir_decision "$OUT/cases/explicit-root")"
eq "cycle-2 first launch saved the connected root" \
  "$(norm_path "$(sed -n "s/^tfRoot=//p" "$OUT/cases-cycle2/first-launch/settings.ini" | tail -1)")" "$expected_root"
eq "cycle-2 restart did not need a second settings file" \
  "$([ -f "$OUT/cases-cycle2/restart/settings.ini" ] && echo yes || echo no)" "no"
eq "cycle-2 capture launched its own staged binary" \
  "$(norm_path "$(case_exe_path "$OUT/cases-cycle2/first-launch")")" \
  "$(norm_path "$(cygpath -w "$CYCLE2_INSTALL/tf2_demo_native.exe")")"
eq "cycle-2 no readiness run invoked an installer or uninstaller mode" "$(no_lifecycle_mode_runs "$OUT/cases-cycle2")" "0"

sec "10/13 the store matrix: loose > VPK, download/maps, missing, broken archives"
prec_tris="$(tris_of "$(case_bsp store-precedence)")"
eq "precedence: loose answered, not the VPK" "$(case_store store-precedence)" "maps"
eq "precedence: selected file" "$(case_selected store-precedence)" "maps/t32_marker.bsp"
if [ -n "$prec_tris" ] && [ "$prec_tris" -gt 0 ] 2>/dev/null; then ok "precedence: the loose BSP was parsed" "$prec_tris tris"; else wrong "precedence: the loose BSP was parsed" "$(case_bsp store-precedence)" "a positive triangle count"; fi
eq "precedence: the VPK opened" "$(archives_opened store-precedence)" "1"
eq "precedence: no VPK refused" "$(archives_refused store-precedence)" "0"
vpk_tris="$(tris_of "$(case_bsp store-vpk-only)")"
eq "vpk-only: the VPK answered" "$(case_store store-vpk-only)" "vpk"
eq "vpk-only: selected entry" "$(case_selected store-vpk-only)" "maps/t32_marker.bsp"
if [ -n "$vpk_tris" ] && [ "$vpk_tris" -gt 0 ] 2>/dev/null; then ok "vpk-only: the packed BSP was parsed" "$vpk_tris tris"; else wrong "vpk-only: the packed BSP was parsed" "$(case_bsp store-vpk-only)" "a positive triangle count"; fi
eq "vpk-only: no VPK refused" "$(archives_refused store-vpk-only)" "0"
dl_tris="$(tris_of "$(case_bsp store-download-maps)")"
eq "download-maps: the downloaded copy answered" "$(case_store store-download-maps)" "download-maps"
eq "download-maps: selected file" "$(case_selected store-download-maps)" "download/maps/t32_marker.bsp"
if [ -n "$dl_tris" ] && [ "$dl_tris" -gt 0 ] 2>/dev/null; then ok "download-maps: the BSP was parsed" "$dl_tris tris"; else wrong "download-maps: the BSP was parsed" "$(case_bsp store-download-maps)" "a positive triangle count"; fi
eq "missing-map: no store answered" "$(case_store store-missing-map)" "none"
eq "missing-map: no bytes read" "$(case_bytes store-missing-map)" "0"
case "$(case_bsp store-missing-map)" in
  invalid*) ok "missing-map: the title reports an invalid BSP" "$(case_bsp store-missing-map)";;
  *) wrong "missing-map: the title reports an invalid BSP" "$(case_bsp store-missing-map)" "invalid/...";;
esac
corrupt_reason="$(refused_reason store-vpk-corrupt)"
eq "corrupt-vpk: no map store answered" "$(case_store store-vpk-corrupt)" "none"
eq "corrupt-vpk: the present archive was counted as refused" "$(archives_refused store-vpk-corrupt)" "1"
eq "corrupt-vpk: the archive refused to open" "$(archives_opened store-vpk-corrupt)" "0"
eq "corrupt-vpk: named refusal reason" "$corrupt_reason" "VPK entry range outside directory archive"
eq "corrupt-vpk: the title carries the refusal count" "$(case_refused store-vpk-corrupt)" "1"
empty_reason="$(refused_reason store-vpk-empty)"
eq "empty-vpk: counted as refused" "$(archives_refused store-vpk-empty)" "1"
eq "empty-vpk: named refusal reason" "$empty_reason" "empty VPK file"
trunc_reason="$(refused_reason store-vpk-truncated)"
eq "truncated-vpk: counted as refused" "$(archives_refused store-vpk-truncated)" "1"
eq "truncated-vpk: named refusal reason" "$trunc_reason" "truncated VPK header"
distinct_reasons="$(printf "%s\n%s\n%s\n" "$corrupt_reason" "$empty_reason" "$trunc_reason" | sort -u | wc -l | tr -d " ")"
eq "the three broken archives get three different named reasons" "$distinct_reasons" "3"
first_opened="$(archives_opened first-launch)"
if [ -n "$first_opened" ] && [ "$first_opened" -ge 1 ] 2>/dev/null; then ok "the real install opened its archives" "opened=$first_opened"; else wrong "the real install opened its archives" "${first_opened:-none}" "at least 1"; fi
eq "the real install refused no VPK" "$(archives_refused first-launch)" "0"

sec "11/13 reinstall over the existing directory (no uninstaller exists)"
cmake --install "$(cygpath -w "$BUILD")" --config Release --prefix "$(cygpath -w "$INSTALL")" > "$OUT/logs/install-reinstall.log" 2>&1
REINSTALL_RC=$?
eq "reinstall rc" "$REINSTALL_RC" "0"
write_inventory "$INSTALL" "$OUT/package-files-reinstall.txt"
eq "reinstall leaves the package identical" "$(diff -q "$OUT/package-files.txt" "$OUT/package-files-reinstall.txt" >/dev/null 2>&1 && echo same || echo different)" "same"

sec "12/13 uninstall: the package is a directory and nothing else"
rm -rf "$INSTALL"
eq "removing the directory removes the package" "$([ -e "$INSTALL" ] && echo present || echo absent)" "absent"
eq "no product process left behind" "$(product_processes)" "0"
eq "the removed executable is gone" "$([ -e "$INSTALL/tf2_demo_native.exe" ] && echo present || echo absent)" "absent"

sec "13/13 installer features this package does NOT have (release blockers)"
installer_defs="$(find . -path ./.git -prune -o -path ./native/build-nmake -prune -o -path ./.scratch -prune -o -type f \( -name "*.iss" -o -name "*.wxs" -o -name "*.nsi" -o -name "*.msi" -o -name "*.msix" \) -print 2>/dev/null | head -5)"
eq "installer definition files in the tree" "${installer_defs:-none}" "none"
cpack_rules="$(grep -lE "include\(CPack\)|CPACK_" native/CMakeLists.txt 2>/dev/null | head -1 || true)"
eq "CPack/package rules in native/CMakeLists.txt" "${cpack_rules:-none}" "none"
# T37 closed this gap: uninstall is now a PRODUCT mode (--uninstall), driven from a
# manifest, not a CMake rule that deletes a directory. The assertion therefore
# measures the thing that matters -- is there a rule that removes directories
# blindly -- rather than the word "uninstall" appearing in a comment.
uninstall_rules="$(grep -lEi "remove_directory|rmdir" native/CMakeLists.txt 2>/dev/null | head -1 || true)"
eq "blind directory-removal rule in native/CMakeLists.txt" "${uninstall_rules:-none}" "none"
eq "the product and the probe link the same lifecycle source" \
  "$(grep -c 'install_lifecycle.cpp' native/CMakeLists.txt | tr -d ' ')" "3"
# T37 added the package version ("0.1.0" / 100) that --version and --install both
# read. There are still no MSI/CPack/registry version keys -- the two lines below
# are the whole metadata story -- so this assertion now pins HOW MUCH version
# metadata exists rather than claiming there is none.
version_defs="$(grep -cE "^set\(TF2_PACKAGE_VERSION" native/CMakeLists.txt 2>/dev/null | tr -d ' ')"
eq "package version metadata lines in native/CMakeLists.txt" "${version_defs:-0}" "2"
has "the version the package answers is the one it ships" \
  "$(sed -n "s/^version=//p" VERSION.txt | tr -d "\r")" "0.1.0"
# T37 closed two of the five: the package already carried an installer
# (--install), an uninstaller (--uninstall) and version metadata (--version,
# VERSION.txt, the manifest header). T42 closed a third: an install now compares
# the manifest's packageVersionCode and refuses to go backwards by name
# (install=FAIL reason=downgrade; a newer one reports mode=upgrade). That is a
# version decision, not a migration engine, and the two gaps that remain are named
# with their owner and are NOT silently dropped -- the count is pinned so closing
# one of them has to update this line rather than quietly changing the number.
echo "  CLOSED  upgrade_and_downgrade_decision=1 owner=T42 (packageVersionCode comparison; see evidence/t37-install/t42-upgrade-downgrade.log)"
echo "  BLOCKER no_shortcuts_or_file_association=1 owner=T28 release packaging"
echo "  BLOCKER tf2_assets_not_packaged_by_design=1 owner=product (locate or prompt for a TF2 install)"
INSTALLER_GAPS=2
eq "installer features absent (pinned: adding one must update this gate)" "$INSTALLER_GAPS" "2"
eq "installer lifecycle modes the package now provides" \
  "$(grep -oE 'L"--(version|install|uninstall|install-status)"' native/src/main.cpp | sort -u | wc -l | tr -d ' ')" "4"

cases=13
echo
echo "  cases=$cases checks=$CHECKS mutations=0"
if [ "$FAIL" -eq 0 ]; then echo "  INSTALL-READINESS=PASS"; exit 0; fi
echo "  INSTALL-READINESS=FAIL fail=$FAIL"
exit 1
fi

# ==================================================================== mutation
# Three perturbations, each aimed at one of the T32 step-5 mutations. Each one
# changes a real source file, rebuilds the product target, re-stages the install
# from that build, and requires the matching reading to leave its pinned value.
# A mutation whose build fails is a false pass (T9 recorded exactly that), so
# every rebuild rc is asserted before its reading is looked at.

sec "mutation preconditions: the forward run artifacts"
for need in "$OUT/cases/first-launch" "$OUT/fixtures/roots/precedence" "$OUT/fixtures/roots/vpk-corrupt"; do
  eq "forward artifact ${need##*/}" "$([ -e "$need" ] && echo present || echo absent)" "present"
done
eq "no product process before the mutation suite" "$(product_processes)" "0"
MUT="$OUT/mutation"
mkdir -p "$MUT/backup" "$MUT/logs"
SRC_MAP="$REPO/native/src/map_lookup.cpp"
SRC_VPK="$REPO/native/src/vpk_archive.cpp"
SRC_CMAKE="$REPO/native/CMakeLists.txt"
cp "$SRC_MAP" "$MUT/backup/map_lookup.cpp"
cp "$SRC_VPK" "$MUT/backup/vpk_archive.cpp"
cp "$SRC_CMAKE" "$MUT/backup/CMakeLists.txt"

rebuild_product() { # <log-name>
  bash build-target.sh tf2_demo_native map_lookup_probe > "$MUT/logs/$1.log" 2>&1
  local rc=$?
  echo "  rebuild $1 rc=$rc"
  return $rc
}
mut_store() { sed -n "s/.*map: lookup .* store=\([^ ]*\).*/\1/p" "$1/trace.txt" 2>/dev/null | tail -1; }
mut_refused() { sed -n "s/.*vpk: archives opened=\([0-9]*\) refused=\([0-9]*\).*/\2/p" "$1/trace.txt" 2>/dev/null | tail -1; }
mut_reason() { sed -n "s/.*vpk: refused file=[^ ]* reason=\(.*\)/\1/p" "$1/trace.txt" 2>/dev/null | tail -1; }
mut_tris() { tris_of "$(bsp_segment "$1/title-last.txt")"; }
# T47: a mutation reading is only evidence about the MUTATED build if the staged
# executable it ran is the one the rebuild produced. Both halves are asserted:
# the staged hash equals the current build hash, and the recorded command line
# names that staged copy (not a stale tf2_demo_native.exe from an earlier
# staging directory).
assert_mutation_binary() { # <label> <install-dir> [<case-dir>]
  local label="$1" install="$2" caseDir="${3:-}"
  eq "$label staged binary is the build under test" \
    "$(sha256sum "$install/tf2_demo_native.exe" | cut -d" " -f1)" \
    "$(sha256sum "$EXE" | cut -d" " -f1)"
  if [ -n "$caseDir" ]; then
    eq "$label capture launched its own staged binary" \
      "$(norm_path "$(case_exe_path "$caseDir")")" \
      "$(norm_path "$(cygpath -w "$install/tf2_demo_native.exe")")"
  fi
}

# ---------------------------------------------------------------- M1 precedence
sec "M1 precedence reversal: the VPK candidate moved in front of the loose map"
python - "$SRC_MAP" <<'PY'
import sys
path = sys.argv[1]
data = open(path, "rb").read()
nl = b"\r\n" if b"\r\n" in data else b"\n"
loose = b"  plan.candidates.push_back({\"maps/\" + file, \"maps\", false});"
vpk = b"  plan.candidates.push_back({\"maps/\" + file, \"vpk\", true});"
download = b"  plan.candidates.push_back({\"download/maps/\" + file, \"download-maps\", false});"
old = nl.join([loose, download, vpk]) + nl
new = nl.join([vpk, loose, download]) + nl
if old not in data:
    sys.exit("M1 anchor absent")
open(path, "wb").write(data.replace(old, new, 1))
print("  mutated: order is vpk, maps, download-maps")
PY
M1_APPLIED=$?
eq "M1 applied" "$M1_APPLIED" "0"
rebuild_product m1-build
eq "M1 build succeeded" "$?" "0"
stage_install "$MUT/install-m1" m1-install
eq "M1 stage rc" "$?" "0"
run_capture "$MUT/install-m1" "$MUT/cases-m1" stores store-precedence
M1_CAPTURE=$?
eq "M1 capture rc" "$M1_CAPTURE" "0"
assert_mutation_binary "M1" "$MUT/install-m1" "$MUT/cases-m1/store-precedence"
M1_STORE="$(mut_store "$MUT/cases-m1/store-precedence")"
M1_TRIS="$(mut_tris "$MUT/cases-m1/store-precedence")"
cp "$MUT/cases-m1/store-precedence/trace.txt" "$MUT/m1-trace.txt" 2>/dev/null || true
cp "$MUT/cases-m1/store-precedence/title-last.txt" "$MUT/m1-title.txt" 2>/dev/null || true
echo "  under M1: store=$M1_STORE bsp=$(bsp_segment "$MUT/cases-m1/store-precedence/title-last.txt")"
MUTATIONS=$((MUTATIONS + 1))
if [ "$M1_STORE" = "vpk" ] && [ -z "$M1_TRIS" ]; then
  ok "M1 caught: the reversed order read the VPK and lost the BSP" "store=vpk bsp=invalid"
else
  wrong "M1 caught: the reversed order read the VPK and lost the BSP" "store=$M1_STORE tris=${M1_TRIS:-none}" "store=vpk with an unparsed BSP"
fi
cp "$MUT/backup/map_lookup.cpp" "$SRC_MAP"
if cmp -s "$SRC_MAP" "$MUT/backup/map_lookup.cpp"; then ok "M1 source restored byte-identical" "yes"; else wrong "M1 source restored byte-identical" "no" "yes"; fi
rebuild_product m1-restore
eq "M1 restore build succeeded" "$?" "0"
stage_install "$MUT/install-m1-restored" m1-restore-install
eq "M1 restore stage rc" "$?" "0"
run_capture "$MUT/install-m1-restored" "$MUT/cases-m1-restored" stores store-precedence
eq "M1 restore capture rc" "$?" "0"
assert_mutation_binary "M1-restored" "$MUT/install-m1-restored" "$MUT/cases-m1-restored/store-precedence"
M1_R_STORE="$(mut_store "$MUT/cases-m1-restored/store-precedence")"
M1_R_TRIS="$(mut_tris "$MUT/cases-m1-restored/store-precedence")"
eq "M1 restored reference store" "$M1_R_STORE" "maps"
if [ -n "$M1_R_TRIS" ] && [ "$M1_R_TRIS" -gt 0 ] 2>/dev/null; then ok "M1 restored reference BSP" "$M1_R_TRIS tris"; else wrong "M1 restored reference BSP" "${M1_R_TRIS:-none}" "a positive triangle count"; fi

# ------------------------------------------------------------- M2 corrupt entry
sec "M2 corrupt-entry acceptance: the 64-bit inline offset narrowed to 32 bits"
python - "$SRC_VPK" <<'PY'
import sys
path = sys.argv[1]
data = open(path, "rb").read()
old = b"const std::uint64_t absolute = static_cast<std::uint64_t>(treeEnd) + offset;"
new = b"const std::uint64_t absolute = static_cast<std::uint32_t>(treeEnd) + offset;"
if old not in data:
    sys.exit("M2 anchor absent")
open(path, "wb").write(data.replace(old, new, 1))
print("  mutated: the tree base is narrowed to 32 bits before the offset is added")
PY
eq "M2 applied" "$?" "0"
rebuild_product m2-build
eq "M2 build succeeded" "$?" "0"
stage_install "$MUT/install-m2" m2-install
eq "M2 stage rc" "$?" "0"
run_capture "$MUT/install-m2" "$MUT/cases-m2" stores store-vpk-corrupt
eq "M2 capture rc" "$?" "0"
assert_mutation_binary "M2" "$MUT/install-m2" "$MUT/cases-m2/store-vpk-corrupt"
M2_STORE="$(mut_store "$MUT/cases-m2/store-vpk-corrupt")"
M2_REFUSED="$(mut_refused "$MUT/cases-m2/store-vpk-corrupt")"
cp "$MUT/cases-m2/store-vpk-corrupt/trace.txt" "$MUT/m2-trace.txt" 2>/dev/null || true
echo "  under M2: store=$M2_STORE refused=${M2_REFUSED:-<no summary>}"
MUTATIONS=$((MUTATIONS + 1))
if [ "$M2_STORE" = "vpk" ] && [ "${M2_REFUSED:-}" != "1" ]; then
  ok "M2 caught: the out-of-range entry was accepted and read as the map" "store=vpk refused=${M2_REFUSED:-none}"
else
  wrong "M2 caught: the out-of-range entry was accepted and read as the map" "store=$M2_STORE refused=${M2_REFUSED:-none}" "store=vpk with no refusal counted"
fi
cp "$MUT/backup/vpk_archive.cpp" "$SRC_VPK"
if cmp -s "$SRC_VPK" "$MUT/backup/vpk_archive.cpp"; then ok "M2 source restored byte-identical" "yes"; else wrong "M2 source restored byte-identical" "no" "yes"; fi
rebuild_product m2-restore
eq "M2 restore build succeeded" "$?" "0"
stage_install "$MUT/install-m2-restored" m2-restore-install
eq "M2 restore stage rc" "$?" "0"
run_capture "$MUT/install-m2-restored" "$MUT/cases-m2-restored" stores store-vpk-corrupt
eq "M2 restore capture rc" "$?" "0"
assert_mutation_binary "M2-restored" "$MUT/install-m2-restored" "$MUT/cases-m2-restored/store-vpk-corrupt"
M2_R_REFUSED="$(mut_refused "$MUT/cases-m2-restored/store-vpk-corrupt")"
M2_R_REASON="$(mut_reason "$MUT/cases-m2-restored/store-vpk-corrupt")"
eq "M2 restored reference refused count" "$M2_R_REFUSED" "1"
eq "M2 restored reference named reason" "$M2_R_REASON" "VPK entry range outside directory archive"

# ------------------------------------------------------------ M3 install files
sec "M3 missing install files: the smoke probe dropped from both install rules"
#   T37 moved the smoke probe from the package root into .uninstall/payload (the
#   file list --install lays down), so dropping it from install(TARGETS) alone no
#   longer changes the shipped package: measured 2026-10-21, the staged package
#   still carried the probe in the payload and the mutation was dead. It now
#   removes the probe from the payload rule as well -- the only way a release
#   stops shipping it -- and the package assertions are measured against the
#   forward run's reference listing instead of a pre-T37 hardcoded list.
M3_REF_HITS="$(grep -c native_install_smoke_probe "$BUILD/cmake_install.cmake" 2>/dev/null | tr -d " ")"
eq "M3 reference install script names the smoke probe twice (root and payload)" "${M3_REF_HITS:-0}" "2"
M3_REF_COUNT="$(wc -l < "$OUT/package-files.txt" | tr -d " ")"
python - "$SRC_CMAKE" <<'PY'
import sys
path = sys.argv[1]
data = open(path, "rb").read()
lines = data.split(b"\n")
# The root rule: drop the probe target from the TARGETS list.
root_index = None
for index, line in enumerate(lines):
    if line.startswith(b"install(TARGETS tf2_demo_native native_install_smoke_probe"):
        root_index = index
        break
if root_index is None:
    sys.exit("M3 root anchor absent")
lines[root_index] = lines[root_index].replace(b" native_install_smoke_probe", b"", 1)
# The payload rule: drop the probe's file line from the FILES command. Matched
# by stripped content so the mutation does not depend on the indentation.
payload_index = None
for index, line in enumerate(lines):
    if line.strip() == b'"$<TARGET_FILE:native_install_smoke_probe>"':
        payload_index = index
        break
if payload_index is None:
    sys.exit("M3 payload anchor absent")
del lines[payload_index]
open(path, "wb").write(b"\n".join(lines))
print("  mutated: neither install rule names native_install_smoke_probe")
PY
eq "M3 applied" "$?" "0"
bash build-target.sh tf2_demo_native > "$MUT/logs/m3-build.log" 2>&1
eq "M3 build succeeded" "$?" "0"
M3_INSTALL_SCRIPT_HITS="$(grep -c native_install_smoke_probe "$BUILD/cmake_install.cmake" 2>/dev/null | tr -d " ")"
eq "M3 reached the generated install script" "${M3_INSTALL_SCRIPT_HITS:-0}" "0"
stage_install "$MUT/install-m3" m3-install
eq "M3 stage rc" "$?" "0"
assert_mutation_binary "M3" "$MUT/install-m3"
write_inventory "$MUT/install-m3" "$MUT/m3-package-files.txt"
M3_LIST="$(cut -f1 "$MUT/m3-package-files.txt")"
echo "  under M3: package files: $(printf "%s " "$M3_LIST")"
MUTATIONS=$((MUTATIONS + 1))
M3_LOST="$(comm -23 <(cut -f1 "$OUT/package-files.txt" | sort) <(cut -f1 "$MUT/m3-package-files.txt" | sort) | tr '\n' ' ')"
M3_ADDED="$(comm -13 <(cut -f1 "$OUT/package-files.txt" | sort) <(cut -f1 "$MUT/m3-package-files.txt" | sort) | tr '\n' ' ')"
if [ "$M3_LOST" = ".uninstall/payload/native_install_smoke_probe.exe native_install_smoke_probe.exe " ] && [ -z "$M3_ADDED" ]; then
  ok "M3 caught: the package lost both probe copies and nothing else" "$M3_LOST"
else
  wrong "M3 caught: the package lost both probe copies and nothing else" \
    "lost=$M3_LOST added=${M3_ADDED:-none}" "lost the two probe copies, added nothing"
fi
eq "M3 package holds the reference file list minus the two probe copies" \
  "$(wc -l < "$MUT/m3-package-files.txt" | tr -d " ")" "$(( M3_REF_COUNT - 2 ))"
cp "$MUT/backup/CMakeLists.txt" "$SRC_CMAKE"
if cmp -s "$SRC_CMAKE" "$MUT/backup/CMakeLists.txt"; then ok "M3 source restored byte-identical" "yes"; else wrong "M3 source restored byte-identical" "no" "yes"; fi
bash build-target.sh tf2_demo_native native_install_smoke_probe > "$MUT/logs/m3-restore-build.log" 2>&1
eq "M3 restore build succeeded" "$?" "0"
M3_R_HITS="$(grep -c native_install_smoke_probe "$BUILD/cmake_install.cmake" 2>/dev/null | tr -d " ")"
eq "M3 restore regenerated the reference install script" "${M3_R_HITS:-0}" "${M3_REF_HITS:-0}"
stage_install "$MUT/install-m3-restored" m3-restore-install
eq "M3 restore stage rc" "$?" "0"
assert_mutation_binary "M3-restored" "$MUT/install-m3-restored"
write_inventory "$MUT/install-m3-restored" "$MUT/m3-restored-package-files.txt"
eq "M3 restored reference package list" \
  "$(cut -f1 "$MUT/m3-restored-package-files.txt" | sort)" \
  "$(cut -f1 "$OUT/package-files.txt" | sort)"

# ------------------------------------------------------------------- verdict
sec "mutation verdict"
for pair in "map_lookup.cpp|$SRC_MAP" "vpk_archive.cpp|$SRC_VPK" "CMakeLists.txt|$SRC_CMAKE"; do
  name="${pair%%|*}"; path="${pair#*|}"; backup="$MUT/backup/$name"
  if cmp -s "$path" "$backup"; then ok "restored byte-identical: $name" "yes"; else wrong "restored byte-identical: $name" "no" "yes"; fi
done
echo
eq "mutation captures invoked no installer or uninstaller mode" "$(no_lifecycle_mode_runs "$MUT")" "0"
eq "no product process after the mutation suite" "$(product_processes)" "0"
echo "  cases=4 checks=$CHECKS mutations=$MUTATIONS"
if [ "$FAIL" -eq 0 ] && [ "$MUTATIONS" -eq 3 ]; then
  echo "  INSTALL-READINESS-MUTATION=PASS (3 mutations caught, sources restored)"
  exit 0
fi
echo "  INSTALL-READINESS-MUTATION=FAIL fail=$FAIL mutations=$MUTATIONS"
exit 1