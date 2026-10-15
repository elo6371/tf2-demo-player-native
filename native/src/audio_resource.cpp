#include "audio_resource.h"

#include <algorithm>
#include <cctype>
#include <cstring>

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

} // namespace tf2::native
