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
# HEADER DEPENDENCIES ARE NOT TRACKED -- THE STAMP GUARD
#   Every `.obj.d` under the build tree is empty: 144 of 144, measured after a
#   clean build on 2026-10-11. CMake hands `cmake_cl_compile_depends` a localized
#   `--filter-prefix` ("注意: 包含文件:") captured at configure time, the bytes
#   cl.exe emits at build time do not match it, and so not one include line is
#   ever recorded. NMake therefore has no header dependencies at all, and the
#   failure is silent: editing a header does not rebuild the translation units
#   that include it, and the stale object links into the exe.
#
#   It already bit this worktree. Adding a member to `native_renderer.h` left
#   `main.cpp.obj` stale, the two objects then disagreed about the struct layout,
#   and a run printed `skinning=1` and `alignment=-` on a line whose own log had
#   just recorded `alignment=weapon_bone`. Until the generator is fixed, a header
#   newer than the stamp forces the requested targets to really rebuild by
#   touching their sources. The stamp lives in the build tree, so a clean build
#   resets it.
#
# Usage: bash build-target.sh entity_model_probe [tf2_demo_native ...]
# Exit:  0 = the named targets built; 1 = they did not.
set -uo pipefail
cd "$(dirname "$0")"

if [ "$#" -eq 0 ]; then
  echo "usage: bash build-target.sh <target> [target...]" >&2
  exit 2
fi

source ./env-msvc.sh

BUILD=native/build-nmake
# The build log is deliberately a fixed, repo-local path rather than mktemp:
# mktemp returns a Windows-style %TEMP% path here, which the sandbox's safe-delete
# wrapper refuses to remove, and a named log survives a failure so the compiler
# errors can be re-read instead of the build being re-run.
LOG=.scratch/build-target.log
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
if [ ! -f "$BUILD/CMakeCache.txt" ]; then
  NEED_CONFIGURE=1
else
  for _t in "$@"; do
    if ! grep -q "^${_t}:" "$BUILD/Makefile" 2>/dev/null; then
      echo "note: target '$_t' has no rule in $BUILD/Makefile; configuring"
      NEED_CONFIGURE=1
      break
    fi
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
if [ ! -f "$STAMP" ]; then
  echo "note: header stamp is missing; forcing requested source rebuild"
  touch native/src/*.cpp native/tools/*.cpp
elif [ -n "$(find native/include native/src native/tools native/third_party \
    \( -name '*.h' -o -name '*.hpp' \) -newer "$STAMP" -print -quit 2>/dev/null)" ]; then
  echo "note: a header is newer than $STAMP; touching sources (header deps are untracked)"
  touch native/src/*.cpp native/tools/*.cpp
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
