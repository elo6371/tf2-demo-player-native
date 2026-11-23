#include "wearable_instance.h"

#include "entity_model.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <map>
#include <utility>

namespace tf2::native {
namespace {

// ---------------------------------------------------------------- reading rules
// The same rule the entity model uses, reached through the same function. A
// second suffix matcher here could drift from rankPropertyCandidates and the
// drift would be invisible exactly when the rule is the thing under test
// (native/src/entity_model.cpp:190-204 ranks by preferCandidate).
const EntityPropertyValue* rankedValue(const EntityState& state, const char* suffix,
                                       std::string* name) {
  const std::vector<PropertyCandidate> ranked = rankPropertyCandidates(state, suffix);
  if (ranked.empty()) return nullptr;
  const auto found = state.properties.find(ranked.front().name);
  if (found == state.properties.end()) return nullptr;
  if (name != nullptr) *name = found->first;
  return &found->second;
}

// A replicated prop can arrive as Float or Int and only the type says which field
// carries the reading -- the same coercion readIntSlot applies.
bool rankedInt(const EntityState& state, const char* suffix, std::int64_t& out) {
  const EntityPropertyValue* value = rankedValue(state, suffix, nullptr);
  if (value == nullptr) return false;
  if (value->type == SendPropType::Float) {
    out = static_cast<std::int64_t>(value->x);
  } else if (value->type == SendPropType::Int) {
    out = value->intValue;
  } else {
    return false;
  }
  return true;
}

// classId -> class name, matched on the schema id. The positional serverClassNames
// array only lines up when wire order happens to match, which is not a property of
// the wire this layer controls.
const std::string* serverClassName(const DemoNetworkSummary& summary, std::int32_t classId) {
  if (classId < 0) return nullptr;
  const std::uint32_t wanted = static_cast<std::uint32_t>(classId);
  for (const ServerClassSchema& schema : summary.serverClassSchemas) {
    if (static_cast<std::uint32_t>(schema.id) == wanted && !schema.name.empty()) return &schema.name;
  }
  return nullptr;
}

// modelprecache lookup with the same bounds test demo_header_strtab.cpp uses: entry
// 0 is the empty slot and the table is indexed by a 16-bit precache index. A
// sentinel reading fails this test, which is the whole point -- it is a reading,
// not a missing value.
const std::string* precachePath(const DemoNetworkSummary& summary, std::int64_t index) {
  if (index < 1 || index > 0xffff) return nullptr;
  const auto found = summary.modelPrecache.find(static_cast<std::uint16_t>(index));
  if (found == summary.modelPrecache.end() || found->second.empty()) return nullptr;
  return &found->second;
}

// Source CBaseHandle: 11-bit entity index in the low bits. Empty handles are the
// ones entity_model.cpp already refuses as parents, plus 0 -- worldspawn is not an
// owner.
constexpr std::uint32_t kHandleIndexMask = 0x7FFu;
constexpr std::uint32_t kInvalidHandle = 0xFFFFFFFFu;
constexpr std::uint32_t kInvalidHandleFull = 0x1FFFFFu;
bool handleIndex(std::int64_t raw, std::uint32_t& index) {
  const std::uint32_t value = static_cast<std::uint32_t>(raw);
  if (value == 0 || value == kInvalidHandle || value == kInvalidHandleFull) return false;
  index = value & kHandleIndexMask;
  return true;
}

// The owner test, spelled once. task-39 used a case-insensitive SUBSTRING test and
// "TFPlayer" is inside "CTFPlayerResource", which also owns entities in a TF2
// snapshot; the class that actually owns a cosmetic is exactly CTFPlayer, so this
// is an equality on the schema name and nothing wider.
bool isPlayerClass(const std::string& className) { return className == "CTFPlayer"; }

// ------------------------------------------------------- schema model selection
// TF2 class numbers to the keys items_game.txt uses inside model_player_per_class.
// The nine class keys measured across that file are exactly these plus "basename",
// which is a FALLBACK KEY and not a class -- treating it as one would make every
// row claim a class it does not have.
//
// THE ORDER IS TF2'S OWN, not this repository's map. entity_model.cpp:699-711 maps
// 6..9 to heavy/pyro/spy/engineer (TF_CLASS_HEAVYWEAPONS, PYRO, SPY, ENGINEER),
// while map_overview.cpp:77-87 has 6..9 as engineer/heavy/pyro/spy. Those two
// tables disagree; this module follows entity_model's, because that is the table
// the rest of the render path resolves player models through, and it uses the
// items_game KEY spelling (demoman, not the demo.mdl stem).
struct ClassKey {
  std::int64_t tfClass;
  const char* key;
};
constexpr ClassKey kClassKeys[] = {
  {1, "scout"}, {2, "sniper"}, {3, "soldier"}, {4, "demoman"}, {5, "medic"},
  {6, "heavy"}, {7, "pyro"}, {8, "spy"}, {9, "engineer"},
};

const char* classKey(std::int64_t tfClass) {
  for (const ClassKey& row : kClassKeys) if (row.tfClass == tfClass) return row.key;
  return nullptr;
}

// Case-insensitive substring test. Used ONLY to notice a class the table has not
// considered -- never to admit one. The table is the rule; this is the alarm that
// fires when the rule has not heard of something.
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

// The player class the owner carries, accepted only in [1,9], the same window
// entity_model.cpp:259-264 accepts m_iClass in. Outside it the reading is not a
// class and a per-class model lookup must not be attempted.
bool playerClassOf(const EntityState& owner, std::int64_t& tfClass) {
  std::int64_t raw = 0;
  if (!rankedInt(owner, "m_iClass", raw)) return false;
  if (raw < 1 || raw > 9) return false;
  tfClass = raw;
  return true;
}

}  // namespace

// ---------------------------------------------------------------- D1: the table
// Why each row is in or out, decided from the build oracle
// (evidence/animation-availability/oracle-all-classes.txt -- nine classes whose
// name contains "Wearable", ids 35 and 336..343) and from what the demos actually
// instance. The table is a NAME allow-table rather than a netvar test because the
// DT_TFWearable netvars (m_bDisguiseWearable [226], m_hWeaponAssociatedWith [227])
// also appear as the last flats of classes that are not wearables at all --
// CTFWeaponPDAExpansion_Teleporter (id 334), CTFWeaponSapper (335),
// CTFProjectile_Arrow (252) -- so a netvar test would admit weapons as cosmetics.
const std::vector<WearableClassRule>& classRuleStorage() {
  static const std::vector<WearableClassRule> kRules = {
      {"CTFWearable", WearableClassVerdict::Allowed, WearableDrawPath::World,
       "The plain cosmetic: 34 of the 36 instances at stv tick 49444, "
       "every one of them owned by a CTFPlayer."},
      {"CTFWearableVM", WearableClassVerdict::Allowed, WearableDrawPath::ViewModel,
       "Owned by the same CTFPlayer, but its moveParent is the CTFViewModel: "
       "attached to the first-person model, so it is admitted and routed to "
       "the other path rather than filtered out by a name test."},
      {"CTFWearableCampaignItem", WearableClassVerdict::Allowed, WearableDrawPath::World,
       "A cosmetic by every observable: CTFPlayer owner, a real itemDef "
       "(5869, the cyoa pda) and a modelprecache path of its own."},
      {"CEconWearable", WearableClassVerdict::Excluded, WearableDrawPath::World,
       "The base the TF wearables derive from, and a separate server class "
       "with 226 flats. Never instanced in any measured tick: it reaches the "
       "world through its derived class."},
      {"CTFWearableDemoShield", WearableClassVerdict::Excluded, WearableDrawPath::World,
       "Never instanced in any measured tick. Admitting it would claim a "
       "draw path that has not been exercised once on this corpus."},
      {"CTFWearableItem", WearableClassVerdict::Excluded, WearableDrawPath::World,
       "Never instanced in any measured tick, and its 226-flat table is the "
       "base of several others."},
      {"CTFWearableLevelableItem", WearableClassVerdict::Excluded, WearableDrawPath::World,
       "Never instanced in any measured tick. Its ownership is a "
       "levelable-item rule this project does not implement, so the row would "
       "be attributed but undrawable."},
      {"CTFWearableRazorback", WearableClassVerdict::Excluded, WearableDrawPath::World,
       "Never instanced in any measured tick; the sniper shield is part of "
       "the player model the game draws, not a separate cosmetic."},
      {"CTFWearableRobotArm", WearableClassVerdict::Excluded, WearableDrawPath::World,
       "Never instanced in any measured tick. A model-swap cosmetic on the "
       "robot arm, which no measured demo contains."},
  };
  return kRules;
}

const WearableClassRule* findWearableClassRule(const std::string& className) {
  for (const WearableClassRule& rule : classRuleStorage()) {
    if (className == rule.name) return &rule;
  }
  return nullptr;
}

const std::vector<WearableClassRule>& wearableClassRules() { return classRuleStorage(); }

const char* wearableClassVerdictName(WearableClassVerdict value) {
  switch (value) {
    case WearableClassVerdict::Allowed: return "allowed";
    case WearableClassVerdict::Excluded: return "excluded";
  }
  return "unknown";
}

const char* wearableDrawPathName(WearableDrawPath value) {
  switch (value) {
    case WearableDrawPath::World: return "world";
    case WearableDrawPath::ViewModel: return "viewmodel";
  }
  return "unknown";
}

// -------------------------------------------------------- slot lifetime capture
void WearableSlotCapture::record(std::int32_t tick, std::uint32_t slot,
                                 std::uint32_t generation) {
  entries.push_back(WearableSlotCaptureEntry{tick, slot, generation});
}

void recordWearableSlotGeneration(WearableSlotCapture* capture, std::int32_t tick,
                                  std::uint32_t slot, std::uint32_t generation) {
  // The null test IS the switch: no capture means no work, which is what makes
  // this free on every path that does not ask for wearables.
  if (capture == nullptr) return;
  capture->record(tick, slot, generation);
}

std::uint32_t WearableSlotCapture::generationAt(std::int32_t tick, std::uint32_t slot,
                                                bool* known) const {
  if (known != nullptr) *known = false;
  // Append order is scan order and the tick never decreases, so the entries that
  // can describe `tick` are a prefix. upper_bound on tick finds the end of it.
  const auto end = std::upper_bound(
      entries.begin(), entries.end(), tick,
      [](std::int32_t wanted, const WearableSlotCaptureEntry& entry) { return wanted < entry.tick; });
  // Walk back through that prefix for the slot's LAST advance at or before the
  // tick. Walking back rather than keeping a per-slot map because the capture is
  // small by construction (two advances per entity lifetime) and a map would need
  // a mutable cache to answer a const question.
  for (auto it = end; it != entries.begin();) {
    --it;
    if (it->slot == slot) {
      if (known != nullptr) *known = true;
      return it->generation;
    }
  }
  // No advance recorded at or before the tick. That is UNKNOWN, not zero: the
  // slot may already have been on its fourth lifetime by then.
  return 0;
}

std::uint32_t WearableSlotCapture::latestGeneration(std::uint32_t slot, bool* known) const {
  if (known != nullptr) *known = false;
  for (auto it = entries.rbegin(); it != entries.rend(); ++it) {
    if (it->slot == slot) {
      if (known != nullptr) *known = true;
      return it->generation;
    }
  }
  return 0;
}

std::int32_t WearableSlotCapture::advanceTickAt(std::int32_t tick, std::uint32_t slot,
                                                 bool* known) const {
  if (known != nullptr) *known = false;
  const auto end = std::upper_bound(
      entries.begin(), entries.end(), tick,
      [](std::int32_t wanted, const WearableSlotCaptureEntry& entry) { return wanted < entry.tick; });
  for (auto it = end; it != entries.begin();) {
    --it;
    if (it->slot == slot) {
      if (known != nullptr) *known = true;
      return it->tick;
    }
  }
  return -1;
}

namespace {

// A schema model string that still carries a printf placeholder is a per-class
// FAMILY template, not a model. items_game.txt writes those families with "%s" meant
// to be substituted with the class token (".../all_class/ring_%s.mdl" for item 5075,
// ".../items/%s/%s_halloween.mdl" for the holiday families) and 5574 lines of the
// file contain a '%'. A loader handed such a string looks for a literal file with a
// '%' in its name and finds nothing, so it must never count as a resolved path --
// but it is not noise either, because it is the schema's actual answer for 15 of 36
// instances on stv. It is therefore measured: kept on the row, excluded from
// winning, and counted under pathSchemaTemplate.
bool hasTemplatePlaceholder(const std::string& value) {
  return value.find('%') != std::string::npos;
}

// ------------------------------------------------------- D3: the path resolver
// THE INVERSION, in code. The handover puts modelprecache first and the item
// schema second (docs/HANDOVER-2026-11-21.md:312). The measurement is the other
// way round: on this corpus 302 of 332 wearable instances carry an m_nModelIndex
// outside [1, 0xffff] -- 4294967280, 4294967292, 4294967294, 4294967278 -- which
// modelprecache cannot index at all, while their itemDef names a real cosmetic
// that items_game.txt publishes a model for. So the schema answers first here and
// the precache is the exception.
//
// Both routes are evaluated on EVERY row regardless of which one wins, and both
// answers are kept on the row. That is what turns the inversion into a reading:
// the dump reports how many rows each route can answer and how many disagree, so
// a later reader can check the precedence instead of taking it on faith.
void resolveWearablePath(const EntityState& state, const EntityState* owner,
                         const DemoNetworkSummary& summary, const ItemSchema* schema,
                         WearablePath& path) {
  // -- the readings that drive everything, kept even when they cannot resolve.
  std::int64_t itemDefinition = 0;
  path.hasItemDefinitionIndex = rankedInt(state, "m_iItemDefinitionIndex", itemDefinition);
  if (path.hasItemDefinitionIndex) path.itemDefinitionIndex = static_cast<std::int32_t>(itemDefinition);
  std::int64_t modelIndex = 0;
  path.hasModelIndex = rankedInt(state, "m_nModelIndex", modelIndex);
  if (path.hasModelIndex) path.modelIndex = static_cast<std::int32_t>(modelIndex);
  path.modelIndexInPrecacheRange = path.hasModelIndex && path.modelIndex >= 1
                                   && path.modelIndex <= 0xffff;
  std::int64_t worldModelIndex = 0;
  path.hasWorldModelIndex = rankedInt(state, "m_iWorldModelIndex", worldModelIndex);
  if (path.hasWorldModelIndex) path.worldModelIndex = static_cast<std::int32_t>(worldModelIndex);

  // -- route 1: the item schema.
  if (schema != nullptr && path.hasItemDefinitionIndex) {
    path.schemaHasEntry = schema->find(path.itemDefinitionIndex) != nullptr;
    const ItemSchemaModelCandidates candidates = schema->modelCandidates(path.itemDefinitionIndex);
    // Per-class first, and only for a class the owner actually carries: an item
    // whose model depends on the class has one model per class, and picking the
    // wrong one would put a demoman hat on a scout. "basename" is the schema's
    // own fallback key inside that block and is NOT a class name.
    //
    // Losing a candidate to a template costs nothing that was usable, so each tier
    // FALLS THROUGH to the next rather than stopping: an item whose per-class entry
    // for this owner is a family template may still publish a literal model_player,
    // and reaching the precache is better than stopping at an unloadable string.
    std::int64_t tfClass = 0;
    const char* key = (owner != nullptr && playerClassOf(*owner, tfClass)) ? classKey(tfClass) : nullptr;
    // schemaClass is null for the two tiers that are not per class, and constructing a
    // std::string from a null pointer is undefined behaviour (it faults), so the
    // assignment goes through a guarded helper rather than the raw pointer.
    auto claim = [&path](const std::string& value, WearableSchemaKey tier, const char* schemaClass) {
      const auto setClass = [&path](const char* text) {
        path.schemaClass = text != nullptr ? text : "";
      };
      if (hasTemplatePlaceholder(value)) {
        path.schemaTemplate = true;
        if (path.schemaCandidate.empty()) {
          // The template travels only when nothing better was found, so the column
          // keeps one meaning: what the schema's best answer was.
          path.schemaCandidate = value;
          path.schemaKey = tier;
          setClass(schemaClass);
        }
        return false;
      }
      path.schemaCandidate = value;
      path.schemaKey = tier;
      setClass(schemaClass);
      path.schemaTemplate = false;
      return true;
    };
    bool resolved = false;
    if (key != nullptr) {
      const auto perClass = candidates.modelPlayerPerClass.find(key);
      if (perClass != candidates.modelPlayerPerClass.end() && !perClass->second.empty()) {
        resolved = claim(perClass->second, WearableSchemaKey::ModelPlayerPerClass, key);
      }
    }
    if (!resolved) {
      const auto basename = candidates.modelPlayerPerClass.find("basename");
      if (basename != candidates.modelPlayerPerClass.end() && !basename->second.empty()) {
        resolved = claim(basename->second, WearableSchemaKey::ModelPlayerPerClass, "basename");
      }
    }
    // A cosmetic hangs off a player, so model_player is the right second choice;
    // model_world is the third and only matters for an item with no player model.
    if (!resolved && !candidates.modelPlayer.empty()) {
      resolved = claim(candidates.modelPlayer, WearableSchemaKey::ModelPlayer, nullptr);
    }
    if (!resolved && !candidates.modelWorld.empty()) {
      resolved = claim(candidates.modelWorld, WearableSchemaKey::ModelWorld, nullptr);
    }
  }

  // -- route 2: modelprecache. m_nModelIndex first, then m_iWorldModelIndex, which
  // is the pair demo_header_strtab.cpp:783-800 already reads for weapon entities.
  if (const std::string* found = path.hasModelIndex ? precachePath(summary, path.modelIndex) : nullptr) {
    path.precacheCandidate = *found;
  } else if (path.hasWorldModelIndex) {
    if (const std::string* world = precachePath(summary, path.worldModelIndex)) {
      path.precacheCandidate = *world;
    }
  }
  // Measured, not assumed: modelprecache holds literal file names, so this is
  // expected to stay false, and the day it is not, the precedence below must not
  // quietly prefer it.
  path.precacheTemplate = hasTemplatePlaceholder(path.precacheCandidate);

  // -- precedence: the schema, because that is the route that answers on this
  // data -- but only when the schema named a MODEL. A family template is not an
  // answer: an unsubstituted "%s" names no file, so a template loses to a literal
  // precache path, and a row where the template is all the schema had and the
  // precache cannot answer either comes out NoPath. Handing the loader "ring_%s.mdl"
  // and calling the row renderable is precisely the failure this flag prevents.
  const bool schemaUsable = !path.schemaCandidate.empty() && !path.schemaTemplate;
  const bool precacheUsable = !path.precacheCandidate.empty() && !path.precacheTemplate;
  if (schemaUsable) {
    path.source = WearablePathSource::Schema;
    path.path = path.schemaCandidate;
  } else if (precacheUsable) {
    path.source = WearablePathSource::Precache;
    path.path = path.precacheCandidate;
  } else {
    path.source = WearablePathSource::None;
  }
  // Disagreement is a reading about the two routes' ANSWERS, so it is computed from
  // the raw candidates whatever their usability: a template that contradicts a real
  // path is still the two routes disagreeing, and suppressing it would hide the case
  // this flag exists for.
  path.routesDisagree = !path.schemaCandidate.empty() && !path.precacheCandidate.empty()
                        && path.schemaCandidate != path.precacheCandidate;
}

}  // namespace

// ----------------------------------------------------------------- the builder
WearableInstanceTable buildWearableInstanceTable(const DemoNetworkSummary& summary,
                                                 const WearableTableRequest& request) {
  std::vector<EntityState> states;
  std::int32_t resolvedTick = request.tick;
  const EntitySnapshotQueryStatus status =
      queryEntitySnapshotAtOrBeforeTick(summary, request.tick, states, &resolvedTick);
  const bool resolved = status == EntitySnapshotQueryStatus::Available
                        || status == EntitySnapshotQueryStatus::Checkpoint;
  return buildWearableInstanceTableFromStates(summary, request, states, resolvedTick, resolved);
}

WearableInstanceTable buildWearableInstanceTableFromStates(
    const DemoNetworkSummary& summary, const WearableTableRequest& request,
    const std::vector<EntityState>& states, std::int32_t resolvedTick, bool resolved) {
  WearableInstanceTable table;
  // "Armed" is a reading, not a constant: it says whether the scan this table was
  // built from had a slot-lifetime capture attached. An unarmed table can still
  // describe models and paths, but every generation on it is unknown and no owner
  // can be certified, so a reader has to see the difference before trusting a row.
  table.armed = summary.wearableSlotCapture != nullptr;
  table.counters.tick = request.tick;
  table.schemaLoaded = request.schema != nullptr;
  table.assetRootValid = request.assetRootValid;
  table.assetRootDescribe = request.assetRootDescribe;
  table.schemaError = request.schemaError;

  // The scan-dump vocabulary again: names, ids and the round trip through the
  // class table are the only things this function trusts without a capture.
  const WearableSlotCapture* capture = summary.wearableSlotCapture;

  table.counters.resolved = resolved;
  table.counters.resolvedTick = resolvedTick;
  table.hasSnapshot = table.counters.resolved && !states.empty();
  if (!table.hasSnapshot) return table;
  // A checkpoint answer is not the tick asked for: it is real data about an
  // older world, and a consumer that ignores the difference will put a stale hat
  // on a live player. The staleness travels with the table.
  table.counters.stale = request.tick - resolvedTick;

  // Every class name in this snapshot that contains "Wearable" but is not in the
  // allow-table. Collected first, so a class the table has never considered shows
  // up as a reading instead of vanishing.
  for (const ServerClassSchema& schema : summary.serverClassSchemas) {
    if (schema.name.empty()) continue;
    if (findWearableClassRule(schema.name) != nullptr) continue;
    if (!containsNoCase(schema.name, "Wearable")) continue;
    if (std::find(table.unlistedWearableNames.begin(), table.unlistedWearableNames.end(),
                  schema.name) == table.unlistedWearableNames.end()) {
      table.unlistedWearableNames.push_back(schema.name);
    }
  }

  std::map<std::string, std::size_t> classTally;
  table.rows.reserve(states.size() / 4u);

  for (std::size_t index = 0; index < states.size(); ++index) {
    const EntityState& state = states[index];
    const std::string* className = serverClassName(summary, state.classId);
    if (className == nullptr) continue;
    const WearableClassRule* rule = findWearableClassRule(*className);
    // Not a class this table knows: not a wearable instance, not examined. The
    // unlisted-name sweep above is what makes sure "not known" cannot hide a
    // wearable the table should have covered.
    if (rule == nullptr) continue;

    WearableInstanceRow row;
    row.tick = request.tick;
    row.resolvedTick = resolvedTick;
    row.entityIndex = static_cast<std::int32_t>(index);
    row.className = *className;
    row.classRule = rule;
    row.path = rule->path;
    ++table.counters.examined;
    ++classTally[*className];

    // -- identity. The generation is the one this slot carried AT THE RESOLVED
    // TICK. It is UNKNOWN when no capture was armed or when the slot had not
    // advanced by then; an unknown generation refuses attribution below rather
    // than being treated as 0.
    if (capture != nullptr) {
      row.generation = capture->generationAt(resolvedTick, static_cast<std::uint32_t>(index),
                                             &row.generationKnown);
    }

    // -- owner. Read the raw handle, print it, then interpret it.
    WearableAttribution& attribution = row.attribution;
    std::int64_t ownerRaw = 0;
    if (rankedInt(state, "m_hOwnerEntity", ownerRaw)) {
      attribution.hasOwnerHandle = true;
      attribution.ownerHandle = static_cast<std::uint32_t>(ownerRaw);
    }
    std::uint32_t ownerIndexValue = 0;
    if (!attribution.hasOwnerHandle || !handleIndex(ownerRaw, ownerIndexValue)) {
      attribution.status = WearableAttributionStatus::UnattributedNoOwner;
    } else if (static_cast<std::size_t>(ownerIndexValue) >= states.size()
               || states[ownerIndexValue].classId < 0) {
      attribution.status = WearableAttributionStatus::UnattributedEmptySlot;
      attribution.ownerIndex = static_cast<std::int32_t>(ownerIndexValue);
    } else {
      attribution.ownerIndex = static_cast<std::int32_t>(ownerIndexValue);
      const std::string* ownerClassName =
          serverClassName(summary, states[ownerIndexValue].classId);
      attribution.ownerClassName = ownerClassName != nullptr ? ownerClassName->c_str() : nullptr;
      if (ownerClassName == nullptr || !isPlayerClass(*ownerClassName)) {
        attribution.status = WearableAttributionStatus::UnattributedNotPlayer;
      } else if (capture == nullptr) {
        attribution.status = WearableAttributionStatus::UnattributedUnknownGeneration;
      } else {
        attribution.ownerGeneration =
            capture->generationAt(resolvedTick, ownerIndexValue, &attribution.ownerGenerationKnown);
        attribution.ownerGenerationAtEnd =
            capture->latestGeneration(ownerIndexValue, &attribution.ownerGenerationAtEndKnown);
        if (!attribution.ownerGenerationKnown) {
          attribution.status = WearableAttributionStatus::UnattributedUnknownGeneration;
        } else if (attribution.ownerGenerationAtEndKnown
                   && attribution.ownerGenerationAtEnd != attribution.ownerGeneration) {
          // The owner slot changed hands after the tick. The handle index still
          // names a player, but not necessarily the one this instance was
          // attached to, so the attachment is BROKEN and never inherited by
          // index alone.
          attribution.status = WearableAttributionStatus::UnattributedGenerationChanged;
        } else {
          attribution.status = WearableAttributionStatus::Attributed;
        }
      }
    }

    // -- moveParent and attachment. Read with both spellings: the wire sends the
    // slot as "moveparent" and entity_model.cpp:885 already learned to accept both.
    WearableParent& parent = row.parent;
    std::int64_t parentRaw = 0;
    if (rankedInt(state, "m_hMoveParent", parentRaw)
        || rankedInt(state, "moveparent", parentRaw)) {
      parent.hasMoveParentHandle = true;
      parent.moveParentHandle = static_cast<std::uint32_t>(parentRaw);
      parent.moveParentIndex = static_cast<std::int32_t>(parentRaw & kHandleIndexMask);
    }
    std::int64_t attachment = 0;
    if (rankedInt(state, "m_iParentAttachment", attachment)) {
      parent.parentAttachment = static_cast<std::int32_t>(attachment);
    }
    std::uint32_t parentIndexValue = 0;
    if (parent.hasMoveParentHandle && handleIndex(parentRaw, parentIndexValue)) {
      parent.moveParentIndex = static_cast<std::int32_t>(parentIndexValue);
      parent.moveParentMatchesOwner = attribution.ownerIndex >= 0
                                      && parent.moveParentIndex == attribution.ownerIndex;
      if (static_cast<std::size_t>(parentIndexValue) < states.size()
          && states[parentIndexValue].classId >= 0) {
        const std::string* parentClassName =
            serverClassName(summary, states[parentIndexValue].classId);
        parent.moveParentClassName = parentClassName != nullptr ? parentClassName->c_str() : nullptr;
      }
      if (capture != nullptr) {
        parent.moveParentGeneration =
            capture->generationAt(resolvedTick, parentIndexValue, &parent.moveParentGenerationKnown);
        parent.moveParentGenerationAtEnd =
            capture->latestGeneration(parentIndexValue, &parent.moveParentGenerationAtEndKnown);
      }
    }

    // -- path. The owner entity is passed in because a per-class model needs the
    // owner's class, and the owner is the only entity that has one.
    const EntityState* ownerState = attribution.ownerIndex >= 0
        && static_cast<std::size_t>(attribution.ownerIndex) < states.size()
        ? &states[static_cast<std::size_t>(attribution.ownerIndex)] : nullptr;
    resolveWearablePath(state, ownerState, summary, request.schema, row.model);

    // -- verdict. Ordered, and the FIRST reason wins, so a row can only be counted
    // once and the count says what actually stopped it: an excluded class is not
    // also reported as pathless, and a broken attachment is not reported as a
    // missing model.
    if (row.classRule->verdict != WearableClassVerdict::Allowed) {
      row.reject = WearableRejectReason::ClassNotAllowed;
      ++table.counters.rejectClassNotAllowed;
    } else if (attribution.status == WearableAttributionStatus::UnattributedGenerationChanged) {
      row.reject = WearableRejectReason::BrokenAttachment;
      ++table.counters.rejectBrokenAttachment;
    } else if (attribution.status == WearableAttributionStatus::UnattributedNotPlayer) {
      row.reject = WearableRejectReason::OwnerNotPlayer;
      ++table.counters.rejectOwnerNotPlayer;
    } else if (attribution.status == WearableAttributionStatus::UnattributedNoOwner
               || attribution.status == WearableAttributionStatus::UnattributedEmptySlot) {
      row.reject = WearableRejectReason::OwnerMissing;
      ++table.counters.rejectOwnerMissing;
    } else if (attribution.status == WearableAttributionStatus::UnattributedUnknownGeneration) {
      row.reject = WearableRejectReason::OwnerGenerationUnknown;
      ++table.counters.rejectOwnerGenerationUnknown;
    } else if (row.model.source == WearablePathSource::None) {
      // No path is a COUNTED SKIP. task-41 must not draw this instance with
      // another model or a placeholder; the row says why it has nothing to draw.
      row.reject = WearableRejectReason::NoPath;
      ++table.counters.rejectNoPath;
      if (row.model.schemaTemplate) ++table.counters.rejectNoPathTemplate;
    } else {
      row.renderable = true;
      ++table.counters.renderable;
      if (row.path == WearableDrawPath::World) ++table.counters.drawPathWorld;
      else ++table.counters.drawPathViewModel;
    }

    // -- the three families.
    switch (attribution.status) {
      case WearableAttributionStatus::Attributed: ++table.counters.attributed; break;
      case WearableAttributionStatus::UnattributedNoOwner:
        ++table.counters.unattributedNoOwner; break;
      case WearableAttributionStatus::UnattributedEmptySlot:
        ++table.counters.unattributedEmptySlot; break;
      case WearableAttributionStatus::UnattributedNotPlayer:
        ++table.counters.unattributedNotPlayer; break;
      case WearableAttributionStatus::UnattributedGenerationChanged:
        ++table.counters.unattributedGenerationChanged; break;
      case WearableAttributionStatus::UnattributedUnknownGeneration:
        ++table.counters.unattributedUnknownGeneration; break;
    }
    switch (row.model.source) {
      case WearablePathSource::Schema: ++table.counters.pathSchema; break;
      case WearablePathSource::Precache: ++table.counters.pathPrecache; break;
      case WearablePathSource::None: ++table.counters.pathNone; break;
    }
    // Census, deliberately outside the three-way switch: a template row can resolve
    // from the precache and still be counted in pathPrecache above.
    if (row.model.schemaTemplate) ++table.counters.pathSchemaTemplate;
    if (row.model.precacheTemplate) ++table.counters.pathPrecacheTemplate;

    // NOTE there is no defensive "renderable but templated" branch here, and its
    // absence is deliberate: resolveWearablePath refuses to set source to Schema or
    // Precache unless the winning candidate is free of placeholders, so a renderable
    // row cannot be carrying a template on the route that won. A guard that repairs
    // the counters here would have to move rows between pathSchema/pathPrecache and
    // pathNone after the three-way switch already counted them, which is exactly the
    // kind of second bookkeeping site that drifts. The invariant lives in one place
    // and pathSchemaTemplate/rejectNoPathTemplate are how a reader verifies it.
    table.rows.push_back(std::move(row));
  }

  for (const auto& entry : classTally) table.counters.classRows.push_back(entry);
  return table;
}

// ------------------------------------------------------------------ counters
bool WearableTableCounters::balanced() const {
  // Three sums, each against the number of rows examined. A failure here is a bug
  // in this module, not in the data, and the dump prints the sum beside the total
  // so the reader can see WHICH family broke.
  const std::size_t attribution = attributed + unattributedNoOwner + unattributedEmptySlot
                                  + unattributedNotPlayer + unattributedGenerationChanged
                                  + unattributedUnknownGeneration;
  const std::size_t sources = pathSchema + pathPrecache + pathNone;
  const std::size_t verdicts = renderable + rejectBrokenAttachment + rejectOwnerNotPlayer
                              + rejectOwnerMissing + rejectOwnerGenerationUnknown
                              + rejectClassNotAllowed + rejectNoPath;
  return attribution == examined && sources == examined && verdicts == examined;
}

// --------------------------------------------------------------------- dump
namespace {

// One field, tab separated, with a dash for "no reading". The dash is load-bearing:
// printing 0 for an absent handle and 0 for the empty handle would make two
// different facts look identical in the output.
void appendField(std::wstring& out, const wchar_t* value) {
  out.append(value != nullptr ? value : L"-");
  out.push_back(L'\t');
}

// The std::string overload comes first because the const char* overload below
// delegates to it, and overload resolution at a call site only sees the
// declarations that precede it.
void appendField(std::wstring& out, const std::string& value) {
  if (value.empty()) {
    out.push_back(L'-');
  } else {
    // The dump is written with std::cout.write against a wide string elsewhere in
    // this probe, so each byte is widened one-for-one rather than interpreted.
    for (const char ch : value) out.push_back(static_cast<wchar_t>(static_cast<unsigned char>(ch)));
  }
  out.push_back(L'\t');
}

// The const char* overload exists because two row fields are optional C strings
// (ownerClassName, moveParentClassName) and are null when the slot had no class.
// Letting the std::string overload take them would construct std::string from a
// null pointer, which is undefined behaviour -- and it would look like it worked.
void appendField(std::wstring& out, const char* value) {
  appendField(out, value != nullptr ? std::string(value) : std::string());
}

void appendNumber(std::wstring& out, std::int64_t value) {
  wchar_t buffer[32] = {};
  std::swprintf(buffer, 32, L"%lld", static_cast<long long>(value));
  out.append(buffer);
  out.push_back(L'\t');
}

void appendCount(std::wstring& out, std::size_t value) {
  appendNumber(out, static_cast<std::int64_t>(value));
}

const wchar_t* attributionStatusName(WearableAttributionStatus value) {
  switch (value) {
    case WearableAttributionStatus::Attributed: return L"attributed";
    case WearableAttributionStatus::UnattributedNoOwner: return L"no-owner-handle";
    case WearableAttributionStatus::UnattributedEmptySlot: return L"empty-slot";
    case WearableAttributionStatus::UnattributedNotPlayer: return L"not-player";
    case WearableAttributionStatus::UnattributedGenerationChanged: return L"generation-changed";
    case WearableAttributionStatus::UnattributedUnknownGeneration: return L"generation-unknown";
  }
  return L"unknown";
}

const wchar_t* pathSourceName(WearablePathSource value) {
  switch (value) {
    case WearablePathSource::Schema: return L"schema";
    case WearablePathSource::Precache: return L"precache";
    case WearablePathSource::None: return L"none";
  }
  return L"unknown";
}

const wchar_t* schemaKeyName(WearableSchemaKey value) {
  switch (value) {
    case WearableSchemaKey::ModelPlayer: return L"model_player";
    case WearableSchemaKey::ModelWorld: return L"model_world";
    case WearableSchemaKey::ModelPlayerPerClass: return L"model_player_per_class";
    case WearableSchemaKey::None: return L"-";
  }
  return L"-";
}

const wchar_t* rejectName(WearableRejectReason value) {
  switch (value) {
    case WearableRejectReason::None: return L"none";
    case WearableRejectReason::BrokenAttachment: return L"broken-attachment";
    case WearableRejectReason::OwnerNotPlayer: return L"owner-not-player";
    case WearableRejectReason::OwnerMissing: return L"owner-missing";
    case WearableRejectReason::OwnerGenerationUnknown: return L"owner-generation-unknown";
    case WearableRejectReason::ClassNotAllowed: return L"class-not-allowed";
    case WearableRejectReason::NoPath: return L"no-path";
  }
  return L"unknown";
}

}  // namespace

std::wstring formatWearableTableLines(const WearableInstanceTable& table) {
  std::wstring out;
  out.reserve(table.rows.size() * 220u + 4096u);
  const std::int32_t tick = table.counters.tick;

  // -- the class table itself, so D1 travels with the evidence it produced.
  for (const WearableClassRule& rule : wearableClassRules()) {
    out.append(L"wearable_rule\t");
    appendNumber(out, tick);
    appendField(out, rule.name);
    appendField(out, wearableClassVerdictName(rule.verdict));
    appendField(out, wearableDrawPathName(rule.path));
    // `because` is an ASCII char literal array; widened byte-for-byte so the dump
    // stays one uniform encoding.
    if (rule.because != nullptr) {
      for (const char ch : std::string(rule.because)) {
        out.push_back(static_cast<wchar_t>(static_cast<unsigned char>(ch)));
      }
    } else {
      out.push_back(L'-');
    }
    out.push_back(L'\n');
  }

  // -- one row per examined instance.
  for (const WearableInstanceRow& row : table.rows) {
    out.append(L"wearable_instance\t");
    appendNumber(out, row.tick);
    appendNumber(out, row.resolvedTick);
    appendNumber(out, row.entityIndex);
    // An unknown generation prints a dash, never 0: 0 is the generation of a slot
    // that has never been recycled, and the two must not look alike.
    if (row.generationKnown) appendNumber(out, static_cast<std::int64_t>(row.generation));
    else appendField(out, std::string());
    appendField(out, row.className);
    appendField(out, row.classRule != nullptr ? row.classRule->verdict == WearableClassVerdict::Allowed
                                               ? "allowed" : "excluded" : "unlisted");
    appendField(out, wearableDrawPathName(row.path));
    appendField(out, attributionStatusName(row.attribution.status));
    if (row.attribution.ownerIndex >= 0) appendNumber(out, row.attribution.ownerIndex);
    else appendField(out, std::string());
    appendField(out, row.attribution.ownerClassName);
    if (row.attribution.ownerGenerationKnown) {
      appendNumber(out, static_cast<std::int64_t>(row.attribution.ownerGeneration));
    } else {
      appendField(out, std::string());
    }
    if (row.attribution.ownerGenerationAtEndKnown) {
      appendNumber(out, static_cast<std::int64_t>(row.attribution.ownerGenerationAtEnd));
    } else {
      appendField(out, std::string());
    }
    if (row.parent.hasMoveParentHandle) {
      appendNumber(out, static_cast<std::int64_t>(row.parent.moveParentHandle));
      appendNumber(out, row.parent.moveParentIndex);
    } else {
      appendField(out, std::string());
      appendField(out, std::string());
    }
    appendField(out, row.parent.moveParentClassName);
    appendNumber(out, row.parent.parentAttachment);
    appendField(out, pathSourceName(row.model.source));
    appendField(out, schemaKeyName(row.model.schemaKey));
    appendField(out, row.model.schemaClass);
    appendField(out, row.model.path);
    if (row.model.hasItemDefinitionIndex) appendNumber(out, row.model.itemDefinitionIndex);
    else appendField(out, std::string());
    if (row.model.hasModelIndex) appendNumber(out, row.model.modelIndex);
    else appendField(out, std::string());
    if (row.model.hasWorldModelIndex) appendNumber(out, row.model.worldModelIndex);
    else appendField(out, std::string());
    // Both route answers travel with the row, so the inversion can be checked
    // from the dump instead of believed from the report.
    appendField(out, row.model.precacheCandidate);
    appendField(out, row.model.schemaCandidate);
    appendField(out, row.model.routesDisagree ? "yes" : "no");
    // Is the schema's answer usable? A row can have a schemaCandidate AND this "no",
    // which is the whole point: the string is printed above, the verdict here says
    // whether it names a file.
    appendField(out, row.model.schemaTemplate ? "no" : row.model.schemaCandidate.empty() ? "-" : "yes");
    appendField(out, row.renderable ? L"renderable" : rejectName(row.reject));
    out.push_back(L'\n');
  }

  // -- counters. Each family prints its own sum beside the number of rows it is
  // supposed to add up to, because a mismatch is a defect in this module and the
  // only useful question is which family broke.
  out.append(L"wearable_counters\t");
  appendNumber(out, tick);
  appendNumber(out, table.counters.resolvedTick);
  appendCount(out, table.counters.examined);
  out.append(L"\n");

  const std::size_t attributionSum = table.counters.attributed
      + table.counters.unattributedNoOwner + table.counters.unattributedEmptySlot
      + table.counters.unattributedNotPlayer + table.counters.unattributedGenerationChanged
      + table.counters.unattributedUnknownGeneration;
  const std::size_t sourceSum = table.counters.pathSchema + table.counters.pathPrecache
      + table.counters.pathNone;
  const std::size_t verdictSum = table.counters.renderable + table.counters.rejectBrokenAttachment
      + table.counters.rejectOwnerNotPlayer + table.counters.rejectOwnerMissing
      + table.counters.rejectOwnerGenerationUnknown + table.counters.rejectClassNotAllowed
      + table.counters.rejectNoPath;

  // family 1: attribution
  out.append(L"wearable_attribution\t");
  appendNumber(out, tick);
  appendCount(out, table.counters.attributed);
  appendCount(out, table.counters.unattributedNoOwner);
  appendCount(out, table.counters.unattributedEmptySlot);
  appendCount(out, table.counters.unattributedNotPlayer);
  appendCount(out, table.counters.unattributedGenerationChanged);
  appendCount(out, table.counters.unattributedUnknownGeneration);
  appendCount(out, attributionSum);
  appendCount(out, table.counters.examined);
  appendField(out, attributionSum == table.counters.examined ? "PASS" : "FAIL");
  out.push_back(L'\n');

  // family 2: path source. This is the three-way split the brief asks for: every
  // examined instance resolves by the schema, by the precache, or by neither.
  out.append(L"wearable_path_source\t");
  appendNumber(out, tick);
  appendCount(out, table.counters.pathSchema);
  appendCount(out, table.counters.pathPrecache);
  appendCount(out, table.counters.pathNone);
  appendCount(out, sourceSum);
  appendCount(out, table.counters.examined);
  if (table.counters.examined > 0u) {
    // Printed as an integer permille so the dump never needs a float formatter.
    appendNumber(out, static_cast<std::int64_t>(table.counters.pathSchema * 1000u
                                                / table.counters.examined));
  } else {
    appendNumber(out, 0);
  }
  appendField(out, sourceSum == table.counters.examined ? "PASS" : "FAIL");
  out.push_back(L'\n');

  // -- the template census. Cross-cuts the source split above, so it is its own line:
  // a template row still resolves from the precache and is counted in pathPrecache,
  // and a pathless row whose item published only a template is a different fact from
  // one whose item published nothing.
  out.append(L"wearable_template	");
  appendNumber(out, tick);
  appendCount(out, table.counters.pathSchemaTemplate);
  appendCount(out, table.counters.pathPrecacheTemplate);
  appendCount(out, table.counters.rejectNoPathTemplate);
  appendCount(out, table.counters.pathSchema);
  appendCount(out, table.counters.examined);
  if (table.counters.examined > 0u) {
    appendNumber(out, static_cast<std::int64_t>(table.counters.pathSchemaTemplate * 1000u
                                                / table.counters.examined));
  } else {
    appendNumber(out, 0);
  }
  // The invariant, stated as a check rather than a comment: no drawn row may have
  // resolved through a candidate that still carries a placeholder. Recomputed from
  // the rows here instead of kept as a counter, so it verifies the rows the dump
  // actually printed rather than trusting a number the builder remembered.
  std::size_t drawnTemplates = 0;
  for (const WearableInstanceRow& row : table.rows) {
    if (!row.renderable) continue;
    if (row.model.source == WearablePathSource::Schema && row.model.schemaTemplate) ++drawnTemplates;
    if (row.model.source == WearablePathSource::Precache && row.model.precacheTemplate) ++drawnTemplates;
  }
  appendCount(out, drawnTemplates);
  appendField(out, drawnTemplates == 0u ? "PASS" : "FAIL");
  out.push_back(L'\n');

  // family 3: verdict
  out.append(L"wearable_verdict\t");
  appendNumber(out, tick);
  appendCount(out, table.counters.renderable);
  appendCount(out, table.counters.rejectBrokenAttachment);
  appendCount(out, table.counters.rejectOwnerNotPlayer);
  appendCount(out, table.counters.rejectOwnerMissing);
  appendCount(out, table.counters.rejectOwnerGenerationUnknown);
  appendCount(out, table.counters.rejectClassNotAllowed);
  appendCount(out, table.counters.rejectNoPath);
  appendCount(out, verdictSum);
  appendCount(out, table.counters.examined);
  appendField(out, verdictSum == table.counters.examined ? "PASS" : "FAIL");
  out.push_back(L'\n');

  // -- the draw-path split, which is orthogonal to the three-way source split.
  out.append(L"wearable_draw_path\t");
  appendNumber(out, tick);
  appendCount(out, table.counters.drawPathWorld);
  appendCount(out, table.counters.drawPathViewModel);
  appendCount(out, table.counters.drawPathWorld + table.counters.drawPathViewModel);
  appendCount(out, table.counters.renderable);
  appendField(out, table.counters.drawPathWorld + table.counters.drawPathViewModel
                   == table.counters.renderable ? "PASS" : "FAIL");
  out.push_back(L'\n');

  // -- the shape of the answer itself.
  out.append(L"wearable_table\t");
  appendNumber(out, tick);
  appendNumber(out, table.counters.resolvedTick);
  appendNumber(out, table.counters.stale);
  appendField(out, table.armed ? "armed" : "not-armed");
  appendField(out, table.hasSnapshot ? "snapshot" : "no-snapshot");
  appendField(out, table.schemaLoaded ? "schema" : "no-schema");
  appendField(out, table.assetRootValid ? "assets" : "no-assets");
  appendCount(out, table.rows.size());
  appendField(out, table.counters.balanced() ? "BALANCED" : "UNBALANCED");
  out.push_back(L'\n');

  if (!table.assetRootDescribe.empty()) {
    out.append(L"wearable_assets\t");
    appendNumber(out, tick);
    appendField(out, table.assetRootDescribe);
    out.append(L"\n");
  }
  if (!table.schemaError.empty()) {
    out.append(L"wearable_schema_error\t");
    appendNumber(out, tick);
    appendField(out, table.schemaError);
    out.append(L"\n");
  }

  // -- a server class whose name contains Wearable but which is NOT in the table.
  // Without this line an unconsidered class would only ever be invisible.
  for (const std::string& name : table.unlistedWearableNames) {
    out.append(L"wearable_unlisted\t");
    appendNumber(out, tick);
    appendField(out, name);
    out.append(L"\n");
  }

  // -- per-class counts of examined rows, so a class that shows up in the census
  // but never as an instance is visible.
  for (const auto& entry : table.counters.classRows) {
    out.append(L"wearable_class_row\t");
    appendNumber(out, tick);
    appendField(out, entry.first);
    appendCount(out, entry.second);
    out.push_back(L'\n');
  }

  return out;
}

}  // namespace tf2::native










