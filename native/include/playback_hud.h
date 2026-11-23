#pragma once

#include "demo_header.h"

#include <cstdint>
#include <vector>

namespace tf2::native {

// Values read from the viewed entity. Unknown stays false when that property
// is absent. This is a readout, not a drawn HUD panel.
struct PlaybackHudState {
  bool healthKnown = false;
  std::int32_t health = 0;
  bool teamKnown = false;
  std::int32_t team = 0;
  bool classKnown = false;
  std::int32_t playerClass = 0;
  bool clipKnown = false;
  std::int32_t clip = 0;
  bool uberKnown = false;
  float uberCharge = 0.0f;
};

PlaybackHudState readPlaybackHud(const std::vector<EntityState>& states, std::int32_t viewEntity);

} // namespace tf2::native
