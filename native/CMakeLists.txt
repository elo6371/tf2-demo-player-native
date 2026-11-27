cmake_minimum_required(VERSION 3.24)
project(tf2_demo_native LANGUAGES CXX C)

# T75 (2026-10-28): this file is the build ENTRY POINT only. The build graph now
# lives in native/cmake/, one module per domain, and the include() order below is
# the original statement order of the single-file script (HEAD db3316e): the
# split moved text into files, it did not re-order a statement.
#
# The order is not cosmetic. CMake creates targets in the order it reads them,
# NMAKE builds them in that order, and comments in the modules rely on the
# consequence (see tf2-probes-viewmodel.cmake: a link dependency left out of
# viewmodel_fov_probe stops NMAKE there, "and every target after this one is
# silently not built"). Re-ordering the includes would re-order the build and
# silently invalidate that reading, so the modules are contiguous slices of the
# original file rather than a free grouping by subject: where the original
# interleaved two domains, the split follows the interleaving and a domain is
# simply split into two modules. That is also why a module is sometimes one
# target -- the alternative was to move a target past another domain's target,
# which is a build-order change dressed up as tidiness.
#
# Layout (details and the original line ranges are in each module's header):
#   tf2-settings.cmake         global settings, the demo-header source/link/define contract
#   tf2-version.cmake          package identity: version, build stamp, source tree
#   tf2-libraries.cmake        the shared static library tf2_core
#   tf2-product.cmake          the product target tf2_demo_native
#   tf2-probes-*.cmake         the 52 probe / query executables, in original order
#   tf2-package.cmake          install rules, package payload, compiler options
#
# Adding a probe: put it in the module that owns its domain and leave this list
# alone. Adding a domain: add the module AND its include() in the position that
# keeps the build order.
include(cmake/tf2-settings.cmake)
include(cmake/tf2-version.cmake)
# T85-E2 (2026-11-13): the shared static library. It must be included after the
# version module (its members compile with ${TF2_VERSION_DEFINES}, defined there)
# and before every module whose targets link it.
include(cmake/tf2-libraries.cmake)
include(cmake/tf2-product.cmake)
include(cmake/tf2-probes-logstf.cmake)
include(cmake/tf2-probes-core.cmake)
include(cmake/tf2-probes-archive.cmake)
include(cmake/tf2-probes-world.cmake)
include(cmake/tf2-probes-presentation.cmake)
include(cmake/tf2-probes-model.cmake)
include(cmake/tf2-probes-overlay.cmake)
include(cmake/tf2-probes-viewmodel.cmake)
include(cmake/tf2-probes-animation.cmake)
include(cmake/tf2-probes-audio.cmake)
include(cmake/tf2-probes-runtime.cmake)
include(cmake/tf2-probes-playback.cmake)
include(cmake/tf2-probes-protocol.cmake)
include(cmake/tf2-probes-quiet-capture.cmake)
include(cmake/tf2-probes-viewmodel-material.cmake)
include(cmake/tf2-probes-hud.cmake)
include(cmake/tf2-probes-install-cli.cmake)
include(cmake/tf2-probes-minimap.cmake)
include(cmake/tf2-probes-review.cmake)
# N27 (2026-11-24): the offline export tool, last of the probes because it is the
# newest domain and nothing downstream links it.
include(cmake/tf2-probes-export.cmake)
include(cmake/tf2-package.cmake)
# D-54: the identity refresh target, last because it depends on both executables
# that compile install_lifecycle.cpp and has to be created after them.
include(cmake/tf2-identity.cmake)

# ---------------------------------------------------------------------------
# N23 (2026-11-18): /W4 for every target, not seven of fifty-five.
#
# docs/arch-ci-2026-11-14/N23-WARNING-COVERAGE.md measured 7 of 55 targets on
# /W4 and the other 48 on MSVC's default warning level, which is /W1: CMake
# stopped adding /W3 when CMP0092 went NEW, so a missing /W flag is not the old
# default. The mechanism was a per-feature whitelist (each entry's comment says
# that probe's evidence must be read at the product's warning level), so the
# 49th target -- the next probe someone adds -- silently landed at /W1 and
# nothing failed. A warning gate over 7 targets cannot see the other 48.
#
# This block makes /W4 the default for every executable and static library in
# this directory, and keeps an explicit, recorded way to hold one target at /W3.
# The target list is read from the directory itself instead of a hand-copied
# list of 55 names: a copy drifts, and drift is what produced 7/55.
#
# Read the flag, not this comment: native/build-nmake/CMakeFiles/<t>.dir/flags.make
# now says /W4 for all 55. Measured 2026-11-18; the before/after function list is
# in docs/N23-WALL-LEVEL-2026-11-18.md.
# ---------------------------------------------------------------------------
if (MSVC)
  # A target that cannot be cleaned yet goes here BY NAME, so the exception is
  # visible in the record instead of being an omission. Empty as of 2026-11-18.
  set(TF2_W3_TARGETS "" CACHE STRING
    "Targets pinned to /W3 instead of the tree-wide /W4 (each needs a reason).")
  # /WX is on for the seven targets that were already /W4 before this flip, so
  # they have a measured history of compiling clean there: the product, the core
  # library and the five protocol/review probes named below. The other forty-eight
  # stay at /W4 only, and that is a decision with a reason: most of them are
  # mutation-gate probes, and several gates deliberately plant a suspicious
  # construct in a probe to prove the check can go red -- /WX would turn a
  # planted warning into a build failure and hide the assertion under test.
  # Adding a target here is a recorded decision, not a default.
  set(TF2_WX_DEFAULT_TARGETS
    tf2_core
    tf2_demo_native
    entity_protocol_probe
    entity_message_fixture_probe
    hud_probe
    minimap_probe
    review_workspace_probe)
  # Extra targets by name, without editing this file (e.g. -DTF2_WX_TARGETS=...).
  set(TF2_WX_TARGETS "" CACHE STRING
    "Extra targets compiled with /WX on top of TF2_WX_DEFAULT_TARGETS.")
  get_property(tf2_n23_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
  foreach(tf2_n23_t IN LISTS tf2_n23_targets)
    get_target_property(tf2_n23_type ${tf2_n23_t} TYPE)
    if (tf2_n23_type STREQUAL "UTILITY" OR tf2_n23_type STREQUAL "INTERFACE_LIBRARY")
      continue()
    endif()
    if (tf2_n23_t IN_LIST TF2_W3_TARGETS)
      target_compile_options(${tf2_n23_t} PRIVATE /W3)
    else()
      target_compile_options(${tf2_n23_t} PRIVATE /W4)
    endif()
    # getenv/fopen/swscanf are called on purpose across this tree: the product
    # reads environment knobs on its diagnostic paths and the probes dump files.
    # C4996 is a MSVC CRT dialect, not a defect, and this repository has exactly
    # one build chain (MSVC, Win32, D3D11 -- build-target.sh and the CMake
    # presets), so the portable alternative the warning proposes (the _s
    # variants) would bind the code to MSVC while buying no portability. The N23
    # report (section 4.3) reached that conclusion for the probes; the product
    # and tf2_core get the same define so the dialect is recorded once here
    # instead of as one pragma per file. Nothing else is silenced: a C4996 in a
    # TU without this define is still a finding, which is why the warning gate
    # has to stay at zero with the define in place.
    target_compile_definitions(${tf2_n23_t} PRIVATE _CRT_SECURE_NO_WARNINGS)
    if (tf2_n23_t IN_LIST TF2_WX_DEFAULT_TARGETS OR tf2_n23_t IN_LIST TF2_WX_TARGETS)
      target_compile_options(${tf2_n23_t} PRIVATE /WX)
    endif()
  endforeach()
endif()

