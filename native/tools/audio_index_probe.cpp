#include <iostream>
#include <string>

#include "audio_player.h"

using namespace tf2::native;

int main() {
  SoundIndexTable index;
  if (!index.set(7, "weapons\\stickybomblauncher_det.wav")
      || !index.set(8, "sound/weapons/rocket1")
      || index.set(0, "weapons/invalid.wav") || index.size() != 2) return 1;
  const auto* sticky = index.find(7);
  const auto* rocket = index.find(8);
  if (!sticky || !rocket || sticky->name != "weapons\\stickybomblauncher_det.wav"
      || rocket->name != "sound/weapons/rocket1") return 2;
  if (normalizeSoundPath(sticky->name) != "sound/weapons/stickybomblauncher_det.wav"
      || normalizeSoundPath(rocket->name) != "sound/weapons/rocket1.wav") return 3;

  std::size_t resolverCalls = 0;
  std::size_t sinkCalls = 0;
  AudioEventScheduler scheduler;
  scheduler.setDeviceId(WAVE_MAPPER);
  scheduler.setResolver([&](const std::string& name, WavPcmData& wav) {
    ++resolverCalls;
    if (name != sticky->name) return false;
    wav = {};
    wav.encoding = WavEncoding::Pcm;
    wav.channels = 1;
    wav.sampleRate = 8000;
    wav.bitsPerSample = 16;
    wav.blockAlign = 2;
    wav.byteRate = 16000;
    wav.pcm = {0, 0, 0, 0};
    wav.valid = true;
    return true;
  });
  scheduler.setPlaybackSink([&](const WavPcmData&, float, float) {
    ++sinkCalls;
    return true;
  });

  SoundEventTimeline timeline;
  SoundPlaybackEvent audible;
  audible.tick = 1;
  audible.soundIndex = sticky->index;
  audible.name = sticky->name;
  audible.kind = sticky->kind;
  audible.origin[0] = 0.0f;
  audible.origin[1] = 0.0f;
  audible.origin[2] = 0.0f;
  if (!timeline.add(audible)) return 4;
  SoundPlaybackEvent silent = audible;
  silent.tick = 2;
  silent.soundIndex = rocket->index;
  silent.name = rocket->name;
  silent.origin[0] = 4096.0f;
  if (!timeline.add(silent)) return 5;
  timeline.sortByTick();

  scheduler.reset(0);
  const auto stats = scheduler.advance(timeline, 2);
  if (stats.eventsSeen != 2 || stats.eventsPlayed != 1 || stats.eventsMissingResource != 1
      || resolverCalls != 2 || sinkCalls != 1) return 6;
  std::cout << "index_entries=2 normalized_paths=2 resolver_calls=2 sink_calls=1"
               " played=1 silent_skipped=1 device_opened=0 status=pass\n";
  return 0;
}
