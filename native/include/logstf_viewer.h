#pragma once

// T54 -- the embedded logs.tf viewer.
//
// WHAT THIS FILE OWNS
//   The whole contract of the logs panel that can be decided without a browser
//   process: which URLs may be navigated to, what the panel does in each state,
//   how its geometry is planned, what the web-view runtime probe answers, and
//   the frame-time readings that say whether a demo kept playing while the
//   panel was open. The Windows WebView2 backend lives behind ILogsViewHost so
//   the probe can drive the SAME controller the product drives with a recording
//   fake: "the empty panel issued no host call" is then a reading about the
//   product's code path, not about a second copy of it in a probe.
//
// THE THREE PROPERTIES THIS FILE EXISTS TO MAKE MEASURABLE
//   1. An empty panel is inert. Nothing is prefetched, nothing is navigated,
//      no browser process is started: LogsPanelController::open() on an empty
//      address reaches LogsPanelState::Empty with hostCalls() == 0 and
//      navigations() == 0. A "helpful" default page is a mutation this file's
//      probe catches (see logstf-check.sh).
//   2. A URL is validated BEFORE anything is navigated. Only the logs.tf host
//      shape is accepted; every refusal names its reason. User info in a URL
//      is refused outright -- this viewer never carries, invents or replays
//      user credentials, and it has no API to set a header, a cookie or a
//      form field.
//   3. A missing runtime is a named state, not a blank rectangle:
//      LogsRuntimeInfo always carries a reason and a fallback, and the panel
//      shows both.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace tf2::native {

// ---------------------------------------------------------------------------
// URL policy
// ---------------------------------------------------------------------------
//
// The accepted shape is exactly what logs.tf serves:
//
//   https://logs.tf/<id>              a log
//   https://logs.tf/log/<id>          a log (alias form)
//   https://logs.tf/logs/<id>         a log (alias form)
//   https://logs.tf/match/<id>        a match
//   https://logs.tf/player/<steamid>  a player's logs
//   https://logs.tf/                  the front page
//
// "www.logs.tf" is accepted and kept as typed. An http:// URL is REFUSED with
// its own reason (NotHttps) rather than silently upgraded: the acceptance text
// says a non-https address must get an explicit refusal, and "we changed what
// you asked for" is a decision the user did not make. Every other scheme is
// refused too: javascript:, file:, data:, about:, ftp:, ms-*: and a bare
// "logs.tf/1" with no scheme. Non-ASCII input is refused rather than
// percent-encoded, because a homograph host must not become "logs.tf" through a
// normalisation step.
enum class LogsUrlVerdict { Empty, Accepted, Refused };

enum class LogsUrlReason {
  None = 0,
  Missing,              // nothing was typed
  TooLong,              // over kMaxLogsUrlLength
  UnsupportedCharacter, // control or non-ASCII byte
  MissingScheme,        // no "://"
  UnsupportedScheme,    // a scheme other than http/https
  NotHttps,             // http://: the panel opens https only, and says so
  MissingHost,          // "https:///" and friends
  UserInfoRefused,      // "https://user:pass@logs.tf/1"
  Malformed,            // backslashes, empty labels, ".." segments
  PortNotAllowed,       // only the default 443 is accepted
  HostNotAllowed,       // not logs.tf / www.logs.tf (lookalikes, IPs, trailing dot)
  PathNotSupported,     // logs.tf host, but not a log/player/match path
};

struct LogsUrlDecision {
  LogsUrlVerdict verdict = LogsUrlVerdict::Empty;
  LogsUrlReason reason = LogsUrlReason::None;
  // Canonical URL to navigate to. Empty unless verdict == Accepted.
  std::string url;
  // Lower-cased host. Empty unless verdict == Accepted.
  std::string host;
  // True when an http:// URL was accepted and rewritten to https://.
  bool upgradedToHttps = false;
  // One sentence naming the outcome. Always non-empty: a refusal without a
  // reason is what "rejected it, no idea why" looks like on screen.
  std::string message;
};

constexpr std::size_t kMaxLogsUrlLength = 2048;

// True for exactly "logs.tf" and "www.logs.tf" (case-insensitive). Kept
// separate so the probe can pin the host rule itself instead of inferring it
// from whichever URL happened to be accepted.
bool isAllowedLogsHost(const std::string& host);

LogsUrlDecision validateLogsUrl(const std::string& input);

const char* logsUrlVerdictName(LogsUrlVerdict verdict);
const char* logsUrlReasonName(LogsUrlReason reason);

// ---------------------------------------------------------------------------
// Geometry: the panel is docked to the right edge and resized by its left edge.
// ---------------------------------------------------------------------------
//
// Physical pixels in, physical pixels out; the caller multiplies DIPs by the
// window's dpi scale, so a 150% display gets 1.5x the panel. The three rules
// the probe pins over a size matrix: the panel never leaves the client, the
// panel never takes the whole client while the demo still has a minimum width
// to draw in, and a requested width is clamped rather than refused (dragging
// past the edge stops at the edge, it does not break the layout).
struct LogsPanelBounds {
  int x = 0;
  int y = 0;
  int w = 0;
  int h = 0;
  bool visible = false;
};

constexpr int kMinLogsPanelWidthDip = 320;
constexpr int kMinDemoWidthDip = 320;
constexpr int kLogsPanelSplitterDip = 6;

// The width a drag lands on: clamped to [min, client - min demo width].
int clampLogsPanelWidth(int requested, int clientWidth, float dpiScale);

LogsPanelBounds planLogsPanel(bool open, int requestedWidth,
    int clientWidth, int clientHeight, float dpiScale);

// The toolbar row inside the panel: back / forward / reload / address field /
// import / close, plus the content rect the web view occupies underneath.
enum class LogsToolbarControl { Back = 0, Forward, Reload, Address, Import, Close, Count };
constexpr std::size_t kLogsToolbarControlCount = 6;

struct LogsToolbarPlan {
  LogsPanelBounds rects[kLogsToolbarControlCount]{};
  LogsPanelBounds content{};
  bool valid = false;
};

LogsToolbarPlan planLogsToolbar(const LogsPanelBounds& panel, float dpiScale);

// ---------------------------------------------------------------------------
// Web-view runtime probe
// ---------------------------------------------------------------------------
//
// Answers "is there a web-view runtime on this Windows installation?" from the
// documented registry locations only -- no network, no credential store, no
// COM. The WebView2 Evergreen runtime registers itself under
//   HKLM | HKCU \ SOFTWARE \ Microsoft \ EdgeUpdate \ Clients \
//       {F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}
// (WOW64 view included) and a system-wide install also leaves
//   %ProgramFiles(x86)%\Microsoft\EdgeWebView\Application\<version>
// so the two are reported apart: "the runtime says it is installed" and
// "the runtime's files are where the loader will look" fail differently.
struct LogsRuntimeInfo {
  bool available = false;
  // "webview2" when the Evergreen runtime was found, "none" otherwise.
  std::string kind = "none";
  // Version string from the registry, or the installation directory's version.
  std::string version;
  // Where the answer came from: "registry" | "installation-dir" | "absent".
  std::string source = "absent";
  // Non-empty when !available: why the runtime could not be used.
  std::string reason;
  // ALWAYS non-empty. The named fallback the panel shows instead of a blank
  // frame: open the same URL in the system browser, and keep the address and
  // the reason visible in the panel.
  std::string fallback =
      "open the same logs.tf URL in the system browser (the panel keeps the address and this reason visible)";
};

// The inputs the environment supplies, separated from the decision so the
// missing-runtime path is TESTABLE: a probe that could only be exercised by
// uninstalling the runtime on the machine running the gate would never be
// exercised at all, and "no runtime" is precisely the state that must never
// fail silently.
struct LogsRuntimeInputs {
  // The Evergreen client's "pv" for each registry view, in the order they are
  // consulted. Empty means that view had no usable value.
  std::string registryVersionHklmWow64;
  std::string registryVersionHklm;
  std::string registryVersionHkcu;
  // The version subdirectories found under the installation directory, and
  // whether each one actually holds msedgewebview2.exe. A directory with no
  // browser is a half-install and is reported as one.
  struct InstallDirectory {
    std::string version;
    bool hasBrowser = false;
  };
  std::vector<InstallDirectory> installDirectories;
};

// The decision, pure: no registry, no filesystem, no COM. probeLogsRuntime()
// is this function applied to what this machine reports.
LogsRuntimeInfo decideLogsRuntime(const LogsRuntimeInputs& inputs);

// Reads this machine and calls decideLogsRuntime.
LogsRuntimeInfo probeLogsRuntime();

// ---------------------------------------------------------------------------
// Web-view host seam
// ---------------------------------------------------------------------------
//
// The product implements this on WebView2; the probe implements it as a
// recorder. Creation is asynchronous because the real one is: the controller
// reports HostStatus::Creating until the browser process answers, and the
// panel's state machine has to be able to say "loading" in between and to
// surface a failure with the runtime's own HRESULT rather than hanging.
enum class HostStatus { Idle, Creating, Ready, Failed };

class ILogsViewHost {
public:
  virtual ~ILogsViewHost() = default;

  // Begin creating the browser inside "parent". Returns false only for a
  // synchronous refusal; async failure arrives through status().
  virtual bool start(void* parentWindow, const LogsPanelBounds& content,
      const std::string& dataDirectory, std::string* error) = 0;
  virtual HostStatus status() const = 0;
  // Non-empty when status() == Failed: the runtime's own words (HRESULT text).
  virtual std::string failureReason() const = 0;

  virtual bool navigate(const std::string& url, std::string* error) = 0;
  // True while the browser has been told to go somewhere and has not finished.
  // A real browser is asynchronous, so "Navigate returned S_OK" is not "the page
  // is there": treating it as Content made goBack() run before the navigation had
  // been committed, and the runtime answered CanGoBack=0. A synchronous host (the
  // probe's recording fake) returns false here, which is the same meaning.
  virtual bool navigationPending() const = 0;
  virtual bool goBack(std::string* error) = 0;
  virtual bool goForward(std::string* error) = 0;
  virtual bool reload(std::string* error) = 0;
  virtual bool canGoBack() const = 0;
  virtual bool canGoForward() const = 0;
  virtual std::string currentUrl() const = 0;
  // Run a script in the page and return its value as text. This is how
  // "scrolling works" becomes a measurement instead of an assertion about a
  // scrollbar's presence: the same seam drives window.scrollBy and reads
  // window.scrollY back. A host that cannot evaluate reports the error.
  virtual bool runScript(const std::string& script, std::string* result, std::string* error) = 0;
  virtual void setBounds(const LogsPanelBounds& content) = 0;
  virtual void destroy() = 0;

  // "webview2" or "none"; the panel reports it so a run cannot silently look
  // the same with and without a runtime.
  virtual const char* kind() const = 0;
};

// ---------------------------------------------------------------------------
// Panel state machine
// ---------------------------------------------------------------------------
enum class LogsPanelState {
  Closed,       // the user has not opened the panel; nothing exists
  Empty,        // open, no URL imported: no host, no navigation, no prefetch
  Loading,      // a URL was accepted and the host is starting/navigating
  Content,      // a URL is loaded and the host is alive
  Unavailable,  // a URL was accepted but there is no usable runtime
};

const char* logsPanelStateName(LogsPanelState state);

struct LogsPanelSnapshot {
  LogsPanelState state = LogsPanelState::Closed;
  std::string url;            // the address bar text (accepted URL only)
  std::string lastRefusal;    // what the user is told about the last refusal
  // The same refusal as a machine-readable token from logsUrlReasonName(), so a
  // gate can assert "it refused for THIS reason" without matching English prose.
  // T54's gate asserted refusal=host-not-allowed against a field that only ever
  // held the sentence, so that assertion could never pass (GATE-DESIGN-TRAPS #1).
  std::string lastRefusalReason;
  std::string unavailable;    // runtime explanation while Unavailable
  std::string fallback;       // the named fallback while Unavailable
  bool canGoBack = false;
  bool canGoForward = false;
  bool hostAlive = false;
  int width = 0;

  // Call counters. "hostCalls" is every method invoked on the host interface;
  // "navigations" counts only navigate() calls. Both are 0 for an empty panel,
  // and that is the empty-state assertion.
  std::uint32_t hostCalls = 0;
  std::uint32_t navigations = 0;
  std::uint32_t backCalls = 0;
  std::uint32_t forwardCalls = 0;
  std::uint32_t reloadCalls = 0;
  std::uint32_t resizeCalls = 0;
  std::uint32_t acceptedImports = 0;
  std::uint32_t refusedImports = 0;
  std::uint32_t opens = 0;
  std::uint32_t closes = 0;
  std::uint32_t scrollCalls = 0;
  std::uint32_t scriptCalls = 0;
  std::uint32_t scriptFailures = 0;
  std::uint32_t hostStarts = 0;
  std::uint32_t hostFailures = 0;
};

class LogsPanelController {
public:
  // "host" is borrowed; the controller never deletes it. Passing nullptr is a
  // supported state (the "no runtime compiled in" build) and every operation
  // then reports Unavailable instead of pretending to load a page.
  LogsPanelController(ILogsViewHost* host, LogsRuntimeInfo runtime);

  void setParentWindow(void* parent) { parent_ = parent; }
  void setDataDirectory(std::string directory) { dataDirectory_ = std::move(directory); }
  void setClientSize(int width, int height, float dpiScale);
  void setContentBounds(const LogsPanelBounds& content);

  bool open();
  bool close();
  bool toggle();

  // The address bar / --logstf-url entry point. Validates first; a refusal
  // leaves the panel exactly as it was (empty stays empty) and records the
  // reason. Returns true when the URL passed policy -- whether a runtime could
  // then load it is what state() answers (Loading/Content vs Unavailable), so
  // "accepted but no runtime" cannot be read as a refused URL.
  bool importUrl(const std::string& raw);

  bool goBack();
  bool goForward();
  bool reload();

  // Scrolling and page extent through the host's script seam. "resultingOffset"
  // is window.scrollY after the scroll, so a caller cannot report success
  // without the offset the page actually took (a clamped scroll reports the
  // clamp, not the request).
  bool scrollContent(int deltaY, int* resultingOffset);
  bool pageExtent(int* scrollHeight, int* viewportHeight);

  // The splitter's drop point and the scripted resize both land here: one
  // clamping implementation, so "resizable" is a property of the panel and not
  // of whichever caller asked.
  bool setWidth(int requestedWidth);

  // The startup width (--logstf-width). Same clamp as a drag, applied before
  // the first layout so the width a run opens with is the width geometry was
  // asserted against.
  void setInitialWidth(int requestedWidth);

  // Advance asynchronous work (host creation). Called once per frame by the
  // product and explicitly by the probe.
  void pump();

  // Push the current content rect to a live host and apply a pending navigate.
  void layout();

  LogsPanelState state() const { return state_; }
  LogsPanelSnapshot snapshot() const;
  LogsPanelBounds panelBounds() const;
  LogsPanelBounds contentBounds() const;

private:
  void countHostCall() { ++counters_.hostCalls; }

  ILogsViewHost* host_ = nullptr;
  LogsRuntimeInfo runtime_{};
  void* parent_ = nullptr;
  std::string dataDirectory_;

  LogsPanelState state_ = LogsPanelState::Closed;
  std::string url_;
  std::string addressText_;   // what the user typed, kept for the address field
  std::string lastRefusal_;
  std::string unavailable_;

  int clientWidth_ = 1280;
  int clientHeight_ = 720;
  float dpiScale_ = 1.0f;
  int requestedWidth_ = kMinLogsPanelWidthDip * 2;  // two DIPs wide by default
  LogsPanelBounds content_{};

  // Counters live in their own snapshot so snapshot() can compose state onto
  // them: the counters outlive a close/reopen, which is what lets a gate read
  // "reopened, and still zero navigations" from one object.
  LogsPanelSnapshot counters_{};
  bool pendingNavigate_ = false;
};

// ---------------------------------------------------------------------------
// Frame-time readings
// ---------------------------------------------------------------------------
//
// The responsiveness claim ("the demo keeps playing while the panel is open")
// needs a number, not an opinion: the product feeds every frame interval into
// this histogram and prints one summary line per window. The ring is bounded so
// a long run cannot grow without limit, and percentiles use the nearest-rank
// definition so the probe can pin exact values on a synthetic series instead of
// comparing floating point against a guess.
class FrameTimeHistogram {
public:
  static constexpr std::size_t kMaxSamples = 8192;

  void clear();
  void add(double milliseconds);
  std::size_t count() const { return count_; }
  double p50() const;
  double p95() const;
  double maxMs() const;
  double mean() const;
  // "window=open frames=120 p50=8.31 p95=9.90 max=21.03"
  std::string summary(const char* window) const;

private:
  double samples_[kMaxSamples]{};
  std::size_t next_ = 0;
  std::size_t count_ = 0;
};

} // namespace tf2::native
