#pragma once

#include <Windows.h>
#include <d3d11.h>
#include <DirectXMath.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>
#include <wrl/client.h>

#include "texture2d.h"
#include "bsp_map.h"
#include "model_loader.h"
#include "demo_header.h"
#include "world_material.h"
#include "world_tile_atlas.h"
#include "viewmodel_material.h"

namespace tf2::native {

constexpr float clampViewModelFov(float value) {
  return value < 40.0f ? 40.0f : (value > 120.0f ? 120.0f : value);
}

enum class QualityPreset {
  Performance,
  Standard,
};

struct RenderSettings {
  QualityPreset preset = QualityPreset::Performance;
  int textureMipBias = 2;
  int antiAliasingQuality = 2;
  int anisotropicLevel = 1;
  bool dynamicLighting = false;
  bool shadows = false;
  bool bumpMapping = false;
  // T27: the per-material feature passes. Deliberately separate from
  // `bumpMapping`: that one is T24's single-slot diagnostic, which binds one
  // texture for the whole world, while these turn on the per-material atlases.
  // A capture that turned on one must not read as having turned on the other,
  // and `normalize()` leaves all three alone for the same reason.
  bool materialBump = false;
  bool bumpMap2 = false;
  bool detailLayer = false;
  bool specular = false;
  bool waterReflection = false;
  bool waterRefraction = true;
  // T2 (2026-10-14): the water surface pass itself, separate from the two
  // Fresnel coefficients above. Those two say what the material's own keys
  // asked for; this says whether the surface is drawn at all, which is the
  // switch `--world-water on|off` overrides. Additive on purpose: the struct's
  // existing fields keep their meaning and the settings file keeps loading.
  bool waterSurface = false;
  bool skybox = false;
  int modelLod = 4;
  float renderScale = 1.0f;
  float viewModelFov = 90.0f;
  bool vsync = false;

  static RenderSettings fromPreset(QualityPreset preset);
  void normalize();
};

struct WorldTexture {
  std::string material;
  std::vector<std::uint8_t> rgba;
  UINT width = 0;
  UINT height = 0;
  // T21: the VTF's dimensions before the downscale to width x height. The
  // vertex uv is normalised by these, because the BSP texcoord is in source
  // texels; zero means the tile is the source (nothing was downscaled).
  UINT sourceWidth = 0;
  UINT sourceHeight = 0;
  // ---- T12 (2026-10-18): the second layer of a blend material ------------
  // A WorldVertexTransition material names two albedo layers and blends them by
  // the displacement's per-vertex alpha. `blendRgba` holds the second layer
  // already resampled to this tile's own `width` x `height`, which is what lets
  // both layers be sampled with one uv: the uv is the material's texcoord in
  // normalised space, so two textures on the same pixel grid agree about where
  // a texel is. Empty means the material declares no second layer.
  std::vector<std::uint8_t> blendRgba;
  // T12: whether the material *declares* a second layer, which is a fact about
  // the VMT and not about this run. `blendRgba` being empty means the layer was
  // not fetched (or did not resolve); `blendDeclared` being false means the
  // material never had one. The terrain-only filter needs the second fact: with
  // `--world-blend off` no layer is fetched at all, and keying the filter on
  // the bytes made the terrain frame empty -- measured, exit 14.
  bool blendDeclared = false;
  // `$blendmodulatetexture`, same grid, r and g carrying the smoothstep range
  // the SDK's shader uses. Empty means the blend is the raw vertex alpha.
  std::vector<std::uint8_t> blendModulateRgba;
  // ---- T27 (2026-10-19): the material features that change the image ------
  // Each of these is already resampled onto this tile's own width x height, for
  // the same reason the blend layer is: one tile rectangle, one uv, so a
  // per-material normal or detail map can be sampled by the vertex uv that is
  // already there. Empty means the feature is not drawn for this material --
  // either it declared nothing, its texture did not resolve, or the switch that
  // fetches it was off. The renderer sets the vertex's feature mask from what
  // it actually blitted, so an empty vector can never claim to be applied.
  std::vector<std::uint8_t> bumpRgba;      // `$bumpmap`
  std::vector<std::uint8_t> bump2Rgba;     // `$bumpmap2`, layer 2's normal
  std::vector<std::uint8_t> detailRgba;    // `$detail`, tiled `$detailscale` times
  // TextureCombine's two arguments, from `$detailblendmode` and
  // `$detailblendfactor` on the material that declares `$detail`.
  std::uint32_t detailMode = 0;
  float detailFactor = 1.0f;
  float detailScale = 1.0f;
  // What the VMT asked for, whether or not it resolved. The terrain filter's
  // lesson (T12) applies here too: "declared and not fetched" is a different
  // fact from "never declared", and only the first one is a bug.
  bool bumpDeclared = false;
  bool bump2Declared = false;
  bool detailDeclared = false;
  // `$ssbump`. Carried so the feature state can say how many of the materials
  // that declare a normal map were refused because their texel is a bumpBasis
  // weight rather than a tangent-space normal -- a refusal that is not a missing
  // resource and must not be counted as one.
  bool ssBump = false;
};

// T12: what the terrain blend path actually did, as counts a gate can read.
// `tiles` is how many materials put a second layer into the blend atlas;
// `weightedVertices` is how many uploaded vertices carry a real weight (a
// displacement whose material names one layer carries the sentinel and is not
// counted); `malformedLayers` is how many layers were refused for a byte count
// that did not match the tile.
struct WorldBlendState {
  // T12: the two switches, carried on the reading rather than assumed by the
  // reader. A gate that compares an "off" frame against another "off" frame has
  // compared the same picture twice and called it a difference; this is the
  // field that says which state the numbers below were taken in.
  bool enabled = true;
  bool terrainOnly = false;
  std::size_t texturesSeen = 0;
  std::size_t layersSeen = 0;
  std::size_t accepted = 0;
  std::size_t acceptedLayers = 0;
  std::size_t tiles = 0;
  std::size_t modulateTiles = 0;
  std::size_t malformedLayers = 0;
  std::size_t weightedVertices = 0;
  std::size_t modulatedVertices = 0;
  // T12: the *distribution* of the weights that reached the GPU. A count of
  // weighted vertices cannot tell "the map's painted weights arrived" from "a
  // constant was written into every vertex", and both draw a picture. These
  // three readings can: a real terrain has several buckets and spans 0..1, a
  // constant has one bucket at whatever value it was forced to.
  float weightMin = -1.0f;
  float weightMax = -1.0f;
  std::size_t distinctWeights = 0;
};

// T27: what the per-material feature path actually did, counted from the
// blits and the vertices rather than from the VMTs. `tiles` is how many
// materials put a layer into a feature atlas; `vertices` is how many uploaded
// vertices carry that bit in `featureMask`; `materials` is how many distinct
// materials those vertices belong to. A declared-and-resolved count without
// these three cannot tell "the shader samples this material's own normal map"
// from "the atlas was built and no vertex points at it", and the second one is
// the T19 failure this task exists to close.
struct WorldFeatureState {
  // The switch the caller set. `enabled == false` means the atlas was not
  // built at all, not that it was built and left unbound.
  bool enabled = false;
  std::size_t tiles = 0;
  std::size_t materials = 0;
  std::size_t vertices = 0;
  // The blend-weight distribution that reached `$bumpmap2`'s lerp. Same reason
  // as the terrain weights: a constant 1 and a painted gradient draw different
  // pictures, and only the distribution tells them apart.
  float bump2WeightMin = -1.0f;
  float bump2WeightMax = -1.0f;
  std::size_t bump2DistinctWeights = 0;
};

struct ProjectileLine {
  float start[3] = {};
  float end[3] = {};
  float color[4] = {1.0f, 0.25f, 0.05f, 0.0f};
};

struct EntityMarker {
  float position[3] = {};
  float color[4] = {0.2f, 0.9f, 0.35f, 0.0f};
};

struct EntityModelDrawInstance {
  std::string cacheKey;
  float origin[3] = {};
  float angles[3] = {};
  bool hasAngles = false;
  float color[4] = {0.72f, 0.72f, 0.76f, 1.0f};
  // Skinning for this instance. `skinningEnabled` is set only when the caller
  // has a complete, in-range pose for this entity; when it is false the instance
  // is drawn through the bind-pose path exactly as before. `boneOffset` indexes
  // the buffer handed to setEntityBoneMatrices, and `boneCount` bounds the
  // per-vertex bone indices the shader is allowed to accept.
  bool skinningEnabled = false;
  std::uint32_t boneOffset = 0;
  std::uint32_t boneCount = 0;
};

// Everything the first-person pass needs, in one value. It is deliberately a
// plain struct rather than a set of setters: the ViewModel pass must not be able
// to read the world path's state by accident, and a single struct makes it
// obvious at the call site which fields the pass actually consumes.
//
// The transform chain is applied in this order, all row-major 3x4 and all with
// the row-vector convention the rest of this file uses (v' = v * A * H * T * V * P):
//   attachment local -> owning bone model space -> ViewModel model space (A)
//   -> hand transform (H) -> camera space (T) -> ViewModel projection (P)
// The left-hand mirror lives only in H. Nothing here reaches the world entity
// bone buffer or the animation decode output.
// One contiguous run of the ViewModel vertex buffer that shares one material.
//
// The ViewModel mesh is a flat triangle list, one vertex per index, so a range is
// a `Draw(count, first)` and needs no index buffer. The runs are contiguous by
// construction: the mesh builder appends a descriptor's vertices as it walks the
// descriptors, so a descriptor's triangles are adjacent in the buffer. Two
// descriptors that share a slot therefore produce two ranges, not one -- the
// alternative (reordering the buffer) would move vertices the T2 mesh readings
// are frozen against for no gain in what is drawn.
struct ViewModelDrawRange {
  std::uint32_t firstVertex = 0;
  std::uint32_t vertexCount = 0;
  std::int32_t slot = -1;
  // The cache key of the material this range binds. Empty means "no material",
  // which is the same state as an unresolvable slot: flat colour.
  std::string materialKey;
  // The material's own `$translucent` / `$alphatest`, copied from the slot table
  // at mesh-build time so the draw does not have to reach back into it.
  bool translucent = false;
  bool alphaTest = false;
};

struct ViewModelDrawState {
  bool enabled = false;
  float fov = 90.0f;
  bool leftHand = false;
  std::array<float, 12> handTransform{};
  std::array<float, 12> attachmentTransform{};
  bool attachmentValid = false;
  std::string materialKey;
  bool materialTranslucent = false;
  bool materialAlphaTest = false;
  std::string attachmentName;
  // Bind pose is `skinningEnabled = false`: the mesh is drawn through the
  // unskinned path rather than being skinned with a matrix table the caller
  // could not build. The plan is explicit that a bone-mapping failure refuses
  // skinning instead of guessing a partial one.
  bool skinningEnabled = false;
  std::uint32_t boneCount = 0;
  // The animation input the caller resolved for this frame, spelled the way
  // viewModelAnimationStatusName does (missing / ok / sequenceOutOfRange /
  // boneMismatch). The product prints it as `vmAnimInput` on the pass-order line.
  //
  // It is deliberately a SECOND field rather than a replacement for
  // `animationInput`, and the reason is that the two answer different questions.
  // `animationInput` is an *effect* reading -- `missing`, or `bones:<n>` once a
  // bone table was uploaded -- and the identity-bones instrument depends on it
  // staying that way. This one is the *contract* reading. Keeping both is what
  // makes "the pass skinned from a real pose" (vmAnimInput=ok, skinning=1)
  // distinguishable from "the pass skinned through the identity instrument"
  // (vmAnimInput=missing, skinning=1); folding them into one string would lose
  // exactly that distinction, and would also rewrite a T2 reading that 154
  // assertions and a byte-identical frame pair were frozen against.
  std::string animationStatus;
  // Whether the resolved pose was actually applied. It is a separate field from
  // `skinningEnabled` because they answer different questions: skinningEnabled is
  // "the pass has a table it accepted", this is "the caller decided to use it".
  // The default is false even when a complete pose exists -- see main.cpp for the
  // measurement behind that -- and the pair (animationStatus=ok,
  // animationApplied=0) is the state this round ships.
  bool animationApplied = false;
  float tint[4] = {0.78f, 0.78f, 0.84f, 1.0f};
  // Why the pass did or did not draw, as a stable string the runtime prints.
  std::string reason;
};

struct WorldMaterialParams {
  bool bumpMapping = false;
  bool envMap = false;
  bool waterMaterial = false;
  bool selfIllum = false;
  float selfIllumR = 0.0f;
  float selfIllumG = 0.0f;
  float selfIllumB = 0.0f;
};

enum class WorldLightmapStatus {
  Unavailable,
  FallbackUnlit,
  Active,
};

// How the world pass samples reflections. Unavailable: the material declares no
// env map, or none resolved. Approximate2d: the 2D projection of $envmap that
// the renderer has always used, sampled by the world-space normal.
// SixFace: a real TextureCube built from a map cubemap's six decoded faces and
// sampled by the reflection vector.
enum class WorldCubemapStatus { Unavailable, Approximate2d, SixFace };
const char* worldCubemapStatusName(WorldCubemapStatus status);

// T19: one material's outcome from the atlas plan, as the product reports it.
// `reason` is "tiled" or a WorldTileRefusal name; `batch` is which atlas holds
// it and `tile` its position inside that atlas.
struct WorldTileReading {
  std::string material;
  int batch = -1;
  int tile = -1;
  int width = 0;
  int height = 0;
  // T21: the tile's first pixel inside its atlas (`slot.x`/`slot.y`) and the
  // atlas' own size. A trace that says "material M is tile 7" cannot be
  // checked against a screen pixel; a trace that says "M lives at (2057,1) in a
  // 4112-wide atlas" can, because the uv is a function of exactly those
  // numbers. Published here rather than recomputed by the caller: the grid
  // rule lives in planWorldTiles and a second copy of it would be a second
  // layout.
  int x = 0;
  int y = 0;
  int atlasWidth = 0;
  int atlasHeight = 0;
  // T21: the pre-downscale dimensions the uv is normalised by. Equal to
  // `width` / `height` for a texture that already fit the tile.
  int sourceWidth = 0;
  int sourceHeight = 0;
  bool blend = false;
  bool modulate = false;
  // T27: whether this material's normal layers reached the feature atlases. A
  // material that declares `$bumpmap` but whose tile could not be placed has
  // `bump == false`, so "declared" and "the shader can sample it" stay two
  // different readings instead of one optimistic flag.
  bool bump = false;
  bool bump2 = false;
  bool detail = false;
  std::string reason;
};

// T21: the world constants the last frame was drawn with, so a diagnostic can
// predict what the pixel shader should produce at a screen point without
// re-deriving the camera, the projection or the lighting switches from
// settings. Every field here is the value uploaded to the constant buffer for
// the world pass; none of them is a preference.
struct WorldShaderConstants {
  bool valid = false;
  float mvp[16] = {};              // row-major view * projection
  float lightDirection[3] = {};    // lightDirectionAndMode.xyz
  float lightMode = 0.0f;          // lightDirectionAndMode.w (diffuse term on)
  float lightmapFactor = 1.0f;     // lightmapFeatures.x
  bool lightmapEnabled = false;    // lightmapFeatures.w > 0.5
  float bumpMode = 0.0f;           // lightmapFeatures.y
  float envMode = 0.0f;            // envFeatures.x
  float cameraPosition[3] = {};    // cameraPositionAndSpecular.xyz
  bool specular = false;           // cameraPositionAndSpecular.w
  float selfIllumTint[3] = {};     // materialFeatures.xyz
  float selfIllum = 0.0f;          // materialFeatures.w
  int debugView = 0;
  int width = 0;
  int height = 0;
};

struct WorldMaterialSampleState {
  WorldLightmapStatus lightmapStatus = WorldLightmapStatus::Unavailable;
  float lightmapIntensity = 1.0f;
  bool bumpTextureBound = false;
  bool envTextureBound = false;
  bool lightmapAtlasBound = false;
  bool selfIllumEnabled = false;
  // T24: the three pass-level lighting constants the LAST world draw uploaded.
  // `bumpMode` is true only when the switch is on and a bump texture is bound,
  // so a frame can be attributed to the constant rather than to the setting.
  // `dynamicLightingMode` and `specularMode` have no resource dependency; they
  // are the settings field, read through the same helper the constant uses.
  bool dynamicLightingMode = false;
  bool specularMode = false;
  bool bumpMode = false;
  // T27: the per-material feature path, as the last upload left it. `*Mode`
  // says the atlas exists AND at least one vertex carries the bit, which is the
  // pair that separates "the material asked" from "the shader samples it".
  bool materialBumpMode = false;
  bool bump2Mode = false;
  bool detailMode = false;
  WorldFeatureState bumpFeature{};
  WorldFeatureState bump2Feature{};
  WorldFeatureState detailFeature{};
  // Falsifiable vertex counts for the lightmap pass. `lightmappedVertexCount`
  // is how many of the uploaded world vertices carry a per-face light uv; a
  // vertex without one is drawn flat no matter what the atlas holds. The pair
  // (lightmapped / total) is the number that says whether the lightmap pass
  // covers the map, which the status enum on its own cannot: `Active` plus
  // 0/600000 would mean the atlas is bound and nothing samples it.
  std::uint32_t worldVertexCount = 0;
  std::uint32_t lightmappedVertexCount = 0;
  // T10 diagnostic: the CONVERTED bounds of the uploaded world buffer. The
  // camera and the entity models share one normalized space; if the world
  // buffer's bounds are not the same box, the world pass is drawing somewhere
  // the camera never looks, and this pair of triples says where.
  float convertedMin[3] = {0.0f, 0.0f, 0.0f};
  float convertedMax[3] = {0.0f, 0.0f, 0.0f};
  // Which of the three things the env path is doing, as a value the runtime can
  // read rather than a claim in a comment. `envTextureBound` alone could not
  // tell a map whose cubemap failed to decode from one that never had an env
  // map: both leave the 2D approximation bound and both read as bound.
  WorldCubemapStatus cubemapStatus = WorldCubemapStatus::Unavailable;
  // The faces uploaded to the cube texture, and its edge in pixels. Six and a
  // power of two are what a D3D11 cube sample needs; anything else is refused
  // and the status stays Approximate2d, so a partial cube cannot be sampled as
  // if it were whole.
  std::uint32_t cubemapFaces = 0;
  std::uint32_t cubemapSize = 0;
  // What the last world draw actually asked the pixel shader to do: 0 = no env
  // pass, 1 = the 2D approximation, 2 = the six-face cube. `cubemapStatus` says
  // what exists; this says what was used. A map whose material declares no
  // $envmap builds the cube and reads status=six-face with mode=0, and without
  // this field that state and "the cube is sampled" would print the same.
  std::uint32_t envMode = 0;
  // What the uploaded vertex buffer actually holds, counted while it is built:
  // how many of its triangles came from a displacement (the parser subdivides a
  // displaced face into a grid, so these are the tessellated ones and not the
  // flat quad) and how many are water. The parse side counts triangles per
  // capability; these say how much of that reached the GPU, which is the
  // difference between "the map has 165376 displacement triangles" and "the
  // map draws them".
  std::uint32_t displacedTriangles = 0;
  std::uint32_t waterTriangles = 0;
  // ---- T1 (2026-10-13): skybox geometry and the pvs clip -------------------
  // The skybox used to be a resource reading only: six vmts found, six textures
  // decoded, no vertex of it ever built. `worldSkyVertexCount` is the number of
  // vertices the sky pass actually draws, and `worldSkyState` is the word the
  // frozen capability layer gives it -- so "active" now means "36 vertices are
  // in a buffer and the pass draws them", not "the files are on disk".
  WorldSkyState worldSkyState = WorldSkyState::Unavailable;
  std::uint32_t worldSkyVertexCount = 0;
  std::uint32_t worldSkyTriangles = 0;
  std::uint32_t worldSkyFaces = 0;
  std::uint32_t worldSkyDraws = 0;
  std::uint32_t worldSkySide = 0;
  bool worldSkyResampled = false;
  std::string worldSkyReason;
  // The clip, as a fact about the last draw rather than a setting. `hidden` is
  // how many of the uploaded triangles the pvs refused; `drawn` is how many the
  // draw call was actually given. The difference between the two and the total
  // is what makes "the pvs hides 26936 triangles" and "26936 triangles are not
  // drawn" the same reading.
  WorldVisClipState worldVisClipState = WorldVisClipState::Unavailable;
  bool worldVisClipApplied = false;
  std::size_t worldVisDrawnTriangles = 0;
  std::size_t worldVisHiddenTriangles = 0;
  std::size_t worldVisTrianglesWithoutCluster = 0;
  std::size_t worldVisInputTriangles = 0;
  int worldVisCameraCluster = -1;
  std::string worldVisClipReason;
  // T10 diagnostic: whether the world pass rasterised with back-face culling
  // on. Read from the renderer after the draw, like the vis line above, so a
  // frame captured with --no-world-cull cannot be mistaken for one drawn with
  // the product's own cull mode.
  bool worldCullBackfaces = true;
  // T11 diagnostic: which debug view the world pass drew. 0 = the product's
  // textured/lightmapped path, 1 = flat colour with no sampling, 2 = depth as
  // grey. Read from the renderer after the draw so a capture cannot claim a
  // mode it did not use.
  int worldDebugView = 0;
  // ---- T19 (2026-10-19): atlas allocation, in full ----------------------
  // The pair that replaces the silent 64-tile cap. `worldTilesTiled` is what
  // got a tile and `worldTilesRefused` is what did not, split by cause, so
  // "the atlas holds 148 materials" and "the atlas silently dropped 51" cannot
  // print the same numbers. `worldAtlasBatches` is how many textures the plan
  // needed: 1 means the old single-atlas shape, 3 means the reference map.
  std::size_t worldTilesTiled = 0;
  std::size_t worldTilesRefused = 0;
  std::size_t worldTilesRefusedMalformed = 0;
  std::size_t worldTilesRefusedDuplicate = 0;
  std::size_t worldTilesRefusedBudget = 0;
  std::size_t worldAtlasBatches = 0;
  std::size_t worldAtlasBlendBatches = 0;
  std::uint64_t worldAtlasLayerBytes = 0;
  std::string worldAtlasPlanReason;
  // T19: the draw batches the world pass issued, and the two cross-pass
  // questions. `weaponDrawsWithWorldAtlas` counts weapon ranges drawn while a
  // world atlas was the bound t0; `worldDrawsWithWeaponTexture` counts world
  // batches drawn while a weapon range's albedo was. Both must be 0: they are
  // the leak the task asks to rule out, and a counter that can only ever be
  // green is not a check, so the mutation binds one on purpose.
  std::size_t worldBatchDrawCalls = 0;
  std::size_t worldBatchTexturedDrawCalls = 0;
  std::size_t weaponDrawsWithWorldAtlas = 0;
  std::size_t worldDrawsWithWeaponTexture = 0;
  std::size_t slot0Binds = 0;
  // One row per material the upload was handed, in the order it was handed
  // them. The reason is "tiled" or the refusal's name, so a gate can assert on
  // the exact material that used to vanish into `capped=51`.
  std::vector<WorldTileReading> worldTileTable;
};

// What a per-triangle pvs clip decided, handed to the renderer by the caller
// that owns the BSP. The renderer stores no map: main.cpp runs the frozen
// clipWorldTrianglesByVis and passes the verdict, so the decision has one
// implementation and the draw list cannot disagree with it.
struct WorldVisDrawList {
  std::vector<std::uint32_t> indices;
  WorldVisClipState state = WorldVisClipState::Unavailable;
  bool applied = false;
  std::size_t inputTriangles = 0;
  std::size_t outputTriangles = 0;
  std::size_t hiddenTriangles = 0;
  std::size_t trianglesWithoutCluster = 0;
  int cameraCluster = -1;
  std::string reason;
};

// ---------------------------------------------------------------------------
// T2 (2026-10-14): the water surface and the ViewModel's advanced materials
// ---------------------------------------------------------------------------
// A second sample state, deliberately NOT an extension of
// WorldMaterialSampleState. T1 froze that struct and world-material-check.sh
// pins its fields, so a field added here could not move one T1 pinned; a struct
// added next to it cannot either. The getter is new for the same reason: the
// water pass and the ViewModel material effects are read back through
// viewModelMaterialSampleState() and worldMaterialSampleState() still returns
// exactly what it returned before.

// The water surface as a draw: how many triangles were handed to it, whether
// the Draw call ran, whether the warp reached the samples, and the state of the
// two render targets nobody has built.
struct WaterSurfaceSampleState {
  tf2::native::WorldWaterSurfaceState state = tf2::native::WorldWaterSurfaceState::Unavailable;
  std::uint32_t waterFaces = 0;
  std::uint32_t waterTriangles = 0;
  std::uint32_t waterDrawCalls = 0;
  // Whether the water is in the picture this frame. A buffer that exists and a
  // pass that drew are different facts and only the second changes pixels.
  bool waterVisible = false;
  bool waterWarpApplied = false;
  bool waterTextureBound = false;
  bool waterFallback = false;
  tf2::native::WorldWaterSurfaceState reflectionState = tf2::native::WorldWaterSurfaceState::Unavailable;
  tf2::native::WorldWaterSurfaceState refractionState = tf2::native::WorldWaterSurfaceState::Unavailable;
  std::string reason;
};

// Per-effect, per-frame: what the material declared, what the pass uploaded and
// how many ranges the effect actually reached. `*AppliedRanges` is the reading
// the task asks for by name, and it is counted from the ranges that drew rather
// than from the ranges that exist -- a material on a range whose texture never
// resolved declares Phong and applies none of it.
struct ViewModelMaterialSampleState {
  // Phong
  tf2::native::ViewModelEffectState phongState = tf2::native::ViewModelEffectState::Unavailable;
  bool phongDeclared = false;
  bool phongParsed = false;
  bool phongUploaded = false;
  bool phongApplied = false;
  std::uint32_t phongAppliedRanges = 0;
  bool phongFallback = false;
  std::string phongReason;
  float phongExponent = 0.0f;
  float phongBoost = 0.0f;
  float phongFresnelMin = 0.0f;
  float phongFresnelMax = 0.0f;
  float phongTint[3] = {0.0f, 0.0f, 0.0f};
  // Bump
  tf2::native::ViewModelEffectState bumpState = tf2::native::ViewModelEffectState::Unavailable;
  bool bumpDeclared = false;
  bool bumpParsed = false;
  bool bumpUploaded = false;
  bool bumpApplied = false;
  std::uint32_t bumpAppliedRanges = 0;
  bool bumpFallback = false;
  std::string bumpReason;
  std::string bumpTexturePath;
  bool bumpTextureResolved = false;
  bool bumpTextureUploaded = false;
  bool bumpTextureReused = false;
  // Selfillum
  tf2::native::ViewModelEffectState selfillumState = tf2::native::ViewModelEffectState::Unavailable;
  bool selfillumDeclared = false;
  bool selfillumParsed = false;
  bool selfillumUploaded = false;
  bool selfillumApplied = false;
  std::uint32_t selfillumAppliedRanges = 0;
  bool selfillumFallback = false;
  std::string selfillumReason;
  bool selfillumMaskDeclared = false;
  bool selfillumMaskBound = false;
  float selfillumTint[3] = {0.0f, 0.0f, 0.0f};
  // The whole-run texture bookkeeping. `texturesUploaded` is cumulative and is
  // NOT reset per frame: that is the reading that catches a pass which rebuilds
  // its textures every frame, because the number keeps climbing.
  std::uint32_t texturesUploaded = 0;
  std::uint32_t texturesReused = 0;
};

enum class ModelGpuStatus { NotLoaded, BindPoseOnly, SkinningReady, SkinningMissing, ViewModelUnknown };


class Renderer {
public:
  bool initialize(HWND window);
  void resize(UINT width, UINT height);
  void orbitCamera(float deltaX, float deltaY);
  void zoomCamera(float delta);
  void setObserverFocus(float x, float y, float z);
  void setObserverFocusWorld(float x, float y, float z);
  void setObserverDemoViewWorld(float x, float y, float z, float pitchDegrees, float yawDegrees);
  void clearObserverDemoView();
  bool observerFocusWorld(float& x, float& y, float& z) const;
  // T10 fix: the observer focus converted BACK to map coordinates. The pvs
  // clip asks the BSP which cluster a point sits in, and the BSP speaks map
  // units; observerFocusWorld() speaks the normalized unit-box the world
  // buffer was scaled into. Handing the normalized point to clusterAt asked
  // "which leaf contains (±0.4, ±0.9, 0.1..0.9) in map units" -- a fixed spot
  // near the map origin, so every camera read as one cluster and the clip hid
  // an arbitrary half of the map. The inverse lives here, next to the forward
  // conversion and its constants, so the two cannot drift apart.
  bool observerFocusMapWorld(float& x, float& y, float& z) const;
  // T21: the constants the last world draw uploaded. Read after a frame, like
  // the vis and cull readings: a caller that predicts a pixel has to use the
  // numbers the shader was given, not the settings that were meant to produce
  // them.
  WorldShaderConstants worldShaderConstants() const;
  // T21: the depth the captured frame actually has at a pixel, read back with
  // the colour buffer. It answers "is the pixel the face the trace picked?"
  // without assuming it: a model, the water pass or another world triangle in
  // front all write a different depth, and a colour comparison over such a
  // pixel would be a comparison with somebody else's surface. Returns a
  // negative value when no depth readback was taken.
  float capturedDepthAt(int x, int y) const;
  // The depth of the FINAL frame -- after the ViewModel pass cleared the world
  // depth and drew the weapon over it. A pixel whose final depth is nearer than
  // the world depth is not the world face any more, whatever its colour is.
  float capturedFinalDepthAt(int x, int y) const;
  void captureDepthBuffer(bool finalPass);
  // The raw 32-bit texel behind capturedDepthAt, for diagnosing a driver whose
  // D24S8 packing is not the one the decode assumes.
  std::uint32_t capturedDepthRawAt(int x, int y) const;
  int capturedDepthWidth() const { return capturedDepthWidth_; }
  int capturedDepthHeight() const { return capturedDepthHeight_; }
  void setWorldMaterialParams(const WorldMaterialParams& params);
  void setProjectileTimeline(const std::vector<ProjectileTimelineEvent>& events, std::int32_t tick);
  void setCpuParticleTimeline(const std::vector<ProjectileTimelineEvent>& events, std::int32_t tick);
  void setEntityMarkers(const std::vector<EntityMarker>& markers);
  bool uploadEntityModelMesh(const std::string& cacheKey, const std::vector<ModelDrawVertex>& vertices);
  bool uploadEntityModelTexture(const std::string& cacheKey, const std::vector<std::uint8_t>& rgba, UINT width, UINT height);
  void setEntityModelInstances(const std::vector<EntityModelDrawInstance>& instances);
  // The bone matrices every skinned instance indexes into, concatenated. Uploaded
  // once per scene update rather than per draw: a demo can carry 96 instances and
  // a per-draw structured-buffer recreate would dominate the frame.
  void setEntityBoneMatrices(const std::vector<std::array<float, 16>>& boneMatrices);
  std::size_t entitySkinnedInstanceCount() const { return entityModelSkinnedInstanceCount_; }
  std::size_t entityBoneMatrixCount() const { return entityBoneMatrixCount_; }
  std::size_t entityModelMeshCount() const { return entityModelMeshes_.size(); }
  std::size_t entityModelInstanceCount() const { return entityModelInstanceCount_; }
  std::size_t entityModelVertexCount() const { return entityModelVertexCount_; }
  std::size_t entityModelTexturedRangeCount() const { return entityModelTexturedRangeCount_; }
  std::size_t entityModelTexturedInstanceCount() const { return entityModelTexturedInstanceCount_; }
  std::size_t entityModelDrawRangeCount() const { return entityModelDrawRanges_.size(); }
  std::size_t projectileVertexCount() const { return projectileVertexCount_; }
  const WorldMaterialParams& worldMaterialParams() const { return worldMaterialParams_; }
  // The env mode a world draw would use now: 0 none, 1 the 2D approximation,
  // 2 the map's six-face cube.
  UINT worldEnvMode() const;
  WorldLightmapStatus worldLightmapStatus() const { return worldLightmapStatus_; }
  WorldMaterialSampleState worldMaterialSampleState() const;
  // T12: the counts behind the terrain blend path (see WorldBlendState).
  WorldBlendState worldBlendState() const;
  // T12: the atlas edge in pixels, its byte size, and the reason it is or is
  // not bound. "bound" is the only value that means the atlas is what the
  // world pass samples.
  // T19: `size` and `bytes` stay the single-atlas readings they were -- the
  // largest one the pass bound -- so a gate written against one atlas keeps
  // meaning what it meant. `atlases` and `totalBytes` are what the batching
  // added: how many there are and what they cost together.
  std::size_t worldAtlasSize() const { return worldAtlasSize_; }
  std::uint64_t worldAtlasBytes() const { return worldAtlasBytes_; }
  std::size_t worldAtlasCount() const { return worldAtlasCount_; }
  std::uint64_t worldAtlasTotalBytes() const { return worldAtlasTotalBytes_; }
  const std::string& worldAtlasReason() const { return worldAtlasReason_; }
  // T19: the per-material disposition of the last atlas upload. Kept as a table
  // rather than folded into the counters because the product's trace names each
  // material and its pvs-visible triangle count next to it -- a count of 51
  // refused tiles answers "how many" and not "which", and the task's question
  // is the second one.
  const std::vector<WorldTileReading>& worldTileTable() const { return worldTileTable_; }
  // T19: the t0 ledger's counters, read after a frame like the draw flags.
  std::size_t weaponDrawsWithWorldAtlas() const { return weaponDrawsWithWorldAtlas_; }
  std::size_t worldDrawsWithWeaponTexture() const { return worldDrawsWithWeaponTexture_; }
  std::size_t slot0BindCount() const { return slot0Binds_; }
  std::size_t worldBatchDrawCallCount() const { return worldBatchDrawCalls_; }
  std::size_t worldAtlasBatchCount() const { return worldAtlasBatches_; }
  std::size_t worldAtlasBlendBatchCount() const { return worldAtlasBlendBatches_; }
  bool saveCameraPreset(std::size_t slot);
  bool applyCameraPreset(std::size_t slot);
  bool clearCameraPreset(std::size_t slot);
  void clearCameraPresets();
  void resetCamera();
  bool draw(float clearRed, float clearGreen, float clearBlue);
  bool uploadTexture(const std::vector<std::uint8_t>& rgba, UINT width, UINT height);
  bool uploadWorldGeometry(const BspMap& map, const std::string& texturedMaterial, UINT textureWidth, UINT textureHeight);
  bool uploadWorldGeometry(const BspMap& map, const std::vector<WorldTexture>& textures);
  bool uploadWorldAuxTextures(const std::vector<std::uint8_t>& bumpRgba, UINT bumpWidth, UINT bumpHeight,
    const std::vector<std::uint8_t>& envRgba, UINT envWidth, UINT envHeight);
  bool uploadWorldLightmap(const std::vector<std::uint8_t>& rgba, UINT width, UINT height);
  // Six decoded cube faces, in Source's face order (+X,-X,+Y,-Y,+Z,-Z), each
  // side*side RGBA. All six must be present, square, and the same size: the
  // upload refuses anything else rather than filling the missing faces with
  // black, which would sample as "the map is dark in that direction".
  bool uploadWorldCubemap(const std::array<std::vector<std::uint8_t>, 6>& faces,
    UINT side, std::string* error = nullptr);
  // ---- T1 (2026-10-13): the skybox as geometry -----------------------------
  // Six sides at their own sizes. A cube needs six equal squares and the content
  // does not have them (this install's sky_well_01dn.vtf is 1x1 beside five
  // 512x256/512x512 siblings), so the uploader picks the smallest power of two
  // that covers every decoded side and resamples to it -- and reports that it
  // did, because "the sky is drawn" and "the sky is drawn at its own pixels"
  // are different claims and only the second one is a sky.
  //
  // Fewer than six sides is refused rather than padded with black: a black side
  // would sample as "the map is dark in that direction", which is a lie about
  // the content rather than an admission about the upload.
  bool uploadWorldSky(const std::array<WorldSkyFace, 6>& faces, std::string* error = nullptr);
  // The box, 6 vertices per side in D3D11 cube order, built by
  // evaluateSkyDrawRequest -- the same pure function the probe asserts, so the
  // vertices the product draws are the vertices the gate counted.
  bool uploadWorldSkyGeometry(const std::vector<WorldSkyVertex>& vertices);
  void setWorldSkyEnabled(bool enabled, const std::string& reason = {}) {
    worldSkyEnabled_ = enabled;
    if (!reason.empty()) worldSkyReason_ = reason;
  }
  // The pvs verdict for the current camera cluster. An empty index list means
  // "draw everything"; a non-empty one is drawn indexed, so the hidden
  // triangles leave the rasteriser as well as the reading.
  void setWorldVisClipEnabled(bool enabled);
  // T10 diagnostic: rasterise the world pass without back-face culling, so the
  // same frame can be drawn culled and unculled and the difference attributed
  // to the winding. Diagnostic only; the product's default stays cull-back.
  void setWorldBackfaceCull(bool enabled) { worldBackfaceCull_ = enabled; }
  // T11 diagnostic: 0 = textured (the product path), 1 = flat colour only,
  // 2 = depth greyscale. Diagnostic only; the product's default stays 0.
  void setWorldDebugView(int view) { worldDebugView_ = view; }
  bool setWorldVisDrawList(const WorldVisDrawList& drawList);
  // ---- T2 (2026-10-14): the water surface as its own pass ----------------
  // A second world buffer holding only the water triangles, its own texture and
  // its own constants. It is a separate pass over a separate buffer precisely so
  // that turning it off cannot change what the world pass draws: the world
  // vertex buffer, the pvs clip and the sky pass are untouched, and
  // `drawnTriangles + hiddenTriangles == inputTriangles` still holds on the
  // world pass with the water on, off, or absent.
  //
  // The vertices it takes are the ones `evaluateWaterDrawRequest` already
  // warped, so the warp has one implementation and the reading that counted the
  // triangles counted the ones that were uploaded.
  bool uploadWorldWaterGeometry(const std::vector<WorldWaterVertex>& vertices);
  // The water material's own texture. An empty buffer is a fallback, not a
  // failure: the pass then draws the flat shade and says so.
  bool uploadWorldWaterTexture(const std::vector<std::uint8_t>& rgba, UINT width, UINT height);
  void setWorldWaterEnabled(bool enabled) { worldWaterEnabled_ = enabled; }
  // ---- T12 (2026-10-18): the terrain blend's two diagnostic switches ------
  // `setWorldBlendEnabled(false)` keeps the world geometry and the first layer
  // exactly as they are and refuses the second layer, so the only difference
  // between the two frames is the blend itself. That is the "off" half of the
  // on/off/repeat triple the task asks for, and it has to be a renderer-side
  // switch rather than a main.cpp-side one: dropping the layer in main.cpp
  // would change the atlas contents as well, and then a differing pixel could
  // be attributed to the atlas rather than to the shader branch.
  void setWorldBlendEnabled(bool enabled) { worldBlendEnabled_ = enabled; }
  // ---- T27 (2026-10-19): the material-feature switches -------------------
  // One switch per semantic, and each one is a renderer-side switch for the
  // same reason the blend switch is: turning it off has to leave the geometry,
  // the albedo atlas and the uv exactly as they are, so the two frames differ
  // by the normal/detail branch alone. `false` means the atlas is not built at
  // all and no vertex carries the bit, which is also what makes the default
  // frame byte-identical to the pre-T27 build.
  void setWorldMaterialBumpEnabled(bool enabled) { worldMaterialBumpEnabled_ = enabled; }
  void setWorldBump2Enabled(bool enabled) { worldBump2Enabled_ = enabled; }
  // T27: the layer-2 diagnostic base. The product path lerps layer 2 into the
  // material's own layer-1 normal (Source's BUMPMAP2 block is compiled under
  // BUMPMAP), so a material whose layer 1 is refused -- the 36 `$ssbump`
  // materials -- has no base for the lerp and must not carry the bit. The
  // diagnostic switch exists because the one material on this map that does have
  // both layers is under the terrain at both gate cameras: it lets the lerp be
  // measured on the base normal the pass already computed, with the trace naming
  // that base. It is a measurement of the shader arm, not a claim about Source's
  // bumpBasis.
  void setWorldBump2Diagnostic(bool enabled) { worldBump2Diagnostic_ = enabled; }
  void setWorldDetailEnabled(bool enabled) { worldDetailEnabled_ = enabled; }
  WorldFeatureState worldBumpFeatureState() const { return worldBumpFeature_; }
  WorldFeatureState worldBump2FeatureState() const { return worldBump2Feature_; }
  WorldFeatureState worldDetailFeatureState() const { return worldDetailFeature_; }
  // `setWorldTerrainOnly(true)` uploads only the triangles whose material is a
  // blend material -- the displacement terrain -- and nothing else. It exists
  // because "the change is in the terrain region" is a claim about *where* the
  // pixels are, and the only honest way to bound that region without assuming a
  // colour or a projection is to draw a frame that contains the terrain and
  // nothing else, from the same camera and the same buffers.
  void setWorldTerrainOnly(bool terrainOnly) { worldTerrainOnly_ = terrainOnly; }
  // T26: `--world-geometry sky` uploads the BSP's sky-brush polygons and
  // nothing else. It is not a picture of the game: it is the only honest answer
  // to "is this empty pixel sky by design or is it a hole", because it is the
  // map's own sky geometry through the product's own camera, projection and
  // world transform. The product never draws this buffer -- sky faces are not
  // in `map.triangles` at all.
  void setWorldSkyFaceOnly(bool skyOnly) { worldSkyFaceOnly_ = skyOnly; }
  // T26: bake the face index into the vertex colour so the flat debug view can
  // output a face-id frame. Set before the upload, because the colour is a
  // vertex property; the draw side is `setWorldDebugView(4)`.
  void setWorldFaceIdUpload(bool enabled) { worldFaceIdUpload_ = enabled; }
  // T12: `--world-geometry terrain|none` is a diagnostic frame, not a picture of
  // the game, so every other pass has to leave it alone: the pvs index buffer
  // (built from the full triangle list, so it would index past a filtered vertex
  // buffer), the "no world" placeholder quad (a full-screen textured quad, which
  // is what the `none` frame used to show instead of the background), and the
  // entity, marker and projectile passes. Measured, all three.
  void setWorldDiagnosticIsolation(bool isolated) { worldDiagnosticIsolation_ = isolated; }
  // T17: the lightmap is the one shading term the product had no off switch for,
  // so "the world is dark" could not be attributed to it. `false` keeps the same
  // geometry, materials and uvs and replaces the sample with white, which turns
  // the question into a frame comparison instead of an argument. Diagnostic
  // only; the product path never calls it.
  void setWorldLightmapEnabled(bool enabled) { worldLightmapEnabled_ = enabled; }
  bool worldLightmapEnabled() const { return worldLightmapEnabled_; }
  // The judge's verdict, handed in rather than recomputed. `worldWaterState_`
  // was never set on the success path of uploadWorldWaterGeometry, so
  // waterSurfaceSampleState() reported `unavailable` for a surface that drew --
  // measured, and it is the reason the mutation that deletes the Draw call first
  // read `draws=0 state=unavailable` instead of the intended `draws=0
  // state=active`. The state is a judgement about the request, the draw count is
  // a fact about the pass, and keeping them in one place made the second one
  // unable to move on its own.
  void setWorldWaterReading(tf2::native::WorldWaterSurfaceState state,
    tf2::native::WorldWaterSurfaceState reflection,
    tf2::native::WorldWaterSurfaceState refraction, const std::string& reason);
  // The warp phase, in seconds, derived from the playback tick by the caller.
  // A paused tick is a fixed phase, which is what makes the repeat frame
  // byte-identical rather than "the same wave at a different time".
  void setWorldWaterWarp(double phase, float amplitude);
  // Built fresh on each call rather than cached, so a mutation that deletes the
  // draw changes what this returns rather than what a stale copy holds. That is
  // the same discipline the sky and vis readings use. Returned by value because
  // a reading assembled from the current fields is not a stored fact: a
  // reference would let two callers in one frame see different water.
  WaterSurfaceSampleState waterSurfaceSampleState() const;
  // The world transform uploadWorldGeometry applied, published because the T2
  // water pass has to place its own vertices in exactly that space. Reading it
  // back here rather than recomputing it in main.cpp is what stops the water
  // from being drawn in a different place from the water the world pass already
  // drew -- a second transform is a second thing that can disagree with the
  // first.
  float worldCenterX() const { return worldCenterX_; }
  float worldCenterY() const { return worldCenterY_; }
  float worldMinZ() const { return worldMinZ_; }
  float worldSpanZ() const { return worldSpanZ_; }
  float worldHorizontalScale() const { return worldHorizontalScale_; }
  // Whether those five numbers describe an upload that happened. The initial
  // values (centre 0, scale 1) are also legitimate values for a map that needs
  // no scaling, so they cannot be used as a sentinel -- which is exactly the
  // mistake the T2 water pass made on its first run: it read the transform
  // before uploadWorldGeometry had set it, built its vertices in raw map
  // coordinates, drew 36 triangles at the wrong scale and changed no pixel while
  // reporting draws=1. This flag is the guard that makes that unreachable.
  bool worldBoundsValid() const { return worldBoundsValid_; }
  // ---- T2 (2026-10-14): the ViewModel's advanced materials ----------------
  // One call per frame carrying the three parsed readings and the material's own
  // parameters. It is a struct rather than three setters because the three
  // effects are uploaded together and a partially-applied set (Phong on, Bump
  // off, last frame's Selfillum still bound) is exactly the stale-state defect
  // this round is meant to make impossible.
  struct ViewModelMaterialEffects {
    tf2::native::ViewModelPhongReading phong;
    tf2::native::ViewModelBumpReading bump;
    tf2::native::ViewModelSelfillumReading selfillum;
    // True when the shader branches for all three compiled. A branch that did
    // not compile is a fallback for every effect, not for one of them, because
    // the three share one pixel shader.
    bool shaderReady = false;
  };
  void setViewModelMaterialEffects(const ViewModelMaterialEffects& effects);
  // The normal map, keyed separately from the albedo cache so a bump texture
  // can never be uploaded as a base texture and the albedo key stays a
  // one-texture-one-key mapping.
  bool uploadViewModelBumpTexture(const std::vector<std::uint8_t>& rgba, UINT width, UINT height);
  // The selfillum mask. Empty is the material's own default (Source masks by
  // the albedo's alpha when no `$selfillummask` is given), so the pass reports
  // `maskFromAlpha` rather than treating a missing file as black.
  bool uploadViewModelSelfillumMask(const std::vector<std::uint8_t>& rgba, UINT width, UINT height);
  // GPU normal-map textures created over the whole run. Monotone, and never reset
  // per frame: read across two frames, a constant value with a growing reuse
  // count is a cache and a growing value is a per-frame rebuild.
  std::uint32_t viewModelBumpTextureUploads() const { return viewModelBumpTexturesUploaded_; }
  // Built fresh on each call, like the water reading, so a mutation that deletes
  // a branch changes what this returns rather than what a stale copy holds.
  ViewModelMaterialSampleState viewModelMaterialSampleState() const;
  bool uploadProjectileLines(const std::vector<ProjectileLine>& lines);
  bool uploadBindPoseModel(const std::vector<ModelDrawVertex>& vertices);
  bool uploadBoneMatrices(const std::vector<std::array<float, 16>>& boneMatrices, bool enableSkinning);
  ModelGpuStatus modelGpuStatus() const { return modelGpuStatus_; }
  void setViewModelFov(float fov) { settings_.viewModelFov = clampViewModelFov(fov); }
  float viewModelFov() const { return settings_.viewModelFov; }

  // ---- First-person ViewModel pass (T2) -----------------------------------
  // Its own vertex buffer, bone buffer, constant buffer, shader, projection and
  // depth state. It shares no buffer with the world or entity path: the world
  // pass normalises geometry into the map's unit box and writes depth with the
  // world policy, so a ViewModel routed through it would be scaled into the map
  // and occluded by it. Reusing `worldConstants_` here would be exactly that
  // mistake, which is why the pass owns `viewModelConstants_` instead.
  bool uploadViewModelMesh(const std::vector<ModelDrawVertex>& vertices,
    const std::vector<ViewModelDrawRange>& ranges = {});
  // The material cache, keyed by the **normalized resource path** of the VTF
  // (`materials/models/weapons/v_scattergun/v_scattergun.vtf`). Never keyed by
  // the VMT's raw `$basetexture` value: on this install that value is written
  // with backslashes in some VMTs and with forward slashes in others, so the raw
  // string gives one texture two keys and the cache stops being a cache.
  //
  // Returns true only when a texture was created. A key already in the cache is a
  // false and a reuse, which is what makes `texturesUploaded` a statement about
  // GPU work rather than about how many times the caller asked.
  bool uploadViewModelMaterialTexture(const std::string& key,
    const std::vector<std::uint8_t>& rgba, UINT width, UINT height);
  void clearViewModelMaterials();
  std::size_t viewModelMaterialCacheSize() const { return viewModelMaterialTextures_.size(); }
  // GPU textures created for this pass over the whole run. Monotone: it only ever
  // moves when a key was not already cached.
  std::size_t viewModelTexturesUploaded() const { return viewModelTexturesUploaded_; }
  // Binds served out of the cache without touching the device. Read together
  // with the two above, this is the per-frame-recreation reading: `uploaded`
  // constant and `reused` growing is a cache; both growing per frame is not.
  std::size_t viewModelTexturesReused() const { return viewModelTexturesReused_; }
  std::size_t viewModelDrawRangeCount() const { return viewModelDrawRanges_.size(); }
  // Ranges on the last draw that had no usable material: no slot, no cached
  // texture for the slot, or no material table at all. They draw the flat colour.
  std::size_t viewModelFallbackRanges() const { return viewModelFallbackRanges_; }
  // Distinct material slots bound on the last draw. Not the same as the number of
  // ranges: two ranges can share one slot.
  std::size_t viewModelBoundSlots() const { return viewModelBoundSlots_; }
  // Ranges drawn with alpha blending on the last draw, from the material's own
  // `$translucent` key.
  std::size_t viewModelBlendedRanges() const { return viewModelBlendedRanges_; }
  std::size_t viewModelAlphaTestedRanges() const { return viewModelAlphaTestedRanges_; }
  bool uploadViewModelBones(const std::vector<std::array<float, 16>>& boneMatrices);
  // Empties the pass's bone table. The table is re-uploaded on every scene update
  // while a pose is being applied, so a frame that does not apply one has to be
  // able to say "there is no table": without this the previous frame's upload
  // stays in the count and `boneTableCoverage` reports a comparison that did not
  // run. It is a named call rather than `uploadViewModelBones({})` because the
  // latter returns false -- meaning "nothing was uploaded" -- and reading that
  // false as "the table is now empty" would be relying on a side effect of a
  // failure.
  void clearViewModelBones() { viewModelBoneUploadCount_ = 0; }
  void setViewModelState(const ViewModelDrawState& state);
  // Draws one ViewModel. Called from draw() after the world and entity passes
  // and before the frame capture, so the frame that gets written to disk is the
  // one that already contains the ViewModel. Returns true only when a draw was
  // actually issued.
  bool drawViewModel();
  std::size_t viewModelDrawCount() const { return viewModelDrawCount_; }
  std::size_t viewModelDrawCallCount() const { return viewModelDrawCallCount_; }
  std::size_t viewModelVertexCount() const { return viewModelVertexCount_; }
  std::size_t viewModelBoneUploadCount() const { return viewModelBoneUploadCount_; }
  float viewModelMeshRadius() const { return viewModelMeshRadius_; }
  // ---- The weapon body: a second, independent draw request ----------------
  // The first-person composite the T2.5 investigation established is two models:
  // the arms (`models/weapons/c_models/c_<class>_arms.mdl`) and the static
  // weapon body the weapon entity's own `m_iWorldModelIndex` names
  // (`c_medigun.mdl`, `c_pro_rifle.mdl`). T2 drew the arms; this is the other
  // half, and it is a SEPARATE MESH SLOT AND A SEPARATE DRAW CALL rather than a
  // second range in the arms buffer: the two models have their own vertex
  // counts, their own measured bounds (the placement distance is derived from
  // the mesh's own radius), their own transform state and their own draw
  // reason, and folding them into one buffer would make "which model did this
  // draw" unanswerable from the readings. The two draws share the pass's
  // pipeline objects (shaders, input layout, constants buffer, depth and
  // rasterizer states) and the depth clear -- clearing between them would erase
  // the arms the first draw just wrote.
  //
  // T14 (2026-10-18): the ranges are a parameter, for the same reason they are
  // one on the arms' upload. `c_pro_rifle.mdl` draws two LOD-0 descriptors -- the
  // rifle on material slot 0 and its scope on slot 1 -- and the pass used to
  // draw both from `state.materialKey`, which is one slot. A weapon body whose
  // second range is painted with the first range's albedo is wrong in a way no
  // counter saw: the draw count, the vertex count and the material chain all
  // read as correct. Each range now carries its own key and its own blend state.
  bool uploadViewModelWeaponMesh(const std::vector<ModelDrawVertex>& vertices,
    const std::vector<ViewModelDrawRange>& ranges = {});
  void setViewModelWeaponState(const ViewModelDrawState& state);
  const ViewModelDrawState& viewModelWeaponState() const { return viewModelWeaponState_; }
  // The weapon body's material readings. They are separate accessors from the
  // arms' on purpose: the two draws share the texture cache but not their state,
  // and "the arms bound two slots" must not be able to answer for the body.
  std::size_t viewModelWeaponDrawRangeCount() const { return viewModelWeaponDrawnRanges_; }
  std::size_t viewModelWeaponTexturedRanges() const { return viewModelWeaponTexturedRanges_; }
  std::size_t viewModelWeaponFallbackRanges() const { return viewModelWeaponFallbackRanges_; }
  std::size_t viewModelWeaponBoundSlots() const { return viewModelWeaponBoundSlots_; }
  // The normalized VTF paths the body bound on the last draw, in range order,
  // comma-separated. A count cannot tell "both ranges used the rifle's albedo"
  // from "each range used its own"; the keys can, and they are the strings a
  // reviewer can compare against the MDL's slot table.
  const std::string& viewModelWeaponMaterialKeys() const { return viewModelWeaponMaterialKeys_; }
  // Per-pass draw-call counters, so `armDrawCalls` and `weaponDrawCalls` are
  // effects rather than claims: each increments at its own Draw. The
  // pre-existing viewModelDrawCount/viewModelDrawCallCount keep their T2 meaning
  // exactly (the arms draw), which is why this pair is separate.
  std::size_t viewModelArmDrawCallCount() const { return viewModelArmDrawCallCount_; }
  std::size_t viewModelWeaponDrawCallCount() const { return viewModelWeaponDrawCallCount_; }
  const std::string& viewModelWeaponDrawReason() const { return viewModelWeaponDrawReason_; }
  float viewModelWeaponMeshRadius() const { return viewModelWeaponMeshRadius_; }
  UINT viewModelWeaponVertexCount() const { return viewModelWeaponVertexCount_; }
  // The weapon body's own centre through its own mvp -- the same reading the
  // arms centre has, so "the weapon is off screen" is answerable for this draw
  // as well.
  const std::array<float, 4>& viewModelWeaponCenterClip() const { return viewModelWeaponCenterClip_; }
  // The pass's depth policy, recorded from the description the state was created
  // with. D3D11 cannot read a state object back, so this is the input to
  // CreateDepthStencilState rather than the device's copy -- stated rather than
  // glossed over. Both values are constants for this pass and are asserted anyway:
  // a change to the depth policy should redden a gate, not quietly change the
  // picture.
  bool viewModelDepthWrite() const { return viewModelDepthWrite_; }
  const std::string& viewModelDepthFuncName() const { return viewModelDepthFunc_; }
  // Whether the *pass* skinned, not whether a caller asked it to. Those differ:
  // a bone table that cannot cover the skeleton is refused (the all-or-nothing
  // gate in drawViewModel) and the bind pose is drawn instead, and reporting the
  // request rather than the effect would make that refusal read as `skinning=1`.
  bool viewModelSkinningEnabled() const { return viewModelEffectiveBoneCount_ > 0; }
  // The product reads the effect above and the pair (viewModelBoneUploadCount(),
  // viewModelState().boneCount) for the table it compared, so there is no
  // separate accessor for the effective count: an accessor nobody calls is a
  // reading nobody checks.
  // What the pass has to skin with, as a reading rather than a claim. A
  // ViewModel is not an entity, so nothing in a demo drives its sequence and the
  // honest state is `missing` -- the pass then draws the model's bind pose, which
  // is a defined picture rather than an animation. The string flips to
  // `bones:<n>` the moment a caller uploads a bone table, so this is not a
  // constant: it is the same counter the all-or-nothing skinning gate reads.
  std::string viewModelAnimationInput() const;
  // Pass ordinals, so "the ViewModel draws after the world" is a reading rather
  // than a claim about the order of the statements in draw().
  std::size_t viewModelPassOrdinal() const { return viewModelPassOrdinal_; }
  std::size_t viewModelWorldPassOrdinal() const { return viewModelWorldPassOrdinal_; }
  const std::string& viewModelDrawReason() const { return viewModelDrawReason_; }
  const ViewModelDrawState& viewModelState() const { return viewModelState_; }
  // Where the mesh centre lands in normalised device coordinates, computed from
  // the same mvp the shader gets. `drawn=1` only says a draw was issued; this
  // says whether the geometry was anywhere the viewport could show, which is the
  // difference between "the pass ran" and "the weapon is on screen".
  const std::array<float, 4>& viewModelCenterClip() const { return viewModelCenterClip_; }
  const std::array<float, 3>& viewModelCenterViewSpace() const { return viewModelCenterViewSpace_; }
  // The frame camera and the placement point the pass solved for. Published so a
  // "the weapon is behind the camera" reading can be traced to the input that
  // caused it instead of being guessed at.
  const float* frameEye() const { return frameEye_; }
  const float* frameForward() const { return frameForward_; }
  const float* viewModelPlacementTarget() const { return viewModelPlacementTarget_.data(); }
  // T18: the framing numbers the placement solve actually used -- the distance it
  // put the mesh centre at, the model radius it derived that distance from, and
  // the near/far planes of the pass's own projection. Published because "the
  // camera is inside the model" is a claim about these four numbers and the mesh
  // extent, and three of them were previously visible only as a local variable.
  const std::array<float, 4>& viewModelFramePlanes() const { return viewModelFramePlanes_; }
  // The composed first-person matrix actually handed to the shader, row-major.
  // Exposed so the hand mirror can be shown to live in the final transform only.
  const std::array<float, 16>& viewModelMvp() const { return viewModelMvp_; }
  void shutdown();
  void setSettings(const RenderSettings& settings) { settings_ = settings; settings_.normalize(); }
  const RenderSettings& settings() const { return settings_; }
  HRESULT lastError() const { return lastError_; }
  // T8 (2026-10-16): clear the recorded error. The device-error policy at the
  // bottom of the main loop used to read `lastError()` without asking what set
  // it, so a resource upload that the load deliberately degraded (a cubemap or
  // texture it could not upload) still reached the policy and ended the program
  // with exit code 3 -- a verdict that reads as "the device failed" although
  // nothing had drawn yet. Clearing it once the load has succeeded is what makes
  // the policy judge the device: after this point the only thing that can set it
  // is this frame's own Present, so a genuine device failure still reports
  // through exactly the same arms (4/2 for device removed or reset, 3 otherwise).
  void clearLastError() { lastError_ = S_OK; }
  // Which step of initialize() refused, as a stable name. Every failure path in
  // there returned the same E_FAIL, so "the renderer did not start" carried no
  // information at all -- and a shader is compiled at run time, which makes a
  // silent startup failure the normal shape of a bad shader edit. The stage is
  // reported next to the HRESULT so the two together name the fault.
  const char* initStage() const { return initStage_; }

  // Diagnostic capture. The back buffer is read back inside draw(), *before*
  // Present(), so the file holds the frame this call actually composed -- not
  // whatever the swap chain happens to leave behind afterwards. The request is
  // consumed once; ask again if you want the next frame too.
  void requestFrameCapture(std::wstring path);
  bool frameCapturePending() const { return !capturePath_.empty(); }
  bool lastFrameCaptureSucceeded() const { return captureSucceeded_; }
  const std::wstring& lastFrameCaptureError() const { return captureError_; }

private:
  struct CameraPreset {
    bool valid = false;
    float focusX = 0.0f;
    float focusY = 0.0f;
    float focusZ = 0.5f;
    float yaw = 0.0f;
    float pitch = 0.55f;
    float distance = 3.0f;
  };

  void releaseTarget();
  bool createTarget(UINT width, UINT height);
  bool createPipeline();
  bool writeBackBufferToFile(const std::wstring& path);
  // T24: the three pass-level lighting controls as the draw will upload them.
  // One implementation each, read by both the constant buffer and the frame
  // sample, so "what the settings asked for" and "what the shader got" cannot
  // drift apart in the trace.
  bool worldDynamicLightingEffective() const { return settings_.dynamicLighting; }
  bool worldSpecularEffective() const { return settings_.specular; }
  bool worldBumpEffective() const {
    return settings_.bumpMapping && worldBumpTexture_.view() != nullptr;
  }
  // The six-array-slice cube, shared by the environment cube and the sky: one
  // implementation, so the cube cannot be right for one and wrong for the other.
  bool createCubeTexture(const std::array<std::vector<std::uint8_t>, 6>& faces, UINT side,
    Microsoft::WRL::ComPtr<ID3D11Texture2D>& outTexture,
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>& outView, std::string* error);

  HWND window_ = nullptr;
  UINT width_ = 0;
  UINT height_ = 0;
  Microsoft::WRL::ComPtr<IDXGISwapChain> swapChain_;
  Microsoft::WRL::ComPtr<ID3D11Device> device_;
  Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
  Microsoft::WRL::ComPtr<ID3D11RenderTargetView> target_;
  Microsoft::WRL::ComPtr<ID3D11Texture2D> depthTexture_;
  Microsoft::WRL::ComPtr<ID3D11DepthStencilView> depthStencilView_;
  Microsoft::WRL::ComPtr<ID3D11DepthStencilState> depthStencilState_;
  Microsoft::WRL::ComPtr<ID3D11DepthStencilState> worldSkyDepthStencilState_;
  Microsoft::WRL::ComPtr<ID3D11RasterizerState> rasterizerState_;
  Microsoft::WRL::ComPtr<ID3D11VertexShader> vertexShader_;
  Microsoft::WRL::ComPtr<ID3D11VertexShader> worldVertexShader_;
  Microsoft::WRL::ComPtr<ID3D11VertexShader> modelVertexShader_;
  Microsoft::WRL::ComPtr<ID3D11VertexShader> entityModelVertexShader_;
  Microsoft::WRL::ComPtr<ID3D11PixelShader> worldPixelShader_;
  Microsoft::WRL::ComPtr<ID3D11PixelShader> modelPixelShader_;
  Microsoft::WRL::ComPtr<ID3D11InputLayout> worldInputLayout_;
  Microsoft::WRL::ComPtr<ID3D11InputLayout> modelInputLayout_;
  Microsoft::WRL::ComPtr<ID3D11InputLayout> entityModelInputLayout_;
  Microsoft::WRL::ComPtr<ID3D11Buffer> worldVertexBuffer_;
  Microsoft::WRL::ComPtr<ID3D11Buffer> worldConstants_;
  Microsoft::WRL::ComPtr<ID3D11Buffer> modelSkinningConstants_;
  UINT worldVertexCount_ = 0;
  // How many of worldVertexCount_ carry a per-face light uv. Kept beside the
  // total so the invariant lightmapped <= total survives a device reset: both
  // are zeroed together in shutdown().
  UINT worldLightmappedVertexCount_ = 0;
  Microsoft::WRL::ComPtr<ID3D11Buffer> projectileVertexBuffer_;
  std::size_t projectileVertexCapacity_ = 0;
  UINT projectileVertexCount_ = 0;
  Microsoft::WRL::ComPtr<ID3D11Buffer> entityMarkerVertexBuffer_;
  std::size_t entityMarkerVertexCapacity_ = 0;
  UINT entityMarkerVertexCount_ = 0;
  Microsoft::WRL::ComPtr<ID3D11Buffer> modelVertexBuffer_;
  UINT modelVertexCount_ = 0;
  ModelGpuStatus modelGpuStatus_ = ModelGpuStatus::NotLoaded;
  struct EntityModelMesh {
    Microsoft::WRL::ComPtr<ID3D11Buffer> vertexBuffer;
    UINT vertexCount = 0;
    // The model's own paint, resolved from its MDL texture table. Empty until
    // uploadEntityModelTexture succeeds; a null view keeps the world atlas.
    Texture2D texture;
  };
  struct EntityModelDrawRange {
    ID3D11Buffer* vertexBuffer = nullptr;
    UINT vertexCount = 0;
    UINT instanceStart = 0;
    UINT instanceCount = 0;
    // Non-owning. Points at the mesh's own Texture2D so the draw loop can bind
    // t0 per range; null means "no model material, fall back to the world atlas".
    ID3D11ShaderResourceView* textureView = nullptr;
  };
  std::unordered_map<std::string, EntityModelMesh> entityModelMeshes_;
  std::vector<EntityModelDrawRange> entityModelDrawRanges_;
  Microsoft::WRL::ComPtr<ID3D11Buffer> entityModelInstanceBuffer_;
  std::size_t entityModelInstanceCapacity_ = 0;
  // Per-instance skin matrices, bound to the entity shader at t4.
  Microsoft::WRL::ComPtr<ID3D11Buffer> entityBoneBuffer_;
  Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> entityBoneView_;
  std::size_t entityBoneCapacity_ = 0;
  std::size_t entityBoneMatrixCount_ = 0;
  UINT entityModelVertexCount_ = 0;
  std::size_t entityModelInstanceCount_ = 0;
  std::size_t entityModelSkinnedInstanceCount_ = 0;
  std::size_t entityModelTexturedRangeCount_ = 0;
  std::size_t entityModelTexturedInstanceCount_ = 0;
  // First-person pass. Nothing here is shared with the entity members above.
  Microsoft::WRL::ComPtr<ID3D11VertexShader> viewModelVertexShader_;
  Microsoft::WRL::ComPtr<ID3D11PixelShader> viewModelPixelShader_;
  Microsoft::WRL::ComPtr<ID3D11InputLayout> viewModelInputLayout_;
  Microsoft::WRL::ComPtr<ID3D11Buffer> viewModelVertexBuffer_;
  UINT viewModelVertexCount_ = 0;
  // The runs of the buffer that share a material, clamped to what was uploaded.
  // Empty means "no range information was supplied", in which case the whole
  // buffer is one range with no material -- exactly the pre-T1 draw.
  std::vector<ViewModelDrawRange> viewModelDrawRanges_;
  // GPU textures for this pass, keyed by the normalized VTF resource path.
  std::map<std::string, Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>> viewModelMaterialTextures_;
  std::size_t viewModelTexturesUploaded_ = 0;
  std::size_t viewModelTexturesReused_ = 0;
  std::size_t viewModelFallbackRanges_ = 0;
  std::size_t viewModelBoundSlots_ = 0;
  std::size_t viewModelBlendedRanges_ = 0;
  std::size_t viewModelAlphaTestedRanges_ = 0;
  // SrcAlpha / InvSrcAlpha, for a range whose material declares `$translucent`.
  // The pass is opaque by default: a weapon is not glass, and blending every
  // range would make the alpha channel of an opaque DXT5 weapon composite it
  // against the map behind it.
  Microsoft::WRL::ComPtr<ID3D11BlendState> viewModelAlphaBlendState_;
  // The mesh's own bounds, measured at upload. The ViewModel is authored in
  // Source units (tens of them across) and the pass has its own projection, so
  // the distance it sits from the eye has to come from its own size: placing it
  // a fixed 0.30 units out, as a world-scaled camera would, puts the whole model
  // behind the near plane and the pass draws nothing visible.
  float viewModelMeshCenter_[3] = {};
  float viewModelMeshRadius_ = 0.0f;
  // The weapon body slot: same rules as the arms slot, separate buffers because
  // the two models are uploaded and switched independently (`--viewmodel-render
  // command` can hold the arms slot while the entity's weapon body stays live,
  // and a path change on either side re-uploads only its own slot).
  Microsoft::WRL::ComPtr<ID3D11Buffer> viewModelWeaponVertexBuffer_;
  UINT viewModelWeaponVertexCount_ = 0;
  float viewModelWeaponMeshCenter_[3] = {};
  float viewModelWeaponMeshRadius_ = 0.0f;
  ViewModelDrawState viewModelWeaponState_{};
  // The body's own range table, uploaded with its mesh. Empty means "no range
  // information", which is the whole-buffer draw this pass issued before T14;
  // it is kept as that rather than turned into a one-range fallback, because the
  // two produce the same picture and saying so costs a branch.
  std::vector<ViewModelDrawRange> viewModelWeaponDrawRanges_;
  // Last-draw readings, reset at the top of the weapon section so a disabled or
  // failed body cannot inherit the previous frame's counts.
  std::size_t viewModelWeaponDrawnRanges_ = 0;
  std::size_t viewModelWeaponTexturedRanges_ = 0;
  std::size_t viewModelWeaponFallbackRanges_ = 0;
  std::size_t viewModelWeaponBoundSlots_ = 0;
  std::string viewModelWeaponMaterialKeys_;
  std::string viewModelWeaponDrawReason_ = "the weapon body pass never ran";
  std::array<float, 4> viewModelWeaponCenterClip_{};
  std::size_t viewModelArmDrawCallCount_ = 0;
  std::size_t viewModelWeaponDrawCallCount_ = 0;
  Microsoft::WRL::ComPtr<ID3D11Buffer> viewModelBoneBuffer_;
  Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> viewModelBoneView_;
  std::size_t viewModelBoneCapacity_ = 0;
  std::size_t viewModelBoneUploadCount_ = 0;
  // Set by drawViewModel to the number of matrices it actually skinned with.
  std::size_t viewModelEffectiveBoneCount_ = 0;
  // Read off the depth description at createPipeline time.
  bool viewModelDepthWrite_ = false;
  std::string viewModelDepthFunc_ = "unset";
  Microsoft::WRL::ComPtr<ID3D11Buffer> viewModelConstants_;
  // A depth state of its own. The pass clears depth before it draws, so the
  // weapon is never clipped by the map it is standing in -- the same policy the
  // game uses, and not something the world depth state can express.
  Microsoft::WRL::ComPtr<ID3D11DepthStencilState> viewModelDepthStencilState_;
  // Its own rasterizer state. The world state culls back faces with the world's
  // winding; a ViewModel mesh whose triangles wind the other way would then be
  // culled away entirely -- it draws, and no pixel changes. A first-person
  // weapon is one closed model seen from inside its own space, so culling is
  // disabled for this pass rather than guessed at.
  Microsoft::WRL::ComPtr<ID3D11RasterizerState> viewModelRasterizerState_;
  ViewModelDrawState viewModelState_{};
  std::array<float, 16> viewModelMvp_{};
  // The mesh centre in clip space (x, y, z, w). w <= 0 means the point is behind
  // the camera; |x| or |y| > w means it is off the side of the viewport.
  std::array<float, 4> viewModelCenterClip_{};
  // The same point in view space. z here is the distance in front of the camera;
  // a negative z is what a "behind the camera" clip w looks like before the
  // projection divides it.
  std::array<float, 3> viewModelCenterViewSpace_{};
  std::array<float, 3> viewModelPlacementTarget_{};
  // {near, far, distance, radius} of the last ViewModel frame. Zeroed at the top
  // of every draw so a frame that did not draw cannot report the last one's.
  std::array<float, 4> viewModelFramePlanes_{};
  std::size_t viewModelDrawCount_ = 0;
  std::size_t viewModelDrawCallCount_ = 0;
  std::size_t viewModelPassOrdinal_ = 0;
  std::size_t viewModelWorldPassOrdinal_ = 0;
  std::string viewModelDrawReason_ = "the pass never ran";
  // The frame's camera, published by draw() so the ViewModel pass can place the
  // weapon in camera space without recomputing (and without disagreeing with)
  // the view matrix the world pass used.
  DirectX::XMFLOAT4X4 frameView_{};
  float frameEye_[3] = {};
  float worldConvertedMin_[3] = {};
  float worldConvertedMax_[3] = {};
  float frameForward_[3] = {};
  float frameUp_[3] = {};
  float frameRight_[3] = {};
  std::size_t framePassOrdinal_ = 0;
  Microsoft::WRL::ComPtr<ID3D11PixelShader> pixelShader_;
  Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler_;
  Texture2D texture_;
  // T19: one albedo atlas per batch. `worldTexture_` is still the single
  // material texture the non-atlas upload uses; the atlas path never writes it,
  // so a map whose materials all failed still falls back to it and the two
  // paths cannot be confused for each other.
  Texture2D worldTexture_;
  std::vector<Texture2D> worldAtlasTextures_;
  std::vector<Texture2D> worldBlendAtlasTextures_;
  std::vector<Texture2D> worldModulateAtlasTextures_;
  // One draw per batch: the tiles' indices are grouped by batch in the index
  // buffer, so each draw is a contiguous range and each binds its own atlas.
  struct WorldDrawBatch {
    int tiles = 0;
    UINT firstIndex = 0;
    UINT indexCount = 0;
    // The views this batch draws with. Raw pointers into the Texture2D objects
    // that own them: the objects live in the vectors below for the lifetime of
    // the upload, and moving a Texture2D in its vector does not move the
    // resource it holds.
    ID3D11ShaderResourceView* albedoView = nullptr;
    ID3D11ShaderResourceView* blendView = nullptr;
    ID3D11ShaderResourceView* modulateView = nullptr;
    // T27: the feature atlases for this batch. Same tile rectangles as
    // `albedoView`, which is what lets one uv address all of them; null when
    // this batch has no material with the feature (the shader's mask bit is
    // clear for those vertices, so null is never sampled).
    ID3D11ShaderResourceView* bumpView = nullptr;
    ID3D11ShaderResourceView* bump2View = nullptr;
    ID3D11ShaderResourceView* detailView = nullptr;
  };
  std::vector<WorldDrawBatch> worldDrawBatches_;
  // Per uploaded triangle, which batch it belongs to; -1 when the material got
  // no tile. Sized map.triangles.size() and used to regroup the pvs draw list.
  std::vector<std::int16_t> worldTriangleBatch_;
  std::vector<WorldTileReading> worldTileTable_;
  // T21: the constants the last world pass uploaded, kept for the face trace.
  WorldShaderConstants worldShaderConstants_;
  // T21: the depth of the frame that was captured, one float per pixel.
  std::vector<float> capturedDepth_;
  std::vector<std::uint32_t> capturedDepthRaw_;
  std::vector<float> capturedFinalDepth_;
  int capturedDepthWidth_ = 0;
  int capturedDepthHeight_ = 0;
  std::size_t worldTilesTiled_ = 0;
  std::size_t worldTilesRefused_ = 0;
  std::size_t worldTilesRefusedMalformed_ = 0;
  std::size_t worldTilesRefusedDuplicate_ = 0;
  std::size_t worldTilesRefusedBudget_ = 0;
  std::size_t worldAtlasBatches_ = 0;
  std::size_t worldAtlasBlendBatches_ = 0;
  std::uint64_t worldAtlasLayerBytes_ = 0;
  std::string worldAtlasPlanReason_ = "no atlas plan was made";
  // T19: the t0 ledger. Every slot-0 bind in the world and ViewModel passes goes
  // through bindSlot0, so "the weapon drew with the world atlas" and "the world
  // drew with a weapon texture" are measured at the draw rather than argued from
  // the order of the statements.
  ID3D11ShaderResourceView* lastSlot0View_ = nullptr;
  std::string lastSlot0Pass_ = "none";
  std::size_t slot0Binds_ = 0;
  std::size_t weaponDrawsWithWorldAtlas_ = 0;
  std::size_t worldDrawsWithWeaponTexture_ = 0;
  std::size_t worldBatchDrawCalls_ = 0;
  std::size_t worldBatchTexturedDrawCalls_ = 0;
  void bindSlot0(ID3D11ShaderResourceView* view, const char* pass);
  bool isWorldAtlasView(const ID3D11ShaderResourceView* view) const;
  bool isWeaponMaterialView(const ID3D11ShaderResourceView* view) const;
  Texture2D worldBumpTexture_;
  Texture2D worldEnvTexture_;
  Texture2D worldLightmapTexture_;
  // T12: the second and third atlases. They share the base atlas's tile
  // rectangles, so a vertex carries one uv for all three. Both are optional and
  // reset to nothing when no material asked for them.
  Texture2D worldBlendTexture_;
  Texture2D worldBlendModulateTexture_;
  std::size_t worldBlendTiles_ = 0;
  // ---- T27: the per-material feature atlases ------------------------------
  // One atlas per batch per feature, on the base atlas's tile rectangles. They
  // exist only when their switch is on, so an off run allocates exactly what
  // the pre-T27 build allocated and the two frames differ by the shader branch
  // and not by a texture the driver decided to lay out differently.
  std::vector<Texture2D> worldBumpAtlasTextures_;
  std::vector<Texture2D> worldBump2AtlasTextures_;
  std::vector<Texture2D> worldDetailAtlasTextures_;
  WorldFeatureState worldBumpFeature_;
  WorldFeatureState worldBump2Feature_;
  WorldFeatureState worldDetailFeature_;
  bool worldMaterialBumpEnabled_ = false;
  bool worldBump2Enabled_ = false;
  bool worldBump2Diagnostic_ = false;
  bool worldDetailEnabled_ = false;
  std::size_t worldBlendTexturesSeen_ = 0;
  std::size_t worldBlendLayersSeen_ = 0;
  std::size_t worldBlendAccepted_ = 0;
  std::size_t worldBlendAcceptedLayers_ = 0;
  std::size_t worldBlendModulateTiles_ = 0;
  std::size_t worldBlendMalformedLayers_ = 0;
  std::size_t worldBlendVertices_ = 0;
  std::size_t worldBlendModulateVertices_ = 0;
  // T12: the world atlas's own identity and the reason it is or is not bound.
  // The atlas being refused used to be invisible -- the caller fell back to one
  // material texture and every count stayed green.
  std::size_t worldAtlasSize_ = 0;
  std::uint64_t worldAtlasBytes_ = 0;
  std::size_t worldAtlasCount_ = 0;
  std::uint64_t worldAtlasTotalBytes_ = 0;
  std::string worldAtlasReason_ = "no world atlas was uploaded";
  Microsoft::WRL::ComPtr<ID3D11Texture2D> worldCubemapTexture_;
  Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> worldCubemapView_;
  UINT worldCubemapSize_ = 0;
  // ---- T1 (2026-10-13) ----------------------------------------------------
  // The sky box: its own cube, its own vertex buffer, its own constant buffer
  // (the mvp has no translation, because the box follows the camera) and its
  // own rasterizer state (the box is seen from inside, so back-face culling
  // would remove every side of it).
  Microsoft::WRL::ComPtr<ID3D11Texture2D> worldSkyTexture_;
  Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> worldSkyView_;
  Microsoft::WRL::ComPtr<ID3D11Buffer> worldSkyVertexBuffer_;
  Microsoft::WRL::ComPtr<ID3D11Buffer> worldSkyConstants_;
  Microsoft::WRL::ComPtr<ID3D11RasterizerState> worldSkyRasterizerState_;
  // T10 diagnostic: the world rasterizer with CULL_NONE, bound only while
  // worldBackfaceCull_ is false.
  Microsoft::WRL::ComPtr<ID3D11RasterizerState> worldNoCullRasterizerState_;
  // The water pass is coplanar with the world's own water triangles, so it needs
  // a depth bias to avoid z-fighting. Its own state rather than a tweak to the
  // world's: the world pass must keep drawing with the depth policy its gate
  // pins.
  Microsoft::WRL::ComPtr<ID3D11RasterizerState> worldWaterRasterizerState_;
  UINT worldSkyVertexCount_ = 0;
  UINT worldSkySide_ = 0;
  UINT worldSkyIndexCapacity_ = 0;
  bool worldSkyResampled_ = false;
  bool worldSkyEnabled_ = true;
  // Set by the sky draw itself, not recomputed from the same condition the draw
  // tests. The first version of this reading recomputed the guard inside
  // worldMaterialSampleState, and a mutation that deleted the draw still
  // reported draws=1 -- the reading described its own copy of the condition
  // rather than the pass. This field can only be true if the Draw call ran.
  bool worldSkyDrewThisFrame_ = false;
  WorldSkyState worldSkyState_ = WorldSkyState::Unavailable;
  std::string worldSkyReason_ = "no sky box was uploaded";
  // The pvs draw list: an index buffer over the world vertex buffer, rebuilt
  // (main.cpp decides, this only stores) whenever the camera's cluster changes.
  // Dynamic because that can happen several times a second in a demo and the
  // buffer is 3 x triangleCount x 4 bytes.
  Microsoft::WRL::ComPtr<ID3D11Buffer> worldIndexBuffer_;
  UINT worldIndexCapacity_ = 0;
  UINT worldIndexCount_ = 0;
  // Set by the draw itself: true only if the pass took the indexed branch. A
  // reading composed from the clip's own decision would report "clipped" for a
  // draw that handed the whole buffer to the rasteriser.
  bool worldVisDrewIndexed_ = false;
  bool worldVisClipEnabled_ = true;
  bool worldBackfaceCull_ = true;
  int worldDebugView_ = 0;
  bool worldVisClipApplied_ = false;
  std::size_t worldVisDrawnTriangles_ = 0;
  std::size_t worldVisHiddenTriangles_ = 0;
  std::size_t worldVisTrianglesWithoutCluster_ = 0;
  std::size_t worldVisInputTriangles_ = 0;
  int worldVisCameraCluster_ = -1;
  WorldVisClipState worldVisClipState_ = WorldVisClipState::Unavailable;
  std::string worldVisClipReason_ = "no pvs draw list was handed to the renderer";
  // ---- T2 (2026-10-14): the water surface ---------------------------------
  // Its own vertex buffer (water triangles only, already warped) and its own
  // texture. Nothing here is shared with the world pass, so the water switch
  // cannot move the world's readings.
  Microsoft::WRL::ComPtr<ID3D11Buffer> worldWaterVertexBuffer_;
  Texture2D worldWaterTexture_;
  UINT worldWaterVertexCount_ = 0;
  UINT worldWaterTextureSide_ = 0;
  bool worldWaterEnabled_ = true;
  bool worldWaterTextureBound_ = false;
  bool worldWaterDrewThisFrame_ = false;
  // ---- T12 (2026-10-18): the terrain blend ---------------------------------
  // `worldBlendEnabled_` gates the second layer only; `worldTerrainOnly_`
  // gates every triangle whose material is not a blend material. They are two
  // flags rather than one because the gate needs all four combinations: all/on,
  // all/off, terrain/on, terrain/off.
  bool worldBlendEnabled_ = true;
  bool worldTerrainOnly_ = false;
  // T26: upload the sky-brush polygons instead of the world triangles, and/or
  // bake the face index into the vertex colour. Both are diagnostic-only and
  // both are read by `uploadWorldGeometry`, which is why they are set before the
  // upload rather than before the draw.
  bool worldSkyFaceOnly_ = false;
  bool worldFaceIdUpload_ = false;
  bool worldDiagnosticIsolation_ = false;
  float worldBlendWeightMin_ = -1.0f;
  float worldBlendWeightMax_ = -1.0f;
  std::size_t worldBlendDistinctWeights_ = 0;
  double worldWaterPhase_ = 0.0;
  float worldWaterAmplitude_ = 0.02f;
  WorldWaterSurfaceState worldWaterState_ = WorldWaterSurfaceState::Unavailable;
  WorldWaterSurfaceState worldWaterReflectionState_ = WorldWaterSurfaceState::Unavailable;
  WorldWaterSurfaceState worldWaterRefractionState_ = WorldWaterSurfaceState::Unavailable;
  std::string worldWaterReason_ = "no water geometry was uploaded";
  WaterSurfaceSampleState waterSurfaceSample_{};
  // ---- T2 (2026-10-14): the ViewModel's advanced materials ----------------
  // The three readings as handed in, plus the per-frame facts about what the
  // last draw did with them. `viewModelPhongAppliedRanges_` and its two
  // siblings are counted inside drawViewModel from the ranges that actually
  // drew, so a material on a range whose texture never resolved is declared and
  // not applied -- which is the pair the task asks to be able to tell apart.
  ViewModelMaterialEffects viewModelMaterialEffects_{};
  bool viewModelMaterialEffectsSet_ = false;
  Microsoft::WRL::ComPtr<ID3D11Texture2D> viewModelBumpTexture_;
  Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> viewModelBumpView_;
  Microsoft::WRL::ComPtr<ID3D11Texture2D> viewModelSelfillumMaskTexture_;
  Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> viewModelSelfillumMaskView_;
  bool viewModelBumpBound_ = false;
  bool viewModelSelfillumMaskBound_ = false;
  std::uint32_t viewModelPhongAppliedRanges_ = 0;
  std::uint32_t viewModelBumpAppliedRanges_ = 0;
  std::uint32_t viewModelSelfillumAppliedRanges_ = 0;
  std::uint32_t viewModelPhongFallbackRanges_ = 0;
  std::uint32_t viewModelBumpFallbackRanges_ = 0;
  std::uint32_t viewModelSelfillumFallbackRanges_ = 0;
  // Cumulative, never reset per frame: the reading that catches a pass which
  // rebuilds a texture every frame, because the number would keep climbing.
  std::uint32_t viewModelBumpTexturesUploaded_ = 0;
  ViewModelMaterialSampleState viewModelMaterialSample_{};
  UINT worldDisplacedTriangleCount_ = 0;
  UINT worldWaterTriangleCount_ = 0;
  RenderSettings settings_{};
  float cameraYaw_ = 0.0f;
  float cameraPitch_ = 0.36f;
  float cameraDistance_ = 1.65f;
  bool observerFocusValid_ = false;
  float observerFocusX_ = 0.0f;
  float observerFocusY_ = 0.0f;
  float observerFocusZ_ = 0.25f;
  bool observerDemoView_ = false;
  float observerDemoEyeX_ = 0.0f;
  float observerDemoEyeY_ = 0.0f;
  float observerDemoEyeZ_ = 0.0f;
  float observerDemoPitchDeg_ = 0.0f;
  float observerDemoYawDeg_ = 0.0f;
  bool worldBoundsValid_ = false;
  float worldMinX_ = 0.0f;
  float worldMinY_ = 0.0f;
  float worldMinZ_ = 0.0f;
  float worldMaxX_ = 0.0f;
  float worldMaxY_ = 0.0f;
  float worldMaxZ_ = 0.0f;
  float worldCenterX_ = 0.0f;
  float worldCenterY_ = 0.0f;
  float worldSpanZ_ = 1.0f;
  float worldHorizontalScale_ = 1.0f;
  std::array<CameraPreset, 8> cameraPresets_{};
  WorldMaterialParams worldMaterialParams_{};
  WorldLightmapStatus worldLightmapStatus_ = WorldLightmapStatus::Unavailable;
  float worldLightmapIntensity_ = 1.0f;
  bool worldLightmapEnabled_ = true;
  HRESULT lastError_ = S_OK;
  const char* initStage_ = "none";
  std::wstring capturePath_;
  bool captureSucceeded_ = false;
  std::wstring captureError_;
};

} // namespace tf2::native
