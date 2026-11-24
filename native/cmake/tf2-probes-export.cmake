# ---------------------------------------------------------------------------
# N27 (2026-11-24): the offline export tool.
#
# The product's per-tick BMP sequence (--capture-sequence, N27 step 1) is turned
# into an MP4 here, through the SAME sink the product's --export-video switch
# would use (native/src/demo_export.cpp, a tf2_core member). Keeping the encoder
# in tf2_core and the file I/O in this tool is what lets the checklist assert
# that the bytes under test came from product code.
#
# It is a console program on purpose: it prints its readings (frame count,
# timestamps, duration) to stdout, where a shell gate can grep them. Nothing
# here is installed; this is a development/verification entry point.
#
# Targets defined here: video_export
# ---------------------------------------------------------------------------
add_executable(video_export tools/video_export.cpp)
target_include_directories(video_export PRIVATE include)
target_compile_definitions(video_export PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN ${TF2_VERSION_DEFINES})
# The Media Foundation libraries are PUBLIC on tf2_core, so they arrive through
# the link line; listing them again here would be the duplication this module
# avoids. ole32 is likewise inherited.
target_link_libraries(video_export PRIVATE tf2_core)
set_target_properties(video_export PROPERTIES WIN32_EXECUTABLE OFF)
