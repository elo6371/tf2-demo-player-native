#!/usr/bin/env bash
# build-identity-content-selftest.sh -- the executable controls for D-90.
#
# WHAT THIS PROVES, AND HOW
#   D-90: the content hash used to enumerate git INDEX entries only, so a file
#   that was present in the worktree but untracked could be compiled while
#   staying outside the digest (the first D-54 build hashed 53f14a19 with two
#   untracked cmake files driving it). The fix enumerates the untracked and
#   ignored files under the same roots and refuses when one of them is a
#   compile input.
#
#   Each control below is a measurement, not a reading of the source:
#
#   POS-1  the cmake generator and the independent bash tool agree on the
#          digest for this tree, so the census did not change what is hashed;
#   POS-2  the generator as it stood BEFORE the fix (git show
#          $D90_BEFORE_COMMIT:native/cmake/gen-build-stamp.cmake) produces the
#          same digest as the fixed one on the same tree -- the census adds a
#          refusal, it does not move the number;
#   NEG-1  an untracked .cpp under native/src makes BOTH implementations refuse
#          (cmake non-zero, bash exit 4), names the file, and writes no stamp;
#   NEG-2  TF2_IDENTITY_ALLOW_UNTRACKED=1 is the documented override: it
#          succeeds and reports contentMethod=git-untracked-v1, which no gate
#          accepts as pinnable;
#   NEG-3  an IGNORED compile input (in an isolated throwaway repository, so the
#          real tree is never touched) is refused and named;
#   GEN-1  a file under native/generated -- this generator's own output -- is
#          NOT an input: the digest does not move and nothing is refused.
#
#   What it does NOT prove: that a build of the two executables fails (no build
#   is run -- the controls drive the generator in cmake -P script mode), and
#   that the bash tool refuses an ignored input (its two branches share the
#   classifier and the refusal shape, but only the cmake side is exercised for
#   the ignored category; the untracked category is exercised on both).
#
# Usage: bash build-identity-content-selftest.sh
#   Exit 0 = every control held, 1 = at least one did not, 2 = could not run.
set -uo pipefail

REPO="$(cd "$(dirname "$0")" && pwd)"
NATIVE="$REPO/native"
TOOL="$REPO/build-identity-content.sh"
GEN="$NATIVE/cmake/gen-build-stamp.cmake"
BEFORE_COMMIT="${D90_BEFORE_COMMIT:-8f40192}"

cd "$REPO" || exit 2
WORK="$(mktemp -d "${TMPDIR:-/tmp}/d90-selftest.XXXXXX")" || { echo "cannot create a scratch dir" >&2; exit 2; }
WPATH="$(cygpath -m "$WORK" 2>/dev/null || printf '%s' "$WORK")"
NPATH="$(cygpath -m "$NATIVE" 2>/dev/null || printf '%s' "$NATIVE")"

PROBES=()
PROBE_DIRS=()
cleanup() {
  local p
  for p in ${PROBES[@]+"${PROBES[@]}"}; do rm -f "$p"; done
  for p in ${PROBE_DIRS[@]+"${PROBE_DIRS[@]}"}; do rmdir "$p" 2>/dev/null || true; done
  rm -rf "$WORK"
}
trap cleanup EXIT INT TERM

CHECKS=0
BAD=0
ok()  { CHECKS=$((CHECKS + 1)); printf "  ok   %s\n" "$1"; }
bad() { CHECKS=$((CHECKS + 1)); BAD=$((BAD + 1)); printf "  FAIL %s\n" "$1"; }
eq()  { if [ "$2" = "$3" ]; then ok "$1 (= $3)"; else bad "$1 (got '$2' want '$3')"; fi; }
ge()  { if [ "${2:-0}" -ge "${3:-0}" ] 2>/dev/null; then ok "$1 ($2 >= $3)"; else bad "$1 (got '${2:-}' want >= $3)"; fi; }

hash_of() { sed -n 's/.*kSourceContent\[\] = "\([0-9a-f]\{64\}\)".*/\1/p' "$1" 2>/dev/null | head -1; }
method_of() { sed -n 's/.*kContentMethod\[\] = "\([^"]*\)".*/\1/p' "$1" 2>/dev/null | head -1; }

run_gen() { # <generator.cmake> <outdir> <logfile> ; sets GEN_RC
  local g="$1" out="$2" log="$3" outw
  outw="$(cygpath -m "$out" 2>/dev/null || printf '%s' "$out")"
  mkdir -p "$out"
  cmake -DTF2_REPO_DIR="$NPATH" \
        -DTF2_STAMP_OUTPUT="$outw/stamp.cpp" \
        -DTF2_PROVENANCE_OUTPUT="$outw/prov.identity" \
        -P "$g" >"$log" 2>&1
  GEN_RC=$?
}

seen() { # <file> <pattern> <description> -- existence assertion before any grep
  if [ -f "$1" ] && grep -q -- "$2" "$1" 2>/dev/null; then ok "$3"; else bad "$3 (no '$2' in $1)"; fi
}

echo "== D-90 selftest =="
echo "   tree:   $(git rev-parse --short=12 HEAD) on $(git rev-parse --abbrev-ref HEAD)"
echo "   repo:   $REPO"
echo "   before: $BEFORE_COMMIT (the pre-fix generator)"
echo "   scratch: $WORK"

# Guard: the probes below must not exist already; a leftover would make the
# "restored" readings meaningless.
if [ -e "$NATIVE/src/zz_untracked_probe.cpp" ] || [ -e "$NATIVE/generated/zz_ignored_generated_probe.cpp" ]; then
  echo "REFUSING: a D-90 probe file is already present in the tree; remove it first" >&2
  exit 2
fi

echo "--- baseline ---"
H0="$(bash "$TOOL" hash 2>/dev/null)"
STATS0="$(bash "$TOOL" stats 2>/dev/null)"
M0="$(bash "$TOOL" method 2>/dev/null)"
eq "the content tool answers with a 64-hex digest" "$(printf '%s' "$H0" | grep -cE '^[0-9a-f]{64}$')" "1"
eq "the baseline method is git-clean-v1" "$M0" "git-clean-v1"
ge "the census line exists" "$(printf '%s' "$STATS0" | grep -c 'method=')" 1
eq "the worktree has no untracked compile input to start from" "$(printf '%s' "$STATS0" | sed -n 's/.*untrackedInputs=\([0-9]*\).*/\1/p')" "0"
eq "the worktree has no ignored compile input to start from" "$(printf '%s' "$STATS0" | sed -n 's/.*ignoredInputs=\([0-9]*\).*/\1/p')" "0"

echo "--- POS-1: cmake generator and bash tool agree ---"
run_gen "$GEN" "$WORK/pos1" "$WORK/pos1.log"
eq "the fixed generator succeeds on a clean tracked tree" "$GEN_RC" "0"
C0="$(hash_of "$WORK/pos1/stamp.cpp")"
eq "the fixed generator wrote a 64-hex sourceContent" "$(printf '%s' "$C0" | grep -cE '^[0-9a-f]{64}$')" "1"
eq "its digest is the bash tool's digest" "$C0" "$H0"
eq "it still reports contentMethod=git-clean-v1" "$(method_of "$WORK/pos1/stamp.cpp")" "git-clean-v1"

echo "--- POS-2: pre-fix generator, same tree, same digest ---"
git show "$BEFORE_COMMIT:native/cmake/gen-build-stamp.cmake" > "$WORK/before.cmake" 2>/dev/null
ge "the pre-fix generator was extracted" "$(wc -l < "$WORK/before.cmake" 2>/dev/null | tr -d ' ')" 100
eq "the extracted generator is the pre-fix one (it mentions no D-90)" "$(grep -c 'D-90' "$WORK/before.cmake")" "0"
run_gen "$WORK/before.cmake" "$WORK/pos2" "$WORK/pos2.log"
eq "the pre-fix generator succeeds on the same tree" "$GEN_RC" "0"
C1="$(hash_of "$WORK/pos2/stamp.cpp")"
eq "the pre-fix generator wrote a 64-hex sourceContent" "$(printf '%s' "$C1" | grep -cE '^[0-9a-f]{64}$')" "1"
eq "the two generations of the generator hash this tree identically" "$C1" "$C0"

echo "--- NEG-1: an untracked compile input must be refused, and named ---"
PROBE_UNTRACKED="$NATIVE/src/zz_untracked_probe.cpp"
PROBES+=("$PROBE_UNTRACKED")
printf "// D-90 negative control: an untracked compile input\n" > "$PROBE_UNTRACKED"
run_gen "$GEN" "$WORK/neg1" "$WORK/neg1.log"
if [ "$GEN_RC" -ne 0 ]; then ok "the generator refuses an untracked compile input (rc=$GEN_RC)"; else bad "the generator refused an untracked compile input (got rc=0)"; fi
seen "$WORK/neg1.log" 'D-90 REFUSED' "the refusal says it is D-90 refusing"
seen "$WORK/neg1.log" 'zz_untracked_probe.cpp' "the refusal names the file it found"
eq "the refusal wrote no stamp.cpp" "$([ -f "$WORK/neg1/stamp.cpp" ] && echo written || echo none)" "none"
eq "the refusal wrote no identity file" "$([ -f "$WORK/neg1/prov.identity" ] && echo written || echo none)" "none"
bash "$TOOL" hash >"$WORK/neg1.sh.out" 2>"$WORK/neg1.sh.err"
eq "the bash tool refuses with exit 4" "$?" "4"
eq "the bash tool printed no digest" "$(wc -c < "$WORK/neg1.sh.out" | tr -d ' ')" "0"
seen "$WORK/neg1.sh.err" 'zz_untracked_probe.cpp' "the bash tool names the file it found"

echo "--- NEG-2: the override is degraded, not silent ---"
run_gen_allow() {
  local g="$1" out="$2" log="$3"
  TF2_IDENTITY_ALLOW_UNTRACKED=1 run_gen "$g" "$out" "$log"
}
run_gen_allow "$GEN" "$WORK/neg2" "$WORK/neg2.log"
eq "the override lets the build proceed" "$GEN_RC" "0"
eq "the overridden stamp says git-untracked-v1" "$(method_of "$WORK/neg2/stamp.cpp")" "git-untracked-v1"
seen "$WORK/neg2.log" 'D-90 OVERRIDDEN' "the log says the check was overridden"
eq "the bash tool reports the same degraded method" "$(TF2_IDENTITY_ALLOW_UNTRACKED=1 bash "$TOOL" method)" "git-untracked-v1"
rm -f "$PROBE_UNTRACKED"

echo "--- restore after NEG-1/NEG-2 ---"
eq "the tree hashes back to the baseline once the probe is gone" "$(bash "$TOOL" hash 2>/dev/null)" "$H0"
eq "the census is empty again" "$(bash "$TOOL" stats 2>/dev/null)" "$STATS0"

echo "--- GEN-1: native/generated is output, not input ---"
if [ -d "$NATIVE/generated" ]; then
  PROBE_GEN="$NATIVE/generated/zz_ignored_generated_probe.cpp"
else
  PROBE_GEN="$NATIVE/generated/zz_ignored_generated_probe.cpp"
  mkdir -p "$NATIVE/generated"
  PROBE_DIRS+=("$NATIVE/generated")
fi
PROBES+=("$PROBE_GEN")
printf "// D-90 control: a build-output directory is not an input\n" > "$PROBE_GEN"
eq "git ignores the generated probe" "$(git status --porcelain -- "$PROBE_GEN" | wc -l | tr -d ' ')" "0"
run_gen "$GEN" "$WORK/gen1" "$WORK/gen1.log"
eq "the generator still succeeds with a .cpp under generated/" "$GEN_RC" "0"
eq "the digest did not move" "$(hash_of "$WORK/gen1/stamp.cpp")" "$H0"
eq "the status line counts it as generated" "$(printf '%s' "$(sed -n 's/.*generated=\([0-9]*\).*/\1/p' "$WORK/gen1.log" | tail -1)")" "1"
eq "and does not count it as an input" "$(sed -n 's/.*untracked=\([0-9]*\)\/\([0-9]*\).*/\1/p' "$WORK/gen1.log" | tail -1)" "0"

echo "--- NEG-3: an IGNORED compile input is refused (isolated repository) ---"
FAKE="$WORK/ignored-repo"
mkdir -p "$FAKE/native/src" "$FAKE/native/cmake"
printf 'src/zz_ignored_probe.cpp\n' > "$FAKE/native/.gitignore"
printf 'int tracked_probe = 1;\n' > "$FAKE/native/src/tracked_probe.cpp"
printf '# a configure-time input\n' > "$FAKE/native/cmake/probe.cmake"
git init -q "$FAKE" 2>/dev/null
git -C "$FAKE" -c user.email=d90@example.invalid -c user.name=d90 add -A 2>/dev/null
git -C "$FAKE" -c user.email=d90@example.invalid -c user.name=d90 commit -qm "base" 2>/dev/null
eq "the isolated repository is valid" "$(git -C "$FAKE" rev-parse --is-inside-work-tree 2>/dev/null)" "true"
printf "// D-90 control: ignored, but named like a compile input\n" > "$FAKE/native/src/zz_ignored_probe.cpp"
eq "the isolated probe really is ignored" "$(git -C "$FAKE/native" check-ignore -q src/zz_ignored_probe.cpp && echo ignored || echo tracked)" "ignored"
run_gen_fake() {
  mkdir -p "$WORK/neg3"
  cmake -DTF2_REPO_DIR="$(cygpath -m "$FAKE/native" 2>/dev/null || printf '%s' "$FAKE/native")" \
        -DTF2_STAMP_OUTPUT="$WPATH/neg3/stamp.cpp" \
        -DTF2_PROVENANCE_OUTPUT="$WPATH/neg3/prov.identity" \
        -P "$GEN" >"$WORK/neg3.log" 2>&1
  GEN_RC=$?
}
run_gen_fake
if [ "$GEN_RC" -ne 0 ]; then ok "an ignored compile input is refused (rc=$GEN_RC)"; else bad "an ignored compile input is refused (got rc=0)"; fi
seen "$WORK/neg3.log" 'zz_ignored_probe.cpp' "the refusal names the ignored file"
eq "no identity was written for the ignored-input tree" "$([ -f "$WORK/neg3/prov.identity" ] && echo written || echo none)" "none"

rm -f "$PROBE_GEN"

echo "--- final state ---"
eq "the tree still hashes to the baseline" "$(bash "$TOOL" hash 2>/dev/null)" "$H0"
eq "the census is empty" "$(bash "$TOOL" stats 2>/dev/null)" "$STATS0"
eq "no probe file is left under native/" "$(git status --porcelain --ignored -- native | grep -c 'zz_.*probe')" "0"

if [ "$BAD" -eq 0 ]; then
  echo "D90-SELFTEST=PASS assertions=$CHECKS"
  RC=0
else
  echo "D90-SELFTEST=FAIL assertions=$CHECKS bad=$BAD"
  RC=1
fi
exit $RC