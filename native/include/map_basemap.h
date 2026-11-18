#pragma once

// E11 (2026-11-17): the demos.tf top-down basemap package.
//
// SOURCE OF TRUTH
//   E:/demo测试/demos_tf_maps by default (basemapRoot()), read at run time:
//     images/<key>.png   original-resolution top-down renders (up to 2099 px)
//     boundaries.json    world-space boundary_min/boundary_max per key
//     manifest.json      what the package itself says the files are
//   Nothing is embedded in the binary; the 46 MB of art stays on disk.
//
// ADDRESSING AND SCALE
//   The world->image mapping is demos.tf's, copied from mapview.js rather than
//   invented here:
//     worldWidth  = boundary_max.x - boundary_min.x
//     worldHeight = boundary_max.y - boundary_min.y
//     scaledX     = (x - boundary_min.x) / worldWidth  * width
//     scaledY     = (worldHeight - (y - boundary_min.y)) / worldHeight * height
//   i.e. world +Y points UP on the image. The facing arrow uses Source engine
//   yaw directly: the direction vector is (cos yaw, -sin yaw) in screen space,
//   which is the same rotation mapview.js applies as "270 - angle" when it
//   rotates a sprite that is drawn pointing up.
//
// NAME RESOLUTION
//   mapview.js resolveMapName is reproduced step for step (see
//   basemapResolveKey), including the cp_gullywash_f* alias, so a demo header
//   name finds the same package key the viewer's own code would pick. A map
//   outside the package resolves to "none" and says so.
//
// EXACT VERSUS LEGACY
//   Some demo map names only have an OLDER version of the same map in the
//   package (cp_gullywash_f9 -> f6, koth_bagel_rc11 -> rc4, ...). Those images
//   and their boundaries describe the old layout, so dots drift and the panel
//   must say so: `exact=false` in the trace and a visible LEGACY badge. The
//   table below is curated from the user-supplied coverage list; anything not in
//   it is exact ONLY when the package key equals the demo header name verbatim.

#include <cstdint>
#include <string>

namespace tf2::native {

class Renderer;

struct BasemapBounds {
  double minX = 0.0;
  double minY = 0.0;
  double maxX = 0.0;
  double maxY = 0.0;
  bool valid = false;

  double width() const { return maxX - minX; }
  double height() const { return maxY - minY; }
};

// One resolved+decoded+uploaded basemap. Every field is a reading the trace
// prints; `reason` is never empty, so "no basemap" always has a cause.
struct BasemapView {
  bool ready = false;
  std::string reason = "not-loaded";
  std::string requestName;
  std::string sourceKey;
  std::string resolveRule = "none";
  bool exact = false;
  std::string imagePath;
  int imageWidth = 0;
  int imageHeight = 0;
  double decodeMs = 0.0;
  double uploadMs = 0.0;
  BasemapBounds bounds;
  std::string format = "none";     // GPU texture format token
  std::string sampler = "none";    // "linear-mip" when the mip chain is bound
  unsigned mipLevels = 0;
  bool textureReady = false;
  // |imageAspect - boundaryAspect| / boundaryAspect * 100. The two are meant to
  // agree (47 of 52 package images agree to < 1%); a large deviation means the
  // image and the boundary describe different shapes and the trace says so.
  double aspectDeviationPercent = 0.0;
};

void basemapSetRoot(const std::string& root);
const std::string& basemapRoot();

// --overview-image <path>: one explicit file override. It still needs a
// boundary for the current map, otherwise it cannot be placed and the panel
// reports "no-boundary" instead of guessing a rectangle.
void basemapSetImageOverride(const std::string& path);
const std::string& basemapImageOverride();

// mapview.js resolveMapName, verbatim: alias, exact, trimmed, reverse-trimmed,
// reverse-trimmed-2, then unchanged. `rule` names which step matched.
std::string basemapResolveKey(const std::string& mapName, bool* exact, std::string* rule);

// Resolves, decodes (once per map) and uploads the image to the renderer's HUD
// image slot (re-uploading after a device reset). `renderer` may be null, in
// which case the pixels are decoded and only the texture step is skipped.
BasemapView basemapLoad(const std::string& mapName, Renderer* renderer);

}  // namespace tf2::native
