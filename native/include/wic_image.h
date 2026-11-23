#pragma once

// E11 (2026-11-17): PNG decoding for the map basemap.
//
// WHY WIC AND NOT A LIBRARY
//   The product has never carried an image decoder and adding stb_image/lodepng
//   would add a third-party dependency to the build. Windows Imaging Component
//   is an OS component (windowscodecs.dll), already installed on every machine
//   the product runs on, so the decoder is a link, not a vendored file.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace tf2::native {

// One decoded image, top row first, straight RGBA8. A decoder that returns
// half-filled data is worse than one that fails, so `valid()` is checked by
// every caller before the pixels are handed to the GPU.
struct DecodedImage {
  int width = 0;
  int height = 0;
  std::vector<std::uint8_t> rgba;

  bool valid() const {
    return width > 0 && height > 0
        && rgba.size() == static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u;
  }
};

// Decodes any format WIC knows (PNG here). On failure `error` is a short token
// such as "no-factory", "decoder", "frame", "convert" or "copy" -- never an
// empty string -- so a trace can say why the panel has no basemap instead of
// leaving a black rectangle.
bool decodeImageFile(const std::string& utf8Path, DecodedImage& image, std::string& error);

}  // namespace tf2::native
