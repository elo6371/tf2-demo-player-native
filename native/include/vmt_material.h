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
  // T19: the keys the world pass can see but cannot yet draw. They are parsed
  // rather than ignored because "this material declares a bump map" and "this
  // material's bump map was not fetched" are different findings, and the second
  // one is only visible if the first one is a field.
  std::string bumpMap2;       // `$bumpmap2`, layer 2's normal map
  bool ssBump = false;        // `$ssbump`, the bump map is in the SSBump format
  std::string detailTexture;  // `$detail`, another material used as a detail layer
  // T27: the three keys that decide how the detail layer is composited.
  // `$detailblendmode` is the TCOMBINE_* index TextureCombine switches on
  // (common_ps_fxc.h), `$detailblendfactor` is its `fBlendFactor`, and
  // `$detailscale` is the uv multiplier the SDK applies in the vertex stage.
  // Parsed even when the layer cannot be drawn: "declares mode 8" and "draws
  // mode 0" are different findings and only the first one is visible in the VMT.
  int detailBlendMode = 0;
  float detailBlendFactor = 1.0f;
  float detailScale = 1.0f;
  // T27: `$detailblendmode` names a TCOMBINE_* branch this pass has no inputs
  // for (4 and up: base alpha, the two post-lighting self-illum modes, the
  // two-pattern select, the detail-alpha mask and the two SSBump combos). The
  // material is refused by name rather than drawn as if it were mode 0.
  bool detailBlendModeUnsupported = false;
  // This VMT's own `%detailtype`, i.e. the value read when this material is the
  // one another material's `$detail` points at. Empty when it declares none.
  // Kept as the raw text: the shipped values are "0" and "2", and a mapping
  // invented here would be a second copy of a rule the pass does not implement.
  std::string detailType;
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
