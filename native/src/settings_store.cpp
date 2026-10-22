#include "settings_store.h"

#include "viewmodel.h"

#include <Windows.h>
#include <shlobj.h>
#include <cstdlib>
#include <iterator>

namespace tf2::native {

namespace {
constexpr wchar_t kSection[] = L"renderer";
constexpr wchar_t kSettingsFile[] = L"settings.ini";
// Empty means "the user's real settings file". Set by setSettingsFileOverride.
std::filesystem::path g_settingsFileOverride;

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
  if (!g_settingsFileOverride.empty()) return g_settingsFileOverride;
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
  const auto file = settingsPath();
  if (file.empty()) return result;
  const bool standardQuality = readInt(file, L"preset", 0) == 1;
  result.render.preset = standardQuality
    ? QualityPreset::Standard : QualityPreset::Performance;
  // T17: `preset` is a label the rest of the program reads -- the env pass asks
  // `preset == Standard` before it samples a cube -- so a settings file that
  // says standard while every flag holds Performance's value is a contradiction
  // the frame inherits. The trace printed both halves at once: `cubemap ...
  // preset=standard` next to `sky-upload state=unavailable reason=skybox draw
  // disabled by the caller`, and 20.2% of the reference frame was clear colour
  // because the sky pass never ran.
  //
  // The sky is taken from the preset because it decides what geometry the world
  // pass draws. The other Standard flags (dynamic lighting, specular, bump,
  // shadows, water reflection, mip bias, anisotropy, model lod) stay exactly
  // where this reader left them: each of those changes the SHADING, and a
  // shading change has to be measured before it is claimed. Enabling them here
  // would have moved every region pin in this task's own evidence for a reason
  // the task did not measure, which is the opposite of what T17 asks for. That
  // half of the contradiction is written down in
  // T17-WORLD-VISUAL-2026-10-19.md §6 and belongs to the render-regression
  // round.
  result.render.skybox = standardQuality;
  result.render.renderScale = static_cast<float>(readInt(file, L"renderScalePercent", 100)) / 100.0f;
  result.render.viewModelFov = static_cast<float>(
    migrateLegacyViewModelFov(readInt(file, L"viewModelFov", kDefaultViewModelFovInt)));
  result.render.vsync = readBool(file, L"vsync", false);
  // T52: the overlay's two keys. They are read here, with everything else, so a
  // gate that pins its own settings file makes the COMPILED default the input --
  // "no stored value" is then reachable from a gate instead of being whatever
  // this machine happens to have saved.
  result.render.outlineEnabled = readBool(file, L"outline", false);

  result.render.outlineDepthTested = readInt(file, L"outline_depth", 0) == 1;

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
      || !writeBool(temporary, L"vsync", render.vsync)
      // T52: the overlay's two keys, written here so there is ONE ini writer. A
      // second writer racing this one is how a settings file ends up with half of
      // one round's keys.
      || !writeBool(temporary, L"outline", render.outlineEnabled)
      || !writeInt(temporary, L"outline_depth", render.outlineDepthTested ? 1 : 0)) {
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

void setSettingsFileOverride(const std::filesystem::path& file) {
  g_settingsFileOverride = file;
}

} // namespace tf2::native
