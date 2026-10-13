// sound_timeline_census_probe.cpp -- what the demo's decoded sound events look
// like once they are placed on a playback-relative timeline.
//
// WHY THIS EXISTS
//   The product wires `demoNetworkSummary.decodedSoundEvents` onto
//   `SoundEventTimeline` by subtracting the first packet's tick. A gate that
//   only read `events_seen` from the running player could not tell three very
//   different states apart:
//     * the events really do cluster at relative tick 0 (signon state), so a
//       pile-up at tick 0 is the data, not a defect;
//     * the events are spread over the recording but the subtraction is wrong,
//       so everything lands on 0 -- a batch flush;
//     * the origin was never found, so the subtraction silently does nothing
//       and every event is scheduled at its absolute tick -- a silent no-op
//       whose only symptom is that nothing ever plays.
//   This probe prints the distribution that separates them: where the events
//   actually sit relative to the first packet, how many precede it, and how
//   many would be clamped to zero by the current `max(0, tick - origin)`.
//
// WHAT IT DOES NOT DO
//   It never opens an audio device, never resolves a VPK, and never plays
//   anything. It reads a demo and reports counts.
//
// Usage:
//   sound_timeline_census_probe --self-test
//   sound_timeline_census_probe --demo <path.dem>
// Exit: 0 = the requested reading was produced.

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <limits>
#include <map>
#include <string>
#include <vector>

#include "audio_player.h"
#include "audio_timeline.h"
#include "demo_header.h"

namespace {

constexpr const char* kStatusOk = "ok";
constexpr const char* kStatusFailed = "failed";

// The origin the product used before this fix: the tick of the first command 1
// or 2 entry. Kept here so the census can print both origins side by side -- the
// whole point of the reading is that the two differ by a whole recording.
std::int32_t legacyOriginOf(const tf2::native::DemoIndex& index, bool& found) {
  for (const auto& entry : index.entries) {
    if (entry.command == 1 || entry.command == 2) { found = true; return entry.tick; }
  }
  found = false;
  return 0;
}

int selfTest() {
  int ok = 0, total = 0;
  const auto check = [&](bool value, const char* what) {
    ++total;
    if (value) { ++ok; std::cout << "PASS " << what << "\n"; }
    else { std::cout << "FAIL " << what << "\n"; }
  };
  // The clamp the product used before the fix, kept as the control: it is what
  // turns "before the origin" into "at the origin".
  const auto legacyClamp = [](std::int32_t tick, std::int32_t origin) {
    return std::max<std::int32_t>(0, tick - origin);
  };
  std::int32_t out = -1;

  // The production helper, on the boundary cases a batch flush is made of.
  check(tf2::native::soundEventRelativeTick(29368, 0, out) && out == 29368,
        "an event at the origin keeps tick 0 when the origin is 0");
  check(tf2::native::soundEventRelativeTick(29368, 29368, out) && out == 0,
        "an event at the origin lands on relative tick 0");
  check(!tf2::native::soundEventRelativeTick(29367, 29368, out),
        "an event one tick BEFORE the origin is refused, not clamped");
  check(!tf2::native::soundEventRelativeTick(0, 29368, out),
        "an event a whole recording before the origin is refused, not clamped");
  check(tf2::native::soundEventRelativeTick(39122, 29368, out) && out == 9754,
        "the last tick maps to the recording length");
  // The legacy clamp maps both refused cases onto 0, which is the collapse the
  // fix removes. Pinned as a control so the difference is a reading, not a claim.
  check(legacyClamp(29367, 29368) == 0 && legacyClamp(0, 29368) == 0,
        "control: the legacy clamp maps every pre-origin event onto tick 0");

  // The overflow road to the same collapse. `100 - INT_MIN` wraps in int32; the
  // legacy clamp then reads 0 and the whole recording lands on the first tick.
  // The helper computes wide and refuses instead.
  const std::int32_t intMin = std::numeric_limits<std::int32_t>::min();
  const std::int32_t wrapped = static_cast<std::int32_t>(100 - intMin);
  check(wrapped < 0 && legacyClamp(100, intMin) == 0,
        "control: an INT_MIN origin wraps a positive tick negative, so the clamp reads 0");
  check(!tf2::native::soundEventRelativeTick(100, intMin, out),
        "an INT_MIN origin is refused rather than wrapped onto tick 0");

  std::cout << "{\"status\":\"" << (ok == total ? kStatusOk : kStatusFailed) << "\""
            << ",\"mode\":\"self-test\""
            << ",\"assertions_ok\":" << ok << ",\"assertions_total\":" << total << "}\n";
  return ok == total ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
  std::string demoPath;
  bool wantSelfTest = false;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--self-test") { wantSelfTest = true; }
    else if (arg == "--demo" && i + 1 < argc) { demoPath = argv[++i]; }
    else {
      std::cout << "{\"status\":\"failed\",\"reason\":\"unknown argument: " << arg << "\"}\n";
      return 2;
    }
  }

  if (demoPath.empty()) {
    if (wantSelfTest) return selfTest();
    std::cout << "{\"status\":\"failed\",\"reason\":\"usage: --self-test | --demo <path.dem>\"}\n";
    return 2;
  }

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

  bool originFound = false;
  const std::int32_t origin = legacyOriginOf(index, originFound);
  // The production origin, from the shared definition in demo_header.cpp.
  bool packetOriginFound = false;
  const std::int32_t packetOrigin = tf2::native::firstRecordedPacketTick(index, packetOriginFound);

  // The entry index, because "the events precede the origin" is only meaningful
  // next to the ticks the entries themselves carry. Without this a reader cannot
  // tell a mis-tagged event from a mis-read entry tick.
  std::size_t pktEntries = 0;
  std::int32_t pktMin = 0, pktMax = 0, pktFirst = 0;
  bool pktFirstSet = false;
  std::vector<std::int32_t> pktTicks;
  std::size_t pktBelowOrigin = 0;
  std::size_t commandCounts[9] = {};
  std::string firstEntries;
  std::size_t entrySeen = 0;
  for (const auto& entry : index.entries) {
    if (entry.command < 9) ++commandCounts[entry.command];
    if (entrySeen < 12) {
      if (entrySeen) firstEntries += ',';
      firstEntries += std::to_string(entry.command) + ":" + std::to_string(entry.tick);
      ++entrySeen;
    }
    if (entry.command != 1 && entry.command != 2) continue;
    if (!pktFirstSet) { pktFirst = entry.tick; pktFirstSet = true; pktMin = pktMax = entry.tick; }
    if (pktTicks.size() < 16) pktTicks.push_back(entry.tick);
    if (entry.tick < 29368) ++pktBelowOrigin;
    pktMin = std::min(pktMin, entry.tick);
    pktMax = std::max(pktMax, entry.tick);
    ++pktEntries;
  }
  std::string pktTickList;
  for (std::size_t i = 0; i < pktTicks.size(); ++i) {
    if (i) pktTickList += ',';
    pktTickList += std::to_string(pktTicks[i]);
  }

  const auto& events = summary.decodedSoundEvents;
  std::map<std::int32_t, std::size_t> byTick;
  std::size_t beforeOrigin = 0, atOrigin = 0, resolved = 0, unresolved = 0;
  std::size_t nonDefaultVolume = 0, nonZeroOrigin = 0, validEvents = 0;
  std::int32_t minTick = 0, maxTick = 0;
  for (std::size_t i = 0; i < events.size(); ++i) {
    const auto& event = events[i];
    if (event.valid) ++validEvents;
    if (!event.resourceName.empty()) ++resolved; else ++unresolved;
    if (event.volume != 1.0f) ++nonDefaultVolume;
    if (event.origin[0] != 0.0f || event.origin[1] != 0.0f || event.origin[2] != 0.0f) ++nonZeroOrigin;
    if (i == 0) { minTick = maxTick = event.tick; }
    minTick = std::min(minTick, event.tick);
    maxTick = std::max(maxTick, event.tick);
    ++byTick[event.tick];
    if (event.tick < origin) ++beforeOrigin;
    else if (event.tick == origin) ++atOrigin;
  }

  // The first ten playback-relative ticks: the shape a batch flush shows up in.
  std::size_t firstTicks[10] = {};
  std::size_t clampedTotal = 0;
  for (const auto& [tick, count] : byTick) {
    const std::int32_t relative = std::max<std::int32_t>(0, tick - origin);
    if (relative < 10) firstTicks[relative] += count;
    if (relative == 0) clampedTotal += count;
  }

  // The same census against the packet-derived origin, using the production
  // helper. If this one stops collapsing onto tick 0, the signon tick was the
  // defect; if it does not, the event ticks themselves are the problem and the
  // origin is not the fix. `unplaceablePacketOrigin` is the count the product
  // refuses rather than clamps.
  std::size_t clampedPacket = 0, unplaceablePacket = 0, firstPacketTicks[10] = {};
  for (const auto& [tick, count] : byTick) {
    std::int32_t relative = 0;
    if (!tf2::native::soundEventRelativeTick(tick, packetOrigin, relative)) {
      unplaceablePacket += count;
      continue;
    }
    if (relative < 10) firstPacketTicks[relative] += count;
    if (relative == 0) clampedPacket += count;
  }


  // Everything above describes the raw decoded events. This block runs the same
  // chain main.cpp runs -- relative tick, delay folding, name/kind handling --
  // so the per-tick counts here are the numbers the player's `events_seen` must
  // match. Without it the two readings cannot be compared, and a scheduler that
  // scanned nothing looks the same as a timeline that holds nothing.
  std::size_t delayZero = 0, delayNegative = 0, delayPositive = 0;
  float delayMin = 0.0f, delayMax = 0.0f;
  for (const auto& event : events) {
    if (event.delaySeconds == 0.0f) ++delayZero;
    else if (event.delaySeconds < 0.0f) ++delayNegative;
    else ++delayPositive;
    delayMin = std::min(delayMin, event.delaySeconds);
    delayMax = std::max(delayMax, event.delaySeconds);
  }

  double tickRate = 66.6666667;
  if (header.playbackTime > 0.0f && header.ticks > 0) {
    tickRate = std::clamp(static_cast<double>(header.ticks)
      / static_cast<double>(header.playbackTime), 30.0, 128.0);
  }

  tf2::native::SoundEventTimeline timeline;
  std::size_t timelineUnresolved = 0, timelineUnplaceable = 0, timelineRefused = 0;
  for (const auto& source : events) {
    if (source.resourceName.empty()) { ++timelineUnresolved; continue; }
    std::int32_t relative = 0;
    if (!packetOriginFound
        || !tf2::native::soundEventRelativeTick(source.tick, packetOrigin, relative)) {
      ++timelineUnplaceable;
      continue;
    }
    tf2::native::ScheduledSound sound;
    sound.tick = relative;
    sound.soundIndex = source.soundIndex;
    sound.name = source.resourceName;
    sound.volume = source.volume;
    sound.delaySeconds = source.delaySeconds;
    sound.origin[0] = source.origin[0];
    sound.origin[1] = source.origin[1];
    sound.origin[2] = source.origin[2];
    if (!tf2::native::scheduleSoundEvent(timeline, std::move(sound), tickRate)) ++timelineRefused;
  }
  timeline.sortByTick();

  std::size_t timelineFirstTicks[10] = {};
  std::int32_t timelineMinTick = 0, timelineMaxTick = 0;
  bool timelineFirst = true;
  for (const auto& event : timeline.events()) {
    if (timelineFirst) { timelineMinTick = timelineMaxTick = event.tick; timelineFirst = false; }
    timelineMinTick = std::min(timelineMinTick, event.tick);
    timelineMaxTick = std::max(timelineMaxTick, event.tick);
    if (event.tick < 10) timelineFirstTicks[event.tick] += 1;
  }
  // Cumulative counts at the ticks a short capture run reaches, so a product
  // reading of `events_seen` has something to be compared against.
  const std::int32_t cumulativeTicks[] = {0, 6, 10, 60, 120, 300, 600, 1200, 3000};
  std::size_t cumulative[sizeof(cumulativeTicks) / sizeof(cumulativeTicks[0])] = {};
  for (const auto& event : timeline.events()) {
    for (std::size_t i = 0; i < sizeof(cumulativeTicks) / sizeof(cumulativeTicks[0]); ++i) {
      if (event.tick <= cumulativeTicks[i]) ++cumulative[i];
    }
  }

  // ---- drive the production scheduler over the timeline it just built -------
  // The player reports `events_seen`; this block reports what that number has to
  // be. Two instruments agreeing is what turns "the cursor advanced and lost
  // nothing" into a reading instead of a hope. The resolver is synthetic (no VPK,
  // no device) so every scheduled event reaches the outcome sink and is counted.
  tf2::native::AudioEventScheduler scheduler;
  scheduler.setResolver([](const std::string&, tf2::native::WavPcmData& wav) {
    wav = {};
    wav.encoding = tf2::native::WavEncoding::Pcm;
    wav.channels = 1;
    wav.sampleRate = 8000;
    wav.bitsPerSample = 16;
    wav.blockAlign = 2;
    wav.byteRate = 16000;
    wav.pcm = {0, 0, 0, 0};
    wav.valid = true;
    return true;
  });
  scheduler.setOutcomeSink([](const tf2::native::WavPcmData&, float, float) {
    return tf2::native::AudioEventScheduler::PlaybackOutcome::skipped;
  });
  const std::size_t sampleCount = sizeof(cumulativeTicks) / sizeof(cumulativeTicks[0]);
  scheduler.reset(0);
  std::size_t forwardSeen[sizeof(cumulativeTicks) / sizeof(cumulativeTicks[0])] = {};
  for (std::size_t i = 0; i < sampleCount; ++i) {
    // Ticks must strictly increase: a repeated tick is refused by design, and
    // that refusal is exactly what `duplicateTicks` counts.
    scheduler.advance(timeline, cumulativeTicks[i]);
    forwardSeen[i] = scheduler.stats().eventsSeen;
  }
  // The duplicate guard, exercised on purpose: re-presenting a consumed tick must
  // not add a single event.
  const std::size_t seenBeforeDuplicate = scheduler.stats().eventsSeen;
  scheduler.advance(timeline, cumulativeTicks[sampleCount - 1]);
  const std::size_t seenAfterDuplicate = scheduler.stats().eventsSeen;
  const std::size_t duplicateTicksAfter = scheduler.stats().duplicateTicks;

  // Reverse timing: re-seed the cursor behind where it has already been and
  // re-advance. The events seen on the way back must be exactly the events in
  // the replayed range, so a reverse run replays the right window and nothing
  // outside it. The window is chosen where the timeline is dense -- a window
  // that happens to hold no events would make the equality trivially true and
  // prove nothing.
  const std::int32_t backTo = cumulativeTicks[6];
  const std::int32_t forwardTo = cumulativeTicks[8];
  scheduler.reset(backTo);
  const std::size_t reverseRecoveriesAfter = scheduler.stats().reverseRecoveries;
  scheduler.advance(timeline, forwardTo);
  const std::size_t replaySeen = scheduler.stats().eventsSeen;
  // The range the reverse pass must have replayed, from the census alone.
  const std::size_t replayExpected = cumulative[8] - cumulative[6];

  std::cout << "{\"status\":\"" << kStatusOk << "\""
            << ",\"mode\":\"demo\""
            << ",\"demo\":\"" << std::filesystem::path(demoPath).filename().string() << "\""
            << ",\"entries\":" << index.entries.size()
            << ",\"packetEntries\":" << pktEntries
            << ",\"packetTickFirst\":" << pktFirst
            << ",\"packetTickMin\":" << pktMin
            << ",\"packetTickMax\":" << pktMax
            << ",\"packetBelow29368\":" << pktBelowOrigin
            << ",\"packetTicks\":\"" << pktTickList << "\""
            << ",\"commandCounts\":[";
  for (int i = 0; i < 9; ++i) {
    if (i) std::cout << ',';
    std::cout << commandCounts[i];
  }
  std::cout << "]"
            << ",\"firstEntries\":\"" << firstEntries << "\""
            << ",\"originFound\":" << (originFound ? "true" : "false")
            << ",\"firstPacketTick\":" << origin
            << ",\"soundEvents\":" << events.size()
            << ",\"validEvents\":" << validEvents
            << ",\"resolved\":" << resolved
            << ",\"unresolved\":" << unresolved
            << ",\"decodeFailures\":" << summary.decodedSoundEventFailures
            << ",\"minTick\":" << minTick
            << ",\"maxTick\":" << maxTick
            << ",\"distinctTicks\":" << byTick.size()
            << ",\"beforeOrigin\":" << beforeOrigin
            << ",\"atOrigin\":" << atOrigin
            << ",\"clampedToZero\":" << clampedTotal
            << ",\"nonDefaultVolume\":" << nonDefaultVolume
            << ",\"nonZeroOrigin\":" << nonZeroOrigin
            << ",\"packetOriginFound\":" << (packetOriginFound ? "true" : "false")
            << ",\"packetOrigin\":" << packetOrigin
            << ",\"clampedToZeroPacketOrigin\":" << clampedPacket
            << ",\"unplaceablePacketOrigin\":" << unplaceablePacket
            << ",\"firstTicksPacketOrigin\":[";
  for (int i = 0; i < 10; ++i) {
    if (i) std::cout << ',';
    std::cout << firstPacketTicks[i];
  }
  std::cout << "]"
            << ",\"firstTicks\":[";
  for (int i = 0; i < 10; ++i) {
    if (i) std::cout << ',';
    std::cout << firstTicks[i];
  }
  std::cout << "]"
            << ",\"delayZero\":" << delayZero
            << ",\"delayNegative\":" << delayNegative
            << ",\"delayPositive\":" << delayPositive
            << ",\"delayMin\":" << delayMin
            << ",\"delayMax\":" << delayMax
            << ",\"tickRate\":" << tickRate
            << ",\"timelineSize\":" << timeline.size()
            << ",\"timelineUnresolved\":" << timelineUnresolved
            << ",\"timelineUnplaceable\":" << timelineUnplaceable
            << ",\"timelineRefused\":" << timelineRefused
            << ",\"timelineMinTick\":" << timelineMinTick
            << ",\"timelineMaxTick\":" << timelineMaxTick
            << ",\"timelineFirstTicks\":[";
  for (int i = 0; i < 10; ++i) {
    if (i) std::cout << ',';
    std::cout << timelineFirstTicks[i];
  }
  std::cout << "]"
            << ",\"cumulativeTicks\":[";
  for (std::size_t i = 0; i < sizeof(cumulativeTicks) / sizeof(cumulativeTicks[0]); ++i) {
    if (i) std::cout << ',';
    std::cout << cumulativeTicks[i];
  }
  std::cout << "]"
            << ",\"cumulative\":[";
  for (std::size_t i = 0; i < sizeof(cumulative) / sizeof(cumulative[0]); ++i) {
    if (i) std::cout << ',';
    std::cout << cumulative[i];
  }
  std::cout << "]"
            << ",\"forwardSeen\":[";
  for (std::size_t i = 0; i < sampleCount; ++i) {
    if (i) std::cout << ',';
    std::cout << forwardSeen[i];
  }
  std::cout << "]"
            << ",\"seenBeforeDuplicate\":" << seenBeforeDuplicate
            << ",\"seenAfterDuplicate\":" << seenAfterDuplicate
            << ",\"duplicateTicksAfter\":" << duplicateTicksAfter
            << ",\"reverseRecoveriesAfter\":" << reverseRecoveriesAfter
            << ",\"replaySeen\":" << replaySeen
            << ",\"replayExpected\":" << replayExpected
            << ",\"replayFrom\":" << backTo
            << ",\"replayTo\":" << forwardTo
            << "}\n";
  return 0;
}
