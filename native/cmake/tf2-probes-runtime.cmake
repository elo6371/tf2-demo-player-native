# ---------------------------------------------------------------------------
# T75 (2026-10-28): split out of native/CMakeLists.txt (HEAD db3316e).
#
# install smoke and the native UI resource panel.
#
# The text below is the ORIGINAL lines 666-687 of native/CMakeLists.txt, moved
# verbatim: no target name, source list, link line, compile definition, compile
# option, install rule or cache variable was edited, added or re-ordered.  The
# include() order in ../CMakeLists.txt is the original statement order, so
# target creation order -- which is the NMAKE build order, and therefore which
# targets a failed link truncates -- is unchanged as well.
#
# Targets defined here: native_install_smoke_probe native_ui_probe
# T75-BODY-START
# ---------------------------------------------------------------------------
# AssetRoot::archives() lives in asset_root.cpp and calls
# VpkArchiveSet::openDirectory, so anything compiling asset_root.cpp also has to
# link vpk_archive.cpp. A clean build catches the omission as LNK2019 rather
# than silently producing a probe that cannot open a VPK.
add_executable(native_install_smoke_probe tools/native_install_smoke_probe.cpp src/asset_root.cpp src/vpk_archive.cpp)
target_include_directories(native_install_smoke_probe PRIVATE include)
target_compile_definitions(native_install_smoke_probe PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(native_install_smoke_probe PRIVATE d3d11 dxgi)
set_target_properties(native_install_smoke_probe PROPERTIES WIN32_EXECUTABLE OFF)

# T15: native_ui.cpp calls AssetRoot::fromPath and AssetRoot::archives (the
# resource-state panel asks whether the tf root is usable) and planMapLookup
# (it shows where the map was found). None of those three lived in this target's
# source list, so the probe linked only while a previous build's objects were
# still on disk; after `--target clean` the link fails with three LNK2019s and
# NMAKE stops, which is what truncated the full build to 33 of 38 executables
# and made the exe_count pin read "correct" for a build that never finished.
add_executable(native_ui_probe tools/native_ui_probe.cpp src/native_ui.cpp src/asset_root.cpp src/map_lookup.cpp src/vpk_archive.cpp ${TF2_DEMO_HEADER_SOURCES} src/proto_wire.cpp src/item_schema.cpp)
target_link_libraries(native_ui_probe PRIVATE ${TF2_DEMO_HEADER_LIBS})
target_compile_definitions(native_ui_probe PRIVATE ${TF2_DEMO_HEADER_DEFINES})
target_include_directories(native_ui_probe PRIVATE include third_party/lzma)
set_target_properties(native_ui_probe PROPERTIES WIN32_EXECUTABLE OFF)
