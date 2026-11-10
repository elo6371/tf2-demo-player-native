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
#   C7 the build rewrites an ignored output                         -> PASS (not an input)
#   C8 comment-only edit: exe bytes identical, content moved        -> PASS by identity
#      (D-54: the pre-D-54 rule failed this shape; see the inline replay)
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
# mock build-target.sh: deterministic "link" = CODE hash of the inputs. Comments
# are stripped before hashing because that is what a compiler does: a comment-only
# edit compiles to the same bytes. The tree CONTENT hash (what the D-54 sidecar
# records) is a different number and does change -- that gap is case C8.
set -uo pipefail
BUILD="${TF2_BUILD_DIR:-native/build-nmake}"
mkdir -p "$BUILD"
# Inputs are tracked files only, like the real gate: native/generated is the
# build's own ignored output and must never feed the link identity.
mock_code_sha() {
  find native/src native/include native/tools -type f 2>/dev/null | LC_ALL=C sort |
    while IFS= read -r f; do
      printf '%s\n' "$f"
      grep -v '^[[:space:]]*//' "$f" 2>/dev/null
    done | sha256sum | cut -d' ' -f1
}
mock_content_sha() {
  if [ -f build-identity-content.sh ]; then bash build-identity-content.sh hash 2>/dev/null
  else printf 'unknown\n'; fi
}
[ -n "${MOCK_WRITE_GENERATED:-}" ] && printf 'stamp:%s\n' "${RANDOM}" > native/generated/tf2_build_stamp.cpp
h=$(mock_code_sha)
for t in "$@"; do
  # tf2_identity is an ALL target that writes sidecars; it links no binary.
  [ "$t" = tf2_identity ] && continue
  exe="$BUILD/$t.exe"
  [ -n "${MOCK_FAIL:-}" ] && exit 3
  # MOCK_LIE models a build that relinks nothing AND refreshes nothing: the
  # stale sidecar / manifest is what the gate must catch.
  [ -n "${MOCK_LIE:-}" ] && { [ -f "$exe" ] || printf 'built:%s\n' "$h" > "$exe"; continue; }
  if [ -z "${MOCK_CHURN:-}" ] && [ -f "$exe" ] && [ "$(cat "$exe")" = "built:$h" ]; then
    :
  elif [ -n "${MOCK_CHURN:-}" ]; then
    printf 'built:%s:%s\n' "$h" "${RANDOM}" > "$exe"
  else
    printf 'built:%s\n' "$h" > "$exe"
  fi
  if [ "$t" = tf2_demo_native ]; then
    printf 'format=tf2-build-identity-sidecar\nformatVersion=1\ntarget=%s\nexeSha256=%s\nsourceContent=%s\ncontentMethod=git-clean-v1\n' \
      "$t" "$(sha256sum "$exe" | cut -d' ' -f1)" "$(mock_content_sha)" > "$BUILD/$t.identity"
  fi
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
  mkdir -p "$dir/native/cmake"
  printf '# settings, no comment yet\n' > "$dir/native/cmake/settings.cmake"
  local t
  for t in entity_overlay_probe entity_model_probe entity_protocol_probe world_geometry_probe; do
    printf '// probe\n' > "$dir/native/tools/$t.cpp"
  done
  : > "$dir/native/build-nmake/Makefile"
  cp "$HERE/release-rebuild.sh" "$dir/release-rebuild.sh"
  # D-54: the gate re-derives sourceContent with this tool; it must sit next to
  # the script under test, exactly as it does at the repository root.
  cp "$HERE/build-identity-content.sh" "$dir/build-identity-content.sh"
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
expect_has "C1b product answers by identity, not by bytes or mtime" "$WORK/c1b.log" "basis=identity-agrees"
expect_lacks "C1b no false REBUILD-FRESH=FAIL" "$WORK/c1b.log" "REBUILD-FRESH=FAIL"
expect_has "C1b mtime_newer>0 is reported and tolerated" "$WORK/c1b.log" "mtime_newer="

# --- C2: nothing changed -----------------------------------------------------
rc=$(run_gate "$d" "$WORK/c2.log")
expect_rc "C2 unchanged tree passes" "$rc" 0
expect_has "C2 product still answers by identity" "$WORK/c2.log" "basis=identity-agrees"
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
expect_has "C4 product caught by identity, not by churn" "$WORK/c4.log" "reason=identity-disagrees target=tf2_demo_native"
expect_has "C4 probes still caught by the manifest rule" "$WORK/c4.log" "reason=inputs-changed-binary-not-rebuilt"

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

# --- C8: comment-only edit -- exe bytes identical, content moved -------------
# D-54 red-first evidence. The pre-D-54 rule asked "did the binary change when the
# inputs changed", so a comment edit -- new input bytes, byte-identical exe --
# was a FAIL on a perfectly fresh tree. The identity rule asks instead whether the
# content the build recorded is the content on disk, which it is. The pre-D-54
# predicate is re-evaluated below from the manifest the previous PASS wrote, so
# the red is demonstrated in the same tree state, not asserted from memory.
d8=$(new_case c8)
rc=$(run_gate "$d8" "$WORK/c8-pre.log")
expect_rc "C8 setup run passes and records a manifest" "$rc" 0
old_in=$(sed -n 's/^inputs_sha256=//p' "$d8/native/build-nmake/.rebuild-inputs/tf2_demo_native.env")
old_bin=$(sed -n 's/^binary_sha256=//p' "$d8/native/build-nmake/.rebuild-inputs/tf2_demo_native.env")
exe_before=$(sha256sum "$d8/native/build-nmake/tf2_demo_native.exe" | cut -d' ' -f1)
# The edit lands in native/cmake -- inside the product's CONTENT set (so the
# identity moves) and outside every target's manifest input list, so this case
# isolates the identity rule from the manifest rule.
printf '# a comment only: no rule, no codegen\n' >> "$d8/native/cmake/settings.cmake"
rc=$(run_gate "$d8" "$WORK/c8.log")
exe_after=$(sha256sum "$d8/native/build-nmake/tf2_demo_native.exe" | cut -d' ' -f1)
new_in=$(sed -n 's/^inputs_sha256=//p' "$d8/native/build-nmake/.rebuild-inputs/tf2_demo_native.env")
expect_rc "C8 comment-only edit passes" "$rc" 0
expect_str "C8 the exe did not change (a comment is not code)" "$exe_after" "$exe_before"
expect_has "C8 verdict names identity-agrees, not rebuilt" "$WORK/c8.log" "basis=identity-agrees"
expect_lacks "C8 no identity-disagrees on a settled tree" "$WORK/c8.log" "reason=identity-disagrees"
if [ -n "$old_in" ] && [ "$old_in" != "$new_in" ] && [ "$old_bin" = "$exe_after" ]; then
  ok "C8 the pre-D-54 rule would have failed here (inputs moved, binary did not)"
else
  bad "C8 the pre-D-54 red was not reproducible (old_in=${old_in:0:12} new_in=${new_in:0:12} old_bin=${old_bin:0:12} exe=${exe_after:0:12})"
fi

echo
if [ "$failures" -ne 0 ]; then
  echo "RELEASE-REBUILD-SELFTEST=FAIL assertions=$count failures=$failures"
  exit 1
fi
echo "RELEASE-REBUILD-SELFTEST=PASS assertions=$count"