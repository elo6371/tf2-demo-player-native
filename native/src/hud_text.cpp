#include "hud_text.h"

#include <Windows.h>
#include <dwrite_2.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <utility>

namespace tf2::native {
namespace {

using Microsoft::WRL::ComPtr;

// The built-in family order. Bahnschrift is the DIN-style face Windows 10/11
// ship; its SemiCondensed family keeps a player name inside a 180 px column.
// Segoe UI is on every supported Windows and is the last primary.
const wchar_t* const kPrimaryFamilies[] = {
  L"Bahnschrift SemiCondensed", L"Bahnschrift", L"Segoe UI",
};
// Per-character fallback after the primary: Latin/Cyrillic/Greek first, then
// the CJK UI faces, then Indic and symbols. Order matters only when two faces
// both have a character; the first one wins, so the result is stable.
const wchar_t* const kFallbackFamilies[] = {
  L"Segoe UI", L"Microsoft YaHei UI", L"Microsoft YaHei", L"Microsoft JhengHei UI",
  L"Yu Gothic UI", L"Malgun Gothic", L"Nirmala UI", L"Segoe UI Symbol", L"Arial",
};

constexpr int kFontSlots = 3;  // indexed by HudTextFont; slot 0 (Bitmap8) unused
constexpr char32_t kEllipsis = 0x2026;

struct Face {
  ComPtr<IDWriteFont> font;
  ComPtr<IDWriteFontFace> face;
  ComPtr<IDWriteFontFace1> face1;
  std::wstring family;
  DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_NORMAL;
  DWRITE_FONT_METRICS metrics{};
  bool kerning = false;
};

struct GlyphKey {
  int face = -1;
  std::uint16_t glyph = 0;
  int px = 0;
  bool operator==(const GlyphKey& other) const {
    return face == other.face && glyph == other.glyph && px == other.px;
  }
};
struct GlyphKeyHash {
  std::size_t operator()(const GlyphKey& key) const {
    return (static_cast<std::size_t>(key.face) * 1000003u)
      ^ (static_cast<std::size_t>(key.glyph) * 131u) ^ static_cast<std::size_t>(key.px);
  }
};

struct GlyphSlot {
  bool hasBitmap = false;
  int x = 0, y = 0, w = 0, h = 0;   // in the atlas
  int offsetX = 0, offsetY = 0;     // bitmap top-left relative to the baseline origin
};

struct Mapping {
  int face = -1;                    // -1: no face in the chain has the character
  std::uint16_t glyph = 0;
  bool fallback = false;
};

enum class RasterResult { Ok, Full, Failed };

struct Engine {
  std::mutex mutex;
  bool initialized = false;
  bool available = false;
  std::string reason = "not-initialized";
  std::wstring preferred;
  std::size_t lateFamilyRequests = 0;
  ComPtr<IDWriteFactory2> factory;
  ComPtr<IDWriteFontCollection> collection;
  std::vector<Face> faces;
  std::vector<int> chain[kFontSlots];  // face indices, primary first
  std::vector<std::uint8_t> atlas;
  int shelfX = 0, shelfY = 0, shelfH = 0;
  std::uint64_t revision = 0;
  std::uint32_t generation = 0;
  std::unordered_map<GlyphKey, GlyphSlot, GlyphKeyHash> glyphs;
  std::unordered_map<std::uint64_t, Mapping> cmap;  // (slot << 32) | codepoint
  std::size_t rasterized = 0, resets = 0, missing = 0, fromFallback = 0;
};

Engine& engine() {
  static Engine instance;
  return instance;
}

std::string narrow(const std::wstring& text) {
  std::string out;
  out.reserve(text.size());
  for (const wchar_t character : text) out.push_back(character < 0x80 ? static_cast<char>(character) : '?');
  return out;
}

// UTF-8 -> code points. Malformed bytes become U+FFFD, the same rule as
// hudUtf8Decode (hud_font.cpp); restated here so the probe does not need the
// T67 translation unit.
std::vector<char32_t> decodeUtf8(const std::string& text) {
  std::vector<char32_t> out;
  out.reserve(text.size());
  std::size_t i = 0;
  while (i < text.size()) {
    const auto lead = static_cast<unsigned char>(text[i]);
    int extra = 0;
    char32_t cp = 0;
    if (lead < 0x80) { cp = static_cast<char32_t>(lead); }
    else if ((lead & 0xE0) == 0xC0) { cp = static_cast<char32_t>(lead & 0x1F); extra = 1; }
    else if ((lead & 0xF0) == 0xE0) { cp = static_cast<char32_t>(lead & 0x0F); extra = 2; }
    else if ((lead & 0xF8) == 0xF0) { cp = static_cast<char32_t>(lead & 0x07); extra = 3; }
    else { out.push_back(0xFFFD); ++i; continue; }
    if (i + static_cast<std::size_t>(extra) >= text.size()) {  // truncated sequence
      out.push_back(0xFFFD);
      break;
    }
    bool ok = true;
    for (int k = 1; k <= extra; ++k) {
      const auto next = static_cast<unsigned char>(text[i + static_cast<std::size_t>(k)]);
      if ((next & 0xC0) != 0x80) { ok = false; break; }
      cp = static_cast<char32_t>((cp << 6) | static_cast<char32_t>(next & 0x3F));
    }
    if (!ok) { out.push_back(0xFFFD); ++i; continue; }
    const bool overlong = (extra == 1 && cp < 0x80) || (extra == 2 && cp < 0x800) || (extra == 3 && cp < 0x10000);
    if (overlong || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) cp = 0xFFFD;
    out.push_back(cp);
    i += static_cast<std::size_t>(extra) + 1u;
  }
  return out;
}

int findOrLoadFace(Engine& e, const std::wstring& family, DWRITE_FONT_WEIGHT weight) {
  for (std::size_t index = 0; index < e.faces.size(); ++index) {
    if (e.faces[index].family == family && e.faces[index].weight == weight) return static_cast<int>(index);
  }
  UINT32 familyIndex = 0;
  BOOL exists = FALSE;
  if (FAILED(e.collection->FindFamilyName(family.c_str(), &familyIndex, &exists)) || !exists) return -1;
  ComPtr<IDWriteFontFamily> fontFamily;
  if (FAILED(e.collection->GetFontFamily(familyIndex, fontFamily.GetAddressOf()))) return -1;
  Face loaded;
  if (FAILED(fontFamily->GetFirstMatchingFont(weight, DWRITE_FONT_STRETCH_NORMAL,
        DWRITE_FONT_STYLE_NORMAL, loaded.font.GetAddressOf()))) return -1;
  if (FAILED(loaded.font->CreateFontFace(loaded.face.GetAddressOf()))) return -1;
  loaded.face->GetMetrics(&loaded.metrics);
  if (loaded.metrics.designUnitsPerEm == 0) return -1;
  if (SUCCEEDED(loaded.face.As(&loaded.face1)) && loaded.face1) {
    loaded.kerning = loaded.face1->HasKerningPairs() != FALSE;
  }
  loaded.family = family;
  loaded.weight = weight;
  e.faces.push_back(std::move(loaded));
  return static_cast<int>(e.faces.size() - 1);
}

void initialize(Engine& e) {
  if (e.initialized) return;
  e.initialized = true;
  if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory2),
        reinterpret_cast<IUnknown**>(e.factory.GetAddressOf()))) || !e.factory) {
    e.reason = "no-dwrite-factory2";
    return;
  }
  if (FAILED(e.factory->GetSystemFontCollection(e.collection.GetAddressOf(), FALSE)) || !e.collection) {
    e.reason = "no-font-collection";
    return;
  }
  const DWRITE_FONT_WEIGHT weights[kFontSlots] = {
    DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_WEIGHT_BOLD};
  for (int slot = 1; slot < kFontSlots; ++slot) {
    std::vector<std::wstring> primaries;
    if (!e.preferred.empty()) primaries.push_back(e.preferred);
    for (const wchar_t* family : kPrimaryFamilies) primaries.push_back(family);
    for (const std::wstring& family : primaries) {
      const int face = findOrLoadFace(e, family, weights[slot]);
      if (face >= 0) { e.chain[slot].push_back(face); break; }
    }
    if (e.chain[slot].empty()) continue;
    for (const wchar_t* family : kFallbackFamilies) {
      const int face = findOrLoadFace(e, family, weights[slot]);
      if (face < 0) continue;
      if (std::find(e.chain[slot].begin(), e.chain[slot].end(), face) == e.chain[slot].end()) {
        e.chain[slot].push_back(face);
      }
    }
  }
  if (e.chain[1].empty() || e.chain[2].empty()) {
    e.reason = "no-face";
    return;
  }
  e.atlas.assign(static_cast<std::size_t>(kHudTextAtlasSize) * kHudTextAtlasSize, 0u);
  e.available = true;
  e.reason = "ok";
}

int slotOf(HudTextFont font) {
  const int slot = static_cast<int>(font);
  return (slot >= 1 && slot < kFontSlots) ? slot : 1;
}

Mapping resolve(Engine& e, int slot, char32_t cp) {
  const std::uint64_t key = (static_cast<std::uint64_t>(slot) << 32) | static_cast<std::uint64_t>(cp);
  const auto found = e.cmap.find(key);
  if (found != e.cmap.end()) return found->second;
  Mapping mapping;
  for (std::size_t position = 0; position < e.chain[slot].size(); ++position) {
    Face& face = e.faces[static_cast<std::size_t>(e.chain[slot][position])];
    BOOL has = FALSE;
    if (FAILED(face.font->HasCharacter(static_cast<UINT32>(cp), &has)) || !has) continue;
    const UINT32 codepoint = static_cast<UINT32>(cp);
    UINT16 glyph = 0;
    if (FAILED(face.face->GetGlyphIndices(&codepoint, 1, &glyph)) || glyph == 0) continue;
    mapping.face = e.chain[slot][position];
    mapping.glyph = glyph;
    mapping.fallback = position > 0;
    break;
  }
  e.cmap.emplace(key, mapping);
  return mapping;
}

float advanceOf(Engine& e, int faceIndex, std::uint16_t glyph, int px) {
  Face& face = e.faces[static_cast<std::size_t>(faceIndex)];
  DWRITE_GLYPH_METRICS metrics{};
  if (FAILED(face.face->GetDesignGlyphMetrics(&glyph, 1, &metrics, FALSE))) return 0.0f;
  return static_cast<float>(metrics.advanceWidth) * static_cast<float>(px)
    / static_cast<float>(face.metrics.designUnitsPerEm);
}

void resetAtlas(Engine& e) {
  std::fill(e.atlas.begin(), e.atlas.end(), static_cast<std::uint8_t>(0));
  e.glyphs.clear();
  e.shelfX = 0;
  e.shelfY = 0;
  e.shelfH = 0;
  ++e.generation;
  ++e.revision;
  ++e.resets;
}

RasterResult rasterize(Engine& e, int faceIndex, std::uint16_t glyph, int px, const GlyphSlot*& out) {
  const GlyphKey key{faceIndex, glyph, px};
  const auto found = e.glyphs.find(key);
  if (found != e.glyphs.end()) { out = &found->second; return RasterResult::Ok; }
  Face& face = e.faces[static_cast<std::size_t>(faceIndex)];
  FLOAT advance = 0.0f;
  DWRITE_GLYPH_OFFSET offset{};
  DWRITE_GLYPH_RUN run{};
  run.fontFace = face.face.Get();
  run.fontEmSize = static_cast<FLOAT>(px);
  run.glyphCount = 1;
  run.glyphIndices = &glyph;
  run.glyphAdvances = &advance;
  run.glyphOffsets = &offset;
  ComPtr<IDWriteGlyphRunAnalysis> analysis;
  // Grayscale antialiasing: with this mode the 1x1 texture carries 8-bit
  // coverage, which is what an R8 atlas sampled with a linear filter wants.
  if (FAILED(e.factory->CreateGlyphRunAnalysis(&run, nullptr, DWRITE_RENDERING_MODE_NATURAL_SYMMETRIC,
        DWRITE_MEASURING_MODE_NATURAL, DWRITE_GRID_FIT_MODE_DEFAULT, DWRITE_TEXT_ANTIALIAS_MODE_GRAYSCALE,
        0.0f, 0.0f, analysis.GetAddressOf())) || !analysis) {
    return RasterResult::Failed;
  }
  GlyphSlot slot;
  RECT bounds{};
  DWRITE_TEXTURE_TYPE textureType = DWRITE_TEXTURE_ALIASED_1x1;
  analysis->GetAlphaTextureBounds(DWRITE_TEXTURE_ALIASED_1x1, &bounds);
  if (bounds.right <= bounds.left || bounds.bottom <= bounds.top) {
    // Some drivers/fonts only answer the ClearType texture; its three bytes per
    // pixel are averaged below. An empty answer to both is a space.
    RECT clearType{};
    analysis->GetAlphaTextureBounds(DWRITE_TEXTURE_CLEARTYPE_3x1, &clearType);
    if (clearType.right > clearType.left && clearType.bottom > clearType.top) {
      bounds = clearType;
      textureType = DWRITE_TEXTURE_CLEARTYPE_3x1;
    }
  }
  const int width = static_cast<int>(bounds.right - bounds.left);
  const int height = static_cast<int>(bounds.bottom - bounds.top);
  if (width <= 0 || height <= 0) {
    out = &e.glyphs.emplace(key, slot).first->second;
    return RasterResult::Ok;
  }
  if (width + 1 > kHudTextAtlasSize || height + 1 > kHudTextAtlasSize) return RasterResult::Failed;
  if (e.shelfX + width + 1 > kHudTextAtlasSize) {
    e.shelfY += e.shelfH + 1;
    e.shelfX = 0;
    e.shelfH = 0;
  }
  if (e.shelfY + height + 1 > kHudTextAtlasSize) return RasterResult::Full;
  const int bytesPerPixel = textureType == DWRITE_TEXTURE_CLEARTYPE_3x1 ? 3 : 1;
  std::vector<BYTE> pixels(static_cast<std::size_t>(width) * static_cast<std::size_t>(height)
    * static_cast<std::size_t>(bytesPerPixel));
  if (FAILED(analysis->CreateAlphaTexture(textureType, &bounds, pixels.data(),
        static_cast<UINT32>(pixels.size())))) {
    return RasterResult::Failed;
  }
  slot.hasBitmap = true;
  slot.x = e.shelfX;
  slot.y = e.shelfY;
  slot.w = width;
  slot.h = height;
  slot.offsetX = static_cast<int>(bounds.left);
  slot.offsetY = static_cast<int>(bounds.top);
  for (int row = 0; row < height; ++row) {
    std::uint8_t* destination = &e.atlas[static_cast<std::size_t>(slot.y + row) * kHudTextAtlasSize
      + static_cast<std::size_t>(slot.x)];
    for (int column = 0; column < width; ++column) {
      const std::size_t at = (static_cast<std::size_t>(row) * static_cast<std::size_t>(width)
        + static_cast<std::size_t>(column)) * static_cast<std::size_t>(bytesPerPixel);
      if (bytesPerPixel == 1) {
        destination[column] = pixels[at];
      } else {
        destination[column] = static_cast<std::uint8_t>(
          (static_cast<unsigned>(pixels[at]) + pixels[at + 1] + pixels[at + 2]) / 3u);
      }
    }
  }
  e.shelfX += width + 1;
  e.shelfH = (std::max)(e.shelfH, height);
  ++e.rasterized;
  ++e.revision;
  out = &e.glyphs.emplace(key, slot).first->second;
  return RasterResult::Ok;
}

struct Item {
  int face = -1;
  std::uint16_t glyph = 0;
  float advance = 0.0f;
  bool missing = false;
  bool fallback = false;
};

// Items for one string at one size, with kerning folded into the advances.
std::vector<Item> shapeLine(Engine& e, int slot, const std::vector<char32_t>& codepoints, int px) {
  std::vector<Item> items;
  items.reserve(codepoints.size());
  for (char32_t cp : codepoints) {
    if (cp < 0x20 || cp == 0x7F) continue;  // controls are not drawn
    if (cp == 0x00A0) cp = 0x20;
    Mapping mapping = resolve(e, slot, cp);
    Item item;
    if (mapping.face < 0) {
      item.missing = true;
      mapping = resolve(e, slot, U'?');
      if (mapping.face < 0) continue;
    }
    item.face = mapping.face;
    item.glyph = mapping.glyph;
    item.fallback = mapping.fallback && !item.missing;
    item.advance = advanceOf(e, item.face, item.glyph, px);
    items.push_back(item);
  }
  for (std::size_t i = 0; i + 1 < items.size(); ++i) {
    if (items[i].face != items[i + 1].face) continue;
    Face& face = e.faces[static_cast<std::size_t>(items[i].face)];
    if (!face.kerning) continue;
    const UINT16 pair[2] = {items[i].glyph, items[i + 1].glyph};
    INT32 adjustments[2] = {0, 0};
    if (SUCCEEDED(face.face1->GetKerningPairAdjustments(2, pair, adjustments))) {
      items[i].advance += static_cast<float>(adjustments[0]) * static_cast<float>(px)
        / static_cast<float>(face.metrics.designUnitsPerEm);
    }
  }
  return items;
}

float widthOf(const std::vector<Item>& items) {
  float width = 0.0f;
  for (const Item& item : items) width += item.advance;
  return width;
}

int pixelsOf(float pixelSize) {
  const float clamped = (std::min)(kHudTextMaxPixels, (std::max)(kHudTextMinPixels, pixelSize));
  return static_cast<int>(std::lround(clamped));
}

void lineMetrics(Engine& e, int slot, int px, float& ascent, float& lineHeight) {
  const Face& face = e.faces[static_cast<std::size_t>(e.chain[slot].front())];
  const float scale = static_cast<float>(px) / static_cast<float>(face.metrics.designUnitsPerEm);
  ascent = std::ceil(static_cast<float>(face.metrics.ascent) * scale);
  lineHeight = ascent + std::ceil(static_cast<float>(face.metrics.descent) * scale);
}

// attempt: 0 on entry. A run that fills the atlas clears it and starts again
// once; a run that fills a CLEAN atlas (one line larger than 1024x1024 of glyphs)
// keeps the glyphs it placed and drops the rest rather than looping.
HudTextRun layoutLocked(Engine& e, const std::string& utf8, HudTextFont font, float pixelSize,
    float x, float top, HudTextAlign align, float maxWidth, bool rasterizeGlyphs, int attempt = 0) {
  HudTextRun run;
  initialize(e);
  if (!e.available || font == HudTextFont::Bitmap8) return run;
  const int slot = slotOf(font);
  const int px = pixelsOf(pixelSize);
  run.available = true;
  run.pixelSize = px;
  lineMetrics(e, slot, px, run.ascent, run.lineHeight);
  std::vector<Item> items = shapeLine(e, slot, decodeUtf8(utf8), px);
  float width = widthOf(items);
  if (maxWidth > 0.0f && width > maxWidth + 0.01f) {
    std::vector<Item> ellipsis = shapeLine(e, slot, {kEllipsis}, px);
    if (ellipsis.empty() || ellipsis.front().missing) ellipsis = shapeLine(e, slot, {U'.', U'.', U'.'}, px);
    const float ellipsisWidth = widthOf(ellipsis);
    while (!items.empty() && width + ellipsisWidth > maxWidth + 0.01f) {
      width -= items.back().advance;
      items.pop_back();
    }
    // A name cut at a space reads as "Name …"; drop the space instead.
    while (!items.empty() && items.back().advance > 0.0f && !items.back().missing) {
      const Mapping space = resolve(e, slot, U' ');
      if (space.face == items.back().face && space.glyph == items.back().glyph) {
        width -= items.back().advance;
        items.pop_back();
      } else {
        break;
      }
    }
    items.insert(items.end(), ellipsis.begin(), ellipsis.end());
    width += ellipsisWidth;
    run.ellipsized = true;
  }
  run.width = width;
  float left = x;
  if (align == HudTextAlign::Center) left = x - width * 0.5f;
  else if (align == HudTextAlign::Right) left = x - width;
  run.left = left;
  run.codepoints = items.size();
  for (const Item& item : items) {
    if (item.missing) ++run.missing;
    if (item.fallback) ++run.fromFallback;
  }
  if (!rasterizeGlyphs) return run;
  const float baseline = std::floor(top + run.ascent + 0.5f);
  const float inverse = 1.0f / static_cast<float>(kHudTextAtlasSize);
  float pen = left;
  run.quads.reserve(items.size());
  for (const Item& item : items) {
    const float penPixel = std::floor(pen + 0.5f);
    const GlyphSlot* glyph = nullptr;
    RasterResult result = rasterize(e, item.face, item.glyph, px, glyph);
    if (result == RasterResult::Full) {
      if (attempt > 0) break;
      // Anything already emitted points at glyphs the reset removes, so the run
      // starts again; the renderer sees the generation move and re-lays out the
      // other text of the frame too.
      resetAtlas(e);
      return layoutLocked(e, utf8, font, pixelSize, x, top, align, maxWidth, true, attempt + 1);
    }
    if (result == RasterResult::Ok && glyph && glyph->hasBitmap) {
      HudTextQuad quad;
      quad.x0 = penPixel + static_cast<float>(glyph->offsetX);
      quad.y0 = baseline + static_cast<float>(glyph->offsetY);
      quad.x1 = quad.x0 + static_cast<float>(glyph->w);
      quad.y1 = quad.y0 + static_cast<float>(glyph->h);
      quad.u0 = static_cast<float>(glyph->x) * inverse;
      quad.v0 = static_cast<float>(glyph->y) * inverse;
      quad.u1 = static_cast<float>(glyph->x + glyph->w) * inverse;
      quad.v1 = static_cast<float>(glyph->y + glyph->h) * inverse;
      run.quads.push_back(quad);
    }
    pen += item.advance;
  }
  e.missing += run.missing;
  e.fromFallback += run.fromFallback;
  run.generation = e.generation;
  return run;
}

}  // namespace

const char* hudTextFontName(HudTextFont font) {
  switch (font) {
    case HudTextFont::Bitmap8: return "bitmap8";
    case HudTextFont::UiSemiBold: return "ui-semibold";
    case HudTextFont::UiBold: return "ui-bold";
  }
  return "bitmap8";
}

const char* hudTextAlignName(HudTextAlign align) {
  switch (align) {
    case HudTextAlign::Left: return "left";
    case HudTextAlign::Center: return "center";
    case HudTextAlign::Right: return "right";
  }
  return "left";
}

HudTextRun hudTextLayout(const std::string& utf8, HudTextFont font, float pixelSize,
    float x, float top, HudTextAlign align, float maxWidth) {
  Engine& e = engine();
  std::lock_guard<std::mutex> lock(e.mutex);
  return layoutLocked(e, utf8, font, pixelSize, x, top, align, maxWidth, true);
}

float hudTextMeasure(const std::string& utf8, HudTextFont font, float pixelSize) {
  Engine& e = engine();
  std::lock_guard<std::mutex> lock(e.mutex);
  return layoutLocked(e, utf8, font, pixelSize, 0.0f, 0.0f, HudTextAlign::Left, 0.0f, false).width;
}

float hudTextLineHeight(HudTextFont font, float pixelSize) {
  Engine& e = engine();
  std::lock_guard<std::mutex> lock(e.mutex);
  initialize(e);
  if (!e.available || font == HudTextFont::Bitmap8) return 0.0f;
  float ascent = 0.0f, lineHeight = 0.0f;
  lineMetrics(e, slotOf(font), pixelsOf(pixelSize), ascent, lineHeight);
  return lineHeight;
}

bool hudTextAvailable() {
  Engine& e = engine();
  std::lock_guard<std::mutex> lock(e.mutex);
  initialize(e);
  return e.available;
}

const std::uint8_t* hudTextAtlasPixels() {
  Engine& e = engine();
  std::lock_guard<std::mutex> lock(e.mutex);
  return e.available ? e.atlas.data() : nullptr;
}

std::uint64_t hudTextAtlasRevision() {
  Engine& e = engine();
  std::lock_guard<std::mutex> lock(e.mutex);
  return e.revision;
}

std::uint32_t hudTextAtlasGeneration() {
  Engine& e = engine();
  std::lock_guard<std::mutex> lock(e.mutex);
  return e.generation;
}

void hudTextSetPreferredFamily(const std::wstring& family) {
  Engine& e = engine();
  std::lock_guard<std::mutex> lock(e.mutex);
  if (e.initialized) { ++e.lateFamilyRequests; return; }
  e.preferred = family;
}

HudTextStats hudTextStats() {
  Engine& e = engine();
  std::lock_guard<std::mutex> lock(e.mutex);
  initialize(e);
  HudTextStats stats;
  stats.available = e.available;
  stats.reason = e.reason;
  if (!e.chain[1].empty()) stats.semiBoldFamily = narrow(e.faces[static_cast<std::size_t>(e.chain[1].front())].family);
  if (!e.chain[2].empty()) stats.boldFamily = narrow(e.faces[static_cast<std::size_t>(e.chain[2].front())].family);
  stats.faces = e.faces.size();
  stats.glyphsCached = e.glyphs.size();
  stats.rasterized = e.rasterized;
  stats.resets = e.resets;
  stats.missing = e.missing;
  stats.fromFallback = e.fromFallback;
  stats.lateFamilyRequests = e.lateFamilyRequests;
  stats.atlasUsedHeight = e.shelfY + e.shelfH;
  stats.revision = e.revision;
  stats.generation = e.generation;
  return stats;
}

}  // namespace tf2::native
