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
| 1 构建 | `errors=0 warnings=1 exe_count=21`（既有 `main.cpp(1284) C4457`） |
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
