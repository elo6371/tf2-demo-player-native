#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "audio_resource.h"

namespace tf2::native {

// Nine kinds, distinguished by path tokens in the sound name. The first five
// keep their original numeric values so stored readings and any code that
// casts the enum stay valid. Voice, announcer and music are the filtered
// kinds: they stay in the timeline so the scheduler can count them as skipped,
// and scheduleGameplaySound refuses them before that. TempEntity and Pcf are
// reserved for sound names routed from the temp-entity and particle channels;
// the shipped VPKs contain no such name (audio_scheduler_probe measures that
// instead of assuming it), so on real data those two read zero.
enum class SoundEventKind : std::uint8_t {
  Unknown = 0,
  Weapon,
  Footstep,
  Uber,
  World,
  TempEntity,
  Pcf,
  Voice,
  Announcer,
  Music,
};

const char* soundEventKindName(SoundEventKind kind);

// Named outcome of the chain demo sound index -> precache name -> VPK WAV path
// -> parsed WAV -> playable PCM. Each failure point in the chain gets its own
// reason so a caller can tell "no such index" from "no such file" from "broken
// bytes" from "bytes the output path cannot play"; one boolean would collapse
// all four into the same shrug.
enum class SoundResolveStatus : std::uint8_t {
  Resolved = 0,
  MissingPrecache,
  MissingWav,
  InvalidWav,
  UnsupportedFormat,
};

const char* soundResolveStatusName(SoundResolveStatus status);

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
  // Set by SoundEventTimeline::add for the filtered kinds (voice, announcer,
  // music). The scheduler counts those events as skipped and never resolves or
  // plays them.
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

// Path-token match for vo/voice/announcer/radio/music/commentary. This is the
// filtered set: classifySoundEvent splits it into the voice, announcer and
// music kinds, and this predicate is their union.
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
  std::size_t tempEntity = 0;
  std::size_t pcf = 0;
  std::size_t voice = 0;
  std::size_t announcer = 0;
  std::size_t music = 0;
};

class SoundEventTimeline {
public:
  void clear();
  // Classifies by name (the caller's kind field is overwritten) and keeps the
  // filtered kinds flagged as skipped instead of dropping them, so
  // AudioScheduleStats::eventsSkippedVoice is a counter something can move.
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

// Walks the whole chain for one sound index against a real VPK archive and
// names the failure point: no precache entry, no WAV/MP3 entry, bytes that do
// not parse, or a resource the native PCM output path cannot play. On success
// out carries the parsed WAV. Never opens an audio device.
//
// The size cap is deliberately absent: which indices get resolved is the
// caller's decision (the product path caps through readSoundResource and its
// own cache), and inventing a fifth status for "too big" would blur the four
// named failure points.
SoundResolveStatus resolveSoundIndexResource(const SoundIndexTable& table,
  const VpkArchive& archive, std::uint32_t soundIndex,
  SoundResource& out, std::string& detail);

// True when a parsed WAV is one the WinMM output path can play as-is: PCM
// encoding, 8 or 16 bits, 1 or 2 channels, non-empty data aligned to the
// block. Anything else is a resource the caller must count, not bytes to hand
// to a device.
//
// Declared here rather than in audio_player.h because the resolver above needs
// it and audio_player.h includes this header; the scheduler in audio_player.cpp
// uses the same predicate so the two paths cannot disagree about playability.
bool wavPcmIsPlayable(const WavPcmData& wav);

} // namespace tf2::native
