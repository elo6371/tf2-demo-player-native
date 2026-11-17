#include "map_basemap.h"


#include <Windows.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "native_renderer.h"
#include "wic_image.h"

namespace tf2::native {
namespace {

// The package the user pointed at (lead relay, 2026-11-17). Overridable with
// --overview-root; never compiled into the binary as image data.
const char* kDefaultRoot = "E:/demo测试/demos_tf_maps";

std::string g_root = kDefaultRoot;
std::string g_imageOverride;

bool widenUtf8(const std::string& text, std::wstring& out) {
  if (text.empty()) return false;
  const int length = MultiByteToWideChar(CP_UTF8, 0, text.c_str(),
    static_cast<int>(text.size()), nullptr, 0);
  if (length <= 0) return false;
  out.assign(static_cast<std::size_t>(length), L'\0');
  return MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
    &out[0], length) == length;
}

// ---------------------------------------------------------------- tiny JSON
// boundaries.json is a generated file with a fixed shape (object -> object ->
// {x, y} numbers). A full JSON library would be a dependency for forty-eight
// rectangles, so this is the smallest parser that can read it and still fail
// loudly on malformed input instead of silently mapping nothing.
struct JsonValue {
  enum class Kind { Null, Number, String, Bool, Array, Object } kind = Kind::Null;
  double number = 0.0;
  bool boolean = false;
  std::string text;
  std::vector<JsonValue> items;
  std::vector<std::pair<std::string, JsonValue>> members;

  const JsonValue* member(const char* name) const {
    for (const auto& entry : members) if (entry.first == name) return &entry.second;
    return nullptr;
  }
};

struct JsonParser {
  const std::string& text;
  std::size_t at = 0;
  std::string error;

  explicit JsonParser(const std::string& source) : text(source) {}

  void skipSpace() {
    while (at < text.size()) {
      const char c = text[at];
      if (c == ' ' || c == '\t' || c == '\r' || c == '\n') { ++at; continue; }
      break;
    }
  }

  bool fail(const char* token) { if (error.empty()) error = token; return false; }

  bool parseString(std::string& out) {
    skipSpace();
    if (at >= text.size() || text[at] != '"') return fail("expected-string");
    ++at;
    out.clear();
    while (at < text.size()) {
      const char c = text[at++];
      if (c == '"') return true;
      if (c != '\\') { out.push_back(c); continue; }
      if (at >= text.size()) return fail("bad-escape");
      const char escaped = text[at++];
      switch (escaped) {
        case '"': out.push_back('"'); break;
        case '\\': out.push_back('\\'); break;
        case '/': out.push_back('/'); break;
        case 'b': out.push_back('\b'); break;
        case 'f': out.push_back('\f'); break;
        case 'n': out.push_back('\n'); break;
        case 'r': out.push_back('\r'); break;
        case 't': out.push_back('\t'); break;
        case 'u': {
          if (at + 4 > text.size()) return fail("bad-unicode");
          at += 4;
          out.push_back('?');
          break;
        }
        default: return fail("bad-escape");
      }
    }
    return fail("unterminated-string");
  }

  bool parseValue(JsonValue& out, int depth) {
    if (depth > 64) return fail("too-deep");
    skipSpace();
    if (at >= text.size()) return fail("unexpected-end");
    const char c = text[at];
    if (c == '{') {
      ++at;
      out.kind = JsonValue::Kind::Object;
      skipSpace();
      if (at < text.size() && text[at] == '}') { ++at; return true; }
      while (true) {
        std::string key;
        if (!parseString(key)) return false;
        skipSpace();
        if (at >= text.size() || text[at] != ':') return fail("expected-colon");
        ++at;
        JsonValue value;
        if (!parseValue(value, depth + 1)) return false;
        out.members.emplace_back(std::move(key), std::move(value));
        skipSpace();
        if (at < text.size() && text[at] == ',') { ++at; continue; }
        if (at < text.size() && text[at] == '}') { ++at; return true; }
        return fail("expected-object-end");
      }
    }
    if (c == '[') {
      ++at;
      out.kind = JsonValue::Kind::Array;
      skipSpace();
      if (at < text.size() && text[at] == ']') { ++at; return true; }
      while (true) {
        JsonValue value;
        if (!parseValue(value, depth + 1)) return false;
        out.items.push_back(std::move(value));
        skipSpace();
        if (at < text.size() && text[at] == ',') { ++at; continue; }
        if (at < text.size() && text[at] == ']') { ++at; return true; }
        return fail("expected-array-end");
      }
    }
    if (c == '"') {
      out.kind = JsonValue::Kind::String;
      return parseString(out.text);
    }
    if (c == 't' && text.compare(at, 4, "true") == 0) {
      at += 4; out.kind = JsonValue::Kind::Bool; out.boolean = true; return true;
    }
    if (c == 'f' && text.compare(at, 5, "false") == 0) {
      at += 5; out.kind = JsonValue::Kind::Bool; out.boolean = false; return true;
    }
    if (c == 'n' && text.compare(at, 4, "null") == 0) {
      at += 4; out.kind = JsonValue::Kind::Null; return true;
    }
    // number
    const char* begin = text.c_str() + at;
    char* end = nullptr;
    const double number = std::strtod(begin, &end);
    if (end == begin) return fail("bad-value");
    out.kind = JsonValue::Kind::Number;
    out.number = number;
    at += static_cast<std::size_t>(end - begin);
    return true;
  }
};

bool readFile(const std::string& utf8Path, std::string& out, std::string& error) {
  std::wstring wide;
  if (!widenUtf8(utf8Path, wide)) { error = "bad-path"; return false; }
  std::ifstream stream(wide.c_str(), std::ios::binary);
  if (!stream) { error = "open"; return false; }
  stream.seekg(0, std::ios::end);
  const std::streamoff size = stream.tellg();
  if (size < 0 || size > (64 << 20)) { error = "size"; return false; }
  stream.seekg(0, std::ios::beg);
  out.assign(static_cast<std::size_t>(size), '\0');
  if (size > 0) stream.read(&out[0], size);
  if (!stream && !stream.eof()) { error = "read"; return false; }
  error = "ok";
  return true;
}

bool fileExists(const std::string& utf8Path) {
  std::wstring wide;
  if (!widenUtf8(utf8Path, wide)) return false;
  const DWORD attributes = GetFileAttributesW(wide.c_str());
  return attributes != INVALID_FILE_ATTRIBUTES
      && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

std::string joinPath(const std::string& base, const std::string& leaf) {
  if (leaf.empty()) return base;
  if (leaf[0] == '/' || (leaf.size() > 1 && leaf[1] == ':')) return leaf;
  if (base.empty()) return leaf;
  const char last = base[base.size() - 1];
  if (last == '/' || last == '\\') return base + leaf;
  return base + "/" + leaf;
}

// ------------------------------------------------------------- name resolve
// demos.tf mapview.js trimMapName: strip trailing _segment while there is more
// than one underscore left. "cp_process_f12" -> "cp_process"; "pl_upward" stays.
std::string trimMapName(const std::string& name) {
  std::string trimmed = name;
  while (trimmed.find('_') != std::string::npos
      && trimmed.rfind('_') > trimmed.find('_')) {
    trimmed = trimmed.substr(0, trimmed.rfind('_'));
  }
  return trimmed;
}

// Curated exact-layout map pairs: the demo header name and the package key that
// depicts the SAME layout. From the user-supplied coverage list (2026-11-17).
// Everything outside this table is "exact" only when the package key equals the
// demo header name verbatim -- the conservative default, because calling an old
// layout exact is the one error that silently misplaces every player dot.
struct ExactRow { const char* demoMap; const char* packageKey; };
constexpr ExactRow kExactLayouts[] = {
  {"pl_upward_f12", "pl_upward"},
  {"cp_process_f12", "cp_process_final"},
};

// ---------------------------------------------------------------- boundaries
struct BoundaryTable {
  bool loaded = false;
  std::string reason = "not-loaded";
  std::string root;
  std::unordered_map<std::string, BasemapBounds> byKey;
  std::vector<std::string> order;
};

BoundaryTable g_boundaries;

bool readNumberMember(const JsonValue* object, const char* name, double& out) {
  if (!object) return false;
  const JsonValue* value = object->member(name);
  if (!value || value->kind != JsonValue::Kind::Number || !std::isfinite(value->number)) return false;
  out = value->number;
  return true;
}

void loadBoundaries() {
  if (g_boundaries.loaded && g_boundaries.root == g_root) return;
  g_boundaries = BoundaryTable{};
  g_boundaries.root = g_root;
  const std::string path = joinPath(g_root, "boundaries.json");
  std::string text, error;
  if (!readFile(path, text, error)) { g_boundaries.reason = "no-boundaries"; return; }
  JsonValue root;
  JsonParser parser(text);
  if (!parser.parseValue(root, 0) || root.kind != JsonValue::Kind::Object) {
    g_boundaries.reason = "boundaries-invalid";
    return;
  }
  for (const auto& entry : root.members) {
    const JsonValue* min = entry.second.member("boundary_min");
    const JsonValue* max = entry.second.member("boundary_max");
    BasemapBounds bounds;
    if (!readNumberMember(min, "x", bounds.minX) || !readNumberMember(min, "y", bounds.minY)
        || !readNumberMember(max, "x", bounds.maxX) || !readNumberMember(max, "y", bounds.maxY)) {
      continue;
    }
    if (!(bounds.maxX > bounds.minX) || !(bounds.maxY > bounds.minY)) continue;
    bounds.valid = true;
    g_boundaries.order.push_back(entry.first);
    g_boundaries.byKey[entry.first] = bounds;
  }
  if (g_boundaries.byKey.empty()) { g_boundaries.reason = "boundaries-empty"; return; }
  g_boundaries.loaded = true;
  g_boundaries.reason = "ok";
}

// ------------------------------------------------------------------- cache
struct CacheEntry {
  bool decoded = false;
  std::string path;
  DecodedImage image;
  double decodeMs = 0.0;   // paid once; a cache hit still reports it
  double uploadMs = 0.0;   // paid once on texture create; a cache hit still reports it
  std::string reason = "not-loaded";
};

std::string g_cacheRoot;
std::unordered_map<std::string, CacheEntry> g_cache;  // mapName -> entry

}  // namespace

void basemapSetRoot(const std::string& root) {
  if (g_root == root) return;
  g_root = root;
  g_boundaries = BoundaryTable{};
  g_cache.clear();
}

const std::string& basemapRoot() { return g_root; }

void basemapSetImageOverride(const std::string& path) {
  if (g_imageOverride == path) return;
  g_imageOverride = path;
  g_cache.clear();
}

const std::string& basemapImageOverride() { return g_imageOverride; }

std::string basemapResolveKey(const std::string& mapName, bool* exact, std::string* rule) {
  if (exact) *exact = false;
  if (rule) *rule = "none";
  if (mapName.empty()) return mapName;
  if (!g_boundaries.loaded) loadBoundaries();
  const auto has = [](const std::string& key) {
    return g_boundaries.byKey.find(key) != g_boundaries.byKey.end();
  };
  // 1) the demos.tf cp_gullywash_f* alias (f7/f8/f9 -> f6): those patch levels
  //    share one layout in the package.
  if (mapName.compare(0, 16, "cp_gullywash_f") == 0
      && mapName.compare(0, 19, "cp_gullywash_final") != 0) {
    if (rule) *rule = "alias-gullywash";
    return "cp_gullywash_f6";
  }
  // 2) exact key.
  if (has(mapName)) { if (rule) *rule = "exact"; return mapName; }
  // 3) the trimmed name is a key.
  const std::string trimmed = trimMapName(mapName);
  if (has(trimmed)) { if (rule) *rule = "trim"; return trimmed; }
  // 4) an existing key trims down to the full name.
  for (const std::string& key : g_boundaries.order) {
    if (trimMapName(key) == mapName) { if (rule) *rule = "reverse-trim"; return key; }
  }
  // 5) an existing key trims to the trimmed name.
  for (const std::string& key : g_boundaries.order) {
    if (trimMapName(key) == trimmed) { if (rule) *rule = "reverse-trim-2"; return key; }
  }
  if (rule) *rule = "unresolved";
  return mapName;
}

BasemapView basemapLoad(const std::string& mapName, Renderer* renderer) {
  BasemapView view;
  view.requestName = mapName;
  if (mapName.empty()) { view.reason = "no-map-name"; return view; }
  loadBoundaries();
  if (!g_boundaries.loaded) { view.reason = g_boundaries.reason; return view; }
  if (g_cacheRoot != g_root) { g_cache.clear(); g_cacheRoot = g_root; }
  bool exact = false;
  std::string rule;
  const std::string key = basemapResolveKey(mapName, &exact, &rule);
  view.sourceKey = key;
  view.resolveRule = rule;
  // Curated layout table decides "exact" when the resolver landed on another
  // key; a same-name hit is always exact.
  if (rule == "exact") {
    exact = true;
  } else {
    for (const ExactRow& row : kExactLayouts) {
      if (mapName == row.demoMap && key == row.packageKey) { exact = true; break; }
    }
  }
  view.exact = exact;
  const auto found = g_boundaries.byKey.find(key);
  if (found == g_boundaries.byKey.end()) { view.reason = "no-boundary"; return view; }
  view.bounds = found->second;
  view.imagePath = !g_imageOverride.empty()
      ? g_imageOverride
      : joinPath(joinPath(g_root, "images"), key + ".png");
  if (!fileExists(view.imagePath)) { view.reason = "no-image"; return view; }
  CacheEntry& cache = g_cache[mapName];
  if (!cache.decoded || cache.path != view.imagePath) {
    cache = CacheEntry{};
    cache.path = view.imagePath;
    const auto started = std::chrono::steady_clock::now();
    std::string error;
    if (!decodeImageFile(view.imagePath, cache.image, error)) {
      cache.reason = "decode-failed:" + error;
      view.reason = cache.reason;
      return view;
    }
    cache.decoded = true;
    cache.reason = "ok";
    cache.decodeMs = std::chrono::duration<double, std::milli>(
      std::chrono::steady_clock::now() - started).count();
  }
  view.decodeMs = cache.decodeMs;
  view.imageWidth = cache.image.width;
  view.imageHeight = cache.image.height;
  const double boundaryAspect = view.bounds.width() / view.bounds.height();
  if (boundaryAspect > 0.0 && view.imageHeight > 0) {
    const double imageAspect = static_cast<double>(view.imageWidth) / view.imageHeight;
    view.aspectDeviationPercent = std::fabs(imageAspect - boundaryAspect) / boundaryAspect * 100.0;
  }
  if (!renderer) { view.ready = true; view.reason = "ok"; view.format = "cpu-only"; return view; }
  const std::string tag = view.imagePath + "#" + std::to_string(view.imageWidth) + "x"
      + std::to_string(view.imageHeight);
  if (!renderer->hudImageTextureReady() || renderer->hudImageTextureTag() != tag) {
    const auto started = std::chrono::steady_clock::now();
    std::string error;
    if (!renderer->setHudImageTexture(static_cast<UINT>(view.imageWidth),
          static_cast<UINT>(view.imageHeight), cache.image.rgba.data(), tag, &error)) {
      view.reason = "upload-failed:" + error;
      return view;
    }
    cache.uploadMs = std::chrono::duration<double, std::milli>(
      std::chrono::steady_clock::now() - started).count();
  }
  view.uploadMs = cache.uploadMs;
  view.textureReady = renderer->hudImageTextureReady();
  view.format = renderer->hudImageTextureFormat();
  view.sampler = renderer->hudImageSamplerMode();
  view.mipLevels = renderer->hudImageMipLevels();
  view.ready = view.textureReady;
  view.reason = view.ready ? "ok" : "upload-failed:no-texture";
  return view;
}

}  // namespace tf2::native
