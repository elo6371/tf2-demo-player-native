#include "demo_header.h"
#include "demo_tick_audit.h"
#include "gameplay_effects.h"
// task-39: rankPropertyCandidates is the entity model's own suffix matcher and
// ordering, exported so a diagnostic prints what the selection rule would pick
// instead of a second implementation that can drift from it.
#include "entity_model.h"
// task-40: the wearable table needs the item schema (the path route that answers
// most of these instances) and the asset root (only to FIND items_game.txt). The
// probe links tf2_core, so both are already available here.
#include "asset_root.h"
#include "item_schema.h"
#include "wearable_instance.h"
// task-41: the bone merge and the fixture suite that proves the attribution rules
// it consumes. Both are linked into this probe only.
#include "wearable_fixture.h"
#include "wearable_pose.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
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

// task-39 gate: the reading rules a wearable row needs, expressed the way the
// entity model already expresses them. rankPropertyCandidates is the selection
// rule's own suffix matcher and ordering, so the slot a row prints is the slot
// the rule would pick -- a second matcher here could drift from it and the drift
// would be invisible exactly when the rule is the thing under test. Properties are
// stored table-qualified ("DT_BaseEntity.m_nModelIndex"), so nothing may be looked
// up by exact key.
const tf2::native::EntityPropertyValue* rankedValue(const tf2::native::EntityState& state,
                                                    const char* suffix, std::string* name) {
  const std::vector<tf2::native::PropertyCandidate> ranked =
      tf2::native::rankPropertyCandidates(state, suffix);
  if (ranked.empty()) return nullptr;
  const auto found = state.properties.find(ranked.front().name);
  if (found == state.properties.end()) return nullptr;
  if (name != nullptr) *name = found->first;
  return &found->second;
}

// The Float/Int coercion readIntSlot applies: a prop can arrive as either type and
// only the type decides which field holds the reading.
bool rankedInt(const tf2::native::EntityState& state, const char* suffix, std::int64_t& out) {
  const tf2::native::EntityPropertyValue* value = rankedValue(state, suffix, nullptr);
  if (value == nullptr) return false;
  out = value->type == tf2::native::SendPropType::Float
      ? static_cast<std::int64_t>(value->x) : value->intValue;
  return true;
}

// Vector / VectorXY only. VectorXY drops z on the wire, so z is read from the
// "<base>[2]" sibling the way readVectorProperty does, and the caller is told when
// that sibling is absent rather than handed a zero dressed up as a reading.
bool rankedVector(const tf2::native::EntityState& state, const char* suffix, float out[3],
                  bool* complete) {
  if (complete != nullptr) *complete = true;
  const std::vector<tf2::native::PropertyCandidate> ranked =
      tf2::native::rankPropertyCandidates(state, suffix);
  for (const tf2::native::PropertyCandidate& candidate : ranked) {
    const auto found = state.properties.find(candidate.name);
    if (found == state.properties.end()) continue;
    const tf2::native::EntityPropertyValue& value = found->second;
    if (value.type != tf2::native::SendPropType::Vector
        && value.type != tf2::native::SendPropType::VectorXY) continue;
    const bool carriesZ = value.type == tf2::native::SendPropType::Vector;
    out[0] = value.x;
    out[1] = value.y;
    out[2] = carriesZ ? value.z : 0.0f;
    if (!carriesZ) {
      const std::string scalar = std::string(suffix) + "[2]";
      std::int64_t fallback = 0;
      if (!rankedInt(state, scalar.c_str(), fallback)) {
        if (complete != nullptr) *complete = false;
      } else {
        out[2] = static_cast<float>(fallback);
      }
    }
    return true;
  }
  return false;
}

// classId -> class name. serverClassSchemas carries an explicit id; the positional
// serverClassNames array only lines up when the wire order happens to match, so it
// is not a safe index.
const std::string* serverClassName(const tf2::native::DemoNetworkSummary& summary,
                                   std::int32_t classId) {
  if (classId < 0) return nullptr;
  const std::uint32_t wanted = static_cast<std::uint32_t>(classId);
  for (const tf2::native::ServerClassSchema& schema : summary.serverClassSchemas) {
    if (static_cast<std::uint32_t>(schema.id) == wanted && !schema.name.empty()) return &schema.name;
  }
  return nullptr;
}

// modelprecache lookup, the same bounds test demo_header_strtab.cpp uses: entry 0
// is the empty slot and the table is indexed by a 16-bit precache index.
const std::string* precachePath(const tf2::native::DemoNetworkSummary& summary,
                                std::int64_t index) {
  if (index < 1 || index > 0xffff) return nullptr;
  const auto found = summary.modelPrecache.find(static_cast<std::uint16_t>(index));
  if (found == summary.modelPrecache.end() || found->second.empty()) return nullptr;
  return &found->second;
}

// Source CBaseHandle: 11-bit entity index, 10-bit serial. Empty handles are the
// ones entity_model.cpp:872-928 already refuses as parents, plus 0 (worldspawn is
// not an owner).
constexpr std::uint32_t kWearableHandleIndexMask = 0x7FFu;
constexpr std::uint32_t kWearableInvalidHandle = 0xFFFFFFFFu;
constexpr std::uint32_t kWearableInvalidHandleFull = 0x1FFFFFu;
bool wearableHandleIndex(std::int64_t raw, std::uint32_t& index) {
  const std::uint32_t value = static_cast<std::uint32_t>(raw);
  if (value == 0 || value == kWearableInvalidHandle || value == kWearableInvalidHandleFull) {
    return false;
  }
  index = value & kWearableHandleIndexMask;
  return true;
}

// Case-insensitive substring test: the class-name allow list task-40 derives from
// this dump must not depend on how this engine happens to spell the case.
bool containsNoCase(const std::string& haystack, const char* needle) {
  const std::size_t length = std::strlen(needle);
  if (length == 0 || haystack.size() < length) return false;
  for (std::size_t start = 0; start + length <= haystack.size(); ++start) {
    std::size_t offset = 0;
    while (offset < length
           && std::tolower(static_cast<unsigned char>(haystack[start + offset]))
               == std::tolower(static_cast<unsigned char>(needle[offset]))) {
      ++offset;
    }
    if (offset == length) return true;
  }
  return false;
}

// task-39: the --snapshot spelling of a query status. --snapshot keeps its own
// inline copy so that switch's bytes cannot move; this copy is for --wearable-scan.
const wchar_t* snapshotStatusText(tf2::native::EntitySnapshotQueryStatus value) {
  switch (value) {
    case tf2::native::EntitySnapshotQueryStatus::Available: return L"available";
    case tf2::native::EntitySnapshotQueryStatus::Checkpoint: return L"checkpoint";
    case tf2::native::EntitySnapshotQueryStatus::NoHistory: return L"no-history";
    case tf2::native::EntitySnapshotQueryStatus::TickBeforeHistory: return L"before-history";
    case tf2::native::EntitySnapshotQueryStatus::Gap: return L"gap";
    case tf2::native::EntitySnapshotQueryStatus::DeltaBaseMissing: return L"delta-base-missing";
  }
  return L"unknown";
}

} // namespace

int wmain(int argc, wchar_t** argv) {
  if (argc < 2) {
    std::wcerr << L"usage: demo_open_probe [--scan] [--snapshot <tick>] [--hud-census <tick,...>] [--motion-census] [--wearable-scan <tick>] [--cosmetics-table <tick>] [--tf-root <dir>] [--camera-tick <tick>] [--events <n>] [--temps <n>] [--tick-audit] [--userinfo] [--userinfo-at <tick,tick,...>] [--userinfo-dump <n>] <demo.dem>\n"
                  L"       demo_open_probe --userinfo-fixture <file>\n"
                  L"       demo_open_probe --wearable-fixture-suite\n"
                  L"       demo_open_probe --wearable-pose-selftest\n";
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
  // task-41: the five attribution fixtures the card requires, run against the
  // real table builder, plus the bone-merge self-test. Both print one line per
  // check and answer with the number of failures, so a gate can use the exit
  // status without parsing.
  if (std::wstring(argv[1]) == L"--wearable-fixture-suite"
      || std::wstring(argv[1]) == L"--wearable-pose-selftest") {
    std::vector<std::string> report;
    std::size_t failed = 0;
    std::size_t ran = 0;
    if (std::wstring(argv[1]) == L"--wearable-fixture-suite") {
      failed += tf2::native::runWearableFixtureSuite(report);
    } else {
      failed += tf2::native::runWearablePoseSelftest(report);
    }
    ran = report.size();
    for (const std::string& line : report) {
      std::cout << line << '\n';
    }
    std::cout.flush();
    std::wcerr << L"wearable_selftest checks=" << ran << L" failed=" << failed << L'\n';
    return failed == 0 ? 0 : 1;
  }
  bool scan = false;
  // --camera-track: opt in to the N12-C per-tick camera row track. Recording it
  // costs a row per stamped packet (56 B each) and nothing reads it in the
  // product, so like the other dump switches it is off unless asked for.
  bool cameraTrack = false;
  // task-35: opt-in anomaly tick audit (file-layer clock/sequence integrity and
  // player-layer doubletap). Diagnostic only; it prints its own tick-audit rows.
  bool tickAudit = false;
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
  if (scan && firstPath < argc && std::wstring(argv[firstPath]) == L"--camera-track") {
    cameraTrack = true;
    firstPath += 1;
  }
  if (scan && firstPath < argc && std::wstring(argv[firstPath]) == L"--tick-audit") {
    tickAudit = true;
    firstPath += 1;
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
  // N12-C step 2 evidence: --camera-row dumps the camera track's own row at one or
  // more EXACT server ticks (comma separated). That row is what the product's eye
  // now reads, so it is the independent value the product trace is compared
  // against, printed at four/six decimals instead of the trace's 0.1 u quantum.
  std::vector<std::int32_t> cameraRowTicks;
  if (scan && firstPath + 1 < argc && std::wstring(argv[firstPath]) == L"--camera-row") {
    const std::wstring list(argv[firstPath + 1]);
    std::size_t start = 0;
    while (start <= list.size()) {
      const std::size_t comma = list.find(L',', start);
      const std::wstring item = list.substr(start, comma == std::wstring::npos ? std::wstring::npos : comma - start);
      if (!item.empty()) {
        wchar_t* end = nullptr;
        const long parsed = std::wcstol(item.c_str(), &end, 10);
        if (end == item.c_str() || *end != L'\0' || parsed < 0
            || parsed > std::numeric_limits<std::int32_t>::max()) {
          std::wcerr << L"invalid camera row tick\n";
          return 2;
        }
        cameraRowTicks.push_back(static_cast<std::int32_t>(parsed));
      }
      if (comma == std::wstring::npos) break;
      start = comma + 1;
    }
    firstPath += 2;
  }

  // N12-C step 3 evidence: --props <entityIndex> dumps every property the engine
  // actually put in the LIVE entity table for that index, sorted by name, with the
  // stored type and values. The camera row builder reads the SAME table, so this is
  // the only honest way to learn which names this engine sends: a guessed alias
  // ("m_vecViewOffset_Z", "m_vecViewOffset2", ...) would miss silently and the
  // flags counter would still read zero without saying why.
  int propsEntity = -1;
  if (scan && firstPath + 1 < argc && std::wstring(argv[firstPath]) == L"--props") {
    wchar_t* end = nullptr;
    const long parsed = std::wcstol(argv[firstPath + 1], &end, 10);
    if (end == argv[firstPath + 1] || *end != L'\0' || parsed < 0 || parsed > 65535) {
      std::wcerr << L"invalid props entity index\n";
      return 2;
    }
    propsEntity = static_cast<int>(parsed);
    firstPath += 2;
  }

  // task-39 gate: --wearable-scan <tick> answers one question with evidence -- can
  // a cosmetic (hat/badge/backpack) be rebuilt from the recording at all? The tick
  // is a SERVER tick, resolved through the same snapshot query the product's
  // entity table comes from, so a field that is missing here is missing in the
  // product too. Like --snapshot and --props it is gated on --scan, so the
  // baseline outputs of the other switches cannot move.
  std::int32_t wearableTick = -1;
  if (scan && firstPath + 1 < argc && std::wstring(argv[firstPath]) == L"--wearable-scan") {
    wchar_t* end = nullptr;
    const long parsed = std::wcstol(argv[firstPath + 1], &end, 10);
    if (end == argv[firstPath + 1] || *end != L'\0' || parsed < 0
        || parsed > std::numeric_limits<std::int32_t>::max()) {
      std::wcerr << L"invalid wearable tick\n";
      return 2;
    }
    wearableTick = static_cast<std::int32_t>(parsed);
    firstPath += 2;
  }

  // task-40: the attribution table. Separate from --wearable-scan on purpose --
  // that dump is pinned by the task-39 gate card and must not move by a byte, so
  // the new reading gets its own switch, off by default.
  std::int32_t cosmeticsTick = -1;
  if (scan && firstPath + 1 < argc && std::wstring(argv[firstPath]) == L"--cosmetics-table") {
    wchar_t* end = nullptr;
    const long parsed = std::wcstol(argv[firstPath + 1], &end, 10);
    if (end == argv[firstPath + 1] || *end != L'\0' || parsed < 0
        || parsed > std::numeric_limits<std::int32_t>::max()) {
      std::wcerr << L"invalid cosmetics tick\n";
      return 2;
    }
    cosmeticsTick = static_cast<std::int32_t>(parsed);
    firstPath += 2;
  }

  // Where items_game.txt lives. Without it the table still runs, reports
  // no-schema, and every instance that only the schema could answer is counted
  // as having no path -- it never falls back to guessing one.
  std::filesystem::path tfRoot;
  if (firstPath + 1 < argc && std::wstring(argv[firstPath]) == L"--tf-root") {
    tfRoot = std::filesystem::path(argv[firstPath + 1]);
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
  // N21(c)/task-29 evidence: --temps <n> dumps the temp-entity events the
  // decoder retained (16384 cap) together with the shooter and weapon id it
  // decoded from m_iPlayer / m_iWeaponID. The question it answers is whether
  // the recorder's own entity is a shooter on the wire at all: a POV-only
  // sound synthesiser is only honest if its trigger is a recorded event, and
  // "CTEFireBullets carries the recorder" has to be a reading, not an
  // assumption. 0 = all rows; the dump itself is printed after the summary.
  int tempLimit = -1;
  if (scan && firstPath + 1 < argc && std::wstring(argv[firstPath]) == L"--temps") {
    wchar_t* end = nullptr;
    const long parsed = std::wcstol(argv[firstPath + 1], &end, 10);
    if (end == argv[firstPath + 1] || *end != L'\0' || parsed < 0 || parsed > std::numeric_limits<int>::max()) {
      std::wcerr << L"invalid temps limit\n";
      return 2;
    }
    tempLimit = static_cast<int>(parsed);
    firstPath += 2;
  }
  // N20 step 0 (2026-11-22): --temp-census prints the retention account
  // (`temp-retention decoded= kept= dropped_cap= cap=`) and one
  // `temp-class <count> <name>` row per decoded server class, most frequent
  // first. It answers "which effect classes does the wire carry, and how many
  // did the 16384-row cap throw away" before anything is drawn. Printed only
  // when asked, so every other scan line stays byte-identical.
  bool tempCensus = false;
  if (scan && firstPath < argc && std::wstring(argv[firstPath]) == L"--temp-census") {
    tempCensus = true;
    ++firstPath;
  }
  // 2026-11-22 (WORKPLAN P3): --viewmodel-census prints, for every ViewModel-class
  // entity, how often m_nSequence / m_nAnimationParity / m_nModelIndex were
  // written and changed over the WHOLE recording, the values with their first
  // tick, and the first 64 sequence transitions. It answers "is a fire or reload
  // clip ever on the wire" before anyone builds on it. Must follow --temp-census
  // (and precede --userinfo): this scan reads its switches in a fixed order.
  bool viewModelCensus = false;
  if (scan && firstPath < argc && std::wstring(argv[firstPath]) == L"--viewmodel-census") {
    viewModelCensus = true;
    ++firstPath;
  }
  // P4 (2026-11-22): --fx-census prints what the medigun / flamethrower / player
  // condition properties did over the whole recording, every Ubercharge edge, and
  // which condition bits rose on the medic and the patient within half a second
  // of each charge -- the check gameplay_effects.h asks for before a default reads
  // the bits. Must follow --viewmodel-census and precede --userinfo.
  bool fxCensus = false;
  if (scan && firstPath < argc && std::wstring(argv[firstPath]) == L"--fx-census") {
    fxCensus = true;
    ++firstPath;
  }
  // H0 (2026-11-23, docs/UI-SHELL-DESIGN-2026-11-22.md section 7): --hud-census
  // <serverTick[,serverTick...]> dumps, from the snapshot at each tick, every
  // property of the entities a broadcast HUD reads -- player resource (health,
  // class, respawn), objective resource (control points), train watcher (payload),
  // teams (score, name), round timer, game rules. The fullscreen HUD is written
  // against the names this prints, not against guessed ones. Must follow
  // --fx-census and precede --userinfo.
  std::vector<std::int32_t> hudCensusTicks;
  if (scan && firstPath + 1 < argc && std::wstring(argv[firstPath]) == L"--hud-census") {
    const std::wstring list = argv[firstPath + 1];
    std::size_t start = 0;
    while (start <= list.size()) {
      const std::size_t comma = list.find(L',', start);
      const std::wstring part = list.substr(start, comma == std::wstring::npos ? std::wstring::npos : comma - start);
      wchar_t* end = nullptr;
      const long parsed = std::wcstol(part.c_str(), &end, 10);
      if (part.empty() || end == part.c_str() || *end != L'\0' || parsed < 0
          || parsed > std::numeric_limits<std::int32_t>::max()) {
        std::wcerr << L"invalid hud census tick\n";
        return 2;
      }
      hudCensusTicks.push_back(static_cast<std::int32_t>(parsed));
      if (comma == std::wstring::npos) break;
      start = comma + 1;
    }
    firstPath += 2;
  }
  // A1 (2026-11-23, docs/UI-SHELL-DESIGN-2026-11-22.md section 9): --motion-census
  // prints the distributions the three anomaly thresholds are chosen against: the
  // server-tick gap between data packets (stall), the per-tick distance a player
  // moved (teleport) and the per-tick angle the eyes turned (snap), with the
  // largest steps and what happened near them. Must follow --hud-census and
  // precede --userinfo.
  bool motionCensus = false;
  if (scan && firstPath < argc && std::wstring(argv[firstPath]) == L"--motion-census") {
    motionCensus = true;
    ++firstPath;
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
    std::wcerr << L"usage: demo_open_probe [--scan] [--snapshot <tick>] [--hud-census <tick,...>] [--motion-census] [--wearable-scan <tick>] [--cosmetics-table <tick>] [--tf-root <dir>] [--camera-tick <tick>] [--events <n>] [--temps <n>] [--tick-audit] [--userinfo] [--userinfo-dump <n>] [--userinfo-at a,b,a] <demo.dem>\n";
    return 2;
  }
  // N04/D46: a caller that checks the exit status must be able to tell a parsed
  // file from one that could not be opened; this probe used to answer 0 for all
  // four cases. 2 = could not open, 1 = opened but did not parse, 0 = parsed.
  int failureCode = 0;
  for (int i = firstPath; i < argc; ++i) {
    const std::filesystem::path path(argv[i]);
    // Raw UTF-8 row dump, written with the narrow stream after the wide summary
    // line: wcout would convert these bytes through the active C locale and
    // mangle the UTF-8 player names that golden rows contain.
    std::string eventDump;
    std::string userInfoDumpText;
    // N12-C step 3 evidence: sorted property-name dump (see --props).
    std::string propsDumpText;
    // N21(c)/task-29 evidence: temp-entity rows (see --temps).
    std::string tempDumpText;
    // task-35 evidence: tick-audit rows, empty unless --tick-audit was given.
    std::string tickAuditText;
    // task-39 evidence: wearable rows, empty unless --wearable-scan was given.
    std::string wearableDumpText;
    // task-40 evidence: the attribution table, empty unless --cosmetics-table was
    // given. Kept out of wearableDumpText so the gate-card bytes stay put.
    std::string cosmeticsTableText;
    std::size_t eventRows = 0;
    std::size_t eventDupes = 0;
    std::size_t tempRows = 0;
    tf2::native::DemoHeader header;
    const bool headerOk = tf2::native::parseDemoHeaderFile(path, header);
    tf2::native::DemoIndex index;
    const bool indexOk = headerOk && tf2::native::indexDemoFile(path, header, index);
    if (!headerOk || !indexOk) {
      if (failureCode == 0) failureCode = headerOk ? 1 : 2;
    }
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
      tf2::native::DemoTickAuditCapture tickAuditCapture;
      // task-40: the per-slot lifetime capture the attribution table reads. Armed
      // only when the table was asked for, so every other run records nothing.
      tf2::native::WearableSlotCapture wearableSlotCapture;
      tf2::native::DemoNetworkSummary summary;
      summary.networkProtocol = header.networkProtocol;
      summary.cameraTrackLimits.enabled = cameraTrack;
      // task-35: the capture is opt-in, so without the switch no packet row, sim
      // sample or anim event is recorded and every existing line stays identical.
      if (tickAudit || viewModelCensus || fxCensus || motionCensus) summary.tickAuditCapture = &tickAuditCapture;
      if (viewModelCensus) tickAuditCapture.enableViewModelCensus(true);
      if (fxCensus) tickAuditCapture.enableFxCensus(true);
      if (motionCensus) tickAuditCapture.enableMotionCensus(true);
      if (cosmeticsTick >= 0) summary.wearableSlotCapture = &wearableSlotCapture;
      const bool scanOk = tf2::native::scanKnownDemoMessages(path, index, summary);
      if (tickAudit) {
        const tf2::native::DemoTickAuditReport tickAuditReport =
            tf2::native::auditDemoTicks(header, index, &tickAuditCapture);
        for (const std::string& line : tf2::native::formatDemoTickAuditLines(tickAuditReport)) {
          tickAuditText += line;
          tickAuditText += '\n';
        }
      }
      if (motionCensus) {
        // A1: every row starts "motion-", pure ASCII, through the narrow stream like
        // the audit rows. Nothing here decides; the thresholds come from reading it.
        const auto& mcCensus = tickAuditCapture.motionCensus();
        std::string& mcOut = tickAuditText;
        const auto mcNum = [](double mcValue) {
          char mcBuf[64];
          std::snprintf(mcBuf, sizeof(mcBuf), "%.2f", mcValue);
          return std::string(mcBuf);
        };
        const auto mcHist = [](const std::vector<double>& mcValues, const std::vector<std::int32_t>& mcBounds) {
          std::vector<std::size_t> mcCounts(mcBounds.size() + 1, 0);
          for (const double mcV : mcValues) {
            std::size_t mcAt = 0;
            while (mcAt < mcBounds.size() && mcV > static_cast<double>(mcBounds[mcAt])) ++mcAt;
            ++mcCounts[mcAt];
          }
          std::string mcText;
          for (std::size_t mcAt = 0; mcAt < mcCounts.size(); ++mcAt) {
            if (!mcText.empty()) mcText += ' ';
            mcText += (mcAt < mcBounds.size() ? "le" + std::to_string(mcBounds[mcAt])
                                              : "gt" + std::to_string(mcBounds.back()))
              + "=" + std::to_string(mcCounts[mcAt]);
          }
          return mcText;
        };
        const auto mcPct = [](std::vector<double> mcValues, double mcQ) {
          if (mcValues.empty()) return 0.0;
          const auto mcK = static_cast<std::size_t>(mcQ * static_cast<double>(mcValues.size() - 1));
          std::nth_element(mcValues.begin(), mcValues.begin() + static_cast<std::ptrdiff_t>(mcK), mcValues.end());
          return mcValues[mcK];
        };
        // 1. stall: server ticks between consecutive data packets.
        std::vector<double> mcGaps;
        std::vector<std::pair<std::int32_t, std::int32_t>> mcGapTop;  // (gap, serverTick)
        std::size_t mcSameTick = 0;
        std::size_t mcBackwards = 0;
        std::size_t mcData = 0;
        std::int32_t mcPrev = -1;
        for (const auto& mcRow : tickAuditCapture.packetRows()) {
          if (mcRow.command != 2) continue;
          ++mcData;
          if (mcPrev >= 0) {
            const std::int32_t mcGap = mcRow.serverTick - mcPrev;
            if (mcGap == 0) {
              ++mcSameTick;
            } else if (mcGap < 0) {
              ++mcBackwards;
            } else {
              mcGaps.push_back(static_cast<double>(mcGap));
              mcGapTop.emplace_back(mcGap, mcRow.serverTick);
            }
          }
          mcPrev = mcRow.serverTick;
        }
        std::size_t mcGt3 = 0, mcGt6 = 0, mcGt16 = 0;
        for (const double mcG : mcGaps) {
          if (mcG > 3.0) ++mcGt3;
          if (mcG > 6.0) ++mcGt6;
          if (mcG > 16.0) ++mcGt16;
        }
        mcOut += "motion-census guardTicks=" + std::to_string(mcCensus.guardTicks)
          + " rateMaxDt=" + std::to_string(mcCensus.rateMaxDt)
          + " dataPackets=" + std::to_string(mcData) + '\n';
        mcOut += "motion-stall gaps=" + std::to_string(mcGaps.size())
          + " sameTick=" + std::to_string(mcSameTick)
          + " backwards=" + std::to_string(mcBackwards)
          + " median=" + mcNum(mcPct(mcGaps, 0.5))
          + " p99=" + mcNum(mcPct(mcGaps, 0.99))
          + " max=" + mcNum(mcPct(mcGaps, 1.0))
          + " gt3=" + std::to_string(mcGt3) + " gt6=" + std::to_string(mcGt6) + " gt16=" + std::to_string(mcGt16)
          + " " + mcHist(mcGaps, {1, 2, 3, 4, 8, 16, 32, 66}) + '\n';
        std::sort(mcGapTop.begin(), mcGapTop.end(), [](const auto& mcA, const auto& mcB) {
          return mcA.first != mcB.first ? mcA.first > mcB.first : mcA.second < mcB.second;
        });
        for (std::size_t mcAt = 0; mcAt < mcGapTop.size() && mcAt < 16u; ++mcAt) {
          mcOut += "motion-stall-top from=" + std::to_string(mcGapTop[mcAt].second - mcGapTop[mcAt].first)
            + " to=" + std::to_string(mcGapTop[mcAt].second)
            + " gap=" + std::to_string(mcGapTop[mcAt].first) + '\n';
        }
        // 2/3. move and turn.
        const auto mcSection = [&](const char* mcName, std::size_t mcSamples, std::size_t mcExcluded,
            std::size_t mcLong, const std::map<std::int32_t, std::size_t>& mcDt,
            const std::vector<tf2::native::DemoMotionRate>& mcRates,
            const std::vector<tf2::native::DemoMotionSample>& mcTop,
            const std::vector<std::int32_t>& mcBounds, const std::vector<double>& mcLimits) {
          std::vector<double> mcValues;
          mcValues.reserve(mcRates.size());
          for (const auto& mcR : mcRates) mcValues.push_back(static_cast<double>(mcR.perTick));
          std::string mcDtText;
          for (const auto& mcBin : mcDt) {
            mcDtText += mcBin.first == std::numeric_limits<std::int32_t>::max()
              ? " dt-gt32=" : " dt-le" + std::to_string(mcBin.first) + "=";
            mcDtText += std::to_string(mcBin.second);
          }
          mcOut += std::string("motion-") + mcName + " steps=" + std::to_string(mcSamples)
            + " counted=" + std::to_string(mcRates.size())
            + " excluded=" + std::to_string(mcExcluded)
            + " longDt=" + std::to_string(mcLong) + mcDtText + '\n';
          mcOut += std::string("motion-") + mcName + "-rate p50=" + mcNum(mcPct(mcValues, 0.5))
            + " p90=" + mcNum(mcPct(mcValues, 0.9))
            + " p99=" + mcNum(mcPct(mcValues, 0.99))
            + " p999=" + mcNum(mcPct(mcValues, 0.999))
            + " max=" + mcNum(mcPct(mcValues, 1.0))
            + " " + mcHist(mcValues, mcBounds) + '\n';
          std::string mcOver;
          for (const double mcLimit : mcLimits) {
            std::size_t mcCount = 0;
            std::set<std::int32_t> mcWho;
            for (const auto& mcR : mcRates) {
              if (static_cast<double>(mcR.perTick) > mcLimit) {
                ++mcCount;
                mcWho.insert(mcR.entity);
              }
            }
            mcOver += " gt" + mcNum(mcLimit) + "=" + std::to_string(mcCount) + "/" + std::to_string(mcWho.size());
          }
          mcOut += std::string("motion-") + mcName + "-candidates steps/players" + mcOver + '\n';
          std::vector<tf2::native::DemoMotionSample> mcSorted = mcTop;
          std::sort(mcSorted.begin(), mcSorted.end(), [](const auto& mcA, const auto& mcB) {
            return mcA.perTick > mcB.perTick;
          });
          for (std::size_t mcAt = 0; mcAt < mcSorted.size() && mcAt < 16u; ++mcAt) {
            const auto& mcS = mcSorted[mcAt];
            mcOut += std::string("motion-") + mcName + "-top tick=" + std::to_string(mcS.tick)
              + " entity=" + std::to_string(mcS.entity)
              + " dt=" + std::to_string(mcS.dt)
              + " amount=" + mcNum(mcS.amount)
              + " perTick=" + mcNum(mcS.perTick)
              + " sinceLife=" + std::to_string(mcS.sinceLife)
              + " sinceTeleport=" + std::to_string(mcS.sinceTeleport)
              + " sinceTeam=" + std::to_string(mcS.sinceTeam)
              + " excluded=" + (mcS.excluded ? "1" : "0") + '\n';
          }
        };
        mcSection("move", mcCensus.moveSamples, mcCensus.moveExcluded, mcCensus.moveLongDt, mcCensus.moveDt,
          mcCensus.moveRates, mcCensus.topMoves, {1, 2, 5, 10, 20, 40, 80, 160, 320}, {20.0, 40.0, 80.0, 160.0});
        mcSection("turn", mcCensus.turnSamples, mcCensus.turnExcluded, mcCensus.turnLongDt, mcCensus.turnDt,
          mcCensus.turnRates, mcCensus.topTurns, {1, 5, 15, 30, 60, 90, 135}, {30.0, 60.0, 90.0, 135.0});
      }
      if (cosmeticsTick >= 0) {
        // The schema is an OPTIONAL input: it is a game asset, not part of this
        // repository. Absent it, the table still runs and every instance that only
        // the schema could answer is counted as pathless -- never guessed.
        tf2::native::WearableTableRequest request;
        request.tick = cosmeticsTick;
        std::unique_ptr<tf2::native::ItemSchema> itemSchema;
        tf2::native::AssetRoot assets;
        if (!tfRoot.empty()) {
          assets = tf2::native::AssetRoot::fromPath(tfRoot);
        } else {
          // Same helper the product uses to walk a Steam root, minus the registry
          // enumeration: a probe given no path should not read the registry.
          assets = tf2::native::AssetRoot::findInstalled(
              tf2::native::AssetRoot::steamLibraryCandidates(
                  {std::filesystem::path(L"C:\\Program Files (x86)\\Steam"),
                   std::filesystem::path(L"C:\\Program Files\\Steam"),
                   std::filesystem::path(L"D:\\SteamLibrary")}));
        }
        request.assetRootValid = assets.valid();
        if (assets.valid()) {
          request.assetRootDescribe = assets.describe();
          std::string schemaError;
          itemSchema = tf2::native::ItemSchema::load(
              assets.tfDirectory / L"scripts/items/items_game.txt", schemaError);
          if (static_cast<bool>(itemSchema)) {
            request.schema = itemSchema.get();
          } else {
            request.schemaError = schemaError.empty() ? std::string("items_game.txt absent")
                                                       : schemaError;
          }
        } else {
          request.schemaError = tfRoot.empty() ? std::string("no asset root found; pass --tf-root")
                                               : std::string("no asset root at --tf-root");
        }
        const tf2::native::WearableInstanceTable table =
            tf2::native::buildWearableInstanceTable(summary, request);
        const std::wstring tableLines = tf2::native::formatWearableTableLines(table);
        // formatWearableTableLines widens each byte one-for-one (it is a dump body, not
        // text that goes through a locale), so narrowing back here is exact. The
        // implicit iterator-range assign would be a C4244 narrowing warning at /W4.
        cosmeticsTableText.clear();
        cosmeticsTableText.reserve(tableLines.size());
        for (const wchar_t ch : tableLines) {
          cosmeticsTableText.push_back(static_cast<char>(ch));
        }
      }

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
                // P3b/N21: the decoded list can only hold events from messages whose raw payload
                // was captured (64 KiB / 4096-message cap, native/src/demo_header_sound.cpp).
                // counted / dropped / decoded make that cap visible, and the dropped total is
                // split by reason (task B): a designed drop is never decoded, so sound_failures
                // now counts only messages whose payload was captured.
                << L" sound_messages=" << summary.soundMessageCount
                << L" sound_dropped=" << summary.rawSoundMessagesDropped
                << L" sound_dropped_payload_too_large=" << summary.rawSoundMessagesDroppedPayloadTooLarge
                << L" sound_dropped_message_cap=" << summary.rawSoundMessagesDroppedMessageCap
                << L" sound_decoded=" << summary.decodedSoundEvents.size()
                << L" sound_decoded_cap_drops=" << summary.decodedSoundEventCapDrops
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
      if (cameraTrack) {
        std::size_t cameraFirstInversion = 0;
        const bool cameraOrdered =
            tf2::native::cameraTickOrderingHolds(summary, &cameraFirstInversion);
        const long long cameraFirstRowTick = summary.cameraTickSamples.empty()
            ? -1 : static_cast<long long>(summary.cameraTickSamples.front().tick);
        const long long cameraLastRowTick = summary.cameraTickSamples.empty()
            ? -1 : static_cast<long long>(summary.cameraTickSamples.back().tick);
        // N12-C: the track is expected to start where the first PacketEntities
        // carries a view target. Count what sits below that line and echo the
        // first ticks, so a head gap is measured rather than argued about.
        std::size_t cameraRowsBeforeEntities = 0;
        for (const tf2::native::CameraTickSample& cameraRow : summary.cameraTickSamples) {
          if (static_cast<long long>(cameraRow.tick) < static_cast<long long>(summary.firstPacketEntitiesTick)) {
            ++cameraRowsBeforeEntities;
          }
        }
        std::wstring cameraFirst5;
        for (std::size_t cameraSampleIndex = 0;
             cameraSampleIndex < summary.cameraTickSamples.size() && cameraSampleIndex < 5u;
             ++cameraSampleIndex) {
          if (cameraSampleIndex != 0) cameraFirst5 += L",";
          cameraFirst5 += std::to_wstring(summary.cameraTickSamples[cameraSampleIndex].tick);
        }
        std::wcout << L" camera_rows=" << summary.cameraRows
                   << L" camera_rows_dropped=" << summary.cameraRowsDropped
                   << L" camera_row_tick_mismatch=" << summary.cameraRowTickMismatch
                   << L" camera_identity_mismatch=" << summary.cameraIdentityMismatch
                   << L" camera_identity_rebuilt=" << summary.cameraIdentityRebuilt
                   << L" camera_rows_no_tick=" << summary.cameraRowsNoTick
                   << L" camera_rows_incomplete_packet=" << summary.cameraRowsIncompletePacket
                   << L" camera_rows_no_target=" << summary.cameraRowsNoTarget
                   << L" camera_first_no_target_tick=" << summary.cameraFirstNoTargetTick
                   << L" camera_duplicate_tick_rows=" << summary.cameraDuplicateTickRows
                   << L" camera_packets_with_set_view=" << summary.cameraPacketsWithSetView
                   << L" camera_set_view_before_entities=" << summary.cameraPacketsSetViewBeforeEntities
                   << L" camera_set_view_after_entities=" << summary.cameraPacketsSetViewAfterEntities
                   << L" camera_tick_ordering=" << (cameraOrdered ? 1 : 0)
                   << L" camera_first_inversion=" << cameraFirstInversion
                   << L" camera_tick_samples=" << summary.cameraTickSamples.size()
                   << L" camera_first_row_tick=" << cameraFirstRowTick
                   << L" camera_last_row_tick=" << cameraLastRowTick
                   << L" camera_rows_before_first_entities=" << cameraRowsBeforeEntities
                   << L" camera_first5_ticks=" << cameraFirst5
                   << L" camera_bytes=" << (summary.cameraTickSamples.size() * sizeof(tf2::native::CameraTickSample));
      }
      // N21(c)/task-29 evidence: one row per retained temp-entity event.
      // Columns: tick, class, player, weaponId, mode, seed, x, y, z. A missing
      // field prints '-' instead of a zero, so "the wire did not carry it" and
      // "the wire carried 0" stay distinguishable.
      if (tempLimit >= 0) {
        for (const auto& event : summary.tempEntityEvents) {
          if (tempLimit > 0 && tempRows >= static_cast<std::size_t>(tempLimit)) break;
          tempDumpText += std::to_string(event.tick);
          tempDumpText += '\t';
          tempDumpText += event.className;
          tempDumpText += '\t';
          tempDumpText += event.hasPlayer ? std::to_string(event.player) : "-";
          tempDumpText += '\t';
          tempDumpText += event.hasWeaponId ? std::to_string(event.weaponId) : "-";
          tempDumpText += '\t';
          tempDumpText += event.hasMode ? std::to_string(event.mode) : "-";
          tempDumpText += '\t';
          tempDumpText += event.hasSeed ? std::to_string(event.seed) : "-";
          if (event.hasOrigin) {
            char position[96] = {};
            std::snprintf(position, sizeof(position), "\t%.2f\t%.2f\t%.2f",
              static_cast<double>(event.origin[0]), static_cast<double>(event.origin[1]),
              static_cast<double>(event.origin[2]));
            tempDumpText += position;
          } else {
            tempDumpText += "\t-\t-\t-";
          }
          tempDumpText += '\n';
          ++tempRows;
        }
      }
      if (viewModelCensus) {
        const auto& census = tickAuditCapture.viewModelCensus();
        tempDumpText += "vm-census entities=" + std::to_string(census.size()) + '\n';
        for (const auto& item : census) {
          const auto& e = item.second;
          tempDumpText += "vm-entity index=" + std::to_string(e.entityIndex)
            + " class=" + e.className
            + " ticks=" + std::to_string(e.firstTick) + ".." + std::to_string(e.lastTick)
            + " sequenceWrites=" + std::to_string(e.sequenceWrites)
            + " sequenceChanges=" + std::to_string(e.sequenceChanges)
            + " distinctSequences=" + std::to_string(e.sequenceWritesByValue.size())
            + " parityWrites=" + std::to_string(e.parityWrites)
            + " parityChanges=" + std::to_string(e.parityChanges)
            + " modelChanges=" + std::to_string(e.modelChanges)
            + " cycleWrites=" + std::to_string(e.cycleWrites)
            + " playbackRateWrites=" + std::to_string(e.playbackRateWrites) + '\n';
          for (const auto& seq : e.sequenceWritesByValue) {
            const auto first = e.sequenceFirstTick.find(seq.first);
            tempDumpText += "vm-sequence index=" + std::to_string(e.entityIndex)
              + " sequence=" + std::to_string(seq.first)
              + " writes=" + std::to_string(seq.second)
              + " firstTick=" + std::to_string(first == e.sequenceFirstTick.end() ? -1 : first->second)
              + '\n';
          }
          for (const auto& step : e.transitions) {
            tempDumpText += "vm-transition index=" + std::to_string(e.entityIndex)
              + " tick=" + std::to_string(step.tick)
              + " from=" + std::to_string(step.from)
              + " to=" + std::to_string(step.to) + '\n';
          }
        }
      }
      if (fxCensus) {
        const auto& census = tickAuditCapture.fxCensus();
        tempDumpText += "fx-census props=" + std::to_string(census.props.size())
          + " chargeEdges=" + std::to_string(census.chargeEdges.size())
          + " condEdges=" + std::to_string(census.condEdges.size())
          + " condEdgesDropped=" + std::to_string(census.condEdgesDropped) + '\n';
        for (const auto& item : census.props) {
          std::string values;
          for (const auto& v : item.second.values) {
            if (!values.empty()) values += ',';
            values += std::to_string(v.first) + ':' + std::to_string(v.second);
          }
          tempDumpText += "fx-prop key=" + item.first
            + " writes=" + std::to_string(item.second.writes)
            + " changes=" + std::to_string(item.second.changes)
            + " values=" + (values.empty() ? std::string("-") : values)
            + " overflow=" + std::to_string(item.second.valuesOverflow) + '\n';
        }
        std::size_t printed = 0;
        for (const auto& edge : census.chargeEdges) {
          if (printed++ >= 64u) break;
          tempDumpText += "fx-charge tick=" + std::to_string(edge.tick)
            + " medigun=" + std::to_string(edge.medigun)
            + " owner=" + std::to_string(edge.owner)
            + " target=" + std::to_string(edge.target)
            + " item=" + std::to_string(edge.item)
            + " kind=" + tf2::native::medigunChargeKindName(tf2::native::medigunChargeKindForItem(edge.item))
            + " on=" + (edge.on ? "1" : "0") + '\n';
        }
        // Which bits rose on the medic / the patient within [-4, +33] ticks of a
        // charge starting, against how often each bit rises at all. A bit that
        // means "invulnerable" rises next to nearly every stock charge and rarely
        // anywhere else.
        struct Hit { std::size_t owner = 0; std::size_t target = 0; };
        std::map<std::string, std::map<std::pair<int, bool>, Hit>> byKind;
        std::map<std::string, std::size_t> chargesByKind;
        std::map<std::pair<int, bool>, std::size_t> totalRises;
        for (const auto& edge : census.condEdges) {
          for (int b = 0; b < 32; ++b) {
            if ((edge.rose >> b) & 1u) ++totalRises[{edge.word * 32 + b, edge.conditionList}];
          }
        }
        for (const auto& charge : census.chargeEdges) {
          if (!charge.on) continue;
          const std::string kind = tf2::native::medigunChargeKindName(tf2::native::medigunChargeKindForItem(charge.item));
          ++chargesByKind[kind];
          std::map<std::pair<int, bool>, int> seen;  // bit -> 1 owner, 2 target, 3 both
          for (const auto& edge : census.condEdges) {
            if (edge.tick < charge.tick - 4 || edge.tick > charge.tick + 33) continue;
            const bool isOwner = edge.entity == charge.owner;
            const bool isTarget = charge.target >= 0 && edge.entity == charge.target;
            if (!isOwner && !isTarget) continue;
            for (int b = 0; b < 32; ++b) {
              if (((edge.rose >> b) & 1u) == 0u) continue;
              int& mask = seen[{edge.word * 32 + b, edge.conditionList}];
              mask |= (isOwner ? 1 : 0) | (isTarget ? 2 : 0);
            }
          }
          for (const auto& bit : seen) {
            Hit& hit = byKind[kind][bit.first];
            if (bit.second & 1) ++hit.owner;
            if (bit.second & 2) ++hit.target;
          }
        }
        for (const auto& kind : byKind) {
          for (const auto& bit : kind.second) {
            const auto total = totalRises.find(bit.first);
            tempDumpText += "fx-correlation kind=" + kind.first
              + " charges=" + std::to_string(chargesByKind[kind.first])
              + " bit=" + std::to_string(bit.first.first)
              + " list=" + (bit.first.second ? "condition_bits" : "m_nPlayerCond")
              + " ownerHits=" + std::to_string(bit.second.owner)
              + " targetHits=" + std::to_string(bit.second.target)
              + " risesAnywhere=" + std::to_string(total == totalRises.end() ? 0u : total->second) + '\n';
          }
        }
      }
      if (tempCensus) {
        std::size_t decoded = 0;
        std::vector<std::pair<std::string, std::size_t>> classes(
          summary.tempClassCensus.begin(), summary.tempClassCensus.end());
        for (const auto& entry : classes) decoded += entry.second;
        // Most frequent first; equal counts by name, so the order is a reading
        // and not hash-table iteration order.
        std::sort(classes.begin(), classes.end(),
          [](const std::pair<std::string, std::size_t>& a, const std::pair<std::string, std::size_t>& b) {
            if (a.second != b.second) return a.second > b.second;
            return a.first < b.first;
          });
        tempDumpText += "temp-retention decoded=" + std::to_string(decoded)
          + " kept=" + std::to_string(summary.tempEntityEvents.size())
          + " dropped_cap=" + std::to_string(summary.tempEntityEventsDroppedCap)
          + " cap=" + std::to_string(tf2::native::DemoNetworkSummary::kTempEntityRetentionCap)
          + " classes=" + std::to_string(classes.size())
          + " balanced=" + ((decoded == summary.tempEntityEvents.size() + summary.tempEntityEventsDroppedCap) ? "1" : "0")
          + '\n';
        for (const auto& entry : classes) {
          tempDumpText += "temp-class " + std::to_string(entry.second) + ' ' + entry.first + '\n';
        }
      }
      // N12-C step 2: the track's own row at each requested exact tick. Printed
      // only when --camera-row was given, so every other scan line stays
      // byte-identical to the step-1 output.
      for (const std::int32_t requestedCameraRow : cameraRowTicks) {
        const tf2::native::CameraTickQueryResult rowResult =
            tf2::native::queryCameraTickSample(summary, requestedCameraRow);
        std::wcout << L" camera_row_req=" << requestedCameraRow
                   << L" camera_row_hit=" << (rowResult.hit ? 1 : 0)
                   << L" camera_row_rows_at_tick=" << rowResult.rowsAtTick
                   << L" camera_row_prev=" << rowResult.previousTick
                   << L" camera_row_next=" << rowResult.nextTick;
        if (rowResult.sample != nullptr) {
          const tf2::native::CameraTickSample& row = *rowResult.sample;
          char numbers[512];
          std::snprintf(numbers, sizeof numbers,
            " camera_row_tick=%d camera_row_entity=%d camera_row_class=%d"
            " camera_row_generation=%u camera_row_flags=%u"
            " camera_row_origin=%.4f,%.4f,%.4f camera_row_view_offset_z=%.6f"
            " camera_row_eye_z=%.6f camera_row_angles=%.4f,%.4f,%.4f"
            " camera_row_life=%d camera_row_obs=%d",
            static_cast<int>(row.tick), static_cast<int>(row.entityIndex),
            static_cast<int>(row.classId), static_cast<unsigned>(row.generation),
            static_cast<unsigned>(row.flags), row.origin[0], row.origin[1], row.origin[2],
            static_cast<double>(row.viewOffsetZ),
            static_cast<double>(row.origin[2] + row.viewOffsetZ),
            row.angles[0], row.angles[1], row.angles[2],
            static_cast<int>(row.lifeState), static_cast<int>(row.observerMode));
          std::wcout << numbers;
        }
      }
      {
        std::wstring first5, last5;
        const std::size_t sampleCount = summary.viewSamples.size();
        const std::size_t tailStart = sampleCount > 5u ? sampleCount - 5u : 0u;
        for (std::size_t viewHeadIndex = 0; viewHeadIndex < sampleCount && viewHeadIndex < 5u; ++viewHeadIndex) {
          if (viewHeadIndex != 0) first5 += L",";
          first5 += std::to_wstring(summary.viewSamples[viewHeadIndex].tick);
        }
        for (std::size_t viewTailIndex = tailStart; viewTailIndex < sampleCount; ++viewTailIndex) {
          if (viewTailIndex != tailStart) last5 += L",";
          last5 += std::to_wstring(summary.viewSamples[viewTailIndex].tick);
        }
        std::wcout << L" view_first5=" << first5 << L" view_last5=" << last5;
      }
      {
        std::wstring firstTicks, lastTicks;
        const std::size_t tickCount = summary.packetServerTicks.size();
        const std::size_t tickTail = tickCount > 5u ? tickCount - 5u : 0u;
        for (std::size_t tickHeadIndex = 0; tickHeadIndex < tickCount && tickHeadIndex < 5u; ++tickHeadIndex) {
          if (tickHeadIndex != 0) firstTicks += L",";
          firstTicks += std::to_wstring(summary.packetServerTicks[tickHeadIndex]);
        }
        for (std::size_t tickTailIndex = tickTail; tickTailIndex < tickCount; ++tickTailIndex) {
          if (tickTailIndex != tickTail) lastTicks += L",";
          lastTicks += std::to_wstring(summary.packetServerTicks[tickTailIndex]);
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
        for (std::size_t checkpointIndex = 1; checkpointIndex < summary.entityHistoryCheckpoints.size(); ++checkpointIndex) {
          const std::int32_t gap = summary.entityHistoryCheckpoints[checkpointIndex].tick
            - summary.entityHistoryCheckpoints[checkpointIndex - 1].tick;
          if (gap > historyGapMax) historyGapMax = gap;
        }
        // The snapshot query resolves against the THINNED ARCHIVE once the live
        // window has flushed, so the archive's own spacing is the bound a
        // staleness reading has to be compared with; the live vector holds only
        // the two checkpoints the window still carries.
        std::int32_t archiveGapMax = 0;
        for (std::size_t archiveIndex = 1; archiveIndex < summary.entityHistoryArchive.size(); ++archiveIndex) {
          const std::int32_t gap = summary.entityHistoryArchive[archiveIndex].tick
            - summary.entityHistoryArchive[archiveIndex - 1].tick;
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
        // N12-C/Q5 cross-end spelling of "no snapshot": the query leaves
        // resolvedTick untouched on Gap / NoHistory / TickBeforeHistory, so the
        // old seed (snapshotTick) printed stale=0 for a tick that resolved to
        // NOTHING. The product publishes g_sceneResolvedServerTick = -1 on exactly
        // these statuses, and demo-clock-check.sh A3 compares the two numbers, so
        // both ends spell it -1: -1 means UNRESOLVED, it does not mean zero
        // staleness. snapshot_resolved_tick is left as it was (raw), because other
        // readers of this line bind to it.
        const bool snapshotResolved = status == tf2::native::EntitySnapshotQueryStatus::Available
            || status == tf2::native::EntitySnapshotQueryStatus::Checkpoint;
        const long long snapshotStale = snapshotResolved
            ? static_cast<long long>(snapshotTick) - resolvedTick : -1LL;
        std::wcout << L" snapshot_tick=" << snapshotTick
                   << L" snapshot_status=" << statusName(status)
                   << L" snapshot_states=" << states.size()
                   << L" snapshot_resolved_tick=" << resolvedTick
                   << L" snapshot_stale=" << snapshotStale;
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
      if (propsEntity >= 0) {
        const auto& table = summary.entityStates;
        if (static_cast<std::size_t>(propsEntity) < table.size()) {
          const tf2::native::EntityState& state = table[static_cast<std::size_t>(propsEntity)];
          std::set<std::string> names;
          for (const auto& entry : state.properties) names.insert(entry.first);
          propsDumpText += "props\tentity\t" + std::to_string(propsEntity)
            + "\tclass\t" + std::to_string(state.classId)
            + "\tcount\t" + std::to_string(names.size()) + "\n";
          for (const std::string& name : names) {
            const auto found = state.properties.find(name);
            if (found == state.properties.end()) continue;
            char buffer[256];
            std::snprintf(buffer, sizeof(buffer),
              "prop\t%s\ttype\t%d\tx\t%.6f\ty\t%.6f\tz\t%.6f\tint\t%lld\tlastWriteTick\t%d",
              name.c_str(), static_cast<int>(found->second.type),
              static_cast<double>(found->second.x), static_cast<double>(found->second.y),
              static_cast<double>(found->second.z),
              static_cast<long long>(found->second.intValue), found->second.lastWriteTick);
            propsDumpText += buffer;
            propsDumpText += "\n";
          }
        } else {
          propsDumpText += "props\tentity\t" + std::to_string(propsEntity)
            + "\tclass\t-1\tcount\t0\tout_of_range\t1\n";
        }
      }
      if (!hudCensusTicks.empty()) {
        // H0: which classes carry the broadcast HUD's inputs. Matched by schema
        // name (the class id differs between builds), and by substring for the two
        // families whose exact name is the question (control points, payload).
        const auto hudClassWanted = [](const std::string& name) {
          static const char* const kExact[] = {"CTFPlayerResource", "CTFObjectiveResource",
            "CTeamTrainWatcher", "CTFTeam", "CTeamRoundTimer", "CTFGameRulesProxy",
            "CTeamplayRoundBasedRulesProxy", "CTeamControlPointMaster", "CTeamControlPoint",
            "CFuncTrackTrain", "CTFTeamSpawn"};
          for (const char* exact : kExact) if (name == exact) return true;
          return name.find("ControlPoint") != std::string::npos || name.find("Train") != std::string::npos
            || name.find("Objective") != std::string::npos || name.find("Payload") != std::string::npos;
        };
        const auto hudClassName = [&summary](std::int32_t classId) -> std::string {
          if (classId < 0) return std::string();
          const auto index = static_cast<std::size_t>(classId);
          if (index < summary.serverClassNames.size() && !summary.serverClassNames[index].empty()) {
            return summary.serverClassNames[index];
          }
          for (const auto& schema : summary.serverClassSchemas) {
            if (schema.id == index) return schema.name;
          }
          return std::string();
        };
        for (const std::int32_t censusTick : hudCensusTicks) {
          std::vector<tf2::native::EntityState> hudStates;
          std::int32_t hudResolved = censusTick;
          const auto hudStatus = tf2::native::queryEntitySnapshotAtOrBeforeTick(
            summary, censusTick, hudStates, &hudResolved);
          const bool hudOk = hudStatus == tf2::native::EntitySnapshotQueryStatus::Available
            || hudStatus == tf2::native::EntitySnapshotQueryStatus::Checkpoint;
          std::size_t hudEntities = 0;
          std::string hudBody;
          if (hudOk) {
            for (std::size_t index = 0; index < hudStates.size(); ++index) {
              const tf2::native::EntityState& state = hudStates[index];
              if (state.classId < 0 || state.properties.empty()) continue;
              const std::string className = hudClassName(state.classId);
              if (className.empty() || !hudClassWanted(className)) continue;
              ++hudEntities;
              std::set<std::string> names;
              for (const auto& entry : state.properties) names.insert(entry.first);
              hudBody += "hud-entity\ttick\t" + std::to_string(censusTick) + "\tindex\t" + std::to_string(index)
                + "\tclass\t" + className + "\tprops\t" + std::to_string(names.size()) + "\n";
              std::size_t printed = 0;
              for (const std::string& name : names) {
                if (++printed > 600u) {
                  hudBody += "hud-prop\ttick\t" + std::to_string(censusTick) + "\tindex\t"
                    + std::to_string(index) + "\ttruncated\t" + std::to_string(names.size() - 600u) + "\n";
                  break;
                }
                const auto found = state.properties.find(name);
                if (found == state.properties.end()) continue;
                std::string text = found->second.stringValue;
                for (char& character : text) {
                  if (character == '\t' || character == '\n' || character == '\r') character = ' ';
                }
                if (text.size() > 64u) text.resize(64u);
                char buffer[512];
                std::snprintf(buffer, sizeof(buffer),
                  "hud-prop\ttick\t%d\tindex\t%zu\t%s\ttype\t%d\tint\t%lld\tx\t%.4f\ty\t%.4f\tz\t%.4f\tstr\t%s\tlastWriteTick\t%d\n",
                  static_cast<int>(censusTick), index, name.c_str(), static_cast<int>(found->second.type),
                  static_cast<long long>(found->second.intValue), static_cast<double>(found->second.x),
                  static_cast<double>(found->second.y), static_cast<double>(found->second.z),
                  text.c_str(), found->second.lastWriteTick);
                hudBody += buffer;
              }
            }
          }
          propsDumpText += "hud-census\ttick\t" + std::to_string(censusTick)
            + "\tstatus\t" + (hudOk ? "resolved" : "unresolved")
            + "\tresolvedTick\t" + std::to_string(hudOk ? hudResolved : -1)
            + "\tentities\t" + std::to_string(hudEntities) + "\n";
          propsDumpText += hudBody;
        }
      }
      if (wearableTick >= 0) {
        // task-39 gate. The three questions the gate asks are all about the SAME
        // tick, so they are answered from a single snapshot: reading once per
        // question would let the answers disagree with each other, and a gate
        // that contradicts itself proves nothing.
        std::vector<tf2::native::EntityState> wearableStates;
        std::int32_t wearableResolvedTick = wearableTick;
        const auto wearableStatus = tf2::native::queryEntitySnapshotAtOrBeforeTick(
          summary, wearableTick, wearableStates, &wearableResolvedTick);
        const bool wearableResolved =
            wearableStatus == tf2::native::EntitySnapshotQueryStatus::Available
            || wearableStatus == tf2::native::EntitySnapshotQueryStatus::Checkpoint;
        const long long wearableStale = wearableResolved
            ? static_cast<long long>(wearableTick) - wearableResolvedTick : -1LL;

        // Histogram over every classId the tick actually carries. Class name comes
        // from serverClassSchemas matched on .id; classId is a data-dependent-width
        // index and serverClassNames only lines up positionally by luck.
        std::map<std::string, std::size_t> histogram;
        std::size_t liveEntities = 0;
        for (const tf2::native::EntityState& state : wearableStates) {
          if (state.classId < 0) continue;
          ++liveEntities;
          const std::string* name = serverClassName(summary, state.classId);
          histogram[name != nullptr ? *name : std::string("<unknown>")] += 1;
        }

        std::size_t wearableEntities = 0;
        std::size_t wearableModelIndex = 0;
        std::size_t wearableModelPath = 0;
        std::size_t wearableItemDef = 0;
        std::size_t wearableOwner = 0;
        std::size_t wearableOwnerLive = 0;
        std::size_t wearableOwnerPlayer = 0;
        std::size_t wearableParent = 0;
        std::size_t wearableAttachment = 0;
        std::size_t wearableOrigin = 0;
        std::size_t wearableAngles = 0;
        // A readable field prints its value; an absent one prints '-'. The same
        // rule the temps dump uses, and for the same reason: a wearable with no
        // owner handle and a wearable whose owner handle is 0 must not look alike
        // to the gate.
        const auto fieldOf = [](std::int64_t value, bool readable) {
          return readable ? std::to_string(value) : std::string("-");
        };
        for (std::size_t entityIndex = 0; entityIndex < wearableStates.size(); ++entityIndex) {
          const tf2::native::EntityState& state = wearableStates[entityIndex];
          if (state.classId < 0) continue;
          const std::string* className = serverClassName(summary, state.classId);
          if (className == nullptr || !containsNoCase(*className, "Wearable")) continue;
          ++wearableEntities;
          // The slot's reuse counter, read the way demo_header_camera.cpp and
          // demo_header_track.cpp read it. It is the only identity available: the
          // wire carries a 10-bit serial on Enter but EntityState does not keep it.
          const std::uint32_t generation =
              entityIndex < summary.projectileTrackSlotGeneration.size()
              ? summary.projectileTrackSlotGeneration[entityIndex] : 0u;

          std::int64_t modelIndex = 0;
          const bool hasModelIndex = rankedInt(state, "m_nModelIndex", modelIndex);
          const std::string* modelPath = hasModelIndex ? precachePath(summary, modelIndex) : nullptr;
          if (hasModelIndex) ++wearableModelIndex;
          if (modelPath != nullptr) ++wearableModelPath;
          std::int64_t worldIndex = 0;
          const bool hasWorldIndex = rankedInt(state, "m_iWorldModelIndex", worldIndex);
          const std::string* worldPath = hasWorldIndex ? precachePath(summary, worldIndex) : nullptr;
          std::int64_t itemDef = 0;
          const bool hasItemDef = rankedInt(state, "m_iItemDefinitionIndex", itemDef);
          if (hasItemDef) ++wearableItemDef;

          std::int64_t ownerRaw = 0;
          const bool hasOwner = rankedInt(state, "m_hOwnerEntity", ownerRaw);
          std::uint32_t ownerIndex = 0;
          const bool ownerIndexed = hasOwner && wearableHandleIndex(ownerRaw, ownerIndex);
          const tf2::native::EntityState* ownerState = nullptr;
          if (ownerIndexed && ownerIndex < wearableStates.size()
              && wearableStates[ownerIndex].classId >= 0) {
            ownerState = &wearableStates[ownerIndex];
          }
          const std::string* ownerClass =
              ownerState != nullptr ? serverClassName(summary, ownerState->classId) : nullptr;
          const bool ownerIsPlayer = ownerClass != nullptr && containsNoCase(*ownerClass, "TFPlayer");
          if (hasOwner) ++wearableOwner;
          if (ownerState != nullptr) ++wearableOwnerLive;
          if (ownerIsPlayer) ++wearableOwnerPlayer;

          // The wire sends this slot as "moveparent", not "m_hMoveParent": the
          // entity_model parent walk reads BOTH names for exactly that reason
          // (native/src/entity_model.cpp:885). Reading only the m_h spelling would
          // have reported every wearable as parentless while the handle was sitting
          // in the state, which is the kind of false negative a gate cannot afford.
          std::int64_t moveParent = 0;
          bool hasMoveParent = rankedInt(state, "m_hMoveParent", moveParent);
          if (!hasMoveParent) hasMoveParent = rankedInt(state, "moveparent", moveParent);
          std::uint32_t parentIndex = 0;
          const bool parentIndexed = hasMoveParent && wearableHandleIndex(moveParent, parentIndex);
          if (parentIndexed) ++wearableParent;
          std::int64_t parentAttachment = 0;
          const bool hasAttachment = rankedInt(state, "m_iParentAttachment", parentAttachment);
          if (hasAttachment) ++wearableAttachment;

          // Vector only. A VectorXY reading has already lost z on the wire and
          // rankedVector reports that as incomplete rather than fabricating a zero,
          // so an origin the engine did not send is printed '-' like any other
          // absent field instead of as 0,0,0 -- which is a real place on the map.
          float origin[3] = {0.0f, 0.0f, 0.0f};
          bool originComplete = false;
          const bool hasOrigin = rankedVector(state, "m_vecOrigin", origin, &originComplete);
          if (hasOrigin && originComplete) ++wearableOrigin;
          float angles[3] = {0.0f, 0.0f, 0.0f};
          bool anglesComplete = false;
          const bool hasAngles = rankedVector(state, "m_angRotation", angles, &anglesComplete);
          if (hasAngles && anglesComplete) ++wearableAngles;
          char originText[64] = "-";
          if (hasOrigin && originComplete) {
            std::snprintf(originText, sizeof(originText), "%.4f,%.4f,%.4f",
              static_cast<double>(origin[0]), static_cast<double>(origin[1]),
              static_cast<double>(origin[2]));
          }
          char anglesText[64] = "-";
          if (hasAngles && anglesComplete) {
            std::snprintf(anglesText, sizeof(anglesText), "%.4f,%.4f,%.4f",
              static_cast<double>(angles[0]), static_cast<double>(angles[1]),
              static_cast<double>(angles[2]));
          }

          // Column order, so the doc and task-40 read the same thing this writes:
          // wearable tick entity generation class modelIndex modelPath worldModelIndex
          // worldModelPath itemDef origin angles ownerHandle ownerIndex ownerClass
          // ownerLive moveParent moveParentIndex parentAttachment
          std::string row;
          const auto addField = [&row](const std::string& value) {
            row += value;
            row += '\t';
          };
          addField("wearable");
          addField(std::to_string(wearableTick));
          addField(std::to_string(entityIndex));
          addField(std::to_string(generation));
          addField(*className);
          addField(fieldOf(modelIndex, hasModelIndex));
          addField(modelPath != nullptr ? *modelPath : std::string("-"));
          addField(fieldOf(worldIndex, hasWorldIndex));
          addField(worldPath != nullptr ? *worldPath : std::string("-"));
          addField(fieldOf(itemDef, hasItemDef));
          addField(originText);
          addField(anglesText);
          addField(fieldOf(ownerRaw, hasOwner));
          addField(ownerIndexed ? std::to_string(ownerIndex) : std::string("-"));
          addField(ownerClass != nullptr ? *ownerClass : std::string("-"));
          addField(ownerState != nullptr ? "1" : "0");
          addField(fieldOf(moveParent, hasMoveParent));
          addField(parentIndexed ? std::to_string(parentIndex) : std::string("-"));
          addField(fieldOf(parentAttachment, hasAttachment));
          if (!row.empty()) row.pop_back();
          wearableDumpText += row;
          wearableDumpText += '\n';
        }

        for (const auto& entry : histogram) {
          wearableDumpText += "wearable_class\t";
          wearableDumpText += std::to_string(wearableTick);
          wearableDumpText += '\t';
          wearableDumpText += entry.first;
          wearableDumpText += '\t';
          wearableDumpText += std::to_string(entry.second);
          wearableDumpText += '\n';
        }

        // An unresolvable tick is not evidence about the recording. Saying FAIL
        // here would read as "the demos carry no wearables" when it means "this
        // tick could not be answered", so the gate rows say NO-DATA and the doc has
        // to re-ask at a tick the history can serve.
        const bool wearablesPresent = wearableEntities > 0;
        const bool modelPathsRecoverable = wearableModelPath > 0;
        const bool ownersResolveToPlayers = wearableOwnerPlayer > 0;
        const auto gateText = [](bool pass) { return pass ? "PASS" : "FAIL"; };
        if (!wearableResolved) {
          wearableDumpText += "wearable_gate\tdata\tNO-DATA\t0\n";
        } else {
          wearableDumpText += "wearable_gate\tentities_present\t";
          wearableDumpText += gateText(wearablesPresent);
          wearableDumpText += '\t';
          wearableDumpText += std::to_string(wearableEntities);
          wearableDumpText += '\n';
          wearableDumpText += "wearable_gate\tmodel_path\t";
          wearableDumpText += gateText(modelPathsRecoverable);
          wearableDumpText += '\t';
          wearableDumpText += std::to_string(wearableModelPath);
          wearableDumpText += '/';
          wearableDumpText += std::to_string(wearableEntities);
          wearableDumpText += '\n';
          wearableDumpText += "wearable_gate\towner_player\t";
          wearableDumpText += gateText(ownersResolveToPlayers);
          wearableDumpText += '\t';
          wearableDumpText += std::to_string(wearableOwnerPlayer);
          wearableDumpText += '/';
          wearableDumpText += std::to_string(wearableEntities);
          wearableDumpText += '\n';
        }

        std::wcout << L" wearable_tick=" << wearableTick
                   << L" wearable_status=" << snapshotStatusText(wearableStatus)
                   << L" wearable_resolved_tick=" << wearableResolvedTick
                   << L" wearable_stale=" << wearableStale
                   << L" wearable_states=" << wearableStates.size()
                   << L" wearable_live=" << liveEntities
                   << L" wearable_classes=" << histogram.size()
                   << L" wearable_entities=" << wearableEntities
                   << L" wearable_model_index=" << wearableModelIndex
                   << L" wearable_model_path=" << wearableModelPath
                   << L" wearable_itemdef=" << wearableItemDef
                   << L" wearable_owner=" << wearableOwner
                   << L" wearable_owner_live=" << wearableOwnerLive
                   << L" wearable_owner_player=" << wearableOwnerPlayer
                   << L" wearable_parent=" << wearableParent
                   << L" wearable_attachment=" << wearableAttachment
                   << L" wearable_origin=" << wearableOrigin
                   << L" wearable_angles=" << wearableAngles;
      }
    }
    std::wcout << L'\n';
    // task-35: audit rows are pure ASCII and go through the narrow stream for
    // the same reason the event dumps do -- wcout would re-encode them.
    if (!tickAuditText.empty()) {
      std::wcout.flush();
      std::cout.write(tickAuditText.data(), static_cast<std::streamsize>(tickAuditText.size()));
      std::cout.flush();
    }
    if (!eventDump.empty() || !userInfoDumpText.empty() || !propsDumpText.empty()
        || !tempDumpText.empty() || !wearableDumpText.empty() || !cosmeticsTableText.empty()) {
      std::wcout.flush();
      std::cout.write(eventDump.data(), static_cast<std::streamsize>(eventDump.size()));
      std::cout.write(userInfoDumpText.data(), static_cast<std::streamsize>(userInfoDumpText.size()));
      std::cout.write(propsDumpText.data(), static_cast<std::streamsize>(propsDumpText.size()));
      std::cout.write(tempDumpText.data(), static_cast<std::streamsize>(tempDumpText.size()));
      std::cout.write(wearableDumpText.data(), static_cast<std::streamsize>(wearableDumpText.size()));
      std::cout.write(cosmeticsTableText.data(), static_cast<std::streamsize>(cosmeticsTableText.size()));
      std::cout.flush();
    }
  }
  return failureCode;
}
