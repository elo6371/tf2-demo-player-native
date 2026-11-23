// main_shutdown.cpp -- N24 B2-1 (2026-11-20): code motion only.
//
// The exit-path persistence and teardown tail of wWinMain (main.cpp:15997-16065
// as it stood before this move) is copied here verbatim: same statements, same
// order, same trace text. The only additions are this header comment, the
// includes, the inputs struct and the namespace wrapper. See
// docs/N24-MAIN-SPLIT-PLAN-2026-11-16.md section 7 for the B2-1 row.
//
// WHY ONE FUNCTION AND NOT SEVEN
//   The seven steps are a sequence, not seven features: bookmarks and keybinds
//   are saved, the renderer shuts down, the logs panel's last frame is emitted,
//   the persistent settings are written, the T51 hud: keys reading is printed,
//   the [hud] section is saved, and the review session closes. Every one of them
//   is a write or a reading that would change what a *previous* step saw if it
//   moved, so splitting them further would invite exactly the reordering this
//   move is not allowed to make.
//
// WHAT DOES NOT MOVE
//   g_renderer = nullptr and the exit-code selection stay in wWinMain, after the
//   N12-C eye counters, because those two are the surrounding function's own
//   state, not the teardown tail's.
//
// THE TWO KEY TABLES ARE NOT INTERCHANGEABLE
//   The save below writes g_keyBinds (the window/camera-script table, declared
//   in main_internal.h) while the T51 reading below reads g_keybinds (the hud.*
//   table main_hud.cpp shares, declared in main_hud.h). They are two distinct
//   objects whose names differ only in case, and the original exit path did
//   precisely this: saved one and reported the other. Preserved verbatim here on
//   purpose -- "unifying" the spelling during a move would silently move the
//   [keys] write onto the other table.

#include "main_internal.h"

#include <string>

#include "asset_root.h"
#include "hud_state.h"
#include "logstf_panel.h"
#include "main_camera.h"
#include "main_hud.h"
#include "native_renderer.h"
#include "review_session.h"
#include "settings_store.h"

namespace tf2::native::main_internal {

void runShutdownPersistence(ShutdownPersistenceInputs& inputs) {
  // T50: the bookmarks and the key table are durable, so they are written at
  // exit whether or not a script ran. loadSettings() runs before the window is
  // created; this is the matching write. A script may have saved them already;
  // the write is idempotent.
  if (!tf2::native::settingsPath().empty()) {
    g_cameraBookmarks.saveToSettings(tf2::native::settingsPath());
    g_keyBinds.saveToSettings(tf2::native::settingsPath());
  }
  inputs.renderer.shutdown();
  // T54: the panel's browser must be gone before the process exits, and its
  // frame-time window is emitted here so the reading covers the whole run.
  {
    const std::string frameLine = tf2::native::logsPanelFrameLine();
    if (!frameLine.empty()) traceFmt("%s", frameLine.c_str());
    traceFmt("%s", tf2::native::logsPanelSummaryLine().c_str());
    traceFmt("%s", tf2::native::logsPanelRuntimeLine().c_str());
    tf2::native::logsPanelShutdown();
  }
  inputs.persistent.render = inputs.renderer.settings();
  if (inputs.assets.valid()) inputs.persistent.tfRoot = inputs.assets.tfDirectory;
  tf2::native::saveSettings(inputs.persistent);
  // T51: the shared hotkey table, as a reading.
  //
  // NOTHING IS WRITTEN HERE, and that is a decision rather than an omission.
  // [keys] is one table shared by two features (contract 1.2) and T50 ships the
  // editor that rebinds it; a second exit path writing the same section would be
  // two writers on one file, and a capture-only run would rewrite the tester's
  // bindings as a side effect of taking a screenshot. T51 registers its three
  // actions into the table so they are discoverable, dispatches them, and reports
  // them -- the write stays with the feature that owns the editor.
  if (g_keybindsLoaded) {
    traceFmt("hud: keys actions=%zu registered=%d dispatched=%u suppressed=%u conflicts=%u",
      g_keybinds.actions().size(),
      (g_keybinds.registered("hud.toggle") && g_keybinds.registered("hud.preview")
        && g_keybinds.registered("hud.reset")) ? 1 : 0,
      g_keybinds.dispatchOkCount(), g_keybinds.suppressedByFocusCount(),
      g_keybinds.conflictCount());
  }

  // T51: the [hud] section is written on the same exit path as [renderer], so a
  // user who imported a HUD and never pressed another button still gets it back
  // next run. A plain capture run writes nothing: hudTouched stays false unless
  // a hud.* value was actually asked for.
  {
    std::string hudStatus;
    const auto hudSettingsFile = uiSettingsFilePath(hudStatus);
    const bool hudTouched = inputs.hudConfigRequested || !inputs.hudRoot.empty()
      || !inputs.hudLayout.empty()
      || g_hud.imported() || g_hud.lastRefusal() != tf2::native::HudRefusal::None;
    if (!hudSettingsFile.empty() && hudTouched) {
      const bool saved = g_hud.saveTo(hudSettingsFile);
      traceFmt("hud: save path=%s ok=%d enabled=%d scale=%d",
        hudSettingsFile.u8string().c_str(), saved ? 1 : 0,
        g_hud.settings().enabled ? 1 : 0, g_hud.settings().scalePercent);
    }
    traceFmt("hud: summary imported=%d accepted=%d refused=%d panels=%zu visible=%zu"
      " invalid=%zu scale=%d enabled=%d lastReason=%s",
      g_hud.imported() ? 1 : 0, g_hud.acceptedImports(), g_hud.refusedImports(),
      g_hud.layout().panels.size(), g_hud.visiblePanels(), g_hud.invalidPanels(),
      g_hud.settings().scalePercent, g_hud.settings().enabled ? 1 : 0,
      tf2::native::hudRefusalName(g_hud.lastRefusal()));
  }
  // T71/T72: close the workspace on the way out. finish() captures where playback
  // really is, autosaves and reports the close state, which is what makes
  // "reopen the demo and the bookmark is still there" a property of an ordinary
  // run rather than of the gate that saved it.
  if (g_reviewSession) {
    g_reviewSession->finish();
    g_reviewSession.reset();
  }
}

}  // namespace tf2::native::main_internal
