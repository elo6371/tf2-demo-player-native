#pragma once

// The install manifest: the file that makes an installed directory an INSTALL
// rather than a staging copy.
//
// WHY THIS EXISTS (T37)
//   `cmake --install` produces a directory and nothing else: no record of what
//   was laid down, no version, no way to remove it without guessing which files
//   were ours. T32 recorded that as blocker #2 and #3 and left them to the
//   release task; T37 is that slice. The manifest is the smallest thing that
//   answers all three questions at once:
//     what was installed  <package> and the file list with sizes and hashes
//     which version       <package version, source date epoch, source tree
//     what may be removed installed-new and installed-changed files, and only
//                         those -- a file the user put in the install directory
//                         is listed as preserved and survives uninstall
//
// WHY PLAIN TEXT AND NOT BINARY/REGISTRY
//   The product is a portable directory (T32 3.3: no registry writes, no
//   services, no exe-relative reads). A manifest that needs a binary format or a
//   registry key to be readable would make the package less portable than it was,
//   and a gate could no longer diff it. It is line-oriented ASCII with a stated
//   format version, written with an explicit line order so two installs of the
//   same package produce byte-identical manifests.
//
// SELF-EXCLUSION
//   The manifest cannot contain its own hash. It therefore lists every file it
//   manages EXCEPT itself, and `uninstall` removes the manifest last. A file
//   list that included the manifest would need a fixed-point hash, which is not
//   a thing; saying so here is cheaper than a reader wondering why it is absent.

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace tf2::native::installer {

// A file the package laid down that was not already there.
inline constexpr const char* kStateNew = "new";
// A file the package overwrote. The bytes the file held BEFORE the install are
// kept under <manifest dir>/backup/, so uninstall can put them back. That is what
// lets the package install into a directory that already holds a copy of itself
// without that copy becoming unremovable.
inline constexpr const char* kStateChanged = "changed";
// A file that was already there with exactly the packaged bytes. The package did
// not create it, so it is left alone on uninstall.
inline constexpr const char* kStateUnchanged = "unchanged";
// A file the package did not lay down. Listed so "uninstall removed what it
// installed" is a claim about a complete inventory rather than about a subset
// chosen after the fact.
inline constexpr const char* kStatePreserved = "preserved";

inline constexpr const char* kManifestName = "install-manifest.txt";
inline constexpr const char* kManifestFormat = "tf2-demo-native-install-manifest";
inline constexpr int kManifestVersion = 1;
inline constexpr const char* kBackupDirectory = "backup";
inline constexpr const char* kInstalledFilesDirectory = "installed";

struct ManifestFile {
  std::string path;      // forward slashes, relative to the install directory
  std::string state;
  std::uint64_t bytes = 0;
  std::string sha256;
  std::string backup;    // path inside the package, relative; empty when none
};

struct Manifest {
  std::string package;
  std::string packageVersion;
  int packageVersionCode = 0;
  std::string sourceDateEpoch;
  std::string sourceTree;
  std::string installedUtc;
  std::string installDirectory;
  std::vector<ManifestFile> files;
};

// Deterministic serialisation: fixed key order, files sorted by path, one record
// per line. Two installs of the same package into different directories differ
// only in `installDirectory` and `installedUtc`.
std::string serialize(const Manifest& manifest);
bool parse(const std::string& text, Manifest* manifest, std::string* error);

bool writeManifest(const std::filesystem::path& file, const Manifest& manifest,
  std::string* error);
bool readManifest(const std::filesystem::path& file, Manifest* manifest,
  std::string* error);

// Lower-case hex SHA-256 of a file. Empty on a read error, with *error set.
std::string sha256File(const std::filesystem::path& file, std::string* error);
std::string sha256Bytes(const std::string& bytes);

// Forward-slash path relative to `root`, or empty when `path` is outside it.
std::string relativeTo(const std::filesystem::path& root,
  const std::filesystem::path& path);

// Every regular file under `root`, as relative forward-slash paths, sorted.
std::vector<std::string> listFiles(const std::filesystem::path& root);

// Windows FILETIME, so "a file the user touched after the install" is decidable
// without a second stat format.
bool lastWriteTime(const std::filesystem::path& file, std::uint64_t* value);
bool setLastWriteTime(const std::filesystem::path& file, std::uint64_t value);

} // namespace tf2::native::installer
