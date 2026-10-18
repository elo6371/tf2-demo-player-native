// vpk_query -- count entries under a prefix in the TF2 archives main.cpp opens.
// Throwaway diagnostic: answers "does any open archive still hold materials/maps/*"
// so the material-chain failure can be attributed to the right archive.
//
// The hit list used to stop at five. That cap is what made "is any shipped viewmodel
// a usable negative fixture?" unanswerable from here: `models/weapons/v_models` holds
// 37 entries and the tool printed 5 of them, so the only way to name the rest was to
// guess. Printing all of them costs nothing and turns that question into a sweep.
#include "vpk_archive.h"

#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

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
    tf2::native::VpkArchive archive;
    std::string error;
    if (!archive.open(tfRoot / name, &error)) {
      std::cout << std::filesystem::path(name).string() << ": OPEN FAILED\n";
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
