#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "audio_resource.h"

namespace tf2::native {

enum class SoundEventKind : std::uint8_t {
  Unknown = 0,
  Weapon,
  Footstep,
  Uber,
  World,
};

struct SoundIndexEntry {
  std::uint32_t index = 0;
  std::string name;
  SoundEventKind kind = SoundEventKind::Unknown;
};

class SoundIndexTable {
public:
  void clear();
  bool set(std::uint32_t index, std::string name);
  const SoundIndexEntry* find(std::uint32_t index) const;
  std::size_t size() const { return entries_.size(); }

private:
  std::unordered_map<std::uint32_t, SoundIndexEntry> entries_;
};

struct SoundPlaybackEvent {
  std::int32_t tick = 0;
  std::uint32_t soundIndex = 0;
  std::string name;
  SoundEventKind kind = SoundEventKind::Unknown;
  float volume = 1.0f;
  float origin[3] = {0.0f, 0.0f, 0.0f};
  bool skipVoice = true;
};

struct SpatialMix {
  float gain = 0.0f;
  float pan = 0.0f;
  bool isSilent() const { return gain == 0.0f; }
  bool isCentered() const { return pan == 0.0f; }
};

// Pure calculation helper. The listener faces +X and +Y is its right side.
// At zero distance it returns gain=1, pan=0; at or beyond maxDistance it
// returns silence; source +Y/-Y produce pan +1/-1. Invalid or NaN input is
// silent. It never touches an audio device.
SpatialMix computeSpatialMix(const float source[3], const float listener[3],
  float maxDistance = 2048.0f, float rolloff = 1.0f);

// Path-token match for vo/voice/announcer/radio/music/commentary.
bool soundNameIsVoiceOrMusic(const std::string& name);

// Weapon, footstep, then Uber, then world. Voice names still classify, and
// the scheduler drops them before playback.
SoundEventKind classifySoundEvent(const std::string& name);

// playbackTick + round(delaySeconds * tickRate). A negative result clamps to
// 0. Returns -1 when the tick, delay, or tick rate cannot be scheduled.
std::int32_t applySoundDelayTicks(std::int32_t tick, float delaySeconds, double tickRate);

struct ScheduledSound {
  std::int32_t tick = 0;
  std::uint32_t soundIndex = 0;
  std::string name;
  float volume = 1.0f;
  float delaySeconds = 0.0f;
  float origin[3] = {0.0f, 0.0f, 0.0f};
};

struct SoundKindCounts {
  std::size_t weapon = 0;
  std::size_t footstep = 0;
  std::size_t uber = 0;
  std::size_t world = 0;
};

class SoundEventTimeline {
public:
  void clear();
  bool add(SoundPlaybackEvent event);
  void sortByTick();
  std::size_t size() const { return events_.size(); }
  bool empty() const { return events_.empty(); }
  std::size_t countAtTick(std::int32_t tick) const;
  const std::vector<SoundPlaybackEvent>& events() const { return events_; }
  std::vector<SoundPlaybackEvent> range(std::int32_t firstTick, std::int32_t lastTick) const;
  void forEachRange(std::int32_t firstTick, std::int32_t lastTick,
    const std::function<void(const SoundPlaybackEvent&)>& visitor) const;

private:
  std::vector<SoundPlaybackEvent> events_;
};

// Classifies the name, folds delay into the playback tick, and drops voice,
// announcer, and music. Does not open an audio device.
bool scheduleGameplaySound(SoundEventTimeline& timeline, ScheduledSound sound, double tickRate);

SoundKindCounts countSoundKinds(const SoundEventTimeline& timeline);

// Resolves a precache name to WAV or MP3 bytes. WAV data is parsed; MP3 is
// only signature-validated because codec decoding belongs to the native audio
// backend. No function here claims that bytes have been scheduled for playback.
bool readSoundResource(const VpkArchive& archive, const std::string& name,
  SoundResource& out, std::size_t maxBytes = 64u * 1024u * 1024u);

} // namespace tf2::native
