#!/usr/bin/env bash
# warn-check.sh -- the per-target /W4 warning gate: compile the translation units
# that actually carry /W4, with the exact flags CMake recorded, and fail when a
# warning appears that the baseline does not already contain.
#
# WHY IT EXISTS (N23)
#   The build already prints 21 warnings per clean build, so "warnings=21" cannot
#   tell a reviewer whether their change added a 22nd. A gate needs a comparison,
#   not a count: this one compares against tools/warn-baseline.tsv and names every
#   line that is new. Turning /W4 off would also make the number drop; that is why
#   the baseline records the code and the site, not the total.
#
# WHAT IT DOES NOT DO
#   It does not build anything and it does not touch the build tree: objects land
#   under .scratch/warn-check/objs/. It gates the warnings of the targets that
#   already ask for /W4 -- 7 targets, 86 translation units, measured 2026-11-15.
#   A target that does not request /W4 is out of scope (that is E3b batch 2).
#
# USAGE
#   bash tools/warn-check.sh                  # check against the baseline
#   bash tools/warn-check.sh --emit-baseline  # rewrite tools/warn-baseline.tsv
#   bash tools/warn-check.sh --selftest       # prove the gate can go red
#   bash tools/warn-check.sh --all            # every target, /W4 or not (slow)
#
# EXIT: 0 = no new warnings; 1 = new warnings or a TU failed; 2 = cannot run.
#
# NOTES FOR WHOEVER EDITS THIS NEXT
#  * flags.make records -ID:\TF2_Native_Worktrees\... and the compile has to go through
#    eval (that is how the NMake rule hands CXX_DEFINES to cl.exe). eval re-parses
#    the line, an unquoted backslash is an escape there, and D:\TF2 collapsed to
#    D:TF2 -- every TU then died with C1083 on its own header. The includes are
#    rewritten to forward slashes for that reason. Do not "clean that up".
#  * every awk program here is single-quoted on purpose; inside double quotes bash
#    would expand $1/$2 before awk saw them.
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$ROOT/native/build-nmake"
OUT="${WARN_CHECK_OUT:-$ROOT/.scratch/warn-check}"
BASELINE="$ROOT/tools/warn-baseline.tsv"
JOBS="${WARN_CHECK_JOBS:-12}"
MODE=check
ALL=0
for arg in "$@"; do
  case "$arg" in
    --emit-baseline) MODE=emit ;;
    --selftest) MODE=selftest ;;
    --all) ALL=1 ;;
    -h|--help) sed -n "2,40p" "$0"; exit 0 ;;
    *) echo "unknown argument: $arg" >&2; exit 2 ;;
  esac
done
[ -d "$BUILD/CMakeFiles" ] || { echo "WARN-GATE=FAIL reason=no-build-tree build=$BUILD" >&2; exit 2; }
mkdir -p "$OUT" || exit 2

# ---- the compile worker ------------------------------------------------
# One TU per invocation so xargs can run several at once. Every child needs the
# toolchain on PATH; a fresh bash from xargs has none.
cat > "$OUT/one.sh" <<'WORKER'
#!/usr/bin/env bash
set -u
ROOT="$1"; BUILD="$2"; OBJDIR="$3"; target="$4"; src="$5"
# The checkout prefix this worker compiles under. It used to be the literal
# "t105-e5" below, which silently stopped matching once the work moved to
# another worktree: keys kept their absolute path, the external-header rule then
# rewrote them to <external>/..., and the gate reported a clean tree as a
# regression. Derived here so a worktree rename cannot do that again.
rootwin=$(cygpath -m "$ROOT" 2>/dev/null || printf "%s" "$ROOT")
export TF2_TOOLCHAIN_QUIET=1
. "$ROOT/tools/tf2-toolchain.sh" >/dev/null 2>&1 || { echo "NO-TOOLCHAIN"; exit 3; }
d="$BUILD/CMakeFiles/$target.dir"
flags=$(sed -n "s/^CXX_FLAGS = //p" "$d/flags.make" | head -1)
# The selftest plants a warning on purpose; a target that has /WX would turn it
# into error C2220 and the comparator would never see it (measured 2026-11-18 when
# tf2_core joined TF2_WX_DEFAULT_TARGETS and --selftest went red with
# "planted-warning-not-observed"). Only the selftest asks for the strip: the gate
# itself must compile with exactly the flags the target builds with.
if [ "${WARNCHECK_NO_WX:-0}" = "1" ]; then
  flags=$(printf "%s" "$flags" | sed -e "s#/WX##g")
fi
defs=$(sed -n "s/^CXX_DEFINES = //p" "$d/flags.make" | head -1)
incs=$(sed -n "s/^CXX_INCLUDES = //p" "$d/flags.make" | head -1)
incs=$(printf "%s" "$incs" | tr '\\' '/')
base=$(basename "$src" .cpp)
log="$OBJDIR/$target.$base.log"
objwin=$(cygpath -m "$OBJDIR/$target.$base.obj")
srcwin=$(cygpath -m "$src")
eval "cl.exe /nologo /c $defs $incs $flags /Fo:\"$objwin\" \"$srcwin\"" >"$log" 2>&1
rc=$?
if [ "$rc" -ne 0 ]; then
  echo "FAILED	$target	$base	$rc"
  exit 0
fi
# Each finding leaves this worker as path:line:code, and the path is normalised first.
# Two normalisations are load-bearing and were each measured on 2026-11-16:
#  1. backslashes -> slashes, then the checkout prefix is stripped, so the key is
#     repo-relative and matches what a human writes into the baseline;
#  2. a warning raised inside an SDK header has an absolute path OUTSIDE the repo.
#     Left alone, its drive colon becomes a field separator downstream and the key
#     degenerates to "C:4813"; it also makes the key depend on the installed toolset
#     version. Hence <external>/<basename>, applied AFTER the line/code extraction
#     (before it, the trailing ": warning C..." text defeats the trailing anchor).
grep "warning C" "$log" 2>/dev/null | sed -e "s#\\\\#/#g" -e "s#^.*${rootwin}/##" \
  | sed -n "s#^\(.*\)(\([0-9]*\)): warning \(C[0-9]*\).*#\1:\2:\3#p" \
  | sed -e "s#^[A-Za-z]:/.*/\([^/:]*:\)#<external>/\1#"
WORKER
chmod +x "$OUT/one.sh" || exit 2

# ---- which targets and which translation units -------------------------
# "carries /W4" is read from flags.make, but a target that lists /W4 and owns no
# object (tf2_build_stamp, tf2_identity) must not be counted -- hence the obj check.
tu_list() { # tu_list <all?> -> target<TAB>source
  local want_all="$1" d t n w4
  for d in "$BUILD"/CMakeFiles/*.dir; do
    t=$(basename "$d" .dir)
    [ -f "$d/flags.make" ] || continue
    [ -f "$d/DependInfo.cmake" ] || continue
    n=$(grep -c '^  "D:/' "$d/DependInfo.cmake" 2>/dev/null || true)
    [ -z "$n" ] && n=0
    [ "$n" -gt 0 ] || continue
    if [ "$want_all" -eq 0 ]; then
      w4=no
      grep -q -- "/W4" "$d/flags.make" && w4=yes
      [ "$w4" = yes ] || continue
    fi
    grep '^  "D:/' "$d/DependInfo.cmake" | cut -d'"' -f2 | while read -r s; do
      printf "%s	%s\n" "$t" "$s"
    done
  done
}

run_compile() { # run_compile -> fills $OUT/warn-raw.tsv and $OUT/warn-fail.tsv
  : > "$OUT/tus.tsv"
  tu_list "$ALL" > "$OUT/tus.tsv"
  mkdir -p "$OUT/objs" || return 2
  local count
  count=$(wc -l < "$OUT/tus.tsv")
  if [ "$count" -eq 0 ]; then echo "WARN-GATE=FAIL reason=no-translation-units" >&2; return 2; fi
  : > "$OUT/warn-raw.tsv"
  : > "$OUT/warn-fail.tsv"
  tr '	' ' ' < "$OUT/tus.tsv" | \
    xargs -P "$JOBS" -L 1 bash -c 'bash "$0/one.sh" "$1" "$2" "$3" "$4" "$5"' \
      "$OUT" "$ROOT" "$BUILD" "$OUT/objs" >> "$OUT/warn-raw.tsv" 2>>"$OUT/warn-stderr.txt"
  grep "^FAILED" "$OUT/warn-raw.tsv" | cut -f2- > "$OUT/warn-fail.tsv" || true
  # Keyed on file:code, not file:line:code, AND counted. A line-keyed baseline
  # reports every untouched warning as NEW the moment somebody inserts a line
  # above it -- main.cpp slid six lines on 2026-11-16 without changing a single
  # warning, and a gate that cries wolf on arithmetic is a gate people learn to
  # ignore. The count keeps what dropping the line would lose: a second C4456 in
  # the same file is a new warning, and the comparator compares counts, not keys.
  # (uniq -c is deliberately not used: its output would only be re-parsed here.)
  grep -v "^FAILED" "$OUT/warn-raw.tsv" | grep -v "^NO-TOOLCHAIN" | awk '{
      n = split($0, p, ":")
      if (n < 3) next
      path = p[1]
      for (i = 2; i <= n - 2; i++) path = path ":" p[i]
      key = path ":" p[n]
      cnt[key]++
    }
    END { for (k in cnt) printf "%s\t%s\n", cnt[k], k }' > "$OUT/warn-counted.tsv" || true
  echo "WARN-GATE tus=$count"
  return 0
}

# ---- comparison --------------------------------------------------------
compare() { # compare <baseline> <counted> -> sets RC and prints findings
  local base="$1" cur="$2" new=0 fixed=0 same=0
  # The two files are joined with a sentinel instead of the usual NR==FNR trick.
  # NR==FNR is a lie when the FIRST file is empty: file 1 contributes no records,
  # so the first record of file 2 still has NR==FNR and is read as a baseline line.
  # A checkout with zero warnings made the comparator report every finding as
  # FIXED and the selftest could not go red because of it (measured 2026-11-16).
  awk -F'	' '$0=="---CURRENT---"{cur=1; next}
    !cur{b[$2]=$1; next} {c[$2]=$1} END{
    for (k in c) { if (!(k in b)) { print "NEW " k " x" c[k]; n++ }
                   else if (c[k] > b[k]) { print "NEW " k " x" c[k] " (baseline " b[k] ")"; n++ }
                   else { m++ } }
    for (k in b) { if (!(k in c)) { print "FIXED " k; f++ } }
    printf "COUNTS new=%d fixed=%d same=%d\n", n+0, f+0, m+0 }' \
    <( { cat "$base"; echo "---CURRENT---"; cat "$cur"; } ) | sed 's/^NEW /  NEW   /; s/^FIXED /  FIXED /'
}

case "$MODE" in
  emit)
    run_compile || exit 2
    # A baseline taken from a tree that does not compile is worse than no baseline:
    # the units that failed contribute no sites, so their warnings silently vanish
    # from the file and the next good run reports them as NEW. That happened on
    # 2026-11-16 while another instance had main.cpp half-written, and it produced
    # a "BASELINE-WRITTEN lines=0" that looked like success.
    emitted_fails=$(wc -l < "$OUT/warn-fail.tsv")
    if [ "$emitted_fails" -ne 0 ]; then
      echo "WARN-GATE=BASELINE-REFUSED reason=compile-failed tus=$emitted_fails (fix the tree, then emit)"
      sed 's/^/  TU-FAILED /; s/	/ /g' "$OUT/warn-fail.tsv"
      exit 2
    fi
    {
      echo "# tools/warn-baseline.tsv -- the /W4 warnings that already exist."
      echo "# <count> TAB <file>:<line>:<code>. Regenerate with: bash tools/warn-check.sh --emit-baseline"
      echo "# Empty as of 2026-11-18: every target of native/ builds at /W4 with zero warnings"
      echo "# (N23, docs/N23-WALL-LEVEL-2026-11-18.md), so every finding is NEW. A warning that"
      echo "# disappears is still reported as FIXED and is no improvement until the site is gone."
      cat "$OUT/warn-counted.tsv"
    } > "$BASELINE"
    echo "WARN-GATE=BASELINE-WRITTEN lines=$(grep -vc "^#" "$BASELINE") path=tools/warn-baseline.tsv"
    ;;
  check)
    [ -f "$BASELINE" ] || { echo "WARN-GATE=FAIL reason=no-baseline path=tools/warn-baseline.tsv" >&2; exit 2; }
    run_compile || exit 2
    fails=$(wc -l < "$OUT/warn-fail.tsv")
    if [ "$fails" -ne 0 ]; then
      echo "WARN-GATE=FAIL reason=compile-failed tus=$fails"
      sed 's/^/  TU-FAILED /; s/	/ /g' "$OUT/warn-fail.tsv"
      exit 1
    fi
    findings=$(compare <(grep -v "^#" "$BASELINE") "$OUT/warn-counted.tsv")
    counts=$(printf "%s\n" "$findings" | sed -n "s/^COUNTS //p")
    newn=$(printf "%s\n" "$counts" | sed -n "s/.*new=\([0-9]*\).*/\1/p")
    printf "%s\n" "$findings" | grep -v "^COUNTS" | sed '/^$/d'
    if [ "${newn:-0}" -ne 0 ]; then
      echo "WARN-GATE=FAIL $counts"
      exit 1
    fi
    echo "WARN-GATE=PASS $counts"
    ;;
  selftest)
    # The gate is only worth having if it can go red. This compiles ONE unmodified
    # TU and ONE copy with a deliberate warning, then requires the comparison to
    # name exactly the planted site -- a comparator that reports nothing fails here.
    [ -f "$BASELINE" ] || { echo "WARN-GATE=FAIL reason=no-baseline-for-selftest" >&2; exit 2; }
    ALL=0 JOBS=2
    clean_src="$ROOT/native/src/install_manifest.cpp"
    [ -f "$clean_src" ] || { echo "WARN-GATE=FAIL reason=no-selftest-source" >&2; exit 2; }
    mkdir -p "$OUT/selftest" || exit 2
    planted="$OUT/selftest/planted_warning.cpp"
    # The plant has to be a construct cl.exe ACTUALLY warns about under these
    # flags. A namespace-scope variable in an anonymous namespace does not: C4189
    # only covers locals, so the first version of this selftest planted nothing and
    # reported "planted-warning-not-observed" against a working gate. Measured
    # 2026-11-16: an unused LOCAL of type std::size_t gives C4189 at /W4.
    { echo "#include <cstddef>"
      echo "void plantedWarningProbe() { std::size_t plantedAndNeverRead = 7; (void)0; }"; } > "$planted"
    WARNCHECK_NO_WX=1 bash "$OUT/one.sh" "$ROOT" "$BUILD" "$OUT/selftest" "tf2_core" "$planted" > "$OUT/selftest/raw.txt" 2>&1
    # The worker rewrites each finding to file:line:code before it prints, so the
    # raw file no longer contains the string "warning C" -- counting that string
    # here made a working gate look dead. Count the rewritten shape instead.
    planted_hits=$(grep -c ":C[0-9][0-9]*$" "$OUT/selftest/raw.txt" || true)
    if [ "${planted_hits:-0}" -eq 0 ]; then
      echo "WARN-GATE=SELFTEST-FAIL reason=planted-warning-not-observed"
      exit 1
    fi
    printf "%s	%s\n" "$planted_hits" "$(sed -n "1p" "$OUT/selftest/raw.txt")" > "$OUT/selftest/counted.tsv"
    seen=$(compare <(grep -v "^#" "$BASELINE") "$OUT/selftest/counted.tsv" | grep -c "^  NEW" || true)
    if [ "${seen:-0}" -eq 0 ]; then
      echo "WARN-GATE=SELFTEST-FAIL reason=comparator-did-not-go-red"
      exit 1
    fi
    echo "WARN-GATE=SELFTEST-PASS planted=$planted_hits reported=$seen"
    ;;
esac