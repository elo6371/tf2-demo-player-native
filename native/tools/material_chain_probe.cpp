#include "bsp_map.h"
#include "vmt_material.h"
#include "vpk_archive.h"
#include "vtf_texture.h"
#include "vmt_material.h"

#include <filesystem>
#include <fstream>
#include <algorithm>
#include <iostream>
#include <cstring>
#include <cctype>
#include <map>
#include <memory>
#include <set>
#include <string_view>
#include <vector>

std::vector<std::uint8_t> readFile(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
}

int main(int argc, char** argv) {
  tf2::native::VtfTexture vtf;
  std::string error;
  const bool emptyRejected = !vtf.parse({}, &error);
  const bool corruptRejected = !vtf.parse({'V', 'T', 'F', 0, 7, 0, 0, 0}, &error);
  std::vector<std::uint8_t> oversizedVtf(64u * 1024u * 1024u + 1u, 0);
  oversizedVtf[0] = 'V'; oversizedVtf[1] = 'T'; oversizedVtf[2] = 'F';
  const bool oversizedRejected = !vtf.parse(oversizedVtf, &error);
  std::vector<std::uint8_t> cubemapVtf(128u, 0);
  std::memcpy(cubemapVtf.data(), "VTF\0", 4);
  const auto put16 = [&cubemapVtf](std::size_t offset, std::uint16_t value) {
    cubemapVtf[offset] = static_cast<std::uint8_t>(value);
    cubemapVtf[offset + 1] = static_cast<std::uint8_t>(value >> 8);
  };
  const auto put32 = [&cubemapVtf](std::size_t offset, std::uint32_t value) {
    for (std::size_t byte = 0; byte < 4; ++byte) cubemapVtf[offset + byte] = static_cast<std::uint8_t>(value >> (byte * 8));
  };
  put32(4, 7); put32(8, 2); put32(12, 80); put16(16, 4); put16(18, 4);
  put32(20, tf2::native::VtfHeader::TextureFlagsEnvMap); put16(24, 1);
  put32(52, 13); cubemapVtf[56] = 1; put16(63, 6);
  for (std::size_t face = 0; face < 6; ++face) cubemapVtf[80 + face * 8] = static_cast<std::uint8_t>(face + 1);
  const bool cubemapParsed = vtf.parse(cubemapVtf, &error);
  const auto cubemapTopMip = cubemapParsed ? vtf.topMip(cubemapVtf) : std::vector<std::uint8_t>{};
  bool cubemapFaceDataOrdered = cubemapParsed && cubemapTopMip.size() == 48;
  for (std::size_t face = 0; cubemapFaceDataOrdered && face < 6; ++face) {
    cubemapFaceDataOrdered = cubemapTopMip[face * 8] == static_cast<std::uint8_t>(face + 1);
  }
  const bool cubemapDecodeRejected = cubemapParsed
    && vtf.decodeRgba(cubemapVtf, &error).empty();
  tf2::native::BspMap map;
  const bool emptyBspRejected = !tf2::native::BspParser::parse({}, map);
  const bool noLightingDefault = map.lightmapSampleCount == 0 && map.lightmapIntensity == 1.0f;
  tf2::native::VmtMaterial syntheticMaterial;
  const std::string syntheticVmt = R"(VertexLitGeneric { "$bumpmap" "materials\\test\\normal" "$envmap" "env_cubemap" "$selfillum" "1" })";
  const bool vmtFeatureMapping = tf2::native::VmtParser::parse(syntheticVmt, syntheticMaterial)
    && !syntheticMaterial.bumpMap.empty()
    && syntheticMaterial.envMap == "env_cubemap"
    && syntheticMaterial.selfIllum
    && tf2::native::VmtParser::resourcePath(syntheticMaterial.bumpMap, ".vtf") == "materials/test/normal.vtf";
  tf2::native::VmtMaterial waterMaterial;
  const bool waterFeatureMapping = tf2::native::VmtParser::parse(
    R"(Water { "$bottommaterial" "water/bottom" "$underwateroverlay" "effects/water_warp" })", waterMaterial)
    && waterMaterial.waterShader && waterMaterial.waterBottomMaterial == "water/bottom"
    && waterMaterial.underwaterOverlay == "effects/water_warp";
  bool realBumpDecoded = false;
  bool realEnvDecoded = false;
  std::size_t realMaterials = 0;
  std::size_t bumpDeclared = 0;
  std::size_t envDeclared = 0;
  std::size_t realVmtCandidates = 0;
  bool archiveOpen = false;
  std::size_t archiveCount = 0;
  std::size_t bspCubemapEntities = 0;
  std::size_t cubemapFaceFiles = 0;
  std::size_t cubemapCompleteSets = 0;
  std::size_t cubemapIncompleteSets = 0;
  std::size_t realCubemapVtfFlags = 0;
  std::size_t realCubemapHighResResources = 0;
  std::size_t realCubemapResourceTags = 0;
  std::size_t realCubemapDepth6 = 0;
  std::uint16_t realCubemapMaxDepth = 0;
  std::uint16_t realCubemapMaxFrames = 0;
  std::size_t allVtfCandidates = 0;
  std::size_t allCubemapVtfFlags = 0;
  std::size_t allCubemapDepth6 = 0;
  std::uint16_t allCubemapMaxDepth = 0;
  std::size_t cubemapParseFailures = 0;
  std::size_t cubemapOversized = 0;
  bool explicitVtfDecoded = false;
  std::uint16_t explicitVtfWidth = 0, explicitVtfHeight = 0;
  std::uint32_t explicitVtfFormat = 0;
  std::size_t explicitVtfRgbaBytes = 0;
  bool explicitVmtParsed = false;
  std::string explicitVmtShader;
  std::string explicitVmtBump;
  std::string explicitVmtEnv;
  bool explicitVmtSelfIllum = false;
  if (argc == 3 && std::string_view(argv[1]) == "--vtf") {
    const auto bytes = readFile(std::filesystem::u8path(argv[2]));
    tf2::native::VtfTexture texture;
    if (!bytes.empty() && texture.parse(bytes) && !texture.header().isCubemap()) {
      const auto rgba = texture.decodeRgba(bytes);
      explicitVtfDecoded = !rgba.empty();
      explicitVtfWidth = texture.header().width;
      explicitVtfHeight = texture.header().height;
      explicitVtfFormat = texture.header().format;
      explicitVtfRgbaBytes = rgba.size();
    }
  }
  if (argc == 3 && std::string_view(argv[1]) == "--vmt") {
    const auto bytes = readFile(std::filesystem::u8path(argv[2]));
    tf2::native::VmtMaterial material;
    explicitVmtParsed = !bytes.empty()
      && tf2::native::VmtParser::parse(std::string(bytes.begin(), bytes.end()), material);
    if (explicitVmtParsed) {
      explicitVmtShader = material.shader;
      explicitVmtBump = material.bumpMap;
      explicitVmtEnv = material.envMap;
      explicitVmtSelfIllum = material.selfIllum;
    }
  }
  if (argc == 3 && std::string_view(argv[1]) == "--bsp") {
    const auto bytes = readFile(std::filesystem::u8path(argv[2]));
    tf2::native::BspMap parsedMap;
    const bool parsed = tf2::native::BspParser::parse(bytes, parsedMap);
    constexpr std::string_view marker = "env_cubemap";
    for (std::size_t at = 0; at + marker.size() <= bytes.size(); ++at) {
      if (std::memcmp(bytes.data() + at, marker.data(), marker.size()) == 0) ++bspCubemapEntities;
    }
    std::cout << "{\"bspParse\":" << (parsed ? "true" : "false")
      << ",\"bspVersion\":" << parsedMap.version
      << ",\"bspTriangles\":" << parsedMap.triangleCount
      << ",\"bspLightmapFaces\":" << parsedMap.lightmapFaceCount
      << ",\"bspLightmapBytes\":" << parsedMap.lightmapBytes
      << ",\"bspError\":\"" << parsedMap.error << "\"}\n";
  }
  if (argc > 1 && std::string_view(argv[1]) != "--vtf" && std::string_view(argv[1]) != "--vmt" && std::string_view(argv[1]) != "--bsp") {
    const std::filesystem::path tfRoot = std::filesystem::u8path(argv[1]);
    std::vector<std::shared_ptr<tf2::native::VpkArchive>> archives;
    std::error_code rootError;
    if (std::filesystem::is_directory(tfRoot, rootError) && !rootError) {
      for (const auto& entry : std::filesystem::directory_iterator(tfRoot, rootError)) {
        if (rootError) break;
        const auto name = entry.path().filename().string();
        if (!entry.is_regular_file() || name.size() < 8 || name.find("_dir.vpk") != name.size() - 8) continue;
        auto archive = std::make_shared<tf2::native::VpkArchive>();
        if (archive->open(entry.path())) archives.push_back(std::move(archive));
      }
    }
    archiveOpen = !archives.empty();
    archiveCount = archives.size();
    std::set<std::string> cubemapPaths;
    const auto collectCubemap = [&](const std::vector<std::string>& paths) {
      for (const auto& path : paths) {
        std::string normalized = path;
        std::transform(normalized.begin(), normalized.end(), normalized.begin(),
          [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
        if (normalized.find("materials/cubemaps/") == std::string::npos) continue;
        cubemapPaths.insert(normalized);
      }
    };
    for (const auto& archive : archives) {
      const auto vtfPaths = archive->list("materials/cubemaps", ".vtf");
      collectCubemap(vtfPaths);
    }
    const auto looseCubemapRoot = tfRoot / "materials" / "cubemaps";
    if (std::filesystem::exists(looseCubemapRoot)) {
      for (const auto& entry : std::filesystem::recursive_directory_iterator(looseCubemapRoot)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".vtf") continue;
        const auto relative = std::filesystem::relative(entry.path(), tfRoot).generic_string();
        cubemapPaths.insert("materials/" + relative.substr(std::string("materials/").size()));
      }
    }
    std::map<std::string, std::set<std::string>> cubeSets;
    static constexpr std::string_view faces[] = {"rt", "lf", "ft", "bk", "up", "dn"};
    for (const auto& path : cubemapPaths) {
      const auto slash = path.find_last_of('/');
      const auto dot = path.rfind(".vtf");
      if (slash == std::string::npos || dot == std::string::npos || dot <= slash + 3) continue;
      const std::string file = path.substr(slash + 1, dot - slash - 1);
      for (const auto face : faces) {
        const std::string suffix = "_" + std::string(face);
        if (file.size() > suffix.size() && file.compare(file.size() - suffix.size(), suffix.size(), suffix) == 0) {
          cubeSets[path.substr(0, slash + 1) + file.substr(0, file.size() - suffix.size())].insert(std::string(face));
          ++cubemapFaceFiles;
          break;
        }
      }
    }
    for (const auto& [stem, foundFaces] : cubeSets) {
      (void)stem;
      if (foundFaces.size() == 6) ++cubemapCompleteSets;
      else ++cubemapIncompleteSets;
    }
    if (argc > 2) {
      const auto bspBytes = readFile(std::filesystem::u8path(argv[2]));
      constexpr std::string_view marker = "env_cubemap";
      for (std::size_t at = 0; at + marker.size() <= bspBytes.size(); ++at) {
        if (std::memcmp(bspBytes.data() + at, marker.data(), marker.size()) == 0) ++bspCubemapEntities;
      }
    }
    constexpr std::size_t maxVtfSize = 64u * 1024u * 1024u;
    for (const auto& path : cubemapPaths) {
      std::vector<std::uint8_t> bytes;
      try {
        for (const auto& archive : archives) {
          std::string error;
          bytes = archive->read(path, &error);
          if (!bytes.empty()) break;
          if (!error.empty()) { ++cubemapParseFailures; break; }
        }
        if (bytes.empty()) {
          const auto fullPath = tfRoot / std::filesystem::path(path);
          if (std::filesystem::exists(fullPath)) {
            const auto fileSize = std::filesystem::file_size(fullPath);
            if (fileSize > maxVtfSize) {
              ++cubemapOversized;
              continue;
            }
            bytes = readFile(fullPath);
          }
        }
      } catch (const std::exception&) {
        ++cubemapParseFailures;
        continue;
      }
      if (bytes.empty()) continue;
      if (bytes.size() > maxVtfSize) {
        ++cubemapOversized;
        continue;
      }
      tf2::native::VtfTexture texture;
      std::string parseError;
      if (!texture.parse(bytes, &parseError)) {
        ++cubemapParseFailures;
        continue;
      }
      if (!texture.header().isCubemap()) continue;
      ++realCubemapVtfFlags;
      realCubemapResourceTags += texture.header().resourceTags.size();
      realCubemapHighResResources += static_cast<std::size_t>(std::count(
        texture.header().resourceTags.begin(), texture.header().resourceTags.end(), 0x30u));
      realCubemapMaxDepth = (std::max)(realCubemapMaxDepth, texture.header().depth);
      realCubemapMaxFrames = (std::max)(realCubemapMaxFrames, texture.header().frames);
      if (texture.header().depth == 6) ++realCubemapDepth6;
    }
  }
  std::cout << "{\"emptyVtfRejected\":" << (emptyRejected ? "true" : "false")
    << ",\"corruptVtfRejected\":" << (corruptRejected ? "true" : "false")
    << ",\"oversizedVtfRejected\":" << (oversizedRejected ? "true" : "false")
    << ",\"cubemapDecodeRejected\":" << (cubemapDecodeRejected ? "true" : "false")
    << ",\"cubemapDepth6Parsed\":" << (cubemapParsed && vtf.header().depth == 6 ? "true" : "false")
    << ",\"cubemapFaceDataOrdered\":" << (cubemapFaceDataOrdered ? "true" : "false")
    << ",\"emptyBspRejected\":" << (emptyBspRejected ? "true" : "false")
    << ",\"noLightingDefault\":" << (noLightingDefault ? "true" : "false")
    << ",\"vmtFeatureMapping\":" << (vmtFeatureMapping ? "true" : "false")
    << ",\"waterFeatureMapping\":" << (waterFeatureMapping ? "true" : "false")
    << ",\"explicitVtfDecoded\":" << (explicitVtfDecoded ? "true" : "false")
    << ",\"explicitVtfWidth\":" << explicitVtfWidth
    << ",\"explicitVtfHeight\":" << explicitVtfHeight
    << ",\"explicitVtfFormat\":" << explicitVtfFormat
    << ",\"explicitVtfRgbaBytes\":" << explicitVtfRgbaBytes
    << ",\"explicitVmtParsed\":" << (explicitVmtParsed ? "true" : "false")
    << ",\"explicitVmtShader\":\"" << explicitVmtShader << "\""
    << ",\"explicitVmtHasBump\":" << (!explicitVmtBump.empty() ? "true" : "false")
    << ",\"explicitVmtHasEnvMap\":" << (!explicitVmtEnv.empty() ? "true" : "false")
    << ",\"explicitVmtSelfIllum\":" << (explicitVmtSelfIllum ? "true" : "false")
    << ",\"realBumpDecoded\":" << (realBumpDecoded ? "true" : "false")
    << ",\"realEnvDecoded\":" << (realEnvDecoded ? "true" : "false")
    << ",\"realMaterials\":" << realMaterials
    << ",\"bumpDeclared\":" << bumpDeclared
    << ",\"envDeclared\":" << envDeclared
    << ",\"archiveOpen\":" << (archiveOpen ? "true" : "false")
    << ",\"archiveCount\":" << archiveCount
    << ",\"realVmtCandidates\":" << realVmtCandidates
    << ",\"bspCubemapEntities\":" << bspCubemapEntities
    << ",\"cubemapFaceFiles\":" << cubemapFaceFiles
    << ",\"cubemapCompleteSets\":" << cubemapCompleteSets
    << ",\"cubemapIncompleteSets\":" << cubemapIncompleteSets
    << ",\"realCubemapComplete\":" << (cubemapCompleteSets > 0 ? "true" : "false")
    << ",\"realCubemapVtfFlags\":" << realCubemapVtfFlags
    << ",\"realCubemapHighResResources\":" << realCubemapHighResResources
    << ",\"realCubemapResourceTags\":" << realCubemapResourceTags
    << ",\"realCubemapDepth6\":" << realCubemapDepth6
    << ",\"realCubemapMaxDepth\":" << realCubemapMaxDepth
    << ",\"realCubemapMaxFrames\":" << realCubemapMaxFrames
    << ",\"cubemapParseFailures\":" << cubemapParseFailures
    << ",\"cubemapOversized\":" << cubemapOversized
    << ",\"allVtfCandidates\":" << allVtfCandidates
    << ",\"allCubemapVtfFlags\":" << allCubemapVtfFlags
    << ",\"allCubemapDepth6\":" << allCubemapDepth6
    << ",\"allCubemapMaxDepth\":" << allCubemapMaxDepth
    << ",\"cubemapMode\":\"approximate-2d\",\"lightmapMode\":\"average-intensity\"}\n";
  return emptyRejected && corruptRejected && oversizedRejected && cubemapDecodeRejected && emptyBspRejected && noLightingDefault
    && vmtFeatureMapping && waterFeatureMapping ? 0 : 1;
}
