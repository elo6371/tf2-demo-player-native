#include "audio_player.h"
#include "effects_timeline.h"
#include "pcf_operators.h"
#include "playback_hud.h"
#include "vpk_archive.h"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace {

void appendBytes(std::vector<std::uint8_t>& out, const void* data, std::size_t size) {
  const auto* bytes = static_cast<const std::uint8_t*>(data);
  out.insert(out.end(), bytes, bytes + size);
}

void appendCString(std::vector<std::uint8_t>& out, const std::string& text) {
  appendBytes(out, text.data(), text.size());
  out.push_back(0);
}

void appendI16(std::vector<std::uint8_t>& out, std::int16_t value) {
  out.push_back(static_cast<std::uint8_t>(value));
  out.push_back(static_cast<std::uint8_t>(value >> 8));
}

void appendI32(std::vector<std::uint8_t>& out, std::int32_t value) {
  const auto raw = static_cast<std::uint32_t>(value);
  out.push_back(static_cast<std::uint8_t>(raw));
  out.push_back(static_cast<std::uint8_t>(raw >> 8));
  out.push_back(static_cast<std::uint8_t>(raw >> 16));
  out.push_back(static_cast<std::uint8_t>(raw >> 24));
}

std::vector<std::uint8_t> syntheticPcf() {
  std::vector<std::uint8_t> out;
  appendCString(out, "<!-- dmx encoding binary 2 format pcf 1 -->\n");
  appendI16(out, 5);
  appendCString(out, "DmeParticleSystemDefinition");
  appendCString(out, "DmeParticleOperator");
  appendCString(out, "operators");
  appendCString(out, "functionName");
  appendCString(out, "radius");
  appendI32(out, 3);
  appendI16(out, 0);
  appendCString(out, "rocket_trail");
  out.insert(out.end(), 16, 0);
  appendI16(out, 1);
  appendCString(out, "move");
  out.insert(out.end(), 16, 0);
  appendI16(out, 1);
  appendCString(out, "fade");
  out.insert(out.end(), 16, 0);
  appendI32(out, 1);
  appendI16(out, 2);
  out.push_back(15);
  appendI32(out, 2);
  appendI32(out, 1);
  appendI32(out, 2);
  appendI32(out, 2);
  appendI16(out, 4);
  out.push_back(3);
  appendI32(out, 0x3f800000);
  appendI16(out, 3);
  out.push_back(5);
  appendCString(out, "movement_basic");
  appendI32(out, 1);
  appendI16(out, 3);
  out.push_back(5);
  appendCString(out, "alpha_fade");
  return out;
}

tf2::native::WavPcmData tinyWav() {
  tf2::native::WavPcmData wav;
  wav.valid = true;
  wav.encoding = tf2::native::WavEncoding::Pcm;
  wav.channels = 1;
  wav.sampleRate = 11025;
  wav.bitsPerSample = 8;
  wav.blockAlign = 1;
  wav.pcm = {128, 128, 128, 128};
  return wav;
}

tf2::native::ScheduledSound soundAt(std::int32_t tick, const char* name, float delay) {
  tf2::native::ScheduledSound sound;
  sound.tick = tick;
  sound.soundIndex = 1;
  sound.name = name;
  sound.delaySeconds = delay;
  sound.volume = 1.0f;
  return sound;
}

} // namespace

int main() {
  using namespace tf2::native;
  const double tickRate = 10.0;
  const bool delayMath = applySoundDelayTicks(10, 0.5f, tickRate) == 15
    && applySoundDelayTicks(1, -1.0f, tickRate) == 0
    && applySoundDelayTicks(4, 0.0f, tickRate) == 4
    && applySoundDelayTicks(3, 0.0f, 0.0) == -1;

  SoundEventTimeline timeline;
  const bool weapon = scheduleGameplaySound(timeline, soundAt(10, "weapons/rocket_shoot.wav", 0.5f), tickRate);
  const bool footstep = scheduleGameplaySound(timeline, soundAt(4, "player/footstep1.wav", 0.0f), tickRate);
  const bool uber = scheduleGameplaySound(timeline, soundAt(20, "medic/uber_charge.wav", 0.0f), tickRate);
  const bool clamped = scheduleGameplaySound(timeline, soundAt(1, "weapons/shotgun_world.wav", -1.0f), tickRate);
  const bool voiceRejected = !scheduleGameplaySound(timeline, soundAt(2, "vo/scout/yes.wav", 0.0f), tickRate);
  const bool announcerRejected = !scheduleGameplaySound(timeline, soundAt(2, "announcer/you_win.wav", 0.0f), tickRate);
  const bool musicRejected = !scheduleGameplaySound(timeline, soundAt(2, "music/class_menu.wav", 0.0f), tickRate);
  timeline.sortByTick();
  const auto counts = countSoundKinds(timeline);
  const bool kinds = weapon && footstep && uber && clamped && voiceRejected && announcerRejected && musicRejected
    && timeline.size() == 4 && counts.weapon == 2 && counts.footstep == 1 && counts.uber == 1
    && timeline.events()[0].tick == 0 && timeline.events()[1].tick == 4
    && timeline.events()[2].tick == 15 && timeline.events()[2].kind == SoundEventKind::Weapon
    && timeline.events()[3].kind == SoundEventKind::Uber;

  const float listener[3] = {0.0f, 0.0f, 0.0f};
  const float ahead[3] = {0.0f, 0.0f, 0.0f};
  const float right[3] = {0.0f, 10.0f, 0.0f};
  const float distant[3] = {10000.0f, 0.0f, 0.0f};
  const float bad[3] = {NAN, 0.0f, 0.0f};
  const SpatialMix centered = computeSpatialMix(ahead, listener);
  const SpatialMix panned = computeSpatialMix(right, listener);
  const SpatialMix silent = computeSpatialMix(distant, listener);
  const SpatialMix invalid = computeSpatialMix(bad, listener);
  const bool spatial = centered.gain == 1.0f && centered.pan == 0.0f
    && panned.pan > 0.9f && panned.gain > 0.0f && panned.gain < 1.0f
    && silent.isSilent() && invalid.isSilent();

  std::size_t sinkCalls = 0;
  AudioEventScheduler scheduler;
  scheduler.setDeviceId(0);
  const WavPcmData wav = tinyWav();
  scheduler.setResolver([&](const std::string&, WavPcmData& out) { out = wav; return true; });
  scheduler.setPlaybackSink([&](const WavPcmData& pcm, float, float) {
    if (!pcm.valid || pcm.pcm.empty()) return false;
    ++sinkCalls;
    return true;
  });
  SoundEventTimeline delayed;
  scheduleGameplaySound(delayed, soundAt(10, "weapons/rocket_shoot.wav", 0.5f), tickRate);
  delayed.sortByTick();
  scheduler.reset(0);
  const auto before = scheduler.advance(delayed, 14);
  const auto played = scheduler.advance(delayed, 15);
  scheduler.stop();
  const auto paused = scheduler.advance(delayed, 20);
  scheduler.reset(15);
  const auto reversed = scheduler.advance(delayed, 15);
  const bool deviceUntouched = before.eventsSeen == 0 && played.eventsPlayed == 1 && sinkCalls == 1
    && paused.eventsSeen == played.eventsSeen && reversed.eventsSeen == 0;

  AudioEventScheduler missing;
  missing.setDeviceId(0);
  missing.setResolver([](const std::string&, WavPcmData&) { return false; });
  missing.setPlaybackSink([&](const WavPcmData&, float, float) { ++sinkCalls; return true; });
  SoundEventTimeline missingTimeline;
  scheduleGameplaySound(missingTimeline, soundAt(1, "weapons/missing.wav", 0.0f), tickRate);
  missingTimeline.sortByTick();
  missing.reset(0);
  const auto missed = missing.advance(missingTimeline, 1);
  const bool missingKept = missed.eventsSeen == 1 && missed.eventsPlayed == 0
    && missed.eventsMissingResource == 1 && sinkCalls == 1;

  std::vector<EntityState> states(4);
  EntityPropertyValue health;
  health.type = SendPropType::Int;
  health.intValue = 125;
  states[2].properties["m_iHealth"] = health;
  EntityPropertyValue team;
  team.type = SendPropType::Int;
  team.intValue = 2;
  states[2].properties["m_iTeamNum"] = team;
  EntityPropertyValue playerClass;
  playerClass.type = SendPropType::Int;
  playerClass.intValue = 3;
  states[2].properties["m_iClass"] = playerClass;
  EntityPropertyValue dottedClass;
  dottedClass.type = SendPropType::Int;
  dottedClass.intValue = 9;
  states[2].properties["m_PlayerClass.m_iClass"] = dottedClass;
  EntityPropertyValue clip;
  clip.type = SendPropType::Int;
  clip.intValue = 6;
  states[2].properties["m_iClip1"] = clip;
  EntityPropertyValue charge;
  charge.type = SendPropType::Float;
  charge.x = 0.25f;
  states[2].properties["m_flChargeLevel"] = charge;
  const PlaybackHudState viewed = readPlaybackHud(states, 2);
  const PlaybackHudState absent = readPlaybackHud(states, -1);
  const bool hud = viewed.healthKnown && viewed.health == 125 && viewed.teamKnown && viewed.team == 2
    && viewed.classKnown && viewed.playerClass == 3 && viewed.clipKnown && viewed.clip == 6
    && viewed.uberKnown && viewed.uberCharge == 0.25f
    && !absent.healthKnown && !absent.teamKnown && !absent.uberKnown;

  TempEntityEvent particle;
  particle.className = "CTETFParticleEffect";
  particle.tick = 30;
  particle.hasOrigin = true;
  particle.origin[0] = 1.0f;
  particle.origin[1] = 2.0f;
  particle.origin[2] = 3.0f;
  particle.hasParticleName = true;
  particle.particleName = "rockettrail";
  TempEffectTimeline effects;
  const bool particleAccepted = addParticleEvent(particle, effects);
  TempEntityEvent wrong = particle;
  wrong.className = "CTEFireBullets";
  const bool particleRejected = !addParticleEvent(wrong, effects);
  effects.sortByTick();
  const bool particleTiming = particleAccepted && particleRejected && effects.countThrough(29) == 0
    && effects.countThrough(30) == 1 && effects.events()[0].particleName == "rockettrail";

  const auto synthetic = syntheticPcf();
  PcfOperatorSummary parsed;
  const bool parsedOk = readPcfOperators(synthetic.data(), synthetic.size(), parsed);
  const bool syntheticPcfOk = parsedOk && parsed.valid && parsed.systemCount == 1 && parsed.operatorCount == 2
    && parsed.commonOperatorCount == 2 && parsed.operators.size() == 2
    && parsed.operators[0].systemName == "rocket_trail"
    && parsed.operators[0].functionName == "movement_basic"
    && parsed.operators[1].functionName == "alpha_fade";
  PcfOperatorSummary emptyParsed;
  const bool emptyRejected = !readPcfOperators(nullptr, 0, emptyParsed) && !emptyParsed.error.empty();
  const std::vector<std::uint8_t> unsupported = {'<', '!', '-', '-', ' ', 'd', 'm', 'x', ' ',
    'e', 'n', 'c', 'o', 'd', 'i', 'n', 'g', ' ', 'b', 'i', 'n', 'a', 'r', 'y', ' ', '5', ' ',
    'f', 'o', 'r', 'm', 'a', 't', ' ', 'p', 'c', 'f', ' ', '2', ' ', '-', '-', '>', '\n', 0};
  PcfOperatorSummary unsupportedParsed;
  const bool unsupportedRejected = !readPcfOperators(unsupported.data(), unsupported.size(), unsupportedParsed)
    && unsupportedParsed.error.find("unsupported") != std::string::npos;
  const std::vector<std::uint8_t> truncated = {'<', '!', '-', '-', 0};
  PcfOperatorSummary truncatedParsed;
  const bool truncatedRejected = !readPcfOperators(truncated.data(), truncated.size(), truncatedParsed)
    && !truncatedParsed.error.empty() && !truncatedParsed.valid;

  bool realOk = true;
  std::string realStatus = "skipped";
  std::size_t realOperators = 0;
  std::size_t realCommon = 0;
  std::string realFunction;
  const char* tfRoot = std::getenv("TF_ROOT");
  const std::string root = tfRoot ? tfRoot : "D:/Steam/steamapps/common/Team Fortress 2/tf";
  VpkArchive archive;
  std::string openError;
  if (archive.open(std::filesystem::path(root) / "tf2_misc_dir.vpk", &openError)) {
    std::string readError;
    const auto bytes = archive.read("particles/bigboom.pcf", &readError);
    PcfOperatorSummary realParsed;
    if (bytes.empty() || !readPcfOperators(bytes.data(), bytes.size(), realParsed) || !realParsed.valid
        || realParsed.elementCount == 0 || realParsed.operatorCount == 0 || realParsed.commonOperatorCount == 0) {
      realOk = false;
      realStatus = realParsed.error.empty() ? (readError.empty() ? "parse-failed" : readError) : realParsed.error;
    } else {
      realStatus = "parsed";
      realOperators = realParsed.operatorCount;
      realCommon = realParsed.commonOperatorCount;
      if (!realParsed.operators.empty()) realFunction = realParsed.operators.front().functionName;
    }
  }

  const bool pass = delayMath && kinds && spatial && deviceUntouched && missingKept && hud
    && particleTiming && syntheticPcfOk && emptyRejected && unsupportedRejected && truncatedRejected && realOk;
  std::cout << "{\"selfTest\":" << (pass ? "true" : "false")
            << ",\"delay\":" << (delayMath ? "true" : "false")
            << ",\"kinds\":" << (kinds ? "true" : "false")
            << ",\"voiceFiltered\":" << ((voiceRejected && announcerRejected && musicRejected) ? "true" : "false")
            << ",\"spatial\":" << (spatial ? "true" : "false")
            << ",\"schedulerSink\":" << (deviceUntouched ? "true" : "false")
            << ",\"missingKept\":" << (missingKept ? "true" : "false")
            << ",\"hud\":" << (hud ? "true" : "false")
            << ",\"particle\":" << (particleTiming ? "true" : "false")
            << ",\"pcf\":" << (syntheticPcfOk ? "true" : "false")
            << ",\"pcfCorrupt\":" << ((emptyRejected && unsupportedRejected && truncatedRejected) ? "true" : "false")
            << ",\"realPcf\":\"" << realStatus << "\""
            << ",\"realOperators\":" << realOperators
            << ",\"realCommon\":" << realCommon
            << ",\"realFunction\":\"" << realFunction << "\""
            << ",\"device\":\"sink\"}\n";
  return pass ? 0 : 1;
}
