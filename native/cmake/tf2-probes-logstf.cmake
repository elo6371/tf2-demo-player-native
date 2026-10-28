# ---------------------------------------------------------------------------
# T75 (2026-10-28): split out of native/CMakeLists.txt (HEAD db3316e).
#
# the logs.tf panel contract probe.
#
# The text below is the ORIGINAL lines 192-203 of native/CMakeLists.txt, moved
# verbatim: no target name, source list, link line, compile definition, compile
# option, install rule or cache variable was edited, added or re-ordered.  The
# include() order in ../CMakeLists.txt is the original statement order, so
# target creation order -- which is the NMAKE build order, and therefore which
# targets a failed link truncates -- is unchanged as well.
#
# Targets defined here: logstf_viewer_probe
# T75-BODY-START
# ---------------------------------------------------------------------------
# T54: the logs panel contract. Links logstf_viewer.cpp (the product's own URL
# policy, geometry, state machine, runtime probe and frame-time histogram) and
# NOT logstf_webview.cpp: the probe drives the controller through a recording
# fake host, so it needs neither the WebView2 SDK nor a browser process. A green
# run is therefore a statement about the rules the product applies; the
# pixel-level question ("does a page appear in the pane") is answered by the
# product run in logstf-check.sh, not inferred here.
add_executable(logstf_viewer_probe tools/logstf_viewer_probe.cpp src/logstf_viewer.cpp)
target_include_directories(logstf_viewer_probe PRIVATE include)
target_compile_definitions(logstf_viewer_probe PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(logstf_viewer_probe PRIVATE advapi32 shell32)
set_target_properties(logstf_viewer_probe PROPERTIES WIN32_EXECUTABLE OFF)
