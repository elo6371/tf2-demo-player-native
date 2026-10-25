// T71/T72: the glue between the stores and a running player.
//
// Everything here is deliberately boring: the workspace store owns the file and
// its states, the bookmark store owns identity/dedupe/sort/jump, and this file
// only decides WHEN they are asked and what the trace says afterwards. A reading
// that cannot be produced here is a reading the product does not have.

#include "review_session.h"

#include <string>
#include <utility>

namespace tf2::native {
namespace {

std::string escapePath(const std::filesystem::path& path) {
  if (path.empty()) return "-";
  return path.string();
}

ReviewLoadState mapOpenState(WorkspaceOpenState state) {
  switch (state) {
    case WorkspaceOpenState::Opened: return ReviewLoadState::Loaded;
    case WorkspaceOpenState::Missing: return ReviewLoadState::Missing;
    case WorkspaceOpenState::Corrupt: return ReviewLoadState::Corrupt;
    case WorkspaceOpenState::HashMismatch: return ReviewLoadState::HashMismatch;
    case WorkspaceOpenState::OldSchema: return ReviewLoadState::OldSchema;
  }
  return ReviewLoadState::NotLoaded;
}

}  // namespace

ReviewSession::ReviewSession(ReviewSessionPorts& ports, ReviewTraceSink trace,
                             ReviewEventLookup lookup, ReviewEnvironmentCapture captureEnv,
                             ReviewEnvironmentApply applyEnv)
  : ports_(ports), trace_(std::move(trace)), lookup_(std::move(lookup)),
    captureEnv_(std::move(captureEnv)), applyEnv_(std::move(applyEnv)) {}

bool ReviewSession::begin(const std::filesystem::path& demoPath, const std::string& demoHash,
                          std::int32_t endTick) {
  demoHash_ = demoHash;
  endTick_ = endTick;
  if (path_.empty()) {
    trace_("workspace: open state=NoPath hash=" + (demoHash_.empty() ? std::string("-") : demoHash_) +
           " path=-");
    open_ = false;
    return false;
  }
  store_.setExplicitPath(path_);
  const WorkspaceOpenResult result = store_.open(demoPath, demoHash);
  bool loaded = result.loaded;
  std::string reason = result.reason;
  if (result.state == WorkspaceOpenState::Missing) {
    std::string createReason;
    const WorkspaceNewStatus created = store_.create(demoPath, demoHash, createReason);
    loaded = created == WorkspaceNewStatus::Created;
    created_ = loaded;
    reason = createReason;
    trace_("workspace: create status=" + std::string(workspaceNewStatusName(created)) +
           " path=" + escapePath(store_.path()) +
           " demoHash=" + (demoHash_.empty() ? std::string("-") : demoHash_) +
           " reason=" + (reason.empty() ? std::string("-") : reason));
  }
  open_ = loaded;
  bookmarks_.setDemo(demoHash_, endTick_);
  bookmarks_.setCursorTick(ports_.tick());
  std::string bookmarkReason;
  if (loaded) {
    const ReviewLoadState bookmarkState = bookmarks_.load(store_.document(), bookmarkReason);
    if (bookmarkState != ReviewLoadState::Loaded) {
      trace_(std::string("workspace: bookmarks state=") + reviewLoadStateName(bookmarkState) +
             " reason=" + (bookmarkReason.empty() ? std::string("-") : bookmarkReason));
    }
  } else {
    bookmarks_.noteOpenState(mapOpenState(result.state),
                             reason.empty() ? std::string("-") : reason);
  }
  trace_("workspace: open state=" + std::string(workspaceOpenStateName(result.state)) +
         " loaded=" + (loaded ? "1" : "0") +
         " hash=" + (demoHash_.empty() ? std::string("-") : demoHash_) +
         " stored=" + (result.storedHash.empty() ? std::string("-") : result.storedHash) +
         " actual=" + (result.actualHash.empty() ? std::string("-") : result.actualHash) +
         " moved=" + (result.movedPath ? "1" : "0") +
         " path=" + escapePath(store_.path()) +
         " bookmarks=" + std::to_string(bookmarks_.count()) +
         " next=" + std::to_string(bookmarks_.nextBookmarkId()) +
         " reason=" + (reason.empty() ? std::string("-") : reason));
  return open_;
}

void ReviewSession::traceState() const {
  trace_("workspace: state open=" + std::string(open_ ? "1" : "0") +
         " created=" + std::string(created_ ? "1" : "0") +
         " bookmarks=" + std::to_string(bookmarks_.count()) +
         " next=" + std::to_string(bookmarks_.nextBookmarkId()) +
         " dirty=" + std::string(store_.dirty() ? "1" : "0") +
         " load=" + std::string(reviewLoadStateName(bookmarks_.loadState())) +
         " loadReason=" + (bookmarks_.loadReason().empty() ? std::string("-") : bookmarks_.loadReason()) +
         " tick=" + std::to_string(ports_.tick()) +
         " speed=" + std::to_string(ports_.speed()) +
         " paused=" + std::string(ports_.paused() ? "1" : "0") +
         " path=" + escapePath(store_.path()));
}

bool ReviewSession::applyPlayback(std::string& detail) {
  const WorkspacePlaybackState& playback = store_.document().playback;
  if (!open_) { detail = "not-open"; return false; }
  if (!playback.restorable) {
    detail = "not-restorable:" + (playback.reason.empty() ? std::string("-") : playback.reason);
    trace_("workspace: playback applied=0 restorable=0 reason=" + detail);
    return false;
  }
  applyEnvironment();
  const bool seeked = ports_.seek(playback.tick);
  const bool speeded = ports_.setSpeed(playback.speed);
  ports_.setPaused(playback.paused);
  const bool landed = ports_.tick() == playback.tick;
  detail = "tick=" + std::to_string(ports_.tick()) + " speed=" + std::to_string(ports_.speed()) +
           " paused=" + (ports_.paused() ? "1" : "0");
  trace_("workspace: playback applied=" + std::string(seeked && landed ? "1" : "0") +
         " seeked=" + std::string(seeked ? "1" : "0") +
         " speeded=" + std::string(speeded ? "1" : "0") +
         " wantTick=" + std::to_string(playback.tick) +
         " tick=" + std::to_string(ports_.tick()) +
         " speed=" + std::to_string(ports_.speed()) +
         " paused=" + std::string(ports_.paused() ? "1" : "0") +
         " restorable=1");
  return seeked && landed;
}

void ReviewSession::capturePlayback() {
  if (!open_) return;
  WorkspacePlaybackState& playback = store_.document().playback;
  playback.restorable = true;
  playback.tick = ports_.tick();
  playback.speed = ports_.speed();
  playback.paused = ports_.paused();
  playback.reverse = ports_.reverse();
  playback.reason = "captured-from-transport";
  store_.markDirty();
  captureEnvironment();
}

// The parts of the document main.cpp owns. The session always records the
// camera it can read through its own port; every other section arrives through
// the injected hook, so a section that is not wired stays at its default value
// instead of being written as if it had been read.
void ReviewSession::captureEnvironment() {
  if (!open_) return;
  WorkspaceDocument& document = store_.document();
  const WorkspaceCameraRef camera = ports_.cameraNow();
  if (camera.valid) {
    document.camera.valid = true;
    document.camera.pose = camera.pose;
    document.camera.fov = camera.fov;
    document.camera.mode = camera.mode;
    document.camera.cameraTick = camera.cameraTick;
    document.camera.cameraReason = camera.cameraReason;
  } else {
    document.camera.valid = false;
    document.camera.cameraReason =
        camera.cameraReason.empty() ? std::string("no-camera") : camera.cameraReason;
  }
  if (captureEnv_) captureEnv_(document);
  store_.markDirty();
}

// Restores the camera the file holds, then hands the remaining sections to the
// hook. The camera goes first for the same reason a bookmark jump applies the
// camera before it seeks: a refused seek still leaves the view where the file
// says it was.
void ReviewSession::applyEnvironment() {
  if (!open_) return;
  const WorkspaceDocument& document = store_.document();
  // The document keeps the camera as a STATE (it also owns the slot list and the
  // tracking target); the port takes the narrower REF, so the fields the port
  // needs are copied out rather than the whole section being handed over.
  WorkspaceCameraRef wanted;
  wanted.valid = document.camera.valid;
  wanted.pose = document.camera.pose;
  wanted.fov = document.camera.fov;
  wanted.mode = document.camera.mode;
  wanted.cameraTick = document.camera.cameraTick;
  wanted.cameraReason = document.camera.cameraReason;
  const bool camera = wanted.valid ? ports_.applyCamera(wanted) : false;
  if (applyEnv_) applyEnv_(document);
  trace_("workspace: env applied camera=" + std::string(camera ? "1" : "0") +
         " slots=" + std::to_string(document.camera.slots.size()));
}

void ReviewSession::syncDocumentFromBookmarks(const char* why) {
  std::string reason;
  if (!bookmarks_.save(store_.document(), reason)) {
    trace_(std::string("workspace: sync why=") + why + " ok=0 reason=" +
           (reason.empty() ? std::string("-") : reason));
    return;
  }
  store_.markDirty();
}

void ReviewSession::traceBookmarkLine(const char* what, const WorkspaceBookmarkRecord& record,
                                      const char* status) {
  trace_(std::string("bookmark: ") + what + " status=" + status +
         " id=" + std::to_string(record.id) +
         " tick=" + std::to_string(record.tick) +
         " title=" + (record.title.empty() ? std::string("-") : record.title) +
         " key=" + ReviewBookmarkStore::eventKey(record) +
         " camera=" + (record.camera.valid ? "1" : "0"));
}

bool ReviewSession::addManual(std::int32_t tick, const std::string& title,
                              const std::string& note, std::string& detail) {
  bookmarks_.setCursorTick(ports_.tick());
  std::uint64_t id = 0;
  std::string reason;
  const ReviewAddStatus status = bookmarks_.addManual(title, note, ports_.cameraNow(), id, reason);
  detail = std::string("status=") + reviewAddStatusName(status) + " id=" + std::to_string(id) +
           " reason=" + (reason.empty() ? std::string("-") : reason);
  if (status != ReviewAddStatus::Added) {
    trace_("bookmark: add status=" + std::string(reviewAddStatusName(status)) +
           " wantTick=" + std::to_string(tick) +
           " reason=" + (reason.empty() ? std::string("-") : reason));
    return false;
  }
  syncDocumentFromBookmarks("add");
  const WorkspaceBookmarkRecord* record = bookmarks_.find(id);
  if (record) traceBookmarkLine("add", *record, "Added");
  return true;
}

bool ReviewSession::addEvent(std::int32_t tick, const std::string& title,
                             const std::string& note, std::string& detail) {
  std::vector<ReviewEventInput> events;
  if (!lookup_ || !lookup_(tick, events) || events.empty()) {
    detail = "no-event-on-tick";
    trace_("bookmark: event status=RefusedUnknownEvent tick=" + std::to_string(tick) +
           " reason=no-event-on-tick");
    return false;
  }
  bookmarks_.setCursorTick(ports_.tick());
  std::size_t added = 0;
  std::size_t duplicate = 0;
  for (const ReviewEventInput& event : events) {
    std::uint64_t id = 0;
    std::string reason;
    const ReviewAddStatus status =
      bookmarks_.addEvent(event, title, note, ports_.cameraNow(), id, reason);
    if (status == ReviewAddStatus::Added) {
      ++added;
      const WorkspaceBookmarkRecord* record = bookmarks_.find(id);
      if (record) traceBookmarkLine("event", *record, "Added");
    } else if (status == ReviewAddStatus::AlreadyExists) {
      ++duplicate;
      trace_("bookmark: event status=AlreadyExists tick=" + std::to_string(tick) +
             " event=" + (event.name.empty() ? std::string("-") : event.name) +
             " reason=" + (reason.empty() ? std::string("-") : reason));
    } else {
      trace_("bookmark: event status=" + std::string(reviewAddStatusName(status)) +
             " tick=" + std::to_string(tick) +
             " event=" + (event.name.empty() ? std::string("-") : event.name) +
             " reason=" + (reason.empty() ? std::string("-") : reason));
    }
  }
  detail = "added=" + std::to_string(added) + " duplicate=" + std::to_string(duplicate) +
           " onTick=" + std::to_string(events.size());
  if (added > 0) syncDocumentFromBookmarks("add-event");
  return added > 0;
}

bool ReviewSession::update(std::uint64_t id, const std::string& title, const std::string& note,
                           std::string& detail) {
  std::string reason;
  const ReviewUpdateStatus status = bookmarks_.update(id, title, note, reason);
  detail = std::string("status=") + reviewUpdateStatusName(status) + " id=" + std::to_string(id);
  trace_("bookmark: update status=" + std::string(reviewUpdateStatusName(status)) +
         " id=" + std::to_string(id) +
         " title=" + (title.empty() ? std::string("-") : title) +
         " reason=" + (reason.empty() ? std::string("-") : reason));
  if (status != ReviewUpdateStatus::Updated) return false;
  syncDocumentFromBookmarks("update");
  return true;
}

bool ReviewSession::erase(std::uint64_t id, std::string& detail) {
  std::string reason;
  const ReviewDeleteStatus status = bookmarks_.remove(id, reason);
  detail = std::string("status=") + reviewDeleteStatusName(status) + " id=" + std::to_string(id);
  trace_("bookmark: delete status=" + std::string(reviewDeleteStatusName(status)) +
         " id=" + std::to_string(id) +
         " reason=" + (reason.empty() ? std::string("-") : reason));
  if (status != ReviewDeleteStatus::Removed) return false;
  syncDocumentFromBookmarks("delete");
  return true;
}

void ReviewSession::list(ReviewSort sort, const ReviewQuery* filter, const std::string& label) {
  std::vector<WorkspaceBookmarkRecord> rows =
    filter ? bookmarks_.query(*filter, sort) : bookmarks_.sorted(sort);
  trace_("bookmark: list label=" + (label.empty() ? std::string("-") : label) +
         " n=" + std::to_string(rows.size()) +
         " sort=" + reviewSortName(sort) +
         " total=" + std::to_string(bookmarks_.count()));
  for (const WorkspaceBookmarkRecord& record : rows) {
    trace_("bookmark: row " + ReviewBookmarkStore::listLine(record));
  }
}

bool ReviewSession::jump(std::uint64_t id, std::string& detail) {
  bookmarks_.setCursorTick(ports_.tick());
  std::string reason;
  const ReviewJumpStatus status = bookmarks_.jump(id, *this, reason);
  detail = std::string("status=") + reviewJumpStatusName(status) +
           " tick=" + std::to_string(bookmarks_.lastJumpTick()) +
           " key=" + bookmarks_.lastJumpEventKey();
  trace_("bookmark: jump status=" + std::string(reviewJumpStatusName(status)) +
         " id=" + std::to_string(id) +
         " tick=" + std::to_string(ports_.tick()) +
         " key=" + (bookmarks_.lastJumpEventKey().empty() ? std::string("-") : bookmarks_.lastJumpEventKey()) +
         " reason=" + (reason.empty() ? std::string("-") : reason));
  return status == ReviewJumpStatus::Applied;
}

bool ReviewSession::seek(std::int32_t tick) {
  return ports_.seek(tick);
}

bool ReviewSession::applyCamera(const WorkspaceCameraRef& camera) {
  rollbackCamera_ = ports_.cameraNow();
  rollbackValid_ = rollbackCamera_.valid;
  return ports_.applyCamera(camera);
}

void ReviewSession::restoreCamera() {
  if (rollbackValid_) ports_.applyCamera(rollbackCamera_);
  rollbackValid_ = false;
}

bool ReviewSession::save(bool force, std::string& detail) {
  capturePlayback();
  std::string bookmarkReason;
  bookmarks_.save(store_.document(), bookmarkReason);
  std::string reason;
  if (force) {
    const WorkspaceSaveStatus status = store_.save(store_.document(), reason);
    detail = std::string("status=") + workspaceSaveStatusName(status) +
             " bookmarks=" + std::to_string(bookmarks_.count());
    trace_("workspace: save status=" + std::string(workspaceSaveStatusName(status)) +
           " force=1 bookmarks=" + std::to_string(bookmarks_.count()) +
           " dirty=" + std::string(store_.dirty() ? "1" : "0") +
           " path=" + escapePath(store_.path()) +
           " reason=" + (reason.empty() ? std::string("-") : reason));
    return status == WorkspaceSaveStatus::Saved;
  }
  const WorkspaceAutosaveStatus status = store_.autosave(store_.document(), store_.dirty(), reason);
  detail = std::string("autosave=") + workspaceAutosaveStatusName(status) +
           " bookmarks=" + std::to_string(bookmarks_.count());
  trace_("workspace: save autosave=" + std::string(workspaceAutosaveStatusName(status)) +
         " force=0 bookmarks=" + std::to_string(bookmarks_.count()) +
         " dirty=" + std::string(store_.dirty() ? "1" : "0") +
         " path=" + escapePath(store_.path()) +
         " reason=" + (reason.empty() ? std::string("-") : reason));
  return status == WorkspaceAutosaveStatus::Saved || status == WorkspaceAutosaveStatus::SkippedClean;
}

void ReviewSession::finish() {
  if (!open_) {
    trace_("workspace: exit open=0 saved=0");
    return;
  }
  capturePlayback();
  std::string bookmarkReason;
  bookmarks_.save(store_.document(), bookmarkReason);
  std::string reason;
  const WorkspaceAutosaveStatus autosave = store_.autosave(store_.document(), store_.dirty(), reason);
  const WorkspaceCloseStatus close = store_.close(store_.dirty());
  trace_("workspace: exit open=1 autosave=" + std::string(workspaceAutosaveStatusName(autosave)) +
         " close=" + std::string(workspaceCloseStatusName(close)) +
         " bookmarks=" + std::to_string(bookmarks_.count()) +
         " dirty=" + std::string(store_.dirty() ? "1" : "0") +
         " reason=" + (reason.empty() ? std::string("-") : reason));
}

}  // namespace tf2::native
