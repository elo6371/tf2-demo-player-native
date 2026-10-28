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
#   tf2-product.cmake          the product target tf2_demo_native
#   tf2-probes-*.cmake         the 52 probe / query executables, in original order
#   tf2-package.cmake          install rules, package payload, compiler options
#
# Adding a probe: put it in the module that owns its domain and leave this list
# alone. Adding a domain: add the module AND its include() in the position that
# keeps the build order.
include(cmake/tf2-settings.cmake)
include(cmake/tf2-version.cmake)
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
include(cmake/tf2-package.cmake)
