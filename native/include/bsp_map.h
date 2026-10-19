#pragma once

#include <array>
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
  // T11: which side of its own BSP plane this face was authored on (dface_t
  // `side`), so a diagnostic can ask a triangle "was the side you were seen from
  // the side your own data says you are seen from" without re-parsing. It is
  // also the bit the emission's orientation rule uses.
  std::uint8_t planeSide = 0;
  // The plane the face was authored on, so a diagnostic can rebuild the face's
  // outward normal (plane normal, flipped by planeSide) from the map itself.
  std::int32_t planeIndex = -1;
  // T11 third round: whether emission reversed this triangle. With it a
  // diagnostic can recover the STORED winding (`a, b, c` swapped back) and
  // therefore measure the shipped data instead of measuring the rule that
  // overwrote it -- which is the difference between "the map is inconsistent"
  // and "the rule is".
  std::uint8_t windingFlipped = 0;
  // T23: this triangle is the flat quad of a displacement face whose grid was
  // refused. It is emitted through the brush path (no grid, no blend weights),
  // so it carries the brush orientation rule -- which is exactly why it can be
  // back-face culled where the grid it replaces would have been oriented. The
  // flag lets a diagnostic count those triangles without re-parsing the face.
  bool fallbackQuad = false;
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
  // T23: faces the parser could not turn into a polygon at all (a surfedge or
  // vertex index outside its lump, or fewer than three vertices). A non-zero
  // count here is the other way geometry can be missing before any cull runs;
  // it is counted rather than inferred from faceCount - renderedFaceCount,
  // because a face can also be skipped by the triangle budget.
  std::size_t facesSkippedInvalid = 0;
  // Faces the map itself marks not-drawn (`SURF_NODRAW`/`SURF_HINT` bits
  // 64/128). They are geometry the renderer is supposed to omit, so they must
  // not be counted as missing surfaces.
  std::size_t facesSkippedNoDraw = 0;
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
  // T17: the tone of what the packer WROTE, per face, before any uv is
  // involved. The per-triangle tone is what the shader samples through the
  // uv; this is the face's own luxels. If the two histograms agree, the uvs
  // address their own rectangle and a dark frame is dark data; if they
  // disagree, the uv mapping is reading somebody else's rectangle. Buckets are
  // 32 wide, the same as the triangle histogram in world_geometry_probe.
  std::array<std::uint32_t, 8> lightmapFaceToneHistogram{};
  std::size_t lightmapFaceToneDark = 0;
  std::size_t lightmapFaceToneZero = 0;
  // The same two histograms taken on lump 53, the HDR page. A map compiled with
  // HDR keeps both and the engine chooses; if the tonemapped page is black
  // where the wide-range page is lit, the frame is dark because of the page we
  // chose, not because of the uvs or the map.
  std::array<std::uint32_t, 8> lightmapHdrFaceToneHistogram{};
  std::size_t lightmapHdrFaceToneDark = 0;
  std::size_t lightmapHdrFaceToneZero = 0;
  std::uint64_t hdrLightmapToneSum = 0;
  std::uint64_t hdrLightmapToneCount = 0;
  std::string lightmapAtlasReason;
  std::vector<std::uint8_t> lightmapAtlas;
  std::vector<BspTriangle> triangles;
  std::string skyName;
  std::size_t skyFaces = 0;
  std::size_t displacementFaces = 0;
  std::size_t displacementTriangles = 0;
  std::size_t displacementCornerMatches = 0;
  // T23: not every face that carries a dispinfo becomes a displacement grid.
  // A face whose grid cannot be built is emitted as its flat quad instead, so
  // "the terrain is missing" and "the terrain is flat here" are different
  // readings. `candidates` counts the faces that asked for a grid; `fallback`
  // counts the ones that did not get one, split by the branch that refused it.
  // A map whose candidate count equals `displacementFaces` has no fallback
  // faces at all, and a hole cannot be blamed on this path.
  std::size_t displacementFaceCandidates = 0;
  std::size_t displacementFallbackFaces = 0;
  std::size_t displacementFallbackLump = 0;       // no usable dispinfo/dispvert lump
  std::size_t displacementFallbackPolygon = 0;    // face is not a quad
  std::size_t displacementFallbackIndex = 0;      // dispinfo index out of range
  std::size_t displacementFallbackPower = 0;      // power outside 2..4
  std::size_t displacementFallbackRange = 0;      // vertex range outside the lump
  std::size_t displacementFallbackCorner = 0;     // no polygon corner matches the start
  std::size_t displacementFallbackNonFinite = 0;  // a grid vertex is not finite
  std::size_t displacementFallbackCell = 0;       // a grid cell is degenerate/oversized
  std::size_t displacementFallbackLimit = 0;      // the triangle budget truncated it
  std::size_t displacementFallbackOther = 0;      // refused with no more specific reason
  // The faces behind those counters, so a fallback is a place in the map and
  // not only a number. Bounded by the number of candidates (1392 on the
  // reference map); the probe prints the first few.
  std::vector<std::int32_t> displacementFallbackFaceIndex;
  std::vector<std::string> displacementFallbackMaterial;
  // T10 winding census, read-only: how many face polygons wind along the side
  // of their own BSP plane the face is meant to be seen from (planeSide 1
  // faces count as agreeing when the winding normal points AGAINST the stored
  // plane normal). The plane lump is ground truth, so agree/total measures the
  // winding the renderer will cull against without depending on how the
  // triangle soup was emitted. No polygon is reordered here: the census only
  // counts.
  std::size_t windingFaceCount = 0;
  std::size_t windingAgreeCount = 0;
  // T11: how many emitted triangles were reversed at emission so their winding
  // normal points along their face's own front side. A triangle is either
  // stored the way the cull mode wants or it is reversed; this count is the
  // number that had to move, and the gate pins it.
  std::size_t windingFlippedTriangles = 0;
  // T11 follow-up: the side bit is part of the ground truth. `windingAgreeCount`
  // above already honours it, but the emission did not: a face whose plane side
  // bit is 1 is seen from the plane's BACK, so the outward direction is -normal,
  // and a rule that orients every face to +normal leaves those faces wound
  // backwards. These two counters are the measurement that says how many faces
  // are on that side and how many emitted triangles still point away from their
  // own outward direction after emission. `misfaced` must read 0 on a coherent
  // buffer; it is the number the cull mode would otherwise remove.
  std::size_t windingSideOneFaces = 0;
  std::size_t windingMisfacedTriangles = 0;
  std::size_t windingMisfacedDisplaced = 0;
  // T12 post-merge: T11 reverses a displacement triangle's b and c to orient
  // it, and the blend weight rides on the vertex. `std::swap(b, c)` alone would
  // leave the alphas where they were and attach the author's weight to the
  // wrong corner -- a picture that still blends, just with the mask mirrored
  // inside every quad. These two counters compare the emitted triangle's
  // (position, weight) pairs against the grid cells it was built from: a swap
  // that moves the positions and not the weights reads as `detached`.
  std::size_t alphaPairingChecked = 0;
  std::size_t alphaPairingDetached = 0;
  // T11 third round: is the SOURCE winding uniform? The rule matrix left one
  // question open -- whether the stored polygon order is a per-face coin toss,
  // which no plane-derived rule could ever reproduce, or a fixed convention
  // relative to the plane, which makes the emission rule a constant rather than
  // a choice. These count, per face class, the face polygons whose own winding
  // normal points ALONG (+) or AGAINST (-) the raw plane normal, and -- for
  // displacement faces -- whether the emitted grid's first triangle keeps or
  // reverses the polygon's own order. A class that reads one column at zero is
  // a uniform convention; a class that splits is the class no single sign can
  // orient. Face counts, not triangle counts, so a 32x32 grid cannot outvote a
  // quad.
  std::size_t windingStoredBrushAlong = 0;
  std::size_t windingStoredBrushAgainst = 0;
  std::size_t windingStoredDispAlong = 0;
  std::size_t windingStoredDispAgainst = 0;
  std::size_t windingGridKeepsPolygon = 0;
  std::size_t windingGridReversesPolygon = 0;
  // The same question asked of the EMITTED triangle before any reversal, which
  // is the only place the fan's own orientation shows: the brush fan emits
  // (0, i+1, i), the reverse of the polygon's order, so the polygon census
  // above and the buffer's order are not the same statement. [0] counts along
  // the raw plane normal, [1] counts along the side-aware outward direction.
  std::size_t windingPreBrushAlongPlane = 0;
  std::size_t windingPreBrushAgainstPlane = 0;
  std::size_t windingPreDispAlongPlane = 0;
  std::size_t windingPreDispAgainstPlane = 0;
  std::size_t windingPreBrushAlongOutward = 0;
  std::size_t windingPreBrushAgainstOutward = 0;
  std::size_t windingPreDispAlongOutward = 0;
  std::size_t windingPreDispAgainstOutward = 0;
  std::size_t waterFaces = 0;
  bool waterReflection = false;
  bool waterRefraction = false;
  std::size_t cubemapCount = 0;
  std::vector<BspCubemapSample> cubemaps;
  BspVisMode visMode = BspVisMode::Unavailable;
  int visClusterCount = 0;
  std::vector<int> visOffsets;
  // T11: dvis_t carries TWO offset tables -- [0] for the 0x80-run compressed
  // rows, [1] for the plain-bit rows, with -1 in whichever table the map did
  // not use. Reading only [0] with a stride of 8 asked for bitofs[0][2c]: a
  // different cluster's row. This vector is [1] and takes precedence.
  std::vector<int> visRawOffsets;
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
