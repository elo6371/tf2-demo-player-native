# ---------------------------------------------------------------------------
# T75 (2026-10-28): split out of native/CMakeLists.txt (HEAD db3316e).
#
# entity protocol census and the synthetic wire fixtures.
#
# The text below is the ORIGINAL lines 708-731 of native/CMakeLists.txt, moved
# verbatim: no target name, source list, link line, compile definition, compile
# option, install rule or cache variable was edited, added or re-ordered.  The
# include() order in ../CMakeLists.txt is the original statement order, so
# target creation order -- which is the NMAKE build order, and therefore which
# targets a failed link truncates -- is unchanged as well.
#
# Targets defined here: entity_protocol_probe entity_message_fixture_probe
# T75-BODY-START
# ---------------------------------------------------------------------------
# P0 entity protocol census probe. Deliberately compiled with the same strict
# flags as the product target (/W4 /permissive-) so a header change that would
# break the product cannot hide behind a lenient probe build.
add_executable(entity_protocol_probe
  tools/entity_protocol_probe.cpp
  ${TF2_DEMO_HEADER_SOURCES}
  src/proto_wire.cpp
)
target_include_directories(entity_protocol_probe PRIVATE include)
target_link_libraries(entity_protocol_probe PRIVATE ${TF2_DEMO_HEADER_LIBS})
target_compile_definitions(entity_protocol_probe PRIVATE ${TF2_DEMO_HEADER_DEFINES})
set_target_properties(entity_protocol_probe PROPERTIES WIN32_EXECUTABLE OFF)

# Synthetic wire fixtures for the message decoders. Same sources and same
# strictness as the product target so a layout regression cannot hide here.
add_executable(entity_message_fixture_probe
  tools/entity_message_fixture_probe.cpp
  ${TF2_DEMO_HEADER_SOURCES}
  src/proto_wire.cpp
)
target_include_directories(entity_message_fixture_probe PRIVATE include)
target_link_libraries(entity_message_fixture_probe PRIVATE ${TF2_DEMO_HEADER_LIBS})
target_compile_definitions(entity_message_fixture_probe PRIVATE ${TF2_DEMO_HEADER_DEFINES})
set_target_properties(entity_message_fixture_probe PROPERTIES WIN32_EXECUTABLE OFF)
