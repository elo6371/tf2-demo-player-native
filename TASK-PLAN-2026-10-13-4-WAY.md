# 四任务并行分工计划

更新时间：2026-10-14
代码基线：`p0-entity-protocol@be27383`
原则：每个任务独立 worktree；禁止直接修改主线；禁止运行完整 `verify-all.sh`。

整合更新：T3 世界材质与光照已由 `task/world-material-lighting@b55e8e5` 合入 `b7d8035`；抓帧确定性修复合入 `1c69f08`；T4 相机/HUD/UI 已由 `task/camera-ui@ca21a50` 合入 `bf0d268`；T1 世界渲染补全已由 `task/world-render-followup@e50b585` 合入 `be27383`。T1 的 skybox 几何、VIS 三角形裁剪和 atlas 宽度选择专项通过；完整总链仍不运行。

T1 已交付并整合：`task/render-current@8ddab34` → 主线整合 `edc4950`。正向复核为 8/9（1 red，capture tick 标定盲区），mutation 为 9/9；详见 `T1-RENDER-CURRENT-2026-10-13.md`。T2 已交付并进入整合：`task/manual-acceptance@ae7bd09`，只新增人工验收证据，不改产品或门禁。下一步是在后续代码任务整合完成后，按需在当前 HEAD 单独复核 `verify-render.sh`，不把来源分支结果当成当前 HEAD 结果。

## 分配总览

| 任务 | 名称 | 类型 | 是否可并行 | 整合顺序 |
|---|---|---|---|---|
| T1 | 当前渲染档复核 | 只读验收/证据 | 可与 T2/T3/T4 并行 | 第 1 个处理 |
| T2 | ViewModel、武器本体、真实音频人工验收 | 只读验收/证据 | 可与 T1/T3/T4 并行 | 第 2 个处理 |
| T3 | 世界材质与光照 | 产品代码 | 可与 T4 并行 | 第 3 个处理 |
| T4 | 相机、观察者模式、HUD/UI | 产品代码 | 可与 T3 并行 | 第 4 个处理 |

“可并行”指不同 worktree 中可以同时开发；不表示可以在同一个 worktree 中同时构建或运行门禁。

## T1：当前渲染档复核

### 工作区

```text
D:\TF2_Native_Worktrees\render-current
task/render-current
```

### 负责内容

- 在基线 `0f023bb` 上运行当前 `verify-render.sh`。
- 正向运行一次；必要时运行 `--mutation`。
- 使用 `--hidden-window --audio-disabled`，避免弹窗和声音。
- 检查锁、源码恢复、证据新鲜度和工作树状态。

### 允许修改

```text
evidence/render-current/
T1-RENDER-CURRENT-2026-10-13.md
```

不得修改产品源码、`verify-render.sh` 判据或 `verify-all.sh`。

### 验收

```bash
bash verify-render.sh
bash verify-render.sh --mutation
git status --short
```

交付必须报告：`VERIFY-RENDER`、gate 数、mutation 数、退出码、日志哈希、锁是否清理。

## T2：人工验收

状态：已交付并整合（`591cbec`）；报告 `T2-MANUAL-ACCEPTANCE-2026-10-13.md`。矩阵证明 ViewModel/FOV/hand 输入影响画面，真实设备打开并播放 19 个事件；用户已确认声音内容。矩阵仍不证明动画、材质或武器本体正确：武器本体在选定 tick 没有 ViewModel 实体，因此 on/off 逐字节相同。

### 工作区

```text
D:\TF2_Native_Worktrees\manual-acceptance
task/manual-acceptance
```

### 负责内容

只产生证据，不修改产品代码。

#### ViewModel/武器本体矩阵

| 维度 | 值 |
|---|---|
| FOV | 40、90、120 |
| 手性 | left、right |
| 武器本体 | on、off |
| tick | 2000、4000、6000 |
| 模式 | `--hidden-window --audio-disabled` |

每组记录：模型路径、attachment、FOV、hand、arm/weapon draw count、weapon visibility、BMP SHA256、ViewModel 区域差异、世界区域差异。

“像素不同”只能证明输入影响了画面，不能单独证明动作或材质正确；人工结论必须带截图、tick、模型路径和读数。

#### 真实音频

自动门禁只验证 null sink，不能代表真实声音。另行使用真实设备运行并记录：设备打开结果、读取/调度/播放数量、缺资源数，以及开火、脚步、Uber、环境音是否可听。

### 验收

```bash
bash verify-fast.sh
```

如需抓帧，统一加：

```text
--hidden-window --audio-disabled
```

真实声音人工验证不得写成自动门禁通过。

## T3：世界材质与光照

### 工作区

```text
D:\TF2_Native_Worktrees\world-material-lighting
task/world-material-lighting
```

### 负责内容

按顺序实现：

1. RGBExp32 逐面 lightmap。
2. cubemap 六面发现、顺序和采样。
3. skybox。
4. displacement。
5. water capability 和缺资源回退。
6. VIS clipping 状态。

每个能力先做合成 fixture，再接真实 BSP；每个能力单独输出状态和失败原因，不能只用整帧变化作证据。

### 允许修改

```text
native/include/world_material*.h
native/src/world_material*.cpp
native/src/native_renderer.cpp
native/tools/world_material_probe.cpp
world-material-check.sh
evidence/world-material/
```

禁止修改音频、ViewModel、PacketEntities 和 UI 文件。若必须改共享 renderer 接口，先提交接口小提交再通知整合者。

### 验收

```bash
bash build-target.sh world_material_probe tf2_demo_native
bash world-material-check.sh
bash world-material-check.sh --mutation
bash verify-render.sh
```

## T4：相机、观察者模式、HUD/UI

### 工作区

```text
D:\TF2_Native_Worktrees\camera-ui
task/camera-ui
```

### 负责内容

- 第一人称、第三人称、自由观察状态。
- 单击/双击空格切换，双击时间窗口可测量。
- P 键暂停/继续。
- 无观察目标时保持当前相机，不擅自搬移。
- HUD 显示 tick、demo 名、播放状态、相机模式和错误状态。
- 窗口缩放/DPI 下文字不重叠。

### 允许修改

```text
native/include/native_ui*.h
native/src/native_ui*.cpp
native/include/playback_hud*.h
native/src/playback_hud*.cpp
native/src/main.cpp
native/tools/native_ui_probe.cpp
camera-ui-check.sh
evidence/camera-ui/
```

禁止修改音频、世界材质、PacketEntities、ViewModel 和 renderer 绘制实现。`main.cpp` 是共享文件，必须最后整合，不能覆盖 T3 已有接线。

### 验收

```bash
bash build-target.sh tf2_demo_native native_ui_probe
bash camera-ui-check.sh
bash camera-ui-check.sh --mutation
bash verify-fast.sh
```

## 并行与禁止并行

### 可以同时进行

- T1 与所有任务都可并行：它只读验收和写独立证据。
- T2 与所有任务都可并行：它不改产品源码。
- T3 与 T4 可以并行开发：T3 负责 world material/renderer，T4 负责 UI/camera/main；各自必须使用独立 worktree。

### 不要同时进行

- 不要在同一个 worktree 同时运行两个 build、mutation 或 gate。
- 不要让 T3 和 T4 同时把共享接口改成不同版本后再整体复制文件；共享接口必须先定小提交。
- 不要在 T3/T4 开发期间运行总链；总链会锁定并重建共享构建目录。
- 不要让 T2 使用正在被 T3/T4 修改的二进制；T2 固定在基线 `0f023bb` 或明确记录测试 HEAD。
- 不要把 T1/T2 的人工证据写入产品门禁，也不要把真实音频结果写成 null sink 通过。

## 高效执行顺序

### Wave 1：四个任务同时启动

启动 T1、T2、T3、T4。T1/T2 先产出证据，T3/T4 并行开发。

### Wave 2：先审查再整合

1. T1 交付后，用 `code-review` 审查证据范围和是否误报通过。
2. T2 交付后，用 `code-review` 审查截图、读数和未验证项。
3. 先整合 T1/T2 的证据，再整合 T3。
4. T3 合并后运行 `verify-render.sh`。
5. 最后整合 T4，因为它修改 `main.cpp`，冲突风险最高。
6. T4 合并后运行 `verify-fast.sh` 和受影响专项门禁。

### Wave 3：发布前

只有当 T1/T2/T3/T4 都整合、工作树干净、专项门禁全部通过后，才运行一次完整 `verify-all.sh --quick`。

## 每个 AI 的交付格式

```text
任务：T1/T2/T3/T4
分支与 worktree：
基线 commit：
最终 commit：
修改文件及理由：
构建命令与 rc：
专项验收命令与 rc：
正向读数：
mutation 结果：
证据路径与哈希：
未验证项：
工作树是否 clean：
GitHub 分支地址：
```

## 已安装技能

- `code-review`：用于每个 AI 交付后的独立审查。
- `git-worktree`：用于创建隔离 worktree、核对基线和清理工作区。
- `coding-problem-playbook`：真实问题复现并验证后，记录可复用解决方案。
