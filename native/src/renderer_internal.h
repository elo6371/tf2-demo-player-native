#pragma once

// Renderer-internal definitions. These used to live in native_renderer.cpp's
// anonymous namespace; they are shared by the renderer's translation units and
// are deliberately not part of native_renderer.h, because nothing outside
// native/src may depend on the GPU-side vertex layouts.

#include <cstddef>
#include <cstdint>

#include <DirectXMath.h>

#include "world_material.h"

namespace tf2::native {

struct WorldVertex {
  float x, y, z;
  float r, g, b, a;
  float u, v;
  float nx, ny, nz;
  float lu = -1.0f;
  float lv = -1.0f;
  float blend = -1.0f;
  float blendModulate = 0.0f;
  // ---- T27 (2026-10-19): per-material feature inputs ---------------------
  // One semantic bit per material feature, because the whole world is a handful
  // of draw calls over atlases: there is no per-material constant to put these
  // in, and a per-vertex flag is what lets the pixel shader sample a material's
  // own normal/detail tile without a second draw. Bit 0 = `$bumpmap` tile, bit 1
  // = `$bumpmap2` tile, bit 2 = `$detail` tile. Set by the renderer from the
  // tiles it actually blitted, not from the VMT: a declared feature whose
  // texture did not resolve must not set a bit that makes the shader sample
  // the neutral filler and call it applied.
  // T101 adds bit 5 (32) = environment fog, and it is deliberately the same
  // kind of claim as the three above: set by the renderer only for vertices the
  // fogged pass actually uploaded, so "the frame is fogged" and "this vertex is
  // in the fogged set" stay two separate readings. The pass switch itself rides
  // the constant's w, not this bit -- see the shader's fog arm.
  float featureMask = 0.0f;
  // `$detailblendmode` and `$detailblendfactor`, as the SDK's TextureCombine
  // reads them (`lightmappedgeneric_ps2_3_x.h` -> `common_ps_fxc.h`).
  float detailMode = 0.0f;
  float detailFactor = 1.0f;
  // T27: the layer-2 diagnostic eligibility. The shader's `feature.w`. Kept
  // OUT of `featureMask` on purpose: setting bit 2 on a vertex whose bit 0 is
  // clear is not a state the shader can consume (layer 2 has no base normal),
  // and measured on this machine that state alone moved 38,538 px with the
  // layer-2 arm deleted -- the mechanism was not isolated, so the state is not
  // created. The flag lets the diagnostic use the base normal the frame already
  // computed without lying about the material's mask.
  float featureDiag = 0.0f;
  // The face's tangent frame: the BSP texinfo u axis, normalised, plus the
  // sign of the bitangent (`cross(N, T) * sign`). Four floats rather than six
  // because the bitangent is rebuilt from the interpolated normal, which is the
  // same vector the scalar lighting uses -- one normal, one frame.
  float tangentX = 0.0f;
  float tangentY = 0.0f;
  float tangentZ = 1.0f;
  float tangentSign = 1.0f;
};
// T2 (2026-10-14): the water pass reuses the world's input layout and vertex
// shader, which is only sound while the two vertex structs agree field for
// field. This is the assertion that says so at compile time rather than at the
// first blurred frame.
static_assert(sizeof(WorldWaterVertex) == sizeof(WorldVertex),
  "the water pass shares the world input layout, so the vertex structs must match");
static_assert(offsetof(WorldWaterVertex, u) == offsetof(WorldVertex, u)
  && offsetof(WorldWaterVertex, nx) == offsetof(WorldVertex, nx)
  && offsetof(WorldWaterVertex, lu) == offsetof(WorldVertex, lu)
  && offsetof(WorldWaterVertex, blend) == offsetof(WorldVertex, blend)
  && offsetof(WorldWaterVertex, blendModulate) == offsetof(WorldVertex, blendModulate)
  && offsetof(WorldWaterVertex, featureMask) == offsetof(WorldVertex, featureMask)
  && offsetof(WorldWaterVertex, detailMode) == offsetof(WorldVertex, detailMode)
  && offsetof(WorldWaterVertex, tangentX) == offsetof(WorldVertex, tangentX),
  "the water vertex's uv, normal, light-uv, blend and feature offsets must match the world's");
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
  // ---- T2 (2026-10-14): the advanced materials ----------------------------
  // The view matrix, so the vertex shader can hand the pixel shader a
  // view-space position and normal. Phong needs both: a specular lobe is a
  // function of the half-vector between the light and the eye, and computing it
  // from a world-space position would need a second matrix the pass does not
  // otherwise carry. The view matrix is uploaded once per draw and is the one
  // `mvp` was already built from, so the two cannot disagree about the camera.
  DirectX::XMFLOAT4X4 viewMatrix;
  // x = $phongexponent, y = $phongboost, z/w = $phongfresnelranges.
  DirectX::XMFLOAT4 phongParams;
  // $phongtint, or the engine default white when the key was absent.
  DirectX::XMFLOAT4 phongTint;
  // x = 1 when the phong branch should run, y = 0/1 for "no tint declared"
  // (so an absent tint is not applied as black), z/w unused.
  DirectX::XMFLOAT4 phongFeatures;
  // x = 1 when the bump branch should run, y = 1 when the material asked for
  // `$normalmapalphaenvmapmask`, z/w unused.
  DirectX::XMFLOAT4 bumpFeatures;
  // x = 1 when the selfillum branch should run, y = 1 when the mask is the
  // albedo's alpha rather than a separate texture, z/w unused.
  DirectX::XMFLOAT4 selfillumFeatures;
  // $selfillumtint, or the engine default white.
  DirectX::XMFLOAT4 selfillumTint;
};
static_assert(sizeof(ViewModelConstantsData) == 272, "viewmodel constant buffer size");
struct ModelGpuVertex {
  float x, y, z; float r, g, b, a; float u, v; float nx, ny, nz;
  float weights[3]; std::uint8_t boneIndices[3]; std::uint8_t boneCount; std::uint8_t padding[1];
};
template <typename T>
T clampValue(T value, T minimum, T maximum) {
  return value < minimum ? minimum : (value > maximum ? maximum : value);
}

} // namespace tf2::native
