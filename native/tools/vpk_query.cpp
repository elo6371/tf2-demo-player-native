// vpk_query -- count entries under a prefix in the TF2 archives main.cpp opens.
// Throwaway diagnostic: answers "does any open archive still hold materials/maps/*"
// so the material-chain failure can be attributed to the right archive.
//
// The hit list used to stop at five. That cap is what made "is any shipped viewmodel
// a usable negative fixture?" unanswerable from here: `models/weapons/v_models` holds
// 37 entries and the tool printed 5 of them, so the only way to name the rest was to
// guess. Printing all of them costs nothing and turns that question into a sweep.
//
// T42: A FAILED OPEN NOW SAYS WHICH FAILURE IT WAS.
//   This tool used to print one string, `OPEN FAILED`, for a missing file, a file
//   the account may not read, and a file the reader refused. T31 recorded
//   "pak01_dir.vpk: OPEN FAILED" as "the archive will not open" on a machine where
//   the archive does not exist, and T37 had to write a second probe to take the
//   ambiguity out. Every listing line now starts with ONE stable category word:
//     <name>: ABSENT (ERROR_FILE_NOT_FOUND)
//     <name>: UNREADABLE (ERROR_ACCESS_DENIED)
//     <name>: UNSUPPORTED (unsupported VPK header)
//     <name>: TRUNCATED (truncated VPK directory tree)
//     <name>: OPEN_FAILED (<reader reason>)        any other refusal
//     <name>: <N> entries, <M> match '<prefix>'    opened
//   The category classifies the file and the reader's own reason, never a path or
//   a fixture name. The product's trace carries the same categories
//   (attribution=...) so an operator reading either one gets the same finding:
//   an absent archive is not a broken one. `--dump` is unchanged: its stdout is a
//   file's bytes, so it stays a reader and grows no diagnostics.
#include "vpk_archive.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {
// The reader's own "cannot read VPK directory file" is a stream failure: it does
// not say whether the path is wrong or the ACL is, and the two have different
// repairs. This asks the operating system directly.
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

std::string readPermission(const std::filesystem::path& path) {
  const HANDLE handle = CreateFileW(path.c_str(), GENERIC_READ,
    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (handle == INVALID_HANDLE_VALUE) return win32Error(GetLastError());
  CloseHandle(handle);
  return "readable";
}

bool isRegularFile(const std::filesystem::path& path) {
  std::error_code error;
  return std::filesystem::is_regular_file(path, error) && !error;
}

// The reader names eleven different refusals ("empty VPK file", "truncated VPK
// extension table", ...). They collapse into the four answers an operator acts
// on. Matching is on the reader's own strings, not on a path, so the same copy of
// a file gets the same category wherever it lives.
std::string classifyReaderReason(const std::string& reason) {
  if (reason == "unsupported VPK header") return "UNSUPPORTED";
  if (reason.rfind("truncated", 0) == 0) return "TRUNCATED";
  return "OPEN_FAILED";
}
} // namespace

int main(int argc, char** argv) {
  if (argc < 3) { std::cerr << "usage: vpk_query <tfRoot> <prefix> [ext]\n"
                               "       vpk_query <tfRoot> --dump <exact/path/in/archive>\n"; return 2; }
  const std::filesystem::path tfRoot = std::filesystem::u8path(argv[1]);
  const std::vector<std::wstring> names = {
    L"pak01_dir.vpk", L"tf2_misc_dir.vpk", L"tf2_textures_dir.vpk",
    L"tf2_sound_misc_dir.vpk", L"tf2_sound_vo_english_dir.vpk",
  };
  // --dump exists because listing a name is not the same as reading the file
  // that name points at. Enumerating the fields a material declares needs the
  // bytes, and a VMT that lives only in an archive cannot be opened by path.
  if (std::string(argv[2]) == "--dump") {
    if (argc < 4) { std::cerr << "--dump needs an archive-relative path\n"; return 2; }
    const std::string wanted = argv[3];
    for (const auto& name : names) {
      tf2::native::VpkArchive archive;
      std::string error;
      if (!archive.open(tfRoot / name, &error)) continue;
      const auto bytes = archive.read(wanted);
      if (bytes.empty()) continue;
      std::cout << "source=" << std::filesystem::path(name).string()
                << " bytes=" << bytes.size() << "\n";
      std::cout.write(reinterpret_cast<const char*>(bytes.data()),
                      static_cast<std::streamsize>(bytes.size()));
      std::cout << "\n";
      return 0;
    }
    std::cerr << "not found in any archive: " << wanted << "\n";
    return 1;
  }
  const std::string prefix = argv[2];
  const std::string ext = argc > 3 ? argv[3] : "";
  std::size_t grand = 0;
  for (const auto& name : names) {
    const auto path = tfRoot / name;
    tf2::native::VpkArchive archive;
    std::string error;
    if (!archive.open(path, &error)) {
      const auto label = std::filesystem::path(name).string();
      const auto permission = readPermission(path);
      const auto reason = error.empty() ? std::string("open failed without a reason") : error;
      if (!isRegularFile(path)) {
        std::cout << label << ": ABSENT (" << permission << ")\n";
      } else if (permission != "readable") {
        std::cout << label << ": UNREADABLE (" << permission << ")\n";
      } else {
        std::cout << label << ": " << classifyReaderReason(reason)
                  << " (" << reason << ")\n";
      }
      continue;
    }
    const auto hits = archive.list(prefix, ext);
    grand += hits.size();
    std::cout << std::filesystem::path(name).string() << ": " << archive.entryCount()
              << " entries, " << hits.size() << " match '" << prefix << "'";
    if (!ext.empty()) std::cout << " ext=" << ext;
    std::cout << "\n";
    for (std::size_t i = 0; i < hits.size(); ++i) std::cout << "    " << hits[i] << "\n";
  }
  std::cout << "total matches: " << grand << "\n";
  return 0;
}
