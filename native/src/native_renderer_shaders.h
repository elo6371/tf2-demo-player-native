#pragma once

// T77 (2026-11-02): the HLSL the renderer compiles, held apart from the code
// that creates the D3D state which uses it.
//
// WHY THIS FILE EXISTS
//   Renderer::createPipeline() was 1,454 lines, and 854 of them were shader
//   source inside the function body. A shader edit and a pipeline-state edit
//   looked the same in review: one long function, no boundary between text
//   the GPU compiles and calls the CPU makes. The text is now here, one
//   named block per program, and the function that was hiding it says
//   "compile shaders::worldPixelSource" instead of carrying 288 lines of HLSL
//   in its middle.
//
// WHAT DID NOT CHANGE
//   The bytes. Every literal below is the block that was in createPipeline,
//   character for character, including the R"HLSL(...)HLSL" delimiters. The
//   capture gate compares the rendered frame against the same pinned hash it
//   compared before, so an accidental edit to a shader shows up as a moved
//   picture rather than as a compile error.

namespace tf2::native::shaders {

extern const char vertexSource[];
extern const char pixelSource[];
extern const char worldVertexSource[];
extern const char worldPixelSource[];
extern const char skyPixelSource[];
// N05 step 3: a passthrough geometry shader whose only job is to stream the
// world vertex shader's own SV_POSITION out to a buffer. It re-declares the
// world VS output struct field for field and copies every field through, so the
// rasteriser sees exactly the vertices it would have seen without it; only the
// stream output declaration narrows what is written back, to position alone.
extern const char worldClipCaptureGeometrySource[];
extern const char modelVertexSource[];
extern const char modelPixelSource[];
extern const char overlayLineVertexSource[];
extern const char overlayVertexSource[];
extern const char overlayPixelSource[];
extern const char entityModelVertexSource[];
extern const char viewModelVertexSource[];
extern const char viewModelPixelSource[];
extern const char hudVertexSource[];
extern const char hudPixelSource[];

}  // namespace tf2::native::shaders
