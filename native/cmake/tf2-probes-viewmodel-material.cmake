# ---------------------------------------------------------------------------
# T75 (2026-10-28): split out of native/CMakeLists.txt (HEAD db3316e).
#
# the ViewModel material resource chain (phase 1).
#
# The text below is the ORIGINAL lines 741-760 of native/CMakeLists.txt, moved
# verbatim: no target name, source list, link line, compile definition, compile
# option, install rule or cache variable was edited, added or re-ordered.  The
# include() order in ../CMakeLists.txt is the original statement order, so
# target creation order -- which is the NMAKE build order, and therefore which
# targets a failed link truncates -- is unchanged as well.
#
# Targets defined here: viewmodel_material_probe
# T75-BODY-START
# ---------------------------------------------------------------------------
# The ViewModel material resource chain, phase 1: MDL texture slot ->
# $cdmaterials -> .vmt -> $basetexture -> .vtf -> decoded pixels, for the three
# ViewModel model families (dynamic c_*_arms, old v_*, static weapon bodies).
# It links the product's own parsers and asset root rather than a second copy of
# either, draws nothing, and is gated by viewmodel-material-check.sh. The
# product still flat-shades the ViewModel: nothing here is wired into a pass.
add_executable(viewmodel_material_probe
  tools/viewmodel_material_probe.cpp
  src/model_loader.cpp
  src/asset_root.cpp
  src/vmt_material.cpp
  src/vtf_texture.cpp
  src/viewmodel_material.cpp
  ${TF2_DEMO_HEADER_SOURCES}
  src/item_schema.cpp
  src/proto_wire.cpp)
target_include_directories(viewmodel_material_probe PRIVATE include third_party/lzma)
target_link_libraries(viewmodel_material_probe PRIVATE ${TF2_DEMO_HEADER_LIBS})
target_compile_definitions(viewmodel_material_probe PRIVATE ${TF2_DEMO_HEADER_DEFINES})
set_target_properties(viewmodel_material_probe PROPERTIES WIN32_EXECUTABLE OFF)
