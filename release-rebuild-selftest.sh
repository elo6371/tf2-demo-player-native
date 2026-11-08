#!/usr/bin/env bash
# release-rebuild-selftest.sh -- static proof that release-rebuild.sh tells the
# four tree states apart, with a MOCK build (no compiler, no product, no GPU).
#
# Why a mock: the defect is in the gate's decision logic, not in the build. The
# four states below are constructed by writing files and touching mtimes, which
# is exactly what a clean checkout does to a build tree. build-target.sh is
# replaced by a 20-line script that (a) links an exe whose content is the hash of
# its inputs, (b) does nothing when that hash already matches, so "no-op" is
# real, and (c) can be told to lie (MOCK_LIE) or to churn (MOCK_CHURN).
#
# States:
#   C1 clean checkout: sources newer than the exe, content unchanged -> PASS
#      (the 2026-11-07 false red: the old mtime rule failed here)
#   C2 nothing changed                                              -> PASS, no churn
#   C3 source content changed                                       -> PASS, rebuilt
#   C4 content changed, build does nothing (lying build)            -> FAIL, named
#   C5 second build churns (not idempotent)                         -> FAIL, named
#   C6 no build tree                                                -> exit 2
#
# Usage: bash release-rebuild-selftest.sh
# Exit: 0 every assertion held, 1 one did not. Prints RELEASE-REBUILD-SELFTEST=PASS assertions=N
set -uo pipefail
cd "$(dirname "$0")" || exit 2
HERE=$(pwd)
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT INT TERM

failures=0
count=0
ok() { count=$((count + 1)); printf '  OK   %s\n' "$1"; }
bad() { count=$((count + 1)); failures=$((failures + 1)); printf '  FAIL %s\n' "$1"; }
expect_rc() { # expect_rc <label> <actual> <expected>
  if [ "$2" = "$3" ]; then ok "$1 (rc=$3)"; else bad "$1 (rc=$2, expected $3)"; fi
}
expect_str() { # expect_str <label> <actual> <expected>
  if [ "$2" = "$3" ]; then ok "$1 = $3"; else bad "$1 = $2, expected $3"; fi
}
expect_has() { # expect_has <label> <file> <substring>
  if grep -qaF "$3" "$2"; then ok "$1"; else bad "$1 (no '$3' in $2)"; fi
}
expect_lacks() { # expect_lacks <label> <file> <substring>
  if grep -qaF "$3" "$2"; then bad "$1 (found '$3' in $2)"; else ok "$1"; fi
}

write_mock_build() { # write_mock_build <dir>
  cat > "$1/build-target.sh" <<'MOCK'
#!/usr/bin/env bash
# mock build-target.sh: deterministic "link" = content hash of native/ inputs.
set -uo pipefail
BUILD="${TF2_BUILD_DIR:-native/build-nmake}"
mkdir -p "$BUILD"
# The mock build hashes tracked inputs only, like the real gate: native/generated
# is the build's own ignored output and must never feed the link identity.
mock_sha() { find native/src native/include native/tools -type f 2>/dev/null | LC_ALL=C sort | xargs -d '\n' -r sha256sum | LC_ALL=C sort | sha256sum | cut -d' ' -f1; }
[ -n "${MOCK_WRITE_GENERATED:-}" ] && printf 'stamp:%s\n' "${RANDOM}" > native/generated/tf2_build_stamp.cpp
h=$(mock_sha)
for t in "$@"; do
  exe="$BUILD/$t.exe"
  [ -n "${MOCK_FAIL:-}" ] && exit 3
  [ -n "${MOCK_LIE:-}" ] && { [ -f "$exe" ] || printf 'built:%s\n' "$h" > "$exe"; continue; }
  if [ -z "${MOCK_CHURN:-}" ] && [ -f "$exe" ] && [ "$(cat "$exe")" = "built:$h" ]; then continue; fi
  if [ -n "${MOCK_CHURN:-}" ]; then printf 'built:%s:%s\n' "$h" "${RANDOM}" > "$exe"; else printf 'built:%s\n' "$h" > "$exe"; fi
done
MOCK
  chmod +x "$1/build-target.sh"
}

new_case() { # new_case <name> -> echoes the case dir, creates tree + exes
  local dir="$WORK/$1"
  mkdir -p "$dir/native/src" "$dir/native/include" "$dir/native/generated" "$dir/native/tools" "$dir/native/build-nmake"
  printf 'int a;\n' > "$dir/native/src/a.cpp"
  printf '// header\n' > "$dir/native/include/a.h"
  printf '// generated\n' > "$dir/native/generated/g.h"
  local t
  for t in entity_overlay_probe entity_model_probe entity_protocol_probe world_geometry_probe; do
    printf '// probe\n' > "$dir/native/tools/$t.cpp"
  done
  : > "$dir/native/build-nmake/Makefile"
  cp "$HERE/release-rebuild.sh" "$dir/release-rebuild.sh"
  # The gate reads git-tracked inputs; give the mock tree a real index and make
  # native/generated ignored exactly as it is in the product tree (.gitignore:6).
  ( cd "$dir" && git init -q && printf 'native/generated\n' > .gitignore && git add -A ) >/dev/null 2>&1
  write_mock_build "$dir"
  printf '%s' "$dir"
}

run_gate() { # run_gate <dir> <logfile> [env pairs...]
  local dir="$1" log="$2"; shift 2
  ( cd "$dir" && env "$@" bash release-rebuild.sh > "$log" 2>&1 )
  printf '%s' "$?"
}

echo "=== release-rebuild selftest (mock build, no compiler) ==="

# --- C1: clean checkout -- sources newer than the exe, content unchanged ----
d=$(new_case c1)
( cd "$d" && bash build-target.sh entity_overlay_probe entity_model_probe entity_protocol_probe world_geometry_probe tf2_demo_native >/dev/null 2>&1 )
find "$d/native/src" "$d/native/include" "$d/native/generated" "$d/native/tools" -type f -exec touch -d '+2 hours' {} +
rc=$(run_gate "$d" "$WORK/c1.log")
expect_rc "C1 clean checkout with newer sources passes" "$rc" 0
expect_has "C1 first run is named settled-first-run (no manifest yet)" "$WORK/c1.log" "basis=settled-first-run"
expect_has "C1 mtime_newer is reported as a diagnostic" "$WORK/c1.log" "mtime_newer="
expect_lacks "C1 no REBUILD-FRESH=FAIL on unchanged bytes" "$WORK/c1.log" "REBUILD-FRESH=FAIL"
if [ -f "$d/native/build-nmake/.rebuild-inputs/tf2_demo_native.env" ]; then ok "C1 inputs manifest exists"; else bad "C1 inputs manifest missing"; fi

# --- C1b: the 2026-11-07 false red, now with a manifest present -------------
# Sources are touched again (new mtime, same bytes). The old rule failed here;
# the content rule must pass and must name content-identity as the basis.
find "$d/native/src" "$d/native/include" "$d/native/generated" "$d/native/tools" -type f -exec touch -d '+4 hours' {} +
rc=$(run_gate "$d" "$WORK/c1b.log")
expect_rc "C1b touched sources with unchanged bytes pass" "$rc" 0
expect_has "C1b basis is content-identical, not mtime" "$WORK/c1b.log" "basis=content-identical"
expect_lacks "C1b no false REBUILD-FRESH=FAIL" "$WORK/c1b.log" "REBUILD-FRESH=FAIL"
expect_has "C1b mtime_newer>0 is reported and tolerated" "$WORK/c1b.log" "mtime_newer="

# --- C2: nothing changed -----------------------------------------------------
rc=$(run_gate "$d" "$WORK/c2.log")
expect_rc "C2 unchanged tree passes" "$rc" 0
expect_has "C2 still content-identical" "$WORK/c2.log" "basis=content-identical"
expect_has "C2 rebuilt=0" "$WORK/c2.log" "rebuilt=0"

# --- C3: source content changed ---------------------------------------------
printf 'int a;\nint b;\n' > "$d/native/src/a.cpp"
rc=$(run_gate "$d" "$WORK/c3.log")
expect_rc "C3 changed source passes after a real rebuild" "$rc" 0
expect_has "C3 verdict says rebuilt" "$WORK/c3.log" "basis=rebuilt"
expect_lacks "C3 no stale failure" "$WORK/c3.log" "REBUILD-FRESH=FAIL"

# --- C4: content changed, build lies (no rebuild) ----------------------------
d4=$(new_case c4)
( cd "$d4" && bash build-target.sh entity_overlay_probe entity_model_probe entity_protocol_probe world_geometry_probe tf2_demo_native >/dev/null 2>&1 )
rc=$(run_gate "$d4" "$WORK/c4-pre.log")   # establish a manifest on a passing run
expect_rc "C4 setup run passes and records the manifest" "$rc" 0
printf 'int a;\nint lied;\n' > "$d4/native/src/a.cpp"
rc=$(run_gate "$d4" "$WORK/c4.log" MOCK_LIE=1)
expect_rc "C4 lying build fails" "$rc" 1
expect_has "C4 named reason" "$WORK/c4.log" "reason=inputs-changed-binary-not-rebuilt"

# --- C5: second build churns -------------------------------------------------
d5=$(new_case c5)
rc=$(run_gate "$d5" "$WORK/c5.log" MOCK_CHURN=1)
expect_rc "C5 churning build fails" "$rc" 1
expect_has "C5 named reason" "$WORK/c5.log" "reason=not-idempotent"

# --- C6: no build tree -------------------------------------------------------
d6=$(new_case c6)
rm -f "$d6/native/build-nmake/Makefile"
rc=$(run_gate "$d6" "$WORK/c6.log")
expect_rc "C6 refuses without a build tree" "$rc" 2
expect_has "C6 named reason" "$WORK/c6.log" "reason=no-build-tree"

# --- C7: the build writes an ignored output while the gate runs --------------
# The 2026-11-08 false red: native/generated/tf2_build_stamp.* is rewritten by
# every build and is ignored by git. Hashing it as an input made the gate fail
# on its own build output. Ignored files are not inputs; this case pins that.
d7=$(new_case c7)
rc=$(run_gate "$d7" "$WORK/c7.log" MOCK_WRITE_GENERATED=1)
expect_rc "C7 a build that rewrites an ignored output passes" "$rc" 0
expect_lacks "C7 no inputs-changed-during-run on an ignored output" "$WORK/c7.log" "inputs changed while the gate was running"
expect_has "C7 verdict still names a basis" "$WORK/c7.log" "basis="

echo
if [ "$failures" -ne 0 ]; then
  echo "RELEASE-REBUILD-SELFTEST=FAIL assertions=$count failures=$failures"
  exit 1
fi
echo "RELEASE-REBUILD-SELFTEST=PASS assertions=$count"