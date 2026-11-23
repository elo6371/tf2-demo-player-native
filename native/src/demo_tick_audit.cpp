#include "demo_tick_audit.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <map>
#include <set>
#include <utility>

namespace tf2::native {
namespace {

constexpr std::int32_t kEntityIndexMask = 0x7ff;
constexpr std::size_t kMaxPendingEvents = 4096;
constexpr std::size_t kMaxStoredHits = 4096;
constexpr std::size_t kMaxHitLines = 16;

struct WeaponProfile {
  const char* className;
  std::int32_t floorTicks;
  bool intervalCheck;
};

// The floor is the shortest interval the game itself permits between two
// primary attacks for that weapon. Values are ticks at 66.67 tps.
const WeaponProfile kWeaponProfiles[] = {
    {"CTFScatterGun", 37, true},
    {"CTFSodaPopper", 18, true},
    {"CTFPEPBrawlerBlaster", 33, true},
    {"CTFPistol_ScoutPrimary", 14, true},
    {"CTFPistol_Scout", 8, false},
    {"CTFPistol", 8, false},
    {"CTFShotgun_Soldier", 36, true},
    {"CTFShotgun_Pyro", 36, true},
    {"CTFShotgun", 36, true},
    {"CTFShotgun_Revenge", 36, true},
    {"CTFShotgun_HWG", 31, true},
};

const WeaponProfile* findWeaponProfile(const std::string& className) {
  for (const auto& profile : kWeaponProfiles) {
    if (className == profile.className) return &profile;
  }
  return nullptr;
}

bool leafMatches(const std::string& name, const char* suffix) {
  const std::size_t suffixLength = std::strlen(suffix);
  if (name.size() == suffixLength) return name == suffix;
  if (name.size() < suffixLength) return false;
  return name.compare(name.size() - suffixLength, suffixLength, suffix) == 0;
}

const EntityPropertyValue* findLeaf(const EntityState& state, const char* suffix) {
  const EntityPropertyValue* best = nullptr;
  for (const auto& entry : state.properties) {
    if (!leafMatches(entry.first, suffix)) continue;
    if (best == nullptr || entry.second.lastWriteTick > best->lastWriteTick) best = &entry.second;
  }
  return best;
}

std::int64_t leafInt(const EntityState& state, const char* suffix, bool& found) {
  const EntityPropertyValue* value = findLeaf(state, suffix);
  found = value != nullptr;
  return found ? value->intValue : 0;
}

std::string joinNames(const std::vector<std::string>& names) {
  std::string joined;
  for (const auto& name : names) {
    if (!joined.empty()) joined += ",";
    joined += name;
  }
  return joined;
}

std::string formatDouble(double value, int precision) {
  char buffer[64] = {};
  std::snprintf(buffer, sizeof(buffer), "%.*f", precision, value);
  return std::string(buffer);
}

}  // namespace

const char* demoTickAuditLevelName(DemoTickAuditLevel level) {
  switch (level) {
    case DemoTickAuditLevel::None: return "None";
    case DemoTickAuditLevel::Info: return "Info";
    case DemoTickAuditLevel::Warning: return "Warning";
    case DemoTickAuditLevel::Edited: return "Edited";
  }
  return "Unknown";
}

DemoTickAuditCapture::DemoTickAuditCapture(const DemoTickAuditThresholds& thresholds)
    : thresholds_(thresholds) {}

const char* DemoTickAuditCapture::classNameForClassId(const DemoNetworkSummary& summary,
    std::int32_t classId) const {
  if (classId < 0) return nullptr;
  const auto index = static_cast<std::size_t>(classId);
  if (index < summary.serverClassNames.size() && !summary.serverClassNames[index].empty()) {
    return summary.serverClassNames[index].c_str();
  }
  for (const auto& schema : summary.serverClassSchemas) {
    if (schema.id == index) return schema.name.c_str();
  }
  return nullptr;
}

bool DemoTickAuditCapture::isPlayerClass(const char* className) {
  if (className == nullptr) return false;
  return std::strncmp(className, "CTFPlayer", 9) == 0;
}

std::int64_t DemoTickAuditCapture::decodeSimValue(const EntityPropertyValue& value,
    std::int32_t serverTick, std::int32_t entityIndex) const {
  // m_flSimulationTime travels as an 8-bit offset from a base derived from the
  // server tick and the entity index (the engine's RecvProxy_SimulationTime).
  // Anything that is not that encoding is treated as a plain value so a
  // differently-encoded property cannot be silently reinterpreted.
  if (value.type != SendPropType::Int) {
    return static_cast<std::int64_t>(std::llround(static_cast<double>(value.x) * thresholds_.tickRate));
  }
  const std::int64_t raw = value.intValue & 0xff;
  if (serverTick <= 0) return raw;
  const std::int64_t indexOffset = static_cast<std::int64_t>(entityIndex % 32);
  std::int64_t base = 100LL * ((static_cast<std::int64_t>(serverTick) - indexOffset) / 100LL);
  std::int64_t tick = base + raw;
  while (tick < static_cast<std::int64_t>(serverTick) - 127) tick += 256;
  while (tick > static_cast<std::int64_t>(serverTick) + 127) tick -= 256;
  if (tick - static_cast<std::int64_t>(serverTick) > 50) tick -= 100;
  return tick;
}

void DemoTickAuditCapture::beginPacket(std::int32_t entryTick, std::uint8_t command,
    std::int32_t serverTickInForce) {
  entryTick_ = entryTick;
  command_ = command;
  packetServerTick_ = serverTickInForce;
  packetOrdinal_ = packets_.size();
  inPacket_ = true;
  pendingPlayers_.clear();
}

void DemoTickAuditCapture::observeEntityProperty(const DemoNetworkSummary& summary,
    std::int32_t serverTick, std::int32_t entityIndex, std::int32_t classId,
    const char* propName, const EntityPropertyValue& value) {
  // Before the audit's own early returns: the census does not depend on them.
  if (viewModelCensusEnabled_) {
    observeViewModelProperty(summary, serverTick, entityIndex, classId, propName, value);
  }
  if (fxCensusEnabled_) {
    observeFxProperty(summary, serverTick, entityIndex, classId, propName, value);
  }
  if (motionCensusEnabled_) {
    observeMotionProperty(summary, serverTick, entityIndex, classId, propName, value);
  }
  if (!thresholds_.playerEnabled) return;
  if (propName == nullptr || std::strcmp(propName, "m_flSimulationTime") != 0) return;
  if (!isPlayerClass(classNameForClassId(summary, classId))) return;
  if (entityIndex < 0 || entityIndex >= 2048) return;
  SimSample sample;
  sample.serverTick = serverTick;
  sample.simTick = decodeSimValue(value, serverTick, entityIndex);
  auto& history = simHistory_[entityIndex];
  if (!history.empty() && history.back().serverTick == sample.serverTick
      && history.back().simTick == sample.simTick) {
    return;
  }
  history.push_back(sample);
  ++simSamples_;
  const std::int32_t cutoff = sample.serverTick - std::max<std::int32_t>(thresholds_.simHistoryTicks, 8);
  while (!history.empty() && history.front().serverTick < cutoff) history.pop_front();
  if (history.size() > 64u) history.pop_front();
}

void DemoTickAuditCapture::observeViewModelProperty(const DemoNetworkSummary& summary,
    std::int32_t serverTick, std::int32_t entityIndex, std::int32_t classId,
    const char* propName, const EntityPropertyValue& value) {
  if (propName == nullptr || entityIndex < 0) return;
  const bool isSequence = std::strcmp(propName, "m_nSequence") == 0;
  const bool isParity = std::strcmp(propName, "m_nAnimationParity") == 0;
  const bool isModel = std::strcmp(propName, "m_nModelIndex") == 0;
  const bool isCycle = std::strcmp(propName, "m_flCycle") == 0;
  const bool isRate = std::strcmp(propName, "m_flPlaybackRate") == 0;
  if (!isSequence && !isParity && !isModel && !isCycle && !isRate) return;
  const char* className = classNameForClassId(summary, classId);
  // CTFViewModel / CBaseViewModel / CPredictedViewModel: the first-person entity
  // that owns the arms' sequence. A class name is the only identity available
  // here, and "ViewModel" is the one substring every variant carries.
  if (className == nullptr || std::strstr(className, "ViewModel") == nullptr) return;
  DemoViewModelCensusEntity& entry = viewModelCensus_[entityIndex];
  if (entry.entityIndex < 0) {
    entry.entityIndex = entityIndex;
    entry.className = className;
    entry.firstTick = serverTick;
  }
  entry.lastTick = serverTick;
  // Float properties carry their value in x (EntityPropertyValue). Only the
  // integer ones (sequence, parity, model index) are compared; the two float ones
  // are counted, not read.
  const std::int64_t number = value.type == SendPropType::Float
    ? static_cast<std::int64_t>(value.x) : value.intValue;
  if (isSequence) {
    ++entry.sequenceWrites;
    ++entry.sequenceWritesByValue[number];
    if (entry.sequenceFirstTick.find(number) == entry.sequenceFirstTick.end()) {
      entry.sequenceFirstTick[number] = serverTick;
    }
    if (entry.haveSequence && number != entry.lastSequence) {
      ++entry.sequenceChanges;
      if (entry.transitions.size() < 64u) {
        entry.transitions.push_back({serverTick, entry.lastSequence, number});
      }
    }
    entry.haveSequence = true;
    entry.lastSequence = number;
  } else if (isParity) {
    ++entry.parityWrites;
    if (entry.haveParity && number != entry.lastParity) ++entry.parityChanges;
    entry.haveParity = true;
    entry.lastParity = number;
  } else if (isModel) {
    if (entry.haveModel && number != entry.lastModel) ++entry.modelChanges;
    entry.haveModel = true;
    entry.lastModel = number;
  } else if (isCycle) {
    ++entry.cycleWrites;
  } else {
    ++entry.playbackRateWrites;
  }
}

void DemoTickAuditCapture::observeFxProperty(const DemoNetworkSummary& summary,
    std::int32_t serverTick, std::int32_t entityIndex, std::int32_t classId,
    const char* propName, const EntityPropertyValue& value) {
  if (propName == nullptr || entityIndex < 0) return;
  const char* className = classNameForClassId(summary, classId);
  if (className == nullptr) return;
  const bool medigun = std::strcmp(className, "CWeaponMedigun") == 0;
  const bool flamethrower = std::strcmp(className, "CTFFlameThrower") == 0;
  const bool player = std::strcmp(className, "CTFPlayer") == 0;
  if (!medigun && !flamethrower && !player) return;
  static const char* const kMedigunProps[] = {"m_bHealing", "m_hHealingTarget", "m_bChargeRelease",
    "m_flChargeLevel", "m_iItemDefinitionIndex", "m_hOwnerEntity", "m_hOwner", "m_nChargeResistType"};
  static const char* const kFlameProps[] = {"m_iWeaponState", "m_bCritFire", "m_bHitTarget",
    "m_iItemDefinitionIndex", "m_hOwnerEntity"};
  static const char* const kPlayerProps[] = {"m_nPlayerCond", "m_nPlayerCondEx", "m_nPlayerCondEx2",
    "m_nPlayerCondEx3", "m_nPlayerCondEx4", "_condition_bits"};
  const char* const* list = medigun ? kMedigunProps : (flamethrower ? kFlameProps : kPlayerProps);
  const std::size_t count = medigun ? sizeof(kMedigunProps) / sizeof(kMedigunProps[0])
    : (flamethrower ? sizeof(kFlameProps) / sizeof(kFlameProps[0]) : sizeof(kPlayerProps) / sizeof(kPlayerProps[0]));
  std::size_t which = count;
  for (std::size_t i = 0; i < count; ++i) {
    if (std::strcmp(propName, list[i]) == 0) { which = i; break; }
  }
  if (which == count) return;
  const std::int64_t number = value.type == SendPropType::Float
    ? static_cast<std::int64_t>(value.x * 1000.0f) : value.intValue;
  DemoFxCensusProp& prop = fxCensus_.props[std::string(className) + "." + propName];
  ++prop.writes;
  auto found = prop.values.find(number);
  if (found != prop.values.end()) {
    ++found->second;
  } else if (prop.values.size() < 32u) {
    prop.values[number] = 1;
  } else {
    ++prop.valuesOverflow;
  }
  const std::int32_t index = entityIndex;
  constexpr std::int64_t kIndexMask = 0x7FF;
  if (medigun) {
    const std::string name = propName;
    if (name == "m_hOwnerEntity" || name == "m_hOwner") {
      if ((number & kIndexMask) != kIndexMask && number >= 0) fxCensus_.medigunOwner[index] = number & kIndexMask;
    } else if (name == "m_hHealingTarget") {
      fxCensus_.medigunTarget[index] = (number < 0 || (number & kIndexMask) == kIndexMask) ? -1 : (number & kIndexMask);
    } else if (name == "m_iItemDefinitionIndex") {
      fxCensus_.medigunItem[index] = number;
    } else if (name == "m_bChargeRelease") {
      auto last = fxCensus_.medigunCharge.find(index);
      const bool changed = last == fxCensus_.medigunCharge.end() ? number != 0 : last->second != number;
      if (changed) {
        ++prop.changes;
        if (fxCensus_.chargeEdges.size() < 1024u) {
          DemoFxChargeEdge edge;
          edge.tick = serverTick;
          edge.medigun = index;
          const auto owner = fxCensus_.medigunOwner.find(index);
          const auto target = fxCensus_.medigunTarget.find(index);
          const auto item = fxCensus_.medigunItem.find(index);
          edge.owner = owner == fxCensus_.medigunOwner.end() ? -1 : static_cast<std::int32_t>(owner->second);
          edge.target = target == fxCensus_.medigunTarget.end() ? -1 : static_cast<std::int32_t>(target->second);
          edge.item = item == fxCensus_.medigunItem.end() ? -1 : item->second;
          edge.on = number != 0;
          fxCensus_.chargeEdges.push_back(edge);
        }
      }
      fxCensus_.medigunCharge[index] = number;
    }
    return;
  }
  if (player) {
    // which: 0 m_nPlayerCond, 1..4 Ex..Ex4, 5 _condition_bits (an alias of word 0).
    const bool conditionList = which == 5;
    const std::int32_t word = conditionList ? 0 : static_cast<std::int32_t>(which);
    const auto key = std::make_pair(index, word * 2 + (conditionList ? 1 : 0));
    const auto bits = static_cast<std::uint32_t>(number & 0xFFFFFFFFll);
    const auto last = fxCensus_.lastCond.find(key);
    const std::uint32_t before = last == fxCensus_.lastCond.end() ? 0u : last->second;
    if (bits != before) {
      ++prop.changes;
      if (fxCensus_.condEdges.size() < 65536u) {
        DemoFxCondEdge edge;
        edge.tick = serverTick;
        edge.entity = index;
        edge.word = word;
        edge.conditionList = conditionList;
        edge.rose = bits & ~before;
        edge.fell = before & ~bits;
        fxCensus_.condEdges.push_back(edge);
      } else {
        ++fxCensus_.condEdgesDropped;
      }
    }
    fxCensus_.lastCond[key] = bits;
  }
}

void DemoTickAuditCapture::observeTempEvent(const DemoNetworkSummary& summary,
    std::int32_t classId, const EntityState& eventState) {
  // Delta decoding reuses the previous property value when an unchanged field
  // is omitted, so the anim-event fields are carried across events of the same
  // class and reset when the class changes -- exactly the decoder's rule.
  if (classId != lastEventClassId_) {
    lastEventClassId_ = classId;
    animPlayerValid_ = false;
    animEventValid_ = false;
    animPlayer_ = 0;
    animEvent_ = 0;
  }
  const EntityPropertyValue* player = findLeaf(eventState, "m_hPlayer");
  if (player != nullptr) {
    animPlayer_ = player->intValue;
    animPlayerValid_ = true;
  }
  const EntityPropertyValue* event = findLeaf(eventState, "m_iEvent");
  if (event != nullptr) {
    animEvent_ = event->intValue;
    animEventValid_ = true;
  }
  const char* className = classNameForClassId(summary, classId);
  if (!thresholds_.playerEnabled || className == nullptr
      || std::strcmp(className, "CTEPlayerAnimEvent") != 0) {
    return;
  }
  if (!animEventValid_ || animEvent_ != 0) return;
  ++attackEvents_;
  if (pendingPlayers_.size() >= kMaxPendingEvents) {
    ++pendingEventsDropped_;
    return;
  }
  pendingPlayers_.push_back(animPlayerValid_ ? animPlayer_ : 0);
}

std::int64_t DemoTickAuditCapture::maxSimStep(const std::int32_t playerEntity,
    std::int32_t serverTick) const {
  const auto found = simHistory_.find(playerEntity);
  if (found == simHistory_.end()) return 0;
  const std::int32_t windowStart = serverTick - std::max<std::int32_t>(thresholds_.simHistoryTicks, 8);
  std::int64_t best = 0;
  bool havePrevious = false;
  std::int64_t previous = 0;
  for (const auto& sample : found->second) {
    if (sample.serverTick < windowStart) continue;
    if (sample.serverTick > serverTick) break;
    if (havePrevious) {
      const std::int64_t step = sample.simTick - previous;
      if (step > 0 && step < 100 && step > best) best = step;
    }
    previous = sample.simTick;
    havePrevious = true;
  }
  return best;
}

void DemoTickAuditCapture::appendHit(const DemoTickAuditHit& hit) {
  if (hits_.size() >= kMaxStoredHits) return;
  hits_.push_back(hit);
}

void DemoTickAuditCapture::settlePending(const DemoNetworkSummary& summary,
    std::int32_t serverTick, std::int32_t gap) {
  if (pendingPlayers_.empty()) return;
  struct ResolvedShot {
    std::int32_t playerEntity = -1;
    std::int32_t weaponEntity = -1;
    std::string weaponClass;
    std::int32_t itemDefinitionIndex = -1;
    std::int32_t floorTicks = 0;
    bool intervalCheck = false;
    bool rateOnly = false;
  };
  std::vector<ResolvedShot> resolved;
  resolved.reserve(pendingPlayers_.size());
  for (const auto& handle : pendingPlayers_) {
    ResolvedShot shot;
    const std::int32_t playerEntity = static_cast<std::int32_t>(handle & kEntityIndexMask);
    if (playerEntity <= 0 || playerEntity >= 2048) {
      ++shotsUnresolvedWeapon_;
      continue;
    }
    if (static_cast<std::size_t>(playerEntity) >= summary.entityStates.size()) {
      ++shotsUnresolvedWeapon_;
      continue;
    }
    const EntityState& playerState = summary.entityStates[static_cast<std::size_t>(playerEntity)];
    if (!isPlayerClass(classNameForClassId(summary, playerState.classId))) continue;
    bool hasWeapon = false;
    const std::int64_t weaponHandle = leafInt(playerState, "m_hActiveWeapon", hasWeapon);
    if (!hasWeapon) {
      ++shotsUnresolvedWeapon_;
      continue;
    }
    const std::int32_t weaponEntity = static_cast<std::int32_t>(weaponHandle & kEntityIndexMask);
    if (weaponEntity <= 0 || weaponEntity == kEntityIndexMask
        || static_cast<std::size_t>(weaponEntity) >= summary.entityStates.size()) {
      ++shotsUnresolvedWeapon_;
      continue;
    }
    const EntityState& weaponState = summary.entityStates[static_cast<std::size_t>(weaponEntity)];
    const char* weaponClass = classNameForClassId(summary, weaponState.classId);
    if (weaponClass == nullptr) {
      ++shotsUnresolvedWeapon_;
      continue;
    }
    shot.playerEntity = playerEntity;
    shot.weaponEntity = weaponEntity;
    shot.weaponClass = weaponClass;
    bool hasItem = false;
    shot.itemDefinitionIndex = static_cast<std::int32_t>(leafInt(weaponState, "m_iItemDefinitionIndex", hasItem));
    if (!hasItem) shot.itemDefinitionIndex = -1;
    const WeaponProfile* profile = findWeaponProfile(shot.weaponClass);
    if (profile != nullptr) {
      shot.floorTicks = profile->floorTicks;
      shot.intervalCheck = profile->intervalCheck;
      // Force-A-Nature fires faster than the other scatterguns; the item is
      // what separates the two, and without it the slower floor is used (a
      // miss, never a false positive).
      if (shot.weaponClass == "CTFScatterGun"
          && (shot.itemDefinitionIndex == 45 || shot.itemDefinitionIndex == 1078)) {
        shot.floorTicks = 19;
      }
    } else if (shot.weaponClass == "CTFMinigun") {
      shot.rateOnly = true;
    } else {
      ++shotsUnresolvedWeapon_;
      continue;
    }
    resolved.push_back(std::move(shot));
  }
  if (resolved.empty()) return;
  std::map<std::pair<std::int32_t, std::int32_t>, std::int32_t> frameCounts;
  for (const auto& shot : resolved) {
    ++frameCounts[std::make_pair(shot.playerEntity, shot.weaponEntity)];
  }
  for (const auto& shot : resolved) {
    if (shot.rateOnly) {
      auto& window = minigunWindow_[shot.playerEntity];
      window.push_back(serverTick);
      while (!window.empty()
          && serverTick - window.front() > static_cast<std::int32_t>(thresholds_.minigunWindowTicks)) {
        window.pop_front();
      }
      if (thresholds_.minigunRateEnabled
          && static_cast<std::int32_t>(window.size()) > thresholds_.minigunMaxEvents) {
        ++minigunRateHits_;
        DemoTickAuditHit hit;
        hit.serverTick = serverTick;
        hit.entryTick = entryTick_;
        hit.playerEntity = shot.playerEntity;
        hit.weaponEntity = shot.weaponEntity;
        hit.weaponClass = shot.weaponClass;
        hit.itemDefinitionIndex = shot.itemDefinitionIndex;
        hit.kind = "minigun-rate";
        hit.weight = 2;
        hit.intervalTicks = static_cast<std::int32_t>(window.size());
        hit.thresholdTicks = thresholds_.minigunMaxEvents;
        hit.shotsInFrame = static_cast<std::int32_t>(window.size());
        appendHit(hit);
      }
      continue;
    }
    const auto lastWeapon = lastWeaponByPlayer_.find(shot.playerEntity);
    const auto lastTick = lastAttackTickByPlayer_.find(shot.playerEntity);
    if (lastWeapon != lastWeaponByPlayer_.end() && lastTick != lastAttackTickByPlayer_.end()
        && lastWeapon->second != shot.weaponEntity
        && serverTick - lastTick->second <= thresholds_.playerSwitchGuardTicks) {
      ++switchGuarded_;
      continue;
    }
    ++shots_;
    ShotState state;
    state.tick = serverTick;
    state.gap = gap;
    state.weaponEntity = shot.weaponEntity;
    state.floorTicks = shot.floorTicks;
    state.itemDefinitionIndex = shot.itemDefinitionIndex;
    state.intervalCheck = shot.intervalCheck;
    auto& history = shotsByPlayer_[shot.playerEntity];
    const std::int32_t shotsInFrame = frameCounts[std::make_pair(shot.playerEntity, shot.weaponEntity)];
    if (shotsInFrame >= 2 && shot.floorTicks > 0) {
      const std::int64_t allowed = static_cast<std::int64_t>(shotsInFrame - 1) * shot.floorTicks
          * thresholds_.sameFrameRatioPercent / 100;
      if (gap < allowed) {
        ++sameFrameHits_;
        DemoTickAuditHit hit;
        hit.serverTick = serverTick;
        hit.entryTick = entryTick_;
        hit.playerEntity = shot.playerEntity;
        hit.weaponEntity = shot.weaponEntity;
        hit.weaponClass = shot.weaponClass;
        hit.itemDefinitionIndex = shot.itemDefinitionIndex;
        hit.kind = "same-frame";
        hit.weight = 2;
        hit.intervalTicks = gap;
        hit.thresholdTicks = static_cast<std::int32_t>(allowed);
        hit.floorTicks = shot.floorTicks;
        hit.shotsInFrame = shotsInFrame;
        appendHit(hit);
      }
    }
    if (shot.intervalCheck && !history.empty()) {
      const ShotState& previous = history.back();
      if (previous.weaponEntity == shot.weaponEntity) {
        const std::int32_t interval = serverTick - previous.tick;
        const std::int32_t slack = (gap - 1) + (previous.gap - 1);
        const std::int32_t threshold = shot.floorTicks * thresholds_.maxFloorRatioPercent / 100
            - thresholds_.toleranceTicks - slack;
        if (threshold >= thresholds_.shortIntervalMinThreshold && interval < threshold) {
          const std::int64_t missing = std::max<std::int64_t>(shot.floorTicks - interval, 0);
          const std::int64_t required = std::max<std::int64_t>(missing, 4);
          const std::int64_t simStep = maxSimStep(shot.playerEntity, serverTick);
          if (simStep >= required) {
            ++lagExcused_;
          } else {
            ++shortIntervalHits_;
            DemoTickAuditHit hit;
            hit.serverTick = serverTick;
            hit.entryTick = entryTick_;
            hit.playerEntity = shot.playerEntity;
            hit.weaponEntity = shot.weaponEntity;
            hit.weaponClass = shot.weaponClass;
            hit.itemDefinitionIndex = shot.itemDefinitionIndex;
            hit.kind = "short-interval";
            hit.weight = 1;
            hit.intervalTicks = interval;
            hit.thresholdTicks = threshold;
            hit.floorTicks = shot.floorTicks;
            hit.shotsInFrame = 1;
            appendHit(hit);
          }
        }
      }
    }
    history.push_back(state);
    if (history.size() > 32u) history.pop_front();
    lastWeaponByPlayer_[shot.playerEntity] = shot.weaponEntity;
    lastAttackTickByPlayer_[shot.playerEntity] = serverTick;
  }
}

void DemoTickAuditCapture::endPacket(const DemoNetworkSummary& summary,
    std::int32_t finalServerTick, bool ownNetTick) {
  if (!inPacket_) return;
  std::int32_t serverTick = finalServerTick >= 0 ? finalServerTick : packetServerTick_;
  if (serverTick < 0) serverTick = entryTick_;
  std::int32_t gap = 1;
  if (prevPacketServerTick_ >= 0) {
    gap = serverTick - prevPacketServerTick_;
    if (gap < 1) gap = 1;
  }
  DemoTickAuditPacketRow row;
  row.entryTick = entryTick_;
  row.serverTick = serverTick;
  row.gap = gap;
  row.command = command_;
  row.packetOrdinal = packetOrdinal_;
  row.ownNetTick = ownNetTick;
  packets_.push_back(row);
  settlePending(summary, serverTick, gap);
  if (motionCensusEnabled_) settleMotion();
  prevPacketServerTick_ = serverTick;
  inPacket_ = false;
  pendingPlayers_.clear();
}

// ---- A1: --motion-census ----------------------------------------------------
namespace {

constexpr std::int32_t kMotionOpenBucket = std::numeric_limits<std::int32_t>::max();
constexpr std::int32_t kMotionDtBounds[] = {1, 2, 4, 8, 32};
constexpr std::size_t kMotionTopKeep = 64;
constexpr std::uint32_t kCondTeleportedBit = 1u << 6;  // TF_COND_TELEPORTED

std::int32_t motionDtBucket(std::int32_t dt) {
  for (const std::int32_t bound : kMotionDtBounds) {
    if (dt <= bound) return bound;
  }
  return kMotionOpenBucket;
}

// The smaller of the two ways round, so 359 -> 1 reads as 2 degrees.
double wrapDegrees(double delta) {
  const double folded = std::fmod(std::fabs(delta), 360.0);
  return folded > 180.0 ? 360.0 - folded : folded;
}

void keepTopMotion(std::vector<DemoMotionSample>& list, const DemoMotionSample& sample) {
  list.push_back(sample);
  if (list.size() > 2 * kMotionTopKeep) {
    std::sort(list.begin(), list.end(), [](const DemoMotionSample& left, const DemoMotionSample& right) {
      return left.perTick > right.perTick;
    });
    list.resize(kMotionTopKeep);
  }
}

}  // namespace

void DemoTickAuditCapture::observeMotionProperty(const DemoNetworkSummary& summary,
    std::int32_t serverTick, std::int32_t entityIndex, std::int32_t classId,
    const char* propName, const EntityPropertyValue& value) {
  if (propName == nullptr || entityIndex < 0 || entityIndex >= 2048) return;
  // Leaf names: the Local- and NonLocal-exclusive tables both carry m_vecOrigin and
  // m_angEyeAngles, and the hook passes the leaf. A write always carries the value
  // current at its tick, so whichever slot wrote, the step is a real one.
  const bool xy = std::strcmp(propName, "m_vecOrigin") == 0;
  const bool z = std::strcmp(propName, "m_vecOrigin[2]") == 0;
  const bool pitch = std::strcmp(propName, "m_angEyeAngles[0]") == 0;
  const bool yaw = std::strcmp(propName, "m_angEyeAngles[1]") == 0;
  const bool life = std::strcmp(propName, "m_lifeState") == 0;
  const bool team = std::strcmp(propName, "m_iTeamNum") == 0;
  const bool playerClass = std::strcmp(propName, "m_iClass") == 0;
  const bool cond = std::strcmp(propName, "m_nPlayerCond") == 0;
  const bool condBits = std::strcmp(propName, "_condition_bits") == 0;
  if (!xy && !z && !pitch && !yaw && !life && !team && !playerClass && !cond && !condBits) return;
  if (!isPlayerClass(classNameForClassId(summary, classId))) return;
  MotionState& state = motionState_[entityIndex];
  if (life) {
    if (state.haveLife && state.life != value.intValue) state.lifeTick = serverTick;
    state.life = value.intValue;
    state.haveLife = true;
    return;
  }
  if (team) {
    if (state.haveTeam && state.team != value.intValue) state.teamTick = serverTick;
    state.team = value.intValue;
    state.haveTeam = true;
    return;
  }
  if (playerClass) {
    if (state.haveClass && state.playerClass != value.intValue) state.teamTick = serverTick;
    state.playerClass = value.intValue;
    state.haveClass = true;
    return;
  }
  if (cond || condBits) {
    const auto bits = static_cast<std::uint32_t>(value.intValue & 0xFFFFFFFFll);
    std::uint32_t& before = cond ? state.cond : state.condBits;
    if ((bits & ~before) & kCondTeleportedBit) state.teleTick = serverTick;
    before = bits;
    return;
  }
  if (!state.touched) {
    state.touched = true;
    motionTouched_.push_back(entityIndex);
    state.px = state.x;
    state.py = state.y;
    state.pz = state.z;
    state.ppitch = state.pitch;
    state.pyaw = state.yaw;
  }
  state.pendingTick = serverTick;
  if (xy) {
    state.px = value.x;
    state.py = value.y;
    state.pendPos = true;
  } else if (z) {
    state.pz = value.x;
    state.pendPos = true;
  } else if (pitch) {
    state.ppitch = value.x;
    state.pendAng = true;
  } else {
    state.pyaw = value.x;
    state.pendAng = true;
  }
}

void DemoTickAuditCapture::settleMotion() {
  for (const std::int32_t entity : motionTouched_) {
    const auto found = motionState_.find(entity);
    if (found == motionState_.end()) continue;
    MotionState& state = found->second;
    state.touched = false;
    const std::int32_t tick = state.pendingTick;
    if (state.pendPos) {
      state.pendPos = false;
      if (state.havePos && tick > state.posTick) {
        const double dx = state.px - state.x;
        const double dy = state.py - state.y;
        const double dz = state.pz - state.z;
        recordMotion(false, entity, tick, tick - state.posTick, std::sqrt(dx * dx + dy * dy + dz * dz), state);
      }
      if (!state.havePos || tick > state.posTick) state.posTick = tick;
      state.havePos = true;
      state.x = state.px;
      state.y = state.py;
      state.z = state.pz;
    }
    if (state.pendAng) {
      state.pendAng = false;
      if (state.haveAng && tick > state.angTick) {
        const double dp = wrapDegrees(state.ppitch - state.pitch);
        const double dyaw = wrapDegrees(state.pyaw - state.yaw);
        recordMotion(true, entity, tick, tick - state.angTick, std::sqrt(dp * dp + dyaw * dyaw), state);
      }
      if (!state.haveAng || tick > state.angTick) state.angTick = tick;
      state.haveAng = true;
      state.pitch = state.ppitch;
      state.yaw = state.pyaw;
    }
  }
  motionTouched_.clear();
}

void DemoTickAuditCapture::recordMotion(bool turn, std::int32_t entity, std::int32_t tick,
    std::int32_t dt, double amount, const MotionState& state) {
  DemoMotionCensus& census = motionCensus_;
  const auto ago = [tick](std::int32_t mark) { return mark < 0 ? -1 : tick - mark; };
  const std::int32_t guard = census.guardTicks;
  const auto guarded = [guard](std::int32_t since) { return since >= 0 && since <= guard; };
  DemoMotionSample sample;
  sample.tick = tick;
  sample.entity = entity;
  sample.dt = dt;
  sample.amount = amount;
  sample.perTick = amount / static_cast<double>(dt);
  sample.sinceLife = ago(state.lifeTick);
  sample.sinceTeleport = ago(state.teleTick);
  sample.sinceTeam = ago(state.teamTick);
  sample.excluded = guarded(sample.sinceLife) || guarded(sample.sinceTeleport) || guarded(sample.sinceTeam);
  if (turn) {
    ++census.turnSamples;
    ++census.turnDt[motionDtBucket(dt)];
  } else {
    ++census.moveSamples;
    ++census.moveDt[motionDtBucket(dt)];
  }
  if (dt > census.rateMaxDt) {
    if (turn) ++census.turnLongDt; else ++census.moveLongDt;
    return;
  }
  DemoMotionRate rate;
  rate.perTick = static_cast<float>(sample.perTick);
  rate.entity = entity;
  rate.tick = tick;
  if (sample.excluded) {
    if (turn) ++census.turnExcluded; else ++census.moveExcluded;
  } else if (turn) {
    census.turnRates.push_back(rate);
  } else {
    census.moveRates.push_back(rate);
  }
  keepTopMotion(turn ? census.topTurns : census.topMoves, sample);
}

DemoTickAuditReport auditDemoTicks(const DemoHeader& header, const DemoIndex& index,
    const DemoTickAuditCapture* capture, const DemoTickAuditThresholds& thresholds) {
  DemoTickAuditReport report;
  report.headerTicks = static_cast<std::uint32_t>(std::max(header.ticks, 0));
  report.headerFrames = static_cast<std::uint32_t>(std::max(header.frames, 0));
  report.headerPlaybackTime = header.playbackTime;
  report.firstDataEntryTick = -1;
  report.lastDataEntryTick = -1;
  auto addIssue = [&](DemoTickAuditLevel level, const std::string& code, std::int32_t entryTick,
                      std::int32_t serverTick, std::size_t packet, const std::string& detail) {
    if (report.issues.size() >= thresholds.maxIssues) {
      ++report.issuesDropped;
      return;
    }
    DemoTickAuditIssue issue;
    issue.level = level;
    issue.code = code;
    issue.entryTick = entryTick;
    issue.serverTick = serverTick;
    issue.packet = packet;
    issue.detail = detail;
    report.issues.push_back(std::move(issue));
    if (level > report.verdict) report.verdict = level;
  };

  // FILE LAYER: demo tick monotonicity and packet sequence numbers.
  std::int32_t lastDataTick = 0;
  bool haveDataTick = false;
  std::size_t packetOrdinal = 0;
  std::int32_t lastInSequence = 0;
  std::int32_t lastOutSequence = 0;
  bool haveDataSequence = false;
  std::size_t sequenceIndex = 0;
  std::int32_t firstBackwardsTick = 0;
  std::size_t firstBackwardsPacket = 0;
  std::int32_t firstGapTick = 0;
  std::size_t firstGapPacket = 0;
  for (const auto& entry : index.entries) {
    if (entry.command == 1) {
      ++report.signonPackets;
      ++packetOrdinal;
      ++sequenceIndex;
      continue;
    }
    if (entry.command != 2) continue;
    ++report.dataPackets;
    ++packetOrdinal;
    if (report.firstDataEntryTick < 0) report.firstDataEntryTick = entry.tick;
    report.lastDataEntryTick = entry.tick;
    if (haveDataTick && entry.tick < lastDataTick) {
      const std::int32_t backwards = lastDataTick - entry.tick;
      ++report.tickBackwards;
      if (backwards > report.largestTickBackwards) report.largestTickBackwards = backwards;
      if (report.tickBackwards <= 8) {
        addIssue(DemoTickAuditLevel::Edited, "tick-backwards", entry.tick, -1, packetOrdinal - 1,
            "demo tick went backwards by " + std::to_string(backwards));
      }
    }
    lastDataTick = entry.tick;
    haveDataTick = true;
    if (sequenceIndex < index.packetInSequence.size()
        && sequenceIndex < index.packetOutSequence.size()) {
      const std::int32_t inSequence = index.packetInSequence[sequenceIndex];
      const std::int32_t outSequence = index.packetOutSequence[sequenceIndex];
      if (haveDataSequence) {
        const std::int32_t inDelta = inSequence - lastInSequence;
        const std::int32_t outDelta = outSequence - lastOutSequence;
        if (inDelta <= 0) {
          if (report.inSequenceBackwards == 0) { firstBackwardsTick = entry.tick; firstBackwardsPacket = packetOrdinal - 1; }
          ++report.inSequenceBackwards;
        } else if (inDelta > 1) {
          if (report.inSequenceGaps == 0 && report.outSequenceGaps == 0) { firstGapTick = entry.tick; firstGapPacket = packetOrdinal - 1; }
          ++report.inSequenceGaps;
        }
        if (outDelta <= 0) {
          if (report.outSequenceBackwards == 0) { firstBackwardsTick = entry.tick; firstBackwardsPacket = packetOrdinal - 1; }
          ++report.outSequenceBackwards;
        } else if (outDelta > 1) {
          if (report.inSequenceGaps == 0 && report.outSequenceGaps == 0) { firstGapTick = entry.tick; firstGapPacket = packetOrdinal - 1; }
          ++report.outSequenceGaps;
        }
      }
      lastInSequence = inSequence;
      lastOutSequence = outSequence;
      haveDataSequence = true;
    }
    ++sequenceIndex;
  }
  report.packets = index.packetCount;
  if (report.tickBackwards > 8) {
    addIssue(DemoTickAuditLevel::Edited, "tick-backwards-more", lastDataTick, -1, packetOrdinal,
        "tick went backwards " + std::to_string(report.tickBackwards) + " times; only the first 8 are listed");
  }
  if (report.inSequenceBackwards > 0) {
    addIssue(DemoTickAuditLevel::Edited, "in-sequence-backwards", firstBackwardsTick, -1, firstBackwardsPacket,
        "in sequence went backwards " + std::to_string(report.inSequenceBackwards) + " times");
  }
  if (report.outSequenceBackwards > 0) {
    addIssue(DemoTickAuditLevel::Edited, "out-sequence-backwards", firstBackwardsTick, -1, firstBackwardsPacket,
        "out sequence went backwards " + std::to_string(report.outSequenceBackwards) + " times");
  }
  if (report.inSequenceGaps > 0 || report.outSequenceGaps > 0) {
    addIssue(DemoTickAuditLevel::Info, "sequence-gaps", firstGapTick, -1, firstGapPacket,
        "in gaps=" + std::to_string(report.inSequenceGaps) + " out gaps="
            + std::to_string(report.outSequenceGaps));
  }

  // FILE LAYER: server-clock offset against the demo clock.
  if (capture != nullptr) {
    const auto& rows = capture->packetRows();
    struct WindowRow {
      std::int64_t offset = 0;
      std::int64_t entryTick = 0;
    };
    std::deque<WindowRow> windowRows;
    std::multiset<std::int64_t> offsetSet;
    std::int64_t lastIssueTick = -1000000;
    std::size_t storedJumpIssues = 0;
    const std::int64_t warmupStart = report.firstDataEntryTick < 0 ? 0 : report.firstDataEntryTick;
    for (const auto& row : rows) {
      if (row.command != 2) continue;
      if (row.gap > report.largestTickGap) report.largestTickGap = static_cast<std::size_t>(row.gap);
      const std::int64_t offset = static_cast<std::int64_t>(row.serverTick) - row.entryTick;
      if (report.clockSamples == 0) report.clockOffsetFirst = offset;
      report.clockOffsetLast = offset;
      if (report.clockSamples == 0 || offset < report.clockOffsetMin) report.clockOffsetMin = offset;
      if (report.clockSamples == 0 || offset > report.clockOffsetMax) report.clockOffsetMax = offset;
      ++report.clockSamples;
      windowRows.push_back({offset, row.entryTick});
      offsetSet.insert(offset);
      while (!windowRows.empty()
          && row.entryTick - windowRows.front().entryTick
                 > static_cast<std::int64_t>(thresholds.clockWindowTicks)) {
        offsetSet.erase(offsetSet.find(windowRows.front().offset));
        windowRows.pop_front();
      }
      const bool warm = row.entryTick - warmupStart >= static_cast<std::int64_t>(thresholds.warmupTicks);
      if (!warm || offsetSet.size() < 32u) continue;
      const std::int64_t median = *std::next(offsetSet.begin(), static_cast<std::ptrdiff_t>(offsetSet.size() / 2u));
      const std::int64_t delta = offset - median;
      const std::int64_t magnitude = delta < 0 ? -delta : delta;
      if (magnitude <= thresholds.clockJumpTicks) continue;
      ++report.clockJumps;
      if (magnitude > report.largestClockJump) report.largestClockJump = static_cast<std::int32_t>(magnitude);
      const bool edited = magnitude >= static_cast<std::int64_t>(thresholds.clockJumpTicks) * 2;
      if (storedJumpIssues < 8u && row.entryTick - lastIssueTick >= 100) {
        ++storedJumpIssues;
        lastIssueTick = row.entryTick;
        addIssue(edited ? DemoTickAuditLevel::Edited : DemoTickAuditLevel::Warning, "clock-jump",
            row.entryTick, row.serverTick, row.packetOrdinal,
            "server clock offset moved by " + std::to_string(magnitude) + " ticks (offset="
                + std::to_string(offset) + " median=" + std::to_string(median) + ")");
      }
    }
  }

  // FILE LAYER: header bookkeeping against the packet stream.
  std::string headerDetail = "frames=" + std::to_string(report.headerFrames) + " data="
      + std::to_string(report.dataPackets);
  if (report.headerFrames > 0 && static_cast<std::size_t>(report.headerFrames) != report.dataPackets) {
    ++report.headerMismatches;
    addIssue(DemoTickAuditLevel::Info, "header-frames", 0, -1, 0,
        "header frames=" + std::to_string(report.headerFrames) + " but indexed data packets="
            + std::to_string(report.dataPackets));
  }
  if (report.firstDataEntryTick >= 0 && report.lastDataEntryTick >= report.firstDataEntryTick) {
    const std::int64_t span = report.lastDataEntryTick - report.firstDataEntryTick + 1;
    headerDetail += " span=" + std::to_string(span);
    if (report.headerTicks > 0) {
      const std::int64_t difference = static_cast<std::int64_t>(report.headerTicks) - span;
      if (difference > 2 || difference < -2) {
        ++report.headerMismatches;
        addIssue(DemoTickAuditLevel::Info, "header-ticks", 0, -1, 0,
            "header ticks=" + std::to_string(report.headerTicks) + " but data span="
                + std::to_string(span));
      }
    }
  }
  if (report.headerTicks > 0 && thresholds.tickRate > 0.0) {
    const double expected = static_cast<double>(report.headerTicks) / thresholds.tickRate;
    headerDetail += " playback=" + formatDouble(static_cast<double>(report.headerPlaybackTime), 3)
        + " expected=" + formatDouble(expected, 3);
    if (std::fabs(static_cast<double>(report.headerPlaybackTime) - expected) > std::max(0.05, expected * 0.002)) {
      ++report.headerMismatches;
      addIssue(DemoTickAuditLevel::Warning, "header-playback-time", 0, -1, 0,
          "header playbackTime=" + formatDouble(static_cast<double>(report.headerPlaybackTime), 3)
              + " but ticks/tickrate=" + formatDouble(expected, 3));
    }
  }
  report.headerDetail = headerDetail;

  // PLAYER LAYER: aggregate the capture's hits into per-player episodes.
  if (capture != nullptr) {
    report.attackEvents = capture->attackEvents();
    report.shots = capture->shots();
    report.shotsUnresolvedWeapon = capture->shotsUnresolvedWeapon();
    report.simSamples = capture->simSamples();
    report.sameFrameHits = capture->sameFrameHits();
    report.shortIntervalHits = capture->shortIntervalHits();
    report.lagExcused = capture->lagExcused();
    report.switchGuarded = capture->switchGuarded();
    report.minigunRateHits = capture->minigunRateHits();
    report.hits = capture->hits();
    std::map<std::int32_t, std::vector<const DemoTickAuditHit*>> byPlayer;
    for (const auto& hit : report.hits) byPlayer[hit.playerEntity].push_back(&hit);
    report.flaggedPlayers = byPlayer.size();
    const std::int32_t episodeWindow = static_cast<std::int32_t>(thresholds.tickRate + 0.5);
    std::vector<std::pair<std::int32_t, std::int32_t>> hitTicks;
    for (const auto& hit : report.hits) hitTicks.emplace_back(hit.serverTick, hit.playerEntity);
    std::sort(hitTicks.begin(), hitTicks.end());
    std::size_t fuseBest = 0;
    {
      std::map<std::int32_t, int> windowCounts;
      std::size_t begin = 0;
      std::size_t distinct = 0;
      for (std::size_t end = 0; end < hitTicks.size(); ++end) {
        auto& count = windowCounts[hitTicks[end].second];
        if (count == 0) ++distinct;
        ++count;
        while (hitTicks[end].first - hitTicks[begin].first
            > static_cast<std::int32_t>(thresholds.fuseWindowTicks)) {
          auto& old = windowCounts[hitTicks[begin].second];
          --old;
          if (old == 0) --distinct;
          ++begin;
        }
        if (distinct > fuseBest) fuseBest = distinct;
      }
    }
    report.fuseTripped = fuseBest > thresholds.fusePlayers;
    for (const auto& pair : byPlayer) {
      DemoTickAuditPlayerSummary summary;
      summary.playerEntity = pair.first;
      std::int32_t firstTick = -1;
      std::int32_t lastTick = -1;
      std::size_t episodes = 0;
      for (const auto* hit : pair.second) {
        ++summary.hits;
        summary.weight += hit->weight;
        if (firstTick < 0 || hit->serverTick < firstTick) firstTick = hit->serverTick;
        if (lastTick < 0 || hit->serverTick > lastTick) lastTick = hit->serverTick;
        bool known = false;
        for (const auto& weapon : summary.weapons) if (weapon == hit->weaponClass) { known = true; break; }
        if (!known) summary.weapons.push_back(hit->weaponClass);
      }
      std::vector<std::int32_t> ticks;
      for (const auto* hit : pair.second) ticks.push_back(hit->serverTick);
      std::sort(ticks.begin(), ticks.end());
      for (std::size_t tickIndex = 0; tickIndex < ticks.size(); ++tickIndex) {
        if (tickIndex == 0 || ticks[tickIndex] - ticks[tickIndex - 1] > episodeWindow) ++episodes;
      }
      summary.episodes = episodes;
      summary.firstHitTick = firstTick;
      summary.lastHitTick = lastTick;
      summary.confidence = summary.weight >= 3 ? "high" : (summary.weight == 2 ? "medium" : "low");
      report.players.push_back(std::move(summary));
    }
    if (report.fuseTripped) {
      addIssue(DemoTickAuditLevel::Info, "player-fuse", 0, -1, 0,
          std::to_string(fuseBest) + " players tripped inside " + std::to_string(thresholds.fuseWindowTicks)
              + " ticks; their verdict is demoted (server-side condition, not a dozen cheaters)");
    }
    for (const auto& summary : report.players) {
      DemoTickAuditLevel level = summary.confidence == "low" ? DemoTickAuditLevel::Info
          : DemoTickAuditLevel::Warning;
      if (report.fuseTripped) level = DemoTickAuditLevel::Info;
      addIssue(level, "player-doubletap", summary.firstHitTick, summary.firstHitTick, 0,
          "player entity=" + std::to_string(summary.playerEntity) + " hits="
              + std::to_string(summary.hits) + " weight=" + std::to_string(summary.weight)
              + " episodes=" + std::to_string(summary.episodes) + " confidence="
              + summary.confidence + " weapons=" + joinNames(summary.weapons));
    }
  }
  report.valid = report.packets > 0;
  if (!report.valid && report.error.empty()) report.error = "no demo packets indexed";
  return report;
}

std::vector<std::string> formatDemoTickAuditLines(const DemoTickAuditReport& report) {
  std::vector<std::string> lines;
  if (!report.valid) {
    lines.push_back("tick-audit: verdict=invalid error=" + report.error);
    return lines;
  }
  lines.push_back("tick-audit: file packets=" + std::to_string(report.packets)
      + " data=" + std::to_string(report.dataPackets)
      + " signon=" + std::to_string(report.signonPackets)
      + " span=" + std::to_string(report.firstDataEntryTick) + ".."
      + std::to_string(report.lastDataEntryTick));
  lines.push_back("tick-audit: seq in-backwards=" + std::to_string(report.inSequenceBackwards)
      + " in-gaps=" + std::to_string(report.inSequenceGaps)
      + " out-backwards=" + std::to_string(report.outSequenceBackwards)
      + " out-gaps=" + std::to_string(report.outSequenceGaps)
      + " tick-backwards=" + std::to_string(report.tickBackwards)
      + " max-tick-backwards=" + std::to_string(report.largestTickBackwards));
  lines.push_back("tick-audit: clock samples=" + std::to_string(report.clockSamples)
      + " offset-first=" + std::to_string(report.clockOffsetFirst)
      + " offset-last=" + std::to_string(report.clockOffsetLast)
      + " offset-min=" + std::to_string(report.clockOffsetMin)
      + " offset-max=" + std::to_string(report.clockOffsetMax)
      + " jumps=" + std::to_string(report.clockJumps)
      + " max-jump=" + std::to_string(report.largestClockJump)
      + " max-gap=" + std::to_string(report.largestTickGap));
  lines.push_back("tick-audit: header detail=" + report.headerDetail
      + " mismatches=" + std::to_string(report.headerMismatches));
  lines.push_back("tick-audit: player attacks=" + std::to_string(report.attackEvents)
      + " shots=" + std::to_string(report.shots)
      + " unresolved=" + std::to_string(report.shotsUnresolvedWeapon)
      + " same-frame=" + std::to_string(report.sameFrameHits)
      + " short-interval=" + std::to_string(report.shortIntervalHits)
      + " lag-excused=" + std::to_string(report.lagExcused)
      + " switch-guarded=" + std::to_string(report.switchGuarded)
      + " minigun=" + std::to_string(report.minigunRateHits)
      + " sim-samples=" + std::to_string(report.simSamples));
  lines.push_back("tick-audit: players=" + std::to_string(report.flaggedPlayers)
      + " hits=" + std::to_string(report.hits.size())
      + " fuse=" + (report.fuseTripped ? "tripped" : "clear"));
  lines.push_back("tick-audit: verdict=" + std::string(demoTickAuditLevelName(report.verdict))
      + " issues=" + std::to_string(report.issues.size())
      + " dropped=" + std::to_string(report.issuesDropped));
  for (const auto& summary : report.players) {
    lines.push_back("tick-audit: player entity=" + std::to_string(summary.playerEntity)
        + " hits=" + std::to_string(summary.hits)
        + " weight=" + std::to_string(summary.weight)
        + " episodes=" + std::to_string(summary.episodes)
        + " confidence=" + summary.confidence
        + " first=" + std::to_string(summary.firstHitTick)
        + " last=" + std::to_string(summary.lastHitTick)
        + " weapons=" + joinNames(summary.weapons));
  }
  std::size_t hitLines = 0;
  for (const auto& hit : report.hits) {
    if (hitLines >= kMaxHitLines) break;
    ++hitLines;
    lines.push_back("tick-audit: hit tick=" + std::to_string(hit.serverTick)
        + " demo-tick=" + std::to_string(hit.entryTick)
        + " player=" + std::to_string(hit.playerEntity)
        + " weapon=" + hit.weaponClass
        + " item=" + std::to_string(hit.itemDefinitionIndex)
        + " kind=" + hit.kind
        + " weight=" + std::to_string(hit.weight)
        + " interval=" + std::to_string(hit.intervalTicks)
        + " threshold=" + std::to_string(hit.thresholdTicks)
        + " floor=" + std::to_string(hit.floorTicks)
        + " shots-in-frame=" + std::to_string(hit.shotsInFrame));
  }
  for (const auto& issue : report.issues) {
    lines.push_back("tick-audit: issue level=" + std::string(demoTickAuditLevelName(issue.level))
        + " code=" + issue.code
        + " tick=" + std::to_string(issue.entryTick)
        + " server=" + std::to_string(issue.serverTick)
        + " packet=" + std::to_string(issue.packet)
        + " detail=" + issue.detail);
  }
  return lines;
}

}  // namespace tf2::native
