#include "world_material.h"

#include "LzmaDec.h"
#include "vmt_material.h"
#include "vtf_texture.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace tf2::native {

namespace {

// The BSP lumps already decode raw LZMA through this same library (bsp_map.cpp);
// these are the two allocator hooks it needs and nothing more.
void* pakLzmaAlloc(ISzAllocPtr, size_t size) { return std::malloc(size); }
void pakLzmaFree(ISzAllocPtr, void* address) { std::free(address); }

// The PKWARE LZMA framing a ZIP method-14 entry carries:
//   2 bytes  LZMA SDK version
//   2 bytes  properties size (5 in every entry this install has)
//   5 bytes  properties: lc/lp/pb in byte 0, dictionary size in bytes 1..4
//   rest     the raw stream
// The size to decode to comes from the central directory, so a truncated stream
// fails the length check rather than returning a short buffer that looks whole.
bool decodeZipLzma(const std::uint8_t* payload, std::size_t payloadSize,
    std::size_t outputSize, std::vector<std::uint8_t>& out, std::string* reason) {
  const auto fail = [&](const char* message) {
    if (reason) *reason = message;
    out.clear();
    return false;
  };
  if (payloadSize < 9) return fail("LZMA entry is shorter than its framing");
  const std::uint16_t propertiesSize = static_cast<std::uint16_t>(payload[2]
    | (static_cast<std::uint16_t>(payload[3]) << 8));
  if (propertiesSize != LZMA_PROPS_SIZE || payloadSize < 4u + propertiesSize) {
    return fail("LZMA entry declares a properties size this reader does not use");
  }
  if (outputSize == 0 || outputSize > 128u * 1024u * 1024u) {
    return fail("LZMA entry declares an implausible uncompressed size");
  }
  ISzAlloc allocator{pakLzmaAlloc, pakLzmaFree};
  out.resize(outputSize);
  SizeT inputSize = payloadSize - 4u - propertiesSize;
  SizeT decodedSize = outputSize;
  ELzmaStatus status = LZMA_STATUS_NOT_SPECIFIED;
  const SRes decoded = LzmaDecode(out.data(), &decodedSize, payload + 4u + propertiesSize,
    &inputSize, payload + 4u, static_cast<unsigned>(propertiesSize), LZMA_FINISH_ANY, &status,
    &allocator);
  if (decoded != SZ_OK || decodedSize != outputSize) {
    if (reason) {
      *reason = "LZMA decode stopped at " + std::to_string(decodedSize) + " of "
        + std::to_string(outputSize) + " bytes (status " + std::to_string(status) + ")";
    }
    out.clear();
    return false;
  }
  return true;
}

} // namespace

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

std::vector<std::uint8_t> WorldResourceProbe::read(const std::string& resourcePath) const {
  (void)resourcePath;
  return {};
}

bool FixedResourceProbe::reachable(const std::string& resourcePath) const {
  for (const auto& resource : present_) {
    if (resource.path == resourcePath) return true;
  }
  return false;
}

std::vector<std::uint8_t> FixedResourceProbe::read(const std::string& resourcePath) const {
  for (const auto& resource : present_) {
    if (resource.path == resourcePath) return resource.bytes;
  }
  return {};
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

bool worldCubemapVtfPath(const std::string& mapName, const int origin[3], std::string& out) {
  // The name is the sample's integer coordinates, sign and all: the BSP's own
  // patch materials refer to the same stem through $envmap, so a name built any
  // other way would resolve against nothing. An empty map name or a null origin
  // is a caller bug, reported as such rather than as a missing file.
  if (mapName.empty() || origin == nullptr) return false;
  out = "materials/maps/" + mapName + "/c" + std::to_string(origin[0]) + "_"
    + std::to_string(origin[1]) + "_" + std::to_string(origin[2]) + ".vtf";
  return true;
}

namespace {

// Little-endian readers over a byte range. The ZIP structures are all
// little-endian and unaligned, so memcpy-style assembly is the portable form.
std::uint16_t zip16(const std::vector<std::uint8_t>& bytes, std::size_t at) {
  return static_cast<std::uint16_t>(bytes[at] | (static_cast<std::uint16_t>(bytes[at + 1]) << 8));
}

std::uint32_t zip32(const std::vector<std::uint8_t>& bytes, std::size_t at) {
  return static_cast<std::uint32_t>(bytes[at])
    | (static_cast<std::uint32_t>(bytes[at + 1]) << 8)
    | (static_cast<std::uint32_t>(bytes[at + 2]) << 16)
    | (static_cast<std::uint32_t>(bytes[at + 3]) << 24);
}

} // namespace

bool bspPakfileEntry(const std::vector<std::uint8_t>& bspBytes, const std::string& name,
    std::vector<std::uint8_t>& out, std::string* reason) {
  out.clear();
  const auto fail = [&](const std::string& message) {
    if (reason) *reason = message;
    return false;
  };
  if (bspBytes.size() < 8 + 64 * 16) return false;
  // Lump 40 is the pakfile. The header's lump table is 64 entries of
  // offset(4)+length(4)+version(4)+fourcc(4) starting at byte 8.
  const std::size_t tableAt = 8 + 40 * 16;
  if (tableAt + 16 > bspBytes.size()) return false;
  const std::uint32_t offset = zip32(bspBytes, tableAt);
  const std::uint32_t length = zip32(bspBytes, tableAt + 4);
  if (offset == 0 || length == 0) return false;
  if (static_cast<std::uint64_t>(offset) + length > bspBytes.size()) return false;
  // The lump is a ZIP with no leading garbage: the first local file header sits
  // at its start and the central directory at its end. Walk the central
  // directory rather than the local headers so a stored payload with a data
  // descriptor cannot desynchronise the scan.
  const std::uint64_t lumpEnd = static_cast<std::uint64_t>(offset) + length;
  std::size_t cursor = static_cast<std::size_t>(lumpEnd) - 22; // end-of-central-directory
  while (cursor >= offset + 4 && cursor + 22 <= static_cast<std::size_t>(lumpEnd)) {
    if (bspBytes[cursor] == 'P' && bspBytes[cursor + 1] == 'K' && bspBytes[cursor + 2] == 5
        && bspBytes[cursor + 3] == 6) {
      break;
    }
    --cursor;
  }
  if (cursor < offset + 4 || cursor + 22 > static_cast<std::size_t>(lumpEnd)) return false;
  // The end-of-central-directory record: the entry counts are two-byte fields
  // and the directory size and offset are four-byte ones. Reading the count as
  // four bytes swallows the low half of the size that follows it, which is how
  // a one-entry archive reads as 5636097 entries and every lookup fails -- a
  // bug the synthetic fixture found, and one the real map could not have: its
  // cubemaps were being asked for by a name that never existed.
  const std::uint32_t entryCount = zip16(bspBytes, cursor + 10);
  const std::uint32_t directoryOffset = zip32(bspBytes, cursor + 16);
  if (entryCount == 0 || entryCount > 100000u) return false;
  std::size_t at = offset + directoryOffset;
  for (std::uint32_t index = 0; index < entryCount; ++index) {
    if (at + 46 > static_cast<std::size_t>(lumpEnd)) return false;
    if (!(bspBytes[at] == 'P' && bspBytes[at + 1] == 'K' && bspBytes[at + 2] == 1
        && bspBytes[at + 3] == 2)) {
      return false;
    }
    const std::uint16_t method = zip16(bspBytes, at + 10);
    const std::uint32_t compressedSize = zip32(bspBytes, at + 20);
    const std::uint32_t uncompressedSize = zip32(bspBytes, at + 24);
    const std::uint32_t localOffset = zip32(bspBytes, at + 42);
    const std::uint16_t nameLength = zip16(bspBytes, at + 28);
    const std::uint16_t extraLength = zip16(bspBytes, at + 30);
    const std::uint16_t commentLength = zip16(bspBytes, at + 32);
    if (at + 46 + nameLength > static_cast<std::size_t>(lumpEnd)) return false;
    const std::string entryName(reinterpret_cast<const char*>(bspBytes.data()) + at + 46,
      nameLength);
    const std::size_t next = at + 46 + nameLength + extraLength + commentLength;
    if (entryName == name) {
      const std::size_t localAt = offset + localOffset;
      if (localAt + 30 > static_cast<std::size_t>(lumpEnd)) return fail("pakfile entry header is out of bounds");
      if (!(bspBytes[localAt] == 'P' && bspBytes[localAt + 1] == 'K' && bspBytes[localAt + 2] == 3
          && bspBytes[localAt + 3] == 4)) {
        return fail("pakfile entry has no local header");
      }
      const std::uint16_t localNameLength = zip16(bspBytes, localAt + 26);
      const std::uint16_t localExtraLength = zip16(bspBytes, localAt + 28);
      const std::size_t dataAt = localAt + 30 + localNameLength + localExtraLength;
      if (dataAt + compressedSize > static_cast<std::size_t>(lumpEnd)) {
        return fail("pakfile entry data runs past the pakfile lump");
      }
      if (method == 0) {
        out.assign(bspBytes.begin() + static_cast<std::ptrdiff_t>(dataAt),
          bspBytes.begin() + static_cast<std::ptrdiff_t>(dataAt + compressedSize));
        return true;
      }
      if (method == 14) {
        return decodeZipLzma(bspBytes.data() + dataAt, compressedSize, uncompressedSize, out, reason);
      }
      // Deflate and everything else is refused by name: a reader that guessed at
      // an unfamiliar method would hand back bytes that decode to a picture
      // nobody wrote, and the picture would look like a finding.
      return fail("pakfile entry uses compression method " + std::to_string(method)
        + ", which this reader does not decode");
    }
    at = next;
  }
  return false;
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
    const WorldResourceProbe& probe, const std::vector<std::uint8_t>& bspBytes) {
  WorldCubemapReading reading;
  reading.samples = map.cubemaps.size();
  reading.perSample.reserve(map.cubemaps.size());
  for (std::size_t i = 0; i < map.cubemaps.size(); ++i) {
    WorldCubemapSampleReading sample;
    sample.index = static_cast<int>(i);
    sample.origin[0] = map.cubemaps[i].origin[0];
    sample.origin[1] = map.cubemaps[i].origin[1];
    sample.origin[2] = map.cubemaps[i].origin[2];
    if (!worldCubemapVtfPath(mapName, sample.origin, sample.vtfPath)) {
      sample.firstMissing = "cubemap path rejected";
      reading.perSample.push_back(std::move(sample));
      continue;
    }
    // Three stores, in the order the engine itself would look: the loose tf
    // directory, then the VPKs, then the map's own pakfile. Which one answered
    // is recorded because "missing from the VPK" and "missing from the map's
    // own pakfile" are different failures.
    std::vector<std::uint8_t> bytes;
    if (probe.reachable(sample.vtfPath)) {
      sample.reachable = true;
      bytes = probe.read(sample.vtfPath);
      sample.source = "loose-or-vpk";
    }
    if (bytes.empty()) {
      std::vector<std::uint8_t> embedded;
      if (bspPakfileEntry(bspBytes, sample.vtfPath, embedded)) {
        bytes = std::move(embedded);
        sample.reachable = true;
        sample.source = "bsp-pakfile";
      }
    }
    if (!sample.reachable) {
      sample.firstMissing = sample.vtfPath;
      reading.perSample.push_back(std::move(sample));
      continue;
    }
    if (bytes.empty()) {
      sample.firstMissing = "reachable but unread: " + sample.vtfPath;
      if (reading.firstUnparsedReason.empty()) {
        reading.firstUnparsedReason = sample.firstMissing + " (source " + sample.source + ")";
      }
      ++reading.samplesUnparsed;
      reading.perSample.push_back(std::move(sample));
      continue;
    }
    sample.bytesRead = true;
    tf2::native::VtfTexture texture;
    std::string error;
    if (!texture.parse(bytes, &error)) {
      sample.firstMissing = "vtf did not parse: " + error;
      if (reading.firstUnparsedReason.empty()) {
        reading.firstUnparsedReason = sample.vtfPath + ": " + error;
      }
      ++reading.samplesUnparsed;
      reading.perSample.push_back(std::move(sample));
      continue;
    }
    sample.parsed = true;
    sample.width = texture.header().width;
    sample.height = texture.header().height;
    sample.format = texture.header().format;
    sample.mipCount = texture.header().mipCount;
    // Face slots the image section actually holds, from the header's own
    // arithmetic against the file's length, and what the header's rule would
    // have predicted. Every cubemap cp_snakewater_final1 ships measures seven
    // and declares six, so the two are reported side by side rather than one
    // being taken for the other.
    sample.facesInFile = texture.header().faceCount;
    sample.declaredFaces = texture.header().declaredFaceCount();
    for (int face = 0; face < 6; ++face) {
      std::uint32_t faceWidth = 0;
      std::uint32_t faceHeight = 0;
      const auto rgba = texture.decodeCubeFaceRgba(bytes, face, faceWidth, faceHeight, &error);
      if (!rgba.empty() && faceWidth == texture.header().width
          && faceHeight == texture.header().height) {
        ++sample.facesDecoded;
        std::uint64_t luminance = 0;
        std::uint32_t checksum = 2166136261u;
        for (std::size_t pixel = 0; pixel + 4 <= rgba.size(); pixel += 4) {
          luminance += rgba[pixel] + rgba[pixel + 1] + rgba[pixel + 2];
          for (std::size_t channel = 0; channel < 4; ++channel) {
            checksum = (checksum ^ rgba[pixel + channel]) * 16777619u;
          }
        }
        const std::size_t pixels = rgba.size() / 4u;
        sample.faceMean[face] = pixels == 0 ? 0
          : static_cast<int>(luminance / (pixels * 3u));
        sample.faceChecksum[face] = checksum;
      } else if (sample.firstMissing.empty()) {
        sample.firstMissing = "face " + std::to_string(face) + " did not decode: " + error;
      }
    }
    // Every decoded face is counted, including the five of a file that lost its
    // sixth: "five of six" is the reading that names the loss, and counting only
    // complete samples would round it back to zero.
    reading.facesDecoded += sample.facesDecoded;
    if (sample.facesDecoded == 6) {
      ++reading.samplesComplete;
      if (sample.source == "bsp-pakfile") ++reading.samplesFromPakfile;
      else ++reading.samplesFromProbe;
      // Face slots beyond the six a cube needs. Counted on complete samples
      // only: a file too short to hold six has no meaningful seventh.
      if (sample.facesInFile > 6) reading.facesBeyondSix += sample.facesInFile - 6;
    } else {
      ++reading.samplesUnparsed;
      if (reading.firstUnparsedReason.empty()) {
        reading.firstUnparsedReason = sample.vtfPath + ": " + sample.firstMissing;
      }
    }
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
    if (!probe.reachable(path)) {
      if (reading.firstMissing.empty()) reading.firstMissing = path;
      continue;
    }
    ++reading.vmtFound;
    // The VMT is there; resolve what it draws with. This is the step that turns
    // "six files exist" into "six textures decode", and it is done with the same
    // VmtParser and VtfTexture the renderer will use, so a reading here and a
    // picture there cannot disagree about which file they loaded.
    const auto vmtBytes = probe.read(path);
    if (vmtBytes.empty()) {
      if (reading.firstTextureProblem.empty()) reading.firstTextureProblem = "vmt unreadable: " + path;
      continue;
    }
    VmtMaterial material;
    if (!VmtParser::parse(std::string(vmtBytes.begin(), vmtBytes.end()), material)) {
      if (reading.firstTextureProblem.empty()) reading.firstTextureProblem = "vmt did not parse: " + path;
      continue;
    }
    if (material.baseTexture.empty()) {
      if (reading.firstTextureProblem.empty()) reading.firstTextureProblem = "vmt declares no $basetexture: " + path;
      continue;
    }
    const auto texturePath = VmtParser::resourcePath(material.baseTexture, ".vtf");
    const auto textureBytes = probe.read(texturePath);
    if (textureBytes.empty()) {
      if (reading.firstTextureProblem.empty()) reading.firstTextureProblem = "texture unreadable: " + texturePath;
      continue;
    }
    ++reading.texturesResolved;
    VtfTexture texture;
    std::string error;
    if (!texture.parse(textureBytes, &error) || texture.header().isCubemap()) {
      if (reading.firstTextureProblem.empty()) {
        reading.firstTextureProblem = texturePath + (error.empty()
          ? ": is a cubemap, which a sky face is not" : ": " + error);
      }
      continue;
    }
    const auto rgba = texture.decodeRgba(textureBytes, &error);
    if (rgba.empty()) {
      if (reading.firstTextureProblem.empty()) reading.firstTextureProblem = texturePath + ": " + error;
      continue;
    }
    ++reading.texturesDecoded;
    reading.decodedBytes += rgba.size();
    if (reading.firstWidth == 0) {
      reading.firstWidth = texture.header().width;
      reading.firstHeight = texture.header().height;
    }
    if (reading.smallestWidth == 0
        || static_cast<std::uint64_t>(texture.header().width) * texture.header().height
          < static_cast<std::uint64_t>(reading.smallestWidth) * reading.smallestHeight) {
      reading.smallestWidth = texture.header().width;
      reading.smallestHeight = texture.header().height;
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
    const std::vector<std::uint8_t>& bspBytes, std::size_t triangleLimit) {
  WorldCapabilityReadings readings;
  readings.lightmap = measureLightmap(map);
  readings.cubemap = measureCubemaps(map, mapName, probe, bspBytes);
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
      + " env_cubemap samples decoded all six faces";
    if (r.facesBeyondSix > 0) {
      verdict.reason += "; " + counts(r.facesBeyondSix)
        + " face slots beyond the six a cube needs";
    }
    return verdict;
  }
  verdict.state = WorldCapabilityState::Fallback;
  verdict.reason = "0 of " + counts(r.samples)
    + " env_cubemap samples decoded all six faces";
  // "The file is not there" and "the file is there and does not parse" are
  // different failures and name different fixes, so the unparsed reason goes
  // first when it exists.
  if (!r.firstUnparsedReason.empty()) {
    verdict.reason += "; " + r.firstUnparsedReason;
  }
  for (const auto& sample : r.perSample) {
    if (!sample.firstMissing.empty()) {
      verdict.reason += "; first missing " + sample.firstMissing;
      break;
    }
  }
  return verdict;
}

WorldCapabilityVerdict judgeSky(const WorldSkyReading& r) {
  // The two failures below the declaration are separate on purpose: "the six
  // vmt files are not there" and "they are there and the textures behind them
  // do not decode" lead to different fixes, and a single 'sky unavailable'
  // would leave a reader with neither.
  WorldCapabilityVerdict verdict;
  if (!r.declared) {
    verdict.state = WorldCapabilityState::Unavailable;
    verdict.reason = "the entity lump names no skyname";
    return verdict;
  }
  if (r.vmtFound < 6) {
    verdict.state = WorldCapabilityState::Fallback;
    verdict.reason = std::to_string(r.vmtFound) + " of 6 skybox vmts resolve for " + r.name;
    if (!r.firstMissing.empty()) verdict.reason += "; first missing " + r.firstMissing;
    return verdict;
  }
  if (r.texturesDecoded < 6) {
    verdict.state = WorldCapabilityState::Fallback;
    verdict.reason = "all six skybox vmts resolve for " + r.name + " and "
      + std::to_string(r.texturesDecoded) + " of their textures decode";
    if (!r.firstTextureProblem.empty()) verdict.reason += "; " + r.firstTextureProblem;
    return verdict;
  }
  verdict.state = WorldCapabilityState::Active;
  verdict.reason = "all six skybox vmts resolve for " + r.name + " and their "
    + std::to_string(r.firstWidth) + "x" + std::to_string(r.firstHeight)
    + ".." + std::to_string(r.smallestWidth) + "x" + std::to_string(r.smallestHeight)
    + " textures decode to " + counts(r.decodedBytes) + " bytes";
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

std::uint32_t crc32(const std::vector<std::uint8_t>& bytes) {
  // Bitwise, no table: the fixture writes a handful of small files and a wrong
  // CRC would make the pakfile unreadable to every tool except the one reader
  // that ignores it, which is exactly the kind of self-consistent fixture this
  // module avoids.
  std::uint32_t crc = 0xFFFFFFFFu;
  for (const std::uint8_t byte : bytes) {
    crc ^= byte;
    for (int bit = 0; bit < 8; ++bit) {
      crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
  }
  return crc ^ 0xFFFFFFFFu;
}

void put16v(std::vector<std::uint8_t>& bytes, std::uint16_t value) {
  bytes.push_back(static_cast<std::uint8_t>(value & 0xFFu));
  bytes.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFFu));
}
void put32v(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
  for (int byte = 0; byte < 4; ++byte) {
    bytes.push_back(static_cast<std::uint8_t>((value >> (byte * 8)) & 0xFFu));
  }
}

// A ZIP archive of stored entries, which is what a Source pakfile lump is. The
// fixture writes one so the product's own pakfile reader has something real to
// read: a fixture that only listed a path would exercise reachability and never
// the reader, and the reader is the part that has to be right for a TF2 map.
std::vector<std::uint8_t> buildStoredZip(const std::map<std::string, std::vector<std::uint8_t>>& entries) {
  struct Record { std::string name; std::uint32_t crc = 0; std::uint32_t size = 0; std::uint32_t offset = 0; };
  std::vector<Record> records;
  std::vector<std::uint8_t> zip;
  for (const auto& [name, content] : entries) {
    Record record;
    record.name = name;
    record.crc = crc32(content);
    record.size = static_cast<std::uint32_t>(content.size());
    record.offset = static_cast<std::uint32_t>(zip.size());
    zip.insert(zip.end(), {'P', 'K', 3, 4});
    put16v(zip, 20);                                    // version needed
    put16v(zip, 0);                                     // flags
    put16v(zip, 0);                                     // method: stored
    put16v(zip, 0);                                     // mod time
    put16v(zip, 0);                                     // mod date
    put32v(zip, record.crc);
    put32v(zip, record.size);                           // compressed size
    put32v(zip, record.size);                           // uncompressed size
    put16v(zip, static_cast<std::uint16_t>(name.size()));
    put16v(zip, 0);                                     // extra length
    zip.insert(zip.end(), name.begin(), name.end());
    zip.insert(zip.end(), content.begin(), content.end());
    records.push_back(std::move(record));
  }
  const auto directoryOffset = static_cast<std::uint32_t>(zip.size());
  for (const auto& record : records) {
    zip.insert(zip.end(), {'P', 'K', 1, 2});
    put16v(zip, 20);                                    // version made by
    put16v(zip, 20);                                    // version needed
    put16v(zip, 0);                                     // flags
    put16v(zip, 0);                                     // method
    put16v(zip, 0);                                     // mod time
    put16v(zip, 0);                                     // mod date
    put32v(zip, record.crc);
    put32v(zip, record.size);
    put32v(zip, record.size);
    put16v(zip, static_cast<std::uint16_t>(record.name.size()));
    put16v(zip, 0);                                     // extra
    put16v(zip, 0);                                     // comment
    put16v(zip, 0);                                     // disk
    put16v(zip, 0);                                     // internal attrs
    put32v(zip, 0);                                     // external attrs
    put32v(zip, record.offset);
    zip.insert(zip.end(), record.name.begin(), record.name.end());
  }
  const auto directorySize = static_cast<std::uint32_t>(zip.size()) - directoryOffset;
  zip.insert(zip.end(), {'P', 'K', 5, 6});
  put16v(zip, 0);                                       // this disk
  put16v(zip, 0);                                       // directory start disk
  put16v(zip, static_cast<std::uint16_t>(records.size()));
  put16v(zip, static_cast<std::uint16_t>(records.size()));
  put32v(zip, directorySize);
  put32v(zip, directoryOffset);
  put16v(zip, 0);                                       // comment length
  return zip;
}

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
  add(40, fixture.pakfileEntries.empty()
    ? std::vector<std::uint8_t>{}
    : buildStoredZip(fixture.pakfileEntries));
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
