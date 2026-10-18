cmake_minimum_required(VERSION 3.24)
project(tf2_demo_native LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_WIN32_EXECUTABLE ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# T9 (2026-10-16): build-graph repair, no behaviour change.
#
# The audio layer moved buildSoundTimeline into demo_header.cpp, where it calls
# into audio_timeline.cpp, which in turn needs audio_resource.cpp (WAV/MP3
# classification) and the winmm/msacm32 libraries that file links against. Any
# target that compiles demo_header.cpp therefore needs all of that, and fourteen
# of them did not have it, so `cmake --build` stopped at the first one
# (LNK1120 on demo_open_probe, four symbols; then three more one layer down,
# then two more in presentation_probe, then four in viewmodel_fov_probe).
#
# Reproducible at the T9 base commit with demo_header.cpp byte-identical to the
# base and every one of those target blocks untouched, so this is a merge break
# that predates T9 and blocked every build, not a T9 change. Listed once here
# because this same list was already silently short in fourteen places; a
# fifteenth copy of it is how it got that way.
set(TF2_DEMO_HEADER_SOURCES src/demo_header.cpp src/audio_timeline.cpp src/audio_resource.cpp src/vpk_archive.cpp third_party/lzma/LzmaDec.c)
set(TF2_DEMO_HEADER_LIBS winmm msacm32)
# audio_resource.cpp calls std::min on values from <windows.h>, so a target that
# compiles it without NOMINMAX fails at `std::min` (C2589 "illegal token on right
# side of ::", because the min macro eats the call). The product target already
# carried these two; the probe copies did not.
set(TF2_DEMO_HEADER_DEFINES NOMINMAX WIN32_LEAN_AND_MEAN)

add_executable(tf2_demo_native WIN32
  src/main.cpp
  ${TF2_DEMO_HEADER_SOURCES}
  src/native_renderer.cpp
  src/asset_root.cpp
  # T9 (2026-10-16): the map search order, shared with map_lookup_probe. In the
  # product rather than inlined into main.cpp because the gate has to assert the
  # order the product actually walks, not a second copy of it.
  src/map_lookup.cpp
  src/vpk_archive.cpp
  src/vtf_texture.cpp
  src/model_loader.cpp
  src/viewmodel.cpp
  # The ViewModel material chain, shared with viewmodel_material_probe. In the
  # product rather than inlined into viewmodel.cpp because the probe has to
  # resolve the same table from the same bytes: a gate that asserts the two agree
  # is only non-circular if there is one implementation to agree with.
  src/viewmodel_material.cpp
  src/entity_model.cpp
  src/animation_decoder.cpp
  src/animation_binding.cpp
  src/vmt_material.cpp
  src/audio_resource.cpp
  src/audio_player.cpp
  src/audio_timeline.cpp
  src/effects_timeline.cpp
  src/pcf_operators.cpp
  src/playback_hud.cpp
  src/bsp_map.cpp
  # The world material capability layer. In the product, not just in the probe:
  # the product resolves a map's cubemap through the same bspPakfileEntry and
  # worldCubemapVtfPath the probe measures, so "what the gate pins" and "what the
  # product does" are one implementation rather than two that agree by hand.
  src/world_material.cpp
  third_party/lzma/LzmaDec.c
  src/texture2d.cpp
  src/settings_store.cpp
  src/item_schema.cpp
  src/native_ui.cpp
)

target_include_directories(tf2_demo_native PRIVATE include third_party/lzma)
target_compile_definitions(tf2_demo_native PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
# msacm32 supplies the ACM stream API audio_resource.cpp uses to turn MPEG
# Layer III resources into PCM; without it the link fails on acmStreamOpen.
target_link_libraries(tf2_demo_native PRIVATE d3d11 dxgi d3dcompiler user32 gdi32 shell32 advapi32 ole32 winmm msacm32 bcrypt psapi comctl32 comdlg32)

add_executable(item_schema_probe tools/item_schema_probe.cpp src/item_schema.cpp src/proto_wire.cpp)
target_include_directories(item_schema_probe PRIVATE include)
set_target_properties(item_schema_probe PROPERTIES WIN32_EXECUTABLE OFF)

add_executable(temp_effect_probe tools/temp_effect_probe.cpp src/effects_timeline.cpp)
target_include_directories(temp_effect_probe PRIVATE include)
set_target_properties(temp_effect_probe PROPERTIES WIN32_EXECUTABLE OFF)

add_executable(temp_entities_fixture_probe tools/temp_entities_fixture_probe.cpp)
set_target_properties(temp_entities_fixture_probe PROPERTIES WIN32_EXECUTABLE OFF)

add_executable(demo_open_probe tools/demo_open_probe.cpp ${TF2_DEMO_HEADER_SOURCES} src/proto_wire.cpp src/item_schema.cpp)
target_include_directories(demo_open_probe PRIVATE include third_party/lzma)
target_link_libraries(demo_open_probe PRIVATE ${TF2_DEMO_HEADER_LIBS})
target_compile_definitions(demo_open_probe PRIVATE ${TF2_DEMO_HEADER_DEFINES})
set_target_properties(demo_open_probe PROPERTIES WIN32_EXECUTABLE OFF)

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
add_executable(terrain_blend_probe tools/terrain_blend_probe.cpp src/bsp_map.cpp src/world_material.cpp src/vmt_material.cpp src/vpk_archive.cpp src/vtf_texture.cpp third_party/lzma/LzmaDec.c)
target_include_directories(terrain_blend_probe PRIVATE include third_party/lzma)
set_target_properties(terrain_blend_probe PROPERTIES WIN32_EXECUTABLE OFF)

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

add_executable(model_pose_probe tools/model_pose_probe.cpp src/model_loader.cpp src/asset_root.cpp ${TF2_DEMO_HEADER_SOURCES} src/item_schema.cpp)
target_link_libraries(model_pose_probe PRIVATE ${TF2_DEMO_HEADER_LIBS})
target_compile_definitions(model_pose_probe PRIVATE ${TF2_DEMO_HEADER_DEFINES})
target_include_directories(model_pose_probe PRIVATE include)
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

add_executable(entity_model_probe tools/entity_model_probe.cpp src/entity_model.cpp src/model_loader.cpp src/asset_root.cpp ${TF2_DEMO_HEADER_SOURCES} src/item_schema.cpp src/vmt_material.cpp src/vtf_texture.cpp)
target_link_libraries(entity_model_probe PRIVATE ${TF2_DEMO_HEADER_LIBS})
target_compile_definitions(entity_model_probe PRIVATE ${TF2_DEMO_HEADER_DEFINES})
target_include_directories(entity_model_probe PRIVATE include third_party/lzma)
set_target_properties(entity_model_probe PROPERTIES WIN32_EXECUTABLE OFF)

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

# Proves the skinning contract (poseToBone is the inverse bind; skin is
# animWorld * poseToBone and identity at bind pose) against the game's own
# files, before the product wires any of it up.
add_executable(skeleton_skin_probe
  tools/skeleton_skin_probe.cpp
  src/animation_decoder.cpp
  src/model_loader.cpp
  src/asset_root.cpp
  src/item_schema.cpp
  src/proto_wire.cpp
  ${TF2_DEMO_HEADER_SOURCES})
target_include_directories(skeleton_skin_probe PRIVATE include third_party/lzma)
target_compile_definitions(skeleton_skin_probe PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(skeleton_skin_probe PRIVATE ${TF2_DEMO_HEADER_LIBS})
target_compile_definitions(skeleton_skin_probe PRIVATE ${TF2_DEMO_HEADER_DEFINES})
set_target_properties(skeleton_skin_probe PROPERTIES WIN32_EXECUTABLE OFF)

# The half skeleton_skin_probe does not cover: an entity's own m_nSequence, read
# out of a real demo, turned into the matrices the product would upload. Links the
# demo reader as well as the animation decoder, because the sequence value comes
# from the wire, not from the model file.
add_executable(weapon_animation_probe
  tools/weapon_animation_probe.cpp
  src/animation_decoder.cpp
  src/model_loader.cpp
  src/asset_root.cpp
  src/item_schema.cpp
  src/proto_wire.cpp
  ${TF2_DEMO_HEADER_SOURCES})
target_include_directories(weapon_animation_probe PRIVATE include third_party/lzma)
target_compile_definitions(weapon_animation_probe PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(weapon_animation_probe PRIVATE ${TF2_DEMO_HEADER_LIBS})
target_compile_definitions(weapon_animation_probe PRIVATE ${TF2_DEMO_HEADER_DEFINES})
set_target_properties(weapon_animation_probe PROPERTIES WIN32_EXECUTABLE OFF)

add_executable(audio_effects_probe tools/audio_effects_probe.cpp src/audio_player.cpp src/audio_timeline.cpp src/audio_resource.cpp src/effects_timeline.cpp src/pcf_operators.cpp src/playback_hud.cpp src/vpk_archive.cpp third_party/lzma/LzmaDec.c)
target_include_directories(audio_effects_probe PRIVATE include third_party/lzma)
target_compile_definitions(audio_effects_probe PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(audio_effects_probe PRIVATE winmm msacm32)
set_target_properties(audio_effects_probe PROPERTIES WIN32_EXECUTABLE OFF)

add_executable(audio_scheduler_probe tools/audio_scheduler_probe.cpp src/audio_player.cpp src/audio_timeline.cpp src/audio_resource.cpp src/vpk_archive.cpp)
target_include_directories(audio_scheduler_probe PRIVATE include)
target_compile_definitions(audio_scheduler_probe PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(audio_scheduler_probe PRIVATE winmm msacm32)
set_target_properties(audio_scheduler_probe PROPERTIES WIN32_EXECUTABLE OFF)

# Reads a real demo and reports where its decoded sound events sit relative to
# the playback origin. Links demo_header.cpp (the decoder) and audio_timeline.cpp
# (the origin/tick helpers) so the census and the product share one definition of
# the relative tick instead of each keeping its own copy.
add_executable(sound_timeline_census_probe
  tools/sound_timeline_census_probe.cpp
  src/audio_player.cpp
  ${TF2_DEMO_HEADER_SOURCES})
target_include_directories(sound_timeline_census_probe PRIVATE include third_party/lzma)
target_compile_definitions(sound_timeline_census_probe PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
# winmm for AudioPlayer's declarations; the probe injects an outcome sink, so no
# device is opened even though the backend is linked. msacm32 for the MPEG
# Layer III decode path in audio_resource.cpp.
target_link_libraries(sound_timeline_census_probe PRIVATE winmm msacm32)
target_link_libraries(sound_timeline_census_probe PRIVATE ${TF2_DEMO_HEADER_LIBS})
target_compile_definitions(sound_timeline_census_probe PRIVATE ${TF2_DEMO_HEADER_DEFINES})
set_target_properties(sound_timeline_census_probe PROPERTIES WIN32_EXECUTABLE OFF)

# The traceable content map: one line per scheduled event from the demo's own
# soundprecache table through the product's archive chain to a named decode
# status, plus the per-kind / per-status counts a gate asserts against. Links
# the same four sources the census links, and winmm only for AudioPlayer's
# declarations -- the scheduler is driven with an injected sink, so no device is
# opened.
add_executable(sound_content_map_probe
  tools/sound_content_map_probe.cpp
  src/audio_player.cpp
  ${TF2_DEMO_HEADER_SOURCES})
target_include_directories(sound_content_map_probe PRIVATE include third_party/lzma)
target_compile_definitions(sound_content_map_probe PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(sound_content_map_probe PRIVATE winmm msacm32)
target_link_libraries(sound_content_map_probe PRIVATE ${TF2_DEMO_HEADER_LIBS})
target_compile_definitions(sound_content_map_probe PRIVATE ${TF2_DEMO_HEADER_DEFINES})
set_target_properties(sound_content_map_probe PROPERTIES WIN32_EXECUTABLE OFF)

# Walks a corpus of real recordings and reports, per demo, which sound event
# kinds the demo actually carries: the decoded event kinds, the resolved-name
# kinds, and the raw svc_voice_*/svc_temp_entities message presence. Exists so
# a claim of the form "music events are covered by a real input" can point at a
# named recording instead of at the classifier's ability to name the kind.
# Links the same sources the content map links plus demo_header.cpp's decode
# entry points, and drives the scheduler with an injected sink so no device is
# opened.
add_executable(sound_corpus_scan_probe
  tools/sound_corpus_scan_probe.cpp
  src/audio_player.cpp
  ${TF2_DEMO_HEADER_SOURCES})
target_include_directories(sound_corpus_scan_probe PRIVATE include third_party/lzma)
target_compile_definitions(sound_corpus_scan_probe PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(sound_corpus_scan_probe PRIVATE winmm msacm32)
target_link_libraries(sound_corpus_scan_probe PRIVATE ${TF2_DEMO_HEADER_LIBS})
target_compile_definitions(sound_corpus_scan_probe PRIVATE ${TF2_DEMO_HEADER_DEFINES})
set_target_properties(sound_corpus_scan_probe PROPERTIES WIN32_EXECUTABLE OFF)

add_executable(winmm_sink_probe tools/winmm_sink_probe.cpp)
target_compile_definitions(winmm_sink_probe PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(winmm_sink_probe PRIVATE winmm)
set_target_properties(winmm_sink_probe PROPERTIES WIN32_EXECUTABLE OFF)

# AssetRoot::archives() lives in asset_root.cpp and calls
# VpkArchiveSet::openDirectory, so anything compiling asset_root.cpp also has to
# link vpk_archive.cpp. A clean build catches the omission as LNK2019 rather
# than silently producing a probe that cannot open a VPK.
add_executable(native_install_smoke_probe tools/native_install_smoke_probe.cpp src/asset_root.cpp src/vpk_archive.cpp)
target_include_directories(native_install_smoke_probe PRIVATE include)
target_compile_definitions(native_install_smoke_probe PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(native_install_smoke_probe PRIVATE d3d11 dxgi)
set_target_properties(native_install_smoke_probe PROPERTIES WIN32_EXECUTABLE OFF)

add_executable(native_ui_probe tools/native_ui_probe.cpp src/native_ui.cpp ${TF2_DEMO_HEADER_SOURCES} src/proto_wire.cpp src/item_schema.cpp)
target_link_libraries(native_ui_probe PRIVATE ${TF2_DEMO_HEADER_LIBS})
target_compile_definitions(native_ui_probe PRIVATE ${TF2_DEMO_HEADER_DEFINES})
target_include_directories(native_ui_probe PRIVATE include third_party/lzma)
set_target_properties(native_ui_probe PROPERTIES WIN32_EXECUTABLE OFF)

# P0 entity protocol census probe. Deliberately compiled with the same strict
# flags as the product target (/W4 /permissive-) so a header change that would
# break the product cannot hide behind a lenient probe build.
add_executable(entity_protocol_probe
  tools/entity_protocol_probe.cpp
  ${TF2_DEMO_HEADER_SOURCES}
  src/proto_wire.cpp
)
target_include_directories(entity_protocol_probe PRIVATE include)
target_link_libraries(entity_protocol_probe PRIVATE ${TF2_DEMO_HEADER_LIBS})
target_compile_definitions(entity_protocol_probe PRIVATE ${TF2_DEMO_HEADER_DEFINES})
set_target_properties(entity_protocol_probe PROPERTIES WIN32_EXECUTABLE OFF)

# Synthetic wire fixtures for the message decoders. Same sources and same
# strictness as the product target so a layout regression cannot hide here.
add_executable(entity_message_fixture_probe
  tools/entity_message_fixture_probe.cpp
  ${TF2_DEMO_HEADER_SOURCES}
  src/proto_wire.cpp
)
target_include_directories(entity_message_fixture_probe PRIVATE include)
target_link_libraries(entity_message_fixture_probe PRIVATE ${TF2_DEMO_HEADER_LIBS})
target_compile_definitions(entity_message_fixture_probe PRIVATE ${TF2_DEMO_HEADER_DEFINES})
set_target_properties(entity_message_fixture_probe PROPERTIES WIN32_EXECUTABLE OFF)

# The quiet-capture reader: compares one normal-mode run and one
# --hidden-window --audio-disabled run of tf2_demo_native (their traces,
# metrics and bitmaps) and reports the contract as one key=value line.
# Standard library only -- no project sources, no Windows APIs -- because it
# reads files the product already wrote.
add_executable(quiet_capture_probe tools/quiet_capture_probe.cpp)
set_target_properties(quiet_capture_probe PROPERTIES WIN32_EXECUTABLE OFF)

# The ViewModel material resource chain, phase 1: MDL texture slot ->
# $cdmaterials -> .vmt -> $basetexture -> .vtf -> decoded pixels, for the three
# ViewModel model families (dynamic c_*_arms, old v_*, static weapon bodies).
# It links the product's own parsers and asset root rather than a second copy of
# either, draws nothing, and is gated by viewmodel-material-check.sh. The
# product still flat-shades the ViewModel: nothing here is wired into a pass.
add_executable(viewmodel_material_probe
  tools/viewmodel_material_probe.cpp
  src/model_loader.cpp
  src/asset_root.cpp
  src/vmt_material.cpp
  src/vtf_texture.cpp
  src/viewmodel_material.cpp
  ${TF2_DEMO_HEADER_SOURCES}
  src/item_schema.cpp
  src/proto_wire.cpp)
target_include_directories(viewmodel_material_probe PRIVATE include third_party/lzma)
target_link_libraries(viewmodel_material_probe PRIVATE ${TF2_DEMO_HEADER_LIBS})
target_compile_definitions(viewmodel_material_probe PRIVATE ${TF2_DEMO_HEADER_DEFINES})
set_target_properties(viewmodel_material_probe PROPERTIES WIN32_EXECUTABLE OFF)

install(TARGETS tf2_demo_native native_install_smoke_probe
  RUNTIME DESTINATION .
)
install(PROGRAMS tools/install_smoke_probe.ps1 DESTINATION .)

if (MSVC)
  target_compile_options(tf2_demo_native PRIVATE /W4 /permissive- /EHsc /utf-8)
  target_compile_options(entity_protocol_probe PRIVATE /W4 /permissive- /EHsc /utf-8)
  target_compile_options(entity_message_fixture_probe PRIVATE /W4 /permissive- /EHsc /utf-8)
  target_link_options(tf2_demo_native PRIVATE /DYNAMICBASE /NXCOMPAT)
endif()

