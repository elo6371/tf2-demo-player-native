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
# The build now measures the identity at build time (native/cmake/
# gen-build-stamp.cmake, run by the tf2_build_stamp target before either target
# compiles it) and --version prints it as a third line:
#     dirty=0 dirtyFiles=0 dirtyTracked=0 dirtyUntracked=0
# This gate compares those readings against git, live, in three states: the tree
# as it is now (sourceTree must be HEAD; the dirty flag and its counts must be
# exactly what "git status --porcelain -uall" reports at that instant); a tree
# the gate deliberately dirties with one tracked change and one untracked file
# (the rebuild must relink and answer dirty=1 with +1/+1 counts); and after
# byte-exact restoration (the identity must return to what it was -- the property
# that makes the reading a function of the tree instead of of what happened to be
# lying around). It also pins that a rebuild with no source change neither
# relinks nor moves buildUtc, so an incremental build pays nothing for the stamp.
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
#   * not a content hash: sourceTree is HEAD's short id, dirty is a COUNT.
#     "built from e15577b with 3 dirty files" does not say which files or what
#     they contained; only a hash of the tree would, and there is none.
#   * not that an ignored file is accounted for: git-ignored inputs are not
#     counted, so a build whose only extra input is ignored reads as clean.
#   * not that the manifest/VERSION.txt agree with --version; that is
#     install-lifecycle-check.sh's subject and this gate reads --version only.
#   * not that a dirty binary can be rebuilt from the dirty tree; the tree is not
#     recorded, only counted.
#   * not that the product UNDERSTANDS an argument it was given: section 1c/5
#     reads the "args: unrecognized=" line, which counts what the loop did not
#     consume. The default run still ignores those arguments -- that is the
#     behaviour the line documents, and --strict-args is the switch that turns
#     the count into a refusal.
#
# Usage:
#   bash build-identity-check.sh --forward      <out-dir>  # BUILD-IDENTITY=PASS
#   bash build-identity-check.sh --mutation     <out-dir>  # 3 mutants must go red
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
# Exit: 0 = every assertion held (with --mutation: all three mutants were caught;
#       with --reproducible: the two clean builds were byte-identical),
#       1 = an assertion failed, 2 = usage, 90 = a mutant built the same bytes.
set -uo pipefail

REPO="$(cd "$(dirname "$0")" && pwd)"
cd "$REPO"

usage() { echo "usage: bash build-identity-check.sh --forward|--mutation|--reproducible <out-dir>" >&2; exit 2; }

MODE=""
[ "$#" -ge 1 ] && MODE="$1"
OUT_ARG=""
[ "$#" -ge 2 ] && OUT_ARG="$2"
case "$MODE" in
  --forward|--mutation|--reproducible) ;;
  *) usage ;;
esac
[ -n "$OUT_ARG" ] || usage
mkdir -p "$OUT_ARG" || exit 2
OUT="$(cd "$OUT_ARG" && pwd)"

BUILD_DIR="native/build-nmake"
[ -n "${TF2_BUILD_DIR-}" ] && BUILD_DIR="$TF2_BUILD_DIR"
EXE="$BUILD_DIR/tf2_demo_native.exe"
PROBE="$BUILD_DIR/install_cli_probe.exe"
GENERATOR="native/cmake/gen-build-stamp.cmake"
STAMP="native/generated/tf2_build_stamp.cpp"

# The gate measures the tree's dirtiness, so it must not add any: every
# intermediate copy goes to .scratch/ (git-ignored), never to $OUT, and $OUT is
# written only after the last reading.
SCRATCH="$REPO/.scratch/build-identity"
rm -rf "$SCRATCH"
mkdir -p "$SCRATCH"

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
  exit "$rc"
}
trap cleanup EXIT INT TERM

build_targets() { # <log-suffix>
  bash build-target.sh tf2_demo_native install_cli_probe > "$SCRATCH/build-$1.log" 2>&1
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

assert_timestamp_window() { # <file> <label>
  local f="$1" label="$2" head_epoch stamp_epoch exe_mtime
  head_epoch="$(git log -1 --format=%ct HEAD)"
  stamp_epoch="$(date -u -d "$(ver_field "$f" buildUtc)" +%s 2>/dev/null)"
  exe_mtime="$(stat -c %Y "$EXE" 2>/dev/null)"
  eq "$label: buildUtc parses as a UTC instant" \
    "$([ -n "${stamp_epoch-}" ] && echo yes || echo no)" "yes"
  ge "$label: buildUtc is not earlier than the commit it names" "${stamp_epoch:-0}" "$head_epoch"
  le "$label: buildUtc is not later than the executable's own mtime" "${stamp_epoch:-1}" "${exe_mtime:-0}"
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
  cp "$SCRATCH/reproducible.txt" "$OUT/reproducible.txt" 2>/dev/null
  cp "$SCRATCH/repro-build1.log" "$OUT/repro-build1.log" 2>/dev/null
  cp "$SCRATCH/repro-build2.log" "$OUT/repro-build2.log" 2>/dev/null
  cp "$SCRATCH/repro1.version" "$OUT/version-repro-build1.txt" 2>/dev/null
  cp "$SCRATCH/repro2.version" "$OUT/version-repro-build2.txt" 2>/dev/null
  [ -f "$SCRATCH/repro-diff.txt" ] && cp "$SCRATCH/repro-diff.txt" "$OUT/repro-diff.txt" 2>/dev/null
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

BASE_SHA="$(shasum "$EXE")"
BASE_STAMP_SHA="$(shasum "$STAMP")"
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

# --- 3/5 a dirty tree -------------------------------------------------------
sec "3/5 a dirty tree must be reported as dirty, with the count git reports"
PROBE_TRACKED="$REPO/docs/RELEASE-POLICY.md"
PROBE_UNTRACKED="$REPO/evidence/.build-identity-check-probe.txt"
cp "$PROBE_TRACKED" "$SCRATCH/probe-doc.backup"
PROBE_DOC_SHA="$(shasum "$PROBE_TRACKED")"
printf '\n<!-- build-identity-check.sh probe: one tracked change; must never be committed -->\n' >> "$PROBE_TRACKED"
printf 'build-identity-check.sh probe\n' > "$PROBE_UNTRACKED"
tree_counts
DIRTY_TOTAL=$T_TOTAL
echo "  --- probe added 1 tracked change (docs/RELEASE-POLICY.md) and 1 untracked file"
eq "the probe added exactly two records to the tree" "$((DIRTY_TOTAL - BASE_TOTAL))" "2"

build_targets dirty
DIRTY_RC=$?
eq "dirty build exit code" "$DIRTY_RC" "0"
if [ "$DIRTY_RC" -ne 0 ]; then report_build_failure "$SCRATCH/build-dirty.log"; fi
read_version "$SCRATCH/dirty.version"
eq "dirty --version exit code" "$?" "0"
echo "  --- dirty --version ---"
sed 's/^/    /' "$SCRATCH/dirty.version"
assert_version_shape "$SCRATCH/dirty.version" "dirty"
assert_identity_matches_tree "$SCRATCH/dirty.version" "dirty"
eq "the dirty rebuild relinked the product (exe sha256 moved)" \
  "$([ "$(shasum "$EXE")" != "$BASE_SHA" ] && echo moved || echo same)" "moved"
eq "the dirty build answers differently from the clean one" \
  "$([ "$(sed -n '3p' "$SCRATCH/dirty.version" | tr -d '\r')" != "$(sed -n '3p' "$SCRATCH/baseline.version" | tr -d '\r')" ] && echo differs || echo same)" "differs"
eq "the dirty build still names the same commit as sourceTree" \
  "$(ver_field "$SCRATCH/dirty.version" sourceTree)" "$(git rev-parse --short=12 HEAD)"

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
# The stamp is generated C++ (kDirtyFiles[] = "<n>";), not a key=value file:
# grepping it for "dirtyFiles=" would match nothing and pass on emptiness.
ge "the restored stamp carries a dirtyFiles reading at all" \
  "$(grep -cE 'kDirtyFiles\[\] = "[0-9]+";' "$STAMP" 2>/dev/null || true)" 1
eq "the restored stamp carries the pre-probe record count ($BASE_TOTAL)" \
  "$(grep -c "kDirtyFiles\[\] = \"$BASE_TOTAL\";" "$STAMP" 2>/dev/null || true)" "1"
eq "the stamp is not left carrying the probe's record count" \
  "$(grep -c "kDirtyFiles\[\] = \"$DIRTY_TOTAL\";" "$STAMP" 2>/dev/null || true)" "0"

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

# --- 5/5 summary ------------------------------------------------------------
sec "5/5 what the assertions above established, and what they did not"
echo "  NOT-PROVEN cross-machine reproducibility: --reproducible measured two clean full builds on THIS machine only"
echo "  NOT-PROVEN a content hash: sourceTree+dirty name a commit and a count, not the dirty content"
echo "  NOT-PROVEN the manifest/VERSION.txt agree with --version (install-lifecycle-check.sh's subject)"
echo "  NOT-PROVEN that a dirty build can be rebuilt from the dirty tree (it is not recorded, only counted)"
echo "  assertions=$CHECKS failed=$FAIL"

if [ "$FAIL" -ne 0 ]; then
  echo "BUILD-IDENTITY=FAIL assertions=$CHECKS red=$FAIL"
  exit 1
fi
if [ "$MODE" = "--forward" ]; then
  # Evidence is copied out only now, after the last reading, so the gate never
  # dirties the tree it measured.
  cp "$SCRATCH/baseline.version" "$OUT/version-baseline.txt"
  cp "$SCRATCH/dirty.version" "$OUT/version-dirty-probe.txt"
  cp "$SCRATCH/restored.version" "$OUT/version-restored.txt"
  cp "$SCRATCH/probe.version" "$OUT/probe-version.txt"
  cp "$SCRATCH/args-unknown.trace" "$OUT/args-unknown.trace"
  cp "$SCRATCH/args-unknown.log" "$OUT/args-unknown.log"
  cp "$SCRATCH/args-strict.err" "$OUT/args-strict.err"
  cp "$SCRATCH/args-strict.trace" "$OUT/args-strict.trace"
  cp "$SCRATCH/args-clean.trace" "$OUT/args-clean.trace"
  cp "$SCRATCH/quit.camera" "$OUT/quit.camera"
  cp "$SCRATCH/source-identity.txt" "$OUT/source-identity.txt"
  { echo "exe=$EXE"; echo "exe_sha256=$BASE_SHA"; echo "stamp_sha256=$BASE_STAMP_SHA"
    echo "records_at_baseline=$BASE_TOTAL"; echo "head=$(git rev-parse HEAD)"
    echo "head_full=$HEAD_FULL"; echo "native_tree_sha256=$NATIVE_TREE_SHA"
    echo "gate_script_sha256=$(shasum "$0")"
    echo "main_cpp_sha256=$(shasum "$REPO/native/src/main.cpp")"
    echo "args_unknown=$OUT/args-unknown.trace"
    echo "args_strict_stderr=$OUT/args-strict.err"
    echo "args_clean=$OUT/args-clean.trace"
  } > "$OUT/readings.txt"
  echo "BUILD-IDENTITY=PASS assertions=$CHECKS identity=exe-sha+HEAD+native-tree-sha head=$HEAD_SHORT"
  exit 0
fi

# --- mutations --------------------------------------------------------------
# M1 and M2 edit native/cmake/gen-build-stamp.cmake; M3 edits native/src/main.cpp.
sec "mutation suite: three edits that must turn a named assertion red"
if [ "$FAIL" -ne 0 ]; then
  echo "MUTATION-SUITE=REFUSED (the forward suite above is red; a mutation cannot be scored against a baseline it never had)"
  exit 1
fi

cp "$GENERATOR" "$SCRATCH/generator.backup"
GENERATOR_SHA="$(shasum "$GENERATOR")"
REFERENCE_SHA="$BASE_SHA"

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

MUT_ANCHOR='string(TIMESTAMP TF2_NOW_UTC "%Y-%m-%dT%H:%M:%SZ" UTC)'

sec "M1: the stamp reports a hard-coded source tree instead of HEAD"
mutate_insert "$MUT_ANCHOR" 'set(TF2_TREE "deadbeef0000")
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
M1_SHA="$(shasum "$EXE")"
eq "M1 mutated build produced a different executable" \
  "$([ "$M1_SHA" != "$REFERENCE_SHA" ] && echo linked || echo identical)" "linked"
if [ "$M1_SHA" = "$REFERENCE_SHA" ]; then
  echo "MUTATION-NOT-LINKED: M1 built the same bytes as the baseline; nothing was measured"
  exit 90
fi
read_version "$SCRATCH/mut1.version"
echo "  --- under M1: $(sed -n '2p' "$SCRATCH/mut1.version" | tr -d '\r') / $(sed -n '3p' "$SCRATCH/mut1.version" | tr -d '\r')"
M1_TREE="$(ver_field "$SCRATCH/mut1.version" sourceTree)"
eq "M1 is caught: the sourceTree assertion is red under the mutant" \
  "$([ "$M1_TREE" = "$(git rev-parse --short=12 HEAD)" ] && echo green || echo red)" "red"
restore_generator "M1"
build_targets mut1-restored
eq "M1 restored build rc" "$?" "0"
read_version "$SCRATCH/mut1-restored.version"
eq "M1 restored: the sourceTree assertion is green again" \
  "$([ "$(ver_field "$SCRATCH/mut1-restored.version" sourceTree)" = "$(git rev-parse --short=12 HEAD)" ] && echo green || echo red)" "green"
MUTATIONS=$((MUTATIONS + 1))

sec "M2: the stamp reports a clean tree while the tree has uncommitted changes"
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
M2_SHA="$(shasum "$EXE")"
eq "M2 mutated build produced a different executable" \
  "$([ "$M2_SHA" != "$REFERENCE_SHA" ] && echo linked || echo identical)" "linked"
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

sec "mutation summary"
eq "the mutant source is back to its original bytes at the end" "$(shasum "$GENERATOR")" "$GENERATOR_SHA"
eq "the mutated C++ source is back to its original bytes at the end" "$(shasum "$MAIN_CPP")" "$MAIN_CPP_SHA"
{ echo "mutations=$MUTATIONS"; echo "generator_sha256=$GENERATOR_SHA"
  echo "main_cpp_sha256=$MAIN_CPP_SHA"
  echo "git_head=$(git rev-parse HEAD)"
  echo "native_tree_sha256=$(git ls-tree -r HEAD -- native/ | sha256sum | cut -d' ' -f1)"
  echo "gate_script_sha256=$(shasum "$0")"
  echo "reference_exe_sha256=$REFERENCE_SHA"; echo "m1_exe_sha256=$M1_SHA"
  echo "m2_exe_sha256=$M2_SHA"; echo "m3_exe_sha256=$M3_SHA"
} > "$OUT/mutation-readings.txt"
cp "$SCRATCH/mut1.version" "$OUT/version-mut1-fake-tree.txt" 2>/dev/null
cp "$SCRATCH/mut2-red.version" "$OUT/version-mut2-claims-clean.txt" 2>/dev/null
cp "$SCRATCH/args-mut3.trace" "$OUT/args-mut3.trace" 2>/dev/null
cp "$SCRATCH/args-mut3.log" "$OUT/args-mut3.log" 2>/dev/null
cp "$SCRATCH/args-restored.trace" "$OUT/args-restored.trace" 2>/dev/null
if [ "$FAIL" -ne 0 ]; then
  echo "BUILD-IDENTITY-MUTATION=FAIL ($FAIL of $CHECKS assertions red, $MUTATIONS mutations attempted)"
  exit 1
fi
echo "BUILD-IDENTITY-MUTATION=PASS ($MUTATIONS mutations caught, sources restored, $CHECKS assertions)"
exit 0
