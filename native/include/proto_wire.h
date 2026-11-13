#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace tf2::native {

struct ProtoWireField {
  std::string path;
  std::uint32_t fieldNumber = 0;
  std::uint8_t wireType = 0;
  std::uint64_t varint = 0;
  std::string stringValue;
  bool hasPrintableString = false;
};

struct ProtoWireParseResult {
  bool ok = false;
  std::string error;
  std::vector<ProtoWireField> fields;
};

ProtoWireParseResult parseProtoWire(const std::string& bytes,
  std::size_t maxFields = 200000, std::size_t maxDepth = 16);

} // namespace tf2::native
