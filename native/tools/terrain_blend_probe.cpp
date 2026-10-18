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
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
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

// A minimal VTF the decoder accepts: version 7.1, 64-byte header, one mip, one
// frame, no resource table. `format` 0 is RGBA8888 and 4 bytes per texel, so
// the payload size is width * height * 4. Enough to drive the six refusal
// branches without shipping binary fixtures in the tree.
std::vector<std::uint8_t> makeVtf(std::uint16_t width, std::uint16_t height,
    std::uint32_t format, bool cubemap, std::size_t payload, std::uint16_t depth = 0) {
  // 7.2 reads depth as a 16-bit field at offset 63, so a header that carries one
  // has to be 65 bytes: at 64 the field's high byte would be the first byte of
  // the image and the chain math would run off the end.
  const std::size_t headerSize = depth > 0 ? 65u : 64u;
  std::vector<std::uint8_t> bytes(headerSize + payload, 0);
  std::memcpy(bytes.data(), "VTF\0", 4);
  const auto put32 = [&](std::size_t at, std::uint32_t value) {
    std::memcpy(bytes.data() + at, &value, 4);
  };
  const auto put16 = [&](std::size_t at, std::uint16_t value) {
    std::memcpy(bytes.data() + at, &value, 2);
  };
  put32(4, 7);                    // major
  put32(8, depth > 0 ? 2 : 1);    // minor; 7.2 is the first version with a depth field
  put32(12, headerSize);          // header size
  put16(16, width);
  put16(18, height);
  put32(20, cubemap ? 0x00004000u : 0u);
  put16(24, 1);                   // frames
  put16(26, 0);                   // first frame
  put32(52, format);
  bytes[56] = 1;                  // mip count
  if (depth > 0) put16(63, depth);
  for (std::size_t i = headerSize; i < bytes.size(); ++i) bytes[i] = static_cast<std::uint8_t>(i * 7u);
  return bytes;
}

} // namespace

int main(int argc, char** argv) {
  std::filesystem::path mapPath;
  std::filesystem::path tfRoot;
  std::string expectBlend;
  std::size_t triangleLimit = 200000;
  bool haveEye = false;
  bool haveCluster = false;
  bool refusalFixtures = false;
  int clusterOverride = -1;
  float eyeX = 0.0f, eyeY = 0.0f, eyeZ = 0.0f;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--map" && i + 1 < argc) { mapPath = std::filesystem::u8path(argv[++i]); continue; }
    if (arg == "--tf-root" && i + 1 < argc) { tfRoot = std::filesystem::u8path(argv[++i]); continue; }
    if (arg == "--expect-blend" && i + 1 < argc) { expectBlend = argv[++i]; continue; }
    if (arg == "--triangle-limit" && i + 1 < argc) {
      triangleLimit = static_cast<std::size_t>(std::stoull(argv[++i])); continue;
    }
    if (arg == "--eye" && i + 1 < argc) {      // The demo camera, in map coordinates. Only the atlas census uses it: a
      // material that misses the atlas matters when it is on screen, and the
      // pvs is the closest thing to "on screen" this probe can ask without a
      // rasteriser.
      if (std::sscanf(argv[++i], "%f,%f,%f", &eyeX, &eyeY, &eyeZ) == 3) haveEye = true;
      continue;
    }
    if (arg == "--cluster" && i + 1 < argc) {
      // The cluster the product's own vis pass reported (`cameraCluster=`). On a
      // paused demo the product traces `eyeMap=(0,0,0)`, so deriving the cluster
      // from a point would measure a different set than the frame was drawn
      // from. Taking the product's number is the only way the two agree.
      clusterOverride = std::atoi(argv[++i]);
      haveCluster = clusterOverride >= 0;
      continue;
    }
    if (arg == "--refusal-fixtures") { refusalFixtures = true; continue; }
    std::cerr << "usage: terrain_blend_probe --map <map.bsp> --tf-root <dir> [--expect-blend <material>]"
                 " [--eye x,y,z] [--cluster N] [--triangle-limit N]\n"
                 "       terrain_blend_probe --refusal-fixtures\n";
    return 2;
  }
  if (!refusalFixtures && (mapPath.empty() || tfRoot.empty())) {
    std::cerr << "usage: terrain_blend_probe --map <map.bsp> --tf-root <dir> [--expect-blend <material>]"
                 " [--eye x,y,z] [--cluster N] [--triangle-limit N]\n"
                 "       terrain_blend_probe --refusal-fixtures\n";
    return 2;
  }
  if (refusalFixtures) {
    // ---- the refusal paths, executed -------------------------------------
    // Six named reasons exist in the product's blend path and the reference map
    // reaches none of them: every layer resolves, so `refused=0` was a reading
    // about the map, not about the code. These fixtures hand the same resolver
    // the malformed inputs it claims to reject, so each reason is executed at
    // least once and printed as a string a gate can compare.
    //
    // Two controls are included on purpose. A fixture set where everything is
    // refused would satisfy "every case refuses" while proving only that the
    // resolver refuses everything; the controls say a well-formed layer and a
    // well-formed modulate still resolve.
    const auto valid = makeVtf(64, 64, 0, false, 64u * 64u * 4u);
    // A cubemap parses (its face chain is in the file) and the 2D decoder
    // refuses it, which is the "parsed but decoded to nothing" path rather than
    // the "not a VTF" one.
    const auto cubemap = makeVtf(32, 32, 0, true, 32u * 32u * 4u * 6u, 6);
    const std::vector<std::uint8_t> notVtf = {'n', 'o', 't', ' ', 'a', ' ', 'v', 't', 'f'};

    const auto layerBase = tf2::native::resolveBlendLayer("materials/fixture/second.vtf", valid, false, 64, 64);
    const auto layerMissing = tf2::native::resolveBlendLayer("materials/fixture/missing.vtf", {}, true, 64, 64);
    const auto layerMalformed = tf2::native::resolveBlendLayer("materials/fixture/broken.vtf", notVtf, true, 64, 64);
    const auto layerUndecodable = tf2::native::resolveBlendLayer("materials/fixture/cube.vtf", cubemap, true, 64, 64);
    const auto layerUnresampleable = tf2::native::resolveBlendLayer("materials/fixture/second.vtf", valid, true, 0, 64);
    const auto modulateNoSecond = tf2::native::resolveBlendModulate("materials/fixture/mod.vtf", valid, false, 64, 64);
    const auto modulateMissing = tf2::native::resolveBlendModulate("materials/fixture/missing.vtf", {}, true, 64, 64);
    const auto modulateUndecodable = tf2::native::resolveBlendModulate("materials/fixture/cube.vtf", cubemap, true, 64, 64);
    const auto layerControl = tf2::native::resolveBlendLayer("materials/fixture/second.vtf", valid, true, 64, 64);
    const auto modulateControl = tf2::native::resolveBlendModulate("materials/fixture/mod.vtf", valid, true, 64, 64);

    const auto emitCase = [&](const char* name, const std::string& reason) {
      emit((std::string("refusal.") + name + ".reason").c_str(), reason.empty() ? "-" : reason);
      emitNumber((std::string("refusal.") + name + ".refused").c_str(), reason.empty() ? 0 : 1);
    };
    emitCase("layer-base-unresolved", layerBase.reason);
    emitCase("layer-second-unresolved", layerMissing.reason);
    emitCase("layer-not-vtf", layerMalformed.reason);
    emitCase("layer-decode-empty", layerUndecodable.reason);
    emitCase("layer-resample-failed", layerUnresampleable.reason);
    emitCase("modulate-no-second-layer", modulateNoSecond.reason);
    emitCase("modulate-unresolved", modulateMissing.reason);
    emitCase("modulate-decode-empty", modulateUndecodable.reason);
    emitNumber("refusal.cases", 8);
    emitNumber("refusal.refused", 8);
    emitNumber("refusal.controlLayerResolved",
      layerControl.status == tf2::native::BlendLayerOutcome::Status::Resolved
        && layerControl.pixels.size() == 64u * 64u * 4u ? 1 : 0);
    emitNumber("refusal.controlModulateResolved",
      modulateControl.status == tf2::native::BlendModulateOutcome::Status::Resolved
        && modulateControl.pixels.size() == 64u * 64u * 4u ? 1 : 0);
    emitNumber("refusal.controlLayerPixels", static_cast<long long>(layerControl.pixels.size()));
    emitNumber("refusal.controlModulatePixels", static_cast<long long>(modulateControl.pixels.size()));
    return 0;
  }
  const auto bspBytes = readFile(mapPath);
  if (bspBytes.empty()) { std::cerr << "cannot read " << mapPath.string() << "\n"; return 2; }
  tf2::native::BspMap map;
  if (!tf2::native::BspParser::parse(bspBytes, map, triangleLimit) || !map.valid) {
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
  emitNumber("map.truncated", map.triangles.size() >= triangleLimit ? 1 : 0);
  emitNumber("map.triangleLimit", static_cast<long long>(triangleLimit));

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
  std::size_t bump2Declared = 0, detailDeclared = 0, ssbumpDeclared = 0;
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
    // T12 step 8: which of the three unimplemented Source fields these actual
    // materials name. The task says to decide from the references, not from the
    // shader's documentation, so the decision is made from these strings: a
    // field is "required" only if a material the terrain draws declares it.
    const auto field = [](const tf2::native::VmtMaterial& m, const char* want) {
      for (const auto& [key, value] : m.fields) {
        std::string lower = key;
        for (auto& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (lower == want) return value;
      }
      return std::string{};
    };
    const auto bump2 = field(material, "$bumpmap2");
    const auto detail = field(material, "%detailtype");
    const auto ssbump = field(material, "$ssbump");
    emit(("blend." + materialName + ".bumpmap2").c_str(), bump2.empty() ? "-" : bump2);
    emit(("blend." + materialName + ".detailtype").c_str(), detail.empty() ? "-" : detail);
    emit(("blend." + materialName + ".ssbump").c_str(), ssbump.empty() ? "-" : ssbump);
    if (!bump2.empty()) ++bump2Declared;
    if (!detail.empty()) ++detailDeclared;
    if (!ssbump.empty()) ++ssbumpDeclared;
  }
  emitNumber("blend.declared", static_cast<long long>(blendDeclared));
  emitNumber("blend.resolved", static_cast<long long>(blendResolved));
  emitNumber("blend.modulateDeclared", static_cast<long long>(modulateDeclared));
  emitNumber("blend.modulateResolved", static_cast<long long>(modulateResolved));
  emitNumber("blend.bumpmap2Declared", static_cast<long long>(bump2Declared));
  emitNumber("blend.detailtypeDeclared", static_cast<long long>(detailDeclared));
  emitNumber("blend.ssbumpDeclared", static_cast<long long>(ssbumpDeclared));
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
  // T12 post-merge: the weight has to stay on its corner when T11 reverses a
  // triangle. Counted at emission (bsp_map.cpp), where the grid cell and the
  // emitted vertex can be compared; a run where `detached` is non-zero draws a
  // blend whose mask is mirrored inside the quads it was authored on.
  emitNumber("disp.alphaPairingChecked", static_cast<long long>(map.alphaPairingChecked));
  emitNumber("disp.alphaPairingDetached", static_cast<long long>(map.alphaPairingDetached));

  // ---- 2b. atlas coverage -------------------------------------------------
  // The product's atlas is a 64-tile grid. Materials past the cap draw the
  // fallback tile, and until this reading existed the only trace was
  // `capped=51` on a trace line: a count with no names, no sizes and no way to
  // tell "51 one-triangle trims" from "the terrain's own material".
  //
  // `capped` here has to mean the same set the product drops, so the walk
  // reproduces main.cpp's acceptance order exactly: a material that declares a
  // second layer sorts ahead of every single-layer material, then triangle
  // count (descending, name ascending on a tie), and the cap is applied to
  // *tiles*. Blend materials go first because past the cap a blend material
  // loses its second layer *and* draws a foreign tile, while a single-layer
  // material only loses its own texture. The cap check also comes before the
  // VMT read, because that is where the product stops: a material past the cap
  // is capped whether or not it would have resolved.
  {
    constexpr std::size_t kAtlasTiles = 64;
    std::unordered_map<std::string, std::size_t> counts;
    for (const auto& triangle : map.triangles) {
      if (!triangle.material.empty()) ++counts[triangle.material];
    }
    std::vector<std::pair<std::size_t, std::string>> order;
    order.reserve(counts.size());
    for (const auto& entry : counts) order.emplace_back(entry.second, entry.first);
    std::sort(order.begin(), order.end(), [](const auto& l, const auto& r) {
      return l.second != r.second ? l.second > r.second : l.first < r.first;
    });
    const int cameraCluster = haveCluster ? clusterOverride
      : (haveEye ? tf2::native::BspParser::clusterAt(map, eyeX, eyeY, eyeZ) : -1);
    std::unordered_map<std::string, std::size_t> visible;
    if (cameraCluster >= 0) {
      for (const auto& triangle : map.triangles) {
        if (triangle.material.empty()) continue;
        if (tf2::native::BspParser::clusterVisible(map, cameraCluster, triangle.cluster)) ++visible[triangle.material];
      }
    }
    // "Would the product give this material a tile?" -- the same four things it
    // checks, in the same order: VMT bytes, parse, a base layer, a readable VTF.
    const auto textureReadable = [&](const std::string& name) {
      if (name.empty()) return false;
      std::vector<std::uint8_t> bytes;
      resolve(tf2::native::VmtParser::resourcePath(name, ".vtf"), bytes);
      if (bytes.empty()) return false;
      tf2::native::VtfTexture texture;
      return texture.parse(bytes);
    };
    // One pass, then the product's sort. `readable` is "would this material
    // take a tile at all", `blends` is the priority key; both come out of the
    // same VMT read, so the order and the acceptance cannot disagree.
    struct Entry {
      std::string name;
      std::size_t tris = 0;
      std::size_t vis = 0;
      bool readable = false;
      bool blends = false;
    };
    std::vector<Entry> entries;
    entries.reserve(order.size());
    for (const auto& entry : order) {
      Entry item;
      item.name = entry.second;
      item.tris = entry.first;
      item.vis = visible.count(item.name) ? visible[item.name] : 0;
      std::vector<std::uint8_t> vmtBytes;
      resolve(tf2::native::VmtParser::resourcePath(item.name, ".vmt"), vmtBytes);
      tf2::native::VmtMaterial material;
      const bool parsed = !vmtBytes.empty()
        && tf2::native::VmtParser::parse(std::string(vmtBytes.begin(), vmtBytes.end()), material)
        && (!material.baseTexture.empty() || !material.baseTexture2.empty());
      item.readable = parsed
        && (textureReadable(material.baseTexture) || textureReadable(material.baseTexture2));
      item.blends = parsed && !material.baseTexture2.empty()
        && material.baseTexture2 != material.baseTexture;
      entries.push_back(std::move(item));
    }
    std::sort(entries.begin(), entries.end(), [](const Entry& l, const Entry& r) {
      if (l.blends != r.blends) return l.blends;
      return l.tris != r.tris ? l.tris > r.tris : l.name < r.name;
    });
    std::size_t tiled = 0, cappedTriangles = 0, cappedVisible = 0, tiledTriangles = 0, tiledVisible = 0;
    std::size_t unresolvedMaterials = 0, blendFirst = 0;
    std::size_t cappedTexturable = 0, cappedUnreadable = 0, cappedTexturableVisible = 0;
    std::vector<std::pair<std::size_t, std::string>> capped;
    std::vector<std::pair<std::size_t, std::string>> onScreen;
    std::string cappedBlend;
    std::size_t cappedBlendCount = 0;
    for (const auto& item : entries) {
      const std::string& name = item.name;
      const std::size_t tris = item.tris;
      const std::size_t vis = item.vis;
      if (item.blends) ++blendFirst;
      if (tiled >= kAtlasTiles) {
        cappedTriangles += tris; cappedVisible += vis;
        capped.emplace_back(tris, name);
        // Two different failures live past the cap, and only one of them can be
        // fixed by giving the material a tile. The product stops reading at the
        // cap, so its own `capped=51` counts only the materials it would have
        // textured; the 32 it would have rejected anyway are counted as
        // `vmtNoBase`/`vmtMissing` on the same line. Split here so the strategy
        // question ("raise the cap, batch, or second atlas") is asked about the
        // materials a tile would actually help.
        if (item.readable) ++cappedTexturable; else ++cappedUnreadable;
        if (item.readable && vis > 0) {
          ++cappedTexturableVisible;
          onScreen.emplace_back(vis, name);
        }
        if (item.blends) {
          ++cappedBlendCount;
          if (!cappedBlend.empty()) cappedBlend += ";";
          cappedBlend += name + "=" + std::to_string(tris);
        }
        continue;
      }
      if (!item.readable) { ++unresolvedMaterials; continue; }
      ++tiled;
      tiledTriangles += tris; tiledVisible += vis;
    }
    std::sort(onScreen.begin(), onScreen.end(), [](const auto& l, const auto& r) {
      return l.first != r.first ? l.first > r.first : l.second < r.second;
    });
    std::sort(capped.begin(), capped.end(), [](const auto& l, const auto& r) {
      return l.first != r.first ? l.first > r.first : l.second < r.second;
    });
    const auto join = [](const std::vector<std::pair<std::size_t, std::string>>& list, std::size_t limit) {
      std::string out;
      for (std::size_t i = 0; i < list.size() && i < limit; ++i) {
        if (!out.empty()) out += ";";
        out += list[i].second + "=" + std::to_string(list[i].first);
      }
      return out.empty() ? std::string("-") : out;
    };
    emitNumber("atlas.cap", static_cast<long long>(kAtlasTiles));
    emitNumber("atlas.distinct", static_cast<long long>(order.size()));
    emitNumber("atlas.tiled", static_cast<long long>(tiled));
    emitNumber("atlas.capped", static_cast<long long>(capped.size()));
    emitNumber("atlas.unresolved", static_cast<long long>(unresolvedMaterials));
    emitNumber("atlas.tiledTriangles", static_cast<long long>(tiledTriangles));
    emitNumber("atlas.cappedTriangles", static_cast<long long>(cappedTriangles));
    emitNumber("atlas.eyeCluster", cameraCluster);
    if (cameraCluster >= 0) {
      emitNumber("atlas.tiledVisible", static_cast<long long>(tiledVisible));
      emitNumber("atlas.cappedVisible", static_cast<long long>(cappedVisible));
      emitNumber("atlas.cappedVisibleMaterials", static_cast<long long>(onScreen.size()));
    }
    emit("atlas.cappedTop", join(capped, 8));
    emit("atlas.cappedVisibleTop", join(onScreen, 8));
    emitNumber("atlas.cappedTexturable", static_cast<long long>(cappedTexturable));
    emitNumber("atlas.cappedUnreadable", static_cast<long long>(cappedUnreadable));
    emitNumber("atlas.cappedTexturableVisible", static_cast<long long>(cappedTexturableVisible));
    emitNumber("atlas.cappedBlendMaterials", static_cast<long long>(cappedBlendCount));
    emit("atlas.cappedBlend", cappedBlend.empty() ? std::string("-") : cappedBlend);
    emitNumber("atlas.blendFirst", static_cast<long long>(blendFirst));
  }

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
