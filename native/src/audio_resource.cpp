#include "audio_resource.h"

#include <windows.h>
// mmsystem.h must come before mmreg.h/msacm.h: it is what pulls in mmiscapi.h,
// which typedefs FOURCC, and mmsyscom.h, which defines MMSYSERR_NOERROR. With
// WIN32_LEAN_AND_MEAN set, windows.h does not include it for us, and msacm.h
// then fails to compile its own ACMDRIVERDETAILS structs ("FOURCC: unknown
// override specifier") rather than reporting a missing include.
#include <mmsystem.h>
#include <mmreg.h>
#include <msacm.h>

#include <algorithm>
#include <cctype>
#include <cstring>

// The Windows SDK defines these two in mmreg.h only for some language setups of
// the WDK-era headers; the values are from the documented MPEGLAYER3WAVEFORMAT.
#ifndef MPEGLAYER3_WFX_EXTRA_BYTES
#define MPEGLAYER3_WFX_EXTRA_BYTES 12
#endif
#ifndef MPEGLAYER3_FLAG_PARSESTREAM
#define MPEGLAYER3_FLAG_PARSESTREAM 1
#endif

namespace tf2::native {
namespace {

bool readU16(const std::vector<std::uint8_t>& bytes, std::size_t at, std::uint16_t& value) {
  if (at + 2 > bytes.size()) return false;
  value = static_cast<std::uint16_t>(bytes[at] | (static_cast<std::uint16_t>(bytes[at + 1]) << 8));
  return true;
}

bool readU32(const std::vector<std::uint8_t>& bytes, std::size_t at, std::uint32_t& value) {
  if (at + 4 > bytes.size()) return false;
  value = static_cast<std::uint32_t>(bytes[at])
    | (static_cast<std::uint32_t>(bytes[at + 1]) << 8)
    | (static_cast<std::uint32_t>(bytes[at + 2]) << 16)
    | (static_cast<std::uint32_t>(bytes[at + 3]) << 24);
  return true;
}

bool fourcc(const std::vector<std::uint8_t>& bytes, std::size_t at, const char* value) {
  return at + 4 <= bytes.size() && std::memcmp(bytes.data() + at, value, 4) == 0;
}

std::string lower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return value;
}

// Source "sound characters" (src/public/soundchars.h): a wave path in a
// soundscript may carry one or two of them in front of the file name --
// `*` CHAR_STREAM, `#` CHAR_DRYMIX, `@` CHAR_OMNI, `>` CHAR_DOPPLER,
// `<` CHAR_DIRECTIONAL, `^` CHAR_DISTVARIANT, `)` CHAR_SPATIALSTEREO,
// `}` CHAR_FAST_PITCH, `!` CHAR_SENTENCE, `?` CHAR_USERVOX. The characters are
// playback metadata (stream from disc, bypass DSP, spatialize channels, ...),
// not part of the file name, and TF2's soundprecache stores the name exactly
// as written, so the run reaches this layer intact.
bool isSoundCharacter(char c) {
  switch (c) {
  case '*': case '#': case '@': case '>': case '<': case '^':
  case ')': case '}': case '!': case '?':
    return true;
  default:
    return false;
  }
}

} // namespace

std::size_t leadingSoundCharacterCount(const std::string& name) {
  // One or two characters, per the Soundscripts documentation ("All file paths
  // can be preceded with one or two sound characters"); the stacked cases
  // measured on real recordings are `)*`, `*#` and `#*`. A longer run is not a
  // case the documentation describes and is left alone, so it reads as a miss
  // instead of being silently over-stripped; a name made only of sound
  // characters has no file path left and is left whole for the same reason.
  std::size_t count = 0;
  while (count < 2 && count < name.size() && isSoundCharacter(name[count])) ++count;
  if (count == name.size()) return 0;
  return count;
}

bool parseWav(const std::vector<std::uint8_t>& bytes, WavPcmData& out) {
  out = {};
  if (bytes.size() < 12 || !fourcc(bytes, 0, "RIFF") || !fourcc(bytes, 8, "WAVE")) {
    out.error = "WAV RIFF/WAVE header is missing";
    return false;
  }
  bool hasFormat = false;
  bool hasData = false;
  std::size_t cursor = 12;
  while (cursor + 8 <= bytes.size()) {
    const std::size_t chunkHeader = cursor;
    std::uint32_t chunkSize = 0;
    if (!readU32(bytes, cursor + 4, chunkSize)) break;
    cursor += 8;
    if (chunkSize > bytes.size() - cursor) {
      out.error = "WAV chunk exceeds file size";
      return false;
    }
    if (fourcc(bytes, chunkHeader, "fmt ")) {
      if (chunkSize < 16) { out.error = "WAV fmt chunk is truncated"; return false; }
      std::uint16_t format = 0;
      if (!readU16(bytes, cursor, format) || !readU16(bytes, cursor + 2, out.channels)
          || !readU32(bytes, cursor + 4, out.sampleRate) || !readU32(bytes, cursor + 8, out.byteRate)
          || !readU16(bytes, cursor + 12, out.blockAlign) || !readU16(bytes, cursor + 14, out.bitsPerSample)) {
        out.error = "WAV fmt chunk is truncated";
        return false;
      }
      out.encoding = static_cast<WavEncoding>(format);
      hasFormat = true;
    } else if (fourcc(bytes, chunkHeader, "data")) {
      out.pcm.assign(bytes.begin() + static_cast<std::ptrdiff_t>(cursor),
        bytes.begin() + static_cast<std::ptrdiff_t>(cursor + chunkSize));
      hasData = true;
    }
    cursor += chunkSize + (chunkSize & 1u);
  }
  if (!hasFormat || !hasData) { out.error = "WAV fmt or data chunk is missing"; return false; }
  if (out.encoding != WavEncoding::Pcm && out.encoding != WavEncoding::IeeeFloat) {
    out.error = "WAV compression format is unsupported";
    return false;
  }
  if (out.channels == 0 || out.sampleRate == 0 || out.bitsPerSample == 0 || out.blockAlign == 0) {
    out.error = "WAV format has invalid channel/rate/bit depth";
    return false;
  }
  if (out.pcm.size() % out.blockAlign != 0) {
    out.error = "WAV data is not aligned to complete samples";
    return false;
  }
  out.valid = true;
  return true;
}

std::string normalizeSoundPath(const std::string& name, const char* extension) {
  // The sound characters are entry metadata, not part of the file path: no
  // shipped archive entry starts with one. Measured on the pinned pivotal
  // recording (2026-10-15): of 698 marked precache entries, 684 resolve to
  // shipped files once the marker run is dropped, and only 14 are missing
  // either way (server-custom content). Without the drop, all 698 -- including
  // stock sounds like weapons/shotgun_shoot.wav,
  // weapons/weapon_crit_charged_on.wav and the round-end music -- read as
  // missing and never play.
  std::string path = name.substr(leadingSoundCharacterCount(name));
  std::replace(path.begin(), path.end(), '\\', '/');
  while (!path.empty() && path.front() == '/') path.erase(path.begin());
  path = lower(path);
  if (path.rfind("sound/", 0) == 0) path.erase(0, 6);
  if (extension && *extension) {
    const std::string suffix = lower(extension);
    if (path.size() < suffix.size() || path.compare(path.size() - suffix.size(), suffix.size(), suffix) != 0) {
      path += suffix;
    }
  }
  return "sound/" + path;
}

bool readSoundWav(const VpkArchive& archive, const std::string& name,
  WavPcmData& out, std::string* resolvedPath) {
  out = {};
  const std::string path = normalizeSoundPath(name, ".wav");
  std::string error;
  const auto bytes = archive.read(path, &error);
  if (bytes.empty()) {
    out.error = error.empty() ? "sound entry is missing" : error;
    return false;
  }
  if (resolvedPath) *resolvedPath = path;
  return parseWav(bytes, out);
}

Mp3FrameInfo findMp3FrameHeader(const std::vector<std::uint8_t>& bytes) {
  Mp3FrameInfo info;
  std::size_t start = 0;
  // ID3v2 header: "ID3", version (2), flags (1), then a 4-byte size whose bytes
  // are all 7-bit. The tag is metadata; a decoder must start after it.
  if (bytes.size() >= 10 && bytes[0] == 'I' && bytes[1] == 'D' && bytes[2] == '3') {
    const std::uint32_t tagSize = (static_cast<std::uint32_t>(bytes[6] & 0x7fu) << 21)
      | (static_cast<std::uint32_t>(bytes[7] & 0x7fu) << 14)
      | (static_cast<std::uint32_t>(bytes[8] & 0x7fu) << 7)
      | static_cast<std::uint32_t>(bytes[9] & 0x7fu);
    if (tagSize > bytes.size() - 10u) return info;
    start = 10u + tagSize;
  }
  // Search the head of the stream, not all of it: a sync-shaped pair appears in
  // ordinary payload bytes often enough that scanning a whole track for one
  // would eventually find a valid-looking header inside the audio data. 4096
  // bytes is what the previous signature check used and what a real file needs
  // (the frame follows the tag immediately in every shipped TF2 MP3).
  const std::size_t end = std::min(bytes.size(), start + 4096u);
  static const std::uint32_t kSampleRates[3][3] = {
    {44100u, 48000u, 32000u},   // MPEG-1
    {22050u, 24000u, 16000u},   // MPEG-2
    {11025u, 12000u, 8000u},    // MPEG-2.5
  };
  static const std::uint32_t kBitrates[16] = {
    0u, 32u, 40u, 48u, 56u, 64u, 80u, 96u, 112u, 128u, 160u, 192u, 224u, 256u, 320u, 0u};
  for (std::size_t i = start; i + 3u < end; ++i) {
    if (bytes[i] != 0xffu || (bytes[i + 1] & 0xe0u) != 0xe0u) continue;
    const std::uint8_t versionBits = static_cast<std::uint8_t>((bytes[i + 1] >> 3) & 3u);
    const std::uint8_t layerBits = static_cast<std::uint8_t>((bytes[i + 1] >> 1) & 3u);
    const std::uint8_t bitrateIndex = static_cast<std::uint8_t>((bytes[i + 2] >> 4) & 15u);
    const std::uint8_t sampleRateIndex = static_cast<std::uint8_t>((bytes[i + 2] >> 2) & 3u);
    // versionBits 1 is reserved; layerBits 0 is reserved; bitrate 0 means free
    // format (no rate to report) and 15 is invalid; sample rate 3 is reserved.
    if (versionBits == 1u || layerBits == 0u) continue;
    if (bitrateIndex == 0u || bitrateIndex == 15u || sampleRateIndex == 3u) continue;
    const std::uint8_t channelMode = static_cast<std::uint8_t>((bytes[i + 3] >> 6) & 3u);
    info.frameOffset = i;
    info.mpegVersion = versionBits == 3u ? 1u : (versionBits == 2u ? 2u : 25u);
    info.layer3 = layerBits == 1u;
    info.bitrateKbps = kBitrates[bitrateIndex];
    info.sampleRate = kSampleRates[info.mpegVersion == 1u ? 0 : (info.mpegVersion == 2u ? 1 : 2)][sampleRateIndex];
    info.channels = channelMode == 3u ? 1u : 2u;
    info.valid = true;
    return info;
  }
  return info;
}

bool mp3DecoderAvailable() {
  // acmMetrics(ACM_METRIC_COUNT_DECODER) is not specific enough: it counts every
  // ACM decoder. Ask the codec list itself for one that accepts the MPEG
  // Layer-3 tag and can convert to PCM, which is the question a caller has.
  WAVEFORMATEX dst{};
  dst.wFormatTag = WAVE_FORMAT_PCM;
  dst.nChannels = 2;
  dst.nSamplesPerSec = 44100;
  dst.wBitsPerSample = 16;
  dst.nBlockAlign = 4;
  dst.nAvgBytesPerSec = 44100u * 4u;
  MPEGLAYER3WAVEFORMAT src{};
  src.wfx.wFormatTag = WAVE_FORMAT_MPEGLAYER3;
  src.wfx.nChannels = 2;
  src.wfx.nSamplesPerSec = 44100;
  src.wfx.nAvgBytesPerSec = 16000;   // 128 kbps
  src.wfx.nBlockAlign = 1;
  src.wfx.wBitsPerSample = 0;
  src.wfx.cbSize = MPEGLAYER3_WFX_EXTRA_BYTES;
  src.wID = MPEGLAYER3_ID_MPEG;
  src.fdwFlags = MPEGLAYER3_FLAG_PARSESTREAM;
  src.nBlockSize = 1;
  src.nFramesPerBlock = 1;
  src.nCodecDelay = 1393;
  HACMSTREAM stream = nullptr;
  const MMRESULT rc = acmStreamOpen(&stream, nullptr, reinterpret_cast<LPWAVEFORMATEX>(&src), &dst,
    nullptr, 0, 0, ACM_STREAMOPENF_NONREALTIME);
  if (rc == MMSYSERR_NOERROR) {
    acmStreamClose(stream, 0);
    return true;
  }
  return false;
}

Mp3DecodeStatus decodeMp3ToPcm(const std::vector<std::uint8_t>& bytes,
  WavPcmData& out, std::string& detail) {
  out = {};
  detail.clear();
  const Mp3FrameInfo frame = findMp3FrameHeader(bytes);
  if (!frame.valid) {
    detail = "MP3 carries no decodable frame signature";
    return Mp3DecodeStatus::InvalidStream;
  }
  if (!frame.layer3) {
    detail = "MPEG audio frame is not Layer III";
    return Mp3DecodeStatus::UnsupportedFormat;
  }
  if (!mp3DecoderAvailable()) {
    detail = "no MP3 decoder is installed (ACM MPEG Layer-3 codec missing)";
    return Mp3DecodeStatus::NoDecoderInstalled;
  }

  MPEGLAYER3WAVEFORMAT src{};
  src.wfx.wFormatTag = WAVE_FORMAT_MPEGLAYER3;
  src.wfx.nChannels = frame.channels;
  src.wfx.nSamplesPerSec = frame.sampleRate;
  // The ACM MPEG Layer-3 decoder reads nAvgBytesPerSec as the stream's average
  // bit rate; nBlockAlign 1 with PARSEstream tells it to take the framing from
  // the frame headers themselves rather than from a fixed block size.
  src.wfx.nAvgBytesPerSec = frame.bitrateKbps * 125u;
  src.wfx.nBlockAlign = 1;
  src.wfx.wBitsPerSample = 0;
  src.wfx.cbSize = MPEGLAYER3_WFX_EXTRA_BYTES;
  src.wID = MPEGLAYER3_ID_MPEG;
  src.fdwFlags = MPEGLAYER3_FLAG_PARSESTREAM;
  src.nBlockSize = 1;
  src.nFramesPerBlock = 1;
  src.nCodecDelay = 1393;

  WAVEFORMATEX dst{};
  dst.wFormatTag = WAVE_FORMAT_PCM;
  dst.nChannels = frame.channels;
  dst.nSamplesPerSec = frame.sampleRate;
  dst.wBitsPerSample = 16;
  dst.nBlockAlign = static_cast<WORD>(frame.channels * 2u);
  dst.nAvgBytesPerSec = frame.sampleRate * dst.nBlockAlign;
  dst.cbSize = 0;

  HACMSTREAM stream = nullptr;
  MMRESULT rc = acmStreamOpen(&stream, nullptr, reinterpret_cast<LPWAVEFORMATEX>(&src), &dst,
    nullptr, 0, 0, ACM_STREAMOPENF_NONREALTIME);
  if (rc != MMSYSERR_NOERROR) {
    detail = "ACM refused the MP3 source format (rate " + std::to_string(frame.sampleRate)
      + ", " + std::to_string(frame.channels) + " channel(s), " + std::to_string(frame.bitrateKbps)
      + " kbps, result " + std::to_string(rc) + ")";
    return Mp3DecodeStatus::UnsupportedFormat;
  }

  const std::uint8_t* feed = bytes.data() + frame.frameOffset;
  const std::size_t feedBytes = bytes.size() - frame.frameOffset;
  if (feedBytes > 0xffffffffu) {
    acmStreamClose(stream, 0);
    detail = "MP3 is larger than the ACM interface accepts";
    return Mp3DecodeStatus::UnsupportedFormat;
  }
  DWORD capacity = 0;
  rc = acmStreamSize(stream, static_cast<DWORD>(feedBytes), &capacity, ACM_STREAMSIZEF_SOURCE);
  if (rc != MMSYSERR_NOERROR || capacity == 0) {
    // The size query is an estimate; a decoder that will not answer it can still
    // decode, so fall back to a bound a 320 kbps stream cannot exceed (a decoded
    // PCM stream is at most about 1.45 MB per second at 44.1 kHz stereo, and the
    // duration cap below refuses anything absurd afterwards).
    capacity = static_cast<DWORD>(std::min<std::size_t>(
      feedBytes * 8u + 65536u, 64u * 1024u * 1024u));
  }
  out.pcm.assign(static_cast<std::size_t>(capacity) + 4096u, 0);
  ACMSTREAMHEADER header{};
  header.cbStruct = sizeof(header);
  header.pbSrc = const_cast<LPBYTE>(feed);
  header.cbSrcLength = static_cast<DWORD>(feedBytes);
  header.pbDst = out.pcm.data();
  header.cbDstLength = static_cast<DWORD>(out.pcm.size());
  rc = acmStreamPrepareHeader(stream, &header, 0);
  if (rc != MMSYSERR_NOERROR) {
    acmStreamClose(stream, 0);
    detail = "ACM refused the MP3 buffer (result " + std::to_string(rc) + ")";
    return Mp3DecodeStatus::InvalidStream;
  }
  rc = acmStreamConvert(stream, &header, ACM_STREAMCONVERTF_END);
  const DWORD produced = header.cbDstLengthUsed;
  const DWORD consumed = header.cbSrcLengthUsed;
  acmStreamUnprepareHeader(stream, &header, 0);
  acmStreamClose(stream, 0);

  if (rc != MMSYSERR_NOERROR) {
    detail = "ACM could not convert the MP3 stream (result " + std::to_string(rc) + ")";
    return Mp3DecodeStatus::InvalidStream;
  }
  if (produced == 0) {
    // A stream that carries sync-looking bytes but no audio lands here: the
    // decoder ran, consumed bytes and produced nothing. Named separately from a
    // missing frame, because "the bytes look like MP3 and yield no sound" is a
    // different thing to investigate than "the bytes are not MP3".
    detail = "ACM decoded 0 PCM bytes from " + std::to_string(consumed) + " MP3 bytes";
    return Mp3DecodeStatus::InvalidStream;
  }
  out.pcm.resize(produced);
  out.encoding = WavEncoding::Pcm;
  out.channels = frame.channels;
  out.sampleRate = frame.sampleRate;
  out.bitsPerSample = 16;
  out.blockAlign = dst.nBlockAlign;
  out.byteRate = dst.nAvgBytesPerSec;
  out.valid = true;
  detail = "decoded " + std::to_string(produced) + " PCM bytes at " + std::to_string(frame.sampleRate)
    + " Hz " + std::to_string(frame.channels) + " channel(s)";
  return Mp3DecodeStatus::Decoded;
}

} // namespace tf2::native
