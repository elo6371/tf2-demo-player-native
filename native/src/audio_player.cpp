#include "audio_player.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <utility>

namespace tf2::native {
namespace {

std::size_t g_waveOutOpenCallCount = 0;
// Counted at the waveOutOpen boundary, the only place this backend can reach an
// audio device. Attempts and successes are separate readings because the
// defect shape that matters here -- playback routed to a real player while a
// sink was injected -- must be visible even when the open itself fails (an
// out-of-range device id), which is how the probe exercises it without
// producing sound.
std::atomic<std::uint32_t> g_deviceOpenAttempts{0};
std::atomic<std::uint32_t> g_deviceOpenedCount{0};
// Set only by installWinMmBackend(); null means "the real entry points".
const WinMmBackend* g_installedBackend = nullptr;
// Cleanup outcomes. Counted at the call sites in stop(), not derived from
// lastError(): a string cannot be summed, and the failure this replaced was
// exactly an outcome nobody could read.
std::atomic<std::uint32_t> g_deviceClosedCount{0};
std::atomic<std::uint32_t> g_cleanupFailureCount{0};
std::atomic<std::uint32_t> g_deadHandleCount{0};
// Buffers currently owned. Incremented once the state is published and
// decremented only by releaseState(), so a non-zero value means the driver
// still owns memory this process is holding on purpose.
std::atomic<std::size_t> g_retainedStateCount{0};
std::atomic<std::uint64_t> g_retainedByteCount{0};
std::atomic<std::uint32_t> g_retainedAtExitCount{0};

std::string mmError(MMRESULT result, const char* operation) {
  char buffer[256]{};
  if (winMmBackend().errorText(result, buffer, static_cast<UINT>(sizeof(buffer))) == MMSYSERR_NOERROR) {
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

const WinMmBackend& productionWinMmBackend() {
  static const WinMmBackend backend{};
  return backend;
}

const WinMmBackend& winMmBackend() {
  const WinMmBackend* installed = g_installedBackend;
  return installed != nullptr ? *installed : productionWinMmBackend();
}

void installWinMmBackend(const WinMmBackend* backend) { g_installedBackend = backend; }

std::size_t waveOutOpenCalls() { return g_waveOutOpenCallCount; }
std::uint32_t audioDeviceOpenAttempts() {
  return g_deviceOpenAttempts.load(std::memory_order_relaxed);
}

std::uint32_t audioDeviceOpenedCount() {
  return g_deviceOpenedCount.load(std::memory_order_relaxed);
}

std::uint32_t audioDeviceClosedCount() {
  return g_deviceClosedCount.load(std::memory_order_relaxed);
}

std::uint32_t audioCleanupFailureCount() {
  return g_cleanupFailureCount.load(std::memory_order_relaxed);
}

std::uint32_t audioDeadHandleCount() {
  return g_deadHandleCount.load(std::memory_order_relaxed);
}

std::size_t audioRetainedStateCount() {
  return g_retainedStateCount.load(std::memory_order_relaxed);
}

std::uint64_t audioRetainedByteCount() {
  return g_retainedByteCount.load(std::memory_order_relaxed);
}

std::uint32_t audioRetainedAtExitCount() {
  return g_retainedAtExitCount.load(std::memory_order_relaxed);
}

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

AudioPlayer::~AudioPlayer() {
  stop();
  // The destructor is the last cleanup opportunity. If the driver still holds
  // the buffer, the state is kept rather than freed: freeing it would hand the
  // device memory the allocator is about to reuse, and that is a crash in a
  // driver thread with no line of product code to point at. Counted, so the
  // leak is a reading instead of a surprise.
  if (playback_ != nullptr) g_retainedAtExitCount.fetch_add(1, std::memory_order_relaxed);
}
bool AudioPlayer::play(const WavPcmData& wav, float volume, float pan) {
  // A voice the driver will not release is not a retry point. Retrying here
  // repeats the failing cleanup on every scheduled sound -- one device fault
  // becomes a stream of driver calls that buries the error which caused it.
  // The retry is stop(), which the caller of a failed play runs anyway.
  if (stuck_) return false;
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
  ++g_waveOutOpenCallCount;
  g_deviceOpenAttempts.fetch_add(1, std::memory_order_relaxed);
  MMRESULT result = winMmBackend().open(&playback->output, deviceId_, &format, 0, 0, CALLBACK_NULL);
  if (result == MMSYSERR_NOERROR) g_deviceOpenedCount.fetch_add(1, std::memory_order_relaxed);
  if (result != MMSYSERR_NOERROR) {
    delete playback;
    lastError_ = mmError(result, "waveOutOpen");
    return false;
  }
  playback_ = playback;

  playback_->pcm = std::move(mixedPcm);
  g_retainedStateCount.fetch_add(1, std::memory_order_relaxed);
  g_retainedByteCount.fetch_add(static_cast<std::uint64_t>(playback_->pcm.size()),
    std::memory_order_relaxed);
  format.nChannels = outputChannels;
  format.nBlockAlign = static_cast<WORD>(outputChannels * (wav.bitsPerSample / 8u));
  format.nAvgBytesPerSec = wav.sampleRate * format.nBlockAlign;
  playback_->header.lpData = reinterpret_cast<LPSTR>(playback_->pcm.data());
  playback_->header.dwBufferLength = static_cast<DWORD>(playback_->pcm.size());
  result = winMmBackend().prepare(playback_->output, &playback_->header, sizeof(playback_->header));
  if (result != MMSYSERR_NOERROR) {
    // The root cause has to outlive the cleanup. stop() can fail too, and that
    // failure describes the cleanup, not why playback never started -- before
    // this the last writer won and lastError() named the wrong step (QA2-03).
    const std::string primary = mmError(result, "waveOutPrepareHeader");
    stop();
    lastError_ = primary;
    return false;
  }
  prepared_ = true;
  result = winMmBackend().write(playback_->output, &playback_->header, sizeof(playback_->header));
  if (result != MMSYSERR_NOERROR) {
    const std::string primary = mmError(result, "waveOutWrite");
    stop();
    lastError_ = primary;
    return false;
  }
  return true;
}

void AudioPlayer::releaseState() {
  if (playback_ == nullptr) return;
  g_retainedStateCount.fetch_sub(1, std::memory_order_relaxed);
  g_retainedByteCount.fetch_sub(static_cast<std::uint64_t>(playback_->pcm.size()),
    std::memory_order_relaxed);
  delete playback_;
  playback_ = nullptr;
  prepared_ = false;
  stuck_ = false;
}

void AudioPlayer::noteCleanupFailure(MMRESULT result, const char* operation) {
  const std::string message = mmError(result, operation);
  lastCleanupError_ = message;
  // Also the current error: for a plain stop() this *is* the reason the voice
  // is still open, and play() restores its own root cause after calling stop().
  lastError_ = message;
  g_cleanupFailureCount.fetch_add(1, std::memory_order_relaxed);
}

void AudioPlayer::stop() {
  if (playback_ == nullptr) {
    stuck_ = false;
    return;
  }
  // Cleanup is a sequence of independent confirmations, not a chain a single
  // refusal can end. It used to be a chain: a refused reset returned before
  // unprepare and close were even attempted, which is how one transient driver
  // hiccup held the device open until the next attempt (QA2-04).
  // Keep the PCM and WAVEHDR alive until WinMM confirms the device is closed.
  MMRESULT result = winMmBackend().reset(playback_->output);
  if (result == MMSYSERR_INVALHANDLE) {
    // The handle is already gone, so no driver can be writing through it: this
    // is the one failure that still permits a free. Retaining the buffer here
    // would be a leak with nothing left to protect.
    g_deadHandleCount.fetch_add(1, std::memory_order_relaxed);
    releaseState();
    lastError_.clear();
    lastCleanupError_.clear();
    return;
  }
  if (result != MMSYSERR_NOERROR) {
    noteCleanupFailure(result, "waveOutReset");
  }
  // Unprepare is attempted whenever this process prepared the buffer, including
  // after a refused reset: the driver can still accept it, and the header's
  // WHDR_PREPARED bit is not ours to trust (QA2-09).
  if (prepared_) {
    result = winMmBackend().unprepare(playback_->output, &playback_->header, sizeof(playback_->header));
    if (result == MMSYSERR_NOERROR || result == WAVERR_UNPREPARED) {
      // WAVERR_UNPREPARED means the driver had already dropped it: the buffer
      // is in the state this step wanted, so it is not a cleanup failure.
      prepared_ = false;
    } else {
      noteCleanupFailure(result, "waveOutUnprepareHeader");
    }
  }
  // Close is attempted even when reset or unprepare was refused. It is the step
  // whose answer decides whether the buffer may be freed, and a driver that
  // refused one step can still accept this one; gating it behind the earlier
  // steps is what turned one refusal into a leak (QA2-04). The decision rests
  // on close alone: a success (or a dead handle) is the only confirmation that
  // no driver is still writing through the buffer.
  result = winMmBackend().close(playback_->output);
  if (result == MMSYSERR_NOERROR || result == MMSYSERR_INVALHANDLE) {
    if (result == MMSYSERR_INVALHANDLE) g_deadHandleCount.fetch_add(1, std::memory_order_relaxed);
    g_deviceClosedCount.fetch_add(1, std::memory_order_relaxed);
    releaseState();
    lastError_.clear();
    lastCleanupError_.clear();
    return;
  }
  noteCleanupFailure(result, "waveOutClose");
  // The driver will not let go. Freeing the buffer would hand the device memory
  // the allocator may already have reused, so it is kept and the voice marked
  // stuck: play() will not retry it, stop() will -- that is the only retry
  // point -- and the counters make the retention readable. Bounded by one
  // buffer per voice, i.e. at most kVoiceCount per scheduler; it does not grow
  // with the number of calls, which is what separates this from a leak.
  stuck_ = true;
}

void AudioEventScheduler::reset(std::int32_t tick) {
  // A reset that moves the cursor backwards is a recovery: the caller wants the
  // range it already consumed to be replayable (reverse, seek, tick regression).
  // Counted before the counters are cleared, and carried across the clear,
  // because it describes the scheduler's life rather than the current window --
  // a value wiped by every reset could never be read from the final stats.
  if (initialized_ && tick < lastTick_) ++stats_.reverseRecoveries;
  const std::size_t keepReverseRecoveries = stats_.reverseRecoveries;
  const std::size_t keepDuplicateTicks = stats_.duplicateTicks;
  // T93-D (T85 defect D): `initialized_` means "reset() has run", not "a range has
  // been consumed", and the startup reset(0) in main.cpp sets it. A resume that
  // followed a paused start therefore re-entered at tick 0 with lastTick_ = 0 and
  // the range 1..0 played nothing: the 746 events at tick 0 (14.0% of the
  // timeline, measured on the T85 audio fixture) were lost -- a whole run read
  // events_seen=10.
  //
  // Dedupe basis: lastTick_ >= 0 is precisely "this scheduler has consumed a
  // range". While it is still -1 nothing has been played, so the reset is
  // INCLUSIVE of the tick it names; once a range has been consumed the old
  // exclusive behaviour stands, which is what makes "pause at T, resume at T"
  // not replay T. replace_all: none of the two call shapes is affected.
  lastTick_ = lastTick_ >= 0 ? tick : tick - 1;
  initialized_ = true;
  stats_ = {};
  stats_.reverseRecoveries = keepReverseRecoveries;
  stats_.duplicateTicks = keepDuplicateTicks;
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
    // playing() alone is not "this voice is still audible". A device that
    // refused one close left playing() true forever, reset() turned that into
    // stopped_ = true, and advance() then answered every later tick with the
    // same stats -- permanent silence after a single hiccup, with no counter
    // to show it (QA2-02). A stuck voice is still reported, but as stuck.
    if (player.playing() && !player.stuck()) ++active;
  }
  return active;
}

std::size_t AudioEventScheduler::stuckVoiceCount() const {
  std::size_t stuck = 0;
  for (const auto& player : players_) {
    if (player.stuck()) ++stuck;
  }
  return stuck;
}

AudioScheduleStats AudioEventScheduler::advance(const SoundEventTimeline& timeline, std::int32_t tick) {
  if (tick < 0 || stopped_) return stats_;
  if (lastTick_ >= tick) { ++stats_.duplicateTicks; return stats_; }
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
    const bool resolved = resolver_ != nullptr && resolver_(event.name, wav) && wav.valid;
    // The codec comes from the buffer the resolver produced, so an event whose
    // name says .mp3 but whose bytes decoded as something else is counted as
    // what it was, and a resolver that stopped decoding cannot leave these
    // counters reading a name-derived value. An event that resolved to PCM and
    // was then refused for being inaudible is still an MP3 event this chain
    // carried, so it is counted in seen -- the identity is
    // seen == played + skipped + failed + inaudible.
    const bool fromMp3 = resolved && wav.codec == SoundCodec::Mp3;
    if (fromMp3) ++stats_.mp3EventsSeen;
    const auto observe = [&](const char* outcome) {
      if (contentObserver_) contentObserver_(event, wav, outcome);
    };
    if (!resolved || lastSpatialMix_.isSilent()) {
      ++stats_.eventsMissingResource;
      if (resolved && lastSpatialMix_.isSilent()) {
        // The resource is fine and the listener would not hear it. Named rather
        // than folded into "missing bytes": same total, different fact.
        ++stats_.eventsInaudible;
        if (fromMp3) ++stats_.mp3EventsInaudible;
        observe("inaudible");
      } else if (fromMp3) {
        ++stats_.mp3EventsFailed;
      }
      return;
    }
    const bool pcmShapeValid = wav.encoding == WavEncoding::Pcm
      && (wav.bitsPerSample == 8 || wav.bitsPerSample == 16)
      && wav.channels >= 1 && wav.channels <= 2 && wav.sampleRate != 0
      && wav.blockAlign != 0 && wav.pcm.size() % wav.blockAlign == 0;
    if (!pcmShapeValid) {
      ++stats_.eventsMissingResource;
      if (fromMp3) ++stats_.mp3EventsFailed;
      observe("failed");
      return;
    }
    if (outcomeSink_) {
      // The device-free sink owns the outcome; `skipped` is the explicit
      // "not played, and that is the contract" answer --audio-disabled uses.
      switch (outcomeSink_(wav, volume, lastSpatialMix_.pan)) {
        case PlaybackOutcome::played:
          ++stats_.eventsPlayed;
          if (fromMp3) ++stats_.mp3EventsPlayed;
          observe("played");
          return;
        case PlaybackOutcome::skipped:
          ++stats_.eventsSkippedDisabled;
          if (fromMp3) ++stats_.mp3EventsSkipped;
          observe("skipped");
          return;
        case PlaybackOutcome::failed:
          ++stats_.eventsMissingResource;
          if (fromMp3) ++stats_.mp3EventsFailed;
          observe("failed");
          return;
      }
    }
    const bool played = wavPcmIsPlayable(wav) && (playbackSink_
      ? playbackSink_(wav, volume, lastSpatialMix_.pan)
      : player.play(wav, volume, lastSpatialMix_.pan));
    if (!played) {
      ++stats_.eventsMissingResource;
      if (fromMp3) ++stats_.mp3EventsFailed;
      observe("failed");
      return;
    }
    ++stats_.eventsPlayed;
    if (fromMp3) ++stats_.mp3EventsPlayed;
    observe("played");
  });
  lastTick_ = tick;
  return stats_;
}

} // namespace tf2::native
