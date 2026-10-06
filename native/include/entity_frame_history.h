#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace tf2::native {

struct EntityFrameState {
  std::int32_t classId = -1;
  std::uint16_t serial = 0;
  bool present = false;
  bool transmitted = false;
  std::unordered_map<std::string, std::int64_t> properties;
};

struct HistoryFrame {
  std::int32_t tick = -1;
  std::int32_t deltaFrom = -1;
  std::vector<EntityFrameState> entities;
};

enum class EntityFrameApplyResult {
  Applied,
  MissingDeltaBase,
  InvalidEntity,
  InvalidSerial,
};

struct EntityFrameUpdate {
  enum class Kind { Enter, Delta, Preserve, Leave, Delete } kind = Kind::Preserve;
  std::uint16_t index = 0;
  std::int32_t classId = -1;
  std::uint16_t serial = 0;
  std::unordered_map<std::string, std::int64_t> properties;
};

class EntityFrameHistory {
 public:
  explicit EntityFrameHistory(std::size_t maxEntities = 2048);

  void clear();
  EntityFrameApplyResult apply(std::int32_t tick, std::int32_t deltaFrom,
                               const std::vector<EntityFrameUpdate>& updates);
  const HistoryFrame* find(std::int32_t tick) const;
  const HistoryFrame& current() const { return current_; }
  std::size_t frameCount() const { return frames_.size(); }

 private:
  std::size_t maxEntities_;
  HistoryFrame current_;
  std::vector<HistoryFrame> frames_;
};

}  // namespace tf2::native
