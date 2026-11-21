#pragma once

#include "demo_header.h"
#include "item_schema.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace tf2::native {

// ===========================================================================
// task-40: WEARABLE ATTRIBUTION -- CLASS TABLE, OWNER, PATH, INSTANCES
//
// WHY THIS EXISTS. The task-39 gate card (docs/WEARABLE-SCAN-2026-11-21.md)
// answered its three questions with one string test each and one path route:
//   className contains "Wearable"     -- cannot tell a cosmetic from a base class
//   owner class contains "TFPlayer"   -- also matches the player-resource class
//   m_nModelIndex -> modelprecache    -- answers 30 of 332 instances
// The other 302 carry an m_nModelIndex sentinel (4294967280, 4294967292,
// 4294967294, 4294967278 -- genuinely outside the range modelprecache indexes)
// and the model those rows name exists only in the item schema. That is why the
// path source here is SCHEMA FIRST and precache second: the order the handover
// assumed is backwards on this data, and docs/WEARABLE-ATTRIBUTION-2026-11-21.md
// carries the numbers instead of the assumption.
//
// WHAT THIS IS NOT. It does not draw. It builds, per tick, the table task-41
// consumes: which class is a cosmetic, who owns the instance, which model path
// answers it and WHICH SOURCE answered, and the verdict for that instance.
// Nothing here touches the renderer, and a row with no path is counted and
// skipped -- never drawn with another model, never filled in with a placeholder.
//
// THE IDENTITY RULE. An entity index is not an identity: Source reuses slots and
// the decoder cannot see the reuse in the retained state, because the wire
// carries a 10-bit serial on Enter and readPacketEntities throws it away
// (native/src/demo_header_entity.cpp:348). So every row carries
// (entityIndex, generation), and the generation is the one that was LIVE AT THE
// TICK QUERIED, not the one the slot carries at the end of the recording. That is
// what WearableSlotCapture is for: it records the two places the per-slot
// lifetime counter advances, so a tick inside the recording can be answered with
// the generation it had then. When that capture is absent the generation at the
// tick is UNKNOWN, and a row whose generation is unknown is refuse-to-attribute
// rather than guess -- crediting an instance to an identity taken from an index
// alone is exactly the failure this module exists to make impossible. The
// counters make that a reading instead of a promise.
// ===========================================================================

// ---------------------------------------------------------------- class table
// D1. Every server class whose name contains "Wearable" in this build oracle
// (evidence/animation-availability/oracle-all-classes.txt -- exactly nine, ids
// 35 and 336..343). This table is the RULE. The string test it replaces survives
// only inside the pinned --wearable-scan dump, whose bytes the gate card holds.
enum class WearableClassVerdict : std::uint8_t {
  Allowed,   // a cosmetic this project may draw
  Excluded,  // considered and turned down: counted apart, never silently dropped
};

// WHICH DRAW PATH a class belongs to. A CTFWearableVM is attached to the same
// player's CTFViewModel rather than to the world player model: measured at stv
// tick 49444, entity 429 (CTFWearableVM) has ownerIndex 10 (CTFPlayer) while its
// moveParent is 397, and entity 397's class id is 320 = CTFViewModel
// (evidence/animation-availability/oracle-all-classes.txt:46372). Handing those
// rows to a world-model drawer would put a first-person gun model on the
// player's head, so they are admitted to the table and routed to the other path
// instead of being filtered out by a name test that cannot tell the two apart.
enum class WearableDrawPath : std::uint8_t {
  World,
  ViewModel,
};

struct WearableClassRule {
  const char* name;
  WearableClassVerdict verdict;
  WearableDrawPath path;
  // Why the row is in or out. Printed by the dump, so the decision is readable
  // next to the class census instead of living only in a commit message.
  const char* because;
};

// nullptr when the name is not in the table at all. A caller must treat that as
// "not a wearable this module knows" -- a different statement from Excluded,
// which means the class WAS considered and turned down.
const WearableClassRule* findWearableClassRule(const std::string& className);
// The whole table in table order, for the dump that carries D1 as evidence.
const std::vector<WearableClassRule>& wearableClassRules();
const char* wearableClassVerdictName(WearableClassVerdict value);
const char* wearableDrawPathName(WearableDrawPath value);

// ------------------------------------------------------------ slot lifetimes
// The per-slot lifetime counter (DemoNetworkSummary::projectileTrackSlotGeneration)
// advances in exactly two places: the Delete branch and the Enter branch of
// readPacketEntities (native/src/demo_header_entity.cpp:331-336 and :381-387).
// Both call the recorder below, and it is the only thing that fills this capture.
//
// Off unless a consumer arms it: DemoNetworkSummary::wearableSlotCapture stays
// null on every existing path, so an unarmed scan pays one null check per
// Enter/Delete and records nothing at all.
struct WearableSlotCaptureEntry {
  std::int32_t tick = 0;
  std::uint32_t slot = 0;
  std::uint32_t generation = 0;
};

struct WearableSlotCapture {
  // Append order is scan order and tick never decreases, which is what lets
  // generationAt binary-search the tick and then walk back to the slot.
  std::vector<WearableSlotCaptureEntry> entries;

  void record(std::int32_t tick, std::uint32_t slot, std::uint32_t generation);
  // Generation `slot` had at `tick`: the last one recorded at or before it.
  // `*known` is false when the capture cannot answer at all (nothing recorded, or
  // the tick precedes the first recorded advance) -- a different statement from
  // "generation 0", and a different statement from "no such slot".
  std::uint32_t generationAt(std::int32_t tick, std::uint32_t slot, bool* known) const;
  // The tick of the slot's last advance at or before `tick`, given the generation
  // that advance produced. This is the tick the CURRENT lifetime of that slot
  // began, which is what anchors the attachment test below: a wearable's owner
  // handle was minted when the wearable entered, so the owner slot's lifetime is
  // only certifiable if it has not advanced since.
  std::int32_t advanceTickAt(std::int32_t tick, std::uint32_t slot, bool* known) const;
  // The last generation recorded for the slot, i.e. its final lifetime. Compare
  // it with generationAt to ask whether the slot was recycled AFTER the tick: a
  // difference means whatever occupied the slot at `tick` is gone by the end of
  // the recording, so an attachment that pointed at it broke.
  std::uint32_t latestGeneration(std::uint32_t slot, bool* known) const;
};

// Called from the two places the counter advances. A null capture is the normal
// case, which is why this is a free function rather than a member: the decoder
// takes a dependency on this declaration only.
void recordWearableSlotGeneration(WearableSlotCapture* capture, std::int32_t tick,
                                  std::uint32_t slot, std::uint32_t generation);

// -------------------------------------------------------------- attribution
// D2. Why (slot, generation) and not the handle. The wire handle packs an index
// and a 10-bit serial, and the serial is decoded and discarded
// (native/src/demo_header_entity.cpp:348, native/include/entity_model.h:103-108),
// so the serial inside m_hOwnerEntity cannot be compared with anything held on
// our side. What CAN be compared is the owner slot lifetime counter, captured at
// the tick of interest. A handover that recycles the owner slot therefore shows
// up as a generation difference even though the raw handle index is unchanged --
// the case the handover calls out at docs/HANDOVER-2026-11-21.md:311 -- and the
// row is refused instead of being re-attributed by index.
enum class WearableAttributionStatus : std::uint8_t {
  Attributed,                     // owner slot held the same lifetime at the tick
  UnattributedNoOwner,            // no usable owner handle at all
  UnattributedEmptySlot,          // owner handle names a slot with no class here
  UnattributedNotPlayer,          // owner slot resolves, but not to a player class
  UnattributedGenerationChanged,  // the owner slot changed hands during the recording
  UnattributedUnknownGeneration,  // capture could not answer: refuse, never guess
};

struct WearableAttribution {
  WearableAttributionStatus status = WearableAttributionStatus::UnattributedNoOwner;
  std::int32_t ownerIndex = -1;
  std::uint32_t ownerGeneration = 0;
  bool ownerGenerationKnown = false;
  // The generation the owner slot carried at the END of the recording. When it
  // differs from ownerGeneration the slot was recycled after the tick, which is
  // not a reason to refuse this row -- the row is about the tick -- but it does
  // mean a later tick cannot reuse this attribution.
  std::uint32_t ownerGenerationAtEnd = 0;
  bool ownerGenerationAtEndKnown = false;
  // Handles are printed raw beside their interpretation, so a reader can check
  // the mask rather than trust it.
  bool hasOwnerHandle = false;
  std::uint32_t ownerHandle = 0;
  const char* ownerClassName = nullptr;
};

// m_hMoveParent and m_iParentAttachment, and whether the parent is the owner.
// The gate card left the parent class open: on 33 of 35 measured rows the parent
// index equals the owner index, and on the 2 CTFWearableVM rows it does not. The
// answer, measured here, is that those 2 point at the same player CTFViewModel
// (stv 49444: wearable 429, owner 10, parent 397, and 397 is class 320). So a
// parent that differs from the owner is normal on the view-model path and must
// be followed, not treated as corruption.
struct WearableParent {
  bool hasMoveParentHandle = false;
  std::uint32_t moveParentHandle = 0;
  std::int32_t moveParentIndex = -1;
  std::int32_t parentAttachment = 0;
  bool moveParentMatchesOwner = false;
  // Class of the moveParent slot in the same snapshot; nullptr when the slot is
  // empty or the handle was unusable.
  const char* moveParentClassName = nullptr;
  // The parent gets the SAME lifetime discipline as the owner: a moveParent whose
  // slot changed hands during the recording certifies nothing, so it is reported
  // apart instead of being treated as a stable link.
  std::uint32_t moveParentGeneration = 0;
  bool moveParentGenerationKnown = false;
  std::uint32_t moveParentGenerationAtEnd = 0;
  bool moveParentGenerationAtEndKnown = false;
};

// ------------------------------------------------------------------- paths
// D3. THE INVERSION. The handover puts modelprecache first and the schema second
// (docs/HANDOVER-2026-11-21.md:312). The measurement is the other way round:
//
//   30 of 332 instances resolve from the precache (9.0 %), 302 from the schema.
//
// Those 302 are not a quirk of one file. They carry m_nModelIndex sentinels --
// 4294967280, 4294967292, 4294967294, 4294967278 -- none of which modelprecache
// can index, while their m_iItemDefinitionIndex names a real cosmetic (30390 The
// Spook Specs at items_game.txt:80145, 30985 at :93219, 31133 at :98402, 828
// Archimedes at :51047). The 30 that DO resolve from the precache are MvM
// spellbook loot, and those are a different schema key (extra_wearable at
// items_game.txt:53127-53128) that ItemSchema does not read at all -- which is
// exactly why they only answer from the precache. So the schema is the MAIN route
// and the precache the exception, and this module is built that way. The source is
// recorded per instance so a reader never has to re-derive which route answered.
//
// One correction to that shape, measured rather than assumed: a large minority of the
// schema's answers are FAMILY TEMPLATES carrying a "%s" placeholder (15 of 36 rows on
// stv, 14 of 34 on bagel), and a template names no file. Those rows are not
// schema-resolved. They fall through to the precache and, failing that, come out
// NoPath and are counted under pathSchemaTemplate -- so the three-way split reports
// what can actually be drawn, and the templates sit beside it as a named reading
// instead of inflating the schema share with paths no loader could ever open.
enum class WearablePathSource : std::uint8_t {
  None,
  Schema,    // items_game model, selected here
  Precache,  // m_nModelIndex or m_iWorldModelIndex -> modelprecache
};

// WHICH schema model won. model_player_per_class is a per-class map whose only
// keys measured across items_game.txt are the nine class basenames plus a
// "basename" fallback key -- "basename" is NOT a class and must never be matched
// as one. There are zero team-prefixed keys in those blocks; the team dimension
// lives in visuals.styles.N.skin_red / skin_blu, which is a different route this
// module does not take, because the instance does not name a style.
enum class WearableSchemaKey : std::uint8_t {
  None,
  ModelPlayer,
  ModelWorld,
  ModelPlayerPerClass,
};

struct WearablePath {
  WearablePathSource source = WearablePathSource::None;
  WearableSchemaKey schemaKey = WearableSchemaKey::None;
  std::string path;
  // The class name a per-class lookup used, when one was used, so the dump shows
  // whether a per-class model was chosen and for whom.
  std::string schemaClass;
  // The raw readings, kept even when they cannot resolve: a sentinel IS the
  // evidence for the inversion, so a failed row must still report what it carried.
  bool hasModelIndex = false;
  std::int32_t modelIndex = 0;
  bool modelIndexInPrecacheRange = false;
  bool hasWorldModelIndex = false;
  std::int32_t worldModelIndex = 0;
  bool hasItemDefinitionIndex = false;
  std::int32_t itemDefinitionIndex = 0;
  // The schema knows the item but publishes no model for it: a different verdict
  // from never having heard of the item, and both must be countable.
  bool schemaHasEntry = false;
  // BOTH routes are evaluated on every row and both answers are kept, whatever the
  // precedence decided. That is what makes the inversion a measurement instead of
  // a policy: a reader can see how often the precache answers at all, and how
  // often the two routes name different files. Dropping the losing candidate would
  // hide exactly the number this module exists to report.
  std::string precacheCandidate;
  std::string schemaCandidate;
  bool routesDisagree = false;
  // A schema answer that still carries a printf placeholder ("%s") is a FAMILY
  // TEMPLATE, not a model. items_game.txt publishes per-class families that way --
  // "models/player/items/all_class/ring_%s.mdl" (item 5075 at :221115),
  // "models/player/items/%s/%s_halloween.mdl", ".../all_domination_%s.mdl" -- and
  // expects the class token to be substituted at load time. 5574 lines of the file
  // contain a '%', so this is the common case inside model_player_per_class, not an
  // oddity: 15 of 36 instances on stv and 15 of 34 on bagel carry one.
  //
  // Substituting the token is a rendering decision and belongs to task-41. What this
  // table must not do is hand a loader a path with a '%s' in it and count the row as
  // resolved, so a template is kept as a READING and never wins a route: a row whose
  // only schema answer is a template falls through to the precache, and if that
  // cannot answer either the row is NoPath. The raw template stays in schemaCandidate
  // so that column keeps its one meaning -- what the schema answered -- and this flag
  // says whether that answer is usable.
  bool schemaTemplate = false;
  // The same test applied to the precache answer. modelprecache strings are literal
  // file names, so this is expected to stay false everywhere; it is measured rather
  // than assumed, because the day one contains a '%' the precedence must not silently
  // prefer it.
  bool precacheTemplate = false;
};

// ---------------------------------------------------------------- rejection
// D4. Why a considered row did not become a drawn instance. Every reason is a
// distinct counter, because "we dropped 17 rows" is not a diagnosis and "we drew
// 320 of 332" is only trustworthy if the other 12 are named.
enum class WearableRejectReason : std::uint8_t {
  None,
  BrokenAttachment,           // owner slot held a different lifetime at the tick
  OwnerNotPlayer,             // owner slot resolves to a non-player class
  OwnerMissing,               // no owner handle, or the handle names an empty slot
  OwnerGenerationUnknown,     // capture could not answer: refuse, never guess
  ClassNotAllowed,            // in the class table but turned down (D1 Excluded)
  NoPath,                     // neither route named a USABLE file (see schemaTemplate)
};
// NOTE there is deliberately no ViewModelPath reason. A view-model cosmetic is
// not a broken instance: it is a complete instance on a different draw path. So
// `renderable` below says the instance resolved, and `path` says which pass may
// draw it -- a consumer that can only draw world models filters on both and reads
// the draw-path counters for how many it left for the other pass. Folding that
// into the reject list would report a working instance as damaged.

// ------------------------------------------------------------------- rows
// One instance as it stood at the tick. Rebuilt every tick -- identity,
// generation, owner and path all move between ticks, so caching any of them
// across ticks would be caching the thing that changes.
struct WearableInstanceRow {
  std::int32_t tick = 0;
  // WRAPPER: the tick the snapshot actually came from. Equal to tick on a live
  // reading, older when the query fell back to an archived checkpoint. A row
  // whose resolvedTick differs is real data about an older world, and a consumer
  // that ignores this will place a stale hat on a live player.
  std::int32_t resolvedTick = 0;
  std::int32_t entityIndex = -1;
  std::uint32_t generation = 0;
  bool generationKnown = false;
  std::string className;
  const WearableClassRule* classRule = nullptr;
  WearableDrawPath path = WearableDrawPath::World;
  WearableAttribution attribution;
  WearableParent parent;
  WearablePath model;
  // The verdict this row carries into the table. Renderable means: allowed class,
  // attributed owner, resolved path, on the world path. Everything else is counted
  // under reject and never drawn.
  bool renderable = false;
  WearableRejectReason reject = WearableRejectReason::None;
};

// ------------------------------------------------------------------ counters
// Three families, all of which must balance against the number of rows examined.
// Each is checked in the dump itself, so a broken counter is visible in the
// output rather than only in a test.
struct WearableTableCounters {
  std::int32_t tick = 0;
  std::int32_t resolvedTick = 0;
  bool resolved = false;
  std::int32_t stale = 0;

  // -- family 1: attribution. attributed + every unattributed reason = examined.
  std::size_t examined = 0;
  std::size_t attributed = 0;
  std::size_t unattributedNoOwner = 0;
  std::size_t unattributedEmptySlot = 0;
  std::size_t unattributedNotPlayer = 0;
  std::size_t unattributedGenerationChanged = 0;
  std::size_t unattributedUnknownGeneration = 0;

  // -- family 2: path source. schema + precache + none = examined. These three
  // are the only values that may sum, and the builder asserts it.
  std::size_t pathSchema = 0;
  std::size_t pathPrecache = 0;
  std::size_t pathNone = 0;

  // -- census: rows whose schema answer was an unsubstituted family template, and
  // rows whose precache answer was one. NOT part of the three-way sum above: this
  // cuts across it, because a template row can still resolve from the precache and
  // is then counted in pathPrecache. It exists so the drop in pathSchema has a named
  // cause in the same dump rather than an explanation in a report.
  std::size_t pathSchemaTemplate = 0;
  std::size_t pathPrecacheTemplate = 0;

  // -- family 3: rejects, by reason. Sums with renderable to examined.
  // -- draw path. Orthogonal to the three-way source counters above, so it is a
  // separate pair and does not disturb their sum. A row counted here is a
  // resolved instance; ViewModel rows are the ones the OTHER pass owns.
  std::size_t drawPathWorld = 0;
  std::size_t drawPathViewModel = 0;

  std::size_t renderable = 0;
  std::size_t rejectBrokenAttachment = 0;
  std::size_t rejectOwnerNotPlayer = 0;
  std::size_t rejectOwnerMissing = 0;
  std::size_t rejectOwnerGenerationUnknown = 0;
  std::size_t rejectClassNotAllowed = 0;
  std::size_t rejectNoPath = 0;

  // -- census: NoPath rows where the schema DID have an answer and it was only a
  // family template. Same reason as pathSchemaTemplate: a pathless row whose item
  // published something is a different fact from a pathless row whose item published
  // nothing, and only this count tells them apart without re-deriving it.
  std::size_t rejectNoPathTemplate = 0;

  // -- per class census by name, for classes with at least one row. Ordered so
  // the dump is stable across runs.
  std::vector<std::pair<std::string, std::size_t>> classRows;

  // True when each family accounts for exactly `examined`. False is a bug in this
  // module, not in the data, and the dump prints it as such.
  bool balanced() const;
};

// ------------------------------------------------------------------- table
struct WearableInstanceTable {
  bool armed = false;
  bool hasSnapshot = false;
  bool schemaLoaded = false;
  bool assetRootValid = false;
  std::string schemaError;
  std::string assetRootDescribe;
  WearableTableCounters counters;
  std::vector<WearableInstanceRow> rows;
  // Server classes whose name contains "Wearable" but which are NOT in the table.
  // The name test here exists ONLY to notice a class the table has not considered:
  // a new one must show up as a reading rather than be silently ignored, which is
  // the same discipline the reject counters apply to individual rows.
  std::vector<std::string> unlistedWearableNames;
};

// ------------------------------------------------------------------- builder
// Rebuilds the whole table from the summary for one tick. Pure: it reads the
// summary and the optional schema and writes nothing back, which is what lets the
// caller run it per frame without the table becoming state.
//
// `schema` may be null: the item schema is an OPTIONAL input (a game asset, not
// part of the repository), and with it absent every row degrades to
// precache-only and the missing rows are counted as NoPath with the reason stated
// in the dump -- never guessed, never substituted.
struct WearableTableRequest {
  std::int32_t tick = 0;
  const ItemSchema* schema = nullptr;
  // What the caller managed to find. Carried here rather than assigned to the
  // returned table afterwards so the table is built once, complete: a caller that
  // rewrites fields after the builder returns is a second place the answer is
  // assembled, and the two can disagree.
  bool assetRootValid = false;
  std::string assetRootDescribe;
  std::string schemaError;
};
WearableInstanceTable buildWearableInstanceTable(const DemoNetworkSummary& summary,
                                                 const WearableTableRequest& request);

// The table as tab-separated dump lines, one row per instance plus the counter
// block. Kept beside the builder so the shape a consumer parses cannot drift from
// the shape the builder produces.
std::wstring formatWearableTableLines(const WearableInstanceTable& table);

}  // namespace tf2::native



