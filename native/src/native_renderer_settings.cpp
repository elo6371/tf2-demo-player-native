// Render settings: preset mapping and normalisation of the quality knobs.
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


RenderSettings RenderSettings::fromPreset(QualityPreset preset) {
  RenderSettings settings{};
  settings.preset = preset;
  if (preset == QualityPreset::Standard) {
    settings.textureMipBias = 0;
    settings.antiAliasingQuality = 2;
    settings.anisotropicLevel = 8;
    settings.dynamicLighting = true;
    settings.shadows = true;
    settings.bumpMapping = true;
    settings.specular = true;
    settings.waterReflection = true;
    settings.waterRefraction = true;
    settings.waterSurface = true;
    settings.skybox = true;
    settings.modelLod = 0;
  }
  settings.normalize();
  return settings;
}

void RenderSettings::normalize() {
  textureMipBias = clampValue(textureMipBias, -2, 4);
  antiAliasingQuality = clampValue(antiAliasingQuality, 0, 8);
  anisotropicLevel = clampValue(anisotropicLevel, 1, 16);
  modelLod = clampValue(modelLod, 0, 8);
  renderScale = clampValue(renderScale, 0.5f, 2.0f);
  viewModelFov = clampViewModelFov(viewModelFov);
  // T24 (2026-10-19): this block used to force the Performance preset's shading
  // flags to false here as well as in fromPreset().
  //
  // Honest scope, because the capture environment hides it: the reference
  // captures run with preset=Standard (the ini stores preset=1 while the
  // shading flags are never persisted and stay false -- the T17 contradiction
  // noted in settings_store.cpp), so this block did NOT fire for them and
  // removing it changed nothing about A..F. It does fire for a caller that
  // passes --quality performance, and that is where it killed the measurement:
  // --world-specular on set the field and this function cleared it again, so the
  // switch and its absence produced the same frame.
  //
  // Removing the duplication is safe because the flags have exactly two other
  // writers and both already produce Performance's values: fromPreset()
  // zero-initialises with `RenderSettings settings{}` and only assigns inside
  // its Standard branch, and loadSettings() starts from `PersistentSettings{}`
  // and reads only preset/renderScale/viewModelFov/vsync from the ini. The one
  // caller that sets them outside fromPreset is main.cpp's three
  // single-semantic switches. Measured: --quality performance still reads
  // 0/0/0, and --quality performance --world-specular on reads specular=1.
  //
  // `dynamicLighting`, `specular` and `bumpMapping` are likewise read at the
  // draw without the `preset == Standard` conjunct, for the same reason: the
  // flag is the statement, and the caller that set it is what is measured.
}

} // namespace tf2::native
