# N14 小地图底图与独立标尺 - 2026-11-15

- 任务卡：`docs/PROJECT-NEXT-TASKS-2026-11-14.md` N14（L235-243），优先级 P2（§11 表）。
- 写域：本卡只读产品源码；本文件与 `tools/n14-oracle/**`、`D:/_shared/EXEC-2026-11-15/n14/**` 是本卡产物。
  **`native/src/main_hud.cpp` 未改一行**（lead 令：N12-A 落地前冻结，且 D41 必须与 gate 的 `shapes==13` / A2 区间同一次提交）。
- 基线：`candidate/e5`，HEAD `6dd853ef24494b6066e4624970ac75620d4863ea`。
- **抓帧用的那个二进制**（不是现在检出里的）：
  `native/build-nmake/tf2_demo_native.exe`，2,991,104 B，
  SHA-256 `b4a2355ecc0a9fe83978f77347b5b9a619b10a8b3a96746daefcddad8891fbdd`
  （`captures/identity.txt` 同值，`head=a901ecec…`）。
- 现在检出里的二进制：2,991,616 B，SHA-256
  `1c3580e278a160194783ec2c96d3bd622a5fe10ee2b5b70bafbea3295af0797e`，mtime 2026-11-14 19:22:35。
  两者不同，所以**七份 trace 不是 HEAD 的 exe 跑出来的**。能确证的范围到这里为止：
  `git diff --stat a901ece 6dd853e -- native/` 只有 `native/src/main.cpp | 297 ++++ 12 ----` 一个文件，
  而 `native/src/main_hud.cpp`、`native/include/minimap.h`、`native/include/hud_panel.h`、
  `native/src/hud_panel.cpp` 在该范围**逐字节未变**——小地图面板的每一个读数字段都出自这几个文件。
  变的是相机（N12-A/N12-B），不是小地图。但没有用 HEAD 的 exe 重跑七图，所以
  「HEAD 的 exe 会打出同样七份 trace」**未验证**（§9 第 8 条）。

## 0 结论摘要

| 项 | 结论 | 证据 |
|---|---|---|
| "面板只有标记" | **实测成立**：七图面板 25,600 px = 1,264 边框 + ~24,070 背景 + 250~300 标记，结构像素 **0** | `n14_panel_audit.py`，§1 |
| 授权 overview 图 | **不可得**：本机安装内七图一张都没有（四条独立路径） | §2 |
| demos.tf 源码复用 | **无已记录的版本/许可证**，路线图声称的报告不存在 → 选原生实现 | §3 |
| 投影独立标尺（D43）tier1 | **54 PASS + 2 UNSUPPORTED + 0 FAIL**（56 项）。那 2 项是两个单控制点 KOTH 图的等比检查：只有 1 对点，相对离散恒等于 0，判 PASS 就是无条件通过——本轮把它如实改成 UNSUPPORTED | `verify-trace-7.json`，§5.3 |
| 投影独立标尺（D43）tier2 | **按名字全中**：七图 26 个控制点，BSP `team_control_point` 原点与产品打印的世界坐标最差 0.01 u。**只证明名字对得上原点，不证明朝向** | 同上，§5 |
| **D43 的真实分母** | 抓帧 **7/7**；名字对 BSP **7/7**；**朝向被钉住 6/7**（`koth_bagel_rc11` 的 X 轴翻镜像位移 0.000 px，不可判）；轴拉伸检查非空 **5/7** | `mirror-sensitivity.json`，§5.2 |
| 像素级独立标尺 | **七图全 PASS**：26 条控制点线，oracle 像素与产品打印值差 ≤0.01 px，且该位置确有对应颜色的像素。**只覆盖产品自己打印的那 26 个点**；`koth_bagel_rc11` 只有 1 个点，把面板水平镜像同样 PASS | `pixel-oracle.json`，§6 |
| 裁剪是否公允 | pl_upward_f12 的 43.4% "界外"**是 3D 天空盒，不是缺陷**（本卡推翻自己早先结论）；**cp_sunshine 11.75% 已知未修，性质 NOT VERIFIED** | `skybox-aware.json`，§4.1 |
| 底图（D41） | **未实现**。本文件给设计、矩形预算与验收口子，不含代码 | §7 |
| 人类可识别性 | **未验**（机器可区分 ≠ 人认得），`side-by-side.png` 是给人看的那张表 | §9 |

## 1 面板里到底有什么（像素级）

`tools/n14-oracle/n14_panel_audit.py` 自己解 24-bit BMP（不用 PIL，不 import gate 的 classify），
把面板每一像素归入 背景 / 边框 / 标记（距某个投影标记 ≤7 px）/ 结构（其余）。判据：结构像素为 0 即 `MARKERS-ONLY`。

| 地图 | 背景 | 边框 | 红 | 蓝 | 中性 | viewer | **结构** | 判定 |
|---|---:|---:|---:|---:|---:|---:|---:|---|
| cp_process_f12 | 24068 | 1264 | 99 | 99 | 52 | 18 | **0** | MARKERS-ONLY |
| cp_sunshine | 24084 | 1264 | 100 | 100 | 52 | 0 | **0** | MARKERS-ONLY |
| cp_gullywash_f9 | 24064 | 1264 | 101 | 101 | 52 | 18 | **0** | MARKERS-ONLY |
| cp_sultry_b8a | 24063 | 1264 | 103 | 102 | 52 | 16 | **0** | MARKERS-ONLY |
| koth_product_final | 24266 | 1264 | 0 | 0 | 52 | 18 | **0** | MARKERS-ONLY |
| koth_bagel_rc11 | 24266 | 1264 | 0 | 0 | 52 | 18 | **0** | MARKERS-ONLY |
| pl_upward_f12 | 24114 | 1264 | 0 | 0 | 204 | 18 | **0** | MARKERS-ONLY |

读法：cp_sunshine 的 viewer 0 px 是因为该图固定相机（eye 0,-2048）本来就在声明矩形之外（`viewerInside=0`），
产品不画越界点是对的；pl_upward_f12 的 neutral 204 = 4 个环 × ~51 px。
这与 `docs/D08-MINIMAP-SEVEN-MAPS-2026-11-14.md` 的形状计数表（Upward 13、五图 14、单点图 10）互相印证。
**任务卡那句"现在面板只有标记，不得称底图已实现"，到此有了像素证据，不再是从源码推的。**

## 2 授权 overview 图：四条路径全否

1. `D:/SteamLibrary/steamapps/common/Team Fortress 2/tf/resource/overviews` 目录不存在（glob os error 2）。
2. `**/overviews/**` 全 `tf/` 通配返回 0 个文件。
3. 全安装逐字节扫描：`overviews` 字串只出现在 `tf2_misc_dir.vpk@5190647` 与 `tf2_textures_dir.vpk@1569076`，
   都是同一组遗留 VGUI 精灵 `materials/overviews/{tc_hydro,caparrows,redteam,blueteam}`，与七图无关。
4. 七个 BSP 自带的 pakfile lump（lump 40）分别 68 / 87 / 102 / 90 / 41 / 89 / 115 B，无 `PK\x03\x04`，装不下任何位图。

=> "有授权底图就贴图"这条路在本机安装上**不可实现**，不是偏好问题。
这也独立复核了产品注释 `native/src/main_hud.cpp:824-827`（"none of the seven supported maps ships a radar overview"，引 T135 §4）。

## 3 demos.tf 源码许可证：没有可复用的依据

任务卡要求"核查 demos.tf 的已记录源码版本、许可证与复用结论"。核查结果：

- 全树只有 `docs/G3-N00-N02-N03-2026-11-14.md:103-132` 提到 demos.tf，讲的是**录像素材**
  （api.demos.tf 搜 pl_upward_f12 只有三条 `_auto-` 且 client 仍是 "SourceTV Demo"），不是小地图源码。
- 路线图 `docs/PROJECT-ROADMAP-HANDOFF-2026-11-12.md:422` 把"Inspect demos.tf minimap source and license/provenance"
  勾成已完成、`:580` 把 R5-1 列为"源码/许可证/来源报告；复用决定"——**但没有任何对应的报告文件存在**。
- 全 docs 搜 LICENSE / 许可证 / MIT / GPL / Apache，没有一条 demos.tf 的许可证文本。
- 已记录的规则只有政策本身（`docs/PROJECT-HANDOFF-2026-11-14.md:176-180` 第 8 条）：
  "只复用来源、许可证、依赖、数据格式、集成成本都已确定的组件；更安全或更简单时选原生实现；不要因为视觉效果有用就照抄。"

**决定：原生实现。** 理由不是"我想自己写"，而是（a）没有可确定的许可证与版本可供复用，
（b）产品已有能解析 LZMA 分块 BSP 的 reader，底图数据本来就在内存里（§8.1），
（c）Win32/D3D 侧只需要 Rect+Circle 两种图元，没有任何算法需要从外部借。

## 4 七图测量总表

源：`D:/_shared/EXEC-2026-11-15/n14/{ruler,basemap,floors,coalesce,skybox-aware}.json`。

| 地图 | 声明矩形长宽比 | 比例 px/u @160 | 地面填充% | 界外地面%(未去天空盒) | **可玩地面界外%** | 边线 run | 合并后 Rect(C) | 层带>2% | 叠层格占比 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| cp_process_f12 | 1.0972 | 0.015823 | 20.0 | 0.00 | 0.00 | 906 | 434 | 6 | 0.062 |
| cp_sunshine | 1.0500 | 0.014881 | 30.8 | 6.51 | **11.75** | 1074 | 522 | 9 | 0.088 |
| cp_gullywash_f9 | 1.3038 | 0.015379 | 27.8 | 1.14 | 0.87 | 479 | 322 | 6 | 0.283 |
| cp_sultry_b8a | 0.8000 | 0.013333 | 35.0 | 0.00 | 0.00 | 387 | 254 | 6 | 0.243 |
| koth_product_final | 0.8216 | 0.017007 | 16.8 | 0.00 | 0.00 | 411 | 210 | 6 | 0.085 |
| koth_bagel_rc11 | 1.4457 | 0.018797 | 24.9 | 4.09 | **2.78** | 507 | 363 | 9 | 0.102 |
| pl_upward_f12 | 1.0000 | 0.022321 | 42.0 | 43.40 | **0.00** | 921 | 482 | 8 | 0.086 |

### 4.1 裁剪公允性——本卡推翻了自己的一条早先结论

早先把 pl_upward_f12 的 43.4% 界外地面判成"真裁剪缺陷"。**这是错的**，任务卡那句
"世界包围盒大于可玩区域不自动等于错误"正好套在这里。证据（`skybox-aware.json` + `n14_upward_skybox_test.py`）：

- 界外那一整块 area = 33,146,533 u²、157 个多边形、bbox = [5120,-4096,9216,0]，
  它 / 主块面积 = **256.000 = 16² = sky_camera scale**，bbox 恰好 4096×4096 且以 sky_camera (7168,-2048) 为中心。
- **pl_upward_f12 的可玩地面界外量 = 0.00 u²。** 它的声明矩形就是可玩块 [-3072,-4096,4096,3072]。

其余地图同样按"簇 bbox 是否包含 sky_camera 原点"逐簇判定后：

- cp_sunshine **11.75%** 是真界外：其中 4,632,576 u² 一块（bbox [-14892,12636,-14060,15420]）装着 12 个
  `info_player_teamspawn` + 4 个 `prop_dynamic`，与 cp_process_f12 在同一坐标上的竞训覆盖区一致（0 u² 在矩形内），
  另有约 2.63 M u² 的主簇从 minX -11008 / maxY 10368 探出。
  **NOT VERIFIED：4,632,576 u² 区域的性质未定，需 (a) 该区连通簇在 BSP 里的 classname，(b) 是否被 `trigger_*` 覆盖。**
  这是唯一一张「已知未修」的裁剪项——**拿到 (a)(b) 之前不许写成缺陷，也不许留空。**
- koth_bagel_rc11 **2.78%**：一块 925,696 u²（7 个多边形）在 [-1152,4416,896,4928]，sky_camera (0,4352) 就在它下沿之外——大概率也是天空盒邻块，判不准，列为待查。
- 其余四图可玩地面界外 0.00–0.87%。

**判定规则（写清楚，免得下次再吵）**：界外质量必须先在 128 u 连通簇上聚，再按
(a) 退化细条（bbox 某轴为 0、1–2 个多边形）、(b) 3D 天空盒（簇 bbox 含 sky_camera 原点，或面积比为 scale² 且 bbox 为 scale×scale 的方形）、
(c) 覆盖/装饰区、(d) 真裁剪 四类分开。**只知道"有质量在界外"不足以判缺陷。**
本分类器已知局限：只能排除 bbox 含 sky_camera 的簇，cp_sunshine 有一块 twinB（3,281,663 u²，同高度带）
紧邻 twinA 却不含 sky_camera，因此被记成"可玩"——镜像测试（A 关于自身中心反射后与 B 相交）给出共享像素 0，它们是两块真几何，不是镜像副本。
按材质复核天空盒的那次尝试**未完成**（手写 face 读取把 SURFEDGE 索引直接当顶点用，`struct.error` 崩），这条仍算 UNANSWERED。

## 5 D43 独立标尺：算术过关，朝向只有六图

`tools/n14-oracle/n14_verify_trace.py` 只从**约定**推投影（等比 min()、居中、世界 +y 向下），
不读产品任何常量；产品表只当"声明矩形"输入。

- `--selfcheck`：把投影做三种变异（y 翻转、scale ×10、offsetX+6），**三种都必须被抓到**才 PASS → `mutation_not_detected: []`。
- **tier1：56 项 = 54 PASS + 2 UNSUPPORTED + 0 FAIL。** 每张图的 state 行都要：scale、offsetX、offsetY、
  viewerPx/Py、angle=(90-yaw)、等比（见 §5.2）、`viewerInside` 与 oracle 自己算的包含关系一致。
  那 2 项 UNSUPPORTED 是两个单控制点 KOTH 图的等比检查——**本轮把它从 PASS 改成 UNSUPPORTED**，
  原因写在 §5.2 开头：一对点的 (max−min)/max 恒为 0，那种 PASS 不依赖产品做对任何事。
- **tier2：七图全 PASS**，按 `minimap: cp name=` 的 targetname 去 BSP 的 `team_control_point` 原点比对：
  process 5 / worst 0.000 u；sunshine 5 / 0.01；gullywash 5 / 0.000；sultry 5 / 0.000；
  product 1 / 0.000；bagel 1 / 0.000；upward 4 / 0.000（共 **26 条**）。
  **这一条只证明「名字对得上原点」，不证明「朝向对」**：两个 KOTH 图各只有 1 个能按名打印的地标，
  1 个点定不了朝向。把 tier2 的 7/7 读成「七图朝向都验过」是口径放宽，§5.2 给出实测边界。
- 证据：`D:/_shared/EXEC-2026-11-15/n14/captures/<map>.trace`（七图各一份，含 cp 行）与
  `verify-trace-7.json`（sha256 `e157b7ba332cacab75f232f6c36555ae80d92f41916a260975f563e55dea4f1f`）。
  这份 JSON 本轮重新生成过（改动见 §5.3），旧版本是 `af8ec8f6…b66f`。

**这推翻了我先前"tier2 UNSUPPORTED"的结论**：那次之所以 UNSUPPORTED，是因为喂进去的 G1 日志本身没有 cp 行，
不是产品的问题。日志没有 cp 行 ≠ 产品不输出 cp 行（gate 的 A5 段一直用 `cp_line()` 读它）。

### 5.1 等比检查被我写错过一次（记在这里，因为它是一种通用错法）

第一版"等比"检查比较 `pw/spanX` 与 `ph/spanY`。面板是正方形而世界矩形不是，所以这两个值**按构造就不同**，
检查在七图里红了六张，却什么也没说明——它测的是我自己的 `min()` 调用。改成"用产品打印的像素测单一比例"：
每对控制点的像素距/世界距必须是同一常数（相对离散 <1%）。实测离散 0.0 ~ 3.12e-4。
**教训：一个只能以错误理由通过、不能以正确理由失败的检查，等于没有检查。**

单控制点 KOTH（product/bagel）本来没有"对"，就把 viewer 当第二个独立世界点加进去
（它的世界坐标来自采集命令的相机位姿，像素由颜色验证）。
**这里我原先写的是「两点定 scale/平移/翻转，样本更小但不是更弱的检验」——翻转那半句是错的**，
而且它正好给了一个无条件通过的检查背书。原句留在上面不删，因为它就是那种"听起来很有道理"的错法；
实测边界见 §5.2。

### 5.2 朝向到底钉住了几图（本轮新增测量）

`tools/n14-oracle/n14_verify_trace.py:182-187` 当时写着「两点定 scale/平移/翻转，所以样本更小但不是更弱的检验」。
**翻转那半句是错的**，脚本注释本轮已改写。错在哪：两个点 (w₁,p₁)、(w₂,p₂) 在镜像投影 p′ = P − p 下同样成立，
所以两点永远分不出镜像；两点只能定 scale 与平移。连带的第二个错是那段注释替等比检查背书，
而该检查在只有 1 对点时 `rel = (max−min)/max` **恒等于 0**——无条件 PASS，属 D55 那一类假绿。

所以把「朝向」单独测量：`tools/n14-oracle/n14_mirror_sensitivity.py`（本轮新写，RC=0，产物
`D:/_shared/EXEC-2026-11-15/n14/mirror-sensitivity.json`）。它不需要重放投影推导：
在 px = offX + x·s、py = offY − y·s 下矩形中心必然映到面板中心，所以把 X 轴翻过来产品会打印
px′ = pw − px，位移 |px′ − px| = 2·|x − cₓ|·s；Y 轴同理（py、ph、c_y）。
判据：只要**任一**被打印样本的位移 > TOL_PX = 0.05 px，该轴镜像就能被抓到（钉住），否则这批样本对它是盲的。
样本 = 产品自己打印的每个控制点（世界坐标取 `verify-trace-7.json` tier2 的 `log_world`）+ 观察者
（世界坐标 = 采集命令的 `--debug-camera 0 -2048 512 0 33`）。

| 地图 | 样本 n | 打印控制点 | 翻 X 最大位移 px | 翻 Y 最大位移 px | 只用控制点 X / Y | X 轴 | Y 轴 |
|---|---:|---:|---:|---:|---|---|---|
| cp_gullywash_f9 | 6 | 5 | 115.894 | 62.991 | 115.894 / 49.581 | 钉住 | 钉住 |
| cp_process_f12 | 6 | 5 | 112.025 | 68.861 | 112.025 / 68.861 | 钉住 | 钉住 |
| cp_sultry_b8a | 6 | 5 | 71.707 | 104.213 | 71.707 / 104.213 | 钉住 | 钉住 |
| cp_sunshine | 6 | 5 | 167.619 | 217.143 | 61.667 / 109.881 | 钉住 | 钉住 |
| pl_upward_f12 | 5 | 4 | 68.571 | 71.429 | 68.571 / 71.429 | 钉住 | 钉住 |
| koth_product_final | 2 | 1 | 53.095 | 69.660 | **0.850** / **0.000** | 钉住（仅靠 CP，余量 17×） | 钉住（**只靠观察者**） |
| koth_bagel_rc11 | 2 | 1 | **0.000** | 81.805 | **0.000** / 4.812 | **盲** | 钉住 |

三件事必须一起读：

1. `koth_bagel_rc11` 的两个样本（控制点 + 观察者）**都落在矩形的竖直中线上**（偏移都是 0），
   所以 X 翻不翻过来，产品打印的像素完全一样——0.000 px。这张图的「朝向对」在现有采集下**不可判**，
   不是「通过」。要判它必须换一个 |eye_x − cₓ| 够大的采集相机（使 2|Δx|s > 0.05），或换一张有多个可打印地标的图。
2. `koth_product_final` 的 Y 轴**只靠观察者**才钉住（控制点 Y 位移 0.000）；X 轴靠控制点，余量 0.850 px = 17 × TOL，
   够用但很薄。
3. 五张 CP 图的余量 61–218 px，来自它们自己的 5 个（upward 4 个）控制点；删掉观察者这些结论不变
   （`x_sign_detectable_without_viewer` / `y_...` 全为 true）。

**所以 D43 的真实分母是：抓帧 7/7、名字对 BSP 7/7、朝向钉住 6/7、轴拉伸检查非空 5/7。**
「七图全过」只对前两项成立；把后两项也并进 7/7 就是偷偷放宽口径（任务卡 D43 明令不许）。

### 5.3 本轮对 oracle 的改动（可复核）

| 文件 | 改动 | 前后差异 |
|---|---|---|
| `tools/n14-oracle/n14_verify_trace.py` | `:182-189` 删掉「两点定翻转」的错注释；`:198-207` 等比检查要求 ≥2 对点，1 对点报 UNSUPPORTED；`:244-247` summary 增加 `tier1_unsupported` 计数 | 旧：每图 8 项全 PASS；新：**54 PASS + 2 UNSUPPORTED + 0 FAIL** |
| `tools/n14-oracle/n14_mirror_sensitivity.py` | 新文件 | §5.2 那张表 |
| `--selfcheck` | 未改 | 三种投影变异（y 翻转 / scale ×10 / offsetX+6）仍全部被抓到，`mutation_not_detected: []` |

`verify-trace-7.json` 重新生成：旧 sha `af8ec8f6…b66f`，新 sha
`e157b7ba332cacab75f232f6c36555ae80d92f41916a260975f563e55dea4f1f`。
**只有这份 JSON 变了，七份 trace 一个字节没动**（本卡没有重跑产品 exe）。

## 6 像素级标尺（与 §5 分工不同）

`tools/n14-oracle/n14_pixel_oracle.py`：只吃 相机(eye,yaw) + 面板矩形 + 地图名 + 声明矩形，
先按约定算出该在哪里，然后**去真实帧里数像素**（5×5 窗口，需 ≥3 像素命中队色，容差 30/255）。
分工理由写在它 docstring 里：§5 查"打印的算术对不对"，这里查"画出来的像素在不在"，因为 compose 与 trace 若有同一个 bug 会互相印证。

结果 `pixel-oracle.json`（sha256 `de2db6c83c56c8cce49b24e2cba7c18b272933599fe24d628bc66bbb90f1d060`）：
**maps=7 pass=7 fail=0 unsupported=0 PASS**。26 条控制点线，oracle 与产品打印值差 ≤0.01 px，
且 oracle 位置上确有 ≥25 个对应队色像素。最差对比例离散 3.12e-4（pl_upward_f12），两个 KOTH 为 0.0。

两个自己踩的坑记在脚本注释里：(1) 自检画了**空心环**（产品画的是实心圆盘，因为 HUD shader 丢掉圆包围盒外半），
窗口落进洞里读到 0 px，让"正确"用例变红；(2) 上面 §5.1 那个错检查两边都犯过。

## 7 底图设计（D41，仅设计，未写代码）

### 7.1 数据从哪来——不需要内置每图线稿表

产品运行循环里已经有一份全保真的 `BspMap`：`native/src/main.cpp:4263` 的 `tf2::native::BspMap displayMap`，
由 `native/src/main.cpp:4710` 的 `BspParser::parse(mapBytes, displayMap, kWorldTriangleCapacity)` 填
（容量常量 `kWorldTriangleCapacity = 400000` 在 `:4709`），
`triangles / fog / staticProps / lightmap*` 都已在用。`BspTriangle` 带 material 字符串与 planeIndex，
所以"哪些面是地面"可以在产品内用与 oracle 相同的平面判据算出来。

反过来，`BspMap` **不带实体表**（`native/include/bsp_map.h` 到 brush-model 计数与 fog 就结束），
所以控制点原点只能继续用 `native/include/minimap.h` 里那张编译进来的表——这解释了那张表为什么必须存在，不是偷懒。

### 7.2 矩形预算（图元账，实测）

HUD 只有 Rect 与 Circle 两种图元（`HudPrimitiveKind Rect=0 Text=1 Circle=2`），没有线/多边形，所以线稿必须化成矩形。

| 策略 | 含义 | 七图 Rect 数 |
|---|---|---:|
| A 填充并集 | 填成实心块 | 191–580 |
| B 1 px 外轮廓并集 | 只画边界 | 合并后 91–409（均值 251.7） |
| C 外轮廓 + 每层带内边界 | 多楼层图不会塌成影子 | 合并后 **210–522**（均值 369.6） |

合并是"同宽 run 在相邻行纵向并",**像素精确**（`all_merges_exact=true`），最大 522 个 Rect = 3,132 顶点 = 137,808 B，
相对 `native/src/native_renderer_frame.cpp:1259` 的 `reserve(hudPrimitives_.size()*6u*11u)` 微不足道，且那里没有图元数量上限。

**推荐 C**：只有它在多楼层图上能显出楼层而不是阴影。地面质量分布支持这个选择——
按"主层带占比 <0.85 即多层"的判据，**七图全是多层**（主层带占比 0.31~0.78），
pl_upward_f12 最极端：主层带只占 31%，有 5 个占比 >5% 的层带，且是唯一带 `team_train_watcher`（推车）的图。

### 7.3 层（floor）策略

- 分带：z 桶宽 256 u（与实测一致），每桶算地面面积；面积占比 >2% 的桶算"一层"。
- 画法：主层带用填充+轮廓，其余层带只画轮廓（避免层与层互相盖住）；
  同一 xy 上有多个层带的格子（叠层格占比 6.2%~28.3%，gullywash 与 sultry 最高）必须按层带分别描边，不能只填一次。
- 不做"高度着色"：面板只有一种中性色可用，做成深浅会与队色标记抢注意力，且 gate 的亮度比对会被改写。

### 7.4 缩放/布局

- 现状：绘制路径调 `native/include/minimap.h:332` 的 `minimapPanelProjection(bounds)`
  （该行注释写明它就是 `makeMinimapProjection(bounds, 1, 0, 0)`），永远是"整块矩形塞满 160 px"，
  实测 0.0133~0.0223 px/u（1 px ≈ 45~75 世界单位）。
- 纯逻辑半边 `makeMinimapProjection(bounds, zoom, panX, panY)` 已经支持 zoom/pan，
  但产品画图那一半没接——**所以缩放是接线任务，不是设计未知**。
- 面板 160 px 固定在后备缓冲像素（`native/include/hud_panel.h:131-151` 明确不是 DIP），
  窄窗口时宽度会被 `usableWidth` 夹到 `w != h`，所以线稿 buffer 必须用 `rect.w/rect.h` 而不是假设 160×160
  （gate 的 A2 现在写死 160×160，窄窗口用例没覆盖到）。

### 7.5 观察者 / 玩家 / 队伍 / 目标 / 推车 的区分

| 角色 | 今天 | 数据在哪 | 结论 |
|---|---|---|---|
| 观察者(自己) | 绿点 + 3 个朝向点 | `minimapFrame` 相机位姿 | 有，保持 |
| 目标点 | 队色圆盘 | `native/include/minimap.h` 内置表 | 有，保持 |
| 其它玩家 | **完全没有** | `native/include/entity_overlay.h:221-259` `OverlayEntitySample`（isPlayer/origin/team/class） | 可做，但必须带 **stale 标记**：`resolveOverlaySnapshot` 会报 ExactTick/CheckpointFallback/Unavailable，参考录像实测最差 1057 tick（16 s）。没有 stale 标记的"玩家点在动"就是编造 |
| 推车 | **没有** | 只有 `pl_upward_f12` 有 `team_train_watcher`；它不是控制点 | 数据缺就不画。任务卡要求"数据缺失时不许猜图标"，这条现在明确是"缺" |
| 队伍区分 | 控制点按 2→红 3→蓝 其它→中性 | 产品表 | 有；注意 BSP 实体的 `team_control_point` 全带空 `TeamNum`，队伍色只能来自产品表，不能读实体 |

### 7.6 普通用户的开关（今天只有命令行）

- 今天唯一入口是 CLI `--hud-minimap`（`native/src/main.cpp:1996` 声明 `hudMinimapCli`，`:2547` 解析置位，`:12679` 消费）。
- 要做产品内开关需三处：`native/include/native_ui.h` 的 `UiControlId` **在 Count 之前**加 id、
  `UiSettings`（`:212-219`，现在只存 standardQuality/fov/vsync/volume/tfRoot/recentDemos）加一个持久字段、
  `UiCommand`（`:191-210`，已有 ToggleHud/ToggleMute 这类）加一条命令；读写走 `writeUiInt` 那套。
- 另有 `TF2_NATIVE_TRACE` 式的环境变量先例（`native/src/main_trace.cpp:84`，故意用 `GetEnvironmentVariableW`），可作为临时旁路，但不是"普通用户 UI"。

## 8 改动会让哪些 gate 变红（必须同一次提交）

`minimap-hud-check.sh`：

| 位置 | 今天 | 加线稿后 |
|---|---|---|
| `:567` | `shapes == 13`（注释写死"1 背景 + 4 边框 + 4 环 + 1 观察者 + 3 朝向点"） | 必须改成按图计算：13/14 + 线稿 Rect 数（Upward 13 + 482） |
| `:609-610` | `20000 <= inside <= 25600` | 线稿几乎填满面板，下界要往上提（否则"只画了标记"也过） |
| `:605-607` | 变化像素 BBOX == 面板矩形 | 仍然成立，不用改 |
| `:613` | `outside == 0` | 仍须成立（线稿不得溢出面版） |
| `:830` | **未列表地图**（koth_bagel_rc13）必须 `shapes == 0` 且帧与不加开关逐字节相同 | **这条会和新设计正面冲突**，见下 |
| `:858` | 自测帧 `inside == 25600` | 合成帧，不受影响 |

**A11 的冲突（必须先想清楚再写代码）**：koth_bagel_rc13 这个 BSP 是能解析的——它只是没有测量过的声明矩形。
如果线稿走"有 `BspMap` 就画地面"这条路，未列表地图就会画出底图，A11 立刻红。
所以线稿的准入条件必须是**继续拴在 `minimapFindMapRow(frame.mapName)` 上**（即"这张图有测量过的矩形"），
而不是"我手上有没有几何"。这条约束不写下来，D41 很容易第一次提交就踩红。

另外 gate 的 A2/A3 都只在 1280x720 下跑，窄窗口（`w != h`）没有用例——这与 N15 的窗口用例是同一条缝。

## 9 还没验的（宁可写在这里，不写进结论）

1. **人类可识别性**：像素差能区分 ≠ 人认得。`side-by-side.png`
   （sha256 `19f1a423a3b485df314569a6e278fd50a5536f57605b9fcc87a1167a38c7e919`，984×3444）
   左列是**真实抓帧裁出的产品面板**、右列是提案线稿（BSP 地面多边形，同一相机、同一矩形）。
   机器可区分性的负控制：抖动 1 px 0.0764 / 加粗 1 px 0.0826 / 8 px 降采样 0.1035 → 上限 0.1035；
   21 对地图全部高于它，最接近的一对 gullywash|product 0.2264~0.2327（2.2 倍余量）。
   **这仍然只是一条"数字上可分"的证据，测试对象是人。**
2. **材质复核天空盒**：崩了，未完成（§4.1）。
3. **koth_bagel_rc11 那 2.78% 界外**：是天空盒邻块还是真裁剪，判不准。
4. **缩放/平移/点击**：产品侧没接，未验。
5. **推车图标**：数据缺，未做（这是"不许猜"而不是"没来得及"）。
6. **窄窗口/DPI 下的面板**：未验（归 N15）。
7. **cp_sunshine 的裁剪**：11.75% 界外，**已知未修**。NOT VERIFIED：4,632,576 u² 区域的性质未定，
   需 (a) 该区连通簇在 BSP 里的 classname，(b) 是否被 `trigger_*` 覆盖。
   没改的理由：改声明矩形会动 `native/include/minimap.h` 的表，属共享写域。
   建议：改表前先用 `n14_skybox_aware.py` 重跑确认，并同步 D08 的 205 项回归。
8. **HEAD 的 exe 会打出同样的七份 trace 吗**：抓帧用的是 `b4a2355e`，现在检出里是 `1c3580e2`。
   `a901ece..6dd853e` 之间 `native/` 只有 `native/src/main.cpp` 变（297+/12-），四个小地图相关文件逐字节未变，
   但**没有用 HEAD 的 exe 重跑七图**（产品 exe 要在 hostlock 下跑，本卡只读）→ 未验证。
9. **koth_bagel_rc11 的朝向**：X 轴镜像位移 0.000 px（§5.2），现有采集下不可判。
   所以它的「镜像没发生」是 NOT VERIFIED，不许写成 PASS。

## 10 复现

```bash
PY=C:/Users/Administrator/.dsh/dsh-runtimes/dsh-primary-runtime/dependencies/python/python.exe
# 1) 七图各跑一次静默抓帧 + trace（走 hostlock）
bash /d/TF2_Native_Worktrees/_shared/hostlock.sh run ui-d-n14-cptrace bash tools/n14-oracle/capture-cp-traces.sh
# 2) 算术标尺（含自检：三种投影变异必须被抓到）
$PY tools/n14-oracle/n14_verify_trace.py --selfcheck
$PY tools/n14-oracle/n14_verify_trace.py native/include/minimap.h D:/_shared/EXEC-2026-11-15/n14/captures/*.trace \
    --maps-dir "D:/SteamLibrary/steamapps/common/Team Fortress 2/tf/maps" \
    --maps-dir "D:/SteamLibrary/steamapps/common/Team Fortress 2/tf/download/maps" \
    --out D:/_shared/EXEC-2026-11-15/n14/verify-trace-7.json
# 3) 像素标尺 —— 只在真实帧上数颜色
$PY tools/n14-oracle/n14_pixel_oracle.py --captures D:/_shared/EXEC-2026-11-15/n14/captures \
    --out D:/_shared/EXEC-2026-11-15/n14/pixel-oracle.json
# 4) 面板成分审计（判 MARKERS-ONLY）
$PY tools/n14-oracle/n14_panel_audit.py --captures D:/_shared/EXEC-2026-11-15/n14/captures
# 5) 给人看的那张表
$PY tools/n14-oracle/n14_sidebyside.py
```

产物清单（都在 `D:/_shared/EXEC-2026-11-15/n14/`）：
`ruler.json` `containment.json` `rect-clip.json` `skybox-aware.json` `floors.json` `landmarks.json`
`basemap.json` `coalesce.json` `budget.json` `strategy-runs.json` `identity.json`
`verify-trace-7.json` `pixel-oracle.json` `side-by-side.png` `linework/contact-sheet.png` `panels/contact-sheet.png`。

> D53 提醒：本目录所有 shell 只用 `cut`/`tr` 提字段，不再经 `awk`——同一份字节在 worktree awk 与 Git awk 下会给出相反结论。

## 10.1 三个同类缺陷类别在本卡脚本上的自查

lead 点名要查三类（awk 数字/字符串比较、字段生产者根本不打印、采样密度变化）。逐条落到本卡脚本上：

| 类别 | 怎么查的 | 结果 |
|---|---|---|
| awk 数字 vs 字符串（D53 族） | `grep -rn awk tools/n14-oracle` | **0 处 awk**。`capture-cp-traces.sh` 只用 `sha256sum \| cut \| tr`；两个 oracle 与面板审计全是 Python |
| 引用生产者不打印的字段（D56 族） | 把 `verify-trace-7.json` 里每个被检查字段回查 `minimap: hud state=` 那行的字段清单 | 7 个被检查字段（scale / offsetX / offsetY / viewerPx / viewerPy / angle / inside）都在同一行里；`edge=`、`cps=`、`shapes=` 没被我当判据 |
| 采样密度改变读数（D59 族） | 面板审计与像素 oracle 都按投影位置取样，投影随 scale 变 | 本轮**没有改采集参数**（`identity.txt` 的 `camera` 七图同一串）→ 读数未受影响；但若换相机，`n14_pixel_oracle.py` 的 5×5 窗口结论必须重跑，不能沿用 |

第三类的具体机制写清楚：像素 oracle 的判据是「投影位置 5×5 窗口里 ≥3 像素命中期望颜色」。
scale 越小（sunshine 0.0149、sultry 0.0133）标记画得越小，窗口越容易落空。所以它给的是
**「这七张图 × 这一次相机位姿」的结论**，不是「任何位姿都成立」。
再加一条同类风险：`verify-trace-7.json` 的 tier1 只比对产品**自己打印**的数与 oracle 的数，
若 trace 的打印与绘制共用同一个 bug，两边会一起错——所以 §6 的像素级检查是必需的，不是重复劳动。

## 11 本文件没有证明什么

按 lead 的口径逐条列出这张纸**没有**证明的东西（能证明的都带了 file:line 或实测数字）：

1. **没有证明底图已实现。** D41 一行代码没写，§7 是设计与预算，不是完成度。
2. **没有证明 HEAD 的 exe 会打出同样七份 trace**（§9 第 8 条）。能证明的只是小地图相关源文件没变。
3. **没有证明 koth_bagel_rc11 的朝向是对的**：X 轴镜像不可判（§5.2、§9 第 9 条）。
4. **没有证明人认得这张图。** §9 第 1 条的负控制（抖动 1 px / 加粗 1 px / 8 px 降采样后仍可分）测的是像素数，不是人。
5. **没有证明 cp_sunshine 那 11.75% 界外是缺陷，也没证明它不是**；现有分类器只能排除 bbox 含 sky_camera 的簇。
6. **没有做材质层面的天空盒判定**：手写 face 读取崩了（`struct.error`），那条仍是 UNANSWERED。
7. **没有证明窄窗口下的面板形状**：本文件只承认 `usableWidth` 会夹宽（§7.4），实测在 N15。
8. **没有证明「26 个控制点」这个分母强度一致**：5 张 CP 图各 5（upward 4）个可打印地标，
   2 张 KOTH 图各 1 个 + 观察者。同一句「26 条全中」里最厚与最薄差一个数量级，§5.2 已摊开。
9. **没有证明 gate 的 A11 冲突已解决**：§8 写的是「准入条件必须继续拴在 minimapFindMapRow 上」，
   那是约束，不是验证——没写代码就没验证。
10. **没有证明跨机器 / 多次运行的确定性**：七份 trace 是本机、本安装、固定 `--debug-camera` 的一次结果，
    没有跑第二次比字节。
