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
};

// Tick boundary adapter. Resource resolution stays injectable so the demo
// parser never needs to know about VPK ownership or an audio device.
class AudioEventScheduler final {
public:
  using Resolver = std::function<bool(const std::string&, WavPcmData&)>;
  using PlaybackSink = std::function<bool(const WavPcmData&, float, float)>;

  void setResolver(Resolver resolver) { resolver_ = std::move(resolver); }
  void setPlaybackSink(PlaybackSink sink) { playbackSink_ = std::move(sink); }
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
  std::int32_t lastTick_ = -1;
  bool initialized_ = false;
  bool stopped_ = true;
  float listener_[3] = {0.0f, 0.0f, 0.25f};
  bool listenerValid_ = false;
  SpatialMix lastSpatialMix_{};
  AudioScheduleStats stats_{};
};

} // namespace tf2::native
