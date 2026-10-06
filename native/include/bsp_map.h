#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace tf2::native {

struct BspVertex { float x = 0; float y = 0; float z = 0; };
struct BspTriangle {
  BspVertex a; BspVertex b; BspVertex c;
  float au = 0; float av = 0; float bu = 0; float bv = 0; float cu = 0; float cv = 0;
  float alu = -1; float alv = -1; float blu = -1; float blv = -1; float clu = -1; float clv = -1;
  std::string material;
  bool lightmapped = false;
  bool water = false;
  bool displacement = false;
  int cluster = -1;
};

enum class BspLightmapMode { Unavailable, AverageIntensity, RgbExp32 };
enum class BspVisMode { Unavailable, ClusterPvs };

struct BspCubemapSample { int origin[3] = {}; int size = 0; };
struct BspPlane { float normal[3] = {}; float dist = 0; };
struct BspNode { int plane = 0; int child[2] = {}; };
struct BspLeaf { int cluster = -1; int firstFace = 0; int faceCount = 0; };

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
  std::size_t lightmapPackedFaces = 0;
  std::size_t lightmapLuxelFits = 0;
  float lightmapIntensity = 1.0f;
  std::size_t hdrLightmapSampleCount = 0;
  std::size_t hdrLightmapFaceCount = 0;
  std::size_t hdrLightmapBytes = 0;
  float hdrLightmapIntensity = 1.0f;
  bool hasHdrLightmap = false;
  BspLightmapMode lightmapMode = BspLightmapMode::Unavailable;
  int lightmapAtlasWidth = 0;
  int lightmapAtlasHeight = 0;
  std::vector<std::uint8_t> lightmapAtlas;
  std::vector<BspTriangle> triangles;
  std::string skyName;
  std::size_t skyFaces = 0;
  std::size_t displacementFaces = 0;
  std::size_t displacementTriangles = 0;
  std::size_t displacementCornerMatches = 0;
  std::size_t waterFaces = 0;
  bool waterReflection = false;
  bool waterRefraction = false;
  std::size_t cubemapCount = 0;
  std::vector<BspCubemapSample> cubemaps;
  BspVisMode visMode = BspVisMode::Unavailable;
  int visClusterCount = 0;
  std::vector<int> visOffsets;
  std::vector<std::uint8_t> visBits;
  std::vector<BspPlane> planes;
  std::vector<BspNode> nodes;
  std::vector<BspLeaf> leaves;
  std::vector<std::uint16_t> leafFaces;
  std::string error;
  bool valid = false;
};

class BspParser {
public:
  static bool parse(const std::vector<std::uint8_t>& bytes, BspMap& map, std::size_t triangleLimit = 200000);
  static const char* lightmapModeName(BspLightmapMode mode);
  static bool skyMaterialPath(const std::string& skyName, int face, std::string& out);
  static bool clusterVisible(const BspMap& map, int fromCluster, int toCluster);
  static int clusterAt(const BspMap& map, float x, float y, float z);
  static int countVisibleClusters(const BspMap& map, int fromCluster);
};

} // namespace tf2::native
