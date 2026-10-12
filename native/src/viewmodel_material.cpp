#include "viewmodel_material.h"

#include <algorithm>
#include <cctype>
#include <fstream>

#include "vmt_material.h"
#include "vtf_texture.h"

namespace tf2::native {

namespace {

// The 2048-side policy the frozen probe pins. It is a policy constant, not a
// fact about the game: 2048 is the largest side measured on this install's
// ViewModel-side textures (`c_scattergun`, 2048x2048 DXT5).
constexpr std::uint32_t kMaxViewModelTextureSide = 2048u;

// VTF header flag bits (VTFLib vtf.h). The decoder keys its alpha handling off
// the format; these two bits are the header's own claim about alpha.
constexpr std::uint32_t kVtfFlagOneBitAlpha = 0x00008000u;
constexpr std::uint32_t kVtfFlagEightBitAlpha = 0x00010000u;

// VTF image formats that carry an alpha channel the decoder expands. DXT1 (13)
// is opaque; DXT1-with-explicit-alpha (20) is not.
bool vtfFormatHasAlpha(std::uint32_t format) {
  switch (format) {
    case 0: case 1: case 2:   // RGBA8888 / ABGR8888 / BGRA8888
    case 6: case 8:           // IA88 / A8
    case 11: case 12:         // BGRA5551 / BGRA4444
    case 14: case 15:         // DXT3 / DXT5
    case 16: case 17:         // ARGB1555 / ARGB4444
    case 20:                  // DXT1 with one-bit alpha
      return true;
    default:
      return false;
  }
}

std::string join(const std::vector<std::string>& values, const char* separator) {
  std::string out;
  for (const auto& value : values) {
    if (!out.empty()) out += separator;
    out += value;
  }
  return out;
}

bool readableName(const std::string& value) {
  return std::all_of(value.begin(), value.end(),
    [](unsigned char character) { return character >= 32 && character <= 126; });
}

} // namespace

const char* viewModelResourceSourceName(ViewModelResourceSource source) {
  switch (source) {
    case ViewModelResourceSource::Loose: return "loose";
    case ViewModelResourceSource::Vpk: return "vpk";
    case ViewModelResourceSource::Rejected: return "rejected";
    default: return "none";
  }
}

ViewModelResourceRead readViewModelResource(const AssetRoot& root, const std::string& resource) {
  ViewModelResourceRead out;
  const auto resolved = root.resolve(std::filesystem::u8path(resource));
  if (resolved.empty()) {
    out.source = ViewModelResourceSource::Rejected;
    return out;
  }
  std::error_code ec;
  if (std::filesystem::is_regular_file(resolved, ec) && !ec) {
    const auto size = std::filesystem::file_size(resolved, ec);
    if (!ec && size > 0 && size <= 256ull * 1024ull * 1024ull) {
      std::ifstream input(resolved, std::ios::binary);
      if (input) {
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
        input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (input && static_cast<std::size_t>(input.gcount()) == bytes.size()) {
          out.bytes = std::move(bytes);
          out.source = ViewModelResourceSource::Loose;
          return out;
        }
      }
    }
  }
  std::vector<std::filesystem::path> holders;
  root.archives().collectContaining(resource, holders);
  for (const auto& holder : holders) {
    const auto* archive = root.archives().find(holder);
    if (!archive) continue;
    auto bytes = archive->read(resource);
    if (!bytes.empty()) {
      out.bytes = std::move(bytes);
      out.source = ViewModelResourceSource::Vpk;
      return out;
    }
  }
  return out;
}

const char* viewModelMaterialStatusName(ViewModelMaterialStatus status) {
  switch (status) {
    case ViewModelMaterialStatus::MissingMaterial: return "missing-material";
    case ViewModelMaterialStatus::MissingTexture: return "missing-texture";
    case ViewModelMaterialStatus::InvalidPath: return "invalid-path";
    case ViewModelMaterialStatus::OversizedTexture: return "oversized-texture";
    default: return "resolved";
  }
}

namespace {

// One slot, walked the way the frozen probe walks it: the candidate list in
// order, and the first candidate whose VMT *and* VTF both read wins. A candidate
// that fails does not abort the slot -- the miss is recorded and the next
// candidate is tried, so a slot that resolves on its second `$cdmaterials` entry
// shows both the miss and the hit.
void resolveSlot(const AssetRoot& root, std::size_t slotIndex, const std::string& name,
    const std::vector<std::string>& candidates, ViewModelMaterialSlot& out) {
  out.slot = slotIndex;
  out.name = readableName(name) ? name : std::string{};
  out.candidates = join(candidates, ",");
  if (candidates.empty()) {
    out.status = ViewModelMaterialStatus::InvalidPath;
    out.reason = "texture slot has no candidate path (bare stem, no $cdmaterials directory)";
    return;
  }
  bool anyRejected = false;
  bool anyVmtRead = false;
  for (const auto& candidate : candidates) {
    if (candidate.empty()) continue;
    const std::string vmtResource = VmtParser::resourcePath(candidate, ".vmt");
    auto vmtRead = readViewModelResource(root, vmtResource);
    if (vmtRead.source == ViewModelResourceSource::Rejected) {
      anyRejected = true;
      out.reason = "material path " + vmtResource + " is outside the asset root";
      continue;
    }
    if (vmtRead.source == ViewModelResourceSource::None) continue;
    VmtMaterial material;
    if (!VmtParser::parse(std::string(vmtRead.bytes.begin(), vmtRead.bytes.end()), material)) {
      out.reason = "VMT " + vmtResource + " did not parse";
      continue;
    }
    anyVmtRead = true;
    out.vmtBytes = vmtRead.bytes.size();
    out.vmtPath = vmtResource;
    out.shader = material.shader;
    if (material.baseTexture.empty()) {
      out.reason = "material " + vmtResource + " declares no $basetexture";
      continue;
    }
    out.baseTexture = material.baseTexture;
    const std::string vtfResource = VmtParser::resourcePath(material.baseTexture, ".vtf");
    auto vtfRead = readViewModelResource(root, vtfResource);
    if (vtfRead.source == ViewModelResourceSource::Rejected) {
      anyRejected = true;
      out.reason = "texture path " + vtfResource + " is outside the asset root";
      continue;
    }
    if (vtfRead.source == ViewModelResourceSource::None) {
      out.reason = "texture " + vtfResource + " is not in the loose files or any archive";
      continue;
    }
    VtfTexture texture;
    std::string parseError;
    if (!texture.parse(vtfRead.bytes, &parseError)) {
      out.reason = "VTF " + vtfResource + " did not parse: " + parseError;
      continue;
    }
    out.vtfBytes = vtfRead.bytes.size();
    out.vtfPath = vtfResource;
    out.width = texture.header().width;
    out.height = texture.header().height;
    out.format = texture.header().format;
    const std::uint32_t flags = texture.header().flags;
    out.alpha = vtfFormatHasAlpha(out.format)
      || (flags & (kVtfFlagOneBitAlpha | kVtfFlagEightBitAlpha)) != 0
      || material.translucent || material.alphaTest;
    out.translucent = material.translucent;
    out.alphaTest = material.alphaTest;
    out.bump = !material.bumpMap.empty();
    out.phong = material.phong;
    out.selfIllum = material.selfIllum;
    out.oversized = out.width > kMaxViewModelTextureSide || out.height > kMaxViewModelTextureSide;
    out.oversizeReason = out.oversized
      ? ("texture " + std::to_string(out.width) + "x" + std::to_string(out.height)
         + " exceeds the " + std::to_string(kMaxViewModelTextureSide) + " side cap")
      : std::string();
    out.vtfBytesData = vtfRead.bytes;
    // The VTF parsed and the decoder has not been asked for pixels yet. Whether
    // the slot reaches `Resolved` is decided by the decode pass, which is where
    // the probe decides it too -- a cubemap parses perfectly and yields nothing.
    out.status = out.oversized
      ? ViewModelMaterialStatus::OversizedTexture
      : ViewModelMaterialStatus::Resolved;
    if (out.oversized) out.reason = out.oversizeReason;
    return;
  }
  if (anyRejected) {
    out.status = ViewModelMaterialStatus::InvalidPath;
    if (out.reason.empty()) out.reason = "every candidate path was refused by the asset root";
    return;
  }
  if (anyVmtRead) {
    out.status = ViewModelMaterialStatus::MissingTexture;
    if (out.reason.empty()) out.reason = "no candidate's texture could be read or decoded";
    return;
  }
  out.status = ViewModelMaterialStatus::MissingMaterial;
  if (out.reason.empty()) out.reason = "no candidate's VMT is in the loose files or any archive";
}

} // namespace

std::size_t ViewModelMaterialTable::materialResolved() const {
  std::size_t count = 0;
  for (const auto& slot : slots) {
    if (slot.pixelsDecoded) ++count;
  }
  return count;
}

std::size_t ViewModelMaterialTable::fallbackSlots() const {
  std::size_t count = 0;
  for (const auto& slot : slots) {
    if (slot.status != ViewModelMaterialStatus::Resolved || !slot.pixelsDecoded) ++count;
  }
  return count;
}

std::size_t ViewModelMaterialTable::alphaSlots() const {
  return static_cast<std::size_t>(std::count_if(slots.begin(), slots.end(),
    [](const ViewModelMaterialSlot& slot) { return slot.pixelsDecoded && slot.alpha; }));
}

std::size_t ViewModelMaterialTable::translucentSlots() const {
  return static_cast<std::size_t>(std::count_if(slots.begin(), slots.end(),
    [](const ViewModelMaterialSlot& slot) { return slot.pixelsDecoded && slot.translucent; }));
}

std::size_t ViewModelMaterialTable::alphaTestSlots() const {
  return static_cast<std::size_t>(std::count_if(slots.begin(), slots.end(),
    [](const ViewModelMaterialSlot& slot) { return slot.pixelsDecoded && slot.alphaTest; }));
}

std::size_t ViewModelMaterialTable::phongSlots() const {
  return static_cast<std::size_t>(std::count_if(slots.begin(), slots.end(),
    [](const ViewModelMaterialSlot& slot) { return slot.pixelsDecoded && slot.phong; }));
}

std::size_t ViewModelMaterialTable::bumpSlots() const {
  return static_cast<std::size_t>(std::count_if(slots.begin(), slots.end(),
    [](const ViewModelMaterialSlot& slot) { return slot.pixelsDecoded && slot.bump; }));
}

std::size_t ViewModelMaterialTable::selfillumSlots() const {
  return static_cast<std::size_t>(std::count_if(slots.begin(), slots.end(),
    [](const ViewModelMaterialSlot& slot) { return slot.pixelsDecoded && slot.selfIllum; }));
}

ViewModelMaterialTable resolveViewModelMaterialTable(const AssetRoot& root,
    const ModelMetadata& metadata, std::int32_t seenDescriptorIndex) {
  ViewModelMaterialTable table;
  table.meshTableAvailable = metadata.meshMaterials.available;
  table.drawnSlotReason = metadata.meshMaterials.reason;
  table.modelStride = metadata.meshMaterials.modelStride;
  table.meshStride = metadata.meshMaterials.meshStride;
  table.modelStrideSource = metadata.meshMaterials.modelStrideSource;
  table.modelStrideExercised = metadata.meshMaterials.modelStrideExercised;
  table.meshStrideExercised = metadata.meshMaterials.meshStrideExercised;
  table.meshCount = metadata.meshMaterials.meshCount;
  table.descriptorsValidated = metadata.meshMaterials.descriptorsValidated;

  // Which slot the picture uses. Read from the MDL mesh table for the descriptor
  // the mesh was built from -- not "slot 0", which happens to be the same answer
  // on all 36 shipped viewmodels measured on 2026-10-12 and would have been the
  // wrong answer on a model whose first mesh is not slot 0.
  if (seenDescriptorIndex >= 0
      && static_cast<std::size_t>(seenDescriptorIndex) < metadata.indices.size()) {
    const auto& descriptor = metadata.indices[static_cast<std::size_t>(seenDescriptorIndex)];
    table.drawnSlot = metadata.meshMaterials.slotFor(descriptor.bodyPart, descriptor.model, descriptor.mesh);
    if (table.drawnSlot >= 0) {
      table.drawnSlotSource = "mdl-mesh-table";
    } else {
      table.drawnSlotSource = "unresolved";
      if (!table.meshTableAvailable) {
        table.drawnSlotReason = "the MDL mesh table could not be validated: " + table.drawnSlotReason;
      } else {
        table.drawnSlotReason = "descriptor (bodyPart " + std::to_string(descriptor.bodyPart)
          + ", model " + std::to_string(descriptor.model) + ", mesh " + std::to_string(descriptor.mesh)
          + ") is not in the validated mesh table";
      }
    }
  } else {
    table.drawnSlotSource = "unresolved";
    table.drawnSlotReason = metadata.indices.empty()
      ? "the model has no decoded VTX index descriptor"
      : "the mesh was built from a descriptor index outside the decoded list";
  }

  // The drawn slot has to be a real slot index. A validated mesh table whose
  // material is past the texture table would make every later reading compare
  // against a slot that does not exist, so the answer is refused here, once,
  // rather than at each consumer.
  if (table.drawnSlot >= 0 && static_cast<std::size_t>(table.drawnSlot) >= metadata.textureCandidates.size()) {
    table.drawnSlotReason = "the mesh table names material slot " + std::to_string(table.drawnSlot)
      + ", past the " + std::to_string(metadata.textureCandidates.size()) + "-slot texture table";
    table.drawnSlot = -1;
    table.drawnSlotSource = "unresolved";
  }

  table.slots.resize(metadata.textureCandidates.size());
  for (std::size_t slot = 0; slot < metadata.textureCandidates.size(); ++slot) {
    const std::string name = slot < metadata.textureNames.size() ? metadata.textureNames[slot] : std::string{};
    resolveSlot(root, slot, name, metadata.textureCandidates[slot], table.slots[slot]);
  }
  return table;
}

bool decodeViewModelMaterialSlot(const ViewModelMaterialSlot& slot,
    std::vector<std::uint8_t>& rgba, std::string& error) {
  rgba.clear();
  error.clear();
  if (slot.vtfBytesData.empty()) {
    error = "no VTF bytes were retained for this slot";
    return false;
  }
  VtfTexture texture;
  if (!texture.parse(slot.vtfBytesData)) {
    error = "the retained VTF bytes did not re-parse";
    return false;
  }
  rgba = texture.decodeRgba(slot.vtfBytesData, &error);
  if (rgba.empty()) {
    if (error.empty()) error = "the decoder returned no pixels";
    return false;
  }
  return true;
}

ViewModelMaterialDecodeSummary decodeViewModelMaterialSlots(ViewModelMaterialTable& table,
    std::int32_t uploadSlot, const ViewModelMaterialUploader& uploader) {
  ViewModelMaterialDecodeSummary summary;
  std::vector<std::uint8_t> rgba;
  for (auto& slot : table.slots) {
    slot.pixelsDecoded = false;
    if (slot.status != ViewModelMaterialStatus::Resolved) continue;
    std::string error;
    if (!decodeViewModelMaterialSlot(slot, rgba, error)) {
      // A slot that parsed but decoded to nothing is not `missing-texture`: the
      // file is right there and its header is consistent. It moves to the same
      // class the probe uses, and the reason carries the decoder's own words.
      slot.status = ViewModelMaterialStatus::MissingTexture;
      slot.reason = "VTF " + slot.vtfPath + " decoded no pixels: " + error;
      ++summary.decodeFailed;
      if (summary.firstFailure.empty()) summary.firstFailure = slot.reason;
      continue;
    }
    slot.pixelsDecoded = true;
    ++summary.decoded;
    summary.decodedBytes += rgba.size();
    if (uploader && uploadSlot >= 0 && static_cast<std::size_t>(uploadSlot) == slot.slot) {
      if (uploader(slot.vtfPath, slot.width, slot.height, rgba)) ++summary.uploaded;
    }
  }
  // The retained VTF bytes have done their job. Keeping them would hold one
  // compressed copy of every slot alive for the whole run to serve a bind path
  // that reads the GPU cache instead.
  for (auto& slot : table.slots) slot.vtfBytesData.clear();
  return summary;
}

} // namespace tf2::native
