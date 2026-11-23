#pragma once

// One material slot of one mesh, resolved through the MDL skin family table to a
// texture the asset tree can actually read.
//
// WHY THIS IS SHARED
//   Two consumers ask the same question: the product (native/src/main.cpp) to
//   decide which VTF to upload for which draw run, and the audit probe
//   (native/tools/entity_model_probe.cpp) to publish what the product painted.
//   If they answered it with two copies of the code the reading could disagree
//   with the pixels and both would look green. One function, two callers.
//
// THE CHAIN (each step is a table, not a guess)
//   mesh (bodyPart, model, mesh) --mstudiomesh_t::material--> material slot
//   material slot --skin family table[family]--> MDL texture slot
//   texture slot --mstudiotexture_t::name + $cdmaterials--> name stems
//   stem --.vmt--> $basetexture --.vtf--> readable texture
//   The first stem whose VMT parses and whose VTF reads wins, in engine try order.
//
// A failure returns false with a reason; it never returns a default slot, because
// "slot 0" is exactly the guess that painted every class red for a whole round.

#include <cstdint>
#include <string>
#include <vector>

#include "model_loader.h"
#include "vmt_material.h"
#include "vtf_texture.h"

namespace tf2::native {

struct MaterialSlotResolution {
  bool resolved = false;
  std::uint32_t skinFamily = 0;
  std::int32_t materialSlot = -1;
  std::int32_t textureSlot = -1;
  std::string textureStem;
  std::string vmtPath;
  std::string vtfPath;
  std::string reason;
  bool alphaTest = false;
  bool translucent = false;
  bool noCull = false;
  bool additive = false;
  bool unlit = false;
  bool modulate = false;
  bool modulate2x = false;
  VmtMaterialProxy proxy;
  std::string secondVtfPath;
};

// THE SKIN FAMILY RULE (engine behaviour, not a tolerance)
// ------------------------------------------------------
// A model's skin family table carries skinFamilyCount families, and a model that
// ships one family has no family 1. The product derives an instance's paint index
// from its team (BLU -> family 1), so a one-family model used to be asked for a
// family it does not contain. What the engine does with an out-of-range paint
// index is clamp it to the model's own default (family 0), and this pair of
// helpers is that rule written once, for both sides of the product:
//
//   * the paint stage uploads one texture per SELECTABLE family, and
//   * the draw stage picks the instance's family from the same list,
//
// so the key a texture is uploaded under and the key the draw looks up cannot
// disagree. On 2026-10-28 they did: 35 (slot, family) requests named a family the
// model did not have, 5 of 96 drawn instances carried no model texture at all,
// and every counter still read plausibly because the range fell back to the world
// atlas.
//
// resolveMaterialSlot below is deliberately NOT changed: an audit that reports
// "family 1 is absent from this model" must be able to say exactly that, and the
// probe (native/tools/entity_model_probe.cpp) counts per family. Choosing a family
// the model has is the caller's job; resolving it is this function's.
constexpr std::uint32_t kMaxSelectedSkinFamilies = 2;  // red and blu

// The family a request becomes. familyCount is the number of families the model
// actually has; 0 means the table is unknown, and then the request is passed
// through rather than invented.
inline std::uint32_t effectiveSkinFamily(std::uint32_t familyCount, std::uint32_t requested) {
  if (familyCount == 0) return requested;
  return requested < familyCount ? requested : 0;
}

// How many families can be selected for one model: one per team, never more than
// the model has, never zero (an unknown table still gets family 0, whose
// resolution then fails loudly instead of the model silently having no paint).
inline std::uint32_t selectableSkinFamilyCount(const ModelMetadata::SkinFamilyTable& skinFamilies) {
  const std::uint32_t count = skinFamilies.skinFamilyCount > 0
    ? skinFamilies.skinFamilyCount : 1u;
  return count < kMaxSelectedSkinFamilies ? count : kMaxSelectedSkinFamilies;
}

// `read` returns the bytes of a resource path, empty when the path does not
// exist. Both callers already have such a lookup (AssetRoot / AssetStoreChain).
template <typename Reader>
bool resolveMaterialSlot(const ModelMetadata::SkinFamilyTable& skinFamilies,
                         const std::vector<std::vector<std::string>>& textureCandidates,
                         std::uint32_t skinFamily, std::int32_t materialSlot,
                         Reader&& read, MaterialSlotResolution& out) {
  out = MaterialSlotResolution{};
  out.skinFamily = skinFamily;
  out.materialSlot = materialSlot;
  if (!skinFamilies.available) {
    out.reason = "skin family table unavailable: " + skinFamilies.reason;
    return false;
  }
  const auto textureSlot = skinFamilies.textureSlotFor(skinFamily, materialSlot);
  if (textureSlot < 0) {
    out.reason = "skin family " + std::to_string(skinFamily) + " has no texture for material slot "
      + std::to_string(materialSlot);
    return false;
  }
  out.textureSlot = textureSlot;
  if (static_cast<std::size_t>(textureSlot) >= textureCandidates.size()) {
    out.reason = "texture slot " + std::to_string(textureSlot) + " is outside the texture table";
    return false;
  }
  for (const auto& stem : textureCandidates[static_cast<std::size_t>(textureSlot)]) {
    if (stem.empty()) continue;
    const std::string vmtResource = VmtParser::resourcePath(stem, ".vmt");
    const auto vmtBytes = read(vmtResource);
    if (vmtBytes.empty()) continue;
    VmtMaterial material;
    if (!VmtParser::parse(std::string(vmtBytes.begin(), vmtBytes.end()), material,
          [&](const std::string& resource) { return read(resource); })) continue;
    const std::string& baseTexture = material.baseTexture.empty()
      ? material.fallbackBaseTexture : material.baseTexture;
    if (baseTexture.empty()) continue;
    const std::string vtfResource = VmtParser::resourcePath(baseTexture, ".vtf");
    const auto vtfBytes = read(vtfResource);
    if (vtfBytes.empty()) continue;
    VtfTexture texture;
    if (!texture.parse(vtfBytes)) continue;
    out.resolved = true;
    out.textureStem = stem;
    out.vmtPath = vmtResource;
    out.vtfPath = vtfResource;
    out.alphaTest = material.alphaTest;
    out.translucent = material.translucent;
    out.noCull = material.noCull;
    out.additive = material.additive;
    out.unlit = material.unlit;
    out.modulate = material.modulate;
    out.modulate2x = material.modulate2x;
    out.proxy = material.proxy;
    if (material.unlit && !material.texture2.empty())
      out.secondVtfPath = VmtParser::resourcePath(material.texture2, ".vtf");
    out.reason = "resolved";
    return true;
  }
  out.reason = "no candidate VMT for texture slot " + std::to_string(textureSlot) + " resolved to a readable VTF";
  return false;
}

} // namespace tf2::native
