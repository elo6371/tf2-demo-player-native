# ---------------------------------------------------------------------------
# T75 (2026-10-28): split out of native/CMakeLists.txt (HEAD db3316e).
#
# the installer lifecycle CLI and the VPK readiness probe.
#
# The text below is the ORIGINAL lines 772-792 of native/CMakeLists.txt, moved
# verbatim: no target name, source list, link line, compile definition, compile
# option, install rule or cache variable was edited, added or re-ordered.  The
# include() order in ../CMakeLists.txt is the original statement order, so
# target creation order -- which is the NMAKE build order, and therefore which
# targets a failed link truncates -- is unchanged as well.
#
# Targets defined here: install_cli_probe vpk_readiness_probe
# T75-BODY-START
# ---------------------------------------------------------------------------
# T37: the lifecycle driver. It links the product's own installer code rather than
# a copy of it, so a gate that drives this probe is driving the bytes the product
# runs. The probe exists because a package is "these files staged next to the
# installer": the gate stages them, the probe performs the install, and neither
# half has to guess what the other meant.
add_executable(install_cli_probe tools/install_cli_probe.cpp
  src/install_lifecycle.cpp src/install_manifest.cpp
  # T90: the build-identity stamp, generated at build time (tf2-version.cmake).
  # The probe reports --version too, and install-lifecycle-check.sh asserts that
  # the probe and the product agree on it, so both sides must compile the same
  # generated constants.
  ${TF2_BUILD_STAMP_SOURCE})
# T90: generate the stamp before this target compiles it.
add_dependencies(install_cli_probe tf2_build_stamp)
# D-54: its own sidecar, named after this executable. install-lifecycle-check.sh
# compares the probe's --version with the product's, and both now read their
# provenance from beside themselves, so the two cannot silently share one file.
tf2_identity_sidecar(install_cli_probe)

# T37: the VPK readiness probe. vpk_query answers "OPEN FAILED" and stops, which
# conflates absent, unreadable and refused; this one reads the header
# independently of the product reader and reports the numbered volumes the tree
# names, so a directory index whose volumes are missing is not read as "the
# archive opened".
add_executable(vpk_readiness_probe tools/vpk_readiness_probe.cpp src/vpk_archive.cpp)
target_include_directories(vpk_readiness_probe PRIVATE include)
target_compile_definitions(vpk_readiness_probe PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
set_target_properties(vpk_readiness_probe PROPERTIES WIN32_EXECUTABLE OFF)
target_include_directories(install_cli_probe PRIVATE include)
target_compile_definitions(install_cli_probe PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN ${TF2_VERSION_DEFINES})
target_link_libraries(install_cli_probe PRIVATE bcrypt)
set_target_properties(install_cli_probe PROPERTIES WIN32_EXECUTABLE OFF)
