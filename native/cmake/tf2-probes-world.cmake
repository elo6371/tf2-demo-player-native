# ---------------------------------------------------------------------------
# T75 (2026-10-28): split out of native/CMakeLists.txt (HEAD db3316e).
#
# world material / geometry / coverage / lighting / feature / terrain blend.
#
# The text below is the ORIGINAL lines 261-309 of native/CMakeLists.txt, moved
# verbatim: no target name, source list, link line, compile definition, compile
# option, install rule or cache variable was edited, added or re-ordered.  The
# include() order in ../CMakeLists.txt is the original statement order, so
# target creation order -- which is the NMAKE build order, and therefore which
# targets a failed link truncates -- is unchanged as well.
#
# Targets defined here: world_material_probe world_geometry_probe material_coverage_probe lighting_semantics_probe world_feature_probe terrain_blend_probe
# T75-BODY-START
# ---------------------------------------------------------------------------
# The world material and lighting capability layer. Shared with the product
# (the renderer publishes its readings from it) rather than inlined into
# native_renderer.cpp for the same reason viewmodel_material.cpp is shared: a
# gate that asserts the two agree is only non-circular if there is one
# implementation to agree with.
add_executable(world_material_probe tools/world_material_probe.cpp src/bsp_map.cpp src/world_material.cpp src/vmt_material.cpp src/vpk_archive.cpp src/vtf_texture.cpp third_party/lzma/LzmaDec.c)
add_executable(world_geometry_probe tools/world_geometry_probe.cpp src/bsp_map.cpp src/asset_root.cpp src/vpk_archive.cpp third_party/lzma/LzmaDec.c)
target_include_directories(world_geometry_probe PRIVATE include third_party/lzma)
set_target_properties(world_geometry_probe PROPERTIES WIN32_EXECUTABLE OFF)
target_include_directories(world_material_probe PRIVATE include third_party/lzma)
set_target_properties(world_material_probe PROPERTIES WIN32_EXECUTABLE OFF)

# T12: the terrain blend resources and the per-vertex weight. Links the same
# parser, VMT reader and VPK reader the product uses, plus world_material.cpp
# for `bspPakfileEntry` -- the third store the lookup policy has to reach.
# T19: the material-coverage probe. It drives world_tile_atlas and
# world_material_features -- the two modules the product uses -- over a synthetic
# material set and over a real map, so the gate can assert the plan and the
# feature classification without a GPU.
add_executable(material_coverage_probe tools/material_coverage_probe.cpp
  src/world_tile_atlas.cpp src/world_material_features.cpp src/vmt_material.cpp
  src/vpk_archive.cpp src/bsp_map.cpp src/vtf_texture.cpp src/world_material.cpp
  third_party/lzma/LzmaDec.c)
target_include_directories(material_coverage_probe PRIVATE include third_party/lzma)
set_target_properties(material_coverage_probe PROPERTIES WIN32_EXECUTABLE OFF)

# T24: the six lighting/shader controls. The probe links the same ledger the
# product's trace reads, so its rows and the product's rows cannot disagree
# about what "declared", "requested" and "effective" mean.
add_executable(lighting_semantics_probe tools/lighting_semantics_probe.cpp
  src/world_lighting.cpp src/world_material_features.cpp src/vmt_material.cpp
  src/vpk_archive.cpp src/bsp_map.cpp src/vtf_texture.cpp src/world_material.cpp
  third_party/lzma/LzmaDec.c)
target_include_directories(lighting_semantics_probe PRIVATE include third_party/lzma)
set_target_properties(lighting_semantics_probe PROPERTIES WIN32_EXECUTABLE OFF)

# T27: the per-material feature path (normal layers, detail layer). Links
# world_tile_atlas.cpp for `worldFeatureTileRect`, so the probe and the renderer
# scale a tile rectangle with one function.
add_executable(world_feature_probe tools/world_feature_probe.cpp
  src/world_lighting.cpp src/world_material_features.cpp src/world_tile_atlas.cpp
  src/vmt_material.cpp src/vpk_archive.cpp src/bsp_map.cpp src/vtf_texture.cpp
  src/world_material.cpp third_party/lzma/LzmaDec.c)
target_include_directories(world_feature_probe PRIVATE include third_party/lzma)
set_target_properties(world_feature_probe PROPERTIES WIN32_EXECUTABLE OFF)

add_executable(terrain_blend_probe tools/terrain_blend_probe.cpp src/bsp_map.cpp src/world_material.cpp src/vmt_material.cpp src/vpk_archive.cpp src/vtf_texture.cpp third_party/lzma/LzmaDec.c)
target_include_directories(terrain_blend_probe PRIVATE include third_party/lzma)
set_target_properties(terrain_blend_probe PROPERTIES WIN32_EXECUTABLE OFF)
