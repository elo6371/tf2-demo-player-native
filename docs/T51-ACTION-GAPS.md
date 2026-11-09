# T51 动作缺口：walk / airwalk / reload（当前动作表口径）

基线 `dea34d1`（分支 task/t99-identity）。上位文档：`docs/T85-ANIMATION-ACTION-TABLE.md`（动作表）、`docs/T85-ANIMATION-RECON.md`（侦察与证据分级）。
本文只谈三个具体缺口，并把「协议保证」「本轮实测」「假设」三样东西分开写。行号是 t99 的。

## 0. 结论

- **walk（地面移动）**：产品能播，但驱动它的**不是 wire 的速度**，而是产品自己对 `m_vecOrigin` 求差（3D，含 z）。协议不保证每 tick 的位移/速度；跳跃与下落因此和跑动落在同一个 `moving` 态。证据：C + 本轮 B（trace 样本见 §1.3）。
- **airwalk（空中移动）**：产品**没有 air 状态**，也**永远不会**选到 `airwalk` 类 clip：`kMovingNeedles={"run","walk","move"}` 是 WholeToken 匹配，`airwalk` 是单个 token，`walk` 不命中（`animation_sequence_match.h:6,14,34,50,139,144-150`）。它现在会被当成 walk/run 播。证据：C + 序列表 B（§2.3）——**这是缺口，不是 bug 修复项**。
- **reload / deploy**：产品**主动排除** reload 家族（`sequenceLabelIsOneShot`，`animation_sequence_match.h:161-163`；两个 picker + ranker 都拒），因为 `ReloadStand_PRIMARY` 里的 "stand" 会被 idle 子串匹配误当站姿（Pyro 全身 bind pose 的旧事故，注释 `:152-160`）。协议侧有 `post_inventory_application`/`weapon_equipped` 事件，但**事件→sequence 的映射不存在**，产品也没有「动作叠在循环之上」的合成机制。证据：C + B（事件计数，§3.3）。

## 1. walk / run

### 1.1 协议能给什么（wire 事实）

| 输入 | 状态 | 说明 |
|---|---|---|
| `DT_BaseEntity.m_vecOrigin` | 每实体快照可读，产品已用 | 唯一被用于移动判定的信号；`main.cpp:10504-10514` |
| `m_fFlags` | 可读，产品只测 bit1 | 全文件唯一 flags 位测试是 `flags & 0x2`（`main.cpp:10625`，蹲伏）；**bit0 从未使用** |
| `m_hActiveWeapon` | 可读，产品已用于挂载 | `native/src/entity_model.cpp:628`；与移动态无关 |
| 速度 / 每 tick 位移 | **不提供** | 没有任何 wire 字段叫 velocity；产品自己求差 |
| `CTEPlayerAnimEvent` | 可读（`m_hPlayer`+`m_iEvent`） | 不含速度、不含着地、不含 sequence；产品**刻意不用**（`main.cpp:10589-10600` 注释） |

### 1.2 产品策略与退化

- speed = `sqrt(dx²+dy²+dz²) * tickRate / elapsedTicks`（`main.cpp:10504-10514`）——**z 参与**，所以上跳/下落都被算成 moving。
- 阈值 `movingSpeedThreshold=0.5` u/s（`animation_clock.h:52`）；moving 时 rate=`clamp(speed/250,0.05,3.0)`（`:53-60`）。
- clip：`kMovingNeedles={"run","walk","move"}`，WholeToken + 首 token 优先（`animation_sequence_match.h:139,144-150`）；八职业 run_PRIMARY、Spy run_SECONDARY（同文件 `:36-38`）。
- 退化：该 class 表里没有匹配 → `chosen=-1` → `++playerAnimSkipped`，这一帧不出动画请求（`main.cpp:10746` 附近）。
- **生命周期边界（未验证）**：传送/重生后第一帧 `m_vecOrigin` 跳变会被算成极高 speed（rate 被 clamp 到 3.0，但状态是 moving）；同一 entityIndex 重生会继承上一世的 `lastOrigin`/`lastOriginTick`（`PlayerAnimationRuntime`，`main.cpp:3451-3477`；只有倒带才 clear）。

### 1.3 直接可见样本

实测（`states-t60.trace`，snakewater `bb841c6d…`，capture-tick 60000，rc=0，27285 行；该 run 的 demo SHA256 `308cc1453672…`）：

```
30967.2 anim: ent=2 model=models/player/soldier.mdl state=moving speed=170.379 rate=0.681514 cycle=0.580505 delta=10.580505 seq=201 label=run_PRIMARY advanced=1 origin=3985.7,-851.2,58.1
30967.3 anim: ent=3 model=models/player/scout.mdl state=moving speed=184.606 rate=0.738424 cycle=0.464033 delta=11.464033 seq=212 label=run_PRIMARY advanced=1 origin=4084.5,-871.4,8.0
30967.5 anim: ent=4 model=models/player/demo.mdl state=moving speed=359.917 rate=1.439669 cycle=0.350855 delta=22.350855 seq=147 label=run_PRIMARY advanced=1 origin=3341.0,-1186.9,8.0
```

- 同一次 run 里 `state=moving` 共 **675** 行、`state=idle` 5335 行（逐 body 逐帧驱动行，格式 `anim: ent=… state=… speed=… label=… origin=…`）。
- moving 行的 label 分布：`run_PRIMARY` 420、`a_flinch01` 198（flinch 层叠加）、`crouch_PRIMARY` 50、`run_SECONDARY` 3、`PRIMARY_flinch` 4。
- 三个不同 body（ent=2 士兵 / ent=3 侦察 / ent=4 demo）在同一帧都是 `state=moving`，speed 170–360 u/s、rate 0.68–1.44，**直接可见**。
- 同帧 `origin` 的 z 分别是 58.1 / 8.0 / 8.0：z 参与 speed 计算，所以高地/落差帧也会被判 moving（这是 §1.2 的已知副作用，不是本轮新发现）。

## 2. airwalk

### 2.1 协议能给什么

- `m_fFlags` 的原始 int 在快照里可读（`readPlayerDriveSignals` 的 `matches("m_fFlags")`，`main.cpp:9967` 附近）。
- **bit0 == FL_ONGROUND 是外部 SDK 语义，不是本仓库证明过的事实**：仓库里只有 bit1 被使用；本轮无法从 demo 协议本身证明 bit0 的含义。要把它当可靠输入，必须先做「bit0 clear 的样本在 z 上持续变化」这类交叉检定。
- 没有 z 速度、没有 onGround 布尔、没有 land 事件（`CTEPlayerAnimEvent` 的 `m_iEvent` 没有权威映射，见 §3.2）。

### 2.2 产品策略与退化

- 只有 `idle/moving/crouch/crouchwalk/flinch` 五种状态（`animation_clock.h:43`；状态层 `main.cpp:10609-10688`）。没有 air/fall/land。
- moving 只看 speed，不看 flags → 空中移动照样播 run/walk。**这就是 airwalk 缺口**：不是播错 clip，而是产品根本没有「空中」这个分层。
- `airwalk` 类 clip 永远选不到：WholeToken 下 `walk` 不匹配 token `airwalk`；`model_pose_probe` 的 matcher self-test 用合成标签 `PRIMARY_airwalk_start`/`run_SECONDARY` 正是钉这条规则（`native/tools/model_pose_probe.cpp:196-208`）。

### 2.3 直接可见样本

**本轮没有拿到「离地那一帧」的直接可见样本，原因与已排除的路径都记下来：**

1. 该 run 里逐 body 的动画行只出现两种状态：`idle` 5335 行、`moving` 675 行——**没有任何 air/fall/land 状态**（状态枚举是穷尽的，不只是采样）。这是「产品没有空中态」的直接读数。
2. 想从 `origin` 的 z 轨迹反推跳跃失败：逐 body 的 `anim:` 行每 update 有 8 行上限（`main.cpp` 注释所述），同一 ent 相邻两行的最小时间间隔 **> 1 s**（实测 dt<1s 的对数 = 0），而 trace 时间戳只到 0.1 s——**拿不到逐 tick 的 z 序列**，因此不能用 z 弧线证明「离地」。
3. 直接可见的 flags 只有状态行（crouch/flinch）里的两例：`flags=131329`（0x20101，bit0=1）与 `flags=131330`（0x20102，bit0=0），同一 tick 12016 两个 body 的 flinch 行。它们与 SDK 语义（bit0=FL_ONGROUND、bit1=FL_DUCKING）**一致**，但一致 ≠ 证明；且这些行只出现在状态迁移，不是逐帧。
4. 另有一次 t75/capture-tick 75000 的 run **无效**：产品在 demo 结束后进了自己的 loop（trace 514712 行、末行 `1565216.7 loop n=195567 tick=64352`），rc=1，被 lead kill；该 run 的全部读数已作废，不作为证据。

结论：airwalk 的**产品行为**（空中仍播 run/walk）由代码路径 + 状态穷举支撑；**具体某一帧离地**仍未经直接观测。要补这一格，需要逐帧属性采样（census 已能解 m_fFlags，但当前 trace 不打印逐帧 flags），属下一轮工具改动。

## 3. reload / deploy

### 3.1 协议能给什么

| 输入 | 状态 | 本轮读数 |
|---|---|---|
| `CTEPlayerAnimEvent` | 只有 `m_hPlayer` + `m_iEvent`（`native/include/demo_header.h:162-203`；解析 `native/src/demo_header_events.cpp:52-54`） | decoded 6321/4265/7819（snakewater/upward/bagel13），`m_iEvent` 实测 18/20/18 种取值；**没有权威枚举名**，die/taunt/reload 没有任何值被证明（动作表 §3.2） |
| `post_inventory_application` | game event，有 tick（播放域）与字段 | 251 / 531 / 239 次（三 demo）；**本轮没有解出它的字段**（gameSample 只对 player_death/player_hurt 打印全字段） |
| `weapon_equipped` | game event | 66 / 90 / 12 次；字段同样未解 |
| `m_hActiveWeapon` | server 属性，产品已读 | 可用于判断「换了武器」，但**给不出换弹的起止**；且是 packed handle（`entity_model.cpp:628-641`） |

### 3.2 event→sequence 映射：不存在，且本轮无法造出来

- 协议只给整数 `m_iEvent`；同一整数在不同地图上频次差一个量级（event=6：1553/1296/2201），只能统计推断（9–14=受伤反应、0=攻击侧，3/3 demo，域对齐口径见 RECON §3.7.1）。
- 即便拿到 event 值，也**没有 sequence 序号可用**：TF2 的 reload clip 是模型级标签（`ReloadStand_PRIMARY` 等），要按每职业/每武器表映射；产品当前的 picker 只有「循环 clip」概念。
- 因此「用 census 事件驱动 reload」需要两步决策：① 值语义的外部真值（客户端 CRC / 开火音频 / 逐 tick 画面）；② 每职业的 reload 标签表 + 分层合成。两步本轮都没做。

### 3.3 产品策略与退化

- `sequenceLabelIsOneShot(label)` = 标签含子串 "reload" 即拒（`animation_sequence_match.h:161-163`），两个 picker（`:193-194`、`:303`）与 ranker（`:314`、`:397`）都在用。
- 唯一的「动作层」是 flinch one-shot：`m_iHealth` 跨 tick 下降触发，窗口 `oneShotTicks`（默认 13 tick），窗口结束回落到移动层（`main.cpp:10609-10640`）。**reload 没有对应层**，换弹期间角色继续 idle/moving。
- 退化（现状即退化）：换弹/换武器时看不出动作，但也不会把 reload clip 误当 idle 播（显式拒绝的收益）。

### 3.4 直接可见样本

**产品侧的直接读数（同一 run，6010 条逐 body 动画行）：**

- label 全集（10 种组合）：`idle/AttackStand_PRIMARY` 3264、`idle/a_flinch01` 1508、`idle/PRIMARY_aimmatrix_crouch_idle` 420、`moving/run_PRIMARY` 420、`moving/a_flinch01` 198、`idle/AttackStand_SECONDARY` 105、`moving/crouch_PRIMARY` 50、`idle/PRIMARY_flinch` 38、`moving/PRIMARY_flinch` 4、`moving/run_SECONDARY` 3。
- **含 "reload" 的 label：0 行**；含 "air" 的 label：0 行。
- 模型侧确实有这些 clip：`evidence/t57-player-model/raw/player_models.json` 里 `ReloadStand_PRIMARY`（:5026）、`ReloadStand_PRIMARY_DEPLOYED`（:5005）、`layer_reload_standing_arms_primary_{start,loop,end}`（:~7401 起）、`airwalk_PRIMARY`（:3989）。所以「没有播」不是模型缺 clip，是产品显式拒绝。
- 换弹窗口本身：本轮没有把 `post_inventory_application`/`weapon_equipped` 的 tick 与 trace 对齐（字段未解，且 trace 不打印武器状态），因此「换弹期间角色保持 idle」只能说与代码一致，**未直接观测**。

**副产物（不属于三个缺口，但实测到了，未解释）：** idle 状态最常播的是 `AttackStand_PRIMARY`（3264/5335 ≈ 61%），而模型表里存在 `PRIMARY_aimmatrix_idle`。为什么 idle 选择器落到 AttackStand 本轮未查（需要 chooser trace / 候选排序），只记录读数，不下结论。

## 4. 实体关联与生命周期（三个缺口共用）

- **`player & 0x7ff` 是假设，不是协议事实。** 11 位索引布局在仓库里被用于解 `m_hActiveWeapon`（`native/src/entity_model.cpp:140-148,638`），且 census 实测 96/96 条 `player % 2048 == entity`、同索引 serial 跨 tick 0 冲突——但「解出的索引就是**发出这个动画的那个玩家**」仍靠统计：event 9–14 与该 tick `player_hurt` 的受伤者 96.3%（0 反例，域对齐后 exactHurt==n），event 0 是攻击者 82.4%（49 反例=5.0%）。所以：**索引解包有强证据；语义归属（谁触发/作用于谁）不是协议结论。**
- **userid 与 entity index 是两个编号空间**：userinfo 未解码（`native/src/review_bookmarks.cpp:321` 在缺身份时写 `identityReason="no-userinfo"`）。demo 级经验表来自 `player_death` 的 `userid`+`victim_entindex`（109/248/168 次死亡全一致），并用 `entity_killed` 独立交叉印证（98.7% / 99.2%）；它只在「死过的玩家」范围内成立。
- **生命周期**：walk 用「相邻 scene tick 的原点差」，所以 demo 的暂停/丢包（held tick）会改变 elapsedTicks；airwalk 需要「着地→离地」的边沿，产品没有；reload 需要「开始/结束」两个边沿 + 分层窗口，产品也没有。三者共用同一条运行时表 `playerAnimationRuntimes`（`main.cpp:3477`，按 entityIndex 键，重生继承旧状态——未验证边界）。

## 5. 需要什么决策（给 lead）

1. walk：是否接受「产品自算速度」作为驱动（现状），还是把 `m_fFlags` bit0 的语义先做一次交叉检定再引入 ground 判定。
2. airwalk：是否新增 `air` 状态（需要 bit0 语义 + 落/起边沿）；不做则必须写明「空中移动播 run/walk」是已知限制。
3. reload/deploy：是否投入「每职业 reload 标签表 + 第二层 one-shot」；前置条件是 event 值语义的外部真值，本轮不具备。

## 6. 本文没做的事

- 没有实现任何新状态；没有改产品动画逻辑。
- 没有做 airwalk 的「flags bit0 ↔ z 变化」交叉检定（需要逐 tick 属性采样）。
- 没有解出 `post_inventory_application`/`weapon_equipped` 的字段名。