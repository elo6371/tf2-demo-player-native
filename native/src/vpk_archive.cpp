#include "vpk_archive.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <string_view>

namespace tf2::native {

namespace {
constexpr std::uint32_t kSignature = 0x55aa1234;
constexpr std::uint16_t kDirectoryArchive = 0x7fff;

std::string normalize(std::string value) {
  std::replace(value.begin(), value.end(), '\\', '/');
  while (!value.empty() && value.front() == '/') value.erase(value.begin());
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return value;
}

bool validPath(const std::string& path) {
  if (path.empty() || path.find('\0') != std::string::npos) return false;
  std::size_t start = 0;
  while (start <= path.size()) {
    const auto end = path.find('/', start);
    const auto part = path.substr(start, end == std::string::npos ? end : end - start);
    if (part.empty() || part == "." || part == "..") return false;
    if (end == std::string::npos) break;
    start = end + 1;
  }
  return true;
}

template <typename T>
bool readValue(const std::vector<std::uint8_t>& bytes, std::size_t offset, T& value) {
  if (offset + sizeof(T) > bytes.size()) return false;
  value = 0;
  for (std::size_t i = 0; i < sizeof(T); ++i) {
    value |= static_cast<T>(bytes[offset + i]) << (i * 8);
  }
  return true;
}

bool readFile(const std::filesystem::path& path, std::vector<std::uint8_t>& bytes) {
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file) return false;
  const auto size = file.tellg();
  if (size < 0) return false;
  bytes.resize(static_cast<std::size_t>(size));
  file.seekg(0);
  return bytes.empty() || static_cast<bool>(file.read(
    reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())));
}
}

bool VpkArchive::open(const std::filesystem::path& directoryFile, std::string* error) {
  entries_.clear();
  directoryFile_.clear();
  dataBase_ = 0;
  std::vector<std::uint8_t> bytes;
  if (!readFile(directoryFile, bytes)) {
    if (error) *error = "cannot read VPK directory file";
    return false;
  }
  std::uint32_t signature = 0;
  std::uint32_t version = 0;
  std::uint32_t treeSize = 0;
  if (!readValue(bytes, 0, signature) || !readValue(bytes, 4, version)
      || !readValue(bytes, 8, treeSize) || signature != kSignature
      || (version != 1 && version != 2)) {
    if (error) *error = "unsupported VPK header";
    return false;
  }
  const std::size_t headerSize = version == 2 ? 28u : 12u;
  if (headerSize > bytes.size() || treeSize == 0
      || headerSize + static_cast<std::size_t>(treeSize) > bytes.size()) {
    if (error) *error = "truncated VPK directory tree";
    return false;
  }
  const auto treeEnd = headerSize + static_cast<std::size_t>(treeSize);
  std::size_t cursor = headerSize;
  const auto readString = [&](std::string& out) {
    const auto start = cursor;
    while (cursor < treeEnd && bytes[cursor] != 0) ++cursor;
    if (cursor >= treeEnd) return false;
    out.assign(reinterpret_cast<const char*>(bytes.data() + start), cursor - start);
    ++cursor;
    return true;
  };
  auto baseName = directoryFile.filename().wstring();
  constexpr std::wstring_view directorySuffix = L"_dir.vpk";
  if (baseName.size() >= directorySuffix.size()
      && baseName.compare(baseName.size() - directorySuffix.size(),
        directorySuffix.size(), directorySuffix) == 0) {
    baseName.resize(baseName.size() - directorySuffix.size());
  }
  while (cursor < treeEnd) {
    std::string extension;
    if (!readString(extension) || extension.empty()) break;
    while (cursor < treeEnd) {
      std::string path;
      if (!readString(path) || path.empty()) break;
      while (cursor < treeEnd) {
        std::string filename;
        if (!readString(filename) || filename.empty()) break;
        if (cursor + 18 > treeEnd) {
          if (error) *error = "truncated VPK entry";
          entries_.clear();
          return false;
        }
        cursor += 4;
        std::uint16_t preloadLength = 0;
        std::uint16_t archiveIndex = 0;
        std::uint32_t offset = 0;
        std::uint32_t length = 0;
        if (!readValue(bytes, cursor, preloadLength)) return false; cursor += 2;
        if (!readValue(bytes, cursor, archiveIndex)) return false; cursor += 2;
        if (!readValue(bytes, cursor, offset)) return false; cursor += 4;
        if (!readValue(bytes, cursor, length)) return false; cursor += 4;
        std::uint16_t terminator = 0;
        if (!readValue(bytes, cursor, terminator) || terminator != 0xffff) return false;
        cursor += 2;
        if (cursor + preloadLength > treeEnd) return false;
        Entry entry{};
        entry.offset = offset;
        entry.length = length;
        entry.preload.assign(bytes.begin() + static_cast<std::ptrdiff_t>(cursor),
          bytes.begin() + static_cast<std::ptrdiff_t>(cursor + preloadLength));
        cursor += preloadLength;
        if (archiveIndex == kDirectoryArchive) {
          entry.archiveFile = directoryFile;
          entry.offset = static_cast<std::uint32_t>(treeEnd + offset);
        } else {
          entry.archiveFile = directoryFile.parent_path()
            / (baseName + L"_" + (archiveIndex < 10 ? L"00" : archiveIndex < 100 ? L"0" : L"")
              + std::to_wstring(archiveIndex) + L".vpk");
        }
        const auto key = normalize(path == " " ? "" : path + "/")
          + normalize(filename) + (extension == " " ? "" : "." + normalize(extension));
        if (validPath(key)) entries_.emplace(key, std::move(entry));
      }
    }
  }
  directoryFile_ = directoryFile;
  dataBase_ = treeEnd;
  return true;
}

bool VpkArchive::contains(const std::string& path) const {
  return entries_.find(normalize(path)) != entries_.end();
}

std::vector<std::string> VpkArchive::list(const std::string& prefix,
  const std::string& extension) const {
  const std::string normalizedPrefix = normalize(prefix);
  std::string normalizedExtension = normalize(extension);
  if (!normalizedExtension.empty() && normalizedExtension.front() != '.') normalizedExtension.insert(normalizedExtension.begin(), '.');
  std::vector<std::string> result;
  result.reserve(entries_.size());
  for (const auto& [path, entry] : entries_) {
    (void)entry;
    if (!normalizedPrefix.empty()) {
      const bool exact = path == normalizedPrefix;
      const bool child = path.size() > normalizedPrefix.size()
        && path.compare(0, normalizedPrefix.size(), normalizedPrefix) == 0
        && path[normalizedPrefix.size()] == '/';
      if (!exact && !child) continue;
    }
    if (!normalizedExtension.empty()) {
      if (path.size() < normalizedExtension.size()
          || path.compare(path.size() - normalizedExtension.size(),
            normalizedExtension.size(), normalizedExtension) != 0) continue;
    }
    result.push_back(path);
  }
  std::sort(result.begin(), result.end());
  return result;
}

std::vector<std::uint8_t> VpkArchive::read(const std::string& path, std::string* error) const {
  const auto found = entries_.find(normalize(path));
  if (found == entries_.end()) return {};
  const Entry& entry = found->second;
  std::vector<std::uint8_t> result = entry.preload;
  if (entry.length == 0) return result;
  std::ifstream file(entry.archiveFile, std::ios::binary);
  if (!file) {
    if (error) *error = "missing VPK data archive";
    return {};
  }
  file.seekg(static_cast<std::streamoff>(entry.offset));
  std::vector<std::uint8_t> tail(entry.length);
  if (!file.read(reinterpret_cast<char*>(tail.data()), static_cast<std::streamsize>(tail.size()))) {
    if (error) *error = "truncated VPK data entry";
    return {};
  }
  result.insert(result.end(), tail.begin(), tail.end());
  return result;
}

} // namespace tf2::native
