// N24 B2-2 (2026-11-20): renderer initialize & T51/T67 HUD startup block moved
// out of wWinMain (main.cpp:8296-8376) as pure code motion.
//
// Ordering invariant:
//   renderer.initialize(window) -> ui.ini HUD load -> CLI HUD overrides &
//   discover/import/reset traces -> registerKeybinds(hudSettingsFile) ->
//   hudApplyRects(window) -> hudPanelSelfTest() -> hudUpdateStatus(window) ->
//   hudPublishTrace(window).
// DestroyWindow(window) + return 12, g_renderer = nullptr, and exit-code
// selection stay in wWinMain.

#include "main_internal.h"
#include "main_hud.h"

#include <Windows.h>

#include <filesystem>
#include <string>

#include "hud_panel.h"
#include "hud_state.h"
#include "native_renderer.h"

namespace tf2::native::main_internal {

bool initializeRendererAndHud(const RendererHudInitInputs& inputs) {
  if (!inputs.renderer.initialize(inputs.window)) {
    // The HRESULT is the only thing that distinguishes "no D3D device" from "a
    // shader failed to compile at run time", and both exit the same way.
    traceFmt("gpu: initialize failed stage=%s hr=0x%08lX",
        inputs.renderer.initStage(), static_cast<unsigned long>(inputs.renderer.lastError()));
    return false;
  }
  // ---- T51: load the HUD state and apply it --------------------------------
  //
  // The [hud] section lives in ui.ini, which is the file NativeUiController owns.
  // It is loaded after the renderer exists because the rectangles can only be
  // resolved once the window has a client size, and applied before the first
  // frame so a --hud-apply run captures a frame that already has the HUD on it.
  {
    std::string settingsStatus;
    const auto hudSettingsFile = uiSettingsFilePath(settingsStatus);
    if (!hudSettingsFile.empty()) {
      g_hud.loadFrom(hudSettingsFile);
    }
    traceFmt("hud: settings-file path=%s status=%s loaded=%d repaired=%d",
        hudSettingsFile.empty() ? "-" : hudSettingsFile.u8string().c_str(),
        settingsStatus.c_str(), hudSettingsFile.empty() ? 0 : 1, g_hud.repaired() ? 1 : 0);
    {
      tf2::native::HudState::HudOverrides overrides;
      std::string detail;
      overrides.importRequested = !inputs.hudRoot.empty() || !inputs.hudLayout.empty() || inputs.hudApplyRequested;
      overrides.root = inputs.hudRoot;
      overrides.layout = inputs.hudLayout;
      if (overrides.importRequested && overrides.layout.empty() && !inputs.hudRoot.empty()) {
        overrides.layout = discoverHudLayout(inputs.hudRoot, detail);
        if (overrides.layout.empty()) {
          // Nothing to import: recorded as a refusal rather than silently doing
          // nothing, for the reason the picker path gives.
          traceFmt("hud: import refused reason=%s detail=no-document-in-root",
              detail.compare(0, 10, "ambiguous:") == 0 ? "ambiguous-document" : "missing");
          g_hud.noteDiscoveryRefusal(detail.empty() ? "cli-no-document" : detail);
          overrides.importRequested = false;
        }
      }
      overrides.resetRequested = inputs.hudResetRequested;
      overrides.scaleGiven = inputs.hudScaleGiven;
      overrides.scalePercent = inputs.hudScale;
      overrides.enabledGiven = inputs.hudOnGiven;
      overrides.enabled = inputs.hudOn;
      RECT client{};
      GetClientRect(inputs.window, &client);
      const float dpiScale = static_cast<float>(GetDpiForWindow(inputs.window)) / 96.0f;
      const auto overridesResult = g_hud.applyOverrides(overrides,
          client.right, client.bottom, dpiScale);
      traceFmt("hud: cli import=%d reset=%d scale=%d enabled=%d result=%s discover=%s",
          overrides.importRequested ? 1 : 0, inputs.hudResetRequested ? 1 : 0,
          inputs.hudScaleGiven ? inputs.hudScale : -1, inputs.hudOnGiven ? (inputs.hudOn ? 1 : 0) : -1,
          tf2::native::hudApplyResultName(overridesResult),
          detail.empty() ? "-" : detail.c_str());
      if (overrides.importRequested) {
        traceFmt("hud: import result=%s document=%s source=cli",
            tf2::native::hudApplyResultName(g_hud.lastApply()), overrides.layout.c_str());
      }
      if (inputs.hudResetRequested) {
        // The reset's own return value, not the flag that asked for it: the gate
        // asserts "the reset ran", and a line printed from the request would be a
        // comment rather than a reading. g_hud.lastReset() is exactly the value
        // resetToDefault() returned, so this cannot print "reset" for a reset that
        // did not happen. (The button and hotkey paths print the same line from
        // hudResetToDefault; the CLI path needs its own because the button is not
        // what ran.)
        traceFmt("hud: reset result=%s", tf2::native::hudApplyResultName(g_hud.lastReset()));
      }
    }
    registerKeybinds(hudSettingsFile);
    hudApplyRects(inputs.window);
    // T67: the panel container is pure layout, so its self-test runs without a
    // device and is reported as a reading rather than a comment.
    {
      const tf2::native::HudPanelSelfTest panelTest = tf2::native::hudPanelSelfTest();
      traceFmt("hudpanel: cases=%zu pass=%zu fail=%zu", panelTest.cases, panelTest.passed, panelTest.failed);
    }
    hudUpdateStatus(inputs.window);
    hudPublishTrace(inputs.window);
  }
  return true;
}

}  // namespace tf2::native::main_internal
