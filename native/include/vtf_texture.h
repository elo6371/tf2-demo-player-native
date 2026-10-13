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
  // One face of a cubemap VTF, decoded as a plain 2D RGBA image.
  //
  // The face order is Source's own: 0=+X(rt) 1=-X(lf) 2=+Y(bk) 3=-Y(ft)
  // 4=+Z(up) 5=-Z(dn), the same order D3D11's TextureCube uses and the same
  // order world_material's suffix table spells out.
  //
  // The pixel work is deliberately NOT a second decoder: this rewrites the
  // header to describe the one face as a 2D texture (ENVMAP cleared, mip count
  // 1) and hands the slice to the existing decodeRgba, so a cubemap face is
  // decoded by exactly the code path every other texture in the product is
  // decoded by. A face that fails to decode fails the same way a flat texture
  // does, with the same message shape.
  //
  // Measured on this install's real cubemaps (cp_snakewater_final1's
  // c<X>_<Y>_<Z>.vtf, 32x32 DXT1, 6 mips, depth 1): the file holds seven
  // face-sized chains and the first six are the faces, so face f starts at
  // mipStart + f * chainBytes. The seventh chain is not interpreted here.
  std::vector<std::uint8_t> decodeCubeFaceRgba(const std::vector<std::uint8_t>& bytes,
    int face, std::uint32_t& width, std::uint32_t& height, std::string* error = nullptr) const;

private:
  VtfHeader header_{};
  std::uint64_t topMipOffset_ = 0;
  std::uint64_t topMipBytes_ = 0;
};

} // namespace tf2::native
