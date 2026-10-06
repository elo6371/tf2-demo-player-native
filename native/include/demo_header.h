#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <filesystem>
#include <unordered_map>

namespace tf2::native {

enum class DemoRecordingType { Unknown, SourceTv, PovHeuristic };

struct DemoIndexEntry {
  std::int32_t tick = 0;
  std::uint8_t command = 0;
  std::size_t offset = 0;
  std::size_t payloadOffset = 0;
  std::size_t payloadSize = 0;
};

struct DemoIndex {
  bool valid = false;
  std::size_t commandCount = 0;
  std::size_t packetCount = 0;
  std::size_t malformedOffset = 0;
  std::string error;
  std::vector<DemoIndexEntry> entries;
};

struct DemoHeader {
  bool valid = false;
  std::string mapName;
  std::string gameDirectory;
  std::string serverName;
  std::string clientName;
  std::int32_t demoProtocol = 0;
  std::int32_t networkProtocol = 0;
  std::int32_t ticks = 0;
  std::int32_t frames = 0;
  float playbackTime = 0.0f;
  DemoRecordingType recordingType = DemoRecordingType::Unknown;
  std::string error;
};

bool parseDemoHeader(const std::vector<std::uint8_t>& bytes, DemoHeader& header);
const char* demoRecordingTypeName(DemoRecordingType type);
bool indexDemoCommands(const std::vector<std::uint8_t>& bytes, const DemoHeader& header, DemoIndex& index);
bool parseDemoHeaderFile(const std::filesystem::path& path, DemoHeader& header);
bool indexDemoFile(const std::filesystem::path& path, const DemoHeader& header, DemoIndex& index);
bool findDemoPacketAtOrBeforeTick(const DemoIndex& index, std::int32_t tick, DemoIndexEntry& entry);
bool readDemoEntryPayload(const std::filesystem::path& path, const DemoIndexEntry& entry, std::vector<std::uint8_t>& payload, std::size_t maxBytes = 128u * 1024u * 1024u);

enum class SendPropType : std::uint8_t { Int = 0, Float = 1, Vector = 2, VectorXY = 3, String = 4, Array = 5, DataTable = 6 };
struct SendPropSchema {
  SendPropType type = SendPropType::Int;
  std::string name;
  std::string ownerTable;
  std::uint16_t flags = 0;
  std::uint8_t bitCount = 0;
  std::uint16_t elementCount = 0;
  SendPropType arrayElementType = SendPropType::Int;
  std::uint16_t arrayElementFlags = 0;
  std::uint8_t arrayElementBitCount = 0;
  std::string referencedTable;
  float lowValue = 0.0f;
  float highValue = 0.0f;
  bool hasFloatRange = false;
};
struct SendTableSchema {
  std::string name;
  bool needsDecoder = false;
  std::vector<SendPropSchema> props;
  std::vector<SendPropSchema> flattenedProps;
};
struct ServerClassSchema {
  std::uint16_t id = 0;
  std::string name;
  std::string dataTable;
};

struct EntityPropertyValue {
  SendPropType type = SendPropType::Int;
  std::int64_t intValue = 0;
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
  std::string stringValue;
};

struct EntityState {
  std::int32_t classId = -1;
  std::unordered_map<std::string, EntityPropertyValue> properties;
};

struct RawSoundMessage {
  std::int32_t tick = 0;
  bool reliable = false;
  std::uint8_t count = 0;
  std::uint16_t payloadBits = 0;
  std::vector<std::uint8_t> payload;
};

struct DecodedSoundEvent {
  std::int32_t tick = 0;
  std::uint16_t soundIndex = 0;
  std::int32_t entityIndex = 0;
  std::uint16_t flags = 0;
  std::uint8_t channel = 0;
  std::uint8_t pitch = 100;
  float volume = 1.0f;
  float delaySeconds = 0.0f;
  float origin[3] = {0.0f, 0.0f, 0.0f};
  bool ambient = false;
  bool sentence = false;
  bool valid = false;
  std::string resourceName;
};

struct TempEntityEvent {
  std::int32_t tick = 0;
  std::string className;
  std::uint32_t classId = 0;
  bool reliable = false;
  bool hasDelay = false;
  std::uint8_t delayRaw = 0;
  std::size_t knownFieldCount = 0;
  bool hasOrigin = false;
  float origin[3] = {0.0f, 0.0f, 0.0f};
  bool hasAngles = false;
  float angles[3] = {0.0f, 0.0f, 0.0f};
  bool hasPlayer = false;
  std::int64_t player = 0;
  bool hasWeaponId = false;
  std::int64_t weaponId = 0;
  bool hasMode = false;
  std::int64_t mode = 0;
  bool hasSeed = false;
  std::int64_t seed = 0;
  bool hasEvent = false;
  std::int64_t event = 0;
  bool hasEffectIndex = false;
  std::int64_t effectIndex = 0;
  bool hasMagnitude = false;
  std::int64_t magnitude = 0;
  bool hasScale = false;
  std::int64_t scale = 0;
  bool hasRadius = false;
  std::int64_t radius = 0;
  bool hasParticleName = false;
  std::string particleName;
  bool hasVelocity = false;
  float velocity[3] = {};
  bool hasModelIndex = false;
  std::int64_t modelIndex = 0;
  bool hasOwner = false;
  std::int64_t owner = 0;
  bool hasLifeTime = false;
  std::int64_t lifeTime = 0;
  std::unordered_map<std::string, EntityPropertyValue> properties;
};

struct ProjectileTimelineEvent {
  std::int32_t tick = 0;
  std::string className;
  bool hasOrigin = false;
  float origin[3] = {0.0f, 0.0f, 0.0f};
  bool hasDirection = false;
  float direction[3] = {0.0f, 0.0f, 0.0f};
  bool hasWeaponId = false;
  std::int64_t weaponId = 0;
  bool hasParticleName = false;
  std::string particleName;
  bool hasMagnitude = false;
  std::int64_t magnitude = 0;
  bool hasScale = false;
  std::int64_t scale = 0;
  bool hasRadius = false;
  std::int64_t radius = 0;
  bool hasVelocity = false;
  float velocity[3] = {};
  bool hasModelIndex = false;
  std::int64_t modelIndex = 0;
  bool hasOwner = false;
  std::int64_t owner = 0;
  bool hasLifeTime = false;
  std::int64_t lifeTime = 0;
};

struct AssetReference {
  std::uint16_t entityIndex = 0;
  std::int32_t classId = -1;
  std::string className;
  bool hasModelPath = false;
  std::string modelPath;
  bool hasWeaponClass = false;
  std::string weaponClass;
  bool hasModelIndex = false;
  std::int64_t modelIndex = 0;
  bool hasWeapon = false;
  std::int64_t weapon = 0;
  bool hasItemDefIndex = false;
  std::int64_t itemDefIndex = 0;
  bool hasPaintKit = false;
  std::int64_t paintKit = 0;
  bool hasSkin = false;
  std::int64_t skin = 0;
  bool hasQuality = false;
  std::int64_t quality = 0;
};

// One property a packet actually wrote, addressed by its position in the
// class's flattened SendTable. Storing the index instead of the composed
// "owner.name" key keeps the record at 4 bytes + the value and makes it
// allocation-free; replay rebuilds the key from the table.
struct EntityPropChange {
  std::uint32_t propIndex = 0;
  EntityPropertyValue value;
};

struct EntityHistoryEvent {
  std::int32_t tick = 0;
  std::uint32_t packetOrdinal = 0;
  std::uint16_t entityIndex = 0;
  std::int32_t classId = -1;
  bool removed = false;
  // Enter events carry the complete post-update state (instance baseline plus
  // the wire properties). Preserve events carry only `changes`.
  bool fullState = false;
  EntityState state;
  // Properties touched by a Preserve update. Before this existed every event
  // deep-copied the whole EntityState; on koth_bagel_rc13 that is 1.39M copies
  // of a ~50-entry unordered_map and dominated the scan.
  std::vector<EntityPropChange> changes;
};

struct EntityHistoryCheckpoint {
  std::int32_t tick = 0;
  std::uint32_t packetOrdinal = 0;
  std::vector<std::int32_t> classByIndex;
  std::vector<EntityState> states;
};

struct EntityHistoryPacket {
  std::int32_t tick = 0;
  std::int32_t deltaFrom = -1;
  std::uint32_t packetOrdinal = 0;
  std::size_t firstEvent = 0;
  std::size_t eventCount = 0;
  bool isDelta = false;
};

struct EntityHistoryLimits {
  std::size_t maxEvents = 8192;
  std::size_t maxCheckpoints = 8;
  std::size_t checkpointStride = 128;
  std::size_t archiveMax = 96;
};

enum class EntitySnapshotQueryStatus {
  Available,
  Checkpoint,
  NoHistory,
  TickBeforeHistory,
  Gap,
  DeltaBaseMissing,
};

enum class DemoViewSource : std::uint8_t { None = 0, CmdInfo = 1, FixAngle = 2 };

struct DemoViewSample {
  std::int32_t tick = 0;
  float origin[3] = {};
  float angles[3] = {};
  bool hasOrigin = false;
  bool hasAngles = false;
  DemoViewSource source = DemoViewSource::None;
};

enum class DemoRecordingKind { Unknown, SourceTv, Pov };

struct DemoRecordingClassification {
  DemoRecordingKind kind = DemoRecordingKind::Unknown;
  bool headerName = false;
  bool serverInfoHltv = false;
  bool serverInfoReplayBit = false;
  const char* label = "unknown";
};

struct DemoNetworkSummary {
  std::size_t packetsScanned = 0;
  std::size_t malformedPackets = 0;
  std::size_t unknownMessagePackets = 0;
  std::size_t serverInfoCount = 0;
  std::size_t signonStateCount = 0;
  std::size_t classInfoCount = 0;
  std::size_t setViewCount = 0;
  std::uint32_t lastViewEntity = 0;
  std::vector<DemoViewSample> viewSamples;
  std::size_t viewSamplesDropped = 0;
  std::size_t fixAngleDecoded = 0;
  float lastFixAngle[3] = {};
  bool lastFixAngleValid = false;
  bool lastFixAngleRelative = false;
  std::uint32_t serverClassCount = 0;
  std::vector<std::string> serverClassNames;
  std::vector<std::string> serverDataTableNames;
  bool classCreateOnClient = false;
  std::uint32_t lastSignonState = 0;
  std::uint32_t signonSpawnCount = 0;
  std::size_t netTickCount = 0;
  std::int32_t lastNetworkTick = -1;
  std::uint32_t lastNetworkTickRaw = 0;
  bool lastNetworkTickRawValid = false;
  std::size_t printCount = 0;
  std::size_t setConVarCount = 0;
  std::size_t setConVarPairCount = 0;
  std::size_t tempEntitiesCount = 0;
  std::size_t tempEventCount = 0;
  std::size_t tempPayloadBits = 0;
  std::size_t tempEventHeadersDecoded = 0;
  std::size_t tempEventDecodeFailures = 0;
  std::size_t tempPropDecodedCount = 0;
  std::size_t tempClientProjectileCount = 0;
  std::size_t tempParticleEffectCount = 0;
  std::size_t tempExplosionCount = 0;
  std::size_t tempFireBulletsCount = 0;
  std::size_t tempPlayerAnimEventCount = 0;
  std::size_t tempFireBulletsFieldHits = 0;
  std::size_t tempParticleEffectFieldHits = 0;
  std::size_t tempExplosionFieldHits = 0;
  std::size_t tempPlayerAnimEventFieldHits = 0;
  std::vector<TempEntityEvent> tempEntityEvents;
  std::vector<ProjectileTimelineEvent> projectileTimeline;
  std::vector<AssetReference> assetReferences;
  std::size_t assetModelIndexKnown = 0;
  std::size_t assetWeaponKnown = 0;
  std::size_t assetItemDefKnown = 0;
  std::size_t assetPaintKitKnown = 0;
  std::size_t assetSkinKnown = 0;
  std::size_t assetQualityKnown = 0;
  std::size_t assetIdentityUnknown = 0;
  std::size_t assetModelPathKnown = 0;
  std::size_t assetWeaponClassKnown = 0;
  std::size_t soundMessageCount = 0;
  std::size_t soundEventCount = 0;
  std::size_t reliableSoundCount = 0;
  std::size_t soundPayloadBits = 0;
  std::vector<RawSoundMessage> rawSoundMessages;
  std::size_t rawSoundMessagesDropped = 0;
  std::vector<DecodedSoundEvent> decodedSoundEvents;
  std::size_t decodedSoundEventFailures = 0;
  std::size_t decodedSoundResourceMatches = 0;
  std::size_t decodedSoundResourceMisses = 0;
  bool soundDeltaStateValid = false;
  DecodedSoundEvent soundDeltaState{};
  std::size_t voiceInitCount = 0;
  std::size_t voiceDataCount = 0;
  std::size_t voicePayloadBits = 0;
  // Message types the reference parser implements and this decoder used to
  // abandon packets on. Counts are kept so "the decoder was actually reached"
  // is a reading, not an assumption about dead code.
  std::size_t fileMessageCount = 0;
  std::size_t setPauseCount = 0;
  std::size_t bspDecalCount = 0;
  std::size_t menuCount = 0;
  std::size_t cmdKeyValuesCount = 0;
  bool setPauseState = false;
  std::size_t updateStringTableCount = 0;
  std::size_t packetEntitiesCount = 0;
  std::size_t packetEntityUpdates = 0;
  std::size_t packetEntityPayloadBits = 0;
  std::size_t packetEntityHeaderUpdates = 0;
  std::size_t packetEntityEnterCount = 0;
  std::size_t packetEntityPreserveCount = 0;
  std::size_t packetEntityLeaveCount = 0;
  std::size_t packetEntityDeleteCount = 0;
  std::size_t packetEntityDeltaCount = 0;
  std::size_t packetEntityDeltaBaseUnavailableCount = 0;
  std::int32_t firstPacketEntitiesUnavailableTick = -1;
  std::int32_t firstPacketEntitiesUnavailableFrom = -1;
  std::size_t packetEntityDecodeFailures = 0;
  std::size_t entityPropMissingTableFailures = 0;
  std::size_t entityPropIndexFailures = 0;
  std::size_t entityPropValueFailures = 0;
  std::size_t entityUnknownStateFailures = 0;
  std::size_t entityUpdateHeaderFailures = 0;
  std::int32_t firstEntityUnknownStateTick = -1;
  std::int32_t firstEntityUnknownStateEntity = -1;
  std::int32_t firstEntityUnknownStateUpdate = -1;
  std::int32_t firstEntityUnknownStateMaxEntries = -1;
  std::int32_t firstEntityUnknownStateUpdatedEntries = -1;
  std::int32_t firstEntityUnknownStatePayloadBits = -1;
  std::int32_t firstEntityUnknownStateDiff = -1;
  std::int32_t firstPacketEntitiesTick = -1;
  std::int32_t firstPacketEntitiesMaxEntries = -1;
  std::int32_t firstPacketEntitiesUpdatedEntries = -1;
  std::int32_t firstPacketEntitiesPayloadBits = -1;
  std::int32_t firstPacketEntitiesDelta = -1;
  std::int32_t firstPacketEntitiesDeltaTick = -1;
  std::int32_t firstPacketEntitiesDeltaFrom = -1;
  std::int32_t firstPacketEntitiesEnterTick = -1;
  std::int32_t firstPacketEntitiesPreserveTick = -1;
  std::int64_t firstPacketEntitiesFirstUpdateBit = -1;
  std::int32_t firstPacketEntitiesFirstDiff = -1;
  std::int64_t firstPacketEntitiesMessageBit = -1;
  std::int32_t firstEntityPropFailureClass = -1;
  std::int32_t firstEntityPropFailureIndex = -1;
  std::int32_t firstEntityPropFailureTick = -1;
  std::int32_t firstEntityPropFailureEntity = -1;
  std::string firstEntityPropFailureStage;
  std::string firstEntityPropFailureName;
  std::int32_t firstEntityPropFailureType = -1;
  std::int32_t firstEntityPropFailureFlags = -1;
  std::int32_t firstEntityPropFailureBits = -1;
  std::int32_t firstTempEntityFailureTick = -1;
  std::int32_t firstTempEntityFailureClass = -1;
  std::int32_t firstTempEntityFailurePayloadBits = -1;
  std::string firstTempEntityFailureStage;
  std::string firstTempEntityFailureName;
  std::int32_t firstTempEntityFailureType = -1;
  std::int32_t firstTempEntityFailureFlags = -1;
  std::int32_t firstTempEntityFailureBits = -1;
  std::size_t activeEntityCount = 0;
  std::size_t maxActiveEntityCount = 0;
  std::vector<std::int32_t> entityClassByIndex;
  std::vector<EntityState> entityStates;
  std::vector<EntityHistoryEvent> entityHistoryEvents;
  std::vector<EntityHistoryCheckpoint> entityHistoryCheckpoints;
  std::vector<EntityHistoryPacket> entityHistoryPackets;
  std::size_t entityHistoryDroppedPackets = 0;
  std::size_t entityHistoryDeltaBaseMisses = 0;
  bool entityHistoryHasGap = false;
  std::int32_t entityHistoryGapTick = 0;
  std::vector<EntityHistoryCheckpoint> entityHistoryArchive;
  EntityHistoryLimits entityHistoryLimits{};
  bool serverInfoHltv = false;
  bool serverInfoDedicated = false;
  bool serverInfoReplayBit = false;
  std::size_t entityOriginStateCount = 0;
  std::size_t entityHealthStateCount = 0;
  std::size_t entityTeamStateCount = 0;
  std::size_t entityClassStateCount = 0;
  std::size_t entityWeaponStateCount = 0;
  std::size_t entityObserverStateCount = 0;
  std::size_t gameEventListCount = 0;
  std::size_t gameEventDefinitionCount = 0;
  std::size_t gameEventCount = 0;
  std::size_t gameEventPayloadBits = 0;
  std::size_t userMessageCount = 0;
  std::size_t userMessagePayloadBits = 0;
  std::size_t prefetchCount = 0;
  std::size_t stringCommandCount = 0;
  std::size_t fixAngleCount = 0;
  std::size_t getCvarValueCount = 0;
  std::size_t entityMessageCount = 0;
  std::size_t entityMessagePayloadBits = 0;
  std::size_t sendTableCount = 0;
  std::size_t sendTablePayloadBits = 0;
  std::size_t dataTablePacketCount = 0;
  std::size_t dataTableDefinitionCount = 0;
  std::size_t dataTablePropCount = 0;
  std::size_t dataTableFlattenedPropCount = 0;
  std::size_t dataTableServerClassCount = 0;
  std::vector<std::string> dataTableNames;
  std::vector<std::string> dataTableServerClassNames;
  std::vector<SendTableSchema> sendTableSchemas;
  std::vector<ServerClassSchema> serverClassSchemas;
  int networkProtocol = 0;
  std::size_t stringTableCount = 0;
  // Largest user-data byte length seen on any string-table entry. The wire
  // field is 14 or 16 bits wide and the local Rust reference imposes NO upper
  // bound (work/_refs_demostf/src/demo/message/stringtable.rs, read_table_entry:
  // read the length, then skip length*8 bits). This decoder used to reject
  // anything over 1024, which turned legal data -- instancebaseline entry 3 of
  // koth_bagel_rc13 is 7669 bytes -- into a malformed packet. Recording the
  // observed maximum makes "the old cap was wrong" a checkable reading.
  std::size_t stringTableUserDataMaxBytes = 0;
  std::vector<std::uint32_t> unknownMessageTypes;
  // Per-type svc_/net_ message histogram over the whole demo. The message loop
  // has to abandon a packet when it meets a type it cannot skip (the length is
  // type-specific), so any type that appears in the histogram with a non-zero
  // count is a type whose decoder MUST exist. This turns "did we implement the
  // whole protocol?" from a guess into a reading: compare the non-zero entries
  // against the handled type list, and the set of types that are decoded but
  // never appear is the set that is untested by this corpus.
  static constexpr std::size_t kMessageTypeHistogramSize = 64;
  std::size_t messageTypeCounts[kMessageTypeHistogramSize] = {};
  std::vector<std::string> stringTableNames;
  std::unordered_map<std::uint16_t, std::string> soundPrecache;
  std::unordered_map<std::uint32_t, std::string> stringTableById;
  std::unordered_map<std::uint32_t, std::uint32_t> stringTableMaxEntries;
  std::size_t soundPrecacheDecodeFailures = 0;
  std::uint32_t soundPrecacheTableId = 0xffffffffu;
  std::uint32_t soundPrecacheMaxEntries = 0;
  std::uint32_t soundPrecacheFixedBits = 0;
  bool soundPrecacheCompressed = false;
  std::size_t soundPrecacheUpdateCount = 0;
  std::uint32_t lastStringTableUpdateId = 0xffffffffu;
  std::uint32_t lastStringTableUpdateEntries = 0;
  std::uint32_t lastStringTableUpdateBits = 0;
  std::vector<std::uint32_t> stringTableUpdateIds;
  std::vector<std::uint32_t> stringTableUpdateEntries;
  std::unordered_map<std::uint16_t, std::vector<std::uint8_t>> instanceBaselines;
  std::size_t instanceBaselineEntryCount = 0;
  std::size_t instanceBaselineDecodeFailures = 0;
  std::size_t instanceBaselineCompressedCount = 0;
  std::uint32_t instanceBaselineMagic = 0;
  std::uint32_t instanceBaselineCompressedBytes = 0;
  std::uint32_t instanceBaselineDecompressedBytes = 0;
  std::size_t instanceBaselineAppliedCount = 0;
  std::size_t instanceBaselineLookupMisses = 0;
  std::int32_t firstInstanceBaselineLookupClassId = -1;
  std::size_t instanceBaselineTableClassHits = 0;
  std::size_t instanceBaselineApplyFailures = 0;
  std::int32_t firstInstanceBaselineClassId = -1;
  bool firstInstanceBaselineLookupHasTable = false;
  bool firstInstanceBaselineLookupHasEntry = false;
  bool firstInstanceBaselineLookupApplied = false;
  std::size_t instanceBaselineClassTextCount = 0;
  std::size_t instanceBaselineClassParseFailures = 0;
  bool sourceTv = false;
  std::string serverName;
  std::string serverMap;
};
bool scanKnownDemoMessages(const std::filesystem::path& path, const DemoIndex& index, DemoNetworkSummary& summary);
bool buildAssetReferenceList(DemoNetworkSummary& summary, std::vector<AssetReference>& references);
bool findEntitySnapshotAtOrBeforeTick(const DemoNetworkSummary& summary, std::int32_t tick,
                                      std::vector<EntityState>& states);
EntitySnapshotQueryStatus queryEntitySnapshotAtOrBeforeTick(
    const DemoNetworkSummary& summary, std::int32_t tick, std::vector<EntityState>& states);
void appendEntityHistoryPacket(DemoNetworkSummary& summary, std::int32_t tick, bool isDelta,
                               std::int32_t deltaFrom, std::vector<EntityHistoryEvent> events);
// Outcome of decoding one demo message stream (the payload of a dem_signon or
// dem_packet entry: a sequence of 6-bit-typed net messages).
struct DemoMessageStreamResult {
  std::size_t messagesDecoded = 0;  // messages whose decoder returned true
  std::size_t bitsConsumed = 0;
  bool packetValid = true;
  bool decodedAny = false;
  bool hitUnknownType = false;
  std::uint32_t unknownType = 0;
  std::int32_t lastNetworkTick = -1;
};

// Decodes a raw message stream exactly the way scanKnownDemoMessages does.
// Exists so tools/entity_message_fixture_probe.cpp can drive the decoders with
// synthetic wire bytes -- including message layouts that no local demo carries
// in isolation -- without fabricating a whole .dem file. `payloadBits` bounds
// the stream; bits inside the last retained byte stay readable, matching the
// byte-aligned entry lengths on the real path.
bool decodeDemoMessageStream(const std::vector<std::uint8_t>& payload,
                             std::size_t payloadBits,
                             std::int32_t entryTick,
                             std::int32_t initialNetworkTick,
                             DemoNetworkSummary& summary,
                             DemoMessageStreamResult& result);
bool findTempEntityEventsInTickRange(const DemoNetworkSummary& summary, std::int32_t firstTick,
                                     std::int32_t lastTick, std::vector<TempEntityEvent>& events);
bool parseDemoCmdInfo(const std::uint8_t* bytes, std::size_t size, DemoViewSample& sample);
float demoBitAngleToDegrees(std::uint32_t raw, int width);
void demoViewForward(float pitchDegrees, float yawDegrees, float out[3]);
bool findObserverViewAtOrBeforeTick(const DemoNetworkSummary& summary, std::int32_t tick, DemoViewSample& sample);
DemoRecordingClassification classifyDemoRecording(const DemoHeader& header, const DemoNetworkSummary& summary);
void applyTempEntityFields(TempEntityEvent& event);
bool projectileFromTempEntity(const TempEntityEvent& event, ProjectileTimelineEvent& timeline);

} // namespace tf2::native
