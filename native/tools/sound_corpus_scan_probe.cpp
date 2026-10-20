// sound_corpus_scan_probe.cpp -- one pass over many demos, asking which audio
// event categories a recording actually contains.
//
// WHY THIS EXISTS
//   T3's content map answered "what does one event become" for three pinned
//   recordings. T7 has to answer a different question -- "does any recording in
//   this corpus contain a music event, a temp_entity sound, a pcf sound, a
//   voice-chat payload?" -- and answering it by running the content map (which
//   resolves every event against the VPKs) over the corpus would spend almost
//   all of its time decoding resources that the question does not touch.
//
//   The v1 scan in T3 also could not answer it fully: it counted events by
//   *name token*, so "zero music events" was measured against the classifier
//   rather than against the corpus. This probe reports both sides separately:
//     * `kinds` -- what the product's own timeline build classifies the
//       scheduled events as;
//     * `table` -- what the recording's soundprecache *names* classify as
//       (a music name in the table with no music event is a precached-but-never
//       -sent sound, which is a different fact from "no music exists");
//     * `svc` -- the message-level channels (svc_Sounds, svc_VoiceData,
//       svc_VoiceInit, svc_TempEntities, svc_UserMessage, svc_EntityMessage),
//       because a category can be absent from SVC_Sounds and still be present
//       in the stream as another channel.
//
// WHAT IT DOES NOT DO
//   It never opens a VPK, never resolves a resource, never opens an audio
//   device, and never renders. It reads demo bytes and reports counts.
//
// THE TIMELINE IT COUNTS IS THE PRODUCT'S
//   Events are placed with buildSoundTimeline, the same function the player
//   calls (main.cpp), so `kinds` cannot drift from what the product schedules,
//   and unplaceable/refused events are reported as their own counters instead
//   of silently vanishing from the totals.
//
// Usage:
//   sound_corpus_scan_probe --demo <file>
//   sound_corpus_scan_probe --corpus <dir> [--limit N] [--per-demo]
//   sound_corpus_scan_probe --manifest <file> [--per-demo]
//     --demo      scan exactly one recording and print its record plus the
//                 one-demo summary. This is the mode a gate pins: one output
//                 file per pinned input, so a later mutation can perturb that
//                 file alone instead of a shared blob.
//     --corpus    scan every *.dem directly under <dir>, filename-sorted.
//     --manifest  scan exactly the demos listed (one path per line, '#' comments
//                 and blank lines ignored); the input list a gate can pin.
//     --limit N   stop after N demos (corpus mode); 0 = no limit.
//     --per-demo  print the one-line JSON record for every demo instead of only
//                 the summary (the summary always prints one line).
// Exit: 0 = at least one demo was scanned; 2 = bad arguments; 3 = none scanned.
//
// The last line is always the summary, so a caller can pin it without parsing
// the per-demo records.
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "audio_timeline.h"
#include "demo_header.h"

namespace {

constexpr std::size_t kKindCount = 10;

struct KindTotals {
  std::size_t events[kKindCount] = {};
  std::size_t table[kKindCount] = {};
};

const char* kindKey(std::size_t index) {
  switch (static_cast<tf2::native::SoundEventKind>(index)) {
  case tf2::native::SoundEventKind::Unknown: return "unknown";
  case tf2::native::SoundEventKind::Weapon: return "weapon";
  case tf2::native::SoundEventKind::Footstep: return "footstep";
  case tf2::native::SoundEventKind::Uber: return "uber";
  case tf2::native::SoundEventKind::World: return "world";
  case tf2::native::SoundEventKind::TempEntity: return "temp_entity";
  case tf2::native::SoundEventKind::Pcf: return "pcf";
  case tf2::native::SoundEventKind::Voice: return "voice";
  case tf2::native::SoundEventKind::Announcer: return "announcer";
  case tf2::native::SoundEventKind::Music: return "music";
  default: return "?";
  }
}

std::string jsonEscape(const std::string& value) {
  std::string out;
  out.reserve(value.size());
  for (const char c : value) {
    if (c == '"' || c == '\\') { out += '\\'; out += c; }
    else if (static_cast<unsigned char>(c) < 0x20) { out += '?'; }
    else out += c;
  }
  return out;
}

bool nameIsMp3(const std::string& name) {
  return name.size() >= 4 && name.compare(name.size() - 4, 4, ".mp3") == 0;
}

// A name with no dot after its last slash. The archive resolver tries .wav
// before .mp3 for these, so which codec they become is not a fact the demo
// bytes carry -- counted so the scan's MP3 reading can be read as a lower
// bound instead of as the whole population.
bool nameHasNoExtension(const std::string& name) {
  const auto slash = name.find_last_of('/');
  const auto dot = name.find_last_of('.');
  return dot == std::string::npos || (slash != std::string::npos && dot < slash);
}

struct DemoReading {
  std::string demo;
  std::string map;
  int protocol = 0;
  std::size_t soundMessages = 0;
  std::size_t decodedEvents = 0;
  std::size_t validEvents = 0;
  std::size_t timelineSize = 0;
  tf2::native::SoundTimelineBuildCounts build;
  std::size_t kinds[kKindCount] = {};
  std::size_t table[kKindCount] = {};
  std::size_t tableEntries = 0;
  // T31: which of the scheduled events and precache names are MP3s. The scan
  // reads demo bytes only -- it never opens a VPK -- so "MP3" here means the
  // name carries an explicit `.mp3` extension, which is how this corpus spells
  // the sound. Names without any extension are counted apart rather than
  // guessed at: those are the ones the resolver has to decide by probing the
  // archive, and a scan that assumed them would be inventing the answer.
  std::size_t mp3NamedEvents = 0;
  std::size_t mp3NamedKinds[kKindCount] = {};
  std::size_t tableMp3Named = 0;
  std::size_t tableNoExtension = 0;
  std::size_t voiceInit = 0;
  std::size_t voiceData = 0;
  std::uint64_t voicePayloadBits = 0;
  std::size_t tempEntityMessages = 0;
  std::size_t tempEntityEvents = 0;
  std::uint64_t tempPayloadBits = 0;
  std::size_t tempDecodeFailures = 0;
  std::size_t userMessages = 0;
  std::size_t entityMessages = 0;
  std::size_t unknownMessagePackets = 0;
  bool ok = false;
  std::string reason;
};

bool scanOne(const std::filesystem::path& path, DemoReading& out) {
  out = {};
  out.demo = path.filename().string();
  tf2::native::DemoHeader header;
  if (!tf2::native::parseDemoHeaderFile(path, header)) { out.reason = "demo header parse failed"; return false; }
  out.map = header.mapName;
  out.protocol = header.networkProtocol;
  tf2::native::DemoIndex index;
  tf2::native::indexDemoFile(path, header, index);
  tf2::native::DemoNetworkSummary summary;
  summary.networkProtocol = header.networkProtocol;
  if (!tf2::native::scanKnownDemoMessages(path, index, summary)) { out.reason = "demo scan failed"; return false; }

  bool originFound = false;
  const std::int32_t origin = tf2::native::firstRecordedPacketTick(index, originFound);
  tf2::native::SoundEventTimeline timeline;
  out.build = tf2::native::buildSoundTimeline(summary.decodedSoundEvents, originFound, origin,
    tf2::native::demoTickRate(header), timeline);
  out.timelineSize = timeline.size();
  const auto counts = tf2::native::countSoundKinds(timeline);
  out.kinds[static_cast<std::size_t>(tf2::native::SoundEventKind::Unknown)] = 0;
  out.kinds[static_cast<std::size_t>(tf2::native::SoundEventKind::Weapon)] = counts.weapon;
  out.kinds[static_cast<std::size_t>(tf2::native::SoundEventKind::Footstep)] = counts.footstep;
  out.kinds[static_cast<std::size_t>(tf2::native::SoundEventKind::Uber)] = counts.uber;
  out.kinds[static_cast<std::size_t>(tf2::native::SoundEventKind::World)] = counts.world;
  out.kinds[static_cast<std::size_t>(tf2::native::SoundEventKind::TempEntity)] = counts.tempEntity;
  out.kinds[static_cast<std::size_t>(tf2::native::SoundEventKind::Pcf)] = counts.pcf;
  out.kinds[static_cast<std::size_t>(tf2::native::SoundEventKind::Voice)] = counts.voice;
  out.kinds[static_cast<std::size_t>(tf2::native::SoundEventKind::Announcer)] = counts.announcer;
  out.kinds[static_cast<std::size_t>(tf2::native::SoundEventKind::Music)] = counts.music;

  for (const auto& entry : summary.soundPrecache) {
    ++out.tableEntries;
    ++out.table[static_cast<std::size_t>(tf2::native::classifySoundEvent(entry.second))];
    if (nameIsMp3(entry.second)) {
      ++out.tableMp3Named;
    } else if (nameHasNoExtension(entry.second)) {
      ++out.tableNoExtension;
    }
  }
  for (const auto& event : timeline.events()) {
    if (!nameIsMp3(event.name)) continue;
    ++out.mp3NamedEvents;
    ++out.mp3NamedKinds[static_cast<std::size_t>(event.kind)];
  }
  out.decodedEvents = summary.decodedSoundEvents.size();
  for (const auto& event : summary.decodedSoundEvents) { if (event.valid) ++out.validEvents; }
  out.soundMessages = summary.messageTypeCounts[17];
  out.voiceInit = summary.messageTypeCounts[14];
  out.voiceData = summary.messageTypeCounts[15];
  out.voicePayloadBits = summary.voicePayloadBits;
  out.tempEntityMessages = summary.messageTypeCounts[27];
  out.tempEntityEvents = summary.tempEntityEvents.size();
  out.tempPayloadBits = summary.tempPayloadBits;
  out.tempDecodeFailures = summary.tempEventDecodeFailures;
  out.userMessages = summary.messageTypeCounts[23];
  out.entityMessages = summary.messageTypeCounts[24];
  out.unknownMessagePackets = summary.unknownMessagePackets;
  out.ok = true;
  return true;
}

void printDemo(const DemoReading& r) {
  std::cout << "{\"demo\":\"" << jsonEscape(r.demo) << "\""
            << ",\"map\":\"" << jsonEscape(r.map) << "\""
            << ",\"protocol\":" << r.protocol
            << ",\"status\":\"" << (r.ok ? "ok" : "failed") << "\"";
  if (!r.ok) { std::cout << ",\"reason\":\"" << jsonEscape(r.reason) << "\"}\n"; return; }
  std::cout << ",\"soundMessages\":" << r.soundMessages
            << ",\"decodedEvents\":" << r.decodedEvents
            << ",\"validEvents\":" << r.validEvents
            << ",\"timeline\":" << r.timelineSize
            << ",\"build\":{\"read\":" << r.build.read
            << ",\"unresolved\":" << r.build.unresolved
            << ",\"unplaceable\":" << r.build.unplaceable
            << ",\"refused\":" << r.build.refused << "}"
            << ",\"kinds\":{";
  for (std::size_t i = 0; i < kKindCount; ++i) {
    if (i) std::cout << ',';
    std::cout << "\"" << kindKey(i) << "\":" << r.kinds[i];
  }
  std::cout << "},\"table\":{\"entries\":" << r.tableEntries;
  for (std::size_t i = 0; i < kKindCount; ++i) {
    if (r.table[i] == 0) continue;
    std::cout << ",\"" << kindKey(i) << "\":" << r.table[i];
  }
  std::cout << "}"
            << ",\"mp3\":{\"events\":" << r.mp3NamedEvents
            << ",\"table\":" << r.tableMp3Named
            << ",\"tableNoExtension\":" << r.tableNoExtension
            << ",\"kinds\":{";
  {
    bool first = true;
    for (std::size_t i = 0; i < kKindCount; ++i) {
      if (r.mp3NamedKinds[i] == 0) continue;
      if (!first) std::cout << ',';
      first = false;
      std::cout << '"' << kindKey(i) << '"' << ':' << r.mp3NamedKinds[i];
    }
  }
  std::cout << "}}"
            << ",\"svc\":{\"voiceInit\":" << r.voiceInit
            << ",\"voiceData\":" << r.voiceData
            << ",\"voicePayloadBits\":" << r.voicePayloadBits
            << ",\"tempEntities\":" << r.tempEntityMessages
            << ",\"tempEvents\":" << r.tempEntityEvents
            << ",\"tempPayloadBits\":" << r.tempPayloadBits
            << ",\"tempDecodeFailures\":" << r.tempDecodeFailures
            << ",\"userMessages\":" << r.userMessages
            << ",\"entityMessages\":" << r.entityMessages
            << ",\"unknownMessagePackets\":" << r.unknownMessagePackets << "}}\n";
}

bool readManifest(const std::filesystem::path& file, std::vector<std::filesystem::path>& out) {
  std::ifstream in(file);
  if (!in) return false;
  std::string line;
  while (std::getline(in, line)) {
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
    if (line.empty() || line[0] == '#') continue;
    out.emplace_back(line);
  }
  return true;
}

} // namespace

int main(int argc, char** argv) {
  std::filesystem::path corpus;
  std::filesystem::path manifest;
  std::filesystem::path single;
  std::size_t limit = 0;
  bool perDemo = false;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--corpus" && i + 1 < argc) corpus = argv[++i];
    else if (arg == "--manifest" && i + 1 < argc) manifest = argv[++i];
    else if (arg == "--demo" && i + 1 < argc) single = argv[++i];
    else if (arg == "--limit" && i + 1 < argc) limit = static_cast<std::size_t>(std::strtoull(argv[++i], nullptr, 10));
    else if (arg == "--per-demo") perDemo = true;
    else { std::cerr << "usage: --demo <file> | --corpus <dir> [--limit N] [--per-demo] | --manifest <file> [--per-demo]\n"; return 2; }
  }
  const int sources = (corpus.empty() ? 0 : 1) + (manifest.empty() ? 0 : 1) + (single.empty() ? 0 : 1);
  if (sources != 1) {
    std::cerr << "usage: exactly one of --demo <file>, --corpus <dir> or --manifest <file> is required\n";
    return 2;
  }

  std::vector<std::filesystem::path> demos;
  if (!single.empty()) {
    demos.push_back(single);
  } else if (!manifest.empty()) {
    if (!readManifest(manifest, demos)) { std::cerr << "manifest unreadable: " << manifest.string() << "\n"; return 2; }
  } else {
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(corpus, ec)) {
      if (!entry.is_regular_file(ec)) continue;
      if (entry.path().extension() != ".dem") continue;
      demos.push_back(entry.path());
    }
    if (ec) { std::cerr << "corpus unreadable: " << corpus.string() << "\n"; return 2; }
    std::sort(demos.begin(), demos.end());
    if (limit != 0 && demos.size() > limit) demos.resize(limit);
  }
  // --demo prints its record unconditionally: the caller asked for that one
  // recording, so a silent success would be a file with a summary and no reading.
  if (!single.empty()) perDemo = true;

  KindTotals events, table;
  std::size_t scanned = 0, failed = 0;
  std::size_t withSoundEvents = 0, withMusicEvents = 0, withTempEntityEvents = 0, withPcfEvents = 0;
  std::size_t withMusicNames = 0, withTempEntityNames = 0, withPcfNames = 0;
  std::size_t withVoiceData = 0, withVoiceNames = 0, withAnnouncerNames = 0;
  std::size_t withTempEntityMessages = 0, withUnknownMessagePackets = 0;
  std::size_t withMp3Events = 0, totalMp3NamedEvents = 0, totalTableMp3Named = 0,
    totalTableNoExtension = 0;
  std::size_t mp3Kinds[kKindCount] = {};
  std::size_t totalSoundMessages = 0, totalVoicePayloadBits = 0, totalTempPayloadBits = 0;
  std::size_t totalDecodedEvents = 0, totalTimeline = 0, totalUnresolved = 0, totalUnplaceable = 0, totalRefused = 0;
  for (const auto& path : demos) {
    DemoReading reading;
    scanOne(path, reading);
    ++scanned;
    if (!reading.ok) { ++failed; if (perDemo) printDemo(reading); continue; }
    if (perDemo) printDemo(reading);
    totalSoundMessages += reading.soundMessages;
    totalDecodedEvents += reading.decodedEvents;
    totalTimeline += reading.timelineSize;
    totalUnresolved += reading.build.unresolved;
    totalUnplaceable += reading.build.unplaceable;
    totalRefused += reading.build.refused;
    totalVoicePayloadBits += reading.voicePayloadBits;
    totalTempPayloadBits += reading.tempPayloadBits;
    for (std::size_t i = 0; i < kKindCount; ++i) { events.events[i] += reading.kinds[i]; table.table[i] += reading.table[i]; }
    if (reading.timelineSize > 0) ++withSoundEvents;
    if (reading.kinds[static_cast<std::size_t>(tf2::native::SoundEventKind::Music)] > 0) ++withMusicEvents;
    if (reading.kinds[static_cast<std::size_t>(tf2::native::SoundEventKind::TempEntity)] > 0) ++withTempEntityEvents;
    if (reading.kinds[static_cast<std::size_t>(tf2::native::SoundEventKind::Pcf)] > 0) ++withPcfEvents;
    if (reading.table[static_cast<std::size_t>(tf2::native::SoundEventKind::Music)] > 0) ++withMusicNames;
    if (reading.table[static_cast<std::size_t>(tf2::native::SoundEventKind::TempEntity)] > 0) ++withTempEntityNames;
    if (reading.table[static_cast<std::size_t>(tf2::native::SoundEventKind::Pcf)] > 0) ++withPcfNames;
    if (reading.table[static_cast<std::size_t>(tf2::native::SoundEventKind::Voice)] > 0) ++withVoiceNames;
    if (reading.table[static_cast<std::size_t>(tf2::native::SoundEventKind::Announcer)] > 0) ++withAnnouncerNames;
    totalMp3NamedEvents += reading.mp3NamedEvents;
    totalTableMp3Named += reading.tableMp3Named;
    totalTableNoExtension += reading.tableNoExtension;
    for (std::size_t i = 0; i < kKindCount; ++i) mp3Kinds[i] += reading.mp3NamedKinds[i];
    if (reading.mp3NamedEvents > 0) ++withMp3Events;
    if (reading.voiceData > 0) ++withVoiceData;
    if (reading.tempEntityMessages > 0) ++withTempEntityMessages;
    if (reading.unknownMessagePackets > 0) ++withUnknownMessagePackets;
  }

  std::cout << "{\"status\":\"" << (scanned > 0 && failed < scanned ? "ok" : "failed") << "\""
            << ",\"mode\":\"" << (!single.empty() ? "demo" : (manifest.empty() ? "corpus" : "manifest")) << "\""
            << ",\"source\":\"" << jsonEscape(!single.empty() ? single.string() : (manifest.empty() ? corpus.string() : manifest.string())) << "\""
            << ",\"demos\":" << scanned
            << ",\"demosFailed\":" << failed
            << ",\"demosWithSoundEvents\":" << withSoundEvents
            << ",\"demosWithMusicEvents\":" << withMusicEvents
            << ",\"demosWithTempEntityEvents\":" << withTempEntityEvents
            << ",\"demosWithPcfEvents\":" << withPcfEvents
            << ",\"demosWithMusicNames\":" << withMusicNames
            << ",\"demosWithTempEntityNames\":" << withTempEntityNames
            << ",\"demosWithPcfNames\":" << withPcfNames
            << ",\"demosWithVoiceNames\":" << withVoiceNames
            << ",\"demosWithAnnouncerNames\":" << withAnnouncerNames
            << ",\"demosWithVoiceData\":" << withVoiceData
            << ",\"demosWithTempEntityMessages\":" << withTempEntityMessages
            << ",\"demosWithUnknownMessagePackets\":" << withUnknownMessagePackets
            << ",\"soundMessages\":" << totalSoundMessages
            << ",\"decodedEvents\":" << totalDecodedEvents
            << ",\"timeline\":" << totalTimeline
            << ",\"unresolved\":" << totalUnresolved
            << ",\"unplaceable\":" << totalUnplaceable
            << ",\"refused\":" << totalRefused
            << ",\"voicePayloadBits\":" << totalVoicePayloadBits
            << ",\"tempPayloadBits\":" << totalTempPayloadBits
            << ",\"demosWithMp3Events\":" << withMp3Events
            << ",\"mp3Events\":" << totalMp3NamedEvents
            << ",\"mp3TableNames\":" << totalTableMp3Named
            << ",\"tableNamesNoExtension\":" << totalTableNoExtension
            << ",\"mp3EventKinds\":{";
  {
    bool first = true;
    for (std::size_t i = 0; i < kKindCount; ++i) {
      if (!first) std::cout << ',';
      first = false;
      std::cout << '"' << kindKey(i) << '"' << ':' << mp3Kinds[i];
    }
  }
  std::cout << "},\"eventKinds\":{";
  for (std::size_t i = 0; i < kKindCount; ++i) {
    if (i) std::cout << ',';
    std::cout << "\"" << kindKey(i) << "\":" << events.events[i];
  }
  std::cout << "},\"tableKinds\":{";
  for (std::size_t i = 0; i < kKindCount; ++i) {
    if (i) std::cout << ',';
    std::cout << "\"" << kindKey(i) << "\":" << table.table[i];
  }
  std::cout << "}}\n";
  if (scanned == 0) return 3;
  return failed < scanned ? 0 : 3;
}
