#include "native_renderer.h"
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
struct Input { float3 position : POSITION; float4 colour : COLOR0; float2 uv : TEXCOORD0; float3 normal : NORMAL0; float2 lightUv : TEXCOORD1; float blend : TEXCOORD4; float blendModulate : TEXCOORD5; float4 feature : TEXCOORD6; float4 tangent : TEXCOORD7; };
struct Output { float4 position : SV_POSITION; float4 colour : COLOR0; float2 uv : TEXCOORD0; float3 normal : NORMAL0; float3 worldPosition : TEXCOORD2; float2 lightUv : TEXCOORD3; float blend : TEXCOORD4; float blendModulate : TEXCOORD5; float4 feature : TEXCOORD6; float4 tangent : TEXCOORD7; };
Output main(Input input) { Output output; output.position = mul(float4(input.position, 1.0), mvp); output.colour = input.colour; output.uv = input.uv; output.normal = input.normal; output.worldPosition = input.position; output.lightUv = input.lightUv; output.blend = input.blend; output.blendModulate = input.blendModulate; output.feature = input.feature; output.tangent = input.tangent; return output; }
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
// T12: the second layer of a WorldVertexTransition material and its
// `$blendmodulatetexture`. Both atlases carry the base atlas's tile
// rectangles, so the vertex's single uv addresses the same texel in all three.
Texture2D blendTexture : register(t6);
Texture2D blendModulateTexture : register(t7);
// T27: the per-material feature atlases. Each batch owns one of each, laid out
// on the SAME tile rectangles as the albedo atlas it is drawn with, so the one
// uv the vertex carries addresses the material's own normal/detail texel in all
// of them. That is the whole reason a second sampler slot is enough: the tiles
// agree about where a texel is, which is what makes a per-material normal map
// possible without a per-material draw.
Texture2D bumpAtlas : register(t8);
Texture2D bumpAtlas2 : register(t9);
Texture2D detailAtlas : register(t10);
SamplerState sampler0 : register(s0);
float3 safeNormalize(float3 value) {
  float lengthSquared = dot(value, value);
  if (!(lengthSquared > 1e-8) || !(lengthSquared < 1e8)) return float3(0.0, 0.0, 0.0);
  return value * rsqrt(lengthSquared);
}
// T27: `$detail`'s TextureCombine, transcribed from
// common_ps_fxc.h:701-739. The mode comes from `$detailblendmode` and the
// factor from `$detailblendfactor`; both ride the vertex because the pass has no
// per-material constant. Modes the product does not draw are refused on the CPU
// and never reach here, so this switch has no "default: guess" arm.
float3 combineDetail(float3 baseColor, float4 detailColor, uint mode, float factor) {
  if (mode == 0u) return baseColor * lerp(float3(1.0, 1.0, 1.0), 2.0 * detailColor.rgb, factor);
  if (mode == 1u) return baseColor + factor * detailColor.rgb;
  if (mode == 2u) return lerp(baseColor, detailColor.rgb, saturate(factor * detailColor.a));
  if (mode == 3u) return lerp(baseColor, detailColor.rgb, factor);
  if (mode == 8u) return lerp(baseColor, baseColor * detailColor.rgb, factor);
  return baseColor;
}
// The normal map decode: NORM_DECODE_NONE (common_ps_fxc.h), i.e.
// `texel.rgb * 2 - 1`. This is the decode `lightmappedgeneric_ps2_3_x.h` applies
// under `#if BUMPMAP == 1`; `$ssbump` is `BUMPMAP == 2`, which deliberately does
// NOT do it -- an SSBump texel stays 0..1 and is consumed as weights on
// `bumpBasis[0..2]` against three lightmap directions. This pass has neither the
// basis nor the three lightmaps, so SSBump materials are refused by name on the
// CPU and never reach this function.
float3 decodeNormal(float3 texel) { return safeNormalize(texel * 2.0 - 1.0); }
// The tangent frame, from the BSP texinfo axes the vertex carries. N is the
// geometric normal; T is the texture's u axis with N removed; B is
// cross(N, T) * sign, the sign that makes B point along increasing v. A
// degenerate T falls back to an identity frame, which makes the bump map a
// no-op on that triangle instead of a NaN.
float3 tangentNormal(float3 texel, float3 Ng, float4 tangent) {
  float3 T = float3(tangent.x, tangent.y, tangent.z);
  T = T - Ng * dot(Ng, T);
  T = safeNormalize(T);
  if (dot(T, T) < 0.5) return Ng;
  float3 B = cross(Ng, T) * (tangent.w < 0.0 ? -1.0 : 1.0);
  float3 n = decodeNormal(texel);
  return safeNormalize(T * n.x + B * n.y + Ng * n.z);
}
float4 main(float4 position : SV_POSITION, float4 colour : COLOR0, float2 uv : TEXCOORD0, float3 normal : NORMAL0, float3 worldPosition : TEXCOORD2, float2 lightUv : TEXCOORD3, float blend : TEXCOORD4, float blendModulate : TEXCOORD5, float4 feature : TEXCOORD6, float4 tangent : TEXCOORD7) : SV_TARGET {
  // Mode 3 is the sky box, and it short-circuits: the box is lit by nothing,
  // carries no lightmap uv and has no meaningful normal, so running the
  // lighting half of this shader over it would be inventing a direction to
  // shade by. The direction a texel of the box looks up is the direction from
  // the camera to it, which is why the box's vertices are built around the
  // origin and the pass drops the view matrix's translation.
  // T61 diagnostic: the two PS-side terms that can zero the albedo on the
  // entity pass, read together. R = the blend weight whose TEXCOORD4 the
  // entity vertex shader never writes (a constant >= 1 makes saturate() 1,
  // so the single-layer lerp reads the unbound t6 and yields 0); B = the
  // detail bit in feature.x (TEXCOORD6, also unwritten). This arm sits above
  // the uv view because that view is open ended (x > 7.5).
  // T61 diagnostic: the ENTITY COVERAGE mask. The entity pass returns an
  // exact sentinel and every other pass returns something else, so 'this
  // pixel is an entity pixel' is a measured mask rather than a hand-drawn
  // box. The product path cannot produce it: the world view for this mode
  // is the flat arm (0.35,0.62,0.38), and this arm is the only one that
  // returns a saturated magenta.
  // T61.1 diagnostic: WHICH entity, not merely whether. The coverage arm
  // below answers "is this pixel an entity" with one sentinel, so two
  // characters are indistinguishable and a sequence resolved for one
  // model can be carried by another with no reading to show it. This arm
  // sits ABOVE the sentinel because the entity pass hands it 11.0 and
  // 11.0 > 9.5: an arm placed below would be unreachable.
  if (envFeatures.x > 10.5) {
    return float4(colour.rgb, 1.0);
  }
  if (envFeatures.x > 9.5) {
    return float4(1.0, 0.0, 1.0, 1.0);
  }
  if (envFeatures.x > 8.5) {
    uint entityDetail = ((uint)feature.x) & 4u;
    return float4(saturate(blend), saturate(blendModulate),
      entityDetail != 0u ? 1.0 : 0.0, 1.0);
  }
  // T61 diagnostic: the texture coordinate the rasterizer interpolated, as a
  // colour. A body that samples black can then be told apart from a body whose
  // uvs are the same value everywhere, which is the other way this fails.
  if (envFeatures.x > 7.5) {
    return float4(frac(uv), 0.0, 1.0);
  }
  if (envFeatures.x > 2.5 && envFeatures.x < 3.5) {
    return float4(envCube.Sample(sampler0, safeNormalize(worldPosition)).rgb, 1.0);
  }
  // T11 diagnostic modes. 4 = one flat colour with no texture, no lightmap,
  // no bump and no env sample -- geometry and nothing else, so a hole in the
  // frame is a triangle that is not there rather than a triangle that is dark.
  // 5 = the interpolated depth as grey -- the same geometry through the same
  // matrices with only the depth question visible. Neither mode exists on the
  // product path; the renderer states which one it is and the trace prints it.
  if (envFeatures.x > 3.5) {
    // T26: mode 6 is the face-id frame. `$colour` carries the dface_t index,
    // written at upload time and identical on all three vertices, so the
    // interpolated value is the id itself. Reading it here is what lets a pixel
    // be attributed to a face instead of to a colour.
    if (envFeatures.x > 5.5) return float4(colour.rgb, 1.0);
    float depthZ = position.z / max(position.w, 1.0e-4);
    if (envFeatures.x > 4.5) return float4(depthZ, depthZ, depthZ, 1.0);
    return float4(0.35, 0.62, 0.38, 1.0);
  }
  float3 sampled = texture0.Sample(sampler0, uv).rgb;
  // T61 diagnostic: the raw t0 sample for this pixel's uv, before the blend
  // layer, the lightmap and the lighting. This is the value the GPU read.
  if (envFeatures.x > 0.5 && envFeatures.x < 1.5) {
    return float4(sampled, 1.0);
  }
)HLSL"
    // MSVC caps a single string literal at about 16 KB and truncates the
    // tail silently as C2026 ("string too large, trailing characters
    // truncated"). The T61.1 diagnostic arm pushed this shader past that
    // cap, so it is carried as adjacent literals that the compiler
    // concatenates. The split is load-bearing: joining the two literals
    // back into one does not compile.
    R"HLSL(  // T12: WorldVertexTransition. Source's LightmappedGeneric pixel shader picks
  // between `$basetexture` and `$basetexture2` with the vertex alpha, and
  // reshapes that weight with `smoothstep(g - r, g + r, w)` when the material
  // declares `$blendmodulatetexture`
  // (source-sdk-2013 src/materialsystem/stdshaders/lightmappedgeneric_ps2_3_x.h,
  // `blendfactor` and the `FANCY_BLENDING` block). A negative weight is the
  // sentinel for a single-layer vertex, so a material without a second layer
  // never samples the blend atlas.
  //
  // T27: the SAME weight is what `$bumpmap2` is lerped by, because Source has one
  // `blendfactor` variable and the BUMPMAP2 block reads it after the modulate
  // has rewritten it. Computing it once here is what keeps the albedo layer and
  // the normal layer from disagreeing about where the boundary is.
  float blendWeight = 0.0f;
  if (blend >= 0.0) {
    float weight = blend;
    if (blendModulate > 0.5) {
      float4 mask = blendModulateTexture.Sample(sampler0, uv);
      float lower = saturate(mask.g - mask.r);
      float upper = saturate(mask.g + mask.r);
      // smoothstep with a zero-width range is undefined in HLSL and would turn
      // a mask texel into a NaN; the floor keeps the curve and keeps the frame
      // a picture of the map.
      weight = smoothstep(lower, lower + max(upper - lower, 1.0e-5), weight);
    }
    blendWeight = saturate(weight);
    sampled = lerp(sampled, blendTexture.Sample(sampler0, uv).rgb, blendWeight);
  }
  float lightmapFactor = max(lightmapFeatures.x, 0.0);
  float3 albedo = lerp(colour.rgb, sampled * colour.rgb, colour.a);
  // T27: `$detail`. The layer is combined with the SAME TextureCombine the SDK
  // uses (common_ps_fxc.h), at the point the SDK calls it: on the base colour
  // with the vertex tint already applied, before lighting (`albedo =
  // TextureCombine(albedo, detailColor, DETAIL_BLEND_MODE,
  // g_DetailBlendFactor)`). `$detailscale` is baked into the detail tile on the
  // CPU -- this pass has one atlas uv per vertex, so the repetition has to live
  // in the pixels rather than in a second texcoord. A material with no detail
  // tile has bit 2 clear and never samples here.
  uint featureMask = (uint)feature.x;
  if ((featureMask & 4u) != 0u) {
    float4 detailColor = detailAtlas.Sample(sampler0, uv);
    albedo = combineDetail(albedo, detailColor, (uint)feature.y, feature.z);
  }
  // T21: mode 6 is the ALBEDO the material path produced -- both layers of a
  // blend material already mixed, the detail layer combined, the flat branch for
  // a material with no tile, and nothing else. Lighting, the lightmap and
  // self-illum are deliberately NOT applied: this view exists so a gate can
  // compare one screen pixel with one texel the CPU sampled from the same tile,
  // which is the only way to tell "the wall is dark because the material is
  // dark" from "the wall is dark because it sampled the wrong tile".
  if (envFeatures.y > 0.5) return float4(albedo, 1.0);
  float3 lightColour = float3(lightmapFactor, lightmapFactor, lightmapFactor);
  if (lightmapFeatures.w > 0.5 && lightUv.x >= 0.0) {
    // T17: RGBExp32 is LINEAR radiance, and the text writes what it sampled, so
    // the atlas holds the lightmap in linear space. Every other input here is
    // display-referred: the albedo textures are raw sRGB bytes and the output
    // target is an UNORM swap chain. Multiplying a display-space albedo by a
    // linear light and writing the product straight out is the "everything is
    // dark" failure: a shadowed wall whose lightmap is 0.1 lands at 25/255
    // instead of the 89/255 the same 0.1 looks like on screen.
    //
    // pow(light, 1/2.2) is the sRGB encode. It is also exactly the
    // gamma-correct composition: (albedo_linear * light_linear)^(1/2.2)
    // = albedo_srgb * light_linear^(1/2.2), because the albedo is already
    // display-encoded. So the albedo needs no second conversion, and the
    // product is the same expression Source's output stage produces through its
    // gamma ramp.
    lightColour = pow(max(lightmapTexture.Sample(sampler0, lightUv).rgb, 0.0), 1.0 / 2.2);
  }
  albedo *= lightColour;
  float3 Ng = safeNormalize(normal);
  // T27: the per-material normal. Bit 0 means this material's own `$bumpmap`
  // tile was blitted into the batch's bump atlas; the vertex's tangent frame
  // comes from the face's texinfo axes, so the map's x/y mean what the material
  // author made them mean. When the material has no tile the legacy single-slot
  // branch still runs, which is what T24's diagnostic switch measures -- the two
  // are separate paths on purpose, and the trace names which one a frame used.
  //
  // Both candidate normals are computed as VALUES and chosen with `?:`. Not
  // for speed: measured on this machine, a vertex whose mask carried bit 2
  // without bit 1 changed 38,538 px even with the layer-2 arm deleted, and it
  // still did after the branch writes below were turned into these selects --
  // so the state itself, not any one write, moved the frame. The mechanism was
  // not isolated this round; what was fixed is that the state is no longer
  // created. The product path only sets bit 2 when bit 0 is set (see the vertex
  // upload), and the diagnostic rides `featureDiag`, which no shader arm
  // mistakes for a layer the material asked for.
  const bool hasLayer1 = (featureMask & 1u) != 0u;
  float3 nMaterial = tangentNormal(bumpAtlas.Sample(sampler0, uv).xyz, Ng, tangent);
  float3 bump = bumpTexture.Sample(sampler0, uv).xyz * 2.0 - 1.0;
  float3 nLegacy = lightmapFeatures.y > 0.5
    ? safeNormalize(Ng + float3(bump.xy * 0.35, bump.z * 0.1)) : Ng;
  float3 n = hasLayer1 ? nMaterial : nLegacy;
  // `$bumpmap2`: layer 2's normal, lerped into the base normal by the same
  // `blendfactor` that selects the albedo layer (SDK: BUMPMAP2 block,
  // `vNormal.xyz = lerp(vNormal.xyz, vNormal2.xyz, blendfactor)`). `blendWeight`
  // is 0 for a vertex with no second layer, which leaves the base alone -- the
  // reading a face with no painted weight should give.
  //
  // T27: the product path requires layer 1 (`hasLayer1`): Source compiles
  // BUMPMAP2 under BUMPMAP, so a material whose layer 1 is `$ssbump` (refused
  // above) has no normal to lerp toward and its bit is never set. `feature.w`
  // is the diagnostic eligibility -- set only by --world-bump2-texture, and only
  // for a material that declares layer 2 -- so the diagnostic never masquerades
  // as a mask bit the shader would read as "this material asked for layer 2
  // with a layer-1 base". `envFeatures.z` is the same switch on the pass side:
  // it lets the diagnostic lerp onto the base normal the frame already computed,
  // and the trace names that base. Both layers are decoded in the SAME geometric
  // frame: layer 2's texel is a normal on this surface, not on the surface layer
  // 1 already tilted.
  // Bit 1 is the product path (layer 1 present); `feature.w` is the diagnostic
  // eligibility, set only by --world-bump2-texture and only for a material that
  // declares layer 2. The diagnostic flag does not masquerade as a mask bit.
  const bool useLayer2 = ((featureMask & 2u) != 0u || feature.w > 0.5)
    && (hasLayer1 || envFeatures.z > 0.5);
  float3 n2 = tangentNormal(bumpAtlas2.Sample(sampler0, uv).xyz, Ng, tangent);
  n = useLayer2 ? safeNormalize(lerp(n, n2, blendWeight)) : n;
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
  // The line/marker half of the same pass. It is a second, tiny program rather
  // than a branch of the silhouette one because the two read different vertex
  // streams: a silhouette is a model mesh with per-instance placement rows, a
  // trail is a plain world-space position with a colour.
  static constexpr char overlayLineVertexSource[] = R"HLSL(
cbuffer OverlayConstants : register(b0) {
  row_major float4x4 mvp;
  float4 cameraPositionAndExpand;
  float4 mode;
};
struct Input { float3 position : POSITION; float4 colour : COLOR0; };
struct Output { float4 position : SV_POSITION; float4 colour : COLOR0; };
Output main(Input input) {
  Output output;
  output.position = mul(float4(input.position, 1.0), mvp);
  output.colour = input.colour;
  return output;
}
)HLSL";

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
  static constexpr char overlayVertexSource[] = R"HLSL(
cbuffer OverlayConstants : register(b0) {
  row_major float4x4 mvp;
  float4 cameraPositionAndExpand;
  float4 mode;
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
StructuredBuffer<float4x4> boneMatrices : register(t4);
struct Output {
  float4 position : SV_POSITION;
  float4 colour : COLOR0;
};
Output main(Input input) {
  Output output;
  float4 local = float4(input.position, 1.0);
  float3 normalLocal = input.normal;
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
  float3 rotated = float3(dot(input.nrow0.xyz, normalLocal), dot(input.nrow1.xyz, normalLocal), dot(input.nrow2.xyz, normalLocal));
  float lengthSquared = dot(rotated, rotated);
  float3 unitNormal = (lengthSquared > 1e-8 && lengthSquared < 1e8) ? rotated * rsqrt(lengthSquared) : float3(0.0, 0.0, 0.0);
  // The expansion that turns the mesh into an outline, in CLIP SPACE so that the
  // unit is a pixel: at this camera a player is ~4 px tall, so a world- or
  // model-space constant is either a 2x blob or invisible (both measured; see
  // kOverlaySilhouetteExpandPixels).
  //   cameraPositionAndExpand.w = thickness in pixels
  //   mode.yz                   = 2 / viewportWidth, 2 / viewportHeight (NDC per pixel)
  //   mode.x                    = 0 for the line/marker path (a trail must not grow)
  float4 clip = mul(float4(mapped, 1.0), mvp);
  // The screen-space direction the vertex should move: the world normal taken
  // through the projection's linear part. Normalised in 2D, so the shell is as
  // thick everywhere instead of stretching with the surface angle.
  float3 clipNormal = float3(dot(rotated, mvp[0].xyz), dot(rotated, mvp[1].xyz),
    dot(rotated, mvp[2].xyz));
  float2 normalPixels = clipNormal.xy;
  float normalLength = length(normalPixels);
  normalPixels = (normalLength > 1e-6) ? normalPixels / normalLength : float2(0.0, 0.0);
  clip.xy += normalPixels * float2(mode.y, mode.z) * clip.w
    * cameraPositionAndExpand.w * mode.x;
  output.position = clip;
  output.colour = input.colour;
  return output;
}
)HLSL";
  // Flat colour, no texture, no lighting. An overlay pixel must be the team
  // colour the reading names; anything the shader did to it would make the
  // pixel predicate a statement about the shader instead.
  static constexpr char overlayPixelSource[] = R"HLSL(
float4 main(float4 position : SV_POSITION, float4 colour : COLOR0) : SV_TARGET {
  return colour;
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
  // T61: the world pixel shader reads four more interpolants than this
  // shader used to write: TEXCOORD4 (blend), 5 (blendModulate), 6
  // (feature) and 7 (tangent). Leaving them undeclared is not a compile
  // error and does not fall back to zero -- an input the vertex shader
  // never writes is undefined, and on this driver it reads as 1.0. A
  // blend weight of 1.0 means the vertex is fully the second layer, so
  // sampled = lerp(sampled, t6_sample, 1.0) replaced every entity texel
  // with the blend atlas -- which this pass never binds -- and the albedo
  // lerp then produced exactly (0,0,0) over the whole player. Measured
  // with --world-entity-blend: all 10845 body pixels read blend=1,
  // blendModulate=1, detail bit 0.
  //
  // Declaring them and writing the neutral value is the fix: -1 is the
  // single-layer sentinel the world shader also uses, so blend >= 0.0 is
  // false and the blend atlas is never sampled, and a zero feature mask
  // means no detail tile.
  float blend : TEXCOORD4;
  float blendModulate : TEXCOORD5;
  float4 feature : TEXCOORD6;
  float4 tangent : TEXCOORD7;
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
  // T61: the neutral values, written explicitly. See the Output struct
  // for what leaving these unwritten did to the frame.
  output.blend = -1.0;
  output.blendModulate = 0.0;
  output.feature = float4(0.0, 0.0, 0.0, 0.0);
  output.tangent = float4(1.0, 0.0, 0.0, 0.0);
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
  row_major float4x4 viewMatrix;
  float4 phongParams;
  float4 phongTint;
  float4 phongFeatures;
  float4 bumpFeatures;
  float4 selfillumFeatures;
  float4 selfillumTint;
};
// T18 (2026-10-19): the bone table is read as FLOAT4s, not as `float4x4`.
//
// `StructuredBuffer<float4x4>` was the original declaration and it was wrong in
// a way no reading in this project could see. HLSL packs matrices column-major
// by default (/Zpc), structured-buffer elements included, and `row_major` is
// rejected on a buffer declaration. The matrices uploaded here come from
// `toColumnMajor`, which stores the transpose of the Source `matrix3x4_t` so that
// `mul(float4(p,1), M)` reproduces `M_source * p`. Read as column-major, the same
// 16 floats describe the transpose of that matrix: the rotation is inverted and
// the translation is read from the wrong four floats -- which are zero here.
//
// Nothing caught it because an identity matrix is its own transpose. The
// identity-bones instrument uploads exactly that, so its byte-identical frame
// pair proved the branch runs, not that it reads what the uploader wrote. What a
// real pose does through the transposed read is rotate every vertex about the
// MODEL origin instead of about its bone pivot: the arms model's bones sit ~50
// units out, so vertices swing tens of units and the hand's triangles are
// stretched across the whole frame.
//
// Four float4s per bone remove the question rather than answering it: `float4`
// has one packing and indexing it by hand cannot disagree with the uploader.
StructuredBuffer<float4> viewModelBones : register(t4);
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
  // T2 (2026-10-14): the view-space position and normal, which is what the
  // Phong half-vector needs. Both are computed from the same `viewMatrix` the
  // pass uploads, so the highlight is a function of the camera that actually
  // drew the frame rather than of a second camera reconstructed in the shader.
  float3 viewPosition : TEXCOORD1;
  float3 viewNormal : TEXCOORD2;
};
// One bone's matrix, assembled from the four float4s the uploader wrote.
// Column k of the Source `matrix3x4_t` is `viewModelBones[slot*4 + k].xyz`, and
// its fourth component is that column's translation term -- the `toColumnMajor`
// layout exactly, with no packing rule left to interpret.
//
// `point`, `base` and `line` are HLSL keywords (legacy geometry-shader primitive
// types and the class-inheritance keyword); using one as a parameter name makes
// the whole parameter list fail to parse, which surfaces as
// "undeclared identifier" on the *next* line rather than at the mistake.
float4 skinByBone(float3 vertexPosition, float weightW, uint boneSlot) {
  uint first = boneSlot * 4;
  float4 c0 = viewModelBones[first + 0];
  float4 c1 = viewModelBones[first + 1];
  float4 c2 = viewModelBones[first + 2];
  float4 c3 = viewModelBones[first + 3];
  return float4(
    vertexPosition.x * c0.x + vertexPosition.y * c1.x + vertexPosition.z * c2.x + weightW * c3.x,
    vertexPosition.x * c0.y + vertexPosition.y * c1.y + vertexPosition.z * c2.y + weightW * c3.y,
    vertexPosition.x * c0.z + vertexPosition.y * c1.z + vertexPosition.z * c2.z + weightW * c3.z,
    weightW);
}
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
          skinnedPos += skinByBone(input.position, 1.0, input.boneIndices[i]).xyz * normalizedWeights[i];
          skinnedNormal += skinByBone(input.normal, 0.0, input.boneIndices[i]).xyz * normalizedWeights[i];
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
  // The view-space pair, from the same view matrix `mvp` was built with. A
  // non-finite or zero view matrix (the pass uploads identity when it has none)
  // leaves both at a value the pixel shader's safe-normalise refuses, so the
  // highlight degrades rather than producing a NaN that paints the whole model.
  output.viewPosition = mul(local, viewMatrix).xyz;
  output.viewNormal = mul(float4(normalLocal, 0.0), viewMatrix).xyz;
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
  row_major float4x4 viewMatrix;
  float4 phongParams;
  float4 phongTint;
  float4 phongFeatures;
  float4 bumpFeatures;
  float4 selfillumFeatures;
  float4 selfillumTint;
};
Texture2D albedo : register(t0);
// t1 is the normal map and t2 the selfillum mask. They are separate slots from
// the albedo on purpose: the T2 rule is that a normal map must never be
// uploaded as the base texture, and one shared slot is exactly how that
// happens. The mask is a third slot for the same reason.
Texture2D bumpTexture : register(t1);
Texture2D selfillumMask : register(t2);
SamplerState albedoSampler : register(s0);
struct Input {
  float4 position : SV_POSITION;
  float3 normal : NORMAL0;
  float2 uv : TEXCOORD0;
  float4 colour : COLOR0;
  float3 viewPosition : TEXCOORD1;
  float3 viewNormal : TEXCOORD2;
};
float3 safeNormalize(float3 value) {
  float lengthSquared = dot(value, value);
  if (!(lengthSquared > 1e-8) || !(lengthSquared < 1e8)) return float3(0.0, 0.0, 0.0);
  return value * rsqrt(lengthSquared);
}
float4 main(Input input) : SV_TARGET {
  // The world-space-ish normal the pass has always shaded by, kept as the
  // fallback so a frame with no advanced material draws exactly what it drew
  // before this round.
  float3 normal = normalize(input.normal);
  float3 lightDirection = normalize(float3(-0.30, 0.42, 0.86));
  float diffuse = saturate(dot(normal, lightDirection));
  float lighting = 0.55 + 0.45 * diffuse;
  if (material.x > 0.5) {
    float4 texel = albedo.Sample(albedoSampler, input.uv);
    // `$alphatest`: the material's own key, not a threshold invented here.
    if (material.y > 0.5 && texel.a < 0.5) discard;
    float3 albedoRgb = texel.rgb;

    // ---- Bump ----------------------------------------------------------
    // The normal map is sampled in tangent space and rotated into view space by
    // the interpolated view normal. There is no tangent basis in the mesh, so
    // the rotation is approximate -- and the approximation is stated rather
    // than dressed up: the branch changes the lighting, which is what makes it
    // falsifiable, but it is not a full TBN transform and the reading says so.
    float3 shadedNormal = safeNormalize(input.viewNormal);
    if (bumpFeatures.x > 0.5) {
      float3 tangentNormal = bumpTexture.Sample(albedoSampler, input.uv).xyz * 2.0 - 1.0;
      // The perturbed normal: the interpolated normal tilted by the map. The
      // magnitude is bounded so a garbage normal map cannot flip the surface.
      shadedNormal = safeNormalize(shadedNormal + tangentNormal * 0.5);
      if (dot(shadedNormal, shadedNormal) < 1e-6) shadedNormal = safeNormalize(input.viewNormal);
    }
    float3 viewLight = safeNormalize(mul(float4(lightDirection, 0.0), viewMatrix).xyz);
    float bumpDiffuse = saturate(dot(shadedNormal, viewLight));
    float bumpLighting = 0.55 + 0.45 * bumpDiffuse;

    // ---- Phong ---------------------------------------------------------
    // A specular lobe on the same view-space normal, using the half-vector
    // between the light and the eye. The exponent is the material's own and the
    // fresnel range scales it, so the two parameters are what the picture is a
    // function of: changing either moves the highlight, which is what the
    // fixture's exponent mutation measures.
    float3 specular = float3(0.0, 0.0, 0.0);
    if (phongFeatures.x > 0.5) {
      float3 viewDirection = safeNormalize(-input.viewPosition);
      float3 halfDirection = safeNormalize(viewLight + viewDirection);
      float exponent = max(phongParams.x, 1.0);
      float specularTerm = pow(saturate(dot(shadedNormal, halfDirection)), exponent);
      // The Fresnel range gates the lobe by the view angle: a surface seen
      // face-on gets the low end, one seen edge-on the high end. A range whose
      // min exceeds its max was refused at parse time, so this is always a
      // well-ordered pair.
      float fresnelBase = saturate(1.0 - saturate(dot(shadedNormal, viewDirection)));
      float fresnel = lerp(phongParams.z, phongParams.w, fresnelBase);
      float strength = specularTerm * phongParams.y * fresnel;
      // `phongFeatures.y == 0` means the material declared no $phongtint, and
      // an absent tint is the engine's white rather than black -- applying
      // black would delete the highlight and look like a working shader.
      float3 tintColour = phongFeatures.y > 0.5 ? phongTint.rgb : float3(1.0, 1.0, 1.0);
      specular = tintColour * strength;
    }

    // ---- Selfillum -----------------------------------------------------
    // The mask is a separate texture when the material named one, and the
    // albedo's alpha when it did not -- Source's own default, stated rather
    // than left to a missing file read as zero. The result is added to the
    // shaded colour so a self-illuminated surface is bright where the mask is
    // bright, which is the whole point of the effect.
    float3 emission = float3(0.0, 0.0, 0.0);
    if (selfillumFeatures.x > 0.5) {
      float mask = selfillumFeatures.y > 0.5
        ? texel.a
        : selfillumMask.Sample(albedoSampler, input.uv).r;
      emission = albedoRgb * selfillumTint.rgb * saturate(mask);
    }

    float3 lit = albedoRgb * bumpLighting;
    lit += specular * albedoRgb;
    lit += emission;
    return float4(lit, texel.a * input.colour.a);
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
  static constexpr char hudVertexSource[] = R"HLSL(
cbuffer HudConstants : register(b0) {
  float4 screenSize;   // xy = back-buffer pixels, zw = 1/size
};
struct Input {
  float2 position : POSITION;
  float4 colour : COLOR0;
  float2 rect : TEXCOORD0;
  float2 uv : TEXCOORD1;
  float kind : TEXCOORD2;
};
struct Output {
  float4 position : SV_POSITION;
  float4 colour : COLOR0;
  float2 local : TEXCOORD0;
  float2 uv : TEXCOORD1;
  float kind : TEXCOORD2;
};
Output main(Input input) {
  Output output;
  // top-left origin -> clip space. The y flip is here and only here.
  const float2 ndc = float2(input.position.x * screenSize.z * 2.0 - 1.0,
                            1.0 - input.position.y * screenSize.w * 2.0);
  output.position = float4(ndc, 0.0, 1.0);
  output.colour = input.colour;
  output.local = input.rect;
  output.uv = input.uv;
  output.kind = input.kind;
  return output;
})HLSL";
  static constexpr char hudPixelSource[] = R"HLSL(
// T67: one shader, four paths, chosen by the per-vertex kind so the whole HUD
// stays a single draw call:
//   0 = bordered rect (T51's panel block)
//   1 = filled rect
//   2 = text: sample the embedded 8x8 R8 atlas. Coverage below 0.5 is discarded,
//       so a glyph pixel is either the table's pixel or nothing -- never a blend
//       of the panel behind it, which is what makes a frame comparison exact.
//   3 = circle: the 0..1 local coordinate spans the bounding box, so a squared
//       distance from the centre above 0.25 is outside the disc.
Texture2D<float> fontAtlas : register(t0);
SamplerState fontSampler : register(s0);
struct Input {
  float4 position : SV_POSITION;
  float4 colour : COLOR0;
  float2 local : TEXCOORD0;
  float2 uv : TEXCOORD1;
  float kind : TEXCOORD2;
};
float4 main(Input input) : SV_TARGET {
  const int kind = (int)(input.kind + 0.5);
  if (kind == 2) {
    const float coverage = fontAtlas.Sample(fontSampler, input.uv);
    if (coverage < 0.5) discard;
    return input.colour;
  }
  if (kind == 3) {
    const float2 offset = input.local - 0.5;
    if (dot(offset, offset) > 0.25) discard;
    return input.colour;
  }
  if (kind == 1) return input.colour;
  const float edge = min(min(input.local.x, input.local.y),
                         min(1.0 - input.local.x, 1.0 - input.local.y));
  const float border = edge < 0.035 ? 1.0 : 0.0;
  float4 colour = input.colour;
  colour.rgb = lerp(colour.rgb * 0.35, colour.rgb, border);
  return colour;
})HLSL";
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

bool Renderer::uploadTexture(const std::vector<std::uint8_t>& rgba, UINT width, UINT height) {
  std::string error;
  if (!device_ || !texture_.create(device_.Get(), rgba, width, height, &error)) {
    if (!error.empty()) OutputDebugStringA(("Texture2D upload failed: " + error + "\n").c_str());
    lastError_ = E_INVALIDARG;
    return false;
  }
  return true;
}

// T26: the face-id frame's encoding, in one place. $colour is unused by the id
// mode, so it carries the BSP face index: 8 bits per channel, and every vertex
// of a triangle carries the same value, so the interpolated result is exact and
// a decode can never be a blend of two faces.
static void writeWorldFaceId(float& r, float& g, float& b, float& a, std::int32_t faceIndex) {
  const std::uint32_t id = static_cast<std::uint32_t>(faceIndex);
  r = static_cast<float>(id & 0xFFu) / 255.0f;
  g = static_cast<float>((id >> 8) & 0xFFu) / 255.0f;
  b = static_cast<float>((id >> 16) & 0xFFu) / 255.0f;
  a = 1.0f;
}
bool Renderer::uploadWorldGeometry(const BspMap& map, const std::string& texturedMaterial, UINT textureWidth, UINT textureHeight) {
  if (!device_ || !map.valid || map.triangles.empty()) {
    lastError_ = E_INVALIDARG;
    return false;
  }
  // T26: `--world-geometry sky` reuses this overload -- one flat colour, no
  // atlas -- over `map.skyTriangles` instead of `map.triangles`. The bounds and
  // the transform still come from the world triangles, so the mask is projected
  // exactly where the world pass would have drawn the same map coordinates; a
  // mask built on its own bounds would be a picture of a different map.
  const std::vector<BspTriangle>& source = worldSkyFaceOnly_ ? map.skyTriangles : map.triangles;
  if (source.empty()) {
    lastError_ = E_INVALIDARG;
    return false;
  }
  // This overload draws the world with ONE material's texture. It is the
  // fallback, not the product path, so it says so where a reader will see it.
  worldAtlasReason_ = "the atlas overload was not used; the world was drawn with one material texture";
  worldAtlasSize_ = 0;
  worldAtlasBytes_ = 0;
  worldAtlasCount_ = 0;
  worldAtlasTotalBytes_ = 0;
  // T19: this path has no batches and no index buffer. Leaving the previous
  // upload's in place would make the draw take the batched branch against a
  // vertex buffer that was never laid out for it.
  worldAtlasTextures_.clear();
  worldBlendAtlasTextures_.clear();
  worldModulateAtlasTextures_.clear();
  worldDrawBatches_.clear();
  worldTriangleBatch_.clear();
  worldIndexBuffer_.Reset();
  worldIndexCount_ = 0;
  worldIndexCapacity_ = 0;
  worldTileTable_.clear();
  worldTilesTiled_ = 0;
  worldTilesRefused_ = 0;
  worldTilesRefusedMalformed_ = 0;
  worldTilesRefusedDuplicate_ = 0;
  worldTilesRefusedBudget_ = 0;
  worldAtlasBatches_ = 0;
  worldAtlasBlendBatches_ = 0;
  worldAtlasLayerBytes_ = 0;
  worldAtlasPlanReason_ = "one material texture, so no atlas plan was made";
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
  vertices.reserve(source.size() * 3);
  std::size_t lightmappedVertices = 0;
  std::size_t displacedTriangles = 0;
  std::size_t waterTriangles = 0;
  const auto convert = [&](const BspVertex& point, const BspVertex& normal) {
    return WorldVertex{(point.x - centerX) * horizontalScale, (point.y - centerY) * horizontalScale, (point.z - minZ) / spanZ * 0.8f + 0.1f, 0.62f, 0.65f, 0.68f, 1.0f, 0.0f, 0.0f, normal.x, normal.y, normal.z};
  };
  for (const auto& triangle : source) {
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
    // T59: the same mask the atlas overload applies, so --world-refused-mask means
    // one thing on both paths. Without this the flag painted 0 px whenever the
    // single-material overload was the one that uploaded (the sky pass and the
    // atlas-failed fallback), and a reading of 0 could not be told apart from
    // "nothing refused is on screen".
    if (worldRefusedMask_ && !textured) {
      a.r = b.r = c.r = 1.0f; a.g = b.g = c.g = 0.0f; a.b = b.b = c.b = 1.0f;
    }
    // T26: the face-id frame. `$colour` is unused by the id mode, so it carries
    // the face index instead. A helper because both upload overloads must agree:
    // the first version of this wrote the id only on the single-material path,
    // and the world capture -- which goes through the atlas overload -- came
    // back pure white, which decodes to face 16,777,215 and does not exist.
    if (worldFaceIdUpload_ && triangle.faceIndex >= 0) {
      writeWorldFaceId(a.r, a.g, a.b, a.a, triangle.faceIndex);
      writeWorldFaceId(b.r, b.g, b.b, b.a, triangle.faceIndex);
      writeWorldFaceId(c.r, c.g, c.b, c.a, triangle.faceIndex);
    }
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
  {
    float cx0 = 1e30f, cy0 = 1e30f, cz0 = 1e30f, cx1 = -1e30f, cy1 = -1e30f, cz1 = -1e30f;
    for (const auto& v : vertices) {
      cx0 = std::min(cx0, v.x); cy0 = std::min(cy0, v.y); cz0 = std::min(cz0, v.z);
      cx1 = std::max(cx1, v.x); cy1 = std::max(cy1, v.y); cz1 = std::max(cz1, v.z);
    }
    worldConvertedMin_[0] = cx0; worldConvertedMin_[1] = cy0; worldConvertedMin_[2] = cz0;
    worldConvertedMax_[0] = cx1; worldConvertedMax_[1] = cy1; worldConvertedMax_[2] = cz1;
  }
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
  // The tile size is the plan's (`WorldAtlasPlanOptions::tileSize`), not a
  // second constant kept here: two copies of 514 is one copy that can drift.
  worldBlendTexturesSeen_ = textures.size();
  worldBlendLayersSeen_ = 0;
  for (const auto& source : textures) if (!source.blendRgba.empty()) ++worldBlendLayersSeen_;
  // T27: every feature reading is written by this upload, never inherited: a
  // frame that did not build the atlas must not read the previous frame's
  // tile count as its own.
  worldBumpFeature_ = WorldFeatureState{};
  worldBump2Feature_ = WorldFeatureState{};
  worldDetailFeature_ = WorldFeatureState{};
  worldBumpFeature_.enabled = worldMaterialBumpEnabled_;
  worldBump2Feature_.enabled = worldMaterialBumpEnabled_ && worldBump2Enabled_;
  worldDetailFeature_.enabled = worldDetailEnabled_;
  // ---- T19: the plan, not a capped walk -----------------------------------
  // Before this, `accepted.size() >= kMaxTiles` skipped the material with a
  // `continue` and no counter: measured on cp_snakewater_final1 from the
  // reference frame, 51 of 148 materials were skipped that way while the trace
  // read `capped=51` and named none of them. planWorldTiles gives every source
  // an outcome -- a batch and a tile, or a refusal with a name -- and sizes
  // each atlas to the tiles it actually holds.
  std::vector<WorldTileSource> requests;
  requests.reserve(textures.size());
  for (const auto& source : textures) {
    WorldTileSource request;
    request.material = source.material;
    request.width = source.width;
    request.height = source.height;
    request.sourceWidth = source.sourceWidth > 0 ? source.sourceWidth : source.width;
    request.sourceHeight = source.sourceHeight > 0 ? source.sourceHeight : source.height;
    request.rgbaBytes = source.rgba.size();
    request.blendDeclared = source.blendDeclared;
    // Only a layer that was fetched enters the plan. `blendRgba` is empty both
    // when the switch withheld it and when it did not resolve, so the atlas a
    // `--world-blend off` run builds has the same rectangles as the on run --
    // which is what keeps the two frames differing by the shader branch alone.
    request.blendBytes = source.blendRgba.size();
    request.modulateBytes = source.blendModulateRgba.size();
    // T27: the two normal layers enter the plan only when their switch is on
    // and main.cpp actually fetched pixels. Both halves matter: with the switch
    // off the atlas must not exist (the default frame is byte-identical), and a
    // declared-but-unresolved `$bumpmap` must not price a texture it cannot
    // fill.
    request.bumpBytes = worldMaterialBumpEnabled_ ? source.bumpRgba.size() : 0;
    request.bump2Bytes = (worldMaterialBumpEnabled_ && worldBump2Enabled_)
      ? source.bump2Rgba.size() : 0;
    requests.push_back(std::move(request));
  }
  const WorldAtlasPlan plan = planWorldTiles(requests);
  worldAtlasPlanReason_ = plan.reason;
  worldTilesTiled_ = plan.accepted;
  worldTilesRefused_ = plan.refused;
  worldTilesRefusedMalformed_ = plan.refusedMalformed;
  worldTilesRefusedDuplicate_ = plan.refusedDuplicate;
  worldTilesRefusedBudget_ = plan.refusedBudget;
  worldAtlasBatches_ = plan.batches.size();
  worldAtlasLayerBytes_ = plan.albedoBytes + plan.blendBytes + plan.modulateBytes
    + plan.bumpBytes + plan.bump2Bytes;
  worldTileTable_.clear();
  worldTileTable_.reserve(textures.size());
  for (std::size_t index = 0; index < textures.size(); ++index) {
    WorldTileReading reading;
    reading.material = textures[index].material;
    reading.batch = plan.slots[index].batch;
    reading.tile = plan.slots[index].tile;
    reading.width = static_cast<int>(plan.slots[index].width);
    reading.height = static_cast<int>(plan.slots[index].height);
    reading.x = static_cast<int>(plan.slots[index].x);
    reading.y = static_cast<int>(plan.slots[index].y);
    reading.sourceWidth = static_cast<int>(plan.slots[index].sourceWidth);
    reading.sourceHeight = static_cast<int>(plan.slots[index].sourceHeight);
    if (plan.slots[index].batch >= 0) {
      const auto& placed = plan.batches[static_cast<std::size_t>(plan.slots[index].batch)];
      reading.atlasWidth = static_cast<int>(placed.width);
      reading.atlasHeight = static_cast<int>(placed.height);
    }
    reading.blend = plan.slots[index].blend;
    reading.modulate = plan.slots[index].modulate;
    reading.reason = plan.slots[index].batch >= 0
      ? "tiled" : worldTileRefusalName(plan.refusals[index]);
    worldTileTable_.push_back(std::move(reading));
  }
  worldBlendAccepted_ = plan.accepted;
  worldBlendAcceptedLayers_ = 0;
  for (const auto& slot : plan.slots) if (slot.batch >= 0 && slot.blend) ++worldBlendAcceptedLayers_;
  if (plan.accepted == 0) {
    // Nothing to bind. The caller falls back to the single material texture,
    // and the reason says which refusal emptied the plan rather than "upload
    // failed".
    worldAtlasReason_ = plan.reason;
    worldAtlasSize_ = 0;
    worldAtlasBytes_ = 0;
    worldAtlasCount_ = 0;
    worldAtlasTotalBytes_ = 0;
    lastError_ = HRESULT_FROM_WIN32(ERROR_NO_DATA);
    return false;
  }

  // One CPU-side buffer per batch and layer, painted tile by tile. The batch's
  // own width is the row stride: a batch of 20 tiles is 5x4, and using batch
  // 0's 8x8 stride for it would write past the end.
  std::vector<std::vector<std::uint8_t>> atlasPixels(plan.batches.size());
  std::vector<std::vector<std::uint8_t>> blendPixels(plan.batches.size());
  std::vector<std::vector<std::uint8_t>> modulatePixels(plan.batches.size());
  // T27: the two normal-map layers. Their fill is the neutral normal
  // (128,128,255) rather than the albedo fill (88): a texel a material did not
  // write must decode to "straight up", so a stray sample is a no-op instead of
  // a plausible-looking tilt that a frame would show as shading.
  std::vector<std::vector<std::uint8_t>> bumpPixels(plan.batches.size());
  std::vector<std::vector<std::uint8_t>> bump2Pixels(plan.batches.size());
  // The detail layer's fill is opaque black. No vertex samples it -- bit 2 of
  // the mask is set from the same set of materials that was blitted here -- and
  // black is chosen so that a regression which sampled the fill anyway would
  // show as a crushed surface rather than as a plausible one: the modulate,
  // additive and multiply branches all collapse toward 0 on it.
  std::vector<std::vector<std::uint8_t>> detailPixels(plan.batches.size());
  const auto makeNeutral = [](std::size_t bytes, std::uint8_t r, std::uint8_t g,
                              std::uint8_t b) {
    std::vector<std::uint8_t> pixels(bytes, 0);
    for (std::size_t at = 0; at + 3 < pixels.size(); at += 4u) {
      pixels[at + 0] = r; pixels[at + 1] = g; pixels[at + 2] = b; pixels[at + 3] = 255u;
    }
    return pixels;
  };
  for (std::size_t batch = 0; batch < plan.batches.size(); ++batch) {
    atlasPixels[batch].assign(static_cast<std::size_t>(plan.batches[batch].albedoBytes), worldAtlasFill);
  }
  std::unordered_set<std::string> blendedMaterials;
  std::unordered_set<std::string> modulatedMaterials;
  std::unordered_set<std::string> terrainMaterials;
  std::unordered_set<std::string> bumpMaterials;
  std::unordered_set<std::string> bump2Materials;
  std::unordered_set<std::string> detailBlitMaterials;
  std::size_t blendTiles = 0, blendModulateTiles = 0, malformedLayers = 0;
  for (std::size_t index = 0; index < textures.size(); ++index) {
    const WorldTileSlot& slot = plan.slots[index];
    if (slot.batch < 0) continue;
    const auto& source = textures[index];
    const auto& batch = plan.batches[static_cast<std::size_t>(slot.batch)];
    const std::size_t expected = static_cast<std::size_t>(slot.width) * slot.height * 4u;
    worldTileBlit(atlasPixels[slot.batch], batch.width, slot.x, slot.y, slot.width, slot.height,
      source.rgba);
    // A layer whose byte count does not match its tile is refused rather than
    // sampled: reading past the end of it would paint whatever the allocator
    // left there, and that picture would look like a finding.
    if (source.blendDeclared) terrainMaterials.insert(source.material);
    const bool hasBlend = worldBlendEnabled_ && !source.blendRgba.empty();
    if (hasBlend && source.blendRgba.size() != expected) {
      ++malformedLayers;
    } else if (hasBlend) {
      if (blendPixels[slot.batch].empty()) {
        blendPixels[slot.batch].assign(atlasPixels[slot.batch].size(), worldAtlasFill);
      }
      worldTileBlit(blendPixels[slot.batch], batch.width, slot.x, slot.y, slot.width, slot.height,
        source.blendRgba);
      blendedMaterials.insert(source.material);
      ++blendTiles;
    } else {
      worldTileTable_[index].blend = false;
    }
    const bool hasModulate = !source.blendModulateRgba.empty();
    if (hasModulate && source.blendModulateRgba.size() != expected) {
      ++malformedLayers;
    } else if (hasModulate && blendedMaterials.count(source.material)) {
      if (modulatePixels[slot.batch].empty()) {
        modulatePixels[slot.batch].assign(atlasPixels[slot.batch].size(), worldAtlasFill);
      }
      worldTileBlit(modulatePixels[slot.batch], batch.width, slot.x, slot.y, slot.width, slot.height,
        source.blendModulateRgba);
      modulatedMaterials.insert(source.material);
      ++blendModulateTiles;
    } else {
      worldTileTable_[index].modulate = false;
    }
    if (!blendedMaterials.count(source.material)) worldTileTable_[index].blend = false;
    // ---- T27: the two normal layers ---------------------------------------
    // Each is resampled from the material's tile grid (where main.cpp put it)
    // into the feature atlas' own rectangle. The feature atlas is the same
    // rectangles in normalised space at 1/featureScale the resolution, so the
    // albedo uv addresses it without a second texcoord. A rectangle that cannot
    // be placed leaves the material's bit clear (the atlas has no texel under
    // its uv, and "sampled the filler" would be reported as applied).
    const auto blitFeature = [&](const std::vector<std::uint8_t>& rgba,
                                 std::vector<std::uint8_t>& target, WorldFeatureState& state,
                                 std::uint8_t fill) {
      if (rgba.empty()) return false;
      if (rgba.size() != expected) return false;
      WorldFeatureTileRect rect;
      if (!worldFeatureTileRect(slot, batch.width, batch.height, batch.featureWidth,
                                batch.featureHeight, rect)) {
        return false;
      }
      std::vector<std::uint8_t> resampled;
      if (!resampleRgba(rgba, slot.width, slot.height, rect.width, rect.height, resampled)) {
        return false;
      }
      if (target.empty()) target = makeNeutral(static_cast<std::size_t>(batch.featureBytes),
                                               fill, fill, fill);
      worldTileBlit(target, batch.featureWidth, rect.x, rect.y, rect.width, rect.height, resampled);
      ++state.tiles;
      return true;
    };
    if (worldMaterialBumpEnabled_ && blitFeature(source.bumpRgba, bumpPixels[slot.batch],
                                                 worldBumpFeature_, 128u)) {
      worldTileTable_[index].bump = true;
      bumpMaterials.insert(source.material);
    }
    if (worldMaterialBumpEnabled_ && worldBump2Enabled_
        && blitFeature(source.bump2Rgba, bump2Pixels[slot.batch], worldBump2Feature_, 128u)) {
      worldTileTable_[index].bump2 = true;
      bump2Materials.insert(source.material);
    }
    if (worldDetailEnabled_ && blitFeature(source.detailRgba, detailPixels[slot.batch],
                                           worldDetailFeature_, 0u)) {
      worldTileTable_[index].detail = true;
      detailBlitMaterials.insert(source.material);
    }
  }
  worldBlendTiles_ = blendTiles;
  worldBlendModulateTiles_ = blendModulateTiles;
  worldBlendMalformedLayers_ = malformedLayers;
  worldBlendVertices_ = 0;
  worldBlendModulateVertices_ = 0;
  worldBumpFeature_.materials = bumpMaterials.size();
  worldBump2Feature_.materials = bump2Materials.size();
  worldDetailFeature_.materials = detailBlitMaterials.size();
  // T12's 67.6 MiB note still applies per batch: a batch of 64 tiles is over the
  // 64 MiB guard the ordinary textures use, and that guard exists to catch a
  // runaway allocation rather than to refuse the size this pass is built around.
  // The whole plan's cost is bounded by the planner's budget and reported.
  worldAtlasTextures_.clear();
  worldBlendAtlasTextures_.clear();
  worldModulateAtlasTextures_.clear();
  worldBumpAtlasTextures_.clear();
  worldBump2AtlasTextures_.clear();
  // The single-atlas readings are the largest atlas, not batch 0's: a gate that
  // asked "is there a 4112-wide, 64-MiB atlas" still gets the atlas it meant,
  // and the total is reported separately.
  worldAtlasSize_ = 0;
  worldAtlasBytes_ = 0;
  worldAtlasCount_ = plan.batches.size();
  worldAtlasTotalBytes_ = plan.albedoBytes;
  for (const auto& layout : plan.batches) {
    worldAtlasSize_ = std::max(worldAtlasSize_, static_cast<std::size_t>(layout.width));
    worldAtlasBytes_ = std::max(worldAtlasBytes_, layout.albedoBytes);
  }
  worldAtlasBlendBatches_ = 0;
  std::string atlasError;
  for (std::size_t batch = 0; batch < plan.batches.size(); ++batch) {
    const auto& layout = plan.batches[batch];
    const std::uint64_t atlasBudget = std::max<std::uint64_t>(64ull * 1024ull * 1024ull,
      layout.albedoBytes);
    Texture2D albedo;
    if (!albedo.create(device_.Get(), atlasPixels[batch], layout.width, layout.height,
                       &atlasError, atlasBudget)) {
      worldAtlasReason_ = atlasError.empty() ? "the world atlas texture was refused" : atlasError;
      lastError_ = E_FAIL;
      return false;
    }
    worldAtlasTextures_.push_back(std::move(albedo));
    if (!blendPixels[batch].empty()) {
      Texture2D blend;
      if (!blend.create(device_.Get(), blendPixels[batch], layout.width, layout.height,
                        &atlasError, atlasBudget)) {
        worldAtlasReason_ = atlasError.empty() ? "the blend atlas texture was refused" : atlasError;
        lastError_ = E_FAIL;
        return false;
      }
      worldBlendAtlasTextures_.push_back(std::move(blend));
      ++worldAtlasBlendBatches_;
    } else {
      worldBlendAtlasTextures_.emplace_back();
    }
    if (!modulatePixels[batch].empty()) {
      Texture2D modulate;
      if (!modulate.create(device_.Get(), modulatePixels[batch], layout.width, layout.height,
                           &atlasError, atlasBudget)) {
        worldAtlasReason_ = atlasError.empty()
          ? "the blend modulate atlas texture was refused" : atlasError;
        lastError_ = E_FAIL;
        return false;
      }
      worldModulateAtlasTextures_.push_back(std::move(modulate));
    } else {
      worldModulateAtlasTextures_.emplace_back();
    }
    // T27: the normal-map layers, one texture per batch at the feature
    // resolution. Empty when the switch was off or no tile in this batch asked
    // for the layer, which is what keeps an off run's allocation identical to
    // the pre-T27 build.
    const std::uint64_t featureBudget = std::max<std::uint64_t>(64ull * 1024ull * 1024ull,
      layout.featureBytes);
    if (!bumpPixels[batch].empty()) {
      Texture2D bump;
      if (!bump.create(device_.Get(), bumpPixels[batch], layout.featureWidth,
                       layout.featureHeight, &atlasError, featureBudget)) {
        worldAtlasReason_ = atlasError.empty()
          ? "the bump atlas texture was refused" : atlasError;
        lastError_ = E_FAIL;
        return false;
      }
      worldBumpAtlasTextures_.push_back(std::move(bump));
    } else {
      worldBumpAtlasTextures_.emplace_back();
    }
    if (!bump2Pixels[batch].empty()) {
      Texture2D bump2;
      if (!bump2.create(device_.Get(), bump2Pixels[batch], layout.featureWidth,
                        layout.featureHeight, &atlasError, featureBudget)) {
        worldAtlasReason_ = atlasError.empty()
          ? "the bump2 atlas texture was refused" : atlasError;
        lastError_ = E_FAIL;
        return false;
      }
      worldBump2AtlasTextures_.push_back(std::move(bump2));
    } else {
      worldBump2AtlasTextures_.emplace_back();
    }
    // The detail layer, same rectangle rule and same budget as the normals.
    if (!detailPixels[batch].empty()) {
      Texture2D detail;
      if (!detail.create(device_.Get(), detailPixels[batch], layout.featureWidth,
                         layout.featureHeight, &atlasError, featureBudget)) {
        worldAtlasReason_ = atlasError.empty()
          ? "the detail atlas texture was refused" : atlasError;
        lastError_ = E_FAIL;
        return false;
      }
      worldDetailAtlasTextures_.push_back(std::move(detail));
    } else {
      worldDetailAtlasTextures_.emplace_back();
    }
  }
  worldAtlasReason_ = "bound";
  // The old single-atlas members keep their meaning for the two other passes
  // that still sample one texture (the entity and projectile draws use
  // `texture_`, not these). Batch 0 is what they would need if they ever did.
  worldBlendTexture_.reset();
  worldBlendModulateTexture_.reset();

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
  // T27: the feature material counts are counted off the BITS THE VERTICES
  // CARRY, not off the blit set. A material can be blitted into an atlas and
  // still have no vertex that samples it (the product path refuses layer 2 when
  // layer 1 is absent); reporting the blit set would say five materials apply
  // while the shader sees one.
  std::unordered_set<std::string> bumpVertexMaterials;
  std::unordered_set<std::string> bump2VertexMaterials;
  std::unordered_set<std::string> detailVertexMaterials;
  // T12: 256 buckets over [0,1]. The count of distinct buckets is what
  // separates "the painted weights arrived" from "a constant was written".
  std::vector<unsigned char> weightBuckets(256, 0);
  // T27: the same instrument for `$bumpmap2`'s lerp weight, because the SDK
  // blends layer 2's normal by the vertex's blend factor -- a constant 1 and a
  // painted gradient both draw a picture, and only the distribution tells them
  // apart.
  std::vector<unsigned char> bump2WeightBuckets(256, 0);
  std::unordered_set<std::string> detailMaterials;
  std::size_t lightmappedVertices = 0;
  std::size_t displacedTriangles = 0;
  std::size_t waterTriangles = 0;
  const auto convert = [&](const BspVertex& point, const BspVertex& normal) {
    return WorldVertex{(point.x - centerX) * horizontalScale, (point.y - centerY) * horizontalScale, (point.z - minZ) / spanZ * 0.8f + 0.1f, 0.62f, 0.65f, 0.68f, 0.0f, 0.0f, 0.0f, normal.x, normal.y, normal.z};
  };
  // T19: material -> its slot in the plan. A material the plan refused is
  // absent here, and the vertex loop's else branch is then the flat fallback the
  // shader already implements -- the same branch a material with no texture has
  // always taken, which is why an omission is a shade and not a wrong tile.
  std::unordered_map<std::string, std::size_t> tileByMaterial;
  tileByMaterial.reserve(plan.accepted);
  for (std::size_t index = 0; index < textures.size(); ++index) {
    if (plan.slots[index].batch >= 0) tileByMaterial.emplace(textures[index].material, index);
  }
  // A triangle whose material got no tile is still drawn: its vertex alpha is
  // 0, the pixel shader's `lerp(colour.rgb, sampled * colour.rgb, alpha)` then
  // uses the flat shade, and untiled geometry is a picture of the map rather
  // than a hole. It rides in batch 0 so it has an atlas to be drawn with, and
  // the flat branch is what keeps that atlas from painting it.
  const std::int16_t untiledBatch = plan.batches.empty() ? -1 : 0;
  worldTriangleBatch_.assign(map.triangles.size(), untiledBatch);
  // The ordinal of the triangle inside the buffer being uploaded, which is what
  // an index has to be built from. It equals the map's own ordinal unless the
  // terrain filter drops a triangle, and in that mode no batch is built at all
  // (see below), so the two never disagree where it matters.
  std::size_t uploadedTriangle = 0;
  for (const auto& triangle : map.triangles) {
    // T12: the terrain-only frame. The bounds above were taken over every
    // triangle on purpose, so filtering here leaves the terrain at exactly the
    // position and scale it has in the full frame -- a mask taken from a
    // re-centred world would not overlay the picture it is meant to bound.
    if (worldTerrainOnly_ && terrainMaterials.count(triangle.material) == 0) continue;
    const auto ab = BspVertex{triangle.b.x - triangle.a.x, triangle.b.y - triangle.a.y, triangle.b.z - triangle.a.z};
    const auto ac = BspVertex{triangle.c.x - triangle.a.x, triangle.c.y - triangle.a.y, triangle.c.z - triangle.a.z};
    const float nx = ab.y * ac.z - ab.z * ac.y, ny = ab.z * ac.x - ab.x * ac.z, nz = ab.x * ac.y - ab.y * ac.x;
    const float normalLength = std::sqrt(nx * nx + ny * ny + nz * nz);
    const auto normal = normalLength > 1e-6f ? BspVertex{nx / normalLength, ny / normalLength, nz / normalLength} : BspVertex{0.0f, 0.0f, 1.0f};
    auto a = convert(triangle.a, normal), b = convert(triangle.b, normal), c = convert(triangle.c, normal);
    const auto tile = tileByMaterial.find(triangle.material);
    if (tile != tileByMaterial.end()) {
      const WorldTileSlot& t = plan.slots[tile->second];
      const WorldTileBatch& batch = plan.batches[static_cast<std::size_t>(t.batch)];
      a.r = a.g = a.b = b.r = b.g = b.b = c.r = c.g = c.b = 1.0f;
      a.a = b.a = c.a = 1.0f;
      // The uv is normalised by THIS batch's atlas: a batch of 20 tiles is
      // 5x4, so its v is divided by four tile heights and not by the width of
      // the first atlas. T21: and it is normalised by the SOURCE size, not the
      // tile size -- `worldTileCoord` scales the wrapped texcoord by
      // extent/sourceSize, which is the downscale. Passing the tile size in
      // both places collapsed that ratio to 1 and made every downscaled
      // texture repeat: 70 of the 85 tiled materials on pl_odyssey.
      a.u = worldTileCoord(triangle.au, t.sourceWidth, t.x, t.width, batch.width);
      a.v = worldTileCoord(triangle.av, t.sourceHeight, t.y, t.height, batch.height);
      b.u = worldTileCoord(triangle.bu, t.sourceWidth, t.x, t.width, batch.width);
      b.v = worldTileCoord(triangle.bv, t.sourceHeight, t.y, t.height, batch.height);
      c.u = worldTileCoord(triangle.cu, t.sourceWidth, t.x, t.width, batch.width);
      c.v = worldTileCoord(triangle.cv, t.sourceHeight, t.y, t.height, batch.height);
      // T12: a weight is handed to the shader only for a material that shipped
      // a second layer. A displacement whose VMT names one layer keeps the
      // sentinel, because the blend atlas has no tile under its uv and the
      // branch would sample the filler.
      if (blendedMaterials.count(triangle.material)) {
        a.blend = triangle.aAlpha; b.blend = triangle.bAlpha; c.blend = triangle.cAlpha;
        const float modulate = modulatedMaterials.count(triangle.material) ? 1.0f : 0.0f;
        a.blendModulate = b.blendModulate = c.blendModulate = modulate;
      }
      // T12: counted off the *vertex*, not off the triangle. The first version of
      // this loop read `triangle.aAlpha` here, so a mutation that forced the
      // vertex weight to a constant moved no reading at all -- measured, m3 read
      // distinctWeights=9 with every uploaded weight at 1. What reaches the
      // shader is the vertex, so the vertex is what is counted; the same rule the
      // lightmap counter above states.
      for (const float weight : {a.blend, b.blend, c.blend}) {
        if (!(weight >= 0.0f)) continue;
        ++worldBlendVertices_;
        if (a.blendModulate > 0.5f) ++worldBlendModulateVertices_;
        const float clamped = std::min(std::max(weight, 0.0f), 1.0f);
        weightBuckets[static_cast<std::size_t>(clamped * 255.0f + 0.5f)] = 1;
      }
      if (triangle.lightmapped) { a.lu = triangle.alu; a.lv = triangle.alv; b.lu = triangle.blu; b.lv = triangle.blv; c.lu = triangle.clu; c.lv = triangle.clv; }
      // ---- T27: the per-material feature inputs ---------------------------
      // The mask is built from the blits this upload performed, not from the
      // VMTs: `bumpMaterials` is filled in the same loop that wrote the atlas,
      // so "the bit is set" and "there is a texel under this uv" are the same
      // statement by construction.
      {
        const WorldTexture& material = textures[tile->second];
        int mask = 0;
        if (bumpMaterials.count(triangle.material)) mask |= 1;
        // T27: layer 2 has no base normal unless layer 1 was blitted, so the
        // product path only sets bit 2 when bit 0 is set. The diagnostic does
        // NOT set bit 2 on a material without layer 1: measured on this machine,
        // that state moved 38,538 px even with the layer-2 arm deleted, and it
        // still did after the layer-1 writes were made selects -- the state
        // itself, not one write, and the mechanism was not isolated. So the
        // diagnostic instead sets `featureDiag` (the shader's `feature.w`) and
        // lerps onto the base the frame already computed.
        const bool layer2Declared = bump2Materials.count(triangle.material) != 0;
        if (layer2Declared && (mask & 1) != 0) mask |= 2;
        const bool layer2Diagnostic = layer2Declared && worldBump2Diagnostic_;
        const bool detailDrawn = worldDetailEnabled_
          && detailBlitMaterials.count(triangle.material) != 0;
        if (detailDrawn) mask |= 4;
        a.featureMask = b.featureMask = c.featureMask = static_cast<float>(mask);
        a.detailMode = b.detailMode = c.detailMode = static_cast<float>(material.detailMode);
        a.detailFactor = b.detailFactor = c.detailFactor = material.detailFactor;
        a.featureDiag = b.featureDiag = c.featureDiag = layer2Diagnostic ? 1.0f : 0.0f;
        // The face's own texture basis. Normalised here rather than in the
        // shader because it is constant across the face and already in map
        // space, which is the space the uv was computed in.
        float tx = triangle.uAxis[0], ty = triangle.uAxis[1], tz = triangle.uAxis[2];
        const float tlen = std::sqrt(tx * tx + ty * ty + tz * tz);
        if (tlen > 1.0e-6f) { tx /= tlen; ty /= tlen; tz /= tlen; }
        else { tx = 0.0f; ty = 0.0f; tz = 1.0f; }
        // The bitangent's sign: does cross(N, T) point along the map's +v axis?
        const float bx = normal.y * tz - normal.z * ty;
        const float by = normal.z * tx - normal.x * tz;
        const float bz = normal.x * ty - normal.y * tx;
        const float vSign = (bx * triangle.vAxis[0] + by * triangle.vAxis[1]
          + bz * triangle.vAxis[2]) < 0.0f ? -1.0f : 1.0f;
        a.tangentX = b.tangentX = c.tangentX = tx;
        a.tangentY = b.tangentY = c.tangentY = ty;
        a.tangentZ = b.tangentZ = c.tangentZ = tz;
        a.tangentSign = b.tangentSign = c.tangentSign = vSign;
        // Counted off the vertices, the same rule the blend weights above use:
        // a material in the set whose triangle never reached the buffer must not
        // read as applied.
        if ((mask & 1) != 0) {
          worldBumpFeature_.vertices += 3;
          bumpVertexMaterials.insert(triangle.material);
        }
        if ((mask & 2) != 0 || layer2Diagnostic) {
          worldBump2Feature_.vertices += 3;
          bump2VertexMaterials.insert(triangle.material);
          // The weight the shader will lerp layer 2's normal by: `max(blend,0)`,
          // saturate, exactly what the pixel shader computes.
          const float clamped = std::min(std::max(a.blend, 0.0f), 1.0f);
          bump2WeightBuckets[static_cast<std::size_t>(clamped * 255.0f + 0.5f)] = 1;
        }
        if ((mask & 4) != 0) {
          worldDetailFeature_.vertices += 3;
          detailMaterials.insert(triangle.material);
          detailVertexMaterials.insert(triangle.material);
        }
      }
      worldTriangleBatch_[uploadedTriangle] = static_cast<std::int16_t>(t.batch);
    } else {
      const std::uint32_t hash = static_cast<std::uint32_t>(std::hash<std::string>{}(triangle.material));
      const float shade = 0.78f + static_cast<float>(hash % 23) / 100.0f;
      a.r = a.g = a.b = b.r = b.g = b.b = c.r = c.g = c.b = shade;
      // T59 diagnostic: the same shade, replaced by magenta on request so the untiled
      // set is countable in pixels. The alpha stays 0 -- that is what makes the pixel
      // shader take the flat branch at all.
      if (worldRefusedMask_) {
        a.r = b.r = c.r = 1.0f;
        a.g = b.g = c.g = 0.0f;
        a.b = b.b = c.b = 1.0f;
      }
      // The lightmap is per *face*, not per *texture*: a face whose material
      // was refused a tile still has its own RGBExp32 slice, and dropping its
      // light uv here would draw it flat. On snakewater the atlas covered 64 of
      // 148 materials before T19, so this branch was where most of the map's
      // lightmapped triangles lost their lighting -- the atlas was bound, the
      // status read Active, and most of the map sampled nothing.
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
    // T26: the same id write as the single-material overload above. Without it
    // `--world-face-id` is white on the product path, which is the path the
    // seam trace has to read.
    if (worldFaceIdUpload_ && triangle.faceIndex >= 0) {
      writeWorldFaceId(a.r, a.g, a.b, a.a, triangle.faceIndex);
      writeWorldFaceId(b.r, b.g, b.b, b.a, triangle.faceIndex);
      writeWorldFaceId(c.r, c.g, c.b, c.a, triangle.faceIndex);
    }
    vertices.push_back(a); vertices.push_back(b); vertices.push_back(c);
    ++uploadedTriangle;
  }
  // T12: the weight distribution, summarised from the buckets the loop filled.
  // Computed here rather than in the pixel shader because it is a fact about
  // what was uploaded, and a shader-side counter would be a reading about the
  // branch rather than about the geometry.
  worldBlendWeightMin_ = -1.0f;
  worldBlendWeightMax_ = -1.0f;
  worldBlendDistinctWeights_ = 0;
  for (std::size_t bucket = 0; bucket < weightBuckets.size(); ++bucket) {
    if (!weightBuckets[bucket]) continue;
    const float value = static_cast<float>(bucket) / 255.0f;
    if (worldBlendWeightMin_ < 0.0f || value < worldBlendWeightMin_) worldBlendWeightMin_ = value;
    if (value > worldBlendWeightMax_) worldBlendWeightMax_ = value;
    ++worldBlendDistinctWeights_;
  }
  // T27: the detail layer's material count, and `$bumpmap2`'s weight
  // distribution -- the reading that separates the painted blend from a
  // constant.
  worldBumpFeature_.materials = bumpVertexMaterials.size();
  worldBump2Feature_.materials = bump2VertexMaterials.size();
  worldDetailFeature_.materials = detailVertexMaterials.size();
  worldDetailFeature_.tiles = detailMaterials.size();
  worldBump2Feature_.bump2WeightMin = -1.0f;
  worldBump2Feature_.bump2WeightMax = -1.0f;
  worldBump2Feature_.bump2DistinctWeights = 0;
  for (std::size_t bucket = 0; bucket < bump2WeightBuckets.size(); ++bucket) {
    if (!bump2WeightBuckets[bucket]) continue;
    const float value = static_cast<float>(bucket) / 255.0f;
    if (worldBump2Feature_.bump2WeightMin < 0.0f
        || value < worldBump2Feature_.bump2WeightMin) {
      worldBump2Feature_.bump2WeightMin = value;
    }
    if (value > worldBump2Feature_.bump2WeightMax) worldBump2Feature_.bump2WeightMax = value;
    ++worldBump2Feature_.bump2DistinctWeights;
  }
  // T12: an empty filter result is a refusal, not a zero-triangle buffer.
  // CreateBuffer with ByteWidth 0 fails on its own, but it fails with E_FAIL
  // and no reason; the caller would then see `uploadedWorld == false` and,
  // before the fallback was guarded, draw the whole world in `terrain` mode.
  if (vertices.empty()) {
    worldAtlasReason_ = worldTerrainOnly_
      ? "the terrain filter kept no triangle, so nothing was uploaded"
      : "the map produced no vertex, so nothing was uploaded";
    lastError_ = HRESULT_FROM_WIN32(ERROR_NO_DATA);
    return false;
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
  // ---- T19: the index buffer, grouped by batch -----------------------------
  if (worldTerrainOnly_)
  {
    // `--world-geometry terrain` uploads a *filtered* vertex buffer while the
    // pvs draw list and the batch table are indexed by the map's own triangle
    // numbers, so an index buffer built from that table would point past the
    // end of what was uploaded -- measured: the terrain-only frame lost a third
    // of its terrain region and the blend gate's on/off pair fell from 1930
    // changed pixels to 470. This mode draws the whole filtered buffer, which is
    // what it did before batching existed.
    worldDrawBatches_.clear();
    worldIndexCount_ = 0;
  }
  else
  // The vertex buffer keeps map.triangles order (the pvs draw list indexes it as
  // triangle*3, and reordering would make every index main.cpp hands over
  // wrong), so batching is expressed in the index buffer instead: batch 0's
  // triangles come first, then batch 1's, and each batch is one contiguous
  // DrawIndexed range with its own atlas. This buffer is also what the pass
  // draws with no clip at all, which is why it is built here rather than in
  // setWorldVisDrawList.
  {
    std::vector<std::vector<std::uint32_t>> perBatch(plan.batches.size());
    for (std::size_t triangle = 0; triangle < worldTriangleBatch_.size(); ++triangle) {
      const int batch = worldTriangleBatch_[triangle];
      if (batch < 0) continue;
      auto& target = perBatch[static_cast<std::size_t>(batch)];
      const auto base = static_cast<std::uint32_t>(triangle * 3u);
      target.push_back(base);
      target.push_back(base + 1u);
      target.push_back(base + 2u);
    }
    std::vector<std::uint32_t> grouped;
    grouped.reserve(map.triangles.size() * 3u);
    worldDrawBatches_.clear();
    for (std::size_t batch = 0; batch < perBatch.size(); ++batch) {
      WorldDrawBatch draw;
      draw.tiles = plan.batches[batch].tiles;
      draw.firstIndex = static_cast<UINT>(grouped.size());
      draw.indexCount = static_cast<UINT>(perBatch[batch].size());
      draw.albedoView = worldAtlasTextures_[batch].view();
      draw.blendView = batch < worldBlendAtlasTextures_.size()
        ? worldBlendAtlasTextures_[batch].view() : nullptr;
      draw.modulateView = batch < worldModulateAtlasTextures_.size()
        ? worldModulateAtlasTextures_[batch].view() : nullptr;
      // T27: the feature views are bound per batch for the same reason the
      // blend atlas is: a batch that holds no bump tile must not sample the
      // previous batch's normal map.
      draw.bumpView = batch < worldBumpAtlasTextures_.size()
        ? worldBumpAtlasTextures_[batch].view() : nullptr;
      draw.bump2View = batch < worldBump2AtlasTextures_.size()
        ? worldBump2AtlasTextures_[batch].view() : nullptr;
      draw.detailView = batch < worldDetailAtlasTextures_.size()
        ? worldDetailAtlasTextures_[batch].view() : nullptr;
      if (draw.indexCount > 0) worldDrawBatches_.push_back(draw);
      grouped.insert(grouped.end(), perBatch[batch].begin(), perBatch[batch].end());
    }
    if (!grouped.empty()) {
      D3D11_BUFFER_DESC indexDescription{};
      indexDescription.ByteWidth = static_cast<UINT>(grouped.size() * sizeof(std::uint32_t));
      indexDescription.Usage = D3D11_USAGE_DYNAMIC;
      indexDescription.BindFlags = D3D11_BIND_INDEX_BUFFER;
      indexDescription.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
      Microsoft::WRL::ComPtr<ID3D11Buffer> indexBuffer;
      if (FAILED(device_->CreateBuffer(&indexDescription, nullptr, indexBuffer.GetAddressOf()))) {
        lastError_ = E_FAIL;
        return false;
      }
      D3D11_MAPPED_SUBRESOURCE mapped{};
      if (FAILED(context_->Map(indexBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        lastError_ = E_FAIL;
        return false;
      }
      std::memcpy(mapped.pData, grouped.data(), grouped.size() * sizeof(std::uint32_t));
      context_->Unmap(indexBuffer.Get(), 0);
      worldIndexBuffer_ = std::move(indexBuffer);
      worldIndexCapacity_ = static_cast<UINT>(grouped.size());
      worldIndexCount_ = static_cast<UINT>(grouped.size());
    }
  }
  {
    float cx0 = 1e30f, cy0 = 1e30f, cz0 = 1e30f, cx1 = -1e30f, cy1 = -1e30f, cz1 = -1e30f;
    for (const auto& v : vertices) {
      cx0 = std::min(cx0, v.x); cy0 = std::min(cy0, v.y); cz0 = std::min(cz0, v.z);
      cx1 = std::max(cx1, v.x); cy1 = std::max(cy1, v.y); cz1 = std::max(cz1, v.z);
    }
    worldConvertedMin_[0] = cx0; worldConvertedMin_[1] = cy0; worldConvertedMin_[2] = cz0;
    worldConvertedMax_[0] = cx1; worldConvertedMax_[1] = cy1; worldConvertedMax_[2] = cz1;
  }
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


bool Renderer::uploadWorldSkyGeometry(const std::vector<WorldSkyVertex>& vertices) {
  worldSkyVertexBuffer_.Reset();
  worldSkyVertexCount_ = 0;
  if (vertices.empty()) {
    worldSkyReason_ = "no sky vertices were built";
    return true;
  }
  if (vertices.size() > (std::numeric_limits<UINT>::max)() / sizeof(WorldVertex)) {
    worldSkyReason_ = "the sky box is too large to upload";
    return false;
  }
  // Uploaded as WorldVertex, not as a sky-specific layout: the pass reuses the
  // world shader and its input layout, and a second vertex layout for six quads
  // would be one more thing to keep in step with the shader. The fields the sky
  // does not use are left at the values the shader ignores in sky mode.
  std::vector<WorldVertex> gpu;
  gpu.reserve(vertices.size());
  for (const auto& vertex : vertices) {
    WorldVertex converted{};
    converted.x = vertex.x; converted.y = vertex.y; converted.z = vertex.z;
    converted.r = 1.0f; converted.g = 1.0f; converted.b = 1.0f; converted.a = 0.0f;
    converted.u = vertex.u; converted.v = vertex.v;
    converted.nx = 0.0f; converted.ny = 0.0f; converted.nz = 0.0f;
    converted.lu = -1.0f; converted.lv = -1.0f;
    gpu.push_back(converted);
  }
  D3D11_BUFFER_DESC description{};
  description.ByteWidth = static_cast<UINT>(gpu.size() * sizeof(WorldVertex));
  description.Usage = D3D11_USAGE_DEFAULT;
  description.BindFlags = D3D11_BIND_VERTEX_BUFFER;
  D3D11_SUBRESOURCE_DATA initial{};
  initial.pSysMem = gpu.data();
  Microsoft::WRL::ComPtr<ID3D11Buffer> buffer;
  if (FAILED(device_->CreateBuffer(&description, &initial, buffer.GetAddressOf()))) {
    worldSkyReason_ = "CreateBuffer refused the sky box";
    return false;
  }
  worldSkyVertexBuffer_ = std::move(buffer);
  worldSkyVertexCount_ = static_cast<UINT>(gpu.size());
  return true;
}

void Renderer::setWorldVisClipEnabled(bool enabled) {
  worldVisClipEnabled_ = enabled;
  if (!enabled) {
    worldVisClipApplied_ = false;
    worldVisClipState_ = WorldVisClipState::Disabled;
    worldVisClipReason_ = "vis clip disabled by the caller";
  }
}

// ---------------------------------------------------------------------------
// T2 (2026-10-14): the water surface
// ---------------------------------------------------------------------------
// A second world buffer holding only the water triangles, in the same converted
// space and the same vertex layout as the world buffer. The layout is shared on
// purpose (see the static_assert at the top of this file) and the buffer is
// separate on purpose: the water pass draws its own subset, so switching it off
// cannot change a single triangle the world pass draws and cannot move the pvs
// clip's `drawn + hidden == input` identity.

bool Renderer::uploadWorldWaterGeometry(const std::vector<WorldWaterVertex>& vertices) {
  worldWaterVertexBuffer_.Reset();
  worldWaterVertexCount_ = 0;
  worldWaterDrewThisFrame_ = false;
  if (vertices.empty()) {
    worldWaterState_ = WorldWaterSurfaceState::Unavailable;
    worldWaterReason_ = "no water geometry was built";
    return true;
  }
  // Not a whole number of triangles is a corrupted buffer, not water. Refusing
  // by name is what stops a truncated upload from drawing half a surface and
  // still counting as a face.
  if (vertices.size() % 3u != 0u || vertices.size() % 2u != 0u) {
    worldWaterState_ = WorldWaterSurfaceState::Unavailable;
    worldWaterReason_ = "the water buffer holds " + std::to_string(vertices.size())
      + " vertices, which is not a whole number of faces";
    return false;
  }
  if (vertices.size() > (std::numeric_limits<UINT>::max)() / sizeof(WorldVertex)) {
    worldWaterState_ = WorldWaterSurfaceState::Unavailable;
    worldWaterReason_ = "the water surface is too large to upload";
    return false;
  }
  D3D11_BUFFER_DESC description{};
  description.ByteWidth = static_cast<UINT>(vertices.size() * sizeof(WorldVertex));
  description.Usage = D3D11_USAGE_DEFAULT;
  description.BindFlags = D3D11_BIND_VERTEX_BUFFER;
  D3D11_SUBRESOURCE_DATA initial{};
  initial.pSysMem = vertices.data();
  Microsoft::WRL::ComPtr<ID3D11Buffer> buffer;
  if (FAILED(device_->CreateBuffer(&description, &initial, buffer.GetAddressOf()))) {
    worldWaterState_ = WorldWaterSurfaceState::Unavailable;
    worldWaterReason_ = "CreateBuffer refused the water surface";
    return false;
  }
  worldWaterVertexBuffer_ = std::move(buffer);
  worldWaterVertexCount_ = static_cast<UINT>(vertices.size());
  return true;
}


bool Renderer::setWorldVisDrawList(const WorldVisDrawList& drawList) {
  worldVisClipState_ = drawList.state;
  worldVisHiddenTriangles_ = drawList.hiddenTriangles;
  worldVisTrianglesWithoutCluster_ = drawList.trianglesWithoutCluster;
  worldVisInputTriangles_ = drawList.inputTriangles;
  worldVisCameraCluster_ = drawList.cameraCluster;
  worldVisClipReason_ = drawList.reason;
  if (!worldVisClipEnabled_ || !drawList.applied || drawList.indices.empty()) {
    // Everything is drawn, which is what the pass did before the clip existed.
    // The reason word still says why, so "off" and "the pvs could not answer"
    // are not the same line.
    worldVisClipApplied_ = false;
    worldVisDrawnTriangles_ = worldVertexCount_ / 3u;
    return true;
  }
  // T19: the draw list arrives in map.triangles order (triangle*3 + j) and the
  // index buffer is grouped by batch, so the surviving triangles are regrouped
  // here. `worldTriangleBatch_` is the same mapping the upload built, which is
  // what keeps the draw list and the atlas from disagreeing about which batch a
  // triangle belongs to.
  if (worldDrawBatches_.empty()) {
    // No batch table: `--world-geometry terrain` filtered the vertex buffer and
    // the list was computed against the full triangle set, so applying it here
    // would index past the buffer. The whole filtered buffer is drawn, which is
    // this mode's behaviour from before batching.
    worldVisClipApplied_ = false;
    worldVisClipReason_ = "this geometry carries no batch table, so the whole buffer is drawn";
    worldVisDrawnTriangles_ = worldVertexCount_ / 3u;
    return true;
  }
  std::vector<std::vector<std::uint32_t>> perBatch(worldDrawBatches_.size());
  for (const std::uint32_t index : drawList.indices) {
    const std::size_t triangle = static_cast<std::size_t>(index) / 3u;
    const int batch = triangle < worldTriangleBatch_.size()
      ? worldTriangleBatch_[triangle] : (worldDrawBatches_.empty() ? -1 : 0);
    if (batch < 0 || static_cast<std::size_t>(batch) >= perBatch.size()) continue;
    perBatch[static_cast<std::size_t>(batch)].push_back(index);
  }
  std::vector<std::uint32_t> regrouped;
  regrouped.reserve(drawList.indices.size());
  for (std::size_t batch = 0; batch < perBatch.size(); ++batch) {
    worldDrawBatches_[batch].firstIndex = static_cast<UINT>(regrouped.size());
    worldDrawBatches_[batch].indexCount = static_cast<UINT>(perBatch[batch].size());
    regrouped.insert(regrouped.end(), perBatch[batch].begin(), perBatch[batch].end());
  }
  const UINT needed = static_cast<UINT>(regrouped.size());
  if (needed == 0) {
    worldVisClipApplied_ = false;
    worldVisClipReason_ = "the pvs list was empty after regrouping, so the whole buffer is drawn";
    worldVisDrawnTriangles_ = worldVertexCount_ / 3u;
    return true;
  }
  if (needed > worldIndexCapacity_ || !worldIndexBuffer_) {
    D3D11_BUFFER_DESC description{};
    description.ByteWidth = static_cast<UINT>(drawList.indices.size() * sizeof(std::uint32_t));
    description.Usage = D3D11_USAGE_DYNAMIC;
    description.BindFlags = D3D11_BIND_INDEX_BUFFER;
    description.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    Microsoft::WRL::ComPtr<ID3D11Buffer> buffer;
    if (FAILED(device_->CreateBuffer(&description, nullptr, buffer.GetAddressOf()))) {
      worldVisClipApplied_ = false;
      worldVisClipReason_ = "CreateBuffer refused the pvs index buffer";
      worldVisDrawnTriangles_ = worldVertexCount_ / 3u;
      return false;
    }
    worldIndexBuffer_ = std::move(buffer);
    worldIndexCapacity_ = needed;
  }
  D3D11_MAPPED_SUBRESOURCE mapped{};
  if (FAILED(context_->Map(worldIndexBuffer_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
    worldVisClipApplied_ = false;
    worldVisClipReason_ = "Map refused the pvs index buffer";
    worldVisDrawnTriangles_ = worldVertexCount_ / 3u;
    return false;
  }
  std::memcpy(mapped.pData, regrouped.data(),
    static_cast<std::size_t>(needed) * sizeof(std::uint32_t));
  context_->Unmap(worldIndexBuffer_.Get(), 0);
  worldIndexCount_ = needed;
  worldVisClipApplied_ = true;
  worldVisDrawnTriangles_ = drawList.outputTriangles;
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


bool Renderer::draw(float clearRed, float clearGreen, float clearBlue) {
  if (!context_ || !target_ || !swapChain_) return false;
  const float colour[4] = { clearRed, clearGreen, clearBlue, 1.0f };
  context_->OMSetRenderTargets(1, target_.GetAddressOf(), depthStencilView_.Get());
  context_->ClearRenderTargetView(target_.Get(), colour);
  if (depthStencilView_) context_->ClearDepthStencilView(depthStencilView_.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
  context_->OMSetDepthStencilState(depthStencilState_.Get(), 0);
  context_->RSSetState(worldBackfaceCull_ ? rasterizerState_.Get()
                                          : worldNoCullRasterizerState_.Get());
  D3D11_VIEWPORT viewport{};
  viewport.Width = static_cast<float>(width_);
  viewport.Height = static_cast<float>(height_);
  viewport.MinDepth = 0.0f;
  viewport.MaxDepth = 1.0f;
  context_->RSSetViewports(1, &viewport);
  context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  framePassOrdinal_ = 0;
  viewModelWorldPassOrdinal_ = 0;
  // T51: the HUD readings are per-frame, reset here for the reason the ordinal
  // above is: a drawHudPass that did not run this frame must read as "nothing was
  // drawn", not as "the last frame that drew something". Without this, turning the
  // HUD off would leave drawn=<the previous count> on the trace and a gate would
  // read a HUD that no longer exists.
  hudDrawnRects_ = 0;
  hudDrawnPrimitives_ = 0;
  hudTextQuads_ = 0;
  hudGlyphCount_ = 0;
  hudUnmappedGlyphs_ = 0;
  hudPassOrdinal_ = 0;
  hudScissorApplied_ = false;
  hudLastRegion_ = HudPixelRectLast{};
  // Both readings below are written by the draw calls themselves and read back
  // by worldMaterialSampleState(). They are reset here, once per frame, so that
  // "the sky drew" and "the world drew indexed" are statements about this frame
  // and cannot survive on a frame where the corresponding call did not run.
  worldSkyDrewThisFrame_ = false;
  worldVisDrewIndexed_ = false;
  // Defaults for the case where worldConstants_ is missing: a Z-up camera
  // looking down +X, so the ViewModel pass always has a camera to place against.
  frameView_ = {};
  frameView_.m[0][0] = 1.0f; frameView_.m[1][1] = 1.0f; frameView_.m[2][2] = 1.0f; frameView_.m[3][3] = 1.0f;
  frameEye_[0] = 0.0f; frameEye_[1] = 0.0f; frameEye_[2] = 0.0f;
  frameForward_[0] = 1.0f; frameForward_[1] = 0.0f; frameForward_[2] = 0.0f;
  frameUp_[0] = 0.0f; frameUp_[1] = 0.0f; frameUp_[2] = 1.0f;
  frameRight_[0] = 0.0f; frameRight_[1] = 1.0f; frameRight_[2] = 0.0f;
  // T11 diagnostic: the frame's world constants live outside the
  // `worldConstants_` block so the world draw (which is outside it) can
  // re-upload a debug variant and put the frame's own values back afterwards.
  // The struct is spelled once, here, so the upload sites cannot drift apart.
  struct WorldConstantsData {
    DirectX::XMFLOAT4X4 mvp;
    DirectX::XMFLOAT4 lightDirectionAndMode;
    DirectX::XMFLOAT4 cameraPositionAndSpecular;
    DirectX::XMFLOAT4 materialFeatures;
    DirectX::XMFLOAT4 lightmapFeatures;
    DirectX::XMFLOAT4 envFeatures;
  };
  WorldConstantsData frameConstants{};
  bool frameConstantsValid = false;
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
      // The debug eye is stored in the same normalized unit box as the world
      // vertex buffer. Its angles are authored in map units, so the direction
      // must pass through the same map-to-buffer scale as the eye; adding a
      // raw one-unit map vector here makes the camera look almost parallel to
      // the map and turns otherwise ordinary faces into giant screen shards.
      const float normalizedForwardX = forward[0] * worldHorizontalScale_;
      const float normalizedForwardY = forward[1] * worldHorizontalScale_;
      const float normalizedForwardZ = forward[2] * (0.8f / std::max(worldSpanZ_, 1.0e-6f));
      target = XMVectorSet(observerDemoEyeX_ + normalizedForwardX,
        observerDemoEyeY_ + normalizedForwardY,
        observerDemoEyeZ_ + normalizedForwardZ, 1.0f);
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
    // T29: THE WORLD IS NOT IN METRES. uploadWorldGeometry scales the map into
    // a normalized box at worldHorizontalScale_ map units per box unit, so a
    // near plane written in box units is a clip distance in MAP units:
    // 0.05 box units is 0.05 / worldHorizontalScale_ = 611 map units on the
    // reference map. The room the reference camera stands in is ~183 map units
    // away (a CPU ray cast from the demo eye hits a front face at a median of
    // 183 units), so the world pass was clipping out the entire space around
    // the camera and drawing only what lay beyond it -- the frame was a
    // dollhouse of distant walls rather than a first-person scene.
    //
    // The near plane is now stated in map units and converted through the same
    // transform the vertices went through, with the old box-unit value kept as
    // the fallback for a tree that has no valid transform yet. 4 map units is
    // the usual Source first-person near distance and is far enough in front of
    // the viewpoint that the camera's own geometry is inside the frame. The far
    // plane is box units too, but it is 100 box units, which is ~55 map spans
    // and also contains the sky box (halfExtent 40 box units), so it stays.
    const float kWorldNearMapUnits = 4.0f;
    const float worldNearPlane = (std::isfinite(worldHorizontalScale_) && worldHorizontalScale_ > 0.0f)
      ? std::max(worldHorizontalScale_ * kWorldNearMapUnits, 1.0e-6f) : 0.05f;
    const float worldFarPlane = 100.0f;
    worldNearPlaneNormalized_ = worldNearPlane;
    worldFarPlaneNormalized_ = worldFarPlane;
    const XMMATRIX projection = XMMatrixPerspectiveFovLH(XMConvertToRadians(settings_.viewModelFov), aspect, worldNearPlane, worldFarPlane);
    const XMMATRIX mvp = view * projection;
    WorldConstantsData& constants = frameConstants;
    XMStoreFloat4x4(&constants.mvp, mvp);
    // T24: the `preset == Standard` conjunct is gone. The flag is the statement
    // -- a caller that sets `dynamicLighting` (or `specular`, or `bumpMapping`)
    // is the thing being measured, and gating it on a preset name meant the
    // single-semantic switches produced no constant at all. The Performance
    // preset still leaves all three false, because fromPreset() and
    // loadSettings() zero-initialise before assigning.
    constants.lightDirectionAndMode = XMFLOAT4(0.35f, -0.45f, 0.82f,
      worldDynamicLightingEffective() ? 1.0f : 0.0f);
    constants.cameraPositionAndSpecular = XMFLOAT4(
      XMVectorGetX(eye), XMVectorGetY(eye), XMVectorGetZ(eye),
      worldSpecularEffective() ? 1.0f : 0.0f);
    const bool standardSelfIllum = settings_.preset == QualityPreset::Standard && worldMaterialParams_.selfIllum;
    constants.materialFeatures = XMFLOAT4(
      worldMaterialParams_.selfIllumR,
      worldMaterialParams_.selfIllumG,
      worldMaterialParams_.selfIllumB,
      standardSelfIllum ? 1.0f : 0.0f);
    constants.lightmapFeatures = XMFLOAT4(
      worldLightmapEnabled_
        ? (worldLightmapStatus_ == WorldLightmapStatus::Unavailable ? 1.0f : worldLightmapIntensity_)
        : 1.0f,
      worldBumpEffective() ? 1.0f : 0.0f,
      settings_.preset == QualityPreset::Standard && worldMaterialParams_.envMap && worldEnvTexture_.view() ? 1.0f : 0.0f,
      worldLightmapEnabled_ && worldLightmapTexture_.view() ? 1.0f : 0.0f);
    // The cube wins when it is there: it is the map's own environment rather
    // than a 2D projection of whatever $envmap named, and the capability layer
    // only builds it when all six faces decoded.
    constants.envFeatures = XMFLOAT4(static_cast<float>(worldEnvMode()), 0.0f,
      worldBump2Diagnostic_ ? 1.0f : 0.0f, 0.0f);
    context_->UpdateSubresource(worldConstants_.Get(), 0, nullptr, &constants, 0, 0);
    frameConstantsValid = true;
    // T21: publish the same numbers for the face trace. Copied field by field
    // from the struct that was just uploaded, so the trace cannot describe a
    // camera or a lighting mode the draw did not use.
    worldShaderConstants_ = WorldShaderConstants{};
    worldShaderConstants_.valid = true;
    worldShaderConstants_.mvp[0] = constants.mvp.m[0][0];
    worldShaderConstants_.mvp[1] = constants.mvp.m[0][1];
    worldShaderConstants_.mvp[2] = constants.mvp.m[0][2];
    worldShaderConstants_.mvp[3] = constants.mvp.m[0][3];
    worldShaderConstants_.mvp[4] = constants.mvp.m[1][0];
    worldShaderConstants_.mvp[5] = constants.mvp.m[1][1];
    worldShaderConstants_.mvp[6] = constants.mvp.m[1][2];
    worldShaderConstants_.mvp[7] = constants.mvp.m[1][3];
    worldShaderConstants_.mvp[8] = constants.mvp.m[2][0];
    worldShaderConstants_.mvp[9] = constants.mvp.m[2][1];
    worldShaderConstants_.mvp[10] = constants.mvp.m[2][2];
    worldShaderConstants_.mvp[11] = constants.mvp.m[2][3];
    worldShaderConstants_.mvp[12] = constants.mvp.m[3][0];
    worldShaderConstants_.mvp[13] = constants.mvp.m[3][1];
    worldShaderConstants_.mvp[14] = constants.mvp.m[3][2];
    worldShaderConstants_.mvp[15] = constants.mvp.m[3][3];
    worldShaderConstants_.lightDirection[0] = constants.lightDirectionAndMode.x;
    worldShaderConstants_.lightDirection[1] = constants.lightDirectionAndMode.y;
    worldShaderConstants_.lightDirection[2] = constants.lightDirectionAndMode.z;
    worldShaderConstants_.lightMode = constants.lightDirectionAndMode.w;
    worldShaderConstants_.cameraPosition[0] = constants.cameraPositionAndSpecular.x;
    worldShaderConstants_.cameraPosition[1] = constants.cameraPositionAndSpecular.y;
    worldShaderConstants_.cameraPosition[2] = constants.cameraPositionAndSpecular.z;
    worldShaderConstants_.specular = constants.cameraPositionAndSpecular.w > 0.5f;
    worldShaderConstants_.selfIllumTint[0] = constants.materialFeatures.x;
    worldShaderConstants_.selfIllumTint[1] = constants.materialFeatures.y;
    worldShaderConstants_.selfIllumTint[2] = constants.materialFeatures.z;
    worldShaderConstants_.selfIllum = constants.materialFeatures.w;
    worldShaderConstants_.lightmapFactor = constants.lightmapFeatures.x;
    worldShaderConstants_.bumpMode = constants.lightmapFeatures.y;
    worldShaderConstants_.lightmapEnabled = constants.lightmapFeatures.w > 0.5f;
    worldShaderConstants_.envMode = constants.envFeatures.x;
    worldShaderConstants_.debugView = worldDebugView_;
    worldShaderConstants_.width = static_cast<int>(width_);
    worldShaderConstants_.height = static_cast<int>(height_);
    // The sky box, first and with its own mvp: the same view and projection with
    // the translation removed, so the box stays centred on the camera and a
    // vertex's own direction is the direction the cube is sampled at. Drawn
    // before the world because it is the farthest thing in the frame, and under
    // the sky's rasterizer state because the box is seen from the inside.
    const bool hasSkyBox = worldSkyEnabled_ && worldSkyVertexBuffer_ && worldSkyVertexCount_ > 0
      && worldSkyView_ && worldSkyConstants_ && worldSkyRasterizerState_;
    if (hasSkyBox) {
      struct SkyConstantsData {
        XMFLOAT4X4 mvp;
        XMFLOAT4 lightDirectionAndMode;
        XMFLOAT4 cameraPositionAndSpecular;
        XMFLOAT4 materialFeatures;
        XMFLOAT4 lightmapFeatures;
        XMFLOAT4 envFeatures;
      } skyConstants{};
      XMMATRIX skyView = view;
      skyView.r[3] = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
      XMStoreFloat4x4(&skyConstants.mvp, skyView * projection);
      // Mode 3 is the sky branch in the pixel shader, and it is the only field
      // the branch reads: the box is lit by nothing and carries no lightmap uv.
      skyConstants.envFeatures = XMFLOAT4(3.0f, 0.0f, 0.0f, 0.0f);
      context_->UpdateSubresource(worldSkyConstants_.Get(), 0, nullptr, &skyConstants, 0, 0);
      const UINT skyStride = sizeof(WorldVertex), skyOffset = 0;
      auto* skyBuffer = worldSkyVertexBuffer_.Get();
      auto* skyCube = worldSkyView_.Get();
      auto* skyConstantsBuffer = worldSkyConstants_.Get();
      context_->RSSetState(worldSkyRasterizerState_.Get());
      context_->OMSetDepthStencilState(worldSkyDepthStencilState_.Get(), 0);
      context_->IASetInputLayout(worldInputLayout_.Get());
      context_->IASetVertexBuffers(0, 1, &skyBuffer, &skyStride, &skyOffset);
      context_->VSSetShader(worldVertexShader_.Get(), nullptr, 0);
      context_->VSSetConstantBuffers(0, 1, &skyConstantsBuffer);
      context_->PSSetShader(worldPixelShader_.Get(), nullptr, 0);
      context_->PSSetConstantBuffers(0, 1, &skyConstantsBuffer);
      context_->PSSetShaderResources(5, 1, &skyCube);
      context_->PSSetSamplers(0, 1, sampler_.GetAddressOf());
      context_->Draw(worldSkyVertexCount_, 0);
      worldSkyDrewThisFrame_ = true;
      // The world pass below must not inherit the sky's cube or its cull state.
      // T10: "the world's cull state" is the one the caller configured -- the
      // no-cull diagnostic must survive the sky's restore, or the capture's
      // world triangles would rasterise culled while the reading says they did
      // not.
      context_->RSSetState(worldBackfaceCull_ ? rasterizerState_.Get()
                                              : worldNoCullRasterizerState_.Get());
      context_->OMSetDepthStencilState(depthStencilState_.Get(), 0);
      ID3D11ShaderResourceView* noSky = nullptr;
      context_->PSSetShaderResources(5, 1, &noSky);
    }
  }
  // The sky box is drawn inside the constants block above, before the world:
  // it is the farthest thing in the frame, so it is the first thing drawn.
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
    ID3D11ShaderResourceView* bumpView = worldBumpTexture_.view();
    ID3D11ShaderResourceView* envView = worldEnvTexture_.view();
    ID3D11ShaderResourceView* cubeView = worldCubemapView_.Get();
    context_->PSSetShaderResources(1, 1, &bumpView);
    context_->PSSetShaderResources(2, 1, &envView);
    context_->PSSetShaderResources(5, 1, &cubeView);
    context_->PSSetSamplers(0, 1, sampler_.GetAddressOf());
    // The pvs clip, where it can be seen: with a draw list the pass draws
    // indexed, so the triangles the pvs hid are not rasterised at all. Without
    // one it draws the whole buffer, which is what it did before the clip
    // existed -- and `worldVisClipApplied_` says which of the two happened, so
    // a clip that silently stops being handed a list reads as off rather than
    // as working.
    // T11 diagnostic: the debug view is a property of THIS draw only. The
    // constants buffer is shared with the water and entity passes, so the
    // override is written for the world draw and the frame's own mode is
    // restored immediately after it -- otherwise a flat capture would also
    // flatten the water and the markers and the diagnosis would be about the
    // wrong pass.
    if (worldDebugView_ != 0 && frameConstantsValid) {
      WorldConstantsData debugConstants = frameConstants;
      // 1/2 are the flat/depth views and ride envFeatures.x (0 is the product
      // path); the albedo view rides envFeatures.y because x > 3.5 is already
      // claimed by the flat/depth block, and a mode 6 in x would be caught
      // there first -- measured: the first albedo capture was a depth frame.
      // T26 adds mode 4 = face id, which deliberately rides that same block at
      // 6.0 and is therefore a third member of it rather than a new sweep.
      debugConstants.envFeatures.x = worldDebugView_ == 2 ? 5.0f
        : worldDebugView_ == 3 ? 0.0f
        : worldDebugView_ == 4 ? 6.0f
        : worldDebugView_ == 5 ? 1.0f
        : worldDebugView_ == 6 ? 8.0f
        : worldDebugView_ == 7 ? 9.0f
        // Coverage mode (8) knowingly falls through to 4.0 so the world
        // never wears the sentinel; only the entity pass sits above it.
        // The id view (9) falls through for the same reason: the world is
        // background there, and a world pixel in an id colour would join a
        // per-entity mask it does not belong to.
        : 4.0f;
      debugConstants.envFeatures.y = worldDebugView_ == 3 ? 1.0f : 0.0f;
      context_->UpdateSubresource(worldConstants_.Get(), 0, nullptr, &debugConstants, 0, 0);
    }
    // ---- T19: one draw per batch, each with its own atlas -----------------
    // A batch is a contiguous index range and the atlas it was laid out in, so
    // "which texture covers this triangle" is answered by the draw call rather
    // than by a 64-tile ceiling. The single-texture overload builds no batches
    // and no index buffer; it keeps the old one-texture path below.
    worldBatchDrawCalls_ = 0;
    worldBatchTexturedDrawCalls_ = 0;
    const bool batchesIndexed = worldIndexBuffer_ && worldIndexCount_ > 0 && !worldDrawBatches_.empty();
    if (batchesIndexed) {
      context_->IASetIndexBuffer(worldIndexBuffer_.Get(), DXGI_FORMAT_R32_UINT, 0);
      for (const auto& batch : worldDrawBatches_) {
        if (batch.indexCount == 0) continue;
        ID3D11ShaderResourceView* albedoView = batch.albedoView;
        bindSlot0(albedoView, "world-batch");
        // The second layer and its mask are per batch: a batch that holds no
        // blended tile has no blend atlas, and binding the previous batch's
        // would let a terrain tile bleed into a batch with no terrain.
        ID3D11ShaderResourceView* blendView = batch.blendView;
        ID3D11ShaderResourceView* blendModulateView = batch.modulateView;
        context_->PSSetShaderResources(6, 1, &blendView);
        context_->PSSetShaderResources(7, 1, &blendModulateView);
        // T27: the material's own normal maps and the detail layer. The detail
        // texture is the same for every batch (it is one forced resource, see
        // the member's comment); the two normal atlases are per batch.
        ID3D11ShaderResourceView* bumpAtlasView = batch.bumpView;
        ID3D11ShaderResourceView* bump2AtlasView = batch.bump2View;
        ID3D11ShaderResourceView* detailView = batch.detailView;
        context_->PSSetShaderResources(8, 1, &bumpAtlasView);
        context_->PSSetShaderResources(9, 1, &bump2AtlasView);
        context_->PSSetShaderResources(10, 1, &detailView);
        // The cross-pass question, measured where it can go wrong: a world
        // draw that happens while a weapon's albedo is the bound t0.
        if (isWeaponMaterialView(lastSlot0View_)) ++worldDrawsWithWeaponTexture_;
        context_->DrawIndexed(batch.indexCount, batch.firstIndex, 0);
        ++worldBatchDrawCalls_;
        if (albedoView) ++worldBatchTexturedDrawCalls_;
      }
      worldVisDrewIndexed_ = !worldDiagnosticIsolation_ && worldVisClipApplied_;
      ID3D11Buffer* noIndex = nullptr;
      context_->IASetIndexBuffer(noIndex, DXGI_FORMAT_R32_UINT, 0);
    } else {
      auto* worldView = worldTexture_.view() ? worldTexture_.view() : texture_.view();
      bindSlot0(worldView, "world-single");
      ID3D11ShaderResourceView* blendView = worldBlendTexture_.view();
      ID3D11ShaderResourceView* blendModulateView = worldBlendModulateTexture_.view();
      context_->PSSetShaderResources(6, 1, &blendView);
      context_->PSSetShaderResources(7, 1, &blendModulateView);
      if (isWeaponMaterialView(lastSlot0View_)) ++worldDrawsWithWeaponTexture_;
      context_->Draw(worldVertexCount_, 0);
      ++worldBatchDrawCalls_;
    }
    if (worldDebugView_ != 0 && frameConstantsValid) {
      context_->UpdateSubresource(worldConstants_.Get(), 0, nullptr, &frameConstants, 0, 0);
    }
  } else if (!worldDiagnosticIsolation_) {
    // T12: this placeholder is a full-screen textured quad, so a `none` frame
    // drawn through it is a picture of the map's display texture rather than
    // the background. The isolation switch skips it; the clear colour then is
    // the whole frame, which is exactly the control the region proof needs.
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

  // ---- T2 (2026-10-14): the water surface, as its own pass ----------------
  // Drawn immediately after the world pass, over the same triangles, with its
  // own biased rasterizer state and its own texture at t0. It reuses the world's
  // vertex shader, input layout and constant buffer deliberately: the mvp and
  // the light direction the world pass just uploaded are the ones the water
  // should be drawn with, and a second copy of either would be a second thing
  // that could disagree.
  //
  // What this pass must NOT do is change what the world pass drew. It reads its
  // own vertex buffer and its own texture, and the only state it writes is its
  // own draw flag -- so `worldVisDrawnTriangles_` and `worldLightmappedVertexCount_`
  // are untouched by switching water on or off, which is the property that keeps
  // T1's VIS identity and this round's water switch independent.
  worldWaterDrewThisFrame_ = false;
  if (worldWaterEnabled_ && worldWaterVertexBuffer_ && worldWaterVertexCount_ > 0
      && worldVertexShader_ && worldPixelShader_ && worldInputLayout_
      && worldWaterRasterizerState_) {
    const UINT waterStride = sizeof(WorldVertex), waterOffset = 0;
    auto* waterBuffer = worldWaterVertexBuffer_.Get();
    auto* waterConstants = worldConstants_.Get();
    // No bump, no env texture and no cube: the water's own material decides
    // those, and leaving the world's bound would sample the map's textures with
    // the water's uv and call the result water.
    ID3D11ShaderResourceView* waterTexture = worldWaterTexture_.view();
    ID3D11ShaderResourceView* noBump = nullptr;
    ID3D11ShaderResourceView* noEnv = nullptr;
    ID3D11ShaderResourceView* noCube = nullptr;
    ID3D11ShaderResourceView* noLightmap = nullptr;
    context_->RSSetState(worldWaterRasterizerState_.Get());
    context_->IASetInputLayout(worldInputLayout_.Get());
    context_->IASetVertexBuffers(0, 1, &waterBuffer, &waterStride, &waterOffset);
    context_->VSSetShader(worldVertexShader_.Get(), nullptr, 0);
    context_->VSSetConstantBuffers(0, 1, &waterConstants);
    context_->PSSetShader(worldPixelShader_.Get(), nullptr, 0);
    context_->PSSetConstantBuffers(0, 1, &waterConstants);
    context_->PSSetShaderResources(0, 1, &waterTexture);
    context_->PSSetShaderResources(1, 1, &noBump);
    context_->PSSetShaderResources(2, 1, &noEnv);
    context_->PSSetShaderResources(3, 1, &noLightmap);
    context_->PSSetShaderResources(5, 1, &noCube);
    // T12: water carries the blend sentinel, so these are never sampled; the
    // bind is cleared so a stale terrain atlas cannot be read by a later pass
    // that forgets to set its own.
    context_->PSSetShaderResources(6, 1, &noBump);
    context_->PSSetShaderResources(7, 1, &noBump);
    context_->PSSetShaderResources(8, 1, &noBump);
    context_->PSSetShaderResources(9, 1, &noBump);
    context_->PSSetShaderResources(10, 1, &noBump);
    context_->PSSetSamplers(0, 1, sampler_.GetAddressOf());
    context_->Draw(worldWaterVertexCount_, 0);
    worldWaterDrewThisFrame_ = true;
    // The passes below must not inherit the water's bias or its texture. Same
    // T10 rule as the sky's restore: the diagnostic cull state survives.
    context_->RSSetState(worldBackfaceCull_ ? rasterizerState_.Get()
                                            : worldNoCullRasterizerState_.Get());
    ID3D11ShaderResourceView* noWater = nullptr;
    context_->PSSetShaderResources(0, 1, &noWater);
    context_->PSSetShaderResources(1, 1, &noBump);
    context_->PSSetShaderResources(2, 1, &noEnv);
    context_->PSSetShaderResources(3, 1, &noLightmap);
    context_->PSSetShaderResources(5, 1, &noCube);
  }
  // The water surface is ordinal 2 and the entity markers 3, so that the
  // overlay slot number below is the contract ordinal (6) rather than
  // whichever ++ happens to come fourth in this function.
  ++framePassOrdinal_;

  if (!worldDiagnosticIsolation_ && entityMarkerVertexBuffer_ && entityMarkerVertexCount_ > 0
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
    // T19: batch 0 is the atlas an untiled pass samples. The markers, the
    // projectiles, the entity instances and the legacy model path bind their
    // own view below where they have one; this is what they use when they do
    // not, and it is a world tile rather than the map's display texture.
    auto* worldView = worldAtlasTextures_.empty()
      ? (worldTexture_.view() ? worldTexture_.view() : texture_.view())
      : worldAtlasTextures_.front().view();
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
  // T12: entity instances are content, not background; a diagnostic frame has
  // none of them, or the mask would include whatever model happened to be near
  // the camera.
  if (!worldDiagnosticIsolation_ && !entityModelDrawRanges_.empty() && entityModelInstanceBuffer_ && entityModelInstanceCount_ > 0
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
    // T19: batch 0 is the atlas an untiled pass samples. The markers, the
    // projectiles, the entity instances and the legacy model path bind their
    // own view below where they have one; this is what they use when they do
    // not, and it is a world tile rather than the map's display texture.
    auto* worldView = worldAtlasTextures_.empty()
      ? (worldTexture_.view() ? worldTexture_.view() : texture_.view())
      : worldAtlasTextures_.front().view();
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
    // T61 diagnostic: the same flat/albedo/face-id views the world pass has,
    // applied to the entity pass. Without this the entity pixels can only be
    // read as the product's own shading, so a black body cannot be told apart
    // from a body whose albedo sample is black. Written for THIS pass only and
    // restored below, for the reason the world block records: the constants
    // buffer is shared, and a debug frame that also flattens the world would
    // be a picture of the wrong pass.
    if (worldDebugView_ != 0 && frameConstantsValid) {
      WorldConstantsData entityDebugConstants = frameConstants;
      entityDebugConstants.envFeatures.x = worldDebugView_ == 2 ? 5.0f
        : worldDebugView_ == 3 ? 0.0f
        : worldDebugView_ == 4 ? 6.0f
        : worldDebugView_ == 5 ? 1.0f
        : worldDebugView_ == 6 ? 8.0f
        : worldDebugView_ == 7 ? 9.0f
        // T61.1: mode 9 (per-entity id) is 11.0, one step above the
        // coverage sentinel, because its arm tests x > 10.5.
        : worldDebugView_ == 8 ? 10.0f
        : worldDebugView_ == 9 ? 11.0f : 4.0f;
      entityDebugConstants.envFeatures.y = worldDebugView_ == 3 ? 1.0f : 0.0f;
      context_->UpdateSubresource(worldConstants_.Get(), 0, nullptr, &entityDebugConstants, 0, 0);
    }
    for (const auto& range : entityModelDrawRanges_) {
      if (!range.vertexBuffer || range.vertexCount < 3 || range.instanceCount == 0) continue;
      // Entity meshes have their own material domain. Binding the world atlas
      // when that material is missing paints characters with unrelated map
      // tiles and makes a resource failure look like a valid draw.
      if (!range.textureView) continue;
      auto* rangeView = range.textureView;
      context_->PSSetShaderResources(0, 1, &rangeView);
      ID3D11Buffer* buffers[2] = { range.vertexBuffer, instanceBuffer };
      // The run is a window into the model's one vertex buffer: offset the bind
      // instead of uploading a buffer per material.
      const UINT rangeOffsets[2] = {
        range.vertexStart * static_cast<UINT>(sizeof(EntityModelGpuVertex)), 0u };
      context_->IASetVertexBuffers(0, 2, buffers, strides, rangeOffsets);
      context_->DrawInstanced(range.vertexCount, range.instanceCount, 0, range.instanceStart);
    }
    if (worldDebugView_ != 0 && frameConstantsValid) {
      context_->UpdateSubresource(worldConstants_.Get(), 0, nullptr, &frameConstants, 0, 0);
    }
  }
  ++framePassOrdinal_;
  // ---- T52: THE OVERLAY PASS ----------------------------------------------
  // Contract ordinal 6: after the entity meshes it outlines and after the
  // projectile-track pass, before the ViewModel pass, which CLEARS the depth
  // buffer. Drawing it after the ViewModel would either punch the weapon
  // through the silhouette or need a second depth clear, and both are worse
  // than drawing it where the scene depth is still the frame depth -- which is
  // why the body is defined here and the CALL sits 200 lines below.
  overlayPassReading_ = OverlayPassReading{};
  // Defined here, CALLED as pass 6 (after the projectile-track pass). The
  // ordinal is taken with the ++ inside the lambda, so moving the call moves
  // the number: the mutation that draws it after the ViewModel cannot keep
  // reading 6.
  const auto drawOverlayPass = [&]() {
    overlayPassReading_.stageOrdinal = ++framePassOrdinal_;
    const bool anyEnabled = overlayLists_[0].enabled || overlayLists_[1].enabled;
    overlayPassReading_.enabled = anyEnabled;
    if (!worldDiagnosticIsolation_) {
      const auto drawList = [&](std::size_t slot) -> bool {
        OverlayDrawList& list = overlayLists_[slot];
        if (!list.enabled) return false;
        overlayPassReading_.depth = list.depth;
        const bool xraySlot = list.depth == OverlayDepthMode::XRay;
        overlayPassReading_.depthTestEnabled = xraySlot
          ? overlayXRayDepthTestEnabled_ : overlayDepthTestedDepthTestEnabled_;
        overlayPassReading_.depthWriteEnabled = xraySlot
          ? overlayXRayDepthWriteEnabled_ : overlayDepthTestedDepthWriteEnabled_;
        const DirectX::XMMATRIX mvpForOverlay = DirectX::XMLoadFloat4x4(&frameConstants.mvp);
        const bool hasSilhouettes = overlayInstanceBuffer_ && overlayInstanceCount_ > 0
          && !overlayDrawRanges_.empty() && overlayVertexShader_ && overlayPixelShader_
          && entityModelInputLayout_ && overlayConstants_ && worldPixelShader_;
        const bool hasLines = overlayLineBuffer_ && overlayLineVertexCountByDepth_[slot] > 0
          && overlayLineVertexShader_ && overlayLineInputLayout_ && overlayConstants_;
        if (!hasSilhouettes && !hasLines) return false;
        if (!overlayConstants_) return false;
        // The frame's own mvp: the overlay is aligned with the world it is drawn
        // over, so it must be the SAME camera. Re-deriving it here would let the
        // two disagree the first time the camera code changes.
        if (frameConstantsValid) {
          struct OverlayConstantsData {
            DirectX::XMFLOAT4X4 mvp;
            DirectX::XMFLOAT4 cameraPositionAndExpand;
            DirectX::XMFLOAT4 mode;
          } overlayConstants{};
          overlayConstants.mvp = frameConstants.mvp;
          overlayConstants.cameraPositionAndExpand = DirectX::XMFLOAT4(
            frameEye_[0], frameEye_[1], frameEye_[2], kOverlaySilhouetteExpandPixels);
          // mode.x = expansion on (silhouettes; 0 for a trail), mode.yz = the NDC
          // size of one pixel on each axis, so the shader's expansion is in pixels.
          overlayConstants.mode = DirectX::XMFLOAT4(1.0f,
            2.0f / static_cast<float>(width_), 2.0f / static_cast<float>(height_), 0.0f);
          context_->UpdateSubresource(overlayConstants_.Get(), 0, nullptr, &overlayConstants, 0, 0);
        } else {
          return false;
        }
        // Its own rasterizer and depth state, so the world's cull and depth
        // settings cannot leak in and the overlay's cannot leak out.
        context_->RSSetState(overlayRasterizerState_.Get());
        ID3D11DepthStencilState* boundDepthState = list.depth == OverlayDepthMode::XRay
          ? overlayXRayDepthState_.Get() : overlayDepthTestedState_.Get();
        context_->OMSetDepthStencilState(boundDepthState, 0);
        overlayPassReading_.stateBoundDepthTest =
          (boundDepthState == overlayXRayDepthState_.Get())
          ? (overlayXRayDepthTestEnabled_ ? 1 : 0)
          : (overlayDepthTestedDepthTestEnabled_ ? 1 : 0);
        context_->PSSetShader(overlayPixelShader_.Get(), nullptr, 0);
        context_->OMSetBlendState(nullptr, nullptr, 0xffffffffu);
        ID3D11ShaderResourceView* noTexture = nullptr;
        context_->PSSetShaderResources(0, 1, &noTexture);
        ID3D11ShaderResourceView* noBump = nullptr;
        context_->PSSetShaderResources(1, 1, &noBump);
        context_->PSSetShaderResources(2, 1, &noBump);
        context_->PSSetShaderResources(3, 1, &noBump);
        context_->PSSetShaderResources(5, 1, &noBump);
        auto* overlayConstantsBuffer = overlayConstants_.Get();
        context_->PSSetConstantBuffers(0, 1, &overlayConstantsBuffer);
        context_->VSSetConstantBuffers(0, 1, &overlayConstantsBuffer);
        if (hasSilhouettes) {
          const UINT strides[2] = { sizeof(EntityModelGpuVertex), sizeof(EntityModelInstanceGpu) };
          const UINT offsets[2] = { 0, 0 };
          context_->IASetInputLayout(entityModelInputLayout_.Get());
          context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
          context_->VSSetShader(overlayVertexShader_.Get(), nullptr, 0);
          // The pass shares the entity pass's bone buffer: a silhouette must
          // follow the pose its mesh was drawn in, not the bind pose.
          auto* boneView = entityBoneView_.Get();
          context_->VSSetShaderResources(4, 1, &boneView);
          auto* instanceBuffer = overlayInstanceBuffer_.Get();
          // One item per frame gets the vertex-level box: 12000 vertices through a
          // matrix is not something to repeat for every silhouette.
          bool vertexBoxDone = false;
          for (const auto& range : overlayDrawRanges_) {
            if (range.depth != list.depth) continue;
            if (!range.vertexBuffer || range.vertexCount < 3 || range.instanceCount == 0) continue;
            ID3D11Buffer* buffers[2] = { range.vertexBuffer, instanceBuffer };
            context_->IASetVertexBuffers(0, 2, buffers, strides, offsets);
            context_->DrawInstanced(range.vertexCount, range.instanceCount, 0, range.instanceStart);
            ++overlayPassReading_.silhouettes;
            ++overlayPassReading_.drawCalls;
            // Publish where each drawn silhouette lands, so a gate can ask about
            // THAT player's region instead of the whole frame. The box is the
            // projection of the instance's own world-space extent through the SAME
            // mvp the shader was handed; a second projection rule here would be a
            // second answer to "where is this entity on screen".
            for (UINT index = range.instanceStart; index < range.instanceStart + range.instanceCount; ++index) {
              if (index >= overlayInstanceCount_) break;
              const auto& instance = overlayInstances_[index];
              OverlayItemReading reading;
              reading.entityIndex = instance.entityIndex;
              reading.team = instance.team;
              reading.depthMode = list.depth;
              reading.meshKeyHash = instance.meshKeyHash;
              reading.meshVertexCount = instance.meshVertexCount;
              reading.skinned = instance.skinned;
              reading.rangeVertexCount = range.vertexCount;
              reading.rangeInstanceCount = range.instanceCount;
              for (int axis = 0; axis < 3; ++axis) {
                reading.meshBoundsMin[axis] = instance.meshBoundsMin[axis];
                reading.meshBoundsMax[axis] = instance.meshBoundsMax[axis];
              }
              // Where this instance's ORIGIN lands on screen, through the same mvp.
              // If the box centre tracks this but the box is squashed, the extent is
              // wrong; if both are offset from the painted blob, the placement is.
              {
                const DirectX::XMVECTOR originPoint = DirectX::XMVectorSet(
                  instance.origin[0], instance.origin[1], instance.origin[2], 1.0f);
                const DirectX::XMVECTOR originClip = DirectX::XMVector4Transform(originPoint, mvpForOverlay);
                const float originW = DirectX::XMVectorGetW(originClip);
                if (originW > 1.0e-4f) {
                  reading.originX = static_cast<int>(
                    (DirectX::XMVectorGetX(originClip) / originW * 0.5f + 0.5f) * static_cast<float>(width_));
                  reading.originY = static_cast<int>(
                    (0.5f - DirectX::XMVectorGetY(originClip) / originW * 0.5f) * static_cast<float>(height_));
                }
              }
              // The extent in the renderer's normalised box: a player model is
              // about 40 map units across and 72 tall (the Source player hull),
              // converted with the frame's own scale factors rather than a
              // constant, so a different map cannot silently change what "the
              // region this player covers" means.
              // The box is THIS mesh's own extent, transformed by THIS instance's
              // rows and projected with the same mvp the shader was handed. The
              // constant hull it replaces projected to under two pixels on this
              // map, so every item read x0==x1 and the region assertions could not
              // be evaluated at all -- a correct silhouette inside a one-pixel box.
              const float legacyHalfX = 20.0f * worldHorizontalScale_;
              const float legacyHalfZ = 36.0f * (0.8f / worldSpanZ_);
              const DirectX::XMVECTOR legacyCenter = DirectX::XMVectorSet(instance.origin[0], instance.origin[1], instance.origin[2], 1.0f);
              float minX = 1.0f, minY = 1.0f, maxX = -1.0f, maxY = -1.0f;
              float nearest = 1.0f;
              bool projected = false;
              for (int axis = 0; axis < 3; ++axis) {
                reading.worldExtent[axis] = instance.worldMin[axis];
                reading.worldExtent[axis + 3] = instance.worldMax[axis];
              }
              const float* boxMin = instance.worldMin;
              const float* boxMax = instance.worldMax;
              for (int corner = 0; corner < 8; ++corner) {
                const DirectX::XMVECTOR world = DirectX::XMVectorSet(
                  (corner & 1) ? boxMax[0] : boxMin[0],
                  (corner & 4) ? boxMax[1] : boxMin[1],
                  (corner & 2) ? boxMax[2] : boxMin[2], 1.0f);
                const DirectX::XMVECTOR clip = DirectX::XMVector4Transform(world, mvpForOverlay);
                const float w = DirectX::XMVectorGetW(clip);
                if (corner == 0 || w < reading.cornerWMin) reading.cornerWMin = w;
                if (corner == 0 || w > reading.cornerWMax) reading.cornerWMax = w;
                if (!(w > 1.0e-4f)) continue;
                ++reading.cornersProjected;
                projected = true;
                const float ndcX = DirectX::XMVectorGetX(clip) / w;
                const float ndcY = DirectX::XMVectorGetY(clip) / w;
                const float ndcZ = DirectX::XMVectorGetZ(clip) / w;
                minX = std::min(minX, ndcX); maxX = std::max(maxX, ndcX);
                minY = std::min(minY, ndcY); maxY = std::max(maxY, ndcY);
                nearest = std::min(nearest, ndcZ);
              }
              // The box the OLD constant hull gives for the same silhouette, in
              // the same units, so ONE trace line carries both. If the two
              // disagree the reader can see which one covers the pixels the pass
              // drew, and nobody can mistake "the box was fixed" for "the
              // assertion was relaxed".
              {
                float legacyMinX = 1.0f, legacyMinY = 1.0f, legacyMaxX = -1.0f, legacyMaxY = -1.0f;
                for (int corner = 0; corner < 8; ++corner) {
                  const float sx = (corner & 1) ? legacyHalfX : -legacyHalfX;
                  const float sz = (corner & 2) ? legacyHalfZ : -legacyHalfZ;
                  const float sy = (corner & 4) ? legacyHalfX : -legacyHalfX;
                  const DirectX::XMVECTOR world = DirectX::XMVectorAdd(legacyCenter,
                    DirectX::XMVectorSet(sx, sy, sz, 0.0f));
                  const DirectX::XMVECTOR clip = DirectX::XMVector4Transform(world, mvpForOverlay);
                  const float w = DirectX::XMVectorGetW(clip);
                  if (!(w > 1.0e-4f)) continue;
                  const float ndcX = DirectX::XMVectorGetX(clip) / w;
                  const float ndcY = DirectX::XMVectorGetY(clip) / w;
                  legacyMinX = std::min(legacyMinX, ndcX); legacyMaxX = std::max(legacyMaxX, ndcX);
                  legacyMinY = std::min(legacyMinY, ndcY); legacyMaxY = std::max(legacyMaxY, ndcY);
                }
                reading.legacyX0 = static_cast<int>((legacyMinX * 0.5f + 0.5f) * static_cast<float>(width_));
                reading.legacyX1 = static_cast<int>((legacyMaxX * 0.5f + 0.5f) * static_cast<float>(width_));
                reading.legacyY0 = static_cast<int>((0.5f - legacyMaxY * 0.5f) * static_cast<float>(height_));
                reading.legacyY1 = static_cast<int>((0.5f - legacyMinY * 0.5f) * static_cast<float>(height_));
              }
              // T52 (2026-10-23): which box the REGION comes from. The pass draws the
              // class mesh's triangles; the AABB it used to publish is the sampler's hull
              // for the entity. Measured on the reference recording they differ by a 1-6 px
              // fringe of painted pixels outside the box, i.e. the assertion's ruler was
              // the wrong object. When the switch is on, every silhouette publishes the
              // projection of ITS OWN vertices (through the same rows and the same mvp the
              // shader was handed), and the AABB box is kept beside it for attribution.
              // Skinned instances are excluded on purpose: the CPU has the BIND positions,
              // so projecting them would publish a box for a pose nobody draws. Those
              // items fall back to the AABB and are counted as extent=2, which the gate
              // reads as "the region is not the drawn extent" instead of trusting it.
              const bool wantVertexBox = overlayVertexExtent_ && instance.skinned == 0
                && instance.vertexPositions && instance.vertexPositionCount > 0;
              if ((wantVertexBox || (!vertexBoxDone && instance.vertexPositions))
                  && instance.vertexPositionCount > 0) {
                if (!wantVertexBox) vertexBoxDone = true;
                float vMinX = 1.0e30f, vMinY = 1.0e30f, vMaxX = -1.0e30f, vMaxY = -1.0e30f;
                std::uint32_t inFront = 0;
                for (std::uint32_t vertex = 0; vertex < instance.vertexPositionCount; ++vertex) {
                  const float* position = instance.vertexPositions + vertex * 3;
                  float world[3] = {0.0f, 0.0f, 0.0f};
                  for (int axis = 0; axis < 3; ++axis) {
                    const float* row = instance.rows + axis * 4;
                    world[axis] = row[0] * position[0] + row[1] * position[1]
                      + row[2] * position[2] + row[3];
                  }
                  const DirectX::XMVECTOR clip = DirectX::XMVector4Transform(
                    DirectX::XMVectorSet(world[0], world[1], world[2], 1.0f), mvpForOverlay);
                  const float w = DirectX::XMVectorGetW(clip);
                  if (!(w > 1.0e-4f)) continue;
                  ++inFront;
                  vMinX = std::min(vMinX, DirectX::XMVectorGetX(clip) / w);
                  vMaxX = std::max(vMaxX, DirectX::XMVectorGetX(clip) / w);
                  vMinY = std::min(vMinY, DirectX::XMVectorGetY(clip) / w);
                  vMaxY = std::max(vMaxY, DirectX::XMVectorGetY(clip) / w);
                }
                reading.vertexCountProjected = inFront;
                if (inFront > 0) {
                  reading.vertexX0 = static_cast<int>((vMinX * 0.5f + 0.5f) * static_cast<float>(width_));
                  reading.vertexX1 = static_cast<int>((vMaxX * 0.5f + 0.5f) * static_cast<float>(width_));
                  reading.vertexY0 = static_cast<int>((0.5f - vMaxY * 0.5f) * static_cast<float>(height_));
                  reading.vertexY1 = static_cast<int>((0.5f - vMinY * 0.5f) * static_cast<float>(height_));
                }
              }
              reading.ndcWidth = maxX - minX;
              reading.ndcHeight = maxY - minY;
              if (projected) {
                reading.aabbX0 = static_cast<int>((minX * 0.5f + 0.5f) * static_cast<float>(width_));
                reading.aabbX1 = static_cast<int>((maxX * 0.5f + 0.5f) * static_cast<float>(width_));
                reading.aabbY0 = static_cast<int>((0.5f - maxY * 0.5f) * static_cast<float>(height_));
                reading.aabbY1 = static_cast<int>((0.5f - minY * 0.5f) * static_cast<float>(height_));
                reading.x0 = reading.aabbX0;
                reading.x1 = reading.aabbX1;
                reading.y0 = reading.aabbY0;
                reading.y1 = reading.aabbY1;
                reading.extentSource = 0;
                if (wantVertexBox) {
                  if (reading.vertexCountProjected > 0) {
                    reading.x0 = reading.vertexX0;
                    reading.x1 = reading.vertexX1;
                    reading.y0 = reading.vertexY0;
                    reading.y1 = reading.vertexY1;
                    reading.extentSource = 1;
                  } else {
                    // Asked for the drawn extent and got nothing in front of the eye: say
                    // so, and keep the AABB box. A silent fallback here would be a region
                    // that claims to be the drawn one and is not.
                    reading.extentSource = 2;
                  }
                }
                reading.depth = nearest;
                // The shell's overhang, in the unit the shader now works in: PIXELS.
                // It used to be derived (constant x model scale / world-per-pixel); it
                // is now simply the constant, because the expansion itself is applied
                // in clip space where a pixel is a pixel. Published rather than assumed
                // so the gate's tolerance is read from the product, never guessed -- and
                // so that a future change to the constant moves the gate too.
                reading.expandPixels = kOverlaySilhouetteExpandPixels;
                reading.onScreen = reading.x1 >= 0 && reading.y1 >= 0
                  && reading.x0 < static_cast<int>(width_) && reading.y0 < static_cast<int>(height_);
                if (reading.onScreen) {
                  reading.x0 = std::clamp(reading.x0, 0, static_cast<int>(width_) - 1);
                  reading.x1 = std::clamp(reading.x1, 0, static_cast<int>(width_) - 1);
                  reading.y0 = std::clamp(reading.y0, 0, static_cast<int>(height_) - 1);
                  reading.y1 = std::clamp(reading.y1, 0, static_cast<int>(height_) - 1);
                }
              }
              overlayPassReading_.items.push_back(reading);
            }

          }
          overlayPassReading_.vertexCount += entityModelVertexCount_;
        }
        if (hasLines) {
          const UINT lineStride = sizeof(WorldVertex), lineOffset = 0;
          auto* lineBuffer = overlayLineBuffer_.Get();
          context_->IASetInputLayout(overlayLineInputLayout_.Get());
          context_->IASetVertexBuffers(0, 1, &lineBuffer, &lineStride, &lineOffset);
          context_->VSSetShader(overlayLineVertexShader_.Get(), nullptr, 0);
          context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
          context_->Draw(overlayLineVertexCountByDepth_[slot], overlayLineStartByDepth_[slot]);
          ++overlayPassReading_.drawCalls;
          overlayPassReading_.vertexCount += overlayLineVertexCountByDepth_[slot];
          overlayPassReading_.lines += overlayLineVertexCountByDepth_[slot] / 2u;
          context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        }
        return true;
      };
      const bool drewXRay = drawList(0);
      const bool drewTested = drawList(1);
      overlayPassReading_.drewThisFrame = drewXRay || drewTested;
      // The restore that makes "the overlay changed nothing else" mechanical
      // rather than careful: both states the world pass uses are put back here,
      // whatever the overlay did.
      context_->OMSetDepthStencilState(depthStencilState_.Get(), 0);
      context_->RSSetState(worldBackfaceCull_ ? rasterizerState_.Get()
                                              : worldNoCullRasterizerState_.Get());
    }
  };
  if (!worldDiagnosticIsolation_ && projectileVertexBuffer_ && projectileVertexCount_ > 0 && worldVertexShader_ && worldPixelShader_
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
    // T19: batch 0 is the atlas an untiled pass samples. The markers, the
    // projectiles, the entity instances and the legacy model path bind their
    // own view below where they have one; this is what they use when they do
    // not, and it is a world tile rather than the map's display texture.
    auto* worldView = worldAtlasTextures_.empty()
      ? (worldTexture_.view() ? worldTexture_.view() : texture_.view())
      : worldAtlasTextures_.front().view();
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
  // T53: the observed trajectory pass. Its own ordinal, its own buffer, and its
  // own state restore, so the TempEntity pass above keeps exactly the behaviour
  // it had. Nothing here runs unless the setting is on AND points were submitted,
  // which is how "off" keeps the frame byte-identical to a build without this.
  if (!worldDiagnosticIsolation_ && settings_.projectileTrajectory
      && projectileTrackVertexBuffer_ && projectileTrackVertexCount_ > 0
      && worldVertexShader_ && worldPixelShader_ && worldInputLayout_ && worldConstants_) {
    const UINT trackStride = sizeof(WorldVertex), trackOffset = 0;
    auto* trackBuffer = projectileTrackVertexBuffer_.Get();
    context_->IASetInputLayout(worldInputLayout_.Get());
    context_->IASetVertexBuffers(0, 1, &trackBuffer, &trackStride, &trackOffset);
    context_->VSSetShader(worldVertexShader_.Get(), nullptr, 0);
    auto* trackConstants = worldConstants_.Get();
    context_->VSSetConstantBuffers(0, 1, &trackConstants);
    context_->PSSetShader(worldPixelShader_.Get(), nullptr, 0);
    context_->PSSetConstantBuffers(0, 1, &trackConstants);
    auto* trackView = worldAtlasTextures_.empty()
      ? (worldTexture_.view() ? worldTexture_.view() : texture_.view())
      : worldAtlasTextures_.front().view();
    context_->PSSetShaderResources(0, 1, &trackView);
    ID3D11ShaderResourceView* trackBumpView = worldBumpTexture_.view();
    ID3D11ShaderResourceView* trackEnvView = worldEnvTexture_.view();
    context_->PSSetShaderResources(1, 1, &trackBumpView);
    context_->PSSetShaderResources(2, 1, &trackEnvView);
    context_->PSSetSamplers(0, 1, sampler_.GetAddressOf());
    context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
    context_->Draw(projectileTrackVertexCount_, 0);
    context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  }
  ++framePassOrdinal_;
  if (!worldDiagnosticIsolation_ && modelVertexBuffer_ && modelVertexCount_ > 0 && modelVertexShader_ && modelPixelShader_ && modelInputLayout_) {
    context_->IASetInputLayout(modelInputLayout_.Get());
    const UINT modelStride = sizeof(ModelGpuVertex), modelOffset = 0; auto* modelBuffer = modelVertexBuffer_.Get();
    context_->IASetVertexBuffers(0, 1, &modelBuffer, &modelStride, &modelOffset);
    context_->VSSetShader(modelVertexShader_.Get(), nullptr, 0);
    auto* worldConstants = worldConstants_.Get();
    context_->VSSetConstantBuffers(0, 1, &worldConstants);
    context_->PSSetShader(modelPixelShader_.Get(), nullptr, 0);
    // This legacy model path has no entity material binding. Use the renderer's
    // neutral fallback texture instead of sampling the world atlas.
    auto* modelTexture = texture_.view();
    context_->PSSetShaderResources(0, 1, &modelTexture);
    context_->PSSetSamplers(0, 1, sampler_.GetAddressOf());
    auto* modelConstants = modelSkinningConstants_.Get();
    context_->VSSetConstantBuffers(1, 1, &modelConstants);
    context_->Draw(modelVertexCount_, 0);
  }
  // T52: THE OVERLAY IS DRAWN HERE -- pass 6, after the projectile tracks and
  // before the ViewModel pass clears depth. Passes that are not on the
  // contract list (the standalone --model diagnostic above) take no ordinal,
  // or the numbers would drift with a flag nobody set.
  drawOverlayPass();
  // T21: the depth the frame has after the world, water and entity passes and
  // BEFORE the ViewModel pass clears it (the weapon's own pass clears depth so
  // the weapon draws over the map). Read here because this is the last moment
  // at which the depth buffer describes the world the face trace walked; after
  // `drawViewModel()` every world pixel reads 1.0 -- measured, the first
  // version of this readback was taken after the clear and answered 00ffffff
  // at every point.
  if (!capturePath_.empty()) captureDepthBuffer(false);
  // First person, after the world and entity passes and before the capture, so
  // the frame written to disk already contains the weapon. The pass restores the
  // world depth state it overrides, so a later HUD pass (T3) is unaffected.
  drawViewModel();
  context_->OMSetDepthStencilState(depthStencilState_.Get(), 0);
  context_->RSSetState(rasterizerState_.Get());
  // T51: the custom HUD, after everything that draws the world and the weapon
  // and before the frame is captured. It is the LAST pass on purpose: a HUD over
  // a weapon is what a HUD is, and a pass that can only write inside its own
  // scissored rectangles cannot change a world pixel by construction -- which is
  // the assertion hud-check.sh makes by comparing two captures outside those
  // rectangles.
  if (hudEnabled_) drawHudPass();
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

// ---- T51: the custom HUD pass --------------------------------------------

void Renderer::setHudPrimitives(const std::vector<HudPrimitive>& primitives) {
  hudPrimitives_ = primitives;
  // The vertex buffer is rebuilt lazily in drawHudPass, not here: this is called
  // on resize and on import, and a device lost between the two would make an
  // eager upload a failure the caller never asked about.
  hudRectVertexCount_ = 0;
}

void Renderer::setHudRects(const std::vector<HudDrawRect>& rects) {
  // T51 callers keep the bordered-block look: a rect is an unfilled Rect
  // primitive, which is exactly what the old draw path emitted.
  std::vector<HudPrimitive> primitives;
  primitives.reserve(rects.size());
  for (const HudDrawRect& rect : rects) {
    HudPrimitive primitive;
    primitive.kind = HudPrimitiveKind::Rect;
    primitive.x = rect.left;
    primitive.y = rect.top;
    primitive.w = rect.right - rect.left;
    primitive.h = rect.bottom - rect.top;
    for (int channel = 0; channel < 4; ++channel) primitive.colour[channel] = rect.colour[channel];
    primitive.filled = false;
    primitives.push_back(std::move(primitive));
  }
  setHudPrimitives(primitives);
}

void Renderer::drawHudPass() {
  if (!context_ || !target_ || width_ == 0 || height_ == 0) return;
  if (!hudVertexShader_ || !hudPixelShader_ || !hudInputLayout_ || !hudConstants_
      || !hudRasterizerState_ || !hudDepthStencilState_ || !hudBlendState_
      || !hudFontView_ || !hudSampler_) return;
  // An empty primitive list is "this document has no panels this build can
  // place"; it is NOT an error and it is NOT the same as `enabled == false`,
  // which is the user's switch. Both are reported separately by the caller.
  if (hudPrimitives_.empty()) return;
  // Six vertices per quad, 11 floats each: pos2, colour4, local2, uv2, kind1.
  // A text primitive expands to one quad per glyph, so the vertex count is only
  // known after the list is expanded; build into a vector, size the buffer from
  // the result.
  std::vector<float> vertices;
  vertices.reserve(hudPrimitives_.size() * 6u * 11u);
  int regionLeft = width_, regionTop = height_, regionRight = 0, regionBottom = 0;
  auto pushQuad = [&vertices](float left, float top, float right, float bottom,
                              const float colour[4], const float locals[4], const float uv[4],
                              float kind) {
    const float corners[6][2] = {
      {left, top}, {left, bottom}, {right, bottom},
      {left, top}, {right, bottom}, {right, top},
    };
    const float localCorners[6][2] = {
      {locals[0], locals[1]}, {locals[0], locals[3]}, {locals[2], locals[3]},
      {locals[0], locals[1]}, {locals[2], locals[3]}, {locals[2], locals[1]},
    };
    const float uvCorners[6][2] = {
      {uv[0], uv[1]}, {uv[0], uv[3]}, {uv[2], uv[3]},
      {uv[0], uv[1]}, {uv[2], uv[3]}, {uv[2], uv[1]},
    };
    for (int corner = 0; corner < 6; ++corner) {
      vertices.push_back(corners[corner][0]);
      vertices.push_back(corners[corner][1]);
      vertices.push_back(colour[0]);
      vertices.push_back(colour[1]);
      vertices.push_back(colour[2]);
      vertices.push_back(colour[3]);
      vertices.push_back(localCorners[corner][0]);
      vertices.push_back(localCorners[corner][1]);
      vertices.push_back(uvCorners[corner][0]);
      vertices.push_back(uvCorners[corner][1]);
      vertices.push_back(kind);
    }
  };
  for (const HudPrimitive& primitive : hudPrimitives_) {
    const float width = primitive.w;
    const float height = primitive.h;
    if (!(width > 0.0f) || !(height > 0.0f)) continue;
    if (primitive.kind == HudPrimitiveKind::Text) {
      const std::vector<char32_t> codepoints = hudUtf8Decode(primitive.text);
      if (codepoints.empty()) continue;
      const float scale = primitive.scale > 0.0f ? primitive.scale : 1.0f;
      const float glyphWidth = static_cast<float>(kHudFontGlyphWidth) * scale;
      const float glyphHeight = static_cast<float>(kHudFontGlyphHeight) * scale;
      float penX = primitive.x;
      const float penY = primitive.y;
      bool drewAny = false;
      for (const char32_t codepoint : codepoints) {
        int cell = hudFontGlyphIndex(codepoint);
        if (cell < 0) {
          cell = kHudFontFallbackCell;
          ++hudUnmappedGlyphs_;
        }
        const float left = (std::max)(0.0f, penX);
        const float top = (std::max)(0.0f, penY);
        const float right = (std::min)(static_cast<float>(width_), penX + glyphWidth);
        const float bottom = (std::min)(static_cast<float>(height_), penY + glyphHeight);
        if (right > left && bottom > top) {
          const int column = cell % kHudFontAtlasColumns;
          const int row = cell / kHudFontAtlasColumns;
          const float cellU0 = static_cast<float>(column * kHudFontGlyphWidth)
            / static_cast<float>(kHudFontAtlasWidth);
          const float cellU1 = static_cast<float>((column + 1) * kHudFontGlyphWidth)
            / static_cast<float>(kHudFontAtlasWidth);
          const float cellV0 = static_cast<float>(row * kHudFontGlyphHeight)
            / static_cast<float>(kHudFontAtlasHeight);
          const float cellV1 = static_cast<float>((row + 1) * kHudFontGlyphHeight)
            / static_cast<float>(kHudFontAtlasHeight);
          const float uv[4] = {
            cellU0 + (left - penX) / glyphWidth * (cellU1 - cellU0),
            cellV0 + (top - penY) / glyphHeight * (cellV1 - cellV0),
            cellU0 + (right - penX) / glyphWidth * (cellU1 - cellU0),
            cellV0 + (bottom - penY) / glyphHeight * (cellV1 - cellV0),
          };
          const float locals[4] = {0.0f, 0.0f, 1.0f, 1.0f};
          pushQuad(left, top, right, bottom, primitive.colour, locals, uv, 2.0f);
          ++hudGlyphCount_;
          drewAny = true;
          regionLeft = (std::min)(regionLeft, static_cast<int>(left));
          regionTop = (std::min)(regionTop, static_cast<int>(top));
          regionRight = (std::max)(regionRight, static_cast<int>(right + 0.999f));
          regionBottom = (std::max)(regionBottom, static_cast<int>(bottom + 0.999f));
        }
        penX += glyphWidth;
      }
      if (drewAny) {
        ++hudDrawnPrimitives_;
        ++hudTextQuads_;
      }
      continue;
    }
    const float left = (std::max)(0.0f, primitive.x);
    const float top = (std::max)(0.0f, primitive.y);
    const float right = (std::min)(static_cast<float>(width_), primitive.x + width);
    const float bottom = (std::min)(static_cast<float>(height_), primitive.y + height);
    if (!(right > left) || !(bottom > top)) continue;
    const float locals[4] = {
      (left - primitive.x) / width, (top - primitive.y) / height,
      (right - primitive.x) / width, (bottom - primitive.y) / height,
    };
    const float uv[4] = {0.0f, 0.0f, 1.0f, 1.0f};
    float kind = 0.0f;
    if (primitive.kind == HudPrimitiveKind::Circle) kind = 3.0f;
    else if (primitive.filled) kind = 1.0f;
    pushQuad(left, top, right, bottom, primitive.colour, locals, uv, kind);
    if (primitive.kind == HudPrimitiveKind::Rect) ++hudDrawnRects_;
    ++hudDrawnPrimitives_;
    regionLeft = (std::min)(regionLeft, static_cast<int>(left));
    regionTop = (std::min)(regionTop, static_cast<int>(top));
    regionRight = (std::max)(regionRight, static_cast<int>(right + 0.999f));
    regionBottom = (std::max)(regionBottom, static_cast<int>(bottom + 0.999f));
  }
  if (vertices.empty()) return;
  const UINT vertexCount = static_cast<UINT>(vertices.size() / 11u);
  if (hudRectCapacity_ < vertexCount) {
    D3D11_BUFFER_DESC description{};
    description.ByteWidth = static_cast<UINT>(vertices.size() * sizeof(float));
    description.Usage = D3D11_USAGE_DYNAMIC;
    description.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    description.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(device_->CreateBuffer(&description, nullptr, hudVertexBuffer_.ReleaseAndGetAddressOf()))) return;
    hudRectCapacity_ = vertexCount;
  }
  D3D11_MAPPED_SUBRESOURCE mapped{};
  if (FAILED(context_->Map(hudVertexBuffer_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return;
  std::memcpy(mapped.pData, vertices.data(), vertices.size() * sizeof(float));
  context_->Unmap(hudVertexBuffer_.Get(), 0);
  hudRectVertexCount_ = vertexCount;
  hudLastRegion_.x = regionLeft;
  hudLastRegion_.y = regionTop;
  hudLastRegion_.w = regionRight - regionLeft;
  hudLastRegion_.h = regionBottom - regionTop;
  hudLastRegion_.empty = hudDrawnPrimitives_ == 0;
  // The whole pass is scissored to the union of the primitives it is about to
  // draw. That is belt and braces next to the fact that it only emits those
  // primitives: the scissor is what makes "cannot paint outside" a hardware
  // property rather than an arithmetic argument, and the reading is printed so
  // the gate can see it was set.
  D3D11_RECT scissor{};
  scissor.left = hudLastRegion_.x;
  scissor.top = hudLastRegion_.y;
  scissor.right = hudLastRegion_.x + hudLastRegion_.w;
  scissor.bottom = hudLastRegion_.y + hudLastRegion_.h;
  context_->RSSetScissorRects(1, &scissor);
  hudScissorApplied_ = true;
  const float screen[4] = { static_cast<float>(width_), static_cast<float>(height_),
    1.0f / static_cast<float>(width_), 1.0f / static_cast<float>(height_) };
  context_->UpdateSubresource(hudConstants_.Get(), 0, nullptr, screen, 0, 0);
  context_->OMSetDepthStencilState(hudDepthStencilState_.Get(), 0);
  context_->RSSetState(hudRasterizerState_.Get());
  const float blendFactor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  context_->OMSetBlendState(hudBlendState_.Get(), blendFactor, 0xFFFFFFFFu);
  context_->IASetInputLayout(hudInputLayout_.Get());
  const UINT stride = sizeof(float) * 11u, offset = 0;
  auto* buffer = hudVertexBuffer_.Get();
  context_->IASetVertexBuffers(0, 1, &buffer, &stride, &offset);
  context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  context_->VSSetShader(hudVertexShader_.Get(), nullptr, 0);
  context_->PSSetShader(hudPixelShader_.Get(), nullptr, 0);
  auto* constants = hudConstants_.Get();
  context_->VSSetConstantBuffers(0, 1, &constants);
  // The font atlas is the only texture this pass may sample. Binding it here and
  // clearing it after the draw keeps the "a HUD frame differs from a HUD-less
  // frame only in the pixels the pass drew" claim true for the resource slots as
  // well as for the pixels.
  auto* fontView = hudFontView_.Get();
  auto* fontSampler = hudSampler_.Get();
  context_->PSSetShaderResources(0, 1, &fontView);
  context_->PSSetSamplers(0, 1, &fontSampler);
  context_->Draw(hudRectVertexCount_, 0);
  hudPassOrdinal_ = ++framePassOrdinal_;
  // Restore the frame's own states so a HUD enabled frame and a HUD disabled
  // frame differ only in the pixels the pass drew: a leftover scissor would
  // clip the NEXT frame's world draw, which is the exact class of defect the
  // world-comparison assertion exists to catch.
  D3D11_RECT full{};
  full.left = 0;
  full.top = 0;
  full.right = static_cast<LONG>(width_);
  full.bottom = static_cast<LONG>(height_);
  context_->RSSetScissorRects(1, &full);
  context_->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFFu);
  context_->OMSetDepthStencilState(depthStencilState_.Get(), 0);
  context_->RSSetState(rasterizerState_.Get());
  ID3D11ShaderResourceView* noTexture = nullptr;
  context_->PSSetShaderResources(0, 1, &noTexture);
  ID3D11SamplerState* noSampler = nullptr;
  context_->PSSetSamplers(0, 1, &noSampler);
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

