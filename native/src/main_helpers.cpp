// T76 stage 2 (2026-11-02): the file, path and byte helpers, moved verbatim out
// of main.cpp's anonymous namespace.
//
// Stage 1 of T76 moved the trace channel, the installer lifecycle, the HUD and
// the camera layer into their own translation units. The pieces below were the
// cheapest next slice: seven functions with no captured state, no globals of
// their own, and no calls into the rest of main.cpp. They are the ones
// runCameraScriptStep, the argument scan and the frame writers reach for when
// they need a file read, a hash, a model path or the local cache directory.
//
// Moved as motion, not as a rewrite: the bodies, comments and every string are
// the shipped bytes. main.cpp picks the declarations up from main_internal.h
// through the using-directive it already had, so no call site changed.
//
// The repository keeps this file out of the public contract: nothing here is
// exported, no gate parses it, and the command line and trace text it takes part
// in are unchanged.

#include "main_internal.h"

#include <Windows.h>
#include <bcrypt.h>
#include <shlobj.h>

#include <array>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "demo_header.h"

namespace tf2::native::main_internal {

// T54: argument text -> UTF-8. The logs panel's URL policy is defined in bytes
// (ASCII only, refusing everything else), so the conversion has to be exact and
// lossless-or-refused rather than "best effort": a URL that cannot survive the
// wide->narrow trip is refused by the policy instead of being mangled into
// something that passes the host allow-list.
std::string narrowAscii(const wchar_t* value) {
  if (value == nullptr) return {};
  std::string result;
  for (const wchar_t* cursor = value; *cursor != L'\0'; ++cursor) {
    if (*cursor < 0 || *cursor > 0x7F) {
      // Out of range on purpose: the policy refuses non-ASCII bytes, and this
      // is how they get to it instead of silently becoming '?'.
      result.push_back('\x7F');
      continue;
    }
    result.push_back(static_cast<char>(*cursor));
  }
  return result;
}

// Reduce a `--model` argument to the form the animation cache is keyed by.
// Two legitimate shapes, and picking the wrong one is the difference between
// "model-missing" and a pose:
//   * a path under the tf root, e.g. tf/download/models/buildables/x.mdl --
//     loose custom models live there and are NOT reachable as `models/...`
//   * a VPK path, e.g. models/weapons/w_models/w_pistol.mdl
// So an absolute path under the root becomes root-relative first, and only a
// path with no root prefix is reduced to its `models/` suffix. Anything that
// matches neither is passed through unchanged, so a wrong path stays wrong and
// surfaces as model-missing with the path it tried.
std::string normalizedModelPath(const std::string& rawPath, const std::filesystem::path& tfRoot) {
  std::string path = rawPath;
  for (char& character : path) {
    if (character == '\\') character = '/';
  }
  const auto lower = [](std::string value) {
    for (char& character : value) {
      character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    }
    return value;
  };
  const std::string root = lower(tfRoot.generic_string());
  const std::string lowered = lower(path);
  if (!root.empty() && lowered.size() > root.size() && lowered.compare(0, root.size(), root) == 0) {
    std::size_t start = root.size();
    while (start < path.size() && path[start] == '/') ++start;
    if (start < path.size()) return path.substr(start);
  }
  // The VPK-relative path starts at the *first* "models/", not the last.
  // `rfind` was latent-wrong for any path containing "v_models/", because that
  // directory name itself contains "models/": `models/weapons/v_models/
  // v_scattergun_scout.mdl` was truncated to `models/v_scattergun_scout.mdl` and
  // every ViewModel then read as "was not found". The two `--model` paths the T1
  // gate uses (`models/player/scout.mdl`, `models/props_ui/bannerflag_comp_l.mdl`)
  // contain the substring once, so `find` and `rfind` agree on them and that
  // gate's readings are unchanged.
  const std::size_t marker = lowered.find("models/");
  if (marker == std::string::npos) return path;
  return path.substr(marker);
}

const wchar_t* entitySnapshotStatusName(tf2::native::EntitySnapshotQueryStatus status) {
  switch (status) {
    case tf2::native::EntitySnapshotQueryStatus::Available: return L"available";
    case tf2::native::EntitySnapshotQueryStatus::Checkpoint: return L"checkpoint";
    case tf2::native::EntitySnapshotQueryStatus::NoHistory: return L"no-history";
    case tf2::native::EntitySnapshotQueryStatus::TickBeforeHistory: return L"before-window";
    case tf2::native::EntitySnapshotQueryStatus::Gap: return L"gap";
    case tf2::native::EntitySnapshotQueryStatus::DeltaBaseMissing: return L"delta-base-missing";
    default: return L"unknown";
  }
}

void addSteamRegistryRoots(std::vector<std::filesystem::path>& roots) {
  constexpr std::array<const wchar_t*, 2> keys = {
    L"Software\\Valve\\Steam",
    L"Software\\WOW6432Node\\Valve\\Steam",
  };
  constexpr std::array<const wchar_t*, 2> values = { L"SteamPath", L"InstallPath" };
  for (const auto* key : keys) {
    for (const auto* value : values) {
      for (const auto hive : { HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE }) {
        HKEY handle = nullptr;
        if (RegOpenKeyExW(hive, key, 0, KEY_READ, &handle) != ERROR_SUCCESS) continue;
        wchar_t buffer[MAX_PATH]{};
        DWORD bytes = sizeof(buffer);
        const LONG result = RegQueryValueExW(handle, value, nullptr, nullptr,
          reinterpret_cast<LPBYTE>(buffer), &bytes);
        RegCloseKey(handle);
        if (result == ERROR_SUCCESS && buffer[0] != L'\0') roots.emplace_back(buffer);
      }
    }
  }
}

std::vector<std::uint8_t> readFile(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file) return {};
  const auto size = file.tellg();
  if (size <= 0) return {};
  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
  file.seekg(0);
  if (!file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) return {};
  return bytes;
}

std::string sha256File(const std::filesystem::path& path, std::string& status) {
  std::ifstream file(path, std::ios::binary);
  if (!file) { status = "missing"; return {}; }
  BCRYPT_ALG_HANDLE algorithm = nullptr;
  BCRYPT_HASH_HANDLE hash = nullptr;
  DWORD objectLength = 0, resultLength = 0;
  if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0
      || BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objectLength), sizeof(objectLength), &resultLength, 0) != 0) {
    if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    status = "failed"; return {};
  }
  std::vector<std::uint8_t> object(objectLength);
  std::array<std::uint8_t, 32> digest{};
  std::string result;
  bool ok = BCryptCreateHash(algorithm, &hash, object.data(), objectLength, nullptr, 0, 0) == 0;
  // Keep the streaming buffer on the heap; the Windows default thread stack
  // is small enough that a 1 MiB local array can trigger STATUS_STACK_OVERFLOW.
  std::vector<std::uint8_t> chunk(1024u * 1024u);
  while (ok && file) {
    file.read(reinterpret_cast<char*>(chunk.data()), static_cast<std::streamsize>(chunk.size()));
    const auto count = file.gcount();
    if (count > 0) ok = BCryptHashData(hash, chunk.data(), static_cast<ULONG>(count), 0) == 0;
  }
  if (ok && BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) == 0) {
    static constexpr char hex[] = "0123456789abcdef";
    result.reserve(64);
    for (const auto byte : digest) { result.push_back(hex[byte >> 4u]); result.push_back(hex[byte & 15u]); }
    status = "ready";
  }
  if (hash) BCryptDestroyHash(hash);
  BCryptCloseAlgorithmProvider(algorithm, 0);
  if (result.empty()) status = "failed";
  return result;
}

std::filesystem::path localCacheDirectory(std::string& status) {
  wchar_t* value = nullptr; std::size_t length = 0;
  if (_wdupenv_s(&value, &length, L"LOCALAPPDATA") == 0 && value) {
    std::filesystem::path path = std::filesystem::path(value) / L"TF2 Demo Player" / L"cache";
    free(value);
    std::error_code error; std::filesystem::create_directories(path, error);
    if (error) { status = "unavailable"; return {}; }
    const auto probe = path / L".write-test";
    std::ofstream probeFile(probe, std::ios::binary | std::ios::trunc);
    if (!probeFile) { status = "unwritable"; return {}; }
    probeFile.close();
    std::filesystem::remove(probe, error);
    status = "ready";
    return path;
  }
  status = "unavailable";
  return {};
}

}  // namespace tf2::native::main_internal
