# D75 — 世界相机 FOV 修法设计（方案，未实现）

- 日期：2026-11-16　实例3 / card-char　worktree：`D:/TF2_Native_Worktrees/t105-e5-n27a`（`work/card-char-n27a`，base `997e945e`）
- **状态：方案。没有改任何源码，没有重建主树 exe，没有跑任何实验。** 主树产品 exe `75dae10e60740cd2c3e780ef68c72eb100e6f4221f8c234227a907a99d0607ab` 未动。
- 缺陷定义（register 原文）：名叫 `viewModelFov` 的设置项同时决定世界相机 FOV，而同名 CLI 旗标碰不到它（`docs/DEFECT-REGISTER-ADDENDUM-D51-D56-2026-11-15.md:339`）。

## 1. 先补读数（lead 指定的三条）

### 1.1 `viewmodel-material-check.sh` 里那条比较的实际公式

产品调用行（`viewmodel-material-check.sh:600`）：

```sh
--viewmodel "$model" --viewmodel-fov 90 "$@" \
```

- 该闸门用 **CLI 把 ViewModel 请求的 FOV 钉在 90**；它不设、也读不到世界 FOV。世界投影在它跑出来的帧里由 **settings 文件里的 `viewModelFov` 键**决定：`settings_store.cpp:84-85` 读键 → `native_renderer.h:71` 的 `RenderSettings::viewModelFov` → `native_renderer_frame.cpp:179` 的世界投影矩阵。
- 该闸门的**判定公式不是像素/FOV**，而是九项材质计数器：从 trace 的 `gpu: viewmodel materialReadings` 行解析（`:626`），逐项 `assert_eq`，例如 `:688` `"no slot fell back" fallbackSlots 0`、`:692` `fallbackRanges 0`；`fallback` 只有在**注入故障**时才可达（`:82-83`），其控制是 `bmp_diff "the fallback frame is exactly the flat control frame"`（`:846`）的逐字节比较。
- 结论：它比较的是「哪些材质被画、绑定是否成立」，不是「相机投影多宽」。世界 FOV 变宽/变窄只会改像素，不会改这九项计数器 ⇒ **这个闸门对 D75 两个方向都不敏感**：它的绿不能证明世界 FOV 正确，它的红也不会由 D75 触发。拿它当 D75 的证据是误用。

### 1.2 `player-model-check.sh` 里 view world 的 FOV 来源

- 该脚本里 `[Ff][Oo][Vv]` 命中 **0**（本卡 grep）；它**不启动产品 exe**，跑的是探针：`player-model-check.sh:44` `PROBE=native/build-nmake/entity_model_probe.exe`，`:283`/`:286` 用 `$OVERLAY`（`entity_overlay_probe`）跑 `--demo/--ticks`。
- 这两个探针源码里也没有 `fov` 字段（`entity_overlay_probe.cpp`、`entity_model_probe.cpp` grep 均 0 命中）。
- 结论：该闸门的 view world 画面**完全不消费产品的 world FOV 状态**（既不吃 ini，也不吃 CLI）；它不能用来验证世界 FOV 旋钮，也不能拿它的绿当「世界 FOV 未回归」的证据。

### 1.3 demo 里的 `m_iFOV` 观测值

来源：`evidence/observer-focus/pov-e3.txt`，demo 为 `D:/SteamLibrary/steamapps/common/Team Fortress 2/tf/demos/autorecord_2026-07-02_13-26-46.dem`（`pov.json:1` 明确记录），tick=53976，entity=3（正是被跟拍的目标：`pov-row.txt:6` `mode=4 follows=1 targetIndex=3`）：

```
pov-e3.txt:45  DT_BasePlayer.m_flFOVTime  x=809.505005
pov-e3.txt:54  DT_BasePlayer.m_iDefaultFOV int=90
pov-e3.txt:55  DT_BasePlayer.m_iFOV        int=0
pov-e3.txt:56  DT_BasePlayer.m_iFOVStart   int=20
```

死亡检查点 `pov-e18-dead.txt:54-56`（entity=18）同形：`m_iDefaultFOV=90`、`m_iFOV=0`、`m_iFOVStart=90`。

字段存在性：`evidence/animation-availability/oracle-ctfplayer.txt:852-855` 列出的网络字段是 `m_iFOV`、`m_iFOVStart`、`m_iDefaultFOV`（UnsignedInt）与 `m_flFOVTime`（float）；**没有 `m_fFOV`**。所以「m_fFOV」不是可选源。

语义（Source 惯例 + 观测值自洽）：`m_iFOV=0` 是「无缩放」哨兵，实际视角 = `m_iFOV > 0 ? m_iFOV : m_iDefaultFOV`。观测到 0 表示这两个检查点都没有缩放处于激活态。

## 2. 世界 FOV 该从哪个源算

**取被观看的目标实体（spectate/observer target），不是观察者实体：**

```
worldFov = m_iFOV > 0 ? m_iFOV : (m_iDefaultFOV > 0 ? m_iDefaultFOV : 90)
```

- 为什么是目标不是观察者：`pov-row.txt:6` 里观察者 entity=18、被跟拍目标 entity=3；FOV 属于被观察的玩家（source 里 LocalPlayer 的 FOV 才是视角来源），观察者实体的 FOV 在跟拍时不是画面参数。
- 不乘 `weapon_fov` 缩放：本仓不存在该 convar/字段（`weapon_fov` grep 0 命中），世界投影在 Source 里也不乘它；仓内唯一的 FOV 夹取是 `clampViewModelFov`（40..120，`native_renderer_settings.cpp:54`），那是 ViewModel 自己的 knob。
- 缓存语义：`m_iFOV`/`m_iDefaultFOV` 只在**变化时**网络化，所以必须 per-entity 缓存并在目标切换（`targetSerial` 变）时重置；没有新值的 tick 沿用最后值，不能当成「回到 0/默认」。
- 回退：无 demo 值（探针/空白场景）时 90，与现在一致。

## 3. demo 兼容风险

1. **已有像素证据会移动**：凡是 demo 里 `m_iDefaultFOV != 90` 或缩放段被激活的帧，修法生效后世界投影会变，已 pin 的 BMP sha 会红——这是修复目的，不是回归，但必须先知道影响面。
2. **缩放段会真正开始动**：`m_flFOVTime`（`pov-e3.txt:45` = 809.505）说明 demo 里有缩放插值时间戳；修法后 sniper 镜等缩放段的世界 FOV 才第一次跟 demo 走。目前**没有**任何一帧证据覆盖缩放段（检查点里 `m_iFOV=0`）。
3. **旧 ini 语义冲突**：现有闸门/证据都只写 `viewModelFov` 键。如果新字段只读新键，旧 ini 会全部落回 90，A40/B120 这类证据静默失效。所以必须定义 legacy 规则（见 §4）。

影响面清单（下一步测量，不在本方案里跑）：用现有 observer-focus 探针对活跃 demo 逐 tick dump `m_iFOV`/`m_iDefaultFOV` 值域，回答「有没有非 90 的默认值」「缩放段占多少 tick」。

## 4. 修法：给世界投影独立字段（不再一个字段喂两个消费者）

| 文件 | 改动 |
|---|---|
| `native/include/native_renderer.h:71` | `RenderSettings` 增加 `float worldFov = -1.0f;`（-1 = auto/未设），并加 `Renderer::setWorldFov` 与 `worldFov()` 读数 |
| `native/src/native_renderer_settings.cpp:48-56` | `normalize()` 增加 `worldFov` 解析与夹取：NaN/越界 → -1；有效值夹到 40..120（与 ViewModel 同界，复用现成上下界） |
| `native/src/native_renderer_frame.cpp:179` | 世界投影从 `settings_.viewModelFov` 改为解析后的 `worldFov`（唯一消费点必须换掉） |
| `native/src/native_renderer_camera.cpp:184` | 相机读数 `reading.fovDegrees` 同步改成世界 FOV，否则 trace 会把 ViewModel 的值报成相机 FOV |
| `native/src/settings_store.cpp:84-85` / `:132` | 新键 `worldFov`（读+写）；**legacy**：文件只有 `viewModelFov` 时，把该值同时当世界 FOV（保证旧闸门/证据逐字节不变） |
| `native/src/main.cpp:3037-3038`、`:3867-3868` | 新 CLI `--world-fov <deg>` |
| `native/src/main.cpp` 的 spectate 块 `:11233-11236` 与 tick 更新区 `:11281-11355` | 每 tick 从目标实体取 `m_iFOV`/`m_iDefaultFOV`，按 §2 公式与缓存规则算出世界 FOV，喂 `renderer.setWorldFov` |
| `native/src/main.cpp:8170`、`:13484`、`:13605`、`:10923` | 读回/持久化/trace readout 增加 worldFov，保持「写什么读什么」 |
| 新增 trace 行 | `world: fov source=demo-m_iFOV|demo-m_iDefaultFOV|cli|ini|legacy|fallback value=90 entity=3 tick=53976`；**source 必打**，否则事后无法解释一帧为什么是这个 FOV |

**解析优先级**（同时满足旧证据不变与新能力可用）：

```
--world-fov（显式 CLI）
  > ini 显式 worldFov
  > ini 显式 viewModelFov（legacy，仅当 worldFov 键不存在）
  > demo 目标实体 m_iFOV/m_iDefaultFOV（auto）
  > 90
```

legacy 放在 demo 之前，是为了让 A40/B120 那类「只设 viewModelFov」的旧实验继续得到 40/120 而不是 demo 的 90。

不做：不改 `clampViewModelFov`；不改 `viewmodel.cpp:276`（`--viewmodel-fov` 继续只服务 ViewModel 请求）；不引入 convar 系统。

## 5. 验证方案（只写方案，本轮不跑）

**前提：先重拍基线。** D75 的 `C90/A40/B120` BMP 是 exe `e61c7fd9…`（3,025,408 B，2026-11-15 22:58）画的，当前产品 exe 是 `75dae10e…`（3,044,864 B，11-16 14:14）——不是同一构建，**不能拿旧 BMP 的 sha 当逐字节正控**。基线必须在隔离 worktree 的 base commit（`997e945e`）用同一构建重拍，记录三组 sha。

| 项 | 做法 | 期望 |
|---|---|---|
| 正控 1（不回归） | 修复后：demo `bb841c6d…`、tick 2000、ViewModel 关、ini 只设 `viewModelFov=90/40/120` | 与 base 重拍的三张 BMP **逐字节相同** |
| 正控 2（新通路） | `--world-fov 40/120` 覆盖 demo 的 90 | 与「ini 设 40/120」同像素（同 demo/tick/settings） |
| 正控 3（auto 读 demo） | 不给 `--world-fov`、ini 无任何 fov 键 | trace `source=demo-m_iDefaultFOV value=90`，像素与 base 的 ini=90 帧相同 |
| 负控 1（字段选择承重） | 注入版把 `m_iFOV` 当绝对值（0→夹到 40）或改用 `m_iFOVStart`(=20→40) | 必须与 base 90 帧**不同**（红），trace 显示 `value=40` |
| 负控 2（legacy 承重） | 把 ini 的 `viewModelFov` 键删掉、不给 CLI | 必须落回 demo 90，与 base 90 帧相同 |
| 回滚 | revert 单个 commit | 无新键时行为 = 现状 90 |

每步前后核对主树 exe sha 不变（`75dae10e…`）。验证器用现有 `--capture-frame` + `cmp`/sha256，不新造阈值。

## 6. 不声称

- **未实现、未构建、未运行**；§5 全部是方案。
- 未测量 demo 的 FOV 值域与缩放段占比（§3 的影响面清单未跑）。
- 未验证第三人称/自由视角下的世界 FOV 语义（本方案先用 auto=目标实体，再看是否要固定）。
- 未验证 `m_iFOVStart` 的插值语义：本方案只把它当观测字段，不参与公式；若后续要还原 Source 的缩放过渡，需要单独立项。
