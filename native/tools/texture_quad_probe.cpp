#include "vtf_texture.h"

#include <Windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <fstream>
#include <cstring>
#include <iostream>
#include <vector>

using Microsoft::WRL::ComPtr;

std::vector<std::uint8_t> readFile(const char* path) {
  std::ifstream input(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

int main(int argc, char** argv) {
  if (argc != 2) return 2;
  const auto bytes = readFile(argv[1]);
  tf2::native::VtfTexture vtf;
  if (bytes.empty() || !vtf.parse(bytes) || vtf.header().isCubemap()) return 3;
  const auto rgba = vtf.decodeRgba(bytes);
  if (rgba.empty()) return 4;
  ComPtr<ID3D11Device> device; ComPtr<ID3D11DeviceContext> context;
  D3D_FEATURE_LEVEL level{};
  if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
      D3D11_SDK_VERSION, device.GetAddressOf(), &level, context.GetAddressOf()))) return 5;
  D3D11_TEXTURE2D_DESC src{}; src.Width = vtf.header().width; src.Height = vtf.header().height;
  src.MipLevels = 1; src.ArraySize = 1; src.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  src.SampleDesc.Count = 1; src.Usage = D3D11_USAGE_IMMUTABLE; src.BindFlags = D3D11_BIND_SHADER_RESOURCE;
  D3D11_SUBRESOURCE_DATA init{rgba.data(), static_cast<UINT>(vtf.header().width) * 4, 0};
  ComPtr<ID3D11Texture2D> source; ComPtr<ID3D11ShaderResourceView> srv;
  if (FAILED(device->CreateTexture2D(&src, &init, source.GetAddressOf())) ||
      FAILED(device->CreateShaderResourceView(source.Get(), nullptr, srv.GetAddressOf()))) return 6;
  const char* vsText = "struct O{float4 p:SV_POSITION;float2 uv:TEXCOORD0;};O main(uint id:SV_VertexID){float2 p[6]={float2(-1,-1),float2(-1,1),float2(1,1),float2(-1,-1),float2(1,1),float2(1,-1)};O o;o.p=float4(p[id],0,1);o.uv=float2((p[id].x+1)*.5, (1-p[id].y)*.5);return o;}";
  const char* psText = "Texture2D t:register(t0);SamplerState s:register(s0);float4 main(float4 p:SV_POSITION,float2 uv:TEXCOORD0):SV_TARGET{return t.Sample(s,uv);}";
  ComPtr<ID3DBlob> vsCode, psCode;
  if (FAILED(D3DCompile(vsText, strlen(vsText), nullptr, nullptr, nullptr, "main", "vs_5_0", 0, 0, vsCode.GetAddressOf(), nullptr)) ||
      FAILED(D3DCompile(psText, strlen(psText), nullptr, nullptr, nullptr, "main", "ps_5_0", 0, 0, psCode.GetAddressOf(), nullptr))) return 7;
  ComPtr<ID3D11VertexShader> vs; ComPtr<ID3D11PixelShader> ps;
  if (FAILED(device->CreateVertexShader(vsCode->GetBufferPointer(), vsCode->GetBufferSize(), nullptr, vs.GetAddressOf())) ||
      FAILED(device->CreatePixelShader(psCode->GetBufferPointer(), psCode->GetBufferSize(), nullptr, ps.GetAddressOf()))) return 8;
  D3D11_TEXTURE2D_DESC target = {}; target.Width = 4; target.Height = 4; target.MipLevels = 1; target.ArraySize = 1;
  target.Format = DXGI_FORMAT_R8G8B8A8_UNORM; target.SampleDesc.Count = 1; target.Usage = D3D11_USAGE_DEFAULT; target.BindFlags = D3D11_BIND_RENDER_TARGET;
  ComPtr<ID3D11Texture2D> color; ComPtr<ID3D11RenderTargetView> rtv;
  if (FAILED(device->CreateTexture2D(&target, nullptr, color.GetAddressOf())) || FAILED(device->CreateRenderTargetView(color.Get(), nullptr, rtv.GetAddressOf()))) return 9;
  D3D11_SAMPLER_DESC sd{}; sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR; sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
  ComPtr<ID3D11SamplerState> sampler; if (FAILED(device->CreateSamplerState(&sd, sampler.GetAddressOf()))) return 10;
  const float clear[4] = {0, 0, 0, 1}; context->OMSetRenderTargets(1, rtv.GetAddressOf(), nullptr); context->ClearRenderTargetView(rtv.Get(), clear);
  D3D11_VIEWPORT viewport{0.0f, 0.0f, 4.0f, 4.0f, 0.0f, 1.0f}; context->RSSetViewports(1, &viewport);
  context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST); context->VSSetShader(vs.Get(), nullptr, 0); context->PSSetShader(ps.Get(), nullptr, 0);
  context->PSSetShaderResources(0, 1, srv.GetAddressOf()); context->PSSetSamplers(0, 1, sampler.GetAddressOf()); context->Draw(6, 0);
  D3D11_TEXTURE2D_DESC staging = target; staging.Usage = D3D11_USAGE_STAGING; staging.BindFlags = 0; staging.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  ComPtr<ID3D11Texture2D> readback; if (FAILED(device->CreateTexture2D(&staging, nullptr, readback.GetAddressOf()))) return 11;
  context->CopyResource(readback.Get(), color.Get()); D3D11_MAPPED_SUBRESOURCE mapped{};
  if (FAILED(context->Map(readback.Get(), 0, D3D11_MAP_READ, 0, &mapped))) return 12;
  const auto* rows = static_cast<const std::uint8_t*>(mapped.pData); std::size_t nonBlackPixels = 0;
  for (UINT y = 0; y < 4; ++y) for (UINT x = 0; x < 4; ++x) {
    const auto* pixel = rows + static_cast<std::size_t>(y) * mapped.RowPitch + x * 4u;
    if (pixel[0] || pixel[1] || pixel[2]) ++nonBlackPixels;
  }
  const bool nonBlack = nonBlackPixels > 0; context->Unmap(readback.Get(), 0);
  std::cout << "{\"vtfWidth\":" << vtf.header().width << ",\"vtfHeight\":" << vtf.header().height
    << ",\"featureLevel\":" << static_cast<int>(level) << ",\"nonBlackPixels\":" << nonBlackPixels
    << ",\"drawPixelsNonBlack\":" << (nonBlack ? "true" : "false") << "}\n";
  return nonBlack ? 0 : 13;
}
