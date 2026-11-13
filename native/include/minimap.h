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

#include <cmath>
#include <cstddef>
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

// ============================================================================
// E9 (2026-11-13): what the PRODUCT HUD panel needs to draw a map.
//
// WHY THE TABLES ARE HERE. The product does not link src/minimap.cpp -- only the
// probe target does (native/cmake/tf2-probes-minimap.cmake) -- so the drawing half
// cannot call makeMinimapProjection/worldToMinimap at link time. The pure rules it
// does need are restated here as inline functions beside the declarations they
// mirror, and the gate recomputes every number independently from these tables, so
// a drift between the two copies is a red frame rather than a silent disagreement.
//
// WHY A COMPILED-IN TABLE. The bounds and the control points are the map's design,
// not a row the BSP exposes: BspMap carries no entity list at all (native/include/
// bsp_map.h stops at brush-model counters and fog), so at load time there is
// nothing to read a control point's origin from. The numbers are transcribed from
// the T135 measurement (D:/_shared/EXEC-2026-11-13/E/minimap/MINIMAP-DATA-REPORT.md,
// section 2 "recommended bounds" and section 3 control-point origins).
//
// A MAP THAT IS NOT LISTED DRAWS NOTHING. An unknown map rendered as an empty panel
// is the failure T65 recorded (a plausible-looking canvas nobody can tell from a
// map with no objectives); here the panel is absent and the reason is a reading.
// The map stem includes its version on purpose: koth_bagel_rc13 is a different BSP
// from koth_bagel_rc11 and its bounds were never measured.
// ============================================================================

struct MinimapControlPointRow {
  const char* name = "";
  int index = -1;   // the control_point entity index the demo stream carries
  int team = 0;     // 2=RED, 3=BLU, 0=neutral/undetermined (the entity values)
  float x = 0.0f;
  float y = 0.0f;
};

struct MinimapMapRow {
  const char* map = "";  // BSP stem: lower case, no directory, no ".bsp"
  float minX = 0.0f;
  float minY = 0.0f;
  float maxX = 0.0f;
  float maxY = 0.0f;
  const MinimapControlPointRow* points = nullptr;
  std::size_t pointCount = 0;
};

// --- cp_process_f12 -------------------------------------------------------
inline const MinimapControlPointRow kMinimapProcessPoints[] = {
  {"cp_blu1", 0, 3, -3540.0f, -1280.0f},
  {"cp_blu2", 1, 3, -1664.0f, -2176.0f},
  {"cp_3",    2, 0, 0.0f, 0.0f},
  {"cp_red2", 3, 2, 1664.0f, 2176.0f},
  {"cp_red1", 4, 2, 3540.0f, 1280.0f},
};

// --- cp_sunshine ---------------------------------------------------------
inline const MinimapControlPointRow kMinimapSunshinePoints[] = {
  {"cp_blu1", 0, 3, -7632.0f, 8940.0f},
  {"cp_blu2", 1, 3, -7704.0f, 6824.0f},
  {"cp_3",    2, 0, -5632.01f, 5248.0f},
  {"cp_red2", 3, 2, -3560.0f, 3672.0f},
  {"cp_red1", 4, 2, -3632.0f, 1556.0f},
};

// --- cp_gullywash_f9 -----------------------------------------------------
inline const MinimapControlPointRow kMinimapGullywashPoints[] = {
  {"cap_blue_1", 1, 3, 3768.0f, -757.0f},
  {"cap_blue_2", 2, 3, 1041.0f, -1612.0f},
  {"cap_center", 3, 0, 0.0f, 0.0f},
  {"cap_red_2",  4, 2, -1041.0f, 1612.0f},
  {"cap_red_1",  5, 2, -3766.2f, 758.0f},
};

// --- cp_sultry_b8a -------------------------------------------------------
inline const MinimapControlPointRow kMinimapSultryPoints[] = {
  {"cp_blu1", 0, 3, -2719.0f, 3892.0f},
  {"cp_blu2", 1, 3, -2536.0f, 796.0f},
  {"cp_mid",  2, 0, -32.0f, -16.0f},
  {"cp_red2", 3, 2, 2472.0f, -824.0f},
  {"cp_red1", 4, 2, 2657.0f, -3920.0f},
};

// --- koth_product_final / koth_bagel_rc11 --------------------------------
inline const MinimapControlPointRow kMinimapProductPoints[] = {
  {"control_point_1", 0, 0, -1536.0f, 0.0f},
};
inline const MinimapControlPointRow kMinimapBagelPoints[] = {
  {"cp_koth", 0, 0, 0.0f, 0.0f},
};

// --- pl_upward_f12 -------------------------------------------------------
// The four sequential caps are neutral in the map's own design (either team can
// own them at any moment), so the table records no team for them rather than
// inventing the colour a round happens to start with.
inline const MinimapControlPointRow kMinimapUpwardPoints[] = {
  {"cap_1", 1, 0, 1024.0f, -1536.0f},
  {"cap_2", 2, 0, 1536.0f, 1088.0f},
  {"cap_3", 3, 0, -1024.0f, 624.0f},
  {"cap_4", 4, 0, 512.0f, -384.0f},
};

inline const MinimapMapRow kMinimapMaps[] = {
  {"cp_process_f12", -5056.0f, -4608.0f, 5056.0f, 4608.0f, kMinimapProcessPoints, 5},
  {"cp_sunshine", -11008.0f, 128.0f, -256.0f, 10368.0f, kMinimapSunshinePoints, 5},
  {"cp_gullywash_f9", -5202.0f, -3990.0f, 5202.0f, 3990.0f, kMinimapGullywashPoints, 5},
  {"cp_sultry_b8a", -4832.0f, -6016.0f, 4768.0f, 5984.0f, kMinimapSultryPoints, 5},
  {"koth_product_final", -5426.0f, -4704.0f, 2304.0f, 4704.0f, kMinimapProductPoints, 1},
  {"koth_bagel_rc11", -4256.0f, -2816.0f, 4256.0f, 3072.0f, kMinimapBagelPoints, 1},
  {"pl_upward_f12", -3072.0f, -4096.0f, 4096.0f, 3072.0f, kMinimapUpwardPoints, 4},
};
inline constexpr std::size_t kMinimapMapCount =
  sizeof(kMinimapMaps) / sizeof(kMinimapMaps[0]);

// The demo names the map the way the server sent it: "cp_sunshine", sometimes a
// path such as "maps/cp_sunshine.bsp", in either case. The comparison is
// case-insensitive because one BSP really is spelled both ways across recordings.
inline std::string minimapNormalizeMapName(const std::string& mapName) {
  std::string name = mapName;
  const std::size_t slash = name.find_last_of("/\\");
  if (slash != std::string::npos) name = name.substr(slash + 1);
  for (char& character : name) {
    if (character >= 'A' && character <= 'Z') {
      character = static_cast<char>(character - 'A' + 'a');
    }
  }
  if (name.size() > 4 && name.compare(name.size() - 4, 4, ".bsp") == 0) {
    name.erase(name.size() - 4);
  }
  return name;
}

// nullptr when the map has no measured bounds: the caller must then draw nothing
// and publish a named reason, never fall back to a default rectangle.
inline const MinimapMapRow* minimapFindMapRow(const std::string& mapName) {
  const std::string name = minimapNormalizeMapName(mapName);
  for (std::size_t index = 0; index < kMinimapMapCount; ++index) {
    if (name == kMinimapMaps[index].map) return &kMinimapMaps[index];
  }
  return nullptr;
}

inline MinimapBounds minimapBoundsForRow(const MinimapMapRow& row, int pixelWidth,
                                         int pixelHeight) {
  MinimapBounds bounds;
  bounds.minX = row.minX;
  bounds.minY = row.minY;
  bounds.maxX = row.maxX;
  bounds.maxY = row.maxY;
  bounds.pixelWidth = pixelWidth;
  bounds.pixelHeight = pixelHeight;
  bounds.valid = row.maxX > row.minX && row.maxY > row.minY && pixelWidth > 0
    && pixelHeight > 0;
  return bounds;
}

// Equal-aspect, centred, unzoomed: exactly makeMinimapProjection(bounds, 1, 0, 0),
// restated as an inline because the product does not link src/minimap.cpp. The
// gate recomputes this from the table in Python and compares it with the published
// scale/offsetX/offsetY and with the pixels, so the two copies cannot drift apart
// without a red frame.
inline MinimapProjection minimapPanelProjection(const MinimapBounds& bounds) {
  MinimapProjection projection;
  if (!bounds.valid) return projection;
  const float spanX = bounds.maxX - bounds.minX;
  const float spanY = bounds.maxY - bounds.minY;
  const float width = static_cast<float>(bounds.pixelWidth);
  const float height = static_cast<float>(bounds.pixelHeight);
  const float scaleX = width / spanX;
  const float scaleY = height / spanY;
  const float scale = scaleX < scaleY ? scaleX : scaleY;
  projection.scale = scale;
  projection.offsetX = (width - spanX * scale) * 0.5f - bounds.minX * scale;
  projection.offsetY = (height - spanY * scale) * 0.5f + bounds.maxY * scale;
  projection.yFlip = true;
  projection.valid = scale > 0.0f;
  return projection;
}

inline MinimapPoint minimapPanelPoint(const MinimapBounds& bounds,
                                      const MinimapProjection& projection,
                                      float worldX, float worldY) {
  MinimapPoint point;
  if (!bounds.valid || !projection.valid) return point;
  point.px = worldX * projection.scale + projection.offsetX;
  if (projection.yFlip) {
    point.py = projection.offsetY - worldY * projection.scale;
  } else {
    point.py = projection.offsetY + worldY * projection.scale;
  }
  point.inside = point.px >= 0.0f && point.py >= 0.0f
    && point.px <= static_cast<float>(bounds.pixelWidth)
    && point.py <= static_cast<float>(bounds.pixelHeight);
  return point;
}

// TF2 yaw is counter-clockwise from +x with +y north; the panel is +x right and
// +y DOWN. A world heading (cos yaw, sin yaw) is therefore (cos yaw, -sin yaw) in
// panel space, whose screen bearing is 90 - yaw. The normalisation only keeps the
// published number inside [0,360).
inline float minimapScreenAngleDegrees(float yawDegrees) {
  float angle = 90.0f - yawDegrees;
  angle = std::fmod(angle, 360.0f);
  if (angle < 0.0f) angle += 360.0f;
  return angle;
}

// What main.cpp hands the panel once per frame. Nothing here comes from the
// demo's entity stream, which is why a paused frame draws the same panel as a
// playing one and the panel cannot lag the tick it is drawn on.
struct MinimapHudFrame {
  bool valid = false;      // the pose below is this frame's, not a held one
  std::string mapName;     // as the demo names it (normalised at lookup)
  float eyeX = 0.0f;
  float eyeY = 0.0f;
  float yawDegrees = 0.0f;
  std::int32_t tick = 0;
};

// Implemented in main_hud.cpp, the translation unit that also owns the HUD pass.
// A process that never calls it (one without --hud-minimap) draws no panel.
void minimapHudSetFrame(const MinimapHudFrame& frame);

// True once any frame has been pushed. The HUD checks this before it does a single
// unit of projection work, so a run without the switch costs nothing and cannot
// print a minimap reading that a gate might mistake for evidence.
bool minimapHudFramePushed();

// The frame as pushed. Read-only, valid until the next set; the HUD copies the two
// numbers it needs and never holds the reference across a publish.
const MinimapHudFrame& minimapHudFrameRef();

}  // namespace tf2::native
