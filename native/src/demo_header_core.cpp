#include "demo_header.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <queue>
#include <chrono>
#include <unordered_map>
#include <unordered_set>

#include "demo_header_internal.h"

namespace tf2::native {

namespace {

template <typename T>
T readValue(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
  T value{};
  std::memcpy(&value, bytes.data() + offset, sizeof(T));
  return value;
}

std::string fixedString(const std::vector<std::uint8_t>& bytes, std::size_t offset, std::size_t length) {
  std::size_t end = 0;
  while (end < length && bytes[offset + end] != 0) ++end;
  std::string value(reinterpret_cast<const char*>(bytes.data() + offset), end);
  for (char& character : value) {
    const unsigned char byte = static_cast<unsigned char>(character);
    if (byte < 0x20 || byte > 0x7e) character = '_';
  }
  return value;
}

bool containsInsensitive(std::string value, const char* needle) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  std::string wanted(needle);
  std::transform(wanted.begin(), wanted.end(), wanted.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return value.find(wanted) != std::string::npos;
}

bool validMapName(const std::string& value) {
  if (value.empty() || value.size() > 128) return false;
  return std::all_of(value.begin(), value.end(), [](unsigned char c) {
    return std::isalnum(c) || c == '_' || c == '-';
  });
}
} // namespace

// src/public/demofile/demoformat.h (Valve) documents the two name fields as:
//   char servername[MAX_OSPATH];  // Name of server
//   char clientname[MAX_OSPATH];  // Name of client who recorded the game
// A SourceTV recording is written by the server's SourceTV client, so the
// recorder's name lands in clientname: the real match demos in this corpus carry
// clientname="SourceTV Demo" while servername is an ordinary hostname
// ("Matcha Bookable", "Spire Server", "na.serveme.tf #633053"). Checking only
// servername labelled all of those POV, which is how five SourceTV demos were
// reported as POV in the P0 acceptance run.
bool namesIndicateSourceTv(const DemoHeader& header) {
  return containsInsensitive(header.serverName, "sourcetv")
      || containsInsensitive(header.serverName, "hltv")
      || containsInsensitive(header.clientName, "sourcetv")
      || containsInsensitive(header.clientName, "hltv");
}
namespace {

} // namespace

bool parseDemoHeader(const std::vector<std::uint8_t>& bytes, DemoHeader& header) {
  header = {};
  constexpr std::size_t kHeaderSize = 1072;
  if (bytes.size() < kHeaderSize) { header.error = "demo header is truncated"; return false; }
  if (std::memcmp(bytes.data(), "HL2DEMO\0", 8) != 0) { header.error = "missing HL2DEMO signature"; return false; }
  header.demoProtocol = readValue<std::int32_t>(bytes, 8);
  header.networkProtocol = readValue<std::int32_t>(bytes, 12);
  header.serverName = fixedString(bytes, 16, 260);
  header.clientName = fixedString(bytes, 276, 260);
  header.mapName = fixedString(bytes, 536, 260);
  header.gameDirectory = fixedString(bytes, 796, 260);
  header.playbackTime = readValue<float>(bytes, 1056);
  header.ticks = readValue<std::int32_t>(bytes, 1060);
  header.frames = readValue<std::int32_t>(bytes, 1064);
  if (!validMapName(header.mapName)) { header.error = "demo map name is invalid"; return false; }
  if (header.ticks < 0 || header.frames < 0 || header.playbackTime < 0.0f) { header.error = "demo timing fields are invalid"; return false; }
  if (namesIndicateSourceTv(header)) {
    header.recordingType = DemoRecordingType::SourceTv;
  } else if (!header.clientName.empty()) {
    header.recordingType = DemoRecordingType::PovHeuristic;
  }
  header.valid = true;
  return true;
}

bool indexDemoCommands(const std::vector<std::uint8_t>& bytes, const DemoHeader& header, DemoIndex& index) {
  index = {};
  if (!header.valid) { index.error = "cannot index invalid demo header"; return false; }
  constexpr std::size_t kHeaderSize = 1072;
  constexpr std::size_t kPacketInfo = 76;
  std::size_t cursor = kHeaderSize;
  auto readI32 = [&](std::size_t offset, std::int32_t& value) {
    if (offset + sizeof(value) > bytes.size()) return false;
    std::memcpy(&value, bytes.data() + offset, sizeof(value));
    return true;
  };
  while (cursor < bytes.size()) {
    const std::size_t commandOffset = cursor;
    if (bytes.size() - cursor < 1 + sizeof(std::int32_t)
        && !(bytes.size() - cursor == 4 && bytes[cursor] == 7)) {
      index.malformedOffset = cursor; index.error = "truncated command prefix"; return false;
    }
    const std::uint8_t command = bytes[cursor++];
    std::int32_t tick = 0;
    if (command == 7 && bytes.size() - cursor == 3) {
      tick = static_cast<std::int32_t>(bytes[cursor])
        | (static_cast<std::int32_t>(bytes[cursor + 1]) << 8)
        | (static_cast<std::int32_t>(bytes[cursor + 2]) << 16);
      cursor += 3;
    } else {
      if (!readI32(cursor, tick)) { index.malformedOffset = commandOffset; index.error = "truncated command tick"; return false; }
      cursor += sizeof(tick);
    }
    // Protocol 4 adds a player slot to commands that carry one. DEM_STOP is
    // the exception: real TF2 files end with command + tick only.
    if (header.demoProtocol >= 4 && command != 7) cursor += 1;
    std::size_t payloadOffset = cursor;
    std::size_t payloadSize = 0;
    if (command == 1 || command == 2) {
      if (cursor + kPacketInfo + 12 > bytes.size()) { index.malformedOffset = commandOffset; index.error = "truncated packet header"; return false; }
      cursor += kPacketInfo + 8;
      std::int32_t length = 0;
      if (!readI32(cursor, length) || length < 0) { index.malformedOffset = commandOffset; index.error = "invalid packet length"; return false; }
      cursor += 4; payloadOffset = cursor; payloadSize = static_cast<std::size_t>(length);
      ++index.packetCount;
    } else if (command == 3 || command == 7) {
      payloadOffset = cursor;
      payloadSize = 0;
    } else if (command == 4 || command == 6 || command == 8) {
      std::int32_t length = 0;
      if (!readI32(cursor, length) || length < 0) { index.malformedOffset = commandOffset; index.error = "invalid length-prefixed command"; return false; }
      cursor += 4; payloadOffset = cursor; payloadSize = static_cast<std::size_t>(length);
    } else if (command == 5) {
      if (cursor + 4 > bytes.size()) { index.malformedOffset = commandOffset; index.error = "truncated user command sequence"; return false; }
      cursor += 4;
      std::int32_t length = 0;
      if (!readI32(cursor, length) || length < 0) { index.malformedOffset = commandOffset; index.error = "invalid user command length"; return false; }
      cursor += 4; payloadOffset = cursor; payloadSize = static_cast<std::size_t>(length);
    } else {
      index.malformedOffset = commandOffset; index.error = "unknown demo command"; return false;
    }
    if (payloadSize > bytes.size() - cursor) { index.malformedOffset = commandOffset; index.error = "command payload exceeds file"; return false; }
    index.entries.push_back({tick, command, commandOffset, payloadOffset, payloadSize});
    ++index.commandCount;
    cursor += payloadSize;
    if (command == 7) break;
  }
  index.valid = !index.entries.empty() && cursor <= bytes.size();
  if (!index.valid && index.error.empty()) index.error = "demo contains no commands";
  return index.valid;
}

bool readExact(std::ifstream& file, void* destination, std::size_t size) {
  return size == 0 || static_cast<bool>(file.read(static_cast<char*>(destination), static_cast<std::streamsize>(size)));
}
namespace {

bool readI32Stream(std::ifstream& file, std::int32_t& value) { return readExact(file, &value, sizeof(value)); }
} // namespace

bool readEntryPayload(std::ifstream& file, const DemoIndexEntry& entry,
    std::vector<std::uint8_t>& payload, std::size_t maxBytes) {
  payload.clear();
  if (entry.payloadSize == 0 || entry.payloadSize > maxBytes
      || entry.payloadOffset > std::numeric_limits<std::size_t>::max() - entry.payloadSize
      || entry.payloadSize > static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max())) return false;
  file.seekg(static_cast<std::streamoff>(entry.payloadOffset), std::ios::beg);
  if (!file) return false;
  payload.resize(entry.payloadSize);
  if (!readExact(file, payload.data(), payload.size())) { payload.clear(); return false; }
  return true;
}

bool parseDemoHeaderFile(const std::filesystem::path& path, DemoHeader& header) {
  std::ifstream file(path, std::ios::binary);
  if (!file) { header = {}; header.error = "demo file cannot be opened"; return false; }
  std::vector<std::uint8_t> bytes(1072);
  if (!readExact(file, bytes.data(), bytes.size())) { header = {}; header.error = "demo header is truncated"; return false; }
  return parseDemoHeader(bytes, header);
}

bool findDemoPacketAtOrBeforeTick(const DemoIndex& index, std::int32_t tick, DemoIndexEntry& entry) {
  if (index.entries.empty()) return false;
  const auto firstPacket = std::find_if(index.entries.begin(), index.entries.end(),
    [](const DemoIndexEntry& candidate) { return candidate.command == 1 || candidate.command == 2; });
  if (firstPacket == index.entries.end()) return false;
  const std::int64_t absoluteTick = static_cast<std::int64_t>(firstPacket->tick) + tick;
  bool found = false;
  for (const auto& candidate : index.entries) {
    if ((candidate.command != 1 && candidate.command != 2)
        || static_cast<std::int64_t>(candidate.tick) > absoluteTick) continue;
    if (!found || candidate.tick > entry.tick
        || (candidate.tick == entry.tick && candidate.offset > entry.offset)) {
      entry = candidate;
      found = true;
    }
  }
  return found;
}
bool readDemoEntryPayload(const std::filesystem::path& path, const DemoIndexEntry& entry,
    std::vector<std::uint8_t>& payload, std::size_t maxBytes) {
  std::ifstream file(path, std::ios::binary);
  return file && readEntryPayload(file, entry, payload, maxBytes);
}
bool indexDemoFile(const std::filesystem::path& path, const DemoHeader& header, DemoIndex& index) {
  index = {};
  if (!header.valid) { index.error = "cannot index invalid demo header"; return false; }
  std::ifstream file(path, std::ios::binary);
  if (!file) { index.error = "demo file cannot be opened"; return false; }
  file.seekg(0, std::ios::end);
  const auto end = file.tellg();
  if (end < 1072) { index.error = "demo header is truncated"; return false; }
  const std::uint64_t fileSize = static_cast<std::uint64_t>(end);
  file.seekg(1072, std::ios::beg);
  const std::size_t prefixSize = header.demoProtocol >= 4 ? 6 : 5;
  constexpr std::size_t packetInfoSize = 76;
  std::uint64_t cursor = 1072;
  while (cursor < fileSize) {
    const std::uint64_t commandOffset = cursor;
    std::uint8_t command = 0; std::int32_t tick = 0;
    if (!readExact(file, &command, 1)) { index.malformedOffset = static_cast<std::size_t>(cursor); index.error = "truncated command prefix"; return false; }
    ++cursor;
    if (command == 7 && fileSize - cursor == 3) {
      std::uint8_t tickBytes[3]{};
      if (!readExact(file, tickBytes, sizeof(tickBytes))) { index.malformedOffset = static_cast<std::size_t>(commandOffset); index.error = "truncated command tick"; return false; }
      tick = static_cast<std::int32_t>(tickBytes[0])
        | (static_cast<std::int32_t>(tickBytes[1]) << 8)
        | (static_cast<std::int32_t>(tickBytes[2]) << 16);
      cursor += 3;
    } else {
      if (!readI32Stream(file, tick)) { index.malformedOffset = static_cast<std::size_t>(cursor); index.error = "truncated command prefix"; return false; }
      cursor += 4;
    }
    if (prefixSize == 6 && command != 7) { std::uint8_t slot = 0; if (!readExact(file, &slot, 1)) { index.malformedOffset = static_cast<std::size_t>(commandOffset); index.error = "truncated player slot"; return false; } ++cursor; }
    std::uint64_t payloadOffset = cursor; std::uint64_t payloadSize = 0;
    if (command == 1 || command == 2) {
      if (cursor + packetInfoSize + 12 > fileSize) { index.malformedOffset = static_cast<std::size_t>(commandOffset); index.error = "truncated packet header"; return false; }
      file.seekg(static_cast<std::streamoff>(packetInfoSize + 8), std::ios::cur); cursor += packetInfoSize + 8;
      std::int32_t length = 0; if (!readI32Stream(file, length) || length < 0) { index.malformedOffset = static_cast<std::size_t>(commandOffset); index.error = "invalid packet length"; return false; }
      cursor += 4; payloadOffset = cursor; payloadSize = static_cast<std::uint64_t>(length); ++index.packetCount;
    } else if (command == 3 || command == 7) {
      payloadOffset = cursor;
    } else if (command == 4 || command == 6 || command == 8) {
      std::int32_t length = 0; if (!readI32Stream(file, length) || length < 0) { index.malformedOffset = static_cast<std::size_t>(commandOffset); index.error = "invalid length-prefixed command"; return false; }
      cursor += 4; payloadOffset = cursor; payloadSize = static_cast<std::uint64_t>(length);
    } else if (command == 5) {
      std::int32_t sequence = 0; if (!readI32Stream(file, sequence)) { index.malformedOffset = static_cast<std::size_t>(commandOffset); index.error = "truncated user command sequence"; return false; }
      cursor += 4; std::int32_t length = 0; if (!readI32Stream(file, length) || length < 0) { index.malformedOffset = static_cast<std::size_t>(commandOffset); index.error = "invalid user command length"; return false; }
      cursor += 4; payloadOffset = cursor; payloadSize = static_cast<std::uint64_t>(length);
    } else { index.malformedOffset = static_cast<std::size_t>(commandOffset); index.error = "unknown demo command"; return false; }
    if (payloadSize > fileSize - cursor) { index.malformedOffset = static_cast<std::size_t>(commandOffset); index.error = "command payload exceeds file"; return false; }
    index.entries.push_back({tick, command, static_cast<std::size_t>(commandOffset), static_cast<std::size_t>(payloadOffset), static_cast<std::size_t>(payloadSize)});
    ++index.commandCount; cursor += payloadSize;
    file.seekg(static_cast<std::streamoff>(cursor), std::ios::beg);
    if (command == 7) break;
  }
  index.valid = !index.entries.empty();
  if (!index.valid && index.error.empty()) index.error = "demo contains no commands";
  return index.valid;
}

std::int32_t firstRecordedPacketTick(const DemoIndex& index, bool& found) {
  for (const auto& entry : index.entries) {
    if (entry.command == 2) { found = true; return entry.tick; }
  }
  found = false;
  return 0;
}

double demoTickRate(const DemoHeader& header) {
  // The bounds are the ones the player has always used: 30 is the slowest
  // server tick rate in the shipped game, 128 the fastest, and a header whose
  // playbackTime or ticks is unusable falls back rather than dividing by zero.
  if (header.playbackTime > 0.0f && header.ticks > 0) {
    return std::clamp(static_cast<double>(header.ticks)
      / static_cast<double>(header.playbackTime), 30.0, 128.0);
  }
  return 66.6666667;
}

const char* demoRecordingTypeName(DemoRecordingType type) {
  switch (type) {
    case DemoRecordingType::SourceTv: return "SourceTV";
    case DemoRecordingType::PovHeuristic: return "POV (heuristic)";
    default: return "unknown";
  }
}

} // namespace tf2::native
