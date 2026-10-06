# P1 交付验收（第 1 项）：实体模型引用接线

> 本轮只覆盖清单里 P1「基础实体回放画面」的**模型引用**一条：
> 把 `m_nModelIndex` 接到 `modelprecache` 字符串表，让实体真的能解析出模型路径。
> 逐 tick 位移 / 观察目标 / 武器 / 投射物 / team-skin / 截图对比**不在本轮**，见 §8。
> 完整 P0 验收见 `ACCEPTANCE-P0-2026-10-06.md`。

---

## 0. 结论

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

---

## 1. 提交、基线与改动文件

- 提交：`8c6f06e`，测试树 `D:\TF2_Native_Test`，分支 `p0-entity-protocol`
- 父提交：`053de1e`（P0 第三遍复核证据）
- 基线：`d585af8`（`native-mvp-source` 未动）
- 补丁哈希：`git diff 053de1e..8c6f06e -- native/ | sha256sum` =
  `4766d440c19717ecd291cedaeecccd5a9050e5344efe4f4c4aefeae03d5e9724`

| 文件 | 改动 | 内容 |
|---|---|---|
| `native/include/demo_header.h` | +41 | `AssetReference::modelPathFromPrecache`；`DemoNetworkSummary` 的 `modelPrecache` 表与 6 个表级计数、4 个资产解析计数 |
| `native/src/demo_header.cpp` | +385/-149 | 抽出共享 `walkPrecacheTablePayload`；新增 `decodeStringTablePayload`；`readCreateStringTable` 加 `modelprecache` 分支；`buildAssetReferenceList` 按索引解析路径；`scanKnownDemoMessages` 加协议号守卫 |
| `native/tools/entity_protocol_probe.cpp` | +32 | 打印 precache 表读数与资产解析分解；打印前先 `buildAssetReferenceList` |
| `native/tools/entity_model_probe.cpp` | +10/-1 | 扫描前把 `header.networkProtocol` 拷进 `summary` |

---

## 2. 根因（两个缺陷）

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

### 未验证

- **画面没有人工确认。** 本轮读数是「渲染请求被构造出来、模型从 VPK 取到」，
  **不是**「屏幕上画对了」。没有截图对比，也没有和 Source 逐像素比对。
- **逐 tick 实体位移、观察目标、武器、投射物、team/skin 的接线不在本轮。**
  本轮只把「模型引用」这一条接通。`requests>0` 不蕴含这些实体在正确的位置上。
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
- 本机 exe 哈希不可复现（两次强制重链接差 2 字节），复核锚点用补丁哈希 + 读数。

---

## 9. 对旧文档的更正

| 位置 | 原文 | 更正 |
|---|---|---|
| `native/docs/HANDOFF-2026-10-06.md:38` | 「当前 `entity_model_probe` 仍为 `vpkRenderable=0`」 | `vpkRenderable` 只在「硬编码 5 个模型」那条分支里赋值，demo 路径的对应读数是 `demoRenderable`。过时的是 `demoRenderable=0`（现 244 / 147）。`vpkRenderable` 在带 `--tf-root` 时读数为 4，在只跑 `--self-test` 时为 0 —— 这行原文的措辞本身就不精确 |
| `native/docs/TASKS-2026-10-06.md:96` | 同上 | 同上 |
| `native/docs/entity-model-instances-2026-10-05.md:26` | 「`assetRefs=195 requests=0 demoRenderable=0`」，并归因为「采样的 Demo 只有 assetRefs、没有 `m_ModelName` 路径，所以回放只能靠玩家职业兜底模型」 | 读数是真实现的，**归因是错的**：demo 不发路径是设计如此（路径在 `modelprecache` 里），不是数据缺失。`requests=0` 是解析缺陷，现已修复 |
