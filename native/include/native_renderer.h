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
  bool specular = false;
  bool waterReflection = false;
  bool waterRefraction = true;
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

struct WorldMaterialSampleState {
  WorldLightmapStatus lightmapStatus = WorldLightmapStatus::Unavailable;
  float lightmapIntensity = 1.0f;
  bool bumpTextureBound = false;
  bool envTextureBound = false;
  bool lightmapAtlasBound = false;
  bool selfIllumEnabled = false;
  // Falsifiable vertex counts for the lightmap pass. `lightmappedVertexCount`
  // is how many of the uploaded world vertices carry a per-face light uv; a
  // vertex without one is drawn flat no matter what the atlas holds. The pair
  // (lightmapped / total) is the number that says whether the lightmap pass
  // covers the map, which the status enum on its own cannot: `Active` plus
  // 0/600000 would mean the atlas is bound and nothing samples it.
  std::uint32_t worldVertexCount = 0;
  std::uint32_t lightmappedVertexCount = 0;
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
  void setWorldSkyEnabled(bool enabled) { worldSkyEnabled_ = enabled; }
  // The pvs verdict for the current camera cluster. An empty index list means
  // "draw everything"; a non-empty one is drawn indexed, so the hidden
  // triangles leave the rasteriser as well as the reading.
  void setWorldVisClipEnabled(bool enabled);
  bool setWorldVisDrawList(const WorldVisDrawList& drawList);
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
  bool uploadViewModelWeaponMesh(const std::vector<ModelDrawVertex>& vertices);
  void setViewModelWeaponState(const ViewModelDrawState& state);
  const ViewModelDrawState& viewModelWeaponState() const { return viewModelWeaponState_; }
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
  // The composed first-person matrix actually handed to the shader, row-major.
  // Exposed so the hand mirror can be shown to live in the final transform only.
  const std::array<float, 16>& viewModelMvp() const { return viewModelMvp_; }
  void shutdown();
  void setSettings(const RenderSettings& settings) { settings_ = settings; settings_.normalize(); }
  const RenderSettings& settings() const { return settings_; }
  HRESULT lastError() const { return lastError_; }
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
  float frameForward_[3] = {};
  float frameUp_[3] = {};
  float frameRight_[3] = {};
  std::size_t framePassOrdinal_ = 0;
  Microsoft::WRL::ComPtr<ID3D11PixelShader> pixelShader_;
  Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler_;
  Texture2D texture_;
  Texture2D worldTexture_;
  Texture2D worldBumpTexture_;
  Texture2D worldEnvTexture_;
  Texture2D worldLightmapTexture_;
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
  bool worldVisClipApplied_ = false;
  std::size_t worldVisDrawnTriangles_ = 0;
  std::size_t worldVisHiddenTriangles_ = 0;
  std::size_t worldVisTrianglesWithoutCluster_ = 0;
  std::size_t worldVisInputTriangles_ = 0;
  int worldVisCameraCluster_ = -1;
  WorldVisClipState worldVisClipState_ = WorldVisClipState::Unavailable;
  std::string worldVisClipReason_ = "no pvs draw list was handed to the renderer";
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
  HRESULT lastError_ = S_OK;
  const char* initStage_ = "none";
  std::wstring capturePath_;
  bool captureSucceeded_ = false;
  std::wstring captureError_;
};

} // namespace tf2::native
