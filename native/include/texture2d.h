#pragma once

#include <Windows.h>
#include <d3d11.h>
#include <cstdint>
#include <string>
#include <vector>
#include <wrl/client.h>

namespace tf2::native {

class Texture2D {
public:
  // `maxBytes` is the guard against a runaway allocation. It defaults to the
  // 64 MiB the ordinary textures have always been held to; the world atlas
  // passes its own budget, because a 64-tile atlas of 512-texel tiles is
  // 67.6 MiB by construction and refusing it silently downgraded the whole
  // world to a single material texture.
  bool create(ID3D11Device* device, const std::vector<std::uint8_t>& rgba,
    UINT width, UINT height, std::string* error = nullptr,
    std::uint64_t maxBytes = 64ull * 1024ull * 1024ull);
  void reset();
  ID3D11ShaderResourceView* view() const { return view_.Get(); }
  UINT width() const { return width_; }
  UINT height() const { return height_; }

private:
  Microsoft::WRL::ComPtr<ID3D11Texture2D> texture_;
  Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view_;
  UINT width_ = 0;
  UINT height_ = 0;
};

} // namespace tf2::native
