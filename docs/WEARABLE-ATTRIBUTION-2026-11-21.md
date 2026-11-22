# 录像饰品（wearable）归属与路径还原（task-40，2026-11-21）

前置：门禁卡 `docs/WEARABLE-SCAN-2026-11-21.md`（task-39）证明了三件事答得出来——实体在、占比可统计、owner 100% 可解析——并且给出对 task-40 的四条硬要求（类名允许表、路径来源必须反转、归属靠 handle + 同 tick 校验、不要用 origin/angles 摆位）。本文是这四条要求的实现、实测与限制。

* 开关：`native/tools/demo_open_probe.cpp` 新增 `--cosmetics-table <tick>` 与 `--tf-root <dir>`（`--wearable-scan` 在 :839-850 解析、`--cosmetics-table` 在 :855-866 解析，两条 usage :646-649 / :973-974 同步）。不给出就完全不起作用：`summary.wearableSlotCapture` 是空指针（:1028 声明，:1035 才被赋值），`buildWearableInstanceTable` 根本不被调用（:1081）。
* 源码：`native/include/wearable_instance.h`（461 行）+ `native/src/wearable_instance.cpp`（1024 行）——两个数字是 `wc -l` 在本仓库实测（`wc -l native/include/wearable_instance.h native/src/wearable_instance.cpp`），不是估算。两者都进 `tf2_core`（`native/cmake/tf2-libraries.cmake:65`），探针与产品共用同一份实现。
* 三条 demo 的实测读数见 §2；三条门禁式检查行（归属 / 路径来源 / 判词）每一族都打印「分项之和 = examined」与 PASS/FAIL（`WearableTableCounters::balanced()`，`native/src/wearable_instance.cpp:668`），所以「某一族漏计一行」是读数而不是推断。

## 0. 结论

| 要求 | 判词 | 实测计数 |
|---|---|---|
| 类名允许表（不做子串匹配） | **PASS** | 9 个含 `Wearable` 的服务端类全部进表：3 个 allowed（`CTFWearable` / `CTFWearableVM` / `CTFWearableCampaignItem`）、6 个 excluded。三条 demo 里实际出现的只有这三个 allowed 类；不在表里的类**不进统计**，但会被 `wearable_unlisted` 单独列出（本次三条 demo 均为 0 行）。 |
| 归属靠 handle + generation 纪律 | **PASS** | stv 36/36、bagel 34/34 全部 attributed；POV 17 个实例只有 4 个 attributed、**13 个 `generation-changed` → `broken-attachment`**——这不是缺陷，是 POV 录像里录制者自己的玩家槽位被反复回收的实测，正是「generation 变了就断开、绝不按 index 重新认领」这条纪律在起作用。 |
| 路径来源反转 | **PASS（但份额必须按新口径引用）** | schema 19/36（stv，527‰）、15/34（bagel，441‰）、10/17（POV，588‰）；precache 分别 2 / 6 / 2。**task-39 的 91% 不能当成 schema 的可得份额**：其中一大部分 schema 答案是带 `%s` 的族模板（stv 15/36、bagel 14/34），模板不是文件，修正后这些行落到 precache 或 `no-path`（§5）。 |
| 每帧实例表 + 三族计数 | **PASS** | 三条 demo 每一条都 `BALANCED`；三族之和全部等于 examined（36 / 34 / 17）。 |
| 默认路径一字节不动（加性） | **PASS** | `--scan`、`--scan --camera-track`、产品抓帧三处，父提交 exe 与子提交 exe 在同样输入上 md5 全部相同（§7）。 |

**一句话**：饰品现在有了可复核的归属、路径与判词，且默认路径逐字节不变；但**能真正画出来的只有 21/36（stv）、21/34（bagel）、3/17（POV）**，其余各有具名原因（模板 / 无路径 / 归属断开），不是一个笼统的「不支持」。

## 1. 三条 demo 的读数

| 样本 | 卡面 tick | 实际解析 tick | 陈旧 | examined | attributed | renderable |
|---|---|---|---|---|---|---|
| stv `1338f5de…cp_process_f12.dem` | 49444 | 48845 | 599 | 36 | 36 | 21 |
| bagel `4a9bfb92…koth_bagel_rc13.dem` | 56747 | 56148 | 599 | 34 | 34 | 21 |
| pov `autorecord_2026-09-03_21-05-39.dem` | 60162 | 59563 | 599 | 17 | 4 | 3 |

**陈旧 599 tick 是设计的结果，不是时钟错位**：`queryEntitySnapshotAtOrBeforeTick()`（`native/src/demo_header_entity.cpp:457-520`）只在能沿实体历史把事件重放到**恰好**该 tick 时才返回 `Available`；落在 live 窗口之前则返回最近的一个 `Checkpoint` 并把 `resolvedTick` 带出来（`*resolvedTick = fallback.tick`）。三条 demo 的 live 窗口都只覆盖最后一段，所以三条都走 checkpoint 分支、都陈旧 599 tick。这个数字是 dump 的第 3 列（`wearable_table`），读者不需要自己推。

三族计数（`wearable_attribution` / `wearable_path_source` / `wearable_verdict` 三行的完整形态）：

```
wearable_attribution  49444  36 0 0 0 0 0  36  36  PASS     # attributed,no-owner,empty-slot,not-player,gen-changed,unknown-gen | sum | examined
wearable_path_source  49444  19 2 15  36  36  527  PASS       # schema,precache,none | sum | examined | schema 千分比
wearable_template     49444  15 0 15  19  36  416  0  PASS    # schema 模板,precache 模板,其中 noPath 行 | pathSchema | examined | 千分比 | 模板被画出的行数
wearable_verdict      49444  21 0 0 0 0 0 15  36  36  PASS   # renderable,broken,owner-not-player,owner-missing,owner-gen-unknown,class-not-allowed,no-path | sum | examined
wearable_draw_path    49444  20 1  21  21  PASS                   # world,viewmodel | renderable | renderable
wearable_table        49444  48845  599  armed  snapshot  schema  assets  36  BALANCED
```

bagel：`attribution 34 0 0 0 0 0`、`path_source 15 6 13`（441‰）、`template 14 0 13`（411‰）、`verdict 21 … 13`、`draw_path 19 2`。
pov：`attribution 4 0 0 0 13 0`、`path_source 10 2 5`（588‰）、`template 5 0 1`（294‰）、`verdict 3 13 0 0 0 0 1`、`draw_path 2 1`。

注意 POV 的两个数字不能互相替代：**attributed 4 但 renderable 只有 3**，因为归属成功不等于有可画路径（有一个 attributed 行的路径是 `no-path`）。三族是正交的，各自的 sum 校验都对着同一个 `examined`。

## 2. D1 —— 类名允许表（`native/src/wearable_instance.cpp:145-195`）

表是**名字允许表**，不是 netvar 判定表。理由写在表头上：`DT_TFWearable` 的两个 netvar（`m_bDisguiseWearable` [226]、`m_hWeaponAssociatedWith` [227]）同时还是若干非饰品类的最后两个 flat——`CTFWeaponPDAExpansion_Teleporter`（id 334）、`CTFWeaponSapper`（335）、`CTFProjectile_Arrow`（252）——按 netvar 判会把武器认成饰品。

| 类名 | 判词 | 绘制路径 | 依据 |
|---|---|---|---|
| `CTFWearable` | allowed | world | stv 49444 的 36 个实例里 34 个是它，owner 全是 `CTFPlayer` |
| `CTFWearableVM` | allowed | **viewmodel** | owner 也是 `CTFPlayer`，但 `m_hMoveParent` 指向 `CTFViewModel`；它是饰品、只是画在另一条路径上，所以**准入并分流**，而不是被子串判定过滤掉 |
| `CTFWearableCampaignItem` | allowed | world | 每一项观测量都成立：owner `CTFPlayer`、真 itemDef（5869，cyoa pda）、自带 modelprecache 路径 |
| `CEconWearable` | excluded | — | TF 饰品的基类，且是另一张 226 flat 的表；任何实测 tick 里都没被实例化（它通过派生类到达世界） |
| `CTFWearableDemoShield` / `CTFWearableItem` / `CTFWearableLevelableItem` / `CTFWearableRazorback` / `CTFWearableRobotArm` | excluded | — | 任何实测 tick 里都没被实例化。允许它们等于声明一条在本语料上一次都没跑过的绘制路径 |

`findWearableClassRule()`（:190）做的是**精确相等**（`className == rule.name`），不是子串匹配。不是表里的类返回 `nullptr`，与「表里明确 excluded」是两件不同的事：前者不进统计，后者进统计并计入 `rejectClassNotAllowed`。表外的类若名字里含 `Wearable`，会以 `wearable_unlisted` 单独列出——这条读数的存在是为了让「漏考虑一个类」变成可见的输出，而不是静默跳过。

`CTFWearableVM` 的分流是 task-39 §2.7 那张 19 列表的直接后果：35 行里 33 行 `moveParentIndex == ownerIndex`（父就是 owner 本人），剩下 2 行恰好是两个 `CTFWearableVM`（父槽位 440 / 291 vs owner 槽位 5 / 4）。本次实测再次确认：`CTFWearableVM` 的 `moveParentClassName` 是 `CTFViewModel`，而身体饰品的父是 `CTFPlayer`。

```
wearable_instance  49444 48845 429 1 CTFWearableVM allowed viewmodel attributed 10 CTFPlayer 1 1 1888653 397 CTFViewModel 0 precache - - models/player/items/mvm_loot/demo/fob_h_stickybomb_diamond.mdl 65535 553 - models/.../fob_h_stickybomb_diamond.mdl - no - renderable
```

（这一行同时是 viewmodel 路径的实证：`pathSource=precache`、`schemaKey=-`、itemDef 65535 是无效定义，模型名只存在于 modelprecache。）

## 3. D2 —— 归属：handle + generation 纪律

**读取**：owner 走 `rankPropertyCandidates(state, "m_hOwnerEntity")` 排第一的候选（`native/include/entity_model.h:307-320` 导出的是**规则本身的排序**，因此这里不会出现第二套后缀匹配），子表限定名（`DT_BaseEntity.m_hOwnerEntity`）因此也能命中。

**判定链**（`buildWearableInstanceTable`，`native/src/wearable_instance.cpp:428` 起），按顺序取第一个成立者：

| 状态 | 成立条件 |
|---|---|
| `no-owner` | 没有 `m_hOwnerEntity` 属性，或解码失败 |
| `empty-slot` | handle 有效但目标槽位 `classId < 0`（空槽） |
| `not-player` | 目标槽位有实体但不是 `CTFPlayer`（精确相等，不用子串——子串会把 `CTFPlayerResource` 也认成玩家） |
| `unknown-generation` | 没有 capture（未开 `--cosmetics-table`），或该槽位在解析 tick 之前没有任何代次记录 |
| `generation-changed` | 解析 tick 时的代次 ≠ 录像结束时的代次——**槽位在录制过程中换过主人** |
| `attributed` | 以上都不成立 |

`WearableSlotCapture`（`native/include/wearable_instance.h:99-142`）是所有代次读数的唯一来源。它只在**两个**地方被推进：`native/src/demo_header_entity.cpp:343-345`（Delete，紧跟 :339 的 `++projectileTrackSlotGeneration[...]`）与 `:398-400`（Enter，紧跟 :395 的 `++...` 与 :396 的 `++projectileTrackSeriesStarted`）。`recordWearableSlotGeneration` 的 null 检查就是开关本体——没开 capture 就是零开销，所以默认路径不会因为这条读数多跑一行。

**为什么必须这样，而不是按 index 认领**：POV 60162 的 13 个拒绝行给出了教科书式的例子——

```
wearable_instance  60162 59563 361 1 CTFWearable allowed world generation-changed 2 CTFPlayer 1 1378 1601538 2 CTFPlayer 0 schema model_player - models/weapons/c_models/c_rocketboots_soldier.mdl 133 -2 - - models/weapons/c_models/c_rocketboots_soldier.mdl no yes broken-attachment
```

解析 tick 时 owner 槽 2 的代次是 **1**，到录像结束时已经是 **1378**，而 `m_hOwnerEntity` 的原始 handle 一直是 1601538 没变。POV 录像里录制者自己的玩家实体被反复回收，一顶旧帽子的 owner handle 指向的槽位早就换了人。按 index 重认领会把这顶帽子戴到别人头上——这就是 handover :311「generation 变化即断开」要防的具体错误。

存活下来的 4 行，owner 槽位代次自始至终没变过（例如 ownerIndex 7、generation 1/1）；`CTFWearableVM` 381 就在这 4 行里，说明 viewmodel 路径不只是声明，是被跑过的。

**父指针**（`WearableParent`）用同一套纪律：`m_hMoveParent` 优先、`moveparent` 兜底，记录 `moveParentMatchesOwner`、父类名、以及父槽位的解析 tick 代次与结束代次。两个代次字段与 owner 侧同名同义，读者不必记两套规则。

## 4. D3 —— 路径来源：反转，以及反转之后必须再修一次

**反转本身**：`resolveWearablePath()`（`native/src/wearable_instance.cpp:308` 起）**先算 schema、再算 precache**，与 handover :312 写的顺序相反。依据是 task-39 §1.2 的分布：332 个实例里只有 30 个（9.0%）的 `m_nModelIndex` 落在 modelprecache 之内，其余 302 个的索引值落在范围外（4294967280 / 4294967292 / 4294967294 / 4294967278 这类哨兵）。`model_loader.cpp:1281-1283` 的 `hasModelPath` 短路和 :1329 的「跳过无路径引用」如果照原样喂给饰品，task-41 会是一片空白——这是门禁卡 :241 的原话。

**两条路径都要算，不是赢了才算**：每一行都同时给出 `precacheCandidate` 与 `schemaCandidate`，再用 `routesDisagree` 报告二者是否指向不同文件。只在分出胜负之后再去看败方，会看不见「两条路给出不同答案」这件事本身。

schema 侧的选模型顺序（`claim` 阶梯，逐级 fall through）：
1. owner 的 `m_iClass` 对应的 `model_player_per_class[<class key>]`（class key 用 items_game 的拼写：`scout` / `sniper` / `soldier` / `demoman` / `medic` / `heavy` / `pyro` / `spy` / `engineer`）；
2. `basename` 键（**并注明它不是类键**——它是族模板键）；
3. `model_player`；
4. `model_world`；
5. 都不成立 → `WearablePathSource::None`（判词 `no-path`）。

`ItemSchema::modelCandidates()`（`native/src/item_schema.cpp:197-230`）自己会 BFS 预制链（prefab），且**先写者赢**：item 自己的 per-class 键压过 prefab 的（`emplace` 不覆盖）。所以阶梯只在 `modelCandidates` 给不出的时侯才需要往下走。

### 4.1 反转之后暴露的第二个缺陷：`%s` 族模板

反转把 schema 从「例外」变成「主路」之后，立刻量出一个新问题：**相当一部分 schema 答案是家族模板，不是文件名**。实测带 `%s` 的行：stv 15/36、bagel 15/34、POV 5/17。

`ops_grep` 确认 `items_game.txt` 里共 5574 行含 `%`，但**没有** `%d` / `%i` / `%o` / `%x` / `%u` / `%f` / `%g`，没有 `%%`，没有 `%[A-Z]`——只有 `%s` 一族的族模板，例如：`models/player/items/all_class/ring_%s.mdl`（item 5075，`items_game.txt:221115`）、`models/player/items/%s/%s_ttg_max.mdl`、`models/workshop/player/items/all_class/xms_beard/xms_beard_%s.mdl`、`models/player/items/all_class/oh_xmas_tree_%s.mdl`、`models/player/items/all_class/flair_%s.mdl`、`models/player/items/%s/bit_trippers_%s.mdl`。加载器拿到这种串会去找一个名字里带 `%` 的文件，永远找不到。

**修法**：`hasTemplatePlaceholder()`（`native/src/wearable_instance.cpp`，第二个匿名命名空间顶部）识别占位符；`claim` 在遇到占位符时**不成立**，把值留在 `schemaCandidate` 并置 `schemaTemplate = true`，于是阶梯继续往下走。最终判定：

```
schemaUsable   = schemaCandidate 非空 && !schemaTemplate
precacheUsable = precacheCandidate 非空 && !precacheTemplate
schema 可用 -> Schema；否则 precache 可用 -> Precache；否则 None
```

三点设计决定值得记下：
1. **模板值仍留在 `schemaCandidate` 列**。那一列的含义固定为「schema 回答了什么」，模板是 schema 的真实回答；`schemaTemplate` 这个 flag 才回答「这个回答能不能用」。两件事分成两列，读的人不必去猜什么时侯该忽略这一列。
2. **`routesDisagree` 仍然比较原始候选**。模板与真实路径不一致，本来就是两条路在打架，这个事实不该因为模板不可用而被抹掉。
3. **模板不进三路之和，而是单独计两列** `pathSchemaTemplate` / `pathPrecacheTemplate`。因为一个模板行完全可能从 precache 拿到真路径并计入 `pathPrecache`——它既属于「schema 给了模板」这条读数，也属于「precache 救回来了」那条读数。把它塞进三路之和会让和不再等于 examined。

`bagel` 的 ring 行一行演示了全部：

```
wearable_instance  56747 56148 699 1 CTFWearable allowed world attributed 6 CTFPlayer 1 1 962566 6 CTFPlayer 0 precache model_player_per_class basename models/player/items/all_class/ring_Soldier.mdl 5075 921 - models/player/items/all_class/ring_Soldier.mdl models/player/items/all_class/ring_%s.mdl yes no renderable
```

schema 给的 `ring_%s.mdl` 是模板，precache 给的 `ring_Soldier.mdl` 是真文件，于是 precache 胜、`routesDisagree=yes`、schema 可用列 `no`、判词 `renderable`。

### 4.2 列序（29 个数据列，`wearable_instance` 之后）

tick, resolvedTick, entityIndex, generation, className, allowed|excluded, drawpath, attributionStatus, ownerIndex, ownerClassName, ownerGeneration, ownerGenerationAtEnd, moveParentHandle, moveParentIndex, moveParentClassName, parentAttachment, pathSource, schemaKey, schemaClass, path, itemDef, modelIndex, worldModelIndex, precacheCandidate, schemaCandidate, routesDisagree, **schemaUsable**（`yes` = schema 答案可用 / `no` = 是模板 / `-` = 没有 schema 答案）, verdict。

上面 ring 行的 `yes no renderable`：`routesDisagree=yes`（两路答案不同）、模板列 `no`（schema 答案是模板）、判词 `renderable`。

## 5. D4 —— 每帧实例表与三族计数

`buildWearableInstanceTable(summary, request)`（`native/src/wearable_instance.cpp:428`）做一次查询、产出一张表、三十行计数器。要点：

* **一次查询**：`queryEntitySnapshotAtOrBeforeTick(summary, request.tick, states, &resolvedTick)`——整段 dump 只有这一次查询，所以归属、路径、判词三族来自同一份快照，不可能互相矛盾。这也是 task-39 门禁卡 :5 立下的规矩。
* **三族各自守「和 = examined」**：归属六桶、路径三桶、判词七桶（renderable + 六个拒绝理由）。`WearableTableCounters::balanced()`（`native/src/wearable_instance.cpp:668`）把这三个等式写成代码，dump 里每族打印各自的 PASS/FAIL，末尾再打印整个 `wearable_table` 的 `BALANCED`/`UNBALANCED`。
* **`wearable_table` 行**：tick, resolvedTick, stale, armed, hasSnapshot, schemaLoaded, assetRootValid, rows.size(), balanced——把「表能不能自证」需要的状态一次说完。
* **`armed` 是独立读数**：`table.armed = summary.wearableSlotCapture != nullptr`。一张没开 capture 的表能说出模型名，却证明不了任何归属（代次全部 unknown），所以这两件事必须分开显示。

判词优先级（取第一个成立者）：`class-not-allowed` > `broken-attachment` > `owner-not-player` > `owner-missing` > `owner-generation-unknown` > `no-path` > `renderable`。把类判定放最前面，因为一个不该画的类即使路径再好也不该出现在 renderable 里；归属类拒绝排在路径之前，因为「戴在谁头上」比「找不找得到模型」更根本。

故意**没有** `rejectViewModelPath`：viewmodel 侧饰品是一个**在另一条路径上可解析**的实例，不是损坏。它计入 `drawPathViewModel`，与三路来源之和正交。

## 6. 探针接线与复现

新增两个开关，都关着的时候一行代码都不多跑：

| 位置 | 内容 |
|---|---|
| `native/tools/demo_open_probe.cpp:646-649` / `:973-974` | 两条 usage 同步加上 `[--cosmetics-table <tick>] [--tf-root <dir>]` |
| `:855-866` | 解析：`--cosmetics-table` 走 `std::wcstol`，超出 `[0, INT32_MAX]` 直接拒绝（:861）；`--wearable-scan` 在 :839-850；`--tf-root` 接受一个目录 |
| `:997` | `std::string cosmeticsTableText;`（`wearableDumpText` 在 :994） |
| `:1028` / `:1035` | `WearableSlotCapture wearableSlotCapture;` 与 `if (cosmeticsTick >= 0) summary.wearableSlotCapture = &wearableSlotCapture;` |
| `:1045-1091` | 仅在开关打开时：装 asset root（`--tf-root` 优先，否则 `AssetRoot::findInstalled(steamLibraryCandidates(...))`）、`ItemSchema::load(<tf>/scripts/items/items_game.txt)`、`buildWearableInstanceTable` → `formatWearableTableLines` → 窄字符 `cosmeticsTableText` |
| `:1828-1829` / `:1835-1836` | 写回判定与写出列表各加一处 |

**复现**（demo 路径必须是最后一个参数——探针先解析开关、再把剩下的每个 argv 都当 demo 路径，把开关写在 demo 后面会被当成第二个路径）：

```bash
native/build-nmake/demo_open_probe.exe --scan --cosmetics-table 49444 <stv.dem>
native/build-nmake/demo_open_probe.exe --scan --cosmetics-table 56747 <bagel.dem>
native/build-nmake/demo_open_probe.exe --scan --cosmetics-table 60162 <pov.dem>
# 显式指定 TF 根（本机默认能自动解析到 D:/SteamLibrary/steamapps/common/Team Fortress 2/tf）
native/build-nmake/demo_open_probe.exe --scan --cosmetics-table 49444 --tf-root <tf dir> <stv.dem>
```

本次原始输出在 `.scratch/`（未入库）：`cos2-stv.txt`（15691 B）、`cos2-bagel.txt`（14809 B）、`cos2-pov.txt`（10572 B）。

`--tf-root` 只在 `--cosmetics-table` 打开时有意义；没有 asset root 时表照出，`wearable_assets` 行写 `(no asset root)`、`wearable_schema_error` 写原因，路径来源一律 `none`——降级是可见的读数，不是静默的空表。

## 7. D5 —— 加性证明：默认路径逐字节不变

父提交 `11c675c7` 的 exe 与本次改动的 exe，在同一批 demo 上逐字节对比。父 exe 在**同一台机器**的 detached worktree 里重建（`git worktree add --detach 11c675c7`），因此这是同机 A/B 而不是跨机 pin——门禁卡 :171 已经论证过 A/B 比跨机 pin 更强。

```
stv   --scan                 3276 B  7b0472d168c60aed72ce9a6cee40c583  ==  7b0472d168c60aed72ce9a6cee40c583
stv   --scan --camera-track  3871 B  4579888f779b21f07cec3b739847187e  ==  4579888f779b21f07cec3b739847187e
bagel --scan                 3263 B  3e3ae8931df50cfa160ab25cfafeec35  ==  3e3ae8931df50cfa160ab25cfafeec35
bagel --scan --camera-track  3856 B  685884549b82fef95b19853bdade8962  ==  685884549b82fef95b19853bdade8962
```

四个 md5 与门禁卡 `docs/WEARABLE-SCAN-2026-11-21.md:163-166` 钉的值逐一相同，即 task-39 钉下的基线在 task-40 之后仍然成立。

产品侧（`tf2_demo_native.exe`，pr600 抓帧，配方见 `docs/HANDOVER-2026-11-21.md:69-70`）：

```
tf2_demo_native.exe --demo <stv.dem> --capture-tick 600 --capture-frame out.bmp --audio-disabled --hidden-window
父 2ddade356e85dd77b795a7380c254bcb   子 2ddade356e85dd77b795a7380c254bcb   2582406 B
```

与 handover :177/:270 钉的 `2ddade35…` 一致。**这是本卡最重的加性证据**：`native/src/demo_header.cpp` 与 `native/src/demo_header_entity.cpp` 都被改过（前者加了 capture 指针的保存/恢复，后者加了两次 `recordWearableSlotGeneration` 调用），而产品渲染路径的抓帧结果一字节没动。

构建：`/W4` 零告警，`grep -acE 'warning C|C4[0-9]{3}' .scratch/build-target.log` = 0，`error C` = 0（口径与门禁卡 :169 一致）。这条读数出自 `.scratch/` 的构建日志，**没有**提交进仓库，所以它不是可复核的证据；本仓库里唯一被提交的零告警转录是 `evidence/t105-model-lighting/gates/warn-check.log`，而它是在 task-36 的树上（`cfd01bd1`，142 个 TU，exe `a052de08…`）跑的，不是这张卡的树。合并头上的告警状态由交接卡的最终 `/W4` 构建给出，见 `evidence/BUILDS.md`。

## 8. 已知限制

1. **schema 的「可得份额」不能引用 91%**。task-39 数的是「`m_nModelIndex` 落在 precache 之外的比例」，那个 91% 说明 precache 靠不住；它不等于 schema 能把模型名给出来。修正模板之后能真正拿到文件的是 19/36（527‰）、15/34（441‰）、10/17（588‰）。下游引用必须用后者。
2. **`%s` 的替换没做**。本卡只识别并降级模板，不把 `%s` 替换成类标记（那需要 items_game 的类名拼写规则，且 `basename` 的 `%s` 语义与 per-class 键不一定是同一套）。替换属于 task-41；本文只保证不会把一个模板当成文件交出去。
3. **模板行不等于画不出来**。stv 的 15 个模板行里有一部分靠 precache 救回（路径来源记 `precache`），真正变成 `no-path` 的是 stv 15 / bagel 13 / POV 1。`wearable_template` 行第 3 列就是「其中最终 noPath 的行数」，它与「总模板数」不必相等。
4. **陈旧 599 tick**。三条 demo 的 live 窗口都只覆盖最后一段，所以三条都走 checkpoint 分支。要在 live 窗口内取值，得换覆盖更早 tick 的 demo，或按「demo tick + 首包 server tick」换算——后者本卡没做。
5. **POV 的 13 个 `generation-changed` 是真实数据形态，不是缺陷，也不该被修好**。想减少它只能换 demo（换一份不反复回收玩家实体的录像），不能改判定规则；按 index 重认领会把饰品戴到错误的人头上。
6. **capture 指针必须走保存/恢复**。`WearableSlotCapture` 在探针里是局部对象，`scanKnownDemoMessages` 内部的 `summary = {}` 会清掉指向它的指针，所以 `native/src/demo_header.cpp:249/255` 必须显式保存并恢复。这个坑本次实际踩到：不恢复的表现是**全部行 `generation-unknown`、而三族之和仍然 PASS**，看起来像「这份录像没有数据」，是一个会自我证明为正常的假阴性。后续任何往 `DemoNetworkSummary` 加指针型 capture 的改动都要走同一条保存/恢复。
7. **`CTFWearableCampaignItem` 只在一个 tick 上出现过 1 次**（stv 49444，itemDef 5869）。它的 allowed 判词有实测支撑，但样本很薄，不能当「已验证的常见形态」。
8. **产品渲染一行没改**。本卡只出读数与判定；`native/src/model_loader.cpp:1281-1283` 的 `hasModelPath` 短路与 :1329 的「跳过无路径引用」仍按原样工作。按门禁卡 :241 为饰品开独立通路是 task-41 的工作，这也正是产品抓帧 md5 不变的原因。

## 9. 参考与锚点

* item schema 数值来自本机游戏安装 `D:/SteamLibrary/steamapps/common/Team Fortress 2/tf/scripts/items/items_game.txt`（游戏资产，不进仓库）。
* 属性选择规则只有一处：`native/include/entity_model.h:307-320` 的 `rankPropertyCandidates`；本体实现在 `native/src/entity_model.cpp:190-204`。本文的读数全部经由它，没有第二套后缀匹配。
* 实体快照解析：`native/src/demo_header_entity.cpp:457-520`（`Checkpoint` 分支带出 `resolvedTick`）。
* 槽位代次的唯一推进点：`native/src/demo_header_entity.cpp:339`（Delete 自己那一行 `++projectileTrackSlotGeneration[...]`，记录在 :343-345）与 `:395-396`（Enter 的 `++projectileTrackSlotGeneration[...]` 与 `++projectileTrackSeriesStarted`，记录在 :398-400）；`:410-424` 的 removed-entity 收尾循环**不推进代次**——这是既有行为的非对称，本卡没有改它，但记录在此，因为任何按「代次变化 = 生命周期结束」做推导的读者都应知道它。
* 类名依据：`evidence/animation-availability/oracle-all-classes.txt`（9 个含 `Wearable` 的服务端类）。
* 前置门禁卡：`docs/WEARABLE-SCAN-2026-11-21.md`。构建与抓帧规矩：`docs/HANDOVER-2026-11-21.md:69-80`。
