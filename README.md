# TF2_Native_Test

P0（SourceTV PacketEntities 状态重建）与 P1 第一项（实体模型引用接线）的隔离测试树。
P1 共四个提交：模型引用接线（`8c6f06e`）、接线暴露出的 VPK 目录重复解析性能悬崖
（`4868e7b`）、补上该修复漏掉的链接依赖（`9316416`），以及记录与判据加固（`f38bcf6`）。

- 源目录 `D:\TF2_Demo_Player` **本次未改动**（`work/native-mvp-source` 仍是
  `d585af8`，`git status` 干净）。
- 本目录由 `git archive d585af8 native | tar -x` 建立，独立 git 仓库，
  分支 `p0-entity-protocol`。

## 入口

| 文件 | 用途 |
|---|---|
| `ACCEPTANCE-P0-2026-10-06.md` | **P0 主验收文档**，按清单的强制验收清单逐项填写 |
| `HANDOFF-P0-2026-10-06.md` | P0 的可并入主线的交接条目 |
| `ACCEPTANCE-P1-2026-10-06.md` | **P1 第 1 项验收**：`m_nModelIndex` → `modelprecache` → 可加载路径 |
| `HANDOFF-P1-2026-10-06.md` | P1 第 1 项的交接条目（含对旧文档 `requests=0` 归因的更正） |
| `fix-P0-entity-messages.patch` | 只含 `native/` 的协议修复补丁（**不含** `226d119` 的分类器修正） |
| `evidence/probe-baseline/` | **冻结**的 9 份探针报告（`recording_stream=` / `index_state=` 两行加入之前的二进制产出），供 `check-probe-output-additive.sh` 当基线。不要用 `run-demos.sh` 覆盖它 |
| `evidence/probe-baseline/added-lines.txt` | 同上，冻结的是 P1 新增的两行（`sound_precache_entries=` / `asset_refs=`）。剥离的行也要有基线，否则「additive」等于「没人验证」 |
| `evidence/p1/` | P1 的原始读数：`protocol-*.fixed.txt` / `model-*.fixed.txt`（探针）、`p0-baseline.csv`（P0 对照主程序）、`bagel-after-fix.csv`（修复后主程序）、`m3.csv` / `m5.csv`（对照运行） |
| `verify-all.sh` | 一条命令跑完整证据链（9 步：构建 → 9 份普查 → fixture → oracle → **oracle 语料抽样** → 录制类型 → 探针增量性 → 变异 → 普查判据可证伪）；`--quick` 把语料抽样降到 8 份 |

## 命令

```bash
cd /d/TF2_Native_Test

bash build-cmake.sh          # 干净全量 Release 构建（NMake，21 个 exe）
bash build-target.sh entity_protocol_probe entity_model_probe   # 只重建单个目标
bash run-demos.sh evidence/final          # 9 份 demo 协议普查
bash check-oracle.sh "<oracle.exe>" evidence/final   # 与 Rust oracle 逐值对照
bash oracle-recording-types.sh            # 钉住 9 份 demo 的 POV/SourceTV 判定（头字段 + 流内 STV 位）
bash check-probe-output-additive.sh       # 证明探针新增输出行没动旧计数器，且新增行本身未漂移
bash check-probe-output-additive.sh --refresh-added   # 只在刻意改过探针输出后刷新新增行基线
bash mutate.sh               # 变异验证（证明判据能变红）；8 个用例，25 条断言
bash census-negative-test.sh # 证明普查判据能变红（10 个变异）
bash verify-all.sh --quick   # 上面全部串起来

# P1：模型引用读数的单点复现
TF="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
POV="$TF/demos/autorecord_2026-07-02_13-26-46.dem"
./native/build-nmake/entity_protocol_probe.exe "$POV" | grep -E 'precache_entries|asset_refs='
./native/build-nmake/entity_model_probe.exe --tf-root "$TF" --demo "$POV"

# P1：主程序读数（会出声，必须 --audio-device 6）
BAGEL="D:/TF2_Demo_Player/testdata/demos/4a9bfb9276509d0ec5f5fdc722a95b17_match-20260927-0239-koth_bagel_rc13.dem"
timeout 60 ./native/build-nmake/tf2_demo_native.exe --tf-root "$TF" --demo "$BAGEL" \
  --audio-device 6 --metrics-file "D:/TF2_Native_Test/evidence/p1/bagel-after-fix.csv"
head -3 evidence/p1/bagel-after-fix.csv    # 首帧应在 ~18 s 出现

# 只读头 1072 字节的独立 Python 头解析器，可覆盖全量语料（秒级）
python corpus-header-scan.py --demos-dir "D:/SteamLibrary/steamapps/common/Team Fortress 2/tf/demos"
# 语料抽样与 Rust oracle 逐值对照（自带 --selftest）
python oracle-corpus-check.py --sample 40 --workers 2

# 全语料普查（1644 份 / 40.2 GB，3 并发约 4 小时；--resume 可断点续跑）
python corpus-census.py \
  --demos-dir "D:/SteamLibrary/steamapps/common/Team Fortress 2/tf/demos" \
  --outdir evidence/corpus-full --workers 3 --timeout 1800 --resume

# 上面两步串起来（先 oracle 抽样再全量，避免内存撞车）
bash run-corpus-evidence.sh
```

## 探针

| exe | 说明 |
|---|---|
| `native/build-nmake/entity_protocol_probe.exe` | 协议普查：包数、消息类型直方图、实体失败坐标、实例基线、precache 表（`soundprecache` / `modelprecache`）、资产引用分解、录制类型（头字段 + 流内 STV 位） |
| `native/build-nmake/entity_message_fixture_probe.exe` | 58 个 bit-exact 合成 wire fixture，覆盖 baseline/delta/Preserve/Leave/Delete |
| `native/build-nmake/presentation_probe.exe` | 视图数学 / 录制类型分类 / 投射物字段 / 全跨度 seek 自检 |
| `native/build-nmake/entity_model_probe.exe` | 资产引用 → 渲染请求；`--tf-root` + `--demo` 给出 `requests` / `demoRenderable` / `vpkExtracts`；`--self-test` 校验实例矩阵 |
| `native/build-nmake/tf2_demo_native.exe` | 主程序 |

## 注意

- 会出声的测试**必须**带 `--audio-device 6`（`耳机 (xduoo audio)`）。
  不传会落到系统默认设备（`WAVE_MAPPER`）。
- 本机 `-G "Visual Studio 17 2022"` 生成器不可用（`vcvars64.bat` 要调被沙箱
  拦掉的 `reg.exe`）。`build-cmake.sh` 顶部注释写了 NMake 走通的两个前置条件。
- 本机 exe 哈希**不可复现**（两次强制重链接差 2 字节）。
  复核锚点用源码哈希 + 编译命令，见验收文档 §8。
- `evidence/final/oracle-*.txt` 约 94 MB，已 gitignore，可随时用
  `check-oracle.sh` 重新生成。
- **`mutate.sh` / 任何会重编译的脚本必须独占运行。** 它们会重写
  `native/build-nmake/*.exe`，并发的探针脚本会读到变异版二进制。
  2026-10-06 已经因此产生过一次「读数不可比」的现场（`oracle-recording-types.sh`
  与 `mutate.sh` 撞车），两边的结果都作废重跑。
- **判据必须写「确实查了 N 件」，不能只写「没有失败」。** 后者在输入缺失时恒真。
  2026-10-06 踩过：`check-probe-output-additive.sh` 用仓库相对路径，
  在只有 `native/` 的隔离树里 9 份全部 `SKIP` 却仍报 PASS；fixture 步只断言
  `fixture_failures=0` 而不断言跑过几个；构建步只断言 `rc=0` 而不断言产出几个 exe；
  变异步只断言 `MUTATION-SUITE=PASS` 而不断言跑了几个用例。
  现在四处都加了工作量计数（`compared=` / `^PASS` 行数 / `exe_count=` / `MUTATION-RED` 行数）。
- **被「剥离」的行等于没人验证的行。** 增量性检查剥掉新输出行才能比旧计数器，
  但剥掉之后那几行就再没有判据了。现在它们有独立的冻结基线
  （`evidence/probe-baseline/added-lines.txt`）。
- **VPK 目录只能解析一次。** `VpkArchive::open()` 会把整个 `_dir.vpk` 目录树读进内存建哈希表
  （`tf2_misc_dir.vpk` 几十 MB、约 5 万条目）。曾经 `ModelLoader::resolveAsset` 每查一个模型
  就重开一遍所有 `_dir.vpk`，还要为 5 个伴生后缀各再来一遍 —— 505 条路径下光重复解析就 90 秒。
  归档现在挂在 `AssetRoot` 上（`VpkArchiveSet`），只解析一次。
  **改 `resolveAsset` / `hasCompanion` / `buildRenderRequests` 时不要退回去逐次 `VpkArchive::open`。**
- **`asset_root.cpp` 与 `vpk_archive.cpp` 是一对。** `AssetRoot::archives()` 调用
  `VpkArchiveSet::openDirectory`，所以任何编译 `asset_root.cpp` 的目标都必须同时链接
  `vpk_archive.cpp`。2026-10-06 漏了 `native_install_smoke_probe` → 干净构建 LNK2019 →
  NMAKE `Stop.` → 后面 4 个目标没建 → 门禁 `exe_count=17`（期望 21）。
  **加新目标时用 `grep asset_root.cpp CMakeLists.txt` 核对一遍。**
- **读数变红先怀疑仪器，不要先怀疑刚改的代码。** 2026-10-06 我把 `entity_model_probe`
  在 bagel 上的 117 秒读成了「卡死」（其实是我给的 `timeout` 比它短），
  差点去查一个不存在的回归。给足时间后它正常返回。同类事故还有 `mutate.sh` 的
  `PATTERN NOT FOUND`：真因是重构让补丁模式过期，不是代码出错。
- **语料是活的。** `tf/demos` 是用户正在录的目录，会话中途就从 1643 涨到 1645 份。
  任何按 `--sample` 抽样再与「上一次的报告目录」比对的脚本都会被这个漂移误伤；
  比较前先对**本次实际用到的文件清单**取快照再取哈希。
