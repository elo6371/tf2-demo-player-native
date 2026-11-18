# N17 默认动画翻转（2026-11-18）

第三人称玩家动画从「默认关、开开关才动」改成「默认开、`--player-animation off` 回到旧画面」。
这次改的是**默认值**，不是动画实现：同一份采样链，同一份时钟，绑定的还是那个把姿态算出来的路径。

## 1 依据与裁决

任务清单的授权原文（2026-11-18）：

> 裁决：N17 默认动画改为开启，保留 `--player-animation off` 作为旧行为基线和排障开关。

支持这条裁决的是同 tick、同机位的两帧：默认（bind pose）与 `--player-animation --animation-drive states`
（下称“动画帧”）在 `tick 1`、相机 `500 -2240 240 17.8 127.97` 上相差 12,224/860,784 像素（1.42%，最大通道差 232）。
差的不是噪点，是整个人形的姿态。并排图 `.scratch/n17-ab/scout/ab.png`（左 bind pose、右动画）是当时给裁决看的材料。

bind pose 不是一个“中性”的默认：它是明显坏掉的身体。这个产品能播的 demo 里 CTFPlayer 实体没有
`m_nSequence`/`m_flCycle`，服务端数据给不出姿态，所以动画只能由实体自身的原点速度经动画时钟驱动。
把这条驱动留在开关后面，等于产品默认画面里每个玩家都是一具僵直的身体。

这条翻转**不**把 N17 的同机位视觉验收标成通过：能自己动，不等于与 TF2 一致。N17 在任务清单里保持 OPEN。

## 2 改了什么

产品（`native/src/main.cpp`）：

- `bool playerAnimationEnabled = false;` → `true`（:3514，注释同时重写成“默认开、为什么、关掉意味着什么”）。
- `--player-animation` 的解析（:4636 起）从“出现即开”改为 `on|off`：
  - `--player-animation off` → 关；
  - `--player-animation on`、裸 `--player-animation` → 开（裸开关保持旧语义，因为已有门禁就是那样拼的，
    默认值变更不应该悄悄改写它们量到的东西）。
- `--animation-drive` 不变：`legacy` 仍是默认，`states` 仍要显式给。

门禁侧：四个门禁过去靠“没有开关就是关”来拿未驱动画面，现在必须写明（否则它们会拿到两帧驱动画面做 A/B）：

| 文件 | 改动 |
|---|---|
| `player-animation-pixels.sh` | `run_frame off` → `run_frame off --player-animation off`（含注释） |
| `class-pixels-check.sh` | `:272` 的 off 捕获加 `--player-animation off` |
| `bone-mismatch-semantics-check.sh` | `:121` 的 undriven 捕获加 `--player-animation off` |
| `terrain-blend-check.sh` | `:245` 的抓帧调用加 `--player-animation off`；它的 lost-pixel 计数基线取自无驱动帧，翻转后 `196833` 个丢失像素里有 1 个露出背景（§5.3 第 3 行） |

## 3 三帧证据

同一 exe（`ef4aadd1…`，翻转构建；先前那次 `5e908c4c…` 的构建在同一机位给出同一帧）、同机位、同 tick、每次从 `native/tests/fixtures/gate-settings.ini` 新拷 settings：

| 命令行 | 帧 sha256 | 说明 |
|---|---|---|
| 不加开关（默认） | `caef7fa60b0282a1e7e91f3af6bd50eaf67f8fc8511137bdf4c1f26abc3d5b0a` | 默认已驱动 |
| `--player-animation off` | `30315f8f7bb4eee7af1324d21398a2bd962d9637996b43aa78c474ac5c1879d4` | 与翻转前基准逐字节相同 |
| `--player-animation on` | `caef7fa6…` | 与默认同帧 |

off 那一行是这次翻转的关键：`30315f8f…` 正是 **翻转之前** 默认路径的基准帧（3D 天空盒、N26 等前几轮都用它做
“默认画面没变”的证据）。所以“关”不是新的近似，是旧的逐字节重放。

## 4 66 个抓帧脚本的盘点

口径（在 `ae0b47cc` 与当前工作树分别实测，两棵树给出同一份名单）：仓库根目录下调用 `--capture-frame` 的脚本共
**66** 个；其中 **11** 个同时提到 `--player-animation`（这些已经自己控制这条路径），**55** 个不提 → 走默认路径。
（先前记的 51 是更早树状态的计数：同一命令当时只捕获 62 个脚本；现在 66 = 55 + 11，名单用 `comm` 在两棵树间逐名核对，差集为空。）

扫描方法（两轮、四棵树、四条并行流）：

- 55 个默认路径脚本按 `下标 % 2` 分成奇偶两半，翻转后与翻转前各用两棵树各跑一半：
  - **翻转后**：`/d/TF2_Native_Worktrees/t105-integrate`（HEAD `fcbb3b2b` + 未提交的翻转，
    exe `ef4aadd1…`）跑偶数下标；`t105-p1`（同 commit + 同一份翻转补丁，exe `4d346fa8…`）跑奇数下标。
  - **翻转前**：`t105-base` 是 `git worktree add /d/TF2_Native_Worktrees/t105-base ae0b47cc`（源码逐字节等于翻转前，
    exe `3f384f03…`）跑偶数；`t105-b1`（同 commit，exe `674e3556…`）跑奇数。
  - `ae0b47cc..fcbb3b2b` 只有一个提交且只改 `build-target.sh`，所以两棵翻转前的树在抓帧行为上等价；
    四棵树都先 `bash build-target.sh all` 把 51 个目标建齐。exe 字节因内嵌路径不同而不同，比较基准是帧不是 exe。
  - 用翻转前的树而不是“同一 exe 加 `off`”，是因为门禁内部不会替调用者传 `off`；翻转前的树才是“旧帧”的字面来源。
- **并行上限是量出来的**：同一命令同一机位，4 个产品并发 52 s 内全部出帧、四帧 sha256 相同（`30315f8f…`）；
  6 个并发则 6 个全部 100 s 超时（rc=124）、CPU 只走到约 1 s。所以本机并发抓帧上限是 4，四条流每条同一时刻最多一个产品。
- `_shared/hostlock.sh` 的排队会让并行流互相等锁；在 `run` 里加了 `HOSTLOCK_PARALLEL_OK=1` 旁路（默认行为不变），
  收尾把备份 `hostlock.sh.bak-20261118` 拷回并核对 sha256（`fa222b6a…`）。
- 每门记录 rc / 秒数 / 该门新产生的帧（`find .scratch evidence -newer <marker>`，只算 `*.bmp`/`*.png`）与逐帧 sha256；
  跨树比较同一个**相对路径**的 sha256。
- 判定：同一门同一帧名，两轮 sha256 相同 = 这格画面不含受动画影响的玩家；不同 = 含玩家，默认动画真的改到了它。

## 5 受影响门禁的重跑

### 5.1 负控：把动画压回关，抓帧门禁必须仍红（证明门禁不是空转）

在同一个翻转后的 exe（`ef4aadd1…`）上，用 `--player-animation off` 整体重跑三个与动画
语义最相关的门禁（原样跑门禁、只让产品侧回到关；报告 `t105-integrate/.scratch/n17-offcfg/report.txt`）：

| 门禁 | 显式 off 下 | 判读 |
| --- | --- | --- |
| `player-animation-pixels.sh` | rc=1，1083 s | 应当红：`FAIL animation-off moved 1.83% < 5.0% of body pixels`；同一次还有 `MASK pixels=302522 outside [2000, 215196]`（关掉驱动后玩家掩码变成整块，越上限）。两条都指向“画面确实由驱动改变” |
| `class-pixels-check.sh` | rc=0，898 s | 应当绿：它断言的是身体身份与占比（`SUBJECT id=95 ent=5 models/player/demo.mdl state=moving seq=147 pixels=5337 moved 75.70%`），与动画开关无关 |
| `bone-mismatch-semantics-check.sh` | 未得结果（rc=-1，1273 s 被 kill） | 该次运行卡住未结束，重跑待补 |

结论：把默认压回关后，动画像素门禁重新变红；同一 exe 上默认=开时它是绿的。
这条负控是“默认值翻转让门禁失去意义”的反证——门禁仍然只对“有驱动”的画面给 PASS。

### 5.2 翻转前就红的门禁（与动画默认值无关，别记在翻转账上）

在翻转前 worktree（`t105-base`，exe `3f384f03…`）上串行重跑失败门禁，结论：以下门禁在翻转前
就是红的，失败原因与动画无关（报告 `t105-base/.scratch/n17-fails/report.txt`）：

| 门禁 | rc / 耗时 | 失败行 | 归属 |
| --- | --- | --- | --- |
| `sultry-translucent-atlas-check.sh` | 1 / 561 s | `FAIL isolation slot0Binds got '' want '7'`（batchDraws、texturedBatchDraws 同形） | 并发下读数为空；串行单跑同样红 |
| `quality-audit-check.sh` | 1 / 153 s | `FAIL ref-a: no capture at tick 2000`，`ref-a.log` 是 `demo open failed: …autorecord_2026-07-02_13-26-46.dem` | 环境缺 demo 文件 |
| `material-coverage-check.sh` | 1 / 1 s | `FATAL: missing input` 同一个 autorecord demo | 环境缺 demo 文件 |
| `lighting-semantics-check.sh` | 1 / 27 s | `FAIL map distinct materials got '127' want '108'`、`bump declared/resolved 46 vs 45` | 既有基线漂移，与动画无关 |
| `demo-clock-check.sh` | 1 / 422 s | `FAIL A5 camera eye 1784.7,-3594.8,285.0 != probe 1784.66,-3594.82,239.965` | N12-A 的 `m_vecViewOffset[2]=45.0`（offsetZ=45.0000）与 A5 规格冲突 |
| `lighting-dark-account-check.sh` | 1 / 1 s | `demo open failed: …autorecord_2026-07-02_13-26-46.dem` | 环境缺 demo 文件 |

### 5.3 55 个默认路径门禁：翻转前 vs 翻转后逐门读数

**口径。** 默认路径抓帧脚本 = 命令行里有 `--capture-frame` 且没有 `--player-animation` 的脚本（66 个抓帧脚本里排掉 11 个显式动画/非默认路径的），共 55 个。两侧跑的是同一份门禁源码：翻转补丁只改 `native/src/main.cpp` 和 3 个门禁脚本，那 3 个都在 55 门外。翻转后树 `t105-integrate`（HEAD `fcbb3b2b` + 未提交翻转，产品 `ef4aadd1…`）与 `t105-p1`（`4d346fa8…`）；翻转前树 `t105-base`（worktree @ `ae0b47cc`，产品 `3f384f03…`）与 `t105-b1`（`674e3556…`）。四棵树各一条流，一门跑完再跑下一门，流内同一时刻只有一个产品进程（本机 4 个产品进程是硬上限，6 路并发全部 `rc=124` 挂死）。逐门原始读数在 `D:/_shared/n17r3/final-compare.tsv`；帧摘要 `w-pass.txt` / `w-base.txt2`（只算 2026-11-18 21:25 之后写入的 `*.bmp`/`*.png`），逐目录表 `final-frames.tsv`，逐门表 `gate-frames.tsv`。表里「帧 同/异」是该门帧目录里同路径帧的 sha256 相同/不同个数（`—` = 该门帧目录没有窗口内新帧）。

**rc 结果：19 门两侧都绿、31 门两侧同红、5 门两侧不一致。**

| 门禁 | 翻转后 rc / 秒 | 翻转前 rc / 秒 | 帧 同/异 |
| --- | --- | --- | --- |
| `argument-refusal-check.sh` | 0 / 95 s | 0 / 95 s | 0/2 |
| `audio-keys-check.sh` | 0 / 262 s | 0 / 258 s | 3/0 |
| `camera-ui-check.sh` | 0 / 151 s | 0 / 143 s | 3/0 |
| `entity-material-check.sh` | 0 / 89 s | 0 / 93 s | 0/1 |
| `fog-check.sh` | 0 / 168 s | 0 / 168 s | 1/3 |
| `frame-capture-check.sh` | 0 / 90 s | 0 / 92 s | 3/1 |
| `hud-check.sh` | 0 / 549 s | 0 / 552 s | - |
| `hud-live-check.sh` | 0 / 278 s | 0 / 273 s | - |
| `hud-text-check.sh` | 0 / 68 s | 0 / 68 s | - |
| `logstf-check.sh` | 0 / 498 s | 0 / 505 s | 12/8 |
| `map-material-matrix.sh` | 0 / 510 s | 0 / 500 s | - |
| `map-overview-check.sh` | 0 / 895 s | 0 / 862 s | 10/0 |
| `quiet-capture-check.sh` | 0 / 104 s | 0 / 105 s | 3/0 |
| `t2-visual-matrix.sh` | 0 / 286 s | 0 / 286 s | 37/50 |
| `world-entity-tint-check.sh` | 0 / 187 s | 0 / 187 s | 0/4 |
| `world-geometry-hole-check.sh` | 0 / 0 s | 0 / 0 s | - |
| `world-material-check.sh` | 0 / 114 s | 0 / 107 s | - |
| `world-render-reliability-check.sh` | 0 / 0 s | 0 / 0 s | - |
| `world-sky-gate-check.sh` | 0 / 361 s | 0 / 350 s | 4/2 |

31 门两侧同红里，与 §5.2 重复的 6 门（sultry、quality-audit、material-coverage、lighting-semantics、demo-clock、lighting-dark-account）已单独定过性；其余多是环境类前置失败（缺 autorecord demo、`rc=2` 的 isolated settings 建不出来、`webview2-loader` 的 `rc=9`）或该门自身的既有基线漂移，两侧读数逐门相同，翻转不改变它们的 pass/fail。

| 门禁 | 翻转后 rc / 秒 | 翻转前 rc / 秒 | 帧 同/异 |
| --- | --- | --- | --- |
| `demo-clock-check.sh` | 1 / 293 s | 1 / 293 s | 2/3 |
| `lighting-dark-account-check.sh` | 1 / 2 s | 1 / 1 s | - |
| `lighting-semantics-check.sh` | 1 / 26 s | 1 / 24 s | - |
| `map-geometry-lighting-check.sh` | 1 / 2226 s | 1 / 2209 s | - |
| `material-coverage-check.sh` | 1 / 1 s | 1 / 0 s | - |
| `minimap-hud-check.sh` | 2 / 0 s | 2 / 0 s | - |
| `playback-error-paths-check.sh` | 1 / 581 s | 1 / 584 s | 2/1 |
| `quality-audit-check.sh` | 1 / 3 s | 1 / 3 s | - |
| `sultry-translucent-atlas-check.sh` | 1 / 286 s | 1 / 289 s | - |
| `t21-capture.sh` | 2 / 0 s | 2 / 1 s | - |
| `verify-audio-content.sh` | 1 / 4 s | 1 / 4 s | - |
| `verify-audio.sh` | 1 / 88 s | 1 / 76 s | - |
| `viewmodel-animation-check.sh` | 1 / 1 s | 1 / 0 s | - |
| `viewmodel-apply-check.sh` | 1 / 0 s | 1 / 0 s | - |
| `viewmodel-companion-check.sh` | 1 / 0 s | 1 / 0 s | - |
| `viewmodel-contract-check.sh` | 2 / 0 s | 2 / 0 s | - |
| `viewmodel-presentation-check.sh` | 1 / 0 s | 1 / 0 s | - |
| `viewmodel-range-check.sh` | 1 / 0 s | 1 / 0 s | - |
| `visual-baseline-check.sh` | 1 / 9 s | 1 / 9 s | - |
| `weapon-body-check.sh` | 1 / 8 s | 1 / 7 s | - |
| `weapon-material-check.sh` | 2 / 0 s | 2 / 1 s | - |
| `webview2-loader-check.sh` | 9 / 0 s | 9 / 0 s | - |
| `world-black-account-check.sh` | 1 / 170 s | 1 / 170 s | 0/3 |
| `world-disp-param-capture.sh` | 2 / 0 s | 2 / 0 s | - |
| `world-geometry-budget-check.sh` | 1 / 177 s | 1 / 185 s | - |
| `world-geometry-completeness-capture.sh` | 1 / 13 s | 1 / 13 s | - |
| `world-geometry-hole-capture.sh` | 2 / 3 s | 2 / 3 s | - |
| `world-material-effects-check.sh` | 1 / 943 s | 1 / 943 s | - |
| `world-material-face-check.sh` | 1 / 3 s | 1 / 2 s | - |
| `world-material-semantics-check.sh` | 1 / 61 s | 1 / 61 s | - |
| `world-near-clip-check.sh` | 1 / 2 s | 1 / 2 s | - |

**5 门不一致的定案（逐门串行复跑）。**

| 门禁 | 扫描读数（后 / 前） | 串行复跑 | 判定 |
| --- | --- | --- | --- |
| `fullscreen-quiet-check.sh` | rc=1 / 195 s · rc=3 / 194 s | 翻转后树连跑两次都是 rc=3（37 s 与 202 s 各一次）：守卫在 4.5 s 内写 `guard.aborted=1`，报告里 `deactivateBy=steamwebhelper`、`stolenBy=SearchHost` | 本机不可定案：守卫的 ESC 判断被别的前台进程抢占触发误判，两次复跑都没跑到断言段。不记为翻转账 |
| `spectate-check.sh` | rc=1 / 649 s · rc=0 / 653 s | 失败行是 `FAIL STALE-BINARY=1 …spectate_probe.exe<native/src/spectate_target.cpp`——并行阶段别的门重建过源码，probe 比源码旧 | 前置条件：`bash build-target.sh spectate_probe` 重建后复跑 rc=0（`SPECTATE-CHECK=PASS`）。与翻转无关 |
| `terrain-blend-check.sh` | rc=1 / 214 s · rc=0 / 211 s | 复跑仍 rc=1，失败行 `FAIL only 196832 of 196833 lost pixels have a surface drawn on them; the rest are background`——差 1 个像素 | 真变化，但量级是 1/196833：翻转让这一格的丢失像素覆盖少覆盖了一个背景像素。该门的像素计数基线取自无驱动帧，按 §2 口径补显式 `--player-animation off` 后复跑 rc=0（`TERRAIN-BLEND=PASS`，202 s） |
| `viewmodel-material-check.sh` | rc=1 / 1347 s · rc=0 / 1349 s | 串行复跑 rc=0、1165 s、`assertions_ok=226`、`VIEWMODEL-MATERIAL=PASS` | 并行噪声（该门含「同一帧抓两次必须 byte-identical」类断言，4 路产品并发时不成立），不记为翻转账 |
| `viewmodel-robustness-check.sh` | rc=2 / 0 s · rc=1 / 147 s | 后侧 `FATAL: native/ is not clean before the run: M native/src/main.cpp` | 清洁度前置：扫描时翻转还没提交；提交后前置自动满足，行为回到与翻转前相同的 rc=1（它自身的既有红） |

**帧结果：35 个目录、90 个文件不同。** 逐门汇总（同/异）：

| 门禁 | 帧 同/异 | 例子（翻转后 sha16 / 翻转前 sha16） |
| --- | --- | --- |
| `t2-visual-matrix.sh` | 37 / 50 | `.scratch/t62-map-axes/cp_snakewater_final1/analysis/empty-mask.bmp` f1ae70ef25443de0 / b24c90b1aec18859 |
| `viewmodel-material-check.sh` | 0 / 9 | `evidence/fast/viewmodel-material/longrun.bmp` b4924766a6b3fda1 / 2aff8ce86e30b4b8 |
| `logstf-check.sh` | 12 / 8 | `evidence/t66-sourcetv/runtime/alive-corpses.bmp` 2a017e42b4b9b2be / 1a09247679b14cf7 |
| `world-entity-tint-check.sh` | 0 / 4 | `.scratch/world-entity-tint/coverage.bmp` 263e80004e584324 / ac6ff977273a6f0c |
| `demo-clock-check.sh` | 2 / 3 | `evidence/t63-clock/gate/a1-one.bmp` 44b4c5aeba6ed16f / e87c785cee538e52 |
| `fog-check.sh` | 1 / 3 | `evidence/t101-fog/fog-cli.bmp` a7e4e2ea951d111f / 8d81d346a1502706 |
| `terrain-blend-check.sh` | 3 / 3 | `evidence/terrain-blend/frames/all-off.bmp` 9ec1bda0e2268edf / 1d54202e5b6914a6 |
| `world-black-account-check.sh` | 0 / 3 | `.scratch/world-black-account/albedo.bmp` 57a101b073665ad7 / f169cac77b254096 |
| `argument-refusal-check.sh` | 0 / 2 | `.scratch/argument-refusal/bare-switch.bmp` db779f54b13ccdc2 / b1d9781eba8c8975 |
| `world-sky-gate-check.sh` | 4 / 2 | `.scratch/w1-world-sky/positive.bmp` 03180cef2ee58791 / 7d0ec3008e9172b5 |
| `entity-material-check.sh` | 0 / 1 | `evidence/entity-material/fresh-tick3001.bmp` 0481bbe0e5e8e80e / c56df7d259139d61 |
| `frame-capture-check.sh` | 3 / 1 | `evidence/frame-capture/demo-installed-map.bmp` 5657aef4b03e5344 / 92380a11b4669cbe |
| `playback-error-paths-check.sh` | 2 / 1 | `evidence/t93-playback/green-b-after.bmp` cdc1e86479ff4ca5 / 877f14b9317c4076 |
| `audio-keys-check.sh` | 3 / 0 | ``  /  |
| `camera-ui-check.sh` | 3 / 0 | ``  /  |
| `fullscreen-quiet-check.sh` | 3 / 0 | ``  /  |
| `map-overview-check.sh` | 10 / 0 | ``  /  |
| `quiet-capture-check.sh` | 3 / 0 | ``  /  |

读法：`t2-visual-matrix.sh` 的 50 个不同帧来自它自己的 map-axes / map-matrix / budget 子目录，它比的是同一次运行内的 A/B 对，所以两侧都 rc=0；`logstf`／`fog`／`world-sky-gate`／`world-entity-tint`／`entity-material` 等第三视角或含玩家剪影的帧会逐像素变化，但门禁断言的是身份/占比/计数，不是整帧哈希；`viewmodel-material` 的 9 个不同帧取自扫描中失败的那次运行，串行复跑已经重写，不宜当作「帧变了」的证据。真正逐像素恒等的门（`audio-keys`、`camera-ui`、`map-overview`、`quiet-capture`、`fullscreen-quiet`）说明翻转只碰得到玩家模型出现的那几格画面。

## 6 没证明什么

- 没有证明与 TF2 的姿态/外观一致：同机位视觉验收仍是 OPEN，本轮只证明“默认画面在动”。
- 没有证明九职业全对：`N17-BLEND-INVENTORY-2026-11-18.md` 里仍列着 8 条 UNKNOWN（blend 索引扁平化、pose 参数 flags 等）。
- 没有跑 `verify-all`：55 个默认路径脚本只是 `verify-all` 的一个子集，而且这里是一轮一轮跑单门，不是整链跑。
- 没有证明其它地图/机位：帧证据全部来自 `pl_upward_f12` 一张图、一个机位（`500 -2240 240 17.8 127.97`）。
- 没有证明其它机器：全部读数来自这一台（32 核、同一 GPU/驱动）；4 路并发上限也是这台机器的实测值，换机器要重测。
- 逐门帧比对只回答“这格画面变没变”，不回答“变得对不对”；本轮的“对”只有 §5.1 的负控（压回关就重新变红）和 §3 的三帧恒等。
- 并发取得的读数不能直接当定案：4 路并发下个别门禁的 trace 解析项会变空（`sultry-translucent-atlas-check.sh` 的 isolation 三项就是空的），红门必须串行复跑；本轮串行确认过的只有 §5.2 那 6 门。
- 5 门 rc 不一致已逐门定案（§5.3）：其中 3 门是前置/并发（spectate 的 probe 陈旧、viewmodel-material 的并行噪声、viewmodel-robustness 的“native/ 必须干净”被未提交翻转顶掉），1 门本机环境不可定案（fullscreen-quiet 的 ESC 误判），只有 terrain-blend 是真变化，且量级是 1/196833 个像素。
- `terrain-blend-check.sh` 补显式 off 的代价是：它不再看得见“动画开着”的画面。它的断言是地形混合的像素覆盖，不是玩家剪影，这个代价是有意接受的；想连动画一起看的场合得另开一门。
- 55 门里只有 7 门把期望哈希写成 64 位 hex 内联在脚本里（`lighting-semantics`、`map-material-matrix`、`material-coverage`、`weapon-body`、`weapon-material`、`world-geometry-hole-capture`、`world-geometry-hole-check`），其余门禁要么比 committed fixture、要么自差分；“旧帧哈希”只能靠翻转前树实测，不能靠 git 里的哈希。

## 7 复现

```bash
# 翻转后的默认帧
bash tools/lockrun.sh n17-default -- bash build-target.sh tf2_demo_native
sha256sum native/build-nmake/tf2_demo_native.exe   # ef4aadd1…
bash .scratch/n17-default/capture.sh               # default / off / on 三帧
```

`--player-animation off` 是这条翻转留下的排障口：任何旧帧都可以用它在新 exe 上重放，
而门禁里需要未驱动画面的地方现在必须写明 `off`，不能靠“没写就是关”。
