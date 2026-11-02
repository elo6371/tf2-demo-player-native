# ---------------------------------------------------------------------------
# T75 (2026-10-28): split out of native/CMakeLists.txt (HEAD db3316e).
#
# the product target tf2_demo_native and its include dirs / defines / link line.
#
# The text below is the ORIGINAL lines 32-154, 181-190 of native/CMakeLists.txt, moved
# verbatim: no target name, source list, link line, compile definition, compile
# option, install rule or cache variable was edited, added or re-ordered.  The
# include() order in ../CMakeLists.txt is the original statement order, so
# target creation order -- which is the NMAKE build order, and therefore which
# targets a failed link truncates -- is unchanged as well.
#
# Targets defined here: tf2_demo_native
# T75-BODY-START
# ---------------------------------------------------------------------------
add_executable(tf2_demo_native WIN32
  src/main.cpp
  # T76: the two translation units split out of main.cpp -- the trace
  # checkpoint channel and the T37 installer lifecycle -- with their shared
  # declarations in src/main_internal.h. Code motion only: the command line,
  # the trace text and the exit codes are unchanged.
  src/main_trace.cpp
  src/main_lifecycle.cpp
  src/playback_state.cpp
  # T76: the T51 HUD product layer (state, folder picker, layout push and the
  # hud: readings). registerKeybinds stays in main.cpp -- hud-check.sh mutates
  # its text there.
  src/main_hud.cpp
  # T76: the T50 camera pose/bookmark layer.
  src/main_camera.cpp
  # T76 stage 2 (2026-11-02): the file, path and byte helpers -- readFile,
  # sha256File, localCacheDirectory, normalizedModelPath, narrowAscii,
  # entitySnapshotStatusName, addSteamRegistryRoots. No captured state and no
  # globals, so they were the cheapest slice to move next.
  src/main_helpers.cpp
  # T76 stage 3 (2026-11-02): the native Win32 control layer (layout, per-frame
  # sync, control creation). windowProc and dispatchHotkey stay in main.cpp.
  src/main_ui.cpp
  ${TF2_DEMO_HEADER_SOURCES}
  src/native_renderer.cpp
  # T77, second stage (2026-11-02): the HLSL, held apart from the D3D state that
  # compiles it. createPipeline() was 1,454 lines with 854 of them shader source
  # in the middle; the text now lives here and the function names it.
  src/native_renderer_shaders.cpp
  # T77 (2026-10-30): the renderer split. native_renderer.cpp keeps the device and
  # the pipeline construction; every other responsibility is its own translation
  # unit below. The bodies moved verbatim, so the pass order (which lives in
  # draw(), native_renderer_frame.cpp) and every threshold are unchanged; the
  # capture, trace and pixel gates assert the same contract they always did.
  src/native_renderer_settings.cpp
  src/native_renderer_capture.cpp
  src/native_renderer_camera.cpp
  src/native_renderer_entity.cpp
  src/native_renderer_overlay.cpp
  src/native_renderer_viewmodel.cpp
  src/native_renderer_world_material.cpp
  src/native_renderer_world.cpp
  src/native_renderer_frame.cpp
  # T53: the trajectory overlay builder. In the product because the probe must
  # drive the filter/linger/quality rules the product actually applies -- a probe
  # with its own copy would only prove two copies agree.
  src/projectile_track.cpp
  src/projectile_overlay.cpp
  src/asset_root.cpp
  # T9 (2026-10-16): the map search order, shared with map_lookup_probe. In the
  # product rather than inlined into main.cpp because the gate has to assert the
  # order the product actually walks, not a second copy of it.
  src/map_lookup.cpp
  src/vpk_archive.cpp
  # T15: the loose / VPK-directory / numbered-volume / pakfile search order as
  # one object. In the product because vpk_integrity_probe has to drive the same
  # order the product uses; a probe with its own copy would only prove that two
  # copies agree.
  src/asset_lookup.cpp
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
  # T61: the per-player animation clock. In the product because the probe and
  # the gate drive the same arithmetic the product drives; a copy in the gate
  # would only prove the two copies agree.
  src/animation_clock.cpp
  src/vmt_material.cpp
  src/audio_resource.cpp
  src/audio_player.cpp
  src/audio_timeline.cpp
  src/effects_timeline.cpp
  src/pcf_operators.cpp
  src/playback_hud.cpp
  # T70: the request/landing ledger. Pure logic, shared with
  # playback_control_probe so the gate drives the same implementation the
  # product runs rather than a second copy that happens to agree.
  src/playback_control.cpp
  # T51: the custom HUD import. hud_import.cpp is the pure rule set (shared with
  # hud_probe byte for byte, so the probe and the product cannot disagree about
  # what a document means); hud_state.cpp is the [hud] settings round trip.
  src/hud_import.cpp
  src/hud_state.cpp
  # T67: the embedded 8x8 font and its atlas; the HUD text pass samples it.
  src/hud_font.cpp
  # T50's shared hotkey/INI layer, copied byte-identical from task/t50-camera.
  # The HUD registers its three actions into the ONE table rather than a second.
  src/keybinds.cpp
  src/ini_section.cpp
  # T52: the shared overlay contract both T52 and T53 implement against.
  src/entity_overlay.cpp
  src/bsp_map.cpp
  # The world material capability layer. In the product, not just in the probe:
  # the product resolves a map's cubemap through the same bspPakfileEntry and
  # worldCubemapVtfPath the probe measures, so "what the gate pins" and "what the
  # product does" are one implementation rather than two that agree by hand.
  src/world_material.cpp
  # T19: the tile atlas plan and the feature scan. Pure functions, shared with
  # material_coverage_probe: the gate asserts the plan the product made, not a
  # second copy of the placement rule.
  src/world_tile_atlas.cpp
  src/world_material_features.cpp
  # T24: the lighting-control ledger, shared with lighting_semantics_probe. In
  # the product because the trace and the gate must read one classification:
  # the probe's six rows and the product's six rows are the same function.
  src/world_lighting.cpp
  third_party/lzma/LzmaDec.c
  src/texture2d.cpp
  src/settings_store.cpp
  src/item_schema.cpp
  src/native_ui.cpp
  # T50: the camera director, the named bookmark store and the ONE key table.
  # In the product rather than only in the probe because the probe has to drive
  # the same freeze rule and the same [camera]/[keys] file the product writes; a
  # probe with its own copy would only prove that two copies agree.
  src/camera_director.cpp
  src/camera_bookmarks.cpp
  src/keybinds.cpp
  src/ini_section.cpp
  # T54: the logs.tf panel. logstf_viewer.cpp is the whole contract that needs
  # no browser -- URL policy, geometry, the state machine, the runtime probe and
  # the frame-time readings -- and it is compiled into logstf_viewer_probe as
  # well, so the gate drives the product's own rules instead of a second copy.
  # logstf_webview.cpp is the WebView2 backend behind ILogsViewHost; it loads
  # WebView2Loader.dll by name at run time, so a host WITHOUT a web-view runtime
  # still starts the player and reports Unavailable (see logstf-check.sh).
  src/logstf_viewer.cpp
  src/logstf_webview.cpp
  # T54: the Win32 half -- the panel's controls, the splitter drag, the child
  # window the browser draws into, and the scripted --logstf-script sequence.
  # main.cpp only forwards six messages, so the panel's diff cannot reach the
  # rest of the player.
  src/logstf_panel.cpp
  # T37: the installer lifecycle (version / install / uninstall) the package was
  # missing. In the product, not only in a probe, because the thing that is
  # installed is the thing that installs: a probe that performed a different
  # install would be a second implementation of the one behaviour this round
  # claims. Both files link bcrypt (already in the link line) for SHA-256.
  src/install_manifest.cpp
  src/install_lifecycle.cpp
  # T66: the SourceTV spectate director -- the death relay, the wall-clock
  # handover and the 'keep the last camera when nobody is alive' rule. Product
  # code, not probe code: the frame loop drives update() and the draw loop asks
  # hidesModel() before it submits an instance, so a gate that ran a second copy
  # would only prove that the copy agrees with itself.
  src/spectate_target.cpp
  # T71/T72: the review workspace store and the bookmark logic. In the PRODUCT
  # target, not only in review_workspace_probe: the session in main.cpp calls them,
  # so a workspace file this build writes has to be the file the probe already
  # verified. A probe-only copy would leave the product's own save path unverified,
  # which is the gap T71 was opened to close.
  src/review_workspace.cpp
  src/review_bookmarks.cpp
  src/review_session.cpp
)

target_include_directories(tf2_demo_native PRIVATE include third_party/lzma)
# T54: the WebView2 SDK headers, vendored under third_party/webview2/include
# (WebView2.h and WebView2EnvironmentOptions.h, with Microsoft's licence beside
# them). Only the backend includes them, so only the targets that compile it
# need this path; the SDK is not a download step and the branch builds offline.
target_include_directories(tf2_demo_native PRIVATE third_party/webview2/include)
target_compile_definitions(tf2_demo_native PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN ${TF2_VERSION_DEFINES})
# msacm32 supplies the ACM stream API audio_resource.cpp uses to turn MPEG
# Layer III resources into PCM; without it the link fails on acmStreamOpen.
target_link_libraries(tf2_demo_native PRIVATE d3d11 dxgi d3dcompiler user32 gdi32 shell32 advapi32 ole32 winmm msacm32 bcrypt psapi comctl32 comdlg32)
