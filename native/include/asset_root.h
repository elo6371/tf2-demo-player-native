#pragma once

#include <filesystem>
#include <vector>
#include <string>

namespace tf2::native {

struct AssetRoot {
  std::filesystem::path tfDirectory;

  static AssetRoot fromPath(const std::filesystem::path& path);
  static AssetRoot findInstalled(const std::vector<std::filesystem::path>& candidates);
  static std::vector<std::filesystem::path> steamLibraryCandidates(
    const std::vector<std::filesystem::path>& steamRoots);
  bool valid() const;
  std::filesystem::path resolve(const std::filesystem::path& relative) const;
  std::string describe() const;
};

} // namespace tf2::native
