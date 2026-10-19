// world_geometry_probe -- T10: the triangle census of a real BSP.
//
// WHAT IT ANSWERS
//   The reference frame for the pinned POV recording shows world geometry as
//   shattered slabs. "Shards" are triangles with one vertex far from the face
//   they belong to, so the question the frame raises is which triangles are
//   outliers and which face/displacement path emitted them. This probe parses
//   the same BSP through the same parser the renderer uses and reports:
//
//     - the triangle total, split by displacement / water / lightmapped,
//     - an edge-length histogram, and the count of triangles whose longest edge
//       exceeds 256 / 1024 / 4096 units (a face edge above 1024 would be a map
//       authoring mistake; a triangle whose LONGEST edge is far above its face's
//       own extent is a decoding defect),
//     - the top offenders with their material, displacement flag, cluster and
//       three vertices, so each one can be attributed,
//     - degenerate (zero-area) triangles,
//     - the displacement corner-match census the parser already keeps.
//
// Read-only: it never writes, never renders, and knows nothing about the GPU.
#include "asset_root.h"
#include "bsp_map.h"
#include "vpk_archive.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

using tf2::native::AssetRoot;
using tf2::native::BspMap;
using tf2::native::BspTriangle;
using tf2::native::BspVertex;

std::vector<std::uint8_t> readFile(const std::filesystem::path& path) {
  std::vector<std::uint8_t> bytes;
  FILE* file = nullptr;
  if (_wfopen_s(&file, path.wstring().c_str(), L"rb") != 0 || file == nullptr) return bytes;
  std::fseek(file, 0, SEEK_END);
  const long size = std::ftell(file);
  std::fseek(file, 0, SEEK_SET);
  if (size > 0) {
    bytes.resize(static_cast<std::size_t>(size));
    if (std::fread(bytes.data(), 1, bytes.size(), file) != bytes.size()) bytes.clear();
  }
  std::fclose(file);
  return bytes;
}

std::vector<std::uint8_t> readAsset(const AssetRoot& root, const std::string& path) {
  const auto& archives = root.archives();
  std::vector<std::filesystem::path> holders;
  archives.collectContaining(path, holders);
  for (const auto& archivePath : holders) {
    const auto* archive = archives.find(archivePath);
    if (archive == nullptr) continue;
    std::string error;
    const auto bytes = archive->read(path, &error);
    if (!bytes.empty()) return bytes;
  }
  return readFile(root.resolve(path));
}

float edge(const BspVertex& a, const BspVertex& b) {
  const float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
  return std::sqrt(dx * dx + dy * dy + dz * dz);
}

float triangleArea(const BspTriangle& t) {
  const float ux = t.b.x - t.a.x, uy = t.b.y - t.a.y, uz = t.b.z - t.a.z;
  const float vx = t.c.x - t.a.x, vy = t.c.y - t.a.y, vz = t.c.z - t.a.z;
  const float cx = uy * vz - uz * vy, cy = uz * vx - ux * vz, cz = ux * vy - uy * vx;
  return 0.5f * std::sqrt(cx * cx + cy * cy + cz * cz);
}

// A triangle's centroid, summed in a canonical vertex order so the reading does
// not move when the emission rule swaps two vertices: the same three points must
// produce the same sample point, or a census of boundary points would drift by a
// triangle every time the winding changed.
BspVertex canonicalCentroid(const BspTriangle& t) {
  BspVertex v[3] = {t.a, t.b, t.c};
  for (int i = 0; i < 2; ++i) for (int j = i + 1; j < 3; ++j) {
    const bool swap = v[j].x < v[i].x
      || (v[j].x == v[i].x && (v[j].y < v[i].y
      || (v[j].y == v[i].y && v[j].z < v[i].z)));
    if (swap) { const BspVertex tmp = v[i]; v[i] = v[j]; v[j] = tmp; }
  }
  return {(v[0].x + v[1].x + v[2].x) / 3.0f,
          (v[0].y + v[1].y + v[2].y) / 3.0f,
          (v[0].z + v[1].z + v[2].z) / 3.0f};
}

void dumpVertex(const char* name, const BspVertex& v) {
  std::printf("      %s=(%.1f, %.1f, %.1f)", name, v.x, v.y, v.z);
}

} // namespace

int main(int argc, char** argv) {
  std::string tfRoot;
  std::string mapPath;
  std::string dumpLightmapPath;
  std::size_t triangleLimit = 4000000;
  bool haveEye = false;
  float eyeX = 0.0f, eyeY = 0.0f, eyeZ = 0.0f;
  int rayCells = 0;
  int rayEyeCells = 0;
  float rayEyeX = 0.0f, rayEyeY = 0.0f, rayEyeZ = 0.0f;
  // T11 third round: the decision table. Each first-hit oracle reduces to four
  // cells -- (class, side) -- and for each cell the count of first hits whose
  // STORED winding already faces the observer, against the cell's total. A rule
  // is then a keep/flip choice per cell, sixteen of them, and both views can be
  // scored from the same four numbers without rendering anything.
  std::size_t topFacing[2][2] = {{0, 0}, {0, 0}};   // stored winding faces the sky
  std::size_t topTotal[2][2] = {{0, 0}, {0, 0}};
  std::size_t eyeFacing[2][2] = {{0, 0}, {0, 0}};   // stored winding faces the eye
  std::size_t eyeTotal[2][2] = {{0, 0}, {0, 0}};
  // T17: the product parses with the parser's default atlas width (4096), which
  // for pl_odyssey produces a 4096x2251 page -- not a square. The uv measurement
  // below is only about the product's page if this number matches it, so it is
  // an option rather than a constant.
  int atlasWidth = 4096;
  // T23: score the fallback-quad defect without changing the product. A
  // displacement face whose grid is refused is emitted as its flat quad through
  // the brush path, so it keeps the brush winding rule; the proposed repair
  // orients exactly those triangles to the face's own side-aware outward
  // direction, which is the rule the grid would have used. With this flag the
  // oracles treat them as repaired and print the first-hit delta the change
  // would buy before a single pixel is drawn.
  bool virtualFallbackOrient = false;
  // T26: face ids named on the command line, printed one line each. The
  // question a hole has to answer is "which face is under this pixel", and the
  // face-id frame answers it with a number; this turns that number back into a
  // material, a class, a plane and an extent without dumping 18,000 rows.
  std::vector<int> faceInfoIds;
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--tf-root") == 0 && i + 1 < argc) tfRoot = argv[++i];
    else if (std::strcmp(argv[i], "--map") == 0 && i + 1 < argc) mapPath = argv[++i];
    else if (std::strcmp(argv[i], "--atlas-width") == 0 && i + 1 < argc) atlasWidth = std::atoi(argv[++i]);
    else if (std::strcmp(argv[i], "--fallback-orient") == 0) virtualFallbackOrient = true;
    else if (std::strcmp(argv[i], "--face-info") == 0 && i + 1 < argc) {
      const std::string list = argv[++i];
      std::size_t at = 0;
      while (at <= list.size()) {
        const auto comma = list.find(',', at);
        const std::string token = list.substr(at, comma == std::string::npos ? std::string::npos : comma - at);
        if (!token.empty()) faceInfoIds.push_back(std::atoi(token.c_str()));
        if (comma == std::string::npos) break;
        at = comma + 1;
      }
    }
    else if (std::strcmp(argv[i], "--dump-lightmap") == 0 && i + 1 < argc) dumpLightmapPath = argv[++i];
    else if (std::strcmp(argv[i], "--triangle-limit") == 0 && i + 1 < argc) triangleLimit = static_cast<std::size_t>(std::stoull(argv[++i]));
    else if (std::strcmp(argv[i], "--ray-down") == 0 && i + 1 < argc) {
      // T11 third round: vertical first-hit census. N is the grid resolution
      // across the map's X and Y extents; each cell casts one ray straight down
      // from above the level and keeps the TOPMOST triangle at that (x, y).
      rayCells = std::atoi(argv[++i]);
      if (rayCells < 1) rayCells = 1;
      if (rayCells > 512) rayCells = 512;
    }
    else if (std::strcmp(argv[i], "--ray-eye") == 0 && i + 2 < argc) {
      // T11 third round: the same first-hit question asked from INSIDE the
      // level, in every direction. A closed room's first surface in any
      // direction is the one the eye sees, so its winding must face the eye --
      // and that is a statement about the data, not about any camera.
      if (std::sscanf(argv[++i], "%f,%f,%f", &rayEyeX, &rayEyeY, &rayEyeZ) == 3) {
        rayEyeCells = std::atoi(argv[++i]);
        if (rayEyeCells < 2) rayEyeCells = 2;
        if (rayEyeCells > 128) rayEyeCells = 128;
      }
    }
    else if (std::strcmp(argv[i], "--eye") == 0 && i + 1 < argc) {
      // MAP coordinates, comma-separated -- the same numbers the product's
      // world-vis trace line prints as eyeMap, so a probe run and a capture
      // can be compared without a unit conversion in somebody's head.
      if (std::sscanf(argv[++i], "%f,%f,%f", &eyeX, &eyeY, &eyeZ) == 3) haveEye = true;
    }
  }
  if (tfRoot.empty() || mapPath.empty()) {
    std::fprintf(stderr, "usage: world_geometry_probe --tf-root <tf> --map <maps/name.bsp> [--triangle-limit N]\n");
    return 2;
  }
  const auto root = AssetRoot::fromPath(tfRoot);
  if (!root.valid()) {
    std::fprintf(stderr, "geometry: invalid tf root\n");
    return 2;
  }
  const auto bytes = readAsset(root, mapPath);
  if (bytes.empty()) {
    std::printf("geometry status=missing map=%s reason=the bsp could not be read from loose files or the vpk set\n", mapPath.c_str());
    return 1;
  }
  BspMap map;
  if (!tf2::native::BspParser::parse(bytes, map, triangleLimit, atlasWidth)) {
    std::printf("geometry status=parse-failed map=%s triangles=%zu reason=the bsp parser rejected the file\n",
                mapPath.c_str(), map.triangles.size());
    return 1;
  }

  // T23: the facing test, with an optional virtual repair for the fallback
  // quads. The normal passed in is the EMITTED winding normal; `view` is the
  // direction from the eye (or +Z for the top-down oracle). The repair does not
  // move any vertex -- it only says which side the triangle would face if the
  // fallback fan were oriented the way its own face's side bit asks.
  std::size_t virtualFlipEvaluated = 0, virtualFlipApplied = 0;
  const auto orientedFacing = [&](const tf2::native::BspTriangle& triangle,
                                  float nx, float ny, float nz,
                                  float vx, float vy, float vz) {
    float dot = nx * vx + ny * vy + nz * vz;
    if (virtualFallbackOrient && triangle.fallbackQuad) {
      ++virtualFlipEvaluated;
      if (triangle.planeIndex >= 0
          && static_cast<std::size_t>(triangle.planeIndex) < map.planes.size()) {
        const auto& plane = map.planes[static_cast<std::size_t>(triangle.planeIndex)];
        const float sideSign = triangle.planeSide == 0 ? 1.0f : -1.0f;
        const float ox = sideSign * plane.normal[0];
        const float oy = sideSign * plane.normal[1];
        const float oz = sideSign * plane.normal[2];
        if (nx * ox + ny * oy + nz * oz < 0.0f) { dot = -dot; ++virtualFlipApplied; }
      }
    }
    return dot > 0.0f;
  };
  std::size_t displaced = 0, water = 0, lightmapped = 0, degenerate = 0;
  std::size_t over256 = 0, over1024 = 0, over4096 = 0;
  float maxEdge = 0.0f;
  float staticMaxEdge = 0.0f, displacedMaxEdge = 0.0f;
  std::size_t staticOver1024 = 0, displacedOver1024 = 0;
  std::size_t staticMaterialOver1024 = 0;
  std::vector<std::size_t> offenders;      // sorted by longest edge, descending
  std::vector<float> longest(map.triangles.size(), 0.0f);
  float minX = 1e30f, minY = 1e30f, minZ = 1e30f, maxX = -1e30f, maxY = -1e30f, maxZ = -1e30f;
  for (std::size_t i = 0; i < map.triangles.size(); ++i) {
    const auto& t = map.triangles[i];
    if (t.displacement) ++displaced;
    if (t.water) ++water;
    if (t.lightmapped) ++lightmapped;
    const float e = std::max({edge(t.a, t.b), edge(t.b, t.c), edge(t.c, t.a)});
    longest[i] = e;
    maxEdge = std::max(maxEdge, e);
    if (e > 256.0f) ++over256;
    if (e > 1024.0f) ++over1024;
    if (e > 4096.0f) ++over4096;
    if (t.displacement) {
      displacedMaxEdge = std::max(displacedMaxEdge, e);
      if (e > 1024.0f) ++displacedOver1024;
    } else {
      staticMaxEdge = std::max(staticMaxEdge, e);
      if (e > 1024.0f) ++staticOver1024;
      if (e > 1024.0f && (t.material.find("NATURE/") == 0 || t.material.find("BLEND") != std::string::npos)) ++staticMaterialOver1024;
    }
    if (triangleArea(t) < 1.0e-4f) ++degenerate;
    for (const auto& v : {t.a, t.b, t.c}) {
      minX = std::min(minX, v.x); minY = std::min(minY, v.y); minZ = std::min(minZ, v.z);
      maxX = std::max(maxX, v.x); maxY = std::max(maxY, v.y); maxZ = std::max(maxZ, v.z);
    }
    if (e > 1024.0f) offenders.push_back(i);
  }
  std::sort(offenders.begin(), offenders.end(),
    [&](std::size_t l, std::size_t r) { return longest[l] > longest[r]; });
  std::printf("geometry status=ok map=%s version=%d triangles=%zu faces=%zu "
              "displaced=%zu water=%zu lightmapped=%zu degenerate=%zu "
              "maxEdge=%.1f over256=%zu over1024=%zu over4096=%zu "
              "bounds=(%.0f,%.0f,%.0f)..(%.0f,%.0f,%.0f)\n",
              mapPath.c_str(), map.version, map.triangles.size(), map.faceCount,
              displaced, water, lightmapped, degenerate, maxEdge, over256, over1024, over4096,
              minX, minY, minZ, maxX, maxY, maxZ);
  // T23: the face-level account. A face that never becomes a polygon is
  // geometry that no winding rule, pvs row or cull mode can bring back, so it
  // is separated from the faces that emitted and from the ones a displacement
  // fallback substituted for.
  std::printf("geometry faces total=%zu rendered=%zu sky=%zu skippedNoDraw=%zu skippedInvalid=%zu\n",
              map.faceCount, map.renderedFaceCount, map.skyFaces,
              map.facesSkippedNoDraw, map.facesSkippedInvalid);
  // T26: the sky faces as geometry rather than as a count. `triangleCount` above
  // is the product's buffer and must not include them; this line says how many
  // polygons the sky brushes contribute, which is what the `--world-geometry
  // sky` mask draws and therefore what a missing pixel can be attributed to.
  std::printf("geometry sky faces=%zu triangles=%zu bufferTriangles=%zu\n",
              map.skyFaces, map.skyTriangles.size(), map.triangles.size());
  if (!faceInfoIds.empty()) {
    struct FaceInfo {
      bool present = false, sky = false, displacement = false, fallback = false;
      std::string material;
      std::size_t triangles = 0;
      int planeIndex = -1, planeSide = -1, cluster = -1;
      double sumX = 0.0, sumY = 0.0, sumZ = 0.0;
      float minX = 1e30f, minY = 1e30f, minZ = 1e30f, maxX = -1e30f, maxY = -1e30f, maxZ = -1e30f;
    };
    std::unordered_map<int, FaceInfo> info;
    const auto collect = [&](const BspTriangle& t, bool sky) {
      if (t.faceIndex < 0) return;
      auto& entry = info[t.faceIndex];
      entry.present = true;
      entry.sky = sky;
      entry.material = t.material;
      entry.displacement = entry.displacement || t.displacement;
      entry.fallback = entry.fallback || t.fallbackQuad;
      entry.planeIndex = t.planeIndex;
      entry.planeSide = t.planeSide;
      entry.cluster = t.cluster;
      ++entry.triangles;
      for (const auto& v : {t.a, t.b, t.c}) {
        entry.sumX += v.x; entry.sumY += v.y; entry.sumZ += v.z;
        entry.minX = std::min(entry.minX, v.x); entry.minY = std::min(entry.minY, v.y); entry.minZ = std::min(entry.minZ, v.z);
        entry.maxX = std::max(entry.maxX, v.x); entry.maxY = std::max(entry.maxY, v.y); entry.maxZ = std::max(entry.maxZ, v.z);
      }
    };
    for (const auto& triangle : map.triangles) collect(triangle, false);
    for (const auto& triangle : map.skyTriangles) collect(triangle, true);
    for (const int id : faceInfoIds) {
      const auto found = info.find(id);
      if (found == info.end()) {
        std::printf("geometry face-info index=%d state=absent reason=no emitted triangle carries this face\n", id);
        continue;
      }
      const auto& entry = found->second;
      const double vertices = entry.triangles == 0 ? 1.0 : static_cast<double>(entry.triangles) * 3.0;
      std::printf("geometry face-info index=%d kind=%s material=%s triangles=%zu displacement=%d fallback=%d"
                  " plane=%d planeSide=%d cluster=%d centroid=(%.1f,%.1f,%.1f)"
                  " bounds=(%.1f,%.1f,%.1f)..(%.1f,%.1f,%.1f)\n",
                  id, entry.sky ? "sky" : "world", entry.material.c_str(), entry.triangles,
                  entry.displacement ? 1 : 0, entry.fallback ? 1 : 0,
                  entry.planeIndex, entry.planeSide, entry.cluster,
                  entry.sumX / vertices, entry.sumY / vertices, entry.sumZ / vertices,
                  entry.minX, entry.minY, entry.minZ, entry.maxX, entry.maxY, entry.maxZ);
    }
  }
  // T17: do the lightmap uvs address the texels the packer actually wrote?
  //
  // The atlas is a shelf pack of per-face rectangles on a 0-filled page whose
  // alpha is 255 exactly where a luxel was written. So "the uv lands on an
  // unwritten texel" is directly measurable: read the alpha under the uv. A
  // correct uv set reads 0 here whatever the lighting is; a v axis normalised
  // by the atlas WIDTH instead of its HEIGHT reads a large fraction, because
  // the atlas is not square (pl_odyssey: 4096x2251).
  if (map.lightmapMode == tf2::native::BspLightmapMode::RgbExp32
      && map.lightmapAtlasWidth > 0 && map.lightmapAtlasHeight > 0
      && !map.lightmapAtlas.empty()) {
    const int width = map.lightmapAtlasWidth, height = map.lightmapAtlasHeight;
    std::size_t vertices = 0, unwritten = 0, sampled = 0;
    double luminance = 0.0;
    for (const auto& triangle : map.triangles) {
      if (!triangle.lightmapped) continue;
      const float uv[3][2] = {{triangle.alu, triangle.alv}, {triangle.blu, triangle.blv},
                              {triangle.clu, triangle.clv}};
      for (const auto& corner : uv) {
        ++vertices;
        const int x = static_cast<int>(std::floor(corner[0] * static_cast<float>(width)));
        const int y = static_cast<int>(std::floor(corner[1] * static_cast<float>(height)));
        if (x < 0 || y < 0 || x >= width || y >= height) { ++unwritten; continue; }
        const std::size_t at = (static_cast<std::size_t>(y) * static_cast<std::size_t>(width)
          + static_cast<std::size_t>(x)) * 4u;
        if (map.lightmapAtlas[at + 3] == 0) { ++unwritten; continue; }
        ++sampled;
        luminance += (static_cast<double>(map.lightmapAtlas[at])
          + map.lightmapAtlas[at + 1] + map.lightmapAtlas[at + 2]) / 3.0;
      }
    }
    std::printf("geometry lightmap-uv atlas=%dx%d vertices=%zu unwrittenTexels=%zu "
                "unwrittenShare=%.4f sampledMeanLum=%.1f\n",
                width, height, vertices, unwritten,
                vertices == 0 ? 0.0 : static_cast<double>(unwritten) / static_cast<double>(vertices),
                sampled == 0 ? 0.0 : luminance / static_cast<double>(sampled));
    // T17: the mean is not enough. A page whose mean is 110 can still be black
    // where most triangles sample, and "most of the frame is black" is a claim
    // about the distribution, not about the average. The histogram says how many
    // emitted triangles read a dark texel; the per-material table says which
    // surfaces those are.
    std::vector<std::size_t> histogram(8, 0);
    struct MaterialTone { std::size_t triangles = 0; double sum = 0.0; std::size_t dark = 0; };
    std::unordered_map<std::string, MaterialTone> tones;
    double darkSum = 0.0; std::size_t darkTriangles = 0;
    for (const auto& triangle : map.triangles) {
      if (!triangle.lightmapped) continue;
      const float uv[3][2] = {{triangle.alu, triangle.alv}, {triangle.blu, triangle.blv},
                              {triangle.clu, triangle.clv}};
      double tone = 0.0; int samples = 0;
      for (const auto& corner : uv) {
        const int x = std::clamp(static_cast<int>(std::floor(corner[0] * static_cast<float>(width))), 0, width - 1);
        const int y = std::clamp(static_cast<int>(std::floor(corner[1] * static_cast<float>(height))), 0, height - 1);
        const std::size_t at = (static_cast<std::size_t>(y) * static_cast<std::size_t>(width)
          + static_cast<std::size_t>(x)) * 4u;
        tone += (static_cast<double>(map.lightmapAtlas[at])
          + map.lightmapAtlas[at + 1] + map.lightmapAtlas[at + 2]) / 3.0;
        ++samples;
      }
      tone /= static_cast<double>(samples == 0 ? 1 : samples);
      ++histogram[std::min<std::size_t>(7, static_cast<std::size_t>(tone / 32.0))];
      auto& entry = tones[triangle.material];
      ++entry.triangles;
      entry.sum += tone;
      if (tone < 24.0) { darkSum += tone; ++darkTriangles; ++entry.dark; }
    }
    std::printf("geometry lightmap-tone histogram=%zu,%zu,%zu,%zu,%zu,%zu,%zu,%zu darkTriangles=%zu darkShare=%.4f\n",
                histogram[0], histogram[1], histogram[2], histogram[3],
                histogram[4], histogram[5], histogram[6], histogram[7],
                darkTriangles, map.triangleCount == 0 ? 0.0
                  : static_cast<double>(darkTriangles) / static_cast<double>(map.triangleCount));
    // T17: the same histogram taken on the rectangles the packer WROTE rather
    // than on the uvs. Two readings of the same page: this one cannot be wrong
    // about the data, the one above cannot be wrong about the sampling. When
    // they agree the frame is dark because the map baked it dark; when they
    // disagree the uvs are reading somebody else's rectangle.
    std::printf("geometry lightmap-face-tone histogram=%u,%u,%u,%u,%u,%u,%u,%u darkFaces=%zu zeroFaces=%zu\n",
                map.lightmapFaceToneHistogram[0], map.lightmapFaceToneHistogram[1],
                map.lightmapFaceToneHistogram[2], map.lightmapFaceToneHistogram[3],
                map.lightmapFaceToneHistogram[4], map.lightmapFaceToneHistogram[5],
                map.lightmapFaceToneHistogram[6], map.lightmapFaceToneHistogram[7],
                map.lightmapFaceToneDark, map.lightmapFaceToneZero);
    if (map.hasHdrLightmap) {
      std::printf("geometry lightmap-hdr-tone histogram=%u,%u,%u,%u,%u,%u,%u,%u darkFaces=%zu zeroFaces=%zu intensity=%.3f samples=%zu\n",
                  map.lightmapHdrFaceToneHistogram[0], map.lightmapHdrFaceToneHistogram[1],
                  map.lightmapHdrFaceToneHistogram[2], map.lightmapHdrFaceToneHistogram[3],
                  map.lightmapHdrFaceToneHistogram[4], map.lightmapHdrFaceToneHistogram[5],
                  map.lightmapHdrFaceToneHistogram[6], map.lightmapHdrFaceToneHistogram[7],
                  map.lightmapHdrFaceToneDark, map.lightmapHdrFaceToneZero,
                  static_cast<double>(map.hdrLightmapIntensity), map.hdrLightmapSampleCount);
    } else {
      std::printf("geometry lightmap-hdr-tone state=absent reason=lump 53 is missing, empty, or not the size of lump 8\n");
    }
    std::vector<std::pair<double, std::string>> ranked;
    for (const auto& entry : tones) {
      if (entry.second.triangles == 0) continue;
      ranked.emplace_back(entry.second.sum / static_cast<double>(entry.second.triangles),
                          entry.first + " n=" + std::to_string(entry.second.triangles));
    }
    std::sort(ranked.begin(), ranked.end());
    std::printf("geometry lightmap-tone darkest:");
    for (std::size_t i = 0; i < ranked.size() && i < 8; ++i) {
      std::printf(" %.0f[%s]", ranked[i].first, ranked[i].second.c_str());
    }
    std::printf("\n");
    // T17: which surfaces the dark triangles belong to, by count rather than by
    // average. The list above ranks materials by mean tone and is therefore
    // dominated by materials with three triangles each; this one says where the
    // 28% of the frame that reads black actually lives.
    std::vector<std::pair<std::size_t, std::string>> byDark;
    for (const auto& entry : tones) {
      if (entry.second.dark == 0) continue;
      byDark.emplace_back(entry.second.dark, entry.first + " n="
        + std::to_string(entry.second.triangles));
    }
    std::sort(byDark.begin(), byDark.end(),
      [](const auto& left, const auto& right) { return left.first > right.first; });
    std::printf("geometry lightmap-tone darkMaterials:");
    for (std::size_t i = 0; i < byDark.size() && i < 8; ++i) {
      std::printf(" %zu[%s]", byDark[i].first, byDark[i].second.c_str());
    }
    std::printf("\n");
    // T17: the page itself, as a picture. "The lightmap is dark" and "the uv
    // samples a dark part of a fine page" are different diagnoses and the tone
    // table cannot tell them apart; the atlas can, because it carries its own
    // alpha (255 where a luxel was written).
    if (!dumpLightmapPath.empty()) {
      const std::uint32_t rowBytes = static_cast<std::uint32_t>(width) * 3u;
      const std::uint32_t padding = (4u - (rowBytes % 4u)) % 4u;
      const std::uint32_t imageBytes = (rowBytes + padding) * static_cast<std::uint32_t>(height);
      std::FILE* file = std::fopen(dumpLightmapPath.c_str(), "wb");
      if (file) {
        std::uint8_t header[54] = {};
        header[0] = 'B'; header[1] = 'M';
        const std::uint32_t fileSize = 54u + imageBytes;
        std::memcpy(header + 2, &fileSize, 4);
        const std::uint32_t dataOffset = 54u;
        std::memcpy(header + 10, &dataOffset, 4);
        const std::uint32_t infoSize = 40u;
        std::memcpy(header + 14, &infoSize, 4);
        std::memcpy(header + 18, &width, 4);
        const std::int32_t storedHeight = height;
        std::memcpy(header + 22, &storedHeight, 4);
        const std::uint16_t planes = 1, bpp = 24;
        std::memcpy(header + 26, &planes, 2);
        std::memcpy(header + 28, &bpp, 2);
        std::fwrite(header, 1, sizeof(header), file);
        std::vector<std::uint8_t> row(rowBytes + padding, 0);
        for (int y = height - 1; y >= 0; --y) {
          for (int x = 0; x < width; ++x) {
            const std::size_t at = (static_cast<std::size_t>(y) * static_cast<std::size_t>(width)
              + static_cast<std::size_t>(x)) * 4u;
            row[static_cast<std::size_t>(x) * 3u + 0] = map.lightmapAtlas[at + 2];
            row[static_cast<std::size_t>(x) * 3u + 1] = map.lightmapAtlas[at + 1];
            row[static_cast<std::size_t>(x) * 3u + 2] = map.lightmapAtlas[at + 0];
          }
          std::fwrite(row.data(), 1, row.size(), file);
        }
        std::fclose(file);
        std::printf("geometry lightmap-dump path=%s size=%dx%d\n", dumpLightmapPath.c_str(), width, height);
      }
    }
  }
  std::printf("geometry edge-groups staticMaxEdge=%.1f staticOver1024=%zu staticMaterialOver1024=%zu displacedMaxEdge=%.1f displacedOver1024=%zu\n",
              staticMaxEdge, staticOver1024, staticMaterialOver1024, displacedMaxEdge, displacedOver1024);
  std::printf("geometry displacement faces=%zu triangles=%zu cornerMatches=%zu\n",
              map.displacementFaces, map.displacementTriangles, map.displacementCornerMatches);
  // T23: terrain completeness at the face level. A candidate face that does not
  // emit a grid is drawn as its flat quad, so it is not a hole -- but a map
  // with fallbacks is a map whose terrain detail is missing at named faces, and
  // the reason list says which branch refused them. `candidates == faces` with
  // `fallback == 0` is the clean reading: every displacement face the bsp names
  // was actually tessellated.
  std::printf("geometry displacement coverage candidates=%zu emitted=%zu fallback=%zu"
              " (lump=%zu polygon=%zu index=%zu power=%zu range=%zu corner=%zu"
              " nonFinite=%zu cell=%zu limit=%zu other=%zu)\n",
              map.displacementFaceCandidates, map.displacementFaces, map.displacementFallbackFaces,
              map.displacementFallbackLump, map.displacementFallbackPolygon,
              map.displacementFallbackIndex, map.displacementFallbackPower,
              map.displacementFallbackRange, map.displacementFallbackCorner,
              map.displacementFallbackNonFinite, map.displacementFallbackCell,
              map.displacementFallbackLimit, map.displacementFallbackOther);
  for (std::size_t i = 0; i < map.displacementFallbackFaceIndex.size() && i < 12; ++i) {
    std::printf("geometry displacement fallback-face index=%d material=%s\n",
                map.displacementFallbackFaceIndex[i], map.displacementFallbackMaterial[i].c_str());
  }
  {
    std::size_t fallbackTriangles = 0;
    for (const auto& triangle : map.triangles) if (triangle.fallbackQuad) ++fallbackTriangles;
    std::printf("geometry displacement fallback-quads triangles=%zu\n", fallbackTriangles);
  }
  std::printf("geometry winding agree=%zu of %zu faces (%.1f%% agree with the bsp plane normals)\n",
              map.windingAgreeCount, map.windingFaceCount,
              map.windingFaceCount == 0 ? 0.0 : 100.0 * static_cast<double>(map.windingAgreeCount)
                / static_cast<double>(map.windingFaceCount));
  // T11: the emission-time orientation. Every emitted triangle is tested
  // against its face's own front normal and reversed when it points the wrong
  // way; the count is what the cull mode would otherwise have removed. A map
  // whose source winding were uniform would read 0 here on one side and all of
  // them on the other; pl_odyssey reads a mixed split, which is the data-level
  // reason a single global flip could never have worked.
  std::printf("geometry winding flippedTriangles=%zu of %zu\n",
              map.windingFlippedTriangles, map.triangles.size());
  // T11 follow-up: how many emitted triangles still point away from their own
  // face's side-aware outward direction. This is the residual the cull mode
  // removes, split so a displacement-only or brush-only regression cannot hide
  // in the total. A coherent buffer reads 0.
  std::printf("geometry winding sideAware misfaced=%zu displacedMisfaced=%zu sideOneFaces=%zu of %zu\n",
              map.windingMisfacedTriangles, map.windingMisfacedDisplaced,
              map.windingSideOneFaces, map.windingFaceCount);
  // T11 third round: is the SOURCE order a convention or a coin toss? Same
  // comparison as `winding agree`, but per class and WITHOUT the side bit. A
  // class that reads one column at zero has a fixed convention, and the
  // emission rule for that class is a constant that can simply be written down;
  // a class that splits is the one no single sign can orient, and the split is
  // the size of the residual.
  std::printf("geometry stored-winding class=brush alongPlane=%zu againstPlane=%zu\n",
              map.windingStoredBrushAlong, map.windingStoredBrushAgainst);
  std::printf("geometry stored-winding class=displacement alongPlane=%zu againstPlane=%zu\n",
              map.windingStoredDispAlong, map.windingStoredDispAgainst);
  std::printf("geometry grid-vs-polygon keeps=%zu reverses=%zu of %zu displacement faces\n",
              map.windingGridKeepsPolygon, map.windingGridReversesPolygon, map.displacementFaces);
  // T11 third round: the EMITTED order before any reversal. This is the buffer's
  // own convention, which is what a rule has to be stated against -- the polygon
  // census above describes the face, and the brush fan emits the reverse of it.
  std::printf("geometry pre-rule class=brush alongPlane=%zu againstPlane=%zu alongOutward=%zu againstOutward=%zu\n",
              map.windingPreBrushAlongPlane, map.windingPreBrushAgainstPlane,
              map.windingPreBrushAlongOutward, map.windingPreBrushAgainstOutward);
  std::printf("geometry pre-rule class=displacement alongPlane=%zu againstPlane=%zu alongOutward=%zu againstOutward=%zu\n",
              map.windingPreDispAlongPlane, map.windingPreDispAgainstPlane,
              map.windingPreDispAlongOutward, map.windingPreDispAgainstOutward);
  // T11 third round: the first-hit oracle. A vertical ray from above the level
  // meets exactly one surface first, and that surface is the one a top-down
  // camera draws. Whether its STORED winding points up is therefore not a
  // question about the plane lump, the side bit or the emission rule: it is the
  // map's own answer about which side of the surface faces the sky. Recovering
  // the stored order is why every triangle carries `windingFlipped`.
  if (rayCells > 0) {
    const float spanX = std::max(1.0f, maxX - minX);
    const float spanY = std::max(1.0f, maxY - minY);
    const float cellW = spanX / static_cast<float>(rayCells);
    const float cellH = spanY / static_cast<float>(rayCells);
    const std::size_t cells = static_cast<std::size_t>(rayCells) * static_cast<std::size_t>(rayCells);
    std::vector<std::vector<std::size_t>> buckets(cells);
    for (std::size_t index = 0; index < map.triangles.size(); ++index) {
      const auto& t = map.triangles[index];
      const float loX = std::min({t.a.x, t.b.x, t.c.x});
      const float hiX = std::max({t.a.x, t.b.x, t.c.x});
      const float loY = std::min({t.a.y, t.b.y, t.c.y});
      const float hiY = std::max({t.a.y, t.b.y, t.c.y});
      int i0 = static_cast<int>((loX - minX) / cellW);
      int i1 = static_cast<int>((hiX - minX) / cellW);
      int j0 = static_cast<int>((loY - minY) / cellH);
      int j1 = static_cast<int>((hiY - minY) / cellH);
      i0 = std::clamp(i0, 0, rayCells - 1); i1 = std::clamp(i1, 0, rayCells - 1);
      j0 = std::clamp(j0, 0, rayCells - 1); j1 = std::clamp(j1, 0, rayCells - 1);
      for (int j = j0; j <= j1; ++j) {
        for (int i = i0; i <= i1; ++i) {
          buckets[static_cast<std::size_t>(j) * static_cast<std::size_t>(rayCells) + static_cast<std::size_t>(i)]
            .push_back(index);
        }
      }
    }
    std::size_t hits = 0, misses = 0, drawn = 0;
    std::size_t brushUp = 0, brushDown = 0, dispUp = 0, dispDown = 0;
    std::size_t brushSide[2][2] = {{0, 0}, {0, 0}};   // [side][storedUp]
    std::size_t dispSide[2][2] = {{0, 0}, {0, 0}};
    for (int j = 0; j < rayCells; ++j) {
      for (int i = 0; i < rayCells; ++i) {
        const float px = minX + (static_cast<float>(i) + 0.5f) * cellW;
        const float py = minY + (static_cast<float>(j) + 0.5f) * cellH;
        float bestZ = -1.0e30f;
        std::size_t bestIndex = map.triangles.size();
        for (const auto index : buckets[static_cast<std::size_t>(j) * static_cast<std::size_t>(rayCells) + static_cast<std::size_t>(i)]) {
          const auto& t = map.triangles[index];
          const float d = (t.b.y - t.c.y) * (t.a.x - t.c.x) + (t.c.x - t.b.x) * (t.a.y - t.c.y);
          if (std::fabs(d) < 1.0e-6f) continue; // edge-on to the ray: never the top surface
          const float l1 = ((t.b.y - t.c.y) * (px - t.c.x) + (t.c.x - t.b.x) * (py - t.c.y)) / d;
          const float l2 = ((t.c.y - t.a.y) * (px - t.c.x) + (t.a.x - t.c.x) * (py - t.c.y)) / d;
          const float l3 = 1.0f - l1 - l2;
          constexpr float kEdge = -1.0e-4f;
          if (l1 < kEdge || l2 < kEdge || l3 < kEdge) continue;
          const float z = l1 * t.a.z + l2 * t.b.z + l3 * t.c.z;
          if (z > bestZ) { bestZ = z; bestIndex = index; }
        }
        if (bestIndex >= map.triangles.size()) { ++misses; continue; }
        ++hits;
        const auto& t = map.triangles[bestIndex];
        const float ux = t.b.x - t.a.x, uy = t.b.y - t.a.y, uz = t.b.z - t.a.z;
        const float vx = t.c.x - t.a.x, vy = t.c.y - t.a.y, vz = t.c.z - t.a.z;
        const float nz = ux * vy - uy * vx;             // emitted normal, z only
        const bool emittedUp = orientedFacing(t, 0.0f, 0.0f, nz, 0.0f, 0.0f, 1.0f);
        const bool storedUp = (t.windingFlipped != 0) ? !emittedUp : emittedUp;
        if (emittedUp) ++drawn;
        if (t.displacement) {
          if (storedUp) ++dispUp; else ++dispDown;
          ++dispSide[t.planeSide != 0 ? 1 : 0][storedUp ? 1 : 0];
          ++topTotal[1][t.planeSide != 0 ? 1 : 0];
          if (storedUp) ++topFacing[1][t.planeSide != 0 ? 1 : 0];
        } else {
          if (storedUp) ++brushUp; else ++brushDown;
          ++brushSide[t.planeSide != 0 ? 1 : 0][storedUp ? 1 : 0];
          ++topTotal[0][t.planeSide != 0 ? 1 : 0];
          if (storedUp) ++topFacing[0][t.planeSide != 0 ? 1 : 0];
        }
      }
    }
    const std::size_t total = hits == 0 ? 1 : hits;
    std::printf("geometry ray-down cells=%zu hits=%zu misses=%zu drawnByShippedRule=%zu of %zu (%.3f)\n",
                cells, hits, misses, drawn, total,
                static_cast<double>(drawn) / static_cast<double>(total));
    std::printf("geometry ray-down first-hit class=brush storedUp=%zu storedDown=%zu\n", brushUp, brushDown);
    std::printf("geometry ray-down first-hit class=displacement storedUp=%zu storedDown=%zu\n", dispUp, dispDown);
    for (int s = 0; s < 2; ++s) {
      std::printf("geometry ray-down first-hit brush planeSide=%d storedUp=%zu storedDown=%zu\n",
                  s, brushSide[s][1], brushSide[s][0]);
      std::printf("geometry ray-down first-hit displacement planeSide=%d storedUp=%zu storedDown=%zu\n",
                  s, dispSide[s][1], dispSide[s][0]);
    }
  }
  // How many materials the map asks for. The renderer's world atlas holds at
  // most 64 tiles, so a map with more distinct materials than that draws the
  // remainder flat-shaded -- a number that has to be printed somewhere or the
  // atlas cap stays invisible.
  {
    std::vector<std::pair<std::size_t, std::string>> counts;
    for (const auto& t : map.triangles) {
      auto it = std::find_if(counts.begin(), counts.end(),
        [&](const std::pair<std::size_t, std::string>& c) { return c.second == t.material; });
      if (it == counts.end()) counts.emplace_back(1, t.material);
      else ++it->first;
    }
    std::sort(counts.begin(), counts.end(), [](const auto& l, const auto& r) { return l.first > r.first; });
    std::size_t top64 = 0;
    for (std::size_t i = 0; i < counts.size() && i < 64; ++i) top64 += counts[i].first;
    const std::size_t total = map.triangles.empty() ? 1 : map.triangles.size();
    std::printf("geometry materials distinct=%zu atlasCap=64 trianglesCoveredByCap=%zu of %zu (%.1f%%)\n",
                counts.size(), top64, total, 100.0 * static_cast<double>(top64) / static_cast<double>(total));
    const std::size_t top = std::min<std::size_t>(counts.size(), 8);
    for (std::size_t i = 0; i < top; ++i) {
      std::printf("geometry material rank=%zu triangles=%zu name=%s\n", i, counts[i].first, counts[i].second.c_str());
    }
  }
  const std::size_t show = std::min<std::size_t>(offenders.size(), 12);
  for (std::size_t n = 0; n < show; ++n) {
    const auto index = offenders[n];
    const auto& t = map.triangles[index];
    std::printf("geometry offender index=%zu longest=%.1f area=%.1f displacement=%d water=%d material=%s cluster=%d\n",
                index, longest[index], triangleArea(t), t.displacement ? 1 : 0, t.water ? 1 : 0,
                t.material.c_str(), t.cluster);
    dumpVertex("a", t.a); dumpVertex("b", t.b); dumpVertex("c", t.c);
    std::printf("\n");
  }
  // T10 --eye: the from-eye facing census. The rasteriser keeps a triangle
  // whose emitted winding (a, b, c) runs clockwise as the camera sees it,
  // which for the maths here means the cross-product normal points AWAY from
  // the eye... or TOWARD it, depending on the convention the parser and the
  // cull mode agreed on -- and that convention is exactly what this census
  // arbitrates. For a closed level seen from an eye inside it, the surfaces
  // that should survive culling are the ones whose front faces the eye, so a
  // healthy pipeline reads "most PVS-kept triangles front-facing"; an
  // inverted convention reads the reverse. Split by the pvs clip's own
  // decision (kept / hidden / no cluster) so the two world defects cannot
  // hide inside each other's totals.
  if (haveEye) {
    const int cameraCluster = tf2::native::BspParser::clusterAt(map, eyeX, eyeY, eyeZ);
    const int visibleClusters = cameraCluster >= 0
      ? tf2::native::BspParser::countVisibleClusters(map, cameraCluster) : -1;
    std::printf("geometry eye=(%.1f,%.1f,%.1f) cluster=%d visClusters=%d bounds=(%.0f,%.0f,%.0f)..(%.0f,%.0f,%.0f)\n",
                eyeX, eyeY, eyeZ, cameraCluster, visibleClusters, minX, minY, minZ, maxX, maxY, maxZ);
    std::size_t keptFront = 0, keptBack = 0, hiddenFront = 0, hiddenBack = 0;
    std::size_t noClusterFront = 0, noClusterBack = 0;
    for (const auto& t : map.triangles) {
      const float ux = t.b.x - t.a.x, uy = t.b.y - t.a.y, uz = t.b.z - t.a.z;
      const float vx = t.c.x - t.a.x, vy = t.c.y - t.a.y, vz = t.c.z - t.a.z;
      const float cx = uy * vz - uz * vy, cy = uz * vx - ux * vz, cz = ux * vy - uy * vx;
      // Signed twice-area cross product dotted with eye->a: positive means the
      // emitted normal points at the eye (front-facing from here).
      const float dx = eyeX - t.a.x, dy = eyeY - t.a.y, dz = eyeZ - t.a.z;
      const bool frontFacing = cx * dx + cy * dy + cz * dz > 0.0f;
      const bool pvsKept = cameraCluster < 0 || t.cluster < 0
        || tf2::native::BspParser::clusterVisible(map, cameraCluster, t.cluster);
      if (t.cluster < 0) {
        ++(frontFacing ? noClusterFront : noClusterBack);
      } else if (pvsKept) {
        ++(frontFacing ? keptFront : keptBack);
      } else {
        ++(frontFacing ? hiddenFront : hiddenBack);
      }
    }
    std::printf("geometry facing pvsKept front=%zu back=%zu pvsHidden front=%zu back=%zu"
                " noCluster front=%zu back=%zu\n",
                keptFront, keptBack, hiddenFront, hiddenBack, noClusterFront, noClusterBack);
    // T11 follow-up: is the face's own outward direction the side the eye is on?
    // For each emission class and each plane side, count PVS-kept triangles whose
    // centroid has the eye on its outward side (`outward` column) or on the other
    // side (`inward`). A face authored on the correct side of its plane reads
    // `outward` for the surfaces the eye can actually see; a class that reads
    // mostly `inward` is the class whose stored data disagrees with its own plane
    // and is the one the cull mode removes.
    {
      struct Row { std::size_t outward = 0, inward = 0; };
      Row rows[2][2]; // [displacement][planeSide]
      for (const auto& t : map.triangles) {
        if (t.planeIndex < 0 || static_cast<std::size_t>(t.planeIndex) >= map.planes.size()) continue;
        const bool pvsKept = cameraCluster < 0 || t.cluster < 0
          || tf2::native::BspParser::clusterVisible(map, cameraCluster, t.cluster);
        if (!pvsKept) continue;
        const auto& plane = map.planes[static_cast<std::size_t>(t.planeIndex)];
        const float sideSign = t.planeSide == 0 ? 1.0f : -1.0f;
        const float ox = sideSign * plane.normal[0], oy = sideSign * plane.normal[1], oz = sideSign * plane.normal[2];
        const BspVertex centroid = canonicalCentroid(t);
        const float cx = centroid.x - eyeX;
        const float cy = centroid.y - eyeY;
        const float cz = centroid.z - eyeZ;
        // The eye is on the outward side when the vector from the triangle to
        // the eye points along the outward normal.
        auto& row = rows[t.displacement ? 1 : 0][t.planeSide != 0 ? 1 : 0];
        if (ox * cx + oy * cy + oz * cz > 0.0f) ++row.outward; else ++row.inward;
      }
      for (int d = 0; d < 2; ++d) for (int s = 0; s < 2; ++s) {
        std::printf("geometry side-census class=%s planeSide=%d outward=%zu inward=%zu\n",
                    d ? "displacement" : "brush", s, rows[d][s].outward, rows[d][s].inward);
      }
    }
    // T11 follow-up: the side bit is ground truth only if the BSP tree agrees
    // with it. The tree is independent of the face lump: a point 2 units along
    // the face's OUTWARD direction must land in an open leaf (a cluster), and a
    // point 2 units the other way must land in the solid leaf (-1). This walks
    // the nodes for every PVS-kept triangle and counts which way round it is, so
    // "the side bit says front, the tree says solid" is a reading and not a
    // guess.
    {
      std::size_t outwardOpen = 0, outwardSolid = 0, inwardOpen = 0, inwardSolid = 0, ambiguous = 0;
      std::size_t bOutwardOpen = 0, bInwardOpen = 0, bAmbiguous = 0;
      std::size_t dOutwardOpen = 0, dInwardOpen = 0, dAmbiguous = 0;
      for (const auto& t : map.triangles) {
        if (t.planeIndex < 0 || static_cast<std::size_t>(t.planeIndex) >= map.planes.size()) continue;
        const bool pvsKept = cameraCluster < 0 || t.cluster < 0
          || tf2::native::BspParser::clusterVisible(map, cameraCluster, t.cluster);
        if (!pvsKept) continue;
        const auto& plane = map.planes[static_cast<std::size_t>(t.planeIndex)];
        const float sideSign = t.planeSide == 0 ? 1.0f : -1.0f;
        const float ox = sideSign * plane.normal[0], oy = sideSign * plane.normal[1], oz = sideSign * plane.normal[2];
        const BspVertex centroid = canonicalCentroid(t);
        const float px = centroid.x;
        const float py = centroid.y;
        const float pz = centroid.z;
        const int outCluster = tf2::native::BspParser::clusterAt(map, px + ox * 2.0f, py + oy * 2.0f, pz + oz * 2.0f);
        const int inCluster = tf2::native::BspParser::clusterAt(map, px - ox * 2.0f, py - oy * 2.0f, pz - oz * 2.0f);
        if (outCluster < 0 && inCluster < 0) ++ambiguous;
        else if (outCluster >= 0 && inCluster >= 0) ++ambiguous;
        else if (outCluster >= 0) ++outwardOpen;
        else ++inwardOpen;
        if (outCluster < 0 == inCluster < 0) {
          if (t.displacement) ++dAmbiguous; else ++bAmbiguous;
        } else if (outCluster >= 0) {
          if (t.displacement) ++dOutwardOpen; else ++bOutwardOpen;
        } else {
          if (t.displacement) ++dInwardOpen; else ++bInwardOpen;
        }
        if (outCluster < 0 && inCluster >= 0) ++outwardSolid;
        if (outCluster >= 0 && inCluster < 0) ++inwardSolid;
      }
      std::printf("geometry tree-side outwardOpen=%zu inwardOpen=%zu ambiguous=%zu\n",
                  outwardOpen, inwardOpen, ambiguous);
      std::printf("geometry tree-side brush outwardOpen=%zu inwardOpen=%zu ambiguous=%zu\n",
                  bOutwardOpen, bInwardOpen, bAmbiguous);
      std::printf("geometry tree-side displacement outwardOpen=%zu inwardOpen=%zu ambiguous=%zu\n",
                  dOutwardOpen, dInwardOpen, dAmbiguous);
    }
  }
  // T11 third round: the same oracle from inside the level. Every direction
  // from the eye has a first surface; in a closed level that surface is what
  // the eye sees, so its STORED winding must face the eye. This is the
  // view-independent half of the oracle: it does not care which camera the
  // acceptance captures use, and it cannot be satisfied by trading one view for
  // the other.
  if (rayEyeCells > 0) {
    const std::size_t dirs = static_cast<std::size_t>(rayEyeCells) * static_cast<std::size_t>(rayEyeCells) * 2u;
    std::size_t hits = 0, misses = 0;
    std::size_t brushToward = 0, brushAway = 0, dispToward = 0, dispAway = 0;
    std::size_t brushSide[2][2] = {{0, 0}, {0, 0}}; // [side][toward]
    std::size_t dispSide[2][2] = {{0, 0}, {0, 0}};
    std::size_t drawnShipped = 0;
    // T23: the same directions, asked the question the product actually asks.
    // The first surface along a direction can be rejected for two independent
    // reasons -- the pvs does not carry its cluster, or the emitted winding
    // faces away from the eye -- and the two are separable here because the
    // triangle carries both its cluster and its emission flag. The counts below
    // are therefore an account, not a verdict: drawn / vis-rejected /
    // back-rejected / both-rejected, plus how many of the rejected directions
    // still have SOME drawable surface behind them (the frame shows a farther
    // surface) versus none at all (the frame shows sky or clear colour).
    const int eyeCluster = tf2::native::BspParser::clusterAt(map, rayEyeX, rayEyeY, rayEyeZ);
    std::size_t classDrawn = 0, classVis = 0, classBack = 0, classBoth = 0;
    std::size_t brushDrawn = 0, brushRejected = 0, dispDrawn = 0, dispRejected = 0;
    std::size_t fallbackDrawn = 0, fallbackRejected = 0;
    std::size_t occludedWithBehind = 0, occludedWithNothing = 0;
    std::size_t drawnDark = 0, drawnToned = 0;
    // T23: a sphere of directions is not a frame. "No geometry" straight up is
    // the sky and is fine; "no geometry" downwards is a hole in the terrain.
    // The same split is applied to the directions whose first hit is rejected
    // and has no drawable surface behind it -- those are the directions in
    // which the frame would show sky where the eye expects a surface.
    std::size_t missDown = 0, missHoriz = 0, missUp = 0;
    std::size_t holeDown = 0, holeHoriz = 0, holeUp = 0;
    std::size_t behindBrush = 0, behindDisp = 0;
    std::size_t holesPrinted = 0;
    const bool haveAtlas = map.lightmapAtlasWidth > 0 && map.lightmapAtlasHeight > 0
      && !map.lightmapAtlas.empty();
    const auto toneOf = [&](const BspTriangle& t) {
      if (!haveAtlas || !t.lightmapped) return -1.0;
      const float uv[3][2] = {{t.alu, t.alv}, {t.blu, t.blv}, {t.clu, t.clv}};
      double sum = 0.0;
      for (const auto& corner : uv) {
        const int x = std::clamp(static_cast<int>(std::floor(corner[0] * static_cast<float>(map.lightmapAtlasWidth))),
                                 0, map.lightmapAtlasWidth - 1);
        const int y = std::clamp(static_cast<int>(std::floor(corner[1] * static_cast<float>(map.lightmapAtlasHeight))),
                                 0, map.lightmapAtlasHeight - 1);
        const std::size_t at = (static_cast<std::size_t>(y) * static_cast<std::size_t>(map.lightmapAtlasWidth)
          + static_cast<std::size_t>(x)) * 4u;
        sum += (static_cast<double>(map.lightmapAtlas[at])
          + map.lightmapAtlas[at + 1] + map.lightmapAtlas[at + 2]) / 3.0;
      }
      return sum / 3.0;
    };
    constexpr float kPi = 3.14159265358979323846f;
    for (int polar = 0; polar < rayEyeCells; ++polar) {
      const float theta = kPi * (static_cast<float>(polar) + 0.5f) / static_cast<float>(rayEyeCells);
      for (int azimuth = 0; azimuth < rayEyeCells * 2; ++azimuth) {
        const float phi = 2.0f * kPi * (static_cast<float>(azimuth) + 0.5f) / static_cast<float>(rayEyeCells * 2);
        const float dx = std::sin(theta) * std::cos(phi);
        const float dy = std::sin(theta) * std::sin(phi);
        const float dz = std::cos(theta);
        float bestT = 1.0e30f;
        std::size_t bestIndex = map.triangles.size();
        // T23: nearest surface the product would actually draw along this ray.
        // "Drawable" means exactly what the pass means: the triangle's cluster
        // survives the camera's pvs, and its emitted winding faces the eye.
        float bestDrawableT = 1.0e30f;
        std::size_t bestDrawableIndex = map.triangles.size();
        for (std::size_t index = 0; index < map.triangles.size(); ++index) {
          const auto& t = map.triangles[index];
          const float e1x = t.b.x - t.a.x, e1y = t.b.y - t.a.y, e1z = t.b.z - t.a.z;
          const float e2x = t.c.x - t.a.x, e2y = t.c.y - t.a.y, e2z = t.c.z - t.a.z;
          const float px = dy * e2z - dz * e2y, py = dz * e2x - dx * e2z, pz = dx * e2y - dy * e2x;
          const float det = e1x * px + e1y * py + e1z * pz;
          if (std::fabs(det) < 1.0e-9f) continue;
          const float inv = 1.0f / det;
          const float tx = rayEyeX - t.a.x, ty = rayEyeY - t.a.y, tz = rayEyeZ - t.a.z;
          const float u = (tx * px + ty * py + tz * pz) * inv;
          if (u < 0.0f || u > 1.0f) continue;
          const float qx = ty * e1z - tz * e1y, qy = tz * e1x - tx * e1z, qz = tx * e1y - ty * e1x;
          const float v = (dx * qx + dy * qy + dz * qz) * inv;
          if (v < 0.0f || u + v > 1.0f) continue;
          const float hit = (e2x * qx + e2y * qy + e2z * qz) * inv;
          if (hit > 1.0e-3f && hit < bestT) { bestT = hit; bestIndex = index; }
          if (hit > 1.0e-3f && hit < bestDrawableT) {
            const float ux = t.b.x - t.a.x, uy = t.b.y - t.a.y, uz = t.b.z - t.a.z;
            const float vx = t.c.x - t.a.x, vy = t.c.y - t.a.y, vz = t.c.z - t.a.z;
            const float nx2 = uy * vz - uz * vy, ny2 = uz * vx - ux * vz, nz2 = ux * vy - uy * vx;
            if (orientedFacing(t, nx2, ny2, nz2, dx, dy, dz)) {
              const bool pvs = eyeCluster < 0 || t.cluster < 0
                || tf2::native::BspParser::clusterVisible(map, eyeCluster, t.cluster);
              if (pvs) { bestDrawableT = hit; bestDrawableIndex = index; }
            }
          }
        }
        if (bestIndex >= map.triangles.size()) {
          ++misses;
          if (dz < -0.3f) ++missDown; else if (dz > 0.3f) ++missUp; else ++missHoriz;
          continue;
        }
        ++hits;
        const auto& t = map.triangles[bestIndex];
        const float ux = t.b.x - t.a.x, uy = t.b.y - t.a.y, uz = t.b.z - t.a.z;
        const float vx = t.c.x - t.a.x, vy = t.c.y - t.a.y, vz = t.c.z - t.a.z;
        float nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
        // The emitted winding, as the rasteriser sees it.
        const float enx = nx, eny = ny, enz = nz;
        const bool drawn = orientedFacing(t, nx, ny, nz, dx, dy, dz);
        // Stored winding, recovered from the emission flag.
        if (t.windingFlipped != 0) { nx = -nx; ny = -ny; nz = -nz; }
        // The eye sees the side the stored normal points at.
        const bool toward = nx * dx + ny * dy + nz * dz > 0.0f;
        if (t.displacement) {
          ++(toward ? dispToward : dispAway);
          ++dispSide[t.planeSide != 0 ? 1 : 0][toward ? 1 : 0];
          ++eyeTotal[1][t.planeSide != 0 ? 1 : 0];
          if (toward) ++eyeFacing[1][t.planeSide != 0 ? 1 : 0];
        } else {
          ++(toward ? brushToward : brushAway);
          ++brushSide[t.planeSide != 0 ? 1 : 0][toward ? 1 : 0];
          ++eyeTotal[0][t.planeSide != 0 ? 1 : 0];
          if (toward) ++eyeFacing[0][t.planeSide != 0 ? 1 : 0];
        }
        // What the shipped rule would leave in front of the eye: it reverses the
        // stored order for every triangle it can orient, so "toward" flips.
        if (drawn) ++drawnShipped;
        const bool pvsKept = eyeCluster < 0 || t.cluster < 0
          || tf2::native::BspParser::clusterVisible(map, eyeCluster, t.cluster);
        if (drawn && pvsKept) {
          ++classDrawn;
          ++(t.displacement ? dispDrawn : brushDrawn);
          if (t.fallbackQuad) ++fallbackDrawn;
          const double tone = toneOf(t);
          if (tone >= 0.0) {
            ++drawnToned;
            if (tone < 24.0) ++drawnDark;
          }
        } else {
          ++(t.displacement ? dispRejected : brushRejected);
          if (t.fallbackQuad) ++fallbackRejected;
          if (drawn) ++classVis;        // the pvs hides a surface that faces the eye
          else if (pvsKept) ++classBack; // the winding turns a visible surface away
          else ++classBoth;
        }
        if (!(drawn && pvsKept)) {
          if (bestDrawableIndex < map.triangles.size()) {
            ++occludedWithBehind;
            ++(t.displacement ? behindDisp : behindBrush);
          } else {
            ++occludedWithNothing;
            if (dz < -0.3f) ++holeDown; else if (dz > 0.3f) ++holeUp; else ++holeHoriz;
            // The directions that matter: geometry exists, the product draws
            // none of it, and there is no farther drawable surface either.
            // Named with the surface that was rejected, so the defect can be
            // followed to a face instead of argued about.
            if (holesPrinted < 80) {
              float ox = 0.0f, oy = 0.0f, oz = 0.0f;
              if (t.planeIndex >= 0
                  && static_cast<std::size_t>(t.planeIndex) < map.planes.size()) {
                const auto& plane = map.planes[static_cast<std::size_t>(t.planeIndex)];
                const float sideSign = t.planeSide == 0 ? 1.0f : -1.0f;
                ox = sideSign * plane.normal[0];
                oy = sideSign * plane.normal[1];
                oz = sideSign * plane.normal[2];
              }
              std::printf("geometry ray-eye hole dir=(%.3f,%.3f,%.3f) t=%.1f hit=(%.1f,%.1f,%.1f) class=%s reason=%s material=%s cluster=%d fallback=%d outward=(%.2f,%.2f,%.2f) outDotDir=%.3f n=(%.2f,%.2f,%.2f) nDotDir=%.3f\n",
                          dx, dy, dz, bestT,
                          rayEyeX + dx * bestT, rayEyeY + dy * bestT, rayEyeZ + dz * bestT,
                          t.displacement ? "displacement" : "brush",
                          (drawn && !pvsKept) ? "vis" : (!drawn && pvsKept) ? "back" : "both",
                          t.material.c_str(), t.cluster, t.fallbackQuad ? 1 : 0,
                          ox, oy, oz, ox * dx + oy * dy + oz * dz,
                          enx, eny, enz, enx * dx + eny * dy + enz * dz);
              ++holesPrinted;
            }
          }
        }
      }
    }
    const std::size_t total = hits == 0 ? 1 : hits;
    std::printf("geometry ray-eye dirs=%zu hits=%zu misses=%zu shippedRuleKeeps=%zu of %zu (%.3f)\n",
                dirs, hits, misses, drawnShipped, total,
                static_cast<double>(drawnShipped) / static_cast<double>(total));
    std::printf("geometry ray-eye first-hit class=brush storedTowardEye=%zu storedAway=%zu\n", brushToward, brushAway);
    std::printf("geometry ray-eye first-hit class=displacement storedTowardEye=%zu storedAway=%zu\n", dispToward, dispAway);
    std::printf("geometry ray-eye classification drawn=%zu visRejected=%zu backRejected=%zu bothRejected=%zu noGeometry=%zu eyeCluster=%d\n",
                classDrawn, classVis, classBack, classBoth, misses, eyeCluster);
    std::printf("geometry ray-eye classification class=brush drawn=%zu rejected=%zu\n", brushDrawn, brushRejected);
    std::printf("geometry ray-eye classification class=displacement drawn=%zu rejected=%zu\n", dispDrawn, dispRejected);
    std::printf("geometry ray-eye classification fallbackQuad drawn=%zu rejected=%zu\n", fallbackDrawn, fallbackRejected);
    std::printf("geometry ray-eye occlusion firstHitRejected=%zu drawableBehind=%zu nothingDrawable=%zu\n",
                classVis + classBack + classBoth, occludedWithBehind, occludedWithNothing);
    std::printf("geometry ray-eye occlusion drawableBehind class=brush %zu displacement %zu\n",
                behindBrush, behindDisp);
    std::printf("geometry ray-eye noGeometry down=%zu horizontal=%zu up=%zu\n",
                missDown, missHoriz, missUp);
    std::printf("geometry ray-eye holes down=%zu horizontal=%zu up=%zu\n",
                holeDown, holeHoriz, holeUp);
    std::printf("geometry ray-eye drawn-tone dark=%zu of %zu (%.3f) atlas=%dx%d\n",
                drawnDark, drawnToned,
                drawnToned == 0 ? 0.0 : static_cast<double>(drawnDark) / static_cast<double>(drawnToned),
                map.lightmapAtlasWidth, map.lightmapAtlasHeight);
    for (int s = 0; s < 2; ++s) {
      std::printf("geometry ray-eye first-hit brush planeSide=%d towardEye=%zu away=%zu\n",
                  s, brushSide[s][1], brushSide[s][0]);
      std::printf("geometry ray-eye first-hit displacement planeSide=%d towardEye=%zu away=%zu\n",
                  s, dispSide[s][1], dispSide[s][0]);
    }
  }
  // T11 third round: the decision table. A rule is a keep/flip choice for each
  // (class, side) cell. Both first-hit oracles reduce to the same four cells, so
  // all sixteen rules can be scored on both views without a rebuild, a capture or
  // a guess. "keep" means the stored order already faces the observer.
  if (rayCells > 0 && rayEyeCells > 0) {
    std::size_t topTotalAll = 0, eyeTotalAll = 0;
    for (int c = 0; c < 2; ++c) for (int s = 0; s < 2; ++s) {
      topTotalAll += topTotal[c][s];
      eyeTotalAll += eyeTotal[c][s];
    }
    std::printf("geometry rule-table cells=%zu (top) %zu (eye) -- keep means the stored order already faces the observer\n",
                topTotalAll, eyeTotalAll);
    for (int mask = 0; mask < 16; ++mask) {
      std::size_t topHit = 0, eyeHit = 0;
      for (int c = 0; c < 2; ++c) for (int s = 0; s < 2; ++s) {
        const int bit = c * 2 + s;
        const bool keep = ((mask >> bit) & 1) != 0;
        topHit += keep ? topFacing[c][s] : (topTotal[c][s] - topFacing[c][s]);
        eyeHit += keep ? eyeFacing[c][s] : (eyeTotal[c][s] - eyeFacing[c][s]);
      }
      const double topScore = topTotalAll == 0 ? 0.0 : static_cast<double>(topHit) / static_cast<double>(topTotalAll);
      const double eyeScore = eyeTotalAll == 0 ? 0.0 : static_cast<double>(eyeHit) / static_cast<double>(eyeTotalAll);
      std::printf("geometry rule-table mask=%02d brushSide0=%s brushSide1=%s dispSide0=%s dispSide1=%s"
                  " top=%.3f eye=%.3f worst=%.3f\n",
                  mask,
                  ((mask >> 0) & 1) ? "keep" : "flip", ((mask >> 1) & 1) ? "keep" : "flip",
                  ((mask >> 2) & 1) ? "keep" : "flip", ((mask >> 3) & 1) ? "keep" : "flip",
                  topScore, eyeScore, std::min(topScore, eyeScore));
    }
  }
  if (virtualFallbackOrient) {
    std::printf("geometry fallback-orient evaluated=%zu flipped=%zu\n",
                virtualFlipEvaluated, virtualFlipApplied);
  }
  return 0;
}
