# ---------------------------------------------------------------------------
# T75 (2026-10-28): split out of native/CMakeLists.txt (HEAD db3316e).
#
# package identity: version, build stamp, source tree, TF2_VERSION_DEFINES.
#
# The text below is the ORIGINAL lines 156-179 of native/CMakeLists.txt, moved
# verbatim: no target name, source list, link line, compile definition, compile
# option, install rule or cache variable was edited, added or re-ordered.  The
# include() order in ../CMakeLists.txt is the original statement order, so
# target creation order -- which is the NMAKE build order, and therefore which
# targets a failed link truncates -- is unchanged as well.
#
# Targets defined here: tf2_build_stamp (custom, no output file of its own)
# T75-BODY-START
# ---------------------------------------------------------------------------
# T37: the package version, stated once. It is a cache variable so a release can
# re-stamp without editing this file, and it is compiled into the product because
# `--version` has to answer before any file exists to read. VERSION.txt beside
# the executable can still override it at run time (see install_lifecycle.cpp),
# so a packaged build does not need a rebuild to be re-numbered.
set(TF2_PACKAGE_VERSION "0.1.0" CACHE STRING "Package version reported by --version")
set(TF2_PACKAGE_VERSION_CODE "100" CACHE STRING "Monotonic package version code")

# T90 (2026-11-07): THE BUILD IDENTITY IS MEASURED AT BUILD TIME, NOT HERE.
#
# What used to be here: `string(TIMESTAMP TF2_BUILD_UTC ...)`, an
# execute_process(git rev-parse HEAD) and `set(TF2_SOURCE_DATE_EPOCH "0")`,
# all baked into TF2_VERSION_DEFINES. Configure runs once; `cmake --build`
# never runs it again. So every executable linked from a cached build tree --
# which is every incremental build, every gate that calls build-target.sh, and
# every step of verify-all -- carried the commit and the wall clock of the LAST
# CONFIGURE while its objects were compiled from newer sources.
#
# Measured on the frozen candidate (t79-integration, the tree T90 branched
# from): native/build-nmake/tf2_demo_native.exe, sha256 a3c93d57..., mtime
# 2026-11-06 20:20:35, answered
#     buildUtc=2026-11-02T16:22:30Z sourceTree=12042361ffd1
# `git rev-list --count 12042361ffd1..e15577b` = 41, so the binary claimed a
# source tree 41 commits behind the one it was built from, and a build time
# four days older than its own file mtime. The worktree was dirty at that
# mtime and nothing in the output could say so. sourceDateEpoch answered 0 on
# every build ever made: the line was a hard-coded "0" in a cache variable's
# clothes, so "which source produced this binary" -- the question the comment
# above says the field exists to answer -- was answered with a constant.
#
# Why configure-time evaluation is wrong, in one sentence: the inputs (HEAD,
# the worktree's dirty state, the clock) change between configures, and the
# only event that can observe them for a given link is the link's own build
# step. Nothing about the tree is knowable "once"; it is knowable "now".
#
# So the measurement moved into the build: gen-build-stamp.cmake writes
# generated/tf2_build_stamp.cpp as the first build step of every build
# (tf2_build_stamp is ALL and both targets that compile install_lifecycle.cpp
# depend on it), and the C++ reads constants compiled from that file. The
# generator rewrites the file ONLY when the identity changes, so a no-op build
# neither recompiles nor relinks, and buildUtc means "when this identity was
# first captured".
#
# What this does NOT provide: a content hash (the tree is not hashed, only
# HEAD and the dirtiness counts), a reproducible build (compiler/paths/times
# still vary), or a guarantee that a dirty tree is dirty in the same way --
# see docs/BUILD-IDENTITY.md in the evidence for the full boundary.
set(TF2_BUILD_STAMP_OUTPUT "${CMAKE_CURRENT_SOURCE_DIR}/generated/tf2_build_stamp.cpp")
set(TF2_BUILD_STAMP_CACHE "${CMAKE_CURRENT_SOURCE_DIR}/generated/tf2_build_stamp.identity")
# The same file, named for the role it plays in the two add_executable() source
# lists: add_executable() requires its sources to exist at configure time.
set(TF2_BUILD_STAMP_SOURCE "${TF2_BUILD_STAMP_OUTPUT}")

# The file has to exist before add_executable() looks at it. On a fresh clone it
# does not, so configure writes a placeholder that is impossible to mistake for
# a reading (sourceTree=configure-placeholder, dirty=-1 = "not measured"). The
# tf2_build_stamp target below rewrites it before anything can compile it; if
# that ever stopped happening, the word "configure-placeholder" would appear in
# `--version` and build-identity-check.sh fails on it, rather than a stale
# commit being reported as if it were current.
file(MAKE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/generated")
if (NOT EXISTS "${TF2_BUILD_STAMP_OUTPUT}")
  file(WRITE "${TF2_BUILD_STAMP_OUTPUT}"
    "// PLACEHOLDER written by configure: the real stamp is generated at build time.\n"
    "// If these values ever reach a binary, tf2_build_stamp did not run first.\n"
    "namespace tf2_build_identity {\n"
    "extern const char kSourceTree[] = \"configure-placeholder\";\n"
    "extern const char kSourceDateEpoch[] = \"0\";\n"
    "extern const char kBuildUtc[] = \"1970-01-01T00:00:00Z\";\n"
    "extern const char kDirty[] = \"-1\";\n"
    "extern const char kDirtyFiles[] = \"-1\";\n"
    "extern const char kDirtyTracked[] = \"-1\";\n"
    "extern const char kDirtyUntracked[] = \"-1\";\n"
    "}  // namespace tf2_build_identity\n")
endif()

# Runs on every build (ALL), before the targets that compile the stamp. The
# work is a handful of git calls plus a copy-if-different, and it is the only
# step whose answer is allowed to depend on the tree's current state.
add_custom_target(tf2_build_stamp ALL
  COMMAND "${CMAKE_COMMAND}"
    "-DTF2_REPO_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
    "-DTF2_STAMP_OUTPUT=${TF2_BUILD_STAMP_OUTPUT}"
    "-DTF2_STAMP_CACHE=${TF2_BUILD_STAMP_CACHE}"
    -P "${CMAKE_CURRENT_LIST_DIR}/gen-build-stamp.cmake"
  COMMENT "build identity stamp: git HEAD + dirty state -> generated/tf2_build_stamp.cpp"
  VERBATIM)

# Only the package version stays a compile definition. The three identity
# fields are declared by the generated translation unit instead; their names
# still start with TF2_ in the C++ (see install_lifecycle.cpp) so the origin of
# each value stays obvious at the read site.
set(TF2_VERSION_DEFINES
  TF2_PACKAGE_VERSION="${TF2_PACKAGE_VERSION}"
  TF2_PACKAGE_VERSION_CODE=${TF2_PACKAGE_VERSION_CODE})
