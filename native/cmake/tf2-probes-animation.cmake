# ---------------------------------------------------------------------------
# T75 (2026-10-28): split out of native/CMakeLists.txt (HEAD db3316e).
#
# skinning contract and weapon animation.
#
# The text below is the ORIGINAL lines 560-593 of native/CMakeLists.txt, moved
# verbatim: no target name, source list, link line, compile definition, compile
# option, install rule or cache variable was edited, added or re-ordered.  The
# include() order in ../CMakeLists.txt is the original statement order, so
# target creation order -- which is the NMAKE build order, and therefore which
# targets a failed link truncates -- is unchanged as well.
#
# Targets defined here: skeleton_skin_probe weapon_animation_probe
# T75-BODY-START
# ---------------------------------------------------------------------------
# Proves the skinning contract (poseToBone is the inverse bind; skin is
# animWorld * poseToBone and identity at bind pose) against the game's own
# files, before the product wires any of it up.
add_executable(skeleton_skin_probe
  tools/skeleton_skin_probe.cpp
  src/animation_decoder.cpp
  src/model_loader.cpp
  src/asset_root.cpp
  src/item_schema.cpp
  src/proto_wire.cpp
  ${TF2_DEMO_HEADER_SOURCES})
target_include_directories(skeleton_skin_probe PRIVATE include third_party/lzma)
target_compile_definitions(skeleton_skin_probe PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(skeleton_skin_probe PRIVATE ${TF2_DEMO_HEADER_LIBS})
target_compile_definitions(skeleton_skin_probe PRIVATE ${TF2_DEMO_HEADER_DEFINES})
set_target_properties(skeleton_skin_probe PROPERTIES WIN32_EXECUTABLE OFF)

# The half skeleton_skin_probe does not cover: an entity's own m_nSequence, read
# out of a real demo, turned into the matrices the product would upload. Links the
# demo reader as well as the animation decoder, because the sequence value comes
# from the wire, not from the model file.
add_executable(weapon_animation_probe
  tools/weapon_animation_probe.cpp
  src/animation_decoder.cpp
  src/model_loader.cpp
  src/asset_root.cpp
  src/item_schema.cpp
  src/proto_wire.cpp
  ${TF2_DEMO_HEADER_SOURCES})
target_include_directories(weapon_animation_probe PRIVATE include third_party/lzma)
target_compile_definitions(weapon_animation_probe PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(weapon_animation_probe PRIVATE ${TF2_DEMO_HEADER_LIBS})
target_compile_definitions(weapon_animation_probe PRIVATE ${TF2_DEMO_HEADER_DEFINES})
set_target_properties(weapon_animation_probe PROPERTIES WIN32_EXECUTABLE OFF)
