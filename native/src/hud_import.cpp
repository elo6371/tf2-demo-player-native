#include "hud_import.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <unordered_set>
#include <utility>

namespace tf2::native {
namespace {

constexpr std::size_t kMaxRelativeChars = 240;

bool isSeparator(char character) { return character == '\\' || character == '/'; }

std::string toLowerAscii(std::string text) {
  for (char& character : text) {
    if (character >= 'A' && character <= 'Z') character = static_cast<char>(character - 'A' + 'a');
  }
  return text;
}

std::string trim(const std::string& text) {
  std::size_t begin = 0, end = text.size();
  while (begin < end && std::isspace(static_cast<unsigned char>(text[begin]))) ++begin;
  while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1]))) --end;
  return text.substr(begin, end - begin);
}

// A reserved device name is refused as a path COMPONENT, not as a substring:
// "console.res" is a perfectly good file name, "CON.res" is not, because
// Win32 opens the device for it. The test is "the stem up to the first dot
// equals a reserved word", which is exactly the rule the OS applies.
bool isReservedDeviceName(const std::string& component) {
  const auto dot = component.find('.');
  const std::string stem = toLowerAscii(component.substr(0, dot));
  static const char* kReserved[] = {
    "con", "prn", "aux", "nul",
    "com1", "com2", "com3", "com4", "com5", "com6", "com7", "com8", "com9",
    "lpt1", "lpt2", "lpt3", "lpt4", "lpt5", "lpt6", "lpt7", "lpt8", "lpt9",
  };
  for (const char* word : kReserved) {
    if (stem == word) return true;
  }
  return false;
}

// UTF-8 acceptance, overlong forms rejected, no surrogate halves, nothing above
// U+10FFFF. A HUD file is text; a file that is not valid UTF-8 is refused rather
// than passed through with replacement characters, because a silent U+FFFD in a
// panel label is a claim about the user's file that is not true.
bool validUtf8(const std::string& text) {
  std::size_t i = 0;
  while (i < text.size()) {
    const unsigned char lead = static_cast<unsigned char>(text[i]);
    std::size_t extra = 0;
    unsigned int codePoint = 0;
    if (lead < 0x80) { ++i; continue; }
    else if ((lead & 0xE0) == 0xC0) { extra = 1; codePoint = lead & 0x1F; }
    else if ((lead & 0xF0) == 0xE0) { extra = 2; codePoint = lead & 0x0F; }
    else if ((lead & 0xF8) == 0xF0) { extra = 3; codePoint = lead & 0x07; }
    else return false;
    if (i + extra >= text.size()) return false;
    for (std::size_t k = 1; k <= extra; ++k) {
      const unsigned char continuation = static_cast<unsigned char>(text[i + k]);
      if ((continuation & 0xC0) != 0x80) return false;
      codePoint = (codePoint << 6) | (continuation & 0x3F);
    }
    if (extra == 1 && codePoint < 0x80) return false;
    if (extra == 2 && codePoint < 0x800) return false;
    if (extra == 3 && codePoint < 0x10000) return false;
    if (codePoint > 0x10FFFF) return false;
    if (codePoint >= 0xD800 && codePoint <= 0xDFFF) return false;
    i += extra + 1;
  }
  return true;
}

} // namespace

const char* hudRefusalName(HudRefusal reason) {
  switch (reason) {
    case HudRefusal::None: return "none";
    case HudRefusal::EmptyPath: return "empty-path";
    case HudRefusal::AbsolutePath: return "absolute-path";
    case HudRefusal::DriveRelative: return "drive-relative";
    case HudRefusal::ParentTraversal: return "parent-traversal";
    case HudRefusal::AlternateStream: return "alternate-stream";
    case HudRefusal::TrailingDotOrSpace: return "trailing-dot-or-space";
    case HudRefusal::ControlCharacter: return "control-character";
    case HudRefusal::ReservedDeviceName: return "reserved-device-name";
    case HudRefusal::PathTooLong: return "path-too-long";
    case HudRefusal::NotInsideRoot: return "not-inside-root";
    case HudRefusal::Missing: return "missing";
    case HudRefusal::NotARegularFile: return "not-a-regular-file";
    case HudRefusal::TooLarge: return "too-large";
    case HudRefusal::TooManyFiles: return "too-many-files";
    case HudRefusal::InvalidUtf8: return "invalid-utf8";
    case HudRefusal::UnsupportedExtension: return "unsupported-extension";
    case HudRefusal::MalformedKeyValues: return "malformed-keyvalues";
    case HudRefusal::UnsupportedInclude: return "unsupported-include";
    case HudRefusal::NestingTooDeep: return "nesting-too-deep";
    case HudRefusal::TooManyPanels: return "too-many-panels";
    case HudRefusal::DuplicatePanelName: return "duplicate-panel-name";
    case HudRefusal::MissingDocumentName: return "missing-document-name";
    case HudRefusal::MissingRootPanel: return "missing-root-panel";
    case HudRefusal::UnsupportedCoordinate: return "unsupported-coordinate";
    case HudRefusal::ZeroExtent: return "zero-extent";
    case HudRefusal::IoError: return "io-error";
  }
  return "unknown";
}

// ---- path safety ----------------------------------------------------------

HudSafePath sanitizeHudRelativePath(const std::string& text) {
  HudSafePath result;
  if (text.empty()) { result.reason = HudRefusal::EmptyPath; return result; }
  if (text.size() > kMaxRelativeChars) { result.reason = HudRefusal::PathTooLong; return result; }

  // Control bytes first: a NUL truncates a Win32 path, and a CR/LF would let a
  // settings value smuggle a second line into anything that logs it.
  for (const char character : text) {
    const unsigned char byte = static_cast<unsigned char>(character);
    if (byte < 0x20 || byte == 0x7f) { result.reason = HudRefusal::ControlCharacter; return result; }
  }

  // Absolute in any of the shapes Windows accepts.
  const bool unc = text.size() >= 2 && isSeparator(text[0]) && isSeparator(text[1]);
  const bool rooted = isSeparator(text[0]);
  const bool driveAbsolute = text.size() >= 3 && std::isalpha(static_cast<unsigned char>(text[0]))
    && text[1] == ':' && isSeparator(text[2]);
  const bool driveRelative = text.size() >= 2 && std::isalpha(static_cast<unsigned char>(text[0]))
    && text[1] == ':' && !driveAbsolute;
  // "\\\\?\\C:\\x" and "\\\\.\\DEVICE" are the extended-length forms; both are absolute.
  const bool extended = text.size() >= 4 && isSeparator(text[0]) && isSeparator(text[1])
    && (text[2] == '?' || text[2] == '.') && isSeparator(text[3]);
  if (unc || extended) { result.reason = HudRefusal::AbsolutePath; return result; }
  if (driveAbsolute) { result.reason = HudRefusal::AbsolutePath; return result; }
  if (driveRelative) { result.reason = HudRefusal::DriveRelative; return result; }
  if (rooted) { result.reason = HudRefusal::AbsolutePath; return result; }

  // Split on both separators, reject per component, then re-join canonically.
  std::vector<std::string> components;
  std::string current;
  for (const char character : text) {
    if (isSeparator(character)) {
      if (!current.empty()) components.push_back(current);
      current.clear();
      continue;
    }
    current.push_back(character);
  }
  if (!current.empty()) components.push_back(current);
  if (components.empty()) { result.reason = HudRefusal::EmptyPath; return result; }

  std::string joined;
  for (const std::string& component : components) {
    if (component == "..") { result.reason = HudRefusal::ParentTraversal; return result; }
    if (component == ".") { result.reason = HudRefusal::ParentTraversal; return result; }
    if (isReservedDeviceName(component)) { result.reason = HudRefusal::ReservedDeviceName; return result; }
    // An ADS is "name:stream". A colon can also appear in a legal relative path
    // only as part of a drive spec, which is already refused above.
    if (component.find(':') != std::string::npos) { result.reason = HudRefusal::AlternateStream; return result; }
    // Windows strips trailing dots and spaces from a component, so "a." and "a"
    // are the same file and the canonical path is not the one that was named.
    const char last = component.back();
    if (last == '.' || last == ' ') { result.reason = HudRefusal::TrailingDotOrSpace; return result; }
    if (!joined.empty()) joined.push_back('/');
    joined += component;
  }
  result.ok = true;
  result.reason = HudRefusal::None;
  result.text = std::move(joined);
  return result;
}

HudSafePath resolveInsideRoot(const std::filesystem::path& root, const std::string& text) {
  HudSafePath result = sanitizeHudRelativePath(text);
  if (!result.ok) return result;
  std::error_code error;
  const auto rootCanonical = std::filesystem::weakly_canonical(root, error);
  if (error || rootCanonical.empty()) { result.ok = false; result.reason = HudRefusal::IoError; return result; }
  const auto candidate = rootCanonical / std::filesystem::u8path(result.text);
  const auto candidateCanonical = std::filesystem::weakly_canonical(candidate, error);
  if (error || candidateCanonical.empty()) {
    // weakly_canonical makes the non-existing tail lexically normal and returns
    // without error for it, so an error here is a real I/O refusal.
    result.ok = false; result.reason = HudRefusal::IoError; return result;
  }
  // Compare component-wise on the canonical forms. A string prefix test would
  // accept "C:/hud2" for the root "C:/hud".
  auto rootIt = rootCanonical.begin();
  auto candidateIt = candidateCanonical.begin();
  for (; rootIt != rootCanonical.end(); ++rootIt, ++candidateIt) {
    if (candidateIt == candidateCanonical.end()) { result.ok = false; result.reason = HudRefusal::NotInsideRoot; return result; }
    if (*rootIt != *candidateIt) { result.ok = false; result.reason = HudRefusal::NotInsideRoot; return result; }
  }
  return result;
}

// ---- file reading ---------------------------------------------------------

HudFileRead readHudFile(const std::filesystem::path& root, const std::string& relative,
    const HudLimits& limits) {
  HudFileRead result;
  const HudSafePath safe = resolveInsideRoot(root, relative);
  if (!safe.ok) { result.reason = safe.reason; return result; }
  const auto file = std::filesystem::weakly_canonical(root) / std::filesystem::u8path(safe.text);
  std::error_code error;
  if (!std::filesystem::exists(file, error)) { result.reason = HudRefusal::Missing; return result; }
  if (!std::filesystem::is_regular_file(file, error)) { result.reason = HudRefusal::NotARegularFile; return result; }
  const auto size = std::filesystem::file_size(file, error);
  if (error) { result.reason = HudRefusal::IoError; return result; }
  // Two size checks on purpose: `file_size` is the OS-reported size, and the
  // read below re-checks the byte count the stream actually produced. A file
  // that is growing under us passes the first and fails the second, which is
  // the case a single check would let through.
  if (size > limits.maxFileBytes) { result.reason = HudRefusal::TooLarge; return result; }
  std::FILE* handle = nullptr;
  if (_wfopen_s(&handle, file.c_str(), L"rb") != 0 || !handle) { result.reason = HudRefusal::IoError; return result; }
  std::string bytes;
  bytes.resize(static_cast<std::size_t>(size) + 1u);
  const std::size_t read = std::fread(bytes.data(), 1u, bytes.size(), handle);
  std::fclose(handle);
  if (read > limits.maxFileBytes) { result.reason = HudRefusal::TooLarge; return result; }
  bytes.resize(read);
  result.bytes = read;
  std::size_t offset = 0;
  if (bytes.size() >= 3 && static_cast<unsigned char>(bytes[0]) == 0xEF
      && static_cast<unsigned char>(bytes[1]) == 0xBB
      && static_cast<unsigned char>(bytes[2]) == 0xBF) {
    offset = 3;
    result.hadBom = true;
  } else if (bytes.size() >= 2 && static_cast<unsigned char>(bytes[0]) == 0xFF
      && static_cast<unsigned char>(bytes[1]) == 0xFE) {
    // A UTF-16LE BOM. Refused rather than transcoded: Source HUD documents are
    // ASCII/UTF-8, and a UTF-16 file that decoded as UTF-8 would read as garbage
    // with NULs, which is a worse answer than "this encoding is not supported".
    result.reason = HudRefusal::InvalidUtf8;
    return result;
  } else if (bytes.size() >= 2 && static_cast<unsigned char>(bytes[0]) == 0xFE
      && static_cast<unsigned char>(bytes[1]) == 0xFF) {
    result.reason = HudRefusal::InvalidUtf8;
    return result;
  }
  const std::string body = bytes.substr(offset);
  if (!validUtf8(body)) { result.reason = HudRefusal::InvalidUtf8; return result; }
  // A NUL inside the text is not a character a HUD document can carry; it is
  // what a mis-decoded binary looks like, so it is refused by name.
  if (body.find('\0') != std::string::npos) { result.reason = HudRefusal::InvalidUtf8; return result; }
  result.ok = true;
  result.reason = HudRefusal::None;
  result.text = body;
  return result;
}

// ---- the coordinate grammar ----------------------------------------------

HudCoord parseHudCoord(const std::string& text) {
  HudCoord result;
  result.raw = text;
  const std::string value = trim(text);
  if (value.empty()) return result;   // Invalid: an empty coordinate is not "0"

  char* end = nullptr;
  const char first = value[0];

  if (first == 'c' || first == 'C') {
    const double number = std::strtod(value.c_str() + 1, &end);
    if (!end || *end != '\0') return result;
    result.ok = true; result.unit = HudCoordUnit::Center; result.value = static_cast<float>(number);
    return result;
  }
  if (first == 'r' || first == 'R') {
    const double number = std::strtod(value.c_str() + 1, &end);
    if (!end || *end != '\0') return result;
    result.ok = true; result.unit = HudCoordUnit::Right; result.value = static_cast<float>(number);
    return result;
  }
  if (first == 'f' || first == 'F') {
    const double number = std::strtod(value.c_str() + 1, &end);
    if (!end || *end != '\0') return result;
    result.ok = true; result.unit = HudCoordUnit::Fraction; result.value = static_cast<float>(number);
    return result;
  }
  // 'p' is Source's "proportional to parent" form. Its meaning depends on the
  // design resolution the panel tree was authored against, which this build does
  // not model, so resolving it as a plain fraction would be an approximation
  // presented as a fact. Parsed, then refused by name.
  if (first == 'p' || first == 'P') {
    const double number = std::strtod(value.c_str() + 1, &end);
    if (!end || *end != '\0') return result;
    result.ok = false; result.unit = HudCoordUnit::ParentProportional;
    result.value = static_cast<float>(number);
    return result;
  }

  if (first == 'o' || first == 'O') {
    const double number = std::strtod(value.c_str() + 1, &end);
    if (!end || *end != '\0') return result;
    // Parsed, then refused by name: "the document uses a form this build does
    // not resolve" and "the document is malformed" are different findings.
    result.ok = false; result.unit = HudCoordUnit::ParentOffset; result.value = static_cast<float>(number);
    return result;
  }
  if (first == 's' || first == 'S') {
    const double number = std::strtod(value.c_str() + 1, &end);
    if (!end || *end != '\0') return result;
    result.ok = false; result.unit = HudCoordUnit::Scaled; result.value = static_cast<float>(number);
    return result;
  }

  const double number = std::strtod(value.c_str(), &end);
  if (!end || end == value.c_str()) return result;
  if (*end == '%') {
    if (*(end + 1) != '\0') return result;
    result.ok = true; result.unit = HudCoordUnit::Percent; result.value = static_cast<float>(number);
    return result;
  }
  if (*end != '\0') return result;
  result.ok = true;
  result.unit = HudCoordUnit::Pixels;
  result.value = static_cast<float>(number);
  return result;
}

bool resolveHudCoord(const HudCoord& coord, int parentExtent, int selfExtent,
    float scalePercent, int& out) {
  if (!coord.ok) return false;
  const auto roundToInt = [](double value) { return static_cast<int>(std::lround(value)); };
  const double parent = static_cast<double>(parentExtent);
  const double self = static_cast<double>(selfExtent);
  const double factor = static_cast<double>(scalePercent) / 100.0;
  switch (coord.unit) {
    case HudCoordUnit::Pixels:
      out = roundToInt(static_cast<double>(coord.value) * factor);
      return true;
    case HudCoordUnit::Center:
      out = roundToInt((parent - self) / 2.0 + static_cast<double>(coord.value) * factor);
      return true;
    case HudCoordUnit::Right:
      out = roundToInt(parent - self - static_cast<double>(coord.value) * factor);
      return true;
    case HudCoordUnit::Fraction:
      // Deliberately NOT scaled: "f0.5" already means "half the screen", and
      // multiplying that by the user scale would make the panel leave the
      // screen at 200% rather than grow in place.
      out = roundToInt(parent * static_cast<double>(coord.value));
      return true;
    case HudCoordUnit::Percent:
      out = roundToInt(parent * static_cast<double>(coord.value) / 100.0);
      return true;
    case HudCoordUnit::ParentOffset:
    case HudCoordUnit::ParentProportional:
    case HudCoordUnit::Scaled:
    case HudCoordUnit::Invalid:
      break;
  }
  return false;
}
// ---- the layout document --------------------------------------------------

namespace {

struct LayoutToken {
  enum class Kind { Text, Open, Close };
  Kind kind = Kind::Text;
  std::string value;
  std::size_t line = 0;
};

// The document is Source KeyValues: quoted or bare tokens, braces, // and /* */
// comments, and the "#base" directive. The tokenizer is deliberately the same
// shape as the VMT one (vmt_material.cpp) because it reads the same family of
// files, but it tracks line numbers: a refusal that says "line 41" is
// actionable where "malformed document" is not.
std::vector<LayoutToken> tokenizeLayout(const std::string& text, bool& unterminated) {
  std::vector<LayoutToken> tokens;
  unterminated = false;
  std::size_t i = 0, line = 1;
  while (i < text.size()) {
    const char character = text[i];
    if (character == '\n') { ++line; ++i; continue; }
    if (std::isspace(static_cast<unsigned char>(character))) { ++i; continue; }
    if (character == '/' && i + 1 < text.size() && text[i + 1] == '/') {
      i += 2; while (i < text.size() && text[i] != '\n') ++i; continue;
    }
    if (character == '/' && i + 1 < text.size() && text[i + 1] == '*') {
      const auto end = text.find("*/", i + 2);
      if (end == std::string::npos) { unterminated = true; return tokens; }
      for (std::size_t k = i; k < end; ++k) { if (text[k] == '\n') ++line; }
      i = end + 2; continue;
    }
    if (character == '{') { tokens.push_back({LayoutToken::Kind::Open, "{", line}); ++i; continue; }
    if (character == '}') { tokens.push_back({LayoutToken::Kind::Close, "}", line}); ++i; continue; }
    LayoutToken token;
    token.line = line;
    if (character == '"') {
      ++i;
      bool closed = false;
      while (i < text.size()) {
        if (text[i] == '"') { ++i; closed = true; break; }
        // A backslash is a path separator in these files, not an escape, for the
        // reason vmt_material.cpp records: TF2 ships "models\props/x". Only a
        // backslash before a quote or another backslash is consumed.
        if (text[i] == '\\' && i + 1 < text.size() && (text[i + 1] == '"' || text[i + 1] == '\\')) {
          ++i; token.value.push_back(text[i]); ++i; continue;
        }
        if (text[i] == '\n') ++line;
        token.value.push_back(text[i]);
        ++i;
      }
      if (!closed) { unterminated = true; return tokens; }
    } else {
      while (i < text.size() && !std::isspace(static_cast<unsigned char>(text[i]))
          && text[i] != '{' && text[i] != '}') {
        token.value.push_back(text[i]);
        ++i;
      }
    }
    if (token.value.empty()) continue;
    tokens.push_back(std::move(token));
  }
  return tokens;
}

bool parseBoolValue(const std::string& text, bool& out) {
  const std::string value = trim(text);
  if (value == "1") { out = true; return true; }
  if (value == "0") { out = false; return true; }
  const std::string lower = toLowerAscii(value);
  if (lower == "true") { out = true; return true; }
  if (lower == "false") { out = false; return true; }
  return false;
}

bool parseIntValue(const std::string& text, int& out) {
  const std::string value = trim(text);
  if (value.empty()) return false;
  char* end = nullptr;
  const long parsed = std::strtol(value.c_str(), &end, 10);
  if (!end || *end != '\0') return false;
  out = static_cast<int>(parsed);
  return true;
}

// One block: its own scalar fields and the blocks it contains. Nesting is kept
// rather than flattened because a document that nests panels needs the tree to
// place them, and this build refuses that shape by name instead of guessing.
struct PanelBlock {
  std::string name;
  std::vector<std::pair<std::string, std::string>> fields;   // lower-case key
  std::vector<PanelBlock> children;
  std::size_t line = 0;
};

bool isKnownPanelKey(const std::string& key) {
  static const char* kKnown[] = {
    "controlname", "fieldname", "xpos", "ypos", "zpos", "wide", "tall",
    "visible", "enabled", "labeltext", "font", "textalignment", "image",
  };
  for (const char* known : kKnown) {
    if (key == known) return true;
  }
  return false;
}

// Reads one block body. depth is the depth of the block being read: the
// document container is depth 1, a panel is depth 2, and a block inside a panel
// is depth 3. limits.maxNesting is how many levels BELOW the document may
// exist, so maxNesting = 1 accepts panels and refuses anything deeper -- which
// is exactly the documented supported subset.
bool readBlock(const std::vector<LayoutToken>& tokens, std::size_t& cursor, bool openBraceExpected,
    PanelBlock& block, unsigned depth, const HudLimits& limits, HudRefusal& reason,
    std::size_t& deepest) {
  if (openBraceExpected) {
    if (cursor >= tokens.size() || tokens[cursor].kind != LayoutToken::Kind::Open) {
      reason = HudRefusal::MalformedKeyValues;
      return false;
    }
    ++cursor;
  }
  if (depth > deepest) deepest = depth;
  if (depth > limits.maxNesting + 1u) { reason = HudRefusal::NestingTooDeep; return false; }
  while (cursor < tokens.size()) {
    const LayoutToken& key = tokens[cursor++];
    if (key.kind == LayoutToken::Kind::Close) return true;
    if (key.kind != LayoutToken::Kind::Text || cursor >= tokens.size()) {
      reason = HudRefusal::MalformedKeyValues;
      return false;
    }
    const LayoutToken& value = tokens[cursor++];
    if (value.kind == LayoutToken::Kind::Open) {
      PanelBlock child;
      child.name = key.value;
      child.line = key.line;
      if (!readBlock(tokens, cursor, false, child, depth + 1u, limits, reason, deepest)) return false;
      block.children.push_back(std::move(child));
      continue;
    }
    if (value.kind == LayoutToken::Kind::Close) { reason = HudRefusal::MalformedKeyValues; return false; }
    block.fields.emplace_back(toLowerAscii(key.value), value.value);
  }
  reason = HudRefusal::MalformedKeyValues;
  return false;
}

} // namespace

HudLayout parseHudLayout(const std::string& text, const HudLimits& limits) {
  HudLayout layout;
  bool unterminated = false;
  const auto tokens = tokenizeLayout(text, unterminated);
  if (unterminated) {
    layout.reason = HudRefusal::MalformedKeyValues;
    layout.detail = "unterminated token or comment";
    return layout;
  }
  if (tokens.empty()) { layout.reason = HudRefusal::MissingDocumentName; return layout; }

  // "#base" is an include. This build resolves none, and a document that depends
  // on one would render with holes, so the import is refused by name rather than
  // silently dropping the included content. The directive is counted and its
  // argument consumed so the argument cannot be mistaken for the document name.
  std::vector<LayoutToken> filtered;
  filtered.reserve(tokens.size());
  for (std::size_t i = 0; i < tokens.size(); ++i) {
    if (tokens[i].kind == LayoutToken::Kind::Text && !tokens[i].value.empty()
        && tokens[i].value[0] == '#' && toLowerAscii(tokens[i].value) == "#base") {
      ++layout.includeCount;
      if (i + 1 < tokens.size() && tokens[i + 1].kind == LayoutToken::Kind::Text) ++i;
      continue;
    }
    filtered.push_back(tokens[i]);
  }
  if (layout.includeCount != 0) {
    layout.reason = HudRefusal::UnsupportedInclude;
    layout.detail = std::to_string(layout.includeCount) + " #base directive(s)";
    return layout;
  }

  if (filtered.size() < 3 || filtered[0].kind != LayoutToken::Kind::Text
      || filtered[1].kind != LayoutToken::Kind::Open) {
    layout.reason = HudRefusal::MissingDocumentName;
    return layout;
  }
  layout.documentName = filtered[0].value;

  PanelBlock document;
  document.name = "document";
  std::size_t cursor = 2;
  HudRefusal reason = HudRefusal::None;
  std::size_t deepest = 1;
  if (!readBlock(filtered, cursor, false, document, 1u, limits, reason, deepest)) {
    layout.reason = reason;
    return layout;
  }
  layout.nestingDepth = deepest;
  if (document.children.empty()) { layout.reason = HudRefusal::MissingRootPanel; return layout; }

  // The document's own scalar fields are not panels; they are counted as unknown
  // so a document that carries settings at that level does not read as fully
  // understood.
  layout.unknownKeys += document.fields.size();
  layout.rootName = document.children.front().name;

  // Panels are flattened into one pre-order vector with a parent index, which is
  // what lets resolution walk them in one pass while still honouring the tree:
  // a child's coordinates are relative to its parent's resolved rectangle, as
  // Source places them. The vector order is pre-order, so a parent is always
  // already resolved when its children are reached.
  std::unordered_set<std::string> seenNames;
  std::vector<std::pair<const PanelBlock*, int>> pending;   // block, parent index
  for (const PanelBlock& child : document.children) pending.emplace_back(&child, -1);
  while (!pending.empty()) {
    const PanelBlock* block = pending.front().first;
    const int parent = pending.front().second;
    pending.erase(pending.begin());
    if (layout.panels.size() >= limits.maxPanels) {
      layout.reason = HudRefusal::TooManyPanels;
      return layout;
    }
    HudPanel panel;
    panel.name = block->name;
    panel.parent = parent;
    panel.depth = parent < 0 ? 1u : layout.panels[static_cast<std::size_t>(parent)].depth + 1u;
    for (const auto& entry : block->fields) {
      const std::string& key = entry.first;
      const std::string& value = entry.second;
      if (!isKnownPanelKey(key)) { ++panel.unknownKeys; continue; }
      ++panel.knownKeys;
      if (key == "controlname") panel.controlName = value;
      else if (key == "fieldname") panel.fieldName = value;
      else if (key == "labeltext") panel.labelText = value;
      else if (key == "font") panel.font = value;
      else if (key == "textalignment") panel.textAlignment = toLowerAscii(trim(value));
      else if (key == "image") panel.image = value;
      else if (key == "xpos") panel.xpos = parseHudCoord(value);
      else if (key == "ypos") panel.ypos = parseHudCoord(value);
      else if (key == "wide") panel.wide = parseHudCoord(value);
      else if (key == "tall") panel.tall = parseHudCoord(value);
      else if (key == "zpos") {
        int parsed = 0;
        if (parseIntValue(value, parsed)) panel.zpos = parsed;
      } else if (key == "visible") {
        bool parsed = true;
        if (parseBoolValue(value, parsed)) panel.visible = parsed;
      } else if (key == "enabled") {
        bool parsed = true;
        if (parseBoolValue(value, parsed)) panel.enabled = parsed;
      }
    }
    // A panel this build cannot place is counted, not fatal: one bad control
    // must not hide the rest, but the count is what stops "it imported" from
    // being read as "it renders".
    if (!panel.xpos.ok || !panel.ypos.ok || !panel.wide.ok || !panel.tall.ok) {
      // A refused panel takes its subtree with it: a child anchored to a parent
      // this build cannot place has no origin, and inventing one would put the
      // child somewhere the document never said.
      ++layout.refusedPanels;
      if (layout.detail.empty()) {
        layout.detail = "panel " + panel.name + " has an unsupported or missing coordinate";
      }
      continue;
    }
    if (!seenNames.insert(panel.name).second) ++layout.duplicateNames;
    layout.unknownKeys += panel.unknownKeys;
    const int index = static_cast<int>(layout.panels.size());
    layout.panels.push_back(std::move(panel));
    // Children are queued AFTER the panel is in the vector, and their parent
    // index is this one, so resolution can read the parent rectangle that this
    // pre-order visit already produced.
    for (const PanelBlock& child : block->children) pending.emplace_back(&child, index);
  }

  // A document whose every panel was refused is a refusal, not an empty HUD.
  if (layout.panels.empty()) {
    layout.reason = layout.refusedPanels != 0 ? HudRefusal::UnsupportedCoordinate
                                             : HudRefusal::MissingRootPanel;
    return layout;
  }
  layout.ok = true;
  layout.reason = HudRefusal::None;
  return layout;
}

// ---- screen-space rectangles ---------------------------------------------

std::vector<HudScreenRect> resolveHudRects(const HudLayout& layout, int screenWidth,
    int screenHeight, float dpiScale, int scalePercent) {
  std::vector<HudScreenRect> rects;
  rects.reserve(layout.panels.size());
  const float scale = static_cast<float>(scalePercent) * (dpiScale > 0.0f ? dpiScale : 1.0f);
  const int width = (std::max)(1, screenWidth);
  const int height = (std::max)(1, screenHeight);
  for (std::size_t index = 0; index < layout.panels.size(); ++index) {
    const HudPanel& panel = layout.panels[index];
    HudScreenRect rect;
    rect.panel = index;
    // A panel's coordinates are relative to the rectangle it lives in: the
    // client for a top-level panel, the parent's own rectangle for a child. That
    // is Source's rule, and it is why the parent has to be resolved first --
    // which the pre-order storage guarantees.
    int originX = 0, originY = 0, parentWidth = width, parentHeight = height;
    if (panel.parent >= 0) {
      const std::size_t parentIndex = static_cast<std::size_t>(panel.parent);
      if (parentIndex >= rects.size() || !rects[parentIndex].valid) {
        // The parent was refused or clipped away; this panel has no origin.
        rect.reason = rects.size() > parentIndex ? rects[parentIndex].reason
                                                 : HudRefusal::UnsupportedCoordinate;
        rects.push_back(rect);
        continue;
      }
      originX = rects[parentIndex].x;
      originY = rects[parentIndex].y;
      parentWidth = rects[parentIndex].w;
      parentHeight = rects[parentIndex].h;
    }
    // Width and height first: the x/y anchors need this panel's own extent.
    int panelWidth = 0, panelHeight = 0;
    if (!resolveHudCoord(panel.wide, parentWidth, 0, scale, panelWidth)
        || !resolveHudCoord(panel.tall, parentHeight, 0, scale, panelHeight)) {
      rect.reason = HudRefusal::UnsupportedCoordinate;
      rects.push_back(rect);
      continue;
    }
    int x = 0, y = 0;
    if (!resolveHudCoord(panel.xpos, parentWidth, panelWidth, scale, x)
        || !resolveHudCoord(panel.ypos, parentHeight, panelHeight, scale, y)) {
      rect.reason = HudRefusal::UnsupportedCoordinate;
      rects.push_back(rect);
      continue;
    }
    x += originX;
    y += originY;
    // A zero or negative extent is not a panel with no pixels; it is a document
    // that computed one. Named, so the trace can count it.
    if (panelWidth <= 0 || panelHeight <= 0) {
      rect.reason = HudRefusal::ZeroExtent;
      rects.push_back(rect);
      continue;
    }
    // Clip to the client: a panel anchored off-screen has no pixels here, and
    // the HUD region the world comparison excludes must not claim area outside
    // the frame. A fully-off-screen panel is reported as such rather than
    // producing an empty rectangle that looks like a valid one.
    const int left = (std::max)(0, x);
    const int top = (std::max)(0, y);
    const int right = (std::min)(width, x + panelWidth);
    const int bottom = (std::min)(height, y + panelHeight);
    if (right <= left || bottom <= top) {
      rect.reason = HudRefusal::ZeroExtent;
      rects.push_back(rect);
      continue;
    }
    rect.valid = true;
    rect.x = left;
    rect.y = top;
    rect.w = right - left;
    rect.h = bottom - top;
    rects.push_back(rect);
  }
  return rects;
}

HudPixelRect hudUnionRect(const std::vector<HudScreenRect>& rects) {
  HudPixelRect result;
  for (const HudScreenRect& rect : rects) {
    if (!rect.valid) continue;
    if (result.empty) {
      result.x = rect.x; result.y = rect.y; result.w = rect.w; result.h = rect.h;
      result.empty = false;
      continue;
    }
    const int left = (std::min)(result.x, rect.x);
    const int top = (std::min)(result.y, rect.y);
    const int right = (std::max)(result.x + result.w, rect.x + rect.w);
    const int bottom = (std::max)(result.y + result.h, rect.y + rect.h);
    result.x = left; result.y = top; result.w = right - left; result.h = bottom - top;
  }
  return result;
}

// ---- the settings block ---------------------------------------------------

int clampHudScale(int scalePercent) {
  // The contract's window is 50..200. A value outside it is clamped rather than
  // taken, and hudScaleInRange() is what lets the caller say it repaired the
  // file instead of silently rendering at a scale the user did not choose.
  if (scalePercent < 50) return 50;
  if (scalePercent > 200) return 200;
  return scalePercent;
}

bool hudScaleInRange(int scalePercent) {
  return scalePercent >= 50 && scalePercent <= 200;
}

} // namespace tf2::native
