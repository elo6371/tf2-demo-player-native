# ---------------------------------------------------------------------------
# T75 (2026-10-28): split out of native/CMakeLists.txt (HEAD db3316e).
#
# PCF capability, material/resource chains through the archive layer, VPK query and integrity.
#
# The text below is the ORIGINAL lines 237-259 of native/CMakeLists.txt, moved
# verbatim: no target name, source list, link line, compile definition, compile
# option, install rule or cache variable was edited, added or re-ordered.  The
# include() order in ../CMakeLists.txt is the original statement order, so
# target creation order -- which is the NMAKE build order, and therefore which
# targets a failed link truncates -- is unchanged as well.
#
# Targets defined here: pcf_capability_probe material_chain_probe resource_reachability_probe vpk_query vpk_integrity_probe
# T75-BODY-START
# ---------------------------------------------------------------------------
add_executable(pcf_capability_probe tools/pcf_capability_probe.cpp src/vpk_archive.cpp)
target_include_directories(pcf_capability_probe PRIVATE include third_party/lzma)
set_target_properties(pcf_capability_probe PROPERTIES WIN32_EXECUTABLE OFF)

add_executable(material_chain_probe tools/material_chain_probe.cpp src/vtf_texture.cpp src/bsp_map.cpp src/vmt_material.cpp src/vpk_archive.cpp third_party/lzma/LzmaDec.c)
target_include_directories(material_chain_probe PRIVATE include third_party/lzma)
set_target_properties(material_chain_probe PROPERTIES WIN32_EXECUTABLE OFF)

add_executable(resource_reachability_probe tools/resource_reachability_probe.cpp src/vtf_texture.cpp src/bsp_map.cpp src/vmt_material.cpp src/vpk_archive.cpp third_party/lzma/LzmaDec.c)
target_include_directories(resource_reachability_probe PRIVATE include third_party/lzma)
set_target_properties(resource_reachability_probe PROPERTIES WIN32_EXECUTABLE OFF)

add_executable(vpk_query tools/vpk_query.cpp src/vpk_archive.cpp third_party/lzma/LzmaDec.c)
target_include_directories(vpk_query PRIVATE include third_party/lzma)
set_target_properties(vpk_query PROPERTIES WIN32_EXECUTABLE OFF)

# T15: malformed-archive rejection and store precedence. vpk_query above answers
# questions about the real TF2 install; this one builds its own archives from
# bytes the test controls, because "a corrupt VPK is refused" cannot be shown
# against a shipped archive that is not corrupt.
add_executable(vpk_integrity_probe tools/vpk_integrity_probe.cpp src/vpk_archive.cpp src/asset_lookup.cpp)
target_include_directories(vpk_integrity_probe PRIVATE include third_party/lzma)
set_target_properties(vpk_integrity_probe PROPERTIES WIN32_EXECUTABLE OFF)
