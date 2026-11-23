#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace tf2::native {

class VpkArchive {
public:
  // Which store answered a read. `PreloadOnly` means the bytes lived in the
  // directory tree itself and no data file was touched at all -- a zero-length
  // entry and a fully preloaded one look the same to a caller that only reads
  // bytes, and they are not the same thing.
  enum class Store { PreloadOnly, DirectoryData, NumberedVolume };

  // read() used to answer with bytes alone, so "the volume is missing" and "the
  // volume is there but the offset points past its end" both arrived as an empty
  // vector. The caller could not tell a refusal from an empty file. ReadResult
  // carries the reason and the store that produced the bytes.
  struct ReadResult {
    std::vector<std::uint8_t> bytes;
    Store store = Store::PreloadOnly;
    bool found = false;  // the name exists in this archive's tree
    std::string error;   // named refusal; empty when the read succeeded
    bool ok() const { return error.empty(); }
  };

  bool open(const std::filesystem::path& directoryFile, std::string* error = nullptr);
  bool contains(const std::string& path) const;
  // Return normalized archive paths under an optional directory prefix and
  // with an optional extension filter. Results are sorted for deterministic
  // resource discovery and are independent of VPK tree insertion order.
  std::vector<std::string> list(const std::string& prefix = {},
    const std::string& extension = {}) const;
  std::vector<std::uint8_t> read(const std::string& path, std::string* error = nullptr) const;
  ReadResult readEntry(const std::string& path) const;
  std::size_t entryCount() const { return entries_.size(); }

private:
  struct Entry {
    std::filesystem::path archiveFile;
    // 64-bit on purpose: a 32-bit offset wrapped when the tree base was added to
    // it, and a wrapped offset is an out-of-range read that still "succeeds".
    std::uint64_t offset = 0;
    std::uint32_t length = 0;
    bool inlineData = false;
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
