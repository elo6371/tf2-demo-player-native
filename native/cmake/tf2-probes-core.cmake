# ---------------------------------------------------------------------------
# T75 (2026-10-28): split out of native/CMakeLists.txt (HEAD db3316e).
#
# item schema, overlay contract, temp effects/fixtures, demo open.
#
# The text below is the ORIGINAL lines 205-235 of native/CMakeLists.txt, moved
# verbatim: no target name, source list, link line, compile definition, compile
# option, install rule or cache variable was edited, added or re-ordered.  The
# include() order in ../CMakeLists.txt is the original statement order, so
# target creation order -- which is the NMAKE build order, and therefore which
# targets a failed link truncates -- is unchanged as well.
#
# Targets defined here: item_schema_probe entity_overlay_probe temp_effect_probe temp_entities_fixture_probe demo_open_probe
# T75-BODY-START
# ---------------------------------------------------------------------------
add_executable(item_schema_probe tools/item_schema_probe.cpp src/item_schema.cpp src/proto_wire.cpp)

# T52: the overlay contract's own probe. Links the contract implementation and
# the demo reader but NOT the renderer: every assertion it makes is about a
# value (entity identity, team, pose, pass order, pixel predicate), and a probe
# that needed a device could not run in the gate's cheapest tier.
add_executable(entity_overlay_probe
  tools/entity_overlay_probe.cpp
  src/entity_overlay.cpp
  src/entity_model.cpp
  ${TF2_DEMO_HEADER_SOURCES})
target_include_directories(entity_overlay_probe PRIVATE include third_party/lzma)
target_link_libraries(entity_overlay_probe PRIVATE ${TF2_DEMO_HEADER_LIBS})
target_compile_definitions(entity_overlay_probe PRIVATE ${TF2_DEMO_HEADER_DEFINES})
set_target_properties(entity_overlay_probe PROPERTIES WIN32_EXECUTABLE OFF)

target_include_directories(item_schema_probe PRIVATE include)
set_target_properties(item_schema_probe PROPERTIES WIN32_EXECUTABLE OFF)

add_executable(temp_effect_probe tools/temp_effect_probe.cpp src/effects_timeline.cpp)
target_include_directories(temp_effect_probe PRIVATE include)
set_target_properties(temp_effect_probe PROPERTIES WIN32_EXECUTABLE OFF)

add_executable(temp_entities_fixture_probe tools/temp_entities_fixture_probe.cpp)
set_target_properties(temp_entities_fixture_probe PROPERTIES WIN32_EXECUTABLE OFF)

add_executable(demo_open_probe tools/demo_open_probe.cpp ${TF2_DEMO_HEADER_SOURCES} src/proto_wire.cpp src/item_schema.cpp)
target_include_directories(demo_open_probe PRIVATE include third_party/lzma)
target_link_libraries(demo_open_probe PRIVATE ${TF2_DEMO_HEADER_LIBS})
target_compile_definitions(demo_open_probe PRIVATE ${TF2_DEMO_HEADER_DEFINES})
set_target_properties(demo_open_probe PROPERTIES WIN32_EXECUTABLE OFF)

# D-14 follow-up (2026-11-09): asks the demo whether a brush entity's "*N" dmodel
# actually moves, so "skippedMoving=84" can be read as 84 doors or 84 trains
# rather than taken on trust. Appended last on purpose: the T75 note at the top of
# this file records that target creation order is NMAKE build order, and a failed
# link truncates whatever follows it, so a new target goes at the end.
add_executable(brush_entity_probe tools/brush_entity_probe.cpp ${TF2_DEMO_HEADER_SOURCES})
target_include_directories(brush_entity_probe PRIVATE include third_party/lzma)
target_link_libraries(brush_entity_probe PRIVATE ${TF2_DEMO_HEADER_LIBS})
target_compile_definitions(brush_entity_probe PRIVATE ${TF2_DEMO_HEADER_DEFINES})
set_target_properties(brush_entity_probe PROPERTIES WIN32_EXECUTABLE OFF)
