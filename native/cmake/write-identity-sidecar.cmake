# ---------------------------------------------------------------------------
# write-identity-sidecar.cmake -- copies the build's provenance beside the
# executable it describes (D-54, 2026-11-10).
#
# WHY THIS IS A SEPARATE FILE AND A SEPARATE STEP
#   tf2_demo_native.exe no longer compiles HEAD or the dirty counts, so
#   --version has to read them from somewhere. That somewhere is
#   <build>/<target>.identity, written next to the executable: the same file the
#   package installs into .uninstall/payload, so an installed copy answers the
#   same questions as the build tree does (install_lifecycle.cpp looks for it by
#   replacing the executable's extension).
#
# WHAT MAKES IT TRUSTWORTHY
#   * exeSha256 is file(SHA256) of the executable as it lies on disk, taken here
#     and not by the build system, so "this sidecar describes these bytes" is a
#     statement about the file rather than about the build graph;
#   * sourceContent is taken from the generated translation unit when the bytes
#     just changed, and carried over from the previous sidecar when they did
#     not. That makes the pair (exe bytes -> content hash) the carrier of the
#     binding: a rebuild that produced identical bytes must not claim a
#     different content, even if the stamp file was regenerated in between.
#
# WHEN IT RUNS
#   Twice, deliberately:
#   1. as POST_BUILD of each identity-bearing target -- so a relink always
#      refreshes its own sidecar;
#   2. from the tf2_identity target, which is ALL -- so a build that only
#      updates the provenance (a new commit, a dirty file appearing) and does
#      not relink still updates the readings. Without step 2 a docs-only commit
#      would leave --version naming the previous commit, which is the T90 defect
#      in its other form.
#
# Run in script mode:
#   cmake -DTF2_PROVENANCE=<...generated/tf2_build_stamp.identity>
#         -DTF2_STAMP_SOURCE=<...generated/tf2_build_stamp.cpp>
#         -DTF2_EXE=<path to the executable> -DTF2_TARGET=<target name>
#         -DTF2_OUTPUT=<path to write> -P write-identity-sidecar.cmake
# ---------------------------------------------------------------------------
cmake_minimum_required(VERSION 3.24)

foreach(required IN ITEMS TF2_PROVENANCE TF2_STAMP_SOURCE TF2_EXE TF2_TARGET TF2_OUTPUT)
  if (NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
    message(FATAL_ERROR "write-identity-sidecar.cmake: -D${required}=... is required")
  endif()
endforeach()

if (NOT EXISTS "${TF2_EXE}")
  # A build that does not touch this target has nothing to describe. Say so
  # rather than writing an empty sidecar; the gate fails closed on a missing
  # file, but an empty one would be a reading.
  message(STATUS "build identity sidecar: ${TF2_TARGET} not built; nothing written")
  return()
endif()
if (NOT EXISTS "${TF2_PROVENANCE}")
  message(FATAL_ERROR
    "write-identity-sidecar.cmake: ${TF2_PROVENANCE} does not exist; the "
    "tf2_build_stamp target must run before any sidecar can be written")
endif()

file(SHA256 "${TF2_EXE}" TF2_EXE_SHA)
file(SIZE "${TF2_EXE}" TF2_EXE_BYTES)

# The compiled content hash: the value this executable will print on line 4 of
# --version. Read out of the generated source rather than recomputed, because
# the question is "what is in these bytes", and that translation unit is what
# went into them.
file(READ "${TF2_STAMP_SOURCE}" TF2_STAMP_TEXT)
set(TF2_COMPILED_CONTENT "unknown")
set(TF2_COMPILED_METHOD "none")
if (TF2_STAMP_TEXT MATCHES "kSourceContent\\[\\] = \"([^\"]*)\"")
  set(TF2_COMPILED_CONTENT "${CMAKE_MATCH_1}")
endif()
if (TF2_STAMP_TEXT MATCHES "kContentMethod\\[\\] = \"([^\"]*)\"")
  set(TF2_COMPILED_METHOD "${CMAKE_MATCH_1}")
endif()

# Provenance: every key=value line the generator wrote, by name, so the sidecar
# has a fixed key order even if the generator's order changes.
file(STRINGS "${TF2_PROVENANCE}" TF2_PROVENANCE_LINES)
function(tf2_provenance_value key out)
  set(found "")
  foreach(line IN LISTS TF2_PROVENANCE_LINES)
    if (line MATCHES "^${key}=(.*)$")
      set(found "${CMAKE_MATCH_1}")
    endif()
  endforeach()
  set(${out} "${found}" PARENT_SCOPE)
endfunction()
tf2_provenance_value(contentMethod TF2_CONTENT_METHOD)
tf2_provenance_value(sourceContent TF2_PROVENANCE_CONTENT)
tf2_provenance_value(sourceTree TF2_SOURCE_TREE)
tf2_provenance_value(sourceDateEpoch TF2_SOURCE_EPOCH)
tf2_provenance_value(dirty TF2_DIRTY)
tf2_provenance_value(dirtyFiles TF2_DIRTY_FILES)
tf2_provenance_value(dirtyTracked TF2_DIRTY_TRACKED)
tf2_provenance_value(dirtyUntracked TF2_DIRTY_UNTRACKED)
tf2_provenance_value(buildUtc TF2_BUILD_UTC)

# The content this sidecar reports: pinned to the executable bytes.
set(TF2_CONTENT "${TF2_COMPILED_CONTENT}")
set(TF2_CONTENT_METHOD "${TF2_COMPILED_METHOD}")
set(TF2_CONTENT_BINDING "recompiled")
set(TF2_PREVIOUS_EXE_SHA "")
set(TF2_PREVIOUS_CONTENT "")
set(TF2_PREVIOUS_METHOD "")
if (EXISTS "${TF2_OUTPUT}")
  file(STRINGS "${TF2_OUTPUT}" TF2_PREVIOUS_LINES)
  foreach(line IN LISTS TF2_PREVIOUS_LINES)
    if (line MATCHES "^exeSha256=(.*)$")
      set(TF2_PREVIOUS_EXE_SHA "${CMAKE_MATCH_1}")
    elseif (line MATCHES "^sourceContent=(.*)$")
      set(TF2_PREVIOUS_CONTENT "${CMAKE_MATCH_1}")
    elseif (line MATCHES "^contentMethod=(.*)$")
      set(TF2_PREVIOUS_METHOD "${CMAKE_MATCH_1}")
    endif()
  endforeach()
endif()
if (TF2_PREVIOUS_EXE_SHA STREQUAL TF2_EXE_SHA AND NOT TF2_PREVIOUS_CONTENT STREQUAL "")
  # Same bytes as the sidecar already described: keep the content hash that was
  # true of them. A regenerated stamp that would otherwise claim a different
  # content for identical code is exactly the bug this rule prevents.
  set(TF2_CONTENT "${TF2_PREVIOUS_CONTENT}")
  set(TF2_CONTENT_METHOD "${TF2_PREVIOUS_METHOD}")
  set(TF2_CONTENT_BINDING "carried")
endif()

set(TF2_SIDECAR_TEXT
"format=tf2-build-identity-sidecar
formatVersion=1
target=${TF2_TARGET}
exeSha256=${TF2_EXE_SHA}
exeBytes=${TF2_EXE_BYTES}
contentBinding=${TF2_CONTENT_BINDING}
sourceContent=${TF2_CONTENT}
contentMethod=${TF2_CONTENT_METHOD}
sourceTree=${TF2_SOURCE_TREE}
sourceDateEpoch=${TF2_SOURCE_EPOCH}
dirty=${TF2_DIRTY}
dirtyFiles=${TF2_DIRTY_FILES}
dirtyTracked=${TF2_DIRTY_TRACKED}
dirtyUntracked=${TF2_DIRTY_UNTRACKED}
buildUtc=${TF2_BUILD_UTC}
")

set(TF2_OLD "")
if (EXISTS "${TF2_OUTPUT}")
  file(READ "${TF2_OUTPUT}" TF2_OLD)
endif()
if (NOT TF2_OLD STREQUAL TF2_SIDECAR_TEXT)
  file(WRITE "${TF2_OUTPUT}.tmp" "${TF2_SIDECAR_TEXT}")
  file(COPY_FILE "${TF2_OUTPUT}.tmp" "${TF2_OUTPUT}" ONLY_IF_DIFFERENT)
  file(REMOVE "${TF2_OUTPUT}.tmp")
  message(STATUS
    "build identity sidecar: ${TF2_TARGET} -> ${TF2_OUTPUT} "
    "(content=${TF2_CONTENT} binding=${TF2_CONTENT_BINDING})")
else()
  message(STATUS "build identity sidecar: ${TF2_TARGET} unchanged")
endif()