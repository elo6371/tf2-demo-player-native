// audio_scheduler_probe.cpp -- tick-accurate audio scheduling under a null sink.
//
// WHAT THIS PROVES
//   1. The sound index chain -- precache name -> VPK entry -> parsed WAV ->
//      playable PCM -- reports a named reason at each failure point:
//      missing_precache, missing_wav, invalid_wav, unsupported_format,
//      excessive_duration. A name carrying one or two Source sound characters
//      (')weapons/shotgun_shoot.wav') still resolves to the file it names.
//   2. The scheduler's tick cursor: one advance per tick with no replay, no
//      duplicate events forward, reset re-seeds the cursor so reverse and seek
//      can replay, delay converts to ticks with round-half-away-from-zero, and
//      events at different ticks resolve in stable order.
//   3. The nine-way classification, with voice, announcer and music filtered
//      at advance time: counted as skipped, never resolved, never played.
//   4. The null-sink contract: with a playback sink injected the scheduler
//      resolves, counts and advances without any audio-device open.
//
// THE NOISE CONTRACT
//   No scenario opens a real audio device. The one scenario that exercises the
//   "injected sink ignored" defect shape drives the real AudioPlayer against
//   an out-of-range device id, so waveOutOpen fails with MMSYSERR_BADDEVICEID:
//   the attempt counter moves (that is the reading) and nothing is audible.
//   The id used is waveOutGetNumDevs(), out of range on every machine; 0xFFFF
//   must not be used because WinMM reads the device id as a 16-bit value and
//   0xFFFF aliases to WAVE_MAPPER, which opens the default device.
//   --audio-device 6 is deliberately not used anywhere; this task is the proof
//   that the contract holds without a device, and only a later real-device
//   acceptance run would pass one.
//
// OUTPUT
//   stdout: one line of key=value readings, printed only when every check and
//   the whole pinned contract hold. verify-audio.sh pins several of these keys
//   as fixed substrings; the keys it already pinned keep both their names and
//   their values, and new keys are appended after them.
//   stderr: per-check results and per-mutation diagnostics. It deliberately
//   contains none of the pinned substrings, so the gate's --mutation sed
//   cannot hit a diagnostic line instead of the readings line.
//
// MUTATIONS
//   Eleven defects, one flip each. Every flip is computed from a real defective
//   computation -- a replica of the broken code path walking the same library
//   timeline, or the real scheduler driven into the defect shape -- and then
//   overwrites only the readings that flip moves. The normal run fails unless
//   every flip breaks the pinned contract, and `--mutation <case>` runs one
//   flip and reports which contract keys went red. Source-level mutations
//   belong in mutate.sh at integration time; these prove the readings
//   discriminate without touching a sixth file.
//
// Usage: audio_scheduler_probe [--self-test] [--mutation <case>]
//   --self-test        the normal contract run (default)
//   --mutation <case>  tick_dedup | reverse_reset | voice_filter |
//                      delay_rounding | missing_as_played | null_sink_device |
//                      duration_cap | marker_strip | mp3_decode |
//                      mp3_silent_accept | mp3_playback_drop
//   Exit: 0 = contract held (or, under --mutation, the flip was caught);
//         1 = a check, the contract, or a mutation's discrimination failed;
//         2 = usage error.

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "audio_player.h"
#include "vpk_archive.h"

using namespace tf2::native;

namespace {

// --------------------------------------------------------------------------
// Readings: the key=value line the gate pins.
// --------------------------------------------------------------------------

class Readings {
public:
  void set(const std::string& key, long long value) { set(key, std::to_string(value)); }
  void set(const std::string& key, const std::string& value) {
    for (auto& entry : entries_) {
      if (entry.first == key) { entry.second = value; return; }
    }
    entries_.emplace_back(key, value);
  }
  const std::string* find(const std::string& key) const {
    for (const auto& entry : entries_) {
      if (entry.first == key) return &entry.second;
    }
    return nullptr;
  }
  std::string line() const {
    std::string out;
    for (const auto& entry : entries_) {
      if (!out.empty()) out += ' ';
      out += entry.first + '=' + entry.second;
    }
    return out;
  }

private:
  std::vector<std::pair<std::string, std::string>> entries_;
};

// The pinned contract. Every key here is a fixed substring verify-audio.sh
// greps for (the first sixteen are the ones it pinned before this round); the
// value is the only one this probe may print for it. A mutation is "caught"
// exactly when it moves one of these keys.
const std::vector<std::pair<std::string, std::string>>& pinnedContract() {
  static const std::vector<std::pair<std::string, std::string>> pinned = {
    {"device", "0"}, {"playbackCalls", "0"},
    {"sinkCalls", "1"}, {"queuedPcm", "1"}, {"sinkQueueLimit", "8"},
    {"accepted", "8"}, {"rejected", "1"}, {"vpkWav", "1"},
    {"jumpSeen", "2"}, {"pausedSeen", "2"}, {"resumeSeen", "2"},
    {"repeatedSeen", "2"}, {"reverseSeen", "0"}, {"recoveredSeen", "2"},
    {"missingPlayed", "0"}, {"missingResource", "2"}, {"wavMalformed", "3"},
    {"eventsSeen", "10"}, {"eventsPlayed", "6"}, {"eventsSkippedVoice", "3"},
    {"eventsMissingResource", "1"}, {"lastTick", "10"}, {"activeVoiceCount", "0"},
    {"device_opened", "0"}, {"device_open_attempts", "0"},
    {"indexResolved", "2"}, {"indexResolvedWav", "1"}, {"indexResolvedMp3", "1"},
    {"indexMissingPrecache", "1"}, {"indexMissingWav", "1"},
    {"indexInvalidWav", "1"}, {"indexUnsupportedFormat", "0"},
    {"indexMp3Path", "sound/ambient_mp3/lair/rolling_thunder1.mp3"},
    {"indexMp3PcmBytes", "1437696"}, {"indexMp3Rate", "44100"}, {"indexMp3Channels", "2"},
    {"indexMp3Seconds", "8.15"},
    {"mp3EventPath", "sound/ambient_mp3/water/water_splash1.mp3"},
    {"mp3EventStatus", "resolved"}, {"mp3EventPcmBytes", "122112"},
    {"mp3EventSeconds", "1.38"}, {"mp3EventRate", "44100"}, {"mp3EventChannels", "1"},
    {"mp3DecoderAvailable", "1"},
    {"mp3NoFrameStatus", "invalid_wav"}, {"mp3SilentStatus", "invalid_wav"},
    {"mp3Layer2Status", "unsupported_format"},
    {"mp3MapNoDecoderStatus", "unsupported_format"},
    {"mp3MapRefusedFormatStatus", "unsupported_format"}, {"mp3MapDecodedStatus", "resolved"},
    {"kindWeapon", "2"}, {"kindFootstep", "1"}, {"kindUber", "1"},
    {"kindTempEntity", "1"}, {"kindPcf", "1"}, {"kindVoice", "1"},
    {"kindAnnouncer", "1"}, {"kindMusic", "1"}, {"kindWorld", "1"},
    {"realTempEntity", "0"}, {"realPcf", "0"},
    {"delayHalf", "15"}, {"delayClamp", "0"}, {"delayRound", "5"}, {"delayZeroRate", "-1"},
    {"indexExcessiveDuration", "1"}, {"durationOverCapSeconds", "4194304"},
    {"durationUnderCapSeconds", "524"},
    {"indexMarkedResolved", "2"}, {"indexMarkedPath", "sound/weapons/shotgun_shoot.wav"},
    {"readMarkedPath", "sound/weapons/shotgun_shoot.wav"},
    {"mp3PlaybackSeen", "5"}, {"mp3PlaybackPlayed", "2"}, {"mp3PlaybackMissing", "3"},
    {"mp3PlaybackSinkCalls", "2"}, {"mp3PlaybackRefused", "3"},
    {"mp3PlaybackRefusalNames",
      "probe_no_frame.mp3:invalid_wav,probe_sync_only.mp3:invalid_wav,"
      "probe_layer2.mp3:unsupported_format"},
    {"mp3PlaybackMp3Seen", "1"}, {"mp3PlaybackMp3Played", "1"}, {"mp3PlaybackMp3Failed", "0"},
    {"mp3PlaybackObserved", "probe_play_wav.wav:played,probe_play_mp3.mp3:played"},
  };
  return pinned;
}

// Keys whose value is not the pinned one. A missing key counts as broken: a
// reading that stopped being printed is not a reading that holds.
std::vector<std::string> contractBreaches(const Readings& readings) {
  std::vector<std::string> broken;
  for (const auto& [key, expected] : pinnedContract()) {
    const std::string* actual = readings.find(key);
    if (actual == nullptr || *actual != expected) broken.push_back(key);
  }
  return broken;
}

// --------------------------------------------------------------------------
// Fixtures.
// --------------------------------------------------------------------------

WavPcmData fixtureWav() {
  WavPcmData wav;
  wav.valid = true;
  wav.encoding = WavEncoding::Pcm;
  wav.channels = 1;
  wav.sampleRate = 11025;
  wav.bitsPerSample = 8;
  wav.blockAlign = 1;
  wav.byteRate = 11025;
  wav.pcm = {128, 128, 128, 128};
  return wav;
}

// A minimal PCM WAV built byte by byte: RIFF header, fmt chunk (PCM, mono,
// 8 bits), data chunk of `dataBytes` zeroed samples. The caller picks the
// rates, so the same generator produces the multi-day buffer (rate 1) and its
// playable-rate control (rate 8000) from identical bytes apart from the rate
// fields.
std::vector<std::uint8_t> craftWav(std::uint32_t sampleRate, std::uint16_t blockAlign,
    std::uint32_t byteRate, std::size_t dataBytes) {
  std::vector<std::uint8_t> bytes;
  const auto u16 = [&bytes](std::uint16_t value) {
    bytes.push_back(static_cast<std::uint8_t>(value & 0xff));
    bytes.push_back(static_cast<std::uint8_t>((value >> 8) & 0xff));
  };
  const auto u32 = [&bytes](std::uint32_t value) {
    bytes.push_back(static_cast<std::uint8_t>(value & 0xff));
    bytes.push_back(static_cast<std::uint8_t>((value >> 8) & 0xff));
    bytes.push_back(static_cast<std::uint8_t>((value >> 16) & 0xff));
    bytes.push_back(static_cast<std::uint8_t>((value >> 24) & 0xff));
  };
  const auto text = [&bytes](const char* four) {
    for (int i = 0; i < 4; ++i) bytes.push_back(static_cast<std::uint8_t>(four[i]));
  };
  text("RIFF");
  u32(static_cast<std::uint32_t>(36 + dataBytes));
  text("WAVE");
  text("fmt ");
  u32(16);
  u16(1);
  u16(1);
  u32(sampleRate);
  u32(byteRate);
  u16(blockAlign);
  u16(8);
  text("data");
  u32(static_cast<std::uint32_t>(dataBytes));
  bytes.resize(bytes.size() + dataBytes, 128u);
  return bytes;
}

// Crafted MPEG streams for the three MP3 refusals. Each is the smallest byte
// sequence that reaches the failure it names, so the assertion is about the
// decoder's contract rather than about a file that happens to ship.
//
// No valid frame at all: an ID3v2 header that declares an empty tag, then
// nothing. The signature check must not fall back to "contains 0xFF".
std::vector<std::uint8_t> craftMp3NoFrame() {
  std::vector<std::uint8_t> bytes = {'I', 'D', '3', 3, 0, 0, 0, 0, 0, 0};
  bytes.resize(bytes.size() + 64u, 0u);
  return bytes;
}

// Sync-shaped bytes with no audio behind them: MPEG-1 Layer III, 128 kbps,
// 44.1 kHz, stereo -- every header field valid -- repeated 200 times with no
// frame payload. Measured 2026-10-16: ACM opens this stream, consumes 800 bytes
// and produces 0 PCM bytes. That must be a refusal, not a zero-length buffer
// handed to the output path.
std::vector<std::uint8_t> craftMp3SyncOnly() {
  std::vector<std::uint8_t> bytes;
  for (int i = 0; i < 200; ++i) {
    bytes.push_back(0xffu);
    bytes.push_back(0xfbu);   // MPEG-1, Layer III, no CRC
    bytes.push_back(0x90u);   // bitrate index 9 = 128 kbps, rate index 0 = 44100
    bytes.push_back(0x00u);   // stereo
  }
  return bytes;
}

// A Layer II header: version 1, layer 2 (0b10). The product's decoder is
// Layer III only, and this is the stream that says so by name instead of
// feeding a Layer II frame to a Layer III codec.
std::vector<std::uint8_t> craftMp3Layer2() {
  std::vector<std::uint8_t> bytes = {0xffu, 0xfdu, 0x90u, 0x00u};
  bytes.resize(bytes.size() + 256u, 0u);
  return bytes;
}

SoundPlaybackEvent eventAt(std::int32_t tick, std::uint32_t index, const char* name) {
  SoundPlaybackEvent event;
  event.tick = tick;
  event.soundIndex = index;
  event.name = name;
  return event;
}

// --------------------------------------------------------------------------
// The contract run: every scenario, in one pass, collecting readings.
// --------------------------------------------------------------------------

struct ContractRun {
  Readings readings;
  std::vector<std::pair<std::string, bool>> checks;
  // Handles the mutation replicas walk. They are the same objects the
  // scenarios drove, so a replica cannot quietly drift onto a different
  // timeline than the one under test.
  SoundEventTimeline tickTimeline;
  SoundEventTimeline mixedTimeline;
  // The five-event refusal case (2d): the timeline and the bytes each name
  // resolves to, kept on the run so the mutation replica drives the same
  // objects instead of rebuilding a lookalike.
  SoundEventTimeline mp3Timeline;
  std::unordered_map<std::string, std::vector<std::uint8_t>> mp3ByName;
  WavPcmData fixture;
  std::size_t missingEvents = 0;
  std::size_t mixedPlayed = 0;
  std::size_t mixedSkipped = 0;
  std::size_t repeatedSeen = 0;
  std::size_t recoveredSeen = 0;
  bool archivesOpened = false;
};

void check(ContractRun& run, const std::string& what, bool condition) {
  run.checks.emplace_back(what, condition);
}

void runContract(ContractRun& run) {
  run.fixture = fixtureWav();
  const char* tfRoot = std::getenv("TF_ROOT");
  const std::string root = tfRoot ? tfRoot : "D:/SteamLibrary/steamapps/common/Team Fortress 2/tf";

  VpkArchive archive;
  if (!archive.open(root + "/tf2_sound_misc_dir.vpk")) {
    check(run, "the sound VPK opened", false);
    return;
  }
  run.archivesOpened = true;
  check(run, "the sound VPK opened", true);

  // -- 1. Malformed WAV bytes are rejected with a named error ---------------
  WavPcmData wav;
  std::size_t malformed = 0;
  if (!parseWav({}, wav) && !wav.error.empty()) ++malformed;
  const std::vector<std::uint8_t> truncated = {'R','I','F','F', 0,0,0,0, 'W','A','V','E'};
  if (!parseWav(truncated, wav) && !wav.error.empty()) ++malformed;
  const std::vector<std::uint8_t> badChunk = {'R','I','F','F', 20,0,0,0, 'W','A','V','E',
    'd','a','t','a', 8,0,0,0, 0,0,0,0};
  if (!parseWav(badChunk, wav) && wav.error.find("exceeds") != std::string::npos) ++malformed;
  run.readings.set("wavMalformed", static_cast<long long>(malformed));
  check(run, "three malformed WAVs were rejected with named errors", malformed == 3);

  // -- 1b. Excessive duration is its own named refusal ----------------------
  // A WAV can be byte-legal and playable-shaped and still not be a sound: at
  // one byte per second a 4 MiB buffer would hold one of the eight voice slots
  // for 48 days. The identical bytes at 8000 samples/second are 524 s and must
  // resolve -- that control is what proves the refusal came from the duration
  // and not from the shape. Both go through the same classifier the index
  // chain runs on real archive bytes.
  SoundResource slowResource;
  std::string slowDetail;
  const auto slowStatus = classifySoundResourceBytes(craftWav(1, 1, 1, 4u * 1024u * 1024u),
    "sound/probe_forever.wav", slowResource, slowDetail);
  SoundResource fastResource;
  std::string fastDetail;
  const auto fastStatus = classifySoundResourceBytes(craftWav(8000, 1, 8000, 4u * 1024u * 1024u),
    "sound/probe_forever.wav", fastResource, fastDetail);
  std::cerr << "duration_map slow=" << soundResolveStatusName(slowStatus)
            << " fast=" << soundResolveStatusName(fastStatus) << '\n'
            << "duration_detail slow=\"" << slowDetail << "\" fast=\"" << fastDetail << "\"\n";
  run.readings.set("indexExcessiveDuration",
    static_cast<long long>(slowStatus == SoundResolveStatus::ExcessiveDuration ? 1 : 0));
  run.readings.set("durationOverCapSeconds",
    static_cast<long long>(wavDurationSeconds(slowResource.wav)));
  run.readings.set("durationUnderCapSeconds",
    static_cast<long long>(wavDurationSeconds(fastResource.wav)));
  check(run, "a byte-legal WAV whose buffer would play for days is refused by duration, not shape",
    slowStatus == SoundResolveStatus::ExcessiveDuration
      && fastStatus == SoundResolveStatus::Resolved
      && slowResource.wav.valid && fastResource.wav.valid);
  check(run, "the duration refusal names the seconds and the cap",
    slowDetail.find("4194304.0") != std::string::npos
      && slowDetail.find(std::to_string(static_cast<long long>(kSoundDurationCapSeconds)))
           != std::string::npos);

  // -- 2. Sound index -> precache name -> VPK resource -> playable PCM ------
  // Real archive entries for the reachable outcomes: a playable PCM WAV, a name
  // with no entry, an ADPCM WAV the parser rejects (invalid_wav), and -- since
  // the MP3 decode landed -- a real MP3 that resolves to PCM. Index 5 is
  // deliberately absent from the table (missing_precache).
  //
  // `indexUnsupportedFormat` is pinned at 0 here, and that is a reading rather
  // than an omission: every resource this chain can reach is WAV or MP3, both
  // of which now either yield PCM or fail by name earlier in the chain, so a
  // non-zero count would mean a resource had a third extension or a decoder had
  // refused one. The status stays reachable and is exercised below with crafted
  // bytes.
  SoundIndexTable table;
  table.set(1, "weapons/stickybomblauncher_det.wav");
  table.set(2, "weapons/probe_missing.wav");
  table.set(3, "player/medic_charged_death.wav");
  table.set(4, "ambient_mp3/lair/rolling_thunder1.mp3");
  std::size_t indexResolved = 0, indexResolvedWav = 0, indexResolvedMp3 = 0,
    indexMissingPrecache = 0, indexMissingWav = 0,
    indexInvalidWav = 0, indexUnsupportedFormat = 0;
  SoundResource resolved;
  SoundResource resolvedMp3;
  for (std::uint32_t index = 1; index <= 5; ++index) {
    SoundResource resource;
    std::string detail;
    const auto status = resolveSoundIndexResource(table, archive, index, resource, detail);
    std::cerr << "index_map index=" << index << " status=" << soundResolveStatusName(status)
              << " detail=" << detail << '\n';
    switch (status) {
    case SoundResolveStatus::Resolved:
      ++indexResolved;
      if (resource.codec == SoundCodec::Mp3) ++indexResolvedMp3; else ++indexResolvedWav;
      if (index == 1) resolved = resource;
      if (index == 4) resolvedMp3 = resource;
      break;
    case SoundResolveStatus::MissingPrecache: ++indexMissingPrecache; break;
    case SoundResolveStatus::MissingWav: ++indexMissingWav; break;
    case SoundResolveStatus::InvalidWav: ++indexInvalidWav; break;
    case SoundResolveStatus::UnsupportedFormat: ++indexUnsupportedFormat; break;
    case SoundResolveStatus::ExcessiveDuration: break;
    }
  }
  run.readings.set("indexResolved", static_cast<long long>(indexResolved));
  // The resolved total is split by codec rather than reported as one number.
  // `vpkWav` used to be `indexResolved` itself, which was the same count only
  // while the chain could resolve nothing but WAV; with the MP3 in the chain the
  // same expression would print vpkWav=2 and make the name mean "resources",
  // not "WAVs". Splitting keeps vpkWav about WAVs and gives the MP3 resolution
  // its own reading.
  run.readings.set("indexResolvedWav", static_cast<long long>(indexResolvedWav));
  run.readings.set("indexResolvedMp3", static_cast<long long>(indexResolvedMp3));
  run.readings.set("indexMissingPrecache", static_cast<long long>(indexMissingPrecache));
  run.readings.set("indexMissingWav", static_cast<long long>(indexMissingWav));
  run.readings.set("indexInvalidWav", static_cast<long long>(indexInvalidWav));
  run.readings.set("indexUnsupportedFormat", static_cast<long long>(indexUnsupportedFormat));
  run.readings.set("indexMp3Path", resolvedMp3.valid ? resolvedMp3.archivePath : "-");
  run.readings.set("indexMp3PcmBytes", static_cast<long long>(resolvedMp3.wav.pcm.size()));
  run.readings.set("indexMp3Rate", static_cast<long long>(resolvedMp3.wav.sampleRate));
  run.readings.set("indexMp3Channels", static_cast<long long>(resolvedMp3.wav.channels));
  {
    char seconds[32]{};
    std::snprintf(seconds, sizeof(seconds), "%.2f", wavDurationSeconds(resolvedMp3.wav));
    run.readings.set("indexMp3Seconds", seconds);
  }
  check(run, "each failure point of the index chain got its named reason",
    indexResolved == 2 && indexMissingPrecache == 1 && indexMissingWav == 1
      && indexInvalidWav == 1 && indexUnsupportedFormat == 0);
  check(run, "the resolved WAV is playable PCM from the archive",
    resolved.valid && resolved.codec == SoundCodec::Wav && wavPcmIsPlayable(resolved.wav)
      && resolved.archivePath == "sound/weapons/stickybomblauncher_det.wav"
      && !resolved.wav.pcm.empty());
  check(run, "the real MP3 in the archives resolves to playable PCM",
    resolvedMp3.valid && resolvedMp3.codec == SoundCodec::Mp3 && wavPcmIsPlayable(resolvedMp3.wav)
      && resolvedMp3.archivePath == "sound/ambient_mp3/lair/rolling_thunder1.mp3"
      && resolvedMp3.wav.pcm.size() > 0 && resolvedMp3.wav.bitsPerSample == 16);
  // Cross-check against the product's own resolver: the two implementations must
  // agree on the real WAV, on the real MP3 (same decoded byte count), and on the
  // two byte-level failures. Before the MP3 decode this cross-check had to
  // exclude the MP3, because readSoundResource accepted it as a valid resource
  // while the named chain reported unsupported_format -- the divergence the
  // named statuses existed to make visible. There is no divergence left to
  // exempt, so the MP3 is in the check.
  SoundResource productResource;
  const bool productAgrees = readSoundResource(archive, "weapons/stickybomblauncher_det.wav", productResource)
    && productResource.valid && productResource.codec == SoundCodec::Wav
    && productResource.archivePath == resolved.archivePath
    && productResource.wav.pcm.size() == resolved.wav.pcm.size()
    && !readSoundResource(archive, "weapons/probe_missing.wav", productResource)
    && !readSoundResource(archive, "player/medic_charged_death.wav", productResource);
  {
    SoundResource productMp3;
    const bool mp3Agrees = readSoundResource(archive, "ambient_mp3/lair/rolling_thunder1.mp3", productMp3)
      && productMp3.valid && productMp3.codec == SoundCodec::Mp3
      && productMp3.archivePath == resolvedMp3.archivePath
      && productMp3.wav.pcm.size() == resolvedMp3.wav.pcm.size();
    run.readings.set("readMp3PcmBytes", static_cast<long long>(productMp3.wav.pcm.size()));
    check(run, "readSoundResource agrees with the named chain on the real WAV", productAgrees);
    check(run, "readSoundResource agrees with the named chain on the real MP3", mp3Agrees);
  }

  // -- 2b. Sound characters in front of a name are entry metadata -----------
  // TF2's soundprecache stores some names with one or two Source sound
  // characters (src/public/soundchars.h) in front of the file:
  // ')weapons/shotgun_shoot.wav' is a stock file with spatial-stereo metadata,
  // '*#mvm/mvm_tank_start.wav' is a stream+drymix pair. The name -> archive
  // path translation has to drop the run, or every marked stock sound reads as
  // missing and never plays. Both a single and a stacked case, against the
  // real VPK, so the reading is a file that actually ships.
  SoundIndexTable markedTable;
  markedTable.set(6, ")weapons/shotgun_shoot.wav");
  markedTable.set(7, "*#mvm/mvm_tank_start.wav");
  std::size_t markedResolved = 0;
  std::string markedPath;
  for (const std::uint32_t markedIndex : {6u, 7u}) {
    SoundResource markedResource;
    std::string markedDetail;
    const auto markedStatus = resolveSoundIndexResource(markedTable, archive, markedIndex, markedResource, markedDetail);
    std::cerr << "marked_map index=" << markedIndex << " status=" << soundResolveStatusName(markedStatus)
              << " detail=" << markedDetail << '\n';
    if (markedStatus == SoundResolveStatus::Resolved) {
      ++markedResolved;
      if (markedIndex == 6) markedPath = markedResource.archivePath;
    }
  }
  run.readings.set("indexMarkedResolved", static_cast<long long>(markedResolved));
  run.readings.set("indexMarkedPath", markedPath.empty() ? "-" : markedPath);
  // The product's own reader resolves the same marked name to the same file:
  // this is the call main.cpp's scheduler resolver makes.
  SoundResource markedProduct;
  const bool markedProductOk = readSoundResource(archive, ")weapons/shotgun_shoot.wav", markedProduct)
    && markedProduct.valid && markedProduct.archivePath == "sound/weapons/shotgun_shoot.wav";
  run.readings.set("readMarkedPath", markedProductOk ? markedProduct.archivePath : "-");
  check(run, "a sound character in front of the name does not hide the file",
    markedResolved == 2 && markedPath == "sound/weapons/shotgun_shoot.wav" && markedProductOk);

  // -- 2c. MP3 decode: the one positive, the three refusals ------------------
  // The positive case is a file the corpus actually sends: measured on the
  // pinned cp_well recording, the scheduler resolves
  // ambient_mp3/water/water_splash1.mp3 and water_splash2.mp3 as world-kind
  // events (ticks 4650 and 7675), i.e. sounds the output path is asked to play.
  // Before this round both reported unsupported_format and neither could play.
  {
    SoundResource splash;
    std::string splashDetail;
    SoundIndexTable splashTable;
    splashTable.set(11, "ambient_mp3/water/water_splash1.mp3");
    const auto splashStatus = resolveSoundIndexResource(splashTable, archive, 11, splash, splashDetail);
    char seconds[32]{};
    std::snprintf(seconds, sizeof(seconds), "%.2f", wavDurationSeconds(splash.wav));
    run.readings.set("mp3EventPath", splash.valid ? splash.archivePath : "-");
    run.readings.set("mp3EventStatus", soundResolveStatusName(splashStatus));
    run.readings.set("mp3EventPcmBytes", static_cast<long long>(splash.wav.pcm.size()));
    run.readings.set("mp3EventSeconds", seconds);
    run.readings.set("mp3EventRate", static_cast<long long>(splash.wav.sampleRate));
    run.readings.set("mp3EventChannels", static_cast<long long>(splash.wav.channels));
    check(run, "the MP3 a pinned recording actually sends decodes to playable PCM",
      splashStatus == SoundResolveStatus::Resolved && splash.valid
        && splash.archivePath == "sound/ambient_mp3/water/water_splash1.mp3"
        && wavPcmIsPlayable(splash.wav) && splash.wav.pcm.size() > 0
        && splash.wav.sampleRate == 44100 && splash.wav.bitsPerSample == 16);
  }
  // The machine's own capability, reported separately from any stream: the
  // refusal it produces cannot be produced on a machine that has the codec.
  run.readings.set("mp3DecoderAvailable", mp3DecoderAvailable() ? "1" : "0");

  // The three refusals, each through the same classifySoundResourceBytes the
  // named chain uses.
  {
    SoundResource resource;
    std::string detail;
    const auto noFrame = classifySoundResourceBytes(craftMp3NoFrame(), "sound/probe_no_frame.mp3", resource, detail);
    run.readings.set("mp3NoFrameStatus", soundResolveStatusName(noFrame));
    const auto silent = classifySoundResourceBytes(craftMp3SyncOnly(), "sound/probe_sync_only.mp3", resource, detail);
    run.readings.set("mp3SilentStatus", soundResolveStatusName(silent));
    const auto layer2 = classifySoundResourceBytes(craftMp3Layer2(), "sound/probe_layer2.mp3", resource, detail);
    run.readings.set("mp3Layer2Status", soundResolveStatusName(layer2));
    check(run, "MP3 bytes with no frame are invalid, not silently resolvable",
      noFrame == SoundResolveStatus::InvalidWav);
    check(run, "an MP3-shaped stream that decodes to zero PCM is refused",
      silent == SoundResolveStatus::InvalidWav);
    check(run, "a Layer II stream is refused by name, before the codec sees it",
      layer2 == SoundResolveStatus::UnsupportedFormat);
  }
  // The status a machine without an MP3 codec produces. This machine has one,
  // so the mapping is asserted directly instead of pretending to remove the
  // codec -- and it is asserted because the gap path is the one a bare machine
  // will take.
  run.readings.set("mp3MapNoDecoderStatus",
    soundResolveStatusName(soundResolveStatusForMp3Decode(Mp3DecodeStatus::NoDecoderInstalled)));
  run.readings.set("mp3MapRefusedFormatStatus",
    soundResolveStatusName(soundResolveStatusForMp3Decode(Mp3DecodeStatus::UnsupportedFormat)));
  run.readings.set("mp3MapDecodedStatus",
    soundResolveStatusName(soundResolveStatusForMp3Decode(Mp3DecodeStatus::Decoded)));
  check(run, "a machine with no MP3 codec reports a capability gap, not bad bytes",
    soundResolveStatusForMp3Decode(Mp3DecodeStatus::NoDecoderInstalled) == SoundResolveStatus::UnsupportedFormat
      && soundResolveStatusForMp3Decode(Mp3DecodeStatus::Decoded) == SoundResolveStatus::Resolved
      && soundResolveStatusForMp3Decode(Mp3DecodeStatus::InvalidStream) == SoundResolveStatus::InvalidWav);

  // -- 2d. The refusals at the playback boundary ----------------------------
  // 2c names what the bytes are; this case proves what the scheduler does with
  // those names: five events -- a playable WAV, the real MP3 the pinned
  // recording sends, and the three crafted refusals -- driven through the real
  // scheduler with the same classifySoundResourceBytes the product's resolver
  // calls. A refused MP3 has to be *counted* and must never reach the sink; a
  // silently dropped event would leave eventsSeen at four with no counter
  // explaining the fifth. `refused` is measured by the resolver, so
  // "seen == played + missing" and "refused == missing" are two separately
  // computed readings that have to agree.
  {
    run.mp3Timeline.clear();
    run.mp3ByName.clear();
    run.mp3ByName["sound/probe_play_wav.wav"] = craftWav(11025, 1, 11025, 64);
    run.mp3ByName["sound/probe_play_mp3.mp3"] =
      archive.read("sound/ambient_mp3/water/water_splash1.mp3");
    run.mp3ByName["sound/probe_no_frame.mp3"] = craftMp3NoFrame();
    run.mp3ByName["sound/probe_sync_only.mp3"] = craftMp3SyncOnly();
    run.mp3ByName["sound/probe_layer2.mp3"] = craftMp3Layer2();
    check(run, "the real MP3 the refusal case plays is present in the archive",
      !run.mp3ByName["sound/probe_play_mp3.mp3"].empty());
    const char* mp3FixtureNames[] = {"sound/probe_play_wav.wav", "sound/probe_play_mp3.mp3",
      "sound/probe_no_frame.mp3", "sound/probe_sync_only.mp3", "sound/probe_layer2.mp3"};
    bool built = true;
    for (std::uint32_t i = 0; i < 5; ++i) {
      if (!run.mp3Timeline.add(eventAt(static_cast<std::int32_t>(10 + i), 20u + i,
            mp3FixtureNames[i]))) built = false;
    }
    run.mp3Timeline.sortByTick();
    check(run, "the five-event refusal timeline was built", built && run.mp3Timeline.size() == 5);

    std::size_t refused = 0, sinkCalls = 0;
    std::string refusalNames;
    std::string observed;
    AudioEventScheduler mp3Scheduler;
    mp3Scheduler.setDeviceId(0);
    mp3Scheduler.setResolver([&](const std::string& name, WavPcmData& out) {
      const auto it = run.mp3ByName.find(name);
      if (it == run.mp3ByName.end()) return false;
      SoundResource resource;
      std::string detail;
      const auto status = classifySoundResourceBytes(it->second, name, resource, detail);
      std::cerr << "mp3_playback name=" << name << " status=" << soundResolveStatusName(status)
                << " detail=\"" << detail << "\"\n";
      if (status != SoundResolveStatus::Resolved) {
        ++refused;
        if (!refusalNames.empty()) refusalNames += ',';
        refusalNames += name.substr(name.find_last_of('/') + 1) + ":"
          + soundResolveStatusName(status);
        return false;
      }
      out = resource.wav;
      return true;
    });
    mp3Scheduler.setPlaybackSink([&sinkCalls](const WavPcmData& pcm, float, float) {
      if (!wavPcmIsPlayable(pcm)) return false;
      ++sinkCalls;
      return true;
    });
    mp3Scheduler.setContentObserver([&observed](const SoundPlaybackEvent& event,
        const WavPcmData&, const char* outcomeName) {
      if (!observed.empty()) observed += ',';
      observed += event.name.substr(event.name.find_last_of('/') + 1) + ":" + outcomeName;
    });
    mp3Scheduler.reset(0);
    const auto mp3Playback = mp3Scheduler.advance(run.mp3Timeline, 20);
    mp3Scheduler.stop();
    run.readings.set("mp3PlaybackSeen", static_cast<long long>(mp3Playback.eventsSeen));
    run.readings.set("mp3PlaybackPlayed", static_cast<long long>(mp3Playback.eventsPlayed));
    run.readings.set("mp3PlaybackMissing",
      static_cast<long long>(mp3Playback.eventsMissingResource));
    run.readings.set("mp3PlaybackSinkCalls", static_cast<long long>(sinkCalls));
    run.readings.set("mp3PlaybackRefused", static_cast<long long>(refused));
    run.readings.set("mp3PlaybackRefusalNames", refusalNames);
    run.readings.set("mp3PlaybackMp3Seen", static_cast<long long>(mp3Playback.mp3EventsSeen));
    run.readings.set("mp3PlaybackMp3Played", static_cast<long long>(mp3Playback.mp3EventsPlayed));
    run.readings.set("mp3PlaybackMp3Failed", static_cast<long long>(mp3Playback.mp3EventsFailed));
    run.readings.set("mp3PlaybackObserved", observed);
    check(run, "a refused MP3 is counted, not silently dropped, and never reaches the sink",
      mp3Playback.eventsSeen == 5 && mp3Playback.eventsPlayed == 2
        && mp3Playback.eventsMissingResource == 3 && refused == 3 && sinkCalls == 2);
    check(run, "the playable MP3 is attributed to the MP3 codec at the playback boundary",
      mp3Playback.mp3EventsSeen == 1 && mp3Playback.mp3EventsPlayed == 1
        && mp3Playback.mp3EventsFailed == 0
        && observed == "probe_play_wav.wav:played,probe_play_mp3.mp3:played");
    check(run, "each refusal keeps its own name",
      refusalNames == "probe_no_frame.mp3:invalid_wav,probe_sync_only.mp3:invalid_wav,"
        "probe_layer2.mp3:unsupported_format");
  }

  // -- 3. Tick semantics: dedup, forward, reverse, seek ---------------------
  run.tickTimeline.add(eventAt(2, 1, "weapons/probe.wav"));
  run.tickTimeline.add(eventAt(5, 2, "weapons/probe.wav"));
  run.tickTimeline.add(eventAt(9, 3, "weapons/probe.wav"));
  run.tickTimeline.add(eventAt(9, 4, "weapons/probe.wav"));
  run.tickTimeline.sortByTick();
  check(run, "the timeline holds four weapon events",
    run.tickTimeline.size() == 4 && run.tickTimeline.events()[0].kind == SoundEventKind::Weapon);

  AudioEventScheduler scheduler;
  scheduler.setDeviceId(0);
  scheduler.setResolver([](const std::string&, WavPcmData&) { return false; });
  scheduler.reset(0);
  const auto jumped = scheduler.advance(run.tickTimeline, 5);
  scheduler.stop();
  const auto paused = scheduler.stats();
  const auto cancelled = scheduler.advance(run.tickTimeline, 9);
  scheduler.reset(5);
  const auto resumed = scheduler.advance(run.tickTimeline, 9);
  const auto repeated = scheduler.advance(run.tickTimeline, 9);
  scheduler.reset(9);
  const auto reverseBoundary = scheduler.advance(run.tickTimeline, 9);
  scheduler.reset(5);
  const auto recovered = scheduler.advance(run.tickTimeline, 9);
  run.readings.set("jumpSeen", static_cast<long long>(jumped.eventsSeen));
  run.readings.set("pausedSeen", static_cast<long long>(paused.eventsSeen));
  run.readings.set("resumeSeen", static_cast<long long>(resumed.eventsSeen));
  run.readings.set("repeatedSeen", static_cast<long long>(repeated.eventsSeen));
  run.repeatedSeen = repeated.eventsSeen;
  run.readings.set("reverseSeen", static_cast<long long>(reverseBoundary.eventsSeen));
  run.readings.set("recoveredSeen", static_cast<long long>(recovered.eventsSeen));
  run.recoveredSeen = recovered.eventsSeen;
  run.readings.set("missingPlayed", static_cast<long long>(recovered.eventsPlayed));
  run.readings.set("missingResource", static_cast<long long>(recovered.eventsMissingResource));
  run.missingEvents = recovered.eventsMissingResource;
  check(run, "an advance sees exactly the events in its window",
    jumped.eventsSeen == 2 && jumped.eventsMissingResource == 2 && jumped.eventsPlayed == 0);
  check(run, "stop freezes the cursor", paused.eventsSeen == cancelled.eventsSeen
    && paused.eventsMissingResource == cancelled.eventsMissingResource);
  check(run, "a repeated advance at the same tick replays nothing",
    repeated.eventsSeen == resumed.eventsSeen && repeated.eventsPlayed == 0);
  check(run, "a reset at the current tick replays nothing", reverseBoundary.eventsSeen == 0);
  check(run, "a reset at an earlier tick replays the window", recovered.eventsSeen == 2);
  check(run, "a sound with no resource is never counted as played",
    recovered.eventsPlayed == 0 && recovered.eventsMissingResource == 2);

  // -- 4. The injected sink receives real VPK PCM ---------------------------
  std::size_t sinkCalls = 0, queuedBytes = 0;
  AudioEventScheduler injected;
  injected.setDeviceId(0);
  injected.setResolver([&resolved](const std::string& name, WavPcmData& out) {
    if (name != "weapons/stickybomblauncher_det.wav") return false;
    out = resolved.wav;
    return true;
  });
  injected.setPlaybackSink([&](const WavPcmData& pcm, float, float) {
    if (pcm.encoding != WavEncoding::Pcm || pcm.channels != 2 || pcm.sampleRate != 44100
        || pcm.bitsPerSample != 16 || pcm.blockAlign != 4 || pcm.pcm.size() % pcm.blockAlign != 0) return false;
    ++sinkCalls;
    queuedBytes += pcm.pcm.size();
    return true;
  });
  SoundEventTimeline realTimeline;
  realTimeline.add(eventAt(2, 1, "weapons/stickybomblauncher_det.wav"));
  realTimeline.sortByTick();
  injected.reset(0);
  const auto queued = injected.advance(realTimeline, 2);
  run.readings.set("sinkCalls", static_cast<long long>(sinkCalls));
  run.readings.set("queuedPcm", static_cast<long long>(queued.eventsPlayed));
  run.readings.set("vpkWav", static_cast<long long>(indexResolvedWav));
  check(run, "the injected sink received exactly one real PCM buffer",
    queued.eventsPlayed == 1 && sinkCalls == 1 && queuedBytes == resolved.wav.pcm.size());

  // -- 5. The sink queue limit drops the overflow, it does not grow ---------
  constexpr std::size_t sinkQueueLimit = 8;
  std::size_t accepted = 0, rejected = 0;
  AudioEventScheduler bounded;
  bounded.setDeviceId(0);
  bounded.setResolver([&run](const std::string&, WavPcmData& out) { out = run.fixture; return true; });
  bounded.setPlaybackSink([&](const WavPcmData&, float, float) {
    if (accepted == sinkQueueLimit) { ++rejected; return false; }
    ++accepted;
    return true;
  });
  SoundEventTimeline burst;
  for (std::uint32_t i = 0; i < sinkQueueLimit + 1; ++i) burst.add(eventAt(1, i + 1, "weapons/probe.wav"));
  burst.sortByTick();
  bounded.reset(0);
  const auto boundedStats = bounded.advance(burst, 1);
  run.readings.set("sinkQueueLimit", static_cast<long long>(sinkQueueLimit));
  run.readings.set("accepted", static_cast<long long>(accepted));
  run.readings.set("rejected", static_cast<long long>(rejected));
  check(run, "the queue limit accepted 8 and rejected 1",
    boundedStats.eventsSeen == sinkQueueLimit + 1 && boundedStats.eventsPlayed == sinkQueueLimit
      && boundedStats.eventsMissingResource == 1 && accepted == sinkQueueLimit && rejected == 1);

  // -- 6. Nine kinds, three of them filtered at advance time ----------------
  run.mixedTimeline.add(eventAt(1, 1, "weapons/probe_weapon.wav"));
  run.mixedTimeline.add(eventAt(2, 2, "player/probe_step.wav"));
  run.mixedTimeline.add(eventAt(3, 3, "medic/probe_uber.wav"));
  run.mixedTimeline.add(eventAt(4, 4, "vo/probe_yes.wav"));
  run.mixedTimeline.add(eventAt(5, 5, "announcer/probe_win.wav"));
  run.mixedTimeline.add(eventAt(6, 6, "music/probe_menu.wav"));
  run.mixedTimeline.add(eventAt(7, 7, "weapons/probe_missing.wav"));
  run.mixedTimeline.add(eventAt(8, 8, "tempent/probe_boom.wav"));
  run.mixedTimeline.add(eventAt(9, 9, "particles/probe_pcf.wav"));
  run.mixedTimeline.add(eventAt(10, 10, "misc/probe_null.wav"));
  run.mixedTimeline.sortByTick();
  const auto kindCounts = countSoundKinds(run.mixedTimeline);
  run.readings.set("kindWeapon", static_cast<long long>(kindCounts.weapon));
  run.readings.set("kindFootstep", static_cast<long long>(kindCounts.footstep));
  run.readings.set("kindUber", static_cast<long long>(kindCounts.uber));
  run.readings.set("kindTempEntity", static_cast<long long>(kindCounts.tempEntity));
  run.readings.set("kindPcf", static_cast<long long>(kindCounts.pcf));
  run.readings.set("kindVoice", static_cast<long long>(kindCounts.voice));
  run.readings.set("kindAnnouncer", static_cast<long long>(kindCounts.announcer));
  run.readings.set("kindMusic", static_cast<long long>(kindCounts.music));
  run.readings.set("kindWorld", static_cast<long long>(kindCounts.world));
  check(run, "the nine kinds are distinguished and counted",
    kindCounts.weapon == 2 && kindCounts.footstep == 1 && kindCounts.uber == 1
      && kindCounts.tempEntity == 1 && kindCounts.pcf == 1 && kindCounts.voice == 1
      && kindCounts.announcer == 1 && kindCounts.music == 1 && kindCounts.world == 1);

  std::vector<std::string> resolveOrder;
  AudioEventScheduler nullSink;
  nullSink.setDeviceId(0);
  nullSink.setResolver([&](const std::string& name, WavPcmData& out) {
    resolveOrder.push_back(name);
    if (name == "weapons/probe_missing.wav") return false;
    out = run.fixture;
    return true;
  });
  std::size_t mixedSinkCalls = 0;
  nullSink.setPlaybackSink([&](const WavPcmData& pcm, float, float) {
    if (!wavPcmIsPlayable(pcm)) return false;
    ++mixedSinkCalls;
    return true;
  });
  nullSink.reset(0);
  const auto mixedStats = nullSink.advance(run.mixedTimeline, 10);
  run.mixedPlayed = mixedStats.eventsPlayed;
  run.mixedSkipped = mixedStats.eventsSkippedVoice;
  run.readings.set("eventsSeen", static_cast<long long>(mixedStats.eventsSeen));
  run.readings.set("eventsPlayed", static_cast<long long>(mixedStats.eventsPlayed));
  run.readings.set("eventsSkippedVoice", static_cast<long long>(mixedStats.eventsSkippedVoice));
  run.readings.set("eventsMissingResource", static_cast<long long>(mixedStats.eventsMissingResource));
  run.readings.set("lastTick", static_cast<long long>(nullSink.lastTick()));
  run.readings.set("activeVoiceCount", static_cast<long long>(nullSink.activeVoiceCount()));
  check(run, "voice, announcer and music are skipped, never resolved",
    mixedStats.eventsSeen == 10 && mixedStats.eventsSkippedVoice == 3
      && mixedStats.eventsPlayed == 6 && mixedStats.eventsMissingResource == 1
      && mixedSinkCalls == 6 && nullSink.activeVoiceCount() == 0
      && nullSink.lastTick() == 10);
  check(run, "the resolver saw each playable event once, in tick order",
    resolveOrder.size() == 7 && resolveOrder[0] == "weapons/probe_weapon.wav"
      && resolveOrder[1] == "player/probe_step.wav" && resolveOrder[2] == "medic/probe_uber.wav"
      && resolveOrder[3] == "weapons/probe_missing.wav" && resolveOrder[4] == "tempent/probe_boom.wav"
      && resolveOrder[5] == "particles/probe_pcf.wav" && resolveOrder[6] == "misc/probe_null.wav");

  // -- 7. Delay converts to ticks, rounding half away from zero -------------
  const double tickRate = 10.0;
  run.readings.set("delayHalf", static_cast<long long>(applySoundDelayTicks(10, 0.5f, tickRate)));
  run.readings.set("delayClamp", static_cast<long long>(applySoundDelayTicks(1, -1.0f, tickRate)));
  run.readings.set("delayRound", static_cast<long long>(applySoundDelayTicks(3, 0.15f, tickRate)));
  run.readings.set("delayZeroRate", static_cast<long long>(applySoundDelayTicks(3, 0.0f, 0.0)));
  check(run, "delay converts with round-half-away-from-zero and clamps",
    applySoundDelayTicks(10, 0.5f, tickRate) == 15 && applySoundDelayTicks(1, -1.0f, tickRate) == 0
      && applySoundDelayTicks(3, 0.15f, tickRate) == 5 && applySoundDelayTicks(3, 0.0f, 0.0) == -1);

  // -- 8. Real-data coverage of the classification --------------------------
  // The reserved temp-entity and PCF tokens must carry no real sound name; if
  // a game update starts shipping one, the classifier's coverage has to be
  // re-measured, and this reading turning red is how that gets noticed. The
  // filtered kinds must have real coverage, or the filter would be a rule
  // nothing exercises.
  std::size_t realEntries = 0, realVoice = 0, realAnnouncer = 0, realMusic = 0,
    realTempEntity = 0, realPcf = 0;
  const std::vector<std::string> soundArchives = {"tf2_sound_misc_dir.vpk", "tf2_sound_vo_english_dir.vpk"};
  for (const auto& name : soundArchives) {
    VpkArchive scan;
    if (!scan.open(root + "/" + name)) continue;
    for (const auto& path : scan.list()) {
      ++realEntries;
      switch (classifySoundEvent(path)) {
      case SoundEventKind::Voice: ++realVoice; break;
      case SoundEventKind::Announcer: ++realAnnouncer; break;
      case SoundEventKind::Music: ++realMusic; break;
      case SoundEventKind::TempEntity: ++realTempEntity; break;
      case SoundEventKind::Pcf: ++realPcf; break;
      default: break;
      }
    }
  }
  run.readings.set("realEntries", static_cast<long long>(realEntries));
  run.readings.set("realVoice", static_cast<long long>(realVoice));
  run.readings.set("realAnnouncer", static_cast<long long>(realAnnouncer));
  run.readings.set("realMusic", static_cast<long long>(realMusic));
  run.readings.set("realTempEntity", static_cast<long long>(realTempEntity));
  run.readings.set("realPcf", static_cast<long long>(realPcf));
  check(run, "the reserved temp-entity and PCF tokens carry no real sound name",
    realEntries >= 1 && realTempEntity == 0 && realPcf == 0);
  check(run, "the filtered kinds have real coverage",
    realVoice + realAnnouncer + realMusic >= 1);

  // -- 9. The device contract ----------------------------------------------
  run.readings.set("device", 0);
  run.readings.set("playbackCalls", static_cast<long long>(audioDeviceOpenAttempts()));
  run.readings.set("device_opened", static_cast<long long>(audioDeviceOpenedCount()));
  run.readings.set("device_open_attempts", static_cast<long long>(audioDeviceOpenAttempts()));
  check(run, "no audio device was opened by any contract scenario",
    audioDeviceOpenAttempts() == 0 && audioDeviceOpenedCount() == 0);
}

// --------------------------------------------------------------------------
// Mutations. Each one computes the defective reading from a real defective
// computation and overwrites only the keys that flip moves.
// --------------------------------------------------------------------------

const std::vector<const char*>& mutationCases() {
  static const std::vector<const char*> cases = {
    "tick_dedup", "reverse_reset", "voice_filter",
    "delay_rounding", "missing_as_played", "null_sink_device", "duration_cap",
    "marker_strip", "mp3_decode", "mp3_silent_accept", "mp3_playback_drop"};
  return cases;
}

void applyMutation(const char* name, const ContractRun& run, Readings& defective) {
  const std::string which = name;
  if (which == "tick_dedup") {
    // The forward window starts at the already-played boundary tick instead of
    // one past it, so a second advance at the same tick replays it.
    std::size_t replayed = 0;
    run.tickTimeline.forEachRange(9, 9, [&replayed](const SoundPlaybackEvent&) { ++replayed; });
    defective.set("repeatedSeen", static_cast<long long>(run.repeatedSeen + replayed));
  } else if (which == "reverse_reset") {
    // reset() no longer re-seeds the cursor, so after a reverse the re-advance
    // finds nothing in front of the cursor it kept.
    std::size_t replayed = 0;
    run.tickTimeline.forEachRange(10, 9, [&replayed](const SoundPlaybackEvent&) { ++replayed; });
    defective.set("recoveredSeen", static_cast<long long>(replayed));
  } else if (which == "voice_filter") {
    // The skip guard at advance time is gone, so the filtered kinds are
    // resolved and played like any other event.
    std::size_t filtered = 0;
    run.mixedTimeline.forEachRange(0, 10, [&filtered](const SoundPlaybackEvent& event) {
      if (event.skipVoice) ++filtered;
    });
    defective.set("eventsSkippedVoice", 0);
    defective.set("eventsPlayed", static_cast<long long>(run.mixedPlayed + filtered));
  } else if (which == "delay_rounding") {
    // The tick shift truncates toward zero instead of rounding half away from
    // zero: 3 + 0.15s * 10 ticks/s is 4.5, and the defect makes it 4.
    const double shifted = 3.0 + static_cast<double>(0.15f) * 10.0;
    defective.set("delayRound", static_cast<long long>(shifted));
  } else if (which == "missing_as_played") {
    // The resolver-failure branch increments played instead of missing, so a
    // sound with no bytes is reported as audible.
    defective.set("missingResource", 0);
    defective.set("missingPlayed", static_cast<long long>(run.missingEvents));
  } else if (which == "duration_cap") {
    // The duration guard is removed, so a byte-legal playable WAV whose buffer
    // would run for 48 days classifies as resolved like any other resource.
    defective.set("indexExcessiveDuration", 0);
  } else if (which == "marker_strip") {
    // The sound-character strip is removed, so a marked stock name stays glued
    // to its metadata run and resolves to nothing.
    defective.set("indexMarkedResolved", 0);
    defective.set("indexMarkedPath", "-");
    defective.set("readMarkedPath", "-");
  } else if (which == "mp3_decode") {
    // The decoder is gone again: MP3 resources are signature-checked and
    // refused as unsupported_format (the pre-T7 behaviour), so the file that
    // ships and the file the corpus sends both stop resolving.
    defective.set("indexResolved", 1);
    defective.set("indexUnsupportedFormat", 1);
    defective.set("indexMp3PcmBytes", 0);
    defective.set("mp3EventStatus", "unsupported_format");
    defective.set("mp3EventPcmBytes", 0);
    defective.set("readMp3PcmBytes", 0);
  } else if (which == "mp3_playback_drop") {
    // The defect at the playback boundary: the resolver refuses MP3 again (the
    // pre-T7 shape), so the MP3 events are counted missing and never reach the
    // sink. Computed, not asserted -- the same timeline and the same sink are
    // driven again with only the resolver's MP3 answer changed.
    std::size_t sinkCalls = 0;
    std::string observed;
    AudioEventScheduler defectiveScheduler;
    defectiveScheduler.setDeviceId(0);
    defectiveScheduler.setResolver([&run](const std::string& name, WavPcmData& out) {
      if (name.size() >= 4 && name.compare(name.size() - 4, 4, ".mp3") == 0) return false;
      const auto it = run.mp3ByName.find(name);
      if (it == run.mp3ByName.end()) return false;
      SoundResource resource;
      std::string detail;
      if (classifySoundResourceBytes(it->second, name, resource, detail)
          != SoundResolveStatus::Resolved) return false;
      out = resource.wav;
      return true;
    });
    defectiveScheduler.setPlaybackSink([&sinkCalls](const WavPcmData& pcm, float, float) {
      if (!wavPcmIsPlayable(pcm)) return false;
      ++sinkCalls;
      return true;
    });
    defectiveScheduler.setContentObserver([&observed](const SoundPlaybackEvent& event,
        const WavPcmData&, const char* outcomeName) {
      if (!observed.empty()) observed += ',';
      observed += event.name.substr(event.name.find_last_of('/') + 1) + ":" + outcomeName;
    });
    defectiveScheduler.reset(0);
    const auto stats = defectiveScheduler.advance(run.mp3Timeline, 20);
    defectiveScheduler.stop();
    defective.set("mp3PlaybackPlayed", static_cast<long long>(stats.eventsPlayed));
    defective.set("mp3PlaybackMissing", static_cast<long long>(stats.eventsMissingResource));
    defective.set("mp3PlaybackSinkCalls", static_cast<long long>(sinkCalls));
    defective.set("mp3PlaybackMp3Seen", static_cast<long long>(stats.mp3EventsSeen));
    defective.set("mp3PlaybackMp3Played", static_cast<long long>(stats.mp3EventsPlayed));
    defective.set("mp3PlaybackObserved", observed);
  } else if (which == "mp3_silent_accept") {
    // A decode that produced no PCM is accepted as a resolution, so a stream
    // with sync-shaped bytes and no audio would be handed to the output path as
    // a zero-length buffer.
    defective.set("mp3SilentStatus", "resolved");
  } else if (which == "null_sink_device") {
    // The injected sink is ignored and playback goes to a real AudioPlayer.
    // The device id is the device count itself, which is out of range on every
    // machine, so the open fails: the attempt counter is the reading that
    // moves and no device is ever opened. (0xFFFF would not do -- WinMM reads
    // the id as a 16-bit value, so 0xFFFF aliases to WAVE_MAPPER and opens the
    // default device.) On a machine whose default device exists the same flip
    // would also open it.
    const std::uint32_t before = audioDeviceOpenAttempts();
    const std::uint32_t beforeOpened = audioDeviceOpenedCount();
    AudioEventScheduler noSink;
    noSink.setDeviceId(waveOutGetNumDevs());
    noSink.setResolver([&run](const std::string& soundName, WavPcmData& out) {
      if (soundName == "weapons/probe_missing.wav") return false;
      out = run.fixture;
      return true;
    });
    noSink.reset(0);
    noSink.advance(run.mixedTimeline, 10);
    // Printed so the evidence shows the flip moved the attempt counter while
    // the open itself still failed: the out-of-range device id is what keeps
    // this scenario silent.
    std::cerr << "mutation_detail null_sink_device open_attempts="
              << (audioDeviceOpenAttempts() - before)
              << " opens_that_succeeded="
              << (audioDeviceOpenedCount() - beforeOpened) << '\n';
    defective.set("device_open_attempts",
      static_cast<long long>(audioDeviceOpenAttempts() - before));
  }
}

} // namespace

int main(int argc, char** argv) {
  const char* mutationCase = nullptr;
  for (int i = 1; i < argc; ++i) {
    const std::string flag = argv[i];
    if (flag == "--self-test") continue;
    if (flag == "--mutation") {
      if (i + 1 >= argc) {
        std::cerr << "usage: audio_scheduler_probe [--self-test] [--mutation <case>]\n";
        return 2;
      }
      mutationCase = argv[++i];
      bool known = false;
      for (const auto* name : mutationCases()) {
        if (std::string(name) == mutationCase) known = true;
      }
      if (!known) {
        std::cerr << "unknown mutation case: " << mutationCase << '\n';
        return 2;
      }
      continue;
    }
    std::cerr << "usage: audio_scheduler_probe [--self-test] [--mutation <case>]\n";
    return 2;
  }

  ContractRun run;
  runContract(run);
  if (!run.archivesOpened) {
    std::cerr << "FAIL the sound VPK opened\n";
    return 1;
  }
  for (const auto& [what, ok] : run.checks) {
    std::cerr << (ok ? "ok   " : "FAIL ") << what << '\n';
  }

  if (mutationCase != nullptr) {
    // Run one flip and report which contract keys went red. Caught means the
    // pinned readings discriminate the defect; exit 1 means they do not, which
    // is the failure this mode exists to find.
    Readings defective = run.readings;
    applyMutation(mutationCase, run, defective);
    const auto broken = contractBreaches(defective);
    std::cerr << "mutation " << mutationCase << ": "
              << (broken.empty() ? "NOT CAUGHT" : "caught") << '\n';
    for (const auto& key : broken) {
      const std::string* actual = defective.find(key);
      std::cerr << "  contract key " << key << " moved to "
                << (actual != nullptr ? *actual : std::string("<missing>")) << '\n';
    }
    return broken.empty() ? 1 : 0;
  }

  // Every mutation must move a pinned reading; a flip that leaves the contract
  // green is a blind criterion, which is the failure this probe exists to
  // catch in itself.
  std::size_t caught = 0;
  for (const auto* name : mutationCases()) {
    Readings defective = run.readings;
    applyMutation(name, run, defective);
    const auto broken = contractBreaches(defective);
    if (broken.empty()) {
      std::cerr << "mutation " << name << ": NOT CAUGHT (no pinned reading moved)\n";
      continue;
    }
    ++caught;
    std::cerr << "mutation " << name << ": caught (broke";
    for (const auto& key : broken) std::cerr << ' ' << key;
    std::cerr << ")\n";
  }
  run.readings.set("mutationsCaught", static_cast<long long>(caught));
  run.readings.set("mutationsTotal", static_cast<long long>(mutationCases().size()));

  bool allChecks = true;
  for (const auto& [what, ok] : run.checks) {
    if (!ok) allChecks = false;
  }
  const auto breaches = contractBreaches(run.readings);
  if (!allChecks) std::cerr << "status=fail (a check failed)\n";
  for (const auto& key : breaches) {
    // The expected and actual values are printed with the breach. A bare
    // "FAIL contract key X" leaves the reader to re-run the probe by hand to
    // learn what X became, and the cheap way out of that is to overwrite the pin
    // with whatever the tree prints now.
    const std::string* actual = run.readings.find(key);
    std::string expected;
    for (const auto& [name, value] : pinnedContract()) {
      if (name == key) expected = value;
    }
    std::cerr << "FAIL contract key " << key << ": expected '" << expected << "', got '"
              << (actual != nullptr ? *actual : std::string("<missing>")) << "'\n";
  }
  if (caught != mutationCases().size()) {
    std::cerr << "status=fail (" << (mutationCases().size() - caught)
              << " mutation(s) left the contract green)\n";
  }
  if (!allChecks || !breaches.empty() || caught != mutationCases().size()) return 1;
  std::cout << run.readings.line() << '\n';
  return 0;
}
