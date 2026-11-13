# ---------------------------------------------------------------------------
# T75 (2026-10-28): split out of native/CMakeLists.txt (HEAD db3316e).
#
# the minimap projection / hit test / panel state.
#
# The text below is the ORIGINAL lines 794-804 of native/CMakeLists.txt, moved
# verbatim: no target name, source list, link line, compile definition, compile
# option, install rule or cache variable was edited, added or re-ordered.  The
# include() order in ../CMakeLists.txt is the original statement order, so
# target creation order -- which is the NMAKE build order, and therefore which
# targets a failed link truncates -- is unchanged as well.
#
# Targets defined here: minimap_probe
# T75-BODY-START
# ---------------------------------------------------------------------------
# T69: the minimap's PURE logic. The probe links src/minimap.cpp -- the same
# translation unit the product will link -- so a green run is a statement about
# the projection arithmetic, the world<->pixel round trip, the hit test and the
# panel state machine, and cannot be helped by a GPU, a window or a demo file.
# The DRAWING half of T69 is deliberately not covered here: it needs T67's HUD
# primitive layer and arrives with it, so this probe proves the numbers a
# renderer will consume, not the picture a user will see.
add_executable(minimap_probe tools/minimap_probe.cpp src/minimap.cpp)
target_include_directories(minimap_probe PRIVATE include)
target_compile_definitions(minimap_probe PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
set_target_properties(minimap_probe PROPERTIES WIN32_EXECUTABLE OFF)
