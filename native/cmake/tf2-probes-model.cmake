# ---------------------------------------------------------------------------
# T75 (2026-10-28): split out of native/CMakeLists.txt (HEAD db3316e).
#
# model pose, viewmodel range, model robustness, entity model.
#
# The text below is the ORIGINAL lines 329-390 of native/CMakeLists.txt, moved
# verbatim: no target name, source list, link line, compile definition, compile
# option, install rule or cache variable was edited, added or re-ordered.  The
# include() order in ../CMakeLists.txt is the original statement order, so
# target creation order -- which is the NMAKE build order, and therefore which
# targets a failed link truncates -- is unchanged as well.
#
# Targets defined here: model_pose_probe viewmodel_range_probe model_robustness_probe entity_model_probe
# T75-BODY-START
# ---------------------------------------------------------------------------
# T61: the pose probe also answers the player-animation questions -- which
# sequence table a player model carries, what a cycle looks like, whether a
# pose is continuous across ticks, and what the clock does at rest and at run
# speed. It links the animation binding, the overlay sampler and the entity
# transform resolver because the readings have to come from the same objects
# the product uses: the binding maps the animation skeleton onto the render
# skeleton, the overlay sampler is the only per-tick player reader, and
# EntityModelResolver owns the origin the speed is measured from.
add_executable(model_pose_probe
  tools/model_pose_probe.cpp
  src/model_loader.cpp
  src/asset_root.cpp
  src/animation_binding.cpp
  src/animation_clock.cpp
  src/animation_decoder.cpp
  src/entity_overlay.cpp
  src/entity_model.cpp
  ${TF2_DEMO_HEADER_SOURCES}
  src/item_schema.cpp)
target_link_libraries(model_pose_probe PRIVATE ${TF2_DEMO_HEADER_LIBS})
target_compile_definitions(model_pose_probe PRIVATE ${TF2_DEMO_HEADER_DEFINES})
target_include_directories(model_pose_probe PRIVATE include third_party/lzma)
set_target_properties(model_pose_probe PROPERTIES WIN32_EXECUTABLE OFF)

# T13: prints the descriptor table and the built range table for one model, so
# "only one 600-vertex range" can be attributed to a cause instead of inferred
# from the pass's own vertex count. Links only the loader: the readings are the
# parser's, and a probe that drew would need a device the gate does not have.
add_executable(viewmodel_range_probe tools/viewmodel_range_probe.cpp src/model_loader.cpp src/asset_root.cpp ${TF2_DEMO_HEADER_SOURCES} src/item_schema.cpp)
target_link_libraries(viewmodel_range_probe PRIVATE ${TF2_DEMO_HEADER_LIBS})
target_compile_definitions(viewmodel_range_probe PRIVATE ${TF2_DEMO_HEADER_DEFINES})
target_include_directories(viewmodel_range_probe PRIVATE include)
set_target_properties(viewmodel_range_probe PROPERTIES WIN32_EXECUTABLE OFF)

# T30: one deterministic line per real model -- selected LOD and ranges, the VTF
# each range binds, the animation source the binding resolves to, and the named
# refusals -- plus a materialisation mode that writes a real model's own bytes
# (MDL/VVD/VTX and the declared *_animations.mdl) into a loose fixture root so a
# gate can make a REAL model's companion set incomplete. Links the loader, the
# animation binding and the same material reader the draw pass uses; it does not
# link the renderer and does not draw.
add_executable(model_robustness_probe
  tools/model_robustness_probe.cpp
  src/animation_binding.cpp
  src/animation_decoder.cpp
  src/viewmodel_material.cpp
  src/vmt_material.cpp
  src/vtf_texture.cpp
  src/model_loader.cpp
  src/asset_root.cpp
  ${TF2_DEMO_HEADER_SOURCES}
  src/item_schema.cpp)
target_link_libraries(model_robustness_probe PRIVATE ${TF2_DEMO_HEADER_LIBS})
target_compile_definitions(model_robustness_probe PRIVATE ${TF2_DEMO_HEADER_DEFINES})
target_include_directories(model_robustness_probe PRIVATE include third_party/lzma)
set_target_properties(model_robustness_probe PROPERTIES WIN32_EXECUTABLE OFF)

add_executable(entity_model_probe tools/entity_model_probe.cpp src/entity_model.cpp src/model_loader.cpp src/asset_root.cpp src/asset_lookup.cpp src/bsp_map.cpp src/world_material.cpp ${TF2_DEMO_HEADER_SOURCES} src/item_schema.cpp src/vmt_material.cpp src/vtf_texture.cpp)
target_link_libraries(entity_model_probe PRIVATE ${TF2_DEMO_HEADER_LIBS})
target_compile_definitions(entity_model_probe PRIVATE ${TF2_DEMO_HEADER_DEFINES})
target_include_directories(entity_model_probe PRIVATE include third_party/lzma)
set_target_properties(entity_model_probe PROPERTIES WIN32_EXECUTABLE OFF)
