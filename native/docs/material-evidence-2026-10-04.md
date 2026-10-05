# Native Material Evidence

This note records the current D3D11 material boundary. It does not claim
Source-equivalent lightmaps, cubemaps, or water rendering.

## Verified

- Release build: `cmake --build native/build --config Release --parallel 2`
- `texture_quad_probe.exe` with the installed `spray.vtf`:
  `vtfWidth=256`, `vtfHeight=256`, `featureLevel=45056`,
  `nonBlackPixels=16`, `drawPixelsNonBlack=true`.
- `material_chain_probe.exe` rejects empty, corrupt, and oversized VTF input,
  and rejects an empty BSP input.
- Explicit `spray.vtf` decode reports format `13` and `262144` RGBA bytes.
- Explicit `2koth_abbey.bsp` probe reports `bspCubemapEntities=0`,
  `cubemapCompleteSets=0`, and `realCubemapComplete=false`.

## Current shader boundary

- The world constant buffer is 128 bytes: an `mvp` matrix followed by four
  `float4` values. CPU `WorldConstantsData` matches this layout and binds it
  to `b0` for both world vertex and pixel shaders.
- `lightmapFeatures.x` is the clamped average-intensity fallback. The current
  shader does not sample per-face lightmap texels.
- Bump and env-map branches are enabled only after a non-empty decoded VTF is
  uploaded to the corresponding SRV slot.
- Self-illumination tint components are finite-checked and clamped to `[0, 4]`
  before upload. The shader applies the tint as an approximation.

## Not established by local evidence

No local resource and pixel readback currently establishes six-face cubemap
sampling, per-face lightmap interpolation, or complete Source water behavior.
The reported modes therefore remain `approximate-2d` and `average-intensity`.
