#pragma once

// U4 (2026-11-16): the pane contract, written down as code instead of as a
// comment.
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
//   surface -- or, since step 2, one surface split into panes that do not share
//   a viewport, a view matrix or a camera state.
//
//   Step 2a (this revision) teaches the predicate about panes. The three errors
//   that were impossible to STATE before now have their own bits, and
//   native_ui_probe's negative controls feed each one through the predicate:
//     * two panes sharing one viewport       -> kPaneMismatchViewportCount
//     * two panes sharing one view matrix    -> kPaneMismatchSharedView
//     * two panes sharing one camera state   -> kPaneMismatchSharedCamera
//   The three of them are the boundary this step exists to make visible: an
//   implementation that half-builds the second pane fails here instead of
//   drawing the same camera twice.
//
//   The readings are facts about a draw (what was bound and used), not requests
//   (what a caller asked for): main.cpp already prints a scene-camera line that
//   reports the REQUEST, and the mismatch between request and frame is exactly
//   what this file exists to keep visible.

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace tf2::native {

// Which camera the world pass actually built its view matrix from. None means
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

// Two panes is the U4 step 2 shape. The array is sized here rather than left
// dynamic so a four-window budget has to bump this constant on purpose -- the
// same review the predicate's bits ask for.
constexpr std::uint32_t kPaneMaxPanes = 2u;
// "This pane's camera slot was not recorded". A recorded slot is a small index
// into the renderer's per-pane camera array; two panes reporting the same index
// is the shared-camera defect.
constexpr std::uint32_t kPaneCameraSlotUnset = 0xFFFFFFFFu;

// The camera inputs one pane draws from. Kept as plain values (not a pointer to
// renderer members) so the readings can hash what the pane used without
// aliasing renderer state, and so "which fields does a pane own" is answerable
// by reading this struct.
struct PaneCameraState {
  bool demoView = false;
  float demoEye[3] = {0.0f, 0.0f, 0.0f};
  float demoPitchDeg = 0.0f;
  float demoYawDeg = 0.0f;
  bool focusValid = false;
  float focus[3] = {0.0f, 0.0f, 0.25f};
  float orbitYaw = 0.0f;
  float orbitPitch = 0.36f;
  float orbitDistance = 1.65f;
};

// FNV-1a over bytes: the hashes below are equality witnesses for values the
// predicate cannot compare field by field (a 4x4 matrix, a camera's nine
// floats). A hash is not a decision about a camera; it only lets two panes say
// "we used the same numbers" or "we did not".
inline std::uint64_t paneHashBytes(std::uint64_t hash, const void* data, std::size_t size) {
  const unsigned char* bytes = static_cast<const unsigned char*>(data);
  for (std::size_t i = 0; i < size; ++i) {
    hash ^= static_cast<std::uint64_t>(bytes[i]);
    hash *= 1099511628211ull;
  }
  return hash;
}

inline std::uint64_t paneHashFloats(std::uint64_t hash, const float* values, std::size_t count) {
  return paneHashBytes(hash, values, sizeof(float) * count);
}

inline std::uint64_t paneHashFloats(const float* values, std::size_t count) {
  return paneHashFloats(1469598103934665603ull, values, count);
}

inline std::uint64_t paneHashUint32(std::uint64_t hash, std::uint32_t value) {
  return paneHashBytes(hash, &value, sizeof(value));
}

// The inputs a pane's view matrix was built from, hashed. Two panes with
// different fingerprints must not report the same view matrix, and two panes
// with the same fingerprint are allowed to agree (a split that deliberately
// shows one camera twice is a viewer choice, not a sharing bug).
inline std::uint64_t paneCameraFingerprint(const PaneCameraState& camera) {
  std::uint64_t hash = 1469598103934665603ull;
  hash = paneHashUint32(hash,
    (camera.demoView ? 1u : 0u) | (camera.focusValid ? 2u : 0u));
  hash = paneHashFloats(hash, camera.demoEye, 3u);
  hash = paneHashFloats(hash, &camera.demoPitchDeg, 1u);
  hash = paneHashFloats(hash, &camera.demoYawDeg, 1u);
  hash = paneHashFloats(hash, camera.focus, 3u);
  hash = paneHashFloats(hash, &camera.orbitYaw, 1u);
  hash = paneHashFloats(hash, &camera.orbitPitch, 1u);
  hash = paneHashFloats(hash, &camera.orbitDistance, 1u);
  return hash;
}

// What one pane occupied and what it was built from.
struct PaneBoundaryPaneReadings {
  // Back-buffer rectangle the pane's viewport covered (top-left origin).
  std::uint32_t x = 0;
  std::uint32_t y = 0;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  PaneCameraSource camera = PaneCameraSource::None;
  // Hash of the view matrix this pane uploaded (16 floats), and of the camera
  // inputs above. Equal matrices over different inputs is the shared-matrix
  // defect; a shared state slot is the shared-camera defect.
  std::uint64_t viewHash = 0;
  std::uint64_t cameraFingerprint = 0;
  std::uint32_t cameraSlot = kPaneCameraSlotUnset;
};

// What one Renderer::draw() call (or one frame across the calls that compose it)
// bound and used. Filled at the point the bindings are made (not reconstructed
// later), so a reading cannot disagree with the frame it describes.
struct PaneBoundaryReadings {
  // 1-based count of frames this Renderer has started; 0 before the first one.
  std::uint32_t drawOrdinal = 0;
  // Bound output surfaces. The contract is still one surface: two panes share
  // it through disjoint viewports (that is the budget statement step 2 makes).
  std::uint32_t renderTargets = 0;
  std::uint32_t depthViews = 0;
  // Viewports bound to compose the frame: 1 for a single pane, one per pane for
  // a split. A split that bound one viewport for two panes breaks R3.
  std::uint32_t viewports = 0;
  // Pane 0's extent (the legacy spelling of it, kept because every existing
  // reader of this struct reads these two fields) and the client rectangle the
  // swap chain presents to.
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
  // Panes composed into this frame, and their readings. paneCount defaults to
  // 1 so a single-pane caller that only fills the legacy fields still describes
  // one pane -- but such a caller must also fill panes[0], because R4 checks
  // that the two spellings agree.
  std::uint32_t paneCount = 1u;
  PaneBoundaryPaneReadings panes[kPaneMaxPanes]{};
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
  // Step 2a: the pane-aware rules.
  kPaneMismatchPaneLayout = 1u << 7,
  kPaneMismatchSharedView = 1u << 8,
  kPaneMismatchSharedCamera = 1u << 9,
};

constexpr std::uint32_t kPaneMismatchAll = (1u << 10) - 1u;

// The contract. One frame, one surface, one depth view, and as many panes as it
// says it has -- each pane with its own viewport rectangle, its own view matrix
// and its own camera state slot.
inline std::uint32_t paneBoundaryMismatches(const PaneBoundaryReadings& readings) {
  std::uint32_t mismatches = kPaneMismatchNone;
  if (readings.renderTargets != 1u) mismatches |= kPaneMismatchRenderTargets;
  if (readings.depthViews != 1u) mismatches |= kPaneMismatchDepthViews;
  if (readings.paneCount == 0u || readings.paneCount > kPaneMaxPanes) {
    mismatches |= kPaneMismatchPaneLayout;
  }
  const std::uint32_t paneCount = readings.paneCount == 0u
    ? kPaneMaxPanes + 1u : readings.paneCount;
  if (readings.viewports != paneCount) mismatches |= kPaneMismatchViewportCount;
  // R5b: the legacy camera field must be pane 0's camera, the same two-spellings
  // argument R4 makes for the extent. A frame whose summary line disagrees with
  // the pane it summarises is a trace bug even when both cameras are valid.
  if (readings.camera != readings.panes[0].camera) mismatches |= kPaneMismatchCamera;
  // R4: the legacy extent pair and pane 0 must be the same reading, and no pane
  // may be empty. Two spellings of one fact drifting apart is how a trace ends
  // up describing a viewport the frame did not bind.
  if (readings.panes[0].width != readings.viewportWidth
      || readings.panes[0].height != readings.viewportHeight
      || readings.panes[0].width == 0u || readings.panes[0].height == 0u) {
    mismatches |= kPaneMismatchViewportExtent;
  }
  for (std::uint32_t i = 0; i < paneCount && i < kPaneMaxPanes; ++i) {
    const PaneBoundaryPaneReadings& pane = readings.panes[i];
    if (pane.width == 0u || pane.height == 0u) mismatches |= kPaneMismatchViewportExtent;
    if (pane.camera == PaneCameraSource::None) mismatches |= kPaneMismatchCamera;
  }
  // R8: the pane rectangles must tile the client -- inside it, pairwise
  // disjoint, and covering it exactly once. Two panes that both claim the left
  // half overlap; two panes that both claim the full client cover it twice;
  // a split that leaves a strip uncovered covers it zero times.
  bool layoutBroken = false;
  std::uint64_t coveredArea = 0ull;
  for (std::uint32_t i = 0; i < paneCount && i < kPaneMaxPanes; ++i) {
    const PaneBoundaryPaneReadings& pane = readings.panes[i];
    if (static_cast<std::uint64_t>(pane.x) + pane.width > readings.clientWidth
        || static_cast<std::uint64_t>(pane.y) + pane.height > readings.clientHeight) {
      layoutBroken = true;
    }
    coveredArea += static_cast<std::uint64_t>(pane.width) * pane.height;
    for (std::uint32_t j = 0; j < i; ++j) {
      const PaneBoundaryPaneReadings& other = readings.panes[j];
      const bool disjoint =
        static_cast<std::uint64_t>(pane.x) + pane.width <= other.x
        || static_cast<std::uint64_t>(other.x) + other.width <= pane.x
        || static_cast<std::uint64_t>(pane.y) + pane.height <= other.y
        || static_cast<std::uint64_t>(other.y) + other.height <= pane.y;
      if (!disjoint) layoutBroken = true;
    }
  }
  if (coveredArea != static_cast<std::uint64_t>(readings.clientWidth) * readings.clientHeight) {
    layoutBroken = true;
  }
  if (layoutBroken) mismatches |= kPaneMismatchPaneLayout;
  // R9: a pane that reports another pane's view matrix while having been built
  // from a different camera used the wrong matrix. (If both panes report the
  // same camera inputs too, showing one camera twice is a viewer choice and is
  // allowed.)
  for (std::uint32_t i = 0; i < paneCount && i < kPaneMaxPanes; ++i) {
    for (std::uint32_t j = i + 1u; j < paneCount && j < kPaneMaxPanes; ++j) {
      if (readings.panes[i].viewHash == readings.panes[j].viewHash
          && readings.panes[i].cameraFingerprint != readings.panes[j].cameraFingerprint) {
        mismatches |= kPaneMismatchSharedView;
      }
      // R10: two panes drawing from one camera state slot cannot have independent
      // cameras, whatever their matrices say.
      if (readings.panes[i].cameraSlot != kPaneCameraSlotUnset
          && readings.panes[i].cameraSlot == readings.panes[j].cameraSlot) {
        mismatches |= kPaneMismatchSharedCamera;
      }
    }
  }
  if (readings.hudPasses > 1u) mismatches |= kPaneMismatchHudPasses;
  if (readings.hudPasses != 0u && !readings.hudScissorApplied) mismatches |= kPaneMismatchHudScissor;
  return mismatches;
}

} // namespace tf2::native
