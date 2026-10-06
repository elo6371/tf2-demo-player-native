#include "texture2d.h"

#include <limits>

namespace tf2::native {

namespace {
constexpr std::uint64_t kMaxTextureBytes = 64ull * 1024ull * 1024ull;
}

bool Texture2D::create(ID3D11Device* device, const std::vector<std::uint8_t>& rgba,
    UINT width, UINT height, std::string* error) {
  reset();
  const auto fail = [&](const char* message) {
    if (error) *error = message;
    reset();
    return false;
  };
  if (!device || width == 0 || height == 0) return fail("invalid D3D11 texture dimensions");
  const auto rowBytes = static_cast<std::uint64_t>(width) * 4;
  const auto expected = rowBytes * height;
  if (rowBytes > std::numeric_limits<UINT>::max() || expected != rgba.size()) {
    return fail("RGBA texture buffer size does not match dimensions");
  }
  if (expected > kMaxTextureBytes) return fail("RGBA texture exceeds 64 MiB limit");
  D3D11_TEXTURE2D_DESC description{};
  description.Width = width;
  description.Height = height;
  description.MipLevels = 1;
  description.ArraySize = 1;
  description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  description.SampleDesc.Count = 1;
  description.Usage = D3D11_USAGE_IMMUTABLE;
  description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
  D3D11_SUBRESOURCE_DATA initial{};
  initial.pSysMem = rgba.data();
  initial.SysMemPitch = static_cast<UINT>(rowBytes);
  if (FAILED(device->CreateTexture2D(&description, &initial, texture_.GetAddressOf()))) {
    return fail("D3D11 texture creation failed");
  }
  D3D11_SHADER_RESOURCE_VIEW_DESC viewDescription{};
  viewDescription.Format = description.Format;
  viewDescription.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
  viewDescription.Texture2D.MipLevels = 1;
  if (FAILED(device->CreateShaderResourceView(texture_.Get(), &viewDescription, view_.GetAddressOf()))) {
    return fail("D3D11 shader resource view creation failed");
  }
  width_ = width;
  height_ = height;
  return true;
}

void Texture2D::reset() {
  view_.Reset();
  texture_.Reset();
  width_ = 0;
  height_ = 0;
}

} // namespace tf2::native
