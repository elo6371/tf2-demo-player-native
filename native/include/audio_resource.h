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

struct WavPcmData {
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
