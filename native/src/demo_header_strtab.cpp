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

namespace {

// Walk the entries of a precache string table and record each one's text by
// index. Shared by svc_CreateStringTable and svc_UpdateStringTable, and by every
// precache table: the sequential/index encoding, the 32-entry string history and
// the user-data skip are byte-identical between them. This used to be three
// near-copies (create/soundprecache, update/soundprecache, and the instancebaseline
// walk), which is exactly how one of them silently loses a field.
//
// entryIndexBits is the index width derived from maxEntries; the caller owns
// that derivation because create and update take maxEntries from different
// places. Returns false if the payload ran out early -- the caller turns that
// into its own per-table failure counter.
bool walkPrecacheTablePayload(const std::vector<std::uint8_t>& payload,
                              std::uint32_t entryCount,
                              std::uint32_t entryIndexBits,
                              bool fixedUserData,
                              std::uint32_t fixedUserDataBits,
                              DemoNetworkSummary& summary,
                              std::unordered_map<std::uint16_t, std::string>& out) {
  MessageBits table(payload);
  std::vector<std::string> history;
  history.reserve(32);
  // -1 rather than a separate haveLastIndex flag: the first sequential entry
  // must resolve to index 0, and -1 + 1 == 0.
  std::int32_t lastIndex = -1;
  for (std::uint32_t entry = 0; entry < entryCount; ++entry) {
    std::uint32_t sequential = 0, index = 0, value = 0;
    if (!table.read(1, sequential)) return false;
    if (sequential) {
      index = static_cast<std::uint32_t>(lastIndex + 1);
    } else if (!table.read(entryIndexBits, index)) {
      return false;
    }
    lastIndex = static_cast<std::int32_t>(index);
    std::string text;
    if (!table.read(1, value)) return false;
    if (value) {
      if (!table.read(1, value)) return false;
      if (value) {
        std::uint32_t historyIndex = 0, copyCount = 0;
        std::string rest;
        if (!table.read(5, historyIndex) || !table.read(5, copyCount) || !table.readString(rest, 4096)) return false;
        if (historyIndex < history.size() && copyCount <= history[historyIndex].size()) {
          text = history[historyIndex].substr(0, copyCount) + rest;
        } else {
          text = std::move(rest);
        }
      } else if (!table.readString(text, 4096)) {
        return false;
      }
    }
    // The history is pushed before the move into `out`, so a later entry can
    // still reference this text.
    history.push_back(text);
    if (history.size() > 32u) history.erase(history.begin());
    if (!table.read(1, value)) return false;
    if (value) {
      if (fixedUserData) {
        if (!table.skip(fixedUserDataBits)) return false;
      } else {
        std::uint32_t userBytes = 0;
        // 14-bit length field; the reference parser imposes no cap. skip()
        // still refuses to run past the end of the payload.
        if (!table.read(14, userBytes)) return false;
        if (userBytes > summary.stringTableUserDataMaxBytes) {
          summary.stringTableUserDataMaxBytes = userBytes;
        }
        if (!table.skip(static_cast<std::size_t>(userBytes) * 8u)) return false;
      }
    }
    if (!text.empty() && index <= 0xffffu) {
      out[static_cast<std::uint16_t>(index)] = std::move(text);
    }
  }
  return true;
}
} // namespace

bool readUpdateStringTable(MessageBits& bits, DemoNetworkSummary& summary) {
  std::uint32_t tableId = 0;
  std::uint32_t changed = 0;
  std::uint32_t changedEntries = 1;
  std::uint32_t payloadBits = 0;
  if (!bits.read(5, tableId) || !bits.read(1, changed)) return false;
  if (changed && !bits.read(16, changedEntries)) return false;
  if (!bits.read(20, payloadBits) || payloadBits > bits.remaining()) return false;
  summary.lastStringTableUpdateId = tableId;
  summary.lastStringTableUpdateEntries = changedEntries;
  summary.lastStringTableUpdateBits = payloadBits;
  if (summary.stringTableUpdateIds.size() < 128u) {
    summary.stringTableUpdateIds.push_back(tableId);
    summary.stringTableUpdateEntries.push_back(changedEntries);
  }
  std::vector<std::uint8_t> payload((static_cast<std::size_t>(payloadBits) + 7u) / 8u, 0);
  for (std::size_t bit = 0; bit < payloadBits; ++bit) {
    std::uint32_t value = 0;
    if (!bits.read(1, value)) return false;
    payload[bit / 8u] |= static_cast<std::uint8_t>(value << (bit % 8u));
  }
  // Both precache tables arrive through the same update path; only the table id,
  // the declared capacity and the destination differ.
  const auto updatePrecache = [&](std::uint32_t maxEntries, std::uint32_t fixedBits,
                                  std::unordered_map<std::uint16_t, std::string>& out,
                                  std::size_t& failures) {
    // Source's log_base2(maxEntries) is floor(log2(maxEntries)); the old loop
    // computed one bit too few for every power-of-two table size.
    std::uint32_t indexBits = 0;
    const std::uint32_t boundedMaxEntries = std::max<std::uint32_t>(1u, maxEntries);
    while ((1u << indexBits) < boundedMaxEntries && indexBits < 31u) ++indexBits;
    // A table with no declared capacity cannot be decoded at all. The old code
    // counted that as a decode failure rather than quietly keeping zero entries,
    // and that must not change.
    const bool ok = maxEntries != 0
      && walkPrecacheTablePayload(payload, changedEntries, indexBits,
                                  fixedBits != 0u, fixedBits, summary, out);
    if (!ok) ++failures;
  };
  if (tableId < 32u && tableId == summary.soundPrecacheTableId) {
    ++summary.soundPrecacheUpdateCount;
    const auto maxIt = summary.stringTableMaxEntries.find(tableId);
    updatePrecache(maxIt == summary.stringTableMaxEntries.end() ? summary.soundPrecacheMaxEntries : maxIt->second,
                   summary.soundPrecacheFixedBits, summary.soundPrecache, summary.soundPrecacheDecodeFailures);
  } else if (tableId < 32u && tableId == summary.modelPrecacheTableId) {
    ++summary.modelPrecacheUpdateCount;
    const auto maxIt = summary.stringTableMaxEntries.find(tableId);
    updatePrecache(maxIt == summary.stringTableMaxEntries.end() ? summary.modelPrecacheMaxEntries : maxIt->second,
                   summary.modelPrecacheFixedBits, summary.modelPrecache, summary.modelPrecacheDecodeFailures);
  }
  ++summary.updateStringTableCount;
  return true;
}
namespace {

// What the 12-byte compressed string-table header declared. Reported separately
// from the decode result so a caller can record the sizes even when the
// decompressor refuses the payload.
struct StringTablePayloadInfo {
  std::uint32_t decompressedBytes = 0;
  std::uint32_t compressedBytes = 0;
  std::uint32_t magic = 0;
  // The payload is usable: either it was never compressed, or it decompressed.
  bool decoded = false;
  // `decodedPayload` holds the bytes to read. Kept explicit rather than inferred
  // from `decodedPayload.empty()`, because a legitimately empty decode would
  // otherwise send the caller back to the compressed bytes.
  bool usedDecompressed = false;
};

std::uint32_t readLe32(const std::vector<std::uint8_t>& data, std::size_t offset) {
  return static_cast<std::uint32_t>(data[offset])
    | (static_cast<std::uint32_t>(data[offset + 1u]) << 8u)
    | (static_cast<std::uint32_t>(data[offset + 2u]) << 16u)
    | (static_cast<std::uint32_t>(data[offset + 3u]) << 24u);
}

// Unwrap a compressed string-table payload. The wrapper is identical for
// soundprecache, modelprecache and instancebaseline: u32 decompressedSize,
// u32 compressedSize, then a 4-byte "SNAP"/"LZSS" tag, then the stream. This was
// copied at each call site before; keeping one copy is what makes "the same
// payload decodes the same way for every table" a property of the code rather
// than of three parallel edits.
StringTablePayloadInfo decodeStringTablePayload(const std::vector<std::uint8_t>& payload,
                                                bool compressed,
                                                std::vector<std::uint8_t>& decodedPayload) {
  StringTablePayloadInfo info;
  if (!compressed) {
    info.decoded = true;
    return info;
  }
  if (payload.size() < 12u) return info;
  info.decompressedBytes = readLe32(payload, 0u);
  info.compressedBytes = readLe32(payload, 4u);
  info.magic = readLe32(payload, 8u);
  const bool snap = payload[8] == 'S' && payload[9] == 'N' && payload[10] == 'A' && payload[11] == 'P';
  const bool lzss = payload[8] == 'L' && payload[9] == 'Z' && payload[10] == 'S' && payload[11] == 'S';
  if (!(snap || lzss) || info.compressedBytes < 4u) return info;
  if (12u + static_cast<std::size_t>(info.compressedBytes - 4u) > payload.size()) return info;
  if (info.decompressedBytes > 100u * 1024u * 1024u) return info;
  info.decoded = snap
    ? decodeSnappyRaw(payload, 12u, info.compressedBytes - 4u, info.decompressedBytes, decodedPayload)
    : decodeLzss(payload, 12u, info.compressedBytes - 4u, info.decompressedBytes, decodedPayload);
  info.usedDecompressed = info.decoded;
  return info;
}

void recordInstanceBaselineEntry(const std::string& text, std::vector<std::uint8_t> raw,
    DemoNetworkSummary& summary) {
  std::uint32_t classId = 0;
  bool validClassId = !text.empty();
  if (validClassId) {
    for (const char character : text) {
      if (character < '0' || character > '9') { validClassId = false; break; }
      const std::uint32_t digit = static_cast<std::uint32_t>(character - '0');
      if (classId > (std::numeric_limits<std::uint16_t>::max() - digit) / 10u) {
        validClassId = false;
        break;
      }
      classId = classId * 10u + digit;
    }
  }
  if (!validClassId) {
    ++summary.instanceBaselineClassParseFailures;
    return;
  }
  ++summary.instanceBaselineClassTextCount;
  summary.instanceBaselines[static_cast<std::uint16_t>(classId)] = std::move(raw);
  ++summary.instanceBaselineEntryCount;
}

bool readInstanceBaselinePayload(const std::vector<std::uint8_t>& payload, std::uint32_t entryCount,
    std::uint32_t maxEntries, bool fixedUserData, std::uint32_t fixedUserDataBits,
    DemoNetworkSummary& summary) {
  MessageBits data(payload);
  std::uint32_t indexBits = 0;
  const std::uint32_t boundedMaxEntries = std::max<std::uint32_t>(1u, maxEntries);
  while ((1u << indexBits) < boundedMaxEntries && indexBits < 31u) ++indexBits;
  std::int32_t lastIndex = -1;
  std::vector<std::string> history;
  history.reserve(std::min<std::uint32_t>(entryCount, 32u));
  for (std::uint32_t entry = 0; entry < entryCount; ++entry) {
    std::uint32_t sequential = 0, index = 0;
    if (!data.read(1, sequential)) return false;
    if (sequential) index = static_cast<std::uint32_t>(lastIndex + 1);
    else if (!data.read(indexBits, index)) return false;
    lastIndex = static_cast<std::int32_t>(index);

    std::string text;
    std::uint32_t hasText = 0;
    if (!data.read(1, hasText)) return false;
    if (hasText) {
      std::uint32_t hasHistory = 0;
      if (!data.read(1, hasHistory)) return false;
      if (hasHistory) {
        std::uint32_t historyIndex = 0, copyCount = 0;
        if (!data.read(5, historyIndex) || !data.read(5, copyCount)) return false;
        std::string rest;
        if (!data.readString(rest, 4096)) return false;
        if (historyIndex < history.size() && copyCount <= history[historyIndex].size()) {
          text = history[historyIndex].substr(0, copyCount);
          text += rest;
        } else {
          text = std::move(rest);
        }
      } else if (!data.readString(text, 4096)) {
        return false;
      }
    }
    history.push_back(text);
    if (history.size() > 32u) history.erase(history.begin());

    std::uint32_t hasUserData = 0;
    if (!data.read(1, hasUserData)) return false;
    if (hasUserData) {
      std::uint32_t userDataBits = fixedUserData ? fixedUserDataBits : 0;
      if (!fixedUserData) {
        std::uint32_t userDataBytes = 0;
        // No 1024-byte cap: the length field is 14 bits and the reference
        // parser imposes no bound on it. Over-long values are still rejected,
        // by the per-bit read below running off the end of the payload.
        if (!data.read(14, userDataBytes)) return false;
        if (userDataBytes > summary.stringTableUserDataMaxBytes) {
          summary.stringTableUserDataMaxBytes = userDataBytes;
        }
        userDataBits = userDataBytes * 8u;
      }
      std::vector<std::uint8_t> raw((static_cast<std::size_t>(userDataBits) + 7u) / 8u, 0);
      for (std::uint32_t bit = 0; bit < userDataBits; ++bit) {
        std::uint32_t value = 0;
        if (!data.read(1, value)) return false;
        raw[bit / 8u] |= static_cast<std::uint8_t>(value << (bit % 8u));
      }
      // instancebaseline's text is the server class id; its entry index is not.
      recordInstanceBaselineEntry(text, std::move(raw), summary);
    }
  }
  return true;
}
} // namespace

bool readDemoStringTablesPayload(const std::vector<std::uint8_t>& payload, DemoNetworkSummary& summary) {
  MessageBits bits(payload);
  std::uint32_t tableCount = 0;
  if (!bits.read(8, tableCount) || tableCount > 64u) return false;
  for (std::uint32_t table = 0; table < tableCount; ++table) {
    std::string name;
    std::uint32_t entryCount = 0;
    if (!bits.readString(name) || !bits.read(16, entryCount)) return false;
    const bool isInstanceBaseline = name == "instancebaseline";
    for (std::uint32_t entry = 0; entry < entryCount; ++entry) {
      std::string text;
      std::uint32_t hasUserData = 0;
      if (!bits.readString(text) || !bits.read(1, hasUserData)) return false;
      std::vector<std::uint8_t> raw;
      if (hasUserData) {
        std::uint32_t userBytes = 0;
        // 16-bit length field, and the reference parser imposes no cap on it.
        // instancebaseline entry 3 of koth_bagel_rc13 is 7669 bytes; the old
        // `> 1024u` guard rejected it and reported the whole dem_stringtables
        // packet as malformed. Bounded here by the payload length instead.
        if (!bits.read(16, userBytes)) return false;
        if (userBytes > summary.stringTableUserDataMaxBytes) {
          summary.stringTableUserDataMaxBytes = userBytes;
        }
        if (static_cast<std::size_t>(userBytes) * 8u > bits.remaining()) return false;
        raw.assign(userBytes, 0);
        for (std::uint32_t bit = 0; bit < userBytes * 8u; ++bit) {
          std::uint32_t value = 0;
          if (!bits.read(1, value)) return false;
          raw[bit / 8u] |= static_cast<std::uint8_t>(value << (bit % 8u));
        }
      }
      if (isInstanceBaseline && hasUserData) recordInstanceBaselineEntry(text, std::move(raw), summary);
    }
    std::uint32_t hasClientEntries = 0;
    if (!bits.read(1, hasClientEntries)) return false;
    if (hasClientEntries) {
      std::uint32_t clientCount = 0;
      if (!bits.read(16, clientCount)) return false;
      for (std::uint32_t entry = 0; entry < clientCount; ++entry) {
        std::string text;
        std::uint32_t hasUserData = 0;
        if (!bits.readString(text) || !bits.read(1, hasUserData)) return false;
        if (hasUserData) {
          std::uint32_t userBytes = 0;
          // Same 16-bit length field, same removed cap.
          if (!bits.read(16, userBytes)) return false;
          if (userBytes > summary.stringTableUserDataMaxBytes) {
            summary.stringTableUserDataMaxBytes = userBytes;
          }
          if (!bits.skip(static_cast<std::size_t>(userBytes) * 8u)) return false;
        }
      }
    }
    if (summary.stringTableCount < 32u) {
      summary.stringTableById[static_cast<std::uint32_t>(summary.stringTableCount)] = name;
      summary.stringTableMaxEntries[static_cast<std::uint32_t>(summary.stringTableCount)] = entryCount;
    }
    if (summary.stringTableNames.size() < 32u) summary.stringTableNames.push_back(name);
    if (name == "soundprecache") {
      summary.soundPrecacheTableId = static_cast<std::uint32_t>(summary.stringTableCount);
      summary.soundPrecacheMaxEntries = entryCount;
    }
    ++summary.stringTableCount;
  }
  return true;
}

bool readCreateStringTable(MessageBits& bits, DemoNetworkSummary& summary) {
  std::string name;
  std::uint32_t maxEntries = 0, entryCount = 0, payloadBits = 0, fixedUserData = 0, fixedUserDataSize = 0, fixedUserDataBits = 0, compressed = 0;
  if (!bits.readString(name) || !bits.read(16, maxEntries)) return false;
  std::uint32_t entryIndexBits = 0;
  const std::uint32_t boundedMaxEntries = std::max<std::uint32_t>(1u, maxEntries);
  while ((1u << entryIndexBits) < boundedMaxEntries && entryIndexBits < 31u) ++entryIndexBits;
  if (!bits.read(entryIndexBits + 1u, entryCount)) return false;
  if ((summary.networkProtocol > 23 && !bits.readVarInt(payloadBits)) ||
      (summary.networkProtocol <= 23 && !bits.read(20, payloadBits))) return false;
  if (!bits.read(1, fixedUserData)) return false;
  if (fixedUserData && (!bits.read(12, fixedUserDataSize) || !bits.read(4, fixedUserDataBits))) return false;
  if (!bits.read(1, compressed) || payloadBits > bits.remaining()) return false;
  std::vector<std::uint8_t> payload((static_cast<std::size_t>(payloadBits) + 7u) / 8u, 0);
  for (std::size_t bit = 0; bit < payloadBits; ++bit) {
    std::uint32_t value = 0;
    if (!bits.read(1, value)) return false;
    payload[bit / 8u] |= static_cast<std::uint8_t>(value << (bit % 8u));
  }
  // soundprecache and modelprecache share the whole create path: the same
  // compressed wrapper, the same entry encoding, the same fixed-user-data rules.
  // Only the table id and the destination differ. modelprecache used to have no
  // arm here at all, which is why every entity carried an m_nModelIndex and no
  // model path.
  if (name == "soundprecache" || name == "modelprecache") {
    const bool isModelPrecache = name == "modelprecache";
    std::unordered_map<std::uint16_t, std::string>& destination =
      isModelPrecache ? summary.modelPrecache : summary.soundPrecache;
    std::size_t& failures = isModelPrecache
      ? summary.modelPrecacheDecodeFailures : summary.soundPrecacheDecodeFailures;
    std::vector<std::uint8_t> decodedPayload;
    const auto info = decodeStringTablePayload(payload, compressed != 0, decodedPayload);
    const std::vector<std::uint8_t>& tablePayload = info.usedDecompressed ? decodedPayload : payload;
    if (!info.decoded) {
      ++failures;
    } else if (!walkPrecacheTablePayload(tablePayload, entryCount, entryIndexBits,
                                        fixedUserData != 0u, fixedUserDataBits,
                                        summary, destination)) {
      ++failures;
    }
  }
  if (name == "instancebaseline") {
    if (compressed) ++summary.instanceBaselineCompressedCount;
    std::vector<std::uint8_t> decodedPayload;
    const auto info = decodeStringTablePayload(payload, compressed != 0, decodedPayload);
    // The declared sizes and magic are recorded whenever the 12-byte header is
    // present, even if the payload is then rejected -- that is what the old
    // inline block did, and these counters are part of the P0 evidence.
    if (compressed && payload.size() >= 12u) {
      summary.instanceBaselineDecompressedBytes = info.decompressedBytes;
      summary.instanceBaselineCompressedBytes = info.compressedBytes;
      summary.instanceBaselineMagic = info.magic;
    }
    const std::vector<std::uint8_t>& tablePayload = info.usedDecompressed ? decodedPayload : payload;
    if (!info.decoded || !readInstanceBaselinePayload(tablePayload, entryCount, maxEntries, fixedUserData != 0, fixedUserDataBits, summary)) {
      ++summary.instanceBaselineDecodeFailures;
    }
  }
  if (summary.stringTableCount < 32u) {
    summary.stringTableById[static_cast<std::uint32_t>(summary.stringTableCount)] = name;
    summary.stringTableMaxEntries[static_cast<std::uint32_t>(summary.stringTableCount)] = maxEntries;
  }
  if (name == "soundprecache") {
    summary.soundPrecacheTableId = static_cast<std::uint32_t>(summary.stringTableCount);
    summary.soundPrecacheMaxEntries = maxEntries;
    summary.soundPrecacheFixedBits = fixedUserData ? fixedUserDataBits : 0u;
    summary.soundPrecacheCompressed = compressed != 0;
  } else if (name == "modelprecache") {
    summary.modelPrecacheTableId = static_cast<std::uint32_t>(summary.stringTableCount);
    summary.modelPrecacheMaxEntries = maxEntries;
    summary.modelPrecacheFixedBits = fixedUserData ? fixedUserDataBits : 0u;
    summary.modelPrecacheCompressed = compressed != 0;
  }
  ++summary.stringTableCount;
  if (summary.stringTableNames.size() < 32) summary.stringTableNames.push_back(name);
  return true;
}

bool buildAssetReferenceList(DemoNetworkSummary& summary, std::vector<AssetReference>& references) {
  references.clear();
  summary.assetModelIndexKnown = summary.assetWeaponKnown = summary.assetItemDefKnown = 0;
  summary.assetPaintKitKnown = summary.assetSkinKnown = summary.assetQualityKnown = 0;
  summary.assetIdentityUnknown = 0;
  summary.assetModelPathKnown = summary.assetWeaponClassKnown = 0;
  summary.assetModelPathFromPrecache = summary.assetModelIndexUnresolved = 0;
  summary.assetModelIndexZero = 0;
  summary.assetModelIndexOutOfRange = 0;
  summary.assetModelIndexUnresolvedMax = -1;
  summary.assetWorldModelIndexKnown = 0;
  summary.assetWorldModelIndexResolved = 0;
  summary.assetWorldModelIndexZero = 0;
  summary.assetWorldModelIndexUnresolved = 0;
  summary.assetWorldModelIndexOutOfRange = 0;
  summary.assetWorldModelIndexUnresolvedMax = -1;
  summary.assetModelPathFromWorldModelIndex = 0;
  summary.assetModelPathWorldModelOnly = 0;
  summary.assetWeaponViewModelIndexAgrees = 0;
  summary.assetWeaponViewModelIndexZero = 0;
  summary.assetWeaponViewModelIndexDiffers = 0;
  for (std::size_t entityIndex = 0; entityIndex < summary.entityStates.size() && entityIndex < 2048u; ++entityIndex) {
    const auto& state = summary.entityStates[entityIndex];
    if (state.classId < 0) continue;
    AssetReference reference;
    reference.entityIndex = static_cast<std::uint16_t>(entityIndex);
    reference.classId = state.classId;
    for (const auto& classSchema : summary.serverClassSchemas) {
      if (classSchema.id == static_cast<std::uint32_t>(state.classId)) {
        reference.className = classSchema.name;
        break;
      }
    }
    auto findInt = [&state](std::initializer_list<const char*> names, bool& known, std::int64_t& target) {
      for (const auto* suffix : names) {
        for (const auto& [name, value] : state.properties) {
          if ((name == suffix || (name.size() > std::strlen(suffix) &&
              name.compare(name.size() - std::strlen(suffix), std::strlen(suffix), suffix) == 0)) &&
              value.type == SendPropType::Int) {
            known = true;
            target = value.intValue;
            return;
          }
        }
      }
    };
    auto findString = [&state](std::initializer_list<const char*> names, bool& known, std::string& target) {
      for (const auto* suffix : names) {
        for (const auto& [name, value] : state.properties) {
          if ((name == suffix || (name.size() > std::strlen(suffix) &&
              name.compare(name.size() - std::strlen(suffix), std::strlen(suffix), suffix) == 0)) &&
              value.type == SendPropType::String && !value.stringValue.empty()) {
            known = true;
            target = value.stringValue;
            return;
          }
        }
      }
    };
    findString({"m_ModelName", "m_iszModel", "m_szModel"}, reference.hasModelPath, reference.modelPath);
    findString({"m_iClassName", "m_szClassName"}, reference.hasWeaponClass, reference.weaponClass);
    findInt({"m_nModelIndex", "m_iModelIndex"}, reference.hasModelIndex, reference.modelIndex);
    findInt({"m_iWorldModelIndex"}, reference.hasWorldModelIndex, reference.worldModelIndex);
    findInt({"m_iViewModelIndex"}, reference.hasViewModelIndex, reference.viewModelIndex);
    for (const auto& [name, value] : state.properties) {
      if (value.type != SendPropType::String || value.stringValue.empty()) continue;
      if (name == "m_ModelName" || name == "m_iszModelName" || name.find("ModelName") != std::string::npos) {
        reference.hasModelPath = true;
        reference.modelPath = value.stringValue;
        break;
      }
    }
    findInt({"m_hActiveWeapon", "m_hWeapon"}, reference.hasWeapon, reference.weapon);
    // A weapon carries two model indices and they name different things. The
    // one DT_BaseEntity.m_nModelIndex holds is the first-person composite: on
    // the POV demo at server tick 55418 every held weapon reads
    // m_nModelIndex == m_iViewModelIndex == its class's c_*_arms model (engineer
    // 1097, medic 1060, spy 1088, pyro 1079, demo 1050), while
    // m_iWorldModelIndex holds the weapon itself (c_pistol 255, c_medigun 261,
    // c_knife 240, c_ham 605). The world pass wants the second one -- an arms
    // model standing where a weapon belongs is what "the weapon's world model is
    // not wired" looked like -- so the world index is tried first. This block is
    // additive: it only ever fills a path the m_nModelIndex block below could not
    // have filled differently, and every bucket it counts has its own counter so
    // that no reading the P1 round recorded had to move for it.
    // The one lookup both index routes go through. Returning nullptr covers every
    // way an index fails to name a model -- the 0 sentinel, Source's negative
    // sentinels arriving as unsigned 32-bit patterns, and an in-range value the
    // table never declared -- so "would the other route have produced a path" is
    // one call rather than four conditions written twice.
    auto precachePathFor = [&summary](std::int64_t index) -> const std::string* {
      if (index < 1 || index > 0xffff) return nullptr;
      const auto found = summary.modelPrecache.find(static_cast<std::uint16_t>(index));
      if (found == summary.modelPrecache.end() || found->second.empty()) return nullptr;
      return &found->second;
    };
    if (reference.hasWorldModelIndex) {
      ++summary.assetWorldModelIndexKnown;
      if (reference.hasViewModelIndex && reference.hasModelIndex) {
        // Both slots name the first-person composite, so where both are set they
        // agree. Where they do not, the view slot is 0: the POV demo's syringe
        // gun (249/0) and Crusader's Crossbow (381/0) are that case, and they are
        // the only shape it takes across both demos. Splitting "unset" from
        // "conflicting" is what makes the claim falsifiable -- a real conflict
        // would land in `Differs`, which is the counter that must stay 0.
        if (reference.viewModelIndex == reference.modelIndex) {
          ++summary.assetWeaponViewModelIndexAgrees;
        } else if (reference.viewModelIndex == 0) {
          ++summary.assetWeaponViewModelIndexZero;
        } else {
          ++summary.assetWeaponViewModelIndexDiffers;
        }
      }
      if (reference.worldModelIndex == 0) {
        // Unset on the instance baseline, so the property being present at all
        // means a packet wrote a real value; 0 can only be an explicit clear.
        ++summary.assetWorldModelIndexZero;
      } else if (reference.worldModelIndex >= 1 && reference.worldModelIndex <= 0xffff) {
        if (const auto* worldPath = precachePathFor(reference.worldModelIndex)) {
          ++summary.assetWorldModelIndexResolved;
          if (!reference.hasModelPath) {
            reference.hasModelPath = true;
            reference.modelPath = *worldPath;
            reference.modelPathFromPrecache = true;
            reference.modelPathFromWorldModelIndex = true;
            ++summary.assetModelPathFromWorldModelIndex;
            // Did the m_nModelIndex route have anything to offer for this entity?
            // If not, this path exists *because* of the world route, which is
            // exactly the quantity assetModelPathKnown moves by.
            if (precachePathFor(reference.modelIndex) == nullptr) {
              ++summary.assetModelPathWorldModelOnly;
            }
          }
        } else {
          ++summary.assetWorldModelIndexUnresolved;
          if (reference.worldModelIndex > summary.assetWorldModelIndexUnresolvedMax) {
            summary.assetWorldModelIndexUnresolvedMax = reference.worldModelIndex;
          }
        }
      } else {
        ++summary.assetWorldModelIndexOutOfRange;
      }
    }
    // A real Source demo does not send the model path as an entity property: the
    // entity carries m_nModelIndex and the path lives in the modelprecache
    // string table. Before this lookup every reference had an index and no path,
    // and ModelLoader::buildRenderRequests dropped all of them -- 673 references
    // in, 0 render requests out, on both a POV and a SourceTV demo. The property
    // route is tried first so a demo that does send the name still wins; the
    // precache route only fills the gap.
    if (!reference.hasModelPath && reference.hasModelIndex) {
      if (reference.modelIndex == 0) {
        // Source's "no model" sentinel, and modelprecache does not declare
        // index 0.
        ++summary.assetModelIndexZero;
      } else if (reference.modelIndex >= 1 && reference.modelIndex <= 0xffff) {
        const auto found = summary.modelPrecache.find(
          static_cast<std::uint16_t>(reference.modelIndex));
        if (found != summary.modelPrecache.end() && !found->second.empty()) {
          reference.hasModelPath = true;
          reference.modelPath = found->second;
          reference.modelPathFromPrecache = true;
        } else {
          // In range but the table never declared it. This is the count that
          // must be zero on a healthy demo.
          ++summary.assetModelIndexUnresolved;
          if (reference.modelIndex > summary.assetModelIndexUnresolvedMax) {
            summary.assetModelIndexUnresolvedMax = reference.modelIndex;
          }
        }
      } else {
        // Outside [1, 0xffff], so it never named a table entry. The local demos
        // send the unset sentinel as 0xFFFFFFEA (-22) / 0xFFFFFFFC (-4); those
        // must not be counted as misses or they would drown the real ones.
        ++summary.assetModelIndexOutOfRange;
      }
      // Never guess a path here. An unresolved index stays unresolved and is
      // counted, so "the table was missing an entry" cannot hide behind a
      // plausible-looking fallback model.
    }
    findInt({"m_iItemDefinitionIndex"}, reference.hasItemDefIndex, reference.itemDefIndex);
    findInt({"m_nFallbackPaintKit", "m_iPaintKit"}, reference.hasPaintKit, reference.paintKit);
    findInt({"m_nSkin"}, reference.hasSkin, reference.skin);
    findInt({"m_iEntityQuality"}, reference.hasQuality, reference.quality);
    if (reference.hasModelIndex) ++summary.assetModelIndexKnown;
    if (reference.hasWeapon) ++summary.assetWeaponKnown;
    if (reference.hasItemDefIndex) ++summary.assetItemDefKnown;
    if (reference.hasPaintKit) ++summary.assetPaintKitKnown;
    if (reference.hasSkin) ++summary.assetSkinKnown;
    if (reference.hasQuality) ++summary.assetQualityKnown;
    if (reference.hasModelPath) ++summary.assetModelPathKnown;
    if (reference.modelPathFromPrecache) ++summary.assetModelPathFromPrecache;
    if (reference.hasWeaponClass) ++summary.assetWeaponClassKnown;
    if (!reference.hasModelPath && !reference.hasWeaponClass && !reference.hasModelIndex &&
        !reference.hasWeapon && !reference.hasItemDefIndex && !reference.hasPaintKit &&
        !reference.hasSkin && !reference.hasQuality) {
      ++summary.assetIdentityUnknown;
    }
    references.push_back(std::move(reference));
  }
  return true;
}

} // namespace tf2::native
