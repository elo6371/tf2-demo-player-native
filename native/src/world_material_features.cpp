#include "world_material_features.h"

#include <cstdio>
#include <set>
#include <unordered_set>

namespace tf2::native {
namespace {

void countTexture(WorldMaterialFeatureCount& count, const std::string& material,
    const std::string& textureName, bool visible,
    const WorldFeatureResolvers& resolvers) {
  ++count.declared;
  if (visible) ++count.visibleDeclared;
  if (count.firstMaterial.empty()) count.firstMaterial = material;
  const std::string path = VmtParser::resourcePath(textureName, ".vtf");
  if (count.firstResource.empty()) count.firstResource = path;
  const bool exists = resolvers.resourceExists && resolvers.resourceExists(path);
  if (exists) ++count.resourcesResolved;
  else ++count.resourcesMissing;
}

std::string pct(std::size_t part, std::size_t whole) {
  char buffer[32] = {0};
  std::snprintf(buffer, sizeof(buffer), "%zu/%zu", part, whole);
  return buffer;
}

} // namespace

WorldMaterialFeatureReading scanWorldMaterialFeatures(
    const std::vector<WorldMaterialFeatureInput>& materials,
    const WorldFeatureResolvers& resolvers) {
  WorldMaterialFeatureReading reading;
  std::set<std::string> bumpPaths;
  std::unordered_set<std::string> detailTypesRead;

  for (const auto& entry : materials) {
    if (!entry.vmt) continue;
    const VmtMaterial& vmt = *entry.vmt;
    const bool visible = entry.visibleTriangles > 0;
    if (!vmt.bumpMap.empty()) {
      countTexture(reading.bumpMap, entry.material, vmt.bumpMap, visible, resolvers);
      bumpPaths.insert(vmt.bumpMap);
      if (vmt.ssBump) ++reading.ssBumpDeclared;
      else ++reading.bumpMapWithoutSsBump;
    }
    if (!vmt.bumpMap2.empty()) {
      countTexture(reading.bumpMap2, entry.material, vmt.bumpMap2, visible, resolvers);
      if (vmt.ssBump) ++reading.bumpMap2SsBump;
    }
    if (vmt.ssBump) {
      // `$ssbump` qualifies `$bumpmap` rather than naming a texture of its own,
      // so the resource it is measured against is the bump map's.
      ++reading.ssBump.declared;
      if (visible) ++reading.ssBump.visibleDeclared;
      if (reading.ssBump.firstMaterial.empty()) reading.ssBump.firstMaterial = entry.material;
      if (reading.ssBump.firstResource.empty()) reading.ssBump.firstResource = vmt.bumpMap;
      if (!vmt.bumpMap.empty()
          && resolvers.resourceExists
          && resolvers.resourceExists(VmtParser::resourcePath(vmt.bumpMap, ".vtf"))) {
        ++reading.ssBump.resourcesResolved;
      } else {
        ++reading.ssBump.resourcesMissing;
      }
    }
    // `%detailtype` is a key in THIS material, not in the detail material that
    // `$detail` points at: VBSP reads it at map compile time and emits
    // prop_detail sprites and models from the map's `detail.vbsp` (Valve's
    // "Detail props" page). T27 measured the placement instead of assuming it --
    // `NATURE/BLENDGROUNDTOGRASS_ODYSSEY` in the reference map carries
    // `%detailtype "tf_forest_grass"` in its own VMT, while the detail VMT it
    // names is absent from the install. Counting the key here is what turns
    // "0 read" into "1 read, and its consumer is the compiler".
    if (!vmt.detailType.empty()) {
      if (reading.firstDetailType.empty()) reading.firstDetailType = vmt.detailType;
      detailTypesRead.insert(vmt.detailType);
    }
    if (!vmt.detailTexture.empty()) {
      ++reading.detail.declared;
      if (visible) ++reading.detail.visibleDeclared;
      if (reading.detail.firstMaterial.empty()) reading.detail.firstMaterial = entry.material;
      const std::string detailVmt = VmtParser::resourcePath(vmt.detailTexture, ".vmt");
      if (reading.detail.firstResource.empty()) reading.detail.firstResource = detailVmt;
      const auto bytes = resolvers.readResource ? resolvers.readResource(detailVmt)
                                                : std::vector<std::uint8_t>{};
      VmtMaterial detailMaterial;
      const bool detailParsed = !bytes.empty()
        && VmtParser::parse(std::string(bytes.begin(), bytes.end()), detailMaterial);
      if (detailParsed) ++reading.detail.resourcesResolved;
      else ++reading.detail.resourcesMissing;
    }
  }
  reading.distinctBumpPaths = bumpPaths.size();
  reading.detailTypesRead = detailTypesRead.size();

  // The named refusals. Each one is derived from the counts above, so a reader
  // can re-derive the sentence from the numbers printed next to it.
  // T27: these four sentences used to describe a pass that could not do any of
  // this ("no material's own normal map is applied", "the world vertex carries
  // one atlas uv, so the layer is refused"). Three of the four are switches now,
  // so the sentence has to say which switch, and keep whatever refusal survives
  // it -- otherwise the ledger tells the next reader to implement work that is
  // already in the tree.
  if (reading.bumpMap.declared > 0) {
    if (reading.distinctBumpPaths > 1) {
      reading.bumpMap.reason = "the materials name " + std::to_string(reading.distinctBumpPaths)
        + " distinct $bumpmap textures, so the one sampler slot the pass started with cannot apply"
        " each material's own; per-material binding is the --world-material-bump switch, and with it"
        " on the " + std::to_string(reading.bumpMapWithoutSsBump) + " that do not set $ssbump sample"
        " their own tile through a texinfo-axis tangent basis while the "
        + std::to_string(reading.ssBumpDeclared) + " that do stay refused (see the $ssbump row)";
    } else if (reading.bumpMap.resourcesMissing > 0) {
      reading.bumpMap.reason = pct(reading.bumpMap.resourcesMissing, reading.bumpMap.declared)
        + " declared $bumpmap textures did not resolve";
    } else {
      reading.bumpMap.reason = "one $bumpmap texture serves the whole world; per-material binding is"
        " available behind --world-material-bump";
    }
  }
  if (reading.bumpMap2.declared > 0) {
    reading.bumpMap2.reason = "$bumpmap2 is layer 2's normal map: it is lerped into layer 1's normal"
      " by the vertex blend weight, so it is sampled only when --world-material-bump and"
      " --world-bump2 are both on; with either off it is refused, not silently replaced by layer 1's";
    if (reading.bumpMap2SsBump > 0) {
      reading.bumpMap2.reason += "; " + pct(reading.bumpMap2SsBump, reading.bumpMap2.declared)
        + " of those materials also declare $ssbump, so their second-layer texel is a bumpBasis"
        " weight and stays refused even with both switches on (--world-bump2-texture measures the"
        " layer with a diagnostic normal map instead)";
    }
  }
  if (reading.ssBump.declared > 0) {
    reading.ssBump.reason = "$ssbump selects `BUMPMAP == 2`, where the texel is not a signed normal"
      " but three bumpBasis weights against three lightmap directions; this pass samples one"
      " lightmap and one tangent-space normal, so these materials are refused by name even when"
      " --world-material-bump is on";
  }
  if (reading.detail.declared > 0) {
    reading.detail.reason = "detail layers are the --world-detail switch: the detail material's"
      " $basetexture is sampled at the atlas uv scaled by $detailscale and combined with the albedo"
      " by $detailblendmode; with the switch off the layer is refused";
  }

  char summary[768] = {0};
  std::snprintf(summary, sizeof(summary),
    "materials=%zu bumpmap=%zu(visible %zu, resolved %zu, missing %zu, distinct %zu)"
    " bumpmap2=%zu(visible %zu) ssbump=%zu(visible %zu) detail=%zu(visible %zu, resolved %zu,"
    " types %zu)",
    materials.size(), reading.bumpMap.declared, reading.bumpMap.visibleDeclared,
    reading.bumpMap.resourcesResolved, reading.bumpMap.resourcesMissing,
    reading.distinctBumpPaths, reading.bumpMap2.declared, reading.bumpMap2.visibleDeclared,
    reading.ssBump.declared, reading.ssBump.visibleDeclared, reading.detail.declared,
    reading.detail.visibleDeclared, reading.detail.resourcesResolved, reading.detailTypesRead);
  reading.summary = summary;
  return reading;
}

} // namespace tf2::native
