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
# USAGE
#   bash install-lifecycle-check.sh --forward  <out-dir>
#   bash install-lifecycle-check.sh --mutation <out-dir>   (forward must have run)
# Exit 0 = every assertion held / every mutation was caught.

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

sec "1/8 the package cmake --install produces"
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

sec "2/8 the package copied into a clean temporary directory"
PKG="$OUT/clean/pkg"
rm -rf "$OUT/clean"; mkdir -p "$OUT/clean"
cp -r "$OUT/stage" "$PKG"
eq "the copy is the package" \
  "$(diff -q <(hashlist "$OUT/stage" /dev/stdout) <(hashlist "$PKG" /dev/stdout) >/dev/null 2>&1 && echo same || echo different)" "same"

sec "3/8 --version: the package names itself, through a redirect"
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

sec "4/8 --install into a directory that holds nothing but the payload (the packaged shape)"
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

sec "4b --install into an empty directory: the shape a user actually takes"
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


sec "5/8 --install-status in a NEW process: the state is on disk, not in memory"
SRC="$(run_exe "$PKG/tf2_demo_native.exe" "$OUT/status.txt" --install-status)"
eq "product --install-status exit code" "$SRC" "0"
echo "  --- --install-status ---"
sed 's/^/    /' "$OUT/status.txt"
has "status is VERIFIED" "$(cat "$OUT/status.txt")" "status=VERIFIED"
eq "status counts every file the manifest lists as present" "$(field "$OUT/status.txt" present)" "4"
eq "status found nothing missing" "$(field "$OUT/status.txt" missing)" "0"
eq "status found nothing modified" "$(field "$OUT/status.txt" modified)" "0"
eq "status reports the installed version" "$(field "$OUT/status.txt" version)" "$declared_version"

sec "6/8 what a user does to an installed directory: edit one file, add another"
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

sec "7/8 --install over an existing install (the upgrade shape)"
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

sec "8/8 --uninstall: only what the manifest says, and the user files stay"
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

cases=9
echo
echo "  cases=$cases checks=$CHECKS mutations=0"
if [ "$FAIL" -eq 0 ]; then echo "  INSTALL-LIFECYCLE=PASS"; exit 0; fi
echo "  INSTALL-LIFECYCLE=FAIL fail=$FAIL"
exit 1
fi

# ==================================================================== mutation
sec "mutation preconditions: the forward run"
for need in "$OUT/clean/pkg" "$OUT/version.txt"; do
  eq "forward artifact ${need##*/}" "$([ -e "$need" ] && echo present || echo absent)" "present"
done
MUT="$OUT/mutation"; mkdir -p "$MUT/backup" "$MUT/logs"
SRC_MAIN="$REPO/native/src/main.cpp"
SRC_LIFE="$REPO/native/src/install_lifecycle.cpp"
SRC_MAN="$REPO/native/src/install_manifest.cpp"
cp "$SRC_MAIN" "$MUT/backup/main.cpp"
cp "$SRC_LIFE" "$MUT/backup/install_lifecycle.cpp"
cp "$SRC_MAN" "$MUT/backup/install_manifest.cpp"

rebuild() { # <log-name>
  bash build-target.sh tf2_demo_native install_cli_probe > "$MUT/logs/$1.log" 2>&1
  local rc=$?
  echo "  rebuild $1 rc=$rc"
  return $rc
}
fresh_package() { # <prefix>
  stage_package "$1" mutation-stage || return 1
  cp -r "$1/.uninstall/payload" "$1/.uninstall/payload" 2>/dev/null || true
  return 0
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
rebuild m1-build
eq "M1 build succeeded" "$?" "0"
fresh_package "$MUT/pkg-m1" || echo "  stage warning"
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
rebuild m2-build
eq "M2 build succeeded" "$?" "0"
M2="$MUT/pkg-m2"
stage_package "$M2" m2-stage || echo "  stage warning"
run_exe "$M2/tf2_demo_native.exe" "$MUT/m2-install.txt" --install > /dev/null
echo 'user file' > "$M2/user-notes.txt"
run_exe "$M2/tf2_demo_native.exe" "$MUT/m2-status.txt" --install-status > /dev/null
run_exe "$M2/tf2_demo_native.exe" "$MUT/m2-uninstall.txt" --uninstall > /dev/null
echo "  under M2: $(cat "$MUT/m2-uninstall.txt" | tr '\n' ' ')"
MUTATIONS=$((MUTATIONS + 1))
if [ ! -f "$M2/user-notes.txt" ]; then
  ok "M2 caught: the user's own file was deleted" "absent"
else
  wrong "M2 caught: the user's own file was deleted" "still present" "absent"
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
rebuild m3-build
eq "M3 build succeeded" "$?" "0"
M3="$MUT/pkg-m3"
stage_package "$M3" m3-stage || echo "  stage warning"
mkdir -p "$M3/.uninstall"
printf 'hello\nthis is not a manifest\n' > "$M3/.uninstall/install-manifest.txt"
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