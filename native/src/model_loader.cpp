#include "model_loader.h"
#include "demo_header.h"
#include "item_schema.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <sstream>
#include <unordered_map>

namespace tf2::native {
namespace {
using Bytes = std::vector<std::uint8_t>;
constexpr std::uint32_t kIdStudio = 0x54534449u; // "IDST"
constexpr std::uint32_t kIdVvd = 0x56534449u; // "IDSV"
constexpr std::uint32_t kIdVtx = 0x31585456u; // "VTX1"
constexpr std::size_t kMaxFileBytes = 256ull * 1024ull * 1024ull;
constexpr std::size_t kBoneStride = 216;
constexpr std::size_t kAttachmentStride = 92;
constexpr std::size_t kSequenceStride = 212;
constexpr std::size_t kBodyPartStride = 16;
constexpr std::size_t kVvdVertexStride = 48;
constexpr std::size_t kVtxBodyPartStride = 8;
constexpr std::size_t kVtxModelStride = 8;
constexpr std::size_t kVtxLodStride = 12;
constexpr std::size_t kVtxMeshStride = 9;
constexpr std::size_t kVtxStripGroupStride = 25;
constexpr std::size_t kVtxIndexStride = 2;
constexpr std::size_t kVtxVertexStride = 12;

std::string normalizeModelPath(std::string value) {
  std::replace(value.begin(), value.end(), '\\', '/');
  while (!value.empty() && value.front() == '/') value.erase(value.begin());
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return value;
}

bool addFits(std::size_t a, std::size_t b, std::size_t limit) {
  return a <= limit && b <= limit - a;
}

bool rangeFits(std::size_t offset, std::size_t count, std::size_t stride, std::size_t size) {
  if (stride != 0 && count > (std::numeric_limits<std::size_t>::max)() / stride) return false;
  const auto bytes = count * stride;
  return offset <= size && bytes <= size - offset;
}

Bytes readFile(const std::filesystem::path& path, std::string& error) {
  std::error_code ec;
  const auto size = std::filesystem::file_size(path, ec);
  if (ec) { error = "file_size failed: " + ec.message(); return {}; }
  if (size == 0 || size > kMaxFileBytes) { error = "file size outside safe parser limit"; return {}; }
  std::ifstream in(path, std::ios::binary);
  if (!in) { error = "open failed"; return {}; }
  Bytes bytes(static_cast<std::size_t>(size));
  in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  if (!in || static_cast<std::size_t>(in.gcount()) != bytes.size()) { error = "short read"; return {}; }
  return bytes;
}

template <typename T> bool readAt(const Bytes& b, std::size_t offset, T& out) {
  if (!rangeFits(offset, sizeof(T), 1, b.size())) return false;
  std::memcpy(&out, b.data() + offset, sizeof(T));
  return true;
}

std::string fixedString(const Bytes& b, std::size_t offset, std::size_t maxLength) {
  if (offset >= b.size()) return {};
  const auto length = std::min(maxLength, b.size() - offset);
  std::size_t end = 0;
  while (end < length && b[offset + end] != 0) ++end;
  return std::string(reinterpret_cast<const char*>(b.data() + offset), end);
}

std::string indexedString(const Bytes& b, std::size_t base, std::int32_t index) {
  if (index < 0) return {};
  const auto relative = static_cast<std::size_t>(index);
  if (!addFits(base, relative, b.size())) return {};
  return fixedString(b, base + relative, 4096);
}

bool readVec3(const Bytes& b, std::size_t offset, std::array<float, 3>& value) {
  return readAt(b, offset, value[0]) && readAt(b, offset + 4, value[1]) && readAt(b, offset + 8, value[2]);
}

void parseVvd(const Bytes& b, ModelMetadata& out) {
  if (b.size() < 64) { out.diagnostics.push_back("VVD header is shorter than the known Source header"); return; }
  std::uint32_t version = 0, numLods = 0, numLod0 = 0, vertexDataStart = 0;
  readAt(b, 4, version); readAt(b, 12, numLods); readAt(b, 16, numLod0); readAt(b, 56, vertexDataStart);
  if (version < 4 || version > 5 || numLod0 > 4000000 || !rangeFits(vertexDataStart, numLod0, kVvdVertexStride, b.size())) {
    out.diagnostics.push_back("VVD vertex table is outside the file or uses an unsupported layout");
    return;
  }
  out.vertices.reserve(numLod0);
  std::size_t invalidSkin = 0;
  for (std::uint32_t i = 0; i < numLod0; ++i) {
    const auto off = static_cast<std::size_t>(vertexDataStart) + i * kVvdVertexStride;
    ModelVertex vertex;
    for (std::size_t j = 0; j < 3; ++j) readAt(b, off + j * 4, vertex.weights[j]);
    for (std::size_t j = 0; j < 3; ++j) readAt(b, off + 12 + j, vertex.boneIndices[j]);
    readAt(b, off + 15, vertex.boneCount);
    if (vertex.boneCount > 3) vertex.boneCount = 3;
    readVec3(b, off + 16, vertex.position); readVec3(b, off + 28, vertex.normal);
    readAt(b, off + 40, vertex.texcoord[0]); readAt(b, off + 44, vertex.texcoord[1]);
    bool vertexSkinValid = true;
    const auto weightSum = vertex.weights[0] + vertex.weights[1] + vertex.weights[2];
    if (!std::isfinite(weightSum) || weightSum < 0.99f || weightSum > 1.01f) { ++invalidSkin; vertexSkinValid = false; }
    for (std::size_t j = 0; j < vertex.boneCount; ++j)
      if (vertex.boneIndices[j] >= out.boneCount) { ++invalidSkin; vertexSkinValid = false; break; }
    if (vertex.boneCount > 0 && vertexSkinValid) ++out.skinningVertexCount;
    out.vertices.push_back(vertex);
  }
  if (numLods > 0) out.meshes.push_back(ModelMeshDescriptor{0, 0, numLod0, 0});
  out.invalidSkinVertexCount = invalidSkin;
  out.skinningDataAvailable = !out.vertices.empty() && invalidSkin == 0;
  out.bindPoseMatricesAvailable = !out.bones.empty()
    && std::all_of(out.bones.begin(), out.bones.end(), [](const ModelBone& bone) { return bone.poseToBoneValid; });
  out.skinningBlockedReason = out.bindPoseMatricesAvailable
    ? "animation sequence matrices are not decoded"
    : "MDL poseToBone bind matrices are missing or invalid for the validated header/stride";
  if (invalidSkin != 0) out.diagnostics.push_back("VVD contains vertices with invalid skin indices");
  if (!out.skinningDataAvailable) out.diagnostics.push_back("GPU skinning data unavailable; bind-pose fallback only");
  out.diagnostics.push_back("GPU skinning blocked: " + out.skinningBlockedReason);
}

void parseVtxDiagnostics(const Bytes& b, ModelMetadata& out) {
  auto& d = out.vtxDiagnostics;
  d.bodyPartStride = kVtxBodyPartStride;
  if (b.size() < 64) { d.failureReason = "VTX header is shorter than 64 bytes"; return; }
  d.headerInBounds = true;
  readAt(b, 0, d.version); readAt(b, 24, d.declaredBytes);
  readAt(b, 28, d.bodyPartCount); readAt(b, 32, d.bodyPartOffset);
  d.bodyPartTableInBounds = d.bodyPartCount <= 4096
      && rangeFits(d.bodyPartOffset, d.bodyPartCount, d.bodyPartStride, b.size());
  if (!d.bodyPartTableInBounds) {
    d.failureReason = "bodypart table exceeds VTX file bounds";
    return;
  }
  if (d.bodyPartCount != 0) {
    std::uint32_t modelOffset = 0;
    readAt(b, d.bodyPartOffset, modelOffset);
    d.firstModelOffsetCandidate = modelOffset;
    if (addFits(d.bodyPartOffset, modelOffset, b.size())) {
      d.firstModelAbsoluteCandidate = d.bodyPartOffset + modelOffset;
      d.failureReason = "bodypart boundary verified; model/lod/mesh/stripgroup layout is not yet proven";
    } else {
      d.failureReason = "first model relative offset exceeds VTX file bounds";
    }
  } else {
    d.failureReason = "VTX contains no bodyparts";
  }
}

void parseVtxHierarchy(const Bytes& b, ModelMetadata& out) {
  auto& d = out.vtxDiagnostics;
  if (!d.bodyPartTableInBounds) return;
  for (std::uint32_t bp = 0; bp < d.bodyPartCount; ++bp) {
    const auto bpOff = static_cast<std::size_t>(d.bodyPartOffset) + bp * kVtxBodyPartStride;
    std::uint32_t modelCount = 0, modelRel = 0;
    readAt(b, bpOff, modelCount); readAt(b, bpOff + 4, modelRel);
    if (modelCount > 4096 || !rangeFits(bpOff + modelRel, modelCount, kVtxModelStride, b.size())) { d.failureReason = "model table exceeds VTX file bounds"; return; }
    for (std::uint32_t model = 0; model < modelCount; ++model) {
      const auto modelOff = bpOff + modelRel + model * kVtxModelStride;
      std::uint32_t lodCount = 0, lodRel = 0; readAt(b, modelOff, lodCount); readAt(b, modelOff + 4, lodRel);
      if (lodCount > 32 || !rangeFits(modelOff + lodRel, lodCount, kVtxLodStride, b.size())) { d.failureReason = "LOD table exceeds VTX file bounds"; return; }
      for (std::uint32_t lod = 0; lod < lodCount; ++lod) {
        const auto lodOff = modelOff + lodRel + lod * kVtxLodStride;
        std::uint32_t meshCount = 0, meshRel = 0; readAt(b, lodOff, meshCount); readAt(b, lodOff + 4, meshRel);
        if (meshCount > 4096 || !rangeFits(lodOff + meshRel, meshCount, kVtxMeshStride, b.size())) { d.failureReason = "mesh table exceeds VTX file bounds"; return; }
        for (std::uint32_t mesh = 0; mesh < meshCount; ++mesh) {
          const auto meshOff = lodOff + meshRel + mesh * kVtxMeshStride;
          std::uint32_t groupCount = 0, groupRel = 0; readAt(b, meshOff, groupCount); readAt(b, meshOff + 4, groupRel);
          if (groupCount > 4096 || !rangeFits(meshOff + groupRel, groupCount, kVtxStripGroupStride, b.size())) { d.failureReason = "stripgroup table exceeds VTX file bounds"; return; }
          for (std::uint32_t group = 0; group < groupCount; ++group) {
            const auto groupOff = meshOff + groupRel + group * kVtxStripGroupStride;
            std::uint32_t vertexCount = 0, vertexRel = 0, indexCount = 0, indexRel = 0;
            readAt(b, groupOff, vertexCount); readAt(b, groupOff + 4, vertexRel); readAt(b, groupOff + 8, indexCount); readAt(b, groupOff + 12, indexRel);
            if (vertexCount > 4000000 || indexCount > 12000000 || !rangeFits(groupOff + vertexRel, vertexCount, kVtxVertexStride, b.size()) || !rangeFits(groupOff + indexRel, indexCount, kVtxIndexStride, b.size())) { d.failureReason = "stripgroup arrays exceed VTX file bounds"; return; }
            ModelIndexDescriptor descriptor{bp, model, lod, mesh, group, static_cast<std::uint32_t>(groupOff + indexRel), indexCount, static_cast<std::uint32_t>(groupOff + vertexRel), vertexCount, static_cast<std::uint32_t>(out.renderIndices.size()), indexCount};
            const auto renderStart = out.renderIndices.size();
            d.vtxIndexCount += indexCount;
            bool conversionOk = true;
            for (std::uint32_t i = 0; i < indexCount; ++i) {
              std::uint16_t localIndex = 0;
              readAt(b, groupOff + indexRel + i * kVtxIndexStride, localIndex);
              if (localIndex >= vertexCount) { ++d.vtxOutOfBoundsCount; conversionOk = false; break; }
              std::uint16_t originalIndex = 0;
              if (!readAt(b, groupOff + vertexRel + localIndex * kVtxVertexStride + 4, originalIndex)
                  || originalIndex >= out.vertices.size()) { ++d.vtxOutOfBoundsCount; conversionOk = false; break; }
              out.renderIndices.push_back(originalIndex);
            }
            if (!conversionOk) {
              out.renderIndices.resize(renderStart);
              d.failureReason = "stripgroup index conversion exceeded local or VVD vertex bounds";
              return;
            }
            ++d.vtxDescriptorCount;
            for (std::uint32_t i = 0; i + 2 < indexCount; i += 3) {
              const auto a = out.renderIndices[renderStart + i];
              const auto bIndex = out.renderIndices[renderStart + i + 1];
              const auto c = out.renderIndices[renderStart + i + 2];
              if (a == bIndex || a == c || bIndex == c) ++d.vtxDegenerateTriangleCount;
            }
            out.indices.push_back(descriptor);
          }
        }
      }
    }
  }
  d.failureReason = "VTX hierarchy and stripgroup arrays are within file bounds";
}

ModelFileStatus inspectFile(const std::filesystem::path& path, ModelFileKind kind) {
  ModelFileStatus result;
  result.kind = kind;
  result.path = path;
  std::error_code ec;
  result.exists = std::filesystem::is_regular_file(path, ec) && !ec;
  if (!result.exists) { result.diagnostic = "missing"; return result; }
  result.bytes = std::filesystem::file_size(path, ec);
  std::string error;
  const auto bytes = readFile(path, error);
  if (bytes.size() < 12) { result.diagnostic = error.empty() ? "file too small" : error; return result; }
  std::uint32_t id = 0;
  readAt(bytes, 0, id);
  std::uint32_t version = 0;
  std::uint32_t checksum = 0;
  if (kind == ModelFileKind::Vtx) {
    // Source VTX files have a version-first header; they do not use an IDST/IDSV tag.
    version = id;
    readAt(bytes, 16, checksum);
    result.version = version;
    result.checksum = checksum;
    result.signatureValid = version >= 6 && version <= 8 && checksum != 0;
  } else {
    readAt(bytes, 4, version);
    readAt(bytes, 8, checksum);
    result.version = version;
    result.checksum = checksum;
    const auto expected = kind == ModelFileKind::Mdl ? kIdStudio : kIdVvd;
    result.signatureValid = id == expected;
  }
  if (!result.signatureValid) { result.diagnostic = "unexpected signature or unsupported VTX header"; return result; }
  result.diagnostic = "signature and header prefix valid";
  return result;
}

void parseMdl(const Bytes& b, ModelMetadata& out) {
  if (b.size() < 316) { out.diagnostics.push_back("MDL header is shorter than the known Source studio header"); return; }
  readAt(b, 0, out.id); readAt(b, 4, out.version); readAt(b, 8, out.checksum);
  out.name = fixedString(b, 12, 64);
  readAt(b, 76, out.length); readAt(b, 152, out.flags);
  std::uint32_t numBones = 0, boneIndex = 0, numTextures = 0, textureIndex = 0;
  std::uint32_t numAttachments = 0, attachmentIndex = 0, numSequences = 0, sequenceIndex = 0;
  std::uint32_t numBodyParts = 0, bodyPartIndex = 0;
  readAt(b, 156, numBones); readAt(b, 160, boneIndex);
  readAt(b, 188, numSequences); readAt(b, 192, sequenceIndex);
  readAt(b, 212, numTextures); readAt(b, 216, textureIndex);
  readAt(b, 240, numBodyParts); readAt(b, 244, bodyPartIndex);
  readAt(b, 248, numAttachments); readAt(b, 252, attachmentIndex);
  out.boneCount = numBones; out.sequenceCount = numSequences; out.textureCount = numTextures;
  out.bodyPartCount = numBodyParts; out.attachmentCount = numAttachments;
  const auto saneCount = [&](std::uint32_t count, const char* label) {
    if (count > 4096) { out.diagnostics.emplace_back(std::string(label) + " count exceeds safe limit"); return false; }
    return true;
  };
  if (!saneCount(numBones, "bone") || !rangeFits(boneIndex, numBones, kBoneStride, b.size())) {
    out.diagnostics.push_back("bone table is outside the MDL file or uses an unsupported layout");
  } else {
    out.bones.reserve(numBones);
    for (std::uint32_t i = 0; i < numBones; ++i) {
      const auto off = static_cast<std::size_t>(boneIndex) + i * kBoneStride;
      std::int32_t nameIndex = -1, parent = -1;
      readAt(b, off, nameIndex); readAt(b, off + 4, parent);
      ModelBone bone; bone.name = indexedString(b, off, nameIndex); bone.parent = parent;
      if (!readVec3(b, off + 32, bone.position)) out.diagnostics.push_back("bone position outside table");
      bone.poseToBoneValid = true;
      for (std::size_t matrixIndex = 0; matrixIndex < bone.poseToBone.size(); ++matrixIndex) {
        if (!readAt(b, off + 96 + matrixIndex * sizeof(float), bone.poseToBone[matrixIndex])
            || !std::isfinite(bone.poseToBone[matrixIndex])) {
          bone.poseToBoneValid = false;
          break;
        }
      }
      if (!bone.poseToBoneValid) out.diagnostics.push_back("bone poseToBone matrix outside table or non-finite");
      out.bones.push_back(std::move(bone));
    }
  }
  if (!saneCount(numAttachments, "attachment") || !rangeFits(attachmentIndex, numAttachments, kAttachmentStride, b.size())) {
    out.diagnostics.push_back("attachment table is outside the MDL file or uses an unsupported layout");
  } else {
    out.attachments.reserve(numAttachments);
    for (std::uint32_t i = 0; i < numAttachments; ++i) {
      const auto off = static_cast<std::size_t>(attachmentIndex) + i * kAttachmentStride;
      std::int32_t nameIndex = -1, flags = 0, bone = -1;
      readAt(b, off, nameIndex); readAt(b, off + 4, flags); readAt(b, off + 8, bone);
      ModelAttachment attachment; attachment.name = indexedString(b, off, nameIndex); attachment.flags = flags; attachment.bone = bone;
      // mstudioattachment_t::local is a row-major matrix3x4_t. The origin is
      // its translation column; axes are the three rotation basis columns.
      const auto readMatrixColumn = [&](std::size_t column, std::array<float, 3>& value) {
        return readAt(b, off + 12 + column * 4, value[0])
            && readAt(b, off + 28 + column * 4, value[1])
            && readAt(b, off + 44 + column * 4, value[2]);
      };
      if (!readMatrixColumn(3, attachment.origin)
          || !readMatrixColumn(0, attachment.axisX)
          || !readMatrixColumn(1, attachment.axisY)
          || !readMatrixColumn(2, attachment.axisZ)) {
        out.diagnostics.push_back("attachment transform outside table");
      }
      out.attachments.push_back(std::move(attachment));
    }
  }
  if (!saneCount(numBodyParts, "bodypart") || !rangeFits(bodyPartIndex, numBodyParts, kBodyPartStride, b.size())) {
    out.diagnostics.push_back("bodypart table is outside the MDL file or uses an unsupported layout");
  } else {
    out.bodyParts.reserve(numBodyParts);
    for (std::uint32_t i = 0; i < numBodyParts; ++i) {
      const auto off = static_cast<std::size_t>(bodyPartIndex) + i * kBodyPartStride;
      std::int32_t nameIndex = -1;
      ModelBodyPart bodyPart;
      readAt(b, off, nameIndex);
      readAt(b, off + 4, bodyPart.modelCount);
      readAt(b, off + 8, bodyPart.base);
      readAt(b, off + 12, bodyPart.modelIndex);
      bodyPart.name = indexedString(b, off, nameIndex);
      if (bodyPart.modelCount < 0 || bodyPart.modelCount > 4096) {
        out.diagnostics.push_back("bodypart model count exceeds safe limit");
      }
      out.bodyParts.push_back(std::move(bodyPart));
    }
  }
  if (!saneCount(numSequences, "sequence") || !rangeFits(sequenceIndex, numSequences, kSequenceStride, b.size())) {
    out.diagnostics.push_back("sequence table is outside the MDL file or uses an unsupported layout");
    out.sequenceDecodeReason = "sequence descriptor table is outside bounds";
  } else {
    out.sequenceDecodeReason = numSequences == 0
      ? "MDL contains no sequence descriptors"
      : "animdesc frame count is unsupported: sequence animindex/animation blocks are not decoded";
    if (numSequences == 0) out.sequenceFrameCountAvailable = true;
    out.sequences.reserve(numSequences);
    for (std::uint32_t i = 0; i < numSequences; ++i) {
      const auto off = static_cast<std::size_t>(sequenceIndex) + i * kSequenceStride;
      ModelSequence sequence;
      // mstudioseqdesc_t stores relative string indices, not inline labels.
      std::int32_t labelIndex = -1;
      readAt(b, off + 4, labelIndex);
      sequence.label = indexedString(b, off, labelIndex);
      if (!std::all_of(sequence.label.begin(), sequence.label.end(), [](unsigned char c) { return c >= 32 && c <= 126; })) {
        sequence.label.clear();
        out.diagnostics.push_back("sequence label is not printable; descriptor kept without a label");
      }
      readAt(b, off + 16, sequence.activity); readAt(b, off + 12, sequence.flags);
      readAt(b, off + 56, sequence.blendCount);
      sequence.frameCountAvailable = false;
      out.sequences.push_back(std::move(sequence));
    }
  }
  if (out.name.empty()) out.diagnostics.push_back("MDL name is empty");
  out.valid = out.id == kIdStudio && out.version >= 44 && out.length <= b.size();
  if (!out.valid) out.diagnostics.push_back("MDL signature/version/declared length failed validation");
}
}

ModelResourcePaths ModelResourcePaths::fromMdl(const std::filesystem::path& mdlPath) {
  ModelResourcePaths paths; paths.mdl = mdlPath;
  paths.vvd = mdlPath; paths.vvd.replace_extension(L".vvd");
  paths.vtx = mdlPath; paths.vtx.replace_extension(L".dx90.vtx");
  paths.vtxDx80 = mdlPath; paths.vtxDx80.replace_extension(L".dx80.vtx");
  paths.vtxSw = mdlPath; paths.vtxSw.replace_extension(L".sw.vtx");
  return paths;
}

ModelAssetCandidate ModelLoader::resolveAsset(const AssetRoot& root, const std::string& modelPath) {
  ModelAssetCandidate result;
  result.requestedPath = normalizeModelPath(modelPath);
  if (result.requestedPath.empty()) { result.diagnostic = "empty model path"; return result; }
  std::filesystem::path relative(result.requestedPath);
  if (relative.extension() != ".mdl") relative += ".mdl";
  result.requestedPath = relative.generic_string();
  const auto loose = root.resolve(relative);
  std::error_code ec;
  if (!loose.empty() && std::filesystem::is_regular_file(loose, ec) && !ec) result.looseMdl = loose;
  for (const auto& entry : std::filesystem::directory_iterator(root.tfDirectory, ec)) {
    if (ec || !entry.is_regular_file(ec)) continue;
    const auto name = entry.path().filename().string();
    if (name.size() >= 8 && name.substr(name.size() - 8) == "_dir.vpk") {
      VpkArchive archive;
      if (archive.open(entry.path()) && archive.contains(result.requestedPath)) result.vpkArchives.push_back(entry.path());
    }
  }
  const bool looseFound = !result.looseMdl.empty();
  result.mdlFound = looseFound || !result.vpkArchives.empty();
  const auto stem = result.requestedPath.substr(0, result.requestedPath.size() - 4);
  const auto hasCompanion = [&](const std::string& suffix) {
    if (looseFound) {
      std::error_code fileError;
      return std::filesystem::is_regular_file(root.resolve(stem + suffix), fileError) && !fileError;
    }
    for (const auto& archivePath : result.vpkArchives) {
      VpkArchive archive;
      if (archive.open(archivePath) && archive.contains(stem + suffix)) return true;
    }
    return false;
  };
  result.vvdFound = hasCompanion(".vvd");
  result.dx90VtxFound = hasCompanion(".dx90.vtx");
  result.dx80VtxFound = hasCompanion(".dx80.vtx");
  result.swVtxFound = hasCompanion(".sw.vtx");
  result.phyFound = hasCompanion(".phy");
  result.companionSetComplete = result.mdlFound && result.vvdFound
      && (result.dx90VtxFound || result.dx80VtxFound || result.swVtxFound);
  if (looseFound && result.vpkArchives.empty()) { result.resolution = ModelAssetResolution::FoundLoose; result.resources = ModelResourcePaths::fromMdl(result.looseMdl); result.diagnostic = "model found as loose file"; }
  else if (!looseFound && result.vpkArchives.size() == 1) { result.resolution = ModelAssetResolution::FoundVpk; result.diagnostic = "model found in VPK; byte extraction requires archive-aware inspection"; }
  else if (looseFound || result.vpkArchives.size() > 1) { result.resolution = ModelAssetResolution::Ambiguous; result.diagnostic = "model exists in loose files and/or multiple VPK archives"; }
  else result.diagnostic = "model not found in loose files or VPK archives";
  result.diagnostic += " companions(vvd=" + std::to_string(result.vvdFound)
      + ",dx90=" + std::to_string(result.dx90VtxFound)
      + ",dx80=" + std::to_string(result.dx80VtxFound)
      + ",sw=" + std::to_string(result.swVtxFound)
      + ",phy=" + std::to_string(result.phyFound) + ")";
  return result;
}

ModelAssetCandidate ModelLoader::resolveAssetReference(const AssetRoot& root, const AssetReference& reference,
  const ItemSchema* schema) {
  if (reference.hasModelPath) return resolveAsset(root, reference.modelPath);
  ModelAssetCandidate result;
  if (reference.hasItemDefIndex) {
    result.itemDefIndex = reference.itemDefIndex;
    if (schema) {
      const auto candidates = schema->modelCandidates(static_cast<int>(reference.itemDefIndex));
      result.itemModelCandidate = !candidates.modelPlayer.empty() || !candidates.modelWorld.empty()
        || !candidates.modelPlayerPerClass.empty();
      result.itemModelPlayer = candidates.modelPlayer;
      result.itemModelWorld = candidates.modelWorld;
      for (const auto& model : candidates.modelPlayerPerClass) result.itemModelPerClass.push_back(model);
      if (result.itemModelCandidate) {
        // The schema candidate is evidence of a possible path, not a path
        // selected by the Demo. Keep the resource resolution explicitly
        // unknown until the Demo model reference is decoded.
        result.resolution = ModelAssetResolution::Unknown;
        result.requestedPath = !candidates.modelWorld.empty() ? candidates.modelWorld : candidates.modelPlayer;
        result.diagnostic = "items_game candidate only (itemDefIndex="
          + std::to_string(reference.itemDefIndex) + "); model path was not present in demo reference";
        return result;
      }
    }
  }
  result.resolution = ModelAssetResolution::Unknown;
  result.diagnostic = reference.hasModelIndex
    ? "asset reference has model index but no model path; index-to-path mapping is unknown"
    : "asset reference has no model path";
  return result;
}

std::vector<ModelRenderRequest> ModelLoader::buildRenderRequests(const AssetRoot& root,
  const std::vector<AssetReference>& references, const ItemSchema* schema) {
  std::vector<ModelRenderRequest> requests;
  requests.reserve(std::min<std::size_t>(references.size(), 2048u));
  std::unordered_map<std::string, ModelAssetCandidate> candidateCache;
  std::unordered_map<std::string, ModelInspection> inspectionCache;
  candidateCache.reserve(256);
  inspectionCache.reserve(256);
  for (const auto& reference : references) {
    if (!reference.hasModelPath || reference.modelPath.empty()) continue;
    if (requests.size() >= 2048u) break;
    ModelRenderRequest request;
    request.entityIndex = reference.entityIndex;
    request.classId = reference.classId;
    request.className = reference.className;
    request.modelPath = reference.modelPath;
    const auto normalized = normalizeModelPath(reference.modelPath);
    auto candidateIt = candidateCache.find(normalized);
    if (candidateIt == candidateCache.end()) {
      candidateIt = candidateCache.emplace(normalized, resolveAssetReference(root, reference, schema)).first;
    }
    const auto& candidate = candidateIt->second;
    request.resolution = candidate.resolution;
    request.companionSetComplete = candidate.companionSetComplete;
    request.diagnostic = candidate.diagnostic;
    if (candidate.resolution == ModelAssetResolution::FoundLoose && candidate.companionSetComplete) {
      auto inspectionIt = inspectionCache.find(normalized);
      if (inspectionIt == inspectionCache.end()) {
        inspectionIt = inspectionCache.emplace(normalized, inspect(candidate.resources)).first;
      }
      request.inspection = inspectionIt->second;
      request.renderable = request.inspection.renderableResourceSet;
      if (!request.renderable) request.diagnostic += "; inspection is not renderable";
    }
    requests.push_back(std::move(request));
  }
  return requests;
}

AppearanceResolution ModelLoader::resolveAppearanceReference(const AssetReference& reference,
  const ItemSchema* schema) {
  if (!schema) {
    AppearanceResolution result;
    result.itemDefinitionIndex = reference.hasItemDefIndex ? static_cast<int>(reference.itemDefIndex) : -1;
    result.paintKitId = reference.hasPaintKit ? static_cast<int>(reference.paintKit) : -1;
    result.skin = reference.hasSkin ? static_cast<int>(reference.skin) : -1;
    return result;
  }
  return schema->resolveAppearance(
    reference.hasItemDefIndex ? static_cast<int>(reference.itemDefIndex) : -1,
    reference.hasPaintKit ? static_cast<int>(reference.paintKit) : -1,
    reference.hasSkin ? static_cast<int>(reference.skin) : -1);
}

ModelInspection ModelLoader::inspect(const std::filesystem::path& mdlPath) {
  return inspect(ModelResourcePaths::fromMdl(mdlPath));
}

ModelInspection ModelLoader::inspect(const ModelResourcePaths& paths) {
  ModelInspection result; result.resources = paths;
  result.mdl = inspectFile(paths.mdl, ModelFileKind::Mdl);
  result.vvd = inspectFile(paths.vvd, ModelFileKind::Vvd);
  result.vtx = inspectFile(paths.vtx, ModelFileKind::Vtx);
  result.vtxDx80 = inspectFile(paths.vtxDx80, ModelFileKind::Vtx);
  result.vtxSw = inspectFile(paths.vtxSw, ModelFileKind::Vtx);
  if (result.mdl.exists && result.mdl.signatureValid) {
    std::string error; const auto bytes = readFile(paths.mdl, error);
    if (!bytes.empty()) parseMdl(bytes, result.metadata); else result.diagnostics.push_back("MDL read failed: " + error);
  }
  if (result.vvd.exists && result.vvd.signatureValid) {
    std::string error; const auto bytes = readFile(paths.vvd, error);
    if (!bytes.empty()) parseVvd(bytes, result.metadata); else result.diagnostics.push_back("VVD read failed: " + error);
  }
  const auto vtxVersion = result.vtx.signatureValid ? result.vtx.version
      : (result.vtxDx80.signatureValid ? result.vtxDx80.version : result.vtxSw.version);
  const bool anyVtx = result.vtx.signatureValid || result.vtxDx80.signatureValid || result.vtxSw.signatureValid;
  if (anyVtx) {
    const auto& status = result.vtx.signatureValid ? result.vtx : (result.vtxDx80.signatureValid ? result.vtxDx80 : result.vtxSw);
    std::string error; const auto bytes = readFile(status.path, error);
    if (!bytes.empty()) { parseVtxDiagnostics(bytes, result.metadata); parseVtxHierarchy(bytes, result.metadata); }
    else result.diagnostics.push_back("VTX read failed: " + error);
  }
  for (auto& mesh : result.metadata.meshes) {
    mesh.vtxVersion = vtxVersion;
    mesh.indexStatus = !anyVtx ? ModelMeshIndexStatus::MissingVtx
        : (!result.metadata.indices.empty() ? ModelMeshIndexStatus::Available : ModelMeshIndexStatus::NotDecoded);
  }
  result.renderableResourceSet = result.mdl.signatureValid && result.vvd.signatureValid && anyVtx;
  if (result.metadata.valid) {
    result.attachmentStatus = result.metadata.attachmentCount > 0
      && result.metadata.attachments.size() == result.metadata.attachmentCount
      ? ModelFeatureStatus::Available : ModelFeatureStatus::Missing;
    // The MDL body-part table is not the runtime bodygroup selection state.
    // Keep this explicitly unknown until entity skin/bodygroup values are wired.
    result.bodygroupStatus = ModelFeatureStatus::Unknown;
    const auto modelName = paths.mdl.filename().generic_string();
    const bool isViewModel = modelName.size() > 6 && modelName.rfind("v_", 0) == 0
      && paths.mdl.extension() == ".mdl";
    result.viewModelPathDetected = isViewModel;
    result.viewModelStatus = isViewModel ? ModelFeatureStatus::Unknown : ModelFeatureStatus::Missing;
  }
  if (!result.mdl.signatureValid) result.diagnostics.push_back("MDL is missing or invalid");
  if (!result.vvd.signatureValid) result.diagnostics.push_back("VVD is missing or invalid");
  if (!anyVtx) result.diagnostics.push_back("no valid DX90/DX80/SW VTX companion found");
  else if (result.metadata.indices.empty()) {
    result.diagnostics.push_back("VTX strip/group index hierarchy produced no descriptors");
  } else {
    const auto& first = result.metadata.indices.front();
    result.diagnostics.push_back("VTX index descriptors=" + std::to_string(result.metadata.indices.size())
        + " first indexOffset=" + std::to_string(first.indexOffset)
        + " indexCount=" + std::to_string(first.indexCount)
        + " vertexOffset=" + std::to_string(first.vertexOffset)
        + " vertexCount=" + std::to_string(first.vertexCount));
  }
  if (result.renderableResourceSet) result.diagnostics.push_back("resource set is structurally complete; GPU mesh upload and skinning are not implemented here");
  result.diagnostics.push_back("attachment status=" + std::string(
    result.attachmentStatus == ModelFeatureStatus::Available ? "available" :
    result.attachmentStatus == ModelFeatureStatus::Missing ? "missing" : "unknown"));
  result.diagnostics.push_back("bodygroup status=unknown; runtime selection is not decoded");
  result.diagnostics.push_back("bodypart probe count=" + std::to_string(result.metadata.bodyParts.size()));
  for (std::size_t i = 0; i < result.metadata.bodyParts.size() && i < 8; ++i) {
    const auto& part = result.metadata.bodyParts[i];
    result.diagnostics.push_back("bodypart[" + std::to_string(i) + "] name=" + part.name
      + " models=" + std::to_string(part.modelCount)
      + " base=" + std::to_string(part.base)
      + " modelIndex=" + std::to_string(part.modelIndex));
  }
  result.diagnostics.push_back("viewmodel status=" + std::string(
    result.viewModelStatus == ModelFeatureStatus::Missing ? "missing" : "unknown")
    + "; first-person rendering is not implemented");
  return result;
}

bool ModelLoader::buildBindPoseMesh(ModelMetadata& metadata, std::size_t descriptorIndex, std::string& error) {
  if (descriptorIndex >= metadata.indices.size()) { error = "model index descriptor is out of range"; return false; }
  const auto& descriptor = metadata.indices[descriptorIndex];
  if (descriptor.renderIndexStart + descriptor.renderIndexCount > metadata.renderIndices.size()) { error = "render index range is outside metadata"; return false; }
  metadata.bindPoseVertices.clear();
  metadata.bindPoseVertexCount = 0;
  metadata.bindPoseVertices.reserve(descriptor.renderIndexCount);
  for (std::uint32_t i = 0; i < descriptor.renderIndexCount; ++i) {
    const auto original = metadata.renderIndices[descriptor.renderIndexStart + i];
    if (original >= metadata.vertices.size()) { error = "render index exceeds VVD vertices"; metadata.bindPoseVertices.clear(); return false; }
    const auto& source = metadata.vertices[original];
    const float weightSum = source.weights[0] + source.weights[1] + source.weights[2];
    if (source.boneCount > source.boneIndices.size()
        || !std::isfinite(weightSum) || weightSum < 0.99f || weightSum > 1.01f) {
      error = "bind-pose vertex has invalid skin weights";
      metadata.bindPoseVertices.clear();
      return false;
    }
    for (std::size_t bone = 0; bone < source.boneCount; ++bone) {
      if (source.boneIndices[bone] >= metadata.boneCount) {
        error = "bind-pose vertex has an out-of-range bone index";
        metadata.bindPoseVertices.clear();
        return false;
      }
    }
    metadata.bindPoseVertices.push_back(ModelDrawVertex{
      source.position, source.normal, source.texcoord, source.weights, source.boneIndices, source.boneCount});
  }
  metadata.bindPoseVertexCount = metadata.bindPoseVertices.size();
  return true;
}

bool ModelLoader::cpuSkinVertexReference(const ModelMetadata& metadata, const ModelDrawVertex& vertex,
    std::array<float, 3>& outputPosition, std::string& error) {
  outputPosition = {};
  if (vertex.boneCount == 0 || vertex.boneCount > vertex.boneIndices.size()) { error = "reference skin vertex has invalid bone count"; return false; }
  const float weightSum = vertex.weights[0] + vertex.weights[1] + vertex.weights[2];
  if (!std::isfinite(weightSum) || weightSum < 0.99f || weightSum > 1.01f) { error = "reference skin vertex has invalid weights"; return false; }
  for (std::size_t i = 0; i < vertex.boneCount; ++i) {
    const auto index = vertex.boneIndices[i];
    if (index >= metadata.bones.size() || !metadata.bones[index].poseToBoneValid) { error = "reference skin vertex has an invalid bone matrix"; return false; }
    const auto& matrix = metadata.bones[index].poseToBone;
    const float x = vertex.position[0], y = vertex.position[1], z = vertex.position[2];
    outputPosition[0] += vertex.weights[i] * (matrix[0] * x + matrix[1] * y + matrix[2] * z + matrix[3]);
    outputPosition[1] += vertex.weights[i] * (matrix[4] * x + matrix[5] * y + matrix[6] * z + matrix[7]);
    outputPosition[2] += vertex.weights[i] * (matrix[8] * x + matrix[9] * y + matrix[10] * z + matrix[11]);
  }
  if (!std::isfinite(outputPosition[0]) || !std::isfinite(outputPosition[1]) || !std::isfinite(outputPosition[2])) {
    error = "reference skin result is non-finite";
    outputPosition = {};
    return false;
  }
  return true;
}

} // namespace tf2::native






