# ---------------------------------------------------------------------------
# T75 (2026-10-28): split out of native/CMakeLists.txt (HEAD db3316e).
#
# the custom HUD import contract.
#
# The text below is the ORIGINAL lines 762-770 of native/CMakeLists.txt, moved
# verbatim: no target name, source list, link line, compile definition, compile
# option, install rule or cache variable was edited, added or re-ordered.  The
# include() order in ../CMakeLists.txt is the original statement order, so
# target creation order -- which is the NMAKE build order, and therefore which
# targets a failed link truncates -- is unchanged as well.
#
# Targets defined here: hud_probe
# T75-BODY-START
# ---------------------------------------------------------------------------
# T51: the HUD import contract. Pure: it links only hud_import.cpp, so a green
# run is a statement about path safety, the KeyValue subset, the coordinate
# grammar and the rectangle arithmetic, and cannot be helped by any installed
# content or any GPU. The product links the same source file, so the probe and
# the product cannot disagree about what a document means.
add_executable(hud_probe tools/hud_probe.cpp src/hud_import.cpp)
target_include_directories(hud_probe PRIVATE include)
target_compile_definitions(hud_probe PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
set_target_properties(hud_probe PROPERTIES WIN32_EXECUTABLE OFF)
