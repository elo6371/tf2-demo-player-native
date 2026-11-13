#include "demo_header.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <queue>
#include <chrono>
#include <unordered_map>
#include <unordered_set>

#include "demo_header_internal.h"

namespace tf2::native {

// Defined in demo_header_net.cpp (F0-a). The decoders report rejection through
// their existing bool and DemoNetworkSummary carries no field for a name, so the
// bound that fired is recorded instead. It is sticky for one whole scan -- reset
// at scan start, never per message -- because the body that trips a budget can be
// decoded several packets after the one that carried it: clearing per stream left
// the probe reading an empty string. The probe asserts the exact text.
void setParserBudgetRejectReason(const char* reason);
const char* parserBudgetRejectReason();

namespace {

// F0-a (QA1B-03): a data entry tick is a raw int32 with no relationship to
// header.ticks, and the playback row it names becomes a real allocation of
// three vectors (int32 + two uint8, six bytes a row). This is a ROW COUNT, not a
// maximum index: 2^21 rows is 17 hours at 66 tick and 4.5 hours at 128, past the
// longest local demo (~2 hours). An index equal to the cap would need cap + 1
// rows, so the refusal is index >= cap and the probe pins that exact boundary.
constexpr std::int64_t kMaxPlaybackRows = 1ll << 21;

} // namespace

bool decodeLzss(const std::vector<std::uint8_t>& input, std::size_t offset, std::size_t length,
    std::size_t expectedSize, std::vector<std::uint8_t>& output) {
  if (offset > input.size() || length > input.size() - offset || length < 4u) return false;
  const std::size_t end = offset + length;
  const std::uint32_t targetSize = static_cast<std::uint32_t>(input[offset]) |
    (static_cast<std::uint32_t>(input[offset + 1u]) << 8u) |
    (static_cast<std::uint32_t>(input[offset + 2u]) << 16u) |
    (static_cast<std::uint32_t>(input[offset + 3u]) << 24u);
  if (targetSize != expectedSize || targetSize > 128u * 1024u * 1024u) return false;
  std::size_t cursor = offset + 4u;
  output.clear();
  output.reserve(targetSize);
  while (cursor < end && output.size() < targetSize) {
    const std::uint8_t commands = input[cursor++];
    std::uint8_t commandBits = commands;
    for (int bit = 0; bit < 8 && output.size() < targetSize; ++bit, commandBits >>= 1u) {
      if ((commandBits & 1u) == 0u) {
        if (cursor >= end) return false;
        output.push_back(input[cursor++]);
        continue;
      }
      if (cursor + 2u > end) return false;
      const std::size_t position = (static_cast<std::size_t>(input[cursor]) << 4u) | (input[cursor + 1u] >> 4u);
      const std::size_t count = (input[cursor + 1u] & 0x0fu) + 1u;
      cursor += 2u;
      if (count == 1u || position + 1u > output.size() || output.size() + count > targetSize) return false;
      const std::size_t start = output.size() - position - 1u;
      for (std::size_t i = 0; i < count; ++i) output.push_back(output[start + i]);
    }
  }
  return output.size() == targetSize;
}

bool decodeSnappyRaw(const std::vector<std::uint8_t>& input, std::size_t offset, std::size_t length,
    std::size_t expectedSize, std::vector<std::uint8_t>& output) {
  if (offset > input.size() || length > input.size() - offset) return false;
  std::size_t cursor = offset;
  const std::size_t end = offset + length;
  std::uint32_t decodedSize = 0;
  std::uint32_t shift = 0;
  while (cursor < end && shift < 32u) {
    const std::uint8_t byte = input[cursor++];
    decodedSize |= static_cast<std::uint32_t>(byte & 0x7fu) << shift;
    if ((byte & 0x80u) == 0) break;
    shift += 7u;
  }
  if (decodedSize != expectedSize || decodedSize > 128u * 1024u * 1024u) return false;
  output.clear();
  output.reserve(decodedSize);
  while (cursor < end && output.size() < decodedSize) {
    const std::uint8_t tag = input[cursor++];
    const std::uint32_t kind = tag & 3u;
    if (kind == 0u) {
      const std::uint32_t lengthCode = tag >> 2u;
      std::uint32_t literalLength = lengthCode + 1u;
      if (lengthCode >= 60u) {
        const std::uint32_t extraBytes = lengthCode - 59u;
        if (extraBytes > 4u || cursor + extraBytes > end) return false;
        literalLength = 0;
        for (std::uint32_t i = 0; i < extraBytes; ++i) literalLength |= static_cast<std::uint32_t>(input[cursor++]) << (8u * i);
        ++literalLength;
      }
      if (literalLength > end - cursor || output.size() + literalLength > decodedSize) return false;
      output.insert(output.end(), input.begin() + static_cast<std::ptrdiff_t>(cursor), input.begin() + static_cast<std::ptrdiff_t>(cursor + literalLength));
      cursor += literalLength;
      continue;
    }
    std::uint32_t copyLength = 0, distance = 0;
    if (kind == 1u) {
      copyLength = ((tag >> 2u) & 7u) + 4u;
      if (cursor >= end) return false;
      distance = ((static_cast<std::uint32_t>(tag) & 0xe0u) << 3u) | input[cursor++];
    } else if (kind == 2u) {
      copyLength = (tag >> 2u) + 1u;
      if (cursor + 2u > end) return false;
      distance = static_cast<std::uint32_t>(input[cursor]) | (static_cast<std::uint32_t>(input[cursor + 1u]) << 8u);
      cursor += 2u;
    } else {
      copyLength = (tag >> 2u) + 1u;
      if (cursor + 4u > end) return false;
      distance = static_cast<std::uint32_t>(input[cursor]) |
        (static_cast<std::uint32_t>(input[cursor + 1u]) << 8u) |
        (static_cast<std::uint32_t>(input[cursor + 2u]) << 16u) |
        (static_cast<std::uint32_t>(input[cursor + 3u]) << 24u);
      cursor += 4u;
    }
    if (distance == 0u || distance > output.size() || output.size() + copyLength > decodedSize) return false;
    for (std::uint32_t i = 0; i < copyLength; ++i) output.push_back(output[output.size() - distance]);
  }
  return output.size() == decodedSize && cursor == end;
}
namespace {

} // namespace

bool decodeDemoMessageStream(const std::vector<std::uint8_t>& payload,
                             std::size_t payloadBits,
                             std::int32_t entryTick,
                             std::int32_t initialNetworkTick,
                             DemoNetworkSummary& summary,
                             DemoMessageStreamResult& result,
                             std::uint32_t packetOrdinal) {
  result = {};
  // F0-a: the reason is deliberately NOT reset per stream. The packet that sets a
  // budget reason is not always the last one, so clearing here erased the reason
  // before any caller could read it; it is reset once per scan instead.
  std::vector<std::uint8_t> trimmed;
  const std::vector<std::uint8_t>* source = &payload;
  if (payloadBits < payload.size() * 8u) {
    // Trailing bits inside the last retained byte are still readable, exactly
    // as they are on the real demo path where the entry length is byte-aligned.
    trimmed.assign(payload.begin(), payload.begin() + static_cast<std::ptrdiff_t>((payloadBits + 7u) / 8u));
    source = &trimmed;
  }
  MessageBits bits(*source);
  std::int32_t networkTick = initialNetworkTick >= 0 ? initialNetworkTick : entryTick;
  bool sawNetTick = false;
  while (bits.remaining() > 6) {
    const std::size_t messageBit = bits.offsetBits();
    std::uint32_t type = 0;
    if (!bits.read(6, type)) { result.packetValid = false; break; }
    if (type < DemoNetworkSummary::kMessageTypeHistogramSize) ++summary.messageTypeCounts[type];
    if (type == 0) { result.decodedAny = true; ++result.messagesDecoded; continue; }
    if (type == 3) { std::uint32_t tick = 0, frameTime = 0, deviation = 0; if (!bits.read(32, tick) || !bits.read(16, frameTime) || !bits.read(16, deviation)) result.packetValid = false; else { networkTick = tick <= 0x7fffffffu ? static_cast<std::int32_t>(tick) : -1; summary.lastNetworkTick = networkTick; summary.lastNetworkTickRaw = tick; summary.lastNetworkTickRawValid = true; sawNetTick = true; ++summary.netTickCount; result.decodedAny = true; } }
    else if (type == 4) { if (!readStringCommand(bits, summary)) result.packetValid = false; else { result.decodedAny = true; } }
    else if (type == 5) { if (!readSetConVar(bits, summary)) result.packetValid = false; else { result.decodedAny = true; } }
    else if (type == 6) { if (!readSignonState(bits, summary)) result.packetValid = false; else { result.decodedAny = true; } }
    else if (type == 7) { std::string printText; if (!bits.readString(printText)) result.packetValid = false; else { ++summary.printCount; result.decodedAny = true; } }
    else if (type == 8) { if (!readServerInfo(bits, summary)) result.packetValid = false; else { ++summary.serverInfoCount; result.decodedAny = true; } }
    else if (type == 9) { if (!readSendTable(bits, summary)) result.packetValid = false; else { result.decodedAny = true; } }
    else if (type == 10) { if (!readClassInfo(bits, summary)) result.packetValid = false; else { result.decodedAny = true; } }
    else if (type == 12) { if (!readCreateStringTable(bits, summary, networkTick)) result.packetValid = false; else { result.decodedAny = true; } }
    else if (type == 13) { if (!readUpdateStringTable(bits, summary, networkTick)) result.packetValid = false; else { result.decodedAny = true; } }
    else if (type == 14) { if (!readVoiceInit(bits, summary)) result.packetValid = false; else { result.decodedAny = true; } }
    else if (type == 15) { if (!readVoiceData(bits, summary)) result.packetValid = false; else { result.decodedAny = true; } }
    else if (type == 18) { if (!readSetView(bits, summary)) result.packetValid = false; else { result.decodedAny = true; } }
    else if (type == 17) { if (!readSounds(bits, summary, entryTick)) result.packetValid = false; else { result.decodedAny = true; } }
    else if (type == 19) { if (!readFixAngle(bits, summary, networkTick)) result.packetValid = false; else { result.decodedAny = true; } }
    else if (type == 23) { if (!readUserMessage(bits, summary)) result.packetValid = false; else { result.decodedAny = true; } }
    else if (type == 24) { if (!readEntityMessage(bits, summary)) result.packetValid = false; else { result.decodedAny = true; } }
    else if (type == 25) { if (!readGameEvent(bits, summary, entryTick, packetOrdinal, static_cast<std::uint32_t>(messageBit))) result.packetValid = false; else { result.decodedAny = true; } }
    else if (type == 26) { if (summary.firstPacketEntitiesMessageBit < 0) summary.firstPacketEntitiesMessageBit = static_cast<std::int64_t>(messageBit); if (!readPacketEntities(bits, summary, networkTick)) result.packetValid = false; else { result.decodedAny = true; } }
    else if (type == 27) { if (!readTempEntities(bits, summary, networkTick)) result.packetValid = false; else { result.decodedAny = true; } }
    else if (type == 28) { if (!readPrefetch(bits, summary)) result.packetValid = false; else { result.decodedAny = true; } }
    else if (type == 30) { if (!readGameEventList(bits, summary)) result.packetValid = false; else { result.decodedAny = true; } }
    else if (type == 31) { if (!readGetCvarValue(bits, summary)) result.packetValid = false; else { result.decodedAny = true; } }
    // Types the local Rust reference implements. Before these existed the loop
    // fell through to the unknown-type arm and abandoned the packet, which also
    // discarded the svc_PacketEntities sharing that packet and silently froze
    // the entity->class map from then on.
    else if (type == 2) { if (!readFileMessage(bits, summary)) result.packetValid = false; else { result.decodedAny = true; } }
    else if (type == 11) { if (!readSetPause(bits, summary)) result.packetValid = false; else { result.decodedAny = true; } }
    else if (type == 21) { if (!readBspDecal(bits, summary)) result.packetValid = false; else { result.decodedAny = true; } }
    else if (type == 29) { if (!readMenu(bits, summary)) result.packetValid = false; else { result.decodedAny = true; } }
    else if (type == 32) { if (!readCmdKeyValues(bits, summary)) result.packetValid = false; else { result.decodedAny = true; } }
    else { ++summary.unknownMessagePackets; if (summary.unknownMessageTypes.size() < 16) summary.unknownMessageTypes.push_back(type); result.hitUnknownType = true; result.unknownType = type; result.packetValid = false; break; }
    if (!result.packetValid) break;
    ++result.messagesDecoded;
  }
  result.bitsConsumed = bits.offsetBits();
  // T63-F1: publish the server tick this packet ends on. A packet with no
  // svc_NetTick of its own keeps the tick carried in from earlier packets; if
  // there is none, the tick is unknown and stays -1 rather than borrowing the
  // entry tick (which is what made an entry tick look like a server tick).
  result.finalNetworkTickKnown = sawNetTick || initialNetworkTick >= 0;
  result.finalNetworkTick = result.finalNetworkTickKnown ? networkTick : -1;
  result.sawNetTick = sawNetTick;
  result.lastNetworkTick = networkTick;
  return true;
}

bool scanKnownDemoMessages(const std::filesystem::path& path, const DemoIndex& index, DemoNetworkSummary& summary) {
  const int networkProtocol = summary.networkProtocol;
  // A zero here means the caller forgot to copy header.networkProtocol in.
  // Decoding with protocol 0 silently takes the wrong branch in
  // svc_CreateStringTable (a 20-bit length field instead of a varint) and
  // produces a plausible-looking but wrong summary. entity_model_probe did
  // exactly that and reported requests=0 out of 673 references; refusing turns a
  // silent wrong reading into a loud failure.
  if (networkProtocol <= 0) return false;
  // T53: the retention limits are CONFIGURATION, not scan output, so they survive
  // the reset below. Without this the caller's `measureAppendCost` and
  // `maxSamples` were silently thrown away before the first packet was read --
  // which is exactly how the append cost read "NOT MEASURED" on a run that had
  // asked for the measurement. Zero-value limits (a caller that set nothing) still
  // resolve to the struct's own defaults, so the reset stays the default path.
  const ProjectileTrackLimits projectileTrackLimits = summary.projectileTrackLimits;
  summary = {};
  setParserBudgetRejectReason(nullptr);
  summary.projectileTrackLimits = projectileTrackLimits;
  summary.networkProtocol = networkProtocol;
  if (index.entries.empty()) return false;
  summary.entityClassByIndex.assign(2048u, -1);
  summary.entityStates.assign(2048u, EntityState{});
  std::ifstream file(path, std::ios::binary); if (!file) return false;
  for (std::size_t entryIndex = 0; entryIndex < index.entries.size(); ++entryIndex) {
    const DemoIndexEntry& entry = index.entries[entryIndex];
    if (entry.command == 6) {
      std::vector<std::uint8_t> dataTablePayload;
      if (!readEntryPayload(file, entry, dataTablePayload, 128u * 1024u * 1024u)) {
        ++summary.malformedPackets;
      } else {
        MessageBits dataTableBits(dataTablePayload);
        if (!readDataTableBody(dataTableBits, summary)) ++summary.malformedPackets;
      }
      continue;
    }
    if (entry.command == 8) {
      std::vector<std::uint8_t> stringTablesPayload;
      if (!readEntryPayload(file, entry, stringTablesPayload, 128u * 1024u * 1024u)
          || !readDemoStringTablesPayload(stringTablesPayload, summary, entry.tick)) {
        ++summary.malformedPackets;
      }
      continue;
    }
    if (entry.command != 1 && entry.command != 2) continue;
    if (entry.command == 1 && summary.firstSignonEntryTick < 0) summary.firstSignonEntryTick = entry.tick;
    if (entry.command == 2) {
      if (summary.firstDataEntryTick < 0) summary.firstDataEntryTick = entry.tick;
      summary.lastDataEntryTick = entry.tick;
    }
    if (entry.payloadOffset >= 88) {
      std::uint8_t cmdInfo[76] = {};
      file.seekg(static_cast<std::streamoff>(entry.payloadOffset - 88), std::ios::beg);
      if (readExact(file, cmdInfo, sizeof(cmdInfo))) {
        DemoViewSample sample;
        if (parseDemoCmdInfo(cmdInfo, sizeof(cmdInfo), sample)) {
          sample.tick = entry.tick;
          sample.fromSignon = entry.command == 1;
          // T63-F1: keep the pre-fix assembly here (signon samples included) so
          // viewSamplesOutOfOrder measures the defect, then partition after the
          // scan. Pushing them in and dropping them later is what makes the
          // pre-repair reading honest.
          if (sample.fromSignon) ++summary.signonViewSamples;
          if (summary.viewSamples.size() >= kMaxViewSamples) {
            ++summary.viewSamplesDropped;
          } else {
            summary.viewSamples.push_back(sample);
          }
        }
      }
    }
    std::vector<std::uint8_t> payload;
    if (!readEntryPayload(file, entry, payload, 128u * 1024u * 1024u)) { ++summary.malformedPackets; continue; }
    DemoMessageStreamResult stream;
    const bool streamOk = decodeDemoMessageStream(payload, payload.size() * 8u, entry.tick,
                                                  summary.lastNetworkTick, summary, stream,
                                                  static_cast<std::uint32_t>(entryIndex));
    ++summary.packetsScanned;
    if (!streamOk || !stream.packetValid || !stream.decodedAny) ++summary.malformedPackets;
    if (entry.command == 2) {
      // T63-F1: the row is indexed by PLAYBACK tick, not by packet ordinal.
      // Data entry tick == firstDataEntryTick + playback tick, so the packet
      // recorded at this entry lands on row (entry.tick - firstDataEntryTick).
      // Indexing by packet ordinal instead slides by one for every dropped
      // packet: S1 drops 22 and S5 drops 4348, so ordinal 20000 is not
      // playback 20000 on either.
      const bool tickKnown = streamOk && stream.finalNetworkTickKnown;
      // F0-a (QA1B-03): entry.tick is a raw int32 with no relation to
      // header.ticks, and the row it names sizes three vectors -- at 0x7fffffff
      // that is ~12.9 GB from a 1 KB file. Widen before subtracting, because
      // entry.tick - firstDataEntryTick overflows int32 on its own.
      const std::int64_t playbackIndex =
          static_cast<std::int64_t>(entry.tick) - static_cast<std::int64_t>(summary.firstDataEntryTick);
      if (playbackIndex >= kMaxPlaybackRows) {
        // Beyond the cap is a corrupt entry, not a long demo: count it and keep
        // scanning instead of sizing three vectors for it. First rejection wins,
        // so a later bad packet cannot rewrite what the probe asserts.
        if (parserBudgetRejectReason() == nullptr) setParserBudgetRejectReason("playback-row-cap");
        ++summary.malformedPackets;
      } else if (playbackIndex >= 0) {
        const std::size_t row = static_cast<std::size_t>(playbackIndex);
        if (row >= summary.packetServerTicks.size()) {
          summary.packetServerTicks.resize(row + 1, -1);
          summary.packetServerTickKnown.resize(row + 1, 0u);
          summary.packetServerTickOwn.resize(row + 1, 0u);
        }
        summary.packetServerTickOwn[row] = 1u;
        ++summary.dataPacketCount;
        if (tickKnown) {
          summary.packetServerTicks[row] = stream.finalNetworkTick;
          summary.packetServerTickKnown[row] = 1u;
          ++summary.serverTickKnownCount;
          if (summary.firstDataNetTick < 0) summary.firstDataNetTick = stream.finalNetworkTick;
        }
      }
      if (stream.sawNetTick) ++summary.packetsWithOwnNetTick;
      if (summary.firstDataPacketOwnNetTick < 0) {
        summary.firstDataPacketOwnNetTick = stream.sawNetTick ? 1 : 0;
      }
    }
  }
  // T63-F1: a playback tick whose packet was dropped still renders. Its row
  // holds the tick of the last recorded packet at or before it -- the same
  // at-or-before rule the entity query uses. That is a hold, not an invented
  // number: packetServerTickOwn[row] is 1 only where a packet exists.
  for (std::size_t i = 1; i < summary.packetServerTicks.size(); ++i) {
    if (summary.packetServerTickOwn[i] != 0) continue;
    if (summary.packetServerTickKnown[i - 1] != 0) {
      summary.packetServerTicks[i] = summary.packetServerTicks[i - 1];
      summary.packetServerTickKnown[i] = 1u;
      ++summary.serverTickHeldCount;
    }
  }
  // Gaps are measured between consecutive RECORDED packets only; a held row
  // repeats the previous tick and would otherwise read as a stall.
  std::int32_t previousRecordedTick = -1;
  for (std::size_t i = 0; i < summary.packetServerTicks.size(); ++i) {
    if (summary.packetServerTickOwn[i] == 0 || summary.packetServerTickKnown[i] == 0) continue;
    if (previousRecordedTick >= 0) {
      const std::int32_t delta = summary.packetServerTicks[i] - previousRecordedTick;
      if (delta > 1) ++summary.serverTickGaps;
      else if (delta <= 0) ++summary.serverTickStalls;
    }
    previousRecordedTick = summary.packetServerTicks[i];
  }
  // T63-F1 diagnosis: count adjacent non-increasing ticks in the sample order
  // as assembled. upper_bound in findObserverViewAtOrBeforeTick requires
  // strictly increasing input, so a non-zero reading here is a precondition
  // violation, not a style detail. Measured before any repair.
  for (std::size_t i = 1; i < summary.viewSamples.size(); ++i) {
    if (summary.viewSamples[i].tick <= summary.viewSamples[i - 1].tick) ++summary.viewSamplesOutOfOrder;
  }
  // T63-F1 fix: partition anything that is not a data-packet cmdinfo sample out
  // of the data sequence, then check the sequence the query actually consumes.
  {
    std::vector<DemoViewSample> dataSamples;
    dataSamples.reserve(summary.viewSamples.size());
    for (auto& sample : summary.viewSamples) {
      if (sample.fromSignon) continue;
      if (sample.source == DemoViewSource::FixAngle) { summary.fixAngleSamples.push_back(sample); continue; }
      dataSamples.push_back(sample);
    }
    summary.viewSamples.swap(dataSamples);
  }
  // POV demos interleave cmdinfo samples out of tick order (measured 505 of
  // 5051 out of order on pl_frontier). upper_bound over an unsorted vector
  // finds nothing, so the camera read back as "no view at or before this
  // tick"; the product then held its default pose. Sort, then keep the LAST
  // sample for a repeated tick.
  std::stable_sort(summary.viewSamples.begin(), summary.viewSamples.end(),
      [](const tf2::native::DemoViewSample& a, const tf2::native::DemoViewSample& b) {
        return a.tick < b.tick;
      });
  if (!summary.viewSamples.empty()) {
    std::vector<tf2::native::DemoViewSample> deduped;
    deduped.reserve(summary.viewSamples.size());
    for (const auto& sample : summary.viewSamples) {
      if (!deduped.empty() && deduped.back().tick == sample.tick) {
        ++summary.viewSamplesDuplicates;
        deduped.back() = sample;
      } else {
        deduped.push_back(sample);
      }
    }
    summary.viewSamples.swap(deduped);
  }
  summary.viewSamplesOrdered = true;
  summary.viewSamplesOutOfOrderAfterRepair = 0;
  for (std::size_t i = 1; i < summary.viewSamples.size(); ++i) {
    if (summary.viewSamples[i].tick <= summary.viewSamples[i - 1].tick) {
      summary.viewSamplesOrdered = false;
      ++summary.viewSamplesOutOfOrderAfterRepair;
    }
  }
  for (const auto& entity : summary.entityStates) {
    for (const auto& [name, value] : entity.properties) {
      if (name.find("m_vecOrigin") != std::string::npos) ++summary.entityOriginStateCount;
      if (name.find("m_iHealth") != std::string::npos) ++summary.entityHealthStateCount;
      if (name.find("m_iTeamNum") != std::string::npos) ++summary.entityTeamStateCount;
      if (name.find("m_iClass") != std::string::npos) ++summary.entityClassStateCount;
      if (name.find("m_hActiveWeapon") != std::string::npos) ++summary.entityWeaponStateCount;
      if (name.find("m_iObserver") != std::string::npos || name.find("m_hObserver") != std::string::npos) ++summary.entityObserverStateCount;
    }
  }
  for (auto& event : summary.decodedSoundEvents) {
    const auto it = summary.soundPrecache.find(event.soundIndex);
    if (it == summary.soundPrecache.end()) {
      ++summary.decodedSoundResourceMisses;
    } else {
      event.resourceName = it->second;
      ++summary.decodedSoundResourceMatches;
    }
  }
  return summary.packetsScanned > 0;
}

} // namespace tf2::native
