#include "logstf_viewer.h"

#include <algorithm>
#include <cstdio>
#include <limits>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

// WHY THIS FILE EXISTS
//   logstf-check.sh needs a forward reading and mutations that can turn it red.
//   The panel's contract has four halves and only one of them needs a browser:
//
//     * the URL policy -- which addresses reach Navigate at all;
//     * the empty state -- that opening the panel reaches the host zero times;
//     * the geometry -- that resize, scroll and the toolbar stay inside the
//       panel at every size and dpi;
//     * the failure states -- that a missing runtime and a failed host both
//       produce a NAMED reason and a NAMED fallback rather than a blank frame.
//
//   Everything above is driven here against a recording fake host. That is the
//   point: "the empty panel navigated nowhere" is then a reading about the
//   product's own controller (logstf_viewer.cpp is compiled into both), not
//   about a copy of the rule kept in the probe.
//
//   The one thing this probe does NOT prove is that WebView2 pixels appear in
//   the panel. That is a product-level observation and it is reported
//   separately by the gate (see the logstf: section of the product trace); the
//   probe reports the runtime answer it can see, and says NOT VERIFIED rather
//   than inferring a picture from a controller state.

namespace {

std::uint32_t g_failures = 0;

void check(bool ok, const char* what) {
  if (!ok) {
    ++g_failures;
    std::cerr << "FAIL " << what << "\n";
  }
}

struct Readings {
  std::vector<std::pair<std::string, std::string>> entries;
  void set(const std::string& key, const std::string& value) {
    for (auto& entry : entries) {
      if (entry.first == key) { entry.second = value; return; }
    }
    entries.emplace_back(key, value);
  }
  void set(const std::string& key, long long value) { set(key, std::to_string(value)); }
  std::string line() const {
    std::string result;
    for (const auto& entry : entries) {
      if (!result.empty()) result += ' ';
      result += entry.first + '=' + entry.second;
    }
    return result;
  }
};

// A host that records everything and answers from a script the test sets up.
// "The panel called nothing" is only meaningful if calls would have been
// observable, so every method here counts and every counter is reported.
class RecordingHost : public tf2::native::ILogsViewHost {
public:
  tf2::native::HostStatus state = tf2::native::HostStatus::Ready;
  bool startResult = true;
  std::string startError;
  std::string failureText;
  bool historyBack = false;
  bool historyForward = false;
  std::string source = "https://logs.tf/1";
  std::string scriptResult = "0";
  bool scriptOk = true;
  int pumpBeforeReady = 0;

  int starts = 0;
  int navigations = 0;
  int backs = 0;
  int forwards = 0;
  int reloads = 0;
  int bounds = 0;
  int destroys = 0;
  int scripts = 0;
  std::vector<std::string> navigatedUrls;
  std::string lastScript;

  bool start(void*, const tf2::native::LogsPanelBounds&, const std::string&,
      std::string* error) override {
    ++starts;
    if (!startResult && error) *error = startError;
    if (!startResult) state = tf2::native::HostStatus::Failed;
    return startResult;
  }
  tf2::native::HostStatus status() const override {
    if (state == tf2::native::HostStatus::Creating) {
      // A host that never becomes ready is a real failure mode (a browser
      // process that dies at startup); "pump does nothing and stays Loading"
      // has to be observable, so the test flips state itself.
      return tf2::native::HostStatus::Creating;
    }
    return state;
  }
  std::string failureReason() const override { return failureText; }
  bool navigate(const std::string& url, std::string*) override {
    ++navigations;
    navigatedUrls.push_back(url);
    source = url;
    return true;
  }
  bool goBack(std::string*) override { ++backs; return historyBack; }
  bool goForward(std::string*) override { ++forwards; return historyForward; }
  bool reload(std::string*) override { ++reloads; return true; }
  bool canGoBack() const override { return historyBack; }
  bool canGoForward() const override { return historyForward; }
  std::string currentUrl() const override { return source; }
  bool runScript(const std::string& script, std::string* result, std::string*) override {
    ++scripts;
    lastScript = script;
    if (!scriptOk) return false;
    if (result) *result = scriptResult;
    return true;
  }
  void setBounds(const tf2::native::LogsPanelBounds&) override { ++bounds; }
  // destroy() drops the browser; it does NOT change what the NEXT start() will
  // answer. A real host recreates a browser on the next start(), so the fake
  // keeps its scripted status too -- otherwise "reopen" would be modelled as
  // "a host that never becomes ready again", which is a different defect.
  void destroy() override { ++destroys; }
  const char* kind() const override { return "webview2"; }
};

tf2::native::LogsRuntimeInfo availableRuntime() {
  tf2::native::LogsRuntimeInfo info;
  info.available = true;
  info.kind = "webview2";
  info.version = "155.0.4283.62";
  info.source = "registry";
  info.reason.clear();
  return info;
}

tf2::native::LogsRuntimeInfo missingRuntime() {
  tf2::native::LogsRuntimeInfo info;
  info.available = false;
  info.kind = "none";
  info.source = "absent";
  info.reason = "no Microsoft Edge WebView2 runtime is registered on this host";
  return info;
}

} // namespace

int main() {
  using namespace tf2::native;
  Readings readings;

  // --- 1. URL policy --------------------------------------------------------
  //
  // The accepted shapes, then every refusal class. Each refusal is asserted by
  // REASON, not only by "not accepted": a run that refused "javascript:" as
  // "host-not-allowed" would be refusing for the wrong cause, and the message
  // shown to the user would then name the wrong problem.
  struct AcceptedCase { const char* input; const char* url; const char* host; bool upgraded; };
  const AcceptedCase accepted[] = {
    {"https://logs.tf/1", "https://logs.tf/1", "logs.tf", false},
    {"https://logs.tf/", "https://logs.tf/", "logs.tf", false},
    {"https://logs.tf", "https://logs.tf/", "logs.tf", false},
    {"https://logs.tf/1234", "https://logs.tf/1234", "logs.tf", false},
    {"https://logs.tf/log/1234", "https://logs.tf/log/1234", "logs.tf", false},
    {"https://logs.tf/logs/1234", "https://logs.tf/logs/1234", "logs.tf", false},
    {"https://logs.tf/match/998", "https://logs.tf/match/998", "logs.tf", false},
    {"https://logs.tf/player/76561198000000000", "https://logs.tf/player/76561198000000000", "logs.tf", false},
    {"https://www.logs.tf/7", "https://www.logs.tf/7", "www.logs.tf", false},
    {"HTTPS://LOGS.TF/9", "https://LOGS.TF/9", "logs.tf", false},
    {"  https://logs.tf/11  ", "https://logs.tf/11", "logs.tf", false},
    {"https://logs.tf/5?tab=chat", "https://logs.tf/5?tab=chat", "logs.tf", false},
    {"https://logs.tf/5#summary", "https://logs.tf/5", "logs.tf", false},
    {"http://logs.tf/3", "https://logs.tf/3", "logs.tf", true},
    {"https://logs.tf:443/4", "https://logs.tf/4", "logs.tf", false},
  };
  int acceptedOk = 0;
  for (const auto& item : accepted) {
    const LogsUrlDecision decision = validateLogsUrl(item.input);
    const bool ok = decision.verdict == LogsUrlVerdict::Accepted
        && decision.url == item.url
        && decision.host == item.host
        && decision.upgradedToHttps == item.upgraded
        && decision.reason == LogsUrlReason::None
        && !decision.message.empty();
    if (!ok) {
      std::cerr << "FAIL accepted case: " << item.input << " -> "
                << logsUrlVerdictName(decision.verdict) << "/"
                << logsUrlReasonName(decision.reason) << " url=" << decision.url << "\n";
      ++g_failures;
    } else {
      ++acceptedOk;
    }
  }
  check(acceptedOk == static_cast<int>(sizeof(accepted) / sizeof(accepted[0])),
    "every accepted URL shape is accepted verbatim");
  readings.set("urlAcceptCases", static_cast<long long>(sizeof(accepted) / sizeof(accepted[0])));
  readings.set("urlAccepted", acceptedOk);

  struct RefusedCase { const char* input; LogsUrlReason reason; };
  const RefusedCase refused[] = {
    {"", LogsUrlReason::Missing},
    {"   ", LogsUrlReason::Missing},
    {"logs.tf/1", LogsUrlReason::MissingScheme},
    {"//logs.tf/1", LogsUrlReason::MissingScheme},
    {"javascript:alert(1)", LogsUrlReason::UnsupportedScheme},
    {"file:///C:/Windows/System32/drivers/etc/hosts", LogsUrlReason::UnsupportedScheme},
    {"data:text/html,<script>alert(1)</script>", LogsUrlReason::UnsupportedScheme},
    {"about:blank", LogsUrlReason::UnsupportedScheme},
    {"ftp://logs.tf/1", LogsUrlReason::UnsupportedScheme},
    {"ms-settings:privacy", LogsUrlReason::UnsupportedScheme},
    {"https://", LogsUrlReason::MissingHost},
    {"https:///1", LogsUrlReason::MissingHost},
    {"https://user:pass@logs.tf/1", LogsUrlReason::UserInfoRefused},
    {"https://logs.tf@evil.example/1", LogsUrlReason::UserInfoRefused},
    {"https://logs.tf\\1", LogsUrlReason::Malformed},
    {"https://logs.tf:8443/1", LogsUrlReason::PortNotAllowed},
    {"https://logs.tf:443abc/1", LogsUrlReason::Malformed},
    {"https://evil.example/1", LogsUrlReason::HostNotAllowed},
    {"https://logs.tf.evil.example/1", LogsUrlReason::HostNotAllowed},
    {"https://evillogs.tf/1", LogsUrlReason::HostNotAllowed},
    {"https://logs.tf./1", LogsUrlReason::Malformed},
    {"https://sub.logs.tf/1", LogsUrlReason::HostNotAllowed},
    {"https://127.0.0.1/1", LogsUrlReason::HostNotAllowed},
    {"https://logs.tf/../../etc/passwd", LogsUrlReason::Malformed},
    {"https://logs.tf/admin", LogsUrlReason::PathNotSupported},
    {"https://logs.tf/a/b/c", LogsUrlReason::PathNotSupported},
  };
  int refusedOk = 0;
  for (const auto& item : refused) {
    const LogsUrlDecision decision = validateLogsUrl(item.input);
    const bool ok = decision.verdict == LogsUrlVerdict::Refused
        && decision.reason == item.reason
        && decision.url.empty()
        && !decision.message.empty();
    if (!ok) {
      std::cerr << "FAIL refused case: '" << item.input << "' -> "
                << logsUrlVerdictName(decision.verdict) << "/"
                << logsUrlReasonName(decision.reason) << " wanted "
                << logsUrlReasonName(item.reason) << "\n";
      ++g_failures;
    } else {
      ++refusedOk;
    }
  }
  check(refusedOk == static_cast<int>(sizeof(refused) / sizeof(refused[0])),
    "every refused URL names the right reason and carries no URL");
  readings.set("urlRefuseCases", static_cast<long long>(sizeof(refused) / sizeof(refused[0])));
  readings.set("urlRefused", refusedOk);

  // A non-ASCII homograph must not become logs.tf through a normalisation
  // step, and a control byte must not travel into a request line.
  {
    const std::string cyrillic = "https://logs.tf\xd0\xb0/1";  // a Cyrillic 'a' after the dot
    const LogsUrlDecision homograph = validateLogsUrl(cyrillic);
    check(homograph.verdict == LogsUrlVerdict::Refused
        && homograph.reason == LogsUrlReason::UnsupportedCharacter,
      "a non-ASCII homograph is refused before any host comparison");
    const std::string control = std::string("https://logs.tf/1\r\nHost: evil.example");
    const LogsUrlDecision injected = validateLogsUrl(control);
    check(injected.verdict == LogsUrlVerdict::Refused,
      "a header-injection attempt is refused");
    // The host allow-list itself, independent of any URL that exercises it.
    check(isAllowedLogsHost("logs.tf") && isAllowedLogsHost("www.logs.tf")
        && isAllowedLogsHost("LOGS.TF"),
      "the allow-list accepts logs.tf in any case");
    check(!isAllowedLogsHost("logs.tf.evil.example") && !isAllowedLogsHost("logs.tf ")
        && !isAllowedLogsHost("logs.tf.") && !isAllowedLogsHost(".logs.tf")
        && !isAllowedLogsHost("logs") && !isAllowedLogsHost("tf"),
      "the allow-list refuses lookalikes, trailing dots and bare labels");
    readings.set("homographRefused", homograph.verdict == LogsUrlVerdict::Refused ? 1LL : 0LL);
    readings.set("injectionRefused", injected.verdict == LogsUrlVerdict::Refused ? 1LL : 0LL);
  }

  // --- 2. The empty panel ---------------------------------------------------
  //
  // The load-bearing claim of T54: an open panel with no imported URL does
  // nothing at all. Not "shows an empty page", not "loads a helpful default" --
  // zero host calls, zero navigations, zero browser creation. A regression that
  // prefetched anything would move hostCalls or navigations off zero, and both
  // are reported so the failure names which one.
  {
    RecordingHost host;
    LogsPanelController controller(&host, availableRuntime());
    check(controller.state() == LogsPanelState::Closed, "the panel starts closed");
    check(!controller.snapshot().hostAlive, "no host is alive before the panel opens");
    check(controller.open(), "opening an empty panel succeeds");
    check(controller.state() == LogsPanelState::Empty, "an opened panel with no URL is Empty");
    // Several frames of pumping: the product calls pump() every frame, and an
    // eager start would show up here even if open() itself were clean.
    for (int frame = 0; frame < 8; ++frame) controller.pump();
    const LogsPanelSnapshot empty = controller.snapshot();
    check(empty.hostCalls == 0, "an empty panel makes no host call");
    check(empty.navigations == 0, "an empty panel navigates nowhere");
    check(empty.hostStarts == 0 && host.starts == 0, "an empty panel creates no browser");
    check(empty.url.empty(), "an empty panel has no address");
    check(empty.lastRefusal.empty(), "an empty panel has no refusal to show");
    check(empty.state == LogsPanelState::Empty, "the empty state survives pumping");
    readings.set("emptyHostCalls", static_cast<long long>(empty.hostCalls));
    readings.set("emptyNavigations", static_cast<long long>(empty.navigations));
    readings.set("emptyState", logsPanelStateName(empty.state));

    // Close and reopen: still empty, still zero. This is the "closed then
    // reopened" acceptance step at the controller level, and the counters are
    // cumulative so "zero after a reopen" cannot be a reset artefact.
    check(controller.close(), "closing an open panel succeeds");
    check(controller.state() == LogsPanelState::Closed, "closing returns to Closed");
    check(controller.open(), "reopening succeeds");
    for (int frame = 0; frame < 4; ++frame) controller.pump();
    const LogsPanelSnapshot reopened = controller.snapshot();
    check(reopened.state == LogsPanelState::Empty, "a reopened panel is Empty again");
    check(reopened.hostCalls == 0 && reopened.navigations == 0,
      "a reopened empty panel still makes no host call");
    check(reopened.opens == 2 && reopened.closes == 1,
      "open and close are counted");
    readings.set("reopenHostCalls", static_cast<long long>(reopened.hostCalls));
    readings.set("reopenState", logsPanelStateName(reopened.state));

    // A refusal while empty must leave the panel empty -- not show an error
    // page, not navigate to a search engine, not keep the bad address.
    check(!controller.importUrl("https://evil.example/1"), "a wrong-host import is refused");
    const LogsPanelSnapshot afterRefusal = controller.snapshot();
    check(afterRefusal.state == LogsPanelState::Empty, "a refused import leaves the panel Empty");
    check(afterRefusal.hostCalls == 0 && afterRefusal.navigations == 0,
      "a refused import makes no host call");
    check(!afterRefusal.lastRefusal.empty(), "a refused import records its reason");
    check(afterRefusal.refusedImports == 1, "the refusal is counted");
    check(afterRefusal.url.empty(), "a refused import does not become the address");
    readings.set("refusalKeepsEmpty", afterRefusal.state == LogsPanelState::Empty ? 1LL : 0LL);
    readings.set("refusalHasReason", afterRefusal.lastRefusal.empty() ? 0LL : 1LL);
  }

  // --- 3. A valid URL, a live host, and the browser controls ---------------
  {
    RecordingHost host;
    host.state = HostStatus::Creating;
    LogsPanelController controller(&host, availableRuntime());
    controller.open();
    check(controller.importUrl("https://logs.tf/1234"), "a valid logs.tf URL is accepted");
    check(controller.snapshot().state == LogsPanelState::Loading,
      "an accepted URL waits for the browser (Loading)");
    check(host.starts == 0, "the browser is created by pump(), not by importUrl");
    controller.pump();
    check(host.starts == 1, "one pump creates the browser exactly once");
    check(controller.snapshot().state == LogsPanelState::Loading,
      "a browser still starting keeps the panel Loading");
    check(host.navigations == 0, "nothing is navigated before the browser is ready");
    host.state = HostStatus::Ready;
    for (int frame = 0; frame < 4; ++frame) controller.pump();
    const LogsPanelSnapshot loaded = controller.snapshot();
    check(loaded.state == LogsPanelState::Content, "a ready browser lands on Content");
    check(loaded.navigations == 1, "exactly one navigation happened");
    check(host.navigatedUrls.size() == 1 && host.navigatedUrls[0] == "https://logs.tf/1234",
      "the navigated URL is the validated canonical form");
    check(loaded.hostAlive, "the host is reported alive");
    check(host.starts == 1, "a second pump does not restart the browser");
    readings.set("contentState", logsPanelStateName(loaded.state));
    readings.set("contentNavigations", static_cast<long long>(loaded.navigations));

    // Back / forward / reload exist only once a page is loaded, and each
    // reaches the host exactly once. The ability answer comes from the host, so
    // "the button is disabled" and "the page has no history" stay distinct.
    host.historyBack = true;
    host.historyForward = true;
    check(controller.goBack(), "back reports the host's move");
    check(controller.goForward(), "forward reports the host's move");
    check(controller.reload(), "reload reports the host's answer");
    const LogsPanelSnapshot navigated = controller.snapshot();
    check(host.backs == 1 && host.forwards == 1 && host.reloads == 1,
      "each control reached the host exactly once");
    check(navigated.backCalls == 1 && navigated.forwardCalls == 1 && navigated.reloadCalls == 1,
      "back/forward/reload are each counted");
    check(navigated.canGoBack && navigated.canGoForward,
      "the snapshot carries the host's history answer");
    readings.set("backCalls", static_cast<long long>(navigated.backCalls));
    readings.set("forwardCalls", static_cast<long long>(navigated.forwardCalls));
    readings.set("reloadCalls", static_cast<long long>(navigated.reloadCalls));

    // Closing drops the browser but keeps the address, and reopening restores
    // it with exactly one navigation -- the page is not refetched per frame.
    check(controller.close(), "closing a loaded panel succeeds");
    check(host.destroys == 1, "closing destroyed the browser");
    check(controller.snapshot().state == LogsPanelState::Closed, "a closed panel reports Closed");
    check(controller.open(), "reopening a loaded panel succeeds");
    check(controller.snapshot().state == LogsPanelState::Loading,
      "a reopen of a loaded panel restores through Loading, not Content");
    for (int frame = 0; frame < 4; ++frame) controller.pump();
    const LogsPanelSnapshot reopened = controller.snapshot();
    check(reopened.state == LogsPanelState::Content, "the reopened panel lands on Content again");
    check(reopened.navigations == 2, "the reopen navigated exactly once more");
    check(host.starts == 2, "the reopen created exactly one more browser");
    readings.set("reopenNavigations", static_cast<long long>(reopened.navigations));
    readings.set("reopenState", logsPanelStateName(reopened.state));
  }

  // --- 4. Failure states ----------------------------------------------------
  //
  // Both failure modes produce a NAMED reason and a NAMED fallback. A blank
  // panel with no explanation is what "silently failed" looks like on screen,
  // so the assertions are about the text, not about a boolean.
  {
    RecordingHost host;
    LogsPanelController controller(&host, missingRuntime());
    controller.open();
    check(controller.importUrl("https://logs.tf/42"),
      "a valid URL is still accepted when no runtime exists");
    const LogsPanelSnapshot noRuntime = controller.snapshot();
    check(noRuntime.state == LogsPanelState::Unavailable,
      "a valid URL without a runtime reports Unavailable, not Content");
    check(!noRuntime.unavailable.empty(),
      "the Unavailable state carries the runtime's own explanation");
    check(!noRuntime.fallback.empty(),
      "the Unavailable state carries a named fallback");
    check(noRuntime.url == "https://logs.tf/42",
      "the accepted address stays visible so it can be opened in a browser");
    check(host.starts == 0 && noRuntime.hostCalls == 0,
      "no browser is created when there is no runtime");
    check(noRuntime.navigations == 0, "nothing is navigated without a runtime");
    readings.set("unavailableState", logsPanelStateName(noRuntime.state));
    readings.set("unavailableHasReason", noRuntime.unavailable.empty() ? 0LL : 1LL);
    readings.set("unavailableHasFallback", noRuntime.fallback.empty() ? 0LL : 1LL);

    // A null host (a build compiled without the WebView2 backend) takes the
    // same path: it must not crash and must not claim content.
    LogsPanelController noHost(nullptr, missingRuntime());
    noHost.open();
    check(noHost.importUrl("https://logs.tf/7"), "a null host still validates the URL");
    check(noHost.snapshot().state == LogsPanelState::Unavailable,
      "a null host reports Unavailable");
    check(noHost.snapshot().navigations == 0, "a null host navigates nowhere");
  }

  {
    // A synchronous create failure (loader missing).
    RecordingHost host;
    host.startResult = false;
    host.startError = "WebView2Loader.dll could not be loaded";
    LogsPanelController controller(&host, availableRuntime());
    controller.open();
    controller.importUrl("https://logs.tf/9");
    controller.pump();
    const LogsPanelSnapshot failed = controller.snapshot();
    check(failed.state == LogsPanelState::Unavailable,
      "a browser that cannot be created reports Unavailable");
    check(failed.unavailable.find("WebView2Loader") != std::string::npos,
      "the create failure keeps the host's own error text");
    check(failed.hostFailures == 1, "the create failure is counted");
    check(failed.navigations == 0, "a failed create never navigates");
    check(!failed.fallback.empty(), "a failed create still names the fallback");
    readings.set("createFailureReason", failed.unavailable);

    // An asynchronous failure (runtime started and then died): the reason comes
    // from failureReason(), and repeated pumps do not multiply the failure.
    RecordingHost asyncHost;
    asyncHost.state = HostStatus::Creating;
    LogsPanelController asyncController(&asyncHost, availableRuntime());
    asyncController.open();
    asyncController.importUrl("https://logs.tf/10");
    asyncController.pump();
    asyncHost.state = HostStatus::Failed;
    asyncHost.failureText = "CreateCoreWebView2Environment failed with 0x80070002";
    asyncController.pump();
    asyncController.pump();
    const LogsPanelSnapshot asyncFailed = asyncController.snapshot();
    check(asyncFailed.state == LogsPanelState::Unavailable,
      "an asynchronous browser failure reports Unavailable");
    check(asyncFailed.unavailable == asyncHost.failureText,
      "the asynchronous failure keeps the runtime's HRESULT text");
    check(asyncFailed.hostFailures == 1, "the asynchronous failure is counted once");
    check(asyncFailed.navigations == 0, "a failed browser never navigates");
    readings.set("asyncFailureReason", asyncFailed.unavailable);
  }

  // --- 5. Geometry: resize, toolbar, content -------------------------------
  //
  // Every client size in the matrix, at 96/120/144/192 dpi. The four rules:
  // the panel stays inside the client, it never takes the whole client while
  // the demo can keep its minimum width, the toolbar controls stay inside the
  // panel and do not overlap, and the content rect sits under the toolbar.
  {
    const int clientSizes[][2] = {
      {1920, 1080}, {1280, 720}, {1024, 768}, {800, 600}, {640, 480},
      {512, 400}, {400, 300}, {320, 240},
    };
    const float scales[] = {1.0f, 1.25f, 1.5f, 2.0f};
    int cases = 0;
    int outside = 0;
    int overlapped = 0;
    int noContent = 0;
    int demoSqueezed = 0;
    int unreachableControls = 0;
    for (const auto& size : clientSizes) {
      for (const float scale : scales) {
        const int clientWidth = size[0];
        const int clientHeight = size[1];
        // Both ends of the resize range plus the middle: a drag to the far
        // left, a drag to the far right, and the default.
        const int requests[] = {1, 100000, (clientWidth / 2)};
        for (const int request : requests) {
          ++cases;
          const LogsPanelBounds panel = planLogsPanel(true, request, clientWidth, clientHeight, scale);
          if (!panel.visible || panel.x < 0 || panel.y < 0
              || panel.x + panel.w > clientWidth || panel.y + panel.h > clientHeight) {
            ++outside;
            continue;
          }
          const LogsToolbarPlan toolbar = planLogsToolbar(panel, scale);
          if (!toolbar.valid || !toolbar.content.visible || toolbar.content.h <= 0) {
            ++noContent;
        continue;
          }
          if (toolbar.content.x < panel.x || toolbar.content.x + toolbar.content.w > panel.x + panel.w
              || toolbar.content.y < panel.y || toolbar.content.y + toolbar.content.h > panel.y + panel.h
              || toolbar.content.y < toolbar.rects[0].y + toolbar.rects[0].h) {
            ++outside;
          }
          // The panel's minimum width (kMinLogsPanelWidthDip) is what makes all
          // six controls fit; a client too narrow to give the panel its minimum
          // is a degenerate size where the planner clips, on purpose. The
          // reachability assertion is therefore stated where it is meaningful:
          // at or above the minimum width the planner advertises.
          const int minimumUsable = static_cast<int>(
              static_cast<float>(kMinLogsPanelWidthDip) * scale + 0.5f);
          const bool controlsMustFit = panel.w >= minimumUsable;
          for (std::size_t index = 0; index < kLogsToolbarControlCount; ++index) {
            const LogsPanelBounds& rect = toolbar.rects[index];
            if (!rect.visible) { if (controlsMustFit) ++unreachableControls; continue; }
            if (rect.x < panel.x || rect.x + rect.w > panel.x + panel.w) ++outside;
            for (std::size_t other = index + 1; other < kLogsToolbarControlCount; ++other) {
              const LogsPanelBounds& b = toolbar.rects[other];
              if (!b.visible) continue;
              const bool separated = rect.x + rect.w <= b.x || b.x + b.w <= rect.x
                  || rect.y + rect.h <= b.y || b.y + b.h <= rect.y;
              if (!separated) ++overlapped;
            }
          }
          // The demo keeps its minimum width whenever the client is wide enough
          // to hold both minimums in physical pixels.
          const int minDemo = static_cast<int>(static_cast<float>(kMinDemoWidthDip) * scale + 0.5f);
          if (clientWidth >= minDemo + static_cast<int>(static_cast<float>(kMinLogsPanelWidthDip) * scale + 0.5f)) {
            if (clientWidth - panel.w < minDemo) ++demoSqueezed;
          }
        }
      }
    }
    check(outside == 0, "the panel and its toolbar always stay inside the client");
    check(overlapped == 0, "no two toolbar controls overlap");
    check(noContent == 0, "every panel size leaves a content rect for the page");
    check(demoSqueezed == 0, "the demo keeps its minimum width whenever the client can hold both");
    check(unreachableControls == 0, "every toolbar control is reachable at every size");
    readings.set("geometryCases", cases);
    readings.set("geometryOutside", outside);
    readings.set("geometryOverlaps", overlapped);
    readings.set("geometryNoContent", noContent);
    readings.set("geometryDemoSqueezed", demoSqueezed);
    readings.set("geometryUnreachable", unreachableControls);

    // A closed panel occupies nothing at all.
    const LogsPanelBounds closed = planLogsPanel(false, 600, 1280, 720, 1.0f);
    check(!closed.visible && closed.w == 0 && closed.h == 0, "a closed panel has no geometry");
    // Resizing is a clamp, not a refusal: dragging past either edge lands on
    // the edge, and the panel is still usable there.
    check(clampLogsPanelWidth(-500, 1920, 1.0f) == kMinLogsPanelWidthDip,
      "dragging past the left edge clamps to the minimum width");
    const int wide = clampLogsPanelWidth(100000, 1920, 1.0f);
    check(wide == 1920 - kMinDemoWidthDip,
      "dragging past the right edge clamps to the demo's minimum");
    check(clampLogsPanelWidth(640, 1920, 1.0f) == 640, "a width inside the range is kept");
    readings.set("clampMin", clampLogsPanelWidth(-500, 1920, 1.0f));
    readings.set("clampMax", wide);
  }

  // --- 6. Scroll seam -------------------------------------------------------
  //
  // Scrolling is measured through the host's script seam: the controller sends
  // a scroll and reads the offset the page took. A host that cannot evaluate
  // reports the failure instead of a made-up zero, and "content shorter than
  // the viewport" is visible as an offset that does not move.
  {
    RecordingHost host;
    LogsPanelController controller(&host, availableRuntime());
    controller.open();
    controller.importUrl("https://logs.tf/1234");
    controller.pump();
    check(controller.snapshot().state == LogsPanelState::Content, "the scroll case has content");

    host.scriptResult = "480";
    int offset = 0;
    check(controller.scrollContent(480, &offset), "a scroll reaches the page");
    check(host.scripts == 1, "the scroll used exactly one script call");
    check(offset == 480, "the offset the page reported is returned, not the requested delta");
    check(host.lastScript.find("scrollTo") != std::string::npos,
      "the scroll script uses an absolute target, not an animated delta");
    check(controller.snapshot().scrollCalls == 1, "the scroll is counted");
    readings.set("scrollOffset", offset);

    // A page shorter than its viewport cannot scroll: the browser clamps, so
    // the controller reports the clamped value instead of claiming success.
    host.scriptResult = "0";
    int clamped = -1;
    check(controller.scrollContent(900, &clamped), "a clamped scroll is still a completed call");
    check(clamped == 0, "a page that cannot scroll reports an unchanged offset");
    readings.set("scrollClamped", clamped);

    // A host that cannot run script surfaces the failure and counts it.
    host.scriptOk = false;
    int failedOffset = 5;
    check(!controller.scrollContent(100, &failedOffset), "a script failure is reported as a failure");
    check(failedOffset == 0, "a failed scroll reports no offset rather than a stale one");
    check(controller.snapshot().scriptFailures == 1, "the script failure is counted");
    readings.set("scrollFailures", static_cast<long long>(controller.snapshot().scriptFailures));

    // Extent: two numbers from one round trip, so "the page is taller than the
    // pane" is a reading rather than an assumption.
    host.scriptOk = true;
    host.scriptResult = "4200,720";
    int scrollHeight = 0;
    int viewportHeight = 0;
    check(controller.pageExtent(&scrollHeight, &viewportHeight), "the page extent is readable");
    check(scrollHeight == 4200 && viewportHeight == 720,
      "the extent reports both the page height and the viewport height");
    readings.set("scrollHeight", scrollHeight);
    readings.set("viewportHeight", viewportHeight);

    // Scrolling an empty panel does nothing at all.
    LogsPanelController empty(&host, availableRuntime());
    empty.open();
    int emptyOffset = 77;
    check(!empty.scrollContent(100, &emptyOffset), "an empty panel cannot scroll");
    check(host.scripts == 3, "an empty panel issues no script");
  }

  // --- 7. The runtime probe ------------------------------------------------
  //
  // Whatever this host answers, the answer must be self-consistent: available
  // implies a version and a source, unavailable implies a reason and a
  // fallback. The gate asserts the same line against the machine it runs on, so
  // a host where the runtime disappears is a FAIL rather than a silent skip.
  {
    const LogsRuntimeInfo runtime = probeLogsRuntime();
    check(!runtime.fallback.empty(), "the runtime probe always names a fallback");
    check(runtime.kind == "webview2" || runtime.kind == "none",
      "the runtime kind is one of the two named values");
    if (runtime.available) {
      check(!runtime.version.empty(), "an available runtime reports a version");
      check(!runtime.source.empty() && runtime.source != "absent",
        "an available runtime reports where it was found");
      check(runtime.reason.empty(), "an available runtime carries no failure reason");
    } else {
      check(!runtime.reason.empty(), "a missing runtime explains itself");
      check(runtime.version.empty(), "a missing runtime has no version to report");
    }
    readings.set("runtimeKind", runtime.kind);
    readings.set("runtimeAvailable", runtime.available ? 1LL : 0LL);
    readings.set("runtimeSource", runtime.source);
    readings.set("runtimeVersion", runtime.version.empty() ? "-" : runtime.version);
  }

  // --- 7b. The runtime decision, in every state this machine can be in -------
  //
  // The missing-runtime path cannot be exercised by uninstalling anything, so
  // it is exercised here: decideLogsRuntime is pure, and the four states it can
  // be handed are the four a real host can produce. What must hold in EACH is
  // that the answer names itself -- available implies a version and a source,
  // unavailable implies a reason and a fallback -- because "no runtime" is the
  // one state that must never read as a blank panel with no explanation.
  {
    LogsRuntimeInputs registered;
    registered.registryVersionHklmWow64 = "155.0.4283.62";
    const LogsRuntimeInfo available = decideLogsRuntime(registered);
    check(available.available && available.kind == "webview2"
        && available.version == "155.0.4283.62" && available.source == "registry"
        && available.reason.empty(), "a registered runtime is reported with its version");

    // The WOW64 entry is preferred, but any of the three views answers.
    LogsRuntimeInputs hkcuOnly;
    hkcuOnly.registryVersionHkcu = "120.0.2210.91";
    const LogsRuntimeInfo perUser = decideLogsRuntime(hkcuOnly);
    check(perUser.available && perUser.version == "120.0.2210.91" && perUser.source == "registry",
      "a per-user registration is accepted");

    LogsRuntimeInputs directoryOnly;
    directoryOnly.installDirectories.push_back({"154.0.1.0", true});
    directoryOnly.installDirectories.push_back({"155.0.4283.62", true});
    const LogsRuntimeInfo fromDirectory = decideLogsRuntime(directoryOnly);
    check(fromDirectory.available && fromDirectory.source == "installation-dir"
        && fromDirectory.version == "155.0.4283.62",
      "an unregistered but installed runtime is found in the installation directory");
    readings.set("runtimeDirFallback", fromDirectory.source);

    // A half-install is named as one, not read as a runtime.
    LogsRuntimeInputs halfInstalled;
    halfInstalled.installDirectories.push_back({"155.0.4283.62", false});
    const LogsRuntimeInfo half = decideLogsRuntime(halfInstalled);
    check(!half.available, "a version directory with no browser is not a runtime");
    check(half.reason.find("half-installed") != std::string::npos,
      "a half-installed runtime is named as half-installed");
    check(!half.fallback.empty(), "a half-installed runtime still names the fallback");
    readings.set("runtimeHalfInstallNamed", half.reason.find("half-installed") != std::string::npos ? 1LL : 0LL);

    // Nothing at all: the state the task says must never fail silently.
    LogsRuntimeInputs nothing;
    const LogsRuntimeInfo absent = decideLogsRuntime(nothing);
    check(!absent.available && absent.kind == "none" && absent.source == "absent",
      "no runtime at all is reported as absent");
    check(!absent.reason.empty(), "an absent runtime explains itself");
    check(absent.reason.find("WebView2") != std::string::npos,
      "the absent-runtime reason names the runtime it looked for");
    check(!absent.fallback.empty(), "an absent runtime names the fallback");
    check(absent.version.empty(), "an absent runtime has no version to report");
    readings.set("runtimeAbsentHasReason", absent.reason.empty() ? 0LL : 1LL);
    readings.set("runtimeAbsentHasFallback", absent.fallback.empty() ? 0LL : 1LL);

    // And the controller's behaviour on that answer: the fallback reaches the
    // snapshot a UI reads, and the address survives it.
    RecordingHost host;
    LogsPanelController controller(&host, absent);
    controller.open();
    controller.importUrl("https://logs.tf/77");
    const LogsPanelSnapshot snapshot = controller.snapshot();
    check(snapshot.state == LogsPanelState::Unavailable, "an absent runtime yields Unavailable");
    check(snapshot.unavailable == absent.reason, "the controller shows the probe's own reason");
    check(snapshot.fallback == absent.fallback, "the controller shows the probe's own fallback");
    check(snapshot.fallback.find("system browser") != std::string::npos,
      "the fallback names the concrete alternative (open it in the system browser)");
    check(snapshot.url == "https://logs.tf/77", "the address stays visible for the fallback");
    check(host.navigations == 0, "an absent runtime navigates nowhere");
    readings.set("absentRuntimeNavigations", static_cast<long long>(host.navigations));
  }

  // --- 8. Frame-time readings ---------------------------------------------
  //
  // The responsiveness claim is a comparison of two windows of the same run.
  // The histogram has to be exact enough that the comparison means something,
  // so its percentiles are pinned on a synthetic series whose answers are
  // known by construction.
  {
    FrameTimeHistogram histogram;
    check(histogram.count() == 0, "a fresh histogram has no samples");
    for (int index = 1; index <= 100; ++index) {
      histogram.add(static_cast<double>(index));
    }
    check(histogram.count() == 100, "100 samples were recorded");
    check(histogram.p50() == 50.0, "p50 of 1..100 is 50 (nearest rank)");
    check(histogram.p95() == 95.0, "p95 of 1..100 is 95 (nearest rank)");
    check(histogram.maxMs() == 100.0, "max of 1..100 is 100");
    check(histogram.mean() == 50.5, "the mean of 1..100 is 50.5");
    FrameTimeHistogram single;
    single.add(8.25);
    check(single.p50() == 8.25 && single.p95() == 8.25 && single.maxMs() == 8.25,
      "one sample is its own p50, p95 and max");
    FrameTimeHistogram rejected;
    rejected.add(-1.0);
    // quiet_NaN rather than 0.0/0.0: MSVC rejects a constant division by zero
    // at compile time (C2124), and the point is the runtime value, not the
    // expression that produces it.
    rejected.add(std::numeric_limits<double>::quiet_NaN());
    check(rejected.count() == 0, "negative and NaN intervals are not samples");
    // The ring keeps the newest samples and its readings describe THAT window:
    // after 9000 samples of 1.0 ms behind the first 100, every duration the
    // summary reports must be 1.0 -- a max or a p95 that still mentioned the
    // old 100 ms sample would be a number about a window nobody is measuring.
    for (int index = 0; index < 9000; ++index) histogram.add(1.0);
    check(histogram.count() == FrameTimeHistogram::kMaxSamples,
      "the ring stops at its bound instead of growing");
    check(histogram.maxMs() == 1.0, "the max describes only the samples still in the ring");
    check(histogram.p50() == 1.0 && histogram.p95() == 1.0,
      "the percentiles describe only the samples still in the ring");
    check(histogram.mean() == 1.0, "the mean describes only the samples still in the ring");
    readings.set("histogramP50", histogram.summary("probe").find("p50=") != std::string::npos ? 1LL : 0LL);
    readings.set("histogramSamples", static_cast<long long>(histogram.count()));
    std::cout << "hist " << histogram.summary("probe") << "\n";
  }

  // --- 9. Verdict -----------------------------------------------------------
  if (g_failures != 0) {
    std::cout << "FAIL logstf_viewer_probe failures=" << g_failures << "\n";
    return 1;
  }
  std::cout << "PASS logstf_viewer_probe"
               " empty=inert url=validated resize=clamped scroll=measured"
               " runtime=explained\n";
  std::cout << readings.line() << "\n";
  return 0;
}
