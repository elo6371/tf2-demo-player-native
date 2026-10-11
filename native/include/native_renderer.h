#pragma once

#include <Windows.h>
#include <d3d11.h>
#include <DirectXMath.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include <wrl/client.h>

#include "texture2d.h"
#include "bsp_map.h"
#include "model_loader.h"
#include "demo_header.h"

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

struct WorldMaterialSampleState {
  WorldLightmapStatus lightmapStatus = WorldLightmapStatus::Unavailable;
  float lightmapIntensity = 1.0f;
  bool bumpTextureBound = false;
  bool envTextureBound = false;
  bool lightmapAtlasBound = false;
  bool selfIllumEnabled = false;
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
  bool uploadViewModelMesh(const std::vector<ModelDrawVertex>& vertices);
  bool uploadViewModelBones(const std::vector<std::array<float, 16>>& boneMatrices);
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
  // The mesh's own bounds, measured at upload. The ViewModel is authored in
  // Source units (tens of them across) and the pass has its own projection, so
  // the distance it sits from the eye has to come from its own size: placing it
  // a fixed 0.30 units out, as a world-scaled camera would, puts the whole model
  // behind the near plane and the pass draws nothing visible.
  float viewModelMeshCenter_[3] = {};
  float viewModelMeshRadius_ = 0.0f;
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
