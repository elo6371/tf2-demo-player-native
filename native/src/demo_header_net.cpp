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
namespace {

void flattenSendTableSchemas(DemoNetworkSummary& summary);
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
} // namespace

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
namespace {

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
} // namespace

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
namespace {

} // namespace

bool readFixAngle(MessageBits& bits, DemoNetworkSummary& summary, std::int32_t packetTick) {
  std::uint32_t relative = 0;
  if (!bits.read(1, relative)) return false;
  float angles[3] = {};
  for (float& angle : angles) {
    std::uint32_t raw = 0;
    if (!bits.read(16, raw)) return false;
    angle = demoBitAngleToDegrees(raw, 16);
  }
  if (relative != 0 && summary.lastFixAngleValid) {
    for (int axis = 0; axis < 3; ++axis) {
      angles[axis] = std::fmod(angles[axis] + summary.lastFixAngle[axis], 360.0f);
      if (angles[axis] < 0.0f) angles[axis] += 360.0f;
    }
  }
  summary.lastFixAngle[0] = angles[0];
  summary.lastFixAngle[1] = angles[1];
  summary.lastFixAngle[2] = angles[2];
  summary.lastFixAngleValid = true;
  summary.lastFixAngleRelative = relative != 0;
  ++summary.fixAngleCount;
  ++summary.fixAngleDecoded;
  if (summary.fixAngleSamples.size() >= kMaxViewSamples) ++summary.viewSamplesDropped;
  else {
    DemoViewSample sample;
    sample.tick = packetTick;
    sample.angles[0] = angles[0]; sample.angles[1] = angles[1]; sample.angles[2] = angles[2];
    sample.hasAngles = true;
    sample.source = DemoViewSource::FixAngle;
    summary.fixAngleSamples.push_back(sample);
  }
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
  // MAX_SOUND_INDEX_BITS is 14 from protocol 23 onward, not 24. First source:
  // Valve source-sdk-2013, src/public/soundinfo.h -- SoundInfo_t::ReadDelta()
  // does `if ( nProtoVersion > 22 ) READ_DELTA_UINT( nSoundNum,
  // MAX_SOUND_INDEX_BITS )` and src/public/soundflags.h defines
  // MAX_SOUND_INDEX_BITS 14. The local Rust reference agrees:
  // work/_refs_demostf/src/demo/message/prefetch.rs uses
  // `if protocol_version > 22 { 14 } else { 13 }`.
  // Reading 13 bits for a protocol-23 demo shifts every later message in the
  // packet by one bit, so the rest of the packet decodes as garbage types.
  const std::uint32_t width = summary.networkProtocol > 22 ? 14u : 13u;
  if (!bits.read(width, soundIndex)) return false;
  ++summary.prefetchCount;
  return true;
}
namespace {

// --- Message types the local reference parser implements and this decoder
// --- used to abandon the whole packet on.
// Numbers are Valve's SVC_MESSAGES / NET_Messages enumerations
// (source-sdk-2013 src/engine/netmessages.h). Layouts are from the local Rust
// reference, work/_refs_demostf/src/demo/message/:
//   generated.rs -- FileMessage, SetPauseMessage, MenuMessage,
//                   CmdKeyValuesMessage (primitive-only structs)
//   bspdecal.rs  -- BSPDecalMessage (uses read_bit_coord, NOT BitCoordMP)
// Leaving any of these unimplemented does not merely skip a message: the
// message loop cannot know its length, so it drops the rest of the packet,
// including the svc_PacketEntities that follows in the same packet.

// bf_read::ReadBitCoord -- source-sdk-2013 src/public/bitbuf.cpp.
// 1 bit has_int, 1 bit has_frac, and only if either is set: 1 bit sign,
// 14-bit integer part, 5-bit fractional part. Max 22 bits.
bool skipBitCoord(MessageBits& bits) {
  std::uint32_t hasInt = 0, hasFrac = 0;
  if (!bits.read(1, hasInt) || !bits.read(1, hasFrac)) return false;
  if (!hasInt && !hasFrac) return true;
  std::uint32_t sign = 0;
  if (!bits.read(1, sign)) return false;
  if (hasInt && !bits.skip(14)) return false;
  if (hasFrac && !bits.skip(5)) return false;
  return true;
}
} // namespace

// net_File = 2: transfer_id(32) + null-terminated name + requested(1).
bool readFileMessage(MessageBits& bits, DemoNetworkSummary& summary) {
  std::uint32_t transferId = 0, requested = 0;
  std::string fileName;
  if (!bits.read(32, transferId) || !bits.readString(fileName) || !bits.read(1, requested)) return false;
  ++summary.fileMessageCount;
  return true;
}

// svc_SetPause = 11: 1 bit. This is the type that used to truncate the packet
// that carried the demo's first svc_PacketEntities after a pause.
bool readSetPause(MessageBits& bits, DemoNetworkSummary& summary) {
  std::uint32_t pause = 0;
  if (!bits.read(1, pause)) return false;
  ++summary.setPauseCount;
  summary.setPauseState = pause != 0;
  return true;
}

// svc_BSPDecal = 21: 3 presence flags, a conditional BitCoord per axis, a
// 9-bit texture index, a 1-bit "has entity/model" flag with an 11-bit entity
// and 13-bit model index, then a low-priority bit.
bool readBspDecal(MessageBits& bits, DemoNetworkSummary& summary) {
  std::uint32_t hasX = 0, hasY = 0, hasZ = 0;
  if (!bits.read(1, hasX) || !bits.read(1, hasY) || !bits.read(1, hasZ)) return false;
  if (hasX && !skipBitCoord(bits)) return false;
  if (hasY && !skipBitCoord(bits)) return false;
  if (hasZ && !skipBitCoord(bits)) return false;
  std::uint32_t textureIndex = 0;
  if (!bits.read(9, textureIndex)) return false;
  std::uint32_t hasIndices = 0;
  if (!bits.read(1, hasIndices)) return false;
  if (hasIndices) {
    std::uint32_t entIndex = 0, modelIndex = 0;
    if (!bits.read(11, entIndex) || !bits.read(13, modelIndex)) return false;
  }
  std::uint32_t lowPriority = 0;
  if (!bits.read(1, lowPriority)) return false;
  ++summary.bspDecalCount;
  return true;
}

// svc_Menu = 29: kind(16) + byte length(16) + that many bytes.
bool readMenu(MessageBits& bits, DemoNetworkSummary& summary) {
  std::uint32_t kind = 0, length = 0;
  if (!bits.read(16, kind) || !bits.read(16, length)) return false;
  const std::size_t payloadBits = static_cast<std::size_t>(length) * 8u;
  if (payloadBits > bits.remaining() || !bits.skip(payloadBits)) return false;
  ++summary.menuCount;
  return true;
}

// svc_CmdKeyValues = 32: byte length(32) + that many bytes.
bool readCmdKeyValues(MessageBits& bits, DemoNetworkSummary& summary) {
  std::uint32_t length = 0;
  if (!bits.read(32, length)) return false;
  const std::size_t payloadBits = static_cast<std::size_t>(length) * 8u;
  if (payloadBits > bits.remaining() || !bits.skip(payloadBits)) return false;
  ++summary.cmdKeyValuesCount;
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
namespace {

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
} // namespace

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
namespace {

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
} // namespace

bool readSendPropValue(MessageBits& bits, const SendPropSchema& prop, EntityPropertyValue& value) {
  return readSendPropValueDepth(bits, prop, value, 0);
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

bool readSetView(MessageBits& bits, DemoNetworkSummary& summary) {
  std::uint32_t entityIndex = 0;
  if (!bits.read(11, entityIndex)) return false;
  ++summary.setViewCount;
  summary.lastViewEntity = entityIndex;
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
  std::uint32_t value = 0, hltv = 0, dedicated = 0, replay = 0;
  if (!bits.read(16, value) || !bits.read(32, value) || !bits.read(1, hltv) || !bits.read(1, dedicated) || !bits.read(32, value) || !bits.read(16, value)) return false;
  for (int i = 0; i < 16; ++i) if (!bits.read(8, value)) return false;
  if (!bits.read(8, value) || !bits.read(8, value) || !bits.read(32, value)) return false;
  std::string platform, game, map, skybox, server;
  if (!bits.readStringLimit(platform, 1) || !bits.readString(game) || !bits.readString(map) || !bits.readString(skybox) || !bits.readString(server) || !bits.read(1, replay)) return false;
  summary.serverMap = map; summary.serverName = server;
  summary.serverInfoHltv = hltv != 0;
  summary.serverInfoDedicated = dedicated != 0;
  summary.serverInfoReplayBit = replay != 0;
  summary.sourceTv = summary.serverInfoHltv;
  return true;
}

} // namespace tf2::native
