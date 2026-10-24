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
};

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
    if (!VmtParser::parse(std::string(vmtBytes.begin(), vmtBytes.end()), material)) continue;
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
    out.reason = "resolved";
    return true;
  }
  out.reason = "no candidate VMT for texture slot " + std::to_string(textureSlot) + " resolved to a readable VTF";
  return false;
}

} // namespace tf2::native
