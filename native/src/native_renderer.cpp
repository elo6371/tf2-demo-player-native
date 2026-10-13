#include "native_renderer.h"

#include "entity_model.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <unordered_map>

namespace tf2::native {

namespace {
struct WorldVertex { float x, y, z; float r, g, b, a; float u, v; float nx, ny, nz; float lu = -1.0f; float lv = -1.0f; };
struct EntityModelGpuVertex {
  float x, y, z;
  float nx, ny, nz;
  float u, v;
  // Skinning inputs. They were dropped on the floor before this round: the
  // loader had decoded them (ModelDrawVertex carries weights/boneIndices) and
  // the GPU copy simply did not carry them, so the entity shader could not have
  // skinned anything even if it had been asked to.
  float weight0, weight1, weight2;
  std::uint8_t bone0, bone1, bone2, boneCount;
};
struct EntityModelInstanceGpu {
  float position[3][4];
  float normal[3][4];
  float color[4];
  // Where this instance's skin matrices start in the shared bone buffer, and
  // whether to use them at all. Both live in the instance rather than in a
  // constant buffer because two instances of the same model routinely carry
  // different sequences -- a per-model upload would silently pose one of them
  // with the other's bones.
  std::uint32_t boneOffset = 0;
  std::uint32_t skinEnabled = 0;
  std::uint32_t padding[2] = {0, 0};
};
static_assert(sizeof(EntityModelGpuVertex) == 48, "entity mesh vertex stride");
static_assert(offsetof(EntityModelGpuVertex, nx) == 12, "entity mesh normal offset");
static_assert(offsetof(EntityModelGpuVertex, u) == 24, "entity mesh uv offset");
static_assert(offsetof(EntityModelGpuVertex, weight0) == 32, "entity mesh blend weight offset");
static_assert(offsetof(EntityModelGpuVertex, bone0) == 44, "entity mesh blend index offset");
static_assert(sizeof(EntityModelInstanceGpu) == 128, "entity instance stride");
static_assert(offsetof(EntityModelInstanceGpu, color) == 96, "entity instance color offset");
static_assert(offsetof(EntityModelInstanceGpu, boneOffset) == 112, "entity instance bone offset");
// Mirrors the ViewModelConstants cbuffer in viewModelVertexSource field for
// field. `skinInfo.x` is the skeleton size (0 = draw the bind pose) so the
// shader can reject an out-of-range bone index the same way the entity shader
// rejects one against `boneInfo.z`.
struct ViewModelConstantsData {
  DirectX::XMFLOAT4X4 mvp;
  DirectX::XMFLOAT4 tint;
  DirectX::XMFLOAT4 skinInfo;
  // x = 1 when this range has a texture bound at t0, 0 for the flat-colour
  // fallback. y = 1 when the material's own `$alphatest` says to discard below
  // 0.5. z and w are unused and kept so the buffer stays a multiple of 16.
  DirectX::XMFLOAT4 material;
};
static_assert(sizeof(ViewModelConstantsData) == 112, "viewmodel constant buffer size");
struct ModelGpuVertex {
  float x, y, z; float r, g, b, a; float u, v; float nx, ny, nz;
  float weights[3]; std::uint8_t boneIndices[3]; std::uint8_t boneCount; std::uint8_t padding[1];
};
template <typename T>
T clampValue(T value, T minimum, T maximum) {
  return value < minimum ? minimum : (value > maximum ? maximum : value);
}

bool compileShader(const char* source, const char* entry, const char* target,
    Microsoft::WRL::ComPtr<ID3DBlob>& bytecode, HRESULT* compileResult = nullptr) {
  Microsoft::WRL::ComPtr<ID3DBlob> errors;
  const HRESULT result = D3DCompile(source, std::strlen(source), nullptr, nullptr, nullptr,
    entry, target, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, bytecode.GetAddressOf(), errors.GetAddressOf());
  if (compileResult) *compileResult = result;
  if (FAILED(result) && errors) {
    // OutputDebugString is invisible to every caller in this repo, and a shader
    // is compiled at run time, so a bad edit surfaces as a bare rc=12 from
    // initialize() with no reason attached. Write the HLSL diagnostic to stderr
    // as well: the gate scripts and every manual run redirect it to a log, which
    // is what turns "it exited 12" into "the entity vertex shader failed on
    // line N".
    OutputDebugStringA(static_cast<const char*>(errors->GetBufferPointer()));
    std::fputs("[shader-compile] ", stderr);
    std::fputs(static_cast<const char*>(errors->GetBufferPointer()), stderr);
    std::fputc('\n', stderr);
    std::fflush(stderr);
  }
  return SUCCEEDED(result);
}
}

RenderSettings RenderSettings::fromPreset(QualityPreset preset) {
  RenderSettings settings{};
  settings.preset = preset;
  if (preset == QualityPreset::Standard) {
    settings.textureMipBias = 0;
    settings.antiAliasingQuality = 2;
    settings.anisotropicLevel = 8;
    settings.dynamicLighting = true;
    settings.shadows = true;
    settings.bumpMapping = true;
    settings.specular = true;
    settings.waterReflection = true;
    settings.waterRefraction = true;
    settings.skybox = true;
    settings.modelLod = 0;
  }
  settings.normalize();
  return settings;
}

void RenderSettings::normalize() {
  textureMipBias = clampValue(textureMipBias, -2, 4);
  antiAliasingQuality = clampValue(antiAliasingQuality, 0, 8);
  anisotropicLevel = clampValue(anisotropicLevel, 1, 16);
  modelLod = clampValue(modelLod, 0, 8);
  renderScale = clampValue(renderScale, 0.5f, 2.0f);
  viewModelFov = clampViewModelFov(viewModelFov);
  if (preset == QualityPreset::Performance) {
    dynamicLighting = false;
    shadows = false;
    bumpMapping = false;
    specular = false;
    waterReflection = false;
    skybox = false;
    anisotropicLevel = 1;
  }
}

bool Renderer::initialize(HWND window) {
  lastError_ = S_OK;
  settings_.normalize();
  window_ = window;
  RECT client{};
  GetClientRect(window_, &client);
  width_ = static_cast<UINT>(client.right - client.left);
  height_ = static_cast<UINT>(client.bottom - client.top);

  DXGI_SWAP_CHAIN_DESC desc{};
  desc.BufferCount = 2;
  desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  desc.OutputWindow = window_;
  desc.SampleDesc.Count = 1;
  desc.Windowed = TRUE;
  desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

  constexpr std::array<D3D_FEATURE_LEVEL, 2> levels = {
    D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0,
  };
  constexpr std::array<D3D_FEATURE_LEVEL, 1> legacyLevels = {
    D3D_FEATURE_LEVEL_11_0,
  };
  D3D_FEATURE_LEVEL selected{};
  auto create = [&](D3D_DRIVER_TYPE driver, const D3D_FEATURE_LEVEL* requested,
                    UINT requestedCount) {
    return D3D11CreateDeviceAndSwapChain(
      nullptr, driver, nullptr, 0, requested, requestedCount,
      D3D11_SDK_VERSION, &desc,
      swapChain_.GetAddressOf(), device_.GetAddressOf(), &selected,
      context_.GetAddressOf());
  };
  auto createWithFallback = [&](D3D_DRIVER_TYPE driver) {
    HRESULT result = create(driver, levels.data(), static_cast<UINT>(levels.size()));
    if (result == E_INVALIDARG) {
      swapChain_.Reset();
      device_.Reset();
      context_.Reset();
      result = create(driver, legacyLevels.data(), static_cast<UINT>(legacyLevels.size()));
    }
    return result;
  };
  initStage_ = "createDeviceAndSwapChain";
  HRESULT result = createWithFallback(D3D_DRIVER_TYPE_HARDWARE);
  if (FAILED(result)) {
    swapChain_.Reset();
    device_.Reset();
    context_.Reset();
    result = createWithFallback(D3D_DRIVER_TYPE_WARP);
  }
  if (FAILED(result)) { lastError_ = result; return false; }
  initStage_ = "createTarget";
  if (!createTarget(width_, height_)) {
    lastError_ = E_FAIL;
    return false;
  }
  initStage_ = "createPipeline";
  if (!createPipeline()) {
    lastError_ = E_FAIL;
    return false;
  }
  initStage_ = "ready";
  return true;
}

bool Renderer::createPipeline() {
  static constexpr char vertexSource[] = R"HLSL(
struct Output { float4 position : SV_POSITION; float2 uv : TEXCOORD0; };
Output main(uint id : SV_VertexID) {
  float2 positions[6] = { float2(-1,-1), float2(-1,1), float2(1,1), float2(-1,-1), float2(1,1), float2(1,-1) };
  Output output;
  output.position = float4(positions[id], 0, 1);
  output.uv = float2((positions[id].x + 1) * 0.5, (1 - positions[id].y) * 0.5);
  return output;
})HLSL";
  static constexpr char pixelSource[] = R"HLSL(
Texture2D texture0 : register(t0);
Texture2D bumpTexture : register(t1);
Texture2D envTexture : register(t2);
SamplerState sampler0 : register(s0);
float4 main(float4 position : SV_POSITION, float2 uv : TEXCOORD0) : SV_TARGET {
  return texture0.Sample(sampler0, uv);
})HLSL";
  Microsoft::WRL::ComPtr<ID3DBlob> vertexBytecode;
  Microsoft::WRL::ComPtr<ID3DBlob> pixelBytecode;
  HRESULT shaderResult = S_OK;
  if (!compileShader(vertexSource, "main", "vs_5_0", vertexBytecode, &shaderResult)
      || !compileShader(pixelSource, "main", "ps_5_0", pixelBytecode, &shaderResult)) {
    lastError_ = shaderResult;
    return false;
  }
  static constexpr char worldVertexSource[] = R"HLSL(
cbuffer WorldConstants : register(b0) {
  row_major float4x4 mvp;
  float4 lightDirectionAndMode;
  float4 cameraPositionAndSpecular;
  float4 materialFeatures;
  float4 lightmapFeatures;
  float4 envFeatures;
};
struct Input { float3 position : POSITION; float4 colour : COLOR0; float2 uv : TEXCOORD0; float3 normal : NORMAL0; float2 lightUv : TEXCOORD1; };
struct Output { float4 position : SV_POSITION; float4 colour : COLOR0; float2 uv : TEXCOORD0; float3 normal : NORMAL0; float3 worldPosition : TEXCOORD2; float2 lightUv : TEXCOORD3; };
Output main(Input input) { Output output; output.position = mul(float4(input.position, 1.0), mvp); output.colour = input.colour; output.uv = input.uv; output.normal = input.normal; output.worldPosition = input.position; output.lightUv = input.lightUv; return output; }
)HLSL";
  static constexpr char worldPixelSource[] = R"HLSL(
cbuffer WorldConstants : register(b0) {
  row_major float4x4 mvp;
  float4 lightDirectionAndMode;
  float4 cameraPositionAndSpecular;
  float4 materialFeatures;
  float4 lightmapFeatures;
  float4 envFeatures;
};
Texture2D texture0 : register(t0);
Texture2D bumpTexture : register(t1);
Texture2D envTexture : register(t2);
Texture2D lightmapTexture : register(t3);
// t5, not t4: the model passes bind their bone buffer at t4 and a slot serving
// two meanings in two passes is one slot too many to keep straight.
TextureCube envCube : register(t5);
SamplerState sampler0 : register(s0);
float3 safeNormalize(float3 value) {
  float lengthSquared = dot(value, value);
  if (!(lengthSquared > 1e-8) || !(lengthSquared < 1e8)) return float3(0.0, 0.0, 0.0);
  return value * rsqrt(lengthSquared);
}
float4 main(float4 position : SV_POSITION, float4 colour : COLOR0, float2 uv : TEXCOORD0, float3 normal : NORMAL0, float3 worldPosition : TEXCOORD2, float2 lightUv : TEXCOORD3) : SV_TARGET {
  float3 sampled = texture0.Sample(sampler0, uv).rgb;
  float lightmapFactor = max(lightmapFeatures.x, 0.0);
  float3 albedo = lerp(colour.rgb, sampled * colour.rgb, colour.a);
  float3 lightColour = float3(lightmapFactor, lightmapFactor, lightmapFactor);
  if (lightmapFeatures.w > 0.5 && lightUv.x >= 0.0) lightColour = lightmapTexture.Sample(sampler0, lightUv).rgb;
  albedo *= lightColour;
  float3 n = safeNormalize(normal);
  if (lightmapFeatures.y > 0.5) {
    float3 bump = bumpTexture.Sample(sampler0, uv).xyz * 2.0 - 1.0;
    n = safeNormalize(n + float3(bump.xy * 0.35, bump.z * 0.1));
  }
  // envFeatures.x is the mode the renderer is actually in, not a claim about it:
  // 0 = no env map, 1 = the 2D approximation, 2 = a real six-face cube. The two
  // branches sample different resources and the mode is what decides which, so a
  // cube that failed to upload cannot be sampled as if it were there.
  if (envFeatures.x > 1.5) {
    float3 viewDirection = safeNormalize(cameraPositionAndSpecular.xyz - worldPosition);
    float3 reflected = reflect(-viewDirection, n);
    albedo = lerp(albedo, albedo * envCube.Sample(sampler0, reflected).rgb, 0.22);
  } else if (envFeatures.x > 0.5) {
    float2 envUv = float2(0.5 + 0.5 * n.x, 0.5 - 0.5 * n.y);
    albedo = lerp(albedo, albedo * envTexture.Sample(sampler0, envUv).rgb, 0.22);
  }
  float selfIllum = saturate(materialFeatures.w);
  albedo += albedo * materialFeatures.xyz * selfIllum;
  float3 lightDirection = safeNormalize(lightDirectionAndMode.xyz);
  float diffuse = saturate(dot(n, lightDirection));
  float lighting = lerp(1.0, 0.62 + 0.38 * diffuse, lightDirectionAndMode.w);
  if (cameraPositionAndSpecular.w > 0.5) {
    float3 viewDirection = safeNormalize(cameraPositionAndSpecular.xyz - worldPosition);
    float3 halfDirection = safeNormalize(lightDirection + viewDirection);
    lighting += 0.12 * pow(saturate(dot(n, halfDirection)), 32.0);
  }
  lighting = max(0.0, min(lighting, 2.0));
  return float4(albedo * lighting, 1.0);
}
)HLSL";
  static constexpr char modelVertexSource[] = R"HLSL(
cbuffer WorldConstants : register(b0) { row_major float4x4 mvp; float4 unused0; float4 unused1; float4 unused2; float4 unused3; };
cbuffer ModelSkinningConstants : register(b1) { row_major float4x4 bones[128]; uint boneCount; uint skinEnabled; uint2 padding; };
struct Input { float3 position : POSITION; float4 colour : COLOR0; float2 uv : TEXCOORD0; float3 normal : NORMAL0; float3 weights : BLENDWEIGHT; uint4 boneIndices : BLENDINDICES; };
struct Output { float4 position : SV_POSITION; float4 colour : COLOR0; float2 uv : TEXCOORD0; float3 normal : NORMAL0; };
Output main(Input input) {
  Output output;
  if (skinEnabled > 0) {
    float3 skinnedPos = float3(0.0, 0.0, 0.0);
    float3 skinnedNormal = float3(0.0, 0.0, 0.0);
    float totalWeight = input.weights.x + input.weights.y + input.weights.z;
    if (totalWeight > 0.0) {
      float3 normalizedWeights = input.weights / totalWeight;
      for (uint i = 0; i < 3; ++i) {
        if (normalizedWeights[i] > 0.0 && input.boneIndices[i] < boneCount) {
          skinnedPos += mul(float4(input.position, 1.0), bones[input.boneIndices[i]]).xyz * normalizedWeights[i];
          skinnedNormal += mul(float4(input.normal, 0.0), bones[input.boneIndices[i]]).xyz * normalizedWeights[i];
        }
      }
      output.position = mul(float4(skinnedPos, 1.0), mvp);
      output.normal = normalize(skinnedNormal);
    } else {
      output.position = mul(float4(input.position, 1.0), mvp);
      output.normal = input.normal;
    }
  } else {
    output.position = mul(float4(input.position, 1.0), mvp);
    output.normal = input.normal;
  }
  output.colour = input.colour;
  output.uv = input.uv;
  return output;
}
)HLSL";
  static constexpr char modelPixelSource[] = R"HLSL(
struct Input { float4 position : SV_POSITION; float4 colour : COLOR0; float2 uv : TEXCOORD0; float3 normal : NORMAL0; };
float4 main(Input input) : SV_TARGET {
  float3 normal = normalize(input.normal);
  float diffuse = saturate(dot(normal, normalize(float3(0.35, 0.55, 0.75))));
  float lighting = 0.62 + 0.38 * diffuse;
  return float4(input.colour.rgb * lighting, input.colour.a);
}
)HLSL";
  static constexpr char entityModelVertexSource[] = R"HLSL(
cbuffer WorldConstants : register(b0) {
  row_major float4x4 mvp;
  float4 lightDirectionAndMode;
  float4 cameraPositionAndSpecular;
  float4 materialFeatures;
  float4 lightmapFeatures;
};
struct Input {
  float3 position : POSITION;
  float3 normal : NORMAL0;
  float2 uv : TEXCOORD0;
  float3 weights : BLENDWEIGHT;
  uint4 boneIndices : BLENDINDICES;
  float4 row0 : TEXCOORD1;
  float4 row1 : TEXCOORD2;
  float4 row2 : TEXCOORD3;
  float4 nrow0 : TEXCOORD4;
  float4 nrow1 : TEXCOORD5;
  float4 nrow2 : TEXCOORD6;
  float4 colour : COLOR0;
  uint4 boneInfo : TEXCOORD7;
};
// Per-instance bone matrices, in a StructuredBuffer rather than the b1 constant
// buffer the single-model shader uses: b1 holds one skeleton at a time, and
// every entity instance needs its own.
StructuredBuffer<float4x4> boneMatrices : register(t4);
struct Output {
  float4 position : SV_POSITION;
  float4 colour : COLOR0;
  float2 uv : TEXCOORD0;
  float3 normal : NORMAL0;
  float3 worldPosition : TEXCOORD2;
  float2 lightUv : TEXCOORD3;
};
Output main(Input input) {
  Output output;
  float4 local = float4(input.position, 1.0);
  float3 normalLocal = input.normal;
  // Skinning runs in bind-pose model space, before the instance transform:
  // skin[i] already maps a bind-pose vertex into the animated model space, so
  // the instance rows then place it in the world exactly as they would place an
  // unskinned vertex.
  if (input.boneInfo.y != 0) {
    float totalWeight = input.weights.x + input.weights.y + input.weights.z;
    if (totalWeight > 0.0) {
      float3 normalizedWeights = input.weights / totalWeight;
      float3 skinnedPos = float3(0.0, 0.0, 0.0);
      float3 skinnedNormal = float3(0.0, 0.0, 0.0);
      for (uint i = 0; i < 3; ++i) {
        if (normalizedWeights[i] > 0.0 && input.boneIndices[i] < input.boneInfo.z) {
          uint bone = input.boneInfo.x + input.boneIndices[i];
          skinnedPos += mul(local, boneMatrices[bone]).xyz * normalizedWeights[i];
          skinnedNormal += mul(float4(input.normal, 0.0), boneMatrices[bone]).xyz * normalizedWeights[i];
        }
      }
      local = float4(skinnedPos, 1.0);
      normalLocal = skinnedNormal;
    }
  }
  float3 mapped = float3(dot(input.row0, local), dot(input.row1, local), dot(input.row2, local));
  output.position = mul(float4(mapped, 1.0), mvp);
  output.colour = input.colour;
  output.uv = input.uv;
  float3 rotated = float3(dot(input.nrow0.xyz, normalLocal), dot(input.nrow1.xyz, normalLocal), dot(input.nrow2.xyz, normalLocal));
  float lengthSquared = dot(rotated, rotated);
  output.normal = (lengthSquared > 1e-8 && lengthSquared < 1e8) ? rotated * rsqrt(lengthSquared) : float3(0.0, 0.0, 1.0);
  output.worldPosition = mapped;
  output.lightUv = float2(-1.0, -1.0);
  return output;
}
)HLSL";
  // The first-person pass has its own shader pair. It is not the entity shader
  // with a different constant buffer: the entity shader takes its placement from
  // per-instance rows (TEXCOORD1..6) and its skin offset from a packed uint4 in
  // the instance stream, neither of which exists here. The ViewModel gets one
  // composed mvp, a tint, and the skeleton in a structured buffer it indexes
  // from bone 0, with boneCount in a constant so an out-of-range index is
  // rejected in the shader exactly as the entity path does.
  static constexpr char viewModelVertexSource[] = R"HLSL(
cbuffer ViewModelConstants : register(b0) {
  row_major float4x4 mvp;
  float4 tint;
  float4 skinInfo;
  float4 material;
};
StructuredBuffer<float4x4> viewModelBones : register(t4);
struct Input {
  float3 position : POSITION;
  float3 normal : NORMAL0;
  float2 uv : TEXCOORD0;
  float3 weights : BLENDWEIGHT;
  uint4 boneIndices : BLENDINDICES;
};
struct Output {
  float4 position : SV_POSITION;
  float3 normal : NORMAL0;
  float2 uv : TEXCOORD0;
  float4 colour : COLOR0;
};
Output main(Input input) {
  Output output;
  float4 local = float4(input.position, 1.0);
  float3 normalLocal = input.normal;
  // skinInfo.x is the skeleton size, or 0 when the caller could not build a
  // pose. 0 means bind pose: the mesh is drawn unskinned rather than skinned
  // with a table that would put it somewhere invented.
  if (skinInfo.x > 0.0) {
    float totalWeight = input.weights.x + input.weights.y + input.weights.z;
    if (totalWeight > 0.0) {
      float3 normalizedWeights = input.weights / totalWeight;
      float3 skinnedPos = float3(0.0, 0.0, 0.0);
      float3 skinnedNormal = float3(0.0, 0.0, 0.0);
      for (uint i = 0; i < 3; ++i) {
        if (normalizedWeights[i] > 0.0 && input.boneIndices[i] < (uint)skinInfo.x) {
          skinnedPos += mul(local, viewModelBones[input.boneIndices[i]]).xyz * normalizedWeights[i];
          skinnedNormal += mul(float4(input.normal, 0.0), viewModelBones[input.boneIndices[i]]).xyz * normalizedWeights[i];
        }
      }
      local = float4(skinnedPos, 1.0);
      normalLocal = skinnedNormal;
    }
  }
  // mvp already carries attachment -> model -> hand -> camera -> projection, so
  // the hand mirror is applied once, here, and never to the world skeleton.
  output.position = mul(local, mvp);
  output.normal = normalLocal;
  // The mesh's own TEXCOORD0. It was decoded from the VVD from the first
  // version of this pass and thrown away here, because the pixel shader had no
  // texture to sample; the material chain is what made it worth carrying.
  output.uv = input.uv;
  output.colour = tint;
  return output;
}
)HLSL";
  // The material stage. Two things this shader is careful about:
  //
  //   * The fallback is a *branch on a constant*, not an unbound sampler. A
  //     sampler with no resource is read as garbage, not as white, so a range
  //     with no material would draw noise rather than the flat colour the gate
  //     pins. `material.x` is the flag that keeps the two apart.
  //   * The tint is the fallback colour, not a multiplier on the albedo. It was
  //     chosen (0.78, 0.78, 0.84) when it *was* the weapon's colour; applying it
  //     to a real albedo would darken a correct texture by 22% for no reason a
  //     reader could see, and the two paths are already told apart by
  //     `texturesUploaded` and `fallbackSlots` rather than by a brightness.
  static constexpr char viewModelPixelSource[] = R"HLSL(
cbuffer ViewModelConstants : register(b0) {
  row_major float4x4 mvp;
  float4 tint;
  float4 skinInfo;
  float4 material;
};
Texture2D albedo : register(t0);
SamplerState albedoSampler : register(s0);
struct Input { float4 position : SV_POSITION; float3 normal : NORMAL0; float2 uv : TEXCOORD0; float4 colour : COLOR0; };
float4 main(Input input) : SV_TARGET {
  float3 normal = normalize(input.normal);
  float diffuse = saturate(dot(normal, normalize(float3(-0.30, 0.42, 0.86))));
  float lighting = 0.55 + 0.45 * diffuse;
  if (material.x > 0.5) {
    float4 texel = albedo.Sample(albedoSampler, input.uv);
    // `$alphatest`: the material's own key, not a threshold invented here.
    if (material.y > 0.5 && texel.a < 0.5) discard;
    return float4(texel.rgb * lighting, texel.a * input.colour.a);
  }
  return float4(input.colour.rgb * lighting, input.colour.a);
}
)HLSL";
  Microsoft::WRL::ComPtr<ID3DBlob> worldVertexBytecode;
  Microsoft::WRL::ComPtr<ID3DBlob> worldPixelBytecode;
  Microsoft::WRL::ComPtr<ID3DBlob> modelVertexBytecode;
  Microsoft::WRL::ComPtr<ID3DBlob> modelPixelBytecode;
  Microsoft::WRL::ComPtr<ID3DBlob> entityModelVertexBytecode;
  Microsoft::WRL::ComPtr<ID3DBlob> viewModelVertexBytecode;
  Microsoft::WRL::ComPtr<ID3DBlob> viewModelPixelBytecode;
  HRESULT worldShaderResult = S_OK;
  initStage_ = "compileShaders.world";
  const bool worldShadersCompiled = compileShader(worldVertexSource, "main", "vs_5_0", worldVertexBytecode, &worldShaderResult)
      && compileShader(worldPixelSource, "main", "ps_5_0", worldPixelBytecode, &worldShaderResult);
  if (!worldShadersCompiled) { lastError_ = worldShaderResult; return false; }
  HRESULT modelShaderResult = S_OK;
  if (!compileShader(modelVertexSource, "main", "vs_5_0", modelVertexBytecode, &modelShaderResult)) {
    lastError_ = modelShaderResult;
    return false;
  }
  if (!compileShader(modelPixelSource, "main", "ps_5_0", modelPixelBytecode, &modelShaderResult)) {
    lastError_ = modelShaderResult;
    return false;
  }
  HRESULT entityShaderResult = S_OK;
  if (!compileShader(entityModelVertexSource, "main", "vs_5_0", entityModelVertexBytecode, &entityShaderResult)) {
    lastError_ = entityShaderResult;
    return false;
  }
  HRESULT viewModelShaderResult = S_OK;
  initStage_ = "compileShaders.viewModel";
  if (!compileShader(viewModelVertexSource, "main", "vs_5_0", viewModelVertexBytecode, &viewModelShaderResult)
      || !compileShader(viewModelPixelSource, "main", "ps_5_0", viewModelPixelBytecode, &viewModelShaderResult)) {
    lastError_ = viewModelShaderResult;
    return false;
  }
  initStage_ = "createShaders.singleModel";
  if (FAILED(device_->CreateVertexShader(vertexBytecode->GetBufferPointer(), vertexBytecode->GetBufferSize(),
      nullptr, vertexShader_.GetAddressOf()))
      || FAILED(device_->CreatePixelShader(pixelBytecode->GetBufferPointer(), pixelBytecode->GetBufferSize(),
      nullptr, pixelShader_.GetAddressOf()))) return false;
  {
    const bool shadersCreated = SUCCEEDED(device_->CreateVertexShader(worldVertexBytecode->GetBufferPointer(), worldVertexBytecode->GetBufferSize(), nullptr, worldVertexShader_.GetAddressOf()))
      && SUCCEEDED(device_->CreatePixelShader(worldPixelBytecode->GetBufferPointer(), worldPixelBytecode->GetBufferSize(), nullptr, worldPixelShader_.GetAddressOf()));
    if (!shadersCreated) { lastError_ = E_FAIL; return false; }
    const D3D11_INPUT_ELEMENT_DESC worldElements[] = {
      {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 28, D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 36, D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT, 0, 48, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    initStage_ = "layout.world";
    const bool layoutCreated = SUCCEEDED(device_->CreateInputLayout(worldElements, 5, worldVertexBytecode->GetBufferPointer(), worldVertexBytecode->GetBufferSize(), worldInputLayout_.GetAddressOf()));
    if (!layoutCreated) {
      worldVertexShader_.Reset(); worldPixelShader_.Reset(); worldInputLayout_.Reset();
      lastError_ = E_FAIL;
      return false;
    }
    const D3D11_INPUT_ELEMENT_DESC modelElements[] = {
      {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 28, D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 36, D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"BLENDWEIGHT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 48, D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"BLENDINDICES", 0, DXGI_FORMAT_R8G8B8A8_UINT, 0, 60, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    const bool modelShaderCreated = SUCCEEDED(device_->CreateVertexShader(modelVertexBytecode->GetBufferPointer(), modelVertexBytecode->GetBufferSize(), nullptr, modelVertexShader_.GetAddressOf()))
      && SUCCEEDED(device_->CreatePixelShader(modelPixelBytecode->GetBufferPointer(), modelPixelBytecode->GetBufferSize(), nullptr, modelPixelShader_.GetAddressOf()));
    initStage_ = "layout.model";
    const bool modelLayoutCreated = modelShaderCreated && SUCCEEDED(device_->CreateInputLayout(modelElements, 6,
      modelVertexBytecode->GetBufferPointer(), modelVertexBytecode->GetBufferSize(), modelInputLayout_.GetAddressOf()));
    if (!modelLayoutCreated) {
      worldVertexShader_.Reset(); worldPixelShader_.Reset(); worldInputLayout_.Reset(); modelVertexShader_.Reset(); modelPixelShader_.Reset(); modelInputLayout_.Reset();
      lastError_ = E_FAIL;
      return false;
    }
    const D3D11_INPUT_ELEMENT_DESC entityElements[] = {
      {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"BLENDWEIGHT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"BLENDINDICES", 0, DXGI_FORMAT_R8G8B8A8_UINT, 0, 44, D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"TEXCOORD", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 0, D3D11_INPUT_PER_INSTANCE_DATA, 1},
      {"TEXCOORD", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 16, D3D11_INPUT_PER_INSTANCE_DATA, 1},
      {"TEXCOORD", 3, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 32, D3D11_INPUT_PER_INSTANCE_DATA, 1},
      {"TEXCOORD", 4, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 48, D3D11_INPUT_PER_INSTANCE_DATA, 1},
      {"TEXCOORD", 5, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 64, D3D11_INPUT_PER_INSTANCE_DATA, 1},
      {"TEXCOORD", 6, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 80, D3D11_INPUT_PER_INSTANCE_DATA, 1},
      {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 96, D3D11_INPUT_PER_INSTANCE_DATA, 1},
      // boneOffset / skinEnabled / padding, read as one uint4 so the layout has
      // no gap the runtime has to guess about.
      {"TEXCOORD", 7, DXGI_FORMAT_R32G32B32A32_UINT, 1, 112, D3D11_INPUT_PER_INSTANCE_DATA, 1},
    };
    initStage_ = "createShaders.entity";
    const bool entityShaderCreated = SUCCEEDED(device_->CreateVertexShader(
      entityModelVertexBytecode->GetBufferPointer(), entityModelVertexBytecode->GetBufferSize(),
      nullptr, entityModelVertexShader_.GetAddressOf()));
    // The element count is derived, not written a second time. It was a literal
    // 11 while this array already had 13 entries, so the layout stopped at
    // TEXCOORD5; CreateInputLayout then refused because the shader also reads
    // TEXCOORD6, COLOR0 and TEXCOORD7, and the only symptom was initialize()
    // returning E_FAIL with the entity path never running at all.
    static_assert(sizeof(entityElements) / sizeof(entityElements[0]) == 13,
      "entity input layout element count");
    constexpr UINT entityElementCount =
      static_cast<UINT>(sizeof(entityElements) / sizeof(entityElements[0]));
    initStage_ = "layout.entity";
    const bool entityLayoutCreated = entityShaderCreated && SUCCEEDED(device_->CreateInputLayout(
      entityElements, entityElementCount, entityModelVertexBytecode->GetBufferPointer(),
      entityModelVertexBytecode->GetBufferSize(), entityModelInputLayout_.GetAddressOf()));
    if (!entityLayoutCreated) {
      worldVertexShader_.Reset(); worldPixelShader_.Reset(); worldInputLayout_.Reset();
      modelVertexShader_.Reset(); modelPixelShader_.Reset(); modelInputLayout_.Reset();
      entityModelVertexShader_.Reset(); entityModelInputLayout_.Reset();
      lastError_ = E_FAIL;
      return false;
    }
    // First-person pass objects. Its input layout is per-vertex only: the
    // ViewModel has one instance, so the instance rows the entity layout carries
    // (TEXCOORD1..7) would be a stream nothing ever binds.
    const D3D11_INPUT_ELEMENT_DESC viewModelElements[] = {
      {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"BLENDWEIGHT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"BLENDINDICES", 0, DXGI_FORMAT_R8G8B8A8_UINT, 0, 44, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    constexpr UINT viewModelElementCount =
      static_cast<UINT>(sizeof(viewModelElements) / sizeof(viewModelElements[0]));
    initStage_ = "createShaders.viewModelObjects";
    const bool viewModelShaderCreated = SUCCEEDED(device_->CreateVertexShader(
      viewModelVertexBytecode->GetBufferPointer(), viewModelVertexBytecode->GetBufferSize(),
      nullptr, viewModelVertexShader_.GetAddressOf()))
      && SUCCEEDED(device_->CreatePixelShader(
      viewModelPixelBytecode->GetBufferPointer(), viewModelPixelBytecode->GetBufferSize(),
      nullptr, viewModelPixelShader_.GetAddressOf()));
    initStage_ = "layout.viewModel";
    const bool viewModelLayoutCreated = viewModelShaderCreated && SUCCEEDED(device_->CreateInputLayout(
      viewModelElements, viewModelElementCount, viewModelVertexBytecode->GetBufferPointer(),
      viewModelVertexBytecode->GetBufferSize(), viewModelInputLayout_.GetAddressOf()));
    if (!viewModelLayoutCreated) {
      viewModelVertexShader_.Reset(); viewModelPixelShader_.Reset(); viewModelInputLayout_.Reset();
      lastError_ = E_FAIL;
      return false;
    }
    // The pass's own constant buffer and depth state. The depth state writes and
    // tests less-equal, and the pass clears depth before drawing, so the weapon
    // is never clipped by the map around it.
    D3D11_BUFFER_DESC viewModelConstantsDescription{};
    viewModelConstantsDescription.ByteWidth = sizeof(ViewModelConstantsData);
    viewModelConstantsDescription.Usage = D3D11_USAGE_DEFAULT;
    viewModelConstantsDescription.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    initStage_ = "createBuffers.viewModelConstants";
    if (FAILED(device_->CreateBuffer(&viewModelConstantsDescription, nullptr,
        viewModelConstants_.GetAddressOf()))) {
      lastError_ = E_FAIL;
      return false;
    }
    D3D11_DEPTH_STENCIL_DESC viewModelDepthDescription{};
    viewModelDepthDescription.DepthEnable = TRUE;
    viewModelDepthDescription.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    viewModelDepthDescription.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
    viewModelDepthDescription.StencilEnable = FALSE;
    initStage_ = "createDepthState.viewModel";
    if (FAILED(device_->CreateDepthStencilState(&viewModelDepthDescription,
        viewModelDepthStencilState_.GetAddressOf()))) {
      lastError_ = E_FAIL;
      return false;
    }
    viewModelDepthWrite_ = viewModelDepthDescription.DepthWriteMask == D3D11_DEPTH_WRITE_MASK_ALL;
    viewModelDepthFunc_ = viewModelDepthDescription.DepthFunc == D3D11_COMPARISON_LESS_EQUAL
      ? "less_equal" : "other";
    D3D11_RASTERIZER_DESC viewModelRasterDescription{};
    viewModelRasterDescription.FillMode = D3D11_FILL_SOLID;
    viewModelRasterDescription.CullMode = D3D11_CULL_NONE;
    viewModelRasterDescription.FrontCounterClockwise = FALSE;
    viewModelRasterDescription.DepthClipEnable = TRUE;
    initStage_ = "createRasterState.viewModel";
    if (FAILED(device_->CreateRasterizerState(&viewModelRasterDescription,
        viewModelRasterizerState_.GetAddressOf()))) {
      lastError_ = E_FAIL;
      return false;
    }
    // The alpha state a `$translucent` material switches to. Created here rather
    // than lazily at the first translucent range: a state object created inside
    // the draw path would be a per-frame creation of exactly the kind this pass
    // is now monitored for.
    D3D11_BLEND_DESC viewModelBlendDescription{};
    viewModelBlendDescription.RenderTarget[0].BlendEnable = TRUE;
    viewModelBlendDescription.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    viewModelBlendDescription.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    viewModelBlendDescription.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    viewModelBlendDescription.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    viewModelBlendDescription.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    viewModelBlendDescription.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    viewModelBlendDescription.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    initStage_ = "createBlendState.viewModel";
    if (FAILED(device_->CreateBlendState(&viewModelBlendDescription,
        viewModelAlphaBlendState_.GetAddressOf()))) {
      lastError_ = E_FAIL;
      return false;
    }
  }
  initStage_ = "samplerStates";
  D3D11_SAMPLER_DESC samplerDescription{};
  samplerDescription.Filter = settings_.anisotropicLevel > 1 ? D3D11_FILTER_ANISOTROPIC : D3D11_FILTER_MIN_MAG_MIP_LINEAR;
  samplerDescription.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
  samplerDescription.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
  samplerDescription.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
  samplerDescription.MaxAnisotropy = static_cast<UINT>(settings_.anisotropicLevel);
  samplerDescription.MipLODBias = static_cast<FLOAT>(settings_.textureMipBias);
  samplerDescription.MaxLOD = D3D11_FLOAT32_MAX;
  if (FAILED(device_->CreateSamplerState(&samplerDescription, sampler_.GetAddressOf()))) return false;
  D3D11_DEPTH_STENCIL_DESC depthDescription{};
  depthDescription.DepthEnable = TRUE;
  depthDescription.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
  depthDescription.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
  if (FAILED(device_->CreateDepthStencilState(&depthDescription, depthStencilState_.GetAddressOf()))) return false;
  D3D11_RASTERIZER_DESC rasterDescription{};
  rasterDescription.FillMode = D3D11_FILL_SOLID;
  rasterDescription.CullMode = D3D11_CULL_BACK;
  rasterDescription.FrontCounterClockwise = FALSE;
  rasterDescription.DepthClipEnable = TRUE;
  if (FAILED(device_->CreateRasterizerState(&rasterDescription, rasterizerState_.GetAddressOf()))) return false;
  D3D11_BUFFER_DESC constantsDescription{};
  // Must cover every field of WorldConstantsData in drawFrame(): mvp (64 bytes)
  // plus five float4 blocks (80) is 144, rounded up to 160 so the next block
  // added does not quietly fall off the end. It did: the buffer was 128 while
  // the struct grew to 144, UpdateSubresource copies ByteWidth bytes and says
  // nothing about the rest, and envFeatures read as zero in the shader -- a
  // whole capability that was wired, compiled, counted as bound and never ran.
  constantsDescription.ByteWidth = 160;
  constantsDescription.Usage = D3D11_USAGE_DEFAULT;
  constantsDescription.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
  if (FAILED(device_->CreateBuffer(&constantsDescription, nullptr, worldConstants_.GetAddressOf()))) return false;
  D3D11_BUFFER_DESC modelConstantsDescription{};
  modelConstantsDescription.ByteWidth = 8208;
  modelConstantsDescription.Usage = D3D11_USAGE_DEFAULT;
  modelConstantsDescription.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
  if (FAILED(device_->CreateBuffer(&modelConstantsDescription, nullptr, modelSkinningConstants_.GetAddressOf()))) return false;
  initStage_ = "testTexture";
  const std::vector<std::uint8_t> testPixels = {
    180, 40, 35, 255, 35, 45, 60, 255,
    35, 45, 60, 255, 180, 40, 35, 255,
  };
  if (!texture_.create(device_.Get(), testPixels, 2, 2)) {
    lastError_ = E_FAIL;
    return false;
  }
  return true;
}

bool Renderer::uploadTexture(const std::vector<std::uint8_t>& rgba, UINT width, UINT height) {
  std::string error;
  if (!device_ || !texture_.create(device_.Get(), rgba, width, height, &error)) {
    if (!error.empty()) OutputDebugStringA(("Texture2D upload failed: " + error + "\n").c_str());
    lastError_ = E_INVALIDARG;
    return false;
  }
  return true;
}

bool Renderer::uploadWorldGeometry(const BspMap& map, const std::string& texturedMaterial, UINT textureWidth, UINT textureHeight) {
  if (!device_ || !map.valid || map.triangles.empty()) {
    lastError_ = E_INVALIDARG;
    return false;
  }
  float minX = std::numeric_limits<float>::max(), maxX = std::numeric_limits<float>::lowest();
  float minY = std::numeric_limits<float>::max(), maxY = std::numeric_limits<float>::lowest();
  float minZ = std::numeric_limits<float>::max(), maxZ = std::numeric_limits<float>::lowest();
  for (const auto& triangle : map.triangles) {
    for (const auto& point : {triangle.a, triangle.b, triangle.c}) {
      minX = std::min(minX, point.x); maxX = std::max(maxX, point.x);
      minY = std::min(minY, point.y); maxY = std::max(maxY, point.y);
      minZ = std::min(minZ, point.z); maxZ = std::max(maxZ, point.z);
    }
  }
  const float spanX = std::max(maxX - minX, 1.0f), spanY = std::max(maxY - minY, 1.0f), spanZ = std::max(maxZ - minZ, 1.0f);
  const float horizontalSpan = std::max(spanX, spanY), horizontalScale = 1.8f / horizontalSpan;
  const float centerX = (minX + maxX) * 0.5f, centerY = (minY + maxY) * 0.5f;
  std::vector<WorldVertex> vertices;
  vertices.reserve(map.triangles.size() * 3);
  std::size_t lightmappedVertices = 0;
  std::size_t displacedTriangles = 0;
  std::size_t waterTriangles = 0;
  const auto convert = [&](const BspVertex& point, const BspVertex& normal) {
    return WorldVertex{(point.x - centerX) * horizontalScale, (point.y - centerY) * horizontalScale, (point.z - minZ) / spanZ * 0.8f + 0.1f, 0.62f, 0.65f, 0.68f, 1.0f, 0.0f, 0.0f, normal.x, normal.y, normal.z};
  };
  for (const auto& triangle : map.triangles) {
    const std::uint32_t hash = static_cast<std::uint32_t>(std::hash<std::string>{}(triangle.material));
    const float shade = 0.78f + static_cast<float>(hash % 23) / 100.0f;
    const bool textured = !texturedMaterial.empty() && triangle.material == texturedMaterial && textureWidth > 0 && textureHeight > 0;
    const auto ab = BspVertex{triangle.b.x - triangle.a.x, triangle.b.y - triangle.a.y, triangle.b.z - triangle.a.z};
    const auto ac = BspVertex{triangle.c.x - triangle.a.x, triangle.c.y - triangle.a.y, triangle.c.z - triangle.a.z};
    const auto nx = ab.y * ac.z - ab.z * ac.y;
    const auto ny = ab.z * ac.x - ab.x * ac.z;
    const auto nz = ab.x * ac.y - ab.y * ac.x;
    const auto normalLength = std::sqrt(nx * nx + ny * ny + nz * nz);
    const auto normal = normalLength > 1e-6f ? BspVertex{nx / normalLength, ny / normalLength, nz / normalLength} : BspVertex{0.0f, 0.0f, 1.0f};
    auto a = convert(triangle.a, normal), b = convert(triangle.b, normal), c = convert(triangle.c, normal);
    a.r = shade; a.g = shade; a.b = shade; a.a = textured ? 1.0f : 0.0f;
    b.r = shade; b.g = shade; b.b = shade; b.a = textured ? 1.0f : 0.0f;
    c.r = shade; c.g = shade; c.b = shade; c.a = textured ? 1.0f : 0.0f;
    const float invWidth = textured ? 1.0f / static_cast<float>(textureWidth) : 0.0f;
    const float invHeight = textured ? 1.0f / static_cast<float>(textureHeight) : 0.0f;
    a.u = triangle.au * invWidth; a.v = triangle.av * invHeight; b.u = triangle.bu * invWidth; b.v = triangle.bv * invHeight; c.u = triangle.cu * invWidth; c.v = triangle.cv * invHeight;
    if (triangle.lightmapped) { a.lu = triangle.alu; a.lv = triangle.alv; b.lu = triangle.blu; b.lv = triangle.blv; c.lu = triangle.clu; c.lv = triangle.clv; }
    // Same outcome-based count as the atlas overload below: keyed on the vertex
    // the pixel shader will actually see, not on the triangle's claim.
    if (a.lu >= 0.0f) lightmappedVertices += 3;
    // Counted here rather than trusted from the parse: this loop is what decides
    // how many triangles exist, and both tags are the parser's own on the
    // triangle it emitted -- a displacement face arrives already subdivided into
    // its grid, so a displaced triangle here is a tessellated one and the count
    // is the geometry the renderer will draw.
    if (triangle.displacement) ++displacedTriangles;
    if (triangle.water) ++waterTriangles;
    vertices.push_back(a); vertices.push_back(b); vertices.push_back(c);
  }
  D3D11_BUFFER_DESC description{};
  description.ByteWidth = static_cast<UINT>(vertices.size() * sizeof(WorldVertex));
  description.Usage = D3D11_USAGE_DEFAULT;
  description.BindFlags = D3D11_BIND_VERTEX_BUFFER;
  D3D11_SUBRESOURCE_DATA initial{}; initial.pSysMem = vertices.data();
  Microsoft::WRL::ComPtr<ID3D11Buffer> buffer;
  if (FAILED(device_->CreateBuffer(&description, &initial, buffer.GetAddressOf()))) {
    lastError_ = E_FAIL;
    return false;
  }
  worldVertexBuffer_ = std::move(buffer);
  worldVertexCount_ = static_cast<UINT>(vertices.size());
  worldLightmappedVertexCount_ = static_cast<UINT>(lightmappedVertices);
  worldDisplacedTriangleCount_ = static_cast<UINT>(displacedTriangles);
  worldWaterTriangleCount_ = static_cast<UINT>(waterTriangles);
  worldMinX_ = minX; worldMinY_ = minY; worldMinZ_ = minZ;
  worldMaxX_ = maxX; worldMaxY_ = maxY; worldMaxZ_ = maxZ;
  worldCenterX_ = centerX; worldCenterY_ = centerY;
  worldSpanZ_ = spanZ; worldHorizontalScale_ = horizontalScale;
  worldBoundsValid_ = true;
  worldLightmapStatus_ = WorldLightmapStatus::FallbackUnlit;
  worldLightmapIntensity_ = std::clamp(map.lightmapIntensity, 0.15f, 2.0f);
  if (map.lightmapTriangleCount > 0) worldLightmapStatus_ = WorldLightmapStatus::Active;
  return true;
}

bool Renderer::uploadWorldGeometry(const BspMap& map, const std::vector<WorldTexture>& textures) {
  if (!device_ || !map.valid || map.triangles.empty()) {
    lastError_ = E_INVALIDARG;
    return false;
  }
  constexpr UINT kTileSize = 514;
  constexpr std::size_t kMaxTiles = 64;
  struct Tile { UINT x, y, width, height, sourceWidth, sourceHeight; };
  std::unordered_map<std::string, Tile> tiles;
  std::vector<const WorldTexture*> accepted;
  accepted.reserve(kMaxTiles);
  for (const auto& source : textures) {
    if (source.material.empty() || source.width == 0 || source.height == 0
        || source.width > kTileSize - 2 || source.height > kTileSize - 2
        || source.rgba.size() != static_cast<std::size_t>(source.width) * source.height * 4u
        || tiles.find(source.material) != tiles.end() || accepted.size() >= kMaxTiles) continue;
    accepted.push_back(&source);
    tiles.emplace(source.material, Tile{});
  }
  const std::size_t grid = accepted.size() <= 1 ? 1 : accepted.size() <= 4 ? 2 : accepted.size() <= 16 ? 4 : 8;
  const UINT atlasSize = static_cast<UINT>(grid * kTileSize);
  std::vector<std::uint8_t> atlas(static_cast<std::size_t>(atlasSize) * atlasSize * 4u, 88u);
  std::size_t tileIndex = 0;
  for (const auto* source : accepted) {
    const UINT x = static_cast<UINT>(tileIndex % grid) * kTileSize + 1;
    const UINT y = static_cast<UINT>(tileIndex / grid) * kTileSize + 1;
    for (UINT row = 0; row < source->height; ++row) {
      std::memcpy(atlas.data() + (static_cast<std::size_t>(y + row) * atlasSize + x) * 4u,
        source->rgba.data() + static_cast<std::size_t>(row) * source->width * 4u,
        static_cast<std::size_t>(source->width) * 4u);
      std::memcpy(atlas.data() + (static_cast<std::size_t>(y + row) * atlasSize + x - 1) * 4u,
        source->rgba.data() + static_cast<std::size_t>(row) * source->width * 4u, 4u);
      std::memcpy(atlas.data() + (static_cast<std::size_t>(y + row) * atlasSize + x + source->width) * 4u,
        source->rgba.data() + (static_cast<std::size_t>(row) * source->width + source->width - 1) * 4u, 4u);
    }
    std::memcpy(atlas.data() + (static_cast<std::size_t>(y - 1) * atlasSize + x - 1) * 4u,
      atlas.data() + (static_cast<std::size_t>(y) * atlasSize + x - 1) * 4u,
      static_cast<std::size_t>(source->width + 2) * 4u);
    std::memcpy(atlas.data() + (static_cast<std::size_t>(y + source->height) * atlasSize + x - 1) * 4u,
      atlas.data() + (static_cast<std::size_t>(y + source->height - 1) * atlasSize + x - 1) * 4u,
      static_cast<std::size_t>(source->width + 2) * 4u);
    tiles[source->material] = Tile{x, y, source->width, source->height, source->width, source->height};
    ++tileIndex;
  }
  if (tileIndex == 0) {
    lastError_ = HRESULT_FROM_WIN32(ERROR_NO_DATA);
    return false;
  }
  if (!worldTexture_.create(device_.Get(), atlas, atlasSize, atlasSize)) {
    lastError_ = E_FAIL;
    return false;
  }

  float minX = std::numeric_limits<float>::max(), maxX = std::numeric_limits<float>::lowest();
  float minY = std::numeric_limits<float>::max(), maxY = std::numeric_limits<float>::lowest();
  float minZ = std::numeric_limits<float>::max(), maxZ = std::numeric_limits<float>::lowest();
  for (const auto& triangle : map.triangles) for (const auto& point : {triangle.a, triangle.b, triangle.c}) {
    minX = std::min(minX, point.x); maxX = std::max(maxX, point.x);
    minY = std::min(minY, point.y); maxY = std::max(maxY, point.y);
    minZ = std::min(minZ, point.z); maxZ = std::max(maxZ, point.z);
  }
  const float spanX = std::max(maxX - minX, 1.0f), spanY = std::max(maxY - minY, 1.0f), spanZ = std::max(maxZ - minZ, 1.0f);
  const float horizontalSpan = std::max(spanX, spanY), horizontalScale = 1.8f / horizontalSpan;
  const float centerX = (minX + maxX) * 0.5f, centerY = (minY + maxY) * 0.5f;
  std::vector<WorldVertex> vertices;
  vertices.reserve(map.triangles.size() * 3);
  std::size_t lightmappedVertices = 0;
  std::size_t displacedTriangles = 0;
  std::size_t waterTriangles = 0;
  const auto convert = [&](const BspVertex& point, const BspVertex& normal) {
    return WorldVertex{(point.x - centerX) * horizontalScale, (point.y - centerY) * horizontalScale, (point.z - minZ) / spanZ * 0.8f + 0.1f, 0.62f, 0.65f, 0.68f, 0.0f, 0.0f, 0.0f, normal.x, normal.y, normal.z};
  };
  const auto atlasCoord = [&](float value, float sourceSize, UINT offset, UINT extent) {
    const float wrapped = value - std::floor(value / sourceSize) * sourceSize;
    return (static_cast<float>(offset) + wrapped * static_cast<float>(extent) / sourceSize + 0.5f) / static_cast<float>(atlasSize);
  };
  for (const auto& triangle : map.triangles) {
    const auto ab = BspVertex{triangle.b.x - triangle.a.x, triangle.b.y - triangle.a.y, triangle.b.z - triangle.a.z};
    const auto ac = BspVertex{triangle.c.x - triangle.a.x, triangle.c.y - triangle.a.y, triangle.c.z - triangle.a.z};
    const float nx = ab.y * ac.z - ab.z * ac.y, ny = ab.z * ac.x - ab.x * ac.z, nz = ab.x * ac.y - ab.y * ac.x;
    const float normalLength = std::sqrt(nx * nx + ny * ny + nz * nz);
    const auto normal = normalLength > 1e-6f ? BspVertex{nx / normalLength, ny / normalLength, nz / normalLength} : BspVertex{0.0f, 0.0f, 1.0f};
    auto a = convert(triangle.a, normal), b = convert(triangle.b, normal), c = convert(triangle.c, normal);
    const auto tile = tiles.find(triangle.material);
    if (tile != tiles.end()) {
      const auto& t = tile->second;
      a.r = a.g = a.b = b.r = b.g = b.b = c.r = c.g = c.b = 1.0f;
      a.a = b.a = c.a = 1.0f;
      a.u = atlasCoord(triangle.au, static_cast<float>(t.sourceWidth), t.x, t.width); a.v = atlasCoord(triangle.av, static_cast<float>(t.sourceHeight), t.y, t.height);
      b.u = atlasCoord(triangle.bu, static_cast<float>(t.sourceWidth), t.x, t.width); b.v = atlasCoord(triangle.bv, static_cast<float>(t.sourceHeight), t.y, t.height);
      c.u = atlasCoord(triangle.cu, static_cast<float>(t.sourceWidth), t.x, t.width); c.v = atlasCoord(triangle.cv, static_cast<float>(t.sourceHeight), t.y, t.height);
      if (triangle.lightmapped) { a.lu = triangle.alu; a.lv = triangle.alv; b.lu = triangle.blu; b.lv = triangle.blv; c.lu = triangle.clu; c.lv = triangle.clv; }
    } else {
      const std::uint32_t hash = static_cast<std::uint32_t>(std::hash<std::string>{}(triangle.material));
      const float shade = 0.78f + static_cast<float>(hash % 23) / 100.0f;
      a.r = a.g = a.b = b.r = b.g = b.b = c.r = c.g = c.b = shade;
      // The lightmap is per *face*, not per *texture*: a face that missed the
      // 64-tile atlas still has its own RGBExp32 slice, and dropping its light
      // uv here drew it flat. On snakewater the atlas covers 7 materials of
      // ~150, so this branch was where ~16.8 万 lightmapped triangles lost
      // their lighting -- the atlas was bound, the status read Active, and
      // most of the map sampled nothing.
      if (triangle.lightmapped) { a.lu = triangle.alu; a.lv = triangle.alv; b.lu = triangle.blu; b.lv = triangle.blv; c.lu = triangle.clu; c.lv = triangle.clv; }
    }
    // Counted off the vertex, not off the triangle: the pixel shader samples
    // the lightmap when lightUv.x >= 0.0, so a counter keyed on
    // triangle.lightmapped would keep reporting the intent after the uv had
    // been dropped -- which is how a regression injection went unnoticed the
    // first time this instrument was written.
    if (a.lu >= 0.0f) lightmappedVertices += 3;
    // Counted here rather than trusted from the parse: this loop is what decides
    // how many triangles exist, and both tags are the parser's own on the
    // triangle it emitted -- a displacement face arrives already subdivided into
    // its grid, so a displaced triangle here is a tessellated one and the count
    // is the geometry the renderer will draw.
    if (triangle.displacement) ++displacedTriangles;
    if (triangle.water) ++waterTriangles;
    vertices.push_back(a); vertices.push_back(b); vertices.push_back(c);
  }
  D3D11_BUFFER_DESC description{};
  description.ByteWidth = static_cast<UINT>(vertices.size() * sizeof(WorldVertex));
  description.Usage = D3D11_USAGE_DEFAULT;
  description.BindFlags = D3D11_BIND_VERTEX_BUFFER;
  D3D11_SUBRESOURCE_DATA initial{}; initial.pSysMem = vertices.data();
  Microsoft::WRL::ComPtr<ID3D11Buffer> buffer;
  if (FAILED(device_->CreateBuffer(&description, &initial, buffer.GetAddressOf()))) {
    lastError_ = E_FAIL;
    return false;
  }
  worldVertexBuffer_ = std::move(buffer);
  worldVertexCount_ = static_cast<UINT>(vertices.size());
  worldLightmappedVertexCount_ = static_cast<UINT>(lightmappedVertices);
  worldDisplacedTriangleCount_ = static_cast<UINT>(displacedTriangles);
  worldWaterTriangleCount_ = static_cast<UINT>(waterTriangles);
  worldMinX_ = minX; worldMinY_ = minY; worldMinZ_ = minZ;
  worldMaxX_ = maxX; worldMaxY_ = maxY; worldMaxZ_ = maxZ;
  worldCenterX_ = centerX; worldCenterY_ = centerY;
  worldSpanZ_ = spanZ; worldHorizontalScale_ = horizontalScale;
  worldBoundsValid_ = true;
  worldLightmapStatus_ = WorldLightmapStatus::FallbackUnlit;
  worldLightmapIntensity_ = std::clamp(map.lightmapIntensity, 0.15f, 2.0f);
  if (map.lightmapTriangleCount > 0) worldLightmapStatus_ = WorldLightmapStatus::Active;
  return true;
}

bool Renderer::uploadWorldAuxTextures(const std::vector<std::uint8_t>& bumpRgba, UINT bumpWidth, UINT bumpHeight,
    const std::vector<std::uint8_t>& envRgba, UINT envWidth, UINT envHeight) {
  worldBumpTexture_.reset();
  worldEnvTexture_.reset();
  std::string error;
  if (!bumpRgba.empty() && !worldBumpTexture_.create(device_.Get(), bumpRgba, bumpWidth, bumpHeight, &error)) {
    if (!error.empty()) OutputDebugStringA(("Bump Texture2D upload failed: " + error + "\n").c_str());
    return false;
  }
  error.clear();
  if (!envRgba.empty() && !worldEnvTexture_.create(device_.Get(), envRgba, envWidth, envHeight, &error)) {
    if (!error.empty()) OutputDebugStringA(("Env Texture2D upload failed: " + error + "\n").c_str());
    return false;
  }
  return true;
}

bool Renderer::uploadWorldLightmap(const std::vector<std::uint8_t>& rgba, UINT width, UINT height) {
  worldLightmapTexture_.reset();
  worldCubemapTexture_.Reset();
  worldCubemapView_.Reset();
  worldCubemapSize_ = 0;
  if (rgba.empty() || width == 0 || height == 0) return true;
  std::string error;
  if (!worldLightmapTexture_.create(device_.Get(), rgba, width, height, &error)) {
    if (!error.empty()) OutputDebugStringA(("Lightmap Texture2D upload failed: " + error + "\n").c_str());
    return false;
  }
  return true;
}

UINT Renderer::worldEnvMode() const {
  // Computed from what is bound and what the material asked for, so it is the
  // mode a world draw would send right now. This is deliberately not a member
  // filled in at draw time: the trace that reports it runs at load, before the
  // first world draw, and a member read there would print its initial zero and
  // be mistaken for "the pass chose not to run".
  const bool wanted = settings_.preset == QualityPreset::Standard && worldMaterialParams_.envMap;
  if (!wanted) return 0;
  if (worldCubemapView_) return 2;
  return worldEnvTexture_.view() ? 1u : 0u;
}

const char* worldCubemapStatusName(WorldCubemapStatus status) {
  switch (status) {
    case WorldCubemapStatus::Unavailable: return "unavailable";
    case WorldCubemapStatus::Approximate2d: return "approximate-2d";
    case WorldCubemapStatus::SixFace: return "six-face";
  }
  return "unknown";
}

bool Renderer::uploadWorldCubemap(const std::array<std::vector<std::uint8_t>, 6>& faces,
    UINT side, std::string* error) {
  worldCubemapTexture_.Reset();
  worldCubemapView_.Reset();
  worldCubemapSize_ = 0;
  const auto fail = [&](const char* message) {
    if (error) *error = message;
    return false;
  };
  // No faces and no side means "this map has no cube", which is not a failure:
  // it clears the previous map's cube and leaves the 2D path in charge, exactly
  // as uploadWorldLightmap treats an empty atlas.
  if (side == 0) {
    bool anyFace = false;
    for (const auto& face : faces) anyFace = anyFace || !face.empty();
    if (!anyFace) return true;
    return fail("a cubemap side of zero was given with face data");
  }
  if ((side & (side - 1)) != 0) {
    return fail("cubemap side is not a power of two, which a TextureCube sample needs");
  }
  const std::size_t expected = static_cast<std::size_t>(side) * side * 4u;
  for (int face = 0; face < 6; ++face) {
    if (faces[static_cast<std::size_t>(face)].size() != expected) {
      return fail("a cubemap face is missing or not side*side*4 bytes");
    }
  }
  D3D11_TEXTURE2D_DESC description{};
  description.Width = side;
  description.Height = side;
  description.MipLevels = 1;
  description.ArraySize = 6;
  description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  description.SampleDesc.Count = 1;
  description.Usage = D3D11_USAGE_IMMUTABLE;
  description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
  // The flag is what makes the six array slices a cube rather than six unrelated
  // 2D slices, and it is what makes the SRV below legal as a cube.
  description.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;
  D3D11_SUBRESOURCE_DATA initial[6]{};
  for (int face = 0; face < 6; ++face) {
    initial[face].pSysMem = faces[static_cast<std::size_t>(face)].data();
    initial[face].SysMemPitch = side * 4u;
    initial[face].SysMemSlicePitch = 0;
  }
  Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
  if (FAILED(device_->CreateTexture2D(&description, initial, texture.GetAddressOf()))) {
    return fail("CreateTexture2D refused the cube");
  }
  D3D11_SHADER_RESOURCE_VIEW_DESC view{};
  view.Format = description.Format;
  view.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
  view.TextureCube.MostDetailedMip = 0;
  view.TextureCube.MipLevels = 1;
  Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> resourceView;
  if (FAILED(device_->CreateShaderResourceView(texture.Get(), &view, resourceView.GetAddressOf()))) {
    return fail("CreateShaderResourceView refused the cube");
  }
  worldCubemapTexture_ = std::move(texture);
  worldCubemapView_ = std::move(resourceView);
  worldCubemapSize_ = side;
  return true;
}

bool Renderer::uploadBindPoseModel(const std::vector<ModelDrawVertex>& vertices) {
  modelVertexBuffer_.Reset(); modelVertexCount_ = 0; modelGpuStatus_ = ModelGpuStatus::NotLoaded;
  if (vertices.empty() || vertices.size() > (std::numeric_limits<UINT>::max)()) { lastError_ = E_INVALIDARG; return false; }
  std::vector<ModelGpuVertex> gpu;
  gpu.reserve(vertices.size());
  std::array<float, 3> minimum = vertices.front().position, maximum = vertices.front().position;
  for (const auto& v : vertices) for (std::size_t axis = 0; axis < 3; ++axis) {
    minimum[axis] = (std::min)(minimum[axis], v.position[axis]);
    maximum[axis] = (std::max)(maximum[axis], v.position[axis]);
  }
  const float extent = (std::max)({maximum[0] - minimum[0], maximum[1] - minimum[1], maximum[2] - minimum[2], 1e-4f});
  const float scale = 1.2f / extent;
  const std::array<float, 3> center = {(minimum[0] + maximum[0]) * 0.5f, (minimum[1] + maximum[1]) * 0.5f, (minimum[2] + maximum[2]) * 0.5f};
  for (const auto& v : vertices) {
    ModelGpuVertex output{};
    output.x = (v.position[0]-center[0])*scale; output.y = (v.position[1]-center[1])*scale;
    output.z = (v.position[2]-center[2])*scale; output.r = 0.72f; output.g = 0.72f;
    output.b = 0.76f; output.a = 1.0f; output.u = v.texcoord[0]; output.v = v.texcoord[1];
    output.nx = v.normal[0]; output.ny = v.normal[1]; output.nz = v.normal[2];
    output.weights[0] = v.weights[0]; output.weights[1] = v.weights[1]; output.weights[2] = v.weights[2];
    output.boneIndices[0] = v.boneIndices[0]; output.boneIndices[1] = v.boneIndices[1];
    output.boneIndices[2] = v.boneIndices[2]; output.boneCount = v.boneCount;
    gpu.push_back(output);
  }
  D3D11_BUFFER_DESC desc{}; desc.ByteWidth = static_cast<UINT>(gpu.size() * sizeof(ModelGpuVertex)); desc.Usage = D3D11_USAGE_DEFAULT; desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
  D3D11_SUBRESOURCE_DATA initial{}; initial.pSysMem = gpu.data();
  if (FAILED(device_->CreateBuffer(&desc, &initial, modelVertexBuffer_.GetAddressOf()))) { lastError_ = E_FAIL; return false; }
  modelVertexCount_ = static_cast<UINT>(gpu.size());
  modelGpuStatus_ = ModelGpuStatus::BindPoseOnly;
  return true;
}

bool Renderer::uploadBoneMatrices(const std::vector<std::array<float, 16>>& boneMatrices, bool enableSkinning) {
  if (!device_ || !context_ || !modelSkinningConstants_) { lastError_ = E_FAIL; return false; }
  constexpr std::size_t maxBones = 128;
  if (boneMatrices.empty() || boneMatrices.size() > maxBones) { lastError_ = E_INVALIDARG; return false; }
  for (const auto& matrix : boneMatrices) {
    for (const float element : matrix) {
      if (!std::isfinite(element)) { lastError_ = E_INVALIDARG; return false; }
    }
  }
  const std::size_t boneCount = boneMatrices.size();
  struct SkinningConstants {
    float bones[128][16];
    std::uint32_t boneCount;
    std::uint32_t skinEnabled;
    std::uint32_t padding[2];
  };
  SkinningConstants constants{};
  for (std::size_t i = 0; i < boneCount; ++i) {
    for (std::size_t j = 0; j < 16; ++j) {
      constants.bones[i][j] = boneMatrices[i][j];
    }
  }
  constants.boneCount = static_cast<std::uint32_t>(boneCount);
  constants.skinEnabled = enableSkinning ? 1u : 0u;
  constants.padding[0] = 0u;
  constants.padding[1] = 0u;
  context_->UpdateSubresource(modelSkinningConstants_.Get(), 0, nullptr, &constants, 0, 0);
  modelGpuStatus_ = enableSkinning ? ModelGpuStatus::SkinningReady : ModelGpuStatus::BindPoseOnly;
  return true;
}


bool Renderer::uploadProjectileLines(const std::vector<ProjectileLine>& lines) {
  projectileVertexBuffer_.Reset();
  projectileVertexCapacity_ = 0;
  projectileVertexCount_ = 0;
  if (!device_ || !worldBoundsValid_ || lines.empty()) return true;
  const std::size_t limited = std::min<std::size_t>(lines.size(), 512u);
  std::vector<WorldVertex> vertices;
  vertices.reserve(limited * 2u);
  const auto convert = [&](const float point[3], const float color[4]) {
    return WorldVertex{(point[0] - worldCenterX_) * worldHorizontalScale_,
      (point[1] - worldCenterY_) * worldHorizontalScale_,
      (point[2] - worldMinZ_) / worldSpanZ_ * 0.8f + 0.1f,
      color[0], color[1], color[2], color[3], 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};
  };
  for (std::size_t i = 0; i < limited; ++i) {
    if (!std::isfinite(lines[i].start[0]) || !std::isfinite(lines[i].start[1]) || !std::isfinite(lines[i].start[2])
        || !std::isfinite(lines[i].end[0]) || !std::isfinite(lines[i].end[1]) || !std::isfinite(lines[i].end[2])) continue;
    vertices.push_back(convert(lines[i].start, lines[i].color));
    vertices.push_back(convert(lines[i].end, lines[i].color));
  }
  if (vertices.empty()) return true;
  D3D11_BUFFER_DESC description{};
  description.ByteWidth = static_cast<UINT>(vertices.size() * sizeof(WorldVertex));
  description.Usage = D3D11_USAGE_DEFAULT;
  description.BindFlags = D3D11_BIND_VERTEX_BUFFER;
  D3D11_SUBRESOURCE_DATA initial{};
  initial.pSysMem = vertices.data();
  Microsoft::WRL::ComPtr<ID3D11Buffer> buffer;
  if (FAILED(device_->CreateBuffer(&description, &initial, buffer.GetAddressOf()))) return false;
  projectileVertexBuffer_ = std::move(buffer);
  projectileVertexCapacity_ = vertices.size();
  projectileVertexCount_ = static_cast<UINT>(vertices.size());
  return true;
}

bool Renderer::createTarget(UINT width, UINT height) {
  if (!swapChain_) return false;
  Microsoft::WRL::ComPtr<ID3D11Texture2D> backBuffer;
  if (FAILED(swapChain_->GetBuffer(0, IID_PPV_ARGS(backBuffer.GetAddressOf())))) return false;
  if (FAILED(device_->CreateRenderTargetView(backBuffer.Get(), nullptr, target_.GetAddressOf()))) return false;
  D3D11_TEXTURE2D_DESC depthDescription{};
  depthDescription.Width = width;
  depthDescription.Height = height;
  depthDescription.MipLevels = 1;
  depthDescription.ArraySize = 1;
  depthDescription.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
  depthDescription.SampleDesc.Count = 1;
  depthDescription.Usage = D3D11_USAGE_DEFAULT;
  depthDescription.BindFlags = D3D11_BIND_DEPTH_STENCIL;
  if (FAILED(device_->CreateTexture2D(&depthDescription, nullptr, depthTexture_.GetAddressOf()))) return false;
  if (FAILED(device_->CreateDepthStencilView(depthTexture_.Get(), nullptr, depthStencilView_.GetAddressOf()))) return false;
  width_ = width;
  height_ = height;
  lastError_ = S_OK;
  return true;
}

void Renderer::releaseTarget() { depthStencilView_.Reset(); depthTexture_.Reset(); target_.Reset(); }

void Renderer::orbitCamera(float deltaX, float deltaY) {
  if (!std::isfinite(deltaX) || !std::isfinite(deltaY)) return;
  observerDemoView_ = false;
  constexpr float kPi = 3.14159265358979323846f;
  const float yaw = std::remainder(cameraYaw_ + deltaX * 0.008f, kPi * 2.0f);
  const float pitch = cameraPitch_ + deltaY * 0.008f;
  if (std::isfinite(yaw)) cameraYaw_ = yaw;
  if (std::isfinite(pitch)) cameraPitch_ = std::clamp(pitch, 0.25f, 1.45f);
}

void Renderer::zoomCamera(float delta) {
  if (!std::isfinite(delta)) return;
  const float distance = cameraDistance_ * std::exp(-delta * 0.08f);
  if (std::isfinite(distance)) cameraDistance_ = std::clamp(distance, 0.8f, 12.0f);
}

void Renderer::setObserverFocus(float x, float y, float z) {
  if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) return;
  observerFocusX_ = std::clamp(x, -1.0e6f, 1.0e6f);
  observerFocusY_ = std::clamp(y, -1.0e6f, 1.0e6f);
  observerFocusZ_ = std::clamp(z, -1.0e6f, 1.0e6f);
  observerFocusValid_ = true;
}

void Renderer::setObserverFocusWorld(float x, float y, float z) {
  if (!worldBoundsValid_ || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)
      || !std::isfinite(worldCenterX_) || !std::isfinite(worldCenterY_)
      || !std::isfinite(worldMinZ_) || !std::isfinite(worldSpanZ_)
      || !std::isfinite(worldHorizontalScale_) || worldSpanZ_ <= 0.0f
      || worldHorizontalScale_ <= 0.0f) return;
  const float normalizedX = (x - worldCenterX_) * worldHorizontalScale_;
  const float normalizedY = (y - worldCenterY_) * worldHorizontalScale_;
  const float normalizedZ = (z - worldMinZ_) / worldSpanZ_ * 0.8f + 0.1f;
  if (!std::isfinite(normalizedX) || !std::isfinite(normalizedY) || !std::isfinite(normalizedZ)) return;
  setObserverFocus(normalizedX, normalizedY, normalizedZ);
}

bool Renderer::observerFocusWorld(float& x, float& y, float& z) const {
  if (!observerFocusValid_) return false;
  x = observerFocusX_; y = observerFocusY_; z = observerFocusZ_;
  return true;
}

void Renderer::clearObserverDemoView() { observerDemoView_ = false; }

void Renderer::setObserverDemoViewWorld(float x, float y, float z, float pitchDegrees, float yawDegrees) {
  if (!std::isfinite(pitchDegrees) || !std::isfinite(yawDegrees)) return;
  setObserverFocusWorld(x, y, z);
  if (!observerFocusValid_) { observerDemoView_ = false; return; }
  observerDemoView_ = true;
  observerDemoEyeX_ = observerFocusX_;
  observerDemoEyeY_ = observerFocusY_;
  observerDemoEyeZ_ = observerFocusZ_;
  observerDemoPitchDeg_ = pitchDegrees;
  observerDemoYawDeg_ = yawDegrees;
}

void Renderer::setWorldMaterialParams(const WorldMaterialParams& params) {
  worldMaterialParams_ = params;
  if (!std::isfinite(worldMaterialParams_.selfIllumR)) worldMaterialParams_.selfIllumR = 0.0f;
  if (!std::isfinite(worldMaterialParams_.selfIllumG)) worldMaterialParams_.selfIllumG = 0.0f;
  if (!std::isfinite(worldMaterialParams_.selfIllumB)) worldMaterialParams_.selfIllumB = 0.0f;
  worldMaterialParams_.selfIllumR = std::clamp(worldMaterialParams_.selfIllumR, 0.0f, 4.0f);
  worldMaterialParams_.selfIllumG = std::clamp(worldMaterialParams_.selfIllumG, 0.0f, 4.0f);
  worldMaterialParams_.selfIllumB = std::clamp(worldMaterialParams_.selfIllumB, 0.0f, 4.0f);
}

WorldMaterialSampleState Renderer::worldMaterialSampleState() const {
  WorldMaterialSampleState state{};
  state.lightmapStatus = worldLightmapStatus_;
  state.lightmapIntensity = worldLightmapIntensity_;
  state.bumpTextureBound = worldBumpTexture_.view() != nullptr;
  state.envTextureBound = worldEnvTexture_.view() != nullptr;
  state.lightmapAtlasBound = worldLightmapTexture_.view() != nullptr;
  state.selfIllumEnabled = worldMaterialParams_.selfIllum;
  state.worldVertexCount = worldVertexCount_;
  state.lightmappedVertexCount = worldLightmappedVertexCount_;
  state.cubemapStatus = worldCubemapView_ ? WorldCubemapStatus::SixFace
    : (worldEnvTexture_.view() ? WorldCubemapStatus::Approximate2d
                              : WorldCubemapStatus::Unavailable);
  state.cubemapFaces = worldCubemapView_ ? 6u : 0u;
  state.cubemapSize = worldCubemapSize_;
  state.envMode = worldEnvMode();
  state.displacedTriangles = worldDisplacedTriangleCount_;
  state.waterTriangles = worldWaterTriangleCount_;
  return state;
}

void Renderer::setProjectileTimeline(const std::vector<ProjectileTimelineEvent>& events, std::int32_t tick) {
  projectileVertexCount_ = 0;
  if (!device_ || !worldBoundsValid_) return;
  std::vector<WorldVertex> vertices;
  vertices.reserve(std::min<std::size_t>(events.size() * 2u, 32768u));
  const auto normalize = [this](float x, float y, float z) {
    return DirectX::XMFLOAT3(
      (x - worldCenterX_) * worldHorizontalScale_,
      (y - worldCenterY_) * worldHorizontalScale_,
      (z - worldMinZ_) / worldSpanZ_ * 0.8f + 0.1f);
  };
  for (const auto& event : events) {
    if (event.tick != tick || !event.hasOrigin) continue;
    const auto start = normalize(event.origin[0], event.origin[1], event.origin[2]);
    if (event.className == "CTETFExplosion" || event.className == "CTEExplosion") {
      const float radius = event.hasRadius
        ? std::clamp(static_cast<float>(event.radius), 8.0f, 512.0f) : 16.0f;
      const auto x = normalize(event.origin[0] + radius, event.origin[1], event.origin[2]);
      const auto y = normalize(event.origin[0], event.origin[1] + radius, event.origin[2]);
      const auto z = normalize(event.origin[0], event.origin[1], event.origin[2] + radius);
      constexpr float color[4] = {1.0f, 0.55f, 0.05f, 1.0f};
      vertices.push_back({start.x, start.y, start.z, color[0], color[1], color[2], color[3], 0.0f, 0.0f, 0.0f, 1.0f, 0.0f});
      vertices.push_back({x.x, x.y, x.z, color[0], color[1], color[2], color[3], 0.0f, 0.0f, 0.0f, 1.0f, 0.0f});
      vertices.push_back({start.x, start.y, start.z, color[0], color[1], color[2], color[3], 0.0f, 0.0f, 0.0f, 1.0f, 0.0f});
      vertices.push_back({y.x, y.y, y.z, color[0], color[1], color[2], color[3], 0.0f, 0.0f, 0.0f, 1.0f, 0.0f});
      vertices.push_back({start.x, start.y, start.z, color[0], color[1], color[2], color[3], 0.0f, 0.0f, 0.0f, 1.0f, 0.0f});
      vertices.push_back({z.x, z.y, z.z, color[0], color[1], color[2], color[3], 0.0f, 0.0f, 0.0f, 1.0f, 0.0f});
      if (vertices.size() >= 32768u) break;
      continue;
    }
    if (!event.hasDirection) continue;
    DirectX::XMFLOAT3 direction(event.direction[0], event.direction[1], event.direction[2]);
    const float length = std::sqrt(direction.x * direction.x + direction.y * direction.y + direction.z * direction.z);
    if (!std::isfinite(length) || length < 1e-4f) continue;
    direction.x /= length; direction.y /= length; direction.z /= length;
    const auto end = normalize(event.origin[0] + direction.x * 96.0f,
                               event.origin[1] + direction.y * 96.0f,
                               event.origin[2] + direction.z * 96.0f);
    constexpr float color[4] = {1.0f, 0.25f, 0.05f, 1.0f};
    vertices.push_back({start.x, start.y, start.z, color[0], color[1], color[2], color[3], 0.0f, 0.0f, 0.0f, 1.0f, 0.0f});
    vertices.push_back({end.x, end.y, end.z, color[0], color[1], color[2], color[3], 0.0f, 0.0f, 0.0f, 1.0f, 0.0f});
    if (vertices.size() >= 32768u) break;
  }
  if (vertices.empty()) return;
  if (!projectileVertexBuffer_ || projectileVertexCapacity_ < vertices.size()) {
    D3D11_BUFFER_DESC description{};
    description.ByteWidth = static_cast<UINT>(vertices.size() * sizeof(WorldVertex));
    description.Usage = D3D11_USAGE_DYNAMIC;
    description.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    description.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(device_->CreateBuffer(&description, nullptr, projectileVertexBuffer_.ReleaseAndGetAddressOf()))) return;
    projectileVertexCapacity_ = vertices.size();
  }
  D3D11_MAPPED_SUBRESOURCE mapped{};
  if (FAILED(context_->Map(projectileVertexBuffer_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return;
  std::memcpy(mapped.pData, vertices.data(), vertices.size() * sizeof(WorldVertex));
  context_->Unmap(projectileVertexBuffer_.Get(), 0);
  projectileVertexCount_ = static_cast<UINT>(vertices.size());
}

void Renderer::setCpuParticleTimeline(const std::vector<ProjectileTimelineEvent>& events, std::int32_t tick) {
  if (!device_ || !worldBoundsValid_) return;
  constexpr std::int32_t lifetimeTicks = 8;
  std::vector<WorldVertex> particles;
  particles.reserve(4096);
  const auto normalize = [this](float x, float y, float z) {
    return DirectX::XMFLOAT3((x - worldCenterX_) * worldHorizontalScale_,
      (y - worldCenterY_) * worldHorizontalScale_,
      (z - worldMinZ_) / worldSpanZ_ * 0.8f + 0.1f);
  };
  for (const auto& event : events) {
    if (event.tick > tick || event.tick < tick - lifetimeTicks || !event.hasOrigin) continue;
    const float age = static_cast<float>(tick - event.tick) / static_cast<float>(lifetimeTicks);
    if (event.className == "CTETFExplosion" || event.className == "CTEExplosion") {
      const float radius = event.hasRadius ? std::clamp(static_cast<float>(event.radius), 8.0f, 256.0f) : 16.0f;
      const auto a = normalize(event.origin[0], event.origin[1], event.origin[2]);
      const auto b = normalize(event.origin[0] + radius * (1.0f - age), event.origin[1], event.origin[2]);
      constexpr float c[4] = {1.0f, 0.75f, 0.1f, 1.0f};
      particles.push_back({a.x,a.y,a.z,c[0],c[1],c[2],c[3],0,0,0,1,0});
      particles.push_back({b.x,b.y,b.z,c[0],c[1],c[2],c[3],0,0,0,1,0});
    } else if (event.className == "CTEFireBullets" && event.hasDirection) {
      DirectX::XMFLOAT3 d(event.direction[0], event.direction[1], event.direction[2]);
      const float length = std::sqrt(d.x*d.x + d.y*d.y + d.z*d.z);
      if (!std::isfinite(length) || length < 1e-4f) continue;
      d.x /= length; d.y /= length; d.z /= length;
      const auto a = normalize(event.origin[0] + d.x * age * 24.0f, event.origin[1] + d.y * age * 24.0f, event.origin[2] + d.z * age * 24.0f);
      const auto b = normalize(event.origin[0] + d.x * (age * 24.0f + 10.0f), event.origin[1] + d.y * (age * 24.0f + 10.0f), event.origin[2] + d.z * (age * 24.0f + 10.0f));
      constexpr float c[4] = {1.0f, 0.3f, 0.05f, 1.0f};
      particles.push_back({a.x,a.y,a.z,c[0],c[1],c[2],c[3],0,0,0,1,0});
      particles.push_back({b.x,b.y,b.z,c[0],c[1],c[2],c[3],0,0,0,1,0});
    }
    if (particles.size() >= 4096) break;
  }
  if (particles.empty()) return;
  if (!projectileVertexBuffer_ || projectileVertexCapacity_ < particles.size()) {
    D3D11_BUFFER_DESC description{};
    description.ByteWidth = static_cast<UINT>(particles.size() * sizeof(WorldVertex));
    description.Usage = D3D11_USAGE_DYNAMIC;
    description.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    description.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(device_->CreateBuffer(&description, nullptr, projectileVertexBuffer_.ReleaseAndGetAddressOf()))) return;
    projectileVertexCapacity_ = particles.size();
  }
  D3D11_MAPPED_SUBRESOURCE mapped{};
  if (FAILED(context_->Map(projectileVertexBuffer_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return;
  std::memcpy(mapped.pData, particles.data(), particles.size() * sizeof(WorldVertex));
  context_->Unmap(projectileVertexBuffer_.Get(), 0);
  projectileVertexCount_ = static_cast<UINT>(particles.size());
}

void Renderer::setEntityMarkers(const std::vector<EntityMarker>& markers) {
  entityMarkerVertexCount_ = 0;
  if (!device_ || !worldBoundsValid_ || markers.empty()) return;
  constexpr std::size_t maxMarkers = 256;
  std::vector<WorldVertex> vertices;
  vertices.reserve(std::min(markers.size(), maxMarkers) * 6u);
  const auto normalize = [this](float x, float y, float z) {
    return DirectX::XMFLOAT3((x - worldCenterX_) * worldHorizontalScale_,
      (y - worldCenterY_) * worldHorizontalScale_,
      (z - worldMinZ_) / worldSpanZ_ * 0.8f + 0.1f);
  };
  for (std::size_t i = 0; i < markers.size() && i < maxMarkers; ++i) {
    const auto& marker = markers[i];
    if (!std::isfinite(marker.position[0]) || !std::isfinite(marker.position[1]) || !std::isfinite(marker.position[2])) continue;
    const auto center = normalize(marker.position[0], marker.position[1], marker.position[2]);
    constexpr float extent = 12.0f;
    const auto x = normalize(marker.position[0] + extent, marker.position[1], marker.position[2]);
    const auto y = normalize(marker.position[0], marker.position[1] + extent, marker.position[2]);
    const auto z = normalize(marker.position[0], marker.position[1], marker.position[2] + extent);
    const auto vertex = [&](const DirectX::XMFLOAT3& p) {
      return WorldVertex{p.x, p.y, p.z, marker.color[0], marker.color[1], marker.color[2], marker.color[3], 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};
    };
    vertices.push_back(vertex(center)); vertices.push_back(vertex(x));
    vertices.push_back(vertex(center)); vertices.push_back(vertex(y));
    vertices.push_back(vertex(center)); vertices.push_back(vertex(z));
  }
  if (vertices.empty()) return;
  if (!entityMarkerVertexBuffer_ || entityMarkerVertexCapacity_ < vertices.size()) {
    D3D11_BUFFER_DESC description{};
    description.ByteWidth = static_cast<UINT>(vertices.size() * sizeof(WorldVertex));
    description.Usage = D3D11_USAGE_DYNAMIC;
    description.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    description.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(device_->CreateBuffer(&description, nullptr, entityMarkerVertexBuffer_.ReleaseAndGetAddressOf()))) return;
    entityMarkerVertexCapacity_ = vertices.size();
  }
  D3D11_MAPPED_SUBRESOURCE mapped{};
  if (FAILED(context_->Map(entityMarkerVertexBuffer_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return;
  std::memcpy(mapped.pData, vertices.data(), vertices.size() * sizeof(WorldVertex));
  context_->Unmap(entityMarkerVertexBuffer_.Get(), 0);
  entityMarkerVertexCount_ = static_cast<UINT>(vertices.size());
}

bool Renderer::uploadEntityModelMesh(const std::string& cacheKey, const std::vector<ModelDrawVertex>& vertices) {
  if (!device_ || cacheKey.empty() || vertices.size() < 3) { lastError_ = E_INVALIDARG; return false; }
  if (entityModelMeshes_.size() >= 64 && entityModelMeshes_.find(cacheKey) == entityModelMeshes_.end()) {
    lastError_ = E_OUTOFMEMORY;
    return false;
  }
  const auto limit = std::min<std::size_t>(vertices.size(), 12000u);
  const auto count = limit - (limit % 3);
  if (count < 3) { lastError_ = E_INVALIDARG; return false; }
  std::vector<EntityModelGpuVertex> local(count);
  for (std::size_t i = 0; i < count; ++i) {
    const auto& source = vertices[i];
    local[i] = EntityModelGpuVertex{
      source.position[0], source.position[1], source.position[2],
      source.normal[0], source.normal[1], source.normal[2],
      source.texcoord[0], source.texcoord[1],
      source.weights[0], source.weights[1], source.weights[2],
      source.boneIndices[0], source.boneIndices[1], source.boneIndices[2], source.boneCount};
  }
  D3D11_BUFFER_DESC description{};
  description.ByteWidth = static_cast<UINT>(count * sizeof(EntityModelGpuVertex));
  description.Usage = D3D11_USAGE_IMMUTABLE;
  description.BindFlags = D3D11_BIND_VERTEX_BUFFER;
  D3D11_SUBRESOURCE_DATA data{};
  data.pSysMem = local.data();
  Microsoft::WRL::ComPtr<ID3D11Buffer> buffer;
  if (FAILED(device_->CreateBuffer(&description, &data, buffer.GetAddressOf()))) {
    lastError_ = E_FAIL;
    return false;
  }
  EntityModelMesh mesh;
  mesh.vertexBuffer = std::move(buffer);
  mesh.vertexCount = static_cast<UINT>(count);
  entityModelMeshes_[cacheKey] = std::move(mesh);
  entityModelDrawRanges_.clear();
  entityModelVertexCount_ = 0;
  entityModelInstanceCount_ = 0;
  entityModelTexturedRangeCount_ = 0;
  entityModelTexturedInstanceCount_ = 0;
  return true;
}

bool Renderer::uploadEntityModelTexture(const std::string& cacheKey, const std::vector<std::uint8_t>& rgba, UINT width, UINT height) {
  if (!device_ || cacheKey.empty() || rgba.empty() || width == 0 || height == 0) { lastError_ = E_INVALIDARG; return false; }
  const auto mesh = entityModelMeshes_.find(cacheKey);
  if (mesh == entityModelMeshes_.end()) { lastError_ = E_INVALIDARG; return false; }
  std::string error;
  if (!mesh->second.texture.create(device_.Get(), rgba, width, height, &error)) {
    if (!error.empty()) OutputDebugStringA(("Entity model texture upload failed: " + error + "\n").c_str());
    lastError_ = E_INVALIDARG;
    return false;
  }
  return true;
}

void Renderer::setEntityModelInstances(const std::vector<EntityModelDrawInstance>& instances) {
  entityModelVertexCount_ = 0;
  entityModelInstanceCount_ = 0;
  entityModelSkinnedInstanceCount_ = 0;
  entityModelTexturedRangeCount_ = 0;
  entityModelTexturedInstanceCount_ = 0;
  entityModelDrawRanges_.clear();
  if (!device_ || !context_ || !worldBoundsValid_ || instances.empty() || entityModelMeshes_.empty()) return;
  if (!std::isfinite(worldCenterX_) || !std::isfinite(worldCenterY_) || !std::isfinite(worldMinZ_)
      || !std::isfinite(worldSpanZ_) || !std::isfinite(worldHorizontalScale_)
      || worldSpanZ_ <= 0.0f || worldHorizontalScale_ <= 0.0f) return;
  EntityModelWorldMap world;
  world.centerX = worldCenterX_;
  world.centerY = worldCenterY_;
  world.minZ = worldMinZ_;
  world.horizontalScale = worldHorizontalScale_;
  world.zScale = 0.8f / worldSpanZ_;
  constexpr std::size_t maxInstances = 96;
  struct Bucket {
    ID3D11Buffer* vertexBuffer = nullptr;
    UINT vertexCount = 0;
    ID3D11ShaderResourceView* textureView = nullptr;
    std::vector<EntityModelInstanceGpu> items;
  };
  std::vector<Bucket> buckets;
  std::unordered_map<std::string, std::size_t> bucketOf;
  buckets.reserve(8);
  bucketOf.reserve(8);
  std::size_t accepted = 0;
  for (std::size_t i = 0; i < instances.size() && accepted < maxInstances; ++i) {
    const auto& instance = instances[i];
    if (instance.cacheKey.empty()) continue;
    const auto mesh = entityModelMeshes_.find(instance.cacheKey);
    if (mesh == entityModelMeshes_.end() || !mesh->second.vertexBuffer
        || mesh->second.vertexCount < 3 || (mesh->second.vertexCount % 3) != 0) continue;
    EntityModelInstanceRows rows;
    if (!buildEntityModelInstanceRows(instance.origin, instance.angles, instance.hasAngles, world, rows)) continue;
    const auto inserted = bucketOf.emplace(instance.cacheKey, buckets.size());
    if (inserted.second) {
      Bucket created;
      created.vertexBuffer = mesh->second.vertexBuffer.Get();
      created.vertexCount = mesh->second.vertexCount;
      created.textureView = mesh->second.texture.view();
      buckets.push_back(std::move(created));
    }
    EntityModelInstanceGpu gpu{};
    for (int row = 0; row < 3; ++row) {
      for (int column = 0; column < 4; ++column) gpu.position[row][column] = rows.positionRows[row * 4 + column];
      for (int column = 0; column < 3; ++column) gpu.normal[row][column] = rows.normalRows[row * 3 + column];
    }
    gpu.color[0] = instance.color[0];
    gpu.color[1] = instance.color[1];
    gpu.color[2] = instance.color[2];
    gpu.color[3] = instance.color[3];
    // Skinning is only advertised when the caller actually supplied matrices for
    // this instance. An instance whose boneOffset would run past the uploaded
    // buffer is drawn unskinned rather than reading another entity's bones --
    // a wrong pose is worse than a static one, and it is silent.
    const bool skinned = instance.skinningEnabled
      && instance.boneCount > 0
      && static_cast<std::size_t>(instance.boneOffset) + instance.boneCount <= entityBoneMatrixCount_;
    if (skinned) {
      gpu.boneOffset = instance.boneOffset;
      gpu.skinEnabled = 1u;
      gpu.padding[0] = instance.boneCount;
      ++entityModelSkinnedInstanceCount_;
    }
    buckets[inserted.first->second].items.push_back(gpu);
    ++accepted;
  }
  if (accepted == 0 || buckets.empty()) return;
  std::vector<EntityModelInstanceGpu> packed;
  packed.reserve(accepted);
  entityModelDrawRanges_.reserve(buckets.size());
  UINT uniqueVertices = 0;
  for (const auto& bucket : buckets) {
    if (bucket.items.empty() || bucket.vertexBuffer == nullptr || bucket.vertexCount < 3) continue;
    EntityModelDrawRange range;
    range.vertexBuffer = bucket.vertexBuffer;
    range.vertexCount = bucket.vertexCount;
    range.instanceStart = static_cast<UINT>(packed.size());
    range.instanceCount = static_cast<UINT>(bucket.items.size());
    range.textureView = bucket.textureView;
    packed.insert(packed.end(), bucket.items.begin(), bucket.items.end());
    entityModelDrawRanges_.push_back(range);
    uniqueVertices += bucket.vertexCount;
    if (range.textureView) {
      ++entityModelTexturedRangeCount_;
      entityModelTexturedInstanceCount_ += range.instanceCount;
    }
  }
  if (packed.empty()) {
    entityModelDrawRanges_.clear();
    entityModelTexturedRangeCount_ = 0;
    entityModelTexturedInstanceCount_ = 0;
    return;
  }
  if (!entityModelInstanceBuffer_ || entityModelInstanceCapacity_ < packed.size()) {
    D3D11_BUFFER_DESC description{};
    const auto capacity = std::max(packed.size(), std::size_t{96});
    description.ByteWidth = static_cast<UINT>(capacity * sizeof(EntityModelInstanceGpu));
    description.Usage = D3D11_USAGE_DYNAMIC;
    description.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    description.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(device_->CreateBuffer(&description, nullptr, entityModelInstanceBuffer_.ReleaseAndGetAddressOf()))) {
      entityModelDrawRanges_.clear();
      entityModelTexturedRangeCount_ = 0;
      entityModelTexturedInstanceCount_ = 0;
      return;
    }
    entityModelInstanceCapacity_ = capacity;
  }
  D3D11_MAPPED_SUBRESOURCE mapped{};
  if (FAILED(context_->Map(entityModelInstanceBuffer_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
    entityModelDrawRanges_.clear();
    entityModelTexturedRangeCount_ = 0;
    entityModelTexturedInstanceCount_ = 0;
    return;
  }
  std::memcpy(mapped.pData, packed.data(), packed.size() * sizeof(EntityModelInstanceGpu));
  context_->Unmap(entityModelInstanceBuffer_.Get(), 0);
  entityModelVertexCount_ = uniqueVertices;
  entityModelInstanceCount_ = packed.size();
}

void Renderer::setEntityBoneMatrices(const std::vector<std::array<float, 16>>& boneMatrices) {
  entityBoneMatrixCount_ = 0;
  if (boneMatrices.empty()) return;
  if (!device_ || !context_) return;
  // Every matrix has to be finite before it reaches the GPU: one NaN in the
  // buffer turns a whole instance into a black smear and gives no clue which
  // entity it came from. Rejecting the upload is the loud failure.
  for (const auto& matrix : boneMatrices) {
    for (const float element : matrix) {
      if (!std::isfinite(element)) { lastError_ = E_INVALIDARG; return; }
    }
  }
  if (!entityBoneBuffer_ || entityBoneCapacity_ < boneMatrices.size()) {
    entityBoneView_.Reset();
    entityBoneBuffer_.Reset();
    D3D11_BUFFER_DESC description{};
    const auto capacity = std::max(boneMatrices.size(), std::size_t{256});
    description.ByteWidth = static_cast<UINT>(capacity * sizeof(std::array<float, 16>));
    description.Usage = D3D11_USAGE_DYNAMIC;
    description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    description.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    description.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    description.StructureByteStride = sizeof(std::array<float, 16>);
    if (FAILED(device_->CreateBuffer(&description, nullptr, entityBoneBuffer_.ReleaseAndGetAddressOf()))) return;
    D3D11_SHADER_RESOURCE_VIEW_DESC view{};
    view.Format = DXGI_FORMAT_UNKNOWN;
    view.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    view.Buffer.FirstElement = 0;
    view.Buffer.NumElements = static_cast<UINT>(capacity);
    if (FAILED(device_->CreateShaderResourceView(entityBoneBuffer_.Get(), &view,
        entityBoneView_.ReleaseAndGetAddressOf()))) {
      entityBoneBuffer_.Reset();
      return;
    }
    entityBoneCapacity_ = capacity;
  }
  D3D11_MAPPED_SUBRESOURCE mapped{};
  if (FAILED(context_->Map(entityBoneBuffer_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return;
  std::memcpy(mapped.pData, boneMatrices.data(), boneMatrices.size() * sizeof(std::array<float, 16>));
  context_->Unmap(entityBoneBuffer_.Get(), 0);
  entityBoneMatrixCount_ = boneMatrices.size();
}

namespace {
// Row-major 3x4 in the Source `matrix3x4_t` layout, applied the column-vector
// way: `v' = M * v`, with the translation in column 3. `multiply34(a, b)` is the
// ordinary product a*b, which under this convention applies b first. These are
// the *ViewModel pass's* matrices; the world `mvp` and the shader's `mul(local,
// mvp)` use the row-vector form, which is why the assembled matrix is transposed
// where it is handed over.
std::array<float, 12> multiply34(const std::array<float, 12>& a, const std::array<float, 12>& b) {
  std::array<float, 12> out{};
  for (int row = 0; row < 3; ++row) {
    for (int column = 0; column < 3; ++column) {
      out[row * 4 + column] = a[row * 4 + 0] * b[0 * 4 + column]
        + a[row * 4 + 1] * b[1 * 4 + column]
        + a[row * 4 + 2] * b[2 * 4 + column];
    }
    out[row * 4 + 3] = a[row * 4 + 0] * b[3] + a[row * 4 + 1] * b[7]
      + a[row * 4 + 2] * b[11] + a[row * 4 + 3];
  }
  return out;
}

std::array<float, 12> identity34() {
  std::array<float, 12> matrix{};
  matrix[0] = 1.0f;
  matrix[5] = 1.0f;
  matrix[10] = 1.0f;
  return matrix;
}

std::array<float, 12> translation34(float x, float y, float z) {
  std::array<float, 12> matrix = identity34();
  matrix[3] = x;
  matrix[7] = y;
  matrix[11] = z;
  return matrix;
}
} // namespace

// The shared body of both ViewModel mesh uploads. A ViewModel is one model per
// slot, so each slot gets its own immutable buffer rather than a keyed mesh map:
// there is no second model of the same kind to key against, and the map the
// entity path uses also resets the entity draw ranges, which this must not do.
//
// The measured bounds travel with the buffer because the pass derives its
// placement distance from them: a ViewModel is authored in Source units (tens
// of them across) and a fixed small offset would put it behind the near plane.
static bool uploadViewModelMeshInto(ID3D11Device* device,
    const std::vector<ModelDrawVertex>& vertices,
    Microsoft::WRL::ComPtr<ID3D11Buffer>& buffer, UINT& vertexCount,
    float center[3], float& radius) {
  vertexCount = 0;
  if (!device || vertices.size() < 3) return false;
  const auto limit = std::min<std::size_t>(vertices.size(), 60000u);
  const auto count = limit - (limit % 3);
  if (count < 3) return false;
  std::vector<EntityModelGpuVertex> local(count);
  // Measure the model while copying it. The pass uses these bounds to decide how
  // far in front of the eye to place it, so they are taken from the same vertices
  // that are uploaded rather than from a second read of the model.
  float minX = vertices[0].position[0], minY = vertices[0].position[1], minZ = vertices[0].position[2];
  float maxX = minX, maxY = minY, maxZ = minZ;
  for (std::size_t i = 0; i < count; ++i) {
    const auto& source = vertices[i];
    local[i] = EntityModelGpuVertex{
      source.position[0], source.position[1], source.position[2],
      source.normal[0], source.normal[1], source.normal[2],
      source.texcoord[0], source.texcoord[1],
      source.weights[0], source.weights[1], source.weights[2],
      source.boneIndices[0], source.boneIndices[1], source.boneIndices[2], source.boneCount};
    minX = std::min(minX, source.position[0]); maxX = std::max(maxX, source.position[0]);
    minY = std::min(minY, source.position[1]); maxY = std::max(maxY, source.position[1]);
    minZ = std::min(minZ, source.position[2]); maxZ = std::max(maxZ, source.position[2]);
  }
  center[0] = 0.5f * (minX + maxX);
  center[1] = 0.5f * (minY + maxY);
  center[2] = 0.5f * (minZ + maxZ);
  const float halfX = 0.5f * (maxX - minX);
  const float halfY = 0.5f * (maxY - minY);
  const float halfZ = 0.5f * (maxZ - minZ);
  radius = std::sqrt(halfX * halfX + halfY * halfY + halfZ * halfZ);
  D3D11_BUFFER_DESC description{};
  description.ByteWidth = static_cast<UINT>(count * sizeof(EntityModelGpuVertex));
  description.Usage = D3D11_USAGE_IMMUTABLE;
  description.BindFlags = D3D11_BIND_VERTEX_BUFFER;
  D3D11_SUBRESOURCE_DATA data{};
  data.pSysMem = local.data();
  Microsoft::WRL::ComPtr<ID3D11Buffer> created;
  if (FAILED(device->CreateBuffer(&description, &data, created.GetAddressOf()))) {
    return false;
  }
  buffer = std::move(created);
  vertexCount = static_cast<UINT>(count);
  return true;
}

bool Renderer::uploadViewModelMesh(const std::vector<ModelDrawVertex>& vertices,
    const std::vector<ViewModelDrawRange>& ranges) {
  if (!uploadViewModelMeshInto(device_.Get(), vertices, viewModelVertexBuffer_,
      viewModelVertexCount_, viewModelMeshCenter_, viewModelMeshRadius_)) {
    viewModelVertexCount_ = 0;
    lastError_ = E_INVALIDARG;
    return false;
  }
  viewModelDrawRanges_.clear();
  for (const auto& range : ranges) {
    if (range.firstVertex >= viewModelVertexCount_) continue;
    ViewModelDrawRange clamped = range;
    const auto available = static_cast<std::uint32_t>(viewModelVertexCount_) - range.firstVertex;
    clamped.vertexCount = std::min(range.vertexCount, available);
    clamped.vertexCount -= clamped.vertexCount % 3u;
    if (clamped.vertexCount != 0) viewModelDrawRanges_.push_back(std::move(clamped));
  }
  return true;
}

bool Renderer::uploadViewModelWeaponMesh(const std::vector<ModelDrawVertex>& vertices) {
  if (!uploadViewModelMeshInto(device_.Get(), vertices, viewModelWeaponVertexBuffer_,
      viewModelWeaponVertexCount_, viewModelWeaponMeshCenter_, viewModelWeaponMeshRadius_)) {
    viewModelWeaponVertexCount_ = 0;
    lastError_ = E_INVALIDARG;
    return false;
  }
  return true;
}

bool Renderer::uploadViewModelMaterialTexture(const std::string& key,
    const std::vector<std::uint8_t>& rgba, UINT width, UINT height) {
  if (!device_) { lastError_ = E_POINTER; return false; }
  if (key.empty() || rgba.empty() || width == 0 || height == 0) { lastError_ = E_INVALIDARG; return false; }
  // The whole point of the key. A second call for the same normalized path is a
  // reuse: the SRV that is already there is returned rather than rebuilt, and
  // `false` says so. Rebuilding here is the defect `texturesReused` exists to
  // detect, so this is the one place that must not "just work anyway".
  if (viewModelMaterialTextures_.find(key) != viewModelMaterialTextures_.end()) return false;
  const std::size_t expected = static_cast<std::size_t>(width) * height * 4u;
  if (rgba.size() < expected) { lastError_ = E_INVALIDARG; return false; }
  D3D11_TEXTURE2D_DESC description{};
  description.Width = width;
  description.Height = height;
  description.MipLevels = 1;
  description.ArraySize = 1;
  description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  description.SampleDesc.Count = 1;
  description.Usage = D3D11_USAGE_IMMUTABLE;
  description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
  D3D11_SUBRESOURCE_DATA data{};
  data.pSysMem = rgba.data();
  data.SysMemPitch = width * 4u;
  Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
  if (FAILED(device_->CreateTexture2D(&description, &data, texture.GetAddressOf()))) {
    lastError_ = E_FAIL;
    return false;
  }
  Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view;
  if (FAILED(device_->CreateShaderResourceView(texture.Get(), nullptr, view.GetAddressOf()))) {
    lastError_ = E_FAIL;
    return false;
  }
  viewModelMaterialTextures_.emplace(key, std::move(view));
  ++viewModelTexturesUploaded_;
  return true;
}

void Renderer::clearViewModelMaterials() {
  viewModelMaterialTextures_.clear();
  // The upload counter is deliberately NOT reset. It is the reading that says how
  // many GPU textures this pass created over the whole run; a reset would let a
  // per-frame recreation read as a small number that keeps starting over.
}

bool Renderer::uploadViewModelBones(const std::vector<std::array<float, 16>>& boneMatrices) {
  viewModelBoneUploadCount_ = 0;
  if (boneMatrices.empty() || !device_ || !context_) return false;
  // Same rule as the entity buffer: one non-finite matrix turns the weapon into
  // a smear and names no bone, so the upload is refused instead.
  for (const auto& matrix : boneMatrices) {
    for (const float element : matrix) {
      if (!std::isfinite(element)) { lastError_ = E_INVALIDARG; return false; }
    }
  }
  if (!viewModelBoneBuffer_ || viewModelBoneCapacity_ < boneMatrices.size()) {
    viewModelBoneView_.Reset();
    viewModelBoneBuffer_.Reset();
    D3D11_BUFFER_DESC description{};
    const auto capacity = std::max(boneMatrices.size(), std::size_t{128});
    description.ByteWidth = static_cast<UINT>(capacity * sizeof(std::array<float, 16>));
    description.Usage = D3D11_USAGE_DYNAMIC;
    description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    description.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    description.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    description.StructureByteStride = sizeof(std::array<float, 16>);
    if (FAILED(device_->CreateBuffer(&description, nullptr, viewModelBoneBuffer_.ReleaseAndGetAddressOf()))) {
      return false;
    }
    D3D11_SHADER_RESOURCE_VIEW_DESC view{};
    view.Format = DXGI_FORMAT_UNKNOWN;
    view.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    view.Buffer.FirstElement = 0;
    view.Buffer.NumElements = static_cast<UINT>(capacity);
    if (FAILED(device_->CreateShaderResourceView(viewModelBoneBuffer_.Get(), &view,
        viewModelBoneView_.ReleaseAndGetAddressOf()))) {
      viewModelBoneBuffer_.Reset();
      return false;
    }
    viewModelBoneCapacity_ = capacity;
  }
  D3D11_MAPPED_SUBRESOURCE mapped{};
  if (FAILED(context_->Map(viewModelBoneBuffer_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return false;
  std::memcpy(mapped.pData, boneMatrices.data(), boneMatrices.size() * sizeof(std::array<float, 16>));
  context_->Unmap(viewModelBoneBuffer_.Get(), 0);
  viewModelBoneUploadCount_ = boneMatrices.size();
  return true;
}

std::string Renderer::viewModelAnimationInput() const {
  // The EFFECT, not the last request and not the last upload. This used to key on
  // viewModelBoneUploadCount_, which was a stable statement while the table was
  // uploaded exactly once at startup; T2.5 re-uploads it on every scene update,
  // and a frame that applies no pose would otherwise report the previous frame's
  // upload -- `animationInput=bones:45` next to `skinning=0`, an effect reading
  // claiming an effect that did not happen. viewModelEffectiveBoneCount_ is reset
  // at the top of every drawViewModel, so it answers "did the pass skin, just
  // now". When it did not, the caller's own contract reading explains why.
  if (viewModelEffectiveBoneCount_ > 0) {
    return "bones:" + std::to_string(viewModelBoneUploadCount_);
  }
  return viewModelState_.animationStatus.empty()
    ? std::string("missing") : viewModelState_.animationStatus;
}

void Renderer::setViewModelState(const ViewModelDrawState& state) {
  viewModelState_ = state;
  viewModelState_.fov = clampViewModelFov(state.fov);
}

bool Renderer::drawViewModel() {
  viewModelMvp_.fill(0.0f);
  // Cleared before every early return, so a refusal this frame cannot leave the
  // previous frame's count behind and read as "it skinned".
  viewModelEffectiveBoneCount_ = 0;
  // The weapon body's readings are reset together with the arms': an early
  // return below is the whole pass being off, and the body must not keep the
  // previous frame's reason (or centre) as if this frame had produced it.
  viewModelWeaponCenterClip_.fill(0.0f);
  viewModelWeaponDrawReason_ = "the pass did not draw";
  if (!context_) { viewModelDrawReason_ = "the renderer has no device context"; return false; }
  if (!viewModelState_.enabled) {
    viewModelDrawReason_ = viewModelState_.reason.empty()
      ? std::string("no viewmodel was requested") : viewModelState_.reason;
    return false;
  }
  if (!viewModelVertexBuffer_ || viewModelVertexCount_ < 3) {
    viewModelDrawReason_ = "no viewmodel mesh is uploaded";
    return false;
  }
  if (!viewModelVertexShader_ || !viewModelPixelShader_ || !viewModelInputLayout_
      || !viewModelConstants_ || !viewModelDepthStencilState_) {
    viewModelDrawReason_ = "the viewmodel pipeline did not build";
    return false;
  }
  // Skinning is all-or-nothing. The plan refuses a partial guess, so when the
  // caller asked for skinning but the bone table cannot cover the skeleton, the
  // pass draws the bind pose and says so instead of skinning with a short table.
  std::uint32_t effectiveBoneCount = 0;
  std::string skinNote;
  if (viewModelState_.skinningEnabled && viewModelState_.boneCount > 0) {
    if (!viewModelBoneView_ || viewModelBoneUploadCount_ < viewModelState_.boneCount) {
      skinNote = "skinning was requested but the bone table covers "
        + std::to_string(viewModelBoneUploadCount_) + " of "
        + std::to_string(viewModelState_.boneCount) + " bones; drawing the bind pose";
    } else {
      effectiveBoneCount = viewModelState_.boneCount;
    }
  }
  viewModelEffectiveBoneCount_ = effectiveBoneCount;

  using namespace DirectX;
  // CONVENTION. Every 3x4 in this pass is the Source `matrix3x4_t` layout --
  // row-major, translation in column 3 -- and every one of them is applied the
  // column-vector way, `v' = M * v`. That is the layout `mstudioattachment_t::
  // local` itself uses, so the attachment matrix needs no reinterpretation.
  //
  // The chain the plan asks for is attachment local -> owning bone model space ->
  // ViewModel model space (A) -> hand (H) -> camera space (T). Column-vector
  // composition applies the rightmost factor first, so the product is T * H * A.
  //
  // The shader consumes `mul(local, mvp)`, which is the row-vector product, so
  // the assembled 3x4 is transposed on its way into the XMMATRIX below. Getting
  // this wrong is not a small error: the first version used the row-vector
  // formula for the centre, multiplied in the row-vector order and then dropped
  // the translation entirely by leaving the XMMATRIX's last row at (0,0,0,1).
  // The mesh centre landed at clip w = -467 -- behind the camera -- while the
  // pass reported `drawn=1`.
  const std::array<float, 12> attachment = viewModelState_.attachmentValid
    ? viewModelState_.attachmentTransform : identity34();
  const std::array<float, 12> hand = viewModelState_.handTransform;
  const std::array<float, 12> attachmentHand = multiply34(hand, attachment);
  // The model's own origin is not its centre, and T is applied last, so the
  // translation is solved for "the mesh centre lands where the weapon should sit"
  // rather than guessed at.
  const float cx = viewModelMeshCenter_[0];
  const float cy = viewModelMeshCenter_[1];
  const float cz = viewModelMeshCenter_[2];
  const float centerX = attachmentHand[0] * cx + attachmentHand[1] * cy + attachmentHand[2] * cz + attachmentHand[3];
  const float centerY = attachmentHand[4] * cx + attachmentHand[5] * cy + attachmentHand[6] * cz + attachmentHand[7];
  const float centerZ = attachmentHand[8] * cx + attachmentHand[9] * cy + attachmentHand[10] * cz + attachmentHand[11];
  // Distance and offsets are in the model's own units, taken from its own radius.
  // A fixed small offset would put a tens-of-units Source model behind the near
  // plane, which is how the first version drew and changed no pixels.
  const float radius = std::max(viewModelMeshRadius_, 1e-3f);
  const float distance = radius * 2.2f;
  const float rightOffset = radius * 0.30f;
  const float downOffset = -radius * 0.22f;
  const float targetX = frameEye_[0] + frameForward_[0] * distance
    + frameRight_[0] * rightOffset + frameUp_[0] * downOffset;
  const float targetY = frameEye_[1] + frameForward_[1] * distance
    + frameRight_[1] * rightOffset + frameUp_[1] * downOffset;
  const float targetZ = frameEye_[2] + frameForward_[2] * distance
    + frameRight_[2] * rightOffset + frameUp_[2] * downOffset;
  viewModelPlacementTarget_ = { targetX, targetY, targetZ };
  const auto placement = translation34(targetX - centerX, targetY - centerY, targetZ - centerZ);
  const auto world = multiply34(placement, attachmentHand);

  // Transposed into the row-vector form the shader multiplies with: the basis
  // becomes the rows and the translation moves to the last row, which is where
  // `mul(float4, float4x4)` reads it.
  const XMMATRIX worldMatrix(
    world[0], world[4], world[8], 0.0f,
    world[1], world[5], world[9], 0.0f,
    world[2], world[6], world[10], 0.0f,
    world[3], world[7], world[11], 1.0f);
  const XMMATRIX view = XMLoadFloat4x4(&frameView_);
  const float aspect = height_ == 0 ? 1.0f : static_cast<float>(width_) / static_cast<float>(height_);

  // Its own projection: the ViewModel FOV, and a near plane derived from the
  // model's own radius so the whole weapon is in front of it.
  const float nearPlane = std::max(radius * 0.05f, 0.01f);
  const float farPlane = distance + radius * 4.0f;
  const XMMATRIX projection = XMMatrixPerspectiveFovLH(
    XMConvertToRadians(viewModelState_.fov), aspect, nearPlane, farPlane);
  const XMMATRIX mvp = worldMatrix * view * projection;

  // Where the *mesh centre* ends up, through the same matrix the shader is
  // handed. `drawn=1` alone cannot distinguish "on screen" from "issued a draw
  // the rasteriser discarded", which is the failure this pass already had once.
  // It has to be the mesh centre, not the placement point: the placement point is
  // already in world space, so running it through `worldMatrix` applies the
  // placement a second time and reports a position the weapon never occupies.
  {
    const XMVECTOR centre = XMVectorSet(cx, cy, cz, 1.0f);
    const XMVECTOR clip = XMVector4Transform(centre, mvp);
    viewModelCenterClip_ = { XMVectorGetX(clip), XMVectorGetY(clip), XMVectorGetZ(clip), XMVectorGetW(clip) };
    const XMVECTOR viewSpace = XMVector4Transform(centre, worldMatrix * view);
    viewModelCenterViewSpace_ = { XMVectorGetX(viewSpace), XMVectorGetY(viewSpace), XMVectorGetZ(viewSpace) };
  }

  ViewModelConstantsData constants{};
  XMStoreFloat4x4(&constants.mvp, mvp);
  constants.tint = XMFLOAT4(viewModelState_.tint[0], viewModelState_.tint[1],
    viewModelState_.tint[2], viewModelState_.tint[3]);
  constants.skinInfo = XMFLOAT4(static_cast<float>(effectiveBoneCount), 0.0f, 0.0f, 0.0f);
  XMStoreFloat4x4(reinterpret_cast<XMFLOAT4X4*>(viewModelMvp_.data()), mvp);

  // The pass's own depth policy. Depth is cleared first, so the weapon is drawn
  // over the map it is standing in rather than being clipped by it -- the world
  // depth state cannot express that, which is why this pass does not use it.
  if (depthStencilView_) {
    context_->ClearDepthStencilView(depthStencilView_.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
  }
  context_->OMSetDepthStencilState(viewModelDepthStencilState_.Get(), 0);
  context_->RSSetState(viewModelRasterizerState_.Get());
  const UINT stride = sizeof(EntityModelGpuVertex), offset = 0;
  auto* buffer = viewModelVertexBuffer_.Get();
  context_->IASetInputLayout(viewModelInputLayout_.Get());
  context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  context_->IASetVertexBuffers(0, 1, &buffer, &stride, &offset);
  context_->VSSetShader(viewModelVertexShader_.Get(), nullptr, 0);
  auto* constantsBuffer = viewModelConstants_.Get();
  context_->VSSetConstantBuffers(0, 1, &constantsBuffer);
  context_->PSSetShader(viewModelPixelShader_.Get(), nullptr, 0);
  context_->PSSetConstantBuffers(0, 1, &constantsBuffer);
  context_->PSSetSamplers(0, 1, &sampler_);
  // t4 is always bound, even when it holds no bones: an unbound structured
  // buffer is read as garbage, not as zeros.
  auto* boneView = viewModelBoneView_.Get();
  context_->VSSetShaderResources(4, 1, &boneView);

  // One draw per material range. A range with no cached texture for its slot is
  // the fallback: `material.x = 0` and the flat colour, which is why the
  // fallback can never be mistaken for a texture that happened to be white.
  // `viewModelDrawRanges_` empty (no range information was supplied) is the
  // whole-buffer draw this pass issued before the material chain existed, and it
  // is kept as that rather than turned into a one-range fallback: the two
  // produce the same picture, and saying so costs a branch.
  viewModelFallbackRanges_ = 0;
  viewModelBoundSlots_ = 0;
  viewModelBlendedRanges_ = 0;
  viewModelAlphaTestedRanges_ = 0;
  ID3D11ShaderResourceView* nullView = nullptr;
  if (viewModelDrawRanges_.empty()) {
    constants.material = XMFLOAT4(0.0f, 0.0f, 0.0f, 0.0f);
    context_->UpdateSubresource(viewModelConstants_.Get(), 0, nullptr, &constants, 0, 0);
    context_->PSSetShaderResources(0, 1, &nullView);
    context_->OMSetBlendState(nullptr, nullptr, 0xffffffffu);
    context_->Draw(viewModelVertexCount_, 0);
    viewModelFallbackRanges_ = 1;
  } else {
    std::vector<std::int32_t> boundSlots;
    for (const auto& range : viewModelDrawRanges_) {
      auto cached = range.materialKey.empty()
        ? viewModelMaterialTextures_.end()
        : viewModelMaterialTextures_.find(range.materialKey);
      const bool textured = cached != viewModelMaterialTextures_.end();
      constants.material = XMFLOAT4(textured ? 1.0f : 0.0f, range.alphaTest ? 1.0f : 0.0f, 0.0f, 0.0f);
      context_->UpdateSubresource(viewModelConstants_.Get(), 0, nullptr, &constants, 0, 0);
      if (textured) {
        auto* view = cached->second.Get();
        context_->PSSetShaderResources(0, 1, &view);
        // Alpha blending is the material's own `$translucent`, not a default.
        context_->OMSetBlendState(range.translucent ? viewModelAlphaBlendState_.Get() : nullptr,
          nullptr, 0xffffffffu);
        if (range.translucent) ++viewModelBlendedRanges_;
        if (range.alphaTest) ++viewModelAlphaTestedRanges_;
        // A bind served out of the cache. It counts only when a texture was
        // actually handed to the pass -- an unbound fallback range is not a
        // reuse of anything.
        ++viewModelTexturesReused_;
        if (std::find(boundSlots.begin(), boundSlots.end(), range.slot) == boundSlots.end()) {
          boundSlots.push_back(range.slot);
        }
      } else {
        context_->PSSetShaderResources(0, 1, &nullView);
        context_->OMSetBlendState(nullptr, nullptr, 0xffffffffu);
        ++viewModelFallbackRanges_;
      }
      context_->Draw(range.vertexCount, range.firstVertex);
    }
    viewModelBoundSlots_ = boundSlots.size();
  }
  context_->PSSetShaderResources(0, 1, &nullView);
  context_->OMSetBlendState(nullptr, nullptr, 0xffffffffu);

  ++viewModelDrawCount_;
  ++viewModelDrawCallCount_;
  ++viewModelArmDrawCallCount_;
  viewModelPassOrdinal_ = ++framePassOrdinal_;
  viewModelDrawReason_ = skinNote.empty() ? std::string("drawn") : skinNote;

  // ---- The weapon body: the second, independent draw request --------------
  // Same pass, same pipeline objects, same depth clear -- a second Draw with
  // its own mesh, its own measured bounds and its own transform state. The
  // depth is NOT cleared again here: the clear above is what keeps the
  // composite in front of the map, and a second clear would erase the arms the
  // first draw just wrote.
  //
  // The body shares the arms' placement point. The two models make up one
  // first-person composite in front of one eye, so the body's own centre is
  // solved onto the same target the arms were solved onto -- a second,
  // independent placement formula would let the two drift apart as their radii
  // differ. Its own radius still drives its own near and far planes, because
  // those are about the geometry actually being drawn.
  //
  // Reaching this point means the arms draw already happened (every early
  // return above is the whole pass being off -- a first-person composite that
  // cannot draw its arms does not draw a floating weapon either).
  if (!viewModelWeaponState_.enabled) {
    viewModelWeaponDrawReason_ = viewModelWeaponState_.reason.empty()
      ? std::string("no weapon body was requested") : viewModelWeaponState_.reason;
  } else if (!viewModelWeaponVertexBuffer_ || viewModelWeaponVertexCount_ < 3) {
    viewModelWeaponDrawReason_ = "no weapon body mesh is uploaded";
  } else {
    const float weaponRadius = std::max(viewModelWeaponMeshRadius_, 1e-3f);
    const std::array<float, 12> weaponAttachment = viewModelWeaponState_.attachmentValid
      ? viewModelWeaponState_.attachmentTransform : identity34();
    const std::array<float, 12> weaponAttachmentHand =
      multiply34(viewModelWeaponState_.handTransform, weaponAttachment);
    const float wcx = viewModelWeaponMeshCenter_[0];
    const float wcy = viewModelWeaponMeshCenter_[1];
    const float wcz = viewModelWeaponMeshCenter_[2];
    const float weaponCenterX = weaponAttachmentHand[0] * wcx + weaponAttachmentHand[1] * wcy
      + weaponAttachmentHand[2] * wcz + weaponAttachmentHand[3];
    const float weaponCenterY = weaponAttachmentHand[4] * wcx + weaponAttachmentHand[5] * wcy
      + weaponAttachmentHand[6] * wcz + weaponAttachmentHand[7];
    const float weaponCenterZ = weaponAttachmentHand[8] * wcx + weaponAttachmentHand[9] * wcy
      + weaponAttachmentHand[10] * wcz + weaponAttachmentHand[11];
    const auto weaponPlacement =
      translation34(targetX - weaponCenterX, targetY - weaponCenterY, targetZ - weaponCenterZ);
    const auto weaponWorld = multiply34(weaponPlacement, weaponAttachmentHand);
    const XMMATRIX weaponWorldMatrix(
      weaponWorld[0], weaponWorld[4], weaponWorld[8], 0.0f,
      weaponWorld[1], weaponWorld[5], weaponWorld[9], 0.0f,
      weaponWorld[2], weaponWorld[6], weaponWorld[10], 0.0f,
      weaponWorld[3], weaponWorld[7], weaponWorld[11], 1.0f);
    const float weaponNear = std::max(weaponRadius * 0.05f, 0.01f);
    const float weaponFar = distance + weaponRadius * 4.0f;
    const XMMATRIX weaponProjection = XMMatrixPerspectiveFovLH(
      XMConvertToRadians(viewModelWeaponState_.fov), aspect, weaponNear, weaponFar);
    const XMMATRIX weaponMvp = weaponWorldMatrix * view * weaponProjection;
    {
      const XMVECTOR centre = XMVectorSet(wcx, wcy, wcz, 1.0f);
      const XMVECTOR clip = XMVector4Transform(centre, weaponMvp);
      viewModelWeaponCenterClip_ = { XMVectorGetX(clip), XMVectorGetY(clip),
        XMVectorGetZ(clip), XMVectorGetW(clip) };
    }
    ViewModelConstantsData weaponConstants{};
    XMStoreFloat4x4(&weaponConstants.mvp, weaponMvp);
    weaponConstants.tint = XMFLOAT4(viewModelWeaponState_.tint[0], viewModelWeaponState_.tint[1],
      viewModelWeaponState_.tint[2], viewModelWeaponState_.tint[3]);
    // No skinning for this draw: a weapon body is a static mesh and the arms'
    // bone table does not describe its skeleton. The shader reads skinInfo from
    // this last update, so it has to be zeroed rather than left at the arms'
    // count -- otherwise the body would be skinned by the arms' matrices.
    weaponConstants.skinInfo = XMFLOAT4(0.0f, 0.0f, 0.0f, 0.0f);
    context_->UpdateSubresource(viewModelConstants_.Get(), 0, nullptr, &weaponConstants, 0, 0);
    auto* weaponBuffer = viewModelWeaponVertexBuffer_.Get();
    context_->IASetVertexBuffers(0, 1, &weaponBuffer, &stride, &offset);
    context_->Draw(viewModelWeaponVertexCount_, 0);
    ++viewModelWeaponDrawCallCount_;
    viewModelWeaponDrawReason_ = "drawn";
  }
  return true;
}

void Renderer::setViewModelWeaponState(const ViewModelDrawState& state) {
  viewModelWeaponState_ = state;
  viewModelWeaponState_.fov = clampViewModelFov(state.fov);
}

bool Renderer::saveCameraPreset(std::size_t slot) {
  if (slot >= cameraPresets_.size() || !std::isfinite(cameraYaw_)
      || !std::isfinite(cameraPitch_) || !std::isfinite(cameraDistance_)) return false;
  const float focusX = observerFocusValid_ ? observerFocusX_ : 0.0f;
  const float focusY = observerFocusValid_ ? observerFocusY_ : 0.0f;
  const float focusZ = observerFocusValid_ ? observerFocusZ_ : 0.5f;
  if (!std::isfinite(focusX) || !std::isfinite(focusY) || !std::isfinite(focusZ)) return false;
  auto& preset = cameraPresets_[slot];
  preset.focusX = std::clamp(focusX, -1.0e6f, 1.0e6f);
  preset.focusY = std::clamp(focusY, -1.0e6f, 1.0e6f);
  preset.focusZ = std::clamp(focusZ, -1.0e6f, 1.0e6f);
  preset.yaw = std::remainder(cameraYaw_, 6.28318530717958647692f);
  preset.pitch = std::clamp(cameraPitch_, 0.25f, 1.45f);
  preset.distance = std::clamp(cameraDistance_, 0.8f, 12.0f);
  preset.valid = true;
  return true;
}

bool Renderer::applyCameraPreset(std::size_t slot) {
  if (slot >= cameraPresets_.size() || !cameraPresets_[slot].valid) return false;
  const auto& preset = cameraPresets_[slot];
  if (!std::isfinite(preset.focusX) || !std::isfinite(preset.focusY)
      || !std::isfinite(preset.focusZ) || !std::isfinite(preset.yaw)
      || !std::isfinite(preset.pitch) || !std::isfinite(preset.distance)) return false;
  setObserverFocus(preset.focusX, preset.focusY, preset.focusZ);
  cameraYaw_ = std::remainder(preset.yaw, 6.28318530717958647692f);
  cameraPitch_ = std::clamp(preset.pitch, 0.25f, 1.45f);
  cameraDistance_ = std::clamp(preset.distance, 0.8f, 12.0f);
  return true;
}

bool Renderer::clearCameraPreset(std::size_t slot) {
  if (slot >= cameraPresets_.size()) return false;
  cameraPresets_[slot] = CameraPreset{};
  return true;
}

void Renderer::clearCameraPresets() {
  cameraPresets_.fill(CameraPreset{});
}

void Renderer::resetCamera() {
  cameraYaw_ = 0.0f;
  cameraPitch_ = 0.36f;
  cameraDistance_ = 1.65f;
  observerDemoView_ = false;
  observerFocusValid_ = false;
  observerFocusX_ = 0.0f;
  observerFocusY_ = 0.0f;
  observerFocusZ_ = 0.25f;
}

void Renderer::resize(UINT width, UINT height) {
  if (!swapChain_ || width == 0 || height == 0 || (width == width_ && height == height_)) return;
  releaseTarget();
  const HRESULT result = swapChain_->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
  if (FAILED(result)) { lastError_ = result; return; }
  if (!createTarget(width, height)) lastError_ = E_FAIL;
  else lastError_ = S_OK;
}

void Renderer::requestFrameCapture(std::wstring path) {
  capturePath_ = std::move(path);
  captureSucceeded_ = false;
  captureError_.clear();
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
  return ok;
}

bool Renderer::draw(float clearRed, float clearGreen, float clearBlue) {
  if (!context_ || !target_ || !swapChain_) return false;
  const float colour[4] = { clearRed, clearGreen, clearBlue, 1.0f };
  context_->OMSetRenderTargets(1, target_.GetAddressOf(), depthStencilView_.Get());
  context_->ClearRenderTargetView(target_.Get(), colour);
  if (depthStencilView_) context_->ClearDepthStencilView(depthStencilView_.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
  context_->OMSetDepthStencilState(depthStencilState_.Get(), 0);
  context_->RSSetState(rasterizerState_.Get());
  D3D11_VIEWPORT viewport{};
  viewport.Width = static_cast<float>(width_);
  viewport.Height = static_cast<float>(height_);
  viewport.MinDepth = 0.0f;
  viewport.MaxDepth = 1.0f;
  context_->RSSetViewports(1, &viewport);
  context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  framePassOrdinal_ = 0;
  viewModelWorldPassOrdinal_ = 0;
  // Defaults for the case where worldConstants_ is missing: a Z-up camera
  // looking down +X, so the ViewModel pass always has a camera to place against.
  frameView_ = {};
  frameView_.m[0][0] = 1.0f; frameView_.m[1][1] = 1.0f; frameView_.m[2][2] = 1.0f; frameView_.m[3][3] = 1.0f;
  frameEye_[0] = 0.0f; frameEye_[1] = 0.0f; frameEye_[2] = 0.0f;
  frameForward_[0] = 1.0f; frameForward_[1] = 0.0f; frameForward_[2] = 0.0f;
  frameUp_[0] = 0.0f; frameUp_[1] = 0.0f; frameUp_[2] = 1.0f;
  frameRight_[0] = 0.0f; frameRight_[1] = 1.0f; frameRight_[2] = 0.0f;
  if (worldConstants_) {
    using namespace DirectX;
    XMVECTOR target = XMVectorSet(
      observerFocusValid_ ? observerFocusX_ : 0.0f,
      observerFocusValid_ ? observerFocusY_ : 0.0f,
      observerFocusValid_ ? observerFocusZ_ : 0.5f, 1.0f);
    XMVECTOR eye;
    XMVECTOR up = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
    if (observerDemoView_) {
      float forward[3] = {};
      demoViewForward(observerDemoPitchDeg_, observerDemoYawDeg_, forward);
      eye = XMVectorSet(observerDemoEyeX_, observerDemoEyeY_, observerDemoEyeZ_, 1.0f);
      target = XMVectorSet(observerDemoEyeX_ + forward[0], observerDemoEyeY_ + forward[1], observerDemoEyeZ_ + forward[2], 1.0f);
      if (std::fabs(forward[2]) > 0.95f) up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    } else {
      const float horizontal = std::cos(cameraPitch_) * cameraDistance_;
      eye = XMVectorSet(
        XMVectorGetX(target) + std::cos(cameraYaw_) * horizontal,
        XMVectorGetY(target) + std::sin(cameraYaw_) * horizontal,
        XMVectorGetZ(target) + std::sin(cameraPitch_) * cameraDistance_, 1.0f);
    }
    const XMMATRIX view = XMMatrixLookAtLH(eye, target, up);
    // Publish the frame camera for the ViewModel pass. The pass places the
    // weapon in this camera's space and reuses this view matrix, so the two
    // passes cannot drift apart.
    XMStoreFloat4x4(&frameView_, view);
    const XMVECTOR forwardVector = XMVector3Normalize(XMVectorSubtract(target, eye));
    // Left-handed basis: right = up x forward.
    const XMVECTOR rightVector = XMVector3Normalize(XMVector3Cross(up, forwardVector));
    frameEye_[0] = XMVectorGetX(eye); frameEye_[1] = XMVectorGetY(eye); frameEye_[2] = XMVectorGetZ(eye);
    frameForward_[0] = XMVectorGetX(forwardVector); frameForward_[1] = XMVectorGetY(forwardVector); frameForward_[2] = XMVectorGetZ(forwardVector);
    frameUp_[0] = XMVectorGetX(up); frameUp_[1] = XMVectorGetY(up); frameUp_[2] = XMVectorGetZ(up);
    frameRight_[0] = XMVectorGetX(rightVector); frameRight_[1] = XMVectorGetY(rightVector); frameRight_[2] = XMVectorGetZ(rightVector);
    const float aspect = height_ == 0 ? 1.0f : static_cast<float>(width_) / static_cast<float>(height_);
    const XMMATRIX projection = XMMatrixPerspectiveFovLH(XMConvertToRadians(settings_.viewModelFov), aspect, 0.05f, 100.0f);
    const XMMATRIX mvp = view * projection;
    struct WorldConstantsData {
      XMFLOAT4X4 mvp;
      XMFLOAT4 lightDirectionAndMode;
      XMFLOAT4 cameraPositionAndSpecular;
      XMFLOAT4 materialFeatures;
      XMFLOAT4 lightmapFeatures;
      XMFLOAT4 envFeatures;
    } constants{};
    XMStoreFloat4x4(&constants.mvp, mvp);
    const bool standardLighting = settings_.preset == QualityPreset::Standard && settings_.dynamicLighting;
    const bool standardSpecular = settings_.preset == QualityPreset::Standard && settings_.specular;
    constants.lightDirectionAndMode = XMFLOAT4(0.35f, -0.45f, 0.82f, standardLighting ? 1.0f : 0.0f);
    constants.cameraPositionAndSpecular = XMFLOAT4(
      XMVectorGetX(eye), XMVectorGetY(eye), XMVectorGetZ(eye), standardSpecular ? 1.0f : 0.0f);
    const bool standardSelfIllum = settings_.preset == QualityPreset::Standard && worldMaterialParams_.selfIllum;
    constants.materialFeatures = XMFLOAT4(
      worldMaterialParams_.selfIllumR,
      worldMaterialParams_.selfIllumG,
      worldMaterialParams_.selfIllumB,
      standardSelfIllum ? 1.0f : 0.0f);
    constants.lightmapFeatures = XMFLOAT4(
      worldLightmapStatus_ == WorldLightmapStatus::Unavailable ? 1.0f : worldLightmapIntensity_,
      settings_.preset == QualityPreset::Standard && worldMaterialParams_.bumpMapping && worldBumpTexture_.view() ? 1.0f : 0.0f,
      settings_.preset == QualityPreset::Standard && worldMaterialParams_.envMap && worldEnvTexture_.view() ? 1.0f : 0.0f,
      worldLightmapTexture_.view() ? 1.0f : 0.0f);
    // The cube wins when it is there: it is the map's own environment rather
    // than a 2D projection of whatever $envmap named, and the capability layer
    // only builds it when all six faces decoded.
    constants.envFeatures = XMFLOAT4(static_cast<float>(worldEnvMode()), 0.0f, 0.0f, 0.0f);
    context_->UpdateSubresource(worldConstants_.Get(), 0, nullptr, &constants, 0, 0);
  }
  auto* lightView = worldLightmapTexture_.view();
  context_->PSSetShaderResources(3, 1, &lightView);
  const bool hasWorldGeometry = worldVertexBuffer_ && worldVertexCount_ > 0;
  if (worldVertexShader_ && worldPixelShader_ && worldInputLayout_ && hasWorldGeometry) {
    const UINT stride = sizeof(WorldVertex), offset = 0;
    auto* buffer = worldVertexBuffer_.Get();
    context_->IASetInputLayout(worldInputLayout_.Get());
    context_->IASetVertexBuffers(0, 1, &buffer, &stride, &offset);
    context_->VSSetShader(worldVertexShader_.Get(), nullptr, 0);
    auto* worldConstants = worldConstants_.Get();
    context_->VSSetConstantBuffers(0, 1, &worldConstants);
    context_->PSSetShader(worldPixelShader_.Get(), nullptr, 0);
    context_->PSSetConstantBuffers(0, 1, &worldConstants);
    auto* worldView = worldTexture_.view() ? worldTexture_.view() : texture_.view();
    context_->PSSetShaderResources(0, 1, &worldView);
    ID3D11ShaderResourceView* bumpView = worldBumpTexture_.view();
    ID3D11ShaderResourceView* envView = worldEnvTexture_.view();
    ID3D11ShaderResourceView* cubeView = worldCubemapView_.Get();
    context_->PSSetShaderResources(1, 1, &bumpView);
    context_->PSSetShaderResources(2, 1, &envView);
    context_->PSSetShaderResources(5, 1, &cubeView);
    context_->PSSetSamplers(0, 1, sampler_.GetAddressOf());
    context_->Draw(worldVertexCount_, 0);
  } else {
    context_->IASetInputLayout(nullptr);
    context_->VSSetShader(vertexShader_.Get(), nullptr, 0);
    context_->PSSetShader(pixelShader_.Get(), nullptr, 0);
    auto* view = texture_.view();
    context_->PSSetShaderResources(0, 1, &view);
    context_->PSSetSamplers(0, 1, sampler_.GetAddressOf());
    context_->Draw(6, 0);
  }
  // The world pass is ordinal 1. The ViewModel pass takes the next ordinal after
  // the passes below, so "the weapon draws after the world" is a number the
  // runtime can print rather than a claim about the order of these statements.
  viewModelWorldPassOrdinal_ = ++framePassOrdinal_;
  if (entityMarkerVertexBuffer_ && entityMarkerVertexCount_ > 0
      && worldVertexShader_ && worldPixelShader_ && worldInputLayout_ && worldConstants_) {
    const UINT markerStride = sizeof(WorldVertex), markerOffset = 0;
    auto* markerBuffer = entityMarkerVertexBuffer_.Get();
    context_->IASetInputLayout(worldInputLayout_.Get());
    context_->IASetVertexBuffers(0, 1, &markerBuffer, &markerStride, &markerOffset);
    context_->VSSetShader(worldVertexShader_.Get(), nullptr, 0);
    auto* worldConstants = worldConstants_.Get();
    context_->VSSetConstantBuffers(0, 1, &worldConstants);
    context_->PSSetShader(worldPixelShader_.Get(), nullptr, 0);
    context_->PSSetConstantBuffers(0, 1, &worldConstants);
    auto* worldView = worldTexture_.view() ? worldTexture_.view() : texture_.view();
    context_->PSSetShaderResources(0, 1, &worldView);
    ID3D11ShaderResourceView* bumpView = worldBumpTexture_.view();
    ID3D11ShaderResourceView* envView = worldEnvTexture_.view();
    context_->PSSetShaderResources(1, 1, &bumpView);
    context_->PSSetShaderResources(2, 1, &envView);
    context_->PSSetSamplers(0, 1, sampler_.GetAddressOf());
    context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
    context_->Draw(entityMarkerVertexCount_, 0);
    context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  }
  ++framePassOrdinal_;
  if (!entityModelDrawRanges_.empty() && entityModelInstanceBuffer_ && entityModelInstanceCount_ > 0
      && entityModelVertexShader_ && entityModelInputLayout_ && worldPixelShader_ && worldConstants_) {
    const UINT strides[2] = { sizeof(EntityModelGpuVertex), sizeof(EntityModelInstanceGpu) };
    const UINT offsets[2] = { 0, 0 };
    context_->IASetInputLayout(entityModelInputLayout_.Get());
    context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context_->VSSetShader(entityModelVertexShader_.Get(), nullptr, 0);
    auto* worldConstants = worldConstants_.Get();
    context_->VSSetConstantBuffers(0, 1, &worldConstants);
    context_->PSSetShader(worldPixelShader_.Get(), nullptr, 0);
    context_->PSSetConstantBuffers(0, 1, &worldConstants);
    auto* worldView = worldTexture_.view() ? worldTexture_.view() : texture_.view();
    context_->PSSetShaderResources(0, 1, &worldView);
    ID3D11ShaderResourceView* bumpView = worldBumpTexture_.view();
    ID3D11ShaderResourceView* envView = worldEnvTexture_.view();
    context_->PSSetShaderResources(1, 1, &bumpView);
    context_->PSSetShaderResources(2, 1, &envView);
    context_->PSSetSamplers(0, 1, sampler_.GetAddressOf());
    // t4 is the per-instance bone buffer. An unbound SRV would make a skinned
    // draw read garbage, so the slot is always set here, even when empty.
    auto* boneView = entityBoneView_.Get();
    context_->VSSetShaderResources(4, 1, &boneView);
    auto* instanceBuffer = entityModelInstanceBuffer_.Get();
    for (const auto& range : entityModelDrawRanges_) {
      if (!range.vertexBuffer || range.vertexCount < 3 || range.instanceCount == 0) continue;
      auto* rangeView = range.textureView ? range.textureView : worldView;
      context_->PSSetShaderResources(0, 1, &rangeView);
      ID3D11Buffer* buffers[2] = { range.vertexBuffer, instanceBuffer };
      context_->IASetVertexBuffers(0, 2, buffers, strides, offsets);
      context_->DrawInstanced(range.vertexCount, range.instanceCount, 0, range.instanceStart);
    }
  }
  ++framePassOrdinal_;
  if (projectileVertexBuffer_ && projectileVertexCount_ > 0 && worldVertexShader_ && worldPixelShader_
      && worldInputLayout_ && worldConstants_) {
    const UINT projectileStride = sizeof(WorldVertex), projectileOffset = 0;
    auto* projectileBuffer = projectileVertexBuffer_.Get();
    context_->IASetInputLayout(worldInputLayout_.Get());
    context_->IASetVertexBuffers(0, 1, &projectileBuffer, &projectileStride, &projectileOffset);
    context_->VSSetShader(worldVertexShader_.Get(), nullptr, 0);
    auto* worldConstants = worldConstants_.Get();
    context_->VSSetConstantBuffers(0, 1, &worldConstants);
    context_->PSSetShader(worldPixelShader_.Get(), nullptr, 0);
    context_->PSSetConstantBuffers(0, 1, &worldConstants);
    auto* worldView = worldTexture_.view() ? worldTexture_.view() : texture_.view();
    context_->PSSetShaderResources(0, 1, &worldView);
    ID3D11ShaderResourceView* bumpView = worldBumpTexture_.view();
    ID3D11ShaderResourceView* envView = worldEnvTexture_.view();
    context_->PSSetShaderResources(1, 1, &bumpView);
    context_->PSSetShaderResources(2, 1, &envView);
    context_->PSSetSamplers(0, 1, sampler_.GetAddressOf());
    context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
    context_->Draw(projectileVertexCount_, 0);
    context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  }
  ++framePassOrdinal_;
  if (modelVertexBuffer_ && modelVertexCount_ > 0 && modelVertexShader_ && modelPixelShader_ && modelInputLayout_) {
    context_->IASetInputLayout(modelInputLayout_.Get());
    const UINT modelStride = sizeof(ModelGpuVertex), modelOffset = 0; auto* modelBuffer = modelVertexBuffer_.Get();
    context_->IASetVertexBuffers(0, 1, &modelBuffer, &modelStride, &modelOffset);
    context_->VSSetShader(modelVertexShader_.Get(), nullptr, 0);
    auto* worldConstants = worldConstants_.Get();
    context_->VSSetConstantBuffers(0, 1, &worldConstants);
    context_->PSSetShader(modelPixelShader_.Get(), nullptr, 0);
    auto* modelTexture = worldTexture_.view() ? worldTexture_.view() : texture_.view();
    context_->PSSetShaderResources(0, 1, &modelTexture);
    context_->PSSetSamplers(0, 1, sampler_.GetAddressOf());
    auto* modelConstants = modelSkinningConstants_.Get();
    context_->VSSetConstantBuffers(1, 1, &modelConstants);
    context_->Draw(modelVertexCount_, 0);
  }
  ++framePassOrdinal_;
  // First person, after the world and entity passes and before the capture, so
  // the frame written to disk already contains the weapon. The pass restores the
  // world depth state it overrides, so a later HUD pass (T3) is unaffected.
  drawViewModel();
  context_->OMSetDepthStencilState(depthStencilState_.Get(), 0);
  context_->RSSetState(rasterizerState_.Get());
  // Capture before Present: once Present has run the back buffer contents are
  // no longer defined, so this is the only point at which "the frame we just
  // composed" is still readable.
  if (!capturePath_.empty()) {
    const std::wstring path = capturePath_;
    capturePath_.clear();
    captureSucceeded_ = writeBackBufferToFile(path);
  }
  const HRESULT result = swapChain_->Present(settings_.vsync ? 1 : 0, 0);
  if (FAILED(result)) { lastError_ = result; return false; }
  return true;
}

void Renderer::shutdown() {
  if (context_) context_->ClearState();
  releaseTarget();
  texture_.reset();
  worldTexture_.reset();
  worldBumpTexture_.reset();
  worldEnvTexture_.reset();
  worldLightmapTexture_.reset();
  worldCubemapTexture_.Reset();
  worldCubemapView_.Reset();
  worldCubemapSize_ = 0;
  worldDisplacedTriangleCount_ = 0;
  worldWaterTriangleCount_ = 0;
  worldVertexBuffer_.Reset();
  projectileVertexBuffer_.Reset();
  projectileVertexCapacity_ = 0;
  projectileVertexCount_ = 0;
  entityMarkerVertexBuffer_.Reset();
  entityMarkerVertexCapacity_ = 0;
  entityMarkerVertexCount_ = 0;
  entityModelInstanceBuffer_.Reset();
  entityBoneView_.Reset();
  entityBoneBuffer_.Reset();
  entityBoneCapacity_ = 0;
  entityBoneMatrixCount_ = 0;
  entityModelInstanceCapacity_ = 0;
  entityModelVertexCount_ = 0;
  entityModelInstanceCount_ = 0;
  entityModelTexturedRangeCount_ = 0;
  entityModelTexturedInstanceCount_ = 0;
  entityModelDrawRanges_.clear();
  entityModelMeshes_.clear();
  modelVertexBuffer_.Reset();
  modelGpuStatus_ = ModelGpuStatus::NotLoaded;
  // First-person pass objects.
  viewModelVertexBuffer_.Reset();
  viewModelVertexCount_ = 0;
  viewModelWeaponVertexBuffer_.Reset();
  viewModelWeaponVertexCount_ = 0;
  viewModelWeaponMeshRadius_ = 0.0f;
  viewModelWeaponState_ = ViewModelDrawState{};
  viewModelWeaponDrawReason_ = "the weapon body pass never ran";
  viewModelWeaponCenterClip_.fill(0.0f);
  viewModelDrawRanges_.clear();
  // The cache goes with the device that owns the textures. Unlike the upload
  // counter, `viewModelTexturesReused_` is reset too: a reuse is a statement
  // about a texture that exists, and after a device reset none do.
  viewModelMaterialTextures_.clear();
  viewModelTexturesReused_ = 0;
  viewModelFallbackRanges_ = 0;
  viewModelBoundSlots_ = 0;
  viewModelBlendedRanges_ = 0;
  viewModelAlphaTestedRanges_ = 0;
  viewModelAlphaBlendState_.Reset();
  viewModelBoneView_.Reset();
  viewModelBoneBuffer_.Reset();
  viewModelBoneCapacity_ = 0;
  viewModelBoneUploadCount_ = 0;
  viewModelConstants_.Reset();
  viewModelDepthStencilState_.Reset();
  viewModelRasterizerState_.Reset();
  viewModelInputLayout_.Reset();
  viewModelVertexShader_.Reset();
  viewModelPixelShader_.Reset();
  viewModelState_ = ViewModelDrawState{};
  viewModelDrawReason_ = "the pass never ran";
  worldBoundsValid_ = false;
  worldLightmapStatus_ = WorldLightmapStatus::Unavailable;
  worldLightmapIntensity_ = 1.0f;
  worldConstants_.Reset();
  modelSkinningConstants_.Reset();
  worldInputLayout_.Reset();
  modelInputLayout_.Reset();
  entityModelInputLayout_.Reset();
  worldPixelShader_.Reset();
  worldVertexShader_.Reset();
  modelVertexShader_.Reset();
  entityModelVertexShader_.Reset();
  modelPixelShader_.Reset();
  worldVertexCount_ = 0;
  worldLightmappedVertexCount_ = 0;
  projectileVertexCount_ = 0;
  modelVertexCount_ = 0;
  sampler_.Reset();
  depthStencilState_.Reset();
  rasterizerState_.Reset();
  pixelShader_.Reset();
  vertexShader_.Reset();
  swapChain_.Reset();
  context_.Reset();
  device_.Reset();
  window_ = nullptr;
  lastError_ = S_OK;
}

} // namespace tf2::native

