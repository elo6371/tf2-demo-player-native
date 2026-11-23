#include "proto_wire.h"

#include <cctype>

namespace tf2::native {
namespace {
struct Reader {
  const std::string& bytes;
  ProtoWireParseResult& result;
  std::size_t maxFields;
  std::size_t maxDepth;

  bool varint(std::size_t& at, std::uint64_t& out) {
    out = 0;
    for (unsigned shift = 0; shift < 64; shift += 7) {
      if (at >= bytes.size()) return fail("truncated varint");
      const auto byte = static_cast<std::uint8_t>(bytes[at++]);
      if (shift == 63 && byte > 1) return fail("varint overflow");
      out |= static_cast<std::uint64_t>(byte & 0x7fu) << shift;
      if ((byte & 0x80u) == 0) return true;
    }
    return fail("varint overflow");
  }

  bool fail(const char* message) {
    if (result.error.empty()) result.error = message;
    return false;
  }

  bool message(std::size_t begin, std::size_t end, const std::string& path, std::size_t depth) {
    if (depth > maxDepth) return fail("protobuf nesting depth exceeded");
    std::size_t at = begin;
    while (at < end) {
      if (result.fields.size() >= maxFields) return fail("protobuf field limit exceeded");
      const auto fieldStart = at;
      std::uint64_t key = 0;
      if (!varint(at, key) || key == 0 || key > (std::uint64_t{0xffffffffu} << 3u)) return fail("invalid protobuf field key");
      const auto number = static_cast<std::uint32_t>(key >> 3u);
      const auto wire = static_cast<std::uint8_t>(key & 7u);
      if (number == 0 || wire == 3 || wire == 4 || wire > 5) return fail("unsupported protobuf wire type");
      ProtoWireField field;
      field.path = path.empty() ? std::to_string(number) : path + "." + std::to_string(number);
      field.fieldNumber = number;
      field.wireType = wire;
      if (wire == 0) {
        if (!varint(at, field.varint)) return false;
      } else if (wire == 1) {
        if (end - at < 8) return fail("truncated fixed64");
        at += 8;
      } else if (wire == 2) {
        std::uint64_t length = 0;
        if (!varint(at, length) || length > end - at) return fail("truncated length-delimited field");
        const auto childBegin = at;
        const auto childEnd = at + static_cast<std::size_t>(length);
        field.stringValue.assign(bytes.data() + childBegin, bytes.data() + childEnd);
        field.hasPrintableString = !field.stringValue.empty();
        for (const unsigned char c : field.stringValue)
          if (c < 32 && c != '\n' && c != '\r' && c != '\t') { field.hasPrintableString = false; break; }
        result.fields.push_back(field);
        if (!message(childBegin, childEnd, field.path, depth + 1)) {
          result.error.clear();
          // A length-delimited field may be opaque bytes rather than a nested message.
        }
        at = childEnd;
      } else {
        if (end - at < 4) return fail("truncated fixed32");
        at += 4;
      }
      if (wire != 2) result.fields.push_back(std::move(field));
      if (at <= fieldStart) return fail("protobuf reader made no progress");
    }
    return true;
  }
};
}

ProtoWireParseResult parseProtoWire(const std::string& bytes, std::size_t maxFields, std::size_t maxDepth) {
  ProtoWireParseResult result;
  Reader reader{bytes, result, maxFields, maxDepth};
  result.ok = reader.message(0, bytes.size(), {}, 0);
  if (!result.ok && result.error.empty()) result.error = "protobuf parse failed";
  return result;
}
} // namespace tf2::native
