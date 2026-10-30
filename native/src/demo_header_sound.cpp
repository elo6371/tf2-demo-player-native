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

bool readSounds(MessageBits& bits, DemoNetworkSummary& summary, std::int32_t packetTick) {
  std::uint32_t reliable = 0;
  std::uint32_t soundCount = 0;
  std::uint32_t payloadBits = 0;
  if (!bits.read(1, reliable)) return false;
  if (reliable) {
    soundCount = 1;
    if (!bits.read(8, payloadBits)) return false;
  } else {
    if (!bits.read(8, soundCount) || !bits.read(16, payloadBits)) return false;
    if (soundCount == 0) return false;
  }
  if (payloadBits > bits.remaining()) return false;
  RawSoundMessage raw;
  raw.tick = packetTick;
  raw.reliable = reliable != 0;
  raw.count = static_cast<std::uint8_t>(soundCount);
  raw.payloadBits = static_cast<std::uint16_t>(payloadBits);
  std::vector<std::uint8_t> soundPayload;
  if (payloadBits < 64u * 1024u && summary.rawSoundMessages.size() < 4096u) {
    raw.payload.assign((static_cast<std::size_t>(payloadBits) + 7u) / 8u, 0);
    for (std::size_t bit = 0; bit < payloadBits; ++bit) {
      std::uint32_t value = 0;
      if (!bits.read(1, value)) return false;
      raw.payload[bit / 8u] |= static_cast<std::uint8_t>(value << (bit % 8u));
    }
    soundPayload = raw.payload;
    summary.rawSoundMessages.push_back(std::move(raw));
  } else {
    ++summary.rawSoundMessagesDropped;
    if (!bits.skip(payloadBits)) return false;
  }
  MessageBits soundBits(soundPayload);
  auto readSigned = [&](std::uint32_t width, std::int32_t& value) {
    std::uint32_t rawValue = 0;
    if (!soundBits.read(width, rawValue)) return false;
    if (width == 0 || width > 32) return false;
    if (width == 32) { value = static_cast<std::int32_t>(rawValue); return true; }
    const std::uint32_t sign = 1u << (width - 1u);
    value = static_cast<std::int32_t>((rawValue & sign) ? (rawValue | (~0u << width)) : rawValue);
    return true;
  };
  for (std::uint32_t eventIndex = 0; eventIndex < soundCount; ++eventIndex) {
    DecodedSoundEvent event = summary.soundDeltaStateValid ? summary.soundDeltaState : DecodedSoundEvent{};
    event.tick = packetTick;
    std::uint32_t value = 0;
    if (!soundBits.read(1, value)) { ++summary.decodedSoundEventFailures; break; }
    if (value) {
      if (!soundBits.read(1, value)) { ++summary.decodedSoundEventFailures; break; }
      if (value) { if (!soundBits.read(5, value)) { ++summary.decodedSoundEventFailures; break; } }
      else if (!soundBits.read(11, value)) { ++summary.decodedSoundEventFailures; break; }
      event.entityIndex = static_cast<std::int32_t>(value);
    }
    if (!soundBits.read(1, value)) { ++summary.decodedSoundEventFailures; break; }
    if (value) { if (!soundBits.read(14, value)) { ++summary.decodedSoundEventFailures; break; } event.soundIndex = static_cast<std::uint16_t>(value); }
    if (!soundBits.read(1, value)) { ++summary.decodedSoundEventFailures; break; }
    if (value) { if (!soundBits.read(11, value)) { ++summary.decodedSoundEventFailures; break; } event.flags = static_cast<std::uint16_t>(value); }
    if (!soundBits.read(1, value)) { ++summary.decodedSoundEventFailures; break; }
    if (value) { if (!soundBits.read(3, value)) { ++summary.decodedSoundEventFailures; break; } event.channel = static_cast<std::uint8_t>(value); }
    if (!soundBits.read(1, value)) { ++summary.decodedSoundEventFailures; break; } event.ambient = value != 0;
    if (!soundBits.read(1, value)) { ++summary.decodedSoundEventFailures; break; } event.sentence = value != 0;
    if ((event.flags & 4u) == 0) {
      if (!soundBits.read(1, value)) { ++summary.decodedSoundEventFailures; break; }
      if (!value) { if (!soundBits.read(1, value)) { ++summary.decodedSoundEventFailures; break; } if (!value && !soundBits.read(10, value)) { ++summary.decodedSoundEventFailures; break; } }
      if (!soundBits.read(1, value)) { ++summary.decodedSoundEventFailures; break; }
      if (value && (!soundBits.read(7, value))) { ++summary.decodedSoundEventFailures; break; }
      if (value) event.volume = static_cast<float>(value) / 127.0f;
      if (!soundBits.read(1, value)) { ++summary.decodedSoundEventFailures; break; }
      if (value && !soundBits.read(9, value)) { ++summary.decodedSoundEventFailures; break; }
      if (!soundBits.read(1, value)) { ++summary.decodedSoundEventFailures; break; }
      if (value && !soundBits.read(8, value)) { ++summary.decodedSoundEventFailures; break; }
      if (value) event.pitch = static_cast<std::uint8_t>(value);
      if (summary.networkProtocol > 21) { if (!soundBits.read(1, value)) { ++summary.decodedSoundEventFailures; break; } if (value && !soundBits.read(8, value)) { ++summary.decodedSoundEventFailures; break; } }
      if (!soundBits.read(1, value)) { ++summary.decodedSoundEventFailures; break; }
      if (value) {
        std::int32_t delay = 0;
        if (!readSigned(13, delay)) { ++summary.decodedSoundEventFailures; break; }
        event.delaySeconds = static_cast<float>(delay) / 1000.0f;
        if (event.delaySeconds < 0.0f) event.delaySeconds *= 10.0f;
        event.delaySeconds -= 0.100f;
      }
      bool failed = false;
      for (float& coordinate : event.origin) {
        if (!soundBits.read(1, value)) { failed = true; break; }
        if (value) {
          std::int32_t rawCoordinate = 0;
          if (!readSigned(12, rawCoordinate)) { failed = true; break; }
          coordinate = static_cast<float>(rawCoordinate) * 8.0f;
        }
      }
      if (failed) { ++summary.decodedSoundEventFailures; break; }
      if (!soundBits.read(1, value)) { ++summary.decodedSoundEventFailures; break; }
      if (value) {
        std::int32_t speakerEntity = 0;
        if (!readSigned(12, speakerEntity)) { ++summary.decodedSoundEventFailures; break; }
      }
    }
    event.valid = event.soundIndex != 0;
    summary.soundDeltaState = event;
    summary.soundDeltaStateValid = true;
    if (event.valid && summary.decodedSoundEvents.size() < 16384u) summary.decodedSoundEvents.push_back(event);
  }
  ++summary.soundMessageCount;
  summary.soundEventCount += soundCount;
  summary.reliableSoundCount += reliable != 0 ? 1u : 0u;
  summary.soundPayloadBits += payloadBits;
  return true;
}

SoundTimelineBuildCounts buildSoundTimeline(const std::vector<DecodedSoundEvent>& events,
    bool originFound, std::int32_t originTick, double tickRate, SoundEventTimeline& timeline) {
  SoundTimelineBuildCounts counts;
  timeline.clear();
  counts.read = events.size();
  for (const auto& source : events) {
    // A decoded event with no precache entry has no name to resolve, so there is
    // nothing to schedule. Counted, not silently dropped: it is one of the named
    // failure points the resolver reports.
    if (source.resourceName.empty()) { ++counts.unresolved; continue; }
    // No origin at all means the recording's own tick counter is unknown. Every
    // event is then unplaceable; refusing them keeps the failure countable
    // instead of scheduling them at their absolute ticks, which would never play
    // and would read exactly like "this recording has no sounds".
    std::int32_t relativeTick = 0;
    if (!originFound || !soundEventRelativeTick(source.tick, originTick, relativeTick)) {
      ++counts.unplaceable;
      continue;
    }
    ScheduledSound sound;
    sound.tick = relativeTick;
    sound.soundIndex = source.soundIndex;
    sound.name = source.resourceName;
    sound.volume = source.volume;
    sound.delaySeconds = source.delaySeconds;
    sound.origin[0] = source.origin[0];
    sound.origin[1] = source.origin[1];
    sound.origin[2] = source.origin[2];
    // scheduleSoundEvent rather than scheduleGameplaySound: voice, announcer and
    // music stay in the timeline flagged as skipped so the scheduler's
    // eventsSkippedVoice counter is a reading that can move. They are still never
    // resolved and never played.
    if (!scheduleSoundEvent(timeline, std::move(sound), tickRate)) ++counts.refused;
  }
  timeline.sortByTick();
  return counts;
}

} // namespace tf2::native
