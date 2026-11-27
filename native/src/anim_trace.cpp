// F-group (G-8 trace hygiene): the destination for animation trace code.
//
// Why this file exists
// --------------------
// The product file is 18,863 lines against a 12,000-line target, and every
// trace-only commit kept adding another block to it. Those blocks live inside
// the animation loop and reach for the sampled pose, the draw instance and the
// clock, so moving them later means moving the loop with them; the longer they
// stay, the more the structural target costs. The 2026-11-25 tasklist therefore
// makes this file the destination for new animation-trace code: readers and
// writers added from now on belong here, not in main.cpp.
//
// This commit creates the destination only. The translation unit intentionally
// has no behaviour: it compiles to an empty object, it registers no static
// initializer, and a default run must not change by a byte because of its
// presence. The pose-gate trace changes that motivated the split
// (serverTick=/sceneTick= on the anim lines, posebones by bone name) are still
// carried by main.cpp this round; they move in the G-8 follow-up, when the
// surrounding loop can move with them.
//
// The build already compiles this file (native/cmake/tf2-product.cmake), so the
// graph carries the destination before the first move lands.