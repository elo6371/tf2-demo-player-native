#include "hud_state.h"

#include "ini_section.h"

#include <algorithm>
#include <cwchar>

namespace tf2::native {
namespace {

constexpr wchar_t kSection[] = L"hud";

// Every key this feature owns, spelled once. The settings writer enumerates this
// list rather than writing keys ad hoc, so "the feature writes only hud.*" is a
// property of the code rather than a promise in a comment.
constexpr const wchar_t* kEnabledKey = L"hud.enabled";
constexpr const wchar_t* kRootKey = L"hud.root";
constexpr const wchar_t* kLayoutKey = L"hud.layout";
constexpr const wchar_t* kScaleKey = L"hud.scale";
constexpr const wchar_t* kPreviewKey = L"hud.preview";

int parseIntValue(const std::wstring& text, int fallback) {
  if (text.empty()) return fallback;
  wchar_t* end = nullptr;
  const long parsed = std::wcstol(text.c_str(), &end, 10);
  if (!end || *end != L'\0') return fallback;
  return static_cast<int>(parsed);
}

bool parseBoolValue(const std::wstring& text, bool fallback) {
  const int value = parseIntValue(text, fallback ? 1 : 0);
  return value != 0;
}

} // namespace

const char* hudApplyResultName(HudApplyResult result) {
  switch (result) {
    case HudApplyResult::Applied: return "applied";
    case HudApplyResult::Refused: return "refused";
    case HudApplyResult::ResetToDefault: return "reset";
    case HudApplyResult::NothingToDo: return "nothing-to-do";
    case HudApplyResult::SettingsWriteFailed: return "settings-write-failed";
  }
  return "unknown";
}

bool HudState::loadFrom(const std::filesystem::path& file) {
  settings_ = HudSettings{};
  repaired_ = false;
  repairDetail_.clear();
  if (file.empty()) return false;
  IniSection section;
  if (!readIniSection(file, kSection, section)) return false;
  const std::wstring empty;
  const std::wstring enabled = iniSectionValue(section, kEnabledKey, empty);
  const std::wstring root = iniSectionValue(section, kRootKey, empty);
  const std::wstring layout = iniSectionValue(section, kLayoutKey, empty);
  const std::wstring scale = iniSectionValue(section, kScaleKey, empty);
  const std::wstring preview = iniSectionValue(section, kPreviewKey, empty);
  settings_.enabled = parseBoolValue(enabled, false);
  settings_.preview = parseBoolValue(preview, true);
  std::string narrow;
  if (wideToUtf8(root, narrow)) settings_.root = narrow;
  narrow.clear();
  if (wideToUtf8(layout, narrow)) settings_.layout = narrow;
  const int rawScale = parseIntValue(scale, 100);
  if (!hudScaleInRange(rawScale)) {
    // A scale outside the contract window is repaired, and the repair is
    // reported. Silently rendering at a scale nobody chose is the failure mode
    // that would make the preview disagree with the settings file on disk.
    settings_.scalePercent = clampHudScale(rawScale);
    repaired_ = true;
    repairDetail_ = "hud.scale=" + std::to_string(rawScale) + " clamped to "
      + std::to_string(settings_.scalePercent);
  } else {
    settings_.scalePercent = rawScale;
  }
  return true;
}

bool HudState::saveTo(const std::filesystem::path& file) const {
  if (file.empty()) return false;
  IniSection section;
  section.reserve(5);
  std::wstring wide;
  // Written in a fixed order rather than map order: two saves of the same state
  // must be byte-identical, and a gate diffs the file.
  section.emplace_back(kEnabledKey, settings_.enabled ? L"1" : L"0");
  if (!utf8ToWide(settings_.root, wide)) return false;
  section.emplace_back(kRootKey, wide);
  if (!utf8ToWide(settings_.layout, wide)) return false;
  section.emplace_back(kLayoutKey, wide);
  section.emplace_back(kScaleKey, std::to_wstring(settings_.scalePercent));
  section.emplace_back(kPreviewKey, settings_.preview ? L"1" : L"0");
  return writeIniSection(file, kSection, section);
}

HudApplyResult HudState::import(const HudImportRequest& request, int screenWidth,
    int screenHeight, float dpiScale) {
  lastRefusal_ = HudRefusal::None;
  lastRefusalDetail_.clear();
  lastApply_ = HudApplyResult::Refused;   // overwritten below only on success
  // The root itself is the user boundary: it may be absolute (it is what the
  // folder picker returned), but it must exist and be a directory. The LAYOUT
  // is the untrusted part and goes through the full relative-path rule.
  std::error_code error;
  if (request.root.empty()) {
    lastRefusal_ = HudRefusal::EmptyPath;
    lastRefusalDetail_ = "no folder was chosen";
    ++refusedImports_;
    return HudApplyResult::Refused;
  }
  if (!std::filesystem::is_directory(request.root, error)) {
    lastRefusal_ = error ? HudRefusal::IoError : HudRefusal::Missing;
    lastRefusalDetail_ = "root=" + request.root.u8string();
    ++refusedImports_;
    return HudApplyResult::Refused;
  }
  const auto rootCanonical = std::filesystem::weakly_canonical(request.root, error);
  if (error) {
    lastRefusal_ = HudRefusal::IoError;
    lastRefusalDetail_ = "root=" + request.root.u8string();
    ++refusedImports_;
    return HudApplyResult::Refused;
  }
  const auto read = readHudFile(rootCanonical, request.layout);
  if (!read.ok) {
    lastRefusal_ = read.reason;
    lastRefusalDetail_ = "layout=" + request.layout + " reason=" + hudRefusalName(read.reason);
    ++refusedImports_;
    return HudApplyResult::Refused;
  }
  const auto parsed = parseHudLayout(read.text);
  if (!parsed.ok) {
    lastRefusal_ = parsed.reason;
    lastRefusalDetail_ = parsed.detail.empty()
      ? std::string("layout=") + request.layout + " reason=" + hudRefusalName(parsed.reason)
      : parsed.detail;
    ++refusedImports_;
    return HudApplyResult::Refused;
  }
  // Only now is anything committed: a refused import leaves the previous state
  // exactly as it was, so a failed attempt cannot half-apply a HUD.
  const auto safe = sanitizeHudRelativePath(request.layout);
  settings_.root = rootCanonical.u8string();
  settings_.layout = safe.ok ? safe.text : request.layout;
  settings_.enabled = true;
  layout_ = parsed;
  imported_ = true;
  lastApply_ = HudApplyResult::Applied;
  ++acceptedImports_;
  layoutFor(screenWidth, screenHeight, dpiScale);
  return HudApplyResult::Applied;
}

void HudState::layoutFor(int screenWidth, int screenHeight, float dpiScale) {
  lastWidth_ = screenWidth;
  lastHeight_ = screenHeight;
  lastDpi_ = dpiScale;
  rects_.clear();
  if (!imported_) return;
  rects_ = resolveHudRects(layout_, screenWidth, screenHeight, dpiScale,
    settings_.scalePercent);
}

HudApplyResult HudState::resetToDefault() {
  // Note what is NOT here: no filesystem call of any kind. The imported root is
  // the user's directory, and resetting a preference is not a licence to touch
  // it. The gate asserts this with fixture files whose bytes are compared
  // before and after.
  settings_ = HudSettings{};
  layout_ = HudLayout{};
  rects_.clear();
  imported_ = false;
  lastRefusal_ = HudRefusal::None;
  lastRefusalDetail_.clear();
  repaired_ = false;
  repairDetail_.clear();
  return HudApplyResult::ResetToDefault;
}

HudApplyResult HudState::applyOverrides(const HudOverrides& overrides, int screenWidth,
    int screenHeight, float dpiScale) {
  // WHY THIS ORDER, and why it is here rather than in main.cpp:
  //
  //   import, then reset, then scale, then enabled.
  //
  // Each step is more specific than the one before, so "--hud-apply --hud-on
  // --hud-scale 150" means "import this document, then put it at 150%" instead of
  // "import it and drop the rest". Import must come before enabled for a second
  // reason too: a successful import sets enabled = true by itself, so a --hud-off
  // applied BEFORE the import would be silently overridden by the very import the
  // caller asked for. Writing the order in the state object rather than at the
  // call site is what keeps the picker, the CLI and the gate applying it the same.
  HudApplyResult last = HudApplyResult::NothingToDo;
  if (overrides.importRequested) {
    HudImportRequest request;
    request.root = overrides.root;
    request.layout = overrides.layout;
    last = import(request, screenWidth, screenHeight, dpiScale);
  }
  if (overrides.resetRequested) last = resetToDefault();
  if (overrides.scaleGiven) {
    setScalePercent(overrides.scalePercent);
    layoutFor(screenWidth, screenHeight, dpiScale);
    last = HudApplyResult::Applied;
  }
  if (overrides.enabledGiven) {
    setEnabled(overrides.enabled);
    last = HudApplyResult::Applied;
  }
  return last;
}

void HudState::noteDiscoveryRefusal(const std::string& reason) {
  lastRefusal_ = HudRefusal::Missing;
  lastRefusalDetail_ = reason;
  ++refusedImports_;
}

std::size_t HudState::resolvedPanels() const {
  std::size_t count = 0;
  for (const auto& rect : rects_) { if (rect.valid) ++count; }
  return count;
}

std::size_t HudState::visiblePanels() const {
  // "Visible" here means "will be drawn": a resolved rect the document hid is not
  // one of these, and neither is a hidden panel's child. The distinction is what
  // the gate's drawn-count assertion compares against, so the two cannot drift.
  std::size_t count = 0;
  for (const auto& rect : rects_) { if (rect.valid && rect.visible) ++count; }
  return count;
}

std::size_t HudState::invalidPanels() const {
  std::size_t count = 0;
  for (const auto& rect : rects_) { if (!rect.valid) ++count; }
  return count;
}

} // namespace tf2::native
