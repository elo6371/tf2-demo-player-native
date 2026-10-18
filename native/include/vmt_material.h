#pragma once

#include <string>
#include <unordered_map>
#include <vector>

namespace tf2::native {

struct VmtValue {
  bool object = false;
  std::string text;
  std::unordered_map<std::string, VmtValue> fields;
};

struct VmtMaterial {
  std::string shader;
  std::unordered_map<std::string, std::string> fields;
  std::string baseTexture;
  std::string baseTexture2;
  // T12: `$blendmodulatetexture` is the mask that reshapes the vertex-alpha
  // blend between `$basetexture` and `$basetexture2`. In the SDK's
  // LightmappedGeneric pixel shader it is read as `smoothstep(g - r, g + r,
  // vertexAlpha)`, so a material that declares one and is drawn without it gets
  // a hard alpha cut where the map author asked for a soft transition.
  std::string blendModulateTexture;
  std::string bumpMap;
  std::string envMap;
  std::string selfIllumTint;
  std::string phongTint;
  std::string waterBottomMaterial;
  std::string underwaterOverlay;
  bool phong = false;
  bool selfIllum = false;
  bool translucent = false;
  bool alphaTest = false;
  bool noCull = false;
  bool aboveWater = false;
  bool waterShader = false;
  bool valid = false;
  std::string error;
};

class VmtParser {
public:
  static bool parse(const std::string& text, VmtMaterial& material);
  static std::string resourcePath(const std::string& materialName, const char* extension);
};

} // namespace tf2::native
