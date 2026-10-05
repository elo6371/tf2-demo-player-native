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

} // namespace tf2::native
