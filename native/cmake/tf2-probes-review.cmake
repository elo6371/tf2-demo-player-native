# ---------------------------------------------------------------------------
# T75 (2026-10-28): split out of native/CMakeLists.txt (HEAD db3316e).
#
# the review workspace store and bookmarks.
#
# The text below is the ORIGINAL lines 806-816 of native/CMakeLists.txt, moved
# verbatim: no target name, source list, link line, compile definition, compile
# option, install rule or cache variable was edited, added or re-ordered.  The
# include() order in ../CMakeLists.txt is the original statement order, so
# target creation order -- which is the NMAKE build order, and therefore which
# targets a failed link truncates -- is unchanged as well.
#
# Targets defined here: review_workspace_probe
# T75-BODY-START
# ---------------------------------------------------------------------------
# T71/T72: review workspace and bookmarks, linked against the same translation
# units the product uses. The probe exercises the real store (atomic write,
# hash binding, migration) and the real bookmark collection (eventKey, sort,
# jump) in a scratch directory, so a green run is a statement about the bytes
# that will be written on a user's disk.
add_executable(review_workspace_probe tools/review_workspace_probe.cpp
  src/review_workspace.cpp src/review_bookmarks.cpp src/install_manifest.cpp)
target_include_directories(review_workspace_probe PRIVATE include)
target_compile_definitions(review_workspace_probe PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN ${TF2_VERSION_DEFINES})
target_link_libraries(review_workspace_probe PRIVATE bcrypt)
set_target_properties(review_workspace_probe PROPERTIES WIN32_EXECUTABLE OFF)
