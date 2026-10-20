#include "install_manifest.h"

#include <Windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <fstream>
#include <sstream>

namespace tf2::native::installer {

namespace {
constexpr char kHex[] = "0123456789abcdef";

std::string lower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return value;
}

std::string trim(const std::string& value) {
  const auto first = value.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) return {};
  const auto last = value.find_last_not_of(" \t\r\n");
  return value.substr(first, last - first + 1);
}

// The manifest is ASCII by construction: every field is a path, a state word, a
// count or a hex digest. A field that is not ASCII would break the one-record-
// per-line format silently, so it is refused instead.
bool asciiOnly(const std::string& value) {
  for (const unsigned char c : value) { if (c < 0x20 || c > 0x7e) return false; }
  return true;
}

void hexEncode(const unsigned char* bytes, std::size_t count, std::string& out) {
  out.clear();
  out.reserve(count * 2);
  for (std::size_t i = 0; i < count; ++i) {
    out.push_back(kHex[bytes[i] >> 4]);
    out.push_back(kHex[bytes[i] & 0x0f]);
  }
}
}

std::string sha256Bytes(const std::string& bytes) {
  BCRYPT_ALG_HANDLE algorithm = nullptr;
  BCRYPT_HASH_HANDLE hash = nullptr;
  std::string result;
  DWORD objectSize = 0, dataSize = 0, produced = 0;
  std::vector<unsigned char> object, digest;
  if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) return {};
  if (BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
      reinterpret_cast<PUCHAR>(&objectSize), sizeof(objectSize), &produced, 0) < 0) {
    BCryptCloseAlgorithmProvider(algorithm, 0);
    return {};
  }
  object.resize(objectSize);
  if (BCryptCreateHash(algorithm, &hash, object.data(), objectSize, nullptr, 0, 0) < 0) {
    BCryptCloseAlgorithmProvider(algorithm, 0);
    return {};
  }
  const auto* data = reinterpret_cast<const unsigned char*>(bytes.data());
  const auto length = static_cast<ULONG>(bytes.size());
  if (BCryptHashData(hash, const_cast<unsigned char*>(data), length, 0) >= 0) {
    digest.resize(32);
    if (BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) >= 0) {
      std::string raw(reinterpret_cast<const char*>(digest.data()), digest.size());
      hexEncode(reinterpret_cast<const unsigned char*>(raw.data()), raw.size(), result);
    }
  }
  BCryptDestroyHash(hash);
  BCryptCloseAlgorithmProvider(algorithm, 0);
  return result;
}

std::string sha256File(const std::filesystem::path& file, std::string* error) {
  std::ifstream stream(file, std::ios::binary);
  if (!stream) {
    if (error) *error = "cannot open for hashing";
    return {};
  }
  BCRYPT_ALG_HANDLE algorithm = nullptr;
  BCRYPT_HASH_HANDLE hash = nullptr;
  std::string result;
  DWORD objectSize = 0, produced = 0;
  std::vector<unsigned char> object, digest;
  if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) {
    if (error) *error = "cannot open the hash provider";
    return {};
  }
  if (BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
      reinterpret_cast<PUCHAR>(&objectSize), sizeof(objectSize), &produced, 0) < 0) {
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (error) *error = "cannot size the hash object";
    return {};
  }
  object.resize(objectSize);
  if (BCryptCreateHash(algorithm, &hash, object.data(), objectSize, nullptr, 0, 0) < 0) {
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (error) *error = "cannot create the hash";
    return {};
  }
  // Streamed: the product is 1.8 MB today, but a manifest reader that loads the
  // whole payload would be the wrong thing to reach for on a 100 MB package.
  std::vector<char> chunk(1u << 16);
  bool ok = true;
  while (stream) {
    stream.read(chunk.data(), static_cast<std::streamsize>(chunk.size()));
    const auto got = stream.gcount();
    if (got <= 0) break;
    if (BCryptHashData(hash, reinterpret_cast<unsigned char*>(chunk.data()),
          static_cast<ULONG>(got), 0) < 0) { ok = false; break; }
  }
  if (ok) {
    digest.resize(32);
    if (BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) >= 0) {
      hexEncode(digest.data(), digest.size(), result);
    } else { ok = false; }
  }
  BCryptDestroyHash(hash);
  BCryptCloseAlgorithmProvider(algorithm, 0);
  if (!ok) {
    if (error) *error = "cannot hash the file";
    return {};
  }
  return result;
}

std::string relativeTo(const std::filesystem::path& root,
    const std::filesystem::path& path) {
  std::error_code error;
  const auto canonicalRoot = std::filesystem::weakly_canonical(root, error);
  if (error) return {};
  const auto canonicalPath = std::filesystem::weakly_canonical(path, error);
  if (error) return {};
  auto relative = canonicalPath.lexically_relative(canonicalRoot);
  if (relative.empty() || relative.is_absolute()) return {};
  if (*relative.begin() == std::filesystem::path("..")) return {};
  std::string text = relative.generic_string();
  return text;
}

std::vector<std::string> listFiles(const std::filesystem::path& root) {
  std::vector<std::string> files;
  std::error_code error;
  for (const auto& entry : std::filesystem::recursive_directory_iterator(root, error)) {
    if (error) break;
    if (!entry.is_regular_file(error) || error) continue;
    auto relative = relativeTo(root, entry.path());
    if (relative.empty()) continue;
    files.push_back(std::move(relative));
  }
  std::sort(files.begin(), files.end());
  return files;
}

bool lastWriteTime(const std::filesystem::path& file, std::uint64_t* value) {
  WIN32_FILE_ATTRIBUTE_DATA data{};
  if (!GetFileAttributesExW(file.c_str(), GetFileExInfoStandard, &data)) return false;
  *value = (static_cast<std::uint64_t>(data.ftLastWriteTime.dwHighDateTime) << 32)
    | data.ftLastWriteTime.dwLowDateTime;
  return true;
}

bool setLastWriteTime(const std::filesystem::path& file, std::uint64_t value) {
  const HANDLE handle = CreateFileW(file.c_str(), FILE_WRITE_ATTRIBUTES,
    FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
  if (handle == INVALID_HANDLE_VALUE) return false;
  FILETIME time{};
  time.dwLowDateTime = static_cast<DWORD>(value & 0xffffffffu);
  time.dwHighDateTime = static_cast<DWORD>(value >> 32);
  const bool ok = SetFileTime(handle, nullptr, nullptr, &time) != 0;
  CloseHandle(handle);
  return ok;
}

std::string serialize(const Manifest& manifest) {
  std::ostringstream out;
  out << "format=" << kManifestFormat << "\n";
  out << "formatVersion=" << kManifestVersion << "\n";
  out << "package=" << manifest.package << "\n";
  out << "packageVersion=" << manifest.packageVersion << "\n";
  out << "packageVersionCode=" << manifest.packageVersionCode << "\n";
  out << "sourceDateEpoch=" << manifest.sourceDateEpoch << "\n";
  out << "sourceTree=" << manifest.sourceTree << "\n";
  out << "installedUtc=" << manifest.installedUtc << "\n";
  out << "installDirectory=" << manifest.installDirectory << "\n";
  out << "files=" << manifest.files.size() << "\n";
  for (const auto& file : manifest.files) {
    out << "file=" << file.state << '\t' << file.bytes << '\t' << file.sha256
        << '\t' << file.path;
    if (!file.backup.empty()) out << '\t' << file.backup;
    out << "\n";
  }
  return out.str();
}

bool parse(const std::string& text, Manifest* manifest, std::string* error) {
  const auto bad = [&](const char* what) {
    if (error) *error = what;
    return false;
  };
  if (manifest == nullptr) return bad("no manifest destination");
  *manifest = Manifest{};
  std::istringstream stream(text);
  std::string line;
  bool sawFormat = false;
  while (std::getline(stream, line)) {
    line = trim(line);
    if (line.empty()) continue;
    if (line.rfind("file=", 0) == 0) {
      // state, bytes, sha256, path [, backup]: the path is last only when there
      // is no backup, so the split is done by field count rather than by a
      // guess about where the path could contain a tab (it cannot: paths are
      // validated to be ASCII without control characters).
      std::vector<std::string> fields;
      std::string rest = line.substr(5);
      std::size_t start = 0;
      while (true) {
        const auto tab = rest.find('\t', start);
        fields.push_back(rest.substr(start, tab == std::string::npos ? std::string::npos : tab - start));
        if (tab == std::string::npos) break;
        start = tab + 1;
      }
      if (fields.size() != 4 && fields.size() != 5) return bad("a file record has the wrong field count");
      ManifestFile file;
      file.state = fields[0];
      try { file.bytes = std::stoull(fields[1]); } catch (...) { return bad("a file record has no byte count"); }
      file.sha256 = fields[2];
      file.path = fields[3];
      if (fields.size() == 5) file.backup = fields[4];
      if (!asciiOnly(file.path)) return bad("a file record path is not printable ASCII");
      manifest->files.push_back(std::move(file));
      continue;
    }
    const auto equals = line.find('=');
    if (equals == std::string::npos) return bad("a manifest line is neither a key nor a file record");
    const std::string key = line.substr(0, equals);
    const std::string value = line.substr(equals + 1);
    if (key == "format") {
      if (value != kManifestFormat) return bad("this is not an install manifest");
      sawFormat = true;
    } else if (key == "formatVersion") {
      if (value != std::to_string(kManifestVersion)) return bad("unsupported manifest format version");
    } else if (key == "package") { manifest->package = value; }
    else if (key == "packageVersion") { manifest->packageVersion = value; }
    else if (key == "packageVersionCode") {
      try { manifest->packageVersionCode = std::stoi(value); } catch (...) { return bad("packageVersionCode is not a number"); }
    }
    else if (key == "sourceDateEpoch") { manifest->sourceDateEpoch = value; }
    else if (key == "sourceTree") { manifest->sourceTree = value; }
    else if (key == "installedUtc") { manifest->installedUtc = value; }
    else if (key == "installDirectory") { manifest->installDirectory = value; }
    else if (key == "files") { /* counted from the records, not trusted */ }
    else return bad("unknown manifest key");
  }
  if (!sawFormat) return bad("this is not an install manifest");
  return true;
}

bool writeManifest(const std::filesystem::path& file, const Manifest& manifest,
    std::string* error) {
  std::error_code code;
  std::filesystem::create_directories(file.parent_path(), code);
  std::ofstream stream(file, std::ios::binary | std::ios::trunc);
  if (!stream) {
    if (error) *error = "cannot create the manifest";
    return false;
  }
  const std::string text = serialize(manifest);
  stream.write(text.data(), static_cast<std::streamsize>(text.size()));
  if (!stream) {
    if (error) *error = "cannot write the manifest";
    return false;
  }
  return true;
}

bool readManifest(const std::filesystem::path& file, Manifest* manifest,
    std::string* error) {
  std::ifstream stream(file, std::ios::binary);
  if (!stream) {
    if (error) *error = "cannot open the manifest";
    return false;
  }
  std::ostringstream buffer;
  buffer << stream.rdbuf();
  return parse(buffer.str(), manifest, error);
}

} // namespace tf2::native::installer
