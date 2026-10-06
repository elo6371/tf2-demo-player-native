#include "entity_frame_history.h"

#include <iostream>

using namespace tf2::native;

static EntityFrameUpdate enter(std::uint16_t index, std::int32_t cls, std::uint16_t serial, std::int64_t x) {
  EntityFrameUpdate update;
  update.kind = EntityFrameUpdate::Kind::Enter;
  update.index = index;
  update.classId = cls;
  update.serial = serial;
  update.properties["x"] = x;
  return update;
}

static EntityFrameUpdate update(EntityFrameUpdate::Kind kind, std::uint16_t index, std::uint16_t serial, std::int64_t x = 0) {
  EntityFrameUpdate result;
  result.kind = kind;
  result.index = index;
  result.serial = serial;
  if (kind == EntityFrameUpdate::Kind::Delta || kind == EntityFrameUpdate::Kind::Preserve) result.properties["x"] = x;
  return result;
}

int main() {
  EntityFrameHistory history(8);
  if (history.apply(10, -1, {enter(2, 7, 3, 10)}) != EntityFrameApplyResult::Applied) return 1;
  if (history.apply(11, 10, {update(EntityFrameUpdate::Kind::Delta, 2, 3, 11)}) != EntityFrameApplyResult::Applied) return 2;
  if (history.apply(12, 11, {update(EntityFrameUpdate::Kind::Preserve, 2, 3, 12)}) != EntityFrameApplyResult::Applied) return 3;
  const auto entity = history.current().entities.find(2);
  if (entity == history.current().entities.end()) return 4;
  const auto property = entity->second.properties.find("x");
  if (property == entity->second.properties.end() || property->second != 12 || !entity->second.transmitted) return 4;
  if (history.apply(13, 12, {update(EntityFrameUpdate::Kind::Leave, 2, 3)}) != EntityFrameApplyResult::Applied) return 5;
  if (history.current().entities.find(2)->second.transmitted || !history.current().entities.find(2)->second.present) return 6;
  if (history.apply(14, 13, {update(EntityFrameUpdate::Kind::Delete, 2, 3)}) != EntityFrameApplyResult::Applied) return 7;
  if (history.current().entities.find(2) != history.current().entities.end()) return 8;
  if (history.apply(15, 999, {}) != EntityFrameApplyResult::MissingDeltaBase) return 9;
  if (history.apply(16, 14, {update(EntityFrameUpdate::Kind::Preserve, 2, 3)}) != EntityFrameApplyResult::InvalidSerial) return 10;
  std::cout << "entity_frame_history=PASS frames=" << history.frameCount() << "\n";
  return 0;
}
