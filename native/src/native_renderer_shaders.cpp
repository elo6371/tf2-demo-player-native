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
  // N21: the map's lights, appended after every block above so no offset the
  // shading arithmetic already uses moves. Same order as WorldConstantsData in
  // native_renderer_frame.cpp; `sunDirectionAndMode.w` is the switch and the
  // eight fields below it are only read when it is 1.
  float4 sunDirectionAndMode;      // xyz surface -> light, w enabled
  float4 sunColorAndIntensity;     // rgb, w brightness/1000
  float4 ambientColorAndIntensity; // rgb, w brightness/1000
  float4 lightCountAndParams;      // x = lights uploaded for this pane
  float4 lightPositionRadius[16];   // xyz box position, w radius
  float4 lightColorIntensity[16];   // rgb colour*intensity, w cosOuter
  float4 lightDirectionAndCone[16]; // xyz spot axis, w cosInner
  float4 lightConeParams[16];       // x exponent, y isSpot
};
struct Input { float3 position : POSITION; float4 colour : COLOR0; float2 uv : TEXCOORD0; float3 normal : NORMAL0; float2 lightUv : TEXCOORD1; float blend : TEXCOORD4; float blendModulate : TEXCOORD5; float4 feature : TEXCOORD6; float4 tangent : TEXCOORD7; float4 tileRect : TEXCOORD8; };
struct Output { float4 position : SV_POSITION; float4 colour : COLOR0; float2 uv : TEXCOORD0; float3 normal : NORMAL0; float3 worldPosition : TEXCOORD2; float2 lightUv : TEXCOORD3; float blend : TEXCOORD4; float blendModulate : TEXCOORD5; float4 feature : TEXCOORD6; float4 tangent : TEXCOORD7; float4 tileRect : TEXCOORD8; nointerpolation uint ambientSlot : TEXCOORD9; };
Output main(Input input) { Output output; output.position = mul(float4(input.position, 1.0), mvp); output.colour = input.colour; output.uv = input.uv; output.normal = input.normal; output.worldPosition = input.position; output.lightUv = input.lightUv; output.blend = input.blend; output.blendModulate = input.blendModulate; output.feature = input.feature; output.tangent = input.tangent; output.tileRect = input.tileRect; output.ambientSlot = 0xFFFFFFFFu; return output; }
)HLSL";

  // N05 step 3: the stream output channel.
  //
  // The world vertex shader above is left byte for byte alone -- it is the one
  // that draws, and a capture that changed it would be measuring a different
  // draw. This geometry shader declares the world VS output struct as its input
  // and copies every field straight through, so nothing downstream sees a
  // different vertex; the only thing narrowed is the stream output declaration,
  // which emits position (clip xyzw) and nothing else. A pixel that disagrees
  // with the CPU prediction can then be attributed to the vertex stage with a
  // reading rather than by inference.
  //
  // The output stream is a TRIANGLE stream, not a point stream, and that is the
  // whole reason the captured frame is still the frame: after a geometry shader
  // the rasteriser draws the primitive the output stream type declares, so a
  // point stream would replace the triangle under test with three dots and the
  // bitmap would no longer be the picture these clip values describe.
  //
  // The nine input fields must match the world VS Output exactly, including the
  // order of the semantics: a GS input signature that drifts from the VS output
  // signature is a silently mismatched pairing (the stage still runs, reading
  // whatever the VS wrote under that semantic name), which is why this block is
  // written out in full rather than trimmed to the one field that is used.
  extern const char worldClipCaptureGeometrySource[] = R"HLSL(
struct WorldOutput {
  float4 position : SV_POSITION;
  float4 colour : COLOR0;
  float2 uv : TEXCOORD0;
  float3 normal : NORMAL0;
  float3 worldPosition : TEXCOORD2;
  float2 lightUv : TEXCOORD3;
  float blend : TEXCOORD4;
  float blendModulate : TEXCOORD5;
  float4 feature : TEXCOORD6;
  float4 tangent : TEXCOORD7;
  float4 tileRect : TEXCOORD8;
  nointerpolation uint ambientSlot : TEXCOORD9;
};
struct ClipSample {
  float4 position : SV_POSITION;
  float4 colour : COLOR0;
  float2 uv : TEXCOORD0;
  float3 normal : NORMAL0;
  float3 worldPosition : TEXCOORD2;
  float2 lightUv : TEXCOORD3;
  float blend : TEXCOORD4;
  float blendModulate : TEXCOORD5;
  float4 feature : TEXCOORD6;
  float4 tangent : TEXCOORD7;
  float4 tileRect : TEXCOORD8;
  nointerpolation uint ambientSlot : TEXCOORD9;
};
[maxvertexcount(3)]
void main(triangle WorldOutput input[3], inout TriangleStream<ClipSample> stream) {
  [unroll] for (uint i = 0; i < 3; ++i) {
    ClipSample sample;
    // Every field is copied through, not just the one that gets streamed out.
    // The pixel shader consumes the interpolants below SV_POSITION, and a
    // geometry shader that dropped them would leave the pixel stage reading
    // elements its producer never wrote -- the captured frame would then differ
    // from the frame without the channel for a reason that has nothing to do
    // with the vertices under test.
    sample.position = input[i].position;
    sample.colour = input[i].colour;
    sample.uv = input[i].uv;
    sample.normal = input[i].normal;
    sample.worldPosition = input[i].worldPosition;
    sample.lightUv = input[i].lightUv;
    sample.blend = input[i].blend;
    sample.blendModulate = input[i].blendModulate;
    sample.feature = input[i].feature;
    sample.tangent = input[i].tangent;
    sample.tileRect = input[i].tileRect;
    sample.ambientSlot = input[i].ambientSlot;
    stream.Append(sample);
  }
  stream.RestartStrip();
}
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
  // N21: the map's lights, appended after every block above so no offset the
  // shading arithmetic already uses moves. Same order as WorldConstantsData in
  // native_renderer_frame.cpp; `sunDirectionAndMode.w` is the switch and the
  // eight fields below it are only read when it is 1.
  float4 sunDirectionAndMode;      // xyz surface -> light, w enabled
  float4 sunColorAndIntensity;     // rgb, w brightness/1000
  float4 ambientColorAndIntensity; // rgb, w brightness/1000
  float4 lightCountAndParams;      // x = lights uploaded for this pane
  float4 lightPositionRadius[16];   // xyz box position, w radius
  float4 lightColorIntensity[16];   // rgb colour*intensity, w cosOuter
  float4 lightDirectionAndCone[16]; // xyz spot axis, w cosInner
  float4 lightConeParams[16];       // x exponent, y isSpot
};
cbuffer ModelMaterialProxy : register(b1) {
  float4 texture2Scroll;
  float4 materialProxyColor;
  float4 materialProxyFlags;
};
// T105: the model lighting block. It is bound HERE too because the entity and
// prop instances -- the meshes this block exists for -- are drawn through this
// world pixel shader, not through the legacy model one. It sits at b2 and not
// at b1 (the register the model and viewmodel pixel shaders use) because b1 in
// this shader is the material proxy above; the CPU binds zeros here for every
// world-geometry draw, so a lightmapped face keeps the arithmetic it had.
cbuffer ModelLightingConstants : register(b2) {
  float4 ambientCube[6];            // +X -X +Y -Y +Z(sky) -Z(ground), rgb colour
  float4 sunDirectionAndIntensity;  // xyz = direction TO the sun, w = intensity
  float4 lightingParams;            // x = lighting arm on, y = normal probe
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
// T105/task-37 Half B: the per-leaf ambient cube table. Slot s owns rows
// [6*s, 6*s+6), one float4 per face in the same (+X,-X,+Y,-Y,+Z,-Z) order the
// six face colours in b2 above use, so the two cubes are interchangeable
// arguments to modelLightingRgb() below. t12 is the first register this shader
// has free -- t0..t11 are all taken by the lines above -- and nothing else in
// the tree declares t12 or higher. Only the entity/prop pass binds it; every
// other draw leaves it unset, and every vertex stage that does not resolve a
// leaf writes the sentinel, so the sample is branched away rather than read.
StructuredBuffer<float4> leafAmbientCubeFaces : register(t12);
SamplerState sampler0 : register(s0);
float3 safeNormalize(float3 value) {
  float lengthSquared = dot(value, value);
  if (!(lengthSquared > 1e-8) || !(lengthSquared < 1e8)) return float3(0.0, 0.0, 0.0);
  return value * rsqrt(lengthSquared);
}
// T105: the ambient cube + Half-Lambert sun, in the units this pass already
// uses (0..2). Each of the six face colours is weighted by the SQUARED
// component of the normal, so the weights sum to 1 for any unit normal and no
// normal can be lit above the brightest face; the sun term is
// `saturate(N.L * 0.5 + 0.5)` squared, which is continuous through the
// terminator instead of clipping to black. `n` reaches this function in WORLD
// space for the entity and prop draws this block exists for -- the entity
// vertex shader rotates the bind-pose normal by the instance normal rows.
float3 modelLightingRgb(float3 normalWorld, float4 cube[6], float4 sun) {
  float3 n = safeNormalize(normalWorld);
  float3 ambient = (n.x >= 0.0 ? cube[0].rgb : cube[1].rgb) * (n.x * n.x)
    + (n.y >= 0.0 ? cube[2].rgb : cube[3].rgb) * (n.y * n.y)
    + (n.z >= 0.0 ? cube[4].rgb : cube[5].rgb) * (n.z * n.z);
  float3 sunDirection = safeNormalize(sun.xyz);
  float halfLambert = saturate(dot(n, sunDirection) * 0.5 + 0.5);
  return ambient + sun.w * (halfLambert * halfLambert);
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
float4 main(float4 position : SV_POSITION, float4 colour : COLOR0, float2 uv : TEXCOORD0, float3 normal : NORMAL0, float3 worldPosition : TEXCOORD2, float2 lightUv : TEXCOORD3, float blend : TEXCOORD4, float blendModulate : TEXCOORD5, float4 feature : TEXCOORD6, float4 tangent : TEXCOORD7, float4 tileRect : TEXCOORD8, nointerpolation uint ambientSlot : TEXCOORD9) : SV_TARGET {
  // G7: the tiling is resolved here rather than by the uploader. The gradient
  // is taken from the unwrapped uv (times the tile span) and not from the
  // wrapped coordinate, because frac() steps at a tile boundary and a
  // derivative read across that step would pick a coarser mip there and draw a
  // seam line. Derivatives are computed before any of the mode branches below,
  // so they are read in uniform control flow.
  const bool g7TileWrap = (tileRect.z > 0.0 || tileRect.w > 0.0);
  const float2 g7Uv = g7TileWrap ? tileRect.xy + frac(uv) * tileRect.zw : uv;
  const float2 g7Ddx = ddx(uv) * tileRect.zw;
  const float2 g7Ddy = ddy(uv) * tileRect.zw;
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
  // N05 step 2: the magenta isolation view. Placed above every other arm because
  // it is the one that must be unambiguous: a pixel is 255,0,255 only if this
  // pass wrote it.
  if (envFeatures.x > 11.5) return float4(1.0, 0.0, 1.0, 1.0);
  if (envFeatures.x > 10.5) {
    return float4(colour.rgb, 1.0);
  }
  if (envFeatures.x > 9.5) {
    return float4(1.0, 0.0, 1.0, 1.0);
  }
  if (envFeatures.x > 8.5) {
    uint entityDetail = ((uint)(feature.x + 0.5)) & 4u;
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
  float4 sampledTexel = g7TileWrap ? texture0.SampleGrad(sampler0, g7Uv, g7Ddx, g7Ddy) : texture0.Sample(sampler0, uv);
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
      float4 mask = g7TileWrap ? blendModulateTexture.SampleGrad(sampler0, g7Uv, g7Ddx, g7Ddy) : blendModulateTexture.Sample(sampler0, uv);
      float lower = saturate(mask.g - mask.r);
      float upper = saturate(mask.g + mask.r);
      // smoothstep with a zero-width range is undefined in HLSL and would turn
      // a mask texel into a NaN; the floor keeps the curve and keeps the frame
      // a picture of the map.
      weight = smoothstep(lower, lower + max(upper - lower, 1.0e-5), weight);
    }
    blendWeight = saturate(weight);
    sampled = lerp(sampled, (g7TileWrap ? blendTexture.SampleGrad(sampler0, g7Uv, g7Ddx, g7Ddy) : blendTexture.Sample(sampler0, uv)).rgb, blendWeight);
  }
  float lightmapFactor = max(lightmapFeatures.x, 0.0);
  float3 albedo = lerp(colour.rgb, sampled * colour.rgb, colour.a);
  // N06 probe 6/7 (forensic, 2026-11-18): one vector per arm, so no reading can
  // be attributed to the wrong channel. View 16 is the vertex colour the albedo is
  // lerped from plus the blend weight; 17 is the albedo next to the sample, the
  // sample's alpha and the vertex alpha. Both ride the gap between the terms probe
  // (1.52) and the lightmap probe (1.62).
  if (envFeatures.x > 1.585) return float4(colour.r, colour.g, colour.a, blendWeight);
  if (envFeatures.x > 1.565) return float4(albedo.r, sampled.r, sampledTexel.a, colour.a);
  // N06 probe: the terms whose product is the albedo, side by side, so a black
  // pixel can be attributed to the vertex colour or to the blend layer instead
  // of being guessed at. R = albedo, G = the vertex colour's red, B = the blend
  // weight. View 12 is the same pixel one step earlier: R = colour.a,
  // G = colour.g, B = the sampled texel's alpha. Both are placed after the
  // declarations they read: the first cut sat above them and HLSL refused it.
  // The two probe modes ride 1.9 and 2.4 because every other value is already
  // claimed by an arm above: 12 and up is magenta, 3.5-11.5 are the T11/T26/T61
  // views, 2.5-3.5 is the environment cube and 0.5-1.5 is the raw texel. The
  // gap between them is dead on the product path, so nothing else can read it.
  if (envFeatures.x > 2.3) return float4(colour.a, colour.g, sampledTexel.a, 1.0);
  if (envFeatures.x > 1.85) return float4(albedo.r, colour.r, blendWeight, 1.0);
  // T27: `$detail`. The layer is combined with the SAME TextureCombine the SDK
  // uses (common_ps_fxc.h), at the point the SDK calls it: on the base colour
  // with the vertex tint already applied, before lighting (`albedo =
  // TextureCombine(albedo, detailColor, DETAIL_BLEND_MODE,
  // g_DetailBlendFactor)`). `$detailscale` is baked into the detail tile on the
  // CPU -- this pass has one atlas uv per vertex, so the repetition has to live
  // in the pixels rather than in a second texcoord. A material with no detail
  // tile has bit 2 clear and never samples here.
  // Round, never truncate: `feature` is interpolated, so a mask 16 can arrive as
  // 15.9999998 and truncate to 15 -- losing `$alphatest`, gaining detail (black).
  uint featureMask = (uint)(feature.x + 0.5);
  // task-34: `$alphatestreference`, riding featureMask bits 12..19 as
  // q = round(reference * 255), with bit 20 saying a reference is present. No
  // bit means the VMT declared none (or exactly 0.5, Source's default), and the
  // literal 0.5 below is then the same constant the old code compared against --
  // so the default path is bit-identical rather than re-derived from 128/255,
  // which a bilinear sample can land between. 0.2 decodes exactly (51/255).
  const float alphaTestReference = ((featureMask >> 20) & 1u) != 0u
    ? float((featureMask >> 12) & 255u) / 255.0f : 0.5f;
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
    if ((featureMask & 16u) != 0u && sampledTexel.a < alphaTestReference) discard;
    const bool alphaBlend = (featureMask & (8u | 64u)) != 0u;
    return float4(unlitColour, alphaBlend ? unlitAlpha : 1.0);
  }
  if ((featureMask & 1024u) != 0u) {
    return float4(lerp(float3(0.5, 0.5, 0.5), saturate(sampled * colour.rgb),
      saturate(sampledTexel.a * colour.a)), 1.0);
  }
  if ((featureMask & 4u) != 0u) {
    float4 detailColor = g7TileWrap ? detailAtlas.SampleGrad(sampler0, g7Uv, g7Ddx, g7Ddy) : detailAtlas.Sample(sampler0, uv);
    albedo = combineDetail(albedo, detailColor, (uint)(feature.y + 0.5), feature.z);
  }
  // T21: mode 6 is the ALBEDO the material path produced -- both layers of a
  // blend material already mixed, the detail layer combined, the flat branch for
  // a material with no tile, and nothing else. Lighting, the lightmap and
  // self-illum are deliberately NOT applied: this view exists so a gate can
  // compare one screen pixel with one texel the CPU sampled from the same tile,
  // which is the only way to tell "the wall is dark because the material is
  // dark" from "the wall is dark because it sampled the wrong tile".
  if (envFeatures.y > 0.5) return float4(albedo, 1.0);
  // N06 probe 8: the same albedo one branch later, next to the vertex colour it
  // came from, so view 17 and the albedo view can be compared in one frame.
  if (envFeatures.x > 1.545) return float4(albedo.r, colour.r, sampled.r, sampledTexel.a);
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
  // N06 probe 3: attribute a black product to the lightmap instead of guessing.
  // R = the light colour actually multiplied in, G = the raw lightmap texel's
  // red, B = whether this pass samples the lightmap at all (lightmapFeatures.w).
  // It rides 1.7: every arm above claims another value (2.3 and up, 1.85-2.3,
  // 0.5-1.5, 2.5-3.5 and 3.5+), so 1.5-1.85 is the last free gap.
  if (envFeatures.x > 1.65) {
    return float4(lightColour.r,
      lightmapTexture.Sample(sampler0, max(lightUv, float2(0.0, 0.0))).r,
      lightmapFeatures.w, 1.0);
  }
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
)HLSL"
    // N21: the map-light path pushed this half past the same cap again (C2026
    // was reported at the old end of the literal), so it is carried as a third
    // adjacent literal. Same rule as the split above: the pieces are
    // concatenated by the compiler and must stay separate.
    R"HLSL(
  const bool hasLayer1 = (featureMask & 1u) != 0u;
  float3 nMaterial = tangentNormal((g7TileWrap ? bumpAtlas.SampleGrad(sampler0, g7Uv, g7Ddx, g7Ddy) : bumpAtlas.Sample(sampler0, uv)).xyz, Ng, tangent);
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
  float3 n2 = tangentNormal((g7TileWrap ? bumpAtlas2.SampleGrad(sampler0, g7Uv, g7Ddx, g7Ddy) : bumpAtlas2.Sample(sampler0, uv)).xyz, Ng, tangent);
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
  // N21: the map's lights. Two exclusive paths, and the off path is the old
  // arithmetic untouched: with sunDirectionAndMode.w at 0 the new branch never
  // runs and the result is exactly lerp(1.0, 0.62 + 0.38 * N.L,
  // lightDirectionAndMode.w), which is what every pinned capture was made with.
  //
  // With the map's lights on, the invented direction becomes the map's sun (the
  // specular term below keeps using it as the key light) and the lighting is a
  // three-term sum in the map's own units:
  //   ambient = _ambient rgb * (_ambient brightness / 1000)
  //   sun     = _light rgb * (_light brightness / 1000) * saturate(N.L)
  //   lamp    = colour * intensity * (1 - d/r)^2 * saturate(N.L') * cone
  // The lamp radius/intensity are the renderer's normalisation of Source's
  // brightness number (see WorldLightSource); colour, position and cone are the
  // map's. The per-pane cap of sixteen is what makes this loop fixed-size.
  float3 lightDirection = safeNormalize(lightDirectionAndMode.xyz);
  float3 lightingRgb = float3(1.0, 1.0, 1.0);
  float lighting = 1.0;
  // T105: the model lighting arm, and it is the FIRST branch on purpose. With
  // `--lighting basic` the entity/prop pass is the only thing that binds a
  // non-zero lightingParams.x, so every other draw falls through to the chain
  // below untouched and the off path is the same instructions it always was.
  //
  // The ambient cube is the classic Valve one: each of the six face colours is
  // weighted by the SQUARED component of the world normal (so the six weights
  // sum to 1 for any unit normal) and picked by that component's sign. The
  // sun term is Half-Lambert -- `f = saturate(N.L * 0.5 + 0.5)`, squared --
  // which is continuous through the terminator instead of clipping to black,
  // because a player model has no artist-painted falloff to hide a hard edge.
  if (lightingParams.x > 0.5) {
    // T105/task-37 Half B: WHICH cube. The six face colours in b2 come from the
    // map's global lights (task-36); the table at t12 comes from the BSP's own
    // per-leaf bake for this instance. The default is copied in first and the
    // table overwrites it only when the vertex stage resolved a leaf, so there
    // is ONE arithmetic path below and the leaf arm cannot drift from the basic
    // one -- it can only substitute the cube.
    //
    // ambientSlot is nointerpolation: a slot index blended between two vertices
    // would index a fraction of a cube. 0xFFFFFFFF is the sentinel both the
    // world and entity vertex shaders write when they have no leaf cube; a real
    // slot is a small non-negative integer and can never collide with it.
    float4 leafCube[6];
    [unroll] for (int ambientFace = 0; ambientFace < 6; ++ambientFace) {
      leafCube[ambientFace] = ambientCube[ambientFace];
    }
    if (ambientSlot != 0xFFFFFFFFu) {
      const uint ambientBase = ambientSlot * 6u;
      [unroll] for (int leafFace = 0; leafFace < 6; ++leafFace) {
        leafCube[leafFace] = leafAmbientCubeFaces[ambientBase + leafFace];
      }
    }
    lightingRgb = modelLightingRgb(n, leafCube, sunDirectionAndIntensity);
    lighting = max(lightingRgb.r, max(lightingRgb.g, lightingRgb.b));
    // --lighting-trace: write the interpolated normal out as colour. The probe
    // is what proves the vector above is world space: one model drawn at two
    // yaws cannot show the same picture this way, and a model-space normal
    // would.
    if (lightingParams.y > 0.5) {
      return float4(safeNormalize(n) * 0.5 + 0.5, 1.0);
    }
  } else if (sunDirectionAndMode.w > 0.5) {
    lightDirection = safeNormalize(sunDirectionAndMode.xyz);
    // N21: a lightmapped face already carries the map's sun and its sky ambient --
    // the VRAD bake IS those lights, sampled above and multiplied into the albedo
    // at line 432. Adding sunColor + ambientColor again here counts the same light
    // twice: measured on the first capture it took the sand from 210/255 to
    // 118/255 and turned every BSP triangle of a curved dune into a visible facet,
    // because the lightmap had been hiding the faceted geometric normal. Those
    // faces therefore start at 1.0 and receive only the map's lamps, which is what
    // a lightmap cannot contain. Geometry with no lightmap uv (entities) keeps the
    // sun/ambient term.
    const float lightmappedSurface = (lightmapFeatures.w > 0.5 && lightUv.x >= 0.0) ? 1.0 : 0.0;
    const float3 skyTerm = ambientColorAndIntensity.rgb * ambientColorAndIntensity.w
      + sunColorAndIntensity.rgb * sunColorAndIntensity.w * saturate(dot(n, lightDirection));
    // Geometry with no lightmap uv (every entity draw writes lightUv = (-1,-1)) has
    // no baked sun either, so the directional approximation is the only sun it can
    // have and it keeps the old one: the first capture ran the map's sun on those
    // too and turned the corridor's white wall lamps into grey blobs (a pixel at
    // 263,415 went 194 -> 54), because a wall fixture's normal faces away from a sun
    // that is 55 degrees up. So: lightmapped -> the bake alone, no lightmap -> the
    // old term, and the map's lamps are added to both.
    const float legacyTerm = lerp(1.0, 0.62 + 0.38 * saturate(dot(n, lightDirection)),
      lightDirectionAndMode.w);
    lightingRgb = lightmappedSurface > 0.5
      ? float3(1.0, 1.0, 1.0)
      : max(skyTerm, float3(legacyTerm, legacyTerm, legacyTerm));
    const uint litCount = (uint)lightCountAndParams.x;
    for (uint litIndex = 0u; litIndex < litCount; ++litIndex) {
      float3 toLight = lightPositionRadius[litIndex].xyz - worldPosition;
      float litDistance = length(toLight);
      float falloff = saturate(1.0 - litDistance / max(lightPositionRadius[litIndex].w, 1.0e-4));
      falloff *= falloff;
      if (falloff <= 0.0) continue;
      float3 towards = safeNormalize(toLight);
      float litDiffuse = saturate(dot(n, towards));
      float cone = 1.0;
      if (lightConeParams[litIndex].y > 0.5) {
        // The axis points away from the lamp, so the lamp-to-pixel vector
        // (-towards) is the one the cosine is measured with.
        float cosAngle = dot(-towards, safeNormalize(lightDirectionAndCone[litIndex].xyz));
        float edge = smoothstep(lightColorIntensity[litIndex].w,
          lightDirectionAndCone[litIndex].w, cosAngle);
        cone = pow(saturate(edge), max(lightConeParams[litIndex].x, 1.0));
      }
      lightingRgb += lightColorIntensity[litIndex].rgb * (falloff * litDiffuse * cone);
    }
    lighting = max(lightingRgb.r, max(lightingRgb.g, lightingRgb.b));
  } else {
    lighting = lerp(1.0, 0.62 + 0.38 * saturate(dot(n, lightDirection)), lightDirectionAndMode.w);
    lightingRgb = float3(lighting, lighting, lighting);
  }
  // Shared by both paths: the probe readouts below print it, and it is the key
  // light's N.L -- the map's sun when the map's lights are on.
  float diffuse = saturate(dot(n, lightDirection));
  if (cameraPositionAndSpecular.w > 0.5) {
    float3 viewDirection = safeNormalize(cameraPositionAndSpecular.xyz - worldPosition);
    float3 halfDirection = safeNormalize(lightDirection + viewDirection);
    float specular = 0.12 * pow(saturate(dot(n, halfDirection)), 32.0);
    lighting += specular;
    lightingRgb += float3(specular, specular, specular);
  }
  lighting = max(0.0, min(lighting, 2.0));
  // Same ceiling the old scalar clamp applied, now per channel: a lamp at
  // point-blank cannot write a colour the old frame could not have contained.
  lightingRgb = clamp(lightingRgb, float3(0.0, 0.0, 0.0), float3(2.0, 2.0, 2.0));
  // T96: the world alpha output. The two keys ride `feature.x` (TEXCOORD6,
  // written by the renderer from the material the tile belongs to): bit 16 is
  // `$alphatest`, bit 8 is `$translucent`. A cutout has no blend state and draws
  // in the opaque sub-batch; its hole comes from `discard`, the same rule the
  // viewmodel pixel shader uses at its own `$alphatest` arm (line 802). A
  // blended surface writes the texel's own alpha and is drawn by the separate
  // blended sub-batch with SrcAlpha/InvSrcAlpha and depth writes off. Every
  // vertex without bit 8 keeps 1.0, so the opaque frame is unchanged.
  if ((featureMask & 16u) != 0u && sampledTexel.a < alphaTestReference) discard;
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
  // delta is multiplied by the inverse vertex scale on all axes
  // (native_renderer_world.cpp:93-101). The uniform world transform keeps fog
  // distance and camera geometry in the same map metric.
  // N06 probe 4/5. 1.62 is the colour the product path is about to write, with
  // fog not yet blended in: if a surface reads black in a product capture but
  // shows a grey here, the arithmetic that ran after the lightmap zeroed it, and
  // if it is black here too the value itself is zero and the next question is
  // which term. 1.52 returns those terms: R = diffuse (0 with a dead normal),
  // G = |Ng| (0 when the vertex carried no usable normal), B = lighting / 2 (a
  // product of albedo and lighting cannot be black while lighting is not).
  if (envFeatures.x > 1.5) {
    if (envFeatures.x < 1.6) return float4(diffuse, saturate(length(Ng)), lighting * 0.5, 1.0);
    return float4(albedo * lightingRgb, 1.0);
  }
  if ((featureMask & 32u) != 0u && fogParams.w > 0.5) {
    float3 fogDelta = (cameraPositionAndSpecular.xyz - worldPosition) * fogScale.xyz;
    float fogDistance = length(fogDelta);
    float fogAmount = saturate((fogDistance - fogParams.x) / max(fogParams.y - fogParams.x, 1.0e-3));
    fogAmount = min(fogAmount, fogParams.z);
    return float4(lerp(albedo * lightingRgb, fogColor.rgb, fogAmount), outputAlpha);
  }
  return float4(albedo * lightingRgb, outputAlpha);
}
)HLSL";

  // W-1 perf: the sky's own pixel shader.
  //
  // The world pixel shader above contains `discard` (the alpha-test arm), and a
  // draw whose pixel shader can discard cannot be early-depth-rejected: the
  // hardware has to run the shader before it can know whether the depth test
  // would have killed the pixel. That is why deferring the sky to the end of the
  // world pass changed nothing measurable (W-1 A/B: 45.1 fps sky off, 38.8 fps
  // sky first, 37.3 fps sky last -- the same 12 triangles shaded every frame
  // pixel in both orders). This shader is the sky branch of the world shader and
  // nothing else: it returns the same colour the branch already returned (the
  // branch returns before the lighting half runs), but with no discard, no depth
  // output and no read beyond the cube, which is what lets the depth test run
  // first and reject the pixels the world already covers.
  extern const char skyPixelSource[] = R"HLSL(
TextureCube envCube : register(t5);
SamplerState sampler0 : register(s0);
float3 safeNormalize(float3 value) {
  float lengthSquared = dot(value, value);
  if (!(lengthSquared > 1e-8) || !(lengthSquared < 1e8)) return float3(0.0, 0.0, 0.0);
  return value * rsqrt(lengthSquared);
}
// The input signature matches the world vertex shader's output struct byte for
// byte, so the same input layout and the same vertex shader feed it.
float4 main(float4 position : SV_POSITION, float4 colour : COLOR0, float2 uv : TEXCOORD0, float3 normal : NORMAL0, float3 worldPosition : TEXCOORD2, float2 lightUv : TEXCOORD3, float blend : TEXCOORD4, float blendModulate : TEXCOORD5, float4 feature : TEXCOORD6, float4 tangent : TEXCOORD7) : SV_TARGET {
  return float4(envCube.Sample(sampler0, safeNormalize(worldPosition)).rgb, 1.0);
})HLSL";

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
// T105: the same lighting block the world pixel shader carries for entity and
// prop instances, redeclared here because these are separate compilation units.
// It sits at b1: this shader bound no pixel constant buffer before, and the
// model skinning buffer the vertex half uses at b1 is a different stage.
cbuffer ModelLightingConstants : register(b1) {
  float4 ambientCube[6];            // +X -X +Y -Y +Z(sky) -Z(ground), rgb colour
  float4 sunDirectionAndIntensity;  // xyz = direction TO the sun, w = intensity
  float4 lightingParams;            // x = lighting arm on, y = normal probe
};
struct Input { float4 position : SV_POSITION; float4 colour : COLOR0; float2 uv : TEXCOORD0; float3 normal : NORMAL0; };
float3 safeNormalize(float3 value) {
  float lengthSquared = dot(value, value);
  if (!(lengthSquared > 1e-8) || !(lengthSquared < 1e8)) return float3(0.0, 0.0, 0.0);
  return value * rsqrt(lengthSquared);
}
float3 modelLightingRgb(float3 normalWorld, float4 cube[6], float4 sun) {
  float3 n = safeNormalize(normalWorld);
  float3 ambient = (n.x >= 0.0 ? cube[0].rgb : cube[1].rgb) * (n.x * n.x)
    + (n.y >= 0.0 ? cube[2].rgb : cube[3].rgb) * (n.y * n.y)
    + (n.z >= 0.0 ? cube[4].rgb : cube[5].rgb) * (n.z * n.z);
  float3 sunDirection = safeNormalize(sun.xyz);
  float halfLambert = saturate(dot(n, sunDirection) * 0.5 + 0.5);
  return ambient + sun.w * (halfLambert * halfLambert);
}
float4 main(Input input) : SV_TARGET {
  float3 normal = normalize(input.normal);
  float diffuse = saturate(dot(normal, normalize(float3(0.35, 0.55, 0.75))));
  float lighting = 0.62 + 0.38 * diffuse;
  // With the switch off this branch is never taken, so the two lines above and
  // the return below describe exactly the frame the previous build drew.
  // The normal here is the MODEL-space one the model vertex shader writes, so
  // the cube is applied in model space; the entity pass in the world pixel
  // shader is the pass whose normals are world space, and the trace probe
  // (lightingParams.y) lives there for that reason.
  if (lightingParams.x > 0.5) {
    float3 litRgb = modelLightingRgb(normal, ambientCube, sunDirectionAndIntensity);
    return float4(input.colour.rgb * litRgb, input.colour.a);
  }
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

  // P4 (2026-11-22): the gameplay-effects pass. Same constant buffer layout as
  // the overlay (b0: mvp, cameraPositionAndExpand, mode) so it can share that
  // buffer; one extra input, the quad's uv, for a soft falloff. A sprite carries
  // uv over [0,1]^2 and fades radially; a ribbon carries u = 0.5 and fades across
  // its width only.
  extern const char gameplayFxVertexSource[] = R"HLSL(
cbuffer OverlayConstants : register(b0) {
  row_major float4x4 mvp;
  float4 cameraPositionAndExpand;
  float4 mode;
};
struct Input { float3 position : POSITION; float4 colour : COLOR0; float2 uv : TEXCOORD0; };
struct Output { float4 position : SV_POSITION; float4 colour : COLOR0; float2 uv : TEXCOORD0; };
Output main(Input input) {
  Output output;
  output.position = mul(float4(input.position, 1.0), mvp);
  output.colour = input.colour;
  output.uv = input.uv;
  return output;
}
)HLSL";

  extern const char gameplayFxPixelSource[] = R"HLSL(
float4 main(float4 position : SV_POSITION, float4 colour : COLOR0, float2 uv : TEXCOORD0) : SV_TARGET {
  float2 d = uv * 2.0 - 1.0;
  float falloff = saturate(1.0 - dot(d, d));
  falloff *= falloff;
  return float4(colour.rgb, colour.a * falloff);
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
  // G7: the shared world pixel shader reads a ninth interpolant as well, the
  // atlas tile rectangle, and this pass draws through that same pixel shader.
  // Same rule as T61 above: an interpolant the vertex shader never writes is
  // undefined here, and on this driver it reads as 1.0 -- which would switch
  // the tiled sampling path on with a rectangle of ones over every entity.
  // G7 is a world-surface fix, so the entity pass writes zeros and keeps the
  // old untiled sampling.
  float4 tileRect : TEXCOORD8;
  // T105/task-37 Half B: the same rule again, third time. The world pixel
  // shader now reads a tenth interpolant -- which leaf cube to shade this
  // instance with -- and this pass draws through that same pixel shader. It has
  // to be declared here or the entity stage leaves it undefined, and on this
  // driver an undefined interpolant reads as 1.0. 1.0 as a slot index is not a
  // neutral value: it would silently select slot 1 of the leaf table for every
  // entity and prop in the frame, which is a real cube belonging to some other
  // part of the map. The sentinel is the neutral write, and it means "fall back
  // to the b2 global cube", which is exactly what --lighting basic draws.
  //
  // The value arrives in the .w of the first normal row: that lane is dead
  // space in a 128-byte instance record (see EntityModelInstanceGpu in
  // native/src/renderer_internal.h) and the CPU fills it per instance, so this
  // costs no stride and no input-layout element.
  nointerpolation uint ambientSlot : TEXCOORD9;
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
  output.tileRect = float4(0.0, 0.0, 0.0, 0.0);
  // T105/task-37 Half B: the leaf slot rides in the .w of the first normal row,
  // which is otherwise unused -- the normal is rotated by nrow0.xyz on the line
  // above and the fourth lane is padding in the 128-byte instance record. The
  // CALLER writes -1.0 there for an instance with no leaf cube, so the
  // comparison is against that exact value rather than a cast of a negative
  // float: HLSL's float-to-uint conversion clamps a negative to 0, and 0 is a
  // perfectly good slot -- a falling-back instance would have shaded itself
  // with whatever cube happened to be first in the table.
  output.ambientSlot = input.nrow0.w < 0.0 ? 0xFFFFFFFFu : (uint)input.nrow0.w;
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
  float4 effectOverlay;
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
  float4 effectOverlay;
};
// T105: the model lighting block, redeclared per compilation unit. b1 is free
// in this pixel stage -- the only pixel constant buffer the pass binds is b0.
// The normals this shader receives are model space (the viewmodel vertex shader
// writes them unrotated), so the cube and the sun are applied in that space:
// it is the same space the hardcoded direction below has always lived in, and
// the drift it introduces is stated in the report rather than hidden.
cbuffer ModelLightingConstants : register(b1) {
  float4 ambientCube[6];            // +X -X +Y -Y +Z(sky) -Z(ground), rgb colour
  float4 sunDirectionAndIntensity;  // xyz = direction TO the sun, w = intensity
  float4 lightingParams;            // x = lighting arm on, y = normal probe
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
float3 modelLightingRgb(float3 normalWorld, float4 cube[6], float4 sun) {
  float3 n = safeNormalize(normalWorld);
  float3 ambient = (n.x >= 0.0 ? cube[0].rgb : cube[1].rgb) * (n.x * n.x)
    + (n.y >= 0.0 ? cube[2].rgb : cube[3].rgb) * (n.y * n.y)
    + (n.z >= 0.0 ? cube[4].rgb : cube[5].rgb) * (n.z * n.z);
  float3 sunDirection = safeNormalize(sun.xyz);
  float halfLambert = saturate(dot(n, sunDirection) * 0.5 + 0.5);
  return ambient + sun.w * (halfLambert * halfLambert);
}
// P4 (2026-11-22): the Ubercharge / crit glow in first person. A team-coloured
// metal look: the colour scaled by the shaded brightness plus a rim term, mixed in
// by effectOverlay.w. w == 0 returns `shaded` unchanged (the lerp is skipped), so
// every frame without an effect is byte-identical to before.
float3 applyEffectOverlay(float3 shaded, float3 viewNormal, float3 viewPosition) {
  if (!(effectOverlay.w > 0.0)) return shaded;
  float luma = dot(shaded, float3(0.299, 0.587, 0.114));
  float3 n = safeNormalize(viewNormal);
  float3 v = safeNormalize(-viewPosition);
  float rim = 1.0 - saturate(abs(dot(n, v)));
  rim *= rim;
  float3 glow = effectOverlay.rgb * (0.35 + 1.1 * luma) + effectOverlay.rgb * rim * 0.9;
  return lerp(shaded, glow, saturate(effectOverlay.w));
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
    // Hoisted out of the block so the lighting arm below can tilt the MODEL
    // normal by the same perturbation; zero when the material has no map, and
    // a zero tangent normal leaves the normal exactly as it was.
    float3 tangentNormal = float3(0.0, 0.0, 0.0);
    if (bumpFeatures.x > 0.5) {
      tangentNormal = bumpTexture.Sample(albedoSampler, input.uv).xyz * 2.0 - 1.0;
      // The perturbed normal: the interpolated normal tilted by the map. The
      // magnitude is bounded so a garbage normal map cannot flip the surface.
      shadedNormal = safeNormalize(shadedNormal + tangentNormal * 0.5);
      if (dot(shadedNormal, shadedNormal) < 1e-6) shadedNormal = safeNormalize(input.viewNormal);
    }
    float3 viewLight = safeNormalize(mul(float4(lightDirection, 0.0), viewMatrix).xyz);
    float bumpDiffuse = saturate(dot(shadedNormal, viewLight));
    float bumpLighting = 0.55 + 0.45 * bumpDiffuse;
    // The diffuse colour, scalar by default so the switch-off path is the same
    // multiplication it was. With the switch on the map's ambient cube and sun
    // replace the hardcoded direction, and the bump perturbation is applied to
    // that normal in the space the cube lives in -- the map is sampled without
    // a tangent basis either way, so it is the same approximation it was, and
    // it still changes the picture with the switch on.
    float3 diffuseRgb = float3(bumpLighting, bumpLighting, bumpLighting);
    if (lightingParams.x > 0.5) {
      float3 bumpedNormal = safeNormalize(normal + tangentNormal * 0.5);
      diffuseRgb = modelLightingRgb(bumpedNormal, ambientCube, sunDirectionAndIntensity);
    }

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

    float3 lit = albedoRgb * diffuseRgb;
    lit += specular * albedoRgb;
    lit += emission;
    lit = applyEffectOverlay(lit, input.viewNormal, input.viewPosition);
    return float4(lit, texel.a * input.colour.a);
  }
  if (lightingParams.x > 0.5) {
    return float4(applyEffectOverlay(input.colour.rgb * modelLightingRgb(normal, ambientCube, sunDirectionAndIntensity),
      input.viewNormal, input.viewPosition), input.colour.a);
  }
  return float4(applyEffectOverlay(input.colour.rgb * lighting, input.viewNormal, input.viewPosition), input.colour.a);
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
//   4 = image: sample the one resident basemap texture with the primitive's own
//       crop in uv, and multiply by the primitive's colour so a caller can dim or
//       tint it. The pass binds t1/s1 only when an image primitive is present.
//   5 = F0 (2026-11-23) antialiased text: the DirectWrite glyph atlas at t2,
//       linearly filtered, coverage times the colour's alpha. Bound only when a
//       kind-5 quad is present; paths 0..4 are unchanged.
Texture2D<float> fontAtlas : register(t0);
SamplerState fontSampler : register(s0);
Texture2D<float4> hudImage : register(t1);
SamplerState hudImageSampler : register(s1);
Texture2D<float> uiAtlas : register(t2);
SamplerState uiSampler : register(s2);
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
  if (kind == 4) return hudImage.Sample(hudImageSampler, input.uv) * input.colour;
  if (kind == 5) {
    const float coverage = uiAtlas.Sample(uiSampler, input.uv);
    if (coverage <= 0.0) discard;
    return float4(input.colour.rgb, input.colour.a * coverage);
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
