#include "logstf_viewer.h"

#include <Windows.h>
#include <objbase.h>
#include <wrl.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>
#include <thread>

// The SDK header defines all 255 interfaces in one translation unit; keep it
// here rather than in a shared header so adding it does not slow every target
// in the tree. It is the vendored copy under native/third_party/webview2 (see
// the CMake target), not a download at build time.
#include <WebView2.h>

// WHY THIS FILE EXISTS
//   logstf_viewer.cpp owns everything about the panel that can be decided
//   without a browser. This is the one part that cannot: the WebView2 backend.
//   It implements ILogsViewHost, so the controller, the URL policy and the
//   state machine stay testable against a recording fake, and only the browser
//   wiring is untested by the probe (a gate exercises it through the product).
//
// TWO DELIBERATE CHOICES
//   1. WebView2Loader.dll is loaded by name at run time, not linked. Linked, a
//      machine without the loader would fail to START the product; loaded, a
//      missing loader is one more entry in the runtime probe's answer and the
//      rest of the player still works. The Evergreen RUNTIME is already on this
//      machine (see logstf: runtime in the trace); only the 159 KB loader is
//      ours to ship, so no browser payload is packaged.
//   2. Creation is asynchronous and reported through status(): the panel must
//      be able to say "loading" between Navigate and the first page, and to
//      surface a failure with the runtime's own HRESULT instead of hanging.

namespace tf2::native {
namespace {

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

using CreateEnvironmentFn = HRESULT(STDAPICALLTYPE*)(PCWSTR, PCWSTR,
    ICoreWebView2EnvironmentOptions*, ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler*);
using GetVersionFn = HRESULT(STDAPICALLTYPE*)(PCWSTR, PCWSTR, LPWSTR*);

std::wstring widen(const std::string& value) {
  if (value.empty()) return {};
  const int size = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, nullptr, 0);
  if (size <= 0) return {};
  std::wstring result(static_cast<std::size_t>(size - 1), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, result.data(), size);
  return result;
}

std::string narrow(const wchar_t* value) {
  if (value == nullptr || value[0] == L'\0') return {};
  const int size = WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0, nullptr, nullptr);
  if (size <= 1) return {};
  std::string result(static_cast<std::size_t>(size - 1), '\0');
  WideCharToMultiByte(CP_UTF8, 0, value, -1, result.data(), size, nullptr, nullptr);
  return result;
}

std::string hresultText(HRESULT hr) {
  char buffer[64];
  std::snprintf(buffer, sizeof(buffer), "0x%08lX", static_cast<unsigned long>(hr));
  return buffer;
}

} // namespace

class WebView2LogsViewHost : public ILogsViewHost {
public:
  WebView2LogsViewHost() = default;
  ~WebView2LogsViewHost() override { destroy(); }

  bool start(void* parentWindow, const LogsPanelBounds& content,
      const std::string& dataDirectory, std::string* error) override {
    if (status_.load() == HostStatus::Ready) return true;
    if (status_.load() == HostStatus::Creating) return true;
    if (!comOwned_) {
      // APARTMENTTHREADED: the product's UI thread. S_FALSE means COM was
      // already initialized here, which is fine and is NOT ours to undo.
      const HRESULT comResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
      comOwned_ = SUCCEEDED(comResult);
      if (FAILED(comResult) && comResult != RPC_E_CHANGED_MODE) {
        status_.store(HostStatus::Failed);
        failure_ = "CoInitializeEx failed with " + hresultText(comResult)
            + "; the web view needs an apartment-threaded COM thread";
        if (error != nullptr) *error = failure_;
        return false;
      }
    }
    loadLibrary();
    if (createEnvironment_ == nullptr) {
      status_.store(HostStatus::Failed);
      failure_ = "WebView2Loader.dll could not be loaded or does not export "
          "CreateCoreWebView2EnvironmentWithOptions; the embedded viewer needs it next to the "
          "executable (the Evergreen runtime itself is already installed when logstf: runtime "
          "reports webview2)";
      if (error != nullptr) *error = failure_;
      return false;
    }
    parent_ = static_cast<HWND>(parentWindow);
    const std::wstring dataDirectoryW = dataDirectory.empty()
        ? std::wstring{} : widen(dataDirectory);
    status_.store(HostStatus::Creating);
    const HRESULT hr = createEnvironment_(nullptr,
        dataDirectoryW.empty() ? nullptr : dataDirectoryW.c_str(), nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [this, content](HRESULT result, ICoreWebView2Environment* environment) -> HRESULT {
              if (FAILED(result) || environment == nullptr) {
                fail("CreateCoreWebView2Environment failed with " + hresultText(result)
                    + " (the Evergreen runtime is not installed, or its version does not match "
                      "the loader)");
                return S_OK;
              }
              environment_ = environment;
              RECT bounds{content.x, content.y, content.x + content.w, content.y + content.h};
              const HRESULT controllerResult = environment->CreateCoreWebView2Controller(parent_,
                  Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                      [this, bounds](HRESULT controllerHr, ICoreWebView2Controller* controller) -> HRESULT {
                        if (FAILED(controllerHr) || controller == nullptr) {
                          fail("CreateCoreWebView2Controller failed with "
                              + hresultText(controllerHr));
                          return S_OK;
                        }
                        controller_ = controller;
                        controller_->get_CoreWebView2(&webview_);
                        if (webview_) {
                          webview_->get_Settings(&settings_);
                          if (settings_) {
                            // No dev tools and no context menus is not
                            // security; it is the difference between a viewer
                            // and a browser, which is what this panel is.
                            settings_->put_AreDevToolsEnabled(FALSE);
                            settings_->put_AreDefaultContextMenusEnabled(FALSE);
                            settings_->put_IsStatusBarEnabled(FALSE);
                          }
                          webview_->add_NavigationCompleted(
                              Callback<ICoreWebView2NavigationCompletedEventHandler>(
                                  [this](ICoreWebView2*,
                                      ICoreWebView2NavigationCompletedEventArgs* args) -> HRESULT {
                                    BOOL success = FALSE;
                                    if (args) args->get_IsSuccess(&success);
                                    navigationInFlight_.store(false);
                                    if (!success) {
                                      fail("the page failed to load (navigation completed "
                                          "unsuccessfully; the network or the host's TLS is the "
                                          "cause, not the viewer)");
                                    }
                                    return S_OK;
                                  }).Get(), &navigationToken_);
                        }
                        RECT rect = bounds;
                        if (controller_) controller_->put_Bounds(rect);
                        // IsVisible is on the CONTROLLER, not on ICoreWebView2:
                        // the controller is the window, the webview is the page
                        // inside it. Calling it on the page does not compile,
                        // which is how this line was found -- never built.
                        if (controller_) controller_->put_IsVisible(TRUE);
                        return S_OK;
                      }).Get());
              if (FAILED(controllerResult)) fail("CreateCoreWebView2Controller returned "
                  + hresultText(controllerResult));
              return S_OK;
            }).Get());
    if (FAILED(hr)) {
      fail("CreateCoreWebView2EnvironmentWithOptions returned " + hresultText(hr));
      if (error != nullptr) *error = failure_;
      return false;
    }
    return true;
  }

  HostStatus status() const override {
    // A failure is sticky until destroy(): a half-created browser must not
    // flicker back to Creating because a late callback arrived.
    if (!failure_.empty()) return HostStatus::Failed;
    if (!webview_) return status_.load() == HostStatus::Failed ? HostStatus::Failed : HostStatus::Creating;
    if (navigationInFlight_.load()) return HostStatus::Creating;
    return HostStatus::Ready;
  }
  std::string failureReason() const override { return failure_; }

  bool navigate(const std::string& url, std::string* error) override {
    if (!webview_) {
      if (error) *error = "the web view is not ready";
      return false;
    }
    // In flight, not "not created": status() reports Creating while this is
    // set, so the panel stays Loading until NavigationCompleted answers.
    failure_.clear();
    navigationInFlight_.store(true);
    const std::wstring target = widen(url);
    const HRESULT hr = webview_->Navigate(target.c_str());
    if (FAILED(hr)) {
      if (error) *error = "Navigate failed with " + hresultText(hr);
      return false;
    }
    url_ = url;
    return true;
  }

  bool navigationPending() const override { return navigationInFlight_.load(); }

  bool goBack(std::string* error) override {
    BOOL can = FALSE;
    if (webview_) webview_->get_CanGoBack(&can);
    if (!can) {
      if (error) *error = "no history entry behind this page";
      return false;
    }
    const HRESULT hr = webview_->GoBack();
    if (FAILED(hr) && error) *error = "GoBack failed with " + hresultText(hr);
    return SUCCEEDED(hr);
  }

  bool goForward(std::string* error) override {
    BOOL can = FALSE;
    if (webview_) webview_->get_CanGoForward(&can);
    if (!can) {
      if (error) *error = "no history entry ahead of this page";
      return false;
    }
    const HRESULT hr = webview_->GoForward();
    if (FAILED(hr) && error) *error = "GoForward failed with " + hresultText(hr);
    return SUCCEEDED(hr);
  }

  bool reload(std::string* error) override {
    if (!webview_) {
      if (error) *error = "the web view is not ready";
      return false;
    }
    const HRESULT hr = webview_->Reload();
    if (FAILED(hr) && error) *error = "Reload failed with " + hresultText(hr);
    return SUCCEEDED(hr);
  }

  bool canGoBack() const override {
    BOOL can = FALSE;
    if (webview_) webview_->get_CanGoBack(&can);
    return can != FALSE;
  }

  bool canGoForward() const override {
    BOOL can = FALSE;
    if (webview_) webview_->get_CanGoForward(&can);
    return can != FALSE;
  }

  std::string currentUrl() const override {
    if (!webview_) return url_;
    LPWSTR source = nullptr;
    if (FAILED(webview_->get_Source(&source)) || source == nullptr) return url_;
    std::string result = narrow(source);
    CoTaskMemFree(source);
    return result;
  }

  // ExecuteScript is asynchronous. The completion handler stores the JSON
  // string and pulses an event; this function pumps the CONTROLLER's window
  // messages while it waits, because the browser delivers the callback on this
  // same thread. Pumping only the browser's own HWND keeps the product's window
  // procedure out of the nested loop, so a script call cannot re-enter the
  // player's message handling.
  bool runScript(const std::string& script, std::string* result, std::string* error) override {
    if (!webview_) {
      if (error) *error = "the web view is not ready";
      return false;
    }
    // No nested evaluation. This function pumps the message queue while it waits,
    // and a dispatched message can re-enter product code that asks for another
    // script (the panel's own pump does). Two ExecuteScript calls waiting on each
    // other's pump is how a script step turns into a hang or a stack overflow, and
    // the probe's fake host is synchronous, so only the real runtime can reach it.
    if (scriptDepth_ != 0) {
      if (error) *error = "a script call is already in flight";
      return false;
    }
    ++scriptDepth_;
    struct DepthGuard {
      int& depth;
      ~DepthGuard() { --depth; }
    } depthGuard{scriptDepth_};
    std::atomic<bool> done{false};
    std::string value;
    std::string failure;
    const std::wstring wide = widen(script);
    const HRESULT hr = webview_->ExecuteScript(wide.c_str(),
        Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
            [&done, &value, &failure](HRESULT scriptHr, LPCWSTR jsonResult) -> HRESULT {
              if (FAILED(scriptHr)) failure = hresultText(scriptHr);
              else value = narrow(jsonResult);
              done.store(true);
              return S_OK;
            }).Get());
    if (FAILED(hr)) {
      if (error) *error = "ExecuteScript failed with " + hresultText(hr);
      return false;
    }
    // Pump EVERY message on this thread, not only the ones addressed to the panel's
    // parent window. WebView2 delivers ExecuteScript's completion to a window of
    // its own, so a PeekMessage filtered by hWnd never dispatches it: every call
    // then waits out the deadline below and reports a timeout that looks like the
    // page failing. (Found by the first real-runtime scroll run, 2026-10-22; the
    // probe cannot see it because its host completes synchronously.)
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(3000);
    while (!done.load()) {
      MSG message{};
      bool pumped = false;
      while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        pumped = true;
        TranslateMessage(&message);
        DispatchMessageW(&message);
      }
      if (std::chrono::steady_clock::now() > deadline) {
        if (error) *error = "the script did not complete within 3 s";
        return false;
      }
      if (!pumped) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (!failure.empty()) {
      if (error) *error = "the script failed with " + failure;
      return false;
    }
    if (result != nullptr) *result = value;
    return true;
  }

  void setBounds(const LogsPanelBounds& content) override {
    if (!controller_) return;
    RECT bounds{content.x, content.y, content.x + content.w, content.y + content.h};
    controller_->put_Bounds(bounds);
  }

  void destroy() override {
    if (webview_) {
      webview_->Stop();
      webview_.Reset();
    }
    if (controller_) {
      controller_->Close();
      controller_.Reset();
    }
    settings_.Reset();
    environment_.Reset();
    failure_.clear();
    navigationInFlight_.store(false);
    status_.store(HostStatus::Idle);
    if (comOwned_) {
      CoUninitialize();
      comOwned_ = false;
    }
  }

  const char* kind() const override { return "webview2"; }

  // The probe asks the same DLL the backend loads: "the loader exports what the
  // viewer calls" is a different question from "the runtime registry entry
  // exists", and the panel's explanation has to name whichever failed.
  static std::string loaderStatus() {
    HMODULE module = LoadLibraryW(L"WebView2Loader.dll");
    if (module == nullptr) {
      std::error_code error;
      const std::filesystem::path beside = std::filesystem::current_path(error) / L"WebView2Loader.dll";
      return "loader=missing (" + narrow(beside.c_str()) + " not loadable)";
    }
    const auto create = reinterpret_cast<CreateEnvironmentFn>(
        GetProcAddress(module, "CreateCoreWebView2EnvironmentWithOptions"));
    const auto version = reinterpret_cast<GetVersionFn>(
        GetProcAddress(module, "GetAvailableCoreWebView2BrowserVersionString"));
    std::string result = "loader=present";
    if (create == nullptr) result += " export-missing=CreateCoreWebView2EnvironmentWithOptions";
    if (version != nullptr) {
      LPWSTR available = nullptr;
      if (SUCCEEDED(version(nullptr, nullptr, &available)) && available != nullptr) {
        result += " browser=" + narrow(available);
        CoTaskMemFree(available);
      }
    }
    return result;
  }

private:
  void loadLibrary() {
    if (createEnvironment_ != nullptr) return;
    module_ = LoadLibraryW(L"WebView2Loader.dll");
    if (module_ == nullptr) return;
    createEnvironment_ = reinterpret_cast<CreateEnvironmentFn>(
        GetProcAddress(module_, "CreateCoreWebView2EnvironmentWithOptions"));
  }

  void fail(const std::string& reason) {
    failure_ = reason;
    status_.store(HostStatus::Failed);
  }

  HMODULE module_ = nullptr;
  CreateEnvironmentFn createEnvironment_ = nullptr;
  // [out] parameter of add_NavigationCompleted. The IDL marks it [out] with no
  // "may be null" contract, so passing nullptr is relying on undocumented
  // leniency of the runtime -- and this is the one line the probe's fake host
  // cannot cover, because only the real runtime ever reaches it.
  EventRegistrationToken navigationToken_{};
  int scriptDepth_ = 0;  // see runScript: nested evaluation is refused
  HWND parent_ = nullptr;
  std::atomic<HostStatus> status_{HostStatus::Idle};
  std::string failure_;

  // Two flags instead of one state, because they answer different questions:
  // "can this host be navigated" (webview_ != nullptr) and "did the page that
  // was asked for actually arrive" (navigationInFlight_). A single Ready flag
  // would report Content for a page that never loaded, which is the silent
  // success this task forbids.
  std::atomic<bool> navigationInFlight_{false};
  // COM: WebView2 requires an STA on the calling thread. The product has no
  // other COM user, so this file owns the initialization -- and owns its
  // matching uninitialize, tracked by comOwned_ so a thread that was already
  // initialized (S_FALSE) is not torn down underneath someone else.
  bool comOwned_ = false;
  std::string url_;
  ComPtr<ICoreWebView2Environment> environment_;
  ComPtr<ICoreWebView2Controller> controller_;
  ComPtr<ICoreWebView2> webview_;
  ComPtr<ICoreWebView2Settings> settings_;
};

// The one factory the product uses. Declared in logstf_viewer.h's companion
// below rather than in the header the probe includes, so a build without the
// WebView2 SDK still compiles the probe and reports Unavailable.
ILogsViewHost* createWebView2LogsHost() {
  return new WebView2LogsViewHost();
}

std::string webView2LoaderStatus() {
  return WebView2LogsViewHost::loaderStatus();
}

} // namespace tf2::native
