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
  # N24 phase 1 (2026-11-19): the renderer resize/render self-test moved out of
  # main.cpp as pure code motion -- body verbatim, same trace text and exit
  # codes (docs/N24-MAIN-SPLIT-PLAN-2026-11-16.md section 7).
  src/main_selftest.cpp
  # N24 B2-1 (2026-11-20): the exit-path persistence and teardown tail moved
  # out of wWinMain as pure code motion -- order, trace text and exit codes
  # verbatim (docs/N24-MAIN-SPLIT-PLAN-2026-11-16.md section 7).
  src/main_shutdown.cpp
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
  # N24 B2-2 (2026-11-20): renderer initialize & T51/T67 HUD startup block moved
  # out of wWinMain (main.cpp:8296-8376) as pure code motion -- order verbatim.
  src/main_renderer_hud.cpp
  # T76: the T50 camera pose/bookmark layer.
  src/main_camera.cpp
  # T76 stage 4 (2026-11-13): the T50 camera-script driver (--camera-script).
  # Code motion only: the command line, the trace text and the exit code are
  # unchanged, and the three call sites in main.cpp keep their spelling.
  src/camera_script.cpp
  # T76 stage 2 (2026-11-02): the file, path and byte helpers -- readFile,
  # sha256File, localCacheDirectory, normalizedModelPath, narrowAscii,
  # entitySnapshotStatusName, addSteamRegistryRoots. No captured state and no
  # globals, so they were the cheapest slice to move next.
  src/main_helpers.cpp
  # T76 stage 3 (2026-11-02): the native Win32 control layer (layout, per-frame
  # sync, control creation). windowProc and dispatchHotkey stay in main.cpp.
  src/main_ui.cpp
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
  # T9 (2026-10-16): the map search order, shared with map_lookup_probe. In the
  # product rather than inlined into main.cpp because the gate has to assert the
  # order the product actually walks, not a second copy of it.
  # T15: the loose / VPK-directory / numbered-volume / pakfile search order as
  # one object. In the product because vpk_integrity_probe has to drive the same
  # order the product uses; a probe with its own copy would only prove that two
  # copies agree.
  # The ViewModel material chain, shared with viewmodel_material_probe. In the
  # product rather than inlined into viewmodel.cpp because the probe has to
  # resolve the same table from the same bytes: a gate that asserts the two agree
  # is only non-circular if there is one implementation to agree with.
  # T61: the per-player animation clock. In the product because the probe and
  # the gate drive the same arithmetic the product drives; a copy in the gate
  # would only prove the two copies agree.
  # T70: the request/landing ledger. Pure logic, shared with
  # playback_control_probe so the gate drives the same implementation the
  # product runs rather than a second copy that happens to agree.
  # T51: the custom HUD import. hud_import.cpp is the pure rule set (shared with
  # hud_probe byte for byte, so the probe and the product cannot disagree about
  # what a document means); hud_state.cpp is the [hud] settings round trip.
  src/hud_state.cpp
  # T67: the embedded 8x8 font and its atlas; the HUD text pass samples it.
  src/hud_font.cpp
  # F0 (2026-11-23): DirectWrite-rasterised UI text for the HUD (hud_text.h).
  # Device-free, so minimap_probe --hud-text-test links the same file.
  src/hud_text.cpp
  # E11 (2026-11-17): the map-overview screen. In the product, not in a probe,
  # because the gate has to drive the same compose/geometry code the product
  # draws with; the module owns the panel, rosters, class chips and the world->
  # image affine, and takes the affine from map_basemap.cpp instead of restating
  # demos.tf's mapview.js formula in a second place.
  src/map_overview.cpp
  # H1 (2026-11-23, UI shell design section 7): the broadcast HUD -- the spectator
  # overlay composed from the overview's own rows and the live readout's scores and
  # clock. In the product for the same reason map_overview.cpp is: its inputs live in
  # TUs that link d3d11/dwrite, and the gate drives the same compose the product draws.
  src/broadcast_hud.cpp
  # E11: the demos.tf basemap package reader (boundaries.json + PNG decode) and
  # the product's only image decoder, WIC. PNG because that is what the package
  # ships; WIC because it is part of Windows, so the product gains no library.
  src/map_basemap.cpp
  src/wic_image.cpp
  # T50's shared hotkey/INI layer, copied byte-identical from task/t50-camera.
  # The HUD registers its three actions into the ONE table rather than a second.
  # T52: the shared overlay contract both T52 and T53 implement against.
  # The world material capability layer. In the product, not just in the probe:
  # the product resolves a map's cubemap through the same bspPakfileEntry and
  # worldCubemapVtfPath the probe measures, so "what the gate pins" and "what the
  # product does" are one implementation rather than two that agree by hand.
  # T19: the tile atlas plan and the feature scan. Pure functions, shared with
  # material_coverage_probe: the gate asserts the plan the product made, not a
  # second copy of the placement rule.
  # T24: the lighting-control ledger, shared with lighting_semantics_probe. In
  # the product because the trace and the gate must read one classification:
  # the probe's six rows and the product's six rows are the same function.
  src/texture2d.cpp
  src/settings_store.cpp
  # T50: the camera director, the named bookmark store and the ONE key table.
  # In the product rather than only in the probe because the probe has to drive
  # the same freeze rule and the same [camera]/[keys] file the product writes; a
  # probe with its own copy would only prove that two copies agree.
  # T54: the logs.tf panel. logstf_viewer.cpp is the whole contract that needs
  # no browser -- URL policy, geometry, the state machine, the runtime probe and
  # the frame-time readings -- and it is compiled into logstf_viewer_probe as
  # well, so the gate drives the product's own rules instead of a second copy.
  # logstf_webview.cpp is the WebView2 backend behind ILogsViewHost; it loads
  # WebView2Loader.dll by name at run time, so a host WITHOUT a web-view runtime
  # still starts the player and reports Unavailable (see logstf-check.sh).
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
  # T90: the build-identity stamp. A translation unit generated at BUILD time
  # by the tf2_build_stamp target (native/cmake/gen-build-stamp.cmake), so
  # --version answers with the commit and dirty state of the tree this link was
  # actually made from rather than of the last configure; see tf2-version.cmake.
  ${TF2_BUILD_STAMP_SOURCE}
  # T66: the SourceTV spectate director -- the death relay, the wall-clock
  # handover and the 'keep the last camera when nobody is alive' rule. Product
  # code, not probe code: the frame loop drives update() and the draw loop asks
  # hidesModel() before it submits an instance, so a gate that ran a second copy
  # would only prove that the copy agrees with itself.
  # T71/T72: the review workspace store and the bookmark logic. In the PRODUCT
  # target, not only in review_workspace_probe: the session in main.cpp calls them,
  # so a workspace file this build writes has to be the file the probe already
  # verified. A probe-only copy would leave the product's own save path unverified,
  # which is the gap T71 was opened to close.
  src/review_session.cpp)

# T90: regenerate the build-identity stamp before this target compiles it.
add_dependencies(tf2_demo_native tf2_build_stamp)
# D-54: and refresh <build>/tf2_demo_native.identity after every link of this
# target, so the sidecar always describes the bytes that were just produced.
tf2_identity_sidecar(tf2_demo_native)

target_include_directories(tf2_demo_native PRIVATE include third_party/lzma)
# T54: the WebView2 SDK headers, vendored under third_party/webview2/include
# (WebView2.h and WebView2EnvironmentOptions.h, with Microsoft's licence beside
# them). Only the backend includes them, so only the targets that compile it
# need this path; the SDK is not a download step and the branch builds offline.
target_include_directories(tf2_demo_native PRIVATE third_party/webview2/include)
target_compile_definitions(tf2_demo_native PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN ${TF2_VERSION_DEFINES})
# msacm32 supplies the ACM stream API audio_resource.cpp uses to turn MPEG
# Layer III resources into PCM; without it the link fails on acmStreamOpen.
target_link_libraries(tf2_demo_native PRIVATE tf2_core d3d11 dxgi d3dcompiler user32 gdi32 shell32 advapi32 ole32 winmm msacm32 bcrypt psapi comctl32 comdlg32 windowscodecs dwrite)
