# Entity model instances (module 1) 2026-10-05

Local work only. Do not treat this file as a GitHub browse target.

## What changed

- VPK MDL/VVD/VTX bytes can be inspected (`ModelLoader::inspectVpk`).
- `buildRenderRequests` inspects both loose files and VPK companions, caches by normalized path, and records `cacheKey=path#checksum`.
- `EntityModelResolver` extracts origin/angles/team/skin/class and builds a bounded instance list. Players without a Demo model path fall back to `models/player/<class>.mdl`.
- Renderer uploads each bind-pose mesh once, in local space. Each tick builds one position matrix and one unscaled normal matrix per instance, then `DrawInstanced` places that shared mesh. Team color stays on the instance. Missing meshes still use a small diagnostic marker.
- VTX stripgroup parsing tries strides 25/33/29 and vertex strides 12/9, and skips bad groups instead of aborting the whole model.

## Probe

```
native/build-mingw/entity_model_probe.exe --self-test --tf-root "D:\Steam\steamapps\common\Team Fortress 2\tf" --demo "D:\Steam\steamapps\common\Team Fortress 2\tf\autorecord_2026-07-05_20-12-47.dem"
```

Observed (llvm-mingw, 2026-10-05):

```
self-test ok
uniqueRenderable=4 vpkRenderable=4 inspections=3 inspectionCacheHits=1
scoutVertices=12000 scoutChecksum=1098120898 duplicateStable=true
playerFallbacks=1
demo assetRefs=195 requests=0 demoRenderable=0
```

The sampled Demo stores 195 asset references but no `m_ModelName` paths, so playback uses player-class fallback models from VPK.

## Instancing check

`entity_model_probe --self-test` checks `buildEntityModelInstanceRows` against hand values: identity with world-cube scale, yaw 90 around a nonzero origin, pitch 90, and a rejected non-finite origin. The normal row stays unscaled when the position row is scaled. The D3D11 shader and `DrawInstanced` path were not executed.

## Not claimed

- MSVC Release `tf2_demo_native.exe` was not rebuilt on this machine (no Visual Studio). The GPU instancing path is therefore not runtime-tested.
- Animation, ViewModel, textures/skins, and Source-equivalent pixels are not in this module.
- Full-Demo random seek and HUD are unchanged.
