// T61.1 review 2, P1: the per-entity diagnostic id is a COLOUR, and the only
// channel that survives the trip to the frame and back is one 8-bit unorm. This
// header owns that mapping so the product and its test cannot disagree about it,
// and so the ceiling is a named constant instead of a magic 255 at the paint
// site -- which is exactly where "more than 255 bodies" would otherwise be
// rounded silently into two bodies sharing a colour, and the reader would then
// decode one entity's pixels as another entity's.
#pragma once

#include <cstddef>

namespace tf2::native {

// One red byte carries the id, so 255 distinct bodies is the hard ceiling.
inline constexpr int kEntityIdMax = 255;

// Writes {id/255, 0, 0, 1} into `color` and returns true, or leaves `color`
// untouched and returns false when the id cannot be represented exactly.
// Green and blue stay zero on purpose: the reader's motion test requires an id
// pixel to be (id, 0, 0), so an encoder that spread the value across channels
// would break the identity of "this pixel belongs to that body".
inline bool encodeEntityId(int id, float* color) {
  if (color == nullptr || id < 1 || id > kEntityIdMax) return false;
  color[0] = static_cast<float>(id) / static_cast<float>(kEntityIdMax);
  color[1] = 0.0f;
  color[2] = 0.0f;
  color[3] = 1.0f;
  return true;
}

}  // namespace tf2::native
