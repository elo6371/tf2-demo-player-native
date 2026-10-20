// T30: model, animation and companion robustness readings.
//
// WHY THIS PROBE EXISTS
//   The existing probes answer one question each: viewmodel_range_probe prints
//   the VTX range table and the four malformed-descriptor refusals,
//   viewmodel_contract_probe prints the resource/attachment contract, and the
//   animation probes print the decode and the binding self-test. T30 asks a
//   question that spans them: for a REAL installed model, what did the loader
//   select (LOD, range count, material slot), what texture does each range
//   actually bind, what happens to the model when one companion file is absent,
//   and which animation source does the binding resolve to? Those readings are
//   produced nowhere as one deterministic line per model, which is what a gate
//   can pin without re-deriving any of them.
//
// WHAT IT REPORTS, AND WHY EACH NUMBER IS A READING
//   * selection      targetLod, lodFallback, targetDescriptors, targetIndices --
//                    all computed the same way ModelLoader::buildModelMeshDataAll
//                    computes them, from the same inspection.
//   * ranges         one line per built range: vertex count, material slot, the
//                    MDL's own material name, the VTF that slot actually resolves
//                    to, the highest bone index the range references and the
//                    count of vertices whose weights do not sum to one.
//   * companions     mdl/vvd/vtx presence from the same ModelLoader::resolveAsset
//                    the product uses, plus the animation source the binding
//                    resolves to (separateAnimSource, animPath) and that source's
//                    status and reason. A missing *_animations.mdl companion is
//                    named, not folded into 'no animation'.
//   * refusals       the same four malformed states viewmodel_range_probe injects
//                    (so the LOD-0 refusal path is exercised on the same entry
//                    point), plus lod0-absent, which is a FALLBACK rather than a
//                    refusal and therefore reports lodFallback=1 instead of a
//                    refusal reason.
//   * materialize    writes a real model's own bytes (MDL, VVD, every VTX
//                    variant, and the declared animation companion) into a
//                    directory as loose files, optionally omitting one file. The
//                    gate uses that to make a REAL model's companion set
//                    incomplete -- the shipped install has no such model -- and
//                    then asks the product for its named refusal.
//
// WHAT IT DOES NOT DO
//   It does not draw. --materialize writes bytes; the gate launches the product
//   against those bytes for the refusal and for the pixel evidence.
//
// USAGE
//   model_robustness_probe --tf-root <tf> --model <models/...mdl>
//     [--fixture none|descriptor-not-triangles|descriptor-out-of-bounds|
//                bone-out-of-range|no-descriptors|lod0-absent]
//     [--materialize <dir>] [--drop mdl|vvd|vtx|animations]
//     [--materialize-only]
//
//   Exit 0: the model resolved and the request built every target-LOD range, or
//           the materialisation completed.
//   Exit 1: a refusal was recorded (named on stderr and in the JSON), or the
//           materialisation failed.
//   Exit 2: usage error.

#include "animation_binding.h"
#include "animation_decoder.h"
#include "asset_root.h"
#include "model_loader.h"
#include "viewmodel_material.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <set>
#include <string>
#include <vector>

namespace {

using tf2::native::AnimationBindingCache;
using tf2::native::AnimationInputStatus;
using tf2::native::AssetRoot;
using tf2::native::ModelAssetResolution;
using tf2::native::ModelInspection;
using tf2::native::ModelMeshData;
using tf2::native::ModelMetadata;

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

void usage() {
  std::cerr << "usage: model_robustness_probe --tf-root <tf> --model <path.mdl>"
               " [--fixture none|descriptor-not-triangles|descriptor-out-of-bounds|"
               "bone-out-of-range|no-descriptors|lod0-absent]"
               " [--materialize <dir>] [--drop mdl|vvd|vtx|animations]"
               " [--materialize-only]\n";
}

std::string normalizeAssetPath(std::string value) {
  std::replace(value.begin(), value.end(), '\\', '/');
  while (!value.empty() && value.front() == '/') value.erase(value.begin());
  std::transform(value.begin(), value.end(), value.begin(),
    [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
  return value;
}

// The same read order the model loader and the animation binding use: every
// archive that holds the key first, then the loose file. Reading either one only
// would make the materialisation lose the bytes it is supposed to copy.
std::vector<std::uint8_t> readAssetBytes(const AssetRoot& root, const std::string& path) {
  std::vector<std::filesystem::path> holders;
  const auto& archives = root.archives();
  archives.collectContaining(path, holders);
  for (const auto& archivePath : holders) {
    const auto* archive = archives.find(archivePath);
    if (archive == nullptr) continue;
    std::string error;
    const auto bytes = archive->read(path, &error);
    if (!bytes.empty()) return bytes;
  }
  std::error_code fileError;
  const auto loose = root.resolve(path);
  if (std::filesystem::is_regular_file(loose, fileError) && !fileError) {
    std::ifstream stream(loose, std::ios::binary);
    if (stream) {
      return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(stream),
        std::istreambuf_iterator<char>());
    }
  }
  return {};
}

const char* resolutionName(ModelAssetResolution value) {
  switch (value) {
    case ModelAssetResolution::FoundLoose: return "found-loose";
    case ModelAssetResolution::FoundVpk: return "found-vpk";
    case ModelAssetResolution::Ambiguous: return "ambiguous";
    default: return "missing";
  }
}

const char* vtfStatusName(tf2::native::ViewModelMaterialStatus status) {
  using tf2::native::ViewModelMaterialStatus;
  switch (status) {
    case ViewModelMaterialStatus::Resolved: return "resolved";
    case ViewModelMaterialStatus::MissingMaterial: return "missing-material";
    case ViewModelMaterialStatus::MissingTexture: return "missing-texture";
    case ViewModelMaterialStatus::InvalidPath: return "invalid-path";
    case ViewModelMaterialStatus::OversizedTexture: return "oversized";
  }
  return "unknown";
}

void rangeSkinReadings(const ModelMeshData& mesh, const ModelMeshData::Range& range,
    int& maxBone, std::size_t& badWeights) {
  maxBone = -1;
  badWeights = 0;
  const auto end = std::min<std::size_t>(range.firstVertex + range.vertexCount, mesh.vertices.size());
  for (std::size_t i = range.firstVertex; i < end; ++i) {
    const auto& vertex = mesh.vertices[i];
    for (std::size_t bone = 0; bone < vertex.boneCount && bone < vertex.boneIndices.size(); ++bone) {
      maxBone = std::max(maxBone, static_cast<int>(vertex.boneIndices[bone]));
    }
    const float sum = vertex.weights[0] + vertex.weights[1] + vertex.weights[2];
    if (!(sum >= 0.99f && sum <= 1.01f)) ++badWeights;
  }
}

std::string materialName(const ModelMetadata& metadata, std::int32_t slot) {
  if (slot < 0) return {};
  const auto index = static_cast<std::size_t>(slot);
  if (index < metadata.textureNames.size()) return metadata.textureNames[index];
  return {};
}

bool writeBytes(const std::filesystem::path& target, const std::vector<std::uint8_t>& bytes,
    std::string& error) {
  std::error_code directoryError;
  std::filesystem::create_directories(target.parent_path(), directoryError);
  std::ofstream stream(target, std::ios::binary);
  if (!stream) { error = "could not open " + target.generic_string(); return false; }
  stream.write(reinterpret_cast<const char*>(bytes.data()),
    static_cast<std::streamsize>(bytes.size()));
  if (!stream) { error = "could not write " + target.generic_string(); return false; }
  return true;
}

struct MaterialisedFile {
  std::string path;
  std::uint64_t bytes = 0;
  bool dropped = false;
};

} // namespace

int main(int argc, char** argv) {
  std::filesystem::path tfRoot;
  std::string model;
  std::string fixture = "none";
  std::filesystem::path materializeDir;
  std::string drop;
  bool materializeOnly = false;
  for (int index = 1; index < argc; ++index) {
    const std::string argument = argv[index];
    if (argument == "--tf-root" && index + 1 < argc) {
      tfRoot = std::filesystem::u8path(argv[++index]);
    } else if (argument == "--model" && index + 1 < argc) {
      model = argv[++index];
    } else if (argument == "--fixture" && index + 1 < argc) {
      fixture = argv[++index];
    } else if (argument == "--materialize" && index + 1 < argc) {
      materializeDir = std::filesystem::u8path(argv[++index]);
    } else if (argument == "--drop" && index + 1 < argc) {
      drop = argv[++index];
    } else if (argument == "--materialize-only") {
      materializeOnly = true;
    } else {
      usage();
      return 2;
    }
  }
  const bool knownFixture = fixture == "none" || fixture == "descriptor-not-triangles"
    || fixture == "descriptor-out-of-bounds" || fixture == "bone-out-of-range"
    || fixture == "no-descriptors" || fixture == "lod0-absent";
  const bool knownDrop = drop.empty() || drop == "mdl" || drop == "vvd"
    || drop == "vtx" || drop == "animations";
  if (tfRoot.empty() || model.empty() || !knownFixture || !knownDrop) {
    usage();
    return 2;
  }
  if (materializeDir.empty() && (!drop.empty() || materializeOnly)) {
    std::fprintf(stderr, "robust error=--drop and --materialize-only need --materialize\n");
    return 2;
  }

  const auto root = AssetRoot::fromPath(tfRoot);
  const auto candidate = tf2::native::ModelLoader::resolveAsset(root, model);
  const bool loose = candidate.resolution == ModelAssetResolution::FoundLoose;
  const bool packed = candidate.resolution == ModelAssetResolution::FoundVpk;

  // ---- materialisation -----------------------------------------------------
  if (!materializeDir.empty()) {
    std::vector<MaterialisedFile> files;
    std::string materialiseError;
    std::string requestedPath = candidate.requestedPath.empty()
      ? normalizeAssetPath(model) : candidate.requestedPath;
    if (requestedPath.size() < 4 || requestedPath.compare(requestedPath.size() - 4, 4, ".mdl") != 0) {
      requestedPath += ".mdl";
    }
    const auto stem = requestedPath.substr(0, requestedPath.size() - 4);
    std::vector<std::pair<std::string, bool>> wanted;
    wanted.emplace_back(requestedPath, drop == "mdl");
    if (candidate.vvdFound) wanted.emplace_back(stem + ".vvd", drop == "vvd");
    if (candidate.dx90VtxFound) wanted.emplace_back(stem + ".dx90.vtx", drop == "vtx");
    if (candidate.dx80VtxFound) wanted.emplace_back(stem + ".dx80.vtx", drop == "vtx");
    if (candidate.swVtxFound) wanted.emplace_back(stem + ".sw.vtx", drop == "vtx");
    // The animation companion is not in the resource candidate: it is declared by
    // the MDL's own include list, which is where the binding reads it from.
    std::string animationsCompanion;
    const auto mdlBytes = readAssetBytes(root, requestedPath);
    if (!mdlBytes.empty()) {
      tf2::native::AnimationModel decoded;
      if (tf2::native::decodeAnimationModel(mdlBytes.data(), mdlBytes.size(), decoded)
          == tf2::native::AnimationStatus::Ok) {
        for (const auto& include : decoded.includes) {
          const auto normalized = normalizeAssetPath(include);
          if (normalized.find("_animations.mdl") != std::string::npos) {
            animationsCompanion = normalized;
            break;
          }
        }
      }
    }
    std::vector<std::uint8_t> animationsBytes;
    if (!animationsCompanion.empty()) {
      animationsBytes = readAssetBytes(root, animationsCompanion);
      wanted.emplace_back(animationsCompanion, drop == "animations");
    }
    for (const auto& entry : wanted) {
      MaterialisedFile file;
      file.path = entry.first;
      file.dropped = entry.second;
      if (!entry.second) {
        const auto bytes = entry.first == animationsCompanion
          ? animationsBytes : readAssetBytes(root, entry.first);
        if (bytes.empty()) {
          materialiseError = "could not read " + entry.first + " from the asset root";
          file.bytes = 0;
        } else {
          file.bytes = bytes.size();
          const auto target = materializeDir / std::filesystem::u8path(entry.first);
          if (!writeBytes(target, bytes, materialiseError)) break;
        }
      }
      files.push_back(std::move(file));
    }
    // A root the product will accept needs a NON-EMPTY store under materials,
    // models and maps: AssetRoot::valid() requires a regular file under each (the
    // committed fixture roots carry a .gitkeep). Without the two markers the
    // animation binding refuses the root as "no asset root" -- read as
    // model-missing on the first forward run -- and the product drops --tf-root.
    std::error_code directoryError;
    std::filesystem::create_directories(materializeDir / "maps", directoryError);
    std::filesystem::create_directories(materializeDir / "materials", directoryError);
    const std::vector<std::uint8_t> markerText = {'#', ' ', 'T', '3', '0', ' ', 'f',
      'i', 'x', 't', 'u', 'r', 'e', ' ', 'r', 'o', 'o', 't', '\n'};
    for (const char* marker : {"maps/.gitkeep", "materials/.gitkeep"}) {
      if (!writeBytes(materializeDir / marker, markerText, materialiseError)) break;
    }
    std::size_t written = 0;
    for (const auto& file : files) if (!file.dropped && file.bytes != 0) ++written;
    std::fprintf(stderr,
      "robust materialize root=%s model=%s fixture=%s written=%zu dropped=%s animations=%s error=%s\n",
      materializeDir.generic_string().c_str(), requestedPath.c_str(), fixture.c_str(), written,
      drop.empty() ? "-" : drop.c_str(),
      animationsCompanion.empty() ? "-" : animationsCompanion.c_str(),
      materialiseError.empty() ? "-" : materialiseError.c_str());
    for (const auto& file : files) {
      std::fprintf(stderr, "robust materialized path=%s bytes=%llu dropped=%d\n",
        file.path.c_str(), static_cast<unsigned long long>(file.bytes), file.dropped ? 1 : 0);
    }
    std::fflush(stderr);
    if (!materialiseError.empty()) return 1;
    if (materializeOnly) return 0;
  }

  // ---- inspection ----------------------------------------------------------
  ModelInspection inspection;
  if (loose) {
    inspection = tf2::native::ModelLoader::inspect(candidate.looseMdl);
  } else if (packed) {
    if (candidate.vpkArchives.size() != 1) {
      std::fprintf(stderr, "robust error=archive set is not exactly one: %zu\n",
        candidate.vpkArchives.size());
      return 1;
    }
    tf2::native::VpkArchive archive;
    std::string error;
    if (!archive.open(candidate.vpkArchives.front(), &error)) {
      std::fprintf(stderr, "robust error=archive did not open: %s\n", error.c_str());
      return 1;
    }
    inspection = tf2::native::ModelLoader::inspectVpk(archive, candidate.requestedPath);
  } else {
    std::fprintf(stderr, "robust error=model did not resolve: %s\n", model.c_str());
    return 1;
  }

  const auto& metadata = inspection.metadata;

  std::uint32_t targetLod = 0;
  std::set<std::uint32_t> lods;
  std::size_t lod0Descriptors = 0;
  for (const auto& descriptor : metadata.indices) {
    lods.insert(descriptor.lod);
    if (descriptor.lod == 0) ++lod0Descriptors;
  }
  if (!metadata.indices.empty()) {
    targetLod = metadata.indices.front().lod;
    for (const auto& descriptor : metadata.indices) targetLod = std::min(targetLod, descriptor.lod);
  }
  // ---- fixtures ------------------------------------------------------------
  // Applied to the decoded model, not to the file: the same entry point the
  // builder reads, with one state changed.
  if (fixture == "no-descriptors") {
    inspection.metadata.indices.clear();
  } else if (fixture == "lod0-absent") {
    auto& indices = inspection.metadata.indices;
    indices.erase(std::remove_if(indices.begin(), indices.end(),
      [](const tf2::native::ModelIndexDescriptor& descriptor) { return descriptor.lod == 0; }),
      indices.end());
  } else if (fixture != "none") {
    for (auto& descriptor : inspection.metadata.indices) {
      if (descriptor.lod != targetLod) continue;
      if (fixture == "descriptor-not-triangles") {
        descriptor.renderIndexCount += 1;
      } else if (fixture == "descriptor-out-of-bounds") {
        descriptor.renderIndexStart = static_cast<std::uint32_t>(
          inspection.metadata.renderIndices.size() + 1024);
      }
      break;
    }
    if (fixture == "bone-out-of-range") inspection.metadata.boneCount = 0;
  }

  ModelMeshData mesh;
  std::string meshError;
  const bool meshBuilt = tf2::native::ModelLoader::buildModelMeshDataAll(inspection, mesh, meshError);

  std::size_t targetDescriptors = 0;
  std::size_t targetIndices = 0;
  std::size_t targetWithoutMaterial = 0;
  std::uint32_t effectiveLod = targetLod;
  // The LOD view is recomputed AFTER the fixture, so a run that removed the LOD-0
  // descriptors reports the LOD it actually selected and calls that a fallback.
  // The first version computed lodFallback before the fixture and read 0 on a run
  // whose own targetLod was 1, which is the one number the fallback claim needs.
  lods.clear();
  lod0Descriptors = 0;
  for (const auto& descriptor : metadata.indices) {
    lods.insert(descriptor.lod);
    if (descriptor.lod == 0) ++lod0Descriptors;
  }
  if (!metadata.indices.empty()) {
    effectiveLod = metadata.indices.front().lod;
    for (const auto& descriptor : metadata.indices) {
      effectiveLod = std::min(effectiveLod, descriptor.lod);
    }
  }
  const bool lodFallback = !metadata.indices.empty() && effectiveLod > 0;
  for (const auto& descriptor : metadata.indices) {
    if (descriptor.lod != effectiveLod) continue;
    ++targetDescriptors;
    targetIndices += descriptor.renderIndexCount;
    const auto slot = metadata.meshMaterials.available
      ? metadata.meshMaterials.slotFor(descriptor.bodyPart, descriptor.model, descriptor.mesh) : -1;
    if (slot < 0) ++targetWithoutMaterial;
  }

  // The material table the product itself resolves for the descriptor the mesh
  // was built from -- shared reader, so 'which VTF does this range bind' is the
  // same answer the draw pass gets.
  tf2::native::ViewModelMaterialTable materials;
  if (meshBuilt) {
    materials = tf2::native::resolveViewModelMaterialTable(root, metadata,
      static_cast<std::int32_t>(mesh.descriptorIndex));
  }

  // ---- the animation source ------------------------------------------------
  AnimationBindingCache animationCache;
  animationCache.reset(&root);
  AnimationInputStatus animationStatus = AnimationInputStatus::ModelMissing;
  std::string animationReason;
  const auto* binding = animationCache.resolve(candidate.requestedPath.empty()
    ? model : candidate.requestedPath, animationStatus, animationReason);
  std::size_t boundSequences = 0;
  if (binding != nullptr) {
    for (const auto& sequence : binding->anims.sequences) {
      if (sequence.animDesc >= 0
          && static_cast<std::size_t>(sequence.animDesc) < binding->anims.animations.size()) {
        ++boundSequences;
      }
    }
  }

  // ---- refusals ------------------------------------------------------------
  std::vector<std::string> refusals;
  if (!inspection.renderableResourceSet) refusals.push_back("model has no renderable resource set");
  if (metadata.indices.empty()) refusals.push_back("no VTX descriptor decoded");
  if (!meshBuilt) {
    refusals.push_back(meshError.empty() ? "mesh builder returned no reason" : meshError);
  }
  if (meshBuilt && mesh.ranges.size() != targetDescriptors) {
    refusals.push_back("builder produced " + std::to_string(mesh.ranges.size()) + " ranges for "
      + std::to_string(targetDescriptors) + " target-LOD descriptors");
  }
  if (meshBuilt && mesh.vertices.size() != targetIndices) {
    refusals.push_back("builder produced " + std::to_string(mesh.vertices.size())
      + " vertices for " + std::to_string(targetIndices) + " target-LOD render indices");
  }
  if (targetWithoutMaterial != 0) {
    refusals.push_back(std::to_string(targetWithoutMaterial)
      + " target-LOD descriptor(s) have no material slot");
  }

  std::string rangeRows;
  std::set<std::string> rangeMaterials;
  std::set<std::string> rangeTextures;
  for (const auto& range : mesh.ranges) {
    int maxBone = -1;
    std::size_t badWeights = 0;
    rangeSkinReadings(mesh, range, maxBone, badWeights);
    const auto name = materialName(metadata, range.materialSlot);
    std::string vtfPath;
    std::string vtfStatus = "unresolved";
    if (range.materialSlot >= 0
        && static_cast<std::size_t>(range.materialSlot) < materials.slots.size()) {
      const auto& slot = materials.slots[static_cast<std::size_t>(range.materialSlot)];
      vtfPath = slot.vtfPath;
      vtfStatus = vtfStatusName(slot.status);
    }
    if (!name.empty()) rangeMaterials.insert(name);
    if (!vtfPath.empty()) rangeTextures.insert(vtfPath);
    if (!rangeRows.empty()) rangeRows += ",";
    rangeRows += "{\"descriptor\":" + std::to_string(range.descriptorIndex)
      + ",\"first\":" + std::to_string(range.firstVertex)
      + ",\"count\":" + std::to_string(range.vertexCount)
      + ",\"material\":" + std::to_string(range.materialSlot)
      + ",\"materialName\":\"" + jsonEscape(name) + "\""
      + ",\"vtf\":\"" + jsonEscape(vtfPath) + "\""
      + ",\"vtfStatus\":\"" + jsonEscape(vtfStatus) + "\""
      + ",\"maxBone\":" + std::to_string(maxBone)
      + ",\"badWeights\":" + std::to_string(badWeights) + "}";
    std::fprintf(stderr,
      "robust range descriptor=%zu count=%u slot=%d name=%s vtf=%s vtfStatus=%s "
      "maxBone=%d badWeights=%zu\n",
      range.descriptorIndex, range.vertexCount, range.materialSlot,
      name.empty() ? "-" : name.c_str(), vtfPath.empty() ? "-" : vtfPath.c_str(),
      vtfStatus.c_str(), maxBone, badWeights);
  }

  std::string refusalRows;
  for (const auto& refusal : refusals) {
    if (!refusalRows.empty()) refusalRows += ",";
    refusalRows += "\"" + jsonEscape(refusal) + "\"";
    std::fprintf(stderr, "robust refusal %s\n", refusal.c_str());
  }

  const std::string animPathValue = binding != nullptr ? binding->animPath : std::string();
  const std::size_t renderBones = binding != nullptr ? binding->renderBones : 0;
  const std::size_t validBindBones = binding != nullptr ? binding->validBindBones : 0;
  std::fprintf(stderr,
    "robust model=%s resolution=%s fixture=%s mdlFound=%d vvd=%d vtx=%d companions=%d "
    "bones=%zu seq=%zu attach=%u slots=%zu lodCount=%zu lod0=%zu targetLod=%u lodFallback=%d "
    "targetDesc=%zu targetIdx=%zu meshBuilt=%d meshRanges=%zu meshVertices=%zu "
    "distinctRangeMaterials=%zu distinctRangeTextures=%zu materialTable=%s "
    "animationInput=%s animPath=%s separateAnimSource=%d animSeqs=%zu boundSeqs=%zu "
    "bindBones=%zu/%zu materialized=%s dropped=%s error=%s\n",
    candidate.requestedPath.c_str(), resolutionName(candidate.resolution), fixture.c_str(),
    candidate.mdlFound ? 1 : 0, candidate.vvdFound ? 1 : 0,
    (candidate.dx90VtxFound || candidate.dx80VtxFound || candidate.swVtxFound) ? 1 : 0,
    candidate.companionSetComplete ? 1 : 0,
    metadata.bones.size(), metadata.sequences.size(), metadata.attachmentCount,
    metadata.textureCandidates.size(), lods.size(), lod0Descriptors, effectiveLod,
    lodFallback ? 1 : 0, targetDescriptors, targetIndices, meshBuilt ? 1 : 0,
    mesh.ranges.size(), mesh.vertices.size(), rangeMaterials.size(), rangeTextures.size(),
    metadata.meshMaterials.available ? "available" : "unavailable",
    tf2::native::animationInputStatusName(animationStatus),
    animPathValue.empty() ? "-" : animPathValue.c_str(),
    binding != nullptr && binding->separateAnimSource ? 1 : 0,
    binding != nullptr ? binding->anims.animations.size() : 0, boundSequences,
    validBindBones, renderBones,
    materializeDir.empty() ? "-" : materializeDir.generic_string().c_str(),
    drop.empty() ? "-" : drop.c_str(),
    meshError.empty() ? (animationReason.empty() ? "-" : animationReason.c_str())
                      : meshError.c_str());
  std::fprintf(stderr, "robust companion declared=%s present=%d status=%s reason=%s\n",
    animPathValue.empty() ? "-" : animPathValue.c_str(),
    binding != nullptr ? 1 : 0,
    tf2::native::animationInputStatusName(animationStatus),
    animationReason.empty() ? "-" : animationReason.c_str());
  std::fflush(stderr);

  std::string descriptorRows;
  for (const auto& descriptor : metadata.indices) {
    if (!descriptorRows.empty()) descriptorRows += ",";
    const auto slot = metadata.meshMaterials.available
      ? metadata.meshMaterials.slotFor(descriptor.bodyPart, descriptor.model, descriptor.mesh) : -1;
    descriptorRows += "{\"lod\":" + std::to_string(descriptor.lod)
      + ",\"mesh\":" + std::to_string(descriptor.mesh)
      + ",\"indices\":" + std::to_string(descriptor.renderIndexCount)
      + ",\"material\":" + std::to_string(slot) + "}";
  }

  std::cout << "{\"model\":\"" << jsonEscape(candidate.requestedPath) << "\""
    << ",\"resolution\":\"" << resolutionName(candidate.resolution) << "\""
    << ",\"fixture\":\"" << jsonEscape(fixture) << "\""
    << ",\"mdlFound\":" << (candidate.mdlFound ? "true" : "false")
    << ",\"vvd\":" << (candidate.vvdFound ? "true" : "false")
    << ",\"vtx\":" << ((candidate.dx90VtxFound || candidate.dx80VtxFound || candidate.swVtxFound) ? "true" : "false")
    << ",\"companions\":" << (candidate.companionSetComplete ? "true" : "false")
    << ",\"bones\":" << metadata.bones.size()
    << ",\"sequences\":" << metadata.sequences.size()
    << ",\"attachments\":" << metadata.attachmentCount
    << ",\"textureSlots\":" << metadata.textureCandidates.size()
    << ",\"lodCount\":" << lods.size()
    << ",\"lod0Descriptors\":" << lod0Descriptors
    << ",\"targetLod\":" << effectiveLod
    << ",\"lodFallback\":" << (lodFallback ? "true" : "false")
    << ",\"targetDescriptors\":" << targetDescriptors
    << ",\"targetIndices\":" << targetIndices
    << ",\"meshBuilt\":" << (meshBuilt ? "true" : "false")
    << ",\"meshRanges\":" << mesh.ranges.size()
    << ",\"meshVertices\":" << mesh.vertices.size()
    << ",\"distinctRangeMaterials\":" << rangeMaterials.size()
    << ",\"distinctRangeTextures\":" << rangeTextures.size()
    << ",\"materialTable\":\"" << (metadata.meshMaterials.available ? "available" : "unavailable") << "\""
    << ",\"animationInput\":\"" << tf2::native::animationInputStatusName(animationStatus) << "\""
    << ",\"animPath\":\"" << jsonEscape(animPathValue) << "\""
    << ",\"animSequences\":" << (binding != nullptr ? binding->anims.animations.size() : 0)
    << ",\"boundSequences\":" << boundSequences
    << ",\"renderBones\":" << renderBones
    << ",\"validBindBones\":" << validBindBones
    << ",\"animationReason\":\"" << jsonEscape(animationReason) << "\""
    << ",\"meshError\":\"" << jsonEscape(meshError) << "\""
    << ",\"descriptors\":[" << descriptorRows << "]"
    << ",\"ranges\":[" << rangeRows << "]"
    << ",\"refusals\":[" << refusalRows << "]}\n";
  return refusals.empty() ? 0 : 1;
}
