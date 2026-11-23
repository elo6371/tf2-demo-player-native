# ---------------------------------------------------------------------------
# D-54 (2026-11-10): the identity refresh, as a target of its own.
#
# The sidecar beside each executable is refreshed from two places, because
# neither alone covers the case the defect needs covered:
#
#   POST_BUILD of the target (tf2-version.cmake)   when the bytes changed, their
#       sidecar is rewritten by the same build step that produced them.
#   this target, which is ALL and depends on both executables   when NOTHING
#       relinked but the provenance changed -- a new commit, a file turning
#       dirty, the clock moving past a fixup -- the sidecar still has to be
#       refreshed, or --version would keep naming the tree of the previous
#       build. That is T90's defect in its present form, and it is invisible to
#       a POST_BUILD rule because no link happened.
#
# It is a named target rather than part of tf2_build_stamp because it reads the
# executables: the stamp generator has to run BEFORE the compile, this has to
# run AFTER the link. One target cannot sit on both sides of that edge.
#
# WHAT A BUILD THAT NAMES TARGETS MUST DO
#   "cmake --build --target tf2_demo_native" builds neither this target nor any
#   other ALL target, so an incremental build that names its target gets a
#   refreshed sidecar only when it relinked. build-identity-check.sh therefore
#   asks for tf2_identity explicitly (see build_targets() there); a full build
#   (build-cmake.sh, no --target) gets it for free.
# ---------------------------------------------------------------------------
set(TF2_IDENTITY_SIDECAR_SCRIPT "${CMAKE_CURRENT_LIST_DIR}/write-identity-sidecar.cmake")

# The commands spell each target out rather than looping over a list, because
# $<TARGET_FILE:...> is only valid for a target that exists in this directory
# scope, and the two executables that compile install_lifecycle.cpp are exactly
# these two (tf2-product.cmake, tf2-probes-install-cli.cmake).
#
# add_dependencies makes the executable build first; COMMAND (not POST_BUILD)
# makes the sidecar refresh part of this target's own recipe, which is what an
# "ALL" target needs.
add_custom_target(tf2_identity ALL
  COMMAND "${CMAKE_COMMAND}"
    "-DTF2_PROVENANCE=${TF2_BUILD_STAMP_PROVENANCE}"
    "-DTF2_STAMP_SOURCE=${TF2_BUILD_STAMP_OUTPUT}"
    "-DTF2_EXE=$<TARGET_FILE:tf2_demo_native>"
    "-DTF2_TARGET=tf2_demo_native"
    "-DTF2_OUTPUT=${CMAKE_BINARY_DIR}/tf2_demo_native.identity"
    -P "${TF2_IDENTITY_SIDECAR_SCRIPT}"
  COMMAND "${CMAKE_COMMAND}"
    "-DTF2_PROVENANCE=${TF2_BUILD_STAMP_PROVENANCE}"
    "-DTF2_STAMP_SOURCE=${TF2_BUILD_STAMP_OUTPUT}"
    "-DTF2_EXE=$<TARGET_FILE:install_cli_probe>"
    "-DTF2_TARGET=install_cli_probe"
    "-DTF2_OUTPUT=${CMAKE_BINARY_DIR}/install_cli_probe.identity"
    -P "${TF2_IDENTITY_SIDECAR_SCRIPT}"
  COMMENT "build identity: refresh the sidecars beside tf2_demo_native.exe and install_cli_probe.exe"
  VERBATIM)
add_dependencies(tf2_identity tf2_demo_native install_cli_probe)
