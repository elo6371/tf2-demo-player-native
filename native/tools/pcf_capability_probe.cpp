#include "vpk_archive.h"

#include <algorithm>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

namespace {
std::string hexPrefix(const std::vector<std::uint8_t>& bytes) {
  std::ostringstream out;
  for (std::size_t i = 0; i < std::min<std::size_t>(bytes.size(), 16); ++i) {
    if (i) out << ' ';
    out << std::hex << std::setw(2) << std::setfill('0') << static_cast<unsigned>(bytes[i]);
  }
  return out.str();
}

std::size_t countText(const std::vector<std::uint8_t>& bytes, const std::string& needle) {
  if (bytes.empty()) return 0;
  const std::string text(bytes.begin(), bytes.end());
  std::size_t count = 0;
  for (std::size_t at = 0; (at = text.find(needle, at)) != std::string::npos; at += needle.size()) ++count;
  return count;
}
}

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "usage: pcf_capability_probe <tf_directory>\n";
    return 2;
  }
  const std::filesystem::path tfRoot = argv[1];
  std::error_code error;
  const auto particlesRoot = tfRoot / "particles";
  std::size_t looseCount = 0;
  if (std::filesystem::is_directory(particlesRoot, error)) {
    for (const auto& entry : std::filesystem::recursive_directory_iterator(particlesRoot, error)) {
      if (error) break;
      if (entry.is_regular_file(error) && entry.path().extension() == ".pcf") ++looseCount;
    }
  }

  tf2::native::VpkArchive archive;
  const auto vpkPath = tfRoot / "tf2_misc_dir.vpk";
  std::string openError;
  const bool opened = archive.open(vpkPath, &openError);
  const auto entries = opened ? archive.list("particles", ".pcf") : std::vector<std::string>{};
  std::vector<std::uint8_t> prefix;
  std::string readError;
  if (!entries.empty()) prefix = archive.read(entries.front(), &readError);
  const std::string dmxHeader = "<!-- dmx encoding";
  const bool dmxHeaderCandidate = prefix.size() >= dmxHeader.size()
    && std::equal(dmxHeader.begin(), dmxHeader.end(), prefix.begin());
  const auto systemMarkers = countText(prefix, "particleSystemDefinition");
  const auto operatorMarkers = countText(prefix, "operator");
  std::cout << "pcf loose_count=" << looseCount
            << " vpk_status=" << (opened ? "opened" : "missing_or_invalid")
            << " vpk_pcf_count=" << entries.size()
            << " first_entry=" << (entries.empty() ? "" : entries.front())
            << " first_prefix_hex=" << hexPrefix(prefix)
            << " dmx_header_candidate=" << (dmxHeaderCandidate ? 1 : 0)
            << " system_markers=" << systemMarkers
            << " operator_markers=" << operatorMarkers
            << " header_validated=false parsed=false rendered=false mapping=false\n";
  if (!opened && !openError.empty()) std::cout << "vpk_error=" << openError << "\n";
  if (!entries.empty() && prefix.empty() && !readError.empty()) std::cout << "pcf_read_error=" << readError << "\n";
  return opened && !entries.empty() ? 0 : 1;
}
