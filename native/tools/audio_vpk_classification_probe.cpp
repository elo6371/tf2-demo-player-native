#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "audio_timeline.h"

using namespace tf2::native;

namespace {
const char* label(SoundEventKind kind) {
  switch (kind) {
  case SoundEventKind::Weapon: return "weapon";
  case SoundEventKind::Footstep: return "footstep";
  case SoundEventKind::Uber: return "uber";
  case SoundEventKind::World: return "world";
  default: return "unknown";
  }
}
}

int main() {
  const std::filesystem::path tfRoot = std::getenv("TF_ROOT")
    ? std::filesystem::path(std::getenv("TF_ROOT"))
    : std::filesystem::path("D:/SteamLibrary/steamapps/common/Team Fortress 2/tf");
  const std::vector<std::string> archives = {
    "tf2_sound_misc_dir.vpk", "tf2_sound_vo_english_dir.vpk"};
  std::size_t archiveCount = 0;
  std::size_t wavEntries = 0;
  std::size_t wavParsed = 0;
  std::size_t wavFailures = 0;
  std::size_t skippedVoice = 0;
  std::size_t categoryCounts[5] = {};
  std::size_t categoryParsed[5] = {};
  std::vector<std::string> examples[5];

  for (const auto& archiveName : archives) {
    VpkArchive archive;
    std::string error;
    if (!archive.open(tfRoot / archiveName, &error)) continue;
    ++archiveCount;
    const auto wavPaths = archive.list({}, ".wav");
    for (const auto& path : wavPaths) {
      ++wavEntries;
      const auto name = path;
      SoundIndexTable table;
      const auto index = static_cast<std::uint32_t>(wavEntries);
      if (!table.set(index, name)) { std::cerr << "set_failed=" << name << "\n"; return 2; }
      const auto* entry = table.find(index);
      if (!entry) { std::cerr << "find_failed index=" << index << " size=" << table.size() << "\n"; return 3; }
      const auto category = static_cast<std::size_t>(entry->kind);
      if (category >= 5) { std::cerr << "category_failed=" << category << "\n"; return 4; }
      ++categoryCounts[category];
      if (entry->kind == SoundEventKind::Unknown || entry->kind == SoundEventKind::World
          || entry->name.find("vo/") == 0 || entry->name.find("vo_english/") == 0) {
        ++skippedVoice;
        continue;
      }
      if (examples[category].size() < 3) examples[category].push_back(entry->name);
      SoundResource resource;
      if (readSoundResource(archive, entry->name, resource) && resource.valid
          && resource.codec == SoundCodec::Wav && resource.wav.valid) {
        ++wavParsed;
        ++categoryParsed[category];
      } else {
        ++wavFailures;
      }
    }
  }
  if (archiveCount == 0 || wavEntries == 0 || wavParsed == 0) {
    std::cout << "archives=" << archiveCount << " wav_entries=" << wavEntries
              << " wav_parsed=" << wavParsed << " wav_failures=" << wavFailures
              << " status=fail\n";
    return 5;
  }
  std::cout << "archives=" << archiveCount << " wav_entries=" << wavEntries
            << " wav_parsed=" << wavParsed << " wav_failures=" << wavFailures
            << " skipped_voice_or_world=" << skippedVoice << "\n";
  for (std::size_t i = 1; i < 4; ++i) {
    std::cout << "kind=" << label(static_cast<SoundEventKind>(i))
              << " classified=" << categoryCounts[i] << " parsed=" << categoryParsed[i];
    for (const auto& example : examples[i]) std::cout << " example=" << example;
    std::cout << "\n";
  }
  std::cout << "device_opened=0 status=pass\n";
  return 0;
}
