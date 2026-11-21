# T105 世界光照图（lightmap）atlas 三条尾巴的证据（2026-11-21）

本目录是 `native/src/bsp_map.cpp` 三处「放弃就回退」路径的证据：

1. **HDR-only 地图**（只有 lump 53、没有 lump 8）不再退回 average-intensity；
2. **每个 atlas patch 外圈一像素 clamp 环**（`TF2_NATIVE_LM_BORDER=1` 才开，默认关）；
3. **没进 atlas 的 face 逐条列出来**（`TF2_NATIVE_LM_DROPS=1`），带 face 序号、材质名、patch 尺寸和一句能指路的原因。

所有文本都是从本机实跑的 probe / 抓帧里抽出来的。重跑命令见文末；这不属于 gate，只解析 BSP，不需要 host lock。

| 文件 | 内容 |
|------|------|
| `selftest.txt` | `world_material_probe --self-test` 全文：`lightmapTailSelfTest` 十一个子项逐项 true。其中两行 `drops-*` 打的是 `shelfDropped=`（只数 shelf 溢出），与同一行清单里的 `N refused face(s)`（数所有被拒的 face）是**两个不同人群**，见下面「两个计数器」一节 |
| `readings-off.txt` | 六张真实地图（snakewater / upward / process / badwater / badlands / viaduct）在**默认路径**下的读数：atlas 尺寸、facesPacked/facesDropped、atlasReason |
| `atlas-border-ab.txt` | 同一批地图 border=0 / border=1 的 A/B：**只有 atlas 高度变，facesPacked 和 atlasReason 不变** |
| `hdr-only.txt` | 把 cp_snakewater_final1 的 lump 8 头表清零、lump 53 原样保留后的读数：`lightmapMode` 仍是 `source-equivalent`，4096x1331，14988/14988 |
| `drops.txt` | 截短 lump 8 的同一张地图 + `TF2_NATIVE_LM_DROPS=1` 的逐条清单；以及八张正常地图在该开关下**一句都不打** |
| `hdr-only-before-after.txt` | **同一份 HDR-only BSP 字节，两棵构建的实测对照**：base commit `390a7c76` 的 probe 读到 `unavailable` / atlas `0x0`，本次构建读到 `source-equivalent` / 4096x1331 / 14988；末尾附未改动原图在两棵构建上的读数（作为「副本应当追平」的参照） |
| `hdr-only-map-build.txt` / `drops-map-build.txt` | 两份派生 BSP 生成时的打印（源图 lump 8 / lump 53 的 offset 与 length、改成了什么），让副本可复现 |
| `off-path-identity.txt` | 默认路径未变的证据：HEAD 构建 vs 本次构建，pl_odyssey 的 geometry probe 逐行相同、抓帧 BMP 的 md5 相同（含 `visual-baseline-check.sh` 在本机 HEAD 上本来就已经偏移的说明） |

## 读法

### 两个「没进 atlas」的计数器不是一回事

这一节是 `selftest.txt` 里两行 drops 读数的注解。它们看起来矛盾（`shelfDropped=0` 却在同一行报 `1 refused face(s)`），实际上是两个不同的人群，代码里也是两个不同来源：

| 名字 | 定义 | 谁在里面 |
|------|------|----------|
| `lightmapAtlasDroppedFaces`（probe 打 `shelfDropped=`） | `lightmapFaceCount - lightmapPackedFaces`，即**进了 atlas 排队的 face 里没排下的** | 只有 **shelf 溢出**：patch 比 atlas 宽/高，或下一行会越过 atlas 底边 |
| 清单里的 `N refused face(s)` 一行（= `lightDropTotal`） | 这次解析里**所有**被拒的 face，不论在哪一步被拒 | shelf 溢出 **+** 在计数之前就被 `hasLightmap` 拒掉的（lighting page 根本就不存在 / samples box 空或超 512x512 / `lightofs` 落在 page 外面） |

为什么后者不进前者：`lightmapFaceCount` 只在 `hasLightmap` 为真的分支里 `++`（`native/src/bsp_map.cpp:1128-1131`），而一个 face 自己的 `lightofs` / samples box 不成立时，它在那之前就被拒了，所以 `lightmapFaceCount - lightmapPackedFaces` 这条减法**永远看不到它**。`lightDropTotal` 在拒绝点无条件 `++`，两者的差就是这一类。`lightmapAtlasReason` 有第四个分支专门说这件事（`N face(s) carried no usable lightmap and were refused before packing`）。

- **HDR-only**：`hdr-only.txt` 里 `lightmapMode` 是 `source-equivalent` 且 `facesPacked==facesWithLightmap==14988`、atlas 4096x1331，与原图同值 —— 说明照亮这张图的是 lump 53 而不是某条平均。
  **改之前的样子是实测的，不是回忆**：`hdr-only-before-after.txt` 把同一份 `hdr-only-cp_snakewater_final1.bsp` 字节喂给两棵构建 —— base commit `390a7c76` 的 probe 读到 `lightmapMode:"unavailable"`、`lightmapSamples:0`、`lightmapFaces:0`、`atlas:"0x0"`、`lightmap.state:unavailable`、`trianglesWithLightUv:0`、`uvAllInRange:false`、`atlasReason:"the bsp declares no lightmap faces"`；本次分支的 probe 在同一份字节上读到 `source-equivalent` / 2793687 samples / 14988 faces / 4096x1331 / `active` / 199922 / `true`。base 构建在文件末尾对**未改动的原图**读到的值与本次构建完全一致（`source-equivalent` / 4096x1331 / 14988），所以两边的差异只来自这份副本，不来自两棵构建的其它差别。
- **border**：`atlas-border-ab.txt` 里每张地图 border=1 只让高度变大（snakewater 1331→1670、upward 1567→1842、process 1179→1438、badwater 1620→2012），facesPacked / facesDropped / atlasReason 三列两行完全相同。**默认关**，所以 gate 上钉住的 atlas 尺寸（例如 `world-material-check.sh` 的 4096x1331）一个字节都不动。
- **默认路径没动**：`off-path-identity.txt` 记的是同一份工作树两次构建（HEAD / 本次）的对比 —— geometry probe 的七行 lightmap 读数一字不差，抓帧 BMP 的 md5 也相同（`a5edf32d…`）。
- **drops**：`drops.txt` 第一段是截短 lump 8 后的真实 refusal（`the light offset does not land inside the lighting page`，带材质名和 patch 尺寸，末尾有 `... N more not listed` 的截断说明）；第二段是八张健康地图，每张都是 `0 refused face(s), 0 listed` —— 开关打开也不吵。

## 重跑

生成脚本和采集脚本都在 `tools/` 下（**入库**），所以从干净 checkout 加一棵 build tree 就能复现本目录所有文本：

```bash
bash evidence/atlas38-lightmap/tools/collect.sh
# 想连 HDR-only 的 before/after 一起重跑，再传一棵从 base commit 构建的 probe：
bash evidence/atlas38-lightmap/tools/collect.sh --with-hdr-base /path/to/base/world_material_probe.exe
```

| 脚本 | 作用 |
|------|------|
| `tools/make_hdr_only.py` | 从 `cp_snakewater_final1.bsp` 派生 HDR-only 副本（lump 8 头表清零、lump 53 原样），写到 `.scratch/hdr-only-cp_snakewater_final1.bsp` |
| `tools/make_short_light.py` | 派生 lump 8 长度 ÷3 的副本，让一部分 face 的 `lightofs` 落到 page 外面，写到 `.scratch/short-light-cp_snakewater_final1.bsp` |
| `tools/collect.sh` | 跑上面两个脚本，再采 `selftest.txt` / `readings-off.txt` / `atlas-border-ab.txt` / `hdr-only.txt` / `drops.txt`（加 `--with-hdr-base` 时连 `hdr-only-before-after.txt`） |

两处需要知道的：派生副本写在 `.scratch/` 下（**不入库**，`cp_snakewater_final1.bsp` 59 MB），生成它们的脚本入库；这一整套只解析 BSP，**不需要 host lock**，只有构建和抓帧需要。
