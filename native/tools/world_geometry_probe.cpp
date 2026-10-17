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
#include <cstring>
#include <string>
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

void dumpVertex(const char* name, const BspVertex& v) {
  std::printf("      %s=(%.1f, %.1f, %.1f)", name, v.x, v.y, v.z);
}

} // namespace

int main(int argc, char** argv) {
  std::string tfRoot;
  std::string mapPath;
  std::size_t triangleLimit = 4000000;
  bool haveEye = false;
  float eyeX = 0.0f, eyeY = 0.0f, eyeZ = 0.0f;
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--tf-root") == 0 && i + 1 < argc) tfRoot = argv[++i];
    else if (std::strcmp(argv[i], "--map") == 0 && i + 1 < argc) mapPath = argv[++i];
    else if (std::strcmp(argv[i], "--triangle-limit") == 0 && i + 1 < argc) triangleLimit = static_cast<std::size_t>(std::stoull(argv[++i]));
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
  if (!tf2::native::BspParser::parse(bytes, map, triangleLimit, 2048)) {
    std::printf("geometry status=parse-failed map=%s triangles=%zu reason=the bsp parser rejected the file\n",
                mapPath.c_str(), map.triangles.size());
    return 1;
  }

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
  std::printf("geometry edge-groups staticMaxEdge=%.1f staticOver1024=%zu staticMaterialOver1024=%zu displacedMaxEdge=%.1f displacedOver1024=%zu\n",
              staticMaxEdge, staticOver1024, staticMaterialOver1024, displacedMaxEdge, displacedOver1024);
  std::printf("geometry displacement faces=%zu triangles=%zu cornerMatches=%zu\n",
              map.displacementFaces, map.displacementTriangles, map.displacementCornerMatches);
  std::printf("geometry winding agree=%zu of %zu faces (%.1f%% agree with the bsp plane normals)\n",
              map.windingAgreeCount, map.windingFaceCount,
              map.windingFaceCount == 0 ? 0.0 : 100.0 * static_cast<double>(map.windingAgreeCount)
                / static_cast<double>(map.windingFaceCount));
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
  }
  return 0;
}
