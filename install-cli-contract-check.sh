#!/usr/bin/env bash
# install-cli-contract-check.sh -- T92 (2026-11-07): the two command line
# contracts the installer lifecycle got wrong, measured on the shipped binary.
#
# WHY THIS GATE EXISTS
#   install-lifecycle-check.sh drives --install/--uninstall/--install-status/
#   --version end to end and never passes --install-dry-run, so two defects sat
#   under 205 green assertions:
#
#   1. --install-dry-run ALONE OPENED THE PRODUCT WINDOW.
#      parseLifecycleRequest set options.dryRun and nothing else, so with no
#      other mode on the command line mode stayed None, the parser returned
#      false, and main.cpp fell through to the normal GUI start. Measured
#      2026-11-07 on the frozen build: a real window titled "TF2 Demo Player",
#      no install=DRYRUN line, self-exit with code 1 after 67.6 s.
#      The flag now selects Install as well, and the second half of the contract
#      matters as much as the first: a dry run BESIDE another mode
#      (--uninstall --install-dry-run, --install-status --install-dry-run) asks
#      two different questions and is refused with exit 2 by name, instead of
#      silently answering one of them.
#
#   2. THE MANIFEST RECORDED ITS INSTALL DIRECTORY IN THE ANSI CODE PAGE.
#      install_lifecycle.cpp wrote installDir.generic_string(), which on MSVC is
#      ACTIVE CODE PAGE bytes -- cp936 on this machine -- while the format and
#      every external reader say UTF-8. A Chinese install path therefore came
#      back as mojibake in any tool that was not the product (the product never
#      reads the field back, which is why nothing caught it). The write is
#      u8string() now, and the read side accepts both: a value that is not valid
#      UTF-8 is re-read as ANSI and announced as
#      "manifest: legacy-acp installDirectory", so a directory installed by an
#      older build still works and the conversion is visible in the log.
#
# WHAT THIS GATE ASSERTS (five groups)
#   A1  a lone --install-dry-run exits 0 inside 5 s, prints install=DRYRUN with
#       the payload count, names itself ("install: dryrun mode=install dir=...
#       plan=N files"), and leaves the directory byte-for-byte unchanged
#   A1b --install --install-dry-run plans the same payload as the lone spelling,
#       so the flag does the same thing wherever it sits on the command line
#   A5  the Chinese-path directory uninstalls after that, legacy manifest and
#       all: exit 0, uninstall=PASS, at least one file removed, manifest gone
#   A2  --uninstall --install-dry-run and --install-status --install-dry-run are
#       refused with exit 2 (reason=dry-run-conflict, mode named) and change
#       nothing
#   A3  an install into a path holding Chinese characters records the path as
#       valid UTF-8, equal to the path, and NOT as ANSI bytes read as UTF-8
#   A4  a manifest whose value IS ANSI bytes (the pre-2026-11-07 shape, built
#       here by rewriting the line the old build wrote) still reads back:
#       --install-status stays VERIFIED and the ANSI fallback is announced; a
#       UTF-8 manifest is not announced as legacy (control)
#
# MUTATIONS (--mutation, run after --forward; evidence/<...>/mutation)
#   M1  the line that makes a lone dry run select Install is removed -> A1 must
#       go red (the product starts its GUI instead of answering)
#   M2  u8string() is put back to generic_string() -> A3 must go red on the
#       Chinese path, which is the only shape where the difference is visible
#   Each mutation rebuilds through build-target.sh, and the binary the fixture
#   then runs is hashed: a fixture that would run the reference build is
#   MUTATION-NOT-LINKED and exits 90, the meaning install-lifecycle-check.sh
#   uses. Sources are restored byte-identical and rebuilt before the gate exits.
#
# USAGE
#   bash install-cli-contract-check.sh --forward  <out-dir>
#   bash install-cli-contract-check.sh --mutation <out-dir>   (forward first)
# Exit 0 = every assertion held / every mutation was caught; 1 = a red reading;
# 2 = usage; 90 = MUTATION-NOT-LINKED.
set -uo pipefail

MODE="--forward"
OUT_ARG=""
if [ "$#" -ge 1 ]; then MODE="$1"; fi
if [ "$#" -ge 2 ]; then OUT_ARG="$2"; fi
if [ "$MODE" != "--forward" ] && [ "$MODE" != "--mutation" ]; then
  echo "usage: $0 --forward|--mutation <out-dir>"; exit 2
fi
if [ -z "$OUT_ARG" ]; then echo "usage: $0 $MODE <out-dir>"; exit 2; fi

REPO="$(cd "$(dirname "$0")" && pwd)"
cd "$REPO" || exit 2
mkdir -p "$OUT_ARG"
OUT="$(cd "$OUT_ARG" && pwd)"
mkdir -p "$OUT/logs" "$OUT/cases"

# HERMETIC SETTINGS -- A GATE PRECONDITION, NOT HYGIENE (D-22).
#   The product writes its settings and its UI state on exit unconditionally,
#   whichever lifecycle mode ran, and it writes the REAL
#   %LOCALAPPDATA%\TF2 Demo Player\settings.ini and ui.ini unless BOTH paths are
#   redirected. verify-release.sh carries a machine guard that red-lights the
#   whole chain when those files move, so every product launch in this gate
#   passes --settings-file AND --ui-settings-file into a directory made here and
#   removed on exit. The trap below refuses to replace an existing EXIT trap.
HERM_ROOT="$(mktemp -d "${TMPDIR:-/tmp}/t92-cli-gate.XXXXXX")" || exit 2
HERM_SET="$HERM_ROOT/settings.ini"; HERM_UI="$HERM_ROOT/ui.ini"
: > "$HERM_SET"; : > "$HERM_UI"
cleanup_hermetic() { rm -rf "$HERM_ROOT"; }
if [ -z "$(trap -p EXIT)" ]; then trap cleanup_hermetic EXIT; fi
echo "hermetic settings: $HERM_ROOT"

BUILD="$REPO/native/build-nmake"
EXE="$BUILD/tf2_demo_native.exe"
PROBE="$BUILD/install_cli_probe.exe"

CHECKS=0; FAIL=0; MUTATIONS=0
sec() { echo; echo "=== $1 ==="; }
ok()   { echo "  OK   $1 = $2"; CHECKS=$((CHECKS + 1)); }
wrong(){ echo "  FAIL $1 = $2, expected $3"; CHECKS=$((CHECKS + 1)); FAIL=$((FAIL + 1)); }
eq()   { if [ "$2" = "$3" ]; then ok "$1" "$2"; else wrong "$1" "$2" "$3"; fi; }
has()  { case "$2" in *"$3"*) ok "$1" "yes";; *) wrong "$1" "no" "contains $3";; esac; }
hasnt(){ case "$2" in *"$3"*) wrong "$1" "yes" "absent";; *) ok "$1" "absent";; esac; }
inventory() { ( cd "$1" && find . -type f -printf "%P\t%s\n" | LC_ALL=C sort ) > "$2"; }

stage_package() { # <prefix> <log-name>
  local prefix="$1" name="$2" rc=0
  rm -rf "$prefix"
  cmake --install "$(cygpath -w "$BUILD")" --config Release --prefix "$(cygpath -w "$prefix")" \
    > "$OUT/logs/$name.log" 2>&1 || rc=$?
  return $rc
}

# ONE PRODUCT ANSWER, WITH A DEADLINE.
#   A WIN32 GUI subsystem process whose lifecycle flags were not recognised
#   starts the window and never returns -- which is exactly the state M1 puts the
#   product back into. Every invocation is therefore capped, and the elapsed time
#   is written beside the answer: "answered as a dry run" is a claim about both
#   the text and the time, and A1 pins the time at 5 s.
RUN_LIMIT=45
run_exe() { # <exe> <out-file> <ms-file> <args...> -> exit code
  local exe="$1" out="$2" msfile="$3"; shift 3
  local t0 t1 rc
  t0="$(date +%s%N)"
  timeout -k 5 "$RUN_LIMIT" "$exe" "$@" > "$out" 2>&1
  rc=$?
  t1="$(date +%s%N)"
  echo $(( (t1 - t0) / 1000000 )) > "$msfile"
  echo $rc
}

HELPER="$OUT/logs/install-dir-bytes.py"
cat > "$HELPER" <<'PY'
# Gate helper: reports which bytes the manifest's installDirectory line holds,
# without guessing and without printing the raw (possibly non-ASCII) value.
import sys

def value_bytes(path):
    data = open(path, 'rb').read()
    for line in data.split(b'\n'):
        if line.startswith(b'installDirectory='):
            return line[len(b'installDirectory='):]
    return None

def valid_utf8(raw):
    try:
        raw.decode('utf-8')
        return True
    except UnicodeDecodeError:
        return False

mode = sys.argv[1]
path = sys.argv[2]
value = value_bytes(path)
if value is None:
    print('line=absent')
    sys.exit(0)
print('bytes=' + str(len(value)))
print('hex=' + value.hex())
print('valid_utf8=' + ('yes' if valid_utf8(value) else 'no'))
if mode == 'check':
    expected = sys.argv[3].replace(chr(92), '/')
    # A Windows path is recorded with either separator depending on how it was
    # given (the product keeps whatever --install-dir held, and this gate passes
    # cygpath -w, which is backslashes). Compare separators-normalized, so the
    # reading is about encoding, not about who wrote the slash.
    def normalized(text): return text.replace(chr(92), '/')
    print('expected_hex=' + expected.encode('utf-8').hex())
    print('equals_utf8=' + ('yes' if normalized(value.decode('utf-8', 'replace')) == expected else 'no'))
    print('cp936_reads_as_path=' + ('yes' if normalized(value.decode('cp936', 'replace')) == expected else 'no'))
elif mode == 'legacy':
    text = value.decode('utf-8')
    legacy = text.encode('cp936')
    data = open(path, 'rb').read()
    data = data.replace(b'installDirectory=' + value, b'installDirectory=' + legacy, 1)
    open(path, 'wb').write(data)
    print('rewritten_to_cp936=' + ('yes' if legacy != value else 'no'))
PY

# ===================================================================== forward
if [ "$MODE" = "--forward" ]; then

sec "0/5 the build the gate is about to drive"
# The stage step runs `cmake --install`, and that install rule copies
# native_install_smoke_probe.exe out of the build tree. install-lifecycle-check.sh
# assumes a tree where someone already built it; in a fresh worktree it is
# missing, `cmake --install` stops after the product exe, and the stage carries
# no payload -- which then reads as a bare "the staged package carries a payload
# = no" three sections later. The gate therefore builds what it is about to stage.
BUILD_RC=0
bash build-target.sh tf2_demo_native install_cli_probe native_install_smoke_probe \
  > "$OUT/logs/build.log" 2>&1 || BUILD_RC=$?
eq "build rc (product, install probe, installer smoke probe)" "$BUILD_RC" "0"
if [ "$BUILD_RC" != "0" ]; then tail -8 "$OUT/logs/build.log" | sed 's/^/    /'; fi
eq "the product was built" "$([ -f "$EXE" ] && echo yes || echo no)" "yes"
eq "the install probe was built" "$([ -f "$PROBE" ] && echo yes || echo no)" "yes"
eq "the installer smoke probe was built" "$([ -f "$BUILD/native_install_smoke_probe.exe" ] && echo yes || echo no)" "yes"
sha256sum "$EXE" > "$OUT/reference-exe.sha256"
sha256sum "$PROBE" > "$OUT/reference-probe.sha256"
echo "  product exe sha256=$(cut -d' ' -f1 "$OUT/reference-exe.sha256")"
echo "  probe   exe sha256=$(cut -d' ' -f1 "$OUT/reference-probe.sha256")"

sec "1/5 A1 --install-dry-run alone: a dry run, not a product launch"
A1="$OUT/cases/a1"
ST1_RC=0; stage_package "$A1" a1-stage || ST1_RC=$?
eq "A1 the package staged" "$ST1_RC" "0"
if [ "$ST1_RC" != "0" ]; then
  echo "  A1 cmake --install said:"; tail -8 "$OUT/logs/a1-stage.log" | sed 's/^/    /'
  echo "  A1 what the failed stage holds:"
  ( cd "$A1" 2>/dev/null && find . -type f -printf '%P\t%s\n' | LC_ALL=C sort ) > "$OUT/logs/a1-stage-inventory.txt"
  sed 's/^/    /' "$OUT/logs/a1-stage-inventory.txt"
fi
eq "A1 the staged package carries a payload" "$([ -d "$A1/.uninstall/payload" ] && echo yes || echo no)" "yes"
inventory "$A1" "$OUT/logs/a1-before.txt"
A1_RC="$(run_exe "$A1/tf2_demo_native.exe" "$OUT/logs/a1-out.txt" "$OUT/logs/a1.ms" \
  --install-dry-run --install-dir "$(cygpath -w "$A1")" \
  --settings-file "$(cygpath -w "$HERM_SET")" \
  --ui-settings-file "$(cygpath -w "$HERM_UI")")"
A1_MS="$(cat "$OUT/logs/a1.ms")"
A1_OUT="$(cat "$OUT/logs/a1-out.txt")"
echo "  A1 exit=$A1_RC elapsed_ms=$A1_MS"
echo "$A1_OUT" | sed 's/^/    /'
eq "A1 exit code" "$A1_RC" "0"
has "A1 the answer is a dry run" "$A1_OUT" "install=DRYRUN"
has "A1 the plan lists the payload count" "$A1_OUT" "files=4"
inventory "$A1" "$OUT/logs/a1-after.txt"
eq "A1 the directory did not change" \
  "$(cmp -s "$OUT/logs/a1-before.txt" "$OUT/logs/a1-after.txt" && echo same || echo changed)" "same"
eq "A1 no manifest was written" \
  "$([ -f "$A1/.uninstall/install-manifest.txt" ] && echo present || echo absent)" "absent"
if [ "$A1_MS" -lt 5000 ] 2>/dev/null; then
  ok "A1 it answered inside 5 s" "$A1_MS"
else
  wrong "A1 it answered inside 5 s" "$A1_MS" "under 5000"
fi

# A1b -- the two legal spellings must agree. "install --install-dry-run" is the
# documented form; a lone "--install-dry-run" is the one that used to start the
# GUI. Both must produce the same plan token, or the flag is still doing two
# different things depending on where it sits.
A1B="$OUT/cases/a1b"
STB_RC=0; stage_package "$A1B" a1b-stage || STB_RC=$?
eq "A1b the package staged" "$STB_RC" "0"
inventory "$A1B" "$OUT/logs/a1b-before.txt"
A1B_RC="$(run_exe "$A1B/tf2_demo_native.exe" "$OUT/logs/a1b-out.txt" "$OUT/logs/a1b.ms" \
  --install --install-dry-run --install-dir "$(cygpath -w "$A1B")" \
  --settings-file "$(cygpath -w "$HERM_SET")" \
  --ui-settings-file "$(cygpath -w "$HERM_UI")")"
A1B_OUT="$(cat "$OUT/logs/a1b-out.txt")"
echo "  A1b exit=$A1B_RC"
echo "$A1B_OUT" | sed 's/^/    /'
eq "A1b exit code" "$A1B_RC" "0"
has "A1b the answer is a dry run" "$A1B_OUT" "install=DRYRUN"
has "A1b the answer names itself" "$A1B_OUT" "install: dryrun mode=install dir="
inventory "$A1B" "$OUT/logs/a1b-after.txt"
eq "A1b the directory did not change" \
  "$(cmp -s "$OUT/logs/a1b-before.txt" "$OUT/logs/a1b-after.txt" && echo same || echo changed)" "same"
A1_FILES="$(sed -n 's/.*install=DRYRUN files=\([0-9][0-9]*\).*/\1/p' <<< "$A1_OUT" | head -1)"
A1B_FILES="$(sed -n 's/.*install=DRYRUN files=\([0-9][0-9]*\).*/\1/p' <<< "$A1B_OUT" | head -1)"
if [ -z "$A1_FILES" ]; then A1_FILES="none"; fi
eq "A1b both spellings plan the same payload" "$A1B_FILES" "$A1_FILES"

sec "2/5 A3 an install into a Chinese path records it as UTF-8"
CJK="$OUT/cases/验收 路径 空格/安装 目录"
ST3_RC=0; stage_package "$CJK" a3-stage || ST3_RC=$?
eq "A3 the package staged" "$ST3_RC" "0"
if [ "$ST3_RC" != "0" ]; then
  echo "  A3 cmake --install said:"; tail -8 "$OUT/logs/a3-stage.log" | sed 's/^/    /'
  echo "  A3 what the failed stage holds:"
  ( cd "$CJK" 2>/dev/null && find . -type f -printf '%P\t%s\n' | LC_ALL=C sort ) > "$OUT/logs/a3-stage-inventory.txt"
  sed 's/^/    /' "$OUT/logs/a3-stage-inventory.txt"
fi
A3_RC="$(run_exe "$CJK/tf2_demo_native.exe" "$OUT/logs/a3-install.txt" "$OUT/logs/a3.ms" \
  --install --install-dir "$(cygpath -w "$CJK")" \
  --settings-file "$(cygpath -w "$HERM_SET")" \
  --ui-settings-file "$(cygpath -w "$HERM_UI")")"
A3_OUT="$(cat "$OUT/logs/a3-install.txt")"
echo "  A3 exit=$A3_RC"
echo "$A3_OUT" | sed 's/^/    /'
eq "A3 install exit code" "$A3_RC" "0"
has "A3 the install succeeded" "$A3_OUT" "install=PASS"
has "A3 the payload was laid down" "$A3_OUT" "files=5"
MAN="$CJK/.uninstall/install-manifest.txt"
eq "A3 the manifest was written" "$([ -f "$MAN" ] && echo yes || echo no)" "yes"
A3_EXPECT="$(cygpath -w "$CJK")"
python "$HELPER" check "$MAN" "$A3_EXPECT" > "$OUT/logs/a3-bytes.txt" 2>&1
A3_BYTES="$(cat "$OUT/logs/a3-bytes.txt")"
echo "$A3_BYTES" | sed 's/^/    /'
has "A3 the recorded value is valid UTF-8" "$A3_BYTES" "valid_utf8=yes"
has "A3 the recorded value is the installed path" "$A3_BYTES" "equals_utf8=yes"
hasnt "A3 it is not ANSI bytes that only look like the path" "$A3_BYTES" "cp936_reads_as_path=yes"

sec "3/5 A2 a dry run beside another mode is refused and touches nothing"
inventory "$CJK" "$OUT/logs/a2-before.txt"
A2_RC="$(run_exe "$CJK/tf2_demo_native.exe" "$OUT/logs/a2-uninstall.txt" "$OUT/logs/a2.ms" \
  --uninstall --install-dry-run --install-dir "$(cygpath -w "$CJK")" \
  --settings-file "$(cygpath -w "$HERM_SET")" \
  --ui-settings-file "$(cygpath -w "$HERM_UI")")"
A2_OUT="$(cat "$OUT/logs/a2-uninstall.txt")"
echo "  A2 --uninstall --install-dry-run exit=$A2_RC"
echo "$A2_OUT" | sed 's/^/    /'
eq "A2 exit code" "$A2_RC" "2"
has "A2 the refusal names the reason" "$A2_OUT" "reason=dry-run-conflict"
has "A2 the refusal names the mode it was given" "$A2_OUT" "mode=uninstall"
inventory "$CJK" "$OUT/logs/a2-after.txt"
eq "A2 nothing was uninstalled" \
  "$(cmp -s "$OUT/logs/a2-before.txt" "$OUT/logs/a2-after.txt" && echo same || echo changed)" "same"
A2B_RC="$(run_exe "$CJK/tf2_demo_native.exe" "$OUT/logs/a2b-status.txt" "$OUT/logs/a2b.ms" \
  --install-status --install-dry-run --install-dir "$(cygpath -w "$CJK")" \
  --settings-file "$(cygpath -w "$HERM_SET")" \
  --ui-settings-file "$(cygpath -w "$HERM_UI")")"
A2B_OUT="$(cat "$OUT/logs/a2b-status.txt")"
echo "  A2b --install-status --install-dry-run exit=$A2B_RC"
echo "$A2B_OUT" | sed 's/^/    /'
eq "A2b exit code" "$A2B_RC" "2"
has "A2b the refusal names the mode it was given" "$A2B_OUT" "mode=install-status"
hasnt "A2b no status answer was produced" "$A2B_OUT" "status=VERIFIED"

sec "4/5 A4 a manifest written in the ANSI code page still reads back"
S1_RC="$(run_exe "$CJK/tf2_demo_native.exe" "$OUT/logs/a4-before.txt" "$OUT/logs/a4.ms" \
  --install-status --install-dir "$(cygpath -w "$CJK")" \
  --settings-file "$(cygpath -w "$HERM_SET")" \
  --ui-settings-file "$(cygpath -w "$HERM_UI")")"
S1_OUT="$(cat "$OUT/logs/a4-before.txt")"
echo "  A4 control exit=$S1_RC"
echo "$S1_OUT" | sed 's/^/    /'
eq "A4 control: status exit code" "$S1_RC" "0"
has "A4 control: status is VERIFIED" "$S1_OUT" "status=VERIFIED"
hasnt "A4 control: a UTF-8 manifest is not announced as legacy" "$S1_OUT" "manifest: legacy-acp installDirectory"
python "$HELPER" legacy "$MAN" > "$OUT/logs/a4-rewrite.txt" 2>&1
A4_REW="$(cat "$OUT/logs/a4-rewrite.txt")"
echo "  $A4_REW"
has "A4 the fixture was rewritten to ANSI bytes" "$A4_REW" "rewritten_to_cp936=yes"
python "$HELPER" check "$MAN" "$A3_EXPECT" > "$OUT/logs/a4-bytes.txt" 2>&1
A4_BYTES="$(cat "$OUT/logs/a4-bytes.txt")"
echo "$A4_BYTES" | sed 's/^/    /'
hasnt "A4 the fixture really is not valid UTF-8" "$A4_BYTES" "valid_utf8=yes"
S2_RC="$(run_exe "$CJK/tf2_demo_native.exe" "$OUT/logs/a4-after.txt" "$OUT/logs/a4.ms" \
  --install-status --install-dir "$(cygpath -w "$CJK")" \
  --settings-file "$(cygpath -w "$HERM_SET")" \
  --ui-settings-file "$(cygpath -w "$HERM_UI")")"
S2_OUT="$(cat "$OUT/logs/a4-after.txt")"
echo "  A4 legacy manifest exit=$S2_RC"
echo "$S2_OUT" | sed 's/^/    /'
eq "A4 legacy status exit code" "$S2_RC" "0"
has "A4 legacy status is still VERIFIED" "$S2_OUT" "status=VERIFIED"
has "A4 the ANSI fallback is announced" "$S2_OUT" "manifest: legacy-acp installDirectory"

sec "5/5 A5 the Chinese-path install uninstalls, legacy manifest and all"
A5_RC="$(run_exe "$CJK/tf2_demo_native.exe" "$OUT/logs/a5-uninstall.txt" "$OUT/logs/a5.ms" \
  --uninstall --install-dir "$(cygpath -w "$CJK")" \
  --settings-file "$(cygpath -w "$HERM_SET")" \
  --ui-settings-file "$(cygpath -w "$HERM_UI")")"
A5_OUT="$(cat "$OUT/logs/a5-uninstall.txt")"
echo "  A5 exit=$A5_RC"
echo "$A5_OUT" | sed 's/^/    /'
eq "A5 uninstall exit code" "$A5_RC" "0"
has "A5 the uninstall succeeded" "$A5_OUT" "uninstall=PASS"
A5_REMOVED="$(sed -n 's/.*removed=\([0-9][0-9]*\).*/\1/p' <<< "$A5_OUT" | head -1)"
if [ -z "$A5_REMOVED" ]; then A5_REMOVED="-1"; fi
if [ "$A5_REMOVED" -ge 1 ] 2>/dev/null; then
  ok "A5 it removed at least one file" "$A5_REMOVED"
else
  wrong "A5 it removed at least one file" "$A5_REMOVED" ">= 1"
fi
eq "A5 the manifest is gone" \
  "$([ -f "$MAN" ] && echo present || echo absent)" "absent"

echo
echo "  cases=5 checks=$CHECKS fail=$FAIL exe_sha256=$(cut -d' ' -f1 "$OUT/reference-exe.sha256")"
if [ "$FAIL" -eq 0 ]; then echo "  INSTALL-CLI-CONTRACT=PASS assertions=$CHECKS"; exit 0; fi
echo "  INSTALL-CLI-CONTRACT=FAIL assertions=$CHECKS"; exit 1
fi

# ==================================================================== mutation
sec "mutation preconditions: the forward run"
for need in "$OUT/reference-exe.sha256" "$OUT/logs/a1-out.txt"; do
  eq "forward artifact $need" "$([ -e "$need" ] && echo present || echo absent)" "present"
done
REFERENCE_EXE_SHA="$(cut -d' ' -f1 "$OUT/reference-exe.sha256" 2>/dev/null)"
MUTATED_EXE_SHA=""
echo "  reference exe sha256=$REFERENCE_EXE_SHA"
MUT="$OUT/mutation"; mkdir -p "$MUT/backup" "$MUT/logs" "$MUT/cases"
SRC_LC="$REPO/native/src/main_lifecycle.cpp"
SRC_LIFE="$REPO/native/src/install_lifecycle.cpp"
cp "$SRC_LC" "$MUT/backup/main_lifecycle.cpp"
cp "$SRC_LIFE" "$MUT/backup/install_lifecycle.cpp"

rebuild() { # <log-name>
  local before after rc=0
  before="$(sha256sum "$EXE" 2>/dev/null | cut -d' ' -f1)"
  bash build-target.sh tf2_demo_native install_cli_probe native_install_smoke_probe \
    > "$MUT/logs/$1.log" 2>&1 || rc=$?
  after="$(sha256sum "$EXE" 2>/dev/null | cut -d' ' -f1)"
  MUTATED_EXE_SHA="$after"
  echo "  rebuild $1 rc=$rc reference=$REFERENCE_EXE_SHA exe_before=$before exe_after=$after"
  if [ "$rc" = "0" ] && [ "$before" = "$after" ]; then
    echo "  MUTATION-NOT-LINKED: the exe is byte-identical after a supposedly mutated rebuild"
    tail -12 "$MUT/logs/$1.log" | sed 's/^/    /'
    return 90
  fi
  return $rc
}

mutated_binary_identity() { # <exe> <label>
  local sha
  sha="$(sha256sum "$1" 2>/dev/null | cut -d' ' -f1)"
  if [ -z "$sha" ]; then wrong "$2 exists" "absent" "present"; return 1; fi
  if [ "$sha" = "$MUTATED_EXE_SHA" ]; then ok "$2 is the binary the mutation built" "$sha"; return 0; fi
  if [ -n "$REFERENCE_EXE_SHA" ] && [ "$sha" = "$REFERENCE_EXE_SHA" ]; then
    echo "  MUTATION-NOT-LINKED: $2 is the REFERENCE binary, not the mutated one"
    return 90
  fi
  wrong "$2 is the binary the mutation built" "$sha" "$MUTATED_EXE_SHA"
  return 1
}

mutation_not_linked() {
  echo "  MUTATION-NOT-LINKED: $1"
  echo "  checks=$CHECKS mutations=$MUTATIONS"
  echo "  INSTALL-CLI-CONTRACT-MUTATION=MUTATION-NOT-LINKED"
  exit 90
}

# ------------------------------------------------------- M1 dry run is a mode
sec "M1 dry run: the flag stops selecting Install, so a lone one starts the GUI"
python - "$SRC_LC" <<'PY'
import sys
path = sys.argv[1]
data = open(path, 'rb').read()
old = b"    request->mode = LifecycleMode::Install;"
new = b"    (void)0; /* mutated: a lone --install-dry-run no longer selects Install */"
if old not in data:
    sys.exit("M1 anchor absent")
open(path, 'wb').write(data.replace(old, new, 1))
print("  mutated: a lone --install-dry-run leaves the mode at None")
PY
eq "M1 applied" "$?" "0"
M1_BUILD=0
rebuild m1-build || M1_BUILD=$?
eq "M1 build succeeded AND relinked" "$M1_BUILD" "0"
M1="$MUT/cases/m1"
STM1_RC=0; stage_package "$M1" m1-stage || STM1_RC=$?
eq "M1 the package staged" "$STM1_RC" "0"
if [ "$STM1_RC" != "0" ]; then
  echo "  M1 cmake --install said:"; tail -8 "$MUT/logs/m1-stage.log" | sed 's/^/    /'
  echo "  M1 what the failed stage holds:"
  ( cd "$M1" 2>/dev/null && find . -type f -printf '%P\t%s\n' | LC_ALL=C sort ) > "$MUT/logs/m1-stage-inventory.txt"
  sed 's/^/    /' "$MUT/logs/m1-stage-inventory.txt"
fi
M1_LINKED=0
mutated_binary_identity "$M1/tf2_demo_native.exe" "M1 fixture binary" || M1_LINKED=$?
if [ "$M1_LINKED" = "90" ]; then mutation_not_linked "the M1 fixture would run the reference build"; fi
inventory "$M1" "$MUT/logs/m1-before.txt"
M1_RC="$(run_exe "$M1/tf2_demo_native.exe" "$MUT/logs/m1-out.txt" "$MUT/logs/m1.ms" \
  --install-dry-run --install-dir "$(cygpath -w "$M1")" \
  --settings-file "$(cygpath -w "$HERM_SET")" \
  --ui-settings-file "$(cygpath -w "$HERM_UI")")"
M1_MS="$(cat "$MUT/logs/m1.ms")"
M1_OUT="$(cat "$MUT/logs/m1-out.txt")"
inventory "$M1" "$MUT/logs/m1-after.txt"
echo "  under M1: exit=$M1_RC elapsed_ms=$M1_MS bytes=$(wc -c < "$MUT/logs/m1-out.txt" | tr -d ' ')"
echo "  under M1 output: $(echo "$M1_OUT" | tr '\n' ' ')"
STRAY=0
sleep 1
STRAY="$(tasklist //fi "IMAGENAME eq tf2_demo_native.exe" //nh 2>/dev/null | grep -c tf2_demo_native)"
if [ "$STRAY" -gt 0 ]; then taskkill //f //im tf2_demo_native.exe > /dev/null 2>&1; fi
echo "  products still running after the deadline: $STRAY (terminated by the gate when non-zero)"
MUTATIONS=$((MUTATIONS + 1))
if [ "$M1_RC" != "0" ] || ! printf '%s' "$M1_OUT" | grep -q "install=DRYRUN"; then
  ok "M1 caught: no dry-run answer inside the deadline" "exit=$M1_RC elapsed_ms=$M1_MS"
else
  wrong "M1 caught: no dry-run answer inside the deadline" "exit=$M1_RC install=DRYRUN present" "a GUI start or a timeout"
fi
cp "$MUT/backup/main_lifecycle.cpp" "$SRC_LC"
if cmp -s "$SRC_LC" "$MUT/backup/main_lifecycle.cpp"; then ok "M1 source restored byte-identical" "yes"; else wrong "M1 source restored byte-identical" "no" "yes"; fi
rebuild m1-restore
eq "M1 restore build succeeded" "$?" "0"

# ------------------------------------------------- M2 the manifest goes ANSI
sec "M2 manifest: the install directory is written in the ANSI code page again"
python - "$SRC_LIFE" <<'PY'
import sys
path = sys.argv[1]
data = open(path, 'rb').read()
old = b"  manifest.installDirectory = installDir.u8string();"
new = b"  manifest.installDirectory = installDir.generic_string();"
if old not in data:
    sys.exit("M2 anchor absent")
open(path, 'wb').write(data.replace(old, new, 1))
print("  mutated: the install directory is recorded with generic_string() again")
PY
eq "M2 applied" "$?" "0"
M2_BUILD=0
rebuild m2-build || M2_BUILD=$?
eq "M2 build succeeded AND relinked" "$M2_BUILD" "0"
M2DIR="$MUT/cases/验收 路径 空格/安装 目录"
STM2_RC=0; stage_package "$M2DIR" m2-stage || STM2_RC=$?
eq "M2 the package staged" "$STM2_RC" "0"
if [ "$STM2_RC" != "0" ]; then
  echo "  M2 cmake --install said:"; tail -8 "$MUT/logs/m2-stage.log" | sed 's/^/    /'
  echo "  M2 what the failed stage holds:"
  ( cd "$M2DIR" 2>/dev/null && find . -type f -printf '%P\t%s\n' | LC_ALL=C sort ) > "$MUT/logs/m2-stage-inventory.txt"
  sed 's/^/    /' "$MUT/logs/m2-stage-inventory.txt"
fi
M2_LINKED=0
mutated_binary_identity "$M2DIR/tf2_demo_native.exe" "M2 fixture binary" || M2_LINKED=$?
if [ "$M2_LINKED" = "90" ]; then mutation_not_linked "the M2 fixture would run the reference build"; fi
M2_RC="$(run_exe "$M2DIR/tf2_demo_native.exe" "$MUT/logs/m2-install.txt" "$MUT/logs/m2.ms" \
  --install --install-dir "$(cygpath -w "$M2DIR")" \
  --settings-file "$(cygpath -w "$HERM_SET")" \
  --ui-settings-file "$(cygpath -w "$HERM_UI")")"
echo "  under M2: install exit=$M2_RC"
M2MAN="$M2DIR/.uninstall/install-manifest.txt"
python "$HELPER" check "$M2MAN" "$(cygpath -w "$M2DIR")" > "$MUT/logs/m2-bytes.txt" 2>&1
M2_BYTES="$(cat "$MUT/logs/m2-bytes.txt")"
echo "$M2_BYTES" | sed 's/^/    /'
MUTATIONS=$((MUTATIONS + 1))
if printf '%s' "$M2_BYTES" | grep -q "valid_utf8=yes"; then
  wrong "M2 caught: the manifest is not valid UTF-8" "valid_utf8=yes" "valid_utf8=no"
else
  ok "M2 caught: the manifest is ANSI bytes, not UTF-8" "valid_utf8=no"
fi
cp "$MUT/backup/install_lifecycle.cpp" "$SRC_LIFE"
if cmp -s "$SRC_LIFE" "$MUT/backup/install_lifecycle.cpp"; then ok "M2 source restored byte-identical" "yes"; else wrong "M2 source restored byte-identical" "no" "yes"; fi
rebuild m2-restore
eq "M2 restore build succeeded" "$?" "0"

echo
echo "  checks=$CHECKS fail=$FAIL mutations=$MUTATIONS"
if [ "$FAIL" -eq 0 ] && [ "$MUTATIONS" -eq 2 ]; then
  echo "  INSTALL-CLI-CONTRACT-MUTATION=PASS"; exit 0
fi
echo "  INSTALL-CLI-CONTRACT-MUTATION=FAIL"; exit 1
