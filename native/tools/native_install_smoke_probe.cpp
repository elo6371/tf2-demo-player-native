#include <Windows.h>
#include <d3d11.h>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>
#include <wrl/client.h>

#include "asset_root.h"

int main(int argc, char** argv) {
  const std::filesystem::path executable = argc > 0
    ? std::filesystem::absolute(std::filesystem::path(argv[0])) : std::filesystem::path{};
  const std::filesystem::path installDir = executable.empty() ? std::filesystem::path{} : executable.parent_path();
  const std::filesystem::path missingRoot = installDir / "__missing_tf_root_for_smoke__";
  const tf2::native::AssetRoot root = tf2::native::AssetRoot::fromPath(missingRoot);
  if (root.valid()) {
    std::cerr << "tf_root_missing_prompt=0\n";
    return 2;
  }

  D3D_FEATURE_LEVEL selected{};
  Microsoft::WRL::ComPtr<ID3D11Device> device;
  Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
  const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
  HRESULT result = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
    levels, ARRAYSIZE(levels), D3D11_SDK_VERSION, device.GetAddressOf(), &selected,
    context.GetAddressOf());
  if (result == E_INVALIDARG) {
    result = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
      &levels[1], 1, D3D11_SDK_VERSION, device.GetAddressOf(), &selected,
      context.GetAddressOf());
  }
  if (FAILED(result)) {
    std::cerr << "warp_fallback=0 hresult=0x" << std::hex << static_cast<unsigned long>(result) << "\n";
    return 3;
  }
  std::cout << "install_executable=1 tf_root_missing_prompt=1 warp_fallback=1 feature_level="
            << std::hex << static_cast<unsigned long>(selected) << std::dec << "\n";
  return 0;
}
