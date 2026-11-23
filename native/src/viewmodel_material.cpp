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
    if (uploader && (uploadSlot == -2 || (uploadSlot >= 0
        && static_cast<std::size_t>(uploadSlot) == slot.slot))) {
      if (uploader(slot.vtfPath, slot.width, slot.height, rgba)) ++summary.uploaded;
    }
  }
  // The retained VTF bytes have done their job. Keeping them would hold one
  // compressed copy of every slot alive for the whole run to serve a bind path
  // that reads the GPU cache instead.
  for (auto& slot : table.slots) slot.vtfBytesData.clear();
  return summary;
}

// ---------------------------------------------------------------------------
// T2: Phong, Bump and Selfillum
// ---------------------------------------------------------------------------

const char* viewModelEffectStateName(ViewModelEffectState state) {
  switch (state) {
    case ViewModelEffectState::Unavailable: return "unavailable";
    case ViewModelEffectState::Declared: return "declared";
    case ViewModelEffectState::Parsed: return "parsed";
    case ViewModelEffectState::Active: return "active";
    case ViewModelEffectState::Fallback: return "fallback";
  }
  return "unknown";
}

namespace {

// The VMT's own field map, which carries every non-object scalar verbatim.
// Reading `$phongexponent` this way keeps vmt_material.h frozen for the tracks
// that include it, and it means a key this reader does not know is still
// visible in `fields` rather than being dropped at parse time.
std::string effectField(const VmtMaterial& material, const char* key) {
  const auto it = material.fields.find(key);
  return it != material.fields.end() ? it->second : std::string{};
}

// A scalar, or false. A present-but-malformed number is a DIFFERENT result
// from an absent key: the material asked for something and the reader refused
// it, which is the row `parsed=false` exists for.
bool effectNumber(const std::string& text, float& out) {
  if (text.empty()) return false;
  const char* begin = text.c_str();
  char* end = nullptr;
  const double value = std::strtod(begin, &end);
  if (end == begin || (end && *end != '\0')) return false;
  if (!std::isfinite(value)) return false;
  out = static_cast<float>(value);
  return true;
}

// Source's own vector argument, `[a b c]` (or `[a b]`) with any whitespace
// separator. Tolerated because every shipped tint and Fresnel range uses it, and
// refused by name otherwise.
//
// `minValues`/`maxValues` bound how many components the caller will accept: a
// tint is three, a Fresnel range is two or three. A vector with the wrong
// component count is a refusal, not a truncation -- reading `[0 0.5 1]` as the
// pair (0, 0.5) would silently halve every highlight.
bool effectVector(const std::string& text, float out[4], int minValues, int maxValues,
    int& count) {
  count = 0;
  if (text.empty()) return false;
  std::string body = text;
  const std::size_t open = body.find('[');
  if (open != std::string::npos) {
    const std::size_t close = body.find(']', open);
    if (close == std::string::npos) return false;
    body = body.substr(open + 1, close - open - 1);
  }
  std::vector<float> values;
  std::size_t cursor = 0;
  while (cursor < body.size()) {
    while (cursor < body.size() && std::isspace(static_cast<unsigned char>(body[cursor]))) ++cursor;
    if (cursor >= body.size()) break;
    std::size_t end = cursor;
    while (end < body.size() && !std::isspace(static_cast<unsigned char>(body[end]))) ++end;
    float value = 0.0f;
    if (!effectNumber(body.substr(cursor, end - cursor), value)) return false;
    values.push_back(value);
    cursor = end;
  }
  if (static_cast<int>(values.size()) < minValues
      || static_cast<int>(values.size()) > maxValues) {
    return false;
  }
  for (std::size_t i = 0; i < values.size(); ++i) {
    out[i] = std::clamp(values[i], -64.0f, 64.0f);
  }
  count = static_cast<int>(values.size());
  return true;
}

bool effectVector3(const std::string& text, float out[3]) {
  float values[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  int count = 0;
  if (!effectVector(text, values, 3, 3, count)) return false;
  for (int i = 0; i < 3; ++i) out[i] = values[i];
  return true;
}

// Source's own default for VertexLitGeneric's `$phongexponent`. Named rather
// than inlined so the fixture can assert the default and not just the override.
constexpr float kSourceDefaultPhongExponent = 5.0f;

} // namespace

ViewModelPhongReading readViewModelPhong(const VmtMaterial& material) {
  ViewModelPhongReading reading;
  reading.declared = material.phong;
  reading.exponentDeclared = !effectField(material, "$phongexponent").empty();
  reading.boostDeclared = !effectField(material, "$phongboost").empty();
  reading.fresnelDeclared = !effectField(material, "$phongfresnelranges").empty();
  reading.tintDeclared = !material.phongTint.empty();
  if (!reading.declared) {
    reading.state = ViewModelEffectState::Unavailable;
    reading.reason = "the material declares no $phong";
    return reading;
  }
  reading.state = ViewModelEffectState::Declared;
  reading.reason = "the material declares $phong";

  // Every parameter is optional. A present-but-malformed one is reported and
  // the parameter is left at its default, so a material asking for something
  // unreadable is declared and NOT parsed rather than silently discounted.
  bool parsed = true;
  if (reading.exponentDeclared) {
    float value = 0.0f;
    if (effectNumber(effectField(material, "$phongexponent"), value)
        && value >= 0.0f && value <= 512.0f) {
      reading.exponent = value;
      reading.reason += "; $phongexponent=" + std::to_string(value);
    } else {
      parsed = false;
      reading.reason += "; $phongexponent=\"" + effectField(material, "$phongexponent")
        + "\" is not a number in 0..512";
    }
  }
  if (reading.boostDeclared) {
    float value = 0.0f;
    if (effectNumber(effectField(material, "$phongboost"), value)
        && value >= 0.0f && value <= 64.0f) {
      reading.boost = value;
      reading.reason += "; $phongboost=" + std::to_string(value);
    } else {
      parsed = false;
      reading.reason += "; $phongboost is not a number in 0..64";
    }
  }
  if (reading.fresnelDeclared) {
    // Source's `$phongfresnelranges` is a THREE-component vector `[min mid max]`
    // -- measured on this install, where the first ViewModel material that
    // declares `$phong 1` also declares `[0 0.5 1]`. The reading keeps two
    // numbers (the two ends) because that is what the shader's Fresnel lerp
    // consumes, and the middle value is the knee of a curve the two-point lerp
    // approximates. A vector with two components is also accepted, because
    // hand-written materials use that form; anything else is refused BY NAME
    // rather than truncated, since reading `[0 0.5 1]` as the pair (0, 0.5)
    // would halve every highlight and look like a working shader.
    float values[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    int count = 0;
    const std::string text = effectField(material, "$phongfresnelranges");
    if (effectVector(text, values, 2, 3, count)) {
      const float lo = values[0];
      const float hi = count >= 3 ? values[2] : values[1];
      if (lo <= hi) {
        reading.fresnel[0] = std::clamp(lo, 0.0f, 1.0f);
        reading.fresnel[1] = std::clamp(hi, 0.0f, 1.0f);
        reading.reason += "; $phongfresnelranges=" + std::to_string(reading.fresnel[0])
          + ".." + std::to_string(reading.fresnel[1]) + " (" + std::to_string(count)
          + " components)";
      } else {
        parsed = false;
        reading.reason += "; $phongfresnelranges has its low end above its high end";
      }
    } else {
      parsed = false;
      reading.reason += "; $phongfresnelranges=\"" + text
        + "\" did not read as two or three numbers";
    }
  }
  if (reading.tintDeclared) {
    if (effectVector3(material.phongTint, reading.tint)) {
      reading.reason += "; $phongtint parsed";
    } else {
      parsed = false;
      reading.reason += "; $phongtint=\"" + material.phongTint + "\" is not three numbers";
    }
  }
  reading.parsed = parsed;
  reading.state = parsed ? ViewModelEffectState::Parsed : ViewModelEffectState::Declared;
  reading.reason += parsed ? "; every present parameter parsed" : "; a parameter failed to parse";
  return reading;
}

ViewModelBumpReading readViewModelBump(const VmtMaterial& material) {
  ViewModelBumpReading reading;
  // `$bumpmap` is the legacy key and `$normalmap` the current one. A shipped
  // material carries either; both are reported so the two files cannot be
  // confused for one another by a reader.
  reading.declared = !material.bumpMap.empty() || !effectField(material, "$normalmap").empty();
  reading.bumpMapPath = material.bumpMap;
  reading.normalMapPath = effectField(material, "$normalmap");
  // `$normalmapalphaenvmapmask` makes the normal map's alpha carry the env mask,
  // which is a different use of the same channel and is recorded rather than
  // interpreted.
  const std::string maskKey = effectField(material, "$normalmapalphaenvmapmask");
  reading.alphaEnvMapMask = maskKey == "1" || maskKey == "true";
  if (!reading.declared) {
    reading.state = ViewModelEffectState::Unavailable;
    reading.reason = "the material declares no $normalmap or $bumpmap";
    return reading;
  }
  // The bound path is the current key when both are present, because that is
  // what Source's own shader picks. Recorded so a fixture that puts different
  // bytes behind the two keys can tell which one the pass would read.
  reading.boundPath = !reading.normalMapPath.empty() ? reading.normalMapPath
                                                     : reading.bumpMapPath;
  reading.parsed = true;
  reading.state = ViewModelEffectState::Parsed;
  reading.reason = "normal map " + reading.boundPath + " named by "
    + (reading.normalMapPath.empty() ? "$bumpmap" : "$normalmap")
    + (reading.alphaEnvMapMask ? "; $normalmapalphaenvmapmask is on"
                               : "; no env mask from the normal map's alpha");
  return reading;
}

ViewModelSelfillumReading readViewModelSelfillum(const VmtMaterial& material) {
  ViewModelSelfillumReading reading;
  reading.declared = material.selfIllum;
  reading.maskDeclared = !effectField(material, "$selfillummask").empty();
  reading.tintDeclared = !material.selfIllumTint.empty();
  if (!reading.declared) {
    reading.state = ViewModelEffectState::Unavailable;
    reading.reason = "the material declares no $selfillum";
    return reading;
  }
  reading.state = ViewModelEffectState::Declared;
  reading.reason = "the material declares $selfillum";
  bool parsed = true;
  if (reading.maskDeclared) {
    reading.maskPath = effectField(material, "$selfillummask");
    reading.reason += "; $selfillummask=" + reading.maskPath;
  } else {
    // Source's own default: with no `$selfillummask` the mask is the albedo's
    // alpha. That is a stated decision, not a missing file read as black.
    reading.maskFromAlpha = true;
    reading.reason += "; no $selfillummask so Source masks by the albedo's alpha";
  }
  if (reading.tintDeclared) {
    if (effectVector3(material.selfIllumTint, reading.tint)) {
      reading.reason += "; $selfillumtint parsed";
    } else {
      parsed = false;
      reading.reason += "; $selfillumtint=\"" + material.selfIllumTint
        + "\" is not three numbers";
    }
  }
  reading.parsed = parsed;
  reading.state = parsed ? ViewModelEffectState::Parsed : ViewModelEffectState::Declared;
  reading.reason += parsed ? "; every present parameter parsed" : "; a parameter failed to parse";
  return reading;
}

ViewModelRangeEffects evaluateViewModelRangeEffects(
    const ViewModelPhongReading& phong, const ViewModelBumpReading& bump,
    const ViewModelSelfillumReading& selfillum, bool textureBound, bool shaderReady) {
  ViewModelRangeEffects effects;
  if (!shaderReady) {
    // The three share one pixel shader, so a branch that did not compile is a
    // fallback for all of them rather than for whichever one is being asked
    // about. Naming the shared reason once is what stops three separate
    // "the shader is missing" sentences from implying three separate defects.
    effects.reason = "the viewmodel pixel shader is not ready, so no advanced"
      " material branch can run";
    return effects;
  }
  if (!textureBound) {
    // A material on a range whose albedo never resolved. Declared is not
    // applied, and this is the case that distinction exists for.
    effects.reason = "this range drew the flat-colour fallback, so no advanced"
      " material branch had a texture to work on";
    return effects;
  }
  // Phong needs a parsed exponent; a material that declared `$phong 1` and
  // nothing else uses the engine default, which is a valid Phong.
  effects.phongApplied = phong.declared && phong.parsed;
  effects.bumpApplied = bump.declared && bump.parsed;
  effects.selfillumApplied = selfillum.declared && selfillum.parsed;
  effects.anyApplied = effects.phongApplied || effects.bumpApplied
    || effects.selfillumApplied;
  if (!effects.anyApplied) {
    effects.reason = "this range's material declares none of phong, bump or"
      " selfillum, so the plain albedo branch ran";
  } else {
    std::vector<std::string> names;
    if (effects.phongApplied) names.push_back("phong");
    if (effects.bumpApplied) names.push_back("bump");
    if (effects.selfillumApplied) names.push_back("selfillum");
    effects.reason = "this range's material applied " + join(names, " + ");
  }
  return effects;
}

} // namespace tf2::native
