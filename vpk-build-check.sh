#!/usr/bin/env bash
# vpk-build-check.sh [--mutation]
#
# T15: VPK and build reliability.
#
# WHAT THIS ASSERTS
#   1. A valid archive resolves the bytes it declares, and the *store* that
#      answered is named: directory-inline data, numbered-volume data, and
#      preload-only bytes are three different answers, not one "bytes".
#   2. Every malformed tree is refused with a specific reason and leaks zero
#      bytes. The reasons are asserted literally, because "it returned false" is
#      satisfied by a parser that refuses everything for one generic reason.
#   3. Lookup precedence is loose file, then archives in the order they were
#      opened, then the map's pakfile -- and order decides, not filename.
#   4. build-target.sh configures and builds from an EMPTY build directory, for
#      an existing target and for the one this round added. A rule that only
#      exists because a previous configure emitted it is not a build system.
#   5. The shipped TF2 archives still open and still yield bytes. Hardening a
#      parser until it rejects everything also passes (1)-(3).
#
# WHAT IT DELIBERATELY DOES NOT DO
#   It does not run the long chain, and it does not capture frames: T15 changes
#   no rendering. The one place it touches the product is that the archives the
#   product reads must still read.
#
# Exit: 0 = every assertion held; 1 = at least one did not.
set -uo pipefail
cd "$(dirname "$0")"

MUTATION=0
if [ "${1:-}" = "--mutation" ]; then MUTATION=1; fi
OUT="${VPK_BUILD_OUT:-evidence/vpk-build}"
mkdir -p "$OUT"
PROBE=native/build-nmake/vpk_integrity_probe.exe
QUERY=native/build-nmake/vpk_query.exe
TFROOT="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
rc_all=0

pass() { printf 'ASSERT-PASS %s\n' "$1"; }
fail() { printf 'ASSERT-FAIL %s\n' "$1"; rc_all=1; }

# assert_reading <label> <file> <literal JSON fragment>
assert_reading() {
  if grep -qF -- "$3" "$2"; then pass "$1"; else fail "$1 (missing: $3)"; fi
}
# Reading a numeric field out of the probe's JSON without a JSON parser: the
# values are digits only, and the key is unique in the stream.
reading() { grep -F "{\"reading\":\"$2\"" "$1" | head -1 | sed 's/.*"value":"//; s/"}$//'; }

echo "=== build ==="
bash build-target.sh vpk_integrity_probe vpk_query > "$OUT/build.txt" 2>&1
build_rc=$?
tail -3 "$OUT/build.txt"
if [ "$build_rc" -eq 0 ]; then pass "build rc=0"; else fail "build rc=$build_rc"; fi
if [ ! -x "$PROBE" ]; then echo "VPK-BUILD=FAIL (no $PROBE)"; exit 1; fi

echo
echo "=== 1/6 valid archives and named stores ==="
"$PROBE" --out .scratch/vpk-integrity > "$OUT/probe.txt" 2> "$OUT/probe.err"
probe_rc=$?
cat "$OUT/probe.txt"
if [ "$probe_rc" -eq 0 ]; then pass "probe rc=0"; else fail "probe rc=$probe_rc"; fi

assert_reading "inline entry opens"        "$OUT/probe.txt" '"reading":"vpk.valid.inline.open","value":"1"'
assert_reading "inline entry resolves"     "$OUT/probe.txt" '"reading":"vpk.valid.inline.bytes","value":"DIRECTORY-DATA"'
assert_reading "inline entry names the directory store" "$OUT/probe.txt" '"reading":"vpk.valid.inline.store","value":"vpk-directory"'
assert_reading "volume entry opens"        "$OUT/probe.txt" '"reading":"vpk.valid.volume.open","value":"1"'
assert_reading "volume entry resolves"     "$OUT/probe.txt" '"reading":"vpk.valid.volume.bytes","value":"NUMBERED-VOLUME-DATA"'
assert_reading "volume entry names the volume store"    "$OUT/probe.txt" '"reading":"vpk.valid.volume.store","value":"vpk-volume"'
assert_reading "preload entry resolves"    "$OUT/probe.txt" '"reading":"vpk.valid.preload.bytes","value":"PRELOAD"'
assert_reading "preload is its own store"  "$OUT/probe.txt" '"reading":"vpk.valid.preload.store","value":"preload"'

echo
echo "=== 2/6 malformed archives: specific reason, zero bytes ==="
# Each reason is asserted literally. The five faults the task names are covered
# by more than five cases because "invalid length" and "invalid offset" each have
# an inline form and a data-archive form, and they fail at different stages.
while IFS='|' read -r name reason; do
  assert_reading "reject $name -> '$reason'" "$OUT/probe.txt" \
    "\"reading\":\"vpk.reject.$name\",\"value\":\"$reason\""
  assert_reading "reject $name leaks no bytes" "$OUT/probe.txt" \
    "\"reading\":\"vpk.reject.$name.bytes\",\"value\":\"0\""
done <<'CASES'
empty-file|empty VPK file
truncated-header|truncated VPK header
unsupported-signature|unsupported VPK header
empty-tree|empty VPK directory tree
truncated-tree|truncated VPK directory tree
invalid-preload-length|truncated VPK preload data
inline-offset-out-of-range|VPK entry range outside directory archive
inline-offset-wraps|VPK entry range outside directory archive
missing-volume|missing VPK data archive
volume-offset-out-of-range|VPK entry range outside data archive
CASES

# The reason strings must be distinct, not ten spellings of one verdict. A
# parser that refuses everything with "bad archive" would still satisfy every
# assertion above if the expected strings were the same.
# Only the ten per-case keys are counted: `vpk.reject.cases`, `.named` and
# `.leaked` are summary counters with the same prefix, and an earlier version of
# this line counted them as two extra "reasons" ("0" and "10"). Every case name
# contains a dash; none of the summary keys does.
distinct=$(grep -oE '"reading":"vpk\.reject\.[a-z0-9-]*-[a-z0-9-]*","value":"[^"]*"' "$OUT/probe.txt" \
  | sed 's/.*"value":"//; s/"$//' | sort -u | wc -l | tr -d ' ')
if [ "$distinct" -eq 9 ]; then pass "reject reasons are distinct ($distinct of 10 cases)"; else fail "expected 9 distinct reasons, got $distinct"; fi

cases=$(reading "$OUT/probe.txt" "vpk.reject.cases")
named=$(reading "$OUT/probe.txt" "vpk.reject.named")
leaked=$(reading "$OUT/probe.txt" "vpk.reject.leaked")
if [ "${cases:-0}" = "${named:-x}" ] && [ "${cases:-0}" -ge 10 ]; then
  pass "every case named its reason ($named/$cases)"
else
  fail "named $named of $cases"
fi
if [ "${leaked:-x}" = "0" ]; then pass "zero bytes leaked"; else fail "leaked=$leaked"; fi

echo
echo "=== 3/6 store precedence ==="
assert_reading "loose file wins"                 "$OUT/probe.txt" '"reading":"vpk.precedence.loose-wins","value":"loose"'
assert_reading "loose bytes are the loose file"  "$OUT/probe.txt" '"reading":"vpk.precedence.loose-wins.bytes","value":"LOOSE-FILE"'
assert_reading "first archive wins over second"  "$OUT/probe.txt" '"reading":"vpk.precedence.directory-before-volume","value":"vpk-directory"'
assert_reading "order decides, not filename"     "$OUT/probe.txt" '"reading":"vpk.precedence.order-decides","value":"vpk-volume"'
assert_reading "a numbered volume alone answers" "$OUT/probe.txt" '"reading":"vpk.precedence.volume-only","value":"vpk-volume"'
assert_reading "pakfile answers when nothing else does" "$OUT/probe.txt" '"reading":"vpk.precedence.pakfile","value":"pakfile"'
assert_reading "pakfile does not outrank loose"  "$OUT/probe.txt" '"reading":"vpk.precedence.pakfile-shadowed","value":"loose"'
assert_reading "absent resource reports no store" "$OUT/probe.txt" '"reading":"vpk.precedence.absent","value":"none"'
assert_reading "probe summary is clean"          "$OUT/probe.txt" '"reading":"vpk.summary","value":"cases=10 named=10 leaked=0 failures=0"'

echo
echo "=== 4/6 inputs are pinned by hash ==="
# The fixtures are generated, so the bytes on disk are the test inputs. Hashing
# them is what lets a later reader tell "the assertion still holds" from "the
# assertion holds against different bytes".
input_count=$(ls -1 .scratch/vpk-integrity/*.vpk 2>/dev/null | wc -l | tr -d ' ')
if [ "$input_count" -ge 13 ]; then pass "fixture count=$input_count"; else fail "fixture count=$input_count"; fi
( cd .scratch/vpk-integrity && sha256sum *.vpk 2>/dev/null | sort -k2 ) > "$OUT/inputs.sha256"
if [ -s "$OUT/inputs.sha256" ]; then
  pass "input hashes recorded ($(wc -l < "$OUT/inputs.sha256" | tr -d ' ') files)"
else
  fail "no input hashes"
fi

echo
echo "=== 5/6 clean build directory (existing + new target) ==="
if [ "${VPK_SKIP_CLEAN_BUILD:-0}" = "1" ]; then
  echo "SKIP clean-build (VPK_SKIP_CLEAN_BUILD=1)"
  echo "clean_build=skipped" >> "$OUT/clean-build.txt"
else
  CLEAN=".scratch/vpk-build/clean-$(date +%Y%m%d-%H%M%S)"
  rm -rf "$CLEAN" 2>/dev/null
  # A directory that has never been configured cannot answer with a cached rule.
  # vpk_query is the existing target; vpk_integrity_probe is this round's new one,
  # so the run also exercises build-target.sh's "no rule in the Makefile ->
  # configure first" path rather than only the steady-state path.
  TF2_BUILD_DIR="$CLEAN" bash build-target.sh vpk_integrity_probe vpk_query \
    > "$OUT/clean-build.txt" 2>&1
  clean_rc=$?
  tail -5 "$OUT/clean-build.txt"
  if [ "$clean_rc" -eq 0 ]; then pass "clean build rc=0"; else fail "clean build rc=$clean_rc"; fi
  if grep -qF '=== configure ===' "$OUT/clean-build.txt"; then
    pass "clean build configured first"
  else
    fail "clean build did not configure (a rule it did not have would be missing)"
  fi
  if [ -f "$CLEAN/CMakeCache.txt" ]; then pass "clean build produced CMakeCache.txt"; else fail "no CMakeCache.txt"; fi
  for exe in vpk_integrity_probe.exe vpk_query.exe; do
    if [ -f "$CLEAN/$exe" ]; then pass "clean build produced $exe"; else fail "clean build missing $exe"; fi
  done
  echo "clean_build_dir=$CLEAN" >> "$OUT/clean-build.txt"
fi

echo
echo "=== 6/6 shipped archives still read (regression guard) ==="
if [ -d "$TFROOT" ]; then
  if "$QUERY" "$TFROOT" materials vmt > .scratch/vpk-build/real-archives-full.txt 2>&1; then
    pass "vpk_query ran against the shipped archives"
  else
    fail "vpk_query failed against the shipped archives"
  fi
  # The full listing is thousands of paths; evidence keeps the per-archive counts
  # and the name that was actually dumped. The listing stays in .scratch because
  # a 2 MB evidence file that nobody diffs is worse than a summary that is read.
  {
    grep -E 'entries,|total matches|OPEN FAILED' .scratch/vpk-build/real-archives-full.txt
  } > "$OUT/real-archives.txt"
  # A known-material read, discovered from the install rather than hard-coded:
  # a pinned path would silently start asserting "not found" on a different build.
  first_material=$(grep -E '^    ' .scratch/vpk-build/real-archives-full.txt | head -1 | sed 's/^ *//')
  if [ -n "$first_material" ]; then
    printf 'discovered=%s\n' "$first_material" >> "$OUT/real-archives.txt"
    if "$QUERY" "$TFROOT" --dump "$first_material" > "$OUT/real-dump.txt" 2>&1; then
      dumped=$(grep -oE 'bytes=[0-9]+' "$OUT/real-dump.txt" | head -1 | cut -d= -f2)
      if [ "${dumped:-0}" -gt 0 ]; then
        pass "a shipped material still reads ($dumped bytes)"
      else
        fail "shipped material read returned no bytes"
      fi
    else
      fail "--dump failed on $first_material"
    fi
  else
    fail "no material listed in the shipped archives"
  fi
  # The fixtures prove the policy against bytes this round wrote. They cannot
  # prove the chain still finds the shipped archives, because a chain with the
  # right order and the wrong archive list passes all of them. This samples real
  # names and requires every one to resolve.
  "$PROBE" --real "$TFROOT" --real-limit 200 > "$OUT/real-chain.txt" 2>&1
  chain_rc=$?
  cat "$OUT/real-chain.txt"
  if [ "$chain_rc" -eq 0 ]; then pass "real-archive chain rc=0"; else fail "real-archive chain rc=$chain_rc"; fi
  # Every *_dir.vpk this install actually has must open. This is also the check
  # that the new inline range guard does not reject real entries: open() refuses
  # the whole archive when one inline entry points outside its own file, so a
  # present archive that fails to open would mean the guard is too strict.
  present=0
  for name in pak01_dir.vpk tf2_misc_dir.vpk tf2_textures_dir.vpk \
              tf2_sound_misc_dir.vpk tf2_sound_vo_english_dir.vpk; do
    if [ -f "$TFROOT/$name" ]; then present=$((present + 1)); fi
  done
  opened=$(reading "$OUT/real-chain.txt" "vpk.real.archives")
  if [ "${opened:-0}" -eq "$present" ] && [ "$present" -gt 0 ]; then
    pass "every present archive opened ($opened of $present; pak01_dir.vpk is absent from this install)"
  else
    fail "opened ${opened:-?} of $present present archives"
  fi
  assert_reading "no sampled name went unresolved" "$OUT/real-chain.txt" '"reading":"vpk.real.empty","value":"0"'
  assert_reading "every sampled name resolved" "$OUT/real-chain.txt" '"reading":"vpk.real.verdict","value":"every sampled name resolved"'
  # The store that answers real material reads must be the numbered volume: a
  # chain that silently fell back to the wrong file would still return bytes.
  real_vol=$(reading "$OUT/real-chain.txt" "vpk.real.byVolume")
  real_dir=$(reading "$OUT/real-chain.txt" "vpk.real.byDirectory")
  if [ "${real_vol:-0}" -gt 0 ]; then
    pass "real reads came from numbered volumes (volume=$real_vol directory=$real_dir)"
  else
    fail "no real read came from a numbered volume (directory=${real_dir:-?})"
  fi
else
  echo "SKIP shipped archives: $TFROOT is not installed" | tee -a "$OUT/real-archives.txt"
  echo "real_archives=skipped" >> "$OUT/real-archives.txt"
fi

echo
if [ "$MUTATION" -eq 1 ]; then
  echo "=== mutations: each defect must move a reading ==="
  # Snapshots, not `git checkout`: the tree is not committed while this runs, and
  # a restore that depends on HEAD would revert the round's own work.
  SNAP=.scratch/vpk-build/snapshot
  mkdir -p "$SNAP"
  for f in native/src/vpk_archive.cpp native/src/asset_lookup.cpp; do
    cp "$f" "$SNAP/$(basename "$f")"
  done
  post_build() { bash build-target.sh vpk_integrity_probe > .scratch/vpk-build/mutation-build.log 2>&1; }
  run_probe() { "$PROBE" --out .scratch/vpk-integrity > "$OUT/probe-mutation.txt" 2>&1; }
  restore_sources() {
    for f in native/src/vpk_archive.cpp native/src/asset_lookup.cpp; do
      cp "$SNAP/$(basename "$f")" "$f"
      if cmp -s "$f" "$SNAP/$(basename "$f")"; then
        printf 'RESTORED-IDENTICAL %s\n' "$f"
      else
        printf 'RESTORED-DIFFERS   %s\n' "$f"; rc_all=1
      fi
    done
  }
  patch_in() { # patch_in <file> <find> <replace>
    python - "$1" "$2" "$3" <<'PY'
import io, sys
path, find, repl = sys.argv[1], sys.argv[2], sys.argv[3]
raw = io.open(path, 'rb').read()
crlf = b'\r\n' in raw
data = raw.decode('utf-8')
if crlf:
    find = find.replace('\n', '\r\n')
    repl = repl.replace('\n', '\r\n')
if find not in data:
    sys.exit('PATTERN NOT FOUND: ' + find[:70])
io.open(path, 'wb').write(data.replace(find, repl, 1).encode('utf-8'))
PY
  }
  # must_move <label> <expected-reading> <must-not-be>
  must_move() {
    local got
    got=$(reading "$OUT/probe-mutation.txt" "$2")
    if [ "$got" != "$3" ]; then
      printf 'MUTATION-CAUGHT=PASS (%s: %s)\n' "$1" "$got"
    else
      printf 'MUTATION-CAUGHT=FAIL (%s: reading still %s)\n' "$1" "$got"
      rc_all=1
    fi
  }

  echo "--- m1: inline offset narrowed to 32 bits again (the wraparound returns)"
  patch_in native/src/vpk_archive.cpp \
    'const std::uint64_t absolute = static_cast<std::uint64_t>(treeEnd) + offset;' \
    'const std::uint64_t absolute = static_cast<std::uint32_t>(treeEnd + offset);' \
    || { fail "m1 patch"; }
  post_build
  run_probe
  must_move "m1 inline wrap" "vpk.reject.inline-offset-wraps" "VPK entry range outside directory archive"
  restore_sources

  echo "--- m2: no bound against the data file's real size"
  patch_in native/src/vpk_archive.cpp \
    'if (entry.offset > available || entry.length > available - entry.offset) {' \
    'if (false && (entry.offset > available || entry.length > available - entry.offset)) {' \
    || { fail "m2 patch"; }
  post_build
  run_probe
  must_move "m2 volume range" "vpk.reject.volume-offset-out-of-range" "VPK entry range outside data archive"
  restore_sources

  echo "--- m3: empty tree folded back into 'truncated'"
  patch_in native/src/vpk_archive.cpp \
    '*error = "empty VPK directory tree";' \
    '*error = "truncated VPK directory tree";' \
    || { fail "m3 patch"; }
  post_build
  run_probe
  must_move "m3 empty tree reason" "vpk.reject.empty-tree" "empty VPK directory tree"
  restore_sources

  echo "--- m4: every entry claims to live in the directory file"
  patch_in native/src/vpk_archive.cpp \
    'result.store = entry.inlineData ? Store::DirectoryData : Store::NumberedVolume;' \
    'result.store = Store::DirectoryData;' \
    || { fail "m4 patch"; }
  post_build
  run_probe
  must_move "m4 store label" "vpk.valid.volume.store" "vpk-volume"
  restore_sources

  echo "--- m5: the loose store is skipped"
  patch_in native/src/asset_lookup.cpp \
    'if (!looseRoot_.empty()) {' \
    'if (false && !looseRoot_.empty()) {' \
    || { fail "m5 patch"; }
  post_build
  run_probe
  must_move "m5 loose precedence" "vpk.precedence.loose-wins" "loose"
  restore_sources

  echo "--- m6: archives consulted in reverse (filename order decides)"
  patch_in native/src/asset_lookup.cpp \
    'for (std::size_t i = 0; i < archives_.size(); ++i) {' \
    'for (std::size_t i = archives_.size(); i-- > 0;) {' \
    || { fail "m6 patch"; }
  post_build
  run_probe
  must_move "m6 archive order" "vpk.precedence.order-decides" "vpk-volume"
  restore_sources

  echo "--- rebuilt restored sources and re-read the fixed values ---"
  post_build
  run_probe
  fixed_ok=1
  for pair in "vpk.reject.inline-offset-wraps|VPK entry range outside directory archive" \
              "vpk.reject.volume-offset-out-of-range|VPK entry range outside data archive" \
              "vpk.reject.empty-tree|empty VPK directory tree" \
              "vpk.valid.volume.store|vpk-volume" \
              "vpk.precedence.loose-wins|loose" \
              "vpk.precedence.order-decides|vpk-volume"; do
    key="${pair%%|*}"; want="${pair##*|}"
    got=$(reading "$OUT/probe-mutation.txt" "$key")
    if [ "$got" = "$want" ]; then
      printf 'RESTORED-VALUE=PASS (%s=%s)\n' "$key" "$got"
    else
      printf 'RESTORED-VALUE=FAIL (%s=%s want=%s)\n' "$key" "$got" "$want"; fixed_ok=0; rc_all=1
    fi
  done
  if [ "$fixed_ok" -eq 1 ]; then echo "MUTATION-SUITE=PASS"; else echo "MUTATION-SUITE=FAIL"; fi
fi

echo
if [ "$rc_all" -eq 0 ]; then echo "VPK-BUILD=PASS"; else echo "VPK-BUILD=FAIL"; fi
exit "$rc_all"
