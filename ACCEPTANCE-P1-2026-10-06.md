# P1 交付验收（第 1 项）：实体模型引用接线 + 逐 tick 位移

> 本轮覆盖清单里 P1「基础实体回放画面」的**两条**：
> **模型引用**（`m_nModelIndex` → `modelprecache` → 可加载路径，§1–§9）与
> **逐 tick 位移**（§10）。观察目标 / 武器 / 投射物 / team-skin / 截图对比
> **不在本轮**，见 §8 与 §10.6。
> 完整 P0 验收见 `ACCEPTANCE-P0-2026-10-06.md`。

---

## 0. 结论

### 0.1 模型引用（§1–§9）

清单写着「当前 `entity_model_probe` 仍为 `vpkRenderable=0`」，并把这归因为
「采样的 Demo 只有 assetRefs、没有 `m_ModelName` 路径，所以回放只能靠玩家职业兜底模型」。

**这个归因是错的。** 真实 Source demo **从来不在实体属性里发模型路径**：实体只带
`m_nModelIndex`（一个整数），路径存在 `modelprecache` 字符串表里，靠索引查。
本机 `readCreateStringTable` 有 `soundprecache` 和 `instancebaseline` 两个分支，
**没有 `modelprecache`**。于是：

- 每个 `AssetReference` 都是「有索引、没路径」；
- `ModelLoader::buildRenderRequests` 的第一条语句就是
  `if (!reference.hasModelPath || reference.modelPath.empty()) continue;`
  —— 全部被丢弃；
- `requests=0`、`demoRenderable=0`，画面里没有任何实体模型。

修复后（同一份 demo，同一台机）：

| 读数 | 修复前 | 修复后 |
|---|---|---|
| `model_precache_entries` | 0（表根本没解） | **1248** |
| `asset_model_path_known` | **0** / 675 | **387** |
| `asset_model_index_unresolved` | **387** | **0** |
| `entity_model_probe requests` | **0** | **387** |
| `demoRenderable` | **0** | **244** |
| `vpkExtracts`（真从 VPK 取到模型） | 0 | **43** |

SourceTV 侧（`73.dem`，101.5 MB）同向：`model_precache_entries=1126`、
`asset_model_path_from_precache=394`、`requests=394`、`demoRenderable=147`、`vpkExtracts=49`。

9 份探针语料 **9/9 `asset_model_index_unresolved=0`**（冻结在
`evidence/probe-baseline/added-lines.txt`），即没有一份 demo 的实体指向了表里没有的模型。

接线本身还暴露出**第三个缺陷（性能，非本轮引入）**：`ModelLoader::resolveAsset`
为了查一个路径，会把目录下每个 `_dir.vpk` 的**整个目录树**读进内存建哈希表，
并且对 5 个伴生后缀各重来一遍。P0 时所有引用都没有路径、这段代码一次没跑过；
P1 喂进 505 条真实路径后，光重复解析就花掉 90 秒，主程序在 bagel 上
**200 秒内起不来**。已改为每个 `AssetRoot` 只解析一次归档集合，
`entity_model_probe` 117 s → **23 s / 31 s**（两次实测）且 JSON 逐字段不变，
主程序首帧回到 **18.33 s**（P0 基线 17.82 s）。详见 §2.3。

我自己的性能修复又漏了一个链接依赖（`AssetRoot::archives()` 需要 `vpk_archive.cpp`），
被门禁第 1 步以 `exe_count=17`（期望 21）抓出，连带第 8 步变异套件变红。已修，见 §2.5。

### 0.2 逐 tick 位移（§10）

接线之后，实体位置第一次真的被送进渲染器，于是暴露出**两个渲染缺陷**和一个
**新的阻塞项**（后者 2026-10-07 已修）：

1. **玩家的 Z 恒为 0。** 玩家的 `m_vecOrigin` 是 VectorXY（wire 上只有 x/y），
   真实 Z 在**另一条独立的 Float 属性** `m_vecOrigin[2]` 里，而 `extractTransform`
   只读前者的 `z`。修复后渲染器在 bagel server tick 129211 拿到
   `z=407.836273`，与独立实现（demostf）的 `407.83627` 一致；修复前是 `0`。**已修**。
2. **Local / NonLocal 两份 origin 由哈希顺序决定。** 两者在 bagel 同一 tick 上
   相距约 **4000 单位**（`-3157.42,110.70` vs `765.625,-529.5`），
   而 `findProperty` 遍历 `unordered_map` 取第一个匹配。**已修**（改为确定规则）。
3. **只有 demo 尾部约 70 个包是 tick 精确的。** 归档快照在
   `57829 → 112221` 之间有 **54392 tick（约 13.7 分钟）的空洞**，
   而 `main.cpp:1260` 把 `Checkpoint` 与 `Available` 一样当作可绘制。
   屏幕上因此会是每十几分钟跳一次的瞬移。**2026-10-07 已修**（`aa93926`：按最大
   tick 间隙抽稀 + 去掉重复检查点；worst gap 54392 → 1180、鸽笼下界 761）。
   详见 §10.4 及末尾的「修复（`aa93926`）」。窗口外的答案仍是 Checkpoint，只是上了界。

这三条都不是计数型判据能看见的，所以新增了一条**逐值**门禁
`oracle-trajectory-check.py`（`compared=40 mismatches=0`，变异 `MUTATION-CAUGHT=PASS`），
它抓到了我第一次 tick 对齐的错误（`delta` 是基线而不是本包 tick）。详见 §10.3。

### 0.3 武器世界模型（§11）

第三类缺陷：**路径可以错，而且错得能解析成功**。武器实体的 `m_nModelIndex` 是它的
第一人称手臂合成模型（`c_*_arms`，一个真实存在的资产），世界渲染按它取路径时，
每一个计数门禁都绿着，而武器会被画成一双手。修复（`cd36db1`）后：

- POV：`armsWeapon=0 worldModelRequests=69 known=71 resolved=69`；
- bagel：`armsWeapon=0 worldModelRequests=32 known=33 resolved=32`；
- oracle 同实体同包见证：实体 822 的 Enter 包 `m_iWorldModelIndex = Integer(363)`
  ↔ `c_rocketlauncher.mdl`，本侧同值同路径（`source=world`）；
- 新增门禁为验收链第 12 步（`weapon-world-model-check.sh`），变异用例 `m10`。

「两个索引指两样东西」这条语义不来自记忆而来自实测：POV tick 55418 的 8/8 件武器
逐件 `m_nModelIndex == m_iViewModelIndex != m_iWorldModelIndex`，再用 modelprecache
把两组索引解成路径互相印证。详见 §11。

---

## 1. 提交、基线与改动文件

- 提交：`8c6f06e`（模型引用接线）+ `4868e7b`（VPK 目录复用）+ `9316416`（补链），
  测试树 `D:\TF2_Native_Test`，分支 `p0-entity-protocol`
- 父提交：`053de1e`（P0 第三遍复核证据）
- 基线：`d585af8`（`native-mvp-source` 未动）
- 补丁哈希：`git diff 053de1e..8c6f06e -- native/ | sha256sum` =
  `4766d440c19717ecd291cedaeecccd5a9050e5344efe4f4c4aefeae03d5e9724`
- 第二个提交（VPK 目录复用）：`git diff f38bcf6..4868e7b -- native/ | sha256sum` =
  `364ede91222b18739a1a1460fe3d1c8af1eb43aa1a8cf3a1ccbe6f2782c18935`，5 文件 +104/-23（见 §2.3）
- 第三个提交（补链，由门禁抓出）：`git diff 4868e7b..9316416 -- native/ | sha256sum` =
  `b1889fa05036a7b96f20b3fb3ba8b3cd7745c631dec7efd04e8154b2d0b6861d`，`native/CMakeLists.txt` +5/-1（见 §2.5）

**武器世界模型（§11）新增一个提交（2026-10-07 第二轮）**：

- `cd36db1`（世界模型优先路线 + 门禁 + m10 + 验收链第 12 步）：
  `git diff cd36db1~1..cd36db1 -- native/ | sha256sum` =
  `0d7620ee2d1a9649e68590d7285d55fa902c0fc1b49380bcef95d75bab6dc0b6`
  （含脚本与冻结证据的整体为 `0cf0de7313392d872d4784ae8a82847e839fc7836899921bd7a7c192271c7743`）

| 文件 | 改动 | 内容 |
|---|---|---|
| `native/src/demo_header.cpp` | +83 | 世界模型优先路线：`precachePathFor` 统一查表；带 `m_iWorldModelIndex` 的引用先按它填路径，只在世界索引指不到条目时才回落到 `m_nModelIndex`；逐桶计数 |
| `native/include/demo_header.h` | +62 | `AssetReference` 5 个新字段（`hasWorldModelIndex` / `worldModelIndex` / `hasViewModelIndex` / `viewModelIndex` / `modelPathFromWorldModelIndex`）+ `DemoNetworkSummary` 11 个新计数器 |
| `native/include/model_loader.h` + `native/src/model_loader.cpp` | +7 | `ModelRenderRequest::modelPathFromWorldModelIndex` 透传 |
| `native/include/entity_model.h` + `native/src/entity_model.cpp` | +5 | `ModelInstance::worldModelIndexPath` 透传 |
| `native/tools/entity_model_probe.cpp` | +267 | weapon-wiring fixture（10 个合成实体逐分支构造）+ `--dump-weapon-models` + 15 个 JSON 键 |
| `native/tools/entity_protocol_probe.cpp` | +19/-1 | `asset_refs=` 行尾追加 11 个新键（旧计数器零移动） |
| `weapon-world-model-check.sh` | 新增 320 行 | 三节门禁：fixture / bagel+POV 端到端 / oracle 见证 + 可证伪的 SKIP |
| `mutate.sh` | +55/-2 | m10：世界路线改成不可达（缺陷原样），8 条断言；默认用例列表 9→10 |
| `verify-all.sh` | +52/-17 | 新第 12 步；步骤标签 11→12；变异计数期望 29→37 |
| `check-probe-output-additive.sh` | +143 | claim 3（变更前冻结的 9 行 `asset_refs=`，剥键后逐字节一致）与 claim 3b（移动量恒等式 `delta == world_model_only`） |

**逐 tick 位移（§10）新增三个提交**：

- `6cb0ecf`（z 合并 + 确定性属性选择）：`git diff a2c584f..6cb0ecf -- native/ | sha256sum` =
  `ce2a53e78490de2a4ba6cfeb7ff0520936b413310047f2dc9f711d71f0d93bd3`
  （`entity_model.cpp` +95/-12、`entity_model_probe.cpp` +530/-12；
  含 `oracle-trajectory-check.py`，整体 `b980a00a…`）
- `cc17ebe`（普查判据改成按增量判定 + 文档）：`7a878b01…`
- `dab6ce1`（把逐值门禁接成 `verify-all.sh` 第 10 步）：`0db83d55…`

**2026-10-07 续接会话（§10.4 修复）新增一个代码提交**：

- `aa93926`（历史保留修复 + 历史覆盖门禁 + m9 变异用例 + 验收链第 11 步）：
  `git diff 9842a9f..aa93926 -- native/ | sha256sum` =
  `628273269f87aaf12382b14b0a472ff9c31db3fcddff984f617990ed56d1a8fa`
  （含 4 个判据脚本的整体为 `71391519eb7766ddef89ac2850a122ac1c16bde9d8722387301c7ec75543550f`）
- 前序：`9842a9f`（文档：把 HANDOFF 的提交列表写实、指明哪条检查说了算）

| 文件 | 改动 | 内容 |
|---|---|---|
| `native/src/demo_header.cpp` | +100/-22 | `thinHistoryArchive` 改按**最大 tick 间隙**抽稀（优先队列 + 二分中点，首尾钉住）；flush 分支去掉重复推入并单列 `entityHistoryFlushes`；`queryEntitySnapshotAtOrBeforeTick` 增加 `resolvedTick` 出参 |
| `native/include/demo_header.h` | +14/-2 | 上述计数器与出参声明 |
| `native/src/main.cpp` | +10/-2 | 查询接收 `resolvedTick`；窗口标题新增 `stale=` |
| `native/tools/entity_model_probe.cpp` | +172 | `--history-coverage N`：均匀抽样统计 exact/checkpoint/unavailable、间隙最坏/中位/鸽笼下界、陈旧度、字节估计 |
| `native/tools/presentation_probe.cpp` | +63/-2 | `checkHistoryCoverage` fixture（合成均匀供给）+ `historyCoverage`/`historyWorstGap`/`historySlotFloor` 输出 |
| `history-coverage-check.sh` | 新增 144 行 | 历史覆盖门禁：fixture + bagel 端到端，`HISTORY-COVERAGE=PASS` |
| `mutate.sh` | +95/-2 | m9：把按索引抽稀原样改回，断言 fixture 与门禁一起变红（新增 `must_exceed` 助手） |
| `verify-all.sh` | +58/-12 | 新第 11 步；步骤标签 10→11；变异计数期望 25→29 |
| `oracle-trajectory-check.py` | +19/-14 | 更新「已知限制」：窗口外的界由 `history-coverage-check.sh` 断言，不再只写「不可比」 |

| 文件 | 改动 | 内容 |
|---|---|---|
| `native/src/entity_model.cpp` | +95/-12 | `readVectorProperty`（VectorXY 的 z 由 `m_vecOrigin[2]` 补齐）；`preferCandidate` / `exclusiveRank`，`findProperty` 与 `readVectorProperty` 都改成确定选择 |
| `native/tools/entity_model_probe.cpp` | +530/-12 | `--trajectory` / `--trajectory-dump` / `--trajectory-at` / `--rendered` / `--props-at` / `--entity` / `--dump-class-props` / `--history-stats`；全部走 stderr |
| `oracle-trajectory-check.py` | 新增 | 与独立实现逐值对照，tick 对齐按值推导，带 `--mutation` 与 `compared==0` 失败 |
| `census-negative-test.sh` | +30/-6 | mutation I 改判**增量**（`skipped` 必须 +1）而非硬编码总数；并断言 victim 本身是 `index_state=ok` |
| `verify-all.sh` | +38/-10 | 新第 10 步：逐值门禁 + 其变异用例；步骤标签 9 → 10 |

| 文件 | 改动 | 内容 |
|---|---|---|
| `native/include/demo_header.h` | +41 | `AssetReference::modelPathFromPrecache`；`DemoNetworkSummary` 的 `modelPrecache` 表与 6 个表级计数、4 个资产解析计数 |
| `native/src/demo_header.cpp` | +385/-149 | 抽出共享 `walkPrecacheTablePayload`；新增 `decodeStringTablePayload`；`readCreateStringTable` 加 `modelprecache` 分支；`buildAssetReferenceList` 按索引解析路径；`scanKnownDemoMessages` 加协议号守卫 |
| `native/tools/entity_protocol_probe.cpp` | +32 | 打印 precache 表读数与资产解析分解；打印前先 `buildAssetReferenceList` |
| `native/tools/entity_model_probe.cpp` | +10/-1 | 扫描前把 `header.networkProtocol` 拷进 `summary` |
| `native/include/vpk_archive.h` | +34 | 新增 `VpkArchiveSet`（一次打开、多处复用） |
| `native/src/vpk_archive.cpp` | +36 | `VpkArchiveSet::openDirectory` / `collectContaining` / `find` |
| `native/include/asset_root.h` | +9 | `mutable shared_ptr<const VpkArchiveSet> vpkArchives` + `archives()` |
| `native/src/asset_root.cpp` | +9 | `archives()` 惰性建集合并缓存 |
| `native/src/model_loader.cpp` | +39/-23 | `resolveAsset` / `hasCompanion` / `buildRenderRequests` 改查已解析的归档集合 |
| `native/CMakeLists.txt` | +5/-1 | `native_install_smoke_probe` 补 `src/vpk_archive.cpp`（`AssetRoot::archives()` 引入的耦合，见 §2.5） |

---

## 2. 根因（四个缺陷）

### 2.1 主缺陷：`modelprecache` 从未被保留

`readCreateStringTable` 只对 `soundprecache` 与 `instancebaseline` 建表，
`modelprecache` 落到「不认识的表名」分支被整表丢弃。`m_nModelIndex` 因此无处可查。

实测证据（把 `modelprecache` 分支去掉、协议号正确，见 §6 变异 m7）：

```
asset_refs=675 asset_model_path_known=0 asset_model_path_from_precache=0
asset_model_index_known=515 asset_model_index_zero=114
asset_model_index_out_of_range=14 asset_model_index_unresolved=387
asset_model_index_unresolved_max=1246 asset_identity_unknown=139
```

**675 条引用里路径已知的是 0 条。** 这就直接否掉了旧文档「靠 `m_ModelName` 属性拿路径」
的假设：属性路径在这批 demo 上一次都没出现。表里有 1248 条，
`unresolved_max=1246` 正好落在表内，说明这些索引本来就该由表来解。

修复时把「查不到」拆成三个互不掩盖的计数器，而不是一个：

| 计数 | 含义 | 健康值 |
|---|---|---|
| `asset_model_index_zero` | 索引 == 0（Source 的「本实体无模型」哨兵） | 非 0 正常 |
| `asset_model_index_out_of_range` | 索引不在 `[1, 0xffff]` | 非 0 正常 |
| `asset_model_index_unresolved` | 索引在 `[1, 0xffff]` 内但表里没有 | **必须 0** |

分开的理由：本机 demo 发的哨兵是 **`-22`（`0xFFFFFFEA`）和 `-4`（`0xFFFFFFFC`）**，
不是 `-1`。sendprop 无符号读入后变成 `4294967274` / `4294967292`，所以判据必须是
「不在 `[1, 0xffff]` 内」，写 `index < 0` 永远为假。第一版把三者合成一个计数，
`unresolved` 读出 14 / 44，看着像「表缺了」，其实是哨兵混进来了。

### 2.2 第二个缺陷：`entity_model_probe` 忘了把协议号传进扫描

`main.cpp` 和 `demo_open_probe.cpp` 都在扫描前设了 `summary.networkProtocol`，
**只有 `entity_model_probe` 漏了**。协议号为 0 时 `svc_CreateStringTable` 走错分支：

- `networkProtocol > 23` → payload 长度按 **varint** 读；
- `<= 23` → 按 **20 位定长**读。

于是它解出一份「看起来合理但是错的」汇总。这正是这个缺陷难发现的原因：程序退出码 0，
输出是一行结构完整的 JSON。变异 m8b 复现的正是这个形态（§6）。

修法两处：调用点补上赋值；`scanKnownDemoMessages` 开头加
`if (networkProtocol <= 0) return false;`，把静默错读数变成响亮失败。

### 2.3 第三个缺陷（性能）：`resolveAsset` 每个模型重开一遍全部 VPK 目录

这个缺陷**不是本轮引入的，是本轮暴露的**。`ModelLoader::resolveAsset`
（`model_loader.cpp:527`）为了回答「这个路径在不在 VPK 里」，做的是：

```cpp
for (const auto& entry : std::filesystem::directory_iterator(root.tfDirectory, ec)) {
  if (name.size() >= 8 && name.substr(name.size() - 8) == "_dir.vpk") {
    VpkArchive archive;                       // 每次调用都新建
    if (archive.open(entry.path()) && archive.contains(result.requestedPath)) ...
  }
}
```

`VpkArchive::open()` 把**整个 `_dir.vpk` 读进内存并建哈希表** —— `tf2_misc_dir.vpk`
的目录树是几十 MB、约 5 万条目。而且 `hasCompanion` 还要对 `.vvd` / `.dx90.vtx` /
`.dx80.vtx` / `.sw.vtx` / `.phy` 各调一次，**每次再把候选归档重开一遍**。
所以一个模型 ≈ 6 轮 × 目录下每个 `_dir.vpk`。

P0 时这段代码**一次都没执行过**：所有 `AssetReference` 都 `hasModelPath=false`，
`buildRenderRequests` 的第一条语句就把它们全 `continue` 掉了，`resolveAsset` 从未被调用。
P1 把 505 条真实路径喂进去，代价才显形。

先量，再改。用 `model_path_bisect_probe`（诊断用，只存在于 `.scratch/`，不入交付）做标度实验：

| 阶段 | 引用数 | 检查数 | 耗时 | 每次检查 |
|---|---|---|---|---|
| `all` | 505 | 34 | **90.08 s** | 2.65 s |
| `sample` | 10 | 3 | 4.13 s | 1.38 s |
| `one` | 1 | **0** | **0.199 s** | — |

`one` 那一行是决定性的：**一个模型、零次检查，仍要 0.2 秒**。若瓶颈在 `inspectVpk`，
这一行应该是 0。所以代价在 `resolveAsset` 的重复开档，不在解模型 ——
既和代码读出来的结论一致，也排除了「某一条路径是元凶」。

修法：归档只在 `AssetRoot` 上开一次（`VpkArchiveSet`，惰性建、`shared_ptr` 共享），
`resolveAsset` / `hasCompanion` / `buildRenderRequests` 都查这份已解析的集合。
遍历顺序不变，所以 `vpkArchives.front()` 仍指向同一个归档。

**读数不变是这次修复的重点**：

| `entity_model_probe` on bagel | 修复前 | 修复后 |
|---|---|---|
| 耗时 | **117 s** | **23 s / 31 s**（两次实测） |
| `assetRefs` / `requests` | 679 / 505 | 679 / 505 |
| `demoRenderable` | 113 | 113 |
| `inspections` / `vpkExtracts` | 34 / 34 | 34 / 34 |
| `scoutChecksum` | 1098120898 | 1098120898 |

`scoutChecksum` 逐位相同，证明从 VPK 里取出来的字节没有变。

主程序（`tf2_demo_native`，bagel，`--audio-device 6`）：

| | P0 基线二进制 | P1 修复前 | P1 修复后 |
|---|---|---|---|
| 首帧时刻 | 17.82 s | 200 s 内无 metrics | **18.33 s** |
| FPS 平台 | 97.8–100.0 | — | 96.6–100.9 |

原始 metrics：`evidence/p1/p0-baseline.csv`（对照）、`evidence/p1/bagel-after-fix.csv`。
主程序比探针更慢，因为 `main.cpp:565` 还有一个**逐引用、无去重**的
`resolveAssetReference` 循环，把同一份代价又付了一遍。

### 2.4 一次错读：把「慢」当成了「卡」

记账要包括自己的错。第一轮我把 `entity_model_probe` 在 bagel 上的 117 秒读成了「卡死」，
并据此宣布「P1 引入了回归、卡在共享代码里」。**结论错了，原因是我的仪器**：
当时给的 `timeout` 比 117 秒短，进程被外壳杀掉，看起来就是「没有输出」。
按「读数变红先怀疑仪器」的顺序重测，给它足够时间，它正常返回了完整 JSON。

所以真正的结论不是「卡」，是**慢**：90 秒花在重复解析 VPK 目录树上。
`model_path_bisect_probe` 的 `one` 阶段（1 个模型 0.199 秒）才是把「慢在哪」钉死的那一行。

### 2.5 第四个缺陷（我引入的）：`AssetRoot` 加了成员却没补链接

`AssetRoot::archives()` 定义在 `asset_root.cpp`，它调用 `VpkArchiveSet::openDirectory`
（定义在 `vpk_archive.cpp`）。所以**任何编译 `asset_root.cpp` 的目标都必须同时链接
`vpk_archive.cpp`**。我改了 4 个本来就链接了的目标，**漏掉 `native_install_smoke_probe`**
（它只列了 `src/asset_root.cpp`）。

干净构建在它这里失败：

```
asset_root.cpp.obj : error LNK2019: 无法解析的外部符号
  "VpkArchiveSet::openDirectory(std::filesystem::path const &)"
  ，函数 "AssetRoot::archives(void) const" 中引用了该符号
native_install_smoke_probe.exe : fatal error LNK1120: 1 个无法解析的外部命令
NMAKE : fatal error U1077 ... Stop.
```

**NMAKE 在这里 `Stop.`，后面 4 个目标根本没建** → 九步链第 1 步报 `exe_count=17`（期望 21），
第 8 步变异套件因为找不到 `entity_message_fixture_probe.exe` 而 `RESTORED-DIFFERS`。
**一个根因，两个红步，而两个红步都不在我改的那段代码里。**

修法：给该目标补 `src/vpk_archive.cpp`，并在 `CMakeLists.txt` 写明这层耦合。
修完增量全量重建：`rc=0 / 0 error / 21 exe`。

**这条是门禁抓出来的，不是我。** 它同时说明 `exe_count=21` 这个工作量断言不是装饰：
`rc=0` 挡不住「某个目标没被构建」，只有计数能发现。

---

## 3. 真实资源路径

| 用途 | 路径 | 大小 |
|---|---|---|
| TF 资源根 | `D:/SteamLibrary/steamapps/common/Team Fortress 2/tf` | — |
| POV 测试 demo | `D:/SteamLibrary/steamapps/common/Team Fortress 2/tf/demos/autorecord_2026-07-02_13-26-46.dem` | 21 856 667 B |
| SourceTV 测试 demo | `D:/TF2_Demo_Player/work/protocol-rescue-20261005/73.dem` | 106 397 681 B |
| 语料目录 | `D:/SteamLibrary/steamapps/common/Team Fortress 2/tf/demos` | 1645 份 `.dem` |
| 独立 oracle | `D:/TF2_Demo_Player_Deliverable/tools/ent-oracle/target/release/ent-oracle.exe` | — |
| oracle 源码 | `D:/TF2_Demo_Player/work/_refs_demostf` | — |

---

## 4. 可复制的命令

```bash
cd /d/TF2_Native_Test
bash build-target.sh entity_protocol_probe entity_model_probe

TF="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
POV="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf/demos/autorecord_2026-07-02_13-26-46.dem"
STV="D:/TF2_Demo_Player/work/protocol-rescue-20261005/73.dem"

./native/build-nmake/entity_protocol_probe.exe "$POV" | grep -E 'precache_entries|asset_refs='
./native/build-nmake/entity_model_probe.exe --tf-root "$TF" --demo "$POV"
./native/build-nmake/entity_protocol_probe.exe "$STV" | grep -E 'precache_entries|asset_refs='
./native/build-nmake/entity_model_probe.exe --tf-root "$TF" --demo "$STV"
```

### 4.1 复现性能读数（§2.3）

```bash
cd /d/TF2_Native_Test
bash build-target.sh tf2_demo_native entity_model_probe

TF="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
BAGEL="D:/TF2_Demo_Player/testdata/demos/4a9bfb9276509d0ec5f5fdc722a95b17_match-20260927-0239-koth_bagel_rc13.dem"

# 探针：修复后 23-31 s（修复前 117 s），JSON 逐字段不变
time ./native/build-nmake/entity_model_probe.exe --tf-root "$TF" --demo "$BAGEL"

# 主程序：会出声，必须 --audio-device 6
rm -f evidence/p1/bagel-after-fix.csv
timeout 60 ./native/build-nmake/tf2_demo_native.exe --tf-root "$TF" --demo "$BAGEL" \
  --audio-device 6 --metrics-file "D:/TF2_Native_Test/evidence/p1/bagel-after-fix.csv"
head -3 evidence/p1/bagel-after-fix.csv   # 首帧应在 ~18 s 出现
```

P0 对照二进制（`053de1e` 编译，用于证明「慢是 P1 暴露的、不是 P1 引入的」）：
`.scratch/p0-tree/native/build-nmake/tf2_demo_native.exe`，输出见
`evidence/p1/p0-baseline.csv`。`.scratch/` 不入 git。

标度实验探针 `model_path_bisect_probe`（只存在于 `.scratch/p1-tree`，不入交付）：

```bash
cd /d/TF2_Native_Test/.scratch/p1-tree
bash build-target.sh model_path_bisect_probe
./native/build-nmake/model_path_bisect_probe.exe "$TF" "$BAGEL" 10
```

### 4.2 复现逐 tick 位移与 z 读数（§10）

```bash
cd /d/TF2_Native_Test
bash build-target.sh entity_model_probe
TF="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
BAGEL="D:/TF2_Demo_Player/testdata/demos/4a9bfb9276509d0ec5f5fdc722a95b17_match-20260927-0239-koth_bagel_rc13.dem"

# 位移 + Z 分布：注意 trajectoryRawZZero 是对照读数，修复后仍是 3516
./native/build-nmake/entity_model_probe.exe --tf-root "$TF" --demo "$BAGEL" \
  --trajectory 24 2>/dev/null | tr ',' '\n' | grep trajectory

# 渲染器实际拿到的位置（修复前这里 z=0）
./native/build-nmake/entity_model_probe.exe --tf-root "$TF" --demo "$BAGEL" \
  --trajectory-at 129211 --rendered 2>&1 >/dev/null | grep 'entity=1 '

# 原始解码属性（与 oracle 对齐用）
./native/build-nmake/entity_model_probe.exe --tf-root "$TF" --demo "$BAGEL" \
  --props-at 129211 --entity 1 2>&1 >/dev/null | grep -E 'm_nTickBase|m_vecOrigin'

# 发送表逐槽对照（对 ent-oracle 3 CTFPlayer）
./native/build-nmake/entity_model_probe.exe --tf-root "$TF" --demo "$BAGEL" \
  --dump-class-props CTFPlayer 2>&1 >/dev/null | sed -n '1,20p'

# 历史保留读数：archive / live 的 tick 列表与间距（§10.4；修复前 maxGap=54392，
# 2026-10-07 aa93926 修复后 maxGap=1180）
./native/build-nmake/entity_model_probe.exe --tf-root "$TF" --demo "$BAGEL" \
  --history-stats 2>&1 >/dev/null | head -2

# 历史覆盖门禁：fixture（合成均匀供给）+ bagel 端到端（§10.4 修复的判据）
bash history-coverage-check.sh    # HISTORY-COVERAGE=PASS
```

逐值门禁（与独立实现 demostf 对照，约 1.5 分钟）：

```bash
cd /d/TF2_Native_Test
PY=C:/Users/Administrator/.workbuddy-ai/binaries/python/versions/3.13.12/python.exe
$PY oracle-trajectory-check.py --ticks 4          # GATE=PASS, compared=40
$PY oracle-trajectory-check.py --ticks 2 --mutation   # MUTATION-CAUGHT=PASS
```

---

## 5. 原始输出

原始文件：`evidence/p1/protocol-pov.fixed.txt`、`evidence/p1/model-pov.fixed.txt`、
`evidence/p1/protocol-stv.fixed.txt`、`evidence/p1/model-stv.fixed.txt`。
（`.fixed.txt` = 修复版二进制的输出，与 `evidence/mutation/` 的命名一致。）

### 5.1 POV `autorecord_2026-07-02_13-26-46.dem`

```
sound_precache_entries=6720 sound_precache_decode_failures=0 model_precache_entries=1248 model_precache_max_entries=4096 model_precache_decode_failures=0 model_precache_updates=0
asset_refs=675 asset_model_path_known=387 asset_model_path_from_precache=387 asset_model_index_known=515 asset_model_index_zero=114 asset_model_index_out_of_range=14 asset_model_index_unresolved=0 asset_model_index_unresolved_max=-1 asset_identity_unknown=139
```

```
{"ok":true,"selfTest":false,"demo":"...autorecord_2026-07-02_13-26-46.dem","assetRefs":675,"requests":387,"demoRenderable":244,"inspections":43,"inspectionCacheHits":201,"vpkExtracts":43,"uniqueRenderable":4,"vpkRenderable":4,"scoutVertices":12000,"scoutChecksum":1098120898,"duplicateStable":true,"instanceCount":6,"playerFallbacks":1}
```

### 5.2 SourceTV `73.dem`

```
sound_precache_entries=6750 sound_precache_decode_failures=0 model_precache_entries=1126 model_precache_max_entries=4096 model_precache_decode_failures=0 model_precache_updates=2
asset_refs=562 asset_model_path_known=394 asset_model_path_from_precache=394 asset_model_index_known=476 asset_model_index_zero=38 asset_model_index_out_of_range=44 asset_model_index_unresolved=0 asset_model_index_unresolved_max=-1 asset_identity_unknown=67
```

```
{"ok":true,"selfTest":false,"demo":"...73.dem","assetRefs":562,"requests":394,"demoRenderable":147,"inspections":49,"inspectionCacheHits":98,"vpkExtracts":49,"uniqueRenderable":4,"vpkRenderable":4,"scoutVertices":12000,"scoutChecksum":1098120898,"duplicateStable":true,"instanceCount":6,"playerFallbacks":1}
```

`model_precache_updates=2`：SourceTV 走的是 `svc_UpdateStringTable` 增量路径，
POV 走 `svc_CreateStringTable`。两条路径现在共用 `walkPrecacheTablePayload`，
所以「创建路径通了、更新路径没通」这类只覆盖一半的修复不会发生。

### 5.3 9 份探针语料（冻结在 `evidence/probe-baseline/added-lines.txt`）

| demo | `model_precache_entries` | `model_precache_max_entries` | `model_precache_updates` | `asset_refs` | `from_precache` | `unresolved` |
|---|---|---|---|---|---|---|
| bagel | 1430 | 4096 | 0 | 679 | 505 | **0** |
| snakewater | 1193 | 4096 | 1 | 626 | 451 | **0** |
| ashville73 | 1126 | 4096 | 2 | 562 | 394 | **0** |
| decal | 941 | 4096 | 0 | 616 | 290 | **0** |
| protocol23 | 843 | **2048** | 0 | 724 | 366 | **0** |
| comp | 945 | 4096 | 1 | 720 | 315 | **0** |
| saytext2 | 1073 | 4096 | 0 | 839 | 449 | **0** |
| short2024 | 1291 | 4096 | 0 | 295 | 151 | **0** |
| small | 779 | 4096 | 0 | 204 | 72 | **0** |

protocol23 的 `max_entries=2048` 与其余 8 份的 4096 不同，说明这个字段确实是从包里读出来的，
不是常量。9/9 `unresolved=0`：没有一份 demo 的实体指向表里没有的模型。

### 5.4 独立实现的旁证

`work/_refs_demostf/test_data/string_tables/modelprecache_meta.json`（Rust `tf_demo_parser`，
demostf，带真实 `modelprecache.bin` 往返测试）：

```json
{ "max_entries": 4096, "fixed_userdata_size": { "size": 1, "bits": 2 }, "count": 780 }
```

`max_entries=4096` 与本机 8 份 demo 的读数一致。

**固定长度 user data 的宽度**：本机按包内声明的 `m_nUserDataSizeBits`（4 位字段）跳位，
oracle 按 `fixed_userdata_size.bits` 跳位 —— 两边都是「跳 bits 位」，同一种约定。
这条约定不是靠读文档确认的，是靠数据自证：**宽度取错会让整表的字符串错位，
解出来的路径不可能在 VPK 里命中**。而 387 / 394 条路径实际取到了模型
（`vpkExtracts=43` / `49`，`demoRenderable=244` / `147`），宽度只能是对的。

---

## 6. 反向验证（`bash mutate.sh`）

新增两个用例，都落在 `mutate.sh` 里，随 `verify-all.sh` 8/9 步一起跑。

### m7：把 `modelprecache` 分支去掉

```
MUTATION-RED   m7 modelprecache dropped -> model_precache_entries       0 (fixed=1248)
MUTATION-RED   m7 modelprecache dropped -> asset_model_path_from_precache 0 (fixed=387)
MUTATION-RED   m7 modelprecache dropped -> asset_model_index_unresolved 387 (fixed=0)
MUTATION-RED   m7 modelprecache dropped -> entity_model_probe requests  0 (fixed=387)
MUTATION-HOLD  m7 modelprecache dropped -> sound_precache_entries stays 6720 (unchanged)
```

最后一条是**必须不动**的：P0 的音频与实例基线路径依赖 `soundprecache` 解出同样的东西。
两个表现在共用同一个 `walkPrecacheTablePayload`，所以这条 `must_hold` 是在证明
「合并三份近似拷贝没有改动原来那份的行为」。

### m8：把协议号赋值去掉（两步，因为差别就是结论）

```
--- m8a: 去掉赋值，守卫在
{"ok":false,"error":"demo scan failed (network protocol not set?)"}   exit=1

--- m8b: 连守卫一起去掉（缺陷原来的形态）
{"ok":true,...,"assetRefs":673,"requests":0,"demoRenderable":0,...}   exit=0
```

m8a 证明守卫把静默错读数变成了响亮失败；m8b 证明**没有守卫时它就是一个退出码 0、
结构完整、数字全错的 JSON** —— 这正是这个缺陷最初躲过审查的原因。
`assetRefs=673` 与源码注释里记的历史读数逐字一致。

### m2 的补丁模式被本轮重构改掉了（已修）

P1 把 `soundprecache` 的 create / update 两份近似拷贝合成了 `walkPrecacheTablePayload`，
所以 m2 原来匹配 `update.read(14, userBytes)` 与 `table.read(...) { tableOk = false; ... }`
的两条模式**都不再存在**，`mutate.sh` 报 `PATTERN NOT FOUND` 并整条用例作废。
这不是「读数变红」，是**仪器坏了**：`verify-all.sh` 8/9 步因此从 25 条断言掉到 22 条。
按「读数变红先怀疑仪器」的顺序查到根因，把模式改成合并后的那一行（五处变四处），
重跑后读数与 P0 完全一致：

```
MUTATION-RED   m2 1024 cap restored -> instance_baselines               6 (fixed=126)
MUTATION-RED   m2 1024 cap restored -> malformed_packets                1 (fixed=0)
MUTATION-RED   m2 1024 cap restored -> baseline_misses                  9191 (fixed=16)
```

顺带说明：`verify-all.sh` 8/9 步原来只 grep `MUTATION-SUITE=PASS`，
**一个用例都没跑也会通过**。现在改成数 `MUTATION-RED` 行数（25）+ `MUTATION-HOLD`（3）
+ `MUTATION-GREEN`/`BROKE` 必须为 0 + `RESTORED-IDENTICAL`（6）。m2 这次失效就是被这个
计数抓出来的 —— 如果只有 `MUTATION-SUITE=PASS`，它会以绿色通过。

---

## 7. P0 读数未移动（回归）

bagel（`entity_protocol_probe`，修复后）：

```
packets_scanned=73121 malformed_packets=0 unknown_message_packets=0
unknown_message_types=<none>
entity_failures=0 entity_update_header_failures=0 packet_entity_decode_failures=0 prop_missing_table=0 prop_index=0 prop_value=0
instance_baselines=126 baseline_applied=10035 baseline_misses=16 baseline_apply_failures=0
string_table_user_data_max_bytes=7669
sound_precache_entries=6701 sound_precache_decode_failures=0
```

与 P0 验收文档 §4.3 逐值一致。protocol23 的 `malformed_packets=0 unknown_message_packets=0`
与 58/58 fixture 也一致。

`check-probe-output-additive.sh` 现在有**两条主张**：

1. 旧计数器逐字节未动 —— 把本轮新增的两行（`sound_precache_entries=`、`asset_refs=`）
   剥掉后与 `evidence/probe-baseline/` 的冻结报告比对，`compared=9/9`；
2. **被剥掉的那两行本身也有冻结基线** —— `evidence/probe-baseline/added-lines.txt`
   （18 行），`--refresh-added` 才刷新。

第二条是补第一遍的漏：只剥离不冻结，「additive」就变成了「没人验证」的同义词。
这正是本轮之前踩过的那类错（见 P0 验收 §0.7）的同一形状。

### 7.1 一条命令的全链（`bash verify-all.sh --quick`，`VERIFY=PASS`）

| 步 | 读数 |
|---|---|
| 1 构建 | `errors=0 warnings=1 exe_count=21`（既有 `main.cpp(1313) C4457`；行号随改动漂移，内容未变） |
| 2 九份普查 | 9/9 `entity_failures=0 malformed_packets=0 unknown_message_packets=0`，coverage 9/9 |
| 3 fixture | 58/58，`fixture_failures=0` |
| 4 oracle 九份 | 9 行逐值全等，`ORACLE-GATE=PASS` |
| 5 oracle 语料抽样 | `sampled=8`，`ORACLE-CORPUS=PASS` + selftest |
| 6 录制类型 | `compared=9/9`，`ORACLE-RECORDING-TYPES=PASS` |
| 7 探针增量性 | `compared=9/9`，`added-lines=IDENTICAL (18 lines frozen)`，`PROBE-OUTPUT-ADDITIVE=PASS` |
| 8 C++ 变异 | `MUTATION-RED=25 HOLD=3 GREEN=0 BROKE=0 RESTORED-IDENTICAL=6`，`MUTATION-SUITE=PASS` |
| 9 普查可证伪 | 10/10 变异，`CENSUS-NEGATIVE=PASS` |

原始输出在 `evidence/verify/1-build.txt` … `9-census-negative.txt`。

上表是**最终提交 `9316416`** 上的结果。P1 的第二次提交（`4868e7b`）曾跑出 `VERIFY=FAIL`，
原因见 §2.5：`exe_count=17` 与 `MUTATION-SUITE=FAIL` 是同一个链接缺陷的两个表现。
门禁抓到了它，修完（`9316416`）后重跑即上表。
本节表格是**第一轮（9 步，`9316416`）**的历史读数；后续两轮的链读数见 §10.7（10 步）、
§11.4（12 步），以及 HANDOFF §3.2 的两段「闭合 / 再闭合」记录。全链始终是同一命令
（`bash verify-all.sh --quick`），步数只因新增门禁而增长。

### 7.2 用干净构建的产物复测 P1 读数

上表第 1 步是**干净构建**（`exe_count=21`），所以第 2–9 步用的都是全新建出来的二进制。
在此之上再复测两条 P1 读数，确认文档数字对最终提交状态成立：

| 读数 | 第一次（`4868e7b` 时构建） | 复测（干净构建） |
|---|---|---|
| `entity_model_probe` bagel 耗时 | 23 s | 31 s |
| 同上 JSON | `assetRefs=679 requests=505 demoRenderable=113 inspections=34 vpkExtracts=34 scoutChecksum=1098120898` | **逐字段相同** |
| 主程序 bagel 首帧 | 18.33 s | 17.21 s |
| 主程序 FPS 峰值 | 100.86 | 99.53 |

耗时在 23–31 s 之间波动（磁盘缓存 / 机器负载），**修复前的 117 s 与修复后的 23–31 s
不重叠**，量级差异成立。JSON 与 `scoutChecksum` 两次完全相同，说明波动只在耗时上。

---

## 8. 已验证 / 未验证 / 已知限制

### 已验证

- `modelprecache` 在真实 POV 与真实 SourceTV 上都被解出（1248 / 1126 条），
  创建与增量更新两条路径都覆盖（`model_precache_updates=0` / `2`）。
- `m_nModelIndex` → 路径的解析在 675 / 562 条引用上生效（387 / 394 条），
  **全部来自 precache 表**（`asset_model_path_from_precache` 与 `asset_model_path_known` 相等）。
- 解析出的路径真的能加载：`vpkExtracts=43` / `49`，`demoRenderable=244` / `147`。
- 9 份探针语料 9/9 `asset_model_index_unresolved=0`。
- 两种哨兵（`index==0`、越界）与真缺失分开计数，修复后不再互相掩盖。
- 协议号守卫可证伪（m8a 退出码 1、m8b 退出码 0 且数字全错）。
- P0 全部读数未移动；58/58 fixture 未动。
- **主程序在 bagel 上跑通**：首帧 18.33 s（P0 基线二进制 17.82 s）、
  FPS 平台 96.6–100.9（P0 97.8–100.0）、tick 正常推进到 1560+，
  `--audio-device 6` 未落到系统默认设备（`evidence/p1/bagel-after-fix.csv`）。
- **VPK 目录只解析一次**：修复前后 `entity_model_probe` 在 bagel 上的
  `assetRefs` / `requests` / `demoRenderable` / `inspections` / `vpkExtracts` /
  `scoutChecksum` 逐字段相同，耗时 117 s → 23 s / 31 s（两次实测）。
- **玩家 Z 已修**：渲染器在 bagel server tick 129211 收到 `z=407.836273`，
  与独立实现的 `407.83627` 一致（修复前为 `0`）；`--rendered` 端到端读数。
- **Local/NonLocal 选择已确定**：改为优先 `LocalPlayerExclusive`，
  不再依赖 `unordered_map` 顺序。
- **新增逐值门禁 `oracle-trajectory-check.py`**：`compared=40 mismatches=0`，
  变异 `MUTATION-CAUGHT=PASS (18 mismatches)`，且 `compared==0` 时直接 FAIL。
- **tick 对齐按值推导**：门禁从两侧共同导出 offset（bagel 上 51811），
  不硬编码；且实测该 offset 在同一份 demo 内**并不恒定**（demo tick 8 处为 56146）。

### 未验证

- **画面没有人工确认。** 本轮读数是「渲染请求被构造出来、模型从 VPK 取到、
  主程序能起来并以 ~100 FPS 推进 tick」，**不是**「屏幕上画对了」。
  没有截图对比，也没有和 Source 逐像素比对。
- **主程序只在 bagel 一份 demo 上跑过**。其余 8 份探针语料只跑过
  `entity_protocol_probe` / `entity_model_probe`，没有跑主程序。
- **逐 tick 位移已做，但只在 bagel 的尾部实时窗口内与独立实现逐值对照过**
  （§10）。观察目标、武器、投射物、team/skin 的接线不在本轮。
  `requests>0` 不蕴含这些实体在正确的位置上。
- `playerFallbacks=1` 仍在：仍有实体没解析出模型而走玩家职业兜底。
  具体是哪一类实体没有逐类统计。
- `asset_identity_unknown` 139 / 67 条（无任何模型标识的引用）未逐类归因。
- 全量 1645 份语料**没有**为本项重跑；`unresolved=0` 的覆盖是 9 份探针语料
  + 2 份手工抽查。`run-corpus-evidence.sh` 可跑，未跑。
- 固定长度 user data 的位宽没有单独打印成读数，是按「路径能从 VPK 命中」反推的
  （见 §5.4）。要把它变成显式读数需要再加一行输出。

### 已知限制

- `model_precache_max_entries` 在 protocol23 上是 2048，其余是 4096；解析按包内声明走，
  没有硬编码 4096。
- 模型路径只按 `m_nModelIndex` 解析。`m_nModelIndexOverrides`（`DT_BaseEntity` 里存在）
  未处理。
- `AssetRoot::archives()` 是惰性建集合，**不是线程安全的**：同一个 `AssetRoot`
  被多个线程首次并发调用会竞争。当前所有调用点都是单线程（`main.cpp` 主线程、
  各探针的 main），所以没有实际暴露；若以后并行解析模型，需要在 `fromPath`
  里提前建好。
- 本机 exe 哈希不可复现（两次强制重链接差 2 字节），复核锚点用补丁哈希 + 读数。

---

## 9. 对旧文档的更正

| 位置 | 原文 | 更正 |
|---|---|---|
| `native/docs/HANDOFF-2026-10-06.md:38` | 「当前 `entity_model_probe` 仍为 `vpkRenderable=0`」 | `vpkRenderable` 只在「硬编码 5 个模型」那条分支里赋值，demo 路径的对应读数是 `demoRenderable`。过时的是 `demoRenderable=0`（现 244 / 147）。`vpkRenderable` 在带 `--tf-root` 时读数为 4，在只跑 `--self-test` 时为 0 —— 这行原文的措辞本身就不精确 |
| `native/docs/TASKS-2026-10-06.md:96` | 同上 | 同上 |
| `native/docs/entity-model-instances-2026-10-05.md:26` | 「`assetRefs=195 requests=0 demoRenderable=0`」，并归因为「采样的 Demo 只有 assetRefs、没有 `m_ModelName` 路径，所以回放只能靠玩家职业兜底模型」 | 读数是真实现的，**归因是错的**：demo 不发路径是设计如此（路径在 `modelprecache` 里），不是数据缺失。`requests=0` 是解析缺陷，现已修复 |

---

## 10. 逐 tick 位移（第 1 项的第二条）：一个渲染缺陷、一个新门禁、一个已修的历史保留阻塞项

本节是「基础回放实体画面」里**逐 tick 位移**这一条的验收。三条结论：

| # | 结论 | 状态 |
|---|---|---|
| 1 | 玩家 Z 恒为 0，因为真实 Z 在另一条属性里 | **已修** |
| 2 | Local / NonLocal 两份 origin 由哈希顺序决定，差约 4000 单位 | **已修** |
| 3 | 只有 demo 尾部约 70 个包是 tick 精确的，其余解析到最远 54392 tick 之前的快照 | **2026-10-07 已修（`aa93926`）**：按最大 tick 间隙抽稀，worst gap 1180 / 下界 761；窗口外仍是 Checkpoint（有界） |

### 10.1 缺陷一：玩家的 Z 恒为 0（已修）

**现象**：`EntityModelResolver::extractTransform`（`native/src/entity_model.cpp:41`）
只读 `m_vecOrigin` 这一个属性的 `z`，而玩家的 `m_vecOrigin` 是 **VectorXY**，
wire 格式只带 x 和 y，解码器永远不会往它的 `z` 写值（恒为 0）。真实的 Z 在
**另一条独立的 Float 属性** `m_vecOrigin[2]` 里，值落在 `EntityPropertyValue.x`。

**证据（三条，互相独立）**：

1. 发送表本身有四个 origin 槽位，两边逐槽一致（`--dump-class-props` 对
   `ent-oracle 3`）：

```
[10] DT_TFLocalPlayerExclusive.m_vecOrigin      VectorXY
[11] DT_TFLocalPlayerExclusive.m_vecOrigin[2]   Float
[14] DT_TFNonLocalPlayerExclusive.m_vecOrigin   VectorXY
[15] DT_TFNonLocalPlayerExclusive.m_vecOrigin[2] Float
```
   `flat=885` 两侧相同，CBeam 的 `flat=44` 也相同。

2. 独立实现（demostf）在 wire 上确实看到这条 Float：
   `ent-oracle <bagel> 8` → `idx= 11 DT_TFLocalPlayerExclusive.m_vecOrigin[2] = Float(354.6386)`。

3. 我们的解码器也**确实**保留了它，只是消费方不读：
   `entity_model_probe --trajectory-at 129211` 里
   `DT_TFLocalPlayerExclusive.m_vecOrigin[2] type=1 x=407.836273`（type=1 = Float）。

**修复**：新增 `readVectorProperty`（`entity_model.cpp`）——按基名找 Vector/VectorXY，
再用同名的 `[i]` 兄弟标量补齐该向量编码不携带的分量。缺失兄弟时**不静默补 0**，
而是把 `diagnostic` 置为 `origin-z-sibling-missing` 并由探针计数
（`trajectoryZSiblingMissing`）。

**修复前后（bagel，`--trajectory 24`）**：

```
修复前  trajectoryZZero=3516  trajectoryZNonZero=10560
修复后  trajectoryZZero=3204  trajectoryZNonZero=10872
        trajectoryRawZZero=3516  trajectoryZSiblingMissing=0
```
`trajectoryRawZZero` 是**对照读数**：它读的是解码器写进 VectorXY 的原始 z，
修复后仍是 3516（缺陷的签名还在，因为 wire 格式就是这样）。两者相差的 312
个采样点就是被修复救回来的玩家位置；剩下的 3204 个 z=0 是**真值为 0** 的实体
（`CWorld`、`CVoteController`、地面上的 prop），不是缺陷。

**端到端**（渲染器实际拿到的值，`--rendered`）：

```
C++  server tick 129211  entity 1  x=-3157.420166 y=110.702408 z=407.836273
oracle demo tick 77400   entity 1  x=-3157.4202   y=110.70241   z=407.83627
```
修复前这里是 `z=0`。

### 10.2 缺陷二：Local / NonLocal 的选择由哈希顺序决定（已修）

`EntityState::properties` 是 `unordered_map`，`findProperty` 遍历它取**第一个**
后缀匹配。玩家同时带 `DT_TFLocalPlayerExclusive.m_vecOrigin`（全精度）与
`DT_TFNonLocalPlayerExclusive.m_vecOrigin`（量化），两者都匹配。

**这不是舍入误差**。bagel server tick 129211 实测：

```
LocalPlayerExclusive      x=-3157.420166 y=110.702408 z=407.836273
NonLocalPlayerExclusive   x=765.625000   y=-529.500000 z=320.000000
```
两点相距约 **4000 单位**（TF2 地图半径约 16384）。也就是说同一个实体的渲染位置
可以在这两个值之间随哈希顺序翻转，而所有计数型判据都是绿的。
（NonLocal 那组是量化值且**已经很旧**——该包里根本没发 `idx=14/15`，
我们的重建把最后一次收到的值留着，这也是为什么它停在一个陈旧的坐标上。）

**修复**：`readVectorProperty` 里加 `exclusiveRank`，优先
`LocalPlayerExclusive`（0）→ 其它（1）→ `NonLocalPlayerExclusive`（2），
同档再按名字字典序取最小，结果与哈希顺序无关。

### 10.3 新门禁：与独立实现逐值对照（`oracle-trajectory-check.py`）

`check-oracle.sh` 只比**计数**（多少实体、多少包）。计数看不见「解码正确但读错属性」
——这正是 z 缺陷的形状：所有计数全绿，每个玩家都在 z=0。所以新增一条逐值门禁。

**tick 对齐是推导出来的，不是硬编码的。** C++ 侧存的是 server tick
（来自 `svc_NetTick`），oracle 迭代的是 demo entry tick，两者的差**在一份 demo 内
并不恒定**（实测 demo tick 8 处是 56146，demo tick 77400 处是 51811）。
门禁的做法是：先问探针它的 tick 精确窗口从哪开始（`S0`），再问 oracle
哪个 demo tick 的 `delta` 等于 `S0`，然后 `offset = S0 - demo_tick` 由两侧在同一次
运行中共同给出。

```
$ python oracle-trajectory-check.py --ticks 4
live window starts at server tick 129210; its first packet's own server tick is
129211 == demo tick 77400 (offset 51811)
archive max gap 54392 ticks -- ticks outside the live window are NOT comparable
entities=12 serverTicks=4 compared=40 mismatches=0 skipped=0
GATE=PASS
```

`compared=40` 是工作量断言：一个什么都没比的判据不算判据（`compared==0` 直接 FAIL）。

**变异验证**（`--mutation`，把期望值整体 +1.0）：

```
$ python oracle-trajectory-check.py --ticks 2 --mutation
MUTATION-CAUGHT=PASS (18 mismatches on the perturbed expectation)
```

**这个门禁抓到过我一次**：第一版把 `demo_tick = server_tick - offset` 直接用，
结果 40 个值里 36 个不匹配、每个都差大约「一 tick 的位移」。原因是
`svc_PacketEntities` 的 `delta` 字段是**基线 tick 而不是本包的 tick**，
本包产生的状态属于 `delta + 1`。门禁红了一次，红得对；修的是门禁不是被测代码。

### 10.4 阻塞项：只有尾部约 70 个包是 tick 精确的（2026-10-07 已修，`aa93926`）

`queryEntitySnapshotAtOrBeforeTick` 的语义是「取 at-or-before 的最新检查点」。
检查点分两层：一个**有界的实时窗口**（重放该窗口内的逐包事件）和一个
**稀疏归档**（`thinHistoryArchive`）。实测 bagel：

```
$ ./native/build-nmake/entity_model_probe.exe --tf-root "$TF" --demo "$BAGEL" --history-stats
history archive=96 ticks=[56148..129125] medianGap=128 maxGap=54392
history liveCheckpoints=2 ticks=[129210..129210] maxGap=0 packets=68 events=1155 dropped=172
```

归档的完整 tick 列表（原始输出，可自行重算间距）：

```
56277 56405 56489 56489 56617 56745 56873 56943 56943 57071 57199 57327 57404 57404
57532 57660 57788 57829 112221 112684 112720 113225 ... 129125
                 ^^^^^^^^^^^^^^^^^^^ 54392 tick 的洞（约 13.7 分钟）
```

两个可复核的事实：

1. **`57829 → 112221` 之间没有任何保留快照。** 落在这个区间的 tick 会解析到
   57829 那个检查点，即最远 **54392 tick（约 13.7 分钟）** 之前的实体位置。
2. **`56489 56489`、`56943 56943`、`57404 57404`、`124508 124508`… 成对重复。**
   每次 flush 之后 `packetOrdinal` 归零，而归零后又满足
   `packetOrdinal % checkpointStride == 0`，于是同一个 tick 被推入两个检查点
   （`demo_header.cpp:1104` 与 `:1128`）。浪费内存，也污染按索引均匀抽稀。

**这对主程序是实际影响**：`main.cpp:1260` 把 `Checkpoint` 与 `Available` 一起
当作可绘制，然后 `buildInstances` + 逐个画。也就是说除最后约 6 秒外，
实体画面来自最远 13.7 分钟之前的快照——屏幕上会是每十几分钟跳一次的「瞬移」，
而不是平滑移动。

**根因（已定位到代码；修复见本节末尾）**：`thinHistoryArchive` 按**索引**均匀抽稀
（`index = (last * slot) / (maxCount - 1)`），而调用方需要的是按 **tick** 均匀覆盖。
每次 flush 追加的检查点数固定（≤8）但覆盖的 tick 跨度随实体活跃度变化，
于是「按索引均匀」会过度采样检查点密集的区段、在稀疏区段留下空洞。

**它同时解释了本轮两个刺眼的数字**：`trajectoryMaxStep=480250`、
`trajectoryMaxZ=275845`。它们不是解码错误——`--trajectory-dump` 显示实体 724
在 tick 56148 是一个玩家（`x=365.2 y=-341.5 z=251.7`），在 70760/85373/99985
是同一个归档检查点里的一个 `CBeam`（`x=232944 y=253587 z=271883`），
在 114598 又变回玩家（`x=329.3 y=-342.5 z=263.2`）。**实体索引在相隔很远的
快照之间被复用**，而「位移」是在这些不同纪元的快照之间算的。

> 关于 `CBeam.m_vecOrigin = 232944` 本身是不是解码错误：**未验证**。
> 它的表项是 `Vector flags=0x0400 bits=19 range=[-16384,16384]`，而我们在能对齐的
> tick 上对玩家位置与 oracle **逐位一致**（§10.3），所以 `readSendPropValue`
> 的坐标路径至少对玩家是正确的。CBeam 这条没有找到 oracle 侧的同类样本
> （扫了 17 个 tick，没有 CBeam 的 origin 变更），**记为未验证**。

**修复（2026-10-07，`aa93926`）**

三个决定都做了：

1. `thinHistoryArchive` 改按**最大 tick 间隙优先**抽稀：优先队列取最宽的间隙、
   二分选最接近 tick 中点的检查点、首尾钉住。抽稀在**每次 flush** 时运行，
   所以「按索引」的 floor() 钉头行为会在下一次 flush 被再次放大 —— 换成「按 tick」
   后同一份归档（96 槽）的 worst gap 从 54392 降到 1180
   （鸽笼下界 `floor = span/(kept-1) = 761`，门禁断言 worst ≤ 2×floor）。
2. flush 后的重复推入删除（`packetOrdinal` 归零后本就会按 stride 推一个）；
   另加 `entityHistoryFlushes` 计数，把 flush 型丢弃与 gap 型丢弃分开
   （修复前两者混在同一个 `dropped` 里，`dropped=172` 无法归因）。
3. `main.cpp` 在 `Checkpoint` 下**仍按原样冻结绘制**（不改画面行为），
   但标题新增 `stale=`；`queryEntitySnapshotAtOrBeforeTick` 新增 `resolvedTick`
   出参，把「答案来自哪个 tick」变成可读数字而不是沉默。

修复后 bagel 读数（`--history-stats`，另加 `--history-coverage 512`）：

```
history archive=96 ticks=[56148..129125] medianGap=822 maxGap=1180
history liveCheckpoints=1 ticks=[129210..129210] maxGap=0 packets=68 events=1155 dropped=172
history flushes=172 gapDropped=0
history coverage samples=512 exact=1 checkpoint=511 unavailable=0
history gap worst=1180 at=[66628..67808] median=822 floor=761 budget=96
history staleness worst=1139 at=87460 mean=407.5
```

（`medianGap` 128 → 822 是预期代价：旧策略把槽位堆在头部，中位好看、worst 失控；
新策略把同样的 96 个槽位摊到全跨度，用中位换 worst 的界。查询感受的是 worst。）

判据（新增，并接为 `verify-all.sh` 第 11 步）：`history-coverage-check.sh` 断言
fixture（合成均匀供给：`historyWorstGap=4096 ≤ 2×historySlotFloor=3565`）与 bagel
（`worst=1180 ≤ 2×floor=761`、`distinct=97/97`、`sampled=512`、`unavailable=0`、
`staleness 1139 ≤ worstGap 1180`、且不得再现 54392 量级的洞）；
`mutate.sh m9` 把按索引抽稀**原样改回**，fixture 与门禁一起变红
（fixture worst gap 23040、bagel 51568、gate `HISTORY-COVERAGE=FAIL`）。

**仍是能力边界**：窗口外答案还是 `Checkpoint`（只是上了界）。要做到窗口外
tick 精确必须重塑保留表示 —— 全量事件链实测 856 MB 事件 / 常驻 4.77 GB /
每 checkpoint 约 8.34 MB（bagel 96 槽 = 801 MB），已否决。

### 10.5 本轮新增的读数开关

| 开关 | 作用 |
|---|---|
| `--trajectory N` | 沿 demo 等距取 N 个 tick，报告位移/Z 分布/摘要；现在同时给「原始」与「渲染」两套 z 读数 |
| `--trajectory-dump` | 位移最大的实体的逐 tick 轨迹（stderr） |
| `--trajectory-at t1,t2` | 每个实体在指定 tick 的**原始** origin 属性（stderr） |
| `--rendered` | 与 `--trajectory-at` 合用：改印 `extractTransform` 的输出，即渲染器拿到的值 |
| `--props-at t1,t2 [--entity N]` | 指定 tick 的属性；带 `--entity` 时打印该实体全部属性（含 `m_nTickBase`），用于与 oracle 对齐 |
| `--dump-class-props <substr>` | 扁平发送表：槽位、owner、名字、类型、`flags`/`bits`/`range`。对 `ent-oracle 3` 的形状 |
| `--history-stats` | 归档/实时窗口的 tick 列表与间距，即 §10.4 的读数 |
| `--history-coverage N` | 均匀抽 N 个 tick 查询一遍：`exact`/`checkpoint`/`unavailable`、最坏/中位 tick 间隙 vs 鸽笼下界、最坏陈旧度、字节估计（`aa93926` 新增） |

全部输出走 stderr，stdout 仍是**单个可解析的 JSON 对象**；
默认（不带这些开关）输出与 `evidence/probe-baseline/` 冻结的内容逐字节一致。

### 10.6 顺带修掉的判据脆弱性：`census-negative-test.sh` 的 mutation I

跑本轮验收链时第 9 步变红了，但**不是因为代码改动**：

```
=== mutation I: index_state=truncated_tail must SKIP, not FAIL and not clean ===
  OK   verdict stayed green (a skip is not a failure)
  FAIL not counted as skipped
```

mutation I 断言 `skipped=1`，这**隐含假设基线里没有已跳过的 demo**。而语料是活的
（1644 份里有 7 份是「录制中断」的 `truncated_tail`），一旦 `--sample 24` 抽到其中一份，
基线本身就是 `skipped=1`，变异后是 2 —— 断言为红，而红的原因是**环境在动**，
不是代码坏了。这一次的现场：`reports=53 clean=23 skipped=1 dirty=0
sum_packets=1260289`，`extra_reports=21 (corpus grew -> sample moved)`。

修法：读基线的 `skipped`，断言变异后**恰好 +1**，并断言基线值确实是个数字
（否则缺行会让比较恒真）。修后：

```
  OK   counted as skipped (1 -> 2)
```

顺带加了一条同类断言：**victim 自己必须是 `index_state=ok`**。若 victim 恰好是被跳过的那份
报告，mutation A–H 就是在改一份「判据根本不读」的报告，会以「什么都没做」的方式通过 ——
与 §「判据不能只写没有失败」是同一类错误。

### 10.7 逐值门禁已接进验收链（`verify-all.sh` 第 10 步）

一条不在链里的判据等于没人跑的判据。第 10 步跑 `oracle-trajectory-check.py`（`--ticks 4`）
与它的变异用例，断言 `TRAJECTORY-ORACLE=PASS (compared=40, mutation caught)`；
`compared` 低于 20 或变异没抓住都算失败。

### 10.8 本轮未验证 / 已知限制

- **画面仍无人工确认。** §10.1 的端到端读数证明渲染器**收到了** `z=407.836273`，
  不证明屏幕上画对了。
- **逐 tick 位置对照只在 bagel 的实时窗口内做过**（12 个实体 × 4 个 tick = 40 个值）。
  其余 8 份 oracle 语料没有做逐值对照，因为它们的实时窗口同样只有尾部几十个包。
  （窗口外的答案不是「精确」的，其陈旧度上界由 `history-coverage-check.sh` 断言 ——
  这是两种不同强度的保证，别混用。）
- **§10.4 的阻塞项已在 2026-10-07（`aa93926`）修复**：按 tick 抽稀 + 去掉重复检查点
  已做；主程序在 `Checkpoint` 下**仍冻结绘制**（这是个决定：不改画面行为，
  只把陈旧度变成读数）。窗口外答案仍是 `Checkpoint`，只是有了界
  （worst ≤ 2×鸽笼下界，由 `history-coverage-check.sh` 担保）。
- `trajectoryDigest` 在同一个二进制上两次运行相同，但它的输入里仍包含按
  `unordered_map` 顺序遍历的实体（修复后 origin 的选择已确定，但**遍历顺序**
  仍是实现定义的）。跨编译器/跨 STL 版本不应假定一致。
- CBeam 的 `m_vecOrigin = 232944`（§10.4 末尾）未归因。
- `SendPropType` 没有无符号区分（我们的 `Int` 对应 oracle 的 `UnsignedInt`）。
  逐槽对照时这一列不可比；是否有符号扩展错误**未验证**。

---

## 11. 武器世界模型（P1 剩余子项，2026-10-07 第二轮，`cd36db1`）

### 11.1 缺陷：世界路线把「手臂」当成了武器

TF2 的武器实体带**两个**模型索引，它们指的不是同一个东西：

| 属性 | 含义 | POV demo server tick 55418 实测（8/8 件武器） |
|---|---|---|
| `DT_BaseEntity.m_nModelIndex` | 第一人称合成模型（`c_*_arms`） | pistol 1097 / medigun 1060 / knife 1088 |
| `DT_BaseCombatWeapon.m_iViewModelIndex` | 同上；这批武器上两槽位逐件相等 | 1097 / 1060 / 1088 |
| `DT_BaseCombatWeapon.m_iWorldModelIndex` | **武器本体**（掉落 / 第三人称绘制用） | pistol 255 / medigun 261 / knife 240 |

modelprecache 解析：255=`c_pistol`、240=`c_knife`、1097=`c_engineer_arms`。

旧管线只读 `m_nModelIndex`。这个错误**能成功渲染出一个真实资产**（一双手臂），
所以所有计数型门禁全绿也看不见它 —— 与 §10 的 z 缺陷同类：
「读对了位、写进了属性、渲染器读错了槽位」。

### 11.2 修复（`cd36db1`）

`buildAssetReferenceList` 新增世界路线：带 `m_iWorldModelIndex` 的引用**先**按它查表，
只在世界索引指不到任何已声明条目时才回落到 `m_nModelIndex` 路线。
该块是**加法**：只在旧路线会填空（或留空）的位置填，因此 P0/P1 既有计数器零移动
（`check-probe-output-additive.sh` 的 claim 1 九份语料 9/9 `IDENTICAL`）。

新增读数（`entity_protocol_probe` 的 `asset_refs=` 行尾 + `entity_model_probe` JSON）：

| 读数 | 含义 |
|---|---|
| `asset_world_model_index_known / resolved / zero / unresolved / out_of_range` | 人口与其划分（恒等式 `resolved+zero+unresolved+outOfRange == known`） |
| `asset_model_path_from_world_model` | 走了世界路线的路径数 |
| `asset_model_path_world_model_only` | **只有**世界路线能给出路径的（旧路线为空） |
| `asset_weapon_view_model_agrees / zero / differs` | 两槽位三态比较：相等 / view 未写(=0) / **真冲突**（两个探针 demo 上必须为 0） |
| `worldModelRequests` / `armsWeaponRefs` / `armsNonWeaponRefs` / `worldModelInstances` | 渲染请求层：走世界路线的请求 / **仍指手臂的武器（必须为 0）** / 无世界索引的手臂穿戴物（`CTFViewModel`，属 ViewModel 轮次） / 实例层同口径 |

### 11.3 读数（固定构建，`evidence/weapon-world-model/`）

```
POV    refs=675 requests=387 known=71 resolved=69 zero=2 fromWorld=69 onlyWorld=0
       worldIndexNonZero=69 armsWeapon=0 armsNonWeapon=6 viewAgrees=69 viewZero=2 viewDiffers=0
       worldModelRequests=69 worldModelInstances=25
bagel  refs=679 requests=505 known=33 resolved=32 zero=1 fromWorld=32 onlyWorld=0
       worldIndexNonZero=32 armsWeapon=0 armsNonWeapon=12 viewAgrees=33 viewZero=0 viewDiffers=0
       worldModelRequests=32 worldModelInstances=0
snakewater  asset_model_path_known 451 -> 454（+3），onlyWorld=3
```

snakewater 的 +3：实体 382 CTFKnife / 428 CTFMinigun / 429 CTFLunchBox
**根本没有 `m_nModelIndex` 属性**（`present=--w`），世界路线是这三条路径的唯一来源 ——
这正是 claim 3b 的恒等式 `delta(asset_model_path_known) == asset_model_path_world_model_only`。

逐实体样例（bagel `--dump-weapon-models`）：

```
weapon entity=822 class=292 CTFRocketLauncher present=mvw modelIndex=1204 viewModelIndex=1204
       worldModelIndex=363 path=models/weapons/c_models/c_rocketlauncher/c_rocketlauncher.mdl source=world
```

**oracle 见证**（同实体同包，值级）：`ent-oracle 1` 的 Enter 列表取最后一条武器 Enter
→ 锚点 `demo tick 52427 entity 822 CTFRocketLauncher` → 该实体块内
`idx= 242 DT_BaseCombatWeapon.m_iWorldModelIndex = Integer(363)`；
本侧发送表同槽位 `[242] … bits=13`；`modelprecache[363]` 即上表路径。

**可证伪的 SKIP**：该属性走**实例基线**，只在武器 Enter 包出现；两个 demo 的实时窗口
（bagel 129210..129277、POV 55265..55394）逐包扫描均**零写入**，值级逐 tick 对照不可得。
脚本把这个前提本身断言下来（`packets checked=68 that carried m_iWorldModelIndex=0`，
窗口上下界从 `--history-stats` 与 oracle survey 推导，不硬编码）——前提变了门禁就变红，
而不是安静地「什么都没比」。

### 11.4 判据与变异

- `weapon-world-model-check.sh`（新，验收链第 12 步）：fixture（10 个合成实体逐分支构造，
  含一个故意「武器仍指手臂」的实体，证明该计数器能燃）→ bagel/POV 端到端恒等式与钉死值
  → oracle 见证 → 可证伪 SKIP。
- `mutate.sh m10`：把世界路线改成不可达（缺陷原样），断言 8 条读数变红，含 fixture 自检
  **拒绝**（rc≠0，读数塌成 `known=0 … armsWeapon=2`）与门禁自身 `WEAPON-WORLD-MODEL=FAIL`。
- `check-probe-output-additive.sh` 新增 claim 3（变更前冻结的 9 行 `asset_refs=`，
  剥掉新键后逐字节一致）与 claim 3b（唯一移动是 snakewater 451→454，且等于
  `asset_model_path_world_model_only`）；`added-lines.txt` 经全部门禁后刷新为 18 行。

### 11.5 未验证 / 已知限制

- **画面仍无人工确认**：本节证明路径正确（实体 → 索引 → 表 → mdl → 渲染请求），
  不证明屏幕上画对了。
- `worldModelInstances` 在 bagel 是 0、POV 是 25：实例层建在末态 + 256 上限之上，
  bagel 实时窗口只有 1 个检查点（POV 有 2 个）。此读数**未入门禁**，仅记录。
- `viewDiffers` 在 POV/bagel 为 0，在语料其他 demo 非 0（saytext2=8：全部是工程师系
  shotgun/builder/PDA×2/wrangler，两槽位在 851/961 间互换）。已核实那些实体世界路线
  仍全部胜出、`armsWeapon=0`；它按**语料形状**记录，不是接线判据。
- `m_nModelIndexOverrides`、观察目标 `m_hObserverTarget`、投射物仍未接（HANDOFF §3.3 其余子项）。

---

## 12. 观察目标 `m_hObserverTarget`（P1 剩余子项，2026-10-07 第三轮，`c0ff710`）

### 12.1 缺陷的形状：管线一处没读，而「跟随」是个陷阱

玩家在看谁，写在**观看者自己的实体**上，而不是被观看者身上：

| 属性 | 发送表槽 | 位宽 | 含义 |
|---|---|---|---|
| `DT_BasePlayer.m_iObserverMode` | 835 | 3 | 0 none / 1 deathcam / 2 freezecam / 3 fixed / 4 in-eye / 5 chase / 6 roaming（`observe_mode.h`） |
| `DT_BasePlayer.m_hObserverTarget` | 836 | 21 | `CBaseHandle`：被观看的实体 |

只有 in-eye 与 chase 把相机挂在目标身上；deathcam / freezecam / fixed / roaming 都不跟。
旧管线两个属性一处未读，聚焦永远落在 view entity 上。

这一条与 §11 的武器、§10 的 z 属于同一形状（**错得能解析成功**）：重放谱系里
view entity 是真实玩家、坐标有限且在图上，只是**可能不是画面正在看的那个人**。

### 12.2 解析（`c0ff710`）

`resolveObserverFocus(viewEntity, statesByIndex)`（`entity_model.{h,cpp}`，渲染器中立）：

| 字段 | 含义 |
|---|---|
| `hasMode / mode` | 读到了 `m_iObserverMode` 及其值 |
| `hasTarget / targetHandle` | 读到了 `m_hObserverTarget` 的**打包**值 |
| `targetIndex / targetSerial` | 按 Source `NUM_ENT_ENTRY_BITS=11` 拆：低 11 位索引、高 10 位序列号 |
| `targetInRange / targetPresent` | 索引在快照内 / 该槽确实有实体（`classId >= 0`） |
| `followsTarget` | `mode ∈ {4,5}` **且** 目标存在 —— 只有这种组合才谈得上「跟」 |

句柄的三种「不是一个能看的位置」分别判定并命名：wire 哨兵 `0xFFFFFFFF`、
字段初始化的 `2047`（`INVALID_EHANDLE_INDEX`）、索引越出快照。
**序列号只解码不校验**：`EntityState` 不带序列号，槽被新实体复用时句柄仍会解析到该槽 ——
这是写明的能力边界，不是绕过。

探针新增 `--observer-focus-at`（服务器 tick 域，逐 tick 打印每个 mode≠0 的实体）
与 `--camera-at`（demo tick 域，打印重放循环会拿到的 `DemoViewSample`），
并在 observer 行上打印 `resolved=<tick>`：归档答案是「查询 tick 之前最近的检查点」，
**陈旧度从此是读数而不是推断**。

### 12.3 读数（固定构建，`evidence/observer-focus/`）

fixture（11 个合成实体，逐分支构造）：

```
observer-focus-fixture cases=11 withMode=9 modeNonZero=8 hasTarget=8 inRange=5
                      present=4 missing=1 outOfRange=3 follows=2 serial=867
```

其中实体 6 是**拿着合法且存在的句柄的 deathcam**：它证明 `follows` 这个计数器
能燃（若把 deathcam 也当跟随，这里会读 3），而不是只会读 0。
`missing=1` 是「槽在快照内但没有实体」，`outOfRange=3` 是索引越界那三种写法。

POV（录像者实体 18，`--entity 18`，五档查询；`resolved` 为实际取数的检查点）：

| 查询 tick | resolved | mode | 目标 | follows | 说明 |
|---|---|---|---|---|---|
| 51900 | 51742 | 0 | idx20 | 0 | 两段死亡之间的存活期 |
| 53400 | 53075 | 0 | idx20 | 0 | 同上（不同检查点） |
| 53976 | 53976 | **4** | idx3 / serial 867 | **1** | in-eye，唯一 `follows=1` 的一档 |
| 54747 | 54747 | 0 | idx3 | 0 | 已复活 |
| 55394 | 55394 | 0 | idx3 | 0 | 实时窗口，tick 精确 |

死亡包（demo tick 33242，server tick 49562）之后第一个检查点是 49589：
`mode=1 / handle=606217 / idx=9 / serial=296`，`follows=0`。

bagel（server tick 129277，实时窗口，tick 精确）：实体 1 `mode=4`、目标 `idx3 / serial 839`、
`follows=1`，且 **`selfOrigin == targetOrigin`**（两者坐标逐位相同）。

### 12.4 为什么不接线：三条独立读数

> ⚠️ **本节是第三轮的当时记录，其中第 2 条的 2729 已被后续两轮改写**：
> §14（2026-10-08，选槽新鲜度）把它降到 45，§15（同日，归因）证明 45 是**一台被冻结的
> 死亡相机**对着**同一具身体**，并证明这一份 demo 里观察目标**从来没有指向过另一个玩家**。
> 也就是说本节三条读数给出的「不接线」结论**仍然成立，但依据不再成立**，请连读 §14、§15。

原计划是把聚焦链接到目标上（`followsTarget` 时聚焦目标实体）。实测把它否掉了：

1. **录制的相机贴在录像者自己身上。** demo tick 37677（oracle 包列表把它与
   server tick 54000 配对）的 `dem_cmdinfo` 相机在
   `-1112.031250,505.593719,459.031250`，与本解码器在检查点 53976 报出的
   实体 18 自身 origin `-1112.019531,461.310516,455.251282` 相差 **0.012**（x）；
   它的俯仰角 `13.764709` 与实体 18 自己的 `m_angEyeAngles[0]` **逐位相同**。
   `dem_cmdinfo` 由录制客户端自己写出，不受本解码器的信念影响 —— 这是独立见证。
2. **管线优先槽给目标解出的坐标离那台相机 2729 单位。**
   实体 3 的 `DT_TFLocalPlayerExclusive.m_vecOrigin` 读作
   `1511.459961,894.130005,-186.000000`（x 方向就差 2623）。
3. **那个槽自己承认过期。** 实体 3 的 `m_nTickBase=51597`，比检查点 53976 落后
   **2379 tick**；它的另一个槽（`NonLocalPlayerExclusive`）读
   `-1112.000000,461.250000,455.250000`，离相机只有 44 单位。

即：按声明接线会把这一份 demo 的画面搬走 2729 单位，而且搬到一个过期两千多 tick 的
坐标上。**本轮的交付因此是「解析 + 仪表 + 账本」，不是「搬相机」**：
`renderer` 的聚焦链一个字未改（`git diff cd36db1..c0ff710 -- native/src/main.cpp` 为空）。

顺带回答了「为什么上一轮的排序规则没被挑战」：在 bagel 上
`LocalPlayerExclusive` 就是真值、分歧为 0（12.3 末行），规则看上去完全正确；
在 POV 实体 3 上它是过期值。**缺的不是换一个槽，而是带新鲜度意识的选槽** ——
可用的新鲜度读数至少有 `m_nTickBase`（实体自报）。留给下一轮，本轮不修。

### 12.5 判据与变异

- `observer-focus-check.sh`（新，验收链第 13 步；本轮 39 条断言，§15 之后为 46 条）：
  ① fixture 逐分支钉死（含 deathcam 反例）；
  ② POV 五档（含 `resolved` 序列 `51742 53075 53976 54747 55394`、句柄三分解）；
  ③ 分歧读数：相机俯仰逐位等于录像者正视俯仰、`camera-to-target distance = 2729`、
     实体 3 `m_nTickBase=51597`、另一槽 `44` 单位；
  ④ oracle 见证：33242 包 `mode=1 / handle=606217` ↔ 本侧 `idx=9 / serial=296`。
- `--mutation`：把 oracle 期望 mode 与 2729 各挪 1，要求**都**变红
  （`MUTATION-CAUGHT=PASS`，两条 `FAIL` 行）。门禁的变异写在门禁内、不落 `mutate.sh`：
  这一轮没有「把缺陷改回源码」的对象（相机没被改），能失败的是**比较本身**。
  §15 把它扩成**三个**扰动（期望 mode、钉住的分歧、普查到的 follow 检查点数），
  并要求**恰好三条**红行 —— 只要求「至少一条」会让「因为别的原因变红」冒充「被抓住了」。
- bagel 侧**不做**相机对照并写明原因：它的 `dem_cmdinfo` 序列在 demo tick ~69881 就停了，
  而实体历史跑到 server tick 129277 —— 那里根本没有可比的录制相机。

### 12.6 未验证 / 已知限制

- **画面仍无人工确认**：本节证明「属性解得对、与独立实现一致、跟着走会搬错多少」，
  不证明屏幕上画对了。
- **序列号不校验**（12.2）：槽复用时会解析到新实体。fixture 里 serial 分支单独断言，
  语料上无法验证。
- **归档非 tick 精确**：53976 之外的所有 POV 读数都带 `resolved` 与陈旧度；
  in-eye 持续区间（≥437 tick）由两个检查点界定，边界不是逐 tick 的。
- **POV 的相机与目标不一致这件事本身未归因**：是 `dem_cmdinfo` 记录的是本地视角，
  还是该 demo 的 in-eye 目标另有含义（实体 3 的 `m_nTickBase` 早于死亡包，
  且同槽曾带 `DT_LocalPlayerExclusive` 数据，索引复用是已知现象）——
  本轮只把它作为**不接线**的依据记录，未做归因结论。
  → **§15 已归因**：录制的相机原点在 demo 里是**保持**的（原始字节逐位相同），
  冻结在录像者死亡那一刻；目标解出的坐标是同一具身体 12 tick 之后的位置。
  索引复用仍**未**归因（同一具身体为什么占两个槽，§15 明确记为未解决）。

---

## 13. 验收机器自己的两处缺陷（2026-10-08 凌晨，`a1d5e8d` + `0eca5ab`）

§12 的代码提交 `c0ff710` 之后，13 步链第一次跑**红了**——红在它该红的地方，
但原因不在被测代码，而在**验收机器自己**。本轮修的是机器，不是产品。

### 13.1 现象：第一次 13 步链 `VERIFY=FAIL`，红在第 9 步

`c0ff710 + 0eca5ab` 上跑完 13 步，末行 `VERIFY=FAIL`（`RC=1`）：
第 1–8 步与第 10–13 步全绿（含新第 13 步
`OBSERVER-FOCUS=PASS (assertions_ok=39, camera-to-target=2729u)` 及其变异），
**只有第 9 步**（普查判据可证伪）红了。现场（当轮 `evidence/verify/9-census-negative.txt`）：

```
  extra_reports=23 (corpus grew -> sample moved)
  victim=evidence/corpus-negative/reports/autorecord_2026-05-18_23-53-06.txt
  FAIL victim autorecord_2026-05-18_23-53-06 is NOT in corpus.csv -- the mutations below would be inert
  FAIL skip count did not rise by one: baseline=0 mutated=0 expected=1
```

门禁自己的**前置断言**拦下了「变异会空转」，并把原因写在现场。这是正确行为。

### 13.2 根因：输入集是「机器的属性」

`census-negative-test.sh` 原本调用 `corpus-census.py --sample 24` 对**活的**
Steam 语料目录（`D:/SteamLibrary/steamapps/common/Team Fortress 2/tf/demos`）做
等距抽样。实现是 `demos = sorted(glob("*.dem"))` 后按 `step = N/24` 取 24 个下标；
**语料每增删一份文件，全部 24 个落点都会移位**。实测：

| 读数 | 值 |
|---|---|
| 语料文件数（当轮） | 1656（含当晚新录的 `autorecord_2026-10-07_21-42-26` / `_22-11-16`） |
| 冻结校准报告数 | 24（`evidence/corpus-calib/`，2026-10-06 存） |
| 两者交集（当轮抽样 ∩ 冻结报告） | **1** |

于是脚本挑中的 victim（冻结集里第一份 `autorecord_*`）不在当轮 `corpus.csv` 里，
**10 个变异全部会改一份没人读的报告** —— 变异跑绿也只是因为分母里没有它。

这与 §2.5 是同一类缺陷的第二次发作：那一次是**断言写成常数**（`skipped=1`），
这一次是**输入来自活目录**。共同点：判据依赖了一个会自己变化的量。

### 13.3 修订一：输入集钉成仓库文件（`a1d5e8d`）

- `corpus-census.py` 新增 `--demos-list <file>`：输入集由文件逐行给名，
  **不再 glob、不再抽样**；与 `--sample/--limit` 同用是 FATAL（"钉住的名单再被截断"
  不是钉住），名单里不存在的 demo 是 FATAL，运行行打印 `pinned=<file>`
  让证据自带来源。
- `evidence/corpus-calib/demos.txt`：24 行，恰为冻结校准报告的 24 个 demo 名。
- `census-negative-test.sh`：改用 `--demos-list`；新增断言
  「名单 ↔ 冻结报告同集（计数 + 逐一）」与「每个钉住的 demo 仍在语料里」；
  victim 的 `corpus.csv` 成员断言保留（现在结构性成立，但仍然检查）。
- `verify-all.sh` 第 9 步：断言 `pinned_reports=24` ——
  `CENSUS-NEGATIVE=PASS` 也是「零变异」会打印的东西。

单步复核（`CENSUS-NEGATIVE=PASS`，2026-10-08 01:02）：
`reports=24 digest=cd96d99803b3de0d2c4b80b4c56ed4ca2cbbc4faf7cdf850bf61300ac96cf2c5`、
`OK pinned list and frozen reports agree: 24 demos`、`OK victim ... is in the scanned set`、
10 个变异逐个 `verdict went red`、`mutation I` 为 `counted as skipped (0 -> 1)`、
`restore: digest unchanged`。

### 13.4 修订二：把「清空目录」从删除改成移出（同提交）

同一处顺手量到一台机器成本：**本沙箱的批量删除约 5 秒/文件**。

| 操作 | 实测 |
|---|---|
| `rm -f` 24 个报告文件 | **2m01.8s** |
| 同目录下 census 判据本体一次完整运行 | **0.57s** |
| 改 `mv` 隔离后，第 9 步整体 | **20s（稳态）/ 55s（需移走 23 份滞留报告）** |

脚本原来每轮 `rm -f "$NEG"/reports/*` 再重拷。现改为把**不在冻结集里的文件**
`mv` 到 `.scratch/corpus-negative-strays/`（gitignore 内，不进证据），
只有在出现滞留文件时才付出 rename 的代价；稳态为零。
这是纯粹的成本修订，**不改变任何断言**。

### 13.5 修订三：链条标号（`0eca5ab`）

第 1–12 步仍打印 `N/12`、第 13 步打印 `13/13`（上一轮加步骤时漏改），
统一为 `N/13`；文件头 "twelve reports" 改 "thirteen"、Cost 段补第 13 步。
显示字符串，断言/工作计数未动；`grep -l '1/12\|13/13' evidence/verify/*` 为空
（证据文件从不含标号）。为遵守"证据必须来自最终提交的字节"，
第一次跑在普查阶段即作废，改完重跑。

### 13.6 与「红线」的关系

第 9 步这次是**外部环境变化触发的前置条件红**，与 §10/§11/§12 的缺陷红不同：
被测代码一个字节没动（`git diff c0ff710..HEAD -- native/` 为空）。
两处修订都在验收机器侧：一处让输入可复现，一处让成本从分钟回到秒。
留下的一条通用判据：**任何"抽样活目录"的判据，都要先问"这份输入是可复现的吗"**。

### 13.7 修订后的重跑：`VERIFY=PASS`（`a1d5e8d`，32m30s）

在已提交的干净树上（`git status --porcelain -- native/` 为空）重跑完整 13 步链，
末行 `VERIFY=PASS`、`RC=0`。逐步判定（`evidence/verify/1..13-*.txt`）：

```
BUILD=PASS
CENSUS=PASS (9/9 demos at zero)
COVERAGE=PASS (9/9 demos, every observed message type decoded)
FIXTURE=PASS (58/58)
ORACLE=PASS
ORACLE-CORPUS=PASS / ORACLE-CORPUS-SELFTEST=PASS
RECORDING-TYPES=PASS
PROBE-ADDITIVE=PASS
MUTATION=PASS
CENSUS-NEGATIVE=PASS (pinned_reports=24)          <- §13.1 的那一步，已转绿
TRAJECTORY-ORACLE=PASS (compared=40, mutation caught)
HISTORY-COVERAGE=PASS (bagel worst gap 1180 <= 2 x floor 761)
WEAPON-WORLD-MODEL=PASS (fixture_ok=12, live_window_packets=68)
OBSERVER-FOCUS=PASS (assertions_ok=39, camera-to-target=2729u)
OBSERVER-FOCUS-MUTATION=PASS (perturbations caught, red_lines=2)
VERIFY=PASS
```

对比两次跑，**唯一变化的就是第 9 步**（`CENSUS-NEGATIVE=FAIL` → `PASS (pinned_reports=24)`），
其余 12 步判定行逐字相同 —— 这与 §13.6 的判断一致：修的是机器，不是产品。

**第 9 步的现场读数**（`evidence/verify/9-census-negative.txt`）：
`reports=24 digest=cd96d99803b3de0d2c4b80b4c56ed4ca2cbbc4faf7cdf850bf61300ac96cf2c5`、
`clean=24 skipped=0 dirty=0 sum_packets=1361392`、`extra_reports=0 (pinned list: expected 0)`、
victim `autorecord_2026-05-18_23-53-06` 在扫描集内、10 个变异逐个变红、
`mutation I` 为 `counted as skipped (0 -> 1)`、`restore: digest unchanged`。

### 13.8 本节自己的教训

第一次跑出的那份文档草稿（README/HANDOFF 的"`VERIFY=PASS` 已在 `c0ff710` + `0eca5ab`
上取得"）是**在链条跑完之前写的**，而那次实际是 `VERIFY=FAIL`。这不只是笔误：
`VERIFY=PASS` 是一个**由运行产生的事实**，不能先写结论再等运行。
订正后的表述把两次跑都写出来（FAIL 在前、PASS 在后、各自的提交与耗时），
并保留"只有第 9 步变化"这个可复核的对照。

### 13.9 同类缺陷的第三次发作：第 5 步（2026-10-09，`cfd6637`）

第 9 步修完后，同一类缺陷**仍在第 5 步潜伏**，并在 2026-10-09 的
17 步复核跑里第一次现形。现场（当轮 `evidence/verify/5-oracle-corpus.txt` 的工作区版本，
未提交）：

```
  ok        autorecord_2026-07-23_23-02-11.dem   1.3MB  0.3s  POV  pkts 1288/1288 ...
  ok        autorecord_2026-08-08_21-09-19.dem  64.6MB 47.6s  POV  pkts 134014/134014 ...
```

而**已提交**的同一文件（`HEAD:evidence/verify/5-oracle-corpus.txt`）比的是：

```
  ok        autorecord_2026-07-23_22-47-25.dem  27.2MB 21.8s  POV  pkts 56753/56753 ...
  ok        autorecord_2026-08-08_21-02-00.dem  12.9MB  7.4s  POV  pkts 27262/27262 ...
```

**八份里没有一份名字重合**。原因与 §13.2 逐字相同：`oracle-corpus-check.py`
用 `sorted(glob("*.dem"))` 对活的 Steam 目录等距抽样，**语料从 1656 涨到 1658**
（当晚又落了新录像），全部下标移位。这一步之所以一直绿，是因为它**只比较计数**，
不比对冻结报告 —— 所以「输入集移位」在这里的表现不是变红，而是**产出一份
无法复现的读数**。第 9 步因为要拿当轮 `corpus.csv` 去对冻结报告，才把这件事
变成了红。**同一根因，一个表现为假绿，一个表现为真红。**

**修法（`cfd6637`，不动 `native/`）**：`oracle-corpus-check.py` 新增
`--demos-list`，读 `evidence/corpus-calib/demos.txt`（与第 9 步同一份冻结名单），
校验名单里每份 demo 都存在，然后**在这份固定universe 内**再抽样 —— 因此
`--quick`（8）与全跑（40，被 24 名上限截到 24）**都变成可复现**。
`verify-all.sh` 第 5 步传入该名单，并断言**实际比较数 == 期望数**（不再只信
`ORACLE-CORPUS=PASS` 这个字符串），使该步无法在零份 demo 上空过、也无法悄悄
退回 glob 活目录。

**留档的通用判据**（已并入 SKILL 的"活语料与沙箱成本"段）：
> 判据的**绿**与判据的**可复现**是两件事。一个抽样活目录的步骤可以在语料
> 移位后**继续报绿**，同时把一份只属于当轮的读数写成证据。凡是"抽样活目录"
> 的判据，都要问一句"这份输入是可复现的吗"，并把答案写进证据本身
> （`pinned=<file>`），而不是写进心里。

## 14. 实体属性选槽的新鲜度（P1 剩余子项「观察目标相机接线」的前置，
## 2026-10-08，`d61b3d6` + `66cd97f`）

整合审查把「观察目标相机接线」列为仍未解决的第 1 项，理由写的是
**目标槽位的新鲜度仍未解决**。本节处理的就是那半项：不是接线，是**选槽**。

### 14.1 缺陷的形状：同一个量在两个槽里，只有一个还在更新

玩家的 `m_vecOrigin` 在实体上出现**两次**：

| 槽 | 表 | 收件人 | 精度 |
|---|---|---|---|
| `DT_TFLocalPlayerExclusive.m_vecOrigin` | `DT_TFLocalPlayerExclusive` | 玩家自己的客户端 | 全精度 |
| `DT_TFNonLocalPlayerExclusive.m_vecOrigin` | `DT_TFNonLocalPlayerExclusive` | 其它所有客户端 | 量化 |

`preferCandidate` 的规则是 rank（Local 0 → 其它 1 → NonLocal 2）再字典序。这条规则
**确定**（那是 `6cb0ecf` 的修复），但确定不等于对：**两个槽的新鲜度可以不同**，
而 rank 规则看不见这件事。实测两份 demo，同一条规则，相反的结局：

| demo | Local 最后写入 | NonLocal 最后写入 | rank 选中 | 对不对 |
|---|---|---|---|---|
| bagel 实体 1 @129277 | 129277（age 0） | 56148（age 73129） | Local | **对** |
| POV 实体 3 @53976 | 51596（**此后再未更新**） | 53976（每检查点重写） | Local | **错** |

POV 上被选中的那个槽的值**冻在** `1511.459961,894.130005`，而 demo 录下的相机在
`-1112.03,505.59,459.03` —— **2729 单位**（第 13 步原本钉的就是这个数）。该槽的
`m_nTickBase` 是 51597，比它被回答的检查点 53976 落后 **2379 tick**；同一实体的
NonLocal 槽只离那台相机 **44 单位**。

**为什么所有计数门禁都是绿的**：候选数、实体数、包数、`compared=` 全都不变 ——
两个槽都在，选中哪个是「值从哪个属性读」的问题，不是「读到几个」的问题。
和第 10 步修的 z 缺陷是同一个形状（解码对了，读错了属性）。

### 14.2 第一步：先只记录，不改规则（`d61b3d6`）

按 `NEXT-round-slot-freshness-recon.md` 的收尾要求，规则与门禁放在量完之后**单独一步**。
所以第一步只加仪表：

- `EntityPropertyValue` 新增 `std::int32_t lastWriteTick = -1`（server tick 域）。
  `-1` 表示**不是包写入的**（fixture 造的、以及默认值）。
- 盖章点在 `readEntityPropUpdates`（`demo_header.cpp`）**唯一**的写入处，且在
  `change.value = value` 之前 —— 历史记录与状态拿到同一个 tick，重放出来的值不会显得更新。
  四个调用点（`preserve` / `baseline` / `enter` / `temp`）**都传 `packetTick`**，所以一处盖章覆盖全部。
- `rankPropertyCandidates()` 暴露**规则自己的排序**（复用同一个 `suffixMatch` +
  `preferCandidate`），避免探针另写一份会漂移的实现。
- 探针新增 `--prop-candidates-at <ticks>` / `--candidate-suffix <S>`，逐候选打印
  `rank / name / type / x,y,z / lastWrite / age`，末尾一行
  `prop-candidates-summary ... chosenStale= worstChosenAge=`。

**这一步零行为变化**，并且是**实测**的：`bash check-probe-output-additive.sh` →
`PROBE-OUTPUT-ADDITIVE=PASS`、`compared=9/9` 全部 `IDENTICAL`、`added-lines=IDENTICAL
(18 lines frozen)`。既有读数逐字节未移动。

### 14.3 第二步：规则改为「取后来写的那个」（`66cd97f`）

```
bool preferCandidate(name, lastWriteTick, bestName, bestTick) {
  if (lastWriteTick != bestTick) return lastWriteTick > bestTick;   // 新鲜度：决定
  ... rank 再字典序 ...                                             // 平局：回落
}
```

**平局回落是这条规则不引入新的不确定性的全部理由**：没有包 tick 的状态（fixture、
任何在 `readEntityPropUpdates` 之外造出来的状态，`lastWriteTick` 全是 `-1`）以及两槽同
tick 的状态，排序**与这条规则存在之前逐位相同**。fixture 把这两个分支都断言了
（`rankTiePrefersLocal=2`），所以「回落还在」是读数而不是说法。

### 14.4 读数（固定构建）

POV 实体 3，三次查询 53976 / 54747 / 55394（`evidence/slot-freshness/pov.txt`）：

```
chosenStale 3 -> 0
rank=0 (chosen) DT_TFNonLocalPlayerExclusive.m_vecOrigin  lastWrite=55394 age=0
rank=1          DT_TFLocalPlayerExclusive.m_vecOrigin     lastWrite=51596 age=3798   <- 仍冻在 51596
worstChosenAge 3798 -> 124
```

`124` **不是缺陷**：54747 那次查询里，包流能提供的最新候选就是 54623 写的那个，
`124` 是「可得的最新值有多旧」，不是「选错了」。

bagel 实体 1 @129277（`evidence/slot-freshness/bagel.txt`）：`chosenStale` **仍是 0**，
选中的**仍是 Local**（lastWrite 129277），被它拒掉的 NonLocal 仍是 age 73129。
这是**对照组**：一个「一律偏好 NonLocal」的规则会通过 POV 那一半、在这一半失败。

第 13 步（`evidence/observer-focus/`）：相机到目标的距离 **2729 → 45**，
并且现在**逐轴打印**：`per-axis camera-minus-target (x,y,z): -0.031,44.344,3.781`。
45 不是均匀摊在三个轴上的 —— 几乎全在 y 轴上，这是读数，不该被一句话带过。
（§15 接着把这 45 归了因：不是误差，是**一台被冻结的死亡相机**对着**同一具身体**。）

### 14.5 判据与变异

**`slot-freshness-check.sh`**（第 14 步，20 条断言，三节）：

1. **fixture**（4 个形状，每个分支按构造存在）：
   `shapes=4 freshChosen=2 staleChosen=0 freshnessDecided=1 rankTiePrefersLocal=2 apiMatches=4 tickRoundTrip=4`。
   其中两条是**防止门禁变成死门禁**的：
   - `freshnessDecided=1`：只有「Local 旧 / NonLocal 新」这一个形状是 rank 单独答不出来的。
     若读成 0，说明新鲜度这一项成了死代码，而**其它计数全都还是绿的**。
   - `apiMatches=4`：fixture 同时走两条独立路径 —— `rankPropertyCandidates`（规则的排序）
     和 `EntityModelResolver::extractTransform`（**渲染器真正调用的公开 API**），
     断言两者答案一致。少了这条，排序可以悄悄偏离实际渲染。
2. **POV 实体 3**：`chosenStale=0`、`withTick=6`、选中槽名 / lastWrite / age，
   以及被拒掉的那个槽**仍冻在 51596**。
3. **bagel 实体 1**：`chosenStale=0`、选中 Local、被拒的 NonLocal age 73129。

`--mutation` 对两份 demo 的 `chosenStale` 各扰动 +1，**要求恰好 2 条红行**
（只红 1 条说明某个断言没在读自己的那份 demo）。

**`mutate.sh m11`**（源码级变异，补 `--mutation` 证明不了的另一半）：
删掉 `preferCandidate` 里的新鲜度项，重建，然后要求

- `slot-freshness-check.sh` **拒绝**（`SLOT-FRESHNESS=FAIL`，exit=1）；
- POV `chosenStale` 从 0 **回到 3**，且**冻在 51596 的 Local 槽重新被选中**；
- **bagel `chosenStale` 保持 0** —— 这一条是防止门禁被「一律偏好 NonLocal」蒙过去的关键。

实测：`MUTATION-RED` ×3、`MUTATION-HOLD` ×1、`MUTATION-SUITE=PASS`，
且恢复后六份读数与固定构建**逐字节相同**。

### 14.6 未验证 / 已知限制

- **相机仍未接线**，这一轮没有改变这个决定。改变的是**理由**：从「接上去会偏 2729 单位」
  （测量）变成「接上去只偏 45 单位，但这轮不做」（决定）。整合审查原本就是基于陈旧槽
  下的读数做出「不接」的结论，现在那个读数已经不成立了，**这件事应当被重新判断，
  而不是沿用**。本轮只负责把数摆正。
  → **§15 给出了重新判断所需的答案，而答案仍然是「不接」，但换了理由**：
  45 不是「接上去的代价」，是一台冻结的死亡相机；这一份 demo 里观察目标**从不指向别的玩家**，
  所以它**无法**回答「跟随另一个玩家对不对」。依据见 §15。
- **45 单位的来源未归因**。它几乎全在 y 轴（44.344），x 只有 0.031、z 只有 3.781。
  一个可能的解释是查询错位（相机在 demo tick 37677 ↔ server tick 54000，而被回答的检查点是
  53976，相差 24 tick），但**没有证明**：24 tick 的位移应该让三个轴一起动，而这里 x 几乎不动。
  按 `CBeam.m_vecOrigin=232944` 的先例，记为**未归因**，不写成「就是量化误差」。
  → **§15 已归因**，并且**推翻了上面那个「查询错位」的猜测**：错位不是原因，
  原因是**相机原点在 demo 里被保持**（原始 `democmdinfo` 字节逐位相同），
  它等于录像者死亡那一刻自己的 origin。44.344 里 44.283 是身体死亡后自己掉的，
  剩下 ≤0.061/轴是 `DT_TFNonLocalPlayerExclusive` 的 1/8 量化。逐轴对照见 §15.3。
- **`baseline` 调用点的时间戳是近似**：`enter` 时套用实例基线用的是**当前包的 tick**
  （`demo_header.cpp:1309`），不是基线被记录时的 tick。所以「只从基线来的属性」会显得比实际新。
  这对 Local/NonLocal 的**比较是公平的**（两槽的基线属性拿同一个戳），但它是一条真实近似。
  实体离场再进场会重新套基线并重新盖章，同样对两槽对称。
- **「写得晚 ⇒ 值更新」是本轮的假设，不是定理**。它在两份 demo 上成立（bagel：Local 新且
  就是真值；POV：NonLocal 新且离录下的相机 44 单位）。上一条恰好指出一个**可能破坏它**的机制：
  只走实例基线的属性会在实体每次重新进场时被重新盖成「新鲜」。若某个重复属性的一槽只走基线、
  另一槽走增量，前者会在重新进场后赢下选槽，而它的值可能来自很久以前的基线。
  本轮**没有构造出**这个场景，也没有门禁盯它 —— 这是一条已识别、未验证的洞。
- **选槽新鲜不等于 tick 精确**。窗口外的答案仍受第 11 步的有界保留约束（bagel worst gap 1180，
  鸽笼下界 761）。本节只保证「在两个都在的槽之间选对」，不保证「答案就是那个 tick 的」。
- **只在两份 demo 上逐值确认过**（bagel SourceTV、POV autorecord 一份）。
  规则改动对其它 POV demo 的影响只由第 2 步的九份普查和既有门禁覆盖，没有逐值对照。
- **`m_nModelIndexOverrides` 未处理**。本轮只把位置/角度的选槽修了。**玩家实体上被复制的
  属性已量过**（`--props-at` 后按「去掉表前缀」分组，见下），`m_nModelIndex` **不在其中**：
  `DT_BaseEntity.m_nModelIndex` 不属于 Local/NonLocal 两张独占表，所以模型索引没有
  「同量两槽」的问题。但玩家实体上**确实**有 `m_nModelIndexOverrides.000..003` 四个槽
  （POV 实体 3 实测），本轮**没有读它**，也没有门禁盯它 —— 这与整合审查列的
  「`m_nModelIndexOverrides` 未处理」一致。
  实测被复制的属性（POV 实体 3 / POV 实体 1 / bagel 实体 1 三处一致）：
  `m_vecOrigin`、`m_vecOrigin[2]`、`m_angEyeAngles[0]`、`m_angEyeAngles[1]`、`m_nWaterLevel`。
  这条规则是通用的（`findProperty` 对任何后缀匹配到多个候选都会走它），所以这五个量现在
  **都**按新鲜度选；但只有 `m_vecOrigin`（含其 z 兄弟）有逐值门禁，其余四个没有。


---

## 15. 45 单位的归因：一台被冻结的死亡相机（P1 剩余子项「观察目标相机接线」的前置，
## 2026-10-08 第六轮，无 `native/` 改动）

§14 把第 13 步的相机到目标距离从 2729 降到 45，并把逐轴拆分打了出来
（`-0.031,44.344,3.781`），但**没有归因**：§14.6 明确写了「45 单位的来源未归因」，
只给了一个未证明的猜测（查询错位 24 tick）。本轮把那 45 拆开，结论是
**它不是误差，也不是「接线的代价」**。本轮**一行 `native/` 代码都没改**。

### 15.1 现象：录制的相机原点在 demo 里是**保持**的，不是逐 tick 的位置

用 `--camera-at` 逐 tick 采样 demo tick 37620..37690（POV
`autorecord_2026-07-02_13-26-46.dem`，map `pl_odyssey`），得到：

- 37620..37639：origin `-1207.415894,528.770142,595.473022`
- **37640..37678：origin `-1112.031250,505.593719,459.031250`（39 个 tick 一个值）**
- 37679..37680：`-1115.727539,347.926453,384.093323`
- 37681..37689：`-1118.161865,339.262421,384.093323`
- 37690：`-1127.099731,313.791687,374.564301`

**同一段时间里角度每 tick 都在变**（俯仰 18.705887 → 13.764709，偏航 -176.304993 →
-119.663826）。「原点不动、角度在动」说明这不是解码器缓存或采样问题。

**直接读原始字节验证**（`democmdinfo_t` 76 字节，紧跟在 `[cmd][tick]` 之后，
`demoProtocol=3` 所以没有 player slot）：

| demo tick | cmdinfo 偏移 | viewOrigin | 前 16 字节 sha1 | flags |
|---|---|---|---|---|
| 37640（第 2 包） | 20992238 | `-1112.031250,505.593719,459.031250` | `ef7a26d97762` | 0 |
| 37650 | 20998090 | 同上 | `ef7a26d97762` | 0 |
| 37655 | 21000734 | 同上 | `ef7a26d97762` | 0 |
| 37677 | 21012619 | 同上 | `ef7a26d97762` | 0 |
| 37679（第 1 包） | 21013134 | 同上 | `ef7a26d97762` | 0 |
| 37679（第 2 包） | 21013622 | `-1115.727539,347.926453,384.093323` | `b3d93d20dbe1` | 0 |

**五个包的 `viewOrigin` 逐字节相同**（角度不同）。所以「保持」是 **demo 自己的属性**，
不是本解码器的行为。任何把「37677 的相机」当成「37677 时刻的位置」再去减另一个 tick 的量，
减出来都不是距离。

### 15.2 那个被保持的值是谁的：录像者**自己**，而录像者此刻**已经死了**

- 该值 `-1112.031250,505.593719,459.031250` 与 **实体 18（录像者，每个包里都带
  `DT_LocalPlayerExclusive`）在 demo tick 37639 / server tick 53968 的
  `DT_TFLocalPlayerExclusive.m_vecOrigin`（x,y）逐位相同**，z 等于它最后一次写下的
  `m_vecOrigin[2]`（demo tick 37633 / server tick 53962，值 `459.03125`）。
- 录像者在这一刻**已经死了**：demo tick 37633 起 `m_fFlags` 出现 `FL_TRANSRAGDOLL`
  （`1073873153` = `0x40020301`，含 `0x40000000`），demo tick 37634 起
  `m_flFallVelocity` 从 `584.88` 掉到 `0`（落地/死亡），`m_lifeState=2`。
- 检查点 53976 上探针自己读出的录像者状态：`m_lifeState=2`、
  `m_fFlags=1073873152`（`FL_TRANSRAGDOLL` 位 = `1073741824`）。这两条现在是第 13 步的断言。

即：**这台相机是死亡相机**（TF2 死亡后相机冻结在死亡位置，视角仍可自由旋转 —— 与
「原点不动、角度在动」逐条吻合）。

### 15.3 45 是怎么来的：逐轴对照

| 量 | x | y | z |
|---|---|---|---|
| 录制相机（demo tick 37677，= 录像者死亡那一刻自己的 origin） | -1112.031250 | 505.593719 | 459.031250 |
| 目标解出的坐标（检查点 53976） | -1112.000000 | 461.250000 | 455.250000 |
| **差（相机 − 目标）** | **-0.031** | **+44.344** | **+3.781** |
| 其中：同一具身体在 53968 → 53980 之间自己掉的 | -0.0118 | 44.2832 | 3.77997 |
| 剩下：`DT_TFNonLocalPlayerExclusive` 的 1/8 量化 | -0.0195 | 0.0605 | 0.0013 |

`sqrt(0.031² + 44.344² + 3.781²) = 44.5 → 45`（门禁按四舍五入取整，与既有读数一致）。

**结论：45 里 44.28 在 y、3.78 在 z，是录像者死后身体自己掉的那段距离；
剩下每轴 ≤0.061 是「别人的位置」这条路径的低精度量化。** 不是坐标解错，
不是查询错位（§14.6 那个猜测被推翻：x 轴几乎不动，正是因为身体几乎沿 y/z 下坠）。

### 15.4 更重要的读数：这一份 demo 里，观察目标**从不指向别的玩家**

把 `--observer-focus-at` 铺满整份 demo（server tick 1000..55000，步长 100，
**541 次查询**，其中 387 次落在 demo 自己的窗口内）：

- `follows=1` 出现在 **13 个不同的检查点**上：
  `18620 25682 26135 30618 43105 43581 46482 46953 50057 50521 53539 53976 54416`
- 其中 **11 个**，目标解出的坐标**就落在录像者自己解出的坐标上（≤0.15 单位）**；
  两者读自**同一个快照**，不是跨时刻比较。
- 剩下 2 个（`25682` 目标 idx=6、`30618` 目标 idx=11）差 `37.812` / `9.426` 单位 ——
  这两个是**未归因**的，见 15.6。

换句话说：这一份 POV demo 的观察对（`m_iObserverMode` + `m_hObserverTarget`）
在它说「跟随」的时候，**指的基本上是录像者自己**（死亡相机看自己）。它**没有**一个检查点
在说「跟随另一个玩家」。所以：

> **第 13 步原来那个 45 从来不是「接线的代价」。它是一台冻结的死亡相机对着同一具身体。
> 而「跟随另一个玩家对不对」这个问题，这一份 demo 结构上就回答不了 —— 它里面没有这种情形。**

整合审查给出的「只合解析与诊断、不接相机」因此**结论不变**，但依据换了：
不再是「接上去会偏 2729 / 45」，而是「这一份 demo 无法验证接线」。
`native/` 仍然一个字未改。

### 15.5 判据与变异

`observer-focus-check.sh` 第 3 节从 39 条断言扩到 **46 条**（`OBSERVER-FOCUS=PASS
(assertions_ok=46)`），新增 7 条：

1. 相机原点在 37640/37650/37677 三个采样上**只有一个值**，且**等于本节做减法的那个值**
   （保持性从「读了一次文件」变成门禁自己每次重读）。
2. 检查点 53976 上录像者 `m_lifeState=2` 且 `FL_TRANSRAGDOLL` 位已置。
3. 541 点普查：不同 follow 检查点 `=13`、其中目标落在录像者自己身上 `=11`、
   最差同快照差距 `=37.812`。

`--mutation` 从两个扰动扩到**三个**（期望 mode、钉住的分歧、普查到的 follow 检查点数），
并且要求**恰好 3 条红行**（原来是「至少 1 条」）。实测：

```
FAIL the pinned camera-to-target distance = 46, expected 45
FAIL distinct follow checkpoints in the census = 14, expected 13
FAIL witness mode = 1, expected 2
MUTATION-CAUGHT=PASS (3/3 perturbations went red)
```

第 13 步的总耗时从约 1 分 10 秒涨到约 2 分（普查 541 次查询约 19 s，
外加两次 `--props-at`）。验收链其余步骤不变。

**整条链重跑（由运行产生）**：`bash verify-all.sh --quick` 在第六轮的提交 `290db52` 上重跑，
**`VERIFY=PASS`**（RC=0，**36m20s**，15 步全绿，读数 `evidence/verify/1..15-*.txt`）。
因为本轮 `native/` 一个字节未改，其余 14 步读数与 `18fe7ea` 那次**逐条相同**，
只有第 13 步按新期望。

### 15.6 未验证 / 已知限制

- **索引复用仍未归因**：同一具身体为什么同时占实体 3 和实体 18 两个槽（两槽位置在
  6 个采样点上相差 ≤1/8 单位，且此刻附近 1000 单位内没有第三个玩家），本轮**没有结论**。
  同理，2 个离群 follow 检查点（`37.812` / `9.426` 单位）**是另一个玩家、还是同一具身体的
  更粗快照、还是过期目标**，本轮**不判定**。普查数的是「观察对要什么」，不是「那是谁」。
- **「保持」的机制未归因**：本轮证明了 demo 里 origin 逐字节重复，**没有**解释引擎为什么
  这样写（可能是死亡相机，可能是 `WriteCmdInfo` 的更新条件）。结论只用到「它是保持的」。
- **只在一份 POV demo 上做过**：整份 demo（39122 demo tick / 21.8 MB）逐 tick 采样只在
  37620..37690 这一小段做过；541 点普查覆盖整份，但普查只读观察对，不读相机。
- **仍然没有画面确认**：本节证明的是「相机的原点是什么、目标解到哪里、两者差多少」，
  不证明屏幕上画对了。
- **没有其它 POV demo 可对照**：本机只有这一份 POV 的 `dem_cmdinfo` 序列足够长；
  bagel 的相机序列在 demo tick ~69881 就停了。要判定「跟随别的玩家对不对」，
  需要一份**录像者在活着的时候 in-eye 别人**的 demo —— 而按 TF2 的语义，
  活着的玩家 `m_iObserverMode` 恒为 0，所以这种 demo 只可能是**旁观者**录的，
  不是玩家 `autorecord`。这一条是本轮给出的、下一轮真正需要的东西。

## 16. 帧抓取：让「画面」第一次变成可复核的产物（P1 剩余子项「实体模型实际渲染」的前置，

## 2026-10-08 第七轮，`3d03719`）

### 16.1 结论

**验收链第 1–15 步全是计数与解码值，没有一条能说画面对不对。** 这不是「少了一条断言」，
而是**结构性**的：一个错的画面可以是一个格式完好的画面。最切近的先例就在上一轮 ——
`cd36db1` 修的「武器画成手臂」缺陷里，`m_nModelIndex` 指向 `c_*_arms`，
**解析成功、模型取到、绘制调用成功**，屏幕上多出一双放在武器位置的手，
而所有计数门禁保持绿色。没有任何计数能看见它。

本轮交的是**仪表，不是结论**：让运行中的程序能把「它刚合成的这一帧」交回来，
并且这个产物可以被机器直接检验。**本轮刻意不判断画面对不对**（那需要人眼或参照图），
只把「画面变了没有」从一句无法证伪的话变成**可哈希的字节**。

### 16.2 抓取点：`Present()` 之前，且只有一次

```cpp
// native/src/native_renderer.cpp（draw() 末尾，Present 之前）
if (!capturePath_.empty()) {
  const std::wstring path = capturePath_;
  capturePath_.clear();
  captureSucceeded_ = writeBackBufferToFile(path);
}
const HRESULT result = swapChain_->Present(settings_.vsync ? 1 : 0, 0);
```

**为什么必须在 `Present()` 之前**：`Present()` 一旦跑完，back buffer 的内容就没有定义了
（它被交给显示子系统），所以 **`Present()` 之前的那一瞬是唯一能读到「刚刚合成的这一帧」的时刻**。
写在 `Present()` 之后读的就是下一帧、或者垃圾。

**为什么是一次性请求**：`requestFrameCapture()` 把路径存下，`draw()` 消费后立刻清空；
主程序抓完就直接 `PostQuitMessage()`，所以门禁**不需要猜什么时候杀进程**——
程序自己会走，并且用退出码告诉我们成了没有。

### 16.3 抓取失败绝不能被当成设备失败

`Renderer::lastError_` 是主循环设备恢复路径的**输入**：

```cpp
// native/src/main.cpp（主循环）
if (FAILED(result) && (result == DXGI_ERROR_DEVICE_REMOVED || result == DXGI_ERROR_DEVICE_RESET)) {
  ...reloadGpuResources()...        // 退出码 14
} else if (FAILED(result)) {
  PostQuitMessage(3);               // 设备错误
}
```

如果抓帧的失败沿着 `lastError_` 走，一个**诊断**失败会**装成设备失败**，
把主循环推进恢复路径。所以抓取错误走**独立通道**：

- `Renderer::captureError_`（`std::wstring`）—— 分开的字符串，不碰 `lastError_`；
- `Renderer::lastFrameCaptureSucceeded()` / `lastFrameCaptureError()` —— 分开的读取口；
- 退出码 **16** = 「帧抓取失败」（13 已被参数/设备错误占用，14 `reloadGpuResources` 失败，
  15 `--metrics-file` 打不开，124 `timeout` 杀）。

`writeBackBufferToFile()` 的注释把这条写死了：

```cpp
// This deliberately does NOT touch lastError_: a capture failure is a
// diagnostic failure, not a device failure, and must not send the main loop
// down its device-removed recovery path.
```

### 16.4 产物：手写未压缩 24 位 BMP

**故意做得「笨」**：不引编码器、不做色彩管理、不带元数据。选它的唯一理由是
**门禁能直接哈希与度量它**，而不是它好看。实现要点：

- 从 swap chain `GetBuffer(0)` 取 back buffer，建 `D3D11_USAGE_STAGING` +
  `CPU_ACCESS_READ` 的纹理，`CopyResource`，`Map(D3D11_MAP_READ)`；
- 自动识别 `DXGI_FORMAT_B8G8R8A8_UNORM`（swap chain 默认）与 `R8G8B8A8_UNORM`，
  按需交换 R/B；
- BMP 行序**自底向上**（源行 `height-1-y`），行尾按 4 字节对齐补 padding；
- 手写 54 字节头：`BM` / `fileBytes` / `dataOffset=54` / `dibSize=40` / `width` / `height` /
  `planes=1` / `bitsPerPixel=24` / `compression=0` / `imageBytes`；
- 用 **`_wfopen_s` 而不是 `_wfopen`**：主目标 `tf2_demo_native` 额外要 `/W4 /permissive-`，
  `_wfopen` 会触发 C4996 弃用警告，而链条第 1 步**钉住了 `warnings=1`**（只有已有的
  `main.cpp` C4457），多一个警告会让链条变红。

### 16.5 门禁：`frame-capture-check.sh`（验收链第 16 步）

断言四件事 —— 正是**后续任何「画面变了」结论的前提**：

| # | 断言 | 为什么它是前提 |
|---|---|---|
| 1 | **文件是 BMP 且头部与自身字节数自洽** | `fileBytes == 54 + stride*height == 磁盘实字节`、`bpp=24`、`dataOffset=54`、`DIB=40`、`compression=0`。用一个字段去核另一个字段，**不需要外部工具**，也不预设分辨率。 |
| 2 | **同一静态场景抓两次逐字节相同** | 抓帧**不确定**的话，后面任何「这两帧不同」都无法解释——可能只是抖动。没这条，第 3 条就是空的。 |
| 3 | **demo 场景与暂停场景逐字节不同** | 若两者相同，说明抓的不是合成结果（或 demo 没生效），「画面」根本测不到。 |
| 4 | **非法 `--capture-tick` 退出 13 而不是静默抓第 0 帧** | 门禁若请求第 N 帧却拿到第 0 帧还判 PASS，就是**测了错的东西还说对了**。 |

**实测读数**（`evidence/verify/16-frame-capture.txt`）：

```
paused capture rc=0 size=2582406
fileBytes=2582406 1264x681 bpp=24 dataOffset=54 dibSize=40 imageBytes=2582352 planes=1 compression=0 actual=2582406
distinct colours (capped at 9) = 9
paused sha256 #1 = ab0bc11e541831513f4c7971caeaca777d8b7aa2dc2da7a623d77a290c37155e
paused sha256 #2 = ab0bc11e541831513f4c7971caeaca777d8b7aa2dc2da7a623d77a290c37155e
demo sha256    = fa1bb19484f466ce5f20eeaa25bdb4b1dd90e3cee72aaf02af36100b95ff7543
invalid --capture-tick rc=13
FRAME-CAPTURE=PASS
```

- **确定性成立**：两次暂停抓帧 sha256 完全相同（`ab0bc11e…`）；
- **场景敏感成立**：demo 帧 `fa1bb194…` ≠ 暂停帧 —— 抓的是真画面；
- **拒猜成立**：非法 tick → rc=13 且**不写文件**；
- 头自洽 9 项全过（`1264×681`，`2582352 = 1264*3*681`，`54+2582352 = 2582406 = 磁盘`）；
- 共 **16 条断言**，链条第 16 步**钉住这个数字**（理由同第 8/14/15 步：
  一个「断言悄悄消失但照样打印 PASS」的门禁不是门禁）。

> **⚠️ 记账：第一次跑链条时第 16 步红了，是我把断言数数错了。**
> `verify-all.sh` 第一版写的是 `-eq 15`，实际是 **16** 条
> （2 个退出码 + 9 个头自洽 + 1 非纯色 + 1 正尺寸 + 1 确定性 + 1 场景敏感 + 2 拒绝）。
> 原始输出因此长这样：
>
> ```
> FRAME-CAPTURE=PASS                      <- 门禁自己说通过
> FRAME-CAPTURE=FAIL (assertions_ok=16)   <- 链条期望 15
> VERIFY=FAIL
> ```
>
> **门禁是绿的，红的是验收机器的期望值。** 这与 `3ca75ae` 那次同类
> （新增变异用例把 `red/hold` 从 `37/3` 抬到 `40/4`，而第 8 步期望写死）——
> **验收机器按设计抓住了作者自己的改动**，这是它该做的事，不是 bug。
> 修正期望后重跑转绿。第 16 步的注释里现在写明了这 16 条是怎么数出来的。

**整条 16 步链在 `e43a3f3` 上重跑并转绿**（`bash verify-all.sh --quick`）：

```
FRAME-CAPTURE=PASS (assertions_ok=16)
FRAME-CAPTURE-MUTATION=PASS (a one-byte change moves the content hash)
VERIFY=PASS
```

RC=0，**38m27s**，16 步全绿。逐步读数见 `evidence/verify/1..16-*.txt`，
日志（gitignore）`.scratch/verify-run-16step-pass.log`。前 15 步的读数与上一轮 15 步链
在实质数字上一致（构建 `errors=0 warnings=1 exe_count=21`、普查 9/9、fixture 58/58、
`oracle-corpus agreed=8 mismatched=0`、`mutation red=40 hold=4`、`census-negative 24`、
`trajectory compared=40 mismatches=0`、`history 1180 ≤ 2×761`、`weapon fixture_ok=12`、
`observer 46`、`slot 24 chosenStale=0`、`source-tv 9 份 917543 包 47 断言`）。

### 16.6 变异：内容检查必须先能红

`--mutation` 翻转图像**第一个像素的一个字节**，要求内容哈希察觉：

```
MUTATION-CAUGHT=PASS (a one-byte change moves the content hash)
```

这条**不是形式**：第 3 条的「两帧不同」全靠哈希比较，一个分辨不出一个字节的哈希比较
等于没比较。所以变异直接打在**后续画面结论将要依赖的那一层**。

### 16.7 ⚠️ 记账：我在本轮犯的错 —— 测试在错误的音频设备上出声

门禁最初的三次抓帧调用**都没带 `--audio-device`**，于是落到 `WAVE_MAPPER`
（**系统默认设备**），用户当面听到并制止。我当时的错误假设是「`--start-paused`
既然是静默启动，就不会开音频」—— **错的**：音频设备在**启动阶段**就初始化，
暂停只停播放、不停设备打开；带 demo 那次更是直接走默认设备。

**修法**：`frame-capture-check.sh` 里**每条启动路径**都显式加 `--audio-device 6`
（`耳机 (xduoo audio)`），并把这条写进脚本注释与 `~/.workbuddy-ai/MEMORY.md`，
理由是「一个在错误设备上出声的门禁，是会被人们关掉的门禁」。

这也顺带确认了一件事：**主程序在本环境确实能出声**，`--audio-device` 的合法范围是
`[0, waveOutGetNumDevs())`（`main.cpp:478`），越界或不带值 → 退出码 13。

### 16.8 本轮的边界（写清楚，不伪装）

- **不判断画面对不对**：本轮只证明「帧抓得到、产物可检验、产物对场景敏感」，
  **没有**证明屏幕上画的是 TF2 该有的样子。**这一条仍然是 P1 未闭环的主缺口。**
- **抓的是暂停/首帧场景**：门禁与实测都用 `--start-paused` 或 `--capture-tick 0`。
  `--capture-tick N` 的能力已实现并可测（带 demo 时主循环约在 17.5 s 开始、
  约 275 tick/s，见 16.9），但门禁**尚未**用「推进到第 N 帧」这条路径做断言 ——
  因为那会让第 16 步额外多花几十秒，而它现在要证的是**仪器**而不是**时点选择**。
  ⚠️ **2026-10-08 修正**：`--capture-tick N>0` 的**前提是不要 `--start-paused`**，
  因为 tick 推进的条件是 `main.cpp:1245` 的
  `if (g_playback.enabled && !g_playback.paused)`；`--start-paused` 下 tick 恒为 0，
  `N>0` 永不触发（只能靠按键步进 `main.cpp:331/334`）。别处若照本节原话
  理解为「暂停也能推进到第 N 帧」，那是错的，见 §17.5。
- **实体模型是否在帧里**未单独确认：暂停场景走的是全屏四边形回退 + UI 叠加层
  （`native_renderer.cpp` 的 `Draw(6, 0)` 路径）。带 demo 的帧里**有没有实体模型**，
  本轮**没有**判定 —— 这正好是下一步（接材质）要解决的。
  ⚠️ **2026-10-08 续**：这一条在 §17 里查了，结论是**第 16 步当时那个 demo 的
  「demo 帧」是回退四边形贴的喷漆图，根本不是场景**；已加第二个有安装地图的 demo
  与「色彩多样性 < 1000」判据修掉。

### 16.9 实测：带 demo 时的启动与推进速度（为后续定超时用）

```
# timeout 150 tf2_demo_native.exe --tf-root ... --demo <bagel> --metrics-file m4.csv
elapsed_seconds,rendered_frames,fps,tick,working_set_bytes,private_bytes
17.4991,0,0,16,1453477888,1520701440      <- 主循环从这里开始
...
130.905,10788,116.967,30940,1451282432,1515896832
```

- **主循环约在 `elapsed≈17.5 s` 开始**（前 17.5 s 是 VPK 索引 104041 条 + demo 加载）；
- 之后 tick 从 16 推进到 **30940**，用时约 113 s → **约 275 tick/s**，渲染 **~116 FPS**；
- 因此 `--capture-tick 0` **第一帧即命中**（`g_playback.tick` 从 0 起步），
  门禁给无 demo **120 s**、带 demo **300 s** 超时，都是宽裕的（实测带 demo 约 20 s 就抓到）。
- `rc=124` 是 `timeout` 杀死进程，**不是缺陷**；启动慢也不是缺陷。

## 17. 资源可达性：第 16 步的假绿，与「demo 场景到底能不能加载」（2026-10-08）

> 起因：准备开工「实体模型接材质」前，按 16.8 留下的硬前提「带 demo 跑主程序时
> BSP 真的加载成功吗」，去抓一帧带 demo 的帧看里面有没有模型区域。
> 结果抓到的帧**不是场景**，是一张**动漫立绘**。由此发现第 16 步通过的理由是错的。

### 17.1 结论（三条，都可机械复核）

1. **第 16 步「demo 帧 ≠ 暂停帧」是假绿。** 门禁唯一命名的 demo 是
   `koth_bagel_rc13`，而**这张图在本机没有安装**（`maps/koth_bagel_rc13.bsp`
   既不在磁盘上、也不在任何能打开的 VPK 里）。BSP 加载失败 →
   `worldBoundsValid_` 为假 → 渲染器走 **6 顶点全屏四边形回退**
   （`native_renderer.cpp:1313-1320` 的 `Draw(6, 0)`），`t0` 绑的是
   `worldTexture_ ?: texture_`，而 `texture_` 因为地图材质缺失回落到
   **`materials/vgui/logos/spray.vmt`（一张喷漆贴图）**。所以那一帧是
   「把喷漆贴图铺满屏幕」，「与暂停帧不同」成立，但**与 demo 无关**。
2. **`pak01_dir.vpk` 在本机不存在。** `main.cpp:647` 硬编码五个档案名并以
   `pak01_dir.vpk` 开头，用 `if (archive->open(...))` **静默跳过**打不开的：
   没有报错、没有日志、没有计数。实测打开 **4/5**，缺的正是 `pak01_dir.vpk`。
   它带走了 `materials/maps/*.vmt`（全部地图专属材质）。
3. **但「世界材质」是可用的，所以「接材质」在当前环境可验证。**
   `cp_snakewater_final1`（本机磁盘上有 BSP）的 148 个材质里
   **111 个能解析成可解码的像素**（VMT→VTF→RGBA 全通）。真正的瓶颈不是
   `pak01`，是 **`main.cpp:775` 的 512×512 上限**：这 111 个里只有 **7 个**
   过得了上限（**104 个是 1024×1024 或更大**），这 7 个只覆盖
   **30731/200000 = 15.4%** 的三角形 —— 所以画面仍是**几乎全灰**。

### 17.2 读数（`resource_reachability_probe`，本机）

```
# 有安装地图的 demo（cp_snakewater_final1）
archivesOpened=4 archivesExpected=5 missingNames="pak01_dir.vpk" totalEntries=149686
bspSource=loose bspBytes=59590420 bspTriangles=200000
distinctMaterials=148 materialsWithVmt=111 materialsWithDecodableVtf=111
atlasEligible=7 oversizedRejected=104 largestAccepted=512 smallestRejected=1024
totalTriangles=200000 trianglesCoveredByAtlas=30731
vmtBytes=0            <- 地图自己的材质（materials/maps/cp_snakewater_final1.vmt）不存在

# 无安装地图的 demo（koth_bagel_rc13，第 16 步用的那个）
bspBytes=0 bspSource=""    <- BSP 根本取不到
```

三张帧的**色彩多样性**把「场景」和「回退四边形」分开（同一套抓帧代码）：

| 帧 | distinct colours | 判读 |
|---|---|---|
| `paused.bmp`（无 demo） | **69** | 世界几何剪影（灰） |
| `demo-installed-map.bmp`（snakewater） | **162** | 有 BSP 的世界，平涂无贴图 |
| `demo.bmp`（bagel） | **139121** | **喷漆立绘铺满屏幕 = 回退四边形** |

### 17.3 修法：两处改动（都是加法）

- **新增仪表** `native/tools/resource_reachability_probe.cpp`：不建窗口、不载 demo，
  直接回答「主程序命名的那五个档案谁打开了」「这张地图的 BSP 取不取得到」
  「它的材质有多少能解到像素」「多少能过 512 上限、覆盖多少三角形」。
  配套 `vpk_query.cpp` 用于查 `list(prefix, ext)`。验收链新增**第 17 步**
  `resource-reachability-check.sh`（9 条断言 + 变异：把期望的档案数改成 5，必须变红）。
- **扩接第 16 步**：加入第二个 demo（`cp_snakewater_final1`，地图有装），
  断言两件事 —— ①它也 ≠ 暂停帧；②它的 distinct colours **< 1000**
  （回退四边形是照片，世界是平涂灰）。**这正是能抓住喷漆假绿的那条判据。**
  第 16 步断言数 16 → **18**（`verify-all.sh` 的 pin 已同步）。

### 17.4 ⚠️ 记账：我在这轮的第一判断是错的

我最初把 `archivesOpened=4` 读成「材质全没了」，并据此写下「接材质在本环境不可验证」。
**错在两点**：① 我先用 `list("concrete")` 查询，返回 0，就以为世界贴图不存在 ——
实际 `VpkArchive::list` 是**路径前缀**匹配，必须写 `list("materials/concrete")`
才命中 253 条；② 我把「地图专属材质缺失」当成了「所有材质缺失」。
把 `atlasEligible/oversizedRejected` 这两个读数加进探针之后，真实瓶颈
（512 上限 vs 1024 贴图）才显出来。**教训：探针给出的 0 要先怀疑探针的查询姿势，
再怀疑资源本身。**（与 16.7 同类，都记在这里。）

**第二次错（同一轮，更该记）**：第 17 步第一次进链时，`verify-all.sh` 把断言数
pin 成 **9**，而门禁实际只打印 **8** 行 `OK` —— `VERIFY=FAIL`，红在第 17 步。
**与第 16 步第一次跑是同一类错：作者数错了自己的断言数，被验收机器抓住。**
处理不是把 pin 降到 8，而是补上**真正缺的那条**：原先所有读数都关于
「一张能用的地图」，缺的是**负面对照** —— 旧门禁那个 demo 的地图
（`koth_bagel_rc13`）必须 `bspBytes=0`。补上后正好 9 条，
且这条断言在**将来该地图被装上时会主动变红**（那时「第 16 步的 demo 没有场景」
这个结论就不再成立，读者需要被告知，而不是留一条过期的注记）。

**第三次错（更早）**：变异套件第一次进链时 `MUTATION=FAIL`，原因是
`mutate.sh:35` 的 `git diff --quiet -- native/` 守卫 —— 我新增的两个探针文件
**还没提交**，套件拒绝在脏树上跑（它要 `git checkout` 回退变异，会毁掉未提交的改动）。
**这次是仪器的保护逻辑正确、我的流程错了**：先提交 `native/`，再跑链。
记在这里是因为它容易被误读成「变异套件坏了」。

### 17.5 本轮的边界（写清楚，不伪装）

- **仍未证明「屏幕上画对了」**。第 17 步只证明「资源取得到、且第 16 步不是假绿」，
  不证明渲染结果正确。16.8 的主缺口仍然开着。
- **相机在 demo 播放中不动**：tick 16/200/500/1000 抓到的帧里，
  200/500/1000 **逐字节相同**（`83db5fdf…`），即 demo 推进 tick 但**视图不跟随**。
  这与第 13 步「观察目标解析已接、跟随刻意未接」一致，但**本轮未判定**这是否是缺陷。
- **`--capture-tick N>0` 需要不暂停播放**（`main.cpp:1245` 的
  `if (g_playback.enabled && !g_playback.paused)`；`--start-paused` 下 tick 恒为 0）。
  16.8 里「`--capture-tick N` 的能力已实现并可测」的表述**据此修正**：能力在，
  但**前提是不暂停**。

## 18. 骨骼动画的输入供给：demo 不给玩家播哪段（P1 剩余子项「骨骼动画接线」的**前置**，
## 2026-10-09，`6d3bfba` + `58d7ab7` + `9363001` + `1d6ab37` + `ce3806e` + `7fb9069` + `2bac4fb` + `590de4f`）

### 18.1 问题不在渲染，在输入

2026-10-07 的侦察把骨骼动画放在渲染缺口清单第 3 位，写成一个**接线任务**：

> 把 `animation_decoder.cpp` 编进主目标，打开 `uploadBoneMatrices(..., true)`，
> 按 demo tick 推进序列。

这句话有一个未被检验的前提：**demo 会告诉你每个实体在播哪段序列**。本轮先问了这个前提，
因为如果它不成立，接线就是「消费一个永不到来的序列」——循环会跑在解码器留下的默认值上，
而计数门禁会全绿。那种形状本项目已经付过两次代价。

### 18.2 读数：CTFPlayer 四个动画属性全是 0

新增 `entity_model_probe --anim-props`：摊平每个 server class 的发送表，按**叶名**统计

| 属性 | 含义 |
|---|---|
| `m_nSequence` | 播哪段 |
| `m_flCycle` | 播到哪 |
| `m_flPlaybackRate` | 播多快 |
| `m_flPoseParameter` | 姿态参数 |

`CTFPlayer` **单列一行**，无论它有没有这四个 —— 这是「它存在但没有」与「它根本没被找到」
可区分的关键。

2026-10-09 在三份录像上的读数（bagel / POV `autorecord_2026-07-02_13-26-46` / SourceTV `73.dem`），
三份完全一致：

```
animprop-player class=CTFPlayer id=247 sequence=0 cycle=0 rate=0 pose=0
animprop-summary classesWithSequence=197 classesWithCycle=195 classesWithRate=197
                tfPlayerFound=1 tfPlayerSequence=0 tfPlayerCycle=0 tfPlayerRate=0
```

**正对照**：同一份读数里，197 个 class 有 `m_nSequence`、195 有 `m_flCycle`、
197 有 `m_flPlaybackRate`；`CTFWeaponBase` 有 1 槽。一条**只能说不**的规则什么都没测，
所以正对照和否定命题是同一轮里的两个断言。

### 18.3 SDK 依据：TF2 主动剥掉它们

`source-sdk-2013`：

- `src/game/server/baseanimating.cpp:245-246` —— `IMPLEMENT_SERVERCLASS_ST(CBaseAnimating, DT_BaseAnimating)`
  里用 `SendProp.Int(SENDINFO(m_nSequence), ANIMATION_SEQUENCE_BITS, SPROP_UNSIGNED)` 与
  `SendPropFloat(SENDINFO(m_flPlaybackRate), ...)` 声明了它们；
- 同文件 `:222` —— `m_flCycle` 走 `DT_ServerAnimationData` 子表，挂在
  `SendProxy_ClientSideAnimation` 上，注释原文：*"Sendtable for fields we don't want to send
  to clientside animating entities."*
- `src/game/client/c_baseanimating.cpp:1168` —— *"Most entities clear out their sequences when
  they change models on the server, but not all entities network down their m_nSequence
  (like **multiplayer game player entities**)…"*

**「SDK 有声明」≠「运行时在线上」**——这正是本项目的硬规矩。所以声明与实测互为佐证才算闭环，
而实测的答案是：**TF2 的玩家类被剥掉了这四个**（运行时表比 SDK 声明少 5 个槽）。

### 18.4 独立见证

`ent-oracle`（Rust，另一份实现）摊平同一份 demo 的 `CTFPlayer`：

```
oracle: DT_BaseAnimating slots=15 animation-property slots=0
```

且它的 schema 里**有** `CTFPlayer` —— 所以「0」是「表加载了但这四个槽不在」，
不是「类没找到」。两种实现一致，且这个一致本身排除了「我们的摊平器有 bug 把它们丢了」。

### 18.5 判据与它怎么变红

`animation-availability-check.sh`（验收链**第 21 步**，23 条断言）分三节：

1. 三份录像上 `CTFPlayer` 存在且四个属性全 0（5 × 3 = 15）；
2. 同一条规则确实会说是（正对照，5 条）；
3. oracle 独立同意（其 `DT_BaseAnimating` 块非空、动画属性 0 个、schema 含 `CTFPlayer`，3 条）。

**变异 `m13`**（`mutate.sh`，源码级、重编译）：把叶名比较改成永假，
扫描就再也不匹配任何东西。要求：

- 门禁**拒绝**（`ANIMATION-AVAILABILITY=FAIL`，退出码 1）；
- 正对照 `classesWithSequence` 从 **197 塌到 0**；
- `CTFPlayer` 那一行**不动**（`sequence=0 cycle=0 rate=0 pose=0`）——因为没动的是计数，不是查类；
- `tfPlayerFound` **保持 1**——「玩家没有」≠「扫描什么都没看」。

四项全对，`MUTATION-SUITE=PASS`，还原树与修复构建逐字节相同。

### 18.6 结论

**玩家骨骼动画不能从 demo 驱动**，所以 ③ 不是接线任务。`skeleton_skin_probe`（`00b6033`）
已经**证明过矩阵约定**（`skin[i] = animWorld[i] * poseToBone[i]`，行主序；78 骨最大误差
3.05e-05；`--mutation` / `--mutation-offset` 都能红），所以真要接线时**数学不用再猜**——
缺的是**输入**，不是公式。

- **武器/投射物/可穿戴确有 `m_nSequence`**（`CTFWeaponBase` 1 槽），那里接线有目标；
  本轮**没有做**，记为可做的下一步。
- **玩家动画需要另一个输入源**：客户端预测（usercmd + 武器状态）或真实游戏连接。
  那是比「接线」大得多的一件事，且**本机这四个外部输入里解决不了玩家动画本身**
  （需要真实连接）。记为**未验证 / 需要外部环境**。
- **未接线的现状不变**：`animation_decoder.cpp` 仍未编进主目标，`uploadBoneMatrices(..., false)`
  仍是 bind pose。本轮**没有**改渲染行为，方向从「待接线」修正为「输入不在此处」。

### 18.7 本轮自己的错

- **变异污染了产物（最严重的一条，已修）**：`--mutation` 第一版就地改写 `"$OUT/bagel.txt"`，
  而 `$OUT` 默认就是**已提交的证据目录** —— 于是 `class=CTFPlayer_MUTATED` 被当成读数
  **提交进了仓库**。这正是本项目 2026-10-06 栽过的那一类：**变异忘了回退，污染了产物，
  之后取的每个读数都作废**。修法是结构性的：变异输出改到 `"$OUT/mutation/"`，
  **在位置上就不可能落到干净证据里**。污染文件已重新生成，两个 `mut.*` 残留已删除。
  **触发这件事的是「真的把链跑了一遍」，不是读代码。**
- **`exe_count` pin 过期（同样是跑链才发现的）**：第 1 步 pin `exe_count=23`，而树实际构建
  **24** 个 exe —— `skeleton_skin_probe` 在 `00b6033` 加进来时**没有同步改 pin**，
  于是那个窗口里**每一次**跑链第 1 步都报 `BUILD=FAIL`。已改成 24 并写明原因。
  （`main.cpp` 那一条 `C4457` 是**既有**警告：本轮之前的 `1-build.txt` 里同一条警告、
  只是行号不同；它**故意不作断言**。）
- `--anim-props` 第一版用 `name.find("Player")` 判玩家，于是把
  `CPlayerDestructionDispenser`（一个建筑）和 `CBasePlayer`（SDK 基类，其表不是 TF2 的线上格式）
  一起扫进来，打印 `playerSequence=32`；而**真正决定设计的 `CTFPlayer` 因为四个计数全 0
  被 `continue` 跳过，根本没出现在列表里**。这是仪器里的假阳性 —— 改成精确比名字，
  且无论有没有都单列一行。**「列表里没有」不能和「有但为 0」混淆**。
- 门禁第一版抓 `pose=` 用了和前三项一样的 `' pose=\([0-9]*\) .*'` 模式，
  但 `pose` 是**行尾**没有尾随空格，抽出空值 → 门禁**正确拒绝**而不是跳过。
  这正是把四个计数**分开断言**而不是一个整体的原因。
- `m13` 的两条 `must_hold` 第一版把整行当断言，而 `mutate.sh` 的 `value()` 按**第一个 `=`**
  切割，于是拿 `0 cycle` 去比 `sequence=0 cycle`，把**已经保持住的**读数报成 `MUTATION-BROKE`。
  改用 `must_appear` 做字面匹配 —— 那也正是这条断言本来的意思。
- 步数注释第一版写 22，实际是 23。**被 pin 的是门禁里的数，不是散文里的算术**，
  已按门禁改正。

### 18.8 可复核产物

| 路径 | 内容 |
|---|---|
| `evidence/animation-availability/{bagel,pov,sourcetv}.txt` | 三份录像的 `--anim-props` 全文与 JSON（**干净读数**，变异不再写这里） |
| `evidence/animation-availability/oracle-ctfplayer.txt` | oracle 的 `CTFPlayer` 扁平表 |
| `evidence/animation-availability/oracle-all-classes.txt` | oracle 的 schema（证明含 `CTFPlayer`） |
| `evidence/mutation/animavail-m13/bagel.txt` | 变异构建下的读数（正对照塌到 0，玩家行不动） |
| `evidence/mutation/gate.m13.txt` | 变异下门禁的完整输出（`ANIMATION-AVAILABILITY=FAIL`） |
| `evidence/verify/21-animation-availability.txt` | 链上第 21 步的门禁全文（含 23 条断言行） |

### 18.9 提交清单

| 提交 | 内容 |
|---|---|
| `00b6033` | `skeleton_skin_probe`：矩阵约定（上一轮已提交，本轮结论引用它） |
| `6d3bfba` | `--anim-props` + `animation-availability-check.sh`（**含污染**：变异写进了证据目录） |
| `58d7ab7` / `9363001` / `1d6ab37` | `mutate.sh` **m13** 及其两条断言修正 |
| `ce3806e` | `verify-all.sh` 第 21 步 + pin 23 断言 |
| `7fb9069` / `2bac4fb` | README / HANDOFF / 本文件 §18 |
| `590de4f` | **修污染**（变异输出改到 `$OUT/mutation/`）+ **修 `exe_count` pin 23→24** |

