#pragma once

#include <array>
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
  std::vector<ModelVertex> vertices;
  std::vector<ModelMeshDescriptor> meshes;
  std::vector<ModelIndexDescriptor> indices;
  std::vector<std::uint32_t> renderIndices;
  std::vector<ModelDrawVertex> bindPoseVertices;
  std::size_t bindPoseVertexCount = 0;
  ModelVtxDiagnostics vtxDiagnostics;
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

class ModelLoader final {
public:
  static ModelAssetCandidate resolveAsset(const AssetRoot& root, const std::string& modelPath);
  static ModelAssetCandidate resolveAssetReference(const AssetRoot& root, const AssetReference& reference,
    const ItemSchema* schema = nullptr);
  static AppearanceResolution resolveAppearanceReference(const AssetReference& reference,
    const ItemSchema* schema);
  static ModelInspection inspect(const std::filesystem::path& mdlPath);
  static ModelInspection inspect(const ModelResourcePaths& paths);
  static bool buildBindPoseMesh(ModelMetadata& metadata, std::size_t descriptorIndex, std::string& error);
  static bool cpuSkinVertexReference(const ModelMetadata& metadata, const ModelDrawVertex& vertex,
    std::array<float, 3>& outputPosition, std::string& error);
};

} // namespace tf2::native
