#include "pcf_operators.h"

#include <cctype>
#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

namespace tf2::native {
namespace {

constexpr std::size_t kMaxFileBytes = 8u * 1024u * 1024u;
constexpr std::size_t kMaxStrings = 8192u;
constexpr std::size_t kMaxElements = 8192u;
constexpr std::size_t kMaxAttributes = 512u;
constexpr std::size_t kMaxArray = 65536u;
constexpr std::size_t kMaxStringBytes = 1024u;
constexpr std::size_t kMaxStoredOperators = 256u;

struct Cursor {
  const std::uint8_t* bytes = nullptr;
  std::size_t size = 0;
  std::size_t at = 0;
  std::string error;

  bool need(std::size_t count) {
    if (error.empty() && at <= size && count <= size - at) return true;
    if (error.empty()) error = "pcf ended early";
    return false;
  }

  bool u8(std::uint8_t& out) {
    if (!need(1)) return false;
    out = bytes[at++];
    return true;
  }

  bool i16(std::int16_t& out) {
    if (!need(2)) return false;
    out = static_cast<std::int16_t>(bytes[at] | (bytes[at + 1] << 8));
    at += 2;
    return true;
  }

  bool i32(std::int32_t& out) {
    if (!need(4)) return false;
    const std::uint32_t raw = static_cast<std::uint32_t>(bytes[at])
      | (static_cast<std::uint32_t>(bytes[at + 1]) << 8)
      | (static_cast<std::uint32_t>(bytes[at + 2]) << 16)
      | (static_cast<std::uint32_t>(bytes[at + 3]) << 24);
    out = static_cast<std::int32_t>(raw);
    at += 4;
    return true;
  }

  bool skip(std::size_t count) {
    if (!need(count)) return false;
    at += count;
    return true;
  }

  bool cstring(std::string& out) {
    out.clear();
    if (!error.empty()) return false;
    const std::size_t start = at;
    while (at < size && bytes[at] != 0) {
      if (at - start >= kMaxStringBytes) {
        error = "pcf string exceeds safety limit";
        return false;
      }
      ++at;
    }
    if (at >= size) {
      error = "pcf string is missing a terminator";
      return false;
    }
    out.assign(reinterpret_cast<const char*>(bytes + start), at - start);
    ++at;
    return true;
  }
};

bool skipValue(Cursor& cursor, std::uint8_t type) {
  if (type == 0 || type > 28) {
    cursor.error = "pcf attribute type is unsupported";
    return false;
  }
  if (type >= 15) {
    std::int32_t count = 0;
    if (!cursor.i32(count) || count < 0 || static_cast<std::size_t>(count) > kMaxArray) {
      if (cursor.error.empty()) cursor.error = "pcf array exceeds safety limit";
      return false;
    }
    const std::uint8_t item = static_cast<std::uint8_t>(type - 14);
    for (std::int32_t i = 0; i < count; ++i) {
      if (!skipValue(cursor, item)) return false;
    }
    return true;
  }
  switch (type) {
    case 1: return cursor.skip(4);
    case 2: return cursor.skip(4);
    case 3: return cursor.skip(4);
    case 4: return cursor.skip(1);
    case 5: {
      std::string ignored;
      return cursor.cstring(ignored);
    }
    case 6: {
      std::int32_t length = 0;
      if (!cursor.i32(length) || length < 0 || static_cast<std::size_t>(length) > kMaxFileBytes) {
        if (cursor.error.empty()) cursor.error = "pcf binary attribute exceeds safety limit";
        return false;
      }
      return cursor.skip(static_cast<std::size_t>(length));
    }
    case 7: return cursor.skip(16);
    case 8: return cursor.skip(4);
    case 9: return cursor.skip(8);
    case 10: return cursor.skip(12);
    case 11: return cursor.skip(16);
    case 12: return cursor.skip(12);
    case 13: return cursor.skip(16);
    case 14: return cursor.skip(64);
    default:
      cursor.error = "pcf attribute type is unsupported";
      return false;
  }
}

bool parseHeader(const std::string& header, PcfOperatorSummary& out) {
  const std::string encodingKey = "encoding ";
  const std::string formatKey = " format ";
  const auto encodingAt = header.find(encodingKey);
  const auto formatAt = header.find(formatKey);
  if (header.rfind("<!-- dmx ", 0) != 0 || encodingAt == std::string::npos || formatAt == std::string::npos
      || formatAt < encodingAt) {
    out.error = "pcf header is missing";
    return false;
  }
  const auto encodingText = header.substr(encodingAt + encodingKey.size(), formatAt - (encodingAt + encodingKey.size()));
  const auto space = encodingText.find(' ');
  if (space == std::string::npos || space == 0) {
    out.error = "pcf encoding is missing";
    return false;
  }
  out.encoding = encodingText.substr(0, space);
  try {
    out.encodingVersion = std::stoi(encodingText.substr(space + 1));
  } catch (...) {
    out.error = "pcf encoding version is invalid";
    return false;
  }
  std::string formatText = header.substr(formatAt + formatKey.size());
  const auto end = formatText.find(" -->");
  if (end != std::string::npos) formatText.resize(end);
  const auto formatSpace = formatText.find(' ');
  if (formatSpace == std::string::npos) {
    out.error = "pcf format is missing";
    return false;
  }
  out.format = formatText.substr(0, formatSpace);
  try {
    out.formatVersion = std::stoi(formatText.substr(formatSpace + 1));
  } catch (...) {
    out.error = "pcf format version is invalid";
    return false;
  }
  if (out.encoding != "binary" || out.encodingVersion != 2) {
    out.error = "unsupported pcf encoding";
    return false;
  }
  return true;
}

void rememberOperator(PcfOperatorSummary& out, const std::string& systemName, const std::string& functionName) {
  ++out.operatorCount;
  const bool common = pcfFunctionNameIsCommon(functionName);
  if (common) ++out.commonOperatorCount;
  if (out.operators.size() >= kMaxStoredOperators) return;
  PcfOperatorUse use;
  use.systemName = systemName;
  use.functionName = functionName;
  use.common = common;
  out.operators.push_back(std::move(use));
}

} // namespace

std::string normalizeFunctionName(std::string name) {
  for (char& character : name) {
    const unsigned char value = static_cast<unsigned char>(character);
    if (character == ' ' || character == '-') character = '_';
    else character = static_cast<char>(std::tolower(value));
  }
  return name;
}

bool pcfFunctionNameIsCommon(const std::string& functionName) {
  // TF2 stores the operator title ("Movement Basic") or a snake_case id.
  // Renderer function names stay outside this list.
  static const std::unordered_set<std::string> names = {
    "movement_basic",
    "movement_rotate_particle_around_axis",
    "movement_lock_to_bone",
    "movement_lock_to_controlpoint",
    "alpha_fade",
    "alpha_fade_in_random",
    "alpha_fade_out_random",
    "alpha_fade_and_decay",
    "alpha_random",
    "color_fade",
    "color_random",
    "radius_scale",
    "radius_random",
    "lifespan_decay",
    "lifetime_random",
    "position_within_sphere",
    "position_within_sphere_random",
    "position_within_box",
    "position_from_parent_particles",
    "position_modify_offset_random",
    "pull_towards_control_point",
    "rotation_basic",
    "rotation_spin",
    "rotation_spin_roll",
    "rotation_random",
    "oscillate_scalar",
    "oscillate_vector",
    "remap_initial_scalar",
    "sequence_random",
    "sequence_two_random",
    "velocity_inherit_from_control_point",
    "emit_continuously",
    "emit_instantaneously",
    "random_force",
  };
  return names.find(normalizeFunctionName(functionName)) != names.end();
}

bool readPcfOperators(const std::uint8_t* bytes, std::size_t size, PcfOperatorSummary& out) {
  out = {};
  if (!bytes || size == 0) {
    out.error = "pcf is empty";
    return false;
  }
  if (size > kMaxFileBytes) {
    out.error = "pcf exceeds safety limit";
    return false;
  }
  Cursor cursor;
  cursor.bytes = bytes;
  cursor.size = size;
  std::string header;
  if (!cursor.cstring(header) || !parseHeader(header, out)) {
    if (out.error.empty()) out.error = cursor.error.empty() ? "pcf header is missing" : cursor.error;
    return false;
  }
  std::int16_t stringCount = 0;
  if (!cursor.i16(stringCount) || stringCount < 0 || static_cast<std::size_t>(stringCount) > kMaxStrings) {
    out.error = cursor.error.empty() ? "pcf string count is invalid" : cursor.error;
    return false;
  }
  std::vector<std::string> strings;
  strings.reserve(static_cast<std::size_t>(stringCount));
  for (std::int16_t i = 0; i < stringCount; ++i) {
    std::string text;
    if (!cursor.cstring(text)) {
      out.error = cursor.error;
      return false;
    }
    strings.push_back(std::move(text));
  }
  out.stringCount = strings.size();
  std::int32_t elementCount = 0;
  if (!cursor.i32(elementCount) || elementCount < 0 || static_cast<std::size_t>(elementCount) > kMaxElements) {
    out.error = cursor.error.empty() ? "pcf element count is invalid" : cursor.error;
    return false;
  }
  struct ElementHead {
    std::string type;
    std::string name;
  };
  std::vector<ElementHead> elements(static_cast<std::size_t>(elementCount));
  for (std::int32_t i = 0; i < elementCount; ++i) {
    std::int16_t typeIndex = 0;
    if (!cursor.i16(typeIndex) || typeIndex < 0 || static_cast<std::size_t>(typeIndex) >= strings.size()) {
      out.error = cursor.error.empty() ? "pcf element type is invalid" : cursor.error;
      return false;
    }
    if (!cursor.cstring(elements[static_cast<std::size_t>(i)].name) || !cursor.skip(16)) {
      out.error = cursor.error.empty() ? "pcf element header is truncated" : cursor.error;
      return false;
    }
    elements[static_cast<std::size_t>(i)].type = strings[static_cast<std::size_t>(typeIndex)];
  }
  out.elementCount = elements.size();
  std::vector<std::string> functionNames(elements.size());
  std::vector<std::vector<std::int32_t>> systemOperators(elements.size());
  for (std::size_t elementIndex = 0; elementIndex < elements.size(); ++elementIndex) {
    std::int32_t attributeCount = 0;
    if (!cursor.i32(attributeCount) || attributeCount < 0 || static_cast<std::size_t>(attributeCount) > kMaxAttributes) {
      out.error = cursor.error.empty() ? "pcf attribute count is invalid" : cursor.error;
      return false;
    }
    const bool isOperator = elements[elementIndex].type == "DmeParticleOperator";
    const bool isSystem = elements[elementIndex].type == "DmeParticleSystemDefinition";
    if (isSystem) ++out.systemCount;
    for (std::int32_t attributeIndex = 0; attributeIndex < attributeCount; ++attributeIndex) {
      std::int16_t nameIndex = 0;
      std::uint8_t type = 0;
      if (!cursor.i16(nameIndex) || !cursor.u8(type)) {
        out.error = cursor.error;
        return false;
      }
      const std::string attributeName = (nameIndex >= 0 && static_cast<std::size_t>(nameIndex) < strings.size())
        ? strings[static_cast<std::size_t>(nameIndex)] : std::string();
      if (nameIndex < -1 || (nameIndex >= 0 && static_cast<std::size_t>(nameIndex) >= strings.size())) {
        out.error = "pcf attribute name is invalid";
        return false;
      }
      if (type == 5 && (isOperator && attributeName == "functionName")) {
        if (!cursor.cstring(functionNames[elementIndex])) {
          out.error = cursor.error;
          return false;
        }
        continue;
      }
      if (type == 15 && isSystem && attributeName == "operators") {
        std::int32_t count = 0;
        if (!cursor.i32(count) || count < 0 || static_cast<std::size_t>(count) > kMaxArray) {
          if (cursor.error.empty()) out.error = "pcf operator list exceeds safety limit";
          else out.error = cursor.error;
          return false;
        }
        for (std::int32_t item = 0; item < count; ++item) {
          std::int32_t index = 0;
          if (!cursor.i32(index)) {
            out.error = cursor.error;
            return false;
          }
          if (index >= 0) systemOperators[elementIndex].push_back(index);
        }
        continue;
      }
      if (!skipValue(cursor, type)) {
        out.error = cursor.error;
        return false;
      }
    }
  }
  std::vector<char> linked(elements.size(), 0);
  for (std::size_t systemIndex = 0; systemIndex < elements.size(); ++systemIndex) {
    if (elements[systemIndex].type != "DmeParticleSystemDefinition") continue;
    for (const std::int32_t index : systemOperators[systemIndex]) {
      if (index < 0 || static_cast<std::size_t>(index) >= elements.size()) {
        out.error = "pcf operator index is outside the element table";
        return false;
      }
      if (elements[static_cast<std::size_t>(index)].type != "DmeParticleOperator") continue;
      linked[static_cast<std::size_t>(index)] = 1;
      rememberOperator(out, elements[systemIndex].name, functionNames[static_cast<std::size_t>(index)]);
    }
  }
  for (std::size_t index = 0; index < elements.size(); ++index) {
    if (elements[index].type != "DmeParticleOperator" || linked[index] || functionNames[index].empty()) continue;
    rememberOperator(out, "", functionNames[index]);
  }
  out.valid = true;
  return true;
}

} // namespace tf2::native
