#pragma once

#include <filesystem>
#include <memory>
#include <vector>
#include <string>

namespace tf2::native {

class VpkArchiveSet;

struct AssetRoot {
  std::filesystem::path tfDirectory;
  // Filled on first use by archives(). Shared so that copies of AssetRoot stay
  // cheap and every lookup in one process reuses the same parsed directory
  // trees. See VpkArchiveSet for why re-opening them per lookup was fatal.
  mutable std::shared_ptr<const VpkArchiveSet> vpkArchives;

  static AssetRoot fromPath(const std::filesystem::path& path);
  static AssetRoot findInstalled(const std::vector<std::filesystem::path>& candidates);
  static std::vector<std::filesystem::path> steamLibraryCandidates(
    const std::vector<std::filesystem::path>& steamRoots);
  bool valid() const;
  std::filesystem::path resolve(const std::filesystem::path& relative) const;
  // All *_dir.vpk under tfDirectory, opened once for the lifetime of this root.
  const VpkArchiveSet& archives() const;
  std::string describe() const;
};

} // namespace tf2::native
