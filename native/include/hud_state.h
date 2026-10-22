#pragma once

#include "hud_import.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace tf2::native {

// ---------------------------------------------------------------------------
// The HudState: what is imported, whether it draws, and what it says.
//
// WHY THIS IS NOT PART OF hud_import.cpp
//   hud_import.cpp is pure: strings and integers in, values out, no disk, no
//   Win32. That is what lets hud_probe assert it without a window. Everything
//   that touches the filesystem or an INI file lives here instead, so the
//   probe never has to own a scratch directory to test the rules, and the
//   product's disk behaviour is testable on its own.
//
// THE SETTINGS CONTRACT
//   The [hud] section of ui.ini, per native/docs/camera-ui-contract.md 1.3:
//     hud.enabled  int 0/1        default 0    (a capture-only run draws no HUD)
//     hud.root     string         default ""   (absolute imported directory)
//     hud.layout   string         default ""   (document, relative to root)
//     hud.scale    int 50..200    default 100
//     hud.preview  int 0/1        default 1
//   Only these five keys are ever written. A key this build does not know is
//   left exactly as it is, because ui.ini is shared with T50 and with the
//   pre-existing [ui] section.
// ---------------------------------------------------------------------------

// Why an import was refused or accepted, in the words the UI shows. The trace
// prints the token, so a gate asserts the token and a human reads the text.
enum class HudApplyResult {
  Applied = 0,
  Refused,
  ResetToDefault,
  NothingToDo,
  SettingsWriteFailed,
};

const char* hudApplyResultName(HudApplyResult result);

// One import request, as the UI would build it after its picker returned.
struct HudImportRequest {
  std::filesystem::path root;    // the folder the user chose
  std::string layout;            // the document, relative to root
};

// The complete state of the feature. Owns no GPU resource and no window: the
// renderer is handed the rectangles it should fill.
class HudState {
public:
  // Reads [hud] from the given file. A missing file or a missing section is
  // the default state, not an error; a malformed value is repaired and
  // REPORTED through repaired(), because a settings file this program wrote
  // being silently overridden is the failure mode that makes reset a lie.
  bool loadFrom(const std::filesystem::path& file);

  // Writes exactly the five [hud] keys. Refuses when the file path is empty.
  bool saveTo(const std::filesystem::path& file) const;

  // Imports the request: validates the root, resolves the layout, parses it,
  // and resolves its rectangles for screenWidth x screenHeight at dpiScale.
  // On refusal the PREVIOUS state is left untouched, so a failed import cannot
  // leave a half-applied HUD behind.
  HudApplyResult import(const HudImportRequest& request, int screenWidth,
    int screenHeight, float dpiScale);

  // Re-resolves the already-imported document for a new client size. This is
  // what makes a resize visible in the preview rather than stale.
  void layoutFor(int screenWidth, int screenHeight, float dpiScale);

  // Clears hud.* to the contract's defaults and forgets the import. It does
  // NOT touch the user's directory: that is the promise the gate pins.
  HudApplyResult resetToDefault();

  // ---- readings ----------------------------------------------------------
  const HudSettings& settings() const { return settings_; }
  void setEnabled(bool enabled) { settings_.enabled = enabled; }
  void setScalePercent(int scalePercent) { settings_.scalePercent = clampHudScale(scalePercent); }
  void setPreview(bool preview) { settings_.preview = preview; }

  bool imported() const { return imported_; }
  const HudLayout& layout() const { return layout_; }
  const std::vector<HudScreenRect>& rects() const { return rects_; }
  HudPixelRect unionRect() const { return hudUnionRect(rects_); }
  // Three different questions, three different answers -- they used to share one
  // counter and a trace line that said "valid=" while printing the visible count.
  //   resolvedPanels()  geometry resolved (drawable or merely hidden)
  //   visiblePanels()   resolved AND the document says it is shown: what is drawn
  //   invalidPanels()   geometry could not be resolved: a refused/zero panel
  std::size_t resolvedPanels() const;
  std::size_t visiblePanels() const;
  std::size_t invalidPanels() const;

  HudRefusal lastRefusal() const { return lastRefusal_; }
  const std::string& lastRefusalDetail() const { return lastRefusalDetail_; }
  bool repaired() const { return repaired_; }
  const std::string& repairDetail() const { return repairDetail_; }
  // A discovery-level refusal (the folder held no document, or several), which
  // happens before there is a request to hand to import(). Counted in the same
  // tally so "the user clicked import and nothing happened" is a reading.
  void noteDiscoveryRefusal(const std::string& reason);

  // Apply the CLI/UI overrides in ONE place so the precedence rule is written
  // once instead of in every caller. Order is deliberate and documented at the
  // definition: import, then reset, then scale, then enabled.
  struct HudOverrides {
    bool importRequested = false;
    std::filesystem::path root;
    std::string layout;
    bool resetRequested = false;
    bool scaleGiven = false;
    int scalePercent = 100;
    bool enabledGiven = false;
    bool enabled = false;
  };
  // Returns the result of the last operation that changed anything; NothingToDo
  // when no field was set.
  HudApplyResult applyOverrides(const HudOverrides& overrides, int screenWidth,
    int screenHeight, float dpiScale);
  int refusedImports() const { return refusedImports_; }
  // The result of the most recent import/overrides application, so a caller can
  // report "refused" without keeping a second copy of the answer.
  HudApplyResult lastApply() const { return lastApply_; }
  int acceptedImports() const { return acceptedImports_; }

private:
  HudSettings settings_{};
  HudLayout layout_{};
  std::vector<HudScreenRect> rects_;
  bool imported_ = false;
  HudRefusal lastRefusal_ = HudRefusal::None;
  std::string lastRefusalDetail_;
  HudApplyResult lastApply_ = HudApplyResult::NothingToDo;
  bool repaired_ = false;
  std::string repairDetail_;
  int refusedImports_ = 0;
  int acceptedImports_ = 0;
  int lastWidth_ = 0;
  int lastHeight_ = 0;
  float lastDpi_ = 1.0f;
};

} // namespace tf2::native
