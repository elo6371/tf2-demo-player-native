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
    }
    if (!vmt.bumpMap2.empty()) {
      countTexture(reading.bumpMap2, entry.material, vmt.bumpMap2, visible, resolvers);
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
      // `%detailtype` lives in the detail material, not in this one. It is read
      // here because the count that matters is how many detail layers the scan
      // could classify, and a version that only counted `$detail` would report
      // 51 declared and 0 read without saying why.
      if (detailParsed) {
        const auto at = detailMaterial.fields.find("%detailtype");
        if (at != detailMaterial.fields.end() && !at->second.empty()) {
          if (reading.firstDetailType.empty()) reading.firstDetailType = at->second;
          detailTypesRead.insert(at->second);
        }
      }
    }
  }
  reading.distinctBumpPaths = bumpPaths.size();
  reading.detailTypesRead = detailTypesRead.size();

  // The named refusals. Each one is derived from the counts above, so a reader
  // can re-derive the sentence from the numbers printed next to it.
  if (reading.bumpMap.declared > 0) {
    if (reading.distinctBumpPaths > 1) {
      reading.bumpMap.reason = "the world pass has one bump sampler slot for the whole world, but the"
        " materials name " + std::to_string(reading.distinctBumpPaths) + " distinct $bumpmap textures, so"
        " no material's own normal map is applied";
    } else if (reading.bumpMap.resourcesMissing > 0) {
      reading.bumpMap.reason = pct(reading.bumpMap.resourcesMissing, reading.bumpMap.declared)
        + " declared $bumpmap textures did not resolve";
    } else {
      reading.bumpMap.reason = "one $bumpmap texture serves the whole world; per-material binding is not implemented";
    }
  }
  if (reading.bumpMap2.declared > 0) {
    reading.bumpMap2.reason = "$bumpmap2 is layer 2's normal map and the pass has one bump sampler;"
      " it is refused, not silently replaced by layer 1's";
  }
  if (reading.ssBump.declared > 0) {
    reading.ssBump.reason = "$ssbump needs the SSBump normal decode and a tangent basis the world"
      " vertex format does not carry; the bump map is not sampled for these materials";
  }
  if (reading.detail.declared > 0) {
    reading.detail.reason = "detail layers need a second, detailscale-scaled uv and the"
      " `$detailblendmode` result; the world vertex carries one atlas uv, so the layer is refused";
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
