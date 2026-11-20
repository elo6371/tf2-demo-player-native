# N17-BLEND — Scout 动画混合网格（blend grid）结构盘点

- 卡片：N17-BLEND（离线素材分析，只读）
- 日期：2026-11-18
- 分析对象：`.scratch/n17-model-scout/assets/` 下的 18 个 mdl（九职业各一份 `<class>.mdl` + 一份 `<class>_animations.mdl`），外加 `scout.vvd`（仅用于骨骼权重交叉验证）
- 本文只做**解析取证**：没有运行产品代码、没有渲染、没有跑构建目标，没有修改 `native/`，没有动任何别的被跟踪文件（除本文件外）
- 数值标记：**[直读]** = 头部/字段直接读出；**[推断]** = 依布局或语义推得（附一致性证据）；**UNKNOWN** = 本证据链无法确定

---

## §0 一句话结论

Scout 的混合网格是**逐序列自带**的：`scout_animations.mdl` 377 条序列里 74 条 `numblends != 1`，其中 **39 条是真正的 3×3 方向移动网格**（pose 参数 move_y/move_x、9 个互不相同的 `a_run*_PRIMARY` 动画），**28 条是 3(yaw)×4(pitch) 瞄准矩阵**，另有 6 条一维网格（4/5/17 混合）；而基础模型 `scout.mdl` 的 306 条序列 `numblends` 全为 0（只有名字/activity 占位，无混合数组、无动画数据），因此**九宫格只能且必须从 companion 文件读**。

---

## §1 方法与被依赖偏移的验证

### §1.1 方法

1. 只用 `struct` 按字节解析；脚本全部在 `.scratch/n17-blend/`（**未入 git**）。
2. 偏移不是凭记忆写的：每个偏移都用以下三种独立检查之一或多个确认——**结构步长判别**（用错步长会读不出可打印名字）、**字段间代数关系**（如 numblends 必须等于 groupsize 乘积）、**全文件覆盖**（377 条序列 / 1012 个 animdesc / 全部骨骼链）。
3. 覆盖率：18 个 mdl 共 **7,876** 个 animdesc 全部走通（九个 companion 共 7,858 个 + 九个基础模型各 2 个 = 18 个）；每条骨骼链都能在文件内终止、链上骨骼序号严格递增；377 条序列的混合数组每一项都指向合法的 localanim 索引。
4. 交叉引用的既有脚本（`.scratch/n17-model-scout/`，前一位探针产出，本卡复用并复跑）：`mdl_scan.py`（mdl/vvd 头部、bone/bodypart）、`anim_hdr.py`（动画头部计数）、`anim_frame0.py`（完整骨骼链 + RLE 采样实现）。它们的偏移结论与本卡独立重算一致。

### §1.2 被依赖偏移与验证方式

**mdl 头部（scout_animations.mdl，5,017,436 B）**

| 字段 | 偏移 | 取值 | 验证方式 |
|---|---|---|---|
| magic | +0 | `IDST` | [直读] 18/18 文件 |
| version | +4 | 48 | [直读] 18/18 文件 |
| numbones | +156 | 76 | [直读]；与 VVD 顶点里的骨骼索引用到 60 个、上界 75 一致 |
| boneindex | +160 | — | [直读]；按 216 B 步长遍历 76 个骨骼名全部可打印 |
| numlocalanim | +180 | 1012 | [直读] |
| localanimindex | +184 | — | [直读]；按 100 B 步长读名字 1012/1012 可打印 |
| numlocalseq | +188 | 377 | [直读] |
| localseqindex | +192 | 4,841,760 | [直读]；377×212 = 79,924 B，序列块末尾 4,921,684 恰为"全 1 权重表"的绝对落点（见 §4.6） |
| numposeparams | +300 | 6 | [直读] |
| poseparamindex | +304 | — | [直读]；按 20 B 步长读 6/6 名字可打印 |

**序列描述（本文件步长 212 B）**

| 字段 | 偏移 | 验证方式 |
|---|---|---|
| label（相对自身） | +4 | [直读] 步长 212 → 377/377 可打印且**互不相同**；步长 208/216/220 → 只有 44/47/45 条可读（判别通过） |
| activity 名（相对自身） | +8 | [直读] 162/377 非空且全部可打印（如 seq6 `AttackStand_PRIMARY` → `ACT_MP_ATTACK_STAND_PRIMARY`） |
| flags | +12 | [直读] 取值见 §2.1；与既有字节级结论一致：`docs/reports/T2.5-MAPPING-RECON-2026-10-12.md:83-85`、`docs/TRIAGE-LOOPFALSE-2026-11-14.md:52-53`（循环位 `kLoopingFlag=0x0001` 定义在 :23，产品代码 `animation_decoder.cpp:498` 也读 +12） |
| 数值 activity | +16 | [直读] 377/377 = −1 |
| numblends | +56 | [直读]；**377/377 满足 numblends == groupsize[0]×groupsize[1]** |
| 混合数组（相对自身） | +60 | [直读]；每项都是合法 localanim 索引（全部 < 1012），74 条 nb≠1 序列的每一项都能解析出可读 animdesc 名 |
| groupsize[2] | +68 / +72 | [直读]；377/377 上唯一恒等于 numblends 因数分解的位置组合；(3,3)/(3,4) 与 §2.2/§2.3 的方向名、瞄准层名逐一对应 |
| paramindex[2] | +76 / +80 | [直读]；取值域 {−1..5}，指向的 pose 参数名与网格语义吻合（aim 矩阵 → body_yaw/body_pitch） |
| paramstart[2] / paramend[2] | +84/+88、+92/+96 | [直读]；28/28 条 nb=12 恒为 (45,90)→(−45,−45)，39/39 条 nb=9 恒为 (−1,−1)→(1,1)，与 pose 参数区间一致 |
| weightlist（相对自身） | +156 | [直读]；加记录起点后读 76 个 float，取值/和见 §4.6；aim 权重为 0 的骨骼集合与 aim 单元从不动的骨骼集合**交集为空**（独立交叉验证） |
| posekey（相对自身） | +160 | [直读]，但含义本轮未使用（303/377 为 0，恰好是全部 nb=1 序列）→ §6 UNKNOWN-5 |

**动画描述（本文件步长 **100 B**，不是经典 SDK 的 72 B）**

| 字段 | 偏移 | 验证方式 |
|---|---|---|
| name（相对自身） | +4 | [直读] 步长 100 → 1012/1012 可打印；步长 96/104 → 只有 56/54 条可读（判别通过） |
| fps | +8 | [直读] {30×967, 24×20, 60×14, 36×7, 15×3, 30.03×1} |
| flags | +12 | [直读] {0:264, 1:302, 4:395, 5:5, 36:46}（bit0 置位 307 个） |
| numframes | +16 | [直读] 全部 > 0 |
| 动画值块（相对自身） | +56 | [直读]；该 animdesc 的骨骼链 1012/1012 走通（全 18 文件 7,876/7,876），链末为 0xFF 或 nextoffset=0 |

**动画节点与值块**

| 字段 | 偏移 | 验证方式 |
|---|---|---|
| bone | +0 | [直读]；链内严格递增，终点 0xFF |
| flags | +1 | [直读]；各 bit 的载荷长度可被字节算术对上（§4.2） |
| nextoffset | +2 | [直读]；逐节点走到 0 为止 |
| 载荷 | +4 | [推断 + 字节算术] stand_PRIMARY：60 节点，53 个 raw64(8 B) + 7 个 raw64+raw48(14 B) = 244 节点头 + 522 数据 = 762，整块 span **770 B**，逐字节吻合 |
| RLE 值对 `{i16 value, u8 valid, u8 total}` | 载荷内 | [推断 + 覆盖] 按第 3 字节 `total` 累加到 ≥ numframes 的走法，run_PRIMARY 九个 blend 的全部 RLE 轴 **0 次失败**；若按"两个 i16"读则大量失败（两种假设的判别） |

**VVD（scout.vvd，529,152 B；仅用于权重交叉验证）**

| 字段 | 偏移 | 验证方式 |
|---|---|---|
| 顶点数据起始 | **+56（头部字段）** | [直读] = 448，**不是硬编码 56**；重实现最易踩的坑之一 |
| 顶点步长/布局 | 48 B | weight[3]@+0, bone[3]@+12, numbones@+15, pos@+16 [推断]；8,261 顶点权重和恒为 1.0000（0 违例），numbones 直方图 {1:6224, 2:1687, 3:350}，被引用骨骼 60 根 |
| 与 mdl 交叉验证 | — | mdl 各 bodypart 的 numvertices 之和 = 8,261 = VVD LOD0 顶点数 |

### §1.3 与"经典 SDK 结构体"的两处硬差异（重实现必读）

1. **animdesc 步长 100 B**，不是 72 B；前 60 B 仍与经典布局口径一致（到 `animindex@+56` 为止），60..100 区段（ikrule/section 等）本轮未解析 → §6 UNKNOWN-6。
2. **groupsize 不在经典偏移 +64**：本文件 +64 **恒为 0**（377/377），真正满足 `numblends == gs0*gs1` 的是 **+68/+72**。照抄"结构体字段表"会在 +64 读到 (0,0)，**静默得出"没有网格"**——这正是"只凭一次 animDesc 就宣称九宫格"会翻车的地方。同理 paramstart/paramend 落在 +84..+96，而非按经典顺序推出的 +80..+92。

---

## §2 Scout 的序列与网格

### §2.1 总览（scout_animations.mdl）

- 序列 **377** 条、动画描述 **1012** 个、骨骼 **76** 根、pose 参数 **6** 个、序列标签 377/377 互不相同 [直读]
- numblends 直方图 [直读]：**{1: 303, 3: 4, 4: 1, 5: 1, 9: 39, 12: 28, 17: 1}** —— 303 条单混合、**74 条多混合**
- 序列 flags 取值 [直读]：0(90) / 20(70) / 1(66) / 21(4) / 22(9) / 276(1) / 1024(86) / 1025(11) / 1044(39) / 1045(1)；用到的 bit 为 0x1/0x2/0x4/0x10/0x100/0x400；**位 0x1（循环）在 82 条序列上置位**
- 数值 activity 全为 −1；有 activity **名**的 162 条 [直读]
- animdesc flags 直方图 [直读] {0:264, 1:302, 4:395, 5:5, 36:46}；fps 直方图 {30:967, 24:20, 60:14, 36:7, 15:3, 30.03:1}

### §2.2 9 混合 = 真正的 3×3 方向网格（39 条）——[推断，3 项独立证据]

1. 头部：`groupsize = (3,3)`、`numblends = 9 = 3×3`（377/377 满足乘积关系）[直读]
2. 9 个单元格指向 **9 个互不相同**的 animdesc，名字按罗盘顺序排 [直读]：
   `a_runSW_PRIMARY / a_runS_PRIMARY / a_runSE_PRIMARY / a_runW_PRIMARY / a_runCenter_PRIMARY / a_runE_PRIMARY / a_runNW_PRIMARY / a_runN_PRIMARY / a_runNE_PRIMARY`
   名字的排布本身就是"三行三列环绕中心"——9×1 不可能产生这种结构
3. pose 参数 `paramindex = (5,4)` → move_y / move_x，两轴区间都是 −1..1，序列 `paramstart = (−1,−1)`、`paramend = (1,1)` [直读] → 9 格 = 两轴各 3 个采样点的全部组合

补充（排除"同一动画复制 9 份"）：9 格的骨骼集合、RLE 块数、整块字节数各不相同（§4.4）。

**扁平化顺序 [推断]**：`k = i(move_x)*3 + i(move_y)`，外层 move_x（南→北 = −1→+1，前后），内层 move_y（西→东 = −1→+1，左右）。**注意 move_x=前后、move_y=左右（Source 坐标语义，见 §8）；按 paramindex 读：行轴 = paramindex[1]、列轴 = paramindex[0]。**证据：前三格 SW,S,SE（同一 y、x 从西到东）、中三格 W,C,E、后三格 NW,N,NE；且 `paramstart=(−1,−1)` 对应索引 0。

**⚠ Scout 没有 9 混合的 walk_* 家族。** 9 混合家族为：`run_*`(8)、`crouch_walk_*`(8)、`swim_*`(7)、`Jump_Start_*`(7)、`Jump_Float_*`(7)、`jump_double_*`(2)；`stand_*` 9 条全是 nb=1。也就是说：站立姿势**没有**方向混合，方向混合出现在 run / crouch_walk / swim / jump。

### §2.3 12 混合 = 3(yaw)×4(pitch) 瞄准矩阵（28 条）——[推断，3 项独立证据]

1. `groupsize = (3,4)`、`numblends = 12`、`paramindex = (1,0)` → body_yaw / body_pitch [直读]
2. 12 格名字呈"3 列 × 4 行" [直读]：k0/k1/k2 三格指向**同一个** animdesc `a_*_aimmatrix_*_straight_up`（pitch=+90 天顶处 yaw 三格退化重合）；随后 k3..k5 = up_right/up_center/up_left、k6..k8 = mid_*、k9..k11 = down_* → **4 个 pitch 层 × 3 个 yaw 列**
3. `paramstart = (45,90)` → `paramend = (−45,−45)` [直读]：yaw 从 +45 扫到 −45（3 采样）、pitch 从 +90 扫到 −45（4 采样），与 3×4 吻合

**扁平化顺序 [推断]**：`k = i(body_pitch)*3 + i(body_yaw)`（行宽 3 = yaw 采样数）。若按 4 宽分行，k0..k3 会混入不同 pitch，与名字矛盾。

**两族看似相反、实为同一公式**：列轴 = paramindex[0]（move_y / body_yaw = 横向），行轴 = paramindex[1]（move_x / body_pitch = 前后/纵向），`k = i(paramindex[1])*gs[0] + i(paramindex[0])`。早期版本把参数名 move_* 当成前后/左右语义，才得出"方向相反、无统一公式"的错误结论 → §8 与 §6 UNKNOWN-1。

### §2.4 其余一维网格（6 条）[直读]

| 序列 | numblends | groupsize | 参数 | 说明 |
|---|---|---|---|---|
| `a_steer_allclass_matrix` (#317) | 5 | 5×1 | move_y | 5 段转向 |
| `a_vehicle_NS_leans` (#318) | 3 | 3×1 | **move_x** | 唯一用 move_x 的一维格 |
| `a_steer_taunt_allclass_matrix` (#328) | 3 | 3×1 | move_y | |
| `a_steer_matrix_moped` (#336) | 3 | 3×1 | move_y | |
| `a_aerobic_turns` (#347) | 3 | 3×1 | move_y | |
| `r_armposes` (#2) | 4 | 4×1 | r_arm | 右上臂 4 档 |
| `r_handposes` (#1) | 17 | 17×1 | r_hand_grip | 手部 17 档抓握 |

### §2.5 全部 74 条 numblends ≠ 1 的序列 [直读]

（"distinct animdesc" = 该序列混合数组里**不同** animdesc 的个数；"frames set" = 这些 animdesc 的帧数集合，出现两个值说明各格帧数不同，见 §2.6）

| # | label | nb | groupsize | params | distinct animdesc | frames set |
|---|---|---|---|---|---|---|
| 1 | r_handposes | 17 | 17x1 | r_hand_grip / — | 17 | 1 |
| 2 | r_armposes | 4 | 4x1 | r_arm / — | 4 | 1 |
| 3 | PRIMARY_aimmatrix_idle | 12 | 3x4 | body_yaw / body_pitch | 10 | 1 |
| 4 | PRIMARY_aimmatrix_run | 12 | 3x4 | body_yaw / body_pitch | 10 | 1 |
| 5 | PRIMARY_aimmatrix_crouch_idle | 12 | 3x4 | body_yaw / body_pitch | 10 | 1 |
| 26 | SECONDARY_aimmatrix_idle | 12 | 3x4 | body_yaw / body_pitch | 10 | 1 |
| 27 | SECONDARY_aimmatrix_run | 12 | 3x4 | body_yaw / body_pitch | 10 | 1 |
| 28 | SECONDARY_aimmatrix_crouch_idle | 12 | 3x4 | body_yaw / body_pitch | 10 | 1 |
| 44 | ITEM2_aimmatrix_idle | 12 | 3x4 | body_yaw / body_pitch | 10 | 1 |
| 45 | ITEM2_aimmatrix_run | 12 | 3x4 | body_yaw / body_pitch | 10 | 1 |
| 46 | ITEM2_aimmatrix_crouch_idle | 12 | 3x4 | body_yaw / body_pitch | 10 | 1 |
| 61 | MELEE_aimmatrix_idle | 12 | 3x4 | body_yaw / body_pitch | 10 | 1 |
| 62 | MELEE_aimmatrix_run | 12 | 3x4 | body_yaw / body_pitch | 10 | 1 |
| 63 | MELEE_aimmatrix_crouch_idle | 12 | 3x4 | body_yaw / body_pitch | 10 | 1 |
| 73 | MELEE_ALLCLASS_aimmatrix_idle | 12 | 3x4 | body_yaw / body_pitch | 10 | 1 |
| 74 | MELEE_ALLCLASS_aimmatrix_run | 12 | 3x4 | body_yaw / body_pitch | 10 | 1 |
| 75 | MELEE_ALLCLASS_aimmatrix_crouch_idle | 12 | 3x4 | body_yaw / body_pitch | 10 | 1 |
| 84 | STUN_aimmatrix_idle | 12 | 3x4 | body_yaw / body_pitch | 10 | 1 |
| 85 | STUN_aimmatrix_run | 12 | 3x4 | body_yaw / body_pitch | 10 | 1 |
| 86 | STUN_aimmatrix_crouch_idle | 12 | 3x4 | body_yaw / body_pitch | 10 | 1 |
| 94 | ITEM1_aimmatrix_idle | 12 | 3x4 | body_yaw / body_pitch | 10 | 1 |
| 95 | ITEM1_aimmatrix_run | 12 | 3x4 | body_yaw / body_pitch | 10 | 1 |
| 96 | ITEM1_aimmatrix_crouch_idle | 12 | 3x4 | body_yaw / body_pitch | 10 | 1 |
| 167 | PRIMARY_aimmatrix_swim | 12 | 3x4 | body_yaw / body_pitch | 10 | 1 |
| 169 | swim_PRIMARY | 9 | 3x3 | move_y / move_x | 9 | 31,61 |
| 172 | Swim_SECONDARY | 9 | 3x3 | move_y / move_x | 9 | 31,61 |
| 175 | Swim_MELEE | 9 | 3x3 | move_y / move_x | 9 | 31,61 |
| 178 | Swim_MELEE_ALLCLASS | 9 | 3x3 | move_y / move_x | 9 | 31,61 |
| 181 | Swim_ITEM1 | 9 | 3x3 | move_y / move_x | 9 | 31,61 |
| 182 | ITEM2_aimmatrix_swim | 12 | 3x4 | body_yaw / body_pitch | 10 | 1 |
| 184 | swim_ITEM2 | 9 | 3x3 | move_y / move_x | 9 | 31,61 |
| 186 | swim_LOSER | 9 | 3x3 | move_y / move_x | 9 | 31,61 |
| 187 | PRIMARY_aimmatrix_runS | 12 | 3x4 | body_yaw / body_pitch | 10 | 1 |
| 188 | run_PRIMARY | 9 | 3x3 | move_y / move_x | 9 | 49 |
| 189 | crouch_walk_PRIMARY | 9 | 3x3 | move_y / move_x | 9 | 27,46 |
| 190 | run_SECONDARY | 9 | 3x3 | move_y / move_x | 9 | 49 |
| 191 | crouch_Walk_SECONDARY | 9 | 3x3 | move_y / move_x | 9 | 27,46 |
| 192 | run_MELEE | 9 | 3x3 | move_y / move_x | 9 | 49 |
| 193 | crouch_walk_MELEE | 9 | 3x3 | move_y / move_x | 9 | 27,31 |
| 194 | run_MELEE_ALLCLASS | 9 | 3x3 | move_y / move_x | 9 | 49 |
| 195 | crouch_walk_MELEE_ALLCLASS | 9 | 3x3 | move_y / move_x | 9 | 27,31 |
| 196 | run_LOSER | 9 | 3x3 | move_y / move_x | 9 | 49 |
| 197 | run_ITEM1 | 9 | 3x3 | move_y / move_x | 9 | 49 |
| 198 | crouch_walk_ITEM1 | 9 | 3x3 | move_y / move_x | 9 | 27,31 |
| 199 | ITEM2_aimmatrix_runS | 12 | 3x4 | body_yaw / body_pitch | 10 | 1 |
| 200 | run_ITEM2 | 9 | 3x3 | move_y / move_x | 9 | 49 |
| 201 | crouch_walk_ITEM2 | 9 | 3x3 | move_y / move_x | 9 | 27,46 |
| 202 | Jump_Start_primary | 9 | 3x3 | move_y / move_x | 9 | 23 |
| 205 | Jump_Float_primary | 9 | 3x3 | move_y / move_x | 9 | 2 |
| 206 | Jump_crouchjump_primary | 9 | 3x3 | move_y / move_x | 9 | 21 |
| 207 | Jump_Start_secondary | 9 | 3x3 | move_y / move_x | 9 | 23 |
| 208 | Jump_Float_secondary | 9 | 3x3 | move_y / move_x | 9 | 2 |
| 209 | Jump_Start_melee | 9 | 3x3 | move_y / move_x | 9 | 23 |
| 210 | Jump_Float_melee | 9 | 3x3 | move_y / move_x | 9 | 2 |
| 211 | Jump_Start_MELEE_ALLCLASS | 9 | 3x3 | move_y / move_x | 9 | 23 |
| 212 | Jump_Float_MELEE_ALLCLASS | 9 | 3x3 | move_y / move_x | 9 | 2 |
| 213 | Jump_Start_ITEM1 | 9 | 3x3 | move_y / move_x | 9 | 23 |
| 214 | Jump_Float_ITEM1 | 9 | 3x3 | move_y / move_x | 9 | 2 |
| 215 | Jump_Start_ITEM2 | 9 | 3x3 | move_y / move_x | 9 | 23 |
| 216 | Jump_Float_ITEM2 | 9 | 3x3 | move_y / move_x | 9 | 2 |
| 217 | Jump_Start_loser | 9 | 3x3 | move_y / move_x | 9 | 23 |
| 218 | Jump_Float_loser | 9 | 3x3 | move_y / move_x | 9 | 2 |
| 219 | jump_double_primary | 9 | 3x3 | move_y / move_x | 7 | 33,35 |
| 220 | jump_double_loser | 9 | 3x3 | move_y / move_x | 7 | 35 |
| 317 | a_steer_allclass_matrix | 5 | 5x1 | move_y / — | 5 | 1 |
| 318 | a_vehicle_NS_leans | 3 | 3x1 | move_x / — | 3 | 1 |
| 328 | a_steer_taunt_allclass_matrix | 3 | 3x1 | move_y / — | 3 | 1 |
| 336 | a_steer_matrix_moped | 3 | 3x1 | move_y / — | 3 | 1 |
| 347 | a_aerobic_turns | 3 | 3x1 | move_y / — | 3 | 1 |
| 362 | PASSTIME_aimmatrix_idle | 12 | 3x4 | body_yaw / body_pitch | 10 | 1 |
| 363 | PASSTIME_aimmatrix_run | 12 | 3x4 | body_yaw / body_pitch | 10 | 1 |
| 364 | PASSTIME_aimmatrix_crouch_idle | 12 | 3x4 | body_yaw / body_pitch | 10 | 1 |
| 374 | run_PASSTIME | 9 | 3x3 | move_y / move_x | 9 | 39,40 |
| 375 | crouch_walk_PASSTIME | 9 | 3x3 | move_y / move_x | 9 | 27,31 |

### §2.6 陷阱：同一 9 混合序列内各格帧数可能不同（16 条）

- `swim_*`：`[31,31,31,31,**61**,31,31,31,31]`
- `crouch_walk_*`：`[27,27,27,27,**46**,27,27,27,27]`（部分为 31）
- `jump_double_primary`：`[35,35,35,35,**33**,35,35,35,35]`
- `run_PASSTIME`：`[40,40,40,**39**,40,39,39,39,39]`

[直读] 结论：**帧数必须逐 animdesc 读**；把"第一格的帧数"当整条序列的帧数会在 16 条序列上出错。中心格（k4）通常是原地/中性分支，长度与方向格不同。

---

## §3 pose 参数表

`scout_animations.mdl` 共 6 个参数 [直读]：

| slot | name | start | end | loop | flags | #seqs using | nb values |
|---|---|---|---|---|---|---|---|
| 0 | body_pitch | -45 | 90 | 360 | 1 | 28 | {12: 28} |
| 1 | body_yaw | -45 | 45 | 360 | 1 | 28 | {12: 28} |
| 2 | r_hand_grip | 0 | 16 | 0 | 0 | 1 | {17: 1} |
| 3 | r_arm | 0 | 3 | 0 | 0 | 1 | {4: 1} |
| 4 | move_x | -1 | 1 | 0 | 0 | 40 | {3: 1, 9: 39} |
| 5 | move_y | -1 | 1 | 0 | 0 | 43 | {3: 3, 5: 1, 9: 39} |

- `loop` 字段：body_pitch / body_yaw = **360**（整圆循环），其余 0 [直读]
- `flags` 字段：body_pitch / body_yaw = **1**，其余 0 [直读]；该 bit 的确切语义未验证 → §6 UNKNOWN-2
- body_pitch **−45..90**、body_yaw **−45..45**，与 §2.3 的扫描方向一致（pitch 由 +90 降到 −45、yaw 由 +45 降到 −45）
- move_x / move_y 都是 −1..1，与 §2.2 的 9 格组合一致
- 用法归属 [推断，由 paramindex 反查]：body_pitch/body_yaw **只**被 28 条 aimmatrix 用；r_hand_grip 只被 `r_handposes`、r_arm 只被 `r_armposes` 用；move_x 被 39 条 9 混合 + 1 条 3 混合用；move_y 被 39 条 9 混合 + 3 条 3 混合 + 1 条 5 混合用

**基础模型 scout.mdl 只有 2 个 pose 参数，且区间与 companion 不同**：`body_pitch −45..45`、`body_yaw −45..45`（loop 均 360、flags 1）[直读]；companion 的 body_pitch 是 −45..**90**。谁是运行时真值 → §6 UNKNOWN-3。

---

## §4 动画值块与骨骼覆盖

### §4.1 三级寻址（"值块怎么排"的完整答案）

```
seqdesc(212B) +60  → int16[numblends]        混合数组：每格一个 localanim 索引
mdl 头部       +184 → localanim 数组（步长 100 B） → animdesc
animdesc(100B) +56 → 相对本 animdesc 的偏移   → 该格的动画值块起点
值块 = 节点链 { u8 bone, u8 flags, i16 nextoffset, payload... }
```

即 **每个 blend 格 = 一个 animdesc = 一个独立的值块**。所以"9 个 value block 怎么排"的答案是：**它们不连续排在一起**，而是由混合数组第 k 项指向的 animdesc 各自定位；k 的顺序就是 §2.2/§2.3 的扁平化顺序。

### §4.2 通道 flags 与载荷尺寸

| bit | 含义 | 每骨载荷 | 本次验证 |
|---|---|---|---|
| 0x20 | raw64：**单个** Quaternion64 常量（整段不变） | 8 B | [推断+算术] stand_PRIMARY 53×8 + 7×14 = 522 B 数据，加 60×4 节点头 = 762，span 770 ✓ |
| 0x02 | raw48：**单个** Quaternion48 常量 | 6 B | [推断] 同族；本卡三例未出现 |
| 0x08 | RLE euler 旋转：载荷开头 3 个 int16 相对偏移，指向 `{i16 value,u8 valid,u8 total}` 段 | 6 B + span | [推断+覆盖] 按 `total`(u8@+3) 累加到 ≥numframes，9 个 blend 全轴 0 失败 |
| 0x01 | raw48：**单个** Vector48 位置常量 | 6 B | [推断+算术] stand 的 7 根带位置骨骼各 14 B = 8+6 ✓ |
| 0x04 | RLE 位置：3 个 int16 相对偏移 | 6 B + span | [推断+覆盖] 同一走法 0 失败；实测每轴只有 **1 个 span**（整段常量平移） |
| 0x10 | delta（RLE 偏离绑定姿势、不再叠加 bind） | — | 本卡三例未出现 → §6 UNKNOWN-4 |

**关键结论 [推断，算术充分]**：`raw64/raw48` 是**单常量**而非"每帧一个四元数"。证据：stand_PRIMARY 45 帧、61 骨，若 raw64 为每帧 8 B 至少需 45×8×54 ≈ 19 KB，实测整块仅 **770 B**；只有常量语义才能吻合。**站立姿势没有任何逐帧旋转数据**，完全是绑定姿势 + 常量偏移。

### §4.3 样例一：`stand_PRIMARY`（seq#151，nb=1，45 帧，fps 30）

- 骨骼链 **61/76** 根：60 个节点 + 1 个 0xFF 终止节点；节点头 244 B、数据 522 B、整块 **770 B**（bones=61 nodes_bytes=244 data_bytes=522 span=770）
- 通道：**61/61 全 raw64 旋转**，**0 个 RLE 旋转块、0 个 RLE 位置块**，其中 7 根额外带 raw48 位置
- 带位置的 7 根：0 `bip_pelvis`、1 `bip_spine_0`、23 `weapon_bone`、24 `weapon_bone_1`、33 `bip_dogtag_0`、34 `bip_dogtag_1`、37 `bip_packtop`（flags 全 0x21）
- "每骨每轴值块数" = **0/0/0**（raw 通道不产生值块）

逐骨清单（序号 | 骨骼名 | flags | 载荷字节）：

```
0|bip_pelvis|0x21|14
1|bip_spine_0|0x21|14
2|bip_spine_1|0x20|8
3|bip_spine_2|0x20|8
4|bip_spine_3|0x20|8
5|bip_neck|0x20|8
6|bip_head|0x20|8
7|bip_collar_L|0x20|8
8|bip_collar_R|0x20|8
9|bip_upperArm_L|0x20|8
10|bip_upperArm_R|0x20|8
11|bip_lowerArm_L|0x20|8
12|bip_lowerArm_R|0x20|8
13|bip_hip_L|0x20|8
14|bip_hip_R|0x20|8
15|bip_knee_L|0x20|8
16|bip_knee_R|0x20|8
17|bip_foot_L|0x20|8
18|bip_foot_R|0x20|8
19|bip_hand_L|0x20|8
20|bip_hand_R|0x20|8
21|bip_toe_L|0x20|8
22|bip_toe_R|0x20|8
23|weapon_bone|0x21|14
24|weapon_bone_1|0x21|14
33|bip_dogtag_0|0x21|14
34|bip_dogtag_1|0x21|14
35|bip_dogtag_2|0x20|8
36|bip_dogtag_3|0x20|8
37|bip_packtop|0x21|14
38|bip_packmiddle|0x20|8
39|bip_thumb_0_L|0x20|8
40|bip_thumb_0_R|0x20|8
41|bip_index_0_L|0x20|8
42|bip_index_0_R|0x20|8
43|bip_middle_0_L|0x20|8
44|bip_middle_0_R|0x20|8
45|bip_ring_0_L|0x20|8
46|bip_ring_0_R|0x20|8
47|bip_pinky_0_L|0x20|8
48|bip_pinky_0_R|0x20|8
49|bip_thumb_1_L|0x20|8
50|bip_thumb_1_R|0x20|8
51|bip_index_1_L|0x20|8
52|bip_index_1_R|0x20|8
53|bip_middle_1_L|0x20|8
54|bip_middle_1_R|0x20|8
55|bip_ring_1_L|0x20|8
56|bip_ring_1_R|0x20|8
57|bip_pinky_1_L|0x20|8
58|bip_pinky_1_R|0x20|8
59|bip_thumb_2_L|0x20|8
60|bip_thumb_2_R|0x20|8
61|bip_index_2_L|0x20|8
62|bip_index_2_R|0x20|8
63|bip_middle_2_L|0x20|8
64|bip_middle_2_R|0x20|8
65|bip_ring_2_L|0x20|8
66|bip_ring_2_R|0x20|8
67|bip_pinky_2_L|0x20|8
68|bip_pinky_2_R|0x20|0
```

### §4.4 样例二：`run_PRIMARY`（seq#188，nb=9，每格 49 帧，fps 30）

九个 blend 汇总（`rot spans` = 该格所有 RLE 骨骼在三轴消耗的 `(valid,total)` 值对总数）：

| k | animdesc | name | frames | bones w/ data | raw64 rot | RLE rot bones | RLE pos bones | rot spans x/y/z | node bytes |
|---|---|---|---|---|---|---|---|---|---|
| 0 | ad499 | a_runSW_PRIMARY | 49 | 61 | 33 | 28 | 4 | 86/50/77 | 7982 |
| 1 | ad492 | a_runS_PRIMARY | 49 | 61 | 32 | 29 | 5 | 68/58/87 | 7974 |
| 2 | ad493 | a_runSE_PRIMARY | 49 | 61 | 33 | 28 | 4 | 63/61/59 | 7962 |
| 3 | ad498 | a_runW_PRIMARY | 49 | 61 | 33 | 28 | 4 | 59/45/60 | 7752 |
| 4 | ad491 | a_runCenter_PRIMARY | 49 | 61 | 60 | 1 | 0 | 2/1/0 | 820 |
| 5 | ad494 | a_runE_PRIMARY | 49 | 61 | 33 | 28 | 4 | 62/48/55 | 7878 |
| 6 | ad497 | a_runNW_PRIMARY | 49 | 61 | 33 | 28 | 4 | 59/48/62 | 7962 |
| 7 | ad496 | a_runN_PRIMARY | 49 | 60 | 32 | 28 | 4 | 52/42/63 | 7860 |
| 8 | ad495 | a_runNE_PRIMARY | 49 | 61 | 33 | 28 | 4 | 60/67/68 | 7968 |

- 8 个方向格：61 根里有 **28~29 根走 RLE 旋转**、4~5 根走 RLE 位置，整块约 **7.8 KB**；三轴值对合计约 52~86 / 42~67 / 55~87 个
- **中心格 k4 是异类**：60/61 根退回 raw64、仅 1 根 1 个 RLE 值块，整块只有 **820 B**（约方向格的 1/10）→ "跑动中心姿势"在这批素材里近乎静态，逐帧数据都在方向格
- 骨骼集合不逐格相同：k7 是 60 根、其余 61 根；但每格链都严格递增（rot span-walk non-terminating=0）
- 网格名顺序即混合数组顺序：见 §2.2

k0（`a_runSW_PRIMARY`）逐骨每轴值块数（28 根 RLE 骨骼；其余 33 根为 raw64、0 值块，未列出）：

| bone | name | flags | rot spans x | y | z | node bytes |
|---|---|---|---|---|---|---|
| 0 | bip_pelvis | 0x0c | 1 | 4 | 2 | 612 |
| 1 | bip_spine_0 | 0x0c | 3 | 1 | 1 | 324 |
| 2 | bip_spine_1 | 0x08 | 4 | 2 | 1 | 306 |
| 3 | bip_spine_2 | 0x08 | 1 | 4 | 1 | 306 |
| 4 | bip_spine_3 | 0x08 | 1 | 1 | 3 | 306 |
| 5 | bip_neck | 0x08 | 1 | 1 | 5 | 306 |
| 6 | bip_head | 0x08 | 2 | 1 | 1 | 306 |
| 7 | bip_collar_L | 0x08 | 26 | 2 | 1 | 114 |
| 8 | bip_collar_R | 0x08 | 1 | 2 | 1 | 114 |
| 9 | bip_upperArm_L | 0x08 | 6 | 1 | 1 | 306 |
| 10 | bip_upperArm_R | 0x08 | 1 | 1 | 4 | 306 |
| 11 | bip_lowerArm_L | 0x08 | 2 | 1 | 2 | 182 |
| 12 | bip_lowerArm_R | 0x08 | 0 | 0 | 1 | 106 |
| 13 | bip_hip_L | 0x08 | 2 | 2 | 1 | 306 |
| 14 | bip_hip_R | 0x08 | 2 | 1 | 3 | 306 |
| 15 | bip_knee_L | 0x08 | 2 | 1 | 2 | 198 |
| 16 | bip_knee_R | 0x08 | 0 | 0 | 1 | 106 |
| 17 | bip_foot_L | 0x08 | 1 | 1 | 3 | 306 |
| 18 | bip_foot_R | 0x08 | 3 | 3 | 2 | 306 |
| 19 | bip_hand_L | 0x08 | 1 | 1 | 1 | 306 |
| 20 | bip_hand_R | 0x08 | 3 | 4 | 1 | 306 |
| 21 | bip_toe_L | 0x08 | 1 | 2 | 15 | 174 |
| 22 | bip_toe_R | 0x08 | 2 | 2 | 10 | 172 |
| 34 | bip_dogtag_1 | 0x0c | 9 | 1 | 1 | 324 |
| 35 | bip_dogtag_2 | 0x08 | 1 | 1 | 4 | 114 |
| 36 | bip_dogtag_3 | 0x08 | 2 | 2 | 1 | 294 |
| 37 | bip_packtop | 0x0c | 6 | 2 | 1 | 342 |
| 38 | bip_packmiddle | 0x08 | 2 | 6 | 7 | 306 |

### §4.5 样例三：`PRIMARY_aimmatrix_idle`（seq#3，nb=12，**1 帧**，fps 30）

| k | animdesc | name | frames | bones w/ data | flags | node bytes |
|---|---|---|---|---|---|---|
| 0 | ad35 | a_PRIMARY_aimmatrix_idle_straight_up | 1 | 13 | 0x30,0x31 | 158 |
| 1 | ad35 | a_PRIMARY_aimmatrix_idle_straight_up | 1 | 13 | 0x30,0x31 | 158 |
| 2 | ad35 | a_PRIMARY_aimmatrix_idle_straight_up | 1 | 13 | 0x30,0x31 | 158 |
| 3 | ad32 | a_PRIMARY_aimmatrix_idle_up_right | 1 | 12 | 0x30,0x31 | 146 |
| 4 | ad33 | a_PRIMARY_aimmatrix_idle_up_center | 1 | 13 | 0x30,0x31 | 158 |
| 5 | ad34 | a_PRIMARY_aimmatrix_idle_up_left | 1 | 13 | 0x30,0x31 | 158 |
| 6 | ad29 | a_PRIMARY_aimmatrix_idle_mid_right | 1 | 10 | 0x30,0x31 | 122 |
| 7 | ad30 | a_PRIMARY_aimmatrix_idle_mid_center | 1 | 0 |  | 8 |
| 8 | ad31 | a_PRIMARY_aimmatrix_idle_mid_left | 1 | 12 | 0x30,0x31 | 146 |
| 9 | ad26 | a_PRIMARY_aimmatrix_idle_down_right | 1 | 14 | 0x30,0x31 | 170 |
| 10 | ad27 | a_PRIMARY_aimmatrix_idle_down_center | 1 | 9 | 0x30,0x31 | 110 |
| 11 | ad28 | a_PRIMARY_aimmatrix_idle_down_left | 1 | 12 | 0x30,0x31 | 146 |

- 只有**上半身**参与：12 格动到的骨骼并集 16 根（pelvis、spine_0..3、neck、head、collar_L/R、upperArm_L/R、lowerArm_L/R、hand_L/R、weapon_bone），每格 9~14 根
- **全部 raw64**，0 个 RLE 块（1 帧姿势用常量存）
- **k7（mid_center）只有一个 0xFF 终止节点** → 该格是**纯绑定姿势**（一根骨骼都不动），即"正前方"参考格
- 12 格由 10 个不同 animdesc 承担（k0/k1/k2 共用 `straight_up`）

### §4.6 每骨骼权重表（weight list）

- 定位：`序列记录起点 + i32(+156)`，长度 = **numbones = 76 个 float** [直读+算术]
- 377 条序列的权重表模式共 **17 种** [直读]：

| (min, max, sum, #zero) | #sequences |
|---|---|
| (1, 1, 76, 0) | 213 |
| (0, 1, 68, 8) | 54 |
| (0, 0, 0, 76) | 46 |
| (0, 1, 21.65, 50) | 24 |
| (0, 1, 44.8, 30) | 10 |
| (0, 1, 20, 56) | 8 |
| (0, 1, 52, 18) | 7 |
| (0, 1, 24.2, 51) | 3 |
| (0, 1, 62.8, 8) | 2 |
| (0, 1, 58, 18) | 2 |
| (0, 0.75, 16.5, 54) | 2 |
| (0, 1, 10, 66) | 1 |
| (0, 1, 42.5, 29) | 1 |
| (0, 1, 20.2, 55) | 1 |
| (0, 1, 30, 46) | 1 |
| (0, 1, 75, 1) | 1 |
| (0, 1, 15.6, 58) | 1 |

- **46 条是"全 0 权重表"**（如 `SECONDARY_fire_alt`、`MELEE_swing`、`gesture_*`、`layer_null`、`dieviolent`）—— 对骨骼姿势无贡献（多层动画的覆盖/占位层）
- 交叉验证（权重偏移正确性的独立证据）：`PRIMARY_aimmatrix_idle` 权重为 0 的骨骼是 **13–18（髋/膝/踝）与 21–22（脚趾）**，而该序列 12 格动到的骨骼集合与这 8 根**交集为空**（腿不动 → 权重 0），与"瞄准矩阵只覆盖上半身"完全一致
- 213 条全 1 权重表的序列，`+156` 相对偏移各不相同但**绝对落点相同**（4,921,684，序列块末尾）→ 文件里只存一份全 1 表；共享/去重策略 → §6 UNKNOWN-7

### §4.7 骨骼覆盖率小结

| 序列 | blend 数 | 有数据骨骼 | 每格值块特征 | 整块字节 |
|---|---|---|---|---|
| stand_PRIMARY | 1 | 61/76 | 全 raw64，0 RLE | 770 B |
| run_PRIMARY 方向格 | 1/格 | 60~61/76 | 28~29 骨 RLE 旋转、4~5 骨 RLE 位置 | ~7.8 KB/格 |
| run_PRIMARY 中心格 | 1/格 | 61/76 | 1 骨 RLE，其余 raw64 | 820 B |
| PRIMARY_aimmatrix_idle | 1/格 | 9~14/76 | 全 raw64 | 0~162 B/格 |

结论：**"每个 blend 都有完整骨架数据"是错的；"每个 blend 只覆盖子集、且子集不一定相同"才是对的。**

---

## §5 九职业快速对比（只看 numblends 分布与 idle/run/aim 家族）

### §5.1 分布

| class | seqs | animdescs | numblends hist | nb12 gs / paramidx | nb9 gs / paramidx | move_x slot | move_y slot |
|---|---|---|---|---|---|---|---|
| scout | 377 | 1012 | {1: 303, 3: 4, 4: 1, 5: 1, 9: 39, 12: 28, 17: 1} | [(3, 4)] / [(1, 0)] | [(3, 3)] / [(5, 4)] | 4 | 5 |
| engineer | 402 | 1131 | {1: 321, 2: 1, 3: 3, 4: 1, 5: 1, 9: 31, 12: 43, 17: 1} | [(3, 4)] / [(1, 0)] | [(3, 3)] / [(4, 5)] | 5 | 4 |
| soldier | 419 | 858 | {1: 370, 3: 3, 4: 1, 5: 1, 7: 1, 9: 19, 12: 23, 17: 1} | [(3, 4)] / [(1, 0)] | [(3, 3)] / [(4, 5)] | 5 | 4 |
| pyro | 351 | 842 | {1: 295, 3: 3, 4: 1, 5: 1, 9: 23, 12: 27, 17: 1} | [(3, 4)] / [(1, 0)] | [(3, 3)] / [(4, 5)] | 5 | 4 |
| demo | 326 | 767 | {1: 277, 3: 3, 4: 1, 5: 1, 9: 19, 12: 24, 17: 1} | [(3, 4)] / [(1, 0)] | [(3, 3)] / [(4, 5)] | 5 | 4 |
| heavy | 317 | 819 | {1: 260, 3: 3, 4: 1, 5: 1, 9: 21, 12: 30, 17: 1} | [(3, 4)] / [(1, 0)] | [(3, 3)] / [(5, 4)] | 4 | 5 |
| medic | 265 | 671 | {1: 212, 3: 3, 4: 1, 5: 1, 9: 31, 12: 16, 17: 1} | [(3, 4)] / [(1, 0)] | [(3, 3)] / [(4, 5)] | 5 | 4 |
| spy | 328 | 826 | {1: 273, 3: 3, 4: 1, 5: 1, 9: 22, 12: 27, 17: 1} | [(3, 4)] / [(1, 0)] | [(3, 3)] / [(4, 5)] | 5 | 4 |
| sniper | 330 | 932 | {1: 263, 3: 3, 4: 1, 5: 1, 9: 27, 12: 34, 17: 1} | [(3, 4)] / [(1, 0)] | [(3, 3)] / [(5, 4)] | 4 | 5 |

### §5.2 pose 参数（每职业 6 个，名字/区间全同，但**槽位顺序不同**）

| class | 6 pose params |
|---|---|
| scout | 0 body_pitch -45..90 L360; 1 body_yaw -45..45 L360; 2 r_hand_grip 0..16 L0; 3 r_arm 0..3 L0; 4 move_x -1..1 L0; 5 move_y -1..1 L0 |
| engineer | 0 body_pitch -45..90 L360; 1 body_yaw -45..45 L360; 2 r_hand_grip 0..16 L0; 3 r_arm 0..3 L0; 4 move_y -1..1 L0; 5 move_x -1..1 L0 |
| soldier | 0 body_pitch -45..90 L360; 1 body_yaw -45..45 L360; 2 r_hand_grip 0..16 L0; 3 r_arm 0..3 L0; 4 move_y -1..1 L0; 5 move_x -1..1 L0 |
| pyro | 0 body_pitch -45..90 L360; 1 body_yaw -45..45 L360; 2 r_hand_grip 0..16 L0; 3 r_arm 0..3 L0; 4 move_y -1..1 L0; 5 move_x -1..1 L0 |
| demo | 0 body_pitch -45..90 L360; 1 body_yaw -45..45 L360; 2 r_hand_grip 0..16 L0; 3 r_arm 0..3 L0; 4 move_y -1..1 L0; 5 move_x -1..1 L0 |
| heavy | 0 body_pitch -45..90 L360; 1 body_yaw -45..45 L360; 2 r_hand_grip 0..16 L0; 3 r_arm 0..3 L0; 4 move_x -1..1 L0; 5 move_y -1..1 L0 |
| medic | 0 body_pitch -45..90 L360; 1 body_yaw -45..45 L360; 2 r_hand_grip 0..16 L0; 3 r_arm 0..3 L0; 4 move_y -1..1 L0; 5 move_x -1..1 L0 |
| spy | 0 body_pitch -45..90 L360; 1 body_yaw -45..45 L360; 2 r_hand_grip 0..16 L0; 3 r_arm 0..3 L0; 4 move_y -1..1 L0; 5 move_x -1..1 L0 |
| sniper | 0 body_pitch -45..90 L360; 1 body_yaw -45..45 L360; 2 r_hand_grip 0..16 L0; 3 r_arm 0..3 L0; 4 move_x -1..1 L0; 5 move_y -1..1 L0 |

**最容易翻车的泛化陷阱**：`move_x`/`move_y` 槽位下标不固定——scout/heavy/sniper 是 `4=move_x, 5=move_y`，其余六职业是 `4=move_y, 5=move_x`；9 混合的 `paramindex` 随之变为 `(5,4)` / `(4,5)`。**按"槽 4 就是 move_x"写死的代码会在六个职业上静默错位。**

### §5.3 家族名与泛化判断

- `stand_*`：九职业全为 nb=1；名字基本一致（PRIMARY/SECONDARY/MELEE/MELEE_ALLCLASS/LOSER/ITEM1/ITEM2），职业特化包括 engineer 的 PDA/BUILDING/BUILDING_DEPLOYED、spy 的 stand_PDA/stand_SAPPER、sniper 的 stand_deployed_*
- `run_*`：九职业全有，网格名逐字相同（`a_runSW_* / a_runS_* / a_runSE_* / a_runW_* / a_runCenter_* / a_runE_* / a_runNW_* / a_runN_* / a_runNE_*`；spy 的等价家族名为 `run_SECONDARY`）→ **3×3 方向网格是九职业共有结构**
- `aimmatrix`：九职业全有，nb=12 的 `groupsize` 一律 (3,4)、`paramindex` 一律 (1,0) → **3×4 瞄准矩阵在九职业上稳定**
- 差异：engineer 多一个 nb=2、soldier 多一个 nb=7（各只出现一次）；nb=17（r_handposes）与 nb=4（r_armposes）九职业各 1

**泛化判断**：3×3 移动网格与 3×4 瞄准网格在九职业上**同构**，可用同一套解析逻辑；但 **pose 槽位顺序、每格帧数、每格骨骼子集必须运行时从文件读**，不能按 scout 写死。

---

## §6 这份证据能证明什么、不能证明什么

### §6.1 能证明（有字节证据）

1. 377 条序列的 numblends / groupsize / paramindex / 混合数组 / 每格 animdesc / 每个 animdesc 的值块位置，在 377/377 与 1012/1012 覆盖率上自洽。
2. 39 条 9 混合是**真 3×3 方向网格**（9 个不同方向动画 + 双轴 pose 参数 + 环绕罗盘命名），**不是 9×1**。
3. 28 条 12 混合是**3(yaw)×4(pitch) 瞄准矩阵**（含 pitch=+90 天顶处 yaw 三格退化为同一 animdesc 的物理证据）。
4. 站姿按"常量偏移"存储（770 B，无逐帧数据）、跑动方向格按 RLE 逐帧存储（约 7.8 KB/格）、中心格近乎常量（820 B）。
5. 九职业的 3×3 / 3×4 网格结构同构，但 move_x/move_y 槽位顺序不同。

### §6.2 不能证明 / UNKNOWN

| # | UNKNOWN | 现状 |
|---|---|---|
| 1 | **groupsize → blend 索引的通用扁平化公式** | **已解决（2026-11-20，§8）**：统一公式 `k = y*gs[0]+x`，`x=paramindex[0]`（横向 move_y/yaw）、`y=paramindex[1]`（前后 move_x/pitch）；原"两族相反"结论作废 |
| 2 | pose 参数 `flags`（值 1，仅 body_pitch/body_yaw）的语义 | 未验证；可能是"循环/可回绕"提示，无证据 |
| 3 | body_pitch 区间：基础模型 −45..45 vs companion −45..90，运行时以谁为准 | 两者皆 [直读]；权威未定 |
| 4 | 节点 flags 的 0x10（DELTA）、0x02（raw48 旋转）在本素材中的实际用例 | 本卡三例均未出现；全库是否存在未统计 |
| 5 | `posekey`（+160）的含义（303/377 为 0） | 未解析；不影响网格结论 |
| 6 | animdesc 100 B 中 60..100 区段（ikrule / section / localhierarchy 等） | 未解析，字段语义 UNKNOWN |
| 7 | 序列描述 +64 处恒为 0 的 u32 的真实身份 | 未解析；只确定"groupsize 不能读在这里" |
| 8 | 权重表的共享/去重策略（213 条指向同一绝对落点） | 消费者按"记录起点+偏移 读 76 个 float"即可；文件为何共享未深究 |
| 9 | RLE 通道的**数值解码**（euler 三元组 → 四元数的合成顺序） | 本卡只验证"值块布局可被走通"；数值正确性属解码器卡片。`.scratch/n17-model-scout/anim_frame0.py` 有可跑采样实现（含 euler 合成顺序），本轮未逐帧对照引擎 |
| 10 | 每条序列**实际何时播放**（activity 绑定） | 162/377 有 activity 名、数值 activity 全 −1；绑定关系需引擎/产品代码，本卡不涉及 |
| 11 | 运行时混合**权重**（相邻格插值系数） | 文件里不存在；由引擎按 pose 参数插值，本卡无法从素材证明公式 |
| 12 | 9 混合里"中心格帧数不同"的 16 条序列，引擎取哪个时长 | 数据 [直读]；播放时长策略 UNKNOWN |
| 13 | VVD 顶点权重只验了 scout.vvd LOD0 | 其余 17 个 vvd 与 LOD1+ 未验证；不影响网格结论 |
| 14 | 越界 blend 索引应报"红色错误"的行为 | 属实现卡片（要写单测）；本卡只证明"正常文件里混合索引 100% 合法"，可据此反推越界判据 |

---

## §7 复现命令

环境：Windows + MSYS bash；Python 3.10.11（`python`）。全部**只读解析**，不构建、不运行产品。

```bash
cd /d/TF2_Native_Worktrees/t105-integrate

# 0) 素材位置（只读）
ls .scratch/n17-model-scout/assets | head

# 1) 全 18 个 mdl 的总览：头部计数、numblends 直方图
python .scratch/n17-blend/inventory.py

# 2) 结构体偏移自检：步长判别 + 乘积关系 + 字段一致性
python .scratch/n17-blend/verify_offsets.py

# 3) nb != 1 序列表、网格名、pose 用法
python .scratch/n17-blend/report_b.py

# 4) 每 blend 骨骼/通道覆盖、每骨每轴值块数
python .scratch/n17-blend/report_c.py
python .scratch/n17-blend/report_span.py

# 5) 九职业对比（分布 / pose 参数 / 槽位顺序）
python .scratch/n17-blend/report_d.py
python .scratch/n17-blend/report_e.py

# 6) 权重表定位与交叉验证
python .scratch/n17-blend/report_wl4.py

# 7) 本文表格的原始生成（输出到 .scratch/n17-blend/frags/）
python .scratch/n17-blend/gen_frags3.py
```

说明：`.scratch/` 下脚本**不入 git**；本卡唯一交付物是本文档。`verify_offsets.py` 是早期版本（`find_pair` 参数个数有个小 bug 未修），偏移结论以 §1.2 的"步长判别 + 乘积关系"两条为准（`report_b.py` / `gen_frags3.py` 内置了同样的判别检查）。

---
文档结束。文中所有数据表均由 §7 的脚本从素材直接生成，未手工誊抄。

---

## §8 2026-11-20 修正：UNKNOWN-1 已解决（G5 独立复核后）

复核证据见 `.scratch/n17-g5-check.md`。本节**推翻 §2.2/§2.3 与 §6 UNKNOWN-1 中"两族嵌套方向相反、没有统一公式"的结论**；其余偏移结论不变（原始字节复核全部一致）。

1. Source 坐标语义：x = 前后（forward）、y = 左右（lateral）。pose 参数名沿用该语义：`move_x` = 前后、`move_y` = 左右；`body_yaw` = 横向、`body_pitch` = 纵向。独立证据：一维网格 `a_vehicle_NS_leans` 用 move_x（§2.4，NS=南北=前后）、`a_steer_allclass_matrix` 用 move_y（转向=左右）。
2. 原始角落名复核：9 格文件顺序 = 南行 SW,S,SE（前后=−1，左右 −1→+1）、中行 W,C,E、北行 NW,N,NE → **列 = 左右 = move_y = paramindex[0]，行 = 前后 = move_x = paramindex[1]**。因此 `k = i(move_x)*3 + i(move_y) = i(paramindex[1])*gs[0] + i(paramindex[0])`。
3. 12 格：列 = body_yaw = paramindex[0]，行 = body_pitch = paramindex[1] → 同一公式。与上游 `skeleton.rs:703-744`（x=share(0)、y=share(1)、cell=i16(list+(y*width+x)*2)）逐字一致。
4. 扁平化**有统一公式**：`k = y*gs[0] + x`，`x` 取 paramindex[0]，`y` 取 paramindex[1]。§2.2 的 `k=i(move_y)*3+i(move_x)` 是"名字误当语义"下的转写，仅因 3×3 两轴等长而数值巧合，**不要照此实现**；§2.3 的 12 格公式碰巧正确。
5. weight_list 是**连续权重**：5674 条可读权重中 1582 条含非 0/1 值（0.5/0.4/0.75/0.7…；`AttackSwim_PRIMARY` 的 pelvis/prop_bone=0.5）；aim 网格 `PRIMARY_aimmatrix_idle` 恰好 0/1（腿 [13..18,21,22] 为 0）。Step 3 应按 float 缩放叠加，不能二值化。
6. 维度读数修正：scout companion 实测 39 个 3×3、28 个 3×4、**7** 个一维（§2.4 原写 6；各职业 6~7）；27 个 mdl 上 numblends==gs0*gs1 违例 0；groupsize=0 只出现在 numblends=0 的占位序列。
