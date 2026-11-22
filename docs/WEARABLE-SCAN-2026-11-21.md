# 录像饰品（wearable）可还原性门禁（task-39，2026-11-21）

* 开关：`native/tools/demo_open_probe.cpp` 新增 `--wearable-scan <tick>`（:839-850 解析，两条 usage :646 / :973 同步更新）。只在显式给出时采样，默认关闭。
* 读取规则：复用 `tf2::native::rankPropertyCandidates`（本地 helper 块 :520-640：`:520` rankedValue、`:533` rankedInt、`:544` rankedVector、`:576` serverClassName、`:588` precachePath、`:602` wearableHandleIndex、`:613` containsNoCase、`:630` snapshotStatusText），不用第二套后缀匹配——属性和选择规则漂移时，漂移恰好发生在"被测对象本身"上会看不见。属性在状态里是表限定的（`DT_BaseEntity.m_nModelIndex`），所以任何读取都只能后缀匹配，不能按完整键查。
* 一次查询：整段 dump 只用一次 `queryEntitySnapshotAtOrBeforeTick()`（实现在 `native/src/demo_header_entity.cpp:457-520`；本 dump 的那一次调用在 `native/tools/demo_open_probe.cpp:1606-1607`），三个门禁答案来自同一个快照，不会互相矛盾。
* 判词例外：tick 落不进实体历史时，探针写 `wearable_gate\tdata\tNO-DATA\t0`，**不写 FAIL**（`native/tools/demo_open_probe.cpp:1768-1777`：注释 :1768-1771、行 :1777）。"这个 tick 答不出来"和"录像里没有饰品"是两件事，混成一个 FAIL 会让门禁读出一个假的否定结论。

## 0. 结论

| 门禁条件 | 判词 | 证明计数 |
|---|---|---|
| ① 录像里确有 Wearable 实体 | **PASS** | 11 个可解析 tick 上共 332 个实例，每 tick 17-36 个；类名 `CTFWearable` / `CTFWearableVM` / `CTFWearableCampaignItem` |
| ② `m_nModelIndex` → modelprecache 路径可统计 | **PASS（占比可统计，但占比很低）** | `wearable_model_index/total` = 332/332（每个饰品都带这个槽）；`wearable_model_path/total` = **30/332 = 9.0%**，其余 302 个的索引值落在 precache 之外 |
| ③ owner 可解析到同 tick 的 `CTFPlayer` | **PASS** | 332/332 = 100%，且 `wearable_owner_live` 与 `wearable_owner_player` 每行相等 |
| 卡面 tick 600/3000/6000 直接可答 | **否（NO-DATA）** | 三条 demo 的实体历史分别从 server tick 48845 / 59563 / 56148 开始，卡面的 600 是 demo tick，远低于记录窗口 |

**结论一句话**：三个门禁条件全过，可以进 task-40；但条件② 过的是"占比可统计"，不是"路径拿得到"——**只有 9.0% 的饰品能从 `m_nModelIndex` 走 modelprecache 拿到模型路径**，其余 91% 的饰品模型路径只存在于 item schema（`items_game.txt`）。task-40 的路径来源必须把 schema 兜底当主路，不能当例外。

## 1. 三个条件到底测了什么，以及为什么第一个条件会"看起来通过、实际要注意"

### 1.1 条件① 实体存在

三个样本文档的 server class 表本来就声明了 `CEconWearable` / `CTFWearable` / `CTFWearableVM` / `CTFWearableItem` 等 9 个类（`evidence/animation-availability/oracle-all-classes.txt:5888` 等），但类表里有声明 ≠ 实例被联网过。探针实测的结果是实例确实在：每 tick 17-36 个，跨 11 个 tick 全部非零。

类名构成（每 tick）：`CTFWearable` 占绝大多数（16-35 个），`CTFWearableVM` 每 tick 必有 1-2 个；`CTFWearableCampaignItem` 只在 stv 的 49444 出现 1 个。这条分布直接就是 task-40 的类名允许表来源（handover :310 要求"区分 CTFWearable 与其它含 Wearable 的类"）。

### 1.2 条件② 路径可还原 —— 这是本卡真正改变结论的地方

原文要求是"modelindex 路径占比可统计"，字面上只要能把分子分母数出来就算过。数出来的结果是一张极端偏斜的分布：

| 样本 | tick | 饰品数 | `m_nModelIndex` 命中 precache | 占比 |
|---|---|---|---|---|
| stv（SourceTV cp_process_f12） | 49444 | 36 | 3 | 8.3% |
| stv | 51844 | 35 | 2 | 5.7% |
| stv | 54844 | 35 | 2 | 5.7% |
| stv | 90000 | 36 | 2 | 5.6% |
| stv | 169500 | 35 | 2 | 5.7% |
| pov（第三人称 POV cp_process_f12） | 60162 | 17 | 2 | 11.8% |
| pov | 62562 | 20 | 2 | 10.0% |
| pov | 65562 | 20 | 2 | 10.0% |
| pov | 200000 | 29 | 1 | 3.4% |
| bagel（SourceTV koth_bagel_rc13） | 56747 | 34 | 6 | 17.6% |
| bagel | 100000 | 35 | 6 | 17.1% |
| **合计** | 11 tick | **332** | **30** | **9.0%** |

未命中的 302 个不是"字段缺失"，而是**字段存在、值落在 precache 范围之外**。原始行（stv tick 169500，19 列制表分隔，此处用 ` | ` 显示分列）：

```
wearable | 169500 | 373 | 1 | CTFWearable | 4294967280 | - | - | - | 30390 | 0.0000,0.0000,0.0000 | 0.0000,0.0000,0.0000 | 1599490 | 2 | CTFPlayer | 1 | - | - | 0
wearable | 169500 | 374 | 1 | CTFWearable | 4294967278 | - | - | - | 30985 | 0.0000,0.0000,0.0000 | 0.0000,0.0000,0.0000 | 1599490 | 2 | CTFPlayer | 1 | - | - | 0
wearable | 169500 | 381 | 57 | CTFWearable | 4294967292 | - | - | - | 133   | 0.0000,0.0000,0.0000 | 0.0000,0.0000,0.0000 | 571401 | 9 | CTFPlayer | 1 | - | - | 0
```

`4294967280` = `0xFFFFFFF0`，按有符号 32 位就是 `-16`；`4294967292` = `-4`；`4294967294` = `-2`。解码路径见 `native/src/demo_header_net.cpp:542-560` 的 `SendPropType::Int` 分支——**三条出口对负值的形态不一样**，不能笼统说成「varint 分支不做符号扩展」：

* varint 且 `flags & 1u` 成立（这一位在网络协议里叫 `SPROP_UNSIGNED`；代码里只写位测试，不写宏名）：`:547` 的 `value.intValue = static_cast<std::int64_t>(raw);` 把 32 位无符号值原样放宽，**不做符号扩展**，负值于是呈现成 `4294967280` 这类 32 位无符号形态。
* varint 但 `flags & 1u` 不成立：走 `:549-550` 的 zigzag（`sign = static_cast<std::uint32_t>(-static_cast<std::int32_t>(raw & 1u)); value.intValue = static_cast<std::int64_t>((raw >> 1u) ^ sign);`），这一步**本身就是符号扩展**，产出的是负数。
* 非 varint：`:557`（`width >= 32u`）用 `static_cast<std::int32_t>(raw)` 打印 `-16`；`:558` 的 sub-32 位分支 `raw | (~0u << width)` 在 uint32 空间里补符号位，同样呈现 `4294967280`。

无论哪种形态，这些值都不在 `[1, 0xffff]` 里，永远不可能通过 `modelPrecache` 表查到东西——这跟 `precachePath()`（`native/tools/demo_open_probe.cpp:588-594`）的边界判断一致。

**这值是真实服务器数据、不是解码噪声**：同一实体的 `m_nModelIndex` 在 stv 的 5 个 tick（49444 / 51844 / 54844 / 90000 / 169500）上完全相同（373→4294967280、374→4294967278、381→4294967292、429→553、431→553……）。解码噪声不会逐 tick 稳定复现。

而且它**真的指向真实饰品**：`m_iItemDefinitionIndex` 全是有效值，用 `items_game.txt` 一查就是真帽子——30390 = The Spook Specs（`"prefab" "hat"`，line 80145）、30985 = Private Maggot Muncher（line 93219）、31133 = The Boom Boxers（line 98402）、828 = Archimedes（line 51047）、133/263 一类低号是早期帽子。所以模型路径存在，只是存在 **item schema** 里，不在录像的 modelprecache 里。

命中的那 30 个是同一批 MvM/咒术书类"世界模型"道具：

```
models/player/items/mvm_loot/demo/fob_h_stickybomb_diamond.mdl       (precache 553, stv 429/431)
models/player/items/mvm_loot/soldier/fob_h_rocketlauncher_diamond.mdl (precache 557, pov 381/383)
models/player/items/mvm_loot/scout/fob_e_scattergun.mdl              (bagel 710/715)
models/player/items/mvm_loot/medic/fob_e_medigun.mdl                  (bagel 786/787)
models/player/items/all_class/ring_Soldier.mdl                        (bagel 699)
models/player/items/all_class/hwn_spellbook_complete.mdl              (bagel 757)
models/player/items/cyoa_pda/cyoa_pda.mdl                             (stv 375/829, 仅 49444)
```

关键旁证：`--props` 逐属性 dump 显示，同一个 `CTFWearable`（实体 373）**有** `m_nModelIndex` 槽，而同 tick 的玩家实体 2（`CTFPlayer`，677 个属性）**根本没有** `m_nModelIndex` 这个属性，只有 `m_nModelIndexOverrides.000-003`。也就是说 model index 不是"人人都有、只是没填"，而是按类决定有没有；拿不到路径的原因在值域，不在缺字段。

### 1.3 条件③ owner 可解析

332/332 全部解析成功，而且 `wearable_owner_live` == `wearable_owner_player` == `wearable_entities`，也就是 owner handle 指向的实体在同 tick 存在**且**类名是 `CTFPlayer`。例：饰品 373 → owner handle 1599490 → 槽位 2 → `CTFPlayer` live=1。`wearable_owner`（handle 本身存在）也是 332/332。

handle 用 11 位槽掩码（`native/tools/demo_open_probe.cpp:596-601`，掩码 `kWearableHandleIndexMask = 0x7FFu` 在 :599），与 `native/src/entity_model.cpp:140-148` 的 `CBaseHandle` 布局一致；`0` / `0xFFFFFFFF` / `0x1FFFFF` 视为空。

### 1.4 一条本卡顺手修掉的真 bug（影响列数）

父指针一度读不到：wire 上这一槽叫 `moveparent`（`--props` 输出里 `prop DT_BaseEntity.moveparent int 1599490` 与 `DT_BaseEntity.m_hOwnerEntity int 1599490` 并列），只读 `m_hMoveParent` 会得到"所有饰品都没有父"的假阴性。现已按 `native/src/entity_model.cpp:885` 的做法两个名字都读（`native/tools/demo_open_probe.cpp:1685-1692`：:1685-1690 的注释说明 wire 上叫 `moveparent`，:1691 先读 `m_hMoveParent`、:1692 兜底读 `moveparent`），并在行尾多出一列 `moveParentIndex`（19 列）。

## 2. 证据

### 2.1 开关契约

```
usage: demo_open_probe [--scan] [--snapshot <tick>] [--wearable-scan <tick>] [--camera-tick <tick>] \
       [--events <n>] [--temps <n>] [--tick-audit] [--userinfo] [--userinfo-at <tick,tick,...>] \
       [--userinfo-dump <n>] <demo.dem>
```

* `--wearable-scan` 必须跟 `--scan` 一起给（与 `--props` / `--snapshot` 同一前提）。
* tick 是 **server tick**（实体历史的键），不是 demo tick。非法数字拒绝并返回 2，错误文本 `invalid wearable tick`。
* 输出两段：宽字符摘要键（接在既有 `scan=...` 长行后面）+ 制表分隔的窄字符行（`wearable` / `wearable_class` / `wearable_gate` 前缀）。窄字符写回是既有约定（:767-769 注释），因为 wcout 会按当前 C locale 重编码。
* 缺值一律打 `-`，不打 0（沿用 temps dump 的约定）。

### 2.2 实际跑的命令

```bash
# 构建（hostlock 串行，全仓唯一队列）
bash /d/_shared/tf2-hostlock/hostlock.sh wear39 -- bash tools/lockrun.sh wear39 -- bash build-target.sh demo_open_probe

# 门禁本体：--scan --wearable-scan <server tick>
native/build-nmake/demo_open_probe.exe --scan --wearable-scan 49444 "E:/demo测试/cp_process_f12/STV/1338f5de79ea343682c17dd514c980d9_matcha-20260927-1312-cp_process_f12.dem"
native/build-nmake/demo_open_probe.exe --scan --wearable-scan 60162 "E:/demo测试/cp_process_f12/POV/autorecord_2026-09-03_21-05-39.dem"
native/build-nmake/demo_open_probe.exe --scan --wearable-scan 56747 "D:/TF2_Demo_Player/testdata/demos/4a9bfb9276509d0ec5f5fdc722a95b17_match-20260927-0239-koth_bagel_rc13.dem"
```

卡面 tick 的映射规则：`server tick = 首包 server tick + demo tick`，三条 demo 分别是 48845 / 59563 / 56148。11 个可解析 tick 全部 rc=0。

### 2.3 摘要行原文（stv tick 169500）

```
wearable_tick=169500 wearable_status=available wearable_resolved_tick=169500 wearable_stale=0
wearable_states=2048 wearable_live=602 wearable_classes=44 wearable_entities=35
wearable_model_index=35 wearable_model_path=2 wearable_itemdef=35
wearable_owner=35 wearable_owner_live=35 wearable_owner_player=35
wearable_parent=0 wearable_attachment=35 wearable_origin=35 wearable_angles=35
```

> ⚠️ 本节两段原文里的 `wearable_parent=0` 是**父指针 bug 修好之前**采的（当时探针只读 `m_hMoveParent`，而录像把该槽位发成 `moveparent`）。修好后的读数是 35/35，见 §2.7。**不要引用这两段的 0。**

checkpoint 型 tick 的原文（pov tick 60162，历史从 59563 开始，落回 599 tick）：

```
wearable_tick=60162 wearable_status=checkpoint wearable_resolved_tick=59563 wearable_stale=599
wearable_states=2048 wearable_live=161 wearable_classes=39 wearable_entities=17
wearable_model_index=17 wearable_model_path=2 wearable_itemdef=17
wearable_owner=17 wearable_owner_live=17 wearable_owner_player=17
wearable_parent=0 wearable_attachment=17 wearable_origin=17 wearable_angles=17
```

### 2.4 类名直方图（stv tick 169500，`wearable_class` 前缀，与 per-entity 行走的是同一份快照）

```
CTFWearable 34   CTFWearableVM 1   CTFViewModel 26   CTFPlayer 18   CBeam 210
CBaseEntity 130  CBaseAnimating 40 CBaseDoor 18      CDynamicProp 15 CTFRagdoll 12
CLightGlow 10    CSceneEntity 8    CSprite 8         CTFDroppedWeapon 5
CTFBat 4   CTFRocketLauncher 4   CTFScatterGun 4   CTFShovel 4   CTFTeam 4
CFuncRespawnRoomVisualizer 8   CParticleSystem 3   CTFAmmoPack 3 ...
```

### 2.5 未解析 tick 的原文（stv tick 600，卡面 tick）

```
wearable_tick=600 wearable_status=before-history wearable_resolved_tick=600 wearable_stale=-1
wearable_states=0 wearable_live=0 wearable_classes=0 wearable_entities=0
wearable_model_index=0 wearable_model_path=0 wearable_itemdef=0
wearable_owner=0 wearable_owner_live=0 wearable_owner_player=0
wearable_parent=0 wearable_attachment=0 wearable_origin=0 wearable_angles=0
wearable_gate	data	NO-DATA	0
```

三条 demo × 三个卡面 tick（600/3000/6000）全部是这个形态：rc=0，无崩溃，只有一行 NO-DATA。这就是"超范围 tick 不改状态、不崩"的实测。

### 2.6 默认路径未受影响（这是本卡的硬约束）

改前后的 exe 在同一批 demo 上跑出来的 `--scan` 与 `--scan --camera-track` **逐字节相同**：

```
IDENTICAL stv  scan bytes=3276 md5 base=7b0472d168c60aed72ce9a6cee40c583 new=7b0472d168c60aed72ce9a6cee40c583 wearable_key_hits base=0 new=0
IDENTICAL stv  cam  bytes=3871 md5 base=4579888f779b21f07cec3b739847187e new=4579888f779b21f07cec3b739847187e wearable_key_hits 0 0
IDENTICAL bagel scan bytes=3263 md5 base=3e3ae8931df50cfa160ab25cfafeec35 new=3e3ae8931df50cfa160ab25cfafeec35 wearable_key_hits 0 0
IDENTICAL bagel cam  bytes=3856 md5 base=685884549b82fef95b19853bdade8962 new=685884549b82fef95b19853bdade8962 wearable_key_hits 0 0
```

两次构建都是 /W4 零警告（`grep -icE 'warning|C4[0-9]{3}' .scratch/build-target.log` = 0）。`wearable_key_hits` 为 0 证明新键确实被 `wearableTick >= 0` 挡住，没有泄漏到 `--scan` 的默认输出里。

说明：`docs/N12-ACCEPTANCE-2026-11-15.md:404` 里那条 `--scan` = 3171 B / md5 `8007b92b…` 的 pin 用的 demo 文件不在本 worktree（`evidence/` 下没有 `pov_f12.scan.nocam.txt`），所以那个数字无法复现，只能用上面的同机 A/B 代替——A/B 比跨机 pin 更强，它比的是"改前 exe vs 改后 exe，同一份输入"。

### 2.7 提交版二进制的 19 列确认（父指针修好之后）

早期样本是在父指针 bug 修好前采的，所以另一条便宜 demo（bagel，16 s）在**提交版二进制**上补采一次，确认交付形态：

```
wearable_tick=100000 wearable_status=checkpoint wearable_resolved_tick=99295 wearable_stale=705
wearable_states=2048 wearable_live=676 wearable_classes=55 wearable_entities=35
wearable_model_index=35 wearable_model_path=6 wearable_itemdef=35
wearable_owner=35 wearable_owner_live=35 wearable_owner_player=35
wearable_parent=35 wearable_attachment=35 wearable_origin=35 wearable_angles=35
wearable_gate	entities_present	PASS	35
wearable_gate	model_path	PASS	6/35
wearable_gate	owner_player	PASS	35/35
```

与修好前的同 tick 读数逐项相同（entities 35、model_path 6、owner_player 35/35），只有 `wearable_parent` 从 0 变成 35 —— 说明这次修的是读数，没有改变任何门禁结论。列数实测 19（`colCounts=19`，35 行全部一致）。

原始行（19 列，`|` 分列）：

```
wearable | 100000 | 429 | 1  | CTFWearable   | 4294967294 | -                                | - | - | 101   | 0.0000,... | 0.0000,... | 1718275 | 3   | CTFPlayer | 1 | 1718275 | 3   | 0
wearable | 100000 | 663 | 1  | CTFWearable   | 4294967270 | -                                | - | - | 133   | 0.0000,... | 0.0000,... | 962566  | 6   | CTFPlayer | 1 | 962566  | 6   | 0
wearable | 100000 | 675 | 1  | CTFWearable   | 4294967272 | -                                | - | - | 828   | 0.0000,... | 0.0000,... | 1718275 | 3   | CTFPlayer | 1 | 1718275 | 3   | 0
wearable | 100000 | 710 | 1  | CTFWearableVM | 701        | models/player/items/mvm_loot/scout/fob_e_scattergun.mdl | - | - | 65535 | 0.0000,... | 0.0000,... | 311301 | 5 | CTFPlayer | 1 | 1407416 | 440 | 0
wearable | 100000 | 786 | 98 | CTFWearableVM | 697        | models/player/items/mvm_loot/medic/fob_e_medigun.mdl     | - | - | 65535 | 0.0000,... | 0.0000,... | 1583108 | 4 | CTFPlayer | 1 | 1280291 | 291 | 0
```

**这条确认顺带暴露一个 task-40 必须处理的形态**：35 行里有 33 行 `moveParentIndex == ownerIndex`（父就是 owner 本人），另外 2 行不是——恰好就是那两个 `CTFWearableVM`（父槽位 440 / 291，owner 槽位 5 / 4），而且它们的 `m_iItemDefinitionIndex` 都是 65535（无效定义）、模型路径都来自 precache。也就是说：**"身体饰品挂在玩家身上"和"viewmodel 侧饰品挂在别的东西上"在录像里是两种不同的 parent 结构**，task-40 不能用一个 `ownerIndex` 统一处理，必须把 `CTFWearableVM` 单独判（它的父实体类型本文未核实，是需要下一步确认的开放问题）。

## 3. 默认路径不受影响

| 路径 | 状态 |
|---|---|
| `demo_open_probe --scan` | 字节不变（见 §2.6，四处 cmp 全 IDENTICAL） |
| `demo_open_probe --scan --camera-track` | 字节不变 |
| `demo_open_probe --props` / `--snapshot` / `--events` / `--temps` / `--tick-audit` / `--userinfo*` | 未改动一行；新 helper `snapshotStatusText`（`native/tools/demo_open_probe.cpp:630-640`）是**另抄一份**而不是抽公共函数，就是为了让 `--snapshot` 自己那份 inline 实现一个字节都不动 |
| 产品 `tf2_demo_native` | 完全不碰（本卡只改探针，零渲染改动） |

## 4. 已知限制

1. **`wearable_parent` 的早期读数（0）是 bug，已修并已实测**：`§1.2`-`§2.3` 那张表的原始 dump 是在父指针 bug 修好**之前**采的，所以那些文件里 `wearable_parent=0`。修好后的二进制（提交版）在 bagel tick 100000 上重采一次：`wearable_parent=35`（35/35 全部有父），19 列布局与 `moveParentIndex` 列都按预期出现（§2.7）。**归属实现要引用的是修好后的读数，不是 §1.2 那张表里的 0。**
2. **`origin`/`angles` 每行都是 `0,0,0`**，且 `complete` 均为真。饰品是纯骨骼附着对象，没有自己的世界变换——这正是 handover :316 禁止 task-41 回退到 origin/angles 硬摆的原因，本文只是提供实测佐证。
3. **`m_iParentAttachment` 每行都是 0**（332/332 存在、值 0）。真实饰品应该挂在具体骨骼上（帽子挂头骨），但录像只给 0，说明这个槽不是"挂哪根骨头"的信息源，task-40 只能靠骨骼名匹配（handover :316 的方案）而拿不到服务器侧的挂点名。
4. **卡面 tick 600/3000/6000 在三条 demo 上全部答不出来**。这不是探针缺陷，是记录窗口问题：实体历史只覆盖每条 demo 的最后一段（stv 169450..169706 的 live 窗口 + 96 个 checkpoint，pov 335909..336297，bagel 129210）。要按卡面字面 tick 验收，必须改用"demo tick + 首包 server tick"换算，或换覆盖更早 tick 的 demo。
5. **`m_nModelIndex` 的符号形态取决于解码分支**（varint 无符号形态打印成 4294967280，非 varint 分支会打印 -16）。两者都落在 precache 之外，判定不受影响，但任何下游拿这个数当无符号 16 位用之前必须先判范围。
6. **条件② 的 9.0% 是三条 demo、11 个 tick 的分布**，不是全服统计。bagel（MvM 风格掉落物多）明显偏高（17%），stv/pov（普通对战）只有 5.7-11.8%。
7. **`--wearable-scan` 与其它 dump 开关一样，一次只能用切换一个**：参数解析是单条 if 链不是循环，同时给 `--props` 和 `--snapshot` 会让后一个被当成 demo 路径。这是既有行为，本卡没改（改了就不是"只加一个开关"了）。

## 5. 参考实现与许可

* 参考仓库（**只读，未复制任何代码**）：OthmanAba/TeamFortress2 的 `econ_entity.cpp` 与 `c_tf_player.cpp`。本文的结论没有一处来自它们——所有数值都是本仓库探针实测。地址见 `docs/REFERENCE-REPOS-2026-11-21.md`（handover :320 记录）。
* item schema 数值来自本机游戏安装：`D:/SteamLibrary/steamapps/common/Team Fortress 2/tf/scripts/items/items_game.txt`（8,251,383 B）。该文件是游戏资产，不进仓库。
* 内部锚点：`native/src/demo_header_net.cpp:542-560`（`SendPropType::Int` 解码与符号，三条出口见 §1 第 4 条）、`native/src/entity_model.cpp:885-886`（父指针双名读取）、`native/src/entity_model.cpp:140-148`（`CBaseHandle` 布局，`kEntityHandleIndexBits` 在 :145）、`native/src/demo_header_strtab.cpp:827-832`（precache 查表的规范写法）、`native/include/entity_model.h:307-320`（`rankPropertyCandidates` 是唯一导出的属性选择入口；结构体在 :307-310、声明在 :319-320）。

## 6. 文件与数据

* 源码：`native/tools/demo_open_probe.cpp`（1841 行；`wc -l` 实测。相对本卡基线 `11c675c7^` 的 `git diff --numstat` 是 `498 4`——不是早前写的 `+378`）
  * :520-640 helper 块（:520 `rankedValue` / :533 `rankedInt` / :544 `rankedVector` / :576 `serverClassName` / :588 `precachePath` / :602 `wearableHandleIndex` / :613 `containsNoCase` / :630 `snapshotStatusText`）
  * :839-850 `--wearable-scan` 解析（另一个新开关 `--cosmetics-table` 在 :855-866）；两条 usage 在 :646-649 / :973-974
  * :994 `wearableDumpText`（同批的 `cosmeticsTableText` 在 :997）
  * :1599-1818 dump 主体（:1599-1612 开关块与那一次 `queryEntitySnapshotAtOrBeforeTick`，:1777 NO-DATA，:1779-1797 三条门禁行，:1800-1817 摘要键）（19 列行 + 直方图 + 三个门禁行 + 宽字符摘要）
  * :1828-1838 窄字符写回接线（:1828-1829 判定，:1835 写 `wearableDumpText`、:1836 写 `cosmeticsTableText`）
* 原始输出（`.scratch/`，未提交）：`pb-{stv,pov,bagel}-<tick>.txt`（19 个）、`pc-wear-{373,429,431,2}.txt`（`--props` 逐属性钻取）、`wearable-phaseA.txt` / `wearable-phaseB.txt` / `wearable-phaseC.txt`（汇总）。
* 复现脚本：`.scratch/wear-phaseA.sh`、`.scratch/wear-phaseB.sh`、`.scratch/wear-phaseC.sh`、`.scratch/wear-ab.sh`。

## 7. 对 task-40 的直接要求（本文档的实际交付）

1. **类名允许表**：`CTFWearable`、`CTFWearableVM`、`CTFWearableCampaignItem`（实测出现过的三类）。不含 `Wearable` 的类一律不进——探针是按类名子串匹配的，容易把 `CTFWearableVM`（viewmodel 侧）也认成身体饰品，需要单独判定它是否该画。
2. **路径来源顺序必须反过来**：实测 91% 的饰品 `m_nModelIndex` 落在 precache 之外，所以"modelprecache 优先、schema 兜底"等于"schema 承担 91% 的活"。`native/src/model_loader.cpp:1281-1283` 的 `hasModelPath` 短路和 :1329 的"跳过无路径引用"必须为饰品开一条独立路径，否则 task-41 会一片空白。
3. **归属靠 handle + 同 tick 校验**：owner 100% 可解析（332/332），所以归属本身不难；难的是 generation 变化即断开（handover :311）——探针的 `generation` 列已经是现成的验证手段。
4. **不要用 origin/angles 摆位**：332/332 全零，这是设计如此，不是数据缺失。
5. **`m_iParentAttachment` 拿不到挂点名**（全 0），骨骼匹配只能按名字（handover :316 的方案），不要期待服务器告知挂在哪根骨头上。
