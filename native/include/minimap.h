#pragma once

// ============================================================================
// T69 minimap -- the PURE half: projection, round trip, hit test, panel state.
//
// WHY THIS FILE IS SEPARATE FROM THE RENDERER
//   The minimap has two halves with very different test costs. Projection math,
//   the world<->pixel round trip, "which player did I click" and the collapsed/
//   zoom state machine are pure values: they need no device, no demo and no
//   window, so a probe can pin them in milliseconds. The drawing half (top-right
//   panel, overview bitmap, team dots, hover labels) needs T67's HUD primitive
//   renderer and is deliberately NOT in this file. Keeping the pure half in its
//   own translation unit means the pixel-level feature can land later without
//   re-testing this math through a device, and a probe can fail on a wrong
//   formula without a GPU in the loop.
//
// THE COORDINATE CONVENTION, STATED ONCE
//   World space is the demo's: +x east, +y north, z up (only x/y are used here).
//   Pixel space is the panel's: +x right, +y DOWN (the screen convention). The
//   y flip is therefore not an accident of one formula; it is the contract, and
//   it is a field in MinimapProjection so a caller cannot forget which one it
//   got. worldToMinimap and minimapToWorld are exact inverses to within float
//   rounding; minimapToWorld returns false rather than a silent 0 on an
//   unusable projection.
//
// WHAT "UNAVAILABLE" MEANS
//   A map whose BSP is missing, whose bounds are degenerate or whose pixel size
//   is zero cannot produce a map. The failure mode this type is built to prevent
//   is the one T65 found in the data: an unavailable map drawn as a plausible-
//   looking empty canvas. So validity is explicit (MinimapBounds::valid,
//   MinimapProjection::valid) and the panel carries a non-empty named reason
//   when it cannot draw; a caller that ignores both has to ignore a field, not
//   infer from an empty bitmap.
//
// WHAT IS NOT HERE
//   No renderer types, no <d3d11.h>, no window handles: this header must stay
//   buildable by a console probe on a machine with no device.

#include <cstdint>
#include <string>
#include <vector>

namespace tf2::native {

// The world rectangle the map covers and the bitmap it is drawn into.
// valid=false means "no map": do not derive a projection from these numbers.
struct MinimapBounds {
  float minX = 0.0f;
  float minY = 0.0f;
  float maxX = 0.0f;
  float maxY = 0.0f;
  int pixelWidth = 0;
  int pixelHeight = 0;
  bool valid = false;
};

// An affine world->pixel transform. scale is pixels per world unit and is the
// SAME on both axes (equal aspect: a map stretched on one axis would put every
// player dot in the wrong place even though each axis "looks" scaled).
// offsetX/offsetY include the centering term and the caller's pan, in pixels.
// yFlip=true is the normal case (world +y up, pixel +y down).
struct MinimapProjection {
  float scale = 0.0f;
  float offsetX = 0.0f;
  float offsetY = 0.0f;
  bool yFlip = true;
  bool valid = false;
};

struct MinimapPoint {
  float px = 0.0f;
  float py = 0.0f;
  bool inside = false;  // inside the pixel rectangle [0,w] x [0,h]
};

// One player as the minimap sees it. visible is the MINIMAP's visibility (not
// on the map / hidden by the map's own rules), not the entity's PVS presence;
// hidden players must not be hit-testable. index is the stable tie-break key.
struct MinimapPlayerState {
  std::uint16_t index = 0;
  int team = 0;
  int health = 0;
  bool alive = false;
  bool visible = false;
  float x = 0.0f;
  float y = 0.0f;
  std::string name;
};

// Equal-aspect, centered projection. zoom > 1 magnifies around the panel centre;
// panX/panY translate the map in pixels (positive = right/down). Returns
// valid=false for unusable bounds or zoom <= 0.
MinimapProjection makeMinimapProjection(const MinimapBounds& bounds, float zoom,
                                        float panX, float panY);

MinimapPoint worldToMinimap(const MinimapBounds& bounds,
                            const MinimapProjection& projection, float worldX,
                            float worldY);

// Exact inverse of worldToMinimap. Returns false (and leaves the outputs
// untouched) when the projection is unusable, instead of writing a plausible 0.
bool minimapToWorld(const MinimapBounds& bounds,
                    const MinimapProjection& projection, float px, float py,
                    float& worldX, float& worldY);

// Nearest VISIBLE player within radiusPx, or -1. Ties (equal distance, to float
// exactness) resolve to the smaller index, not to vector order, so the answer
// cannot change when the caller sorts its players differently.
int minimapHitTest(const std::vector<MinimapPlayerState>& players,
                   const MinimapBounds& bounds,
                   const MinimapProjection& projection, float px, float py,
                   float radiusPx);

// A stable, non-empty reason when these bounds cannot be drawn; empty when they
// can. mapName is included in the reason so the caller can say WHICH map.
std::string minimapUnavailableReason(const MinimapBounds& bounds,
                                     const std::string& mapName);

// The panel's non-render state: collapsed by default (T69: the map starts
// folded), zoom, pan, and the counters the gate reads. Deliberately no drawing,
// no window, no clock.
class MinimapPanel {
 public:
  void setMap(const MinimapBounds& bounds, std::string mapName);
  bool available() const;
  const std::string& unavailableReason() const { return unavailableReason_; }

  bool collapsed() const { return collapsed_; }
  void expand();
  void collapse();
  void toggle();

  float zoom() const { return zoom_; }
  float panX() const { return panX_; }
  float panY() const { return panY_; }
  void setZoom(float zoom);
  void setPan(float panX, float panY);
  void resetView();

  MinimapProjection projection() const;

  // Calls minimapHitTest and records hitTests/hits/misses/lastHitIndex.
  int hitTest(const std::vector<MinimapPlayerState>& players, float px, float py,
              float radiusPx);

  std::uint64_t expandCount() const { return expandCount_; }
  std::uint64_t collapseCount() const { return collapseCount_; }
  std::uint64_t zoomChanges() const { return zoomChanges_; }
  std::uint64_t hitTests() const { return hitTests_; }
  std::uint64_t hits() const { return hits_; }
  std::uint64_t misses() const { return misses_; }
  int lastHitIndex() const { return lastHitIndex_; }

  // One line of key=value pairs, stable order, for the gate to grep.
  std::string stateLine() const;

 private:
  MinimapBounds bounds_;
  std::string mapName_;
  std::string unavailableReason_;
  bool collapsed_ = true;
  float zoom_ = 1.0f;
  float panX_ = 0.0f;
  float panY_ = 0.0f;
  std::uint64_t expandCount_ = 0;
  std::uint64_t collapseCount_ = 0;
  std::uint64_t zoomChanges_ = 0;
  std::uint64_t hitTests_ = 0;
  std::uint64_t hits_ = 0;
  std::uint64_t misses_ = 0;
  int lastHitIndex_ = -1;
};

}  // namespace tf2::native
