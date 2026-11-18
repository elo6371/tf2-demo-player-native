// Frame: the per-frame pass sequence (draw), the HUD pass and the HUD primitive lists.
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
#include <cstdlib>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace tf2::native {


bool Renderer::draw(float clearRed, float clearGreen, float clearBlue) {
  paneCountThisDraw_ = 1;
  paneIndexThisDraw_ = 0;
  syncPrimaryPaneCamera();
  return drawPaneInternal(clearRed, clearGreen, clearBlue, 0, 1);
}

// U4 step 2b: two panes, one surface, one clock. The panes are composed by
// running the same draw body twice with disjoint viewports; the demo playback
// clock is shared by design (see docs/U4-PANE-OWNERSHIP-2026-11-16.md), so this
// is a viewport/camera split and not a second simulation.
bool Renderer::drawSplit(float clearRed, float clearGreen, float clearBlue) {
  if (!context_ || !target_ || !swapChain_) return false;
  paneCountThisDraw_ = kPaneMaxPanes;
  syncPrimaryPaneCamera();
  paneDepthClears_ = 0;
  bool drawn = true;
  DirectX::XMFLOAT4X4 primaryView{};
  float primaryEye[3] = {};
  float primaryForward[3] = {};
  float primaryUp[3] = {};
  float primaryRight[3] = {};
  for (std::uint32_t pane = 0; pane < kPaneMaxPanes; ++pane) {
    paneIndexThisDraw_ = pane;
    drawn = drawPaneInternal(clearRed, clearGreen, clearBlue, pane, kPaneMaxPanes) && drawn;
    if (pane == 0) {
      primaryView = frameView_;
      for (int i = 0; i < 3; ++i) {
        primaryEye[i] = frameEye_[i];
        primaryForward[i] = frameForward_[i];
        primaryUp[i] = frameUp_[i];
        primaryRight[i] = frameRight_[i];
      }
    }
  }
  // The camera the rest of the program reads after a frame is pane 0's: the
  // single-pane camera, not whichever pane was drawn last.
  frameView_ = primaryView;
  for (int i = 0; i < 3; ++i) {
    frameEye_[i] = primaryEye[i];
    frameForward_[i] = primaryForward[i];
    frameUp_[i] = primaryUp[i];
    frameRight_[i] = primaryRight[i];
  }
  return drawn;
}

// Pane 0's camera is the orbit/observer state every existing caller drives.
// Copying it once per frame keeps one source of truth: pane 1 is written only by
// setPaneCameraOrbit, so camera input cannot reach it by accident.
void Renderer::syncPrimaryPaneCamera() {
  PaneCameraState& camera = paneCameras_[0];
  camera = PaneCameraState{};
  camera.demoView = observerDemoView_;
  camera.demoEye[0] = observerDemoEyeX_;
  camera.demoEye[1] = observerDemoEyeY_;
  camera.demoEye[2] = observerDemoEyeZ_;
  camera.demoPitchDeg = observerDemoPitchDeg_;
  camera.demoYawDeg = observerDemoYawDeg_;
  camera.focusValid = observerFocusValid_;
  camera.focus[0] = observerFocusX_;
  camera.focus[1] = observerFocusY_;
  camera.focus[2] = observerFocusZ_;
  camera.orbitYaw = cameraYaw_;
  camera.orbitPitch = cameraPitch_;
  camera.orbitDistance = cameraDistance_;
}

void Renderer::setPaneCameraOrbit(std::uint32_t pane, float yaw, float pitch, float distance) {
  if (pane == 0u || pane >= kPaneMaxPanes) return;  // pane 0 belongs to the orbit camera
  PaneCameraState& camera = paneCameras_[pane];
  camera = PaneCameraState{};
  camera.orbitYaw = yaw;
  camera.orbitPitch = pitch;
  camera.orbitDistance = distance;
}

void Renderer::setPaneCameraFocusWorld(std::uint32_t pane, float x, float y, float z) {
  if (pane == 0u || pane >= kPaneMaxPanes) return;  // pane 0 belongs to the orbit camera
  if (!worldBoundsValid_ || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)
      || !std::isfinite(worldCenterX_) || !std::isfinite(worldCenterY_)
      || !std::isfinite(worldMinZ_) || !std::isfinite(worldSpanZ_)
      || !std::isfinite(worldHorizontalScale_) || worldSpanZ_ <= 0.0f
      || worldHorizontalScale_ <= 0.0f) return;
  // The same three lines as setObserverFocusWorld, deliberately copied instead of
  // factored out of it: the pane-0 conversion every existing capture depends on
  // must not move while a second pane is added.
  const float normalizedX = (x - worldCenterX_) * worldHorizontalScale_;
  const float normalizedY = (y - worldCenterY_) * worldHorizontalScale_;
  const float normalizedZ = (z - worldMinZ_) * worldHorizontalScale_ + 0.1f;
  if (!std::isfinite(normalizedX) || !std::isfinite(normalizedY) || !std::isfinite(normalizedZ)) return;
  PaneCameraState& camera = paneCameras_[pane];
  camera.focus[0] = std::clamp(normalizedX, -1.0e6f, 1.0e6f);
  camera.focus[1] = std::clamp(normalizedY, -1.0e6f, 1.0e6f);
  camera.focus[2] = std::clamp(normalizedZ, -1.0e6f, 1.0e6f);
  camera.focusValid = true;
  // Third person is the orbit around this focus, never a stale demo eye: a pane
  // that kept its eye would ignore the focus it was just handed.
  camera.demoView = false;
}

void Renderer::setPaneCameraDemoViewWorld(std::uint32_t pane, float x, float y, float z,
    float pitchDegrees, float yawDegrees) {
  if (pane == 0u || pane >= kPaneMaxPanes) return;
  if (!std::isfinite(pitchDegrees) || !std::isfinite(yawDegrees)) return;
  if (!worldBoundsValid_ || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)
      || !std::isfinite(worldCenterX_) || !std::isfinite(worldCenterY_)
      || !std::isfinite(worldMinZ_) || !std::isfinite(worldSpanZ_)
      || !std::isfinite(worldHorizontalScale_) || worldSpanZ_ <= 0.0f
      || worldHorizontalScale_ <= 0.0f) return;
  const float normalizedX = (x - worldCenterX_) * worldHorizontalScale_;
  const float normalizedY = (y - worldCenterY_) * worldHorizontalScale_;
  const float normalizedZ = (z - worldMinZ_) * worldHorizontalScale_ + 0.1f;
  if (!std::isfinite(normalizedX) || !std::isfinite(normalizedY) || !std::isfinite(normalizedZ)) return;
  PaneCameraState& camera = paneCameras_[pane];
  camera.demoView = true;
  camera.demoEye[0] = std::clamp(normalizedX, -1.0e6f, 1.0e6f);
  camera.demoEye[1] = std::clamp(normalizedY, -1.0e6f, 1.0e6f);
  camera.demoEye[2] = std::clamp(normalizedZ, -1.0e6f, 1.0e6f);
  camera.demoPitchDeg = pitchDegrees;
  camera.demoYawDeg = yawDegrees;
  // Mirrors setObserverDemoViewWorld: the eye is also the focus, so leaving first
  // person for third has a target rather than jumping back to the origin.
  camera.focusValid = true;
  camera.focus[0] = camera.demoEye[0];
  camera.focus[1] = camera.demoEye[1];
  camera.focus[2] = camera.demoEye[2];
}

void Renderer::clearPaneCameraDemoView(std::uint32_t pane) {
  if (pane == 0u || pane >= kPaneMaxPanes) return;
  paneCameras_[pane].demoView = false;
}

void Renderer::orbitPaneCamera(std::uint32_t pane, float deltaX, float deltaY) {
  if (pane == 0u || pane >= kPaneMaxPanes) return;
  if (!std::isfinite(deltaX) || !std::isfinite(deltaY)) return;
  PaneCameraState& camera = paneCameras_[pane];
  constexpr float kPi = 3.14159265358979323846f;
  const float yaw = std::remainder(camera.orbitYaw + deltaX * 0.008f, kPi * 2.0f);
  const float pitch = camera.orbitPitch + deltaY * 0.008f;
  if (std::isfinite(yaw)) camera.orbitYaw = yaw;
  if (std::isfinite(pitch)) camera.orbitPitch = std::clamp(pitch, 0.25f, 1.45f);
  // Right-dragging is the user taking the camera: it leaves the demo eye, for the
  // same reason orbitCamera clears pane 0's.
  camera.demoView = false;
}

void Renderer::zoomPaneCamera(std::uint32_t pane, float delta) {
  if (pane == 0u || pane >= kPaneMaxPanes) return;
  if (!std::isfinite(delta)) return;
  PaneCameraState& camera = paneCameras_[pane];
  const float distance = camera.orbitDistance * std::exp(-delta * 0.08f);
  if (std::isfinite(distance)) camera.orbitDistance = std::clamp(distance, 0.8f, 12.0f);
}

bool Renderer::drawPaneInternal(float clearRed, float clearGreen, float clearBlue,
                                std::uint32_t paneIndex, std::uint32_t paneCount) {
  if (!context_ || !target_ || !swapChain_) return false;
  // The back-buffer rectangle this pane owns: the whole surface in a single-pane
  // frame, half of it in a split (the remainder goes to the right pane, so an odd
  // width loses no column).
  std::uint32_t paneX = 0;
  std::uint32_t paneWidth = width_;
  std::uint32_t paneHeight = height_;
  if (paneCount > 1u) {
    const std::uint32_t leftWidth = width_ / 2u;
    paneWidth = paneIndex == 0u ? leftWidth : width_ - leftWidth;
    paneX = paneIndex == 0u ? 0u : leftWidth;
  }
  const float colour[4] = { clearRed, clearGreen, clearBlue, 1.0f };
  context_->OMSetRenderTargets(1, target_.GetAddressOf(), depthStencilView_.Get());
  // One surface serves both panes through disjoint viewports, so the frame is
  // cleared once, by the pane that starts it. Clearing per pane would erase the
  // half that was already drawn.
  if (paneIndex == 0u) {
    context_->ClearRenderTargetView(target_.Get(), colour);
    if (depthStencilView_) {
      context_->ClearDepthStencilView(depthStencilView_.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
      ++paneDepthClears_;
    }
  } else if (depthStencilView_) {
    // The colour clear above stays pane 0's job -- clearing it again would erase
    // the half that is already drawn. Depth is the opposite: the two panes are
    // disjoint viewports of one surface, so a depth value written by pane 0 says
    // nothing about what pane 1 should occlude. Adding this clear did not change
    // the captured split frame with a weapon (the ViewModel pass clears the same
    // surface first), so it is a correctness fix rather than a visual one: the
    // split frame that draws no weapon used to depth-test pane 1 against pane 0's
    // map. A full-surface clear is safe here because pane 0 has finished drawing
    // and nothing after the panes tests depth against it.
    context_->ClearDepthStencilView(depthStencilView_.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
    ++paneDepthClears_;
  }
  context_->OMSetDepthStencilState(depthStencilState_.Get(), 0);
  // R1: the cleared baseline. Every plane below is measured against it -- if
  // anything but 1.0 shows up here, the dump itself is the suspect.
  noteDepthState("frame-start:write-all");
  capturePassDepth("00-cleared");
  context_->RSSetState(worldBackfaceCull_ ? rasterizerState_.Get()
                                          : worldNoCullRasterizerState_.Get());
  D3D11_VIEWPORT viewport{};
  viewport.TopLeftX = static_cast<float>(paneX);
  viewport.Width = static_cast<float>(paneWidth);
  viewport.Height = static_cast<float>(paneHeight);
  viewport.MinDepth = 0.0f;
  viewport.MaxDepth = 1.0f;
  context_->RSSetViewports(1, &viewport);
  // U4 step 1: record the bindings this draw just made against the
  // single-surface pane contract (pane_boundary.h). Taken here, at the point the
  // bindings are made, so the reading cannot describe a different frame; the
  // camera and HUD fields are filled where those decisions are made below.
  if (paneIndex == 0u) {
    RECT paneClientRect{};
    if (window_) GetClientRect(window_, &paneClientRect);
    paneBoundary_ = PaneBoundaryReadings{};
    paneBoundary_.drawOrdinal = ++paneDrawOrdinal_;
    paneBoundary_.renderTargets = target_ ? 1u : 0u;
    paneBoundary_.depthViews = depthStencilView_ ? 1u : 0u;
    paneBoundary_.viewports = paneCount;
    paneBoundary_.viewportWidth = paneWidth;
    paneBoundary_.viewportHeight = paneHeight;
    paneBoundary_.paneCount = paneCount;
    paneBoundary_.clientWidth = window_
      ? static_cast<std::uint32_t>(paneClientRect.right - paneClientRect.left) : 0u;
    paneBoundary_.clientHeight = window_
      ? static_cast<std::uint32_t>(paneClientRect.bottom - paneClientRect.top) : 0u;
  }
  context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  // Per-frame counters are reset by the pane that starts the frame; later panes
  // keep counting into the same frame totals, which is what makes the budget
  // trace's pass count a frame number rather than a pane number.
  if (paneIndex == 0u) {
    framePassOrdinal_ = 0;
    viewModelWorldPassOrdinal_ = 0;
  }
  // T51: the HUD readings are per-frame, reset here for the reason the ordinal
  // above is: a drawHudPass that did not run this frame must read as "nothing was
  // drawn", not as "the last frame that drew something". Without this, turning the
  // HUD off would leave drawn=<the previous count> on the trace and a gate would
  // read a HUD that no longer exists. U4 step 2b: once per frame, at pane 0, for
  // the same reason -- a split frame's HUD reading describes the frame, not pane 1.
  if (paneIndex == 0u) {
    hudDrawnRects_ = 0;
    hudDrawnPrimitives_ = 0;
    hudTextQuads_ = 0;
    hudGlyphCount_ = 0;
    hudUnmappedGlyphs_ = 0;
    hudImageQuads_ = 0;
    hudPassOrdinal_ = 0;
    hudScissorApplied_ = false;
    hudLastRegion_ = HudPixelRectLast{};
    // Both readings below are written by the draw calls themselves and read back
    // by worldMaterialSampleState(). They are reset here, once per frame, so that
    // "the sky drew" and "the world drew indexed" are statements about this frame
    // and cannot survive on a frame where the corresponding call did not run.
    worldSkyDrewThisFrame_ = false;
    worldSkyFarPlane_ = false;
    worldSkyDedicatedShader_ = false;
    worldVisDrewIndexed_ = false;
    // N05 step 3: the clip capture describes one draw, so it is cleared once per
    // frame for the same reason the two readings above are: a frame where the
    // capture did not run must read as "not attempted", never as the previous
    // frame's vertices.
    worldClipCaptureValid_ = false;
    worldClipCaptureAttempted_ = false;
  }
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
    // T101: environment fog. fogParams = (start, end, maxDensity, enabled) in
    // MAP units; fogScale converts a box-space delta to map-space so the shader
    // measures distance in the units the map authored. See WorldFogState.
    DirectX::XMFLOAT4 fogParams;
    DirectX::XMFLOAT4 fogColor;
    DirectX::XMFLOAT4 fogScale;
    // N21: the map's lights, appended AFTER every field above so the offsets
    // the existing shader arithmetic uses do not move. `sunDirectionAndMode.w`
    // is the switch: at 0 the pixel shader never reads past this vector, and
    // everything below is uploaded as zeros, so a run without --world-lights
    // cannot differ by an uninitialised term.
    DirectX::XMFLOAT4 sunDirectionAndMode;      // xyz surface->light, w enabled
    DirectX::XMFLOAT4 sunColorAndIntensity;     // rgb, w brightness/1000
    DirectX::XMFLOAT4 ambientColorAndIntensity; // rgb, w brightness/1000
    DirectX::XMFLOAT4 lightCountAndParams;      // x = lights uploaded this pane
    // 16 is the per-pane cap the renderer applies (see the fill below): a TF2
    // map declares ~200 lights and a fragment shader loop is the wrong place to
    // find out which of them matter, so the nearest sixteen are chosen on the
    // CPU and the shader loop has a fixed trip count it can unroll.
    DirectX::XMFLOAT4 lightPositionRadius[16];    // xyz box position, w radius
    DirectX::XMFLOAT4 lightColorIntensity[16];    // rgb colour*intensity, w cosOuter
    DirectX::XMFLOAT4 lightDirectionAndCone[16];  // xyz spot axis, w cosInner
    DirectX::XMFLOAT4 lightConeParams[16];        // x exponent, y isSpot
  };
  // T101: the constant buffer's ByteWidth (native_renderer.cpp) must equal this
  // size exactly -- UpdateSubresource copies ByteWidth bytes and says nothing
  // about the rest. It was 160 against this 144-byte struct, so every frame read
  // 16 bytes past the end. The comment that asked for the two to match was
  // already beside that number and did not hold; this assertion does.
  // 192 before N21; the light block adds four float4 headers (64) and four
  // 16-float4 arrays (1024), so 1280 = 192 + 64 + 1024.
  static_assert(sizeof(WorldConstantsData) == 1280,
    "WorldConstantsData must be exactly the world constant buffer's ByteWidth");
  struct ModelMaterialProxyConstantsData {
    DirectX::XMFLOAT4 texture2Scroll;
    DirectX::XMFLOAT4 color;
    DirectX::XMFLOAT4 flags;
  };
  static_assert(sizeof(ModelMaterialProxyConstantsData) == 48,
    "ModelMaterialProxy constants must match their D3D11 buffer");
  WorldConstantsData frameConstants{};
  bool frameConstantsValid = false;
  // W-1 perf (A/B): the sky's DRAW is deferred to the end of the opaque world
  // pass. The constants upload below stays where it was; only the draw moves.
  // Same 12 triangles, same rasterizer and depth states -- the difference is
  // that the depth buffer is now full of world pixels, so the part of the
  // screen the world covers rejects the sky in the depth test instead of the
  // sky shading that area and the world overdrawing it.
  //
  // Moving the draw alone was not enough: with the world's pixel shader bound
  // the sky still shaded every frame pixel, because that shader contains
  // `discard` and a shader that can discard forfeits early depth rejection
  // (measured in the A/B: 38.8 fps sky first vs 37.3 fps sky last). The draw
  // therefore binds skyPixelShader_, the same branch with no discard and no
  // depth output, which is the state early-z needs. That is also why this is a
  // separate shader rather than a mode switch in the world one.
  bool skyDrawPending = false;
  auto drawWorldSkyBox = [this]() {
    const UINT skyStride = sizeof(WorldVertex), skyOffset = 0;
    auto* skyBuffer = worldSkyVertexBuffer_.Get();
    auto* skyCube = worldSkyView_.Get();
    auto* skyConstantsBuffer = worldSkyConstants_.Get();
    context_->RSSetState(worldSkyRasterizerState_.Get());
    context_->OMSetDepthStencilState(worldSkyDepthStencilState_.Get(), 0);
    noteDepthState("sky:write-off");
    context_->IASetInputLayout(worldInputLayout_.Get());
    context_->IASetVertexBuffers(0, 1, &skyBuffer, &skyStride, &skyOffset);
    context_->VSSetShader(worldVertexShader_.Get(), nullptr, 0);
    context_->VSSetConstantBuffers(0, 1, &skyConstantsBuffer);
    // W-1 perf: the discard-free shader, not the world one -- see the note on
    // skyDrawPending above. The vertex shader and input layout stay the world's,
    // because the box is built from the same vertex format.
    context_->PSSetShader(skyPixelShader_.Get(), nullptr, 0);
    context_->PSSetConstantBuffers(0, 1, &skyConstantsBuffer);
    context_->PSSetShaderResources(5, 1, &skyCube);
    context_->PSSetSamplers(0, 1, sampler_.GetAddressOf());
    context_->Draw(worldSkyVertexCount_, 0);
    worldSkyDrewThisFrame_ = true;
    // W-1 perf: two facts a gate can assert from the trace, recorded by the
    // draw itself -- the box is pinned to the far plane (clip.z == clip.w) and
    // the discard-free sky shader is the one bound. If either is lost the pass
    // is back to a small box shaded by the world shader, which the picture can
    // hide (it is depth-rejected either way) but the trace cannot.
    worldSkyFarPlane_ = true;
    worldSkyDedicatedShader_ = true;
    capturePassDepth("10-sky");
    // The next world draw must not inherit the sky's cube or its cull state.
    // T10: "the world's cull state" is the one the caller configured -- the
    // no-cull diagnostic must survive the sky's restore, or the capture's
    // world triangles would rasterise culled while the reading says they did
    // not.
    context_->RSSetState(worldBackfaceCull_ ? rasterizerState_.Get()
                                            : worldNoCullRasterizerState_.Get());
    context_->OMSetDepthStencilState(depthStencilState_.Get(), 0);
    noteDepthState("world:write-all");
    // The world's own slot 5 is its reflection cubemap, and its vertex and
    // constant-buffer bindings are the ones the batch loop expects to still be
    // in place: the sky borrowed all three, so all three go straight back.
    ID3D11ShaderResourceView* cubeView = worldCubemapView_.Get();
    context_->PSSetShaderResources(5, 1, &cubeView);
    auto* worldBuffer = worldVertexBuffer_.Get();
    auto* worldConstantsBuffer = worldConstants_.Get();
    const UINT worldStride = sizeof(WorldVertex), worldOffset = 0;
    context_->IASetVertexBuffers(0, 1, &worldBuffer, &worldStride, &worldOffset);
    context_->VSSetConstantBuffers(0, 1, &worldConstantsBuffer);
    context_->PSSetConstantBuffers(0, 1, &worldConstantsBuffer);
    // The sky borrowed the pixel shader too: every later world draw in this
    // frame expects the map shader bound, so it goes back here and not at the
    // next bind site.
    context_->PSSetShader(worldPixelShader_.Get(), nullptr, 0);
  };
  if (worldConstants_) {
    using namespace DirectX;
    // U4 step 2b: this pane's camera, not the frame's. Pane 0's state was copied
    // from the orbit/observer members at frame start; pane 1's is whatever
    // setPaneCameraOrbit wrote, and camera input never touches it.
    const PaneCameraState& paneCamera = paneCameras_[paneIndex];
    XMVECTOR target = XMVectorSet(
      paneCamera.focusValid ? paneCamera.focus[0] : 0.0f,
      paneCamera.focusValid ? paneCamera.focus[1] : 0.0f,
      paneCamera.focusValid ? paneCamera.focus[2] : 0.5f, 1.0f);
    XMVECTOR eye;
    XMVECTOR up = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
    if (paneCamera.demoView) {
      float forward[3] = {};
      demoViewForward(paneCamera.demoPitchDeg, paneCamera.demoYawDeg, forward);
      eye = XMVectorSet(paneCamera.demoEye[0], paneCamera.demoEye[1], paneCamera.demoEye[2], 1.0f);
      // The debug eye is stored in the same normalized unit box as the world
      // vertex buffer. Its angles are authored in map units, so the direction
      // must pass through the same map-to-buffer scale as the eye; adding a
      // raw one-unit map vector here makes the camera look almost parallel to
      // the map and turns otherwise ordinary faces into giant screen shards.
      const float normalizedForwardX = forward[0] * worldHorizontalScale_;
      const float normalizedForwardY = forward[1] * worldHorizontalScale_;
      const float normalizedForwardZ = forward[2] * worldHorizontalScale_;
      target = XMVectorSet(paneCamera.demoEye[0] + normalizedForwardX,
        paneCamera.demoEye[1] + normalizedForwardY,
        paneCamera.demoEye[2] + normalizedForwardZ, 1.0f);
      if (std::fabs(forward[2]) > 0.95f) up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    } else {
      const float horizontal = std::cos(paneCamera.orbitPitch) * paneCamera.orbitDistance;
      eye = XMVectorSet(
        XMVectorGetX(target) + std::cos(paneCamera.orbitYaw) * horizontal,
        XMVectorGetY(target) + std::sin(paneCamera.orbitYaw) * horizontal,
        XMVectorGetZ(target) + std::sin(paneCamera.orbitPitch) * paneCamera.orbitDistance, 1.0f);
    }
    // U4 step 1/2b: name the camera the view matrix below is built from. This is
    // THIS pane's camera, not the one a caller requested; a pane reading
    // "demo-view" here while its own request said otherwise is the defect the
    // pane trace exists to surface.
    const PaneCameraSource paneCameraSource = paneCamera.demoView
      ? PaneCameraSource::DemoView : PaneCameraSource::Orbit;
    const XMMATRIX view = XMMatrixLookAtLH(eye, target, up);
    // Publish the frame camera for the ViewModel pass. The pass places the
    // weapon in this camera's space and reuses this view matrix, so the two
    // passes cannot drift apart.
    XMStoreFloat4x4(&frameView_, view);
    // U4 step 2b: record THIS pane in the pane-aware readings. The fingerprint is
    // the camera input set the view above was built from and the view hash is the
    // matrix that was actually stored, so the predicate can tell "two panes
    // reported one matrix" from "two panes asked for one camera". The slot is the
    // state array index: two panes quoting one slot is the shared-camera defect,
    // whatever their matrices say.
    paneBoundary_.panes[paneIndex].x = paneX;
    paneBoundary_.panes[paneIndex].y = 0u;
    paneBoundary_.panes[paneIndex].width = paneWidth;
    paneBoundary_.panes[paneIndex].height = paneHeight;
    paneBoundary_.panes[paneIndex].camera = paneCameraSource;
    paneBoundary_.panes[paneIndex].viewHash = paneHashFloats(&frameView_.m[0][0], 16u);
    paneBoundary_.panes[paneIndex].cameraFingerprint = paneCameraFingerprint(paneCamera);
    paneBoundary_.panes[paneIndex].cameraSlot = paneIndex;
    if (paneIndex == 0u) paneBoundary_.camera = paneCameraSource;
    const XMVECTOR forwardVector = XMVector3Normalize(XMVectorSubtract(target, eye));
    // Left-handed basis: right = up x forward.
    const XMVECTOR rightVector = XMVector3Normalize(XMVector3Cross(up, forwardVector));
    frameEye_[0] = XMVectorGetX(eye); frameEye_[1] = XMVectorGetY(eye); frameEye_[2] = XMVectorGetZ(eye);
    frameForward_[0] = XMVectorGetX(forwardVector); frameForward_[1] = XMVectorGetY(forwardVector); frameForward_[2] = XMVectorGetZ(forwardVector);
    frameUp_[0] = XMVectorGetX(up); frameUp_[1] = XMVectorGetY(up); frameUp_[2] = XMVectorGetZ(up);
    frameRight_[0] = XMVectorGetX(rightVector); frameRight_[1] = XMVectorGetY(rightVector); frameRight_[2] = XMVectorGetZ(rightVector);
    // U4 step 2b: the pane's aspect, not the window's -- a half-width viewport
    // projected with the window's aspect would stretch every pane of a split.
    const float aspect = paneHeight == 0u ? 1.0f
      : static_cast<float>(paneWidth) / static_cast<float>(paneHeight);
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
    const float worldVerticalFov = sourceHorizontalFovToVertical(settings_.worldFov);
    // N17 item 5: state the projection this frame actually used. 5a599d1e fixed
    // the world FOV conversion; this line is what makes the fix readable in a
    // capture log (horizontal 90 -> vertical 73.7398 through the 4:3 Source
    // reference) instead of something a reader has to verify in the source.
    if (!capturePath_.empty()) {
      // C2: name the origin as well as the value -- "horizontal 90" from a gate's
      // settings file and the same 90 from --world-fov are different claims about
      // what was asked for.
      std::printf("capture: fov source=%s horizontal=%.4f vertical=%.4f"
        " rule=sourceHorizontalFovToVertical aspect=%.6f near=%.4f far=%.4f\n",
        worldFovFromCli_ ? "cli.worldFov" : "settings.worldFov",
        settings_.worldFov, worldVerticalFov, aspect, worldNearPlane, worldFarPlane);
    }
    XMMATRIX projection = XMMatrixPerspectiveFovLH(XMConvertToRadians(worldVerticalFov), aspect, worldNearPlane, worldFarPlane);
    // The view basis puts game LEFT on screen right (see the proof in
    // renderer_internal.h's horizontalFlipCorrectionEnabled); negating clip-space
    // X is what puts the world back the way the game shows it. The matching cull
    // flip lives in native_renderer.cpp's rasterizer states -- the two are one
    // change and must never be applied separately.
    if (horizontalFlipCorrectionEnabled()) projection.r[0] = XMVectorNegate(projection.r[0]);
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
    // T101: the fog, converted into the spaces the shader needs in one place
    // so the scales cannot drift from the vertex convert (which uses the same
    // two members, native_renderer_world.cpp:93-101). When the pass was uploaded
    // unfogged every value here is a no-op: enabled 0 keeps the shader branch
    // closed, and scale 1.0 is finite even if something read a distance anyway.
    // A disabled pass therefore uploads (0,0,0,0)/(1,1,1,0) and not garbage.
    const float fogBoxToMapXZ = (std::isfinite(worldHorizontalScale_) && worldHorizontalScale_ > 0.0f)
      ? 1.0f / worldHorizontalScale_ : 1.0f;
    const float fogBoxToMapZ = (std::isfinite(worldHorizontalScale_) && worldHorizontalScale_ > 0.0f)
      ? 1.0f / worldHorizontalScale_ : 1.0f;
    constants.fogParams = worldFog_.enabled
      ? XMFLOAT4(worldFog_.start, worldFog_.end, worldFog_.maxDensity, 1.0f)
      : XMFLOAT4(0.0f, 0.0f, 0.0f, 0.0f);
    constants.fogColor = worldFog_.enabled
      ? XMFLOAT4(worldFog_.color[0], worldFog_.color[1], worldFog_.color[2], 1.0f)
      : XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    constants.fogScale = XMFLOAT4(fogBoxToMapXZ, fogBoxToMapXZ, fogBoxToMapZ, 0.0f);
    // ---- N21: the map's lights -------------------------------------------
    // Filled HERE, per pane, because the selection is per pane: the cap is
    // sixteen and the sixteen are the ones nearest THIS pane's eye, so the two
    // halves of a split screen do not light each other's rooms.
    //
    // The conversion is the one uploadWorldGeometry applied
    // (native_renderer_world.cpp:93-101): x/y scaled by worldHorizontalScale_
    // about the map's centre, z by the same scale from worldMinZ_ + 0.1. It is
    // a uniform scale with no rotation, so a direction survives it unchanged
    // and a radius scales with it -- which is why only position and radius are
    // converted below.
    //
    // Off (the default) uploads zeros for the whole block and leaves
    // sunDirectionAndMode.w at 0: the pixel shader's switch is that w, so a
    // map-light block that was never filled cannot change a pixel.
    constants.sunDirectionAndMode = worldLights_.enabled
      ? XMFLOAT4(worldLights_.sunDirection[0], worldLights_.sunDirection[1],
        worldLights_.sunDirection[2], 1.0f)
      : XMFLOAT4(0.0f, 0.0f, 1.0f, 0.0f);
    constants.sunColorAndIntensity = XMFLOAT4(worldLights_.sunColor[0],
      worldLights_.sunColor[1], worldLights_.sunColor[2], worldLights_.sunIntensity);
    constants.ambientColorAndIntensity = XMFLOAT4(worldLights_.ambientColor[0],
      worldLights_.ambientColor[1], worldLights_.ambientColor[2], worldLights_.ambientIntensity);
    constants.lightCountAndParams = XMFLOAT4(0.0f, 0.0f, 0.0f, 0.0f);
    for (std::size_t slot = 0; slot < 16u; ++slot) {
      constants.lightPositionRadius[slot] = XMFLOAT4(0.0f, 0.0f, 0.0f, 0.0f);
      constants.lightColorIntensity[slot] = XMFLOAT4(0.0f, 0.0f, 0.0f, 0.0f);
      constants.lightDirectionAndCone[slot] = XMFLOAT4(0.0f, 0.0f, 0.0f, 0.0f);
      constants.lightConeParams[slot] = XMFLOAT4(0.0f, 0.0f, 0.0f, 0.0f);
    }
    std::size_t uploadedLights = 0;
    if (worldLights_.enabled && !worldLights_.lights.empty()) {
      const float lightScale = (std::isfinite(worldHorizontalScale_) && worldHorizontalScale_ > 0.0f)
        ? worldHorizontalScale_ : 1.0f;
      const float eyeX = XMVectorGetX(eye);
      const float eyeY = XMVectorGetY(eye);
      const float eyeZ = XMVectorGetZ(eye);
      struct LightCandidate {
        float distanceSq = 0.0f;
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        std::size_t index = 0;
      };
      // A fixed-size max-heap: sixteen slots, no allocation in the frame, and
      // the farthest kept light sits at the top, so a nearer one replaces it in
      // one comparison. order_of_key style bookkeeping is not needed -- the
      // shader's loop bound is lightCountAndParams.x and the order within the
      // sixteen is irrelevant to the sum.
      std::array<LightCandidate, 16> nearest{};
      std::size_t nearestCount = 0;
      const auto fartherFirst = [](const LightCandidate& a, const LightCandidate& b) {
        return a.distanceSq < b.distanceSq;
      };
      for (std::size_t index = 0; index < worldLights_.lights.size(); ++index) {
        const auto& light = worldLights_.lights[index];
        LightCandidate candidate;
        candidate.x = (light.position[0] - worldCenterX_) * lightScale;
        candidate.y = (light.position[1] - worldCenterY_) * lightScale;
        candidate.z = (light.position[2] - worldMinZ_) * lightScale + 0.1f;
        const float dx = candidate.x - eyeX;
        const float dy = candidate.y - eyeY;
        const float dz = candidate.z - eyeZ;
        candidate.distanceSq = dx * dx + dy * dy + dz * dz;
        candidate.index = index;
        if (nearestCount < nearest.size()) {
          nearest[nearestCount++] = candidate;
          std::push_heap(nearest.begin(), nearest.begin() + nearestCount, fartherFirst);
        } else if (candidate.distanceSq < nearest.front().distanceSq) {
          std::pop_heap(nearest.begin(), nearest.end(), fartherFirst);
          nearest.back() = candidate;
          std::push_heap(nearest.begin(), nearest.end(), fartherFirst);
        }
      }
      for (std::size_t slot = 0; slot < nearestCount; ++slot) {
        const auto& candidate = nearest[slot];
        const auto& light = worldLights_.lights[candidate.index];
        constants.lightPositionRadius[slot] = XMFLOAT4(candidate.x, candidate.y,
          candidate.z, light.radius * lightScale);
        // Colour premultiplied by intensity so the shader's loop is one
        // multiply; w carries cosOuter, which the cone's smoothstep needs and
        // which the colour slot is otherwise not using.
        constants.lightColorIntensity[slot] = XMFLOAT4(
          light.color[0] * light.intensity, light.color[1] * light.intensity,
          light.color[2] * light.intensity, light.coneCosOuter);
        constants.lightDirectionAndCone[slot] = XMFLOAT4(
          light.direction[0], light.direction[1], light.direction[2], light.coneCosInner);
        constants.lightConeParams[slot] = XMFLOAT4(light.exponent,
          light.coneCosOuter >= 0.0f ? 1.0f : 0.0f, 0.0f, 0.0f);
      }
      uploadedLights = nearestCount;
      constants.lightCountAndParams = XMFLOAT4(static_cast<float>(nearestCount),
        0.0f, 0.0f, 0.0f);
    }
    worldLightsUploaded_ = uploadedLights;
    // N21: state the lighting the frame actually uploaded. A run with the
    // switch off prints the same line with enabled=0, so "the map's lights
    // changed nothing" and "they were never on" are different readings instead
    // of one silence. Printed after the fill, so `uploaded` is this pane's
    // post-cap count and not the previous frame's.
    if (!capturePath_.empty()) {
      std::printf("capture: lights enabled=%d sun=%d uploaded=%zu declared=%zu rejected=%zu "
        "sunIntensity=%.3f ambientIntensity=%.3f\n",
        worldLights_.enabled ? 1 : 0, worldLights_.sunPresent ? 1 : 0, uploadedLights,
        worldLights_.lightsDeclared, worldLights_.lightsRejected,
        worldLights_.sunIntensity, worldLights_.ambientIntensity);
    }
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
    // T101: the fog readings, copied from the struct that was just uploaded.
    worldShaderConstants_.fogEnabled = constants.fogParams.w > 0.5f;
    worldShaderConstants_.fogStart = constants.fogParams.x;
    worldShaderConstants_.fogEnd = constants.fogParams.y;
    worldShaderConstants_.fogMaxDensity = constants.fogParams.z;
    worldShaderConstants_.fogColor[0] = constants.fogColor.x;
    worldShaderConstants_.fogColor[1] = constants.fogColor.y;
    worldShaderConstants_.fogColor[2] = constants.fogColor.z;
    // N21: the light readings, copied from the same uploaded struct.
    worldShaderConstants_.worldLightsEnabled = constants.sunDirectionAndMode.w > 0.5f;
    worldShaderConstants_.sunDirection[0] = constants.sunDirectionAndMode.x;
    worldShaderConstants_.sunDirection[1] = constants.sunDirectionAndMode.y;
    worldShaderConstants_.sunDirection[2] = constants.sunDirectionAndMode.z;
    worldShaderConstants_.worldLightCount = static_cast<int>(uploadedLights);
    worldShaderConstants_.fogBoxToMapXZ = constants.fogScale.x;
    worldShaderConstants_.fogBoxToMapZ = constants.fogScale.z;
    // The sky box, with its own mvp: the same view and projection with the
    // translation removed, so the box stays centred on the camera and a
    // vertex's own direction is the direction the cube is sampled at. The
    // constants are uploaded here; the DRAW is deferred to the end of the
    // opaque world pass (drawWorldSkyBox above), under the sky's rasterizer
    // state because the box is seen from the inside, so the world's depth
    // already rejects the pixels it will paint (W-1 perf A/B).
    const bool hasSkyBox = worldSkyEnabled_ && worldSkyVertexBuffer_ && worldSkyVertexCount_ > 0
      && worldSkyView_ && worldSkyConstants_ && worldSkyRasterizerState_ && skyPixelShader_;
    if (hasSkyBox) {
      struct SkyConstantsData {
        XMFLOAT4X4 mvp;
        XMFLOAT4 lightDirectionAndMode;
        XMFLOAT4 cameraPositionAndSpecular;
        XMFLOAT4 materialFeatures;
        XMFLOAT4 lightmapFeatures;
        XMFLOAT4 envFeatures;
        XMFLOAT4 fogParams;
        XMFLOAT4 fogColor;
        XMFLOAT4 fogScale;
      } skyConstants{};
      // T101: the sky's copy is uploaded into the SAME buffer, so it has the same
      // required size -- see the note beside WorldConstantsData. The three fog
      // blocks are written with explicit no-fog sentinels rather than left at
      // whatever the initialiser left: the sky branch returns before the fog arm
      // today, but a struct member that is "probably not read" is how the
      // 144/160 over-read got in, and a zeroed fog params block is the same value
      // the disabled world pass uploads.
      static_assert(sizeof(SkyConstantsData) == 192,
        "SkyConstantsData must be exactly the world constant buffer's ByteWidth");
      XMMATRIX skyView = view;
      skyView.r[3] = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
      XMStoreFloat4x4(&skyConstants.mvp, skyView * projection);
      // Q6 (external evidence, engine gl_rsurf.cpp Shader_WorldEnd ->
      // gl_warp.cpp R_DrawSkyBox): the engine draws the sky AFTER the opaque
      // world, as up to six quads anchored at the far plane, so the pass is a
      // background and not a small box the map happens to be drawn over. This
      // box becomes that far plane here: XMFLOAT4X4 is row-major and the shader
      // multiplies a row vector, so output component j is sum_i p_i * m[i][j] --
      // column 3 is the w result and copying it into column 2 makes clip.z ==
      // clip.w for every vertex, i.e. z_ndc = 1 for the whole box at any
      // half-extent. The depth test (LESS_EQUAL, writes off) then reads a world
      // pixel -- always nearer than the far plane -- as a reject, and a pixel no
      // world draw touched as 1.0 against the cleared 1.0, which passes on
      // "equal". The colour is what the box produced before; what changes is
      // that the depth test now means what the pass order says it means.
      // The pin is a hair INSIDE the far plane, not on it. The exact pin is
      // measurable too: geometry whose clip.z is exactly clip.w lies on the
      // clip plane, and there the boundary is decided by float rounding. The
      // two variants move the same number of pixels (4 of ~861k, 1 LSB each,
      // across the three pinned poses) but not the same ones: the exact pin
      // puts all 4 at pr600 and none at up0, this inset spreads them 2 + 2.
      // What the inset buys is therefore not a smaller count but not sitting
      // on the clip plane; what it must not do is get wider -- see below.
      // A wider inset fails in the other direction: at 1e-3 the box is deep
      // enough inside the depth range that distant world surfaces stop
      // rejecting it, and 681 px of world were painted over by sky in the up0
      // pose. At 1e-6 the three pinned poses differ from the green frames by
      // 4 px total (2 at up0, 2 at pr600, 0 at up1800), all 1 LSB in one
      // channel, and all of them pixels no world draw touches (checked with
      // --capture-pass-depth: d24 = 16777215 at every one). See
      // docs/WORLD-HOLES-2026-11-17.md for the attribution table.
      const float farPlanePin = 1.0f - 1.0e-6f;
      for (int row = 0; row < 4; ++row)
        skyConstants.mvp.m[row][2] = skyConstants.mvp.m[row][3] * farPlanePin;
      // Mode 3 is the sky branch in the pixel shader, and it is the only field
      // the branch reads: the box is lit by nothing and carries no lightmap uv.
      skyConstants.envFeatures = XMFLOAT4(3.0f, 0.0f, 0.0f, 0.0f);
      // T101: explicit no-fog sentinels -- the same values a disabled world pass
      // uploads. Nothing here is left to the zero-initialiser's accident.
      skyConstants.fogParams = XMFLOAT4(0.0f, 0.0f, 0.0f, 0.0f);
      skyConstants.fogColor = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
      skyConstants.fogScale = XMFLOAT4(1.0f, 1.0f, 1.0f, 0.0f);
      context_->UpdateSubresource(worldSkyConstants_.Get(), 0, nullptr, &skyConstants, 0, 0);
      // W-1 perf A/B: the constants are uploaded, but the draw waits for the
      // opaque batches -- see the flush points and drawWorldSkyBox above.
      skyDrawPending = true;
    }
  }
  // The sky's draw is deferred: it runs at the end of the opaque world pass
  // (drawWorldSkyBox above), under its own rasterizer state and with depth
  // writes off, and under the discard-free sky shader, so every pixel the world
  // covers is rejected by the early depth test rather than shaded and then
  // covered.
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
    // N06 probe hook: the command line's debug view is a giant if/else chain in
    // main.cpp that MSVC refuses to grow (C1061 block nesting too deep), so the
    // two probe views are selected by environment instead. It is a diagnostic,
    // read once, and it only ever overrides a view the caller already asked for.
    static const int probeViewEnv = [] {
      const char* raw = std::getenv("TF2_NATIVE_WORLD_VIEW");
      return raw != nullptr ? std::atoi(raw) : 0;
    }();
    const int debugViewNow = probeViewEnv != 0 ? probeViewEnv : worldDebugView_;
    if (debugViewNow != 0 && frameConstantsValid) {
      WorldConstantsData debugConstants = frameConstants;
      // 1/2 are the flat/depth views and ride envFeatures.x (0 is the product
      // path); the albedo view rides envFeatures.y because x > 3.5 is already
      // claimed by the flat/depth block, and a mode 6 in x would be caught
      // there first -- measured: the first albedo capture was a depth frame.
      // T26 adds mode 4 = face id, which deliberately rides that same block at
      // 6.0 and is therefore a third member of it rather than a new sweep.
      debugConstants.envFeatures.x = debugViewNow == 2 ? 5.0f
        : debugViewNow == 3 ? 0.0f
        : debugViewNow == 4 ? 6.0f
        : debugViewNow == 5 ? 1.0f
        : debugViewNow == 6 ? 8.0f
        : debugViewNow == 7 ? 9.0f
        // N05 step 2: mode 10 is the magenta isolation view -- one flat opaque
        // colour, so "did this triangle paint this pixel" is a colour equality
        // rather than a difference from a background that other passes also fill.
        : debugViewNow == 10 ? 12.0f
        // N06 probe views: 11 rides 16.0 and 12 rides 17.0, both above the
        // magenta arm, and both are read in the shared pixel shader where the
        // albedo terms exist. They are diagnostics, not product modes.
        : debugViewNow == 11 ? 1.9f
        : debugViewNow == 12 ? 2.4f
        : debugViewNow == 13 ? 1.7f
        // N06 probe 4/5: 14 returns the composed colour the product path is
        // about to write (albedo * lighting, before fog), 15 returns the terms
        // that make it (diffuse, |normal|, lighting). Both ride values no other
        // arm claims (1.5-1.65, below the 1.65 lightmap probe).
        : debugViewNow == 14 ? 1.62f
        : debugViewNow == 15 ? 1.52f
        // N06 probe 6/7: the albedo's terms at one pixel (16) and the sample's
        // alpha next to the blend weight and the vertex colour (17).
        : debugViewNow == 16 ? 1.59f
        : debugViewNow == 17 ? 1.57f
        : debugViewNow == 18 ? 1.55f
        // Coverage mode (8) knowingly falls through to 4.0 so the world
        // never wears the sentinel; only the entity pass sits above it.
        // The id view (9) falls through for the same reason: the world is
        // background there, and a world pixel in an id colour would join a
        // per-entity mask it does not belong to.
        : 4.0f;
      debugConstants.envFeatures.y = debugViewNow == 3 ? 1.0f : 0.0f;
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
    worldBatchesIndexedAtDraw_ = batchesIndexed ? 1 : 0;
    worldIndexCountAtDraw_ = worldIndexCount_;
    if (batchesIndexed) {
      context_->IASetIndexBuffer(worldIndexBuffer_.Get(), DXGI_FORMAT_R32_UINT, 0);
      bool blendedStateSet = false;
      int worldBatchOrdinal = 0;
      for (const auto& batch : worldDrawBatches_) {
        if (batch.indexCount == 0) continue;
        // T96: the blended sub-batch is the only one that changes device state.
        // SrcAlpha/InvSrcAlpha with depth writes off, exactly what the viewmodel
        // pass does for its own `$translucent` ranges
        // (native_renderer_viewmodel.cpp:701). The opaque sub-batch explicitly
        // clears blending so a state left by an earlier pass cannot composite
        // the map.
        if (batch.alphaMode == 1) {
          // W-1 perf A/B: every opaque batch has written depth and the blended
          // ones are about to composite over it -- the sky belongs between
          // them, or a translucent surface would be over-painted by it.
          if (skyDrawPending) {
            drawWorldSkyBox();
            skyDrawPending = false;
          }
          context_->OMSetBlendState(worldAlphaBlendState_.Get(), nullptr, 0xffffffffu);
          context_->OMSetDepthStencilState(worldBlendedDepthStencilState_.Get(), 0);
          noteDepthState("world-blended:write-off");
          blendedStateSet = true;
        } else {
          context_->OMSetBlendState(nullptr, nullptr, 0xffffffffu);
        }
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
        // N05 step 3: the clip capture, bound for exactly this draw.
        //
        // The vertex shader is not touched: this geometry shader declares that
        // shader's own output struct as its input signature, copies every field
        // through, and streams position out to a buffer. The matrices, the
        // viewport and the constant block are the ones this frame already bound,
        // so a value read back is this draw's vertex stage and not a second
        // transform computed beside it.
        //
        // Bounded to the isolated draw on purpose. Under --world-triangle the
        // index buffer holds three indices, so exactly three vertices arrive and
        // the target (three clip quads) cannot overrun. On a batched draw with
        // more indices the stream output would silently stop at capacity, which
        // is a truncated sample being read as a whole one -- so the capture is
        // refused rather than truncated, and says so.
        bool clipCaptureBound = false;
        if (worldClipCaptureRequested_ && worldClipCaptureGeometryShader_ && worldClipCaptureBuffer_
            && worldIsolatedTriangle_ >= 0 && batch.indexCount == 3u) {
          worldClipCaptureAttempted_ = true;
          context_->GSSetShader(worldClipCaptureGeometryShader_.Get(), nullptr, 0);
          ID3D11Buffer* clipTargets[1] = { worldClipCaptureBuffer_.Get() };
          const UINT clipOffsets[1] = { 0u };
          context_->SOSetTargets(1, clipTargets, clipOffsets);
          clipCaptureBound = true;
        } else if (worldClipCaptureRequested_ && worldIsolatedTriangle_ < 0) {
          worldClipCaptureAttempted_ = true;
          worldClipCaptureFailure_ = "the clip capture needs --world-triangle: a batched draw "
            "would fill the three-vertex target and read back as a whole sample";
        }
        context_->DrawIndexed(batch.indexCount, batch.firstIndex, 0);
        // R1: this batch's OWN depth, beside the state it was drawn with. The alpha
        // mode is the batch's, read here rather than assumed, so "the blended batch
        // wrote nothing" is a reading and not a claim.
        if (!passDepthPrefix_.empty() && !capturePath_.empty()) {
          char passName[48];
          std::snprintf(passName, sizeof(passName), "20-world-batch%02d-alpha%d",
            worldBatchOrdinal, batch.alphaMode);
          capturePassDepth(passName);
        }
        ++worldBatchOrdinal;
        if (clipCaptureBound) {
          // Unbind before anything else can draw: the water, entity and viewmodel
          // passes follow this one and none of them has a geometry shader, so a
          // stream output target left bound would make every later draw in the
          // frame write into it (and, at three vertices, refuse the rest).
          ID3D11Buffer* noTargets[1] = { nullptr };
          context_->SOSetTargets(1, noTargets, nullptr);
          context_->GSSetShader(nullptr, nullptr, 0);
          // The read-back. CopyResource into the staging twin and map it once;
          // this runs only on a capture frame, and only around the isolated
          // draw, so a normal frame pays nothing for the channel existing.
          context_->CopyResource(worldClipCaptureReadback_.Get(), worldClipCaptureBuffer_.Get());
          D3D11_MAPPED_SUBRESOURCE clipMapped{};
          if (SUCCEEDED(context_->Map(worldClipCaptureReadback_.Get(), 0, D3D11_MAP_READ, 0, &clipMapped))) {
            const auto* clipQuads = static_cast<const float*>(clipMapped.pData);
            for (int v = 0; v < 12; ++v) worldClipCaptureVertex_[v] = clipQuads[v];
            context_->Unmap(worldClipCaptureReadback_.Get(), 0);
            // The index values and the viewport the draw used. Read off the
            // same draw rather than recomputed: an index buffer that disagreed
            // with the mapping the triangle table assumes is exactly the kind
            // of defect that would otherwise be attributed to the vertices.
            const std::uint32_t clipBase = worldIsolatedTriangle_ >= 0
              ? static_cast<std::uint32_t>(worldIsolatedTriangle_ * 3) : 0u;
            for (int j = 0; j < 3; ++j) worldClipCaptureIndex_[j] = clipBase + static_cast<std::uint32_t>(j);
            worldClipCaptureViewport_[0] = 0.0f;
            worldClipCaptureViewport_[1] = 0.0f;
            worldClipCaptureViewport_[2] = static_cast<float>(width_);
            worldClipCaptureViewport_[3] = static_cast<float>(height_);
            // A hash of the 64 bytes the vertex stage actually reads (the mvp)
            // plus the viewport, not of the whole 192-byte block: the other
            // blocks feed the pixel stage, and folding them in would let a
            // lighting edit look like a vertex-stage difference. FNV-1a, on the
            // frame's own constants -- the debug-view override that may have
            // re-uploaded them changes envFeatures only, which the VS never reads.
            const auto* clipMvpBytes = reinterpret_cast<const unsigned char*>(&frameConstants.mvp);
            unsigned int clipHash = 2166136261u;
            for (std::size_t b = 0; b < sizeof(frameConstants.mvp); ++b) {
              clipHash ^= clipMvpBytes[b];
              clipHash *= 16777619u;
            }
            for (int j = 0; j < 4; ++j) {
              const float extent = worldClipCaptureViewport_[j];
              const auto* extentBytes = reinterpret_cast<const unsigned char*>(&extent);
              for (std::size_t b = 0; b < sizeof(float); ++b) {
                clipHash ^= extentBytes[b];
                clipHash *= 16777619u;
              }
            }
            worldClipCaptureConstantsHash_ = clipHash;
            worldClipCaptureValid_ = true;
            worldClipCaptureFailure_ = "";
          } else {
            worldClipCaptureFailure_ = "Map refused the clip read-back buffer";
          }
        }
        ++worldBatchDrawCalls_;
        if (albedoView) ++worldBatchTexturedDrawCalls_;
      }
      if (skyDrawPending) {
        drawWorldSkyBox();
        skyDrawPending = false;
      }
      // Put the state back before the water and entity passes: an
      // OMSetBlendState is device state, not a per-draw argument.
      context_->OMSetBlendState(nullptr, nullptr, 0xffffffffu);
      if (blendedStateSet) context_->OMSetDepthStencilState(depthStencilState_.Get(), 0);
      worldVisDrewIndexed_ = !worldDiagnosticIsolation_ && worldVisClipApplied_;
    capturePassDepth("30-world-done");
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
      if (skyDrawPending) {
        drawWorldSkyBox();
        skyDrawPending = false;
      }
    capturePassDepth("30-world-done");
    }
    if (worldDebugView_ != 0 && frameConstantsValid) {
      context_->UpdateSubresource(worldConstants_.Get(), 0, nullptr, &frameConstants, 0, 0);
    }
  } else if (!worldDiagnosticIsolation_) {
    // W-1 perf A/B: this quad is painted OVER the sky, so the sky still has to
    // run first here -- exactly the order the whole pass used to have.
    if (skyDrawPending) {
      drawWorldSkyBox();
      skyDrawPending = false;
    }
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
  // W-1 perf A/B: a frame that reached here without any world draw (no world
  // vertex buffer at all) still owes its sky -- the old code drew it
  // unconditionally inside the constants block.
  if (skyDrawPending) {
    drawWorldSkyBox();
    skyDrawPending = false;
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
    // T78: the material's normal map, or nothing. t1 stays at nullptr when the
    // material named none (see the neutral-constants block below, which also
    // clears the shader's bump flag so a null slot is never sampled as if it
    // were a normal).
    ID3D11ShaderResourceView* waterBump = worldWaterBumpBound_ ? worldWaterBumpTexture_.view() : nullptr;
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
    context_->PSSetShaderResources(1, 1, &waterBump);
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
    // T78: the water pass draws with the world's constant buffer, and two of its
    // fields state something about a LIGHTMAPPED WORLD FACE that a water surface
    // is not. Both are neutralised here, not in the shader, for the reason the
    // T62 entity block below records: the world pass has to keep reading these
    // fields as what they are.
    //
    // (1) AMBIENT. `lightmapFeatures.x` is the map's lightmap intensity, a
    // per-map compensation for faces whose pixels sample the atlas. A water
    // vertex carries lightUv = (-1,-1) -- WorldWaterVertex's own comment says
    // why -- so its pixels never reach that sample and would keep the intensity
    // as their ambient colour. On cp_snakewater_final1 (intensity 0.439) the
    // whole surface was multiplied by 0.439 before the directional term, which
    // turns any water colour into a dark band. 1.0 is the neutral ambient the
    // directional term then shapes.
    // (2) ENV. `envFeatures.x` is the world's env mode (0 none, 1 the 2D
    // approximation, 2 a real cube). This pass binds t2 and t5 to nullptr -- it
    // has no env map of its own -- so on a map that has one the water would
    // sample an unbound slot and lerp 22% of nothing into its colour. Zero is
    // the mode this pass is actually in.
    //
    // Written for THIS pass only and restored below: the buffer is shared with
    // the world pass that just drew, and a world frame that inherited a neutral
    // ambient would be a picture of a map with no lightmap on it.
    //
    // (3) BUMP. `lightmapFeatures.y` is the shader's "this surface has a normal
    // map at t1" flag. It is inherited from whichever world draw ran last, and
    // neither answer is this pass's: 0 would throw away the water material's own
    // normal map (the ripples the surface is supposed to have) and 1 with no
    // texture bound would tilt every normal by a null sample. The flag is
    // therefore written from what this pass actually bound, and the bind above
    // reads the same flag, so the shader cannot be told to sample a slot this
    // pass left empty.
    const float waterBumpFlag = worldWaterBumpBound_ ? 1.0f : 0.0f;
    const bool waterConstantsDiffer = frameConstantsValid
      && (frameConstants.lightmapFeatures.x != 1.0f
          || frameConstants.lightmapFeatures.y != waterBumpFlag
          || frameConstants.envFeatures.x != 0.0f);
    if (waterConstantsDiffer) {
      WorldConstantsData waterOnlyConstants = frameConstants;
      waterOnlyConstants.lightmapFeatures.x = 1.0f;
      waterOnlyConstants.lightmapFeatures.y = waterBumpFlag;
      waterOnlyConstants.envFeatures.x = 0.0f;
      context_->UpdateSubresource(worldConstants_.Get(), 0, nullptr, &waterOnlyConstants, 0, 0);
    }
    context_->Draw(worldWaterVertexCount_, 0);
    worldWaterDrewThisFrame_ = true;
  capturePassDepth("40-water");
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
    if (waterConstantsDiffer) {
      context_->UpdateSubresource(worldConstants_.Get(), 0, nullptr, &frameConstants, 0, 0);
    }
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
  if (!worldDiagnosticIsolation_ && !entityModelDrawRanges_.empty() && entityModelInstanceBuffer_
      && (entityModelInstanceCount_ + mapStaticPropInstanceCount_) > 0
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
    // T62: this pass re-uploads the frame constants for ITS OWN draws, for two
    // reasons, and both live here so the pass has one upload site.
    //
    // (1) AMBIENT. `lightmapFeatures.x` is the map's lightmap intensity, a
    // per-map compensation for lightmapped WORLD faces. An entity vertex
    // writes lightUv = (-1,-1), so its pixels never reach the lightmap sample
    // and would keep that intensity as their ambient colour. On
    // koth_bagel_rc11 (intensity 0.386) every player body was multiplied by
    // 0.386 before the directional term and a BLU Pyro's suit landed at mean
    // luma 8/255 -- a black silhouette that was not a texture, mesh or bone
    // defect. 1.0 is the neutral ambient the directional term then shapes,
    // and it is written HERE rather than in the shader so the world pass keeps
    // reading `lightmapFeatures.x` as the factor it is.
    //
    // (2) T61 DIAGNOSTICS. The same flat/albedo/face-id views the world pass
    // has, applied to the entity pass. Without them the entity pixels can only
    // be read as the product's own shading, so a black body cannot be told
    // apart from a body whose albedo sample is black. Written for THIS pass
    // only and restored below, for the reason the world block records: the
    // constants buffer is shared, and a debug frame that also flattens the
    // world would be a picture of the wrong pass.
    const bool entityConstantsDiffer = frameConstantsValid
      && (worldDebugView_ != 0 || frameConstants.lightmapFeatures.x != 1.0f);
    if (entityConstantsDiffer) {
      WorldConstantsData entityConstants = frameConstants;
      entityConstants.lightmapFeatures.x = 1.0f;
      if (worldDebugView_ != 0) {
        entityConstants.envFeatures.x = worldDebugView_ == 2 ? 5.0f
          : worldDebugView_ == 3 ? 0.0f
          : worldDebugView_ == 4 ? 6.0f
          : worldDebugView_ == 5 ? 1.0f
          : worldDebugView_ == 6 ? 8.0f
          : worldDebugView_ == 7 ? 9.0f
          // T61.1: mode 9 (per-entity id) is 11.0, one step above the
          // coverage sentinel, because its arm tests x > 10.5.
          : worldDebugView_ == 8 ? 10.0f
          : worldDebugView_ == 9 ? 11.0f : 4.0f;
        entityConstants.envFeatures.y = worldDebugView_ == 3 ? 1.0f : 0.0f;
      }
      context_->UpdateSubresource(worldConstants_.Get(), 0, nullptr, &entityConstants, 0, 0);
    }
    // R1b: the depth the entity pass starts from, so the first drawn range is
    // measured against the world pass rather than against nothing.
    captureRangeDepth("entity-start", -1, -1, -1);
    int entityRangeOrdinal = 0;
    for (int blended = 0; blended < 2; ++blended) {
    for (const auto& range : entityModelDrawRanges_) {
      if (range.translucent != (blended != 0)) continue;
      if (!range.vertexBuffer || range.vertexCount < 3 || range.instanceCount == 0) continue;
      // Entity meshes have their own material domain. Binding the world atlas
      // when that material is missing paints characters with unrelated map
      // tiles and makes a resource failure look like a valid draw.
      if (!range.textureView) continue;
      // R1b negative control: drop one drawn range on purpose to show the
      // footprint follows the DRAW and not the tick. It changes the picture by
      // design; it is a control, never a mode. N17: the gate reads the skip index
      // alone -- requiring the depth prefix too meant a skip-only run silently
      // rendered the unchanged frame, which is a control that cannot fail.
      // N17 item 1: the per-range account the window was missing. Guarded by an
      // environment variable (the same way TF2_NATIVE_TRACE is armed) so a normal
      // run prints nothing; with TF2_NATIVE_RANGE_LIST=1 each numbered range names
      // its model, run window and material, and a skip index can be checked
      // against the geometry instead of being believed.
      static const bool rangeListDump = std::getenv("TF2_NATIVE_RANGE_LIST") != nullptr;
      if (rangeListDump) {
        std::printf("range: ordinal=%d blended=%d slot=%u body=%u key=%s vertexStart=%u vertexCount=%u instanceStart=%u instanceCount=%u translucent=%d textured=%d dbg=%d\n",
          entityRangeOrdinal, blended, range.materialSlot, range.body,
          range.cacheKey.empty() ? "(unknown)" : range.cacheKey.c_str(),
          range.vertexStart, range.vertexCount, range.instanceStart, range.instanceCount,
          range.translucent ? 1 : 0, 1, worldDebugView_);
      }
      if (rangeDepthSkipIndex_ >= 0 && entityRangeOrdinal == rangeDepthSkipIndex_) {
        // R1b: name what is being dropped. "skip 97" is not a finding; the model,
        // the material run and the vertex window are. Printed once per frame,
        // and only when a skip is armed.
        std::printf("range-skip: ordinal=%d blended=%d slot=%u body=%u key=%s vertexStart=%u vertexCount=%u instanceStart=%u instanceCount=%u translucent=%d dbg=%d\n",
          entityRangeOrdinal, blended, range.materialSlot, range.body,
          range.cacheKey.empty() ? "(unknown)" : range.cacheKey.c_str(),
          range.vertexStart, range.vertexCount, range.instanceStart, range.instanceCount,
          range.translucent ? 1 : 0, worldDebugView_);
        ++entityRangeOrdinal;
        continue;
      }
      context_->RSSetState(range.noCull ? worldNoCullRasterizerState_.Get() : rasterizerState_.Get());
      ID3D11BlendState* materialBlend = range.translucent ? worldAlphaBlendState_.Get() : nullptr;
      if ((range.materialFlags & 64u) != 0u) materialBlend = mapAdditiveBlendState_.Get();
      if ((range.materialFlags & 1024u) != 0u) materialBlend = (range.materialFlags & 2048u) != 0u
        ? mapModulate2xBlendState_.Get() : mapModulateBlendState_.Get();
      context_->OMSetBlendState(materialBlend, nullptr, 0xffffffffu);
      context_->OMSetDepthStencilState(range.translucent ? worldBlendedDepthStencilState_.Get() : depthStencilState_.Get(), 0);
      noteDepthState(range.translucent ? "entity-blended:write-off" : "entity:write-all");
      auto* rangeView = range.textureView;
      context_->PSSetShaderResources(0, 1, &rangeView);
      auto* secondView = range.secondTextureView;
      context_->PSSetShaderResources(11, 1, &secondView);
      ModelMaterialProxyConstantsData proxyConstants{};
      proxyConstants.texture2Scroll = DirectX::XMFLOAT4(range.proxyScrollU,
        range.proxyScrollV, 0.0f, 0.0f);
      proxyConstants.color = DirectX::XMFLOAT4(range.proxyColor,
        range.proxyColor, range.proxyColor, 1.0f);
      proxyConstants.flags = DirectX::XMFLOAT4(
        range.proxy.textureScroll ? 1.0f : 0.0f,
        range.proxy.sineColor ? 1.0f : 0.0f, 0.0f, 0.0f);
      context_->UpdateSubresource(worldMaterialProxyConstants_.Get(), 0, nullptr,
        &proxyConstants, 0, 0);
      auto* proxyBuffer = worldMaterialProxyConstants_.Get();
      context_->PSSetConstantBuffers(1, 1, &proxyBuffer);
      ID3D11Buffer* buffers[2] = { range.vertexBuffer, instanceBuffer };
      // The run is a window into the model's one vertex buffer: offset the bind
      // instead of uploading a buffer per material.
      const UINT rangeOffsets[2] = {
        range.vertexStart * static_cast<UINT>(sizeof(EntityModelGpuVertex)), 0u };
      context_->IASetVertexBuffers(0, 2, buffers, strides, rangeOffsets);
      context_->DrawInstanced(range.vertexCount, range.instanceCount, 0, range.instanceStart);
      // R1b: this range's own depth, read after it drew and named by the same
      // ordinal the skip control counts, so a control run can name the range it
      // removed.
      captureRangeDepth("draw", entityRangeOrdinal, blended,
        static_cast<int>(range.materialSlot));
      ++entityRangeOrdinal;
    }
    }
    context_->RSSetState(rasterizerState_.Get());
    context_->OMSetBlendState(nullptr, nullptr, 0xffffffffu);
    context_->OMSetDepthStencilState(depthStencilState_.Get(), 0);
    noteDepthState("entity-restore:write-all");
    // R1: whatever the entity passes left. The occluder class M2b could only name
    // as {static prop, map model} shows up here as depth it actually wrote.
    capturePassDepth("50-entities");
    if (entityConstantsDiffer) {
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
        noteDepthState("overlay:own");
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
              const float legacyHalfZ = 36.0f * worldHorizontalScale_;
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
      noteDepthState("overlay-restore:write-all");
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
    capturePassDepth("60-overlay");
  // T21: the depth the frame has after the world, water and entity passes and
  // BEFORE the ViewModel pass clears it (the weapon's own pass clears depth so
  // the weapon draws over the map). Read here because this is the last moment
  // at which the depth buffer describes the world the face trace walked; after
  // `drawViewModel()` every world pixel reads 1.0 -- measured, the first
  // version of this readback was taken after the clear and answered 00ffffff
  // at every point.
  capturePassDepth("70-hud-pre");
  if (!capturePath_.empty() && paneIndex + 1u == paneCount) captureDepthBuffer(false);
  // First person, after the world and entity passes and before the capture, so
  // the frame written to disk already contains the weapon. The pass restores the
  // world depth state it overrides, so a later HUD pass (T3) is unaffected.
  // U4 step 2b deferred the split frame's weapon because the pass built its
  // projection from the window aspect. N18 closes that: the pass takes the pane's
  // own width/height, and it draws only in pane 0 while that pane stands in the
  // player. Pane 1 is a second camera -- a weapon overlay there would be the
  // local player's gun drawn into somebody else's eyeline, and both panes are
  // DemoView when pane 1 follows a player, so "any DemoView pane" is too loose.
  // The single-pane path keeps drawing unconditionally, so a lone orbit view
  // behaves exactly as before.
  const bool paneStandsInPlayer =
    paneIndex == 0u && paneBoundary_.panes[paneIndex].camera == PaneCameraSource::DemoView;
  if (paneCount == 1u || paneStandsInPlayer) {
    drawViewModel(paneWidth, paneHeight);
  }
  context_->OMSetDepthStencilState(depthStencilState_.Get(), 0);
  noteDepthState("post-viewmodel:write-all");
  capturePassDepth("80-post-viewmodel");
  context_->RSSetState(rasterizerState_.Get());
  // T51: the custom HUD, after everything that draws the world and the weapon
  // and before the frame is captured. It is the LAST pass on purpose: a HUD over
  // a weapon is what a HUD is, and a pass that can only write inside its own
  // scissored rectangles cannot change a world pixel by construction -- which is
  // the assertion hud-check.sh makes by comparing two captures outside those
  // rectangles.
  // U4 step 2b: a split frame draws the HUD once, over the whole window. The HUD
  // speaks in back-buffer coordinates and scissors itself to client-sized
  // rectangles, so the last pane restores the full-client viewport and runs it;
  // in a single-pane frame the viewport above already is that viewport.
  if (hudEnabled_ && paneIndex + 1u == paneCount) {
    if (paneCount > 1u) {
      D3D11_VIEWPORT fullViewport{};
      fullViewport.Width = static_cast<float>(width_);
      fullViewport.Height = static_cast<float>(height_);
      fullViewport.MinDepth = 0.0f;
      fullViewport.MaxDepth = 1.0f;
      context_->RSSetViewports(1, &fullViewport);
    }
    drawHudPass();
  }
  // U4 step 1: the HUD half of the pane contract. hudPassOrdinal_ is non-zero
  // only when the pass actually ran (drawHudPass returns early on missing
  // resources), and hudScissorApplied_ is written by the draw itself -- the two
  // facts the scissor rule needs, taken after the pass rather than guessed.
  paneBoundary_.hudPasses = hudPassOrdinal_ != 0 ? 1u : 0u;
  paneBoundary_.hudScissorApplied = hudScissorApplied_;
  // Capture before Present: once Present has run the back buffer contents are
  // no longer defined, so this is the only point at which "the frame we just
  // composed" is still readable.
  // U4 step 2b: the frame is written and presented once, by the pane that
  // finished it. Capturing from pane 0 would write a half-composed frame.
  if (paneIndex + 1u == paneCount) {
    if (!capturePath_.empty()) {
      const std::wstring path = capturePath_;
      capturePath_.clear();
      passDepthPrefix_.clear();
      rangeDepthPrefix_.clear();
      rangeDepthPrev_.clear();
      rangeDepthSkipIndex_ = -1;
      captureSucceeded_ = writeBackBufferToFile(path);
    }
    // The depth that belongs to the frame just written. Read from the same
    // capture (frame loop, before the ViewModel clear), so picture and depth
    // describe one frame -- a depth image of a different tick would compare two
    // cameras.
    if (!captureDepthPath_.empty()) {
      const std::wstring depthPath = captureDepthPath_;
      captureDepthPath_.clear();
      depthCaptureSucceeded_ = writeWorldDepthToPgm(depthPath);
    }
    const HRESULT result = swapChain_->Present(settings_.vsync ? 1 : 0, 0);
    if (FAILED(result)) { lastError_ = result; return false; }
  }
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
  // F0-c: the counters below are incremented while the vertices are generated,
  // which is before the device has accepted a buffer to put them in. A pass that
  // returns after that point drew nothing, so the readings that describe what the
  // last draw covered go back to zero -- otherwise a refused upload is reported as
  // three hundred primitives that no pixel shows.
  auto discardPass = [this]() {
    hudDrawnPrimitives_ = 0;
    hudDrawnRects_ = 0;
    hudTextQuads_ = 0;
    hudGlyphCount_ = 0;
    hudUnmappedGlyphs_ = 0;
    hudImageQuads_ = 0;
    hudRectVertexCount_ = 0;
  };
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
    // T68: an image primitive with nothing resident would sample an unbound t1.
    // Skipping it is the same outcome as drawing nothing (an unbound fetch reads
    // zero, which blends as transparent), but it also keeps the counter honest:
    // hudImageQuads() answers "quads that read real pixels".
    if (primitive.kind == HudPrimitiveKind::Image && !hudImageView_) continue;
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
    // T68: an Image primitive carries its own crop in uv, so the pass keeps one
    // vertex format and one draw call; only the sampled slot and the shader path
    // differ.
    float uv[4] = {0.0f, 0.0f, 1.0f, 1.0f};
    if (primitive.kind == HudPrimitiveKind::Image) {
      uv[0] = primitive.uv[0];
      uv[1] = primitive.uv[1];
      uv[2] = primitive.uv[2];
      uv[3] = primitive.uv[3];
    }
    float kind = 0.0f;
    if (primitive.kind == HudPrimitiveKind::Circle) kind = 3.0f;
    else if (primitive.kind == HudPrimitiveKind::Image) kind = 4.0f;
    else if (primitive.filled) kind = 1.0f;
    pushQuad(left, top, right, bottom, primitive.colour, locals, uv, kind);
    if (primitive.kind == HudPrimitiveKind::Rect) ++hudDrawnRects_;
    else if (primitive.kind == HudPrimitiveKind::Image) ++hudImageQuads_;
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
    // F0-c: the replacement is built in a temporary and swapped in only once
    // the device accepted it. Releasing the old buffer as an argument to
    // CreateBuffer -- which is what this did -- destroys it before the call is
    // even attempted, so a refusal leaves no buffer at all while the capacity
    // still claims there is one.
    Microsoft::WRL::ComPtr<ID3D11Buffer> hudBufferResultTemporary;
    const HRESULT hudBufferResult = resourceFaultHit("hud.createbuffer")
      ? static_cast<HRESULT>(E_OUTOFMEMORY)
      : device_->CreateBuffer(&description, nullptr, hudBufferResultTemporary.GetAddressOf());
    if (FAILED(hudBufferResult)) {
      ++resourceRefusals_;
      std::fprintf(stderr,
        "[renderer-resource] refuse surface=hud.createbuffer kind=create hresult=0x%08lX capacity=%zu vertices=%zu\n",
        static_cast<unsigned long>(hudBufferResult), hudRectCapacity_,
        static_cast<std::size_t>(vertexCount));
      std::fflush(stderr);
      discardPass();
      return;
    }
    hudVertexBuffer_ = std::move(hudBufferResultTemporary);
    hudRectCapacity_ = vertexCount;
  }
  D3D11_MAPPED_SUBRESOURCE mapped{};
  const HRESULT hudMapResult = resourceFaultHit("hud.map")
    ? static_cast<HRESULT>(E_OUTOFMEMORY)
    : context_->Map(hudVertexBuffer_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
  if (FAILED(hudMapResult)) {
    ++resourceMapRefusals_;
    std::fprintf(stderr,
      "[renderer-resource] refuse surface=hud.map kind=map hresult=0x%08lX vertices=%zu\n",
      static_cast<unsigned long>(hudMapResult), static_cast<std::size_t>(vertexCount));
    std::fflush(stderr);
    discardPass();
    return;
  }
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
  noteDepthState("hud:own");
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
  // The font atlas and -- only when the list actually contained an image quad --
  // the resident basemap are the only textures this pass may sample. Binding them
  // here and clearing both after the draw keeps the "a HUD frame differs from a
  // HUD-less frame only in the pixels the pass drew" claim true for the resource
  // slots as well as for the pixels: a slot this pass never used is never left
  // holding a view a later pass could sample by accident.
  auto* fontView = hudFontView_.Get();
  auto* fontSampler = hudSampler_.Get();
  context_->PSSetShaderResources(0, 1, &fontView);
  context_->PSSetSamplers(0, 1, &fontSampler);
  if (hudImageQuads_ > 0 && hudImageView_ && hudImageSampler_) {
    auto* imageView = hudImageView_.Get();
    auto* imageSampler = hudImageSampler_.Get();
    context_->PSSetShaderResources(1, 1, &imageView);
    context_->PSSetSamplers(1, 1, &imageSampler);
  }
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
  ID3D11ShaderResourceView* noTexture[2] = {nullptr, nullptr};
  context_->PSSetShaderResources(0, 2, noTexture);
  ID3D11SamplerState* noSampler[2] = {nullptr, nullptr};
  context_->PSSetSamplers(0, 2, noSampler);
}

} // namespace tf2::native
