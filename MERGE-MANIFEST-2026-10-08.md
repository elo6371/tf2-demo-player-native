# 合并清单（给整合 AI）— 2026-10-08

> **一句话**：源码改动只有 **10 个提交 / 16 个文件 / +3427 −242**，已实测能干净应用到主线
> `d585af8`；**其余 200 个文件全是源码之外的东西**（判据脚本、证据、文档），
> 它们不进产品，但**没有它们就无法复核这批源码**。
>
> 本清单里的每条结论都做过实测，命令与预期读数写在 §6/§7。

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
本轮（2026-10-08）我只改了测试树里的判据脚本与文档/证据，`native/` 一个字节没动。

---

## 2. 源码 vs 外围：分界

`git diff --name-status 7ab4e72..HEAD` 一共 **216 个路径**：

| 类别 | 文件数 | 进产品？ | 说明 |
|---|---|---|---|
| `native/`（源码） | 16 | **是** | 10 个提交，+3427 −242 |
| 判据脚本（仓库根） | 19 | 否 | 复核源码用，不参与产品构建 |
| 文档 | 6 | 否 | 验收/交接文档 |
| 证据 `evidence/` | 173 | 否 | 读数与产物（含冻结基线） |
| 补丁 `merge-patches/` | 11 | 否 | 本轮新生成，方便你合并 |
| `.gitignore` | 1 | 视情况 | 与主线**冲突**（见 §6.2） |

**提交总数 `7ab4e72..HEAD` = 35**，其中**只有 10 个碰 `native/`**。
剩下 25 个是纯文档/证据/判据提交 —— 判断源码有没有被动过要看
`git log --oneline 7ab4e72..HEAD -- native/`，不要数提交条数。

---

## 3. 源码改动（10 个提交，要合并进产品的部分）

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

### ⚠️ 三个合并时必须知道的行为变化

1. **`src/entity_model.cpp`、`src/demo_header.cpp` 是主程序 `tf2_demo_native` 的源文件**
   （`native/CMakeLists.txt` 的 `add_executable` 列表）。合并它们 = 主程序行为变化。
   若只想保留解析能力、不要行为变化，可单独回退：
   `git checkout d585af8 -- native/src/entity_model.cpp`（代价：玩家 Z 回到 0）。
2. **`main.cpp` 只被 `aa93926` 动过 +9/−1**，只加了一行 `stale=` 读数。
   `cd36db1` / `c0ff710` 两轮 `main.cpp` 一个字没动。
3. **`c0ff710` 的观察目标刻意不接线**：按 `m_iObserverMode` 把相机搬到目标，
   在这份 POV demo 上会把画面搬走 **2729 单位**（该槽还落后 checkpoint 2379 tick）。
   合并时**不要顺手把它接上**，那是有实测依据的拒绝，不是没做完。

---

## 4. 外围：判据脚本（19 个，源码之外的核心交付）

这些是「源码之外的活」的主体。**每个都带工作量计数**（`compared=` / `exe_count=` /
`pinned_reports=` / 断言条数），因为「没有失败」在输入缺失时恒真。

| 脚本 | 用途 |
|---|---|
| `verify-all.sh` | **一条命令跑完 13 步验收链**，末行 `VERIFY=PASS`/`FAIL`。支持 `--quick` |
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
| `observer-focus-check.sh` | 观察目标解析 + oracle 见证 + 「跟着走要搬 2729 单位」的分歧读数；`--mutation` 在脚本内 |
| `oracle-corpus-check.py` | 在语料抽样上与 oracle 对照（带 selftest） |
| `corpus-census.py` | 全语料普查（`--demos-list` 钉输入集；`--workers 3 --resume`） |
| `corpus-header-scan.py` | 只读头的全语料普查 |
| `probe-peak.py` | 单份 demo 的耗时/内存峰值 |
| `run-corpus-evidence.sh` | 两个长语料运行，按顺序 |

**硬依赖（合并后必须保留）**：`verify-all.sh` 按**相对路径**调用上述所有脚本，
并且第 11/12/13 步分别调用 `history-coverage-check.sh` / `weapon-world-model-check.sh` /
`observer-focus-check.sh`。缺一个，链条就断。

---

## 5. 外围：文档（6 个）与证据（173 个）

**文档**

| 文件 | 内容 |
|---|---|
| `HANDOFF-2026-10-07.md` | **单一入口接手文档**（9 节）。合并后建议保留 |
| `ACCEPTANCE-P1-2026-10-06.md` | P1 验收：§10 逐 tick 位移 / §11 武器世界模型 / §12 观察目标 / §13 验收机器自己的缺陷 |
| `ACCEPTANCE-P0-2026-10-06.md` | P0 验收 |
| `HANDOFF-P0/P1-2026-10-06.md` | 上一轮交接（保留作对照） |
| `README.md` | 索引 + 当前状态 |

**证据 `evidence/`（173 个 tracked）**

| 目录 | tracked | 是什么 |
|---|---|---|
| `evidence/mutation/` | 41 | 变异运行的红色读数（证明判据能红） |
| `evidence/after-fix/` | 27 | P0 修复后的探针 + oracle 对照 |
| `evidence/verify/` | 16 | **13 步链的逐步原始输出**（`1-build.txt` … `13-observer-focus.txt`） |
| `evidence/observer-focus/` | 13 | 观察目标门禁原始读数 |
| `evidence/probe-baseline/` | 11 | **冻结基线**（9 份探针报告 + 新增行基线）。增量性检查拿它当参照，**不要用新运行覆盖** |
| `evidence/final/` | 10 | P0 最终态探针报告 |
| `evidence/baseline/` | 9 | P0 修复前的对照 |
| `evidence/weapon-world-model/` | 8 | 武器世界模型门禁原始读数 |
| `evidence/p1/` | 8 | P1 单点复现读数 |
| `evidence/history-coverage/` | 3 | 历史覆盖门禁读数 |
| `evidence/header-scan/` | 2 | 只读头的语料普查 |
| `evidence/corpus-calib/` | 2 | `demos.txt`（**钉住的 24 份输入集**）+ `corpus-summary.json` |
| `evidence/corpus-negative/` | 2 | 普查判据的失败清单与汇总 |
| `evidence/oracle-corpus/` | 1 | oracle 语料抽样汇总 |
| 其它根级 | ~20 | `sdk-demoformat.h`（Valve 参考）、`metrics-*.csv`、`perf-*.txt`、各 `*.log` |

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

### 6.1 路径 A：只合并源码（**已实测 10/10 干净**）

```bash
cd <你的主线仓库>            # 基线 d585af8
git apply --check /d/TF2_Native_Test/merge-patches/00-native-combined.patch   # 通过
git apply         /d/TF2_Native_Test/merge-patches/00-native-combined.patch
```

实测结论：

- 10 个提交的 `native/` 部分**逐个 `git apply --check` 全部通过**（10 干净 / 0 冲突）
- 应用结果与测试树的 `native/` **树对象完全相同**：`6ba1d19b2c741b1658da64250cc54bf370307344`

`merge-patches/` 里还有**逐提交**的 10 份补丁（`01-658ae69-*.patch` … `10-c0ff710-*.patch`），
要逐个 review 就用它们。

### 6.2 路径 B：合并全部 35 个提交（含判据与证据）

```bash
git clone -c core.autocrlf=false "D:/TF2_Demo_Player/work/native-mvp-source" work
cd work
git remote add testtree "D:/TF2_Native_Test"
git fetch testtree p0-entity-protocol
git cherry-pick 658ae69 226d119 22dc465 8c6f06e 4868e7b 9316416 \
               6cb0ecf aa93926 cd36db1 c0ff710        # 或直接 merge testtree/p0-entity-protocol
```

实测结论（已跑通，10/10 落地）：

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

## 7. 合并后必须做的四件事

1. **把 `evidence/corpus-calib/reports/`（24 份，gitignore 内）一起带过去**，
   或按 §5 的方法重新生成。否则第 9 步会失败。
2. **重跑变异套件**。`mutate.sh` 靠**文本模式匹配**改源码；任何格式化/重排都会让
   某个用例 `PATTERN NOT FOUND` → 用例被 `continue` 跳过 → 断言数悄悄变少而
   `MUTATION-SUITE=PASS` 照样打印。**这是本项目抓到过的真实事故。**
3. **重跑 13 步链**，确认末行仍是 `VERIFY=PASS`：
   ```bash
   git status --porcelain -- native/     # 必须为空（mutate.sh 的干净树守卫）
   bash verify-all.sh --quick            # 约 32 分钟
   ```
   预期：`CENSUS-NEGATIVE=PASS (pinned_reports=24)`、
   `OBSERVER-FOCUS=PASS (assertions_ok=39, camera-to-target=2729u)`、`VERIFY=PASS`。
4. **检查 `evidence/probe-baseline/` 是否仍是旧二进制的产物**。它是**冻结基线**；
   如果你改了探针的输出格式，增量性检查会（正确地）变红 —— 那是要你确认的行为变化，
   不是 bug。要更新基线得用 `--refresh-added` 并重新提交。

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
6. **判据要带工作量计数**；缺输入记**失败**不记跳过。
7. **读数变红先怀疑仪器**，不要先怀疑刚改的代码。

---

## 9. 复核锚点

| 范围 | `sha256` |
|---|---|
| `git diff 7ab4e72..HEAD -- native/`（全部源码改动） | `488fc828f32693d3c1bedf6092ed6784cd3312692b57b8dd863ef6222687415c` |
| `merge-patches/00-native-combined.patch`（与上一行同一份字节） | `488fc828f32693d3c1bedf6092ed6784cd3312692b57b8dd863ef6222687415c` |
| `native/` 树对象（合并后应与测试树相同） | `6ba1d19b2c741b1658da64250cc54bf370307344` |
| 基线 `native/` 树对象（两边同源） | `887a2f31b9d0097438f44392464ad139133e1efc` |

> **本机 exe 哈希不可复现**（两次强制重链接差 2 字节）→ 用**源码补丁哈希 + 读数**做锚点，
> 不要用 exe 哈希。

测试树当前状态：HEAD = `79ffc8b`，分支 `p0-entity-protocol`，工作区干净，
13 步链 `VERIFY=PASS`。
