# N17 会话报告：UI 对齐、世界合并、Scout 探针与「无法完成项」

日期：2026-11-17  ·  分支：`work/integrate`（D:/TF2_Native_Worktrees/t105-integrate）
当前 tip：`d5037e58`（合并 `d0704eef` 之后）
作者：lead（本报告由会话记录与实测读数整理，不含推测）

---

## 0 一句话结论

UI 的「红蓝对齐 + 时间比分居中」已经实现、门禁重钉并在合并树上构建通过；世界的三支修复已经并入且验证过；
**但「Scout 外观到底哪里不对」这一条今天没有定案** —— 它卡在 A 级 TF2 参考帧拿不到，而拿不到的原因是三件与代码无关的事（tick 空间、回放无相机、回放锁相机），不是继续读代码能解决的。下面第 3 节逐条写清。

---

## 1 已经做完并验证的

### 1.1 UI：红蓝对齐 + 时间/比分居中（用户 16:xx 的需求）

用户原话（m09457）：「UI 红蓝两边要对齐啊，时间和分数放在中间最上面」。

实现后读数（1280×720 窗口 / 1264×681 客户区）：

| 项 | 读数 |
|----|------|
| 时间 + 比分 | 两条都水平居中于顶部：`header=536,24,192,38`；`lineX=496/560,536,24`（数字宽度不同时自动居中） |
| RED 名单 | `red=12,96,220,206` |
| BLU 名单 | `blu=1032,96,220,206`（与 RED 同 y、同宽高、镜像 12 px 边距） |
| 行数 | `rowsRed=6 rowsBlu=6` |
| 小地图 | 右下角 `panel=1002,419,250`、`area=1006,423,242` |
| 第三行 NAME/HP | 保留在左 margin `nameBlock=24,68,368,16`（它是被观战者，不是比分） |

1920×1080（客户区 1904×1041）：`panel=1642,779,250`、`red=12,96,220,206`、`blu=1672,96,220,206`、`header=856,24,192,38`。
320×240（客户区 304×201）：小地图与名单必然相交，按**通用 no-overlap 规则**丢弃小地图（`reason=no-room`、`panelPrimitives=0`），两块名单仍对齐 `rows 2/2`；300×200 请求被拒（exit 13，不写位图）。

提交：`f21db22e`（布局）+ `d0704eef`（门禁重钉）。
门禁（在 mapui 分支树上跑，非合并树）：`MAP-OVERVIEW=PASS (229 checks)`、`HUD-LIVE-CHECK=PASS 106 OK`、`AUDIO-KEYS=PASS 19 OK`。
证据图：`analysis/2026-11-17-mapui-{1280x720,1920x1080}-{full,map,header}.png`。

### 1.2 合并与构建

- `work/integrate` 依次并入：世界修复 `cfb6e7e4`、地图/UI `09fc824c`、天空 `2957a6a0`，以及本轮 UI 两笔 → tip `d5037e58`。
- `bash build-cmake.sh`：`EXIT=0`、`exe_count=54`、`native/build-nmake/tf2_demo_native.exe` 已生成。
- 合并冲突一处：`map-overview-check.sh`（两边都改过），已手工合成：采用布局侧新版断言 + 保留合并树侧的 `--window-size`/`pinned=1` 断言（320×240 与三档尺寸都断言窗口确实按请求尺寸创建）。

### 1.3 世界与天空（前序卡片的收尾，已并入）

- 天空解耦 + sky-last：天空门 17/17；相对 round-2 的四像素差异精确复现（up0 (45,52)(285,196)、pr600 (639,21)(652,67)，各 ±1 LSB）；性能开销经六对顺序平衡 A/B 测得 **+0.73 fps，低于噪声（±1.3）**，旧读数「−13.9% / 3.59 ms」已撤回。
- 位移裂缝：up0 残余 3262 px 中「缺几何」为 0（1887 无实心面 + 1337 仅背面，1327 属 trigger_hurt，38 px 轮廓）；pr600 453 px 里 94 px 是真几何被屏幕绕序背面测试剔除，`--no-world-cull` 能补 81/94，罪证行 `native/src/main.cpp:11097 const bool faces = area > 0.0f`。
- 合并后帧复算与分支逐字一致：up0 3262 / up1800 0 / pr600 453 / koth0 0。

### 1.4 探针审计与埋点（未合并）

- 审计：`docs/N17-PROBE-AUDIT-2026-11-17.md`（`a6669e6c` → `924e0f52`，480 行）。
- 阶段二埋点（`f52af70c` + `675bdd92`，分支 `work/model-visual`）：`args: verbatim=`（argv 进 trace）、per-pane `camera:` 行（fov/aspect/near/far/mvp + 来源）、`entityDraw:`（每提交实例一行）、`entityDrawRange:`（每 draw range 一行，带 `textured=0/1`）。
- **状态：未编译、未运行、未合并。** 见第 3.2 节。

---

## 2 本轮被证伪 / 被纠正的判断（留档，避免重复踩）

1. **「近处 Scout 被画面边缘裁切」—— 不成立。** 轮廓投影实测：左沿 x≈100、帽顶 y≈150、右手 x≈370、鞋底 y≈655，**屏外像素 0**。图二的问题不是裁切。
2. **「七个门禁/探针」—— 是六个。** `world-entity-visual-check.sh` 在树里不存在，是我 brief 里的笔误。
3. **「产品名单和 TF2 名单对不上」—— 是我拿错了图。** 我贴的 720p 截图跑的是 `cp_process_f12` 地图门禁 demo；用户那张是 `pl_upward_f12`。用 userinfo 探针解出来的名字（`rzeke / julie / Lyv / Sinines / med took 20 / your biggest fan…`）与用户截图一致，**文件确认无误**。
4. **「tick 11057」—— 两个坐标系。** 产品的 `--capture-tick` 是**服务器 tick**；demoui 显示的是**回放 tick**；`服务器 tick = 回放 tick + 11056`（demo 共 85204 回放 tick，服务器 tick 11057…96260）。所以「服务器 tick 11057」= **回放第 0 帧**，不是回放 11057。我先前让用户拖到 11057 是错的，两者差了 165.9 秒。
5. **`sv_cheats/setpos/setang` 在 demo 回放里无效**（用户实测：相机完全不动）。回放相机是锁死的，`spec_player/spec_mode` 也不改变视角。
6. **回放第 0 帧在 TF2 里没有相机**：`getpos` 返回 `setpos 0.000000 0.000000 0.000000`，画面黑/花。而我们的产品在这一帧用 `src=follow` 自己合成相机（`eye=308.7,-1983.7,199.7`）。

---

## 3 做不了 / 没做完 / 需要别人的（本报告的重点）

### 3.1 A 级 TF2 参考帧：当前拿不到（需要人和一台能跑 TF2 的机器）

**为什么必须要有它**：只有「同 demo + 同 tick + 同相机 + 同客户区 + 记录 FOV」的参考帧，才能把「Scout 看起来不对」判成产品缺陷（轮廓、可见网格、头部/手/鞋材质区域、姿态相位）；B/C/D/E 级只能说明「本该长什么样」，不能定案。

**今天卡住的三件事（都与代码无关）**：

1. **回放第 0 帧两边没有共同基准。** 产品在这一帧有相机，TF2 在这一帧 `getpos=0,0,0`（demo 相机数据尚未建立）。所以「开局这一帧」永远无法做 A 级对照。
2. **回放锁相机。** `setpos / setang / spec_player / spec_mode` 都不能把 TF2 相机摆到 `(308.7, −1983.7, 199.7)`；只能跟随录像里当时被观战的视角。
3. **tick 空间不同。** 必须由人在 TF2 侧找到「有画面且 `getpos` 非零」的回放 tick，再换算成我们的服务器 tick（`+11056`）去抓同一瞬间。**这个 tick 还没找到** —— 从 0 往前后拖几档即可，但需要人工在游戏里做。

**接手人最小任务（照抄即可）**：

1. 启动 TF2：`-windowed -noborder -w 1264 -h 681`，确认客户区就是 **1264×681**。
2. `playdemo a9630c6557468b24f0b8c2e7dab38ed8_match-20261030-2145-pl_upward_f12`（放 `tf/` 下）。
3. demoui 从 0 开始往前拖，**每拖一档打一次 `getpos`**，直到位置不再是 `0,0,0`（回放相机已建立）。记下这个**回放 tick**。
4. 在该 tick：`fov_desired 90`、`r_drawviewmodel 0`、`hidepanels`（或 `cl_drawhud 0`），原尺寸截图，**不缩放不裁边**。
5. 回报五样：① 回放 tick ② `getpos` 原文 ③ 实际 `fov_desired` 值 ④ 客户区尺寸 ⑤ 截图。

拿到后我们这边用 `--capture-tick <回放tick + 11056>` 抓同一瞬间，做轮廓/ROI/六材质区域对照。

**FOV 的坑（必须记录）**：产品用 **竖直 90°**（`XMMatrixPerspectiveFovLH` 的 `FovAngleY`，`native/src/native_renderer_frame.cpp:488/490`，无水平↔竖直换算）；TF2 的 `fov_desired 90` 是 **4:3 下的水平 90°**（1264×681 下竖直 73.74°）。同一视口竖直差 **16.26°**，同距离下身体高度差 **1.33×**。所以：参考帧必须记录实际 `fov_desired`；对照时按 0.75× 归一化，或接受「角尺寸不可直接比」。`fov_desired` 通常被夹在 75–90，**竖直 90 大概率给不出来** —— 这一条也解释得了「同位置下 TF2 里人比产品里大」的观感。

### 3.2 阶段二埋点：写了但没有验证（未编译、未运行、未合并）

- `work/model-visual` 的 `f52af70c`、`675bdd92` 从未编译、从未运行、未合入 `work/integrate`。
- 计划中的两条控制未执行：**C1** 同命令在 `TF2_NATIVE_ENTITY_DRAWS` 未设/设了两种情况下，帧位图必须逐字节相同且 trace 行数 0 / 正；**C2** `entityDraw` 行数必须等于同帧 `scene: setInstances draws=N`。
- 该埋点目前只在**我本地抓的一帧**里被看到过（见 3.3），没有门禁断言；合并前不能当作「已验证」。
- 合并时要重编一次（合并树与埋点分支都改过 `native/src/main.cpp` 附近），冲突需人工处理。

### 3.3 今天用埋点看到的（单帧、未入档结论）

在本机 `pl_upward_f12`、服务器 tick 11057（回放第 0 帧）的一帧 trace 里：

- `entityMaterialRanges=503/504` —— 有一条材质范围被静默丢弃（`native/src/main.cpp:14126-14133`：无 textureView 的 range 是丢弃，不是画白）。
- `animSource=missing`、`animSkinned=32/96` —— 骨骼动画源缺失，部分实例按 bind pose 画。
- `scene: camera mode=first-person target=1 follows=1 holds=1 src=follow eye=308.7,-1983.7,199.7 pitch=10.94 yaw=354.37` —— 这一帧的相机是**产品自己 follow 实体合成的**，不是录像相机（与 3.1 第 1 条对应）。
- `entityModels=61 entitySlots=1420 entityMeshes=73`。

**这些都还没有成为结论**：单帧、且这一帧本身缺共同基准。要定案必须先有 3.1 的参考帧，或先用门禁把这几条读数钉住。

### 3.4 世界残项（与外观无关，但没做完）

1. **pr600 的 94 px 绕序剔除**：`native/src/main.cpp:11097 const bool faces = area > 0.0f` 把背面朝向的三角形整批剔除，属剔除策略而非参数问题；要单独立项（改剔除，不是调参数）。
2. **14 个退化位移面仍 flat**，未逐个归因。
3. **world-material 基线红**：材质门在合并树上未重新钉基线。
4. **`material.noCull` 未接入世界通路**（只在首发材质用到才做）。
5. **几何洞门 pl_odyssey 位姿 pin 未重钉**：换 `pl_upward_f12` 能跑但门不绿（`missing 9 != 95892`、`enclosed 9 != 4634`、`eye -1945.3,-1016.6,243.1 != 815.5,-147.8,-194.3` 等）。
6. **完整 `verify-all` 未在合并树跑**：合并树只构建过；三套 UI 门是在 mapui 分支树上绿的（见 1.1），**合并树上的门禁尚未重跑**。

---

## 4 复现命令（照抄）

```bash
# 交互
tf2_demo_native.exe --tf-root <tf目录> --demo <dem路径>

# 抓一帧（tick = 服务器 tick = 回放 tick + 11056）
tf2_demo_native.exe --tf-root <tf> --demo <dem> \
  --capture-tick <serverTick> --capture-frame out.bmp \
  --audio-disabled --hidden-window --world-water off

# trace
TF2_NATIVE_TRACE=<trace路径> tf2_demo_native.exe ...

# 实体绘制逐行（阶段二埋点分支才有）
TF2_NATIVE_ENTITY_DRAWS=1 TF2_NATIVE_TRACE=<trace> tf2_demo_native.exe ...

# 门禁（单实例，跑前确认没有别的产品实例）
bash map-overview-check.sh
bash hud-live-check.sh
bash audio-keys-check.sh
bash world-sky-gate-check.sh
```

demo：`E:/demo测试/pl_upward_f12/STV/a9630c6557468b24f0b8c2e7dab38ed8_match-20261030-2145-pl_upward_f12.dem`
（SourceTV、pl_upward_f12、回放 85204 ticks / 1278.060 s、tickrate 66.667、客户端 1264×681、外层 1280×720）

---

## 5 需要谁做什么（最短清单）

| 事项 | 谁做 | 交付 |
|------|------|------|
| 找到「回放有相机」的 tick 并回报 getpos + 截图（3.1 五样） | 有 TF2 客户端的人 | 回放 tick、getpos、fov_desired、客户区、截图 |
| 合并 `work/model-visual` 的埋点、跑 C1/C2 控制、把 `entityMaterialRanges` / `animSource` 钉成断言 | 开发（我或接手人） | 提交 + 门禁读数 |
| 在合并树跑 map/hud/audio/sky 四套门 | 开发 | 门禁读数（当前只有分支树读数） |
| pr600 绕序剔除、14 个退化面、material.noCull、pl_odyssey pin | 开发（各自立卡） | 每项一卡 |
