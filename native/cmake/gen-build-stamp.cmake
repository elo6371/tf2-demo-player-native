# ---------------------------------------------------------------------------
# gen-build-stamp.cmake -- writes the build-identity stamp for the binary that
# is being linked right now.
#
# WHY THIS IS NOT A CONFIGURE-TIME MACRO (T90, 2026-11-07)
# -------------------------------------------------------
# tf2-version.cmake used to measure the source tree with configure-time
# commands and bake the answer into TF2_VERSION_DEFINES. Configure runs once;
# `cmake --build` (build-target.sh, every gate, verify-all) never re-runs it, so
# every executable linked from a cached build tree kept the commit and the wall
# clock of the LAST CONFIGURE while its objects were regenerated from newer
# sources. Measured on the frozen candidate
# t79-integration/native/build-nmake/tf2_demo_native.exe (sha256 a3c93d57...,
# mtime 2026-11-06 20:20:35) `--version` answered
#     buildUtc=2026-11-02T16:22:30Z sourceTree=12042361ffd1
# while HEAD was e15577b -- 41 commits later -- and the worktree had
# uncommitted changes. sourceDateEpoch answered 0 on every build ever made,
# because that line was a hard-coded "0" wearing a cache variable's clothes.
# A binary cannot honestly answer "which source am I" from the moment configure
# last happened; the answer has to be taken from the tree at build time.
#
# WHAT IT WRITES
# --------------
# native/generated/tf2_build_stamp.cpp -- one translation unit holding the
# identity as string constants, compiled into every target that compiles
# src/install_lifecycle.cpp (the product and install_cli_probe). A translation
# unit, not a generated header, because NMAKE has NO header dependencies in
# this tree (build-target.sh's header-stamp guard exists precisely because
# every .obj.d is empty): a generated header would change and its consumers
# would not recompile.
#
# IDENTITY MOVES ONLY WHEN THE SOURCE TREE MOVES
# ----------------------------------------------
# The identity measured here (HEAD, epoch, dirty flag and counts) is compared
# with the one written last time, in TF2_STAMP_CACHE. Unchanged -> the .cpp is
# left byte-identical, so neither the object nor the link can be triggered and
# a no-op build stays free; buildUtc therefore means "when this identity was
# first captured", not "when nmake last ran". Changed -> the .cpp is rewritten
# with a fresh UTC stamp, and the product relinks carrying the new identity.
#
# WRITE-IF-CHANGED uses `cmake -E copy_if_different`, never file(WRITE): an
# unconditional rewrite bumps the mtime of an identical file, and NMAKE would
# then recompile the stamp and relink on every single build.
#
# FAILURE
# -------
# An unavailable git is a READING, not a build failure: sourceTree=unknown,
# sourceDateEpoch=0 and dirty=-1 ("not measured"), exactly the fallback the
# configure-time code had, so a failed link still truncates the target list at
# the same place. dirty is never reported as 0 when git could not answer -- an
# unmeasurable tree must not claim to be clean. A stamp that cannot be WRITTEN,
# on the other hand, is fatal: linking a binary whose identity was invented is
# the defect this file exists to remove.
#
# Inputs (all three required):
#   -DTF2_REPO_DIR=<dir>      git worktree root to measure (native/)
#   -DTF2_STAMP_OUTPUT=<file> generated .cpp to (re)write
#   -DTF2_STAMP_CACHE=<file>  identity of the last stamp written, minus buildUtc
# Run in script mode:  cmake -DTF2_REPO_DIR=... -P gen-build-stamp.cmake
# ---------------------------------------------------------------------------

if (NOT DEFINED TF2_REPO_DIR OR NOT DEFINED TF2_STAMP_OUTPUT OR NOT DEFINED TF2_STAMP_CACHE)
  message(FATAL_ERROR "gen-build-stamp.cmake: TF2_REPO_DIR, TF2_STAMP_OUTPUT and TF2_STAMP_CACHE are all required")
endif()

# Measured defaults: "could not look" is said out loud, never rounded to clean.
set(TF2_TREE "unknown")
set(TF2_EPOCH "0")
set(TF2_DIRTY "-1")
set(TF2_DIRTY_FILES "-1")
set(TF2_DIRTY_TRACKED "-1")
set(TF2_DIRTY_UNTRACKED "-1")

execute_process(
  COMMAND git rev-parse --short=12 HEAD
  WORKING_DIRECTORY "${TF2_REPO_DIR}"
  OUTPUT_VARIABLE TF2_TREE_OUTPUT
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET
  RESULT_VARIABLE TF2_TREE_RC)
if (TF2_TREE_RC EQUAL 0 AND NOT TF2_TREE_OUTPUT STREQUAL "")
  set(TF2_TREE "${TF2_TREE_OUTPUT}")

  # SOURCE_DATE_EPOCH convention: the committer epoch of the source commit.
  execute_process(
    COMMAND git log -1 --format=%ct HEAD
    WORKING_DIRECTORY "${TF2_REPO_DIR}"
    OUTPUT_VARIABLE TF2_EPOCH_OUTPUT
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
    RESULT_VARIABLE TF2_EPOCH_RC)
  if (TF2_EPOCH_RC EQUAL 0 AND TF2_EPOCH_OUTPUT MATCHES "^[0-9]+$")
    set(TF2_EPOCH "${TF2_EPOCH_OUTPUT}")
  endif()

  # Dirty state, counted the way a human checks it:
  #   git status --porcelain -uall | wc -l
  # -uall so an untracked directory is counted per file, not as one entry.
  execute_process(
    COMMAND git status --porcelain -uall
    WORKING_DIRECTORY "${TF2_REPO_DIR}"
    OUTPUT_VARIABLE TF2_STATUS
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
    RESULT_VARIABLE TF2_STATUS_RC)
  if (TF2_STATUS_RC EQUAL 0)
    set(TF2_TRACKED_COUNT 0)
    set(TF2_UNTRACKED_COUNT 0)
    set(TF2_RECORD_COUNT 0)
    if (NOT TF2_STATUS STREQUAL "")
      string(REPLACE "\r\n" "\n" TF2_STATUS "${TF2_STATUS}")
      string(REPLACE "\n" ";" TF2_STATUS_RECORDS "${TF2_STATUS}")
      foreach(TF2_RECORD IN LISTS TF2_STATUS_RECORDS)
        if (TF2_RECORD STREQUAL "")
          continue()
        endif()
        string(SUBSTRING "${TF2_RECORD}" 0 2 TF2_RECORD_KIND)
        if (TF2_RECORD_KIND STREQUAL "??")
          math(EXPR TF2_UNTRACKED_COUNT "${TF2_UNTRACKED_COUNT} + 1")
        else()
          math(EXPR TF2_TRACKED_COUNT "${TF2_TRACKED_COUNT} + 1")
        endif()
        math(EXPR TF2_RECORD_COUNT "${TF2_RECORD_COUNT} + 1")
      endforeach()
    endif()
    if (TF2_RECORD_COUNT GREATER 0)
      set(TF2_DIRTY "1")
    else()
      set(TF2_DIRTY "0")
    endif()
    set(TF2_DIRTY_FILES "${TF2_RECORD_COUNT}")
    set(TF2_DIRTY_TRACKED "${TF2_TRACKED_COUNT}")
    set(TF2_DIRTY_UNTRACKED "${TF2_UNTRACKED_COUNT}")
  endif()
endif()

string(TIMESTAMP TF2_NOW_UTC "%Y-%m-%dT%H:%M:%SZ" UTC)

# The identity WITHOUT the timestamp: two builds of the same source state share
# it, so the second one writes nothing and the stamp (and its buildUtc) survives.
set(TF2_IDENTITY "sourceTree=${TF2_TREE}\nsourceDateEpoch=${TF2_EPOCH}\ndirty=${TF2_DIRTY}\ndirtyFiles=${TF2_DIRTY_FILES}\ndirtyTracked=${TF2_DIRTY_TRACKED}\ndirtyUntracked=${TF2_DIRTY_UNTRACKED}\n")

set(TF2_NEEDS_WRITE TRUE)
if (EXISTS "${TF2_STAMP_OUTPUT}" AND EXISTS "${TF2_STAMP_CACHE}")
  file(READ "${TF2_STAMP_CACHE}" TF2_PREVIOUS_IDENTITY)
  if (TF2_PREVIOUS_IDENTITY STREQUAL TF2_IDENTITY)
    set(TF2_NEEDS_WRITE FALSE)
  endif()
endif()

if (TF2_NEEDS_WRITE)
  set(TF2_STAMP_TEXT "")
  string(APPEND TF2_STAMP_TEXT
    "// GENERATED AT BUILD TIME -- DO NOT EDIT.\n"
    "//\n"
    "// Written by native/cmake/gen-build-stamp.cmake through the tf2_build_stamp\n"
    "// custom target, which runs on every build and rewrites this file only when\n"
    "// the identity of the source tree changes (see tf2-version.cmake for why this\n"
    "// is not a configure-time define). The values describe the tree this binary\n"
    "// was compiled from, not the tree it was configured from.\n"
    "//\n"
    "//   sourceTree       git rev-parse --short=12 HEAD\n"
    "//   sourceDateEpoch  git log -1 --format=%ct HEAD (SOURCE_DATE_EPOCH convention)\n"
    "//   buildUtc         when THIS identity was first captured by a build\n"
    "//   dirty            1 = uncommitted changes when this identity was captured,\n"
    "//                    0 = a clean worktree, -1 = git could not answer\n"
    "//   dirtyFiles       git status --porcelain -uall record count (tracked+untracked)\n"
    "//   dirtyTracked     of those records, tracked modifications/deletions\n"
    "//   dirtyUntracked   of those records, untracked files (-uall: one per file)\n"
    "namespace tf2_build_identity {\n")
  string(APPEND TF2_STAMP_TEXT "extern const char kSourceTree[] = \"${TF2_TREE}\";\n")
  string(APPEND TF2_STAMP_TEXT "extern const char kSourceDateEpoch[] = \"${TF2_EPOCH}\";\n")
  string(APPEND TF2_STAMP_TEXT "extern const char kBuildUtc[] = \"${TF2_NOW_UTC}\";\n")
  string(APPEND TF2_STAMP_TEXT "extern const char kDirty[] = \"${TF2_DIRTY}\";\n")
  string(APPEND TF2_STAMP_TEXT "extern const char kDirtyFiles[] = \"${TF2_DIRTY_FILES}\";\n")
  string(APPEND TF2_STAMP_TEXT "extern const char kDirtyTracked[] = \"${TF2_DIRTY_TRACKED}\";\n")
  string(APPEND TF2_STAMP_TEXT "extern const char kDirtyUntracked[] = \"${TF2_DIRTY_UNTRACKED}\";\n")
  string(APPEND TF2_STAMP_TEXT "}  // namespace tf2_build_identity\n")

  set(TF2_STAMP_TMP "${TF2_STAMP_OUTPUT}.tmp")
  file(WRITE "${TF2_STAMP_TMP}" "${TF2_STAMP_TEXT}")
  execute_process(
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${TF2_STAMP_TMP}" "${TF2_STAMP_OUTPUT}"
    RESULT_VARIABLE TF2_COPY_RC ERROR_VARIABLE TF2_COPY_ERROR)
  file(REMOVE "${TF2_STAMP_TMP}")
  if (NOT TF2_COPY_RC EQUAL 0)
    message(FATAL_ERROR "gen-build-stamp.cmake: cannot write ${TF2_STAMP_OUTPUT}: ${TF2_COPY_ERROR}")
  endif()
  file(WRITE "${TF2_STAMP_CACHE}" "${TF2_IDENTITY}")
  message(STATUS "build identity: ${TF2_TREE} dirty=${TF2_DIRTY} (${TF2_DIRTY_FILES} records) captured at ${TF2_NOW_UTC}")
else()
  message(STATUS "build identity: ${TF2_TREE} dirty=${TF2_DIRTY} (${TF2_DIRTY_FILES} records) unchanged; stamp kept")
endif()
