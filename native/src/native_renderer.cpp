#include "native_renderer.h"
#include "native_renderer_shaders.h"
#include "renderer_internal.h"

#include "entity_model.h"
#include "hud_font.h"

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
#include <unordered_set>

namespace tf2::native {

namespace {

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
  // T77: the programs are compiled from src/native_renderer_shaders.cpp;
  // this function now only creates the state that uses them.
  using namespace tf2::native::shaders;
  Microsoft::WRL::ComPtr<ID3DBlob> vertexBytecode;
  Microsoft::WRL::ComPtr<ID3DBlob> pixelBytecode;
  HRESULT shaderResult = S_OK;
  if (!compileShader(vertexSource, "main", "vs_5_0", vertexBytecode, &shaderResult)
      || !compileShader(pixelSource, "main", "ps_5_0", pixelBytecode, &shaderResult)) {
    lastError_ = shaderResult;
    return false;
  }
  // The line/marker half of the same pass. It is a second, tiny program rather
  // than a branch of the silhouette one because the two read different vertex
  // streams: a silhouette is a model mesh with per-instance placement rows, a
  // trail is a plain world-space position with a colour.

  // ---- T52: the overlay pass's shaders -------------------------------------
  // The silhouette is the entity's own mesh drawn as a shell expanded along its
  // normals, in the team colour, with no texture and no lighting: an outline
  // that shaded itself would blend into the map instead of reading as an
  // overlay. The expansion is done in the VERTEX shader from the same instance
  // rows the entity pass uses, so the shell follows the pose (and the skinning)
  // rather than the bind pose.
  //
  // The same program draws lines and markers: a primitive with boneInfo.y == 0
  // and a zero row0/row1/row2 falls through to the flat `position` path below,
  // which is what the line and marker buffers are built in.
  // Flat colour, no texture, no lighting. An overlay pixel must be the team
  // colour the reading names; anything the shader did to it would make the
  // pixel predicate a statement about the shader instead.

  // The first-person pass has its own shader pair. It is not the entity shader
  // with a different constant buffer: the entity shader takes its placement from
  // per-instance rows (TEXCOORD1..6) and its skin offset from a packed uint4 in
  // the instance stream, neither of which exists here. The ViewModel gets one
  // composed mvp, a tint, and the skeleton in a structured buffer it indexes
  // from bone 0, with boneCount in a constant so an out-of-range index is
  // rejected in the shader exactly as the entity path does.
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
  // T52: the overlay pass's own program. Compiled here with the rest so a bad
  // edit fails at startup with a stage name instead of at the first overlay
  // frame, when the device is already drawing.
  initStage_ = "compileShaders.overlay";
  {
    Microsoft::WRL::ComPtr<ID3DBlob> overlayVertexBytecode;
    Microsoft::WRL::ComPtr<ID3DBlob> overlayPixelBytecode;
    HRESULT overlayShaderResult = S_OK;
    if (!compileShader(overlayVertexSource, "main", "vs_5_0", overlayVertexBytecode, &overlayShaderResult)
        || !compileShader(overlayPixelSource, "main", "ps_5_0", overlayPixelBytecode, &overlayShaderResult)) {
      lastError_ = overlayShaderResult;
      return false;
    }
    if (FAILED(device_->CreateVertexShader(overlayVertexBytecode->GetBufferPointer(),
          overlayVertexBytecode->GetBufferSize(), nullptr, overlayVertexShader_.GetAddressOf()))
        || FAILED(device_->CreatePixelShader(overlayPixelBytecode->GetBufferPointer(),
          overlayPixelBytecode->GetBufferSize(), nullptr, overlayPixelShader_.GetAddressOf()))) {
      lastError_ = E_FAIL;
      return false;
    }
  }

  initStage_ = "compileShaders.overlayLine";
  {
    Microsoft::WRL::ComPtr<ID3DBlob> overlayLineVertexBytecode;
    HRESULT overlayLineResult = S_OK;
    if (!compileShader(overlayLineVertexSource, "main", "vs_5_0", overlayLineVertexBytecode, &overlayLineResult)) {
      lastError_ = overlayLineResult;
      return false;
    }
    if (FAILED(device_->CreateVertexShader(overlayLineVertexBytecode->GetBufferPointer(),
          overlayLineVertexBytecode->GetBufferSize(), nullptr, overlayLineVertexShader_.GetAddressOf()))) {
      lastError_ = E_FAIL;
      return false;
    }
    // The line program's input layout: position plus a colour, nothing else.
    const D3D11_INPUT_ELEMENT_DESC overlayLineElements[] = {
      {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    if (FAILED(device_->CreateInputLayout(overlayLineElements, 2,
          overlayLineVertexBytecode->GetBufferPointer(), overlayLineVertexBytecode->GetBufferSize(),
          overlayLineInputLayout_.GetAddressOf()))) {
      lastError_ = E_FAIL;
      return false;
    }
  }

  initStage_ = "compileShaders.viewModel";
  if (!compileShader(viewModelVertexSource, "main", "vs_5_0", viewModelVertexBytecode, &viewModelShaderResult)
      || !compileShader(viewModelPixelSource, "main", "ps_5_0", viewModelPixelBytecode, &viewModelShaderResult)) {
    lastError_ = viewModelShaderResult;
    return false;
  }
  // ---- T51: the HUD pass's shader pair and states ------------------------
  //
  // HLSL constants are handed in as pixels and converted to clip space here, so
  // the pass needs no constant buffer of its own and no viewport arithmetic on
  // the CPU side that could disagree with the layout resolution: the same
  // numbers the trace prints are the ones the rasteriser received.
  initStage_ = "compileShaders.hud";
  {
    HRESULT hudShaderResult = S_OK;
    Microsoft::WRL::ComPtr<ID3DBlob> hudVertexBytecode;
    Microsoft::WRL::ComPtr<ID3DBlob> hudPixelBytecode;
    if (!compileShader(hudVertexSource, "main", "vs_5_0", hudVertexBytecode, &hudShaderResult)
        || !compileShader(hudPixelSource, "main", "ps_5_0", hudPixelBytecode, &hudShaderResult)) {
      lastError_ = hudShaderResult;
      return false;
    }
    const D3D11_INPUT_ELEMENT_DESC hudElements[] = {
      {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"TEXCOORD", 2, DXGI_FORMAT_R32_FLOAT, 0, 40, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    if (FAILED(device_->CreateVertexShader(hudVertexBytecode->GetBufferPointer(),
          hudVertexBytecode->GetBufferSize(), nullptr, hudVertexShader_.GetAddressOf()))
        || FAILED(device_->CreatePixelShader(hudPixelBytecode->GetBufferPointer(),
          hudPixelBytecode->GetBufferSize(), nullptr, hudPixelShader_.GetAddressOf()))
        || FAILED(device_->CreateInputLayout(hudElements, 5,
          hudVertexBytecode->GetBufferPointer(), hudVertexBytecode->GetBufferSize(),
          hudInputLayout_.GetAddressOf()))) {
      lastError_ = E_FAIL;
      return false;
    }
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
      // T12: the terrain blend weight and the "this material has a modulate
      // mask" flag. Both are per-vertex rather than per-draw because the whole
      // world is one draw call over an atlas, so there is no per-material
      // constant to put them in.
      {"TEXCOORD", 4, DXGI_FORMAT_R32_FLOAT, 0, 56, D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"TEXCOORD", 5, DXGI_FORMAT_R32_FLOAT, 0, 60, D3D11_INPUT_PER_VERTEX_DATA, 0},
      // T27: the per-material feature mask/params and the face's tangent frame.
      // Same reason as the blend fields above -- the world is a handful of
      // atlas draws, so the material's own feature inputs travel on the vertex.
      {"TEXCOORD", 6, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 64, D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"TEXCOORD", 7, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 80, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    initStage_ = "layout.world";
    const bool layoutCreated = SUCCEEDED(device_->CreateInputLayout(worldElements, 9, worldVertexBytecode->GetBufferPointer(), worldVertexBytecode->GetBufferSize(), worldInputLayout_.GetAddressOf()));
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
  // ---- T52: the overlay pass's own state ----------------------------------
  // Stated here rather than borrowed from the world pass: the overlay must be
  // unable to change the world's depth, and a borrowed state is a state whose
  // writes somebody else chose.
  {
    D3D11_BUFFER_DESC overlayConstantsDescription{};
    overlayConstantsDescription.ByteWidth = 96;  // mvp + cameraPositionAndExpand + mode
    overlayConstantsDescription.Usage = D3D11_USAGE_DEFAULT;
    overlayConstantsDescription.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    if (FAILED(device_->CreateBuffer(&overlayConstantsDescription, nullptr,
        overlayConstants_.GetAddressOf()))) return false;
  }
  {
    D3D11_DEPTH_STENCIL_DESC overlayDescription{};
    overlayDescription.DepthEnable = FALSE;
    overlayDescription.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    overlayDescription.DepthFunc = D3D11_COMPARISON_ALWAYS;
    overlayDescription.StencilEnable = FALSE;
    // What we ASK for, kept beside what we GET: the pair is the only way to tell a
    // state that drifted from a reading that was never wired to it.
    overlayXRayStateCreatedDepthEnable_ = overlayDescription.DepthEnable ? 1u : 0u;
    overlayXRayStateCreatedWriteMask_ = static_cast<std::uint32_t>(overlayDescription.DepthWriteMask);
    if (FAILED(device_->CreateDepthStencilState(&overlayDescription,
        overlayXRayDepthState_.GetAddressOf()))) return false;
    if (overlayXRayDepthState_) {
      D3D11_DEPTH_STENCIL_DESC created{};
      overlayXRayDepthState_->GetDesc(&created);
      overlayXRayStateReadDepthEnable_ = created.DepthEnable ? 1u : 0u;
      overlayXRayStateReadWriteMask_ = static_cast<std::uint32_t>(created.DepthWriteMask);
    }
    // The control the depth-awareness mutation needs: ordinary depth test, still
    // no depth write, so a silhouette behind a wall disappears exactly as the
    // wall says it should.
    overlayDescription.DepthEnable = TRUE;
    overlayDescription.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
    overlayDepthTestedStateCreatedDepthEnable_ = overlayDescription.DepthEnable ? 1u : 0u;
    overlayDepthTestedStateCreatedWriteMask_ = static_cast<std::uint32_t>(overlayDescription.DepthWriteMask);
    if (FAILED(device_->CreateDepthStencilState(&overlayDescription,
        overlayDepthTestedState_.GetAddressOf()))) return false;
    if (overlayDepthTestedState_) {
      D3D11_DEPTH_STENCIL_DESC created{};
      overlayDepthTestedState_->GetDesc(&created);
      overlayDepthTestedStateReadDepthEnable_ = created.DepthEnable ? 1u : 0u;
      overlayDepthTestedStateReadWriteMask_ = static_cast<std::uint32_t>(created.DepthWriteMask);
    }
    // Read both states BACK. The contract says x-ray is "depth test OFF and
    // depth write OFF"; with this readback that sentence is a number in the
    // trace instead of a comment beside the lines that set it, and a state whose
    // description drifted cannot stay silent while the picture stays plausible.
    // ID3D11DepthStencilState::GetDesc returns void, not HRESULT (D3D11 differs
    // from the D3D12-style getters here): the state object is immutable once
    // created, so the readback cannot fail and needs no SUCCEEDED wrapper. The
    // first attempt wrapped it anyway and did not compile.
    D3D11_DEPTH_STENCIL_DESC overlayReadback{};
    if (overlayXRayDepthState_) {
      overlayXRayDepthState_->GetDesc(&overlayReadback);
      overlayXRayStateReadDepthEnable_ = overlayReadback.DepthEnable ? 1u : 0u;
      overlayXRayStateReadWriteMask_ = static_cast<std::uint32_t>(overlayReadback.DepthWriteMask);
      overlayXRayDepthTestEnabled_ = overlayReadback.DepthEnable != FALSE;
      // The EFFECT, not the mask: a state whose DepthEnable is FALSE writes nothing
      // whatever its mask says -- and the runtime reports that mask as ALL, which is
      // what made this reading say "writes depth" for a pass that cannot write depth.
      overlayXRayDepthWriteEnabled_ = overlayReadback.DepthEnable != FALSE
        && overlayReadback.DepthWriteMask != D3D11_DEPTH_WRITE_MASK_ZERO;
    }
    if (overlayDepthTestedState_) {
      overlayDepthTestedState_->GetDesc(&overlayReadback);
      overlayDepthTestedStateReadDepthEnable_ = overlayReadback.DepthEnable ? 1u : 0u;
      overlayDepthTestedStateReadWriteMask_ = static_cast<std::uint32_t>(overlayReadback.DepthWriteMask);
      overlayDepthTestedDepthTestEnabled_ = overlayReadback.DepthEnable != FALSE;
      overlayDepthTestedDepthWriteEnabled_ = overlayReadback.DepthEnable != FALSE
        && overlayReadback.DepthWriteMask != D3D11_DEPTH_WRITE_MASK_ZERO;
    }
    // What this block exists for: the device must report what we asked for. Measured
    // 2026-10-23 on this machine, a state created with DepthEnable=FALSE and
    // WRITE_MASK_ZERO reads back with WRITE_MASK_ALL -- the runtime treats the mask as
    // "no opinion" when depth is off. So agreement is checked where it means something:
    // the enable flag always, the mask only when depth is enabled.
    overlayStateReadbackAgrees_ =
      overlayXRayStateCreatedDepthEnable_ == overlayXRayStateReadDepthEnable_
      && overlayDepthTestedStateCreatedDepthEnable_ == overlayDepthTestedStateReadDepthEnable_
      && (overlayXRayStateReadDepthEnable_ == 0
          || overlayXRayStateCreatedWriteMask_ == overlayXRayStateReadWriteMask_)
      && (overlayDepthTestedStateReadDepthEnable_ == 0
          || overlayDepthTestedStateCreatedWriteMask_ == overlayDepthTestedStateReadWriteMask_);
  }
  {
    D3D11_RASTERIZER_DESC overlayRasterizer{};
    overlayRasterizer.FillMode = D3D11_FILL_SOLID;
    overlayRasterizer.CullMode = D3D11_CULL_NONE;
    overlayRasterizer.FrontCounterClockwise = FALSE;
    overlayRasterizer.DepthClipEnable = TRUE;
    if (FAILED(device_->CreateRasterizerState(&overlayRasterizer,
        overlayRasterizerState_.GetAddressOf()))) return false;
  }

  D3D11_DEPTH_STENCIL_DESC depthDescription{};
  depthDescription.DepthEnable = TRUE;
  depthDescription.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
  depthDescription.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
  if (FAILED(device_->CreateDepthStencilState(&depthDescription, depthStencilState_.GetAddressOf()))) return false;
  D3D11_DEPTH_STENCIL_DESC skyDepthDescription = depthDescription;
  skyDepthDescription.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
  if (FAILED(device_->CreateDepthStencilState(&skyDepthDescription,
      worldSkyDepthStencilState_.GetAddressOf()))) return false;
  // T96: the world alpha pass's two device objects. The depth state is the
  // world's own test with writes cleared -- a translucent surface must still be
  // hidden by an opaque one in front of it, but must not hide a second
  // translucent surface behind it. The blend state is the same SrcAlpha /
  // InvSrcAlpha recipe the viewmodel pass uses for its `$translucent` ranges;
  // created once here rather than lazily in the draw path, for the same reason
  // that block states.
  D3D11_DEPTH_STENCIL_DESC worldBlendedDepthDescription = depthDescription;
  worldBlendedDepthDescription.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
  initStage_ = "createDepthState.worldBlended";
  if (FAILED(device_->CreateDepthStencilState(&worldBlendedDepthDescription,
      worldBlendedDepthStencilState_.GetAddressOf()))) return false;
  D3D11_BLEND_DESC worldAlphaBlendDescription{};
  worldAlphaBlendDescription.RenderTarget[0].BlendEnable = TRUE;
  worldAlphaBlendDescription.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
  worldAlphaBlendDescription.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
  worldAlphaBlendDescription.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
  worldAlphaBlendDescription.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
  worldAlphaBlendDescription.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
  worldAlphaBlendDescription.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
  worldAlphaBlendDescription.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
  initStage_ = "createBlendState.world";
  if (FAILED(device_->CreateBlendState(&worldAlphaBlendDescription,
      worldAlphaBlendState_.GetAddressOf()))) return false;
  D3D11_RASTERIZER_DESC rasterDescription{};
  rasterDescription.FillMode = D3D11_FILL_SOLID;
  rasterDescription.CullMode = D3D11_CULL_BACK;
  rasterDescription.FrontCounterClockwise = FALSE;
  rasterDescription.DepthClipEnable = TRUE;
  if (FAILED(device_->CreateRasterizerState(&rasterDescription, rasterizerState_.GetAddressOf()))) return false;
  // The sky box is drawn from the inside, so the world's back-face culling
  // would remove all six of its sides and the sky would be a no-op that
  // reported itself as drawn. Culling off for that one pass is the honest fix;
  // winding the box inward would work too and would fail silently the first
  // time somebody changed the box.
  D3D11_RASTERIZER_DESC skyRasterDescription = rasterDescription;
  skyRasterDescription.CullMode = D3D11_CULL_NONE;
  if (FAILED(device_->CreateRasterizerState(&skyRasterDescription,
      worldSkyRasterizerState_.GetAddressOf()))) return false;
  // T10 diagnostic: the world rasterizer with culling off, so one capture can
  // draw the frame both ways and the missing faces get attributed to the
  // winding instead of guessed at. The product's own draws keep cull-back;
  // only a caller that turned the diagnostic on reaches this state.
  D3D11_RASTERIZER_DESC worldNoCullDescription = rasterDescription;
  worldNoCullDescription.CullMode = D3D11_CULL_NONE;
  if (FAILED(device_->CreateRasterizerState(&worldNoCullDescription,
      worldNoCullRasterizerState_.GetAddressOf()))) return false;
  // The water pass draws the same triangles the world pass already drew, so a
  // coplanar surface would z-fight and the frame would speckle instead of
  // showing water. The bias pulls the water toward the camera; it is stated
  // rather than left to the driver because a bias that is too small produces a
  // plausible-looking noisy picture and one that is too large pulls water over
  // geometry that should be in front of it. Both values are pinned constants and
  // the pixel comparison would redden if they changed.
  D3D11_RASTERIZER_DESC waterRasterDescription = rasterDescription;
  waterRasterDescription.DepthBias = -256;
  waterRasterDescription.SlopeScaledDepthBias = -2.0f;
  if (FAILED(device_->CreateRasterizerState(&waterRasterDescription,
      worldWaterRasterizerState_.GetAddressOf()))) return false;
  D3D11_BUFFER_DESC constantsDescription{};
  // ByteWidth must equal sizeof(WorldConstantsData) in drawFrame(): mvp (64
  // bytes) plus eight float4 blocks (128) is 192. It was 160 -- "rounded up so
  // the next block added does not quietly fall off the end" -- and that padding
  // was never harmless: UpdateSubresource copies ByteWidth bytes out of a
  // 144-byte struct, so every frame read 16 bytes past the end of it. The same
  // comment was here when the buffer was 128 and the struct 144, and it did not
  // stop *that* over-read either, which is why the invariant is now a
  // static_assert beside the struct (native_renderer_frame.cpp) and this number
  // is exactly the sizeof rather than a round number above it.
  //
  // T101 added the three fog blocks (fogParams, fogColor, fogScale) to the
  // struct and this number together, in one commit, for the same reason: a
  // field added without the ByteWidth is a field the constant buffer never
  // carries, and a ByteWidth raised without a field is an over-read.
  constantsDescription.ByteWidth = 192;
  constantsDescription.Usage = D3D11_USAGE_DEFAULT;
  constantsDescription.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
  if (FAILED(device_->CreateBuffer(&constantsDescription, nullptr, worldConstants_.GetAddressOf()))) return false;
  // The sky's own copy of the same layout, because the sky draw needs a
  // different mvp (translation removed) from the one the world draw is using in
  // the same frame, and two UpdateSubresource calls into one buffer would make
  // whichever ran second the truth for both passes.
  if (FAILED(device_->CreateBuffer(&constantsDescription, nullptr, worldSkyConstants_.GetAddressOf()))) return false;
  // ---- T51: the HUD pass's constants, states and vertex buffer -----------
  //
  // Depth test and depth write are both OFF, and the rasteriser culls nothing:
  // a HUD panel is screen-space geometry whose winding is an implementation
  // detail of the quad builder, and letting it be culled would silently drop
  // panels. Blending is standard src-alpha so a panel colour can be translucent
  // without inventing a second rule.
  D3D11_BUFFER_DESC hudConstantsDescription{};
  hudConstantsDescription.ByteWidth = 16;   // one float4: screenSize
  hudConstantsDescription.Usage = D3D11_USAGE_DEFAULT;
  hudConstantsDescription.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
  if (FAILED(device_->CreateBuffer(&hudConstantsDescription, nullptr, hudConstants_.GetAddressOf()))) return false;
  D3D11_DEPTH_STENCIL_DESC hudDepthDescription{};
  hudDepthDescription.DepthEnable = FALSE;
  hudDepthDescription.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
  hudDepthDescription.DepthFunc = D3D11_COMPARISON_ALWAYS;
  hudDepthDescription.StencilEnable = FALSE;
  if (FAILED(device_->CreateDepthStencilState(&hudDepthDescription, hudDepthStencilState_.GetAddressOf()))) return false;
  D3D11_RASTERIZER_DESC hudRasterDescription{};
  hudRasterDescription.FillMode = D3D11_FILL_SOLID;
  hudRasterDescription.CullMode = D3D11_CULL_NONE;
  hudRasterDescription.FrontCounterClockwise = FALSE;
  // DepthClipEnable FALSE keeps a rect whose coordinates are exactly on the
  // edge from being clipped by rounding: the layout already clipped it to the
  // client, and a second clip here would make the drawn region differ from the
  // region the gate excludes.
  hudRasterDescription.DepthClipEnable = FALSE;
  hudRasterDescription.ScissorEnable = TRUE;
  if (FAILED(device_->CreateRasterizerState(&hudRasterDescription, hudRasterizerState_.GetAddressOf()))) return false;
  D3D11_BLEND_DESC hudBlendDescription{};
  hudBlendDescription.AlphaToCoverageEnable = FALSE;
  hudBlendDescription.IndependentBlendEnable = FALSE;
  hudBlendDescription.RenderTarget[0].BlendEnable = TRUE;
  hudBlendDescription.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
  hudBlendDescription.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
  hudBlendDescription.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
  hudBlendDescription.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
  hudBlendDescription.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
  hudBlendDescription.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
  hudBlendDescription.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
  if (FAILED(device_->CreateBlendState(&hudBlendDescription, hudBlendState_.GetAddressOf()))) return false;
  // T67: the embedded font atlas, uploaded once. R8_UNORM and immutable: the
  // table is compiled into the binary, so nothing can fail at draw time and
  // nothing needs updating. Point sampling with clamp addressing makes a texel
  // fetch exact -- a filter that blended neighbouring cells would make the
  // frame-versus-table comparison meaningless.
  {
    const unsigned char* atlas = hudFontAtlas();
    const UINT atlasWidth = static_cast<UINT>(hudFontAtlasWidth());
    const UINT atlasHeight = static_cast<UINT>(hudFontAtlasHeight());
    if (!atlas || atlasWidth == 0 || atlasHeight == 0) { lastError_ = E_FAIL; return false; }
    D3D11_TEXTURE2D_DESC atlasDescription{};
    atlasDescription.Width = atlasWidth;
    atlasDescription.Height = atlasHeight;
    atlasDescription.MipLevels = 1;
    atlasDescription.ArraySize = 1;
    atlasDescription.Format = DXGI_FORMAT_R8_UNORM;
    atlasDescription.SampleDesc.Count = 1;
    atlasDescription.Usage = D3D11_USAGE_IMMUTABLE;
    atlasDescription.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    const D3D11_SUBRESOURCE_DATA atlasData{atlas, atlasWidth, 0};
    if (FAILED(device_->CreateTexture2D(&atlasDescription, &atlasData, hudFontTexture_.GetAddressOf()))) return false;
    D3D11_SHADER_RESOURCE_VIEW_DESC viewDescription{};
    viewDescription.Format = atlasDescription.Format;
    viewDescription.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    viewDescription.Texture2D.MipLevels = 1;
    if (FAILED(device_->CreateShaderResourceView(hudFontTexture_.Get(), &viewDescription, hudFontView_.GetAddressOf()))) return false;
    D3D11_SAMPLER_DESC samplerDescription{};
    samplerDescription.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    samplerDescription.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDescription.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDescription.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDescription.ComparisonFunc = D3D11_COMPARISON_NEVER;
    samplerDescription.MaxLOD = D3D11_FLOAT32_MAX;
    if (FAILED(device_->CreateSamplerState(&samplerDescription, hudSampler_.GetAddressOf()))) return false;
  }
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

// T101: what the world pass was uploaded with. The two scales are derived from
// the same members the vertex convert uses (native_renderer_world.cpp:93-101,
// :188), which is the point: a caller that recomputed them could disagree with
// the buffer, and then the fog would sit at a different depth than the geometry
// it is fogging.
WorldFogUploadState Renderer::worldFogState() const {
  WorldFogUploadState state;
  state.fog = worldFog_;
  if (std::isfinite(worldHorizontalScale_) && worldHorizontalScale_ > 0.0f) {
    state.boxToMapXZ = 1.0f / worldHorizontalScale_;
  }
  if (std::isfinite(worldSpanZ_) && worldSpanZ_ > 0.0f) {
    state.boxToMapZ = worldSpanZ_ / 0.8f;
  }
  state.verticesFogged = worldFogVertices_;
  return state;
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


void Renderer::resize(UINT width, UINT height) {
  if (!swapChain_ || width == 0 || height == 0 || (width == width_ && height == height_)) return;
  releaseTarget();
  const HRESULT result = swapChain_->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
  if (FAILED(result)) { lastError_ = result; return; }
  if (!createTarget(width, height)) lastError_ = E_FAIL;
  else lastError_ = S_OK;
}


void Renderer::shutdown() {
  if (context_) context_->ClearState();
  releaseTarget();
  texture_.reset();
  worldTexture_.reset();
  worldBlendTexture_.reset();
  worldBlendModulateTexture_.reset();
  worldBumpTexture_.reset();
  worldBumpAtlasTextures_.clear();
  worldBump2AtlasTextures_.clear();
  worldBumpFeature_ = WorldFeatureState{};
  worldBump2Feature_ = WorldFeatureState{};
  worldDetailFeature_ = WorldFeatureState{};
  worldDetailAtlasTextures_.clear();
  worldEnvTexture_.reset();
  worldLightmapTexture_.reset();
  worldCubemapTexture_.Reset();
  worldCubemapView_.Reset();
  worldCubemapSize_ = 0;
  worldSkyTexture_.Reset();
  worldSkyView_.Reset();
  worldSkyVertexBuffer_.Reset();
  worldSkyConstants_.Reset();
  worldSkyRasterizerState_.Reset();
  worldNoCullRasterizerState_.Reset();
  worldSkyVertexCount_ = 0;
  worldSkySide_ = 0;
  worldSkyIndexCapacity_ = 0;
  worldSkyResampled_ = false;
  worldSkyState_ = WorldSkyState::Unavailable;
  worldSkyReason_ = "no sky box was uploaded";
  worldSkyDrewThisFrame_ = false;
  // ---- T2 (2026-10-14): the water surface --------------------------------
  // Reset with the rest of the world's GPU state so a new map starts from
  // "no water was uploaded" rather than from the previous map's surface.
  worldWaterVertexBuffer_.Reset();
  worldWaterTexture_.reset();
  worldWaterRasterizerState_.Reset();
  worldWaterVertexCount_ = 0;
  worldWaterTextureSide_ = 0;
  worldWaterTextureBound_ = false;
  worldWaterDrewThisFrame_ = false;
  worldWaterState_ = WorldWaterSurfaceState::Unavailable;
  worldWaterReflectionState_ = WorldWaterSurfaceState::Unavailable;
  worldWaterRefractionState_ = WorldWaterSurfaceState::Unavailable;
  worldWaterReason_ = "no water geometry was uploaded";
  waterSurfaceSample_ = WaterSurfaceSampleState{};
  // T2 (2026-10-14): the ViewModel's advanced-material GPU state. The counters
  // go back to zero with the resources they describe; the cumulative upload
  // counter does not, because it is a whole-run reading.
  viewModelBumpTexture_.Reset();
  viewModelBumpView_.Reset();
  viewModelSelfillumMaskTexture_.Reset();
  viewModelSelfillumMaskView_.Reset();
  viewModelBumpBound_ = false;
  viewModelSelfillumMaskBound_ = false;
  viewModelMaterialEffects_ = ViewModelMaterialEffects{};
  viewModelMaterialEffectsSet_ = false;
  viewModelMaterialSample_ = ViewModelMaterialSampleState{};
  worldIndexBuffer_.Reset();
  worldIndexCapacity_ = 0;
  worldIndexCount_ = 0;
  worldVisClipApplied_ = false;
  worldVisDrawnTriangles_ = 0;
  worldVisHiddenTriangles_ = 0;
  worldVisTrianglesWithoutCluster_ = 0;
  worldVisInputTriangles_ = 0;
  worldVisCameraCluster_ = -1;
  worldVisClipState_ = WorldVisClipState::Unavailable;
  worldVisClipReason_ = "no pvs draw list was handed to the renderer";
  worldVisDrewIndexed_ = false;
  worldDisplacedTriangleCount_ = 0;
  worldWaterTriangleCount_ = 0;
  worldVertexBuffer_.Reset();
  projectileVertexBuffer_.Reset();
  projectileVertexCapacity_ = 0;
  projectileVertexCount_ = 0;
  projectileTrackVertexBuffer_.Reset();
  projectileTrackVertexCapacity_ = 0;
  projectileTrackVertexCount_ = 0;
  projectileTrackPointCount_ = 0;
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
  entityModelInstanceSlotCount_ = 0;
  entityModelTexturedRangeCount_ = 0;
  entityModelTexturedInstanceCount_ = 0;
  entityModelMeshTruncatedCount_ = 0;
  entityModelTextureCount_ = 0;
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
  projectileTrackVertexCount_ = 0;
  projectileTrackPointCount_ = 0;
  modelVertexCount_ = 0;
  sampler_.Reset();
  depthStencilState_.Reset();
  worldSkyDepthStencilState_.Reset();
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

