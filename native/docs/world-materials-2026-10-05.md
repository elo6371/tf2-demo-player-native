# World materials (module 2) 2026-10-05

Local work only. Do not treat this file as a GitHub browse target.

`source-equivalent` in this note means the luxel-index contract: a face's lightmap UV is computed from the texinfo lightmap vectors and the face's stored lightmap mins/size, then packed into an atlas. It does not mean the pixels match Source.

## What changed

- Lighting lump 8 is read as ColorRGBExp32 (4 bytes: r, g, b, signed exponent). Each sample is scaled with `ldexp(1, exp) / 255` and clamped to 8-bit. That is tone-mapped LDR (`ldr-clamp`), not the HDR lighting lump.
- Style 0 is the block that is packed. A style byte of 255 means the face has no lightmap. Bump-light faces still contribute only that first block. The extra bump-direction samples are not read.
- Lightmap vectors are used as luxels. They are not multiplied by 0.125.
- Packed faces go into a 2048-wide shelf atlas. The stored image is then cropped to the used rows, and the V coordinate is rescaled to that height. Unpacked vertices keep light UV `-1` and use the scalar average.
- Displacement faces with power 2..4 emit a grid when the start corner matches and the vertices stay finite. A face that fails those checks keeps the flat triangle fan.
- Cubemap lump 42 is stored as 16-byte samples. The GPU still samples an approximate 2D environment. The 2D VTF decoder still rejects cubemaps. Six-face byte order stays the synthetic check in `material_chain_probe`.
- `skyname` is read from the entity lump. `skyMaterialPath` returns `materials/skybox/<name>{rt,lf,bk,ft,up,dn}.vmt`. The skybox is not drawn. Sky brushes are still skipped.
- Faces with `SURF_WARP` stay in the mesh and are tagged water. Reflection and refraction stay unavailable.
- Visibility stores cluster PVS offsets. `clusterVisible` can tell clusters apart. The orbit camera mesh is not culled, because that camera is not a Source leaf.

## Probe

```
native/build-mingw/world_material_probe.exe --self-test --bsp "D:\Steam\steamapps\common\Team Fortress 2\tf\maps\itemtest.bsp" --bsp "D:\Steam\steamapps\common\Team Fortress 2\tf\maps\cp_cloak.bsp"
native/build-mingw/material_chain_probe.exe
native/build-mingw/material_chain_probe.exe --bsp "D:\Steam\steamapps\common\Team Fortress 2\tf\maps\itemtest.bsp"
```

Observed with llvm-mingw on 2026-10-05. Both probes exited 0.

itemtest: `source-equivalent`, `ldr-clamp`, 513/513 luxel fits, atlas `2048x98`, 20/20 displacement corner matches, 1024 displacement triangles, sky `sky_day01_01`, 4 cubemap samples, 70 clusters with 10 visible from cluster 0, water absent.

cp_cloak: 898/898 luxel fits, atlas `2048x164`, sky `sky_well_01`, 3 cubemap samples, 0 displacements, 381 clusters with 90 visible from cluster 0, 158 sky faces skipped, water absent.

`material_chain_probe` with no map still reports `lightmapMode=average-intensity` and `noLightingDefault=true`. With itemtest it reports `source-equivalent` and `lightmapToneMap=ldr-clamp`. The synthetic cubemap still parses depth 6 in face order and the 2D decoder still rejects it.

## Not claimed

- The D3D11 shader and `uploadWorldLightmap` were not executed. This machine has no Visual Studio. A mingw syntax check of `native_renderer.cpp` still stops on the pre-existing DirectXMath names (`XMVECTOR`, `XMMatrix*`) and does not compile HLSL.
- HDR lump 53, bump-direction samples, real cube-map sampling, drawn skybox, water reflection, and orbit-camera PVS culling are not in this module.
- Displacement winding matched the corner test and the triangle budget. It was not compared to a Source screenshot.
