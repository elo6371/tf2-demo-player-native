// Viewmodel: mesh/material/bone uploads and the viewmodel pass itself.
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

bool Renderer::uploadViewModelWeaponMesh(const std::vector<ModelDrawVertex>& vertices,
    const std::vector<ViewModelDrawRange>& ranges) {
  if (!uploadViewModelMeshInto(device_.Get(), vertices, viewModelWeaponVertexBuffer_,
      viewModelWeaponVertexCount_, viewModelWeaponMeshCenter_, viewModelWeaponMeshRadius_)) {
    viewModelWeaponVertexCount_ = 0;
    viewModelWeaponDrawRanges_.clear();
    lastError_ = E_INVALIDARG;
    return false;
  }
  // Same clamping rule as the arms' upload: a range is accepted only inside the
  // buffer that was actually created, and a non-multiple-of-three count is
  // truncated rather than turned into a partial triangle. A range that clamps to
  // nothing is dropped, which is how "the MDL's second descriptor did not fit"
  // reaches the readings as a smaller range count instead of as a Draw that
  // reads past the buffer.
  viewModelWeaponDrawRanges_.clear();
  for (const auto& range : ranges) {
    if (range.firstVertex >= viewModelWeaponVertexCount_) continue;
    ViewModelDrawRange clamped = range;
    const auto available = viewModelWeaponVertexCount_ - range.firstVertex;
    clamped.vertexCount = std::min(range.vertexCount, available);
    clamped.vertexCount -= clamped.vertexCount % 3u;
    if (clamped.vertexCount != 0) viewModelWeaponDrawRanges_.push_back(std::move(clamped));
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

// ---------------------------------------------------------------------------
// T2 (2026-10-14): the ViewModel's advanced materials
// ---------------------------------------------------------------------------
// Three effects, three separate texture slots, and one rule that the whole
// section exists to enforce: a normal map is never uploaded as the albedo. The
// albedo cache is keyed by the slot's own normalized vtf path and the normal map
// has its own single texture, so the two cannot collide by construction rather
// than by a check somebody has to remember.

bool Renderer::uploadViewModelBumpTexture(const std::vector<std::uint8_t>& rgba, UINT width, UINT height) {
  viewModelBumpTexture_.Reset();
  viewModelBumpView_.Reset();
  viewModelBumpBound_ = false;
  // Empty is the material's own "no normal map": the pass draws plain lighting
  // and the reading says so. It is not a failure.
  if (rgba.empty() || width == 0 || height == 0) return true;
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
  if (FAILED(device_->CreateTexture2D(&description, &data, viewModelBumpTexture_.GetAddressOf()))) {
    lastError_ = E_FAIL;
    return false;
  }
  if (FAILED(device_->CreateShaderResourceView(viewModelBumpTexture_.Get(), nullptr,
      viewModelBumpView_.GetAddressOf()))) {
    viewModelBumpTexture_.Reset();
    lastError_ = E_FAIL;
    return false;
  }
  viewModelBumpBound_ = true;
  // Cumulative and never reset: the reading that catches a pass which rebuilds
  // its normal map every frame. A per-frame reset would make that look constant.
  ++viewModelBumpTexturesUploaded_;
  return true;
}

bool Renderer::uploadViewModelSelfillumMask(const std::vector<std::uint8_t>& rgba, UINT width, UINT height) {
  viewModelSelfillumMaskTexture_.Reset();
  viewModelSelfillumMaskView_.Reset();
  viewModelSelfillumMaskBound_ = false;
  if (rgba.empty() || width == 0 || height == 0) return true;
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
  if (FAILED(device_->CreateTexture2D(&description, &data,
      viewModelSelfillumMaskTexture_.GetAddressOf()))) {
    lastError_ = E_FAIL;
    return false;
  }
  if (FAILED(device_->CreateShaderResourceView(viewModelSelfillumMaskTexture_.Get(), nullptr,
      viewModelSelfillumMaskView_.GetAddressOf()))) {
    viewModelSelfillumMaskTexture_.Reset();
    lastError_ = E_FAIL;
    return false;
  }
  viewModelSelfillumMaskBound_ = true;
  return true;
}

void Renderer::setViewModelMaterialEffects(const ViewModelMaterialEffects& effects) {
  viewModelMaterialEffects_ = effects;
  viewModelMaterialEffectsSet_ = true;
}

ViewModelMaterialSampleState Renderer::viewModelMaterialSampleState() const {
  // Assembled from the stored effects plus the per-frame range counters the draw
  // itself incremented. The counters are what makes `applied` a fact about a
  // frame rather than about a declaration: a material can declare Phong and be
  // drawn by a range whose texture never resolved, and that is a different row.
  ViewModelMaterialSampleState sample{};
  const auto& fx = viewModelMaterialEffects_;

  // ---- Phong ----
  sample.phongDeclared = fx.phong.declared;
  sample.phongParsed = fx.phong.parsed;
  sample.phongUploaded = fx.phong.parsed && fx.shaderReady;
  sample.phongApplied = viewModelPhongAppliedRanges_ > 0;
  sample.phongAppliedRanges = viewModelPhongAppliedRanges_;
  sample.phongFallback = fx.phong.declared
    && (!fx.phong.parsed || viewModelPhongAppliedRanges_ == 0);
  sample.phongExponent = fx.phong.exponent;
  sample.phongBoost = fx.phong.boost;
  sample.phongFresnelMin = fx.phong.fresnel[0];
  sample.phongFresnelMax = fx.phong.fresnel[1];
  for (int i = 0; i < 3; ++i) sample.phongTint[i] = fx.phong.tint[i];
  if (!fx.phong.declared) {
    sample.phongState = ViewModelEffectState::Unavailable;
  } else if (!fx.phong.parsed) {
    sample.phongState = ViewModelEffectState::Declared;
  } else if (viewModelPhongAppliedRanges_ > 0) {
    sample.phongState = ViewModelEffectState::Active;
  } else {
    sample.phongState = ViewModelEffectState::Fallback;
  }
  sample.phongReason = !fx.shaderReady
    ? std::string("the viewmodel pixel shader is not ready")
    : (fx.phong.reason.empty() ? std::string("no phong was read") : fx.phong.reason);

  // ---- Bump ----
  sample.bumpDeclared = fx.bump.declared;
  sample.bumpParsed = fx.bump.parsed;
  sample.bumpTexturePath = fx.bump.boundPath;
  sample.bumpTextureResolved = fx.bump.parsed && !fx.bump.boundPath.empty();
  sample.bumpTextureUploaded = viewModelBumpBound_;
  // A bind served out of the existing texture rather than a fresh upload. Read
  // together with bumpTexturesUploaded staying at 1 over many frames, this is
  // the per-frame-recreation reading.
  sample.bumpTextureReused = viewModelBumpBound_
    && viewModelBumpTexturesUploaded_ > 0 && viewModelBumpAppliedRanges_ > 0;
  sample.bumpUploaded = viewModelBumpBound_;
  sample.bumpApplied = viewModelBumpAppliedRanges_ > 0;
  sample.bumpAppliedRanges = viewModelBumpAppliedRanges_;
  sample.bumpFallback = fx.bump.declared
    && (!fx.bump.parsed || !viewModelBumpBound_ || viewModelBumpAppliedRanges_ == 0);
  if (!fx.bump.declared) {
    sample.bumpState = ViewModelEffectState::Unavailable;
  } else if (!fx.bump.parsed) {
    sample.bumpState = ViewModelEffectState::Declared;
  } else if (viewModelBumpAppliedRanges_ > 0) {
    sample.bumpState = ViewModelEffectState::Active;
  } else {
    sample.bumpState = ViewModelEffectState::Fallback;
  }
  sample.bumpReason = !fx.shaderReady
    ? std::string("the viewmodel pixel shader is not ready")
    : (fx.bump.reason.empty() ? std::string("no normal map was read") : fx.bump.reason);

  // ---- Selfillum ----
  sample.selfillumDeclared = fx.selfillum.declared;
  sample.selfillumParsed = fx.selfillum.parsed;
  sample.selfillumMaskDeclared = fx.selfillum.maskDeclared;
  sample.selfillumMaskBound = viewModelSelfillumMaskBound_;
  for (int i = 0; i < 3; ++i) {
    sample.selfillumTint[i] = fx.selfillum.tint[i];
  }
  sample.selfillumUploaded = fx.selfillum.parsed
    && (viewModelSelfillumMaskBound_ || fx.selfillum.maskFromAlpha);
  sample.selfillumApplied = viewModelSelfillumAppliedRanges_ > 0;
  sample.selfillumAppliedRanges = viewModelSelfillumAppliedRanges_;
  sample.selfillumFallback = fx.selfillum.declared
    && (!fx.selfillum.parsed || viewModelSelfillumAppliedRanges_ == 0);
  if (!fx.selfillum.declared) {
    sample.selfillumState = ViewModelEffectState::Unavailable;
  } else if (!fx.selfillum.parsed) {
    sample.selfillumState = ViewModelEffectState::Declared;
  } else if (viewModelSelfillumAppliedRanges_ > 0) {
    sample.selfillumState = ViewModelEffectState::Active;
  } else {
    sample.selfillumState = ViewModelEffectState::Fallback;
  }
  sample.selfillumReason = !fx.shaderReady
    ? std::string("the viewmodel pixel shader is not ready")
    : (fx.selfillum.reason.empty() ? std::string("no selfillum was read")
                                   : fx.selfillum.reason);

  sample.texturesUploaded = viewModelTexturesUploaded_;
  sample.texturesReused = viewModelTexturesReused_;
  return sample;
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
    // The shader reads this table as `StructuredBuffer<float4>` -- four elements
    // per bone -- so the structure stride is one float4, not one matrix. The
    // bytes are identical either way; only the stride tells the shader how to
    // find bone N, and a 64-byte stride with a 16-byte element type would make
    // every bone after the first one read the wrong 16 floats.
    description.ByteWidth = static_cast<UINT>(capacity * sizeof(std::array<float, 16>));
    description.Usage = D3D11_USAGE_DYNAMIC;
    description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    description.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    description.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    description.StructureByteStride = sizeof(float) * 4;
    if (FAILED(device_->CreateBuffer(&description, nullptr, viewModelBoneBuffer_.ReleaseAndGetAddressOf()))) {
      return false;
    }
    D3D11_SHADER_RESOURCE_VIEW_DESC view{};
    view.Format = DXGI_FORMAT_UNKNOWN;
    view.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    view.Buffer.FirstElement = 0;
    view.Buffer.NumElements = static_cast<UINT>(capacity * 4);
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
  // Same rule for the framing numbers: a frame that did not draw must not report
  // the last frame's distance as if it had solved for one.
  viewModelFramePlanes_.fill(0.0f);
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
  viewModelFramePlanes_ = { nearPlane, farPlane, distance, radius };

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
  // ---- T2 (2026-10-14): the advanced-material constants --------------------
  // The same `view` the mvp was built from, so the shader's view-space normal
  // and the highlight derived from it belong to the camera that drew this frame
  // rather than to a second camera reconstructed in the shader.
  XMStoreFloat4x4(&constants.viewMatrix, view);
  const auto& effects = viewModelMaterialEffects_;
  // A branch runs only when the material parsed AND the shader compiled. The
  // shaderReady flag is the caller's answer to "did the pixel shader build",
  // which is the one fact this function cannot see: a shader that failed to
  // compile is a fallback for all three effects at once.
  const bool phongOn = effects.shaderReady && effects.phong.declared && effects.phong.parsed;
  const bool bumpOn = effects.shaderReady && effects.bump.declared && effects.bump.parsed
    && viewModelBumpBound_;
  const bool selfillumOn = effects.shaderReady && effects.selfillum.declared
    && effects.selfillum.parsed;
  constants.phongParams = XMFLOAT4(effects.phong.exponent, effects.phong.boost,
    effects.phong.fresnel[0], effects.phong.fresnel[1]);
  constants.phongTint = XMFLOAT4(effects.phong.tint[0], effects.phong.tint[1],
    effects.phong.tint[2], 1.0f);
  constants.phongFeatures = XMFLOAT4(phongOn ? 1.0f : 0.0f,
    effects.phong.tintDeclared ? 1.0f : 0.0f, 0.0f, 0.0f);
  constants.bumpFeatures = XMFLOAT4(bumpOn ? 1.0f : 0.0f,
    effects.bump.alphaEnvMapMask ? 1.0f : 0.0f, 0.0f, 0.0f);
  constants.selfillumFeatures = XMFLOAT4(selfillumOn ? 1.0f : 0.0f,
    effects.selfillum.maskFromAlpha ? 1.0f : 0.0f, 0.0f, 0.0f);
  constants.selfillumTint = XMFLOAT4(effects.selfillum.tint[0], effects.selfillum.tint[1],
    effects.selfillum.tint[2], 1.0f);
  XMStoreFloat4x4(reinterpret_cast<XMFLOAT4X4*>(viewModelMvp_.data()), mvp);
  // The three per-range counters are reset here, at the top of the pass, so a
  // frame that draws nothing cannot inherit the previous frame's counts. The
  // texture-upload counter above is deliberately NOT reset: it is a whole-run
  // reading and a per-frame reset would hide a pass that rebuilds every frame.
  viewModelPhongAppliedRanges_ = 0;
  viewModelBumpAppliedRanges_ = 0;
  viewModelSelfillumAppliedRanges_ = 0;
  viewModelPhongFallbackRanges_ = 0;
  viewModelBumpFallbackRanges_ = 0;
  viewModelSelfillumFallbackRanges_ = 0;

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
  // t1 and t2 are bound unconditionally too, for the same reason t4 is: the
  // shader's branch is what decides whether they are sampled, and an unbound
  // sampler slot is read as garbage rather than as white. A null view here is
  // the honest "this material has no normal map", and `bumpFeatures.x` is the
  // flag that stops the branch from sampling it.
  {
    auto* bumpView = viewModelBumpView_.Get();
    auto* maskView = viewModelSelfillumMaskView_.Get();
    context_->PSSetShaderResources(1, 1, &bumpView);
    context_->PSSetShaderResources(2, 1, &maskView);
  }

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
        // T2 (2026-10-14): the per-range effect counts. Counted here, on the
        // range that is about to draw with a texture bound, so `applied` is a
        // fact about this frame's draw and not about the material's own
        // declaration. A material whose slot resolved but whose range was never
        // drawn increments nothing, which is the row the task's
        // "declared is not applied" rule exists for.
        if (phongOn) ++viewModelPhongAppliedRanges_;
        if (bumpOn) ++viewModelBumpAppliedRanges_;
        if (selfillumOn) ++viewModelSelfillumAppliedRanges_;
      } else {
        context_->PSSetShaderResources(0, 1, &nullView);
        context_->OMSetBlendState(nullptr, nullptr, 0xffffffffu);
        ++viewModelFallbackRanges_;
        // The flat-colour range: every advanced branch is off for it, and the
        // fallback counters are what say so rather than leaving a reader to
        // infer it from an applied count of zero.
        if (effects.phong.declared) ++viewModelPhongFallbackRanges_;
        if (effects.bump.declared) ++viewModelBumpFallbackRanges_;
        if (effects.selfillum.declared) ++viewModelSelfillumFallbackRanges_;
      }
      context_->Draw(range.vertexCount, range.firstVertex);
    }
    viewModelBoundSlots_ = boundSlots.size();
  }
  context_->PSSetShaderResources(0, 1, &nullView);
  ID3D11ShaderResourceView* noAux = nullptr;
  context_->PSSetShaderResources(1, 1, &noAux);
  context_->PSSetShaderResources(2, 1, &noAux);
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
  // Reset before the branch, not inside the drawn branch: a body that is off or
  // whose mesh is missing must report zero ranges and zero bound slots rather
  // than the previous frame's, or the readings would describe a draw that did
  // not happen this frame.
  viewModelWeaponDrawnRanges_ = 0;
  viewModelWeaponTexturedRanges_ = 0;
  viewModelWeaponFallbackRanges_ = 0;
  viewModelWeaponBoundSlots_ = 0;
  viewModelWeaponMaterialKeys_.clear();
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
    // The weapon has its own albedo and intentionally does not inherit the
    // arms' advanced effects.
    XMStoreFloat4x4(&weaponConstants.viewMatrix, view);
    weaponConstants.phongFeatures = XMFLOAT4(0, 0, 0, 0);
    weaponConstants.bumpFeatures = XMFLOAT4(0, 0, 0, 0);
    weaponConstants.selfillumFeatures = XMFLOAT4(0, 0, 0, 0);
    const auto weaponTexture = viewModelMaterialTextures_.find(viewModelWeaponState_.materialKey);
    const bool hasWeaponTexture = weaponTexture != viewModelMaterialTextures_.end();
    auto* weaponBuffer = viewModelWeaponVertexBuffer_.Get();
    context_->IASetVertexBuffers(0, 1, &weaponBuffer, &stride, &offset);
    if (viewModelWeaponDrawRanges_.empty()) {
      // No range information: the whole-buffer draw this pass issued before
      // T14, from the one material key on the state. Kept as that rather than
      // synthesised into a one-range table, because the picture is the same and
      // a caller that supplied no ranges cannot have wanted a range reading.
      weaponConstants.material = XMFLOAT4(hasWeaponTexture ? 1.0f : 0.0f,
        viewModelWeaponState_.materialAlphaTest ? 1.0f : 0.0f, 0, 0);
      context_->UpdateSubresource(viewModelConstants_.Get(), 0, nullptr, &weaponConstants, 0, 0);
      ID3D11ShaderResourceView* weaponAlbedo = hasWeaponTexture ? weaponTexture->second.Get() : nullptr;
      bindSlot0(weaponAlbedo, "weapon-single");
      if (isWorldAtlasView(lastSlot0View_)) ++weaponDrawsWithWorldAtlas_;
      context_->OMSetBlendState(viewModelWeaponState_.materialTranslucent
        ? viewModelAlphaBlendState_.Get() : nullptr, nullptr, 0xffffffffu);
      context_->Draw(viewModelWeaponVertexCount_, 0);
      ++viewModelWeaponDrawnRanges_;
      if (hasWeaponTexture) {
        ++viewModelWeaponTexturedRanges_;
        viewModelWeaponBoundSlots_ = 1;
        viewModelWeaponMaterialKeys_ = weaponTexture->first;
      } else {
        ++viewModelWeaponFallbackRanges_;
      }
    } else {
      // One draw per range, each with its own material. This is the T14 fix:
      // the body's MDL names two slots for two meshes, and binding one texture
      // for the whole buffer paints the scope with the rifle's albedo.
      std::vector<std::int32_t> boundSlots;
      for (const auto& range : viewModelWeaponDrawRanges_) {
        auto cached = range.materialKey.empty()
          ? viewModelMaterialTextures_.end()
          : viewModelMaterialTextures_.find(range.materialKey);
        const bool textured = cached != viewModelMaterialTextures_.end();
        weaponConstants.material = XMFLOAT4(textured ? 1.0f : 0.0f,
          range.alphaTest ? 1.0f : 0.0f, 0.0f, 0.0f);
        context_->UpdateSubresource(viewModelConstants_.Get(), 0, nullptr, &weaponConstants, 0, 0);
        if (textured) {
          auto* view = cached->second.Get();
          bindSlot0(view, "weapon-range");
          // T19: the leak question, asked at the draw. A range drawn while a
          // world atlas is at t0 is the defect this counter exists for; the
          // gate wants it at 0 and the mutation binds the atlas here to prove
          // the number can move.
          if (isWorldAtlasView(lastSlot0View_)) ++weaponDrawsWithWorldAtlas_;
          context_->OMSetBlendState(range.translucent ? viewModelAlphaBlendState_.Get() : nullptr,
            nullptr, 0xffffffffu);
          ++viewModelWeaponTexturedRanges_;
          if (std::find(boundSlots.begin(), boundSlots.end(), range.slot) == boundSlots.end()) {
            boundSlots.push_back(range.slot);
          }
          if (!viewModelWeaponMaterialKeys_.empty()) viewModelWeaponMaterialKeys_ += ",";
          // The key of the SRV that was actually bound -- NOT range.materialKey.
          // These are the same string on a correct run, and the difference is the
          // whole point: a reading built from what the range ASKED for cannot
          // see a lookup that answered with something else. m3 in
          // weapon-material-check.sh binds the hands' atlas here while the
          // range's own key still reads correctly; only this line catches it.
          viewModelWeaponMaterialKeys_ += cached->first;
        } else {
          // The range's slot did not resolve, or --weapon flat colour emptied
          // its key. Flat colour is the control and this is the fallback; the
          // two reach the same state on purpose, so a fallback can never be
          // mistaken for a texture that happened to be white.
          bindSlot0(nullView, "weapon-range-fallback");
          context_->OMSetBlendState(nullptr, nullptr, 0xffffffffu);
          ++viewModelWeaponFallbackRanges_;
        }
        context_->Draw(range.vertexCount, range.firstVertex);
        ++viewModelWeaponDrawnRanges_;
      }
      viewModelWeaponBoundSlots_ = boundSlots.size();
    }
    bindSlot0(nullView, "weapon-clear");
    context_->OMSetBlendState(nullptr, nullptr, 0xffffffffu);
    ++viewModelWeaponDrawCallCount_;
    viewModelWeaponDrawReason_ = "drawn";
  }
  return true;
}

void Renderer::setViewModelWeaponState(const ViewModelDrawState& state) {
  viewModelWeaponState_ = state;
  viewModelWeaponState_.fov = clampViewModelFov(state.fov);
}

} // namespace tf2::native
