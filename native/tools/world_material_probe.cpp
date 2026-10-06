#include "bsp_map.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

std::vector<std::uint8_t> readFile(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
}

bool lightUvInRange(const tf2::native::BspTriangle& triangle) {
  const float values[] = {triangle.alu, triangle.alv, triangle.blu, triangle.blv, triangle.clu, triangle.clv};
  for (const float value : values) {
    if (!std::isfinite(value) || value < 0.0f || value > 1.0f) return false;
  }
  return true;
}

const char* waterSurfaceName(const tf2::native::BspMap& map) {
  return map.waterFaces > 0 ? "approximate-warp" : "absent";
}

bool expectSky(const tf2::native::BspMap& map, std::string& error) {
  if (map.skyName.empty()) return true;
  static const char* suffixes[] = {"rt", "lf", "bk", "ft", "up", "dn"};
  for (int face = 0; face < 6; ++face) {
    std::string path;
    if (!tf2::native::BspParser::skyMaterialPath(map.skyName, face, path)) {
      error = "sky path rejected";
      return false;
    }
    const std::string expected = "materials/skybox/" + map.skyName + suffixes[face] + ".vmt";
    if (path != expected) {
      error = "sky path " + path;
      return false;
    }
  }
  std::string rejected;
  if (tf2::native::BspParser::skyMaterialPath(map.skyName, 6, rejected)) {
    error = "sky face 6 accepted";
    return false;
  }
  return true;
}

bool checkMap(const tf2::native::BspMap& map, const std::filesystem::path& path, std::string& error) {
  if (!map.valid) { error = map.error.empty() ? "parse failed" : map.error; return false; }
  if (map.lightmapMode == tf2::native::BspLightmapMode::RgbExp32) {
    if (map.lightmapPackedFaces == 0 || map.lightmapTriangleCount == 0) { error = "rgbexp32 without packed faces"; return false; }
    if (map.lightmapAtlasWidth != 2048 || map.lightmapAtlasHeight <= 0) { error = "atlas size"; return false; }
    const auto bytes = static_cast<std::size_t>(map.lightmapAtlasWidth) * static_cast<std::size_t>(map.lightmapAtlasHeight) * 4u;
    if (map.lightmapAtlas.size() != bytes) { error = "atlas bytes"; return false; }
    if (map.lightmapLuxelFits == 0) { error = "no luxel fits"; return false; }
  } else if (map.lightmapMode == tf2::native::BspLightmapMode::AverageIntensity) {
    if (map.lightmapSampleCount == 0 || !map.lightmapAtlas.empty()) { error = "average-intensity fallback shape"; return false; }
  } else if (map.lightmapSampleCount != 0 || map.lightmapIntensity != 1.0f) {
    error = "unavailable lighting is not the empty default";
    return false;
  }
  std::size_t lightmapped = 0;
  for (const auto& triangle : map.triangles) {
    if (!triangle.lightmapped) {
      if (triangle.alu != -1.0f || triangle.alv != -1.0f) { error = "unmapped light uv"; return false; }
      continue;
    }
    if (!lightUvInRange(triangle)) { error = "light uv outside 0..1"; return false; }
    ++lightmapped;
  }
  if (lightmapped != map.lightmapTriangleCount) { error = "lightmapped triangle count"; return false; }
  if (map.displacementCornerMatches != map.displacementFaces) { error = "displacement corner mismatch"; return false; }
  if (map.waterReflection || map.waterRefraction) { error = "water capability claimed"; return false; }
  if (!expectSky(map, error)) return false;
  if (map.visMode == tf2::native::BspVisMode::ClusterPvs) {
    if (map.visClusterCount <= 1 || map.visOffsets.size() != static_cast<std::size_t>(map.visClusterCount)) {
      error = "vis header";
      return false;
    }
    const int visible = tf2::native::BspParser::countVisibleClusters(map, 0);
    if (visible <= 0 || visible >= map.visClusterCount) { error = "vis did not hide any cluster"; return false; }
    if (tf2::native::BspParser::clusterVisible(map, -1, 0)) { error = "invalid cluster treated as visible"; return false; }
  }
  const std::string stem = path.stem().string();
  if (stem == "itemtest") {
    if (map.skyName != "sky_day01_01" || map.cubemapCount != 4 || map.displacementFaces != 20
        || map.displacementCornerMatches != 20 || map.visClusterCount != 70
        || map.lightmapFaceCount != 513 || map.lightmapLuxelFits != 513 || map.waterFaces != 0
        || map.lightmapMode != tf2::native::BspLightmapMode::RgbExp32) {
      error = "itemtest contract";
      return false;
    }
  } else if (stem == "cp_cloak") {
    if (map.skyName != "sky_well_01" || map.cubemapCount != 3 || map.displacementFaces != 0
        || map.visClusterCount != 381 || map.lightmapFaceCount != 898 || map.lightmapLuxelFits != 898
        || map.waterFaces != 0 || map.lightmapMode != tf2::native::BspLightmapMode::RgbExp32) {
      error = "cp_cloak contract";
      return false;
    }
  }
  return true;
}

void printMap(const std::filesystem::path& path, const tf2::native::BspMap& map) {
  const char* tone = map.lightmapMode == tf2::native::BspLightmapMode::RgbExp32 ? "ldr-clamp" : "none";
  const int visible = map.visMode == tf2::native::BspVisMode::ClusterPvs
    ? tf2::native::BspParser::countVisibleClusters(map, 0) : -1;
  std::cout << "{\"bsp\":\"" << path.filename().string()
    << "\",\"valid\":" << (map.valid ? "true" : "false")
    << ",\"error\":\"" << map.error
    << "\",\"triangles\":" << map.triangleCount
    << ",\"lightmapMode\":\"" << tf2::native::BspParser::lightmapModeName(map.lightmapMode)
    << "\",\"lightmapContract\":\"luxel-index\",\"lightmapToneMap\":\"" << tone
    << "\",\"lightmapSamples\":" << map.lightmapSampleCount
    << ",\"lightmapFaces\":" << map.lightmapFaceCount
    << ",\"lightmapPackedFaces\":" << map.lightmapPackedFaces
    << ",\"lightmapLuxelFits\":" << map.lightmapLuxelFits
    << ",\"lightmapTriangles\":" << map.lightmapTriangleCount
    << ",\"atlas\":\"" << map.lightmapAtlasWidth << "x" << map.lightmapAtlasHeight
    << "\",\"sky\":\"" << map.skyName
    << "\",\"skyFaces\":" << map.skyFaces
    << ",\"displacements\":" << map.displacementFaces
    << ",\"displacementTriangles\":" << map.displacementTriangles
    << ",\"displacementCornerMatches\":" << map.displacementCornerMatches
    << ",\"cubemaps\":" << map.cubemapCount
    << ",\"cubemapMode\":\"approximate-2d\""
    << ",\"waterFaces\":" << map.waterFaces
    << ",\"waterSurface\":\"" << waterSurfaceName(map)
    << "\",\"waterReflection\":\"unavailable\",\"waterRefraction\":\"unavailable\""
    << ",\"visMode\":\"" << (map.visMode == tf2::native::BspVisMode::ClusterPvs ? "cluster-pvs" : "unavailable")
    << "\",\"visClusters\":" << map.visClusterCount
    << ",\"visibleFromCluster0\":" << visible
    << "}\n";
}

bool selfTest() {
  tf2::native::BspMap empty;
  const bool rejected = !tf2::native::BspParser::parse({}, empty);
  const bool noLightingDefault = empty.lightmapSampleCount == 0 && empty.lightmapIntensity == 1.0f
    && empty.lightmapMode == tf2::native::BspLightmapMode::Unavailable;
  const bool names = std::string(tf2::native::BspParser::lightmapModeName(tf2::native::BspLightmapMode::RgbExp32)) == "source-equivalent"
    && std::string(tf2::native::BspParser::lightmapModeName(tf2::native::BspLightmapMode::AverageIntensity)) == "average-intensity"
    && std::string(tf2::native::BspParser::lightmapModeName(tf2::native::BspLightmapMode::Unavailable)) == "unavailable";
  std::string sky;
  const bool skyPath = tf2::native::BspParser::skyMaterialPath("sky_day01_01", 0, sky) && sky == "materials/skybox/sky_day01_01rt.vmt"
    && tf2::native::BspParser::skyMaterialPath("sky_day01_01", 5, sky) && sky == "materials/skybox/sky_day01_01dn.vmt"
    && !tf2::native::BspParser::skyMaterialPath("", 0, sky)
    && !tf2::native::BspParser::skyMaterialPath("sky_day01_01", -1, sky);
  const bool visOpen = tf2::native::BspParser::clusterVisible(empty, 0, 1)
    && tf2::native::BspParser::clusterAt(empty, 0.0f, 0.0f, 0.0f) == -1
    && tf2::native::BspParser::countVisibleClusters(empty, 0) == 0;
  const bool ok = rejected && noLightingDefault && names && skyPath && visOpen;
  std::cout << "{\"selfTest\":" << (ok ? "true" : "false")
    << ",\"emptyBspRejected\":" << (rejected ? "true" : "false")
    << ",\"noLightingDefault\":" << (noLightingDefault ? "true" : "false")
    << ",\"lightmapNames\":" << (names ? "true" : "false")
    << ",\"skyPath\":" << (skyPath ? "true" : "false")
    << ",\"visUnavailableOpen\":" << (visOpen ? "true" : "false")
    << ",\"cubemapMode\":\"approximate-2d\",\"waterReflection\":\"unavailable\",\"waterRefraction\":\"unavailable\"}\n";
  return ok;
}

int main(int argc, char** argv) {
  bool runSelf = argc <= 1;
  bool ok = true;
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg = argv[i];
    if (arg == "--self-test") { runSelf = true; continue; }
    if (arg == "--bsp" && i + 1 < argc) {
      const std::filesystem::path path = std::filesystem::u8path(argv[++i]);
      const auto bytes = readFile(path);
      tf2::native::BspMap map;
      tf2::native::BspParser::parse(bytes, map, 200000);
      printMap(path, map);
      std::string error;
      if (!checkMap(map, path, error)) {
        std::cerr << path.filename().string() << " failed: " << error << "\n";
        ok = false;
      }
      continue;
    }
    std::cerr << "usage: world_material_probe --self-test [--bsp map.bsp]\n";
    return 2;
  }
  if (runSelf && !selfTest()) ok = false;
  return ok ? 0 : 1;
}
