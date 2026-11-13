#include "asset_lookup.h"

#include <fstream>
#include <string_view>
#include <system_error>
#include <utility>

namespace tf2::native {

const char* assetStoreName(AssetStore store) {
  switch (store) {
    case AssetStore::Loose: return "loose";
    case AssetStore::VpkDirectory: return "vpk-directory";
    case AssetStore::VpkVolume: return "vpk-volume";
    case AssetStore::Pakfile: return "pakfile";
    case AssetStore::None: break;
  }
  return "none";
}

namespace {
std::vector<std::uint8_t> readWholeFile(const std::filesystem::path& path, std::string& error) {
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file) {
    error = "cannot read loose asset";
    return {};
  }
  const auto size = file.tellg();
  if (size < 0) {
    error = "cannot size loose asset";
    return {};
  }
  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
  file.seekg(0);
  if (!bytes.empty()
      && !file.read(reinterpret_cast<char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size()))) {
    error = "cannot read loose asset";
    return {};
  }
  return bytes;
}
} // namespace

AssetStoreChain::AssetStoreChain(std::filesystem::path looseRoot)
  : looseRoot_(std::move(looseRoot)) {}

void AssetStoreChain::addArchive(std::shared_ptr<VpkArchive> archive,
    std::filesystem::path directoryFile) {
  if (!archive) return;
  archives_.push_back(std::move(archive));
  archivePaths_.push_back(std::move(directoryFile));
}

void AssetStoreChain::setPakfileReader(PakfileReader reader) {
  pakfile_ = std::move(reader);
}

AssetLookup AssetStoreChain::lookup(const std::string& path) const {
  AssetLookup result;
  if (path.empty()) {
    result.error = "empty asset path";
    return result;
  }

  if (!looseRoot_.empty()) {
    std::error_code error;
    const auto candidate = looseRoot_ / std::filesystem::path(path);
    if (std::filesystem::is_regular_file(candidate, error) && !error) {
      std::string looseError;
      auto bytes = readWholeFile(candidate, looseError);
      if (!looseError.empty()) {
        result.error = looseError;
        result.origin = "loose";
      } else if (!bytes.empty()) {
        result.bytes = std::move(bytes);
        result.store = AssetStore::Loose;
        result.origin = "loose";
        return result;
      }
    }
  }

  std::string emptyOrigin;
  for (std::size_t i = 0; i < archives_.size(); ++i) {
    const auto& archive = archives_[i];
    if (!archive) continue;
    const auto entry = archive->readEntry(path);
    if (!entry.found) continue;
    const auto origin = archivePaths_[i].filename().string();
    if (!entry.ok()) {
      // Remember the last refusal, but keep looking: another archive may hold a
      // readable copy, and a refusal from one store is not a verdict on the
      // lookup. Only if nothing answers does this become the reported reason.
      result.error = entry.error;
      result.origin = origin;
      continue;
    }
    if (entry.bytes.empty()) {
      emptyOrigin = origin;
      continue;
    }
    result.bytes = entry.bytes;
    // Preload bytes live in the directory file even when the entry names a
    // volume, so a preload-only read is a directory read.
    result.store = entry.store == VpkArchive::Store::NumberedVolume
      ? AssetStore::VpkVolume : AssetStore::VpkDirectory;
    result.origin = origin;
    result.error.clear();
    return result;
  }

  if (pakfile_) {
    std::vector<std::uint8_t> bytes;
    std::string reason;
    if (pakfile_(path, bytes, reason) && !bytes.empty()) {
      result.bytes = std::move(bytes);
      result.store = AssetStore::Pakfile;
      result.origin = "pakfile";
      result.error.clear();
      return result;
    }
    if (result.error.empty() && !reason.empty()) {
      result.error = reason;
      if (result.origin.empty()) result.origin = "pakfile";
    }
  }

  if (result.error.empty() && !emptyOrigin.empty()) {
    result.error = "empty asset in " + emptyOrigin;
    result.origin = emptyOrigin;
  }
  return result;
}

} // namespace tf2::native
