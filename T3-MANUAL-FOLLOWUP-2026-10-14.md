# T3 · 人工验收与真实输入（manual-followup）

- 任务：T3「人工验收与真实输入」（本轮四任务分工：T1 世界几何 / T2 水面与高级材质 / T3 人工验收 / T4 稳定性与发布）。
- 分支 / worktree：`task/manual-followup` / `D:\TF2_Native_Worktrees\manual-followup`
- 基线 commit：`3c92f7d`（`fix: make camera HUD gate encoding-safe`；含世界材质 `b7d8035`、抓帧确定性 `1c69f08`、相机/HUD `bf0d268` 三轮整合）
- 执行时间：2026-10-13 23:05 – 2026-10-14 00:45（本机时钟；跨午夜，报告按完成日 2026-10-14 命名）
- 约束：**不改产品源码、不改自动门禁判据**；全部新增都在 `evidence/manual-followup/`、本文件与四份状态文档的 T3 状态条目（工作树状态见 §11）。
- 观察方式的边界：本轮的"人工观察"是**查看抓帧 PNG 与产品读数**，不是有人坐在屏幕前看实时画面；涉及"可听/可看"的结论按此边界写在 §10。

## 1. 结论摘要

| 项 | 结果 |
|---|---|
| 1. ViewModel 实体 | 找到：`autorecord_2026-07-02_13-26-46.dem`（pl_odyssey POV，ticks=39122）——ViewModel 实体 **1077**、武器实体 **1008** `CTFSniperRifle` → `c_pro_rifle`（`m_iWorldModelIndex=533`）；tick 1000/2000/3000/6000/16306 五次采样全部存在 |
| 2. ViewModel 开关 | 关 → 开：帧差 **3.0638%**（79118 B，max_delta 217） |
| 2. 武器本体开关 | 开 → 关：帧差 **1.6345%**（**42209 B**，max_delta 178）——与 T2 报告引用的 POV 验收读数（on/off 帧差 42,209 字节）逐字节一致；开启时画面多出一支灰色步枪（§6 有对照描述） |
| 2. 武器切换 | **未确认**：本录制 11044 次武器观测全部是同一把（entity 1008 / `CTFSniperRifle` / `m_iWorldModelIndex=533`），未见切换 |
| 2. ViewModel 动画画面 | **未确认（无可用绑定）**：`scene: vmAnim … status=missing poseAvailable=0 applied=0`；`--viewmodel-apply-animation` 与默认帧**逐字节相同**（0 B 差）；构造的 `--viewmodel-sequence 0` 改变 1.4293% |
| 3. 真实键盘 | SPACE 单击 → 第三人称；SPACE 双击（两次按下间隔 ≈150 ms，窗口 300 ms）→ free-look；P → 暂停（HUD `tick=` 冻结 2.0 s）/恢复（tick 262→436 推进）。产品自记：`spacePresses=3 spaceSingles=2 spaceDoubles=1 pauseToggles=2 modeChanges=2`；trace 里全运行唯一的 tick 停滞段与两次 P 的按下时刻对齐（§5，三重独立证据）。按键经 WM_KEYDOWN/WM_KEYUP 消息投递（与物理键同一条消息路径；见 §10 的边界）。本轮同时定位并绕过了一个标题读数仪器缺陷（§5 末） |
| 4. 窗口缩放 / DPI | 默认帧 1264x681；窗口 800x600 → 客户区 784x561 → 帧 **784x561**；窗口 1400x900 → 客户区 1384x861 → 帧 **1384x861**（交换链跟随客户区，帧头为证）。系统 DPI **96（100%）**、显示器 2560x1440、窗口 DPI 96；其它 DPI 档未实机切换（未确认） |
| 5. 人工观察 | ViewModel 与武器本体可见且可开关（见 §6）；HUD 行已在窗口标题上引到（§6.3，含 tick/demo/state/camera/error 五个字段）；天空为**纯色**（无天空盒纹理）；**水面未入画**（未确认） |
| 6. 声音 | 真实设备（`--audio-device 6`）：`audio_mode=device`、设备打开、`events_seen=1037 events_played=601 events_skipped=48 events_missing=388`；null sink（`--audio-disabled`）：`events_played=0 events_skipped=649 events_missing=388`。两组分开记录（§7）；**可听性未确认** |
| 验收 | `bash verify-fast.sh` = `VERIFY-FAST=PASS (assertions_ok=43)` rc=0；`bash camera-ui-check.sh` = `CAMERA-UI=PASS` rc=0（均为完整跑完的末行） |

## 2. 任务 1：确实存在 ViewModel 实体的 demo 和 tick

四个候选各跑一次（`--capture-tick 2000 --hidden-window --audio-disabled`），用产品自己的 trace 判定：

| demo | 地图 | 场景状态 | 结论 |
|---|---|---|---|
| `autorecord_2026-07-02_13-26-46.dem`（POV） | pl_odyssey（已安装） | `scene: snapshot status=1 states=2048` | **有 ViewModel 实体**：`scene: vmAnim entity=1077 sequence=1/28/29/30 …`、`scene: weaponBody entity=1008 class=CTFSniperRifle worldModelIndex=533 path=…/c_pro_rifle.mdl` |
| `bb841c6d…matcha…cp_snakewater_final1.dem` | cp_snakewater_final1（已安装） | `status=3 states=0` | 无实体（与 T2 报告一致：matcha 录不到 ViewModel 实体） |
| `73.dem`（SourceTV） | 未安装 | `status=3 states=0`，world `unavailable` | 无实体 |
| `4a9bfb…koth_bagel_rc13.dem` | 未安装 | `status=3 states=0`，world `unavailable` | 无实体 |

ViewModel 请求只在"被告知画什么模型"时发生（`viewModelRequested = !commandViewModel.empty()`，`main.cpp:2204`），所以后续所有开关用例都显式给 `--viewmodel models/weapons/v_models/v_scattergun_scout.mdl`（与 `weapon-body-check.sh` 同一参数）。tick 2000 上 `gpu: viewmodel request … status=ok resolution=3 companions=1 meshReady=1 attachments=5 valid=5 … alignment=weapon_bone fov=90.0 hand=right reason=-`。

## 3. 任务 2：开关矩阵（先修方法论，再读数）

**第一遍矩阵的教训（必须写在前面）**：直接对比不同用例的帧会读到 22% 的"差异"，但 `--viewmodel-weapon-body on` 与默认逐字节相同的两个用例（control）也差 22%——真因是**抓帧落点漂移**：`--capture-tick 2000` 的语义是"第一个 tick ≥ 2000 的 draw"，同一命令两次可以落在 2000/2001/2002/2007。`weapon-body-check.sh` 的解法是重试到落点相等；本轮沿用（最多 8 次，逐次记录落点），11 个用例最终**全部落在 tick 2000**（重试次数 1–7，见 `logs/matrix-landings.txt`）。

落点钉住后的读数（tick 2000，pl_odyssey POV，全部 1264x681）：

| 用例对 | 差异字节 | 占比 | max_delta | 说明 |
|---|---|---|---|---|
| ViewModel 关 → 开 | 79118 | 3.0638% | 217 | 关 = 不给 `--viewmodel`；开 = companion |
| companion → command | 95361 | 3.6928% | 217 | 画实体自己的模型 vs 画命令指定的模型 |
| 武器本体 开 → 关 | **42209** | **1.6345%** | 178 | 与 T2 的 POV 读数（42,209 B）一致 |
| 手 right → left | 84681 | 3.2792% | 217 | 镜像 |
| FOV 40 → 120 | 475456 | **18.4117%** | 217 | 视野 |
| 动画默认 → seq0 | 36909 | 1.4293% | 217 | 构造的正例 |
| **对照**：默认 vs 显式 `on` | **0** | 0.0000% | 0 | 证明默认就是 on（第一遍的 22% 是落点漂移） |
| **对照**：默认 vs 显式 `right` | **0** | 0.0000% | 0 | 默认即右手 |
| **对照**：不给动画 vs `--viewmodel-apply-animation` | **0** | 0.0000% | 0 | 应用了动画但**没有可应用的绑定**（§4） |

三个零差异对照同时证明：同 flags 两次运行是逐字节确定的；"显式给出默认值"不改变画面；动画开关在无绑定时不产生任何变化。

## 4. 任务 2（续）：武器切换与 ViewModel 动画

- **武器切换：未观察到**。tick 1000/2000/3000/6000/16306 五次运行、以及单次运行内 11044 条 `scene: weaponBody` 观测，全部是 `entity=1008 class=CTFSniperRifle worldModelIndex=533`。本录制在这段区间不含换武器；"切换路径的正确性"因此**未确认**（切换发生时产品应换画另一个 `m_iWorldModelIndex`，本轮没有素材验证）。
- **ViewModel 动画：输入在变，姿态不可用**。同一段运行内 ViewModel 实体的序列在 1/28/29/30 之间变化（`scene: vmAnim entity=1077 sequence=…`），但每一条都是 `status=missing poseAvailable=0 applied=0 reason=sequence has no animation binding`——这正是 T2.5 的结论（`c_sniper_arms` 的序列表没有这些序列的动画绑定），于是画面停在 bind pose：`--viewmodel-apply-animation` 与默认帧**0 字节差**。构造的 `--viewmodel-sequence 0`（唯一有绑定的序列）改变 1.4293%，是"动画通路本身能改画面"的唯一正向证据。

## 5. 任务 3：真实键盘（SPACE 单击 / SPACE 双击 / P）

方法：产品窗口以可见方式启动（POV demo，`--capture-tick 2000`，`--audio-disabled`），脚本按 PID 找到窗口后**投递 WM_KEYDOWN/WM_KEYUP 消息**（抢焦点路径 `SetForegroundWindow` 实测被 Windows 焦点策略拒绝、报 `pywintypes.error: (0, 'SetForegroundWindow', ...)`；消息投递走的正是物理键进入窗口的同一条路径，且不需要抢焦点）。按键脚本跑了两轮：第一轮（`keys.py`）暴露了一个**标题读数仪器缺陷**（见本节末），第二轮（`keys2.py`）用修正后的读法重跑，本节的表与读数以第二轮为准；第一轮的暂停结论由 trace 补足。

第二轮读数（HUD 标题，读法见下；脚本先按键、等待 1.6 s、再记录，故**按键时刻 ≈ 记录时刻 − 1.6 s**）：

| 步骤 | 按键时刻（≈，T+） | HUD 读数（记录时刻 → tick / camera） |
|---|---|---|
| 找到含 HUD 的完整标题（3135 字符） | — | 16.08 s |
| 基线 | — | 16.09 s → tick=0 / first-person |
| SPACE 单击 | 16.13 s | 17.73 s → tick=78 / **third-person** |
| SPACE 双击（两次按下间隔 ≈150 ms） | 17.7 s | 19.51 s → tick=262 / **free-look** |
| P（暂停） | ≈19.5 s | 21.14 s → tick=262 / free-look |
| 暂停保持 2.0 s | — | 23.14 s → **tick=262，冻结** / free-look |
| P（恢复） | ≈23.1 s | 24.77 s → **tick=436，恢复推进** / free-look |

三个独立证据互相吻合：

1. **HUD 标题（修正读数法）**：上表——单击进第三人称、双击进 free-look、暂停时 `tick=` 2.0 s 不变、恢复后推进。
2. **trace 的 tick 停滞段**：全运行 1768 次 draw 中，唯一一段 tick 不推进的区间是 tick=262 连画 **236 帧**（trace 19363.1→22997.2 ms ≈ 3.63 s）。trace 时钟整体比脚本计时点早 0.12–0.18 s（子进程启动开销；同一轮内是常数——两轮各自的两端自洽到几十毫秒以内），按此对齐后两次 P 的按下时刻正落在停滞段两端。第一轮（`keys.py`）给出同样的结构：1398 次 draw 中唯一停滞段是 tick=247 连画 **151 帧**（≈3.66 s），同样与两次 P 对齐。
3. **产品自记计数器**：`camera: summary mode=free-look modeChanges=2 spacePresses=3 spaceSingles=2 spaceDoubles=1 doubleWindowMs=300 pauseToggles=2 follows=70 holdsNoTarget=0 freezeFrames=1823`（第二轮；第一轮同结构，仅 follows=59 / freezeFrames=1134，见 §5 末的语义更正）。

两轮运行在相同按键序列后各自动抓帧一次（均在 free-look 下的 tick 2000），两张 BMP 的 sha256 相同（`15ae09e596d8c924…`）；`keys-after-space-pause.png` 显示相机已在远处环视（free-look 视角）。

**两处读数语义更正（本轮自查发现）**：

- `freezeFrames` **不是暂停计数**：`main.cpp:150` 的注释是 `free-look scene ticks: nothing fed the camera`，递增点在 `decideCameraSceneAction` 返回 `FreezeCurrent` 时（`main.cpp:3118–3121`），即 free-look 模式下的场景 tick 数。暂停的计数读数是 `pauseToggles=2`（P 收到两次），配合上面两条证据。本轮报告的初稿曾按字面把它写成"暂停期间的冻结帧数"，已按源码更正。
- **标题读数仪器缺陷（第一轮假阳性）**：第一轮 `keys.py` 用 `win32gui.GetWindowText` 读标题，该 API 在**跨进程窗口**上固定截断到 **511 字符**。探针实测（1200 字符标题，另一进程持有）：`GetWindowTextLengthW=1200`、`win32gui.GetWindowText` 读回 **511**、按长度分配缓冲区的读法读回 1200。第一轮窗口标题全长 3135 字符，截断点恰好落在 `HUD[` 之前，于是那轮读到的只是标题里那条**静态**资源行（含 `nettick=38555`）——"pause check: tick field frozen = True"比较的是同一段静态文本，不构成证据（保留在 `logs/keys.log` 里作为缺陷现场）。修正：用与 `camera-ui-check.sh` 相同的读法（先 `GetWindowTextLengthW` 取长度、再按长度分配缓冲区读全文）。另一个小陷阱：HUD 字段抽取不能用裸 `tick=(\d+)` 正则——标题里先出现 `nettick=<n>`（静态的解码摘要），会被抢先匹配成一个永不变化的字段；必须锚定 `HUD[tick=`。

## 6. 任务 4 / 5：窗口缩放、DPI 与人工观察

### 6.1 窗口缩放与 DPI

| 运行 | 窗口 rect | 客户区 | 帧头尺寸（BMP 自述） | sha256(16) |
|---|---|---|---|---|
| 默认 | (260,260,1540,980) | 1264x681 | **1264x681** | `9a74039cd4bab6a6` |
| 800x600 | (120,80,920,680) | 784x561 | **784x561** | `2836ffd8544a14e2` |
| 1400x900 | (120,80,1520,980) | 1384x861 | **1384x861** | `5f83611b1f6d8c58` |

帧头尺寸 == 客户区尺寸，即交换链确实跟随窗口缩放。DPI：系统 96（100%）、显示器 `\\.\DISPLAY1` 2560x1440、窗口 DPI 96。**其它 DPI 档（125%/150%）未实机切换**——那会重排整台机器的桌面；布局层面的覆盖由 `native_ui_probe` 的 42 组窗口尺寸×DPI 组合承担（`camera-ui-check.sh` 断言 18 条 + e2e，本轮通过）。

### 6.2 画面观察（查看抓帧）

- **ViewModel + 武器本体**（`frames/matrix-vm-companion.png` vs `frames/matrix-body-off.png`）：开状态下画面中部偏右有一支灰色步枪（`c_pro_rifle`，来自武器实体 1008 的世界模型索引 533）与一个红色部件；`--viewmodel-weapon-body off` 后**灰色步枪消失、其余不变**。世界是 pl_odyssey 的白色几何体，天空区域为深色。
- **自由视角**（`frames/keys-after-space-pause.png`，第一轮按键运行的抓帧；第二轮在同样步骤后抓到的 BMP 与它逐字节相同）：SPACE 双击后相机退到远处环视地图（与第一人称帧的"贴脸"视角完全不同）。
- **天空盒**：所有帧的天空区域都是**纯色**（pl_odyssey 为深色、snakewater 帧为深蓝），未见任何天空盒纹理细节——与 T3 世界材质轮的读数一致（`sky` 六纹理可解、几何不绘制）。
- **水面**：本机可用的相机位置/帧里**没有观察到水面**；世界材质轮的读数（snakewater 18 个水面、渲染侧 `waterTriangles=36`）只说明几何与计数存在，画面未覆盖 → 未确认。

### 6.3 HUD

HUD 行在**窗口标题**上（camera-ui-check.sh 的既定读数面）。按该门禁自己的方式（按 PID 找窗口、保留含 `HUD[tick=` 的标题）在 POV demo 上轮询 45 s：**231 次** HUD 标题、216 个不同版本。引一条（tick=2000 时）：

```
HUD[tick=2000 demo=autorecord_2026-07-02_13-26-46.dem state=playing camera=first-person
     error=none speed=1.000000 entity=checkpoint stale=303 audio=0/388 health=125 team=2
     class=2 clip=? uber=? snd=888/2263/14 fxDue=1298 trailVerts=0/1024 cache=ready@…
     modelMissing=0 vpkHits=244 … animSkinned=34/70 …]
```

五个必须字段（tick / demo / state / camera / error）都在；本轮没有对它做布局重叠判定（那是 `native_ui_probe` 的 42 组布局断言的职责，`camera-ui-check.sh` 已通过），**逐像素文字重叠未人工检查**。

## 7. 任务 6：声音（真实设备与 null sink 分开记录）

同一条 POV 录制、tick 2000，两次运行，读数分开：

| 运行 | 模式 | 设备 | 事件读数 |
|---|---|---|---|
| 真实设备 | `--audio-device 6` | `audio_mode=device`、设备打开 | `events_seen=1037 events_played=601 events_skipped=48 events_missing=388`；`audio: timeline read=5563 scheduled=5323 unresolved=240 kinds=w888/f2263/u14/o1647/v504/a7/m0` |
| null sink | `--audio-disabled` | `audio_mode=disabled`、`audio_device_opened=0` | `events_seen=1037 events_played=0 events_skipped=649 events_missing=388` |

**这两行不能互换阅读**：null sink 的 `played=0` 是设计行为，不是失败；真实设备的 `played=601` 是产品自己的播放计数，也不是"人耳听到了 601 个声音"。开火/脚步/Uber/环境音是否可听 → **未确认**（§10）。

## 8. 验收命令（最终树）

```
bash verify-fast.sh      → VERIFY-FAST=PASS (assertions_ok=43)   rc=0
bash camera-ui-check.sh  → CAMERA-UI=PASS                        rc=0
```

两条都在本 worktree 的最终提交状态上完整跑完（非截断）。`camera-ui-check.sh` 的 e2e 段还给出了它自己的对照读数：`camera: summary … spacePresses=0 pauseToggles=0`（门禁运行不发按键）与 `mode: summary … audio_mode=disabled audio_device_opened=0`。

## 9. 截图清单（每帧：demo / 落点 tick / 模型路径 / FOV / hand / 相机 / sha256）

全部帧：demo = `autorecord_2026-07-02_13-26-46.dem`（pl_odyssey POV），原始 BMP 1264x681（除非另注）。**表中 23 帧全部已转 PNG 入库** `evidence/manual-followup/frames/`（PNG 的 sha256 见 `SHA256SUMS`；另加 `hud-bagel.png` 共 24 张）；表中给出的是**原始 BMP** 的 sha256(16)——BMP 每张 2.5 MB，留在 `.scratch/` 不入库。逐字节相同的用例共用同一画面（本节末注）。

| 帧 | 落点 tick | 模型路径 | FOV | hand | 相机 | 原始 BMP sha256(16) |
|---|---|---|---|---|---|---|
| matrix-vm-off | 2000 | -（未请求 ViewModel） | - | - | first-person | `9a74039cd4bab6a6` |
| matrix-vm-companion | 2000 | `models/weapons/v_models/v_scattergun_scout.mdl` | 90.0 | right | first-person | `6f1da97e3d30c014` |
| matrix-vm-command | 2000 | 同上（command 渲染） | 90.0 | right | first-person | `1f492911bd1416da` |
| matrix-body-on | 2000 | 同上 | 90.0 | right | first-person | `6f1da97e3d30c014` |
| matrix-body-off | 2000 | 同上 | 90.0 | right | first-person | `381d91c72c292068` |
| matrix-hand-left | 2000 | 同上 | 90.0 | left | first-person | `e644228a2461d680` |
| matrix-hand-right | 2000 | 同上 | 90.0 | right | first-person | `6f1da97e3d30c014` |
| matrix-fov-40 | 2000 | 同上 | 40.0 | right | first-person | `80a50c5670660f30` |
| matrix-fov-120 | 2000 | 同上 | 120.0 | right | first-person | `f5b2b10783f5ed9c` |
| matrix-anim-on | 2000 | 同上 | 90.0 | right | first-person | `6f1da97e3d30c014` |
| matrix-anim-seq0 | 2000 | 同上 | 90.0 | right | first-person | `289e6531fe7b7283` |
| switch-t1000 | 1001 | 同上 | 90.0 | right | first-person | `660010b4479fb3a4` |
| switch-t2000 | 2001 | 同上 | 90.0 | right | first-person | `6f1da97e3d30c014` |
| switch-t3000 | 3000 | 同上 | 90.0 | right | first-person | `90a13b9611a7c069` |
| switch-t6000 | 6001 | 同上 | 90.0 | right | first-person | `553c820ddad6f744` |
| switch-t16306 | 16307 | 同上 | 90.0 | right | first-person | `648f0949c180dca1` |
| keys-after-space-pause | 2000 | - | - | - | **free-look**（按键后） | `15ae09e596d8c924` |
| resize-default | 2000 | - | - | - | first-person | `9a74039cd4bab6a6` |
| resize-800x600（帧 784x561） | 2001 | - | - | - | first-person | `2836ffd8544a14e2` |
| resize-1400x900（帧 1384x861） | 2002 | - | - | - | first-person | `5f83611b1f6d8c58` |
| sound-real-device | 2001 | - | - | - | first-person | `9a74039cd4bab6a6` |
| sound-null-sink | 2001 | - | - | - | first-person | `9a74039cd4bab6a6` |
| hud-pov | 2000 | - | - | - | first-person | （`hud2.bmp`，见 SHA256SUMS） |

注：`resize-default` / `sound-*` 与 `matrix-vm-off` 的 BMP sha256 相同（`9a74039c…`）——它们是同一条命令在不同阶段的重跑，逐字节一致本身是抓帧确定性的一个旁证。两轮按键运行（`keys.py` / `keys2.py`）在同样步骤后各自在 free-look 状态下抓到 tick 2000 帧，两张 BMP 的 sha256 也相同（`15ae09e5…`）——跨运行的又一个确定性读数。

## 10. 未确认项清单

1. **武器切换**：本录制在 1000–16306 的采样区间内没有换武器（11044 次观测同一把）；切换路径**未确认**。
2. **ViewModel 动画画面**：录制带的序列（1/28/29/30）在该模型上**没有动画绑定**（`status=missing`），画面不随 `--viewmodel-apply-animation` 变化；只有构造的 `--viewmodel-sequence 0` 产生 1.43% 的变化。"动画看起来对不对"**未确认**。
3. **物理键盘**：按键以 WM_KEY* 消息投递到真实窗口，不是人手敲击；消息路径与三个独立读数（修正后的 HUD 标题、产品计数器、trace 停滞段）一致，但"物理键盘驱动链"**未确认**（本环境没有可驱动的物理输入设备）。
4. **声音可听性**：真实设备打开且产品计数 `played=601`，但没有人耳确认声音内容/音量/延迟；**未确认**。
5. **其它 DPI 档**（125%/150%）与多显示器切换：**未确认**（未改系统 DPI）。
6. **天空盒纹理**：所有帧的天空区域是纯色，未见天空盒细节；"天空盒该有的样子"**未确认**（资源读数在 T3 世界材质轮）。
7. **水面**：未出现在任何采样画面里；**未确认**。
8. **HUD 文字重叠**：只做了字段存在性与标题引用，未逐像素检查文字重叠（布局层面由 `native_ui_probe` 的 42 组断言覆盖，门禁通过）。
9. **人工"人眼"对标**：本轮观察由 AI 查看抓帧完成，不是实时屏幕前的人工观看；画面观感（颜色、光照是否"像 TF2"）**未确认**。
10. **武器本体跟随切换**：没有切换素材，故"换武器后本体是否换模型"**未确认**。

## 11. 证据路径、哈希与工作树

- 证据目录：`evidence/manual-followup/`（58 个文件）：`frames/`（24 张 PNG：§9 表 23 帧 + `hud-bagel`；逐字节相同的用例共用画面，见 §9 末注）、`traces/`（26 份完整 `TF2_NATIVE_TRACE`，含两轮按键运行）、`logs/`（7 份：按键时间线两轮、缩放、HUD 轮询、矩阵落点、标题读数探针）、`SHA256SUMS`（57 项全过）。
- 关键日志：`logs/keys.log`（第一轮按键步骤与时间；含被截断的标题读数——缺陷现场）、`logs/keys2.log`（第二轮，修正读数法；§5 的表引自此）、`logs/title-probe.log`（`win32gui.GetWindowText` 511 字符截断的实测）、`logs/matrix-landings.txt`（每个用例的重试次数与落点）、`logs/resize.log`（窗口/客户区/DPI）、`logs/hud-pov.log`（HUD 标题轮询）。
- 复核方式：`cd evidence/manual-followup && sha256sum -c SHA256SUMS`（本轮工作树内 44/44 通过）。注意本仓库 `core.autocrlf=true`：fresh checkout 会把该文件行尾变成 CRLF，GNU `sha256sum -c` 会因路径带 CR 而失败（既有先例文件同样如此）——此时用 `tr -d '\r' < SHA256SUMS | sha256sum -c -`。若要让哈希清单跨 checkout 直接可用，建议后续统一加 `.gitattributes` 的 `-text` 规则（本轮不改，避免超出"只动 evidence 与报告"的边界）。
- 工作树：除 `evidence/manual-followup/`、本文件与四份状态文档的状态条目（`CHANGELOG.md`、`README.md`、`HANDOFF-2026-10-07.md`、`IMPLEMENTATION-PLAN-2026-10-10.md`，均为"只新增本节"）外无改动；产品源码与门禁脚本**未触碰**（`git status --porcelain` 只显示本任务的新增/这些文档的状态条目）。
