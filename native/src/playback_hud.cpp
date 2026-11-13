#include "playback_hud.h"

#include <cmath>
#include <cstdint>
#include <limits>
#include <string>

namespace tf2::native {
namespace {

const EntityPropertyValue* findNamed(const EntityState& state, const char* key) {
  const std::string token = key;
  const EntityPropertyValue* exact = nullptr;
  const EntityPropertyValue* dotted = nullptr;
  for (const auto& entry : state.properties) {
    if (entry.first == token) {
      exact = &entry.second;
    } else if (entry.first.size() > token.size()
        && entry.first[entry.first.size() - token.size() - 1] == '.'
        && entry.first.compare(entry.first.size() - token.size(), token.size(), token) == 0) {
      dotted = &entry.second;
    }
  }
  return exact ? exact : dotted;
}

bool readIntProperty(const EntityState& state, const char* key, std::int32_t& out) {
  const EntityPropertyValue* value = findNamed(state, key);
  if (!value || value->type != SendPropType::Int) return false;
  if (value->intValue < static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::min())
      || value->intValue > static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::max())) return false;
  out = static_cast<std::int32_t>(value->intValue);
  return true;
}

} // namespace

PlaybackHudState readPlaybackHud(const std::vector<EntityState>& states, std::int32_t viewEntity) {
  PlaybackHudState hud;
  if (viewEntity < 0 || static_cast<std::size_t>(viewEntity) >= states.size()) return hud;
  const EntityState& state = states[static_cast<std::size_t>(viewEntity)];
  hud.healthKnown = readIntProperty(state, "m_iHealth", hud.health);
  hud.teamKnown = readIntProperty(state, "m_iTeamNum", hud.team);
  hud.classKnown = readIntProperty(state, "m_iClass", hud.playerClass);
  hud.clipKnown = readIntProperty(state, "m_iClip1", hud.clip);
  if (const EntityPropertyValue* charge = findNamed(state, "m_flChargeLevel")) {
    if (charge->type == SendPropType::Float && std::isfinite(charge->x)) {
      hud.uberKnown = true;
      hud.uberCharge = charge->x;
    }
  }
  return hud;
}

} // namespace tf2::native
