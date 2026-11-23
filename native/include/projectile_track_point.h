#pragma once

// ============================================================================
// T53: one observed projectile position, and the ONLY thing the renderer and the
// overlay builder share.
//
// WHY ITS OWN HEADER, WITH NO INCLUDES AT ALL.
//   native_renderer.h pulls in <d3d11.h>; demo_header.h pulls in the demo
//   decoder. The renderer must not learn about the decoder, and the builder must
//   not need a GPU to be tested. A three-float POD in a header that includes
//   nothing lets both include it without either dependency, and it keeps the two
//   from disagreeing about the struct layout -- which is the failure a duplicated
//   POD would produce silently, since the fields would still compile.
// ============================================================================

#include <cstdint>

namespace tf2::native {

struct ProjectileTrackPoint {
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
  // Age in demo ticks. Zero means the projectile is here now; a point at the
  // linger limit fades out. Carried on the point rather than computed from a
  // clock inside the renderer, so a captured frame is reproducible from
  // (tick, retained rows) alone.
  float ageTicks = 0.0f;
  std::uint16_t entityIndex = 0;
};

} // namespace tf2::native
