#!/usr/bin/env bash
# build-identity-check.sh -- "--version" must describe the tree this binary was
# linked from, not the tree the build directory was configured from.
#
# Why this exists (T90, 2026-11-07)
# ---------------------------------
# tf2-version.cmake measured the source tree with configure-time commands and
# wrote the answer into TF2_VERSION_DEFINES. Configure runs once; every
# incremental build reuses the cached tree and never re-runs it. The frozen
# candidate therefore answered
#     buildUtc=2026-11-02T16:22:30Z sourceDateEpoch=0 sourceTree=12042361ffd1
# while it had been linked at 2026-11-06 20:20:35 from a tree whose HEAD was
# e15577b -- 41 commits later (git rev-list --count 12042361ffd1..e15577b) --
# and which had uncommitted changes at that moment. sourceDateEpoch was a
# literal "0" on every build ever made, and nothing in the output could tell a
# binary built from a clean commit from one built on top of it.
#
# T90 (2026-11-07) moved the measurement from configure time to build time,
# which fixed the reading but not the churn. D-54 (2026-11-10) split it: what
# the SOURCES contain is one content hash, compiled into both executables; where
# those sources CAME FROM -- HEAD, the dirty counts, the epoch, the build time --
# is written to a sidecar beside each executable and printed from there.
# --version now answers four lines:
#     1  package=tf2_demo_native version=0.1.0 versionCode=100
#     2  buildUtc=<sidecar> sourceDateEpoch=<sidecar> sourceTree=<sidecar>
#     3  dirty=0 dirtyFiles=0 dirtyTracked=0 dirtyUntracked=0      (sidecar)
#     4  sourceContent=<64 hex> contentMethod=git-clean-v1 identity=sidecar
# Line 4 is read from the sidecar too, but the assertion that matters is that
# the sidecar's hash is the hash COMPILED INTO the executable: the sidecar must
# name the bytes next to it, and the gate recomputes the hash from the tree with
# a second, independent implementation (build-identity-content.sh).
#
# This gate compares those readings against git, live, in four states: the tree
# as it is now (sourceTree must be HEAD; the dirty flag and its counts must be
# exactly what "git status --porcelain -uall" reports at that instant; the
# sidecar must name the exe on disk and the hash compiled into it; the build's
# content hash must equal an independent recomputation); a tree the gate dirties
# with docs-only changes, where D-54 requires the READING to move and the BYTES
# to stay identical (a change to docs/ cannot alter one instruction); a tree
# dirtied in a COMPILED input, where both must move, followed by byte-exact
# restoration (bytes and hash must return -- the property that makes the reading
# a function of the tree rather than of what happened to be lying around); and a
# no-op rebuild, which must move neither the bytes nor the stamp and must not
# rewrite the sidecar.
#
# AN EXE SHA IS NOT AN IDENTITY. Measured on 2026-11-07: three builds of one
# source tree produced three different sha256 values, because the COFF
# TimeDateStamp in the PE header (e_lfanew + 8) moved with the wall clock
# (0x6aeee287 -> 0x6aeee295 across a 14 s gap). The product now links with
# /Brepro (native/cmake/tf2-settings.cmake), --reproducible measures two CLEAN
# FULL builds byte for byte, and every identity this gate records pairs the exe
# sha256 with git HEAD AND a sha256 of the native/ tree it was built from
# (section 4b/5). Quoted alone, a sha names one link; it does not name a tree.
#
# THE ORDER OF EVERY CHECK HERE IS: assert the reading EXISTS, then assert what
# it says. An absent line must not be able to compare equal to an empty value --
# that is how "the trace lost the line" and "the value is right" both turn green.
#
# What this gate does NOT prove -- read before quoting a PASS:
#   * not cross-machine reproducibility: --reproducible proves two clean full
#     builds of this tree on THIS machine and toolchain are byte-identical; it
#     says nothing about another compiler, SDK, path or machine.
#   * not that the content hash covers the toolchain: line 4 hashes the compiled
#     INPUTS, so the same inputs built with another compiler have the same line 4
#     and different bytes. It identifies the sources, not the image.
#   * not that raw-v1 and git-clean-v1 are comparable: when git is unavailable the
#     hash degrades to raw bytes, and contentMethod is the only thing that says
#     which of the two you are looking at. Two different methods never agree.
#   * not that an ignored file is accounted for: git-ignored inputs are not
#     counted, so a build whose only extra input is ignored reads as clean.
#   * not that the manifest/VERSION.txt agree with --version; that is
#     install-lifecycle-check.sh's subject and this gate reads --version only.
#   * not that a dirty binary can be rebuilt from the dirty tree; the tree is not
#     recorded, only counted -- the hash covers the compiled inputs, not the
#     provenance the sidecar reports.
#   * not that an installed product keeps its identity: the sidecar is installed
#     beside the executable (tf2-package.cmake), but a package assembled without
#     it answers identity=missing and unknown/-1 on lines 2 and 3. That is
#     install-lifecycle-check.sh's subject, not this gate's.
#   * not that the product UNDERSTANDS an argument it was given: section 1c/5
#     reads the "args: unrecognized=" line, which counts what the loop did not
#     consume. The default run still ignores those arguments -- that is the
#     behaviour the line documents, and --strict-args is the switch that turns
#     the count into a refusal.
#
# Usage:
#   bash build-identity-check.sh --forward      <out-dir>  # BUILD-IDENTITY=PASS
#   bash build-identity-check.sh --mutation     <out-dir>  # 4 mutants must go red
#   bash build-identity-check.sh --reproducible <out-dir>  # two clean full
#       # builds must be byte-identical: BUILD-REPRODUCIBLE=PASS builds=2 sha=<sha>
#
# The argument surface (section 1c/5) is the second subject of this gate: the
# product used to drop any argv it did not recognise without a word (D-23), and
# two gates in this wave were written against switches that do not exist. Every
# run now prints "args: unrecognized=N first=... all=..." to the trace named by
# TF2_NATIVE_TRACE, and --strict-args turns N>0 into stderr + exit 2.
#
# This gate BUILDS the product and launches it, so it runs under the host lock;
# it does not acquire the lock itself (hostlock is not reentrant):
#   bash /d/TF2_Native_Worktrees/_shared/hostlock.sh run t90-identity \
#       bash build-identity-check.sh --forward .scratch/identity-forward
# --version is answered before settings, window and audio initialisation
# (main_lifecycle.cpp:20-34), so no device is opened; the launch still happens
# inside the lock like every other launcher.
#
# Exit: 0 = every assertion held (with --mutation: all four mutants were caught;
#       with --reproducible: the two clean builds were byte-identical),
#       1 = an assertion failed, 2 = usage,
#       90 = a mutant left the READING it was meant to break unchanged, so nothing
#            was measured. Under D-54 that guard is reading-level, not byte-level:
#            a mutant inside native/cmake/ also moves the content hash and thus
#            the bytes, which says nothing about whether the reading was tested.
set -uo pipefail

REPO="$(cd "$(dirname "$0")" && pwd)"
cd "$REPO"

usage() { echo "usage: bash build-identity-check.sh --forward|--mutation|--reproducible <out-dir>" >&2; exit 2; }

MODE="--forward"          # bare call = the forward run (see the note below)
[ "$#" -ge 1 ] && MODE="$1"
OUT_ARG=""
[ "$#" -ge 2 ] && OUT_ARG="$2"
case "$MODE" in
  --forward|--mutation|--reproducible) ;;
  *) usage ;;
esac
# verify-release.sh runs every step as "bash <script>" with NO arguments, so a
# bare call has to mean "forward run into a default output directory" -- otherwise
# the eighth step of the chain dies on usage (exit 2) and reads as a red gate.
# The default output lives under .scratch/ (gitignored) so the run cannot dirty
# the very tree whose dirty count it is measuring.
if [ -n "$OUT_ARG" ]; then
  mkdir -p "$OUT_ARG" || { echo "READ-FAILED cannot create the output directory $OUT_ARG" >&2; exit 2; }
else
  # NOT under .scratch/build-identity: SCRATCH (below) is deleted wholesale by
  # this run, and an OUT inside it was created here and then deleted there --
  # RUN19 (2026-11-09) lost all eleven evidence files that way while still
  # printing PASS. The run output lives beside SCRATCH, never inside it.
  OUT_ARG="$REPO/.scratch/build-identity-run/forward"
  mkdir -p "$OUT_ARG" || { echo "READ-FAILED cannot create the output directory $OUT_ARG" >&2; exit 2; }
fi
OUT="$(cd "$OUT_ARG" && pwd)"

BUILD_DIR="native/build-nmake"
[ -n "${TF2_BUILD_DIR-}" ] && BUILD_DIR="$TF2_BUILD_DIR"
EXE="$BUILD_DIR/tf2_demo_native.exe"
PROBE="$BUILD_DIR/install_cli_probe.exe"
GENERATOR="native/cmake/gen-build-stamp.cmake"
STAMP="native/generated/tf2_build_stamp.cpp"
# D-54: the provenance the build wrote for the tree it just built, and the two
# sidecars copied beside the executables. The .identity files carry HEAD, the
# dirty counts, the epoch and the build time -- everything that used to be
# compiled in and therefore changed the bytes of a binary whose code had not
# changed. They are named after the executable, so the product and the probe
# cannot read each other's.
PROVENANCE="native/generated/tf2_build_stamp.identity"
IDENTITY="$BUILD_DIR/tf2_demo_native.identity"
PROBE_IDENTITY="$BUILD_DIR/install_cli_probe.identity"
# The gate's own re-implementation of the content hash (git blob ids, sorted,
# sha256). Deliberately not the generator: an assertion whose two sides are the
# same code proves that the code is deterministic, not that it measured the tree.
CONTENT_TOOL="build-identity-content.sh"

# The gate measures the tree's dirtiness, so it must not add any: every
# intermediate copy goes to .scratch/ (git-ignored), never to $OUT, and $OUT is
# written only after the last reading.
SCRATCH="$REPO/.scratch/build-identity"
# The next line deletes SCRATCH. Anything the run promises to leave behind must
# not live there, so an OUT that would be swept away is refused loudly instead
# of being silently recreated after the sweep (that silent recreate is what made
# a red gate print PASS).
case "$OUT/" in
  "$SCRATCH"/*)
    echo "READ-FAILED out=$OUT is inside the scratch directory $SCRATCH, which this run deletes" >&2
    echo "  give an output directory outside $SCRATCH; the default is $REPO/.scratch/build-identity-run/forward" >&2
    exit 2 ;;
esac
rm -rf "$SCRATCH"
mkdir -p "$SCRATCH"

# Evidence copies. A bare "cp ... 2>/dev/null" hides exactly the failure this
# gate suffered in RUN19: eleven copies failed and nothing said so. Every copy
# below goes through copy_out, which counts, and each mode asserts the promised
# files are on disk and non-empty before it prints a verdict.
COPY_N=0
COPY_FAIL=0
copy_out() { # copy_out <src> <name>
  COPY_N=$((COPY_N + 1))
  if ! cp "$1" "$OUT/$2"; then
    COPY_FAIL=$((COPY_FAIL + 1))
    echo "  COPY-FAILED $2 <- $1"
  fi
}
# Two kinds of promised file, and they are judged differently on purpose:
#   present  -- the product's own stdout/stderr capture for a refusal path. The
#               product prints NOTHING for an unknown argument (it exits 13 with
#               an empty stdout), so a 0-byte args-unknown.log is the expected
#               result and not a lost file. Judging it with -s would call a
#               healthy run red (it did, 2026-11-09 23:59, RUN22: 11 of 12 files
#               non-empty and the gate failed on the twelfth).
#   nonempty -- anything that carries a reading (--version text, identity record,
#               trace, readings.txt). An empty reading IS a missing reading.
evidence_missing() { # evidence_missing <present|nonempty> <name>...
  local kind="$1"
  shift
  local missing=0
  local f
  for f in "$@"; do
    case "$kind" in
      present) [ -f "$OUT/$f" ] || missing=$((missing + 1)) ;;
      *)       [ -s "$OUT/$f" ] || missing=$((missing + 1)) ;;
    esac
    [ -e "$OUT/$f" ] || echo "  MISSING $f" >&2
    [ -e "$OUT/$f" ] && [ ! -s "$OUT/$f" ] && [ "$kind" = "nonempty" ] && echo "  EMPTY $f" >&2
  done
  printf '%s' "$missing"
}

# HERMETIC SETTINGS, FOR EVERY LAUNCH (2026-11-07 hygiene rule).
#   The product writes settings unconditionally on exit: main.cpp:11613-11625
#   (saveSettings + camera bookmarks/hotkeys) and native_ui.cpp:137-139 (the
#   recentDemo0..7 MRU list). A launch without both overrides rewrites the real
#   %LOCALAPPDATA%\TF2 Demo Player\settings.ini and ui.ini, so two runs stop
#   being the same input and the user's own state is edited as a side effect.
#   The copies live in SCRATCH (git-ignored) so that even a write stays inside
#   the run. --version returns from main_lifecycle.cpp before the settings file
#   is read, so the flags cost nothing here -- they are prophylactic, and they
#   are passed on every product launch this gate makes.
#   install_cli_probe is NOT the product: it links install_lifecycle.cpp only,
#   opens no settings file, and rejects unknown arguments (exit 2), so it is
#   launched without the flags.
# One mktemp directory per run, removed by cleanup() below. The files are created
# empty first and then overwritten with the machine's own copies when they exist,
# so a launch always gets paths that exist even on a machine that never ran the
# product -- the override must not fail just because there was nothing to copy.
INPUTS="$(mktemp -d "${TMPDIR:-/tmp}/tf2-build-identity-settings.XXXXXX")" || exit 2
: > "$INPUTS/settings.ini"
: > "$INPUTS/ui.ini"
LOCALAPPDATA_UX="$(cygpath -u "${LOCALAPPDATA-}" 2>/dev/null)"
if [ -n "${LOCALAPPDATA_UX-}" ] && [ -d "$LOCALAPPDATA_UX/TF2 Demo Player" ]; then
  cp "$LOCALAPPDATA_UX/TF2 Demo Player/settings.ini" "$INPUTS/settings.ini" 2>/dev/null || true
  cp "$LOCALAPPDATA_UX/TF2 Demo Player/ui.ini" "$INPUTS/ui.ini" 2>/dev/null || true
fi
SETTINGS_ARGS=(--settings-file "$INPUTS/settings.ini" --ui-settings-file "$INPUTS/ui.ini")

CHECKS=0
FAIL=0
MUTATIONS=0

sec() { echo; echo "=== $1 ==="; }
ok() { echo "  OK   $1 = $2"; CHECKS=$((CHECKS + 1)); }
wrong() { echo "  FAIL $1 = $2, expected $3"; CHECKS=$((CHECKS + 1)); FAIL=$((FAIL + 1)); }
eq() { if [ "$2" = "$3" ]; then ok "$1" "$2"; else wrong "$1" "$2" "$3"; fi; }
ge() {
  if [ -n "${2-}" ] && [ "$2" -ge "$3" ] 2>/dev/null; then ok "$1" "$2"; else wrong "$1" "${2:-<none>}" ">= $3"; fi
}
le() {
  if [ -n "${2-}" ] && [ "$2" -le "$3" ] 2>/dev/null; then ok "$1" "$2"; else wrong "$1" "${2:-<none>}" "<= $3"; fi
}
hasnt() {
  case "$2" in
    *"$3"*) wrong "$1" "present" "absent: $3" ;;
    *) ok "$1" "absent" ;;
  esac
}

# A field of the --version answer, e.g. ver_field "$f" sourceTree. The leading
# anchor keeps dirty= from matching inside dirtyFiles=.
ver_field() {
  sed -n "s/.*\(^\|[[:space:]]\)$2=\([^[:space:]]*\).*/\2/p" "$1" | head -1
}
count_field() { grep -cE "(^|[[:space:]])$2=" "$1"; }
count_line() { sed -n "$2p" "$1" | grep -c .; }
shasum() { sha256sum "$1" 2>/dev/null | cut -d' ' -f1; }

cleanup() {
  local rc=$?
  # The per-run settings copies: after this, nothing this gate launched can be
  # pointed at the machine's real %LOCALAPPDATA%/TF2 Demo Player/ files again.
  [ -n "${INPUTS-}" ] && rm -rf "$INPUTS"
  if [ -f "$SCRATCH/probe-doc.backup" ] && [ -n "${PROBE_TRACKED-}" ]; then
    cp "$SCRATCH/probe-doc.backup" "$PROBE_TRACKED" 2>/dev/null
    echo "  (exit cleanup: restored $PROBE_TRACKED from the gate's own backup)"
    rm -f "$SCRATCH/probe-doc.backup"
  fi
  if [ -n "${PROBE_UNTRACKED-}" ]; then rm -f "$PROBE_UNTRACKED"; fi
  # D-54 3b/5 edits a COMPILED input (native/src/main.cpp). If the gate is
  # interrupted between the edit and the inline restore, the tree would be left
  # with a modified source -- so the same backup is restored here as well.
  if [ -f "$SCRATCH/probe-source.backup" ] && [ -n "${PROBE_SOURCE-}" ] && [ -f "${PROBE_SOURCE-}" ]; then
    cp "$SCRATCH/probe-source.backup" "$PROBE_SOURCE" 2>/dev/null
    echo "  (exit cleanup: restored $PROBE_SOURCE from the gate's own backup)"
    rm -f "$SCRATCH/probe-source.backup"
  fi
  exit "$rc"
}
trap cleanup EXIT INT TERM

build_targets() { # <log-suffix>
  # D-54: tf2_identity is the ALL target that refreshes the two .identity
  # sidecars. An incremental build that NAMES a target does not build ALL
  # targets, so it has to be named too -- otherwise a build that changed only
  # the provenance would leave the sidecar describing the previous tree, and
  # every assertion below would be reading a stale file.
  bash build-target.sh tf2_demo_native install_cli_probe tf2_identity > "$SCRATCH/build-$1.log" 2>&1
  return $?
}
report_build_failure() {
  echo "  --- build log tail ---"
  grep -a -E "error C|error MSB|fatal error|LNK[0-9]+" "$1" | head -20 | sed 's/^/    /'
}
read_version() { "$EXE" --version "${SETTINGS_ARGS[@]}" > "$1" 2>&1; return $?; }

tree_counts() { # sets T_TOTAL T_TRACKED T_UNTRACKED
  local records
  records="$(git status --porcelain -uall)"
  if [ -z "$records" ]; then T_TOTAL=0; else T_TOTAL="$(printf '%s\n' "$records" | wc -l | tr -d ' ')"; fi
  T_UNTRACKED="$(printf '%s\n' "$records" | cut -c1-2 | grep -c '^??$' 2>/dev/null || true)"
  [ -n "${T_UNTRACKED-}" ] || T_UNTRACKED=0
  T_TRACKED=$((T_TOTAL - T_UNTRACKED))
}

assert_version_shape() { # <file> <label>
  local f="$1" label="$2"
  ge "$label: the --version answer printed a line 1" "$(count_line "$f" 1)" 1
  ge "$label: the --version answer printed a line 2" "$(count_line "$f" 2)" 1
  ge "$label: the --version answer printed a line 3" "$(count_line "$f" 3)" 1
  ge "$label: the answer carries buildUtc=" "$(count_field "$f" buildUtc)" 1
  ge "$label: the answer carries sourceDateEpoch=" "$(count_field "$f" sourceDateEpoch)" 1
  ge "$label: the answer carries sourceTree=" "$(count_field "$f" sourceTree)" 1
  ge "$label: the answer carries dirty=" "$(count_field "$f" dirty)" 1
  ge "$label: the answer carries dirtyFiles=" "$(count_field "$f" dirtyFiles)" 1
  ge "$label: the answer carries dirtyTracked=" "$(count_field "$f" dirtyTracked)" 1
  ge "$label: the answer carries dirtyUntracked=" "$(count_field "$f" dirtyUntracked)" 1
  eq "$label: buildUtc is an ISO-8601 UTC stamp" \
    "$(grep -cE '(^|[[:space:]])buildUtc=[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}Z' "$f")" "1"
  hasnt "$label: the answer is not the configure-time placeholder" "$(cat "$f")" "configure-placeholder"
  eq "$label: sourceTree is not the placeholder" \
    "$([ "$(ver_field "$f" sourceTree)" = "configure-placeholder" ] && echo placeholder || echo real)" "real"
  # D-54, line 4. Every value assertion is preceded by an existence assertion:
  # "sourceContent == <hash>" would otherwise pass on two empty readings, which
  # is the exact shape GATE-DESIGN-TRAPS.md:47-58 names -- the assertion has to
  # fail when the LINE is missing, not only when the value is wrong.
  ge "$label: the --version answer printed a line 4" "$(count_line "$f" 4)" 1
  ge "$label: line 4 carries a sourceContent=" "$(count_field "$f" sourceContent)" 1
  ge "$label: line 4 carries a contentMethod=" "$(count_field "$f" contentMethod)" 1
  ge "$label: line 4 carries an identity=" "$(count_field "$f" identity)" 1
  eq "$label: sourceContent is a 64-character lowercase sha256, not a word" \
    "$(printf '%s' "$(ver_field "$f" sourceContent)" | grep -cE '^[0-9a-f]{64}$')" "1"
  eq "$label: contentMethod names a method the build can actually produce" \
    "$(printf '%s' "$(ver_field "$f" contentMethod)" | grep -cE '^(git-clean-v1|raw-v1)$')" "1"
  eq "$label: identity says the provenance came from the sidecar" \
    "$(ver_field "$f" identity)" "sidecar"
}

# The core assertion set: the reading must be a functional image of the tree.
assert_identity_matches_tree() { # <file> <label>
  local f="$1" label="$2" head_commit head_epoch expected_dirty
  head_commit="$(git rev-parse --short=12 HEAD)"
  head_epoch="$(git log -1 --format=%ct HEAD)"
  tree_counts
  if [ "$T_TOTAL" -gt 0 ]; then expected_dirty=1; else expected_dirty=0; fi
  echo "  --- tree now: HEAD=$head_commit records=$T_TOTAL tracked=$T_TRACKED untracked=$T_UNTRACKED"
  echo "  --- $label answers: $(sed -n '2p;3p' "$f" | tr -d '\r' | tr '\n' ' ')"
  eq "$label: sourceTree is the commit this build was made from" "$(ver_field "$f" sourceTree)" "$head_commit"
  eq "$label: sourceDateEpoch is that commit's committer epoch" "$(ver_field "$f" sourceDateEpoch)" "$head_epoch"
  eq "$label: dirty is 1 exactly when the worktree has uncommitted changes" \
    "$(ver_field "$f" dirty)" "$expected_dirty"
  eq "$label: dirtyFiles is the porcelain record count git reports now" \
    "$(ver_field "$f" dirtyFiles)" "$T_TOTAL"
  eq "$label: dirtyTracked is the tracked share of that count" \
    "$(ver_field "$f" dirtyTracked)" "$T_TRACKED"
  eq "$label: dirtyUntracked is the untracked share of that count" \
    "$(ver_field "$f" dirtyUntracked)" "$T_UNTRACKED"
}

# D-54: the sidecar is what makes lines 2 and 3 a reading of THIS binary. The
# two assertions with teeth are the last two: the sidecar must name the bytes on
# disk, and the content hash in the sidecar must be the hash compiled into the
# executable. Without them "the sidecar exists" would pass while the file next
# to the executable described a different build.
assert_sidecar_binding() { # <identity-file> <exe> <version-file> <label>
  local sidecar="$1" exe="$2" f="$3" label="$4"
  eq "$label: the sidecar exists beside $(basename "$exe")" \
    "$([ -f "$sidecar" ] && echo present || echo absent)" "present"
  ge "$label: the sidecar carries an exeSha256= reading" \
    "$(grep -cE '^exeSha256=[0-9a-f]{64}$' "$sidecar" 2>/dev/null || true)" 1
  eq "$label: the sidecar names the executable that is on disk" \
    "$(sed -n 's/^exeSha256=//p' "$sidecar" | tr -d '\r')" "$(shasum "$exe")"
  eq "$label: the sidecar's sourceContent is the hash compiled into the executable" \
    "$(sed -n 's/^sourceContent=//p' "$sidecar" | tr -d '\r')" "$(ver_field "$f" sourceContent)"
  eq "$label: the sidecar's sourceTree is the live HEAD" \
    "$(sed -n 's/^sourceTree=//p' "$sidecar" | tr -d '\r')" "$(git rev-parse --short=12 HEAD)"
}

# D-54: recompute the content hash the way the builder claims to, from the git
# index and the working copy, and compare it with the hash the build reported.
# If the two disagree, either the build measured a different set of files or it
# measured them differently -- both are the defect this gate exists to catch.
assert_content_hash_agrees() { # <version-file> <label>
  local f="$1" label="$2" recomputed method
  method="$(bash "$CONTENT_TOOL" method 2>/dev/null)"
  recomputed="$(bash "$CONTENT_TOOL" hash 2>/dev/null)"
  eq "$label: the gate can compute a content hash at all" \
    "$([ -n "$recomputed" ] && echo computed || echo missing)" "computed"
  eq "$label: the gate's method is the method the build reported" \
    "$method" "$(ver_field "$f" contentMethod)"
  eq "$label: the build's sourceContent is the tree's content, recomputed independently" \
    "$recomputed" "$(ver_field "$f" sourceContent)"
}

assert_timestamp_window() { # <file> <label>
  local f="$1" label="$2" head_epoch stamp_epoch exe_mtime sidecar_mtime
  head_epoch="$(git log -1 --format=%ct HEAD)"
  stamp_epoch="$(date -u -d "$(ver_field "$f" buildUtc)" +%s 2>/dev/null)"
  exe_mtime="$(stat -c %Y "$EXE" 2>/dev/null)"
  sidecar_mtime="$(stat -c %Y "$IDENTITY" 2>/dev/null)"
  # D-54 PLAN B CHANGED WHAT buildUtc DESCRIBES, SO THE OLD RULE IS REPLACED.
  #   Before plan B buildUtc was compiled into the executable (a TU constant
  #   stamped at configure time), so it necessarily predated the link and
  #   "buildUtc <= exe mtime" was a real check. Under plan B the same field is
  #   written by the build into the .identity sidecar and can legitimately land
  #   AFTER the link: the sidecar is written POST_BUILD, and a build that
  #   rewrites the provenance without relinking the binary (a docs-only commit,
  #   or the carried case) refreshes buildUtc while the exe keeps its bytes and
  #   its old mtime. The old direction is therefore FALSE BY CONSTRUCTION, not
  #   merely unlucky; keeping it would pin the compile-time model this change
  #   removed. What must still hold -- and what the T90 defect was about -- is
  #   that the number the binary answers with is a time this identity file's own
  #   build wrote, not a value pasted in from somewhere else: the sidecar cannot
  #   predate the clock it reports, and the executable it names by sha was
  #   already linked when that sidecar was written. Both readings are taken from
  #   the same file the product reads, and both are preceded by an existence
  #   check so a missing file cannot pass by comparing two empty values.
  eq "$label: buildUtc parses as a UTC instant" \
    "$([ -n "${stamp_epoch-}" ] && echo yes || echo no)" "yes"
  eq "$label: the executable the stamp describes exists" \
    "$([ -n "${exe_mtime-}" ] && echo present || echo absent)" "present"
  eq "$label: the identity sidecar that reports buildUtc exists" \
    "$([ -n "${sidecar_mtime-}" ] && echo present || echo absent)" "present"
  ge "$label: buildUtc is not earlier than the commit it names" "${stamp_epoch:-0}" "$head_epoch"
  le "$label: buildUtc is not later than the sidecar that reports it" \
    "${stamp_epoch:-1}" "$(( ${sidecar_mtime:-0} + 5 ))"
  le "$label: the executable is not newer than the sidecar that names its sha" \
    "${exe_mtime:-1}" "$(( ${sidecar_mtime:-0} + 5 ))"
}

assert_line_one_contract() { # <file> <label>
  local f="$1" label="$2" declared line1
  declared="$(sed -n 's/^version=//p' VERSION.txt | tr -d '\r' | head -1)"
  line1="$(sed -n '1p' "$f" | tr -d '\r')"
  eq "$label: line 1 keeps the shape install-lifecycle-check.sh parses" \
    "$(printf '%s' "$line1" | sed 's/ versionCode=.*//')" \
    "package=tf2_demo_native version=$declared"
  eq "$label: line 1 ends in a numeric versionCode" \
    "$(printf '%s' "$line1" | grep -cE ' versionCode=[0-9]+$')" "1"
}

pe_timedate_stamp() { # <exe> -> decimal COFF TimeDateStamp, or "unreadable"
  python - "$1" <<'PY'
import struct, sys
try:
    data = open(sys.argv[1], "rb").read()
except OSError:
    print("unreadable"); raise SystemExit
if len(data) < 0x40:
    print("unreadable"); raise SystemExit
offset = struct.unpack_from("<I", data, 0x3C)[0]
if offset + 24 > len(data) or data[offset:offset + 2] != b"PE":
    print("unreadable"); raise SystemExit
print(struct.unpack_from("<I", data, offset + 8)[0])
PY
}

# The other fields a red on bit-reproducibility has to name: the PE checksum and
# the CodeView (RSDS) GUID the linker wrote into the debug directory. /Brepro
# fixes the COFF timestamp; the PDB signature is a separate question, and this
# report answers it instead of assuming it.
pe_report() { # <exe> -> one line naming the fields a reproducibility red must show
  python - "$1" <<'PY'
import struct, sys
try:
    data = open(sys.argv[1], "rb").read()
except OSError:
    print("unreadable"); raise SystemExit
off = struct.unpack_from("<I", data, 0x3C)[0]
if off + 24 > len(data) or data[off:off + 2] != b"PE":
    print("unreadable"); raise SystemExit
timedate = struct.unpack_from("<I", data, off + 8)[0]
opt = off + 24
magic = struct.unpack_from("<H", data, opt)[0]
checksum = struct.unpack_from("<I", data, opt + 64)[0]
dirs = opt + (112 if magic == 0x20B else 96)
rva, size = struct.unpack_from("<II", data, dirs + 6 * 8)
if rva == 0 or size < 28:
    print("timedate=%d checksum=%d debug_timedate=0 cv=none type=0" % (timedate, checksum)); raise SystemExit
nsec = struct.unpack_from("<H", data, off + 6)[0]
optsize = struct.unpack_from("<H", data, off + 20)[0]
st = opt + optsize
sections = []
for i in range(nsec):
    base = st + i * 40
    vsize, vaddr, rawsize, rawptr = struct.unpack_from("<IIII", data, base + 8)
    sections.append((vaddr, vsize, rawptr, rawsize))
def file_offset(r):
    for vaddr, vsize, rawptr, rawsize in sections:
        if vaddr <= r < vaddr + max(vsize, rawsize):
            return rawptr + (r - vaddr)
    return None
fo = file_offset(rva)
if fo is None:
    print("timedate=%d checksum=%d cv=unmapped" % (timedate, checksum)); raise SystemExit
# Data directory 6 points at an ARRAY of IMAGE_DEBUG_DIRECTORY records (28 bytes
# each); the CodeView payload lives at the record's PointerToRawData. Reading
# the directory itself as if it were RSDS is how this function first said
# "unknown" about a binary that simply has no CodeView record at all.
dbg_td = struct.unpack_from("<I", data, fo + 4)[0]
cv_type = struct.unpack_from("<I", data, fo + 12)[0]
cv_ptr = struct.unpack_from("<I", data, fo + 24)[0]
if cv_type != 2 or data[cv_ptr:cv_ptr + 4] != b"RSDS":
    print("timedate=%d checksum=%d debug_timedate=%d cv=none type=%d" % (timedate, checksum, dbg_td, cv_type)); raise SystemExit
guid = data[cv_ptr + 4:cv_ptr + 20].hex()
age = struct.unpack_from("<I", data, cv_ptr + 20)[0]
nul = data.find(b"\x00", cv_ptr + 24)
pdb = data[cv_ptr + 24:nul if nul >= 0 else cv_ptr + 264].decode("latin-1", "replace")
print("timedate=%d checksum=%d debug_timedate=%d cv_guid=%s cv_age=%d pdb=%s" % (timedate, checksum, dbg_td, guid, age, pdb))
PY
}

native_tree_sha() { git ls-tree -r HEAD -- native/ | sha256sum | cut -d' ' -f1; }

if [ "$MODE" = "--reproducible" ]; then
  sec "repro/1 the tree both builds will be made from"
  REPRO_HEAD="$(git rev-parse HEAD)"
  REPRO_HEAD_SHORT="$(git rev-parse --short=12 HEAD)"
  REPRO_TREE_SHA="$(native_tree_sha)"
  tree_counts
  REPRO_RECORDS=$T_TOTAL
  REPRO_T0="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "  --- HEAD=$REPRO_HEAD_SHORT native/ tree sha256=$REPRO_TREE_SHA records=$REPRO_RECORDS"
  eq "repro: the native/ tree is hashed (a sha256, not a placeholder)" \
    "$(printf '%s' "$REPRO_TREE_SHA" | grep -cE '^[0-9a-f]{64}$' || true)" "1"

  sec "repro/2 first clean full build"
  bash build-cmake.sh > "$SCRATCH/repro-build1.log" 2>&1
  R1_RC=$?
  eq "repro: clean full build 1 exit code" "$R1_RC" "0"
  if [ "$R1_RC" -ne 0 ]; then
    report_build_failure "$SCRATCH/repro-build1.log"
    echo "BUILD-REPRODUCIBLE=FAIL (clean build 1 failed rc=$R1_RC)"
    exit 1
  fi
  ge "repro: build 1 compiled the sources (it was not a no-op)" \
    "$(grep -c 'Building CXX object' "$SCRATCH/repro-build1.log" || true)" 100
  eq "repro: build 1 produced the executable" "$([ -f "$EXE" ] && echo present || echo absent)" "present"
  SHA1="$(shasum "$EXE")"
  SIZE1="$(stat -c %s "$EXE" 2>/dev/null)"
  PE1="$(pe_timedate_stamp "$EXE")"
  PE1_REPORT="$(pe_report "$EXE")"
  eq "repro: build 1 has a sha256 reading" \
    "$(printf '%s' "$SHA1" | grep -cE '^[0-9a-f]{64}$' || true)" "1"
  read_version "$SCRATCH/repro1.version"
  eq "repro: build 1 --version exit code" "$?" "0"
  assert_version_shape "$SCRATCH/repro1.version" "repro build 1"
  assert_identity_matches_tree "$SCRATCH/repro1.version" "repro build 1"
  STAMP1="$(shasum "$STAMP")"
  cp "$EXE" "$SCRATCH/repro-build1.exe" 2>/dev/null
  echo "  --- build 1: sha256=$SHA1 size=$SIZE1 pe_timedate=$PE1"
  echo "  --- build 1 PE: $PE1_REPORT"

  sec "repro/3 second clean full build of the same tree"
  bash build-cmake.sh > "$SCRATCH/repro-build2.log" 2>&1
  R2_RC=$?
  eq "repro: clean full build 2 exit code" "$R2_RC" "0"
  if [ "$R2_RC" -ne 0 ]; then
    report_build_failure "$SCRATCH/repro-build2.log"
    echo "BUILD-REPRODUCIBLE=FAIL (clean build 2 failed rc=$R2_RC)"
    exit 1
  fi
  ge "repro: build 2 compiled the sources (it was not a no-op)" \
    "$(grep -c 'Building CXX object' "$SCRATCH/repro-build2.log" || true)" 100
  eq "repro: build 2 produced the executable" "$([ -f "$EXE" ] && echo present || echo absent)" "present"
  SHA2="$(shasum "$EXE")"
  SIZE2="$(stat -c %s "$EXE" 2>/dev/null)"
  PE2="$(pe_timedate_stamp "$EXE")"
  PE2_REPORT="$(pe_report "$EXE")"
  eq "repro: build 2 has a sha256 reading" \
    "$(printf '%s' "$SHA2" | grep -cE '^[0-9a-f]{64}$' || true)" "1"
  read_version "$SCRATCH/repro2.version"
  eq "repro: build 2 --version exit code" "$?" "0"
  assert_version_shape "$SCRATCH/repro2.version" "repro build 2"
  assert_identity_matches_tree "$SCRATCH/repro2.version" "repro build 2"
  STAMP2="$(shasum "$STAMP")"
  echo "  --- build 2: sha256=$SHA2 size=$SIZE2 pe_timedate=$PE2"
  echo "  --- build 2 PE: $PE2_REPORT"

  sec "repro/4 the two builds are the same binary, and the identity is a triple"
  tree_counts
  eq "repro: both builds were made from the same HEAD" "$(git rev-parse HEAD)" "$REPRO_HEAD"
  eq "repro: both builds were made from the same native/ tree hash" "$(native_tree_sha)" "$REPRO_TREE_SHA"
  eq "repro: the tree did not change while the two builds ran" "$T_TOTAL" "$REPRO_RECORDS"
  eq "repro: a rebuild did not rewrite the stamp (buildUtc stayed where it was)" "$STAMP2" "$STAMP1"
  eq "repro: the two clean builds have the same size in bytes" "$SIZE2" "$SIZE1"
  eq "repro: the two clean builds are byte-identical (same sha256)" "$SHA2" "$SHA1"
  eq "repro: the PE COFF TimeDateStamp is identical (the residue that made an sha a link id)" "$PE2" "$PE1"
  eq "repro: both builds answer --version with the same bytes" \
    "$(cat "$SCRATCH/repro2.version")" "$(cat "$SCRATCH/repro1.version")"
  if [ "$SHA2" != "$SHA1" ]; then
    {
      echo "build1_sha256=$SHA1"; echo "build2_sha256=$SHA2"
      echo "build1_pe=$PE1_REPORT"; echo "build2_pe=$PE2_REPORT"
      echo "--- first differing bytes (cmp -l; byte offset 1-based, values octal) ---"
      cmp -l "$SCRATCH/repro-build1.exe" "$EXE" 2>&1 | head -10
    } > "$SCRATCH/repro-diff.txt" 2>&1
    echo "  --- the residue, named rather than explained away:"
    sed 's/^/    /' "$SCRATCH/repro-diff.txt"
  fi

  REPRO_RECORD="$SCRATCH/reproducible.txt"
  {
    echo "exe=$EXE"
    echo "exe_sha256=$SHA1"
    echo "exe_sha256_build2=$SHA2"
    echo "exe_size_bytes=$SIZE1"
    echo "pe_coff_timedate=$PE1"
    echo "git_head=$REPRO_HEAD"
    echo "git_head_short=$REPRO_HEAD_SHORT"
    echo "native_tree_sha256=$REPRO_TREE_SHA"
    echo "dirty_files=$REPRO_RECORDS"
    echo "builds=2"
    echo "builder_wallclock_start=$REPRO_T0"
    echo "gate_script_sha256=$(shasum "$0")"
  } > "$REPRO_RECORD"
  ge "repro: the identity record has the fields it promises" "$(grep -c . "$REPRO_RECORD" || true)" 8
  eq "repro: the record pairs the exe sha256 with a full git HEAD" \
    "$(grep -c '^git_head=[0-9a-f]\{40\}$' "$REPRO_RECORD" || true)" "1"
  eq "repro: the record pairs it with a native/ tree sha256" \
    "$(grep -c '^native_tree_sha256=[0-9a-f]\{64\}$' "$REPRO_RECORD" || true)" "1"
  eq "repro: the recorded exe sha256 is the executable on disk" \
    "$(sed -n 's/^exe_sha256=//p' "$REPRO_RECORD" | head -1)" "$SHA2"
  eq "repro: the recorded HEAD is the live HEAD" \
    "$(sed -n 's/^git_head=//p' "$REPRO_RECORD")" "$REPRO_HEAD"
  eq "repro: the recorded native/ hash is what git hashes now" \
    "$(sed -n 's/^native_tree_sha256=//p' "$REPRO_RECORD")" "$REPRO_TREE_SHA"

  mkdir -p "$OUT"
  copy_out "$SCRATCH/reproducible.txt" reproducible.txt
  copy_out "$SCRATCH/repro-build1.log" repro-build1.log
  copy_out "$SCRATCH/repro-build2.log" repro-build2.log
  copy_out "$SCRATCH/repro1.version" version-repro-build1.txt
  copy_out "$SCRATCH/repro2.version" version-repro-build2.txt
  if [ -f "$SCRATCH/repro-diff.txt" ]; then copy_out "$SCRATCH/repro-diff.txt" repro-diff.txt; fi
  eq "evidence: every copy reached the output directory" "$COPY_FAIL" 0
  eq "evidence: the output directory holds the readings this run promises" \
    "$(evidence_missing reproducible.txt repro-build1.log repro-build2.log \
        version-repro-build1.txt version-repro-build2.txt)" 0
  if [ "$FAIL" -ne 0 ]; then
    echo "BUILD-REPRODUCIBLE=FAIL ($FAIL of $CHECKS assertions red; build1 sha=$SHA1 pe=$PE1, build2 sha=$SHA2 pe=$PE2)"
    exit 1
  fi
  echo "BUILD-REPRODUCIBLE=PASS builds=2 sha=$SHA1"
  exit 0
fi

# --- 1/5 baseline -----------------------------------------------------------
sec "1/5 build the product and the probe, then read what the binary says about itself"
build_targets baseline
BASE_BUILD_RC=$?
eq "baseline build exit code" "$BASE_BUILD_RC" "0"
if [ "$BASE_BUILD_RC" -ne 0 ]; then
  report_build_failure "$SCRATCH/build-baseline.log"
  echo "BUILD-IDENTITY=FAIL (the build did not succeed; nothing below could be read)"
  exit 1
fi
eq "the build produced $EXE" "$([ -f "$EXE" ] && echo present || echo absent)" "present"
eq "the build produced $PROBE" "$([ -f "$PROBE" ] && echo present || echo absent)" "present"

read_version "$SCRATCH/baseline.version"
BASE_VERSION_RC=$?
eq "product --version exit code" "$BASE_VERSION_RC" "0"
echo "  --- baseline --version ---"
sed 's/^/    /' "$SCRATCH/baseline.version"
assert_version_shape "$SCRATCH/baseline.version" "baseline"
assert_identity_matches_tree "$SCRATCH/baseline.version" "baseline"
assert_timestamp_window "$SCRATCH/baseline.version" "baseline"
assert_line_one_contract "$SCRATCH/baseline.version" "baseline"
assert_sidecar_binding "$IDENTITY" "$EXE" "$SCRATCH/baseline.version" "baseline"
assert_content_hash_agrees "$SCRATCH/baseline.version" "baseline"

BASE_SHA="$(shasum "$EXE")"
BASE_STAMP_SHA="$(shasum "$STAMP")"
BASE_IDENTITY_SHA="$(shasum "$IDENTITY")"
BASE_PROVENANCE_SHA="$(shasum "$PROVENANCE")"
BASE_CONTENT="$(ver_field "$SCRATCH/baseline.version" sourceContent)"
BASE_STAMP_EPOCH="$(date -u -d "$(ver_field "$SCRATCH/baseline.version" buildUtc)" +%s 2>/dev/null)"
BASE_VERSION="$(cat "$SCRATCH/baseline.version")"
tree_counts
BASE_TOTAL=$T_TOTAL
BASE_TRACKED=$T_TRACKED
BASE_UNTRACKED=$T_UNTRACKED
echo "  --- baseline exe sha256=$BASE_SHA stamp sha256=$BASE_STAMP_SHA records=$BASE_TOTAL"
eq "the stamp file is where the build put it" "$([ -f "$STAMP" ] && echo present || echo absent)" "present"

sec "1b/5 the probe reports the same identity as the product"
# The probe is not the product (it links install_lifecycle.cpp only and opens no
# settings file); it also rejects arguments it does not know, so no overrides.
"$PROBE" --version > "$SCRATCH/probe.version" 2>&1
PROBE_RC=$?
eq "install_cli_probe --version exit code" "$PROBE_RC" "0"
ge "the probe printed a line 1" "$(count_line "$SCRATCH/probe.version" 1)" 1
ge "the probe printed dirty=" "$(count_field "$SCRATCH/probe.version" dirty)" 1
eq "install_cli_probe agrees with the product on line 1" \
  "$(sed -n '1p' "$SCRATCH/probe.version" | tr -d '\r' | sed 's/ versionCode=.*//')" \
  "$(sed -n '1p' "$SCRATCH/baseline.version" | tr -d '\r' | sed 's/ versionCode=.*//')"
eq "install_cli_probe reports the same sourceTree" \
  "$(ver_field "$SCRATCH/probe.version" sourceTree)" "$(ver_field "$SCRATCH/baseline.version" sourceTree)"
eq "install_cli_probe reports the same dirty state" \
  "$(ver_field "$SCRATCH/probe.version" dirty)" "$(ver_field "$SCRATCH/baseline.version" dirty)"
# D-54: the probe reads its OWN sidecar (install_cli_probe.identity). Binding it
# to its own bytes is what makes "the probe agrees with the product" a
# statement about two independent readings rather than one file read twice.
eq "install_cli_probe reports the same line 4 as the product" \
  "$(sed -n '4p' "$SCRATCH/probe.version" | tr -d '\r')" \
  "$(sed -n '4p' "$SCRATCH/baseline.version" | tr -d '\r')"
assert_sidecar_binding "$PROBE_IDENTITY" "$PROBE" "$SCRATCH/probe.version" "probe"


# --- 1c/5 the argument surface ----------------------------------------------
# D-23 (2026-11-07): the loop at native/src/main.cpp:2523-3054 had no final
# else, so an argument the product did not understand vanished without a word.
# This wave already lost two gates to that silence -- one drove
# "--no-player-animation", a switch the product never had, and one passed a bare
# "--player-weapon-attach", which main.cpp:3173-3178 refuses -- and neither run
# could tell "the switch did nothing" from "there is no such switch". The loop
# now prints one line on EVERY run,
#     args: unrecognized=N first=<name> all=<name>,...
# and --strict-args turns N>0 into stderr + exit 2 before startup. The three
# runs below are that line's whole contract: an unknown argument is COUNTED, the
# default run still IGNORES it, and --strict-args on a command line the product
# understands changes nothing.
sec "1c/5 an argument the product does not know is a reading, not silence"
QUIT_SCRIPT="$SCRATCH/quit.camera"
printf '%s\n' '# one command per line; "quit" is the script mode own exit.' 'quit' > "$QUIT_SCRIPT"
UNKNOWN_FLAG="--definitely-not-a-flag"
trace_text() { tr -d '\r' < "$1" 2>/dev/null; }

# (a) COUNTED, and still ignored by default. The command also carries a value
# the product refuses (--playback-speed 0, main.cpp:3159-3165), so the default
# run terminates on its own with no window, no device and no demo: rc=13 is that
# value refusal, asserted because it is the default this wave must NOT change --
# a product that started refusing unknown arguments on its own would answer 2
# here instead.
ARGS_UNKNOWN_TRACE="$SCRATCH/args-unknown.trace"
rm -f "$ARGS_UNKNOWN_TRACE" "$SCRATCH/args-unknown.log"
TF2_NATIVE_TRACE="$ARGS_UNKNOWN_TRACE" "$EXE" "${SETTINGS_ARGS[@]}" \
  --hidden-window --audio-disabled "$UNKNOWN_FLAG" --playback-speed 0 \
  > "$SCRATCH/args-unknown.log" 2>&1
UNKNOWN_RC=$?
eq "arg surface: an unknown argument is still ignored, rc=13 is the value refusal" "$UNKNOWN_RC" "13"
ge "arg surface: the run wrote a trace file with lines" \
  "$([ -f "$ARGS_UNKNOWN_TRACE" ] && wc -l < "$ARGS_UNKNOWN_TRACE" | tr -d ' ' || echo 0)" 1
eq "arg surface: the trace has exactly one args: line" \
  "$(trace_text "$ARGS_UNKNOWN_TRACE" | grep -c '^[0-9.]\+ args: ' || true)" "1"
eq "arg surface: the unknown argument is counted (args: unrecognized=1)" \
  "$(trace_text "$ARGS_UNKNOWN_TRACE" | grep -c 'args: unrecognized=1 ' || true)" "1"
# The end anchor below is "$", not "[$]": a bracket expression matches a
# literal dollar character, so "all=<flag>[$]" counts zero while looking
# anchored. The gate's first forward run failed exactly this way, and its two
# red lines printed the correct trace line right under the failure.
eq "arg surface: that line names the argument in first= and all=" \
  "$(trace_text "$ARGS_UNKNOWN_TRACE" | grep -c "args: unrecognized=1 first=$UNKNOWN_FLAG all=$UNKNOWN_FLAG$" || true)" "1"
# D-67 (AUDIT-GATE-DIFFS W2): the four evidence files around this run are copied
# under the "present" rule (a refusal path may print nothing), so an empty log
# alone could not tell a healthy run from a truncated one. The reading lives in
# the trace, asserted above; what the log itself must prove is that it is EMPTY.
# A product that started printing here would be a different product, and this
# line is what makes "present" honest instead of merely lenient.
eq "arg surface: the default run prints nothing (an empty capture is the expectation)" \
  "$([ -s "$SCRATCH/args-unknown.log" ] && echo 1 || echo 0)" "0"

# (b) --strict-args refuses it, by name, before the startup work that the trace
# would otherwise record (the "resources: requested=" line is emitted after the
# argument loop and before any device exists).
ARGS_STRICT_TRACE="$SCRATCH/args-strict.trace"
rm -f "$ARGS_STRICT_TRACE" "$SCRATCH/args-strict.log" "$SCRATCH/args-strict.err"
TF2_NATIVE_TRACE="$ARGS_STRICT_TRACE" "$EXE" "${SETTINGS_ARGS[@]}" \
  --hidden-window --audio-disabled "$UNKNOWN_FLAG" --playback-speed 0 --strict-args \
  > "$SCRATCH/args-strict.log" 2> "$SCRATCH/args-strict.err"
STRICT_RC=$?
eq "arg surface: --strict-args refuses the unknown argument (rc 2)" "$STRICT_RC" "2"
ge "arg surface: the refusal reached stderr" \
  "$([ -s "$SCRATCH/args-strict.err" ] && wc -l < "$SCRATCH/args-strict.err" | tr -d ' ' || echo 0)" 1
eq "arg surface: the refusal names the argument the caller wrote" \
  "$(grep -c -- "$UNKNOWN_FLAG" "$SCRATCH/args-strict.err" || true)" "1"
eq "arg surface: the strict run still printed its args: line" \
  "$(trace_text "$ARGS_STRICT_TRACE" | grep -c 'args: unrecognized=1 ' || true)" "1"
hasnt "arg surface: the strict exit happens before startup resolves assets" \
  "$(trace_text "$ARGS_STRICT_TRACE")" "resources: requested="

# (c) On a command line the product understands, --strict-args must not change
# anything -- including the exit status. The script "quit" is the product's own
# way out of the loop (main.cpp:11308-11311 sets done, main.cpp:11756 returns
# g_cameraScriptExitCode, 0 when no step failed), so this run needs no demo and
# writes no capture; timeout 180 turns "the loop never ended" into rc 124
# instead of a hung gate.
ARGS_CLEAN_TRACE="$SCRATCH/args-clean.trace"
rm -f "$ARGS_CLEAN_TRACE" "$SCRATCH/args-clean.log"
TF2_NATIVE_TRACE="$ARGS_CLEAN_TRACE" timeout 180 "$EXE" "${SETTINGS_ARGS[@]}" \
  --hidden-window --audio-disabled --strict-args --camera-script "$QUIT_SCRIPT" \
  > "$SCRATCH/args-clean.log" 2>&1
CLEAN_RC=$?
eq "arg surface: a command line the product understands is not refused (rc 0)" "$CLEAN_RC" "0"
eq "arg surface: that run counts zero unrecognised arguments" \
  "$(trace_text "$ARGS_CLEAN_TRACE" | grep -c 'args: unrecognized=0 ' || true)" "1"
eq "arg surface: with nothing to list, first= and all= say so (-)" \
  "$(trace_text "$ARGS_CLEAN_TRACE" | grep -c 'args: unrecognized=0 first=- all=-$' || true)" "1"
# --- 2/5 no-op rebuild ------------------------------------------------------
sec "2/5 a rebuild with no source change must neither relink nor move buildUtc"
build_targets noop
NOOP_RC=$?
eq "no-op build exit code" "$NOOP_RC" "0"
if [ "$NOOP_RC" -ne 0 ]; then report_build_failure "$SCRATCH/build-noop.log"; fi
read_version "$SCRATCH/noop.version"
eq "no-op --version exit code" "$?" "0"
eq "a no-op rebuild does not relink the product (same exe sha256)" "$(shasum "$EXE")" "$BASE_SHA"
eq "a no-op rebuild does not rewrite the stamp (same stamp sha256)" "$(shasum "$STAMP")" "$BASE_STAMP_SHA"
eq "a no-op rebuild answers with the same bytes" "$(cat "$SCRATCH/noop.version")" "$BASE_VERSION"
# D-54: the sidecar and the provenance are rewritten by every build (the stamp
# target runs unconditionally), so "unchanged" here is a claim about content, not
# about the file not being touched: the copied-in values must be identical, and
# buildUtc must be the one carried forward from the previous provenance rather
# than the clock at this instant.
eq "D-54: a no-op rebuild does not rewrite the sidecar (same sha256)" \
  "$(shasum "$IDENTITY")" "$BASE_IDENTITY_SHA"
eq "D-54: a no-op rebuild does not rewrite the provenance (buildUtc carried)" \
  "$(shasum "$PROVENANCE")" "$BASE_PROVENANCE_SHA"

# --- 3/5 a dirty tree the compiler never sees --------------------------------
# D-54 inverts T90's assertion here. T90 required a dirty tree to RELINK the
# product, which is the defect restated as a requirement: a change to docs/
# cannot alter one instruction of the binary, and making it relink is what
# invalidated every cache keyed on the executable's bytes for nothing. The
# reading must move; the bytes must not.
sec "3/5 a docs-only change must move the reading and leave the bytes alone"
PROBE_TRACKED="$REPO/docs/RELEASE-POLICY.md"
PROBE_UNTRACKED="$REPO/evidence/.build-identity-check-probe.txt"
cp "$PROBE_TRACKED" "$SCRATCH/probe-doc.backup"
PROBE_DOC_SHA="$(shasum "$PROBE_TRACKED")"
printf '\n<!-- build-identity-check.sh probe: one tracked change; must never be committed -->\n' >> "$PROBE_TRACKED"
printf 'build-identity-check.sh probe\n' > "$PROBE_UNTRACKED"
tree_counts
DOCS_DIRTY_TOTAL=$T_TOTAL
echo "  --- probe added 1 tracked change (docs/RELEASE-POLICY.md) and 1 untracked file"
eq "the docs probe added exactly two records to the tree" "$((DOCS_DIRTY_TOTAL - BASE_TOTAL))" "2"

build_targets dirty
DOCS_RC=$?
eq "docs-dirty build exit code" "$DOCS_RC" "0"
if [ "$DOCS_RC" -ne 0 ]; then report_build_failure "$SCRATCH/build-dirty.log"; fi
read_version "$SCRATCH/dirty.version"
eq "docs-dirty --version exit code" "$?" "0"
echo "  --- docs-dirty --version ---"
sed 's/^/    /' "$SCRATCH/dirty.version"
assert_version_shape "$SCRATCH/dirty.version" "dirty"
assert_identity_matches_tree "$SCRATCH/dirty.version" "dirty"
assert_sidecar_binding "$IDENTITY" "$EXE" "$SCRATCH/dirty.version" "dirty"
assert_content_hash_agrees "$SCRATCH/dirty.version" "dirty"
eq "D-54: a docs-only change did NOT relink the product (exe sha256 unmoved)" \
  "$(shasum "$EXE")" "$BASE_SHA"
eq "D-54: the docs-only change still moved the reading (line 3 differs)" \
  "$([ "$(sed -n '3p' "$SCRATCH/dirty.version" | tr -d '\r')" != "$(sed -n '3p' "$SCRATCH/baseline.version" | tr -d '\r')" ] && echo differs || echo same)" "differs"
eq "D-54: the content hash did not move for a docs-only change" \
  "$(ver_field "$SCRATCH/dirty.version" sourceContent)" "$BASE_CONTENT"
eq "D-54: the rewritten source is unchanged too (the stamp TU is content only)" \
  "$(shasum "$STAMP")" "$BASE_STAMP_SHA"
eq "D-54: the sidecar reports the probe's record count" \
  "$(sed -n 's/^dirtyFiles=//p' "$IDENTITY" | tr -d '\r')" "$DOCS_DIRTY_TOTAL"

# --- 3b/5 a dirty tree the compiler does see --------------------------------
# The other half of the same claim, and the one the old gate never tested: a
# change to a COMPILED input must move the bytes and the content hash together.
# The probe edits one string literal (a byte in .rdata, guaranteed to change the
# image) and is restored byte-identically afterwards -- the restore is checked
# with cmp AND with sha256, because a restore that "looks" right is not one.
sec "3b/5 a change to a compiled input must move the bytes and the content hash"
PROBE_SOURCE="$REPO/native/src/main.cpp"
cp "$PROBE_SOURCE" "$SCRATCH/probe-source.backup"
PROBE_SOURCE_SHA="$(shasum "$PROBE_SOURCE")"
python - "$PROBE_SOURCE" <<'PY'
import sys
path = sys.argv[1]
with open(path, "r", encoding="utf-8", newline="") as stream:
    data = stream.read()
# The needle is the whole statement, not the bare word: the word appears in a
# comment and in a second comparison, and a probe that silently edits three
# places is not a one-byte probe.
needle = 'result = "suppressed-by-focus";'
if data.count(needle) != 1:
    sys.exit("compiled-input probe: expected exactly one occurrence of " + needle)
with open(path, "w", encoding="utf-8", newline="") as stream:
    stream.write(data.replace(needle, 'result = "suppressed-by-focus-d54-probe";', 1))
PY
PROBE_SOURCE_APPLY_RC=$?
eq "the compiled-input probe was applied exactly once" "$PROBE_SOURCE_APPLY_RC" "0"
if [ "$PROBE_SOURCE_APPLY_RC" -ne 0 ]; then
  echo "BUILD-IDENTITY=FAIL (the compiled-input probe could not be applied; nothing was measured)"
  exit 1
fi
build_targets source
SOURCE_RC=$?
eq "compiled-input-dirty build exit code" "$SOURCE_RC" "0"
if [ "$SOURCE_RC" -ne 0 ]; then report_build_failure "$SCRATCH/build-source.log"; fi
read_version "$SCRATCH/source.version"
eq "compiled-input-dirty --version exit code" "$?" "0"
assert_version_shape "$SCRATCH/source.version" "source"
assert_sidecar_binding "$IDENTITY" "$EXE" "$SCRATCH/source.version" "source"
assert_content_hash_agrees "$SCRATCH/source.version" "source"
eq "D-54: a compiled-input change DID relink the product (exe sha256 moved)" \
  "$([ "$(shasum "$EXE")" != "$BASE_SHA" ] && echo moved || echo same)" "moved"
eq "D-54: a compiled-input change moved the content hash" \
  "$([ "$(ver_field "$SCRATCH/source.version" sourceContent)" != "$BASE_CONTENT" ] && echo moved || echo same)" "moved"
cp "$SCRATCH/probe-source.backup" "$PROBE_SOURCE"
cmp -s "$SCRATCH/probe-source.backup" "$PROBE_SOURCE"
eq "the compiled-input probe is restored byte-identically" "$?" "0"
eq "the restored source has its original sha256" "$(shasum "$PROBE_SOURCE")" "$PROBE_SOURCE_SHA"
rm -f "$SCRATCH/probe-source.backup"
build_targets source-restored
eq "restored-after-source build exit code" "$?" "0"
read_version "$SCRATCH/source-restored.version"
eq "D-54: restoring the source restores the executable byte-identically" \
  "$(shasum "$EXE")" "$BASE_SHA"
eq "D-54: restoring the source restores the content hash" \
  "$(ver_field "$SCRATCH/source-restored.version" sourceContent)" "$BASE_CONTENT"

# --- 4/5 restoration --------------------------------------------------------
sec "4/5 restoring the tree must restore the identity"
rm -f "$PROBE_UNTRACKED"
PROBE_UNTRACKED=""
cp "$SCRATCH/probe-doc.backup" "$PROBE_TRACKED"
cmp -s "$SCRATCH/probe-doc.backup" "$PROBE_TRACKED"
eq "the probe's tracked file is restored byte-identically" "$?" "0"
eq "the restored tracked file has its original sha256" "$(shasum "$PROBE_TRACKED")" "$PROBE_DOC_SHA"
rm -f "$SCRATCH/probe-doc.backup"

build_targets restored
RESTORED_RC=$?
eq "restored build exit code" "$RESTORED_RC" "0"
if [ "$RESTORED_RC" -ne 0 ]; then report_build_failure "$SCRATCH/build-restored.log"; fi
read_version "$SCRATCH/restored.version"
eq "restored --version exit code" "$?" "0"
echo "  --- restored --version ---"
sed 's/^/    /' "$SCRATCH/restored.version"
assert_version_shape "$SCRATCH/restored.version" "restored"
assert_identity_matches_tree "$SCRATCH/restored.version" "restored"
tree_counts
eq "the gate left the tree with the record count it found" "$T_TOTAL" "$BASE_TOTAL"
eq "the gate left the tree with the tracked/untracked split it found" \
  "$T_TRACKED/$T_UNTRACKED" "$BASE_TRACKED/$BASE_UNTRACKED"
strip_build_utc() { sed -n '2p;3p' "$1" | tr -d '\r' | sed 's/buildUtc=[^ ]* //' | tr '\n' ' '; }
eq "the restored identity fields match the pre-probe identity" \
  "$(strip_build_utc "$SCRATCH/restored.version")" "$(strip_build_utc "$SCRATCH/baseline.version")"
RESTORED_EPOCH="$(date -u -d "$(ver_field "$SCRATCH/restored.version" buildUtc)" +%s 2>/dev/null)"
ge "the restored build's buildUtc did not move backwards" "${RESTORED_EPOCH:-0}" "${BASE_STAMP_EPOCH:-0}"
# D-54: kDirtyFiles[] no longer exists -- the stamp TU carries the content
# hash only. The dirty provenance moved to the .identity file, which IS a
# key=value file, so the reading is grepped there. The existence assertion comes
# first for the same reason as always: "dirtyFiles == <n>" would otherwise pass
# on a missing line, which is exactly the trap GATE-DESIGN-TRAPS.md:47-58 names.
ge "the restored provenance carries a dirtyFiles reading at all" \
  "$(grep -cE '^dirtyFiles=[0-9]+$' "$PROVENANCE" 2>/dev/null || true)" 1
eq "the restored provenance carries the pre-probe record count ($BASE_TOTAL)" \
  "$(sed -n 's/^dirtyFiles=//p' "$PROVENANCE" | tr -d '\r')" "$BASE_TOTAL"
eq "the provenance is not left carrying the docs probe's record count" \
  "$(grep -cE "^dirtyFiles=$DOCS_DIRTY_TOTAL\$" "$PROVENANCE" 2>/dev/null || true)" "0"
eq "the restored sidecar reports the pre-probe record count" \
  "$(sed -n 's/^dirtyFiles=//p' "$IDENTITY" | tr -d '\r')" "$BASE_TOTAL"
assert_sidecar_binding "$IDENTITY" "$EXE" "$SCRATCH/restored.version" "restored"
assert_content_hash_agrees "$SCRATCH/restored.version" "restored"

# --- 4b/5 what an identity is ------------------------------------------------
sec "4b/5 the identity record: exe sha + HEAD + native/ tree hash, never the sha alone"
HEAD_FULL="$(git rev-parse HEAD)"
HEAD_SHORT="$(git rev-parse --short=12 HEAD)"
NATIVE_TREE_SHA="$(git ls-tree -r HEAD -- native/ | sha256sum | cut -d' ' -f1)"
tree_counts
IDENTITY_RECORD="$SCRATCH/source-identity.txt"
{
  echo "exe=$EXE"
  echo "exe_sha256=$(shasum "$EXE")"
  echo "exe_size_bytes=$(stat -c %s "$EXE" 2>/dev/null)"
  echo "git_head=$HEAD_FULL"
  echo "git_head_short=$HEAD_SHORT"
  echo "native_tree_sha256=$NATIVE_TREE_SHA"
  echo "dirty_files=$T_TOTAL"
  echo "version_line2=$(sed -n '2p' "$SCRATCH/restored.version" | tr -d '\r')"
  echo "version_line3=$(sed -n '3p' "$SCRATCH/restored.version" | tr -d '\r')"
  echo "version_line4=$(sed -n '4p' "$SCRATCH/restored.version" | tr -d '\r')"
  echo "identity_sidecar_sha256=$(shasum "$IDENTITY")"
  echo "provenance_sha256=$(shasum "$PROVENANCE")"
  echo "content_tool_sha256=$(shasum "$CONTENT_TOOL")"
  echo "gate_script_sha256=$(shasum "$0")"
} > "$IDENTITY_RECORD"
ge "identity: the record has the fields it promises" "$(grep -c . "$IDENTITY_RECORD" || true)" 7
eq "identity: the record pairs the exe sha256 with a full git HEAD" \
  "$(grep -c '^git_head=[0-9a-f]\{40\}$' "$IDENTITY_RECORD" || true)" "1"
eq "identity: the record pairs it with a native/ tree sha256" \
  "$(grep -c '^native_tree_sha256=[0-9a-f]\{64\}$' "$IDENTITY_RECORD" || true)" "1"
eq "identity: the recorded exe sha256 is the executable on disk" \
  "$(sed -n 's/^exe_sha256=//p' "$IDENTITY_RECORD")" "$(shasum "$EXE")"
eq "identity: the recorded HEAD is the live HEAD" \
  "$(sed -n 's/^git_head=//p' "$IDENTITY_RECORD")" "$HEAD_FULL"
eq "identity: the recorded native/ hash is what git hashes now" \
  "$(sed -n 's/^native_tree_sha256=//p' "$IDENTITY_RECORD")" "$NATIVE_TREE_SHA"
eq "identity: the version's sourceTree is the short form of that same HEAD" \
  "$(ver_field "$SCRATCH/restored.version" sourceTree)" "$HEAD_SHORT"
eq "identity: the recorded content hash is a sha256, not a word" \
  "$(grep -c '^sourceContent=[0-9a-f]\{64\}$' "$IDENTITY")" "1"
eq "identity: the recorded sidecar hash is the sidecar on disk" \
  "$(sed -n 's/^identity_sidecar_sha256=//p' "$IDENTITY_RECORD")" "$(shasum "$IDENTITY")"
eq "identity: the sidecar beside the executable binds it to this HEAD" \
  "$(sed -n 's/^sourceTree=//p' "$IDENTITY" | tr -d '\r')" "$HEAD_SHORT"

# --- 5/5 summary ------------------------------------------------------------
sec "5/5 what the assertions above established, and what they did not"
echo "  NOT-PROVEN cross-machine reproducibility: --reproducible measured two clean full builds on THIS machine only"
# D-54 replaced the old "NOT-PROVEN a content hash" line: line 4 now IS a hash of
# the compiled inputs, and the gate recomputes it independently above. What is
# still not proven is smaller and worth naming precisely.
echo "  NOT-PROVEN that the content hash covers the toolchain: same sources, same hash, different compiler = different bytes"
echo "  NOT-PROVEN that raw-v1 and git-clean-v1 hashes are comparable: they are not, and contentMethod is the only thing that says which one you have"
echo "  NOT-PROVEN that an installed product keeps its sidecar: install-lifecycle-check.sh's subject, and a package without the .identity file answers identity=missing"
echo "  NOT-PROVEN the manifest/VERSION.txt agree with --version (install-lifecycle-check.sh's subject)"
echo "  NOT-PROVEN that a dirty build can be rebuilt from the dirty tree (it is not recorded, only counted)"
# Counted before the evidence-copy assertions, which run below on purpose (a red
# run must still leave its readings). The verdict line carries the total.
echo "  assertions_so_far=$CHECKS failed_so_far=$FAIL (the evidence assertions below are counted after this line; the verdict line carries the total)"

if [ "$MODE" = "--forward" ]; then
  # Evidence is copied out now, after the last reading, so the gate never dirties
  # the tree it measured -- and it is copied whether the suite is green or red:
  # a red run is exactly the run whose readings someone will want to read, and
  # the 2026-11-09 RUN19 loss showed what "PASS with no files" costs. The OUT
  # directory is outside SCRATCH by construction (see the guard above).
  copy_out "$SCRATCH/baseline.version" version-baseline.txt
  copy_out "$SCRATCH/dirty.version" version-dirty-probe.txt
  copy_out "$SCRATCH/restored.version" version-restored.txt
  copy_out "$SCRATCH/probe.version" probe-version.txt
  copy_out "$SCRATCH/args-unknown.trace" args-unknown.trace
  copy_out "$SCRATCH/args-unknown.log" args-unknown.log
  copy_out "$SCRATCH/args-strict.err" args-strict.err
  copy_out "$SCRATCH/args-strict.trace" args-strict.trace
  copy_out "$SCRATCH/args-clean.trace" args-clean.trace
  copy_out "$SCRATCH/quit.camera" quit.camera
  copy_out "$SCRATCH/source-identity.txt" source-identity.txt
  copy_out "$SCRATCH/source.version" version-source-probe.txt
  copy_out "$SCRATCH/source-restored.version" version-source-restored.txt
  copy_out "$IDENTITY" identity-sidecar.txt
  copy_out "$PROVENANCE" provenance.txt
  { echo "exe=$EXE"; echo "exe_sha256=$BASE_SHA"; echo "stamp_sha256=$BASE_STAMP_SHA"
    echo "records_at_baseline=$BASE_TOTAL"; echo "head=$(git rev-parse HEAD)"
    echo "head_full=$HEAD_FULL"; echo "native_tree_sha256=$NATIVE_TREE_SHA"
    echo "gate_script_sha256=$(shasum "$0")"
    echo "main_cpp_sha256=$(shasum "$REPO/native/src/main.cpp")"
    echo "args_unknown=$OUT/args-unknown.trace"
    echo "args_strict_stderr=$OUT/args-strict.err"
    echo "args_clean=$OUT/args-clean.trace"
  } > "$OUT/readings.txt"
  # The verdict is only allowed to be PASS if the evidence it names is really on
  # disk. COPY_FAIL counts failed copies (the 2026-11-09 failure mode: OUT was
  # deleted, every cp failed, the gate printed PASS); the second assertion checks
  # the fifteen promised files exist and are non-empty, so a missing directory
  # cannot pass through this either.
  eq "evidence: every copy reached the output directory" "$COPY_FAIL" 0
  eq "evidence: the output directory holds the readings this run promises" \
    "$(( $(evidence_missing present args-unknown.log args-strict.err) \
       + $(evidence_missing nonempty version-baseline.txt version-dirty-probe.txt \
           version-restored.txt probe-version.txt version-source-probe.txt \
           version-source-restored.txt identity-sidecar.txt provenance.txt \
           args-unknown.trace args-strict.trace args-clean.trace quit.camera \
           source-identity.txt readings.txt) ))" 0
  ge "evidence: the run copied as many files as the verdict names" "$COPY_N" 15
  # Merge note (candidate/t102 x D-54 ba628c6): both sides changed this block.
  # Incoming widened the promised-file list from 11 to 15 because the identity
  # sidecar arrives with its own evidence files; local added the D-72 floor.
  # Keep BOTH: the wider list is the newer evidence contract, and the floor is
  # the only thing stopping a run in which whole sections never executed from
  # reading exactly like a green one. The floor stays at 100 -- measured 219 on
  # the D-54 tree and 118 on the pre-D-54 tree, so 100 is below both.
  # D-72 (D68-SCAN K3): the verdict printed $CHECKS but nothing tied it to a
  # floor, so a forward run in which whole sections were skipped -- or in which
  # eq() quietly stopped being called -- printed "assertions=3" and exited 0.
  # Floor = 100, i.e. 85% of the measured 118 on the integrated tree
  # (measured 2026-11-10 from /d/_shared/EXEC-2026-11-09/t46-buildid/run-20261109T160357Z.log).
  if [ "$CHECKS" -lt 100 ]; then
    echo "  FAIL assertions: only $CHECKS assertions ran, below the floor 100 -- sections that stopped running read exactly like a green run (D-72)"
    FAIL=$((FAIL + 1))
  fi
  if [ "$FAIL" -ne 0 ]; then
    echo "BUILD-IDENTITY=FAIL assertions=$CHECKS red=$FAIL (evidence: $OUT)"
    exit 1
  fi
  echo "BUILD-IDENTITY=PASS assertions=$CHECKS identity=exe-sha+HEAD+native-tree-sha head=$HEAD_SHORT evidence=$OUT"
  exit 0
fi

# A mode that did not exit above (--mutation) still has to answer for a red
# forward suite before it scores mutants against it.
if [ "$FAIL" -ne 0 ]; then
  echo "BUILD-IDENTITY=FAIL assertions=$CHECKS red=$FAIL"
  exit 1
fi

# --- mutations --------------------------------------------------------------
# Four mutants, one named assertion each. M1, M2 and M4 edit
# native/cmake/gen-build-stamp.cmake; M3 edits native/src/main.cpp.
#
# WHY THE OLD "the mutant must relink" GUARD IS GONE (D-54; was :904-905 and
# :947-948). T90 scored a mutant as caught only if the executable changed, which
# made sense while HEAD and the dirty counts were C++ constants. They are not
# constants any more: a mutant that forges the provenance changes a key=value
# file, and a mutant that forges the dirty counts changes a key=value file.
# Keeping that guard would have declared such a mutant invalid (exit 90) for
# doing exactly what the design wants it to do -- and because native/cmake/ is
# itself a hashed input, editing the generator also moves the content hash, so
# the bytes move for a reason that proves nothing about the reading. The guard is
# now reading-level: the mutant must MOVE the reading it was aimed at; if it does
# not, nothing was measured (exit 90), and the source is restored first so the
# tree is never left mutated.
sec "mutation suite: four edits that must turn a named assertion red"
if [ "$FAIL" -ne 0 ]; then
  echo "MUTATION-SUITE=REFUSED (the forward suite above is red; a mutation cannot be scored against a baseline it never had)"
  exit 1
fi

cp "$GENERATOR" "$SCRATCH/generator.backup"
GENERATOR_SHA="$(shasum "$GENERATOR")"
REFERENCE_SHA="$BASE_SHA"
BASE_TREE="$(git rev-parse --short=12 HEAD)"

mutate_insert() { # <anchor> <text>: insert <text> immediately before <anchor>
  python - "$GENERATOR" "$1" "$2" <<'PY'
import sys
path, anchor, insert = sys.argv[1], sys.argv[2], sys.argv[3]
with open(path, "r", encoding="utf-8", newline="") as stream:
    data = stream.read()
if anchor not in data:
    sys.exit("mutant anchor absent: " + anchor)
with open(path, "w", encoding="utf-8", newline="") as stream:
    stream.write(data.replace(anchor, insert + anchor, 1))
PY
}

restore_generator() { # <label>
  cp "$SCRATCH/generator.backup" "$GENERATOR"
  cmp -s "$SCRATCH/generator.backup" "$GENERATOR"
  eq "$1: the mutant source is restored byte-identically" "$?" "0"
  eq "$1: the restored source has the original sha256" "$(shasum "$GENERATOR")" "$GENERATOR_SHA"
}

# The provenance values are assembled in one place; inserting before it forges a
# field without disturbing the git calls that measured it. The old anchor (the
# buildUtc TIMESTAMP) no longer exists: build time is carried forward from the
# previous provenance now, so it is not a line a mutant can target.
MUT_ANCHOR='set(TF2_VALUES'
MUT_CONTENT_ANCHOR='string(SHA256 TF2_CONTENT'

sec "M1: the stamp reports a hard-coded source tree instead of HEAD"
mutate_insert "$MUT_ANCHOR" 'set(TF2_SOURCE_TREE "deadbeef0000")
'
M1_APPLY_RC=$?
eq "M1 applied (the anchor was found and edited)" "$M1_APPLY_RC" "0"
if [ "$M1_APPLY_RC" -ne 0 ]; then
  echo "M1: the mutant could not be applied; the source is unchanged"
  exit 1
fi
build_targets mut1
M1_BUILD_RC=$?
eq "M1 mutated build rc (a mutant that does not compile is not caught)" "$M1_BUILD_RC" "0"
if [ "$M1_BUILD_RC" -ne 0 ]; then report_build_failure "$SCRATCH/build-mut1.log"; fi
read_version "$SCRATCH/mut1.version"
echo "  --- under M1: $(sed -n '2p' "$SCRATCH/mut1.version" | tr -d '\r') / $(sed -n '3p' "$SCRATCH/mut1.version" | tr -d '\r')"
M1_TREE="$(ver_field "$SCRATCH/mut1.version" sourceTree)"
eq "M1 is caught: the sourceTree assertion is red under the mutant" \
  "$([ "$M1_TREE" = "$BASE_TREE" ] && echo green || echo red)" "red"
if [ "$M1_TREE" = "$BASE_TREE" ]; then
  restore_generator "M1"
  echo "MUTATION-NOT-LIVE: M1 was meant to forge sourceTree and did not; nothing was measured"
  exit 90
fi
restore_generator "M1"
build_targets mut1-restored
eq "M1 restored build rc" "$?" "0"
read_version "$SCRATCH/mut1-restored.version"
eq "M1 restored: the sourceTree assertion is green again" \
  "$([ "$(ver_field "$SCRATCH/mut1-restored.version" sourceTree)" = "$BASE_TREE" ] && echo green || echo red)" "green"
MUTATIONS=$((MUTATIONS + 1))

sec "M2: the provenance reports a clean tree while the tree has uncommitted changes"
mutate_insert "$MUT_ANCHOR" 'set(TF2_DIRTY "0")
set(TF2_DIRTY_FILES "0")
set(TF2_DIRTY_TRACKED "0")
set(TF2_DIRTY_UNTRACKED "0")
'
M2_APPLY_RC=$?
eq "M2 applied (the anchor was found and edited)" "$M2_APPLY_RC" "0"
if [ "$M2_APPLY_RC" -ne 0 ]; then
  restore_generator "M2"
  exit 1
fi
PROBE_TRACKED="$REPO/docs/RELEASE-POLICY.md"
PROBE_UNTRACKED="$REPO/evidence/.build-identity-check-probe.txt"
cp "$PROBE_TRACKED" "$SCRATCH/probe-doc.backup"
printf '\n<!-- build-identity-check.sh M2 probe -->\n' >> "$PROBE_TRACKED"
printf 'build-identity-check.sh M2 probe\n' > "$PROBE_UNTRACKED"
tree_counts
M2_TOTAL=$T_TOTAL
build_targets mut2
M2_BUILD_RC=$?
eq "M2 mutated build rc (a mutant that does not compile is not caught)" "$M2_BUILD_RC" "0"
if [ "$M2_BUILD_RC" -ne 0 ]; then report_build_failure "$SCRATCH/build-mut2.log"; fi
read_version "$SCRATCH/mut2.version"
echo "  --- under M2: $(sed -n '3p' "$SCRATCH/mut2.version" | tr -d '\r') (the tree has $M2_TOTAL records)"
M2_DIRTY="$(ver_field "$SCRATCH/mut2.version" dirty)"
M2_FILES="$(ver_field "$SCRATCH/mut2.version" dirtyFiles)"
M2_EXPECTED=0
[ "$M2_TOTAL" -gt 0 ] && M2_EXPECTED=1
eq "M2 is caught: the dirty assertion is red under the mutant" \
  "$([ "$M2_DIRTY" = "$M2_EXPECTED" ] && echo green || echo red)" "red"
eq "M2 is caught: the dirtyFiles assertion is red under the mutant" \
  "$([ "$M2_FILES" = "$M2_TOTAL" ] && echo green || echo red)" "red"
cp "$SCRATCH/mut2.version" "$SCRATCH/mut2-red.version"
if [ "$M2_DIRTY" = "$M2_EXPECTED" ] && [ "$M2_FILES" = "$M2_TOTAL" ]; then
  rm -f "$PROBE_UNTRACKED"; PROBE_UNTRACKED=""
  cp "$SCRATCH/probe-doc.backup" "$PROBE_TRACKED"
  restore_generator "M2"
  echo "MUTATION-NOT-LIVE: M2 was meant to forge the dirty counts and did not; nothing was measured"
  exit 90
fi
rm -f "$PROBE_UNTRACKED"
PROBE_UNTRACKED=""
cp "$SCRATCH/probe-doc.backup" "$PROBE_TRACKED"
cmp -s "$SCRATCH/probe-doc.backup" "$PROBE_TRACKED"
eq "M2: the dirty probe is removed and its file restored byte-identically" "$?" "0"
rm -f "$SCRATCH/probe-doc.backup"
restore_generator "M2"
build_targets mut2-restored
eq "M2 restored build rc" "$?" "0"
read_version "$SCRATCH/mut2-restored.version"
tree_counts
M2_RESTORED_EXPECTED=0
[ "$T_TOTAL" -gt 0 ] && M2_RESTORED_EXPECTED=1
eq "M2 restored: the dirty assertion is green again" \
  "$([ "$(ver_field "$SCRATCH/mut2-restored.version" dirty)" = "$M2_RESTORED_EXPECTED" ] && echo green || echo red)" "green"
eq "M2 restored: the dirtyFiles assertion is green again" \
  "$([ "$(ver_field "$SCRATCH/mut2-restored.version" dirtyFiles)" = "$T_TOTAL" ] && echo green || echo red)" "green"
MUTATIONS=$((MUTATIONS + 1))

sec "M3: the argument loop still ignores an unknown argument but stops counting it"
# The mutation is one line of native/src/main.cpp: the branch that recorded an
# argument the loop did not consume now drops it, which is exactly the defect
# D-23 removed. The named assertion that must go red is the (a) reading from
# section 1c/5 -- "args: unrecognized=1" -- not a new assertion invented here.
MAIN_CPP="native/src/main.cpp"
cp "$MAIN_CPP" "$SCRATCH/main-cpp.backup"
MAIN_CPP_SHA="$(shasum "$MAIN_CPP")"
mutate_file() { # <file> <anchor> <replacement>
  python - "$1" "$2" "$3" <<'PY'
import sys
path, anchor, replacement = sys.argv[1], sys.argv[2], sys.argv[3]
with open(path, "r", encoding="utf-8", newline="") as stream:
    data = stream.read()
if anchor not in data:
    sys.exit("mutant anchor absent: " + anchor)
with open(path, "w", encoding="utf-8", newline="") as stream:
    stream.write(data.replace(anchor, replacement, 1))
PY
}
restore_main_cpp() { # <label>
  cp "$SCRATCH/main-cpp.backup" "$MAIN_CPP"
  cmp -s "$SCRATCH/main-cpp.backup" "$MAIN_CPP"
  eq "$1: the mutant source is restored byte-identically" "$?" "0"
  eq "$1: the restored source has the original sha256" "$(shasum "$MAIN_CPP")" "$MAIN_CPP_SHA"
}
mutate_file "$MAIN_CPP" 'unrecognizedArguments.push_back(arguments[i]);' '(void)arguments[i]; // M3 mutant: ignored, not counted'
M3_APPLY_RC=$?
eq "M3 applied (the anchor was found and edited)" "$M3_APPLY_RC" "0"
if [ "$M3_APPLY_RC" -ne 0 ]; then
  echo "M3: the mutant could not be applied; the source is unchanged"
  exit 1
fi
build_targets mut3
M3_BUILD_RC=$?
eq "M3 mutated build rc (a mutant that does not compile is not caught)" "$M3_BUILD_RC" "0"
if [ "$M3_BUILD_RC" -ne 0 ]; then report_build_failure "$SCRATCH/build-mut3.log"; fi
M3_SHA="$(shasum "$EXE")"
eq "M3 mutated build produced a different executable" \
  "$([ "$M3_SHA" != "$REFERENCE_SHA" ] && echo linked || echo identical)" "linked"
if [ "$M3_SHA" = "$REFERENCE_SHA" ]; then
  echo "MUTATION-NOT-LINKED: M3 built the same bytes as the baseline; nothing was measured"
  restore_main_cpp "M3"
  exit 90
fi
rm -f "$SCRATCH/args-mut3.trace"
TF2_NATIVE_TRACE="$SCRATCH/args-mut3.trace" "$EXE" "${SETTINGS_ARGS[@]}" \
  --hidden-window --audio-disabled "$UNKNOWN_FLAG" --playback-speed 0 \
  > "$SCRATCH/args-mut3.log" 2>&1
M3_RUN_RC=$?
eq "M3 mutant still ends at the value refusal (the mutation is not a crash)" "$M3_RUN_RC" "13"
# Same compensation as the forward run's args-unknown.log (D-67): the capture is
# allowed to be empty, and this assertion says so, while the trace line below
# carries the reading that must go red under the mutant.
eq "M3: the mutant's capture is empty too, the reading lives in the trace" \
  "$([ -s "$SCRATCH/args-mut3.log" ] && echo 1 || echo 0)" "0"
echo "  --- under M3: $(trace_text "$SCRATCH/args-mut3.trace" | grep ' args: ' | head -1)"
eq "M3 is caught: the args: unrecognized=1 assertion is red under the mutant" \
  "$([ "$(trace_text "$SCRATCH/args-mut3.trace" | grep -c 'args: unrecognized=1 ' || true)" = "1" ] && echo green || echo red)" "red"
restore_main_cpp "M3"
build_targets mut3-restored
eq "M3 restored build rc" "$?" "0"
rm -f "$SCRATCH/args-restored.trace"
TF2_NATIVE_TRACE="$SCRATCH/args-restored.trace" "$EXE" "${SETTINGS_ARGS[@]}" \
  --hidden-window --audio-disabled "$UNKNOWN_FLAG" --playback-speed 0 \
  > "$SCRATCH/args-restored.log" 2>&1
M3R_RUN_RC=$?
eq "M3 restored run rc" "$M3R_RUN_RC" "13"
eq "M3 restored: the args: unrecognized=1 assertion is green again" \
  "$([ "$(trace_text "$SCRATCH/args-restored.trace" | grep -c 'args: unrecognized=1 ' || true)" = "1" ] && echo green || echo red)" "green"
MUTATIONS=$((MUTATIONS + 1))

sec "M4: the build hashes something that is not the compiled inputs"
# The mutant appends one line to the text the content hash is computed over. Every
# consumer of that hash moves together -- the TU, both executables, both sidecars
# -- so the sidecar still names the bytes beside it and assert_sidecar_binding
# stays green. Only the independent recomputation (build-identity-content.sh: a
# different implementation reading a different source of truth, the git index)
# can see this, which is the reason that second implementation exists.
mutate_insert "$MUT_CONTENT_ANCHOR" 'string(APPEND TF2_CONTENT_TEXT "# M4 mutant: one line the tree does not have")
'
M4_APPLY_RC=$?
eq "M4 applied (the anchor was found and edited)" "$M4_APPLY_RC" "0"
if [ "$M4_APPLY_RC" -ne 0 ]; then
  restore_generator "M4"
  exit 1
fi
build_targets mut4
M4_BUILD_RC=$?
eq "M4 mutated build rc (a mutant that does not compile is not caught)" "$M4_BUILD_RC" "0"
if [ "$M4_BUILD_RC" -ne 0 ]; then report_build_failure "$SCRATCH/build-mut4.log"; fi
M4_SHA="$(shasum "$EXE")"
eq "M4 mutated build relinked the product (the content hash is compiled in)" \
  "$([ "$M4_SHA" != "$REFERENCE_SHA" ] && echo linked || echo identical)" "linked"
read_version "$SCRATCH/mut4.version"
echo "  --- under M4: $(sed -n '4p' "$SCRATCH/mut4.version" | tr -d '\r')"
M4_CONTENT="$(ver_field "$SCRATCH/mut4.version" sourceContent)"
M4_RECOMPUTED="$(bash "$CONTENT_TOOL" hash 2>/dev/null)"
eq "M4: the gate can still recompute the tree's content hash" \
  "$([ -n "$M4_RECOMPUTED" ] && echo computed || echo missing)" "computed"
eq "M4: the sidecar still names the mutated bytes (binding alone is blind to it)" \
  "$(sed -n 's/^sourceContent=//p' "$IDENTITY" | tr -d '\r')" "$M4_CONTENT"
eq "M4 is caught: the content-hash assertion is red under the mutant" \
  "$([ "$M4_RECOMPUTED" = "$M4_CONTENT" ] && echo green || echo red)" "red"
cp "$SCRATCH/mut4.version" "$SCRATCH/mut4-red.version"
if [ "$M4_RECOMPUTED" = "$M4_CONTENT" ]; then
  restore_generator "M4"
  echo "MUTATION-NOT-LIVE: M4 was meant to move the content hash and did not; nothing was measured"
  exit 90
fi
restore_generator "M4"
build_targets mut4-restored
eq "M4 restored build rc" "$?" "0"
read_version "$SCRATCH/mut4-restored.version"
eq "M4 restored: the content hash is the tree's again" \
  "$([ "$(ver_field "$SCRATCH/mut4-restored.version" sourceContent)" = "$(bash "$CONTENT_TOOL" hash 2>/dev/null)" ] && echo green || echo red)" "green"
eq "M4 restored: the product is back to its baseline bytes" "$(shasum "$EXE")" "$REFERENCE_SHA"
MUTATIONS=$((MUTATIONS + 1))

sec "mutation summary"
eq "the mutant source is back to its original bytes at the end" "$(shasum "$GENERATOR")" "$GENERATOR_SHA"
eq "the mutated C++ source is back to its original bytes at the end" "$(shasum "$MAIN_CPP")" "$MAIN_CPP_SHA"
eq "the mutation suite ran every mutation it declares" "$MUTATIONS" "4"
{ echo "mutations=$MUTATIONS"; echo "generator_sha256=$GENERATOR_SHA"
  echo "main_cpp_sha256=$MAIN_CPP_SHA"
  echo "git_head=$(git rev-parse HEAD)"
  echo "native_tree_sha256=$(git ls-tree -r HEAD -- native/ | sha256sum | cut -d' ' -f1)"
  echo "gate_script_sha256=$(shasum "$0")"
  echo "content_tool_sha256=$(shasum "$CONTENT_TOOL")"
  echo "reference_exe_sha256=$REFERENCE_SHA"
  echo "m1_named_assertion=sourceTree"; echo "m1_reading=$M1_TREE"
  echo "m2_named_assertion=dirty+dirtyFiles"; echo "m2_reading=$M2_DIRTY/$M2_FILES tree=$M2_TOTAL"
  echo "m3_named_assertion=args-unrecognized"; echo "m3_exe_sha256=$M3_SHA"
  echo "m4_named_assertion=content-hash-agrees"; echo "m4_reading=$M4_CONTENT recomputed=$M4_RECOMPUTED"
} > "$OUT/mutation-readings.txt"
copy_out "$SCRATCH/mut1.version" version-mut1-fake-tree.txt
copy_out "$SCRATCH/mut2-red.version" version-mut2-claims-clean.txt
copy_out "$SCRATCH/mut4-red.version" version-mut4-wrong-content.txt
copy_out "$SCRATCH/args-mut3.trace" args-mut3.trace
copy_out "$SCRATCH/args-mut3.log" args-mut3.log
copy_out "$SCRATCH/args-restored.trace" args-restored.trace
eq "evidence: every copy reached the output directory" "$COPY_FAIL" 0
eq "evidence: the output directory holds the readings this run promises" \
  "$(( $(evidence_missing present args-mut3.log) \
     + $(evidence_missing nonempty mutation-readings.txt version-mut1-fake-tree.txt \
         version-mut2-claims-clean.txt version-mut4-wrong-content.txt \
         args-mut3.trace args-restored.trace) ))" 0
if [ "$FAIL" -ne 0 ]; then
  echo "BUILD-IDENTITY-MUTATION=FAIL ($FAIL of $CHECKS assertions red, $MUTATIONS mutations attempted)"
  exit 1
fi
echo "BUILD-IDENTITY-MUTATION=PASS ($MUTATIONS mutations caught, sources restored, $CHECKS assertions)"
exit 0
