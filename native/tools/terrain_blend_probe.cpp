// terrain_blend_probe -- T12: what the terrain materials declare, and what the
// parser read out of the displacement that weights them.
//
// WHAT IT ANSWERS
//   The release blocker is "NATURE/BLEND* surfaces do not show their intended
//   terrain appearance". Two facts decide whether that can be fixed in the
//   renderer at all, and neither is visible from a counter in the product:
//
//     1. Which resources the blend materials actually name, and whether each of
//        them resolves from the store the lookup policy is supposed to reach.
//        The reference map's brush faces use a VBSP-generated `_wvt_patch`
//        material that exists *only* inside the BSP pakfile, so a probe that
//        stops at the VPK reports a policy that was never exercised.
//
//     2. Whether the per-vertex blend weight exists in the file. Source's
//        WorldVertexTransition weights the two layers by `CDispVert::m_flAlpha`,
//        so a parser that skips the 4 bytes at offset 16 leaves the renderer
//        with nothing to blend by -- and every other reading stays green.
//
//   This probe parses the same BSP through the same BspParser the product uses,
//   resolves every resource through the same loose -> VPK -> pakfile order, and
//   prints readings a gate can assert and a mutation can move.
//
// Read-only: it never writes, never renders, and knows nothing about the GPU.
#include "bsp_map.h"
#include "vmt_material.h"
#include "vpk_archive.h"
#include "vtf_texture.h"
#include "world_material.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace {

std::vector<std::uint8_t> readFile(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(input)),
    std::istreambuf_iterator<char>());
}

void emit(const char* key, const std::string& value) {
  std::cout << "{\"reading\":\"" << key << "\",\"value\":\"" << value << "\"}\n";
}
void emitNumber(const char* key, long long value) {
  std::cout << "{\"reading\":\"" << key << "\",\"value\":\"" << value << "\"}\n";
}

// The same five archives, in the same order, that main.cpp opens.
const std::vector<std::wstring> kArchiveNames = {
  L"pak01_dir.vpk", L"tf2_misc_dir.vpk", L"tf2_textures_dir.vpk",
  L"tf2_sound_misc_dir.vpk", L"tf2_sound_vo_english_dir.vpk",
};

struct Resolution {
  bool loose = false;
  bool archive = false;
  bool pakfile = false;
  std::size_t bytes = 0;
  std::string store;
};

} // namespace

int main(int argc, char** argv) {
  std::filesystem::path mapPath;
  std::filesystem::path tfRoot;
  std::string expectBlend;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--map" && i + 1 < argc) { mapPath = std::filesystem::u8path(argv[++i]); continue; }
    if (arg == "--tf-root" && i + 1 < argc) { tfRoot = std::filesystem::u8path(argv[++i]); continue; }
    if (arg == "--expect-blend" && i + 1 < argc) { expectBlend = argv[++i]; continue; }
    std::cerr << "usage: terrain_blend_probe --map <map.bsp> --tf-root <dir> [--expect-blend <material>]\n";
    return 2;
  }
  if (mapPath.empty() || tfRoot.empty()) {
    std::cerr << "usage: terrain_blend_probe --map <map.bsp> --tf-root <dir> [--expect-blend <material>]\n";
    return 2;
  }
  const auto bspBytes = readFile(mapPath);
  if (bspBytes.empty()) { std::cerr << "cannot read " << mapPath.string() << "\n"; return 2; }
  tf2::native::BspMap map;
  if (!tf2::native::BspParser::parse(bspBytes, map, 200000) || !map.valid) {
    std::cerr << "BSP refused: " << map.error << "\n";
    return 2;
  }
  emit("map.name", mapPath.stem().string());
  emitNumber("map.triangles", static_cast<long long>(map.triangles.size()));
  emitNumber("map.displacementFaces", static_cast<long long>(map.displacementFaces));
  // The product parses with the same 200000-triangle ceiling, so a census taken
  // here is a census of the part of the map the product can draw. Printed
  // rather than assumed: a truncated parse that reads as complete is how a
  // material census silently stops being a census.
  emitNumber("map.truncated", map.triangles.size() >= 200000 ? 1 : 0);

  std::vector<std::unique_ptr<tf2::native::VpkArchive>> archives;
  for (const auto& name : kArchiveNames) {
    auto archive = std::make_unique<tf2::native::VpkArchive>();
    std::string error;
    if (!archive->open(tfRoot / name, &error)) continue;
    archives.push_back(std::move(archive));
  }
  emitNumber("archives.open", static_cast<long long>(archives.size()));

  // One lookup policy, loose then archives then the map's own pakfile -- the
  // same order the product uses, so a reading here means the same thing there.
  const auto resolve = [&](const std::string& path, std::vector<std::uint8_t>& out) {
    Resolution resolution;
    out = readFile(tfRoot / std::filesystem::path(path));
    if (!out.empty()) { resolution.loose = true; resolution.store = "loose"; }
    if (out.empty()) {
      for (const auto& archive : archives) {
        out = archive->read(path);
        if (!out.empty()) { resolution.archive = true; resolution.store = "vpk"; break; }
      }
    }
    if (out.empty() && !bspBytes.empty()) {
      std::string reason;
      if (tf2::native::bspPakfileEntry(bspBytes, path, out, &reason) && !out.empty()) {
        resolution.pakfile = true; resolution.store = "pakfile";
      }
    }
    resolution.bytes = out.size();
    return resolution;
  };

  // ---- 1. the materials a displacement actually uses ----------------------
  std::map<std::string, std::size_t> displacementMaterials;
  for (const auto& triangle : map.triangles) {
    if (triangle.displacement && !triangle.material.empty()) ++displacementMaterials[triangle.material];
  }
  emitNumber("disp.materials", static_cast<long long>(displacementMaterials.size()));

  std::size_t blendDeclared = 0, blendResolved = 0, modulateDeclared = 0, modulateResolved = 0;
  std::size_t patchOnly = 0;
  // T12: the names of the materials that actually declare a second layer. The
  // bounds below are computed from this set, because "where the blend terrain
  // is" is a question a camera has to be pointed at and a claim about the
  // frame has to be checked against.
  std::set<std::string> blendMaterialNames;
  for (const auto& entry : displacementMaterials) {
    const std::string& materialName = entry.first;
    std::vector<std::uint8_t> vmtBytes;
    const auto vmtPath = tf2::native::VmtParser::resourcePath(materialName, ".vmt");
    const auto vmtResolution = resolve(vmtPath, vmtBytes);
    if (vmtBytes.empty()) {
      emit(("disp.material." + materialName + ".vmt").c_str(), "missing");
      continue;
    }
    tf2::native::VmtMaterial material;
    if (!tf2::native::VmtParser::parse(std::string(vmtBytes.begin(), vmtBytes.end()), material)) {
      emit(("disp.material." + materialName + ".vmt").c_str(), "unparsable");
      continue;
    }
    emit(("disp.material." + materialName + ".shader").c_str(), material.shader);
    emit(("disp.material." + materialName + ".store").c_str(), vmtResolution.store);
    if (vmtResolution.pakfile && !vmtResolution.loose && !vmtResolution.archive) ++patchOnly;
    const bool declaresBlend = !material.baseTexture2.empty()
      && material.baseTexture2 != material.baseTexture;
    if (!declaresBlend) continue;
    ++blendDeclared;
    blendMaterialNames.insert(materialName);
    std::vector<std::uint8_t> firstBytes, secondBytes, modulateBytes;
    const auto first = resolve(tf2::native::VmtParser::resourcePath(material.baseTexture, ".vtf"), firstBytes);
    const auto second = resolve(tf2::native::VmtParser::resourcePath(material.baseTexture2, ".vtf"), secondBytes);
    const bool hasModulate = !material.blendModulateTexture.empty();
    Resolution modulate;
    if (hasModulate) {
      ++modulateDeclared;
      modulate = resolve(tf2::native::VmtParser::resourcePath(material.blendModulateTexture, ".vtf"), modulateBytes);
      if (modulate.bytes > 0) ++modulateResolved;
    }
    const bool all = first.bytes > 0 && second.bytes > 0 && (!hasModulate || modulate.bytes > 0);
    if (all) ++blendResolved;
    emit(("blend." + materialName + ".layers").c_str(),
      std::string(first.store.empty() ? "-" : first.store) + "/" +
      (second.store.empty() ? "-" : second.store) + "/" +
      (hasModulate ? (modulate.store.empty() ? "-" : modulate.store) : "none"));
    // Dimensions are the reading the task asks for: what the source layers
    // were, before anything resamples them onto a tile grid.
    const auto dims = [](const std::vector<std::uint8_t>& bytes) {
      tf2::native::VtfTexture texture;
      if (bytes.empty() || !texture.parse(bytes)) return std::string("-");
      return std::to_string(texture.header().width) + "x" + std::to_string(texture.header().height);
    };
    emit(("blend." + materialName + ".dims").c_str(),
      dims(firstBytes) + "/" + dims(secondBytes) + "/" + (hasModulate ? dims(modulateBytes) : "none"));
  }
  emitNumber("blend.declared", static_cast<long long>(blendDeclared));
  emitNumber("blend.resolved", static_cast<long long>(blendResolved));
  emitNumber("blend.modulateDeclared", static_cast<long long>(modulateDeclared));
  emitNumber("blend.modulateResolved", static_cast<long long>(modulateResolved));
  // How many of the materials this map's displacements use exist only in the
  // pakfile. On the reference map that is 0: the pakfile store is exercised by
  // the *brush* faces instead, which is the census below.
  emitNumber("disp.pakfileOnly", static_cast<long long>(patchOnly));
  // Where the blend terrain is, in map coordinates. A gate that has to point a
  // camera at it should not have to guess.
  {
    float bx0 = 1e30f, by0 = 1e30f, bz0 = 1e30f, bx1 = -1e30f, by1 = -1e30f, bz1 = -1e30f;
    std::size_t blendTriangles = 0;
    for (const auto& triangle : map.triangles) {
      if (blendMaterialNames.count(triangle.material) == 0) continue;
      ++blendTriangles;
      for (const auto& point : {triangle.a, triangle.b, triangle.c}) {
        bx0 = std::min(bx0, point.x); by0 = std::min(by0, point.y); bz0 = std::min(bz0, point.z);
        bx1 = std::max(bx1, point.x); by1 = std::max(by1, point.y); bz1 = std::max(bz1, point.z);
      }
    }
    emitNumber("disp.blendTriangles", static_cast<long long>(blendTriangles));
    char bounds[128] = {0}, centre[96] = {0};
    if (blendTriangles) {
      std::snprintf(bounds, sizeof(bounds), "%.1f,%.1f,%.1f..%.1f,%.1f,%.1f",
        static_cast<double>(bx0), static_cast<double>(by0), static_cast<double>(bz0),
        static_cast<double>(bx1), static_cast<double>(by1), static_cast<double>(bz1));
      std::snprintf(centre, sizeof(centre), "%.1f,%.1f,%.1f",
        static_cast<double>((bx0 + bx1) * 0.5f), static_cast<double>((by0 + by1) * 0.5f),
        static_cast<double>((bz0 + bz1) * 0.5f));
    } else {
      std::snprintf(bounds, sizeof(bounds), "-");
      std::snprintf(centre, sizeof(centre), "-");
    }
    emit("disp.blendBounds", bounds);
    emit("disp.blendCentre", centre);
  }

  // ---- 1b. the third store, over the whole map ---------------------------
  // VBSP writes a `_wvt_patch` copy of every blend material for the brush faces
  // and puts it in the BSP pakfile, so "is the pakfile store reached at all" is
  // a question about the whole map rather than about its displacements. A
  // lookup policy that stopped at the VPK would report these as missing while
  // every displacement reading stayed green.
  std::set<std::string> allMaterials;
  for (const auto& triangle : map.triangles) {
    if (!triangle.material.empty()) allMaterials.insert(triangle.material);
  }
  std::size_t materialsResolved = 0, materialsMissing = 0, materialsPakfileOnly = 0;
  for (const auto& name : allMaterials) {
    std::vector<std::uint8_t> bytes;
    const auto resolution = resolve(tf2::native::VmtParser::resourcePath(name, ".vmt"), bytes);
    if (bytes.empty()) { ++materialsMissing; continue; }
    ++materialsResolved;
    if (resolution.pakfile && !resolution.loose && !resolution.archive) ++materialsPakfileOnly;
  }
  emitNumber("materials.distinct", static_cast<long long>(allMaterials.size()));
  emitNumber("materials.resolved", static_cast<long long>(materialsResolved));
  emitNumber("materials.missing", static_cast<long long>(materialsMissing));
  emitNumber("materials.pakfileOnly", static_cast<long long>(materialsPakfileOnly));

  // ---- 2. the weight the parser read --------------------------------------
  // Counted off the triangles, not off the lump: this is the value the
  // renderer will upload. A parser that stops reading at offset 12 leaves every
  // one of these at the -1 sentinel and the count below is the reading that
  // says so.
  std::size_t weighted = 0, sentinel = 0;
  std::set<int> distinct;
  float minAlpha = 1.0f, maxAlpha = 0.0f;
  for (const auto& triangle : map.triangles) {
    if (!triangle.displacement) continue;
    for (const float alpha : {triangle.aAlpha, triangle.bAlpha, triangle.cAlpha}) {
      if (!(alpha >= 0.0f)) { ++sentinel; continue; }
      ++weighted;
      distinct.insert(static_cast<int>(alpha * 1000.0f + 0.5f));
      minAlpha = std::min(minAlpha, alpha);
      maxAlpha = std::max(maxAlpha, alpha);
    }
  }
  emitNumber("disp.weightedVertices", static_cast<long long>(weighted));
  emitNumber("disp.sentinelVertices", static_cast<long long>(sentinel));
  emitNumber("disp.distinctWeights", static_cast<long long>(distinct.size()));
  char range[64] = {0};
  std::snprintf(range, sizeof(range), "%.3f..%.3f", weighted ? minAlpha : -1.0f, weighted ? maxAlpha : -1.0f);
  emit("disp.weightRange", range);

  // ---- 3. the material the task names -------------------------------------
  if (!expectBlend.empty()) {
    std::vector<std::uint8_t> vmtBytes;
    const auto resolution = resolve(tf2::native::VmtParser::resourcePath(expectBlend, ".vmt"), vmtBytes);
    tf2::native::VmtMaterial material;
    const bool parsed = !vmtBytes.empty()
      && tf2::native::VmtParser::parse(std::string(vmtBytes.begin(), vmtBytes.end()), material);
    emit("expect.material", expectBlend);
    emit("expect.shader", parsed ? material.shader : "unparsable");
    emit("expect.baseTexture", parsed ? material.baseTexture : "");
    emit("expect.baseTexture2", parsed ? material.baseTexture2 : "");
    emit("expect.blendModulate", parsed ? material.blendModulateTexture : "");
    emit("expect.store", resolution.store.empty() ? "missing" : resolution.store);
    emitNumber("expect.ok", parsed && !material.baseTexture2.empty() ? 1 : 0);
  }
  return 0;
}
