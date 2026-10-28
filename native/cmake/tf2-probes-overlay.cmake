# ---------------------------------------------------------------------------
# T75 (2026-10-28): split out of native/CMakeLists.txt (HEAD db3316e).
#
# the overlays that read per-entity state: SourceTV spectate and projectile track.
#
# The text below is the ORIGINAL lines 392-432 of native/CMakeLists.txt, moved
# verbatim: no target name, source list, link line, compile definition, compile
# option, install rule or cache variable was edited, added or re-ordered.  The
# include() order in ../CMakeLists.txt is the original statement order, so
# target creation order -- which is the NMAKE build order, and therefore which
# targets a failed link truncates -- is unchanged as well.
#
# Targets defined here: spectate_probe projectile_track_probe
# T75-BODY-START
# ---------------------------------------------------------------------------
# T66: the spectate probe. --self-test drives the relay rules with synthetic
# snapshots, so every rule is a named assertion that needs no GPU, no demo and no
# lock; --demo replays a real SourceTV recording through the same class, so the
# gate can assert that the sample's own deaths and handovers happened rather than
# only the synthetic path. entity_model.cpp is linked because the probe reads the
# slot origin with the same resolver the draw loop uses -- a probe-side reader
# would let the two disagree about where the player stands.
add_executable(spectate_probe
  tools/spectate_probe.cpp
  src/spectate_target.cpp
  src/entity_model.cpp
  src/model_loader.cpp
  src/asset_root.cpp
  ${TF2_DEMO_HEADER_SOURCES}
  src/item_schema.cpp
  src/vmt_material.cpp
  src/vtf_texture.cpp)
target_link_libraries(spectate_probe PRIVATE ${TF2_DEMO_HEADER_LIBS})
target_compile_definitions(spectate_probe PRIVATE ${TF2_DEMO_HEADER_DEFINES})
target_include_directories(spectate_probe PRIVATE include third_party/lzma)
set_target_properties(spectate_probe PROPERTIES WIN32_EXECUTABLE OFF)

# T53: the projectile track probe. Links entity_model.cpp because one of its
# assertions is that the trajectory origin reader and the entity resolver agree
# on the same state -- two readers that merely agreed on the easy case would make
# that assertion vacuous, so the probe has to be able to call both.
add_executable(projectile_track_probe
  tools/projectile_track_probe.cpp
  src/projectile_track.cpp
  src/projectile_overlay.cpp
  src/entity_model.cpp
  src/model_loader.cpp
  src/asset_root.cpp
  ${TF2_DEMO_HEADER_SOURCES}
  src/item_schema.cpp
  src/vmt_material.cpp
  src/vtf_texture.cpp)
target_link_libraries(projectile_track_probe PRIVATE ${TF2_DEMO_HEADER_LIBS})
target_compile_definitions(projectile_track_probe PRIVATE ${TF2_DEMO_HEADER_DEFINES})
target_include_directories(projectile_track_probe PRIVATE include third_party/lzma)
set_target_properties(projectile_track_probe PROPERTIES WIN32_EXECUTABLE OFF)
