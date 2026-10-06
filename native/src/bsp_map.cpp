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

bool BspParser::parse(const std::vector<std::uint8_t>& bytes, BspMap& map, std::size_t triangleLimit) {
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
      bool offsetsOk = true;
      for (int cluster = 0; cluster < clusters; ++cluster) {
        const auto offset = read<std::int32_t>(vis, 4 + static_cast<std::size_t>(cluster) * 8u);
        if (offset < 0 || static_cast<std::size_t>(offset) >= vis.size()) offsetsOk = false;
        map.visOffsets[static_cast<std::size_t>(cluster)] = offset;
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
  constexpr int kAtlas = 2048;
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
    std::vector<BspVertex> polygon; polygon.reserve(static_cast<std::size_t>(count)); bool validFace = true;
    for (std::int16_t i = 0; i < count; ++i) { const auto surf = read<std::int32_t>(surfEdges, (static_cast<std::size_t>(firstEdge) + i) * 4); const auto edge = surf < 0 ? -surf : surf; if (edge < 0 || static_cast<std::size_t>(edge) >= edgeCount) { validFace = false; break; } const auto vertexIndex = read<std::uint16_t>(edges, static_cast<std::size_t>(edge) * 4 + (surf < 0 ? 2 : 0)); if (vertexIndex >= vertexCount) { validFace = false; break; } polygon.push_back(vertex(vertices, static_cast<std::size_t>(vertexIndex) * 12)); }
    if (!validFace || polygon.size() < 3) continue;
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
    const auto pushTriangle = [&](const BspVertex& a, const BspVertex& b, const BspVertex& c, bool displacement) {
      if (map.triangles.size() >= triangleLimit) return;
      const auto ta = texCoord(a), tb = texCoord(b), tc = texCoord(c);
      BspTriangle triangle;
      triangle.a = a; triangle.b = b; triangle.c = c;
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
      triangle.cluster = cluster;
      map.triangles.push_back(std::move(triangle));
    };
    const auto dispIndex = read<std::int16_t>(faces, base + 12);
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
          for (int i = 0; i < 4; ++i) quad[i] = polygon[static_cast<std::size_t>((corner + i) % 4)];
          std::vector<BspVertex> grid(static_cast<std::size_t>(size) * static_cast<std::size_t>(size));
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
              if (!std::isfinite(vx) || !std::isfinite(vy) || !std::isfinite(vz) || !std::isfinite(dist) || std::fabs(dist) > 4096.0f) { finite = false; break; }
              grid[static_cast<std::size_t>(y * size + x)] = {flat.x + vx * dist, flat.y + vy * dist, flat.z + vz * dist};
            }
          }
          if (finite && map.triangles.size() + static_cast<std::size_t>(size - 1) * static_cast<std::size_t>(size - 1) * 2u <= triangleLimit) {
            const auto before = map.triangles.size();
            for (int y = 0; y < size - 1; ++y) for (int x = 0; x < size - 1; ++x) {
              const auto at = [&](int gx, int gy) { return grid[static_cast<std::size_t>(gy * size + gx)]; };
              pushTriangle(at(x, y), at(x + 1, y), at(x + 1, y + 1), true);
              pushTriangle(at(x, y), at(x + 1, y + 1), at(x, y + 1), true);
            }
            emittedDisp = map.triangles.size() > before;
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
      for (std::size_t i = 1; i + 1 < polygon.size() && map.triangles.size() < triangleLimit; ++i) pushTriangle(polygon[0], polygon[i], polygon[i + 1], false);
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
  const auto offset = map.visOffsets[static_cast<std::size_t>(fromCluster)];
  if (offset < 0 || static_cast<std::size_t>(offset) >= map.visBits.size()) return false;
  std::size_t cursor = static_cast<std::size_t>(offset);
  int cluster = 0;
  while (cluster <= toCluster && cursor < map.visBits.size()) {
    const auto bits = map.visBits[cursor++];
    if (bits == 0) {
      if (cursor >= map.visBits.size()) return false;
      cluster += static_cast<int>(map.visBits[cursor++]) * 8;
      continue;
    }
    if (toCluster >= cluster && toCluster < cluster + 8) return (bits & (1u << (toCluster - cluster))) != 0;
    cluster += 8;
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
