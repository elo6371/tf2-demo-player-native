#include "audio_timeline.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <utility>

namespace tf2::native {
namespace {

SoundEventKind classify(std::string name) {
  std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  if (name.find("footstep") != std::string::npos || name.find("step") != std::string::npos) return SoundEventKind::Footstep;
  if (name.find("uber") != std::string::npos || name.find("invulnerable") != std::string::npos) return SoundEventKind::Uber;
  if (name.find("weapon") != std::string::npos || name.find("weapons/") != std::string::npos
      || name.find("rocket") != std::string::npos
      || name.find("scatter") != std::string::npos || name.find("shot") != std::string::npos) return SoundEventKind::Weapon;
  return SoundEventKind::World;
}

bool safeName(const std::string& name) {
  if (name.empty() || name.find('\0') != std::string::npos) return false;
  std::string part;
  for (std::size_t i = 0; i <= name.size(); ++i) {
    const char c = i == name.size() ? '/' : name[i];
    if (c == '\\' || c == '/') {
      if (part == ".." || part.empty() && (i == 0 || i == name.size())) return false;
      part.clear();
    } else {
      part += c;
    }
  }
  return true;
}

bool hasPathToken(const std::string& name, const char* token) {
  std::string lowerName = name;
  std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), [](unsigned char c) {
    return c == '\\' ? '/' : static_cast<char>(std::tolower(c));
  });
  const std::size_t tokenLength = std::char_traits<char>::length(token);
  std::size_t at = lowerName.find(token);
  while (at != std::string::npos) {
    const bool leftBoundary = at == 0 || lowerName[at - 1] == '/' || lowerName[at - 1] == '_' || lowerName[at - 1] == '-';
    const std::size_t after = at + tokenLength;
    const bool rightBoundary = after == lowerName.size() || lowerName[after] == '/' || lowerName[after] == '.'
      || lowerName[after] == '_' || lowerName[after] == '-';
    if (leftBoundary && rightBoundary) return true;
    at = lowerName.find(token, at + 1);
  }
  return false;
}

bool isNonGameplayAudio(const std::string& name) {
  return hasPathToken(name, "vo") || hasPathToken(name, "voice")
    || hasPathToken(name, "announcer") || hasPathToken(name, "radio")
    || hasPathToken(name, "music") || hasPathToken(name, "commentary");
}

bool hasMp3Frame(const std::vector<std::uint8_t>& bytes) {
  std::size_t start = 0;
  if (bytes.size() >= 10 && bytes[0] == 'I' && bytes[1] == 'D' && bytes[2] == '3') {
    const std::uint32_t tagSize = ((bytes[6] & 0x7fu) << 21) | ((bytes[7] & 0x7fu) << 14)
      | ((bytes[8] & 0x7fu) << 7) | (bytes[9] & 0x7fu);
    if (tagSize > bytes.size() - 10) return false;
    start = 10 + tagSize;
  }
  const std::size_t end = std::min(bytes.size(), start + 4096u);
  for (std::size_t i = start; i + 1 < end; ++i) {
    if (bytes[i] != 0xff || (bytes[i + 1] & 0xe0u) != 0xe0u) continue;
    const std::uint8_t layer = static_cast<std::uint8_t>((bytes[i + 1] >> 1) & 3u);
    const std::uint8_t bitrate = static_cast<std::uint8_t>((bytes[i + 2] >> 4) & 15u);
    const std::uint8_t sampleRate = static_cast<std::uint8_t>((bytes[i + 2] >> 2) & 3u);
    if (layer != 0 && bitrate != 0 && bitrate != 15 && sampleRate != 3) return true;
  }
  return false;
}

} // namespace

void SoundIndexTable::clear() { entries_.clear(); }

bool SoundIndexTable::set(std::uint32_t index, std::string name) {
  if (index == 0 || !safeName(name)) return false;
  SoundIndexEntry entry;
  entry.index = index;
  entry.kind = classify(name);
  entry.name = std::move(name);
  entries_[index] = std::move(entry);
  return true;
}

const SoundIndexEntry* SoundIndexTable::find(std::uint32_t index) const {
  const auto it = entries_.find(index);
  return it == entries_.end() ? nullptr : &it->second;
}

SpatialMix computeSpatialMix(const float source[3], const float listener[3],
    float maxDistance, float rolloff) {
  if (!source || !listener || !std::isfinite(maxDistance) || maxDistance <= 0.0f
      || !std::isfinite(rolloff) || rolloff < 0.0f) return {};
  const float dx = source[0] - listener[0];
  const float dy = source[1] - listener[1];
  const float dz = source[2] - listener[2];
  if (!std::isfinite(dx) || !std::isfinite(dy) || !std::isfinite(dz)) return {};
  const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
  if (!std::isfinite(distance) || distance >= maxDistance) return {};
  const float normalizedDistance = std::clamp(distance / maxDistance, 0.0f, 1.0f);
  SpatialMix result;
  result.gain = std::clamp(std::pow(1.0f - normalizedDistance, rolloff), 0.0f, 1.0f);
  const float horizontalDistance = std::hypot(dx, dy);
  if (horizontalDistance > 0.0001f) result.pan = std::clamp(dy / horizontalDistance, -1.0f, 1.0f);
  return result;
}

void SoundEventTimeline::clear() { events_.clear(); }

bool SoundEventTimeline::add(SoundPlaybackEvent event) {
  if (event.tick < 0 || event.soundIndex == 0 || event.name.empty()) return false;
  if (isNonGameplayAudio(event.name)) return false;
  if (!(event.volume >= 0.0f && event.volume <= 1.0f)) return false;
  if (!std::isfinite(event.origin[0]) || !std::isfinite(event.origin[1]) || !std::isfinite(event.origin[2])) return false;
  if (event.kind == SoundEventKind::Weapon || event.kind == SoundEventKind::Footstep
      || event.kind == SoundEventKind::Uber || event.kind == SoundEventKind::World) {
    event.skipVoice = false;
  }
  events_.push_back(std::move(event));
  return true;
}

void SoundEventTimeline::sortByTick() {
  std::stable_sort(events_.begin(), events_.end(), [](const auto& a, const auto& b) { return a.tick < b.tick; });
}

std::size_t SoundEventTimeline::countAtTick(std::int32_t tick) const {
  return static_cast<std::size_t>(std::count_if(events_.begin(), events_.end(),
    [tick](const auto& event) { return event.tick == tick; }));
}

std::vector<SoundPlaybackEvent> SoundEventTimeline::range(std::int32_t firstTick, std::int32_t lastTick) const {
  if (firstTick > lastTick) return {};
  std::vector<SoundPlaybackEvent> result;
  const auto begin = std::lower_bound(events_.begin(), events_.end(), firstTick,
    [](const auto& event, std::int32_t tick) { return event.tick < tick; });
  for (auto it = begin; it != events_.end() && it->tick <= lastTick; ++it) result.push_back(*it);
  return result;
}

void SoundEventTimeline::forEachRange(std::int32_t firstTick, std::int32_t lastTick,
    const std::function<void(const SoundPlaybackEvent&)>& visitor) const {
  if (!visitor) return;
  if (firstTick > lastTick) return;
  const auto begin = std::lower_bound(events_.begin(), events_.end(), firstTick,
    [](const auto& event, std::int32_t tick) { return event.tick < tick; });
  for (auto it = begin; it != events_.end() && it->tick <= lastTick; ++it) visitor(*it);
}

bool readSoundResource(const VpkArchive& archive, const std::string& name,
  SoundResource& out, std::size_t maxBytes) {
  out = {};
  if (!safeName(name) || maxBytes == 0) { out.error = "unsafe or empty sound name"; return false; }
  std::string base = normalizeSoundPath(name, nullptr);
  const auto slash = base.find_last_of('/');
  const auto dot = base.find_last_of('.');
  const bool hasExtension = dot != std::string::npos && (slash == std::string::npos || dot > slash);
  std::vector<std::string> candidates;
  if (hasExtension) candidates.push_back(base);
  else { candidates.push_back(base + ".wav"); candidates.push_back(base + ".mp3"); }
  for (const auto& path : candidates) {
    if (!archive.contains(path)) continue;
    std::string error;
    const auto bytes = archive.read(path, &error);
    if (bytes.empty()) { out.error = error.empty() ? "sound entry is empty" : error; return false; }
    if (bytes.size() > maxBytes) { out.error = "sound entry exceeds safety limit"; return false; }
    out.archivePath = path;
    out.bytes = bytes;
    const auto ext = path.substr(path.find_last_of('.') + 1);
    if (ext == "wav") {
      out.codec = SoundCodec::Wav;
      if (!parseWav(bytes, out.wav)) { out.error = out.wav.error; return false; }
    } else if (ext == "mp3") {
      out.codec = SoundCodec::Mp3;
      if (!hasMp3Frame(bytes)) { out.error = "MP3 frame signature is missing"; return false; }
    } else {
      out.error = "unsupported sound extension";
      return false;
    }
    out.valid = true;
    return true;
  }
  out.error = "WAV/MP3 sound entry is missing";
  return false;
}

} // namespace tf2::native
