#pragma once

// T71/T72 product wiring. The stores (review_workspace.*, review_bookmarks.*)
// are pure logic with no window and no transport; this is the glue that binds
// one workspace and its bookmarks to a running player, so that "the workspace
// remembers the camera and the bookmarks" is a reading from outside the process
// instead of a claim only a probe can make.
//
// The session never reaches into main.cpp: everything it needs arrives through
// ReviewSessionPorts (transport + camera), a trace sink and an event lookup.

#include "review_bookmarks.h"
#include "review_workspace.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace tf2::native {

// What the player owns. tick/speed/paused/reverse are the live transport;
// seek/setSpeed/setPaused are the same requests the keyboard and the script
// driver use, so a restored workspace takes no shortcut the user cannot take.
class ReviewSessionPorts {
 public:
  virtual ~ReviewSessionPorts() = default;
  virtual std::int32_t tick() const = 0;
  virtual double speed() const = 0;
  virtual bool paused() const = 0;
  virtual bool reverse() const = 0;
  virtual bool seek(std::int32_t tick) = 0;
  virtual bool setSpeed(double speed) = 0;
  virtual void setPaused(bool paused) = 0;
  virtual WorkspaceCameraRef cameraNow() const = 0;
  virtual bool applyCamera(const WorkspaceCameraRef& camera) = 0;
};

// The verified events on one tick. main.cpp implements this over the decoded
// T68 table, so a bookmark can only be created from an event that exists.
using ReviewEventLookup =
  std::function<bool(std::int32_t tick, std::vector<ReviewEventInput>& out)>;

using ReviewTraceSink = std::function<void(const std::string&)>;

// The workspace document holds more than this file knows how to read: the T50
// numbered camera slots, the HUD panel set, the minimap, the event filter.
// main.cpp owns those, so it fills and restores them through these two hooks
// instead of the session guessing at product state.
using ReviewEnvironmentCapture = std::function<void(WorkspaceDocument&)>;
using ReviewEnvironmentApply = std::function<void(const WorkspaceDocument&)>;

class ReviewSession : private ReviewJumpPorts {
 public:
  ReviewSession(ReviewSessionPorts& ports, ReviewTraceSink trace, ReviewEventLookup lookup,
                ReviewEnvironmentCapture captureEnv = {}, ReviewEnvironmentApply applyEnv = {});
  ~ReviewSession() override = default;

  ReviewSession(const ReviewSession&) = delete;
  ReviewSession& operator=(const ReviewSession&) = delete;

  // -- lifecycle -------------------------------------------------------
  void setPath(const std::filesystem::path& path) { path_ = path; }
  const std::filesystem::path& path() const { return path_; }
  // Binds the workspace to the open demo: opens it by content hash, or creates
  // it when the demo has never been reviewed. Returns true when a document is
  // in memory (loaded, created or migrated), false when the file was refused.
  bool begin(const std::filesystem::path& demoPath, const std::string& demoHash,
             std::int32_t endTick);
  bool isOpen() const { return open_; }

  // -- state -----------------------------------------------------------
  void traceState() const;
  bool applyPlayback(std::string& detail);
  void capturePlayback();
  void captureEnvironment();
  void applyEnvironment();

  // -- bookmarks (T72) -------------------------------------------------
  bool addManual(std::int32_t tick, const std::string& title, const std::string& note,
                 std::string& detail);
  bool addEvent(std::int32_t tick, const std::string& title, const std::string& note,
                std::string& detail);
  bool update(std::uint64_t id, const std::string& title, const std::string& note,
              std::string& detail);
  bool erase(std::uint64_t id, std::string& detail);
  void list(ReviewSort sort, const ReviewQuery* filter, const std::string& label);
  bool jump(std::uint64_t id, std::string& detail);

  // -- persistence -----------------------------------------------------
  bool save(bool force, std::string& detail);
  void finish();

  WorkspaceStore& store() { return store_; }
  ReviewBookmarkStore& bookmarks() { return bookmarks_; }
  const WorkspaceDocument& document() const { return store_.document(); }

 private:
  // ReviewJumpPorts
  bool seek(std::int32_t tick) override;
  bool applyCamera(const WorkspaceCameraRef& camera) override;
  void restoreCamera() override;

  void syncDocumentFromBookmarks(const char* why);
  void traceBookmarkLine(const char* what, const WorkspaceBookmarkRecord& record, const char* status);

  ReviewSessionPorts& ports_;
  ReviewTraceSink trace_;
  ReviewEventLookup lookup_;
  ReviewEnvironmentCapture captureEnv_;
  ReviewEnvironmentApply applyEnv_;
  WorkspaceStore store_;
  ReviewBookmarkStore bookmarks_;
  std::filesystem::path path_;
  std::string demoHash_;
  std::int32_t endTick_ = -1;
  bool open_ = false;
  bool created_ = false;
  WorkspaceCameraRef rollbackCamera_{};
  bool rollbackValid_ = false;
};

}  // namespace tf2::native
