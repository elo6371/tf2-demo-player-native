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

