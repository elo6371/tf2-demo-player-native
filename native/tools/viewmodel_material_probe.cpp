// viewmodel_material_probe -- can a ViewModel model's own materials be reached?
//
// WHY THIS EXISTS
//   T2 drew the first-person ViewModel in flat colour and said so:
//   `viewmodel.cpp` counts the texture table and then hard-codes
//   `materialResolved = 0`, because nothing binds a VMT yet. That is honest, but
//   it is also a blank space: nobody has measured whether the chain the product
//   would need -- MDL texture slot -> $cdmaterials -> .vmt -> $basetexture ->
//   .vtf -> decoded pixels -- actually resolves for the three ViewModel model
//   families. This probe measures it, through the product's own parsers
//   (ModelLoader, VmtParser, VtfTexture) and the product's own asset root, and
//   reports per model what resolved and what did not.
//
//   It draws nothing. There is no window, no D3D device and no demo: this is
//   the resource contract only. Whether the pixels reach the screen through the
//   ViewModel pass is a later task, and nothing here may be read as evidence
//   for it.
//
// WHAT IT REPORTS, PER MODEL
//   model            the requested path
//   resolution       how the MDL itself was found (loose / vpk / missing)
//   cdmaterials      the studiohdr_t $cdmaterials list, in slot order
//   material_count   mstudiotexture_t slots the model declares
//   vmt_resolved     slots whose .vmt was read and parsed
//   vtf_resolved     slots whose .vtf was read and parsed
//   pixels_decoded   slots whose RGBA decode produced bytes
//   missing_materials slots with no readable .vmt on any candidate path
//   missing_textures  slots with a .vmt but no decodable .vtf
//   invalid_paths     slots whose candidate path the asset root refused
//                     (an escape attempt, or no path at all)
//   oversized_textures slots whose decoded texture exceeds the side cap
//   texture_width/height/format   the first decoded slot's texture
//   has_alpha/bumpmap/phong/selfillum  the same slot's VMT declarations
//   complete         every slot resolved to decoded pixels within the cap
//
// THE SIDE CAP IS A POLICY CONSTANT, NOT A MEASUREMENT
//   kMaxViewModelTextureSide is 2048. The world atlas cap of 512 is deliberately
//   not reused: the ViewModel pass has no atlas, and TF2's own largest
//   ViewModel-side texture measured on this install is 2048 (c_scattergun). A
//   texture above the cap is reported as oversized -- it is not dropped, and
//   the model is not called complete. The constant lives here so the gate pins
//   it; if the product later chooses a different cap, that is a product change
//   and this reading must move with it.
//
// USAGE
//   viewmodel_material_probe --tf-root <tf> --model <a.mdl> [--model <b.mdl> ...]
//   viewmodel_material_probe --fixture-root <root>
// Exit: 0 = every requested model complete; 1 = at least one incomplete;
//       2 = usage error. Per-slot detail goes to stderr; stdout is one JSON
//       object per model plus a summary line.

#include "asset_root.h"
#include "model_loader.h"
#include "viewmodel_material.h"
#include "vmt_material.h"
#include "vpk_archive.h"
#include "vtf_texture.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

// See the header comment: a policy constant the gate pins, not a fact about the
// game. 2048 is the largest side measured on this install's ViewModel-side
// textures (c_scattergun, 2048x2048 DXT5).
constexpr std::uint32_t kMaxViewModelTextureSide = 2048u;

// VTF header flag bits (VTFLib vtf.h). The decoder keys its alpha handling off
// the format; these two bits are the header's own claim about alpha.
constexpr std::uint32_t kVtfFlagOneBitAlpha = 0x00008000u;
constexpr std::uint32_t kVtfFlagEightBitAlpha = 0x00010000u;

// VTF image formats that carry an alpha channel the decoder expands. DXT1
// (13) is opaque; DXT1-with-explicit-alpha (20) is not.
bool vtfFormatHasAlpha(std::uint32_t format) {
  switch (format) {
    case 0: case 1: case 2:   // RGBA8888 / ABGR8888 / BGRA8888
    case 6: case 8:           // IA88 / A8-ish intensity+alpha
    case 11: case 12:         // BGRA5551 / BGRA4444 variants
    case 14: case 15:         // DXT3 / DXT5
    case 16: case 17:         // ARGB1555 / ARGB4444
    case 20:                  // DXT1 with one-bit alpha
      return true;
    default:
      return false;
  }
}

const char* vtfFormatName(std::uint32_t format) {
  switch (format) {
    case 0: return "RGBA8888";
    case 1: return "ABGR8888";
    case 2: return "RGB888";
    case 3: return "BGR888";
    case 4: return "RGB565";
    case 5: return "I8";
    case 6: return "IA88";
    case 8: return "A8";
    case 13: return "DXT1";
    case 14: return "DXT3";
    case 15: return "DXT5";
    case 20: return "DXT1_ONEBITALPHA";
    default: return "other";
  }
}

std::string jsonEscape(const std::string& value) {
  std::string out;
  out.reserve(value.size() + 8);
  for (const char character : value) {
    switch (character) {
      case '\\': out += "\\\\"; break;
      case '"': out += "\\\""; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (static_cast<unsigned char>(character) < 32) out += '?';
        else out += character;
    }
  }
  return out;
}

std::string join(const std::vector<std::string>& values, const char* separator) {
  std::string out;
  for (const auto& value : values) {
    if (!out.empty()) out += separator;
    out += value;
  }
  return out;
}

// The reader is the product's, not a second copy of it. Until 2026-10-12 this
// file carried its own loose-file-then-archive walk, which meant the frozen
// readings and the runtime table were produced by two implementations that had
// no obligation to agree about precedence or about what "rejected" means. The
// gate now asserts the two against each other for the same model and slot, and
// that assertion would be circular if they shared nothing.
using ResourceSource = tf2::native::ViewModelResourceSource;
using ResourceRead = tf2::native::ViewModelResourceRead;

const char* sourceName(ResourceSource source) {
  return tf2::native::viewModelResourceSourceName(source);
}

ResourceRead readResource(const tf2::native::AssetRoot& root, const std::string& resource) {
  return tf2::native::readViewModelResource(root, resource);
}

const char* resolutionName(tf2::native::ModelAssetResolution value) {
  using tf2::native::ModelAssetResolution;
  switch (value) {
    case ModelAssetResolution::FoundLoose: return "found-loose";
    case ModelAssetResolution::FoundVpk: return "found-vpk";
    case ModelAssetResolution::Ambiguous: return "ambiguous";
    case ModelAssetResolution::Missing: return "missing";
    default: return "unknown";
  }
}

struct SlotReport {
  std::size_t slot = 0;
  std::string name;
  std::string status;  // ok / missing-material / missing-texture / invalid-path / oversized-texture
  std::string reason;
  std::string tried;           // candidate stems, in the order tried
  std::string vmtResource;
  std::string vmtShader;
  std::string baseTexture;
  std::string vtfResource;
  std::size_t vmtBytes = 0;
  std::size_t vtfBytes = 0;
  const char* vmtSource = "none";
  const char* vtfSource = "none";
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::uint32_t format = 0;
  std::size_t decodedBytes = 0;
  bool decoded = false;
  bool oversized = false;
  bool hasAlpha = false;
  bool hasBumpmap = false;
  bool hasPhong = false;
  bool hasSelfIllum = false;
  // `$translucent` / `$alphatest` as the VMT declares them. These are the two
  // fields that decide whether the material's alpha channel is *used*: the
  // texture format says the channel exists, the shader key says the engine reads
  // it. Phase 1 of the ViewModel material pass honours exactly these two.
  bool translucent = false;
  bool alphaTest = false;
};

struct ModelReport {
  std::string model;
  std::string resolution = "unknown";
  bool mdlReadable = false;
  std::vector<std::string> cdmaterials;
  std::size_t materialCount = 0;
  std::size_t vmtResolved = 0;
  std::size_t vtfResolved = 0;
  std::size_t pixelsDecoded = 0;
  std::size_t missingMaterials = 0;
  std::size_t missingTextures = 0;
  std::size_t invalidPaths = 0;
  std::size_t oversizedTextures = 0;
  std::size_t decodedBytes = 0;
  std::size_t textureSlot = 0;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::uint32_t format = 0;
  bool hasTexture = false;
  bool hasAlpha = false;
  bool hasBumpmap = false;
  bool hasPhong = false;
  bool hasSelfIllum = false;
  bool complete = false;
  std::string reason;
  // The mesh table decode: which `mstudiotexture_t` slot the mesh that a draw
  // descriptor names actually uses. Reported here because "the model has three
  // slots and all three resolve" says nothing about which one the picture uses,
  // and binding the wrong one is exactly as wrong as binding none.
  bool meshTableAvailable = false;
  std::string meshTableReason;
  std::size_t modelStride = 0;
  std::size_t meshStride = 0;
  std::string modelStrideSource = "none";
  bool modelStrideExercised = false;
  bool meshStrideExercised = false;
  std::size_t meshCount = 0;
  std::size_t descriptorsValidated = 0;
  std::int32_t drawnSlot = -1;
  std::uint32_t drawnBodyPart = 0;
  std::uint32_t drawnModel = 0;
  std::uint32_t drawnMesh = 0;
  std::uint32_t drawnLod = 0;
  std::vector<SlotReport> slots;
};

// One slot, walked exactly the way the entity material path walks it: the
// candidate list in order, first candidate whose VMT *and* VTF both read wins.
// A candidate that fails does not abort the slot -- the miss is recorded and
// the next candidate is tried, so a slot that resolves on its second
// $cdmaterials entry shows both the miss and the hit.
SlotReport resolveSlot(const tf2::native::AssetRoot& root, std::size_t slot,
    const std::string& name, const std::vector<std::string>& candidates) {
  SlotReport report;
  report.slot = slot;
  report.name = name;
  report.tried = join(candidates, ",");
  if (candidates.empty()) {
    report.status = "invalid-path";
    report.reason = "texture slot has no candidate path (bare stem, no $cdmaterials directory)";
    return report;
  }
  bool anyRejected = false;
  bool anyVmtRead = false;
  for (const auto& candidate : candidates) {
    const std::string vmtResource = tf2::native::VmtParser::resourcePath(candidate, ".vmt");
    auto vmtRead = readResource(root, vmtResource);
    if (vmtRead.source == ResourceSource::Rejected) {
      anyRejected = true;
      report.reason = "material path " + vmtResource + " is outside the asset root";
      continue;
    }
    if (vmtRead.source == ResourceSource::None) continue;
    tf2::native::VmtMaterial material;
    if (!tf2::native::VmtParser::parse(std::string(vmtRead.bytes.begin(), vmtRead.bytes.end()), material)) {
      report.reason = "VMT " + vmtResource + " did not parse";
      continue;
    }
    anyVmtRead = true;
    report.vmtBytes = vmtRead.bytes.size();
    report.vmtSource = sourceName(vmtRead.source);
    report.vmtResource = vmtResource;
    report.vmtShader = material.shader;
    if (material.baseTexture.empty()) {
      report.reason = "material " + vmtResource + " declares no $basetexture";
      continue;
    }
    report.baseTexture = material.baseTexture;
    const std::string vtfResource = tf2::native::VmtParser::resourcePath(material.baseTexture, ".vtf");
    auto vtfRead = readResource(root, vtfResource);
    if (vtfRead.source == ResourceSource::Rejected) {
      anyRejected = true;
      report.reason = "texture path " + vtfResource + " is outside the asset root";
      continue;
    }
    if (vtfRead.source == ResourceSource::None) {
      report.reason = "texture " + vtfResource + " is not in the loose files or any archive";
      continue;
    }
    tf2::native::VtfTexture texture;
    std::string parseError;
    if (!texture.parse(vtfRead.bytes, &parseError)) {
      report.reason = "VTF " + vtfResource + " did not parse: " + parseError;
      continue;
    }
    report.vtfBytes = vtfRead.bytes.size();
    report.vtfSource = sourceName(vtfRead.source);
    report.vtfResource = vtfResource;
    report.width = texture.header().width;
    report.height = texture.header().height;
    report.format = texture.header().format;
    const std::uint32_t flags = texture.header().flags;
    report.hasAlpha = vtfFormatHasAlpha(report.format)
      || (flags & (kVtfFlagOneBitAlpha | kVtfFlagEightBitAlpha)) != 0
      || material.translucent || material.alphaTest;
    report.hasBumpmap = !material.bumpMap.empty();
    report.hasPhong = material.phong;
    report.hasSelfIllum = material.selfIllum;
    report.translucent = material.translucent;
    report.alphaTest = material.alphaTest;
    std::string decodeError;
    const auto rgba = texture.decodeRgba(vtfRead.bytes, &decodeError);
    if (rgba.empty()) {
      report.reason = "VTF " + vtfResource + " decoded no pixels: " + decodeError;
      continue;
    }
    report.decoded = true;
    report.decodedBytes = rgba.size();
    report.oversized = report.width > kMaxViewModelTextureSide || report.height > kMaxViewModelTextureSide;
    report.status = report.oversized ? "oversized-texture" : "ok";
    if (report.oversized) {
      report.reason = "texture " + std::to_string(report.width) + "x" + std::to_string(report.height)
        + " exceeds the " + std::to_string(kMaxViewModelTextureSide) + " side cap";
    }
    return report;
  }
  if (anyRejected) {
    report.status = "invalid-path";
    if (report.reason.empty()) report.reason = "every candidate path was refused by the asset root";
    return report;
  }
  if (anyVmtRead) {
    report.status = "missing-texture";
    if (report.reason.empty()) report.reason = "no candidate's texture could be read or decoded";
    return report;
  }
  report.status = "missing-material";
  if (report.reason.empty()) report.reason = "no candidate's VMT is in the loose files or any archive";
  return report;
}

ModelReport buildModelReport(const tf2::native::AssetRoot& root, const std::string& modelPath) {
  ModelReport report;
  report.model = modelPath;
  const auto candidate = tf2::native::ModelLoader::resolveAsset(root, modelPath);
  report.resolution = resolutionName(candidate.resolution);

  tf2::native::ModelInspection inspection;
  bool inspected = false;
  if (candidate.resolution == tf2::native::ModelAssetResolution::FoundLoose) {
    inspection = tf2::native::ModelLoader::inspect(candidate.looseMdl);
    inspected = inspection.mdl.signatureValid && inspection.metadata.valid;
  } else if (candidate.resolution == tf2::native::ModelAssetResolution::FoundVpk
      || candidate.resolution == tf2::native::ModelAssetResolution::Ambiguous) {
    for (const auto& holder : candidate.vpkArchives) {
      const auto* archive = root.archives().find(holder);
      if (!archive) continue;
      inspection = tf2::native::ModelLoader::inspectVpk(*archive, modelPath);
      if (inspection.mdl.signatureValid && inspection.metadata.valid) {
        inspected = true;
        break;
      }
    }
  }
  if (!inspected) {
    report.reason = candidate.resolution == tf2::native::ModelAssetResolution::Missing
      ? "model is not in the loose files or any archive"
      : "model was found but its MDL did not parse";
    return report;
  }
  report.mdlReadable = true;
  report.cdmaterials = inspection.metadata.cdTexturePaths;
  const auto& metadata = inspection.metadata;
  // The descriptor the ViewModel mesh is built from: `buildModelMeshData` is
  // called with index 0 when the model has a decodable mesh, so descriptor 0 is
  // the one whose material the picture uses. Reported even when the mesh table
  // could not be validated -- `-1` is then the reading, not a guess.
  report.meshTableAvailable = metadata.meshMaterials.available;
  report.meshTableReason = metadata.meshMaterials.reason;
  report.modelStride = metadata.meshMaterials.modelStride;
  report.meshStride = metadata.meshMaterials.meshStride;
  report.modelStrideSource = metadata.meshMaterials.modelStrideSource;
  report.modelStrideExercised = metadata.meshMaterials.modelStrideExercised;
  report.meshStrideExercised = metadata.meshMaterials.meshStrideExercised;
  report.meshCount = metadata.meshMaterials.meshCount;
  report.descriptorsValidated = metadata.meshMaterials.descriptorsValidated;
  if (!metadata.indices.empty()) {
    const auto& descriptor = metadata.indices.front();
    report.drawnBodyPart = descriptor.bodyPart;
    report.drawnModel = descriptor.model;
    report.drawnMesh = descriptor.mesh;
    report.drawnLod = descriptor.lod;
    report.drawnSlot = metadata.meshMaterials.slotFor(descriptor.bodyPart, descriptor.model, descriptor.mesh);
  }
  report.materialCount = metadata.textureCandidates.size();
  for (std::size_t slot = 0; slot < metadata.textureCandidates.size(); ++slot) {
    const std::string& name = slot < metadata.textureNames.size() ? metadata.textureNames[slot] : std::string{};
    auto slotReport = resolveSlot(root, slot, name, metadata.textureCandidates[slot]);
    if (slotReport.vmtBytes > 0) ++report.vmtResolved;
    if (slotReport.vtfBytes > 0) ++report.vtfResolved;
    if (slotReport.decoded) {
      ++report.pixelsDecoded;
      report.decodedBytes += slotReport.decodedBytes;
      if (!report.hasTexture) {
        // The per-model texture readings come from the first decoded slot.
        // Every slot's own values are on its stderr line.
        report.hasTexture = true;
        report.textureSlot = slot;
        report.width = slotReport.width;
        report.height = slotReport.height;
        report.format = slotReport.format;
        report.hasAlpha = slotReport.hasAlpha;
        report.hasBumpmap = slotReport.hasBumpmap;
        report.hasPhong = slotReport.hasPhong;
        report.hasSelfIllum = slotReport.hasSelfIllum;
      }
    }
    if (slotReport.status == "missing-material") ++report.missingMaterials;
    else if (slotReport.status == "missing-texture") ++report.missingTextures;
    else if (slotReport.status == "invalid-path") ++report.invalidPaths;
    else if (slotReport.status == "oversized-texture") ++report.oversizedTextures;
    std::fprintf(stderr,
      "vm-material model=%s slot=%zu name=%s candidates=%s status=%s vmt=%s vmt_bytes=%zu vmt_source=%s"
      " shader=%s translucent=%d alphatest=%d base=%s vtf=%s vtf_bytes=%zu vtf_source=%s size=%ux%u format=%s decoded=%zu"
      " alpha=%d bumpmap=%d phong=%d selfillum=%d reason=%s\n",
      modelPath.c_str(), slot, name.empty() ? "<empty>" : name.c_str(),
      slotReport.tried.empty() ? "<none>" : slotReport.tried.c_str(),
      slotReport.status.c_str(),
      slotReport.vmtResource.empty() ? "-" : slotReport.vmtResource.c_str(),
      slotReport.vmtBytes, slotReport.vmtSource,
      slotReport.vmtShader.empty() ? "-" : slotReport.vmtShader.c_str(),
      slotReport.translucent ? 1 : 0, slotReport.alphaTest ? 1 : 0,
      slotReport.baseTexture.empty() ? "-" : slotReport.baseTexture.c_str(),
      slotReport.vtfResource.empty() ? "-" : slotReport.vtfResource.c_str(),
      slotReport.vtfBytes, slotReport.vtfSource,
      slotReport.width, slotReport.height, vtfFormatName(slotReport.format),
      slotReport.decodedBytes, slotReport.hasAlpha ? 1 : 0, slotReport.hasBumpmap ? 1 : 0,
      slotReport.hasPhong ? 1 : 0, slotReport.hasSelfIllum ? 1 : 0,
      slotReport.reason.empty() ? "-" : slotReport.reason.c_str());
    report.slots.push_back(std::move(slotReport));
  }
  report.complete = report.materialCount > 0 && report.pixelsDecoded == report.materialCount
    && report.missingMaterials == 0 && report.missingTextures == 0
    && report.invalidPaths == 0 && report.oversizedTextures == 0;
  if (!report.complete && report.reason.empty()) {
    const std::string tally = std::to_string(report.pixelsDecoded) + " of "
      + std::to_string(report.materialCount) + " slots resolved to decoded pixels";
    if (report.materialCount == 0) {
      report.reason = "model declares no texture slots";
    } else {
      // Name the first slot that did not reach decoded pixels. The tally alone
      // tells a reader of stdout that the model is incomplete but not why; the
      // per-slot reasons were already on stderr, and a consumer that reads
      // only the JSON line deserves the cause on the same line.
      for (const auto& slot : report.slots) {
        if (!slot.reason.empty()) {
          report.reason = "slot " + std::to_string(slot.slot) + ": " + slot.reason
            + " (" + tally + ")";
          break;
        }
      }
      if (report.reason.empty()) report.reason = tally;
    }
  }
  return report;
}

void printModelReport(const ModelReport& report) {
  std::printf("{\"model\":\"%s\"", jsonEscape(report.model).c_str());
  std::printf(",\"resolution\":\"%s\"", jsonEscape(report.resolution).c_str());
  std::printf(",\"mdlReadable\":%s", report.mdlReadable ? "true" : "false");
  std::printf(",\"cdmaterials\":\"%s\"", jsonEscape(join(report.cdmaterials, "|")).c_str());
  std::printf(",\"material_count\":%zu", report.materialCount);
  std::printf(",\"vmt_resolved\":%zu", report.vmtResolved);
  std::printf(",\"vtf_resolved\":%zu", report.vtfResolved);
  std::printf(",\"pixels_decoded\":%zu", report.pixelsDecoded);
  std::printf(",\"missing_materials\":%zu", report.missingMaterials);
  std::printf(",\"missing_textures\":%zu", report.missingTextures);
  std::printf(",\"invalid_paths\":%zu", report.invalidPaths);
  std::printf(",\"oversized_textures\":%zu", report.oversizedTextures);
  std::printf(",\"decoded_bytes\":%zu", report.decodedBytes);
  std::printf(",\"texture_slot\":%zu", report.textureSlot);
  std::printf(",\"texture_width\":%u", report.width);
  std::printf(",\"texture_height\":%u", report.height);
  std::printf(",\"texture_format\":%u", report.format);
  std::printf(",\"has_texture\":%s", report.hasTexture ? "true" : "false");
  std::printf(",\"has_alpha\":%s", report.hasAlpha ? "true" : "false");
  std::printf(",\"has_bumpmap\":%s", report.hasBumpmap ? "true" : "false");
  std::printf(",\"has_phong\":%s", report.hasPhong ? "true" : "false");
  std::printf(",\"has_selfillum\":%s", report.hasSelfIllum ? "true" : "false");
  std::printf(",\"complete\":%s", report.complete ? "true" : "false");
  std::printf(",\"mesh_table\":%s", report.meshTableAvailable ? "true" : "false");
  std::printf(",\"model_stride\":%zu", report.modelStride);
  std::printf(",\"mesh_stride\":%zu", report.meshStride);
  std::printf(",\"model_stride_source\":\"%s\"", jsonEscape(report.modelStrideSource).c_str());
  std::printf(",\"model_stride_exercised\":%s", report.modelStrideExercised ? "true" : "false");
  std::printf(",\"mesh_stride_exercised\":%s", report.meshStrideExercised ? "true" : "false");
  std::printf(",\"mesh_count\":%zu", report.meshCount);
  std::printf(",\"descriptors_validated\":%zu", report.descriptorsValidated);
  std::printf(",\"drawn_slot\":%d", report.drawnSlot);
  std::printf(",\"drawn_bodypart\":%u", report.drawnBodyPart);
  std::printf(",\"drawn_model\":%u", report.drawnModel);
  std::printf(",\"drawn_mesh\":%u", report.drawnMesh);
  std::printf(",\"drawn_lod\":%u", report.drawnLod);
  std::printf(",\"mesh_table_reason\":\"%s\"", jsonEscape(report.meshTableReason).c_str());
  std::printf(",\"reason\":\"%s\"}\n", jsonEscape(report.reason).c_str());
}

// The fixture set, pinned here rather than discovered by scanning the fixture
// root: a directory listing is a property of the machine, and a fixture that
// silently failed to appear must be a red gate, not an empty scan.
const char* kFixtureModels[] = {
  "models/weapons/v_models/vmfix_ok.mdl",
  "models/weapons/v_models/vmfix_no_vmt.mdl",
  "models/weapons/v_models/vmfix_no_vtf.mdl",
  "models/weapons/v_models/vmfix_bad_cdmaterials.mdl",
  "models/weapons/v_models/vmfix_backslash.mdl",
  "models/weapons/v_models/vmfix_dxt5.mdl",
  "models/weapons/v_models/vmfix_oversized.mdl",
  "models/weapons/v_models/vmfix_bare.mdl",
  "models/weapons/v_models/vmfix_escape.mdl",
  "models/weapons/v_models/vmfix_cubemap.mdl",
};

void usage() {
  std::cerr << "usage: viewmodel_material_probe --tf-root <tf> --model <path.mdl> [--model <path.mdl> ...]\n"
               "       viewmodel_material_probe --fixture-root <root>\n";
}

}  // namespace

int main(int argc, char** argv) {
  std::filesystem::path tfRoot;
  std::filesystem::path fixtureRoot;
  std::vector<std::string> models;
  for (int index = 1; index < argc; ++index) {
    const std::string argument = argv[index];
    if (argument == "--tf-root" && index + 1 < argc) {
      tfRoot = std::filesystem::u8path(argv[++index]);
    } else if (argument == "--fixture-root" && index + 1 < argc) {
      fixtureRoot = std::filesystem::u8path(argv[++index]);
    } else if (argument == "--model" && index + 1 < argc) {
      models.emplace_back(argv[++index]);
    } else {
      usage();
      return 2;
    }
  }
  std::vector<std::string> requested;
  std::filesystem::path root;
  if (!fixtureRoot.empty()) {
    if (!tfRoot.empty() || !models.empty()) {
      usage();
      return 2;
    }
    root = fixtureRoot;
    requested.assign(std::begin(kFixtureModels), std::end(kFixtureModels));
  } else {
    if (tfRoot.empty() || models.empty()) {
      usage();
      return 2;
    }
    root = tfRoot;
    requested = std::move(models);
  }

  const auto assets = tf2::native::AssetRoot::fromPath(root);
  if (!assets.valid()) {
    std::cerr << "FATAL: " << root.string() << " is not a valid asset root"
      " (needs materials/, models/ and maps/ stores)\n";
    return 2;
  }

  std::size_t slots = 0, vmtResolved = 0, vtfResolved = 0, pixelsDecoded = 0;
  std::size_t missingMaterials = 0, missingTextures = 0, invalidPaths = 0, oversizedTextures = 0;
  std::size_t complete = 0;
  for (const auto& model : requested) {
    const auto report = buildModelReport(assets, model);
    printModelReport(report);
    std::fprintf(stderr,
      "vm-mesh-material model=%s meshTable=%d modelStride=%zu(%s,exercised=%d) meshStride=%zu(exercised=%d) meshes=%zu"
      " descriptorsValidated=%zu drawn=(bodyPart %u, model %u, mesh %u, lod %u) drawnSlot=%d reason=%s\n",
      model.c_str(), report.meshTableAvailable ? 1 : 0, report.modelStride,
      report.modelStrideSource.c_str(), report.modelStrideExercised ? 1 : 0, report.meshStride,
      report.meshStrideExercised ? 1 : 0, report.meshCount,
      report.descriptorsValidated, report.drawnBodyPart, report.drawnModel,
      report.drawnMesh, report.drawnLod, report.drawnSlot,
      report.meshTableReason.empty() ? "-" : report.meshTableReason.c_str());
    slots += report.materialCount;
    vmtResolved += report.vmtResolved;
    vtfResolved += report.vtfResolved;
    pixelsDecoded += report.pixelsDecoded;
    missingMaterials += report.missingMaterials;
    missingTextures += report.missingTextures;
    invalidPaths += report.invalidPaths;
    oversizedTextures += report.oversizedTextures;
    if (report.complete) ++complete;
  }
  std::printf("{\"summary\":true,\"models\":%zu,\"complete\":%zu,\"slots\":%zu"
    ",\"vmt_resolved\":%zu,\"vtf_resolved\":%zu,\"pixels_decoded\":%zu"
    ",\"missing_materials\":%zu,\"missing_textures\":%zu,\"invalid_paths\":%zu"
    ",\"oversized_textures\":%zu}\n",
    requested.size(), complete, slots, vmtResolved, vtfResolved, pixelsDecoded,
    missingMaterials, missingTextures, invalidPaths, oversizedTextures);
  return complete == requested.size() ? 0 : 1;
}
