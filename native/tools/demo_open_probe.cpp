#include "demo_header.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <set>
#include <string>
#include <tuple>
#include <vector>

namespace {

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

  std::wcout << L"parser_bounds_selftest missing_slot_rejected=" << (rejectedMissingSlot ? 1 : 0)
             << L" valid_control=" << (acceptedControl ? 1 : 0)
             << L" nan_time_rejected=" << (rejectedNanTime ? 1 : 0)
             << L" infinite_time_rejected=" << (rejectedInfiniteTime ? 1 : 0)
             << L" finite_time_control=" << (acceptedFiniteTime ? 1 : 0)
             << L" malformed_error=" << std::wstring(malformed.error.begin(), malformed.error.end())
             << L" valid_entries=" << valid.entries.size() << L'\n';
  return rejectedMissingSlot && acceptedControl && rejectedNanTime
      && rejectedInfiniteTime && acceptedFiniteTime ? 0 : 1;
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
