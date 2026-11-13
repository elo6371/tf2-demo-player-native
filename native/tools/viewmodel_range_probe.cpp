// Dumps the mesh range table a ViewModel request is actually built from.
//
// WHY THIS PROBE EXISTS
//   F3 of the release blocker list says `c_sniper_arms.mdl` "currently exposes
//   only one 600-vertex range in the visible path". A count like that can be
//   produced by at least four different causes -- one LOD-0 descriptor, a
//   descriptor table truncated by a stride guess, a mesh builder that stops
//   after the first descriptor, or a model that genuinely has one mesh -- and
//   they are indistinguishable from the pass's own `vertices=` field. This
//   probe prints the table itself: every VTX descriptor the parser decoded,
//   every range the mesh builder produced, the material slot and material name
//   bound to each, and the highest bone index each range's vertices reference.
//
// WHAT IT DOES NOT DO
//   It does not draw and it does not decide whether the hands look right. The
//   readings here are the input to that judgement, not a substitute for it.
//
// USAGE
//   viewmodel_range_probe --tf-root <tf> --model <models/...mdl> [--all-lods]
//                         [--fixture <name>]
//
//   --fixture applies one defect to the decoded model before the builder runs,
//   through the same entry point the product uses. A shipped model cannot show
//   these states, so a gate that only read shipped models could not prove the
//   refusal path exists at all:
//     descriptor-not-triangles  a LOD-0 descriptor whose index count is not a
//                               multiple of three
//     descriptor-out-of-bounds  a LOD-0 descriptor whose render indices start
//                               past the end of the index array
//     bone-out-of-range         the skeleton shrunk so every vertex's bone
//                               indices fall outside it
//     no-descriptors            the descriptor table emptied
//
//   Exit 0 when the model resolved and every target-LOD descriptor became a
//   range with a material slot; exit 1 when something was refused, with the
//   refusal named on stderr. Exit 2 is a usage error.

#include "asset_root.h"
#include "model_loader.h"

#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace {

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
  std::cerr << "usage: viewmodel_range_probe --tf-root <tf> --model <path.mdl>"
               " [--all-lods] [--fixture descriptor-not-triangles|descriptor-out-of-bounds|"
               "bone-out-of-range|no-descriptors]\n";
}

const char* materialName(const tf2::native::ModelMetadata& metadata, std::int32_t slot) {
  if (slot < 0) return "";
  const auto index = static_cast<std::size_t>(slot);
  if (index < metadata.textureNames.size()) return metadata.textureNames[index].c_str();
  return "";
}

// The highest bone index any vertex of this range references, and the number of
// vertices whose weights do not sum to one. Both are read from the vertices the
// builder produced, so they describe what will be uploaded rather than what the
// header claims.
void rangeSkinReadings(const tf2::native::ModelMeshData& mesh,
    const tf2::native::ModelMeshData::Range& range, int& maxBone, std::size_t& badWeights) {
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

} // namespace

int main(int argc, char** argv) {
  std::filesystem::path tfRoot;
  std::string model;
  std::string fixture = "none";
  bool allLods = false;
  for (int index = 1; index < argc; ++index) {
    const std::string argument = argv[index];
    if (argument == "--tf-root" && index + 1 < argc) {
      tfRoot = std::filesystem::u8path(argv[++index]);
    } else if (argument == "--model" && index + 1 < argc) {
      model = argv[++index];
    } else if (argument == "--fixture" && index + 1 < argc) {
      fixture = argv[++index];
    } else if (argument == "--all-lods") {
      allLods = true;
    } else {
      usage();
      return 2;
    }
  }
  if (tfRoot.empty() || model.empty()) {
    usage();
    return 2;
  }
  const bool knownFixture = fixture == "none" || fixture == "descriptor-not-triangles"
    || fixture == "descriptor-out-of-bounds" || fixture == "bone-out-of-range"
    || fixture == "no-descriptors";
  if (!knownFixture) {
    std::fprintf(stderr, "range: unknown fixture '%s'\n", fixture.c_str());
    return 2;
  }

  const auto root = tf2::native::AssetRoot::fromPath(tfRoot);
  const auto candidate = tf2::native::ModelLoader::resolveAsset(root, model);
  std::string resolution;
  switch (candidate.resolution) {
    case tf2::native::ModelAssetResolution::FoundLoose: resolution = "found-loose"; break;
    case tf2::native::ModelAssetResolution::FoundVpk: resolution = "found-vpk"; break;
    case tf2::native::ModelAssetResolution::Ambiguous: resolution = "ambiguous"; break;
    default: resolution = "missing"; break;
  }

  tf2::native::ModelInspection inspection;
  if (candidate.resolution == tf2::native::ModelAssetResolution::FoundLoose) {
    inspection = tf2::native::ModelLoader::inspect(candidate.looseMdl);
  } else if (candidate.resolution == tf2::native::ModelAssetResolution::FoundVpk) {
    if (candidate.vpkArchives.size() != 1) {
      std::fprintf(stderr, "range: archive set is not exactly one: %zu\n",
        candidate.vpkArchives.size());
      return 1;
    }
    tf2::native::VpkArchive archive;
    std::string error;
    if (!archive.open(candidate.vpkArchives.front(), &error)) {
      std::fprintf(stderr, "range: archive did not open: %s\n", error.c_str());
      return 1;
    }
    inspection = tf2::native::ModelLoader::inspectVpk(archive, model);
  } else {
    std::fprintf(stderr, "range: model did not resolve: %s\n", model.c_str());
    return 1;
  }

  const auto& metadata = inspection.metadata;
  const auto& diag = metadata.vtxDiagnostics;

  // The LOD the builder will draw, computed the same way the builder computes
  // it. If the two ever disagree the readings below stop describing the mesh,
  // which is why the builder also refuses a short range list.
  std::uint32_t targetLod = metadata.indices.empty() ? 0u : metadata.indices.front().lod;
  for (const auto& descriptor : metadata.indices) {
    targetLod = std::min(targetLod, descriptor.lod);
  }

  // The fixture is applied to the decoded model, not to the file: the defect is
  // injected where the builder reads it, which is the same function the product
  // calls on a real model with different input.
  if (fixture == "no-descriptors") {
    inspection.metadata.indices.clear();
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

  tf2::native::ModelMeshData mesh;
  std::string meshError;
  const bool meshBuilt = tf2::native::ModelLoader::buildModelMeshDataAll(inspection, mesh, meshError);

  std::size_t targetDescriptors = 0;
  std::size_t targetIndices = 0;
  std::size_t targetWithoutMaterial = 0;
  for (const auto& descriptor : metadata.indices) {
    if (descriptor.lod != targetLod) continue;
    ++targetDescriptors;
    targetIndices += descriptor.renderIndexCount;
    const auto slot = metadata.meshMaterials.available
      ? metadata.meshMaterials.slotFor(descriptor.bodyPart, descriptor.model, descriptor.mesh) : -1;
    if (slot < 0) ++targetWithoutMaterial;
  }

  std::fprintf(stderr,
    "range model=%s resolution=%s fixture=%s bodyParts=%u bones=%zu vvdVertices=%zu "
    "vtxDescriptors=%zu vtxOutOfBounds=%zu targetLod=%u targetDescriptors=%zu "
    "targetIndices=%zu meshBuilt=%d meshRanges=%zu meshVertices=%zu materialTable=%s error=%s\n",
    model.c_str(), resolution.c_str(), fixture.c_str(), metadata.bodyPartCount,
    metadata.bones.size(), metadata.vertices.size(), diag.vtxDescriptorCount,
    diag.vtxOutOfBoundsCount, targetLod, targetDescriptors, targetIndices,
    meshBuilt ? 1 : 0, mesh.ranges.size(), mesh.vertices.size(),
    metadata.meshMaterials.available ? "available" : "unavailable",
    meshError.empty() ? "-" : meshError.c_str());
  for (const auto& descriptor : metadata.indices) {
    if (!allLods && descriptor.lod != targetLod) continue;
    const auto slot = metadata.meshMaterials.available
      ? metadata.meshMaterials.slotFor(descriptor.bodyPart, descriptor.model, descriptor.mesh) : -1;
    std::fprintf(stderr,
      "range descriptor bp=%u model=%u lod=%u mesh=%u group=%u vertices=%u indices=%u "
      "renderStart=%u material=%d name=%s\n",
      descriptor.bodyPart, descriptor.model, descriptor.lod, descriptor.mesh,
      descriptor.stripGroup, descriptor.vertexCount, descriptor.renderIndexCount,
      descriptor.renderIndexStart, slot, materialName(metadata, slot));
  }
  for (const auto& range : mesh.ranges) {
    int maxBone = -1;
    std::size_t badWeights = 0;
    rangeSkinReadings(mesh, range, maxBone, badWeights);
    std::fprintf(stderr,
      "range built descriptor=%zu first=%u count=%u material=%d name=%s maxBone=%d badWeights=%zu\n",
      range.descriptorIndex, range.firstVertex, range.vertexCount, range.materialSlot,
      materialName(metadata, range.materialSlot), maxBone, badWeights);
  }
  std::fflush(stderr);

  // What a pass would have to refuse rather than draw partially.
  std::vector<std::string> refusals;
  if (!inspection.renderableResourceSet) refusals.push_back("model has no renderable resource set");
  if (metadata.indices.empty()) refusals.push_back("no VTX descriptor decoded");
  if (!meshBuilt) {
    refusals.push_back(meshError.empty() ? "mesh builder returned no reason" : meshError);
  }
  if (meshBuilt && mesh.ranges.size() != targetDescriptors) {
    refusals.push_back("builder produced " + std::to_string(mesh.ranges.size())
      + " ranges for " + std::to_string(targetDescriptors) + " target-LOD descriptors");
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
  for (const auto& range : mesh.ranges) {
    int maxBone = -1;
    std::size_t badWeights = 0;
    rangeSkinReadings(mesh, range, maxBone, badWeights);
    if (!rangeRows.empty()) rangeRows += ",";
    rangeRows += "{\"descriptor\":" + std::to_string(range.descriptorIndex)
      + ",\"first\":" + std::to_string(range.firstVertex)
      + ",\"count\":" + std::to_string(range.vertexCount)
      + ",\"material\":" + std::to_string(range.materialSlot)
      + ",\"materialName\":\"" + jsonEscape(materialName(metadata, range.materialSlot)) + "\""
      + ",\"maxBone\":" + std::to_string(maxBone)
      + ",\"badWeights\":" + std::to_string(badWeights) + "}";
  }
  std::string descriptorRows;
  for (const auto& descriptor : metadata.indices) {
    if (!allLods && descriptor.lod != targetLod) continue;
    if (!descriptorRows.empty()) descriptorRows += ",";
    const auto slot = metadata.meshMaterials.available
      ? metadata.meshMaterials.slotFor(descriptor.bodyPart, descriptor.model, descriptor.mesh) : -1;
    descriptorRows += "{\"bp\":" + std::to_string(descriptor.bodyPart)
      + ",\"model\":" + std::to_string(descriptor.model)
      + ",\"lod\":" + std::to_string(descriptor.lod)
      + ",\"mesh\":" + std::to_string(descriptor.mesh)
      + ",\"group\":" + std::to_string(descriptor.stripGroup)
      + ",\"vertices\":" + std::to_string(descriptor.vertexCount)
      + ",\"indices\":" + std::to_string(descriptor.renderIndexCount)
      + ",\"material\":" + std::to_string(slot)
      + ",\"materialName\":\"" + jsonEscape(materialName(metadata, slot)) + "\"}";
  }
  std::string refusalRows;
  for (const auto& refusal : refusals) {
    if (!refusalRows.empty()) refusalRows += ",";
    refusalRows += "\"" + jsonEscape(refusal) + "\"";
  }

  std::cout << "{\"model\":\"" << jsonEscape(model) << "\""
    << ",\"fixture\":\"" << jsonEscape(fixture) << "\""
    << ",\"resolution\":\"" << resolution << "\""
    << ",\"bodyParts\":" << metadata.bodyPartCount
    << ",\"bones\":" << metadata.bones.size()
    << ",\"sequences\":" << metadata.sequences.size()
    << ",\"textureSlots\":" << metadata.textureCandidates.size()
    << ",\"vvdVertices\":" << metadata.vertices.size()
    << ",\"vtxDescriptors\":" << diag.vtxDescriptorCount
    << ",\"vtxOutOfBounds\":" << diag.vtxOutOfBoundsCount
    << ",\"vtxDegenerate\":" << diag.vtxDegenerateTriangleCount
    << ",\"targetLod\":" << targetLod
    << ",\"targetDescriptors\":" << targetDescriptors
    << ",\"targetIndices\":" << targetIndices
    << ",\"meshBuilt\":" << (meshBuilt ? "true" : "false")
    << ",\"meshRanges\":" << mesh.ranges.size()
    << ",\"meshVertices\":" << mesh.vertices.size()
    << ",\"meshIndices\":" << mesh.indices.size()
    << ",\"materialTable\":\"" << (metadata.meshMaterials.available ? "available" : "unavailable") << "\""
    << ",\"materialReason\":\"" << jsonEscape(metadata.meshMaterials.reason) << "\""
    << ",\"meshError\":\"" << jsonEscape(meshError) << "\""
    << ",\"descriptors\":[" << descriptorRows << "]"
    << ",\"ranges\":[" << rangeRows << "]"
    << ",\"refusals\":[" << refusalRows << "]"
    << "}\n";
  return refusals.empty() ? 0 : 1;
}
