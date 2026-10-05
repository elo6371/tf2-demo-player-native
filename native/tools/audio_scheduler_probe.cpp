#include <iostream>
#include <cstdlib>
#include <vector>
#include "audio_player.h"
using namespace tf2::native;
static SoundPlaybackEvent eventAt(std::int32_t tick, std::uint32_t index) { SoundPlaybackEvent event; event.tick=tick; event.soundIndex=index; event.name="weapons/probe.wav"; event.kind=SoundEventKind::Weapon; return event; }
int main() {
  const char* tfRoot = std::getenv("TF_ROOT");
  const std::string vpkPath = std::string(tfRoot ? tfRoot : "D:/SteamLibrary/steamapps/common/Team Fortress 2/tf") + "/tf2_sound_misc_dir.vpk";
  VpkArchive archive;
  if (!archive.open(vpkPath)) return 9;
  SoundResource resource;
  if (!readSoundResource(archive, "weapons/stickybomblauncher_det.wav", resource)
      || !resource.valid || resource.codec != SoundCodec::Wav || !resource.wav.valid
      || resource.wav.pcm.empty()) return 10;
  WavPcmData wav;
  if (parseWav({}, wav) || wav.error.empty()) return 6;
  const std::vector<std::uint8_t> truncated = {'R','I','F','F', 0,0,0,0, 'W','A','V','E'};
  if (parseWav(truncated, wav) || wav.error.empty()) return 7;
  const std::vector<std::uint8_t> badChunk = {'R','I','F','F', 20,0,0,0, 'W','A','V','E', 'd','a','t','a', 8,0,0,0, 0,0,0,0};
  if (parseWav(badChunk, wav) || wav.error.find("exceeds") == std::string::npos) return 8;
  SoundEventTimeline timeline; timeline.add(eventAt(2,1)); timeline.add(eventAt(5,2)); timeline.add(eventAt(9,3)); timeline.add(eventAt(9,4)); timeline.sortByTick();
  AudioEventScheduler scheduler; scheduler.setDeviceId(0); scheduler.setResolver([](const std::string&, WavPcmData&) { return false; });
  scheduler.reset(0); const auto jumped=scheduler.advance(timeline,5); if(jumped.eventsSeen!=2||jumped.eventsMissingResource!=2||jumped.eventsPlayed!=0)return 1;
  scheduler.stop(); const auto paused=scheduler.stats();
  const auto cancelled=scheduler.advance(timeline, 9);
  if(paused.eventsSeen!=2||paused.eventsMissingResource!=2||cancelled.eventsSeen!=paused.eventsSeen||cancelled.eventsMissingResource!=paused.eventsMissingResource)return 2;
  scheduler.reset(5); const auto resumed=scheduler.advance(timeline,9); if(resumed.eventsSeen!=2||resumed.eventsMissingResource!=2||resumed.eventsPlayed!=0)return 3;
  const auto repeated=scheduler.advance(timeline,9); if(repeated.eventsSeen!=resumed.eventsSeen||repeated.eventsMissingResource!=resumed.eventsMissingResource)return 12;
  scheduler.reset(9); const auto reverseBoundary=scheduler.advance(timeline,9); if(reverseBoundary.eventsSeen!=0)return 4;
  scheduler.reset(5); const auto recovered=scheduler.advance(timeline,9); if(recovered.eventsSeen!=2||recovered.eventsMissingResource!=2)return 5;
  std::size_t queuedBytes = 0, sinkCalls = 0;
  AudioEventScheduler injected; injected.setDeviceId(0);
  injected.setResolver([&](const std::string& name, WavPcmData& out) {
    if (name != "weapons/stickybomblauncher_det.wav") return false;
    out = resource.wav; return true;
  });
  injected.setPlaybackSink([&](const WavPcmData& pcm, float, float) {
    if (pcm.encoding != WavEncoding::Pcm || pcm.channels != 2 || pcm.sampleRate != 44100
        || pcm.bitsPerSample != 16 || pcm.blockAlign != 4 || pcm.pcm.size() % pcm.blockAlign != 0) return false;
    ++sinkCalls; queuedBytes += pcm.pcm.size(); return true;
  });
  SoundEventTimeline realTimeline;
  auto realEvent = eventAt(2, 1); realEvent.name = "weapons/stickybomblauncher_det.wav";
  realTimeline.clear(); realTimeline.add(realEvent); realTimeline.sortByTick();
  injected.reset(0); const auto queued = injected.advance(realTimeline, 2);
  if (queued.eventsPlayed != 1 || sinkCalls != 1 || queuedBytes != resource.wav.pcm.size()) return 11;
  constexpr std::size_t sinkQueueLimit = 8;
  std::size_t accepted = 0, rejected = 0;
  AudioEventScheduler bounded; bounded.setDeviceId(0);
  bounded.setResolver([&](const std::string&, WavPcmData& out) { out = resource.wav; return true; });
  bounded.setPlaybackSink([&](const WavPcmData&, float, float) {
    if (accepted == sinkQueueLimit) { ++rejected; return false; }
    ++accepted; return true;
  });
  SoundEventTimeline burst;
  for (std::uint32_t i = 0; i < sinkQueueLimit + 1; ++i) burst.add(eventAt(1, i + 1));
  burst.sortByTick(); bounded.reset(0);
  const auto boundedStats = bounded.advance(burst, 1);
  if (boundedStats.eventsSeen != sinkQueueLimit + 1 || boundedStats.eventsPlayed != sinkQueueLimit
      || boundedStats.eventsMissingResource != 1 || accepted != sinkQueueLimit || rejected != 1) return 13;
  std::cout<<"device=0 playbackCalls=0 sinkCalls=1 queuedPcm=1 sinkQueueLimit=8 accepted=8 rejected=1 vpkWav=1 jumpSeen=2 pausedSeen=2 resumeSeen=2 repeatedSeen=2 reverseSeen=0 recoveredSeen=2 missingPlayed=0 wavMalformed=3\n"; return 0;
}
