// weapon_body_probe -- the static weapon body, the second half of the
// first-person composite.
//
// WHAT IT ANSWERS. The ViewModel entity names a pair of arms
// (`models/weapons/c_models/c_<class>_arms.mdl`) through its own
// `m_nModelIndex`, and the weapon ENTITY that entity's `m_hWeapon` slot points
// at names the static weapon body through `DT_BaseCombatWeapon.m_iWorldModelIndex`
// (`c_pro_rifle.mdl`, `c_medigun.mdl`). Those are two models, and the product
// draws them as two draw requests. This probe drives the lookup that walks that
// chain, against inputs a recording cannot be relied on to contain.
//
// WHY FIXTURES AND NOT MORE RECORDINGS. The shipped input reaches the success
// path and the "nothing resolved" path, and that is all it reaches: no recording
// here contains a weapon whose `m_iWorldModelIndex` is 0, a weapon whose index
// resolves to the arms model, or a weapon entity that is named but absent from
// the snapshot. Each of those is a different repair, and "the body did not draw"
// without a reason is exactly the state that hides a wiring defect. So the
// refusals are constructed here, one at a time, on an input that is otherwise
// identical to the accepted one -- which is also what makes each assertion
// falsifiable: change the one field back and the verdict has to change with it.
//
// The chain, the refusals and the invariant `worldModelPath` is non-empty if and
// only if the lookup succeeded are declared in entity_model.h. The material
// reading is declared in viewmodel.h. This file is a driver, not a second copy
// of either rule.
//
// Exit codes: 0 all checks passed, 1 a check failed, 13 a bad argument.

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include "asset_root.h"
#include "demo_header.h"
#include "entity_model.h"
#include "model_loader.h"
#include "viewmodel.h"

namespace {

using tf2::native::AssetRoot;
using tf2::native::EntityPropertyValue;
using tf2::native::EntityState;
using tf2::native::ModelInspection;
using tf2::native::SendPropType;
using tf2::native::ServerClassSchema;
using tf2::native::ViewModelWeaponBodyLookup;
using tf2::native::ViewModelWeaponBodyRefusal;

// ---------------------------------------------------------------------------
// The fixture's fixed vocabulary
// ---------------------------------------------------------------------------
// One recording's worth of names, taken from the POV recording's tick 16306
// (measured 2026-10-12): ViewModel entity 1077 owns weapon entity 1008, a
// CTFSniperRifle whose m_iWorldModelIndex is 533 and whose precache entry is
// c_pro_rifle.mdl, while the arms the ViewModel entity names sit at precache
// 1032. The indices are kept, because they are the ones a reader can check
// against the recording; the entity numbers are not, because a fixture's entity
// numbering is its own business.
constexpr int kViewModelEntityIndex = 100;
constexpr int kWeaponEntityIndex = 200;
constexpr std::size_t kSnapshotSize = 256;

constexpr std::int32_t kViewModelClassId = 320;
constexpr std::int32_t kWeaponClassId = 307;
constexpr const char* kViewModelClassName = "CTFViewModel";
constexpr const char* kWeaponClassName = "CTFSniperRifle";

constexpr std::uint16_t kWorldModelIndex = 533;
constexpr std::uint16_t kArmsModelIndex = 1032;
constexpr std::uint16_t kOtherWeaponModelIndex = 374;

constexpr const char* kArmsModel = "models/weapons/c_models/c_sniper_arms.mdl";
constexpr const char* kWeaponModel =
  "models/workshop/weapons/c_models/c_pro_rifle/c_pro_rifle.mdl";
constexpr const char* kOtherWeaponModel =
  "models/weapons/c_models/c_medigun/c_medigun.mdl";

// Source's handle layout, restated rather than included: the probe has to be
// able to build a handle that is WRONG in a specific way (the empty sentinel,
// the 2047 the engine leaves behind), and a helper shared with the reader would
// be the same code answering its own question. public/basehandle.h, 11-bit
// index.
constexpr std::uint32_t kHandleIndexBits = 11;
constexpr std::uint32_t kHandleIndexMask = (1u << kHandleIndexBits) - 1u;
constexpr std::uint32_t kInvalidEntityIndex = kHandleIndexMask;
constexpr std::uint32_t kInvalidHandle = 0xffffffffu;

std::uint32_t makeHandle(int entityIndex, std::uint32_t serial = 1) {
  return (serial << kHandleIndexBits) | static_cast<std::uint32_t>(entityIndex);
}

std::string jsonEscape(const std::string& value) {
  std::string out;
  out.reserve(value.size() + 8);
  for (const char character : value) {
    switch (character) {
      case '\\': out += "\\\\"; break;
      case '"': out += "\\\""; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (static_cast<unsigned char>(character) < 32) out += '?';
        else out += character;
    }
  }
  return out;
}

// ---------------------------------------------------------------------------
// The fixture
// ---------------------------------------------------------------------------
struct Fixture {
  std::vector<EntityState> states;
  std::vector<ServerClassSchema> schemas;
  std::unordered_map<std::uint16_t, std::string> precache;
  int viewModelEntityIndex = kViewModelEntityIndex;
  std::string armModelPath = kArmsModel;
};

void setInt(EntityState& state, const char* name, std::int64_t value) {
  EntityPropertyValue property;
  property.type = SendPropType::Int;
  property.intValue = value;
  state.properties[name] = property;
}

void setString(EntityState& state, const char* name, const std::string& value) {
  EntityPropertyValue property;
  property.type = SendPropType::String;
  property.stringValue = value;
  state.properties[name] = property;
}

void eraseSlot(EntityState& state, const char* name) {
  state.properties.erase(name);
}

// The accepted input: every field the chain reads, set to a value that resolves.
// Every negative case below is this fixture with exactly one thing changed, so a
// verdict that fails to move is attributable to that one thing.
Fixture makeFixture() {
  Fixture fixture;
  fixture.states.resize(kSnapshotSize);
  fixture.schemas.push_back({static_cast<std::uint16_t>(kViewModelClassId),
    kViewModelClassName, "DT_TFViewModel"});
  fixture.schemas.push_back({static_cast<std::uint16_t>(kWeaponClassId),
    kWeaponClassName, "DT_BaseCombatWeapon"});
  fixture.precache[kWorldModelIndex] = kWeaponModel;
  fixture.precache[kArmsModelIndex] = kArmsModel;
  fixture.precache[kOtherWeaponModelIndex] = kOtherWeaponModel;

  EntityState& viewModel = fixture.states[kViewModelEntityIndex];
  viewModel.classId = kViewModelClassId;
  setInt(viewModel, "m_nModelIndex", kArmsModelIndex);
  setInt(viewModel, "m_nSequence", 28);
  setInt(viewModel, "m_nViewModelIndex", 0);
  setInt(viewModel, "m_hOwner", static_cast<std::int64_t>(makeHandle(6)));
  setInt(viewModel, "m_hWeapon", static_cast<std::int64_t>(makeHandle(kWeaponEntityIndex)));

  EntityState& weapon = fixture.states[kWeaponEntityIndex];
  weapon.classId = kWeaponClassId;
  setInt(weapon, "m_iWorldModelIndex", kWorldModelIndex);
  setInt(weapon, "m_nModelIndex", kArmsModelIndex);
  setInt(weapon, "m_iViewModelIndex", kArmsModelIndex);
  setInt(weapon, "m_hOwner", static_cast<std::int64_t>(makeHandle(6)));
  return fixture;
}

ViewModelWeaponBodyLookup run(const Fixture& fixture) {
  return tf2::native::findViewModelWeaponBodyInput(fixture.states, fixture.schemas,
    fixture.viewModelEntityIndex, fixture.armModelPath, fixture.precache);
}

const char* refusalName(ViewModelWeaponBodyRefusal refusal) {
  return tf2::native::viewModelWeaponBodyRefusalName(refusal);
}

// The one invariant the driver relies on, asserted once per case rather than
// repeated in each: a refusal never carries a path, so a caller that only wants
// "is there a body to draw" cannot be handed a real file by a refusal. The arms
// case is the one that makes this worth asserting -- it refuses while naming a
// file that exists and draws.
bool pathMatchesVerdict(const ViewModelWeaponBodyLookup& lookup) {
  const bool accepted = lookup.refusal == ViewModelWeaponBodyRefusal::None;
  return accepted == !lookup.worldModelPath.empty();
}

// ---------------------------------------------------------------------------
// Check bookkeeping
// ---------------------------------------------------------------------------
struct Checks {
  std::size_t passed = 0;
  std::size_t failed = 0;
  void check(bool condition, const char* what) {
    if (condition) {
      std::fprintf(stderr, "  OK   %s\n", what);
      ++passed;
    } else {
      std::fprintf(stderr, "  FAIL %s\n", what);
      ++failed;
    }
  }
};

const char* snapshotStatusName(tf2::native::EntitySnapshotQueryStatus status) {
  using tf2::native::EntitySnapshotQueryStatus;
  switch (status) {
    case EntitySnapshotQueryStatus::Available: return "available";
    case EntitySnapshotQueryStatus::Checkpoint: return "checkpoint";
    case EntitySnapshotQueryStatus::NoHistory: return "no-history";
    case EntitySnapshotQueryStatus::TickBeforeHistory: return "before-window";
    case EntitySnapshotQueryStatus::Gap: return "gap";
    case EntitySnapshotQueryStatus::DeltaBaseMissing: return "delta-base-missing";
  }
  return "unknown";
}

// ---------------------------------------------------------------------------
// Section 1 -- the refusals, and the invariant that separates them from success
// ---------------------------------------------------------------------------
void selfTestRefusals(Checks& checks, std::map<std::string, std::size_t>& refusalCounts) {
  const auto record = [&refusalCounts](ViewModelWeaponBodyRefusal refusal) {
    ++refusalCounts[refusalName(refusal)];
  };

  // ---- The shared path rule ----------------------------------------------
  // The arms comparison is only as good as the rule under it, and that rule is
  // shared by three translation units (model_loader.cpp, viewmodel.cpp,
  // entity_model.cpp). Asserted here on the spellings a demo actually writes,
  // because a comparison that folded nothing would still pass every case below
  // whose two sides happen to be spelled identically -- and on the pinned
  // recordings they happen to be.
  {
    using tf2::native::normalizeModelPath;
    checks.check(normalizeModelPath("Models\\Weapons\\C_Models\\C_Sniper_Arms.mdl")
                 == "models/weapons/c_models/c_sniper_arms.mdl",
      "the shared path rule folds backslashes, case and a leading slash");
    checks.check(normalizeModelPath("/models/weapons/c_models/c_pro_rifle.mdl")
                 == "models/weapons/c_models/c_pro_rifle.mdl",
      "the shared path rule strips a leading separator");
    checks.check(normalizeModelPath("models/weapons/c_models/c_pro_rifle.mdl")
                 == normalizeModelPath("MODELS\\WEAPONS\\C_MODELS\\C_PRO_RIFLE.MDL"),
      "the two spellings of one model compare equal");
  }

  // ---- The accepted input -------------------------------------------------
  {
    const auto lookup = run(makeFixture());
    record(lookup.refusal);
    checks.check(lookup.refusal == ViewModelWeaponBodyRefusal::None,
      "the fixture's own chain (ViewModel -> m_hWeapon -> weapon -> m_iWorldModelIndex) resolves");
    checks.check(lookup.worldModelPath == kWeaponModel,
      "the resolved path is the one m_iWorldModelIndex names");
    checks.check(lookup.weaponEntityIndex == kWeaponEntityIndex,
      "the resolved weapon entity is the one m_hWeapon points at");
    checks.check(lookup.weaponEntityPresent, "the weapon entity is reported present");
    checks.check(lookup.weaponClassName == kWeaponClassName,
      "the weapon entity's class is named (not just its index)");
    checks.check(lookup.hasWorldModelIndex && lookup.worldModelIndex == kWorldModelIndex,
      "the world model index that produced the path is reported");
    checks.check(lookup.reason.find("m_iWorldModelIndex") != std::string::npos,
      "the success reason names the rule that produced the path");
    // The serial bits of the handle: entity 200 is not the handle 200, and a
    // lookup that used the whole 32-bit value would look for entity 2248.
    checks.check(lookup.weaponEntityIndex == static_cast<int>(makeHandle(kWeaponEntityIndex) & kHandleIndexMask),
      "only the handle's low 11 bits are used as the entity index");
  }

  // ---- Negative 1: m_iWorldModelIndex is 0 --------------------------------
  // Source's "no model" sentinel, and separately reportable from "the index
  // named nothing": modelprecache does not declare 0 either, so one counter
  // would make the two the same repair.
  {
    auto fixture = makeFixture();
    setInt(fixture.states[kWeaponEntityIndex], "m_iWorldModelIndex", 0);
    const auto lookup = run(fixture);
    record(lookup.refusal);
    checks.check(lookup.refusal == ViewModelWeaponBodyRefusal::WorldModelIndexZero,
      "a weapon whose m_iWorldModelIndex is 0 refuses as a zero index");
    checks.check(lookup.worldModelPath.empty(), "a zero index resolves to no path");
    checks.check(lookup.hasWorldModelIndex, "the slot was read, so the refusal is not 'no slot'");
    checks.check(pathMatchesVerdict(lookup), "a zero index leaves no path behind");
  }

  // ---- Negative 2: the world model resource does not exist ----------------
  // Three separate ways the resource can be absent, and they are three
  // different repairs: a precache entry the demo never declared, an index past
  // the wire's 16-bit space, and a declared entry that carries no name.
  {
    auto fixture = makeFixture();
    fixture.precache.erase(kWorldModelIndex);
    const auto lookup = run(fixture);
    record(lookup.refusal);
    checks.check(lookup.refusal == ViewModelWeaponBodyRefusal::WorldModelNotDeclared,
      "an index modelprecache never declared refuses as undeclared");
    checks.check(lookup.worldModelPath.empty(), "an undeclared index resolves to no path");
    checks.check(lookup.reason.find(std::to_string(kWorldModelIndex)) != std::string::npos,
      "the refusal names the index that was not declared");
  }
  {
    auto fixture = makeFixture();
    fixture.precache[kWorldModelIndex] = "";
    const auto lookup = run(fixture);
    record(lookup.refusal);
    checks.check(lookup.refusal == ViewModelWeaponBodyRefusal::WorldModelNotDeclared,
      "a declared entry with no name refuses the same way (there is nothing to draw)");
  }
  {
    auto fixture = makeFixture();
    setInt(fixture.states[kWeaponEntityIndex], "m_iWorldModelIndex", 70000);
    const auto lookup = run(fixture);
    record(lookup.refusal);
    checks.check(lookup.refusal == ViewModelWeaponBodyRefusal::WorldModelIndexOutOfRange,
      "an index past the 16-bit wire space refuses as out of range");
    checks.check(lookup.worldModelPath.empty(), "an out-of-range index resolves to no path");
  }
  {
    auto fixture = makeFixture();
    setInt(fixture.states[kWeaponEntityIndex], "m_iWorldModelIndex", -4);
    const auto lookup = run(fixture);
    record(lookup.refusal);
    checks.check(lookup.refusal == ViewModelWeaponBodyRefusal::WorldModelIndexOutOfRange,
      "a negative index refuses as out of range rather than wrapping");
  }

  // ---- Negative 3: the arms and the weapon have been swapped --------------
  // The defect signature. A weapon carries m_nModelIndex and m_iWorldModelIndex
  // next to each other in the send table, both resolve to real files, and the
  // wrong one draws a second pair of hands. The refusal is what makes that case
  // visible instead of plausible.
  {
    auto fixture = makeFixture();
    fixture.precache[kWorldModelIndex] = kArmsModel;
    const auto lookup = run(fixture);
    record(lookup.refusal);
    checks.check(lookup.refusal == ViewModelWeaponBodyRefusal::WorldModelIsArms,
      "a world index that resolves to the arms refuses as the arms");
    checks.check(lookup.worldModelPath.empty(),
      "the refusal does NOT hand back the arms path -- the invariant");
    checks.check(lookup.reason.find(kArmsModel) != std::string::npos,
      "the refusal names the model it refused, so the trace is repairable");
    // The control: the same input, with the arms path moved out of the way,
    // resolves. Without this pair "refused" could be a constant.
    auto control = fixture;
    control.armModelPath = kOtherWeaponModel;
    const auto accepted = run(control);
    checks.check(accepted.refusal == ViewModelWeaponBodyRefusal::None,
      "the same input accepts once the arms path is something else (the refusal is not a constant)");
    checks.check(accepted.worldModelPath == kArmsModel,
      "and then the path it reports is exactly the one the index gave");
  }
  {
    // The comparison is on the resolved path, so a demo that wrote the other
    // separator or a different case is the same model and must still refuse.
    auto fixture = makeFixture();
    fixture.precache[kWorldModelIndex] = "models\\weapons\\c_models\\c_sniper_arms.mdl";
    fixture.armModelPath = "models/weapons/c_models/c_sniper_arms.mdl";
    const auto lookup = run(fixture);
    record(lookup.refusal);
    checks.check(lookup.refusal == ViewModelWeaponBodyRefusal::WorldModelIsArms,
      "a backslash-separated arms path is still recognised as the arms");
    // And the other way round, plus case: the two sides are compared as values,
    // not as the strings two different writers happened to produce.
    auto reversed = makeFixture();
    reversed.precache[kWorldModelIndex] = kArmsModel;
    reversed.armModelPath = "MODELS\\WEAPONS\\C_MODELS\\C_SNIPER_ARMS.MDL";
    const auto reverseLookup = run(reversed);
    record(reverseLookup.refusal);
    checks.check(reverseLookup.refusal == ViewModelWeaponBodyRefusal::WorldModelIsArms,
      "and so is an upper-case arms path on the other side of the comparison");
  }

  // ---- Negative 4: the world model is the WRONG weapon --------------------
  // This is the boundary of what the chain can judge, asserted rather than
  // left implied: the lookup has no way to know which weapon body belongs to
  // a CTFSniperRifle. What it CAN promise is that the path it reports is the
  // path the entity's own index gave -- so a substitution is visible in the
  // reading ('the reading says medigun') rather than silently corrected.
  {
    auto fixture = makeFixture();
    fixture.precache[kWorldModelIndex] = kOtherWeaponModel;
    const auto lookup = run(fixture);
    record(lookup.refusal);
    checks.check(lookup.refusal == ViewModelWeaponBodyRefusal::None,
      "a world index naming another, real weapon body is accepted (the chain cannot judge the pairing)");
    checks.check(lookup.worldModelPath == kOtherWeaponModel,
      "and the reading reports the substituted model, not the expected one");
    checks.check(lookup.weaponClassName == kWeaponClassName,
      "the weapon's own class is still reported alongside it, so the pair can be compared");
  }
  {
    // The same substitution, with the weapon's class left as it was and the
    // arms pointing at the medigun: now the arms comparison catches it, which
    // is the one form of "wrong model" the chain can refuse.
    auto fixture = makeFixture();
    fixture.precache[kWorldModelIndex] = kOtherWeaponModel;
    fixture.armModelPath = kOtherWeaponModel;
    const auto lookup = run(fixture);
    record(lookup.refusal);
    checks.check(lookup.refusal == ViewModelWeaponBodyRefusal::WorldModelIsArms,
      "a substitution that also becomes the arms model is refused");
  }

  // ---- Negative 5: the weapon entity is temporarily absent ----------------
  // m_hWeapon names a slot the snapshot does not hold yet. This is a different
  // state from "the handle is empty" -- the entity was named, it is just not
  // here -- and it repairs differently (wait for the packet, not fix the slot).
  {
    auto fixture = makeFixture();
    fixture.states[kWeaponEntityIndex].classId = -1;
    fixture.states[kWeaponEntityIndex].properties.clear();
    const auto lookup = run(fixture);
    record(lookup.refusal);
    checks.check(lookup.refusal == ViewModelWeaponBodyRefusal::WeaponAbsent,
      "a named weapon entity that is not in this snapshot refuses as absent");
    checks.check(lookup.weaponEntityIndex == kWeaponEntityIndex,
      "the absent entity is still named by index, so the trace says which slot");
    checks.check(!lookup.weaponEntityPresent, "an absent entity is not reported present");
    checks.check(lookup.worldModelPath.empty(), "an absent entity yields no path");
  }
  {
    auto fixture = makeFixture();
    setInt(fixture.states[kViewModelEntityIndex], "m_hWeapon",
      static_cast<std::int64_t>(makeHandle(2000)));
    const auto lookup = run(fixture);
    record(lookup.refusal);
    checks.check(lookup.refusal == ViewModelWeaponBodyRefusal::WeaponPastSnapshot,
      "a handle pointing past the snapshot refuses as past-the-end");
    checks.check(lookup.weaponEntityIndex == 2000, "the out-of-range index is still reported");
  }

  // ---- Negative 6: no weapon slot, and the empty handle -------------------
  {
    auto fixture = makeFixture();
    eraseSlot(fixture.states[kViewModelEntityIndex], "m_hWeapon");
    const auto lookup = run(fixture);
    record(lookup.refusal);
    checks.check(lookup.refusal == ViewModelWeaponBodyRefusal::NoWeaponSlot,
      "a ViewModel entity with no m_hWeapon slot refuses as 'no slot'");
    checks.check(!lookup.hasWeaponHandle, "the missing slot is reported as missing");
  }
  {
    auto fixture = makeFixture();
    setInt(fixture.states[kViewModelEntityIndex], "m_hWeapon",
      static_cast<std::int64_t>(kInvalidHandle));
    const auto lookup = run(fixture);
    record(lookup.refusal);
    checks.check(lookup.refusal == ViewModelWeaponBodyRefusal::EmptyWeaponHandle,
      "the 0xffffffff handle sentinel refuses as empty");
  }
  {
    // 2047 is what field initialization leaves behind on an empty-slot
    // ViewModel entity, and it is NOT equal to 0xffffffff -- a check that only
    // compared against the wire sentinel would look up entity 2047.
    auto fixture = makeFixture();
    setInt(fixture.states[kViewModelEntityIndex], "m_hWeapon",
      static_cast<std::int64_t>(kInvalidEntityIndex));
    const auto lookup = run(fixture);
    record(lookup.refusal);
    checks.check(lookup.refusal == ViewModelWeaponBodyRefusal::EmptyWeaponHandle,
      "the 2047 index also refuses as empty");
  }

  // ---- Negative 7: no ViewModel entity at all -----------------------------
  {
    auto fixture = makeFixture();
    fixture.viewModelEntityIndex = -1;
    const auto lookup = run(fixture);
    record(lookup.refusal);
    checks.check(lookup.refusal == ViewModelWeaponBodyRefusal::NoViewModelEntity,
      "no selected ViewModel entity refuses without naming a weapon");
    checks.check(lookup.weaponEntityIndex == -1, "and no weapon entity index is invented");
  }
  {
    auto fixture = makeFixture();
    fixture.viewModelEntityIndex = static_cast<int>(kSnapshotSize) + 40;
    const auto lookup = run(fixture);
    record(lookup.refusal);
    checks.check(lookup.refusal == ViewModelWeaponBodyRefusal::NoViewModelEntity,
      "a ViewModel entity index past the snapshot refuses the same way");
  }
  {
    auto fixture = makeFixture();
    fixture.states[kViewModelEntityIndex].classId = -1;
    fixture.states[kViewModelEntityIndex].properties.clear();
    const auto lookup = run(fixture);
    record(lookup.refusal);
    checks.check(lookup.refusal == ViewModelWeaponBodyRefusal::ViewModelEntityAbsent,
      "a selected slot holding no entity refuses as absent, not as 'no slot'");
    checks.check(!lookup.hasViewModelEntity, "an absent ViewModel entity is not reported present");
  }

  // ---- Negative 8: the weapon carries no m_iWorldModelIndex slot ----------
  {
    auto fixture = makeFixture();
    eraseSlot(fixture.states[kWeaponEntityIndex], "m_iWorldModelIndex");
    const auto lookup = run(fixture);
    record(lookup.refusal);
    checks.check(lookup.refusal == ViewModelWeaponBodyRefusal::NoWorldModelSlot,
      "a weapon with no m_iWorldModelIndex slot refuses as 'no slot'");
    checks.check(!lookup.hasWorldModelIndex, "the missing slot is reported as missing");
  }

  // ---- The invariant, over every case above -------------------------------
  // Re-run each case once and assert the pair, rather than trusting the
  // per-case assertions to have covered it.
  {
    std::vector<Fixture> cases;
    cases.push_back(makeFixture());
    {
      auto fixture = makeFixture();
      setInt(fixture.states[kWeaponEntityIndex], "m_iWorldModelIndex", 0);
      cases.push_back(fixture);
    }
    {
      auto fixture = makeFixture();
      fixture.precache.erase(kWorldModelIndex);
      cases.push_back(fixture);
    }
    {
      auto fixture = makeFixture();
      fixture.precache[kWorldModelIndex] = kArmsModel;
      cases.push_back(fixture);
    }
    {
      auto fixture = makeFixture();
      fixture.states[kWeaponEntityIndex].classId = -1;
      cases.push_back(fixture);
    }
    {
      auto fixture = makeFixture();
      eraseSlot(fixture.states[kViewModelEntityIndex], "m_hWeapon");
      cases.push_back(fixture);
    }
    std::size_t consistent = 0;
    for (const auto& fixture : cases) {
      if (pathMatchesVerdict(run(fixture))) ++consistent;
    }
    checks.check(consistent == cases.size(),
      "worldModelPath is non-empty exactly when the verdict is accepted, in every case");
  }
}

// ---------------------------------------------------------------------------
// Section 2 -- the enum is the partition
// ---------------------------------------------------------------------------
// Every refusal the header declares has a name of its own and a bucket in the
// product's counter array. The array is sized by this count, so a value added
// without a bucket is a compile error in main.cpp -- this check is what makes
// the same statement about the NAMES, which no compiler can see.
void selfTestEnumeration(Checks& checks, std::map<std::string, std::size_t>& refusalCounts) {
  std::vector<std::string> names;
  bool anyUnknown = false;
  bool anyDuplicate = false;
  for (int value = 0; value <= 11; ++value) {
    const char* name = refusalName(static_cast<ViewModelWeaponBodyRefusal>(value));
    if (name == nullptr || std::strcmp(name, "unknown") == 0) anyUnknown = true;
    const std::string text = name == nullptr ? std::string() : std::string(name);
    for (const auto& seen : names) {
      if (seen == text) anyDuplicate = true;
    }
    names.push_back(text);
  }
  checks.check(!anyUnknown, "every declared refusal has a name of its own");
  checks.check(!anyDuplicate, "no two refusals share a name");
  checks.check(names[0] == "none", "value 0 is the accepted verdict");

  // Every refusal this run constructed was reachable. A case that silently
  // stopped being constructed would otherwise just do nothing, and "no
  // negative case fired" is the shape of a probe that tests nothing.
  const std::size_t constructed = refusalCounts.size();
  checks.check(constructed >= 12,
    "the refusals exercised cover the whole enum (>= 12 distinct verdicts)");
  checks.check(refusalCounts.count("none") == 1, "the accepted verdict was exercised");
  std::size_t distinctRefusals = 0;
  for (const auto& entry : refusalCounts) {
    if (entry.first != "none") ++distinctRefusals;
  }
  checks.check(distinctRefusals >= 11, "all eleven refusals were exercised");
}

// ---------------------------------------------------------------------------
// Section 3 -- the material chain, driven with input a shipped model cannot give
// ---------------------------------------------------------------------------
ModelInspection inspectModel(const AssetRoot& root, const std::string& path, bool& ok) {
  ModelInspection inspection;
  const auto candidate = tf2::native::ModelLoader::resolveAsset(root, path);
  if (candidate.resolution == tf2::native::ModelAssetResolution::FoundLoose) {
    inspection = tf2::native::ModelLoader::inspect(candidate.looseMdl);
  } else if (candidate.resolution == tf2::native::ModelAssetResolution::FoundVpk
             && candidate.vpkArchives.size() == 1) {
    tf2::native::VpkArchive archive;
    std::string error;
    if (!archive.open(candidate.vpkArchives.front(), &error)) {
      ok = false;
      return inspection;
    }
    inspection = tf2::native::ModelLoader::inspectVpk(archive, path);
  } else {
    ok = false;
    return inspection;
  }
  ok = inspection.mdl.signatureValid && inspection.metadata.valid;
  return inspection;
}

struct MaterialTotals {
  std::size_t models = 0;
  std::size_t slots = 0;
  std::size_t named = 0;
  std::size_t vmt = 0;
  std::size_t vtf = 0;
};

MaterialTotals measureMaterials(const AssetRoot& root, const std::vector<std::string>& models) {
  MaterialTotals totals;
  for (const auto& path : models) {
    bool ok = false;
    const auto inspection = inspectModel(root, path, ok);
    if (!ok) continue;
    const auto reading = tf2::native::resolveViewModelMaterialChain(root, inspection);
    ++totals.models;
    totals.slots += reading.slots;
    totals.named += reading.named;
    totals.vmt += reading.vmtResolved;
    totals.vtf += reading.vtfResolved;
  }
  return totals;
}

MaterialTotals selfTestMaterials(const AssetRoot& root, Checks& checks,
    const std::vector<std::string>& models) {
  MaterialTotals totals;
  for (const auto& path : models) {
    bool ok = false;
    const auto inspection = inspectModel(root, path, ok);
    const auto padded = std::string(" [") + path + "]";
    checks.check(ok, ("the model resolves and its MDL parses" + padded).c_str());
    if (!ok) continue;
    checks.check(!inspection.metadata.textureCandidates.empty(),
      ("the model declares at least one texture slot" + padded).c_str());

    // The shipped reading: how far the real chain gets.
    const auto real = tf2::native::resolveViewModelMaterialChain(root, inspection);
    ++totals.models;
    totals.slots += real.slots;
    totals.named += real.named;
    totals.vmt += real.vmtResolved;
    totals.vtf += real.vtfResolved;
    std::fprintf(stderr,
      "  ..   %s materials slots=%zu named=%zu vmt=%zu vtf=%zu failure=%s\n",
      path.c_str(), real.slots, real.named, real.vmtResolved, real.vtfResolved,
      real.firstFailure.empty() ? "-" : real.firstFailure.c_str());
    checks.check(real.slots == inspection.metadata.textureCandidates.size(),
      ("the reading counts every slot the MDL declares" + padded).c_str());
    checks.check(real.named == real.slots,
      ("every slot of a shipped model names at least one candidate" + padded).c_str());
    checks.check(real.vmtResolved <= real.named && real.vtfResolved <= real.vmtResolved,
      ("the chain's counts are monotone (vmt <= named, vtf <= vmt)" + padded).c_str());
    const std::size_t missingTextures = real.vmtResolved - real.vtfResolved;
    if (missingTextures == 0) {
      checks.check(real.vtfResolved == real.slots,
        ("this model's materials resolve all the way to a texture" + padded).c_str());
    } else {
      // A model whose $basetexture does not resolve is a fact about the
      // installation, not about this chain. It is reported rather than asserted
      // away, so the run cannot pass by not looking.
      std::fprintf(stderr,
        "  ..   %s has %zu slot(s) whose $basetexture did not resolve: %s\n",
        path.c_str(), missingTextures,
        real.firstFailure.empty() ? "-" : real.firstFailure.c_str());
      checks.check(!real.firstFailure.empty(),
        ("an unresolved $basetexture is named, not just counted" + padded).c_str());
    }

    // ---- The states a shipped installation cannot show --------------------
    // A chain state needs a material that is not installed, and the probe
    // cannot write into the game's installation to make one. So the function is
    // driven with a hand-built inspection instead: the product's own resolution
    // code, given one slot whose candidate names a material nothing holds. That
    // covers "the .vmt is not there"; "the .vmt is there and its $basetexture is
    // not" is covered by mutation in weapon-body-check.sh, for the same reason.
    {
      auto tampered = inspection;
      const std::string absent = "materials/weapon_body_probe/absent_material";
      tampered.metadata.textureCandidates.front() = {absent};
      const auto reading = tf2::native::resolveViewModelMaterialChain(root, tampered);
      checks.check(reading.slots == real.slots,
        ("a tampered slot does not change the slot count" + padded).c_str());
      checks.check(reading.named == real.named,
        ("the tampered slot still counts as named: it has a path, the path just holds nothing"
          + padded).c_str());
      checks.check(reading.vmtResolved + 1 == real.vmtResolved,
        ("a slot whose material is not installed lowers the vmt count by exactly one" + padded).c_str());
      checks.check(reading.vtfResolved + 1 == real.vtfResolved,
        ("and the texture count with it (no texture behind a missing material)" + padded).c_str());
      checks.check(!reading.firstFailure.empty(),
        ("the reading names the slot it stopped on instead of only reporting a number" + padded).c_str());
      checks.check(reading.firstFailure.find("absent_material") != std::string::npos,
        ("the failure names the resource that was not found" + padded).c_str());
      checks.check(reading.firstFailure.find("was not found") != std::string::npos,
        ("and says the resource was absent, not that it failed to parse" + padded).c_str());
    }
    // A slot with no candidate at all: the third way a slot contributes nothing
    // to the chain, and the one the failure text has to distinguish from a
    // missing file ("no candidate path" is a broken MDL; "was not found" is a
    // broken installation).
    {
      auto tampered = inspection;
      tampered.metadata.textureCandidates.front() = {};
      const auto reading = tf2::native::resolveViewModelMaterialChain(root, tampered);
      checks.check(reading.named + 1 == real.named,
        ("a slot with no candidate path is not counted as named" + padded).c_str());
      checks.check(reading.slots == real.slots,
        ("an unnamed slot is still a slot: the count does not silently shrink" + padded).c_str());
      checks.check(!reading.firstFailure.empty(),
        ("an unnamed slot is named in the failure text" + padded).c_str());
      checks.check(reading.firstFailure.find("no candidate path") != std::string::npos,
        ("the failure says the slot had no candidate at all, not that a file was missing"
          + padded).c_str());
    }
    checks.check(inspection.metadata.textureCandidates.size() > 0
                 && !inspection.metadata.textureCandidates.front().empty(),
      ("the tampering worked on copies: the real inspection is unchanged" + padded).c_str());
  }
  return totals;
}

// ---------------------------------------------------------------------------
// Section 3b -- the range table's own materials, through the runtime resolver
// ---------------------------------------------------------------------------
// The section above answers "do this model's texture slots reach a VTF". It says
// nothing about which slot each RANGE draws with, and that is the question T14
// is about: `c_pro_rifle.mdl` draws two LOD-0 descriptors -- the rifle on slot 0
// and its scope on slot 1 -- and a pass that binds one key for the whole buffer
// paints one of them with the other one's albedo. The reading is taken through
// `resolveViewModelMaterialTable`, the same function the product binds from, and
// over `buildModelMeshDataAll`'s range table, the same table the product draws,
// so "the probe says slot 1 is the scope" and "the product drew slot 1" are
// statements about one implementation rather than two that agree by hand.
struct RangeMaterialTotals {
  std::size_t models = 0;
  std::size_t ranges = 0;
  std::size_t texturedRanges = 0;
  std::size_t distinctTextures = 0;
};

RangeMaterialTotals selfTestRangeMaterials(const AssetRoot& root, Checks& checks,
    const std::vector<std::string>& models) {
  RangeMaterialTotals totals;
  for (const auto& path : models) {
    bool ok = false;
    const auto inspection = inspectModel(root, path, ok);
    const auto padded = std::string(" [") + path + "]";
    checks.check(ok, ("the range table's model resolves and parses" + padded).c_str());
    if (!ok) continue;

    tf2::native::ModelMeshData mesh;
    std::string error;
    const bool built = tf2::native::ModelLoader::buildModelMeshDataAll(inspection, mesh, error);
    checks.check(built, ("its LOD-0 mesh builds" + padded).c_str());
    if (!built) continue;
    ++totals.models;

    const auto table = tf2::native::resolveViewModelMaterialTable(root, inspection.metadata,
      static_cast<std::int32_t>(mesh.descriptorIndex));
    checks.check(table.slots.size() == inspection.metadata.textureCandidates.size(),
      ("the runtime table has one entry per MDL texture slot" + padded).c_str());
    checks.check(!mesh.ranges.empty(),
      ("the built mesh has at least one range" + padded).c_str());

    std::vector<std::string> distinct;
    for (const auto& range : mesh.ranges) {
      ++totals.ranges;
      const std::int32_t slot = range.materialSlot;
      const bool inTable = slot >= 0
        && static_cast<std::size_t>(slot) < table.slots.size();
      checks.check(inTable,
        ("range descriptor " + std::to_string(range.descriptorIndex)
          + " names a material slot the MDL declares" + padded).c_str());
      std::string slotName = "-";
      std::string status = "-";
      std::string vmt = "-";
      std::string vtf = "-";
      if (inTable) {
        const auto& material = table.slots[static_cast<std::size_t>(slot)];
        const auto pathParts = [](const std::string& value) {
          return value.empty() ? std::string("-") : value;
        };
        slotName = pathParts(material.name);
        status = tf2::native::viewModelMaterialStatusName(material.status);
        vmt = pathParts(material.vmtPath);
        vtf = pathParts(material.vtfPath);
        const bool textured = material.status == tf2::native::ViewModelMaterialStatus::Resolved
          && !material.vtfPath.empty();
        if (textured) ++totals.texturedRanges;
        if (textured && std::find(distinct.begin(), distinct.end(), material.vtfPath) == distinct.end()) {
          distinct.push_back(material.vtfPath);
        }
      }
      // One line per range: the slot, the resource, and where the chain stopped.
      // Printed rather than only asserted, because the gate pins these strings --
      // "slot 1 resolved to c_pro_rifle.vtf" is checkable against the VMT, while
      // "two slots resolved" is not.
      std::fprintf(stderr,
        "  ..   range descriptor=%zu vertices=%u slot=%d slotName=%s status=%s vmt=%s vtf=%s\n",
        range.descriptorIndex, range.vertexCount, slot, slotName.c_str(), status.c_str(),
        vmt.c_str(), vtf.c_str());
    }
    totals.distinctTextures += distinct.size();
    std::fprintf(stderr,
      "  ..   rangeMaterials ranges=%zu textured=%zu distinctVtf=%zu slots=%zu%s\n",
      mesh.ranges.size(),
      static_cast<std::size_t>(std::count_if(mesh.ranges.begin(), mesh.ranges.end(),
        [&](const tf2::native::ModelMeshData::Range& range) {
          const auto slot = range.materialSlot;
          return slot >= 0 && static_cast<std::size_t>(slot) < table.slots.size()
            && table.slots[static_cast<std::size_t>(slot)].status
              == tf2::native::ViewModelMaterialStatus::Resolved
            && !table.slots[static_cast<std::size_t>(slot)].vtfPath.empty();
        })),
      distinct.size(), table.slots.size(), padded.c_str());
  }
  return totals;
}

// ---------------------------------------------------------------------------
// Section 4 -- the chain on a real recording
// ---------------------------------------------------------------------------
struct DemoCase {
  int viewModelEntity = -1;
  int weaponEntity = -1;
  std::string weaponClass;
  std::int64_t worldModelIndex = 0;
  std::string worldModelPath;
  std::string refusal;
  std::string reason;
  bool snapshot = false;
};

DemoCase runDemo(const std::string& demoPath, std::int64_t tick) {
  DemoCase result;
  tf2::native::DemoHeader header;
  tf2::native::DemoIndex index;
  tf2::native::DemoNetworkSummary summary;
  if (!tf2::native::parseDemoHeaderFile(demoPath, header)) {
    result.reason = "the demo header did not parse";
    return result;
  }
  tf2::native::indexDemoFile(demoPath, header, index);
  summary.networkProtocol = header.networkProtocol;
  if (!tf2::native::scanKnownDemoMessages(demoPath, index, summary)) {
    result.reason = "the demo scan failed";
    return result;
  }
  std::vector<EntityState> states;
  std::int32_t resolvedTick = 0;
  const auto status = tf2::native::queryEntitySnapshotAtOrBeforeTick(
    summary, static_cast<std::int32_t>(tick), states, &resolvedTick);
  if (status != tf2::native::EntitySnapshotQueryStatus::Available
      && status != tf2::native::EntitySnapshotQueryStatus::Checkpoint) {
    result.reason = std::string("no snapshot at or before the tick: ") + snapshotStatusName(status);
    return result;
  }
  result.snapshot = true;
  // The same selection the product makes: the entity the demo's own m_hOwner
  // pairing points at, found by the product's own rule rather than repeated
  // here.
  const auto lookup = tf2::native::findViewModelAnimationInput(states,
    summary.serverClassSchemas, summary.setViewCount > 0, summary.lastViewEntity,
    summary.modelPrecache);
  if (!lookup.input.entityPresent) {
    result.reason = "no ViewModel entity was selected in this snapshot";
    return result;
  }
  result.viewModelEntity = lookup.input.entityIndex;
  const auto body = tf2::native::findViewModelWeaponBodyInput(states,
    summary.serverClassSchemas, lookup.input.entityIndex, lookup.input.entityModelPath,
    summary.modelPrecache);
  result.weaponEntity = body.weaponEntityIndex;
  result.weaponClass = body.weaponClassName;
  result.worldModelIndex = body.worldModelIndex;
  result.worldModelPath = body.worldModelPath;
  result.refusal = refusalName(body.refusal);
  result.reason = body.reason;
  return result;
}

} // namespace

int main(int argc, char** argv) {
  bool selfTestOnly = false;
  std::string tfRoot;
  std::string demoPath;
  std::int64_t tick = -1;
  std::vector<std::string> materialsModels;
  for (int i = 1; i < argc; ++i) {
    const std::string argument = argv[i];
    if (argument == "--self-test") selfTestOnly = true;
    else if (argument == "--tf-root" && i + 1 < argc) tfRoot = argv[++i];
    else if (argument == "--demo" && i + 1 < argc) demoPath = argv[++i];
    else if (argument == "--tick" && i + 1 < argc) tick = std::strtoll(argv[++i], nullptr, 10);
    else if (argument == "--model" && i + 1 < argc) materialsModels.push_back(argv[++i]);
    else {
      std::fprintf(stderr,
        "usage: weapon_body_probe [--self-test] [--tf-root <path>] [--model <mdl>]..."
        " [--demo <path> --tick <n>]\n");
      return 13;
    }
  }

  Checks checks;
  std::map<std::string, std::size_t> refusalCounts;
  std::fprintf(stderr, "=== 1/3 the refusals ===\n");
  selfTestRefusals(checks, refusalCounts);
  std::fprintf(stderr, "=== 2/3 the enumeration ===\n");
  selfTestEnumeration(checks, refusalCounts);

  std::size_t materialModelsTested = 0;
  std::size_t materialSlots = 0;
  std::size_t materialVmt = 0;
  std::size_t materialVtf = 0;
  RangeMaterialTotals rangeMaterial;
  DemoCase demo;

  if (!tfRoot.empty()) {
    const auto assets = AssetRoot::fromPath(tfRoot);
    if (!materialsModels.empty()) {
      std::fprintf(stderr, "=== 3/3 the material chain ===\n");
      const auto totals = selfTestMaterials(assets, checks, materialsModels);
      materialModelsTested = totals.models;
      materialSlots = totals.slots;
      materialVmt = totals.vmt;
      materialVtf = totals.vtf;
      std::fprintf(stderr, "=== 3b/3 the range materials ===\n");
      rangeMaterial = selfTestRangeMaterials(assets, checks, materialsModels);
    }
    if (!demoPath.empty() && tick >= 0) {
      std::fprintf(stderr, "=== 3/3 the recording ===\n");
      demo = runDemo(demoPath, tick);
      if (demo.refusal.empty()) {
        checks.check(false, "the demo case produced a verdict");
      } else {
        std::fprintf(stderr,
          "  ..   demo viewModelEntity=%d weaponEntity=%d class=%s worldModelIndex=%lld path=%s refusal=%s\n",
          demo.viewModelEntity, demo.weaponEntity, demo.weaponClass.c_str(),
          static_cast<long long>(demo.worldModelIndex),
          demo.worldModelPath.empty() ? "-" : demo.worldModelPath.c_str(),
          demo.refusal.c_str());
        checks.check(demo.snapshot, "a snapshot was available at the requested tick");
        checks.check(demo.viewModelEntity >= 0, "a ViewModel entity was selected");
        checks.check(demo.weaponEntity >= 0, "the ViewModel entity named a weapon entity");
        checks.check(!demo.weaponClass.empty(), "the weapon entity's class resolved");
        checks.check(!demo.worldModelPath.empty(),
          "the weapon's own m_iWorldModelIndex resolved to a path");
        checks.check(demo.worldModelPath.find("_arms.mdl") == std::string::npos,
          "the resolved path is not the arms composite");
      }
    }
  } else if (!selfTestOnly) {
    std::fprintf(stderr, "note: no --tf-root given; the material chain was not measured\n");
  }

  std::fprintf(stderr, "self-test failures=%zu passed=%zu\n", checks.failed, checks.passed);

  std::string refusalSummary;
  for (const auto& entry : refusalCounts) {
    if (!refusalSummary.empty()) refusalSummary += ",";
    refusalSummary += entry.first;
    refusalSummary += ":";
    refusalSummary += std::to_string(entry.second);
  }
  std::cout << "{"
            << "\"ok\":" << (checks.failed == 0 ? "true" : "false")
            << ",\"selfTest\":" << (selfTestOnly ? "true" : "false")
            << ",\"checksPassed\":" << checks.passed
            << ",\"checksFailed\":" << checks.failed
            << ",\"refusalsExercised\":\"" << jsonEscape(refusalSummary) << "\""
            << ",\"materialModelsTested\":" << materialModelsTested
            << ",\"materialSlots\":" << materialSlots
            << ",\"materialVmtResolved\":" << materialVmt
            << ",\"materialVtfResolved\":" << materialVtf
            << ",\"rangeModelsTested\":" << rangeMaterial.models
            << ",\"rangeCount\":" << rangeMaterial.ranges
            << ",\"rangeTextured\":" << rangeMaterial.texturedRanges
            << ",\"rangeDistinctTextures\":" << rangeMaterial.distinctTextures
            << ",\"demo\":\"" << jsonEscape(demoPath) << "\""
            << ",\"tick\":" << tick
            << ",\"viewModelEntity\":" << demo.viewModelEntity
            << ",\"weaponEntity\":" << demo.weaponEntity
            << ",\"weaponClass\":\"" << jsonEscape(demo.weaponClass) << "\""
            << ",\"worldModelIndex\":" << demo.worldModelIndex
            << ",\"worldModelPath\":\"" << jsonEscape(demo.worldModelPath) << "\""
            << ",\"refusal\":\"" << jsonEscape(demo.refusal) << "\""
            << ",\"reason\":\"" << jsonEscape(demo.reason) << "\""
            << "}\n";
  std::fflush(stderr);
  std::fflush(stdout);
  return checks.failed == 0 ? 0 : 1;
}
