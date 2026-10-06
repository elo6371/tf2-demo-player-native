#include "demo_header.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <limits>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <unordered_map>
#include <unordered_set>

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
  if (containsInsensitive(header.serverName, "sourcetv") || containsInsensitive(header.serverName, "hltv")) {
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

namespace {
bool readExact(std::ifstream& file, void* destination, std::size_t size) {
  return size == 0 || static_cast<bool>(file.read(static_cast<char*>(destination), static_cast<std::streamsize>(size)));
}

bool readI32Stream(std::ifstream& file, std::int32_t& value) { return readExact(file, &value, sizeof(value)); }

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
}bool readDemoEntryPayload(const std::filesystem::path& path, const DemoIndexEntry& entry,
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


namespace {
class MessageBits {
public:
  explicit MessageBits(const std::vector<std::uint8_t>& bytes) : bytes_(bytes) {}
  std::size_t offsetBits() const { return offset_; }
  std::size_t remaining() const { return bytes_.size() * 8 - offset_; }
  void setMinimumRemaining(std::size_t minimum) { minimumRemaining_ = std::min(minimum, bytes_.size() * 8); }
  bool read(std::uint32_t count, std::uint32_t& value) {
    if (count > 32 || count > remaining() || remaining() - count < minimumRemaining_) return false;
    value = 0;
    for (std::uint32_t i = 0; i < count; ++i) { const std::size_t bit = offset_ + i; value |= static_cast<std::uint32_t>((bytes_[bit / 8] >> (bit % 8)) & 1u) << i; }
    offset_ += count; return true;
  }
  bool readString(std::string& value, std::size_t maxLength = 1024) {
    value.clear();
    for (std::size_t i = 0; i < maxLength; ++i) { std::uint32_t byte = 0; if (!read(8, byte)) return false; if (byte == 0) return true; value.push_back(static_cast<char>(byte)); }
    return false;
  }
  bool skip(std::size_t count) { if (count > remaining() || remaining() - count < minimumRemaining_) return false; offset_ += count; return true; }
  void alignToByte() { offset_ = (offset_ + 7u) & ~std::size_t(7u); }
  bool readVarInt(std::uint32_t& value) {
    value = 0; for (std::uint32_t shift = 0; shift < 32; shift += 7) { std::uint32_t byte = 0; if (!read(8, byte)) return false; value |= (byte & 0x7fu) << shift; if ((byte & 0x80u) == 0) return true; } return false;
  }
  bool readBitVar(std::uint32_t& value) {
    std::uint32_t kind = 0; if (!read(2, kind)) return false;
    const std::uint32_t width = kind == 0 ? 4u : (kind == 1 ? 8u : (kind == 2 ? 12u : 32u));
    return read(width, value);
  }
  bool readStringLimit(std::string& value, std::size_t limit) {
    value.clear();
    bool terminated = false;
    for (std::size_t i = 0; i < limit; ++i) { std::uint32_t byte = 0; if (!read(8, byte)) return false; if (!terminated && byte == 0) terminated = true; else if (!terminated) value.push_back(static_cast<char>(byte)); }
    return true;
  }
private:
  const std::vector<std::uint8_t>& bytes_; std::size_t offset_ = 0; std::size_t minimumRemaining_ = 0;
};

bool readVoiceInit(MessageBits& bits, DemoNetworkSummary& summary) {
  std::string codec;
  std::uint32_t quality = 0;
  std::uint32_t sampleRate = 0;
  if (!bits.readString(codec) || !bits.read(8, quality)) return false;
  if (quality == 255 && !bits.read(16, sampleRate)) return false;
  ++summary.voiceInitCount;
  return true;
}

bool readVoiceData(MessageBits& bits, DemoNetworkSummary& summary) {
  std::uint32_t fromClient = 0;
  std::uint32_t proximity = 0;
  std::uint32_t payloadBits = 0;
  if (!bits.read(8, fromClient) || !bits.read(8, proximity) || !bits.read(16, payloadBits)) return false;
  if (payloadBits > bits.remaining() || !bits.skip(payloadBits)) return false;
  ++summary.voiceDataCount;
  summary.voicePayloadBits += payloadBits;
  return true;
}

void flattenSendTableSchemas(DemoNetworkSummary& summary);
bool readDataTableBody(MessageBits& bits, DemoNetworkSummary& summary);
bool readDataTablePacket(MessageBits& bits, DemoNetworkSummary& summary) {
  std::uint32_t tick = 0, lengthBytes = 0;
  if (!bits.read(32, tick) || !bits.read(32, lengthBytes)) return false;
  if (lengthBytes > bits.remaining() / 8u || lengthBytes > 128u * 1024u * 1024u) return false;
  std::vector<std::uint8_t> payload((static_cast<std::size_t>(lengthBytes) + 7u) / 8u, 0);
  for (std::size_t bit = 0; bit < static_cast<std::size_t>(lengthBytes) * 8u; ++bit) {
    std::uint32_t value = 0;
    if (!bits.read(1, value)) return false;
    payload[bit / 8u] |= static_cast<std::uint8_t>(value << (bit % 8u));
  }
  MessageBits body(payload);
  return readDataTableBody(body, summary);
}

bool readDataTableBody(MessageBits& bits, DemoNetworkSummary& summary) {
  while (bits.remaining() > 16u) {
    std::uint32_t hasTable = 0;
    if (!bits.read(1, hasTable)) return false;
    if (hasTable == 0) break;
    std::uint32_t needsDecoder = 0, propCount = 0;
    SendTableSchema table;
    if (!bits.read(1, needsDecoder) || !bits.readString(table.name) || !bits.read(10, propCount)) return false;
    table.needsDecoder = needsDecoder != 0;
    table.props.reserve(propCount);
    ++summary.dataTableDefinitionCount;
    summary.dataTablePropCount += propCount;
    if (summary.dataTableNames.size() < 256) summary.dataTableNames.push_back(table.name);
    SendPropSchema pendingArrayElement;
    bool hasPendingArrayElement = false;
    for (std::uint32_t i = 0; i < propCount; ++i) {
      std::uint32_t type = 0, flags = 0;
      SendPropSchema prop;
      if (!bits.read(5, type) || type > 6 || !bits.readString(prop.name) || !bits.read(16, flags)) return false;
      prop.type = static_cast<SendPropType>(type);
      prop.flags = static_cast<std::uint16_t>(flags);
      if ((flags & 64u) != 0 || type == 6u) {
        if (!bits.readString(prop.referencedTable)) return false;
      } else if (type == 5u) {
        std::uint32_t elementCount = 0;
        if (!bits.read(10, elementCount)) return false;
        prop.elementCount = static_cast<std::uint16_t>(elementCount);
      } else {
        std::uint32_t lowBits = 0, highBits = 0, bitCount = 0;
        if (!bits.read(32, lowBits) || !bits.read(32, highBits) || !bits.read(7, bitCount)) return false;
        std::memcpy(&prop.lowValue, &lowBits, sizeof(prop.lowValue));
        std::memcpy(&prop.highValue, &highBits, sizeof(prop.highValue));
        prop.hasFloatRange = type == static_cast<std::uint32_t>(SendPropType::Float) || type == static_cast<std::uint32_t>(SendPropType::Vector) || type == static_cast<std::uint32_t>(SendPropType::VectorXY);
        prop.bitCount = static_cast<std::uint8_t>(bitCount);
      }
      if ((flags & 256u) != 0) {
        pendingArrayElement = prop;
        hasPendingArrayElement = true;
      } else if (hasPendingArrayElement) {
        if (prop.type != SendPropType::Array) return false;
        prop.arrayElementType = pendingArrayElement.type;
        prop.arrayElementFlags = pendingArrayElement.flags;
        prop.arrayElementBitCount = pendingArrayElement.bitCount;
        hasPendingArrayElement = false;
        table.props.push_back(std::move(prop));
      } else {
        table.props.push_back(std::move(prop));
      }
    }
    if (hasPendingArrayElement) return false;
    summary.sendTableSchemas.push_back(std::move(table));
  }
  std::uint32_t classCount = 0;
  if (!bits.read(16, classCount) || classCount > 2048) return false;
  summary.dataTableServerClassCount = classCount;
  summary.serverClassSchemas.reserve(classCount);
  for (std::uint32_t i = 0; i < classCount; ++i) {
    ServerClassSchema serverClass;
    std::uint32_t classId = 0;
    if (!bits.read(16, classId) || !bits.readString(serverClass.name) || !bits.readString(serverClass.dataTable)) return false;
    serverClass.id = static_cast<std::uint16_t>(classId);
    if (summary.dataTableServerClassNames.size() < 512) summary.dataTableServerClassNames.push_back(serverClass.name);
    summary.serverClassSchemas.push_back(std::move(serverClass));
  }
  flattenSendTableSchemas(summary);
  ++summary.dataTablePacketCount;
  return true;
}

void flattenSendTableSchemas(DemoNetworkSummary& summary) {
  std::unordered_map<std::string, std::size_t> tableIndex;
  tableIndex.reserve(summary.sendTableSchemas.size());
  for (std::size_t i = 0; i < summary.sendTableSchemas.size(); ++i) tableIndex.emplace(summary.sendTableSchemas[i].name, i);
  using ExcludeSet = std::unordered_set<std::string>;
  constexpr std::size_t kMaxTableFlattenDepth = 128;
  std::function<void(std::size_t, ExcludeSet&, std::unordered_set<std::string>&)> collectExcludes;
  collectExcludes = [&](std::size_t index, ExcludeSet& excludes, std::unordered_set<std::string>& stack) {
    if (index >= summary.sendTableSchemas.size()) return;
    if (stack.size() >= kMaxTableFlattenDepth) return;
    const auto& table = summary.sendTableSchemas[index];
    if (!stack.insert(table.name).second) return;
    for (const auto& prop : table.props) {
      if ((prop.flags & 64u) != 0) {
        excludes.insert(prop.referencedTable + "|" + prop.name);
      } else if (prop.type == SendPropType::DataTable) {
        const auto child = tableIndex.find(prop.referencedTable);
        if (child != tableIndex.end()) collectExcludes(child->second, excludes, stack);
      }
    }
    stack.erase(table.name);
  };
  std::function<void(std::size_t, const ExcludeSet&, std::vector<SendPropSchema>&, std::vector<SendPropSchema>&, std::unordered_set<std::string>&)> pushCollapse;
  std::function<void(std::size_t, const ExcludeSet&, std::vector<SendPropSchema>&, std::unordered_set<std::string>&)> pushEnd;
  pushEnd = [&](std::size_t index, const ExcludeSet& excludes, std::vector<SendPropSchema>& output, std::unordered_set<std::string>& stack) {
    if (stack.size() >= kMaxTableFlattenDepth) return;
    std::vector<SendPropSchema> local;
    pushCollapse(index, excludes, local, output, stack);
    output.insert(output.end(), local.begin(), local.end());
  };
  pushCollapse = [&](std::size_t index, const ExcludeSet& excludes, std::vector<SendPropSchema>& local, std::vector<SendPropSchema>& output, std::unordered_set<std::string>& stack) {
    if (index >= summary.sendTableSchemas.size()) return;
    if (stack.size() >= kMaxTableFlattenDepth) return;
    const auto& table = summary.sendTableSchemas[index];
    if (!stack.insert(table.name).second) return;
    for (const auto& prop : table.props) {
      if ((prop.flags & 64u) != 0 || excludes.find(table.name + "|" + prop.name) != excludes.end()) continue;
      if (prop.type == SendPropType::DataTable) {
        const auto child = tableIndex.find(prop.referencedTable);
        if (child == tableIndex.end() || stack.count(prop.referencedTable) != 0) continue;
        if ((prop.flags & 4096u) != 0) pushCollapse(child->second, excludes, local, output, stack);
        else pushEnd(child->second, excludes, output, stack);
      } else {
        auto leaf = prop;
        leaf.ownerTable = table.name;
        local.push_back(std::move(leaf));
      }
    }
    stack.erase(table.name);
  };
  summary.dataTableFlattenedPropCount = 0;
  for (std::size_t i = 0; i < summary.sendTableSchemas.size(); ++i) {
    ExcludeSet excludes;
    std::unordered_set<std::string> excludeStack;
    collectExcludes(i, excludes, excludeStack);
    std::vector<SendPropSchema> output;
    std::unordered_set<std::string> stack;
    pushEnd(i, excludes, output, stack);
    // Source's flatten pass moves each CHANGES_OFTEN prop to the next front
    // slot with a swap. A stable partition changes the relative order of the
    // remaining props and therefore changes the wire property indexes.
    std::size_t changesOftenEnd = 0;
    for (std::size_t propIndex = 0; propIndex < output.size(); ++propIndex) {
      if ((output[propIndex].flags & 1024u) == 0) continue;
      if (propIndex != changesOftenEnd) std::swap(output[propIndex], output[changesOftenEnd]);
      ++changesOftenEnd;
    }
    summary.sendTableSchemas[i].flattenedProps = std::move(output);
    summary.dataTableFlattenedPropCount += summary.sendTableSchemas[i].flattenedProps.size();
  }
}

bool readSendTable(MessageBits& bits, DemoNetworkSummary& summary) {
  std::uint32_t needsDecoder = 0;
  std::uint32_t payloadBits = 0;
  if (!bits.read(1, needsDecoder) || !bits.read(16, payloadBits)) return false;
  if (payloadBits > bits.remaining() || !bits.skip(payloadBits)) return false;
  ++summary.sendTableCount;
  summary.sendTablePayloadBits += payloadBits;
  return true;
}

bool readEntityMessage(MessageBits& bits, DemoNetworkSummary& summary) {
  std::uint32_t entityIndex = 0;
  std::uint32_t classId = 0;
  std::uint32_t payloadBits = 0;
  if (!bits.read(11, entityIndex) || !bits.read(9, classId) || !bits.read(11, payloadBits)) return false;
  if (payloadBits > bits.remaining() || !bits.skip(payloadBits)) return false;
  ++summary.entityMessageCount;
  summary.entityMessagePayloadBits += payloadBits;
  return true;
}

bool readGetCvarValue(MessageBits& bits, DemoNetworkSummary& summary) {
  std::uint32_t cookie = 0;
  std::string name;
  if (!bits.read(32, cookie) || !bits.readString(name)) return false;
  ++summary.getCvarValueCount;
  return true;
}

void appendObserverCameraSample(DemoNetworkSummary& summary, ObserverCameraTrackSample sample) {
  constexpr std::size_t kMaxObserverCameraSamples = 32768;
  if (summary.observerCameraTrack.size() >= kMaxObserverCameraSamples) {
    summary.observerCameraTrack.erase(summary.observerCameraTrack.begin());
    ++summary.observerCameraTrackDropped;
  }
  summary.observerCameraTrack.push_back(sample);
}

bool readFixAngle(MessageBits& bits, DemoNetworkSummary& summary, std::int32_t packetTick) {
  std::uint32_t relative = 0;
  std::uint32_t raw[3] = {};
  if (!bits.read(1, relative) || !bits.read(16, raw[0]) ||
      !bits.read(16, raw[1]) || !bits.read(16, raw[2])) return false;
  constexpr float kAngleScale = 360.0f / 65536.0f;
  summary.lastFixAngleRelative = relative != 0;
  for (std::size_t axis = 0; axis < 3; ++axis) {
    summary.lastFixAngle[axis] = static_cast<float>(raw[axis]) * kAngleScale;
  }
  summary.lastFixAngleValid = true;
  ++summary.fixAngleCount;
  ObserverCameraTrackSample sample;
  sample.tick = packetTick;
  sample.hasAngles = true;
  sample.anglesRelative = summary.lastFixAngleRelative;
  std::copy(std::begin(summary.lastFixAngle), std::end(summary.lastFixAngle), std::begin(sample.angles));
  appendObserverCameraSample(summary, sample);
  return true;
}

bool readStringCommand(MessageBits& bits, DemoNetworkSummary& summary) {
  std::string command;
  if (!bits.readString(command)) return false;
  ++summary.stringCommandCount;
  return true;
}

bool readPrefetch(MessageBits& bits, DemoNetworkSummary& summary) {
  std::uint32_t soundIndex = 0;
  const std::uint32_t width = summary.networkProtocol > 23 ? 14u : 13u;
  if (!bits.read(width, soundIndex)) return false;
  ++summary.prefetchCount;
  return true;
}

bool readGameEvent(MessageBits& bits, DemoNetworkSummary& summary) {
  std::uint32_t payloadBits = 0;
  if (!bits.read(11, payloadBits) || payloadBits > bits.remaining() || !bits.skip(payloadBits)) return false;
  ++summary.gameEventCount;
  summary.gameEventPayloadBits += payloadBits;
  return true;
}

bool readUserMessage(MessageBits& bits, DemoNetworkSummary& summary) {
  std::uint32_t messageType = 0;
  std::uint32_t payloadBits = 0;
  if (!bits.read(8, messageType) || !bits.read(11, payloadBits) || payloadBits > bits.remaining() || !bits.skip(payloadBits)) return false;
  ++summary.userMessageCount;
  summary.userMessagePayloadBits += payloadBits;
  return true;
}

bool readGameEventList(MessageBits& bits, DemoNetworkSummary& summary) {
  std::uint32_t eventCount = 0;
  std::uint32_t payloadBits = 0;
  if (!bits.read(9, eventCount) || !bits.read(20, payloadBits)) return false;
  if (payloadBits > bits.remaining() || !bits.skip(payloadBits)) return false;
  ++summary.gameEventListCount;
  summary.gameEventDefinitionCount += eventCount;
  return true;
}

bool skipCoord(MessageBits& bits, bool integral, bool lowPrecision, bool multiplayer) {
  if (!multiplayer) {
    std::uint32_t hasInt = 0, hasFraction = 0;
    if (!bits.read(1, hasInt) || !bits.read(1, hasFraction)) return false;
    if (!hasInt && !hasFraction) return true;
    if (!bits.skip(1)) return false;
    if (hasInt && !bits.skip(14)) return false;
    return !hasFraction || bits.skip(5);
  }
  std::uint32_t inBounds = 0, hasInt = 0;
  if (!bits.read(1, inBounds) || !bits.read(1, hasInt)) return false;
  if (integral) {
    if (!hasInt) return true;
    if (!bits.skip(1)) return false;
    return bits.skip(inBounds ? 11 : 14);
  }
  if (!bits.skip(1)) return false;
  if (hasInt && !bits.skip(inBounds ? 11 : 14)) return false;
  return bits.skip(lowPrecision ? 3 : 5);
}

bool readNormalFloatValue(MessageBits& bits, float& value) {
  std::uint32_t negative = 0, fraction = 0;
  if (!bits.read(1, negative) || !bits.read(11, fraction)) return false;
  value = static_cast<float>(fraction) / 2048.0f;
  if (negative != 0) value = -value;
  return true;
}

bool readCoordValue(MessageBits& bits, bool integral, bool lowPrecision, bool multiplayer, float& value) {
  if (!multiplayer) {
    std::uint32_t hasInt = 0, hasFraction = 0;
    if (!bits.read(1, hasInt) || !bits.read(1, hasFraction)) return false;
    if (!hasInt && !hasFraction) { value = 0.0f; return true; }
    std::uint32_t negative = 0;
    if (!bits.read(1, negative)) return false;
    std::uint32_t integer = 0, fraction = 0;
    if (hasInt && !bits.read(14, integer)) return false;
    if (hasFraction && !bits.read(5, fraction)) return false;
    value = (static_cast<float>(hasInt ? integer + 1u : 0u) + static_cast<float>(fraction) / 32.0f) * (negative ? -1.0f : 1.0f);
    return true;
  }
  std::uint32_t inBounds = 0, hasInt = 0, negative = 0;
  if (!bits.read(1, inBounds) || !bits.read(1, hasInt)) return false;
  if (integral) {
    if (!hasInt) { value = 0.0f; return true; }
    if (!bits.read(1, negative)) return false;
    std::uint32_t integer = 0;
    if (!bits.read(inBounds ? 11u : 14u, integer)) return false;
    value = static_cast<float>(integer + 1u) * (negative ? -1.0f : 1.0f);
    return true;
  }
  if (!bits.read(1, negative)) return false;
  std::uint32_t integer = 0, fraction = 0;
  if (hasInt && !bits.read(inBounds ? 11u : 14u, integer)) return false;
  const std::uint32_t fractionBits = lowPrecision ? 3u : 5u;
  if (!bits.read(fractionBits, fraction)) return false;
  value = (static_cast<float>(hasInt ? integer + 1u : 0u) + static_cast<float>(fraction) / static_cast<float>(1u << fractionBits)) * (negative ? -1.0f : 1.0f);
  return true;
}

const SendTableSchema* tableForClass(const DemoNetworkSummary& summary, std::uint32_t classId) {
  std::string className;
  for (const auto& classSchema : summary.serverClassSchemas) {
    if (classSchema.id != classId) continue;
    for (const auto& table : summary.sendTableSchemas) if (table.name == classSchema.dataTable) return &table;
  }
  if (classId < summary.serverClassNames.size()) className = summary.serverClassNames[classId];
  if (!className.empty()) for (const auto& classSchema : summary.serverClassSchemas) {
    if (classSchema.name != className) continue;
    for (const auto& table : summary.sendTableSchemas) if (table.name == classSchema.dataTable) return &table;
  }
  return nullptr;
}

std::uint32_t effectiveServerClassCount(const DemoNetworkSummary& summary) {
  return summary.serverClassCount != 0
      ? summary.serverClassCount
      : static_cast<std::uint32_t>(summary.dataTableServerClassCount);
}


bool readSendPropValueDepth(MessageBits& bits, const SendPropSchema& prop, EntityPropertyValue& value,
                            std::uint32_t depth) {
  constexpr std::uint32_t kMaxValueDepth = 64;
  if (depth > kMaxValueDepth) return false;
  value = {};
  value.type = prop.type;
  const std::uint16_t flags = prop.flags;
  switch (prop.type) {
    case SendPropType::Int: {
      if ((flags & 32u) != 0) {
        std::uint32_t raw = 0;
        if (!bits.readVarInt(raw)) return false;
        if ((flags & 1u) != 0) value.intValue = static_cast<std::int64_t>(raw);
        else {
          const std::uint32_t sign = static_cast<std::uint32_t>(-static_cast<std::int32_t>(raw & 1u));
          value.intValue = static_cast<std::int64_t>((raw >> 1u) ^ sign);
        }
        return true;
      }
      const std::uint32_t width = prop.bitCount == 0 ? 32u : prop.bitCount;
      std::uint32_t raw = 0;
      if (!bits.read(width, raw)) return false;
      if ((flags & 1u) != 0 || width >= 32u) value.intValue = static_cast<std::int64_t>(static_cast<std::int32_t>(raw));
      else if ((raw & (1u << (width - 1u))) != 0) value.intValue = static_cast<std::int64_t>(raw | (~0u << width));
      else value.intValue = static_cast<std::int64_t>(raw);
      return true;
    }
    case SendPropType::Float: {
      if ((flags & 2u) != 0) return readCoordValue(bits, false, false, false, value.x);
      if ((flags & 32u) != 0) return readNormalFloatValue(bits, value.x);
      if ((flags & 32768u) != 0) return readCoordValue(bits, true, false, true, value.x);
      if ((flags & 16384u) != 0) return readCoordValue(bits, false, true, true, value.x);
      if ((flags & 8192u) != 0) return readCoordValue(bits, false, false, true, value.x);
      const std::uint32_t width = (flags & 4u) != 0 ? 32u : (prop.bitCount == 0 ? 32u : prop.bitCount);
      std::uint32_t raw = 0;
      if (!bits.read(width, raw)) return false;
      if (width == 32u && (flags & 4u) != 0) {
        std::memcpy(&value.x, &raw, sizeof(value.x));
      } else if (prop.hasFloatRange && prop.bitCount > 0 && prop.bitCount < 32u && prop.highValue > prop.lowValue) {
        const std::uint32_t maxRaw = (1u << prop.bitCount) - 1u;
        value.x = prop.lowValue + (prop.highValue - prop.lowValue) * (static_cast<float>(raw) / static_cast<float>(maxRaw));
      } else {
        value.x = static_cast<float>(raw);
      }
      return true;
    }
    case SendPropType::Vector:
      if ((flags & 128u) != 0) return readCoordValue(bits, false, false, false, value.x) && readCoordValue(bits, false, false, false, value.y) && readCoordValue(bits, false, false, false, value.z);
      { EntityPropertyValue component{};
        if (!readSendPropValueDepth(bits, SendPropSchema{SendPropType::Float, {}, {}, flags, prop.bitCount, 0, {}}, component, depth + 1)) return false;
        value.x = component.x;
        if (!readSendPropValueDepth(bits, SendPropSchema{SendPropType::Float, {}, {}, flags, prop.bitCount, 0, {}}, component, depth + 1)) return false;
        value.y = component.x;
        if (!readSendPropValueDepth(bits, SendPropSchema{SendPropType::Float, {}, {}, flags, prop.bitCount, 0, {}}, component, depth + 1)) return false;
        value.z = component.x;
        return true; }
    case SendPropType::VectorXY: {
      EntityPropertyValue component{};
      if (!readSendPropValueDepth(bits, SendPropSchema{SendPropType::Float, {}, {}, flags, prop.bitCount, 0, {}}, component, depth + 1)) return false;
      value.x = component.x;
      if (!readSendPropValueDepth(bits, SendPropSchema{SendPropType::Float, {}, {}, flags, prop.bitCount, 0, {}}, component, depth + 1)) return false;
      value.y = component.x;
      return true;
    }
    case SendPropType::String: {
      std::uint32_t length = 0;
      if (!bits.read(9, length) || length > 512u) return false;
      value.stringValue.reserve(length);
      for (std::uint32_t i = 0; i < length; ++i) { std::uint32_t byte = 0; if (!bits.read(8, byte)) return false; value.stringValue.push_back(static_cast<char>(byte)); }
      return true;
    }
    case SendPropType::Array: {
      std::uint32_t count = 0;
      std::uint32_t countBits = 1;
      // Source uses floor(log2(elementCount)) + 1 bits, including when the
      // array capacity is an exact power of two.
      while (countBits < 31u && (1u << countBits) <= std::max<std::uint32_t>(1u, prop.elementCount)) ++countBits;
      if (!bits.read(countBits, count) || count > prop.elementCount) return false;
      EntityPropertyValue element{};
      const SendPropSchema child{prop.arrayElementType, prop.name, prop.ownerTable, prop.arrayElementFlags, prop.arrayElementBitCount, 0, {}, {}, {}, {}};
      for (std::uint32_t i = 0; i < count; ++i) if (!readSendPropValueDepth(bits, child, element, depth + 1)) return false;
      value.intValue = static_cast<std::int64_t>(count);
      return true;
    }
    case SendPropType::DataTable:
      return false;
  }
  return false;
}

bool readSendPropValue(MessageBits& bits, const SendPropSchema& prop, EntityPropertyValue& value) {
  return readSendPropValueDepth(bits, prop, value, 0);
}

bool readEntityPropUpdates(MessageBits& bits, const SendTableSchema* table, EntityState& state, DemoNetworkSummary* summary = nullptr,
    std::int32_t packetTick = -1, std::int32_t entityIndex = -1, const char* stage = "entity") {
  const bool isTempStage = summary != nullptr && std::strcmp(stage, "temp") == 0;
  auto recordTempFailure = [&](const char* name, const SendPropSchema* prop) {
    if (!isTempStage || summary->firstTempEntityFailureTick >= 0) return;
    summary->firstTempEntityFailureTick = packetTick;
    summary->firstTempEntityFailureClass = state.classId;
    summary->firstTempEntityFailureStage = stage;
    summary->firstTempEntityFailureName = name;
    if (prop) {
      summary->firstTempEntityFailureType = static_cast<std::int32_t>(prop->type);
      summary->firstTempEntityFailureFlags = prop->flags;
      summary->firstTempEntityFailureBits = prop->bitCount;
    }
  };
  std::int32_t lastProp = -1;
  if (!table) {
    if (summary) {
      ++summary->entityPropMissingTableFailures;
      recordTempFailure("<missing-table>", nullptr);
      if (summary->firstEntityPropFailureTick < 0) { summary->firstEntityPropFailureClass = state.classId; summary->firstEntityPropFailureTick = packetTick; summary->firstEntityPropFailureEntity = entityIndex; summary->firstEntityPropFailureStage = stage; }
    }
    return false;
  }
  while (bits.remaining() > 0) {
    std::uint32_t hasProp = 0;
    if (!bits.read(1, hasProp)) return false;
    if (!hasProp) return true;
    std::uint32_t diff = 0;
    if (!bits.readBitVar(diff)) return false;
    lastProp += static_cast<std::int32_t>(diff) + 1;
    if (lastProp < 0 || static_cast<std::size_t>(lastProp) >= table->flattenedProps.size()) {
      if (summary) {
        ++summary->entityPropIndexFailures;
        recordTempFailure("index", nullptr);
        if (summary->firstEntityPropFailureTick < 0) {
          summary->firstEntityPropFailureClass = state.classId; summary->firstEntityPropFailureIndex = lastProp; summary->firstEntityPropFailureTick = packetTick; summary->firstEntityPropFailureEntity = entityIndex; summary->firstEntityPropFailureStage = stage; summary->firstEntityPropFailureName = "index";
        }
      }
      return false;
    }
    const auto& prop = table->flattenedProps[static_cast<std::size_t>(lastProp)];
    EntityPropertyValue value;
    if (!readSendPropValue(bits, prop, value)) {
      if (summary) {
        ++summary->entityPropValueFailures;
        recordTempFailure(prop.name.c_str(), &prop);
        if (summary->firstEntityPropFailureTick < 0) {
          summary->firstEntityPropFailureClass = state.classId; summary->firstEntityPropFailureIndex = lastProp; summary->firstEntityPropFailureTick = packetTick; summary->firstEntityPropFailureEntity = entityIndex; summary->firstEntityPropFailureStage = stage; summary->firstEntityPropFailureName = prop.name; summary->firstEntityPropFailureType = static_cast<std::int32_t>(prop.type); summary->firstEntityPropFailureFlags = prop.flags; summary->firstEntityPropFailureBits = prop.bitCount;
        }
      }
      return false;
    }
    const std::string key = prop.ownerTable.empty() ? prop.name : prop.ownerTable + "." + prop.name;
    state.properties[key] = std::move(value);
  }
  return false;
}

constexpr std::size_t kEntityHistoryMaxEvents = 8192;
constexpr std::size_t kEntityHistoryMaxCheckpoints = 8;
constexpr std::size_t kEntityHistoryCheckpointStride = 128;

const EntityFrame* findEntityFrame(const DemoNetworkSummary& summary, std::int32_t tick) {
  const auto it = std::find_if(summary.entityFrames.rbegin(), summary.entityFrames.rend(),
                               [tick](const EntityFrame& frame) { return frame.tick == tick; });
  return it == summary.entityFrames.rend() ? nullptr : &*it;
}

void retainEntityFrame(DemoNetworkSummary& summary, std::int32_t tick) {
  summary.entityFrames.push_back({tick, summary.entityClassByIndex, summary.entityStates});
  constexpr std::size_t maxFrames = 256;
  if (summary.entityFrames.size() > maxFrames) {
    summary.entityFrames.erase(summary.entityFrames.begin(),
                               summary.entityFrames.begin() + static_cast<std::ptrdiff_t>(summary.entityFrames.size() - maxFrames));
  }
}

void appendEntityHistory(DemoNetworkSummary& summary, std::int32_t tick, bool isDelta,
                         std::int32_t deltaFrom, std::vector<EntityHistoryEvent>&& events) {
  if (summary.entityHistoryHasGap) {
    if (isDelta) {
      ++summary.entityHistoryDroppedPackets;
      return;
    }
    summary.entityHistoryHasGap = false;
  }
  std::uint32_t packetOrdinal = static_cast<std::uint32_t>(summary.entityHistoryPackets.size());
  if (summary.entityHistoryEvents.size() + events.size() > kEntityHistoryMaxEvents ||
      (packetOrdinal > 0 && packetOrdinal % kEntityHistoryCheckpointStride == 0 &&
       summary.entityHistoryCheckpoints.size() >= kEntityHistoryMaxCheckpoints)) {
    // The live entity table already contains the state after this packet. Start
    // a new bounded history window from that state instead of turning normal
    // retention into a permanent decode gap. Each history event stores a full
    // post-update state, so subsequent packets remain independently replayable.
    ++summary.entityHistoryDroppedPackets;
    summary.entityHistoryEvents.clear();
    summary.entityHistoryPackets.clear();
    summary.entityHistoryCheckpoints.clear();
    packetOrdinal = 0;
    summary.entityHistoryCheckpoints.push_back({tick, 0u,
                                                summary.entityClassByIndex,
                                                summary.entityStates});
  }
  const std::size_t firstEvent = summary.entityHistoryEvents.size();
  for (auto& event : events) event.packetOrdinal = packetOrdinal;
  if (isDelta) {
    const auto base = std::lower_bound(
        summary.entityHistoryPackets.begin(), summary.entityHistoryPackets.end(),
        deltaFrom,
        [](const EntityHistoryPacket& packet, std::int32_t tick) {
          return packet.tick < tick;
        });
    if (base == summary.entityHistoryPackets.end() || base->tick != deltaFrom) {
      ++summary.entityHistoryDeltaBaseMisses;
    }
  }
  summary.entityHistoryEvents.insert(summary.entityHistoryEvents.end(),
                                     std::make_move_iterator(events.begin()),
                                     std::make_move_iterator(events.end()));
  summary.entityHistoryPackets.push_back({tick, deltaFrom, packetOrdinal, firstEvent,
                                           events.size(), isDelta});
  if (summary.entityHistoryCheckpoints.empty() ||
      packetOrdinal % kEntityHistoryCheckpointStride == 0) {
    summary.entityHistoryCheckpoints.push_back({tick, packetOrdinal,
                                                summary.entityClassByIndex,
                                                summary.entityStates});
  }
  retainEntityFrame(summary, tick);
}

bool readPacketEntities(MessageBits& bits, DemoNetworkSummary& summary, std::int32_t packetTick) {
  std::uint32_t maxEntries = 0, isDelta = 0, deltaFrom = 0, baseline = 0, updatedEntries = 0, payloadBits = 0, updateBaseline = 0;
  if (!bits.read(11, maxEntries) || !bits.read(1, isDelta)) return false;
  if (isDelta && !bits.read(32, deltaFrom)) return false;
  if (!bits.read(1, baseline) || !bits.read(11, updatedEntries) || !bits.read(20, payloadBits) || !bits.read(1, updateBaseline)) return false;
  if (updatedEntries > maxEntries || maxEntries > 2048u) return false;
  if (summary.firstPacketEntitiesTick < 0) { summary.firstPacketEntitiesTick = packetTick; summary.firstPacketEntitiesMaxEntries = static_cast<std::int32_t>(maxEntries); summary.firstPacketEntitiesUpdatedEntries = static_cast<std::int32_t>(updatedEntries); summary.firstPacketEntitiesPayloadBits = static_cast<std::int32_t>(payloadBits); summary.firstPacketEntitiesDelta = static_cast<std::int32_t>(isDelta); }
  if (payloadBits > bits.remaining()) return false;
  const std::size_t payloadEnd = bits.remaining() - payloadBits;
  bits.setMinimumRemaining(payloadEnd);
  const bool deltaBaseUnavailable = isDelta != 0 &&
      (summary.entityHistoryHasGap ||
       summary.entityHistoryPackets.empty() ||
       [&summary, deltaFrom] {
         const auto base = std::lower_bound(
             summary.entityHistoryPackets.begin(), summary.entityHistoryPackets.end(),
             static_cast<std::int32_t>(deltaFrom),
             [](const EntityHistoryPacket& packet, std::int32_t tick) {
               return packet.tick < tick;
             });
         return base == summary.entityHistoryPackets.end()
             || base->tick != static_cast<std::int32_t>(deltaFrom);
       }());
  if (isDelta) {
    ++summary.packetEntityDeltaCount;
    if (summary.firstPacketEntitiesDeltaTick < 0) {
      summary.firstPacketEntitiesDeltaTick = packetTick;
      summary.firstPacketEntitiesDeltaFrom = static_cast<std::int32_t>(deltaFrom);
    }
    if (deltaBaseUnavailable) {
      ++summary.packetEntityDeltaBaseUnavailableCount;
      if (summary.firstPacketEntitiesUnavailableTick < 0) {
        summary.firstPacketEntitiesUnavailableTick = packetTick;
        summary.firstPacketEntitiesUnavailableFrom = static_cast<std::int32_t>(deltaFrom);
      }
    }
    if (const auto* frame = findEntityFrame(summary, static_cast<std::int32_t>(deltaFrom))) {
      summary.entityClassByIndex = frame->classByIndex;
      summary.entityStates = frame->states;
    }
  }
  std::int32_t lastEntity = -1;
  bool entityUpdatesComplete = true;
  std::vector<EntityHistoryEvent> historyEvents;
  for (std::uint32_t i = 0; i < updatedEntries; ++i) {
    const std::size_t updateBitOffset = bits.offsetBits();
    std::uint32_t diff = 0, updateType = 0;
    if (!bits.readBitVar(diff) || !bits.read(2, updateType)) { ++summary.packetEntityDecodeFailures; ++summary.entityUpdateHeaderFailures; entityUpdatesComplete = false; break; }
    lastEntity += static_cast<std::int32_t>(diff) + 1;
    ++summary.packetEntityHeaderUpdates;
    if (updateType == 0) {
      if (summary.firstPacketEntitiesPreserveTick < 0) {
        summary.firstPacketEntitiesPreserveTick = packetTick;
        summary.firstPacketEntitiesFirstUpdateBit = static_cast<std::int64_t>(updateBitOffset);
        summary.firstPacketEntitiesFirstDiff = static_cast<std::int32_t>(diff);
      }
      ++summary.packetEntityPreserveCount;
      if (lastEntity < 0 || lastEntity >= 2048 || static_cast<std::size_t>(lastEntity) >= summary.entityClassByIndex.size() || summary.entityClassByIndex[static_cast<std::size_t>(lastEntity)] < 0) { ++summary.entityUnknownStateFailures; if (summary.firstEntityUnknownStateTick < 0) { summary.firstEntityUnknownStateTick = packetTick; summary.firstEntityUnknownStateEntity = lastEntity; summary.firstEntityUnknownStateUpdate = static_cast<std::int32_t>(updateType); summary.firstEntityUnknownStateMaxEntries = static_cast<std::int32_t>(maxEntries); summary.firstEntityUnknownStateUpdatedEntries = static_cast<std::int32_t>(updatedEntries); summary.firstEntityUnknownStatePayloadBits = static_cast<std::int32_t>(payloadBits); summary.firstEntityUnknownStateDiff = static_cast<std::int32_t>(diff); } entityUpdatesComplete = false; break; }
      const auto classId = static_cast<std::uint32_t>(summary.entityClassByIndex[static_cast<std::size_t>(lastEntity)]);
      const auto* table = tableForClass(summary, classId);
      EntityState candidate = summary.entityStates[static_cast<std::size_t>(lastEntity)];
      candidate.inPvs = true;
      if (!readEntityPropUpdates(bits, table, candidate, &summary, packetTick, lastEntity, "preserve")) { ++summary.packetEntityDecodeFailures; entityUpdatesComplete = false; break; }
      summary.entityStates[static_cast<std::size_t>(lastEntity)] = std::move(candidate);
      historyEvents.push_back({packetTick, 0, static_cast<std::uint16_t>(lastEntity),
                               summary.entityClassByIndex[static_cast<std::size_t>(lastEntity)], false,
                               summary.entityStates[static_cast<std::size_t>(lastEntity)]});
    } else if (updateType == 1) {
      ++summary.packetEntityLeaveCount;
      // Leave means the entity left the PVS, not that it was destroyed. Keep
      // Keep class/serial/state for a later Preserve, but mark it outside PVS.
      if (lastEntity >= 0 && static_cast<std::size_t>(lastEntity) < summary.entityStates.size())
        summary.entityStates[static_cast<std::size_t>(lastEntity)].inPvs = false;
    } else if (updateType == 3) {
      ++summary.packetEntityDeleteCount;
      if (lastEntity >= 0 && static_cast<std::size_t>(lastEntity) < summary.entityClassByIndex.size() && summary.entityClassByIndex[static_cast<std::size_t>(lastEntity)] >= 0) { summary.entityClassByIndex[static_cast<std::size_t>(lastEntity)] = -1; if (static_cast<std::size_t>(lastEntity) < summary.entityStates.size()) summary.entityStates[static_cast<std::size_t>(lastEntity)] = {}; if (summary.activeEntityCount > 0) --summary.activeEntityCount; }
      if (lastEntity >= 0 && lastEntity < 2048) historyEvents.push_back({packetTick, 0, static_cast<std::uint16_t>(lastEntity), -1, true, {}});
    } else if (updateType == 2) {
      if (summary.firstPacketEntitiesEnterTick < 0) {
        summary.firstPacketEntitiesEnterTick = packetTick;
        summary.firstPacketEntitiesFirstUpdateBit = static_cast<std::int64_t>(updateBitOffset);
        summary.firstPacketEntitiesFirstDiff = static_cast<std::int32_t>(diff);
      }
      ++summary.packetEntityEnterCount;
      const std::uint32_t serverClassCount = effectiveServerClassCount(summary);
      std::uint32_t classBits = 1; while (classBits < 31u && (1u << classBits) <= std::max<std::uint32_t>(1u, serverClassCount)) ++classBits;
      std::uint32_t classId = 0, serial = 0;
      if (!bits.read(classBits, classId) || !bits.read(10, serial)) { ++summary.packetEntityDecodeFailures; entityUpdatesComplete = false; break; }
      if (lastEntity < 0 || lastEntity >= 2048 || classId >= summary.serverClassSchemas.size()) { ++summary.packetEntityDecodeFailures; entityUpdatesComplete = false; break; }
      if (summary.entityClassByIndex.size() < 2048u) summary.entityClassByIndex.resize(2048u, -1);
      if (summary.entityStates.size() < 2048u) summary.entityStates.resize(2048u);
      EntityState candidate;
      candidate.classId = static_cast<std::int32_t>(classId);
      candidate.serial = serial;
      candidate.inPvs = true;
      const auto* table = tableForClass(summary, classId);
      const auto baselineEntry = summary.instanceBaselines.find(static_cast<std::uint16_t>(classId));
      if (summary.firstInstanceBaselineClassId < 0) {
        summary.firstInstanceBaselineClassId = static_cast<std::int32_t>(classId);
      }
      if (summary.firstInstanceBaselineLookupClassId < 0) {
        summary.firstInstanceBaselineLookupClassId = static_cast<std::int32_t>(classId);
        summary.firstInstanceBaselineLookupHasTable = table != nullptr;
        summary.firstInstanceBaselineLookupHasEntry = baselineEntry != summary.instanceBaselines.end();
      }
      if (table && baselineEntry != summary.instanceBaselines.end()) {
        MessageBits baselineBits(baselineEntry->second);
        if (readEntityPropUpdates(baselineBits, table, candidate, &summary, packetTick, lastEntity, "baseline")) {
          ++summary.instanceBaselineAppliedCount;
          if (summary.firstInstanceBaselineLookupClassId == static_cast<std::int32_t>(classId)) summary.firstInstanceBaselineLookupApplied = true;
        } else ++summary.instanceBaselineApplyFailures;
      } else {
        ++summary.instanceBaselineLookupMisses;
      }
      if (!readEntityPropUpdates(bits, table, candidate, &summary, packetTick, lastEntity, "enter")) { ++summary.packetEntityDecodeFailures; entityUpdatesComplete = false; break; }
      auto& classSlot = summary.entityClassByIndex[static_cast<std::size_t>(lastEntity)];
      if (classSlot < 0) ++summary.activeEntityCount;
      classSlot = static_cast<std::int32_t>(classId);
      summary.maxActiveEntityCount = std::max(summary.maxActiveEntityCount, summary.activeEntityCount);
      summary.entityStates[static_cast<std::size_t>(lastEntity)] = std::move(candidate);
      historyEvents.push_back({packetTick, 0, static_cast<std::uint16_t>(lastEntity),
                               static_cast<std::int32_t>(classId), false,
                               summary.entityStates[static_cast<std::size_t>(lastEntity)]});
    } else { ++summary.packetEntityDecodeFailures; ++summary.entityUpdateHeaderFailures; entityUpdatesComplete = false; break; }
    if (bits.remaining() <= payloadEnd) break;
  }
  if (entityUpdatesComplete && isDelta) {
    while (bits.remaining() > payloadEnd) {
      std::uint32_t hasRemoved = 0;
      if (!bits.read(1, hasRemoved)) { ++summary.packetEntityDecodeFailures; entityUpdatesComplete = false; break; }
      if (!hasRemoved) break;
      std::uint32_t removedEntity = 0;
      if (!bits.read(11, removedEntity)) { ++summary.packetEntityDecodeFailures; entityUpdatesComplete = false; break; }
      if (removedEntity < summary.entityClassByIndex.size() && summary.entityClassByIndex[removedEntity] >= 0) {
        summary.entityClassByIndex[removedEntity] = -1;
        if (removedEntity < summary.entityStates.size()) summary.entityStates[removedEntity] = {};
        if (summary.activeEntityCount > 0) --summary.activeEntityCount;
        if (removedEntity < 2048) historyEvents.push_back({packetTick, 0, static_cast<std::uint16_t>(removedEntity), -1, true, {}});
      }
    }
  }
  if (bits.remaining() > payloadEnd) { ++summary.packetEntityDecodeFailures; if (!bits.skip(bits.remaining() - payloadEnd)) return false; }
  bits.setMinimumRemaining(0);
  ++summary.packetEntitiesCount;
  summary.packetEntityUpdates += updatedEntries;
  summary.packetEntityPayloadBits += payloadBits;
  if (bits.remaining() < payloadEnd) return false;
  if (bits.remaining() > payloadEnd) return false;
  if (maxEntries > 2048) return false;
  if (!entityUpdatesComplete) {
    summary.entityHistoryEvents.clear();
    summary.entityHistoryPackets.clear();
    summary.entityHistoryCheckpoints.clear();
    summary.entityHistoryHasGap = true;
    summary.entityHistoryGapTick = packetTick;
    // Source keeps the live entity directory across packet decode failures and
    // delta gaps. Clearing it turns the next Preserve updates into a cascade
    // of false unknown-state failures; history remains unavailable until a
    // complete packet re-establishes a checkpoint.
    return true;
  }
  appendEntityHistory(summary, packetTick, isDelta != 0, static_cast<std::int32_t>(deltaFrom), std::move(historyEvents));
  return true;
}

bool readUpdateStringTable(MessageBits& bits, DemoNetworkSummary& summary) {
  std::uint32_t tableId = 0;
  std::uint32_t changed = 0;
  std::uint32_t changedEntries = 1;
  std::uint32_t payloadBits = 0;
  if (!bits.read(5, tableId) || !bits.read(1, changed)) return false;
  if (changed && !bits.read(16, changedEntries)) return false;
  if (!bits.read(20, payloadBits) || payloadBits > bits.remaining()) return false;
  summary.lastStringTableUpdateId = tableId;
  summary.lastStringTableUpdateEntries = changedEntries;
  summary.lastStringTableUpdateBits = payloadBits;
  if (summary.stringTableUpdateIds.size() < 128u) {
    summary.stringTableUpdateIds.push_back(tableId);
    summary.stringTableUpdateEntries.push_back(changedEntries);
  }
  std::vector<std::uint8_t> payload((static_cast<std::size_t>(payloadBits) + 7u) / 8u, 0);
  for (std::size_t bit = 0; bit < payloadBits; ++bit) {
    std::uint32_t value = 0;
    if (!bits.read(1, value)) return false;
    payload[bit / 8u] |= static_cast<std::uint8_t>(value << (bit % 8u));
  }
  if (tableId < 32u && tableId == summary.soundPrecacheTableId) {
    ++summary.soundPrecacheUpdateCount;
    const auto maxIt = summary.stringTableMaxEntries.find(tableId);
    const std::uint32_t maxEntries = maxIt == summary.stringTableMaxEntries.end() ? summary.soundPrecacheMaxEntries : maxIt->second;
    // Source's log_base2(maxEntries) is floor(log2(maxEntries)); the old
    // loop computed one bit too few for every power-of-two table size.
    std::uint32_t indexBits = 0;
    const std::uint32_t boundedMaxEntries = std::max<std::uint32_t>(1u, maxEntries);
    while ((1u << indexBits) < boundedMaxEntries && indexBits < 31u) ++indexBits;
    MessageBits update(payload);
    std::vector<std::string> history;
    std::int32_t lastIndex = -1;
    bool ok = maxEntries != 0;
    for (std::uint32_t i = 0; ok && i < changedEntries; ++i) {
      std::uint32_t sequential = 0, value = 0, index = 0;
      if (!update.read(1, sequential)) { ok = false; break; }
      if (sequential) index = static_cast<std::uint32_t>(lastIndex + 1);
      else if (!update.read(indexBits, index)) { ok = false; break; }
      lastIndex = static_cast<std::int32_t>(index);
      std::string text;
      if (!update.read(1, value)) { ok = false; break; }
      if (value) {
        if (!update.read(1, value)) { ok = false; break; }
        if (value) {
          std::uint32_t historyIndex = 0, copyCount = 0;
          std::string rest;
          if (!update.read(5, historyIndex) || !update.read(5, copyCount) || !update.readString(rest, 4096)) { ok = false; break; }
          if (historyIndex < history.size() && copyCount <= history[historyIndex].size()) text = history[historyIndex].substr(0, copyCount) + rest;
          else text = std::move(rest);
        } else if (!update.readString(text, 4096)) { ok = false; break; }
      }
      history.push_back(text);
      if (history.size() > 32u) history.erase(history.begin());
      if (!update.read(1, value)) { ok = false; break; }
      if (value) {
        if (summary.soundPrecacheFixedBits != 0u) {
          if (!update.skip(summary.soundPrecacheFixedBits)) { ok = false; break; }
        } else {
          std::uint32_t userBytes = 0;
          if (!update.read(14, userBytes) || userBytes > 1024u || !update.skip(static_cast<std::size_t>(userBytes) * 8u)) { ok = false; break; }
        }
      }
      if (!text.empty() && index <= 0xffffu) summary.soundPrecache[static_cast<std::uint16_t>(index)] = std::move(text);
    }
    if (!ok) ++summary.soundPrecacheDecodeFailures;
  }
  ++summary.updateStringTableCount;
  return true;
}

bool readSounds(MessageBits& bits, DemoNetworkSummary& summary, std::int32_t packetTick) {
  std::uint32_t reliable = 0;
  std::uint32_t soundCount = 0;
  std::uint32_t payloadBits = 0;
  if (!bits.read(1, reliable)) return false;
  if (reliable) {
    soundCount = 1;
    if (!bits.read(8, payloadBits)) return false;
  } else {
    if (!bits.read(8, soundCount) || !bits.read(16, payloadBits)) return false;
    if (soundCount == 0) return false;
  }
  if (payloadBits > bits.remaining()) return false;
  RawSoundMessage raw;
  raw.tick = packetTick;
  raw.reliable = reliable != 0;
  raw.count = static_cast<std::uint8_t>(soundCount);
  raw.payloadBits = static_cast<std::uint16_t>(payloadBits);
  std::vector<std::uint8_t> soundPayload;
  if (payloadBits < 64u * 1024u && summary.rawSoundMessages.size() < 4096u) {
    raw.payload.assign((static_cast<std::size_t>(payloadBits) + 7u) / 8u, 0);
    for (std::size_t bit = 0; bit < payloadBits; ++bit) {
      std::uint32_t value = 0;
      if (!bits.read(1, value)) return false;
      raw.payload[bit / 8u] |= static_cast<std::uint8_t>(value << (bit % 8u));
    }
    soundPayload = raw.payload;
    summary.rawSoundMessages.push_back(std::move(raw));
  } else {
    ++summary.rawSoundMessagesDropped;
    if (!bits.skip(payloadBits)) return false;
  }
  MessageBits soundBits(soundPayload);
  auto readSigned = [&](std::uint32_t width, std::int32_t& value) {
    std::uint32_t rawValue = 0;
    if (!soundBits.read(width, rawValue)) return false;
    if (width == 0 || width > 32) return false;
    if (width == 32) { value = static_cast<std::int32_t>(rawValue); return true; }
    const std::uint32_t sign = 1u << (width - 1u);
    value = static_cast<std::int32_t>((rawValue & sign) ? (rawValue | (~0u << width)) : rawValue);
    return true;
  };
  for (std::uint32_t eventIndex = 0; eventIndex < soundCount; ++eventIndex) {
    DecodedSoundEvent event = summary.soundDeltaStateValid ? summary.soundDeltaState : DecodedSoundEvent{};
    event.tick = packetTick;
    std::uint32_t value = 0;
    if (!soundBits.read(1, value)) { ++summary.decodedSoundEventFailures; break; }
    if (value) {
      if (!soundBits.read(1, value)) { ++summary.decodedSoundEventFailures; break; }
      if (value) { if (!soundBits.read(5, value)) { ++summary.decodedSoundEventFailures; break; } }
      else if (!soundBits.read(11, value)) { ++summary.decodedSoundEventFailures; break; }
      event.entityIndex = static_cast<std::int32_t>(value);
    }
    if (!soundBits.read(1, value)) { ++summary.decodedSoundEventFailures; break; }
    if (value) { if (!soundBits.read(14, value)) { ++summary.decodedSoundEventFailures; break; } event.soundIndex = static_cast<std::uint16_t>(value); }
    if (!soundBits.read(1, value)) { ++summary.decodedSoundEventFailures; break; }
    if (value) { if (!soundBits.read(11, value)) { ++summary.decodedSoundEventFailures; break; } event.flags = static_cast<std::uint16_t>(value); }
    if (!soundBits.read(1, value)) { ++summary.decodedSoundEventFailures; break; }
    if (value) { if (!soundBits.read(3, value)) { ++summary.decodedSoundEventFailures; break; } event.channel = static_cast<std::uint8_t>(value); }
    if (!soundBits.read(1, value)) { ++summary.decodedSoundEventFailures; break; } event.ambient = value != 0;
    if (!soundBits.read(1, value)) { ++summary.decodedSoundEventFailures; break; } event.sentence = value != 0;
    if ((event.flags & 4u) == 0) {
      if (!soundBits.read(1, value)) { ++summary.decodedSoundEventFailures; break; }
      if (!value) { if (!soundBits.read(1, value)) { ++summary.decodedSoundEventFailures; break; } if (!value && !soundBits.read(10, value)) { ++summary.decodedSoundEventFailures; break; } }
      if (!soundBits.read(1, value)) { ++summary.decodedSoundEventFailures; break; }
      if (value && (!soundBits.read(7, value))) { ++summary.decodedSoundEventFailures; break; }
      if (value) event.volume = static_cast<float>(value) / 127.0f;
      if (!soundBits.read(1, value)) { ++summary.decodedSoundEventFailures; break; }
      if (value && !soundBits.read(9, value)) { ++summary.decodedSoundEventFailures; break; }
      if (!soundBits.read(1, value)) { ++summary.decodedSoundEventFailures; break; }
      if (value && !soundBits.read(8, value)) { ++summary.decodedSoundEventFailures; break; }
      if (value) event.pitch = static_cast<std::uint8_t>(value);
      if (summary.networkProtocol > 21) { if (!soundBits.read(1, value)) { ++summary.decodedSoundEventFailures; break; } if (value && !soundBits.read(8, value)) { ++summary.decodedSoundEventFailures; break; } }
      if (!soundBits.read(1, value)) { ++summary.decodedSoundEventFailures; break; }
      if (value) {
        std::int32_t delay = 0;
        if (!readSigned(13, delay)) { ++summary.decodedSoundEventFailures; break; }
        event.delaySeconds = static_cast<float>(delay) / 1000.0f;
        if (event.delaySeconds < 0.0f) event.delaySeconds *= 10.0f;
        event.delaySeconds -= 0.100f;
      }
      bool failed = false;
      for (float& coordinate : event.origin) {
        if (!soundBits.read(1, value)) { failed = true; break; }
        if (value) {
          std::int32_t rawCoordinate = 0;
          if (!readSigned(12, rawCoordinate)) { failed = true; break; }
          coordinate = static_cast<float>(rawCoordinate) * 8.0f;
        }
      }
      if (failed) { ++summary.decodedSoundEventFailures; break; }
      if (!soundBits.read(1, value)) { ++summary.decodedSoundEventFailures; break; }
      if (value) {
        std::int32_t speakerEntity = 0;
        if (!readSigned(12, speakerEntity)) { ++summary.decodedSoundEventFailures; break; }
      }
    }
    event.valid = event.soundIndex != 0;
    summary.soundDeltaState = event;
    summary.soundDeltaStateValid = true;
    if (event.valid && summary.decodedSoundEvents.size() < 16384u) summary.decodedSoundEvents.push_back(event);
  }
  ++summary.soundMessageCount;
  summary.soundEventCount += soundCount;
  summary.reliableSoundCount += reliable != 0 ? 1u : 0u;
  summary.soundPayloadBits += payloadBits;
  return true;
}

bool readTempEntities(MessageBits& bits, DemoNetworkSummary& summary, std::int32_t packetTick) {
  std::uint32_t count = 0, payloadBits = 0;
  if (!bits.read(8, count)) return false;
  const bool lengthRead = summary.networkProtocol > 23 ? bits.readVarInt(payloadBits) : bits.read(17, payloadBits);
  if (!lengthRead || payloadBits > bits.remaining()) return false;
  summary.tempPayloadBits += payloadBits;
  std::vector<std::uint8_t> payload((static_cast<std::size_t>(payloadBits) + 7u) / 8u, 0);
  for (std::size_t bit = 0; bit < payloadBits; ++bit) {
    std::uint32_t value = 0;
    if (!bits.read(1, value)) return false;
    payload[bit / 8u] |= static_cast<std::uint8_t>(value << (bit % 8u));
  }
  MessageBits body(payload);
  const std::uint32_t eventCount = count == 0 ? 1u : count;
  std::uint32_t lastClassId = 0;
  bool haveClass = false;
  const std::uint32_t serverClassCount = effectiveServerClassCount(summary);
  std::uint32_t classBits = 1;
  while (classBits < 31u && (1u << classBits) <= std::max<std::uint32_t>(1u, serverClassCount)) ++classBits;
  for (std::uint32_t i = 0; i < eventCount; ++i) {
    std::uint32_t hasDelay = 0, delayRaw = 0, hasClass = 0;
    if (!body.read(1, hasDelay)) { ++summary.tempEventDecodeFailures; break; }
    if (hasDelay && !body.read(8, delayRaw)) { ++summary.tempEventDecodeFailures; break; }
    if (!body.read(1, hasClass)) { ++summary.tempEventDecodeFailures; break; }
    if (hasClass) {
      std::uint32_t encodedClass = 0;
      if (!body.read(classBits, encodedClass) || encodedClass == 0 ||
          encodedClass > serverClassCount) { ++summary.tempEventDecodeFailures; break; }
      lastClassId = encodedClass - 1u;
      haveClass = true;
    } else if (!haveClass) {
      ++summary.tempEventDecodeFailures;
      break;
    }
    EntityState eventState;
    eventState.classId = static_cast<std::int32_t>(lastClassId);
    std::string eventClassName;
    for (const auto& classSchema : summary.serverClassSchemas) if (classSchema.id == lastClassId) {
      eventClassName = classSchema.name;
      if (classSchema.name == "CTEClientProjectile") ++summary.tempClientProjectileCount;
      if (classSchema.name == "CTETFParticleEffect") ++summary.tempParticleEffectCount;
      if (classSchema.name == "CTETFExplosion" || classSchema.name == "CTEExplosion") ++summary.tempExplosionCount;
      if (classSchema.name == "CTEFireBullets") ++summary.tempFireBulletsCount;
      if (classSchema.name == "CTEPlayerAnimEvent") ++summary.tempPlayerAnimEventCount;
      break;
    }
    const auto* table = tableForClass(summary, lastClassId);
    if (!readEntityPropUpdates(body, table, eventState, &summary, packetTick, -1, "temp")) {
      if (summary.firstTempEntityFailureTick < 0) {
        summary.firstTempEntityFailureTick = packetTick;
        summary.firstTempEntityFailureClass = static_cast<std::int32_t>(lastClassId);
        summary.firstTempEntityFailureStage = summary.firstEntityPropFailureStage;
        summary.firstTempEntityFailurePayloadBits = static_cast<std::int32_t>(payloadBits);
      }
      ++summary.tempEventDecodeFailures; break;
    }
    ++summary.tempEventHeadersDecoded;
    summary.tempPropDecodedCount += eventState.properties.size();
    if (summary.tempEntityEvents.size() < 16384u) {
      TempEntityEvent event;
      event.tick = packetTick;
      event.className = std::move(eventClassName);
      event.classId = lastClassId;
      event.reliable = count == 0;
      event.hasDelay = hasDelay != 0;
      event.delayRaw = static_cast<std::uint8_t>(delayRaw);
      event.properties = eventState.properties;
      auto findValue = [&eventState](const char* suffix) -> const EntityPropertyValue* {
        for (const auto& [name, value] : eventState.properties) {
          if (name == suffix || (name.size() > std::strlen(suffix) &&
                                 name.compare(name.size() - std::strlen(suffix), std::strlen(suffix), suffix) == 0)) {
            return &value;
          }
        }
        return nullptr;
      };
      auto intField = [&event, &findValue](const char* name, bool& present, std::int64_t& target) {
        if (const auto* value = findValue(name); value && value->type == SendPropType::Int) {
          present = true; target = value->intValue; ++event.knownFieldCount;
        }
      };
      auto vecField = [&event, &findValue](const char* name, bool& present, float target[3]) {
        if (const auto* value = findValue(name); value &&
            (value->type == SendPropType::Vector || value->type == SendPropType::VectorXY)) {
          present = value->type == SendPropType::Vector;
          target[0] = value->x; target[1] = value->y; target[2] = value->z;
          if (present) ++event.knownFieldCount;
        }
      };
      vecField("m_vecOrigin", event.hasOrigin, event.origin);
      vecField("m_vecAngles", event.hasAngles, event.angles);
      if (event.className == "CTEFireBullets") {
        intField("m_iPlayer", event.hasPlayer, event.player);
        intField("m_iWeaponID", event.hasWeaponId, event.weaponId);
        intField("m_iMode", event.hasMode, event.mode);
        intField("m_iSeed", event.hasSeed, event.seed);
      } else if (event.className == "CTEPlayerAnimEvent") {
        intField("m_hPlayer", event.hasPlayer, event.player);
        intField("m_iEvent", event.hasEvent, event.event);
      } else if (event.className == "CTETFParticleEffect") {
        intField("m_iEffectIndex", event.hasEffectIndex, event.effectIndex);
        if (const auto* value = findValue("m_szParticleName"); value && value->type == SendPropType::String) {
          event.hasParticleName = true; event.particleName = value->stringValue; ++event.knownFieldCount;
        }
      } else if (event.className == "CTETFExplosion" || event.className == "CTEExplosion") {
        intField("m_iMagnitude", event.hasMagnitude, event.magnitude);
        intField("m_iScale", event.hasScale, event.scale);
        intField("m_iRadius", event.hasRadius, event.radius);
      }
      if (event.className == "CTEFireBullets") summary.tempFireBulletsFieldHits += event.knownFieldCount;
      else if (event.className == "CTETFParticleEffect") summary.tempParticleEffectFieldHits += event.knownFieldCount;
      else if (event.className == "CTETFExplosion" || event.className == "CTEExplosion") summary.tempExplosionFieldHits += event.knownFieldCount;
      else if (event.className == "CTEPlayerAnimEvent") summary.tempPlayerAnimEventFieldHits += event.knownFieldCount;
      const bool projectileClass = event.className == "CTEFireBullets" ||
                                   event.className == "CTETFParticleEffect" ||
                                   event.className == "CTETFExplosion" ||
                                   event.className == "CTEExplosion";
      if (projectileClass && summary.projectileTimeline.size() < 16384u) {
        ProjectileTimelineEvent timeline;
        timeline.tick = event.tick;
        timeline.className = event.className;
        timeline.hasOrigin = event.hasOrigin && std::isfinite(event.origin[0]) &&
                             std::isfinite(event.origin[1]) && std::isfinite(event.origin[2]);
        if (timeline.hasOrigin) std::copy(std::begin(event.origin), std::end(event.origin), std::begin(timeline.origin));
        timeline.hasDirection = event.hasAngles && std::isfinite(event.angles[0]) &&
                                std::isfinite(event.angles[1]) && std::isfinite(event.angles[2]);
        if (timeline.hasDirection) std::copy(std::begin(event.angles), std::end(event.angles), std::begin(timeline.direction));
        timeline.hasWeaponId = event.hasWeaponId;
        timeline.weaponId = event.weaponId;
        timeline.hasParticleName = event.hasParticleName;
        timeline.particleName = event.particleName;
        timeline.hasMagnitude = event.hasMagnitude && event.magnitude >= 0;
        timeline.magnitude = event.magnitude;
        timeline.hasScale = event.hasScale;
        timeline.scale = event.scale;
        timeline.hasRadius = event.hasRadius && event.radius >= 0;
        timeline.radius = event.radius;
        summary.projectileTimeline.push_back(std::move(timeline));
      }
      summary.tempEntityEvents.push_back(std::move(event));
    }
  }
  if (bits.remaining() > 0) { /* payload was already consumed bit-for-bit */ }
  ++summary.tempEntitiesCount;
  summary.tempEventCount += eventCount;
  summary.tempPayloadBits += payloadBits;
  return true;
}

bool readSetConVar(MessageBits& bits, DemoNetworkSummary& summary) {
  std::uint32_t count = 0;
  if (!bits.read(8, count) || count > 255) return false;
  for (std::uint32_t i = 0; i < count; ++i) {
    std::string name;
    std::string value;
    if (!bits.readString(name) || !bits.readString(value)) return false;
    ++summary.setConVarPairCount;
  }
  ++summary.setConVarCount;
  return true;
}

bool readSetView(MessageBits& bits, DemoNetworkSummary& summary, std::int32_t packetTick) {
  std::uint32_t entityIndex = 0;
  if (!bits.read(11, entityIndex)) return false;
  ++summary.setViewCount;
  summary.lastViewEntity = entityIndex;
  ObserverCameraTrackSample sample;
  sample.tick = packetTick;
  sample.hasViewEntity = true;
  sample.viewEntity = entityIndex;
  appendObserverCameraSample(summary, sample);
  return true;
}

bool readClassInfo(MessageBits& bits, DemoNetworkSummary& summary) {
  std::uint32_t classCount = 0;
  std::uint32_t createOnClient = 0;
  if (!bits.read(16, classCount) || classCount > 2048 || !bits.read(1, createOnClient)) return false;
  if (!createOnClient) {
    std::uint32_t classIdBits = 0;
    while ((1u << classIdBits) <= classCount && classIdBits < 31) ++classIdBits;
    if (classIdBits == 0) classIdBits = 1;
    for (std::uint32_t i = 0; i < classCount; ++i) {
      std::uint32_t classId = 0;
      std::string className;
      std::string dataTableName;
      if (!bits.read(classIdBits, classId) || !bits.readString(className) || !bits.readString(dataTableName)) return false;
      if (summary.serverClassNames.size() < 512) {
        summary.serverClassNames.push_back(className);
        summary.serverDataTableNames.push_back(dataTableName);
      }
    }
  }
  ++summary.classInfoCount;
  summary.serverClassCount = classCount;
  summary.classCreateOnClient = createOnClient != 0;
  return true;
}

bool readSignonState(MessageBits& bits, DemoNetworkSummary& summary) {
  std::uint32_t state = 0;
  std::uint32_t spawnCount = 0;
  if (!bits.read(8, state) || !bits.read(32, spawnCount)) return false;
  ++summary.signonStateCount;
  summary.lastSignonState = state;
  summary.signonSpawnCount = spawnCount;
  return state <= 7;
}

bool readServerInfo(MessageBits& bits, DemoNetworkSummary& summary) {
  std::uint32_t value = 0;
  std::uint32_t stv = 0;
  if (!bits.read(16, value) || !bits.read(32, value) || !bits.read(1, stv) || !bits.read(1, value) || !bits.read(32, value) || !bits.read(16, value)) return false;
  for (int i = 0; i < 16; ++i) if (!bits.read(8, value)) return false;
  if (!bits.read(8, value) || !bits.read(8, value) || !bits.read(32, value)) return false;
  std::string platform, game, map, skybox, server;
  if (!bits.readStringLimit(platform, 1) || !bits.readString(game) || !bits.readString(map) || !bits.readString(skybox) || !bits.readString(server) || !bits.read(1, value)) return false;
  summary.serverMap = map; summary.serverName = server; summary.sourceTv = stv != 0; return true;
}

bool decodeLzss(const std::vector<std::uint8_t>& input, std::size_t offset, std::size_t length,
    std::size_t expectedSize, std::vector<std::uint8_t>& output) {
  if (offset > input.size() || length > input.size() - offset || length < 4u) return false;
  const std::size_t end = offset + length;
  const std::uint32_t targetSize = static_cast<std::uint32_t>(input[offset]) |
    (static_cast<std::uint32_t>(input[offset + 1u]) << 8u) |
    (static_cast<std::uint32_t>(input[offset + 2u]) << 16u) |
    (static_cast<std::uint32_t>(input[offset + 3u]) << 24u);
  if (targetSize != expectedSize || targetSize > 128u * 1024u * 1024u) return false;
  std::size_t cursor = offset + 4u;
  output.clear();
  output.reserve(targetSize);
  while (cursor < end && output.size() < targetSize) {
    const std::uint8_t commands = input[cursor++];
    std::uint8_t commandBits = commands;
    for (int bit = 0; bit < 8 && output.size() < targetSize; ++bit, commandBits >>= 1u) {
      if ((commandBits & 1u) == 0u) {
        if (cursor >= end) return false;
        output.push_back(input[cursor++]);
        continue;
      }
      if (cursor + 2u > end) return false;
      const std::size_t position = (static_cast<std::size_t>(input[cursor]) << 4u) | (input[cursor + 1u] >> 4u);
      const std::size_t count = (input[cursor + 1u] & 0x0fu) + 1u;
      cursor += 2u;
      if (count == 1u || position + 1u > output.size() || output.size() + count > targetSize) return false;
      const std::size_t start = output.size() - position - 1u;
      for (std::size_t i = 0; i < count; ++i) output.push_back(output[start + i]);
    }
  }
  return output.size() == targetSize;
}

bool decodeSnappyRaw(const std::vector<std::uint8_t>& input, std::size_t offset, std::size_t length,
    std::size_t expectedSize, std::vector<std::uint8_t>& output) {
  if (offset > input.size() || length > input.size() - offset) return false;
  std::size_t cursor = offset;
  const std::size_t end = offset + length;
  std::uint32_t decodedSize = 0;
  std::uint32_t shift = 0;
  while (cursor < end && shift < 32u) {
    const std::uint8_t byte = input[cursor++];
    decodedSize |= static_cast<std::uint32_t>(byte & 0x7fu) << shift;
    if ((byte & 0x80u) == 0) break;
    shift += 7u;
  }
  if (decodedSize != expectedSize || decodedSize > 128u * 1024u * 1024u) return false;
  output.clear();
  output.reserve(decodedSize);
  while (cursor < end && output.size() < decodedSize) {
    const std::uint8_t tag = input[cursor++];
    const std::uint32_t kind = tag & 3u;
    if (kind == 0u) {
      const std::uint32_t lengthCode = tag >> 2u;
      std::uint32_t literalLength = lengthCode + 1u;
      if (lengthCode >= 60u) {
        const std::uint32_t extraBytes = lengthCode - 59u;
        if (extraBytes > 4u || cursor + extraBytes > end) return false;
        literalLength = 0;
        for (std::uint32_t i = 0; i < extraBytes; ++i) literalLength |= static_cast<std::uint32_t>(input[cursor++]) << (8u * i);
        ++literalLength;
      }
      if (literalLength > end - cursor || output.size() + literalLength > decodedSize) return false;
      output.insert(output.end(), input.begin() + static_cast<std::ptrdiff_t>(cursor), input.begin() + static_cast<std::ptrdiff_t>(cursor + literalLength));
      cursor += literalLength;
      continue;
    }
    std::uint32_t copyLength = 0, distance = 0;
    if (kind == 1u) {
      copyLength = ((tag >> 2u) & 7u) + 4u;
      if (cursor >= end) return false;
      distance = ((static_cast<std::uint32_t>(tag) & 0xe0u) << 3u) | input[cursor++];
    } else if (kind == 2u) {
      copyLength = (tag >> 2u) + 1u;
      if (cursor + 2u > end) return false;
      distance = static_cast<std::uint32_t>(input[cursor]) | (static_cast<std::uint32_t>(input[cursor + 1u]) << 8u);
      cursor += 2u;
    } else {
      copyLength = (tag >> 2u) + 1u;
      if (cursor + 4u > end) return false;
      distance = static_cast<std::uint32_t>(input[cursor]) |
        (static_cast<std::uint32_t>(input[cursor + 1u]) << 8u) |
        (static_cast<std::uint32_t>(input[cursor + 2u]) << 16u) |
        (static_cast<std::uint32_t>(input[cursor + 3u]) << 24u);
      cursor += 4u;
    }
    if (distance == 0u || distance > output.size() || output.size() + copyLength > decodedSize) return false;
    for (std::uint32_t i = 0; i < copyLength; ++i) output.push_back(output[output.size() - distance]);
  }
  return output.size() == decodedSize && cursor == end;
}

void recordInstanceBaselineEntry(const std::string& text, std::vector<std::uint8_t> raw,
    DemoNetworkSummary& summary) {
  std::uint32_t classId = 0;
  bool validClassId = !text.empty();
  if (validClassId) {
    for (const char character : text) {
      if (character < '0' || character > '9') { validClassId = false; break; }
      const std::uint32_t digit = static_cast<std::uint32_t>(character - '0');
      if (classId > (std::numeric_limits<std::uint16_t>::max() - digit) / 10u) {
        validClassId = false;
        break;
      }
      classId = classId * 10u + digit;
    }
  }
  if (!validClassId) {
    ++summary.instanceBaselineClassParseFailures;
    return;
  }
  ++summary.instanceBaselineClassTextCount;
  summary.instanceBaselines[static_cast<std::uint16_t>(classId)] = std::move(raw);
  ++summary.instanceBaselineEntryCount;
}

bool readInstanceBaselinePayload(const std::vector<std::uint8_t>& payload, std::uint32_t entryCount,
    std::uint32_t maxEntries, bool fixedUserData, std::uint32_t fixedUserDataBits,
    DemoNetworkSummary& summary) {
  MessageBits data(payload);
  std::uint32_t indexBits = 0;
  const std::uint32_t boundedMaxEntries = std::max<std::uint32_t>(1u, maxEntries);
  while ((1u << indexBits) < boundedMaxEntries && indexBits < 31u) ++indexBits;
  std::int32_t lastIndex = -1;
  std::vector<std::string> history;
  history.reserve(std::min<std::uint32_t>(entryCount, 32u));
  for (std::uint32_t entry = 0; entry < entryCount; ++entry) {
    std::uint32_t sequential = 0, index = 0;
    if (!data.read(1, sequential)) return false;
    if (sequential) index = static_cast<std::uint32_t>(lastIndex + 1);
    else if (!data.read(indexBits, index)) return false;
    lastIndex = static_cast<std::int32_t>(index);

    std::string text;
    std::uint32_t hasText = 0;
    if (!data.read(1, hasText)) return false;
    if (hasText) {
      std::uint32_t hasHistory = 0;
      if (!data.read(1, hasHistory)) return false;
      if (hasHistory) {
        std::uint32_t historyIndex = 0, copyCount = 0;
        if (!data.read(5, historyIndex) || !data.read(5, copyCount)) return false;
        std::string rest;
        if (!data.readString(rest, 4096)) return false;
        if (historyIndex < history.size() && copyCount <= history[historyIndex].size()) {
          text = history[historyIndex].substr(0, copyCount);
          text += rest;
        } else {
          text = std::move(rest);
        }
      } else if (!data.readString(text, 4096)) {
        return false;
      }
    }
    history.push_back(text);
    if (history.size() > 32u) history.erase(history.begin());

    std::uint32_t hasUserData = 0;
    if (!data.read(1, hasUserData)) return false;
    if (hasUserData) {
      std::uint32_t userDataBits = fixedUserData ? fixedUserDataBits : 0;
      if (!fixedUserData) {
        std::uint32_t userDataBytes = 0;
        if (!data.read(14, userDataBytes) || userDataBytes > 1024u) return false;
        userDataBits = userDataBytes * 8u;
      }
      std::vector<std::uint8_t> raw((static_cast<std::size_t>(userDataBits) + 7u) / 8u, 0);
      for (std::uint32_t bit = 0; bit < userDataBits; ++bit) {
        std::uint32_t value = 0;
        if (!data.read(1, value)) return false;
        raw[bit / 8u] |= static_cast<std::uint8_t>(value << (bit % 8u));
      }
      // instancebaseline's text is the server class id; its entry index is not.
      recordInstanceBaselineEntry(text, std::move(raw), summary);
    }
  }
  return true;
}

bool readDemoStringTablesPayload(const std::vector<std::uint8_t>& payload, DemoNetworkSummary& summary) {
  MessageBits bits(payload);
  std::uint32_t tableCount = 0;
  if (!bits.read(8, tableCount) || tableCount > 64u) return false;
  for (std::uint32_t table = 0; table < tableCount; ++table) {
    std::string name;
    std::uint32_t entryCount = 0;
    if (!bits.readString(name) || !bits.read(16, entryCount)) return false;
    const bool isInstanceBaseline = name == "instancebaseline";
    for (std::uint32_t entry = 0; entry < entryCount; ++entry) {
      std::string text;
      std::uint32_t hasUserData = 0;
      if (!bits.readString(text) || !bits.read(1, hasUserData)) return false;
      std::vector<std::uint8_t> raw;
      if (hasUserData) {
        std::uint32_t userBytes = 0;
        if (!bits.read(16, userBytes) || userBytes > 1024u) return false;
        raw.assign(userBytes, 0);
        for (std::uint32_t bit = 0; bit < userBytes * 8u; ++bit) {
          std::uint32_t value = 0;
          if (!bits.read(1, value)) return false;
          raw[bit / 8u] |= static_cast<std::uint8_t>(value << (bit % 8u));
        }
      }
      if (isInstanceBaseline && hasUserData) recordInstanceBaselineEntry(text, std::move(raw), summary);
    }
    std::uint32_t hasClientEntries = 0;
    if (!bits.read(1, hasClientEntries)) return false;
    if (hasClientEntries) {
      std::uint32_t clientCount = 0;
      if (!bits.read(16, clientCount)) return false;
      for (std::uint32_t entry = 0; entry < clientCount; ++entry) {
        std::string text;
        std::uint32_t hasUserData = 0;
        if (!bits.readString(text) || !bits.read(1, hasUserData)) return false;
        if (hasUserData) {
          std::uint32_t userBytes = 0;
          if (!bits.read(16, userBytes) || userBytes > 1024u || !bits.skip(static_cast<std::size_t>(userBytes) * 8u)) return false;
        }
      }
    }
    if (summary.stringTableCount < 32u) {
      summary.stringTableById[static_cast<std::uint32_t>(summary.stringTableCount)] = name;
      summary.stringTableMaxEntries[static_cast<std::uint32_t>(summary.stringTableCount)] = entryCount;
    }
    if (summary.stringTableNames.size() < 32u) summary.stringTableNames.push_back(name);
    if (name == "soundprecache") {
      summary.soundPrecacheTableId = static_cast<std::uint32_t>(summary.stringTableCount);
      summary.soundPrecacheMaxEntries = entryCount;
    }
    ++summary.stringTableCount;
  }
  return true;
}

bool readCreateStringTable(MessageBits& bits, DemoNetworkSummary& summary) {
  std::string name;
  std::uint32_t maxEntries = 0, entryCount = 0, payloadBits = 0, fixedUserData = 0, fixedUserDataSize = 0, fixedUserDataBits = 0, compressed = 0;
  if (!bits.readString(name) || !bits.read(16, maxEntries)) return false;
  std::uint32_t entryIndexBits = 0;
  const std::uint32_t boundedMaxEntries = std::max<std::uint32_t>(1u, maxEntries);
  while ((1u << entryIndexBits) < boundedMaxEntries && entryIndexBits < 31u) ++entryIndexBits;
  if (!bits.read(entryIndexBits + 1u, entryCount)) return false;
  if ((summary.networkProtocol > 23 && !bits.readVarInt(payloadBits)) ||
      (summary.networkProtocol <= 23 && !bits.read(20, payloadBits))) return false;
  if (!bits.read(1, fixedUserData)) return false;
  if (fixedUserData && (!bits.read(12, fixedUserDataSize) || !bits.read(4, fixedUserDataBits))) return false;
  if (!bits.read(1, compressed) || payloadBits > bits.remaining()) return false;
  std::vector<std::uint8_t> payload((static_cast<std::size_t>(payloadBits) + 7u) / 8u, 0);
  for (std::size_t bit = 0; bit < payloadBits; ++bit) {
    std::uint32_t value = 0;
    if (!bits.read(1, value)) return false;
    payload[bit / 8u] |= static_cast<std::uint8_t>(value << (bit % 8u));
  }
  if (name == "soundprecache") {
    std::vector<std::uint8_t> decodedPayload;
    const std::vector<std::uint8_t>* tablePayload = &payload;
    bool decoded = compressed == 0;
    if (compressed != 0 && payload.size() >= 12u) {
      const std::uint32_t decompressedSize = static_cast<std::uint32_t>(payload[0]) |
        (static_cast<std::uint32_t>(payload[1]) << 8u) |
        (static_cast<std::uint32_t>(payload[2]) << 16u) |
        (static_cast<std::uint32_t>(payload[3]) << 24u);
      const std::uint32_t compressedSize = static_cast<std::uint32_t>(payload[4]) |
        (static_cast<std::uint32_t>(payload[5]) << 8u) |
        (static_cast<std::uint32_t>(payload[6]) << 16u) |
        (static_cast<std::uint32_t>(payload[7]) << 24u);
      const bool snap = payload[8] == 'S' && payload[9] == 'N' && payload[10] == 'A' && payload[11] == 'P';
      const bool lzss = payload[8] == 'L' && payload[9] == 'Z' && payload[10] == 'S' && payload[11] == 'S';
      if ((snap || lzss) && compressedSize >= 4u
          && 12u + static_cast<std::size_t>(compressedSize - 4u) <= payload.size()
          && decompressedSize <= 100u * 1024u * 1024u) {
        decoded = snap
          ? decodeSnappyRaw(payload, 12u, compressedSize - 4u, decompressedSize, decodedPayload)
          : decodeLzss(payload, 12u, compressedSize - 4u, decompressedSize, decodedPayload);
        if (decoded) tablePayload = &decodedPayload;
      }
    }
    if (!decoded) {
      ++summary.soundPrecacheDecodeFailures;
    } else {
    MessageBits table(*tablePayload);
    std::vector<std::string> history;
    history.reserve(32);
    std::uint32_t lastIndex = 0;
    bool haveLastIndex = false;
    bool tableOk = true;
    for (std::uint32_t entry = 0; entry < entryCount; ++entry) {
      std::uint32_t sequential = 0, index = 0;
      std::uint32_t value = 0;
      if (!table.read(1, sequential)) { tableOk = false; break; }
      if (sequential) index = haveLastIndex ? lastIndex + 1u : 0u;
      else if (!table.read(entryIndexBits, index)) { tableOk = false; break; }
      lastIndex = index; haveLastIndex = true;
      std::string text;
      if (!table.read(1, value)) { tableOk = false; break; }
      if (value) {
        if (!table.read(1, value)) { tableOk = false; break; }
        if (value) {
          std::uint32_t historyIndex = 0, copyCount = 0;
          if (!table.read(5, historyIndex) || !table.read(5, copyCount)) { tableOk = false; break; }
          std::string rest;
          if (!table.readString(rest, 4096)) { tableOk = false; break; }
          if (historyIndex < history.size() && copyCount <= history[historyIndex].size()) text = history[historyIndex].substr(0, copyCount) + rest;
          else text = std::move(rest);
        } else if (!table.readString(text, 4096)) { tableOk = false; break; }
      }
      history.push_back(text);
      if (history.size() > 32u) history.erase(history.begin());
      if (!table.read(1, value)) { tableOk = false; break; }
      if (value) {
        if (fixedUserData != 0u) {
          if (!table.skip(fixedUserDataBits)) { tableOk = false; break; }
        } else {
          std::uint32_t userBytes = 0;
          if (!table.read(14, userBytes) || userBytes > 1024u || !table.skip(static_cast<std::size_t>(userBytes) * 8u)) { tableOk = false; break; }
        }
      }
      if (!text.empty() && index <= 0xffffu) summary.soundPrecache[static_cast<std::uint16_t>(index)] = std::move(text);
    }
    if (!tableOk) ++summary.soundPrecacheDecodeFailures;
    }
  }
  if (name == "instancebaseline") {
    std::vector<std::uint8_t> decodedPayload;
    const std::vector<std::uint8_t>* tablePayload = &payload;
    bool decoded = !compressed;
    if (compressed) {
      ++summary.instanceBaselineCompressedCount;
      if (payload.size() >= 12u) {
        const std::uint32_t decompressedSize = static_cast<std::uint32_t>(payload[0]) |
          (static_cast<std::uint32_t>(payload[1]) << 8u) |
          (static_cast<std::uint32_t>(payload[2]) << 16u) |
          (static_cast<std::uint32_t>(payload[3]) << 24u);
        const std::uint32_t compressedSize = static_cast<std::uint32_t>(payload[4]) |
          (static_cast<std::uint32_t>(payload[5]) << 8u) |
          (static_cast<std::uint32_t>(payload[6]) << 16u) |
          (static_cast<std::uint32_t>(payload[7]) << 24u);
        summary.instanceBaselineDecompressedBytes = decompressedSize;
        summary.instanceBaselineCompressedBytes = compressedSize;
        summary.instanceBaselineMagic = static_cast<std::uint32_t>(payload[8]) |
          (static_cast<std::uint32_t>(payload[9]) << 8u) |
          (static_cast<std::uint32_t>(payload[10]) << 16u) |
          (static_cast<std::uint32_t>(payload[11]) << 24u);
        const bool snap = payload[8] == 'S' && payload[9] == 'N' && payload[10] == 'A' && payload[11] == 'P';
        const bool lzss = payload[8] == 'L' && payload[9] == 'Z' && payload[10] == 'S' && payload[11] == 'S';
        if ((snap || lzss) && compressedSize >= 4u && 12u + static_cast<std::size_t>(compressedSize - 4u) <= payload.size()) {
          decoded = snap
            ? decodeSnappyRaw(payload, 12u, compressedSize - 4u, decompressedSize, decodedPayload)
            : decodeLzss(payload, 12u, compressedSize - 4u, decompressedSize, decodedPayload);
          if (decoded) tablePayload = &decodedPayload;
        }
      }
    }
    if (!decoded || !readInstanceBaselinePayload(*tablePayload, entryCount, maxEntries, fixedUserData != 0, fixedUserDataBits, summary)) {
      ++summary.instanceBaselineDecodeFailures;
    }
  }
  if (summary.stringTableCount < 32u) {
    summary.stringTableById[static_cast<std::uint32_t>(summary.stringTableCount)] = name;
    summary.stringTableMaxEntries[static_cast<std::uint32_t>(summary.stringTableCount)] = maxEntries;
  }
  if (name == "soundprecache") {
    summary.soundPrecacheTableId = static_cast<std::uint32_t>(summary.stringTableCount);
    summary.soundPrecacheMaxEntries = maxEntries;
    summary.soundPrecacheFixedBits = fixedUserData ? fixedUserDataBits : 0u;
    summary.soundPrecacheCompressed = compressed != 0;
  }
  ++summary.stringTableCount;
  if (summary.stringTableNames.size() < 32) summary.stringTableNames.push_back(name);
  return true;
}

}

bool scanKnownDemoMessages(const std::filesystem::path& path, const DemoIndex& index, DemoNetworkSummary& summary) {
  const int networkProtocol = summary.networkProtocol;
  summary = {};
  summary.networkProtocol = networkProtocol;
  if (index.entries.empty()) return false;
  summary.entityClassByIndex.assign(2048u, -1);
  summary.entityStates.assign(2048u, EntityState{});
  std::ifstream file(path, std::ios::binary); if (!file) return false;
  for (const auto& entry : index.entries) {
    if (entry.command == 6) {
      std::vector<std::uint8_t> dataTablePayload;
      if (!readEntryPayload(file, entry, dataTablePayload, 128u * 1024u * 1024u)) {
        ++summary.malformedPackets;
      } else {
        MessageBits dataTableBits(dataTablePayload);
        if (!readDataTableBody(dataTableBits, summary)) ++summary.malformedPackets;
      }
      continue;
    }
    if (entry.command == 8) {
      std::vector<std::uint8_t> stringTablesPayload;
      if (!readEntryPayload(file, entry, stringTablesPayload, 128u * 1024u * 1024u)
          || !readDemoStringTablesPayload(stringTablesPayload, summary)) {
        ++summary.malformedPackets;
      }
      continue;
    }
    if (entry.command != 1 && entry.command != 2) continue;
    std::vector<std::uint8_t> payload;
    if (!readEntryPayload(file, entry, payload, 128u * 1024u * 1024u)) { ++summary.malformedPackets; continue; }
    MessageBits bits(payload); bool decodedAny = false; bool packetValid = true;
    std::int32_t networkTick = summary.lastNetworkTick >= 0 ? summary.lastNetworkTick : entry.tick;
    while (bits.remaining() > 6) {
      const std::size_t messageBit = bits.offsetBits();
      std::uint32_t type = 0; if (!bits.read(6, type)) { packetValid = false; break; }
      if (type == 0) { decodedAny = true; continue; }
      if (type == 3) { std::uint32_t tick = 0, frameTime = 0, deviation = 0; if (!bits.read(32, tick) || !bits.read(16, frameTime) || !bits.read(16, deviation)) packetValid = false; else { networkTick = tick <= 0x7fffffffu ? static_cast<std::int32_t>(tick) : -1; summary.lastNetworkTick = networkTick; summary.lastNetworkTickRaw = tick; summary.lastNetworkTickRawValid = true; ++summary.netTickCount; decodedAny = true; } }
      else if (type == 4) { if (!readStringCommand(bits, summary)) packetValid = false; else { decodedAny = true; } }
      else if (type == 5) { if (!readSetConVar(bits, summary)) packetValid = false; else { decodedAny = true; } }
      else if (type == 6) { if (!readSignonState(bits, summary)) packetValid = false; else { decodedAny = true; } }
      else if (type == 7) { std::string printText; if (!bits.readString(printText)) packetValid = false; else { ++summary.printCount; decodedAny = true; } }
      else if (type == 8) { if (!readServerInfo(bits, summary)) packetValid = false; else { ++summary.serverInfoCount; decodedAny = true; } }
      else if (type == 9) { if (!readSendTable(bits, summary)) packetValid = false; else { decodedAny = true; } }
      else if (type == 10) { if (!readClassInfo(bits, summary)) packetValid = false; else { decodedAny = true; } }
      else if (type == 12) { if (!readCreateStringTable(bits, summary)) packetValid = false; else { decodedAny = true; } }
      else if (type == 13) { if (!readUpdateStringTable(bits, summary)) packetValid = false; else { decodedAny = true; } }
      else if (type == 14) { if (!readVoiceInit(bits, summary)) packetValid = false; else { decodedAny = true; } }
      else if (type == 15) { if (!readVoiceData(bits, summary)) packetValid = false; else { decodedAny = true; } }
      else if (type == 18) { if (!readSetView(bits, summary, networkTick)) packetValid = false; else { decodedAny = true; } }
      else if (type == 17) { if (!readSounds(bits, summary, entry.tick)) packetValid = false; else { decodedAny = true; } }
      else if (type == 19) { if (!readFixAngle(bits, summary, networkTick)) packetValid = false; else { decodedAny = true; } }
      else if (type == 23) { if (!readUserMessage(bits, summary)) packetValid = false; else { decodedAny = true; } }
      else if (type == 24) { if (!readEntityMessage(bits, summary)) packetValid = false; else { decodedAny = true; } }
      else if (type == 25) { if (!readGameEvent(bits, summary)) packetValid = false; else { decodedAny = true; } }
      else if (type == 26) { if (summary.firstPacketEntitiesMessageBit < 0) summary.firstPacketEntitiesMessageBit = static_cast<std::int64_t>(messageBit); if (!readPacketEntities(bits, summary, networkTick)) packetValid = false; else { decodedAny = true; } }
      else if (type == 27) { if (!readTempEntities(bits, summary, networkTick)) packetValid = false; else { decodedAny = true; } }
      else if (type == 28) { if (!readPrefetch(bits, summary)) packetValid = false; else { decodedAny = true; } }
      else if (type == 30) { if (!readGameEventList(bits, summary)) packetValid = false; else { decodedAny = true; } }
      else if (type == 31) { if (!readGetCvarValue(bits, summary)) packetValid = false; else { decodedAny = true; } }
      else { ++summary.unknownMessagePackets; if (summary.unknownMessageTypes.size() < 16) summary.unknownMessageTypes.push_back(type); packetValid = false; break; }
      if (!packetValid) break;
    }
    ++summary.packetsScanned; if (!packetValid || !decodedAny) ++summary.malformedPackets;
  }
  for (const auto& entity : summary.entityStates) {
    for (const auto& [name, value] : entity.properties) {
      if (name.find("m_vecOrigin") != std::string::npos) ++summary.entityOriginStateCount;
      if (name.find("m_iHealth") != std::string::npos) ++summary.entityHealthStateCount;
      if (name.find("m_iTeamNum") != std::string::npos) ++summary.entityTeamStateCount;
      if (name.find("m_iClass") != std::string::npos) ++summary.entityClassStateCount;
      if (name.find("m_hActiveWeapon") != std::string::npos) ++summary.entityWeaponStateCount;
      if (name.find("m_iObserver") != std::string::npos || name.find("m_hObserver") != std::string::npos) ++summary.entityObserverStateCount;
    }
  }
  for (auto& event : summary.decodedSoundEvents) {
    const auto it = summary.soundPrecache.find(event.soundIndex);
    if (it == summary.soundPrecache.end()) {
      ++summary.decodedSoundResourceMisses;
    } else {
      event.resourceName = it->second;
      ++summary.decodedSoundResourceMatches;
    }
  }
  return summary.packetsScanned > 0;
}

EntitySnapshotQueryStatus queryEntitySnapshotAtOrBeforeTick(
    const DemoNetworkSummary& summary, std::int32_t tick, std::vector<EntityState>& states) {
  if (summary.entityHistoryHasGap) return EntitySnapshotQueryStatus::Gap;
  if (summary.entityHistoryCheckpoints.empty()) return EntitySnapshotQueryStatus::NoHistory;
  const auto checkpoint = std::upper_bound(
      summary.entityHistoryCheckpoints.begin(), summary.entityHistoryCheckpoints.end(), tick,
      [](std::int32_t value, const EntityHistoryCheckpoint& item) { return value < item.tick; });
  if (checkpoint == summary.entityHistoryCheckpoints.begin()) return EntitySnapshotQueryStatus::TickBeforeHistory;
  const auto& base = *std::prev(checkpoint);
  states = base.states;
  for (const auto& packet : summary.entityHistoryPackets) {
    if (packet.packetOrdinal <= base.packetOrdinal || packet.tick > tick) continue;
    // History events contain the complete post-update state for every changed
    // entity. The live decoder applies deltas in stream order, so replaying a
    // retained window does not require retaining the packet named by
    // deltaFrom (that reference may intentionally point outside the bounded
    // history window).
    const std::size_t end = packet.firstEvent + packet.eventCount;
    if (end > summary.entityHistoryEvents.size()) return EntitySnapshotQueryStatus::Gap;
    for (std::size_t i = packet.firstEvent; i < end; ++i) {
      const auto& event = summary.entityHistoryEvents[i];
      if (event.entityIndex >= states.size()) states.resize(2048u);
      states[event.entityIndex] = event.removed ? EntityState{} : event.state;
    }
  }
  return EntitySnapshotQueryStatus::Available;
}

bool findEntitySnapshotAtOrBeforeTick(const DemoNetworkSummary& summary, std::int32_t tick,
                                      std::vector<EntityState>& states) {
  return queryEntitySnapshotAtOrBeforeTick(summary, tick, states) ==
         EntitySnapshotQueryStatus::Available;
}

bool findTempEntityEventsInTickRange(const DemoNetworkSummary& summary, std::int32_t firstTick,
                                     std::int32_t lastTick, std::vector<TempEntityEvent>& events) {
  events.clear();
  if (firstTick > lastTick || summary.tempEntityEvents.empty()) return false;
  const auto begin = std::lower_bound(
      summary.tempEntityEvents.begin(), summary.tempEntityEvents.end(), firstTick,
      [](const TempEntityEvent& event, std::int32_t tick) { return event.tick < tick; });
  const auto end = std::upper_bound(
      begin, summary.tempEntityEvents.end(), lastTick,
      [](std::int32_t tick, const TempEntityEvent& event) { return tick < event.tick; });
  events.assign(begin, end);
  return !events.empty();
}

bool findObserverCameraAtOrBeforeTick(const DemoNetworkSummary& summary, std::int32_t tick,
                                      ObserverCameraTrackSample& sample) {
  sample = {};
  bool found = false;
  for (const auto& candidate : summary.observerCameraTrack) {
    if (candidate.tick > tick) continue;
    if (candidate.hasViewEntity) {
      sample.hasViewEntity = true;
      sample.viewEntity = candidate.viewEntity;
    }
    if (candidate.hasAngles) {
      sample.hasAngles = true;
      sample.anglesRelative = candidate.anglesRelative;
      std::copy(std::begin(candidate.angles), std::end(candidate.angles), std::begin(sample.angles));
    }
    if (candidate.hasViewEntity || candidate.hasAngles) {
      sample.tick = candidate.tick;
      found = true;
    }
  }
  return found;
}

bool buildAssetReferenceList(DemoNetworkSummary& summary, std::vector<AssetReference>& references) {
  references.clear();
  summary.assetModelIndexKnown = summary.assetWeaponKnown = summary.assetItemDefKnown = 0;
  summary.assetPaintKitKnown = summary.assetSkinKnown = summary.assetQualityKnown = 0;
  summary.assetIdentityUnknown = 0;
  summary.assetModelPathKnown = summary.assetWeaponClassKnown = 0;
  for (std::size_t entityIndex = 0; entityIndex < summary.entityStates.size() && entityIndex < 2048u; ++entityIndex) {
    const auto& state = summary.entityStates[entityIndex];
    if (state.classId < 0) continue;
    AssetReference reference;
    reference.entityIndex = static_cast<std::uint16_t>(entityIndex);
    reference.classId = state.classId;
    for (const auto& classSchema : summary.serverClassSchemas) {
      if (classSchema.id == static_cast<std::uint32_t>(state.classId)) {
        reference.className = classSchema.name;
        break;
      }
    }
    auto findInt = [&state](std::initializer_list<const char*> names, bool& known, std::int64_t& target) {
      for (const auto* suffix : names) {
        for (const auto& [name, value] : state.properties) {
          if ((name == suffix || (name.size() > std::strlen(suffix) &&
              name.compare(name.size() - std::strlen(suffix), std::strlen(suffix), suffix) == 0)) &&
              value.type == SendPropType::Int) {
            known = true;
            target = value.intValue;
            return;
          }
        }
      }
    };
    auto findString = [&state](std::initializer_list<const char*> names, bool& known, std::string& target) {
      for (const auto* suffix : names) {
        for (const auto& [name, value] : state.properties) {
          if ((name == suffix || (name.size() > std::strlen(suffix) &&
              name.compare(name.size() - std::strlen(suffix), std::strlen(suffix), suffix) == 0)) &&
              value.type == SendPropType::String && !value.stringValue.empty()) {
            known = true;
            target = value.stringValue;
            return;
          }
        }
      }
    };
    findString({"m_ModelName", "m_iszModel", "m_szModel"}, reference.hasModelPath, reference.modelPath);
    findString({"m_iClassName", "m_szClassName"}, reference.hasWeaponClass, reference.weaponClass);
    findInt({"m_nModelIndex", "m_iModelIndex"}, reference.hasModelIndex, reference.modelIndex);
    for (const auto& [name, value] : state.properties) {
      if (value.type != SendPropType::String || value.stringValue.empty()) continue;
      if (name == "m_ModelName" || name == "m_iszModelName" || name.find("ModelName") != std::string::npos) {
        reference.hasModelPath = true;
        reference.modelPath = value.stringValue;
        break;
      }
    }
    findInt({"m_hActiveWeapon", "m_hWeapon"}, reference.hasWeapon, reference.weapon);
    findInt({"m_iItemDefinitionIndex"}, reference.hasItemDefIndex, reference.itemDefIndex);
    findInt({"m_nFallbackPaintKit", "m_iPaintKit"}, reference.hasPaintKit, reference.paintKit);
    findInt({"m_nSkin"}, reference.hasSkin, reference.skin);
    findInt({"m_iEntityQuality"}, reference.hasQuality, reference.quality);
    if (reference.hasModelIndex) ++summary.assetModelIndexKnown;
    if (reference.hasWeapon) ++summary.assetWeaponKnown;
    if (reference.hasItemDefIndex) ++summary.assetItemDefKnown;
    if (reference.hasPaintKit) ++summary.assetPaintKitKnown;
    if (reference.hasSkin) ++summary.assetSkinKnown;
    if (reference.hasQuality) ++summary.assetQualityKnown;
    if (reference.hasModelPath) ++summary.assetModelPathKnown;
    if (reference.hasWeaponClass) ++summary.assetWeaponClassKnown;
    if (!reference.hasModelPath && !reference.hasWeaponClass && !reference.hasModelIndex &&
        !reference.hasWeapon && !reference.hasItemDefIndex && !reference.hasPaintKit &&
        !reference.hasSkin && !reference.hasQuality) {
      ++summary.assetIdentityUnknown;
    }
    references.push_back(std::move(reference));
  }
  return true;
}

const char* demoRecordingTypeName(DemoRecordingType type) {
  switch (type) {
    case DemoRecordingType::SourceTv: return "SourceTV";
    case DemoRecordingType::PovHeuristic: return "POV (heuristic)";
    default: return "unknown";
  }
}

} // namespace tf2::native
