#include "minimap.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace tf2::native {
namespace {

// The one definition of "these numbers can produce a map", shared by the
// projection builder, both conversions and the hit test so none of them can
// disagree about whether the map exists.
bool usableBounds(const MinimapBounds& bounds) {
  if (!bounds.valid) {
    return false;
  }
  if (!(bounds.pixelWidth > 0) || !(bounds.pixelHeight > 0)) {
    return false;
  }
  const float spanX = bounds.maxX - bounds.minX;
  const float spanY = bounds.maxY - bounds.minY;
  return spanX > 0.0f && spanY > 0.0f && std::isfinite(spanX) && std::isfinite(spanY);
}

}  // namespace

MinimapProjection makeMinimapProjection(const MinimapBounds& bounds, float zoom,
                                        float panX, float panY) {
  MinimapProjection projection;
  if (!usableBounds(bounds) || !(zoom > 0.0f) || !std::isfinite(zoom) ||
      !std::isfinite(panX) || !std::isfinite(panY)) {
    return projection;
  }
  const float spanX = bounds.maxX - bounds.minX;
  const float spanY = bounds.maxY - bounds.minY;
  const float scaleX = static_cast<float>(bounds.pixelWidth) / spanX;
  const float scaleY = static_cast<float>(bounds.pixelHeight) / spanY;
  // Equal aspect is part of the contract, not a convenience: the smaller of the
  // two fits the whole world rectangle inside the bitmap, and the unused axis is
  // split evenly by the centering term below.
  const float scale = std::min(scaleX, scaleY) * zoom;
  if (!(scale > 0.0f) || !std::isfinite(scale)) {
    return projection;
  }
  const float contentWidth = spanX * scale;
  const float contentHeight = spanY * scale;
  projection.scale = scale;
  projection.offsetX = (static_cast<float>(bounds.pixelWidth) - contentWidth) * 0.5f -
                       bounds.minX * scale + panX;
  // yFlip=true: py = offsetY - worldY*scale, so the world's maximum y lands on
  // the bitmap's top edge. Stated as a formula rather than folded into the call
  // sites so minimapToWorld can be its exact inverse.
  projection.offsetY = (static_cast<float>(bounds.pixelHeight) - contentHeight) * 0.5f +
                       bounds.maxY * scale + panY;
  projection.yFlip = true;
  projection.valid = true;
  return projection;
}

MinimapPoint worldToMinimap(const MinimapBounds& bounds,
                            const MinimapProjection& projection, float worldX,
                            float worldY) {
  MinimapPoint point;
  if (!projection.valid || !usableBounds(bounds)) {
    return point;
  }
  point.px = worldX * projection.scale + projection.offsetX;
  point.py = projection.yFlip ? projection.offsetY - worldY * projection.scale
                              : projection.offsetY + worldY * projection.scale;
  point.inside = point.px >= 0.0f &&
                 point.px <= static_cast<float>(bounds.pixelWidth) &&
                 point.py >= 0.0f &&
                 point.py <= static_cast<float>(bounds.pixelHeight);
  return point;
}

bool minimapToWorld(const MinimapBounds& bounds,
                    const MinimapProjection& projection, float px, float py,
                    float& worldX, float& worldY) {
  if (!projection.valid || !usableBounds(bounds) || !(projection.scale > 0.0f)) {
    return false;
  }
  worldX = (px - projection.offsetX) / projection.scale;
  worldY = projection.yFlip ? (projection.offsetY - py) / projection.scale
                            : (py - projection.offsetY) / projection.scale;
  return true;
}

int minimapHitTest(const std::vector<MinimapPlayerState>& players,
                   const MinimapBounds& bounds,
                   const MinimapProjection& projection, float px, float py,
                   float radiusPx) {
  if (!projection.valid || !usableBounds(bounds) || !(radiusPx >= 0.0f)) {
    return -1;
  }
  // A tie is "the same distance to float exactness". The tie-break is the
  // player's stable index, NOT the order the caller happened to store its
  // vector in: a minimap that picks a different player when the roster is
  // sorted is a minimap whose click target depends on bookkeeping.
  constexpr float kTieEpsilon = 1.0e-4f;
  int bestIndex = -1;
  float bestDistance = 0.0f;
  for (const MinimapPlayerState& player : players) {
    if (!player.visible) {
      continue;
    }
    const MinimapPoint point =
        worldToMinimap(bounds, projection, player.x, player.y);
    const float dx = point.px - px;
    const float dy = point.py - py;
    const float distance = std::sqrt(dx * dx + dy * dy);
    if (distance > radiusPx) {
      continue;
    }
    const bool closer = distance < bestDistance - kTieEpsilon;
    const bool tieWithSmallerIndex =
        std::fabs(distance - bestDistance) <= kTieEpsilon &&
        static_cast<int>(player.index) < bestIndex;
    if (bestIndex < 0 || closer || tieWithSmallerIndex) {
      bestIndex = static_cast<int>(player.index);
      bestDistance = distance;
    }
  }
  return bestIndex;
}

std::string minimapUnavailableReason(const MinimapBounds& bounds,
                                     const std::string& mapName) {
  const std::string name = mapName.empty() ? std::string("unnamed-map") : mapName;
  if (!bounds.valid) {
    return name + ":bounds-invalid";
  }
  const float spanX = bounds.maxX - bounds.minX;
  const float spanY = bounds.maxY - bounds.minY;
  if (!(spanX > 0.0f) || !(spanY > 0.0f) || !std::isfinite(spanX) ||
      !std::isfinite(spanY)) {
    return name + ":world-span-nonpositive";
  }
  if (!(bounds.pixelWidth > 0) || !(bounds.pixelHeight > 0)) {
    return name + ":pixel-size-nonpositive";
  }
  return std::string();
}

void MinimapPanel::setMap(const MinimapBounds& bounds, std::string mapName) {
  bounds_ = bounds;
  mapName_ = std::move(mapName);
  unavailableReason_ = minimapUnavailableReason(bounds_, mapName_);
}

bool MinimapPanel::available() const {
  return unavailableReason_.empty() && projection().valid;
}

void MinimapPanel::expand() {
  if (collapsed_) {
    collapsed_ = false;
    ++expandCount_;
  }
}

void MinimapPanel::collapse() {
  if (!collapsed_) {
    collapsed_ = true;
    ++collapseCount_;
  }
}

void MinimapPanel::toggle() {
  if (collapsed_) {
    expand();
  } else {
    collapse();
  }
}

void MinimapPanel::setZoom(float zoom) {
  if (zoom > 0.0f && std::isfinite(zoom) && zoom != zoom_) {
    zoom_ = zoom;
    ++zoomChanges_;
  }
}

void MinimapPanel::setPan(float panX, float panY) {
  if (std::isfinite(panX) && std::isfinite(panY)) {
    panX_ = panX;
    panY_ = panY;
  }
}

void MinimapPanel::resetView() {
  const bool zoomChanged = zoom_ != 1.0f;
  zoom_ = 1.0f;
  panX_ = 0.0f;
  panY_ = 0.0f;
  if (zoomChanged) {
    ++zoomChanges_;
  }
}

MinimapProjection MinimapPanel::projection() const {
  return makeMinimapProjection(bounds_, zoom_, panX_, panY_);
}

int MinimapPanel::hitTest(const std::vector<MinimapPlayerState>& players,
                          float px, float py, float radiusPx) {
  ++hitTests_;
  const int result = minimapHitTest(players, bounds_, projection(), px, py, radiusPx);
  if (result >= 0) {
    ++hits_;
    lastHitIndex_ = result;
  } else {
    ++misses_;
    lastHitIndex_ = -1;
  }
  return result;
}

std::string MinimapPanel::stateLine() const {
  char buffer[640];
  std::snprintf(
      buffer, sizeof(buffer),
      "collapsed=%d zoom=%.6f panX=%.6f panY=%.6f expandCount=%llu "
      "collapseCount=%llu zoomChanges=%llu hitTests=%llu hits=%llu misses=%llu "
      "lastHitIndex=%d available=%d reason=%s",
      collapsed_ ? 1 : 0, static_cast<double>(zoom_), static_cast<double>(panX_),
      static_cast<double>(panY_),
      static_cast<unsigned long long>(expandCount_),
      static_cast<unsigned long long>(collapseCount_),
      static_cast<unsigned long long>(zoomChanges_),
      static_cast<unsigned long long>(hitTests_),
      static_cast<unsigned long long>(hits_),
      static_cast<unsigned long long>(misses_), lastHitIndex_,
      available() ? 1 : 0,
      unavailableReason_.empty() ? "-" : unavailableReason_.c_str());
  return std::string(buffer);
}

}  // namespace tf2::native
