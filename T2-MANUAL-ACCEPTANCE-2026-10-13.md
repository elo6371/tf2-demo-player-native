# T2 人工验收证据（2026-10-13，`task/manual-acceptance`）

> **后续人工确认（2026-10-13）**：用户已通过真实设备确认声音内容。下文保留原始交付时的机器证据边界；该后续确认补齐了声音听感项，不改变矩阵对动画、材质或武器本体的限制。

工作区 `D:\TF2_Native_Worktrees\manual-acceptance`，分支 `task/manual-acceptance`，
基线 `origin/p0-entity-protocol@0f023bb`（`docs: record T3 integration and fix audio input line endings`）。
本任务**只产证据，未改任何产品源码**（见 §7 工作树状态）。

一句话结论：`--viewmodel` 输入确实改变画面（无 ViewModel 对照相差 57.986%），
FOV 与手性两个维度在本 demo 上**可分离**（各自帧 sha256 不同）；
tick 2000 与 4000 两帧**逐字节相同**、6000 不同；武器本体 on/off 两帧
**逐字节相同**，原因是该 demo 在这些播放位置没有选中任何 ViewModel 实体
（读数 `weaponFallback=noViewModelEntity weaponVisible=0 weaponDrawCalls=0`）。
真实设备（6 号 xduoo）播放成功：`audio_device_opened=19 == events_played=19`，
原始采集时没有记录人耳确认；后续用户已通过真实设备确认声音内容。

---

## 1. 范围与坐标约定

| 项 | 值 |
|---|---|
| demo | `bb841c6d379ff7c40d0c8baf99f59d8d_matcha-20260927-1347-cp_snakewater_final1.dem` |
| ViewModel 模型 | `models/weapons/v_models/v_scattergun_scout.mdl` |
| tf root | `D:/SteamLibrary/steamapps/common/Team Fortress 2/tf` |
| 音频设备 | 6（`xduoo audio`，真实输出；`--list-devices` 实测，共 13 个设备） |
| 统一参数 | `--hidden-window --audio-disabled`（像素矩阵）/ 真实音频去掉 `--audio-disabled` |
| 分辨率 | 1264x681x24，全帧 2,582,406 字节 |

`--capture-tick` 是**播放位置**而非 demo tick：本 demo 实体从 tick 10252 起，
播放位置 0 即 demo tick 10252（实测 tick=2001 ↔ sceneTick=12253，tick=4000 ↔ sceneTick=14252）。
2000/4000/6000 均为合法播放位置（与 `weapon-animation-check.sh` 的取值一致）。

区域定义与 ViewModel 材质门禁相同，使两份报告谈的是同一批像素：
ViewModel 复合区 `420,250,424,431`；世界区为ViewModel 之外三条带
（左 `0,0,420,681`、右 `844,0,420,681`、上 `0,0,1264,250`）。

## 2. 矩阵读数（每格 = 一次产品运行）

命令模板（每格只换 `--viewmodel-fov` / `--viewmodel-hand` / `--capture-tick`）：

```text
tf2_demo_native.exe --tf-root <tf> --demo <demo> --audio-device 6
  --hidden-window --audio-disabled --settings-file <每格独立 ini>
  --viewmodel models/weapons/v_models/v_scattergun_scout.mdl --viewmodel-render command
  --viewmodel-fov <40|90|120> --viewmodel-hand <left|right> --viewmodel-weapon-body on
  --capture-tick <tick> --capture-frame <bmp> --metrics-file <csv>
```

`--settings-file` 每格独立是必须的：产品退出时会写用户真实设置文件，
 pilot 因此出现过命令行写 90、读数 `settingsFov=40.0`（继承了上一轮的值）。

完整逐格读数见 `evidence/manual-acceptance/matrix2.csv`（8 格，全部 rc=0、size=2,582,406）：

| 单元格 | fov | hand | tick | sha256（前 16 位） | armDrawCalls | weaponDrawCalls | weaponVisible | VM 区差异 | 世界区差异 |
|---|---|---|---|---|---|---|---|---|---|
| r2-f40-left-t2000 | 40 | left | 2000 | `a0d36dbfedae2324` | 1444 | 0 | 0 | 66.1221% | 26.3170% |
| r2-f40-right-t2000 | 40 | right | 2000 | `8fcbf4ba8f20c858` | 1360 | 0 | 0 | 66.7571% | 25.6082% |
| r2-f90-left-t2000 | 90 | left | 2000 | `d9e7862a77f7f659` | 1501 | 0 | 0 | 0.0000%（基准） | 0.0000% |
| r2-f90-right-t2000 | 90 | right | 2000 | `6f676993389c6c16` | 1510 | 0 | 0 | 93.2964% | 6.7036% |
| r2-f120-left-t2000 | 120 | left | 2000 | `4380806146c9ca38` | 1394 | 0 | 0 | 95.0549% | 4.9451% |
| r2-f120-right-t2000 | 120 | right | 2000 | `0177e11d483581e5` | 1449 | 0 | 0 | 95.0589% | 4.9411% |
| r2-f90-left-t4000 | 90 | left | 4000 | `d9e7862a77f7f659` | 2920 | 0 | 0 | 0.0000% | 0.0000% |
| r2-f90-left-t6000 | 90 | left | 6000 | `e42c0d18e53f2c49` | 4371 | 0 | 0 | 100.0000% | 0.0000% |

区域差异以 `r2-f90-left-t2000`（编译默认 FOV 90 + left）为基准计算
（`evidence/manual-acceptance/regions.csv`）。每格还记录：模型路径
`models/weapons/v_models/v_scattergun_scout.mdl`、attachment
`ok(valid=5,partial=0,invalid=0,total=5)`、FOV 与 hand 读数与命令行一致。

### 两个维度的实测结论（重要）

1. **tick 2000 与 4000 逐字节相同**：两帧 sha256 均为 `d9e7862a77f7f659`，
   `--min-delta 1` 比对 differing=0。这是独立复现的结果（缩减矩阵第一轮
   与矩阵第一轮各测一次，`matrix.csv` 的 f40 组合同样相同）。世界冻结 +
   ViewModel 位姿在这两个播放位置相同。tick 6000 的帧不同
   （`e42c0d18…`，VM 区差异 100%、世界区 0%），所以"相同"不是演示路径的假象。
2. **武器本体 on/off 逐字节相同**：两格 sha256 均为 `a0d36dbf…`，
   读数解释原因——该 demo 在这些播放位置未选中任何 ViewModel 实体，
   `weaponFallback=noViewModelEntity`、`weaponVisible=0`、`weaponDrawCalls=0`。
   因此本任务**没有**测量到武器本体开关对画面的影响；这一维度保持未验证。

## 3. 输入确实改变画面（防止"什么都没测到"）

| 对照 | differing | percent | max_delta |
|---|---|---|---|
| f40/left（有 ViewModel） vs pilot2（无 `--viewmodel`） | 1,497,402 | 57.9860% | 153 |
| f40/left vs f90/left | 268,116 | 10.3826% | 153 |
| tick 2000 vs tick 4000（同参数） | 0 | 0.0000% | 0 |

第一行证明 ViewModel 复合体真的被画了；第二行证明 FOV 输入能改变画面。
没有这两行，"矩阵里 36 格都长一样"就无法区分是输入无效还是场景本身静止。

## 4. 真实音频

设备：`winmm_sink_probe --list-devices` 实测 13 个设备，选 6 号 `xduoo audio`
（真实输出 DAC，非 Voicemeeter 虚拟设备）。运行命令与矩阵相同但**去掉
`--audio-disabled`**、`--capture-tick 300`（短位置，避免长时间占用声卡）。

| 读数 | `--audio-disabled`（null sink） | 真实设备 6 |
|---|---|---|
| `audio_mode` | （无该字段） | `device` |
| `audio_device_opened` | — | **19** |
| `soundEventsRead` | 5292 | 5292 |
| `soundEventsScheduled` | 5229 | 5229 |
| `soundEventsPlayed` | 56 | **19** |
| `soundEventsSkippedVoice` | 55 | 5 |
| `soundEventsMissingResource` | 313 | 56 |
| `unresolved` / `unplaceable` / `refused` | 63 / 0 / 0 | 63 / 0 / 0 |
| 窗口 | hidden=1 | hidden=1（无弹窗） |

`audio_device_opened=19 == events_played=19`：每一次播放都对应一次成功打开设备，
没有打开失败被静默吞掉。读取与调度总量两种模式一致（5292/5229），
缺资源 313 → 56 的差异来自 null sink 不下载资源而真实设备要下载。

## 5. 自动化验收

```text
bash verify-fast.sh  ->  assertions_ok=43 assertions_bad=0
                         VERIFY-FAST=PASS (rc=0)
```

抓帧一律带 `--hidden-window`（矩阵另加 `--audio-disabled`）。
真实音频这一节**不是**自动门禁结果，`audio_device_opened` 只是设备打开计数；
本报告的后续人工确认由用户补充，不由该计数推导。

## 6. 未验证项（不写成"完成"）

1. **声音内容已由用户通过真实设备确认**；机器证据仍只记录设备打开和播放计数。
   `kinds` 分布（w2953/f5/u14/o1620/v633/a4/m0）说明事件类型存在，不能替代
   用户对具体音效的听感确认。
2. **武器本体维度未测到**：on/off 两帧逐字节相同（无 ViewModel 实体被选中），
   武器本体是否影响画面在本 demo 上未获得证据。
3. **tick 维度部分未区分**：2000 与 4000 相同，只有 6000 分离。
4. **动画/材质正确性未验**：任务书原话——"像素不同"只能证明输入影响了画面，
   不能单独证明动作或材质正确。本报告所有百分比都只在这个意义上使用。
5. **人工画面未看**：没有任何人眼看过的帧；本报告是机器比对。

## 7. 复现与工作树状态

```text
evidence/manual-acceptance/matrix.sh        第一轮矩阵（36 格，发现两维度不可区分后中止）
evidence/manual-acceptance/matrix2.sh       缩减矩阵（8 格，本报告数据来源）
evidence/manual-acceptance/regions.py       VM/世界区域差异计算
evidence/manual-acceptance/matrix.csv       第一轮 4 格原始读数
evidence/manual-acceptance/matrix2.csv      本轮 8 格完整读数
evidence/manual-acceptance/regions.csv      区域差异表
evidence/manual-acceptance/verify-fast.log  verify-fast 输出
evidence/manual-acceptance/audio-real.trace 真实设备运行读数
evidence/manual-acceptance/*.bmp            抓帧（frozen witness）
```

`git status --short`：本任务只新增上述 evidence 文件与本报告，
**未修改任何产品源码、`verify-render.sh`、`verify-all.sh`**。

### 过程中的两个仪器问题（已修，无残留）

1. 第一轮矩阵 36 格全部 `rc=127`：脚本 `cd "$(dirname "$0")/.."` 只上溯一级到
   `evidence/`，相对路径的 exe 找不到。修为上溯两级并用绝对路径。
2. pilot 连续两次读不到 ViewModel 读数：`--viewmodel-render` 只切 companion/command
   模式，**打开 ViewModel 的开关是 `--viewmodel <model>`**（`viewModelRequested =
   !commandViewModel.empty()`）。已按门禁同样用法修正。
