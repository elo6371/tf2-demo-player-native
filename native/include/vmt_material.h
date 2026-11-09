#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace tf2::native {

struct VmtValue {
  bool object = false;
  std::string text;
  std::unordered_map<std::string, VmtValue> fields;
};

// T59: what a patch VMT asked for and what happened to it. A patch file is the
// map's per-envmap wrapper -- header patch, an include naming another material,
// and a replace block whose keys override the include's. Before this existed the
// parser read the wrapper's own root fields, found no $basetexture, and every
// patched material was refused as no-basetexture and drawn flat: 17 materials on
// pl_odyssey, every one resolvable, because the include was a real
// LightMappedGeneric VMT in tf2_misc_dir.vpk with a real VTF.
struct VmtPatchReading {
  bool declared = false;                   // the file's header was patch
  std::string includePath;                 // the include value as written
  std::string includeResource;             // resolved to materials/....vmt
  bool resolved = false;                   // the include parsed and its fields were merged
  std::vector<std::string> replaced;       // scalar keys the replace block overrode
  std::vector<std::string> objectBlocks;   // object-valued blocks, named not merged
};

struct VmtMaterial {
  std::string shader;
  std::unordered_map<std::string, std::string> fields;
  std::string baseTexture;
  // Some TF2 shaders (EyeRefract) declare their base texture only inside a
  // nested DX8 fallback block, e.g. "Eyes_dx8" { "$basetexture" ... }, and
  // leave the root key empty. Kept as a separate field so the world pass still
  // reads exactly the root key it read before; the model material resolver
  // follows the fallback when the root key is absent.
  std::string fallbackBaseTexture;
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
  // T59: the wrapper this material came from, when it came from one.
  VmtPatchReading patch;
  // T59: $basetexturetransform is not the identity, so the pass cannot put the
  // texture where TF2 would. Refused by name rather than drawn axis-aligned.
  bool baseTextureTransform = false;
  std::string baseTextureTransformText;
  // R2: the same declaration, parsed into the five numbers the uv walk needs.
  // A transform this parser CAN read is no longer a refusal: the world pass
  // applies it in the vertex stage instead of giving the face a flat shade.
  // `baseTextureTransformParsed` false means the declaration was an object
  // block or a word this parser does not understand -- that stays a named
  // refusal, because guessing the identity is how a rotated texture gets drawn
  // axis-aligned without anyone noticing (the T59 rule).
  bool baseTextureTransformParsed = false;
  float baseTextureTransformCenter[2] = {0.5f, 0.5f};
  float baseTextureTransformScale[2] = {1.0f, 1.0f};
  // Degrees. Source documents a positive `rotate` as turning the texture
  // counter-clockwise; see the matrix note in vmt_material.cpp.
  float baseTextureTransformRotate = 0.0f;
  float baseTextureTransformTranslate[2] = {0.0f, 0.0f};
  bool valid = false;
  std::string error;
};

class VmtParser {
public:
  // T59: the store a patch VMT's include is read from. An empty reader means
  // no store was given, and a patch is then refused by name instead of silently
  // falling back to the wrapper's own root -- which is exactly the defect this
  // exists to remove. Bytes are empty when the path does not resolve.
  using IncludeReader = std::function<std::vector<std::uint8_t>(const std::string&)>;
  static bool parse(const std::string& text, VmtMaterial& material);
  static bool parse(const std::string& text, VmtMaterial& material,
    const IncludeReader& readInclude);
  static std::string resourcePath(const std::string& materialName, const char* extension);
};

} // namespace tf2::native
