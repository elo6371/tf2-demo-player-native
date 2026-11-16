#pragma once

// U4 step 1 (2026-11-16): the single-surface pane contract, written down as
// code instead of as a comment.
//
// WHY THIS FILE EXISTS
//   U4 makes the viewer show two panes of one demo. Before that can be built,
//   the things one pane currently shares with everything else have to be named:
//   a frame binds exactly one render target, one depth view and one viewport
//   that covers the whole client, is built from one camera, and runs at most one
//   HUD pass which must be scissored. Those are the facts a second pane WILL
//   have to stop being true; while they hold, a per-pane implementation that
//   reuses these members is silently wrong rather than visibly unsupported.
//   So the contract is a predicate: the renderer records the readings it
//   actually produced, and this header decides whether they describe one
//   surface. A second pane must edit this predicate on purpose -- which is the
//   review the boundary needs -- instead of inheriting a shared viewport.
//
//   The readings are facts about a draw (what was bound), not requests (what a
//   caller asked for): main.cpp already prints a `scene: camera` line that
//   reports the REQUEST, and the mismatch between request and frame is exactly
//   what this file exists to keep visible.

#include <cstdint>

namespace tf2::native {

// Which camera the world pass actually built its view matrix from. `None` means
// no world camera was available this frame (no constants), which is a broken
// frame, not an orbit frame.
enum class PaneCameraSource { None = 0, Orbit, DemoView };

inline const char* paneCameraSourceName(PaneCameraSource source) {
  switch (source) {
  case PaneCameraSource::Orbit: return "orbit";
  case PaneCameraSource::DemoView: return "demo-view";
  case PaneCameraSource::None: break;
  }
  return "none";
}

// What one Renderer::draw() bound and used. Filled at the point the bindings
// are made (not reconstructed later), so a reading cannot disagree with the
// frame it describes.
struct PaneBoundaryReadings {
  // 1-based count of draws this Renderer has started; 0 before the first draw.
  std::uint32_t drawOrdinal = 0;
  // Bound output surfaces. The contract is one of each.
  std::uint32_t renderTargets = 0;
  std::uint32_t depthViews = 0;
  std::uint32_t viewports = 0;
  // Viewport extent vs the client rectangle the swap chain presents to.
  std::uint32_t viewportWidth = 0;
  std::uint32_t viewportHeight = 0;
  std::uint32_t clientWidth = 0;
  std::uint32_t clientHeight = 0;
  PaneCameraSource camera = PaneCameraSource::None;
  // HUD passes run this frame (0 or 1 today) and whether the last one
  // scissored itself. A HUD pass that ran without a scissor can paint outside
  // its rectangles, which is the claim hud-check.sh rests on.
  std::uint32_t hudPasses = 0;
  bool hudScissorApplied = false;
};

// One bit per broken rule. A bitmask rather than a bool so a probe can prove
// each rule is separately reachable: a predicate that always answers 0 is the
// "gate that passed while checking nothing" this repository has paid for.
enum PaneBoundaryMismatch : std::uint32_t {
  kPaneMismatchNone = 0u,
  kPaneMismatchRenderTargets = 1u << 0,
  kPaneMismatchDepthViews = 1u << 1,
  kPaneMismatchViewportCount = 1u << 2,
  kPaneMismatchViewportExtent = 1u << 3,
  kPaneMismatchCamera = 1u << 4,
  kPaneMismatchHudPasses = 1u << 5,
  kPaneMismatchHudScissor = 1u << 6,
};

constexpr std::uint32_t kPaneMismatchAll = (1u << 7) - 1u;

// The contract. Every rule is about what must hold for ONE pane drawing into the
// ONE surface the swap chain owns; a two-pane frame either draws the panes with
// two scissored viewports through this same single surface (and then this
// predicate has to learn about panes) or gives each pane its own target (and
// then the renderer stops owning a single target). Either way the change is
// explicit here.
inline std::uint32_t paneBoundaryMismatches(const PaneBoundaryReadings& readings) {
  std::uint32_t mismatches = kPaneMismatchNone;
  if (readings.renderTargets != 1u) mismatches |= kPaneMismatchRenderTargets;
  if (readings.depthViews != 1u) mismatches |= kPaneMismatchDepthViews;
  if (readings.viewports != 1u) mismatches |= kPaneMismatchViewportCount;
  if (readings.viewportWidth != readings.clientWidth
      || readings.viewportHeight != readings.clientHeight
      || readings.viewportWidth == 0u || readings.viewportHeight == 0u) {
    mismatches |= kPaneMismatchViewportExtent;
  }
  if (readings.camera == PaneCameraSource::None) mismatches |= kPaneMismatchCamera;
  if (readings.hudPasses > 1u) mismatches |= kPaneMismatchHudPasses;
  if (readings.hudPasses != 0u && !readings.hudScissorApplied) mismatches |= kPaneMismatchHudScissor;
  return mismatches;
}

} // namespace tf2::native
