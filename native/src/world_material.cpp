#include "world_material.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace tf2::native {

// ---------------------------------------------------------------------------
// Capability state
// ---------------------------------------------------------------------------

const char* worldCapabilityStateName(WorldCapabilityState state) {
  switch (state) {
    case WorldCapabilityState::Unavailable: return "unavailable";
    case WorldCapabilityState::Fallback: return "fallback";
    case WorldCapabilityState::Active: return "active";
  }
  return "unknown";
}

bool FixedResourceProbe::reachable(const std::string& resourcePath) const {
  return std::find(present_.begin(), present_.end(), resourcePath) != present_.end();
}

namespace {

bool lightUvInUnitSquare(const BspTriangle& triangle) {
  const float values[] = {triangle.alu, triangle.alv, triangle.blu, triangle.blv,
    triangle.clu, triangle.clv};
  for (const float value : values) {
    if (!std::isfinite(value) || value < 0.0f || value > 1.0f) return false;
  }
  return true;
}

} // namespace

// ---------------------------------------------------------------------------
// 1. RGBExp32 per-face lightmap
// ---------------------------------------------------------------------------

WorldLightmapReading measureLightmap(const BspMap& map) {
  WorldLightmapReading reading;
  reading.facesWithLightmap = map.lightmapFaceCount;
  reading.facesPacked = map.lightmapPackedFaces;
  // The number this module exists to name. A face that had lightmap data and
  // lost it to the 2048-wide shelf packer is not the same as a face that never
  // had any, and until now nothing could tell them apart.
  reading.facesDropped = reading.facesWithLightmap > reading.facesPacked
    ? reading.facesWithLightmap - reading.facesPacked : 0;
  reading.triangles = map.triangleCount;
  reading.samples = map.lightmapSampleCount;
  reading.atlasBytes = map.lightmapAtlas.size();
  reading.atlasWidth = map.lightmapAtlasWidth;
  reading.atlasHeight = map.lightmapAtlasHeight;
  for (const auto& triangle : map.triangles) {
    if (!triangle.lightmapped) continue;
    ++reading.trianglesWithLightUv;
    if (!lightUvInUnitSquare(triangle)) ++reading.lightUvOutOfRange;
  }
  if (reading.atlasWidth > 0 && reading.atlasHeight > 0) {
    const auto expected = static_cast<std::size_t>(reading.atlasWidth)
      * static_cast<std::size_t>(reading.atlasHeight) * 4u;
    reading.atlasBytesMatchDimensions = reading.atlasBytes == expected;
  }
  reading.uvAllInRange = reading.trianglesWithLightUv > 0 && reading.lightUvOutOfRange == 0;
  return reading;
}

// ---------------------------------------------------------------------------
// 2. Cubemap
// ---------------------------------------------------------------------------

const char* worldCubemapFaceSuffix(int face) {
  static const char* suffixes[] = {"rt", "lf", "bk", "ft", "up", "dn"};
  if (face < 0 || face > 5) return nullptr;
  return suffixes[face];
}

bool worldCubemapFacePaths(const std::string& mapName, int sampleIndex, std::string (&out)[6]) {
  // The BSP stores the sample count; an index beyond any plausible map is a
  // parser bug rather than a missing resource, and reporting it as the latter
  // would send a reader looking in the VPK for a file that was never named.
  if (mapName.empty() || sampleIndex < 0 || sampleIndex > 4096) return false;
  for (int face = 0; face < 6; ++face) {
    out[face] = "materials/maps/" + mapName + "/c_" + mapName + "_"
      + std::to_string(sampleIndex) + worldCubemapFaceSuffix(face) + ".vtf";
  }
  return true;
}

bool worldCubemapFaceForDirection(float x, float y, float z, int& face, float& u, float& v) {
  if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) return false;
  const float ax = std::fabs(x), ay = std::fabs(y), az = std::fabs(z);
  const float major = std::max({ax, ay, az});
  // A zero direction has no dominant axis. Sampling face 0 by default would
  // make a broken reflection vector look like a working one.
  if (!(major > 0.0f)) return false;
  float sc = 0.0f, tc = 0.0f;
  if (ax >= ay && ax >= az) {
    face = x > 0.0f ? 0 : 1;
    sc = face == 0 ? -z : z;
    tc = -y;
  } else if (ay >= az) {
    face = y > 0.0f ? 2 : 3;
    sc = x;
    tc = face == 2 ? z : -z;
  } else {
    face = z > 0.0f ? 4 : 5;
    sc = face == 4 ? x : -x;
    tc = -y;
  }
  u = (sc / major + 1.0f) * 0.5f;
  v = (1.0f - tc / major) * 0.5f;
  return std::isfinite(u) && std::isfinite(v);
}

WorldCubemapReading measureCubemaps(const BspMap& map, const std::string& mapName,
    const WorldResourceProbe& probe) {
  WorldCubemapReading reading;
  reading.samples = map.cubemaps.size();
  reading.perSample.reserve(map.cubemaps.size());
  for (std::size_t i = 0; i < map.cubemaps.size(); ++i) {
    WorldCubemapSampleReading sample;
    sample.index = static_cast<int>(i);
    sample.origin[0] = map.cubemaps[i].origin[0];
    sample.origin[1] = map.cubemaps[i].origin[1];
    sample.origin[2] = map.cubemaps[i].origin[2];
    std::string paths[6];
    if (!worldCubemapFacePaths(mapName, sample.index, paths)) {
      sample.firstMissing = "face path rejected";
      reading.perSample.push_back(std::move(sample));
      continue;
    }
    for (int face = 0; face < 6; ++face) {
      if (probe.reachable(paths[face])) {
        ++sample.facesFound;
      } else if (sample.firstMissing.empty()) {
        sample.firstMissing = paths[face];
      }
    }
    if (sample.facesFound == 6) ++reading.samplesComplete;
    reading.perSample.push_back(std::move(sample));
  }
  reading.anyComplete = reading.samplesComplete > 0;
  return reading;
}

// ---------------------------------------------------------------------------
// 3. Skybox
// ---------------------------------------------------------------------------

WorldSkyReading measureSky(const BspMap& map, const WorldResourceProbe& probe) {
  WorldSkyReading reading;
  reading.declared = !map.skyName.empty();
  reading.name = map.skyName;
  reading.skyFaces = map.skyFaces;
  if (!reading.declared) return reading;
  for (int face = 0; face < 6; ++face) {
    std::string path;
    if (!BspParser::skyMaterialPath(map.skyName, face, path)) {
      if (reading.firstMissing.empty()) reading.firstMissing = "sky path rejected";
      continue;
    }
    if (probe.reachable(path)) {
      ++reading.vmtFound;
    } else if (reading.firstMissing.empty()) {
      reading.firstMissing = path;
    }
  }
  return reading;
}

// ---------------------------------------------------------------------------
// 4. Displacement
// ---------------------------------------------------------------------------

WorldDisplacementReading measureDisplacement(const BspMap& map, std::size_t triangleLimit) {
  WorldDisplacementReading reading;
  reading.faces = map.displacementFaces;
  reading.cornerMatches = map.displacementCornerMatches;
  reading.triangles = map.displacementTriangles;
  reading.renderedFaces = map.renderedFaceCount;
  reading.facesTotal = map.faceCount;
  reading.limitTruncated = triangleLimit > 0 && map.triangles.size() >= triangleLimit;
  reading.cornerMatchesAllFaces = reading.faces > 0
    && reading.cornerMatches == reading.faces;
  for (const auto& triangle : map.triangles) {
    if (triangle.displacement) ++reading.trianglesTagged;
  }
  return reading;
}

// ---------------------------------------------------------------------------
// 5. Water
// ---------------------------------------------------------------------------

WorldWaterReading measureWater(const BspMap& map, bool reflectionDeclaredBySettings,
    bool refractionDeclaredBySettings, bool waterMaterialDeclared, bool waterTextureReachable) {
  WorldWaterReading reading;
  reading.faces = map.waterFaces;
  reading.reflectionDeclaredBySettings = reflectionDeclaredBySettings;
  reading.refractionDeclaredBySettings = refractionDeclaredBySettings;
  reading.materialDeclared = waterMaterialDeclared;
  reading.textureReachable = waterTextureReachable;
  for (const auto& triangle : map.triangles) {
    if (triangle.water) ++reading.trianglesTagged;
  }
  return reading;
}

// ---------------------------------------------------------------------------
// 6. VIS
// ---------------------------------------------------------------------------

WorldVisReading measureVis(const BspMap& map) {
  WorldVisReading reading;
  reading.mode = map.visMode == BspVisMode::ClusterPvs ? "cluster-pvs" : "unavailable";
  reading.clusters = map.visClusterCount;
  if (map.visMode == BspVisMode::ClusterPvs) {
    reading.visibleFromCluster0 = BspParser::countVisibleClusters(map, 0);
  }
  // A PVS that hides nothing is not clipping. With one cluster, or with every
  // cluster visible, "0 hidden" is a fact about the map, and reporting it as a
  // clipping capability would be the same class of claim as the ones this
  // module was written to replace.
  reading.queryable = reading.clusters > 1 && reading.visibleFromCluster0 > 0
    && reading.visibleFromCluster0 < reading.clusters;
  for (const auto& triangle : map.triangles) {
    if (triangle.cluster >= 0) ++reading.trianglesWithCluster;
    else ++reading.trianglesWithoutCluster;
  }
  reading.clusterCoverage = map.triangles.empty() ? 0.0
    : static_cast<double>(reading.trianglesWithCluster) / static_cast<double>(map.triangles.size());
  return reading;
}

// ---------------------------------------------------------------------------
// All six
// ---------------------------------------------------------------------------

WorldCapabilityReadings measureWorldCapabilities(const BspMap& map, const std::string& mapName,
    const WorldResourceProbe& probe, bool reflectionDeclaredBySettings,
    bool refractionDeclaredBySettings, bool waterMaterialDeclared, bool waterTextureReachable,
    std::size_t triangleLimit) {
  WorldCapabilityReadings readings;
  readings.lightmap = measureLightmap(map);
  readings.cubemap = measureCubemaps(map, mapName, probe);
  readings.sky = measureSky(map, probe);
  readings.displacement = measureDisplacement(map, triangleLimit);
  readings.water = measureWater(map, reflectionDeclaredBySettings, refractionDeclaredBySettings,
    waterMaterialDeclared, waterTextureReachable);
  readings.vis = measureVis(map);
  return readings;
}

// ---------------------------------------------------------------------------
// Judgement
// ---------------------------------------------------------------------------

namespace {

std::string counts(std::size_t value) { return std::to_string(value); }

} // namespace

WorldCapabilityVerdict judgeLightmap(const WorldLightmapReading& r) {
  WorldCapabilityVerdict verdict;
  if (r.trianglesWithLightUv == 0) {
    if (r.facesWithLightmap == 0) {
      verdict.state = WorldCapabilityState::Unavailable;
      verdict.reason = "the bsp declares no lightmap faces";
      return verdict;
    }
    // The packer is the loss, and saying so is the difference between "this
    // map has no baked light" and "this map's baked light did not fit". The
    // third case is the other way to reach zero triangles: the caller's
    // triangle limit stopped the walk before any of them were emitted.
    verdict.state = WorldCapabilityState::Fallback;
    if (r.facesPacked == 0) {
      verdict.reason = counts(r.facesWithLightmap)
        + " faces carry lightmap data and 0 survived the 2048-wide atlas packer";
    } else {
      verdict.reason = counts(r.facesWithLightmap) + " faces carry lightmap data and "
        + counts(r.facesPacked) + " packed, but the triangle limit left 0 triangles to sample it";
    }
    return verdict;
  }
  if (!r.uvAllInRange) {
    verdict.state = WorldCapabilityState::Fallback;
    verdict.reason = counts(r.lightUvOutOfRange) + " of " + counts(r.trianglesWithLightUv)
      + " lightmapped triangles carry a light uv outside 0..1";
    return verdict;
  }
  if (!r.atlasBytesMatchDimensions) {
    verdict.state = WorldCapabilityState::Fallback;
    verdict.reason = "atlas holds " + counts(r.atlasBytes) + " bytes but "
      + std::to_string(r.atlasWidth) + "x" + std::to_string(r.atlasHeight) + "x4 needs more";
    return verdict;
  }
  verdict.state = WorldCapabilityState::Active;
  verdict.reason = "rgbexp32 atlas " + std::to_string(r.atlasWidth) + "x"
    + std::to_string(r.atlasHeight) + " packs " + counts(r.facesPacked) + " of "
    + counts(r.facesWithLightmap) + " faces; " + counts(r.trianglesWithLightUv) + " of "
    + counts(r.triangles) + " triangles carry a per-face light uv";
  if (r.facesDropped > 0) {
    verdict.reason += "; " + counts(r.facesDropped) + " faces dropped by the packer";
  }
  return verdict;
}

WorldCapabilityVerdict judgeCubemaps(const WorldCubemapReading& r) {
  WorldCapabilityVerdict verdict;
  if (r.samples == 0) {
    verdict.state = WorldCapabilityState::Unavailable;
    verdict.reason = "the bsp declares no env_cubemap samples";
    return verdict;
  }
  if (r.anyComplete) {
    verdict.state = WorldCapabilityState::Active;
    verdict.reason = counts(r.samplesComplete) + " of " + counts(r.samples)
      + " env_cubemap samples have all six faces reachable";
    return verdict;
  }
  verdict.state = WorldCapabilityState::Fallback;
  verdict.reason = "0 of " + counts(r.samples)
    + " env_cubemap samples have all six faces reachable";
  for (const auto& sample : r.perSample) {
    if (!sample.firstMissing.empty()) {
      verdict.reason += "; first missing " + sample.firstMissing;
      break;
    }
  }
  return verdict;
}

WorldCapabilityVerdict judgeSky(const WorldSkyReading& r) {
  WorldCapabilityVerdict verdict;
  if (!r.declared) {
    verdict.state = WorldCapabilityState::Unavailable;
    verdict.reason = "the entity lump names no skyname";
    return verdict;
  }
  if (r.vmtFound == 6) {
    verdict.state = WorldCapabilityState::Active;
    verdict.reason = "all six skybox vmts resolve for " + r.name;
    return verdict;
  }
  verdict.state = WorldCapabilityState::Fallback;
  verdict.reason = std::to_string(r.vmtFound) + " of 6 skybox vmts resolve for " + r.name;
  if (!r.firstMissing.empty()) verdict.reason += "; first missing " + r.firstMissing;
  return verdict;
}

WorldCapabilityVerdict judgeDisplacement(const WorldDisplacementReading& r) {
  WorldCapabilityVerdict verdict;
  if (r.faces == 0) {
    verdict.state = WorldCapabilityState::Unavailable;
    verdict.reason = "no displacement face emitted geometry";
    return verdict;
  }
  if (!r.cornerMatchesAllFaces) {
    verdict.state = WorldCapabilityState::Fallback;
    verdict.reason = "corner match " + counts(r.cornerMatches) + " of " + counts(r.faces)
      + " displacement faces";
    return verdict;
  }
  if (r.trianglesTagged != r.triangles) {
    verdict.state = WorldCapabilityState::Fallback;
    verdict.reason = counts(r.trianglesTagged) + " triangles carry the displacement tag but the"
      " parser counted " + counts(r.triangles);
    return verdict;
  }
  if (r.limitTruncated) {
    verdict.state = WorldCapabilityState::Fallback;
    verdict.reason = "the triangle limit truncated the walk after " + counts(r.triangles)
      + " triangles";
    return verdict;
  }
  verdict.state = WorldCapabilityState::Active;
  verdict.reason = counts(r.faces) + " displacement faces emit " + counts(r.triangles)
    + " triangles, corner match " + counts(r.cornerMatches) + "/" + counts(r.faces);
  return verdict;
}

WorldCapabilityVerdict judgeWater(const WorldWaterReading& r) {
  WorldCapabilityVerdict verdict;
  if (r.faces == 0) {
    verdict.state = WorldCapabilityState::Unavailable;
    verdict.reason = "the bsp declares no water faces";
    return verdict;
  }
  if (!r.materialDeclared) {
    verdict.state = WorldCapabilityState::Fallback;
    verdict.reason = counts(r.faces) + " water faces, no water material declared";
    return verdict;
  }
  if (!r.textureReachable) {
    verdict.state = WorldCapabilityState::Fallback;
    verdict.reason = "water material declared but its texture is unreachable";
    return verdict;
  }
  // The surface resolves, and that is as far as this goes. Reflection and
  // refraction are render targets nobody has built; reporting the surface as
  // "active" would be the same class of claim as the ones this module replaces.
  verdict.state = WorldCapabilityState::Fallback;
  verdict.reason = counts(r.faces) + " water faces resolve to a texture; reflection="
    + (r.reflectionDeclaredBySettings ? "declared" : "off") + " refraction="
    + (r.refractionDeclaredBySettings ? "declared" : "off")
    + ", neither render target is implemented so the surface draws unlit";
  return verdict;
}

WorldCapabilityVerdict judgeVis(const WorldVisReading& r) {
  WorldCapabilityVerdict verdict;
  if (std::strcmp(r.mode, "cluster-pvs") != 0) {
    verdict.state = WorldCapabilityState::Unavailable;
    verdict.reason = "the bsp carries no usable pvs lump";
    return verdict;
  }
  if (!r.queryable) {
    verdict.state = WorldCapabilityState::Fallback;
    verdict.reason = "pvs present with " + counts(static_cast<std::size_t>(r.clusters))
      + " clusters but hides nothing (" + counts(static_cast<std::size_t>(r.visibleFromCluster0))
      + " visible from cluster 0)";
    return verdict;
  }
  if (r.trianglesWithCluster == 0) {
    verdict.state = WorldCapabilityState::Fallback;
    verdict.reason = "pvs hides clusters but no triangle carries one, so a per-triangle"
      " clip has nothing to act on";
    return verdict;
  }
  verdict.state = WorldCapabilityState::Active;
  const auto hidden = static_cast<std::size_t>(r.clusters - r.visibleFromCluster0);
  verdict.reason = "pvs hides " + counts(hidden) + " of "
    + counts(static_cast<std::size_t>(r.clusters)) + " clusters; " + counts(r.trianglesWithCluster)
    + " of " + counts(r.trianglesWithCluster + r.trianglesWithoutCluster)
    + " triangles carry a cluster";
  return verdict;
}

// ---------------------------------------------------------------------------
// Synthetic BSP fixtures
// ---------------------------------------------------------------------------

namespace {

constexpr std::size_t kFixtureHeaderSize = 8 + 64 * 16 + 4;

struct Lump { std::int32_t offset = 0; std::int32_t length = 0; };

void put16(std::vector<std::uint8_t>& bytes, std::size_t at, std::uint16_t value) {
  std::memcpy(bytes.data() + at, &value, 2);
}
void put32(std::vector<std::uint8_t>& bytes, std::size_t at, std::int32_t value) {
  std::memcpy(bytes.data() + at, &value, 4);
}
void putFloat(std::vector<std::uint8_t>& bytes, std::size_t at, float value) {
  std::memcpy(bytes.data() + at, &value, 4);
}

// Lumps the parser requires. Index order is the BSP lump order; the writer
// emits them in this order and records each one's offset/length in the table.
struct LumpPlan {
  int index = 0;
  std::vector<std::uint8_t> bytes;
};

} // namespace

std::int32_t appendLuxel(std::vector<std::uint8_t>& lighting, std::uint8_t r, std::uint8_t g,
    std::uint8_t b, std::int8_t exponent) {
  if (lighting.size() > static_cast<std::size_t>((std::numeric_limits<std::int32_t>::max)())) return -1;
  const auto offset = static_cast<std::int32_t>(lighting.size());
  lighting.push_back(r);
  lighting.push_back(g);
  lighting.push_back(b);
  lighting.push_back(static_cast<std::uint8_t>(exponent));
  return offset;
}

std::vector<std::uint8_t> buildFixtureBsp(const BspFixture& fixture) {
  const std::size_t faceCount = fixture.faces.size();

  // ---- texData / string table / string data -------------------------------
  // One texData entry per face so each face can name its own material, and one
  // string per face. The parser resolves names[tableId] = string at
  // stringTable[tableId], so table entry i is the byte offset of name i.
  std::vector<std::uint8_t> stringData;
  std::vector<std::int32_t> stringOffsets;
  for (std::size_t i = 0; i < faceCount; ++i) {
    stringOffsets.push_back(static_cast<std::int32_t>(stringData.size()));
    const std::string name = "fixture_mat" + std::to_string(i);
    stringData.insert(stringData.end(), name.begin(), name.end());
    stringData.push_back(0);
  }
  std::vector<std::uint8_t> texData(faceCount * 32, 0);
  std::vector<std::uint8_t> stringTable(stringOffsets.size() * 4, 0);
  for (std::size_t i = 0; i < faceCount; ++i) {
    put32(texData, i * 32 + 12, static_cast<std::int32_t>(i));
  }
  for (std::size_t i = 0; i < stringOffsets.size(); ++i) {
    put32(stringTable, i * 4, stringOffsets[i]);
  }

  // ---- vertices / edges / surfedges --------------------------------------
  // Each face is a standalone quad: four vertices and four edges, no sharing.
  // Sharing would be more realistic and would also make the fixture's edge
  // table a second thing to get right for no reading it buys.
  std::vector<std::uint8_t> vertices(faceCount * 4 * 12, 0);
  std::vector<std::uint8_t> edges(faceCount * 4 * 4, 0);
  std::vector<std::uint8_t> surfEdges(faceCount * 4 * 4, 0);
  for (std::size_t i = 0; i < faceCount; ++i) {
    const auto& face = fixture.faces[i];
    for (int corner = 0; corner < 4; ++corner) {
      const auto at = (i * 4 + static_cast<std::size_t>(corner)) * 12;
      putFloat(vertices, at + 0, face.corners[corner][0]);
      putFloat(vertices, at + 4, face.corners[corner][1]);
      putFloat(vertices, at + 8, face.corners[corner][2]);
    }
    for (int e = 0; e < 4; ++e) {
      const auto first = static_cast<std::uint16_t>(i * 4 + static_cast<std::size_t>(e));
      const auto second = static_cast<std::uint16_t>(i * 4 + static_cast<std::size_t>((e + 1) % 4));
      put16(edges, (i * 4 + static_cast<std::size_t>(e)) * 4, first);
      put16(edges, (i * 4 + static_cast<std::size_t>(e)) * 4 + 2, second);
      put32(surfEdges, (i * 4 + static_cast<std::size_t>(e)) * 4,
        static_cast<std::int32_t>(i * 4 + static_cast<std::size_t>(e)));
    }
  }

  // ---- texinfo ------------------------------------------------------------
  std::vector<std::uint8_t> texInfo(faceCount * 72, 0);
  for (std::size_t i = 0; i < faceCount; ++i) {
    const auto& face = fixture.faces[i];
    const auto base = i * 72;
    for (int k = 0; k < 4; ++k) {
      putFloat(texInfo, base + 0 + k * 4, face.uAxis[k]);
      putFloat(texInfo, base + 16 + k * 4, face.vAxis[k]);
      putFloat(texInfo, base + 32 + k * 4, face.luAxis[k]);
      putFloat(texInfo, base + 48 + k * 4, face.lvAxis[k]);
    }
    put32(texInfo, base + 64, face.flags);
    put32(texInfo, base + 68, face.texDataIndex);
  }

  // ---- faces --------------------------------------------------------------
  std::vector<std::uint8_t> faces(faceCount * 56, 0);
  for (std::size_t i = 0; i < faceCount; ++i) {
    const auto& face = fixture.faces[i];
    const auto base = i * 56;
    put32(faces, base + 4, static_cast<std::int32_t>(i * 4));   // firstEdge
    put16(faces, base + 8, 4);                                   // count
    put16(faces, base + 10, static_cast<std::uint16_t>(i));      // infoIndex
    put16(faces, base + 12, static_cast<std::uint16_t>(face.dispIndex));
    faces[base + 16] = face.style0;
    put32(faces, base + 20, face.lightOffset);
    put32(faces, base + 28, face.minsU);
    put32(faces, base + 32, face.minsV);
    put32(faces, base + 36, face.samplesW);
    put32(faces, base + 40, face.samplesH);
  }

  // ---- models -------------------------------------------------------------
  std::vector<std::uint8_t> models(48, 0);
  put32(models, 40, 0);                                   // firstFace
  put32(models, 44, static_cast<std::int32_t>(faceCount));

  // ---- entities -----------------------------------------------------------
  std::vector<std::uint8_t> entities(fixture.entities.begin(), fixture.entities.end());

  // ---- planes / nodes -----------------------------------------------------
  std::vector<std::uint8_t> planes, nodes;
  if (fixture.withTree) {
    planes.resize(20, 0);
    putFloat(planes, 0, 1.0f);
    putFloat(planes, 4, 0.0f);
    putFloat(planes, 8, 0.0f);
    putFloat(planes, 12, fixture.splitX);
    nodes.resize(32, 0);
    put32(nodes, 0, 0);    // plane
    put32(nodes, 4, -1);   // child[0] -> leaf 0
    put32(nodes, 8, -2);   // child[1] -> leaf 1
  }

  // ---- leafs / leafFaces --------------------------------------------------
  std::vector<std::uint8_t> leafs(fixture.leaves.size() * 32, 0);
  std::vector<std::uint8_t> leafFaceBytes;
  for (std::size_t i = 0; i < fixture.leaves.size(); ++i) {
    const auto& leaf = fixture.leaves[i];
    const auto base = i * 32;
    put16(leafs, base + 4, static_cast<std::uint16_t>(leaf.cluster));
    put16(leafs, base + 20, static_cast<std::uint16_t>(leafFaceBytes.size() / 2));
    put16(leafs, base + 22, static_cast<std::uint16_t>(leaf.faces.size()));
    for (const auto faceIndex : leaf.faces) {
      const auto at = leafFaceBytes.size();
      leafFaceBytes.resize(at + 2);
      put16(leafFaceBytes, at, faceIndex);
    }
  }

  // ---- displacement -------------------------------------------------------
  std::vector<std::uint8_t> dispInfo(fixture.displacements.size() * 176, 0);
  for (std::size_t i = 0; i < fixture.displacements.size(); ++i) {
    const auto& disp = fixture.displacements[i];
    const auto base = i * 176;
    putFloat(dispInfo, base + 0, disp.start.x);
    putFloat(dispInfo, base + 4, disp.start.y);
    putFloat(dispInfo, base + 8, disp.start.z);
    put32(dispInfo, base + 12, disp.vertStart);
    put32(dispInfo, base + 20, disp.power);
  }
  std::vector<std::uint8_t> dispVerts = fixture.dispVertBytes;

  // ---- cubemaps -----------------------------------------------------------
  std::vector<std::uint8_t> cubemapBytes(fixture.cubemaps.size() * 16, 0);
  for (std::size_t i = 0; i < fixture.cubemaps.size(); ++i) {
    const auto base = i * 16;
    put32(cubemapBytes, base + 0, fixture.cubemaps[i].origin[0]);
    put32(cubemapBytes, base + 4, fixture.cubemaps[i].origin[1]);
    put32(cubemapBytes, base + 8, fixture.cubemaps[i].origin[2]);
    cubemapBytes[base + 12] = static_cast<std::uint8_t>(fixture.cubemaps[i].size);
  }

  // ---- vis ----------------------------------------------------------------
  // int32 cluster count, then one int32 offset per cluster, then the bits. Each
  // cluster's mask starts at its own offset so a mutation of one cluster's byte
  // cannot be mistaken for another's.
  std::vector<std::uint8_t> vis;
  if (fixture.clusterCount > 0) {
    const auto clusters = static_cast<std::size_t>(fixture.clusterCount);
    vis.resize(4 + clusters * 8 + clusters, 0);
    put32(vis, 0, fixture.clusterCount);
    for (std::size_t i = 0; i < clusters; ++i) {
      const auto offset = static_cast<std::int32_t>(4 + clusters * 8 + i);
      put32(vis, 4 + i * 8, i < fixture.visOffsets.size() ? fixture.visOffsets[i] : offset);
    }
    for (std::size_t i = 0; i < clusters && i < fixture.visBits.size(); ++i) {
      vis[4 + clusters * 8 + i] = fixture.visBits[i];
    }
  }

  // ---- assemble -----------------------------------------------------------
  // Only the required lumps plus the ones this fixture actually filled. A lump
  // the fixture left empty is written as offset 0 / length 0, which the parser
  // decodes as empty rather than as an error.
  std::vector<LumpPlan> plan;
  const auto add = [&](int index, std::vector<std::uint8_t> bytes) {
    plan.push_back(LumpPlan{index, std::move(bytes)});
  };
  add(0, std::move(entities));
  add(1, std::move(planes));
  add(2, std::move(texData));
  add(3, std::move(vertices));
  add(4, std::move(vis));
  add(5, std::move(nodes));
  add(6, std::move(texInfo));
  add(7, std::move(faces));
  add(8, fixture.lighting);
  add(10, std::move(leafs));
  add(12, std::move(edges));
  add(13, std::move(surfEdges));
  add(14, std::move(models));
  add(16, std::move(leafFaceBytes));
  add(26, std::move(dispInfo));
  add(33, std::move(dispVerts));
  add(42, std::move(cubemapBytes));
  add(43, std::move(stringData));
  add(44, std::move(stringTable));

  std::sort(plan.begin(), plan.end(), [](const LumpPlan& a, const LumpPlan& b) {
    return a.index < b.index;
  });

  std::vector<std::uint8_t> file(kFixtureHeaderSize, 0);
  std::memcpy(file.data(), "VBSP", 4);
  put32(file, 4, 20);   // version; the parser stores it and checks nothing
  for (const auto& lump : plan) {
    const auto offset = static_cast<std::int32_t>(file.size());
    file.insert(file.end(), lump.bytes.begin(), lump.bytes.end());
    put32(file, 8 + static_cast<std::size_t>(lump.index) * 16, offset);
    put32(file, 8 + static_cast<std::size_t>(lump.index) * 16 + 4,
      static_cast<std::int32_t>(lump.bytes.size()));
  }
  put32(file, 8 + 64 * 16, 0);  // revision
  return file;
}

} // namespace tf2::native
