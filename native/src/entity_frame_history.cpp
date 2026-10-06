#include "entity_frame_history.h"

#include <algorithm>

namespace tf2::native {

EntityFrameHistory::EntityFrameHistory(std::size_t maxEntities)
    : maxEntities_(maxEntities), current_{} {
}

void EntityFrameHistory::clear() {
  current_ = {};
  frames_.clear();
}

const HistoryFrame* EntityFrameHistory::find(std::int32_t tick) const {
  for (const auto& frame : frames_) {
    if (frame.tick == tick) return &frame;
  }
  return nullptr;
}

EntityFrameApplyResult EntityFrameHistory::apply(
    std::int32_t tick, std::int32_t deltaFrom,
    const std::vector<EntityFrameUpdate>& updates) {
  HistoryFrame next;
  if (deltaFrom >= 0) {
    const HistoryFrame* base = find(deltaFrom);
    if (!base) return EntityFrameApplyResult::MissingDeltaBase;
    next = *base;
  } else {
  }
  next.tick = tick;
  next.deltaFrom = deltaFrom;
  for (const auto& update : updates) {
    if (update.index >= maxEntities_) return EntityFrameApplyResult::InvalidEntity;
    auto existing = next.entities.find(update.index);
    EntityFrameState empty;
    auto& entity = existing == next.entities.end() ? next.entities.emplace(update.index, std::move(empty)).first->second : existing->second;
    switch (update.kind) {
      case EntityFrameUpdate::Kind::Enter:
        if (update.serial > 1023) return EntityFrameApplyResult::InvalidSerial;
        entity = {};
        entity.classId = update.classId;
        entity.serial = update.serial;
        entity.present = true;
        entity.transmitted = true;
        entity.properties = update.properties;
        break;
      case EntityFrameUpdate::Kind::Delta:
      case EntityFrameUpdate::Kind::Preserve:
        if (!entity.present || entity.serial != update.serial) return EntityFrameApplyResult::InvalidSerial;
        entity.transmitted = true;
        for (const auto& [name, value] : update.properties) entity.properties[name] = value;
        break;
      case EntityFrameUpdate::Kind::Leave:
        if (!entity.present || entity.serial != update.serial) return EntityFrameApplyResult::InvalidSerial;
        entity.transmitted = false;
        break;
      case EntityFrameUpdate::Kind::Delete:
        next.entities.erase(update.index);
        break;
    }
  }
  current_ = next;
  frames_.push_back(std::move(next));
  return EntityFrameApplyResult::Applied;
}

}  // namespace tf2::native
