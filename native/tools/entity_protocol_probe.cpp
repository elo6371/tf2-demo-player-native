// entity_protocol_probe - full-demo entity protocol decoder census.
//
// WHY THIS EXISTS
//   `demo_open_probe` only reports header/index/packet counts. The P0 task in
//   docs/TASKS-2026-10-06.md needs a reading for entityUnknownStateFailures (the
//   "7320 failures" number) plus enough context to explain each failure. This
//   probe prints one key=value line per metric so it can be diffed and gated.
//
// WHAT IT ADDS OVER THE EXISTING PROBES
//   - message_type_histogram: how many times each 6-bit svc_/net_ type appeared.
//     Any type with a non-zero count MUST have a decoder, because a packet is
//     abandoned at the first type whose length the decoder cannot skip. This
//     makes "is the protocol fully covered?" a reading instead of a guess.
//   - first_* failure coordinates, so a regression points at one packet instead
//     of a count.
//
// USAGE
//   entity_protocol_probe <demo.dem>
//   entity_protocol_probe --histogram <demo.dem>    # only the type histogram
//   entity_protocol_probe --summary <demo.dem>      # only the counters
//
// EXIT CODES
//   0 = scanned (including a demo whose index stopped at a truncated tail)
//   2 = usage error, unreadable file, or an index failure that is NOT a
//       truncated tail (index_state=invalid -- the indexer walked into
//       something it does not understand)
#include "demo_header.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <system_error>
#include <vector>

namespace {

using tf2::native::DemoHeader;
using tf2::native::DemoIndex;
using tf2::native::DemoNetworkSummary;

// Source's SVC_MESSAGES / NET_Messages enumerations. Reference for the numbers:
//   - Valve source-sdk-2013, src/engine/netmessages.h (SVC_MESSAGES, NET_Messages)
//   - local Rust reference: work/_refs_demostf/src/demo/message/mod.rs (MessageType)
// Both agree on every value used here. Types 16/20/22/33 exist in the enum but
// are NOT implemented by the Rust reference either, which is why the histogram
// matters: if their count is 0 in the corpus, not implementing them is provably
// harmless rather than merely convenient.
const char* messageTypeName(std::uint32_t type) {
  switch (type) {
    case 0: return "net_NOP";
    case 1: return "net_Disconnect";
    case 2: return "net_File";
    case 3: return "net_Tick";
    case 4: return "net_StringCmd";
    case 5: return "net_SetConVar";
    case 6: return "net_SignonState";
    case 7: return "svc_Print";
    case 8: return "svc_ServerInfo";
    case 9: return "svc_SendTable";
    case 10: return "svc_ClassInfo";
    case 11: return "svc_SetPause";
    case 12: return "svc_CreateStringTable";
    case 13: return "svc_UpdateStringTable";
    case 14: return "svc_VoiceInit";
    case 15: return "svc_VoiceData";
    case 16: return "svc_HLTV";
    case 17: return "svc_Sounds";
    case 18: return "svc_SetView";
    case 19: return "svc_FixAngle";
    case 20: return "svc_CrosshairAngle";
    case 21: return "svc_BSPDecal";
    case 22: return "svc_SplitScreen";
    case 23: return "svc_UserMessage";
    case 24: return "svc_EntityMessage";
    case 25: return "svc_GameEvent";
    case 26: return "svc_PacketEntities";
    case 27: return "svc_TempEntities";
    case 28: return "svc_Prefetch";
    case 29: return "svc_Menu";
    case 30: return "svc_GameEventList";
    case 31: return "svc_GetCvarValue";
    case 32: return "svc_CmdKeyValues";
    case 33: return "svc_SetPauseTimed";
    default: return "unknown";
  }
}

const char* recordingName(tf2::native::DemoRecordingType type) {
  return tf2::native::demoRecordingTypeName(type);
}

// The header alone cannot tell SourceTV from POV: TF2's SourceTV demos are not
// required to spell "SourceTV" in servername, and POV demos are not required to
// fill in clientname. The authoritative in-stream marker is svc_ServerInfo's
// m_bIsHLTV bit, which classifyDemoRecording() also consults. The probe used to
// print only the header verdict, so the corpus census could not see whether any
// demo in the corpus actually declared itself HLTV. Print both.
const char* recordingKindName(tf2::native::DemoRecordingKind kind) {
  switch (kind) {
    case tf2::native::DemoRecordingKind::SourceTv: return "SourceTV";
    case tf2::native::DemoRecordingKind::Pov: return "POV";
    case tf2::native::DemoRecordingKind::Unknown: return "unknown";
  }
  return "unknown";
}

// How far the indexer got. A plain const char* would invite `state == "invalid"`,
// which compares pointers rather than text and happens to work only if the linker
// merges the two literals -- /W4 flags it as C4130 for good reason.
enum class IndexState { Ok, TruncatedTail, Invalid };

const char* indexStateName(IndexState state) {
  switch (state) {
    case IndexState::Ok: return "ok";
    case IndexState::TruncatedTail: return "truncated_tail";
    case IndexState::Invalid: return "invalid";
  }
  return "invalid";
}

void printHistogram(const DemoNetworkSummary& summary) {
  std::cout << "message_type_histogram:";
  bool any = false;
  for (std::size_t type = 0; type < DemoNetworkSummary::kMessageTypeHistogramSize; ++type) {
    if (summary.messageTypeCounts[type] == 0) continue;
    any = true;
    std::cout << ' ' << type << '(' << messageTypeName(static_cast<std::uint32_t>(type)) << ')'
              << '=' << summary.messageTypeCounts[type];
  }
  if (!any) std::cout << " <none>";
  std::cout << "\n";

  // The handled set is what scanKnownDemoMessages() dispatches. Keep it next to
  // the histogram so a type that shows up without a decoder is obvious.
  // Every 6-bit message type the decoder in src/demo_header.cpp has an arm for.
  // If a type shows a non-zero count in the histogram and is missing here, the
  // message loop reached the unknown-type arm and abandoned the packet, which
  // also discards any svc_PacketEntities sharing that packet.
  static const std::uint32_t kHandled[] = {0, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
                                          17, 18, 19, 21, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32};
  std::cout << "message_types_seen_without_decoder:";
  bool gap = false;
  for (std::size_t type = 0; type < DemoNetworkSummary::kMessageTypeHistogramSize; ++type) {
    if (summary.messageTypeCounts[type] == 0) continue;
    const bool handled = std::find(std::begin(kHandled), std::end(kHandled),
                                   static_cast<std::uint32_t>(type)) != std::end(kHandled);
    if (!handled) {
      gap = true;
      std::cout << ' ' << type << '(' << messageTypeName(static_cast<std::uint32_t>(type)) << ')';
    }
  }
  if (!gap) std::cout << " <none>";
  std::cout << "\n";
}

void printSummary(const DemoNetworkSummary& summary) {
  std::cout << "packets_scanned=" << summary.packetsScanned
            << " malformed_packets=" << summary.malformedPackets
            << " unknown_message_packets=" << summary.unknownMessagePackets << "\n";

  std::cout << "unknown_message_types=";
  if (summary.unknownMessageTypes.empty()) std::cout << "<none>";
  for (std::size_t i = 0; i < summary.unknownMessageTypes.size(); ++i) {
    if (i) std::cout << ',';
    std::cout << summary.unknownMessageTypes[i];
  }
  std::cout << "\n";

  std::cout << "packet_entities=" << summary.packetEntitiesCount
            << " packet_entity_updates=" << summary.packetEntityUpdates
            << " header_updates=" << summary.packetEntityHeaderUpdates
            << " delta_packets=" << summary.packetEntityDeltaCount << "\n";

  std::cout << "enter=" << summary.packetEntityEnterCount
            << " preserve=" << summary.packetEntityPreserveCount
            << " leave=" << summary.packetEntityLeaveCount
            << " delete=" << summary.packetEntityDeleteCount << "\n";

  // The acceptance number for the P0 task.
  std::cout << "entity_failures=" << summary.entityUnknownStateFailures
            << " entity_update_header_failures=" << summary.entityUpdateHeaderFailures
            << " packet_entity_decode_failures=" << summary.packetEntityDecodeFailures
            << " prop_missing_table=" << summary.entityPropMissingTableFailures
            << " prop_index=" << summary.entityPropIndexFailures
            << " prop_value=" << summary.entityPropValueFailures << "\n";

  std::cout << "delta_base_unavailable=" << summary.packetEntityDeltaBaseUnavailableCount
            << " history_gap=" << (summary.entityHistoryHasGap ? 1 : 0)
            << " history_gap_tick=" << summary.entityHistoryGapTick
            << " history_dropped_packets=" << summary.entityHistoryDroppedPackets << "\n";

  std::cout << "instance_baselines=" << summary.instanceBaselineEntryCount
            << " baseline_applied=" << summary.instanceBaselineAppliedCount
            << " baseline_misses=" << summary.instanceBaselineLookupMisses
            << " baseline_apply_failures=" << summary.instanceBaselineApplyFailures << "\n";

  std::cout << "active_entities=" << summary.activeEntityCount
            << " max_active_entities=" << summary.maxActiveEntityCount << "\n";

  // The message types this decoder used to have no arm for. All four are
  // non-zero on at least one local demo, so these counters double as the
  // per-demo proof that the new decoders actually ran.
  std::cout << "file_messages=" << summary.fileMessageCount
            << " set_pause=" << summary.setPauseCount
            << " bsp_decals=" << summary.bspDecalCount
            << " menus=" << summary.menuCount
            << " cmd_key_values=" << summary.cmdKeyValuesCount << "\n";

  std::cout << "string_table_user_data_max_bytes=" << summary.stringTableUserDataMaxBytes << "\n";

  // Precache tables, reported together so a change to one is visible against the
  // other. soundprecache and instancebaseline were already retained and are what
  // the audio and entity-baseline paths consume; modelprecache is what turns an
  // entity's m_nModelIndex into a model path, and until it is retained the
  // entity-model path has nothing to load.
  std::cout << "sound_precache_entries=" << summary.soundPrecache.size()
            << " sound_precache_decode_failures=" << summary.soundPrecacheDecodeFailures
            << " model_precache_entries=" << summary.modelPrecache.size()
            << " model_precache_max_entries=" << summary.modelPrecacheMaxEntries
            << " model_precache_decode_failures=" << summary.modelPrecacheDecodeFailures
            << " model_precache_updates=" << summary.modelPrecacheUpdateCount << "\n";

  // Why the asset references do or do not become render requests. The breakdown
  // separates "the entity sent no model reference at all" from "it sent a model
  // index that could not be resolved because the precache table was missing".
  // Those need different fixes, and only the second one is the P1 wiring gap.
  // asset_model_index_unresolved is the one that must be zero on a healthy
  // demo: a non-zero value means the entity named a model the table never
  // declared, and there is no path for the renderer to load.
  std::cout << "asset_refs=" << summary.assetReferences.size()
            << " asset_model_path_known=" << summary.assetModelPathKnown
            << " asset_model_path_from_precache=" << summary.assetModelPathFromPrecache
            << " asset_model_index_known=" << summary.assetModelIndexKnown
            << " asset_model_index_zero=" << summary.assetModelIndexZero
            << " asset_model_index_out_of_range=" << summary.assetModelIndexOutOfRange
            << " asset_model_index_unresolved=" << summary.assetModelIndexUnresolved
            << " asset_model_index_unresolved_max=" << summary.assetModelIndexUnresolvedMax
            << " asset_identity_unknown=" << summary.assetIdentityUnknown << "\n";

  // First-failure coordinates. -1 means "never happened".
  std::cout << "first_entity_failure_tick=" << summary.firstEntityUnknownStateTick
            << " first_entity_failure_entity=" << summary.firstEntityUnknownStateEntity
            << " first_entity_failure_update=" << summary.firstEntityUnknownStateUpdate
            << " first_entity_failure_max_entries=" << summary.firstEntityUnknownStateMaxEntries
            << " first_entity_failure_updated_entries=" << summary.firstEntityUnknownStateUpdatedEntries
            << " first_entity_failure_payload_bits=" << summary.firstEntityUnknownStatePayloadBits
            << " first_entity_failure_diff=" << summary.firstEntityUnknownStateDiff << "\n";

  std::cout << "first_delta_base_unavailable_tick=" << summary.firstPacketEntitiesUnavailableTick
            << " first_delta_base_unavailable_from=" << summary.firstPacketEntitiesUnavailableFrom
            << "\n";

  if (!summary.firstEntityPropFailureStage.empty()) {
    std::cout << "first_prop_failure_stage=" << summary.firstEntityPropFailureStage
              << " tick=" << summary.firstEntityPropFailureTick
              << " entity=" << summary.firstEntityPropFailureEntity
              << " class=" << summary.firstEntityPropFailureClass
              << " index=" << summary.firstEntityPropFailureIndex
              << " name=" << summary.firstEntityPropFailureName
              << " type=" << summary.firstEntityPropFailureType
              << " flags=" << summary.firstEntityPropFailureFlags
              << " bits=" << summary.firstEntityPropFailureBits << "\n";
  }
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  if (argc < 2) {
    std::wcerr << L"usage: entity_protocol_probe [--histogram|--summary] <demo.dem>\n";
    return 2;
  }
  bool histogramOnly = false;
  bool summaryOnly = false;
  int firstPath = 1;
  while (firstPath < argc) {
    const std::wstring arg(argv[firstPath]);
    if (arg == L"--histogram") { histogramOnly = true; ++firstPath; continue; }
    if (arg == L"--summary") { summaryOnly = true; ++firstPath; continue; }
    break;
  }
  if (firstPath >= argc) {
    std::wcerr << L"usage: entity_protocol_probe [--histogram|--summary] <demo.dem>\n";
    return 2;
  }

  int exitCode = 0;
  for (int i = firstPath; i < argc; ++i) {
    const std::filesystem::path path(argv[i]);
    DemoHeader header;
    const bool headerOk = tf2::native::parseDemoHeaderFile(path, header);
    DemoIndex index;
    const bool indexOk = headerOk && tf2::native::indexDemoFile(path, header, index);
    DemoNetworkSummary summary;
    summary.networkProtocol = header.networkProtocol;

    // An interrupted recording ends mid-entry, so the last command is partial and
    // the indexer refuses the file. The entries before that point are still
    // whole, and one of the seven such files in the corpus has 110886 of them.
    // Throwing the whole file away would lose that coverage and would also read
    // as a decode failure, which it is not. Classify the tail explicitly instead:
    // a short unindexed tail is a truncated recording; a long one means the
    // indexer walked into something it does not understand, which is a real
    // failure and must not be filed under "truncated".
    std::size_t tailBytes = 0;
    if (headerOk) {
      std::error_code sizeError;
      const auto fileSize = std::filesystem::file_size(path, sizeError);
      if (!sizeError && fileSize > index.malformedOffset) {
        tailBytes = static_cast<std::size_t>(fileSize - index.malformedOffset);
      }
    }
    constexpr std::size_t kTruncatedTailLimit = 64u * 1024u;
    IndexState indexState = IndexState::Ok;
    if (!indexOk) {
      indexState = (!index.entries.empty() && tailBytes > 0 && tailBytes <= kTruncatedTailLimit)
          ? IndexState::TruncatedTail : IndexState::Invalid;
    }
    const bool hasEntries = !index.entries.empty();
    const bool scanOk = hasEntries && tf2::native::scanKnownDemoMessages(path, index, summary);

    // Keep the path out of the output; it may not survive the console code page.
    std::cout << "header=" << (headerOk ? 1 : 0)
              << " index=" << (indexOk ? 1 : 0)
              << " scan=" << (scanOk ? 1 : 0)
              << " recording=" << recordingName(header.recordingType)
              << " protocol=" << header.networkProtocol
              << " map=" << header.mapName
              << " commands=" << index.commandCount
              << " packets=" << index.packetCount << "\n";
    std::cout << "index_state=" << indexStateName(indexState)
              << " index_entries=" << index.entries.size()
              << " index_tail_bytes=" << tailBytes << "\n";
    // scan=0 with no entries at all means there is genuinely nothing to decode,
    // so it is not a probe failure -- the caller decides whether that is a SKIP
    // or a problem. A non-truncated index failure is the probe's own failure.
    if (!scanOk && (indexState == IndexState::Invalid || !headerOk)) { exitCode = 2; continue; }
    if (!scanOk) continue;

    const auto classification = tf2::native::classifyDemoRecording(header, summary);
    std::cout << "recording_stream=" << recordingKindName(classification.kind)
              << " recording_header_name=" << (classification.headerName ? 1 : 0)
              << " server_info_count=" << summary.serverInfoCount
              << " server_info_hltv=" << (summary.serverInfoHltv ? 1 : 0)
              << " server_info_replay_bit=" << (summary.serverInfoReplayBit ? 1 : 0)
              << " source_tv_flag=" << (summary.sourceTv ? 1 : 0) << "\n";

    // Fill the asset reference list before printing, so the breakdown below
    // describes this demo's entities rather than an empty vector.
    tf2::native::buildAssetReferenceList(summary, summary.assetReferences);
    if (!histogramOnly) printSummary(summary);
    if (!summaryOnly) printHistogram(summary);
  }
  return exitCode;
}
