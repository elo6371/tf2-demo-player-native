# 合并清单（给整合 AI）— 2026-10-08

> **一句话**：源码改动只有 **13 个提交 / 19 个文件 / +3851 −242**，已实测能干净应用到主线
> `d3e2b7c`（**87 个代码文件逐个 blob 相同**）；**其余 235 个路径全是源码之外的东西**
> （判据脚本、证据、文档），它们不进产品，但**没有它们就无法复核这批源码**。
>
> 本清单里的每条结论都做过实测，命令与预期读数写在 §6/§7。
>
> 修订记录：
> - 2026-10-08 第二轮（实体属性选槽新鲜度，`d61b3d6` + `66cd97f`）后重测并更新 ——
>   源码提交 10 → 12、路径 216 → 235、补丁树对象 `6ba1d19b…` → `ece80926…`、补丁 sha256
>   `488fc828…` → `0b583685…`。**§6 的两条路径都按新数字重跑过。**
> - 2026-10-08 晚第三轮（真实 SourceTV 覆盖，`18fe7ea`）后更新 —— **这一轮一个 `native/`
>   文件都没动**，所以补丁 sha256 与合并后树对象**逐字节未变**（§9 已复核）；
>   变的只有外围：路径 235 → **245**、判据脚本 20 → **21**、验收链 14 → **15** 步。
> - 2026-10-08 深夜第四轮（45 单位归因，`290db52`）后更新 —— **这一轮同样一个 `native/`
>   文件都没动**（§9 已复核），变的只有外围：路径 245 → **250**、证据 199 → **204**
>   （新增 `pov-census.json` / `pov-census.txt` / `pov-e18-dead.txt`，`pov-camera.txt` 改为
>   三行）、提交总数 42 → **44**、第 13 步断言 39 → **46**、第 13 步变异 2 → **3** 条红行。
> - 2026-10-08 深夜第六轮收尾（`23db4af`，纯文档/证据）—— 把 15 步链在第六轮树上的
>   `VERIFY=PASS` 记下来（36m20s），并把审查第 3–5 项的只读侦察写进 `HANDOFF` §8 第 2 条。
>   **`native/` 仍未动**（§9 复核过），路径仍 250、提交总数 44 → **45**（多出的 1 个是本次文档提交）。
> - **2026-10-08 第七轮（帧抓取，`3d03719`）后重测并更新 —— 这是第一次改动 `native/`。**
>   源码提交 12 → **13**、`native/` 文件 16 → **19**、+3684 −242 → **+3851 −242**、
>   补丁 sha256 `0b583685…` → **`c09742a1…`**、补丁字节 245595 → **257962 B**、
>   `HEAD:native` 树对象 `ece80926…` → **`a6d1944d…`**、提交总数 45 → **48**、路径 250 → **254**、
>   判据脚本 21 → **22**、验收链 15 → **16** 步、证据 204 → **216**（新增 `evidence/frame-capture/` **10 个**
>   + `evidence/verify/16-*.txt` **2 个**；变异帧 `mutated.bmp` 已 gitignore —— 它由脚本从
>   `paused.bmp` 现生成，是已保留文件的一字节改动，不携带独立证据）。
>   **§6.1（路径 A）已实测重跑**：`git apply` 在 `d3e2b7c` 上干净，
>   排除 `native/docs/` 后 **87 个代码文件 blob 逐个相同**。
>   **§6.2（路径 B）仍未重跑**（见 §6.2 的诚实标注）。

---

## 1. 先回答「复制一份源码来改」

**这件事已经是现状，不需要再做。** 证据：

| 事实 | 读数 |
|---|---|
| 测试树来历 | `7ab4e72 chore: import native-mvp @ d585af8 as isolated test tree` |
| 测试树是不是独立仓库 | 是。`D:\TF2_Native_Test`，独立 `.git`，分支 `p0-entity-protocol` |
| 源目录被改过吗 | **没有**。`work/native-mvp-source` 仍是 `d585af8`，`git status --porcelain` 为空 |
| 基线处两边源码是否同源 | `native/` 树对象**完全相同**：`887a2f31b9d0097438f44392464ad139133e1efc` |
| 另一个工作树 | `work/wt-P0-sourcetv-fix` 仍是 `7b8d97d`，其 5 个未提交文件（**另一路 AI 会话的在制品**）全程未被触碰 |

也就是说：**测试树就是那份「复制出来、可以随便改」的源码**，源目录一直只读。
本轮（2026-10-08）我在测试树里改了 `native/` 的 4 个文件（`demo_header.{h,cpp}`、
`entity_model.{h,cpp}`、`entity_model_probe.cpp`）+ 判据脚本 + 文档/证据；
**源目录 `D:\TF2_Demo_Player` 仍然一个字节没动**。

---

## 2. 源码 vs 外围：分界

`git diff --name-status 7ab4e72..HEAD` 一共 **254 个路径**：

| 类别 | 文件数 | 进产品？ | 说明 |
|---|---|---|---|
| `native/`（源码） | 19 | **是** | 13 个提交，+3851 −242 |
| 判据脚本（仓库根） | 22 | 否 | 17 个 `.sh` + 5 个 `.py`，复核源码用，不参与产品构建 |
| 文档 | 7 | 否 | 验收/交接文档 |
| 证据 `evidence/` | 216 | 否 | 读数与产物（含冻结基线） |
| `.gitignore` + `fix-P0-entity-messages.patch` | 2 | 视情况 | `.gitignore` 与主线**冲突**（见 §6.2） |

> 上表的行加起来不等于 254，是因为 `native/` 那 19 个里含 `native/README.md`
> （文档，但在 `native/` 路径下），且证据/脚本的行是**目录计数**而路径是**文件计数**；
> 要看精确分界就用 `git diff --name-only 7ab4e72..HEAD -- native/ | wc -l`（= 19）。

`merge-patches/` 里的 13 份补丁**不计入这 254**（已 gitignore，随时可用
`git diff 7ab4e72..HEAD -- native/` 重新生成）。

**提交总数 `7ab4e72..HEAD` = 48**，其中**只有 13 个碰 `native/`**。
剩下 35 个是纯文档/证据/判据提交 —— 判断源码有没有被动过要看
`git log --oneline 7ab4e72..HEAD -- native/`，不要数提交条数
（**这两个数会随每一次文档提交增长；`native/` 那 13 个只在本轮真改代码时 +1**）。
**2026-10-08 第七轮终于动了 `native/`**（`3d03719` 帧抓取，+156 行，全加法），
所以 `git diff --stat 66cd97f..HEAD -- native/` 从空变成了 3 个文件。

---

## 3. 源码改动（13 个提交，要合并进产品的部分）

| # | 提交 | 做了什么 | `native/` 改动 |
|---|---|---|---|
| 1 | `658ae69` | P0：5 个「遇到就 `break`」的消息类型改为解码（同包后续实体不再被整块丢弃） | `CMakeLists.txt` +23、`demo_header.h` +66、`demo_header.cpp` +242/−49、**新** `entity_message_fixture_probe.cpp` +581、**新** `entity_protocol_probe.cpp` +252、`presentation_probe.cpp` +3 |
| 2 | `226d119` | P0：SourceTV 被误判成 POV（判定要看 `clientname`，不是 `servername`） | `demo_header.cpp` +22/−2、`entity_protocol_probe.cpp` +23、`presentation_probe.cpp` +23/−1 |
| 3 | `22dc465` | P0：`index_state` 三态（ok / invalid / truncated_tail），让普查判据能变红 | `entity_protocol_probe.cpp` +53/−3 |
| 4 | `8c6f06e` | P1：模型路径从 `modelprecache` 字符串表解析（此前整表被丢弃） | `demo_header.h` +41、`demo_header.cpp` +237/−148、`entity_model_probe.cpp` +9/−1、`entity_protocol_probe.cpp` +32 |
| 5 | `4868e7b` | P1：VPK 目录只解析一次（性能，117 s → 23–31 s） | `asset_root.h` +9、`vpk_archive.h` +34、`asset_root.cpp` +9、`model_loader.cpp` +16/−23、`vpk_archive.cpp` +36 |
| 6 | `9316416` | P1：给 install smoke probe 补链 `vpk_archive.cpp`（修 LNK2019） | `CMakeLists.txt` +5/−1 |
| 7 | `6cb0ecf` | P1：玩家 Z 从兄弟属性 `m_vecOrigin[2]` 读；重复属性选择确定化 | `entity_model.cpp` +85/−10、`entity_model_probe.cpp` +528/−2 |
| 8 | `aa93926` | P1：归档按 **tick 间隙**抽稀，不再按索引（54392 tick 空洞 → worst 1180 ≤ 2×761） | `demo_header.h` +13/−1、`demo_header.cpp` +85/−15、`main.cpp` +9/−1、`entity_model_probe.cpp` +172、`presentation_probe.cpp` +62/−1 |
| 9 | `cd36db1` | P1：武器世界模型用 `m_iWorldModelIndex`（此前画出一双能解析成功的**手**） | `demo_header.h` +62、`entity_model.h` +4、`model_loader.h` +6、`demo_header.cpp` +83、`entity_model.cpp` +1、`model_loader.cpp` +1、`entity_model_probe.cpp` +267、`entity_protocol_probe.cpp` +19/−1 |
| 10 | `c0ff710` | P1：解析观察目标 `m_iObserverMode` / `m_hObserverTarget`（**刻意不接线**，见下） | `entity_model.h` +38、`entity_model.cpp` +49、`entity_model_probe.cpp` +244 |
| 11 | `d61b3d6` | P1：**只加仪表** —— `EntityPropertyValue::lastWriteTick`（在 `readEntityPropUpdates` 唯一写入点盖章）+ `rankPropertyCandidates()` + 探针 `--prop-candidates-at`。**零行为变化**（`PROBE-OUTPUT-ADDITIVE=PASS` 9/9） | `demo_header.h` +15、`entity_model.h` +16、`demo_header.cpp` +5、`entity_model.cpp` +17、`entity_model_probe.cpp` +142 |
| 12 | `66cd97f` | P1：选槽改为**先比 `lastWriteTick`、平局回落到原 rank＋字典序**（POV 上把解析出的坐标从陈旧 Local 槽搬到新鲜 NonLocal 槽） | `demo_header.h` +9/−2、`entity_model.h` +13/−8、`entity_model.cpp` +27/−11、`entity_model_probe.cpp` +73/−35 |
| 13 | `3d03719` | **帧抓取**：渲染器可按请求读回 back buffer（`Present()` 之前）写未压缩 24 位 BMP；主程序 `--capture-frame` / `--capture-tick`；失败走**独立**退出码 16（不污染 `lastError_` 的设备恢复路径）。**纯加法，渲染行为未变** | `native_renderer.h` +13、`native_renderer.cpp` +113、`main.cpp` +30 |

### ⚠️ 四个合并时必须知道的行为变化

1. **`src/entity_model.cpp`、`src/demo_header.cpp` 是主程序 `tf2_demo_native` 的源文件**
   （`native/CMakeLists.txt` 的 `add_executable` 列表）。合并它们 = 主程序行为变化。
   若只想保留解析能力、不要行为变化，可单独回退：
   `git checkout d585af8 -- native/src/entity_model.cpp`（代价：玩家 Z 回到 0，
   且选槽回到 rank 规则 —— POV 上目标坐标会重新变成陈旧槽）。
2. **`main.cpp` 现在被两个提交动过**：`aa93926` 只加了一行 `stale=` 读数（+9/−1），
   `3d03719`（第七轮）加了 `--capture-frame` / `--capture-tick` 参数与一次性抓帧接线（+30）——
   **后者是纯加法**：不传 `--capture-frame` 时程序行为与之前逐字节相同
   （抓帧只在被请求时执行一次，且抓完即退出）。`cd36db1` / `c0ff710` / `d61b3d6` / `66cd97f`
   四轮 `main.cpp` 一个字没动。
3. **`c0ff710` 的观察目标刻意不接线**。⚠️ **这条理由经过两次改写，最新的一次（`290db52`）
   把问题判成了「这一份 demo 答不了」**：
   - 第三轮：接线会偏的距离从 **2729 单位降到 45 单位**（`observer-focus-check.sh` 第 13 步，
     逐轴 `-0.031,44.344,3.781`）；
   - 第四轮（`290db52`）：那 45 **不是误差、也不是接线的代价**。录制的相机原点在 demo 里是
     **保持**的（原始 `democmdinfo` 字节在 demo tick 37640/37650/37655/37677/37679 逐字节相同，
     只是角度不同），它等于**录像者死亡那一刻自己**的 origin；录像者此刻已死
     （`m_lifeState=2`、`FL_TRANSRAGDOLL`）。45 里 44.28 在 y、3.78 在 z 是身体死后自己掉的。
     更有分量的是 **541 点普查**：`follows=1` 只在 **13 个不同检查点**出现，其中 **11 个**
     目标解出的坐标**就落在录像者自己身上（≤0.15 单位，同一快照）** ——
     即**这一份 POV demo 的观察对从不指向别的玩家**。
   **所以合并时这一条不用再「重新判断」**：结论仍是**不接**，依据是
   **「这一份 demo 无法验证接线」**（要验证需要旁观者录的 demo；活着的玩家
   `m_iObserverMode` 恒为 0，玩家 `autorecord` 结构上不可能包含这种情形）。
   本轮仍然没接，`native/` 一个字未改。
4. **选槽规则是通用的**：`findProperty` 对任何后缀匹配到多个候选都会走它。
   实测玩家实体上被复制的属性只有五个 —— `m_vecOrigin`、`m_vecOrigin[2]`、
   `m_angEyeAngles[0]`、`m_angEyeAngles[1]`、`m_nWaterLevel`（第 14 步断言了这个集合，
   并断言 `m_nModelIndex` **不在**其中，所以模型选择不受影响）。
   前四个以外没有逐值门禁。

---

## 4. 外围：判据脚本（22 个文件，源码之外的核心交付）

这些是「源码之外的活」的主体。**每个都带工作量计数**（`compared=` / `exe_count=` /
`pinned_reports=` / 断言条数），因为「没有失败」在输入缺失时恒真。

| 脚本 | 用途 |
|---|---|
| `verify-all.sh` | **一条命令跑完 16 步验收链**，末行 `VERIFY=PASS`/`FAIL`。支持 `--quick` |
| `mutate.sh` | 变异套件：改源码证明读数能变红，跑完 `git checkout -- native/` 恢复。**有干净树守卫** |
| `build-cmake.sh` / `build-target.sh` / `env-msvc.sh` | 本机 CMake 构建（`vcvars64.bat` 不可用，见 §8） |
| `run-demos.sh` | 对 9 份本地 demo 跑探针普查 |
| `check-oracle.sh` | 与独立 Rust oracle 逐包对照 |
| `oracle-recording-types.sh` | 钉住 9 份 oracle demo 的录制类型（POV / SourceTV） |
| `check-probe-output-additive.sh` | 证明探针新增输出行**没有移动**既有计数器，且新增行本身有冻结基线 |
| `census-negative-test.sh` | 证明普查判据**能变红**（10 个变异）；输入集钉成仓库文件 |
| `oracle-trajectory-check.py` | 逐**值**对照重建位置（`compared=40 mismatches=0`，`--mutation` 能红） |
| `history-coverage-check.sh` | Checkpoint 答案最多能多旧（fixture + bagel） |
| `weapon-world-model-check.sh` | 武器不能被画成手臂（fixture + 两 demo + oracle 见证） |
| `observer-focus-check.sh` | 观察目标解析 + oracle 见证 + **45 单位的归因**（相机原点被保持、录像者已死、逐轴拆分）+ **541 点普查**（13 个 follow 检查点、11 个落在录像者自己身上、最差 37.812 单位）；`--mutation` 三个扰动、要求**恰好 3 条红行**，在脚本内 |
| `slot-freshness-check.sh` | **选槽必须取后写的那个槽**：fixture 4 形状（含平局/无 tick 回落 + 渲染 API 一致）+ POV/bagel 对照 + 规则影响面（哪五个属性被复制）；`--mutation` 在脚本内 |
| `source-tv-coverage-check.sh` | **本机全部 9 份真实 SourceTV 录制整份解码**，逐份断言录制类型（头字段＋流内）、`index_tail_bytes` **等于文件字节数**（证明读完了整份）、钉住的包数/地图、以及四类零失败；`--mutation` 两个扰动各要求恰好 9 行红 |
| `frame-capture-check.sh` | **把「画面」变成可复核的产物**：从运行中的程序抓一帧（`Present()` 之前读 back buffer），断言 BMP 头与自身字节数自洽、同场景两次逐字节相同、demo 场景与暂停场景不同、非法 tick 退出 13；**缺输入记 FAIL 不记 SKIP**；`--mutation` 翻转一字节要求内容哈希察觉 |
| `oracle-corpus-check.py` | 在语料抽样上与 oracle 对照（带 selftest） |
| `corpus-census.py` | 全语料普查（`--demos-list` 钉输入集；`--workers 3 --resume`） |
| `corpus-header-scan.py` | 只读头的全语料普查 |
| `probe-peak.py` | 单份 demo 的耗时/内存峰值 |
| `run-corpus-evidence.sh` | 两个长语料运行，按顺序 |

**硬依赖（合并后必须保留）**：`verify-all.sh` 按**相对路径**调用上述所有脚本，
并且第 11/12/13/14/15/16 步分别调用 `history-coverage-check.sh` / `weapon-world-model-check.sh` /
`observer-focus-check.sh` / `slot-freshness-check.sh` / `source-tv-coverage-check.sh` /
`frame-capture-check.sh`。
缺一个，链条就断。第 15 步还依赖**本机的 9 份 SourceTV 录制**
（`D:/SteamLibrary/.../tf/demos/`，见 §7 第 5 条）；**第 16 步是本轮唯一依赖
「主程序能在这台机器上跑起来」的一步**（它会真启动 `tf2_demo_native.exe` 抓帧，
约 4 次启动 × 每次 ~20 s）—— 所以它在这台机器上会过，在**没有 GPU / 无桌面会话**的
CI 上会失败，这是**设计如此**（那条断言要证的就是「这台机器上画得出帧」）。
第 16 步还要求主程序**能出声**（`--audio-device 6`）—— 门禁里每条启动都写了这个参数。

---

## 5. 外围：文档（7 个）与证据（190 个）

**文档**

| 文件 | 内容 |
|---|---|
| `HANDOFF-2026-10-07.md` | **单一入口接手文档**（9 节）。合并后建议保留 |
| `ACCEPTANCE-P1-2026-10-06.md` | P1 验收：§10 逐 tick 位移 / §11 武器世界模型 / §12 观察目标 / §13 验收机器自己的缺陷 / §14 选槽新鲜度 / §15 45 单位的归因 / **§16 帧抓取** |
| `ACCEPTANCE-P0-2026-10-06.md` | P0 验收 |
| `HANDOFF-P0/P1-2026-10-06.md` | 上一轮交接（保留作对照） |
| `MERGE-MANIFEST-2026-10-08.md` | 本文件 |
| `README.md` | 索引 + 当前状态 |

**证据 `evidence/`（216 个 tracked）**

| 目录 | tracked | 是什么 |
|---|---|---|
| `evidence/mutation/` | 49 | 变异运行的红色读数（证明判据能红） |
| `evidence/after-fix/` | 27 | P0 修复后的探针 + oracle 对照 |
| `evidence/verify/` | 22 | **16 步链的逐步原始输出**（`1-build.txt` … `16-frame-capture.txt`，第 16 步另有 `16-frame-capture-mutation.txt`） |
| `evidence/observer-focus/` | 16 | 观察目标门禁原始读数（含 `pov-census.{json,txt}` 541 点普查与 `pov-e18-dead.txt` 死亡见证） |
| `evidence/frame-capture/` | 10 | **帧抓取门禁原始产物**：`paused.bmp` / `paused-again.bmp` / `demo.bmp` 及各自 `.log`、`.csv`，以及 `bad.log` —— **这是「接材质前后画面变没变」的唯一对照基线**（`mutated.bmp` 由脚本现生成，已 gitignore） |
| `evidence/probe-baseline/` | 11 | **冻结基线**（9 份探针报告 + 新增行基线）。增量性检查拿它当参照，**不要用新运行覆盖** |
| `evidence/final/` | 10 | P0 最终态探针报告 |
| `evidence/baseline/` | 9 | P0 修复前的对照 |
| `evidence/source-tv-coverage/` | 9 | **9 份真实 SourceTV 的完整探针输出**（不重跑就复核时读这里） |
| `evidence/weapon-world-model/` | 8 | 武器世界模型门禁原始读数 |
| `evidence/p1/` | 8 | P1 单点复现读数 |
| `evidence/slot-freshness/` | 7 | 选槽新鲜度门禁原始读数（POV 逐候选 `rank/name/lastWrite/age` + bagel 对照） |
| `evidence/history-coverage/` | 3 | 历史覆盖门禁读数 |
| `evidence/header-scan/` | 2 | 只读头的语料普查（**9 份 SourceTV 的名单出自这里**） |
| `evidence/corpus-calib/` | 2 | `demos.txt`（**钉住的 24 份输入集**）+ `corpus-summary.json` |
| `evidence/corpus-negative/` | 2 | 普查判据的失败清单与汇总 |
| `evidence/oracle-corpus/` | 1 | oracle 语料抽样汇总 |
| 其它根级 | 20 | `sdk-demoformat.h`（Valve 参考）、`metrics-*.csv`、`perf-*.txt`、各 `*.log` |

> `evidence/**/oracle-survey-*.txt`（约 94 MB）与 `evidence/corpus*/reports/`（1600+ 份）
> 是**派生数据**，被 `.gitignore` 排除，只在磁盘上。门禁读的是 `corpus-summary.json` +
> `corpus-failures.txt`，那两个是 tracked 的。

### ⚠️ 一个会咬人的陷阱：第 9 步依赖 gitignore 内的文件

`census-negative-test.sh` 的 `SRC=evidence/corpus-calib`，它做的是：

```bash
cp "$SRC"/reports/*.txt "$NEG/reports/"     # 24 份冻结校准报告
```

而 **`evidence/corpus-calib/reports/` 里那 24 份报告 0 个 tracked**
（被 `.gitignore:11` 的 `evidence/corpus*/reports/` 排除）。
只 `git clone` 或只搬 tracked 文件的话，**第 9 步会因为 `cp` 找不到文件而失败**。

两条解法，任选：

1. **把这份目录一起带过去**（`evidence/corpus-calib/reports/` 共 24 份，磁盘上有）；
2. **重新生成**：
   ```bash
   python corpus-census.py --demos-list evidence/corpus-calib/demos.txt   # 输出到 evidence/corpus-calib/reports/
   ```
   生成的报告内容应与冻结版一致（判据只要求它们 `clean=24 skipped=0 dirty=0`）。

同理，`evidence/corpus-negative/reports/` 也是运行时由脚本自己铺的，不需要搬。

---

## 6. 合并路径（两条，都已实测）

### 6.1 路径 A：只合并源码（**已实测 16/16 干净**）

```bash
cd <你的主线仓库>            # 基线 d585af8
git apply --check /d/TF2_Native_Test/merge-patches/00-native-combined.patch   # 通过
git apply         /d/TF2_Native_Test/merge-patches/00-native-combined.patch
```

实测结论（**2026-10-08 第七轮重跑**，基线 `7ab4e72:native` = `887a2f31…`）：

- 13 个提交的 `native/` 部分合起来**一次 `git apply` 全部通过**（在主线 `d3e2b7c` 上
  `git apply --check` 通过，随后 `git apply` 成功）
- 应用结果与测试树的 `native/` **逐文件相同**：排除 `native/docs/`（主线专有文档）后，
  **87 个代码文件 blob 哈希 `IDENTICAL=87 DIFFERS=0`**
- 本轮补丁**涉及 19 个文件**（17 改 + 2 新增：`entity_message_fixture_probe.cpp`、
  `entity_protocol_probe.cpp`；比第二轮多 3 个 —— 第七轮新增的
  `native_renderer.h` / `native_renderer.cpp`，以及 `native/README.md` 的 11 行参数说明）
- **`native/docs/` 下 3 个 markdown 会与主线不同**（`HANDOFF-2026-10-06.md`、
  `TASKS-2026-10-06.md` 被另一路 AI 会话改过，`CHANGELOG-NATIVE.md` 是它新增的）——
  **它们不在补丁范围内**，合并时取主线版本即可
- 复现命令（**注意路径写法**：`git apply` 不认 MSYS 的 `/d/...`，要用 `D:/...`）：

```bash
cd <你的主线仓库>            # 基线 d585af8，本轮实测时是 d3e2b7c
git apply --check "D:/TF2_Native_Test/merge-patches/00-native-combined.patch"   # 通过
git apply         "D:/TF2_Native_Test/merge-patches/00-native-combined.patch"
git add -A native/ && git ls-tree -r $(git write-tree) native/ | grep -v 'native/docs/'  # 与测试树比 blob
```

> ⚠️ **主线已经不是我上次记的 `d585af8` 了。** 现在是 `d3e2b7c`（`d585af8` 的后继），
> 作者仍是 `Codex Native`，提交时间 `Thu Oct 8 12:42:31 2026 +0800` ——
> **另一路 AI 会话正在同一台机器上动主线**（这是常态，不是异常）。
> 好消息是那次提交只改了 `native/docs/` 里的 3 个 markdown（+56/−4），
> **一个代码文件都没动**，所以补丁照样干净。合并前请再确认一次主线 HEAD。
> 也正因为如此，**路径 B 合并文档时会在这 3 个文件上冲突**，取主线版本。

`merge-patches/` 里还有**逐提交**的 13 份补丁（`658ae69-*.patch` … `3d03719-*.patch`，
文件名带提交短哈希与摘要），要逐个 review 就用它们。

⚠️ **复现时的一个坑（我自己踩了两次）**：`git apply` 的上下文是 **LF**，
而本机 `core.autocrlf=true` 会让 `git archive` **把 LF 转成 CRLF**，
于是解出来的基线文件行尾和补丁对不上，报一堆 `patch does not apply`。
用 `git -c core.autocrlf=false archive …` 解，或直接
`git clone -c core.autocrlf=false`。**行尾差异不是内容差异**，先看树对象对不对再怀疑补丁。

### 6.2 路径 B：合并全部 42 个提交（含判据与证据）

```bash
git clone -c core.autocrlf=false "D:/TF2_Demo_Player/work/native-mvp-source" work
cd work
git remote add testtree "D:/TF2_Native_Test"
git fetch testtree p0-entity-protocol
git cherry-pick 658ae69 226d119 22dc465 8c6f06e 4868e7b 9316416 \
               6cb0ecf aa93926 cd36db1 c0ff710 d61b3d6 66cd97f   # 或直接 merge testtree/p0-entity-protocol
```

实测结论（10 个提交时跑通，`native/` 100% 落地）：

- **`native/` 从不冲突**。冲突全部落在判据/证据/文档文件上
- 冲突清单：`.gitignore`（add/add）、`mutate.sh`、`verify-all.sh`、
  `check-probe-output-additive.sh`、`evidence/cmake-build.log`、`evidence/probe-baseline/added-lines.txt`、
  以及若干 `evidence/final/*`、`evidence/mutation/*`、两份 P0 文档
- **解冲突原则**：这些文件在主线里本来不存在（或属测试树专有），**一律取测试树版本**
  （`git show <sha>:<file> > <file> && git add <file>`）即可
- **`.gitignore` 要取并集**，不要二选一：主线那份有 `node_modules/`、`work/`、`release-*/`、
  `/*.log`、`dist.bak*/` 等产品规则，测试树那份有 `evidence/**/oracle-survey-*.txt`、
  `evidence/corpus*/reports/`、`evidence/corpus*/corpus.csv`。两边都有 `native/build/`、
  `.scratch/`；主线的 `native/build-*/` 已覆盖测试树的 `native/build-nmake/`

> **诚实标注（2026-10-08 第二轮）**：上面这份冲突清单是**10 个提交时**跑的，
> **新增的 2 个提交（`d61b3d6` / `66cd97f`）之后没有重跑整条 cherry-pick**。
> 可以确定的是它们的 `native/` 部分已经随路径 A 重测干净（16/16），
> 且它们**碰的文件全都已在上面那份冲突清单里**（`mutate.sh`、`verify-all.sh`、
> `slot-freshness-check.sh`、`observer-focus-check.sh`、`evidence/*`）——
> 也就是说**不会新增冲突文件**，但这是推理，不是重跑的读数。
> 另注：主线 `d3e2b7c` 已经改过 `native/docs/` 里的 3 个 markdown，
> 而测试树里也有同名的旧版本，**这 3 个文件合并时会冲突，取主线版本**。
>
> **第三轮补充（2026-10-08 晚，`3a935a3` + `18fe7ea`）**：这两个提交**同样没有**重跑
> 路径 B。它们新增的文件只有 `source-tv-coverage-check.sh` 与
> `evidence/source-tv-coverage/*.txt`、`evidence/verify/15-*.txt` —— 全是**测试树专有**的
> 新文件，主线里不存在，所以按上面的原则「取测试树版本」即可，
> **不会产生新的冲突类型**。仍然是推理，不是读数。
>
> **一句话给整合者**：路径 A（只并 `native/`，16 文件）已经实测干净且本轮未变；
> 路径 B 需要真跑一遍才能确认冲突清单，**别照抄这份清单，跑完再改它**。

### ⚠️ `core.autocrlf` 陷阱（踩过，写下来）

两边仓库 `core.autocrlf` 都是 `true`。**必须在 clone 时**设：

```bash
git clone -c core.autocrlf=false <repo> <dir>     # 对
git clone <repo> <dir> && git config core.autocrlf false   # 错：工作区立刻全变「已修改」
```

不这样做，cherry-pick 会报一堆假冲突（我实测过：同一个操作，
`autocrlf=true` 下冲突 4 个文件，`autocrlf=false` 下只冲突 `.gitignore`）。
另注：测试树里 `native/tools/entity_protocol_probe.cpp` 的工作区副本是 **LF**，
其余源码是 **CRLF** —— 这是检出残留，不是内容差异（blob 哈希相同）。

---

## 7. 合并后必须做的六件事

1. **把 `evidence/corpus-calib/reports/`（24 份，gitignore 内）一起带过去**，
   或按 §5 的方法重新生成。否则第 9 步会失败。
2. **重跑变异套件**。`mutate.sh` 靠**文本模式匹配**改源码；任何格式化/重排都会让
   某个用例 `PATTERN NOT FOUND` → 用例被 `continue` 跳过 → 断言数悄悄变少而
   `MUTATION-SUITE=PASS` 照样打印。**这是本项目抓到过的真实事故。**
3. **重跑 16 步链**，确认末行仍是 `VERIFY=PASS`：
   ```bash
   git status --porcelain -- native/     # 必须为空（mutate.sh 的干净树守卫）
   bash verify-all.sh --quick            # 约 40 分钟
   ```
   预期：`CENSUS-NEGATIVE=PASS (pinned_reports=24)`、
   `OBSERVER-FOCUS=PASS (assertions_ok=46, camera-to-target=45u)` 及其
   `OBSERVER-FOCUS-MUTATION=PASS (3/3 perturbations caught, red_lines=3)`、
   `SLOT-FRESHNESS=PASS (assertions_ok=24, POV chosenStale=0)`、
   `SOURCE-TV-COVERAGE=PASS (demos=9 packets=917543 assertions_ok=47)`、
   **`FRAME-CAPTURE=PASS (assertions_ok=16)` + `FRAME-CAPTURE-MUTATION=PASS`**、`VERIFY=PASS`。
   （链条现为 **16 步**；第 13 步那个数在第二轮由 2729 变成 45、断言 39 → 46（第四轮）；
   第 15 步在第三轮加入；**第 16 步在本轮加入，它需要主程序能在这台机器上跑起来**。
   第 13 步耗时约 2 分钟，**第 16 步约 1.5 分钟**（4 次启动 × ~20 s）。）
4. **检查 `evidence/probe-baseline/` 是否仍是旧二进制的产物**。它是**冻结基线**；
   如果你改了探针的输出格式，增量性检查会（正确地）变红 —— 那是要你确认的行为变化，
   不是 bug。要更新基线得用 `--refresh-added` 并重新提交。
5. **第 15 步需要本机那 9 份 SourceTV 录制**（`D:/SteamLibrary/.../tf/demos/` 下的
   `73.dem`、`SUNSHINE.dem`、`gullyscout.dem`、`proc2.dem`、`processdemo.dem`、
   `prodcutscout.dem`、`review1.dem`、`review12.dem`、`review44.dem`，共 664 MB）。
   缺任何一份 → 该份记 **FAIL**（不是 SKIP），链条红。这是刻意的：
   覆盖门禁在输入缺失时必须是红的，否则它就成了一个装饰。
   录制的原始读数留在 `evidence/source-tv-coverage/`，不重跑也能看。
6. **第 16 步需要一张显卡 + 一个能出声的设备**（它真启动主程序抓帧，并传
   `--audio-device 6`）。在无 GPU / 无桌面会话的机器上它会红——**这是设计如此**，
   因为它要证的就是「这台机器上画得出帧」。要在一个不能跑图形的主机上跳过，
   应该显式删掉第 16 步，**而不是让它静默变成 SKIP**（那正是本轮的判据铁律要禁的）。

---

## 8. 红线

1. **不要改 `D:\TF2_Demo_Player`**。它是只读源目录（多 worktree）。
   尤其 `work\wt-P0-sourcetv-fix` 里的未提交改动是**另一路 AI 会话的在制品，不要覆盖**。
2. **会重编译的脚本（`mutate.sh` / `build-cmake.sh` / `build-target.sh`）必须独占运行。**
   它们重写 `native/build-nmake/*.exe`；并发的探针脚本会读到变异版二进制，两边读数一起作废。
3. **`verify-all.sh` 必须在已提交的树上跑**（`mutate.sh` 用 `git checkout -- native/` 恢复，
   工作区脏时它会拒绝运行 —— 这是正确行为）。
4. **本机 `vcvars64.bat` 不可用**（调 `reg.exe`，被沙箱黑名单）。CMake 只能用
   `-G "NMake Makefiles"`，且要显式设 `INCLUDE`/`LIB` + 把 SDK 的 `bin\10.0.26100.0\x64`
   加进 `PATH`。`CMakeLists.txt` **只认 `#` 注释，写 `//` 会语法错。**
5. **会出声的测试一律 `--audio-device 6`**（`耳机 (xduoo audio)`）；不传会落到系统默认设备。
   ⚠️ **`--start-paused` 不豁免**：音频设备在**启动阶段**就打开，暂停只停播放。
   本轮的 `frame-capture-check.sh` 第一版就是漏了这个参数、在系统默认设备上出声
   （见 `ACCEPTANCE-P1-2026-10-06.md` §16.7）—— **写任何新的运行型门禁，
   第一件事就是给每条启动加这个参数。**
6. **判据要带工作量计数**；缺输入记**失败**不记跳过。
7. **读数变红先怀疑仪器**，不要先怀疑刚改的代码。
8. **第 16 步会真启动图形程序**：它与其他会自动启动主程序的脚本一样，**不能并发跑**，
   否则会同时占 GPU / 音频设备，读数互相污染（还可能在错误的设备上出声）。

---

## 9. 复核锚点

| 范围 | `sha256` |
|---|---|
| `git diff 7ab4e72..HEAD -- native/`（全部源码改动） | `c09742a1168c3829dd3800ac09bbad1fae867c3d39e1e1acb78bf0bb7bdfd339` |
| `merge-patches/00-native-combined.patch`（与上一行同一份字节，257962 B） | `c09742a1168c3829dd3800ac09bbad1fae867c3d39e1e1acb78bf0bb7bdfd339` |
| `native/` 树对象（合并后应与测试树相同，**排除 `native/docs/`**） | 代码 87 文件逐个相同（`IDENTICAL=87 DIFFERS=0`） |
| 基线 `native/` 树对象（两边同源，= `7ab4e72:native` = `d585af8:native`） | `887a2f31b9d0097438f44392464ad139133e1efc` |

> **本机 exe 哈希不可复现**（两次强制重链接差 2 字节）→ 用**源码补丁哈希 + 读数**做锚点，
> 不要用 exe 哈希。
>
> **2026-10-08 第七轮的复核（第一次改动 `native/`，所以锚点终于动了）**：
> 补丁 sha256 `0b583685…` → **`c09742a1…`**、字节数 245595 → **257962 B**、
> `HEAD:native` 树对象 `ece80926…` → **`a6d1944d…`**、碰 `native/` 的提交 12 → **13**。
> **基线 `7ab4e72:native` = `887a2f31…` 未变**（基线本来就该不动）。
> 上一轮（第三、四轮）连续两次外围改动时这四个值**逐字节没动**，本轮因为真的改了代码而全部变化
> —— 这正好反过来印证了「认基线用 blob 哈希」比「数提交条数」可靠：
> **条数会因文档提交一直涨，哈希只在代码真变时才变。**
>
> 上一轮的锚点保留作对照（说明你手上是旧补丁）：补丁 sha256 `0b583685…`、
> 合并后树对象 `ece80926…`；更旧一轮：`488fc828…` / `6ba1d19b…`。

测试树状态：分支 `p0-entity-protocol`，**`native/` 干净**（`git status --porcelain -- native/` 为空），
但 `evidence/` 下有 6 个文件带着**上一轮链条重跑留下的时值漂移**（见下）。

### ⚠️ 证据里有 6 个文件「每次跑都会变」—— 变的全是时值，不是结论

重跑链条会把这几个文件**重写一遍**，`git diff` 因此常驻非空。逐个核对过，**差异只在时值上**：

| 文件 | 差异内容 | 结论性数字是否变 |
|---|---|---|
| `evidence/verify/1-build.txt` | 15 行 `?⒁? 包含文件: …`（MSVC `/showIncludes` 的追踪噪声） | **否**：`warning C` 条数 2 → 2、`errors=0` |
| `evidence/verify/5-oracle-corpus.txt` | 各 demo 的 `xx.xs` 与 `wall_seconds` | **否**：`112172/112172`、`3030444/3030444`、`agreed=8 mismatched=0` 全部逐字未变 |
| `evidence/oracle-corpus/oracle-corpus-summary.json` | `wall_seconds` 52.1 → 44.7 | **否**：`sampled/agreed/mismatched/sum_*` 未变 |
| `evidence/frame-capture/paused.bmp.csv` | `elapsed_seconds`/`fps`/内存字节 | **否**：`tick=0`、`rendered_frames=1` |
| `evidence/frame-capture/paused-again.bmp.csv` | 同上 | **否**：同上 |
| `evidence/frame-capture/demo.bmp.csv` | 同上 | **否**：`tick=16`、帧数 0→1 |

**为什么是这 6 个**：`oracle-corpus-check.py:143,204` 写的是
`round(time.perf_counter() - started, 1)` —— **墙钟测量，本质不确定**；
`1-build.txt` 的 include 行是 MSVC 的追踪输出，**顺序与内容也不确定**；
三个 `.csv` 是 `--metrics-file` 的时值侧产物。**没有任何一处来自被测代码。**
可机械复核（把「包含文件」行剔除后逐行比）：

```bash
diff <(git show e43a3f3:evidence/verify/1-build.txt | grep -v '包含文件') \
     <(grep -v '包含文件' evidence/verify/1-build.txt)   # 无输出 = 除追踪外完全一致
```

> **合并建议**：这 6 个文件**要带过去（它们是证据），但不要因为 `git diff` 非空就以为
> 代码变了**。判断代码有没有动，只看 `git diff --stat <基线>..HEAD -- native/`（§9 锚点）。
> 若要让它们保持逐字节稳定，需要在生成脚本里把时值字段归一化（例如写 `wall_seconds=0`
> 或集中到一个 `timings.json`）—— **本轮没做**，因为这会改动第 5 步的判据脚本，
> 而那属于「改仪器去迁就证据外表」，得先问清楚再动。

**最后一次碰 `native/` 的提交是 `3d03719`**（本轮）—— 用它做基线，**不要用「当前 HEAD」**
（文档提交会一直把 HEAD 往前推：`290db52` → `23db4af` → `f9a1b0f` → `8aa3c95` → …，
上面几个锚点却只在代码真变时才动，这正是「认基线用 blob 哈希」的用处）。

**16 步链已在 `e43a3f3` 上重跑**（本轮把链条从 15 步拓到 16 步，新增帧抓取门禁）：
**`VERIFY=PASS`**（RC=0，**38m27s**，16 步全绿）。**第一次跑 `3d03719` 是 `VERIFY=FAIL`，
唯一红的是第 16 步** —— `verify-all.sh` 把断言数写死成 15 而门禁实际打印 16 条，
**是验收机器抓住了作者自己的数错**（与 `3ca75ae` 同类），修正后转绿。
读数见 §7 第 3 条与 `evidence/verify/1..16-*.txt`。
上一轮 **15 步链在 `290db52` 上 `VERIFY=PASS`**（RC=0，**36m20s**，15 步全绿）：第六轮只改了
第 13 步门禁与文档、`native/` 一个字节未改，其余 14 步读数与 `18fe7ea` 那次**逐条相同**，
第 13 步按新期望（`assertions_ok=46`、变异 3/3 恰 3 行红）。
