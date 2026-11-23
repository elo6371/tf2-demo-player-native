// install_cli_probe -- drive the product's installer lifecycle from a script.
//
// WHY THIS IS A SEPARATE EXECUTABLE
//   The lifecycle itself (native/src/install_lifecycle.cpp) is product code: it
//   is linked into tf2_demo_native and is what a shipped package runs. This
//   probe is only the command line around it. A package is "these files, staged
//   next to the installer", so a gate stages them under
//   <install>/.uninstall/payload/ and this probe performs the install. That keeps
//   the payload an explicit, hashable input instead of a glob over whatever a
//   build directory happens to contain.
//
// USAGE
//   install_cli_probe --version
//   install_cli_probe --install   [--install-dir DIR] [--dry-run] [--verbose]
//   install_cli_probe --status    [--install-dir DIR]
//   install_cli_probe --uninstall [--install-dir DIR] [--dry-run]
//   install_cli_probe --install-safety-selftest
//   --install-dir defaults to the current working directory.
// Exit 0 when the operation reported PASS/VERIFIED, 2 when it reported a named
// failure.
//
// WHY THE SELF-TEST IS HERE (F0-d)
//   The two install defects QA-4 recorded are properties of install_lifecycle.cpp
//   and install_manifest.cpp, and both need a directory that is NOT the one the
//   product was installed into. Driving them in this process keeps the fixture an
//   explicit set of files -- a manifest this code writes with serialize() -- rather
//   than a shell script guessing at the line format, and it proves the refusal in
//   the bytes the product runs. Every case builds its own directory under the TEMP
//   directory and removes it afterwards: no real install location, no payload, no
//   GUI, and no host lock are involved.
#include "install_lifecycle.h"
#include "install_manifest.h"

#include <Windows.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

namespace installer = tf2::native::installer;

// Every check is counted. A case that stops early -- a fixture that could not be
// written, an exception out of std::filesystem -- therefore cannot read as a pass:
// the final verdict additionally requires cases == 4, so a harness that silently
// runs nothing fails instead of reporting "no failures".
struct SelfTestLog {
  int cases = 0;
  int checks = 0;
  int failures = 0;

  void check(bool ok, const std::string& what, const std::string& detail = std::string()) {
    ++checks;
    if (!ok) ++failures;
    std::printf("install-safety-selftest: check=%s result=%s", what.c_str(),
      ok ? "PASS" : "FAIL");
    if (!detail.empty()) std::printf(" detail=%s", detail.c_str());
    std::printf("\n");
    std::fflush(stdout);
  }
};

std::string firstLineStarting(const std::vector<std::string>& lines,
    const std::string& prefix) {
  for (const auto& line : lines) {
    if (line.rfind(prefix, 0) == 0) return line;
  }
  return {};
}

// The product narrates a refusal before it judges it: "uninstall=note
// could_not_remove=..." arrives first and "uninstall=INCOMPLETE ..." is the
// last line it writes. A reading that wants the verdict therefore has to take
// the LAST line with the prefix -- taking the first one read the note and
// reported the verdict as missing (measured on the A+B+D2 tree, 2026-11-11:
// verdict_incomplete and count_named FAIL with the note as detail).
std::string lastLineStarting(const std::vector<std::string>& lines,
    const std::string& prefix) {
  for (auto it = lines.rbegin(); it != lines.rend(); ++it) {
    if (it->rfind(prefix, 0) == 0) return *it;
  }
  return {};
}

bool hasLineStarting(const std::vector<std::string>& lines, const std::string& prefix) {
  return !firstLineStarting(lines, prefix).empty();
}

bool contains(const std::string& haystack, const std::string& needle) {
  return haystack.find(needle) != std::string::npos;
}

bool writeFixtureFile(const std::filesystem::path& file, const std::string& bytes) {
  std::error_code error;
  std::filesystem::create_directories(file.parent_path(), error);
  if (error) return false;
  std::ofstream stream(file, std::ios::binary | std::ios::trunc);
  if (!stream) return false;
  stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  return stream.good();
}

// The manifest is built with the product's own serialize(), so the fixture cannot
// disagree with the writer about a key, an order or a separator. Only the file
// records are chosen here.
installer::Manifest fixtureManifest(const std::filesystem::path& installDir) {
  installer::Manifest manifest;
  manifest.package = "tf2_demo_native";
  manifest.packageVersion = "0.0.0-selftest";
  manifest.packageVersionCode = 1;
  manifest.sourceDateEpoch = "0";
  manifest.sourceTree = "000000000000";
  manifest.sourceDirty = "0";
  manifest.sourceDirtyFiles = "0";
  manifest.installedUtc = "2026-11-11T00:00:00Z";
  manifest.installDirectory = installDir.u8string();
  return manifest;
}

bool writeFixtureManifest(const std::filesystem::path& installDir,
    const installer::Manifest& manifest) {
  std::error_code error;
  std::filesystem::create_directories(installDir / installer::kStateDirectory, error);
  if (error) return false;
  std::ofstream stream(
    installDir / installer::kStateDirectory / installer::kManifestName,
    std::ios::binary | std::ios::trunc);
  if (!stream) return false;
  const std::string text = installer::serialize(manifest);
  stream.write(text.data(), static_cast<std::streamsize>(text.size()));
  return stream.good();
}

// A manifest record path is checked by parse() as printable ASCII, so a fixture
// whose path is not ASCII would be refused by the OLD code too and the case would
// prove nothing. Say that out loud rather than counting a skip.
bool isAsciiPath(const std::filesystem::path& path) {
  for (const char c : path.u8string()) {
    const auto value = static_cast<unsigned char>(c);
    if (value < 0x20 || value > 0x7e) return false;
  }
  return true;
}

std::filesystem::path manifestFileOf(const std::filesystem::path& installDir) {
  return installDir / installer::kStateDirectory / installer::kManifestName;
}

// CONTROL CASE. Without it the three refusal cases could all pass by refusing
// everything, which is the "gate that proves nothing" shape: the ordinary
// uninstall has to still remove the file and still say PASS.
void caseNormalUninstall(SelfTestLog& log, const std::filesystem::path& root) {
  const auto dir = root / "normal";
  std::error_code error;
  std::filesystem::create_directories(dir, error);
  const auto file = dir / "hello.txt";
  const std::string content = "hello\n";
  const bool written = writeFixtureFile(file, content);
  installer::Manifest manifest = fixtureManifest(dir);
  installer::ManifestFile record;
  record.path = "hello.txt";
  record.state = installer::kStateNew;
  record.bytes = content.size();
  record.sha256 = installer::sha256File(file, nullptr);
  manifest.files.push_back(record);
  log.check(written && writeFixtureManifest(dir, manifest), "normal_fixture");
  installer::Options options;
  options.installDir = dir;
  const auto result = installer::uninstall(options);
  const std::string verdict = firstLineStarting(result.lines, "uninstall=");
  log.check(result.exitCode == 0, "normal_rc_zero",
    "rc=" + std::to_string(result.exitCode));
  log.check(contains(verdict, "uninstall=PASS"), "normal_verdict_pass", verdict);
  log.check(!std::filesystem::exists(file), "normal_file_removed");
  log.check(!std::filesystem::exists(manifestFileOf(dir)), "normal_manifest_removed");
}

// INS-1. Two shapes: a parent-relative record and a drive-qualified absolute one.
// The old parser accepted both -- ".." and "C:" are printable ASCII -- and
// uninstall then joined the record to the install directory, which on Windows
// replaces the whole path for an absolute one. The victim file starts OUTSIDE the
// install directory with the hash the record names, so the only thing standing
// between it and deletion is the containment check under test.
void caseEscape(SelfTestLog& log, const std::filesystem::path& root, bool absolute) {
  const std::string name = absolute ? "escape_absolute" : "escape_relative";
  const auto dir = root / (absolute ? "escape_absolute_dir" : "escape_relative_dir");
  std::error_code error;
  std::filesystem::create_directories(dir, error);
  const auto victim = absolute ? (root / "absolute-victim.txt") : (root / "escapee.txt");
  const std::string content = "this file is outside the install directory\n";
  const bool written = writeFixtureFile(victim, content);
  if (!isAsciiPath(victim)) {
    log.check(false, name + "_fixture_ascii",
      "the TEMP path is not ASCII, so this record path cannot be expressed");
    return;
  }
  installer::Manifest manifest = fixtureManifest(dir);
  installer::ManifestFile record;
  record.path = absolute ? victim.generic_u8string() : std::string("../escapee.txt");
  record.state = installer::kStateNew;
  record.bytes = content.size();
  record.sha256 = installer::sha256File(victim, nullptr);
  manifest.files.push_back(record);
  log.check(written && writeFixtureManifest(dir, manifest), name + "_fixture");
  installer::Options options;
  options.installDir = dir;
  const auto result = installer::uninstall(options);
  const std::string verdict = firstLineStarting(result.lines, "uninstall=");
  log.check(result.exitCode != 0, name + "_nonzero_rc",
    "rc=" + std::to_string(result.exitCode));
  log.check(hasLineStarting(result.lines, "uninstall=FAIL"), name + "_named_refusal",
    verdict);
  log.check(std::filesystem::exists(victim), name + "_victim_survived");
  log.check(std::filesystem::exists(manifestFileOf(dir)), name + "_manifest_kept");
}

// INS-2. The delete has to be refused by the operating system, not by a client
// check. FILE_ATTRIBUTE_READONLY does not do that here: MSVC's
// std::filesystem::remove clears the attribute and deletes the file (measured
// 2026-11-11 20:47 on this toolchain: attrs=0x1, remove()==true, file gone),
// even though a raw DeleteFileW on the same file returns FALSE with win32=5.
// A handle opened WITHOUT FILE_SHARE_DELETE is the form that fails for every
// caller on every volume: DeleteFileW gets ERROR_SHARING_VIOLATION (32) and the
// file stays until the last such handle closes. The old code printed a note,
// counted nothing, reported PASS with exit 0 and then deleted the manifest, so
// the leftover file became an orphan and a second uninstall answered
// "no_manifest".
struct HeldFile {
  HANDLE handle = INVALID_HANDLE_VALUE;
  HeldFile() = default;
  HeldFile(const HeldFile&) = delete;
  HeldFile& operator=(const HeldFile&) = delete;
  ~HeldFile() { close(); }
  bool open(const std::filesystem::path& file) {
    close();
    handle = CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
      OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    return handle != INVALID_HANDLE_VALUE;
  }
  void close() {
    if (handle != INVALID_HANDLE_VALUE) {
      CloseHandle(handle);
      handle = INVALID_HANDLE_VALUE;
    }
  }
  bool valid() const { return handle != INVALID_HANDLE_VALUE; }
};

void caseRemoveDenied(SelfTestLog& log, const std::filesystem::path& root) {
  const auto dir = root / "remove_denied";
  std::error_code error;
  std::filesystem::create_directories(dir, error);
  const auto file = dir / "locked.txt";
  const std::string content = "a file held without FILE_SHARE_DELETE cannot be deleted\n";
  const bool written = writeFixtureFile(file, content);
  installer::Manifest manifest = fixtureManifest(dir);
  installer::ManifestFile record;
  record.path = "locked.txt";
  record.state = installer::kStateNew;
  record.bytes = content.size();
  record.sha256 = installer::sha256File(file, nullptr);
  manifest.files.push_back(record);
  // The handle is closed by the destructor on every path out of this case,
  // including an exception from uninstall(), so no stale lock can survive a
  // failure and mask the next run. The fixture assertion therefore states what
  // the case needs: a valid handle, an unchanged file and a written manifest --
  // not "an attribute was set".
  HeldFile held;
  const bool opened = written && held.open(file);
  log.check(opened && held.valid() && std::filesystem::exists(file)
      && installer::sha256File(file, nullptr) == record.sha256
      && writeFixtureManifest(dir, manifest),
    "remove_denied_fixture");
  installer::Options options;
  options.installDir = dir;
  const auto result = installer::uninstall(options);
  const std::string verdict = lastLineStarting(result.lines, "uninstall=");
  log.check(result.exitCode != 0, "remove_denied_nonzero_rc",
    "rc=" + std::to_string(result.exitCode));
  log.check(contains(verdict, "uninstall=INCOMPLETE"), "remove_denied_verdict_incomplete",
    verdict);
  log.check(contains(verdict, "couldNotRemove=1"), "remove_denied_count_named", verdict);
  held.close();  // release the lock only after uninstall, then prove the file stayed
  log.check(std::filesystem::exists(file), "remove_denied_file_survived");
  log.check(std::filesystem::exists(manifestFileOf(dir)), "remove_denied_manifest_kept");
}

int runInstallSafetySelfTest() {
  SelfTestLog log;
  std::error_code error;
  const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
  const auto root = std::filesystem::temp_directory_path(error)
    / ("tf2-install-safety-selftest-" + std::to_string(stamp));
  if (error) {
    std::printf("install-safety-selftest=FAIL reason=cannot_resolve_temp\n");
    return 1;
  }
  std::filesystem::remove_all(root, error);
  std::filesystem::create_directories(root, error);
  if (error) {
    std::printf("install-safety-selftest=FAIL reason=cannot_create_workdir\n");
    return 1;
  }
  std::printf("install-safety-selftest: temp=%s\n", root.u8string().c_str());
  std::fflush(stdout);

  const auto run = [&log](const char* name, auto&& body) {
    ++log.cases;
    const int before = log.failures;
    std::printf("install-safety-selftest: case=%s start\n", name);
    std::fflush(stdout);
    try {
      body();
    } catch (const std::exception& failure) {
      log.check(false, std::string(name) + "_exception", failure.what());
    } catch (...) {
      log.check(false, std::string(name) + "_exception", "unknown exception");
    }
    std::printf("install-safety-selftest: case=%s result=%s\n", name,
      log.failures == before ? "PASS" : "FAIL");
    std::fflush(stdout);
  };

  run("normal_uninstall", [&] { caseNormalUninstall(log, root); });
  run("escape_relative", [&] { caseEscape(log, root, false); });
  run("escape_absolute", [&] { caseEscape(log, root, true); });
  run("remove_denied", [&] { caseRemoveDenied(log, root); });

  std::filesystem::remove_all(root, error);
  const bool ok = log.failures == 0 && log.cases == 4;
  std::printf("install-safety-selftest=%s cases=%d checks=%d fail=%d\n",
    ok ? "PASS" : "FAIL", log.cases, log.checks, log.failures);
  std::fflush(stdout);
  return ok ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
  enum class Mode { None, Version, Install, Uninstall, Status, SelfTest };
  Mode mode = Mode::None;
  tf2::native::installer::Options options;
  bool haveDirectory = false;

  for (int i = 1; i < argc; ++i) {
    const char* argument = argv[i];
    if (std::strcmp(argument, "--version") == 0) { mode = Mode::Version; }
    else if (std::strcmp(argument, "--install") == 0) { mode = Mode::Install; }
    else if (std::strcmp(argument, "--uninstall") == 0) { mode = Mode::Uninstall; }
    else if (std::strcmp(argument, "--status") == 0) { mode = Mode::Status; }
    else if (std::strcmp(argument, "--install-safety-selftest") == 0) { mode = Mode::SelfTest; }
    else if (std::strcmp(argument, "--dry-run") == 0) { options.dryRun = true; }
    else if (std::strcmp(argument, "--verbose") == 0) { options.verbose = true; }
    else if (std::strcmp(argument, "--install-dir") == 0 && i + 1 < argc) {
      options.installDir = std::filesystem::absolute(std::filesystem::u8path(argv[++i]));
      haveDirectory = true;
    } else {
      std::fprintf(stderr, "usage: %s --version | --install | --uninstall | --status"
        " | --install-safety-selftest"
        " [--install-dir DIR] [--dry-run] [--verbose]\n", argv[0]);
      return 2;
    }
  }
  // The self-test needs no install directory: it builds its own under TEMP, and
  // resolving the working directory first would make the fixture depend on where
  // the caller happened to stand.
  if (mode == Mode::SelfTest) return runInstallSafetySelfTest();
  if (!haveDirectory) {
    // The working directory: "install here" is the portable-package model this
    // product already has, not a Program Files location this round cannot test.
    std::error_code error;
    options.installDir = std::filesystem::current_path(error);
    if (error) {
      std::fprintf(stderr, "cannot resolve the working directory\n");
      return 2;
    }
  }

  tf2::native::installer::Result result;
  switch (mode) {
    case Mode::Version: result = tf2::native::installer::version(options); break;
    case Mode::Install: result = tf2::native::installer::install(options); break;
    case Mode::Uninstall: result = tf2::native::installer::uninstall(options); break;
    case Mode::Status: result = tf2::native::installer::status(options); break;
    case Mode::None:
    case Mode::SelfTest:
    default:
      std::fprintf(stderr, "one of --version, --install, --uninstall or --status is required\n");
      return 2;
  }
  for (const auto& line : result.lines) std::printf("%s\n", line.c_str());
  std::printf("exit=%d\n", result.exitCode);
  return result.exitCode;
}
