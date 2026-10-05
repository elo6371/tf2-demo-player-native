cmake_minimum_required(VERSION 3.24)
project(tf2_demo_native LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_WIN32_EXECUTABLE ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_executable(tf2_demo_native WIN32
  src/main.cpp
  src/demo_header.cpp
  src/native_renderer.cpp
  src/asset_root.cpp
  src/vpk_archive.cpp
  src/vtf_texture.cpp
  src/model_loader.cpp
  src/vmt_material.cpp
  src/audio_resource.cpp
  src/audio_player.cpp
  src/audio_timeline.cpp
  src/effects_timeline.cpp
  src/bsp_map.cpp
  third_party/lzma/LzmaDec.c
  src/texture2d.cpp
  src/settings_store.cpp
  src/item_schema.cpp
  src/native_ui.cpp
)

target_include_directories(tf2_demo_native PRIVATE include third_party/lzma)
target_compile_definitions(tf2_demo_native PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(tf2_demo_native PRIVATE d3d11 dxgi d3dcompiler user32 gdi32 shell32 advapi32 ole32 winmm bcrypt psapi comctl32 comdlg32)

add_executable(item_schema_probe tools/item_schema_probe.cpp src/item_schema.cpp src/proto_wire.cpp)
target_include_directories(item_schema_probe PRIVATE include)
set_target_properties(item_schema_probe PROPERTIES WIN32_EXECUTABLE OFF)

add_executable(temp_effect_probe tools/temp_effect_probe.cpp src/effects_timeline.cpp)
target_include_directories(temp_effect_probe PRIVATE include)
set_target_properties(temp_effect_probe PROPERTIES WIN32_EXECUTABLE OFF)

add_executable(temp_entities_fixture_probe tools/temp_entities_fixture_probe.cpp)
set_target_properties(temp_entities_fixture_probe PROPERTIES WIN32_EXECUTABLE OFF)

add_executable(demo_open_probe tools/demo_open_probe.cpp src/demo_header.cpp src/proto_wire.cpp src/item_schema.cpp src/vpk_archive.cpp third_party/lzma/LzmaDec.c)
target_include_directories(demo_open_probe PRIVATE include third_party/lzma)
set_target_properties(demo_open_probe PROPERTIES WIN32_EXECUTABLE OFF)

add_executable(pcf_capability_probe tools/pcf_capability_probe.cpp src/vpk_archive.cpp)
target_include_directories(pcf_capability_probe PRIVATE include third_party/lzma)
set_target_properties(pcf_capability_probe PROPERTIES WIN32_EXECUTABLE OFF)

add_executable(material_chain_probe tools/material_chain_probe.cpp src/vtf_texture.cpp src/bsp_map.cpp src/vmt_material.cpp src/vpk_archive.cpp third_party/lzma/LzmaDec.c)
target_include_directories(material_chain_probe PRIVATE include third_party/lzma)
set_target_properties(material_chain_probe PROPERTIES WIN32_EXECUTABLE OFF)

add_executable(texture_quad_probe tools/texture_quad_probe.cpp src/vtf_texture.cpp)
target_include_directories(texture_quad_probe PRIVATE include)
target_link_libraries(texture_quad_probe PRIVATE d3d11 d3dcompiler)
set_target_properties(texture_quad_probe PROPERTIES WIN32_EXECUTABLE OFF)

add_executable(model_pose_probe tools/model_pose_probe.cpp src/model_loader.cpp src/asset_root.cpp src/vpk_archive.cpp src/demo_header.cpp src/item_schema.cpp)
target_include_directories(model_pose_probe PRIVATE include)
set_target_properties(model_pose_probe PROPERTIES WIN32_EXECUTABLE OFF)

add_executable(model_render_request_probe tools/model_render_request_probe.cpp src/model_loader.cpp src/asset_root.cpp src/vpk_archive.cpp src/demo_header.cpp src/item_schema.cpp)
target_include_directories(model_render_request_probe PRIVATE include)
set_target_properties(model_render_request_probe PROPERTIES WIN32_EXECUTABLE OFF)

add_executable(viewmodel_fov_probe tools/viewmodel_fov_probe.cpp)
target_include_directories(viewmodel_fov_probe PRIVATE include)
set_target_properties(viewmodel_fov_probe PROPERTIES WIN32_EXECUTABLE OFF)

add_executable(audio_scheduler_probe tools/audio_scheduler_probe.cpp src/audio_player.cpp src/audio_timeline.cpp src/audio_resource.cpp src/vpk_archive.cpp)
target_include_directories(audio_scheduler_probe PRIVATE include)
target_compile_definitions(audio_scheduler_probe PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(audio_scheduler_probe PRIVATE winmm)
set_target_properties(audio_scheduler_probe PROPERTIES WIN32_EXECUTABLE OFF)

add_executable(winmm_sink_probe tools/winmm_sink_probe.cpp)
target_compile_definitions(winmm_sink_probe PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(winmm_sink_probe PRIVATE winmm)
set_target_properties(winmm_sink_probe PROPERTIES WIN32_EXECUTABLE OFF)

add_executable(native_install_smoke_probe tools/native_install_smoke_probe.cpp src/asset_root.cpp)
target_include_directories(native_install_smoke_probe PRIVATE include)
target_compile_definitions(native_install_smoke_probe PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(native_install_smoke_probe PRIVATE d3d11 dxgi)
set_target_properties(native_install_smoke_probe PROPERTIES WIN32_EXECUTABLE OFF)

add_executable(native_ui_probe tools/native_ui_probe.cpp src/native_ui.cpp src/demo_header.cpp src/proto_wire.cpp src/item_schema.cpp src/vpk_archive.cpp third_party/lzma/LzmaDec.c)
target_include_directories(native_ui_probe PRIVATE include third_party/lzma)
set_target_properties(native_ui_probe PROPERTIES WIN32_EXECUTABLE OFF)

install(TARGETS tf2_demo_native native_install_smoke_probe
  RUNTIME DESTINATION .
)
install(PROGRAMS tools/install_smoke_probe.ps1 DESTINATION .)

if (MSVC)
  target_compile_options(tf2_demo_native PRIVATE /W4 /permissive- /EHsc /utf-8)
  target_link_options(tf2_demo_native PRIVATE /DYNAMICBASE /NXCOMPAT)
endif()

