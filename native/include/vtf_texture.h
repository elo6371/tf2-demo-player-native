#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace tf2::native {

struct VtfHeader {
  static constexpr std::uint32_t TextureFlagsEnvMap = 0x00004000u;
  std::uint32_t major = 0;
  std::uint32_t minor = 0;
  std::uint32_t headerSize = 0;
  std::uint16_t width = 0;
  std::uint16_t height = 0;
  std::uint32_t flags = 0;
  std::uint16_t frames = 0;
  std::uint32_t format = 0;
  std::uint8_t mipCount = 0;
  std::uint32_t lowResFormat = 0;
  std::uint8_t lowResWidth = 0;
  std::uint8_t lowResHeight = 0;
  std::uint16_t depth = 1;
  std::uint32_t resourceCount = 0;
  std::vector<std::uint32_t> resourceTags;
  std::uint64_t mipStart = 0;
  std::uint64_t chainBytes = 0;
  bool isCubemap() const { return (flags & TextureFlagsEnvMap) != 0; }
};

class VtfTexture {
public:
  bool parse(const std::vector<std::uint8_t>& bytes, std::string* error = nullptr);
  const VtfHeader& header() const { return header_; }
  std::vector<std::uint8_t> topMip(const std::vector<std::uint8_t>& bytes) const;
  std::vector<std::uint8_t> decodeRgba(const std::vector<std::uint8_t>& bytes,
    std::string* error = nullptr) const;

private:
  VtfHeader header_{};
  std::uint64_t topMipOffset_ = 0;
  std::uint64_t topMipBytes_ = 0;
};

} // namespace tf2::native
