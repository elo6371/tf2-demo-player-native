#include "wic_image.h"

#include <Windows.h>
#include <objbase.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <cstdio>

namespace tf2::native {
namespace {

// UTF-8 (what the CLI and the demo header carry) to UTF-16 (what WIC wants).
bool widen(const std::string& text, std::wstring& out) {
  if (text.empty()) return false;
  const int length = MultiByteToWideChar(CP_UTF8, 0, text.c_str(),
    static_cast<int>(text.size()), nullptr, 0);
  if (length <= 0) return false;
  out.assign(static_cast<std::size_t>(length), L'\0');
  if (MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
        &out[0], length) != length) return false;
  return true;
}

}  // namespace

bool decodeImageFile(const std::string& utf8Path, DecodedImage& image, std::string& error) {
  image = DecodedImage{};
  error = "unknown";
  std::wstring widePath;
  if (!widen(utf8Path, widePath)) { error = "bad-path"; return false; }
  const HRESULT comResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  const bool comOwned = SUCCEEDED(comResult);
  if (FAILED(comResult) && comResult != RPC_E_CHANGED_MODE) { error = "no-com"; return false; }
  bool ok = false;
  // Every WIC object is scoped to this block on purpose: its Release() must run
  // while this thread's apartment is still alive. CoUninitialize() below tears
  // the apartment down, and releasing the objects afterwards faulted inside
  // windowscodecs (0xC0000005 at the end of this function, 2026-11-17).
  {
    Microsoft::WRL::ComPtr<IWICImagingFactory> factory;
    HRESULT result = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
      IID_PPV_ARGS(factory.GetAddressOf()));
    if (FAILED(result)) {
      error = "no-factory";
    } else {
      Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
      result = factory->CreateDecoderFromFilename(widePath.c_str(), nullptr, GENERIC_READ,
        WICDecodeMetadataCacheOnDemand, decoder.GetAddressOf());
      if (FAILED(result)) {
        error = "decoder";
      } else {
        Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
        result = decoder->GetFrame(0, frame.GetAddressOf());
        if (FAILED(result)) {
          error = "frame";
        } else {
          UINT width = 0;
          UINT height = 0;
          result = frame->GetSize(&width, &height);
          if (FAILED(result) || width == 0 || height == 0 || width > 16384 || height > 16384) {
            error = "size";
          } else {
            Microsoft::WRL::ComPtr<IWICFormatConverter> converter;
            result = factory->CreateFormatConverter(converter.GetAddressOf());
            if (FAILED(result)) {
              error = "converter";
            } else {
              // Straight RGBA8: the HUD sample path is UNORM end to end, so a
              // premultiplied or BGR source would shift every colour by a constant
              // the trace could not see.
              result = converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA,
                WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
              if (FAILED(result)) {
                error = "convert";
              } else {
                const std::size_t bytes =
                  static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u;
                std::vector<std::uint8_t> rgba(bytes);
                result = converter->CopyPixels(nullptr, width * 4u,
                  static_cast<UINT>(bytes), rgba.data());
                if (FAILED(result)) {
                  error = "copy";
                } else {
                  image.width = static_cast<int>(width);
                  image.height = static_cast<int>(height);
                  image.rgba = std::move(rgba);
                  error = "ok";
                  ok = image.valid();
                }
              }
            }
          }
        }
      }
    }
  }
  if (comOwned) CoUninitialize();
  return ok;
}

}  // namespace tf2::native
