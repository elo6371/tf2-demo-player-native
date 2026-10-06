#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace tf2::native {

struct BspVertex { float x = 0; float y = 0; float z = 0; };
struct BspTriangle { BspVertex a; BspVertex b; BspVertex c; float au = 0; float av = 0; float bu = 0; float bv = 0; float cu = 0; float cv = 0; float alu = 0; float alv = 0; float blu = 0; float blv = 0; float clu = 0; float clv = 0; float lightA = 1.0f; float lightB = 1.0f; float lightC = 1.0f; std::string material; };
struct BspMap {
  int version = 0;
  int revision = 0;
  std::size_t faceCount = 0;
  std::size_t renderedFaceCount = 0;
  std::size_t triangleCount = 0;
  std::size_t lightmapSampleCount = 0;
  std::size_t lightmapFaceCount = 0;
  std::size_t lightmapTriangleCount = 0;
  std::size_t lightmapBytes = 0;
  float lightmapIntensity = 1.0f;
  // HDR BSP lighting uses RGBExp32 (RGB bytes plus a shared exponent).
  std::size_t hdrLightmapSampleCount = 0;
  std::size_t hdrLightmapFaceCount = 0;
  std::size_t hdrLightmapBytes = 0;
  float hdrLightmapIntensity = 1.0f;
  bool hasHdrLightmap = false;
  std::vector<BspTriangle> triangles;
  std::string error;
  bool valid = false;
};

class BspParser {
public:
  static bool parse(const std::vector<std::uint8_t>& bytes, BspMap& map, std::size_t triangleLimit = 200000);
};

} // namespace tf2::native
