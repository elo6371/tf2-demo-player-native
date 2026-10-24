#pragma once
// T67: the embedded 8x8 bitmap font the HUD text pass rasterises.
//
// WHY THIS EXISTS
//   T51 could draw only solid/bordered rectangles: the product had no font and
//   deliberately used no GDI, so "text" in a HUD panel was a claim the frame
//   could not show. This module is the smallest thing that makes text a
//   property of the pixels: a fixed, embedded 8x8 glyph table and the R8 atlas
//   the D3D pixel shader samples. It is pure data plus a UTF-8 decoder -- no
//   device, no window, no GDI, no file I/O -- so a gate can compare the pixels
//   on screen against this exact table.
//
// PROVENANCE
//   The glyph table is Daniel Hepper's font8x8_basic (public domain), itself
//   based on Marcel Sondaar's and IBM's public-domain 8x8 VGA fonts. The rows
//   are copied verbatim for U+0020..U+007E; the fallback box is ours.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace tf2::native {

constexpr int kHudFontGlyphWidth = 8;
constexpr int kHudFontGlyphHeight = 8;
constexpr int kHudFontAtlasColumns = 16;
constexpr int kHudFontAtlasRows = 6;
constexpr int kHudFontAtlasWidth = kHudFontGlyphWidth * kHudFontAtlasColumns;   // 128
constexpr int kHudFontAtlasHeight = kHudFontGlyphHeight * kHudFontAtlasRows;    // 48
// Printable ASCII 0x20..0x7E occupies cells 0..94; cell 95 is the fallback.
constexpr int kHudFontPrintableCount = 95;
constexpr int kHudFontFallbackCell = kHudFontPrintableCount;

struct HudGlyph {
  // Rows top-to-bottom; bit 0 is the leftmost pixel.
  unsigned char rows[kHudFontGlyphHeight] = {0, 0, 0, 0, 0, 0, 0, 0};
  int advance = kHudFontGlyphWidth;
};

// 0..94 for a mapped printable ASCII codepoint, -1 otherwise. The caller counts
// the fallback and draws hudFontFallbackGlyph().
int hudFontGlyphIndex(char32_t codepoint);
// index 0..94 is the printable table; anything else returns the fallback.
const HudGlyph& hudFontGlyphByIndex(int index);
const HudGlyph& hudFontFallbackGlyph();
// The table lookup the renderer uses: nullptr when unmapped.
const HudGlyph* hudFontGlyph(char32_t codepoint);
std::size_t hudFontGlyphCount();

// R8 atlas, kHudFontAtlasWidth x kHudFontAtlasHeight, one byte per texel,
// 0 = transparent, 255 = covered. Built once on first use.
const unsigned char* hudFontAtlas();
int hudFontAtlasWidth();
int hudFontAtlasHeight();

// Decodes UTF-8. Invalid sequences become U+FFFD (unmapped, so they exercise
// the same fallback path as a real non-ASCII character).
std::vector<char32_t> hudUtf8Decode(const std::string& text);

} // namespace tf2::native
