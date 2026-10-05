#include "asset_root.h"

#include <algorithm>
#include <cwchar>
#include <cwctype>
#include <fstream>
#include <regex>
#include <sstream>
#include <string_view>
#include <unordered_set>

namespace tf2::native {

namespace {
bool hasVpkArchive(const std::filesystem::path& root, const wchar_t* directory) {
  std::error_code error;
  if (!std::filesystem::is_directory(root, error) || error) return false;
  for (const auto& entry : std::filesystem::directory_iterator(root, error)) {
    if (error) return false;
    if (!entry.is_regular_file(error) || error) continue;
    auto name = entry.path().filename().wstring();
    std::transform(name.begin(), name.end(), name.begin(), [](wchar_t character) {
      return static_cast<wchar_t>(std::towlower(character));
    });
    constexpr std::wstring_view suffix = L"_dir.vpk";
    if (name.size() <= suffix.size()
        || name.compare(name.size() - suffix.size(), suffix.size(), suffix) != 0) continue;
    const std::wstring_view stem(name.data(), name.size() - suffix.size());
    const auto startsWith = [&](std::wstring_view prefix) {
      return stem.size() >= prefix.size() && stem.compare(0, prefix.size(), prefix) == 0;
    };
    if (std::wcscmp(directory, L"materials") == 0
        && (startsWith(L"pak") || startsWith(L"tf2_misc")
          || startsWith(L"tf2_textures"))) return true;
    if (std::wcscmp(directory, L"models") == 0
        && (startsWith(L"pak") || startsWith(L"tf2_misc"))) return true;
    if (std::wcscmp(directory, L"maps") == 0 && startsWith(L"pak")) return true;
  }
  return false;
}

bool hasAssetStore(const std::filesystem::path& root, const wchar_t* directory) {
  std::error_code error;
  if (std::filesystem::is_directory(root / directory, error) && !error) return true;
  return hasVpkArchive(root, directory);
}

std::string unescapeVdfPath(std::string value) {
  std::string result;
  result.reserve(value.size());
  bool escaped = false;
  for (const char character : value) {
    if (escaped) {
      result.push_back(character);
      escaped = false;
    } else if (character == '\\') {
      escaped = true;
    } else {
      result.push_back(character);
    }
  }
  if (escaped) result.push_back('\\');
  return result;
}
}

AssetRoot AssetRoot::fromPath(const std::filesystem::path& path) {
  AssetRoot root{};
  std::error_code error;
  const auto canonical = std::filesystem::weakly_canonical(path, error);
  root.tfDirectory = error ? std::filesystem::path{} : canonical;
  return root;
}

AssetRoot AssetRoot::findInstalled(const std::vector<std::filesystem::path>& candidates) {
  for (const auto& candidate : candidates) {
    AssetRoot root = fromPath(candidate);
    if (root.valid()) return root;
  }
  return {};
}

std::vector<std::filesystem::path> AssetRoot::steamLibraryCandidates(
    const std::vector<std::filesystem::path>& steamRoots) {
  std::vector<std::filesystem::path> candidates;
  std::unordered_set<std::wstring> seen;
  const std::regex pathLine(R"VDF("path"\s+"([^"]+)")VDF", std::regex::icase);
  for (const auto& steamRoot : steamRoots) {
    const auto add = [&](const std::filesystem::path& path) {
      const auto key = path.lexically_normal().wstring();
      if (seen.insert(key).second) candidates.push_back(path);
    };
    add(steamRoot / "steamapps/common/Team Fortress 2/tf");
    std::ifstream file(steamRoot / "steamapps/libraryfolders.vdf");
    if (!file) continue;
    std::string line;
    while (std::getline(file, line)) {
      std::smatch match;
      if (!std::regex_search(line, match, pathLine) || match.size() < 2) continue;
      std::filesystem::path library(unescapeVdfPath(match[1].str()));
      add(library / "steamapps/common/Team Fortress 2/tf");
    }
  }
  return candidates;
}

bool AssetRoot::valid() const {
  std::error_code error;
  if (!std::filesystem::is_directory(tfDirectory, error) || error) return false;
  return hasAssetStore(tfDirectory, L"materials")
    && hasAssetStore(tfDirectory, L"models")
    && hasAssetStore(tfDirectory, L"maps");
}

std::filesystem::path AssetRoot::resolve(const std::filesystem::path& relative) const {
  if (relative.empty() || relative.is_absolute() || tfDirectory.empty()) return {};
  std::error_code error;
  const auto root = std::filesystem::weakly_canonical(tfDirectory, error);
  if (error) return {};
  const auto candidate = std::filesystem::weakly_canonical(root / relative, error);
  if (error) return {};
  const auto within = candidate.lexically_relative(root);
  if (within.empty() || within.is_absolute() || within == std::filesystem::path("..")) return {};
  const auto first = *within.begin();
  if (first == std::filesystem::path("..")) return {};
  return candidate;
}

std::string AssetRoot::describe() const {
  std::ostringstream result;
  result << tfDirectory.string() << (valid()
    ? " (valid TF2 tf directory; loose files or VPK archives)"
    : " (missing materials/models/maps and VPK archives)");
  return result.str();
}

} // namespace tf2::native
