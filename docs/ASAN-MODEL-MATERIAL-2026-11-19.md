# ASan：模型与材质路径 — 2026-11-19

这份文件是 L5 的交付（`docs/TASK-ALLOCATION-2026-11-19.md` §2 的 L5），也是
`docs/TASK-LIST-2026-11-19.md` 未解决项里 model/material sanitizer 那一条的读数。
它回答一个问题：把 `model_robustness_probe` 与 `material_chain_probe` 用
`/fsanitize=address` 重新编译，再拿真实语料喂它们，会不会出现越界/释放后使用/双释放。
本轮读完 104 个用例：**AddressSanitizer 报告 0 条**，并且 22 个非零退出**全部**能
归到设计上（探针按约定拒绝畸形输入），没有一条是"不知道原因"的。

产品源码零改动；新增的只有两个脚本和这份证据。

## 1. 结论

| 项 | 数 |
|---|---|
| 用例行 | **104** |
| AddressSanitizer 报告 | **0** |
| 有报告的用例（hit-rows） | **0** |
| 保留的 sanitizer 日志（`logs/`） | **0** |
| 非零退出 | 22（见 §3，全部按设计） |
| 每个用例留存的探针 JSON | 104（`json/`） |

## 2. 怎么跑（仓库内可复跑）

### 2.1 构建

```bash
tools/asan-build.sh     # 首次 configure，之后增量；独立树，不污染 native/build-nmake
```

- 编译器：MSVC `cl 14.44.35207`（VS 2022 Community），flags `/fsanitize=address /Zi`，
  RelWithDebInfo，链接 `/DEBUG`（`tools/asan-build.sh` 第 27-31 行）。
- 默认树 `/d/_shared/n23-asan-build`（`ASAN_BUILD_DIR` 可覆盖）；用独立树的理由：
  `/fsanitize=address` 的 obj 与普通构建不可互换。
- ASan 运行库 `clang_rt.asan_dynamic-x86_64.dll` 在 MSVC 的
  `bin/Hostx64/x64/` 下，`tools/tf2-toolchain.sh` 已把它放进 PATH。
- 默认 targets：`tf2_core model_robustness_probe material_chain_probe demo_open_probe`。
  本轮读数用的两个探针：`model_robustness_probe.exe` 3,074,560 B、
  `material_chain_probe.exe` 2,052,608 B（14:00 重建，晚于 P9 的三处 `std::wstring`
  改动，所以读数覆盖的是当前 HEAD `c9b0def0` 的核心代码）。
- `CMakeCache.txt` 里可核对：`CMAKE_CXX_FLAGS:STRING=/fsanitize=address /Zi`。

### 2.2 语料

| 语料 | 内容 |
|---|---|
| 松散模型树 `D:/TF2_Demo_Player/work/_tf2-samples-all` | 598 MB；66 个 `.mdl`、26,054 个 `.vmt`、1,495 个 `.vtf` |
| 真实安装 `D:/SteamLibrary/steamapps/common/Team Fortress 2/tf` | 64.9 GB，含 `tf2_misc_000..NNN.vpk` 编号卷 |
| 桩目录 `/d/TF2_Demo_Player/tmp_material_tf_all` | 只有 `*_dir.vpk`、**没有**编号卷，8 MB（用来读"缺卷"分支） |

抽样不是全量：模型是 66 个全跑；VMT 取 `NR%2000==1`（14 个）；VTF 取 `NR%150==1`
（10 个）。这是"铺开覆盖面"而不是"每个文件都跑"，理由见 §5。

### 2.3 驱动

`tools/asan-model-material.sh` 分 8 节串行跑，每节一个 `run_case`：

1. `model` 66 个松散 `.mdl`（`--tf-root <样本树> --model <相对路径>`）；
2. `model-vpk` 6 个（scout/soldier/demo/heavy/engineer/medic，`--tf-root` 指真实安装，
   走 `asset_root` 的 VPK 解析）；
3. `fixture` 5 个（同一真实模型 `models/player/demo.mdl` 的畸形改法：
   descriptor-not-triangles、descriptor-out-of-bounds、bone-out-of-range、
   no-descriptors、lod0-absent）；
4. `vmt` 14 个（`--vmt <file> --vmt-include-root <materials>`）；
5. `vtf` 10 个（`--vtf <file>`）；
6. `root-loose`：`material_chain_probe <样本树>`；
7. `root-vpk`：`material_chain_probe <真实安装>`；
8. `root-vpk-stub`：`material_chain_probe <桩目录>`。

```bash
ASAN_OPTIONS=detect_leaks=0:halt_on_error=0:abort_on_error=0:print_summary=1 \
  bash tools/asan-model-material.sh
```

`detect_leaks=0` 是有意的：这轮要问的是内存安全（越界/UAF），不是泄漏；
MSVC 的 ASan 泄漏检测会拖长每个用例的退出时间，且探针本来就在进程退出时交还全部内存。
`halt_on_error=0` 让一个用例里多条报告都打出来再继续，`print_summary=1` 保证末尾有总账。

## 3. 读数（按节）

| 节 | 行数 | rc=0 | rc≠0 |
|---|---|---|---|
| model | 66 | 48 | **18** |
| model-vpk | 6 | 6 | 0 |
| fixture | 5 | 1 | **4** |
| vmt | 14 | 14 | 0 |
| vtf | 10 | 10 | 0 |
| root-loose | 1 | 1 | 0 |
| root-vpk | 1 | 1 | 0 |
| root-vpk-stub | 1 | 1 | 0 |
| **合计** | **104** | **82** | **22** |

22 条非零退出逐类：

1. **模型 18 条**：全部是 `*_animations.mdl` / `*_user_animations.mdl` —— 这些文件只有
   骨骼与序列、**没有** `.vvd`/`.vtx` 伴生文件（JSON 里 `vvd:false vtx:false
   companions:false`，`bones` 82 等）。探针的约定是"没有顶点数据就不算可解析模型"，
   于是按设计 `exit 1`；这正是它该做的事（不能拿半份数据去建 mesh）。
2. **夹具 4 条**：每条都带一句明确的拒绝理由，而不是崩溃：
   - `descriptor-not-triangles` / `descriptor-out-of-bounds`：
     `descriptor 0 (bodyPart 0 model 0 mesh 0 lod 0) was refused: descriptor is not a
     bounded triangle list; the mesh is not built from a partial range set`；
   - `bone-out-of-range`：`... bind-pose vertex has an out-of-range bone index; the mesh
     is not built from a partial range set`；
   - `no-descriptors`：`model inspection has no renderable descriptors` +
     `no VTX descriptor decoded`。
   三条带 "the mesh is not built from a partial range set" 的拒绝是同一个不变量在起作用：
   **一个 range 集合被拒，整个 descriptor 就不建 mesh**，不会画出半截几何。
3. **`lod0-absent` 是 rc=0**：`lodCount` 从 7 变 6、`lod0Descriptors=0`、
   `targetLod=1`、`lodFallback=true`、`meshBuilt=true`。缺 LOD0 属于可降级输入，
   探针选下一级并建出 mesh —— 所以它不该进"拒绝"统计。

非零退出里没有一条来自 sanitizer，也没有一条是"探针自己死了"：`logs/` 为空，
`hit-rows=0`，`reports=0`。

## 4. 材料根三态与 VPK 语义

同一份 `material_chain_probe` 对三种材料根各跑一次，读数的差别本身就是结论：

| 根 | `archiveOpen` | `archiveCount` | `cubemapFaceFiles` | `cubemapParseFailures` | 负向断言 |
|---|---|---|---|---|---|
| 样本树（松散） | false | 0 | 0 | 0 | 全部 true |
| 真实安装 | **true** | **4** | 0 | **0** | 全部 true |
| 桩目录（缺编号卷） | **true** | **4** | 0 | **31** | 全部 true |

三条都 `rc=0`，并且 `emptyVtfRejected`/`corruptVtfRejected`/`oversizedVtfRejected`/
`cubemapDecodeRejected`/`emptyBspRejected` 等负向断言全部成立，说明"坏输入必须被拒"
这条路径在 ASan 下也没有被绕过。

关于桩目录那 31 条 `cubemapParseFailures`：它不是 sanitizer 报告，也不是构建差异。
机制在 `native/src/vpk_archive.cpp:239-284`（`VpkArchive::readEntry`）：
**"路径不在树里"返回空 error（只是 found=false），只有真正读失败才写 error**
（`missing VPK data archive` / `VPK entry range outside data archive` /
`truncated VPK data entry`）。桩目录只有 `*_dir.vpk`、没有 `tf2_misc_000..NNN.vpk`，
于是每个 cubemap 面文件都落到"缺数据卷"这一支，
`material_chain_probe.cpp:299` 的 `if (!readError.empty()) { ++cubemapParseFailures; break; }`
把它计成一次解析失败 —— 这是探针在如实报告材料根不完整，`cubemapFaceFiles=0` 也印证
一个面都没读成。

**跨构建对照**：同一根（桩目录）分别用 ASan 构建与普通构建
（`native/build-nmake/material_chain_probe.exe`）跑，输出的 JSON 都是 **1,419 B、
逐字节相同**（`diff` 为空）。所以上表的数值不是 sanitizer 构建带来的偏差。

真实安装的读数还给材料层一个正面读数：`realCubemapVtfFlags=14`、
`realCubemapHighResResources=14`、`realCubemapResourceTags=28`，`archiveCount=4` ——
4 个 VPK 卷被打开并解码了立方体贴图的资源标签。

## 5. 这次没覆盖到什么（静态无法定论的部分）

1. **只跑了探针，没跑产品进程**。`tf2_demo_native.exe` 的渲染/上传/每帧路径不在本轮
   sanitizer 覆盖内；ASan 版本的产品 exe 没有构建，也没有跑 demo。
2. **只测 parser 与选择逻辑**，没测 GPU 侧（纹理上传、采样、shader 输入布局）。
   越界写 vertex buffer 这类问题不在这些探针的射程里。
3. **抽样不是全量**：VMT 14/26,054、VTF 10/1,495。覆盖面靠"均匀抽样"而不是穷举；
   真要穷举，下一步应该按目录分层再抽，或把 `material_chain_probe` 的批处理模式用起来。
4. **没有做变异/fuzz**。5 个夹具是人工构造的定点输入，不是随机变异器；
   "ASan 没报"只对这批输入成立。
5. **泄漏检测关闭**（`detect_leaks=0`），所以本轮不回答"有没有泄漏"。
6. **桩目录是人为造的缺卷场景**，它证明的是探针在这种输入下如实报错，不是游戏真实
   安装会这样。

以上每一条都值得单独开一项，而不是从本轮的"0 报告"里推论。

## 6. 证据与复跑

- 读数表：`evidence/asan-2026-11-19/readings.tsv`（104 行，列：kind/subject/exit/asan_reports/note/first_report）
- 每用例探针 JSON：`evidence/asan-2026-11-19/json/`（104 个）
- sanitizer 日志：`evidence/asan-2026-11-19/logs/`（空 —— 本轮没有命中）
- 构建日志：`/d/_shared/asan-rebuild-2026-11-19.log`、`/d/_shared/asan-mm-2026-11-19b.log`
- 构建树：`/d/_shared/n23-asan-build`（`ASAN_BUILD_DIR`）

复跑全套（约 1 分钟；不含构建）：

```bash
tools/asan-build.sh && bash tools/asan-model-material.sh
```

## 7. 变更清单

- 新增 `tools/asan-build.sh`（独立 ASan 构建树 + target 列表，默认值即本轮用的那套）。
- 新增 `tools/asan-model-material.sh`（8 节语料驱动，写 `readings.tsv` + 每用例 JSON）。
- 新增 `evidence/asan-2026-11-19/`（读数与 JSON，不含命中日志）。
- **产品源码零改动**；探针源码（`native/tools/model_robustness_probe.cpp`、
  `native/tools/material_chain_probe.cpp`）原样。
