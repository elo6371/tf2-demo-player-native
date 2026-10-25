// T72 review bookmarks: the in-memory bookmark collection that lives inside a
// T71 review workspace. Pure logic: no Win32, no file I/O -- the workspace store
// owns persistence so that a failed save can never damage a good file.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "workspace_types.h"

namespace tf2::native {

// Mirrors the load state of the owning workspace. A bookmark store is never
// "loaded" unless the workspace hash matched the demo that is open.
enum class ReviewLoadState {
  NotLoaded = 0,
  Loaded = 1,
  Missing = 2,
  Corrupt = 3,
  HashMismatch = 4,
  OldSchema = 5,
};

enum class ReviewAddStatus {
  Added = 0,
  AlreadyExists = 1,
  RefusedEmptyTitle = 2,
  RefusedSeekPending = 3,
  RefusedNoDemo = 4,
  RefusedNotLoaded = 5,
  RefusedUnknownEvent = 6,
  RefusedHashMismatch = 7,
  RefusedInvalidTick = 8,
};

enum class ReviewUpdateStatus {
  Updated = 0,
  NotFound = 1,
  RefusedEmptyTitle = 2,
  RefusedNotLoaded = 3,
  RefusedHashMismatch = 4,
};

enum class ReviewDeleteStatus {
  Removed = 0,
  NotFound = 1,
  RefusedNotLoaded = 2,
};

enum class ReviewJumpStatus {
  Applied = 0,
  RefusedNotFound = 1,
  RefusedHashMismatch = 2,
  RefusedOutOfRange = 3,
  RefusedCamera = 4,
  RefusedPlayback = 5,
  RefusedNoDemo = 6,
  RefusedNotLoaded = 7,
};

enum class ReviewSort {
  Tick = 0,
  Created = 1,
  Title = 2,
};

const char* reviewLoadStateName(ReviewLoadState state);
const char* reviewAddStatusName(ReviewAddStatus status);
const char* reviewUpdateStatusName(ReviewUpdateStatus status);
const char* reviewDeleteStatusName(ReviewDeleteStatus status);
const char* reviewJumpStatusName(ReviewJumpStatus status);
const char* reviewSortName(ReviewSort sort);

// Search and filter, all clauses ANDed. Empty strings and negative tick bounds
// mean "no constraint". eventName compares exactly ("manual" for bookmarks that
// carry no event). player accepts "userid:<n>", "entindex:<n>" or a name
// substring (ASCII case-insensitive).
struct ReviewQuery {
  std::string text;
  std::string eventName;
  std::string player;
  std::int32_t tickMin = -1;
  std::int32_t tickMax = -1;
};

// A T64 event as handed to the bookmark store. tick is the event's own tick;
// (tick, packetOrdinal, messageBit) is the T68 eventKey that tells two events
// on the same tick apart.
struct ReviewEventInput {
  std::string name;
  std::uint32_t eventId = 0;
  std::int32_t tick = -1;
  std::uint64_t packetOrdinal = 0;
  std::uint32_t messageBit = 0;
  std::vector<WorkspacePlayerRef> players;
  std::string identityReason;
};

// The outside world a jump needs. Camera is applied before playback so that a
// refused seek can roll the camera back; seek() returns false when playback
// refuses the tick (no demo, seeking, out of range, ...).
class ReviewJumpPorts {
 public:
  virtual ~ReviewJumpPorts() = default;
  virtual bool seek(std::int32_t tick) = 0;
  virtual bool applyCamera(const WorkspaceCameraRef& camera) = 0;
  virtual void restoreCamera() = 0;
};

struct ReviewCounters {
  std::uint64_t added = 0;
  std::uint64_t updated = 0;
  std::uint64_t deleted = 0;
  std::uint64_t refused = 0;
  std::uint64_t loadFailures = 0;
  std::uint64_t saveFailures = 0;
  std::uint64_t jumpFailures = 0;
  std::uint64_t duplicateEvent = 0;
};

class ReviewBookmarkStore {
 public:
  // -- context ---------------------------------------------------------
  // demoHash is the sha256 of the demo that is actually open; endTick is the
  // demo's last tick (-1 when unknown). cursorTick is where playback really is.
  void setDemo(const std::string& demoHash, std::int32_t endTick);
  void clearDemo();
  void setCursorTick(std::int32_t tick);
  void setSeekPending(bool pending);
  void setNowUtc(std::int64_t seconds);  // tests use a fixed clock

  // Record why the owning workspace did not load. Sets loadState and bumps
  // loadFailures once per call.
  void noteOpenState(ReviewLoadState state, const std::string& reason);

  // -- workspace bridge ------------------------------------------------
  ReviewLoadState load(const WorkspaceDocument& workspace, std::string& reason);
  // Copies the live collection into the workspace document. Does not write a
  // file: the T71 store owns the atomic save.
  bool save(WorkspaceDocument& workspace, std::string& reason);

  // -- CRUD ------------------------------------------------------------
  ReviewAddStatus addManual(const std::string& title, const std::string& note,
                            const WorkspaceCameraRef& camera, std::uint64_t& outId,
                            std::string& reason);
  ReviewAddStatus addEvent(const ReviewEventInput& event, const std::string& title,
                           const std::string& note, const WorkspaceCameraRef& camera,
                           std::uint64_t& outId, std::string& reason);
  ReviewUpdateStatus update(std::uint64_t id, const std::string& title,
                            const std::string& note, std::string& reason);
  ReviewDeleteStatus remove(std::uint64_t id, std::string& reason);

  // -- queries ---------------------------------------------------------
  std::vector<WorkspaceBookmarkRecord> sorted(ReviewSort sort) const;
  std::vector<WorkspaceBookmarkRecord> query(const ReviewQuery& filter, ReviewSort sort) const;

  // -- jump ------------------------------------------------------------
  ReviewJumpStatus jump(std::uint64_t id, ReviewJumpPorts& ports, std::string& reason);

  // -- state -----------------------------------------------------------
  const std::vector<WorkspaceBookmarkRecord>& bookmarks() const { return bookmarks_; }
  std::size_t count() const { return bookmarks_.size(); }
  std::uint64_t nextBookmarkId() const { return nextBookmarkId_; }
  bool dirty() const { return dirty_; }
  void markClean() { dirty_ = false; }
  ReviewLoadState loadState() const { return loadState_; }
  const std::string& loadReason() const { return loadReason_; }
  const std::string& lastJumpEventKey() const { return lastJumpEventKey_; }
  std::int32_t lastJumpTick() const { return lastJumpTick_; }
  const ReviewCounters& counters() const { return counters_; }
  ReviewCounters& counters() { return counters_; }
  const WorkspaceBookmarkRecord* find(std::uint64_t id) const;

  // -- trace helpers ---------------------------------------------------
  static std::string eventKey(const WorkspaceBookmarkRecord& record);
  static std::string playersTrace(const std::vector<WorkspacePlayerRef>& players);
  static std::string identityTrace(const WorkspaceBookmarkRecord& record);
  static std::string listLine(const WorkspaceBookmarkRecord& record);

 private:
  ReviewAddStatus checkAddReady(std::string& reason) const;
  WorkspaceBookmarkRecord makeRecord(std::int32_t tick, const std::string& title,
                                     const std::string& note,
                                     const WorkspaceCameraRef& camera) const;
  static void normalizeCamera(WorkspaceCameraRef& camera);

  std::string demoHash_;
  std::int32_t endTick_ = -1;
  std::int32_t cursorTick_ = -1;
  bool seekPending_ = false;
  std::int64_t nowUtc_ = 0;

  std::vector<WorkspaceBookmarkRecord> bookmarks_;
  std::uint64_t nextBookmarkId_ = 1;
  bool dirty_ = false;
  ReviewLoadState loadState_ = ReviewLoadState::NotLoaded;
  std::string loadReason_ = "not-loaded";
  std::string lastJumpEventKey_;
  std::int32_t lastJumpTick_ = -1;
  ReviewCounters counters_;
};

}  // namespace tf2::native
