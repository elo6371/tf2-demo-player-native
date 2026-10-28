# ---------------------------------------------------------------------------
# T75 (2026-10-28): split out of native/CMakeLists.txt (HEAD db3316e).
#
# playback control and the camera director / bookmarks / key table.
#
# The text below is the ORIGINAL lines 689-706 of native/CMakeLists.txt, moved
# verbatim: no target name, source list, link line, compile definition, compile
# option, install rule or cache variable was edited, added or re-ordered.  The
# include() order in ../CMakeLists.txt is the original statement order, so
# target creation order -- which is the NMAKE build order, and therefore which
# targets a failed link truncates -- is unchanged as well.
#
# Targets defined here: playback_control_probe camera_probe
# T75-BODY-START
# ---------------------------------------------------------------------------
# T70 playback-control probe. PlaybackControl is pure logic (no Win32, no D3D,
# no demo parser), so this target links only its translation unit. The gate
# drives it under the shared host lock like every other probe.
add_executable(playback_control_probe tools/playback_control_probe.cpp src/playback_control.cpp)
target_include_directories(playback_control_probe PRIVATE include)
set_target_properties(playback_control_probe PROPERTIES WIN32_EXECUTABLE OFF)

# T50 camera gate probe. The freeze rule, the bookmark store and the key table
# are all pure; this probe drives them without a GPU or a demo, which is what
# lets camera-ui-check.sh assert the pause/bookmark contract on every run.
add_executable(camera_probe tools/camera_probe.cpp
  src/camera_director.cpp
  src/camera_bookmarks.cpp
  src/keybinds.cpp
  src/ini_section.cpp)
target_compile_definitions(camera_probe PRIVATE ${TF2_DEMO_HEADER_DEFINES})
target_include_directories(camera_probe PRIVATE include)
set_target_properties(camera_probe PROPERTIES WIN32_EXECUTABLE OFF)
