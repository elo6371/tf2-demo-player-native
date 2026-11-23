#pragma once
// F0 (2026-11-23, docs/UI-SHELL-DESIGN-2026-11-22.md section 7): proportional,
// antialiased HUD text, rasterised by DirectWrite from the fonts Windows ships.
//
// WHY A SECOND TEXT PATH
//   The T67 font (hud_font.h) is 8x8 ASCII. Scaled up it is a mosaic, and every
//   non-ASCII name -- Cyrillic and CJK player names are common -- becomes the
//   fallback box. The broadcast HUD needs 14-28 px text that reads like the
//   game's own HUD. This module is that text; hud_font.h is left exactly as it
//   was, because gates compare frames against its table pixel for pixel.
//
// WHAT IT IS
//   CPU-only: DirectWrite rasterises one glyph at a time into a grayscale R8
//   atlas held in memory, and the renderer uploads the atlas when its revision
//   changes. No D3D type appears here, so a probe can lay out, measure and
//   rasterise text on a machine with no device (minimap_probe --hud-text-test).
//
//   Fonts: the first family the system has of
//     "Bahnschrift SemiCondensed", "Bahnschrift", "Segoe UI"
//   (or --hud-font-family <name> in front of them), then a per-character
//   fallback chain (Segoe UI, Microsoft YaHei UI, Microsoft JhengHei UI,
//   Yu Gothic UI, Malgun Gothic, Nirmala UI, Segoe UI Symbol, Arial). Nothing is
//   bundled or redistributed: every face is read from the user's own Windows.
//
// DETERMINISM
//   Same string, font, size and position -> same quads and same atlas bytes on
//   one machine: every pen position is snapped to a whole pixel before the glyph
//   is placed, and nothing reads a clock. Across machines the pixels depend on
//   the installed font versions, which is why no gate pins this path's pixels
//   and the T67 path stays the one the frame-comparison gates use.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace tf2::native {

enum class HudTextFont : std::uint8_t {
  Bitmap8 = 0,     // T67's embedded 8x8 font (hud_font.h); not handled here
  UiSemiBold = 1,  // weight 600
  UiBold = 2,      // weight 700
};
enum class HudTextAlign : std::uint8_t { Left = 0, Center = 1, Right = 2 };

const char* hudTextFontName(HudTextFont font);
const char* hudTextAlignName(HudTextAlign align);

constexpr int kHudTextAtlasSize = 1024;   // R8, square
constexpr float kHudTextMinPixels = 6.0f;
constexpr float kHudTextMaxPixels = 160.0f;

// One glyph, in back-buffer pixels, with its crop of the atlas (0..1).
struct HudTextQuad {
  float x0 = 0.0f, y0 = 0.0f, x1 = 0.0f, y1 = 0.0f;
  float u0 = 0.0f, v0 = 0.0f, u1 = 0.0f, v1 = 0.0f;
};

struct HudTextRun {
  bool available = false;     // false: no DirectWrite / no face; quads empty
  std::vector<HudTextQuad> quads;
  float left = 0.0f;          // x of the first glyph's pen position, after alignment
  float width = 0.0f;         // advance width of what is drawn (after any ellipsis)
  float lineHeight = 0.0f;    // ascent + descent of the primary face, whole pixels
  float ascent = 0.0f;        // baseline = top + ascent
  int pixelSize = 0;          // the em size actually used (rounded, clamped)
  std::size_t codepoints = 0; // drawn codepoints (after ellipsis, before spaces drop)
  std::size_t missing = 0;    // codepoints no face in the chain has (drawn as '?')
  std::size_t fromFallback = 0; // codepoints taken from a face other than the primary
  bool ellipsized = false;
  std::uint32_t generation = 0; // atlas generation the quads' uv belong to
};

// Lays out one line. top is the top of the line box (not the baseline). maxWidth
// > 0 shortens the text and appends an ellipsis so the advance fits. Rasterises
// any glyph not yet in the atlas.
HudTextRun hudTextLayout(const std::string& utf8, HudTextFont font, float pixelSize,
  float x, float top, HudTextAlign align, float maxWidth);
// The advance width hudTextLayout would produce, without touching the atlas.
float hudTextMeasure(const std::string& utf8, HudTextFont font, float pixelSize);
float hudTextLineHeight(HudTextFont font, float pixelSize);

bool hudTextAvailable();
// R8, kHudTextAtlasSize x kHudTextAtlasSize, row-major; nullptr when unavailable.
const std::uint8_t* hudTextAtlasPixels();
// Bumped on every write to the atlas; the renderer uploads when it changes.
std::uint64_t hudTextAtlasRevision();
// Bumped when the atlas fills up and is cleared. Quads from an older generation
// point at glyphs that are gone; the renderer lays its text out again.
std::uint32_t hudTextAtlasGeneration();

// Put a family in front of the built-in list (--hud-font-family). Must be called
// before the first layout; later calls are ignored and counted.
void hudTextSetPreferredFamily(const std::wstring& family);

struct HudTextStats {
  bool available = false;
  std::string reason;             // "ok", "no-dwrite-factory2", "no-font-collection", "no-face"
  std::string semiBoldFamily;     // the primary family actually loaded, per font
  std::string boldFamily;
  std::size_t faces = 0;          // faces loaded (primary + fallbacks, both weights)
  std::size_t glyphsCached = 0;
  std::size_t rasterized = 0;     // rasterisations since start (includes re-rasterising after a reset)
  std::size_t resets = 0;         // atlas full -> cleared
  std::size_t missing = 0;        // codepoints laid out with no face (cumulative)
  std::size_t fromFallback = 0;   // codepoints laid out from a fallback face (cumulative)
  std::size_t lateFamilyRequests = 0;
  int atlasUsedHeight = 0;        // rows of the atlas holding glyphs
  std::uint64_t revision = 0;
  std::uint32_t generation = 0;
};
HudTextStats hudTextStats();

}  // namespace tf2::native
