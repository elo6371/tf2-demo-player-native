# N21(c) `--audio-pov-fire`：验收前提不成立（审计报告）

日期 2026-11-21 · worktree `D:/TF2_Native_Worktrees/t105-integrate`（work/integrate @ `98331eea`）
本轮**没有改任何产品源码**；只加了只读探针开关（见"证据与复现"）和这份文档。

## 结论

1. **验收录像不是客户端 POV 录制，是 SourceTV 转播录制。** `bb841c6d…_matcha-20260927-1347-cp_snakewater_final1.dem`
   的头/索引给出 `recording=SourceTV service_name=MatchaTV source_tv=1`；产品自己的分类器
   `classifyDemoRecording`（`native/src/demo_header_track.cpp:430-451`）对它返回
   `kind=SourceTv` / `label="SourceTV"`（`headerName` 或 `serverInfoHltv` 命中即返回，不走 POV 分支）。
2. **卡里的"POV owner 实体 1"在这个录像里是转播相机的观察者槽位，不是开火玩家。**
   tick 200000 的快照（`--snapshot 200000 --props 1`，`snapshot_resolved_tick=200000 stale=0`）：
   `m_lifeState=2`、`m_iHealth=1`、`m_iObserverMode=7`、`m_hActiveWeapon=2097151`（无效）、
   `m_hMyWeapons.000..` 全部 `2097151`、带 `DT_BCCLocalPlayerExclusive.m_flNextAttack`，
   675 个 prop 的 `lastWriteTick` 全是 10320（signon 全量）。产品源码对同一件事早有定论：
   `native/src/main.cpp:13112-13117`（T66）写明 *"the demo names ONE entity (svc_SetView -> lastViewEntity)
   and on this recording that entity is the camera's own spectator slot, dead at every tick -- so the observed
   player is a decision, not a reading"*；T81 的 `g_spectateCameraHiddenEntity`（`main.cpp:13198-13205`）
   是"转播相机站在谁的眼里"的运行时决定。
3. **全片 temp-entity 流里没有一条属于实体 1 的开火事件。** 本 demo `temp_events=26921`，产品把前 16,384 条
   存在 `summary.tempEntityEvents`（`native/src/demo_header_events.cpp:310`）。把探针上限临时放开后做全量 dump：
   `CTEFireBullets` 共 **1,418** 条，`m_iPlayer` 只出现 **2 / 6 / 9 / 14**，**没有 player 1**；
   `CTEPlayerAnimEvent` 的 player 是 EHANDLE（entry = value % 2048），出现 entry 2..17，**同样没有 entry 1**。
   ⇒ "给 POV 实体 1 合成 fire/slash" 在这个录像上**没有触发数据**。
4. **声音侧不缺。** 声音消息 `sounds=15942`、解码 `sound_decoded=15935`（99.96%）；产品时间线 trace
   `audio: timeline ... read=15935 scheduled=15746 unresolved=189 unplaceable=0 refused=0
   kinds=w8791/f9/u68/o5007/v1859/a12/m0`。weapon 类开火音名齐全且是**真实玩家**的开火：
   `stickybomblauncher_det` 1286、`scatter_gun_shoot` 899、`quake_rpg_fire_remastered` 839、
   `stickybomblauncher_shoot` 637、`pistol_shoot` 405、`rocket_shoot` 266…
   卡里"服务器从不发 owner 自己的开火音"**只在客户端 POV 录制上成立**（客户端自己预测自己的枪声），
   对 SourceTV 转播不成立——转播相机收到的是全量声音。
5. **动画事件通道审计（卡片的 gate）：不够用。** `native/include/animation_decoder.h` 没有 AE/事件通道；
   能播的 demo 不带每玩家 `m_nSequence`/`m_flCycle`（`docs/REF-ADOPTION-2026-11-20.md:76-78`、
   `native/src/main.cpp:3326-3328`）；上一轮审计量的 viewmodel 序列重启在同类录像上几乎不动
   （recorder arms 实体 685，2,293 ticks 里只有 5 次序列变化）。所以**不需要扩面到动画事件通道**，
   它解决不了触发问题；真正的逐发信号是 `CTEFireBullets`。

## 验收项对账

| 卡上验收项 | 结果 | 依据 |
|---|---|---|
| matcha 上实体 1 每段检测到多少攻击序列起点 | **每段 0**（9 个 8,000-tick 段全 0） | 全量 temp dump：`player 1` 0 条；实体 1 是相机槽位 |
| 多少变成排程声音 | **0**（未实现 flag；即使实现也无触发数据） | 同上 |
| on/off summary diff | 无 flag ⇒ 无 diff；产品源码零改动 | `git status --porcelain native/ src/` 只有探针文件 |
| 默认路径 bmp md5 | **`bdeca8c24ec4fa22113c6b55d7419430`**（HEAD 默认，tick 3000）
`--world-show-own-body` 时 **`9681f12c9e672ae896105f7756b3bf5f`** | `phasec-capture.sh tan29-default` / `tan29-ownbody`，rc=0 |
| `--audio-device 6` 可听检查 | 未做——没有新增声音可听 | 结论 3/4 |

> 备注：卡上的 `9681f12c` 是 task-9 隐藏自身之前的基线，本 HEAD 上要靠 `--world-show-own-body` 才复现；
> HEAD 默认是 `bdeca8c2`（与 task-31 报告一致）。两次实测都列在上表。

## 每段读数（`CTEFireBullets`，段 = tick/8000）

| 段 | player 2 | player 6 | player 9 | player 14 | **player 1** |
|---|---|---|---|---|---|
| 1 | 26 | 14 | 12 | 31 | 0 |
| 2 | 24 | 19 | 30 | 75 | 0 |
| 3 | 31 | 96 | 28 | 55 | 0 |
| 4 | 28 | 25 | 44 | 64 | 0 |
| 5 | 38 | 160 | 15 | 66 | 0 |
| 6 | 20 | 29 | 22 | 35 | 0 |
| 7 | 37 | 11 | 23 | 80 | 0 |
| 8 | 32 | 29 | 23 | 70 | 0 |
| 9 | 1 | 90 | 9 | 26 | 0 |
| 合计 | 237 | 473 | 206 | 502 | **0** |

实体 1 在 `svc_Sounds` 里只有 17 条、全是 world 类（impact/ammo_pickup/wpn_denyselect/
`stickybomblauncher_det` 等），没有一条 fire/slash —— 它本来就不开枪。

## 若真要做这个特性，缺什么

1. **一份客户端 POV 录像**（自己录自己的那种）。当前磁盘上 4 个大 demo 全是 SourceTV
   （`4a9bfb92…koth_bagel_rc13`、`a9630c65…pl_upward_f12`、`bb841c6d…cp_snakewater_final1`、
   `dfbce608…koth_bagel_rc11`，header 全部 `recording=SourceTV`）；唯一的 POV 是 8.7 KB 合成 fixture
   `D:/TF2_Demo_Player/testdata/pov_test.dem`（`commands=1`、`index_error=unknown demo command`），不能当验收样本。
2. **逐 tick 的 POV 玩家**。SourceTV 里"在看谁"是 director 的运行时决定（T66），加载期只有 checkpoint
   （`EntityHistoryLimits{maxCheckpoints=8, checkpointStride=128, archiveMax=96}`，
   `native/include/demo_header.h:318-323`）。要在音频构建期拿到就得新增 opt-in 逐 tick 保留；
   仓库里 T53 `ProjectileTrackLimits`（`demo_header.h:351-404`、`:931-942`、查询 `:1212-1227`）是同类先例。
3. **weaponId → 开火音名映射**。`CTEFireBullets` 的 `weaponId` 是 TF_WeaponID 枚举
   （本片出现 16/18/42/12/41/17），产品里没有这张表；参考实现是按**武器模型名**关键字查的。
   另外实测发现"开火音来源实体"和 `m_iPlayer` 不是一一对应（player 14 有 502 发，但以实体 14 为源的
   shoot/fire 音 0 条；以玩家为源的计数只覆盖一部分），所以还要 weapon→owner 映射才能做去重。
4. **去重**。SourceTV 下真实玩家的开火音已经在流里，合成必须按"实体 + 时间窗"去重，否则双响。

## 对读记录

- 已对读**仓库内 vendored 参考实现** `.scratch/ref-tf2da/src/player/view.rs`：
  `USUAL_FIRE_SOUNDS` :127-163、`is_fire_sound` :504、`learned_fire_sounds` :512、
  `usual_fire_sound(model, melee)` :536、`own_shot_sounds` :549-575（触发 = viewmodel sequence_start 变化 +
  activity 含 PRIMARYATTACK/SWING；origin=eye；OWN_SHOT_LEVEL=94）。
- 已对读本仓库产品代码：`classifyDemoRecording`（`demo_header_track.cpp:430-451`）、T66 relay 注释
  （`main.cpp:13112-13117`）、T81 `g_spectateCameraHiddenEntity`（`main.cpp:13198-13205`）、
  `addFireBulletsEvent`（`native/include/effects_timeline.h:58`）、
  `TempEffectEvent{entityIndex,weaponId,origin,...}`（`effects_timeline.h:19-40`）。
- **未对读外部源码**：没有访问 source-sdk-2013 / Valve 仓库；对读只覆盖仓库内 vendored 副本与本仓库产品代码。

## 证据与复现

证据目录 `.scratch/n21c/pov-fire/`：

- `tempsfull.out`（26,922 行 = 1 summary + 26,921 全量 temp 行）、`tempsfull.err`（空）、
  `temps.out/temps.err`（16,384 上限下的旧 dump）、`own1.out/own1.err`（实体 1 snapshot + props + userinfo）、
  `corr.py`（开火音/开火事件相关性脚本）。
- 复现命令（探针本轮新增只读开关 `--temps <n>`，0 = 全部；已随本文档提交）：
  ```
  bash tools/lockrun.sh lead -- bash build-target.sh demo_open_probe
  ./native/build-nmake/demo_open_probe.exe --scan --temps 0 <dem> > tempsfull.out
  ./native/build-nmake/demo_open_probe.exe --scan --snapshot 200000 --props 1 --userinfo <dem> > own1.out
  bash .scratch/n17-review/phasec/phasec-capture.sh tan29-default      # -> bdeca8c2
  bash .scratch/n17-review/phasec/phasec-capture.sh tan29-ownbody "" --world-show-own-body  # -> 9681f12c
  ```
- 探针的 16,384 存储上限是产品行为（`demo_header_events.cpp:310`）；全量 dump 时临时放宽过、**已还原**，
  `git status` 里没有该文件。

## 建议

`--audio-pov-fire` 按卡片原样实施会在验收录像上产出 **0 条**声音（实体 1 是相机，永远不开枪），
做出来也是死代码。建议二选一：

- **A（推荐）**：把 task-29 判为"前提不成立"关闭；保留本报告的读数作为 N21(c) 的结论。
- **B**：改卡为"客户端 POV 录像的自开火声合成"，先补一份 POV 录像样本，再按上面第 1-4 条开工
  （需要新增 opt-in 逐 tick 保留 + weapon→owner 映射 + 去重，不是小改动）。
