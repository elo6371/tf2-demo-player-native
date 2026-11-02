# T9 -- splitting native/src/native_renderer.cpp

Branch `task/t9-renderer-split`, base `d5839b5` (the published HEAD), worktree
`D:/TF2_Native_Worktrees/t9-renderer`. Nothing here is merged or pushed.

## What the file was

1,757 lines, four functions:

| function | lines |
|---|---|
| `Renderer::createPipeline()` | 115-1568 (**1,454**) |
| `Renderer::initialize()` | 49-113 |
| `Renderer::uploadTexture()` | 1,569-1,578 |
| `Renderer::resize()` / `shutdown()` | 1,580-1,600 |

`createPipeline()` is the D3D state creation -- device-side blend/depth/raster
states, buffers, constant layouts, the overlay and entity pipelines -- and
**854 of its 1,454 lines were 14 HLSL programs written inline as `R"HLSL(...)HLSL"`
string literals** (world pixel shader alone 288 lines, view-model vertex 117,
view-model pixel 115, entity-model vertex 98, overlay vertex 74).

The same review lines therefore covered two different things: text the GPU
compiles, and calls the CPU makes. A shader edit and a state edit were
indistinguishable, and the function was too long to read in one screen anyway.

## What moved

The 14 literals moved verbatim into a new unit:

- `native/src/native_renderer_shaders.h` (39 lines): `extern const char <name>[];`
  for each program, in `namespace tf2::native::shaders`, with the reason written
  down.
- `native/src/native_renderer_shaders.cpp` (874 lines): the 14 definitions,
  character for character, delimiters included.
- `native/cmake/tf2-product.cmake`: one entry, `src/native_renderer_shaders.cpp`,
  next to the T77 comment that already describes the renderer split.
- `native_renderer.cpp` includes the new header and `createPipeline()` opens with
  `using namespace tf2::native::shaders;`, so every `compileShader(vertexSource,
  ...)` call site is unchanged.

`native_renderer.cpp` 1,757 -> **906** lines; `createPipeline()` 1,454 -> **603**.

## What did not change

- The shader text. A script compared every block against
  `git show HEAD:native/src/native_renderer.cpp`: **14/14 identical, 42,735 bytes
  both sides, zero mismatches**. The header declares exactly those 14 names.
- The pass order, which lives in `draw()` (`native_renderer_frame.cpp`), and every
  threshold and format in the pipelines.
- No target name, command line, trace line or probe output changed.

## How it was checked

1. `bash build-target.sh tf2_demo_native` -- rc=0.
2. `bash frame-capture-check.sh` -- the product must hand back the frame it
   composed, and the file must be checkable without an eye: `FRAME-CAPTURE=PASS`,
   **18 OK / 0 FAIL**, rc=0, including `two captures of the same static scene are
   byte-identical` (sha256 `37a48004...`). Log:
   `/d/_shared/t9/frame-capture.log`, copied next to this file.

## Still in native_renderer.cpp, and why

`createPipeline()` is still 603 lines: it creates eleven blend/depth/raster state
objects, five constant buffers and their layouts, and compiles the 14 programs.
That shape is the remaining work -- a per-pipeline unit (overlay, entity, view
model, HUD) would be the honest next cut -- and it needs its own round because it
touches how the state objects are owned, not just where the text lives.
