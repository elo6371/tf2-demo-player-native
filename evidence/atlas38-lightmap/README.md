# T105 世界光照图（lightmap）atlas 三条尾巴的证据（2026-11-21）

本目录是 `native/src/bsp_map.cpp` 三处「放弃就回退」路径的证据：

1. **HDR-only 地图**（只有 lump 53、没有 lump 8）不再退回 average-intensity；
2. **每个 atlas patch 外圈一像素 clamp 环**（`TF2_NATIVE_LM_BORDER=1` 才开，默认关）；
3. **没进 atlas 的 face 逐条列出来**（`TF2_NATIVE_LM_DROPS=1`），带 face 序号、材质名、patch 尺寸和一句能指路的原因。

所有文本都是从本机实跑的 probe / 抓帧里抽出来的。重跑命令见文末；这不属于 gate，只解析 BSP，不需要 host lock。

| 文件 | 内容 |
|------|------|
| `selftest.txt` | `world_material_probe --self-test` 全文：`lightmapTailSelfTest` 十一个子项逐项 true |
| `readings-off.txt` | 六张真实地图（snakewater / upward / process / badwater / badlands / viaduct）在**默认路径**下的读数：atlas 尺寸、facesPacked/facesDropped、atlasReason |
| `atlas-border-ab.txt` | 同一批地图 border=0 / border=1 的 A/B：**只有 atlas 高度变，facesPacked 和 atlasReason 不变** |
| `hdr-only.txt` | 把 cp_snakewater_final1 的 lump 8 头表清零、lump 53 原样保留后的读数：`lightmapMode` 仍是 `source-equivalent`，4096x1331，14988/14988 |
| `drops.txt` | 截短 lump 8 的同一张地图 + `TF2_NATIVE_LM_DROPS=1` 的逐条清单；以及八张正常地图在该开关下**一句都不打** |
| `off-path-identity.txt` | 默认路径未变的证据：HEAD 构建 vs 本次构建，pl_odyssey 的 geometry probe 逐行相同、抓帧 BMP 的 md5 相同（含 `visual-baseline-check.sh` 在本机 HEAD 上本来就已经偏移的说明） |

## 读法

- **HDR-only**：`hdr-only.txt` 里 `lightmapMode` 是 `source-equivalent` 且 `facesPacked==facesWithLightmap==14988`、atlas 4096x1331，与原图同值 —— 说明照亮这张图的是 lump 53 而不是某条平均。改之前同一份 BSP 读到的是 `lightmapMode:"unavailable"`、atlas `0x0`、`atlasReason:"the bsp declares no lightmap faces"`。
- **border**：`atlas-border-ab.txt` 里每张地图 border=1 只让高度变大（snakewater 1331→1670、upward 1567→1842、process 1179→1438、badwater 1620→2012），facesPacked / facesDropped / atlasReason 三列两行完全相同。**默认关**，所以 gate 上钉住的 atlas 尺寸（例如 `world-material-check.sh` 的 4096x1331）一个字节都不动。
- **默认路径没动**：`off-path-identity.txt` 记的是同一份工作树两次构建（HEAD / 本次）的对比 —— geometry probe 的七行 lightmap 读数一字不差，抓帧 BMP 的 md5 也相同（`a5edf32d…`）。
- **drops**：`drops.txt` 第一段是截短 lump 8 后的真实 refusal（`the light offset does not land inside the lighting page`，带材质名和 patch 尺寸，末尾有 `... N more not listed` 的截断说明）；第二段是八张健康地图，每张都是 `0 refused face(s), 0 listed` —— 开关打开也不吵。

## 重跑

```bash
bash .scratch/collect-evidence.sh        # selftest / readings-off / hdr-only / drops
bash .scratch/collect-border-ab.sh       # border A/B
```

`hdr-only.txt` / `drops.txt` 依赖两份改造过的 BSP 副本，用 `.scratch/make_hdr_only.py` 和 `.scratch/make_short_light.py` 从 `cp_snakewater_final1.bsp` 生成（脚本不入库；两份副本在 `.scratch/` 下，也不入库）。
