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
  // Subtraction rather than addition: `offset + sizeof(T)` wraps for an offset
  // near SIZE_MAX, and a wrapped bound passes the comparison it was written to
  // fail. This is the only guard between a directory-tree field and a read.
  if (offset > bytes.size() || sizeof(T) > bytes.size() - offset) return false;
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
  // Four distinct header outcomes, because they ask for four different fixes and
  // the old single reason covered all of them:
  //   empty file        -> the path is wrong or a fixture was never written
  //   truncated header  -> fewer than 12 bytes, cannot even hold a v1 header
  //   unsupported       -> present and long enough, but not a VPK we read
  //   empty tree        -> a valid header that declares no entries at all
  if (bytes.empty()) {
    if (error) *error = "empty VPK file";
    return false;
  }
  if (!readValue(bytes, 0, signature) || !readValue(bytes, 4, version)) {
    if (error) *error = "truncated VPK header";
    return false;
  }
  if (signature != kSignature || (version != 1 && version != 2)) {
    if (error) *error = "unsupported VPK header";
    return false;
  }
  const std::size_t headerSize = version == 2 ? 28u : 12u;
  if (headerSize > bytes.size() || !readValue(bytes, 8, treeSize)) {
    if (error) *error = "truncated VPK header";
    return false;
  }
  if (treeSize == 0) {
    if (error) *error = "empty VPK directory tree";
    return false;
  }
  if (headerSize + static_cast<std::size_t>(treeSize) > bytes.size()) {
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
  bool sawExtensionTerminator = false;
  auto baseName = directoryFile.filename().wstring();
  constexpr std::wstring_view directorySuffix = L"_dir.vpk";
  if (baseName.size() >= directorySuffix.size()
      && baseName.compare(baseName.size() - directorySuffix.size(),
        directorySuffix.size(), directorySuffix) == 0) {
    baseName.resize(baseName.size() - directorySuffix.size());
  }
  while (cursor < treeEnd) {
    std::string extension;
    if (!readString(extension)) {
      if (error) *error = "truncated VPK extension table";
      entries_.clear();
      return false;
    }
    if (extension.empty()) { sawExtensionTerminator = true; break; }
    while (cursor < treeEnd) {
      std::string path;
      if (!readString(path)) {
        if (error) *error = "truncated VPK path table";
        entries_.clear();
        return false;
      }
      if (path.empty()) break;
      while (cursor < treeEnd) {
        std::string filename;
        if (!readString(filename)) {
          if (error) *error = "truncated VPK filename table";
          entries_.clear();
          return false;
        }
        if (filename.empty()) break;
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
        if (!readValue(bytes, cursor, preloadLength)) { if (error) *error = "truncated VPK preload length"; entries_.clear(); return false; } cursor += 2;
        if (!readValue(bytes, cursor, archiveIndex)) { if (error) *error = "truncated VPK archive index"; entries_.clear(); return false; } cursor += 2;
        if (!readValue(bytes, cursor, offset)) { if (error) *error = "truncated VPK entry offset"; entries_.clear(); return false; } cursor += 4;
        if (!readValue(bytes, cursor, length)) { if (error) *error = "truncated VPK entry length"; entries_.clear(); return false; } cursor += 4;
        std::uint16_t terminator = 0;
        if (!readValue(bytes, cursor, terminator)) { if (error) *error = "truncated VPK entry terminator"; entries_.clear(); return false; }
        if (terminator != 0xffff) { if (error) *error = "invalid VPK entry terminator"; entries_.clear(); return false; }
        cursor += 2;
        if (cursor + preloadLength > treeEnd) { if (error) *error = "truncated VPK preload data"; entries_.clear(); return false; }
        Entry entry{};
        entry.length = length;
        entry.preload.assign(bytes.begin() + static_cast<std::ptrdiff_t>(cursor),
          bytes.begin() + static_cast<std::ptrdiff_t>(cursor + preloadLength));
        cursor += preloadLength;
        if (archiveIndex == kDirectoryArchive) {
          entry.archiveFile = directoryFile;
          entry.inlineData = true;
          // The old code stored `treeEnd + offset` in a uint32 and wrapped when
          // the sum passed 4 GiB, so a tree could name an offset that reads from
          // somewhere else entirely and still look like a success. Widened, then
          // checked against the bytes we actually hold: an inline entry that
          // points outside its own directory file is a self-contradicting tree.
          const std::uint64_t absolute = static_cast<std::uint64_t>(treeEnd) + offset;
          if (length != 0 && (absolute > bytes.size()
              || length > bytes.size() - static_cast<std::size_t>(absolute))) {
            if (error) *error = "VPK entry range outside directory archive";
            entries_.clear();
            return false;
          }
          entry.offset = absolute;
        } else {
          entry.archiveFile = directoryFile.parent_path()
            / (baseName + L"_" + (archiveIndex < 10 ? L"00" : archiveIndex < 100 ? L"0" : L"")
              + std::to_wstring(archiveIndex) + L".vpk");
          entry.offset = static_cast<std::uint64_t>(offset);
        }
        const auto key = normalize(path == " " ? "" : path + "/")
          + normalize(filename) + (extension == " " ? "" : "." + normalize(extension));
        if (validPath(key)) entries_.emplace(key, std::move(entry));
      }
    }
  }
  if (!sawExtensionTerminator) {
    if (error) *error = "VPK directory tree has no terminal extension";
    entries_.clear();
    return false;
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

VpkArchive::ReadResult VpkArchive::readEntry(const std::string& path) const {
  ReadResult result;
  const auto found = entries_.find(normalize(path));
  // A name that is not in the tree is not a refusal -- it is the normal answer
  // for a lookup that misses, and it must stay distinguishable from "the entry
  // is here but its bytes are not readable".
  if (found == entries_.end()) return result;
  result.found = true;
  const Entry& entry = found->second;
  result.bytes = entry.preload;
  result.store = entry.inlineData ? Store::DirectoryData : Store::NumberedVolume;
  if (entry.length == 0) {
    result.store = Store::PreloadOnly;
    return result;
  }
  std::ifstream file(entry.archiveFile, std::ios::binary);
  if (!file) {
    result.error = "missing VPK data archive";
    return result;
  }
  // Bounded read. The tree decides both offset and length, so both are checked
  // against the data file's real size *before* a buffer of `length` bytes is
  // allocated: a 0xffffffff length would otherwise be a 4 GiB allocation, and an
  // offset past EOF used to be reported as "truncated" when the accurate name is
  // "out of range". Skipping this check is how a malformed archive turns into an
  // out-of-range read that still returns bytes.
  file.seekg(0, std::ios::end);
  const auto fileSize = file.tellg();
  if (fileSize < 0) {
    result.error = "cannot size VPK data archive";
    return result;
  }
  const std::uint64_t available = static_cast<std::uint64_t>(fileSize);
  if (entry.offset > available || entry.length > available - entry.offset) {
    result.error = "VPK entry range outside data archive";
    return result;
  }
  file.seekg(static_cast<std::streamoff>(entry.offset));
  std::vector<std::uint8_t> tail(entry.length);
  if (!file.read(reinterpret_cast<char*>(tail.data()), static_cast<std::streamsize>(tail.size()))) {
    result.error = "truncated VPK data entry";
    return result;
  }
  result.bytes.insert(result.bytes.end(), tail.begin(), tail.end());
  return result;
}

std::vector<std::uint8_t> VpkArchive::read(const std::string& path, std::string* error) const {
  ReadResult result = readEntry(path);
  if (error) *error = result.error;
  return std::move(result.bytes);
}

VpkArchiveSet VpkArchiveSet::openDirectory(const std::filesystem::path& tfDirectory) {
  VpkArchiveSet set;
  std::error_code error;
  for (const auto& entry : std::filesystem::directory_iterator(tfDirectory, error)) {
    if (error) break;
    if (!entry.is_regular_file(error) || error) continue;
    const auto name = entry.path().filename().string();
    if (name.size() < 8 || name.substr(name.size() - 8) != "_dir.vpk") continue;
    Held held;
    held.path = entry.path();
    if (!held.archive.open(held.path)) continue;
    set.archives_.push_back(std::move(held));
  }
  return set;
}

void VpkArchiveSet::collectContaining(const std::string& normalizedPath,
    std::vector<std::filesystem::path>& out) const {
  for (const auto& held : archives_) {
    if (held.archive.contains(normalizedPath)) out.push_back(held.path);
  }
}

const VpkArchive* VpkArchiveSet::find(const std::filesystem::path& archivePath) const {
  for (const auto& held : archives_) {
    if (held.path == archivePath) return &held.archive;
  }
  return nullptr;
}

std::size_t VpkArchiveSet::totalEntryCount() const {
  std::size_t total = 0;
  for (const auto& held : archives_) total += held.archive.entryCount();
  return total;
}

} // namespace tf2::native
