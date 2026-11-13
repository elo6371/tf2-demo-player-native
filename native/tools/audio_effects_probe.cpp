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
  // play() rebuilds the WAVEFORMATEX from these fields and refuses a buffer
  // whose byteRate disagrees with sampleRate*blockAlign. This fixture was
  // never handed to a device before -- the default mode goes through the
  // outcome sink -- so the field was never needed, and every play() failed at
  // the format gate before the seam was reached. 11025 Hz * 1 ch * 1 byte.
  wav.byteRate = 11025;
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

// ---------------------------------------------------------------------------
// Player fault self-test (--player-fault-selftest).
//
// The cleanup state machine this probe drives cannot be exercised against a
// real device: WinMM will not answer "reset fails, unprepare works, close
// works" on request, a device test is not repeatable, and a test that reaches a
// real device can be heard. audio_player.h exposes the seven entry points
// through a table of function pointers for exactly this reason; the backend
// below answers them from a script, so every failure shape is stated once and
// then asserted. The production table is installed again before the probe
// prints, and every reading here comes from the player or from the script --
// nothing is inferred from the fact that the test was supposed to pass.
//
// The assertions are written against the behaviour the state machine must have,
// not against the code that happens to be here: on the pre-fix player the ones
// named in F0-b (reset-once-*, reset-always-*, dead-handle-released,
// prepare-root-cause, stuck-no-cleanup-storm, live-*) fail, which is what makes
// this a negative control rather than a description.

// Scripted answers. Results are consumed in order and the last one repeats, so
// {NOMEM, NOERROR} is "fail once, then recover" and {NOMEM} is "never recover".
// An empty script always succeeds.
struct ScriptedResult {
  std::vector<MMRESULT> results;
  std::size_t next = 0;
  MMRESULT take() {
    if (results.empty()) return MMSYSERR_NOERROR;
    if (next < results.size()) return results[next++];
    return results.back();
  }
};

struct FakeWinMm {
  ScriptedResult open;
  ScriptedResult prepare;
  ScriptedResult write;
  ScriptedResult reset;
  ScriptedResult unprepare;
  ScriptedResult close;
  ScriptedResult errorText;
  std::vector<std::string> calls;
  std::size_t opens = 0;
  std::size_t prepares = 0;
  std::size_t writes = 0;
  std::size_t resets = 0;
  std::size_t unprepares = 0;
  std::size_t closes = 0;
  std::size_t cleanupFailures = 0;
  HWAVEOUT handle = reinterpret_cast<HWAVEOUT>(static_cast<std::uintptr_t>(0x4d4d0001u));

  // A scenario owns its scripts and its call log; opens accumulate in
  // g_fakeOpenTotal because the open counter in the player is process-wide and
  // has to keep matching it across scenarios.
  void clear() {
    const HWAVEOUT keep = handle;
    *this = FakeWinMm{};
    handle = keep;
  }
  std::size_t countOf(const char* name) const {
    std::size_t seen = 0;
    for (const auto& call : calls) {
      if (call == name) ++seen;
    }
    return seen;
  }
};

FakeWinMm g_fake;
std::size_t g_fakeOpenTotal = 0;

MMRESULT WINAPI fakeWaveOutOpen(LPHWAVEOUT out, UINT, LPCWAVEFORMATEX, DWORD_PTR, DWORD_PTR, DWORD) {
  g_fake.calls.push_back("open");
  const MMRESULT result = g_fake.open.take();
  if (result == MMSYSERR_NOERROR) {
    if (out != nullptr) *out = g_fake.handle;
    ++g_fake.opens;
    ++g_fakeOpenTotal;
  }
  return result;
}

MMRESULT WINAPI fakeWaveOutPrepareHeader(HWAVEOUT, LPWAVEHDR header, UINT) {
  g_fake.calls.push_back("prepare");
  const MMRESULT result = g_fake.prepare.take();
  if (result == MMSYSERR_NOERROR) {
    // The real driver sets WHDR_PREPARED on success; the player is allowed to
    // rely on it (and today does), so the fake has to reproduce it.
    if (header != nullptr) header->dwFlags |= WHDR_PREPARED;
    ++g_fake.prepares;
  }
  return result;
}

MMRESULT WINAPI fakeWaveOutWrite(HWAVEOUT, LPWAVEHDR, UINT) {
  g_fake.calls.push_back("write");
  const MMRESULT result = g_fake.write.take();
  if (result == MMSYSERR_NOERROR) ++g_fake.writes;
  return result;
}

MMRESULT WINAPI fakeWaveOutReset(HWAVEOUT) {
  g_fake.calls.push_back("reset");
  const MMRESULT result = g_fake.reset.take();
  if (result == MMSYSERR_NOERROR) {
    ++g_fake.resets;
  } else {
    ++g_fake.cleanupFailures;
  }
  return result;
}

MMRESULT WINAPI fakeWaveOutUnprepareHeader(HWAVEOUT, LPWAVEHDR header, UINT) {
  g_fake.calls.push_back("unprepare");
  const MMRESULT result = g_fake.unprepare.take();
  if (result == MMSYSERR_NOERROR) {
    if (header != nullptr) header->dwFlags &= ~WHDR_PREPARED;
    ++g_fake.unprepares;
  } else if (result != WAVERR_UNPREPARED) {
    // WAVERR_UNPREPARED means "it was not prepared"; the buffer is not queued
    // either way, so it is not a cleanup failure.
    ++g_fake.cleanupFailures;
  }
  return result;
}

MMRESULT WINAPI fakeWaveOutClose(HWAVEOUT) {
  g_fake.calls.push_back("close");
  const MMRESULT result = g_fake.close.take();
  if (result == MMSYSERR_NOERROR) {
    ++g_fake.closes;
  } else {
    ++g_fake.cleanupFailures;
  }
  return result;
}

MMRESULT WINAPI fakeWaveOutGetErrorTextA(MMRESULT, LPSTR text, UINT capacity) {
  g_fake.calls.push_back("errorText");
  const MMRESULT result = g_fake.errorText.take();
  if (result != MMSYSERR_NOERROR) return result;
  const char* message = "injected";
  if (text == nullptr || capacity < 9) return MMSYSERR_INVALPARAM;
  for (UINT index = 0; index < 9; ++index) text[index] = message[index];
  return MMSYSERR_NOERROR;
}

tf2::native::WinMmBackend fakeBackend() {
  tf2::native::WinMmBackend backend;
  backend.open = &fakeWaveOutOpen;
  backend.prepare = &fakeWaveOutPrepareHeader;
  backend.write = &fakeWaveOutWrite;
  backend.reset = &fakeWaveOutReset;
  backend.unprepare = &fakeWaveOutUnprepareHeader;
  backend.close = &fakeWaveOutClose;
  backend.errorText = &fakeWaveOutGetErrorTextA;
  return backend;
}

struct FaultChecks {
  std::vector<std::string> failed;
  void check(bool ok, const char* name) {
    if (!ok) failed.push_back(name);
  }
  std::string list() const {
    std::string joined;
    for (const auto& name : failed) {
      if (!joined.empty()) joined += "+";
      joined += name;
    }
    return joined;
  }
};

int runPlayerFaultSelfTest() {
  using namespace tf2::native;
  FaultChecks checks;
  const std::size_t openCallsAtStart = waveOutOpenCalls();
  const std::uint32_t openAttemptsAtStart = audioDeviceOpenAttempts();
  const std::uint32_t openedAtStart = audioDeviceOpenedCount();
  const WinMmBackend backend = fakeBackend();
  installWinMmBackend(&backend);

  // S1: the healthy path. If the seam changed it, every other reading below is
  // about a different player.
  {
    g_fake.clear();
    AudioPlayer player;
    player.setDeviceId(0);
    checks.check(player.play(tinyWav(), 1.0f, 0.0f), "clean-play");
    // The fixture has to reach the seam at all. Without this reading, a
    // fixture that fails play()'s format gate before any WinMM call turns
    // every scenario red for a reason that has nothing to do with cleanup,
    // and a red phase that fails on every play() attributes nothing.
    checks.check(g_fake.opens == 1, "clean-open");
    checks.check(player.playing(), "clean-playing");
    player.stop();
    checks.check(!player.playing(), "clean-released");
    checks.check(g_fake.closes == 1, "clean-close");
    checks.check(player.lastError().empty(), "clean-last-error-empty");
  }

  // S2: one refused reset must not end the cleanup. Unprepare is the step that
  // proves the driver no longer owns the buffer, and it has to be reached even
  // though reset was refused.
  {
    g_fake.clear();
    g_fake.reset.results = {MMSYSERR_NOMEM, MMSYSERR_NOERROR};
    AudioPlayer player;
    checks.check(player.play(tinyWav(), 1.0f, 0.0f), "reset-once-play");
    player.stop();
    checks.check(!player.playing(), "reset-once-released");
    checks.check(g_fake.unprepares == 1, "reset-once-unprepared");
    checks.check(g_fake.closes == 1, "reset-once-closed");
  }

  // S3: a reset that never succeeds still has to release the buffer while
  // unprepare and close work.
  {
    g_fake.clear();
    g_fake.reset.results = {MMSYSERR_NOMEM};
    AudioPlayer player;
    checks.check(player.play(tinyWav(), 1.0f, 0.0f), "reset-always-play");
    player.stop();
    checks.check(!player.playing(), "reset-always-released");
    checks.check(g_fake.unprepares == 1, "reset-always-unprepared");
    checks.check(g_fake.closes == 1, "reset-always-closed");
  }

  // S4: a dead handle is the one case where freeing without unprepare/close is
  // correct -- no driver can be holding the buffer through an invalid handle.
  {
    g_fake.clear();
    g_fake.reset.results = {MMSYSERR_INVALHANDLE};
    AudioPlayer player;
    checks.check(player.play(tinyWav(), 1.0f, 0.0f), "dead-handle-play");
    player.stop();
    checks.check(!player.playing(), "dead-handle-released");
    checks.check(g_fake.closes == 0, "dead-handle-no-close");
  }

  // S5: when the failure that stopped playback and the failure that stopped the
  // cleanup differ, lastError() must name the first one -- that is the reason a
  // reader has anything to act on, and the cleanup error must not overwrite it.
  {
    g_fake.clear();
    g_fake.prepare.results = {MMSYSERR_NOMEM};
    g_fake.close.results = {MMSYSERR_NOMEM, MMSYSERR_NOERROR};
    AudioPlayer player;
    checks.check(!player.play(tinyWav(), 1.0f, 0.0f), "prepare-play-failed");
    checks.check(player.lastError().find("waveOutPrepareHeader") != std::string::npos,
      "prepare-root-cause");
    player.stop();
    checks.check(!player.playing(), "prepare-retry-released");
  }

  // S6: a refused write is the same shape with a cleanup that works, so this is
  // the control for S5: the root cause must be named here too.
  {
    g_fake.clear();
    g_fake.write.results = {MMSYSERR_NOMEM};
    AudioPlayer player;
    checks.check(!player.play(tinyWav(), 1.0f, 0.0f), "write-play-failed");
    checks.check(player.lastError().find("waveOutWrite") != std::string::npos, "write-named");
    checks.check(!player.playing(), "write-released");
  }

  // S7: a voice that cannot be cleaned up must stop asking. The scheduler hands
  // one event per scheduled sound, so retrying the failing sequence on every
  // play() turns a single device fault into an API storm that buries the
  // original error.
  {
    g_fake.clear();
    g_fake.close.results = {MMSYSERR_NOMEM};
    AudioPlayer player;
    checks.check(player.play(tinyWav(), 1.0f, 0.0f), "stuck-play");
    player.stop();
    checks.check(player.playing(), "stuck-retained");
    const std::size_t callsWhenStuck = g_fake.calls.size();
    for (int attempt = 0; attempt < 5; ++attempt) player.play(tinyWav(), 1.0f, 0.0f);
    checks.check(g_fake.calls.size() == callsWhenStuck, "stuck-no-cleanup-storm");
    g_fake.close.results.clear();
    player.stop();
    checks.check(!player.playing(), "stuck-recovers");
  }

  // S8: the P0. reset() derives stopped_ from activeVoiceCount(); one voice whose
  // close failed used to count as active forever, so every later advance()
  // answered with the same stats -- permanent silence after one device hiccup.
  {
    g_fake.clear();
    g_fake.close.results = {MMSYSERR_NOMEM, MMSYSERR_NOERROR};
    AudioEventScheduler scheduler;
    scheduler.setDeviceId(0);
    scheduler.setResolver([](const std::string&, WavPcmData& out) { out = tinyWav(); return true; });
    SoundEventTimeline timeline;
    for (std::int32_t tick = 1; tick <= 4; ++tick) {
      scheduleGameplaySound(timeline, soundAt(tick, "weapons/rocket_shoot.wav", 0.0f), 10.0);
    }
    timeline.sortByTick();
    scheduler.reset(0);
    const auto first = scheduler.advance(timeline, 1);
    checks.check(first.eventsPlayed == 1, "live-first-played");
    scheduler.reset(2);
    checks.check(scheduler.activeVoiceCount() == 0, "live-stuck-voice-not-active");
    const auto after = scheduler.advance(timeline, 3);
    checks.check(after.eventsSeen == 1, "live-advance-moves");
    checks.check(scheduler.lastTick() == 3, "live-last-tick");
    g_fake.close.results.clear();
    scheduler.stop();
    checks.check(scheduler.activeVoiceCount() == 0, "live-recovered");
  }

  // S9: the destructor is the last cleanup opportunity. If close still refuses,
  // the buffer is deliberately kept -- freeing it would hand the device memory
  // the process is about to return to the allocator -- and counted. This runs
  // last because that state stays retained until the process exits.
  {
    g_fake.clear();
    g_fake.close.results = {MMSYSERR_NOMEM};
    {
      AudioPlayer player;
      checks.check(player.play(tinyWav(), 1.0f, 0.0f), "exit-play");
    }
    checks.check(g_fake.countOf("close") == 1, "exit-close-attempted");
  }

  installWinMmBackend(nullptr);
  const bool restored = &winMmBackend() == &productionWinMmBackend();
  // The open counters live at the call site, above the seam: they must have
  // moved with the scripted opens, which is what proves the seam covers every
  // path that can reach a device.
  checks.check(openCallsAtStart + g_fakeOpenTotal == waveOutOpenCalls(), "open-counter-matches");
  checks.check(openAttemptsAtStart + g_fakeOpenTotal == audioDeviceOpenAttempts(),
    "open-attempts-match");
  checks.check(openedAtStart + g_fakeOpenTotal == audioDeviceOpenedCount(), "opened-count-matches");
  checks.check(restored, "production-backend-restored");

  const bool pass = checks.failed.empty();
  std::cout << "{\"playerFaultSelfTest\":" << (pass ? "true" : "false")
            << ",\"failedChecks\":" << checks.failed.size()
            << ",\"failed\":\"" << checks.list() << "\""
            << ",\"fakeOpens\":" << g_fakeOpenTotal
            << ",\"fakeCloses\":" << g_fake.closes
            << ",\"fakeResets\":" << g_fake.resets
            << ",\"fakeUnprepares\":" << g_fake.unprepares
            << ",\"fakeCleanupFailures\":" << g_fake.cleanupFailures
            << ",\"deviceOpenCalls\":" << waveOutOpenCalls()
            << ",\"backendRestored\":" << (restored ? "true" : "false")
            << "}\n";
  return pass ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
  if (argc > 1 && std::string(argv[1]) == "--player-fault-selftest") return runPlayerFaultSelfTest();
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

  // The scheduler in this mode is driven by sinks, so the number of WinMM opens
  // this process made must be zero. Asserting the counter turns "this probe
  // never touches a device" from a property of the code into a reading.
  const bool noDevice = waveOutOpenCalls() == 0;
  const bool pass = delayMath && kinds && spatial && deviceUntouched && missingKept && hud
    && particleTiming && syntheticPcfOk && emptyRejected && unsupportedRejected && truncatedRejected
    && realOk && noDevice;
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
            << ",\"device\":\"sink\""
            << ",\"deviceOpenCalls\":" << tf2::native::waveOutOpenCalls()
            << "}\n";
  return pass ? 0 : 1;
}
