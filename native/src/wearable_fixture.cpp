#include "wearable_fixture.h"

#include "demo_header.h"
#include "item_schema.h"
#include "wearable_instance.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <fstream>
#include <system_error>
#include <utility>

namespace tf2::native {
namespace {

// --------------------------------------------------------------- the synthetic
// items_game.txt. Written to the temp directory and removed again, because
// ItemSchema is move-only and its only public constructor path is load(): a
// fixture that could build one directly would be testing a schema object the
// product can never hold.
constexpr const char* kFixtureSchemaText =
  "\"items_game\"\n"
  "{\n"
  "	\"prefabs\"\n"
  "	{\n"
  "		\"hat_fixture_family\"\n"
  "		{\n"
  "			\"item_class\"	\"tf_wearable\"\n"
  "			\"model_player_per_class\"\n"
  "			{\n"
  "				\"basename\"	\"models/player/items/all_class/ring_%s.mdl\"\n"
  "			}\n"
  "		}\n"
  "	}\n"
  "	\"items\"\n"
  "	{\n"
  "		\"1001\"\n"
  "		{\n"
  "			\"prefab\"	\"hat_fixture_family\"\n"
  "			\"model_player\"	\"models/player/items/fixture/hat_literal.mdl\"\n"
  "		}\n"
  "		\"1002\"\n"
  "		{\n"
  "			\"model_player_per_class\"\n"
  "			{\n"
  "				\"soldier\"	\"models/player/items/fixture/hat_soldier.mdl\"\n"
  "			}\n"
  "		}\n"
  "		\"1003\"\n"
  "		{\n"
  "			\"item_class\"	\"tf_wearable\"\n"
  "		}\n"
  "	}\n"
  "}\n";

struct Check {
  std::string name;
  bool passed = false;
  std::string detail;
};

void reportChecks(std::vector<Check>& checks, std::vector<std::string>& report,
                  std::size_t& failed) {
  for (const Check& check : checks) {
    if (!check.passed) ++failed;
    std::string line = "wearable_fixture\t";
    line += check.name;
    line += check.passed ? "\tPASS\t" : "\tFAIL\t";
    line += check.detail;
    report.push_back(std::move(line));
  }
  checks.clear();
}

// ------------------------------------------------------------- snapshot pieces
EntityPropertyValue intProperty(std::int64_t value) {
  EntityPropertyValue property;
  property.type = SendPropType::Int;
  property.intValue = value;
  return property;
}

void setInt(EntityState& state, const char* name, std::int64_t value) {
  state.properties[name] = intProperty(value);
}

// One snapshot under construction: the class table, the states it indexes, and
// the slot-generation capture the table reads generations out of.
struct Fixture {
  DemoNetworkSummary summary;
  std::vector<EntityState> states;
  WearableSlotCapture capture;
};

constexpr std::uint16_t kPlayerClassId = 1;
constexpr std::uint16_t kWearableClassId = 2;

void addClasses(Fixture& fixture) {
  ServerClassSchema player;
  player.id = kPlayerClassId;
  player.name = "CTFPlayer";
  player.dataTable = "DT_TFPlayer";
  fixture.summary.serverClassSchemas.push_back(std::move(player));
  ServerClassSchema wearable;
  wearable.id = kWearableClassId;
  wearable.name = "CTFWearable";
  wearable.dataTable = "DT_WearableItem";
  fixture.summary.serverClassSchemas.push_back(std::move(wearable));
}

// Entity 0 is worldspawn, 1 is the player, 2 the wearable: the smallest snapshot
// in which the attribution rule has something to be wrong about.
constexpr std::size_t kPlayerEntity = 1;
constexpr std::size_t kWearableEntity = 2;

void addPlayer(Fixture& fixture, std::int64_t tfClass) {
  EntityState player;
  player.classId = kPlayerClassId;
  setInt(player, "m_iClass", tfClass);
  fixture.states[kPlayerEntity] = std::move(player);
}

void addWearable(Fixture& fixture) {
  EntityState wearable;
  wearable.classId = kWearableClassId;
  fixture.states[kWearableEntity] = std::move(wearable);
}

// A slot generation is what separates "this wearable's owner is still the player
// that was in that slot" from "the slot was deleted and reused since". Every
// fixture that expects an ATTRIBUTED row must therefore arm the capture: an
// unarmed capture answers "generation unknown", which is a different verdict and
// would make the case under test unreachable.
void armDefaultCapture(Fixture& fixture) {
  fixture.capture.record(0, static_cast<std::uint32_t>(kPlayerEntity), 1u);
  fixture.summary.wearableSlotCapture = &fixture.capture;
}

std::int64_t ownerHandleFor(std::size_t entityIndex) {
  // Source CBaseHandle: 11-bit entity index in the low bits, generation in the
  // high ones. The table masks the index out, so a bare index is a valid handle.
  return static_cast<std::int64_t>(entityIndex);
}

WearableInstanceTable build(Fixture& fixture, std::int32_t tick, const ItemSchema* schema) {
  WearableTableRequest request;
  request.tick = tick;
  request.schema = schema;
  request.assetRootValid = true;
  request.assetRootDescribe = "fixture";
  return buildWearableInstanceTableFromStates(
    fixture.summary, request, fixture.states, tick, true);
}

const WearableInstanceRow* firstRow(const WearableInstanceTable& table) {
  for (const WearableInstanceRow& row : table.rows) {
    if (row.entityIndex == static_cast<std::int32_t>(kWearableEntity)) return &row;
  }
  return nullptr;
}

// The dump module keeps its name tables in an anonymous namespace, so the suite
// prints the enum VALUES and one short word each. The names are not the evidence
// here -- the value is, and the probe's own dump still prints the names.
const char* attributionWord(WearableAttributionStatus value) {
  switch (value) {
    case WearableAttributionStatus::Attributed: return "attributed";
    case WearableAttributionStatus::UnattributedNoOwner: return "no-owner-handle";
    case WearableAttributionStatus::UnattributedEmptySlot: return "empty-slot";
    case WearableAttributionStatus::UnattributedNotPlayer: return "not-player";
    case WearableAttributionStatus::UnattributedGenerationChanged: return "generation-changed";
    case WearableAttributionStatus::UnattributedUnknownGeneration: return "generation-unknown";
  }
  return "?";
}

const char* pathWord(WearablePathSource value) {
  switch (value) {
    case WearablePathSource::None: return "none";
    case WearablePathSource::Schema: return "schema";
    case WearablePathSource::Precache: return "precache";
  }
  return "?";
}

const char* rejectWord(WearableRejectReason value) {
  switch (value) {
    case WearableRejectReason::None: return "none";
    case WearableRejectReason::BrokenAttachment: return "broken-attachment";
    case WearableRejectReason::OwnerNotPlayer: return "owner-not-player";
    case WearableRejectReason::OwnerMissing: return "owner-missing";
    case WearableRejectReason::OwnerGenerationUnknown: return "owner-generation-unknown";
    case WearableRejectReason::ClassNotAllowed: return "class-not-allowed";
    case WearableRejectReason::NoPath: return "no-path";
  }
  return "?";
}

std::string describe(const WearableInstanceRow* row) {
  if (row == nullptr) return "no row for the wearable entity";
  std::string text = "attribution=";
  text += attributionWord(row->attribution.status);
  text += " owner=";
  text += std::to_string(row->attribution.ownerIndex);
  text += " source=";
  text += pathWord(row->model.source);
  text += " reject=";
  text += rejectWord(row->reject);
  text += " renderable=";
  text += row->renderable ? "1" : "0";
  return text;
}

// -------------------------------------------------------------- the five cases
void checkMissingOwnerProperty(std::vector<Check>& checks) {
  Fixture fixture;
  fixture.states.resize(3);
  addClasses(fixture);
  addPlayer(fixture, 3);
  addWearable(fixture);
  armDefaultCapture(fixture);
  // The property is simply absent: no m_hOwnerEntity key at all.
  const WearableInstanceTable table = build(fixture, 100, nullptr);
  const WearableInstanceRow* row = firstRow(table);
  Check check;
  check.name = "missing-owner-property";
  check.passed = row != nullptr
    && row->attribution.status == WearableAttributionStatus::UnattributedNoOwner
    && row->reject == WearableRejectReason::OwnerMissing
    && !row->renderable;
  check.detail = describe(row);
  checks.push_back(std::move(check));
}

void checkInvalidHandles(std::vector<Check>& checks) {
  // (a) the empty/invalid sentinel. Source writes 0xFFFFFFFF for "no entity".
  {
    Fixture fixture;
    fixture.states.resize(3);
    addClasses(fixture);
    addPlayer(fixture, 3);
    addWearable(fixture);
    armDefaultCapture(fixture);
    setInt(fixture.states[kWearableEntity], "m_hOwnerEntity", 0xFFFFFFFFLL);
    const WearableInstanceTable table = build(fixture, 100, nullptr);
    const WearableInstanceRow* row = firstRow(table);
    Check check;
    check.name = "invalid-handle-sentinel";
    check.passed = row != nullptr
      && row->attribution.status == WearableAttributionStatus::UnattributedNoOwner
      && row->reject == WearableRejectReason::OwnerMissing
      && !row->renderable;
    check.detail = describe(row);
    checks.push_back(std::move(check));
  }
  // (b) a handle that indexes past the end of the snapshot. The index survives
  // the mask, so this is the empty-slot case, not the no-handle case -- the two
  // are different facts and the counters keep them apart.
  {
    Fixture fixture;
    fixture.states.resize(3);
    addClasses(fixture);
    addPlayer(fixture, 3);
    addWearable(fixture);
    armDefaultCapture(fixture);
    setInt(fixture.states[kWearableEntity], "m_hOwnerEntity", 900LL);
    const WearableInstanceTable table = build(fixture, 100, nullptr);
    const WearableInstanceRow* row = firstRow(table);
    Check check;
    check.name = "invalid-handle-past-snapshot";
    check.passed = row != nullptr
      && row->attribution.status == WearableAttributionStatus::UnattributedEmptySlot
      && row->reject == WearableRejectReason::OwnerMissing
      && !row->renderable;
    check.detail = describe(row);
    checks.push_back(std::move(check));
  }
  // (c) a live handle onto an entity that is not a player. CTFPlayerResource owns
  // entities in a TF2 snapshot and "TFPlayer" is a substring of it, so the owner
  // test has to be an equality on the class name.
  {
    Fixture fixture;
    fixture.states.resize(3);
    addClasses(fixture);
    ServerClassSchema resource;
    resource.id = 7;
    resource.name = "CTFPlayerResource";
    fixture.summary.serverClassSchemas.push_back(std::move(resource));
    EntityState notPlayer;
    notPlayer.classId = 7;
    fixture.states[kPlayerEntity] = std::move(notPlayer);
    addWearable(fixture);
    armDefaultCapture(fixture);
    setInt(fixture.states[kWearableEntity], "m_hOwnerEntity",
           ownerHandleFor(kPlayerEntity));
    const WearableInstanceTable table = build(fixture, 100, nullptr);
    const WearableInstanceRow* row = firstRow(table);
    Check check;
    check.name = "owner-is-not-a-player";
    check.passed = row != nullptr
      && row->attribution.status == WearableAttributionStatus::UnattributedNotPlayer
      && row->reject == WearableRejectReason::OwnerNotPlayer
      && !row->renderable;
    check.detail = describe(row);
    checks.push_back(std::move(check));
  }
}

void checkDeletedAndReusedSlot(std::vector<Check>& checks) {
  Fixture fixture;
  fixture.states.resize(3);
  addClasses(fixture);
  addPlayer(fixture, 3);
  addWearable(fixture);
  setInt(fixture.states[kWearableEntity], "m_hOwnerEntity", ownerHandleFor(kPlayerEntity));
  setInt(fixture.states[kWearableEntity], "m_iItemDefinitionIndex", 1002);
  // The player slot was used at tick 50, deleted, and reused at tick 300. The
  // table is built for tick 100, where the slot held generation 1.
  fixture.capture.record(50, static_cast<std::uint32_t>(kPlayerEntity), 1u);
  fixture.capture.record(300, static_cast<std::uint32_t>(kPlayerEntity), 2u);
  fixture.summary.wearableSlotCapture = &fixture.capture;
  const WearableInstanceTable table = build(fixture, 100, nullptr);
  const WearableInstanceRow* row = firstRow(table);
  Check check;
  check.name = "deleted-and-reused-slot";
  check.passed = row != nullptr
    && row->attribution.status == WearableAttributionStatus::UnattributedGenerationChanged
    && row->attribution.ownerGeneration == 1u
    && row->attribution.ownerGenerationAtEnd == 2u
    && row->reject == WearableRejectReason::BrokenAttachment
    && !row->renderable;
  check.detail = describe(row)
    + " generation=" + std::to_string(row != nullptr ? row->attribution.ownerGeneration : 0u)
    + " atEnd=" + std::to_string(row != nullptr ? row->attribution.ownerGenerationAtEnd : 0u);
  checks.push_back(std::move(check));
}

void checkMissingModelIndex(std::vector<Check>& checks) {
  Fixture fixture;
  fixture.states.resize(3);
  addClasses(fixture);
  addPlayer(fixture, 3);
  addWearable(fixture);
  armDefaultCapture(fixture);
  setInt(fixture.states[kWearableEntity], "m_hOwnerEntity", ownerHandleFor(kPlayerEntity));
  // Attributed to a live player, and no route to a model: no item definition for
  // the schema, no model index for the precache, nothing to open.
  const WearableInstanceTable table = build(fixture, 100, nullptr);
  const WearableInstanceRow* row = firstRow(table);
  Check check;
  check.name = "missing-model-index";
  check.passed = row != nullptr
    && row->attribution.status == WearableAttributionStatus::Attributed
    && row->model.source == WearablePathSource::None
    && row->reject == WearableRejectReason::NoPath
    && !row->renderable;
  check.detail = describe(row);
  checks.push_back(std::move(check));
}

void checkSchemaFallback(std::vector<Check>& checks, const ItemSchema* schema) {
  if (schema == nullptr) {
    Check check;
    check.name = "schema-fallback";
    check.passed = false;
    check.detail = "the synthetic items_game.txt could not be loaded";
    checks.push_back(std::move(check));
    return;
  }
  // (a) the family TEMPLATE loses to the literal. Item 1001 inherits
  // "ring_%s.mdl" from its prefab for the basename tier and publishes a literal
  // model_player; the template is recorded as the schema's answer but must not
  // win, because "%s" names no file.
  {
    Fixture fixture;
    fixture.states.resize(3);
    addClasses(fixture);
    addPlayer(fixture, 3);
    addWearable(fixture);
    armDefaultCapture(fixture);
    setInt(fixture.states[kWearableEntity], "m_hOwnerEntity", ownerHandleFor(kPlayerEntity));
    setInt(fixture.states[kWearableEntity], "m_iItemDefinitionIndex", 1001);
    const WearableInstanceTable table = build(fixture, 100, schema);
    const WearableInstanceRow* row = firstRow(table);
    Check check;
    check.name = "schema-template-loses-to-literal";
    check.passed = row != nullptr
      && row->model.source == WearablePathSource::Schema
      && row->model.schemaKey == WearableSchemaKey::ModelPlayer
      && !row->model.schemaTemplate
      && row->model.path == "models/player/items/fixture/hat_literal.mdl"
      && row->renderable;
    check.detail = describe(row) + " path=" + (row != nullptr ? row->model.path : std::string());
    checks.push_back(std::move(check));
  }
  // (b) the per-class tier wins when the owner carries a class the item has a
  // model for, and the row names that class so a wrong-class hat is visible.
  {
    Fixture fixture;
    fixture.states.resize(3);
    addClasses(fixture);
    addPlayer(fixture, 3);  // soldier
    addWearable(fixture);
    armDefaultCapture(fixture);
    setInt(fixture.states[kWearableEntity], "m_hOwnerEntity", ownerHandleFor(kPlayerEntity));
    setInt(fixture.states[kWearableEntity], "m_iItemDefinitionIndex", 1002);
    const WearableInstanceTable table = build(fixture, 100, schema);
    const WearableInstanceRow* row = firstRow(table);
    Check check;
    check.name = "schema-per-class-wins";
    check.passed = row != nullptr
      && row->model.source == WearablePathSource::Schema
      && row->model.schemaKey == WearableSchemaKey::ModelPlayerPerClass
      && row->model.schemaClass == "soldier"
      && row->renderable;
    check.detail = describe(row) + " schemaClass="
      + (row != nullptr ? row->model.schemaClass : std::string());
    checks.push_back(std::move(check));
  }
  // (c) an item the schema has an entry for but no model for falls through to
  // the precache, which is the route that answers the rows the schema cannot.
  {
    Fixture fixture;
    fixture.states.resize(3);
    addClasses(fixture);
    addPlayer(fixture, 3);
    addWearable(fixture);
    armDefaultCapture(fixture);
    setInt(fixture.states[kWearableEntity], "m_hOwnerEntity", ownerHandleFor(kPlayerEntity));
    setInt(fixture.states[kWearableEntity], "m_iItemDefinitionIndex", 1003);
    setInt(fixture.states[kWearableEntity], "m_nModelIndex", 42);
    fixture.summary.modelPrecache[42] = "models/player/items/fixture/hat_precache.mdl";
    const WearableInstanceTable table = build(fixture, 100, schema);
    const WearableInstanceRow* row = firstRow(table);
    Check check;
    check.name = "schema-empty-falls-to-precache";
    check.passed = row != nullptr
      && row->model.source == WearablePathSource::Precache
      && row->model.path == "models/player/items/fixture/hat_precache.mdl"
      && row->renderable;
    check.detail = describe(row) + " path=" + (row != nullptr ? row->model.path : std::string());
    checks.push_back(std::move(check));
  }
  // (d) a template the schema had and a precache that cannot answer is NoPath --
  // and it is counted as a template refusal, so "no hat" and "a %s we could not
  // fill in" are different numbers.
  {
    Fixture fixture;
    fixture.states.resize(3);
    addClasses(fixture);
    addPlayer(fixture, 3);
    addWearable(fixture);
    armDefaultCapture(fixture);
    setInt(fixture.states[kWearableEntity], "m_hOwnerEntity", ownerHandleFor(kPlayerEntity));
    // Item 1003 has a schema entry and no model on any tier, and its model index
    // names no precache entry either: both routes answer "nothing", so the row is
    // no-path rather than a template that looked like a path.
    setInt(fixture.states[kWearableEntity], "m_nModelIndex", 999);
    setInt(fixture.states[kWearableEntity], "m_iItemDefinitionIndex", 1003);
    const WearableInstanceTable table = build(fixture, 100, schema);
    const WearableInstanceRow* row = firstRow(table);
    Check check;
    check.name = "no-route-is-no-path";
    check.passed = row != nullptr
      && row->model.source == WearablePathSource::None
      && row->reject == WearableRejectReason::NoPath
      && !row->renderable
      && table.counters.rejectNoPath == 1;
    check.detail = describe(row) + " rejectNoPath="
      + std::to_string(table.counters.rejectNoPath);
    checks.push_back(std::move(check));
  }
}

// The synthetic schema, loaded from a temp file and removed again. Returns null
// with the reason in \`error\` when the fixture cannot be built at all.
std::unique_ptr<ItemSchema> loadFixtureSchema(std::string& error) {
  std::error_code code;
  const std::filesystem::path directory = std::filesystem::temp_directory_path(code);
  if (code) { error = "no temp directory: " + code.message(); return nullptr; }
  const std::filesystem::path path = directory / "tf2_wearable_fixture_items_game.txt";
  {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) { error = "cannot write " + path.string(); return nullptr; }
    output.write(kFixtureSchemaText, static_cast<std::streamsize>(std::strlen(kFixtureSchemaText)));
    if (!output) { error = "short write to " + path.string(); return nullptr; }
  }
  std::string loadError;
  std::unique_ptr<ItemSchema> schema = ItemSchema::load(path, loadError);
  std::filesystem::remove(path, code);
  if (!schema) { error = loadError.empty() ? "ItemSchema::load refused the fixture" : loadError; }
  return schema;
}

} // namespace

std::size_t runWearableFixtureSuite(std::vector<std::string>& report) {
  std::size_t failed = 0;
  std::vector<Check> checks;
  checkMissingOwnerProperty(checks);
  checkInvalidHandles(checks);
  checkDeletedAndReusedSlot(checks);
  checkMissingModelIndex(checks);
  std::string schemaError;
  const std::unique_ptr<ItemSchema> schema = loadFixtureSchema(schemaError);
  checkSchemaFallback(checks, schema.get());
  reportChecks(checks, report, failed);
  if (!schemaError.empty()) {
    report.push_back("wearable_fixture\tschema-load\tFAIL\t" + schemaError);
    ++failed;
  }
  return failed;
}

} // namespace tf2::native
