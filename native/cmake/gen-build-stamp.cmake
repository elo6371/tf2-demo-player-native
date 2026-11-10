# ---------------------------------------------------------------------------
# gen-build-stamp.cmake -- writes the tree's identity as it stands at BUILD time.
#
# TWO OUTPUTS, AND THE SPLIT IS THE POINT (D-54, 2026-11-10)
#
#   generated/tf2_build_stamp.cpp       what the SOURCES contain: one content
#                                       hash. Compiled into tf2_demo_native.exe
#                                       and install_cli_probe.exe.
#   generated/tf2_build_stamp.identity  where those sources CAME FROM: HEAD, the
#                                       dirty counts, the wall clock. Never
#                                       compiled into anything; copied beside the
#                                       executable by write-identity-sidecar.cmake.
#
# T90 moved the measurement from configure time to build time, which fixed the
# reading but not the churn: the commit hash was still a C++ constant, so a
# commit that touched no compiled input (docs/, evidence/, a rebase label) still
# changed the bytes of both executables, and every cache keyed on the executable
# assumed the code had changed. The content hash is the only identity that is
# true of the bytes: two trees with the same compiled inputs produce the same
# hash, and two trees that differ in a compiled input produce different ones.
# HEAD and the dirty state are facts about the WORKTREE, not about the code, so
# they belong in a file that can be refreshed without relinking.
#
# WHAT THE HASH COVERS (git-clean-v1)
#   The git index entries of native/src, native/include, native/third_party,
#   native/cmake and native/CMakeLists.txt -- every tracked file the two
#   executables compile or configure from. Build products (native/generated)
#   and tools (native/tools, which belong to other targets) are NOT inputs of
#   these two executables and are not hashed; including them was what made
#   release-rebuild.sh report "inputs changed, binary not rebuilt" after a build
#   had merely rewritten its own stamp.
#
#   The digest is the sha256 of this text, with paths relative to native/ and
#   lines sorted with LC_ALL=C:
#
#       <mode> <blob> <path>\n          one line per tracked file
#
#   <blob> is git's blob id for the file's CURRENT content: for a clean file it
#   is the index's id; for a modified file the generator asks git to normalise
#   the working copy (git hash-object --stdin-paths), so a dirty file is hashed
#   by content and not by its index entry. Two builds of the same bytes agree
#   whether the tree is clean or dirty. A tree that differs only in line endings
#   does NOT change the hash, which is deliberate: git's normalisation is the
#   repository's own definition of "the same source".
#
#   If git cannot answer at all (no repository, no git on PATH) the fallback is
#   raw-v1: sha256 of "<sha256-of-file> <path>" lines over the same roots. The
#   method is written into contentMethod= so no reader can mistake one method's
#   digest for the other's; the two are not comparable.
#
# WHAT THIS DOES NOT PROVIDE (stated where it is produced, not only in a doc)
#   * no compiler, linker, SDK, flag or environment hash: a different toolchain
#     from the same sources produces the same content hash;
#   * no cross-machine claim unless the checkout produced the same blob ids;
#   * no claim about files outside the roots above (the WebView2 SDK binary, the
#     VPK data, other probes' sources).
#
# Run in script mode:
#   cmake -DTF2_REPO_DIR=<native/> -DTF2_STAMP_OUTPUT=<...cpp>
#         -DTF2_PROVENANCE_OUTPUT=<...identity> -P gen-build-stamp.cmake
# ---------------------------------------------------------------------------
cmake_minimum_required(VERSION 3.24)

foreach(required IN ITEMS TF2_REPO_DIR TF2_STAMP_OUTPUT TF2_PROVENANCE_OUTPUT)
  if (NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
    message(FATAL_ERROR
      "gen-build-stamp.cmake: -D${required}=... is required (all three of "
      "TF2_REPO_DIR, TF2_STAMP_OUTPUT, TF2_PROVENANCE_OUTPUT)")
  endif()
endforeach()

# The roots are relative to TF2_REPO_DIR (native/), and they are the same list
# release-rebuild.sh and build-identity-check.sh hash on their side. Changing it
# here without changing them turns the cross-check into two hashes that disagree
# for an uninteresting reason -- build-identity-check.sh asserts that the two
# implementations agree, so that mistake fails loudly instead of silently.
set(TF2_CONTENT_ROOTS src include third_party cmake)
set(TF2_CONTENT_FILES CMakeLists.txt)

# ---------------------------------------------------------------------------
# 1. The content hash.
# ---------------------------------------------------------------------------
set(TF2_CONTENT "unknown")
set(TF2_CONTENT_METHOD "unavailable")
set(TF2_CONTENT_LINES "")

execute_process(
  COMMAND git ls-files -s -- ${TF2_CONTENT_ROOTS} ${TF2_CONTENT_FILES}
  WORKING_DIRECTORY "${TF2_REPO_DIR}"
  OUTPUT_VARIABLE TF2_INDEX
  ERROR_QUIET
  RESULT_VARIABLE TF2_INDEX_RC
  OUTPUT_STRIP_TRAILING_WHITESPACE)

if (TF2_INDEX_RC EQUAL 0)
  string(REPLACE "\r\n" "\n" TF2_INDEX "${TF2_INDEX}")
  string(REPLACE "\n" ";" TF2_INDEX_RECORDS "${TF2_INDEX}")
  set(TF2_MODES "")
  set(TF2_PATHS "")
  foreach(record IN LISTS TF2_INDEX_RECORDS)
    if (record STREQUAL "")
      continue()
    endif()
    # "<mode> <blob> <stage>\t<path>"
    string(FIND "${record}" "\t" TF2_TAB)
    if (TF2_TAB LESS 0)
      continue()
    endif()
    string(SUBSTRING "${record}" 0 ${TF2_TAB} TF2_META)
    math(EXPR TF2_PATH_FROM "${TF2_TAB}+1")
    string(SUBSTRING "${record}" ${TF2_PATH_FROM} -1 TF2_PATH)
    # A tracked file deleted in the working copy is not part of the bytes that
    # will be compiled; skipping it is what makes the hash describe the build.
    if (NOT EXISTS "${TF2_REPO_DIR}/${TF2_PATH}")
      continue()
    endif()
    string(REPLACE " " ";" TF2_META_PARTS "${TF2_META}")
    list(GET TF2_META_PARTS 0 TF2_MODE)
    list(APPEND TF2_MODES "${TF2_MODE}")
    list(APPEND TF2_PATHS "${TF2_PATH}")
  endforeach()

  list(LENGTH TF2_PATHS TF2_PATH_COUNT)
  if (TF2_PATH_COUNT GREATER 0)
    # One git call for every input (178 at the time of writing) rather than one
    # per file, and the paths go on the command line: --stdin-paths resolves
    # them against the REPOSITORY ROOT rather than against the working
    # directory, while TF2_CONTENT_ROOTS is relative to native/. hash-object
    # hashes each working-copy file exactly as "git add" would, which is what
    # makes the mode/blob form a statement about content and not about stat().
    execute_process(
      COMMAND git hash-object -- ${TF2_PATHS}
      WORKING_DIRECTORY "${TF2_REPO_DIR}"
      OUTPUT_VARIABLE TF2_BLOBS
      ERROR_QUIET
      RESULT_VARIABLE TF2_BLOB_RC
      OUTPUT_STRIP_TRAILING_WHITESPACE)
    if (TF2_BLOB_RC EQUAL 0)
      string(REPLACE "\n" ";" TF2_BLOB_LIST "${TF2_BLOBS}")
      list(LENGTH TF2_BLOB_LIST TF2_BLOB_COUNT)
      if (TF2_BLOB_COUNT EQUAL TF2_PATH_COUNT)
        math(EXPR TF2_LAST "${TF2_PATH_COUNT}-1")
        foreach(index RANGE ${TF2_LAST})
          list(GET TF2_PATHS ${index} TF2_ONE_PATH)
          list(GET TF2_MODES ${index} TF2_ONE_MODE)
          list(GET TF2_BLOB_LIST ${index} TF2_ONE_BLOB)
          list(APPEND TF2_CONTENT_LINES "${TF2_ONE_MODE} ${TF2_ONE_BLOB} ${TF2_ONE_PATH}")
        endforeach()
        set(TF2_CONTENT_METHOD "git-clean-v1")
      endif()
    endif()
  endif()
endif()

if (TF2_CONTENT_METHOD STREQUAL "unavailable")
  # raw-v1: no git, no index. Still a content hash of the same roots, but of the
  # bytes as they lie on disk, so line-ending normalisation does not apply and
  # the digest is NOT comparable with git-clean-v1.
  foreach(root IN LISTS TF2_CONTENT_ROOTS)
    if (IS_DIRECTORY "${TF2_REPO_DIR}/${root}")
      file(GLOB_RECURSE TF2_FOUND RELATIVE "${TF2_REPO_DIR}" "${TF2_REPO_DIR}/${root}/*")
    elseif (EXISTS "${TF2_REPO_DIR}/${root}")
      set(TF2_FOUND "${root}")
    else()
      set(TF2_FOUND "")
    endif()
    foreach(found IN LISTS TF2_FOUND)
      if (IS_DIRECTORY "${TF2_REPO_DIR}/${found}")
        continue()
      endif()
      file(SHA256 "${TF2_REPO_DIR}/${found}" TF2_FILE_SHA)
      list(APPEND TF2_CONTENT_LINES "${TF2_FILE_SHA} ${found}")
    endforeach()
  endforeach()
  if (TF2_CONTENT_LINES)
    set(TF2_CONTENT_METHOD "raw-v1")
  endif()
endif()

if (TF2_CONTENT_LINES)
  list(SORT TF2_CONTENT_LINES)
  string(REPLACE ";" "\n" TF2_CONTENT_TEXT "${TF2_CONTENT_LINES}")
  string(APPEND TF2_CONTENT_TEXT "\n")
  string(SHA256 TF2_CONTENT "${TF2_CONTENT_TEXT}")
endif()
if (TF2_CONTENT_METHOD STREQUAL "unavailable")
  # No hash at all: report "unknown" rather than a digest of nothing. A reader
  # that compares this to anything will disagree, which is the honest outcome.
  set(TF2_CONTENT "unknown")
endif()

# ---------------------------------------------------------------------------
# 2. The provenance: HEAD, the dirty counts, the clock.
# ---------------------------------------------------------------------------
set(TF2_SOURCE_TREE "unknown")
set(TF2_SOURCE_EPOCH "0")
set(TF2_DIRTY "-1")
set(TF2_DIRTY_FILES "-1")
set(TF2_DIRTY_TRACKED "-1")
set(TF2_DIRTY_UNTRACKED "-1")

execute_process(
  COMMAND git rev-parse --short=12 HEAD
  WORKING_DIRECTORY "${TF2_REPO_DIR}"
  OUTPUT_VARIABLE TF2_HEAD_OUT
  ERROR_QUIET
  RESULT_VARIABLE TF2_HEAD_RC
  OUTPUT_STRIP_TRAILING_WHITESPACE)
if (TF2_HEAD_RC EQUAL 0 AND TF2_HEAD_OUT MATCHES "^[0-9a-f]+$")
  set(TF2_SOURCE_TREE "${TF2_HEAD_OUT}")
endif()

execute_process(
  COMMAND git show -s --format=%ct HEAD
  WORKING_DIRECTORY "${TF2_REPO_DIR}"
  OUTPUT_VARIABLE TF2_EPOCH_OUT
  ERROR_QUIET
  RESULT_VARIABLE TF2_EPOCH_RC
  OUTPUT_STRIP_TRAILING_WHITESPACE)
if (TF2_EPOCH_RC EQUAL 0 AND TF2_EPOCH_OUT MATCHES "^[0-9]+$")
  set(TF2_SOURCE_EPOCH "${TF2_EPOCH_OUT}")
endif()

# The dirty counts keep T90's exact reading (the whole worktree, not just the
# hashed roots): --version has always meant "this binary came from a tree in
# this state", and narrowing it to the inputs would change the claim silently.
execute_process(
  COMMAND git status --porcelain -uall
  WORKING_DIRECTORY "${TF2_REPO_DIR}"
  OUTPUT_VARIABLE TF2_STATUS
  ERROR_QUIET
  RESULT_VARIABLE TF2_STATUS_RC
  OUTPUT_STRIP_TRAILING_WHITESPACE)
if (NOT TF2_STATUS_RC EQUAL 0)
  # unmeasurable is never reported as clean
  set(TF2_DIRTY "-1")
elseif (TF2_STATUS STREQUAL "")
  set(TF2_DIRTY "0")
  set(TF2_DIRTY_FILES "0")
  set(TF2_DIRTY_TRACKED "0")
  set(TF2_DIRTY_UNTRACKED "0")
else()
  string(REPLACE "\r\n" "\n" TF2_STATUS "${TF2_STATUS}")
  string(REPLACE "\n" ";" TF2_STATUS_RECORDS "${TF2_STATUS}")
  set(TF2_TRACKED 0)
  set(TF2_UNTRACKED 0)
  foreach(record IN LISTS TF2_STATUS_RECORDS)
    if (record STREQUAL "")
      continue()
    endif()
    string(SUBSTRING "${record}" 0 2 TF2_XY)
    if (TF2_XY STREQUAL "??")
      math(EXPR TF2_UNTRACKED "${TF2_UNTRACKED}+1")
    else()
      math(EXPR TF2_TRACKED "${TF2_TRACKED}+1")
    endif()
  endforeach()
  math(EXPR TF2_STATUS_TOTAL "${TF2_TRACKED}+${TF2_UNTRACKED}")
  set(TF2_DIRTY "1")
  set(TF2_DIRTY_FILES "${TF2_STATUS_TOTAL}")
  set(TF2_DIRTY_TRACKED "${TF2_TRACKED}")
  set(TF2_DIRTY_UNTRACKED "${TF2_UNTRACKED}")
endif()

# buildUtc is the only field that changes on a build that changed nothing. It
# means "when this identity was first captured", so it is carried forward while
# every other field is unchanged; the tag below is what "unchanged" is computed
# from, rather than a line-by-line diff of the file.
set(TF2_VALUES
"contentMethod=${TF2_CONTENT_METHOD}
sourceContent=${TF2_CONTENT}
sourceTree=${TF2_SOURCE_TREE}
sourceDateEpoch=${TF2_SOURCE_EPOCH}
dirty=${TF2_DIRTY}
dirtyFiles=${TF2_DIRTY_FILES}
dirtyTracked=${TF2_DIRTY_TRACKED}
dirtyUntracked=${TF2_DIRTY_UNTRACKED}")
string(SHA256 TF2_TAG "${TF2_VALUES}")

set(TF2_PREVIOUS_TAG "")
set(TF2_PREVIOUS_UTC "")
if (EXISTS "${TF2_PROVENANCE_OUTPUT}")
  file(STRINGS "${TF2_PROVENANCE_OUTPUT}" TF2_PREVIOUS_LINES)
  foreach(line IN LISTS TF2_PREVIOUS_LINES)
    if (line MATCHES "^tag=(.+)$")
      set(TF2_PREVIOUS_TAG "${CMAKE_MATCH_1}")
    elseif (line MATCHES "^buildUtc=(.+)$")
      set(TF2_PREVIOUS_UTC "${CMAKE_MATCH_1}")
    endif()
  endforeach()
endif()

if (TF2_PREVIOUS_TAG STREQUAL TF2_TAG AND NOT TF2_PREVIOUS_UTC STREQUAL "")
  set(TF2_BUILD_UTC "${TF2_PREVIOUS_UTC}")
else()
  string(TIMESTAMP TF2_BUILD_UTC "%Y-%m-%dT%H:%M:%SZ" UTC)
endif()

# ---------------------------------------------------------------------------
# 3. Write both files, and only when their content changed.
# ---------------------------------------------------------------------------
function(tf2_write_if_changed path content)
  set(old "")
  if (EXISTS "${path}")
    file(READ "${path}" old)
  endif()
  if (NOT old STREQUAL content)
    file(WRITE "${path}.tmp" "${content}")
    file(COPY_FILE "${path}.tmp" "${path}" ONLY_IF_DIFFERENT)
    file(REMOVE "${path}.tmp")
    set(TF2_REWRITTEN TRUE PARENT_SCOPE)
  endif()
endfunction()

set(TF2_PROVENANCE_TEXT
"format=tf2-build-identity
formatVersion=2
tag=${TF2_TAG}
contentMethod=${TF2_CONTENT_METHOD}
sourceContent=${TF2_CONTENT}
sourceTree=${TF2_SOURCE_TREE}
sourceDateEpoch=${TF2_SOURCE_EPOCH}
dirty=${TF2_DIRTY}
dirtyFiles=${TF2_DIRTY_FILES}
dirtyTracked=${TF2_DIRTY_TRACKED}
dirtyUntracked=${TF2_DIRTY_UNTRACKED}
buildUtc=${TF2_BUILD_UTC}
")

set(TF2_STAMP_TEXT
"// GENERATED at BUILD time by native/cmake/gen-build-stamp.cmake -- do not edit.
// It is a translation unit, not a header, because this tree's NMAKE build has
// no header dependencies at all (build-target.sh explains the empty .obj.d
// files); a rewritten header would leave install_lifecycle.obj stale and the
// binary would keep the previous identity.
//
// D-54: this file carries the CONTENT hash only. HEAD, the dirty counts and the
// build time are provenance, not code: they live in the .identity file beside
// the executable, and --version reads them from there. That is what keeps a
// commit which touches no compiled input from changing these bytes.
namespace tf2_build_identity {
extern const char kSourceContent[] = \"${TF2_CONTENT}\";
extern const char kContentMethod[] = \"${TF2_CONTENT_METHOD}\";
}  // namespace tf2_build_identity
")

set(TF2_REWRITTEN FALSE)
tf2_write_if_changed("${TF2_STAMP_OUTPUT}" "${TF2_STAMP_TEXT}")
set(TF2_STAMP_STATE "unchanged")
if (TF2_REWRITTEN)
  set(TF2_STAMP_STATE "rewritten")
endif()

# The provenance file is written on every build -- including the builds where
# nothing else happens. It is the only recorded answer to "which tree produced
# the binary in this directory right now", and a build that skipped it would
# leave --version describing the previous tree.
file(WRITE "${TF2_PROVENANCE_OUTPUT}.tmp" "${TF2_PROVENANCE_TEXT}")
file(COPY_FILE "${TF2_PROVENANCE_OUTPUT}.tmp" "${TF2_PROVENANCE_OUTPUT}" ONLY_IF_DIFFERENT)
file(REMOVE "${TF2_PROVENANCE_OUTPUT}.tmp")

message(STATUS
  "build identity: content=${TF2_CONTENT} method=${TF2_CONTENT_METHOD} "
  "tree=${TF2_SOURCE_TREE} epoch=${TF2_SOURCE_EPOCH} dirty=${TF2_DIRTY}/${TF2_DIRTY_FILES} "
  "stamp=${TF2_STAMP_STATE}")