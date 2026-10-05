#include "item_schema.h"
#include "proto_wire.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {
std::size_t countBinaryString(const std::string& data, std::string needle) {
  std::string lower = data;
  std::transform(lower.begin(), lower.end(), lower.begin(),
    [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  std::transform(needle.begin(), needle.end(), needle.begin(),
    [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  std::size_t count = 0;
  for (std::size_t at = 0; (at = lower.find(needle, at)) != std::string::npos; at += needle.size()) ++count;
  return count;
}

std::uint32_t u32le(const std::string& bytes, std::size_t offset) {
  return static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[offset]))
    | (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[offset + 1])) << 8u)
    | (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[offset + 2])) << 16u)
    | (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[offset + 3])) << 24u);
}

std::string hexBytes(const std::string& bytes, std::size_t offset, std::size_t count) {
  static constexpr char digits[] = "0123456789abcdef";
  std::string result;
  const auto end = std::min(bytes.size(), offset + count);
  for (std::size_t at = offset; at < end; ++at) {
    if (!result.empty()) result.push_back(' ');
    const auto value = static_cast<unsigned char>(bytes[at]);
    result.push_back(digits[value >> 4]);
    result.push_back(digits[value & 15]);
  }
  return result;
}
}

int main(int argc, char** argv) {
  if (argc == 2 && std::string(argv[1]) == "--wire-self-test") {
    const auto truncatedVarint = tf2::native::parseProtoWire(std::string({char(0x80)}));
    const auto truncatedLength = tf2::native::parseProtoWire(std::string({char(0x0a), char(0x03), 'a', 'b'}));
    const auto valid = tf2::native::parseProtoWire(std::string({char(0x0a), char(0x03), 'a', 'b', 'c'}));
    const bool pass = !truncatedVarint.ok && !truncatedLength.ok && valid.ok;
    std::cout << "wire_self_test truncated_varint=" << (truncatedVarint.ok ? 0 : 1)
              << " truncated_length=" << (truncatedLength.ok ? 0 : 1)
              << " valid=" << (valid.ok ? 1 : 0)
              << " status=" << (pass ? "pass" : "fail") << "\n";
    return pass ? 0 : 1;
  }
  if (argc < 2) {
    std::cerr << "usage: item_schema_probe <items_game.txt> [item_definition_index] [paint_kit_id]\n";
    return 2;
  }
  std::string error;
  auto schema = tf2::native::ItemSchema::load(argv[1], error);
  if (!schema) { std::cerr << "error: " << error << "\n"; return 1; }
  std::cout << "entries=" << schema->size() << "\n";
  std::cout << "paint_kits=" << schema->paintKitCount() << "\n";
  const auto itemsPath = std::filesystem::path(argv[1]);
  const auto protoPath = itemsPath.parent_path().parent_path() / "protodefs" / "proto_defs.vpd";
  const auto protoSignaturePath = protoPath.string() + ".sig";
  std::error_code signatureError;
  const bool signaturePresent = std::filesystem::is_regular_file(protoSignaturePath, signatureError) && !signatureError;
  const auto signatureBytes = signaturePresent ? std::filesystem::file_size(protoSignaturePath, signatureError) : 0;
  std::ifstream proto(protoPath, std::ios::binary | std::ios::ate);
  if (!proto) {
    std::cout << "proto_defs status=missing path=" << protoPath.string()
              << " signature_status=" << (signaturePresent ? "present" : "missing")
              << " signature_bytes=" << signatureBytes
              << " semantic_status=unproven decoded=false mapping=false\n";
  } else {
    const auto size = proto.tellg();
    proto.seekg(0);
    std::string bytes(static_cast<std::size_t>(size), '\0');
    proto.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    const bool headerComplete = bytes.size() >= 12;
    const std::size_t vpdHeaderBytes = headerComplete ? 12 : bytes.size();
    const std::uint32_t header0 = headerComplete ? u32le(bytes, 0) : 0;
    const std::uint32_t header1 = headerComplete ? u32le(bytes, 4) : 0;
    const std::uint32_t header2 = headerComplete ? u32le(bytes, 8) : 0;
    const bool segmentComplete = headerComplete && header1 <= bytes.size() - vpdHeaderBytes;
    const std::size_t segmentBytes = segmentComplete ? header1 : 0;
    const auto wire = segmentComplete
      ? tf2::native::parseProtoWire(bytes.substr(vpdHeaderBytes, segmentBytes))
      : tf2::native::ProtoWireParseResult{};
    // The first 12 bytes are a file header, not a proven repeated record header.
    // Search only for a complete protobuf prefix; never treat a printable string
    // hit or a malformed wire tag as a semantic PaintKit record.
    std::size_t safePrefixBytes = 0;
    std::size_t safePrefixFields = 0;
    if (segmentComplete) {
      for (std::size_t candidate = 1; candidate <= segmentBytes; ++candidate) {
        const auto prefix = tf2::native::parseProtoWire(
          bytes.substr(vpdHeaderBytes, candidate), 200000, 16);
        if (prefix.ok) {
          safePrefixBytes = candidate;
          safePrefixFields = prefix.fields.size();
        }
      }
    }
    std::size_t firstFieldLength = 0;
    bool firstFieldLengthComplete = false;
    if (headerComplete && bytes.size() > vpdHeaderBytes && static_cast<unsigned char>(bytes[vpdHeaderBytes]) == 0x0a
        && bytes.size() > vpdHeaderBytes + 1) {
      firstFieldLength = static_cast<unsigned char>(bytes[vpdHeaderBytes + 1]);
      firstFieldLengthComplete = firstFieldLength <= bytes.size() - vpdHeaderBytes - 2;
    }
    std::size_t paintkitStrings = 0, warpaintStrings = 0, patternStrings = 0;
    for (const auto& field : wire.fields) {
      if (!field.hasPrintableString) continue;
      std::string value = field.stringValue;
      std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
      if (value.find("paintkit") != std::string::npos) ++paintkitStrings;
      if (value.find("warpaint") != std::string::npos) ++warpaintStrings;
      if (value.find("patterns/paint_") != std::string::npos) ++patternStrings;
    }
    std::cout << "proto_defs status=present bytes=" << bytes.size()
              << " paintkit_390=" << countBinaryString(bytes, "Paintkit 390")
              << " warpaint=" << countBinaryString(bytes, "Warpaint")
              << " patterns_paint=" << countBinaryString(bytes, "patterns/paint_")
              << " signature_status=" << (signaturePresent ? "present" : "missing")
              << " signature_bytes=" << signatureBytes
              << " vpd_header_bytes=" << vpdHeaderBytes
              << " header0=" << header0 << " header1=" << header1 << " header2=" << header2
              << " segment_offset=" << vpdHeaderBytes << " segment_bytes=" << segmentBytes
              << " segment_complete=" << (segmentComplete ? 1 : 0)
              << " first_field_length=" << firstFieldLength
              << " first_field_complete=" << (firstFieldLengthComplete ? 1 : 0)
              << " wire_ok=" << (wire.ok ? 1 : 0)
              << " wire_fields=" << wire.fields.size()
              << " wire_paintkit_candidates=" << paintkitStrings
              << " wire_warpaint_candidates=" << warpaintStrings
              << " wire_pattern_candidates=" << patternStrings
              << " safe_prefix_bytes=" << safePrefixBytes
              << " safe_prefix_fields=" << safePrefixFields
              << " boundary_candidate=" << ((safePrefixBytes > 0 && safePrefixBytes < segmentBytes) ? 1 : 0)
              << " boundary_payload_bytes=" << safePrefixBytes
              << " boundary_remainder_bytes=" << (segmentBytes >= safePrefixBytes ? segmentBytes - safePrefixBytes : 0)
              << " boundary_failure_segment_offset=" << safePrefixBytes
              << " boundary_failure_file_offset=" << (vpdHeaderBytes + safePrefixBytes)
              << " boundary_tail_hex=" << hexBytes(bytes, vpdHeaderBytes + safePrefixBytes, 6)
              << " container_segments=unproven"
              << " semantic_status=unproven"
              << " decoded=false"
              << " mapping=false\n";
    if (!wire.ok) std::cout << "proto_defs wire_error=" << wire.error << "\n";
    for (const auto& field : wire.fields) {
      if (field.hasPrintableString && ([&] {
          std::string value = field.stringValue;
          std::transform(value.begin(), value.end(), value.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
          return value.find("paintkit") != std::string::npos
            || value.find("warpaint") != std::string::npos
            || value.find("patterns/paint_") != std::string::npos;
        })())
        std::cout << "proto_candidate path=" << field.path << " field=" << field.fieldNumber
                  << " wire=" << static_cast<int>(field.wireType)
                  << " value=" << field.stringValue << "\n";
    }
  }
  if (argc < 3) return 0;
  const int id = std::stoi(argv[2]);
  const auto* item = schema->find(id);
  if (!item) { std::cout << "item=" << id << " status=missing\n"; return 3; }
  std::cout << "item=" << id << " name=" << item->itemName
            << " class=" << item->itemClass
            << " model_player=" << item->modelPlayer
            << " model_world=" << item->modelWorld
            << " per_class=" << item->modelPlayerPerClass.size()
            << " visuals=" << item->visuals.size()
            << " explicit_model=" << (item->hasExplicitModel ? 1 : 0) << "\n";
  for (const auto& [klass, path] : item->modelPlayerPerClass)
    std::cout << "class_model " << klass << "=" << path << "\n";
  const auto candidates = schema->modelCandidates(id);
  std::cout << "candidates model_player=" << candidates.modelPlayer
            << " model_world=" << candidates.modelWorld
            << " per_class=" << candidates.modelPlayerPerClass.size() << "\n";
  for (const auto& [klass, path] : candidates.modelPlayerPerClass)
    std::cout << "candidate_class_model " << klass << "=" << path << "\n";
  for (const auto& [key, value] : item->visuals)
    std::cout << "visual " << key << "=" << value << "\n";
  const auto appearance = schema->resolveAppearance(id, -1, -1);
  std::cout << "appearance item=" << id
            << " item_status=" << static_cast<int>(appearance.itemStatus)
            << " manifest_status=" << static_cast<int>(appearance.manifestStatus)
            << " replacement_status=" << static_cast<int>(appearance.replacementStatus)
            << " visual_declarations=" << appearance.visuals.size() << "\n";
  for (const auto& visual : appearance.visuals)
    std::cout << "appearance_visual key=" << visual.key << " value=" << visual.value
              << " resource_status=" << static_cast<int>(visual.resourceStatus) << "\n";
  if (argc >= 4) {
    const int paintKitId = std::stoi(argv[3]);
    const auto* paintKit = schema->findPaintKit(paintKitId);
    if (!paintKit) { std::cout << "paint_kit=" << paintKitId << " status=missing\n"; return 4; }
    std::cout << "paint_kit=" << paintKitId << " name=" << paintKit->name
              << " description=" << paintKit->description
              << " fields=" << paintKit->fields.size() << "\n";
    for (const auto& [key, value] : paintKit->fields)
      std::cout << "paint_kit_field " << key << "=" << value << "\n";
  }
  return 0;
}
