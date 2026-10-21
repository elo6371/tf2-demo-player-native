#!/usr/bin/env bash
# install-lifecycle-check.sh -- T37 (2026-10-20): the installer lifecycle the
# package was missing, driven end to end on a package that was staged by
# 'cmake --install' and then copied into a CLEAN temporary directory.
#
# WHAT THIS GATE IS
#   T32 measured a staging directory and recorded, by name, that the tree had no
#   installer and no uninstaller (its gate section 13 pinned five such gaps).
#   T37 makes the product its own installer, and this gate is the measurement of
#   that claim:
#     --version           the package names its own version, on a redirected
#                         stdout, with a non-empty file (see the M1 note below)
#     --install           payload -> directory, with a manifest of exactly what
#                         was laid down and a backup of what it overwrote
#     --install-status    the directory still matches its manifest
#     --uninstall         removes what the manifest says it installed, restores
#                         what it backed up, and leaves everything else
#
# THE FOUR QUESTIONS T32 COULD NOT ASK
#   what was installed       the manifest file list, with sizes and SHA-256
#   which version            VERSION.txt + the manifest packageVersionCode
#   what may be removed      state=new and state=changed only; a user file is
#                            state=preserved and survives, an edited file is
#                            counted 'modified' and also survives
#   what is left behind      the directory inventory after uninstall
#
# THE STATE SURVIVES THE PROCESS
#   Every step below is a separate process, so 'the install remembers itself'
#   is a claim about bytes on disk, not about an object that stayed alive.
#
# T42 (2026-10-21): THE SIX LIFECYCLE QUESTIONS T37 LEFT OPEN, AND THE VPK MATRIX
# ITS FIXTURE SCRIPT WAS WRITTEN FOR BUT WAS NEVER DRIVEN FROM
#   upgrade/downgrade  the manifest's packageVersionCode is compared now: a newer
#                      package upgrades (mode=upgrade), an older one is refused by
#                      name (reason=downgrade) instead of silently replacing the
#                      install. Reinstall (same code) keeps its existing reading.
#   migration          the installed directory moved: status and uninstall read it
#                      from the new location; nothing on disk pins the old path.
#   first launch       the installed directory boots (hidden window, audio
#                      disabled), connects to a fixture root and saves it.
#   restart            a second process reuses the root the first one saved
#                      without being told the path again.
#   non-admin dir      an install into a directory the account may not write into
#                      refuses by name and writes nothing into it.
#   missing file       an installed runtime file and VERSION.txt removed: status
#                      names them missing, --version falls back to the compiled
#                      version, uninstall counts them alreadyMissing.
#   VPK matrix         absent / unreadable / unsupported / truncated / valid read
#                      through BOTH vpk_query and the installed product, with
#                      absent kept apart from corrupt. The two limitation roots
#                      (missing volumes, bad loose file shadowing a good archived
#                      one) are measured and named rather than skipped.
#
# THE FIXTURE ROOTS ARE BUILT FROM THE PINNED DEMO AND MAP
#   install-lifecycle-fixtures.py builds them (the readiness gate's methodology:
#   never from this machine's Steam install's contents). The deny-read VPK and
#   the deny-write install directory are the only non-file fixtures; their ACLs
#   are removed again before the gate exits.
#
# MUTATION MODE
#   A mutation must run its own binary. Three mutations rebuild the product; each
#   fixture asserts that the executable it is about to run is the one the mutated
#   build produced, so a stale copy cannot be read as "the mutation was not
#   caught". A fixture that would run the reference binary instead is
#   MUTATION-NOT-LINKED and exits 90 -- the same meaning the rebuild guard uses.
#
# USAGE
#   bash install-lifecycle-check.sh --forward  <out-dir>
#   bash install-lifecycle-check.sh --mutation <out-dir>   (forward must have run)
# Exit 0 = every assertion held / every mutation was caught; 1 = a red reading;
# 2 = usage; 90 = MUTATION-NOT-LINKED (a fixture would exercise the reference
# binary, so the mutation proves nothing about itself).

set -uo pipefail

MODE="${1:---forward}"
OUT_ARG="${2:-}"
if [ "$MODE" != "--forward" ] && [ "$MODE" != "--mutation" ]; then
  echo "usage: $0 --forward|--mutation <out-dir>"; exit 2
fi
if [ -z "$OUT_ARG" ]; then echo "usage: $0 $MODE <out-dir>"; exit 2; fi

REPO="$(cd "$(dirname "$0")" && pwd)"
cd "$REPO" || exit 2
mkdir -p "$OUT_ARG"
OUT="$(cd "$OUT_ARG" && pwd)"
mkdir -p "$OUT/logs"

BUILD="$REPO/native/build-nmake"
EXE="$BUILD/tf2_demo_native.exe"
PROBE="$BUILD/install_cli_probe.exe"
VPK_QUERY="$BUILD/vpk_query.exe"
# T42: the pinned inputs the fixture roots are built from, and the harness that
# launches the INSTALLED product. title-capture.ps1 owns the record -> close ->
# exit sequence and adds --hidden-window --audio-disabled; this gate never starts
# a product any other way.
TF_ROOT="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
REAL_DEMO="$TF_ROOT/demos/autorecord_2026-07-02_13-26-46.dem"
BSP_SOURCE="$TF_ROOT/maps/itemtest.bsp"
TITLE_CAPTURE="$REPO/title-capture.ps1"
FIXTURES_PY="$REPO/install-lifecycle-fixtures.py"

CHECKS=0; FAIL=0; MUTATIONS=0
sec() { echo; echo "=== $1 ==="; }
ok()   { echo "  OK   $1 = $2"; CHECKS=$((CHECKS + 1)); }
wrong(){ echo "  FAIL $1 = $2, expected $3"; CHECKS=$((CHECKS + 1)); FAIL=$((FAIL + 1)); }
eq()   { if [ "$2" = "$3" ]; then ok "$1" "$2"; else wrong "$1" "$2" "$3"; fi; }
ge()   { if [ "${2:-0}" -ge "${3:-1}" ] 2>/dev/null; then ok "$1" "$2"; else wrong "$1" "${2:-none}" ">= $3"; fi; }
has()  { case "$2" in *"$3"*) ok "$1" "yes";; *) wrong "$1" "no" "contains $3";; esac; }
hasnt(){ case "$2" in *"$3"*) wrong "$1" "yes" "absent";; *) ok "$1" "absent";; esac; }

field() { sed -n "s/.*[[:space:]]$2=\([^[:space:]]*\).*/\1/p" "$1" 2>/dev/null | tail -1; }
head_field() { sed -n "s/^$2=\([^[:space:]]*\).*/\1/p" "$1" 2>/dev/null | tail -1; }
inventory() { ( cd "$1" && find . -type f -printf "%P\t%s\n" | LC_ALL=C sort ) > "$2"; }
hashlist()  { ( cd "$1" && find . -type f -print0 | LC_ALL=C sort -z | xargs -0 sha256sum ) > "$2"; }

# ---- T42 readings from a product run ---------------------------------------
# title-capture writes one file per reading; the trace is where the VPK/store
# account lives, the title is where the product says what it connected to.
trace_field() { sed -n "s/.*[[:space:]]$2=\([^[:space:]]*\).*/\1/p" "$1" 2>/dev/null | tail -1; }
case_rc() { sed "1s/^\xEF\xBB\xBF//" "$1/exit-code.txt" 2>/dev/null | tr -d "\r\n"; }
title_refused() { sed -n "s/.*\[VPK entries: [0-9]*, VPK refused: \([0-9]*\).*/\1/p" "$1" 2>/dev/null | tail -1; }
refused_reason() { sed -n "s/.*vpk: refused file=[^ ]* reason=\(.*\)/\1/p" "$1" 2>/dev/null | tail -1; }
archives_opened() { sed -n "s/.*vpk: archives opened=\([0-9]*\) refused=\([0-9]*\).*/\1/p" "$1" 2>/dev/null | tail -1; }
archives_refused() { sed -n "s/.*vpk: archives opened=\([0-9]*\) refused=\([0-9]*\).*/\2/p" "$1" 2>/dev/null | tail -1; }
trace_store() { sed -n "s/.*map: lookup .* store=\([^ ]*\).*/\1/p" "$1" 2>/dev/null | tail -1; }
trace_bytes() { sed -n "s/.*map: lookup .* bytes=\([0-9]*\).*/\1/p" "$1" 2>/dev/null | tail -1; }
bsp_segment() { sed -n "s/.*BSP: \([^,]*\).*/\1/p" "$1" 2>/dev/null | tail -1; }
tris_of() { case "$1" in *" tris") printf "%s" "${1% tris}";; *) printf "";; esac; }
connected_root() { sed -n "s/.*\([A-Za-z]:[^[]*\) \[VPK entries:.*/\1/p" "$1" 2>/dev/null | tail -1 | sed "s/[[:space:]]*$//"; }
resources_valid() { sed -n "s/.*resources: .* valid=\([0-9]*\).*/\1/p" "$1" 2>/dev/null | tail -1; }
norm_path() { local s="$1"; s="${s//\\//}"; printf "%s" "$s" | tr "A-Z" "a-z"; }

# ONE INSTALLED-PRODUCT LAUNCH, THROUGH THE HARNESS THAT ALREADY OWNS ONE.
#   A PowerShell -File call binds one scalar per parameter, so an array such as
#   title-capture's -ExtraArgs cannot be handed over from bash. This generated
#   driver is a forwarding shim: it builds the hashtable title-capture documents
#   and changes nothing about how the product is launched or closed.
write_product_driver() {
  local driver="$OUT/logs/run-installed-product.ps1"
  cat > "$driver" <<'PS'
# Generated by install-lifecycle-check.sh -- see write_product_driver there.
param(
  [Parameter(Mandatory = $true)][string]$Capture,
  [Parameter(Mandatory = $true)][string]$Exe,
  [Parameter(Mandatory = $true)][string]$Demo,
  [Parameter(Mandatory = $true)][string]$OutDir,
  [string]$TfRoot = '',
  [string]$SettingsFile = ''
)
$arguments = @{ Demo = $Demo; OutDir = $OutDir; Seconds = 30; AudioDevice = 6; ExePath = $Exe }
if (-not [string]::IsNullOrEmpty($TfRoot)) { $arguments['ExtraArgs'] = @('--tf-root', $TfRoot) }
if (-not [string]::IsNullOrEmpty($SettingsFile)) { $arguments['SettingsFile'] = $SettingsFile }
& $Capture @arguments
exit $LASTEXITCODE
PS
  printf "%s" "$driver"
}

run_product() { # <exe> <demo> <outdir> <root|-> [settings-file|-] -> exit code
  local exe="$1" demo="$2" outdir="$3" root="$4" settings="${5:--}"
  mkdir -p "$outdir"
  local args=(-NoProfile -ExecutionPolicy Bypass -File "$OUT/logs/run-installed-product.ps1"
    -Capture "$(cygpath -w "$TITLE_CAPTURE")" -Exe "$(cygpath -w "$exe")"
    -Demo "$(cygpath -w "$demo")" -OutDir "$(cygpath -w "$outdir")")
  if [ "$root" != "-" ]; then args+=(-TfRoot "$(cygpath -w "$root")"); fi
  if [ "$settings" != "-" ]; then args+=(-SettingsFile "$(cygpath -w "$settings")"); fi
  powershell "${args[@]}" > "$outdir/capture.log" 2>&1
  echo $?
}

# The product is a WIN32 subsystem executable. A gate reads it through a
# redirect, which is also the only way a script can read any product answer; M1
# in this file exists because an earlier version attached to the parent console
# and left the redirect file empty while exiting 0.
run_exe() { # <exe> <out-file> <args...>
  local exe="$1" out="$2"; shift 2
  "$exe" "$@" > "$out" 2>&1
  echo $?
}

stage_package() { # <prefix> <log-name>
  local prefix="$1" name="$2" rc=0
  rm -rf "$prefix"
  cmake --install "$(cygpath -w "$BUILD")" --config Release --prefix "$(cygpath -w "$prefix")" \
    > "$OUT/logs/$name.log" 2>&1 || rc=$?
  return $rc
}

# ===================================================================== forward
if [ "$MODE" = "--forward" ]; then

sec "1/15 the package cmake --install produces"
rc=0; stage_package "$OUT/stage" install || rc=$?
eq "cmake --install rc" "$rc" "0"
inventory "$OUT/stage" "$OUT/package-files.txt"
echo "  --- staged package ---"
sed 's/^/    /' "$OUT/package-files.txt"
eq "the package carries the product" "$([ -f "$OUT/stage/tf2_demo_native.exe" ] && echo yes || echo no)" "yes"
eq "the package carries a payload the installer will lay down" \
  "$([ -f "$OUT/stage/.uninstall/payload/tf2_demo_native.exe" ] && echo yes || echo no)" "yes"
eq "the package carries its version file" "$([ -f "$OUT/stage/VERSION.txt" ] && echo yes || echo no)" "yes"
payload_files="$(cd "$OUT/stage/.uninstall/payload" 2>/dev/null && find . -type f | wc -l | tr -d ' ')"
eq "payload file count" "${payload_files:-0}" "3"
eq "no file in the package is empty" \
  "$(awk -F'\t' '$2 == 0 {n++} END {print n+0}' "$OUT/package-files.txt")" "0"
hashlist "$OUT/stage" "$OUT/package-hashes.txt"

# T42: the tools and pinned inputs every later section needs, named here so a
# missing one fails as a precondition rather than as a confusing reading later.
for required in "$VPK_QUERY" "$TITLE_CAPTURE" "$FIXTURES_PY"; do
  eq "input present ${required##*/}" "$([ -e "$required" ] && echo yes || echo no)" "yes"
done
for required in "$REAL_DEMO" "$BSP_SOURCE"; do
  eq "pinned input present ${required##*/}" "$([ -e "$required" ] && echo yes || echo no)" "yes"
done
# THE FORWARD READINGS ARE ABOUT A NAMED BINARY.
#   The mutation run compares every fixture's binary against this reference: a
#   fixture that still holds the reference bytes is MUTATION-NOT-LINKED, not a
#   miss. Both hashes are printed every run so a log says which build it describes.
BUILD_EXE_SHA="$(sha256sum "$EXE" 2>/dev/null | cut -d' ' -f1)"
STAGE_EXE_SHA="$(sha256sum "$OUT/stage/tf2_demo_native.exe" 2>/dev/null | cut -d' ' -f1)"
echo "  reference build exe sha256=$BUILD_EXE_SHA"
echo "  staged package exe sha256=$STAGE_EXE_SHA"
eq "the staged package is the build's binary (every forward reading describes it)" \
  "$STAGE_EXE_SHA" "$BUILD_EXE_SHA"
printf '%s\n' "$STAGE_EXE_SHA" > "$OUT/reference-exe.sha256"
eq "the reference binary identity is recorded for the mutation run" \
  "$([ -s "$OUT/reference-exe.sha256" ] && echo yes || echo no)" "yes"
write_product_driver > /dev/null
eq "the installed-product launch driver was written" \
  "$([ -s "$OUT/logs/run-installed-product.ps1" ] && echo yes || echo no)" "yes"

sec "2/15 the package copied into a clean temporary directory"
PKG="$OUT/clean/pkg"
rm -rf "$OUT/clean"; mkdir -p "$OUT/clean"
cp -r "$OUT/stage" "$PKG"
eq "the copy is the package" \
  "$(diff -q <(hashlist "$OUT/stage" /dev/stdout) <(hashlist "$PKG" /dev/stdout) >/dev/null 2>&1 && echo same || echo different)" "same"

sec "2b/15 clean-room fixture roots for the first-launch and VPK cases"
# install-lifecycle-fixtures.py was written by T37 and never driven from a gate.
# It builds seven roots from the PINNED demo and map (not from this machine's
# Steam contents): valid, absent, unreadable (deny ACE), unsupported (version 3),
# truncated tree, missing volumes, and a bad loose file in front of a good
# archived one. T42 wires it to the two consumers that need it: the first-launch
# sections and the VPK matrix.
FIX="$OUT/fixtures"
python "$FIXTURES_PY" --out "$(cygpath -w "$FIX")" --demo "$(cygpath -w "$REAL_DEMO")" --bsp "$(cygpath -w "$BSP_SOURCE")" > "$OUT/logs/fixtures.log" 2>&1
FIX_RC=$?
sed "s/^/  /" "$OUT/logs/fixtures.log" | head -14
eq "fixture build rc" "$FIX_RC" "0"
eq "fixture root count" "$(ls -1 "$FIX/roots" 2>/dev/null | wc -l | tr -d " ")" "7"
eq "the fixture demo is the pinned demo with the fixture map name" \
  "$([ -s "$FIX/demo/t32_marker.dem" ] && echo yes || echo no)" "yes"
eq "the valid-state archive exists" \
  "$([ -s "$FIX/roots/vpk-valid/pak01_dir.vpk" ] && echo yes || echo no)" "yes"
cp "$FIX/inputs.sha256" "$OUT/fixture-inputs.sha256" 2>/dev/null || true
eq "the fixture inputs are hashed (a later diff can say what changed)" \
  "$([ -s "$OUT/fixture-inputs.sha256" ] && echo yes || echo no)" "yes"

sec "3/15 --version: the package names itself, through a redirect"
VRC="$(run_exe "$PKG/tf2_demo_native.exe" "$OUT/version.txt" --version)"
eq "product --version exit code" "$VRC" "0"
eq "product --version wrote a non-empty answer to the redirect" \
  "$([ -s "$OUT/version.txt" ] && echo yes || echo no)" "yes"
version_line="$(cat "$OUT/version.txt")"
echo "  --- --version ---"
sed 's/^/    /' "$OUT/version.txt"
has "version line names the package" "$version_line" "package=tf2_demo_native"
pkg_version="$(printf '%s\n' "$version_line" | sed -n 's/.*version=\([^ ]*\).*/\1/p' | head -1)"
declared_version="$(sed -n 's/^version=//p' "$REPO/VERSION.txt" | tr -d '\r' | head -1)"
echo "  version=$pkg_version declared=$declared_version"
eq "the version the product answers is the version the package ships" "$pkg_version" "$declared_version"
has "the version line carries a monotonic versionCode" "$version_line" "versionCode="
has "the version line names the source tree it came from" "$version_line" "sourceTree="
PVC="$(printf '%s\n' "$version_line" | sed -n 's/.*versionCode=\([0-9]*\).*/\1/p' | head -1)"
eq "versionCode is a number" "$([ -n "$PVC" ] && echo yes || echo no)" "yes"
eq "install_cli_probe agrees with the product" \
  "$("$PROBE" --version | head -1 | sed 's/ versionCode=.*//')" \
  "package=tf2_demo_native version=$pkg_version"

sec "4/15 --install into a directory that holds nothing but the payload (the packaged shape)"
# The directory cmake --install produced already holds the product; installing
# into it therefore exercises the "it is already here" path, not the path a user
# takes. Both are real and both are measured, so four files below is the right
# answer for the packaged prefix and three is the right answer for a fresh
# directory. The fresh one is section 4b.
IRC="$(run_exe "$PKG/tf2_demo_native.exe" "$OUT/install.txt" --install)"
eq "product --install exit code" "$IRC" "0"
echo "  --- --install ---"
sed "s/^/    /" "$OUT/install.txt"
has "install reported PASS" "$(cat "$OUT/install.txt")" "install=PASS"
eq "install mode" "$(field "$OUT/install.txt" mode)" "fresh"
eq "files the install owns (3 payload + the VERSION.txt it writes)" "$(field "$OUT/install.txt" files)" "4"
eq "the only file it created" "$(field "$OUT/install.txt" new)" "1"
eq "files it had to back up" "$(field "$OUT/install.txt" backups)" "0"
eq "no file was already there to preserve" "$(field "$OUT/install.txt" preserved)" "0"
eq "the payload was consumed, not left behind" \
  "$([ -d "$PKG/.uninstall/payload" ] && echo present || echo consumed)" "consumed"
eq "the manifest is on disk" "$([ -f "$PKG/.uninstall/install-manifest.txt" ] && echo yes || echo no)" "yes"
STATE_FILES_AFTER_INSTALL="$(find "$PKG/.uninstall" -type f ! -path '*payload*' 2>/dev/null | wc -l | tr -d ' ')"
echo "  state files after the install: $STATE_FILES_AFTER_INSTALL"
inventory "$PKG" "$OUT/installed-files.txt"
echo "  --- installed directory ---"
sed "s/^/    /" "$OUT/installed-files.txt"
eq "installed file count (3 payload + VERSION.txt + manifest)" \
  "$(wc -l < "$OUT/installed-files.txt" | tr -d " ")" "5"
eq "the installed manifest records the package version" \
  "$(sed -n "s/^packageVersion=//p" "$PKG/.uninstall/install-manifest.txt" | tr -d "\r")" "$declared_version"
eq "the installed manifest records a monotonic version code" \
  "$(sed -n "s/^packageVersionCode=//p" "$PKG/.uninstall/install-manifest.txt" | tr -d "\r")" "$PVC"
eq "the manifest lists exactly the installed files, each once" \
  "$(sed -n "s/^file=//p" "$PKG/.uninstall/install-manifest.txt" | wc -l | tr -d " ")" "4"
# A record is "state<TAB>bytes<TAB>sha256<TAB>path", so the state words are read
# from the tab-separated field and not from the start of the line: a manifest key
# line also starts with a word.
eq "every manifest record is a state word the product knows" \
  "$(sed -n "s/^file=\([a-z]*\)\t.*/\1/p" "$PKG/.uninstall/install-manifest.txt" | sort -u | tr "\n" " " | sed "s/ $//")" \
  "new unchanged"

sec "4b/15 --install into an empty directory: the shape a user actually takes"
FRESH="$OUT/clean/fresh"; rm -rf "$FRESH"; mkdir -p "$FRESH/.uninstall"
cp -r "$OUT/stage/.uninstall/payload" "$FRESH/.uninstall/payload"
FRC="$(run_exe "$FRESH/tf2_demo_native.exe" "$OUT/install-fresh.txt" --install --install-dir "$(cygpath -w "$FRESH")" 2>/dev/null || true)"
# The payload holds the product, not an installer: to install into a directory
# with no product yet, the product is run from the package and pointed at it.
FRC="$(run_exe "$OUT/stage/tf2_demo_native.exe" "$OUT/install-fresh.txt" --install --install-dir "$(cygpath -w "$FRESH")")"
eq "install into an empty directory exits 0" "$FRC" "0"
echo "  --- --install (empty directory) ---"
sed "s/^/    /" "$OUT/install-fresh.txt"
eq "a fresh install owns the three payload files plus the VERSION.txt it writes" \
  "$(field "$OUT/install-fresh.txt" files)" "4"
eq "a fresh install creates every one of them" "$(field "$OUT/install-fresh.txt" new)" "4"
inventory "$FRESH" "$OUT/installed-fresh.txt"
echo "  --- the empty directory after the install ---"
sed "s/^/    /" "$OUT/installed-fresh.txt"
eq "the fresh directory holds the payload, the version file and the manifest" \
  "$(wc -l < "$OUT/installed-fresh.txt" | tr -d " ")" "5"
FSRC="$(run_exe "$FRESH/tf2_demo_native.exe" "$OUT/status-fresh.txt" --install-status)"
eq "the fresh install reports VERIFIED" "$(cat "$OUT/status-fresh.txt" | grep -o "status=[A-Z]*" | head -1)" "status=VERIFIED"


sec "5/15 --install-status in a NEW process: the state is on disk, not in memory"
SRC="$(run_exe "$PKG/tf2_demo_native.exe" "$OUT/status.txt" --install-status)"
eq "product --install-status exit code" "$SRC" "0"
echo "  --- --install-status ---"
sed 's/^/    /' "$OUT/status.txt"
has "status is VERIFIED" "$(cat "$OUT/status.txt")" "status=VERIFIED"
eq "status counts every file the manifest lists as present" "$(field "$OUT/status.txt" present)" "4"
eq "status found nothing missing" "$(field "$OUT/status.txt" missing)" "0"
eq "status found nothing modified" "$(field "$OUT/status.txt" modified)" "0"
eq "status reports the installed version" "$(field "$OUT/status.txt" version)" "$declared_version"

sec "6/15 what a user does to an installed directory: edit one file, add another"
# This runs in the FRESH directory (section 4b), the one this package created
# every file in. The packaged prefix is a different shape -- its files were laid
# down by cmake --install before the lifecycle ever ran -- and the product
# deliberately treats a file it did not create as not-its-own there. Measuring
# "uninstall removes what it installed" in a directory where it installed
# nothing would be measuring the other contract.
echo 'user note' > "$FRESH/user-notes.txt"
echo 'edited by the user after the install' >> "$FRESH/install_smoke_probe.ps1"
SRC3="$(run_exe "$FRESH/tf2_demo_native.exe" "$OUT/status-edited.txt" --install-status)"
eq "--install-status exit code after an edit" "$SRC3" "0"
echo "  --- status after edit ---"
sed "s/^/    /" "$OUT/status-edited.txt"
has "status noticed the edit" "$(cat "$OUT/status-edited.txt")" "status=DEGRADED"
eq "status counts the edited file as modified" "$(field "$OUT/status-edited.txt" modified)" "1"

sec "7/15 --install over an existing install (the reinstall shape)"
# The payload is consumed by the first install, so an upgrade starts from the
# same place the first one did: the package stages it again. Without a payload the
# product says so by name instead of reinstalling half a package -- measured, and
# pinned here, because "reinstall silently did nothing" would be the worse answer.
NOPAY="$(run_exe "$PKG/tf2_demo_native.exe" "$OUT/reinstall-no-payload.txt" --install)"
eq "install with the payload already consumed exits 2" "$NOPAY" "2"
has "and names the missing payload" "$(cat "$OUT/reinstall-no-payload.txt")" "reason=no_payload"
cp -r "$OUT/stage/.uninstall/payload" "$PKG/.uninstall/payload"
IRC2="$(run_exe "$PKG/tf2_demo_native.exe" "$OUT/reinstall.txt" --install)"
eq "product --install (second time) exit code" "$IRC2" "0"
echo "  --- --install over the existing install ---"
sed "s/^/    /" "$OUT/reinstall.txt"
eq "the second install knows it is a reinstall" "$(field "$OUT/reinstall.txt" mode)" "reinstall"
eq "a reinstall leaves the package directory itself intact" \
  "$([ -f "$PKG/install_smoke_probe.ps1" ] && echo present || echo absent)" "present"
eq "the version the install carries is unchanged by a reinstall" \
  "$(sed -n "s/^packageVersion=//p" "$PKG/.uninstall/install-manifest.txt" | tr -d "\r")" "$declared_version"

# The fresh directory was edited by the user before this reinstall. Refreshing a
# file the user edited must keep the edited bytes recoverable, so exactly one
# backup is the correct reading -- a backup of 0 would mean the edit was
# silently destroyed by an operation that reported PASS.
cp -r "$OUT/stage/.uninstall/payload" "$FRESH/.uninstall/payload"
FRC2="$(run_exe "$FRESH/tf2_demo_native.exe" "$OUT/reinstall-fresh.txt" --install --install-dir "$(cygpath -w "$FRESH")")"
eq "reinstall over the edited install exits 0" "$FRC2" "0"
eq "the reinstall backed up the one file the user had edited" "$(field "$OUT/reinstall-fresh.txt" backups)" "1"
eq "the edited bytes are still in the backup" \
  "$([ -f "$FRESH/.uninstall/backup/install_smoke_probe.ps1" ] && echo present || echo absent)" "present"

sec "8/15 --uninstall: only what the manifest says, and the user files stay"
URC="$(run_exe "$FRESH/tf2_demo_native.exe" "$OUT/uninstall.txt" --uninstall --install-dir "$(cygpath -w "$FRESH")")"
# The uninstaller is one of the files it must remove and it is the running image,
# so the honest exit code is 3 (incomplete), not 0. A 0 here would mean the
# uninstaller claimed to have removed itself.
eq "product --uninstall exit code (incomplete by design: it is the running image)" "$URC" "3"
echo "  --- --uninstall ---"
sed "s/^/    /" "$OUT/uninstall.txt"
has "uninstall reported the incomplete removal honestly" "$(cat "$OUT/uninstall.txt")" "uninstall=INCOMPLETE"
eq "the file the user added is still there" \
  "$([ -f "$FRESH/user-notes.txt" ] && echo present || echo absent)" "present"
eq "the file the user edited is restored to the bytes it had before the reinstall" \
  "$([ -f "$FRESH/install_smoke_probe.ps1" ] && echo present || echo absent)" "present"
eq "the unedited installed files are gone" \
  "$([ -f "$FRESH/native_install_smoke_probe.exe" ] && echo present || echo absent)" "absent"
# The uninstaller IS the executable being uninstalled. Windows cannot delete a
# running image, so the correct reading is a NAMED deferral, not a PASS that
# silently leaves it behind -- and not a claim that it went.
eq "the running product is deferred by name, not deleted" \
  "$(field "$OUT/uninstall.txt" deferredRunningImage)" "1"
has "and the deferral names the file" "$(cat "$OUT/uninstall.txt")" "deferred_running_image=tf2_demo_native.exe"
has "so the verdict is INCOMPLETE, not PASS" "$(cat "$OUT/uninstall.txt")" "uninstall=INCOMPLETE"
eq "the state directory is gone" "$([ -d "$FRESH/.uninstall" ] && echo present || echo absent)" "absent"
eq "the version file the install wrote is gone" \
  "$([ -f "$FRESH/VERSION.txt" ] && echo present || echo absent)" "absent"
inventory "$FRESH" "$OUT/after-uninstall.txt"
echo "  --- what is left ---"
sed "s/^/    /" "$OUT/after-uninstall.txt"
eq "what is left is the two files the user made plus the deferred running image" \
  "$(cut -f1 "$OUT/after-uninstall.txt" | tr "\n" " " | sed "s/ *$//")" "install_smoke_probe.ps1 tf2_demo_native.exe user-notes.txt"
eq "the install directory itself survives" "$([ -d "$FRESH" ] && echo present || echo absent)" "present"

sec "uninstall refuses a directory that is not an install"
NOTAN="$OUT/clean/not-an-install"; rm -rf "$NOTAN"; mkdir -p "$NOTAN"
echo 'precious' > "$NOTAN/data.txt"
NRC="$(run_exe "$PKG/tf2_demo_native.exe" "$OUT/uninstall-not-install.txt" --uninstall --install-dir "$(cygpath -w "$NOTAN")")"
eq "uninstall in a directory with no manifest exits 2" "$NRC" "2"
has "and it names the missing manifest rather than deleting anything" \
  "$(cat "$OUT/uninstall-not-install.txt")" "reason=no_manifest"
eq "the user file is untouched" "$([ -f "$NOTAN/data.txt" ] && echo present || echo absent)" "present"

sec "install refuses an empty payload instead of pretending"
EMPTY="$OUT/clean/empty-payload"; rm -rf "$EMPTY"; mkdir -p "$EMPTY/.uninstall/payload"
ERC="$(run_exe "$PKG/tf2_demo_native.exe" "$OUT/install-empty.txt" --install --install-dir "$(cygpath -w "$EMPTY")")"
eq "install with an empty payload exits 2" "$ERC" "2"
has "and says why" "$(cat "$OUT/install-empty.txt")" "reason=empty_payload"

sec "the install is a directory, not machine state"
eq "installed files contain no registry or service artefact" \
  "$(grep -ciE 'registry|HKEY_' "$OUT/installed-files.txt" 2>/dev/null | head -1 | tr -d ' ')" "0"
eq "the manifest is the only state the install writes" "${STATE_FILES_AFTER_INSTALL:-none}" "1"

# ============================================================ T42 lifecycle cases
# Each case below is a separate staged directory under $OUT/clean (or $OUT/cases
# for capture output) and a separate process. No case reuses another's directory.

sec "9/15 upgrade and downgrade: the installed packageVersionCode is compared"
# T37 recorded "nothing compares packageVersionCode" as a named gap; the smallest
# honest closure is a DECISION, not a migration engine. An older package over a
# newer install is refused by name, a newer one says it is an upgrade, and a
# same-code one keeps its existing mode=reinstall. The upgrade package is the
# shipped one re-stamped through VERSION.txt (the documented no-rebuild release
# path) with one payload byte changed, so the reading is about delivered content
# and not only about a version string.
UP="$OUT/clean/upgrade"; rm -rf "$UP"; mkdir -p "$UP/target/.uninstall"
cp -r "$OUT/stage/.uninstall/payload" "$UP/target/.uninstall/payload"
U0="$(run_exe "$OUT/stage/tf2_demo_native.exe" "$OUT/upgrade-install-100.txt" --install --install-dir "$(cygpath -w "$UP/target")")"
eq "the first install of the pair exits 0" "$U0" "0"
eq "the pair starts as a fresh install of the shipped version" \
  "$(field "$OUT/upgrade-install-100.txt" mode)" "fresh"
UPKG="$UP/package-200"; cp -r "$OUT/stage" "$UPKG"
printf 'package=tf2_demo_native\nversion=0.2.0\nversionCode=200\n' > "$UPKG/VERSION.txt"
echo '# upgraded payload' >> "$UPKG/.uninstall/payload/install_smoke_probe.ps1"
cp -r "$UPKG/.uninstall/payload" "$UP/target/.uninstall/payload"
U1="$(run_exe "$UPKG/tf2_demo_native.exe" "$OUT/upgrade-install-200.txt" --install --install-dir "$(cygpath -w "$UP/target")")"
eq "the upgrade exits 0" "$U1" "0"
echo "  --- --install (newer package over the install) ---"
sed "s/^/    /" "$OUT/upgrade-install-200.txt"
eq "the install names the operation an upgrade" "$(field "$OUT/upgrade-install-200.txt" mode)" "upgrade"
eq "the upgrade replaces the one file whose payload bytes changed" \
  "$(field "$OUT/upgrade-install-200.txt" changed)" "1"
eq "the pre-upgrade bytes of that file were backed up before replacement" \
  "$(field "$OUT/upgrade-install-200.txt" backups)" "1"
eq "the manifest now names the upgraded version" \
  "$(sed -n "s/^packageVersion=//p" "$UP/target/.uninstall/install-manifest.txt" | tr -d "\r")" "0.2.0"
eq "the manifest now names the upgraded versionCode" \
  "$(sed -n "s/^packageVersionCode=//p" "$UP/target/.uninstall/install-manifest.txt" | tr -d "\r")" "200"
eq "the upgraded file on disk carries the new payload's marker" \
  "$(grep -c "upgraded payload" "$UP/target/install_smoke_probe.ps1" 2>/dev/null | tr -d " ")" "1"
US="$(run_exe "$UP/target/tf2_demo_native.exe" "$OUT/upgrade-status.txt" --install-status)"
eq "status after the upgrade exits 0" "$US" "0"
eq "status after the upgrade reports the new version" "$(field "$OUT/upgrade-status.txt" version)" "0.2.0"
eq "status after the upgrade finds nothing missing" "$(field "$OUT/upgrade-status.txt" missing)" "0"

# The downgrade is the half a version comparison exists for. It must be refused by
# name without touching one byte of the newer installation. The target holds no
# staged payload at this point, which is deliberate: the request is refused for
# being a downgrade, not for being unpacked in the wrong order.
D0="$(run_exe "$OUT/stage/tf2_demo_native.exe" "$OUT/downgrade.txt" --install --install-dir "$(cygpath -w "$UP/target")")"
eq "an older package over a newer install exits 2" "$D0" "2"
echo "  --- --install (older package over newer install) ---"
sed "s/^/    /" "$OUT/downgrade.txt"
has "the refusal names both version codes" "$(cat "$OUT/downgrade.txt")" \
  "install=FAIL reason=downgrade installedVersionCode=200 packageVersionCode=100"
hasnt "the refusal did not start an install it would not finish" "$(cat "$OUT/downgrade.txt")" "install=START"
eq "the refused downgrade left the newer manifest in place" \
  "$(sed -n "s/^packageVersionCode=//p" "$UP/target/.uninstall/install-manifest.txt" | tr -d "\r")" "200"
eq "the refused downgrade left the upgraded payload byte in place" \
  "$(grep -c "upgraded payload" "$UP/target/install_smoke_probe.ps1" 2>/dev/null | tr -d " ")" "1"
DS="$(run_exe "$UP/target/tf2_demo_native.exe" "$OUT/downgrade-status.txt" --install-status)"
eq "and the directory still verifies against the newer manifest" \
  "$(field "$OUT/downgrade-status.txt" missing)" "0"

sec "10/15 migration: the installed directory moved somewhere else"
# The manifest records where the install happened, but status and uninstall read
# the directory they are run from. A move is therefore a migration the product
# does not have to be told about; this measures that instead of assuming it.
MIG="$OUT/clean/migrated"; rm -rf "$MIG"; cp -r "$UP/target" "$MIG"
echo "user notes that were never installed" > "$MIG/user-notes.txt"
M0="$(run_exe "$MIG/tf2_demo_native.exe" "$OUT/migrated-status.txt" --install-status)"
eq "status in the moved directory exits 0" "$M0" "0"
has "the moved install verifies against its manifest" "$(cat "$OUT/migrated-status.txt")" "status=VERIFIED"
eq "the moved install reports the version it was upgraded to" "$(field "$OUT/migrated-status.txt" version)" "0.2.0"
eq "the foreign user file is neither missing nor modified" \
  "$(field "$OUT/migrated-status.txt" missing):$(field "$OUT/migrated-status.txt" modified)" "0:0"
has "the manifest still records the OLD install path (nothing reads it)" \
  "$(cat "$MIG/.uninstall/install-manifest.txt")" "/upgrade/target"
MIG_MANIFEST="$(cat "$MIG/.uninstall/install-manifest.txt" 2>/dev/null)"
hasnt "the user's note was never in the manifest (it was added after the install)" \
  "$MIG_MANIFEST" "user-notes.txt"
M1="$(run_exe "$MIG/tf2_demo_native.exe" "$OUT/migrated-uninstall.txt" --uninstall)"
eq "the moved install uninstalls (incomplete only for its running image)" "$M1" "3"
echo "  --- --uninstall (moved install) ---"
sed "s/^/    /" "$OUT/migrated-uninstall.txt"
eq "the uninstaller restored the pre-upgrade bytes of the file the upgrade replaced" \
  "$(field "$OUT/migrated-uninstall.txt" restored)" "1"
eq "and the pre-upgrade payload bytes are back on disk" \
  "$(grep -c "upgraded payload" "$MIG/install_smoke_probe.ps1" 2>/dev/null | tr -d " ")" "0"
eq "the user file that was never installed is still there" \
  "$([ -f "$MIG/user-notes.txt" ] && echo present || echo absent)" "present"
eq "the uninstall removed the two installed files the upgrade left behind" \
  "$(field "$OUT/migrated-uninstall.txt" removed)" "2"
eq "and it did not claim the file it never recorded as preserved (preserved counts manifest records only)" \
  "$(field "$OUT/migrated-uninstall.txt" preserved)" "0"
eq "the running image is deferred by name, not deleted" \
  "$(field "$OUT/migrated-uninstall.txt" deferredRunningImage)" "1"

sec "11/15 first launch: the installed directory boots and saves the root it connected"
LAUNCH="$OUT/clean/launch"; rm -rf "$LAUNCH"; mkdir -p "$LAUNCH/.uninstall"
cp -r "$OUT/stage/.uninstall/payload" "$LAUNCH/.uninstall/payload"
L0="$(run_exe "$OUT/stage/tf2_demo_native.exe" "$OUT/launch-install.txt" --install --install-dir "$(cygpath -w "$LAUNCH")")"
eq "the directory the first launch boots is a real install" "$L0" "0"
eq "it is a fresh install of the shipped version" "$(field "$OUT/launch-install.txt" mode)" "fresh"
FIX_ROOT="$FIX/roots/vpk-valid"
LAUNCH_CASE="$OUT/cases/first-launch"
LR="$(run_product "$LAUNCH/tf2_demo_native.exe" "$FIX/demo/t32_marker.dem" "$LAUNCH_CASE" "$FIX_ROOT" -)"
eq "first launch exit code" "$LR" "0"
has "the capture harness read a title from its own process" "$(cat "$LAUNCH_CASE/capture.log")" "TITLE-CAPTURE=PASS"
FIX_ROOT_WIN="$(cygpath -w "$FIX_ROOT")"
eq "first launch root valid" "$(resources_valid "$LAUNCH_CASE/trace.txt")" "1"
eq "first launch connected to the fixture root it was given" \
  "$(norm_path "$(connected_root "$LAUNCH_CASE/title-last.txt")")" "$(norm_path "$FIX_ROOT_WIN")"
has "and the title carries the archive bracket a connected run has" \
  "$(cat "$LAUNCH_CASE/title-last.txt")" "[VPK entries:"
eq "first launch opened the fixture archive" "$(archives_opened "$LAUNCH_CASE/trace.txt")" "1"
eq "first launch refused no archive" "$(archives_refused "$LAUNCH_CASE/trace.txt")" "0"
eq "first launch parsed the fixture map out of that archive" \
  "$(trace_store "$LAUNCH_CASE/trace.txt")" "vpk"
LSETTINGS="$LAUNCH_CASE/settings.ini"
eq "first launch wrote its settings file" "$([ -s "$LSETTINGS" ] && echo yes || echo no)" "yes"
eq "and saved the root it connected" \
  "$(norm_path "$(sed -n "s/^tfRoot=//p" "$LSETTINGS" | tail -1)")" "$(norm_path "$FIX_ROOT_WIN")"
eq "the install directory itself holds no settings file (the capture used its own)" \
  "$([ -f "$LAUNCH/settings.ini" ] && echo present || echo absent)" "absent"

sec "12/15 restart: a second process reuses the root the first one saved"
RESTART_CASE="$OUT/cases/restart"
LR2="$(run_product "$LAUNCH/tf2_demo_native.exe" "$FIX/demo/t32_marker.dem" "$RESTART_CASE" - "$LSETTINGS")"
eq "restart exit code" "$LR2" "0"
eq "restart was given no root on its command line" \
  "$(sed -n "s/.*resources: requested=\([^ ]*\).*/\1/p" "$RESTART_CASE/trace.txt" | tail -1)" "-"
eq "restart connected to the fixture root the first launch saved" \
  "$(norm_path "$(connected_root "$RESTART_CASE/title-last.txt")")" "$(norm_path "$FIX_ROOT_WIN")"
eq "restart resolved a valid root" "$(resources_valid "$RESTART_CASE/trace.txt")" "1"
eq "restart opened the fixture archive" "$(archives_opened "$RESTART_CASE/trace.txt")" "1"
eq "restart did not need a second settings file" \
  "$([ -f "$RESTART_CASE/settings.ini" ] && echo yes || echo no)" "no"

sec "13/15 non-admin shape: an install into a directory the account may not write"
# This host runs elevated, so a real Program Files install would measure nothing.
# The non-admin failure is reproduced where the product actually meets it: a
# deny-write DACL for the running account on the target directory. The payload is
# unpacked BEFORE the ACL is applied, which is the shape a user takes ("unpack
# here", then install into a protected directory). The ACL is removed again at the
# end of the section so the case can be cleaned up.
PROT="$OUT/clean/protected"; rm -rf "$PROT"; mkdir -p "$PROT/.uninstall"
cp -r "$OUT/stage/.uninstall/payload" "$PROT/.uninstall/payload"
echo "user data that must survive a refused install" > "$PROT/user-data.txt"
inventory "$PROT" "$OUT/protected-before.txt"
python "$FIXTURES_PY" --out "$(cygpath -w "$FIX")" --deny-write "$(cygpath -w "$PROT")" > "$OUT/logs/protected-deny.log" 2>&1
eq "the deny-write ACL was applied to the target directory" "$?" "0"
PN="$(run_exe "$OUT/stage/tf2_demo_native.exe" "$OUT/protected-install.txt" --install --install-dir "$(cygpath -w "$PROT")")"
eq "install into a write-denied directory exits 2" "$PN" "2"
echo "  --- --install (deny-write directory) ---"
sed "s/^/    /" "$OUT/protected-install.txt"
has "and names the copy the account could not perform" "$(cat "$OUT/protected-install.txt")" "install=FAIL reason=cannot_copy"
hasnt "the refusal cannot be read as a PASS" "$(cat "$OUT/protected-install.txt")" "install=PASS"
inventory "$PROT" "$OUT/protected-after.txt"
eq "the refused install wrote nothing into the directory" \
  "$(diff -q "$OUT/protected-before.txt" "$OUT/protected-after.txt" >/dev/null 2>&1 && echo same || echo different)" "same"
eq "the user's file survived the refused install" "$([ -f "$PROT/user-data.txt" ] && echo present || echo absent)" "present"
eq "no manifest was written by the refused install" \
  "$([ -f "$PROT/.uninstall/install-manifest.txt" ] && echo present || echo absent)" "absent"
python "$FIXTURES_PY" --out "$(cygpath -w "$FIX")" --allow-write "$(cygpath -w "$PROT")" > "$OUT/logs/protected-allow.log" 2>&1
eq "the deny-write ACL was removed again" "$?" "0"

sec "14/15 missing runtime file: status names it, --version falls back, uninstall counts it"
MISS="$OUT/clean/missing"; rm -rf "$MISS"; mkdir -p "$MISS/.uninstall"
cp -r "$OUT/stage/.uninstall/payload" "$MISS/.uninstall/payload"
MI0="$(run_exe "$OUT/stage/tf2_demo_native.exe" "$OUT/missing-install.txt" --install --install-dir "$(cygpath -w "$MISS")")"
eq "the directory the missing-file case starts from is a real install" "$MI0" "0"
eq "the install owns four records (three payload files and the version file)" \
  "$(field "$OUT/missing-install.txt" files)" "4"
rm -f "$MISS/native_install_smoke_probe.exe" "$MISS/VERSION.txt"
MI1="$(run_exe "$MISS/tf2_demo_native.exe" "$OUT/missing-status.txt" --install-status)"
eq "status with two installed files removed exits 0 (it reports, it does not crash)" "$MI1" "0"
echo "  --- --install-status (an installed runtime file and VERSION.txt removed) ---"
sed "s/^/    /" "$OUT/missing-status.txt"
has "status is DEGRADED, not VERIFIED" "$(cat "$OUT/missing-status.txt")" "status=DEGRADED"
eq "status counts the two removed files as missing" "$(field "$OUT/missing-status.txt" missing)" "2"
eq "status still counts the two intact records as present" "$(field "$OUT/missing-status.txt" present)" "2"
eq "status found nothing modified (a missing file is not a modified one)" \
  "$(field "$OUT/missing-status.txt" modified)" "0"
MV="$(run_exe "$MISS/tf2_demo_native.exe" "$OUT/missing-version.txt" --version)"
eq "the installed product still answers --version with its version file gone" "$MV" "0"
eq "--version wrote a non-empty answer to the redirect" \
  "$([ -s "$OUT/missing-version.txt" ] && echo yes || echo no)" "yes"
has "--version still names the package" "$(cat "$OUT/missing-version.txt")" "package=tf2_demo_native"
has "--version falls back to the compiled version" "$(cat "$OUT/missing-version.txt")" "version=0.1.0"
MU="$(run_exe "$MISS/tf2_demo_native.exe" "$OUT/missing-uninstall.txt" --uninstall)"
eq "uninstall with two files already missing exits 3 (its running image is deferred)" "$MU" "3"
echo "  --- --uninstall (two files already missing) ---"
sed "s/^/    /" "$OUT/missing-uninstall.txt"
eq "uninstall counts the two missing records as alreadyMissing" \
  "$(field "$OUT/missing-uninstall.txt" alreadyMissing)" "2"
eq "uninstall removes the one installed file that is still there" \
  "$(field "$OUT/missing-uninstall.txt" removed)" "1"
eq "that file is gone" "$([ -f "$MISS/install_smoke_probe.ps1" ] && echo present || echo absent)" "absent"
eq "the file it cannot remove is still a named deferral, not a removal" \
  "$(field "$OUT/missing-uninstall.txt" deferredRunningImage)" "1"

sec "15/15 VPK states: absent, unreadable, unsupported, truncated and valid, through vpk_query AND the installed product"
# The lifecycle fixture script was written by T37 to build exactly these roots; this
# is the gate that finally drives them. Two readers see the same five copies:
#   vpk_query              the operator's question, answered with ONE stable
#                          category word (ABSENT/UNREADABLE/UNSUPPORTED/TRUNCATED/
#                          OPEN_FAILED) plus the reader's own reason.
#   the installed product  the same copies as the tf root of a real launch: an
#                          absent archive is skipped (refused=0), a present one
#                          that will not open is counted and attributed.
vpk_state_query() { # <root-name> -> rc
  "$VPK_QUERY" "$(cygpath -w "$FIX/roots/$1")" maps .bsp > "$OUT/vpk-query-$1.txt" 2>&1
  echo $?
}
vpk_query_line() { sed -n "s/^pak01_dir.vpk: //p" "$OUT/vpk-query-$1.txt" | head -1; }
vpk_query_category() { vpk_query_line "$1" | sed -n "s/^\([A-Z_]*\) .*/\1/p" | tr "A-Z" "a-z"; }
vpk_attribution() { sed -n "s/.*vpk: refused attribution=\([^ ]*\).*/\1/p" "$1" 2>/dev/null | tail -1; }
vpk_state_product() { # <root-name> -> rc
  run_product "$LAUNCH/tf2_demo_native.exe" "$FIX/demo/t32_marker.dem" "$OUT/cases/$1" "$FIX/roots/$1" -
}
state_case() { printf "%s" "$OUT/cases/$1"; }

# ---- valid: the positive control
QR="$(vpk_state_query vpk-valid)"; eq "valid: vpk_query exit code" "$QR" "0"
eq "valid: vpk_query opened the archive and found the map" \
  "$(vpk_query_line vpk-valid)" "1 entries, 1 match 'maps' ext=.bsp"
PR="$(vpk_state_product vpk-valid)"; eq "valid: the installed product boots" "$PR" "0"
eq "valid: product opened exactly one archive" "$(archives_opened "$(state_case vpk-valid)/trace.txt")" "1"
eq "valid: product refused nothing" "$(archives_refused "$(state_case vpk-valid)/trace.txt")" "0"
eq "valid: product read the map out of the archive" "$(trace_store "$(state_case vpk-valid)/trace.txt")" "vpk"
has "valid: the archived BSP was parsed" "$(bsp_segment "$(state_case vpk-valid)/title-last.txt")" " tris"

# ---- absent: not there, and that is not a refusal
QR="$(vpk_state_query vpk-missing)"; eq "absent: vpk_query exit code" "$QR" "0"
eq "absent: vpk_query names the file absent and the OS reason" \
  "$(vpk_query_line vpk-missing)" "ABSENT (ERROR_FILE_NOT_FOUND)"
PR="$(vpk_state_product vpk-missing)"; eq "absent: the installed product boots" "$PR" "0"
eq "absent: product opened no archive (there is none)" "$(archives_opened "$(state_case vpk-missing)/trace.txt")" "0"
eq "absent: product counted no refusal" "$(archives_refused "$(state_case vpk-missing)/trace.txt")" "0"
hasnt "absent: the trace carries no refusal line at all" "$(cat "$(state_case vpk-missing)/trace.txt")" "vpk: refused"
eq "absent: the title has no VPK refusal count" "$(title_refused "$(state_case vpk-missing)/title-last.txt")" ""
eq "absent: the loose map still answered" "$(trace_store "$(state_case vpk-missing)/trace.txt")" "maps"

# ---- unreadable: present, and the account may not read it
QR="$(vpk_state_query vpk-unreadable)"; eq "unreadable: vpk_query exit code" "$QR" "0"
eq "unreadable: vpk_query separates it from absent and from refused" \
  "$(vpk_query_line vpk-unreadable)" "UNREADABLE (ERROR_ACCESS_DENIED)"
PR="$(vpk_state_product vpk-unreadable)"; eq "unreadable: the installed product boots" "$PR" "0"
eq "unreadable: the root stayed valid (the archive NAME is there; readability is a separate question)" \
  "$(resources_valid "$(state_case vpk-unreadable)/trace.txt")" "1"
eq "unreadable: product opened no archive" "$(archives_opened "$(state_case vpk-unreadable)/trace.txt")" "0"
eq "unreadable: product counted one present-but-unopenable archive" \
  "$(archives_refused "$(state_case vpk-unreadable)/trace.txt")" "1"
eq "unreadable: product names the reader's own reason" \
  "$(refused_reason "$(state_case vpk-unreadable)/trace.txt")" "cannot read VPK directory file"
eq "unreadable: product attribution is the same category vpk_query used" \
  "$(vpk_attribution "$(state_case vpk-unreadable)/trace.txt")" "$(vpk_query_category vpk-unreadable)"
eq "unreadable: the title carries the refusal count" "$(title_refused "$(state_case vpk-unreadable)/title-last.txt")" "1"
eq "unreadable: the map could not be read" "$(trace_store "$(state_case vpk-unreadable)/trace.txt")" "none"

# ---- unsupported: a well-formed file in a format this reader does not know
QR="$(vpk_state_query vpk-format)"; eq "unsupported: vpk_query exit code" "$QR" "0"
eq "unsupported: vpk_query names the format, not corruption" \
  "$(vpk_query_line vpk-format)" "UNSUPPORTED (unsupported VPK header)"
PR="$(vpk_state_product vpk-format)"; eq "unsupported: the installed product boots" "$PR" "0"
eq "unsupported: product counted one refusal" "$(archives_refused "$(state_case vpk-format)/trace.txt")" "1"
eq "unsupported: product repeats the named format reason" \
  "$(refused_reason "$(state_case vpk-format)/trace.txt")" "unsupported VPK header"
eq "unsupported: product attribution is the same category vpk_query used" \
  "$(vpk_attribution "$(state_case vpk-format)/trace.txt")" "$(vpk_query_category vpk-format)"
eq "unsupported: the title carries the refusal count" "$(title_refused "$(state_case vpk-format)/title-last.txt")" "1"

# ---- truncated: a valid header whose tree runs past EOF
QR="$(vpk_state_query vpk-truncated-tree)"; eq "truncated: vpk_query exit code" "$QR" "0"
eq "truncated: vpk_query names the truncation, not the format" \
  "$(vpk_query_line vpk-truncated-tree)" "TRUNCATED (truncated VPK directory tree)"
PR="$(vpk_state_product vpk-truncated-tree)"; eq "truncated: the installed product boots" "$PR" "0"
eq "truncated: product counted one refusal" \
  "$(archives_refused "$(state_case vpk-truncated-tree)/trace.txt")" "1"
eq "truncated: product repeats the named truncation reason" \
  "$(refused_reason "$(state_case vpk-truncated-tree)/trace.txt")" "truncated VPK directory tree"
eq "truncated: product attribution is the same category vpk_query used" \
  "$(vpk_attribution "$(state_case vpk-truncated-tree)/trace.txt")" "$(vpk_query_category vpk-truncated-tree)"

# ---- the distinction the whole section exists for
absent_line="$(vpk_query_line vpk-missing)"
corrupt_lines="$(printf "%s\n%s\n%s\n" "$(vpk_query_line vpk-unreadable)" \
  "$(vpk_query_line vpk-format)" "$(vpk_query_line vpk-truncated-tree)")"
eq "absent is not any corrupt or unreadable answer (vpk_query)" \
  "$(case "$corrupt_lines" in *"$absent_line"*) echo shared;; *) echo distinct;; esac)" "distinct"
product_attributions="$(printf "%s\n%s\n%s\n" \
  "$(vpk_attribution "$(state_case vpk-unreadable)/trace.txt")" \
  "$(vpk_attribution "$(state_case vpk-format)/trace.txt")" \
  "$(vpk_attribution "$(state_case vpk-truncated-tree)/trace.txt")")"
eq "the three present-but-refused states get three different product attributions" \
  "$(printf "%s\n" "$product_attributions" | sort -u | wc -l | tr -d " ")" "3"
eq "and absent stays refused=0 while every present-but-broken copy is refused=1" \
  "$(archives_refused "$(state_case vpk-missing)/trace.txt"):$(archives_refused "$(state_case vpk-truncated-tree)/trace.txt")" "0:1"
eq "vpk_query gives the five states five different answers" \
  "$(printf "%s\n%s\n%s\n%s\n%s\n" "$(vpk_query_line vpk-valid)" "$absent_line" \
    "$(vpk_query_line vpk-unreadable)" "$(vpk_query_line vpk-format)" \
    "$(vpk_query_line vpk-truncated-tree)" | sort -u | wc -l | tr -d " ")" "5"

# ---- the two limitation roots: measured and named, not skipped
QR="$(vpk_state_query vpk-volume-missing)"; eq "volume-missing: vpk_query exit code" "$QR" "0"
eq "volume-missing: vpk_query lists the entry the archive cannot produce" \
  "$(vpk_query_line vpk-volume-missing)" "1 entries, 1 match 'maps' ext=.bsp"
PR="$(vpk_state_product vpk-volume-missing)"; eq "volume-missing: the installed product boots" "$PR" "0"
eq "volume-missing: the directory index opened" \
  "$(archives_opened "$(state_case vpk-volume-missing)/trace.txt")" "1"
eq "volume-missing: and no refusal is counted (the open succeeded)" \
  "$(archives_refused "$(state_case vpk-volume-missing)/trace.txt")" "0"
eq "volume-missing: the map read still produced no bytes" \
  "$(trace_bytes "$(state_case vpk-volume-missing)/trace.txt")" "0"
eq "volume-missing: NAMED LIMITATION: store=none with no per-entry reason" \
  "$(trace_store "$(state_case vpk-volume-missing)/trace.txt")" "none"

QR="$(vpk_state_query precedence-shadow)"; eq "shadow: vpk_query exit code" "$QR" "0"
eq "shadow: vpk_query still lists the archived copy" "$(vpk_query_line precedence-shadow)" \
  "1 entries, 1 match 'maps' ext=.bsp"
PR="$(vpk_state_product precedence-shadow)"; eq "shadow: the installed product boots" "$PR" "0"
eq "shadow: the bad loose file answered before the valid archived one" \
  "$(trace_store "$(state_case precedence-shadow)/trace.txt")" "maps"
case "$(bsp_segment "$(state_case precedence-shadow)/title-last.txt")" in
  invalid*) ok "shadow: the bad loose BSP shadowed the valid archived one" "$(bsp_segment "$(state_case precedence-shadow)/title-last.txt")";;
  *) wrong "shadow: the bad loose BSP shadowed the valid archived one" "$(bsp_segment "$(state_case precedence-shadow)/title-last.txt")" "invalid/...";;
esac

# The only fixture with an ACE is reset before the gate exits, so the out
# directory can be deleted by the next reader.
python "$FIXTURES_PY" --out "$(cygpath -w "$FIX")" --demo "$(cygpath -w "$REAL_DEMO")" --bsp "$(cygpath -w "$BSP_SOURCE")" --reset-acl > "$OUT/logs/fixtures-reset-acl.log" 2>&1
eq "the deny-read VPK fixture ACL was reset for cleanup" "$?" "0"

cases=15

echo
echo "  cases=$cases checks=$CHECKS mutations=0"
if [ "$FAIL" -eq 0 ]; then echo "  INSTALL-LIFECYCLE=PASS"; exit 0; fi
echo "  INSTALL-LIFECYCLE=FAIL fail=$FAIL"
exit 1
fi

# ==================================================================== mutation
sec "mutation preconditions: the forward run"
for need in "$OUT/clean/pkg" "$OUT/version.txt" "$OUT/reference-exe.sha256"; do
  eq "forward artifact ${need##*/}" "$([ -e "$need" ] && echo present || echo absent)" "present"
done
# The identity of the binary every fixture must NOT be running. A fixture whose
# uninstaller hashes to this is MUTATION-NOT-LINKED: the mutation was never in the
# process that produced the reading, so a "miss" would be evidence of nothing.
REFERENCE_EXE_SHA="$(cat "$OUT/reference-exe.sha256" 2>/dev/null)"
MUTATED_EXE_SHA=""
echo "  reference exe sha256=${REFERENCE_EXE_SHA:-missing}"
MUT="$OUT/mutation"; mkdir -p "$MUT/backup" "$MUT/logs"
SRC_MAIN="$REPO/native/src/main.cpp"
SRC_LIFE="$REPO/native/src/install_lifecycle.cpp"
SRC_MAN="$REPO/native/src/install_manifest.cpp"
cp "$SRC_MAIN" "$MUT/backup/main.cpp"
cp "$SRC_LIFE" "$MUT/backup/install_lifecycle.cpp"
cp "$SRC_MAN" "$MUT/backup/install_manifest.cpp"

# A MUTATION MUST RUN ITS OWN BINARY.
#   A build that silently did not relink leaves the previous exe in place and every
#   reading after it is a reading of the REFERENCE product -- the mutation then
#   "fails to be caught" for a reason that has nothing to do with the mutation.
#   The exe hash is compared across the rebuild so this can never be mistaken for
#   a real miss: measured 2026-10-20, M2's mutated binary was byte-identical to the
#   reference one because the link had failed quietly.
rebuild() { # <log-name>
  local before after rc=0
  before="$(sha256sum "$EXE" 2>/dev/null | cut -d' ' -f1)"
  bash build-target.sh tf2_demo_native install_cli_probe > "$MUT/logs/$1.log" 2>&1 || rc=$?
  after="$(sha256sum "$EXE" 2>/dev/null | cut -d' ' -f1)"
  MUTATED_EXE_SHA="$after"
  echo "  rebuild $1 rc=$rc reference=${REFERENCE_EXE_SHA:0:12} exe_before=${before:0:12} exe_after=${after:0:12}"
  if [ "$rc" = "0" ] && [ "$before" = "$after" ]; then
    echo "  MUTATION-NOT-LINKED: the exe is byte-identical after a supposedly mutated rebuild"
    echo "  --- last 12 lines of $MUT/logs/$1.log ---"
    tail -12 "$MUT/logs/$1.log" | sed 's/^/    /'
    return 90
  fi
  return $rc
}
fresh_package() { # <prefix>
  stage_package "$1" mutation-stage || return 1
  return 0
}

# A FIXTURE MUST RUN THE BINARY THE MUTATION BUILT.
#   rebuild() proves the BUILD DIRECTORY changed; it cannot see which copy a gate
#   then executes. T37 measured exactly that gap: M2's fixture copied its payload
#   from the forward run's staging directory, so the uninstaller that ran was the
#   REFERENCE build and the mutation was never in the process. The reading was
#   honest and about the wrong program. This guard turns that class into a named
#   MUTATION-NOT-LINKED instead of a silent miss, and prints both hashes.
mutated_binary_identity() { # <exe> <label>
  local sha
  sha="$(sha256sum "$1" 2>/dev/null | cut -d' ' -f1)"
  if [ -z "$sha" ]; then wrong "$2 exists" "absent" "present"; return 1; fi
  if [ "$sha" = "${MUTATED_EXE_SHA:-}" ]; then
    ok "$2 is the binary the mutation built" "${sha:0:12}"
    return 0
  fi
  if [ -n "${REFERENCE_EXE_SHA:-}" ] && [ "$sha" = "$REFERENCE_EXE_SHA" ]; then
    echo "  MUTATION-NOT-LINKED: $2 is the REFERENCE binary (${sha:0:12}), not the mutated one (${MUTATED_EXE_SHA:0:12})"
    return 90
  fi
  wrong "$2 is the binary the mutation built" "${sha:0:12}" "${MUTATED_EXE_SHA:0:12}"
  return 1
}
mutation_not_linked() { # <message>
  echo "  MUTATION-NOT-LINKED: $1"
  echo "  cases=3 checks=$CHECKS mutations=$MUTATIONS"
  echo "  INSTALL-LIFECYCLE-MUTATION=MUTATION-NOT-LINKED"
  exit 90
}

# ---------------------------------------------------------------- M1 stdout
sec "M1 stdout: the product takes the redirected handle again"
python - "$SRC_MAIN" <<'PY'
import sys
path = sys.argv[1]
data = open(path, 'rb').read()
old = b"  if (noStdout) {"
new = b"  if (true) {"
if old not in data:
    sys.exit("M1 anchor absent")
open(path, "wb").write(data.replace(old, new, 1))
print("  mutated: the console is attached even when the caller redirected stdout")
PY
eq "M1 applied" "$?" "0"
M1_BUILD=0
rebuild m1-build || M1_BUILD=$?
eq "M1 build succeeded AND relinked" "$M1_BUILD" "0"
fresh_package "$MUT/pkg-m1" || echo "  stage warning"
M1_LINKED=0
mutated_binary_identity "$MUT/pkg-m1/tf2_demo_native.exe" "M1 fixture binary" || M1_LINKED=$?
if [ "$M1_LINKED" = "90" ]; then mutation_not_linked "the M1 fixture would run the reference build"; fi
M1RC="$(run_exe "$MUT/pkg-m1/tf2_demo_native.exe" "$MUT/m1-version.txt" --version)"
echo "  under M1: exit=$M1RC bytes=$(wc -c < "$MUT/m1-version.txt" | tr -d ' ')"
MUTATIONS=$((MUTATIONS + 1))
if [ "$M1RC" = "0" ] && [ ! -s "$MUT/m1-version.txt" ]; then
  ok "M1 caught: exit 0 with an empty redirect file" "exit=0 bytes=0"
else
  wrong "M1 caught: exit 0 with an empty redirect file" \
    "exit=$M1RC bytes=$(wc -c < "$MUT/m1-version.txt" | tr -d ' ')" "exit=0 bytes=0"
fi
cp "$MUT/backup/main.cpp" "$SRC_MAIN"
if cmp -s "$SRC_MAIN" "$MUT/backup/main.cpp"; then ok "M1 source restored byte-identical" "yes"; else wrong "M1 source restored byte-identical" "no" "yes"; fi
rebuild m1-restore
eq "M1 restore build succeeded" "$?" "0"

# ------------------------------------------------------- M2 uninstall deletes
sec "M2 uninstall: the manifest's state words are ignored, so every listed file goes"
python - "$SRC_LIFE" <<'PY'
import sys
path = sys.argv[1]
data = open(path, "rb").read()
old = b"    if (file.state == kStatePreserved || file.state == kStateUnchanged) { ++preserved; continue; }"
new = b"    if (false) { ++preserved; continue; }"
if old not in data:
    sys.exit("M2 anchor absent")
open(path, "wb").write(data.replace(old, new, 1))
print("  mutated: preserved/unchanged records are no longer skipped")
PY
eq "M2 applied" "$?" "0"
M2_BUILD=0
rebuild m2-build || M2_BUILD=$?
eq "M2 build succeeded AND relinked" "$M2_BUILD" "0"
M2="$MUT/pkg-m2"
rm -rf "$M2"; mkdir -p "$M2/target/.uninstall"
# THE PAYLOAD IS STAGED FROM THE MUTATED BUILD, IN A CLEAN DIRECTORY, AFTER THE REBUILD.
#   T37's fixture copied $OUT/stage/.uninstall/payload -- the REFERENCE build staged
#   by the forward run. The uninstaller that then ran was the reference binary, so
#   the measured "preserved=1" was correct behaviour of the wrong program: M2 was
#   MUTATION-NOT-LINKED in the fixture, not uncaught in the product. Measured
#   2026-10-21: reference exe 49239329..., the old fixture's installed uninstaller
#   49239329..., the mutated build caab6897...; the same fixture staged from the
#   mutated build ran caab6897... and deleted the user file.
fresh_package "$MUT/stage-m2" || echo "  stage warning"
cp -r "$MUT/stage-m2/.uninstall/payload" "$M2/target/.uninstall/payload"
# The user's file is put there FIRST, before the install, because that is the only
# way it reaches the manifest: a file added after the install is not in the
# manifest at all, so no change to the state-word filter could ever delete it and
# the mutation would prove nothing.
echo 'user file the package never installed' > "$M2/target/user-notes.txt"
run_exe "$MUT/stage-m2/tf2_demo_native.exe" "$MUT/m2-install.txt" --install --install-dir "$(cygpath -w "$M2/target")" > /dev/null
M2_STATE="$(sed -n 's/^file=\([a-z]*\)\t.*user-notes.txt$/\1/p' "$M2/target/.uninstall/install-manifest.txt" | head -1)"
echo "  under M2, manifest recorded the user file as: $M2_STATE"
eq "the M2 fixture's manifest lists the user file as preserved (what the mutation targets)" "$M2_STATE" "preserved"
M2_SAVED_MANIFEST="$MUT/m2-manifest.txt"
cp "$M2/target/.uninstall/install-manifest.txt" "$M2_SAVED_MANIFEST" 2>/dev/null
# The uninstaller this fixture will run must be the mutated build, not the reference.
M2_LINKED=0
mutated_binary_identity "$M2/target/tf2_demo_native.exe" "M2 installed uninstaller" || M2_LINKED=$?
if [ "$M2_LINKED" = "90" ]; then mutation_not_linked "the M2 fixture would run the reference build (payload staged from the forward run)"; fi
run_exe "$M2/target/tf2_demo_native.exe" "$MUT/m2-uninstall.txt" --uninstall --install-dir "$(cygpath -w "$M2/target")" > /dev/null
echo "  under M2: $(cat "$MUT/m2-uninstall.txt" | tr '\n' ' ')"
MUTATIONS=$((MUTATIONS + 1))
# The mutated build must delete a file the reference build keeps. If the user file
# survives here it is not because the mutation failed to apply -- M2 applied, the
# rebuild succeeded, and the hash above says the fixture ran it -- but because
# something else protects it, and that would mean the mutation is not aimed at the
# reading this gate pins.
if [ ! -f "$M2/target/user-notes.txt" ]; then
  ok "M2 caught: the user's own file was deleted" "absent"
else
  wrong "M2 caught: the user's own file was deleted" \
    "still present; manifest said: $(sed -n 's/^file=\([a-z]*\).*user-notes.txt$/\1/p' "$M2_SAVED_MANIFEST" 2>/dev/null)" "absent"
fi
cp "$MUT/backup/install_lifecycle.cpp" "$SRC_LIFE"
if cmp -s "$SRC_LIFE" "$MUT/backup/install_lifecycle.cpp"; then ok "M2 source restored byte-identical" "yes"; else wrong "M2 source restored byte-identical" "no" "yes"; fi
rebuild m2-restore
eq "M2 restore build succeeded" "$?" "0"

# ---------------------------------------------------------- M3 manifest parse
sec "M3 manifest: the format line is not required, so any text file parses"
python - "$SRC_MAN" <<'PY'
import sys
path = sys.argv[1]
data = open(path, "rb").read()
old = b'  if (!sawFormat) return bad("this is not an install manifest");'
new = b'  if (false) return bad("this is not an install manifest");'
if old not in data:
    sys.exit("M3 anchor absent")
open(path, "wb").write(data.replace(old, new, 1))
print("  mutated: a file that is not a manifest parses as one")
PY
eq "M3 applied" "$?" "0"
M3_BUILD=0
rebuild m3-build || M3_BUILD=$?
eq "M3 build succeeded AND relinked" "$M3_BUILD" "0"
M3="$MUT/pkg-m3"
stage_package "$M3" m3-stage || echo "  stage warning"
M3_LINKED=0
mutated_binary_identity "$M3/tf2_demo_native.exe" "M3 staged binary" || M3_LINKED=$?
if [ "$M3_LINKED" = "90" ]; then mutation_not_linked "the M3 fixture would run the reference build"; fi
mkdir -p "$M3/.uninstall"
# A file with no format= line but otherwise well-formed: without the format check
# it parses as an install manifest with zero files, and uninstall returns 0 having
# "removed" nothing from a directory this package never installed into. A file of
# random text would be rejected by the record parser instead, which proves nothing
# about the format check.
printf 'package=some-other-package\npackageVersion=9.9.9\npackageVersionCode=999\nfiles=0\n' > "$M3/.uninstall/install-manifest.txt"
M3RC="$(run_exe "$M3/tf2_demo_native.exe" "$MUT/m3-uninstall.txt" --uninstall)"
echo "  under M3: exit=$M3RC reading=$(cat "$MUT/m3-uninstall.txt" | tr '\n' ' ')"
MUTATIONS=$((MUTATIONS + 1))
if [ "$M3RC" = "0" ]; then
  ok "M3 caught: a foreign file was accepted as an install manifest" "exit=0"
else
  wrong "M3 caught: a foreign file was accepted as an install manifest" "exit=$M3RC" "exit=0"
fi
cp "$MUT/backup/install_manifest.cpp" "$SRC_MAN"
if cmp -s "$SRC_MAN" "$MUT/backup/install_manifest.cpp"; then ok "M3 source restored byte-identical" "yes"; else wrong "M3 source restored byte-identical" "no" "yes"; fi
rebuild m3-restore
eq "M3 restore build succeeded" "$?" "0"

sec "mutation verdict"
for pair in "main.cpp|$SRC_MAIN" "install_lifecycle.cpp|$SRC_LIFE" "install_manifest.cpp|$SRC_MAN"; do
  name="${pair%%|*}"; path="${pair#*|}"; backup="$MUT/backup/$name"
  if cmp -s "$path" "$backup"; then ok "restored byte-identical: $name" "yes"; else wrong "restored byte-identical: $name" "no" "yes"; fi
done
echo
echo "  cases=3 checks=$CHECKS mutations=$MUTATIONS"
if [ "$FAIL" -eq 0 ] && [ "$MUTATIONS" -eq 3 ]; then
  echo "  INSTALL-LIFECYCLE-MUTATION=PASS (3 mutations caught, sources restored)"
  exit 0
fi
echo "  INSTALL-LIFECYCLE-MUTATION=FAIL fail=$FAIL mutations=$MUTATIONS"
exit 1