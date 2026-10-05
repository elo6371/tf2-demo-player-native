#include "audio_player.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace tf2::native {
namespace {

std::string mmError(MMRESULT result, const char* operation) {
  char buffer[256]{};
  if (waveOutGetErrorTextA(result, buffer, static_cast<UINT>(sizeof(buffer))) == MMSYSERR_NOERROR) {
    return std::string(operation) + ": " + buffer;
  }
  return std::string(operation) + " failed (MMRESULT=" + std::to_string(result) + ")";
}

std::int16_t readS16(const std::uint8_t* bytes) {
  return static_cast<std::int16_t>(static_cast<std::uint16_t>(bytes[0])
    | (static_cast<std::uint16_t>(bytes[1]) << 8u));
}

void writeS16(std::uint8_t* bytes, std::int32_t sample) {
  const auto bounded = std::clamp(sample, -32768, 32767);
  bytes[0] = static_cast<std::uint8_t>(bounded & 0xff);
  bytes[1] = static_cast<std::uint8_t>((bounded >> 8) & 0xff);
}

} // namespace

bool applyPcmPan(std::vector<std::uint8_t>& pcm, std::uint16_t& channels,
    std::uint16_t bitsPerSample, float volume, float pan) {
  if ((channels != 1 && channels != 2) || (bitsPerSample != 8 && bitsPerSample != 16)
      || !std::isfinite(volume) || !std::isfinite(pan) || pcm.empty()) return false;
  const float clampedVolume = std::clamp(volume, 0.0f, 1.0f);
  const float clampedPan = std::clamp(pan, -1.0f, 1.0f);
  const std::size_t bytesPerSample = bitsPerSample / 8u;
  const std::size_t inputFrameBytes = bytesPerSample * channels;
  if (pcm.size() % inputFrameBytes != 0) return false;
  const float leftGain = clampedVolume * (clampedPan > 0.0f ? 1.0f - clampedPan : 1.0f);
  const float rightGain = clampedVolume * (clampedPan < 0.0f ? 1.0f + clampedPan : 1.0f);
  if (channels == 1) {
    std::vector<std::uint8_t> stereo;
    if (pcm.size() > (std::numeric_limits<std::size_t>::max() / 2u)) return false;
    stereo.resize(pcm.size() * 2u);
    for (std::size_t in = 0, out = 0; in < pcm.size(); in += bytesPerSample, out += bytesPerSample * 2u) {
      if (bitsPerSample == 8) {
        const float centered = static_cast<float>(static_cast<int>(pcm[in]) - 128);
        stereo[out] = static_cast<std::uint8_t>(std::clamp(static_cast<int>(std::lround(128.0f + centered * leftGain)), 0, 255));
        stereo[out + 1] = static_cast<std::uint8_t>(std::clamp(static_cast<int>(std::lround(128.0f + centered * rightGain)), 0, 255));
      } else {
        const auto sample = static_cast<float>(readS16(&pcm[in]));
        writeS16(&stereo[out], static_cast<std::int32_t>(std::lround(sample * leftGain)));
        writeS16(&stereo[out + 2], static_cast<std::int32_t>(std::lround(sample * rightGain)));
      }
    }
    pcm = std::move(stereo);
    channels = 2;
    return true;
  }
  for (std::size_t at = 0; at < pcm.size(); at += bytesPerSample * 2u) {
    if (bitsPerSample == 8) {
      const auto left = static_cast<float>(static_cast<int>(pcm[at]) - 128);
      const auto right = static_cast<float>(static_cast<int>(pcm[at + 1]) - 128);
      pcm[at] = static_cast<std::uint8_t>(std::clamp(static_cast<int>(std::lround(128.0f + left * leftGain)), 0, 255));
      pcm[at + 1] = static_cast<std::uint8_t>(std::clamp(static_cast<int>(std::lround(128.0f + right * rightGain)), 0, 255));
    } else {
      writeS16(&pcm[at], static_cast<std::int32_t>(std::lround(readS16(&pcm[at]) * leftGain)));
      writeS16(&pcm[at + 2], static_cast<std::int32_t>(std::lround(readS16(&pcm[at + 2]) * rightGain)));
    }
  }
  return true;
}

AudioPlayer::~AudioPlayer() { stop(); }

bool AudioPlayer::play(const WavPcmData& wav, float volume, float pan) {
  stop();
  if (playback_) return false;
  lastError_.clear();
  if (!wav.valid || wav.encoding != WavEncoding::Pcm || wav.pcm.empty()) {
    lastError_ = "only non-empty PCM WAV data can be played";
    return false;
  }
  const auto bytesPerSample = static_cast<std::uint32_t>(wav.bitsPerSample / 8u);
  const auto expectedBlockAlign = static_cast<std::uint32_t>(wav.channels) * bytesPerSample;
  const auto expectedByteRate = static_cast<std::uint64_t>(wav.sampleRate) * expectedBlockAlign;
  if ((wav.bitsPerSample != 8 && wav.bitsPerSample != 16) || wav.channels == 0
      || wav.channels > 2 || wav.sampleRate == 0 || wav.blockAlign == 0
      || expectedBlockAlign == 0 || wav.blockAlign != expectedBlockAlign
      || expectedByteRate > std::numeric_limits<std::uint32_t>::max()
      || wav.byteRate != expectedByteRate || wav.pcm.size() % wav.blockAlign != 0
      || wav.pcm.size() > std::numeric_limits<DWORD>::max() || !std::isfinite(volume)) {
    lastError_ = "WAV format is unsupported by the native output path";
    return false;
  }

  std::vector<std::uint8_t> mixedPcm = wav.pcm;
  std::uint16_t outputChannels = wav.channels;
  if (!applyPcmPan(mixedPcm, outputChannels, wav.bitsPerSample, volume, pan)) {
    lastError_ = "PCM pan conversion failed";
    return false;
  }
  if (mixedPcm.size() > std::numeric_limits<DWORD>::max()) {
    lastError_ = "mixed PCM buffer exceeds WinMM limit";
    return false;
  }
  WAVEFORMATEX format{};
  format.wFormatTag = WAVE_FORMAT_PCM;
  format.nChannels = outputChannels;
  format.nSamplesPerSec = wav.sampleRate;
  format.wBitsPerSample = wav.bitsPerSample;
  format.nBlockAlign = static_cast<WORD>(outputChannels * (wav.bitsPerSample / 8u));
  format.nAvgBytesPerSec = wav.sampleRate * format.nBlockAlign;
  format.cbSize = 0;
  auto* playback = new PlaybackState{};
  MMRESULT result = waveOutOpen(&playback->output, deviceId_, &format, 0, 0, CALLBACK_NULL);
  if (result != MMSYSERR_NOERROR) {
    delete playback;
    lastError_ = mmError(result, "waveOutOpen");
    return false;
  }
  playback_ = playback;

  playback_->pcm = std::move(mixedPcm);
  format.nChannels = outputChannels;
  format.nBlockAlign = static_cast<WORD>(outputChannels * (wav.bitsPerSample / 8u));
  format.nAvgBytesPerSec = wav.sampleRate * format.nBlockAlign;
  playback_->header.lpData = reinterpret_cast<LPSTR>(playback_->pcm.data());
  playback_->header.dwBufferLength = static_cast<DWORD>(playback_->pcm.size());
  result = waveOutPrepareHeader(playback_->output, &playback_->header, sizeof(playback_->header));
  if (result != MMSYSERR_NOERROR) {
    lastError_ = mmError(result, "waveOutPrepareHeader");
    stop();
    return false;
  }
  result = waveOutWrite(playback_->output, &playback_->header, sizeof(playback_->header));
  if (result != MMSYSERR_NOERROR) {
    lastError_ = mmError(result, "waveOutWrite");
    stop();
    return false;
  }
  return true;
}

void AudioPlayer::stop() {
  if (!playback_) {
    return;
  }
  std::string cleanupError;
  MMRESULT result = waveOutReset(playback_->output);
  if (result != MMSYSERR_NOERROR) cleanupError = mmError(result, "waveOutReset");
  if (playback_->header.dwFlags & WHDR_PREPARED) {
    result = waveOutUnprepareHeader(playback_->output, &playback_->header, sizeof(playback_->header));
    if (result != MMSYSERR_NOERROR && cleanupError.empty())
      cleanupError = mmError(result, "waveOutUnprepareHeader");
  }
  result = waveOutClose(playback_->output);
  if (result != MMSYSERR_NOERROR && cleanupError.empty())
    cleanupError = mmError(result, "waveOutClose");
  delete playback_;
  playback_ = nullptr;
  if (!cleanupError.empty()) lastError_ = std::move(cleanupError);
}

void AudioEventScheduler::reset(std::int32_t tick) {
  lastTick_ = initialized_ ? tick : tick - 1;
  initialized_ = true;
  stats_ = {};
  nextVoice_ = 0;
  for (auto& player : players_) player.stop();
  stopped_ = activeVoiceCount() != 0;
}

void AudioEventScheduler::stop() {
  for (auto& player : players_) player.stop();
  stopped_ = true;
}

void AudioEventScheduler::setListenerPosition(float x, float y, float z) {
  if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) {
    listenerValid_ = false;
    lastSpatialMix_ = {};
    return;
  }
  listener_[0] = x; listener_[1] = y; listener_[2] = z;
  listenerValid_ = true;
}

std::size_t AudioEventScheduler::activeVoiceCount() const {
  std::size_t active = 0;
  for (const auto& player : players_) {
    if (player.playing()) ++active;
  }
  return active;
}

AudioScheduleStats AudioEventScheduler::advance(const SoundEventTimeline& timeline, std::int32_t tick) {
  if (tick < 0 || stopped_) return stats_;
  if (lastTick_ >= tick) return stats_;
  stopped_ = false;
  timeline.forEachRange(lastTick_ + 1, tick, [&](const auto& event) {
    ++stats_.eventsSeen;
    if (event.skipVoice) {
      ++stats_.eventsSkippedVoice;
      return;
    }
    if (!resolver_) {
      ++stats_.eventsMissingResource;
      return;
    }
    WavPcmData wav;
    auto& player = players_[nextVoice_++ % kVoiceCount];
    lastSpatialMix_ = listenerValid_ ? computeSpatialMix(event.origin, listener_) : SpatialMix{1.0f, 0.0f};
    const float volume = event.volume * lastSpatialMix_.gain;
    if (!resolver_(event.name, wav) || !wav.valid || lastSpatialMix_.isSilent()) {
      ++stats_.eventsMissingResource;
      return;
    }
    const bool pcmShapeValid = wav.encoding == WavEncoding::Pcm
      && (wav.bitsPerSample == 8 || wav.bitsPerSample == 16)
      && wav.channels >= 1 && wav.channels <= 2 && wav.sampleRate != 0
      && wav.blockAlign != 0 && wav.pcm.size() % wav.blockAlign == 0;
    const bool played = pcmShapeValid && (playbackSink_
      ? playbackSink_(wav, volume, lastSpatialMix_.pan)
      : player.play(wav, volume, lastSpatialMix_.pan));
    if (!played) {
      ++stats_.eventsMissingResource;
      return;
    }
    ++stats_.eventsPlayed;
  });
  lastTick_ = tick;
  return stats_;
}

} // namespace tf2::native
