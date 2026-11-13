# Native material capability probe (2026-10-05)

This is a bounded local-resource probe result. It records capability and
limitations; it does not claim Source-equivalent material rendering.

## Real TF2 resource evidence

Root: `D:\\SteamLibrary\\steamapps\\common\\Team Fortress 2\\tf`.

```text
archiveOpen=true archiveCount=4
realCubemapVtfFlags=14
realCubemapHighResResources=14
realCubemapResourceTags=28
realCubemapDepth6=0 realCubemapMaxDepth=1
realCubemapComplete=false
realBumpDecoded=false realEnvDecoded=false
cubemapMode=approximate-2d lightmapMode=average-intensity
cubemapParseFailures=0 cubemapOversized=0
```

The bounded scan found VTFs carrying cubemap metadata, but no valid depth-6
six-face resource. The renderer must therefore keep the current approximate
fallback until a real six-face asset and face-order verification are available.

The sampled `materials/vgui/logos/spray.vmt` is `UnlitGeneric` with a base
texture and alpha/vertex-color parameters. It does not exercise `$bumpmap`,
`$selfillum`, `$envmap`, or a lightmap shader. Its `spray.vtf` decoded as
`256x256`, format `13`, with `262144` RGBA bytes; the texture quad probe drew
`16` non-black pixels. This proves the VTF upload/draw path for that asset only.

## Acceptance boundary

Missing, corrupt, and oversized VTF/BSP inputs are rejected without aborting.
The result is sufficient for safe fallback and diagnostics, but not for
Phong, water reflection/refraction, six-face cubemap sampling, or per-face
luxel color reconstruction.
