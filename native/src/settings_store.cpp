#include "settings_store.h"

#include <Windows.h>
#include <shlobj.h>
#include <cstdlib>
#include <iterator>

namespace tf2::native {

namespace {
constexpr wchar_t kSection[] = L"renderer";
constexpr wchar_t kSettingsFile[] = L"settings.ini";

int readInt(const std::filesystem::path& file, const wchar_t* key, int fallback) {
  return GetPrivateProfileIntW(kSection, key, fallback, file.c_str());
}

bool readBool(const std::filesystem::path& file, const wchar_t* key, bool fallback) {
  return readInt(file, key, fallback ? 1 : 0) != 0;
}

bool writeInt(const std::filesystem::path& file, const wchar_t* key, int value) {
  wchar_t buffer[32]{};
  _snwprintf_s(buffer, _TRUNCATE, L"%d", value);
  return WritePrivateProfileStringW(kSection, key, buffer, file.c_str()) != FALSE;
}

bool writeBool(const std::filesystem::path& file, const wchar_t* key, bool value) {
  return writeInt(file, key, value ? 1 : 0);
}
}

std::filesystem::path settingsPath() {
  PWSTR appData = nullptr;
  std::filesystem::path base;
  if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &appData))) {
    base = appData;
    CoTaskMemFree(appData);
  } else {
    wchar_t* fallback = nullptr;
    size_t length = 0;
    if (_wdupenv_s(&fallback, &length, L"LOCALAPPDATA") == 0 && fallback) {
      base = fallback;
      free(fallback);
    }
  }
  if (base.empty()) return {};
  return base / L"TF2 Demo Player" / kSettingsFile;
}

PersistentSettings loadSettings() {
  PersistentSettings result{};
  result.render = RenderSettings::fromPreset(QualityPreset::Performance);
  const auto file = settingsPath();
  if (file.empty()) return result;
  result.render.preset = readInt(file, L"preset", 0) == 1
    ? QualityPreset::Standard : QualityPreset::Performance;
  result.render.renderScale = static_cast<float>(readInt(file, L"renderScalePercent", 100)) / 100.0f;
  result.render.viewModelFov = static_cast<float>(readInt(file, L"viewModelFov", 80));
  result.render.vsync = readBool(file, L"vsync", false);
  result.render.normalize();
  wchar_t root[32768]{};
  GetPrivateProfileStringW(kSection, L"tfRoot", L"", root,
    static_cast<DWORD>(std::size(root)), file.c_str());
  if (root[0] != L'\0') {
    std::error_code error;
    const auto canonical = std::filesystem::weakly_canonical(std::filesystem::path(root), error);
    if (!error && !canonical.empty()) result.tfRoot = canonical;
  }
  return result;
}

bool saveSettings(const PersistentSettings& settings) {
  const auto file = settingsPath();
  if (file.empty()) return false;
  const auto temporary = file.wstring() + L".tmp";
  std::error_code error;
  std::filesystem::create_directories(file.parent_path(), error);
  if (error) return false;
  RenderSettings render = settings.render;
  render.normalize();
  DeleteFileW(temporary.c_str());
  if (!writeInt(temporary, L"preset", render.preset == QualityPreset::Standard ? 1 : 0)
      || !writeInt(temporary, L"renderScalePercent", static_cast<int>(render.renderScale * 100.0f + 0.5f))
      || !writeInt(temporary, L"viewModelFov", static_cast<int>(render.viewModelFov + 0.5f))
      || !writeBool(temporary, L"vsync", render.vsync)) {
    DeleteFileW(temporary.c_str());
    return false;
  }
  const auto root = settings.tfRoot.lexically_normal();
  if (!WritePrivateProfileStringW(kSection, L"tfRoot", root.c_str(), temporary.c_str())) {
    DeleteFileW(temporary.c_str());
    return false;
  }
  WritePrivateProfileStringW(nullptr, nullptr, nullptr, temporary.c_str());
  if (!MoveFileExW(temporary.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    DeleteFileW(temporary.c_str());
    return false;
  }
  return true;
}

} // namespace tf2::native
