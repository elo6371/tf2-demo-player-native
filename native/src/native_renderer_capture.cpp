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
bool Renderer::writeDepthPlanePgm(const std::wstring& path, const std::vector<float>& plane,
    int width, int height, unsigned long long* bytes) {
  if (plane.empty() || width <= 0 || height <= 0) {
    captureError_ = L"no depth plane to write";
    return false;
  }
  std::FILE* file = nullptr;
  if (_wfopen_s(&file, path.c_str(), L"wb") != 0 || file == nullptr) {
    captureError_ = L"depth file open failed";
    return false;
  }
  std::fprintf(file, "P5\n%d %d\n65535\n", width, height);
  std::vector<std::uint8_t> row(static_cast<std::size_t>(width) * 2u, 0u);
  unsigned long long written = 0;
  bool ok = true;
  for (int y = 0; y < height && ok; ++y) {
    for (int x = 0; x < width; ++x) {
      const float depth = plane[static_cast<std::size_t>(y) *
        static_cast<std::size_t>(width) + static_cast<std::size_t>(x)];
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
  if (bytes) *bytes = written;
  if (!ok) captureError_ = L"writing the depth image failed";
  return ok;
}

bool Renderer::writeWorldDepthToPgm(const std::wstring& path) {
  if (capturedDepth_.empty() || capturedDepthWidth_ <= 0 || capturedDepthHeight_ <= 0) {
    captureError_ = L"no world depth was captured for this frame";
    return false;
  }
  return writeDepthPlanePgm(path, capturedDepth_, capturedDepthWidth_, capturedDepthHeight_,
    &depthCaptureBytes_);
}

// R1 (2026-11-16): the per-pass depth channel. M2b established that the 109 px
// boundary is not occlusion by reading the frame's LAST world depth; it could not
// say WHICH pass left that depth. This reads the same D24S8 buffer at named
// points inside one frame, writes each plane beside the picture's prefix, and
// prints the depth-stencil state bound there (recorded at the bind site, not
// inferred from a comment). Active only on a capture frame with a prefix set, so
// the product path pays one empty-string test per dump point.
void Renderer::capturePassDepth(const char* name) {
  if (passDepthPrefix_.empty() || capturePath_.empty()) return;
  if (!depthTexture_ || !device_ || !context_) {
    passDepthSucceeded_ = false;
    return;
  }
  D3D11_TEXTURE2D_DESC depthDesc{};
  depthTexture_->GetDesc(&depthDesc);
  const unsigned width = depthDesc.Width;
  const unsigned height = depthDesc.Height;
  std::vector<float> plane;
  bool mappedOk = false;
  if (depthDesc.Format == DXGI_FORMAT_D24_UNORM_S8_UINT && width > 0 && height > 0) {
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
        plane.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), -1.0f);
        const auto* rows = static_cast<const std::uint8_t*>(depthMapped.pData);
        for (unsigned y = 0; y < height; ++y) {
          const auto* row = reinterpret_cast<const std::uint32_t*>(
            rows + static_cast<std::size_t>(y) * depthMapped.RowPitch);
          for (unsigned x = 0; x < width; ++x) {
            plane[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + x] =
              static_cast<float>(row[x] & 0x00FFFFFFu) / 16777215.0f;
          }
        }
        context_->Unmap(depthReadback.Get(), 0);
        mappedOk = true;
      }
    }
  }
  if (!mappedOk) {
    passDepthSucceeded_ = false;
    std::printf("pass-depth: index=%d name=%s wrote=0 reason=readback-failed depthStateLastBind=%s\n",
      passDepthIndex_, name, lastDepthStateLabel_);
    std::fflush(stdout);
    return;
  }
  const std::wstring nameW(name, name + std::strlen(name));
  const std::wstring path = passDepthPrefix_ + L"-" + std::to_wstring(passDepthIndex_)
    + L"-" + nameW + L".pgm";
  unsigned long long bytes = 0;
  const bool ok = writeDepthPlanePgm(path, plane, static_cast<int>(width),
    static_cast<int>(height), &bytes);
  std::printf("pass-depth: index=%d name=%s depthStateLastBind=%s width=%u height=%u"
    " bytes=%llu wrote=%d\n", passDepthIndex_, name, lastDepthStateLabel_, width, height,
    bytes, ok ? 1 : 0);
  std::fflush(stdout);
  ++passDepthIndex_;
  if (ok) ++passDepthWritten_; else passDepthSucceeded_ = false;
}

// R1b (2026-11-16): per-range depth inside 50-entities. R1 showed the 1181 px of
// the M2b footprint is written by the entity pass and by nothing else, but one
// plane per pass cannot say WHICH range draw wrote it. This reads the same D24S8
// buffer after every drawn range, compares it with the plane the previous range
// left, and writes that window so a caller can name the range that moved a pixel.
//
// The window exists because the probe runs per draw: a whole-plane readback costs
// 3.4 MB and a full map per range (259 ranges on the R1 corpus), while the
// footprint question lives in a ~100 px box. A window of 0 width means "whole
// plane", which is still allowed for a one-off, just not for a sweep.
void Renderer::captureRangeDepth(const char* phase, int rangeIndex, int blended,
    int materialSlot) {
  if (rangeDepthPrefix_.empty() || capturePath_.empty()) return;
  if (!depthTexture_ || !device_ || !context_) {
    rangeDepthSucceeded_ = false;
    return;
  }
  D3D11_TEXTURE2D_DESC depthDesc{};
  depthTexture_->GetDesc(&depthDesc);
  if (depthDesc.Format != DXGI_FORMAT_D24_UNORM_S8_UINT || depthDesc.Width == 0
      || depthDesc.Height == 0) {
    rangeDepthSucceeded_ = false;
    std::printf("range-depth: index=%d phase=%s range=%d blended=%d slot=%d changed=0"
      " wrote=0 reason=depth-format depthStateLastBind=%s\n",
      rangeDepthIndex_, phase, rangeIndex, blended, materialSlot, lastDepthStateLabel_);
    std::fflush(stdout);
    return;
  }
  unsigned x0 = 0;
  unsigned y0 = 0;
  unsigned width = depthDesc.Width;
  unsigned height = depthDesc.Height;
  if (rangeDepthWindow_[2] != 0 && rangeDepthWindow_[3] != 0) {
    x0 = rangeDepthWindow_[0];
    y0 = rangeDepthWindow_[1];
    width = rangeDepthWindow_[2];
    height = rangeDepthWindow_[3];
    if (x0 >= depthDesc.Width || y0 >= depthDesc.Height) {
      rangeDepthSucceeded_ = false;
      std::printf("range-depth: index=%d phase=%s range=%d blended=%d slot=%d changed=0"
        " wrote=0 reason=window-outside depthStateLastBind=%s\n",
        rangeDepthIndex_, phase, rangeIndex, blended, materialSlot, lastDepthStateLabel_);
      std::fflush(stdout);
      return;
    }
    width = (std::min)(width, depthDesc.Width - x0);
    height = (std::min)(height, depthDesc.Height - y0);
  }
  // The copy is whole-plane even when the window is small. A partial box copy out
  // of a depth-stencil resource is not supported: the runtime accepts it, leaves
  // the destination undefined, and the first version of this probe read one draw
  // as writing all 860,784 px of the frame. So copy whole, crop on the CPU.
  if (!rangeDepthStaging_) {
    D3D11_TEXTURE2D_DESC staging = depthDesc;
    staging.Usage = D3D11_USAGE_STAGING;
    staging.BindFlags = 0;
    staging.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    staging.MiscFlags = 0;
    staging.MipLevels = 1;
    staging.ArraySize = 1;
    if (FAILED(device_->CreateTexture2D(&staging, nullptr,
        rangeDepthStaging_.GetAddressOf()))) {
      rangeDepthStaging_.Reset();
      rangeDepthSucceeded_ = false;
      std::printf("range-depth: index=%d phase=%s range=%d blended=%d slot=%d changed=0"
        " wrote=0 reason=staging-failed depthStateLastBind=%s\n",
        rangeDepthIndex_, phase, rangeIndex, blended, materialSlot, lastDepthStateLabel_);
      std::fflush(stdout);
      return;
    }
  }
  context_->CopyResource(rangeDepthStaging_.Get(), depthTexture_.Get());
  std::vector<float> plane;
  bool mappedOk = false;
  D3D11_MAPPED_SUBRESOURCE mapped{};
  if (SUCCEEDED(context_->Map(rangeDepthStaging_.Get(), 0, D3D11_MAP_READ, 0, &mapped))) {
    plane.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), -1.0f);
    const auto* rows = static_cast<const std::uint8_t*>(mapped.pData);
    for (unsigned y = 0; y < height; ++y) {
      const auto* row = reinterpret_cast<const std::uint32_t*>(
        rows + static_cast<std::size_t>(y0 + y) * mapped.RowPitch);
      for (unsigned x = 0; x < width; ++x) {
        plane[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + x] =
          static_cast<float>(row[x0 + x] & 0x00FFFFFFu) / 16777215.0f;
      }
    }
    context_->Unmap(rangeDepthStaging_.Get(), 0);
    mappedOk = true;
  }
  if (!mappedOk) {
    rangeDepthSucceeded_ = false;
    std::printf("range-depth: index=%d phase=%s range=%d blended=%d slot=%d changed=0"
      " wrote=0 reason=readback-failed depthStateLastBind=%s\n",
      rangeDepthIndex_, phase, rangeIndex, blended, materialSlot, lastDepthStateLabel_);
    std::fflush(stdout);
    return;
  }
  // Compare against the plane the previous drawn range left. A range's own writes
  // are therefore the difference between two readbacks of the SAME frame, not a
  // guess from the final picture.
  const std::size_t pixels = plane.size();
  const bool firstOfFrame = rangeDepthPrev_.size() != pixels;
  int changed = 0;
  int wrote = 0;
  // Not "near": windef.h still defines that as an empty macro, so the name would
  // vanish from the declaration and the compiler would see "int = 0".
  int nearPixels = 0;
  int bx0 = static_cast<int>(width);
  int by0 = static_cast<int>(height);
  int bx1 = -1;
  int by1 = -1;
  for (std::size_t i = 0; i < pixels; ++i) {
    const float now = plane[i];
    if (now < 0.999999f) ++nearPixels;
    if (firstOfFrame) continue;
    const float prev = rangeDepthPrev_[i];
    if (prev == now) continue;
    ++changed;
    if (prev >= 0.999999f && now < 0.999999f) ++wrote;
    const int px = static_cast<int>(i % width);
    const int py = static_cast<int>(i / width);
    if (px < bx0) bx0 = px;
    if (py < by0) by0 = py;
    if (px > bx1) bx1 = px;
    if (py > by1) by1 = py;
  }
  const std::wstring phaseW(phase, phase + std::strlen(phase));
  const std::wstring path = rangeDepthPrefix_ + L"-" + std::to_wstring(rangeDepthIndex_)
    + L"-" + phaseW + L".pgm";
  unsigned long long bytes = 0;
  const bool ok = writeDepthPlanePgm(path, plane, static_cast<int>(width),
    static_cast<int>(height), &bytes);
  std::printf("range-depth: index=%d phase=%s range=%d blended=%d slot=%d changed=%d"
    " wrote=%d near=%d bbox=%d,%d,%d,%d window=%u,%u,%u,%u bytes=%llu wrote_pgm=%d"
    " depthStateLastBind=%s\n",
    rangeDepthIndex_, phase, rangeIndex, blended, materialSlot, changed, wrote, nearPixels,
    bx1 < 0 ? -1 : bx0, bx1 < 0 ? -1 : by0, bx1, bx1 < 0 ? -1 : by1,
    x0, y0, width, height, bytes, ok ? 1 : 0, lastDepthStateLabel_);
  std::fflush(stdout);
  rangeDepthPrev_ = std::move(plane);
  ++rangeDepthIndex_;
  if (ok) ++rangeDepthWritten_; else rangeDepthSucceeded_ = false;
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
