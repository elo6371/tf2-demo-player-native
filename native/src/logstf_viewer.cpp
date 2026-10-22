#include "logstf_viewer.h"

#include <Windows.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <system_error>
#include <utility>
#include <vector>

// WHY THIS FILE EXISTS
//   T54 asks for an embedded logs.tf viewer: an empty panel by default, a URL
//   that is validated before it is navigated, a resizable and scrollable pane
//   with browser-like back/forward/reload, a demo that keeps playing while it
//   is open, and an explicit answer when the host has no web-view runtime.
//
//   Every one of those except "does the pixel reach the screen" is decided in
//   code that needs no browser: the URL policy, the geometry, the state
//   machine, the runtime probe, the frame-time readings. Putting them here
//   rather than in main.cpp is what lets logstf_viewer_probe drive the SAME
//   controller the product drives -- with a recording fake host -- so "the empty
//   panel navigated nowhere" is a statement about the product's code path and
//   not about a second copy of it. The WebView2 backend (logstf_webview.cpp)
//   sits behind ILogsViewHost and is the only part that needs a browser.
//
//   The one thing this file deliberately cannot do is carry credentials: there
//   is no API for a header, a cookie, a form field or a proxy auth, and a URL
//   with user info is refused rather than stripped.

namespace tf2::native {
namespace {

std::string toLower(std::string value) {
  for (char& character : value) {
    character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
  }
  return value;
}

// The path shapes logs.tf actually serves. A host that passes the allow-list
// but names /admin or /../etc must still be refused: the allow-list answers
// "which site", this answers "which page of it".
bool isSupportedLogsPath(const std::string& path) {
  if (path.empty() || path == "/") return true;
  if (path[0] != '/') return false;
  // Split on '/', ignoring the leading empty segment.
  std::vector<std::string> segments;
  std::size_t start = 1;
  while (start <= path.size()) {
    const std::size_t slash = path.find('/', start);
    const std::size_t end = slash == std::string::npos ? path.size() : slash;
    segments.push_back(path.substr(start, end - start));
    if (slash == std::string::npos) break;
    start = slash + 1;
  }
  // A trailing slash leaves one empty segment; that is the same page.
  while (!segments.empty() && segments.back().empty()) segments.pop_back();
  if (segments.empty()) return true;
  if (segments.size() > 2) return false;
  const std::string& head = segments[0];
  if (head != "log" && head != "logs" && head != "match" && head != "player") {
    // A bare numeric id is also a log: https://logs.tf/1234
    if (segments.size() != 1) return false;
    return !head.empty() && std::all_of(head.begin(), head.end(),
      [](char character) { return character >= '0' && character <= '9'; });
  }
  if (segments.size() == 1) return true;  // /log, /player: the host redirects
  return !segments[1].empty();
}

const char* kUserInfoRefusal =
    "the URL carries user info (user:password@host); this viewer never sends, invents or replays "
    "user credentials, so the address is refused instead of stripped";

} // namespace

bool isAllowedLogsHost(const std::string& host) {
  const std::string lowered = toLower(host);
  return lowered == "logs.tf" || lowered == "www.logs.tf";
}

const char* logsUrlVerdictName(LogsUrlVerdict verdict) {
  switch (verdict) {
  case LogsUrlVerdict::Empty: return "empty";
  case LogsUrlVerdict::Accepted: return "accepted";
  case LogsUrlVerdict::Refused: return "refused";
  }
  return "unknown";
}

const char* logsUrlReasonName(LogsUrlReason reason) {
  switch (reason) {
  case LogsUrlReason::None: return "none";
  case LogsUrlReason::Missing: return "missing";
  case LogsUrlReason::TooLong: return "too-long";
  case LogsUrlReason::UnsupportedCharacter: return "unsupported-character";
  case LogsUrlReason::MissingScheme: return "missing-scheme";
  case LogsUrlReason::UnsupportedScheme: return "unsupported-scheme";
  case LogsUrlReason::NotHttps: return "not-https";
  case LogsUrlReason::MissingHost: return "missing-host";
  case LogsUrlReason::UserInfoRefused: return "user-info-refused";
  case LogsUrlReason::Malformed: return "malformed";
  case LogsUrlReason::PortNotAllowed: return "port-not-allowed";
  case LogsUrlReason::HostNotAllowed: return "host-not-allowed";
  case LogsUrlReason::PathNotSupported: return "path-not-supported";
  }
  return "unknown";
}

LogsUrlDecision validateLogsUrl(const std::string& input) {
  LogsUrlDecision decision;
  // Trim leading/trailing whitespace: a URL pasted from a chat has one, and
  // refusing it as "unsupported character" would name the wrong cause.
  std::size_t begin = 0;
  std::size_t end = input.size();
  while (begin < end && std::isspace(static_cast<unsigned char>(input[begin]))) ++begin;
  while (end > begin && std::isspace(static_cast<unsigned char>(input[end - 1]))) --end;
  const std::string raw = input.substr(begin, end - begin);

  if (raw.empty()) {
    decision.verdict = LogsUrlVerdict::Empty;
    decision.reason = LogsUrlReason::Missing;
    decision.message = "no address was entered; the panel stays empty until a logs.tf URL is imported";
    return decision;
  }
  if (raw.size() > kMaxLogsUrlLength) {
    decision.verdict = LogsUrlVerdict::Refused;
    decision.reason = LogsUrlReason::TooLong;
    decision.message = "the address is longer than " + std::to_string(kMaxLogsUrlLength) + " bytes";
    return decision;
  }
  for (unsigned char character : raw) {
    if (character < 0x20 || character >= 0x7F) {
      decision.verdict = LogsUrlVerdict::Refused;
      decision.reason = LogsUrlReason::UnsupportedCharacter;
      decision.message = "the address contains a control or non-ASCII byte; only plain ASCII URLs are accepted";
      return decision;
    }
  }
  const std::string lowered = toLower(raw);
  const std::size_t schemeEnd = lowered.find("://");
  if (schemeEnd == std::string::npos) {
    // "javascript:alert(1)" and "ms-settings:privacy" have a named scheme and
    // no authority. Reporting those as "no scheme" would name the wrong cause
    // and, worse, invite a normaliser that turns them into something navigable.
    // A scheme is [a-z][a-z0-9+.-]* followed by ':'. "ms-settings:privacy" is
    // one; "logs.tf/1" is not (no colon, and '.' is not a letter).
    const std::size_t colon = lowered.find(':');
    bool namedScheme = colon != std::string::npos && colon > 0
        && lowered[0] >= 'a' && lowered[0] <= 'z';
    for (std::size_t index = 0; namedScheme && index < colon; ++index) {
      const char character = lowered[index];
      const bool legal = (character >= 'a' && character <= 'z')
          || (character >= '0' && character <= '9')
          || character == '+' || character == '-' || character == '.';
      if (!legal) namedScheme = false;
    }
    decision.verdict = LogsUrlVerdict::Refused;
    decision.reason = namedScheme ? LogsUrlReason::UnsupportedScheme : LogsUrlReason::MissingScheme;
    decision.message = namedScheme
        ? "scheme '" + lowered.substr(0, colon) + "' is not accepted; only http:// and https:// are"
        : "the address has no scheme; enter https://logs.tf/<id> (a bare host is refused rather than guessed)";
    return decision;
  }
  const std::string scheme = lowered.substr(0, schemeEnd);
  // Only letters are allowed in a scheme, so "javascript:" and "file:" reach
  // the unsupported-scheme branch instead of being read as a host.
  if (scheme.find_first_not_of("abcdefghijklmnopqrstuvwxyz") != std::string::npos) {
    decision.verdict = LogsUrlVerdict::Refused;
    decision.reason = LogsUrlReason::UnsupportedScheme;
    decision.message = "the scheme is not a plain name; only http:// and https:// are accepted";
    return decision;
  }
  if (scheme == "http") {
    // Refused, not upgraded: see the header. Keeping this a refusal means the
    // reason the user sees (and the gate reads) is about the address they typed,
    // not about a silent rewrite of it.
    decision.verdict = LogsUrlVerdict::Refused;
    decision.reason = LogsUrlReason::NotHttps;
    decision.message = "this address is http://; the panel opens https:// pages only"
        " -- try https://" + lowered.substr(7);
    return decision;
  }
  if (scheme != "https") {
    decision.verdict = LogsUrlVerdict::Refused;
    decision.reason = LogsUrlReason::UnsupportedScheme;
    decision.message = "scheme '" + scheme + "' is not accepted; only https:// is";
    return decision;
  }

  std::string rest = raw.substr(schemeEnd + 3);
  // Backslashes are refused rather than normalised: browsers treat "\" as "/",
  // so a normaliser here would make "https:\\evil" read as an authority.
  if (rest.find('\\') != std::string::npos) {
    decision.verdict = LogsUrlVerdict::Refused;
    decision.reason = LogsUrlReason::Malformed;
    decision.message = "the address contains a backslash; only a normal / scheme://host/path URL is accepted";
    return decision;
  }
  // Strip a fragment. A query is kept: logs.tf uses it for view options.
  const std::size_t hash = rest.find('#');
  if (hash != std::string::npos) rest = rest.substr(0, hash);
  std::string query;
  const std::size_t question = rest.find('?');
  if (question != std::string::npos) {
    query = rest.substr(question);
    rest = rest.substr(0, question);
  }
  const std::size_t slash = rest.find('/');
  const std::string authority = slash == std::string::npos ? rest : rest.substr(0, slash);
  const std::string path = slash == std::string::npos ? std::string("/") : rest.substr(slash);

  if (authority.empty()) {
    decision.verdict = LogsUrlVerdict::Refused;
    decision.reason = LogsUrlReason::MissingHost;
    decision.message = "the address has no host";
    return decision;
  }
  if (authority.find('@') != std::string::npos) {
    decision.verdict = LogsUrlVerdict::Refused;
    decision.reason = LogsUrlReason::UserInfoRefused;
    decision.message = kUserInfoRefusal;
    return decision;
  }
  std::string host = authority;
  const std::size_t colon = authority.rfind(':');
  if (colon != std::string::npos) {
    const std::string port = authority.substr(colon + 1);
    if (port.empty() || port.find_first_not_of("0123456789") != std::string::npos) {
      decision.verdict = LogsUrlVerdict::Refused;
      decision.reason = LogsUrlReason::Malformed;
      decision.message = "the port is not a number";
      return decision;
    }
    // 443 is the default and therefore not a difference; anything else is
    // refused rather than silently dropped, because "https://logs.tf:8443/"
    // would be a different service than the one this panel promises.
    if (port != "443") {
      decision.verdict = LogsUrlVerdict::Refused;
      decision.reason = LogsUrlReason::PortNotAllowed;
      decision.message = "port " + port + " is not accepted; only the default 443 is";
      return decision;
    }
    host = authority.substr(0, colon);
  }
  if (host.empty()) {
    decision.verdict = LogsUrlVerdict::Refused;
    decision.reason = LogsUrlReason::MissingHost;
    decision.message = "the address has no host";
    return decision;
  }
  if (host.front() == '.' || host.back() == '.' || host.find("..") != std::string::npos) {
    decision.verdict = LogsUrlVerdict::Refused;
    decision.reason = LogsUrlReason::Malformed;
    decision.message = "the host has an empty label (a leading, trailing or doubled dot)";
    return decision;
  }
  if (!isAllowedLogsHost(host)) {
    decision.verdict = LogsUrlVerdict::Refused;
    decision.reason = LogsUrlReason::HostNotAllowed;
    decision.message = "'" + host + "' is not logs.tf; only logs.tf and www.logs.tf are accepted";
    return decision;
  }
  // ".." anywhere in the path is refused: this is a viewer for one site's
  // pages, and a traversal is never a page a user meant to open.
  if (path.find("..") != std::string::npos) {
    decision.verdict = LogsUrlVerdict::Refused;
    decision.reason = LogsUrlReason::Malformed;
    decision.message = "the path contains '..'; path traversal is refused";
    return decision;
  }
  if (!isSupportedLogsPath(path)) {
    decision.verdict = LogsUrlVerdict::Refused;
    decision.reason = LogsUrlReason::PathNotSupported;
    decision.message = "'" + path + "' is not a logs.tf log/player/match address";
    return decision;
  }

  decision.verdict = LogsUrlVerdict::Accepted;
  decision.reason = LogsUrlReason::None;
  decision.host = toLower(host);
  // https is the only accepted scheme, so nothing is ever rewritten; the field
  // stays because a decision records what it did, and "did not upgrade" is a
  // reading a caller can still assert.
  decision.upgradedToHttps = false;
  // The default port is dropped from the canonical form: "https://logs.tf:443/4"
  // and "https://logs.tf/4" are the same page, and a canonical URL that keeps
  // one of two spellings cannot be compared with the host's reported source.
  decision.url = "https://" + host + path + query;
  decision.message = "accepted: " + decision.url;
  return decision;
}

// ---------------------------------------------------------------------------
// Geometry
// ---------------------------------------------------------------------------
//
// The three rules, in the order they matter when a user drags the splitter
// past both edges:
//   1. the panel never leaves the client;
//   2. the demo keeps a minimum drawing width whenever the client can afford
//      both, so "make the panel huge" cannot make the demo disappear;
//   3. a request outside the range is clamped, not refused -- a drag that ends
//      past the edge stops at the edge.
int clampLogsPanelWidth(int requested, int clientWidth, float dpiScale) {
  if (clientWidth <= 0) return 0;
  if (!(dpiScale > 0.0f)) dpiScale = 1.0f;
  const auto scaled = [dpiScale](int dip) {
    const float pixels = static_cast<float>(dip) * dpiScale;
    return pixels < 1.0f ? 1 : static_cast<int>(pixels + 0.5f);
  };
  int minWidth = scaled(kMinLogsPanelWidthDip);
  int maxWidth = clientWidth - scaled(kMinDemoWidthDip);
  // A client too narrow for both keeps the same rule that already applies to
  // the control planner: clamp, do not refuse. The panel then takes what is
  // left and the demo's minimum is what gives way, never the panel's presence.
  if (maxWidth < 1) maxWidth = 1;
  if (minWidth > maxWidth) minWidth = maxWidth;
  if (requested < minWidth) return minWidth;
  if (requested > maxWidth) return maxWidth;
  return requested;
}

LogsPanelBounds planLogsPanel(bool open, int requestedWidth,
    int clientWidth, int clientHeight, float dpiScale) {
  LogsPanelBounds bounds;
  if (!open || clientWidth <= 0 || clientHeight <= 0) return bounds;
  const int width = clampLogsPanelWidth(requestedWidth, clientWidth, dpiScale);
  if (width <= 0) return bounds;
  bounds.x = clientWidth - width;
  bounds.y = 0;
  bounds.w = width;
  bounds.h = clientHeight;
  bounds.visible = true;
  return bounds;
}

LogsToolbarPlan planLogsToolbar(const LogsPanelBounds& panel, float dpiScale) {
  LogsToolbarPlan plan;
  if (!panel.visible || panel.w <= 0 || panel.h <= 0) return plan;
  if (!(dpiScale > 0.0f)) dpiScale = 1.0f;
  const auto scaled = [dpiScale](int dip) {
    const float pixels = static_cast<float>(dip) * dpiScale;
    return pixels < 1.0f ? 1 : static_cast<int>(pixels + 0.5f);
  };
  const int pad = scaled(6);
  const int button = scaled(30);
  const int rowHeight = scaled(32);
  const int gap = scaled(4);

  int cursor = panel.x + pad;
  const int right = panel.x + panel.w - pad;
  const auto place = [&](std::size_t index, int width) {
    LogsPanelBounds& rect = plan.rects[index];
    const int clampedWidth = std::max(0, std::min(width, right - cursor));
    rect.x = cursor;
    rect.y = panel.y + pad;
    rect.w = clampedWidth;
    rect.h = button;
    rect.visible = clampedWidth > 0;
    cursor += clampedWidth + gap;
  };
  place(static_cast<std::size_t>(LogsToolbarControl::Back), button);
  place(static_cast<std::size_t>(LogsToolbarControl::Forward), button);
  place(static_cast<std::size_t>(LogsToolbarControl::Reload), button);

  // The address field takes the slack; import and close are anchored to the
  // right edge so they never slide out of reach as the panel narrows.
  //
  // Each of the three is measured from the space that is actually left, right
  // to left, and shortened rather than allowed to slide over its neighbour: a
  // narrow panel clips the address field to nothing and then the Import button,
  // and never overlaps two controls. The earlier version anchored from the
  // right but let a short panel push Import across Close, which is a layout
  // defect a user sees as two labels on top of each other.
  const int closeWidth = std::min(button, std::max(0, right - cursor));
  const int closeX = right - closeWidth;
  const int importWidth = std::min(scaled(64),
      std::max(0, closeX - gap - cursor));
  const int importX = closeX - gap - importWidth;
  const int addressWidth = std::max(0, importX - gap - cursor);

  LogsPanelBounds& address = plan.rects[static_cast<std::size_t>(LogsToolbarControl::Address)];
  address.x = cursor;
  address.y = panel.y + pad;
  address.w = addressWidth;
  address.h = button;
  address.visible = addressWidth > 0;

  LogsPanelBounds& import = plan.rects[static_cast<std::size_t>(LogsToolbarControl::Import)];
  import.x = importWidth > 0 ? importX : std::min(cursor, closeX);
  import.y = panel.y + pad;
  import.w = importWidth;
  import.h = button;
  import.visible = importWidth > 0;

  LogsPanelBounds& close = plan.rects[static_cast<std::size_t>(LogsToolbarControl::Close)];
  close.x = closeX;
  close.y = panel.y + pad;
  close.w = closeWidth;
  close.h = button;
  close.visible = closeWidth > 0;

  const int contentTop = panel.y + pad + rowHeight + pad;
  plan.content.x = panel.x;
  plan.content.y = contentTop;
  plan.content.w = panel.w;
  plan.content.h = std::max(0, panel.y + panel.h - contentTop);
  plan.content.visible = plan.content.w > 0 && plan.content.h > 0;
  plan.valid = true;
  return plan;
}

// ---------------------------------------------------------------------------
// Web-view runtime probe
// ---------------------------------------------------------------------------
//
// Registry first, directory second, and both are reported: they fail
// differently. A runtime whose registry entry survived an uninstall leaves
// files missing (the loader will fail with a version-not-found), and a runtime
// whose files are present but whose registration is gone fails the same way.
// "Not available" therefore carries which of the two answers produced it.
LogsRuntimeInfo decideLogsRuntime(const LogsRuntimeInputs& inputs) {
  LogsRuntimeInfo info;
  // The three registry views in the order they are consulted. The WOW64 view is
  // first because that is where the Evergreen installer writes on a 64-bit
  // Windows, which is the only shape this product ships for.
  const std::string* order[] = {&inputs.registryVersionHklmWow64,
    &inputs.registryVersionHklm, &inputs.registryVersionHkcu};
  for (const std::string* candidate : order) {
    if (candidate->empty()) continue;
    info.available = true;
    info.kind = "webview2";
    info.version = *candidate;
    info.source = "registry";
    info.reason.clear();
    return info;
  }

  // No registration. The install directory can still answer -- but only if the
  // browser binary is really there: a version folder left behind by a failed
  // install is the failure this branch exists to name instead of treating as a
  // runtime.
  const LogsRuntimeInputs::InstallDirectory* best = nullptr;
  bool sawDirectoryWithoutBrowser = false;
  for (const auto& directory : inputs.installDirectories) {
    if (directory.version.empty()) continue;
    if (!directory.hasBrowser) { sawDirectoryWithoutBrowser = true; continue; }
    if (best == nullptr || directory.version > best->version) best = &directory;
  }
  if (best != nullptr) {
    info.available = true;
    info.kind = "webview2";
    info.version = best->version;
    info.source = "installation-dir";
    info.reason.clear();
    return info;
  }

  info.available = false;
  info.kind = "none";
  info.source = "absent";
  info.version.clear();
  if (sawDirectoryWithoutBrowser) {
    info.reason =
        "a WebView2 installation directory exists but holds no msedgewebview2.exe "
        "(a half-installed runtime); the embedded viewer needs a complete install or the "
        "fixed-version runtime shipped with the package";
  } else {
    info.reason =
        "no Microsoft Edge WebView2 runtime: the Evergreen client is not registered under "
        "HKLM/HKCU Software\\Microsoft\\EdgeUpdate\\Clients and no "
        "%ProgramFiles(x86)%\\Microsoft\\EdgeWebView\\Application\\<version>\\msedgewebview2.exe was found; "
        "the embedded viewer needs it (install the Evergreen Runtime, or ship the fixed-version runtime "
        "with the package: WebView2Loader.dll plus the EBWebView directory)";
  }
  return info;
}

namespace {

std::string readRegistryVersion(HKEY root, const wchar_t* key) {
  wchar_t version[128]{};
  DWORD bytes = sizeof(version);
  const LSTATUS status = RegGetValueW(root, key, L"pv", RRF_RT_REG_SZ, nullptr, version, &bytes);
  if (status != ERROR_SUCCESS || version[0] == L'\0') return {};
  char narrow[128]{};
  if (WideCharToMultiByte(CP_UTF8, 0, version, -1, narrow, sizeof(narrow) - 1, nullptr, nullptr) <= 0) {
    return {};
  }
  return narrow;
}

} // namespace

LogsRuntimeInfo probeLogsRuntime() {
  constexpr wchar_t kClientKey[] =
      L"SOFTWARE\\Microsoft\\EdgeUpdate\\Clients\\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}";
  constexpr wchar_t kWow64ClientKey[] =
      L"SOFTWARE\\WOW6432Node\\Microsoft\\EdgeUpdate\\Clients\\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}";

  LogsRuntimeInputs inputs;
  inputs.registryVersionHklmWow64 = readRegistryVersion(HKEY_LOCAL_MACHINE, kWow64ClientKey);
  inputs.registryVersionHklm = readRegistryVersion(HKEY_LOCAL_MACHINE, kClientKey);
  inputs.registryVersionHkcu = readRegistryVersion(HKEY_CURRENT_USER, kWow64ClientKey);
  if (inputs.registryVersionHkcu.empty()) {
    inputs.registryVersionHkcu = readRegistryVersion(HKEY_CURRENT_USER, kClientKey);
  }

  // The install directory the Evergreen bootstrapper leaves behind. Only read
  // when the registry had nothing: the directory answer is the fallback, and a
  // gate reading "installation-dir" should be able to say exactly that.
  if (inputs.registryVersionHklmWow64.empty() && inputs.registryVersionHklm.empty()
      && inputs.registryVersionHkcu.empty()) {
    wchar_t programFilesX86[MAX_PATH]{};
    if (GetEnvironmentVariableW(L"ProgramFiles(x86)", programFilesX86, MAX_PATH) == 0) {
      GetEnvironmentVariableW(L"ProgramFiles", programFilesX86, MAX_PATH);
    }
    if (programFilesX86[0] != L'\0') {
      const std::filesystem::path root = std::filesystem::path(programFilesX86)
          / L"Microsoft" / L"EdgeWebView" / L"Application";
      std::error_code error;
      if (std::filesystem::is_directory(root, error)) {
        for (std::filesystem::directory_iterator it(root, error), last;
             !error && it != last; it.increment(error)) {
          if (!it->is_directory(error)) continue;
          LogsRuntimeInputs::InstallDirectory entry;
          const std::wstring name = it->path().filename().wstring();
          char narrow[128]{};
          WideCharToMultiByte(CP_UTF8, 0, name.c_str(), -1, narrow, sizeof(narrow) - 1, nullptr, nullptr);
          entry.version = narrow;
          entry.hasBrowser = std::filesystem::exists(it->path() / L"msedgewebview2.exe", error);
          inputs.installDirectories.push_back(entry);
        }
      }
    }
  }
  return decideLogsRuntime(inputs);
}

// ---------------------------------------------------------------------------
// Panel state machine
// ---------------------------------------------------------------------------
const char* logsPanelStateName(LogsPanelState state) {
  switch (state) {
  case LogsPanelState::Closed: return "closed";
  case LogsPanelState::Empty: return "empty";
  case LogsPanelState::Loading: return "loading";
  case LogsPanelState::Content: return "content";
  case LogsPanelState::Unavailable: return "unavailable";
  }
  return "unknown";
}

LogsPanelController::LogsPanelController(ILogsViewHost* host, LogsRuntimeInfo runtime)
    : host_(host), runtime_(std::move(runtime)) {
  if (runtime_.fallback.empty()) {
    // The struct has a default, but a caller that built one by hand could have
    // cleared it. A missing runtime must always name a fallback, so this is the
    // one place that guarantees it rather than trusting every call site.
    runtime_.fallback =
        "open the same logs.tf URL in the system browser (the panel keeps the address and this reason visible)";
  }
  counters_.state = LogsPanelState::Closed;
  counters_.fallback = runtime_.fallback;
}

void LogsPanelController::setClientSize(int width, int height, float dpiScale) {
  clientWidth_ = width > 0 ? width : 0;
  clientHeight_ = height > 0 ? height : 0;
  dpiScale_ = dpiScale > 0.0f ? dpiScale : 1.0f;
  layout();
}

void LogsPanelController::setContentBounds(const LogsPanelBounds& content) {
  content_ = content;
  if (host_ && counters_.hostAlive && content.visible) {
    countHostCall();
    host_->setBounds(content);
  }
}

bool LogsPanelController::open() {
  if (state_ != LogsPanelState::Closed) return false;
  ++counters_.opens;
  // The empty state is entered WITHOUT touching the host: no creation, no
  // navigation, no default page. That is the whole of "empty panel by default".
  //
  // A panel that HAD a page restores it on reopen, and restoring is an explicit
  // navigation this call arms -- not a side effect of creating the host. The
  // two behaviours are therefore distinguishable in the counters: an empty
  // reopen leaves navigations at 0, a restoring reopen adds exactly one per
  // reopen once the host is ready.
  if (url_.empty()) {
    state_ = LogsPanelState::Empty;
    counters_.state = state_;
    return true;
  }
  if (!runtime_.available || host_ == nullptr) {
    unavailable_ = runtime_.reason.empty()
        ? "no web-view runtime is available to this build"
        : runtime_.reason;
    counters_.unavailable = unavailable_;
    state_ = LogsPanelState::Unavailable;
    counters_.state = state_;
    return true;
  }
  // A closed panel's browser is gone, so the restore goes through the same
  // create-then-navigate path a first import uses.
  counters_.hostAlive = false;
  pendingNavigate_ = true;
  state_ = LogsPanelState::Loading;
  counters_.state = state_;
  return true;
}

bool LogsPanelController::close() {
  if (state_ == LogsPanelState::Closed) return false;
  ++counters_.closes;
  if (host_ && counters_.hostAlive) {
    countHostCall();
    host_->destroy();
  }
  counters_.hostAlive = false;
  counters_.canGoBack = false;
  counters_.canGoForward = false;
  // The address is kept so a reopen can resume the same page; what is dropped
  // is the live browser, its history and any pending navigation.
  pendingNavigate_ = false;
  state_ = LogsPanelState::Closed;
  counters_.state = state_;
  return true;
}

bool LogsPanelController::toggle() {
  return state_ == LogsPanelState::Closed ? open() : close();
}

bool LogsPanelController::importUrl(const std::string& raw) {
  const LogsUrlDecision decision = validateLogsUrl(raw);
  if (decision.verdict == LogsUrlVerdict::Empty) {
    lastRefusal_ = decision.message;
    counters_.lastRefusal = lastRefusal_;
    counters_.lastRefusalReason = logsUrlReasonName(decision.reason);
    return false;
  }
  if (decision.verdict == LogsUrlVerdict::Refused) {
    ++counters_.refusedImports;
    lastRefusal_ = decision.message;
    counters_.lastRefusal = lastRefusal_;
    counters_.lastRefusalReason = logsUrlReasonName(decision.reason);
    counters_.state = state_;
    return false;
  }
  ++counters_.acceptedImports;
  lastRefusal_.clear();
  counters_.lastRefusal.clear();
  counters_.lastRefusalReason.clear();
  url_ = decision.url;
  addressText_ = raw;
  counters_.url = url_;
  if (state_ == LogsPanelState::Closed) open();

  // No runtime: the URL is still recorded and shown, the reason and the
  // fallback are surfaced, and the panel does not pretend to load a page.
  if (!runtime_.available || host_ == nullptr) {
    unavailable_ = runtime_.reason.empty()
        ? "no web-view runtime is available to this build"
        : runtime_.reason;
    counters_.unavailable = unavailable_;
    counters_.fallback = runtime_.fallback;
    state_ = LogsPanelState::Unavailable;
    counters_.state = state_;
    return true;
  }

  pendingNavigate_ = true;
  state_ = LogsPanelState::Loading;
  counters_.state = state_;
  if (counters_.hostAlive) {
    // A live host can be navigated immediately; the asynchronous create path
    // is entered only when the browser does not exist yet.
    std::string error;
    countHostCall();
    ++counters_.navigations;
    if (!host_->navigate(url_, &error)) {
      unavailable_ = error.empty() ? "the web view refused to navigate" : error;
      counters_.unavailable = unavailable_;
      state_ = LogsPanelState::Unavailable;
      counters_.state = state_;
      pendingNavigate_ = false;
      return false;
    }
    pendingNavigate_ = false;
    // Loading, plus the pump below promotes it once the host says the page is
    // there. Leaving it Loading is what makes the asynchronous path and the
    // synchronous one the same state machine.
    if (!host_->navigationPending()) {
      state_ = LogsPanelState::Content;
      counters_.state = state_;
    }
  }
  return true;
}

void LogsPanelController::pump() {
  if (state_ != LogsPanelState::Loading || host_ == nullptr) return;
  if (!counters_.hostAlive) {
    std::string error;
    countHostCall();
    ++counters_.hostStarts;
    const LogsPanelBounds content = contentBounds();
    if (!host_->start(parent_, content, dataDirectory_, &error)) {
      ++counters_.hostFailures;
      unavailable_ = error.empty() ? "the web view could not be created" : error;
      counters_.unavailable = unavailable_;
      state_ = LogsPanelState::Unavailable;
      counters_.state = state_;
      return;
    }
    counters_.hostAlive = true;
  }
  const HostStatus status = host_->status();
  if (status == HostStatus::Failed) {
    ++counters_.hostFailures;
    unavailable_ = host_->failureReason();
    if (unavailable_.empty()) unavailable_ = "the web view failed to start";
    counters_.unavailable = unavailable_;
    counters_.hostAlive = false;
    state_ = LogsPanelState::Unavailable;
    counters_.state = state_;
    return;
  }
  if (status != HostStatus::Ready) return;
  if (pendingNavigate_) {
    std::string error;
    countHostCall();
    ++counters_.navigations;
    if (!host_->navigate(url_, &error)) {
      unavailable_ = error.empty() ? "the web view refused to navigate" : error;
      counters_.unavailable = unavailable_;
      state_ = LogsPanelState::Unavailable;
      counters_.state = state_;
      pendingNavigate_ = false;
      return;
    }
    pendingNavigate_ = false;
  }
  // Stay Loading while the browser is still on its way. "Navigate returned S_OK"
  // is not "the page is there": with the asynchronous real host, entering Content
  // here let the very next goBack() run before the navigation was committed, and
  // the runtime answered CanGoBack=0 (the first product run that drove
  // back/forward, 2026-10-22). The synchronous probe host never reports pending,
  // so both hosts agree on what Content means; only the timing differs.
  if (host_->navigationPending()) return;
  // The browser exists and the page it was sent to has arrived. Content means "a
  // page is loaded on a live host", and the host's own source is reported as the
  // URL when it has one -- so a redirect is visible instead of being overwritten
  // with what was typed.
  state_ = LogsPanelState::Content;
  counters_.state = state_;
  counters_.canGoBack = host_->canGoBack();
  counters_.canGoForward = host_->canGoForward();
  counters_.url = host_->currentUrl().empty() ? url_ : host_->currentUrl();
}

void LogsPanelController::layout() {
  const LogsPanelBounds panel = panelBounds();
  const LogsToolbarPlan toolbar = planLogsToolbar(panel, dpiScale_);
  const LogsPanelBounds content = toolbar.valid ? toolbar.content : LogsPanelBounds{};
  if (content_.x != content.x || content_.y != content.y || content_.w != content.w
      || content_.h != content.h || content_.visible != content.visible) {
    content_ = content;
    if (host_ && counters_.hostAlive) {
      countHostCall();
      host_->setBounds(content_);
    }
  }
}

void LogsPanelController::setInitialWidth(int requestedWidth) {
  // Applied before the first layout so --logstf-width is the panel's width and
  // not a value that only takes effect after a drag: the geometry the probe
  // asserts and the geometry a run opens with have to be the same geometry.
  requestedWidth_ = clampLogsPanelWidth(requestedWidth, clientWidth_, dpiScale_);
  layout();
}

bool LogsPanelController::setWidth(int requestedWidth) {
  const int clamped = clampLogsPanelWidth(requestedWidth, clientWidth_, dpiScale_);
  const bool changed = clamped != requestedWidth_;
  requestedWidth_ = clamped;
  // The resize is applied even when the value did not change: the caller asked
  // for a layout pass, and a hidden host would otherwise keep a stale rect.
  layout();
  return changed;
}

bool LogsPanelController::goBack() {
  if (state_ != LogsPanelState::Content || host_ == nullptr || !counters_.hostAlive) return false;
  std::string error;
  countHostCall();
  ++counters_.backCalls;
  const bool moved = host_->goBack(&error);
  counters_.canGoBack = host_->canGoBack();
  counters_.canGoForward = host_->canGoForward();
  counters_.url = host_->currentUrl().empty() ? url_ : host_->currentUrl();
  return moved;
}

bool LogsPanelController::goForward() {
  if (state_ != LogsPanelState::Content || host_ == nullptr || !counters_.hostAlive) return false;
  std::string error;
  countHostCall();
  ++counters_.forwardCalls;
  const bool moved = host_->goForward(&error);
  counters_.canGoBack = host_->canGoBack();
  counters_.canGoForward = host_->canGoForward();
  counters_.url = host_->currentUrl().empty() ? url_ : host_->currentUrl();
  return moved;
}

bool LogsPanelController::reload() {
  if (state_ != LogsPanelState::Content || host_ == nullptr || !counters_.hostAlive) return false;
  std::string error;
  countHostCall();
  ++counters_.reloadCalls;
  return host_->reload(&error);
}

bool LogsPanelController::scrollContent(int deltaY, int* resultingOffset) {
  if (resultingOffset != nullptr) *resultingOffset = 0;
  if (state_ != LogsPanelState::Content || host_ == nullptr || !counters_.hostAlive) return false;
  std::string error;
  char script[160];
  // scrollBy is deliberately NOT used: it animates in some builds and the
  // offset read back would be a moving target. scrollTo with an absolute target
  // computed by the page itself is one synchronous step.
  std::snprintf(script, sizeof(script),
    "(function(){var y=(window.scrollY||document.documentElement.scrollTop||0)+(%d);"
    "window.scrollTo(0,y);return String(window.scrollY||document.documentElement.scrollTop||0);})()",
    deltaY);
  std::string result;
  countHostCall();
  ++counters_.scrollCalls;
  ++counters_.scriptCalls;
  if (!host_->runScript(script, &result, &error)) {
    ++counters_.scriptFailures;
    return false;
  }
  if (resultingOffset != nullptr) *resultingOffset = std::atoi(result.c_str());
  return true;
}

bool LogsPanelController::pageExtent(int* scrollHeight, int* viewportHeight) {
  if (scrollHeight != nullptr) *scrollHeight = 0;
  if (viewportHeight != nullptr) *viewportHeight = 0;
  if (state_ != LogsPanelState::Content || host_ == nullptr || !counters_.hostAlive) return false;
  std::string error;
  const char* script =
      "(function(){return String(document.documentElement.scrollHeight||0)+','+"
      "String(window.innerHeight||0);})()";
  std::string result;
  countHostCall();
  ++counters_.scriptCalls;
  if (!host_->runScript(script, &result, &error)) {
    ++counters_.scriptFailures;
    return false;
  }
  const std::size_t comma = result.find(',');
  if (comma == std::string::npos) return false;
  if (scrollHeight != nullptr) *scrollHeight = std::atoi(result.substr(0, comma).c_str());
  if (viewportHeight != nullptr) *viewportHeight = std::atoi(result.substr(comma + 1).c_str());
  return true;
}

LogsPanelBounds LogsPanelController::panelBounds() const {
  return planLogsPanel(state_ != LogsPanelState::Closed, requestedWidth_,
    clientWidth_, clientHeight_, dpiScale_);
}

LogsPanelBounds LogsPanelController::contentBounds() const {
  const LogsPanelBounds panel = panelBounds();
  const LogsToolbarPlan toolbar = planLogsToolbar(panel, dpiScale_);
  return toolbar.valid ? toolbar.content : LogsPanelBounds{};
}

LogsPanelSnapshot LogsPanelController::snapshot() const {
  LogsPanelSnapshot result = counters_;
  result.state = state_;
  result.url = url_;
  result.lastRefusal = lastRefusal_;
  result.unavailable = unavailable_;
  result.fallback = runtime_.fallback;
  result.hostAlive = counters_.hostAlive;
  result.width = panelBounds().w;
  if (host_ != nullptr && counters_.hostAlive) {
    result.canGoBack = host_->canGoBack();
    result.canGoForward = host_->canGoForward();
  }
  return result;
}

// ---------------------------------------------------------------------------
// Frame-time readings
// ---------------------------------------------------------------------------
//
// The ring keeps the newest kMaxSamples intervals. Counters are derived from
// the live window rather than accumulated: an accumulated max or sum would go
// on describing samples the ring has already dropped, and a p95 that quietly
// includes a 4-second startup stall from ten minutes ago is worse than no p95.
void FrameTimeHistogram::clear() {
  next_ = 0;
  count_ = 0;
}

void FrameTimeHistogram::add(double milliseconds) {
  if (!(milliseconds >= 0.0)) return;  // NaN and negative intervals are not samples
  samples_[next_] = milliseconds;
  next_ = (next_ + 1) % kMaxSamples;
  if (count_ < kMaxSamples) ++count_;
}

namespace {
// Nearest-rank percentile over a sorted copy. rank = ceil(p * n), 1-based,
// clamped into [1, n]: one sample has p50 == p95 == max == that sample.
double percentile(const std::vector<double>& sorted, double fraction) {
  if (sorted.empty()) return 0.0;
  double rank = std::ceil(fraction * static_cast<double>(sorted.size()));
  if (rank < 1.0) rank = 1.0;
  if (rank > static_cast<double>(sorted.size())) rank = static_cast<double>(sorted.size());
  return sorted[static_cast<std::size_t>(rank) - 1];
}

std::vector<double> sortedWindow(const double* samples, std::size_t count) {
  std::vector<double> sorted(samples, samples + count);
  std::sort(sorted.begin(), sorted.end());
  return sorted;
}
} // namespace

double FrameTimeHistogram::mean() const {
  if (count_ == 0) return 0.0;
  double total = 0.0;
  for (std::size_t index = 0; index < count_; ++index) total += samples_[index];
  return total / static_cast<double>(count_);
}

double FrameTimeHistogram::p50() const {
  return percentile(sortedWindow(samples_, count_), 0.50);
}

double FrameTimeHistogram::p95() const {
  return percentile(sortedWindow(samples_, count_), 0.95);
}

double FrameTimeHistogram::maxMs() const {
  double maximum = 0.0;
  for (std::size_t index = 0; index < count_; ++index) {
    if (samples_[index] > maximum) maximum = samples_[index];
  }
  return maximum;
}

std::string FrameTimeHistogram::summary(const char* window) const {
  char buffer[256];
  std::snprintf(buffer, sizeof(buffer),
    "window=%s frames=%zu p50=%.2f p95=%.2f max=%.2f mean=%.2f",
    window == nullptr ? "?" : window, count_, p50(), p95(), maxMs(), mean());
  return std::string(buffer);
}

} // namespace tf2::native
