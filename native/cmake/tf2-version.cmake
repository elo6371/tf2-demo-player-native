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
# Targets defined here: -
# T75-BODY-START
# ---------------------------------------------------------------------------
# T37: the package version, stated once. It is a cache variable so a release can
# re-stamp without editing this file, and it is compiled into the product because
# `--version` has to answer before any file exists to read. VERSION.txt beside
# the executable can still override it at run time (see install_lifecycle.cpp),
# so a packaged build does not need a rebuild to be re-numbered. The source date
# epoch and tree are recorded because "which source produced this binary" is the
# question an upgrade actually has to answer.
set(TF2_PACKAGE_VERSION "0.1.0" CACHE STRING "Package version reported by --version")
set(TF2_PACKAGE_VERSION_CODE "100" CACHE STRING "Monotonic package version code")
string(TIMESTAMP TF2_BUILD_UTC "%Y-%m-%dT%H:%M:%SZ" UTC)
execute_process(COMMAND git rev-parse --short=12 HEAD
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  OUTPUT_VARIABLE TF2_SOURCE_TREE OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET RESULT_VARIABLE TF2_GIT_RC)
if (NOT TF2_GIT_RC EQUAL 0)
  set(TF2_SOURCE_TREE "unknown")
endif()
set(TF2_SOURCE_DATE_EPOCH "0" CACHE STRING "Source date epoch recorded in the manifest")
set(TF2_VERSION_DEFINES
  TF2_PACKAGE_VERSION="${TF2_PACKAGE_VERSION}"
  TF2_PACKAGE_VERSION_CODE=${TF2_PACKAGE_VERSION_CODE}
  TF2_BUILD_UTC="${TF2_BUILD_UTC}"
  TF2_SOURCE_DATE_EPOCH="${TF2_SOURCE_DATE_EPOCH}"
  TF2_SOURCE_TREE="${TF2_SOURCE_TREE}")
