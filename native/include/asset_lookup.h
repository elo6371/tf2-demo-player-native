#pragma once

#include "vpk_archive.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace tf2::native {

// Which store answered a lookup. The product treats these as one search, but
// "resolved" is not the whole statement: a resource read out of a numbered
// volume and one read out of loose files fail for different reasons when a
// custom build moves or replaces them, and only the store name says which.
enum class AssetStore { None, Loose, VpkDirectory, VpkVolume, Pakfile };

const char* assetStoreName(AssetStore store);

struct AssetLookup {
  std::vector<std::uint8_t> bytes;
  AssetStore store = AssetStore::None;
  // The concrete file that answered: "loose", "pak01_dir.vpk", "pak01_000.vpk"
  // or "pakfile". Empty when nothing answered.
  std::string origin;
  // Named reason from the last store that was reached and refused. Empty when
  // the resource was found, and also empty when no store had the name at all --
  // "absent" and "present but unreadable" must not collapse into one string.
  std::string error;

  bool found() const { return store != AssetStore::None; }
};

// The lookup policy, in one place: the loose tf file first, then the archives
// in the order they were added, then the map's own pakfile.
//
// It used to be open-coded at six call sites in main.cpp with the same
// loose-then-archives loop, and each site could drift from the others without
// anything noticing. T12 made the three lookups a *material* performs share one
// order; this makes the order itself a single object that a probe can drive.
//
// The "first non-empty answer wins" rule is the pre-existing behaviour and is
// kept deliberately: an archive that holds a zero-length entry does not stop the
// search, which is what the product did before this file existed.
class AssetStoreChain {
public:
  using PakfileReader =
    std::function<bool(const std::string&, std::vector<std::uint8_t>&, std::string&)>;

  explicit AssetStoreChain(std::filesystem::path looseRoot = {});

  // Archives are shared with the caller so the product keeps its existing
  // sound/archive objects instead of opening every *_dir.vpk twice.
  void addArchive(std::shared_ptr<VpkArchive> archive, std::filesystem::path directoryFile);
  void setPakfileReader(PakfileReader reader);

  AssetLookup lookup(const std::string& path) const;

  std::size_t archiveCount() const { return archives_.size(); }
  const std::vector<std::filesystem::path>& archivePaths() const { return archivePaths_; }

private:
  std::filesystem::path looseRoot_;
  std::vector<std::shared_ptr<VpkArchive>> archives_;
  std::vector<std::filesystem::path> archivePaths_;
  PakfileReader pakfile_;
};

} // namespace tf2::native
