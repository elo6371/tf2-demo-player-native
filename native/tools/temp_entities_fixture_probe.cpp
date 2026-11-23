#include <algorithm>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace {

struct Bits {
  std::vector<std::uint8_t> bytes;
  std::size_t bit = 0;
  void write(std::uint32_t value, std::size_t width) {
    for (std::size_t i = 0; i < width; ++i) {
      if (bit % 8u == 0) bytes.push_back(0);
      bytes.back() |= static_cast<std::uint8_t>(((value >> i) & 1u) << (bit % 8u));
      ++bit;
    }
  }
  bool read(std::size_t width, std::uint32_t& value) {
    value = 0;
    if (width > bytes.size() * 8u - std::min(bit, bytes.size() * 8u)) return false;
    for (std::size_t i = 0; i < width; ++i) value |= static_cast<std::uint32_t>(((bytes[bit / 8u] >> (bit % 8u)) & 1u) << i), ++bit;
    return true;
  }
};

struct Event { bool reliable = false; bool hasDelay = false; std::uint32_t delay = 0; std::uint32_t classId = 0; };

std::size_t classBits(std::uint32_t classCount) {
  std::size_t bits = 1;
  while (bits < 31u && (1u << bits) <= std::max<std::uint32_t>(1u, classCount)) ++bits;
  return bits;
}

bool decode(const std::vector<std::uint8_t>& packet, std::uint32_t classCount, std::vector<Event>& events) {
  Bits bits{packet};
  std::uint32_t count = 0, payloadBits = 0;
  if (!bits.read(8, count) || !bits.read(16, payloadBits)) return false;
  if (payloadBits > bits.bytes.size() * 8u - bits.bit) return false;
  const std::size_t payloadEnd = bits.bit + payloadBits;
  const std::uint32_t eventCount = count == 0 ? 1u : count;
  const std::size_t classWidth = classBits(classCount);
  bool haveClass = false;
  std::uint32_t lastClass = 0;
  for (std::uint32_t i = 0; i < eventCount; ++i) {
    std::uint32_t value = 0;
    Event event;
    event.reliable = count == 0;
    if (bits.bit >= payloadEnd || !bits.read(1, value)) return false;
    event.hasDelay = value != 0;
    if (event.hasDelay && (!bits.read(8, event.delay) || bits.bit > payloadEnd)) return false;
    if (bits.bit >= payloadEnd || !bits.read(1, value)) return false;
    if (value) {
      if (!bits.read(classWidth, value) || value == 0 || value > classCount || bits.bit > payloadEnd) return false;
      lastClass = value - 1u;
      haveClass = true;
    } else if (!haveClass) return false;
    event.classId = lastClass;
    events.push_back(event);
  }
  return bits.bit <= payloadEnd;
}

std::vector<std::uint8_t> makeFixture(std::uint32_t count, std::uint32_t payloadBits, const Bits& body) {
  Bits packet;
  packet.write(count, 8);
  packet.write(payloadBits, 16);
  packet.bytes.insert(packet.bytes.end(), body.bytes.begin(), body.bytes.end());
  return packet.bytes;
}

bool expect(bool condition, const char* name) {
  std::cout << (condition ? "PASS " : "FAIL ") << name << '\n';
  return condition;
}

} // namespace

int main() {
  Bits body;
  body.write(1, 1); body.write(7, 8); body.write(1, 1); body.write(3, 3);
  body.write(0, 1); body.write(0, 1);
  std::vector<Event> events;
  bool ok = decode(makeFixture(2, static_cast<std::uint32_t>(body.bit), body), 5, events);
  bool all = expect(ok && events.size() == 2 && events[0].classId == 2 && events[1].classId == 2 &&
                    events[0].hasDelay && events[0].delay == 7 && !events[1].hasDelay &&
                    !events[0].reliable && !events[1].reliable, "event-header/class-reuse");
  events.clear();
  Bits reliableBody; reliableBody.write(0, 1); reliableBody.write(1, 1); reliableBody.write(1, 3); reliableBody.write(0, 1);
  ok = decode(makeFixture(0, static_cast<std::uint32_t>(reliableBody.bit), reliableBody), 5, events);
  all &= expect(ok && events.size() == 1 && events[0].reliable && events[0].classId == 0, "reliable-count-zero");
  events.clear();
  all &= expect(!decode(makeFixture(2, static_cast<std::uint32_t>(body.bit + 8), body), 5, events), "payload-length-boundary");
  events.clear();
  Bits noClass; noClass.write(0, 1); noClass.write(0, 1);
  all &= expect(!decode(makeFixture(1, static_cast<std::uint32_t>(noClass.bit), noClass), 5, events), "class-reuse-without-first-class");
  return all ? 0 : 1;
}
