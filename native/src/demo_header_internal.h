#pragma once

#include "demo_header.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace tf2::native {

// T63-F1: one cap for every view-sample stream (cmdinfo, signon, fixangle).
// 65536 was below what a SourceTV demo can carry (bagel: 73121 fix-angle
// samples), so samples were dropped silently and the surviving sequence looked
// ordered. Shared with the t66-camera branch: keep this line identical there.

inline constexpr std::size_t kMaxViewSamples = 1048576u;

class MessageBits {
public:
  explicit MessageBits(const std::vector<std::uint8_t>& bytes) : bytes_(bytes), limitBits_(bytes.size() * 8) {}
  std::size_t offsetBits() const { return offset_; }
  std::size_t remaining() const { return limitBits_ - offset_; }
  void setMinimumRemaining(std::size_t minimum) { minimumRemaining_ = std::min(minimum, limitBits_ - offset_); }
  // A reader over the next `count` bits of this one. Reads past that boundary
  // fail instead of walking into the next message: an svc_GameEvent body is
  // bounded by its own 11-bit length, and a malformed length must not be able
  // to eat the following message's 6-bit type.
  MessageBits limited(std::size_t count) const {
    MessageBits view(*this);
    view.limitBits_ = offset_ + std::min(count, remaining());
    view.minimumRemaining_ = 0;
    return view;
  }
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
  const std::vector<std::uint8_t>& bytes_; std::size_t offset_ = 0; std::size_t limitBits_ = 0; std::size_t minimumRemaining_ = 0;
};

// Internal, cross-translation-unit declarations for the demo_header split. Each one
// is defined in exactly one of the demo_header_*.cpp translation units.
bool namesIndicateSourceTv(const DemoHeader& header);
bool readExact(std::ifstream& file, void* destination, std::size_t size);
bool readEntryPayload(std::ifstream& file, const DemoIndexEntry& entry, std::vector<std::uint8_t>& payload, std::size_t maxBytes);
const EntityPropertyValue* findPropertySuffix(const std::unordered_map<std::string, EntityPropertyValue>& properties, const char* suffix);
bool readVoiceInit(MessageBits& bits, DemoNetworkSummary& summary);
bool readVoiceData(MessageBits& bits, DemoNetworkSummary& summary);
bool readDataTableBody(MessageBits& bits, DemoNetworkSummary& summary);
bool readSendTable(MessageBits& bits, DemoNetworkSummary& summary);
bool readEntityMessage(MessageBits& bits, DemoNetworkSummary& summary);
bool readGetCvarValue(MessageBits& bits, DemoNetworkSummary& summary);
bool readFixAngle(MessageBits& bits, DemoNetworkSummary& summary, std::int32_t packetTick);
bool readStringCommand(MessageBits& bits, DemoNetworkSummary& summary);
bool readPrefetch(MessageBits& bits, DemoNetworkSummary& summary);
bool readFileMessage(MessageBits& bits, DemoNetworkSummary& summary);
bool readSetPause(MessageBits& bits, DemoNetworkSummary& summary);
bool readBspDecal(MessageBits& bits, DemoNetworkSummary& summary);
bool readMenu(MessageBits& bits, DemoNetworkSummary& summary);
bool readCmdKeyValues(MessageBits& bits, DemoNetworkSummary& summary);
bool readGameEvent(MessageBits& bits, DemoNetworkSummary& summary, std::int32_t entryTick, std::uint32_t packetOrdinal, std::uint32_t messageBit);
bool readUserMessage(MessageBits& bits, DemoNetworkSummary& summary);
bool readGameEventList(MessageBits& bits, DemoNetworkSummary& summary);
const SendTableSchema* tableForClass(const DemoNetworkSummary& summary, std::uint32_t classId);
std::uint32_t effectiveServerClassCount(const DemoNetworkSummary& summary);
bool readSendPropValue(MessageBits& bits, const SendPropSchema& prop, EntityPropertyValue& value);
bool readEntityPropUpdates(MessageBits& bits, const SendTableSchema* table, EntityState& state, DemoNetworkSummary* summary = nullptr, std::int32_t packetTick = -1,
    std::int32_t entityIndex = -1, const char* stage = "entity",
    std::vector<EntityPropChange>* changes = nullptr);
bool readPacketEntities(MessageBits& bits, DemoNetworkSummary& summary, std::int32_t packetTick);
bool readUpdateStringTable(MessageBits& bits, DemoNetworkSummary& summary, std::int32_t packetTick);
bool readSounds(MessageBits& bits, DemoNetworkSummary& summary, std::int32_t packetTick);
bool readTempEntities(MessageBits& bits, DemoNetworkSummary& summary, std::int32_t packetTick);
bool readSetConVar(MessageBits& bits, DemoNetworkSummary& summary);
bool readSetView(MessageBits& bits, DemoNetworkSummary& summary);
bool readClassInfo(MessageBits& bits, DemoNetworkSummary& summary);
bool readSignonState(MessageBits& bits, DemoNetworkSummary& summary);
bool readServerInfo(MessageBits& bits, DemoNetworkSummary& summary);
bool decodeLzss(const std::vector<std::uint8_t>& input, std::size_t offset, std::size_t length, std::size_t expectedSize, std::vector<std::uint8_t>& output);
bool decodeSnappyRaw(const std::vector<std::uint8_t>& input, std::size_t offset, std::size_t length, std::size_t expectedSize, std::vector<std::uint8_t>& output);
bool readDemoStringTablesPayload(const std::vector<std::uint8_t>& payload, DemoNetworkSummary& summary, std::int32_t packetTick);
bool readCreateStringTable(MessageBits& bits, DemoNetworkSummary& summary, std::int32_t packetTick);

} // namespace tf2::native
