#include "asset_root.h"
#include "demo_header.h"
#include "model_loader.h"

#include <fstream>
#include <iostream>

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "usage: model_render_request_probe <tf-directory>\n";
    return 2;
  }
  const auto root = tf2::native::AssetRoot::fromPath(std::filesystem::u8path(argv[1]));
  if (!root.valid()) {
    std::cerr << "invalid_tf_root\n";
    return 1;
  }
  tf2::native::AssetReference vpkReference;
  vpkReference.entityIndex = 7;
  vpkReference.classId = 12;
  vpkReference.className = "CTestModel";
  vpkReference.hasModelPath = true;
  vpkReference.modelPath = "models/ambulance.mdl";
  tf2::native::AssetReference missingReference = vpkReference;
  missingReference.entityIndex = 8;
  missingReference.modelPath = "models/does_not_exist_probe.mdl";

  const auto repeated = tf2::native::ModelLoader::buildRenderRequests(root, {vpkReference, vpkReference}, nullptr);
  const auto missing = tf2::native::ModelLoader::buildRenderRequests(root, {missingReference}, nullptr);
  std::vector<tf2::native::AssetReference> boundedReferences(2050, vpkReference);
  const auto boundedRequests = tf2::native::ModelLoader::buildRenderRequests(root, boundedReferences, nullptr);
  const bool duplicateStable = repeated.size() == 2 && repeated[0].resolution == repeated[1].resolution
    && repeated[0].diagnostic == repeated[1].diagnostic;
  const bool vpkResolution = repeated.size() == 2
    && (repeated[0].resolution == tf2::native::ModelAssetResolution::FoundVpk
      || repeated[0].resolution == tf2::native::ModelAssetResolution::Ambiguous);
  const bool vpkFallback = vpkResolution && !repeated[0].renderable;
  const bool missingFallback = missing.size() == 1 && missing[0].resolution == tf2::native::ModelAssetResolution::Missing
    && !missing[0].renderable && !missing[0].companionSetComplete;
  const bool boundedOk = boundedRequests.size() == 2048;
  const auto archivePath = root.tfDirectory / "tf2_misc_dir.vpk";
  tf2::native::VpkArchive archive;
  std::string archiveError;
  bool extractedRenderable = false;
  bool malformedRejected = false;
  if (archive.open(archivePath, &archiveError)) {
    const std::filesystem::path temp = std::filesystem::temp_directory_path() / "tf2-model-render-request-probe";
    std::error_code cleanupError;
    std::filesystem::remove_all(temp, cleanupError);
    std::filesystem::create_directories(temp / "models", cleanupError);
    const std::string stem = "models/ambulance";
    const auto write = [&](const std::string& suffix, std::size_t limit = 0) {
      const auto bytes = archive.read(stem + suffix, &archiveError);
      if (bytes.empty()) return false;
      const auto target = temp / ("models/ambulance" + suffix);
      std::ofstream file(target, std::ios::binary);
      if (!file) return false;
      const auto count = limit == 0 ? bytes.size() : std::min(limit, bytes.size());
      file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(count));
      return static_cast<bool>(file);
    };
    const bool filesReady = write(".mdl") && write(".vvd") && write(".dx90.vtx");
    if (filesReady) {
      const auto inspection = tf2::native::ModelLoader::inspect(temp / "models/ambulance.mdl");
      extractedRenderable = inspection.renderableResourceSet && !inspection.metadata.indices.empty()
        && inspection.metadata.renderIndices.size() >= inspection.metadata.indices.front().renderIndexCount;
      std::error_code malformedError;
      std::filesystem::remove(temp / "models/ambulance.dx90.vtx", malformedError);
      malformedRejected = !tf2::native::ModelLoader::inspect(temp / "models/ambulance.mdl").renderableResourceSet;
    }
    std::filesystem::remove_all(temp, cleanupError);
  }
  std::cout << "duplicate_stable=" << (duplicateStable ? 1 : 0)
            << " vpk_fallback=" << (vpkFallback ? 1 : 0)
            << " vpk_resolution=" << static_cast<int>(repeated.empty() ? tf2::native::ModelAssetResolution::Unknown : repeated[0].resolution)
            << " vpk_companions=" << (!repeated.empty() && repeated[0].companionSetComplete ? 1 : 0)
            << " missing_fallback=" << (missingFallback ? 1 : 0)
            << " bounded_2048=" << (boundedOk ? 1 : 0)
            << " extracted_renderable=" << (extractedRenderable ? 1 : 0)
            << " malformed_rejected=" << (malformedRejected ? 1 : 0)
            << " requests=" << boundedRequests.size() << '\n';
  return duplicateStable && vpkFallback && missingFallback && boundedOk
    && extractedRenderable && malformedRejected ? 0 : 1;
}
