#include "bsp_map.h"
#include "LzmaDec.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <unordered_map>

namespace tf2::native {
namespace {
std::string gLzmaError;
struct Lump { std::int32_t offset = 0; std::int32_t length = 0; };
using Bytes = std::vector<std::uint8_t>;
constexpr std::size_t kHeaderSize = 8 + 64 * 16 + 4;
constexpr int kLumpTexData = 2, kLumpVertices = 3, kLumpTexInfo = 6, kLumpFaces = 7,
  kLumpLighting = 8,
  kLumpEdges = 12, kLumpSurfEdges = 13, kLumpModels = 14,
  kLumpStringData = 43, kLumpStringTable = 44;

template <typename T> T read(const Bytes& b, std::size_t at) {
  T value{}; std::memcpy(&value, b.data() + at, sizeof(T)); return value;
}
BspVertex vertex(const Bytes& b, std::size_t at) {
  return { read<float>(b, at), read<float>(b, at + 4), read<float>(b, at + 8) };
}
std::string readString(const Bytes& bytes, std::int32_t offset) {
  if (offset < 0 || static_cast<std::size_t>(offset) >= bytes.size()) return {};
  const auto begin = bytes.begin() + offset;
  const auto end = std::find(begin, bytes.end(), static_cast<std::uint8_t>(0));
  return std::string(begin, end);
}

void* lzmaAlloc(ISzAllocPtr, size_t size) { return std::malloc(size); }
void lzmaFree(ISzAllocPtr, void* address) { std::free(address); }

bool decodeRawLzma(const std::uint8_t* properties, const std::uint8_t* compressed,
    std::size_t compressedSize, std::size_t outputSize, Bytes& result) {
  if (outputSize == 0 || outputSize > 128u * 1024u * 1024u || compressedSize == 0) return false;
  ISzAlloc allocator{lzmaAlloc, lzmaFree};
  result.resize(outputSize);
  SizeT inputSize = compressedSize;
  SizeT decodedSize = outputSize;
  ELzmaStatus status = LZMA_STATUS_NOT_SPECIFIED;
  const SRes decoded = LzmaDecode(result.data(), &decodedSize, compressed, &inputSize,
      properties, LZMA_PROPS_SIZE, LZMA_FINISH_ANY, &status, &allocator);
  if (decoded != SZ_OK || decodedSize != outputSize) {
    gLzmaError = "decode:" + std::to_string(decoded) + "/" + std::to_string(decodedSize) + "/" + std::to_string(outputSize) + "/status:" + std::to_string(status);
    result.clear(); return false;
  }
  return true;
}

bool decodeLump(const Bytes& file, Lump lump, Bytes& result) {
  if (lump.offset < 0 || lump.length < 0) return false;
  const auto offset = static_cast<std::size_t>(lump.offset), size = static_cast<std::size_t>(lump.length);
  if (offset > file.size() || size > file.size() - offset) return false;
  if (size < 4 || std::memcmp(file.data() + offset, "LZMA", 4) != 0) {
    result.assign(file.begin() + offset, file.begin() + offset + size); return true;
  }
  if (size < 17) return false;
  const auto outputSize = read<std::uint32_t>(file, offset + 4);
  const auto packed = read<std::uint32_t>(file, offset + 8);
  if (packed != size - 17) return false;
  return decodeRawLzma(file.data() + offset + 12, file.data() + offset + 17,
      size - 17, outputSize, result);
}

bool tryDecode(const Bytes& file, Lump lump, Bytes& result) {
  result.clear();
  if (lump.length <= 0) return false;
  return decodeLump(file, lump, result);
}

std::string quotedValue(const std::string& text, const char* key) {
  const std::string token = std::string("\"") + key + "\"";
  const auto at = text.find(token);
  if (at == std::string::npos) return {};
  const auto open = text.find('"', at + token.size());
  if (open == std::string::npos) return {};
  const auto close = text.find('"', open + 1);
  if (close == std::string::npos) return {};
  return text.substr(open + 1, close - open - 1);
}

BspVertex lerpVertex(const BspVertex& a, const BspVertex& b, float t) {
  return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t};
}

void writeLuxel(std::uint8_t* dest, const std::uint8_t* sample, std::uint64_t& toneSum, std::size_t& toneCount) {
  const int exponent = static_cast<int>(static_cast<std::int8_t>(sample[3]));
  const float scale = std::ldexp(1.0f, exponent) / 255.0f;
  for (int channel = 0; channel < 3; ++channel) {
    const float decoded = static_cast<float>(sample[channel]) * scale;
    std::uint8_t byte = 0;
    if (decoded >= 1.0f) byte = 255;
    else if (decoded > 0.0f) byte = static_cast<std::uint8_t>(decoded * 255.0f + 0.5f);
    dest[channel] = byte;
    toneSum += byte;
    ++toneCount;
  }
  dest[3] = 255;
}
}

bool BspParser::parse(const std::vector<std::uint8_t>& bytes, BspMap& map,
    std::size_t triangleLimit, int atlasMaxWidth) {
  map = {}; auto fail = [&](const char* message) { map.error = message; return false; };
  if (bytes.size() < kHeaderSize || read<std::uint32_t>(bytes, 0) != 0x50534256u) return fail("BSP header is invalid");
  map.version = read<std::int32_t>(bytes, 4); map.revision = read<std::int32_t>(bytes, 8 + 64 * 16);
  Lump table[64]{}; for (int i = 0; i < 64; ++i) { table[i].offset = read<std::int32_t>(bytes, 8 + i * 16); table[i].length = read<std::int32_t>(bytes, 8 + i * 16 + 4); }
  Bytes texData, vertices, texInfo, faces, edges, surfEdges, models, stringData, stringTable, lighting;
  if (!decodeLump(bytes, table[kLumpTexData], texData)) { map.error = std::string("BSP texdata LZMA decode failed/") + gLzmaError; return false; }
  if (!decodeLump(bytes, table[kLumpVertices], vertices)) return fail("BSP vertices LZMA decode failed");
  if (!decodeLump(bytes, table[kLumpTexInfo], texInfo)) return fail("BSP texinfo LZMA decode failed");
  if (!decodeLump(bytes, table[kLumpFaces], faces)) return fail("BSP faces LZMA decode failed");
  if (!decodeLump(bytes, table[kLumpLighting], lighting)) return fail("BSP lighting LZMA decode failed");
  if (!decodeLump(bytes, table[kLumpEdges], edges)) return fail("BSP edges LZMA decode failed");
  if (!decodeLump(bytes, table[kLumpSurfEdges], surfEdges)) return fail("BSP surfedges LZMA decode failed");
  if (!decodeLump(bytes, table[kLumpModels], models)) return fail("BSP models LZMA decode failed");
  if (!decodeLump(bytes, table[kLumpStringData], stringData)) return fail("BSP string data LZMA decode failed");
  if (!decodeLump(bytes, table[kLumpStringTable], stringTable)) return fail("BSP string table LZMA decode failed");
  if (vertices.size() % 12 || texInfo.size() % 72 || faces.size() % 56 || edges.size() % 4 || surfEdges.size() % 4 || models.size() % 48 || texData.size() % 32 || stringTable.size() % 4 || models.empty()) return fail("BSP lump layout is invalid");
  Bytes entities, planes, vis, nodes, leafs, leafFaceBytes, dispInfo, dispVerts, cubemapBytes;
  tryDecode(bytes, table[0], entities);
  tryDecode(bytes, table[1], planes);
  tryDecode(bytes, table[4], vis);
  tryDecode(bytes, table[5], nodes);
  tryDecode(bytes, table[10], leafs);
  tryDecode(bytes, table[16], leafFaceBytes);
  tryDecode(bytes, table[26], dispInfo);
  tryDecode(bytes, table[33], dispVerts);
  tryDecode(bytes, table[42], cubemapBytes);
  if (!entities.empty()) map.skyName = quotedValue(std::string(entities.begin(), entities.end()), "skyname");
  if (planes.size() % 20 == 0) {
    map.planes.reserve(planes.size() / 20);
    for (std::size_t i = 0; i < planes.size() / 20; ++i) {
      BspPlane plane;
      plane.normal[0] = read<float>(planes, i * 20);
      plane.normal[1] = read<float>(planes, i * 20 + 4);
      plane.normal[2] = read<float>(planes, i * 20 + 8);
      plane.dist = read<float>(planes, i * 20 + 12);
      map.planes.push_back(plane);
    }
  }
  if (nodes.size() % 32 == 0) {
    map.nodes.reserve(nodes.size() / 32);
    for (std::size_t i = 0; i < nodes.size() / 32; ++i) {
      BspNode node;
      node.plane = read<std::int32_t>(nodes, i * 32);
      node.child[0] = read<std::int32_t>(nodes, i * 32 + 4);
      node.child[1] = read<std::int32_t>(nodes, i * 32 + 8);
      map.nodes.push_back(node);
    }
  }
  if (leafFaceBytes.size() % 2 == 0) {
    map.leafFaces.resize(leafFaceBytes.size() / 2);
    for (std::size_t i = 0; i < map.leafFaces.size(); ++i) map.leafFaces[i] = read<std::uint16_t>(leafFaceBytes, i * 2);
  }
  std::vector<int> faceCluster(faces.size() / 56, -1);
  if (leafs.size() % 32 == 0) {
    map.leaves.reserve(leafs.size() / 32);
    for (std::size_t i = 0; i < leafs.size() / 32; ++i) {
      BspLeaf leaf;
      leaf.cluster = read<std::int16_t>(leafs, i * 32 + 4);
      leaf.firstFace = read<std::uint16_t>(leafs, i * 32 + 20);
      leaf.faceCount = read<std::uint16_t>(leafs, i * 32 + 22);
      map.leaves.push_back(leaf);
      if (leaf.firstFace < 0 || leaf.faceCount < 0) continue;
      for (int face = 0; face < leaf.faceCount; ++face) {
        const auto index = static_cast<std::size_t>(leaf.firstFace) + static_cast<std::size_t>(face);
        if (index >= map.leafFaces.size()) break;
        const auto faceIndex = map.leafFaces[index];
        if (faceIndex < faceCluster.size() && faceCluster[faceIndex] < 0) faceCluster[faceIndex] = leaf.cluster;
      }
    }
  }
  if (vis.size() >= 4) {
    const auto clusters = read<std::int32_t>(vis, 0);
    const auto headerBytes = static_cast<std::size_t>(4) + static_cast<std::size_t>(clusters) * 8u;
    if (clusters > 0 && clusters < 1000000 && headerBytes <= vis.size()) {
      map.visClusterCount = clusters;
      map.visOffsets.resize(static_cast<std::size_t>(clusters));
      map.visRawOffsets.assign(static_cast<std::size_t>(clusters), -1);
      bool offsetsOk = true;
      for (int cluster = 0; cluster < clusters; ++cluster) {
        // T11: dvis_t is { int numclusters; int bitofs[2][numclusters]; }. The
        // two tables are contiguous, so [0][c] is at 4 + c*4 and [1][c] at
        // 4 + numclusters*4 + c*4. The previous reader stepped bitofs[0] by 8
        // bytes, which lands on [0][2c] -- every cluster decoded another
        // cluster's row -- and then ran a run-length decoder over data that
        // this map stores uncompressed ([1][c] >= 0 for all c, measured).
        const auto compressedOffset = read<std::int32_t>(vis, 4 + static_cast<std::size_t>(cluster) * 4u);
        const auto rawOffset = read<std::int32_t>(vis,
          4 + static_cast<std::size_t>(clusters) * 4u + static_cast<std::size_t>(cluster) * 4u);
        if (rawOffset < 0 && (compressedOffset < 0 || static_cast<std::size_t>(compressedOffset) >= vis.size())) offsetsOk = false;
        if (rawOffset >= 0 && static_cast<std::size_t>(rawOffset) >= vis.size()) offsetsOk = false;
        map.visOffsets[static_cast<std::size_t>(cluster)] = compressedOffset;
        map.visRawOffsets[static_cast<std::size_t>(cluster)] = rawOffset;
      }
      if (offsetsOk) { map.visBits = vis; map.visMode = BspVisMode::ClusterPvs; }
      else { map.visOffsets.clear(); map.visClusterCount = 0; }
    }
  }
  if (cubemapBytes.size() % 16 == 0) {
    map.cubemaps.reserve(cubemapBytes.size() / 16);
    for (std::size_t i = 0; i < cubemapBytes.size() / 16; ++i) {
      BspCubemapSample sample;
      sample.origin[0] = read<std::int32_t>(cubemapBytes, i * 16);
      sample.origin[1] = read<std::int32_t>(cubemapBytes, i * 16 + 4);
      sample.origin[2] = read<std::int32_t>(cubemapBytes, i * 16 + 8);
      sample.size = cubemapBytes[i * 16 + 12];
      map.cubemaps.push_back(sample);
    }
    map.cubemapCount = map.cubemaps.size();
  }
  const bool dispInfoOk = !dispInfo.empty() && dispInfo.size() % 176 == 0;
  const bool dispVertsOk = dispVerts.size() % 20 == 0;
  const auto firstFace = read<std::int32_t>(models, 40), faceCount = read<std::int32_t>(models, 44); if (firstFace < 0 || faceCount < 0 || static_cast<std::size_t>(firstFace) + static_cast<std::size_t>(faceCount) > faces.size() / 56) return fail("BSP world face range is invalid"); map.faceCount = static_cast<std::size_t>(faceCount);
  std::unordered_map<std::int32_t, std::string> names; for (std::size_t i = 0; i < texData.size() / 32; ++i) { const auto id = read<std::int32_t>(texData, i * 32 + 12); if (id >= 0 && static_cast<std::size_t>(id) < stringTable.size() / 4) names[static_cast<std::int32_t>(i)] = readString(stringData, read<std::int32_t>(stringTable, static_cast<std::size_t>(id) * 4)); }
  const auto vertexCount = vertices.size() / 12, edgeCount = edges.size() / 4, surfEdgeCount = surfEdges.size() / 4;
  // T1 (2026-10-13): the shelf packer used to be hard-coded to 2048 and a face
  // that did not fit was dropped silently. The atlas size is now chosen, and
  // the choice is deliberately conservative: the candidate list starts at the
  // historical 2048 so every map whose lightmaps already fit keeps the atlas
  // dimensions and the packed count its readings are pinned on, and it only
  // grows for a caller that asked for more room (atlasMaxWidth > 2048). A map
  // that still loses faces at the largest allowed size reports the number and
  // the reason rather than a count nobody explains.
  const auto chooseAtlasSize = [&](int maxWidth) {
    // Dry run of the same shelf packer over the same face range: rows of
    // decreasing height, wrapping when a face would cross the right edge. It
    // reads only the two sample counts, so it costs one pass over the lump and
    // no polygon work, and it is the same arithmetic the real pass below uses,
    // which is why the answer cannot disagree with what the pass then does.
    const auto fits = [&](int size) {
      int cursorX = 0, cursorY = 0, rowHeight = 0;
      for (std::int32_t faceIndex = firstFace; faceIndex < firstFace + faceCount; ++faceIndex) {
        const auto base = static_cast<std::size_t>(faceIndex) * 56;
        const auto infoIndex = read<std::int16_t>(faces, base + 10);
        if (infoIndex < 0 || static_cast<std::size_t>(infoIndex) >= texInfo.size() / 72) continue;
        const auto flags = read<std::int32_t>(texInfo, static_cast<std::size_t>(infoIndex) * 72 + 64);
        if ((flags & (2 | 4 | 64 | 128)) != 0) continue;
        const auto lightOffset = read<std::int32_t>(faces, base + 20);
        const auto samplesW = read<std::int32_t>(faces, base + 36) + 1;
        const auto samplesH = read<std::int32_t>(faces, base + 40) + 1;
        const auto styles0 = faces[base + 16];
        const auto lightBytes = samplesW > 0 && samplesH > 0 && samplesW <= 512 && samplesH <= 512
          ? static_cast<std::uint64_t>(samplesW) * static_cast<std::uint64_t>(samplesH) * 4u : 0u;
        if (styles0 == 255 || lightOffset < 0 || lightBytes == 0
            || static_cast<std::uint64_t>(lightOffset) + lightBytes > lighting.size()) continue;
        if (samplesW > size || samplesH > size) return false;
        if (cursorX + samplesW > size) { cursorX = 0; cursorY += rowHeight; rowHeight = 0; }
        if (cursorY + samplesH > size) return false;
        cursorX += samplesW;
        rowHeight = std::max(rowHeight, samplesH);
      }
      return true;
    };
    const int candidates[3] = { 2048, 4096, 8192 };
    for (const int size : candidates) {
      if (size > maxWidth) break;
      if (fits(size)) return size;
    }
    return maxWidth >= 2048 ? maxWidth : 2048;
  };
  const int kAtlas = chooseAtlasSize(atlasMaxWidth);
  map.lightmapAtlasMaxWidth = kAtlas;
  map.lightmapAtlasGrown = kAtlas > 2048;
  std::vector<std::uint8_t> atlas;
  int atlasHeight = 0, cursorX = 0, cursorY = 0, rowHeight = 0;
  std::uint64_t toneSum = 0;
  std::size_t toneCount = 0;
  const auto luxelOf = [](const BspVertex& point, float x0, float x1, float x2, float x3, float y0, float y1, float y2, float y3) {
    return std::array<float, 2>{point.x * x0 + point.y * x1 + point.z * x2 + x3, point.x * y0 + point.y * y1 + point.z * y2 + y3};
  };
  struct PackedLight { bool ok = false; int x = 0, y = 0, samplesW = 0, samplesH = 0, storedW = 0, storedH = 0, minsU = 0, minsV = 0; };
  for (std::int32_t faceIndex = firstFace; faceIndex < firstFace + faceCount; ++faceIndex) {
    const auto base = static_cast<std::size_t>(faceIndex) * 56; const auto firstEdge = read<std::int32_t>(faces, base + 4); const auto count = read<std::int16_t>(faces, base + 8); const auto infoIndex = read<std::int16_t>(faces, base + 10);
    if (count < 3 || firstEdge < 0 || static_cast<std::size_t>(firstEdge) + static_cast<std::size_t>(count) > surfEdgeCount || infoIndex < 0 || static_cast<std::size_t>(infoIndex) >= texInfo.size() / 72) continue;
    const auto flags = read<std::int32_t>(texInfo, static_cast<std::size_t>(infoIndex) * 72 + 64);
    if ((flags & (2 | 4)) != 0) { ++map.skyFaces; continue; }
    if ((flags & (64 | 128)) != 0) continue;
    const bool water = (flags & 8) != 0;
    const auto texDataIndex = read<std::int32_t>(texInfo, static_cast<std::size_t>(infoIndex) * 72 + 68); const auto material = names.count(texDataIndex) ? names[texDataIndex] : std::string{};
    const auto infoBase = static_cast<std::size_t>(infoIndex) * 72;
    const float u0 = read<float>(texInfo, infoBase + 0), u1 = read<float>(texInfo, infoBase + 4), u2 = read<float>(texInfo, infoBase + 8), u3 = read<float>(texInfo, infoBase + 12);
    const float v0 = read<float>(texInfo, infoBase + 16), v1 = read<float>(texInfo, infoBase + 20), v2 = read<float>(texInfo, infoBase + 24), v3 = read<float>(texInfo, infoBase + 28);
    const float lu0 = read<float>(texInfo, infoBase + 32), lu1 = read<float>(texInfo, infoBase + 36), lu2 = read<float>(texInfo, infoBase + 40), lu3 = read<float>(texInfo, infoBase + 44);
    const float lv0 = read<float>(texInfo, infoBase + 48), lv1 = read<float>(texInfo, infoBase + 52), lv2 = read<float>(texInfo, infoBase + 56), lv3 = read<float>(texInfo, infoBase + 60);
    const auto lightOffset = read<std::int32_t>(faces, base + 20);
    const auto samplesW = read<std::int32_t>(faces, base + 36) + 1;
    const auto samplesH = read<std::int32_t>(faces, base + 40) + 1;
    const auto minsU = read<std::int32_t>(faces, base + 28);
    const auto minsV = read<std::int32_t>(faces, base + 32);
    const auto styles0 = faces[base + 16];
    const auto lightBytes = samplesW > 0 && samplesH > 0 && samplesW <= 512 && samplesH <= 512
      ? static_cast<std::uint64_t>(samplesW) * static_cast<std::uint64_t>(samplesH) * 4u : 0u;
    const bool hasLightmap = styles0 != 255 && lightOffset >= 0 && lightBytes > 0
      && static_cast<std::uint64_t>(lightOffset) + lightBytes <= lighting.size();
    std::vector<BspVertex> polygon; polygon.reserve(static_cast<std::size_t>(count));
    bool validFace = true;
    for (std::int16_t i = 0; i < count; ++i) {
      const auto surf = read<std::int32_t>(surfEdges,
        (static_cast<std::size_t>(firstEdge) + i) * 4);
      // A negative surfedge names the same edge with its endpoints reversed.
      // Source stores the edge number as the absolute value; the sign only
      // selects which endpoint is read first.
      const auto edge = surf < 0 ? -surf : surf;
      if (edge < 0 || static_cast<std::size_t>(edge) >= edgeCount) {
        validFace = false;
        break;
      }
      const auto vertexIndex = read<std::uint16_t>(edges,
        static_cast<std::size_t>(edge) * 4 + (surf < 0 ? 2 : 0));
      if (vertexIndex >= vertexCount) {
        validFace = false;
        break;
      }
      polygon.push_back(vertex(vertices, static_cast<std::size_t>(vertexIndex) * 12));
    }
    if (!validFace || polygon.size() < 3) continue;
    // T10 winding census: the polygon's normal (from its own winding) against
    // the face's BSP plane. dface_t: planenum is a uint16 at +0, the back-side
    // flag a byte at +2. This counts the SOURCE winding -- it is a measurement
    // of the shipped data, not of what the renderer decides.
    const std::uint32_t facePlaneNumber = read<std::uint16_t>(faces, base);
    const std::uint8_t facePlaneSide = faces[base + 2];
    // T11 third round: the face class is a property of the dface_t itself, so it
    // is read here rather than at the displacement branch below -- the winding
    // census needs it while the plane is still in scope.
    const auto dispIndex = read<std::int16_t>(faces, base + 12);
    // T11: the face's own OUTWARD direction -- the direction a player looks
    // along to see this face. `facePlaneSide` is the half-space the face is seen
    // from, so the outward vector is +plane for side 0 and -plane for side 1.
    // It is both the measurement axis and (for displacement, below) the
    // orientation target; `misfaced` counts how far the emitted winding sits
    // from it (15126 of 99312 after the third-round decision, all of them
    // brush/side-1, with 5388 of 16136 faces authored on the back of their
    // plane). Without a usable plane the emission keeps the stored order and the
    // face is not counted.
    float faceOutwardX = 0.0f, faceOutwardY = 0.0f, faceOutwardZ = 0.0f;
    // The raw plane normal and the polygon's own winding normal, kept so the
    // stored-winding census and the grid-vs-polygon census can both be taken
    // without re-deriving them inside the emission lambda.
    float facePlaneX = 0.0f, facePlaneY = 0.0f, facePlaneZ = 0.0f;
    float facePolygonX = 0.0f, facePolygonY = 0.0f, facePolygonZ = 0.0f;
    bool haveFaceOutward = false;
    if (!map.planes.empty()
        && static_cast<std::size_t>(facePlaneNumber) < map.planes.size()) {
      const auto& plane = map.planes[static_cast<std::size_t>(facePlaneNumber)];
      const float pnx = plane.normal[0], pny = plane.normal[1], pnz = plane.normal[2];
      const float ux = polygon[1].x - polygon[0].x, uy = polygon[1].y - polygon[0].y, uz = polygon[1].z - polygon[0].z;
      const float vx = polygon[2].x - polygon[0].x, vy = polygon[2].y - polygon[0].y, vz = polygon[2].z - polygon[0].z;
      const float cx = uy * vz - uz * vy, cy = uz * vx - ux * vz, cz = ux * vy - uy * vx;
      const float len = std::sqrt(cx * cx + cy * cy + cz * cz);
      const float plen = std::sqrt(pnx * pnx + pny * pny + pnz * pnz);
      if (len > 1e-6f && plen > 1e-6f && std::isfinite(pnx) && std::isfinite(pny) && std::isfinite(pnz)) {
        ++map.windingFaceCount;
        if (facePlaneSide != 0) ++map.windingSideOneFaces;
        const bool pointsAlong = (cx * pnx + cy * pny + cz * pnz) > 0.0f;
        if (pointsAlong == (facePlaneSide == 0)) ++map.windingAgreeCount;
        // T11 third round: the same comparison, split by class and WITHOUT the
        // side bit. This is the reading that says whether the shipped polygon
        // order is a convention or a coin toss.
        if (dispIndex >= 0) {
          if (pointsAlong) ++map.windingStoredDispAlong; else ++map.windingStoredDispAgainst;
        } else {
          if (pointsAlong) ++map.windingStoredBrushAlong; else ++map.windingStoredBrushAgainst;
        }
        const float sideSign = facePlaneSide == 0 ? 1.0f : -1.0f;
        faceOutwardX = sideSign * pnx;
        faceOutwardY = sideSign * pny;
        faceOutwardZ = sideSign * pnz;
        facePlaneX = pnx; facePlaneY = pny; facePlaneZ = pnz;
        facePolygonX = cx; facePolygonY = cy; facePolygonZ = cz;
        haveFaceOutward = true;
      }
    }
    if (hasLightmap) {
      ++map.lightmapFaceCount;
      map.lightmapBytes += static_cast<std::size_t>(lightBytes);
      const int storedW = samplesW - 1, storedH = samplesH - 1;
      int inside = 0;
      for (const auto& point : polygon) {
        const auto lux = luxelOf(point, lu0, lu1, lu2, lu3, lv0, lv1, lv2, lv3);
        const bool uOk = lux[0] >= static_cast<float>(minsU) - 1.0f && lux[0] <= static_cast<float>(minsU + storedW) + 1.0f;
        const bool vOk = lux[1] >= static_cast<float>(minsV) - 1.0f && lux[1] <= static_cast<float>(minsV + storedH) + 1.0f;
        if (uOk && vOk) ++inside;
      }
      if (inside * 2 >= static_cast<int>(polygon.size())) ++map.lightmapLuxelFits;
    }
    PackedLight packed;
    if (hasLightmap && samplesW <= kAtlas && samplesH <= kAtlas) {
      if (atlas.empty()) atlas.assign(static_cast<std::size_t>(kAtlas) * kAtlas * 4u, 0);
      if (cursorX + samplesW > kAtlas) { cursorX = 0; cursorY += rowHeight; rowHeight = 0; }
      if (cursorY + samplesH <= kAtlas) {
        packed.ok = true;
        packed.x = cursorX; packed.y = cursorY;
        packed.samplesW = samplesW; packed.samplesH = samplesH;
        packed.storedW = samplesW - 1; packed.storedH = samplesH - 1;
        packed.minsU = minsU; packed.minsV = minsV;
        for (int y = 0; y < samplesH; ++y) {
          for (int x = 0; x < samplesW; ++x) {
            const auto sampleAt = static_cast<std::size_t>(lightOffset) + (static_cast<std::size_t>(y) * static_cast<std::size_t>(samplesW) + static_cast<std::size_t>(x)) * 4u;
            const auto destAt = (static_cast<std::size_t>(cursorY + y) * kAtlas + static_cast<std::size_t>(cursorX + x)) * 4u;
            writeLuxel(atlas.data() + destAt, lighting.data() + sampleAt, toneSum, toneCount);
          }
        }
        cursorX += samplesW;
        rowHeight = std::max(rowHeight, samplesH);
        atlasHeight = std::max(atlasHeight, cursorY + samplesH);
        ++map.lightmapPackedFaces;
      }
    }
    const int cluster = faceIndex >= 0 && static_cast<std::size_t>(faceIndex) < faceCluster.size() ? faceCluster[static_cast<std::size_t>(faceIndex)] : -1;
    const auto texCoord = [&](const BspVertex& point) { return std::array<float, 2>{point.x * u0 + point.y * u1 + point.z * u2 + u3, point.x * v0 + point.y * v1 + point.z * v2 + v3}; };
    const auto lightUv = [&](const BspVertex& point) {
      const auto lux = luxelOf(point, lu0, lu1, lu2, lu3, lv0, lv1, lv2, lv3);
      const auto axis = [&](float luxel, int stored, int samples, int mins, int origin, int atlasSize) {
        float coord = stored > 0 ? (luxel - static_cast<float>(mins)) / static_cast<float>(stored) : 0.5f;
        if (coord < 0.0f) coord = 0.0f; else if (coord > 1.0f) coord = 1.0f;
        const float texel = samples <= 1 ? 0.5f : 0.5f + coord * static_cast<float>(samples - 1);
        return (static_cast<float>(origin) + texel) / static_cast<float>(atlasSize);
      };
      return std::array<float, 2>{
        axis(lux[0], packed.storedW, packed.samplesW, packed.minsU, packed.x, kAtlas),
        axis(lux[1], packed.storedH, packed.samplesH, packed.minsV, packed.y, kAtlas)};
    };
    // T11 third round: the first grid triangle of a displacement face is
    // compared against the polygon's own winding once per face, so a 32x32 grid
    // cannot outvote a quad in the census.
    bool gridCounted = false;
    const auto pushTriangle = [&](BspVertex a, BspVertex b, BspVertex c, bool displacement,
                                  float aAlpha = -1.0f, float bAlpha = -1.0f, float cAlpha = -1.0f) {
      if (map.triangles.size() >= triangleLimit) return;
      bool flipped = false;
      // T11 third round: does the displacement grid's parametrisation keep the
      // polygon's own order? Taken on the FIRST triangle of the face and BEFORE
      // the orientation rule below, so it measures the shipped parametrisation
      // and not the rule that overwrites it.
      if (displacement && !gridCounted && haveFaceOutward) {
        gridCounted = true;
        const float ux = b.x - a.x, uy = b.y - a.y, uz = b.z - a.z;
        const float vx = c.x - a.x, vy = c.y - a.y, vz = c.z - a.z;
        const float nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
        const float dot = nx * facePolygonX + ny * facePolygonY + nz * facePolygonZ;
        if (dot > 0.0f) ++map.windingGridKeepsPolygon;
        else if (dot < 0.0f) ++map.windingGridReversesPolygon;
      }
      // T11: orient the emitted triangle against its face's own side bit.
      //
      // The history matters, because two earlier readings of the same data were
      // both wrong. The first rule reversed every triangle to one global sign
      // and culled half the map. The second split the sign by emission class
      // (brush along its plane normal, displacement against it), on the
      // evidence of the face POLYGON census -- and the census was reading the
      // wrong thing: the brush fan emits (0, i+1, i), the reverse of the
      // polygon's order, so the polygon's convention is not the buffer's. The
      // buffer's own order is one convention for both classes (`geometry
      // pre-rule`: brush 41624 along the plane normal against 396, displacement
      // 46856 against 142), which leaves exactly one thing to say about a face:
      // the side bit. Point the winding along it.
      //
      // The first-hit oracle derives the same rule without using the side bit at
      // all: from above, 5301 of 5381 topmost surfaces are authored with the
      // side-aware direction pointing at the sky (analysis/first-hit-oracle.txt).
      if (haveFaceOutward) {
        const float ux = b.x - a.x, uy = b.y - a.y, uz = b.z - a.z;
        const float vx = c.x - a.x, vy = c.y - a.y, vz = c.z - a.z;
        const float nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
        // T11 third round: the buffer's own order, before any reversal. The
        // brush fan emits (0, i+1, i) -- the reverse of the polygon's order --
        // so this is the only place the fan's orientation can be read.
        const float alongPlane = nx * facePlaneX + ny * facePlaneY + nz * facePlaneZ;
        const float alongOutward = nx * faceOutwardX + ny * faceOutwardY + nz * faceOutwardZ;
        if (displacement) {
          if (alongPlane > 0.0f) ++map.windingPreDispAlongPlane; else if (alongPlane < 0.0f) ++map.windingPreDispAgainstPlane;
          if (alongOutward > 0.0f) ++map.windingPreDispAlongOutward; else if (alongOutward < 0.0f) ++map.windingPreDispAgainstOutward;
        } else {
          if (alongPlane > 0.0f) ++map.windingPreBrushAlongPlane; else if (alongPlane < 0.0f) ++map.windingPreBrushAgainstPlane;
          if (alongOutward > 0.0f) ++map.windingPreBrushAlongOutward; else if (alongOutward < 0.0f) ++map.windingPreBrushAgainstOutward;
        }
        // T11 third round: the buffer ships ONE convention, not two. Measured on
        // the emitted order before this rule (`geometry pre-rule`): brush
        // 41624 along the plane normal against 396, displacement 46856 against
        // 142. The class split the previous rule was built on does not exist in
        // the data -- it was an artifact of reading the face polygon instead of
        // the buffer, and the brush fan emits (0, i+1, i), the reverse of the
        // polygon's own order. With one convention there is one rule: point the
        // winding along the direction the face's own side bit calls its front.
        // That is also what the first-hit oracle derives on its own (see
        // evidence/t11-world-geometry/analysis/first-hit-oracle.txt): from
        // above, 5301 of 5381 topmost surfaces are authored with +F pointing at
        // the sky, i.e. F is the side the surface is seen from.
        // T11 third round: the buffer ships ONE convention, not two. Measured on
        // the emitted order before this rule (`geometry pre-rule`): brush 41624
        // along the plane normal against 396, displacement 46856 against 142.
        // The class split the previous rule was built on does not exist in the
        // buffer -- it came from reading the face POLYGON, and the brush fan
        // emits (0, i+1, i), the reverse of the polygon's own order.
        //
        // What is left is not a choice of philosophy but a measured decision, and
        // it is written out in full in analysis/rule-decision.txt: every rule is
        // a keep/flip choice for four cells (class x side), both first-hit
        // oracles score all sixteen without rendering, and the two views
        // disagree in exactly two cells -- brush/side-1 and displacement/side-0.
        // This is mask 07, the best compromise of the sixteen: brush keeps the
        // map's own order (the POV is its primary consumer and the map's order is
        // what it was authored against), displacement is oriented to the face's
        // side-aware outward direction, which is the cell the previous round
        // pinned as `displacedMisfaced=38614` and the cell that holds the
        // top-down view's right half.
        if (haveFaceOutward && displacement) {
          if (nx * faceOutwardX + ny * faceOutwardY + nz * faceOutwardZ < 0.0f) {
            std::swap(b, c);
            std::swap(bAlpha, cAlpha);
            ++map.windingFlippedTriangles;
            flipped = true;
          }
        }
      }
      // T11 follow-up: the residual, measured. After the reversal above the
      // triangle has a winding normal; this counts the ones that still point
      // away from the face's own side-aware outward direction, split so a
      // displacement-only or brush-only regression cannot hide in the total. A
      // buffer whose winding and plane lump agreed would read 0 here.
      if (haveFaceOutward) {
        const float ux = b.x - a.x, uy = b.y - a.y, uz = b.z - a.z;
        const float vx = c.x - a.x, vy = c.y - a.y, vz = c.z - a.z;
        const float nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
        if (nx * faceOutwardX + ny * faceOutwardY + nz * faceOutwardZ < 0.0f) {
          ++map.windingMisfacedTriangles;
          if (displacement) ++map.windingMisfacedDisplaced;
        }
      }
      const auto ta = texCoord(a), tb = texCoord(b), tc = texCoord(c);
      BspTriangle triangle;
      triangle.a = a; triangle.b = b; triangle.c = c;
      triangle.aAlpha = aAlpha; triangle.bAlpha = bAlpha; triangle.cAlpha = cAlpha;
      triangle.au = ta[0]; triangle.av = ta[1]; triangle.bu = tb[0]; triangle.bv = tb[1]; triangle.cu = tc[0]; triangle.cv = tc[1];
      if (packed.ok) {
        const auto la = lightUv(a), lb = lightUv(b), lc = lightUv(c);
        triangle.alu = la[0]; triangle.alv = la[1]; triangle.blu = lb[0]; triangle.blv = lb[1]; triangle.clu = lc[0]; triangle.clv = lc[1];
        triangle.lightmapped = true;
        ++map.lightmapTriangleCount;
      }
      triangle.material = material;
      triangle.water = water;
      triangle.displacement = displacement;
      triangle.planeSide = facePlaneSide;
      triangle.planeIndex = haveFaceOutward ? static_cast<std::int32_t>(facePlaneNumber) : -1;
      triangle.windingFlipped = flipped ? 1 : 0;
      triangle.cluster = cluster;
      map.triangles.push_back(std::move(triangle));
    };
    bool emittedDisp = false;
    if (dispIndex >= 0 && dispInfoOk && dispVertsOk && polygon.size() == 4
        && static_cast<std::size_t>(dispIndex) < dispInfo.size() / 176) {
      const auto dispBase = static_cast<std::size_t>(dispIndex) * 176;
      const BspVertex start = vertex(dispInfo, dispBase);
      const auto vertStart = read<std::int32_t>(dispInfo, dispBase + 12);
      const auto power = read<std::int32_t>(dispInfo, dispBase + 20);
      const int size = power >= 2 && power <= 4 ? (1 << power) + 1 : 0;
      if (size > 1 && vertStart >= 0
          && static_cast<std::size_t>(vertStart) + static_cast<std::size_t>(size) * static_cast<std::size_t>(size) <= dispVerts.size() / 20) {
        int corner = 0;
        float best = 1.0e30f;
        for (int i = 0; i < 4; ++i) {
          const float dx = polygon[static_cast<std::size_t>(i)].x - start.x;
          const float dy = polygon[static_cast<std::size_t>(i)].y - start.y;
          const float dz = polygon[static_cast<std::size_t>(i)].z - start.z;
          const float distance = dx * dx + dy * dy + dz * dz;
          if (distance < best) { best = distance; corner = i; }
        }
        if (best <= 1.0f) {
          BspVertex quad[4];
          // The displacement vertex rows are stored in the face's canonical
          // corner order. The face-side bit affects the emitted winding below,
          // not the row/column correspondence of the displacement grid.
          constexpr int step = 1;
          for (int i = 0; i < 4; ++i) {
            const int index = (corner + step * i + 8) % 4;
            quad[i] = polygon[static_cast<std::size_t>(index)];
          }
          std::vector<BspVertex> grid(static_cast<std::size_t>(size) * static_cast<std::size_t>(size));
          // The blend weight travels beside the position, on the same grid
          // index, because the two are read from the same 20-byte displacement
          // vertex. A weight that is not a finite number stays at the sentinel
          // instead of failing the whole face: a surface with one unreadable
          // weight is still a surface, and drawing it single-layer is the
          // honest degradation.
          std::vector<float> gridAlpha(static_cast<std::size_t>(size) * static_cast<std::size_t>(size), -1.0f);
          bool finite = true;
          for (int y = 0; y < size && finite; ++y) {
            const float fy = static_cast<float>(y) / static_cast<float>(size - 1);
            const auto left = lerpVertex(quad[0], quad[3], fy);
            const auto right = lerpVertex(quad[1], quad[2], fy);
            for (int x = 0; x < size; ++x) {
              const float fx = static_cast<float>(x) / static_cast<float>(size - 1);
              const auto flat = lerpVertex(left, right, fx);
              const auto vertAt = (static_cast<std::size_t>(vertStart) + static_cast<std::size_t>(y * size + x)) * 20u;
              const float vx = read<float>(dispVerts, vertAt), vy = read<float>(dispVerts, vertAt + 4), vz = read<float>(dispVerts, vertAt + 8), dist = read<float>(dispVerts, vertAt + 12);
              const float alpha = read<float>(dispVerts, vertAt + 16);
              if (!std::isfinite(vx) || !std::isfinite(vy) || !std::isfinite(vz) || !std::isfinite(dist) || std::fabs(dist) > 4096.0f) { finite = false; break; }
              grid[static_cast<std::size_t>(y * size + x)] = {flat.x + vx * dist, flat.y + vy * dist, flat.z + vz * dist};
              if (std::isfinite(alpha)) {
                gridAlpha[static_cast<std::size_t>(y * size + x)] = std::clamp(alpha, 0.0f, 1.0f);
              }
            }
          }
          bool displacementComplete = finite;
          const auto usable = [](const BspVertex& a, const BspVertex& b, const BspVertex& c) {
            const float abx = b.x - a.x, aby = b.y - a.y, abz = b.z - a.z;
            const float acx = c.x - a.x, acy = c.y - a.y, acz = c.z - a.z;
            const float bcx = c.x - b.x, bcy = c.y - b.y, bcz = c.z - b.z;
            const float ab2 = abx * abx + aby * aby + abz * abz;
            const float ac2 = acx * acx + acy * acy + acz * acz;
            const float bc2 = bcx * bcx + bcy * bcy + bcz * bcz;
            const float nx = aby * acz - abz * acy, ny = abz * acx - abx * acz, nz = abx * acy - aby * acx;
            const float area2 = nx * nx + ny * ny + nz * nz;
            return std::isfinite(area2) && area2 > 1.0e-8f
              && ab2 <= 262144.0f && ac2 <= 262144.0f && bc2 <= 262144.0f;
          };
          if (finite && map.triangles.size() + static_cast<std::size_t>(size - 1) * static_cast<std::size_t>(size - 1) * 2u <= triangleLimit) {
            const auto before = map.triangles.size();
            for (int y = 0; y < size - 1; ++y) for (int x = 0; x < size - 1; ++x) {
              const auto at = [&](int gx, int gy) { return grid[static_cast<std::size_t>(gy * size + gx)]; };
              const auto weightAt = [&](int gx, int gy) { return gridAlpha[static_cast<std::size_t>(gy * size + gx)]; };
              const auto p00 = at(x, y), p10 = at(x + 1, y), p11 = at(x + 1, y + 1), p01 = at(x, y + 1);
              if (!usable(p00, p10, p11) || !usable(p00, p11, p01)) { displacementComplete = false; continue; }
              pushTriangle(at(x, y), at(x + 1, y + 1), at(x + 1, y), true,
                weightAt(x, y), weightAt(x + 1, y + 1), weightAt(x + 1, y));
              pushTriangle(at(x, y), at(x, y + 1), at(x + 1, y + 1), true,
                weightAt(x, y), weightAt(x, y + 1), weightAt(x + 1, y + 1));
            }
            emittedDisp = map.triangles.size() > before;
            if (!displacementComplete) {
              map.triangles.resize(before);
              emittedDisp = false;
            }
            if (emittedDisp) {
              ++map.displacementFaces;
              ++map.displacementCornerMatches;
              map.displacementTriangles += map.triangles.size() - before;
            }
          }
        }
      }
    }
    if (!emittedDisp) {
      for (std::size_t i = 1; i + 1 < polygon.size() && map.triangles.size() < triangleLimit; ++i) {
        pushTriangle(polygon[0], polygon[i + 1], polygon[i], false);
      }
    }
    if (water) ++map.waterFaces;
    ++map.renderedFaceCount;
    if (map.triangles.size() >= triangleLimit) break;
  }
  map.triangleCount = map.triangles.size();
  if (lighting.size() % 4 == 0) map.lightmapSampleCount = lighting.size() / 4u;
  else if (lighting.size() % 3 == 0) map.lightmapSampleCount = lighting.size() / 3u;
  if (map.lightmapPackedFaces > 0 && atlasHeight > 0) {
    if (atlasHeight != kAtlas) {
      const float vScale = static_cast<float>(kAtlas) / static_cast<float>(atlasHeight);
      for (auto& triangle : map.triangles) {
        if (!triangle.lightmapped) continue;
        triangle.alv *= vScale;
        triangle.blv *= vScale;
        triangle.clv *= vScale;
      }
    }
    map.lightmapMode = BspLightmapMode::RgbExp32;
    map.lightmapAtlasWidth = kAtlas;
    map.lightmapAtlasHeight = atlasHeight;
    map.lightmapAtlas.assign(atlas.begin(), atlas.begin() + static_cast<std::size_t>(atlasHeight) * kAtlas * 4u);
    if (toneCount > 0) map.lightmapIntensity = static_cast<float>(std::clamp((static_cast<double>(toneSum) / static_cast<double>(toneCount)) / 128.0, 0.15, 2.0));
  } else if (!lighting.empty()) {
    std::uint64_t sum = 0;
    for (const auto value : lighting) sum += value;
    map.lightmapIntensity = static_cast<float>(std::clamp((static_cast<double>(sum) / static_cast<double>(lighting.size())) / 128.0, 0.15, 2.0));
    map.lightmapMode = BspLightmapMode::AverageIntensity;
  }
  // What the packer left behind, named. Before this the only trace of a face
  // that missed the atlas was the gap between `lightmapFaceCount` and
  // `lightmapPackedFaces`, and a gap nobody prints is a gap nobody fixes: the
  // drop is now a number with a sentence, and the sentence says how wide the
  // shelf was, so "abbey loses faces" and "the atlas is too narrow for abbey"
  // are the same reading instead of two.
  map.lightmapAtlasDroppedFaces = map.lightmapFaceCount >= map.lightmapPackedFaces
    ? map.lightmapFaceCount - map.lightmapPackedFaces : 0;
  if (map.lightmapFaceCount == 0) {
    map.lightmapAtlasReason = "the bsp declares no lightmap faces";
  } else if (map.lightmapAtlasDroppedFaces == 0) {
    map.lightmapAtlasReason = "all " + std::to_string(map.lightmapFaceCount) + " faces fit the "
      + std::to_string(kAtlas) + "-wide atlas";
  } else {
    map.lightmapAtlasReason = std::to_string(map.lightmapAtlasDroppedFaces) + " of "
      + std::to_string(map.lightmapFaceCount) + " faces did not fit the "
      + std::to_string(kAtlas) + "-wide atlas";
  }
  map.waterReflection = false;
  map.waterRefraction = false;
  map.valid = true; return true;
}

const char* BspParser::lightmapModeName(BspLightmapMode mode) {
  if (mode == BspLightmapMode::RgbExp32) return "source-equivalent";
  if (mode == BspLightmapMode::AverageIntensity) return "average-intensity";
  return "unavailable";
}

bool BspParser::skyMaterialPath(const std::string& skyName, int face, std::string& out) {
  static const char* suffixes[] = {"rt", "lf", "bk", "ft", "up", "dn"};
  if (skyName.empty() || face < 0 || face > 5) return false;
  out = "materials/skybox/" + skyName + suffixes[face] + ".vmt";
  return true;
}

bool BspParser::clusterVisible(const BspMap& map, int fromCluster, int toCluster) {
  if (map.visMode != BspVisMode::ClusterPvs) return true;
  if (fromCluster < 0 || toCluster < 0 || fromCluster >= map.visClusterCount || toCluster >= map.visClusterCount) return false;
  // T11: prefer the uncompressed row whenever the map carries one. The row is
  // (numclusters + 7) / 8 plain bytes, bit i of byte k = cluster k*8 + i. The
  // previous code always ran a run-length walk over the COMPRESSED offset
  // table and answered with whatever the walk produced -- for pl_odyssey that
  // was another cluster's row read as runs, which is why the clip hid faces
  // that were plainly in front of the camera.
  const auto rawOffset = static_cast<std::size_t>(fromCluster) < map.visRawOffsets.size()
    ? map.visRawOffsets[static_cast<std::size_t>(fromCluster)] : -1;
  if (rawOffset >= 0) {
    const std::size_t byteIndex = static_cast<std::size_t>(rawOffset)
      + static_cast<std::size_t>(toCluster) / 8u;
    if (byteIndex >= map.visBits.size()) return false;
    return (map.visBits[byteIndex] & (1u << (toCluster & 7))) != 0;
  }
  const auto offset = map.visOffsets[static_cast<std::size_t>(fromCluster)];
  if (offset < 0 || static_cast<std::size_t>(offset) >= map.visBits.size()) return false;
  // Compressed rows: a byte with the high bit set repeats the next byte
  // (count = low 7 bits + 1); otherwise the byte copies that many + 1 bytes
  // verbatim. Walked only as far as the queried cluster.
  std::size_t cursor = static_cast<std::size_t>(offset);
  int cluster = 0;
  while (cluster <= toCluster && cursor < map.visBits.size()) {
    const auto run = map.visBits[cursor++];
    if ((run & 0x80u) != 0u) {
      const int count = static_cast<int>(run & 0x7fu) + 1;
      if (cursor >= map.visBits.size()) return false;
      const auto value = map.visBits[cursor++];
      if (toCluster < cluster + count * 8) return (value & (1u << (toCluster - cluster))) != 0;
      cluster += count * 8;
    } else {
      const int count = static_cast<int>(run) + 1;
      for (int i = 0; i < count; ++i) {
        if (cursor >= map.visBits.size()) return false;
        const auto value = map.visBits[cursor++];
        if (toCluster < cluster + 8) return (value & (1u << (toCluster - cluster))) != 0;
        cluster += 8;
      }
    }
  }
  return false;
}

int BspParser::clusterAt(const BspMap& map, float x, float y, float z) {
  if (map.nodes.empty() || map.planes.empty() || map.leaves.empty()) return -1;
  if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) return -1;
  int node = 0;
  for (int guard = 0; node >= 0 && guard < 4096; ++guard) {
    if (static_cast<std::size_t>(node) >= map.nodes.size()) return -1;
    const auto& current = map.nodes[static_cast<std::size_t>(node)];
    if (current.plane < 0 || static_cast<std::size_t>(current.plane) >= map.planes.size()) return -1;
    const auto& plane = map.planes[static_cast<std::size_t>(current.plane)];
    const float side = x * plane.normal[0] + y * plane.normal[1] + z * plane.normal[2] - plane.dist;
    node = side >= 0.0f ? current.child[0] : current.child[1];
  }
  const int leaf = -node - 1;
  if (leaf < 0 || static_cast<std::size_t>(leaf) >= map.leaves.size()) return -1;
  return map.leaves[static_cast<std::size_t>(leaf)].cluster;
}

int BspParser::countVisibleClusters(const BspMap& map, int fromCluster) {
  if (map.visMode != BspVisMode::ClusterPvs || fromCluster < 0) return 0;
  int visible = 0;
  for (int cluster = 0; cluster < map.visClusterCount; ++cluster) {
    if (clusterVisible(map, fromCluster, cluster)) ++visible;
  }
  return visible;
}
}
