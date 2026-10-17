#include "bsp_map.h"
#include "vmt_material.h"
#include "vpk_archive.h"
#include "vtf_texture.h"
#include "world_material.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

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
    // The atlas width is chosen now (T1, 2026-10-13): the historical 2048 for
    // every map whose lightmaps fit it, 4096 for the ones that do not. Pinning
    // the single value 2048 would go red on exactly the maps this change
    // exists for, so what is asserted is the contract: one of the two widths,
    // a non-empty height that is not taller than the atlas is wide, and a
    // byte count that matches.
    const int atlasWidth = map.lightmapAtlasWidth;
    if ((atlasWidth != 2048 && atlasWidth != 4096) || map.lightmapAtlasHeight <= 0
        || map.lightmapAtlasHeight > atlasWidth) { error = "atlas size"; return false; }
    const auto bytes = static_cast<std::size_t>(map.lightmapAtlasWidth) * static_cast<std::size_t>(map.lightmapAtlasHeight) * 4u;
    if (map.lightmapAtlas.size() != bytes) { error = "atlas bytes"; return false; }
    if (map.lightmapLuxelFits == 0) { error = "no luxel fits"; return false; }
    // And the drop is reported either way: zero faces lost with a reason that
    // says so, or a named count. A reason that is empty is a map whose atlas
    // outcome nobody recorded, which is the state this field was added to end.
    if (map.lightmapAtlasReason.empty()) { error = "atlas reason is empty"; return false; }
    if ((map.lightmapAtlasDroppedFaces == 0) != (map.lightmapAtlasReason.find("did not fit") == std::string::npos)) {
      error = "atlas reason disagrees with the drop count";
      return false;
    }
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

// ---------------------------------------------------------------------------
// The water material, resolved the way main.cpp resolves it
// ---------------------------------------------------------------------------
// main.cpp reads the first triangle's material, parses its VMT and takes
// `waterShader` from it. That is the product's own rule, and a probe that
// reported "no water material declared" while the product had found one would
// be stating something false about the install. So the probe walks to a *water*
// triangle instead of the first one -- same parser, same resource order, one
// deliberate difference, recorded here because it is the only one.

struct WaterMaterialResolution {
  bool declared = false;
  bool textureReachable = false;
  std::string material;
  std::string vmtPath;
  std::string reason;
};

WaterMaterialResolution resolveWaterMaterial(const tf2::native::BspMap& map,
    const std::filesystem::path& tfRoot, const tf2::native::VpkArchiveSet& archives) {
  WaterMaterialResolution resolution;
  const auto readBytes = [&](const std::string& path) {
    std::error_code error;
    if (std::filesystem::exists(tfRoot / std::filesystem::u8path(path), error)) {
      return readFile(tfRoot / std::filesystem::u8path(path));
    }
    std::vector<std::filesystem::path> holders;
    archives.collectContaining(path, holders);
    for (const auto& holder : holders) {
      const auto* archive = archives.find(holder);
      if (!archive) continue;
      const auto bytes = archive->read(path);
      if (!bytes.empty()) return bytes;
    }
    return std::vector<std::uint8_t>{};
  };
  for (const auto& triangle : map.triangles) {
    if (!triangle.water || triangle.material.empty()) continue;
    resolution.material = triangle.material;
    break;
  }
  if (resolution.material.empty()) {
    resolution.reason = "no water triangle carries a material name";
    return resolution;
  }
  resolution.vmtPath = tf2::native::VmtParser::resourcePath(resolution.material, ".vmt");
  const auto vmtBytes = readBytes(resolution.vmtPath);
  if (vmtBytes.empty()) {
    resolution.reason = "water vmt unreadable: " + resolution.vmtPath;
    return resolution;
  }
  tf2::native::VmtMaterial material;
  if (!tf2::native::VmtParser::parse(std::string(vmtBytes.begin(), vmtBytes.end()), material)) {
    resolution.reason = "water vmt did not parse: " + resolution.vmtPath;
    return resolution;
  }
  resolution.declared = material.waterShader;
  if (!resolution.declared) {
    resolution.reason = "water vmt declares no water shader: " + resolution.vmtPath;
    return resolution;
  }
  // A Water shader has no $basetexture by design: its surface is the normal map
  // it refracts through. Checking $basetexture first and reporting the water as
  // untextured would be a reading about the wrong key.
  const char* keys[] = {"$basetexture", "$normalmap", "$bumpmap"};
  std::string textureName;
  std::string textureKey;
  for (const char* key : keys) {
    const auto at = material.fields.find(key);
    if (at != material.fields.end() && !at->second.empty()) {
      textureName = at->second;
      textureKey = key;
      break;
    }
  }
  if (textureName.empty()) {
    resolution.reason = "water vmt declares no surface texture: " + resolution.vmtPath;
    return resolution;
  }
  const auto texturePath = tf2::native::VmtParser::resourcePath(textureName, ".vtf");
  resolution.textureReachable = !readBytes(texturePath).empty();
  resolution.reason = resolution.textureReachable
    ? "water surface texture resolves via " + textureKey + ": " + texturePath
    : "water surface texture unreadable via " + textureKey + ": " + texturePath;
  return resolution;
}

// ---------------------------------------------------------------------------
// Resource reachability against the real install
// ---------------------------------------------------------------------------

class TfResourceProbe : public tf2::native::WorldResourceProbe {
public:
  TfResourceProbe(std::filesystem::path tfRoot, tf2::native::VpkArchiveSet archives)
    : tfRoot_(std::move(tfRoot)), archives_(std::move(archives)) {}

  bool reachable(const std::string& resourcePath) const override {
    std::error_code error;
    if (std::filesystem::exists(tfRoot_ / std::filesystem::u8path(resourcePath), error)) return true;
    std::vector<std::filesystem::path> holders;
    archives_.collectContaining(resourcePath, holders);
    return !holders.empty();
  }

  // The bytes, from the same two stores and in the same order reachable() looks
  // in. Without this the sky reading could say six vmts resolve and zero of
  // their textures decode -- a claim about the install made by a probe that
  // never opened a file.
  std::vector<std::uint8_t> read(const std::string& resourcePath) const override {
    std::error_code error;
    const auto loose = tfRoot_ / std::filesystem::u8path(resourcePath);
    if (std::filesystem::exists(loose, error)) return readFile(loose);
    std::vector<std::filesystem::path> holders;
    archives_.collectContaining(resourcePath, holders);
    for (const auto& holder : holders) {
      const auto* archive = archives_.find(holder);
      if (!archive) continue;
      auto bytes = archive->read(resourcePath);
      if (!bytes.empty()) return bytes;
    }
    return {};
  }

private:
  std::filesystem::path tfRoot_;
  tf2::native::VpkArchiveSet archives_;
};

// ---------------------------------------------------------------------------
// Assertions
// ---------------------------------------------------------------------------
// A check carries its own verdict so the runner can print a single line per
// failure instead of re-deriving what went wrong from a pair of strings.

struct Check {
  std::string key;
  bool ok = false;
  std::string detail;
};

Check checkStr(const std::string& key, const std::string& actual, const std::string& expected) {
  return Check{key, actual == expected, "actual=" + actual + " expected=" + expected};
}
Check checkNum(const std::string& key, std::size_t actual, std::size_t expected) {
  return checkStr(key, std::to_string(actual), std::to_string(expected));
}
Check checkNum(const std::string& key, int actual, int expected) {
  return checkStr(key, std::to_string(actual), std::to_string(expected));
}
Check checkBool(const std::string& key, bool actual, bool expected) {
  return checkStr(key, actual ? "true" : "false", expected ? "true" : "false");
}
Check checkNear(const std::string& key, double actual, double expected, double tolerance) {
  const bool ok = std::fabs(actual - expected) <= tolerance;
  std::ostringstream detail;
  detail.precision(8);
  detail << "actual=" << actual << " expected=" << expected;
  return Check{key, ok, detail.str()};
}

// The parser's own light-uv derivation, copied so the expectation is derived
// from the fixture's parameters instead of hard-coded from a reading someone
// once observed. If the parser changes, this changes with it and the fixture
// fails loudly rather than quietly agreeing with the new behaviour.
double expectedLightUv(double luxel, int stored, int samples, int mins, int origin, int atlasSize) {
  double coord = stored > 0 ? (luxel - static_cast<double>(mins)) / static_cast<double>(stored) : 0.5;
  if (coord < 0.0) coord = 0.0;
  else if (coord > 1.0) coord = 1.0;
  const double texel = samples <= 1 ? 0.5 : 0.5 + coord * static_cast<double>(samples - 1);
  return (static_cast<double>(origin) + texel) / static_cast<double>(atlasSize);
}

// One RGBExp32 luxel decoded the way bsp_map.cpp's writeLuxel decodes it, so the
// expected atlas pixel is derived rather than copied out of a dump.
std::array<std::uint8_t, 4> expectedAtlasPixel(std::uint8_t r, std::uint8_t g, std::uint8_t b,
    std::int8_t exponent) {
  const double scale = std::ldexp(1.0, static_cast<int>(exponent)) / 255.0;
  auto channel = [&](std::uint8_t sample) {
    const double decoded = static_cast<double>(sample) * scale;
    if (decoded >= 1.0) return static_cast<std::uint8_t>(255);
    if (decoded <= 0.0) return static_cast<std::uint8_t>(0);
    return static_cast<std::uint8_t>(decoded * 255.0 + 0.5);
  };
  return {channel(r), channel(g), channel(b), static_cast<std::uint8_t>(255)};
}

std::string atlasPixelText(const std::array<std::uint8_t, 4>& pixel) {
  return std::to_string(pixel[0]) + "," + std::to_string(pixel[1]) + ","
    + std::to_string(pixel[2]) + "," + std::to_string(pixel[3]);
}

std::string atlasPixelAt(const tf2::native::BspMap& map, int x, int y) {
  if (map.lightmapAtlas.empty()) return "no-atlas";
  const auto at = (static_cast<std::size_t>(y) * static_cast<std::size_t>(map.lightmapAtlasWidth)
    + static_cast<std::size_t>(x)) * 4u;
  if (at + 4u > map.lightmapAtlas.size()) return "out-of-range";
  return std::to_string(map.lightmapAtlas[at]) + "," + std::to_string(map.lightmapAtlas[at + 1])
    + "," + std::to_string(map.lightmapAtlas[at + 2]) + ","
    + std::to_string(map.lightmapAtlas[at + 3]);
}

// ---------------------------------------------------------------------------
// A synthetic cubemap VTF
// ---------------------------------------------------------------------------
// Built to the layout this install's real cubemaps use, not to a convenient
// one. The format's image section is
//
//   for each mip (smallest to largest)
//     for each face (first to last)
//
// so the top mips of every face sit together at the end of the section; each
// face's smaller mips hold the inverse of its colour, so a decoder that reads
// the section at the wrong offset returns a colour the fixture never wrote
// rather than a plausible-looking picture. The header mirrors the real files:
// minor 4, a 96-byte header with one resource (tag 0x30) pointing at image data
// that starts right after it, and seven face slots where the header's own rule
// declares six.
std::vector<std::uint8_t> syntheticCubemapVtf(std::uint32_t width, std::uint32_t height,
    const std::array<std::array<std::uint8_t, 3>, 6>& faceColors, int faceSlots = 7) {
  const std::uint32_t mipCount = 2;
  const auto mipBytes = [&](std::uint32_t level) {
    const std::uint32_t w = std::max(1u, width >> level);
    const std::uint32_t h = std::max(1u, height >> level);
    return static_cast<std::size_t>(w) * h * 4u;
  };
  std::vector<std::uint8_t> file(96, 0);
  const auto put16At = [&](std::size_t at, std::uint16_t value) {
    file[at] = static_cast<std::uint8_t>(value & 0xFFu);
    file[at + 1] = static_cast<std::uint8_t>((value >> 8) & 0xFFu);
  };
  const auto put32At = [&](std::size_t at, std::uint32_t value) {
    for (int byte = 0; byte < 4; ++byte) {
      file[at + static_cast<std::size_t>(byte)] = static_cast<std::uint8_t>((value >> (byte * 8)) & 0xFFu);
    }
  };
  std::memcpy(file.data(), "VTF\0", 4);
  put32At(4, 7);
  put32At(8, 4);
  put32At(12, 96);
  put16At(16, static_cast<std::uint16_t>(width));
  put16At(18, static_cast<std::uint16_t>(height));
  put32At(20, tf2::native::VtfHeader::TextureFlagsEnvMap);
  put16At(24, 1);                       // frame count
  put16At(26, 0);                       // first frame: not -1, so the header rule says six faces
  put32At(52, 0);                       // format 0: RGBA8888, identity decode
  file[56] = static_cast<std::uint8_t>(mipCount);
  put32At(57, 0);                       // low-res format
  file[61] = 1;                         // low-res width
  file[62] = 1;                         // low-res height
  put16At(63, 1);                       // depth
  put32At(68, 1);                       // one resource
  put32At(88, 0x30);                    // high-resolution image data
  put32At(92, 96);                      // ... which starts right after the header
  const auto colorFor = [&](int face) {
    return face < 6 ? faceColors[static_cast<std::size_t>(face)]
                    : std::array<std::uint8_t, 3>{255, 0, 255};
  };
  for (std::uint32_t mip = mipCount; mip-- > 0;) {
    for (int face = 0; face < faceSlots; ++face) {
      const auto color = colorFor(face);
      const std::size_t bytes = mipBytes(mip);
      for (std::size_t pixel = 0; pixel < bytes; pixel += 4) {
        for (std::size_t channel = 0; channel < 3; ++channel) {
          const std::uint8_t value = color[channel];
          file.push_back(mip == 0 ? value : static_cast<std::uint8_t>(255 - value));
        }
        file.push_back(255);
      }
    }
  }
  const std::size_t expected = 96
    + static_cast<std::size_t>(faceSlots) * (mipBytes(0) + mipBytes(1));
  if (file.size() != expected) return {};
  return file;
}

// A flat (non-cubemap) VTF in the same header shape as the cubemap builder: one
// mip, format 0 so the decode is an identity, and no ENVMAP flag, because a sky
// face is a plain texture and one that was flagged as a cube must be refused by
// the sky path rather than sampled as if it were flat.
std::vector<std::uint8_t> syntheticFlatVtf(std::uint32_t width, std::uint32_t height,
    const std::array<std::uint8_t, 3>& colour) {
  // Eighty bytes, not sixty-four: the depth field at offset 63 is two bytes
  // wide, so a 64-byte header would have the second one land on the first image
  // byte -- one byte past the end of the header vector. Writing it there is an
  // out-of-bounds write and reading it back is a texture whose first pixel is
  // the depth. The real files put the image after a 96-byte header; this builder
  // uses the smallest header that holds every field it sets.
  std::vector<std::uint8_t> file(80, 0);
  const auto put16At = [&](std::size_t at, std::uint16_t value) {
    file[at] = static_cast<std::uint8_t>(value & 0xFFu);
    file[at + 1] = static_cast<std::uint8_t>((value >> 8) & 0xFFu);
  };
  const auto put32At = [&](std::size_t at, std::uint32_t value) {
    for (int byte = 0; byte < 4; ++byte) {
      file[at + static_cast<std::size_t>(byte)] = static_cast<std::uint8_t>((value >> (byte * 8)) & 0xFFu);
    }
  };
  std::memcpy(file.data(), "VTF\0", 4);
  put32At(4, 7);
  put32At(8, 2);
  put32At(12, 80);
  put16At(16, static_cast<std::uint16_t>(width));
  put16At(18, static_cast<std::uint16_t>(height));
  put16At(24, 1);
  put32At(52, 0);
  file[56] = 1;
  file[61] = 1;
  file[62] = 1;
  put16At(63, 1);
  for (std::size_t pixel = 0; pixel < static_cast<std::size_t>(width) * height; ++pixel) {
    file.push_back(colour[0]);
    file.push_back(colour[1]);
    file.push_back(colour[2]);
    file.push_back(255);
  }
  return file;
}

std::string rgbaText(const std::vector<std::uint8_t>& rgba, std::size_t pixel) {
  const std::size_t at = pixel * 4u;
  if (at + 4u > rgba.size()) return "out-of-range";
  return std::to_string(rgba[at]) + "," + std::to_string(rgba[at + 1]) + ","
    + std::to_string(rgba[at + 2]) + "," + std::to_string(rgba[at + 3]);
}

// ---------------------------------------------------------------------------
// Fixture cases
// ---------------------------------------------------------------------------
// Each case builds real BSP bytes through buildFixtureBsp and reads them back
// through BspParser::parse, so the parser is what is under test. `mutate`
// returns false when the case has no mutation, which the runner treats as "no
// mutated run" rather than as a failure.

using CaseBody = std::function<std::vector<Check>(const tf2::native::BspMap&,
  const tf2::native::WorldCapabilityReadings&)>;
using CaseMutate = std::function<bool(tf2::native::BspFixture&,
  std::vector<tf2::native::FixedResource>&)>;

struct FixtureCase {
  std::string name;
  tf2::native::BspFixture fixture;
  std::vector<tf2::native::FixedResource> resources;
  // What the map's own material declares about water. Kept as explicit fields
  // rather than inferred from "resources is non-empty", because a fixture with
  // a water face and no material is a different case from one with neither.
  bool waterMaterialDeclared = false;
  bool waterTextureReachable = false;
  // The triangle limit handed to BspParser::parse. 200000 is what the product
  // uses; a case that needs the walk to stop early sets it lower.
  std::size_t triangleLimit = 200000;
  // The widest atlas the parser may choose. 4096 is the product's cap (T1,
  // 2026-10-13); a case that means to test the packer's drop path sets it to the
  // historical 2048, because at 4096 the faces that used to drop now fit -- which
  // is the whole point of the change and would otherwise erase the fixture.
  int atlasMaxWidth = 4096;
  CaseBody straight;
  CaseMutate mutate;
  CaseBody mutated;
};

// Flattened into the key space the fixture bodies and the gate assert on, so a
// fixture, the product and world-material-check.sh read one vocabulary.
std::map<std::string, std::string> flattenReadings(
    const tf2::native::WorldCapabilityReadings& readings) {
  using tf2::native::worldCapabilityStateName;
  std::map<std::string, std::string> out;
  const auto lightmap = tf2::native::judgeLightmap(readings.lightmap);
  const auto cubemap = tf2::native::judgeCubemaps(readings.cubemap);
  const auto sky = tf2::native::judgeSky(readings.sky);
  const auto displacement = tf2::native::judgeDisplacement(readings.displacement);
  const auto water = tf2::native::judgeWater(readings.water);
  const auto vis = tf2::native::judgeVis(readings.vis);

  out["lightmap.state"] = worldCapabilityStateName(lightmap.state);
  out["lightmap.reason"] = lightmap.reason;
  out["lightmap.facesWithLightmap"] = std::to_string(readings.lightmap.facesWithLightmap);
  out["lightmap.facesPacked"] = std::to_string(readings.lightmap.facesPacked);
  out["lightmap.facesDropped"] = std::to_string(readings.lightmap.facesDropped);
  out["lightmap.trianglesWithLightUv"] = std::to_string(readings.lightmap.trianglesWithLightUv);
  out["lightmap.triangles"] = std::to_string(readings.lightmap.triangles);
  out["lightmap.atlas"] = std::to_string(readings.lightmap.atlasWidth) + "x"
    + std::to_string(readings.lightmap.atlasHeight);
  // The shelf width the packer chose, whether it grew to get there, and the
  // sentence that says what happened to the faces that did not fit -- so "this
  // map loses faces to the atlas" is a reading with a cause rather than a
  // subtraction a reader has to notice.
  out["lightmap.atlasMaxWidth"] = std::to_string(readings.lightmap.atlasMaxWidth);
  out["lightmap.atlasGrown"] = readings.lightmap.atlasGrown ? "true" : "false";
  out["lightmap.atlasReason"] = readings.lightmap.atlasReason.empty()
    ? std::string("no atlas") : readings.lightmap.atlasReason;
  out["lightmap.uvAllInRange"] = readings.lightmap.uvAllInRange ? "true" : "false";

  out["cubemap.state"] = worldCapabilityStateName(cubemap.state);
  out["cubemap.reason"] = cubemap.reason;
  out["cubemap.samples"] = std::to_string(readings.cubemap.samples);
  out["cubemap.samplesComplete"] = std::to_string(readings.cubemap.samplesComplete);
  out["cubemap.fromProbe"] = std::to_string(readings.cubemap.samplesFromProbe);
  out["cubemap.fromPakfile"] = std::to_string(readings.cubemap.samplesFromPakfile);
  out["cubemap.facesDecoded"] = std::to_string(readings.cubemap.facesDecoded);
  out["cubemap.facesBeyondSix"] = std::to_string(readings.cubemap.facesBeyondSix);
  out["cubemap.firstFacesInFile"] = readings.cubemap.perSample.empty() ? std::string()
    : std::to_string(readings.cubemap.perSample.front().facesInFile);
  out["cubemap.firstDeclaredFaces"] = readings.cubemap.perSample.empty() ? std::string()
    : std::to_string(readings.cubemap.perSample.front().declaredFaces);
  // The first sample's own file, so a reader can see which naming convention the
  // measurement used instead of taking it on faith.
  // The first sample's six decoded faces, as means and checksums. A gate can
  // pin these; without them a decoder that walks the image section at the wrong
  // stride still reports six faces and passes every count.
  {
    std::string means;
    std::string sums;
    char text[32];
    for (int face = 0; face < 6; ++face) {
      if (!readings.cubemap.perSample.empty()) {
        std::snprintf(text, sizeof(text), "%s%d", face ? "," : "",
          readings.cubemap.perSample.front().faceMean[face]);
        means += text;
        std::snprintf(text, sizeof(text), "%s%08x", face ? "," : "",
          readings.cubemap.perSample.front().faceChecksum[face]);
        sums += text;
      }
    }
    out["cubemap.firstFaceMeans"] = means;
    out["cubemap.firstFaceChecksums"] = sums;
  }
  out["cubemap.firstPath"] = readings.cubemap.perSample.empty()
    ? std::string() : readings.cubemap.perSample.front().vtfPath;
  out["cubemap.firstSource"] = readings.cubemap.perSample.empty()
    ? std::string() : readings.cubemap.perSample.front().source;

  out["sky.state"] = worldCapabilityStateName(sky.state);
  out["sky.reason"] = sky.reason;
  out["sky.name"] = readings.sky.name;
  out["sky.faces"] = std::to_string(readings.sky.skyFaces);
  out["sky.vmtFound"] = std::to_string(readings.sky.vmtFound);
  out["sky.texturesResolved"] = std::to_string(readings.sky.texturesResolved);
  out["sky.texturesDecoded"] = std::to_string(readings.sky.texturesDecoded);
  out["sky.firstSize"] = std::to_string(readings.sky.firstWidth) + "x"
    + std::to_string(readings.sky.firstHeight);
  out["sky.smallestSize"] = std::to_string(readings.sky.smallestWidth) + "x"
    + std::to_string(readings.sky.smallestHeight);
  out["sky.decodedBytes"] = std::to_string(readings.sky.decodedBytes);
  // T8 (2026-10-16): the six-way pairing, one group of keys per cube face.
  // `sky.texturesDecoded == 6` says six files decoded; these say which side of
  // the box each one is on, which is a different claim and the one a swapped
  // suffix table would break while leaving every counter above unchanged.
  for (int face = 0; face < 6; ++face) {
    const auto& origin = readings.sky.origins[static_cast<std::size_t>(face)];
    const std::string key = "sky.face" + std::to_string(face) + ".";
    out[key + "dir"] = origin.direction;
    out[key + "suffix"] = origin.suffix;
    out[key + "vmt"] = origin.materialPath;
    out[key + "vtf"] = origin.texturePath;
    out[key + "rgba"] = origin.sampleRgba;
    out[key + "decoded"] = origin.decoded ? "true" : "false";
  }

  out["displacement.state"] = worldCapabilityStateName(displacement.state);
  out["displacement.reason"] = displacement.reason;
  out["displacement.faces"] = std::to_string(readings.displacement.faces);
  out["displacement.triangles"] = std::to_string(readings.displacement.triangles);
  out["displacement.trianglesTagged"] = std::to_string(readings.displacement.trianglesTagged);
  out["displacement.cornerMatches"] = std::to_string(readings.displacement.cornerMatches);

  out["water.state"] = worldCapabilityStateName(water.state);
  out["water.reason"] = water.reason;
  out["water.faces"] = std::to_string(readings.water.faces);
  out["water.trianglesTagged"] = std::to_string(readings.water.trianglesTagged);

  out["vis.state"] = worldCapabilityStateName(vis.state);
  out["vis.reason"] = vis.reason;
  out["vis.mode"] = readings.vis.mode;
  out["vis.clusters"] = std::to_string(readings.vis.clusters);
  out["vis.visibleFromCluster0"] = std::to_string(readings.vis.visibleFromCluster0);
  out["vis.trianglesWithCluster"] = std::to_string(readings.vis.trianglesWithCluster);
  return out;
}

std::vector<Check> readingChecks(const tf2::native::WorldCapabilityReadings& readings,
    const std::vector<std::pair<std::string, std::string>>& expect) {
  const auto flat = flattenReadings(readings);
  std::vector<Check> checks;
  for (const auto& [key, expected] : expect) {
    const auto at = flat.find(key);
    checks.push_back(checkStr(key, at == flat.end() ? "<absent>" : at->second, expected));
  }
  return checks;
}

// T8 (2026-10-16): the six sky sides have to land on the six cube directions
// they belong to, and the box has to be carrying six different decoded images
// rather than one flat colour. Both are asserted per face, and `expectations` is
// written out by the caller instead of being derived from the product's own
// tables -- a check built from the table it is checking is a tautology.
struct SkyFaceExpectation {
  const char* suffix;
  const char* direction;
  const char* rgba;
};

std::vector<Check> skyFaceMappingChecks(const tf2::native::WorldCapabilityReadings& readings,
    const std::vector<SkyFaceExpectation>& expect) {
  const auto flat = flattenReadings(readings);
  std::vector<Check> checks;
  std::vector<std::string> samples;
  for (std::size_t face = 0; face < expect.size(); ++face) {
    const std::string key = "sky.face" + std::to_string(face) + ".";
    const auto& wanted = expect[face];
    const auto get = [&](const std::string& tail) {
      const auto at = flat.find(key + tail);
      return at == flat.end() ? std::string("<absent>") : at->second;
    };
    checks.push_back(checkStr(key + "suffix", get("suffix"), wanted.suffix));
    checks.push_back(checkStr(key + "dir", get("dir"), wanted.direction));
    checks.push_back(checkStr(key + "rgba", get("rgba"), wanted.rgba));
    checks.push_back(checkStr(key + "vmt", get("vmt"),
      "materials/skybox/" + readings.sky.name + wanted.suffix + ".vmt"));
    checks.push_back(checkStr(key + "vtf", get("vtf"),
      "materials/skybox/" + readings.sky.name + wanted.suffix + ".vtf"));
    checks.push_back(checkStr(key + "decoded", get("decoded"), "true"));
    samples.push_back(get("rgba"));
  }
  // Six sides, six different pixels. A sky that sampled one constant would keep
  // every counter above at six and show the same thing in every direction, which
  // is the failure this line is here to make visible.
  std::vector<std::string> sorted = samples;
  std::sort(sorted.begin(), sorted.end());
  sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());
  checks.push_back(checkNum("sky.distinctSamples", sorted.size(), samples.size()));
  return checks;
}

tf2::native::BspFixtureFace lightmapQuad() {
  tf2::native::BspFixtureFace face;
  const float corners[4][3] = {{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f},
    {1.0f, 1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}};
  for (int i = 0; i < 4; ++i) {
    face.corners[i][0] = corners[i][0];
    face.corners[i][1] = corners[i][1];
    face.corners[i][2] = corners[i][2];
  }
  // Lightmap axes map the quad onto its own luxel box: corner (0,0) -> luxel
  // (0,0) and corner (1,1) -> luxel (1,1), with minsU/minsV at 0.
  face.luAxis[0] = 1.0f; face.luAxis[1] = 0.0f; face.luAxis[2] = 0.0f; face.luAxis[3] = 0.0f;
  face.lvAxis[0] = 0.0f; face.lvAxis[1] = 1.0f; face.lvAxis[2] = 0.0f; face.lvAxis[3] = 0.0f;
  face.minsU = 0;
  face.minsV = 0;
  face.samplesW = 1;  // stored as value-1, so the grid is 2x2
  face.samplesH = 1;
  face.lightOffset = 0;
  face.style0 = 0;
  return face;
}

std::vector<FixtureCase> buildCases() {
  using tf2::native::BspFixture;
  std::vector<FixtureCase> cases;

  // ---- 1. per-face RGBExp32 lightmap --------------------------------------
  {
    FixtureCase item;
    item.name = "lightmap-per-face";
    // Four luxels chosen so one channel of the decode is non-trivial: exponent
    // 0 is the identity, exponent 1 doubles.
    tf2::native::appendLuxel(item.fixture.lighting, 200, 100, 50, 0);
    tf2::native::appendLuxel(item.fixture.lighting, 10, 20, 30, 1);
    tf2::native::appendLuxel(item.fixture.lighting, 255, 255, 255, 0);
    tf2::native::appendLuxel(item.fixture.lighting, 0, 0, 0, 0);
    item.fixture.faces.push_back(lightmapQuad());
    const auto body = [](const tf2::native::BspMap& map,
        const tf2::native::WorldCapabilityReadings& readings) {
      std::vector<Check> checks = readingChecks(readings, {
        {"lightmap.state", "active"},
        {"lightmap.facesWithLightmap", "1"},
        {"lightmap.facesPacked", "1"},
        {"lightmap.facesDropped", "0"},
        {"lightmap.trianglesWithLightUv", "2"},
        {"lightmap.triangles", "2"},
        {"lightmap.atlas", "2048x2"},
        {"lightmap.uvAllInRange", "true"},
      });
      // The atlas texel a triangle's light uv points at is that face's own
      // luxel, decoded. This is the per-face claim made checkable.
      checks.push_back(checkStr("atlasPixel00", atlasPixelAt(map, 0, 0),
        atlasPixelText(expectedAtlasPixel(200, 100, 50, 0))));
      checks.push_back(checkStr("atlasPixel10", atlasPixelAt(map, 1, 0),
        atlasPixelText(expectedAtlasPixel(10, 20, 30, 1))));
      const auto& triangle = map.triangles.front();
      // The parser rescales v when the trimmed atlas is shorter than the shelf
      // width, so the expectation carries the same factor; both sides go through
      // the same derivation and cannot drift apart silently. The width comes
      // from the map rather than from the literal 2048 it used to be: the shelf
      // is chosen now, and a fixture that kept the old literal would assert the
      // right uv for the wrong atlas.
      const int atlasWidth = map.lightmapAtlasWidth;
      const double vScale = map.lightmapAtlasHeight != atlasWidth
        ? static_cast<double>(atlasWidth) / static_cast<double>(map.lightmapAtlasHeight) : 1.0;
      checks.push_back(checkNear("lightUv.a.u", triangle.alu,
        expectedLightUv(0, 1, 2, 0, 0, atlasWidth), 1e-9));
      checks.push_back(checkNear("lightUv.a.v", triangle.alv,
        expectedLightUv(0, 1, 2, 0, 0, atlasWidth) * vScale, 1e-9));
      checks.push_back(checkNear("lightUv.b.u", triangle.blu,
        expectedLightUv(1, 1, 2, 0, 0, atlasWidth), 1e-9));
      checks.push_back(checkNear("lightUv.c.v", triangle.clv,
        expectedLightUv(1, 1, 2, 0, 0, atlasWidth) * vScale, 1e-9));
      return checks;
    };
    item.straight = body;
    // The exponent byte of the first luxel. If the atlas is not driven by the
    // face's own samples, this changes nothing and the mutation is caught.
    item.mutate = [](BspFixture& f, std::vector<tf2::native::FixedResource>&) { f.lighting[3] = 1; return true; };
    item.mutated = [](const tf2::native::BspMap& map,
        const tf2::native::WorldCapabilityReadings& readings) {
      std::vector<Check> checks = readingChecks(readings, {{"lightmap.state", "active"}});
      checks.push_back(checkStr("atlasPixel00", atlasPixelAt(map, 0, 0),
        atlasPixelText(expectedAtlasPixel(200, 100, 50, 1))));
      return checks;
    };
    cases.push_back(std::move(item));
  }

  // ---- 2. the atlas's loss is measured, not assumed ------------------------
  // The parser caps a face's lightmap at 512x512 samples (a bigger box makes
  // lightBytes zero, so the face stops being a lightmap face at all), which
  // means the only way to lose a face to the packer is to fill the 2048-tall
  // atlas first. Four rows of four 512x512 faces fill it exactly, so the
  // seventeenth face is the one that drops.
  {
    FixtureCase item;
    item.name = "lightmap-atlas-capacity";
    // Pinned to the historical width on purpose: at the product's 4096 cap the
    // seventeenth face fits and nothing drops, so the drop path can only be
    // exercised at 2048. The pair of readings that makes this a check rather
    // than a description is the self-test's -- same seventeen faces, dropped 1
    // at 2048 and dropped 0 at 4096.
    item.atlasMaxWidth = 2048;
    // One lighting slice shared by every face. The parser reads each face's
    // box out of the same bytes, which is what makes a 1 MB fixture able to
    // carry seventeen 512x512 lightmaps.
    item.fixture.lighting.assign(512u * 512u * 4u, 0x40);
    for (int i = 0; i < 17; ++i) {
      auto face = lightmapQuad();
      face.samplesW = 511;   // stored as value-1: the box is 512x512
      face.samplesH = 511;
      // The quad spans the whole luxel box so every corner lands inside it.
      face.corners[1][0] = 511.0f;
      face.corners[2][0] = 511.0f;
      face.corners[2][1] = 511.0f;
      face.corners[3][1] = 511.0f;
      item.fixture.faces.push_back(face);
    }
    item.straight = [](const tf2::native::BspMap&,
        const tf2::native::WorldCapabilityReadings& readings) {
      return readingChecks(readings, {
        {"lightmap.state", "active"},
        {"lightmap.facesWithLightmap", "17"},
        {"lightmap.facesPacked", "16"},
        {"lightmap.facesDropped", "1"},
        {"lightmap.trianglesWithLightUv", "32"},
        {"lightmap.atlas", "2048x2048"},
        {"lightmap.uvAllInRange", "true"},
      });
    };
    // Sixteen faces fill the atlas exactly and nothing drops. This is the
    // reading that makes the previous one meaningful: without it, "dropped 1"
    // could be a constant.
    item.mutate = [](tf2::native::BspFixture& f, std::vector<tf2::native::FixedResource>&) {
      f.faces.pop_back();
      return true;
    };
    item.mutated = [](const tf2::native::BspMap&,
        const tf2::native::WorldCapabilityReadings& readings) {
      return readingChecks(readings, {
        {"lightmap.state", "active"},
        {"lightmap.facesWithLightmap", "16"},
        {"lightmap.facesPacked", "16"},
        {"lightmap.facesDropped", "0"},
        {"lightmap.trianglesWithLightUv", "32"},
      });
    };
    cases.push_back(std::move(item));
  }

  // ---- 2b. counted but unsampled -------------------------------------------
  // The other way to reach "faces with lightmap data, zero triangles sampling
  // it": the caller's triangle limit stops the walk before any triangle is
  // emitted, while the atlas is still packed. Zero is the degenerate end of the
  // same knob the product sets to 200000.
  {
    FixtureCase item;
    item.name = "lightmap-triangle-limit";
    item.triangleLimit = 0;
    item.fixture.lighting.assign(512u * 512u * 4u, 0x40);
    auto face = lightmapQuad();
    face.samplesW = 511;
    face.samplesH = 511;
    item.fixture.faces.push_back(face);
    item.straight = [](const tf2::native::BspMap& map,
        const tf2::native::WorldCapabilityReadings& readings) {
      auto checks = readingChecks(readings, {
        {"lightmap.state", "fallback"},
        {"lightmap.facesWithLightmap", "1"},
        {"lightmap.facesPacked", "1"},
        {"lightmap.trianglesWithLightUv", "0"},
      });
      // The atlas exists and holds the packed face; what is missing is any
      // triangle to sample it. Saying which of the two failed is the whole
      // point of the separate branches in judgeLightmap.
      checks.push_back(checkBool("atlasPackedAnyway", !map.lightmapAtlas.empty(), true));
      const auto flat = flattenReadings(readings);
      const auto at = flat.find("lightmap.reason");
      checks.push_back(checkStr("reasonNamesTriangleLimit",
        at == flat.end() || at->second.find("triangle limit") == std::string::npos
          ? "not-named" : "named", "named"));
      return checks;
    };
    cases.push_back(std::move(item));
  }

  // ---- 3. cubemap: six-face discovery, order, sampling --------------------
  // Two samples on purpose, one per store: the loose tree answers the first and
  // the map's own pakfile answers the second, which is the store every TF2 map
  // actually uses. The colours are the face identities -- face f is a solid
  // colour no other face has -- so a reader that takes the faces in the wrong
  // order, or reads a chain at the wrong offset, returns a colour the fixture
  // never wrote.
  {
    FixtureCase item;
    item.name = "cubemap-six-face";
    item.fixture.cubemaps.push_back({{100, 200, 300}, 64});
    item.fixture.cubemaps.push_back({{-400, 500, 600}, 64});
    item.fixture.faces.push_back(lightmapQuad());
    const std::array<std::array<std::uint8_t, 3>, 6> faceColors = {{
      {204, 20, 20}, {20, 204, 20}, {20, 20, 204},
      {204, 204, 20}, {204, 20, 204}, {20, 204, 204},
    }};
    const auto loosePath = std::string("materials/maps/fixture/c100_200_300.vtf");
    const auto pakPath = std::string("materials/maps/fixture/c-400_500_600.vtf");
    const auto looseBytes = syntheticCubemapVtf(4, 4, faceColors);
    item.resources.push_back({loosePath, looseBytes});
    item.fixture.pakfileEntries[pakPath] = syntheticCubemapVtf(4, 4, faceColors);
    item.straight = [faceColors, looseBytes](const tf2::native::BspMap&,
        const tf2::native::WorldCapabilityReadings& readings) {
      auto checks = readingChecks(readings, {
        {"cubemap.state", "active"},
        {"cubemap.samples", "2"},
        {"cubemap.samplesComplete", "2"},
        {"cubemap.fromProbe", "1"},
        {"cubemap.fromPakfile", "1"},
        {"cubemap.facesDecoded", "12"},
        // The file carries seven face slots where its own header rule declares
        // six, which is what every cubemap in this install measures. Both
        // numbers are asserted, so a reading that quietly takes one for the
        // other goes red here.
        {"cubemap.facesBeyondSix", "2"},
        {"cubemap.firstFacesInFile", "7"},
        {"cubemap.firstDeclaredFaces", "6"},
        {"cubemap.firstPath", "materials/maps/fixture/c100_200_300.vtf"},
        {"cubemap.firstSource", "loose-or-vpk"},
      });
      // The order claim, checked against the pixels rather than against the
      // index the decoder was handed: face f of the fixture's own file must
      // decode to face f's colour, at its first and its last pixel. The second
      // mip of every face holds the inverse colour, so a decoder that reads the
      // chain at the wrong end fails this too.
      tf2::native::VtfTexture texture;
      std::string error;
      checks.push_back(checkBool("synthVtfParses", texture.parse(looseBytes, &error), true));
      for (int face = 0; face < 6; ++face) {
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        const auto rgba = texture.decodeCubeFaceRgba(looseBytes, face, width, height, &error);
        const std::string expected = std::to_string(faceColors[face][0]) + ","
          + std::to_string(faceColors[face][1]) + "," + std::to_string(faceColors[face][2]) + ",255";
        checks.push_back(checkStr("cubeFace" + std::to_string(face) + "FirstPixel",
          rgbaText(rgba, 0), expected));
        checks.push_back(checkStr("cubeFace" + std::to_string(face) + "LastPixel",
          rgbaText(rgba, 15), expected));
        checks.push_back(checkNum("cubeFace" + std::to_string(face) + "Width",
          static_cast<int>(width), 4));
        checks.push_back(checkNum("cubeFace" + std::to_string(face) + "Height",
          static_cast<int>(height), 4));
      }
      return checks;
    };
    // Break both samples in the two ways that are not "the six faces are all
    // there": take the loose file away so nothing resolves it, and cut the
    // pakfile's file down to five face chains so it resolves, parses, and then
    // turns out to be missing a face. Zero complete samples is the state whose
    // reason has to name both causes -- a path that is not there and a file that
    // is -- and that naming is what a reader of the real install's reading has
    // to be able to trust.
    item.mutate = [](tf2::native::BspFixture& fixture,
        std::vector<tf2::native::FixedResource>& resources) {
      resources.clear();
      auto& embedded = fixture.pakfileEntries["materials/maps/fixture/c-400_500_600.vtf"];
      // Cut the file down to three whole faces -- 80 bytes per face, the
      // smaller mip and the top mip -- so it stays self-consistent: it parses,
      // it measures three face slots, and the three faces it claims are the
      // three it holds. Faces 3, 4 and 5 are then refused rather than decoded
      // from whatever bytes happen to follow.
      const std::size_t threeFaces = 96 + 3 * 80;
      if (embedded.size() > threeFaces) embedded.resize(threeFaces);
      return true;
    };
    item.mutated = [](const tf2::native::BspMap&,
        const tf2::native::WorldCapabilityReadings& readings) {
      auto checks = readingChecks(readings, {
        {"cubemap.state", "fallback"},
        {"cubemap.samples", "2"},
        {"cubemap.samplesComplete", "0"},
        {"cubemap.fromProbe", "0"},
        {"cubemap.fromPakfile", "0"},
        // Three of the six faces survived the cut, and the number is exact: a
        // partial count that rounded to zero would hide how much of the file
        // was still readable.
        {"cubemap.facesDecoded", "3"},
        {"cubemap.facesBeyondSix", "0"},
      });
      const auto flat = flattenReadings(readings);
      const auto at = flat.find("cubemap.reason");
      const std::string reason = at == flat.end() ? std::string() : at->second;
      checks.push_back(checkStr("cubemap.reasonNamesMissingPath",
        reason.find("c100_200_300.vtf") == std::string::npos ? "not-named" : "named", "named"));
      checks.push_back(checkStr("cubemap.reasonNamesUndecodedFace",
        reason.find("face 3 did not decode") == std::string::npos ? "not-named" : "named", "named"));
      return checks;
    };
    cases.push_back(std::move(item));
  }

  // ---- 4. skybox -----------------------------------------------------------
  {
    FixtureCase item;
    item.name = "skybox-six-vmt";
    item.fixture.entities = "\"skyname\" \"sky_fixture_01\"\n";
    item.fixture.faces.push_back(lightmapQuad());
    // Six VMTs that name six textures, and six textures that decode: a fixture
    // whose VMTs were names with no bytes would prove "the files are listed" and
    // nothing about the sky, which is the reading this capability is about.
    for (int face = 0; face < 6; ++face) {
      const std::string suffix = tf2::native::worldCubemapFaceSuffix(face);
      const std::string stem = "materials/skybox/sky_fixture_01" + suffix;
      // One string, then its bytes. The first version of this built the vector
      // from the begin() of one temporary and the end() of another: two
      // different buffers, and an iterator range that walked off the end and
      // took the process down with STATUS_STACK_BUFFER_OVERRUN before a single
      // fixture printed.
      const std::string vmtText = "UnlitGeneric\n{\n\t\"$basetexture\" \"skybox/sky_fixture_01"
        + suffix + "\"\n}\n";
      item.resources.push_back({stem + ".vmt",
        std::vector<std::uint8_t>(vmtText.begin(), vmtText.end())});
      item.resources.push_back({stem + ".vtf",
        syntheticFlatVtf(8, 4, {{40, 90, 200}})});
    }
    item.straight = [](const tf2::native::BspMap&,
        const tf2::native::WorldCapabilityReadings& readings) {
      return readingChecks(readings, {
        {"sky.state", "active"},
        {"sky.name", "sky_fixture_01"},
        {"sky.vmtFound", "6"},
        {"sky.texturesResolved", "6"},
        {"sky.texturesDecoded", "6"},
        {"sky.firstSize", "8x4"},
        {"sky.smallestSize", "8x4"},
        {"sky.decodedBytes", "768"},
      });
    };
    // Take the DOWN face's texture, not its vmt: the vmt still resolves, so the
    // reading has to distinguish "five vmts" from "six vmts, five textures" --
    // the two failures this capability separates.
    item.mutate = [](tf2::native::BspFixture&, std::vector<tf2::native::FixedResource>& resources) {
      const std::string path = "materials/skybox/sky_fixture_01dn.vtf";
      const auto at = std::find_if(resources.begin(), resources.end(),
        [&](const tf2::native::FixedResource& resource) { return resource.path == path; });
      if (at != resources.end()) resources.erase(at);
      return true;
    };
    item.mutated = [](const tf2::native::BspMap&,
        const tf2::native::WorldCapabilityReadings& readings) {
      auto checks = readingChecks(readings, {
        {"sky.state", "fallback"},
        {"sky.vmtFound", "6"},
        {"sky.texturesResolved", "5"},
        {"sky.texturesDecoded", "5"},
      });
      const auto flat = flattenReadings(readings);
      const auto at = flat.find("sky.reason");
      const std::string reason = at == flat.end() ? std::string() : at->second;
      checks.push_back(checkStr("sky.reasonNamesTexture",
        reason.find("sky_fixture_01dn.vtf") == std::string::npos ? "not-named" : "named", "named"));
      return checks;
    };
    cases.push_back(std::move(item));
  }

  // ---- 4b. T8 (2026-10-16): the six sky sides land on the six directions ----
  // `skybox-six-vmt` gives all six sides the SAME colour, so it can prove six
  // files decode and cannot prove which one the renderer put on +Z. This case
  // gives each side a different flat colour and then asserts, per cube index,
  // the direction, the suffix, the two paths and one sampled pixel.
  //
  // The expected suffix and direction per index are written out literally below
  // rather than read back through `worldCubemapFaceSuffix` / `worldSkyFaceDirection`:
  // an assertion built from the table it is meant to check is a tautology. These
  // six pairs are the Source convention (rt=+X, lf=-X, bk=+Y, ft=-Y, up=+Z,
  // dn=-Z), stated here as the thing the product has to agree with.
  {
    FixtureCase item;
    item.name = "skybox-six-distinct";
    item.fixture.entities = "\"skyname\" \"sky_distinct_01\"\n";
    item.fixture.faces.push_back(lightmapQuad());
    static const char* expectedSuffixes[6] = {"rt", "lf", "bk", "ft", "up", "dn"};
    static const std::array<std::uint8_t, 3> expectedColours[6] = {
      {{255, 0, 0}}, {{0, 255, 0}}, {{0, 0, 255}},
      {{255, 255, 0}}, {{255, 0, 255}}, {{0, 255, 255}},
    };
    for (int face = 0; face < 6; ++face) {
      const std::string suffix = expectedSuffixes[face];
      const std::string stem = "materials/skybox/sky_distinct_01" + suffix;
      const std::string vmtText = "UnlitGeneric\n{\n\t\"$basetexture\" \"skybox/sky_distinct_01"
        + suffix + "\"\n}\n";
      item.resources.push_back({stem + ".vmt",
        std::vector<std::uint8_t>(vmtText.begin(), vmtText.end())});
      item.resources.push_back({stem + ".vtf",
        syntheticFlatVtf(8, 4, expectedColours[face])});
    }
    item.straight = [](const tf2::native::BspMap&,
        const tf2::native::WorldCapabilityReadings& readings) {
      return skyFaceMappingChecks(readings, {
        {"rt", "+X", "255,0,0,255"}, {"lf", "-X", "0,255,0,255"},
        {"bk", "+Y", "0,0,255,255"}, {"ft", "-Y", "255,255,0,255"},
        {"up", "+Z", "255,0,255,255"}, {"dn", "-Z", "0,255,255,255"},
      });
    };
    // No probe-level mutation: the defect this case catches is a swap in the
    // product's own suffix table, which is a source mutation. The gate applies it
    // with sed, rebuilds, and asserts these per-face colours go red -- a fixture
    // data mutation cannot, because the table lives in native/src, not in the
    // fixture's resource list.
    cases.push_back(std::move(item));
  }

  // ---- 5. displacement -----------------------------------------------------
  {
    FixtureCase item;
    item.name = "displacement-flat-grid";
    auto face = lightmapQuad();
    face.dispIndex = 0;
    item.fixture.faces.push_back(face);
    tf2::native::BspFixture::Displacement disp;
    disp.start = {face.corners[0][0], face.corners[0][1], face.corners[0][2]};
    disp.power = 2;   // grid size 5 -> 32 triangles
    disp.vertStart = 0;
    // Flat: a null direction with zero distance leaves every grid point on the
    // bilinear surface, so the displacement face covers exactly the footprint
    // of the flat quad and only the triangle count moves.
    for (int i = 0; i < 25; ++i) disp.verts.push_back({0.0f, 0.0f, 0.0f, 0.0f});
    for (const auto& vert : disp.verts) {
      // Twenty bytes per vert: direction (3 floats), distance, then four bytes
      // the format reserves. Sixteen would make the parser count twenty verts
      // where twenty-five were written and silently build no grid at all.
      for (int channel = 0; channel < 4; ++channel) {
        const float value = channel < 3 ? vert[channel] : vert[3];
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(&value);
        item.fixture.dispVertBytes.insert(item.fixture.dispVertBytes.end(), bytes, bytes + 4);
      }
      item.fixture.dispVertBytes.resize(item.fixture.dispVertBytes.size() + 4, 0);
    }
    item.fixture.displacements.push_back(disp);
    item.straight = [](const tf2::native::BspMap& map,
        const tf2::native::WorldCapabilityReadings& readings) {
      auto checks = readingChecks(readings, {
        {"displacement.state", "active"},
        {"displacement.faces", "1"},
        {"displacement.triangles", "32"},
        {"displacement.trianglesTagged", "32"},
        {"displacement.cornerMatches", "1"},
      });
      // The flat quad alone would be 2 triangles; 32 is the subdivided grid, so
      // the displacement path really ran rather than the fallback.
      checks.push_back(checkBool("gridSubdivides", map.triangles.size() > 2, true));
      return checks;
    };
    // power 9 is outside the parser's 2..4, so no grid is built and the face
    // falls back to the flat quad.
    item.mutate = [](BspFixture& f, std::vector<tf2::native::FixedResource>&) {
      f.displacements[0].power = 9; return true;
    };
    item.mutated = [](const tf2::native::BspMap&,
        const tf2::native::WorldCapabilityReadings& readings) {
      return readingChecks(readings, {
        {"displacement.state", "unavailable"},
        {"displacement.faces", "0"},
        {"displacement.triangles", "0"},
        {"displacement.trianglesTagged", "0"},
      });
    };
    cases.push_back(std::move(item));
  }

  // ---- 6. water ------------------------------------------------------------
  {
    FixtureCase item;
    item.name = "water-capability";
    auto face = lightmapQuad();
    face.flags = 8;   // the parser's water bit
    item.fixture.faces.push_back(face);
    item.resources.push_back({"materials/fixture/water.vtf", {}});
    item.waterMaterialDeclared = true;
    item.waterTextureReachable = true;
    item.straight = [](const tf2::native::BspMap&,
        const tf2::native::WorldCapabilityReadings& readings) {
      auto checks = readingChecks(readings, {
        {"water.state", "fallback"},
        {"water.faces", "1"},
        {"water.trianglesTagged", "2"},
      });
      // The product's own settings carry waterReflection/waterRefraction; the
      // BSP hard-codes both false. The reading has to say which of the two the
      // fallback came from, and here it is the unimplemented render target.
      const auto flat = flattenReadings(readings);
      const auto at = flat.find("water.reason");
      checks.push_back(checkStr("water.reasonNamesRenderTarget",
        at == flat.end() || at->second.find("render target") == std::string::npos
          ? "not-named" : "named", "named"));
      return checks;
    };
    // Clear the water bit: the faces stop being water faces entirely, which is
    // the difference between "water that cannot reflect" and "no water".
    item.mutate = [](BspFixture& f, std::vector<tf2::native::FixedResource>&) {
      f.faces[0].flags = 0; return true;
    };
    item.mutated = [](const tf2::native::BspMap&,
        const tf2::native::WorldCapabilityReadings& readings) {
      return readingChecks(readings, {
        {"water.state", "unavailable"},
        {"water.faces", "0"},
        {"water.trianglesTagged", "0"},
      });
    };
    cases.push_back(std::move(item));
  }

  // ---- 7. VIS --------------------------------------------------------------
  {
    FixtureCase item;
    item.name = "vis-cluster-clip";
    item.fixture.clusterCount = 4;
    item.fixture.visBits = {0x03, 0x01, 0x00, 0x00};
    // Cluster 0 sees clusters 0 and 1; the rest see only themselves.
    item.fixture.withTree = true;
    item.fixture.splitX = 0.5f;
    item.fixture.leaves.push_back({0, {0}});
    item.fixture.leaves.push_back({1, {}});
    item.fixture.faces.push_back(lightmapQuad());
    item.straight = [](const tf2::native::BspMap&,
        const tf2::native::WorldCapabilityReadings& readings) {
      return readingChecks(readings, {
        {"vis.state", "active"},
        {"vis.mode", "cluster-pvs"},
        {"vis.clusters", "4"},
        {"vis.visibleFromCluster0", "2"},
        {"vis.trianglesWithCluster", "2"},
      });
    };
    // Cluster 0's mask loses a bit: it now sees one cluster instead of two.
    item.mutate = [](BspFixture& f, std::vector<tf2::native::FixedResource>&) {
      f.visBits[0] = 0x01; return true;
    };
    item.mutated = [](const tf2::native::BspMap&,
        const tf2::native::WorldCapabilityReadings& readings) {
      return readingChecks(readings, {
        {"vis.state", "active"},
        {"vis.visibleFromCluster0", "1"},
      });
    };
    cases.push_back(std::move(item));
  }

  // ---- 8. the camera's own cluster is answerable ---------------------------
  {
    FixtureCase item;
    item.name = "vis-camera-cluster";
    item.fixture.clusterCount = 2;
    item.fixture.visBits = {0x03, 0x03};
    item.fixture.withTree = true;
    item.fixture.splitX = 0.5f;
    item.fixture.leaves.push_back({0, {0}});
    item.fixture.leaves.push_back({1, {}});
    item.fixture.faces.push_back(lightmapQuad());
    item.straight = [](const tf2::native::BspMap& map,
        const tf2::native::WorldCapabilityReadings& readings) {
      auto checks = readingChecks(readings, {{"vis.mode", "cluster-pvs"}});
      // clusterAt is the only way a camera position becomes a cluster, so if it
      // is wrong the clip clips the wrong half of the map. A point on the +x
      // side of the split lands in leaf 0 (cluster 0); a point on the -x side
      // lands in leaf 1 (cluster 1).
      checks.push_back(checkNum("clusterAtPlusX",
        tf2::native::BspParser::clusterAt(map, 2.0f, 0.0f, 0.0f), 0));
      checks.push_back(checkNum("clusterAtMinusX",
        tf2::native::BspParser::clusterAt(map, -2.0f, 0.0f, 0.0f), 1));
      checks.push_back(checkNum("clusterAtNaN",
        tf2::native::BspParser::clusterAt(map, NAN, 0.0f, 0.0f), -1));
      return checks;
    };
    cases.push_back(std::move(item));
  }

  return cases;
}

// ---------------------------------------------------------------------------
// Runner
// ---------------------------------------------------------------------------

struct CaseOutcome {
  std::string name;
  std::vector<Check> straight;
  std::vector<Check> mutated;
  bool mutationRan = false;
};

CaseOutcome runCase(const FixtureCase& item, bool withMutation) {
  CaseOutcome outcome;
  outcome.name = item.name;
  const auto evaluate = [&](const tf2::native::BspFixture& fixture,
      const std::vector<tf2::native::FixedResource>& resources, const CaseBody& body) {
    const auto bytes = tf2::native::buildFixtureBsp(fixture);
    tf2::native::BspMap map;
    tf2::native::BspParser::parse(bytes, map, item.triangleLimit, item.atlasMaxWidth);
    tf2::native::FixedResourceProbe probe(resources);
    const auto readings = tf2::native::measureWorldCapabilities(map, fixture.mapName, probe,
      false, false, item.waterMaterialDeclared, item.waterTextureReachable, bytes, item.triangleLimit);
    return body(map, readings);
  };
  outcome.straight = evaluate(item.fixture, item.resources, item.straight);
  if (withMutation && item.mutate && item.mutated) {
    auto fixture = item.fixture;
    auto resources = item.resources;
    if (item.mutate(fixture, resources)) {
      outcome.mutationRan = true;
      outcome.mutated = evaluate(fixture, resources, item.mutated);
    }
  }
  return outcome;
}

std::string checksToJson(const std::vector<Check>& checks) {
  std::ostringstream text;
  text << "[";
  bool first = true;
  for (const auto& check : checks) {
    if (!first) text << ",";
    first = false;
    std::string detail = check.detail;
    // A detail is built from paths and numbers, but a path is caller data: one
    // quote in it would end the JSON string and silently truncate the log.
    std::replace(detail.begin(), detail.end(), '"', '\'');
    text << "{\"key\":\"" << check.key << "\",\"ok\":" << (check.ok ? "true" : "false")
      << ",\"detail\":\"" << detail << "\"}";
  }
  text << "]";
  return text.str();
}

std::size_t countBad(const std::vector<Check>& checks) {
  std::size_t bad = 0;
  for (const auto& check : checks) if (!check.ok) ++bad;
  return bad;
}

int runFixtures(bool withMutation) {
  const auto cases = buildCases();
  std::size_t total = 0, bad = 0, mutations = 0, mutationsBad = 0;
  for (const auto& item : cases) {
    const auto outcome = runCase(item, withMutation);
    total += outcome.straight.size();
    bad += countBad(outcome.straight);
    for (const auto& check : outcome.straight) {
      if (!check.ok) {
        std::cerr << "FAIL " << outcome.name << " " << check.key << ": " << check.detail << "\n";
      }
    }
    if (outcome.mutationRan) {
      ++mutations;
      total += outcome.mutated.size();
      const auto mutatedBad = countBad(outcome.mutated);
      bad += mutatedBad;
      mutationsBad += mutatedBad;
      for (const auto& check : outcome.mutated) {
        if (!check.ok) {
          std::cerr << "FAIL " << outcome.name << " [mutated] " << check.key << ": "
            << check.detail << "\n";
        }
      }
    }
    std::cout << "{\"fixture\":\"" << outcome.name << "\",\"checks\":" << outcome.straight.size()
      << ",\"bad\":" << countBad(outcome.straight)
      << ",\"mutationRan\":" << (outcome.mutationRan ? "true" : "false")
      << ",\"mutationChecks\":" << outcome.mutated.size()
      << ",\"mutationBad\":" << countBad(outcome.mutated) << "}\n";
  }
  std::cout << "{\"fixtures\":" << cases.size() << ",\"checks\":" << total << ",\"bad\":" << bad
    << ",\"mutations\":" << mutations << ",\"mutationBad\":" << mutationsBad << "}\n";
  if (bad != 0) return 1;
  // A mutation run where nothing moved is a run that proved nothing, so it is a
  // failure rather than a pass with a footnote.
  if (withMutation && mutations == 0) {
    std::cerr << "FAIL no fixture had a mutation to run\n";
    return 1;
  }
  return 0;
}

// ---------------------------------------------------------------------------
// Self-test
// ---------------------------------------------------------------------------
// The original self-test line is left byte-identical on purpose: verify-fast.sh
// pins seven substrings of it, and that script is outside this task's
// write scope. The capability self-test is a second line, so a reader of the
// fast tier's capture sees both without either changing.

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

// The pure half of the capability layer: no BSP, no resources, no GPU. Every
// function here is one the renderer will call, so a wrong face order or a
// direction that resolves to no face is caught here rather than in a picture.
bool capabilitySelfTest() {
  int face = -1;
  float u = 0.0f, v = 0.0f;
  const bool axes = tf2::native::worldCubemapFaceForDirection(1.0f, 0.0f, 0.0f, face, u, v)
    && face == 0 && std::fabs(u - 0.5f) < 1e-6f && std::fabs(v - 0.5f) < 1e-6f;
  const bool negativeX = tf2::native::worldCubemapFaceForDirection(-1.0f, 0.0f, 0.0f, face, u, v)
    && face == 1;
  const bool positiveY = tf2::native::worldCubemapFaceForDirection(0.0f, 1.0f, 0.0f, face, u, v)
    && face == 2;
  const bool negativeY = tf2::native::worldCubemapFaceForDirection(0.0f, -1.0f, 0.0f, face, u, v)
    && face == 3;
  const bool positiveZ = tf2::native::worldCubemapFaceForDirection(0.0f, 0.0f, 1.0f, face, u, v)
    && face == 4;
  const bool negativeZ = tf2::native::worldCubemapFaceForDirection(0.0f, 0.0f, -1.0f, face, u, v)
    && face == 5;
  // The dominant axis wins even when another axis is large: (2,1,0) is still
  // the +X face, and its u,v are inside the face.
  const bool dominant = tf2::native::worldCubemapFaceForDirection(2.0f, 1.0f, 0.0f, face, u, v)
    && face == 0 && u >= 0.0f && u <= 1.0f && v >= 0.0f && v <= 1.0f;
  // A zero or non-finite direction has no dominant axis and must be refused
  // rather than quietly sampling face 0.
  const bool refused = !tf2::native::worldCubemapFaceForDirection(0.0f, 0.0f, 0.0f, face, u, v)
    && !tf2::native::worldCubemapFaceForDirection(NAN, 0.0f, 1.0f, face, u, v);
  std::string path;
  const int origin[3] = {-4771, 2021, -15};
  const int negative[3] = {100, -200, 0};
  const bool samplePath = tf2::native::worldCubemapVtfPath("cp_snakewater_final1", origin, path)
    && path == "materials/maps/cp_snakewater_final1/c-4771_2021_-15.vtf"
    && tf2::native::worldCubemapVtfPath("cp_snakewater_final1", negative, path)
    && path == "materials/maps/cp_snakewater_final1/c100_-200_0.vtf"
    && !tf2::native::worldCubemapVtfPath("", origin, path)
    && !tf2::native::worldCubemapVtfPath("cp_snakewater_final1", nullptr, path);
  const bool suffixes = std::string(tf2::native::worldCubemapFaceSuffix(0)) == "rt"
    && std::string(tf2::native::worldCubemapFaceSuffix(5)) == "dn"
    && tf2::native::worldCubemapFaceSuffix(6) == nullptr
    && tf2::native::worldCubemapFaceSuffix(-1) == nullptr;
  // An empty fixture must parse to an invalid map rather than crash, and the
  // capability layer must call that "unavailable" for every capability.
  tf2::native::BspFixture emptyFixture;
  const auto emptyBytes = tf2::native::buildFixtureBsp(emptyFixture);
  tf2::native::BspMap emptyMap;
  tf2::native::BspParser::parse(emptyBytes, emptyMap, 200000);
  tf2::native::FixedResourceProbe nothing({});
  const auto emptyReadings = tf2::native::measureWorldCapabilities(emptyMap, "empty", nothing,
    false, false, false, false, emptyBytes, 200000);
  const auto emptyLightmap = tf2::native::judgeLightmap(emptyReadings.lightmap);
  const auto emptyCubemap = tf2::native::judgeCubemaps(emptyReadings.cubemap);
  const auto emptySky = tf2::native::judgeSky(emptyReadings.sky);
  const auto emptyDisplacement = tf2::native::judgeDisplacement(emptyReadings.displacement);
  const auto emptyWater = tf2::native::judgeWater(emptyReadings.water);
  const auto emptyVis = tf2::native::judgeVis(emptyReadings.vis);
  const bool emptyStates = emptyLightmap.state == tf2::native::WorldCapabilityState::Unavailable
    && emptyCubemap.state == tf2::native::WorldCapabilityState::Unavailable
    && emptySky.state == tf2::native::WorldCapabilityState::Unavailable
    && emptyDisplacement.state == tf2::native::WorldCapabilityState::Unavailable
    && emptyWater.state == tf2::native::WorldCapabilityState::Unavailable
    && emptyVis.state == tf2::native::WorldCapabilityState::Unavailable;
  const bool ok = axes && negativeX && positiveY && negativeY && positiveZ && negativeZ
    && dominant && refused && samplePath && suffixes && emptyStates;
  std::cout << "{\"capabilitySelfTest\":" << (ok ? "true" : "false")
    << ",\"cubeAxes\":" << (axes ? "true" : "false")
    << ",\"cubeNegativeX\":" << (negativeX ? "true" : "false")
    << ",\"cubePositiveY\":" << (positiveY ? "true" : "false")
    << ",\"cubeNegativeY\":" << (negativeY ? "true" : "false")
    << ",\"cubePositiveZ\":" << (positiveZ ? "true" : "false")
    << ",\"cubeNegativeZ\":" << (negativeZ ? "true" : "false")
    << ",\"cubeDominantAxis\":" << (dominant ? "true" : "false")
    << ",\"cubeZeroRefused\":" << (refused ? "true" : "false")
    << ",\"cubeSamplePath\":" << (samplePath ? "true" : "false")
    << ",\"cubeSuffixes\":" << (suffixes ? "true" : "false")
    << ",\"emptyBspAllUnavailable\":" << (emptyStates ? "true" : "false") << "}\n";
  return ok;
}

// ---------------------------------------------------------------------------
// T2 (2026-10-14): the water surface, as geometry
// ---------------------------------------------------------------------------
// The three properties the task asks of the warp are properties rather than
// values, so they are asserted as properties: bounded, NaN-free, and repeatable
// at a fixed phase with a different answer at a different one. The draw-request
// states are then asserted on the same function the renderer calls, so a green
// line here is a statement about the water pass and not about a second
// implementation written to be tested.
bool waterSelfTest() {
  using tf2::native::WorldWaterDrawRequest;
  using tf2::native::WorldWaterDrawReading;
  using tf2::native::WorldWaterParams;
  using tf2::native::WorldWaterSurfaceState;
  using tf2::native::WorldWaterVertex;

  // Bounded: for every phase and every input in [0,1], the warp may not move a
  // uv by more than the amplitude. Measured over a grid rather than at one
  // point, because a bound that holds at u=0.5 and not at u=0.97 is not a bound.
  const float amplitude = 0.02f;
  bool bounded = true, finite = true;
  float maxDelta = 0.0f;
  for (double phase = 0.0; phase < 6.4; phase += 0.37) {
    for (int i = 0; i <= 40; ++i) {
      const float u = static_cast<float>(i) / 40.0f;
      const float v = 1.0f - u;
      float ou = 0.0f, ov = 0.0f;
      tf2::native::worldWaterWarpUv(u, v, phase, amplitude, ou, ov);
      if (!std::isfinite(ou) || !std::isfinite(ov)) { finite = false; break; }
      maxDelta = std::max(maxDelta, std::fabs(ou - u));
      maxDelta = std::max(maxDelta, std::fabs(ov - v));
      if (std::fabs(ou - u) > amplitude + 1e-6f || std::fabs(ov - v) > amplitude + 1e-6f) {
        bounded = false;
      }
    }
  }
  // NaN-free: a non-finite input or amplitude is passed through unchanged, so a
  // bad uv poisons one sample rather than the whole surface.
  float nu = 0.0f, nv = 0.0f;
  tf2::native::worldWaterWarpUv(NAN, 0.5f, 1.0, amplitude, nu, nv);
  const bool nanIn = std::isnan(nu) && nv == 0.5f;
  tf2::native::worldWaterWarpUv(0.5f, 0.5f, 1.0, NAN, nu, nv);
  const bool nanAmplitude = nu == 0.5f && nv == 0.5f;
  tf2::native::worldWaterWarpUv(0.5f, 0.5f, 1.0, -1.0f, nu, nv);
  const bool negativeAmplitude = nu == 0.5f && nv == 0.5f;
  // Repeatable at a fixed phase, and different at a different one. The second
  // half is what makes `water-on == water-repeat` a statement about the warp
  // rather than about a constant.
  float a1u = 0.0f, a1v = 0.0f, a2u = 0.0f, a2v = 0.0f, b1u = 0.0f, b1v = 0.0f;
  tf2::native::worldWaterWarpUv(0.3f, 0.7f, 0.0, amplitude, a1u, a1v);
  tf2::native::worldWaterWarpUv(0.3f, 0.7f, 0.0, amplitude, a2u, a2v);
  tf2::native::worldWaterWarpUv(0.3f, 0.7f, 0.37, amplitude, b1u, b1v);
  const bool repeatable = a1u == a2u && a1v == a2v;
  const bool phaseMoves = a1u != b1u || a1v != b1v;
  // Zero amplitude is the identity: the switch that turns the warp off must
  // leave the uvs alone rather than warping by zero in a way that is not zero.
  float zu = 0.0f, zv = 0.0f;
  tf2::native::worldWaterWarpUv(0.3f, 0.7f, 1.0, 0.0f, zu, zv);
  const bool zeroIsIdentity = zu == 0.3f && zv == 0.7f;

  // The draw request states.
  const auto quad = [](int count) {
    std::vector<WorldWaterVertex> vertices;
    for (int i = 0; i < count; ++i) {
      WorldWaterVertex v;
      v.u = 0.25f * static_cast<float>(i % 3);
      v.v = 0.5f;
      vertices.push_back(v);
    }
    return vertices;
  };
  WorldWaterParams params;
  params.declared = true;
  params.reason = "fixture";

  WorldWaterDrawRequest off;
  off.enabled = false;
  off.vertices = quad(6);
  off.params = params;
  off.textureResolved = true;
  std::vector<WorldWaterVertex> out;
  const auto offReading = tf2::native::evaluateWaterDrawRequest(off, out);
  const bool offState = offReading.state == WorldWaterSurfaceState::Unavailable
    && offReading.waterDrawCalls == 0 && out.empty()
    && offReading.reason.find("disabled by the caller") != std::string::npos;

  WorldWaterDrawRequest empty;
  empty.enabled = true;
  empty.params = params;
  std::vector<WorldWaterVertex> emptyOut;
  const auto emptyReading = tf2::native::evaluateWaterDrawRequest(empty, emptyOut);
  const bool emptyState = emptyReading.state == WorldWaterSurfaceState::Unavailable
    && emptyReading.waterTriangles == 0
    && emptyReading.reason.find("no water geometry") != std::string::npos;

  WorldWaterDrawRequest partial;
  partial.enabled = true;
  partial.vertices = quad(4);
  partial.params = params;
  partial.textureResolved = true;
  std::vector<WorldWaterVertex> partialOut;
  const auto partialReading = tf2::native::evaluateWaterDrawRequest(partial, partialOut);
  const bool partialState = partialReading.state == WorldWaterSurfaceState::Unavailable
    && partialReading.reason.find("whole number of triangles") != std::string::npos;

  WorldWaterDrawRequest noMaterial;
  noMaterial.enabled = true;
  noMaterial.vertices = quad(6);
  noMaterial.textureResolved = false;
  std::vector<WorldWaterVertex> noMaterialOut;
  const auto noMaterialReading = tf2::native::evaluateWaterDrawRequest(noMaterial, noMaterialOut);
  // Six vertices is one BSP face (two triangles), which is the arithmetic the
  // real map confirms. The fallback is a DRAWN state: the surface is in the
  // picture with the flat shade, which is a different fact from "nothing was
  // drawn".
  const bool fallbackState = noMaterialReading.state == WorldWaterSurfaceState::Fallback
    && noMaterialReading.waterFallback && noMaterialReading.waterDrawCalls == 1
    && noMaterialReading.waterVisible && noMaterialReading.waterFaces == 1
    && noMaterialReading.waterTriangles == 2;

  WorldWaterDrawRequest good;
  good.enabled = true;
  good.vertices = quad(6);
  good.params = params;
  good.textureResolved = true;
  good.warpAmplitude = amplitude;
  good.warpPhase = 0.37;
  std::vector<WorldWaterVertex> goodOut;
  const auto goodReading = tf2::native::evaluateWaterDrawRequest(good, goodOut);
  // One face and two triangles from six vertices, the warp applied, and the
  // vertices handed out are the warped ones -- the renderer uploads what this
  // function counted.
  bool warpedInOutput = false;
  for (std::size_t i = 0; i < goodOut.size() && i < good.vertices.size(); ++i) {
    if (goodOut[i].u != good.vertices[i].u || goodOut[i].v != good.vertices[i].v) {
      warpedInOutput = true;
      break;
    }
  }
  const bool activeState = goodReading.state == WorldWaterSurfaceState::Active
    && goodReading.waterWarpApplied && goodReading.waterFaces == 1
    && goodReading.waterTriangles == 2 && goodReading.waterDrawCalls == 1
    && !goodReading.waterFallback && warpedInOutput
    && goodReading.waterReflectionState == WorldWaterSurfaceState::Unavailable
    && goodReading.waterRefractionState == WorldWaterSurfaceState::Unavailable;

  // The material reader, on the text a TF2 water material actually carries.
  const std::string waterVmt =
    "Water\n{\n"
    "  \"$abovewater\" \"0\"\n"
    "  \"$bumpmap\" \"water/test_water_normal\"\n"
    "  \"$bottommaterial\" \"water/test_beneath\"\n"
    "  \"$reflectivity\" \"[0.6 0.6 0.6]\"\n"
    "}\n";
  const auto waterParams = tf2::native::measureWaterParams(waterVmt, "water/test_water.vmt");
  const bool paramsOk = waterParams.declared
    && waterParams.bumpMap == "water/test_water_normal"
    && waterParams.bottomMaterial == "water/test_beneath"
    && std::fabs(waterParams.reflectivity - 0.6f) < 1e-4f
    && waterParams.reason.find("parsed") != std::string::npos;
  const auto badParams = tf2::native::measureWaterParams("Water\n{\n", "broken.vmt");
  const bool badParamsOk = !badParams.declared
    && badParams.reason.find("did not parse") != std::string::npos;

  const bool ok = bounded && finite && nanIn && nanAmplitude && negativeAmplitude
    && repeatable && phaseMoves && zeroIsIdentity && offState && emptyState
    && partialState && fallbackState && activeState && paramsOk && badParamsOk;
  std::cout << "{\"waterSelfTest\":" << (ok ? "true" : "false")
    << ",\"warpBounded\":" << (bounded ? "true" : "false")
    << ",\"warpFinite\":" << (finite ? "true" : "false")
    << ",\"warpMaxDelta\":" << maxDelta
    << ",\"warpNanIn\":" << (nanIn ? "true" : "false")
    << ",\"warpNanAmplitude\":" << (nanAmplitude ? "true" : "false")
    << ",\"warpNegativeAmplitude\":" << (negativeAmplitude ? "true" : "false")
    << ",\"warpRepeatable\":" << (repeatable ? "true" : "false")
    << ",\"warpPhaseMoves\":" << (phaseMoves ? "true" : "false")
    << ",\"warpZeroIdentity\":" << (zeroIsIdentity ? "true" : "false")
    << ",\"waterOffState\":" << (offState ? "true" : "false")
    << ",\"waterEmptyState\":" << (emptyState ? "true" : "false")
    << ",\"waterPartialState\":" << (partialState ? "true" : "false")
    << ",\"waterFallbackState\":" << (fallbackState ? "true" : "false")
    << ",\"waterActiveState\":" << (activeState ? "true" : "false")
    << ",\"waterParams\":" << (paramsOk ? "true" : "false")
    << ",\"waterParamsBad\":" << (badParamsOk ? "true" : "false") << "}\n";
  return ok;
}

// The water fixtures, in the same flat key space the capability fixtures use so
// the gate asserts one vocabulary. A positive case resolves; the negative cases
// are the ones the task names: no material, a partial buffer, a switch that was
// turned off, and a malformed water VMT.
struct WaterFixtureCase {
  std::string name;
  bool enabled = true;
  int vertices = 6;
  bool textureResolved = true;
  bool materialDeclared = true;
  std::string expectState;
  std::size_t expectFaces = 0;
  std::size_t expectTriangles = 0;
  std::size_t expectDraws = 0;
  bool expectFallback = false;
  bool expectWarp = false;
};

int runWaterFixtures() {
  // A "face" is one BSP face = two triangles = six vertices, which is the
  // arithmetic the real map confirms (36 triangles from 18 faces on
  // cp_snakewater_final1). Twelve vertices is therefore two faces and four
  // triangles, and the three numbers cannot be confused for one another.
  const std::vector<WaterFixtureCase> cases = {
    {"resolved",            true,  12, true,  true,  "active",      2, 4, 1, false, true},
    {"no-material",         true,  12, false, false, "fallback",    2, 4, 1, true,  false},
    {"texture-unresolved",  true,  12, false, true,  "fallback",    2, 4, 1, true,  false},
    {"partial-buffer",      true,  4,  true,  true,  "unavailable", 0, 0, 0, false, false},
    {"no-geometry",         true,  0,  true,  true,  "unavailable", 0, 0, 0, false, false},
    {"switch-off",          false, 12, true,  true,  "unavailable", 0, 0, 0, false, false},
  };
  std::size_t checks = 0, bad = 0;
  for (const auto& c : cases) {
    tf2::native::WorldWaterDrawRequest request;
    request.enabled = c.enabled;
    request.textureResolved = c.textureResolved;
    request.params.declared = c.materialDeclared;
    request.params.reason = "fixture";
    request.warpAmplitude = 0.02f;
    request.warpPhase = 0.0;
    for (int i = 0; i < c.vertices; ++i) {
      tf2::native::WorldWaterVertex v;
      v.u = 0.25f * static_cast<float>(i % 3);
      v.v = 0.5f;
      request.vertices.push_back(v);
    }
    std::vector<tf2::native::WorldWaterVertex> out;
    const auto reading = tf2::native::evaluateWaterDrawRequest(request, out);
    const auto expect = [&](const char* key, const std::string& got, const std::string& want) {
      ++checks;
      if (got != want) {
        std::cout << "{\"waterFixture\":\"" << c.name << "\",\"check\":\"" << key
          << "\",\"got\":\"" << got << "\",\"want\":\"" << want << "\"}\n";
        ++bad;
      }
    };
    expect("state", tf2::native::worldWaterSurfaceStateName(reading.state), c.expectState);
    expect("faces", std::to_string(reading.waterFaces), std::to_string(c.expectFaces));
    expect("triangles", std::to_string(reading.waterTriangles),
      std::to_string(c.expectTriangles));
    expect("drawCalls", std::to_string(reading.waterDrawCalls), std::to_string(c.expectDraws));
    expect("fallback", reading.waterFallback ? "1" : "0", c.expectFallback ? "1" : "0");
    expect("warpApplied", reading.waterWarpApplied ? "1" : "0", c.expectWarp ? "1" : "0");
    // The output buffer is the contract the renderer relies on: it must hold
    // exactly the triangles the reading counted, or the pass draws something the
    // gate never measured.
    expect("outputVertices", std::to_string(out.size()),
      std::to_string(c.expectTriangles * 3u));
  }
  std::cout << "{\"waterFixtures\":" << cases.size() << ",\"checks\":" << checks
    << ",\"bad\":" << bad << "}\n";
  return bad == 0 ? 0 : 1;
}

// ---------------------------------------------------------------------------
// The third self-test line: the skybox as geometry, and the pvs clip
// ---------------------------------------------------------------------------
// Both entry points are the ones the product calls -- evaluateSkyDrawRequest
// builds the box the renderer uploads, clipWorldTrianglesByVis decides the
// indices the renderer draws -- so a green line here is a statement about the
// drawing and not about a second implementation of it written to be tested.
bool worldDrawSelfTest() {
  using tf2::native::WorldSkyDrawRequest;
  using tf2::native::WorldSkyFace;
  using tf2::native::WorldSkyState;
  using tf2::native::WorldSkyVertex;
  using tf2::native::WorldVisClipRequest;
  using tf2::native::WorldVisClipState;
  using tf2::native::BspTriangle;
  using tf2::native::BspMap;

  const auto side = [](std::uint32_t width, std::uint32_t height) {
    WorldSkyFace face;
    face.width = width;
    face.height = height;
    face.rgba.assign(static_cast<std::size_t>(width) * height * 4u, 0xC0u);
    return face;
  };
  WorldSkyDrawRequest full;
  full.enabled = true;
  for (auto& face : full.faces) face = side(8, 8);
  std::vector<WorldSkyVertex> vertices;
  const auto fullReading = tf2::native::evaluateSkyDrawRequest(full, vertices);
  // A box is 36 vertices and 12 triangles, six per side, and the sides are the
  // ones the cube order names: side 0 is +X (rt), side 1 is -X (lf).
  const bool box = fullReading.state == WorldSkyState::Active
    && fullReading.skyboxVertices == 36 && fullReading.skyboxTriangles == 12
    && fullReading.skyboxFaces == 6 && fullReading.skyboxDraws == 1
    && vertices.size() == 36;
  bool planes = true;
  for (int index = 0; index < 6; ++index) {
    planes = planes && std::fabs(vertices[static_cast<std::size_t>(index)].x - full.halfExtent) < 1e-4f;
    planes = planes && std::fabs(vertices[static_cast<std::size_t>(6 + index)].x + full.halfExtent) < 1e-4f;
  }
  bool uvs = true;
  for (int face = 0; face < 6; ++face) {
    const auto base = static_cast<std::size_t>(face) * 6u;
    uvs = uvs && vertices[base + 0].u == 0.0f && vertices[base + 0].v == 0.0f
      && vertices[base + 1].u == 1.0f && vertices[base + 1].v == 0.0f
      && vertices[base + 2].u == 1.0f && vertices[base + 2].v == 1.0f
      && vertices[base + 3].u == 0.0f && vertices[base + 3].v == 0.0f
      && vertices[base + 4].u == 1.0f && vertices[base + 4].v == 1.0f
      && vertices[base + 5].u == 0.0f && vertices[base + 5].v == 1.0f;
  }
  WorldSkyDrawRequest partial = full;
  partial.faces[1] = WorldSkyFace{};
  partial.faces[3] = WorldSkyFace{};
  std::vector<WorldSkyVertex> partialVertices;
  const auto partialReading = tf2::native::evaluateSkyDrawRequest(partial, partialVertices);
  // A partial sky is drawn as the sides that exist, and the reason names how
  // many: "4 of 6" is the fallback, and a gate that could only read "fallback"
  // would not be able to tell four sides from one.
  const bool partialFallback = partialReading.state == WorldSkyState::Fallback
    && partialReading.skyboxVertices == 24 && partialReading.skyboxFaces == 4
    && partialReading.reason.find("4 of 6") != std::string::npos;
  WorldSkyDrawRequest empty = full;
  for (auto& face : empty.faces) face = WorldSkyFace{};
  std::vector<WorldSkyVertex> emptyVertices;
  const auto emptyReading = tf2::native::evaluateSkyDrawRequest(empty, emptyVertices);
  const bool noSides = emptyReading.state == WorldSkyState::Unavailable
    && emptyReading.skyboxVertices == 0 && emptyReading.skyboxDraws == 0
    && emptyReading.reason.find("no sky side decoded") != std::string::npos;
  WorldSkyDrawRequest off = full;
  off.enabled = false;
  std::vector<WorldSkyVertex> offVertices;
  const auto offReading = tf2::native::evaluateSkyDrawRequest(off, offVertices);
  const bool disabled = offReading.state == WorldSkyState::Unavailable
    && offReading.skyboxVertices == 0
    && offReading.reason.find("disabled by the caller") != std::string::npos;

  // The clip, on a fixture whose three faces sit in three clusters and whose
  // vis lump says cluster 0 sees clusters 0 and 1 only. The numbers below are
  // therefore derived from the pvs and not from the triangle count alone: two
  // of six triangles survive the clip at cluster 0, and four at cluster 2.
  tf2::native::BspFixture clipFixture;
  clipFixture.clusterCount = 3;
  clipFixture.visBits = {3u, 3u, 4u};
  // The four luxels every face's 2x2 box reads, so the faces really are
  // lightmapped and the parse below also has an atlas to report on.
  tf2::native::appendLuxel(clipFixture.lighting, 200, 100, 50, 0);
  tf2::native::appendLuxel(clipFixture.lighting, 10, 20, 30, 1);
  tf2::native::appendLuxel(clipFixture.lighting, 255, 255, 255, 0);
  tf2::native::appendLuxel(clipFixture.lighting, 0, 0, 0, 0);
  clipFixture.faces.push_back(lightmapQuad());
  clipFixture.faces.push_back(lightmapQuad());
  clipFixture.faces.push_back(lightmapQuad());
  clipFixture.leaves.push_back(tf2::native::BspFixture::Leaf{0, {0}});
  clipFixture.leaves.push_back(tf2::native::BspFixture::Leaf{1, {1}});
  clipFixture.leaves.push_back(tf2::native::BspFixture::Leaf{2, {2}});
  const auto clipBytes = tf2::native::buildFixtureBsp(clipFixture);
  BspMap clipMap;
  tf2::native::BspParser::parse(clipBytes, clipMap);
  std::vector<BspTriangle> kept;
  std::vector<std::size_t> keptIndices;
  WorldVisClipRequest clipRequest;
  clipRequest.enabled = true;
  clipRequest.cameraCluster = 0;
  const auto clip = tf2::native::clipWorldTrianglesByVis(clipMap, clipMap.triangles,
    clipRequest, kept, &keptIndices);
  const bool clipHides = clip.state == WorldVisClipState::Active && clip.clipApplied
    && clip.inputTriangles == 6 && clip.outputTriangles == 4 && clip.hiddenTriangles == 2
    && clip.trianglesWithoutCluster == 0 && clip.cameraCluster == 0;
  // The indices are what the renderer turns into a draw list, so they are the
  // half of this that can be wrong while the counts are right.
  const bool clipIndices = keptIndices.size() == kept.size() && keptIndices.size() == 4
    && keptIndices[0] == 0 && keptIndices[1] == 1 && keptIndices[2] == 2 && keptIndices[3] == 3;
  clipRequest.cameraCluster = 2;
  std::vector<BspTriangle> keptFromTwo;
  const auto clipFromTwo = tf2::native::clipWorldTrianglesByVis(clipMap, clipMap.triangles,
    clipRequest, keptFromTwo);
  const bool clipFollowsCamera = clipFromTwo.state == WorldVisClipState::Active
    && clipFromTwo.outputTriangles == 2 && clipFromTwo.hiddenTriangles == 4;
  clipRequest.enabled = false;
  std::vector<BspTriangle> keptWhenOff;
  std::vector<std::size_t> indicesWhenOff;
  const auto clipOff = tf2::native::clipWorldTrianglesByVis(clipMap, clipMap.triangles,
    clipRequest, keptWhenOff, &indicesWhenOff);
  const bool clipDisabled = clipOff.state == WorldVisClipState::Disabled
    && !clipOff.clipApplied && clipOff.outputTriangles == 6 && keptWhenOff.size() == 6
    && indicesWhenOff.size() == 6;
  clipRequest.enabled = true;
  clipRequest.cameraCluster = -1;
  std::vector<BspTriangle> keptNoCamera;
  const auto clipNoCamera = tf2::native::clipWorldTrianglesByVis(clipMap, clipMap.triangles,
    clipRequest, keptNoCamera);
  const bool clipNeedsCamera = clipNoCamera.state == WorldVisClipState::Unavailable
    && !clipNoCamera.clipApplied && clipNoCamera.outputTriangles == 6
    && clipNoCamera.reason.find("no named cluster") != std::string::npos;
  tf2::native::BspFixture noVisFixture;
  noVisFixture.faces.push_back(lightmapQuad());
  const auto noVisBytes = tf2::native::buildFixtureBsp(noVisFixture);
  BspMap noVisMap;
  tf2::native::BspParser::parse(noVisBytes, noVisMap);
  std::vector<BspTriangle> keptNoPvs;
  const auto clipNoPvs = tf2::native::clipWorldTrianglesByVis(noVisMap, noVisMap.triangles,
    clipRequest, keptNoPvs);
  const bool clipWithoutPvs = clipNoPvs.state == WorldVisClipState::Unavailable
    && !clipNoPvs.clipApplied && clipNoPvs.outputTriangles == noVisMap.triangles.size()
    && clipNoPvs.reason.find("no cluster pvs") != std::string::npos;

  // The atlas: the shelf width is chosen now, and the choice is what turns "this
  // map loses faces" from a subtraction into a behaviour. The same seventeen
  // 512x512 faces are parsed twice, at the historical 2048 cap and at the
  // product's 4096 -- one drops a face and names it, the other fits all
  // seventeen and says so. A single parse could not tell a fixed packer from one
  // that never had anything to drop.
  const auto bigFace = []() {
    tf2::native::BspFixtureFace face;
    const float corners[4][3] = {{0.0f, 0.0f, 0.0f}, {511.0f, 0.0f, 0.0f},
      {511.0f, 511.0f, 0.0f}, {0.0f, 511.0f, 0.0f}};
    for (int index = 0; index < 4; ++index) {
      for (int axis = 0; axis < 3; ++axis) face.corners[index][axis] = corners[index][axis];
    }
    face.minsU = 0;
    face.minsV = 0;
    face.samplesW = 511;  // stored as value-1, so the box is 512x512
    face.samplesH = 511;
    face.lightOffset = 0;
    face.style0 = 0;
    return face;
  };
  tf2::native::BspFixture capacity;
  capacity.lighting.assign(512u * 512u * 4u, 0x40);
  for (int index = 0; index < 17; ++index) capacity.faces.push_back(bigFace());
  const auto capacityBytes = tf2::native::buildFixtureBsp(capacity);
  BspMap atlasAt2048;
  tf2::native::BspParser::parse(capacityBytes, atlasAt2048, 200000, 2048);
  BspMap atlasAt4096;
  tf2::native::BspParser::parse(capacityBytes, atlasAt4096, 200000, 4096);
  const bool atlasGrows = atlasAt2048.lightmapAtlasWidth == 2048
    && atlasAt2048.lightmapAtlasHeight == 2048
    && atlasAt2048.lightmapPackedFaces == 16 && atlasAt2048.lightmapAtlasDroppedFaces == 1
    && atlasAt2048.lightmapAtlasReason == "1 of 17 faces did not fit the 2048-wide atlas"
    && atlasAt4096.lightmapAtlasWidth == 4096 && atlasAt4096.lightmapAtlasHeight == 1536
    && atlasAt4096.lightmapPackedFaces == 17 && atlasAt4096.lightmapAtlasDroppedFaces == 0
    && atlasAt4096.lightmapAtlasGrown
    && atlasAt4096.lightmapAtlasReason == "all 17 faces fit the 4096-wide atlas";
  const bool atlasReason = tf2::native::BspParser::parse(clipBytes, clipMap, 200000, 2048)
    && clipMap.lightmapAtlasDroppedFaces == 0
    && clipMap.lightmapAtlasReason.find("did not fit") == std::string::npos
    && !clipMap.lightmapAtlasGrown
    && clipMap.lightmapAtlasWidth == 2048;

  const bool ok = box && planes && uvs && partialFallback && noSides && disabled
    && clipHides && clipIndices && clipFollowsCamera && clipDisabled && clipNeedsCamera
    && clipWithoutPvs && atlasReason && atlasGrows;
  std::cout << "{\"worldDrawSelfTest\":" << (ok ? "true" : "false")
    << ",\"skyBoxVertices\":" << (box ? "true" : "false")
    << ",\"skyBoxPlanes\":" << (planes ? "true" : "false")
    << ",\"skyBoxUvs\":" << (uvs ? "true" : "false")
    << ",\"skyPartialFallback\":" << (partialFallback ? "true" : "false")
    << ",\"skyNoSidesReason\":" << (noSides ? "true" : "false")
    << ",\"skyDisabledReason\":" << (disabled ? "true" : "false")
    << ",\"visClipHides\":" << (clipHides ? "true" : "false")
    << ",\"visClipIndices\":" << (clipIndices ? "true" : "false")
    << ",\"visClipFollowsCamera\":" << (clipFollowsCamera ? "true" : "false")
    << ",\"visClipDisabled\":" << (clipDisabled ? "true" : "false")
    << ",\"visClipNeedsCamera\":" << (clipNeedsCamera ? "true" : "false")
    << ",\"visClipWithoutPvs\":" << (clipWithoutPvs ? "true" : "false")
    << ",\"atlasFitsAt2048\":" << (atlasReason ? "true" : "false")
    << ",\"atlasGrowsWhenFull\":" << (atlasGrows ? "true" : "false") << "}\n";
  return ok;
}

} // namespace

int main(int argc, char** argv) {
  bool runSelf = argc <= 1;
  bool runFixturesFlag = false;
  bool runWaterFixturesFlag = false;
  bool withMutation = false;
  std::vector<std::filesystem::path> bspPaths;
  std::filesystem::path tfRoot;
  std::string mapName;
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg = argv[i];
    if (arg == "--self-test") { runSelf = true; continue; }
    if (arg == "--fixture") { runFixturesFlag = true; continue; }
    if (arg == "--water-fixture") { runWaterFixturesFlag = true; continue; }
    if (arg == "--mutation") { withMutation = true; continue; }
    if (arg == "--bsp" && i + 1 < argc) {
      bspPaths.push_back(std::filesystem::u8path(argv[++i]));
      continue;
    }
    if (arg == "--tf-root" && i + 1 < argc) {
      tfRoot = std::filesystem::u8path(argv[++i]);
      continue;
    }
    if (arg == "--map-name" && i + 1 < argc) {
      mapName = argv[++i];
      continue;
    }
    std::cerr << "usage: world_material_probe [--self-test] [--fixture [--mutation]]"
      " [--bsp map.bsp [--tf-root dir] [--map-name name]]\n";
    return 2;
  }
  int status = 0;
  if (runSelf) {
    if (!selfTest()) status = 1;
    if (!capabilitySelfTest()) status = 1;
    if (!worldDrawSelfTest()) status = 1;
    if (!waterSelfTest()) status = 1;
  }
  if (runFixturesFlag) {
    if (runFixtures(withMutation) != 0) status = 1;
  }
  if (runWaterFixturesFlag) {
    if (runWaterFixtures() != 0) status = 1;
  }
  for (const auto& path : bspPaths) {
    const auto bytes = readFile(path);
    tf2::native::BspMap map;
    tf2::native::BspParser::parse(bytes, map, 200000);
    printMap(path, map);
    // The readings are printed even when the map is invalid, because an
    // unreadable BSP is exactly the case the capability layer has to describe:
    // every capability unavailable, each with the reason it is unavailable.
    // checkMap's verdict is reported afterwards and separately, so a gate can
    // assert on the readings of a map it knows is broken.
    std::string error;
    const bool mapOk = checkMap(map, path, error);
    if (!mapOk) std::cerr << path.filename().string() << " failed: " << error << "\n";
    const std::string name = mapName.empty() ? path.stem().string() : mapName;
    tf2::native::FixedResourceProbe nothing({});
    // Held by pointer, not by value: copying a FixedResourceProbe into a
    // WorldResourceProbe would slice it and answer every lookup with the base
    // class's behaviour.
    const tf2::native::WorldResourceProbe* resourceProbe = &nothing;
    std::unique_ptr<tf2::native::WorldResourceProbe> held;
    std::unique_ptr<tf2::native::VpkArchiveSet> archives;
    if (!tfRoot.empty()) {
      archives = std::make_unique<tf2::native::VpkArchiveSet>(
        tf2::native::VpkArchiveSet::openDirectory(tfRoot));
      held = std::make_unique<TfResourceProbe>(tfRoot, *archives);
      resourceProbe = held.get();
    }
    // Water is the one capability whose declaration lives in a VMT rather than
    // in the BSP, so it is resolved before the shared measurement and handed in.
    bool waterMaterialDeclared = false;
    bool waterTextureReachable = false;
    std::string waterReason = "no tf root was given, so no material could be resolved";
    if (archives) {
      const auto water = resolveWaterMaterial(map, tfRoot, *archives);
      waterMaterialDeclared = water.declared;
      waterTextureReachable = water.textureReachable;
      waterReason = water.reason;
    }
    const auto readings = tf2::native::measureWorldCapabilities(map, name, *resourceProbe,
      false, false, waterMaterialDeclared, waterTextureReachable, bytes, 200000);
    const auto flat = flattenReadings(readings);
    for (const auto& [key, value] : flat) {
      std::cout << "{\"reading\":\"" << key << "\",\"value\":\"" << value << "\"}\n";
    }
    std::cout << "{\"reading\":\"water.materialResolution\",\"value\":\"" << waterReason << "\"}\n";
    if (!mapOk) status = 1;
  }
  return status;
}
