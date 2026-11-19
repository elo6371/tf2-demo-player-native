# T4：N24 Phase 2 现状勘察（只读）

- **目标文件**：`docs/N24-PHASE2-SURVEY-2026-11-19.md`（只读勘察，未改源码）
- **基线提交**：`work/integrate` @ `69804728f368099aeda7b2e5a8e89056521b3611`

---

## 1. N24 Phase 2 的原始定义
出自 `docs/TASK-LIST-2026-11-19.md:47–50`：
> `9. **N24 phase 2+**：wWinMain 仍 13,093 行一个函数（B1 后 main.cpp 16,046 行、起点 :2954）；CLI 比较链`
> `C1061 已撞过两次（旧比较链、B1 的 if 包装），下一步是 renderer/HUD 调用面 seam，不是继续加开关。`

*(对照 `docs/N24-MAIN-SPLIT-PLAN-2026-11-16.md` §4：原计划 P2 为 `main_cli.cpp` 预扫描与参数解析、P3–P5 为抓帧/关闭/HUD 循环尾段。)*

---

## 2. 现状（`69804728` 实测）
- **已合入部分**：
  1. **N24 phase 1（`88ade848`）**：`runRendererSelftest` 抽出为 `native/src/main_selftest.cpp`（213 行，`docs/TASK-LIST-2026-11-19.md:12`），`main.cpp` 从 16,413 降至 16,221 行。
  2. **G1 B1**：34 条 CLI 开关表化迁入 `native/src/main_cli.h`（376 行），`main.cpp` 降至 **16,046 行**，`wWinMain` 起于 `native/src/main.cpp:2954`（13,093 行），剩余 156 条 `wcscmp(..., L"--"`。
- **当前能力**：`main.cpp` 之外已有 7 个拆分 `main_*.cpp` TU（camera/helpers/hud/lifecycle/selftest/trace/ui）及 `main_cli.h`，`wWinMain` 主体、HUD/抓帧循环尾段与退出汇总仍在单函数内。

---

## 3. 缺口清单

| 缺口 | 卡在哪一行 (`69804728`) | 类型（结构性 / 缺开关） |
| :--- | :--- | :--- |
| `wWinMain` 单函数 13,093 行导致 `/analyze` 仍报 `C6262` (58,208 B) | `native/src/main.cpp:2954–16046` | **结构性**（需拆出尾段/循环块，非单个大数组） |
| 35 个文件作用域可变全局 `g_*`（含 `g_renderer`、`g_nativeUi`、20 个 `HWND`）直接被 `wWinMain` 与 WndProc 共享 | `native/src/main.cpp:134–204` | **结构性**（须先收口为显式 context struct） |
| 剩余 156 条 CLI `wcscmp` 比较链仍逼近 MSVC `C1061` 嵌套上限 | `native/src/main.cpp:3715–4553` | **结构性**（已撞过两次 `C1061`） |
| `g_playback.tick` 存在 3 处写入者且全循环扇出读取 | `wWinMain` 主循环内 | **结构性**（拆分必须保留其单一所有者） |

---

## 4. 与 Renderer / HUD Seam 及 T3 矩阵的关系
**有直接交叉**：N24 phase 2 下一步要切开的正是 `wWinMain` 调入 `Renderer`（`g_renderer`、`main.cpp:135`）与 2D HUD（`hudApplyRects`、`g_uiHud*`）的调用面 seam，而 T3 矩阵中的 `drawHudPass`（`native_renderer_frame.cpp:2342–2372`）与 `drawViewModel`（`:2031`）正是该调用面的 GPU 侧落点，两者在“不改状态恢复顺序、不破帧 md5”上共用同一验收边界。

---

## 5. 如果要做，第一步是什么
先将 `native/src/main.cpp:134–204` 中被关闭块与 HUD 尾段读取的 `g_renderer`/`g_nativeUi`/UI 句柄收口为只读参数结构，并把纯单向的 Shutdown 尾段（B2-1）搬入 `native/src/main_shutdown.cpp` 以验证首个 `wWinMain` 内部切口。
