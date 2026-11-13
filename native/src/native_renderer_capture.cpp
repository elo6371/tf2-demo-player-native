// Frame capture: back-buffer BMP writer and depth-buffer readback used by the pixel gates.
//
// One translation unit of the renderer split: the bodies were moved verbatim out
// of native_renderer.cpp, so pass order, thresholds and trace text are unchanged.

#include "native_renderer.h"
#include "renderer_internal.h"

#include "entity_model.h"
#include "hud_font.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace tf2::native {


void Renderer::requestFrameCapture(std::wstring path, std::wstring depthPath) {
  capturePath_ = std::move(path);
  captureSucceeded_ = false;
  captureDepthPath_ = std::move(depthPath);
  depthCaptureSucceeded_ = false;
  depthCaptureBytes_ = 0;
  captureError_.clear();
}

// The world depth of the frame that was just written, as a 16-bit PGM (P5,
// top-down, which is the order the staging readback hands rows over in).
//
// Why this exists at all: --world-depth is an 8-bit VIEW and saturates for
// everything past a few units -- T26 asserts that saturation, because a depth
// image that saturates is not evidence and the gate says so out loud. The
// question section 5 of outline-check.sh has to answer is finer than that: "is
// the world geometry in front of this player". 255 against 255 answers nothing.
// The buffer behind it is D24S8, and capturedDepth_ already holds it for the
// face trace, so this is a writer, not a second measurement.
bool Renderer::writeWorldDepthToPgm(const std::wstring& path) {
  if (capturedDepth_.empty() || capturedDepthWidth_ <= 0 || capturedDepthHeight_ <= 0) {
    captureError_ = L"no world depth was captured for this frame";
    return false;
  }
  std::FILE* file = nullptr;
  if (_wfopen_s(&file, path.c_str(), L"wb") != 0 || file == nullptr) {
    captureError_ = L"depth file open failed";
    return false;
  }
  std::fprintf(file, "P5\n%d %d\n65535\n", capturedDepthWidth_, capturedDepthHeight_);
  std::vector<std::uint8_t> row(static_cast<std::size_t>(capturedDepthWidth_) * 2u, 0u);
  unsigned long long written = 0;
  bool ok = true;
  for (int y = 0; y < capturedDepthHeight_ && ok; ++y) {
    for (int x = 0; x < capturedDepthWidth_; ++x) {
      const float depth = capturedDepth_[static_cast<std::size_t>(y) *
        static_cast<std::size_t>(capturedDepthWidth_) + static_cast<std::size_t>(x)];
      // 1.0 is the cleared far value; an unmeasured entry reads as far rather
      // than as a near surface, so a hole can never claim to occlude anything.
      const float clamped = !(depth >= 0.0f) ? 1.0f : (depth > 1.0f ? 1.0f : depth);
      const unsigned value = static_cast<unsigned>(std::lround(clamped * 65535.0f));
      row[static_cast<std::size_t>(x) * 2u + 0u] = static_cast<std::uint8_t>((value >> 8) & 0xFFu);
      row[static_cast<std::size_t>(x) * 2u + 1u] = static_cast<std::uint8_t>(value & 0xFFu);
    }
    if (std::fwrite(row.data(), 1, row.size(), file) != row.size()) ok = false;
    else written += row.size();
  }
  if (std::fclose(file) != 0) ok = false;
  depthCaptureBytes_ = written;
  if (!ok) captureError_ = L"writing the depth image failed";
  return ok;
}

// Reads the swap chain's back buffer back to the CPU and writes it as an
// uncompressed 24-bit BMP. Deliberately dumb: no encoder dependency, no colour
// management, one file per call. The point is a byte-inspectable artefact a
// gate can hash and measure, not a pretty picture.
//
// This deliberately does NOT touch lastError_: a capture failure is a
// diagnostic failure, not a device failure, and must not send the main loop
// down its device-removed recovery path.
bool Renderer::writeBackBufferToFile(const std::wstring& path) {
  if (!swapChain_ || !device_ || !context_) {
    captureError_ = L"renderer is not initialised";
    return false;
  }
  Microsoft::WRL::ComPtr<ID3D11Texture2D> backBuffer;
  HRESULT result = swapChain_->GetBuffer(0, __uuidof(ID3D11Texture2D),
    reinterpret_cast<void**>(backBuffer.GetAddressOf()));
  if (FAILED(result)) { captureError_ = L"swap chain GetBuffer(0) failed"; return false; }
  D3D11_TEXTURE2D_DESC desc{};
  backBuffer->GetDesc(&desc);
  const bool bgra = desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM;
  if (!bgra && desc.Format != DXGI_FORMAT_R8G8B8A8_UNORM) {
    captureError_ = L"unsupported back buffer format";
    return false;
  }
  D3D11_TEXTURE2D_DESC staging = desc;
  staging.Usage = D3D11_USAGE_STAGING;
  staging.BindFlags = 0;
  staging.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  staging.MiscFlags = 0;
  staging.MipLevels = 1;
  staging.ArraySize = 1;
  Microsoft::WRL::ComPtr<ID3D11Texture2D> readback;
  result = device_->CreateTexture2D(&staging, nullptr, readback.GetAddressOf());
  if (FAILED(result)) { captureError_ = L"staging texture creation failed"; return false; }
  context_->CopyResource(readback.Get(), backBuffer.Get());
  D3D11_MAPPED_SUBRESOURCE mapped{};
  result = context_->Map(readback.Get(), 0, D3D11_MAP_READ, 0, &mapped);
  if (FAILED(result)) { captureError_ = L"mapping the staging texture for read failed"; return false; }

  const UINT width = desc.Width;
  const UINT height = desc.Height;
  const UINT rowBytes = width * 3;
  const UINT padding = (4 - (rowBytes % 4)) % 4;
  const UINT stride = rowBytes + padding;
  const std::uint32_t imageBytes = static_cast<std::uint32_t>(stride) * height;
  const std::uint32_t headerBytes = 14 + 40;
  const std::uint32_t fileBytes = headerBytes + imageBytes;

  bool ok = true;
  std::FILE* file = nullptr;
  // _wfopen_s rather than _wfopen: the main target builds at /W4 and the
  // deprecation warning would move the chain's warning count off its pinned 1.
  if (_wfopen_s(&file, path.c_str(), L"wb") != 0 || !file) {
    captureError_ = L"could not open the output file";
    ok = false;
  } else {
    std::uint8_t header[54] = {};
    header[0] = 'B'; header[1] = 'M';
    const std::uint32_t dataOffset = headerBytes;
    const std::uint32_t dibSize = 40;
    const std::uint16_t planes = 1;
    const std::uint16_t bitsPerPixel = 24;
    const std::uint32_t compression = 0;
    std::memcpy(&header[2], &fileBytes, 4);
    std::memcpy(&header[10], &dataOffset, 4);
    std::memcpy(&header[14], &dibSize, 4);
    std::memcpy(&header[18], &width, 4);
    std::memcpy(&header[22], &height, 4);
    std::memcpy(&header[26], &planes, 2);
    std::memcpy(&header[28], &bitsPerPixel, 2);
    std::memcpy(&header[30], &compression, 4);
    std::memcpy(&header[34], &imageBytes, 4);
    if (std::fwrite(header, 1, sizeof(header), file) != sizeof(header)) ok = false;

    const auto* base = static_cast<const std::uint8_t*>(mapped.pData);
    std::vector<std::uint8_t> row(stride, 0);
    // BMP rows run bottom-up, so walk the source from its last row downwards.
    for (UINT y = 0; ok && y < height; ++y) {
      const auto* source = base + static_cast<std::size_t>(height - 1 - y) * mapped.RowPitch;
      for (UINT x = 0; x < width; ++x) {
        const std::uint8_t c0 = source[x * 4 + 0];
        const std::uint8_t c1 = source[x * 4 + 1];
        const std::uint8_t c2 = source[x * 4 + 2];
        row[x * 3 + 0] = bgra ? c0 : c2;   // blue
        row[x * 3 + 1] = c1;               // green
        row[x * 3 + 2] = bgra ? c2 : c0;   // red
      }
      for (UINT p = 0; p < padding; ++p) row[rowBytes + p] = 0;
      if (std::fwrite(row.data(), 1, stride, file) != stride) ok = false;
    }
    if (std::fclose(file) != 0) ok = false;
    if (!ok) captureError_ = L"writing the output file failed";
  }
  context_->Unmap(readback.Get(), 0);
  // The final depth, after the ViewModel pass cleared the world depth and drew
  // the weapon. Kept separately from the world depth so the face trace can ask
  // "did the world pixel survive the weapon pass" instead of assuming it.
  captureDepthBuffer(true);
  return ok;
}

void Renderer::captureDepthBuffer(bool finalPass) {
  // ---- T21: the depth of the world/water/entity passes -------------------
  // The face trace picks a world triangle per screen point by projecting the
  // map, so "the pixel belongs to that triangle" is an assumption until the
  // depth buffer confirms it. A model, the water pass, or a nearer world
  // triangle all invalidate it, and a colour comparison over such a pixel is a
  // comparison with somebody else's surface. The D24S8 packing was READ, not
  // assumed: the cleared depth of the first capture answered 0x00ffffff, i.e.
  // depth 1.0 sits in the LOW 24 bits with the stencil byte above it, so this
  // reader masks the low 24 bits. A format it does not know leaves the buffer
  // empty, and `capturedDepthAt` then answers negative -- "not measured",
  // never "matched".
  std::vector<float>& target = finalPass ? capturedFinalDepth_ : capturedDepth_;
  target.clear();
  // The dimensions belong to the world readback; a failed final readback must
  // not erase the size of a successful world one.
  if (!finalPass) {
    capturedDepthWidth_ = 0;
    capturedDepthHeight_ = 0;
  }
  if (depthTexture_) {
    D3D11_TEXTURE2D_DESC depthDesc{};
    depthTexture_->GetDesc(&depthDesc);
    if (depthDesc.Format == DXGI_FORMAT_D24_UNORM_S8_UINT) {
      D3D11_TEXTURE2D_DESC stagingDepth = depthDesc;
      stagingDepth.Usage = D3D11_USAGE_STAGING;
      stagingDepth.BindFlags = 0;
      stagingDepth.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
      stagingDepth.MiscFlags = 0;
      stagingDepth.MipLevels = 1;
      stagingDepth.ArraySize = 1;
      Microsoft::WRL::ComPtr<ID3D11Texture2D> depthReadback;
      if (SUCCEEDED(device_->CreateTexture2D(&stagingDepth, nullptr, depthReadback.GetAddressOf()))) {
        context_->CopyResource(depthReadback.Get(), depthTexture_.Get());
        D3D11_MAPPED_SUBRESOURCE depthMapped{};
        if (SUCCEEDED(context_->Map(depthReadback.Get(), 0, D3D11_MAP_READ, 0, &depthMapped))) {
          target.assign(static_cast<std::size_t>(depthDesc.Width) * depthDesc.Height, -1.0f);
          if (!finalPass) capturedDepthRaw_.assign(static_cast<std::size_t>(depthDesc.Width) * depthDesc.Height, 0u);
          capturedDepthWidth_ = static_cast<int>(depthDesc.Width);
          capturedDepthHeight_ = static_cast<int>(depthDesc.Height);
          const auto* rows = static_cast<const std::uint8_t*>(depthMapped.pData);
          for (UINT y = 0; y < depthDesc.Height; ++y) {
            const auto* row = reinterpret_cast<const std::uint32_t*>(
              rows + static_cast<std::size_t>(y) * depthMapped.RowPitch);
            for (UINT x = 0; x < depthDesc.Width; ++x) {
              if (!finalPass) {
                capturedDepthRaw_[static_cast<std::size_t>(y) * depthDesc.Width + x] = row[x];
              }
              target[static_cast<std::size_t>(y) * depthDesc.Width + x] =
                static_cast<float>(row[x] & 0x00FFFFFFu) / 16777215.0f;
            }
          }
          context_->Unmap(depthReadback.Get(), 0);
        }
      }
    }
  }
}

float Renderer::capturedDepthAt(int x, int y) const {
  if (x < 0 || y < 0 || x >= capturedDepthWidth_ || y >= capturedDepthHeight_) return -1.0f;
  const std::size_t at = static_cast<std::size_t>(y) * static_cast<std::size_t>(capturedDepthWidth_)
    + static_cast<std::size_t>(x);
  return at < capturedDepth_.size() ? capturedDepth_[at] : -1.0f;
}

float Renderer::capturedFinalDepthAt(int x, int y) const {
  if (x < 0 || y < 0 || x >= capturedDepthWidth_ || y >= capturedDepthHeight_) return -1.0f;
  const std::size_t at = static_cast<std::size_t>(y) * static_cast<std::size_t>(capturedDepthWidth_)
    + static_cast<std::size_t>(x);
  return at < capturedFinalDepth_.size() ? capturedFinalDepth_[at] : -1.0f;
}

std::uint32_t Renderer::capturedDepthRawAt(int x, int y) const {
  if (x < 0 || y < 0 || x >= capturedDepthWidth_ || y >= capturedDepthHeight_) return 0u;
  const std::size_t at = static_cast<std::size_t>(y) * static_cast<std::size_t>(capturedDepthWidth_)
    + static_cast<std::size_t>(x);
  return at < capturedDepthRaw_.size() ? capturedDepthRaw_[at] : 0u;
}

} // namespace tf2::native
