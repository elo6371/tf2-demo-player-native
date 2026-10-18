# T12 地形混合材质 — 合并后收尾报告

- 任务：T12 收尾（release blocker F2，T11 合并后的验收缺口）
- 分支：`task/t12-blend-close`
- worktree：`D:\TF2_Native_Worktrees\t12-blend-close`
- 基线：`p0-entity-protocol@4dbf396`（Merge T11 world geometry and T12 terrain blend）
- 本轮提交：`aeea503`（产品）、`1a49e2d`（测试与证据）、文档提交见 `git log` 末条
- 推送状态：**未推送**，`task/t12-blend-close` 仅存在于本地 worktree
- 日期：2026-10-18

---

## 0. 结论摘要

**470 像素已经解释清楚，且不是混合退化。** T11 修好绕序后，世界几何更多地挡在地形带前面；
地形单独渲染时混合开启/关闭相差 **3530** 像素，全帧只剩 **470**，丢失的 3060 个里有
**3036（99.2%）落在此帧世界几何覆盖的像素上**。门禁现在把这条归因写成断言，而不是叙述：
全帧变化必须是地形对变化的子集（470/470），且丢失像素中至少 90% 被世界几何覆盖（实测 99.2%）。

**pin 的修正有测量支撑。** 全帧下限 500 → **400**（实测 470），同时把真正承载
"混合正确"的地形单独特写从 500 提到 **3000**（实测 3530）。没有降低对混合本身的判据，
只是把"合成帧里还能看见多少"和"混合是否发生"分开测量。

**T11 的绕序交换确实带着权重走。** 发射时逐三角形比对 (位置, 权重) 配对：
**165628 次检查、0 次脱离**；新增变异 m5 删掉 `std::swap(bAlpha, cAlpha)` 后转为 **72 次脱离**，
证明这个计数器不是恒零摆设。

**atlas 不再截掉地形材质。** 混合材质排到 64 tile cap 之前：产品读数 `blendLastRank=3`、
`blendCapped=0`、blend tiles 从 2 升到 3（COALMINES 地形终于有了自己的 tile）。
仍被截断的是 **48 个可贴图的单层材质**（其中 41 个在相机 PVS 内，共 2243 个可见三角形），
它们各自退化为一张外来 tile——这是测量过的已知限制，不是地形问题（§4.3、§7）。

**拒绝路径被执行。** 8 个畸形夹具全部给出各自的具名原因，另有 2 个对照证明正常资源仍能解析
（各 16384 字节 RGBA）。此前 `refused=0` 只是"参考图上没有坏资源"，不是"拒绝代码能工作"。

**门禁：前向 `TERRAIN-BLEND=PASS`（exit 0），变异 6/6 `MUTATION-CAUGHT=PASS`（exit 0）。**

---

## 1. 任务步骤对照

| T12 步骤 | 本轮结果 | 证据 |
|---|---|---|
| 1 从整合树构建三个目标 | `build-target.sh tf2_demo_native world_geometry_probe terrain_blend_probe` → rc=0 | §3 |
| 2 串行跑前向与变异门禁 | 前向 exit 0；变异 exit 0（6/6 转红，源码逐字节还原） | `gate-forward.log`、`gate-mutation.log` |
| 3 确认 T11 绕序交换同时交换混合权重 | 发射期配对检查 165628 次 / 0 脱离；m5 使其变 72 次脱离 | `probe-cp_snakewater_final1.txt`、`mutated-m5-detached-alpha.txt` |
| 4 用整合后新帧复现并解释 470 | 470 / 3530 / 3060 / 3036 四组读数，归因为世界几何遮挡 | `region.txt`、§4.1 |
| 5 判定短差性质并决定 pin | 属"预期中的 T11 几何变化"；全帧 500→400，地形对 500→3000，均附测量 | `terrain-blend-check.sh` §5/6 |
| 6 测量参考图与目标图的 atlas 覆盖 | 参考图 148 材质 / 64 tile / 48 可贴图被截（41 可见）；`pl_odyssey` 108/57/0；`koth_bagel_rc13` 本机未安装 | `atlas-coverage-*.txt`、§4.3 |
| 7 执行具名拒绝用例 | 8 个具名原因全部执行 + 2 个对照 | `refusal-fixtures.txt` |
| 8 判定 `$bumpmap2` / `%detailtype` / `$ssbump` | 实测 1/1/3 个材质声明；本轮不实现，未支持项显式记录 | §4.5、§7 |

---

## 2. 改动文件

| 文件 | 改动 | 原因 |
|---|---|---|
| `native/include/bsp_map.h` | 新增 `alphaPairingChecked/Detached` | 绕序交换可能只搬位置不搬权重，必须有一个只看这件事的计数器 |
| `native/src/bsp_map.cpp` | 置换三角形发射后比对 (位置, 权重) 配对 | 在网格单元与发射顶点都能看到的地方计数，而不是在三角形成型之后 |
| `native/include/world_material.h` | `resolveBlendLayer`（上一轮）、新增 `BlendModulateOutcome` / `resolveBlendModulate` | modulate 的三条拒绝原因原本内联在 400 行循环里，无法被夹具执行 |
| `native/src/world_material.cpp` | `resampleRgba`、`resolveBlendLayer`（上一轮）、新增 `resolveBlendModulate` | 产品与夹具共用同一段判定，理由字符串只有一个来源 |
| `native/src/main.cpp` | ① 混合材质优先排序；② `blendLastRank` / `blendCapped` 读数；③ modulate 改调共享解析器 | 排序本身是修复，但"排序生效没有"必须能被读数与变异看见；modulate 拒绝路径要可执行 |
| `native/tools/terrain_blend_probe.cpp` | atlas census 对齐产品排序 + `--cluster`；`--refusal-fixtures`；三个字段声明读数 | capped 集合必须与产品丢掉的是同一批；拒绝要被执行；字段决策要有引用依据 |
| `terrain-blend-check.sh` | 新增第 3 段（拒绝夹具）、4b 段（覆盖）、alpha 配对断言；重订像素下限；新增 m5/m6 | 把本轮新增的每个结论都变成会转红的断言 |
| `evidence/terrain-blend/**` | 新旧帧、哈希、读数、覆盖、拒绝、变异与两份门禁日志 | 交付物 |

---

## 3. 命令、退出码、可执行文件身份

| 项 | 值 |
|---|---|
| 地图 | `D:/SteamLibrary/steamapps/common/Team Fortress 2/tf/maps/cp_snakewater_final1.bsp` |
| demo | `bb841c…-cp_snakewater_final1.dem`，`--start-paused --capture-tick 0` |
| 音频 | `--audio-device 6 --audio-disabled` |
| 构建 | `bash build-target.sh tf2_demo_native world_geometry_probe terrain_blend_probe` → **rc=0** |
| 前向门禁 | `bash terrain-blend-check.sh` → **exit 0**，`TERRAIN-BLEND=PASS` |
| 变异门禁 | `bash terrain-blend-check.sh --mutation` → **exit 0**，6 个 `MUTATION-CAUGHT=PASS` |
| `tf2_demo_native.exe` | 1,640,448 B，10-18 19:17，`956af0e41e6c1a7d648a99755460d5e72de69074da0c2cceef1604e1a38bf42f` |
| `terrain_blend_probe.exe` | 502,784 B，10-18 19:16，`3b44b6465e36f738f1f6ba2c6e9b73e080003504b38dcb9f6c646d7c6bb37ff4` |
| `world_geometry_probe.exe` | 449,536 B，10-18 18:33，`5d80a7ab7c86d56012c3caf2814baa69b8b7554a45b7fe7a80ec71b6a0a57220` |

两个被门禁使用的 exe 都晚于各自依赖的最后一次源码改动；变异段每次还在断言后
`cp` 回备份、重编、`cmp` 逐字节比对、删备份（六次还原全部通过，`ls .scratch/terrain-blend/*.backup` 为空）。

本轮提交（三个逻辑单元，基线 `4dbf396`）：

| 提交 | 内容 |
|---|---|
| `aeea503` | 产品代码：alpha 配对校验、`resolveBlendLayer`/`resolveBlendModulate` 抽取、混合材质优先排序与 `blendLastRank`/`blendCapped` 读数 |
| `1a49e2d` | 测试与证据：探针 `--cluster`/`--refusal-fixtures`/atlas census 重排、门禁 6 段与 m5/m6、`evidence/terrain-blend/**` |
| 本文档 | 交付报告 |

---

## 4. 读数

### 4.1 470 像素：位置、幅度、归因

| 读数 | 合并前（HEAD 帧） | 合并后（本轮新帧） |
|---|---|---|
| 地形区域（`terrain-off` ⊕ `none`） | 13,714 | **13,745** |
| `all-on` ⊕ `all-off` | 1,930 | **470** |
| 全部落在地形区域内 | 1,930（100%） | **470（100%）** |
| 差异包围盒 | x 0..938, y 328..383 | **x 397..873, y 320..383** |
| 最大通道差 / 均差 | 59 / 7.9 | **15 / 4.2** |
| `terrain-on` ⊕ `terrain-off`（只画地形） | 3,535 | **3,530** |
| `all-on` ⊕ `all-repeat` | 0 | **0** |

归因链，全部是同一批帧上的读数：

1. **地形区域没变**：新旧掩膜 IoU = 0.996（13,705 / 13,714 重合），所以不是"地形换了地方"。
2. **地形混合本身没变弱**：只画地形时仍有 3,530 个像素变化，量级与合并前一致（3,535）。
3. **消失的是被挡住的那部分**：地形对 3,530 个变化像素中，全帧只看到 470 个；
   丢失 3,060 个里，**3,036 个（99.2%）在该像素上满足"世界帧 ≠ 只画地形的帧"**，
   即世界几何画在了地形前面。合并前旧帧上 x<397 的 1252 个混合差异像素，现在全部仍在
   地形掩膜内，但没有一个仍参与全帧差异——正是被新画出来的墙面盖住。
4. **不是抓帧不稳定**：repeat 逐字节相同；六帧哈希在多次独立运行（前向两次、变异一次、
   手工复跑一次）完全一致；唯一一次异常是启动期瞬态 `rc=127`（进程打开 trace 后立即退出），
   单独复跑同命令 rc=0，属启动期抖动，不产生帧，也不参与任何读数。

因此短差属于**预期中的 T11 几何变化**，不是产品缺陷、不是抓帧不稳定、也不是失效 pin 的巧合；
旧 pin（≥500）是 T11 合并前的边界，已由新证据替换。

### 4.2 T11 绕序与混合权重

```
disp.alphaPairingChecked = 165628
disp.alphaPairingDetached = 0
```

检查点在 `bsp_map.cpp` 的发射处：把 `pushTriangle` 实际写出的三个 (位置, 权重) 与网格单元的
(位置, 权重) 做精确匹配。因为发射顶点是网格顶点的拷贝、中间没有重采样，精确比较是成立的。
变异 m5 只删 `std::swap(bAlpha, cAlpha)`，读数变为 **detached=72 / checked=165628**（checked 不变），
说明计数器只在配对断裂时移动。

### 4.3 atlas 覆盖

产品读数（参考图，`--world-blend on`）：

```
gpu: world material-fetch distinct=148 tiled=64 capped=51 vmtMissing=1 vmtNoBase=31
     vtfMissing=1 vtfTooLarge=59 decodeEmpty=0 ... blendFirst=3 blendLastRank=3 blendCapped=0
gpu: world blend enabled=1 ... tiles=3 ... weightedVertices=33888 ... distinctWeights=9
```

探针 census（与产品同序、同 cluster=639）：

| 读数 | 参考图 `cp_snakewater_final1` | 目标图 `pl_odyssey` |
|---|---|---|
| 材质总数 | 148 | 108 |
| 进入 atlas | 64 | 57（未触及 cap） |
| 被 cap 截断（全部） | 83 | 0 |
| 其中可贴图（有 tile 就会画对） | **48** | 0 |
| 其中不可贴图（给了 tile 也没用） | 35 | 51（未解析） |
| 可贴图且相机 PVS 可见 | **41**（2243 三角形） | 不可测（无 demo/无 PVS 数据） |
| 混合材质 | 3，全部 `blendLastRank=3`，`cappedBlend=0` | 0 |
| 被截断材质的三角形占比 | 3,365 / 199,788（1.7%） | — |

被截断的前几名（按三角形数）：`BRICK/BRICKWALL004D=144`、`METAL/WALL011E=144`、
`WOOD/WOOD_WALL007A=133`、`MEDIEVAL/STONEWALL001A=128`，以及 `maps/…/nature/blendgroundtograss008_wvt_patch=117`
（pakfile 里的混合材质补丁副本，自身是单层 LightmappedGeneric）。

**决策：本轮不扩 atlas、不做分批。** 依据是测量而不是偏好：三个地形混合材质已经全部进入
atlas（这是 F2 的必需项）；剩下 48 个可贴图材质全是单层表面，合计只占已绘制三角形的 1.7%，
把 64 tile 扩到 128 会让 atlas 从 67.6 MiB 翻到约 135 MiB，而收益无法在当前基线上做视觉验证
（世界帧仍近乎全黑，见 §7）。这 48 个材质的名字、三角形数与可见性已经落盘，下一轮可以据此
选择"按 PVS 分批"或"第二 atlas"，而不是重新猜。

`koth_bagel_rc13` 是本机第二份 demo 的地图，但 **BSP 未安装**（demo 能开，地图退化为空场景），
因此无法测覆盖；已如实记录，不用别的地图顶替。

### 4.4 拒绝夹具（8 个用例 + 2 个对照）

`terrain_blend_probe --refusal-fixtures`，同一段 `resolveBlendLayer` / `resolveBlendModulate` 被产品调用：

| 用例 | 具名原因 |
|---|---|
| layer-base-unresolved | the first layer did not resolve, so there is nothing to blend from |
| layer-second-unresolved | the second layer materials/fixture/missing.vtf did not resolve from loose, VPK or pakfile |
| layer-not-vtf | the second layer materials/fixture/broken.vtf is not a readable VTF |
| layer-decode-empty | the second layer materials/fixture/cube.vtf decoded to nothing |
| layer-resample-failed | the second layer materials/fixture/second.vtf could not be resampled to the tile grid |
| modulate-no-second-layer | the material has no usable second layer to modulate |
| modulate-unresolved | did not resolve from loose, VPK or pakfile |
| modulate-decode-empty | decoded to nothing or could not be resampled |

对照：`controlLayerResolved=1`、`controlModulateResolved=1`，各输出 16384 字节（64×64 RGBA），
证明这组夹具不是"什么都拒绝"。

### 4.5 `$bumpmap2` / `%detailtype` / `$ssbump`

从参考图置换面实际用到的三个 `WorldVertexTransition` 材质读出：

| 材质 | `$bumpmap2` | `%detailtype` | `$ssbump` |
|---|---|---|---|
| `NATURE/BLENDGROUNDTOGRASS008` | — | `tf_forest_grass` | 1 |
| `NATURE/BLENDGROUNDTOGRASS008_NODETAIL` | — | — | 1 |
| `COALMINES/BLENDGROUNDTOGRASS_COALMINES` | `Nature/dirtground003_height-ssbump` | — | 1 |

计数：`bumpmap2Declared=1`、`detailtypeDeclared=1`、`ssbumpDeclared=3`。

**决策：本轮不实现这三项，并显式记录为未支持。** 理由写在 §7 第 3 条：三项都不是混合链路
（base + base2 + modulate + 顶点权重）的一部分，而是着色层的附加语义；`%detailtype` 需要
`detail.vbsp` 的 detail type 表才能解析到贴图（本仓库没有该表，硬编码路径等于发明语义），
`$ssbump` 需要两通道法线重建。当前帧地形只占 1.6% 且 99% 被遮挡，实现后无法用画面证明对错，
违背"不要用计数器宣称视觉正确"。将来实现时应只针对上表这三个引用，不做通配支持。

---

## 5. 变异

`bash terrain-blend-check.sh --mutation`，六个缺陷各自必须转红，且每次断言后源码逐字节还原：

| 变异 | 缺陷 | 转红依据 |
|---|---|---|
| m1 | 置换顶点不读 `+16` 处 alpha | `weightedVertices=0`、`sentinelVertices=496128` |
| m2 | 不解析 `$basetexture2` | `blend.declared=0`，而 shader 仍是 WorldVertexTransition |
| m3 | 上传时把顶点权重写 1 | `distinctWeights=1`、`weightMin=1`，`weightedVertices=33900` 不变 |
| m4 | modulate 取到但 flag 不落顶点 | `modulateTiles=2` 保持、`modulatedVertices=0` |
| m5 | 绕序交换只换位置不换权重 | `alphaPairingDetached=72`（forward 为 0，checked 不变） |
| m6 | atlas 排序忽略第二层 | `blendCapped=1`、`blendLastRank=136 / 148`，`tiled=64` 不变 |

m6 的判据本身修过一次：第一版读 `blendFirst`（声明数量），发现它**不随排序变化**——
计数器读错了对象。现在产品同时输出 `blendLastRank` 与 `blendCapped`，前者是"最后一个混合材质
排在第几位"，后者是"cap 丢了几个混合材质"，m6 用这两项转红。

---

## 6. 证据、工作区、残留进程

- 证据：`evidence/terrain-blend/`
  - `probe-cp_snakewater_final1.txt`、`product-readings.txt`、`region.txt`
  - `atlas-coverage-cp_snakewater_final1.txt`、`atlas-coverage-pl_odyssey.txt`
  - `refusal-fixtures.txt`
  - `frames/*.bmp` + `frames.sha256`
  - `mutated-m1..m6-*.txt`
  - `gate-forward.log`、`gate-mutation.log`
- 帧哈希（本轮新帧，与合并前不同是预期的）：

```
72761b509d8978e05b6ce41a647da2da4d7f80ae024091adedbaa87b3ffcb42a *all-off.bmp
cc825e818def8a95a923b88556c7883b82073e1baccf6f722bdbc0fe7eac322b *all-on.bmp
cc825e818def8a95a923b88556c7883b82073e1baccf6f722bdbc0fe7eac322b *all-repeat.bmp
c04021f716da86dc61d30c8115703ce97ad037f762c3e8f7b291b9b056cfecfc *none.bmp
863d81863e6324cff1962b71a795fd35088ee26eecf53b8b3afc54b11b8d5085 *terrain-off.bmp
1183f97c077a066c917229a166c565a5cd9d066d831870870ef5dc47c983ba08 *terrain-on.bmp
```

- 视觉检查：把差异包围盒裁出来按 3 倍放大看过 on/off/放大差异图——地形是破碎几何中的一条窄带，
  混合差异集中在一条 476×64 的水平带内；变化幅度很小（最大 15/255），因为两层贴图在这条带里
  本身的色差就小，且带内大部分像素权重接近 0 或 1。这不构成"地形看起来像 TF2 地形"的证据（§7）。
- 工作区：提交后 `git status --short` 为空（`.scratch/` 被忽略）。
- 残留进程：本 worktree 无残留。主线 `D:\TF2_Native_Test` 的用户改动
  （`evidence/world-material/real-cp_snakewater_final1.txt` 与两份未跟踪文档）**未触碰**。

---

## 7. 仍未关闭的限制

1. **"地形看起来像 TF2 地形"仍不可宣称。** 本帧世界几何是破碎的：地形只占 1.6%，
   且其中 99% 被其他几何遮挡。本轮证明的是"混合链路正确、变化落在地形上、并且能被读数与变异
   抓住"，不是"地形好看"。近景取景在 F1 未修前仍不成立（上一轮已实测为单色帧）。
2. **48 个可贴图的单层材质仍被 cap 截断。** 其中 41 个在相机 PVS 内（2243 三角形）。
   本轮选择不扩 atlas，理由与数据见 §4.3；这些材质在画面里退化为一张外来 tile，
   属于已知、已量化的限制。
3. **`$bumpmap2` / `%detailtype` / `$ssbump` 未实现。** 声明来源见 §4.5；
   `%detailtype` 需要 `detail.vbsp` 的 detail type 表，`$ssbump` 需要两通道法线重建，
   第二层法线需要第二张 bump 采样。三项都会改变近景着色，不影响本轮的混合判据。
4. **重复实现的两套 census 仍有 3 个材质的口径差。** 探针的"可贴图被截"是 48，产品自己的
   `capped` 是 51；两者对 148 个材质的判定在 3 个上不一致（读 VMT/VTF 的边界情形）。
   门禁断言的是"没有混合材质被截"（0）与"存在可贴图被截"（>0），不把 48/51 写成等式；
   差值来源未追到单条材质，记录在此以免被当成一致。
5. **`koth_bagel_rc13` 未测。** 该地图未安装，demo 打开后世界为空场景；不做替代测量。
6. **启动期瞬态失败未根治。** 一次 `--world-blend off` 运行在打开 trace 后以 rc=127 退出，
   同命令复跑正常。门禁没有为该现象加重试，因为重试会掩盖真正的启动期崩溃；
   如果复现频率上升，应单独定位（疑似 D3D 设备创建期的外部竞争）。

---

## 8. 复核方式

```bash
cd /d/TF2_Native_Worktrees/t12-blend-close
bash build-target.sh tf2_demo_native world_geometry_probe terrain_blend_probe
bash terrain-blend-check.sh              # 期望 TERRAIN-BLEND=PASS, exit 0
bash terrain-blend-check.sh --mutation   # 期望 6 个 MUTATION-CAUGHT=PASS, exit 0
```

单项读数：

```bash
native/build-nmake/terrain_blend_probe.exe --refusal-fixtures
native/build-nmake/terrain_blend_probe.exe --map "<tf>/maps/cp_snakewater_final1.bsp" \
  --tf-root "<tf>" --cluster 639
```
