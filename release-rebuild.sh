#!/usr/bin/env bash
# release-rebuild.sh -- rebuild every binary the capture steps trust, then prove
# that the tree and those binaries AGREE.
#
# WHY THIS STEP EXISTS (2026-11-07 chain, round 2)
#   Step 4 (outline) failed after two seconds with
#     "native/build-nmake/tf2_demo_native.exe cannot be trusted ...
#      sources are newer than the binary: native/src/entity_model.cpp"
#   Nothing had edited that file. The mutation suite restores every case with
#   "git checkout -- native/", and a restore writes a NEW MTIME with unchanged
#   content. outline-check.sh trusts a prebuilt binary only while no source is
#   newer than it, so its stale guard fired on a tree whose bytes were correct.
#
#   THE FALSE RED THIS SCRIPT NOW AVOIDS (2026-11-07, T5A)
#   The first version of this step repeated the same mistake: it asked
#     find <inputs> -newer <exe>
#   "is a source newer than the binary?", which is NOT the question the build
#   system answers. A clean checkout writes every source with a fresh mtime while
#   the object files and the exe keep theirs; make compares source-to-OBJECT, not
#   source-to-exe, so the first build is a no-op and the exe stays older than the
#   sources. The gate then reported REBUILD-FRESH=FAIL on a tree that was byte
#   for byte the frozen commit, and the only way to silence it was to rebuild
#   until the clock agreed.
#
#   WHAT THE GATE ASKS NOW
#   Acceptance is "the tree and the binary agree", i.e. the build system has no
#   work left and the inputs the binary was built from are the inputs on disk.
#   That is a content question, so it is answered with content hashes:
#     - inputs_sha(target) = sha256 over the sorted "sha256  path" lines of every
#       file the target is allowed to depend on (per-target list below);
#     - a manifest written on every PASS records inputs_sha256 + binary_sha256;
#     - mtime is reported (mtime_newer=N) as a DIAGNOSTIC and never fails alone.
#
#   D-54 (2026-11-10): WHAT THE PRODUCT PROVES NOW
#   The manifest rule above still asks "did the binary change when the inputs
#   changed". For the product that question is wrong in the same way mtime was:
#   a docs-only commit changes the tree, is not a build input, and must NOT be
#   answered with a relink. So the product is judged by identity instead: the
#   build writes <build>/<target>.identity next to the exe, which records the
#   sourceContent it compiled (and the gate's own copy of the same hash comes
#   from build-identity-content.sh -- two independent implementations of one
#   definition). Agreement between the two, plus the sidecar still naming the
#   exe on disk, is the PASS. A changed binary is a DIAGNOSTIC for the product,
#   not a requirement.
#
#   The other four targets keep the manifest rule: they carry no sidecar and
#   their input set does not include the generated stamp.
#
#   D-93 (2026-11-16): WHICH INPUTS ARE HASHED
#   "Allowed to depend on" is not "actually read". The manifest rule now hashes
#   each target's ACTUAL compiler dependency set (see project_deps/dep_sha below)
#   instead of the directory superset, and records the definition that produced
#   it (deps_sha256 + deps_method) so two definitions are never compared. A
#   manifest written before this rule has no dependency baseline: the first run is
#   named deps-rule-first-run, records one, and every later run is strict. This is
#   a criterion fix, not a relaxation: a changed dependency still requires the
#   binary to change, and the superset still guards mutations during the run.
#
#   THE FIVE STATES (each has one named verdict field)
#     1. clean checkout, sources newer than the exe, content unchanged
#        -> basis=content-identical  PASS  (mtime_newer>0 is expected)
#     2. source content changed (mtime newer or not)
#        -> basis=rebuilt            PASS  (binary_sha256 changed)
#     3. source content changed but the binary did NOT change after both passes
#        -> REBUILD-FRESH=FAIL reason=inputs-changed-binary-not-rebuilt
#           (the build system is lying: this is the case that matters)
#     4. nothing changed
#        -> basis=content-identical  PASS  (no-op, no churn)
#     5. no dependency baseline yet (manifest from before the per-target rule)
#        -> basis=deps-rule-first-run  PASS  (baseline recorded, next run strict)
#   mtime alone never decides. The second build is still required: it is what
#   turns "the build system says it is done" into "a second run agrees".
#
#   NO RETRY LOOP. Two build passes, maximum, always. If pass 1 left the tree
#   stale, pass 2 is the forced rebuild the task allows; if pass 2 still does not
#   change the binary the answer is FAIL, not a third attempt. Termination is
#   structural (2 passes), not "until green".
#
# WHAT IT PROVES
#   1. all five targets link from the tree as it stands (build rc=0);
#   2. the inputs on disk are the inputs the binary was built from (content hash
#      vs manifest; without a manifest the inputs must at least be identical
#      before and after both passes, and the manifest is written);
#   3. a second build changes nothing -- the tree has no declared work left.
#
# VERDICT: REBUILD=PASS targets=5 rebuilt=N fresh=5 noop=5 basis=<identity-agrees|content-identical|rebuilt|deps-rule-first-run|settled-first-run|relinked-unchanged-inputs> mtime_newer=X exe_sha256=<sha>
#   rebuilt counts binaries whose sha changed across the two passes; rebuilt=0 is
#   a pass, the claim is agreement, not churn.
# Exit: 0 pass, 1 fail, 2 refused (no build tree / no such target / usage).
set -uo pipefail
cd "$(dirname "$0")" || exit 2

BUILD="${TF2_BUILD_DIR:-native/build-nmake}"
# world_geometry_probe is built here because step 10 (world-geometry-budget)
# refuses to run without it and only rebuilds it on the --mutation path: left out
# of this step it would be a stale binary the forward pass trusts.
# The product is kept in $PRIMARY and last in the array on purpose: this script
# builds and hashes the binaries, it never executes them, and the launch-hygiene
# scan reads a product name in command position (or first in an array body) as a
# launch. Keeping the name out of command position keeps the ratchet honest
# instead of teaching it a new exemption.
PRIMARY=tf2_demo_native
TARGETS=(entity_overlay_probe entity_model_probe entity_protocol_probe world_geometry_probe "$PRIMARY")
N=${#TARGETS[@]}

# D-54: every build pass also refreshes the identity sidecars. tf2_identity is
# an ALL target, so it is NOT pulled in by naming the binaries -- it has to be
# named explicitly, exactly as build-identity-check.sh does.
BUILD_TARGETS=(${TARGETS[@]} tf2_identity)
MANIFEST_DIR="$BUILD/.rebuild-inputs"

if [ ! -f "$BUILD/Makefile" ]; then
  echo "REBUILD=FAIL reason=no-build-tree build=$BUILD (build or configure it first)"
  exit 2
fi

exe_of() { printf '%s/%s.exe\n' "$BUILD" "$1"; }

# The inputs each binary is allowed to depend on. Restricted per target on
# purpose: native/tools holds ~40 probe sources that no shipped binary links, so
# a blanket "native/ is newer" check would fail on files that cannot stale it.
# Inputs are the files git tracks, never "every file on disk": a build rewrites
# its own ignored outputs (native/generated/tf2_build_stamp.*) while the gate is
# running, and those must not look like a changed input. In a tree without git
# the gate falls back to every file on disk (still correct, just blinder).
#
# D-54: the product's list is now the SAME set gen-build-stamp.cmake hashes
# (src, include, third_party, cmake, CMakeLists.txt) minus native/generated.
# native/generated is the build's own output twice over now: the generated TU
# AND the provenance/identity files are rewritten during a build, so keeping it
# in the input list would make every build look like an input change.
sources_of() {
  if [ "$1" = "$PRIMARY" ]; then
    printf '%s\n' native/src native/include native/third_party native/cmake \
      native/CMakeLists.txt
  else
    printf '%s\n' "native/tools/$1.cpp" native/src native/include
  fi
}

list_inputs() { # list_inputs <target> -- every existing input file, one per line
  local t="$1" p tracked=""
  local -a sources=()
  git rev-parse --is-inside-work-tree >/dev/null 2>&1 && tracked=1
  # D-93b: the sources are collected into an array before the loop. Under MSYS
  # bash a `while read` loop fed by a here-string loses the remaining lines as
  # soon as its body runs a pipeline headed by a native Windows binary
  # (git.exe): the here-string stdin reaches EOF after the first iteration, so
  # this function silently listed only native/src and inputs_sha256 never
  # covered native/include, native/third_party, native/cmake or
  # native/CMakeLists.txt. mapfile + for never reads stdin, so all sources
  # are enumerated.
  mapfile -t sources < <(sources_of "$t")
  for p in "${sources[@]}"; do
    [ -z "$p" ] && continue
    if [ -d "$p" ]; then
      if [ -n "$tracked" ]; then git ls-files -z -- "$p" | tr '\0' '\n'
      else find "$p" -type f 2>/dev/null; fi
    elif [ -f "$p" ]; then
      if [ -n "$tracked" ]; then
        git ls-files --error-unmatch -- "$p" >/dev/null 2>&1 && printf '%s\n' "$p"
      else printf '%s\n' "$p"; fi
    fi
  done
}

# --- per-target ACTUAL dependency set (D-93, 2026-11-16) ----------------------
# The allowed-input superset above answers "what may this binary depend on"; the
# manifest rule needs "what did it actually read". The build writes that answer
# itself: CMake's NMake generator records the compiler's dependency scan for each
# target in CMakeFiles/<target>.dir/compiler_depend.internal. Files outside the
# repository (MSVC / Windows SDK headers) are not part of the tree and are not
# hashed; the object line and the header lines are skipped.
#
# Why this exists: the 2026-11-16 merge moved native/src/native_renderer_frame.cpp.
# The four probes are allowed to depend on native/src, but none of them includes
# that file, so their unchanged binaries were correct and the superset rule
# failed them anyway. A dependency definition belongs with the build, not with a
# directory listing.
dep_file_of() { printf '%s/CMakeFiles/%s.dir/compiler_depend.internal\n' "$BUILD" "$1"; }

project_deps() { # project_deps <target> -- repo-relative compiler dependencies
  local t="$1" f roots
  f=$(dep_file_of "$t")
  [ -f "$f" ] || return 0
  # Root spellings differ between git (D:/...) and msys (which may hand back
  # /d/...); match against every spelling, longest first so a nested root wins.
  roots=$( { git rev-parse --show-toplevel 2>/dev/null; cygpath -m "$(pwd)" 2>/dev/null; pwd; } |
    sed 's#\\#/#g; s#/*$##' | awk '{ print length($0), $0 }' | LC_ALL=C sort -rn |
    cut -d' ' -f2- )
  awk -v roots="$roots" '
    BEGIN { n = split(roots, R, "\n") }
    {
      line = $0
      sub(/^[ \t]+/, "", line)
      if (line == "" || substr(line, 1, 1) == "#") next
      if (line ~ /\.obj$/) next
      p = line
      gsub(/\\/, "/", p)
      for (i = 1; i <= n; i++) {
        r = R[i]
        if (r == "") continue
        if (tolower(substr(p, 1, length(r) + 1)) == tolower(r "/")) {
          print substr(p, length(r) + 2)
          next
        }
      }
    }' "$f" | LC_ALL=C sort -u
}

dep_sha() { # dep_sha <target> -- content hash of the target's ACTUAL dependencies
  project_deps "$1" | xargs -d '\n' -r sha256sum 2>/dev/null \
    | LC_ALL=C sort | sha256sum | cut -d' ' -f1
}

dep_method_of() { # dep_method_of <target> -- names the definition used for dep_sha
  if [ -s "$(dep_file_of "$1")" ] && [ -n "$(project_deps "$1")" ]; then
    printf 'cmake-compiler-depend\n'
  else
    printf 'superset-fallback\n'
  fi
}

# Content identity of a target's inputs. The "sha256  path" lines are sorted as
# whole strings, which is deterministic because repository paths contain no
# newlines.
inputs_sha() {
  list_inputs "$1" | LC_ALL=C sort -u | xargs -d '\n' -r sha256sum 2>/dev/null \
    | LC_ALL=C sort | sha256sum | cut -d' ' -f1
}

sha_of() { if [ -f "$1" ]; then sha256sum "$1" | cut -d' ' -f1; else printf 'missing\n'; fi; }

# D-54 identity helpers. content_of re-derives the product's sourceContent with
# build-identity-content.sh, which implements the same git-clean-v1 definition as
# native/cmake/gen-build-stamp.cmake but shares no code with it: a bug in one
# implementation cannot make both agree by construction. It prints "unknown"
# when neither git nor a readable tree is available, which is a FAIL below, never
# a pass.
content_of() { bash build-identity-content.sh hash 2>/dev/null; }
content_method() { bash build-identity-content.sh method 2>/dev/null; }

sidecar_field() { # sidecar_field <file> <key> -- empty when absent
  [ -f "$1" ] || return 1
  sed -n "s/^$2=//p" "$1" | head -1
}

mtime_newer() { # mtime_newer <exe> <target> -- DIAGNOSTIC ONLY, never a verdict
  local e="$1" t="$2" n=0 f
  while IFS= read -r f; do
    [ -n "$f" ] && [ "$f" -nt "$e" ] && n=$((n + 1))
  done < <(list_inputs "$t")
  printf '%s' "$n"
}

manifest_get() { # manifest_get <target> <field>
  local f="$MANIFEST_DIR/$1.env"
  [ -f "$f" ] || return 1
  sed -n "s/^$2=//p" "$f" | head -1
}

manifest_put() { # manifest_put <target> <inputs_sha> <binary_sha> <deps_sha> <deps_method>
  mkdir -p "$MANIFEST_DIR"
  printf 'target=%s\ninputs_sha256=%s\nbinary_sha256=%s\ndeps_sha256=%s\ndeps_method=%s\nwritten_by=release-rebuild.sh\n' \
    "$1" "$2" "$3" "$4" "$5" > "$MANIFEST_DIR/$1.env"
}

declare -A BIN_BEFORE IN_BEFORE
for t in "${TARGETS[@]}"; do
  BIN_BEFORE[$t]=$(sha_of "$(exe_of "$t")")
  IN_BEFORE[$t]=$(inputs_sha "$t")
done

echo "=== build (pass 1 of 2) ==="
bash build-target.sh "${BUILD_TARGETS[@]}"
rc=$?
if [ "$rc" -ne 0 ]; then
  echo "REBUILD=FAIL reason=build-failed rc=$rc targets=${TARGETS[*]} log=.scratch/build-target.log"
  exit 1
fi

declare -A BIN_MID IN_MID
for t in "${TARGETS[@]}"; do
  BIN_MID[$t]=$(sha_of "$(exe_of "$t")")
  IN_MID[$t]=$(inputs_sha "$t")
  if [ "${BIN_MID[$t]}" = "missing" ]; then
    echo "REBUILD-FRESH=FAIL exe=$(exe_of "$t") missing after the first build"
    echo "REBUILD=FAIL reason=exe-not-built target=$t exe=$(exe_of "$t")"
    exit 1
  fi
  if [ "${IN_MID[$t]}" != "${IN_BEFORE[$t]}" ]; then
    echo "REBUILD-FRESH=FAIL exe=$(exe_of "$t") inputs changed while the gate was running (before=${IN_BEFORE[$t]:0:12} mid=${IN_MID[$t]:0:12})"
    echo "REBUILD=FAIL reason=inputs-changed-during-run target=$t"
    exit 1
  fi
done

echo "=== build (pass 2 of 2, must be a no-op) ==="
bash build-target.sh "${BUILD_TARGETS[@]}"
rc=$?
if [ "$rc" -ne 0 ]; then
  echo "REBUILD=FAIL reason=second-build-failed rc=$rc (the tree is not settled)"
  exit 1
fi

rebuilt=0
noop_changed=0
stale=0
basis="content-identical"
for t in "${TARGETS[@]}"; do
  e=$(exe_of "$t")
  in_now=$(inputs_sha "$t")
  bin_now=$(sha_of "$e")
  newer=$(mtime_newer "$e" "$t")
  if [ "$in_now" != "${IN_MID[$t]}" ]; then
    echo "REBUILD-FRESH=FAIL exe=$e inputs changed between the two builds (mid=${IN_MID[$t]:0:12} now=${in_now:0:12})"
    echo "REBUILD=FAIL reason=inputs-not-settled target=$t"
    exit 1
  fi
  [ "$bin_now" != "${BIN_MID[$t]}" ] && noop_changed=$((noop_changed + 1))
  [ "$bin_now" != "${BIN_BEFORE[$t]}" ] && rebuilt=$((rebuilt + 1))

  # The dependency baseline the manifest rule compares against (D-93).
  dep_now=$(dep_sha "$t")
  dep_method_now=$(dep_method_of "$t")

  t_fail=0
  if [ "$t" = "$PRIMARY" ]; then
    # D-54: the product proves freshness by identity, not by churn. The build
    # recorded the sourceContent it compiled; the gate recomputes that content
    # hash from the tree. Equal hashes plus a sidecar that still names the exe on
    # disk = the binary was built from exactly this tree. A relinked binary is
    # NOT required here: a docs-only commit changes the tree without changing any
    # build input, and demanding a new binary would fail that honest case.
    sc="$BUILD/$t.identity"
    recorded=$(sidecar_field "$sc" sourceContent)
    rec_method=$(sidecar_field "$sc" contentMethod)
    bound=$(sidecar_field "$sc" exeSha256)
    live=$(content_of)
    live_method=$(content_method)
    if [ -z "$recorded" ] || [ -z "$bound" ]; then
      echo "REBUILD-FRESH=FAIL reason=identity-sidecar-missing target=$t file=$sc (the build did not record what it compiled)"
      stale=1
      t_fail=1
    elif [ -z "$live" ] || [ "$live" = "unknown" ]; then
      echo "REBUILD-FRESH=FAIL reason=identity-unavailable target=$t (build-identity-content.sh could not hash the tree)"
      stale=1
      t_fail=1
    elif [ "$live_method" != "$rec_method" ]; then
      echo "REBUILD-FRESH=FAIL reason=identity-method-mismatch target=$t recorded=$rec_method live=$live_method (two hash methods are not comparable)"
      stale=1
      t_fail=1
    elif [ "$recorded" != "$live" ]; then
      echo "REBUILD-FRESH=FAIL reason=identity-disagrees target=$t sidecar=${recorded:0:12} live=${live:0:12} (the binary was built from content this tree no longer has)"
      stale=1
      t_fail=1
    elif [ "$bound" != "$bin_now" ]; then
      echo "REBUILD-FRESH=FAIL reason=sidecar-not-bound-to-binary target=$t sidecar_exe_sha256=${bound:0:12} exe=$bin_now (the sidecar describes some other binary)"
      stale=1
      t_fail=1
    else
      [ "$basis" = "content-identical" ] && basis="identity-agrees"
      echo "rebuild: target=$t state=identity-agrees exe=$e sha256=$bin_now sourceContent=${recorded:0:12} method=$rec_method mtime_newer=$newer"
    fi
  elif want_bin=$(manifest_get "$t" binary_sha256); then
    want_deps=$(manifest_get "$t" deps_sha256)
    want_method=$(manifest_get "$t" deps_method)
    if [ -z "$want_deps" ] || [ -z "$want_method" ]; then
      # Manifest written before the per-target dependency rule. There is no
      # dependency baseline to compare against, and comparing the old superset
      # hash with the new per-target hash would be a category error. The only
      # claim available is "settled": both passes agree and the second did no
      # work. Record the baseline; every later run is strict.
      [ "$basis" = "content-identical" ] && basis="deps-rule-first-run"
      echo "rebuild: target=$t state=deps-rule-first-run exe=$e sha256=$bin_now (no dependency baseline yet; recorded method=$dep_method_now)"
    elif [ "$want_method" != "$dep_method_now" ]; then
      echo "REBUILD-FRESH=FAIL reason=deps-method-changed target=$t recorded=$want_method now=$dep_method_now (two dependency definitions are not comparable)"
      stale=1
      t_fail=1
    elif [ "$want_deps" != "$dep_now" ]; then
      # The files this target actually reads changed since the last recorded
      # pass. The binary must have changed with them; if two build passes left it
      # identical, nothing rebuilt it.
      if [ "$bin_now" = "$want_bin" ]; then
        echo "REBUILD-FRESH=FAIL exe=$e did not change although its actual dependencies did (dependency scan: $(dep_file_of "$t"))"
        echo "REBUILD-FRESH=FAIL reason=inputs-changed-binary-not-rebuilt target=$t deps_sha256_was=${want_deps:0:12} now=${dep_now:0:12} binary_sha256=${bin_now:0:12} deps_method=$dep_method_now"
        stale=1
        t_fail=1
      else
        [ "$basis" = "content-identical" ] && basis="rebuilt"
        echo "rebuild: target=$t state=rebuilt exe=$e sha256=$bin_now (deps ${want_deps:0:12}->${dep_now:0:12}) mtime_newer=$newer"
      fi
    elif [ "$bin_now" != "$want_bin" ]; then
      # Same dependency content, different binary: the relink was caused by a
      # touch or by non-determinism. Agreement still holds after the rebuild, so
      # this is a note, not a failure -- the manifest is refreshed below.
      [ "$basis" = "content-identical" ] && basis="relinked-unchanged-inputs"
      echo "rebuild: target=$t state=relinked-unchanged-inputs exe=$e sha256=$bin_now (was ${want_bin:0:12}) mtime_newer=$newer"
    else
      echo "rebuild: target=$t state=content-identical exe=$e sha256=$bin_now mtime_newer=$newer"
    fi
  else
    # No manifest yet (first run on this build tree). The strongest statement
    # available is that the inputs were identical before and after both passes
    # and the second build refused to touch the binary -- the tree is settled.
    basis="settled-first-run"
    echo "rebuild: target=$t state=settled-first-run exe=$e sha256=$bin_now mtime_newer=$newer"
  fi
  # A failed target must not overwrite the baseline it just disproved: the next
  # run compares against the last PASS, never against a poisoned manifest.
  if [ "$t_fail" -eq 0 ]; then
    manifest_put "$t" "$in_now" "$bin_now" "$dep_now" "$dep_method_now"
  fi
done

if [ "$stale" -ne 0 ]; then
  echo "REBUILD=FAIL reason=source-newer-than-binary targets=$N (see REBUILD-FRESH=FAIL above)"
  exit 1
fi
if [ "$noop_changed" -ne 0 ]; then
  echo "REBUILD-NOOP=FAIL a second build changed $noop_changed/$N binaries -- something still declares itself out of date"
  echo "REBUILD=FAIL reason=not-idempotent noop=$noop_changed/$N"
  exit 1
fi

total_newer=0
# Split on purpose: this host's bash (5.3.20 msys) leaves $(( )) unexpanded when a
# command substitution with a quoted word is nested in it (tools/check-nested-arith.sh).
# mtime_newer() always prints a count, and this reading is a DIAGNOSTIC that never
# fails the gate on its own, so a bad reading is coerced without a FAIL.
for t in "${TARGETS[@]}"; do
  _newer_count=$(mtime_newer "$(exe_of "$t")" "$t")
  case "$_newer_count" in
    ''|*[!0-9]*) _newer_count=0 ;;
  esac
  total_newer=$((total_newer + _newer_count))
done
echo "REBUILD=PASS targets=$N rebuilt=$rebuilt fresh=$N noop=$N basis=$basis mtime_newer=$total_newer exe_sha256=$(sha_of "$(exe_of "$PRIMARY")")"
