# N17 Phase C 目视缺陷：归因与修法（2026-11-21）

卡片：task-30「N17 目视缺陷：legacy 相机埋躯干 + posegrid 红色尖刺/巨手（Phase C 后）」。
写域：native/src/animation_binding.cpp、native/src/main.cpp。**本卡只改了 native/src/main.cpp**，animation_binding.cpp 一行未动。

## 0 结论

1. 卡条目 1)「legacy 在 1500/2500 相机埋自己躯干」与 2)「posegrid 在 3500/4500/5500 红色尖刺 + 顶部巨手」，在本轮开工时的构建上**已经不复现**：task-9（提交 `ff79365b`：世界通道自身隐藏 + T11 位移绕序默认关）合入后，四个 tick 的修前帧逐帧干净（§3）。本卡没有为 1)/2) 增加任何代码。
2. 卡条目 3)（pose 输入侧：aim_yaw 锚点错+符号反、move_y 镜像）确认存在并已修，两处都在 `native/src/main.cpp`（§4）。
3. 默认路径零影响：本卡改动前后，"不开开关"与 `--animation-blend legacy` 两条 arm 在 tick 3000 的抓帧 md5 都是 `bdeca8c2...`，逐字节相同（§5.3）。
4. 卡上"默认路径 bmp md5 `9681f12c9e672ae896105f7756b3bf5f` 必须保持"这条**不是本卡能保持的**，因为它早于 task-9：用 `--world-show-own-body` 把自身隐藏退回旧行为后，同命令抓帧逐字节复现 `9681f12c...`（§5.3）。这条 pin 的差异来源是 task-9，不是 task-30。

## 1 对读记录（docs/REFERENCE-REPOS-2026-11-21.md §5）

- §5 为本任务指定的库：`source-sdk-2013` 的 `c_baseplayer/camera` 与 `TF2-Base`。
- 本机两处均无副本（`ops_glob` 在 `D:/` 找 `source-sdk-2013` / `TF2-Base` 目录，0 命中）⇒ **未对读外部源码**。因此本卡所有判断来自本仓执行与 trace，不引用任何外部实现。
- 语义参照（此前会话已读，**未复制一行**；外部库许可证不同）：wombo14/tf2-demo-analyser build-4（GPL-3.0）`Demo Analysis/source/src/player/view.rs:1676-1697` 的 recorder/spectator 眼高判据、`view.rs:2047-2050` 的第一人称跳过本体。
- 本仓同源证据：`docs/N18-FIRSTPERSON-VISIBILITY-2026-11-20.md` §2/§3；`native/src/main.cpp:695`（`kSpectatorEyeHeight=68`）与 `:703`（`kSpectatorBackUnits=0`，T81 注释解释了为何贴眼）。

## 2 复现装置

- 脚本：`.scratch/n17-review/phasec/phasec-capture.sh <tag> [exe] [extra args...]`；tick 用 `PHASEC_TICK`；legacy 臂 = 只给 `--player-animation`；posegrid 臂 = 追加 `--animation-blend posegrid`。
- demo：`D:/TF2_Demo_Player/testdata/demos/bb841c6d379ff7c40d0c8baf99f59d8d_matcha-20260927-1347-cp_snakewater_final1.dem`；TF：`D:/SteamLibrary/steamapps/common/Team Fortress 2/tf`。
- 输出 1264×681（window outer 1280×720），每帧 74–113 s，rc=0；每臂留 `.bmp` + `.trace.txt` + `.csv` + `.rc`。目录 `.scratch/n17-review/phasec/`。
- 修前 = task-9 提交后、本卡改动前的构建；修后 = 本卡两处 hunk 之后重建（仅 `native/src/main.cpp` 变化）。

## 3 条目 1)/2) 的现状：修前帧已经干净

修前帧（未含本卡任何改动）在四个 tick 上逐帧目视：

| 帧 | md5 | 目视 |
| --- | --- | --- |
| `t30-pre-legacy3000` | `bdeca8c24ec4fa22113c6b55d7419430` | 正常世界 + 两个他人玩家；**没有整屏衣服内壁** |
| `t30-pre-pg3500` | `23d3929634308aad5c8271ca0adc0dc3` | 空房间（视锥内无玩家躯体），无尖刺/巨手 |
| `t30-pre-pg4500` | `84779acb9ddc6eb5c7561fb75480be3d` | 干净越肩背影 + 两个远景玩家；**无红色尖刺、无顶部巨手** |
| `t30-pre-pg5500` | `a7b407f7962e894221fe8b47e74e68c3` | 干净背影；**无红色尖刺、无顶部巨手** |

所以卡上的两处目视缺陷，是 task-9 的"相机视点自身隐藏"（世界通道把被观察者从 draw 列表里剔掉）与 T11 位移绕序默认关一并消掉的：症状是"从自己模型内部往外看/自己模型的拉伸三角形盖住整屏"，不是 blend 矩阵缺 clamp。`t30-pre-legacy3000` 的 trace 里 `scene: cameraFilter removed=1 ... hidden0=3` 与 `docs/N18-FIRSTPERSON-VISIBILITY-2026-11-20.md` 的通道判定一致。

## 4 条目 3)：pose 输入的两处缺陷与修法

### 4.1 缺陷

修前 `native/src/main.cpp`（`--player-animation` 分支）：

```cpp
request.pose.aimYaw = static_cast<float>(viewYaw - bodyHeadingDegrees);   // 旧
request.pose.moveY  = normalisedSpeed * static_cast<float>(-std::sin(relative));  // 旧
```

两处都不对：

- `aimYaw` 用了**速度朝向** `bodyHeadingDegrees`（`native/src/main.cpp:13802-13841`，`atan2(dy,dx)` 的位移方向，持久化在 `runtime.headingDegrees`）。CTFPlayer 不网络 `m_angRotation`，被画的身体就是按视图 yaw 画的，所以"视图相对被画身体"的横向角就是 0。旧式在站着不动（速度朝向是上一次移动的残留值）时也给出非零值，实测 198.92 / −60.64 / +52.33 / +121.64 度（`.scratch/n17-review/phasec/phasec-report.md`）。轴还是 Source yaw 的左正，而网格契约右正。
- `moveY` 的负号把横向输入做成了行进方向的反号（契约是 move_y 正 = 右）。

消费点（`native/src/animation_binding.cpp:675-693`）：`BodyYaw` 轴吃 `input.aimYaw`，`MoveY` 轴吃 `input.moveY`，取值再按网格自身的 `paramStart..paramEnd` 做 clamp（`animation_binding.cpp:690-693`）。所以旧 `aimYaw` 不是"瞄准层多转一点"，而是把**整具身体的网格采样位置**在 BodyYaw 轴上推偏（3×4 瞄准矩阵的 yaw 轴范围 ±45），腿/脚/躯干一起换姿势——这正是修后差异掩码覆盖整具身体、而背景逐像素不变的原因（`.scratch/n17-review/phasec/diff-pg4500.png`）。

### 4.2 修法（native/src/main.cpp，2 个 hunk）

- `aimYaw = 0.0f`。注释写清：今天被画的身体 yaw 就是 `viewYaw`；横向偏移为 0；旧的"速度朝向"不是身体朝向；轴左正 vs 网格右正两个缺陷一起消失。Step 5（把身体按 heading 画、再从 heading 瞄准）之后才应是 `-(viewYaw - bodyHeadingDegrees)`，那是另一张卡。
- `moveY = normalisedSpeed * sin(relative)`（去掉前导负号）。`moveX` 原本就对（`+cos(relative)`）。
- `aimPitch = -instance.transform.angles[0]` 保持不动（cos/sin 契约已验证正确，posecheck 里 `aimPitch=-pitch`）。
- 不改 `kSpectatorBackUnits`：T81 的贴眼理由仍然成立，且 1)/2) 的症状不是相机后退量能解的。不加 `c_*_arms` 的世界通道排除：本 demo 的世界 pass trace 里没有任何 `models/weapons/` 行（N18 §3 同样结论）。

## 5 验收读数

### 5.1 同 tick 修前/修后（1264×681，逐像素 RGB 差）

| 臂 | 修前 md5 | 修后 md5 | 变化像素 | 变化 bbox |
| --- | --- | --- | --- | --- |
| legacy @3000 | `bdeca8c24ec4fa22113c6b55d7419430` | `bdeca8c24ec4fa22113c6b55d7419430` | **0** | — |
| posegrid @3500 | `23d3929634308aad5c8271ca0adc0dc3` | `23d3929634308aad5c8271ca0adc0dc3` | **0** | — |
| posegrid @4500 | `84779acb9ddc6eb5c7561fb75480be3d` | `f4cc38cc645333a4d1c0aa77c66f10b9` | **52859** | (485,183)-(1264,601) |
| posegrid @5500 | `a7b407f7962e894221fe8b47e74e68c3` | `9e78069e42f353165053583ffa3d8bbc` | **13477** | (621,243)-(781,502) |

- `legacy @3000` 0 变化 = 默认/legacy 路径不吃 pose 输入（`phasec-report.md` 已确认 legacy 不填 pose），修法落在 `--player-animation` 的 posegrid 分支，符合预期。
- `posegrid @3500` 0 变化 = 该 tick 视锥内没有任何玩家躯体（`t30-pre-pg3500.bmp` 是空房间，见 §3 表），不是修法失效。
- `@4500` / `@5500` 的变化掩码只落在被观察玩家身上，背景 0 像素变化（`.scratch/n17-review/phasec/diff-pg4500.png`、`diff-pg5500.png`）。

### 5.2 修后 posecheck（`TF2_NATIVE_POSECHECK=1`，同 tick 4500）

```
anim: posecheck tick=10320 ent=2 model=models/player/soldier.mdl mode=posegrid state=idle
  pitch=20.118 yaw=92.551 moveX=-0.0000 moveY=0.0000 aimYaw=0.000 aimPitch=-20.118 aimW=0.0000 bones=86
anim: posecheck ... ent=3 model=models/player/sniper.mdl mode=posegrid ...
  pitch=-7.412 yaw=91.496 moveX=-0.0000 moveY=0.0000 aimYaw=0.000 aimPitch=7.412
anim: posecheck ... ent=4 model=models/player/demo.mdl mode=posegrid ...
  pitch=18.706 yaw=92.551 moveX=-0.0000 moveY=0.0000 aimYaw=0.000 aimPitch=-18.706
```

`aimYaw=0.000`、`aimPitch=-pitch`、idle 时 `moveX/moveY=0`（`.scratch/n17-review/phasec/t30-pc4500.trace.txt`）。`aimW=0.0000` 是当前 idle 的层权重，不影响上面的轴取值。

### 5.3 默认路径 pin 的控制实验

| 跑法（tick 3000） | md5 | 说明 |
| --- | --- | --- |
| 不加开关（`t30-def3000`） | `bdeca8c24ec4fa22113c6b55d7419430` | 修后默认路径 |
| `--animation-blend legacy`（`t30-blendlegacy3000`） | `bdeca8c24ec4fa22113c6b55d7419430` | 与默认逐字节相同 |
| 不加开关 + `--world-show-own-body`（`t30-def3000-ownbody`） | `9681f12c9e672ae896105f7756b3bf5f` | **逐字节等于卡上的 pin** |
| 修前默认（`t30-pre-legacy3000` 同 exe 前身） | `bdeca8c24ec4fa22113c6b55d7419430` | 与修后相同 ⇒ 本卡对默认路径零影响 |

结论：`9681f12c...` 是**自身隐藏之前**的基线（`docs/REF-ADOPTION-2026-11-20.md:33` 记录它时 task-9 尚未合入），它被 `--world-show-own-body` 逐字节复原；task-30 的两处 hunk 对"不开开关/legacy"两条 arm 一个像素都没动。

## 6 复现命令

```bash
cd /d/TF2_Native_Worktrees/t105-integrate
# 修后四臂（各约 74-113 s）
PHASEC_TICK=3000 bash .scratch/n17-review/phasec/phasec-capture.sh t30-post-legacy3000 native/build-nmake/tf2_demo_native.exe
PHASEC_TICK=3500 bash .scratch/n17-review/phasec/phasec-capture.sh t30-post-pg3500  native/build-nmake/tf2_demo_native.exe --animation-blend posegrid
PHASEC_TICK=4500 bash .scratch/n17-review/phasec/phasec-capture.sh t30-post-pg4500  native/build-nmake/tf2_demo_native.exe --animation-blend posegrid
PHASEC_TICK=5500 bash .scratch/n17-review/phasec/phasec-capture.sh t30-post-pg5500  native/build-nmake/tf2_demo_native.exe --animation-blend posegrid
# posecheck（同 tick 4500）
TF2_NATIVE_POSECHECK=1 PHASEC_TICK=4500 bash .scratch/n17-review/phasec/phasec-capture.sh t30-pc4500 native/build-nmake/tf2_demo_native.exe --animation-blend posegrid
# pin 控制臂
PHASEC_TICK=3000 bash .scratch/n17-review/phasec/phasec-capture.sh t30-def3000-ownbody native/build-nmake/tf2_demo_native.exe --world-show-own-body
# 逐像素差（.scratch/n17-review/phasec/t30_diff.py）
python .scratch/n17-review/phasec/t30_diff.py
```

## 7 未闭环

- posegrid 姿态的"源引擎保真"仍差 `SEQUENCE_DELTA`（跨层 delta 通道），本卡只修输入轴，没有新增 blend 能力；逐骨骼 A/B 不在本卡写域。
- `9681f12c...` 这个 pin 的正式对账应把 task-9 自身隐藏作为**独立变量**记进 `docs/REF-ADOPTION-2026-11-20.md:33`（本卡不写那份文档，避免与 task-9 报告的口径分叉）。
- 相机后退量（`kSpectatorBackUnits`）与 `c_*_arms` 世界通道排除两项都判断为不需要（理由见 §4.2），但如果后续有人复现"埋躯干"，应先用 `--world-show-own-body` 确认是不是把 task-9 的隐藏关掉了，而不是调相机。
