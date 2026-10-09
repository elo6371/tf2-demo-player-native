#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace tf2::native {

class VpkArchive {
public:
  bool open(const std::filesystem::path& directoryFile, std::string* error = nullptr);
  bool contains(const std::string& path) const;
  // Return normalized archive paths under an optional directory prefix and
  // with an optional extension filter. Results are sorted for deterministic
  // resource discovery and are independent of VPK tree insertion order.
  std::vector<std::string> list(const std::string& prefix = {},
    const std::string& extension = {}) const;
  std::vector<std::uint8_t> read(const std::string& path, std::string* error = nullptr) const;
  std::size_t entryCount() const { return entries_.size(); }

private:
  struct Entry {
    std::filesystem::path archiveFile;
    std::uint32_t offset = 0;
    std::uint32_t length = 0;
    std::vector<std::uint8_t> preload;
  };

  std::filesystem::path directoryFile_;
  std::uint64_t dataBase_ = 0;
  std::unordered_map<std::string, Entry> entries_;
};

// Every *_dir.vpk directly under a tf directory, opened once and reused.
//
// VpkArchive::open() reads the whole directory tree into memory and builds a
// hash map of every entry -- tens of megabytes and roughly 50k entries for
// tf2_misc_dir.vpk. Opening an archive per lookup therefore cost about a second
// per model, and ModelLoader::resolveAsset did it once for the model plus five
// more times for the companion suffixes (.vvd/.dx90.vtx/.dx80.vtx/.sw.vtx/.phy),
// across every *_dir.vpk in the directory. That was invisible while no demo
// model path resolved (the resolve loop never ran) and turned into a ~96 s
// stall once the P1 modelprecache work started feeding it 505 real paths.
//
// Resolving a model is a pure function of the archive contents, so open them
// once and share the result. Iteration order matches std::filesystem::
// directory_iterator on the tf directory, which is what callers relied on when
// they took vpkArchives.front().
class VpkArchiveSet {
public:
  static VpkArchiveSet openDirectory(const std::filesystem::path& tfDirectory);
  // Archive paths holding normalizedPath, appended in directory order.
  void collectContaining(const std::string& normalizedPath,
    std::vector<std::filesystem::path>& out) const;
  const VpkArchive* find(const std::filesystem::path& archivePath) const;
  bool empty() const { return archives_.empty(); }
  std::size_t size() const { return archives_.size(); }
  std::size_t totalEntryCount() const;

private:
  struct Held {
    std::filesystem::path path;
    VpkArchive archive;
  };
  std::vector<Held> archives_;
};

} // namespace tf2::native
