#!/usr/bin/env bash
# install-lifecycle-prereq-selftest.sh -- the executable controls for D-91.
#
# WHAT THIS PROVES, AND HOW
#   D-91: when the build directory was missing a member of the install set,
#   install-lifecycle-check.sh did not say so. It staged a short package and then
#   failed 162 assertions that all measured the same absence, so the gate printed
#   a wall of red that pointed at no defect. The fix checks the install set before
#   section 1/15 and refuses with one PREREQUISITE-FAIL line and exit 3.
#
#   Every control below is a measurement made by running the real gate script
#   against a stand-in build directory (TF2_INSTALL_BUILD), never against the
#   real one, and nothing is built:
#
#   POS-1  a complete stand-in set passes: exit 0, one PASS line, members=7, and
#          the inventory hashes each member (the exe hash is compared against
#          sha256sum of the same file, so "hashed" is not a word in the log);
#   POS-2  when the set is complete the gate proceeds to section 1/15 -- the
#          refusal is a precondition, not a detour around the case body;
#   NEG-1  a missing member (vpk_query.exe) refuses with exit 3, exactly one
#          PREREQUISITE-FAIL line naming it, and no section starts at all;
#   NEG-2  a zero-byte member is refused rather than hashed and counted;
#   NEG-3  a sidecar whose sourceContent is not 64 hex digits is refused, because
#          it would otherwise turn into a red --version reading one layer later;
#   NEG-4  --forward on an incomplete set refuses at the same place and never
#          reaches cmake --install (no install.log is written);
#   NEG-5  TF2_INSTALL_BUILD together with --mutation is refused with exit 2: a
#          mutation must run the build it just made, not a stand-in.
#
#   What it does NOT prove: that a real forward run against a complete build
#   directory passes (that needs an E5 build and its 223 checks -- it is run in
#   the build window, not here), and that the 7 members are exactly the set
#   tf2-package.cmake installs (the payload name list is asserted by the forward
#   run itself, install-lifecycle-check.sh:PAYLOAD_NAMES).
#
# Usage: bash install-lifecycle-prereq-selftest.sh
#   Exit 0 = every control held, 1 = at least one did not, 2 = could not run.
set -uo pipefail

REPO="$(cd "$(dirname "$0")" && pwd)"
GATE="$REPO/install-lifecycle-check.sh"
if [ ! -f "$GATE" ]; then echo "D91-PREREQ-SELFTEST=CANNOT-RUN missing $GATE" >&2; exit 2; fi

WORK="${TMPDIR:-/tmp}/d91-prereq-selftest.$$"
rm -rf "$WORK"; mkdir -p "$WORK"
cleanup() { rm -rf "$WORK"; }
trap cleanup EXIT INT TERM

CHECKS=0; FAIL=0
ok()    { CHECKS=$((CHECKS + 1)); echo "  ok   $1"; }
wrong() { CHECKS=$((CHECKS + 1)); FAIL=$((FAIL + 1)); echo "  FAIL $1 = $2, expected $3"; }
eq()    { if [ "$2" = "$3" ]; then ok "$1 ($2)"; else wrong "$1" "$2" "$3"; fi; }
ge()    { if [ "${2:-0}" -ge "${3:-0}" ] 2>/dev/null; then ok "$1 ($2 >= $3)"; else wrong "$1" "${2:-<none>}" ">= $3"; fi; }
# Pattern matching goes through case, not "printf | grep -q": under pipefail the
# printf dies of SIGPIPE the moment grep -q finds its match, which turns a large
# matching input into a false FAIL.
has()   { case "$2" in *"$3"*) ok "$1";; *) wrong "$1" "<no '$3'>" "contains '$3'";; esac; }
hasnt() { case "$2" in *"$3"*) wrong "$1" "<has '$3'>" "does not contain '$3'";; *) ok "$1";; esac; }

# A stand-in for native/build-nmake: every member the check reads, with distinct
# contents so an accidental "any file counts" shortcut would still hash wrong.
standin() { # <dir>
  local dir="$1"
  rm -rf "$dir"; mkdir -p "$dir"
  printf 'stand-in product\n'          > "$dir/tf2_demo_native.exe"
  printf 'stand-in smoke probe\n'      > "$dir/native_install_smoke_probe.exe"
  printf 'stand-in install probe\n'    > "$dir/install_cli_probe.exe"
  printf 'stand-in vpk query\n'        > "$dir/vpk_query.exe"
  {
    echo "format=tf2-identity-v1"
    echo "formatVersion=2"
    echo "contentMethod=git-clean-v1"
    echo "sourceContent=0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
    echo "sourceTree=deadbeef0000"
  } > "$dir/tf2_demo_native.identity"
}

run_gate() { # <out-dir> <mode> [timeout-seconds] -> rc, output in $out/log.txt
  local out="$1" mode="$2" limit="${3:-}"
  mkdir -p "$out"
  if [ -n "$limit" ]; then
    timeout "$limit" bash "$GATE" "$mode" "$out" > "$out/log.txt" 2>&1
  else
    bash "$GATE" "$mode" "$out" > "$out/log.txt" 2>&1
  fi
  echo $?
}

echo "D-91 prerequisite controls on $(basename "$REPO")"
echo "  gate: $GATE"
echo "  gate sha256: $(sha256sum "$GATE" | cut -d' ' -f1)"
echo

# ---------------------------------------------------------------- POS-1 / POS-2
echo "POS: a complete stand-in set passes and hands control to section 1/15"
STANDIN="$WORK/build-complete"; standin "$STANDIN"
OUT="$WORK/out-pos"; rc=0
rc="$(TF2_INSTALL_BUILD="$STANDIN" run_gate "$OUT" --prereq-only)" || true
eq "POS-1 the prerequisite stage exits 0 on a complete set" "$rc" "0"
LOG="$(cat "$OUT/log.txt" 2>/dev/null)"
has  "POS-1 the stage reports PASS"                 "$LOG" "INSTALL-LIFECYCLE-PREREQ=PASS members=7"
has  "POS-1 no refusal line on the passing path"    "$LOG" "prerequisites present=7/7"
eq   "POS-1 the inventory has one line per member"  "$(grep -c '^PREREQUISITE ok ' "$OUT/logs/prerequisite.txt" 2>/dev/null)" "7"
eq   "POS-1 the inventory records bytes and sha256 for each" \
     "$(grep -cE '^PREREQUISITE ok .* bytes=[1-9][0-9]* sha256=[0-9a-f]{64} path=' "$OUT/logs/prerequisite.txt" 2>/dev/null)" "7"
EXE_SHA="$(sha256sum "$STANDIN/tf2_demo_native.exe" | cut -d' ' -f1)"
has  "POS-1 the recorded hash is the file's own hash, not a placeholder" \
     "$(cat "$OUT/logs/prerequisite.txt")" "sha256=$EXE_SHA"
has  "POS-1 the sidecar content hash is checked, not just the file" \
     "$(cat "$OUT/logs/prerequisite.txt")" "sourceContent=0123456789abcdef"

# POS-2 runs --forward on the same complete stand-in and stops it as soon as the
# case body is reached: the claim is only that the refusal did not fire and that
# section 1 began, not that the forwarding run passes (it cannot, there is no
# package behind the stand-in).
OUT="$WORK/out-pos-forward"; rc=0
rc="$(TF2_INSTALL_BUILD="$STANDIN" run_gate "$OUT" --forward 15)" || true
FLOG="$(cat "$OUT/log.txt" 2>/dev/null)"
hasnt "POS-2 a complete set is not refused"        "$FLOG" "PREREQUISITE-FAIL"
has   "POS-2 the case body starts at section 1/15" "$FLOG" "=== 1/15"

# ---------------------------------------------------------------------- NEG-1
echo
echo "NEG: a missing member is one line and exit 3, before any case runs"
BROKEN="$WORK/build-missing"; standin "$BROKEN"; rm -f "$BROKEN/vpk_query.exe"
OUT="$WORK/out-missing"; rc=0
rc="$(TF2_INSTALL_BUILD="$BROKEN" run_gate "$OUT" --prereq-only)" || true
eq    "NEG-1 a missing member exits 3"                    "$rc" "3"
MLOG="$(cat "$OUT/log.txt" 2>/dev/null)"
eq    "NEG-1 exactly one PREREQUISITE-FAIL line is printed" \
      "$(printf '%s\n' "$MLOG" | grep -c '^PREREQUISITE-FAIL ')" "1"
has   "NEG-1 the line names the missing member"           "$MLOG" "missing=vpk_query.exe"
has   "NEG-1 the line says why it was requested"          "$MLOG" "reason=prereq-only"
has   "NEG-1 the verdict names the failure"               "$MLOG" "INSTALL-LIFECYCLE=PREREQUISITE-FAIL missing=1"
has   "NEG-1 the inventory marks it missing"              "$(cat "$OUT/logs/prerequisite.txt" 2>/dev/null)" "PREREQUISITE missing vpk_query.exe"
hasnt "NEG-1 no PASS verdict is printed"                  "$MLOG" "INSTALL-LIFECYCLE=PASS"

# ---------------------------------------------------------------------- NEG-2
echo
echo "NEG: a zero-byte member is refused, not hashed and counted"
EMPTY="$WORK/build-empty"; standin "$EMPTY"; : > "$EMPTY/native_install_smoke_probe.exe"
OUT="$WORK/out-empty"; rc=0
rc="$(TF2_INSTALL_BUILD="$EMPTY" run_gate "$OUT" --prereq-only)" || true
eq  "NEG-2 a zero-byte member exits 3"             "$rc" "3"
ELOG="$(cat "$OUT/log.txt" 2>/dev/null)"
has "NEG-2 the member is named"                    "$ELOG" "missing=native_install_smoke_probe.exe"
has "NEG-2 the inventory says empty, not ok"       "$(cat "$OUT/logs/prerequisite.txt" 2>/dev/null)" "PREREQUISITE empty native_install_smoke_probe.exe"

# ---------------------------------------------------------------------- NEG-3
echo
echo "NEG: a sidecar without a 64-hex content hash is refused"
NOSHA="$WORK/build-nosha"; standin "$NOSHA"
sed -i 's/^sourceContent=.*/sourceContent=/' "$NOSHA/tf2_demo_native.identity"
OUT="$WORK/out-nosha"; rc=0
rc="$(TF2_INSTALL_BUILD="$NOSHA" run_gate "$OUT" --prereq-only)" || true
eq  "NEG-3 an empty sidecar hash exits 3"          "$rc" "3"
has "NEG-3 the refusal names the field"            "$(cat "$OUT/log.txt" 2>/dev/null)" "missing=tf2_demo_native.identity:sourceContent"

# ---------------------------------------------------------------------- NEG-4
echo
echo "NEG: --forward on an incomplete set never reaches cmake --install"
OUT="$WORK/out-forward-missing"; rc=0
rc="$(TF2_INSTALL_BUILD="$BROKEN" run_gate "$OUT" --forward 15)" || true
eq    "NEG-4 forward exits 3 on an incomplete set"  "$rc" "3"
FMLOG="$(cat "$OUT/log.txt" 2>/dev/null)"
hasnt "NEG-4 no section starts"                     "$FMLOG" "=== 1/15"
hasnt "NEG-4 no package is staged"                  "$FMLOG" "--- staged package ---"
eq    "NEG-4 no install log was written" \
      "$([ -e "$OUT/logs/install.log" ] && echo written || echo absent)" "absent"

# ---------------------------------------------------------------------- NEG-5
echo
echo "NEG: a mutation may not run against a stand-in build directory"
OUT="$WORK/out-mutation"; rc=0
rc="$(TF2_INSTALL_BUILD="$STANDIN" run_gate "$OUT" --mutation)" || true
eq  "NEG-5 --mutation with a stand-in build exits 2" "$rc" "2"
has "NEG-5 the refusal says why" "$(cat "$OUT/log.txt" 2>/dev/null)" "must run the real build"

# ---------------------------------------------------------------------- manner
echo
echo "the gate script still parses"
bash -n "$GATE"; eq "bash -n install-lifecycle-check.sh" "$?" "0"

echo
echo "  checks=$CHECKS fail=$FAIL"
if [ "$FAIL" -eq 0 ]; then echo "  D91-PREREQ-SELFTEST=PASS assertions=$CHECKS"; exit 0; fi
echo "  D91-PREREQ-SELFTEST=FAIL assertions=$CHECKS fail=$FAIL"; exit 1
