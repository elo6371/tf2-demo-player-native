#!/usr/bin/env bash
# vmt-nesting-check.sh -- pin the VMT parser's recursion bound.
#
# WHY THIS EXISTS
#   readObject() used to recurse once per nested object with no depth argument.
#   A 100000-deep fixture therefore exhausted the stack: the old probe exited
#   127 with zero bytes on stdout and stderr.  A 65-deep fixture was accepted.
#   The fix adds an explicit depth argument and rejects depth > 64.  This gate
#   pins both sides: 64 is accepted, 65 is refused with the exact diagnostic,
#   and the 100000 case is a bounded parse failure rather than a crash.
#
# WHAT THIS GATE ASSERTS (36 assertions in the green run)
#   A01-A09  the nine fixture sha256 values
#   A10-A13  nesting-4 and nesting-64 parse, with an empty error
#   A14-A15  nesting-65 is refused with the exact depth diagnostic
#   A16-A18  nesting-100000 returns rc=0, parses false, same diagnostic
#   A19-A22  self-patch: false, patch declared, patch not resolved
#   A23-A26  cycle-a / cycle-b: false, with the patch-chain diagnostic
#   A27-A32  wrapper: parsed, shader, baseTexture, patch fields, include
#   A33-A36  base: parsed, shader, baseTexture, empty error
#
# WHAT THIS GATE DOES NOT CLAIM
#   It does not run the product, does not prove the product's world-material
#   path reaches this parser, and does not prove the build is clean.  It is
#   chain-external (D-103): it is not wired into verify-all.sh or
#   verify-fast.sh.  It must be run under hostlock by the caller.
#
# NEGATIVE CONTROL
#   Pass --negative --probe <pre-fix material_chain_probe.exe>.  The old binary
#   must report nesting-65 as parsed=true, must exit non-zero on nesting-100000,
#   and must emit zero bytes for that case.  The token is then
#   VMT-NESTING-NEGATIVE=PASS (3 assertions); it is not part of the 36.
#
# INDEPENDENT BY DESIGN
#   It stages only its own output under .scratch/vmt-nesting and reads committed
#   fixtures.  It never acquires hostlock (hostlock is not reentrant): it calls
#   hostlock status and refuses with exit 9 unless the caller already holds it.

set -uo pipefail
cd "$(dirname "$0")" || exit 9
export LC_ALL=C

PROBE="native/build-nmake/material_chain_probe.exe"
FIXTURES="testdata/fixtures/vmt-nesting"
OUT=".scratch/vmt-nesting"
KEEP=0
NEGATIVE=0
QUIET=0
HOSTLOCK="${HOSTLOCK:-/d/TF2_Native_Worktrees/_shared/hostlock.sh}"
OWNER="${VMT_NESTING_OWNER:-}"

while [ "$#" -gt 0 ]; do
  case "$1" in
    --probe) PROBE="$2"; shift 2 ;;
    --fixtures) FIXTURES="$2"; shift 2 ;;
    --out) OUT="$2"; shift 2 ;;
    --negative) NEGATIVE=1; shift ;;
    --keep) KEEP=1; shift ;;
    --quiet) QUIET=1; shift ;;
    --help|-h)
      sed -n '2,50p' "$0"
      exit 0
      ;;
    *) echo "vmt-nesting-check: unknown argument: $1" >&2; exit 2 ;;
  esac
done

TOKEN="VMT-NESTING"
if [ "$NEGATIVE" -eq 1 ]; then TOKEN="VMT-NESTING-NEGATIVE"; fi

if [ -z "$OWNER" ]; then
  echo "$TOKEN=FAIL (0 assertions) lock=not-held reason=owner-not-set" >&2
  echo "  run it as: bash $HOSTLOCK run <owner> env VMT_NESTING_OWNER=<owner> bash $0" >&2
  exit 9
fi
if [ ! -f "$HOSTLOCK" ]; then
  echo "$TOKEN=FAIL (0 assertions) lock=not-held reason=hostlock-script-missing path=$HOSTLOCK"
  exit 9
fi
lock_status="$(bash "$HOSTLOCK" status 2>&1)"
held="$(printf '%s\n' "$lock_status" | sed -n 's/^LOCKED owner=\([^ ]*\).*/\1/p' | head -n 1)"
if [ "$held" != "$OWNER" ]; then
  echo "$TOKEN=FAIL (0 assertions) lock=not-held owner=$OWNER held=${held:-none}" >&2
  printf '%s\n' "$lock_status" | sed 's/^/  lock: /' >&2
  exit 9
fi

case "$OUT" in
  ""|"/"|".") echo "$TOKEN=FAIL (0 assertions) unsafe out=$OUT" >&2; exit 2 ;;
esac
rm -rf "$OUT"
mkdir -p "$OUT" || { echo "$TOKEN=FAIL (0 assertions) out-unwritable=$OUT" >&2; exit 1; }

ASSERTIONS=0
FAILED=0
pass() {
  ASSERTIONS=$((ASSERTIONS + 1))
  if [ "$QUIET" -eq 0 ]; then printf 'OK %s\n' "$*"; fi
}
fail() {
  ASSERTIONS=$((ASSERTIONS + 1))
  FAILED=$((FAILED + 1))
  printf 'FAIL %s\n' "$*"
}
assert_eq() {
  local name="$1" actual="$2" expected="$3"
  if [ "$actual" = "$expected" ]; then
    pass "$name=$actual"
  else
    fail "$name expected=$expected actual=$actual"
  fi
}
file_sha() {
  local path="$1"
  if [ ! -f "$path" ]; then printf 'missing'; return; fi
  sha256sum "$path" | cut -d' ' -f1
}
# A missing key is not the same as an empty string: the green assertions for
# "empty error" must fail against a probe that does not emit the field at all.
json_field() {
  local key="$1" file="$2"
  if [ ! -f "$file" ] || ! grep -Fq "$key" "$file"; then
    printf '<missing>'
    return
  fi
  sed -n 's/.*"'"$key"'":"\([^"]*\)".*/\1/p' "$file" | head -n 1
}
json_bool() {
  local key="$1" file="$2"
  if [ ! -f "$file" ] || ! grep -Fq "$key" "$file"; then
    printf '<missing>'
    return
  fi
  sed -nE 's/.*"'"$key"'":(true|false).*/\1/p' "$file" | head -n 1
}
run_case() {
  local name="$1" target="$2" include_root="${3-}"
  local rc=0
  if [ -n "$include_root" ]; then
    "$PROBE" --vmt "$target" --vmt-include-root "$include_root" \
      > "$OUT/$name.json" 2> "$OUT/$name.err" || rc=$?
  else
    "$PROBE" --vmt "$target" \
      > "$OUT/$name.json" 2> "$OUT/$name.err" || rc=$?
  fi
  printf '%s\n' "$rc" > "$OUT/$name.rc"
  return 0
}
finish() {
  echo
  if [ "$FAILED" -eq 0 ]; then
    echo "$TOKEN=PASS ($ASSERTIONS assertions)"
    if [ "$KEEP" -eq 0 ]; then rm -rf "$OUT"; fi
    exit 0
  fi
  echo "$TOKEN=FAIL ($FAILED/$ASSERTIONS failed)"
  exit 1
}

echo "probe: $PROBE"
if [ ! -f "$PROBE" ]; then
  echo "$TOKEN=FAIL (0 assertions) probe=missing path=$PROBE"
  exit 1
fi
echo "probe_sha256=$(sha256sum "$PROBE" | cut -d' ' -f1)"
echo "probe_bytes=$(stat -c %s "$PROBE" 2>/dev/null || echo unknown)"
echo "fixtures: $FIXTURES"
echo "out: $OUT"
echo "mode: $([ "$NEGATIVE" -eq 1 ] && echo negative || echo green)"
echo

F4="$FIXTURES/nesting-4.vmt"
F64="$FIXTURES/nesting-64.vmt"
F65="$FIXTURES/nesting-65.vmt"
F100K="$FIXTURES/nesting-100000.vmt"
SELF="$FIXTURES/self-patch.vmt"
CYCA="$FIXTURES/cycle-a.vmt"
CYCB="$FIXTURES/cycle-b.vmt"
WRAP="$FIXTURES/patch-chain-ok/wrapper.vmt"
BASE="$FIXTURES/patch-chain-ok/base.vmt"

# A01-A09: the fixtures themselves.  In --negative mode these nine hashes
# are not part of the control: the negative run must report exactly 3
# assertions, so the fixtures are checked for presence only there.
if [ "$NEGATIVE" -eq 0 ]; then
  assert_eq "fixture.nesting-4.sha256" "$(file_sha "$F4")" "99f27adc7db0d4b6001d7fb2541c3f44187c62b0cbf6b2e86630e794240299ba"
  assert_eq "fixture.nesting-64.sha256" "$(file_sha "$F64")" "dce3aa053e80d2d4c755c4cb4e0e726608619ab1011e784c9b5bc364d7f8c807"
  assert_eq "fixture.nesting-65.sha256" "$(file_sha "$F65")" "74068db5031b2a611868587433168c04806407f50ae250a43c1aabbac4fc92f1"
  assert_eq "fixture.nesting-100000.sha256" "$(file_sha "$F100K")" "c6cea9f2515ef5b84b8aee79effaa8561a53fd4187dd36a02a6ec9c5b4e01ce0"
  assert_eq "fixture.self-patch.sha256" "$(file_sha "$SELF")" "7b58dee01389433544a4a11ce57dc11eb8eb779c4d5fc82e4ecc29865e13c40d"
  assert_eq "fixture.cycle-a.sha256" "$(file_sha "$CYCA")" "45d6aeab27d61bd66332454e13031a1a54d53bf02cef617cb2993490f4f247f2"
  assert_eq "fixture.cycle-b.sha256" "$(file_sha "$CYCB")" "787360837438fb0b2f39320170356616361b2e3d1313366f9020301931202db5"
  assert_eq "fixture.wrapper.sha256" "$(file_sha "$WRAP")" "c04c547230ea0cec41c40f224dae98130c6f8aef0abb8a6dd1f8010aaaca2fff"
  assert_eq "fixture.base.sha256" "$(file_sha "$BASE")" "5f889522d26a665e740dde9a6818693fe432d666c66b2e73beecb788d47cd184"
fi

if [ ! -f "$F4" ] || [ ! -f "$F64" ] || [ ! -f "$F65" ] || [ ! -f "$F100K" ] || [ ! -f "$SELF" ] || [ ! -f "$CYCA" ] || [ ! -f "$CYCB" ] || [ ! -f "$WRAP" ] || [ ! -f "$BASE" ]; then
  echo "$TOKEN=FAIL ($ASSERTIONS/$ASSERTIONS failed) reason=fixture-missing dir=$FIXTURES"
  exit 1
fi

if [ "$NEGATIVE" -eq 1 ]; then
  # Three assertions against a pre-fix probe.  The old binary accepted a
  # 65-deep object (parsed=true) and crashed on the 100000-deep one (rc=127,
  # zero output).  These are the red half of the evidence.
  run_case neg-nesting-65 "$F65"
  assert_eq "old.nesting-65.parsed" "$(json_bool explicitVmtParsed "$OUT/neg-nesting-65.json")" "true"

  run_case neg-nesting-100000 "$F100K"
  rc="$(cat "$OUT/neg-nesting-100000.rc")"
  if [ "$rc" -ne 0 ]; then actual_rc="nonzero"; else actual_rc="zero"; fi
  assert_eq "old.nesting-100000.rc" "$actual_rc" "nonzero"
  bytes=$(( $(stat -c %s "$OUT/neg-nesting-100000.json") + $(stat -c %s "$OUT/neg-nesting-100000.err") ))
  assert_eq "old.nesting-100000.output_bytes" "$bytes" "0"

  finish
fi

# A10-A13: depth 4 and depth 64 are inside the bound and parse cleanly.
run_case nesting-4 "$F4"
assert_eq "nesting-4.parsed" "$(json_bool explicitVmtParsed "$OUT/nesting-4.json")" "true"
assert_eq "nesting-4.error" "$(json_field explicitVmtError "$OUT/nesting-4.json")" ""

run_case nesting-64 "$F64"
assert_eq "nesting-64.parsed" "$(json_bool explicitVmtParsed "$OUT/nesting-64.json")" "true"
assert_eq "nesting-64.error" "$(json_field explicitVmtError "$OUT/nesting-64.json")" ""

# A14-A15: one object past the bound is refused, with the named diagnostic.
run_case nesting-65 "$F65"
assert_eq "nesting-65.parsed" "$(json_bool explicitVmtParsed "$OUT/nesting-65.json")" "false"
assert_eq "nesting-65.error" "$(json_field explicitVmtError "$OUT/nesting-65.json")" "VMT object nesting is deeper than 64"

# A16-A18: the pathological case is a bounded failure, not a crash.
run_case nesting-100000 "$F100K"
assert_eq "nesting-100000.rc" "$(cat "$OUT/nesting-100000.rc")" "0"
assert_eq "nesting-100000.parsed" "$(json_bool explicitVmtParsed "$OUT/nesting-100000.json")" "false"
assert_eq "nesting-100000.error" "$(json_field explicitVmtError "$OUT/nesting-100000.json")" "VMT object nesting is deeper than 64"

# A19-A22: a self-patch is still refused by the patch-chain bound, not by the
# new object bound.  That distinction is the reason kMaxPatchDepth is untouched.
run_case self-patch "$SELF" "$FIXTURES"
assert_eq "self-patch.parsed" "$(json_bool explicitVmtParsed "$OUT/self-patch.json")" "false"
assert_eq "self-patch.error" "$(json_field explicitVmtError "$OUT/self-patch.json")" "patch include chain is deeper than 4"
assert_eq "self-patch.patch_declared" "$(json_bool explicitVmtPatchDeclared "$OUT/self-patch.json")" "true"
assert_eq "self-patch.patch_resolved" "$(json_bool explicitVmtPatchResolved "$OUT/self-patch.json")" "false"

# A23-A26: both two-file cycles are refused with the same named diagnostic.
run_case cycle-a "$CYCA" "$FIXTURES"
assert_eq "cycle-a.parsed" "$(json_bool explicitVmtParsed "$OUT/cycle-a.json")" "false"
assert_eq "cycle-a.error" "$(json_field explicitVmtError "$OUT/cycle-a.json")" "patch include chain is deeper than 4"

run_case cycle-b "$CYCB" "$FIXTURES"
assert_eq "cycle-b.parsed" "$(json_bool explicitVmtParsed "$OUT/cycle-b.json")" "false"
assert_eq "cycle-b.error" "$(json_field explicitVmtError "$OUT/cycle-b.json")" "patch include chain is deeper than 4"

# A27-A32: a legal patch resolves through the include reader.  This is the
# positive control for the probe's --vmt-include-root path, not for the depth
# bound itself.
run_case wrapper "$WRAP" "$FIXTURES/patch-chain-ok"
assert_eq "wrapper.parsed" "$(json_bool explicitVmtParsed "$OUT/wrapper.json")" "true"
assert_eq "wrapper.shader" "$(json_field explicitVmtShader "$OUT/wrapper.json")" "LightMappedGeneric"
assert_eq "wrapper.base_texture" "$(json_field explicitVmtBaseTexture "$OUT/wrapper.json")" "qa1a02/replaced"
assert_eq "wrapper.patch_declared" "$(json_bool explicitVmtPatchDeclared "$OUT/wrapper.json")" "true"
assert_eq "wrapper.patch_resolved" "$(json_bool explicitVmtPatchResolved "$OUT/wrapper.json")" "true"
assert_eq "wrapper.patch_include" "$(json_field explicitVmtPatchInclude "$OUT/wrapper.json")" "materials/base.vmt"

# A33-A36: the included base is read as its own material.
run_case base "$BASE"
assert_eq "base.parsed" "$(json_bool explicitVmtParsed "$OUT/base.json")" "true"
assert_eq "base.shader" "$(json_field explicitVmtShader "$OUT/base.json")" "LightMappedGeneric"
assert_eq "base.base_texture" "$(json_field explicitVmtBaseTexture "$OUT/base.json")" "qa1a02/base"
assert_eq "base.error" "$(json_field explicitVmtError "$OUT/base.json")" ""

finish
