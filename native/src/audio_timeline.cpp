#include "audio_timeline.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <limits>
#include <utility>

namespace tf2::native {
namespace {

SoundEventKind classify(const std::string& name) {
  return classifySoundEvent(name);
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

// Plain substring match on the lowercased name. The gameplay tokens below keep
// this looser rule on purpose: "player/footstep1.wav" must classify as a
// footstep, the digit is part of the name, and audio_effects_probe pins that
// outcome. The filtered and reserved groups use hasPathToken instead, so a
// broadcast token has to sit on a path boundary.
bool hasSubstring(const std::string& name, const char* token) {
  std::string lowerName = name;
  std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return lowerName.find(token) != std::string::npos;
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

// The candidate archive paths for one precache name, mirroring
// readSoundResource: a name that already carries an extension is used as-is,
// otherwise .wav is tried before .mp3.
std::vector<std::string> soundPathCandidates(const std::string& name) {
  const std::string base = normalizeSoundPath(name, nullptr);
  const auto slash = base.find_last_of('/');
  const auto dot = base.find_last_of('.');
  const bool hasExtension = dot != std::string::npos && (slash == std::string::npos || dot > slash);
  std::vector<std::string> candidates;
  if (hasExtension) candidates.push_back(base);
  else { candidates.push_back(base + ".wav"); candidates.push_back(base + ".mp3"); }
  return candidates;
}

} // namespace

const char* soundEventKindName(SoundEventKind kind) {
  switch (kind) {
  case SoundEventKind::Weapon: return "weapon";
  case SoundEventKind::Footstep: return "footstep";
  case SoundEventKind::Uber: return "uber";
  case SoundEventKind::World: return "world";
  case SoundEventKind::TempEntity: return "temp_entity";
  case SoundEventKind::Pcf: return "pcf";
  case SoundEventKind::Voice: return "voice";
  case SoundEventKind::Announcer: return "announcer";
  case SoundEventKind::Music: return "music";
  default: return "unknown";
  }
}

const char* soundResolveStatusName(SoundResolveStatus status) {
  switch (status) {
  case SoundResolveStatus::Resolved: return "resolved";
  case SoundResolveStatus::MissingPrecache: return "missing_precache";
  case SoundResolveStatus::MissingWav: return "missing_wav";
  case SoundResolveStatus::InvalidWav: return "invalid_wav";
  case SoundResolveStatus::UnsupportedFormat: return "unsupported_format";
  default: return "unknown";
  }
}

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
  if (!(event.volume >= 0.0f && event.volume <= 1.0f)) return false;
  if (!std::isfinite(event.origin[0]) || !std::isfinite(event.origin[1]) || !std::isfinite(event.origin[2])) return false;
  // The kind follows the name, decided in exactly one place: a caller cannot
  // relabel a voice name as weapon audio, and the nine-way classification
  // cannot drift between the index table, this container and the scheduler.
  event.kind = classifySoundEvent(event.name);
  // Voice, announcer and music are kept, flagged, and skipped by the scheduler
  // at advance time. Dropping them here instead would leave
  // eventsSkippedVoice a counter nothing can ever move, and scheduleGameplaySound
  // is already the layer that refuses them for gameplay scheduling.
  event.skipVoice = event.kind == SoundEventKind::Voice
    || event.kind == SoundEventKind::Announcer || event.kind == SoundEventKind::Music;
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
  for (const auto& path : soundPathCandidates(name)) {
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

bool soundNameIsVoiceOrMusic(const std::string& name) {
  const SoundEventKind kind = classifySoundEvent(name);
  return kind == SoundEventKind::Voice || kind == SoundEventKind::Announcer
    || kind == SoundEventKind::Music;
}

SoundEventKind classifySoundEvent(const std::string& rawName) {
  // The filtered kinds come first: a name carrying a broadcast token is
  // skipped even when it also carries a gameplay token, because
  // "vo/killstreak/announcer_ks_26.mp3" is an announcer line that arrived
  // through the vo archive, not a weapon sound.
  if (hasPathToken(rawName, "announcer") || hasPathToken(rawName, "commentary")) return SoundEventKind::Announcer;
  if (hasPathToken(rawName, "vo") || hasPathToken(rawName, "voice") || hasPathToken(rawName, "radio")) return SoundEventKind::Voice;
  if (hasPathToken(rawName, "music")) return SoundEventKind::Music;
  // Gameplay tokens keep the substring rule they always had (see
  // hasSubstring): "player/footstep1.wav" is a footstep, digit and all.
  if (hasSubstring(rawName, "footstep") || hasSubstring(rawName, "step")) return SoundEventKind::Footstep;
  if (hasSubstring(rawName, "uber") || hasSubstring(rawName, "invulnerable")) return SoundEventKind::Uber;
  if (hasSubstring(rawName, "weapon") || hasSubstring(rawName, "rocket")
      || hasSubstring(rawName, "scatter") || hasSubstring(rawName, "shot")) return SoundEventKind::Weapon;
  // Reserved for sound names routed from the temp-entity and particle channels.
  // No shipped sound name carries these tokens today; the probe measures that
  // rather than assuming it.
  if (hasPathToken(rawName, "tempent") || hasPathToken(rawName, "temp_entity")) return SoundEventKind::TempEntity;
  if (hasPathToken(rawName, "particles") || hasPathToken(rawName, "pcf")) return SoundEventKind::Pcf;
  return SoundEventKind::World;
}

std::int32_t applySoundDelayTicks(std::int32_t tick, float delaySeconds, double tickRate) {
  if (tick < 0 || !std::isfinite(delaySeconds) || !std::isfinite(tickRate) || tickRate <= 0.0) return -1;
  const double shifted = static_cast<double>(tick) + static_cast<double>(delaySeconds) * tickRate;
  if (!std::isfinite(shifted)) return -1;
  const long long rounded = std::llround(shifted);
  if (rounded < 0) return 0;
  if (rounded > static_cast<long long>(std::numeric_limits<std::int32_t>::max())) {
    return std::numeric_limits<std::int32_t>::max();
  }
  return static_cast<std::int32_t>(rounded);
}

bool scheduleGameplaySound(SoundEventTimeline& timeline, ScheduledSound sound, double tickRate) {
  if (sound.soundIndex == 0 || sound.name.empty() || soundNameIsVoiceOrMusic(sound.name)) return false;
  if (!std::isfinite(sound.volume)) return false;
  sound.volume = std::clamp(sound.volume, 0.0f, 1.0f);
  const std::int32_t scheduledTick = applySoundDelayTicks(sound.tick, sound.delaySeconds, tickRate);
  if (scheduledTick < 0) return false;
  SoundPlaybackEvent event;
  event.tick = scheduledTick;
  event.soundIndex = sound.soundIndex;
  event.name = std::move(sound.name);
  event.kind = classifySoundEvent(event.name);
  event.volume = sound.volume;
  event.origin[0] = sound.origin[0];
  event.origin[1] = sound.origin[1];
  event.origin[2] = sound.origin[2];
  event.skipVoice = false;
  return timeline.add(std::move(event));
}

SoundKindCounts countSoundKinds(const SoundEventTimeline& timeline) {
  SoundKindCounts counts;
  for (const auto& event : timeline.events()) {
    if (event.kind == SoundEventKind::Weapon) ++counts.weapon;
    else if (event.kind == SoundEventKind::Footstep) ++counts.footstep;
    else if (event.kind == SoundEventKind::Uber) ++counts.uber;
    else if (event.kind == SoundEventKind::World) ++counts.world;
    else if (event.kind == SoundEventKind::TempEntity) ++counts.tempEntity;
    else if (event.kind == SoundEventKind::Pcf) ++counts.pcf;
    else if (event.kind == SoundEventKind::Voice) ++counts.voice;
    else if (event.kind == SoundEventKind::Announcer) ++counts.announcer;
    else if (event.kind == SoundEventKind::Music) ++counts.music;
  }
  return counts;
}

SoundResolveStatus resolveSoundIndexResource(const SoundIndexTable& table,
  const VpkArchive& archive, std::uint32_t soundIndex,
  SoundResource& out, std::string& detail) {
  out = {};
  detail.clear();
  const SoundIndexEntry* entry = table.find(soundIndex);
  if (entry == nullptr) {
    detail = "sound index " + std::to_string(soundIndex) + " has no precache entry";
    return SoundResolveStatus::MissingPrecache;
  }
  // SoundIndexTable::set already rejects unsafe names, so this is a defensive
  // stop rather than a reachable branch; it is still named, not silently
  // resolved, because an entry that got here some other way is a table bug.
  if (!safeName(entry->name)) {
    detail = "precache name for index " + std::to_string(soundIndex) + " is not a safe path";
    return SoundResolveStatus::MissingPrecache;
  }
  const auto candidates = soundPathCandidates(entry->name);
  std::string present;
  for (const auto& candidate : candidates) {
    if (archive.contains(candidate)) { present = candidate; break; }
  }
  if (present.empty()) {
    detail = "no WAV/MP3 entry for precache name '" + entry->name + "'";
    return SoundResolveStatus::MissingWav;
  }
  std::string readError;
  const auto bytes = archive.read(present, &readError);
  if (bytes.empty()) {
    detail = "archive entry '" + present + "' yielded no bytes";
    if (!readError.empty()) detail += ": " + readError;
    return SoundResolveStatus::MissingWav;
  }
  out.archivePath = present;
  out.bytes = bytes;
  const auto ext = present.substr(present.find_last_of('.') + 1);
  if (ext == "wav") {
    out.codec = SoundCodec::Wav;
    if (!parseWav(bytes, out.wav)) {
      out.error = out.wav.error;
      detail = "WAV '" + present + "' does not parse: " + out.wav.error;
      return SoundResolveStatus::InvalidWav;
    }
  } else if (ext == "mp3") {
    out.codec = SoundCodec::Mp3;
    if (!hasMp3Frame(bytes)) {
      out.error = "MP3 frame signature is missing";
      detail = "MP3 '" + present + "' carries no decodable frame signature";
      return SoundResolveStatus::InvalidWav;
    }
  } else {
    out.codec = SoundCodec::Unknown;
    out.error = "unsupported sound extension";
    detail = "sound '" + present + "' is neither WAV nor MP3";
    return SoundResolveStatus::UnsupportedFormat;
  }
  // Found and parsed, but not playable: an MP3 has no PCM decode in this
  // backend, and a WAV whose shape the output path rejects is just as
  // unplayable. Both are resource facts, not byte corruption.
  if (!wavPcmIsPlayable(out.wav)) {
    out.error = out.codec == SoundCodec::Mp3
      ? "MP3 is not decoded by the native PCM output path"
      : "parsed WAV is not playable by the native PCM output path";
    detail = out.error + " (" + present + ")";
    return SoundResolveStatus::UnsupportedFormat;
  }
  out.valid = true;
  detail = "resolved '" + present + "' channels=" + std::to_string(out.wav.channels)
    + " rate=" + std::to_string(out.wav.sampleRate)
    + " bits=" + std::to_string(out.wav.bitsPerSample)
    + " bytes=" + std::to_string(out.wav.pcm.size());
  return SoundResolveStatus::Resolved;
}

bool wavPcmIsPlayable(const WavPcmData& wav) {
  return wav.valid && wav.encoding == WavEncoding::Pcm
    && (wav.bitsPerSample == 8 || wav.bitsPerSample == 16)
    && wav.channels >= 1 && wav.channels <= 2 && wav.sampleRate != 0
    && wav.blockAlign != 0 && !wav.pcm.empty() && wav.pcm.size() % wav.blockAlign == 0;
}

} // namespace tf2::native
