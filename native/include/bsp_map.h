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
  // T12 (2026-10-18): the per-vertex blend weight, straight from
  // `CDispVert::m_flAlpha` (bspfile.h, 20 bytes per vertex: vector, distance,
  // alpha). WorldVertexTransition -- the shader every terrain material on the
  // reference map uses -- picks between `$basetexture` and `$basetexture2` with
  // exactly this number (`lightmappedgeneric_ps2_3_x.h`: `blendfactor =
  // i.vertexBlendX_fogFactorW.r`), so without it the second layer has nothing
  // to be weighted by and the surface can only be drawn as one of the two.
  //
  // -1 is "no weight", which is what a non-displacement triangle gets and what
  // a displacement vertex gets when its alpha is unreadable. The renderer reads
  // the sentinel as "this vertex has one layer", so a missing weight degrades
  // to the single-layer picture instead of to a fabricated blend.
  float aAlpha = -1.0f;
  float bAlpha = -1.0f;
  float cAlpha = -1.0f;
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
  // T1 interface (2026-10-13): what the atlas packer ended up doing. The three
  // fields are read by world_material's lightmap reading, so a map that loses
  // faces to the packer says so with a number and a reason instead of silently
  // drawing unlit. `grown` is false for every map that fits the first shelf, so
  // a map whose readings are already pinned keeps them.
  int lightmapAtlasMaxWidth = 0;
  std::size_t lightmapAtlasDroppedFaces = 0;
  bool lightmapAtlasGrown = false;
  std::string lightmapAtlasReason;
  std::vector<std::uint8_t> lightmapAtlas;
  std::vector<BspTriangle> triangles;
  std::string skyName;
  std::size_t skyFaces = 0;
  std::size_t displacementFaces = 0;
  std::size_t displacementTriangles = 0;
  std::size_t displacementCornerMatches = 0;
  // T10 winding census, read-only: how many face polygons wind along the side
  // of their own BSP plane the face is meant to be seen from (planeSide 1
  // faces count as agreeing when the winding normal points AGAINST the stored
  // plane normal). The plane lump is ground truth, so agree/total measures the
  // winding the renderer will cull against without depending on how the
  // triangle soup was emitted. No polygon is reordered here: the census only
  // counts.
  std::size_t windingFaceCount = 0;
  std::size_t windingAgreeCount = 0;
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
  static bool parse(const std::vector<std::uint8_t>& bytes, BspMap& map,
    std::size_t triangleLimit = 200000, int atlasMaxWidth = 4096);
  static const char* lightmapModeName(BspLightmapMode mode);
  static bool skyMaterialPath(const std::string& skyName, int face, std::string& out);
  static bool clusterVisible(const BspMap& map, int fromCluster, int toCluster);
  static int clusterAt(const BspMap& map, float x, float y, float z);
  static int countVisibleClusters(const BspMap& map, int fromCluster);
};

} // namespace tf2::native
