// World materials: atlas/lightmap/cubemap uploads, material sampling and blend state selection.
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


bool Renderer::uploadWorldAuxTextures(const std::vector<std::uint8_t>& bumpRgba, UINT bumpWidth, UINT bumpHeight,
    const std::vector<std::uint8_t>& envRgba, UINT envWidth, UINT envHeight) {
  worldBumpTexture_.reset();
  worldEnvTexture_.reset();
  std::string error;
  if (!bumpRgba.empty() && !worldBumpTexture_.create(device_.Get(), bumpRgba, bumpWidth, bumpHeight, &error)) {
    if (!error.empty()) OutputDebugStringA(("Bump Texture2D upload failed: " + error + "\n").c_str());
    return false;
  }
  error.clear();
  if (!envRgba.empty() && !worldEnvTexture_.create(device_.Get(), envRgba, envWidth, envHeight, &error)) {
    if (!error.empty()) OutputDebugStringA(("Env Texture2D upload failed: " + error + "\n").c_str());
    return false;
  }
  return true;
}

bool Renderer::uploadWorldLightmap(const std::vector<std::uint8_t>& rgba, UINT width, UINT height) {
  worldLightmapTexture_.reset();
  worldCubemapTexture_.Reset();
  worldCubemapView_.Reset();
  worldCubemapSize_ = 0;
  if (rgba.empty() || width == 0 || height == 0) return true;
  std::string error;
  if (!worldLightmapTexture_.create(device_.Get(), rgba, width, height, &error)) {
    if (!error.empty()) OutputDebugStringA(("Lightmap Texture2D upload failed: " + error + "\n").c_str());
    return false;
  }
  return true;
}

UINT Renderer::worldEnvMode() const {
  // Computed from what is bound and what the material asked for, so it is the
  // mode a world draw would send right now. This is deliberately not a member
  // filled in at draw time: the trace that reports it runs at load, before the
  // first world draw, and a member read there would print its initial zero and
  // be mistaken for "the pass chose not to run".
  const bool wanted = settings_.preset == QualityPreset::Standard && worldMaterialParams_.envMap;
  if (!wanted) return 0;
  if (worldCubemapView_) return 2;
  return worldEnvTexture_.view() ? 1u : 0u;
}

const char* worldCubemapStatusName(WorldCubemapStatus status) {
  switch (status) {
    case WorldCubemapStatus::Unavailable: return "unavailable";
    case WorldCubemapStatus::Approximate2d: return "approximate-2d";
    case WorldCubemapStatus::SixFace: return "six-face";
  }
  return "unknown";
}

bool Renderer::uploadWorldCubemap(const std::array<std::vector<std::uint8_t>, 6>& faces,
    UINT side, std::string* error) {
  worldCubemapTexture_.Reset();
  worldCubemapView_.Reset();
  worldCubemapSize_ = 0;
  const auto fail = [&](const char* message) {
    if (error) *error = message;
    return false;
  };
  // No faces and no side means "this map has no cube", which is not a failure:
  // it clears the previous map's cube and leaves the 2D path in charge, exactly
  // as uploadWorldLightmap treats an empty atlas.
  if (side == 0) {
    bool anyFace = false;
    for (const auto& face : faces) anyFace = anyFace || !face.empty();
    if (!anyFace) return true;
    return fail("a cubemap side of zero was given with face data");
  }
  if ((side & (side - 1)) != 0) {
    return fail("cubemap side is not a power of two, which a TextureCube sample needs");
  }
  const std::size_t expected = static_cast<std::size_t>(side) * side * 4u;
  for (int face = 0; face < 6; ++face) {
    if (faces[static_cast<std::size_t>(face)].size() != expected) {
      return fail("a cubemap face is missing or not side*side*4 bytes");
    }
  }
  return createCubeTexture(faces, side, worldCubemapTexture_, worldCubemapView_, error)
    ? (worldCubemapSize_ = side, true)
    : false;
}

// The six-array-slice cube, used by both the map's environment and the sky. One
// implementation because a cube built two ways is a cube that can be right for
// the env pass and wrong for the sky.
bool Renderer::createCubeTexture(const std::array<std::vector<std::uint8_t>, 6>& faces,
    UINT side, Microsoft::WRL::ComPtr<ID3D11Texture2D>& outTexture,
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>& outView, std::string* error) {
  const auto fail = [&](const char* message) {
    if (error) *error = message;
    return false;
  };
  D3D11_TEXTURE2D_DESC description{};
  description.Width = side;
  description.Height = side;
  description.MipLevels = 1;
  description.ArraySize = 6;
  description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  description.SampleDesc.Count = 1;
  description.Usage = D3D11_USAGE_IMMUTABLE;
  description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
  // The flag is what makes the six array slices a cube rather than six unrelated
  // 2D slices, and it is what makes the SRV below legal as a cube.
  description.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;
  D3D11_SUBRESOURCE_DATA initial[6]{};
  for (int face = 0; face < 6; ++face) {
    initial[face].pSysMem = faces[static_cast<std::size_t>(face)].data();
    initial[face].SysMemPitch = side * 4u;
    initial[face].SysMemSlicePitch = 0;
  }
  Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
  if (FAILED(device_->CreateTexture2D(&description, initial, texture.GetAddressOf()))) {
    return fail("CreateTexture2D refused the cube");
  }
  D3D11_SHADER_RESOURCE_VIEW_DESC view{};
  view.Format = description.Format;
  view.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
  view.TextureCube.MostDetailedMip = 0;
  view.TextureCube.MipLevels = 1;
  Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> resourceView;
  if (FAILED(device_->CreateShaderResourceView(texture.Get(), &view, resourceView.GetAddressOf()))) {
    return fail("CreateShaderResourceView refused the cube");
  }
  outTexture = std::move(texture);
  outView = std::move(resourceView);
  return true;
}

bool Renderer::uploadWorldSky(const std::array<WorldSkyFace, 6>& faces, std::string* error) {
  worldSkyTexture_.Reset();
  worldSkyView_.Reset();
  worldSkySide_ = 0;
  worldSkyResampled_ = false;
  worldSkyState_ = WorldSkyState::Unavailable;
  const auto fail = [&](const char* message) {
    worldSkyReason_ = message;
    if (error) *error = message;
    return false;
  };
  int decoded = 0;
  std::uint32_t largest = 0;
  bool uniform = true;
  for (const auto& face : faces) {
    if (face.width == 0 || face.height == 0
        || face.rgba.size() != static_cast<std::size_t>(face.width) * face.height * 4u) continue;
    ++decoded;
    largest = std::max(largest, std::max(face.width, face.height));
    if (face.width != face.height || face.width != largest) uniform = false;
  }
  if (decoded == 0) return fail("no sky side decoded");
  if (decoded < 6) {
    // Not padded with black, and not sampled as if it were whole: five sides of
    // sky still cover five sixths of the view, but the pass has to say which
    // five, so the caller decides whether to draw at all.
    return fail("fewer than six sides decoded, so no cube can be built");
  }
  // Smallest power of two that covers every side, capped because a sky is not
  // worth a hundred megabytes of texture.
  std::uint32_t side = 1;
  while (side < largest && side < 1024u) side <<= 1;
  if (side > 1024u) side = 1024u;
  const std::size_t expected = static_cast<std::size_t>(side) * side * 4u;
  std::array<std::vector<std::uint8_t>, 6> cube;
  bool resampled = !uniform || side != largest;
  for (std::size_t face = 0; face < 6; ++face) {
    const auto& source = faces[face];
    cube[face].assign(expected, 0u);
    for (std::uint32_t y = 0; y < side; ++y) {
      const std::uint32_t sourceY = source.height == 0 ? 0
        : std::min<std::uint32_t>(source.height - 1, y * source.height / side);
      for (std::uint32_t x = 0; x < side; ++x) {
        const std::uint32_t sourceX = source.width == 0 ? 0
          : std::min<std::uint32_t>(source.width - 1, x * source.width / side);
        const auto from = (static_cast<std::size_t>(sourceY) * source.width + sourceX) * 4u;
        const auto to = (static_cast<std::size_t>(y) * side + x) * 4u;
        std::memcpy(cube[face].data() + to, source.rgba.data() + from, 4u);
      }
    }
  }
  std::array<std::vector<std::uint8_t>, 6> raw{};
  for (std::size_t face = 0; face < 6; ++face) raw[face] = std::move(cube[face]);
  if (!createCubeTexture(raw, static_cast<UINT>(side), worldSkyTexture_, worldSkyView_, error)) {
    worldSkyReason_ = error && !error->empty() ? *error : "CreateTexture2D refused the sky cube";
    return false;
  }
  worldSkyReason_ = resampled
    ? "six sides resampled to a " + std::to_string(side) + "x" + std::to_string(side) + " cube"
    : "six sides at their own " + std::to_string(side) + "x" + std::to_string(side) + " size";
  worldSkyResampled_ = resampled;
  worldSkySide_ = side;
  worldSkyState_ = WorldSkyState::Active;
  return true;
}

bool Renderer::uploadWorldWaterTexture(const std::vector<std::uint8_t>& rgba, UINT width, UINT height) {
  worldWaterTexture_.reset();
  worldWaterTextureBound_ = false;
  worldWaterTextureSide_ = 0;
  // An empty buffer is a fallback, not a failure. The pass then draws the flat
  // shade and says so, which is a different row from "the texture failed to
  // upload" -- and a caller that has no water material at all should still be
  // able to draw the surface.
  if (rgba.empty() || width == 0 || height == 0) return true;
  const std::size_t expected = static_cast<std::size_t>(width) * height * 4u;
  if (rgba.size() < expected) {
    OutputDebugStringA("TF2 Demo Player: water texture is smaller than its dimensions claim.\n");
    return false;
  }
  std::string error;
  if (!worldWaterTexture_.create(device_.Get(), rgba, width, height, &error)) {
    OutputDebugStringA(("Water texture upload failed: " + error + "\n").c_str());
    return false;
  }
  worldWaterTextureBound_ = worldWaterTexture_.view() != nullptr;
  worldWaterTextureSide_ = width;
  return worldWaterTextureBound_;
}

bool Renderer::uploadWorldWaterBumpTexture(const std::vector<std::uint8_t>& rgba, UINT width, UINT height) {
  worldWaterBumpTexture_.reset();
  worldWaterBumpBound_ = false;
  // Same rule as the colour map: an empty buffer means "the material named no
  // normal map", which is a fallback the pass reports rather than a failure.
  if (rgba.empty() || width == 0 || height == 0) return true;
  const std::size_t expected = static_cast<std::size_t>(width) * height * 4u;
  if (rgba.size() < expected) {
    OutputDebugStringA("TF2 Demo Player: water normal map is smaller than its dimensions claim.\n");
    return false;
  }
  std::string error;
  if (!worldWaterBumpTexture_.create(device_.Get(), rgba, width, height, &error)) {
    OutputDebugStringA(("Water normal map upload failed: " + error + "\n").c_str());
    return false;
  }
  worldWaterBumpBound_ = worldWaterBumpTexture_.view() != nullptr;
  return worldWaterBumpBound_;
}

void Renderer::setWorldWaterReading(WorldWaterSurfaceState state,
    WorldWaterSurfaceState reflection, WorldWaterSurfaceState refraction,
    const std::string& reason) {
  worldWaterState_ = state;
  worldWaterReflectionState_ = reflection;
  worldWaterRefractionState_ = refraction;
  worldWaterReason_ = reason;
}

void Renderer::setWorldWaterWarp(double phase, float amplitude) {
  worldWaterPhase_ = phase;
  worldWaterAmplitude_ = amplitude;
}

WaterSurfaceSampleState Renderer::waterSurfaceSampleState() const {
  // Built fresh on each read rather than cached, so a mutation that deletes the
  // draw changes what this returns rather than what a stale copy holds. That is
  // the same discipline the sky and vis readings use.
  WaterSurfaceSampleState sample{};
  sample.state = worldWaterEnabled_ ? worldWaterState_ : WorldWaterSurfaceState::Unavailable;
  sample.waterTriangles = worldWaterVertexCount_ / 3u;
  sample.waterFaces = worldWaterVertexCount_ / 6u;
  // Whether the Draw call ran. Set inside the pass, never recomputed from the
  // same condition the pass tests: a reading composed from its own copy of the
  // guard reports a draw that was deleted.
  sample.waterDrawCalls = worldWaterDrewThisFrame_ ? 1u : 0u;
  sample.waterVisible = worldWaterDrewThisFrame_;
  sample.waterTextureBound = worldWaterTextureBound_;
  sample.waterWarpApplied = worldWaterDrewThisFrame_
    && worldWaterTextureBound_ && worldWaterAmplitude_ > 0.0f;
  sample.waterFallback = worldWaterDrewThisFrame_ && !worldWaterTextureBound_;
  sample.reflectionState = worldWaterReflectionState_;
  sample.refractionState = worldWaterRefractionState_;
  sample.reason = worldWaterEnabled_ ? worldWaterReason_
                                      : "water draw disabled by the caller";
  return sample;
}

void Renderer::setWorldMaterialParams(const WorldMaterialParams& params) {
  worldMaterialParams_ = params;
  if (!std::isfinite(worldMaterialParams_.selfIllumR)) worldMaterialParams_.selfIllumR = 0.0f;
  if (!std::isfinite(worldMaterialParams_.selfIllumG)) worldMaterialParams_.selfIllumG = 0.0f;
  if (!std::isfinite(worldMaterialParams_.selfIllumB)) worldMaterialParams_.selfIllumB = 0.0f;
  worldMaterialParams_.selfIllumR = std::clamp(worldMaterialParams_.selfIllumR, 0.0f, 4.0f);
  worldMaterialParams_.selfIllumG = std::clamp(worldMaterialParams_.selfIllumG, 0.0f, 4.0f);
  worldMaterialParams_.selfIllumB = std::clamp(worldMaterialParams_.selfIllumB, 0.0f, 4.0f);
}

WorldBlendState Renderer::worldBlendState() const {
  WorldBlendState state{};
  state.enabled = worldBlendEnabled_;
  state.terrainOnly = worldTerrainOnly_;
  state.texturesSeen = worldBlendTexturesSeen_;
  state.layersSeen = worldBlendLayersSeen_;
  state.accepted = worldBlendAccepted_;
  state.acceptedLayers = worldBlendAcceptedLayers_;
  state.tiles = worldBlendTiles_;
  state.modulateTiles = worldBlendModulateTiles_;
  state.malformedLayers = worldBlendMalformedLayers_;
  state.weightedVertices = worldBlendVertices_;
  state.modulatedVertices = worldBlendModulateVertices_;
  state.weightMin = worldBlendWeightMin_;
  state.weightMax = worldBlendWeightMax_;
  state.distinctWeights = worldBlendDistinctWeights_;
  return state;
}

void Renderer::bindSlot0(ID3D11ShaderResourceView* view, const char* pass) {
  lastSlot0View_ = view;
  lastSlot0Pass_ = pass ? pass : "unnamed";
  ++slot0Binds_;
  context_->PSSetShaderResources(0, 1, &view);
}

bool Renderer::isWorldAtlasView(const ID3D11ShaderResourceView* view) const {
  if (!view) return false;
  for (const auto& atlas : worldAtlasTextures_) {
    if (atlas.view() == view) return true;
  }
  return false;
}

bool Renderer::isWeaponMaterialView(const ID3D11ShaderResourceView* view) const {
  if (!view) return false;
  for (const auto& entry : viewModelMaterialTextures_) {
    if (entry.second.Get() == view) return true;
  }
  return false;
}

WorldShaderConstants Renderer::worldShaderConstants() const {
  return worldShaderConstants_;
}

WorldMaterialSampleState Renderer::worldMaterialSampleState() const {
  WorldMaterialSampleState state{};
  state.lightmapStatus = worldLightmapStatus_;
  state.lightmapIntensity = worldLightmapIntensity_;
  state.bumpTextureBound = worldBumpTexture_.view() != nullptr;
  state.envTextureBound = worldEnvTexture_.view() != nullptr;
  state.lightmapAtlasBound = worldLightmapTexture_.view() != nullptr;
  state.selfIllumEnabled = worldMaterialParams_.selfIllum;
  state.dynamicLightingMode = worldDynamicLightingEffective();
  state.specularMode = worldSpecularEffective();
  state.bumpMode = worldBumpEffective();
  // T27: the feature path's own state. `*Mode` requires both the atlas and a
  // vertex that points at it: an atlas with no vertex bit is a texture nothing
  // samples, and a bit with no atlas samples the filler.
  state.bumpFeature = worldBumpFeature_;
  state.bump2Feature = worldBump2Feature_;
  state.detailFeature = worldDetailFeature_;
  state.materialBumpMode = worldMaterialBumpEnabled_ && worldBumpFeature_.tiles > 0
    && worldBumpFeature_.vertices > 0;
  state.bump2Mode = worldMaterialBumpEnabled_ && worldBump2Enabled_
    && worldBump2Feature_.tiles > 0 && worldBump2Feature_.vertices > 0;
  state.detailMode = worldDetailEnabled_ && worldDetailFeature_.tiles > 0
    && worldDetailFeature_.vertices > 0;
  state.worldVertexCount = worldVertexCount_;
  state.convertedMin[0] = worldConvertedMin_[0]; state.convertedMin[1] = worldConvertedMin_[1]; state.convertedMin[2] = worldConvertedMin_[2];
  state.convertedMax[0] = worldConvertedMax_[0]; state.convertedMax[1] = worldConvertedMax_[1]; state.convertedMax[2] = worldConvertedMax_[2];
  state.lightmappedVertexCount = worldLightmappedVertexCount_;
  state.cubemapStatus = worldCubemapView_ ? WorldCubemapStatus::SixFace
    : (worldEnvTexture_.view() ? WorldCubemapStatus::Approximate2d
                              : WorldCubemapStatus::Unavailable);
  state.cubemapFaces = worldCubemapView_ ? 6u : 0u;
  state.cubemapSize = worldCubemapSize_;
  state.envMode = worldEnvMode();
  state.displacedTriangles = worldDisplacedTriangleCount_;
  state.waterTriangles = worldWaterTriangleCount_;
  // The skybox, as geometry rather than as files: `worldSkyVertexCount` is what
  // the pass draws, and `worldSkyDraws` is whether the Draw call ran this frame
  // -- not whether it would have, which is a condition the reading can restate
  // and a draw that was deleted cannot satisfy.
  state.worldSkyState = worldSkyEnabled_ ? worldSkyState_ : WorldSkyState::Unavailable;
  state.worldSkyVertexCount = worldSkyVertexCount_;
  state.worldSkyTriangles = worldSkyVertexCount_ / 3u;
  state.worldSkyFaces = worldSkyVertexCount_ / 6u;
  state.worldSkyDraws = (worldSkyEnabled_ && worldSkyDrewThisFrame_) ? 1u : 0u;
  state.worldSkySide = worldSkySide_;
  state.worldSkyResampled = worldSkyResampled_;
    state.worldSkyReason = worldSkyReason_;
  // The pvs clip: the state, whether the last draw took the indexed branch, and
  // how many triangles that branch withheld. `worldVisDrawnTriangles` is the
  // number the draw call saw, which is the number that has to move when the
  // clip is switched off or when the indexed branch is removed.
  state.worldVisClipState = worldVisClipEnabled_ ? worldVisClipState_ : WorldVisClipState::Disabled;
  state.worldVisClipApplied = worldVisClipEnabled_ && worldVisDrewIndexed_;
  state.worldVisDrawnTriangles = state.worldVisClipApplied
    ? worldVisDrawnTriangles_ : static_cast<std::size_t>(worldVertexCount_ / 3u);
  state.worldVisHiddenTriangles = state.worldVisClipApplied ? worldVisHiddenTriangles_ : 0u;
  state.worldVisTrianglesWithoutCluster = state.worldVisClipApplied ? worldVisTrianglesWithoutCluster_ : 0u;
  state.worldVisInputTriangles = worldVisInputTriangles_;
  state.worldVisCameraCluster = worldVisCameraCluster_;
  state.worldVisClipReason = worldVisClipEnabled_ ? worldVisClipReason_ : "vis clip disabled by the caller";
  // T19: the atlas plan, in full. `worldTileTable` is a copy on purpose -- the
  // sample is what the caller reads and the renderer must not hand out a
  // reference to state a later upload can replace under it.
  state.worldTilesTiled = worldTilesTiled_;
  state.worldTilesRefused = worldTilesRefused_;
  state.worldTilesRefusedMalformed = worldTilesRefusedMalformed_;
  state.worldTilesRefusedDuplicate = worldTilesRefusedDuplicate_;
  state.worldTilesRefusedBudget = worldTilesRefusedBudget_;
  state.worldAtlasBatches = worldAtlasBatches_;
  state.worldAtlasBlendBatches = worldAtlasBlendBatches_;
  state.worldAtlasLayerBytes = worldAtlasLayerBytes_;
  state.worldAtlasPlanReason = worldAtlasPlanReason_;
  state.worldTileTable = worldTileTable_;
  state.worldBatchDrawCalls = worldBatchDrawCalls_;
  state.worldBatchTexturedDrawCalls = worldBatchTexturedDrawCalls_;
  state.weaponDrawsWithWorldAtlas = weaponDrawsWithWorldAtlas_;
  state.worldDrawsWithWeaponTexture = worldDrawsWithWeaponTexture_;
  state.slot0Binds = slot0Binds_;
  // T10 diagnostic: what the last world rasterisation actually ran with, so a
  // no-cull capture carries its own confession.
  state.worldCullBackfaces = worldBackfaceCull_;
  state.worldDebugView = worldDebugView_;
  return state;
}

} // namespace tf2::native
