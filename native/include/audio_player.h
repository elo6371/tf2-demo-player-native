#pragma once

#include <cstdint>
#include <array>
#include <string>
#include <utility>
#include <vector>
#include <functional>

#include <Windows.h>
#include <mmsystem.h>

#include "audio_resource.h"
#include "audio_timeline.h"

namespace tf2::native {

// Offline PCM mixer. It never opens an audio device. Pan is -1 (left) to +1
// (right); volume is clamped and samples are saturated to their PCM range.
bool applyPcmPan(std::vector<std::uint8_t>& pcm, std::uint16_t& channels,
  std::uint16_t bitsPerSample, float volume, float pan);

// Process-wide counters for real audio-device opens. Every waveOutOpen attempt
// increments the first; every successful open increments the second. A run
// that injects a playback sink must leave both at zero -- that is what turns
// "no device was opened" from a promise in a comment into a reading a gate can
// pin, and what makes a scheduler that ignores its sink fail the gate instead
// of beeping in someone's ear at 3 a.m.
std::uint32_t audioDeviceOpenAttempts();
std::uint32_t audioDeviceOpenedCount();

// Small single-buffer output used by the native player.  It deliberately
// accepts only PCM WAV data so the caller can schedule decoded events without
// handing compressed or untrusted bytes to the device API.
class AudioPlayer final {
public:
  AudioPlayer() = default;
  ~AudioPlayer();
  AudioPlayer(const AudioPlayer&) = delete;
  AudioPlayer& operator=(const AudioPlayer&) = delete;

  bool play(const WavPcmData& wav, float volume = 1.0f, float pan = 0.0f);
  void setDeviceId(UINT deviceId) { deviceId_ = deviceId; }
  void stop();
  bool playing() const { return playback_ != nullptr; }
  const std::string& lastError() const { return lastError_; }

private:
  struct PlaybackState {
    HWAVEOUT output = nullptr;
    WAVEHDR header{};
    std::vector<std::uint8_t> pcm;
  };

  UINT deviceId_ = WAVE_MAPPER;
  PlaybackState* playback_ = nullptr;
  std::string lastError_;
};

struct AudioScheduleStats {
  std::size_t eventsSeen = 0;
  std::size_t eventsPlayed = 0;
  std::size_t eventsSkippedVoice = 0;
  std::size_t eventsMissingResource = 0;
  // Events the device-free sink answered with an explicit `skipped` result
  // (--audio-disabled). Counted apart from eventsMissingResource on purpose:
  // the resource resolved and the event was deliberately not played, which is
  // a different state from "nothing was found".
  std::size_t eventsSkippedDisabled = 0;
  // advance() calls refused because the tick had already been consumed. The
  // player calls advance() once per rendered frame while the timeline moves at
  // the demo tick rate, so this is expected to be large; what it proves is that
  // the guard fires, which is what keeps a repeated tick from replaying its
  // events. Zero means every call was a fresh tick, i.e. the guard was never
  // exercised and "the same tick does not replay" was not tested.
  std::size_t duplicateTicks = 0;
  // reset() calls that moved the cursor backwards: the reverse key, a seek, or a
  // tick regression re-seeding the scheduler so the range it already consumed
  // can be replayed. Without this a reverse run looks identical to a stalled one.
  std::size_t reverseRecoveries = 0;
};

// Tick boundary adapter. Resource resolution stays injectable so the demo
// parser never needs to know about VPK ownership or an audio device.
class AudioEventScheduler final {
public:
  using Resolver = std::function<bool(const std::string&, WavPcmData&)>;
  using PlaybackSink = std::function<bool(const WavPcmData&, float, float)>;
  // A sink result that can say "deliberately not played" as itself. The bool
  // sink above cannot: `false` there means "failed", and a test mode whose
  // refusals were counted as failures would make the quiet-mode readings
  // unreadable. The two sinks are mutually exclusive; setting one clears the
  // other so "which sink ran" is never ambiguous.
  enum class PlaybackOutcome { played, skipped, failed };
  using OutcomeSink = std::function<PlaybackOutcome(const WavPcmData&, float, float)>;

  void setResolver(Resolver resolver) { resolver_ = std::move(resolver); }
  void setPlaybackSink(PlaybackSink sink) { playbackSink_ = std::move(sink); outcomeSink_ = nullptr; }
  void setOutcomeSink(OutcomeSink sink) { outcomeSink_ = std::move(sink); playbackSink_ = nullptr; }
  void setDeviceId(UINT deviceId) { for (auto& player : players_) player.setDeviceId(deviceId); }
  void reset(std::int32_t tick = 0);
  AudioScheduleStats advance(const SoundEventTimeline& timeline, std::int32_t tick);
  void stop();
  const AudioScheduleStats& stats() const { return stats_; }
  std::int32_t lastTick() const { return lastTick_; }
  bool initialized() const { return initialized_; }
  bool stopped() const { return stopped_; }
  std::size_t activeVoiceCount() const;
  void setListenerPosition(float x, float y, float z);
  SpatialMix lastSpatialMix() const { return lastSpatialMix_; }

private:
  static constexpr std::size_t kVoiceCount = 8;
  std::array<AudioPlayer, kVoiceCount> players_{};
  std::size_t nextVoice_ = 0;
  Resolver resolver_;
  PlaybackSink playbackSink_;
  OutcomeSink outcomeSink_;
  std::int32_t lastTick_ = -1;
  bool initialized_ = false;
  bool stopped_ = true;
  float listener_[3] = {0.0f, 0.0f, 0.25f};
  bool listenerValid_ = false;
  SpatialMix lastSpatialMix_{};
  AudioScheduleStats stats_{};
};

// Number of waveOutOpen calls this process has made, across every AudioPlayer.
// The quiet-capture contract ("no audio device is opened") is asserted against
// this count rather than against the flag that requested the mode: a run that
// ignores --audio-disabled still calls waveOutOpen, and only a real call
// counter can tell the two apart. Counted at the call site, so an open that
// fails still counts -- the contract is that the device was not even asked for.
std::size_t waveOutOpenCalls();

} // namespace tf2::native
