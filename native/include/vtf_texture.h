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
  // The header's "first frame" field. Only interesting for cubemaps: the
  // format's own rule is that an environment map is six-faced unless the
  // version is below 7.5 and this field is -1, in which case it is seven-faced.
  std::int16_t firstFrame = 0;
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
  // Face slots the image data holds, MEASURED from the file rather than read
  // from a field: one face's whole mip chain is faceChainBytes, and the image
  // section divided by it is how many faces are really there. The two can
  // disagree -- every cubemap cp_snakewater_final1 ships carries seven face
  // slots while the header's rule above predicts six -- and the file is what a
  // decoder has to walk, so the file is what is counted.
  std::uint32_t faceCount = 1;
  std::uint64_t faceChainBytes = 0;
  std::uint64_t mip0Bytes = 0;
  // Offset and size of one face's largest mip level. The layout is the
  // format's: for each mip (smallest to largest), for each face, so the top
  // mips of every face sit together at the end of the image section and face f
  // of mip 0 is at mip0Offset + f * mip0Bytes.
  std::uint64_t mip0Offset = 0;
  std::uint64_t imageBytes = 0;
  bool isCubemap() const { return (flags & TextureFlagsEnvMap) != 0; }
  // What the header's own rule predicts, for comparison against faceCount.
  std::uint32_t declaredFaceCount() const {
    if (!isCubemap()) return 1;
    return (minor < 5 && firstFrame == -1) ? 7 : 6;
  }
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
  // The slice arithmetic is the format's layout, not an assumption:
  //
  //   for each mip (smallest to largest)
  //     for each face (first to last)
  //
  // so face f's mip 0 begins at mip0Offset + f * mip0Bytes. An earlier version
  // of this function assumed one face-sized chain per face starting at
  // mipStart + f * chainBytes, which is a different arrangement that this
  // install's files do not use: decoding a real 7-face cubemap that way returns
  // each face's SMALLER MIPS laid out as if they were the whole face, and the
  // fixture notices because face f comes back as a colour nobody wrote.
  std::vector<std::uint8_t> decodeCubeFaceRgba(const std::vector<std::uint8_t>& bytes,
    int face, std::uint32_t& width, std::uint32_t& height, std::string* error = nullptr) const;

private:
  VtfHeader header_{};
  std::uint64_t topMipOffset_ = 0;
  std::uint64_t topMipBytes_ = 0;
};

} // namespace tf2::native
