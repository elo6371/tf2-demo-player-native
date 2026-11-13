# ---------------------------------------------------------------------------
# T75 (2026-10-28): split out of native/CMakeLists.txt (HEAD db3316e).
#
# presentation, map lookup order, texture quad.
#
# The text below is the ORIGINAL lines 311-327 of native/CMakeLists.txt, moved
# verbatim: no target name, source list, link line, compile definition, compile
# option, install rule or cache variable was edited, added or re-ordered.  The
# include() order in ../CMakeLists.txt is the original statement order, so
# target creation order -- which is the NMAKE build order, and therefore which
# targets a failed link truncates -- is unchanged as well.
#
# Targets defined here: presentation_probe map_lookup_probe texture_quad_probe
# T75-BODY-START
# ---------------------------------------------------------------------------
add_executable(presentation_probe tools/presentation_probe.cpp ${TF2_DEMO_HEADER_SOURCES})
target_include_directories(presentation_probe PRIVATE include third_party/lzma)
target_link_libraries(presentation_probe PRIVATE ${TF2_DEMO_HEADER_LIBS})
target_compile_definitions(presentation_probe PRIVATE ${TF2_DEMO_HEADER_DEFINES})
set_target_properties(presentation_probe PROPERTIES WIN32_EXECUTABLE OFF)

# T9 (2026-10-16): the map search order. Pure: it links only map_lookup.cpp, so
# a green run is a statement about the order and the name rules and cannot be
# helped by any installed content.
add_executable(map_lookup_probe tools/map_lookup_probe.cpp src/map_lookup.cpp)
target_include_directories(map_lookup_probe PRIVATE include)
set_target_properties(map_lookup_probe PROPERTIES WIN32_EXECUTABLE OFF)

add_executable(texture_quad_probe tools/texture_quad_probe.cpp src/vtf_texture.cpp)
target_include_directories(texture_quad_probe PRIVATE include)
target_link_libraries(texture_quad_probe PRIVATE d3d11 d3dcompiler)
set_target_properties(texture_quad_probe PROPERTIES WIN32_EXECUTABLE OFF)
