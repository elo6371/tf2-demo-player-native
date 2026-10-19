# T19 — Material Coverage Expansion

2026-10-19 · owner: material specialist · branch `task/t19-material-coverage`

## 1. 结论

T19 要解决的是**静默丢弃**：在 `cp_snakewater_final1` 上，148 个 distinct 材质里有
51 个被写死的 64-tile 上限挡在 atlas 之外，既没有渲染，也没有任何一行拒绝理由。

这一轮把固定上限换成分批规划，并给每个材质一行处置记录：

| 读数 | 基线 `e67630b` | 本轮 |
|---|---|---|
| `material-fetch tiled` | 64 | **112** |
| `material-fetch capped` | 51 | **0** |
| `atlas-plan batches` | 1（固定上限） | **2**（64 + 48 tiles） |
| `atlas-plan refused` | — （无此读数） | **0**（malformed/duplicate/budget 全 0） |
| 被上限丢弃且在当前 PVS 可见的材质 | 48 个中 44 个可见 | 0 个被上限丢弃 |
| 参考帧与基线逐像素差异 | — | 19,566 px（2.3%），其中 12,268 px 差值 >8 |

四个未实现的 shader 特性（`$bumpmap`、`$bumpmap2`、`$ssbump`、`$detail`+`%detailtype`）
现在各有**具名拒绝理由**，并附上声明数、PVS 可见数、资源解析数：

```
bumpmap   50 declared, 47 visible, 50 resolved, 28 distinct paths  -> refused: one slot, 28 textures
bumpmap2   1 declared,  1 visible,  1 resolved                      -> refused: one bump sampler
ssbump    41 declared, 39 visible, 41 resolved                      -> refused: needs SSBump decode + tangent basis
detail    51 declared, 49 visible,  0 resolved (VMTs absent)        -> refused: needs a second uv + $detailblendmode
```

world 与 weapon 的 t0 状态由 draw 处的账本测量：`weaponDrawsWithWorldAtlas=0`、
`worldDrawsWithWeaponTexture=0`（含一帧真的画了武器的 POV 帧）。

## 2. 环境

| 项 | 值 |
|---|---|
| worktree | `D:/TF2_Native_Worktrees/t19-material-coverage` |
| branch | `task/t19-material-coverage`（从 `e67630b` 拉出） |
| base commit | `e67630b` Merge branch `task/t15-vpk-build` into `p0-entity-protocol` |
| 对照 worktree | `D:/TF2_Native_Worktrees/t19-baseline`（`--detach e67630b`，只用于取基线帧） |
| 主工作树 | `D:/TF2_Native_Test` — 用户既有改动（`M evidence/world-material/real-cp_snakewater_final1.txt`、3 个未跟踪 md）**未触碰** |

## 3. 改动文件与理由

新增：

| 文件 | 理由 |
|---|---|
| `native/include/world_tile_atlas.h`、`native/src/world_tile_atlas.cpp` | 图集规划与 uv 的纯函数契约：`planWorldTiles()` 做校验/去重/分批/预算/落位，`worldTileCoord()` 做 uv 归一化，`worldTileBlit()` 做带 1-texel 复制边框的写入。产品与 probe 共享同一份实现，所以「规划」可以被单独断言。 |
| `native/include/world_material_features.h`、`native/src/world_material_features.cpp` | `$bumpmap` / `$bumpmap2` / `$ssbump` / `$detail`+`%detailtype` 的共享分类：按材质累计 declared/visible/resolved/missing，并生成具名拒绝理由。理由由数字推导，读者可以对照同一行的计数复核。 |
| `native/tools/material_coverage_probe.cpp` | 独立可执行：`--self-test`（26 条断言：规划、预算、uv、blit、每条拒绝分支）、`--fixture`（16 个实色材质，验证每个 tile 采样到自己的块）、`--map`（真实 BSP 上读出 148/112/2 与特性计数）。 |
| `material-coverage-check.sh` | 本轮门禁：7 个正向小节 + 6 个 mutation。 |
| `evidence/material-coverage/` | 门禁每次运行的证据（probe 输出、tile 表、读数、对照差）。 |

修改：

| 文件 | 理由 |
|---|---|
| `native/src/native_renderer.cpp` | ① `uploadWorldGeometry` 删除 `kMaxTiles=64`、`unordered_map<string,Tile>` 与静默 `continue`，改调 `planWorldTiles()`，按批分配 albedo/blend/modulate 三组纹理；② 顶点缓冲保持 `map.triangles` 顺序（PVS draw list 用 `triangle*3+j` 索引），分批放在**索引缓冲**：按批拼成连续区间，每批一次 `DrawIndexed`；③ `setWorldVisDrawList` 按 `worldTriangleBatch_` 重分组，使 clip 后仍按批绘制；④ t0 账本（`bindSlot0`/`isWorldAtlasView`/`isWeaponMaterialView`）在 draw 处统计跨 pass 泄漏；⑤ 每批独立 blend/modulate atlas，避免跨批采样；⑥ `--world-geometry terrain` 不建索引缓冲（过滤后的顶点缓冲与地图三角形编号不一致，建了会越界——实测 terrain-only 帧丢掉了三分之一的区域，blend 门禁的 on/off 差从 1930 掉到 470）。 |
| `native/src/main.cpp` | ① 抓取上限 64 → 256，超限具名计数 `capped`；② 记录 VMT parse 成功的全部材质（含没有 `$basetexture` 但仍声明 `$detail/$ssbump` 的），供特性扫描；③ 新增 `gpu: world atlas-plan` / `gpu: world tile` / `gpu: world isolation` / `gpu: world features` 四类 trace；④ **修正 `materialOrder` 比较器**：pair 是 `(triangles, name)`，原代码比较 `second`（材质名），把「覆盖序」实现成了反字母序。实测该 bug 让本图最大的材质 `MEDIEVAL/PLASTERWALL002A`（80,256 三角形）排在第 71 位，正好被 64 上限挡住。 |
| `native/include/native_renderer.h` | T19 读数与批状态：`WorldTileReading` 表、atlas 批计数/字节、t0 账本计数、`worldDrawBatches_`、`worldTriangleBatch_`。 |
| `native/include/vmt_material.h`、`native/src/vmt_material.cpp` | 解析 `$bumpmap2`、`$ssbump`、`$detail`（`%detailtype` 从 detail 材质里读）。 |
| `native/CMakeLists.txt` | 共享源加入 `tf2_demo_native`；新增 `material_coverage_probe` 目标。 |

## 4. Step 1 — 被省略的材质里，哪些可见

按旧抓取序（反字母序，即基线行为）统计，被 64 上限挡住的 48 个材质中：

- **44 个在当前 PVS 里可见**，合计 **106,983** 个可见三角形；
- 其中最大的是 `MEDIEVAL/PLASTERWALL002A`：80,256 三角形，全部可见。

按修正后的覆盖序（本轮实现），第 2 批 48 个是**最小**的材质：43 个可见，合计 2,048 个
可见三角形，5 个在本帧完全不可见（`COALMINES/RED_BEAM1`、`CONCRETE/CONCRETEWALL015A`、
`METAL/WALL016`、`CONCRETE/CONCRETEWALL002C`、`METAL/IBEAM002`）。

两个数字一起说才完整：旧代码丢的是"随便 48 个"，其中包含整张图最大的面；修正顺序后，
预算若真的不够，先丢的是最不重要的面。参考帧的差异也印证了这一点——修正顺序前后，
整帧只有 66 个像素不同（最大通道差 1，图集原点不同带来的浮点舍入）。

## 5. Step 2 — 有界分批替换固定上限

- 每批最多 64 tiles（`maxTilesPerBatch`），批网格 `columns=ceil(sqrt(n))`、`rows=ceil(n/columns)`；
  64 tiles → 4112×4112，48 tiles → 3598×3598（不是 4112×4112 的浪费）。
- 预算 384 MiB/层；按批计费，第一批发不下就整体具名拒绝（`atlas-budget`），不半批入图。
- 本图：2 批，albedo 119,416,592 B（113 MiB），blend 64 MiB（仅 batch 0 有 blend tile），
  modulate 64 MiB；总计 254,684,944 B。
- 未拿到 tile 的三角形仍然绘制：alpha=0 走 shader 既有的 flat 分支，是"没有贴图的
  表面"，不是洞。

## 6. Step 3/4 — 只在追踪引用并定义 shader 结果之后才实现

四个特性都**没有**实现，理由是资源与 shader 形状都不支持，且都给了具名拒绝：

| 特性 | 声明/可见 | 资源 | 拒绝理由 |
|---|---|---|---|
| `$bumpmap` | 50 / 47 | 50 resolved，28 个不同路径 | world pass 只有一个 bump 采样槽，28 个材质各自指名不同贴图，无法"每材质自己的法线" |
| `$bumpmap2` | 1 / 1 | 1 resolved | layer 2 的法线图；只有一个采样槽，拒绝而不是被 layer 1 顶替 |
| `$ssbump` | 41 / 39 | 41 resolved | 需要 SSBump 解码与切线基，world 顶点格式不携带切线 |
| `$detail` + `%detailtype` | 51 / 49 | **0 resolved**（`materials/overlays/detail006.vmt` 等在本机 VPK 中不存在） | 需要第二套 detailscale uv 与 `$detailblendmode` 结果；顶点只有一个 atlas uv，且 detail 材质本身解析不到，`%detailtype` 读不出 |

`%detailtype` 的读取路径已实现（从 `$detail` 指向的 VMT 里读 `%detailtype`），但本机
51 个 detail 材质全部解析失败，所以 `detailTypesRead=0` 是资源事实，不是读数缺失。

## 7. Step 5 — 跨 draw range 的状态隔离

t0 账本在每次 `bindSlot0` 记录 (view, pass)，在 draw 处判定：

- world 分批绘制时，若 t0 仍是武器材质 → `worldDrawsWithWeaponTexture++`；
- 武器 range 绘制时，若 t0 是 world atlas → `weaponDrawsWithWorldAtlas++`。

参考帧：`weaponDrawsWithWorldAtlas=0 worldDrawsWithWeaponTexture=0 slot0Binds=2 batchDraws=2 texturedBatchDraws=2`。
武器帧（POV demo，tick 2000，`--viewmodel v_scattergun_scout`）：
`afterWorld=1 drawCalls=59 weaponDrawsWithWorldAtlas=0 worldDrawsWithWeaponTexture=0 slot0Binds=4`。

## 8. 门禁

### 正向（`bash material-coverage-check.sh`）

```
=== 1/7 probe 自检 ===            26/26 断言通过（规划/预算/uv/blit/每条拒绝分支）
=== 2/7 fixture ===               16 材质各自采样到自己的块；malformed 被拒 1
=== 3/7 真实地图 ===              148 distinct / 112 有像素 / 0 拒绝 / 2 批 / beyondOldCap=48
=== 4/7 产品参考帧 ===            tiled=112 capped=0 batches=2 refused=0 atlas bound
                                  atlases=2 largest=67,634,176 total=119,416,592
                                  tile 表 112 行（64+48），batch-1 可见 2,048 三角形
                                  隔离计数全 0；同构建两次运行帧逐字节一致
=== 5/7 --world-blend off ===     albedo 分批不动（2 批/112 tiles），blend 批 = 0
=== 6/7 含武器的 POV 帧 ===       afterWorld=1 drawCalls=59，隔离计数全 0
=== 7/7 单批对照（预算 210 MiB）  tiled=64 refused=48（全部 reason=atlas-budget）
                                  与全量帧差 30,533 px，最大通道差 146
MATERIAL-COVERAGE=PASS
```

### mutation（`bash material-coverage-check.sh --mutation`）

```
--- m1 预算只够一批（把旧的 64 上限用新零件重建）
  MUTATION-CAUGHT=PASS (tiled=64 refused=48 while the forward run read 112/0)
--- m2 worldTileCoord 丢掉半 texel
  MUTATION-CAUGHT=PASS (self-test rc=1, uv.origin=0.001946 against 0.002918)
--- m3 武器 range 把 world atlas 绑到 t0（枪被墙面贴图涂上）
  MUTATION-CAUGHT=PASS (weaponDrawsWithWorldAtlas=2 while the forward run read 0)
--- m4 $bumpmap2 不再被统计（未实现的层被静默忽略）
  MUTATION-CAUGHT=PASS (declared=0 after the mutation, 1 before)
--- m5 索引按三角形号而不是顶点号构建（几何撕裂）
  MUTATION-CAUGHT=PASS (tiled=112 batches=2 unchanged, the frame moved)
--- m6 clip 后的索引全部塞进 batch 0（batch 1 采样 batch 0 的 atlas）
  MUTATION-CAUGHT=PASS (tiled=112 batches=2 unchanged, the frame moved)
MUTATION-CAUGHT=PASS (… every source came back byte-identical)
```

**源码恢复证明**：每次 mutation 前 `git hash-object` 记下文件哈希，还原后重新计算并要求
逐字节相同，随后重新构建；六个 mutation 全部通过该检查（否则脚本以
`MATERIAL-COVERAGE-MUTATION=FAIL` 退出）。运行结束后再次 `grep` 六个补丁锚点，均为原始
文本。

m5 的第一版打在**上传期**索引缓冲上，而本帧 clip 生效、根本不用那条缓冲，所以"帧没变"
被误读成"没抓住"。改为用 `--no-world-vis-clip` 采一帧（`clipApplied=0`,
`drawnTriangles=200000`）再比较，缺陷立刻可见；m6 则专门覆盖 clip 后的重分组路径。
这条弯路本身是证据：没有无 clip 帧时，上传期索引的正确性**没有**任何一帧能证明。

### 既有门禁回归

| 门禁 | 结果 | 说明 |
|---|---|---|
| `world-material-check.sh` | **rc=0 PASS** | 世界材质/光照/天空/vis 的既有断言全部保持 |
| `visual-baseline-check.sh` | **rc=0 PASS** | 四个屏幕区域的亮度/对比/背景占比区间全部保持（首次运行红是 `world_geometry_probe.exe` 未构建，补建后通过） |
| `terrain-blend-check.sh` | **rc=1**（**基线同款红**） | 唯一失败项 `pixels changed by the blend = 470, expected >= 500`。在 `e67630b` 的独立 worktree 上跑同一门禁，读数同样是 **470**，所以这条红在 T19 之前就存在，不是本轮引入。其余断言（terrain 区域、on/off 在区域内、repeat 逐字节一致、terrain pair 差异）全部通过 |

terrain 门禁的其他读数在本轮有移动且方向正确：`diff_terrain_pair_in_region`
`3530`（基线）→ `4867`（本轮），因为第二批发现在有 blend tile，更多位移面参与了混合。

**没有修改 `verify-all.sh`**，也没有修改任何既有门禁的阈值；`terrain-blend-check.sh` 的红
按原样保留并在上面注明其在基线上同样成立。

## 9. 命令与退出码

```
# 构建（NMake，native/build-nmake）
bash build-target.sh tf2_demo_native material_coverage_probe                    # rc=0
bash build-target.sh world_geometry_probe                                       # rc=0（既有门禁的依赖）
bash build-target.sh terrain_blend_probe                                        # rc=0

# probe
./native/build-nmake/material_coverage_probe.exe --self-test                    # rc=0, 26 checks
./native/build-nmake/material_coverage_probe.exe --fixture                      # rc=0, 2 checks
./native/build-nmake/material_coverage_probe.exe --map <cp_snakewater_final1.bsp> \
    --tf-root <tf> --expect-material MEDIEVAL/PLASTERWALL002A                   # rc=0

# 本轮门禁
bash material-coverage-check.sh                                                 # rc=0 MATERIAL-COVERAGE=PASS
bash material-coverage-check.sh --mutation                                      # rc=0 MUTATION-CAUGHT=PASS

# 回归
bash world-material-check.sh                                                    # rc=0 WORLD-MATERIAL=PASS
bash visual-baseline-check.sh                                                   # rc=0 PASS
bash terrain-blend-check.sh                                                     # rc=1（基线同款 470<500）
```

采集一帧的完整命令（门禁内部同样使用）：

```
TF2_NATIVE_TRACE=<trace> ./native/build-nmake/tf2_demo_native.exe \
  --tf-root "D:/SteamLibrary/steamapps/common/Team Fortress 2/tf" \
  --audio-device 6 --audio-disabled --hidden-window \
  --demo <demo> --start-paused --capture-tick 0 \
  --capture-frame <bmp> --metrics-file <csv>
```

无 clip 帧与武器帧分别在上述命令上加 `--no-world-vis-clip`、`--viewmodel
models/weapons/v_models/v_scattergun_scout.mdl --viewmodel-fov 90`（武器帧用 POV demo、
`--capture-tick 2000`、**不加** `--start-paused`：POV demo 在 `--start-paused` 下 tick 永远
停在 0，会一直跑到超时）。

## 10. 哈希

可执行文件（sha256）：

```
820f6d89da16cdc245b5403f468b4e5b708a86abf9ee9225a9a0ea9acb737cfe  native/build-nmake/tf2_demo_native.exe
59bc28d311ea7aa338c67811abc8642b67bd05ee71cf3fe6155221d6b1b2f5eb  native/build-nmake/material_coverage_probe.exe
```

这两个哈希只标识报告生成时的产物：同一份源码重编会得到不同的 PE 字节（MSVC 在镜像里写
时间戳/调试目录），实测同一源码两次构建的 exe 哈希不同、**帧哈希相同**。所以"同一构建两次
运行逐字节一致"的断言针对帧（`c0a996fd…`），不是针对 exe。

输入（sha256）：

```
f09d406d2bbe397c8f9df9ca17f11b36b53d40fcf57e3f1532ec9af811b2a1eb  cp_snakewater_final1.bsp
308cc1453672b26dfdcc106bcb62eac4a7eea97899adc8830f51711be834607f  matcha-20260927-1347-cp_snakewater_final1.dem
c8a58501d222bdd9aec6db92d2782931a29a13d963a87364d603263e6dab091e  autorecord_2026-07-02_13-26-46.dem（POV，武器帧）
```

帧（sha256，1264×681 BMP）：

```
65934eded901e43e6c98f28132760d0b4cb3777c2b5c65fe372ea4a75243da7c  基线 e67630b 参考帧
c0a996fd649dce34aedaf180ca98125307e040e310672beab6bcbcd243f9d95f  本轮参考帧（两次运行逐字节一致）
c7e0547237b42c253d6a98957b89ef5bcde0bd54df1fe3a94fe19d5e06adb7f8  本轮无 clip 帧（clipApplied=0）
df3daee0585f1a2b6f45ade8c6e0cc98c43f51f84044c2577615eb50fcb88747  m5（索引按三角形号）
237984ea14de97c414454449443e1db9d3701e3b5c5834e6c083411b3e5bcf9b  m6（clip 全塞 batch 0）
```

源码指纹（`git hash-object`，mutation 前后逐字节相同）：

```
c9287cc6285163573632e19775023793e5c46ff8  native/src/native_renderer.cpp
fd50dd06dd06a5e7c5e4277c97ea4fdc11d0d11e  native/src/world_tile_atlas.cpp
```

## 11. 手工视觉发现

- 基线帧与全量帧：构图、相机、光照完全一致；差异集中在画面下半部的墙面（y 222–448，
  列 300–900 与 1000–1200），不是天空、也不是武器区域。
- 修正抓取顺序前后：整帧 66 px 差、最大通道差 1，肉眼不可分辨。
- 全量帧与单批对照帧：墙面纹理有无的差别肉眼可见（30,533 px，最大差 146）。
- 画面中央下方的橙黄色光斑在两帧中都存在（既有行为，与 T19 无关）。

## 12. 未验证项

- **只测了一张地图 + 一帧 POV**。`cp_snakewater_final1` 之外的地图没有重测，
  `kMaxWorldMaterials=256` 这个硬上限没有在"distinct 材质超过 256"的地图上验证过。
- **`$bumpmap` 的 shader 结果未在真实数据上验证**：本帧 `gpu: world lightmap ... bump=0`，
  world pass 没有全局 bump 贴图，因此"只绑一个 bump 采样槽"的拒绝是对 shader 形状的
  陈述，不是对某张图上采样结果的测量。
- **`%detailtype` 的读取路径未被真实资源走到**：本机 51 个 `$detail` 指向的 VMT 全部不存在，
  所以 `detailTypesRead=0`、`resolved=0` 是资源事实；解析 `%detailtype` 的代码没有一条
  真实输入穿过。
- **多批 + 武器同帧未采集**：武器帧用的 POV demo 所在地图只有 1 个 world 批；参考帧有 2 批
  但没有武器。隔离计数是在 draw 处读的，逻辑上覆盖，但"2 批 + 武器"这一组合没有一帧。
- **384 MiB 预算从未真正拒绝**：预算拒绝只在 210 MiB 对照里发生；默认预算在本图上有
  241 MiB 余量，没有测到边界。
- **`--world-geometry terrain` 不带分批**：该模式按设计整缓冲绘制，分批路径在这种模式下
  不执行。
- **`vmtMissing` 由 1 变 4**：不是回归，而是抓取不再停在 64 后走到了更多材质（见 §3 的
  抓取上限说明）；四个缺失 VMT 没有逐个列出名字。
- **像素级正确性只到"图集原点不同的舍入"**：没有对单个材质做"TF2 原图 vs 本引擎"的
  逐像素比对；fixture 只验证每个 tile 采样到自己的块，真实 VTF 的解码正确性依赖既有
  T10/T12 路径。

## 13. 最终状态

- 工作树 `D:/TF2_Native_Worktrees/t19-material-coverage`：6 个修改文件 + 9 个新增文件，
  无其它脏文件（跑既有门禁时被覆盖的 `evidence/terrain-blend/`、`evidence/visual-baseline/`、
  `evidence/world-material/` 已用 `git checkout --` 还原，不属于本轮改动）。
- 主工作树 `D:/TF2_Native_Test`：用户的 `M evidence/world-material/real-cp_snakewater_final1.txt`
  与 3 个未跟踪 md 原样保留，未被提交、未被触碰。
- 对照工作树 `D:/TF2_Native_Worktrees/t19-baseline` 仍存在（detached `e67630b`），只用于
  取基线帧与基线地形门禁读数；交付前未删除。
- 进程：`tasklist` 中无 `tf2_demo_native` / `material_coverage_probe` / `terrain_blend_probe` /
  `world_geometry_probe` 残留。
- 本机串行约束：所有产品采集、构建、mutation 均为串行执行（单个后台任务链），没有并发跑过
  两个产品实例。
