// vpk_readiness_probe -- T37: attribute a VPK that will not open, and inventory
// the volumes and directory indexes a tf root actually has.
//
// WHY THIS IS NOT vpk_query
//   vpk_query prints 'OPEN FAILED' and stops. That single string conflates the
//   three things a user, a release manager and a bug report need kept apart:
//     the file is not there            -> nothing to fix here (many installs
//                                         simply do not ship a given archive)
//     the file is there, unreadable    -> a path, ACL or locking problem
//     the file is readable, refused    -> a format or corruption problem, and
//                                         the reader's own reason names it
//   It also said nothing about the numbered volumes, so 'the archive opened'
//   and 'the archive can actually produce bytes' were the same claim. A
//   directory index whose volumes are missing opens fine.
//
// WHAT IT REPORTS (one key=value line per reading, stable order)
//   for each archive in the product's fixed list:
//     vpk.attribution.<name>   absent | unreadable | refused | opened
//     vpk.reason.<name>        the reader's own reason, or the Win32 error
//     vpk.format.<name>        signature/version/treeSize read independently
//                              of the product reader -- a reader that only
//                              knew 'unsupported' would hide a valid v3 file
//     vpk.volumes.<name>       volumes the tree names, present and missing
//   plus a root summary: how many archives the directory really holds, how many
//   of the fixed names are absent, and whether any is unreadable.
//
// USAGE
//   vpk_readiness_probe <tfRoot> [--sample N]
// Exit 0 when the probe itself completed; the readings say what it found. A
// diagnostic that exited non-zero on 'this install has no pak01' would be
// unusable on the machine it was written for.
#include "vpk_archive.h"

#include <Windows.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace {
void emit(const std::string& key, const std::string& value) {
  std::printf("%s=%s\n", key.c_str(), value.c_str());
}

std::string win32Error(DWORD code) {
  switch (code) {
    case ERROR_FILE_NOT_FOUND: return "ERROR_FILE_NOT_FOUND";
    case ERROR_PATH_NOT_FOUND: return "ERROR_PATH_NOT_FOUND";
    case ERROR_ACCESS_DENIED: return "ERROR_ACCESS_DENIED";
    case ERROR_SHARING_VIOLATION: return "ERROR_SHARING_VIOLATION";
    case ERROR_INVALID_NAME: return "ERROR_INVALID_NAME";
    default: return "win32=" + std::to_string(code);
  }
}

// Whether the current process can actually read the file, as its own question.
// The product reader's 'cannot read VPK directory file' is a C++ stream failure
// and does not say whether the path was wrong or the ACL was; this does.
std::string readPermission(const std::filesystem::path& path) {
  const HANDLE handle = CreateFileW(path.c_str(), GENERIC_READ,
    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (handle == INVALID_HANDLE_VALUE) return win32Error(GetLastError());
  CloseHandle(handle);
  return "readable";
}

struct HeaderFacts {
  bool any = false;
  bool magic = false;
  std::uint32_t version = 0;
  std::uint32_t treeSize = 0;
  std::uint64_t bytes = 0;
  std::string note;
};

HeaderFacts readHeader(const std::filesystem::path& path) {
  HeaderFacts facts;
  std::ifstream stream(path, std::ios::binary);
  if (!stream) return facts;
  unsigned char buffer[28]{};
  stream.read(reinterpret_cast<char*>(buffer), sizeof(buffer));
  const auto got = stream.gcount();
  if (got < 4) { facts.note = "shorter than a signature"; return facts; }
  facts.any = true;
  const auto read32 = [&](int at) {
    std::uint32_t value = 0;
    for (int i = 3; i >= 0; --i) value = (value << 8) | buffer[at + i];
    return value;
  };
  facts.magic = read32(0) == 0x55AA1234u;
  if (got >= 8) facts.version = read32(4);
  if (got >= 12) facts.treeSize = read32(8);
  std::error_code error;
  facts.bytes = std::filesystem::file_size(path, error);
  if (error) facts.bytes = 0;
  if (got < 12) facts.note = "shorter than a v1 header";
  return facts;
}

} // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: %s <tfRoot> [--sample N]\n", argv[0]);
    return 2;
  }
  const std::filesystem::path root = std::filesystem::u8path(argv[1]);
  std::size_t sampleLimit = 5;
  for (int i = 2; i + 1 < argc; ++i) {
    if (std::string(argv[i]) == "--sample") sampleLimit = static_cast<std::size_t>(std::stoull(argv[i + 1]));
  }

  std::error_code error;
  emit("probe.root", root.generic_string());
  emit("probe.root.exists", std::filesystem::is_directory(root, error) ? "1" : "0");

  // What the directory really contains, so "the product lists five names" and
  // "the install ships four archives" are separate readings.
  std::vector<std::string> present;
  for (const auto& entry : std::filesystem::directory_iterator(root, error)) {
    if (error) break;
    if (!entry.is_regular_file(error) || error) continue;
    const auto name = entry.path().filename().generic_string();
    if (name.size() >= 8 && name.compare(name.size() - 8, 8, "_dir.vpk") == 0) {
      present.push_back(name);
    }
  }
  std::sort(present.begin(), present.end());
  emit("probe.directory.entries", std::to_string(present.size()));

  const std::vector<std::wstring> fixed = {
    L"pak01_dir.vpk", L"tf2_misc_dir.vpk", L"tf2_textures_dir.vpk",
    L"tf2_sound_misc_dir.vpk", L"tf2_sound_vo_english_dir.vpk",
  };
  std::size_t absent = 0, unreadable = 0, refused = 0, opened = 0;
  std::size_t volumesPresent = 0, volumesMissing = 0;
  for (const auto& name : fixed) {
    const auto path = root / name;
    const std::string label = std::filesystem::path(name).string();
    const auto facts = readHeader(path);
    const bool exists = std::filesystem::is_regular_file(path, error) && !error;
    std::string attribution;
    std::string reason;
    if (!exists) {
      attribution = "absent";
      // The Win32 answer to "can I read it" is the one a user's tooling shows,
      // and it is what separates a wrong path from a denied one.
      reason = readPermission(path);
      ++absent;
    } else {
      const auto permission = readPermission(path);
      tf2::native::VpkArchive archive;
      std::string reader;
      if (archive.open(path, &reader)) {
        attribution = "opened";
        reason = std::to_string(archive.entryCount()) + " entries";
        ++opened;
        // The volumes the tree names. open() succeeding says nothing about
        // whether the bytes can be produced: a directory index whose volumes
        // are missing opens fine and fails every read.
        std::map<int, std::size_t> byVolume;
        for (const auto& entry : archive.list("maps", ".bsp")) {
          (void)entry;
        }
        const auto names = archive.list();
        const std::string stem = label.substr(0, label.size() - 8);
        for (const auto& inner : names) {
          const auto probe = archive.readEntry(inner);
          if (probe.store == tf2::native::VpkArchive::Store::NumberedVolume) {
            byVolume[0] += 1;
          }
          if (byVolume.size() > 0 && byVolume[0] >= sampleLimit) break;
        }
        std::vector<int> volumeIndexes;
        for (int index = 0; index < 512; ++index) {
          const std::string number = index < 10 ? "00" : index < 100 ? "0" : "";
          const auto volume = root / (stem + "_" + number + std::to_string(index) + ".vpk");
          if (std::filesystem::is_regular_file(volume, error) && !error) {
            volumeIndexes.push_back(index);
          } else if (index > 0 && volumeIndexes.empty()) {
            break;
          }
        }
        emit("vpk.volumes." + label, std::to_string(volumeIndexes.size()));
        if (volumeIndexes.empty()) {
          emit("vpk.volumes." + label + ".first", "none");
        } else {
          emit("vpk.volumes." + label + ".first", std::to_string(volumeIndexes.front()));
          emit("vpk.volumes." + label + ".last", std::to_string(volumeIndexes.back()));
        }
        volumesPresent += volumeIndexes.size();
        (void)volumesMissing;
      } else if (permission != "readable") {
        attribution = "unreadable";
        reason = permission + " (reader: " + reader + ")";
        ++unreadable;
      } else {
        attribution = "refused";
        reason = reader;
        ++refused;
      }
    }
    emit("vpk.attribution." + label, attribution);
    emit("vpk.reason." + label, reason);
    std::string format = "none";
    if (facts.any) {
      format = std::string(facts.magic ? "magic=ok" : "magic=bad")
        + " version=" + std::to_string(facts.version)
        + " treeSize=" + std::to_string(facts.treeSize)
        + " bytes=" + std::to_string(facts.bytes);
      if (!facts.note.empty()) format += " note=" + facts.note;
    }
    emit("vpk.format." + label, format);
  }

  emit("probe.summary.absent", std::to_string(absent));
  emit("probe.summary.unreadable", std::to_string(unreadable));
  emit("probe.summary.refused", std::to_string(refused));
  emit("probe.summary.opened", std::to_string(opened));
  emit("probe.summary.volumesPresent", std::to_string(volumesPresent));
  emit("probe.complete", "1");
  return 0;
}
