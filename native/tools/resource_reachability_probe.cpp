// resource_reachability_probe -- answer, without a window and without a demo,
// which resources the current TF2 install can actually serve.
//
// Why this exists
// ---------------
// The frame-capture gate (step 16) asserts "the demo frame differs from the
// paused frame" and stops there. It never asserts that the demo frame contains
// the demo's *scene*. When this probe was written, that gap was already hiding a
// real defect: the demo frame came back as the vgui spray decal painted over the
// whole viewport, because the map's base texture never resolved and the renderer's
// last-drawn full-screen fallback quad fell back to `texture_` (the spray).
//
// main.cpp builds its archive list from five hardcoded names starting with
// pak01_dir.vpk, and silently skips any that fail to open:
//     if (archive->open(assets.tfDirectory / name)) soundArchives.push_back(...)
// A missing archive therefore produces no error, no log line, and no counter --
// it just makes resources vanish. This probe names them.
//
// Usage: resource_reachability_probe <tfRoot> <mapStem> [materialName]
//   mapStem e.g. koth_bagel_rc13  (path probed: maps/<stem>.bsp)
//   materialName e.g. maps/koth_bagel_rc13 (probed as materials/<name>.vmt)
// Prints one JSON object. Exit 0 always; the readings carry the verdict so the
// caller can assert on them.
//
// `triangleCoverageByCap` recomputes the triangle count at several atlas caps
// (1024, 2048, 4096, uncapped) with the install and the parse held fixed. Its
// purpose is to attribute the loss: on cp_snakewater_final1 the 512 cap covers
// 30731/200000 triangles, a 1024 cap covers 193875, and no cap covers 199227 --
// so the cap, not the material wiring, is what discards the map, and the next fix
// is one integer. It is a measurement, not a proposal: main.cpp is untouched.

#include "bsp_map.h"
#include "vmt_material.h"
#include "vpk_archive.h"
#include "vtf_texture.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {

std::vector<std::uint8_t> readFile(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(input)),
                                    std::istreambuf_iterator<char>());
}

std::string escape(const std::string& value) {
  std::string out;
  for (const char c : value) {
    if (c == '"' || c == '\\') { out.push_back('\\'); out.push_back(c); }
    else if (c == '\n') out += "\\n";
    else out.push_back(c);
  }
  return out;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 3) {
    std::cerr << "usage: resource_reachability_probe <tfRoot> <mapStem> [materialName]\n";
    return 2;
  }
  const std::filesystem::path tfRoot = std::filesystem::u8path(argv[1]);
  const std::string mapStem = argv[2];
  const std::string materialName = argc > 3 ? argv[3] : ("maps/" + mapStem);

  // The exact list main.cpp uses, in the exact order. Names that do not open are
  // reported by name rather than dropped, which is the whole point.
  const std::vector<std::wstring> expected = {
    L"pak01_dir.vpk", L"tf2_misc_dir.vpk", L"tf2_textures_dir.vpk",
    L"tf2_sound_misc_dir.vpk", L"tf2_sound_vo_english_dir.vpk",
  };
  std::vector<std::shared_ptr<tf2::native::VpkArchive>> archives;
  std::string openedNames, missingNames;
  std::size_t totalEntries = 0;
  for (const auto& name : expected) {
    auto archive = std::make_shared<tf2::native::VpkArchive>();
    std::string error;
    if (archive->open(tfRoot / name, &error)) {
      archives.push_back(archive);
      totalEntries += archive->entryCount();
      if (!openedNames.empty()) openedNames += ",";
      openedNames += escape(std::filesystem::path(name).string());
    } else {
      if (!missingNames.empty()) missingNames += ",";
      missingNames += escape(std::filesystem::path(name).string());
    }
  }

  const std::string bspResource = "maps/" + mapStem + ".bsp";
  std::vector<std::uint8_t> bspBytes = readFile(tfRoot / std::filesystem::u8path(bspResource));
  std::string bspSource = bspBytes.empty() ? "" : "loose";
  for (const auto& archive : archives) {
    if (!bspBytes.empty()) break;
    bspBytes = archive->read(bspResource);
    if (!bspBytes.empty()) bspSource = "vpk";
  }
  std::size_t bspTriangles = 0;
  std::string mapMaterialFound;
  std::size_t distinctMaterials = 0, resolvableMaterials = 0, resolvableBaseTextures = 0;
  std::size_t atlasEligible = 0, oversizedRejected = 0;
  std::uint32_t largestAccepted = 0, smallestRejected = 0xFFFFFFFFu;
  std::string firstResolvedTexture;
  std::vector<std::string> eligibleMaterialNames;
  // Counterfactual: what the world atlas could cover if main.cpp:775's 512x512
  // cap were raised. The cap is the single parameter that drops 104 of the 111
  // snakewater materials, so "how much more geometry becomes drawable" is a
  // question about one integer, not about wiring. We record each resolved
  // material's longest side and then count triangle coverage at several caps,
  // so the answer is a reading rather than an estimate.
  std::vector<std::pair<std::string, std::uint32_t>> resolvedMaterialLongest;
  std::vector<std::pair<std::uint32_t, std::size_t>> triangleCoverageByCap;
  std::size_t totalTriangles = 0, texturedTriangles = 0;
  if (!bspBytes.empty()) {
    tf2::native::BspMap map;
    if (tf2::native::BspParser::parse(bspBytes, map, 200000)) {
      bspTriangles = map.triangles.size();
      std::vector<std::string> materials;
      for (const auto& triangle : map.triangles) {
        if (!triangle.material.empty()) materials.push_back(triangle.material);
      }
      if (!materials.empty()) mapMaterialFound = materials.front();
      std::sort(materials.begin(), materials.end());
      materials.erase(std::unique(materials.begin(), materials.end()), materials.end());
      distinctMaterials = materials.size();
      // The number that matters: how many of this map's own materials can be
      // resolved all the way to decodable pixels through the current install.
      // Text on its own proves nothing; only a decoded VTF does.
      for (const auto& name : materials) {
        const std::string vmtPath = tf2::native::VmtParser::resourcePath(name, ".vmt");
        std::vector<std::uint8_t> vmt = readFile(tfRoot / std::filesystem::u8path(vmtPath));
        for (const auto& archive : archives) {
          if (!vmt.empty()) break;
          vmt = archive->read(vmtPath);
        }
        if (vmt.empty()) continue;
        tf2::native::VmtMaterial material;
        if (!tf2::native::VmtParser::parse(std::string(vmt.begin(), vmt.end()), material)) continue;
        if (material.baseTexture.empty()) continue;
        ++resolvableMaterials;
        const std::string vtfPath = tf2::native::VmtParser::resourcePath(material.baseTexture, ".vtf");
        std::vector<std::uint8_t> vtfBytes = readFile(tfRoot / std::filesystem::u8path(vtfPath));
        for (const auto& archive : archives) {
          if (!vtfBytes.empty()) break;
          vtfBytes = archive->read(vtfPath);
        }
        if (vtfBytes.empty()) continue;
        tf2::native::VtfTexture texture;
        std::string decodeError;
        if (texture.parse(vtfBytes, &decodeError) && !texture.decodeRgba(vtfBytes, &decodeError).empty()) {
          ++resolvableBaseTextures;
          if (firstResolvedTexture.empty()) firstResolvedTexture = vtfPath;
          const std::uint32_t w = texture.header().width, h = texture.header().height;
          const std::uint32_t longest = (std::max)(w, h);
          resolvedMaterialLongest.emplace_back(name, longest);
          const bool fitsAtlas = w <= 512 && h <= 512;
          if (fitsAtlas) eligibleMaterialNames.push_back(name);
          // main.cpp keeps only textures at or below 512x512 when building the
          // world atlas (it uses 514px tiles with a 1px border). Count both sides
          // so "the atlas is empty" can be told apart from "the atlas is capped".
          if (!fitsAtlas) {
            ++oversizedRejected;
            smallestRejected = (std::min)(smallestRejected, longest);
          } else {
            ++atlasEligible;
            largestAccepted = (std::max)(largestAccepted, longest);
          }
        }
      }
      // How much of the map can the world atlas actually cover? A material that
      // resolves but is 1024x1024 is dropped by main.cpp's 512 cap, so counting
      // resolvable materials alone would overstate what can reach the screen.
      std::sort(eligibleMaterialNames.begin(), eligibleMaterialNames.end());
      for (const auto& triangle : map.triangles) {
        ++totalTriangles;
        if (std::binary_search(eligibleMaterialNames.begin(), eligibleMaterialNames.end(), triangle.material)) {
          ++texturedTriangles;
        }
      }
      // Counterfactual coverage: hold the install and the parse fixed, and vary
      // only the cap. A cap of 0 means "no cap". If the 512 cap is what is
      // throwing the picture away, these numbers diverge sharply; if wiring is
      // the bottleneck they stay close, and the wiring is what to fix instead.
      const std::uint32_t caps[] = {512u, 1024u, 2048u, 4096u, 0u};
      for (const std::uint32_t cap : caps) {
        std::vector<std::string> names;
        for (const auto& [name, longest] : resolvedMaterialLongest) {
          if (cap == 0u || longest <= cap) names.push_back(name);
        }
        std::sort(names.begin(), names.end());
        std::size_t covered = 0;
        for (const auto& triangle : map.triangles) {
          if (std::binary_search(names.begin(), names.end(), triangle.material)) ++covered;
        }
        triangleCoverageByCap.emplace_back(cap, covered);
      }
    }
  }

  // Resolve the material chain the same way main.cpp does: VMT -> $basetexture ->
  // VTF, at each step trying the loose file then every archive.
  const std::string vmtResource = "materials/" + materialName + ".vmt";
  std::vector<std::uint8_t> vmtBytes = readFile(tfRoot / std::filesystem::u8path(vmtResource));
  std::string vmtSource = vmtBytes.empty() ? "" : "loose";
  for (const auto& archive : archives) {
    if (!vmtBytes.empty()) break;
    vmtBytes = archive->read(vmtResource);
    if (!vmtBytes.empty()) vmtSource = "vpk";
  }
  std::string baseTexture, vtfResource, vtfSource, vtfError;
  std::uint32_t vtfWidth = 0, vtfHeight = 0;
  std::size_t vtfRgbaBytes = 0;
  if (!vmtBytes.empty()) {
    tf2::native::VmtMaterial material;
    if (tf2::native::VmtParser::parse(std::string(vmtBytes.begin(), vmtBytes.end()), material)) {
      baseTexture = material.baseTexture;
      if (!baseTexture.empty()) {
        vtfResource = tf2::native::VmtParser::resourcePath(baseTexture, ".vtf");
        std::vector<std::uint8_t> vtfBytes = readFile(tfRoot / std::filesystem::u8path(vtfResource));
        if (!vtfBytes.empty()) vtfSource = "loose";
        for (const auto& archive : archives) {
          if (!vtfBytes.empty()) break;
          vtfBytes = archive->read(vtfResource);
          if (!vtfBytes.empty()) vtfSource = "vpk";
        }
        if (!vtfBytes.empty()) {
          tf2::native::VtfTexture texture;
          if (texture.parse(vtfBytes, &vtfError)) {
            vtfWidth = texture.header().width;
            vtfHeight = texture.header().height;
            vtfRgbaBytes = texture.decodeRgba(vtfBytes, &vtfError).size();
          }
        }
      }
    }
  }

  std::cout << "{"
            << "\"tfRoot\":\"" << escape(tfRoot.string()) << "\","
            << "\"archivesOpened\":" << archives.size() << ","
            << "\"archivesExpected\":5,"
            << "\"openedNames\":\"" << openedNames << "\","
            << "\"missingNames\":\"" << missingNames << "\","
            << "\"totalEntries\":" << totalEntries << ","
            << "\"bspResource\":\"" << escape(bspResource) << "\","
            << "\"bspBytes\":" << bspBytes.size() << ","
            << "\"bspSource\":\"" << bspSource << "\","
            << "\"bspTriangles\":" << bspTriangles << ","
            << "\"mapMaterialInBsp\":\"" << escape(mapMaterialFound) << "\","
            << "\"distinctMaterials\":" << distinctMaterials << ","
            << "\"materialsWithVmt\":" << resolvableMaterials << ","
            << "\"materialsWithDecodableVtf\":" << resolvableBaseTextures << ","
            << "\"firstResolvedTexture\":\"" << escape(firstResolvedTexture) << "\","
            << "\"atlasEligible\":\"" << atlasEligible << "\","
            << "\"oversizedRejected\":" << oversizedRejected << ","
            << "\"largestAccepted\":\"" << largestAccepted << "\","
            << "\"smallestRejected\":" << (smallestRejected == 0xFFFFFFFFu ? 0u : smallestRejected) << ","
            << "\"totalTriangles\":" << totalTriangles << ","
            << "\"trianglesCoveredByAtlas\":" << texturedTriangles << ","
            << "\"triangleCoverageByCap\":{";
  for (std::size_t i = 0; i < triangleCoverageByCap.size(); ++i) {
    if (i) std::cout << ",";
    const auto& [cap, covered] = triangleCoverageByCap[i];
    // cap 0 is printed as "uncapped" so a reader cannot mistake it for a real cap.
    std::cout << "\"" << (cap == 0u ? std::string("uncapped") : std::to_string(cap))
              << "\":" << covered;
  }
  std::cout << "},"
            << "\"materialsResolvedLongest\":" << resolvedMaterialLongest.size() << ","
            << "\"vmtResource\":\"" << escape(vmtResource) << "\","
            << "\"vmtBytes\":" << vmtBytes.size() << ","
            << "\"vmtSource\":\"" << vmtSource << "\","
            << "\"baseTexture\":\"" << escape(baseTexture) << "\","
            << "\"vtfResource\":\"" << escape(vtfResource) << "\","
            << "\"vtfSource\":\"" << vtfSource << "\","
            << "\"vtfWidth\":" << vtfWidth << ","
            << "\"vtfHeight\":" << vtfHeight << ","
            << "\"vtfRgbaBytes\":" << vtfRgbaBytes << ","
            << "\"vtfError\":\"" << escape(vtfError) << "\""
            << "}\n";
  return 0;
}
