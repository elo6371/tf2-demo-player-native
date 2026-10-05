#pragma once

#include <Windows.h>
#include <d3d11.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
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
  float viewModelFov = 80.0f;
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
  bool selfIllumEnabled = false;
};

enum class ModelGpuStatus { NotLoaded, BindPoseOnly, SkinningMissing, ViewModelUnknown };

class Renderer {
public:
  bool initialize(HWND window);
  void resize(UINT width, UINT height);
  void orbitCamera(float deltaX, float deltaY);
  void zoomCamera(float delta);
  void setObserverFocus(float x, float y, float z);
  void setObserverFocusWorld(float x, float y, float z);
  bool observerFocusWorld(float& x, float& y, float& z) const;
  void setWorldMaterialParams(const WorldMaterialParams& params);
  void setProjectileTimeline(const std::vector<ProjectileTimelineEvent>& events, std::int32_t tick);
  void setCpuParticleTimeline(const std::vector<ProjectileTimelineEvent>& events, std::int32_t tick);
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
  bool uploadProjectileLines(const std::vector<ProjectileLine>& lines);
  bool uploadBindPoseModel(const std::vector<ModelDrawVertex>& vertices);
  bool uploadBoneMatrices(const std::vector<std::array<float, 16>>& boneMatrices, bool enableSkinning);
  ModelGpuStatus modelGpuStatus() const { return modelGpuStatus_; }
  void setViewModelFov(float fov) { settings_.viewModelFov = clampViewModelFov(fov); }
  float viewModelFov() const { return settings_.viewModelFov; }
  void shutdown();
  void setSettings(const RenderSettings& settings) { settings_ = settings; settings_.normalize(); }
  const RenderSettings& settings() const { return settings_; }
  HRESULT lastError() const { return lastError_; }

private:
  struct CameraPreset {
    bool valid = false;
    float focusX = 0.0f;
    float focusY = 0.0f;
    float focusZ = 0.25f;
    float yaw = 0.0f;
    float pitch = 0.55f;
    float distance = 3.0f;
  };

  void releaseTarget();
  bool createTarget(UINT width, UINT height);
  bool createPipeline();

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
  Microsoft::WRL::ComPtr<ID3D11PixelShader> worldPixelShader_;
  Microsoft::WRL::ComPtr<ID3D11InputLayout> worldInputLayout_;
  Microsoft::WRL::ComPtr<ID3D11InputLayout> modelInputLayout_;
  Microsoft::WRL::ComPtr<ID3D11Buffer> worldVertexBuffer_;
  Microsoft::WRL::ComPtr<ID3D11Buffer> worldConstants_;
  Microsoft::WRL::ComPtr<ID3D11Buffer> modelSkinningConstants_;
  UINT worldVertexCount_ = 0;
  Microsoft::WRL::ComPtr<ID3D11Buffer> projectileVertexBuffer_;
  UINT projectileVertexCount_ = 0;
  Microsoft::WRL::ComPtr<ID3D11Buffer> modelVertexBuffer_;
  UINT modelVertexCount_ = 0;
  ModelGpuStatus modelGpuStatus_ = ModelGpuStatus::NotLoaded;
  Microsoft::WRL::ComPtr<ID3D11PixelShader> pixelShader_;
  Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler_;
  Texture2D texture_;
  Texture2D worldTexture_;
  Texture2D worldBumpTexture_;
  Texture2D worldEnvTexture_;
  RenderSettings settings_{};
  float cameraYaw_ = 0.0f;
  float cameraPitch_ = 0.9f;
  float cameraDistance_ = 3.0f;
  bool observerFocusValid_ = false;
  float observerFocusX_ = 0.0f;
  float observerFocusY_ = 0.0f;
  float observerFocusZ_ = 0.25f;
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
};

} // namespace tf2::native
