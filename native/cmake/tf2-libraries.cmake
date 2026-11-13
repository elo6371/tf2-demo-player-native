# ---------------------------------------------------------------------------
# T85-E2 (2026-11-13): the shared static library.
#
# Before this module existed the fifty-one translation units below had no
# owner: every target that needed one listed it in its own add_executable().
# One clean build at the seam commit (1485396) therefore compiled them 551
# times -- the same text, the same headers, the same flags, re-parsed for each
# of the 54 executables. On the previous build log (evidence/cmake-build.log:
# 52 sources compiled by two or more targets, 553 of its 633 compile actions)
# moving them here turns 634 (target, source) pairs into 132 compile actions:
# one per source plus the two per-target copies of the generated build stamp.
#
# This is code motion, not a behaviour change: it changes WHICH target compiles
# a file, not what the file says. The flags below are the product's existing set
# for these exact files (/W4 /permissive- /utf-8 /EHsc; UNICODE omitted because
# no member's headers resolve differently with it) -- the product already
# compiles every member under that set, so it is the only choice with a
# compile-existence proof. The binaries still change bytes: a translation unit
# compiled once cannot produce the same COFF objects as the same unit compiled
# forty times, so 'behaviour unchanged' is proven by gate readings and
# fixed-pose frames, never by an exe hash.
#
# Position: included AFTER tf2-version.cmake (its members are compiled with
# TF2_VERSION_DEFINES, defined there) and BEFORE every module whose targets
# link it.
#
# Deliberately not a member: generated/tf2_build_stamp.cpp. It carries the
# build identity of the executable that links it, so it stays per target.
# ---------------------------------------------------------------------------
add_library(tf2_core STATIC
  # T9/T90: the demo-header contract -- the one list of twelve sources.
  src/demo_header.cpp
  src/demo_header_core.cpp
  src/demo_header_net.cpp
  src/demo_header_entity.cpp
  src/demo_header_strtab.cpp
  src/demo_header_sound.cpp
  src/demo_header_events.cpp
  src/demo_header_track.cpp
  src/audio_timeline.cpp
  src/audio_resource.cpp
  src/vpk_archive.cpp
  third_party/lzma/LzmaDec.c

  # The shared domain implementations. Each one is here because two or more
  # targets compiled it separately; no consumer keeps a private copy.

  # Map and asset access.
  src/asset_root.cpp
  src/asset_lookup.cpp
  src/bsp_map.cpp
  src/model_loader.cpp
  src/vtf_texture.cpp
  src/vmt_material.cpp
  src/item_schema.cpp
  src/proto_wire.cpp

  # Animation.
  src/animation_decoder.cpp
  src/animation_binding.cpp
  src/animation_clock.cpp
  src/entity_model.cpp
  src/viewmodel.cpp
  src/viewmodel_material.cpp

  # World material and lighting.
  src/world_material.cpp
  src/world_material_features.cpp
  src/world_lighting.cpp
  src/world_tile_atlas.cpp
  src/effects_timeline.cpp

  # Audio playback (the import libraries are linked below).
  src/audio_player.cpp

  # Overlay, playback ledger and HUD import rules.
  src/entity_overlay.cpp
  src/projectile_track.cpp
  src/projectile_overlay.cpp
  src/spectate_target.cpp
  src/playback_hud.cpp
  src/playback_control.cpp
  src/map_lookup.cpp
  src/pcf_operators.cpp
  src/hud_import.cpp
  src/camera_director.cpp
  src/camera_bookmarks.cpp

  # Settings, UI and installer surfaces.
  src/keybinds.cpp
  src/ini_section.cpp
  src/native_ui.cpp
  src/logstf_viewer.cpp
  src/review_workspace.cpp
  src/review_bookmarks.cpp
  src/install_manifest.cpp
  src/install_lifecycle.cpp
)

# The members are compiled once, so what they need to compile has to be here
# rather than in whichever consumer happens to link them.
target_include_directories(tf2_core PUBLIC include third_party/lzma)

# NOMINMAX: audio_resource.cpp calls std::min on values from <windows.h> and
# fails with C2589 without it. The version defines: install_lifecycle.cpp
# bakes TF2_PACKAGE_VERSION into the installer lifecycle it reports.
target_compile_definitions(tf2_core PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN ${TF2_VERSION_DEFINES})

# The product's warning and standard set, so each member is compiled exactly as
# the product compiled it before this module existed.
target_compile_options(tf2_core PRIVATE /W4 /permissive- /utf-8 /EHsc)

# audio_resource.cpp calls into winmm/msacm32. PUBLIC because a static library
# does not carry its own import libraries: every consumer links them too.
target_link_libraries(tf2_core PUBLIC winmm msacm32)
