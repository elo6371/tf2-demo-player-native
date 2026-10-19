# T24 — 光照与着色器语义

2026-10-19 · owner: rendering specialist · branch `task/t24-lighting-semantics`

## 1. 结论

T24 要解决的是**六个控制项被当成同一类东西**：`dynamicLighting`、`specular`、`bump`
是 `RenderSettings` 上的字段，乘进同一个常量缓冲，作用于整个世界；`$bumpmap2`、
`%detailtype`、`$ssbump` 是 VMT 里的材质键，而 pass 只有一个 bump 采样槽、一套图集
uv、没有切线基。改动之前，前三者唯一的入口是 `--quality standard`——一次翻八个字段。

本轮给前三项各配了一个 on/off 开关，一次只动一个语义，并给六项各一行账本：

| 控制项 | 类别 | 声明/可见/解析 | 状态 |
|---|---|---|---|
| `dynamicLighting` | pass 开关 | —（无 VMT 声明它） | disabled → **applied** |
| `specular` | pass 开关 | `$phong` 0 声明 | disabled → **applied** |
| `bump`（`$bumpmap`） | pass 开关 | 44 / 43 / 44（缺 0） | 关时 disabled；开且无纹理 **refused** |
| `$bumpmap2` | 材质键 | 5 / 5 / 5 | **refused**（不静默替换为第 1 层） |
| `%detailtype` | 材质键 | 15 / 14 / 0（15 个 VMT 缺失） | **refused** |
| `$ssbump` | 材质键 | 36 / 35 / 36 | **refused** |

一帧一次只动一项，实测（`pl_odyssey`，tick 2000，1264×681）：

| 对照 | 变化像素 | 占比 | 最大差 | 方向 |
|---|---|---|---|---|
| A vs A2（确定性） | **0** | 0.00% | 0 | — |
| A→B 动态光 | 1,583,615 | 61.32% | 83 | 全变暗（brighter=0） |
| A→C 高光 | 265,448 | 10.28% | 12 | 全变亮（darker=0） |
| A→D 单独开 bump | **0** | 0.00% | 0 | — |
| B→E 动态光下开 bump | 749,629 | 29.03% | 13 | 692,986 变亮 / 56,643 变暗 |

**最终整合树更新**：T21 修复降采样纹理 UV 后，世界 albedo 发生预期变化（T21 报告记录
1,459,130 个变化像素），所以以上三个绝对差分 pin 不再适用于合并树。最终树重新测得：
A→B **1,584,117**（全暗，max Δ83），A→C **267,343**（全亮，max Δ12），B→E
**746,559**（max Δ13）；A→D 仍为 **0**。`lighting-semantics-check.sh` 已将 pin 移到这些
最终树实测值，方向和 bump 无消费者时的恒等结论保持成立。

**D 帧是本轮最实的一个读数**：trace 报 `bump=1`（纹理已绑定、常量已上传），画面与基
线逐字节相同。原因是着色器里 bump 只扰动 `n`，而 `n` 只有三条路能到达输出——
`dot(n, lightDirection)`（受 `lightDirectionAndMode.w` 门控）、specular 波瓣、env 采
样。三者全关时，换一张法线贴图改变不了一个像素。也就是说「bump 已生效」这句话在没
有消费者时不成立，而改动之前没有任何读数能区分这两种情况。

方向读数是第二条防线：`0.62+0.38*NdotL` 只能从烘焙光照里**减**（brighter=0），
specular 只能**加**（darker=0）。任一项出现一个反向字节，就否掉了报告里的那句话——
所以它不能仅凭「画面变了」被提成默认值。

## 2. 环境

| 项 | 值 |
|---|---|
| worktree | `D:/TF2_Native_Worktrees/t24-lighting` |
| branch | `task/t24-lighting-semantics`（从 `9591499` 拉出） |
| base commit | `9591499` Merge branch `task/t17-world-visual` into `p0-entity-protocol` |
| final commit | `593065d` 实现（含证据），`573a3ed` 交付报告 |
| push | `origin/task/t24-lighting-semantics`，`git push -u` rc=0（首次推送，新建远端分支） |
| 参考地图 | `maps/pl_odyssey.bsp`，108 个 distinct 材质，首个 `WOOD/WALL007A` |
| 参考 demo / tick | `autorecord_2026-07-02_13-26-46.dem`，tick 2000（脚本重试到恰好 2000） |
| 诊断 bump 纹理 | `materials/brick/brickwall001_normal.vtf` |
| 对照基线 | `D:/TF2_Native_Worktrees/t24-baseline`（`git worktree --detach 9591499`，干净树） |
| 主工作树 | `D:/TF2_Native_Test` — **未触碰** |

## 3. 改动文件与理由

新增：

| 文件 | 理由 |
|---|---|
| `native/include/world_lighting.h`、`native/src/world_lighting.cpp` | 六项控制的账本：把 `declared / visible / resolved / missing / parsed / requested / effective` 与一句具名 `reason` 做成纯函数 `scanWorldLightingSemantics()`。产品与 probe 链接同一份实现，所以「probe 的六行」和「产品的六行」不可能是两套定义。 |
| `native/tools/lighting_semantics_probe.cpp` | 独立可执行：`--self-test`（30 条断言，含一个 fixture——安装目录里根本没有 `%detailtype` 的 VMT，所以解析证据只能来自 fixture）、`--map`（真实 BSP 上的六行账本）、`--resource <path>`（单资源是否存在）、`--list-bump`（把缺失的 bump 路径归因到 chain / pakfile / 都不在）。 |
| `lighting-semantics-check.sh` | 本轮门禁：6 个正向小节 + 4 个 mutation。 |
| `bmp-delta-sign.py` | 把差值拆成 brighter / darker 两向计数。语义断言是**有方向**的，只统计「变了」会同时放过变亮和变暗两种相反的错误。 |
| `evidence/lighting-semantics/` | 每次运行的证据：probe 输出、参考 trace、差值、方向、帧 sha256。 |

修改：

| 文件 | 理由 |
|---|---|
| `native/src/main.cpp` | 新增 `--world-dynamic-light on\|off`、`--world-specular on\|off`、`--world-bump on\|off`、`--world-bump-texture <path>`；把 `RenderSettings` 三字段与 pass 状态装配成 `WorldLightingPassState` 交给账本；trace 增加 `gpu: world lighting ...` 一行与六行 `gpu: world lighting-control control=...`。另外修掉一个悬垂引用（见 §4）。 |
| `native/include/native_renderer.h`、`native/src/native_renderer.cpp` | 新增三个 `*Effective()` 判定，常量缓冲与帧采样读**同一个**实现，所以「设置要什么」和「着色器拿到什么」不会在 trace 里分叉；移除 `normalize()` 里与 `fromPreset()` 重复的性能预设清零块、以及 draw 处 `preset == Standard` 的合取条件（理由与实测见 §5）。 |
| `native/CMakeLists.txt` | 两个目标都链接 `world_lighting.cpp`。 |

## 4. 顺带修掉的一个真实缺陷

`main.cpp` 里注册给资源链的 pakfile reader 捕获了块内局部 vector 的**引用**：

```cpp
if (!mapBytes.empty()) {
  assetChain.setPakfileReader([&mapBytes](...) { return bspPakfileEntry(mapBytes, ...); });
}   // mapBytes 在这里析构，lambda 还活着
```

该块结束后 reader 仍被后续查找调用。这 44 个 `$bumpmap` 路径里有 **9 个只存在于地图
自己的 pakfile**（`bump.pakfileOnly=9`、`bump.absent=0`），它们因此读成「缺失」——一个
悬垂引用表现为一条假的材质缺失记录。现在字节改由 `shared_ptr` 持有、按值捕获，产品侧
`bump` 一行读 `44/43/44/0`（缺 0）。门禁把 `chainMissing=9 / pakfileOnly=9 / absent=0`
钉住，免得它改天以「9 个缺失」的样子回来。

## 5. 参考帧与基线的对照：0 px

在 `t24-baseline`（`--detach 9591499`，干净树）构建对照二进制，同地图、同 tick、同预设
抓帧，两次都是 **0 px / max_delta=0**：

| 预设 | 对照 | 结果 |
|---|---|---|
| performance（不画天空盒） | 9591499 vs T24 | **differing=0** |
| standard（画天空盒） | 9591499 vs T24 | **differing=0** |

即 T24 没有改变默认画面——新增的是读数与开关，不是着色。

这里有个差点写错的结论，记下来：第一次对照拿 `t21-material-visual` 的二进制当基线，量
出 56.5% 的差异；那个 HEAD 其实是 `1d65bbd`（9591499 + T21 的 atlas uv 归一化），不是
本轮的 base。换成真正的 9591499 后差异降到 20.1%，而 20.1% 仍然**不是** T24 造成的——
用同一个基线二进制、只改 ini 里的 `preset`，就是同样的 20.1265%（`standard` 画天空盒、
`performance` 不画，T17 已记录的既有行为）。证据见
`evidence/lighting-semantics/baseline-compare.txt`。

**由此发现的一个环境问题**：产品退出时会把本次运行的设置写回
`%LOCALAPPDATA%/TF2 Demo Player/settings.ini`，所以一次带 `--quality performance` 的抓
帧会把机器的 preset 从 1 改成 0，影响之后每一次运行。本轮 P1/P2 抓帧就触发了它（已把
用户的 ini 恢复为 `preset=1`）。门禁现在给每次抓帧传 `--settings-file <scratch>`（该参
数在 `main.cpp` 里是预扫描的，读写双向覆盖），不再读也不再写机器配置。

## 6. 移除预设门控：为什么，以及它实际上改变了什么

改动包含一处容易说过头的地方，所以先把边界写清楚。

参考帧跑在 `preset=standard` 下——`settings.ini` 存了 `preset=1`，而三个着色标志从不
持久化、保持 false（就是 `settings_store.cpp` 注释里记的那处 T17 矛盾）。因此
`normalize()` 的性能预设清零块和 draw 处的 `preset == Standard` 合取条件**都没有在
A..F 上生效**，移除它们对这六帧的影响为零。

真正受影响的是显式传 `--quality performance` 的调用者：旧代码里 `--world-specular on`
设好字段、`normalize()` 立刻清掉，开关与不开产生同一帧——测量本身是死的。所以移除的
理由是「显式开关应当胜过预设名」，不是「参考帧被挡住了」。

影响面只有三个字段：除 `fromPreset()`／`loadSettings()` 外，全仓库只有 `main.cpp` 的三
个单语义开关给它们赋值（已核对 `src/*.cpp`）；`fromPreset(Performance)` 走
`RenderSettings{}` 零初始化。实测两组对照，都已钉进门禁：

| 运行 | 读数 |
|---|---|
| `--quality performance` | `dynamic=0 specular=0 bump=0`（与改动前一致） |
| `--quality performance --world-specular on` | `dynamic=0 specular=1 bump=0`，`requestedSpecular=1` |

## 7. 门禁

`bash lighting-semantics-check.sh [--mutation]`

正向 96 条断言全过，0 失败（含 P1/P2 两组预设对照、A2 确定性对照）。四组变异各自必须
变红，源码必须字节级还原：

| mutation | 注入的缺陷 | 变红的表现 |
|---|---|---|
| m1 | `$bumpmap2` 报 applied 而不是 refused（T19 点名的「静默替换」） | `$bumpmap2 state = applied`；自检 `bump2.refused` 不再通过 |
| m2 | `%detailtype` 的读取路径不再读这个键 | 自检 `detail.typeRead` 不再通过（只有 fixture 能抓到） |
| m3 | `--world-dynamic-light` 同时翻 specular（一开关两语义） | 只开动态光的那次运行 `specular=1` |
| m4 | bump 在没有绑定纹理时报 effective | F 帧 `bump=1` 而 `bumpTexture=-`（应为 `0 / -`） |

四组全部 RED，四份源码全部 `RESTORE=OK`。最后一次运行 exit=0。

## 8. 边界：本轮没有声称什么

* **没有声称六项里任何一项视觉上正确。** 只钉住 declared/parsed/requested/effective
  与实测帧差。
* **per-material 的 specular 与 bump 仍是按名拒绝。** pass 只有一个槽；`$bumpmap2`、
  `%detailtype`、`$ssbump` 各有具名拒绝理由，不是「画不出来所以没画」。
* **`%detailtype` 只有解析证据，没有渲染证据。** 安装目录里 `materials/overlays/detail006.vmt`
  不存在（15/15 个被引用的 detail VMT 都缺失），所以它的读取路径只能由 probe 的
  fixture 证明存在。
* **D 帧的 0 像素差是测量，不是通过。** 它是「bump 在没有消费者时不产生像素」这句话的
  证据，不是「bump 正确」的证据。
