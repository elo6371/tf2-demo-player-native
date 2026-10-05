#include "demo_header.h"

#include <iostream>

int main(int argc, char** argv) {
  if (argc < 2) {
    std::cerr << "usage: demo_open_probe [--scan] <demo.dem>\n";
    return 2;
  }
  bool scan = false;
  int firstPath = 1;
  if (std::string(argv[1]) == "--scan") { scan = true; firstPath = 2; }
  if (firstPath >= argc) {
    std::cerr << "usage: demo_open_probe [--scan] <demo.dem>\n";
    return 2;
  }
  for (int i = firstPath; i < argc; ++i) {
    const auto path = std::filesystem::u8path(argv[i]);
    tf2::native::DemoHeader header;
    const bool headerOk = tf2::native::parseDemoHeaderFile(path, header);
    tf2::native::DemoIndex index;
    const bool indexOk = headerOk && tf2::native::indexDemoFile(path, header, index);
    std::cout << "path=" << path.string()
              << " header=" << (headerOk ? 1 : 0)
              << " header_error=" << header.error
              << " index=" << (indexOk ? 1 : 0)
              << " index_error=" << index.error
              << " malformed_offset=" << index.malformedOffset
              << " commands=" << index.commandCount
              << " packets=" << index.packetCount;
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
      std::cout << " scan=" << (scanOk ? 1 : 0)
                << " packets_scanned=" << summary.packetsScanned
                << " malformed_packets=" << summary.malformedPackets
                << " temp_events=" << summary.tempEventCount
                << " temp_failures=" << summary.tempEventDecodeFailures
                << " particles=" << summary.tempParticleEffectCount
                << " explosions=" << summary.tempExplosionCount
                << " firebullets=" << summary.tempFireBulletsCount
                << " projectiles=" << summary.projectileTimeline.size()
                << " sounds=" << summary.soundEventCount
                << " sound_failures=" << summary.decodedSoundEventFailures
                << " sound_matches=" << summary.decodedSoundResourceMatches
                << " sound_misses=" << summary.decodedSoundResourceMisses
                << " entities=" << summary.packetEntityUpdates
                << " entity_failures=" << summary.packetEntityDecodeFailures
                << " entity_header_failures=" << summary.entityUpdateHeaderFailures
                << " entity_unknown_state_failures=" << summary.entityUnknownStateFailures
                << " entity_missing_table_failures=" << summary.entityPropMissingTableFailures
                << " entity_prop_index_failures=" << summary.entityPropIndexFailures
                << " entity_prop_value_failures=" << summary.entityPropValueFailures
                << " first_unknown_tick=" << summary.firstEntityUnknownStateTick
                << " first_unknown_entity=" << summary.firstEntityUnknownStateEntity
                << " first_unknown_update=" << summary.firstEntityUnknownStateUpdate
                << " first_unknown_max=" << summary.firstEntityUnknownStateMaxEntries
                << " first_unknown_entries=" << summary.firstEntityUnknownStateUpdatedEntries
                << " first_unknown_payload=" << summary.firstEntityUnknownStatePayloadBits
                << " first_unknown_diff=" << summary.firstEntityUnknownStateDiff
                << " first_entities_tick=" << summary.firstPacketEntitiesTick
                << " first_entities_max=" << summary.firstPacketEntitiesMaxEntries
                << " first_entities_entries=" << summary.firstPacketEntitiesUpdatedEntries
                << " first_entities_payload=" << summary.firstPacketEntitiesPayloadBits
                << " first_entities_delta=" << summary.firstPacketEntitiesDelta
                << " first_enter_tick=" << summary.firstPacketEntitiesEnterTick
                << " first_preserve_tick=" << summary.firstPacketEntitiesPreserveTick
                << " first_entity_failure_stage=" << summary.firstEntityPropFailureStage
                << " first_entity_failure_name=" << summary.firstEntityPropFailureName
                << " first_temp_failure_stage=" << summary.firstTempEntityFailureStage
                << " first_temp_failure_name=" << summary.firstTempEntityFailureName
                << " first_temp_failure_class=" << summary.firstTempEntityFailureClass
                << " first_temp_failure_class_name=" << firstTempClassName
                << " first_temp_failure_type=" << summary.firstTempEntityFailureType
                << " first_temp_failure_flags=" << summary.firstTempEntityFailureFlags
                << " first_temp_failure_bits=" << summary.firstTempEntityFailureBits
                << " first_temp_failure_tick=" << summary.firstTempEntityFailureTick
                << " unknown_packets=" << summary.unknownMessagePackets;
    }
    std::cout << '\n';
  }
  return 0;
}
