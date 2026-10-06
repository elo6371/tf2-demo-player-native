#include "demo_header.h"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>

int wmain(int argc, wchar_t** argv) {
  if (argc < 2) {
    std::wcerr << L"usage: demo_open_probe [--scan] <demo.dem>\n";
    return 2;
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
  if (firstPath >= argc) {
    std::wcerr << L"usage: demo_open_probe [--scan] <demo.dem>\n";
    return 2;
  }
  for (int i = firstPath; i < argc; ++i) {
    const std::filesystem::path path(argv[i]);
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
               << L" packets=" << index.packetCount;
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
                << L" unknown_packets=" << summary.unknownMessagePackets;
      std::wcout << L" fixangle_valid=" << (summary.lastFixAngleValid ? 1 : 0)
                 << L" fixangle_relative=" << (summary.lastFixAngleRelative ? 1 : 0)
                 << L" fixangle=" << summary.lastFixAngle[0] << L"," << summary.lastFixAngle[1]
                 << L"," << summary.lastFixAngle[2]
                 << L" camera_track=" << summary.observerCameraTrack.size()
                 << L" camera_track_dropped=" << summary.observerCameraTrackDropped;
      if (cameraTick >= 0) {
        tf2::native::ObserverCameraTrackSample camera;
        const bool found = tf2::native::findObserverCameraAtOrBeforeTick(summary, cameraTick, camera);
        std::wcout << L" camera_tick=" << cameraTick << L" camera_found=" << (found ? 1 : 0)
                   << L" camera_sample_tick=" << camera.tick
                   << L" camera_view_entity=" << (camera.hasViewEntity ? static_cast<int>(camera.viewEntity) : -1)
                   << L" camera_angles_valid=" << (camera.hasAngles ? 1 : 0);
      }
      if (snapshotTick >= 0) {
        std::vector<tf2::native::EntityState> states;
        const auto status = tf2::native::queryEntitySnapshotAtOrBeforeTick(summary, snapshotTick, states);
        const auto statusName = [](tf2::native::EntitySnapshotQueryStatus value) {
          switch (value) {
            case tf2::native::EntitySnapshotQueryStatus::Available: return L"available";
            case tf2::native::EntitySnapshotQueryStatus::NoHistory: return L"no-history";
            case tf2::native::EntitySnapshotQueryStatus::TickBeforeHistory: return L"before-history";
            case tf2::native::EntitySnapshotQueryStatus::Gap: return L"gap";
            case tf2::native::EntitySnapshotQueryStatus::DeltaBaseMissing: return L"delta-base-missing";
          }
          return L"unknown";
        };
        std::wcout << L" snapshot_tick=" << snapshotTick
                   << L" snapshot_status=" << statusName(status)
                   << L" snapshot_states=" << states.size();
      }
    }
    std::wcout << L'\n';
  }
  return 0;
}
