# P0 交付验收：SourceTV PacketEntities 状态重建

交付目录：`D:\TF2_Native_Test`（隔离测试树，独立 git 仓库）
源目录：`D:\TF2_Demo_Player`（**本次未改动**，见 §7）
日期：2026-10-06

> **后记（P1 之后）**：本文件记录的是 P0 交付当时的读数，保留原样。
> 之后 `8c6f06e`（P1 实体模型引用）改了探针输出与两处判据强度：
> `verify-all.sh` 8/9 步改为数断言条数（25 red / 3 hold / 0 green / 0 broke / 6 restored），
> `check-probe-output-additive.sh` 多了一条「被剥离的行本身也有冻结基线」的主张，
> `mutate.sh` 的 m2 补丁模式随三份近似拷贝的合并而更新。
> 因此本文件 §4.8 引用的全链输出**已过时**，最新全链输出见
> `ACCEPTANCE-P1-2026-10-06.md` §7。P0 的计数器读数本身未移动（同节已逐值复核）。

---

## 0. 本轮修正（第二遍复核，2026-10-06 晚）

第一遍验收有三处结论是错的。用户提供
`D:\SteamLibrary\steamapps\common\Team Fortress 2\tf\demos`（1643 份 / 40.2 GB）
之后暴露出来。**这三条是我自己的判断失误，不是环境问题**，原样记录：

### 0.1 「本机 9 份 demo 全部是 POV」是错的 —— 5 份是 SourceTV

第一遍用探针的 `recording=` 判定，而该字段来自 `parseDemoHeader`，
它只查 `servername`。但 Valve `src/public/demofile/demoformat.h` 写得很清楚：

```c
char servername[ MAX_OSPATH ];  // Name of server
char clientname[ MAX_OSPATH ];  // Name of client who recorded the game
```

SourceTV 录制由服务端的 SourceTV 客户端写出，**录制者名字落在 `clientname`**。
本机 9 份 oracle 语料的真实字段：

| demo | servername | clientname | 实际类型 | 流内 `m_bIsHLTV` |
|---|---|---|---|---|
| bagel | `na.serveme.tf #633053` | `SourceTV Demo` | **SourceTV** | 1 |
| snakewater | `Matcha Bookable` | `SourceTV Demo` | **SourceTV** | 1 |
| ashville73 | `Matcha Bookable` | `SourceTV Demo` | **SourceTV** | 1 |
| comp | `Spire Server` | `SourceTV Demo` | **SourceTV** | 1 |
| saytext2 | `GNDS RGL Highlander Match` | `SourceTV Demo` | **SourceTV** | 1 |
| decal | `sigafoo36.game.nfoservers.com: Tomat△` | `Tomat△` | POV | 0 |
| protocol23 | `95.156.230.156:27085` | `[GC]Kimo [DK]` | POV | 0 |
| short2024 | `localhost:27015` | `Icewind \| demos.tf` | POV | 0 |
| small | `localhost:27015` | `Icewind \| demos.tf` | POV | 0 |

**推论**：§8 里「真实 SourceTV 语料缺失」这一项从来就不成立。
`ORACLE-GATE=PASS` 的 9 份里本来就有 5 份 SourceTV —— 包括 bagel，
也就是整个 P0 缺陷最初被发现的那份 demo。清单要求的
「SourceTV delta/base 语义」一直有 SourceTV 侧证据，只是被错误地标成了 POV。

修复：判定收敛到 `namesIndicateSourceTv()`（`demo_header.cpp` 匿名命名空间），
`parseDemoHeader` 与 `classifyDemoRecording` 共用；`servername` 与 `clientname`
任一含 `sourcetv`/`hltv`（大小写不敏感）即判 SourceTV。
回归用例写在 `presentation_probe.cpp::checkRecording()`，取的就是上表里的
两对真实字段（`Matcha Bookable` + `SourceTV Demo` 必须判 SourceTV；
`169.254.129.46:10120` + `Icewind | demos.tf` 必须判 POV）。
新增 `oracle-recording-types.sh` 把 9 份的判定钉成可 diff 的读数。

### 0.2 普查判据恒为通过（`CORPUS-CENSUS=PASS` 但什么都没查）

`corpus-census.py` 第一版的 `parse_report()` 只解析 `header=` 那一行。
探针把其余计数器分行打印，于是 `entity_failures` / `malformed_packets` /
`packet_entity_decode_failures` 等 8 个字段全部读成「缺失」，
而断言循环写的是「字段存在且非 0 才算失败」→ 缺失被当成跳过。
结果：24 份 demo 报 `CORPUS-CENSUS=PASS`，同一份汇总里 `sum_packets=0`。

修复三条：
1. 解析所有 `key=value` 行（`message_type_histogram:` 行除外）。
2. **字段缺失即失败**（`REQUIRED_FIELDS`），不再静默跳过。
3. `packets_scanned=0` 即失败 —— 「什么都没查」不能读成「什么都没错」。

`census-negative-test.sh` 用 10 个变异证明该判据能变红：改计数器、删字段、
清零包数、伪造分类分歧、流内 HLTV 位被忽略、`source_tv_flag` 自相矛盾、
`index_state=invalid` 必须红、`truncated_tail` 必须记为跳过、
以及截断文件的前缀计数器非 0 仍须红。恢复后报告逐字节相同。

### 0.3 探针改了输出却没有证明旧读数没动

给 `entity_protocol_probe` 加 `recording_stream=` 一行时，没有任何东西证明
原有计数器没被顺手改掉。补 `check-probe-output-additive.sh`：
把新行过滤掉后与 `evidence/final/*.txt` 的存档报告逐字节比对。

**这一条第一版是空转的**（全部 SKIP 却报 PASS），详见 §0.7。
修好之后的真实读数是 9/9 计数器逐字节相同。

### 0.4 顺带补上的读数

`entity_protocol_probe` 现在同时输出：
`recording=`（仅头字段）、`recording_stream=`（完整分类器）、
`recording_header_name=`、`server_info_count=`、`server_info_hltv=`、
`server_info_replay_bit=`、`source_tv_flag=`。
加这一行的直接原因：头判定看不到流内 `svc_ServerInfo` 的 `m_bIsHLTV` 位，
所以普查既无法验证 SourceTV 标记，也无法发现语料里到底有没有 SourceTV。

### 0.5 语料里有 7 份「录制中断」的文件，原来会被记成失败

头字段普查（`corpus-header-scan.py`，独立 Python 实现，见 §0.6）发现
**7 份 demo 的 `playback_ticks=0` 且 `playback_frames=0`**。原因：TF2 在录制
**开始**时写头（`ticks`/`frames` 为 0），在收到 `dem_stop` 时回填；录制被中断
（客户端被杀、进程退出）就永远不回填，文件末尾停在半个 entry 上。

实测这 7 份：索引器走到文件末尾失败（`tail_after_fail=0.00MB`、无 `dem_stop`），
但**失败点之前的 entry 全部完整**。原探针的行为是直接 `index=0`、`rc=2`、
不扫描 —— 后果有两层：

1. 普查会把它们记成失败（`header/index`、`no_packets_scanned`），**误红**。
2. **丢掉真实覆盖**：其中一份有 235951 个 entry / 110886 个包，全部被丢弃。

修复（`entity_protocol_probe.cpp`）：
- 新增 `index_state=ok|truncated_tail|invalid`、`index_entries=`、`index_tail_bytes=`。
  未索引尾部 ≤ 64 KiB 且已有 entry → `truncated_tail`；否则 `invalid`。
- 只要索引出过 entry 就照常扫描，退出码只在 `invalid` 或无 entry 时为 2。
- **区分是必须的**：把 `invalid` 也归到「截断」会把真缺陷变成静默通过。

普查侧改成**三态**（`corpus-census.py`）：

| 判定 | 条件 | 含义 |
|---|---|---|
| `clean` | `index_state=ok` + 计数器全 0 + 字段齐全 | 完整读取，通过 |
| `skipped` | `truncated_tail` 或 `index_entries=0` | **既不是 PASS 也不是 FAIL**：文件没被读全，不构成证据。按名字列出 |
| `dirty` | 其它一切，含 `index_state=invalid` | 失败 |

截断文件的**已扫描前缀仍要断言计数器全 0**（`census-negative-test.sh` 的
mutation J 钉住这条）。

7 份的实测读数（`evidence/truncated-tail-probe.txt`）：

| demo | entries | 尾部字节 | 恢复的包数 | failures |
|---|---|---|---|---|
| `autorecord_2026-05-21_21-54-16` | 235951 | 144 | 110885 | 0 |
| `autorecord_2026-07-18_22-16-54` | 82643 | 204 | 39011 | 0 |
| `autorecord_2026-07-05_22-03-13` | 59478 | 26 | 27106 | 0 |
| `autorecord_2026-07-06_00-35-38` | 21071 | 42 | 9940 | 0 |
| `autorecord_2026-09-02_20-36-02` | 17285 | 287 | 8185 | 0 |
| `autorecord_2026-09-30_21-03-06` | 10178 | 12 | 4998 | 0 |
| `autorecord_2026-09-02_20-46-11` | 5330 | 256 | 2447 | 0 |

合计 **202572 个包**从「被丢弃」变成「被扫描且全部通过」。

顺带：`/W4` 在这个改动上抓到一处真 bug —— 我最初用
`const char* indexState` 加 `indexState == "invalid"`，那是指针比较（C4130），
只在链接器合并字面量时才碰巧成立。改成 `enum class IndexState`。

### 0.6 全语料头字段普查（独立实现）

`corpus-header-scan.py` 按 `demoformat.h` 的布局**另写一份 Python 解析器**，
只读每份文件的前 1072 字节，因此能覆盖解码要花几小时的全量语料。
结果（1643/1643 可读）：

| 项 | 值 |
|---|---|
| 扫描 | 1643，不可读 0 |
| 体积 | 40.22 GB（min 1.0 / median 19.7 / p90 55.6 / max 143.0 MB） |
| 录制类型 | **1634 POV + 9 SourceTV** |
| `autorecord_*` 判 POV 违反数 | **0**（1634/1634 一致） |
| SourceTV 的 `clientname` | 全部为 `SourceTV Demo`（唯一取值） |
| 不同地图 | 129 |
| 协议 | demoProtocol 3 / networkProtocol 24（唯一） |
| `ticks=0` 文件 | 7（见 §0.5） |

**9 份 SourceTV 全部是手工命名的那 9 份**（`autorecord_*` 一份都不是）：
`73.dem`、`SUNSHINE.dem`、`gullyscout.dem`、`proc2.dem`、`processdemo.dem`、
`prodcutscout.dem`、`review1.dem`、`review12.dem`、`review44.dem`，
服务端都是 `Matcha Bookable`，地图 9 张各不相同（ashville / sunshine /
gullywash / process / product / reckoner / sultry）。

`--selftest` 用 6 个用例（含 4 个必须判 SourceTV 的写法变体 + 1 个必须判 POV
的负对照）证明分类不是常量。

### 0.7 `check-probe-output-additive.sh` 从建立起就是空转的

§0.3 说「补了增量性检查，9/9 相同」。**那个读数从来没有真实发生过。**

该脚本用的是仓库相对路径（`testdata/demos/bagel.dem`），而隔离测试树是用
`git archive d585af8 native` 导入的，**只有 `native/`，没有 `testdata/`**。
于是 9 份 demo 全部命中「文件不存在」分支，打印 `SKIP` 后 `fail` 仍为 0，
脚本以 `PROBE-OUTPUT-ADDITIVE=PASS` 结束。**零次比较的通过不是通过。**

修掉路径后又暴露第二层：5 份 demo 在 `recording=` 一个字段上 `DRIFTED` ——
`POV (heuristic)` → `SourceTV`。这不是计数器漂移，而是 `226d119` 分类器修复
的**预期结果**（存档报告是修复前的二进制产出的）。

现在这个脚本只主张一件事：**计数器逐字节未动**。
- 路径改为绝对路径（`SRC=D:/TF2_Demo_Player`），缺失输入记为失败而非跳过。
- 新增 `compared=` 计数，必须等于 demo 数，否则判 FAIL。
- 基线改为**冻结目录 `evidence/probe-baseline/`**，不再用 `evidence/final/`。
  原因是 `verify-all.sh` 的 2/9 步会跑 `run-demos.sh evidence/final`
  把那份存档用当前二进制**覆盖**，等 7/9 步跑到时已经变成「新对新」。
  第一次接进验证链就是这么红的：9 份全部 `STALE-BASE`、`compared=0/9`。
- 存档报告若已含被过滤的行 → `STALE-BASE` 失败（说明基线被刷新过）。
- `recording=` 连同其可选 ` (heuristic)` 后缀一起归一化；该字段的断言
  交给 `oracle-recording-types.sh`（它按硬编码表逐份钉 `recording=` /
  `recording_stream=` / `server_info_hltv=` / `server_info_replay_bit=`）。

同类缺陷一并修掉（都是「通过但什么都没查」）：

| 位置 | 原判据 | 现在 |
|---|---|---|
| `verify-all.sh` 1/9 | 只看 `cmake_build_rc=0` + `errors=0` | 加 `exe_count=21` |
| `verify-all.sh` 3/9 | 只看 `fixture_failures=0` | 加 `^PASS` 行数 = 58 |
| `oracle-recording-types.sh` | 缺 demo → `SKIP` 且仍 PASS；`hltv_bit` 列只打印不校验 | 缺 demo → FAIL；加 `compared` 计数；逐份断言 `hltv_bit`（SourceTV=1 / POV=0）与 `replay_bit=0` |
| `census-negative-test.sh` | 摘要对 `reports/*.txt` 取 glob；受害者可能已不在抽样里 | 摘要对**本次快照**取哈希；新增「受害者必须在 `corpus.csv` 里」断言（`corpus.csv` 的 demo 名带 `.dem`，报告文件名是 `.txt`，两种写法都接受） |

**教训**：判据不能只写「没有失败」，还要写「确实查了 N 件」。前者在输入缺失
时恒真。这条已写进 `verify-all.sh` 头部注释。

---

## 1. 提交、基线与修改文件

| 项 | 值 |
|---|---|
| 测试树 | `D:\TF2_Native_Test`，分支 `p0-entity-protocol` |
| 导入提交 | `7ab4e72` = `git archive d585af8 native` 的 `native/` 树 |
| **修复提交** | **`658ae69`** `fix(p0): decode the message types that were abandoning whole packets` |
| **第二遍复核提交** | **`226d119`** `fix(p0): SourceTV demos were classified as POV, and the census could not go red` |
| **第三遍复核提交** | **`22dc465`** `fix(p0): a verdict that passes without checking anything is not a verdict`（截断尾部三态 + 三处空转判据） |
| 上游基线 | `d585af8`（`native-mvp`；`HANDOFF` 记录的 `51f6f0d` 是它的父提交） |
| 源目录状态 | `work/native-mvp-source` HEAD `d585af8`，`git status` 干净 |

`git show 658ae69 --numstat`（仅协议相关文件）：

```
23   0   native/CMakeLists.txt
66   0   native/include/demo_header.h
242  49  native/src/demo_header.cpp
252  0   native/tools/entity_protocol_probe.cpp          (新)
581  0   native/tools/entity_message_fixture_probe.cpp   (新)
3    0   native/tools/presentation_probe.cpp
```

- `presentation_probe.cpp` 的 3 行是**跟随改动**：它用手写的 `EntityHistoryEvent`
  把 tick 编码进 `state.classId`，历史事件改为增量表示后需要显式置 `fullState`。
  不置会静默改变该探针语义。
- 另外包含构建脚本（`build-cmake.sh` / `build-target.sh` / `env-msvc.sh` /
  `run-demos.sh` / `mutate.sh` / `check-oracle.sh`）与 `evidence/`。
  纯协议改动可用 `fix-P0-entity-messages.patch` 单独提取。

### 1.1 缺陷本体

`demo_header.cpp` 的消息循环对 5 个消息类型没有分支。命中未知类型时它
`break` 掉**整个包**，同包里排在后面的 `svc_PacketEntities` 一起被丢弃，
实体→类映射从此静默冻结。

| 类型 | 名字 | 布局来源 |
|---|---|---|
| 2 | `net_File` | `_refs_demostf/src/demo/message/generated.rs` `FileMessage` |
| 11 | `svc_SetPause` | 同上 `SetPauseMessage`（1 bit） |
| 21 | `svc_BSPDecal` | 同上 `bspdecal.rs`（用 `read_bit_coord`，**不是** `BitCoordMP`） |
| 29 | `svc_Menu` | 同上 `MenuMessage` |
| 32 | `svc_CmdKeyValues` | 同上 `CmdKeyValuesMessage` |

`koth_bagel_rc13` 的直方图在修复前只有 `11(svc_SetPause)=2` 这一个没有解码器；
`protocol23` 是 `2, 11, 21` 加上因位错读出的 `34/46/50/56`。

### 1.2 顺带修掉的三个缺陷（都是复现过程中暴露的）

1. **`svc_Prefetch` 位宽**：原为 `networkProtocol > 23 ? 14 : 13`。
   第一来源 Valve `src/public/soundinfo.h` 的
   `SoundInfo_t::ReadDelta()`：`if ( nProtoVersion > 22 ) READ_DELTA_UINT( nSoundNum, MAX_SOUND_INDEX_BITS )`，
   `src/public/soundflags.h` 定义 `MAX_SOUND_INDEX_BITS 14`。本地参考
   `prefetch.rs` 一致。协议 23 上少读 1 bit → 同包后续消息全部错位。
2. **string-table user data 的 1024 字节上限**：5 处 `> 1024u` 判定。
   参考实现 `stringtable.rs` 的 `read_table_entry` 读 14 位长度后直接
   `read_bits(bytes * 8)`，**无上界**。bagel 的 `instancebaseline` 第 3 条是
   7669 字节，被拒后**整张 instancebaseline 表被丢弃**：9 份 demo 里
   实例基线只剩 6 条（实际 65–198 条），绝大多数实体 Enter 时状态为空。
3. **preserve 路径整表拷贝**：`EntityState candidate = liveState; ... = std::move(candidate);`
   每次更新复制两遍 `unordered_map`。bagel 有 1 391 706 次 preserve。

`EntityHistoryEvent` 原本按值携带整份 `EntityState`，每次 preserve 事件深拷贝一次
属性表。bagel 1.39M 次。修复前这个开销被"丢包导致实体状态大多为空"掩盖；
协议修好后它变成主导项（bagel 单份 7 分钟）。现改为只记录该包实际写入的属性
（`propIndex` + 值），键在回放时从 SendTable 重建。

---

## 2. 真实资源路径与 Demo 文件名

TF2 资源根（构建/运行时的 `--tf-root`）：
`D:\SteamLibrary\steamapps\common\Team Fortress 2\tf`

Demo 文件：

| 名称 | 绝对路径 | 录制类型 |
|---|---|---|
| bagel | `D:\TF2_Demo_Player\testdata\demos\4a9bfb9276509d0ec5f5fdc722a95b17_match-20260927-0239-koth_bagel_rc13.dem` | POV |
| snakewater | `D:\TF2_Demo_Player\testdata\demos\bb841c6d379ff7c40d0c8baf99f59d8d_matcha-20260927-1347-cp_snakewater_final1.dem` | POV |
| ashville73 | `D:\TF2_Demo_Player\work\protocol-rescue-20261005\73.dem` | POV |
| decal | `D:\TF2_Demo_Player\.scratch\tf2-demo-parser\test_data\decal.dem` | POV |
| protocol23 | `D:\TF2_Demo_Player\.scratch\tf2-demo-parser\test_data\protocol23.dem` | POV |
| comp | `D:\TF2_Demo_Player\.scratch\tf2-demo-parser\test_data\comp.dem` | POV |
| saytext2 | `D:\TF2_Demo_Player\.scratch\tf2-demo-parser\test_data\saytext2.dem` | POV |
| short2024 | `D:\TF2_Demo_Player\.scratch\tf2-demo-parser\test_data\short-2024.dem` | POV |
| small | `D:\TF2_Demo_Player\.scratch\tf2-demo-parser\test_data\small.dem` | POV |

独立 oracle（demostf `tf_demo_parser`，Rust，与 C++ 无共享代码）：
`D:\TF2_Demo_Player_Deliverable\tools\ent-oracle\target\release\ent-oracle.exe`
sha256 `f5d1694212050add4629b3b3ef216ca28c28c387a0845761d38e4e90b2921e05`

---

## 3. 可复制的命令

### 3.1 环境（本机关键约束，已验证）

CMake 自带的 `Visual Studio 17 2022` 生成器在本机**不可用**：它要跑
`vcvars64.bat` → `reg.exe`，而 `reg.exe` 在沙箱命令黑名单里，编译器探测直接
失败（`No CMAKE_CXX_COMPILER could be found`；传 `-DCMAKE_GENERATOR_INSTANCE`
无效，`vswhere.exe` 本身能跑）。改用 **NMake Makefiles**，但必须：

1. 显式设 `INCLUDE` / `LIB`（Windows 风格路径）；
2. 把 Windows SDK 的 `bin\<ver>\x64` 加进 `PATH`，否则 `rc.exe`/`mt.exe` 缺失，
   CMake 报 `CMAKE_MT-NOTFOUND` 且链接步 `no such file or directory`。

`env-msvc.sh` / `build-cmake.sh` 已封装。

```bash
cd /d/TF2_Native_Test
bash build-cmake.sh            # 干净全量 Release 构建（21 个 exe）
bash build-target.sh entity_protocol_probe entity_message_fixture_probe
```

### 3.2 读数

一条命令跑完整个证据链（构建 → 普查 → fixture → oracle → 变异）：

```bash
cd /d/TF2_Native_Test
bash verify-all.sh          # 输出 evidence/verify/*.txt，末行 VERIFY=PASS
```

分步：

```bash
cd /d/TF2_Native_Test
bash run-demos.sh evidence/final          # 9 份 demo 普查
native/build-nmake/entity_protocol_probe.exe --summary <demo.dem>
native/build-nmake/entity_message_fixture_probe.exe
bash check-oracle.sh "D:/TF2_Demo_Player_Deliverable/tools/ent-oracle/target/release/ent-oracle.exe" evidence/final
bash mutate.sh                            # 变异验证套件
```

### 3.3 产物

| exe | 绝对路径 |
|---|---|
| 主程序 | `D:\TF2_Native_Test\native\build-nmake\tf2_demo_native.exe` |
| 协议普查探针 | `D:\TF2_Native_Test\native\build-nmake\entity_protocol_probe.exe` |
| 消息 fixture 探针 | `D:\TF2_Native_Test\native\build-nmake\entity_message_fixture_probe.exe` |

---

## 4. 原始输出

### 4.1 构建（干净全量，`evidence/cmake-build.log`）

```
=== summary ===
cmake_build_rc=0
errors=0
warnings=1
D:\TF2_Native_Test\native\src\main.cpp(1284): warning C4457: “instance”的声明隐藏了函数参数
exe_count=21
```

唯一的警告是 `HANDOFF-2026-10-06.md` 已记录的既有 `C4457`，与本次改动无关。

### 4.2 9 份 demo 普查：基线 → 修复后

`entity_failures` 是 P0 验收数字。`evidence/baseline/` 与 `evidence/final/`。

| demo | entity_failures | malformed_packets | unknown_message_packets | history_gap | instance_baselines |
|---|---|---|---|---|---|
| bagel | 6581 → **0** | 3 → **0** | 2 → **0** | 1 → **0** | 6 → **126** |
| snakewater | 0 → 0 | 1 → **0** | 0 → 0 | 0 → 0 | 6 → **120** |
| ashville73 | 0 → 0 | 1 → **0** | 0 → 0 | 0 → 0 | 6 → **158** |
| decal | 49 → **0** | 8 → **0** | 7 → **0** | 1 → **0** | 6 → **171** |
| protocol23 | 0 → 0 | 1703 → **0** | 1189 → **0** | 0 → 0 | 8 → **125** |
| comp | 0 → 0 | 1 → **0** | 0 → 0 | 0 → 0 | 6 → **198** |
| saytext2 | 0 → 0 | 1 → **0** | 0 → 0 | 0 → 0 | 6 → **194** |
| short2024 | 0 → 0 | 1 → **0** | 0 → 0 | 0 → 0 | 6 → **65** |
| small | 0 → 0 | 1 → **0** | 0 → 0 | 0 → 0 | 6 → **65** |

9/9 全部 `entity_failures=0 malformed_packets=0 unknown_message_packets=0`，
且 9/9 的 `message_types_seen_without_decoder: <none>`。

### 4.3 bagel 关键读数（修复后，`evidence/final/bagel.txt`）

```
packets_scanned=73121 malformed_packets=0 unknown_message_packets=0
packet_entities=73118 packet_entity_updates=1409096 delta_packets=73117
enter=10051 preserve=1390706 leave=1357 delete=6982
entity_failures=0 entity_update_header_failures=0 packet_entity_decode_failures=0
  prop_missing_table=0 prop_index=0 prop_value=0
delta_base_unavailable=0 history_gap=0 history_dropped_packets=172
instance_baselines=126 baseline_applied=10035 baseline_misses=16 baseline_apply_failures=0
active_entities=679 max_active_entities=732
first_entity_failure_tick=-1 ... (全部 -1 = 从未发生)
```

对照修复前（`evidence/baseline/bagel.txt`）：

```
entity_failures=6581 packet_entity_decode_failures=7320 prop_index=217 prop_value=21
delta_base_unavailable=72268 history_gap=1 history_gap_tick=129277
first_entity_failure_tick=57004 entity=804 update=0 payload_bits=5018 diff=9
message_types_seen_without_decoder: 11(svc_SetPause)
```

`7320` 与清单记录的失败数一致 —— 基线被精确复现，不是重新定义出来的。

### 4.4 新解码器在真实 demo 上的触发次数

| demo | file_messages | set_pause | bsp_decals | menus | cmd_key_values | string_table_user_data_max_bytes |
|---|---|---|---|---|---|---|
| bagel | 0 | **2** | 0 | 0 | 0 | 7669 |
| decal | **9** | **4** | **971** | 0 | 0 | 2395 |
| protocol23 | **2** | 0 | **1** | 0 | 0 | 1940 |
| snakewater / ashville73 / comp / saytext2 / short2024 / small | 0 | 0 | 0 | 0 | 0 | 1940–7669 |

`menus` 与 `cmd_key_values` 在本地 9 份 demo 里**一次都没出现**，只由 §5 的
fixture 覆盖。这是本机语料的限制，不写进"已验证"。

`string_table_user_data_max_bytes=7669` 就是原 1024 上限拒掉的那条。

### 4.5 fixture 探针（`evidence/fixture.txt`）

```
entity_message_fixture_probe
-- message layout fixtures
PASS net_File: sentinel tick reached
PASS svc_SetPause: paused state latched
PASS svc_BSPDecal: bit-coord + ent/model form
PASS svc_BSPDecal: fraction-only coordinate form
PASS svc_Menu: sentinel tick reached
PASS svc_CmdKeyValues: sentinel tick reached
PASS svc_Prefetch: protocol 24 uses 14 bits
PASS svc_Prefetch: protocol 22 uses 13 bits
PASS svc_Prefetch: 13-bit stream misaligns protocol 23
-- svc_PacketEntities fixtures
PASS baseline: entity 0 bound to class 0
PASS baseline: m_iHealth == 100
PASS baseline: m_iTeamNum == 200
PASS delta preserve: m_iHealth updated to 150
PASS delta preserve: untouched m_iTeamNum kept at 200
PASS history: preserve event carries one property delta
PASS replay: m_iHealth == 150
PASS replay: untouched m_iTeamNum == 200 after delta replay
PASS delta leave: class retained
PASS delta delete: class cleared
PASS preserve-after-delete: counted as an entity failure
PASS preserve-after-delete: no fabricated class binding
PASS orphan delta: missing base frame reported
...
fixture_failures=0
```

共 **58 个断言，0 失败，退出码 0**。覆盖清单要求的
baseline / delta / Preserve / Leave / Delete，外加两条反向用例：

- `preserve-after-delete`：对已删除实体做 Preserve 必须**计为失败**，
  且不得凭空绑定一个类 —— 对应清单"不得用最近帧伪造缺失基帧"。
- `orphan delta`：`deltaFrom` 指向历史里不存在的帧时必须
  上报 `delta_base_unavailable`，而不是悄悄换一个邻近帧当基准。

### 4.6 与 Rust oracle 的逐值对照

见 §4.7（`check-oracle.sh` 输出）。三项：oracle `enter` 行数 ↔ `enter=`、
oracle `pkt` 行数 ↔ `packet_entities=`、oracle `Σentities=` ↔
`packet_entity_updates=`。

### 4.7 oracle 门禁

`bash check-oracle.sh <oracle.exe> evidence/final` —— 9/9 三项逐值全等：

```
demo          o_enter  c_enter    o_pkts   c_pkts  o_entities c_entities    fails  verdict
bagel           10051    10051     73118    73118     1409096    1409096        0  OK
snakewater       9822     9822     64330    64330     1427501    1427501        0  OK
ashville73      22424    22424    112172   112172     3030444    3030444        0  OK
decal          158837   158837     80886    80886     1971055    1971055        0  OK
protocol23     184114   184114     43324    43324     1294290    1294290        0  OK
comp            14605    14605     85864    85864     2335407    2335407        0  OK
saytext2        10846    10846     72528    72528     1739480    1739480        0  OK
short2024         296      296       170      170         474        474        0  OK
small             204      204       111      111         316        316        0  OK

ORACLE-GATE=PASS
```

这是最强的一条独立证据：demostf 的 `tf_demo_parser` 与 C++ 解码器没有共享代码，
两者对同一份 demo 的 enter 数、`svc_PacketEntities` 包数、实体更新总数完全一致。
清单要求的"包数/实体更新数与 Rust `tf_demo_parser` 对齐"由此满足。

（首次运行该门禁时 9 行全是 0：我把 evidence 目录当成了 oracle 路径传进去，
而 `[ -x <目录> ]` 为真所以没报错。已改为 `[ -f ]` 并把默认值改成
`evidence/final`。这类"静默读 0"正是 SOUL 说的"判据读在错误的时刻"。）

---

### 4.8 一条命令的全链验证

```bash
$ bash verify-all.sh
=== 1/5 clean full Release build ===
cmake_build_rc=0
errors=0
warnings=1
exe_count=21
BUILD=PASS
=== 2/5 nine-demo census ===
... 9/9 entity_failures=0 malformed_packets=0 unknown_message_packets=0
CENSUS=PASS (9/9 demos at zero)
COVERAGE=PASS (9/9 demos, every observed message type decoded)
=== 3/5 wire fixtures ===
fixture_pass_count=58
fixture_failures=0
FIXTURE=PASS
=== 4/5 oracle cross-check ===
... 9/9 OK
ORACLE-GATE=PASS
ORACLE=PASS
=== 5/5 mutation suite ===
... 5/5 MUTATION-RED
RESTORED-IDENTICAL bagel.fixed.txt
RESTORED-IDENTICAL proto23.fixed.txt
RESTORED-IDENTICAL fixture.fixed.txt
MUTATION-SUITE=PASS
MUTATION=PASS

VERIFY=PASS
```

原始输出落在 `evidence/verify/1-build.txt` … `5-mutation.txt`。
总耗时约 15 分钟。

---

## 5. 变异验证（反向证据）
`bash mutate.sh`，5 个变异全部变红，恢复后读数与修复版**逐字节相同**。

| 变异 | 改动 | 读数 | 结果 |
|---|---|---|---|
| m1 | 删掉 `type == 11` 分派分支 | bagel `unknown_message_types` `11,11`；`entity_failures` 0 → **6581**；`malformed_packets` 0 → 2 | RED |
| m2 | 5 处 string-table 上限恢复 `> 1024u` | bagel `instance_baselines` 126 → **6**；`malformed_packets` 0 → 1；`baseline_misses` 16 → **9191** | RED |
| m3 | `svc_Prefetch` 位宽退回 `> 23` | protocol23 `malformed_packets` 0 → **1700**；`unknown_message_packets` 0 → **1183**；`unknown_message_types` `<none>` → `34,34,56,56,...` | RED |
| m4 | preserve 历史事件改回整份 `EntityState` | fixture `fixture_failures` 0 → **1**；`FAIL history: preserve event recorded` | RED |
| m5 | preserve 路径不换回基态 | fixture `fixture_failures` 0 → **1**；`FAIL delta preserve: untouched m_iTeamNum kept at 200` | RED |

```
RESTORED-IDENTICAL bagel.fixed.txt
RESTORED-IDENTICAL proto23.fixed.txt
RESTORED-IDENTICAL fixture.fixed.txt
MUTATION-SUITE=PASS
```

m1 尤其关键：把修复**单独**去掉，bagel 精确回到 `entity_failures=6581` ——
说明这个数字就是该缺陷造成的，不是环境噪声。

`m3` 的 `1700/1183` 与基线 `1703/1189` 略有差异，因为基线还同时缺另外 4 个
解码器；这不是不一致，是变异只回退了 prefetch 一处。

---

## 6. 性能

| 场景 | bagel 单份 | 9 份合计 |
|---|---|---|
| 基线构建 | ~60 s | ~10 min |
| 仅修协议（历史事件仍整表拷贝） | **~7 min（回退）** | 未跑完 |
| 修协议 + 事件改增量 | **19.5 s** | 3 min 41 s |

原始时间戳（可复核，`ls -la --time-style=+%H:%M:%S`）：

```
evidence/baseline/   bagel.txt 18:00:04   ...   small.txt 18:09:04
evidence/after-fix/  bagel.txt 18:17:57   snakewater.txt 18:19:xx ... 18:21:5x
evidence/final/      bagel.txt 18:52:xx   ...   small.txt 18:56:0x
```

基线 9 份从首份完成到末份完成是 9m0s，加上首份自身约 60 s ≈ 10 min。
最终版 9 份 3m41s（`run-demos.sh` 的实测总时长）。

`entity_failures=0` 之后实体状态不再为空，历史事件的整表拷贝从"被掩盖"变成
主导项，所以协议修复单独上线会**让扫描慢 7 倍**。这条是本次交付里最重要的
非协议发现，也是"缺陷掩盖了另一个缺陷"的实例。

---

## 7. 原目录未被改动

```
work/native-mvp-source     HEAD=d585af8 dirty=0
work/entity-protocol-next  HEAD=d2ba957 dirty=0
work/wt-P0-sourcetv-fix    HEAD=7b8d97d dirty=5   <- 文件 mtime 11:45–11:48，本会话 12:07 之前
```

`native-mvp-source` 干净；`wt-P0-sourcetv-fix` 的 5 个脏文件时间戳早于本会话开始，
是 Ringo 上一轮留下的，未被我触碰。

`D:\TF2_Native_Test` 由 `git archive d585af8 native | tar -x` 建立，独立 `git init`。

---

## 8. 已验证 / 未验证 / 已知限制

### 已验证

- 9/9 本地 demo：`entity_failures=0 malformed_packets=0 unknown_message_packets=0`。
- **9/9 的录制类型已由头字段与流内标记双向确认**（`oracle-recording-types.sh`，
  `ORACLE-RECORDING-TYPES=PASS`）：5 份 SourceTV（`clientname="SourceTV Demo"`，
  流内 `m_bIsHLTV=1`）+ 4 份 POV（`m_bIsHLTV=0`），两种判定 9/9 一致。
  见 §0.1 —— 第一遍把 9 份全标成 POV 是分类器只读 `servername` 所致。
- bagel 与 snakewater 全量扫描通过（清单指定）。**两份都是 SourceTV**，
  所以清单的「SourceTV delta/base 语义」有直接证据。
- 与 Rust oracle 的包数 / 实体更新数 / enter 数逐值对照（§4.7）。
- 58 个 bit-exact fixture 断言通过，覆盖 baseline / delta / Preserve / Leave / Delete。
- 6 个变异全部变红，恢复后读数逐字节相同。
- 干净全量构建 21 个 exe，0 error，1 个既有警告。
- 新增 5 个消息解码器中的 3 个（`net_File` / `svc_SetPause` / `svc_BSPDecal`）
  在真实 demo 上确实触发（§4.4）。
- **全语料头字段普查 1643/1643 可读**（`corpus-header-scan.py`，独立 Python 实现）：
  1634 POV + 9 SourceTV，`autorecord_*` 违反 0（§0.6）。
- **7 份「录制中断」文件**从「记成失败且不扫描」改为 `truncated_tail` 后照常扫描，
  恢复 **202572 个包**，计数器全 0（§0.5）。
- **普查判据可证伪**：`census-negative-test.sh` 10 个变异全部按预期变红
  （含 `index_state=invalid` 必须红、`truncated_tail` 必须记为跳过、
  截断文件前缀计数器非 0 仍须红），恢复后报告摘要不变。
- **oracle 语料抽样**与 Rust 实现逐值全等（`--quick` 为 8 份；
  `ORACLE-CORPUS=PASS` + `--selftest` 6 用例证明比较能拒绝不匹配）。
- **探针输出增量性**：`compared=9/9`，计数器逐字节相同（§0.7）。

### 未验证（本机做不到，明确标出）

- ~~**真实 SourceTV 语料**~~ —— **已关闭，见 §0.1**。本机 9 份 oracle 语料里
  5 份是真实 SourceTV（bagel / snakewater / ashville73 / comp / saytext2），
  全部通过 oracle 逐值对照。原先写「缺语料」是分类器缺陷造成的误判。
- **`svc_Menu` / `svc_CmdKeyValues`**：本地 demo 零出现，只有 fixture 证据。
- **主程序画面**：本次只跑探针，未做人工画面确认（属外部输入）。
- **本机 exe 哈希不可复现**（本次实测）：`touch` 源码强制重链接两次，产物 sha256 不同，
  `cmp -l` 差 **2 字节**（偏移 273 与 387877）。所以 exe 哈希**不能**当复核锚点。
  可复核锚点是源码哈希 + 编译命令：

  ```
  sha256(native/src/demo_header.cpp)   = 654d5c9b692bed52488c3fd1781144512d159368636e7a7b22f7f5023540109a
  sha256(native/include/demo_header.h) = 704079b310812ca1ea0859e01904f430c9df6e5e0cd4fdbeb7a030a450815d8e
  ```

  注意第一次做这个实验时我忘了 `touch`，CMake 判定无需重链接，两次"构建"其实是同一个文件
  → 哈希相同，差点得出相反结论。
- **主程序 1.41 GB 工作集**偏高，未做长时 soak（属 P1 稳定性）。
- 未跑 10 分钟 soak、安装包双击、真实 GPU FPS（属 P1 稳定性任务）。

### 已知限制

- `EntityHistoryEvent` 改为增量后，回放需要 `classId` 能找到 SendTable；
  找不到时 `queryEntitySnapshotAtOrBeforeTick` 返回 `Gap` 而不是给一个错状态。
  这是有意的降级，不是静默容错。
- `decodeDemoMessageStream` 是为 fixture 新增的测试入口（公开符号，无行为改动）。
  如果主线不想要这个缝，`entity_message_fixture_probe` 需要一起拿掉。

---

## 9. 产物可运行性

| 项 | 值 |
|---|---|
| 目标 exe | `D:\TF2_Native_Test\native\build-nmake\tf2_demo_native.exe` |
| 资源前提 | `--tf-root "D:\SteamLibrary\steamapps\common\Team Fortress 2\tf"` |
| 音频设备 | **测试一律显式指定 `--audio-device 6`（`耳机 (xduoo audio)`）**，见 §9.2 |
| 是否可运行 | 是 |

### 9.1 启动/退出记录

**A. 拒绝默认音频设备（无 GUI 帧）**

```
$ native/build-nmake/tf2_demo_native.exe --audio-device
exit_code=13
```

`--audio-device` 不带值时按设计拒绝默认设备并返回 13，不打开任何设备。

**B. 小 demo 实跑（默认设备，仅此一次）**

```
$ tf2_demo_native.exe --tf-root "<TF2 tf>" --demo "...\small.dem" \
      --metrics-file "...\evidence\metrics-small.csv"
timeout_exit=124          # 由 timeout 终止，GUI 程序不自退
```

`evidence/metrics-small.csv`（节选）：

```
elapsed_seconds,rendered_frames,fps,tick,working_set_bytes,private_bytes
1.00138,71,70.9023,115,184922112,197521408
5.01825,491,106.963,115,181198848,193691648
10.0214,1032,115.972,115,181248000,193667072
16.0239,1721,114.948,115,181252096,193667072
```

约 100–116 FPS，`small.dem` 共 115 tick 播完后停在末帧；工作集约 181 MB 且稳定。

**C. bagel 实跑（P0 目标 demo，音频固定到设备 6）**

```
$ tf2_demo_native.exe --tf-root "<TF2 tf>" --demo "...koth_bagel_rc13.dem" \
      --audio-device 6 --metrics-file "...\evidence\metrics-bagel-dev6.csv"
timeout_exit=124
```

`evidence/metrics-bagel-dev6.csv`（66 行，节选）：

```
elapsed_seconds,rendered_frames,fps,tick,working_set_bytes,private_bytes
20.6023,0,0,16,1410453504,1459417088
...
89.9489,5867,95.9599,7324,1409822720,1458339840
```

- 首次写入在 `elapsed≈20.6 s`：**加载 73k 包 + 资源解析约 20 秒**，之后才开始出帧。
- 出帧后约 96 FPS，tick 推进到 7324，全程无崩溃。
- 工作集约 **1.41 GB**，私有约 1.46 GB，**10 秒内无增长趋势**，但绝对值偏高 ——
  这是 P1 稳定性任务的输入，不是本项结论。
- 无残留进程：`tasklist /FI "IMAGENAME eq tf2_demo_native.exe"` → 没有运行的任务。

### 9.2 音频设备状态

`winmm_sink_probe --list-devices` 报告本机 `device_count=13`：

```
device=0  name=耳机 (Realtek(R) Audio)
device=6  name=耳机 (xduoo audio)          <- 测试固定使用这一路
device=9  name=Voicemeeter Input (VB-Audio Voicemeeter VAIO)
device=10 name=扬声器 (Realtek(R) Audio)
...
```

- 设备 6 已实测可打开并出帧（§9.1-C）。
- 未传 `--audio-device` 时会用 `WAVE_MAPPER`（系统默认设备）。
  **这是本项交付里我犯的错**：第一次跑 bagel 没带该参数，音频从默认设备放了出来。
  此后所有会出声的测试固定 `--audio-device 6`。
- `--audio-device` 只接受数字索引（`std::wcstoul` 解析后必须
  `< waveOutGetNumDevs()`），不支持按名字。

---

## 10. 结论

P0 的核心缺口已闭环：bagel 从 `entity_failures=6581 / decode_failures=7320`
降到 0，snakewater 与另外 7 份 demo 同样归零，失败原因为"5 个消息类型没有
解码分支导致整包被丢弃"，不是 SendProp 位流语义错误。

**清单要求的 SourceTV 侧证据成立**：9 份 oracle 语料里 5 份是真实 SourceTV
（§0.1），全部通过 oracle 逐值对照；其中 bagel 正是 P0 缺陷的发现样本。
第一遍写「SourceTV 语料缺失」是分类器缺陷造成的误判，已在本轮修正。

仍然阻塞的只有**人工画面确认**（外部输入）。P1 的实体回放画面依赖本项的状态
重建，现在状态本身可信了，但画面必须由人看。

第三遍复核（§0.7）修掉的是另一类问题：**判据「通过」但什么都没查**。
`check-probe-output-additive.sh` 因路径写错，9 份全部 `SKIP` 却仍报 PASS；
fixture 步只断言失败数为 0 而不断言跑过几个；构建步只断言 rc=0 而不断言产出
几个 exe。三处现在都断言「确实查了 N 件」。验证链因此从 8 步扩到 9 步，
并加了 oracle 语料抽样这一步 —— 此前「POV 保持 0 failures」只有 9 份 demo 的
证据，对 1634 份 POV 语料没有独立实现侧证据。

---

## 附：修复补丁

`fix-P0-entity-messages.patch` —— 只含协议相关文件（`native/`），
sha256 `5c513c851c4a68af28df810ff6852947442ee55e77fe1280d1f27907e22a9cf5`，
1413 行。

**已实测**：把 `d585af8` 的 `native/` 树导出到空目录后
`git apply` 干净通过（`--check` 也过），6 个文件与交付树**逐字节相同**：

```
APPLY=OK
IDENTICAL native/src/demo_header.cpp
IDENTICAL native/include/demo_header.h
IDENTICAL native/CMakeLists.txt
IDENTICAL native/tools/presentation_probe.cpp
IDENTICAL native/tools/entity_protocol_probe.cpp
IDENTICAL native/tools/entity_message_fixture_probe.cpp
```

注：本仓库 `core.autocrlf=true`，补丁内容来自 blob（LF），
`git apply` 落盘时转 CRLF，与工作树一致。
第一次比对时两个**新增**文件报 `DIFFERS`：它们是我用 LF 直接写的，
而仓库其余文件是 CRLF，差 252 / 581 字节（正好等于行数）。已统一为 CRLF
（blob 内容不变，`git diff --stat` 为空），重编译后 `fixture_failures=0`、
bagel `entity_failures=0` 读数不变。
