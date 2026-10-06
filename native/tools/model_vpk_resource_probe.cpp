#include "asset_root.h"
#include "model_loader.h"

#include <filesystem>
#include <iostream>
#include <string>

namespace {
const char* resolution(tf2::native::ModelAssetResolution value) {
  switch (value) {
    case tf2::native::ModelAssetResolution::FoundLoose: return "loose";
    case tf2::native::ModelAssetResolution::FoundVpk: return "vpk";
    case tf2::native::ModelAssetResolution::Ambiguous: return "ambiguous";
    case tf2::native::ModelAssetResolution::Unknown: return "unknown";
    case tf2::native::ModelAssetResolution::Missing: default: return "missing";
  }
}
}

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "usage: model_vpk_resource_probe <tf-directory>\n";
    return 2;
  }
  const auto root = tf2::native::AssetRoot::fromPath(std::filesystem::u8path(argv[1]));
  if (!root.valid()) { std::cerr << "invalid_tf_root\n"; return 1; }
  const std::string paths[] = {
    "models/player/demo.mdl",
    "models/weapons/v_models/v_bat_scout.mdl",
    "models/weapons/w_models/w_rocketlauncher.mdl",
  };
  bool allKnown = true;
  bool allCompanions = true;
  for (const auto& path : paths) {
    const auto candidate = tf2::native::ModelLoader::resolveAsset(root, path);
    const bool known = candidate.resolution == tf2::native::ModelAssetResolution::FoundLoose
      || candidate.resolution == tf2::native::ModelAssetResolution::FoundVpk
      || candidate.resolution == tf2::native::ModelAssetResolution::Ambiguous;
    allKnown = allKnown && known;
    allCompanions = allCompanions && candidate.companionSetComplete;
    std::cout << "path=" << path
      << " resolution=" << resolution(candidate.resolution)
      << " mdl=" << (candidate.mdlFound ? 1 : 0)
      << " vvd=" << (candidate.vvdFound ? 1 : 0)
      << " dx90=" << (candidate.dx90VtxFound ? 1 : 0)
      << " dx80=" << (candidate.dx80VtxFound ? 1 : 0)
      << " sw=" << (candidate.swVtxFound ? 1 : 0)
      << " phy=" << (candidate.phyFound ? 1 : 0)
      << " companions=" << (candidate.companionSetComplete ? 1 : 0) << '\n';
  }
  const auto missing = tf2::native::ModelLoader::resolveAsset(root, "models/does_not_exist_resource_probe.mdl");
  const bool missingRejected = missing.resolution == tf2::native::ModelAssetResolution::Missing
    && !missing.companionSetComplete;
  std::cout << "missingRejected=" << (missingRejected ? 1 : 0)
    << " allKnown=" << (allKnown ? 1 : 0)
    << " allCompanions=" << (allCompanions ? 1 : 0) << '\n';
  return allKnown && allCompanions && missingRejected ? 0 : 1;
}
