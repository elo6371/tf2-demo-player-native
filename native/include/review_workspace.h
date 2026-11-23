#pragma once

// T71: versioned, per-demo review workspace. The workspace is bound to the
// demo's content hash (sha256 of the file), not to its path or name, so a demo
// that moved still finds its workspace. The store owns the only code that
// reads or writes the live .ini file; T72 bookmark mutations go through
// WorkspaceDocument and are persisted here.

#include "workspace_types.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

namespace tf2::native {

enum class WorkspaceOpenState {
  Opened,
  Missing,
  Corrupt,
  HashMismatch,
  OldSchema,
};

enum class WorkspaceSaveStatus {
  Saved,
  RefusedNotLoaded,
  RefusedHashMismatch,
  RefusedReadOnly,
  WriteFailed,
};

enum class WorkspaceNewStatus {
  Created,
  AlreadyExists,
  NoHash,
  WriteFailed,
};

enum class WorkspaceAutosaveStatus {
  Saved,
  SkippedClean,
  SkippedDisabled,
  Failed,
};

enum class WorkspaceRenameStatus {
  Renamed,
  RefusedEmptyName,
  RefusedTooLong,
};

enum class WorkspaceCloseStatus {
  Clean,
  UnsavedExit,
};

const char* workspaceOpenStateName(WorkspaceOpenState state);
const char* workspaceSaveStatusName(WorkspaceSaveStatus status);
const char* workspaceNewStatusName(WorkspaceNewStatus status);
const char* workspaceAutosaveStatusName(WorkspaceAutosaveStatus status);
const char* workspaceRenameStatusName(WorkspaceRenameStatus status);
const char* workspaceCloseStatusName(WorkspaceCloseStatus status);

struct WorkspaceOpenResult {
  WorkspaceOpenState state = WorkspaceOpenState::Missing;
  bool loaded = false;
  bool movedPath = false;
  std::string reason;
  std::string storedHash;
  std::string actualHash;
  std::size_t bookmarkCount = 0;
  std::uint64_t nextBookmarkId = 1;
  std::filesystem::path path;
};

class WorkspaceStore {
 public:
  // Derived path root, e.g. %LOCALAPPDATA%/TF2 Demo Player/workspaces.
  void setRoot(const std::filesystem::path& root);
  // Test/product override: use exactly this file instead of the derived name.
  void setExplicitPath(const std::filesystem::path& path);
  bool hasExplicitPath() const;

  std::filesystem::path pathFor(const std::string& algo, const std::string& hash) const;
  const std::filesystem::path& path() const;
  const WorkspaceDocument& document() const;
  WorkspaceDocument& document();
  bool loaded() const;
  bool dirty() const;
  void markDirty();
  void markClean();
  const std::string& storedHash() const;
  const std::string& actualHash() const;
  WorkspaceOpenState loadState() const;

  WorkspaceOpenResult open(const std::filesystem::path& demoPath, const std::string& actualHash);
  WorkspaceNewStatus create(const std::filesystem::path& demoPath, const std::string& actualHash,
                            std::string& reason);
  WorkspaceSaveStatus save(WorkspaceDocument& document, std::string& reason);
  WorkspaceAutosaveStatus autosave(WorkspaceDocument& document, bool dirty, std::string& reason);
  WorkspaceRenameStatus rename(WorkspaceDocument& document, const std::string& name,
                               std::string& reason);
  WorkspaceCloseStatus close(bool dirty);

  // Explicit migration of a document the parser read as an older schema. The
  // header hash and every record hash are rewritten together; a header-only
  // rewrite is what "migrated" must never mean.
  bool migrate(const std::string& actualHash, std::string& reason);
  bool hasMigrationCandidate() const;

  static std::string serialize(const WorkspaceDocument& document);
  static bool parse(const std::string& text, WorkspaceDocument& document, std::string& reason,
                    WorkspaceOpenState& state);
  static bool readFile(const std::filesystem::path& path, std::string& out, std::string& reason);

 private:
  std::filesystem::path resolvePath(const std::string& hash) const;
  bool writeDocument(const WorkspaceDocument& document, std::string& reason,
                     WorkspaceSaveStatus& status);

  std::filesystem::path root_;
  std::filesystem::path explicitPath_;
  std::filesystem::path path_;
  WorkspaceDocument document_;
  WorkspaceDocument migrationCandidate_;
  bool hasMigrationCandidate_ = false;
  bool loaded_ = false;
  bool dirty_ = false;
  std::string storedHash_;
  std::string actualHash_;
  WorkspaceOpenState loadState_ = WorkspaceOpenState::Missing;
};

} // namespace tf2::native
