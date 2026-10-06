# Native Material Audit: 2koth_abbey

This is an isolated-resource audit. It records what the Native D3D11 path
proves and does not claim Source-equivalent lighting or water rendering.

## Inputs

- BSP: `D:\SteamLibrary\steamapps\common\Team Fortress 2\tf\maps\2koth_abbey.bsp`
- VMT: `D:\SteamLibrary\steamapps\common\Team Fortress 2\tf\materials\vgui\logos\spray.vmt`
- VTF: `D:\SteamLibrary\steamapps\common\Team Fortress 2\tf\materials\vgui\logos\spray.vtf`

## Evidence

- BSP parse: version `20`, `68271` triangles, `22991` lightmap faces,
  `5209257` lightmap bytes.
- Explicit VMT: `UnlitGeneric`; bump, envmap, and selfillum are all absent.
- Explicit VTF: `256x256`, format `13`, `262144` decoded RGBA bytes.
- WARP texture draw: feature level `45056`, `16` non-black pixels.
- Missing-root/WARP smoke: `tf_root_missing_prompt=1`, `warp_fallback=1`,
  feature level `b100`.
- Limited TF2-root scan: `archiveCount=4`, `realCubemapVtfFlags=14`,
  `realCubemapDepth6=0`, `realCubemapComplete=false`.

## Boundary

The renderer remains `lightmapMode=average-intensity` and
`cubemapMode=approximate-2d`. The observed cubemap files expose metadata but
no verified six-face (`depth=6`) pixel resource, so they are not treated as a
real cubemap. The selected VMT provides no real bump/selfillum/envmap sample.
