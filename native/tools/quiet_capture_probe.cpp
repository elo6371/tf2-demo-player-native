// quiet_capture_probe.cpp -- reads one normal-mode run and one
// --hidden-window --audio-disabled run of tf2_demo_native (trace + metrics +
// bitmap) and reports the quiet-capture contract as one key=value line.
//
// WHY THIS EXISTS
//   The capture gates must be able to run without a visible window and without
//   opening an audio device, and the frame they produce must be the same
//   frame. "The window did not appear" and "no sound was heard" are not
//   readings, so both states are taken from the product itself: the window
//   state is its own IsWindowVisible() answer and the audio state is a
//   process-wide count of waveOutOpen calls, which is incremented at the call
//   site so a call that *failed* still counts as having been made. This probe
//   is the reader of those artifacts; it never runs the program.
//
// TWO PAIRS, BECAUSE ONE PAIR CANNOT ANSWER BOTH QUESTIONS
//   A pair of runs can be held to the same tick, or it can be a run in which
//   audio really happens. It cannot be both: audio only advances while playback
//   is running, and a running playback reaches a given tick after a wall-clock
//   interval that differs between the two runs, so every clock-derived field
//   (elapsed_seconds, fps, rendered_frames, the tick at which the capture
//   fires, the number of trace lines) is free to differ.
//
//   --mode pinned  the pair was launched with --start-paused, so the tick is
//                  pinned by construction in both runs. This is the pair that
//                  can be held to *equality*: the same tick, the same frame,
//                  the same metrics row, the same trace. The audio accounting
//                  is not exercised here (a paused scheduler does not advance),
//                  so it is not asserted either; the readings are still
//                  reported, at 0.
//   --mode live    the pair ran in real time to a capture tick. This is the
//                  pair in which the scheduler advances and the device is
//                  really asked for, so it is where the audio contract is
//                  asserted: the quiet run's waveOutOpen count is 0, it played
//                  nothing, it skipped exactly the events the normal run
//                  played, and both runs close their own accounting. Equality
//                  is not asserted on clock-derived fields; what is asserted is
//                  that both runs drew their captured frame at or after the
//                  requested tick. The bitmap comparison still runs here and
//                  still reads 1 -- it says the pixels were unchanged by
//                  turning the window and the device off -- but "the same tick
//                  gives the same frame" is the *pinned* pair's claim, because
//                  in a live pair the two runs draw the captured frame at
//                  whichever tick the capture condition first holds.
//
// WHAT IT COMPARES, AND WHAT IT REFUSES TO COMPARE
//   Trace lines are normalized before comparison: the per-line millisecond
//   prefix is stripped (traceCheckpoint carries it, traceFmt does not, and it
//   differs between runs), numeric millisecond tokens inside a line are
//   dropped (a "17.3ms" is how long this machine took, not what was loaded),
//   and the lines that name wall-clock time or frame counts (loop / metrics /
//   draw) are dropped along with the `mode:` lines (whose job is to differ in
//   the window/audio fields; they are asserted field by field instead). In
//   pinned mode every remaining line must match in order. In live mode the two
//   regions that are independent of the iteration count are compared: the load
//   prefix before the first `scene:` block, and the last `scene:` block -- the
//   one belonging to the captured frame -- with the tick values in it replaced
//   by `?`, since the capture fires at the first iteration at or after the
//   requested tick and that iteration is not the same number in both runs. The
//   count of compared lines is always reported, so "nothing compared" cannot
//   read as success.
//
// USAGE
//   quiet_capture_probe --mode pinned|live --capture-tick <n> \
//     --normal-trace <f> --quiet-trace <f> \
//     --normal-metrics <f> --quiet-metrics <f> \
//     --normal-bmp <f> --quiet-bmp <f>
// EXIT: 0 = every check held; 1 = at least one check failed; 2 = usage.

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace {

enum class PairMode { pinned, live };

struct Options {
  PairMode mode = PairMode::pinned;
  bool modeGiven = false;
  long long captureTick = -1;
  std::string normalTrace;
  std::string quietTrace;
  std::string normalMetrics;
  std::string quietMetrics;
  std::string normalBmp;
  std::string quietBmp;
};

void usage(const char* program) {
  std::fprintf(stderr,
    "usage: %s --mode pinned|live --capture-tick <n>"
    " --normal-trace <f> --quiet-trace <f> --normal-metrics <f>"
    " --quiet-metrics <f> --normal-bmp <f> --quiet-bmp <f>\n", program);
}

bool readFile(const std::string& path, std::string& out) {
  std::ifstream input(path, std::ios::binary);
  if (!input) return false;
  std::ostringstream buffer;
  buffer << input.rdbuf();
  out = buffer.str();
  return true;
}

std::vector<std::string> splitLines(const std::string& text) {
  std::vector<std::string> lines;
  std::string current;
  for (const char c : text) {
    if (c == '\n') {
      if (!current.empty() && current.back() == '\r') current.pop_back();
      lines.push_back(current);
      current.clear();
    } else {
      current.push_back(c);
    }
  }
  if (!current.empty()) {
    if (current.back() == '\r') current.pop_back();
    lines.push_back(current);
  }
  return lines;
}

std::vector<std::string> splitDelimiter(const std::string& text, char delimiter) {
  std::vector<std::string> parts;
  std::string current;
  for (const char c : text) {
    if (c == delimiter) { parts.push_back(current); current.clear(); }
    else current.push_back(c);
  }
  parts.push_back(current);
  return parts;
}

bool startsWith(const std::string& text, const char* prefix) {
  const std::size_t length = std::strlen(prefix);
  return text.size() >= length && std::memcmp(text.data(), prefix, length) == 0;
}

// traceCheckpoint() lines carry a "<ms> " prefix ("12.3 scene: done") and the
// number differs between runs; the text after it does not. traceFmt() lines
// have no prefix. Stripping the prefix keeps checkpoint content comparable.
std::string stripTimestamp(const std::string& line) {
  std::size_t i = 0;
  while (i < line.size() && std::isdigit(static_cast<unsigned char>(line[i]))) ++i;
  if (i == 0 || i >= line.size() || line[i] != '.') return line;
  std::size_t j = i + 1;
  while (j < line.size() && std::isdigit(static_cast<unsigned char>(line[j]))) ++j;
  if (j == i + 1 || j >= line.size() || line[j] != ' ') return line;
  return line.substr(j + 1);
}

bool excludedFromComparison(const std::string& line) {
  return startsWith(line, "loop ")     // iteration counter and tick
    || startsWith(line, "metrics ")    // elapsed seconds and fps
    || startsWith(line, "draw: ")      // frame counts and pending state
    || startsWith(line, "mode:");      // asserted field by field, allowed to differ
}

bool isMillisecondToken(const std::string& token) {
  if (token.size() < 3 || token.compare(token.size() - 2, 2, "ms") != 0) return false;
  bool dotSeen = false;
  for (std::size_t i = 0; i + 2 < token.size(); ++i) {
    const char c = token[i];
    if (c == '.' && !dotSeen) { dotSeen = true; continue; }
    if (!std::isdigit(static_cast<unsigned char>(c))) return false;
  }
  return true;
}

// "gpu: model 3/52 1024x1024 17.3ms uploaded=1" -- the millisecond token is how
// long this machine took to upload, not what was uploaded, and it differs
// between two runs of the same command. Dropped; the rest of the line must
// still match exactly.
std::string stripTimingTokens(const std::string& line) {
  std::istringstream stream(line);
  std::string out, token;
  bool first = true;
  while (stream >> token) {
    if (isMillisecondToken(token)) continue;
    if (!first) out.push_back(' ');
    out += token;
    first = false;
  }
  return out;
}

// A decimal integer, sign included: sceneTick is negative on this path
// (firstPacketTick + tick, where the first packet's tick is the index's
// "no tick yet" sentinel), so a digits-only test would leave it unmasked.
bool isIntegerValue(const std::string& value) {
  if (value.empty()) return false;
  std::size_t i = (value[0] == '-' || value[0] == '+') ? 1 : 0;
  if (i >= value.size()) return false;
  for (; i < value.size(); ++i) {
    if (!std::isdigit(static_cast<unsigned char>(value[i]))) return false;
  }
  return true;
}

// "scene: begin tick=601 sceneTick=-2147483047" -- which tick a live run
// happens to have reached when the capture fires is a clock outcome, not a
// render fact, so in live mode the two values are masked out and compared as
// "?" while the rest of the line still has to match. The values themselves are
// reported as readings instead.
std::string maskTickValues(const std::string& line) {
  std::istringstream stream(line);
  std::string out, token;
  bool first = true;
  while (stream >> token) {
    const std::size_t equals = token.find('=');
    if (equals != std::string::npos) {
      const std::string key = token.substr(0, equals);
      if ((key == "tick" || key == "sceneTick") && isIntegerValue(token.substr(equals + 1))) {
        token = key + "=?";
      }
    }
    if (!first) out.push_back(' ');
    out += token;
    first = false;
  }
  return out;
}

struct Readings {
  bool startupFound = false;
  bool summaryFound = false;
  std::map<std::string, std::string> startup;
  std::map<std::string, std::string> summary;
};

void parseKeyValues(const std::string& line, std::map<std::string, std::string>& out) {
  std::istringstream stream(line);
  std::string token;
  while (stream >> token) {
    const std::size_t equals = token.find('=');
    if (equals == std::string::npos || equals == 0) continue;   // "mode:" / "summary"
    out[token.substr(0, equals)] = token.substr(equals + 1);
  }
}

Readings parseTrace(const std::string& text) {
  Readings readings;
  for (const std::string& raw : splitLines(text)) {
    const std::string line = stripTimestamp(raw);
    if (startsWith(line, "mode: summary ")) {
      readings.summaryFound = true;
      parseKeyValues(line, readings.summary);
    } else if (startsWith(line, "mode: window_created=")) {
      readings.startupFound = true;
      parseKeyValues(line, readings.startup);
    }
  }
  return readings;
}

long long number(const std::map<std::string, std::string>& values, const char* key) {
  const auto found = values.find(key);
  if (found == values.end()) return -1;
  try {
    return std::stoll(found->second);
  } catch (...) {
    return -1;
  }
}

std::string text(const std::map<std::string, std::string>& values, const char* key) {
  const auto found = values.find(key);
  return found == values.end() ? std::string() : found->second;
}

// A metrics field that is missing, empty or not a number reads as -1 rather
// than throwing: the checks below compare against real tick and frame counts,
// and -1 fails every one of them.
long long toNumber(const std::string& value) {
  if (value.empty()) return -1;
  try {
    return std::stoll(value);
  } catch (...) {
    return -1;
  }
}

// The tick named by the last "draw: begin tick=<n> frames=<f>" line: the frame
// the capture was taken from. Every line in a run carries its tick, but only
// this one names the tick of the frame that was written to disk.
long long lastDrawTick(const std::string& traceText) {
  long long tick = -1;
  for (const std::string& raw : splitLines(traceText)) {
    const std::string line = stripTimestamp(raw);
    if (!startsWith(line, "draw: begin tick=")) continue;
    std::size_t i = std::strlen("draw: begin tick=");
    const std::size_t start = i;
    while (i < line.size() && std::isdigit(static_cast<unsigned char>(line[i]))) ++i;
    if (i == start) continue;
    try {
      tick = std::stoll(line.substr(start, i - start));
    } catch (...) {
      tick = -1;
    }
  }
  return tick;
}

struct Csv {
  bool parsed = false;
  std::vector<std::string> header;
  std::vector<std::string> lastRow;
};

Csv parseCsv(const std::string& text) {
  Csv csv;
  std::vector<std::string> rows;
  for (const std::string& line : splitLines(text)) {
    if (!line.empty()) rows.push_back(line);
  }
  if (rows.empty()) return csv;
  csv.header = splitDelimiter(rows.front(), ',');
  if (rows.size() >= 2) {
    csv.lastRow = splitDelimiter(rows.back(), ',');
    csv.parsed = true;
  }
  return csv;
}

std::string csvField(const Csv& csv, const std::string& name) {
  const auto index = std::find(csv.header.begin(), csv.header.end(), name);
  if (index == csv.header.end()) return std::string();
  const std::size_t at = static_cast<std::size_t>(index - csv.header.begin());
  if (at >= csv.lastRow.size()) return std::string();
  return csv.lastRow[at];
}

bool fieldEqual(const Csv& a, const Csv& b, const std::string& name) {
  const std::string left = csvField(a, name);
  return !left.empty() && left == csvField(b, name);
}

struct Checks {
  int total = 0;
  int failed = 0;
  std::string firstFail;
  void add(bool ok, const std::string& name) {
    ++total;
    if (!ok) {
      ++failed;
      if (firstFail.empty()) firstFail = name;
    }
  }
};

// The `mode:` fields whose values are allowed to differ between the two runs
// (window and audio state); everything else in the two startup lines must
// agree, and the summary fields that describe the frame and the run's
// completion must agree as well. `drawn` is deliberately not in the summary
// list: it counts every frame until the capture completes, so it is a clock
// reading in a live pair. Its own checks below are per-side, and in pinned mode
// it is compared exactly.
const char* kStartupMayDiffer[] = {"window_visible", "hidden", "audio_mode"};
const char* kSummaryMustEqual[] = {"window_created", "capture_written", "metrics_written", "exit"};

bool differOnlyInAllowed(
    const std::map<std::string, std::string>& normal,
    const std::map<std::string, std::string>& quiet,
    const char* const* allowed, std::size_t allowedCount) {
  std::vector<std::string> keys;
  for (const auto& entry : normal) keys.push_back(entry.first);
  for (const auto& entry : quiet) {
    if (normal.find(entry.first) == normal.end()) keys.push_back(entry.first);
  }
  for (const std::string& key : keys) {
    const bool mayDiffer = std::any_of(allowed, allowed + allowedCount,
      [&](const char* name) { return key == name; });
    if (mayDiffer) continue;
    if (text(normal, key.c_str()) != text(quiet, key.c_str())) return false;
  }
  return true;
}

// The lines one trace offers for comparison, under the mode's scope.
struct TraceScope {
  std::vector<std::string> lines;
  std::size_t prefix = 0;
  std::size_t tail = 0;
};

std::vector<std::string> comparableLines(const std::string& traceText) {
  std::vector<std::string> lines;
  for (const std::string& raw : splitLines(traceText)) {
    const std::string normalized = stripTimingTokens(stripTimestamp(raw));
    if (!excludedFromComparison(normalized)) lines.push_back(normalized);
  }
  return lines;
}

// The load prefix: everything before the first scene block. It does not depend
// on how far the run got.
std::size_t prefixEnd(const std::vector<std::string>& lines) {
  for (std::size_t i = 0; i < lines.size(); ++i) {
    if (startsWith(lines[i], "scene: begin")) return i;
  }
  return lines.size();
}

// The last run of consecutive `scene:` lines: the block belonging to the frame
// the capture was taken from.
void lastSceneBlock(const std::vector<std::string>& lines, std::size_t& first, std::size_t& last) {
  long long beginIndex = -1;
  for (std::size_t i = 0; i < lines.size(); ++i) {
    if (startsWith(lines[i], "scene: begin")) beginIndex = static_cast<long long>(i);
  }
  if (beginIndex < 0) { first = lines.size(); last = lines.size(); return; }
  std::size_t at = static_cast<std::size_t>(beginIndex);
  while (at + 1 < lines.size() && startsWith(lines[at + 1], "scene: ")) ++at;
  first = static_cast<std::size_t>(beginIndex);
  last = at;
}

TraceScope buildScope(const std::vector<std::string>& lines, PairMode mode) {
  TraceScope scope;
  if (mode == PairMode::pinned) {
    scope.lines = lines;
    return scope;
  }
  const std::size_t end = prefixEnd(lines);
  scope.lines.assign(lines.begin(), lines.begin() + static_cast<std::ptrdiff_t>(end));
  scope.prefix = scope.lines.size();
  std::size_t first = 0, last = 0;
  lastSceneBlock(lines, first, last);
  if (first < lines.size()) {
    for (std::size_t i = first; i <= last; ++i) scope.lines.push_back(maskTickValues(lines[i]));
    scope.tail = last - first + 1;
  }
  return scope;
}

} // namespace

int main(int argc, char** argv) {
  Options options;
  for (int i = 1; i < argc; ++i) {
    const std::string flag = argv[i];
    const auto next = [&](std::string& target) { if (i + 1 < argc) target = argv[++i]; };
    if (flag == "--normal-trace") next(options.normalTrace);
    else if (flag == "--quiet-trace") next(options.quietTrace);
    else if (flag == "--normal-metrics") next(options.normalMetrics);
    else if (flag == "--quiet-metrics") next(options.quietMetrics);
    else if (flag == "--normal-bmp") next(options.normalBmp);
    else if (flag == "--quiet-bmp") next(options.quietBmp);
    else if (flag == "--mode" && i + 1 < argc) {
      const std::string value = argv[++i];
      if (value == "pinned") { options.mode = PairMode::pinned; options.modeGiven = true; }
      else if (value == "live") { options.mode = PairMode::live; options.modeGiven = true; }
      else { usage(argv[0]); return 2; }
    }
    else if (flag == "--capture-tick" && i + 1 < argc) {
      try {
        options.captureTick = std::stoll(argv[++i]);
      } catch (...) {
        usage(argv[0]);
        return 2;
      }
    }
    else { usage(argv[0]); return 2; }
  }
  // No default mode: a pair that was pinned and a pair that ran live are
  // checked differently, and guessing which one the caller has would silently
  // drop the assertions that make the other one meaningful.
  if (!options.modeGiven || options.captureTick < 0
      || options.normalTrace.empty() || options.quietTrace.empty()
      || options.normalMetrics.empty() || options.quietMetrics.empty()
      || options.normalBmp.empty() || options.quietBmp.empty()) {
    usage(argv[0]);
    return 2;
  }
  const bool live = options.mode == PairMode::live;

  std::string normalTraceText, quietTraceText, normalMetricsText, quietMetricsText;
  std::string normalBmpBytes, quietBmpBytes;
  const bool normalTraceRead = readFile(options.normalTrace, normalTraceText);
  const bool quietTraceRead = readFile(options.quietTrace, quietTraceText);
  const bool normalMetricsRead = readFile(options.normalMetrics, normalMetricsText);
  const bool quietMetricsRead = readFile(options.quietMetrics, quietMetricsText);
  const bool normalBmpRead = readFile(options.normalBmp, normalBmpBytes);
  const bool quietBmpRead = readFile(options.quietBmp, quietBmpBytes);

  Checks checks;
  checks.add(normalTraceRead, "normal_trace_readable");
  checks.add(quietTraceRead, "quiet_trace_readable");
  checks.add(normalMetricsRead && quietMetricsRead, "metrics_readable");
  checks.add(normalBmpRead && quietBmpRead, "bitmaps_readable");

  const Readings normal = parseTrace(normalTraceText);
  const Readings quiet = parseTrace(quietTraceText);
  checks.add(normal.startupFound, "normal_startup_line");
  checks.add(quiet.startupFound, "quiet_startup_line");
  checks.add(normal.summaryFound, "normal_summary_line");
  checks.add(quiet.summaryFound, "quiet_summary_line");

  // Normal side: the control readings of the default launch path.
  checks.add(number(normal.startup, "window_created") == 1, "normal.window_created_eq_1");
  checks.add(number(normal.startup, "window_visible") == 1, "normal.window_visible_eq_1");
  checks.add(number(normal.startup, "hidden") == 0, "normal.hidden_eq_0");
  checks.add(text(normal.startup, "audio_mode") == "device", "normal.audio_mode_device");
  checks.add(number(normal.summary, "drawn") >= 1, "normal.drawn_ge_1");
  checks.add(number(normal.summary, "capture_written") == 1, "normal.capture_written_eq_1");
  checks.add(number(normal.summary, "metrics_written") == 1, "normal.metrics_written_eq_1");
  checks.add(number(normal.summary, "exit") == 0, "normal.exit_eq_0");

  // Quiet side: the contract itself.
  checks.add(number(quiet.startup, "window_created") == 1, "quiet.window_created_eq_1");
  checks.add(number(quiet.startup, "window_visible") == 0, "quiet.window_visible_eq_0");
  checks.add(number(quiet.startup, "hidden") == 1, "quiet.hidden_eq_1");
  checks.add(text(quiet.startup, "audio_mode") == "disabled", "quiet.audio_mode_disabled");
  checks.add(number(quiet.summary, "audio_device_opened") == 0, "quiet.audio_device_opened_eq_0");
  checks.add(number(quiet.summary, "events_played") == 0, "quiet.events_played_eq_0");
  checks.add(number(quiet.summary, "drawn") >= 1, "quiet.drawn_ge_1");
  checks.add(number(quiet.summary, "capture_written") == 1, "quiet.capture_written_eq_1");
  checks.add(number(quiet.summary, "metrics_written") == 1, "quiet.metrics_written_eq_1");
  checks.add(number(quiet.summary, "exit") == 0, "quiet.exit_eq_0");

  // "Quiet mode changes only the window and audio status": the two mode lines
  // may differ in the allowed fields and must agree everywhere else, and the
  // summary fields that describe the frame and completion must be equal.
  checks.add(
    differOnlyInAllowed(normal.startup, quiet.startup,
      kStartupMayDiffer, sizeof(kStartupMayDiffer) / sizeof(kStartupMayDiffer[0])),
    "startup_differs_only_in_window_audio");
  bool summaryMustEqual = true;
  for (const char* key : kSummaryMustEqual) {
    if (text(normal.summary, key) != text(quiet.summary, key)) summaryMustEqual = false;
  }
  checks.add(summaryMustEqual, "summary_core_fields_equal");
  if (!live) {
    checks.add(number(normal.summary, "drawn") == number(quiet.summary, "drawn"),
      "pinned.drawn_equal");
  }

  // The frame itself: byte-identical bitmaps.
  const bool bmpSameSize = normalBmpBytes.size() == quietBmpBytes.size();
  const bool bmpIdentical = normalBmpRead && quietBmpRead
    && !quietBmpBytes.empty() && bmpSameSize
    && std::memcmp(normalBmpBytes.data(), quietBmpBytes.data(), quietBmpBytes.size()) == 0;
  checks.add(bmpIdentical, "bmp_identical");

  const Csv normalCsv = parseCsv(normalMetricsText);
  const Csv quietCsv = parseCsv(quietMetricsText);
  checks.add(normalCsv.parsed && quietCsv.parsed, "metrics_rows_present");
  checks.add(normalCsv.header == quietCsv.header, "metrics_headers_identical");
  const long long normalFrames = toNumber(csvField(normalCsv, "rendered_frames"));
  const long long quietFrames = toNumber(csvField(quietCsv, "rendered_frames"));
  const long long normalLastTick = toNumber(csvField(normalCsv, "tick"));
  const long long quietLastTick = toNumber(csvField(quietCsv, "tick"));
  // A metrics row that never names a frame, or names a tick the run cannot have
  // reached, is not a metrics row this gate should be reading.
  checks.add(normalFrames >= 1 && quietFrames >= 1, "metrics_rows_name_a_frame");
  if (live) {
    checks.add(normalLastTick >= options.captureTick && quietLastTick >= options.captureTick,
      "live.metrics_reached_capture_tick");
  } else {
    // Pinned at a tick: both runs must name that same tick, and the same number
    // of rendered frames, because nothing about a paused run is clock-derived.
    checks.add(normalLastTick == options.captureTick && quietLastTick == options.captureTick,
      "pinned.metrics_tick_is_capture_tick");
    checks.add(fieldEqual(normalCsv, quietCsv, "tick")
        && fieldEqual(normalCsv, quietCsv, "rendered_frames"),
      "pinned.metrics_core_fields_equal");
  }

  // The trace, under the mode's scope.
  const TraceScope normalScope = buildScope(comparableLines(normalTraceText), options.mode);
  const TraceScope quietScope = buildScope(comparableLines(quietTraceText), options.mode);
  long long firstMismatch = -1;
  bool traceIdentical = normalScope.lines.size() == quietScope.lines.size();
  if (traceIdentical) {
    for (std::size_t i = 0; i < normalScope.lines.size(); ++i) {
      if (normalScope.lines[i] != quietScope.lines[i]) {
        traceIdentical = false;
        firstMismatch = static_cast<long long>(i);
        break;
      }
    }
  }
  checks.add(static_cast<long long>(normalScope.lines.size()) >= 1, "trace_lines_compared_ge_1");
  checks.add(traceIdentical, "trace_render_identical");

  // The tick the captured frame was drawn at, read from each trace. The capture
  // fires on the first iteration at or after the requested tick, so this is
  // "at least the tick that was asked for", not "exactly it" -- in live mode
  // the two runs need not land on the same one, and in pinned mode they are
  // both the pinned tick because the tick cannot move.
  const long long normalDrawTick = lastDrawTick(normalTraceText);
  const long long quietDrawTick = lastDrawTick(quietTraceText);
  checks.add(normalDrawTick >= options.captureTick && quietDrawTick >= options.captureTick,
    "capture_tick_reached");
  if (!live) {
    checks.add(normalDrawTick == options.captureTick && quietDrawTick == options.captureTick,
      "pinned.capture_tick_is_requested_tick");
  }

  const long long normalDeviceCalls = number(normal.summary, "audio_device_opened");
  const long long quietDeviceCalls = number(quiet.summary, "audio_device_opened");
  const long long normalSeen = number(normal.summary, "events_seen");
  const long long quietSeen = number(quiet.summary, "events_seen");
  const long long normalPlayed = number(normal.summary, "events_played");
  const long long quietPlayed = number(quiet.summary, "events_played");
  const long long normalSkipped = number(normal.summary, "events_skipped");
  const long long quietSkipped = number(quiet.summary, "events_skipped");
  const long long normalMissing = number(normal.summary, "events_missing");
  const long long quietMissing = number(quiet.summary, "events_missing");

  if (live) {
    // The control that gives the quiet readings their power: on this pair the
    // normal run must really ask the device for a device and really play. Zero
    // on both sides would make "audio_device_opened=0" true for a reason that
    // has nothing to do with the flag.
    checks.add(normalDeviceCalls >= 1, "live.normal_audio_device_opened_ge_1");
    checks.add(normalSeen >= 1, "live.normal_events_seen_ge_1");
    checks.add(normalPlayed >= 1, "live.normal_events_played_ge_1");
    checks.add(quietSeen >= 1, "live.quiet_events_seen_ge_1");
    // The scheduler must have seen the same work with the device off: the
    // timeline is the input, and disabling the output may not change it.
    checks.add(quietSeen == normalSeen, "live.events_seen_equal");
    checks.add(quietMissing == normalMissing, "live.events_missing_equal");
    // The events the normal run played are exactly the events the quiet run
    // skipped: same predicate, the last step differs by policy, and `skipped`
    // is the answer that says so instead of `failed`.
    checks.add(quietSkipped == normalPlayed, "live.events_skipped_eq_normal_played");
    // Both accountings have to close on their own side.
    checks.add(normalSeen == normalPlayed + normalSkipped + normalMissing,
      "live.normal_accounting_closes");
    checks.add(quietSeen == quietPlayed + quietSkipped + quietMissing,
      "live.quiet_accounting_closes");
  }

  // One line, so the gate and a human read the same bytes.
  std::ostringstream out;
  out << "quiet_capture_probe:";
  const auto emit = [&](const char* name, long long value) { out << ' ' << name << '=' << value; };
  const auto emitText = [&](const char* name, const std::string& value) {
    out << ' ' << name << '=' << (value.empty() ? "-" : value);
  };
  out << " mode=" << (live ? "live" : "pinned");
  emit("capture_tick", options.captureTick);
  emit("normal.window_created", number(normal.startup, "window_created"));
  emit("normal.window_visible", number(normal.startup, "window_visible"));
  emit("normal.hidden", number(normal.startup, "hidden"));
  emitText("normal.audio_mode", text(normal.startup, "audio_mode"));
  emit("normal.audio_device_opened", normalDeviceCalls);
  emit("normal.events_seen", normalSeen);
  emit("normal.events_played", normalPlayed);
  emit("normal.events_skipped", normalSkipped);
  emit("normal.events_missing", normalMissing);
  emit("normal.drawn", number(normal.summary, "drawn"));
  emit("normal.capture_written", number(normal.summary, "capture_written"));
  emit("normal.metrics_written", number(normal.summary, "metrics_written"));
  emit("normal.exit", number(normal.summary, "exit"));
  emit("normal.draw_tick", normalDrawTick);
  emit("quiet.window_created", number(quiet.startup, "window_created"));
  emit("quiet.window_visible", number(quiet.startup, "window_visible"));
  emit("quiet.hidden", number(quiet.startup, "hidden"));
  emitText("quiet.audio_mode", text(quiet.startup, "audio_mode"));
  emit("quiet.audio_device_opened", quietDeviceCalls);
  emit("quiet.events_seen", quietSeen);
  emit("quiet.events_played", quietPlayed);
  emit("quiet.events_skipped", quietSkipped);
  emit("quiet.events_missing", quietMissing);
  emit("quiet.drawn", number(quiet.summary, "drawn"));
  emit("quiet.capture_written", number(quiet.summary, "capture_written"));
  emit("quiet.metrics_written", number(quiet.summary, "metrics_written"));
  emit("quiet.exit", number(quiet.summary, "exit"));
  emit("quiet.draw_tick", quietDrawTick);
  emit("bmp_identical", bmpIdentical ? 1 : 0);
  emit("bmp_bytes", static_cast<long long>(quietBmpBytes.size()));
  emit("metrics_last_tick_normal", normalLastTick);
  emit("metrics_last_tick_quiet", quietLastTick);
  emit("metrics_last_frames_normal", normalFrames);
  emit("metrics_last_frames_quiet", quietFrames);
  // What the metrics comparison actually did, named so the count cannot be
  // read as more work than it was: in pinned mode two fields are compared for
  // *equality* across the pair; in live mode the clock-derived fields cannot be
  // compared for equality at all, and what is checked instead is that each
  // side's own last row reached the requested tick.
  if (live) {
    out << " metrics_scope=reach metrics_equal_fields=- metrics_equal_count=0"
        << " metrics_reach_fields=tick metrics_reach_count=2";
  } else {
    out << " metrics_scope=equality metrics_equal_fields=rendered_frames,tick"
        << " metrics_equal_count=2";
  }
  out << " trace_scope=" << (live ? "load-prefix+last-scene" : "all");
  emit("trace_prefix_lines", static_cast<long long>(normalScope.prefix));
  emit("trace_tail_lines", static_cast<long long>(normalScope.tail));
  emit("trace_render_identical", traceIdentical ? 1 : 0);
  emit("trace_lines_compared", static_cast<long long>(normalScope.lines.size()));
  emit("trace_mismatch_line", traceIdentical ? -1 : firstMismatch);
  emit("checks", static_cast<long long>(checks.total));
  emit("failed", static_cast<long long>(checks.failed));
  if (!checks.firstFail.empty()) out << " first_fail=" << checks.firstFail;
  out << " status=" << (checks.failed == 0 ? "pass" : "fail");
  std::printf("%s\n", out.str().c_str());
  return checks.failed == 0 ? 0 : 1;
}
