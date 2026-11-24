// export_audio_mix_probe.cpp -- deterministic, device-free offline PCM mixdown
// of a TF2 demo's sound timeline.
//
// WHAT THIS IS
//   export_audio_mix_probe <demo.dem> <startTick> <tickCount> <out.pcm>
//     [--tf-dir <dir>] [--sample-rate 48000] [--channels 2]
//     [--preroll <ticks>] [--listener view|none]
//
//   Writes exactly tickCount * (sampleRate * 3 / 200) sample frames of signed
//   16-bit little-endian PCM. At the default 48000 Hz that is 720 frames per
//   demo tick (a demo tick is 200/3 Hz = 15 ms, 48000 * 0.015 = 720). The
//   output is a function of (demo bytes, tick range, tf-dir contents) only:
//   no audio device, no wall clock, no thread timing, no random seed.
//
// WHAT IS REUSED FROM THE PRODUCT, AND WHERE THE PRODUCT STOPS
//   Placement: buildSoundTimeline() (native/src/demo_header_sound.cpp:189) and
//   demoTickRate()/firstRecordedPacketTick() (native/src/demo_header_core.cpp)
//   are the product's own functions, so "which event lands on which tick" is
//   the product's answer, not a re-implementation of it.
//   Dispatch: AudioEventScheduler::advance() (native/src/audio_player.cpp:419)
//   is driven one tick at a time, exactly as main.cpp drives it per frame, so
//   the voice index, the skip-voice filter, the spatial mix and the volume
//   arithmetic are the product's.
//   Per-voice PCM: the injected PlaybackSink hands back the same
//   (WavPcmData, volume, pan) that AudioEventScheduler would otherwise give
//   AudioPlayer::play(); this probe calls applyPcmPan() with it, i.e. it
//   produces the identical byte buffer AudioPlayer::play() would have handed
//   to waveOut -- at the resource's native rate.
//   THAT IS THE DEVICE BOUNDARY. AudioPlayer::play()
//   (native/src/audio_player.cpp) is where the product stops being offline:
//   it calls waveOutOpen()/waveOutPrepareHeader()/waveOutWrite() and lets
//   winmm's mixer do rate conversion and summing. waveOutOpen is the blocking
//   symbol: there is no product layer above it that mixes two voices into one
//   buffer, because the product never has to -- the driver does that.
//   Consequently the closest honest offline layer is "applyPcmPan's output per
//   event" (which is byte-exact product data) plus a mixdown this probe owns:
//   deterministic integer linear-interpolation resampling to the output rate
//   followed by a plain sum in double and a clamp/round to s16. That mixdown is
//   NOT a product symbol; it is this tool's own, and it is deterministic by
//   construction (integer phase, fixed order, no FP sorting, no time source).
//   Note the product has no 48 kHz *mix* path either: each of the 8 AudioPlayers
//   opens its own waveOut device at the resource rate (typically 44100 for TF2
//   WAVs) and the OS mixer resamples. So the 48000 Hz output here is this
//   tool's sample rate choice; when a resource is 44100 it is resampled with the
//   fixed-ratio integer linear interpolator below (44100 -> 48000 is 160:147
//   exactly, so the result is reproducible and pitch-correct, but it is linear
//   interpolation, not a band-limited SRC).
//
//   Sound data: the product's VPK access is readSoundResource() over the same
//   five archive names main.cpp opens, with the same "first archive that
//   resolves wins, cache by name" policy (native/src/main.cpp:9106-9257).
//
// LISTENER
//   --listener view (default): the listener position for tick T is the last
//   recorded DemoViewSample at or before T (summary.viewSamples, absolute
//   ticks mapped into the timeline's playback-relative space), which makes
//   spatial gain/pan a function of the demo's own camera track. The product
//   drives setListenerPosition() from the live camera each frame; the recorded
//   view sample is the offline equivalent of that camera position.
//   --listener none: an invalid (NaN) listener, which makes advance() use
//   gain 1 / pan 0 -- the same fallback the product takes when it has no
//   camera, and the setting to compare against "spatial mix changed it".
//
// TICKS AND PRE-ROLL
//   startTick/tickCount are in the same playback-relative space the timeline
//   uses (the demo's own ticks minus the first recorded packet's tick). Sounds
//   that started before startTick still have tails, so the mix starts at
//   mixOrigin = max(0, startTick - preroll) and the written window is
//   [startTick, startTick+tickCount). A sound that would extend past the last
//   written tick is truncated at the window edge; the count is reported as
//   'cut-at-end' so the truncation is a reading instead of an assumption.
//
// TRACE (ASCII, one line each; the required five lines are emitted verbatim)
//   audio-mix: ticks=<n> sampleRate=48000 channels=2 frames=<n> bytes=<n>
//   audio-mix: events=<n>
//   audio-mix: first-event-tick=<n> last-event-tick=<n>   (or both "none")
//   audio-mix: peak=<0..32767> rms=<int>
//   audio-mix: sha256=<hex>
//   audio-mix: result=ok|fail detail=<text>
//   Extra readings (also prefixed "audio-mix: ") spell out the parts: timeline
//   buckets, archive count, scheduler stats, device-open count (must be 0),
//   clipping, concurrency.
//
// EXIT CODES: 0 ok, 1 audio/demo failure, 2 usage.
//
// No existing product file is modified; this is a standalone probe like
// native/tools/sound_content_map_probe.cpp, built by
// native/tools/export_audio_mix_probe.sh.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <queue>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "audio_player.h"
#include "audio_timeline.h"
#include "demo_header.h"
#include "vpk_archive.h"

namespace {

using tf2::native::AudioEventScheduler;
using tf2::native::DemoHeader;
using tf2::native::DemoIndex;
using tf2::native::DemoNetworkSummary;
using tf2::native::DemoViewSample;
using tf2::native::SoundEventTimeline;
using tf2::native::SoundResource;
using tf2::native::VpkArchive;
using tf2::native::WavPcmData;

// A demo tick is 200/3 Hz, i.e. 15 ms exactly. The frame count per tick must be
// an integer for "exactly N frames per tick" to mean anything; at 48000 Hz it
// is 720.
constexpr double kSecondsPerTick = 0.015;
constexpr std::int32_t kDefaultPrerollTicks = 2000;
// Upper bound on the mix buffer this probe is willing to hold (frames). 8M
// frames is 166 s at 48 kHz and ~256 MB of double accumulators; past that the
// tool refuses with a named detail instead of thrashing.
constexpr std::size_t kMaxMixFrames = 8000000;
// The five archive names, in the order main.cpp opens them.
constexpr const char* kProductArchiveNames[5] = {
  "pak01_dir.vpk",
  "tf2_misc_dir.vpk",
  "tf2_textures_dir.vpk",
  "tf2_sound_misc_dir.vpk",
  "tf2_sound_vo_english_dir.vpk",
};
constexpr const char* kDefaultTfDir = "D:/SteamLibrary/steamapps/common/Team Fortress 2/tf";

// ---------------------------------------------------------------------------
// SHA-256 (FIPS 180-4), inline so the trace hash needs no external tool.
// ---------------------------------------------------------------------------
class Sha256 {
public:
  void update(const std::uint8_t* data, std::size_t len) {
    total_ += static_cast<std::uint64_t>(len);
    while (len > 0) {
      const std::size_t take = std::min(len, sizeof(buf_) - bufLen_);
      std::memcpy(buf_ + bufLen_, data, take);
      bufLen_ += take;
      data += take;
      len -= take;
      if (bufLen_ == sizeof(buf_)) {
        transform(buf_);
        bufLen_ = 0;
      }
    }
  }

  std::string finishHex() {
    const std::uint64_t bits = total_ * 8u;
    std::uint8_t pad[72];
    std::size_t padLen = 0;
    pad[padLen++] = 0x80;
    while ((bufLen_ + padLen) % 64 != 56) pad[padLen++] = 0;
    for (int i = 7; i >= 0; --i) pad[padLen++] = static_cast<std::uint8_t>((bits >> (i * 8)) & 0xffu);
    update(pad, padLen);
    char out[65];
    for (int i = 0; i < 8; ++i) std::snprintf(out + i * 8, 9, "%08x", h_[i]);
    return std::string(out, 64);
  }

private:
  static std::uint32_t rotr(std::uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

  void transform(const std::uint8_t* p) {
    static const std::uint32_t k[64] = {
      0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u,
      0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
      0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u,
      0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
      0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
      0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
      0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu,
      0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
      0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au,
      0x5b9cca4fu, 0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
      0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
    };
    std::uint32_t w[64];
    for (int i = 0; i < 16; ++i) {
      w[i] = (static_cast<std::uint32_t>(p[i * 4]) << 24)
           | (static_cast<std::uint32_t>(p[i * 4 + 1]) << 16)
           | (static_cast<std::uint32_t>(p[i * 4 + 2]) << 8)
           | static_cast<std::uint32_t>(p[i * 4 + 3]);
    }
    for (int i = 16; i < 64; ++i) {
      const std::uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
      const std::uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
      w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    std::uint32_t a = h_[0], b = h_[1], c = h_[2], d = h_[3];
    std::uint32_t e = h_[4], f = h_[5], g = h_[6], hh = h_[7];
    for (int i = 0; i < 64; ++i) {
      const std::uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
      const std::uint32_t ch = (e & f) ^ ((~e) & g);
      const std::uint32_t t1 = hh + s1 + ch + k[i] + w[i];
      const std::uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
      const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
      const std::uint32_t t2 = s0 + maj;
      hh = g; g = f; f = e; e = d + t1;
      d = c; c = b; b = a; a = t1 + t2;
    }
    h_[0] += a; h_[1] += b; h_[2] += c; h_[3] += d;
    h_[4] += e; h_[5] += f; h_[6] += g; h_[7] += hh;
  }

  std::uint32_t h_[8] = {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
                         0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
  std::uint8_t buf_[64] = {};
  std::size_t bufLen_ = 0;
  std::uint64_t total_ = 0;
};

// ---------------------------------------------------------------------------
// Options
// ---------------------------------------------------------------------------
struct Options {
  std::string demoPath;
  std::int32_t startTick = 0;
  std::int32_t tickCount = 0;
  std::string outPath;
  std::string tfDir;
  int sampleRate = 48000;
  int channels = 2;
  std::int32_t prerollTicks = kDefaultPrerollTicks;
  bool listenerFromView = true;
};

void usage() {
  std::printf(
    "usage: export_audio_mix_probe <demo.dem> <startTick> <tickCount> <out.pcm>\n"
    "         [--tf-dir <dir>] [--sample-rate 48000] [--channels 2]\n"
    "         [--preroll <ticks>] [--listener view|none]\n");
}

bool parseInt(const char* text, long long& out) {
  if (text == nullptr || *text == '\0') return false;
  char* end = nullptr;
  errno = 0;
  const long long value = std::strtoll(text, &end, 10);
  if (errno != 0 || end == text || *end != '\0') return false;
  out = value;
  return true;
}

bool parseArgs(int argc, char** argv, Options& opts, std::string& error) {
  std::vector<std::string> positional;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    auto next = [&](const char* name) -> const char* {
      if (i + 1 >= argc) {
        error = std::string("missing value for ") + name;
        return nullptr;
      }
      return argv[++i];
    };
    if (arg == "--tf-dir") {
      const char* value = next("--tf-dir");
      if (value == nullptr) return false;
      opts.tfDir = value;
    } else if (arg == "--sample-rate") {
      long long value = 0;
      const char* raw = next("--sample-rate");
      if (raw == nullptr) return false;
      if (!parseInt(raw, value)) { error = "bad --sample-rate"; return false; }
      opts.sampleRate = static_cast<int>(value);
    } else if (arg == "--channels") {
      long long value = 0;
      const char* raw = next("--channels");
      if (raw == nullptr) return false;
      if (!parseInt(raw, value)) { error = "bad --channels"; return false; }
      opts.channels = static_cast<int>(value);
    } else if (arg == "--preroll") {
      long long value = 0;
      const char* raw = next("--preroll");
      if (raw == nullptr) return false;
      if (!parseInt(raw, value)) { error = "bad --preroll"; return false; }
      opts.prerollTicks = static_cast<std::int32_t>(value);
    } else if (arg == "--listener") {
      const char* value = next("--listener");
      if (value == nullptr) return false;
      const std::string mode = value;
      if (mode == "view") opts.listenerFromView = true;
      else if (mode == "none") opts.listenerFromView = false;
      else { error = "bad --listener (view|none)"; return false; }
    } else if (arg == "-h" || arg == "--help") {
      error = "help";
      return false;
    } else if (!arg.empty() && arg[0] == '-' && arg != "-") {
      error = "unknown option " + arg;
      return false;
    } else {
      positional.push_back(arg);
    }
  }
  if (positional.size() != 4) {
    error = "expected 4 positional arguments, got " + std::to_string(positional.size());
    return false;
  }
  opts.demoPath = positional[0];
  long long start = 0, count = 0;
  if (!parseInt(positional[1].c_str(), start)) { error = "bad startTick"; return false; }
  if (!parseInt(positional[2].c_str(), count)) { error = "bad tickCount"; return false; }
  opts.startTick = static_cast<std::int32_t>(start);
  opts.tickCount = static_cast<std::int32_t>(count);
  opts.outPath = positional[3];
  if (opts.tfDir.empty()) {
    const char* env = std::getenv("TF2_NATIVE_TF_DIR");
    opts.tfDir = (env != nullptr && *env != '\0') ? env : kDefaultTfDir;
  }
  if (opts.startTick < 0) { error = "startTick must be >= 0"; return false; }
  if (opts.tickCount <= 0) { error = "tickCount must be > 0"; return false; }
  if (opts.prerollTicks < 0) { error = "preroll must be >= 0"; return false; }
  if (opts.channels != 1 && opts.channels != 2) { error = "--channels must be 1 or 2"; return false; }
  // Frames per tick must be an exact integer, else "exactly 720 frames per tick"
  // is not a thing this tool can promise. sampleRate * 3/200 is integral for
  // 48000 (720), 40000 (600), 32000 (480), 24000 (360), 16000 (240)... and is
  // NOT for 44100 (661.5), which is exactly why the default is 48000.
  if (opts.sampleRate <= 0 || (static_cast<long long>(opts.sampleRate) * 3) % 200 != 0) {
    error = "sample rate does not divide a 15 ms demo tick into whole frames";
    return false;
  }
  return true;
}

// ---------------------------------------------------------------------------
// PCM helpers
// ---------------------------------------------------------------------------
// The exact per-voice buffer AudioPlayer::play() would have produced: copy the
// resource PCM, then applyPcmPan with the scheduler's (volume, pan). This is a
// product function, called with product arguments, so the bytes are not a
// re-implementation. Returns the interleaved s16 stereo samples.
bool buildPannedStereo(const WavPcmData& wav, float volume, float pan,
                       std::vector<std::int16_t>& out) {
  out.clear();
  if (!wav.valid || wav.sampleRate == 0 || wav.pcm.empty()) return false;
  std::vector<std::uint8_t> bytes = wav.pcm;  // AudioPlayer::play() copies too
  std::uint16_t channels = wav.channels;
  if (!tf2::native::applyPcmPan(bytes, channels, wav.bitsPerSample, volume, pan)) return false;
  if (channels != 2) return false;
  if (wav.bitsPerSample == 16) {
    if (bytes.size() % 4 != 0) return false;
    const std::size_t frames = bytes.size() / 4;
    out.resize(frames * 2);
    for (std::size_t i = 0; i < frames; ++i) {
      const std::uint8_t* p = bytes.data() + i * 4;
      out[i * 2 + 0] = static_cast<std::int16_t>(static_cast<std::uint16_t>(p[0] | (p[1] << 8)));
      out[i * 2 + 1] = static_cast<std::int16_t>(static_cast<std::uint16_t>(p[2] | (p[3] << 8)));
    }
    return true;
  }
  if (wav.bitsPerSample == 8) {
    if (bytes.size() % 2 != 0) return false;
    const std::size_t frames = bytes.size() / 2;
    out.resize(frames * 2);
    for (std::size_t i = 0; i < frames; ++i) {
      // Unsigned 8-bit -> signed 16-bit, the usual <<8 with the 128 offset.
      out[i * 2 + 0] = static_cast<std::int16_t>((static_cast<int>(bytes[i * 2 + 0]) - 128) * 256);
      out[i * 2 + 1] = static_cast<std::int16_t>((static_cast<int>(bytes[i * 2 + 1]) - 128) * 256);
    }
    return true;
  }
  return false;
}

// Deterministic resample+mix of one stereo s16 voice into the double
// accumulators. Integer phase arithmetic (no FP in the position), so the same
// input bytes always land on the same output samples regardless of platform FP
// settings; the interpolation itself is exact integer math too.
struct MixStats {
  std::size_t framesWritten = 0;
  std::size_t cutAtEnd = 0;
  std::size_t resampled = 0;
};

MixStats mixVoice(const std::vector<std::int16_t>& interleaved, std::uint32_t srcRate,
                  std::size_t dstStart, std::vector<double>& left, std::vector<double>& right,
                  std::uint32_t outRate) {
  MixStats stats;
  if (srcRate == 0 || interleaved.size() < 2) return stats;
  const std::size_t srcFrames = interleaved.size() / 2;
  const std::size_t total = left.size();
  if (dstStart >= total) { stats.cutAtEnd = srcFrames; return stats; }
  if (srcRate == outRate) {
    const std::size_t room = total - dstStart;
    const std::size_t frames = std::min(room, srcFrames);
    for (std::size_t i = 0; i < frames; ++i) {
      left[dstStart + i] += static_cast<double>(interleaved[i * 2 + 0]);
      right[dstStart + i] += static_cast<double>(interleaved[i * 2 + 1]);
    }
    stats.framesWritten = frames;
    stats.cutAtEnd = srcFrames - frames;
    return stats;
  }
  // Output frames this voice covers at the mix rate: floor(srcFrames * out / src).
  const std::uint64_t scaled = static_cast<std::uint64_t>(srcFrames) * outRate;
  std::size_t dstFrames = static_cast<std::size_t>(scaled / srcRate);
  if (dstFrames == 0 && srcFrames > 0) dstFrames = 1;
  const std::size_t room = total - dstStart;
  if (dstFrames > room) {
    stats.cutAtEnd = dstFrames - room;
    dstFrames = room;
  }
  const std::int64_t half = static_cast<std::int64_t>(outRate / 2);
  const std::int64_t denom = static_cast<std::int64_t>(outRate);
  for (std::size_t j = 0; j < dstFrames; ++j) {
    const std::uint64_t pos = static_cast<std::uint64_t>(j) * srcRate;
    std::size_t i0 = static_cast<std::size_t>(pos / outRate);
    const std::int64_t frac = static_cast<std::int64_t>(pos % outRate);
    if (i0 >= srcFrames) i0 = srcFrames - 1;
    const std::size_t i1 = (i0 + 1 < srcFrames) ? i0 + 1 : i0;
    for (int ch = 0; ch < 2; ++ch) {
      const std::int64_t s0 = interleaved[i0 * 2 + static_cast<std::size_t>(ch)];
      const std::int64_t s1 = interleaved[i1 * 2 + static_cast<std::size_t>(ch)];
      std::int64_t v = s0 * (denom - frac) + s1 * frac;
      v = (v >= 0) ? (v + half) / denom : -((-v + half) / denom);
      if (ch == 0) left[dstStart + j] += static_cast<double>(v);
      else right[dstStart + j] += static_cast<double>(v);
    }
  }
  stats.framesWritten = dstFrames;
  stats.resampled = dstFrames;
  return stats;
}

// Clamp+round to s16. The rounding is the explicit "add half, truncate" form
// rather than std::lround so it cannot depend on the FP rounding mode.
std::int32_t toS16(double value) {
  if (value >= 32767.0) return 32767;
  if (value <= -32768.0) return -32768;
  return value > 0.0 ? static_cast<std::int32_t>(value + 0.5)
                     : static_cast<std::int32_t>(value - 0.5);
}

// Largest number of simultaneously sounding voices over the span list. Sorted
// starts + a min-heap of ends: O(n log n), no per-frame array.
std::size_t maxConcurrent(std::vector<std::pair<std::int32_t, std::int32_t>>& spans) {
  std::sort(spans.begin(), spans.end());
  std::priority_queue<std::int32_t, std::vector<std::int32_t>, std::greater<std::int32_t>> ends;
  std::size_t best = 0;
  for (const auto& span : spans) {
    while (!ends.empty() && ends.top() <= span.first) ends.pop();
    ends.push(span.second);
    best = std::max(best, ends.size());
  }
  return best;
}

int fail(const std::string& detail, int code = 1) {
  std::printf("audio-mix: result=fail detail=%s\n", detail.c_str());
  std::fflush(stdout);
  return code;
}

}  // namespace

int main(int argc, char** argv) {
  Options opts;
  std::string usageError;
  if (!parseArgs(argc, argv, opts, usageError)) {
    if (usageError != "help") {
      usage();
      std::printf("audio-mix: result=fail detail=%s\n", usageError.c_str());
      return 2;
    }
    usage();
    return 0;
  }

  const std::int32_t framesPerTick =
      static_cast<std::int32_t>(static_cast<long long>(opts.sampleRate) * 3 / 200);
  const std::int64_t requestedFrames =
      static_cast<std::int64_t>(opts.tickCount) * framesPerTick;
  if (requestedFrames <= 0) return fail("requested frame count is zero");
  const std::int32_t startTick = opts.startTick;
  const std::int32_t endTick = startTick + opts.tickCount;
  const std::int32_t mixOrigin = std::max<std::int32_t>(0, startTick - opts.prerollTicks);
  const std::int64_t mixFrames64 = static_cast<std::int64_t>(endTick - mixOrigin) * framesPerTick;
  if (mixFrames64 > static_cast<std::int64_t>(kMaxMixFrames)) {
    return fail("mix window too large (" + std::to_string(mixFrames64) + " frames > "
                + std::to_string(kMaxMixFrames) + "); raise --preroll or reduce tickCount");
  }
  const std::size_t mixFrames = static_cast<std::size_t>(mixFrames64);
  const std::size_t outFrames = static_cast<std::size_t>(requestedFrames);
  const std::size_t outByteCount = outFrames * static_cast<std::size_t>(opts.channels) * 2u;

  const std::filesystem::path demoPath(opts.demoPath);
  std::error_code ec;
  if (!std::filesystem::is_regular_file(demoPath, ec)) {
    return fail("demo not found: " + opts.demoPath);
  }

  // ---- the demo, through the product's own chain ---------------------------
  DemoHeader header;
  if (!tf2::native::parseDemoHeaderFile(demoPath, header)) return fail("demo header parse failed");
  DemoIndex index;
  if (!tf2::native::indexDemoFile(demoPath, header, index)) return fail("demo index failed");
  DemoNetworkSummary summary;
  summary.networkProtocol = header.networkProtocol;
  if (!tf2::native::scanKnownDemoMessages(demoPath, index, summary)) {
    return fail("demo scan failed");
  }

  bool originFound = false;
  const std::int32_t originTick = tf2::native::firstRecordedPacketTick(index, originFound);
  const double tickRate = tf2::native::demoTickRate(header);

  SoundEventTimeline timeline;
  const auto built = tf2::native::buildSoundTimeline(
      summary.decodedSoundEvents, originFound, originTick, tickRate, timeline);

  // ---- sound data: the product's archive chain ----------------------------
  std::vector<std::unique_ptr<VpkArchive>> archives;
  std::string archiveDetail;
  for (const char* name : kProductArchiveNames) {
    auto archive = std::make_unique<VpkArchive>();
    const std::filesystem::path path = std::filesystem::path(opts.tfDir) / name;
    std::string error;
    if (archive->open(path, &error)) {
      archives.push_back(std::move(archive));
    } else {
      if (!archiveDetail.empty()) archiveDetail += ",";
      archiveDetail += std::string(name) + ":missing";
    }
  }
  if (archives.empty()) {
    return fail("no sound archive opened under tf-dir: " + opts.tfDir
                + " (" + archiveDetail + ")");
  }

  // Resolver: first archive in product order that yields playable PCM wins,
  // cached by name (main.cpp caches by name for the same reason -- the mapping
  // and the sink must not be able to disagree).
  std::unordered_map<std::string, WavPcmData> resolveCache;
  std::size_t resolveHits = 0, resolveMisses = 0, resolveCalls = 0;
  const auto resolver = [&](const std::string& name, WavPcmData& out) -> bool {
    const auto cached = resolveCache.find(name);
    if (cached != resolveCache.end()) {
      ++resolveHits;
      if (!cached->second.valid) return false;
      out = cached->second;
      return true;
    }
    ++resolveCalls;
    WavPcmData found;
    bool resolved = false;
    for (const auto& archive : archives) {
      SoundResource resource;
      if (!tf2::native::readSoundResource(*archive, name, resource)) continue;
      if (!resource.wav.valid || !tf2::native::wavPcmIsPlayable(resource.wav)) continue;
      found = resource.wav;
      resolved = true;
      break;
    }
    resolveCache.emplace(name, found);
    ++resolveMisses;
    if (!resolved) return false;
    out = found;
    return true;
  };

  // ---- mix accumulators ---------------------------------------------------
  std::vector<double> left(mixFrames, 0.0);
  std::vector<double> right(mixFrames, 0.0);
  std::vector<std::pair<std::int32_t, std::int32_t>> spans;
  std::vector<std::int16_t> voicePcm;

  std::int32_t captureTick = mixOrigin;
  std::size_t sinkCalls = 0, sinkFailures = 0, mixedInWindow = 0;
  std::size_t pannedOutOfRange = 0, truncatedEvents = 0, resampledEvents = 0;
  std::size_t volumeClamped = 0;
  const std::size_t mixStartFrame =
      static_cast<std::size_t>(static_cast<std::int64_t>(startTick - mixOrigin) * framesPerTick);


  AudioEventScheduler scheduler;
  scheduler.setDeviceId(0);  // never opened; the sink replaces AudioPlayer entirely
  scheduler.setResolver(resolver);
  scheduler.setPlaybackSink([&](const WavPcmData& wav, float volume, float pan) -> bool {
    ++sinkCalls;
    if (!(volume > 0.0f)) return false;
    if (volume > 1.0f) ++volumeClamped;  // applyPcmPan clamps; count it when it bites
    if (!buildPannedStereo(wav, volume, pan, voicePcm)) { ++sinkFailures; return false; }
    const std::int64_t startOffset =
        static_cast<std::int64_t>(captureTick - mixOrigin) * framesPerTick;
    if (startOffset < 0 || startOffset >= static_cast<std::int64_t>(mixFrames)) {
      ++pannedOutOfRange;
      return true;
    }
    const MixStats stats = mixVoice(voicePcm, wav.sampleRate, static_cast<std::size_t>(startOffset),
                                    left, right, static_cast<std::uint32_t>(opts.sampleRate));
    if (stats.resampled > 0) ++resampledEvents;
    if (stats.cutAtEnd > 0) ++truncatedEvents;
    if (captureTick >= startTick && captureTick < endTick) ++mixedInWindow;
    const std::int64_t endOffset = startOffset + static_cast<std::int64_t>(stats.framesWritten);
    spans.emplace_back(static_cast<std::int32_t>(startOffset),
                       static_cast<std::int32_t>(std::min<std::int64_t>(endOffset,
                                                                       static_cast<std::int64_t>(mixFrames))));
    return true;
  });

  // Listener: the recorded view sample at or before each tick (playback-relative
  // ticks), or an invalid listener for --listener none.
  std::vector<std::pair<std::int32_t, std::array<float, 3>>> listenerSamples;
  if (opts.listenerFromView && originFound) {
    listenerSamples.reserve(summary.viewSamples.size());
    for (const DemoViewSample& sample : summary.viewSamples) {
      if (!sample.hasOrigin || sample.fromSignon) continue;
      std::int32_t relative = 0;
      if (!tf2::native::soundEventRelativeTick(sample.tick, originTick, relative)) continue;
      listenerSamples.emplace_back(relative, std::array<float, 3>{sample.origin[0], sample.origin[1], sample.origin[2]});
    }
    std::stable_sort(listenerSamples.begin(), listenerSamples.end(),
                     [](const auto& a, const auto& b) { return a.first < b.first; });
  }
  std::size_t listenerCursor = 0;
  bool listenerValid = false;
  std::array<float, 3> listenerPos{0.0f, 0.0f, 0.0f};
  const float noListener = std::numeric_limits<float>::quiet_NaN();

  scheduler.reset(mixOrigin - 1);
  for (std::int32_t tick = mixOrigin; tick < endTick; ++tick) {
    captureTick = tick;
    if (opts.listenerFromView) {
      while (listenerCursor < listenerSamples.size() && listenerSamples[listenerCursor].first <= tick) {
        listenerPos = listenerSamples[listenerCursor].second;
        listenerValid = true;
        ++listenerCursor;
      }
      if (listenerValid) scheduler.setListenerPosition(listenerPos[0], listenerPos[1], listenerPos[2]);
      else scheduler.setListenerPosition(noListener, noListener, noListener);
    } else {
      scheduler.setListenerPosition(noListener, noListener, noListener);
    }
    scheduler.advance(timeline, tick);
  }
  const auto stats = scheduler.stats();

  // ---- events whose start falls in the requested range ---------------------
  std::size_t eventsInRange = 0;
  std::int32_t firstEventTick = -1, lastEventTick = -1;
  timeline.forEachRange(startTick, endTick - 1, [&](const tf2::native::SoundPlaybackEvent& event) {
    if (eventsInRange == 0) firstEventTick = event.tick;
    lastEventTick = event.tick;
    ++eventsInRange;
  });

  // ---- serialise ----------------------------------------------------------
  std::vector<std::uint8_t> out(static_cast<std::size_t>(outByteCount), 0u);
  std::size_t written = 0;
  std::uint32_t peak = 0;
  std::int32_t minSample = 32767, maxSample = -32768;
  double sumSquares = 0.0;
  std::size_t sampleCount = 0, clipped = 0;
  for (std::size_t i = 0; i < outFrames; ++i) {
    const std::size_t frame = mixStartFrame + i;
    const std::int32_t l = toS16(frame < mixFrames ? left[frame] : 0.0);
    const std::int32_t r = toS16(frame < mixFrames ? right[frame] : 0.0);
    if (frame < mixFrames) {
      if (left[frame] >= 32767.0 || left[frame] <= -32768.0) ++clipped;
      if (right[frame] >= 32767.0 || right[frame] <= -32768.0) ++clipped;
    }
    const std::int32_t values[2] = {l, r};
    const int count = opts.channels == 1 ? 1 : 2;
    const std::int32_t emit = (opts.channels == 1)
        ? static_cast<std::int32_t>((l + r) / 2)  // integer downmix, deterministic
        : 0;
    for (int c = 0; c < count; ++c) {
      const std::int32_t value = (opts.channels == 1) ? emit : values[c];
      if (value < minSample) minSample = value;
      if (value > maxSample) maxSample = value;
      // Peak is reported as a magnitude in 0..32767; -32768 is full scale too, so it
      // clamps to 32767 here and the exact rails show up on the 'extremes' line.
      const std::int64_t wide = value < 0 ? -static_cast<std::int64_t>(value) : static_cast<std::int64_t>(value);
      const std::uint32_t reported = wide > 32767 ? 32767u : static_cast<std::uint32_t>(wide);
      if (reported > peak) peak = reported;
      sumSquares += static_cast<double>(wide) * static_cast<double>(wide);
      ++sampleCount;
      out[written++] = static_cast<std::uint8_t>(value & 0xff);
      out[written++] = static_cast<std::uint8_t>((value >> 8) & 0xff);
    }
  }
  const double rms = sampleCount > 0 ? std::sqrt(sumSquares / static_cast<double>(sampleCount)) : 0.0;

  Sha256 sha;
  sha.update(out.data(), out.size());
  const std::string digest = sha.finishHex();

  std::ofstream file(opts.outPath, std::ios::binary | std::ios::trunc);
  if (!file) return fail("cannot open output: " + opts.outPath);
  file.write(reinterpret_cast<const char*>(out.data()), static_cast<std::streamsize>(out.size()));
  file.close();
  if (!file) return fail("write failed: " + opts.outPath);
  std::error_code sizeEc;
  const std::uintmax_t onDisk = std::filesystem::file_size(opts.outPath, sizeEc);
  if (sizeEc || onDisk != outByteCount) {
    return fail("output size mismatch: wrote " + std::to_string(onDisk) + " bytes, expected "
                + std::to_string(outByteCount));
  }

  // ---- trace --------------------------------------------------------------
  std::string detail = archiveDetail.empty() ? "all-archives-present" : archiveDetail;
  std::printf("audio-mix: demo=%s map=%s demoBytes=%llu\n",
              demoPath.filename().string().c_str(), header.mapName.c_str(),
              static_cast<unsigned long long>(std::filesystem::file_size(demoPath, sizeEc)));
  std::printf("audio-mix: tick-rate=%.7f origin-found=%d origin-tick=%d listener=%s view-samples=%llu\n",
              tickRate, originFound ? 1 : 0, static_cast<int>(originTick),
              opts.listenerFromView ? "view" : "none",
              static_cast<unsigned long long>(listenerSamples.size()));
  std::printf("audio-mix: archives=%llu tf-dir=%s archive-detail=%s\n",
              static_cast<unsigned long long>(archives.size()), opts.tfDir.c_str(), detail.c_str());
  std::printf("audio-mix: timeline read=%llu scheduled=%llu unresolved=%llu unplaceable=%llu refused=%llu\n",
              static_cast<unsigned long long>(built.read),
              static_cast<unsigned long long>(timeline.size()),
              static_cast<unsigned long long>(built.unresolved),
              static_cast<unsigned long long>(built.unplaceable),
              static_cast<unsigned long long>(built.refused));
  std::printf("audio-mix: ticks=%d sampleRate=%d channels=%d frames=%llu bytes=%llu\n",
              static_cast<int>(opts.tickCount), opts.sampleRate, opts.channels,
              static_cast<unsigned long long>(outFrames),
              static_cast<unsigned long long>(outByteCount));
  std::printf("audio-mix: events=%llu\n", static_cast<unsigned long long>(eventsInRange));
  if (eventsInRange == 0) {
    std::printf("audio-mix: first-event-tick=none last-event-tick=none\n");
  } else {
    std::printf("audio-mix: first-event-tick=%d last-event-tick=%d\n",
                static_cast<int>(firstEventTick), static_cast<int>(lastEventTick));
  }
  std::printf("audio-mix: peak=%u rms=%d\n", peak, static_cast<int>(rms + 0.5));
  std::printf("audio-mix: extremes=min=%d max=%d\n", static_cast<int>(minSample), static_cast<int>(maxSample));
  std::printf("audio-mix: window mix-origin-tick=%d preroll-ticks=%d mixed=%llu sink-calls=%llu "
              "sink-failures=%llu resampled=%llu truncated-at-end=%llu out-of-range=%llu volume-clamped=%llu\n",
              static_cast<int>(mixOrigin), static_cast<int>(opts.prerollTicks),
              static_cast<unsigned long long>(mixedInWindow),
              static_cast<unsigned long long>(sinkCalls),
              static_cast<unsigned long long>(sinkFailures),
              static_cast<unsigned long long>(resampledEvents),
              static_cast<unsigned long long>(truncatedEvents),
              static_cast<unsigned long long>(pannedOutOfRange),
              static_cast<unsigned long long>(volumeClamped));
  std::printf("audio-mix: scheduler seen=%llu played=%llu skipped-voice=%llu missing=%llu "
              "inaudible=%llu mp3-seen=%llu mp3-played=%llu duplicate-ticks=%llu\n",
              static_cast<unsigned long long>(stats.eventsSeen),
              static_cast<unsigned long long>(stats.eventsPlayed),
              static_cast<unsigned long long>(stats.eventsSkippedVoice),
              static_cast<unsigned long long>(stats.eventsMissingResource),
              static_cast<unsigned long long>(stats.eventsInaudible),
              static_cast<unsigned long long>(stats.mp3EventsSeen),
              static_cast<unsigned long long>(stats.mp3EventsPlayed),
              static_cast<unsigned long long>(stats.duplicateTicks));
  std::printf("audio-mix: resolve calls=%llu cache-hits=%llu distinct=%llu\n",
              static_cast<unsigned long long>(resolveCalls),
              static_cast<unsigned long long>(resolveHits),
              static_cast<unsigned long long>(resolveCache.size()));
  std::printf("audio-mix: mix clipped=%llu max-concurrent=%llu rms-basis=s16-output\n",
              static_cast<unsigned long long>(clipped),
              static_cast<unsigned long long>(maxConcurrent(spans)));
  std::printf("audio-mix: device-opens=%u device-opened=%u\n",
              tf2::native::audioDeviceOpenAttempts(), tf2::native::audioDeviceOpenedCount());
  std::printf("audio-mix: sha256=%s\n", digest.c_str());
  std::printf("audio-mix: result=ok detail=device-free-offline-sum product-scheduler=%d resampler=linear-int "
              "srcRate=resource outRate=%d\n", 1, opts.sampleRate);
  std::fflush(stdout);
  return 0;
}
