#include "demo_header.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <queue>
#include <chrono>
#include <unordered_map>
#include <unordered_set>

#include "demo_header_internal.h"

namespace tf2::native {

void applyTempEntityFields(TempEntityEvent& event) {
  event.knownFieldCount = 0;
  event.hasOrigin = event.hasAngles = event.hasPlayer = event.hasWeaponId = event.hasMode = false;
  event.hasSeed = event.hasEvent = event.hasEffectIndex = event.hasMagnitude = event.hasScale = false;
  event.hasRadius = event.hasParticleName = event.hasVelocity = event.hasModelIndex = false;
  event.hasOwner = event.hasLifeTime = false;
  const auto* properties = &event.properties;
  auto takeInt = [&](const char* name, bool& present, std::int64_t& target) {
    const auto* value = findPropertySuffix(*properties, name);
    if (!value || value->type != SendPropType::Int) return;
    present = true;
    target = value->intValue;
    ++event.knownFieldCount;
  };
  auto takeVec = [&](const char* name, bool& present, float target[3]) {
    const auto* value = findPropertySuffix(*properties, name);
    if (!value || value->type != SendPropType::Vector) return;
    if (!std::isfinite(value->x) || !std::isfinite(value->y) || !std::isfinite(value->z)) return;
    present = true;
    target[0] = value->x; target[1] = value->y; target[2] = value->z;
    ++event.knownFieldCount;
  };
  takeVec("m_vecOrigin", event.hasOrigin, event.origin);
  takeVec("m_vecAngles", event.hasAngles, event.angles);
  if (event.className == "CTEFireBullets") {
    takeInt("m_iPlayer", event.hasPlayer, event.player);
    takeInt("m_iWeaponID", event.hasWeaponId, event.weaponId);
    takeInt("m_iMode", event.hasMode, event.mode);
    takeInt("m_iSeed", event.hasSeed, event.seed);
  } else if (event.className == "CTEPlayerAnimEvent") {
    takeInt("m_hPlayer", event.hasPlayer, event.player);
    takeInt("m_iEvent", event.hasEvent, event.event);
  } else if (event.className == "CTETFParticleEffect") {
    takeInt("m_iEffectIndex", event.hasEffectIndex, event.effectIndex);
    if (const auto* value = findPropertySuffix(*properties, "m_szParticleName");
        value && value->type == SendPropType::String) {
      event.hasParticleName = true;
      event.particleName = value->stringValue;
      ++event.knownFieldCount;
    }
  } else if (event.className == "CTETFExplosion" || event.className == "CTEExplosion") {
    takeInt("m_iMagnitude", event.hasMagnitude, event.magnitude);
    takeInt("m_iScale", event.hasScale, event.scale);
    takeInt("m_iRadius", event.hasRadius, event.radius);
  } else if (event.className == "CTEClientProjectile") {
    takeVec("m_vecVelocity", event.hasVelocity, event.velocity);
    takeInt("m_nModelIndex", event.hasModelIndex, event.modelIndex);
    if (!event.hasModelIndex) takeInt("m_iModelIndex", event.hasModelIndex, event.modelIndex);
    takeInt("m_hOwner", event.hasOwner, event.owner);
    takeInt("m_nLifeTime", event.hasLifeTime, event.lifeTime);
    if (!event.hasLifeTime) takeInt("m_iLifeTime", event.hasLifeTime, event.lifeTime);
  }
}

bool projectileFromTempEntity(const TempEntityEvent& event, ProjectileTimelineEvent& timeline) {
  const bool projectileClass = event.className == "CTEFireBullets" ||
    event.className == "CTETFParticleEffect" || event.className == "CTETFExplosion" ||
    event.className == "CTEExplosion" || event.className == "CTEClientProjectile";
  if (!projectileClass) return false;
  timeline = {};
  timeline.tick = event.tick;
  timeline.className = event.className;
  timeline.hasOrigin = event.hasOrigin;
  if (timeline.hasOrigin) std::copy(std::begin(event.origin), std::end(event.origin), std::begin(timeline.origin));
  timeline.hasDirection = event.hasAngles;
  if (timeline.hasDirection) std::copy(std::begin(event.angles), std::end(event.angles), std::begin(timeline.direction));
  timeline.hasWeaponId = event.hasWeaponId;
  timeline.weaponId = event.weaponId;
  timeline.hasParticleName = event.hasParticleName;
  timeline.particleName = event.particleName;
  timeline.hasMagnitude = event.hasMagnitude && event.magnitude >= 0;
  timeline.magnitude = event.magnitude;
  timeline.hasScale = event.hasScale;
  timeline.scale = event.scale;
  timeline.hasRadius = event.hasRadius && event.radius >= 0;
  timeline.radius = event.radius;
  timeline.hasVelocity = event.hasVelocity;
  if (timeline.hasVelocity) std::copy(std::begin(event.velocity), std::end(event.velocity), std::begin(timeline.velocity));
  timeline.hasModelIndex = event.hasModelIndex;
  timeline.modelIndex = event.modelIndex;
  timeline.hasOwner = event.hasOwner;
  timeline.owner = event.owner;
  timeline.hasLifeTime = event.hasLifeTime && event.lifeTime >= 0;
  timeline.lifeTime = event.lifeTime;
  return true;
}
namespace {

// Game-event values are rendered the way the T64 oracle renders them
// (work/_refs_demostf/examples/dump_events.rs): strings sanitised (\t \n \r -> space,
// ';' -> ','), floats as Rust's Display (shortest round-trip digits, fixed
// notation, no exponent), integers decimal, booleans true/false. The oracle is
// the only consumer of this text, so the format is pinned by the gate.
std::string sanitizeGameEventText(const std::string& value) {
  std::string text = value;
  for (char& character : text) {
    if (character == '\t' || character == '\n' || character == '\r') character = ' ';
    else if (character == ';') character = ',';
  }
  return text;
}

std::string formatGameEventFloat(float value) {
  char buffer[64] = {};
  const auto converted = std::to_chars(buffer, buffer + sizeof(buffer), value, std::chars_format::fixed);
  if (converted.ec == std::errc()) return std::string(buffer, converted.ptr);
  char fallback[64] = {};
  std::snprintf(fallback, sizeof(fallback), "%.9g", static_cast<double>(value));
  return fallback;
}
} // namespace

// svc_GameEvent = 25: 11-bit length in BITS, then a payload of exactly that many
// bits: 9-bit event id followed by one value per definition entry, in
// definition order. The definition is the only schema, so the consumed bit
// count is validated against the declared length; a mismatch is counted, never
// turned into a shorter event. The tick is the cmd=2 packet tick (playback
// domain), NOT the signon entry's tick field.
bool readGameEvent(MessageBits& bits, DemoNetworkSummary& summary, std::int32_t entryTick,
                   std::uint32_t packetOrdinal, std::uint32_t messageBit) {
  std::uint32_t payloadBits = 0;
  if (!bits.read(11, payloadBits)) return false;
  if (payloadBits > bits.remaining()) return false;
  MessageBits payload = bits.limited(payloadBits);
  std::uint32_t eventId = 0;
  const bool idOk = payload.read(9, eventId);
  if (!idOk) {
    ++summary.gameEventDecodeFailures;
  } else if (summary.gameEventDefinitions.empty()) {
    // The table arrives in svc_GameEventList during signon, before the first
    // data packet. An event before it is a decode failure, not an empty event:
    // the reference crate returns an empty event and does not even consume the
    // payload, which is the silent loss this counter exists to prevent.
    ++summary.gameEventDecodeFailures;
  } else if (eventId >= summary.gameEventDefinitions.size()) {
    ++summary.gameEventUnknownId;
  } else {
    const GameEventDefinition& definition = summary.gameEventDefinitions[eventId];
    DemoGameEvent event;
    event.tick = entryTick;
    event.eventId = static_cast<std::uint16_t>(eventId);
    event.name = definition.name;
    event.packetOrdinal = packetOrdinal;
    event.messageBit = messageBit;
    bool valuesOk = true;
    for (const auto& entry : definition.entries) {
      std::string rendered;
      switch (entry.kind) {
        case GameEventValueType::String: {
          std::string text;
          // The terminator byte is part of the value, so a value can never be
          // longer than the payload's remaining whole bytes.
          if (!payload.readString(text, payload.remaining() / 8)) { valuesOk = false; break; }
          rendered = sanitizeGameEventText(text);
          break;
        }
        case GameEventValueType::Float: {
          std::uint32_t raw = 0;
          if (!payload.read(32, raw)) { valuesOk = false; break; }
          float real = 0.0f;
          std::memcpy(&real, &raw, sizeof(real));
          rendered = formatGameEventFloat(real);
          break;
        }
        case GameEventValueType::Long: { std::uint32_t number = 0; if (!payload.read(32, number)) { valuesOk = false; break; } rendered = std::to_string(number); break; }
        case GameEventValueType::Short: { std::uint32_t number = 0; if (!payload.read(16, number)) { valuesOk = false; break; } rendered = std::to_string(number); break; }
        case GameEventValueType::Byte: { std::uint32_t number = 0; if (!payload.read(8, number)) { valuesOk = false; break; } rendered = std::to_string(number); break; }
        case GameEventValueType::Boolean: { std::uint32_t flag = 0; if (!payload.read(1, flag)) { valuesOk = false; break; } rendered = flag != 0 ? "true" : "false"; break; }
        case GameEventValueType::Local: break;  // no wire bits; the value is client state
        case GameEventValueType::None: valuesOk = false; break;
      }
      if (!valuesOk) break;
      event.fields.emplace_back(entry.name, std::move(rendered));
    }
    if (!valuesOk || payload.remaining() != 0) {
      // Do not fail the packet: the event's length field is still authoritative
      // and the next 6-bit message type sits exactly at start + payloadBits.
      ++summary.gameEventDecodeFailures;
    } else {
      ++summary.gameEventDecoded;
      summary.gameEvents.push_back(std::move(event));
    }
  }
  ++summary.gameEventCount;
  summary.gameEventPayloadBits += payloadBits;
  if (!bits.skip(payloadBits)) return false;
  return true;
}

// svc_GameEventList = 30: 9-bit count, 20-bit length in BITS, then that many
// definitions: 9-bit id, null-terminated UTF-8 name, repeated (3-bit value type,
// null-terminated UTF-8 entry name) until a 3-bit type == 0. The table replaces
// any previous one (last list wins), matching the reference parser.
bool readGameEventList(MessageBits& bits, DemoNetworkSummary& summary) {
  std::uint32_t eventCount = 0;
  std::uint32_t payloadBits = 0;
  if (!bits.read(9, eventCount) || !bits.read(20, payloadBits)) return false;
  if (payloadBits > bits.remaining()) return false;
  MessageBits payload = bits.limited(payloadBits);
  std::vector<GameEventDefinition> definitions;
  definitions.reserve(eventCount);
  bool ok = true;
  for (std::uint32_t index = 0; index < eventCount && ok; ++index) {
    GameEventDefinition definition;
    std::uint32_t id = 0;
    if (!payload.read(9, id) || !payload.readString(definition.name, payload.remaining() / 8)) { ok = false; break; }
    definition.id = static_cast<std::uint16_t>(id);
    while (ok) {
      std::uint32_t kind = 0;
      if (!payload.read(3, kind)) { ok = false; break; }
      if (kind == 0) break;
      if (kind > static_cast<std::uint32_t>(GameEventValueType::Local)) { ok = false; break; }
      GameEventEntry entry;
      entry.kind = static_cast<GameEventValueType>(kind);
      if (!payload.readString(entry.name, payload.remaining() / 8)) { ok = false; break; }
      definition.entries.push_back(std::move(entry));
    }
    definitions.push_back(std::move(definition));
  }
  if (ok && payload.remaining() != 0) ok = false;  // real data has no padding
  ++summary.gameEventListCount;
  summary.gameEventDefinitionCount += eventCount;
  if (!bits.skip(payloadBits)) return false;
  if (!ok) return false;
  summary.gameEventDefinitions = std::move(definitions);
  summary.gameEventDefinitionsParsed = summary.gameEventDefinitions.size();
  return true;
}

bool readTempEntities(MessageBits& bits, DemoNetworkSummary& summary, std::int32_t packetTick) {
  std::uint32_t count = 0, payloadBits = 0;
  if (!bits.read(8, count)) return false;
  const bool lengthRead = summary.networkProtocol > 23 ? bits.readVarInt(payloadBits) : bits.read(17, payloadBits);
  if (!lengthRead || payloadBits > bits.remaining()) return false;
  summary.tempPayloadBits += payloadBits;
  std::vector<std::uint8_t> payload((static_cast<std::size_t>(payloadBits) + 7u) / 8u, 0);
  for (std::size_t bit = 0; bit < payloadBits; ++bit) {
    std::uint32_t value = 0;
    if (!bits.read(1, value)) return false;
    payload[bit / 8u] |= static_cast<std::uint8_t>(value << (bit % 8u));
  }
  MessageBits body(payload);
  const std::uint32_t eventCount = count == 0 ? 1u : count;
  std::uint32_t lastClassId = 0;
  bool haveClass = false;
  const std::uint32_t serverClassCount = effectiveServerClassCount(summary);
  std::uint32_t classBits = 1;
  while (classBits < 31u && (1u << classBits) <= std::max<std::uint32_t>(1u, serverClassCount)) ++classBits;
  for (std::uint32_t i = 0; i < eventCount; ++i) {
    std::uint32_t hasDelay = 0, delayRaw = 0, hasClass = 0;
    if (!body.read(1, hasDelay)) { ++summary.tempEventDecodeFailures; break; }
    if (hasDelay && !body.read(8, delayRaw)) { ++summary.tempEventDecodeFailures; break; }
    if (!body.read(1, hasClass)) { ++summary.tempEventDecodeFailures; break; }
    if (hasClass) {
      std::uint32_t encodedClass = 0;
      if (!body.read(classBits, encodedClass) || encodedClass == 0 ||
          encodedClass > serverClassCount) { ++summary.tempEventDecodeFailures; break; }
      lastClassId = encodedClass - 1u;
      haveClass = true;
    } else if (!haveClass) {
      ++summary.tempEventDecodeFailures;
      break;
    }
    EntityState eventState;
    eventState.classId = static_cast<std::int32_t>(lastClassId);
    std::string eventClassName;
    for (const auto& classSchema : summary.serverClassSchemas) if (classSchema.id == lastClassId) {
      eventClassName = classSchema.name;
      if (classSchema.name == "CTEClientProjectile") ++summary.tempClientProjectileCount;
      if (classSchema.name == "CTETFParticleEffect") ++summary.tempParticleEffectCount;
      if (classSchema.name == "CTETFExplosion" || classSchema.name == "CTEExplosion") ++summary.tempExplosionCount;
      if (classSchema.name == "CTEFireBullets") ++summary.tempFireBulletsCount;
      if (classSchema.name == "CTEPlayerAnimEvent") ++summary.tempPlayerAnimEventCount;
      break;
    }
    const auto* table = tableForClass(summary, lastClassId);
    if (!readEntityPropUpdates(body, table, eventState, &summary, packetTick, -1, "temp")) {
      if (summary.firstTempEntityFailureTick < 0) {
        summary.firstTempEntityFailureTick = packetTick;
        summary.firstTempEntityFailureClass = static_cast<std::int32_t>(lastClassId);
        summary.firstTempEntityFailureStage = summary.firstEntityPropFailureStage;
        summary.firstTempEntityFailurePayloadBits = static_cast<std::int32_t>(payloadBits);
      }
      ++summary.tempEventDecodeFailures; break;
    }
    ++summary.tempEventHeadersDecoded;
    summary.tempPropDecodedCount += eventState.properties.size();
    if (summary.tempEntityEvents.size() < 16384u) {
      TempEntityEvent event;
      event.tick = packetTick;
      event.className = std::move(eventClassName);
      event.classId = lastClassId;
      event.reliable = count == 0;
      event.hasDelay = hasDelay != 0;
      event.delayRaw = static_cast<std::uint8_t>(delayRaw);
      event.properties = eventState.properties;
      applyTempEntityFields(event);
      if (event.className == "CTEFireBullets") summary.tempFireBulletsFieldHits += event.knownFieldCount;
      else if (event.className == "CTETFParticleEffect") summary.tempParticleEffectFieldHits += event.knownFieldCount;
      else if (event.className == "CTETFExplosion" || event.className == "CTEExplosion") summary.tempExplosionFieldHits += event.knownFieldCount;
      else if (event.className == "CTEPlayerAnimEvent") summary.tempPlayerAnimEventFieldHits += event.knownFieldCount;
      ProjectileTimelineEvent timeline;
      if (projectileFromTempEntity(event, timeline) && summary.projectileTimeline.size() < 16384u) {
        summary.projectileTimeline.push_back(std::move(timeline));
      }
      summary.tempEntityEvents.push_back(std::move(event));
    }
  }
  if (bits.remaining() > 0) { /* payload was already consumed bit-for-bit */ }
  ++summary.tempEntitiesCount;
  summary.tempEventCount += eventCount;
  summary.tempPayloadBits += payloadBits;
  return true;
}

bool findTempEntityEventsInTickRange(const DemoNetworkSummary& summary, std::int32_t firstTick,
                                     std::int32_t lastTick, std::vector<TempEntityEvent>& events) {
  events.clear();
  if (firstTick > lastTick || summary.tempEntityEvents.empty()) return false;
  const auto begin = std::lower_bound(
      summary.tempEntityEvents.begin(), summary.tempEntityEvents.end(), firstTick,
      [](const TempEntityEvent& event, std::int32_t tick) { return event.tick < tick; });
  const auto end = std::upper_bound(
      begin, summary.tempEntityEvents.end(), lastTick,
      [](std::int32_t tick, const TempEntityEvent& event) { return tick < event.tick; });
  events.assign(begin, end);
  return !events.empty();
}

} // namespace tf2::native
