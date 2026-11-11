#include "demo_header.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <set>
#include <string>
#include <system_error>
#include <tuple>
#include <vector>

namespace tf2::native {
// Defined in demo_header_net.cpp: the named reason for the most recent parser
// budget rejection. Declared here rather than in demo_header.h because F0-a is
// scoped to three files; a gate asserts on the name so a future change to a cap
// cannot silently re-use another bound's rejection.
const char* parserBudgetRejectReason();
} // namespace tf2::native

namespace {

std::wstring lastBudgetReason() {
  const char* reason = tf2::native::parserBudgetRejectReason();
  if (reason == nullptr) return std::wstring();
  return std::wstring(reason, reason + std::strlen(reason));
}

std::vector<std::uint8_t> protocol4Fixture(bool includePlayerSlot) {
  std::vector<std::uint8_t> bytes(1072, 0);
  bytes.push_back(3); // dem_synctick, no payload
  bytes.insert(bytes.end(), {0, 0, 0, 0}); // tick
  if (includePlayerSlot) bytes.push_back(0);
  if (includePlayerSlot) {
    bytes.push_back(7); // dem_stop uses a three-byte terminal tick
    bytes.insert(bytes.end(), {0, 0, 0});
  }
  return bytes;
}

bool parseHeaderWithPlaybackTime(float playbackTime) {
  std::vector<std::uint8_t> bytes(1072, 0);
  std::memcpy(bytes.data(), "HL2DEMO\0", 8);
  std::memcpy(bytes.data() + 536, "cp_test", 7);
  std::memcpy(bytes.data() + 1056, &playbackTime, sizeof(playbackTime));
  tf2::native::DemoHeader header;
  return tf2::native::parseDemoHeader(bytes, header);
}

// --- F0-a: synthetic demos for the parser budgets -------------------------
// The three bounds F0-a adds sit on the FILE path, not on the message-stream
// path: an svc_DataTable body arrives as a command-6 block and the playback row
// index comes from a command-2 entry tick, neither of which
// decodeDemoMessageStream ever sees. Rather than invent a private seam, these
// tests build a whole .dem in memory and drive the same three entry points the
// product drives: parseDemoHeader -> indexDemoCommands -> scanKnownDemoMessages.

class BitWriter {
 public:
  // MessageBits::read takes stream bit i from bit (i % 8) of byte (i / 8). A
  // fixture written in any other order is simply read as different bits, so
  // this layout is copied from the reader rather than chosen.
  void put(std::uint32_t value, std::uint32_t count) {
    for (std::uint32_t i = 0; i < count; ++i) {
      if (bit_ % 8u == 0) bytes_.push_back(0);
      if (((value >> i) & 1u) != 0u) bytes_.back() |= static_cast<std::uint8_t>(1u << (bit_ % 8u));
      ++bit_;
    }
  }
  void putString(const char* text) {
    for (const char* p = text; *p != 0; ++p) put(static_cast<std::uint32_t>(static_cast<unsigned char>(*p)), 8);
    put(0, 8);
  }
  std::vector<std::uint8_t> take() { return std::move(bytes_); }

 private:
  std::vector<std::uint8_t> bytes_;
  std::size_t bit_ = 0;
};

// A demo the indexer accepts: 1072-byte header, protocol-4 command framing, and
// a three-byte DEM_STOP so the indexer terminates on the real rule instead of
// running off the end.
class SyntheticDemo {
 public:
  SyntheticDemo() {
    bytes_.assign(1072, 0);
    const char signature[8] = {'H', 'L', '2', 'D', 'E', 'M', 'O', 0};
    std::memcpy(bytes_.data(), signature, 8);
    setI32(8, 4);    // demoProtocol 4: every command but DEM_STOP carries a player slot
    setI32(12, 24);  // networkProtocol > 23: svc_CreateStringTable lengths are varints
    std::memcpy(bytes_.data() + 536, "cp_synthetic", 12);
  }
  // Command 2 (packet): cmd, tick, slot, 76-byte cmdinfo + 8 bytes, length, payload.
  void addPacket(std::int32_t tick, const std::vector<std::uint8_t>& payload) {
    beginCommand(2, tick);
    bytes_.insert(bytes_.end(), 84u, 0);  // cmdinfo values are not what these tests read
    appendI32(static_cast<std::int32_t>(payload.size()));
    bytes_.insert(bytes_.end(), payload.begin(), payload.end());
  }
  // Command 6 (svc_DataTable, signon): cmd, tick, slot, length, payload.
  void addDataTableBody(std::int32_t tick, const std::vector<std::uint8_t>& payload) {
    beginCommand(6, tick);
    appendI32(static_cast<std::int32_t>(payload.size()));
    bytes_.insert(bytes_.end(), payload.begin(), payload.end());
  }
  void addStop(std::int32_t tick) {
    bytes_.push_back(7);
    for (int i = 0; i < 3; ++i) bytes_.push_back(static_cast<std::uint8_t>((tick >> (8 * i)) & 0xff));
  }
  const std::vector<std::uint8_t>& bytes() const { return bytes_; }

 private:
  void beginCommand(std::uint8_t command, std::int32_t tick) {
    bytes_.push_back(command);
    appendI32(tick);
    // The player slot comes AFTER the tick: indexDemoCommands reads cmd, then
    // the 4-byte tick (demo_header_core.cpp:120-130), then the protocol-4 slot
    // (:133-140). Writing the slot first does not break the framing -- the two
    // fields are consumed in the same total width -- but the tick then reads as
    // (slot << 24) | (tick << 8) | (tick >> 24), i.e. tick * 256 for slot 0.
    // Every fixture here used tick 0, so only the playback cases noticed.
    bytes_.push_back(0);  // player slot (protocol 4)
  }
  void appendI32(std::int32_t value) {
    const std::uint8_t raw[4] = {static_cast<std::uint8_t>(value & 0xff),
                                 static_cast<std::uint8_t>((value >> 8) & 0xff),
                                 static_cast<std::uint8_t>((value >> 16) & 0xff),
                                 static_cast<std::uint8_t>((value >> 24) & 0xff)};
    bytes_.insert(bytes_.end(), raw, raw + 4);
  }
  void setI32(std::size_t offset, std::int32_t value) { std::memcpy(bytes_.data() + offset, &value, 4); }
  std::vector<std::uint8_t> bytes_;
};

struct SyntheticScan {
  bool headerOk = false;
  bool indexOk = false;
  bool scanOk = false;
  std::string indexError;
  tf2::native::DemoNetworkSummary summary;
};

// scanKnownDemoMessages reads from a path, so the fixture has to reach disk: it
// is the only entry point that runs the command-6 and command-2 paths.
bool scanSyntheticDemo(const std::vector<std::uint8_t>& bytes, SyntheticScan& out) {
  static std::size_t sequence = 0;
  std::error_code error;
  const std::filesystem::path directory = std::filesystem::temp_directory_path(error);
  if (error) return false;
  const std::filesystem::path path = directory
      / ("t85_f0a_" + std::to_string(++sequence) + "_"
         + std::to_string(reinterpret_cast<std::uintptr_t>(&bytes)) + ".dem");
  {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) return false;
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!file) return false;
  }
  tf2::native::DemoHeader header;
  out.headerOk = tf2::native::parseDemoHeader(bytes, header);
  tf2::native::DemoIndex index;
  out.indexOk = out.headerOk && tf2::native::indexDemoCommands(bytes, header, index);
  out.indexError = index.error;
  if (out.indexOk) {
    out.summary.networkProtocol = header.networkProtocol;
    out.scanOk = tf2::native::scanKnownDemoMessages(path, index, out.summary);
  }
  std::filesystem::remove(path, error);
  return out.indexOk;
}

// A single NOP message: enough for the scan to score the packet as decoded, so
// a fixture that isolates a command-6 body is still a demo with one packet.
const std::vector<std::uint8_t>& nopPacket() {
  static const std::vector<std::uint8_t> payload(1, 0);
  return payload;
}

// A command-6 body naming "count" tables that have no properties. Each table is
// 20 bits -- hasTable(1), needsDecoder(1), empty name(8), propCount(10) -- so a
// flood of 8192 tables is a 20 KB body that still makes the parser keep one
// heap-backed SendTableSchema per 20 bits of input.
std::vector<std::uint8_t> emptyDataTableBody(std::size_t count) {
  BitWriter writer;
  for (std::size_t i = 0; i < count; ++i) {
    writer.put(1, 1);
    writer.put(0, 1);
    writer.put(0, 8);
    writer.put(0, 10);
  }
  writer.put(0, 16);  // server class count; the table loop stops at <= 16 bits left
  return writer.take();
}

// A diamond, not a chain: every non-leaf table carries two DataTable properties
// that both point at the SAME child, so a depth-k pyramid expands to 2^k leaf
// emissions while the body stays a few hundred bytes. A chain would flatten in
// linear time and prove nothing -- this is the shape QA1B-02 measured.
std::vector<std::uint8_t> diamondDataTableBody(std::size_t depth, std::size_t leafProps) {
  BitWriter writer;
  // Root first. Flatten resolves children by name, so the order does not change
  // what the pyramid expands to -- but it does change what a REJECTION leaves
  // behind: only a root-first body can be refused with zero props already
  // counted, which is the invariant the probe pins.
  for (std::size_t level = 0; level <= depth; ++level) {
    writer.put(1, 1);
    writer.put(0, 1);
    writer.putString(("T" + std::to_string(level)).c_str());
    const bool leaf = level == depth;
    writer.put(leaf ? static_cast<std::uint32_t>(leafProps) : 2u, 10);
    if (leaf) {
      for (std::size_t i = 0; i < leafProps; ++i) {
        writer.put(0, 5);   // SendPropType::Int
        writer.putString(("p" + std::to_string(i)).c_str());
        writer.put(0, 16);  // flags
        writer.put(0, 32);  // lowValue
        writer.put(0, 32);  // highValue
        writer.put(1, 7);   // bitCount
      }
    } else {
      const std::string child = "T" + std::to_string(level + 1);
      for (std::uint32_t slot = 0; slot < 2u; ++slot) {
        writer.put(6, 5);  // SendPropType::DataTable
        writer.putString(("c" + std::to_string(slot)).c_str());
        writer.put(0, 16);  // flags: no COLLAPSIBLE(4096), so the child lands in output
        writer.putString(child.c_str());
      }
    }
  }
  writer.put(0, 16);
  return writer.take();
}

// F0-a: the caps under test, copied from where they are defined so the probe can
// pin them exactly instead of merely bounding them. Duplicated on purpose: a cap
// change must make this test fail so it is made in both places consciously.
//   schema count             demo_header_net.cpp:53  kMaxSendTableSchemas
//   per-table flatten props  demo_header_net.cpp:54  kMaxFlattenedPropsPerTable
//   playback rows            demo_header.cpp:36      kMaxPlaybackRows
constexpr std::size_t kExpectedSchemaCap = 4096;
constexpr std::size_t kExpectedFlattenCap = 4096;
constexpr std::int64_t kExpectedMaxRows = 1ll << 21;

// A root that points at "childCount" distinct children, each carrying
// "propsPerChild" Int properties: flattening the root emits the product, so this
// fixture can sit exactly ON the per-table budget (8 x 512 == 4096) and one step
// past it (8 x 513 == 4104) -- the diamond cannot do that, its count is a power
// of two times the leaf size. Root first, so a rejection on it leaves no earlier
// table's props in dataTableFlattenedPropCount.
std::vector<std::uint8_t> fanDataTableBody(std::size_t childCount, std::size_t propsPerChild) {
  BitWriter writer;
  writer.put(1, 1);
  writer.put(0, 1);
  writer.putString("R");
  writer.put(static_cast<std::uint32_t>(childCount), 10);
  for (std::size_t i = 0; i < childCount; ++i) {
    writer.put(6, 5);  // SendPropType::DataTable
    writer.putString(("c" + std::to_string(i)).c_str());
    writer.put(0, 16);
    writer.putString(("C" + std::to_string(i)).c_str());
  }
  for (std::size_t child = 0; child < childCount; ++child) {
    writer.put(1, 1);
    writer.put(0, 1);
    writer.putString(("C" + std::to_string(child)).c_str());
    writer.put(static_cast<std::uint32_t>(propsPerChild), 10);
    for (std::size_t i = 0; i < propsPerChild; ++i) {
      writer.put(0, 5);   // Int
      writer.putString(("p" + std::to_string(i)).c_str());
      writer.put(0, 16);  // flags
      writer.put(0, 32);  // lowValue
      writer.put(0, 32);  // highValue
      writer.put(1, 7);   // bitCount
    }
  }
  writer.put(0, 16);
  return writer.take();
}

int runParserBoundsSelfTest() {
  tf2::native::DemoHeader header;
  header.valid = true;
  header.demoProtocol = 4;

  const auto malformedBytes = protocol4Fixture(false);
  tf2::native::DemoIndex malformed;
  const bool malformedOk = tf2::native::indexDemoCommands(malformedBytes, header, malformed);
  const bool rejectedMissingSlot = !malformedOk
      && malformed.error == "truncated player slot" && malformed.entries.empty();

  const auto validBytes = protocol4Fixture(true);
  tf2::native::DemoIndex valid;
  const bool validOk = tf2::native::indexDemoCommands(validBytes, header, valid);
  const bool acceptedControl = validOk && valid.valid && valid.entries.size() == 2;
  const bool rejectedNanTime = !parseHeaderWithPlaybackTime(
      std::numeric_limits<float>::quiet_NaN());
  const bool rejectedInfiniteTime = !parseHeaderWithPlaybackTime(
      std::numeric_limits<float>::infinity());
  const bool acceptedFiniteTime = parseHeaderWithPlaybackTime(30.0f);

  // F0-a case 1 (QA1B-01): 8192 tables in one 20 KB command-6 body. The red
  // reading is "the parser accepted all of them" -- the count grows with the
  // input and never stops; nothing here needs the 3.4 GB the defect could reach.
  constexpr std::size_t kSchemaFloodTables = 8192;
  SyntheticDemo schemaFloodDemo;
  schemaFloodDemo.addDataTableBody(0, emptyDataTableBody(kSchemaFloodTables));
  schemaFloodDemo.addPacket(0, nopPacket());
  schemaFloodDemo.addStop(0);
  SyntheticScan schemaFlood;
  const bool schemaFloodBuilt = scanSyntheticDemo(schemaFloodDemo.bytes(), schemaFlood);
  // Read the reason before the next scan resets it. The count is pinned to the
  // cap itself: "fewer than I sent" would pass for any cap at all between the
  // control and the flood, which is exactly what this test must not allow.
  const std::wstring schemaFloodReason = lastBudgetReason();
  const bool rejectedSchemaFlood = schemaFloodBuilt
      && schemaFlood.summary.malformedPackets > 0
      && schemaFlood.summary.dataTableDefinitionCount == kExpectedSchemaCap
      && schemaFloodReason == L"datatable-schema-cap";

  // Positive control: exactly cap tables must still be accepted -- a cap that is
  // off by one, or arbitrarily small, fails here instead of passing quietly.
  constexpr std::size_t kSchemaControlTables = kExpectedSchemaCap;
  SyntheticDemo schemaControlDemo;
  schemaControlDemo.addDataTableBody(0, emptyDataTableBody(kSchemaControlTables));
  schemaControlDemo.addPacket(0, nopPacket());
  schemaControlDemo.addStop(0);
  SyntheticScan schemaControl;
  const bool schemaControlBuilt = scanSyntheticDemo(schemaControlDemo.bytes(), schemaControl);
  const bool acceptedSchemaControl = schemaControlBuilt && schemaControl.indexOk
      && schemaControl.summary.malformedPackets == 0
      && schemaControl.summary.dataTableDefinitionCount == kSchemaControlTables
      && lastBudgetReason().empty();

  // F0-a case 2 (QA1B-02): depth-12 diamond, four props per leaf -> 16384 props
  // emitted for one root table, from a body under 400 bytes. A budget has to
  // bite below that 2^12 emission; the red reading is the emitted count itself,
  // so the test does not pin whichever constant the fix chooses.
  constexpr std::size_t kDiamondDepth = 12;
  constexpr std::size_t kDiamondLeafProps = 4;
  SyntheticDemo diamondDemo;
  diamondDemo.addDataTableBody(0, diamondDataTableBody(kDiamondDepth, kDiamondLeafProps));
  diamondDemo.addPacket(0, nopPacket());
  diamondDemo.addStop(0);
  SyntheticScan diamond;
  const bool diamondBuilt = scanSyntheticDemo(diamondDemo.bytes(), diamond);
  // Root first is what makes the zero below an invariant rather than a coincidence:
  // the root is table 0, so its refusal happens before any other table's props are
  // counted. The whole packet is abandoned (naming a reason and leaving the table
  // half-filled would be a wrong answer, not a smaller one).
  const std::wstring diamondReason = lastBudgetReason();
  const bool rejectedDiamondBudget = diamondBuilt
      && diamond.summary.malformedPackets > 0
      && diamond.summary.dataTableFlattenedPropCount == 0
      && diamondReason == L"datatable-flatten-budget";

  // Positive control at depth 3: 32 props, which no reasonable budget rejects.
  SyntheticDemo diamondControlDemo;
  diamondControlDemo.addDataTableBody(0, diamondDataTableBody(3, kDiamondLeafProps));
  diamondControlDemo.addPacket(0, nopPacket());
  diamondControlDemo.addStop(0);
  SyntheticScan diamondControl;
  const bool diamondControlBuilt = scanSyntheticDemo(diamondControlDemo.bytes(), diamondControl);
  const bool acceptedDiamondControl = diamondControlBuilt && diamondControl.indexOk
      && diamondControl.summary.malformedPackets == 0
      && diamondControl.summary.dataTableFlattenedPropCount > 0
      && lastBudgetReason().empty();

  // F0-a: the per-table budget pinned from both sides. The diamond can only reach
  // it as a power of two times the leaf size, so it cannot show that the boundary
  // is where it claims to be. Eight children of 512 props put the root at exactly
  // 4096 (accepted, all of them kept) and eight of 513 at 4104 (refused, nothing
  // counted).
  SyntheticDemo flattenEdgeDemo;
  flattenEdgeDemo.addDataTableBody(0, fanDataTableBody(8, kExpectedFlattenCap / 8));
  flattenEdgeDemo.addPacket(0, nopPacket());
  flattenEdgeDemo.addStop(0);
  SyntheticScan flattenEdge;
  const bool flattenEdgeBuilt = scanSyntheticDemo(flattenEdgeDemo.bytes(), flattenEdge);
  const std::wstring flattenEdgeReason = lastBudgetReason();
  const bool acceptedFlattenEdge = flattenEdgeBuilt && flattenEdge.indexOk
      && flattenEdge.summary.malformedPackets == 0
      && flattenEdge.summary.sendTableSchemas.size() == 9u
      && flattenEdge.summary.sendTableSchemas[0].flattenedProps.size() == kExpectedFlattenCap
      && flattenEdgeReason.empty();

  SyntheticDemo flattenOverDemo;
  flattenOverDemo.addDataTableBody(0, fanDataTableBody(8, kExpectedFlattenCap / 8 + 1));
  flattenOverDemo.addPacket(0, nopPacket());
  flattenOverDemo.addStop(0);
  SyntheticScan flattenOver;
  const bool flattenOverBuilt = scanSyntheticDemo(flattenOverDemo.bytes(), flattenOver);
  const std::wstring flattenOverReason = lastBudgetReason();
  const bool rejectedFlattenOver = flattenOverBuilt
      && flattenOver.summary.malformedPackets > 0
      && flattenOver.summary.dataTableFlattenedPropCount == 0
      && flattenOverReason == L"datatable-flatten-budget";

  // F0-a case 3 (QA1B-03): the second data entry tick names playback row
  // 2097160. The defect is that this number sizes three vectors, so the red
  // reading is the row count, not the 12.9 GB an int32-max tick would ask for.
  constexpr std::int32_t kPlaybackProbeTick = (1 << 21) + 8;
  SyntheticDemo rowFloodDemo;
  rowFloodDemo.addPacket(0, nopPacket());
  rowFloodDemo.addPacket(kPlaybackProbeTick, nopPacket());
  rowFloodDemo.addStop(kPlaybackProbeTick);
  SyntheticScan rowFlood;
  const bool rowFloodBuilt = scanSyntheticDemo(rowFloodDemo.bytes(), rowFlood);
  // One row: the good entry at tick 0. Two rows would mean the bad entry was
  // sized before being refused, which is the allocation the cap exists to stop.
  const std::wstring rowFloodReason = lastBudgetReason();
  const bool rejectedPlaybackRows = rowFloodBuilt
      && rowFlood.summary.malformedPackets > 0
      && rowFlood.summary.packetServerTicks.size() == 1u
      && rowFloodReason == L"playback-row-cap";

  // Positive control: a 100-tick span must still index row 100 exactly.
  SyntheticDemo rowControlDemo;
  rowControlDemo.addPacket(0, nopPacket());
  rowControlDemo.addPacket(100, nopPacket());
  rowControlDemo.addStop(100);
  SyntheticScan rowControl;
  const bool rowControlBuilt = scanSyntheticDemo(rowControlDemo.bytes(), rowControl);
  const bool acceptedPlaybackControl = rowControlBuilt && rowControl.indexOk
      && rowControl.summary.malformedPackets == 0
      && rowControl.summary.packetServerTicks.size() == 101u
      && lastBudgetReason().empty();

  // The last legal row and the first illegal one. With the cap read as a row
  // COUNT, index cap - 1 needs exactly cap rows and must be kept, while index cap
  // needs cap + 1 and must be refused -- a ">" test accepts the second case and
  // lets one row past the budget.
  SyntheticDemo rowEdgeDemo;
  rowEdgeDemo.addPacket(0, nopPacket());
  rowEdgeDemo.addPacket(static_cast<std::int32_t>(kExpectedMaxRows - 2), nopPacket());
  rowEdgeDemo.addStop(static_cast<std::int32_t>(kExpectedMaxRows - 2));
  SyntheticScan rowEdge;
  const bool rowEdgeBuilt = scanSyntheticDemo(rowEdgeDemo.bytes(), rowEdge);
  const std::wstring rowEdgeReason = lastBudgetReason();
  const bool acceptedPlaybackEdge = rowEdgeBuilt && rowEdge.indexOk
      && rowEdge.summary.malformedPackets == 0
      && rowEdge.summary.packetServerTicks.size() == static_cast<std::size_t>(kExpectedMaxRows - 1)
      && rowEdgeReason.empty();

  SyntheticDemo rowAtCapDemo;
  rowAtCapDemo.addPacket(0, nopPacket());
  rowAtCapDemo.addPacket(static_cast<std::int32_t>(kExpectedMaxRows), nopPacket());
  rowAtCapDemo.addStop(static_cast<std::int32_t>(kExpectedMaxRows));
  SyntheticScan rowAtCap;
  const bool rowAtCapBuilt = scanSyntheticDemo(rowAtCapDemo.bytes(), rowAtCap);
  const std::wstring rowAtCapReason = lastBudgetReason();
  const bool rejectedPlaybackAtCap = rowAtCapBuilt
      && rowAtCap.summary.malformedPackets > 0
      && rowAtCap.summary.packetServerTicks.size() == 1u
      && rowAtCapReason == L"playback-row-cap";

  std::wcout << L"parser_bounds_selftest missing_slot_rejected=" << (rejectedMissingSlot ? 1 : 0)
             << L" valid_control=" << (acceptedControl ? 1 : 0)
             << L" nan_time_rejected=" << (rejectedNanTime ? 1 : 0)
             << L" infinite_time_rejected=" << (rejectedInfiniteTime ? 1 : 0)
             << L" finite_time_control=" << (acceptedFiniteTime ? 1 : 0)
             << L" malformed_error=" << std::wstring(malformed.error.begin(), malformed.error.end())
             << L" valid_entries=" << valid.entries.size()
             << L" datatable_schema_flood_rejected=" << (rejectedSchemaFlood ? 1 : 0)
             << L" datatable_schema_flood_tables=" << schemaFlood.summary.dataTableDefinitionCount
             << L" datatable_schema_control=" << (acceptedSchemaControl ? 1 : 0)
             << L" datatable_flatten_budget_rejected=" << (rejectedDiamondBudget ? 1 : 0)
             << L" datatable_flatten_props=" << diamond.summary.dataTableFlattenedPropCount
             << L" datatable_flatten_control=" << (acceptedDiamondControl ? 1 : 0)
             << L" playback_row_cap_rejected=" << (rejectedPlaybackRows ? 1 : 0)
             << L" playback_rows=" << rowFlood.summary.packetServerTicks.size()
             << L" playback_row_control=" << (acceptedPlaybackControl ? 1 : 0)
             << L" datatable_flatten_edge=" << (acceptedFlattenEdge ? 1 : 0)
             << L" datatable_flatten_over_rejected=" << (rejectedFlattenOver ? 1 : 0)
             << L" playback_row_edge=" << (acceptedPlaybackEdge ? 1 : 0)
             << L" playback_row_at_cap_rejected=" << (rejectedPlaybackAtCap ? 1 : 0)
             << L" datatable_schema_flood_reason=" << schemaFloodReason
             << L" datatable_flatten_reason=" << diamondReason
             << L" datatable_flatten_over_reason=" << flattenOverReason
             << L" playback_row_reason=" << rowFloodReason
             << L" playback_row_at_cap_reason=" << rowAtCapReason << L'\n';
  return rejectedMissingSlot && acceptedControl && rejectedNanTime
      && rejectedInfiniteTime && acceptedFiniteTime && rejectedSchemaFlood
      && acceptedSchemaControl && rejectedDiamondBudget && acceptedDiamondControl
      && acceptedFlattenEdge && rejectedFlattenOver && rejectedPlaybackRows
      && acceptedPlaybackControl && acceptedPlaybackEdge && rejectedPlaybackAtCap ? 0 : 1;
}

} // namespace

int wmain(int argc, wchar_t** argv) {
  if (argc < 2) {
    std::wcerr << L"usage: demo_open_probe [--scan] [--snapshot <tick>] [--camera-tick <tick>] [--events <n>] [--userinfo] [--userinfo-at <tick,tick,...>] [--userinfo-dump <n>] <demo.dem>\n"
                  L"       demo_open_probe --userinfo-fixture <file>\n";
    return 2;
  }
  // A synthetic message stream, decoded through the same entry point the demo
  // scan uses. The gate builds these bytes (a good svc_CreateStringTable for
  // "userinfo" and deliberately damaged copies of it), so the decoder is tested
  // against layouts no local demo isolates -- truncated messages among them,
  // which is the case that used to be indistinguishable from "no players".
  if (std::wstring(argv[1]) == L"--userinfo-fixture") {
    if (argc < 3) {
      std::wcerr << L"usage: demo_open_probe --userinfo-fixture <file>\n";
      return 2;
    }
    std::ifstream fixture(argv[2], std::ios::binary);
    if (!fixture) {
      std::wcerr << L"cannot open fixture: " << argv[2] << L"\n";
      return 2;
    }
    const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(fixture)),
                                          std::istreambuf_iterator<char>());
    tf2::native::DemoNetworkSummary summary;
    // Above 23 the payload length is a varint rather than a 20-bit field; the
    // gate writes its fixtures that way, and so does every modern demo.
    summary.networkProtocol = 24;
    tf2::native::DemoMessageStreamResult result;
    const bool streamOk = tf2::native::decodeDemoMessageStream(
      bytes, bytes.size() * 8u, 0, -1, summary, result, 0);
    std::size_t named = 0;
    std::size_t duplicateUserIds = 0;
    std::set<std::uint32_t> userIds;
    for (const auto& entry : summary.userInfos) {
      if (!entry.name.empty()) ++named;
      if (entry.userId != 0u && !userIds.insert(entry.userId).second) ++duplicateUserIds;
    }
    std::wcout << L"userinfo_fixture_bytes=" << bytes.size()
               << L" userinfo_fixture_stream_ok=" << (streamOk ? 1 : 0)
               << L" userinfo_fixture_decoded_any=" << (result.decodedAny ? 1 : 0)
               << L" userinfo_fixture_packet_valid=" << (result.packetValid ? 1 : 0)
               << L" userinfo_fixture_hit_unknown=" << (result.hitUnknownType ? 1 : 0)
               << L" userinfo_table_id=" << summary.userInfoTableId
               << L" userinfo_create=" << summary.userInfoCreateCount
               << L" userinfo_entries=" << summary.userInfoEntryCount
               << L" userinfo_slots=" << summary.userInfos.size()
               << L" userinfo_samples=" << summary.userInfoSamples.size()
               << L" userinfo_named=" << named
               << L" userinfo_failures=" << summary.userInfoDecodeFailures
               << L" userinfo_unparsed=" << summary.userInfoUnparsedCount
               << L" userinfo_missing_userdata=" << summary.userInfoMissingUserDataCount
               << L" userinfo_empty_names=" << summary.userInfoEmptyNameCount
               << L" userinfo_text_slot_mismatches=" << summary.userInfoTextSlotMismatchCount
               << L" userinfo_duplicate_userids=" << duplicateUserIds
               << L" userinfo_max_userdata_bits=" << summary.userInfoMaxUserDataBits
               << L" userinfo_layout_mask=" << summary.userInfoLayoutMask
               << L'\n';
    for (const auto& entry : summary.userInfos) {
      std::string row;
      row += std::to_string(entry.entryIndex);
      row += '\t';
      row += std::to_string(entry.entityIndex);
      row += '\t';
      row += std::to_string(entry.userId);
      row += '\t';
      row += entry.name;
      row += '\t';
      row += std::to_string(entry.layoutId);
      row += '\t';
      row += entry.tableText;
      row += '\n';
      std::wcout.flush();
      std::cout.write(row.data(), static_cast<std::streamsize>(row.size()));
    }
    std::cout.flush();
    return 0;
  }
  if (std::wstring(argv[1]) == L"--parser-bounds-selftest") {
    return runParserBoundsSelfTest();
  }
  bool scan = false;
  std::int32_t snapshotTick = -1;
  std::int32_t cameraTick = -1;
  int firstPath = 1;
  if (std::wstring(argv[1]) == L"--scan") { scan = true; firstPath = 2; }
  if (scan && firstPath + 1 < argc && std::wstring(argv[firstPath]) == L"--snapshot") {
    wchar_t* end = nullptr;
    const long parsed = std::wcstol(argv[firstPath + 1], &end, 10);
    if (end == argv[firstPath + 1] || *end != L'\0' || parsed < 0 || parsed > std::numeric_limits<std::int32_t>::max()) {
      std::wcerr << L"invalid snapshot tick\n";
      return 2;
    }
    snapshotTick = static_cast<std::int32_t>(parsed);
    firstPath += 2;
  }
  if (scan && firstPath + 1 < argc && std::wstring(argv[firstPath]) == L"--camera-tick") {
    wchar_t* end = nullptr;
    const long parsed = std::wcstol(argv[firstPath + 1], &end, 10);
    if (end == argv[firstPath + 1] || *end != L'\0' || parsed < 0 || parsed > std::numeric_limits<std::int32_t>::max()) {
      std::wcerr << L"invalid camera tick\n";
      return 2;
    }
    cameraTick = static_cast<std::int32_t>(parsed);
    firstPath += 2;
  }

  std::vector<std::int32_t> serverTickAts;
  if (scan && firstPath + 1 < argc && std::wstring(argv[firstPath]) == L"--server-tick-at") {
    const std::wstring list(argv[firstPath + 1]);
    std::size_t start = 0;
    while (start <= list.size()) {
      const std::size_t comma = list.find(L',', start);
      const std::wstring item = list.substr(start, comma == std::wstring::npos ? std::wstring::npos : comma - start);
      if (!item.empty()) {
        wchar_t* end = nullptr;
        const long parsed = std::wcstol(item.c_str(), &end, 10);
        if (end == item.c_str() || *end != L'\0' || parsed < 0 || parsed > std::numeric_limits<std::int32_t>::max()) {
          std::wcerr << L"invalid server tick index\n";
          return 2;
        }
        serverTickAts.push_back(static_cast<std::int32_t>(parsed));
      }
      if (comma == std::wstring::npos) break;
      start = comma + 1;
    }
    firstPath += 2;
  }

  // --events <n>: dump decoded svc_GameEvent rows in wire order, n rows (0 = all).
  // Only meaningful with --scan, like the other dump switches.
  int eventLimit = -1;
  if (scan && firstPath + 1 < argc && std::wstring(argv[firstPath]) == L"--events") {
    wchar_t* end = nullptr;
    const long parsed = std::wcstol(argv[firstPath + 1], &end, 10);
    if (end == argv[firstPath + 1] || *end != L'\0' || parsed < 0 || parsed > std::numeric_limits<int>::max()) {
      std::wcerr << L"invalid events limit\n";
      return 2;
    }
    eventLimit = static_cast<int>(parsed);
    firstPath += 2;
  }
  // --userinfo: print the decoded "userinfo" string table. --userinfo-dump <n>:
  // also hex-dump the first n slots' raw player_info_t bytes, so the layout can be
  // read out of the demo instead of assumed. --userinfo-at a,b,a: resolve every
  // slot's name at those ticks, in the order given -- repeating a tick in one
  // process is the cheap form of "seek back and forth and get the same answer".
  bool userInfo = false;
  int userInfoDumpLimit = -1;
  std::vector<std::int32_t> userInfoAtTicks;
  if (scan && firstPath < argc && std::wstring(argv[firstPath]) == L"--userinfo") {
    userInfo = true;
    ++firstPath;
  }
  if (scan && firstPath + 1 < argc && std::wstring(argv[firstPath]) == L"--userinfo-dump") {
    wchar_t* end = nullptr;
    const long parsed = std::wcstol(argv[firstPath + 1], &end, 10);
    if (end == argv[firstPath + 1] || *end != L'\0' || parsed < 0 || parsed > std::numeric_limits<int>::max()) {
      std::wcerr << L"invalid userinfo dump limit\n";
      return 2;
    }
    userInfoDumpLimit = static_cast<int>(parsed);
    firstPath += 2;
  }
  if (scan && firstPath + 1 < argc && std::wstring(argv[firstPath]) == L"--userinfo-at") {
    const std::wstring list(argv[firstPath + 1]);
    std::size_t start = 0;
    while (start <= list.size()) {
      const std::size_t comma = list.find(L',', start);
      const std::wstring item = list.substr(start, comma == std::wstring::npos ? std::wstring::npos : comma - start);
      if (!item.empty()) {
        wchar_t* end = nullptr;
        const long parsed = std::wcstol(item.c_str(), &end, 10);
        if (end == item.c_str() || *end != L'\0' || parsed < 0 || parsed > std::numeric_limits<std::int32_t>::max()) {
          std::wcerr << L"invalid userinfo tick\n";
          return 2;
        }
        userInfoAtTicks.push_back(static_cast<std::int32_t>(parsed));
      }
      if (comma == std::wstring::npos) break;
      start = comma + 1;
    }
    firstPath += 2;
  }
  if (firstPath >= argc) {
    std::wcerr << L"usage: demo_open_probe [--scan] [--snapshot <tick>] [--camera-tick <tick>] [--events <n>] [--userinfo] [--userinfo-dump <n>] [--userinfo-at a,b,a] <demo.dem>\n";
    return 2;
  }
  for (int i = firstPath; i < argc; ++i) {
    const std::filesystem::path path(argv[i]);
    // Raw UTF-8 row dump, written with the narrow stream after the wide summary
    // line: wcout would convert these bytes through the active C locale and
    // mangle the UTF-8 player names that golden rows contain.
    std::string eventDump;
    std::string userInfoDumpText;
    std::size_t eventRows = 0;
    std::size_t eventDupes = 0;
    tf2::native::DemoHeader header;
    const bool headerOk = tf2::native::parseDemoHeaderFile(path, header);
    tf2::native::DemoIndex index;
    const bool indexOk = headerOk && tf2::native::indexDemoFile(path, header, index);
    // Keep probe output ASCII-compatible; the input path itself may contain
    // characters unsupported by the active console code page.
    std::wcout << L"path=<wide-path>"
               << L" header=" << (headerOk ? 1 : 0)
               << L" header_error=" << std::wstring(header.error.begin(), header.error.end())
               << L" index=" << (indexOk ? 1 : 0)
               << L" index_error=" << std::wstring(index.error.begin(), index.error.end())
               << L" recording=" << std::wstring(tf2::native::demoRecordingTypeName(header.recordingType),
                                                  tf2::native::demoRecordingTypeName(header.recordingType) +
                                                  std::strlen(tf2::native::demoRecordingTypeName(header.recordingType)))
               << L" map=" << std::wstring(header.mapName.begin(), header.mapName.end())
               << L" malformed_offset=" << index.malformedOffset
               << L" commands=" << index.commandCount
               << L" packets=" << index.packetCount
               << L" ticks=" << header.ticks
               << L" frames=" << header.frames;
    if (scan && indexOk) {
      tf2::native::DemoNetworkSummary summary;
      summary.networkProtocol = header.networkProtocol;
      const bool scanOk = tf2::native::scanKnownDemoMessages(path, index, summary);
      std::string firstTempClassName;
      for (const auto& serverClass : summary.serverClassSchemas) {
        if (serverClass.id == static_cast<std::uint16_t>(summary.firstTempEntityFailureClass)) {
          firstTempClassName = serverClass.name;
          break;
        }
      }
      std::string firstEntityClassName;
      std::size_t firstEntityPropCount = 0;
      for (const auto& serverClass : summary.serverClassSchemas) {
        if (serverClass.id != static_cast<std::uint16_t>(summary.firstEntityPropFailureClass)) continue;
        firstEntityClassName = serverClass.name;
        for (const auto& table : summary.sendTableSchemas) {
          if (table.name == serverClass.dataTable) firstEntityPropCount = table.flattenedProps.size();
        }
        break;
      }
      const auto wide = [](const std::string& value) {
        return std::wstring(value.begin(), value.end());
      };
      if (eventLimit >= 0) {
        // Dedupe key is (tick, packetOrdinal, messageBit), NOT tick: bagel has
        // 2,915 ticks with more than one event and tick 334 alone carries 209,
        // so a tick-only key silently loses almost every same-tick event.
        std::set<std::tuple<std::int32_t, std::uint32_t, std::uint32_t>> seen;
        for (const auto& event : summary.gameEvents) {
          const auto key = std::make_tuple(event.tick, event.packetOrdinal, event.messageBit);
          if (!seen.insert(key).second) { ++eventDupes; continue; }
          if (eventLimit > 0 && eventRows >= static_cast<std::size_t>(eventLimit)) break;
          eventDump += std::to_string(event.tick);
          eventDump += '\t';
          eventDump += std::to_string(event.eventId);
          eventDump += '\t';
          eventDump += event.name;
          eventDump += '\t';
          for (std::size_t field = 0; field < event.fields.size(); ++field) {
            if (field != 0) eventDump += ';';
            eventDump += event.fields[field].first;
            eventDump += '=';
            eventDump += event.fields[field].second;
          }
          eventDump += '\n';
          ++eventRows;
        }
      }
      std::wcout << L" scan=" << (scanOk ? 1 : 0)
                << L" source_tv=" << (summary.sourceTv ? 1 : 0)
                << L" server_name=" << std::wstring(summary.serverName.begin(), summary.serverName.end())
                << L" server_map=" << std::wstring(summary.serverMap.begin(), summary.serverMap.end())
                << L" setview_count=" << summary.setViewCount
                << L" last_view_entity=" << summary.lastViewEntity
                << L" signon_states=" << summary.signonStateCount
                << L" last_signon=" << summary.lastSignonState
                << L" classes=" << summary.serverClassCount
                << L" class_info=" << summary.classInfoCount
                << L" datatable_packets=" << summary.dataTablePacketCount
                << L" datatable_defs=" << summary.dataTableDefinitionCount
                << L" datatable_classes=" << summary.dataTableServerClassCount
                << L" packets_scanned=" << summary.packetsScanned
                << L" malformed_packets=" << summary.malformedPackets
                << L" temp_events=" << summary.tempEventCount
                << L" temp_failures=" << summary.tempEventDecodeFailures
                << L" particles=" << summary.tempParticleEffectCount
                << L" explosions=" << summary.tempExplosionCount
                << L" firebullets=" << summary.tempFireBulletsCount
                << L" projectiles=" << summary.projectileTimeline.size()
                << L" sounds=" << summary.soundEventCount
                << L" sound_failures=" << summary.decodedSoundEventFailures
                << L" sound_matches=" << summary.decodedSoundResourceMatches
                << L" sound_misses=" << summary.decodedSoundResourceMisses
                << L" entities=" << summary.packetEntityUpdates
                << L" entity_failures=" << summary.packetEntityDecodeFailures
                << L" entity_header_failures=" << summary.entityUpdateHeaderFailures
                << L" entity_unknown_state_failures=" << summary.entityUnknownStateFailures
                << L" entity_missing_table_failures=" << summary.entityPropMissingTableFailures
                << L" entity_prop_index_failures=" << summary.entityPropIndexFailures
                << L" entity_prop_value_failures=" << summary.entityPropValueFailures
                << L" entity_history_gap=" << (summary.entityHistoryHasGap ? 1 : 0)
                << L" entity_history_delta_misses=" << summary.entityHistoryDeltaBaseMisses
                << L" entity_history_packets=" << summary.entityHistoryPackets.size()
                << L" entity_history_events=" << summary.entityHistoryEvents.size()
                << L" entity_history_checkpoints=" << summary.entityHistoryCheckpoints.size()
                << L" entity_history_first_tick=" << (summary.entityHistoryCheckpoints.empty() ? -1 : summary.entityHistoryCheckpoints.front().tick)
                << L" entity_history_last_tick=" << (summary.entityHistoryCheckpoints.empty() ? -1 : summary.entityHistoryCheckpoints.back().tick)
                << L" entity_delta_count=" << summary.packetEntityDeltaCount
                << L" entity_delta_base_unavailable=" << summary.packetEntityDeltaBaseUnavailableCount
                << L" first_delta_tick=" << summary.firstPacketEntitiesDeltaTick
                << L" first_delta_from=" << summary.firstPacketEntitiesDeltaFrom
                << L" first_unavailable_tick=" << summary.firstPacketEntitiesUnavailableTick
                << L" first_unavailable_from=" << summary.firstPacketEntitiesUnavailableFrom
                << L" first_unknown_tick=" << summary.firstEntityUnknownStateTick
                << L" first_unknown_entity=" << summary.firstEntityUnknownStateEntity
                << L" first_unknown_update=" << summary.firstEntityUnknownStateUpdate
                << L" first_unknown_max=" << summary.firstEntityUnknownStateMaxEntries
                << L" first_unknown_entries=" << summary.firstEntityUnknownStateUpdatedEntries
                << L" first_unknown_payload=" << summary.firstEntityUnknownStatePayloadBits
                << L" first_unknown_diff=" << summary.firstEntityUnknownStateDiff
                << L" first_entities_tick=" << summary.firstPacketEntitiesTick
                << L" first_entities_max=" << summary.firstPacketEntitiesMaxEntries
                << L" first_entities_entries=" << summary.firstPacketEntitiesUpdatedEntries
                << L" first_entities_payload=" << summary.firstPacketEntitiesPayloadBits
                << L" first_entities_delta=" << summary.firstPacketEntitiesDelta
                << L" first_enter_tick=" << summary.firstPacketEntitiesEnterTick
                << L" first_preserve_tick=" << summary.firstPacketEntitiesPreserveTick
                << L" first_entity_update_bit=" << summary.firstPacketEntitiesFirstUpdateBit
                << L" first_entity_update_diff=" << summary.firstPacketEntitiesFirstDiff
                << L" first_entities_message_bit=" << summary.firstPacketEntitiesMessageBit
                << L" first_entity_failure_stage=" << wide(summary.firstEntityPropFailureStage)
                << L" first_entity_failure_tick=" << summary.firstEntityPropFailureTick
                << L" first_entity_failure_name=" << wide(summary.firstEntityPropFailureName)
                << L" first_entity_failure_class=" << summary.firstEntityPropFailureClass
                << L" first_entity_failure_index=" << summary.firstEntityPropFailureIndex
                << L" first_entity_failure_entity=" << summary.firstEntityPropFailureEntity
                << L" first_entity_failure_type=" << summary.firstEntityPropFailureType
                << L" first_entity_failure_flags=" << summary.firstEntityPropFailureFlags
                << L" first_entity_failure_bits=" << summary.firstEntityPropFailureBits
                << L" first_entity_failure_class_name=" << wide(firstEntityClassName)
                << L" first_entity_failure_prop_count=" << firstEntityPropCount
                << L" first_temp_failure_stage=" << wide(summary.firstTempEntityFailureStage)
                << L" first_temp_failure_name=" << wide(summary.firstTempEntityFailureName)
                << L" first_temp_failure_class=" << summary.firstTempEntityFailureClass
                << L" first_temp_failure_class_name=" << wide(firstTempClassName)
                << L" first_temp_failure_type=" << summary.firstTempEntityFailureType
                << L" first_temp_failure_flags=" << summary.firstTempEntityFailureFlags
                << L" first_temp_failure_bits=" << summary.firstTempEntityFailureBits
                << L" first_temp_failure_tick=" << summary.firstTempEntityFailureTick
                << L" unknown_packets=" << summary.unknownMessagePackets
                << L" eventlists=" << summary.gameEventListCount
                << L" events=" << summary.gameEventCount
                << L" eventdefs=" << summary.gameEventDefinitionsParsed
                << L" eventdecoded=" << summary.gameEventDecoded
                << L" eventfail=" << summary.gameEventDecodeFailures
                << L" eventunknown=" << summary.gameEventUnknownId
                << L" event_rows=" << eventRows
                << L" event_dupes=" << eventDupes;
      std::wcout << L" fixangle_valid=" << (summary.lastFixAngleValid ? 1 : 0)
                 << L" fixangle_relative=" << (summary.lastFixAngleRelative ? 1 : 0)
                 << L" fixangle=" << summary.lastFixAngle[0] << L"," << summary.lastFixAngle[1]
                 << L"," << summary.lastFixAngle[2]
                 << L" camera_track=" << summary.viewSamples.size()
                 << L" camera_track_dropped=" << summary.viewSamplesDropped
                 << L" view_signon_samples=" << summary.signonViewSamples
                 << L" view_out_of_order=" << summary.viewSamplesOutOfOrder
                 << L" view_out_of_order_after_repair=" << summary.viewSamplesOutOfOrderAfterRepair
                 << L" view_duplicates=" << summary.viewSamplesDuplicates
                 << L" view_strictly_increasing=" << (summary.viewSamplesOrdered ? 1 : 0)
                 << L" first_signon_entry_tick=" << summary.firstSignonEntryTick
                 << L" first_data_entry_tick=" << summary.firstDataEntryTick
                 << L" last_data_entry_tick=" << summary.lastDataEntryTick
                 << L" first_data_net_tick=" << summary.firstDataNetTick
                 << L" net_minus_entry=" << (summary.firstDataNetTick - summary.firstDataEntryTick)
                 << L" view_ordered=" << (summary.viewSamplesOrdered ? 1 : 0)
                 << L" packet_server_ticks=" << summary.packetServerTicks.size()
                 << L" data_packets=" << summary.dataPacketCount
                 << L" server_tick_held=" << summary.serverTickHeldCount
                 << L" server_tick_known_count=" << summary.serverTickKnownCount
                 << L" server_tick_gaps=" << summary.serverTickGaps
                 << L" server_tick_stalls=" << summary.serverTickStalls
                 << L" fixangle_samples=" << summary.fixAngleSamples.size()
                 << L" packets_with_own_net_tick=" << summary.packetsWithOwnNetTick
                 << L" first_data_packet_own_net_tick=" << summary.firstDataPacketOwnNetTick;
      {
        std::wstring first5, last5;
        const std::size_t sampleCount = summary.viewSamples.size();
        const std::size_t tailStart = sampleCount > 5u ? sampleCount - 5u : 0u;
        for (std::size_t i = 0; i < sampleCount && i < 5u; ++i) {
          if (i != 0) first5 += L",";
          first5 += std::to_wstring(summary.viewSamples[i].tick);
        }
        for (std::size_t i = tailStart; i < sampleCount; ++i) {
          if (i != tailStart) last5 += L",";
          last5 += std::to_wstring(summary.viewSamples[i].tick);
        }
        std::wcout << L" view_first5=" << first5 << L" view_last5=" << last5;
      }
      {
        std::wstring firstTicks, lastTicks;
        const std::size_t tickCount = summary.packetServerTicks.size();
        const std::size_t tickTail = tickCount > 5u ? tickCount - 5u : 0u;
        for (std::size_t i = 0; i < tickCount && i < 5u; ++i) {
          if (i != 0) firstTicks += L",";
          firstTicks += std::to_wstring(summary.packetServerTicks[i]);
        }
        for (std::size_t i = tickTail; i < tickCount; ++i) {
          if (i != tickTail) lastTicks += L",";
          lastTicks += std::to_wstring(summary.packetServerTicks[i]);
        }
        std::wcout << L" packet_server_first5=" << firstTicks << L" packet_server_last5=" << lastTicks;
      }
      for (const std::int32_t idx : serverTickAts) {
        std::int32_t value = -1;
        if (idx >= 0 && static_cast<std::size_t>(idx) < summary.packetServerTicks.size()) {
          value = summary.packetServerTicks[static_cast<std::size_t>(idx)];
        }
        std::int32_t own = 0;
        if (idx >= 0 && static_cast<std::size_t>(idx) < summary.packetServerTickOwn.size()) {
          own = summary.packetServerTickOwn[static_cast<std::size_t>(idx)];
        }
        std::wcout << L" server_tick_at_" << idx << L"=" << value
                   << L" server_tick_own_at_" << idx << L"=" << own;
      }
      if (cameraTick >= 0) {
        tf2::native::DemoViewSample camera;
        const bool found = tf2::native::findObserverViewAtOrBeforeTick(summary, cameraTick, camera);
        std::wcout << L" camera_tick=" << cameraTick << L" camera_found=" << (found ? 1 : 0)
                   << L" camera_sample_tick=" << camera.tick
                   << L" camera_angles_valid=" << (camera.hasAngles ? 1 : 0)
                   << L" camera_origin_valid=" << (camera.hasOrigin ? 1 : 0)
                   << L" camera_origin=" << camera.origin[0] << L"," << camera.origin[1] << L"," << camera.origin[2]
                   << L" camera_pitch=" << camera.angles[0] << L" camera_yaw=" << camera.angles[1];
      }
      {
        // How far a checkpoint can sit behind a requested tick. The gate reads
        // this so it can assert the product's staleness equals the history's own
        // density instead of demanding a zero a checkpoint store cannot give.
        std::int32_t historyGapMax = 0;
        for (std::size_t i = 1; i < summary.entityHistoryCheckpoints.size(); ++i) {
          const std::int32_t gap = summary.entityHistoryCheckpoints[i].tick
            - summary.entityHistoryCheckpoints[i - 1].tick;
          if (gap > historyGapMax) historyGapMax = gap;
        }
        // The snapshot query resolves against the THINNED ARCHIVE once the live
        // window has flushed, so the archive's own spacing is the bound a
        // staleness reading has to be compared with; the live vector holds only
        // the two checkpoints the window still carries.
        std::int32_t archiveGapMax = 0;
        for (std::size_t i = 1; i < summary.entityHistoryArchive.size(); ++i) {
          const std::int32_t gap = summary.entityHistoryArchive[i].tick
            - summary.entityHistoryArchive[i - 1].tick;
          if (gap > archiveGapMax) archiveGapMax = gap;
        }
        std::wcout << L" entity_history_checkpoints=" << summary.entityHistoryCheckpoints.size()
                   << L" entity_history_gap_max=" << historyGapMax
                   << L" entity_history_archive=" << summary.entityHistoryArchive.size()
                   << L" entity_history_archive_gap_max=" << archiveGapMax;
      }
      if (snapshotTick >= 0) {
        std::vector<tf2::native::EntityState> states;
        std::int32_t resolvedTick = snapshotTick;
        const auto status = tf2::native::queryEntitySnapshotAtOrBeforeTick(summary, snapshotTick, states, &resolvedTick);
        const auto statusName = [](tf2::native::EntitySnapshotQueryStatus value) {
          switch (value) {
            case tf2::native::EntitySnapshotQueryStatus::Available: return L"available";
            case tf2::native::EntitySnapshotQueryStatus::Checkpoint: return L"checkpoint";
            case tf2::native::EntitySnapshotQueryStatus::NoHistory: return L"no-history";
            case tf2::native::EntitySnapshotQueryStatus::TickBeforeHistory: return L"before-history";
            case tf2::native::EntitySnapshotQueryStatus::Gap: return L"gap";
            case tf2::native::EntitySnapshotQueryStatus::DeltaBaseMissing: return L"delta-base-missing";
          }
          return L"unknown";
        };
        std::wcout << L" snapshot_tick=" << snapshotTick
                   << L" snapshot_status=" << statusName(status)
                   << L" snapshot_states=" << states.size()
                   << L" snapshot_resolved_tick=" << resolvedTick
                   << L" snapshot_stale=" << (snapshotTick - resolvedTick);
      }
      if (userInfo || userInfoDumpLimit >= 0 || !userInfoAtTicks.empty()) {
        std::size_t named = 0;
        std::size_t duplicateUserIds = 0;
        std::set<std::uint32_t> userIds;
        for (const auto& entry : summary.userInfos) {
          if (!entry.name.empty()) ++named;
          // userId 0 means "slot has no player yet" and is not a duplicate.
          // Anything else appears on exactly one slot, so a second sighting is a
          // misread slot index or a misread struct -- which is why this is a
          // counter the gate can demand at zero rather than a comment.
          if (entry.userId != 0u && !userIds.insert(entry.userId).second) ++duplicateUserIds;
        }
        std::wcout << L" userinfo_table_id=" << summary.userInfoTableId
                   << L" userinfo_create=" << summary.userInfoCreateCount
                   << L" userinfo_updates=" << summary.userInfoUpdateCount
                   << L" userinfo_entries=" << summary.userInfoEntryCount
                   << L" userinfo_slots=" << summary.userInfos.size()
                   << L" userinfo_samples=" << summary.userInfoSamples.size()
                   << L" userinfo_named=" << named
                   << L" userinfo_failures=" << summary.userInfoDecodeFailures
                   << L" userinfo_unparsed=" << summary.userInfoUnparsedCount
                   << L" userinfo_missing_userdata=" << summary.userInfoMissingUserDataCount
                   << L" userinfo_empty_names=" << summary.userInfoEmptyNameCount
                   << L" userinfo_text_slot_mismatches=" << summary.userInfoTextSlotMismatchCount
                   << L" userinfo_duplicate_userids=" << duplicateUserIds
                   << L" userinfo_table_resets=" << summary.userInfoTableResetCount
                   << L" userinfo_fixed=" << (summary.userInfoFixedUserData ? 1 : 0)
                   << L" userinfo_fixed_bits=" << summary.userInfoFixedUserDataBits
                   << L" userinfo_max_entries=" << summary.userInfoMaxEntries
                   << L" userinfo_max_userdata_bits=" << summary.userInfoMaxUserDataBits
                   << L" userinfo_layout_mask=" << summary.userInfoLayoutMask;
        if (userInfo) {
          for (const auto& entry : summary.userInfos) {
            userInfoDumpText += std::to_string(entry.entryIndex);
            userInfoDumpText += '\t';
            userInfoDumpText += std::to_string(entry.entityIndex);
            userInfoDumpText += '\t';
            userInfoDumpText += std::to_string(entry.userId);
            userInfoDumpText += '\t';
            userInfoDumpText += entry.name;
            userInfoDumpText += '\t';
            userInfoDumpText += std::to_string(entry.firstTick);
            userInfoDumpText += '\t';
            userInfoDumpText += std::to_string(entry.lastTick);
            userInfoDumpText += '\t';
            userInfoDumpText += std::to_string(entry.updateCount);
            userInfoDumpText += '\t';
            userInfoDumpText += std::to_string(entry.layoutId);
            userInfoDumpText += '\t';
            userInfoDumpText += entry.tableText;
            userInfoDumpText += '\t';
            userInfoDumpText += entry.guid;
            userInfoDumpText += '\n';
          }
        }
        if (userInfoDumpLimit > 0) {
          char hex[4];
          int dumped = 0;
          for (const auto& entry : summary.userInfos) {
            if (dumped >= userInfoDumpLimit) break;
            ++dumped;
            userInfoDumpText += "userinfo_raw\t";
            userInfoDumpText += std::to_string(entry.entryIndex);
            userInfoDumpText += '\t';
            userInfoDumpText += std::to_string(entry.userDataBytes * 8u);
            userInfoDumpText += '\t';
            for (const std::uint8_t byte : entry.userData) {
              std::snprintf(hex, sizeof(hex), "%02x", static_cast<unsigned>(byte));
              userInfoDumpText += hex;
            }
            userInfoDumpText += '\n';
          }
        }
        for (const std::int32_t tick : userInfoAtTicks) {
          const auto resolved = tf2::native::userInfosAtTick(summary, tick);
          for (const auto& entry : resolved) {
            userInfoDumpText += "userinfo_at\t";
            userInfoDumpText += std::to_string(tick);
            userInfoDumpText += '\t';
            userInfoDumpText += std::to_string(entry.entryIndex);
            userInfoDumpText += '\t';
            userInfoDumpText += std::to_string(entry.entityIndex);
            userInfoDumpText += '\t';
            userInfoDumpText += std::to_string(entry.userId);
            userInfoDumpText += '\t';
            userInfoDumpText += entry.name;
            userInfoDumpText += '\t';
            userInfoDumpText += std::to_string(entry.layoutId);
            userInfoDumpText += '\n';
          }
        }
      }
    }
    std::wcout << L'\n';
    if (!eventDump.empty() || !userInfoDumpText.empty()) {
      std::wcout.flush();
      std::cout.write(eventDump.data(), static_cast<std::streamsize>(eventDump.size()));
      std::cout.write(userInfoDumpText.data(), static_cast<std::streamsize>(userInfoDumpText.size()));
      std::cout.flush();
    }
  }
  return 0;
}
