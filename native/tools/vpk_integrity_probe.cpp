// vpk_integrity_probe -- malformed VPK rejection and store precedence.
//
// WHY THIS BUILDS ITS OWN ARCHIVES
//   vpk_query answers questions about the shipped TF2 install. "A corrupt VPK is
//   refused with a named reason" cannot be shown against an archive that is not
//   corrupt, and a fixture that reimplements the parser proves only that two
//   implementations agree. So this probe writes VPK bytes it controls, then
//   drives VpkArchive::open/readEntry and AssetStoreChain -- the same code the
//   product runs -- and prints what each one answered.
//
// WHAT "SAFE" MEANS HERE
//   Not "returned false". A refusal has to (a) name a specific fault, (b) return
//   no bytes at all, and (c) not read from a location the tree did not intend.
//   The wraparound fixture is the sharp one: an inline entry naming offset
//   0xFFFFFFF0 used to have `treeEnd + offset` narrowed to 32 bits, wrap, and
//   read four bytes out of the middle of the directory tree. A parser that only
//   checks its bounds with 32-bit arithmetic reports success there.
//
// Usage: vpk_integrity_probe [--out <dir>]
#include "asset_lookup.h"
#include "vpk_archive.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <system_error>
#include <vector>

namespace {

constexpr std::uint32_t kSignature = 0x55aa1234;
constexpr std::uint16_t kDirectoryArchive = 0x7fff;

void emit(const std::string& key, const std::string& value) {
  std::cout << "{\"reading\":\"" << key << "\",\"value\":\"" << value << "\"}\n";
}
void emitNumber(const std::string& key, long long value) {
  emit(key, std::to_string(value));
}

std::uint64_t fnv1a(const std::vector<std::uint8_t>& bytes) {
  std::uint64_t hash = 1469598103934665603ull;
  for (const auto byte : bytes) {
    hash ^= byte;
    hash *= 1099511628211ull;
  }
  return hash;
}

std::string hex64(std::uint64_t value) {
  char buffer[17];
  std::snprintf(buffer, sizeof(buffer), "%016llx", static_cast<unsigned long long>(value));
  return buffer;
}

void put32(std::vector<std::uint8_t>& out, std::uint32_t value) {
  for (int i = 0; i < 4; ++i) out.push_back(static_cast<std::uint8_t>((value >> (i * 8)) & 0xff));
}
void put16(std::vector<std::uint8_t>& out, std::uint16_t value) {
  for (int i = 0; i < 2; ++i) out.push_back(static_cast<std::uint8_t>((value >> (i * 8)) & 0xff));
}
void putString(std::vector<std::uint8_t>& out, const std::string& value) {
  out.insert(out.end(), value.begin(), value.end());
  out.push_back(0);
}

struct FileSpec {
  std::string extension = "txt";
  std::string path = " ";          // " " is the VPK way of spelling "root"
  std::string filename = "hello";
  std::uint32_t crc = 0;
  std::uint16_t preloadLength = 0;
  std::uint16_t archiveIndex = kDirectoryArchive;
  std::uint32_t offset = 0;
  std::uint32_t length = 0;
  std::vector<std::uint8_t> preload;
  // Overrides used to describe a tree that lies about itself.
  bool overridePreloadLength = false;
  std::uint16_t preloadLengthField = 0;
};

// A v1 directory tree: extension, path, filename, 18 fixed bytes, terminator,
// preload, then the three empty terminators that close filename/path/extension.
std::vector<std::uint8_t> buildTree(const std::vector<FileSpec>& files) {
  std::vector<std::uint8_t> tree;
  for (std::size_t i = 0; i < files.size(); ++i) {
    const FileSpec& file = files[i];
    putString(tree, file.extension);
    putString(tree, file.path);
    putString(tree, file.filename);
    put32(tree, file.crc);
    put16(tree, file.overridePreloadLength ? file.preloadLengthField : file.preloadLength);
    put16(tree, file.archiveIndex);
    put32(tree, file.offset);
    put32(tree, file.length);
    put16(tree, 0xffff);
    tree.insert(tree.end(), file.preload.begin(), file.preload.end());
    // Close the filename loop, then the path loop.
    tree.push_back(0);
    tree.push_back(0);
  }
  tree.push_back(0);  // closes the extension loop; the parser requires it
  return tree;
}

std::vector<std::uint8_t> buildArchive(const std::vector<FileSpec>& files,
    std::vector<std::uint8_t> trailing = {}, bool overrideTreeSize = false,
    std::uint32_t treeSizeField = 0) {
  const auto tree = buildTree(files);
  std::vector<std::uint8_t> out;
  put32(out, kSignature);
  put32(out, 1);
  put32(out, overrideTreeSize ? treeSizeField : static_cast<std::uint32_t>(tree.size()));
  out.insert(out.end(), tree.begin(), tree.end());
  out.insert(out.end(), trailing.begin(), trailing.end());
  return out;
}

std::vector<std::uint8_t> bytesOf(const std::string& text) {
  return std::vector<std::uint8_t>(text.begin(), text.end());
}

bool writeFile(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes) {
  std::error_code error;
  std::filesystem::create_directories(path.parent_path(), error);
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  if (!file) return false;
  if (!bytes.empty()) {
    file.write(reinterpret_cast<const char*>(bytes.data()),
      static_cast<std::streamsize>(bytes.size()));
  }
  return static_cast<bool>(file);
}

std::string bytesToString(const std::vector<std::uint8_t>& bytes) {
  return std::string(bytes.begin(), bytes.end());
}

// One malformed case: build it, open it, and record the named reason.
struct Case {
  std::string name;
  std::filesystem::path directoryFile;
  std::string expectedReason;
  bool expectOpenSuccess = false;
  std::string entryPath;   // when set, readEntry is exercised after a success
  std::uint64_t inputHash = 0;
  std::uint64_t inputSize = 0;
};

} // namespace

int main(int argc, char** argv) {
  std::filesystem::path outDirectory = ".scratch/vpk-integrity";
  std::filesystem::path realRoot;
  std::size_t realLimit = 400;
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--out") == 0 && i + 1 < argc) {
      outDirectory = std::filesystem::u8path(argv[++i]);
    } else if (std::strcmp(argv[i], "--real") == 0 && i + 1 < argc) {
      realRoot = std::filesystem::u8path(argv[++i]);
    } else if (std::strcmp(argv[i], "--real-limit") == 0 && i + 1 < argc) {
      realLimit = static_cast<std::size_t>(std::stoull(argv[++i]));
    } else {
      std::cerr << "usage: vpk_integrity_probe [--out <dir>] [--real <tfRoot>] [--real-limit N]\n";
      return 2;
    }
  }

  // ---- real archives: the chain has to agree with the install -------------
  // The fixtures prove the policy on bytes this probe wrote. They cannot prove
  // the policy still resolves the shipped archives, because a chain that got the
  // order right and the archive list wrong passes every one of them. This mode
  // samples names out of the real trees and asks the chain to resolve them.
  if (!realRoot.empty()) {
    tf2::native::AssetStoreChain chain(realRoot);
    for (const auto* name : { L"pak01_dir.vpk", L"tf2_misc_dir.vpk", L"tf2_textures_dir.vpk",
                              L"tf2_sound_misc_dir.vpk", L"tf2_sound_vo_english_dir.vpk" }) {
      auto archive = std::make_shared<tf2::native::VpkArchive>();
      std::string reason;
      if (!archive->open(realRoot / name, &reason)) {
        emit("vpk.real.open." + std::filesystem::path(name).string(), reason);
        continue;
      }
      chain.addArchive(archive, realRoot / name);
    }
    emitNumber("vpk.real.archives", static_cast<long long>(chain.archiveCount()));
    if (chain.archiveCount() == 0) {
      emit("vpk.real.verdict", "no archive opened");
      return 1;
    }
    std::vector<std::string> sample;
    const auto step = std::max<std::size_t>(1, realLimit / (chain.archiveCount() * 4));
    for (const auto& path : chain.archivePaths()) {
      auto archive = std::make_shared<tf2::native::VpkArchive>();
      if (!archive->open(path)) continue;
      const auto names = archive->list("materials", ".vmt");
      for (std::size_t i = 0; i < names.size() && sample.size() < realLimit; i += step) {
        sample.push_back(names[i]);
      }
    }
    std::size_t resolved = 0, empty = 0, loose = 0, directory = 0, volume = 0;
    std::string firstOrigin;
    for (const auto& path : sample) {
      const auto looked = chain.lookup(path);
      if (!looked.found()) { ++empty; continue; }
      ++resolved;
      if (firstOrigin.empty()) firstOrigin = looked.origin;
      switch (looked.store) {
        case tf2::native::AssetStore::Loose: ++loose; break;
        case tf2::native::AssetStore::VpkDirectory: ++directory; break;
        case tf2::native::AssetStore::VpkVolume: ++volume; break;
        // No pakfile reader is registered in this mode, and a map's pakfile is
        // per-map, so the store cannot appear here.
        case tf2::native::AssetStore::Pakfile: break;
        case tf2::native::AssetStore::None: break;
      }
    }
    emitNumber("vpk.real.sampled", static_cast<long long>(sample.size()));
    emitNumber("vpk.real.resolved", static_cast<long long>(resolved));
    emitNumber("vpk.real.empty", static_cast<long long>(empty));
    emitNumber("vpk.real.byLoose", static_cast<long long>(loose));
    emitNumber("vpk.real.byDirectory", static_cast<long long>(directory));
    emitNumber("vpk.real.byVolume", static_cast<long long>(volume));
    emit("vpk.real.firstOrigin", firstOrigin);
    emit("vpk.real.verdict", sample.empty() ? "no names sampled"
      : (resolved == sample.size() ? "every sampled name resolved" : "some sampled names did not resolve"));
    if (sample.empty() || resolved != sample.size()) return 1;
    return 0;
  }

  std::error_code error;
  std::filesystem::remove_all(outDirectory, error);
  std::filesystem::create_directories(outDirectory, error);
  if (error) {
    std::cerr << "cannot prepare " << outDirectory.string() << "\n";
    return 2;
  }

  // ---- valid archives -----------------------------------------------------
  // Three stores, three fixtures, each holding the same logical resource under a
  // different physical arrangement.
  const std::string resourcePath = "materials/test/probe.txt";
  const std::string inlineText = "DIRECTORY-DATA";
  const std::string volumeText = "NUMBERED-VOLUME-DATA";
  const std::string preloadText = "PRELOAD";

  const auto inlineArchive = buildArchive({ FileSpec{ "txt", "materials/test", "probe",
      0, 0, kDirectoryArchive, 0, static_cast<std::uint32_t>(inlineText.size()), {} } },
    bytesOf(inlineText));
  const auto volumeArchive = buildArchive({ FileSpec{ "txt", "materials/test", "probe",
      0, 0, 0, 0, static_cast<std::uint32_t>(volumeText.size()), {} } });
  const auto preloadArchive = buildArchive({ FileSpec{ "txt", "materials/test", "probe",
      0, static_cast<std::uint16_t>(preloadText.size()), kDirectoryArchive, 0, 0,
      bytesOf(preloadText) } });

  const auto validInline = outDirectory / "valid-inline_dir.vpk";
  const auto validVolumeDir = outDirectory / "valid-volume_dir.vpk";
  const auto validVolumeData = outDirectory / "valid-volume_000.vpk";
  const auto validPreload = outDirectory / "valid-preload_dir.vpk";
  writeFile(validInline, inlineArchive);
  writeFile(validVolumeDir, volumeArchive);
  writeFile(validVolumeData, bytesOf(volumeText));
  writeFile(validPreload, preloadArchive);

  int failures = 0;
  const auto fail = [&](const std::string& what, const std::string& detail) {
    std::cerr << "FAIL " << what << ": " << detail << "\n";
    ++failures;
  };

  {
    tf2::native::VpkArchive archive;
    std::string reason;
    const bool opened = archive.open(validInline, &reason);
    emitNumber("vpk.valid.inline.open", opened ? 1 : 0);
    if (!opened) fail("valid inline archive did not open", reason);
    const auto entry = archive.readEntry(resourcePath);
    emit("vpk.valid.inline.bytes", bytesToString(entry.bytes));
    emit("vpk.valid.inline.store",
      entry.store == tf2::native::VpkArchive::Store::DirectoryData ? "vpk-directory"
      : entry.store == tf2::native::VpkArchive::Store::NumberedVolume ? "vpk-volume" : "preload");
    emitNumber("vpk.valid.inline.entries", static_cast<long long>(archive.entryCount()));
    if (!entry.ok() || bytesToString(entry.bytes) != inlineText) {
      fail("inline archive did not return its bytes", entry.error);
    }
    // Preload-only and inline-data must not be the same store label: one never
    // touches a data file, the other always does.
    if (entry.store != tf2::native::VpkArchive::Store::DirectoryData) {
      fail("inline archive reported the wrong store", "expected DirectoryData");
    }
  }
  {
    tf2::native::VpkArchive archive;
    std::string reason;
    const bool opened = archive.open(validVolumeDir, &reason);
    emitNumber("vpk.valid.volume.open", opened ? 1 : 0);
    if (!opened) fail("valid volume archive did not open", reason);
    const auto entry = archive.readEntry(resourcePath);
    emit("vpk.valid.volume.bytes", bytesToString(entry.bytes));
    emit("vpk.valid.volume.store",
      entry.store == tf2::native::VpkArchive::Store::NumberedVolume ? "vpk-volume" : "not-volume");
    if (!entry.ok() || bytesToString(entry.bytes) != volumeText) {
      fail("volume archive did not return its bytes", entry.error);
    }
  }
  {
    tf2::native::VpkArchive archive;
    std::string reason;
    const bool opened = archive.open(validPreload, &reason);
    emitNumber("vpk.valid.preload.open", opened ? 1 : 0);
    if (!opened) fail("valid preload archive did not open", reason);
    const auto entry = archive.readEntry(resourcePath);
    emit("vpk.valid.preload.bytes", bytesToString(entry.bytes));
    emit("vpk.valid.preload.store",
      entry.store == tf2::native::VpkArchive::Store::PreloadOnly ? "preload" : "not-preload");
    if (!entry.ok() || bytesToString(entry.bytes) != preloadText) {
      fail("preload archive did not return its bytes", entry.error);
    }
  }

  // ---- malformed archives -------------------------------------------------
  std::vector<Case> cases;

  const auto addCase = [&](const std::string& name, const std::string& fileName,
                           const std::vector<std::uint8_t>& bytes,
                           const std::string& expected) {
    Case one;
    one.name = name;
    one.directoryFile = outDirectory / fileName;
    one.expectedReason = expected;
    one.inputHash = fnv1a(bytes);
    one.inputSize = bytes.size();
    writeFile(one.directoryFile, bytes);
    cases.push_back(one);
  };

  // 1. empty file: no header at all.
  addCase("empty-file", "bad-empty-file_dir.vpk", {}, "empty VPK file");
  // 2. fewer bytes than a v1 header.
  {
    std::vector<std::uint8_t> bytes;
    put32(bytes, kSignature);
    put32(bytes, 1);
    addCase("truncated-header", "bad-truncated-header_dir.vpk", bytes, "truncated VPK header");
  }
  // 3. a header that is long enough but not a VPK.
  {
    std::vector<std::uint8_t> bytes;
    put32(bytes, 0xdeadbeef);
    put32(bytes, 1);
    put32(bytes, 4);
    put32(bytes, 0);
    addCase("unsupported-signature", "bad-signature_dir.vpk", bytes, "unsupported VPK header");
  }
  // 4. valid header, zero-length tree: an archive that declares no entries.
  {
    std::vector<std::uint8_t> bytes;
    put32(bytes, kSignature);
    put32(bytes, 1);
    put32(bytes, 0);
    addCase("empty-tree", "bad-empty-tree_dir.vpk", bytes, "empty VPK directory tree");
  }
  // 5. tree says it is longer than the file.
  {
    const auto bytes = buildArchive({ FileSpec{ "txt", " ", "a", 0, 0, kDirectoryArchive, 0, 0, {} } },
      {}, true, 4096);
    addCase("truncated-tree", "bad-truncated-tree_dir.vpk", bytes, "truncated VPK directory tree");
  }
  // 6. preload length larger than the tree: the tree's own length field lies.
  {
    FileSpec spec{ "txt", " ", "a", 0, 0, kDirectoryArchive, 0, 0, {} };
    spec.overridePreloadLength = true;
    spec.preloadLengthField = 4096;
    const auto bytes = buildArchive({ spec });
    addCase("invalid-preload-length", "bad-preload-length_dir.vpk", bytes,
      "truncated VPK preload data");
  }
  // 7. inline entry whose offset+length leaves the directory file.
  {
    const auto bytes = buildArchive({ FileSpec{ "txt", " ", "a", 0, 0, kDirectoryArchive, 0, 16, {} } });
    addCase("inline-offset-out-of-range", "bad-inline-range_dir.vpk", bytes,
      "VPK entry range outside directory archive");
  }
  // 8. the wraparound: offset 0xFFFFFFF0 with a short tree. 32-bit arithmetic
  //    wraps to a small number inside the tree and reads four bytes of it.
  {
    const auto bytes = buildArchive({ FileSpec{ "txt", " ", "a", 0, 0, kDirectoryArchive,
      0xfffffff0u, 4, {} } });
    addCase("inline-offset-wraps", "bad-inline-wrap_dir.vpk", bytes,
      "VPK entry range outside directory archive");
  }
  // 9. numbered volume that is not on disk.
  {
    const auto bytes = buildArchive({ FileSpec{ "txt", " ", "a", 0, 0, 7, 0, 4, {} } });
    addCase("missing-volume", "bad-missing-volume_dir.vpk", bytes, "missing VPK data archive");
  }
  // 10. numbered volume present but the entry points past its end. The volume
  //     holds a sentinel; a parser without a range check returns part of it.
  {
    const auto bytes = buildArchive({ FileSpec{ "txt", " ", "a", 0, 0, 3, 6, 8, {} } });
    addCase("volume-offset-out-of-range", "bad-volume-range_dir.vpk", bytes,
      "VPK entry range outside data archive");
    writeFile(outDirectory / "bad-volume-range_003.vpk", bytesOf("SENTINEL"));
  }

  std::size_t namedReasons = 0;
  std::size_t bytesLeaked = 0;
  for (const auto& one : cases) {
    tf2::native::VpkArchive archive;
    std::string reason;
    const bool opened = archive.open(one.directoryFile, &reason);
    std::string observed = reason;
    std::string stage = "open";
    std::size_t leaked = 0;
    if (opened) {
      // A tree can be acceptable as a whole and still refuse the entry it names:
      // "the volume is not on disk" is not knowable from the directory file, so
      // it surfaces when the entry is read. Which stage refused is a reading of
      // its own -- it says how much of the archive can be trusted.
      const auto entry = archive.readEntry(one.entryPath.empty() ? "a.txt" : one.entryPath);
      observed = entry.error.empty() ? "opened-without-refusal" : entry.error;
      stage = "read";
      leaked = entry.bytes.size();
    }
    emit("vpk.reject." + one.name, observed);
    emit("vpk.reject." + one.name + ".stage", stage);
    emitNumber("vpk.reject." + one.name + ".bytes", static_cast<long long>(leaked));
    emit("vpk.reject." + one.name + ".hash", hex64(one.inputHash));
    emitNumber("vpk.reject." + one.name + ".size", static_cast<long long>(one.inputSize));
    if (observed == one.expectedReason) ++namedReasons;
    else fail("reason for " + one.name, "expected '" + one.expectedReason + "', got '" + observed + "'");
    if (leaked != 0) { ++bytesLeaked; fail("bytes leaked for " + one.name, std::to_string(leaked)); }
  }
  emitNumber("vpk.reject.cases", static_cast<long long>(cases.size()));
  emitNumber("vpk.reject.named", static_cast<long long>(namedReasons));
  emitNumber("vpk.reject.leaked", static_cast<long long>(bytesLeaked));

  // ---- store precedence ---------------------------------------------------
  // Same logical resource in three stores, so which one answers is the reading.
  const auto precedence = outDirectory / "precedence";
  const auto looseRoot = precedence / "loose";
  const auto emptyLooseRoot = precedence / "no-loose";
  writeFile(looseRoot / "materials/test/probe.txt", bytesOf("LOOSE-FILE"));
  std::filesystem::create_directories(emptyLooseRoot, error);
  const auto firstArchivePath = precedence / "pak01_dir.vpk";
  const auto secondArchivePath = precedence / "pak02_dir.vpk";
  writeFile(firstArchivePath, inlineArchive);
  writeFile(secondArchivePath, volumeArchive);
  writeFile(precedence / "pak02_000.vpk", bytesOf(volumeText));

  const auto makeArchive = [](const std::filesystem::path& path) {
    auto archive = std::make_shared<tf2::native::VpkArchive>();
    std::string reason;
    const bool opened = archive->open(path, &reason);
    return opened ? archive : nullptr;
  };

  const auto runPrecedence = [&](const std::string& name,
      const std::filesystem::path& loose,
      const std::vector<std::filesystem::path>& archives,
      const std::string& wanted,
      const std::string& expectedStore,
      const std::string& expectedBytes) {
    tf2::native::AssetStoreChain chain(loose);
    for (const auto& path : archives) {
      auto archive = makeArchive(path);
      if (!archive) fail("archive did not open for precedence " + name, path.string());
      chain.addArchive(archive, path);
    }
    const auto looked = chain.lookup(wanted);
    emit("vpk.precedence." + name, tf2::native::assetStoreName(looked.store));
    emit("vpk.precedence." + name + ".origin", looked.origin);
    emit("vpk.precedence." + name + ".bytes", bytesToString(looked.bytes));
    if (std::string(tf2::native::assetStoreName(looked.store)) != expectedStore) {
      fail("precedence " + name, std::string("expected store ") + expectedStore + ", got "
        + tf2::native::assetStoreName(looked.store));
    }
    if (bytesToString(looked.bytes) != expectedBytes) {
      fail("precedence " + name + " bytes", bytesToString(looked.bytes));
    }
  };

  runPrecedence("loose-wins", looseRoot, { firstArchivePath, secondArchivePath },
    resourcePath, "loose", "LOOSE-FILE");
  runPrecedence("directory-before-volume", emptyLooseRoot,
    { firstArchivePath, secondArchivePath }, resourcePath, "vpk-directory", inlineText);
  runPrecedence("order-decides", emptyLooseRoot, { secondArchivePath, firstArchivePath },
    resourcePath, "vpk-volume", volumeText);
  runPrecedence("volume-only", emptyLooseRoot, { secondArchivePath },
    resourcePath, "vpk-volume", volumeText);

  // The pakfile is last: it answers only when no earlier store holds the name.
  {
    tf2::native::AssetStoreChain chain(looseRoot);
    chain.addArchive(makeArchive(firstArchivePath), firstArchivePath);
    chain.setPakfileReader([](const std::string& path, std::vector<std::uint8_t>& bytes,
                              std::string& reason) {
      if (path != "materials/test/pakonly.txt") { reason = "pakfile has no such entry"; return false; }
      bytes = bytesOf("PAKFILE");
      return true;
    });
    const auto fromPak = chain.lookup("materials/test/pakonly.txt");
    emit("vpk.precedence.pakfile", tf2::native::assetStoreName(fromPak.store));
    emit("vpk.precedence.pakfile.bytes", bytesToString(fromPak.bytes));
    if (fromPak.store != tf2::native::AssetStore::Pakfile) {
      fail("pakfile fallback", tf2::native::assetStoreName(fromPak.store));
    }
    const auto shadowed = chain.lookup(resourcePath);
    emit("vpk.precedence.pakfile-shadowed", tf2::native::assetStoreName(shadowed.store));
    if (shadowed.store != tf2::native::AssetStore::Loose) {
      fail("pakfile must not outrank loose", tf2::native::assetStoreName(shadowed.store));
    }
    const auto absent = chain.lookup("materials/test/absent.txt");
    emit("vpk.precedence.absent", tf2::native::assetStoreName(absent.store));
    emit("vpk.precedence.absent.reason", absent.error);
    if (absent.found()) fail("absent resource reported as found", absent.origin);
  }

  emit("vpk.summary", "cases=" + std::to_string(cases.size())
    + " named=" + std::to_string(namedReasons)
    + " leaked=" + std::to_string(bytesLeaked)
    + " failures=" + std::to_string(failures));
  return failures == 0 ? 0 : 1;
}
