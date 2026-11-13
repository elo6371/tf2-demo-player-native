// T76 stage 3 (2026-11-02): the native Win32 control layer -- layout, the
// per-frame control sync and the control creation -- moved verbatim out of
// main.cpp's anonymous namespace.
//
// These five functions are the only place the product pushes state at the
// Win32 controls, and they read nothing but the twenty-five handles and two
// flags that now live in tf2::native::main_internal. That is what made them the
// next slice: nothing in the cluster calls back into main.cpp, so the move is
// code motion and not a rewrite.
//
// Still in main.cpp, on purpose: dispatchHotkey and windowProc. windowProc is
// the message pump that calls these, and moving it would put the Win32 entry
// point in a second file for no gain while hud-check.sh still mutates text in
// main.cpp.
//
// Moved as motion: bodies, comments, the control ids and every string are the
// shipped bytes; only the namespace and the linkage changed.

#include "main_internal.h"

#include <Windows.h>
#include <commctrl.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <tuple>

#include "main_camera.h"
#include "main_hud.h"
#include "native_ui.h"
#include "playback_state.h"

namespace tf2::native::main_internal {

// Repositions every UI control from the planner's answer for the current
// client rect and window dpi. The planner guarantees containment and
// non-overlap; this function is the only place that turns a plan into
// MoveWindow calls, so a layout regression is a planner regression the probe
// can see, not a Win32 accident it cannot.
void applyUiLayout(HWND window) {
  if (!window) return;
  RECT client{};
  if (!GetClientRect(window, &client)) return;
  const UINT dpi = GetDpiForWindow(window);
  const auto plan = tf2::native::planUiLayout(client.right, client.bottom,
    static_cast<float>(dpi) / 96.0f);
  if (!plan.valid) return;
  const auto move = [&](tf2::native::UiControlId id, HWND control) {
    if (!control) return;
    const auto& rect = plan.rects[static_cast<std::size_t>(id)];
    MoveWindow(control, rect.x, rect.y, rect.w, rect.h, TRUE);
  };
  move(tf2::native::UiControlId::Status, g_uiStatus);
  move(tf2::native::UiControlId::Open, g_uiOpen);
  move(tf2::native::UiControlId::Cancel, g_uiCancel);
  move(tf2::native::UiControlId::Confirm, g_uiConfirm);
  move(tf2::native::UiControlId::Play, g_uiPlay);
  move(tf2::native::UiControlId::Stop, g_uiStop);
  move(tf2::native::UiControlId::Back, g_uiBack);
  move(tf2::native::UiControlId::Forward, g_uiForward);
  move(tf2::native::UiControlId::Reverse, g_uiReverse);
  move(tf2::native::UiControlId::Timeline, g_uiTimeline);
  move(tf2::native::UiControlId::CameraMode, g_uiCameraMode);
  move(tf2::native::UiControlId::FreeFly, g_uiFreeFly);
  move(tf2::native::UiControlId::ResetToPlayer, g_uiResetToPlayer);
  move(tf2::native::UiControlId::ResetView, g_uiResetView);
  move(tf2::native::UiControlId::BookmarkSave, g_uiBookmarkSave);
  move(tf2::native::UiControlId::BookmarkNameEdit, g_uiBookmarkName);
  move(tf2::native::UiControlId::BookmarkDelete, g_uiBookmarkDelete);
  move(tf2::native::UiControlId::BookmarkList, g_uiBookmarkList);
  move(tf2::native::UiControlId::KeybindReset, g_uiKeybindReset);
  // T51: the HUD row is placed by consuming the plan above, never by editing it.
  const auto hudPlan = tf2::native::planHudLayout(plan, client.right, client.bottom,
    static_cast<float>(dpi) / 96.0f);
  if (hudPlan.valid) {
    const auto moveHud = [&](tf2::native::UiHudControlId id, HWND control) {
      if (!control) return;
      const auto& rect = hudPlan.rects[static_cast<std::size_t>(id)];
      MoveWindow(control, rect.x, rect.y, rect.w, rect.h, TRUE);
    };
    moveHud(tf2::native::UiHudControlId::Import, g_uiHudImport);
    moveHud(tf2::native::UiHudControlId::Preview, g_uiHudPreview);
    moveHud(tf2::native::UiHudControlId::Scale, g_uiHudScale);
    moveHud(tf2::native::UiHudControlId::Reset, g_uiHudReset);
    moveHud(tf2::native::UiHudControlId::Status, g_uiHudStatus);
  }
  if (g_nativeUi) {
    g_nativeUi->setDisplayMetrics(dpi, static_cast<unsigned>(client.right),
      static_cast<unsigned>(client.bottom), IsZoomed(window) != 0);
  }
}

void setUiControlVisible(HWND control, bool visible) {
  if (control) ShowWindow(control, visible ? SW_SHOW : SW_HIDE);
}

// Signature of everything updateNativeUiControls actually pushes to a control:
// the screen, the fields that appear in the status line, the camera mode the
// player status line shows, and the timeline range and position. `dpi`/
// `recentDemos`/`tfRoot` are deliberately absent -- nothing here renders them,
// so a change there must not force a refresh. dtors of the tuple members
// (std::string) are cheap next to a SendMessageW round trip.
bool uiControlsUnchanged(const tf2::native::UiSnapshot& state, int cameraMode,
    const std::tuple<int, std::int32_t, std::int32_t, bool, bool, std::string,
      std::string, bool, std::uint32_t, std::string, int, std::size_t, bool, bool>&
      signature) {
  return std::get<0>(signature) == static_cast<int>(state.screen)
    && std::get<1>(signature) == state.tick
    && std::get<2>(signature) == state.ticks
    && std::get<3>(signature) == state.playing
    && std::get<4>(signature) == state.reverse
    && std::get<5>(signature) == state.mapName
    && std::get<6>(signature) == state.recordingType
    && std::get<7>(signature) == state.bspAvailable
    && std::get<8>(signature) == state.missingResourceCount
    && std::get<9>(signature) == state.error
    && std::get<10>(signature) == cameraMode
    && std::get<11>(signature) == g_uiBookmarkSlot
    && std::get<12>(signature) == g_cameraDirector.freeFly()
    && std::get<13>(signature) == g_textFieldFocused;
}

// Returns true when it actually pushed to the controls, false when the cached
// signature said there was nothing to push. Callers use the return value as the
// "work done" reading rather than counting the call itself.
bool updateNativeUiControls(HWND window) {
  if (!g_nativeUi) return false;
  const auto state = g_nativeUi->snapshot();
  const int cameraMode = static_cast<int>(g_cameraMode.mode());
  // Cached signature of the previous push. A static here is safe: there is one
  // window, hence one call site, and the whole reason this function is called
  // every iteration is to keep the controls in step with the playback state.
  static std::tuple<int, std::int32_t, std::int32_t, bool, bool, std::string,
    std::string, bool, std::uint32_t, std::string, int, std::size_t, bool, bool>
    lastSignature{};
  static bool signatureValid = false;
  // T50 adds the bookmark slot, the free-fly state and the text-field focus to
  // the cache key. Without them the camera label and the slot button would only
  // refresh when some unrelated field moved, which is exactly the stale-label
  // class this cache exists to avoid -- a control that is correct once and
  // frozen afterwards.
  const std::tuple<int, std::int32_t, std::int32_t, bool, bool, std::string,
    std::string, bool, std::uint32_t, std::string, int, std::size_t, bool, bool>
    signature{
      static_cast<int>(state.screen), state.tick, state.ticks, state.playing,
      state.reverse, state.mapName, state.recordingType, state.bspAvailable,
      state.missingResourceCount, state.error, cameraMode, g_uiBookmarkSlot,
      g_cameraDirector.freeFly(), g_textFieldFocused};
  if (signatureValid && uiControlsUnchanged(state, cameraMode, lastSignature)) return false;
  lastSignature = signature;
  signatureValid = true;
  const bool opening = state.screen == tf2::native::UiScreen::Opening;
  const bool review = state.screen == tf2::native::UiScreen::ImportReview;
  const bool player = state.screen == tf2::native::UiScreen::Player;
  const bool error = state.screen == tf2::native::UiScreen::Error;
  setUiControlVisible(g_uiOpen, opening || error);
  setUiControlVisible(g_uiCancel, review || error);
  setUiControlVisible(g_uiConfirm, review);
  setUiControlVisible(g_uiPlay, player);
  setUiControlVisible(g_uiStop, player);
  setUiControlVisible(g_uiBack, player);
  setUiControlVisible(g_uiForward, player);
  setUiControlVisible(g_uiReverse, player);
  setUiControlVisible(g_uiTimeline, player);
  // T50: the camera controls are player-screen controls, like the transport.
  setUiControlVisible(g_uiCameraMode, player);
  setUiControlVisible(g_uiFreeFly, player);
  setUiControlVisible(g_uiResetToPlayer, player);
  setUiControlVisible(g_uiResetView, player);
  setUiControlVisible(g_uiBookmarkSave, player);
  setUiControlVisible(g_uiBookmarkName, player);
  setUiControlVisible(g_uiBookmarkDelete, player);
  setUiControlVisible(g_uiBookmarkList, player);
  setUiControlVisible(g_uiKeybindReset, player);
  if (player) {
    // The camera row is a readout: the mode the frame is drawing from, plus
    // whether a text field currently owns the keyboard. Both are state a user
    // needs to see to understand why a key did nothing.
    if (g_uiCameraMode) {
      const char* modeName = tf2::native::cameraModeName(g_cameraMode.mode());
      std::wstring text = L"camera: " + std::wstring(modeName, modeName + std::strlen(modeName));
      if (g_cameraDirector.freeFly()) text += L" | free-fly";
      if (g_textFieldFocused) text += L" | typing";
      SetWindowTextW(g_uiCameraMode, text.c_str());
    }
    if (g_uiBookmarkList) {
      // "Slot n/9" carries the slot, whether it holds a bookmark and the name,
      // so the cycler's state is readable without opening a dialog.
      const auto& bookmark = g_cameraBookmarks.at(g_uiBookmarkSlot);
      std::wstring label = L"Slot " + std::to_wstring(g_uiBookmarkSlot);
      if (bookmark.saved) {
        label += L": " + std::wstring(bookmark.name.begin(), bookmark.name.end());
      }
      SetWindowTextW(g_uiBookmarkList, label.c_str());
    }
    if (g_uiBookmarkDelete) EnableWindow(g_uiBookmarkDelete,
      g_cameraBookmarks.saved(g_uiBookmarkSlot) ? TRUE : FALSE);
  }
  if (g_uiTimeline) {
    SendMessageW(g_uiTimeline, TBM_SETRANGE, TRUE,
      MAKELONG(0, std::max<std::int32_t>(0, state.ticks)));
    SendMessageW(g_uiTimeline, TBM_SETPOS, TRUE, state.tick);
  }
  std::wstring status = L"Opening: choose a .dem file";
  if (review) {
    status = L"Import review: " + std::wstring(state.mapName.begin(), state.mapName.end())
      + L" | " + std::wstring(state.recordingType.begin(), state.recordingType.end())
      + L" | ticks=" + std::to_wstring(state.ticks)
      + L" | BSP=" + (state.bspAvailable ? L"ready" : L"missing")
      + L" | missing=" + std::to_wstring(state.missingResourceCount);
  } else if (player) {
    status = L"Player: tick " + std::to_wstring(state.tick) + L"/" + std::to_wstring(state.ticks)
      + (state.playing ? L" | playing" : L" | paused")
      + (state.reverse ? L" | reverse" : L"")
      + L" | camera " + [&]() {
          const char* modeName = tf2::native::cameraModeName(g_cameraMode.mode());
          return std::wstring(modeName, modeName + std::strlen(modeName));
        }();
  } else if (error) {
    status = L"Error: " + std::wstring(state.error.begin(), state.error.end());
  }
  if (g_uiStatus) SetWindowTextW(g_uiStatus, status.c_str());
  if (window) InvalidateRect(window, nullptr, FALSE);
  return true;
}

void createNativeUiControls(HWND window, HINSTANCE instance) {
  INITCOMMONCONTROLSEX common{sizeof(common), ICC_BAR_CLASSES};
  InitCommonControlsEx(&common);
  const auto make = [&](LPCWSTR className, LPCWSTR text, DWORD style, int id,
      int x, int y, int width, int height) {
    return CreateWindowExW(0, className, text, WS_CHILD | style,
      x, y, width, height, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
      instance, nullptr);
  };
  // The initial geometry comes from the same planner WM_SIZE will use later,
  // so the first paint and every resize after it follow one set of rules.
  RECT client{};
  UINT dpi = 96;
  if (GetClientRect(window, &client)) {
    dpi = GetDpiForWindow(window);
  } else {
    client.right = 1280;
    client.bottom = 720;
  }
  const auto plan = tf2::native::planUiLayout(client.right, client.bottom,
    static_cast<float>(dpi) / 96.0f);
  const auto& rects = plan.rects;
  const auto rectFor = [&](tf2::native::UiControlId id, int fallbackX, int fallbackY,
      int fallbackW, int fallbackH) {
    if (!plan.valid) return std::make_tuple(fallbackX, fallbackY, fallbackW, fallbackH);
    const auto& rect = rects[static_cast<std::size_t>(id)];
    return std::make_tuple(rect.x, rect.y, rect.w, rect.h);
  };
  auto [sx, sy, sw, sh] = rectFor(tf2::native::UiControlId::Status, 16, 12, 900, 28);
  g_uiStatus = make(L"STATIC", L"Opening: choose a .dem file", SS_LEFT, 0, sx, sy, sw, sh);
  auto [ox, oy, ow, oh] = rectFor(tf2::native::UiControlId::Open, 16, 48, 120, 30);
  g_uiOpen = make(L"BUTTON", L"Open Demo...", BS_PUSHBUTTON, kUiOpen, ox, oy, ow, oh);
  auto [cx, cy, cw, ch] = rectFor(tf2::native::UiControlId::Cancel, 144, 48, 90, 30);
  g_uiCancel = make(L"BUTTON", L"Cancel", BS_PUSHBUTTON, kUiCancel, cx, cy, cw, ch);
  auto [fx, fy, fw, fh] = rectFor(tf2::native::UiControlId::Confirm, 240, 48, 90, 30);
  g_uiConfirm = make(L"BUTTON", L"Import", BS_DEFPUSHBUTTON, kUiConfirm, fx, fy, fw, fh);
  auto [px, py, pw, ph] = rectFor(tf2::native::UiControlId::Play, 16, 84, 110, 30);
  g_uiPlay = make(L"BUTTON", L"Play/Pause", BS_PUSHBUTTON, kUiPlay, px, py, pw, ph);
  auto [tx, ty, tw, th] = rectFor(tf2::native::UiControlId::Stop, 132, 84, 80, 30);
  g_uiStop = make(L"BUTTON", L"Stop", BS_PUSHBUTTON, kUiStop, tx, ty, tw, th);
  auto [bx, by, bw, bh] = rectFor(tf2::native::UiControlId::Back, 218, 84, 80, 30);
  g_uiBack = make(L"BUTTON", L"< Tick", BS_PUSHBUTTON, kUiBack, bx, by, bw, bh);
  auto [nx, ny, nw, nh] = rectFor(tf2::native::UiControlId::Forward, 304, 84, 80, 30);
  g_uiForward = make(L"BUTTON", L"Tick >", BS_PUSHBUTTON, kUiForward, nx, ny, nw, nh);
  auto [rx, ry, rw, rh] = rectFor(tf2::native::UiControlId::Reverse, 390, 84, 90, 30);
  g_uiReverse = make(L"BUTTON", L"Reverse", BS_PUSHBUTTON, kUiReverse, rx, ry, rw, rh);
  auto [lx, ly, lw, lh] = rectFor(tf2::native::UiControlId::Timeline, 16, 122, 700, 30);
  g_uiTimeline = make(TRACKBAR_CLASSW, L"", TBS_AUTOTICKS | TBS_ENABLESELRANGE,
    kUiTimeline, lx, ly, lw, lh);
  // T50: the camera controls. Labels are the shipped key names so the button and
  // the binding read the same; a rebound key does not rename the button (the
  // binding is the source of truth and the tooltip/hint names the action, not a
  // key that may have moved).
  auto [ffx, ffy, ffw, ffh] = rectFor(tf2::native::UiControlId::FreeFly, 16, 158, 90, 30);
  g_uiFreeFly = make(L"BUTTON", L"Free-fly", BS_PUSHBUTTON, kUiFreeFly, ffx, ffy, ffw, ffh);
  auto [rpx, rpy, rpw, rph] = rectFor(tf2::native::UiControlId::ResetToPlayer, 112, 158, 90, 30);
  g_uiResetToPlayer = make(L"BUTTON", L"To player", BS_PUSHBUTTON, kUiResetToPlayer,
    rpx, rpy, rpw, rph);
  auto [rvx, rvy, rvw, rvh] = rectFor(tf2::native::UiControlId::ResetView, 208, 158, 90, 30);
  g_uiResetView = make(L"BUTTON", L"Reset view", BS_PUSHBUTTON, kUiResetView,
    rvx, rvy, rvw, rvh);
  {
    // A copy, not a reference: the ternary would otherwise have to reconcile a
    // const reference with a temporary.
    tf2::native::UiRect rect{304, 158, 200, 30};
    if (plan.valid) rect = rects[static_cast<std::size_t>(tf2::native::UiControlId::CameraMode)];
    g_uiCameraMode = make(L"STATIC", L"camera: first-person", SS_LEFT, 0,
      rect.x, rect.y, rect.w, rect.h);
  }
  auto [bsx, bsy, bsw, bsh] = rectFor(tf2::native::UiControlId::BookmarkSave, 16, 194, 90, 28);
  g_uiBookmarkSave = make(L"BUTTON", L"Save mark", BS_PUSHBUTTON, kUiBookmarkSave,
    bsx, bsy, bsw, bsh);
  auto [bnx, bny, bnw, bnh] = rectFor(tf2::native::UiControlId::BookmarkNameEdit, 112, 194, 240, 28);
  // A text field owns the keyboard while it has focus: dispatchHotkey consults
  // shouldDispatchHotkey BEFORE the table, and the flag it reads is set from the
  // edit control EN_SETFOCUS / EN_KILLFOCUS notifications in WM_COMMAND. No
  // subclass is used: WM_COMMAND is a path the product already routes, so focus
  // state has one owner.
  g_uiBookmarkName = make(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL,
    kUiBookmarkName, bnx, bny, bnw, bnh);
  auto [bdx, bdy, bdw, bdh] = rectFor(tf2::native::UiControlId::BookmarkDelete, 358, 194, 90, 28);
  g_uiBookmarkDelete = make(L"BUTTON", L"Delete", BS_PUSHBUTTON, kUiBookmarkDelete,
    bdx, bdy, bdw, bdh);
  auto [blx, bly, blw, blh] = rectFor(tf2::native::UiControlId::BookmarkList, 454, 194, 110, 28);
  g_uiBookmarkList = make(L"BUTTON", L"Slot 1", BS_PUSHBUTTON, kUiBookmarkList,
    blx, bly, blw, blh);
  auto [krx, kry, krw, krh] = rectFor(tf2::native::UiControlId::KeybindReset, 570, 194, 110, 28);
  g_uiKeybindReset = make(L"BUTTON", L"Default keys", BS_PUSHBUTTON, kUiKeybindReset,
    krx, kry, krw, krh);
  // T51: the HUD row, from the HUD planner. It consumes the base plan computed
  // above, so the HUD controls follow whatever T50's row ends up being.
  const auto hudPlan = tf2::native::planHudLayout(plan, client.right, client.bottom,
    static_cast<float>(dpi) / 96.0f);
  const auto hudRectFor = [&](tf2::native::UiHudControlId id, int fallbackX,
      int fallbackY, int fallbackW, int fallbackH) {
    if (!hudPlan.valid) return std::make_tuple(fallbackX, fallbackY, fallbackW, fallbackH);
    const auto& rect = hudPlan.rects[static_cast<std::size_t>(id)];
    return std::make_tuple(rect.x, rect.y, rect.w, rect.h);
  };
  auto [hix, hiy, hiw, hih] = hudRectFor(tf2::native::UiHudControlId::Import, 16, 160, 130, 30);
  g_uiHudImport = make(L"BUTTON", L"Import HUD...", BS_PUSHBUTTON, kUiHudImport, hix, hiy, hiw, hih);
  auto [hpx, hpy, hpw, hph] = hudRectFor(tf2::native::UiHudControlId::Preview, 152, 160, 120, 30);
  g_uiHudPreview = make(L"BUTTON", L"Preview HUD", BS_AUTOCHECKBOX, kUiHudPreview, hpx, hpy, hpw, hph);
  auto [hsx, hsy, hsw, hsh] = hudRectFor(tf2::native::UiHudControlId::Scale, 278, 160, 160, 30);
  g_uiHudScale = make(TRACKBAR_CLASSW, L"", TBS_AUTOTICKS | TBS_HORZ,
    kUiHudScale, hsx, hsy, hsw, hsh);
  if (g_uiHudScale) {
    // 50..200 in the contract's window, one tick per 10% so a drag lands on a
    // round number and the preview text is readable.
    SendMessageW(g_uiHudScale, TBM_SETRANGE, TRUE, MAKELONG(50, 200));
    SendMessageW(g_uiHudScale, TBM_SETTICFREQ, 10, 0);
    SendMessageW(g_uiHudScale, TBM_SETPOS, TRUE, 100);
  }
  auto [hrx, hry, hrw, hrh] = hudRectFor(tf2::native::UiHudControlId::Reset, 444, 160, 140, 30);
  g_uiHudReset = make(L"BUTTON", L"Reset HUD", BS_PUSHBUTTON, kUiHudReset, hrx, hry, hrw, hrh);
  auto [hcx, hcy, hcw, hch] = hudRectFor(tf2::native::UiHudControlId::Status, 590, 160, 400, 30);
  g_uiHudStatus = make(L"STATIC", L"HUD: none", SS_LEFT, 0, hcx, hcy, hcw, hch);
  updateNativeUiControls(window);
}
}  // namespace tf2::native::main_internal
