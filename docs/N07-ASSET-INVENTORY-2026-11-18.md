# N07 资产盘点：首发七图 模型 → 材质链账本（2026-11-18）

> 角色：`assets-n07` / 共享任务板 `task-11`。范围：只读静态盘点。
> 硬约束执行情况：**未构建、未运行产品 `tf2_demo_native`、未抓帧、未改产品代码、未改 tracked evidence**。
> 所有数字可用 `§3` 的脚本从安装与 BSP 原样复算；不确定的地方一律标成「待运行读数」，不写成缺陷。

## 0. 结论（先看这四条）

1. **模型文件层零缺失**：七图合计 1412 个唯一模型引用（含 BSP 内嵌 pak 自带的自定义模型），`model-not-found` = 0，`MDL 解析失败` = 0。
2. **材质链 8913/8963 槽位可解析（99.44%）**：不可解析只剩 **14 个模型 / 50 条 slot 读数 / 5 个真缺失文件 + 1 个占位语义（TOOLSNODRAW）**。
3. **旧「20 slot」不是 20 个缺失模型**：它是 slot 级读数（同一模型 × 多个 skin family × 多个 texture slot 各记一条）。20 条读数 → 14 个模型、5 个缺失材质名 + 1 个缺失 VTF。逐条复算见 §4，product 的 3 条可精确复现。
4. **Scout：17 条材质链全部解析**（`models/player/scout.mdl`，8 个 skin family）。demo 里 `ent=14 / id=91 / team=2 / family=0` 落在 **RED 常态** family，其 4 张主材质 `scout_red / scout_head_red / eyeball_l / eyeball_r` 全部存在且**不带 alpha-test**；Scout 专项**没有无法解析的材质链**。

---

## 1. 口径与边界

| 项 | 本卡口径 |
|---|---|
| 覆盖对象 | 七张首发图的 **实体模型 + prop_static 静态道具模型** 的材质链（不是 brush 面材质） |
| 不覆盖 | 世界 brush 贴图面材质（N07-BRIEF §6 的 `material_coverage_probe --list-materials` 账本已单独收口）、贴图 mip 完整性、运行时 loader 行为 |
| 判定「存在」 | BSP 内嵌 pak（lump 40 ZIP）→ tf/download loose / tf 根 loose → 4 个 VPK（tf2_misc_dir / tf2_textures_dir / hl2_misc_dir / hl2_textures_dir），路径按 Source 语义做	extbf{词法规范化}（折叠 `//`、解 `../`）后再查 |
| 判定「skin family」 | 每模型按 MDL skin 表逐 family 全解（不是只解默认 family），再单独标出「实例实际选中的 family」 |
| 证据分层 | **文件级已证** = 上面这条解析链能给出确定答案；**待运行读数** = 需要产品装载/绘制才能判 |

---

## 2. 资产真值与环境

| 项 | 真值 |
|---|---|
| TF 安装根 | `D:/SteamLibrary/steamapps/common/Team Fortress 2/tf`（上一级另有 `hl2/`，已挂载 hl2 的两个 VPK） |
| 主 VPK 条目数（实读目录树） | tf2_misc_dir **104,343**；tf2_textures_dir **29,669**；hl2_misc_dir **18,946**；hl2_textures_dir **5,032**；合计 **187,990** |
| BSP 版本 | 七图均 v20（`VBSP` 魔数 0x50534256） |
| BSP 路径 | `tf/download/maps/`：cp_process_f12、cp_gullywash_f9、cp_sultry_b8a、koth_product_final、koth_bagel_rc11、pl_upward_f12；`tf/maps/`：cp_sunshine |
| 磁盘上是否有 models/ 目录 | **没有**（如 docs/N17-MODEL-LAYER-DIAGNOSIS-2026-11-18.md:10 所述），模型与材质全部来自 VPK / BSP pak |

七图内嵌 pak（lump 40）自带内容里与材质相关的量：

| 图 | pak 内 vtf | vmt | mdl | 说明 |
|---|---|---|---|---|
| cp_process_f12 | 86 | 50 | 2 | 2 个自定义模型（cliff_wall_02c、cliff_wall_06） |
| cp_gullywash_f9 | 74 | 89 | 0 | 无自带模型 |
| cp_sultry_b8a | 181 | 267 | 52 | 自定义模型最多；路径带双前缀 `models/models/...` |
| koth_product_final | 43 | 32 | 14 | 含 stairs001 等 |
| koth_bagel_rc11 | 251 | 132 | 56 | 含 oak/handrail 等 |
| pl_upward_f12 | 416 | 93 | 41 | refresh_upward* 等 |
| cp_sunshine | 222 | 68 | 0 | 无自带模型 |

---

## 3. 方法与可复现脚本

全部在 `.scratch/assets-n07-2026-11-18/`（gitignore 内），Python 直读二进制，不依赖外部工具：

| 脚本 | 作用 | 输出 |
|---|---|---|
| `n07lib.py` | 库：VPK v1/v2 目录树 + 分卷读取、BSP 64 lump + 每-lump LZMA、game lump（prps/prpd）、MDL（v48 texture/skin/bodypart/model）、VMT tokenizer、VTF header | — |
| `inv_maps4.py` | 七图实体（entdata）+ prop_static（prps 字典/实例/每实例 skin）→ 唯一模型清单 | `inv-maps4.json` |
| `ledger3.py` | 逐模型 × 每 skin family × 每 slot：MDL→VMT→`$basetexture`→VTF，并记 `$alphatest/$translucent/$nocull/$additive/$bumpmap/$envmap` 与 Probe 块 | `ledger3.json` |
| `recon2.py` | 「只算实例实际选中 family」的口径复算 + 每模型 used-skin | stdout |
| `slots.py` | 14 个未解析模型逐 family 的 slot 明细（§6.3） | stdout |
| `absence.py` | 6 个缺失名在 4 VPK + 七图 pak + loose 里的**彻底缺席**证明 | stdout |
| `scout2.py` | Scout 材质链（带 cdtexture 解析）+ bodypart | stdout |
| `stats.py` / `reconcile.py` / `analyze.py` | 属性统计 / 计数口径对照 / 早期候选路径归因 | stdout |

运行（只读，约 40–90 s/趟）：

```
"C:/Users/Administrator/.dsh/dsh-runtimes/dsh-primary-runtime/dependencies/python/python.exe" -u .scratch/assets-n07-2026-11-18/ledger3.py
```

> 口径声明：ledger3 的「槽位」= (模型, skin family, texture slot) 三元组。同一材质会被多个模型/多个 family 反复计入，**槽位计数 ≠ 缺失文件数**。

---

## 4. 旧「20 slot」读数：出处、口径、复算与更正

### 4.1 出处

| 文档 | 行 | 记的内容 |
|---|---|---|
| docs/MAP-MATERIAL-PROGRESS-2026-11-12.md | 76–81 | `Unresolved entity paint slots remain: Sultry 13, Bagel RC11 4, Product Final 3`；同段注明是 actual candidate-resolution misses, not capture failures；13+4+3=20 |
| 同上 | 89–109 | 探针查过 Sultry/Bagel/Product 全部 **19 条 named unresolved model paths**，并逐条给出 family 与缺失材质名 |
| docs/DEFECT-REGISTER-2026-11-14.md / -MASTER | 53 / 45 | 20 个未解 slot 读数未收口（OPEN） |
| docs/PROJECT-NEXT-TASKS-2026-11-14.md | 228 | 明确「原报告 20 slot 读数可能重复，不能当 20 个独立缺失模型」 |
| docs/PROJECT-ROADMAP-HANDOFF-2026-11-12.md | 257–262 | 19 named model paths；20 条读数含重复 model families，且有些未必被当前抓帧选中 |

### 4.2 原始证据已经不可复现

原始逐条 trace 在 `.scratch/map-six-current/`（`summary.json` + 各图 `frame.trace`），该目录在 `.gitignore` 内，**当前 worktree 已不存在**。因此 13/4/3 只能「按口径复算 + 用可复现读数替换」，不能逐条重放原件。这点必须写进后续引用：**任何引用 20 的人应改为引用本卡 §5–§6 的可复现账本**。

### 4.3 计数口径（已由复算确定）

- 单位是 **(模型, skin family, texture slot)** 的「候选解析失败」读数，不是模型数、不是文件数。
- **没有混入世界 brush**：20 条读数全部落在 prop/entity 模型的 texture slot 上；brush 面材质是另一条统计线（N07-BRIEF §6：vmtMissing 全 0，剩余非 ok 全是 water 的 `$abovewater/$belowwater` 无 `$basetexture`，不属缺陷）。
- **存在重复计数**：Sultry 的 `brick_pillar_003` 24 条对应 **6 个模型**；Bagel 的 `tf_oak_tree01a` 15 条对应 **3 个模型**；Product 的 `cliff_wall_101a` 8 条对应 **2 个模型**。

### 4.4 复算对照（两个口径并排）

| 图 | 旧读数 | 全 family 槽位（ledger3） | 只算实例实际选中 family | 唯一未解析模型数 | 结论 |
|---|---|---|---|---|---|
| cp_sultry_b8a | 13 | 26（25 VMT + 1 VTF） | **12** | 8 | 12 与旧 13 相差的 1 条 = `healthkit_patch` 的 fam0（实例只用到 fam1/fam2，fam0 未被实例选中）；旧探针把它一起算进去了 → **13 可被解释** |
| koth_product_final | 3 | 9 | **3** | 3 | **精确复现**（2 个 cliff 模型各 1 条 + stairs001 的 TOOLSNODRAW 1 条） |
| koth_bagel_rc11 | 4 | 15 | 7 | 3 | 旧 4 = 3 个 oak 模型各记 1 条 + 1 条 zombie 路径；实例实际用到 oak 的 fam2/fam3 共 7 条 → **旧读数是「每模型记 1 条」的更粗口径** |
| cp_process_f12 / cp_gullywash_f9 / cp_sunshine | 0 / 0 / 0 | 0 / 0 / 0 | 0 | 0 | 一致 |
| pl_upward_f12 | 0（旧文未列） | 0 | 0 | 0 | 一致（本卡新增确认） |

**结论**：20 = 13 + 4 + 3 是「3 张图 × 各自的 slot 读数」，可分解；但 **20 ≠ 20 个模型、≠ 20 个文件**。本卡的可复现替代读数：**14 个模型 / 5 个缺失材质文件 + 1 个缺失 VTF + 1 个占位语义名**。

### 4.5 必须更正的三处旧读数

| # | 旧记录 | 本卡证据 | 处置 |
|---|---|---|---|
| 1 | Sultry 的三个缺 VTF 之一 `dev/dev_measurewall01d.vtf` 不存在 | 实读 `materials/dev/dev_measurewall01d.vtf` → **hl2_textures_dir.vpk 中存在** | **撤销该条**（旧探针很可能未挂 hl2_textures；N07-BRIEF §6 记录过「挂载 hl2 后 vmtMissing 10→0」同类坑） |
| 2 | `concrete/concretewall002.vtf` 不存在 | 实读 → **tf2_textures_dir.vpk 中存在** | **撤销该条** |
| 3 | `watergate/wall028a.vtf` 不存在（VMT 壳在 BSP 里能解析） | 复算：VMT 在 sultry 的 **pak** 内（`materials/models/props_watergate/watergate_sky_buildings_02/wall028a.vmt`），其 `$basetexture = materials/watergate/wall028a.vtf`，**4 个 VPK + 七图 pak + loose 都没有该 VTF，也无同名文件** | **保留**（真缺失） |

另：旧记录「Bagel zombie 模型的 family 1 请求 `models/player/items/all_class/demoman_blue_zombie.vmt`」——该路径确实不存在，但**同名文件在安装内以 `materials/models/player/demo/demoman_blue_zombie.vmt` 存在**（tf2_misc）。本次七图的 prop/entity 模型引用里**没有复现**该 zombie 引用（`zombie/all_class` 命中的是 workshop 装饰物），故只作为「路径口径问题」记录，不计入本卡缺失项。

---

## 5. 七图总账

| 图 | 实体数 | prop_static 实例 | prps 字典 | 唯一模型 | 模型位置（vpk / pak） | 模型缺失 |
|---|---|---|---|---|---|---|
| cp_process_f12 | 1024 | 1353 | 138 | 146 | 144 / 2 | 0 |
| cp_gullywash_f9 | 903 | 1257 | 135 | 145 | 145 / 0 | 0 |
| cp_sultry_b8a | 1180 | 1866 | 201 | 209 | 157 / 52 | 0 |
| koth_product_final | 477 | 942 | 172 | 181 | 167 / 14 | 0 |
| koth_bagel_rc11 | 872 | 1562 | 232 | 245 | 189 / 56 | 0 |
| pl_upward_f12 | 1106 | 1107 | 268 | 281 | 240 / 41 | 0 |
| cp_sunshine | 988 | 1617 | 196 | 205 | 205 / 0 | 0 |

槽位总量（七图 × 全 family）：**8963**；可解析 **8913**；VMT 缺失 49；VTF 缺失 1。

> 口径注意：entdata 里 `model` 值为 `"*1"`/`"*10"` 的是 **brush 子模型索引**，必须过滤，否则会造出 200+ 条 `*N.mdl` 假缺失（本卡早期踩过，已修）。这也是「brush 是否混入」的可复现证据。

---

## 6. 未解析材质链清单（14 模型 / 50 slot 读数）

### 6.1 A 类：安装内确实没有的文件（4 个 VMT + 1 个 VTF）

| 缺失名 | 模型请求的 VMT 路径 | 在 4 VPK / 七图 pak / loose 的检索结果 |
|---|---|---|
| `brick_pillar_003` | `materials/models/props_watergate/brick_pillar_003.vmt` | **全无**（basename 命中 0；七图 pak 内 0） |
| `cliff_wall_101a` | `materials/models/props_rocks/cliff_wall_101a.vmt` | **全无**（basename 命中 0） |
| `tf_oak_tree01a` | `materials/models/props_foliage/tf_oak_tree01a.vmt` | **全无**（basename 命中 0） |
| `wood_debris1` | `materials/models/props_sultry/wood_debris1.vmt` | 该路径无；同名文件只存在于 `materials/models/props_coalmine/wood_debris1.vmt`（tf2_misc）→ **模型要的路径没有** |
| `wall028a`(VTF) | VMT 在 sultry pak：`materials/models/props_watergate/watergate_sky_buildings_02/wall028a.vmt` | `$basetexture=materials/watergate/wall028a.vtf`，**该 VTF 全无**；`materials/watergate/wall028a.vmt` 也不存在 |

### 6.2 B 类：占位语义 `TOOLSNODRAW`（不是文件缺失）

`models/props_product/stairs001.mdl` 槽 1 的纹理名是 `TOOLSNODRAW`，模型相对路径 `materials/models/props_product/toolsnodraw.vmt` 不存在；但**引擎内建名 `materials/tools/toolsnodraw.vmt` 在 tf2_misc 与 hl2_misc 都存在**（shader=`lightmappedgeneric`，`$basetexture=Tools/toolsnodraw`，`%compilenodraw=1`），其 VTF 也在 tf2_textures。
→ 静态只能证明「文件在」；**引擎是否把无路径名回退到 `materials/tools/`** 属运行行为，列 §10 验证项（本卡不改产品行为、不发明替代贴图）。**§12.5 已进一步定位**：该标准名不在模型的候选路径上（候选只有 `materials/models/props_product/toolsnodraw.vmt` 与 `materials/toolsnodraw.vmt`），本机二进制字符串不支持自动回退，产品侧确定行为是「跳过该子网格」。

### 6.3 逐模型 × 逐 family 的 slot 明细（原样来自 ledger3）

| 图 | 模型 | family 数 | 逐 family：ok / miss（slot:材质） |
|---|---|---|---|
| sultry | `props_watergate/brick_pillar_01/02/03、brick_pillar_len3_02/03、len4_02` | 3 | fam0: ok 0:001,1:002 / **miss 2:003**；fam1: ok 0:002,1:002 / **miss 2:003**；fam2: ok 1:002 / **miss 0:003,2:003**（6 个模型同构） |
| sultry | `props_sultry/healthkit_patch` | 3 | fam0: ok 1:woodammo,2:woodhealth / **miss 0:wood_debris1**；fam1、fam2 全 ok |
| sultry | `props_watergate/watergate_sky_buildings_02` | 1 | fam0: ok 0:ibeam001b,1:wall028,2:brickwall001b,3:brickwall006c,4:concretewall014b,5:ground_brick_01,6:wood_shingles01,8:brickwall003e / **miss 7:wall028a（VTF 缺）** |
| product | `props_rocks/cliff_wall_10`、`cliff_wall_10_mirrored` | 3 | fam0: ok 0:cliff_wall_10,2:cliff_wall_10_snow / **miss 1:101a**；fam1: ok 2:snow / **miss 0:101a,1:101a**；fam2: ok 0:snow,2:snow / **miss 1:101a** |
| product | `props_product/stairs001` | 1 | fam0: ok 0:WOOD_BRIDGE001 / **miss 1:TOOLSNODRAW** |
| bagel | `props_foliage/oak_l_01`、`oak_m_01`、`oak_s_01` | 4 | fam0: ok 0:01,2:01b,3:01c / **miss 1:01a**；fam1: ok 2:01b,3:01c / **miss 0:01a,1:01a**；fam2: ok 0:01b,2:01b,3:01c / **miss 1:01a**；fam3: ok 0:01c,2:01b,3:01c / **miss 1:01a** |

### 6.4 实例 skin 是否落在缺失 family（可见性预估，非抓帧结论）

| 图 | 模型 | 实例用到的 skin | 是否命中缺失 family |
|---|---|---|---|
| sultry | 6 个 brick_pillar* | {0,1}（len4_02 只有 {1}） | **命中**（fam0 slot2、fam1 slot2） |
| sultry | healthkit_patch | {1,2} | 不命中（只缺 fam0） |
| sultry | watergate_sky_buildings_02 | {0} | **命中**（slot7 的 VTF） |
| product | cliff_wall_10 / _mirrored | {0} | **命中**（slot1） |
| product | stairs001 | {0} | **命中**（slot1 TOOLSNODRAW） |
| bagel | oak_l_01 | {2,3} | **命中**（fam2/3 的 slot1） |
| bagel | oak_m_01 | {0,2,3} | **命中** |
| bagel | oak_s_01 | {0,2} | **命中** |

→ 按「实例 skin 是否命中缺失 family」这**单一**条件，14 个模型里 13 个会命中（仅 `healthkit_patch` 不会）。但**这个口径不完整**：§12.3 补了「mesh 是否真的引用该槽」、§12.4 补了「绘制阶段按实例 skin 取纹理」，两个条件合起来静态上只有 **2 条**可能落到画面（`stairs001` slot1、`watergate_sky_buildings_02` slot7）。本节保留为最初粗筛，结论以 §12.4 为准；抓帧仍未做。

---

## 7. VMT 属性统计（slot 加权，七图）

| 图 | 解析槽位 | `$alphatest=1` | `$translucent=1` | `$nocull=1` | `$additive=1` | `$bumpmap` | `$envmap` | 含代理/动画块 |
|---|---|---|---|---|---|---|---|---|
| cp_process_f12 | 1190 | 527 | 509 | 25 | 0 | 159 | 182 | 54 |
| cp_gullywash_f9 | 1015 | 18 | 3 | 15 | 1 | 595 | 593 | 48 |
| cp_sultry_b8a | 1327 | 353 | 354 | 12 | 2 | 325 | 334 | 64 |
| koth_product_final | 1050 | 521 | 507 | 38 | 1 | 23 | 21 | 47 |
| koth_bagel_rc11 | 1317 | 87 | 20 | 78 | 0 | 457 | 489 | 88 |
| pl_upward_f12 | 1476 | 27 | 3 | 29 | 0 | 696 | 603 | 40 |
| cp_sunshine | 1538 | 400 | 345 | 41 | 1 | 502 | 483 | 47 |
| **合计** | **8913** | **1933** | **1741** | **238** | **5** | **2757** | **2705** | **388** |

要点：
- **alpha 材质在七图普遍存在**（`$alphatest` 占 21.7%、`$translucent` 占 19.5%）：绘制侧只要在这两键上判错（例如把 alphatest 当不透明画、或 translucent 不排序），就会在这些道具上出硬边/排序错，而不是「少数特例」。
- `$nocull` 238 槽位：单面片（草/铁栅/桥面等）被背面剔除会直接变透明。
- `$additive` 仅 5 槽位（发光体），`$bumpmap/$envmap` 占三分之一：法线/环境反射缺失会在这些金属/砖面上表现为「平掉」。
- 388 个槽位的 VMT 带二级块（`Proxies`/`AnimatedTexture` 等）；本卡只记录「块存在」，不评估代理求值（属渲染侧实现）。

---

## 8. Scout 专项

### 8.1 `models/player/scout.mdl` 结构（实读 vpk:tf2_misc）

| 项 | 值 |
|---|---|
| 版本 | 48 |
| texture | **17**（flags 全 0） |
| skin family | **8**，每 family skinref **17**（布局 A：`numskinref@220 / numskinfamilies@224`） |
| bodyparts | **5**：`scout`(1 model)、`hat`(2)、`headphones`(2)、`shoes_socks`(2)、`dogtags`(2) |
| cdtextures | `models\player\scout\`、`` |
| 与已有记录一致 | `scout.mdl` textures=17 / skinfamilies=8 / bodyparts=5 → 与 docs/N17-MODEL-LAYER-DIAGNOSIS-2026-11-18.md:56 一致 |

### 8.2 8 个 skin family 的语义（逐 family 实读）

| family | slot0（身体） | slot1（头） | 状态 | 是否带 `$alphatest` |
|---|---|---|---|---|
| **0** | `scout_red` | `scout_head_red` | **RED 常态** | **否** |
| 1 | `scout_blue` | `scout_head_blue` | BLU 常态 | 否 |
| 2 | `scout_red_invun` | `scout_head_red_invun` | RED 无敌 | 否 |
| 3 | `scout_blue_invun` | `scout_head_blue_invun` | BLU 无敌 | 否 |
| 4 | `scout_red_zombie_alphatest` | `scout_head_zombie` | RED 僵尸 | **是（slot0/slot11 等）** |
| 5 | `scout_blue_zombie_alphatest` | `scout_head_zombie` | BLU 僵尸 | **是** |
| 6 | `scout_red_invun_zombie` | `scout_head_red_invun` | RED 无敌僵尸 | **是** |
| 7 | `scout_blue_invun_zombie` | `scout_head_blue_invun` | BLU 无敌僵尸 | **是** |

slot2/3 = `eyeball_l`/`eyeball_r`（family 2..7 替换成 invun/zombie 眼球）；slot4..16 = 帽子/耳机/鞋袜/狗牌等其它 bodypart 的候选材质，**8 个 family 之间完全相同**。

### 8.3 17 条材质链（全部可解析）

| 纹理名（slot） | VMT | shader | `$alphatest` | `$basetexture` → VTF |
|---|---|---|---|---|
| scout_red (0) | materials/models/player/scout/scout_red.vmt | vertexlitgeneric | — | 存在 |
| scout_head_red (1) | …/scout_head_red.vmt | vertexlitgeneric | — | 存在 |
| eyeball_l (2) / eyeball_r (3) | …/eyeball_l.vmt、eyeball_r.vmt | **eyerefract** | — | 无 `$basetexture` 键（eyerefract 走环境折射，正常） |
| scout_blue / scout_head_blue (4/5) | …/scout_blue.vmt、scout_head_blue.vmt | vertexlitgeneric | — | 存在 |
| scout_red_invun / scout_head_red_invun (6/7) | 同名 vmt | vertexlitgeneric | — | 存在 |
| eyeball_invun (8) | …/eyeball_invun.vmt | vertexlitgeneric | — | 存在 |
| scout_blue_invun / scout_head_blue_invun (9/10) | 同名 vmt | vertexlitgeneric | — | 存在 |
| scout_red_zombie_alphatest (11) | 同名 vmt | vertexlitgeneric | **1** | 存在 |
| scout_head_zombie (12) | 同名 vmt | vertexlitgeneric | — | 存在 |
| eyeball_zombie (13) | 同名 vmt | vertexlitgeneric | — | 存在 |
| scout_blue_zombie_alphatest (14) | 同名 vmt | vertexlitgeneric | **1** | 存在 |
| scout_red_invun_zombie (15) | 同名 vmt | vertexlitgeneric | **1** | 存在 |
| scout_blue_invun_zombie (16) | 同名 vmt | vertexlitgeneric | **1** | 存在 |

- **Scout 材质链无法解析项 = 0**。17/17 VMT 命中，`$basetexture` VTF 全部存在（`eyeball_*` 用 eyerefract 无 `$basetexture` 属设计如此）。
- **`$nocull` = 无**（17 条均未设）；**`$translucent` = 无**。
- alpha-test 只出现在 zombie 家族（family 4..7），**RED/BLU 常态 family 不带 alpha-test** → 正常对局里 Scout 的绘制不需要 alpha-test 分支就能正确出图。

### 8.4 bodygroup 组合与 `m_nBody` 语义

- 组合空间：`hat`×`headphones`×`shoes_socks`×`dogtags` = 2×2×2×2 = **16 种**，`scout` 本体固定 1 → bodypart 选择器是「位标志」语义（hat=1、headphones=2、shoes_socks=4、dogtags=8，见 docs/N17-MODEL-LAYER-DIAGNOSIS-2026-11-18.md:46）。
- **但几何上等价**：`bp1..bp4` 的 **model1 顶点数 = 0**（同文档：`bp1 model1 nv=0`、`bp2/3/4 model1 nv=0`，VTX 侧同为空）→ 即使 bodygroup 选择器失效，Scout 也不会重复叠画/少画几何；`m_nBody=0` 恰好等于「每组取 model0」。
- 备注：本次解析 bodypart 的 model 名读出空串（mstudiobodypart→mstudiomodel 的 name offset 口径未收口），**不影响**上面的 model 数与 §8.3 材质链；如需 bodypart 内 model 名字，需另修解析（列已知限制）。

### 8.5 与 demo `ent=14 / id=91 / team=2` 的对照

权威读数（docs/N17-MODEL-LAYER-DIAGNOSIS-2026-11-18.md:154，原文）：

```
entity: id=91 ent=14 model=models/player/scout.mdl team=2 family=0 state=idle seq=175 label=stand_PRIMARY
        skinned=1 origin=365.7,-2068.3,129.4 frame_tick=1 cycle=0.000000 frame=0/45 localFrame=0 sectionFrames=0
        skinBones=78 validBones=78
```

| 项 | 读数 | 与静态资产对照 |
|---|---|---|
| model | `models/player/scout.mdl` | 存在（vpk:tf2_misc），17 tex / 8 family |
| team | 2（RED） | 与 `family=0` 自洽：**family 0 = RED 常态** → `scout_red / scout_head_red / eyeball_l / eyeball_r` |
| family（skin family） | 0 | 4 条主材质链**全解析**，无 alpha-test、无 nocull → 正常红 Scout 不需要额外材质分支 |
| state / seq | idle / seq 175 `stand_PRIMARY` | 与 docs/N17-SCOUT-CAMERA-ANIMATION-2026-11-17.md:80、docs/PROJECT-NEXT-TASKS-2026-11-14.md:346 记录一致（动画资产在独立 `models/player/scout_animations.mdl`，不属本卡材质链） |
| skinned / skinBones | 1 / 78 valid | 骨骼与蒙皮生效；材质侧无缺失 |
| HUD 能力位（同 doc :155） | `modelSkin=none modelSeq=-1 bodygroup=unknown` | **产品自述未把 skin/bodygroup 能力报出来**——这是产品侧能力上报缺口，不是资产缺失（§9 待运行项） |

### 8.6 Scout 无法解析材质链：无

结论：Scout（含 8 个 family × 17 slot = 136 条链）**全部可解析**；不存在需要「fallback 材质」的槽位。唯一需要其它人接手的是「产品是否按 family 选材质」的**运行/代码侧**问题（HUD `modelSkin=none`），不是资产账本问题。

---

## 9. 结论分层

### 9.1 文件级已证（可直接作为事实引用）

1. 七图 1412 个唯一模型引用，**0 个模型文件缺失**；MDL 解析 0 失败。
2. 材质链 8963 槽位中 **8913 可解析（99.44%）**；未解析 50 条读数集中在 **14 个模型**。
3. 真缺失文件只有：`brick_pillar_003.vmt`、`cliff_wall_101a.vmt`、`tf_oak_tree01a.vmt`、`wood_debris1.vmt`(props_sultry 路径)、`watergate/wall028a.vtf`；另 `TOOLSNODRAW` 是占位语义（标准文件在）。
4. 旧读数 `dev_measurewall01d.vtf`、`concretewall002.vtf` 「缺失」**不成立**，应撤销。
5. 旧 20 slot = slot 级读数（含重复 family/重复模型），**可分解、不可当 20 个缺失模型**；本卡替代读数：14 模型 / 5 文件 / 1 占位名。
6. Scout 17 条链全解析；RED 常态 family（0）不带 alpha-test/nocull；demo ent14 的 team=2/family=0 与静态 family 语义自洽。

### 9.2 需运行读数才能判（本卡不下结论）

1. `TOOLSNODRAW`：引擎是否把无路径的 `TOOLSNODRAW` 回退到 `materials/tools/toolsnodraw.vmt`（静态只证文件在）。→ **§12.5 已有静态结论**：该名不在候选路径上，产品侧确定跳过该子网格；引擎侧本卡无法证否。
2. 按 §12.4 更正后的口径，只有 2 条（`stairs001` slot1、`watergate_sky_buildings_02` slot7）可能在画面可见；抓帧确认是否真出现缺面/缺图（§6.4 是最初粗筛，已被 §12.3–12.4 收窄）。
3. sultry 的自定义模型路径双前缀 `models/models/...`：产品 loader 是否与引擎同口径（本卡能按原路径命中，但产品可能不同）。
4. 产品材质层是否按 skin family 选择（HUD `modelSkin=none` 提示未接通）；Scout 换 team/BLU 时是否真换到 family 1。
5. bodygroup 选择器是否接通（Scout 几何应不变；对 model1 非空职业才可判）。
6. 世界 brush 面材质另账（N07-BRIEF §6 的 coverage 读数需按同一口径复跑，本卡未覆盖）。

---

## 10. 最小运行验证清单（给接手的人，本卡未执行）

| # | 动作 | 观察点 | 判据 |
|---|---|---|---|
| 1 | 用已有只读探针复跑 `material_coverage_probe --list-materials` 七图 | 是否仍为 process 119/119、sultry 260、gullywash 112、sunshine 167、bagel 97、upward 85、product 67，vmtMissing=0 | 与 N07-BRIEF §6 一致 → brush 线无回归 |
| 2 | 抓 sultry 的 `brick_pillar*`（skin 0/1）与 `watergate_sky_buildings_02` | 第 3 材质槽 / slot7 是否出现棋盘格或纯色 | 出现 = 真缺失在画面可见；消失 = loader 有回退，需查回退来源 |
| 3 | 抓 bagel `oak_l/m/s_01`（skin 2/3）与 product `cliff_wall_10`（skin 0） | slot1 是否缺图 | 同上 |
| 4 | 抓 product `stairs001` 的 slot1 | 是否呈现 toolsnodraw 样式（灰白网格）而非错误材质 | 若呈现 toolsnodraw → 回退路径成立 |
| 5 | Scout 换队 / 改 `m_nSkin` 到 1 | 是否由红变蓝 | 变 = family 选择接通；不变 = 材质层未消费 skin（资产侧已证明 family 1 齐全，问题在产品） |
| 6 | Scout 同帧改 `m_nBody` 0/1/2/4/8 | 几何是否不变 | 应与 §8.4 一致（model1 顶点为 0） |

---

## 11. 附录：本轮踩过的解析坑（写下来防止下一个人重新踩）

1. **Source 每-lump LZMA 不是 LZMA-alone**：`b'LZMA' + u32 actual + u32 lzmaSize + u8 packed(lc/lp/pb) + u32 dictSize`，RAW LZMA1 从 offset 17 开始；`FORMAT_ALONE` 会报 “Input format not supported by decoder”。
2. **game lump 的 `fileofs` 是相对 BSP 文件起点的绝对偏移**，不是相对 lump；用 `gl[ofs:]` 会切出 0 字节。lump35 条目 id 小端读出是反的：静态道具 `prps`、细节道具 `prpd`。载荷本身还带一层 LZMA。
3. **静态道具 prps 字典是定长 128 字节 \0 填充**，不是 cstr 串接；用 cstr 读会把 propCount 读成 10 位数（4 亿次循环）。实例 stride 实测 **72 字节**（七图一致）。
4. **MDL 名字偏移是相对各自结构体**：texture 名 = `cstr(data, o+noff)`，**flags 在 +4**；bodypart `mstudiobodyparts_t`=16B、model `mstudiomodel_t`=148B。
5. **VMT 的 shader 行是带引号的**（`"LightmappedGeneric"\r\n{`），判 shader 要判「第一个 token 后面跟 `{`」，不能判它是否带引号；tokenizer 要用 key/value 交替状态机。
6. **候选路径必须词法规范化**：`materials//models/...`（cdtexture 空串）与 `materials/../props_forest/...`（纹理名自带 `../`）若不折叠，会把 14 个存在材质误判成缺失——这也是本卡早期 vmtMiss 从 4/48/22/94/15 降到 0/26/9/15/0 的原因。
7. **entdata 里 `"*N"` 是 brush 子模型索引**，不是模型路径；不过滤会造出假缺失。
8. sultry 的 pak/字典里自定义模型路径带**双前缀** `models/models/...`（97 条），按原样能命中，别自作主张去重。

## 12. 五个缺失文件：引用链、生成交叉、运行时读数与 `TOOLSNODRAW` 语义

> 本节是 lead 追加要求的只读补充：① 每个缺失文件的引用链（VMT→MDL→slot/family）与「是否真被七图生成」；② `TOOLSNODRAW` 的回退语义与产品当前实现的静态判断。
> **证据边界**：结论只来自本机可复核的三处材料——(a) 安装内 VPK 与七图 pak 的资产，(b) 产品源码（native/），(c) 既有运行 trace。**外网不可用**（搜索无凭据、Bing 兜底 ECONNREFUSED、raw.githubusercontent fetch failed），因此**无法在线核对 Source SDK / OthmanAba 的公开源码**；凡涉及引擎内部行为处，只用「本机二进制字符串 + 资产布局」给静态判断并标注未证。本节未构建、未运行、未改产品代码。

### 12.1 引用链：每个缺失名被谁要、要几次

| 缺失文件 | 候选路径 | 引用模型（均在本图 pak 内） | 实例数 / 实例 skin | 静态 miss 行 | mesh 真正引用的槽 | 产品会尝试的 (family,slot) |
|---|---|---|---|---|---|---|
| `TOOLSNODRAW` | `materials/models/props_product/toolsnodraw.vmt`、`materials/toolsnodraw.vmt`（**都不存在**） | `props_product/stairs001`（product） | 10 / {0} | 1 | [0,1] | (0,1) |
| `brick_pillar_003` | `materials/brick_pillar_003.vmt`、`materials/models/props_watergate/brick_pillar_003.vmt` | `props_watergate/brick_pillar_01、_02、_03、brick_pillar_len3_02、len3_03、len4_02`（sultry，6 个） | 14 / 154 / 78 / 14 / 17 / 6；skin {0,1}（`len4_02` 只 {1}） | 24 | 全 [0] | (2,0) ×6 |
| `cliff_wall_101a` | `materials/cliff_wall_101a.vmt`、`materials/models/props_forest/cliff_wall_101a.vmt` | `props_rocks/cliff_wall_10`、`cliff_wall_10_mirrored`（product） | 2 + 1 / {0} | 8 | 全 [0] | (1,0) ×2 |
| `tf_oak_tree01a` | `materials/models/props_foliage/tf_oak_tree01a.vmt`、`materials/tf_oak_tree01a.vmt` | `props_foliage/oak_l_01、oak_m_01、oak_s_01`（bagel） | 8 / 10 / 7；oak_l {2,3}、oak_m {0,2,3}、oak_s {0,2} | 15 | 全 [0] | (1,0) ×3 |
| `wood_debris1` | `materials/models/props_sultry/wood_debris1.vmt`、`materials/wood_debris1.vmt` | `props_sultry/healthkit_patch`（sultry） | 34；skin {1,2} | 1 | [0] | (0,0) |
| `watergate/wall028a.vtf`（VTF 缺、VMT 在） | VMT `materials/models/props_watergate/watergate_sky_buildings_02/wall028a.vmt`（sultry pak）→ `$basetexture=materials/watergate/wall028a.vtf` | `props_watergate/watergate_sky_buildings_02`（sultry） | 2 / {0} | 1 | [0..8]（9 槽全用） | (0,7) |

合计：**静态 miss 行 50 条**（sultry 26 / product 9 / bagel 15），覆盖 14 个模型；这些模型**全部**由本图 static prop 列表实例化，没有一条来自「装了但没摆」的资产（理由见 12.2 的 MDL 全量扫描）。

### 12.2 缺席证明与「变体缺失」

- **七图 pak 内 0 命中**：对五个 basename（`brick_pillar_003`、`cliff_wall_101a`、`tf_oak_tree01a`、`wood_debris1`、`wall028a`）扫七图 pak 条目表，每图命中 **0**（`probe_refs.py` §A）。
- **四 VPK 条目扫**（tf2_misc / tf2_textures / hl2_misc / hl2_textures）：`brick_pillar_003`、`cliff_wall_101a`、`tf_oak_tree01a` **全 0**；`wood_debris1` 只以 coalmine 版本存在（`models/props_coalmines/wood_debris1.mdl|.vvd|.vtx` + `materials/models/props_coalmine/wood_debris1.vmt` + tf2_textures 的 `.vtf`）→ 模型要的 `props_sultry` 路径没有；`wall028a` 在 tf2_* **0 命中**，hl2_* 只有无关的 `materials/wood/woodwall028a.*` 与 `materials/concrete/concretewall028a.*`（不是 `materials/watergate/wall028a.vtf`）。
- **没有「引用它们的未使用资产」**：对 VPK 全量 **16653 个 `.mdl` 条目**逐个解出 texture 引用名（`probe_vpkrefs.py` → `chain_refs.json`），对五个 stem 的引用次数 **均为 0**。→ 这 13 个引用模型全部来自七图实体，**不存在「缺文件但没人用」的情况**；缺失名也不是某张未加载地图的残留。
- **是「变体缺失」而非整族缺失**（说明是上游打包丢了单个变体，不是这类资产本身没有）：
  - sultry pak 里有 `materials/models/props_watergate/brick_pillar_001.vmt|.vtf` 与 `brick_pillar_002.vmt|.vtf`，**独缺 003**；六个模型靠 skin 表在 001/002/003 间切换。
  - bagel pak 里有 `tf_oak_tree01.vmt|.vtf`、`tf_oak_tree01b`、`tf_oak_tree01c`（外加 card 变体），**独缺 01a**。
  - sultry pak 的天贴图目录里有 `wall028.vmt` 兄弟版本（缺的是 `wall028a` 的 VTF）。
  - product pak 有 `cliff_wall_06/06_mirrored/07/07_mirrored/10/10_mirrored` 的模型文件，但**整个 pak 没有 cliff 材质**（材质在 VPK 的其它路径下）。

### 12.3 为什么是 14 条而不是 50 条：以 mesh 实际引用为准

产品上色只遍历 **mesh 真正引用的 materialSlot**，不是整张 skin 表：

- `native/src/main.cpp:7813-7814`：`std::set<std::uint32_t> slots; for (const auto& run : prepared.runs) slots.insert(run.materialSlot);`
- `native/src/main.cpp:7823-7825`：`familyCount = prepared.priority == ...WorldProp ? std::max(1u, prepared.skinFamilies.skinFamilyCount) : selectableSkinFamilyCount(...)`（静态道具会把**所有** family 都上色）
- `native/src/main.cpp:7826-7827`：`for (family...) for (const auto slot : slots)`；失败即 `traceFmt("gpu: entity paint unresolved path=%s family=%u slot=%u reason=%s")` 后 `continue`（:7831-7833）
- mesh→slot 的读法用产品自身常量复算：`model_loader.cpp:38` `kBodyPartStride=16`、`:60` `kMeshStride=116`、`:64-68`（`kModelNumMeshesOffset=72`、`kModelMeshIndexOffset=76`、`kModelNumVerticesOffset=80`、`kModelVertexIndexOffset=84`、`kModelStrideCandidates={148,140}`）、`:744-750`（bodypart 的 modelindex 相对 bodypart 记录）、`:793-799`（meshindex 相对 model 记录）、`:806-815`（逐 mesh 读 int32 `material` 并校验 `< textureCount`）。

结果（`mesh_slots.py`）：`healthkit_patch` [0]；六个 pillar 模型全 [0]；`watergate_sky_buildings_02` [0..8]；`stairs001` [0,1]；两个 cliff [0]；三棵 oak 全 [0]。**与缺失行求交**：

| 图 | 静态 miss 行 | 去掉「mesh 不引用」的 skin 表槽后 | 明细 |
|---|---|---|---|
| sultry | 26 | **8** | 6 个 pillar 各 (2,0) + `healthkit_patch` (0,0) + `watergate_sky_buildings_02` (0,7) |
| product | 9 | **3** | `stairs001` (0,1) + 两个 cliff 各 (1,0) |
| bagel | 15 | **3** | 三棵 oak 各 (1,0) |
| 合计 | 50 | **14** | |

两个推论：

1. **§4.4 的「sultry 13 ≈ 12+1」要改口径**：那 13 是 **skin 表行数**，不是产品尝试次数。mesh 过滤后 sultry 只有 8 条；被滤掉的 5 条（pillar fam0/fam1 的 slot2、fam2 slot2 等）是 mesh 根本不引用的槽——skin 表里有映射，但没有任何三角面用它。旧读数里 bagel 4 比新的 3 多 1，同理来自一条当前七图不生成僵尸模型的路径。product 的 3 与旧读数**精确一致**。
2. **反面验证（强证据）**：运行时 trace 里出现的 unresolved 组合与这 14 条**逐条对齐**——product 3 条、bagel 3 条，一条不多一条不少（见 12.4）。说明「静态 mesh 解 + skin 表」可复现产品的实际尝试集合，本节的口径可以当账本用。

### 12.4 运行时读数与可见性（对 §6.4 的更正）

历史 trace（**本次未复跑**；`.scratch/` 被 gitignore，属既有产物）：86 个 trace 文件含 `paint unresolved`。按 (model, family, slot) 聚合：

- `oak_l_01` / `oak_m_01` / `oak_s_01`：family=1 slot=0 各 8 次
- `stairs001`：family=0 slot=1，6 次
- `cliff_wall_10` / `cliff_wall_10_mirrored`：family=1 slot=0 各 6 次
- `c_ubersaw_xmas`：family=0/1 slot=2 各 72 次（N07 范围外，另账）

原句（注意 trace 是 UTF-8 含非法字节，`read` 会报 invalid UTF-8，需 `raw.decode('utf-8','replace')` 再正则）：

- `.scratch/final-frames/koth0-depth.trace:57`：`gpu: entity paint unresolved path=models/props_product/stairs001.mdl family=0 slot=1 reason=no candidate VMT for texture slot 1 resolved to a readable VTF`；`:58-59` 是同句的两个 cliff 模型。
- `.scratch/camera-ui/t50-camera.trace:77-79`：同句的 `oak_m_01` / `oak_l_01` / `oak_s_01`。

**可见性要按两个条件同时成立才算**（这是对 §6.4 的实质更正）：

1. slot 被 mesh 引用（否则产品不尝试，12.3）；
2. **实例当前 skin 落在缺失的 family 上**——产品虽然把所有 family 都上色（`main.cpp:7823-7825`），但绘制阶段是按 `(materialSlot, 实例 skin family)` 取纹理键：`native/src/native_renderer_entity.cpp:578-580` `textureKey(run.materialSlot, bucket.skinFamily)`，取不到就 `textureView=nullptr`；`native/src/native_renderer_frame.cpp:1186` `if (!range.textureView) continue;`。**未被实例 skin 取用的 family，其 unresolved 只进日志、不进画面。**

| 缺失组 | 产品会尝试 | 实例 skin | skin 是否取用该 family | 静态上是否可能落到画面 |
|---|---|---|---|---|
| `stairs001` TOOLSNODRAW | (0,1) | {0} | **是** | **是** |
| `watergate_sky_buildings_02` wall028a | (0,7) | {0} | **是** | **是** |
| 6 个 brick_pillar* | (2,0) | {0,1} | 否（fam2 不在实例 skin 里） | 否 |
| `healthkit_patch` wood_debris1 | (0,0) | {1,2} | 否 | 否 |
| 两个 cliff | (1,0) | {0} | 否 | 否 |
| 三棵 oak | (1,0) | oak_l{2,3}、oak_m{0,2,3}、oak_s{0,2} | 否 | 否 |

→ 因此 §6.4 的「14 个未解析模型里 13 个命中实例 skin」**不成立**，正确读法是：**产品会尝试 14 条，其中实例 skin 真正会取用的只有 2 条**（`stairs001` slot1、`watergate_sky_buildings_02` slot7）。cliff 与 oak 的 trace 之所以出现 family=1，正是因为产品会先给所有 family 上色——**trace 里的 unresolved 不等于画面缺图**，这一点在 §10 的抓帧判读里必须注意（否则会把日志当缺陷证据）。

可证伪预测（给下一个跑 sultry 的人）：sultry 开 trace 后应恰好出现 **8 条** unresolved —— brick_pillar_01/02/03、len3_02、len3_03、len4_02 各 `family=2 slot=0` 一条，`healthkit_patch` `family=0 slot=0` 一条，`watergate_sky_buildings_02` `family=0 slot=7` 一条。多一条或少一条都说明本节的 mesh/skin 解有偏。

措辞保留：抓帧仍未做；`bucket.skinFamily` 的确切取值规则（`effectiveSkinFamily` 对静态道具是否夹到 0）没有逐行读死，所以「2 条可见」是静态结论，需一次抓帧或一条带 skin 的日志确认。

### 12.5 `TOOLSNODRAW` 的回退语义（静态判断）

**资产侧**：`stairs001.mdl` slot1 的纹理名是 `TOOLSNODRAW`，按候选规则只试 `materials/models/props_product/toolsnodraw.vmt` 与 `materials/toolsnodraw.vmt`，**两条都不存在**。标准名 `materials/tools/toolsnodraw.vmt` **在**（`tf2_misc`、`hl2_misc` 都有；`shader=lightmappedgeneric`、`$basetexture=Tools/toolsnodraw`、`%compilenodraw=1`，VTF 在 `tf2_textures`），但它**不在候选路径上**——要回退到它，引擎需要额外一条「无路径名 → materials/tools/」的补全规则。

**二进制侧**（本机 320 个 PE/模块字符串扫）：`toolsnodraw` 只出现在 `hammer_dll.dll`；`engine.dll`、`client.dll` **0 命中**。`error.vmt` / `error.vtf` 在四个 VPK 里**都找不到**；`MaterialSystem.dll` 里能读到合成错误材质的路径字符串（`___error.vmt` 用 `UnlitGeneric` + `$basetexture error`，另有 `___flat.vmt`）。→ **静态证据不支持「引擎自动回退到 tools/toolsnodraw」**；若引擎真有硬编码特判，它不在本机可见的字符串/资产里，本卡无法证否（外网不可用，无法核对公开源码）。

**产品侧**（这一侧是确定的）：`native/src` 全仓对 `toolsnodraw` / `TOOLSNODRAW` / `error.vmt` / checker / `__error` **0 命中**；`material_slot_resolution.h:127-128` 唯一回退是 VMT 里的 `material.fallbackBaseTexture`；解析失败返回 `false` 且**绝不返回默认 slot**（注释写明 slot 0 就是曾把所有职业画红的那个猜测）。失败后该 (family,slot) 不产生 PaintedSlot → `native_renderer_entity.cpp:578-580` 得 `textureView=nullptr` → `native_renderer_frame.cpp:1186` **整段跳过**（源码注释明确写了不退世界图集，避免把资源失败伪装成正常绘制）。

→ **产品当前实现**：`stairs001` 的 slot1 子网格**被整段不画**（视觉上是缺面/洞），既不是灰白 `toolsnodraw`，也不是紫黑棋盘格，也不会串到别的贴图。这条判断是静态代码结论，可直接引用；引擎侧若真有回退，也与产品无关（产品不走引擎材质系统）。

### 12.6 结论分层补充（并入 §9）

**本节新增的已证事实**：

1. 五个缺失名在整个安装（四 VPK + 七图 pak）**都不存在**；且与它们同族的兄弟变体存在 → 丢失的是单个变体文件，不是整类资产。
2. 引用它们的 13 个模型**全部由七图 static prop 实例化**；VPK 全量 16653 个 MDL 对五个 stem **零引用** → 不存在「被未使用资产引用」的干扰项。
3. 产品真正会尝试的是 **14 条 (model,family,slot)**（50 条静态行经 mesh 过滤），其中 **product 3 条与 bagel 3 条已被运行时 trace 逐条证实**。
4. 双重条件（mesh 引用 ∧ 实例 skin 命中）下，静态上**只有 2 条可能落到画面**：`stairs001` (0,1)、`watergate_sky_buildings_02` (0,7)。§6.4 的「13/14 命中」据此更正。
5. `TOOLSNODRAW` 不是「文件在所以会回退」：候选路径上无文件、产品无特判、引擎侧可见字符串无回退证据；产品的确定行为是**跳过该子网格**。

**仍需运行才能判的（并入 §9.2 / §10）**：

1. sultry 三组（8 条）没有历史 trace；按 12.4 的预测表跑一次即可核对。
2. 「2 条可见」需要抓帧：`stairs001` slot1 应该看到缺面，`sky_buildings_02` slot7 应该看到该贴图缺失（若两者都正常，说明绘制阶段有本卡未读到的回退/覆盖）。
3. 引擎自身的错误材质外观（若产品走引擎材质系统则另论）本卡不下结论。

### 12.7 本节新增脚本（只读）

`probe_names.py`（缺失名 × 候选 × 引用模型汇总）、`probe_vpkrefs.py`→`chain_refs.json`（VPK 全量 16653 MDL 的引用扫描）、`probe_refs.py`（七图 pak basename 缺席证明）、`mesh_slots.py`（用产品常量解 mesh→materialSlot）、`n07_chain.py`→`chain.json`（实例数 / skin 分布 / 逐模型 origin 交叉）、`trace_agg2.py` / `trace_ctx.py`（历史 trace 聚合与上下文）。

---

## 13. 产物清单

| 文件 | 内容 |
|---|---|
| `.scratch/assets-n07-2026-11-18/n07lib.py` | 解析库（VPK/BSP/game lump/MDL/VMT/VTF） |
| `.scratch/assets-n07-2026-11-18/inv-maps4.json` | 七图实体 + prps 实例 + 每实例 skin + 唯一模型 |
| `.scratch/assets-n07-2026-11-18/ledger3.json` | 逐模型 × family × slot 的材质链账本（本卡主证据） |
| `ledger3.py / recon2.py / slots.py / absence.py / scout2.py / stats.py` | 复算脚本（只读） |
| 本文件 | 交付说明与分层结论 |
| `.scratch/assets-n07-2026-11-18/n07_chain.py` / `chain.json` | 七图实例数、实例 skin 分布、逐模型 origin（§12.1 交叉） |
| `.scratch/assets-n07-2026-11-18/probe_refs.py` | 五个缺失名在七图 pak 的缺席证明（basename 全 0） |
| `.scratch/assets-n07-2026-11-18/probe_vpkrefs.py` / `chain_refs.json` | VPK 全量 16653 个 MDL 的 texture 引用扫描（对五个 stem 零引用） |
| `.scratch/assets-n07-2026-11-18/mesh_slots.py` | 用产品自身常量解 `mstudiomesh_t.material` → mesh 引用槽（§12.3） |
| `.scratch/assets-n07-2026-11-18/probe_names.py` | 缺失名 × 候选路径 × 引用模型汇总 |
| `.scratch/assets-n07-2026-11-18/trace_agg2.py` / `trace_ctx.py` | 历史 trace 的 `paint unresolved` 聚合与上下文（§12.4） |
