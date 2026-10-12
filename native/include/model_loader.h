#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "asset_root.h"
#include "item_schema.h"
#include "vpk_archive.h"

namespace tf2::native {

struct AssetReference;
enum class ModelFileKind { Mdl, Vvd, Vtx, Unknown };

// The one rule for comparing two model paths, whichever layers they came from.
//
// A model path reaches this code spelled four different ways: the demo's
// model_precache writes forward slashes, an MDL's own $cdmaterials block writes
// backslashes, a VMT's $basetexture writes backslashes without the `materials/`
// prefix, and a command line can carry either. Whether two of them name the
// same model is therefore never a string equality on the raw values.
//
// Exported because more than one translation unit needs to ask that question --
// model_loader.cpp while resolving an asset, viewmodel.cpp when it checks that
// the model an entity names is the one being drawn, and entity_model.cpp when it
// checks that a weapon's world model is not the arms composite. Three separate
// copies would be three answers, and the failure they produce is quiet: a
// comparison that misses draws the wrong model while every count stays green.
inline std::string normalizeModelPath(std::string value) {
  std::replace(value.begin(), value.end(), '\\', '/');
  while (!value.empty() && value.front() == '/') value.erase(value.begin());
  std::transform(value.begin(), value.end(), value.begin(),
    [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
  return value;
}

struct ModelResourcePaths {
  std::filesystem::path mdl;
  std::filesystem::path vvd;
  std::filesystem::path vtx;
  std::filesystem::path vtxDx80;
  std::filesystem::path vtxSw;
  static ModelResourcePaths fromMdl(const std::filesystem::path& mdlPath);
};

enum class ModelAssetResolution { Unknown, Missing, FoundLoose, FoundVpk, Ambiguous };

enum class ModelFeatureStatus { Unknown, Missing, Available };

struct ModelAssetCandidate {
  ModelAssetResolution resolution = ModelAssetResolution::Missing;
  std::string requestedPath;
  std::filesystem::path looseMdl;
  std::vector<std::filesystem::path> vpkArchives;
  bool mdlFound = false;
  bool vvdFound = false;
  bool dx90VtxFound = false;
  bool dx80VtxFound = false;
  bool swVtxFound = false;
  bool phyFound = false;
  bool companionSetComplete = false;
  std::int64_t itemDefIndex = 0;
  bool itemModelCandidate = false;
  std::string itemModelPlayer;
  std::string itemModelWorld;
  std::vector<std::pair<std::string, std::string>> itemModelPerClass;
  ModelResourcePaths resources;
  std::string diagnostic;
};

struct ModelFileStatus {
  ModelFileKind kind = ModelFileKind::Unknown;
  std::filesystem::path path;
  bool exists = false;
  bool signatureValid = false;
  std::uint32_t version = 0;
  std::uint32_t checksum = 0;
  std::uint64_t bytes = 0;
  std::string diagnostic;
};

struct ModelBone {
  std::string name;
  std::int32_t parent = -1;
  std::array<float, 3> position{};
  std::array<float, 12> poseToBone{};
  bool poseToBoneValid = false;
};

struct ModelAttachment {
  std::string name;
  std::int32_t flags = 0;
  std::int32_t bone = -1;
  std::array<float, 3> origin{};
  std::array<float, 3> axisX{};
  std::array<float, 3> axisY{};
  std::array<float, 3> axisZ{};
};

struct ModelSequence {
  std::string label;
  std::int32_t flags = 0;
  std::int32_t activity = 0;
  std::int32_t blendCount = 0;
  std::int32_t frameCount = 0;
  bool frameCountAvailable = false;
};

struct ModelBodyPart {
  std::string name;
  std::int32_t modelCount = 0;
  std::int32_t base = 0;
  std::int32_t modelIndex = 0;
};

struct ModelVertex {
  std::array<float, 3> position{};
  std::array<float, 3> normal{};
  std::array<float, 2> texcoord{};
  std::array<float, 3> weights{};
  std::array<std::uint8_t, 3> boneIndices{};
  std::uint8_t boneCount = 0;
};

struct ModelDrawVertex {
  std::array<float, 3> position{};
  std::array<float, 3> normal{};
  std::array<float, 2> texcoord{};
  std::array<float, 3> weights{};
  std::array<std::uint8_t, 3> boneIndices{};
  std::uint8_t boneCount = 0;
};

enum class ModelMeshIndexStatus { MissingVtx, NotDecoded, Available };

struct ModelMeshDescriptor {
  std::uint32_t lod = 0;
  std::uint32_t vertexStart = 0;
  std::uint32_t vertexCount = 0;
  std::uint32_t vtxVersion = 0;
  ModelMeshIndexStatus indexStatus = ModelMeshIndexStatus::MissingVtx;
};

struct ModelIndexDescriptor {
  std::uint32_t bodyPart = 0;
  std::uint32_t model = 0;
  std::uint32_t lod = 0;
  std::uint32_t mesh = 0;
  std::uint32_t stripGroup = 0;
  std::uint32_t indexOffset = 0;
  std::uint32_t indexCount = 0;
  std::uint32_t vertexOffset = 0;
  std::uint32_t vertexCount = 0;
  std::uint32_t renderIndexStart = 0;
  std::uint32_t renderIndexCount = 0;
};

// The MDL mesh table, decoded far enough to answer one question: **which
// `mstudiotexture_t` slot does the mesh a draw descriptor names use?**
//
// WHY THIS EXISTS
//   `mstudiomesh_t` entry 0 is `material`, the texture slot index, and it is the
//   only place in the resource set that ties a triangle to a material. Without
//   it a consumer has to guess, and the guess that was already in the code --
//   "slot 0 is the model's material" -- is right for the three ViewModels
//   measured on 2026-10-12 and would have been wrong for any model whose first
//   mesh is not slot 0. A reading that *is* the guess cannot tell the two apart.
//
// WHY IT IS VALIDATED RATHER THAN ASSUMED
//   The strides (`mstudiomodel_t`, `mstudiomesh_t`) are version-dependent, and a
//   wrong stride does not look like garbage -- it looks like a smaller table.
//   Two independent witnesses pin it instead of a hard-coded layout:
//     * the VTX descriptor list, which carries its own (bodyPart, model, mesh)
//       triple for every strip group. A candidate stride is rejected unless every
//       descriptor's triple lands inside the decoded table.
//     * the texture count: every decoded `material` must be a real slot index.
//   A candidate that satisfies both is reported with the stride it used, so a
//   wrong decode reads as `available=false` plus a reason rather than as a slot
//   number nobody can trace.
struct ModelMeshMaterialTable {
  bool available = false;
  std::string reason;
  // [bodyPart][model] -> one material slot per mesh, in MDL (= VTX) mesh order.
  std::vector<std::vector<std::vector<std::int32_t>>> slots;
  std::size_t modelStride = 0;
  std::size_t meshStride = 0;
  // "derived" when the stride was solved from consecutive bodypart tables,
  // "assumed" when only the layout constant was available. The difference
  // matters: a derived stride carries its own arithmetic witness.
  std::string modelStrideSource;
  // Whether the model stride was used at all. Every ViewModel measured on
  // 2026-10-12 has exactly one model per bodypart, so `model` is always 0 and the
  // decode cannot depend on the stride -- which means a wrong stride is *not*
  // ruled out by the fact that the decode validated. The reading says so instead
  // of letting "validated" imply more than it does.
  bool modelStrideExercised = false;
  // The same question for `meshStride`: a model whose meshes all decode to
  // `nummeshes <= 1` never reads mesh 1, so the stride is not part of its answer.
  bool meshStrideExercised = false;
  std::size_t meshCount = 0;
  // How many VTX descriptors were checked against this table.
  std::size_t descriptorsValidated = 0;

  std::int32_t slotFor(std::uint32_t bodyPart, std::uint32_t model, std::uint32_t mesh) const {
    if (bodyPart >= slots.size()) return -1;
    if (model >= slots[bodyPart].size()) return -1;
    if (mesh >= slots[bodyPart][model].size()) return -1;
    return slots[bodyPart][model][mesh];
  }
};

struct ModelVtxDiagnostics {
  bool headerInBounds = false;
  bool bodyPartTableInBounds = false;
  std::uint32_t version = 0;
  std::uint32_t declaredBytes = 0;
  std::uint32_t bodyPartCount = 0;
  std::uint32_t bodyPartOffset = 0;
  std::uint32_t bodyPartStride = 0;
  std::uint32_t firstModelOffsetCandidate = 0;
  std::uint32_t firstModelAbsoluteCandidate = 0;
  std::size_t vtxIndexCount = 0;
  std::size_t vtxDescriptorCount = 0;
  std::size_t vtxOutOfBoundsCount = 0;
  std::size_t vtxDegenerateTriangleCount = 0;
  std::string failureReason;
};

struct ModelMetadata {
  bool valid = false;
  std::uint32_t id = 0;
  std::uint32_t version = 0;
  std::uint32_t checksum = 0;
  std::string name;
  std::uint32_t length = 0;
  std::uint32_t flags = 0;
  std::uint32_t boneCount = 0;
  std::uint32_t attachmentCount = 0;
  std::uint32_t sequenceCount = 0;
  std::uint32_t bodyPartCount = 0;
  std::uint32_t textureCount = 0;
  std::size_t skinningVertexCount = 0;
  std::size_t invalidSkinVertexCount = 0;
  bool skinningDataAvailable = false;
  bool bindPoseMatricesAvailable = false;
  std::size_t boneStride = 216;
  std::size_t poseToBoneOffset = 96;
  std::string skinningBlockedReason;
  bool sequenceFrameCountAvailable = false;
  std::string sequenceDecodeReason;
  std::vector<ModelBone> bones;
  std::vector<ModelAttachment> attachments;
  std::vector<ModelSequence> sequences;
  std::vector<ModelBodyPart> bodyParts;
  // The `mstudiotexture_t` name table, in slot order. `textureNames[i]` is the
  // first candidate from `textureCandidates[i]`; each name is a material path
  // stem (`models/player/scout/scout_red`) that resolves to a `.vmt` under
  // `materials/`. Nothing consumed this table before the entity-material round:
  // the renderer bound the map atlas to every entity draw, so the count was
  // enough. It is not enough to bind the model's own texture.
  std::vector<std::string> textureNames;
  // Every path a slot's raw name could resolve to, in the order the engine would
  // try them: a rooted name stands alone, a bare one is prefixed with each
  // `$cdmaterials` directory in turn. The consumer takes the first candidate
  // whose `.vmt` actually reads. Slots whose name was unreadable stay in the
  // vector as empty lists, so slot order is preserved.
  std::vector<std::vector<std::string>> textureCandidates;
  // The `studiohdr_t` CD-texture path list (words 212/216): `$cdmaterials`
  // directories, normalized with a trailing slash. This is the list a bare
  // texture stem is relative to -- not the model's own directory, which only
  // happens to agree for the player models.
  std::vector<std::string> cdTexturePaths;
  std::vector<ModelVertex> vertices;
  std::vector<ModelMeshDescriptor> meshes;
  std::vector<ModelIndexDescriptor> indices;
  std::vector<std::uint32_t> renderIndices;
  std::vector<ModelDrawVertex> bindPoseVertices;
  std::size_t bindPoseVertexCount = 0;
  ModelVtxDiagnostics vtxDiagnostics;
  // Which texture slot each mesh uses, decoded and cross-validated against the
  // VTX descriptor list. Filled by `decodeMeshMaterials` after the VTX hierarchy
  // is up, because the validation needs the descriptors. Empty and
  // `available=false` is a real state -- a consumer then has to say which slot it
  // guessed instead of reporting an unearned slot number.
  ModelMeshMaterialTable meshMaterials;
  std::vector<std::string> diagnostics;
};

struct ModelInspection {
  ModelResourcePaths resources;
  ModelFileStatus mdl;
  ModelFileStatus vvd;
  ModelFileStatus vtx;
  ModelFileStatus vtxDx80;
  ModelFileStatus vtxSw;
  ModelMetadata metadata;
  bool renderableResourceSet = false;
  ModelFeatureStatus attachmentStatus = ModelFeatureStatus::Unknown;
  ModelFeatureStatus bodygroupStatus = ModelFeatureStatus::Unknown;
  ModelFeatureStatus viewModelStatus = ModelFeatureStatus::Unknown;
  bool viewModelPathDetected = false;
  std::vector<std::string> diagnostics;
};

struct ModelMeshData {
  std::size_t descriptorIndex = 0;
  std::vector<ModelDrawVertex> vertices;
  std::vector<std::uint32_t> indices;
  bool triangleList = false;
};

struct ModelRenderRequest {
  std::uint16_t entityIndex = 0;
  std::int32_t classId = -1;
  std::string className;
  std::string modelPath;
  std::string cacheKey;
  std::uint32_t checksum = 0;
  ModelAssetResolution resolution = ModelAssetResolution::Unknown;
  bool companionSetComplete = false;
  bool renderable = false;
  bool inspectedFromVpk = false;
  // Mirrors AssetReference::modelPathFromWorldModelIndex so a caller downstream
  // of the reference list can still tell "this request names the weapon" from
  // "this request names the first-person arms". The distinction matters exactly
  // because the arms model is a real, resolvable asset: the wrong route produces
  // a perfectly renderable request for the wrong object.
  bool modelPathFromWorldModelIndex = false;
  ModelInspection inspection;
  std::string diagnostic;
};

struct ModelRenderRequestStats {
  std::size_t candidateResolves = 0;
  std::size_t candidateCacheHits = 0;
  std::size_t inspections = 0;
  std::size_t inspectionCacheHits = 0;
  std::size_t vpkExtracts = 0;
  std::size_t archiveOpens = 0;
};

class ModelLoader final {
public:
  static ModelAssetCandidate resolveAsset(const AssetRoot& root, const std::string& modelPath);
  static ModelAssetCandidate resolveAssetReference(const AssetRoot& root, const AssetReference& reference,
    const ItemSchema* schema = nullptr);
  static std::vector<ModelRenderRequest> buildRenderRequests(const AssetRoot& root,
    const std::vector<AssetReference>& references, const ItemSchema* schema = nullptr,
    ModelRenderRequestStats* stats = nullptr);
  static AppearanceResolution resolveAppearanceReference(const AssetReference& reference,
    const ItemSchema* schema);
  static ModelInspection inspect(const std::filesystem::path& mdlPath);
  static ModelInspection inspect(const ModelResourcePaths& paths);
  static ModelInspection inspectVpk(const VpkArchive& archive, const std::string& modelPath);
  static bool buildModelMeshData(const ModelInspection& inspection, std::size_t descriptorIndex,
    ModelMeshData& mesh, std::string& error);
  static bool buildBindPoseMesh(ModelMetadata& metadata, std::size_t descriptorIndex, std::string& error);
  static bool buildBindPoseMeshLod0(ModelMetadata& metadata, std::string& error, std::size_t maxVertices = 12000);
  static bool cpuSkinVertexReference(const ModelMetadata& metadata, const ModelDrawVertex& vertex,
    std::array<float, 3>& outputPosition, std::string& error);
};

} // namespace tf2::native
