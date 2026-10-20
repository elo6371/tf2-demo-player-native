#pragma once

// The installer lifecycle the package was missing: version, install, uninstall.
//
// WHAT THIS IS
//   A self-contained lifecycle implemented by the product executable itself, so
//   the thing that is installed is the thing that installs. There is no MSI, no
//   NSIS, no CPack artefact -- those carry their own signing, service and
//   registry story and would be a much larger change than this round can verify.
//   What is here is the part a release can already stand on:
//
//     --version           the package names its own version
//     --install           copy the payload beside the executable into an install
//                         directory, back up what it overwrites, and write a
//                         manifest of exactly what it laid down
//     --uninstall         remove the files the manifest says this package
//                         installed, restore what it overwrote, and leave
//                         everything else -- including user-modified files --
//                         exactly where it is
//     --install-status    compare the install directory against its manifest
//
// WHY THE PRODUCT AND NOT A SCRIPT
//   install_smoke_probe.ps1 already exists and checks a layout; it is a
//   diagnostic, not an installer: it cannot remove anything, and a user without
//   PowerShell execution policy rights cannot run it. Putting the lifecycle in
//   the executable also means the exit codes and the readings come from the same
//   binary a gate already knows how to launch and parse.
//
// WHAT IT DELIBERATELY DOES NOT DO
//   No registry, no services, no Start Menu, no file associations, no elevation.
//   The package is a directory that works from anywhere (T32 3.3), and an
//   installer that wrote machine state would make the uninstall question --
//   "what is left behind?" -- unanswerable from the package alone. Those are
//   recorded as still-missing release features, not silently dropped.

#include <filesystem>
#include <string>
#include <vector>

namespace tf2::native::installer {

// The directory inside the install directory that holds the lifecycle's own
// state. Named rather than derived so a later format change is one constant.
inline constexpr const char* kStateDirectory = ".uninstall";
inline constexpr const char* kPayloadDirectory = "payload";
inline constexpr const char* kVersionFileName = "VERSION.txt";

struct PackageInfo {
  std::string version;         // e.g. "0.1.0"
  int versionCode = 0;         // monotonic, for an upgrade comparison to use
  std::string buildUtc;        // when this executable was configured
  std::string sourceDateEpoch;
  std::string sourceTree;
};

PackageInfo packageInfo();

struct Options {
  std::filesystem::path installDir;
  bool dryRun = false;
  bool verbose = false;
};

struct Result {
  int exitCode = 0;
  // key=value lines, printed verbatim and in a fixed order. Every branch that
  // returns writes at least one, so a failed run is still a readable reading.
  std::vector<std::string> lines;
};

Result install(const Options& options);
Result uninstall(const Options& options);
Result status(const Options& options);
Result version(const Options& options);

} // namespace tf2::native::installer
