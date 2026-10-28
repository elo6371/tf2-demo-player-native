# ---------------------------------------------------------------------------
# T75 (2026-10-28): split out of native/CMakeLists.txt (HEAD db3316e).
#
# the ViewModel contract, FOV, animation state and the static weapon body.
#
# The text below is the ORIGINAL lines 434-558 of native/CMakeLists.txt, moved
# verbatim: no target name, source list, link line, compile definition, compile
# option, install rule or cache variable was edited, added or re-ordered.  The
# include() order in ../CMakeLists.txt is the original statement order, so
# target creation order -- which is the NMAKE build order, and therefore which
# targets a failed link truncates -- is unchanged as well.
#
# Targets defined here: viewmodel_fov_probe viewmodel_contract_probe viewmodel_animation_probe weapon_body_probe animation_viewmodel_probe
# T75-BODY-START
# ---------------------------------------------------------------------------
add_executable(viewmodel_fov_probe
  tools/viewmodel_fov_probe.cpp
  src/viewmodel.cpp
  # viewmodel.cpp calls buildAnimationSkinMatrices, so the T1 animation layer is
  # a link dependency of every target that compiles it. Left out, the link fails,
  # NMAKE stops, and every target after this one is silently not built.
  src/animation_binding.cpp
  src/animation_decoder.cpp
  # T1: viewmodel.cpp now calls the shared material resolver, which parses VMTs
  # and decodes VTFs. Same failure class as the animation layer above -- a link
  # dependency left out fails the link, NMAKE stops, and every target after this
  # one is silently not built. Caught by verify-fast.sh, not by any viewmodel gate.
  src/viewmodel_material.cpp
  src/vmt_material.cpp
  src/vtf_texture.cpp
  src/model_loader.cpp
  src/asset_root.cpp
  ${TF2_DEMO_HEADER_SOURCES}
  src/item_schema.cpp
  src/proto_wire.cpp
  # viewmodel.cpp also resolves the model's material chain (MDL texture slot ->
  # $cdmaterials -> .vmt -> $basetexture -> .vtf), so VmtParser is a link
  # dependency of every target that compiles it. Same failure mode as the
  # animation layer above: left out, the link fails and NMAKE stops.
  src/vmt_material.cpp)
target_include_directories(viewmodel_fov_probe PRIVATE include third_party/lzma)
target_link_libraries(viewmodel_fov_probe PRIVATE ${TF2_DEMO_HEADER_LIBS})
target_compile_definitions(viewmodel_fov_probe PRIVATE ${TF2_DEMO_HEADER_DEFINES})
set_target_properties(viewmodel_fov_probe PROPERTIES WIN32_EXECUTABLE OFF)

add_executable(viewmodel_contract_probe
  tools/viewmodel_contract_probe.cpp
  src/viewmodel.cpp
  src/animation_binding.cpp
  src/animation_decoder.cpp
  # T1 material chain: viewmodel.cpp calls into it, so it links wherever
  # viewmodel.cpp does. See the note on viewmodel_fov_probe.
  src/viewmodel_material.cpp
  src/vmt_material.cpp
  src/vtf_texture.cpp
  src/model_loader.cpp
  src/asset_root.cpp
  ${TF2_DEMO_HEADER_SOURCES}
  src/item_schema.cpp
  src/proto_wire.cpp
  src/vmt_material.cpp)
target_include_directories(viewmodel_contract_probe PRIVATE include third_party/lzma)
target_link_libraries(viewmodel_contract_probe PRIVATE ${TF2_DEMO_HEADER_LIBS})
target_compile_definitions(viewmodel_contract_probe PRIVATE ${TF2_DEMO_HEADER_DEFINES})
set_target_properties(viewmodel_contract_probe PROPERTIES WIN32_EXECUTABLE OFF)

# Answers the question T2 left open and T1 answered "no" for players: does the
# demo carry a ViewModel's animation state at all, and does it index the model
# the ViewModel pass draws? It links viewmodel.cpp so the self-test can drive the
# product's own path rule and its own resolver instead of trusting a second copy
# of either.
add_executable(viewmodel_animation_probe
  tools/viewmodel_animation_probe.cpp
  src/viewmodel.cpp
  src/animation_binding.cpp
  src/animation_decoder.cpp
  # T1 material chain: viewmodel.cpp calls into it, so it links wherever
  # viewmodel.cpp does. See the note on viewmodel_fov_probe.
  src/viewmodel_material.cpp
  src/vmt_material.cpp
  src/vtf_texture.cpp
  src/entity_model.cpp
  src/model_loader.cpp
  src/asset_root.cpp
  ${TF2_DEMO_HEADER_SOURCES}
  src/item_schema.cpp
  src/proto_wire.cpp
  src/vmt_material.cpp)
target_include_directories(viewmodel_animation_probe PRIVATE include third_party/lzma)
target_link_libraries(viewmodel_animation_probe PRIVATE ${TF2_DEMO_HEADER_LIBS})
target_compile_definitions(viewmodel_animation_probe PRIVATE ${TF2_DEMO_HEADER_DEFINES})
set_target_properties(viewmodel_animation_probe PROPERTIES WIN32_EXECUTABLE OFF)

# The static weapon body: the second half of the first-person composite the
# ViewModel pass draws. It links entity_model.cpp for the chain that walks
# ViewModel entity -> m_hWeapon -> weapon entity -> m_iWorldModelIndex, and
# viewmodel.cpp for the material reading and the request builder the product
# uses, so the probe drives the product's own rules instead of a second copy.
# Its negatives are constructed (a zero index, an index that resolves to the
# arms, an absent weapon entity) because no recording here contains them; the
# ones that need a broken installation are left to mutation in
# weapon-body-check.sh.
add_executable(weapon_body_probe
  tools/weapon_body_probe.cpp
  src/viewmodel.cpp
  src/viewmodel_material.cpp
  src/animation_binding.cpp
  src/animation_decoder.cpp
  src/entity_model.cpp
  src/model_loader.cpp
  src/asset_root.cpp
  src/vmt_material.cpp
  src/vtf_texture.cpp
  ${TF2_DEMO_HEADER_SOURCES}
  src/item_schema.cpp
  src/proto_wire.cpp)
target_include_directories(weapon_body_probe PRIVATE include third_party/lzma)
target_link_libraries(weapon_body_probe PRIVATE ${TF2_DEMO_HEADER_LIBS})
target_compile_definitions(weapon_body_probe PRIVATE ${TF2_DEMO_HEADER_DEFINES})
set_target_properties(weapon_body_probe PROPERTIES WIN32_EXECUTABLE OFF)

add_executable(animation_viewmodel_probe
  tools/animation_viewmodel_probe.cpp
  src/animation_decoder.cpp
  src/animation_binding.cpp
  src/viewmodel.cpp
  # T1 material chain: viewmodel.cpp calls into it, so it links wherever
  # viewmodel.cpp does. See the note on viewmodel_fov_probe.
  src/viewmodel_material.cpp
  src/vmt_material.cpp
  src/vtf_texture.cpp
  src/model_loader.cpp
  src/asset_root.cpp
  ${TF2_DEMO_HEADER_SOURCES}
  src/item_schema.cpp
  src/vmt_material.cpp)
target_include_directories(animation_viewmodel_probe PRIVATE include)
target_link_libraries(animation_viewmodel_probe PRIVATE ${TF2_DEMO_HEADER_LIBS})
target_compile_definitions(animation_viewmodel_probe PRIVATE ${TF2_DEMO_HEADER_DEFINES})
set_target_properties(animation_viewmodel_probe PROPERTIES WIN32_EXECUTABLE OFF)
