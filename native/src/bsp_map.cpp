#include "bsp_map.h"
#include "LzmaDec.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
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
  const auto firstFace = read<std::int32_t>(models, 40), faceCount = read<std::int32_t>(models, 44); if (firstFace < 0 || faceCount < 0 || static_cast<std::size_t>(firstFace) + static_cast<std::size_t>(faceCount) > faces.size() / 56) return fail("BSP world face range is invalid"); map.faceCount = static_cast<std::size_t>(faceCount);
  std::unordered_map<std::int32_t, std::string> names; for (std::size_t i = 0; i < texData.size() / 32; ++i) { const auto id = read<std::int32_t>(texData, i * 32 + 12); if (id >= 0 && static_cast<std::size_t>(id) < stringTable.size() / 4) names[static_cast<std::int32_t>(i)] = readString(stringData, read<std::int32_t>(stringTable, static_cast<std::size_t>(id) * 4)); }
  const auto vertexCount = vertices.size() / 12, edgeCount = edges.size() / 4, surfEdgeCount = surfEdges.size() / 4;
  for (std::int32_t faceIndex = firstFace; faceIndex < firstFace + faceCount; ++faceIndex) {
    const auto base = static_cast<std::size_t>(faceIndex) * 56; const auto firstEdge = read<std::int32_t>(faces, base + 4); const auto count = read<std::int16_t>(faces, base + 8); const auto infoIndex = read<std::int16_t>(faces, base + 10);
    if (count < 3 || firstEdge < 0 || static_cast<std::size_t>(firstEdge) + static_cast<std::size_t>(count) > surfEdgeCount || infoIndex < 0 || static_cast<std::size_t>(infoIndex) >= texInfo.size() / 72) continue;
    const auto flags = read<std::int32_t>(texInfo, static_cast<std::size_t>(infoIndex) * 72 + 64); if ((flags & (2 | 4 | 64 | 128)) != 0) continue;
    const auto texDataIndex = read<std::int32_t>(texInfo, static_cast<std::size_t>(infoIndex) * 72 + 68); const auto material = names.count(texDataIndex) ? names[texDataIndex] : std::string{};
    const float u0 = read<float>(texInfo, static_cast<std::size_t>(infoIndex) * 72 + 0), u1 = read<float>(texInfo, static_cast<std::size_t>(infoIndex) * 72 + 4), u2 = read<float>(texInfo, static_cast<std::size_t>(infoIndex) * 72 + 8), u3 = read<float>(texInfo, static_cast<std::size_t>(infoIndex) * 72 + 12);
    const float v0 = read<float>(texInfo, static_cast<std::size_t>(infoIndex) * 72 + 16), v1 = read<float>(texInfo, static_cast<std::size_t>(infoIndex) * 72 + 20), v2 = read<float>(texInfo, static_cast<std::size_t>(infoIndex) * 72 + 24), v3 = read<float>(texInfo, static_cast<std::size_t>(infoIndex) * 72 + 28);
    const float lu0 = read<float>(texInfo, static_cast<std::size_t>(infoIndex) * 72 + 32), lu1 = read<float>(texInfo, static_cast<std::size_t>(infoIndex) * 72 + 36), lu2 = read<float>(texInfo, static_cast<std::size_t>(infoIndex) * 72 + 40), lu3 = read<float>(texInfo, static_cast<std::size_t>(infoIndex) * 72 + 44);
    const float lv0 = read<float>(texInfo, static_cast<std::size_t>(infoIndex) * 72 + 48), lv1 = read<float>(texInfo, static_cast<std::size_t>(infoIndex) * 72 + 52), lv2 = read<float>(texInfo, static_cast<std::size_t>(infoIndex) * 72 + 56), lv3 = read<float>(texInfo, static_cast<std::size_t>(infoIndex) * 72 + 60);
    const auto lightCoord = [&](const BspVertex& point) { return std::array<float, 2>{(point.x * lu0 + point.y * lu1 + point.z * lu2 + lu3) * 0.125f, (point.x * lv0 + point.y * lv1 + point.z * lv2 + lv3) * 0.125f}; };
    const auto lightOffset = read<std::int32_t>(faces, base + 20);
    const auto lightWidth = read<std::int32_t>(faces, base + 36) + 1;
    const auto lightHeight = read<std::int32_t>(faces, base + 40) + 1;
    const auto lightBytes = lightWidth > 0 && lightHeight > 0
      ? static_cast<std::uint64_t>(lightWidth) * static_cast<std::uint64_t>(lightHeight) * 3u : 0u;
    const bool hasLightmap = lightOffset >= 0 && lightBytes > 0
      && static_cast<std::uint64_t>(lightOffset) <= lighting.size()
      && lightBytes <= static_cast<std::uint64_t>(lighting.size()) - static_cast<std::uint64_t>(lightOffset);
    if (hasLightmap) { ++map.lightmapFaceCount; map.lightmapBytes += static_cast<std::size_t>(lightWidth) * static_cast<std::size_t>(lightHeight) * 3u; }
    std::vector<BspVertex> polygon; polygon.reserve(static_cast<std::size_t>(count)); bool validFace = true;
    for (std::int16_t i = 0; i < count; ++i) { const auto surf = read<std::int32_t>(surfEdges, (static_cast<std::size_t>(firstEdge) + i) * 4); const auto edge = surf < 0 ? -surf : surf; if (edge < 0 || static_cast<std::size_t>(edge) >= edgeCount) { validFace = false; break; } const auto vertexIndex = read<std::uint16_t>(edges, static_cast<std::size_t>(edge) * 4 + (surf < 0 ? 2 : 0)); if (vertexIndex >= vertexCount) { validFace = false; break; } polygon.push_back(vertex(vertices, static_cast<std::size_t>(vertexIndex) * 12)); }
    if (!validFace) continue; ++map.renderedFaceCount;
    const auto texCoord = [&](const BspVertex& point) { return std::array<float, 2>{point.x * u0 + point.y * u1 + point.z * u2 + u3, point.x * v0 + point.y * v1 + point.z * v2 + v3}; };
    for (std::size_t i = 1; i + 1 < polygon.size() && map.triangles.size() < triangleLimit; ++i) {
      const auto ta = texCoord(polygon[0]), tb = texCoord(polygon[i]), tc = texCoord(polygon[i + 1]);
      const auto la = lightCoord(polygon[0]), lb = lightCoord(polygon[i]), lc = lightCoord(polygon[i + 1]);
      if (hasLightmap) ++map.lightmapTriangleCount;
      const auto lightValue = [&](const std::array<float, 2>& coord) {
        if (!hasLightmap) return map.lightmapIntensity;
        const auto x = std::clamp(static_cast<int>(std::floor(coord[0])), 0, lightWidth - 1);
        const auto y = std::clamp(static_cast<int>(std::floor(coord[1])), 0, lightHeight - 1);
        const auto at = static_cast<std::size_t>(lightOffset) +
          (static_cast<std::size_t>(y) * static_cast<std::size_t>(lightWidth) + static_cast<std::size_t>(x)) * 3u;
        const float value = (static_cast<float>(lighting[at]) + static_cast<float>(lighting[at + 1]) + static_cast<float>(lighting[at + 2])) / (3.0f * 128.0f);
        return std::clamp(value, 0.15f, 2.0f);
      };
      map.triangles.push_back({polygon[0], polygon[i], polygon[i + 1], ta[0], ta[1], tb[0], tb[1], tc[0], tc[1], la[0], la[1], lb[0], lb[1], lc[0], lc[1], lightValue(la), lightValue(lb), lightValue(lc), material});
    }
    if (map.triangles.size() >= triangleLimit) break;
  }
  map.triangleCount = map.triangles.size(); map.lightmapSampleCount = lighting.size() / 3u;
  if (!lighting.empty()) {
    std::uint64_t sum = 0;
    for (const auto value : lighting) sum += value;
    const double average = static_cast<double>(sum) / static_cast<double>(lighting.size());
    map.lightmapIntensity = static_cast<float>(std::clamp(average / 128.0, 0.15, 2.0));
  }
  map.valid = true; return true;
}
}
