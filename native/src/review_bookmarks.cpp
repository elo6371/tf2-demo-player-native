#include "review_bookmarks.h"

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstdio>
#include <cstdlib>

namespace tf2::native {
namespace {

std::string trimAscii(const std::string& text) {
  std::size_t begin = 0;
  std::size_t end = text.size();
  while (begin < end && (text[begin] == ' ' || text[begin] == '\t' || text[begin] == '\r' || text[begin] == '\n')) ++begin;
  while (end > begin && (text[end - 1] == ' ' || text[end - 1] == '\t' || text[end - 1] == '\r' || text[end - 1] == '\n')) --end;
  return text.substr(begin, end - begin);
}

char asciiLower(char c) {
  return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

bool containsAsciiFold(const std::string& haystack, const std::string& needle) {
  if (needle.empty()) return true;
  if (haystack.size() < needle.size()) return false;
  for (std::size_t i = 0; i + needle.size() <= haystack.size(); ++i) {
    bool match = true;
    for (std::size_t j = 0; j < needle.size(); ++j) {
      if (asciiLower(haystack[i + j]) != asciiLower(needle[j])) {
        match = false;
        break;
      }
    }
    if (match) return true;
  }
  return false;
}

bool parseIntStrict(const std::string& text, std::int64_t& out) {
  if (text.empty()) return false;
  std::size_t index = 0;
  if (text[0] == '-') index = 1;
  if (index >= text.size()) return false;
  for (std::size_t i = index; i < text.size(); ++i) {
    if (text[i] < '0' || text[i] > '9') return false;
  }
  errno = 0;
  char* end = nullptr;
  const long long value = std::strtoll(text.c_str(), &end, 10);
  if (end != text.c_str() + text.size() || errno == ERANGE) return false;
  out = static_cast<std::int64_t>(value);
  return true;
}

}  // namespace

const char* reviewLoadStateName(ReviewLoadState state) {
  switch (state) {
    case ReviewLoadState::NotLoaded: return "not_loaded";
    case ReviewLoadState::Loaded: return "loaded";
    case ReviewLoadState::Missing: return "missing";
    case ReviewLoadState::Corrupt: return "corrupt";
    case ReviewLoadState::HashMismatch: return "hash_mismatch";
    case ReviewLoadState::OldSchema: return "old_schema";
  }
  return "not_loaded";
}

const char* reviewAddStatusName(ReviewAddStatus status) {
  switch (status) {
    case ReviewAddStatus::Added: return "added";
    case ReviewAddStatus::AlreadyExists: return "already_exists";
    case ReviewAddStatus::RefusedEmptyTitle: return "refused_empty_title";
    case ReviewAddStatus::RefusedSeekPending: return "refused_seek_pending";
    case ReviewAddStatus::RefusedNoDemo: return "refused_no_demo";
    case ReviewAddStatus::RefusedNotLoaded: return "refused_not_loaded";
    case ReviewAddStatus::RefusedUnknownEvent: return "refused_unknown_event";
    case ReviewAddStatus::RefusedHashMismatch: return "refused_hash_mismatch";
    case ReviewAddStatus::RefusedInvalidTick: return "refused_invalid_tick";
  }
  return "refused_not_loaded";
}

const char* reviewUpdateStatusName(ReviewUpdateStatus status) {
  switch (status) {
    case ReviewUpdateStatus::Updated: return "updated";
    case ReviewUpdateStatus::NotFound: return "not_found";
    case ReviewUpdateStatus::RefusedEmptyTitle: return "refused_empty_title";
    case ReviewUpdateStatus::RefusedNotLoaded: return "refused_not_loaded";
    case ReviewUpdateStatus::RefusedHashMismatch: return "refused_hash_mismatch";
  }
  return "refused_not_loaded";
}

const char* reviewDeleteStatusName(ReviewDeleteStatus status) {
  switch (status) {
    case ReviewDeleteStatus::Removed: return "removed";
    case ReviewDeleteStatus::NotFound: return "not_found";
    case ReviewDeleteStatus::RefusedNotLoaded: return "refused_not_loaded";
  }
  return "refused_not_loaded";
}

const char* reviewJumpStatusName(ReviewJumpStatus status) {
  switch (status) {
    case ReviewJumpStatus::Applied: return "applied";
    case ReviewJumpStatus::RefusedNotFound: return "refused_not_found";
    case ReviewJumpStatus::RefusedHashMismatch: return "refused_hash_mismatch";
    case ReviewJumpStatus::RefusedOutOfRange: return "refused_out_of_range";
    case ReviewJumpStatus::RefusedCamera: return "refused_camera";
    case ReviewJumpStatus::RefusedPlayback: return "refused_playback";
    case ReviewJumpStatus::RefusedNoDemo: return "refused_no_demo";
    case ReviewJumpStatus::RefusedNotLoaded: return "refused_not_loaded";
  }
  return "refused_not_loaded";
}

const char* reviewSortName(ReviewSort sort) {
  switch (sort) {
    case ReviewSort::Tick: return "tick";
    case ReviewSort::Created: return "created";
    case ReviewSort::Title: return "title";
  }
  return "tick";
}

void ReviewBookmarkStore::setDemo(const std::string& demoHash, std::int32_t endTick) {
  demoHash_ = demoHash;
  endTick_ = endTick;
}

void ReviewBookmarkStore::clearDemo() {
  demoHash_.clear();
  endTick_ = -1;
}

void ReviewBookmarkStore::setCursorTick(std::int32_t tick) { cursorTick_ = tick; }
void ReviewBookmarkStore::setSeekPending(bool pending) { seekPending_ = pending; }
void ReviewBookmarkStore::setNowUtc(std::int64_t seconds) { nowUtc_ = seconds; }

void ReviewBookmarkStore::noteOpenState(ReviewLoadState state, const std::string& reason) {
  loadState_ = state;
  loadReason_ = reason;
  if (state != ReviewLoadState::Loaded) {
    ++counters_.loadFailures;
    bookmarks_.clear();
    nextBookmarkId_ = 1;
    dirty_ = false;
  }
}

ReviewLoadState ReviewBookmarkStore::load(const WorkspaceDocument& workspace, std::string& reason) {
  if (demoHash_.empty()) {
    loadState_ = ReviewLoadState::NotLoaded;
    loadReason_ = "no-demo";
    ++counters_.loadFailures;
    reason = loadReason_;
    return loadState_;
  }
  if (!workspace.demoHash.empty() && workspace.demoHash != demoHash_) {
    loadState_ = ReviewLoadState::HashMismatch;
    loadReason_ = "demo-hash-mismatch";
    ++counters_.loadFailures;
    reason = loadReason_;
    return loadState_;
  }
  bookmarks_ = workspace.bookmarks;
  nextBookmarkId_ = workspace.nextBookmarkId;
  dirty_ = false;
  loadState_ = ReviewLoadState::Loaded;
  loadReason_ = "loaded";
  reason = loadReason_;
  return loadState_;
}

bool ReviewBookmarkStore::save(WorkspaceDocument& workspace, std::string& reason) {
  if (loadState_ != ReviewLoadState::Loaded) {
    ++counters_.saveFailures;
    reason = "not-loaded";
    return false;
  }
  if (!demoHash_.empty() && workspace.demoHash != demoHash_) {
    ++counters_.saveFailures;
    reason = "hash-mismatch";
    return false;
  }
  workspace.bookmarks = bookmarks_;
  workspace.nextBookmarkId = nextBookmarkId_;
  reason = "saved";
  return true;
}

ReviewAddStatus ReviewBookmarkStore::checkAddReady(std::string& reason) const {
  if (seekPending_) {
    reason = "seek-pending";
    return ReviewAddStatus::RefusedSeekPending;
  }
  if (demoHash_.empty()) {
    reason = "no-demo";
    return ReviewAddStatus::RefusedNoDemo;
  }
  if (loadState_ == ReviewLoadState::HashMismatch) {
    reason = "hash-mismatch";
    return ReviewAddStatus::RefusedHashMismatch;
  }
  if (loadState_ != ReviewLoadState::Loaded) {
    reason = "not-loaded";
    return ReviewAddStatus::RefusedNotLoaded;
  }
  return ReviewAddStatus::Added;
}

void ReviewBookmarkStore::normalizeCamera(WorkspaceCameraRef& camera) {
  camera.pose.valid = camera.valid;
  if (!camera.valid && camera.cameraReason.empty()) camera.cameraReason = "no-renderer";
  if (camera.valid) camera.cameraReason.clear();
}

WorkspaceBookmarkRecord ReviewBookmarkStore::makeRecord(std::int32_t tick, const std::string& title,
                                                       const std::string& note,
                                                       const WorkspaceCameraRef& camera) const {
  WorkspaceBookmarkRecord record;
  record.id = nextBookmarkId_;
  record.tick = tick;
  record.title = title;
  record.note = note;
  record.camera = camera;
  normalizeCamera(record.camera);
  record.createdAtUtc = nowUtc_;
  record.updatedAtUtc = nowUtc_;
  record.demoHash = demoHash_;
  record.recordVersion = 1;
  return record;
}

ReviewAddStatus ReviewBookmarkStore::addManual(const std::string& title, const std::string& note,
                                               const WorkspaceCameraRef& camera,
                                               std::uint64_t& outId, std::string& reason) {
  const ReviewAddStatus ready = checkAddReady(reason);
  if (ready != ReviewAddStatus::Added) {
    ++counters_.refused;
    return ready;
  }
  if (trimAscii(title).empty()) {
    ++counters_.refused;
    reason = "empty-title";
    return ReviewAddStatus::RefusedEmptyTitle;
  }
  if (cursorTick_ < 0 || (endTick_ >= 0 && cursorTick_ > endTick_)) {
    ++counters_.refused;
    reason = "invalid-tick";
    return ReviewAddStatus::RefusedInvalidTick;
  }
  WorkspaceBookmarkRecord record = makeRecord(cursorTick_, title, note, camera);
  record.eventPresent = false;
  record.eventName.clear();
  record.eventId = 0;
  record.eventTick = -1;
  record.eventPacketOrdinal = 0;
  record.eventMessageBit = 0;
  record.players.clear();
  record.identityReason = "no-player-context";
  outId = record.id;
  bookmarks_.push_back(std::move(record));
  ++nextBookmarkId_;
  dirty_ = true;
  ++counters_.added;
  reason = "added";
  return ReviewAddStatus::Added;
}

ReviewAddStatus ReviewBookmarkStore::addEvent(const ReviewEventInput& event, const std::string& title,
                                              const std::string& note,
                                              const WorkspaceCameraRef& camera,
                                              std::uint64_t& outId, std::string& reason) {
  const ReviewAddStatus ready = checkAddReady(reason);
  if (ready != ReviewAddStatus::Added) {
    ++counters_.refused;
    return ready;
  }
  if (trimAscii(title).empty()) {
    ++counters_.refused;
    reason = "empty-title";
    return ReviewAddStatus::RefusedEmptyTitle;
  }
  if (event.tick < 0 || (endTick_ >= 0 && event.tick > endTick_) || event.name.empty()) {
    ++counters_.refused;
    reason = event.name.empty() ? "unknown-event" : "invalid-tick";
    return event.name.empty() ? ReviewAddStatus::RefusedUnknownEvent : ReviewAddStatus::RefusedInvalidTick;
  }
  for (const auto& existing : bookmarks_) {
    if (!existing.eventPresent) continue;
    if (existing.tick == event.tick && existing.eventPacketOrdinal == event.packetOrdinal &&
        existing.eventMessageBit == event.messageBit) {
      outId = existing.id;
      ++counters_.duplicateEvent;
      ++counters_.refused;
      reason = "duplicate-event-key " + eventKey(existing) + " id=" + std::to_string(existing.id);
      return ReviewAddStatus::AlreadyExists;
    }
  }
  WorkspaceBookmarkRecord record = makeRecord(event.tick, title, note, camera);
  record.eventPresent = true;
  record.eventName = event.name;
  record.eventId = event.eventId;
  record.eventTick = event.tick;
  record.eventPacketOrdinal = event.packetOrdinal;
  record.eventMessageBit = event.messageBit;
  record.players = event.players;
  if (record.players.empty()) {
    record.identityReason = event.identityReason.empty() ? "no-player-context" : event.identityReason;
  } else {
    bool anyName = false;
    for (std::size_t i = 0; i < record.players.size(); ++i) {
      if (!record.players[i].name.empty()) anyName = true;
      if (record.players[i].name.empty() && record.players[i].nameReason.empty()) {
        record.players[i].nameReason = "unavailable";
      }
    }
    record.identityReason = anyName ? std::string() : (event.identityReason.empty() ? "no-userinfo" : event.identityReason);
  }
  outId = record.id;
  bookmarks_.push_back(std::move(record));
  ++nextBookmarkId_;
  dirty_ = true;
  ++counters_.added;
  reason = "added";
  return ReviewAddStatus::Added;
}

ReviewUpdateStatus ReviewBookmarkStore::update(std::uint64_t id, const std::string& title,
                                               const std::string& note, std::string& reason) {
  if (loadState_ != ReviewLoadState::Loaded) {
    reason = loadState_ == ReviewLoadState::HashMismatch ? "hash-mismatch" : "not-loaded";
    ++counters_.refused;
    return loadState_ == ReviewLoadState::HashMismatch ? ReviewUpdateStatus::RefusedHashMismatch
                                                       : ReviewUpdateStatus::RefusedNotLoaded;
  }
  if (trimAscii(title).empty()) {
    reason = "empty-title";
    ++counters_.refused;
    return ReviewUpdateStatus::RefusedEmptyTitle;
  }
  for (auto& record : bookmarks_) {
    if (record.id != id) continue;
    record.title = title;
    record.note = note;
    record.updatedAtUtc = nowUtc_;
    dirty_ = true;
    ++counters_.updated;
    reason = "updated";
    return ReviewUpdateStatus::Updated;
  }
  reason = "not-found";
  ++counters_.refused;
  return ReviewUpdateStatus::NotFound;
}

ReviewDeleteStatus ReviewBookmarkStore::remove(std::uint64_t id, std::string& reason) {
  if (loadState_ != ReviewLoadState::Loaded) {
    reason = "not-loaded";
    ++counters_.refused;
    return ReviewDeleteStatus::RefusedNotLoaded;
  }
  for (auto it = bookmarks_.begin(); it != bookmarks_.end(); ++it) {
    if (it->id != id) continue;
    bookmarks_.erase(it);
    dirty_ = true;
    ++counters_.deleted;
    reason = "removed";
    return ReviewDeleteStatus::Removed;
  }
  reason = "not-found";
  ++counters_.refused;
  return ReviewDeleteStatus::NotFound;
}

std::vector<WorkspaceBookmarkRecord> ReviewBookmarkStore::sorted(ReviewSort sort) const {
  std::vector<WorkspaceBookmarkRecord> rows = bookmarks_;
  switch (sort) {
    case ReviewSort::Tick:
      std::sort(rows.begin(), rows.end(), [](const WorkspaceBookmarkRecord& a, const WorkspaceBookmarkRecord& b) {
        if (a.tick != b.tick) return a.tick < b.tick;
        if (a.eventPacketOrdinal != b.eventPacketOrdinal) return a.eventPacketOrdinal < b.eventPacketOrdinal;
        if (a.eventMessageBit != b.eventMessageBit) return a.eventMessageBit < b.eventMessageBit;
        return a.id < b.id;
      });
      break;
    case ReviewSort::Created:
      std::sort(rows.begin(), rows.end(), [](const WorkspaceBookmarkRecord& a, const WorkspaceBookmarkRecord& b) {
        if (a.createdAtUtc != b.createdAtUtc) return a.createdAtUtc < b.createdAtUtc;
        return a.id < b.id;
      });
      break;
    case ReviewSort::Title:
      std::sort(rows.begin(), rows.end(), [](const WorkspaceBookmarkRecord& a, const WorkspaceBookmarkRecord& b) {
        if (a.title != b.title) return a.title < b.title;
        return a.id < b.id;
      });
      break;
  }
  return rows;
}

namespace {

bool playerMatches(const WorkspaceBookmarkRecord& record, const std::string& filter) {
  if (filter.empty()) return true;
  if (filter.rfind("userid:", 0) == 0) {
    std::int64_t wanted = 0;
    if (!parseIntStrict(filter.substr(7), wanted)) return false;
    for (const auto& player : record.players) {
      if (player.userid == wanted) return true;
    }
    return false;
  }
  if (filter.rfind("entindex:", 0) == 0) {
    std::int64_t wanted = 0;
    if (!parseIntStrict(filter.substr(9), wanted)) return false;
    for (const auto& player : record.players) {
      if (player.entindex == wanted) return true;
    }
    return false;
  }
  for (const auto& player : record.players) {
    if (containsAsciiFold(player.name, filter)) return true;
  }
  return false;
}

bool textMatches(const WorkspaceBookmarkRecord& record, const std::string& text) {
  if (text.empty()) return true;
  if (containsAsciiFold(record.title, text)) return true;
  if (containsAsciiFold(record.note, text)) return true;
  if (containsAsciiFold(record.eventName, text)) return true;
  for (const auto& player : record.players) {
    if (containsAsciiFold(player.name, text)) return true;
    if (player.userid >= 0 && containsAsciiFold(std::to_string(player.userid), text)) return true;
  }
  return false;
}

}  // namespace

std::vector<WorkspaceBookmarkRecord> ReviewBookmarkStore::query(const ReviewQuery& filter,
                                                                ReviewSort sort) const {
  std::vector<WorkspaceBookmarkRecord> rows;
  for (const auto& record : sorted(sort)) {
    if (!textMatches(record, filter.text)) continue;
    if (!filter.eventName.empty()) {
      const std::string name = record.eventPresent ? record.eventName : std::string("manual");
      if (name != filter.eventName) continue;
    }
    if (!playerMatches(record, filter.player)) continue;
    if (filter.tickMin >= 0 && record.tick < filter.tickMin) continue;
    if (filter.tickMax >= 0 && record.tick > filter.tickMax) continue;
    rows.push_back(record);
  }
  return rows;
}

ReviewJumpStatus ReviewBookmarkStore::jump(std::uint64_t id, ReviewJumpPorts& ports, std::string& reason) {
  if (loadState_ != ReviewLoadState::Loaded) {
    ++counters_.jumpFailures;
    reason = loadState_ == ReviewLoadState::HashMismatch ? "hash-mismatch" : "not-loaded";
    return loadState_ == ReviewLoadState::HashMismatch ? ReviewJumpStatus::RefusedHashMismatch
                                                       : ReviewJumpStatus::RefusedNotLoaded;
  }
  if (demoHash_.empty()) {
    ++counters_.jumpFailures;
    reason = "no-demo";
    return ReviewJumpStatus::RefusedNoDemo;
  }
  const WorkspaceBookmarkRecord* record = find(id);
  if (record == nullptr) {
    ++counters_.jumpFailures;
    reason = "not-found";
    return ReviewJumpStatus::RefusedNotFound;
  }
  if (record->demoHash != demoHash_) {
    ++counters_.jumpFailures;
    reason = "hash-mismatch";
    return ReviewJumpStatus::RefusedHashMismatch;
  }
  if (record->tick < 0 || (endTick_ >= 0 && record->tick > endTick_)) {
    ++counters_.jumpFailures;
    reason = "out-of-range tick=" + std::to_string(record->tick);
    return ReviewJumpStatus::RefusedOutOfRange;
  }
  bool cameraApplied = false;
  if (record->camera.valid) {
    // Camera first, playback second (T72 4.3.4): a refused apply restores the
    // pre-jump camera and leaves the cursor alone; a refused seek rolls the
    // already-applied camera back.
    if (!ports.applyCamera(record->camera)) {
      ports.restoreCamera();
      ++counters_.jumpFailures;
      reason = "camera";
      return ReviewJumpStatus::RefusedCamera;
    }
    cameraApplied = true;
  }
  if (!ports.seek(record->tick)) {
    if (cameraApplied) ports.restoreCamera();
    ++counters_.jumpFailures;
    reason = "playback";
    return ReviewJumpStatus::RefusedPlayback;
  }
  cursorTick_ = record->tick;
  lastJumpEventKey_ = eventKey(*record);
  lastJumpTick_ = record->tick;
  reason = "applied";
  return ReviewJumpStatus::Applied;
}

const WorkspaceBookmarkRecord* ReviewBookmarkStore::find(std::uint64_t id) const {
  for (const auto& record : bookmarks_) {
    if (record.id == id) return &record;
  }
  return nullptr;
}

std::string ReviewBookmarkStore::eventKey(const WorkspaceBookmarkRecord& record) {
  if (!record.eventPresent) return "none";
  return std::to_string(record.tick) + ":" + std::to_string(record.eventPacketOrdinal) + ":" +
         std::to_string(record.eventMessageBit);
}

std::string ReviewBookmarkStore::playersTrace(const std::vector<WorkspacePlayerRef>& players) {
  if (players.empty()) return "none";
  std::string out;
  for (std::size_t i = 0; i < players.size(); ++i) {
    if (i != 0) out += ';';
    out += workspacePercentEncode(players[i].role);
    out += ',';
    out += std::to_string(players[i].entindex);
    out += ',';
    out += std::to_string(players[i].userid);
    out += ',';
    out += workspacePercentEncode(players[i].name);
  }
  return out;
}

std::string ReviewBookmarkStore::identityTrace(const WorkspaceBookmarkRecord& record) {
  if (record.players.empty()) return record.identityReason.empty() ? "unavailable" : record.identityReason;
  if (!record.identityReason.empty()) return record.identityReason;
  for (const auto& player : record.players) {
    if (!player.nameReason.empty()) return player.nameReason;
  }
  return "available";
}

std::string ReviewBookmarkStore::listLine(const WorkspaceBookmarkRecord& record) {
  std::string out = "list-item id=" + std::to_string(record.id) + " tick=" + std::to_string(record.tick) +
                    " title=" + workspacePercentEncode(record.title) + " eventKey=" + eventKey(record) +
                    " players=" + playersTrace(record.players) + " nameReason=" + identityTrace(record) +
                    " camera=" + (record.camera.valid ? "valid" : "unavailable:" + workspacePercentEncode(record.camera.cameraReason)) +
                    " createdUtc=" + std::to_string(record.createdAtUtc) +
                    " updatedUtc=" + std::to_string(record.updatedAtUtc);
  return out;
}

}  // namespace tf2::native
