// sound_content_map_probe.cpp -- the traceable content map for one recording's
// sound events: from the demo's own soundprecache string table, through the
// scheduled timeline, to the exact VPK path and decode status each event would
// resolve to on the product's archive chain.
//
// WHY THIS EXISTS
//   The timeline census proves WHERE the events sit (tick origin, spread,
//   duplicate and reverse guards) and the player's summary proves how many of
//   them the scheduler moved through. Neither answers the content question the
//   T3 acceptance asks: for each category the demo actually carries -- weapon,
//   footstep, uber, world, voice, announcer, music -- which resource does an
//   event end up as, and did that resource decode? This probe walks the same
//   chain the product walks (precache table -> relative tick -> delay fold ->
//   scheduleSoundEvent -> the product's archive order) and writes one line per
//   scheduled event, plus the counts a gate can assert against:
//
//     scheduled = the timeline size the player also reports
//     resolved / missing_precache / missing_wav / invalid_wav /
//     unsupported_format / excessive_duration = one named decode status per
//       scheduled event, so the six counts must sum to the timeline size
//     filtered = voice + announcer + music (scheduled, never resolved, never
//       played)
//     played = the device-free sink accepted this many real PCM buffers
//     rejected = read but never scheduled (unresolved + unplaceable + refused)
//     duplicate / reverse = the same-tick guard and the backward reset, driven
//       over the real timeline with the real resolver
//
// WHAT IT DOES NOT DO
//   It never opens an audio device. The scheduler is driven with an injected
//   playback sink, so "played" here means "a real decoded PCM buffer was handed
//   to a device-free sink", not "someone heard it".
//
// Usage:
//   sound_content_map_probe --demo <path.dem> [--tf-root <tf dir>]
//                           [--map-out <file>] [--table-out <file>]
//   TF_ROOT is honoured when --tf-root is absent.
// Exit: 0 = the map was produced; 1 = the demo could not be read; 2 = usage.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "audio_player.h"
#include "audio_timeline.h"
#include "demo_header.h"
#include "vpk_archive.h"

namespace {

constexpr const char* kStatusOk = "ok";
constexpr const char* kStatusFailed = "failed";

// The product's archive chain, in the product's order (main.cpp opens exactly
// these five under the tf directory and the resolver iterates them in this
// order). Kept here as data so a sixth archive added to the product shows up
// as a diff in this list, not as a silent disagreement about where bytes come
// from.
const std::vector<const char*>& productArchiveNames() {
  static const std::vector<const char*> names = {
    "pak01_dir.vpk", "tf2_misc_dir.vpk", "tf2_textures_dir.vpk",
    "tf2_sound_misc_dir.vpk", "tf2_sound_vo_english_dir.vpk"};
  return names;
}

struct ResourceAnswer {
  tf2::native::SoundResolveStatus status = tf2::native::SoundResolveStatus::MissingWav;
  std::string path;    // the archive path that resolved, empty otherwise
  std::string detail;  // the named reason, or the resolved format summary
  double seconds = -1; // playback duration when the bytes parsed as WAV
};

std::string formatSeconds(double seconds) {
  if (!(seconds >= 0.0) || !std::isfinite(seconds)) return "-";
  char buffer[64]{};
  std::snprintf(buffer, sizeof(buffer), "%.1f", seconds);
  return buffer;
}

} // namespace

int main(int argc, char** argv) {
  std::string demoPath;
  std::string tfRoot;
  std::string mapOutPath;
  std::string tableOutPath;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--demo" && i + 1 < argc) { demoPath = argv[++i]; }
    else if (arg == "--tf-root" && i + 1 < argc) { tfRoot = argv[++i]; }
    else if (arg == "--map-out" && i + 1 < argc) { mapOutPath = argv[++i]; }
    else if (arg == "--table-out" && i + 1 < argc) { tableOutPath = argv[++i]; }
    else {
      std::cout << "{\"status\":\"failed\",\"reason\":\"unknown argument: " << arg << "\"}\n";
      return 2;
    }
  }
  if (demoPath.empty()) {
    std::cout << "{\"status\":\"failed\",\"reason\":\"usage: --demo <path.dem> [--tf-root <dir>] [--map-out <file>] [--table-out <file>]\"}\n";
    return 2;
  }
  if (tfRoot.empty()) {
    const char* env = std::getenv("TF_ROOT");
    tfRoot = env ? env : "D:/SteamLibrary/steamapps/common/Team Fortress 2/tf";
  }

  // ---- the demo, exactly the chain main.cpp runs ---------------------------
  tf2::native::DemoHeader header;
  if (!tf2::native::parseDemoHeaderFile(demoPath, header)) {
    std::cout << "{\"status\":\"failed\",\"reason\":\"demo header parse failed\"}\n";
    return 1;
  }
  tf2::native::DemoIndex index;
  tf2::native::indexDemoFile(demoPath, header, index);
  tf2::native::DemoNetworkSummary summary;
  summary.networkProtocol = header.networkProtocol;
  if (!tf2::native::scanKnownDemoMessages(demoPath, index, summary)) {
    std::cout << "{\"status\":\"failed\",\"reason\":\"demo scan failed\"}\n";
    return 1;
  }

  // The string table every event's name comes from: the demo's own
  // soundprecache table. Rebuilt here as the SoundIndexTable the named resolve
  // chain reads, so "which table did this name come from" is a reading
  // (soundPrecacheEntries / decode failures) rather than an assumption.
  tf2::native::SoundIndexTable indexTable;
  std::size_t precacheEntries = 0, precacheSetFailures = 0;
  for (const auto& [soundIndex, name] : summary.soundPrecache) {
    if (indexTable.set(soundIndex, name)) ++precacheEntries;
    else ++precacheSetFailures;
  }
  // The table's own kind histogram: what the recording *precaches* is a
  // candidate, not an event. A taunt's music can sit in the table and never be
  // referenced by a sound message inside the recorded window, so the two
  // histograms are kept apart -- collapsing them would report "music was
  // played" for a recording that never played it.
  std::size_t tableKinds[10] = {};  // indexed by SoundEventKind
  std::vector<std::pair<std::uint16_t, std::string>> tableSorted;
  tableSorted.reserve(summary.soundPrecache.size());
  for (const auto& [soundIndex, name] : summary.soundPrecache) {
    ++tableKinds[static_cast<std::size_t>(tf2::native::classifySoundEvent(name))];
    tableSorted.emplace_back(soundIndex, name);
  }
  std::sort(tableSorted.begin(), tableSorted.end(),
    [](const auto& a, const auto& b) { return a.first < b.first; });

  std::size_t tableLines = 0;
  if (!tableOutPath.empty()) {
    std::ofstream table(tableOutPath, std::ios::binary);
    if (!table) {
      std::cout << "{\"status\":\"failed\",\"reason\":\"cannot open --table-out file\"}\n";
      return 2;
    }
    table << "# soundprecache table\n";
    table << "# demo=" << std::filesystem::path(demoPath).filename().string()
          << " entries=" << tableSorted.size()
          << " decodeFailures=" << summary.soundPrecacheDecodeFailures << "\n";
    table << "# columns: index\tkind\tname\n";
    for (const auto& [soundIndex, name] : tableSorted) {
      table << soundIndex << '\t' << tf2::native::soundEventKindName(tf2::native::classifySoundEvent(name))
            << '\t' << name << '\n';
      ++tableLines;
    }
  }

  bool originFound = false;
  const std::int32_t origin = tf2::native::firstRecordedPacketTick(index, originFound);

  double tickRate = 66.6666667;
  if (header.playbackTime > 0.0f && header.ticks > 0) {
    tickRate = std::clamp(static_cast<double>(header.ticks)
      / static_cast<double>(header.playbackTime), 30.0, 128.0);
  }

  tf2::native::SoundEventTimeline timeline;
  std::size_t read = summary.decodedSoundEvents.size();
  std::size_t unresolved = 0, unplaceable = 0, refused = 0;
  for (const auto& source : summary.decodedSoundEvents) {
    if (source.resourceName.empty()) { ++unresolved; continue; }
    std::int32_t relativeTick = 0;
    if (!originFound
        || !tf2::native::soundEventRelativeTick(source.tick, origin, relativeTick)) {
      ++unplaceable;
      continue;
    }
    tf2::native::ScheduledSound sound;
    sound.tick = relativeTick;
    sound.soundIndex = source.soundIndex;
    sound.name = source.resourceName;
    sound.volume = source.volume;
    sound.delaySeconds = source.delaySeconds;
    sound.origin[0] = source.origin[0];
    sound.origin[1] = source.origin[1];
    sound.origin[2] = source.origin[2];
    if (!tf2::native::scheduleSoundEvent(timeline, std::move(sound), tickRate)) ++refused;
  }
  timeline.sortByTick();
  const auto kindCounts = tf2::native::countSoundKinds(timeline);

  // ---- the product's archive chain, opened once ----------------------------
  std::vector<std::unique_ptr<tf2::native::VpkArchive>> archives;
  for (const char* name : productArchiveNames()) {
    auto archive = std::make_unique<tf2::native::VpkArchive>();
    const std::filesystem::path path = std::filesystem::path(tfRoot) / name;
    if (archive->open(path)) archives.push_back(std::move(archive));
  }

  // Resolve one sound index the way the product would: walk the archives in
  // order, first archive where the resource resolves wins; if none resolves,
  // the first informative reason (a corrupt or unsupported entry beats "this
  // archive does not hold it") is the named outcome. Cached per index -- the
  // mapping and the sink must never disagree about the same event, and a
  // second decode of the same name would be a second chance to drift.
  std::unordered_map<std::uint32_t, ResourceAnswer> answerCache;
  const auto answerFor = [&](std::uint32_t soundIndex) -> const ResourceAnswer& {
    const auto cached = answerCache.find(soundIndex);
    if (cached != answerCache.end()) return cached->second;
    ResourceAnswer best;
    bool haveBest = false;
    for (const auto& archive : archives) {
      tf2::native::SoundResource resource;
      std::string detail;
      const auto status = tf2::native::resolveSoundIndexResource(indexTable, *archive, soundIndex, resource, detail);
      const double seconds = resource.wav.valid ? tf2::native::wavDurationSeconds(resource.wav) : -1.0;
      if (status == tf2::native::SoundResolveStatus::Resolved) {
        best = {status, resource.archivePath, detail, seconds};
        haveBest = true;
        break;
      }
      if (status != tf2::native::SoundResolveStatus::MissingWav && !haveBest) {
        best = {status, {}, detail, seconds};
        haveBest = true;
      }
    }
    if (!haveBest) {
      best.status = tf2::native::SoundResolveStatus::MissingWav;
      best.detail = "no archive in the product chain holds the resource";
    }
    return answerCache.emplace(soundIndex, std::move(best)).first->second;
  };

  // ---- one status per scheduled event, counted ----------------------------
  std::size_t statusCounts[6] = {};  // indexed by SoundResolveStatus
  std::size_t maxResolvedSeconds = 0;
  std::size_t markerEvents = 0, markerEventsResolved = 0, markerEventsMissing = 0, markerEventsOther = 0;
  std::unordered_map<std::uint32_t, std::size_t> distinctIndices;
  for (const auto& event : timeline.events()) {
    const auto& answer = answerFor(event.soundIndex);
    ++statusCounts[static_cast<std::size_t>(answer.status)];
    ++distinctIndices[event.soundIndex];
    // The marked entries: names that carry one or two Source sound characters
    // in front of the file. Counted apart, because "a marked name resolved" is
    // the reading that shows the strip is applied, and "a marked name is still
    // missing" is server-custom content the strip cannot help. `other` keeps
    // the three-way split exact -- a marked name can also end in
    // unsupported_format (an MP3) or invalid_wav, so the gate asserts
    // resolved + missing + other == total instead of pretending only two
    // outcomes exist.
    if (tf2::native::leadingSoundCharacterCount(event.name) > 0) {
      ++markerEvents;
      if (answer.status == tf2::native::SoundResolveStatus::Resolved) ++markerEventsResolved;
      else if (answer.status == tf2::native::SoundResolveStatus::MissingWav) ++markerEventsMissing;
      else ++markerEventsOther;
    }
    if (answer.status == tf2::native::SoundResolveStatus::Resolved && answer.seconds > 0.0) {
      maxResolvedSeconds = std::max(maxResolvedSeconds,
        static_cast<std::size_t>(answer.seconds));
    }
  }

  // ---- the mapping file: one line per scheduled event ---------------------
  std::size_t mapLines = 0;
  if (!mapOutPath.empty()) {
    std::ofstream map(mapOutPath, std::ios::binary);
    if (!map) {
      std::cout << "{\"status\":\"failed\",\"reason\":\"cannot open --map-out file\"}\n";
      return 2;
    }
    map << "# sound content map\n";
    map << "# demo=" << std::filesystem::path(demoPath).filename().string()
        << " origin=" << origin << " originFound=" << (originFound ? 1 : 0)
        << " tickRate=" << formatSeconds(tickRate) << "\n";
    map << "# srctable=soundprecache entries=" << precacheEntries
        << " decodeFailures=" << summary.soundPrecacheDecodeFailures
        << " archives=" << archives.size() << "\n";
    map << "# columns: tick\tindex\tkind\tstatus\tseconds\tsrctable\tpath\tname\tdetail\n";
    for (const auto& event : timeline.events()) {
      const auto& answer = answerFor(event.soundIndex);
      map << event.tick << '\t' << event.soundIndex << '\t'
          << tf2::native::soundEventKindName(event.kind) << '\t'
          << tf2::native::soundResolveStatusName(answer.status) << '\t'
          << formatSeconds(answer.seconds) << '\t' << "soundprecache" << '\t'
          << (answer.path.empty() ? "-" : answer.path) << '\t'
          << event.name << '\t' << answer.detail << '\n';
      ++mapLines;
    }
  }

  // ---- the scheduler over the real timeline, device-free ------------------
  // The resolver here is the product's: read the resource from the archive
  // chain and hand back the parsed WAV. The sink is injected, so nothing opens
  // a device; "played" counts real PCM buffers handed to that sink. Cached by
  // name so a replay window does not re-read the archive, exactly like the
  // product's own sound cache.
  std::unordered_map<std::string, tf2::native::WavPcmData> wavCache;
  std::unordered_map<std::string, bool> wavMissing;
  std::size_t sinkCalls = 0, sinkBytes = 0;
  tf2::native::AudioEventScheduler scheduler;
  scheduler.setDeviceId(0);
  scheduler.setResolver([&](const std::string& name, tf2::native::WavPcmData& out) {
    const auto missing = wavMissing.find(name);
    if (missing != wavMissing.end()) return !missing->second;
    const auto cached = wavCache.find(name);
    if (cached != wavCache.end()) { out = cached->second; return true; }
    for (const auto& archive : archives) {
      tf2::native::SoundResource resource;
      if (tf2::native::readSoundResource(*archive, name, resource)
          && resource.codec == tf2::native::SoundCodec::Wav && resource.wav.valid) {
        wavCache.emplace(name, resource.wav);
        out = resource.wav;
        return true;
      }
    }
    wavMissing.emplace(name, true);
    return false;
  });
  scheduler.setPlaybackSink([&](const tf2::native::WavPcmData& wav, float, float) {
    if (!tf2::native::wavPcmIsPlayable(wav)) return false;
    ++sinkCalls;
    sinkBytes += wav.pcm.size();
    return true;
  });

  const std::int32_t lastTick = timeline.empty() ? 0 : timeline.events().back().tick;
  scheduler.reset(0);
  scheduler.advance(timeline, lastTick);
  const auto fullStats = scheduler.stats();
  const std::size_t seenBeforeDuplicate = fullStats.eventsSeen;
  scheduler.advance(timeline, lastTick);  // the same tick again: must add nothing
  const std::size_t seenAfterDuplicate = scheduler.stats().eventsSeen;
  const std::size_t duplicateTicksAfter = scheduler.stats().duplicateTicks;

  // Backward playback: re-seed the cursor a quarter of the recording back and
  // re-advance. The events seen on the way back must be exactly the events in
  // the replayed window, computed from the timeline alone. A quarter rather
  // than a half because these recordings cluster their sounds early -- measured
  // on the pov demo, the second half holds 8 of 5323 events, which would make
  // the equality below technically true and practically empty.
  const std::int32_t backTo = lastTick / 4;
  scheduler.reset(backTo);
  const std::size_t reverseRecoveriesAfter = scheduler.stats().reverseRecoveries;
  scheduler.advance(timeline, lastTick);
  const std::size_t replaySeen = scheduler.stats().eventsSeen;
  std::size_t replayExpected = 0;
  timeline.forEachRange(backTo + 1, lastTick, [&replayExpected](const tf2::native::SoundPlaybackEvent&) {
    ++replayExpected;
  });

  // ---- the reading ---------------------------------------------------------
  std::cout << "{\"status\":\"" << kStatusOk << "\""
            << ",\"mode\":\"demo\""
            << ",\"demo\":\"" << std::filesystem::path(demoPath).filename().string() << "\""
            << ",\"archivesOpened\":" << archives.size()
            << ",\"soundPrecacheEntries\":" << precacheEntries
            << ",\"soundPrecacheSetFailures\":" << precacheSetFailures
            << ",\"soundPrecacheDecodeFailures\":" << summary.soundPrecacheDecodeFailures
            << ",\"soundPrecacheUpdateCount\":" << summary.soundPrecacheUpdateCount
            << ",\"originFound\":" << (originFound ? "true" : "false")
            << ",\"packetOrigin\":" << origin
            << ",\"tickRate\":" << formatSeconds(tickRate)
            << ",\"read\":" << read
            << ",\"unresolved\":" << unresolved
            << ",\"unplaceable\":" << unplaceable
            << ",\"refused\":" << refused
            << ",\"rejected\":" << (unresolved + unplaceable + refused)
            << ",\"scheduled\":" << timeline.size()
            << ",\"mapLines\":" << mapLines
            << ",\"tableLines\":" << tableLines
            << ",\"tableEntries\":" << tableSorted.size()
            << ",\"tableKindWeapon\":"
            << tableKinds[static_cast<std::size_t>(tf2::native::SoundEventKind::Weapon)]
            << ",\"tableKindFootstep\":"
            << tableKinds[static_cast<std::size_t>(tf2::native::SoundEventKind::Footstep)]
            << ",\"tableKindUber\":"
            << tableKinds[static_cast<std::size_t>(tf2::native::SoundEventKind::Uber)]
            << ",\"tableKindWorld\":"
            << tableKinds[static_cast<std::size_t>(tf2::native::SoundEventKind::World)]
            << ",\"tableKindTempEntity\":"
            << tableKinds[static_cast<std::size_t>(tf2::native::SoundEventKind::TempEntity)]
            << ",\"tableKindPcf\":"
            << tableKinds[static_cast<std::size_t>(tf2::native::SoundEventKind::Pcf)]
            << ",\"tableKindVoice\":"
            << tableKinds[static_cast<std::size_t>(tf2::native::SoundEventKind::Voice)]
            << ",\"tableKindAnnouncer\":"
            << tableKinds[static_cast<std::size_t>(tf2::native::SoundEventKind::Announcer)]
            << ",\"tableKindMusic\":"
            << tableKinds[static_cast<std::size_t>(tf2::native::SoundEventKind::Music)]
            << ",\"tableKindUnknown\":"
            << tableKinds[static_cast<std::size_t>(tf2::native::SoundEventKind::Unknown)]
            << ",\"distinctIndices\":" << distinctIndices.size()
            << ",\"kindWeapon\":" << kindCounts.weapon
            << ",\"kindFootstep\":" << kindCounts.footstep
            << ",\"kindUber\":" << kindCounts.uber
            << ",\"kindWorld\":" << kindCounts.world
            << ",\"kindTempEntity\":" << kindCounts.tempEntity
            << ",\"kindPcf\":" << kindCounts.pcf
            << ",\"kindVoice\":" << kindCounts.voice
            << ",\"kindAnnouncer\":" << kindCounts.announcer
            << ",\"kindMusic\":" << kindCounts.music
            << ",\"filtered\":"
            << (kindCounts.voice + kindCounts.announcer + kindCounts.music)
            << ",\"statusResolved\":"
            << statusCounts[static_cast<std::size_t>(tf2::native::SoundResolveStatus::Resolved)]
            << ",\"statusMissingPrecache\":"
            << statusCounts[static_cast<std::size_t>(tf2::native::SoundResolveStatus::MissingPrecache)]
            << ",\"statusMissingWav\":"
            << statusCounts[static_cast<std::size_t>(tf2::native::SoundResolveStatus::MissingWav)]
            << ",\"statusInvalidWav\":"
            << statusCounts[static_cast<std::size_t>(tf2::native::SoundResolveStatus::InvalidWav)]
            << ",\"statusUnsupportedFormat\":"
            << statusCounts[static_cast<std::size_t>(tf2::native::SoundResolveStatus::UnsupportedFormat)]
            << ",\"statusExcessiveDuration\":"
            << statusCounts[static_cast<std::size_t>(tf2::native::SoundResolveStatus::ExcessiveDuration)]
            << ",\"markerEvents\":" << markerEvents
            << ",\"markerEventsResolved\":" << markerEventsResolved
            << ",\"markerEventsMissing\":" << markerEventsMissing
            << ",\"markerEventsOther\":" << markerEventsOther
            << ",\"seenEvents\":" << fullStats.eventsSeen
            << ",\"playedEvents\":" << fullStats.eventsPlayed
            << ",\"skippedVoiceEvents\":" << fullStats.eventsSkippedVoice
            << ",\"missingResourceEvents\":" << fullStats.eventsMissingResource
            << ",\"sinkCalls\":" << sinkCalls
            << ",\"sinkBytes\":" << sinkBytes
            << ",\"seenBeforeDuplicate\":" << seenBeforeDuplicate
            << ",\"seenAfterDuplicate\":" << seenAfterDuplicate
            << ",\"duplicateTicksAfter\":" << duplicateTicksAfter
            << ",\"reverseRecoveriesAfter\":" << reverseRecoveriesAfter
            << ",\"replaySeen\":" << replaySeen
            << ",\"replayExpected\":" << replayExpected
            << ",\"replayFrom\":" << backTo
            << ",\"replayTo\":" << lastTick
            << ",\"maxResolvedSeconds\":" << maxResolvedSeconds
            << "}\n";
  return 0;
}
