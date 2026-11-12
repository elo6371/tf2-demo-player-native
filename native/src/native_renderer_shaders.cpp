// T77: the shader text moved out of Renderer::createPipeline().
#include "native_renderer_shaders.h"

namespace tf2::native::shaders {

  extern const char vertexSource[] = R"HLSL(
struct Output { float4 position : SV_POSITION; float2 uv : TEXCOORD0; };
Output main(uint id : SV_VertexID) {
  float2 positions[6] = { float2(-1,-1), float2(-1,1), float2(1,1), float2(-1,-1), float2(1,1), float2(1,-1) };
  Output output;
  output.position = float4(positions[id], 0, 1);
  output.uv = float2((positions[id].x + 1) * 0.5, (1 - positions[id].y) * 0.5);
  return output;
})HLSL";

  extern const char pixelSource[] = R"HLSL(
Texture2D texture0 : register(t0);
Texture2D bumpTexture : register(t1);
Texture2D envTexture : register(t2);
SamplerState sampler0 : register(s0);
float4 main(float4 position : SV_POSITION, float2 uv : TEXCOORD0) : SV_TARGET {
  return texture0.Sample(sampler0, uv);
})HLSL";

  extern const char worldVertexSource[] = R"HLSL(
cbuffer WorldConstants : register(b0) {
  row_major float4x4 mvp;
  float4 lightDirectionAndMode;
  float4 cameraPositionAndSpecular;
  float4 materialFeatures;
  float4 lightmapFeatures;
  float4 envFeatures;
  // T101: environment fog. fogParams = (start, end, maxDensity, enabled) in map
  // units; fogScale converts a box-space delta to map space. Copied verbatim
  // into the pixel shader's cbuffer below -- the two blocks are the same buffer.
  float4 fogParams;
  float4 fogColor;
  float4 fogScale;
};
struct Input { float3 position : POSITION; float4 colour : COLOR0; float2 uv : TEXCOORD0; float3 normal : NORMAL0; float2 lightUv : TEXCOORD1; float blend : TEXCOORD4; float blendModulate : TEXCOORD5; float4 feature : TEXCOORD6; float4 tangent : TEXCOORD7; };
struct Output { float4 position : SV_POSITION; float4 colour : COLOR0; float2 uv : TEXCOORD0; float3 normal : NORMAL0; float3 worldPosition : TEXCOORD2; float2 lightUv : TEXCOORD3; float blend : TEXCOORD4; float blendModulate : TEXCOORD5; float4 feature : TEXCOORD6; float4 tangent : TEXCOORD7; };
Output main(Input input) { Output output; output.position = mul(float4(input.position, 1.0), mvp); output.colour = input.colour; output.uv = input.uv; output.normal = input.normal; output.worldPosition = input.position; output.lightUv = input.lightUv; output.blend = input.blend; output.blendModulate = input.blendModulate; output.feature = input.feature; output.tangent = input.tangent; return output; }
)HLSL";

  extern const char worldPixelSource[] = R"HLSL(
cbuffer WorldConstants : register(b0) {
  row_major float4x4 mvp;
  float4 lightDirectionAndMode;
  float4 cameraPositionAndSpecular;
  float4 materialFeatures;
  float4 lightmapFeatures;
  float4 envFeatures;
  float4 fogParams;
  float4 fogColor;
  float4 fogScale;
};
cbuffer ModelMaterialProxy : register(b1) {
  float4 texture2Scroll;
  float4 materialProxyColor;
  float4 materialProxyFlags;
};
Texture2D texture0 : register(t0);
Texture2D modelSecondTexture : register(t11);
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
  // T96: the texel is sampled as float4 and its alpha kept. The VTF decoder
  // already produced that alpha and the atlas is R8G8B8A8_UNORM with four
  // channels copied per texel (world_tile_atlas.cpp:271), so `$translucent` and
  // `$alphatest` only ever needed a consumer -- this is it. `sampled` stays the
  // rgb triple every existing arm reads, so the opaque path is unchanged.
  float4 sampledTexel = texture0.Sample(sampler0, uv);
  float3 sampled = sampledTexel.rgb;
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
  // Map-only unlit/model blend modes. World and player feature masks never set
  // these bits. UnlitTwoTexture multiplies both textures and adds with ONE/ONE;
  // Modulate interpolates from neutral grey through the texture's alpha before
  // the output merger multiplies it into the framebuffer (twice for $mod2x).
  if ((featureMask & 512u) != 0u) {
    float3 unlitColour = sampled * colour.rgb;
    float unlitAlpha = sampledTexel.a;
    if ((featureMask & 128u) != 0u) {
      float2 secondUv = uv + texture2Scroll.xy * materialProxyFlags.x;
      float4 secondTexel = modelSecondTexture.Sample(sampler0, secondUv);
      unlitColour *= secondTexel.rgb * lerp(float3(1.0, 1.0, 1.0),
        materialProxyColor.rgb, materialProxyFlags.y);
      unlitAlpha *= secondTexel.a;
    }
    if ((featureMask & 16u) != 0u && sampledTexel.a < 0.5) discard;
    const bool alphaBlend = (featureMask & (8u | 64u)) != 0u;
    return float4(unlitColour, alphaBlend ? unlitAlpha : 1.0);
  }
  if ((featureMask & 1024u) != 0u) {
    return float4(lerp(float3(0.5, 0.5, 0.5), saturate(sampled * colour.rgb),
      saturate(sampledTexel.a * colour.a)), 1.0);
  }
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
  // T62: the ambient here is the map's lightmap intensity, which is a per-map
  // compensation for lightmapped world faces. A draw with NO lightmap uv (the
  // entity vertex shader writes lightUv = (-1,-1)) never reaches the lightmap
  // sample below, so it would keep this factor as its ambient: on
  // koth_bagel_rc11, intensity=0.386 multiplied every player body by 0.386
  // before the directional term and a BLU Pyro's suit landed at mean luma
  // 8/255 -- a black silhouette that was not a texture, mesh or bone defect.
  // The neutral ambient is chosen by the ENTITY PASS, which re-uploads the
  // frame constants with lightmapFeatures.x = 1.0 for its own draws (see
  // native_renderer_frame.cpp); the world pass still reads this factor, so
  // this shader keeps one meaning for lightmapFeatures.x.
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
  // T96: the world alpha output. The two keys ride `feature.x` (TEXCOORD6,
  // written by the renderer from the material the tile belongs to): bit 16 is
  // `$alphatest`, bit 8 is `$translucent`. A cutout has no blend state and draws
  // in the opaque sub-batch; its hole comes from `discard`, the same rule the
  // viewmodel pixel shader uses at its own `$alphatest` arm (line 802). A
  // blended surface writes the texel's own alpha and is drawn by the separate
  // blended sub-batch with SrcAlpha/InvSrcAlpha and depth writes off. Every
  // vertex without bit 8 keeps 1.0, so the opaque frame is unchanged.
  if ((featureMask & 16u) != 0u && sampledTexel.a < 0.5) discard;
  const float outputAlpha = (featureMask & 8u) != 0u ? sampledTexel.a : 1.0f;
  // T101: environment fog, on bit 32 (32/64/128 were free) AND on the constant
  // itself. The vertex bit says "this surface is in the fogged set"; the
  // constant's w says the pass is fogged at all. Both are required so a stale
  // bit in a vertex buffer cannot fog a frame whose pass was uploaded unfogged,
  // and so a disabled pass costs one integer test per pixel, not a branch into
  // uninitialised values. Only the rgb is blended: alpha is what the $translucent
  // path just decided, and fog is a colour it sits behind, not a transparency.
  //
  // The distance is measured in MAP units. worldPosition is the normalised box
  // vertex (the vertex shader copies input.position straight through), so the
  // delta is multiplied by the same two scales the vertex convert used
  // (native_renderer_world.cpp:93-101): 1/horizontalScale for x/y, spanZ/0.8
  // for z. One scalar would put the fog at a different depth in every map whose
  // long axis is not 0.8 wide.
  if ((featureMask & 32u) != 0u && fogParams.w > 0.5) {
    float3 fogDelta = (cameraPositionAndSpecular.xyz - worldPosition) * fogScale.xyz;
    float fogDistance = length(fogDelta);
    float fogAmount = saturate((fogDistance - fogParams.x) / max(fogParams.y - fogParams.x, 1.0e-3));
    fogAmount = min(fogAmount, fogParams.z);
    return float4(lerp(albedo * lighting, fogColor.rgb, fogAmount), outputAlpha);
  }
  return float4(albedo * lighting, outputAlpha);
}
)HLSL";

  extern const char modelVertexSource[] = R"HLSL(
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

  extern const char modelPixelSource[] = R"HLSL(
struct Input { float4 position : SV_POSITION; float4 colour : COLOR0; float2 uv : TEXCOORD0; float3 normal : NORMAL0; };
float4 main(Input input) : SV_TARGET {
  float3 normal = normalize(input.normal);
  float diffuse = saturate(dot(normal, normalize(float3(0.35, 0.55, 0.75))));
  float lighting = 0.62 + 0.38 * diffuse;
  return float4(input.colour.rgb * lighting, input.colour.a);
}
)HLSL";

  extern const char overlayLineVertexSource[] = R"HLSL(
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

  extern const char overlayVertexSource[] = R"HLSL(
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

// Read one bone as four float4s. HLSL packs matrices column-major by default,
// structured-buffer elements included, so `StructuredBuffer<float4x4>` read
// the column-major upload as if it were row-major: the rotation inverted and
// the translation was read from the wrong four floats. The viewmodel path was
// fixed this way in T18; the entity/overlay paths kept the broken read and
// collapsed every player into a blob at the model origin. Four float4s have one
// packing and indexing them by hand cannot disagree with `toColumnMajor`.
StructuredBuffer<float4> boneColumns : register(t4);
float4 skinByBone(float3 vertexPosition, float weightW, uint boneSlot) {
  uint first = boneSlot * 4;
  float4 c0 = boneColumns[first + 0];
  float4 c1 = boneColumns[first + 1];
  float4 c2 = boneColumns[first + 2];
  float4 c3 = boneColumns[first + 3];
  return float4(
    vertexPosition.x * c0.x + vertexPosition.y * c1.x + vertexPosition.z * c2.x + weightW * c3.x,
    vertexPosition.x * c0.y + vertexPosition.y * c1.y + vertexPosition.z * c2.y + weightW * c3.y,
    vertexPosition.x * c0.z + vertexPosition.y * c1.z + vertexPosition.z * c2.z + weightW * c3.z,
    weightW);
}
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
          // T62 root cause: every weight has to scale the WHOLE bone transform,
          // not just the translation term. This used to pass the weight in as
          // skinByBone's weightW, which multiplies only c3 (the translation) and
          // leaves the rotation unscaled -- so a two-bone vertex got R0*p + R1*p
          // + w0*t0 + w1*t1, roughly twice its correct position, and a three-bone
          // vertex up to three times. Single-bone vertices were unaffected, which
          // is why the body looked planted while the shoulders, hips and knees
          // (the multi-bone ones) shot out into the spike fan. The viewmodel copy
          // of this loop already had the weighted form; these two did not.
          skinnedPos += skinByBone(input.position, 1.0, bone).xyz * normalizedWeights[i];
          skinnedNormal += skinByBone(input.normal, 0.0, bone).xyz * normalizedWeights[i];
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

  extern const char overlayPixelSource[] = R"HLSL(
float4 main(float4 position : SV_POSITION, float4 colour : COLOR0) : SV_TARGET {
  return colour;
}
)HLSL";

  extern const char entityModelVertexSource[] = R"HLSL(
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

// Read one bone as four float4s. HLSL packs matrices column-major by default,
// structured-buffer elements included, so `StructuredBuffer<float4x4>` read
// the column-major upload as if it were row-major: the rotation inverted and
// the translation was read from the wrong four floats. The viewmodel path was
// fixed this way in T18; the entity/overlay paths kept the broken read and
// collapsed every player into a blob at the model origin. Four float4s have one
// packing and indexing them by hand cannot disagree with `toColumnMajor`.
StructuredBuffer<float4> boneColumns : register(t4);
float4 skinByBone(float3 vertexPosition, float weightW, uint boneSlot) {
  uint first = boneSlot * 4;
  float4 c0 = boneColumns[first + 0];
  float4 c1 = boneColumns[first + 1];
  float4 c2 = boneColumns[first + 2];
  float4 c3 = boneColumns[first + 3];
  return float4(
    vertexPosition.x * c0.x + vertexPosition.y * c1.x + vertexPosition.z * c2.x + weightW * c3.x,
    vertexPosition.x * c0.y + vertexPosition.y * c1.y + vertexPosition.z * c2.y + weightW * c3.y,
    vertexPosition.x * c0.z + vertexPosition.y * c1.z + vertexPosition.z * c2.z + weightW * c3.z,
    weightW);
}
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
          // T62 root cause: every weight has to scale the WHOLE bone transform,
          // not just the translation term. This used to pass the weight in as
          // skinByBone's weightW, which multiplies only c3 (the translation) and
          // leaves the rotation unscaled -- so a two-bone vertex got R0*p + R1*p
          // + w0*t0 + w1*t1, roughly twice its correct position, and a three-bone
          // vertex up to three times. Single-bone vertices were unaffected, which
          // is why the body looked planted while the shoulders, hips and knees
          // (the multi-bone ones) shot out into the spike fan. The viewmodel copy
          // of this loop already had the weighted form; these two did not.
          skinnedPos += skinByBone(input.position, 1.0, bone).xyz * normalizedWeights[i];
          skinnedNormal += skinByBone(input.normal, 0.0, bone).xyz * normalizedWeights[i];
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
  output.feature = float4((float)input.boneInfo.w, 0.0, 0.0, 0.0);
  output.tangent = float4(1.0, 0.0, 0.0, 0.0);
  return output;
}
)HLSL";

  extern const char viewModelVertexSource[] = R"HLSL(
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

  extern const char viewModelPixelSource[] = R"HLSL(
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

  extern const char hudVertexSource[] = R"HLSL(
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

  extern const char hudPixelSource[] = R"HLSL(
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

}  // namespace tf2::native::shaders
