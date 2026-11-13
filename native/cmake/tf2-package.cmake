# ---------------------------------------------------------------------------
# T75 (2026-10-28): split out of native/CMakeLists.txt (HEAD db3316e).
#
# install rules, package payload, WebView2 loader staging, compiler options.
#
# The text below is the ORIGINAL lines 818-891 of native/CMakeLists.txt, moved
# verbatim: no target name, source list, link line, compile definition, compile
# option, install rule or cache variable was edited, added or re-ordered.  The
# include() order in ../CMakeLists.txt is the original statement order, so
# target creation order -- which is the NMAKE build order, and therefore which
# targets a failed link truncates -- is unchanged as well.
#
# Targets defined here: -
# T75-BODY-START
# ---------------------------------------------------------------------------
# T37: the install rule now lays down the LIFECYCLE, not only the executables.
#
#   the package payload   <prefix>/.uninstall/payload/ -- exactly the files
#                         `--install` will copy into the directory
#   the installer         <prefix>/tf2_demo_native.exe -- already staged, so the
#                         directory can install ITSELF with no external tool and
#                         no network
#   the version file      <prefix>/VERSION.txt -- what `--version` reads first
#
# The payload is staged by `cmake --install` itself rather than by a packaging
# step, because the rule already knows the exact file list and sizes T32 measured.
# It is deliberately NOT a copy of the developer's whole build directory: only
# named targets and named program files are listed, so a stray artefact cannot
# end up in a release.
install(TARGETS tf2_demo_native native_install_smoke_probe
  RUNTIME DESTINATION .
)
install(PROGRAMS tools/install_smoke_probe.ps1 DESTINATION .)
# T54: the WebView2 loader, and ONLY the loader. The browser itself is the
# Evergreen runtime Windows already carries (see the logstf: runtime line the
# product traces), so the package ships 159 KB rather than a browser: no
# fixed-version runtime is staged. A host without the runtime still installs and
# runs; the panel then reports Unavailable with the reason and the fallback.
if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/webview2/x64/WebView2Loader.dll")
  install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/third_party/webview2/x64/WebView2Loader.dll"
    DESTINATION .)
  install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/third_party/webview2/x64/WebView2Loader.dll"
    DESTINATION .uninstall/payload)
  # The loader is redistributed as a BINARY, so the SDK's own licence and its
  # third-party notices travel with the package: BSD-3-Clause clause 2 requires
  # the copyright notice, the conditions and the disclaimer in the documentation
  # and/or other materials accompanying a binary redistribution. They are
  # installed as documentation next to the package, NOT into .uninstall/payload:
  # the payload is the set of files the installer lays down and owns (and its
  # count is asserted elsewhere), while a licence is read, not installed.
  install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/third_party/webview2/LICENSE.txt"
                "${CMAKE_CURRENT_SOURCE_DIR}/third_party/webview2/NOTICE.txt"
    DESTINATION third_party/webview2)
endif()
# The payload uses $<TARGET_FILE:...> rather than a second install(TARGETS) so
# each artefact has exactly one install rule that names it; two rules for one
# target is how the "which files does the package contain" answer stops being
# readable from this file.
install(FILES "$<TARGET_FILE:tf2_demo_native>"
              "$<TARGET_FILE:native_install_smoke_probe>"
  DESTINATION .uninstall/payload)
# D-54: the identity sidecar travels with the executable, in the build tree, the
# package root and the payload. Without it the installed product reports
# identity=missing and unknown/-1 -- technically honest, but the release would
# have less to say about itself than the build directory it came from. It is one
# more file in the payload, so the file counts asserted by
# install-lifecycle-check.sh (payload / installed files) move by one when this
# lands: the counts are readings, and this is the change that moves them.
install(FILES "${CMAKE_BINARY_DIR}/tf2_demo_native.identity"
  DESTINATION .)
install(FILES "${CMAKE_BINARY_DIR}/tf2_demo_native.identity"
  DESTINATION .uninstall/payload)
install(PROGRAMS tools/install_smoke_probe.ps1 DESTINATION .uninstall/payload)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/../VERSION.txt" DESTINATION . OPTIONAL)

# T54: the loader must sit beside the executable the build tree produces, not
# only in the package, or a build-tree run reports loader=missing and the panel
# half of logstf-check.sh would be skipped on the very machine that can run it.
if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/webview2/x64/WebView2Loader.dll")
  add_custom_command(TARGET tf2_demo_native POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CMAKE_CURRENT_SOURCE_DIR}/third_party/webview2/x64/WebView2Loader.dll"
      "$<TARGET_FILE_DIR:tf2_demo_native>/WebView2Loader.dll"
    COMMENT "Copying WebView2Loader.dll next to tf2_demo_native.exe")
endif()

if (MSVC)
  target_compile_options(tf2_demo_native PRIVATE /W4 /permissive- /utf-8)
  target_compile_options(entity_protocol_probe PRIVATE /W4 /permissive- /EHsc /utf-8)
  target_compile_options(entity_message_fixture_probe PRIVATE /W4 /permissive- /EHsc /utf-8)
  # T51: the probe is held to the product's warning bar, because a probe is read as
  # evidence and evidence built at /W3 is not the same evidence.
  target_compile_options(hud_probe PRIVATE /W4 /permissive- /EHsc /utf-8)
  # T69: same bar as hud_probe, for the same reason.
  target_compile_options(minimap_probe PRIVATE /W4 /permissive- /EHsc /utf-8)
  # T71/T72: evidence at the product's warning bar.
  target_compile_options(review_workspace_probe PRIVATE /W4 /permissive- /EHsc /utf-8)
  target_link_options(tf2_demo_native PRIVATE /DYNAMICBASE /NXCOMPAT)
endif()
