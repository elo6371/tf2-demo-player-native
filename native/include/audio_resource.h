#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "vpk_archive.h"

namespace tf2::native {

enum class SoundCodec : std::uint8_t {
  Unknown = 0,
  Wav,
  Mp3,
};

enum class WavEncoding : std::uint16_t {
  Unknown = 0,
  Pcm = 1,
  IeeeFloat = 3,
};

// Parseable MPEG audio frame header fields. `frameOffset` is the first byte of
// the frame (after any ID3v2 tag), which is where a decoder has to start.
struct Mp3FrameInfo {
  std::size_t frameOffset = 0;
  std::uint32_t sampleRate = 0;
  std::uint16_t channels = 0;
  std::uint32_t bitrateKbps = 0;
  std::uint8_t mpegVersion = 0;   // 1 = MPEG-1, 2 = MPEG-2, 25 = MPEG-2.5
  bool layer3 = false;
  bool valid = false;
};

// Find the first frame whose header fields are self-consistent (a sync-shaped
// pair of bytes with a defined bitrate and sample rate is not enough -- the
// version/layer/bitrate/sample-rate combinations encode a real format or they
// do not). Skips an ID3v2 tag when present. Used by both the MP3 signature check
// and the decoder, so "looks like an MP3" and "can be decoded as an MP3" start
// from the same parse.
Mp3FrameInfo findMp3FrameHeader(const std::vector<std::uint8_t>& bytes);

// Outcome of an MP3 decode attempt. The four answers are separate because the
// caller has to name them differently: a machine without a decoder is a
// capability gap, a codec that refuses the stream is a format fact, and bytes
// that parse to no audio are a data fact.
enum class Mp3DecodeStatus : std::uint8_t {
  Decoded = 0,
  NoDecoderInstalled,
  UnsupportedFormat,
  InvalidStream,
};

struct WavPcmData {
  // Which reader produced these bytes: parseWav sets Wav, decodeMp3ToPcm sets
  // Mp3. The provenance is carried on the PCM rather than re-derived by the
  // caller because the scheduler only ever sees the decoded buffer -- without
  // it, "the MP3 chain reached the output path" is not observable from the
  // product at all, and the per-event playback record would have to guess the
  // codec from the name.
  SoundCodec codec = SoundCodec::Unknown;
  WavEncoding encoding = WavEncoding::Unknown;
  std::uint16_t channels = 0;
  std::uint32_t sampleRate = 0;
  std::uint16_t bitsPerSample = 0;
  std::uint16_t blockAlign = 0;
  std::uint32_t byteRate = 0;
  std::vector<std::uint8_t> pcm;
  bool valid = false;
  std::string error;
};

// Decode MP3 bytes to 16-bit PCM through the platform ACM codec (the MPEG
// Layer-3 ACM that ships with Windows). `out` is filled the same way parseWav
// fills it, so a decoded MP3 flows through the same playability check, the same
// duration cap and the same WinMM output path as a WAV. `detail` always names
// the failure when the status is not Decoded.
//
// Why ACM rather than a bundled decoder: the product's output path is WinMM, so
// the codec that pairs with it is the system's ACM codec, and the alternative --
// a vendored MP3 implementation -- would be a codec to maintain inside a demo
// player. The cost of the choice is that MP3 playback depends on a codec being
// installed, which is why NoDecoderInstalled is its own status instead of being
// folded into InvalidStream.
Mp3DecodeStatus decodeMp3ToPcm(const std::vector<std::uint8_t>& bytes,
  WavPcmData& out, std::string& detail);

// True when this machine has an ACM MPEG Layer-3 decoder installed. Exposed so a
// probe can report the machine's capability separately from a stream's bytes.
bool mp3DecoderAvailable();

struct SoundResource {
  SoundCodec codec = SoundCodec::Unknown;
  std::string archivePath;
  std::vector<std::uint8_t> bytes;
  WavPcmData wav;
  bool valid = false;
  std::string error;
};

// Parse a RIFF/WAVE file without assuming a fixed chunk order. The returned
// PCM bytes are the original interleaved sample bytes; callers can upload them
// to WASAPI/XAudio/Web Audio according to encoding and bitsPerSample.
bool parseWav(const std::vector<std::uint8_t>& bytes, WavPcmData& out);

// Normalize a Source sound name to the archive path used by TF2 VPKs.
// Inputs may be "weapons/rocket1.wav", "sound/weapons/rocket1", or use '\\'.
//
// A name may be preceded by one or two Source "sound characters"
// (src/public/soundchars.h): ')weapons/shotgun_shoot.wav' is a stock file with
// spatial-stereo metadata in front of it. TF2's soundprecache stores the name
// as written, so those characters are dropped here -- at the single point
// where an entry name becomes an archive path -- and kept in the name itself
// so the traceable content map can still report what the recording stored.
std::string normalizeSoundPath(const std::string& name, const char* extension = ".wav");

// The length of the leading sound-character run on a name, capped at the "one
// or two" the Soundscripts documentation describes. Exposed so probes can
// count marked entries (and their resolve outcome) without re-deriving the
// character set.
std::size_t leadingSoundCharacterCount(const std::string& name);

// Resolve and parse a sound directly from a VPK archive. This deliberately
// accepts only WAV for deterministic native playback; MP3/other codecs are
// reported as missing/unsupported instead of silently mis-decoding them.
bool readSoundWav(const VpkArchive& archive, const std::string& name,
  WavPcmData& out, std::string* resolvedPath = nullptr);

} // namespace tf2::native
