# ---------------------------------------------------------------------------
# T75 (2026-10-28): split out of native/CMakeLists.txt (HEAD db3316e).
#
# the quiet-capture trace/metric/bitmap reader.
#
# The text below is the ORIGINAL lines 733-739 of native/CMakeLists.txt, moved
# verbatim: no target name, source list, link line, compile definition, compile
# option, install rule or cache variable was edited, added or re-ordered.  The
# include() order in ../CMakeLists.txt is the original statement order, so
# target creation order -- which is the NMAKE build order, and therefore which
# targets a failed link truncates -- is unchanged as well.
#
# Targets defined here: quiet_capture_probe
# T75-BODY-START
# ---------------------------------------------------------------------------
# The quiet-capture reader: compares one normal-mode run and one
# --hidden-window --audio-disabled run of tf2_demo_native (their traces,
# metrics and bitmaps) and reports the contract as one key=value line.
# Standard library only -- no project sources, no Windows APIs -- because it
# reads files the product already wrote.
add_executable(quiet_capture_probe tools/quiet_capture_probe.cpp)
set_target_properties(quiet_capture_probe PROPERTIES WIN32_EXECUTABLE OFF)
