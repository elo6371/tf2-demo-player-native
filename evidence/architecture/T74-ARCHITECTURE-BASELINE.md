# T74 — 架构基线与文件所有权盘点

基线提交：`db3316e`（分支 `p0-entity-protocol`）。本报告**只读**：没有改动任何产品源码、门禁脚本或证据文件。
唯一写入是本文件。
（2026-10-26 归档：本文件原先写在 `.scratch/t74-architecture.md`，而 `.scratch/` 被 `.gitignore:31` 忽略；
 归档后位置为 `evidence/architecture/T74-ARCHITECTURE-BASELINE.md`，正文未改，只改了 §0 与 §6 中描述自身位置的句子。）

## 0. 冻结时的仓库状态（登记，不清理）

```
HEAD                     db3316e
git status --porcelain   45 项：20 modified + 25 untracked
  NEXT-TASKS-2026-10-23.md    M   用户自己的任务清单，不动
  evidence/t68-events/runs/*  M   T68 门禁产物，不属于本轮
  evidence/t70-controls/gate/* M  T70 门禁产物（16 个文件）
  evidence/release-gates/*    ??  19 个发布日志（untracked）
  evidence/t56-map-audit/runtime/ ??  T56 运行时目录
hostlock                 FREE，queue=head=，reservation=none
product_processes        0
```

`native/` 下没有任何未提交改动，所以“代码基线”等于 `db3316e`；但整个工作区**不是干净冻结树**，发布冻结前必须由用户决定这 45 项的归属（见 §7 风险 R1）。

## 1. 产物地图（谁能独立拆分）

工程是 1 个产品 + 52 个探针，全部由一个 `native/CMakeLists.txt`（891 行）定义：

| 目标 | 源文件数 | 说明 |
|---|---|---|
| `tf2_demo_native` | **51** | 唯一产品目标 |
| 52 个 `*_probe` / `vpk_query` | 1–13 | 探针，其中大多数链接产品自己的实现 |

产品 51 个源文件的所有权分布是本次盘点的核心数字：

- **仅产品可用（product-only）：9 个** —— 改它们不会动任何门禁的行为；
- **与探针共享（shared）：42 个** —— 改动会同时改变一个或多个门禁的被测实现。

共享度最高的几个（“改这里 = 同时改门禁的被测对象”）：

```
src/vpk_archive.cpp            39 targets
src/audio_resource.cpp         26
src/audio_timeline.cpp         26
src/demo_header.cpp            24
src/vtf_texture.cpp            19
src/asset_root.cpp             18
src/vmt_material.cpp           18
src/item_schema.cpp            18
src/model_loader.cpp           15
```

这个比例说明一件重要的事：**这个仓库的门禁哲学是“探针链接产品实现”，所以绝大多数模块都不是内部实现，而是契约**。任何“拆分/重构”任务（T76/T77/T78）实际上是在移动契约的物理位置，而不是改私有代码——因此必须保持 target 名、源文件同一份实现、链接库不变，否则门禁立刻失去意义。

## 2. 依赖方向

```
                    ┌──────────────────────────────┐
   Win32 message ──►│ windowProc (main.cpp:2325)   │
                    └──────────────┬───────────────┘
                                   │
        ┌──────────────────────────┼──────────────────────────┐
        ▼                          ▼                          ▼
 g_cameraDirector            g_playback                 g_hud* / native_ui
 (camera_director.cpp)       (playback_control.cpp)     (native_ui.cpp, hud_*)
        │                          │                          │
        └──────────┬───────────────┴──────────────┬───────────┘
                   ▼                              ▼
            g_renderer (native_renderer)   g_reviewSession (review_session)
                   │                              │
     ┌─────────────┼─────────────┐                ├─ review_workspace (INI codec)
     ▼             ▼             ▼                └─ review_bookmarks (identity+jump)
 world_material  entity_model  viewmodel
     │             │             │
     └─────────────┴──────┬──────┘
                          ▼
                 asset_lookup / vpk_archive / vtf_texture / vmt_material
                          ▼
                    demo_header (解析与网络实体)
```

依赖方向是**单向**的：渲染层不反向调用播放层，解析层不依赖渲染层。唯一的双向感来自 `main.cpp`——它同时持有全部 9 个 `g_` 家族并承担 74.6% 的代码（`wWinMain` 单函数 8,359 行）。

## 3. 公共接口与不可并行文件

### 3.1 三个“必须串行”的契约文件

| 文件 | 规模 | 为什么不能并行 |
|---|---|---|
| `native/src/main.cpp` | 11,206 行 | 90 个 `g_` 标识符、9 个职责族；任何两个任务同时改必然在 `wWinMain` 内冲突 |
| `native/include/native_renderer.h` | 1,979 行 | 16 个 struct / 4 个 enum / 38 个 setter 是产品与 6 个渲染类探针的公共类型 |
| `native/include/demo_header.h` | 1,037 行 | 28 个 struct / 31 个函数，被 24 个目标编译 |

外加 `native/CMakeLists.txt`（891 行，53 个 `add_executable`）与根目录 `verify-all.sh`（30 步）。

### 3.2 `main.cpp` 的职责构成（T76 的拆分对象）

顶层定义 44 个，其中 6 个占了绝大部分体积：

```
8359 行  wWinMain                     (2849)
 505 行  runCameraScriptStep          (1100)
 457 行  windowProc                   (2325)
 192 行  narrowAscii                  (341)
 185 行  jumpToCameraBookmark         (710)
 145 行  runLifecycle                 (118)
```

按 `g_` 家族的耦合度（引用次数，是拆分成本的直接代理）：

```
g_playback          155 引用    → 播放编排
g_hud* / hud*(      77/72      → HUD 与面板
g_renderer          59 站点 / 23 个不同方法 → 渲染调用面（薄）
g_cameraDirector     44        → 相机
g_reviewSession      27        → 复盘工作区
其余 53 个 g_ 只在 wWinMain 内使用（状态机局部性较好）
```

好消息：进入渲染器的调用面只有 23 个方法、59 个站点（`renderer.h` 的 82 个方法里），相机通过 `syncDirectorPoseToRenderer` 收束成单点。真正难拆的是 `g_playback`（155 处）和 HUD 家族（149 处），它们才是 T76 的主要工作量。

### 3.3 文件所有权分配建议（供后续任务）

| 任务 | 独占写的文件 | 只读依赖 | 冲突风险 |
|---|---|---|---|
| T61.1 动画标签 | `native/src/main.cpp`（动画选择段落）`player-animation-check.sh` | animation_binding/decoder | 与 T76 抢 main.cpp |
| T75 CMake | `native/CMakeLists.txt` + 新 `native/cmake/*.cmake` | — | 无人可并行（构建入口） |
| T76 入口拆分 | `native/src/main.cpp` + 新 `native/src/app_*.cpp` | 全部 | 独占 main.cpp |
| T77 渲染器拆分 | `native/src/native_renderer*.cpp` + `native_renderer.h` | scene 类型 | 独占渲染器 |
| T78 解析拆分 | `native/src/demo_*.cpp` + `demo_header.h` | — | 独占解析层 |
| T72.1 UI | `native/src/main.cpp`（输入/菜单）+ `review_*_ui.*` | review_session | **与 T76/T61.1 抢 main.cpp** |

结论：**同一时刻只能有一个任务持有 `main.cpp`**。这就是执行顺序里 T61.1 → T75 → T76 → T72.1 必须串行的技术原因，不是排期偏好。

## 4. 门禁地图（57 个 `-check.sh`）

按面积分布：

```
世界/渲染   19   entity-material, frame-capture, lighting-dark-account, lighting-semantics,
                 material-coverage, outline, quality-audit, terrain-blend, visual-baseline,
                 world-geometry-hole, world-material*, weapon-material ...
实体/模型   18   animation-availability, entity-property-lookup, history-coverage, observer-focus,
                 player-animation, player-model, slot-freshness, source-tv-coverage, spectate,
                 viewmodel-*, weapon-animation, weapon-body, weapon-world-model ...
安装/资源    5   install-lifecycle, install-readiness, resource-discovery, resource-reachability, vpk-build
HUD/UI       4   camera-ui, hud, hud-text, minimap
抓帧/静默    3   fullscreen-quiet, idle-spin, quiet-capture
其它         3   game-event, stability-release, trajectory-overlay
播放/相机    2   demo-clock, playback-control
                        （外加 review-workspace、logstf×2）
```

每个模块对应的 focused gate 已在 `evidence/t62-matrix/` 的矩阵里逐格给出证据行；本报告只登记“模块 ↔ 门禁”的归属关系，不重复矩阵内容。

## 5. 尚未明确的接口（契约缺口）

1. **`HudState` 没有面板可见性 setter**（`native/include/hud_state.h`）：T72.1 要“恢复 HUD 面板可见性/布局”，但当前 API 只有 `setEnabled/setScalePercent/setPreview`。工作区文档里有 `WorkspaceHudPanel{visible,x,y,w,h}`，产品端**无法把它写回 HUD**——这是 T72.1 的第一个硬缺口。
2. **`Renderer` 没有“列出当前 pass”的自省接口**：T77 拆 pass 时，唯一的顺序证据是 trace 行，没有类型级契约。
3. **`demo_header.h` 同时暴露“文件/索引”与“网络实体 + 事件 + 声音 + 临时实体”**：T78 要拆，但调用方（24 个目标）都 include 同一个头，第一轮必须保留转发头。
4. **`review_session.h` 的 `ReviewEnvironmentCapture/Apply` 是 std::function 注入**：T76 把入口拆出去后，谁负责注入这两个回调必须固定，否则会退化成全局。
5. **相机 `g_cameraDirector` 与 `g_cameraBookmarks` 是两套状态**（`camera_director` vs `camera_bookmarks`），工作区恢复要在两者之间同步——顺序没有文档化。

## 6. 与任务清单的对照（T74 验收）

- 盘点基于 `db3316e`：是（HEAD 与 `native/` 干净状态见 §0）。
- 无产品源码/门禁/证据改动：是（本文件是新产物，2026-10-26 归档到 `evidence/architecture/`）。
- 架构图、依赖方向、所有权表、模块↔门禁、未明确接口：§2/§1/§3.3/§4/§5。

## 7. 风险与建议

- **R1（必须先解决）**：45 项未提交证据使“冻结树”不成立；T73 之前必须由用户逐类确认（用户清单 / 门禁产物 / 可丢弃）。
- **R2**：`world-material-face-check.sh --mutation` 的计数修法**仍未跑过一次**（我上一轮改的 `mred` 计数），这是 T62 第一件事。
- **R3**：照明/材质语义证据仍是时钟修复前的相机位（eye=-2294.8,348.8,383.4），矩阵里 12 个 NOT VERIFIED 格有 6 个卡在这里。
- **R4**：`main.cpp` 单文件 11.2k 行 + 90 个全局，T76 的 2–4 天估计只有在“每次只搬一个职责、每搬一步跑 focused gate”的前提下成立。
