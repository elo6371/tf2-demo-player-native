#include "logstf_panel.h"

#include "logstf_viewer.h"
#include "logstf_webview.h"

#include <commctrl.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>

// WHY THIS FILE EXISTS
//   See logstf_panel.h. In one sentence: logstf_viewer.cpp decides what the
//   panel is allowed to do, this file does it to a window, and main.cpp only
//   forwards six messages.
//
// THE THREE THINGS A READER SHOULD BE ABLE TO CHECK HERE
//   1. Nothing about the browser exists until a URL is imported. The child
//      window and the controls are created with the panel; the host object is
//      created by createWebView2LogsHost() on the first start, and the trace
//      line says whether that happened.
//   2. The scripted sequence drives the SAME entry points the interactive
//      controls use (importUrl, setWidth, goBack, ...). A script that took a
//      short cut would produce a reading no user could reproduce.
//   3. Every waiting step pumps the message loop for the same reason a real
//      run does: the browser answers on this thread.

namespace tf2::native {
namespace {

constexpr int kLogsUiBack = 4301;
constexpr int kLogsUiForward = 4302;
constexpr int kLogsUiReload = 4303;
constexpr int kLogsUiAddress = 4304;
constexpr int kLogsUiImport = 4305;
constexpr int kLogsUiClose = 4306;

HINSTANCE g_instance = nullptr;
HWND g_window = nullptr;         // the product window
HWND g_host = nullptr;           // the child the browser draws into
HWND g_back = nullptr;
HWND g_forward = nullptr;
HWND g_reload = nullptr;
HWND g_address = nullptr;
HWND g_import = nullptr;
HWND g_close = nullptr;
HWND g_explain = nullptr;        // the static that shows the reason/fallback

std::unique_ptr<LogsPanelController> g_controller;
std::unique_ptr<ILogsViewHost> g_hostObject;
LogsPanelOptions g_options;
FrameTimeHistogram g_frameWindow;      // samples while the panel is open
FrameTimeHistogram g_frameBaseline;    // samples while it was closed
bool g_panelWasOpen = false;
bool g_splitterDragging = false;
POINT g_splitterOrigin{};
int g_splitterStartWidth = 0;

std::wstring widen(const std::string& value) {
  if (value.empty()) return {};
  const int size = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, nullptr, 0);
  if (size <= 1) return {};
  std::wstring result(static_cast<std::size_t>(size - 1), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, result.data(), size);
  return result;
}

std::string narrow(const std::wstring& value) {
  if (value.empty()) return {};
  const int size = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
  if (size <= 1) return {};
  std::string result(static_cast<std::size_t>(size - 1), '\0');
  WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, result.data(), size, nullptr, nullptr);
  return result;
}

void moveControl(HWND control, const LogsPanelBounds& rect) {
  if (control == nullptr) return;
  if (!rect.visible) {
    ShowWindow(control, SW_HIDE);
    return;
  }
  MoveWindow(control, rect.x, rect.y, rect.w, rect.h, TRUE);
  ShowWindow(control, SW_SHOW);
}

// The data directory is WebView2's user-data folder. Kept under the product's
// own LOCALAPPDATA directory rather than the install directory (an installed
// copy may be read-only), and named in the trace so a run can prove it used the
// one it was told to.
std::string defaultUserDataDirectory() {
  wchar_t* value = nullptr;
  std::size_t length = 0;
  std::string result;
  if (_wdupenv_s(&value, &length, L"LOCALAPPDATA") == 0 && value != nullptr) {
    result = (std::filesystem::path(value) / L"TF2 Demo Player" / L"logs-webview").u8string();
    free(value);
  } else {
    result = (std::filesystem::current_path() / L"logs-webview").u8string();
  }
  std::error_code error;
  std::filesystem::create_directories(std::filesystem::path(result), error);
  return result;
}

void refreshControls() {
  if (!g_controller) return;
  const LogsPanelSnapshot snapshot = g_controller->snapshot();
  const bool open = snapshot.state != LogsPanelState::Closed;
  const bool content = snapshot.state == LogsPanelState::Content;
  ShowWindow(g_back, open ? SW_SHOW : SW_HIDE);
  ShowWindow(g_forward, open ? SW_SHOW : SW_HIDE);
  ShowWindow(g_reload, open ? SW_SHOW : SW_HIDE);
  ShowWindow(g_address, open ? SW_SHOW : SW_HIDE);
  ShowWindow(g_import, open ? SW_SHOW : SW_HIDE);
  ShowWindow(g_close, open ? SW_SHOW : SW_HIDE);
  ShowWindow(g_explain, open ? SW_SHOW : SW_HIDE);
  ShowWindow(g_host, content ? SW_SHOW : SW_HIDE);
  if (!open) return;
  EnableWindow(g_back, snapshot.canGoBack ? TRUE : FALSE);
  EnableWindow(g_forward, snapshot.canGoForward ? TRUE : FALSE);
  EnableWindow(g_reload, content ? TRUE : FALSE);
  // The address field shows what was typed; the panel does not rewrite it into
  // a canonical form under the user's cursor, but the trace prints the URL the
  // navigation actually used, so the two are never confused.
  if (GetFocus() != g_address) {
    SetWindowTextW(g_address, widen(snapshot.url).c_str());
  }
  std::wstring explanation;
  if (snapshot.state == LogsPanelState::Unavailable) {
    explanation = L"Logs viewer unavailable: " + widen(snapshot.unavailable)
        + L"\r\nFallback: " + widen(snapshot.fallback);
  } else if (snapshot.state == LogsPanelState::Empty) {
    explanation = L"No log loaded. Paste a logs.tf URL and press Import. Nothing is "
                  L"fetched until you do.";
  } else if (!snapshot.lastRefusal.empty()) {
    explanation = L"Refused: " + widen(snapshot.lastRefusal);
  } else if (snapshot.state == LogsPanelState::Loading) {
    explanation = L"Loading " + widen(snapshot.url) + L" ...";
  }
  SetWindowTextW(g_explain, explanation.c_str());
}

void applyPanelLayout(HWND window) {
  if (!g_controller || !window) return;
  RECT client{};
  if (!GetClientRect(window, &client)) return;
  const UINT dpi = GetDpiForWindow(window);
  g_controller->setClientSize(client.right, client.bottom,
    static_cast<float>(dpi) / 96.0f);
  const LogsPanelBounds panel = g_controller->panelBounds();
  const LogsToolbarPlan toolbar = planLogsToolbar(panel, static_cast<float>(dpi) / 96.0f);
  if (panel.visible && toolbar.valid) {
    moveControl(g_back, toolbar.rects[static_cast<std::size_t>(LogsToolbarControl::Back)]);
    moveControl(g_forward, toolbar.rects[static_cast<std::size_t>(LogsToolbarControl::Forward)]);
    moveControl(g_reload, toolbar.rects[static_cast<std::size_t>(LogsToolbarControl::Reload)]);
    moveControl(g_address, toolbar.rects[static_cast<std::size_t>(LogsToolbarControl::Address)]);
    moveControl(g_import, toolbar.rects[static_cast<std::size_t>(LogsToolbarControl::Import)]);
    moveControl(g_close, toolbar.rects[static_cast<std::size_t>(LogsToolbarControl::Close)]);
    moveControl(g_host, toolbar.content);
    // The explanation row sits in the content area and is hidden as soon as
    // there is a page: a page plus a paragraph about a missing page would be
    // two answers to one question.
    if (g_explain) {
      const LogsPanelBounds explanation{toolbar.content.x, toolbar.content.y,
        toolbar.content.w, std::min(toolbar.content.h, 120)};
      moveControl(g_explain, explanation);
      const LogsPanelSnapshot snapshot = g_controller->snapshot();
      const bool showExplanation = snapshot.state != LogsPanelState::Content;
      if (showExplanation) ShowWindow(g_explain, SW_SHOW);
      else ShowWindow(g_explain, SW_HIDE);
    }
  } else {
    HWND controls[] = {g_back, g_forward, g_reload, g_address, g_import, g_close, g_explain,
      g_host};
    for (HWND control : controls) if (control) ShowWindow(control, SW_HIDE);
  }
  refreshControls();
}

} // namespace

void logsPanelAttach(HWND window, HINSTANCE instance, const LogsPanelOptions& options) {
  g_window = window;
  g_instance = instance;
  g_options = options;

  // One runtime probe per process, reported once. Doing it here rather than per
  // navigation means the answer cannot change under a run, and a gate reads one
  // line that describes the whole process.
  const LogsRuntimeInfo runtime = probeLogsRuntime();
  g_hostObject.reset(createWebView2LogsHost());
  g_controller.reset(new LogsPanelController(g_hostObject.get(), runtime));
  g_controller->setParentWindow(window);
  g_controller->setDataDirectory(options.userData.empty()
      ? defaultUserDataDirectory() : options.userData);
  if (options.width > 0) g_controller->setInitialWidth(options.width);

  const auto make = [&](LPCWSTR className, LPCWSTR text, DWORD style, int id) {
    return CreateWindowExW(0, className, text, WS_CHILD | style, 0, 0, 10, 10, window,
      reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance, nullptr);
  };
  g_back = make(L"BUTTON", L"<", BS_PUSHBUTTON, kLogsUiBack);
  g_forward = make(L"BUTTON", L">", BS_PUSHBUTTON, kLogsUiForward);
  g_reload = make(L"BUTTON", L"Reload", BS_PUSHBUTTON, kLogsUiReload);
  // ES_AUTOHSCROLL, not ES_MULTILINE: a URL is one line, and a multi-line edit
  // would let a pasted CR reach the policy as an "unsupported character".
  g_address = make(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL, kLogsUiAddress);
  g_import = make(L"BUTTON", L"Import", BS_DEFPUSHBUTTON, kLogsUiImport);
  g_close = make(L"BUTTON", L"Close", BS_PUSHBUTTON, kLogsUiClose);
  g_explain = make(L"STATIC", L"", SS_LEFT | SS_EDITCONTROL, 0);
  // The browser's own child window: created up front so the browser has a
  // stable parent, hidden until there is content. Creating a window is not
  // creating a browser; no WebView2 object exists until start().
  g_host = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_CLIPCHILDREN,
    0, 0, 10, 10, window, nullptr, instance, nullptr);

  HWND controls[] = {g_back, g_forward, g_reload, g_address, g_import, g_close, g_explain,
    g_host};
  for (HWND control : controls) if (control) ShowWindow(control, SW_HIDE);
  if (g_explain) {
    SetWindowTextW(g_explain, L"No log loaded. Paste a logs.tf URL and press Import. "
                              L"Nothing is fetched until you do.");
  }

  // Layout BEFORE opening so the first open already sees the real client rect
  // and dpi: opening first and laying out second would size the panel against
  // the default 1280x720 for one frame, which is exactly the frame a capture
  // would take.
  applyPanelLayout(window);
  if (options.openAtStartup) g_controller->open();
  applyPanelLayout(window);
  g_panelWasOpen = g_controller->state() != LogsPanelState::Closed;
}

void logsPanelLayout(HWND window) {
  applyPanelLayout(window);
}

bool logsPanelCommand(HWND window, int id) {
  if (!g_controller) return false;
  switch (id) {
  case kLogsUiClose:
    g_controller->close();
    applyPanelLayout(window);
    return true;
  case kLogsUiBack:
    g_controller->goBack();
    applyPanelLayout(window);
    return true;
  case kLogsUiForward:
    g_controller->goForward();
    applyPanelLayout(window);
    return true;
  case kLogsUiReload:
    g_controller->reload();
    applyPanelLayout(window);
    return true;
  case kLogsUiImport: {
    wchar_t buffer[4096]{};
    const int length = g_address ? GetWindowTextW(g_address, buffer, 4096) : 0;
    const std::string typed = length > 0 ? narrow(std::wstring(buffer, static_cast<std::size_t>(length))) : std::string{};
    g_controller->importUrl(typed);
    applyPanelLayout(window);
    return true;
  }
  default:
    return false;
  }
}

// The splitter is a HIT TEST on the parent's client, not a child window: a
// child window would receive its own clicks and the parent would never see the
// drag. The grab band is kLogsPanelSplitterDip wide, centred on the panel's
// left edge, so a drag anywhere in that band resizes.
bool logsPanelMouse(HWND window, UINT message, WPARAM, LPARAM lParam) {
  if (!g_controller || !window) return false;
  const POINT point{static_cast<int>(static_cast<short>(LOWORD(lParam))),
    static_cast<int>(static_cast<short>(HIWORD(lParam)))};
  const LogsPanelBounds panel = g_controller->panelBounds();
  if (message == WM_LBUTTONDOWN) {
    if (!panel.visible) return false;
    RECT client{};
    if (!GetClientRect(window, &client)) return false;
    const float scale = static_cast<float>(GetDpiForWindow(window)) / 96.0f;
    const int band = static_cast<int>(static_cast<float>(kLogsPanelSplitterDip) * scale + 0.5f);
    // A drag only starts in the band, never over the toolbar controls: those
    // are children and take their own clicks, but the panel's painted margin
    // does not, so the band is what makes the edge grabbable there.
    if (point.x < panel.x - band || point.x > panel.x + band) return false;
    if (point.y < 0 || point.y > client.bottom) return false;
    g_splitterDragging = true;
    g_splitterStartWidth = panel.w;
    RECT origin{point.x, point.y, point.x, point.y};
    MapWindowPoints(window, nullptr, reinterpret_cast<POINT*>(&origin), 2);
    g_splitterOrigin = POINT{origin.left, origin.top};
    SetCapture(window);
    SetCursor(LoadCursorW(nullptr, IDC_SIZEWE));
    return true;
  }
  if (message == WM_MOUSEMOVE && g_splitterDragging) {
    POINT screen = point;
    ClientToScreen(window, &screen);
    // Dragging the splitter left makes the panel wider.
    const int delta = g_splitterOrigin.x - screen.x;
    g_controller->setWidth(g_splitterStartWidth + delta);
    applyPanelLayout(window);
    return true;
  }
  if (message == WM_LBUTTONUP && g_splitterDragging) {
    g_splitterDragging = false;
    if (GetCapture() == window) ReleaseCapture();
    return true;
  }
  return false;
}

void logsPanelPump(HWND window) {
  if (!g_controller) return;
  const LogsPanelState before = g_controller->state();
  g_controller->pump();
  const LogsPanelState after = g_controller->state();
  const bool open = after != LogsPanelState::Closed;
  // A state change moves which controls exist (empty -> content hides the
  // explanation and shows the browser), so it re-lays out exactly like an
  // open/close does. A steady state costs nothing.
  if (after != before || open != g_panelWasOpen || g_splitterDragging) {
    g_panelWasOpen = open;
    applyPanelLayout(window);
  }
}

void logsPanelSampleFrame(double milliseconds) {
  if (!g_controller) return;
  if (g_controller->state() == LogsPanelState::Closed) {
    g_frameBaseline.add(milliseconds);
  } else {
    g_frameWindow.add(milliseconds);
  }
}

bool logsPanelOpen() {
  return g_controller && g_controller->state() != LogsPanelState::Closed;
}

bool logsPanelHasContent() {
  return g_controller && g_controller->state() == LogsPanelState::Content;
}

HWND logsPanelHostWindow() {
  return g_host;
}

void logsPanelShutdown() {
  if (g_controller) {
    g_controller->close();
    g_controller.reset();
  }
  g_hostObject.reset();
}

// ---------------------------------------------------------------------------
// The scripted sequence
// ---------------------------------------------------------------------------
//
// A gate cannot click. This drives the same controller entry points the
// controls use, in named sequences, and pumps the message loop while it waits
// -- which is also what a real run does, because the browser answers on this
// thread.
namespace {

void pumpFor(HWND window, int milliseconds) {
  const ULONGLONG deadline = GetTickCount64() + static_cast<ULONGLONG>(std::max(0, milliseconds));
  do {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
      if (message.message == WM_QUIT) return;
      TranslateMessage(&message);
      DispatchMessageW(&message);
    }
    if (g_controller) g_controller->pump();
    // 5 ms slices: the browser's callbacks arrive on this thread, so a sleep
    // long enough to miss a callback is a sleep that turns the wait into a
    // timeout.
    MsgWaitForMultipleObjectsEx(0, nullptr, 5, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
  } while (GetTickCount64() < deadline);
}

// Settle: pump until the controller stops being Loading, or the deadline.
// Returns the state it reached. The distinction matters -- "still loading"
// reported as "content" would be the exact silent success this task forbids.
LogsPanelState settle(HWND window, int milliseconds) {
  const ULONGLONG deadline = GetTickCount64() + static_cast<ULONGLONG>(std::max(0, milliseconds));
  while (GetTickCount64() < deadline) {
    const LogsPanelState state = g_controller->state();
    if (state != LogsPanelState::Loading) return state;
    pumpFor(window, 25);
  }
  return g_controller ? g_controller->state() : LogsPanelState::Closed;
}

void failStep(const char* step, const std::string& detail) {
  if (g_options.exitReason.empty()) g_options.exitReason = std::string(step) + ": " + detail;
}

} // namespace

void logsPanelRunScript(HWND window) {
  if (!g_controller) return;
  const std::string& script = g_options.script;
  if (script.empty()) return;

  const int deadline = g_options.pumpMilliseconds;
  if (script == "empty") {
    if (!g_options.openAtStartup && !g_controller->open()) {
      failStep("empty", "the panel refused to open");
    }
    applyPanelLayout(window);
    pumpFor(window, std::min(deadline, 1500));
    const LogsPanelSnapshot snapshot = g_controller->snapshot();
    if (snapshot.state != LogsPanelState::Empty) {
      failStep("empty", std::string("state is ") + logsPanelStateName(snapshot.state)
          + ", expected empty");
    }
    return;
  }

  if (script == "import" || script == "history" || script == "scroll" || script == "resize"
      || script == "reopen" || script == "close") {
    if (g_controller->state() == LogsPanelState::Closed) g_controller->open();
    applyPanelLayout(window);
    if (g_options.url.empty()) {
      failStep(script.c_str(), "--logstf-url is required for this script");
      return;
    }
    if (!g_controller->importUrl(g_options.url)) {
      const LogsPanelSnapshot snapshot = g_controller->snapshot();
      failStep(script.c_str(), "the URL was refused: " + snapshot.lastRefusal);
      return;
    }
    applyPanelLayout(window);
    const LogsPanelState reached = settle(window, deadline);
    if (reached == LogsPanelState::Unavailable) {
      // Not a failure of the script: the panel did the right thing on a host
      // without a runtime, and the trace's logstf: summary line says so. The
      // gate decides what to do with it.
      return;
    }
    if (reached != LogsPanelState::Content) {
      failStep(script.c_str(), std::string("settled at ") + logsPanelStateName(reached)
          + ", expected content");
      return;
    }

    if (script == "history") {
      if (g_options.url2.empty()) {
        failStep("history", "--logstf-url2 is required for this script");
        return;
      }
      if (!g_controller->importUrl(g_options.url2)) {
        failStep("history", "the second URL was refused: " + g_controller->snapshot().lastRefusal);
        return;
      }
      settle(window, deadline);
      if (!g_controller->goBack()) failStep("history", "back was refused");
      pumpFor(window, 500);
      settle(window, deadline);
      if (!g_controller->goForward()) failStep("history", "forward was refused");
      pumpFor(window, 500);
      settle(window, deadline);
      if (!g_controller->reload()) failStep("history", "reload was refused");
      pumpFor(window, 500);
      settle(window, deadline);
    } else if (script == "scroll") {
      int offset = 0;
      if (!g_controller->scrollContent(480, &offset)) {
        failStep("scroll", "the scroll script failed");
      }
      int scrollHeight = 0;
      int viewportHeight = 0;
      if (!g_controller->pageExtent(&scrollHeight, &viewportHeight)) {
        failStep("scroll", "the extent script failed");
      }
    } else if (script == "resize") {
      // Both ends of the range, through the same entry point the splitter
      // uses. The reading a gate asserts is the clamped width, not the request.
      g_controller->setWidth(1);
      applyPanelLayout(window);
      g_controller->setWidth(100000);
      applyPanelLayout(window);
    } else if (script == "reopen") {
      g_controller->close();
      applyPanelLayout(window);
      pumpFor(window, 250);
      if (!g_controller->open()) failStep("reopen", "the panel refused to reopen");
      applyPanelLayout(window);
      const LogsPanelState reopened = settle(window, deadline);
      if (reopened != LogsPanelState::Content) {
        failStep("reopen", std::string("reopened at ") + logsPanelStateName(reopened));
      }
    } else if (script == "close") {
      g_controller->close();
      applyPanelLayout(window);
    }
    pumpFor(window, 250);
  }
}

std::string logsPanelRuntimeLine() {
  const LogsRuntimeInfo runtime = probeLogsRuntime();
  const std::string loader = webView2LoaderStatus();
  std::string line = "logstf: runtime kind=" + runtime.kind;
  line += " available=" + std::string(runtime.available ? "1" : "0");
  line += " source=" + (runtime.source.empty() ? std::string("absent") : runtime.source);
  line += " version=" + (runtime.version.empty() ? std::string("-") : runtime.version);
  // The loader status already names itself ("loader=present", "loader=missing
  // (path)"). Prefixing it here produced "loader=loader=present" in every trace
  // line of the first gate run -- a doubled token that the gate's "loader="
  // assertion passed on, and a reader would have spent a minute on.
  line += " " + (loader.empty() ? std::string("loader=not-compiled") : loader);
  if (!runtime.available) {
    line += " reason=" + runtime.reason;
    line += " fallback=" + runtime.fallback;
  }
  return line;
}

std::string logsPanelSummaryLine() {
  if (!g_controller) return "logstf: summary attached=0";
  const LogsPanelSnapshot snapshot = g_controller->snapshot();
  const LogsPanelBounds panel = g_controller->panelBounds();
  const LogsPanelBounds content = g_controller->contentBounds();
  RECT client{};
  if (g_window) GetClientRect(g_window, &client);
  const double dpiScale = g_window ? static_cast<double>(GetDpiForWindow(g_window)) / 96.0 : 1.0;
  char buffer[1280];
  std::snprintf(buffer, sizeof(buffer),
    "logstf: summary attached=1 script=%s state=%s host=%s panel=%d content=%d client=%d,%d"
    " dpiScale=%.2f url=%s"
    " navigations=%u accepted=%u refused=%u opens=%u closes=%u hostStarts=%u hostFailures=%u"
    " scrollCalls=%u scriptFailures=%u canBack=%d canForward=%d refusal=%s refusalReason=%s unavailable=%s",
    g_options.script.empty() ? "-" : g_options.script.c_str(),
    logsPanelStateName(snapshot.state),
    snapshot.hostAlive ? "alive" : "none",
    panel.visible ? panel.w : 0,
    content.visible ? content.h : 0,
    static_cast<int>(client.right), static_cast<int>(client.bottom), dpiScale,
    snapshot.url.empty() ? "-" : snapshot.url.c_str(),
    snapshot.navigations, snapshot.acceptedImports, snapshot.refusedImports,
    snapshot.opens, snapshot.closes, snapshot.hostStarts, snapshot.hostFailures,
    snapshot.scrollCalls, snapshot.scriptFailures,
    snapshot.canGoBack ? 1 : 0, snapshot.canGoForward ? 1 : 0,
    snapshot.lastRefusal.empty() ? "-" : snapshot.lastRefusal.c_str(),
    snapshot.lastRefusalReason.empty() ? "-" : snapshot.lastRefusalReason.c_str(),
    snapshot.unavailable.empty() ? "-" : snapshot.unavailable.c_str());
  std::string line = buffer;
  if (!g_options.exitReason.empty()) line += " scriptFailure=" + g_options.exitReason;
  return line;
}

std::string logsPanelFrameLine() {
  if (!g_options.measureFrames) return {};
  return "logstf: frames " + g_frameBaseline.summary("closed") + " | "
      + g_frameWindow.summary("open");
}

} // namespace tf2::native
