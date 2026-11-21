#include "demo_tick_audit.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
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
  inPacket_ = true;
  pendingPlayers_.clear();
}

void DemoTickAuditCapture::observeEntityProperty(const DemoNetworkSummary& summary,
    std::int32_t serverTick, std::int32_t entityIndex, std::int32_t classId,
    const char* propName, const EntityPropertyValue& value) {
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
  row.ownNetTick = ownNetTick;
  packets_.push_back(row);
  settlePending(summary, serverTick, gap);
  prevPacketServerTick_ = serverTick;
  inPacket_ = false;
  pendingPlayers_.clear();
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
        if (inDelta <= 0) ++report.inSequenceBackwards;
        else if (inDelta > 1) ++report.inSequenceGaps;
        if (outDelta <= 0) ++report.outSequenceBackwards;
        else if (outDelta > 1) ++report.outSequenceGaps;
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
    addIssue(DemoTickAuditLevel::Edited, "in-sequence-backwards", 0, -1, 0,
        "in sequence went backwards " + std::to_string(report.inSequenceBackwards) + " times");
  }
  if (report.outSequenceBackwards > 0) {
    addIssue(DemoTickAuditLevel::Edited, "out-sequence-backwards", 0, -1, 0,
        "out sequence went backwards " + std::to_string(report.outSequenceBackwards) + " times");
  }
  if (report.inSequenceGaps > 0 || report.outSequenceGaps > 0) {
    addIssue(DemoTickAuditLevel::Info, "sequence-gaps", 0, -1, 0,
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
            row.entryTick, row.serverTick, 0,
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
      for (std::size_t index = 0; index < ticks.size(); ++index) {
        if (index == 0 || ticks[index] - ticks[index - 1] > episodeWindow) ++episodes;
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
