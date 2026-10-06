#include "vtf_texture.h"

#include <algorithm>
#include <array>
#include <cstring>

namespace tf2::native {

namespace {
constexpr std::uint64_t kMaxVtfChainBytes = 64ull * 1024ull * 1024ull;
template <typename T>
bool readValue(const std::vector<std::uint8_t>& bytes, std::size_t offset, T& value) {
  if (offset + sizeof(T) > bytes.size()) return false;
  value = 0;
  for (std::size_t i = 0; i < sizeof(T); ++i) {
    value |= static_cast<T>(bytes[offset + i]) << (i * 8);
  }
  return true;
}

std::uint64_t mipSpan(std::uint32_t format, std::uint32_t width, std::uint32_t height,
    std::uint32_t mipCount, std::uint32_t depth) {
  static constexpr std::array<std::uint32_t, 4> blockFormats = { 13, 14, 15, 20 };
  const bool block = std::find(blockFormats.begin(), blockFormats.end(), format) != blockFormats.end();
  const std::uint32_t blockBytes = (format == 13 || format == 20) ? 8 : 16;
  static constexpr std::array<std::uint32_t, 27> pixelBytes = {
    4, 4, 3, 3, 2, 1, 2, 1, 1, 3, 3, 4, 4, 0, 0, 4, 4, 2, 2, 2,
    0, 2, 2, 4, 8, 2, 4,
  };
  if (!block && format >= pixelBytes.size()) return 0;
  std::uint64_t total = 0;
  std::uint32_t w = width;
  std::uint32_t h = height;
  for (std::uint32_t mip = 0; mip < mipCount; ++mip) {
    if (block) total += static_cast<std::uint64_t>(std::max(1u, (w + 3) / 4))
      * std::max(1u, (h + 3) / 4) * blockBytes;
    else total += static_cast<std::uint64_t>(w) * h * pixelBytes[format];
    w = std::max(1u, w / 2);
    h = std::max(1u, h / 2);
  }
  return total * std::max(1u, depth);
}
}

bool VtfTexture::parse(const std::vector<std::uint8_t>& bytes, std::string* error) {
  header_ = {};
  topMipOffset_ = 0;
  topMipBytes_ = 0;
  auto fail = [&](const char* message) {
    if (error) *error = message;
    return false;
  };
  if (bytes.size() < 16 || std::memcmp(bytes.data(), "VTF\0", 4) != 0) return fail("invalid VTF signature");
  if (!readValue(bytes, 4, header_.major) || !readValue(bytes, 8, header_.minor)
      || !readValue(bytes, 12, header_.headerSize)) return fail("truncated VTF header");
  if (header_.major != 7 || header_.minor > 5 || header_.headerSize < 64
      || header_.headerSize > bytes.size()) return fail("unsupported VTF version or header size");
  if (!readValue(bytes, 16, header_.width) || !readValue(bytes, 18, header_.height)
      || !readValue(bytes, 20, header_.flags) || !readValue(bytes, 24, header_.frames)
      || !readValue(bytes, 52, header_.format)) return fail("truncated VTF fields");
  header_.mipCount = bytes[56];
  if (!readValue(bytes, 57, header_.lowResFormat)) return fail("truncated VTF low-res format");
  header_.lowResWidth = bytes[61];
  header_.lowResHeight = bytes[62];
  if (header_.major == 7 && header_.minor >= 2
      && !readValue(bytes, 63, header_.depth)) return fail("truncated VTF depth");
  if (header_.major == 7 && header_.minor >= 3) {
    if (!readValue(bytes, 68, header_.resourceCount) || header_.resourceCount > 64) {
      return fail("invalid VTF resource count");
    }
  }
  if (header_.width == 0 || header_.height == 0 || header_.mipCount == 0 || header_.mipCount > 16) {
    return fail("invalid VTF dimensions or mip count");
  }
  if (header_.resourceCount) {
    const std::size_t tableStart = header_.headerSize - header_.resourceCount * 8u;
    if (tableStart < 64 || tableStart + header_.resourceCount * 8u > bytes.size()) return fail("invalid VTF resource table");
    header_.resourceTags.reserve(header_.resourceCount);
    std::uint32_t highResOffset = 0;
    bool foundHighRes = false;
    for (std::uint32_t index = 0; index < header_.resourceCount; ++index) {
      std::uint32_t tag = 0;
      std::uint32_t offset = 0;
      if (!readValue(bytes, tableStart + index * 8u, tag)
          || !readValue(bytes, tableStart + index * 8u + 4, offset)) return fail("truncated VTF resource");
      header_.resourceTags.push_back(tag);
      if (tag == 0x30) { highResOffset = offset; foundHighRes = true; }
    }
    header_.mipStart = foundHighRes ? highResOffset : header_.headerSize;
  } else {
    header_.mipStart = header_.headerSize;
  }
  header_.chainBytes = mipSpan(header_.format, header_.width, header_.height,
    header_.mipCount, header_.depth);
  if (header_.chainBytes > kMaxVtfChainBytes) return fail("VTF mip chain exceeds 64 MiB limit");
  if (header_.chainBytes == 0 || header_.mipStart + header_.chainBytes > bytes.size()) {
    return fail("VTF image data is out of bounds");
  }
  topMipBytes_ = mipSpan(header_.format, header_.width, header_.height, 1, header_.depth);
  if (topMipBytes_ > header_.chainBytes) return fail("invalid VTF mip chain");
  topMipOffset_ = header_.mipStart + header_.chainBytes - topMipBytes_;
  return true;
}

namespace {
void decodeColor565(std::uint16_t value, std::uint8_t* out) {
  out[0] = static_cast<std::uint8_t>(((value >> 11) & 31) * 255 / 31);
  out[1] = static_cast<std::uint8_t>(((value >> 5) & 63) * 255 / 63);
  out[2] = static_cast<std::uint8_t>((value & 31) * 255 / 31);
  out[3] = 255;
}

void writePixel(std::vector<std::uint8_t>& output, std::uint32_t width,
    std::uint32_t x, std::uint32_t y, const std::uint8_t* rgba) {
  if (x >= width) return;
  const auto index = (static_cast<std::size_t>(y) * width + x) * 4;
  if (index + 4 > output.size()) return;
  std::memcpy(output.data() + index, rgba, 4);
}

void decodeColorBlock(const std::uint8_t* block, bool oneBitAlpha,
    std::vector<std::uint8_t>& output, std::uint32_t width, std::uint32_t height,
    std::uint32_t blockX, std::uint32_t blockY, const std::uint8_t* alpha) {
  const auto c0 = static_cast<std::uint16_t>(block[0] | (block[1] << 8));
  const auto c1 = static_cast<std::uint16_t>(block[2] | (block[3] << 8));
  std::array<std::array<std::uint8_t, 4>, 4> colors{};
  decodeColor565(c0, colors[0].data());
  decodeColor565(c1, colors[1].data());
  if (c0 > c1 || !oneBitAlpha) {
    for (int channel = 0; channel < 3; ++channel) {
      colors[2][channel] = static_cast<std::uint8_t>((2 * colors[0][channel] + colors[1][channel]) / 3);
      colors[3][channel] = static_cast<std::uint8_t>((colors[0][channel] + 2 * colors[1][channel]) / 3);
    }
    colors[2][3] = colors[3][3] = 255;
  } else {
    for (int channel = 0; channel < 3; ++channel) colors[2][channel] = static_cast<std::uint8_t>((colors[0][channel] + colors[1][channel]) / 2);
    colors[2][3] = 255;
    colors[3] = { 0, 0, 0, 0 };
  }
  const auto indices = static_cast<std::uint32_t>(block[4])
    | (static_cast<std::uint32_t>(block[5]) << 8)
    | (static_cast<std::uint32_t>(block[6]) << 16)
    | (static_cast<std::uint32_t>(block[7]) << 24);
  for (std::uint32_t y = 0; y < 4; ++y) for (std::uint32_t x = 0; x < 4; ++x) {
    const auto index = (indices >> (2 * (y * 4 + x))) & 3;
    auto pixel = colors[index];
    if (alpha) pixel[3] = alpha[y * 4 + x];
    if (blockX + x < width && blockY + y < height) writePixel(output, width, blockX + x, blockY + y, pixel.data());
  }
}

void decodeAlphaDxt5(const std::uint8_t* block, std::array<std::uint8_t, 16>& alpha) {
  std::array<std::uint8_t, 8> table{};
  table[0] = block[0]; table[1] = block[1];
  if (table[0] > table[1]) {
    for (int i = 1; i < 7; ++i) table[i + 1] = static_cast<std::uint8_t>(((7 - i) * table[0] + i * table[1]) / 7);
  } else {
    for (int i = 1; i < 5; ++i) table[i + 1] = static_cast<std::uint8_t>(((5 - i) * table[0] + i * table[1]) / 5);
    table[6] = 0; table[7] = 255;
  }
  std::uint64_t bits = 0;
  for (int i = 0; i < 6; ++i) bits |= static_cast<std::uint64_t>(block[2 + i]) << (8 * i);
  for (int i = 0; i < 16; ++i) alpha[i] = table[(bits >> (3 * i)) & 7];
}
}

std::vector<std::uint8_t> VtfTexture::decodeRgba(const std::vector<std::uint8_t>& bytes,
    std::string* error) const {
  const auto fail = [&](const char* message) {
    if (error) *error = message;
    return std::vector<std::uint8_t>{};
  };
  if (header_.isCubemap()) return fail("VTF cubemap face order is unsupported by the 2D decoder");
  const auto data = topMip(bytes);
  if (data.empty()) return fail("VTF top mip is unavailable");
  const std::uint32_t width = header_.width;
  const std::uint32_t height = header_.height;
  std::vector<std::uint8_t> output(static_cast<std::size_t>(width) * height * 4);
  const auto requireBytes = [&](std::size_t bytesPerPixel) {
    return data.size() >= static_cast<std::size_t>(width) * height * bytesPerPixel;
  };
  if (header_.format == 0 || header_.format == 1 || header_.format == 11 || header_.format == 12) {
    if (!requireBytes(4)) return fail("VTF 32-bit data is truncated");
    for (std::size_t i = 0, pixel = 0; pixel < output.size(); i += 4, pixel += 4) {
      if (header_.format == 0) { output[pixel] = data[i]; output[pixel + 1] = data[i + 1]; output[pixel + 2] = data[i + 2]; output[pixel + 3] = data[i + 3]; }
      else if (header_.format == 1) { output[pixel] = data[i + 3]; output[pixel + 1] = data[i + 2]; output[pixel + 2] = data[i + 1]; output[pixel + 3] = data[i]; }
      else if (header_.format == 11) { output[pixel] = data[i + 1]; output[pixel + 1] = data[i + 2]; output[pixel + 2] = data[i + 3]; output[pixel + 3] = data[i]; }
      else { output[pixel] = data[i + 2]; output[pixel + 1] = data[i + 1]; output[pixel + 2] = data[i]; output[pixel + 3] = data[i + 3]; }
    }
    return output;
  }
  if (header_.format == 2 || header_.format == 3 || header_.format == 9 || header_.format == 10) {
    if (!requireBytes(3)) return fail("VTF 24-bit data is truncated");
    for (std::size_t i = 0, pixel = 0; pixel < output.size(); i += 3, pixel += 4) {
      const bool bgr = header_.format == 3 || header_.format == 10;
      output[pixel] = data[i + (bgr ? 2 : 0)]; output[pixel + 1] = data[i + 1]; output[pixel + 2] = data[i + (bgr ? 0 : 2)];
      output[pixel + 3] = ((header_.format == 9 || header_.format == 10) && output[pixel] == 0 && output[pixel + 1] == 0 && output[pixel + 2] == 255) ? 0 : 255;
    }
    return output;
  }
  if (header_.format == 4 || header_.format == 16 || header_.format == 17) {
    if (!requireBytes(2)) return fail("VTF 16-bit data is truncated");
    for (std::size_t i = 0, pixel = 0; pixel < output.size(); i += 2, pixel += 4) {
      const std::uint16_t value = static_cast<std::uint16_t>(data[i] | (data[i + 1] << 8));
      if (header_.format == 4) { std::uint8_t colour[4]{}; decodeColor565(value, colour); output[pixel] = colour[0]; output[pixel + 1] = colour[1]; output[pixel + 2] = colour[2]; output[pixel + 3] = 255; }
      else if (header_.format == 16) { output[pixel] = static_cast<std::uint8_t>(((value >> 10) & 31) * 255 / 31); output[pixel + 1] = static_cast<std::uint8_t>(((value >> 5) & 31) * 255 / 31); output[pixel + 2] = static_cast<std::uint8_t>((value & 31) * 255 / 31); output[pixel + 3] = (value & 1) ? 255 : 0; }
      else { output[pixel] = static_cast<std::uint8_t>(((value >> 8) & 15) * 17); output[pixel + 1] = static_cast<std::uint8_t>(((value >> 4) & 15) * 17); output[pixel + 2] = static_cast<std::uint8_t>((value & 15) * 17); output[pixel + 3] = static_cast<std::uint8_t>(((value >> 12) & 15) * 17); }
    }
    return output;
  }
  if (header_.format == 5 || header_.format == 6 || header_.format == 8) {
    const std::size_t bytesPerPixel = header_.format == 6 ? 2 : 1;
    if (!requireBytes(bytesPerPixel)) return fail("VTF intensity data is truncated");
    for (std::size_t i = 0, pixel = 0; pixel < output.size(); i += bytesPerPixel, pixel += 4) {
      const std::uint8_t intensity = data[i]; output[pixel] = intensity; output[pixel + 1] = intensity; output[pixel + 2] = intensity; output[pixel + 3] = header_.format == 8 ? intensity : (header_.format == 6 ? data[i + 1] : 255);
    }
    return output;
  }
  if (header_.format != 13 && header_.format != 14 && header_.format != 15 && header_.format != 20) {
    return fail("VTF format is not supported by the native decoder");
  }
  const std::size_t blockBytes = header_.format == 13 || header_.format == 20 ? 8 : 16;
  const std::size_t expected = static_cast<std::size_t>((width + 3) / 4) * ((height + 3) / 4) * blockBytes;
  if (data.size() < expected) return fail("VTF block data is truncated");
  std::size_t cursor = 0;
  for (std::uint32_t by = 0; by < height; by += 4) for (std::uint32_t bx = 0; bx < width; bx += 4) {
    std::array<std::uint8_t, 16> alpha{};
    const std::uint8_t* color = data.data() + cursor;
    if (header_.format == 14) {
      for (int i = 0; i < 16; ++i) alpha[i] = static_cast<std::uint8_t>(((data[cursor + i / 2] >> ((i & 1) * 4)) & 15) * 17);
      color += 8;
    } else if (header_.format == 15) {
      decodeAlphaDxt5(data.data() + cursor, alpha);
      color += 8;
    }
    decodeColorBlock(color, header_.format == 20, output, width, height, bx, by,
      header_.format == 13 || header_.format == 20 ? nullptr : alpha.data());
    cursor += blockBytes;
  }
  return output;
}

std::vector<std::uint8_t> VtfTexture::topMip(const std::vector<std::uint8_t>& bytes) const {
  if (topMipOffset_ + topMipBytes_ > bytes.size()) return {};
  return std::vector<std::uint8_t>(bytes.begin() + static_cast<std::ptrdiff_t>(topMipOffset_),
    bytes.begin() + static_cast<std::ptrdiff_t>(topMipOffset_ + topMipBytes_));
}

} // namespace tf2::native
