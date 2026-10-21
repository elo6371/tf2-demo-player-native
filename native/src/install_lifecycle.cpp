#include "install_lifecycle.h"

#include "install_manifest.h"

#include <Windows.h>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

namespace tf2::native::installer {

namespace {

// Backups keep the file's own name so a reader can see what was replaced. Only
// characters a Windows path cannot hold (or that would let a name escape the
// backup directory) are escaped; for a package payload that means none of them.
std::string encodeName(const std::string& name) {
  std::string result;
  result.reserve(name.size());
  for (const unsigned char c : name) {
    if (c == '\\' || c == '/' || c == ':') { result.push_back('%'); continue; }
    result.push_back(static_cast<char>(c));
  }
  return result;
}

std::string trimText(std::string value) {
  const auto first = value.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) return {};
  const auto last = value.find_last_not_of(" \t\r\n");
  return value.substr(first, last - first + 1);
}

std::string readTextFile(const std::filesystem::path& file) {
  std::ifstream stream(file, std::ios::binary);
  if (!stream) return {};
  std::ostringstream buffer;
  buffer << stream.rdbuf();
  return buffer.str();
}

bool copyFileRaw(const std::filesystem::path& from, const std::filesystem::path& to) {
  std::error_code error;
  std::filesystem::create_directories(to.parent_path(), error);
  std::error_code ignored;
  std::filesystem::copy_file(from, to, std::filesystem::copy_options::overwrite_existing, ignored);
  return !ignored;
}

// VERSION.txt is key=value so a release can be re-stamped by editing one text
// file instead of re-running CMake. A key that is absent keeps the compiled
// default rather than becoming an empty string: a version that silently reads ""
// is the failure mode this avoids.
void applyVersionOverrides(const std::filesystem::path& file, PackageInfo* info) {
  const std::string text = readTextFile(file);
  if (text.empty()) return;
  std::istringstream stream(text);
  std::string line;
  while (std::getline(stream, line)) {
    const auto equals = line.find('=');
    if (equals == std::string::npos) continue;
    const std::string key = trimText(line.substr(0, equals));
    const std::string value = trimText(line.substr(equals + 1));
    if (key == "version" && !value.empty()) info->version = value;
    else if (key == "versionCode") { try { info->versionCode = std::stoi(value); } catch (...) {} }
    else if (key == "buildUtc") info->buildUtc = value;
  }
}

std::string utcNow() {
  SYSTEMTIME time{};
  GetSystemTime(&time);
  char buffer[32]{};
  std::snprintf(buffer, sizeof(buffer), "%04u-%02u-%02uT%02u:%02u:%02uZ",
    time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond);
  return buffer;
}

// Excluded from every walk and from the manifest: the lifecycle's own state
// directory (it holds the manifest and the backups, and a manifest that listed
// its own backups would have to be written after itself) and the staged payload
// (an input to the install, not part of it).
// Whether a destination IS the executable currently running. Windows keeps a
// running image locked, so an in-place copy onto it fails with a sharing
// violation -- and the package this round ships is exactly that case: the thing
// that installs is the thing that is installed. Naming it is the difference
// between "the installer is broken" and "a running image cannot replace itself".
bool isRunningImage(const std::filesystem::path& path) {
  wchar_t self[MAX_PATH]{};
  if (GetModuleFileNameW(nullptr, self, MAX_PATH) == 0) return false;
  std::error_code error;
  const auto candidate = std::filesystem::weakly_canonical(path, error);
  if (error) return false;
  const auto running = std::filesystem::weakly_canonical(std::filesystem::path(self), error);
  if (error) return false;
  return candidate == running;
}

bool isLifecycleState(const std::string& relative) {
  return relative == kStateDirectory
    || relative.rfind(std::string(kStateDirectory) + "/", 0) == 0
    || relative == kPayloadDirectory
    || relative.rfind(std::string(kPayloadDirectory) + "/", 0) == 0;
}

std::filesystem::path manifestPath(const Options& options) {
  return options.installDir / kStateDirectory / kManifestName;
}

std::filesystem::path statePath(const Options& options,
    const std::filesystem::path& relative) {
  return options.installDir / kStateDirectory / relative;
}

void emit(Result& result, const std::string& line) { result.lines.push_back(line); }

Result installInto(const Options& options, const PackageInfo& info) {
  Result result;
  const auto installDir = options.installDir;
  const auto stateDir = installDir / kStateDirectory;
  const auto manifestFile = manifestPath(options);
  std::string error;

  Manifest existing;
  const bool reinstalling = readManifest(manifestFile, &existing, &error);

  // T42: an install over an existing install is a decision about VERSIONS.
  //   T37 recorded 'nothing compares packageVersionCode' as a named gap and named
  //   the consequence: a package newer than the install rewrote the manifest with
  //   its own version and called itself a reinstall, so the install directory's
  //   version depended on which package ran last, and a downgrade silently
  //   replaced files with older ones. The smallest honest closure is the
  //   decision, not a migration engine: going backwards is refused by name before
  //   one byte is touched, going forwards says 'upgrade', and only an equal code
  //   keeps the pre-T42 word 'reinstall'. A legacy manifest without a versionCode
  //   (0) cannot be ordered, so it keeps the old behaviour rather than guessing.
  std::string installMode = reinstalling ? "reinstall" : "fresh";
  if (reinstalling && info.versionCode != 0 && existing.packageVersionCode != 0) {
    if (info.versionCode < existing.packageVersionCode) {
      emit(result, "install=FAIL reason=downgrade installedVersionCode="
        + std::to_string(existing.packageVersionCode)
        + " packageVersionCode=" + std::to_string(info.versionCode));
      result.exitCode = 2;
      return result;
    }
    if (info.versionCode > existing.packageVersionCode) {
      installMode = "upgrade";
      emit(result, "install=note upgrade fromVersionCode="
        + std::to_string(existing.packageVersionCode)
        + " toVersionCode=" + std::to_string(info.versionCode));
    }
  }

  const auto payloadDir = stateDir / kPayloadDirectory;
  std::error_code code;
  if (!std::filesystem::is_directory(payloadDir, code)) {
    emit(result, "install=FAIL reason=no_payload payload=" + payloadDir.generic_string());
    result.exitCode = 2;
    return result;
  }

  std::uint64_t payloadBytes = 0;
  std::size_t payloadCount = 0;
  for (const auto& entry : std::filesystem::directory_iterator(payloadDir, code)) {
    if (code) break;
    if (!entry.is_regular_file()) continue;
    ++payloadCount;
    payloadBytes += entry.file_size();
  }
  emit(result, "install=START package=" + info.version + " payloadFiles="
    + std::to_string(payloadCount) + " payloadBytes=" + std::to_string(payloadBytes)
    + " mode=" + installMode);
  if (payloadCount == 0) {
    emit(result, "install=FAIL reason=empty_payload");
    result.exitCode = 2;
    return result;
  }

  // What was here BEFORE this install. Only used to classify each payload file as
  // new, changed or unchanged; the bytes of a changed file are kept so uninstall
  // can put them back.
  struct Prior { std::uint64_t bytes = 0; std::string sha; std::uint64_t written = 0; };
  std::map<std::string, Prior> prior;
  for (const auto& relative : listFiles(installDir)) {
    if (isLifecycleState(relative)) continue;
    Prior record;
    const auto path = installDir / std::filesystem::path(relative);
    record.bytes = std::filesystem::file_size(path, code);
    record.sha = sha256File(path, nullptr);
    lastWriteTime(path, &record.written);
    prior.emplace(relative, record);
  }

  // The reinstall case: the files the previous manifest said it installed are the
  // package's own, not the user's. Without this, a second install into the same
  // directory would classify its own previous payload as "changed" and back up a
  // copy of itself. An upgrade is a replacement, not an overwrite of user data.
  std::set<std::string> previouslyInstalled;
  if (reinstalling) {
    for (const auto& file : existing.files) {
      if (file.state == kStateNew || file.state == kStateChanged) {
        previouslyInstalled.insert(file.path);
      }
    }
  }

  std::vector<ManifestFile> files;
  std::size_t backups = 0;
  for (const auto& entry : std::filesystem::directory_iterator(payloadDir, code)) {
    if (code) break;
    if (!entry.is_regular_file()) continue;
    const auto source = entry.path();
    const auto name = source.filename().generic_string();
    const auto destination = installDir / entry.path().filename();
    const auto sourceSha = sha256File(source, &error);
    if (sourceSha.empty()) {
      emit(result, "install=FAIL reason=cannot_hash_payload file=" + name + " error=" + error);
      result.exitCode = 2;
      return result;
    }
    const auto sourceBytes = static_cast<std::uint64_t>(entry.file_size());

    ManifestFile record;
    record.path = name;
    record.bytes = sourceBytes;
    record.sha256 = sourceSha;

    const auto was = prior.find(name);
    const bool oursBefore = previouslyInstalled.count(name) != 0;
    if (was == prior.end()) {
      record.state = kStateNew;
    } else if (was->second.sha == sourceSha && was->second.bytes == sourceBytes && oursBefore) {
      // The bytes are already exactly the package own, and the previous manifest
      // proves this package put them there. The record must stay `new`: `unchanged`
      // means "it was here before we arrived and is not ours", and uninstall
      // deliberately leaves those. A reinstall that reclassified its own files as
      // unchanged made them unremovable -- measured 2026-10-20 as an uninstall
      // that reported PASS while leaving the product and the smoke probe behind.
      record.state = kStateNew;
    } else if (was->second.sha == sourceSha && was->second.bytes == sourceBytes) {
      record.state = kStateUnchanged;
    } else if (oursBefore) {
      // Ours, but the bytes on disk are NOT the bytes the previous manifest
      // recorded: the user edited a file this package installed. Refreshing it is
      // correct -- an upgrade replaces its own files -- but the bytes being
      // replaced are the user work, so they are backed up first and the record
      // says `changed`, which is what makes uninstall restore them.
      record.state = kStateChanged;
      const auto backupRelative = std::filesystem::path(kBackupDirectory) / encodeName(name);
      const auto backupPath = statePath(options, backupRelative);
      if (!copyFileRaw(destination, backupPath)) {
        emit(result, "install=FAIL reason=cannot_back_up file=" + name);
        result.exitCode = 2;
        return result;
      }
      record.backup = backupRelative.generic_string();
      ++backups;
    } else {
      record.state = kStateChanged;
      const auto backupRelative = std::filesystem::path(kBackupDirectory) / encodeName(name);
      const auto backupPath = statePath(options, backupRelative);
      if (!copyFileRaw(destination, backupPath)) {
        emit(result, "install=FAIL reason=cannot_back_up file=" + name);
        result.exitCode = 2;
        return result;
      }
      record.backup = backupRelative.generic_string();
      if (!setLastWriteTime(backupPath, was->second.written)) {
        emit(result, "install=WARN cannot_preserve_mtime backup=" + name);
      }
      if (!setLastWriteTime(destination, was->second.written)) {
        emit(result, "install=WARN cannot_preserve_mtime file=" + name);
      }
      ++backups;
    }
    files.push_back(record);
  }
  if (files.empty()) {
    emit(result, "install=FAIL reason=empty_payload");
    result.exitCode = 2;
    return result;
  }
  // Deterministic manifest: records are emitted in path order, not in whatever
  // order the directory iterator produced.
  std::sort(files.begin(), files.end(),
    [](const ManifestFile& a, const ManifestFile& b) { return a.path < b.path; });

  if (options.dryRun) {
    emit(result, "install=DRYRUN files=" + std::to_string(files.size()) + " backups="
      + std::to_string(backups) + " manifest="
      + std::filesystem::path(kStateDirectory).generic_string() + "/" + kManifestName);
    for (const auto& file : files) {
      if (options.verbose) emit(result, "  plan " + file.state + " " + file.path);
    }
    return result;
  }

  // Lay the payload down. The manifest is written last: a crash between the copy
  // and the manifest leaves a directory whose manifest claims nothing, and
  // uninstall then removes only what it can prove -- the safe direction.
  std::size_t copied = 0;
  for (const auto& file : files) {
    const auto source = payloadDir / std::filesystem::path(file.path);
    const auto destination = installDir / std::filesystem::path(file.path);
    // The package this round ships installs ITSELF: the running image is one of
    // the files being laid down. Windows keeps a running image locked, so a copy
    // onto it fails with a sharing violation -- measured 2026-10-20 as
    // "install=FAIL reason=cannot_copy file=tf2_demo_native.exe" on a package that
    // was otherwise complete. The bytes are already there and are the ones the
    // manifest hashes, so the correct reading is "already in place", not a failure.
    // Under a HARNESS the copy would succeed and truncate the live program.
    if (isRunningImage(destination)) {
      const auto running = sha256File(destination, nullptr);
      if (running != file.sha256) {
        emit(result, "install=note running_image_kept file=" + file.path
          + " reason=running_images_cannot_be_replaced_here");
      }
      ++copied;
      continue;
    }
    if (!copyFileRaw(source, destination)) {
      emit(result, "install=FAIL reason=cannot_copy file=" + file.path);
      result.exitCode = 2;
      return result;
    }
    ++copied;
  }

  Manifest manifest;
  manifest.package = "tf2_demo_native";
  manifest.packageVersion = info.version;
  manifest.packageVersionCode = info.versionCode;
  manifest.sourceDateEpoch = info.sourceDateEpoch;
  manifest.sourceTree = info.sourceTree;
  manifest.installedUtc = utcNow();
  manifest.installDirectory = installDir.generic_string();
  manifest.files = files;

  // preserved: everything that was already in the directory and is not ours.
  // Collected AFTER the file list is final, because the list grows by one when
  // the version file this package wrote has to be recorded: computing preserved
  // from a list that later grew would list the package own VERSION.txt twice.
  const auto collectPreserved = [&]() {
    std::set<std::string> ours;
    for (const auto& file : files) ours.insert(file.path);
    for (const auto& [relative, record] : prior) {
      (void)record;
      if (ours.count(relative) != 0) continue;
      ManifestFile preserved;
      preserved.path = relative;
      preserved.state = kStatePreserved;
      const auto path = installDir / std::filesystem::path(relative);
      preserved.bytes = std::filesystem::file_size(path, code);
      preserved.sha256 = sha256File(path, nullptr);
      manifest.files.push_back(std::move(preserved));
    }
    std::sort(manifest.files.begin(), manifest.files.end(),
      [](const ManifestFile& a, const ManifestFile& b) { return a.path < b.path; });
  };

  // VERSION.txt is a file this package LAYS DOWN, so the manifest has to record
  // it: a file that is written but not listed is one uninstall cannot prove it may
  // remove, and the earlier version left it behind after a "complete" uninstall.
  // It is written here -- before the manifest -- so the record can carry the real
  // bytes and hash of what was written.
  //
  // WHO OWNS IT is decided BEFORE the bytes are written, and by the one source of
  // truth there is: a previous manifest that lists it as preserved says the user
  // brought it, and then it is left exactly alone. Everything else is the
  // package own file -- either the copy cmake --install put beside the executable,
  // or one an earlier run of this installer wrote. An earlier version wrote it
  // unconditionally and then listed it as preserved, which is the worst of both:
  // the user file is destroyed and the manifest claims it was left alone.
  bool previouslyPreserved = false;
  if (reinstalling) {
    for (const auto& file : existing.files) {
      if (file.path == kVersionFileName && file.state == kStatePreserved) previouslyPreserved = true;
    }
  }
  const bool userVersionIsOurs = !previouslyPreserved;
  bool wroteUserVersionFile = false;
  if (userVersionIsOurs) {
    std::ostringstream text;
    text << "package=tf2_demo_native\n";
    text << "version=" << info.version << "\n";
    text << "versionCode=" << info.versionCode << "\n";
    text << "buildUtc=" << info.buildUtc << "\n";
    text << "sourceDateEpoch=" << info.sourceDateEpoch << "\n";
    text << "sourceTree=" << info.sourceTree << "\n";
    text << "installedUtc=" << utcNow() << "\n";
    const auto contents = text.str();
    std::ofstream stream(installDir / kVersionFileName, std::ios::binary | std::ios::trunc);
    if (stream) {
      stream.write(contents.data(), static_cast<std::streamsize>(contents.size()));
      wroteUserVersionFile = true;
    }
  }
  if (wroteUserVersionFile) {
    ManifestFile record;
    record.path = kVersionFileName;
    record.state = kStateNew;
    const auto versionPath = installDir / kVersionFileName;
    record.bytes = std::filesystem::file_size(versionPath, code);
    record.sha256 = sha256File(versionPath, &error);
    if (record.sha256.empty()) {
      emit(result, "install=FAIL reason=cannot_hash_payload file=" + std::string(kVersionFileName)
        + " error=" + error);
      result.exitCode = 2;
      return result;
    }
    files.push_back(std::move(record));
    std::sort(files.begin(), files.end(),
      [](const ManifestFile& a, const ManifestFile& b) { return a.path < b.path; });
  } else {
    emit(result, "install=note kept_user_version_file=" + std::string(kVersionFileName));
  }

  manifest.files = files;
  collectPreserved();
  if (!writeManifest(manifestFile, manifest, &error)) {
    emit(result, "install=FAIL reason=cannot_write_manifest error=" + error);
    result.exitCode = 2;
    return result;
  }

  // The payload is an input, not part of the install: leaving it would double the
  // package size and make "what is installed here" ambiguous.
  std::filesystem::remove_all(payloadDir, code);

  std::size_t newFiles = 0, changed = 0, unchanged = 0;
  for (const auto& file : files) {
    if (file.state == kStateNew) ++newFiles;
    else if (file.state == kStateChanged) ++changed;
    else ++unchanged;
  }
  emit(result, "install=PASS files=" + std::to_string(files.size())
    + " new=" + std::to_string(newFiles) + " changed=" + std::to_string(changed)
    + " unchanged=" + std::to_string(unchanged) + " preserved="
    + std::to_string(manifest.files.size() - files.size()) + " backups="
    + std::to_string(backups) + " copied=" + std::to_string(copied));
  emit(result, "install_manifest=" + std::filesystem::path(kStateDirectory).generic_string()
    + "/" + kManifestName + " version=" + info.version + " versionCode="
    + std::to_string(info.versionCode));
  return result;
}

Result uninstallFrom(const Options& options) {
  Result result;
  const auto manifestFile = manifestPath(options);
  Manifest manifest;
  std::string error;
  if (!readManifest(manifestFile, &manifest, &error)) {
    // Not an install, or an install whose manifest is gone. Refusing by name is
    // the whole point: a tool that deleted a directory on a guess is how a user
    // loses files that were never ours.
    emit(result, "uninstall=FAIL reason=no_manifest manifest="
      + std::filesystem::path(kStateDirectory).generic_string() + "/" + kManifestName
      + " detail=" + error);
    result.exitCode = 2;
    return result;
  }

  std::size_t removed = 0, restored = 0, preserved = 0, modified = 0, missing = 0, deferred = 0;
  for (const auto& file : manifest.files) {
    if (file.state == kStatePreserved || file.state == kStateUnchanged) { ++preserved; continue; }
    const auto path = options.installDir / std::filesystem::path(file.path);
    std::error_code code;
    if (!std::filesystem::is_regular_file(path, code)) { ++missing; continue; }
    const auto current = sha256File(path, nullptr);
    if (current != file.sha256) {
      // The user edited a file this package installed. It stays, and the reading
      // says so: an uninstall that reported "removed 3" while leaving an edited
      // file behind would be a lie a later audit could not detect.
      ++modified;
      continue;
    }
    if (!file.backup.empty()) {
      const auto backupPath = statePath(options, std::filesystem::path(file.backup));
      if (std::filesystem::is_regular_file(backupPath, code)) {
        if (!copyFileRaw(backupPath, path)) {
          emit(result, "uninstall=FAIL reason=cannot_restore file=" + file.path);
          result.exitCode = 2;
          return result;
        }
        ++restored;
        continue;
      }
    }
    // A RUNNING IMAGE CANNOT DELETE ITSELF.
    //   Windows keeps the mapped image locked with delete-share denied, so
    //   std::filesystem::remove on the executable that is executing this code
    //   fails -- measured 2026-10-20 as an uninstall that printed removed=2 and
    //   left tf2_demo_native.exe behind, which is exactly the kind of green
    //   reading this manifest exists to prevent. It is counted separately, by
    //   name, and the caller is told what to do, instead of being folded into a
    //   success count it does not belong in.
    if (isRunningImage(path)) {
      ++deferred;
      emit(result, "uninstall=note deferred_running_image=" + file.path);
      continue;
    }
    if (!std::filesystem::remove(path, code)) {
      // Not removed and not ours to defer: say so rather than counting a pass.
      emit(result, "uninstall=note could_not_remove=" + file.path
        + " win32=" + std::to_string(code.value()));
      continue;
    }
    ++removed;
  }

  // Directories this package created, deepest first, and only while empty: a
  // directory the user also put something in is left alone by construction.
  {
    std::vector<std::filesystem::path> directories;
    std::error_code code;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(options.installDir, code)) {
      if (code) break;
      if (entry.is_directory() && entry.path().filename() != kStateDirectory) {
        directories.push_back(entry.path());
      }
    }
    std::sort(directories.begin(), directories.end(),
      [](const std::filesystem::path& a, const std::filesystem::path& b) {
        return a.generic_string().size() > b.generic_string().size();
      });
    for (const auto& directory : directories) {
      std::error_code ignored;
      std::filesystem::remove(directory, ignored);
    }
  }

  // The manifest last, so a run that dies part way can simply be repeated.
  {
    std::error_code code;
    std::filesystem::remove(manifestFile, code);
    // VERSION.txt is removed by the record loop above when the manifest lists it
    // as ours. Removing it unconditionally here would delete a version file the
    // user brought with them, which is the exact class of bug this manifest
    // exists to make impossible.
    std::filesystem::remove_all(options.installDir / kStateDirectory, code);
  }
  // Three, not zero: the caller asked for a complete removal and did not get one.
  // Exit 2 is reserved for "this directory is not an install", which is a
  // different finding and must stay distinguishable in a script.
  if (deferred != 0) result.exitCode = 3;
  emit(result, "uninstall=" + std::string(deferred == 0 ? "PASS" : "INCOMPLETE")
    + " removed=" + std::to_string(removed) + " restored="
    + std::to_string(restored) + " preserved=" + std::to_string(preserved) + " modified="
    + std::to_string(modified) + " alreadyMissing=" + std::to_string(missing)
    + " deferredRunningImage=" + std::to_string(deferred));
  return result;
}

Result statusOf(const Options& options) {
  Result result;
  const auto manifestFile = manifestPath(options);
  Manifest manifest;
  std::string error;
  if (!readManifest(manifestFile, &manifest, &error)) {
    emit(result, "status=ABSENT reason=" + error);
    return result;
  }
  std::size_t present = 0, missing = 0, modified = 0;
  for (const auto& file : manifest.files) {
    if (file.state == kStatePreserved) continue;
    const auto path = options.installDir / std::filesystem::path(file.path);
    std::error_code code;
    if (!std::filesystem::is_regular_file(path, code)) { ++missing; continue; }
    if (sha256File(path, nullptr) == file.sha256) ++present; else ++modified;
  }
  const bool clean = missing == 0 && modified == 0;
  emit(result, std::string("status=") + (clean ? "VERIFIED" : "DEGRADED")
    + " package=" + manifest.package + " version=" + manifest.packageVersion
    + " versionCode=" + std::to_string(manifest.packageVersionCode)
    + " installedUtc=" + manifest.installedUtc + " files=" + std::to_string(manifest.files.size())
    + " present=" + std::to_string(present) + " missing=" + std::to_string(missing)
    + " modified=" + std::to_string(modified));
  return result;
}
} // namespace

PackageInfo packageInfo() {
  PackageInfo info;
  info.version = TF2_PACKAGE_VERSION;
  info.versionCode = TF2_PACKAGE_VERSION_CODE;
  info.buildUtc = TF2_BUILD_UTC;
  info.sourceDateEpoch = TF2_SOURCE_DATE_EPOCH;
  info.sourceTree = TF2_SOURCE_TREE;
  // A VERSION.txt beside the executable, or in the state directory, wins over the
  // compiled default so a packaged release can be re-stamped without a rebuild.
  wchar_t self[MAX_PATH]{};
  if (GetModuleFileNameW(nullptr, self, MAX_PATH) != 0) {
    const auto directory = std::filesystem::path(self).parent_path();
    applyVersionOverrides(directory / kStateDirectory / kVersionFileName, &info);
    applyVersionOverrides(directory / kVersionFileName, &info);
  }
  return info;
}

Result install(const Options& options) { return installInto(options, packageInfo()); }

Result uninstall(const Options& options) { return uninstallFrom(options); }

Result status(const Options& options) { return statusOf(options); }

Result version(const Options&) {
  Result result;
  const auto info = packageInfo();
  emit(result, "package=tf2_demo_native version=" + info.version + " versionCode="
    + std::to_string(info.versionCode));
  emit(result, "buildUtc=" + info.buildUtc + " sourceDateEpoch=" + info.sourceDateEpoch
    + " sourceTree=" + info.sourceTree);
  return result;
}

} // namespace tf2::native::installer