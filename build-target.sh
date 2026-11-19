#!/usr/bin/env bash
# build-target.sh <target> [target...]  -- build specific CMake targets.
#
# WHY THIS FILE EXISTS
#   build-cmake.sh is a *clean* build: it runs `cmake --build --target clean`
#   first, so it costs ~4 minutes and relinks all 23 targets. A round that
#   changes one .cpp does not need that, and paying it on every step is part of
#   what made the one-hour chain the only gate anyone ran. This script builds
#   only the named targets, so an unchanged target costs nothing.
#
# EXIT STATUS IS THE BUILD STATUS
#   It used to print `rc=` per target and always exit 0, on the theory that a
#   human was reading the output. Every caller that piped it to /dev/null then
#   swallowed a failed build, and a stale binary makes the gate that follows read
#   green for the wrong reason -- the same class as the 2026-10-06
#   `exe_count=17` incident, where a link error stopped NMAKE and four targets
#   were never built. The `|| { echo "mutation: rebuild failed"; exit 1; }` guard
#   in entity-property-lookup-check.sh was a no-op for exactly this reason. A
#   build failure now exits non-zero and prints the compiler errors.
#
# ALL TARGETS IN ONE INVOCATION
#   One `cmake --build` call for the whole list, not one per target. NMake
#   re-scans the dependency graph per invocation: measured on 2026-10-09, the six
#   targets verify-fast.sh needs cost 35 s as six calls and 14 s as one. The
#   per-target `rc=` line went away with the loop; what it reported (which target
#   broke) is already in the compiler error lines, which are printed verbatim.
#
# NO CONFIGURE ON THE STEADY PATH
#   Configure runs only when there is no build tree. CMake's generated NMake
#   files carry a rule that re-runs cmake when `CMakeLists.txt` changes, so a new
#   target is still picked up by `cmake --build` alone; running configure
#   unconditionally cost 22 s per call on this machine and bought nothing.
#
# HEADER DEPENDENCIES: TRACKED NOW, STAMP GUARD KEPT AS A FALLBACK
#   The 2026-10-11 reading was "every `.obj.d` under the build tree is empty:
#   144 of 144". CMake handed `cmake_cl_compile_depends` a localized
#   `--filter-prefix` ("注意: 包含文件:") captured at configure time, the bytes
#   cl.exe emitted at build time did not match it, so no include line was ever
#   recorded. The failure was silent and it bit this worktree: adding a member to
#   `native_renderer.h` left `main.cpp.obj` stale, the two objects then disagreed
#   about the struct layout, and a run printed `skinning=1` and `alignment=-` on a
#   line whose own log had just recorded `alignment=weapon_bone`.
#
#   That is not what this tree does any more. Measured 2026-11-18 (N26
#   follow-up, docs/N26-DEPENDENCY-ACCURACY-2026-11-18.md): the depend files are
#   populated and nmake rebuilds exactly the objects they name -- touching
#   `native_renderer.h` rebuilt 19 objects, `hud_font.h` 12, `item_schema.h` 31,
#   each equal to the include scan's prediction and each well under the blanket
#   touch's 87 objects / 129 s (12 took 24 s).
#
#   So the guard below trusts the depend files when every changed header is named
#   in one, and falls back to touching every source when a changed header is not
#   named in any of them. That fallback is also what happens if the localized
#   prefix bug ever returns, because empty depend files name no header. The stamp
#   lives in the build tree, so a clean build resets it.
#
# Usage: bash build-target.sh entity_model_probe [tf2_demo_native ...]
# Exit:  0 = the named targets built; 1 = they did not.
set -uo pipefail
cd "$(dirname "$0")"

if [ "$#" -eq 0 ]; then
  echo "usage: bash build-target.sh <target> [target...]" >&2
  exit 2
fi

# E3b: a toolchain that cannot be established must stop the build HERE, by name.
# Before this guard the source returned 1 into a `set -u` script that carried on
# with no cl.exe on PATH, and the failure surfaced later as a confusing compile
# or configure error. env-msvc.sh prints the named reason (tools/tf2-toolchain.sh).
source ./env-msvc.sh || {
  echo "build-target.sh: FAILED to establish the MSVC/SDK environment; no target was built" >&2
  exit 1
}

BUILD="${TF2_BUILD_DIR:-native/build-nmake}"
# The build log is deliberately a fixed, repo-local path rather than mktemp:
# mktemp returns a Windows-style %TEMP% path here, which the sandbox's safe-delete
# wrapper refuses to remove, and a named log survives a failure so the compiler
# errors can be re-read instead of the build being re-run.
# T15: the log follows the build directory. A clean-directory build writes
# somewhere else, and two builders sharing one log path truncate each other's
# output -- which is the same "failure preserved, evidence lost" problem.
LOG_SUFFIX=""
if [ -n "${TF2_BUILD_DIR:-}" ]; then LOG_SUFFIX="-$(basename "$BUILD")"; fi
LOG=".scratch/build-target${LOG_SUFFIX}.log"
mkdir -p .scratch

# A BRAND-NEW TARGET IS NOT PICKED UP BY `cmake --build` -- CONFIGURE FIRST
#   The note above says CMake's NMake files re-run cmake when CMakeLists.txt
#   changes, so a new target needs nothing extra. That is true for the targets
#   the *last* configure emitted: each of them has a `name: cmake_check_build_system`
#   rule in the generated Makefile, so invoking it regenerates first. A target
#   that did not exist at configure time has no rule at all, and `nmake name`
#   then dies on
#     NMAKE : fatal error U1073: 不知道如何生成目标 "name"
#   before any check rule can fire. Reproduced on 2026-10-16 while adding
#   sound_corpus_scan_probe: `bash build-target.sh sound_corpus_scan_probe`
#   exited 2 with that message against an up-to-date tree.
#   So: if any requested target has no rule in the build tree, configure once
#   here. The rule is looked up in the generated Makefile rather than inferred
#   from CMakeLists.txt so the check reads the same artifact nmake reads.
NEED_CONFIGURE=0
FRESH_TREE=0
if [ ! -f "$BUILD/CMakeCache.txt" ]; then
  NEED_CONFIGURE=1
  # A build tree that has never been configured has no objects to be stale, so
  # every source compiles no matter what the timestamps say. The stamp guard
  # below exists to defeat NMAKE's missing header dependencies in an INCREMENTAL
  # tree; in a fresh one it has nothing to protect and one real side effect: it
  # touches every source, which makes those sources newer than the binaries in
  # every *other* build directory. T15 found this by running the clean-directory
  # test and then discovering the main tree's exes were older than the sources.
  FRESH_TREE=1
else
  for _t in "$@"; do
    if ! grep -q "^${_t}:" "$BUILD/Makefile" 2>/dev/null; then
      echo "note: target '$_t' has no rule in $BUILD/Makefile; configuring"
      NEED_CONFIGURE=1
      break
    fi
  done
  # N24 (2026-11-19): a source named in native/cmake but absent from the
  # configured object list is a RECONFIGURE bug, not a compile bug. A tool that
  # preserves mtimes (explicit timestamps, some editors) leaves every build
  # dependency older than the cache, so `nmake`'s cmake_check_build_system sees
  # no change: it neither reconfigures nor relinks with the new translation unit.
  # Measured that day as `LNK2019: unresolved external symbol
  # runRendererSelftest` after main_selftest.cpp landed, which left the identity
  # gate with no exe to read. The check compares the product's source list in
  # CMake with the artifact nmake actually reads, and only when that target is
  # requested here.
  for _t in "$@"; do
    [ "$_t" = tf2_demo_native ] || continue
    _product_make="$BUILD/CMakeFiles/tf2_demo_native.dir/build.make"
    [ -f "$_product_make" ] || break
    for _src in $(awk '/^add_executable\(tf2_demo_native /{f=1} f&&/^\)/{f=0} f' \
        native/cmake/tf2-product.cmake | grep -oE 'src/[A-Za-z0-9_]+\.cpp'); do
      if ! grep -q "tf2_demo_native.dir/$_src.obj" "$_product_make"; then
        echo "note: $_src is named in native/cmake but missing from the configured link; configuring"
        NEED_CONFIGURE=1
        break
      fi
    done
    break
  done
fi
if [ "$NEED_CONFIGURE" -eq 1 ]; then
  echo "=== configure ==="
  cmake -S native -B "$BUILD" -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl > "$LOG" 2>&1
  if [ $? -ne 0 ]; then
    echo "configure FAILED; see $LOG"
    grep -a -E "error|Error" "$LOG" | head -20
    exit 1
  fi
fi

echo "=== build $* ==="
: > "$LOG"
STAMP="$BUILD/.headers-stamp"
if [ "$FRESH_TREE" -eq 1 ]; then
  # Nothing in this tree has been built yet, so nothing in it can be stale.
  echo "note: fresh build tree; header stamp guard not needed"
elif [ ! -f "$STAMP" ]; then
  echo "note: header stamp is missing; forcing requested source rebuild"
  touch native/src/*.cpp native/tools/*.cpp
else
  # N26 follow-up (measured 2026-11-18): this guard used to touch every source
  # whenever any header moved, on the belief that NMAKE has no header deps. It
  # does: CMake's MSVC/NMake backend writes per-object header lists into
  # CMakeFiles/<target>.dir/compiler_depend.make, and they are accurate --
  # touching native/include/native_renderer.h rebuilt 19 objects in 62 s (static
  # fanout 18 TUs plus one probe object) and native/include/hud_font.h rebuilt 12
  # in 25 s, while the blanket touch rebuilt 87 in 129 s. So let nmake do its job
  # when every changed header is listed in at least one depend file, and fall
  # back to the blanket touch only for a header no depend file mentions: a
  # brand-new header that nothing has included yet cannot be reasoned about this
  # way, and that is exactly the case the old guard protected.
  CHANGED_HEADERS=$(find native/include native/src native/tools native/third_party \
    \( -name '*.h' -o -name '*.hpp' \) -newer "$STAMP" -print 2>/dev/null)
  UNTRACKED_HEADERS=""
  for _h in $CHANGED_HEADERS; do
    _tail=${_h#./}
    _tail=${_tail//\//\\}
    if ! grep -qF -- "$_tail" "$BUILD"/CMakeFiles/*/compiler_depend.make 2>/dev/null; then
      UNTRACKED_HEADERS="$UNTRACKED_HEADERS $_h"
    fi
  done
  if [ -n "$UNTRACKED_HEADERS" ]; then
    echo "note: no depend file mentions$UNTRACKED_HEADERS; touching sources (header deps unknown)"
    touch native/src/*.cpp native/tools/*.cpp
  elif [ -n "$CHANGED_HEADERS" ]; then
    echo "note: headers newer than $STAMP appear in the depend files; nmake rebuilds their dependents"
  fi
fi
cmake --build "$BUILD" --target "$@" > "$LOG" 2>&1
rc=$?
# A failed build is retried once. The failure this was added for (2026-10-10) was
# entity_message_fixture_probe.exe missing when mutate.sh case m5 rebuilt it,
# seconds after case m4 had run the same exe: Windows keeps an image locked until
# the previous process is fully gone, so the link cannot replace it. The retry
# cannot hide a real error -- a deterministic failure fails again, and the
# preserved log below is what gets read -- and it keeps a transient lock from
# being reported as a mutation that did not move its reading.
if [ "$rc" -ne 0 ]; then
  echo "build failed (rc=$rc); preserving $LOG.failed-attempt-1 and retrying once"
  cp "$LOG" "$LOG.failed-attempt-1" 2>/dev/null
  sleep 3
  cmake --build "$BUILD" --target "$@" > "$LOG" 2>&1
  rc=$?
fi
# Warnings are printed for a human; errors are printed and also decide the exit
# status. LNK errors are listed explicitly: a missing source in a target's link
# line does not contain the string "error C". `-a` because NMAKE's progress
# output contains bytes grep would otherwise treat as binary and report as
# "Binary file ... matches" instead of printing the line.
grep -a -E "error C|warning C|error MSB|fatal error|LNK[0-9]+" "$LOG" | head -20
# Keep the log of a build that is still failing: $LOG is a fixed path that the
# next build truncates, and a caller that redirects this script's output has no
# other way to read the compiler error afterwards.
if [ "$rc" -ne 0 ]; then
  cp "$LOG" "$LOG.failed" 2>/dev/null
  echo "build FAILED (rc=$rc); full output preserved at $LOG.failed"
else
  # Record that the tree is built with the headers as they are now. A header
  # touched after this point forces the sources to be touched on the next build.
  touch "$STAMP"
fi
echo "rc=$rc"
exit "$rc"
