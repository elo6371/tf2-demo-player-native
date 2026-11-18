# TF2 原生渲染器 · 世界可见性修复完整报告（2026-11-17）

**一句话：**「缺面」不是一个 bug，是六个彼此独立的问题叠在一起。其中两个直接造成你看到的黑洞：
天空盒被画质预设关掉（有几何的地方露出清屏色），以及位移面网格走法转置（相邻地形块之间真的裂开）。
两个都修了，合并树上四台机位、九张地图的孔洞读数归零或降到底，余下的像素全部有归因。

- 工作树：`D:\TF2_Native_Worktrees\t105-integrate`，分支 `work/integrate`，tip `3ef31ab0`
- 基线：`candidate/e5` = `cd04373f`
- 二进制：`native/build-nmake/tf2_demo_native.exe`，`sha256 9d8333d8ff40f4cadb2911433e4351c85f172334bac6402eae18198678bf7e86`（两次干净重建同 hash）
- 规模：21 个提交，67 个文件，+9,521 / −239
- 技术记录：`docs/WORLD-VISUAL-INTEGRATION-2026-11-17.md`、`docs/WORLD-HOLES-2026-11-16.md` §12–14、
  `docs/WORLD-DISP-PARAM-2026-11-17.md` §1–10、`docs/WORLD-VISUAL-REPAIR-2026-11-16.md` §6

---

## 1. 缺陷全景

| # | 缺陷 | 用户能看到的现象 | 状态 |
| --- | --- | --- | --- |
| A | 天空盒由画质预设推导，`preset` 缺省 0（Performance）= 天空不画 | 本该是天空的地方是清屏色（近黑），像地板/墙被掏空 | **已修**，四姿势归零 |
| B | 位移面（displacement）网格按转置走法采样 `dispvert` | 地形是镜像的；相邻位移面接缝真的裂开，地板/楼梯缺失 | **已修**，接缝 3,220→7,502 对 |
| C | 实体网格被二次队色染色 | 带贴图的道具被染成红/蓝、发灰 | **已修**，像素级证明 |
| D | 未知 `--` 开关只警告不拒绝 | 参数拼错也照跑，跑出错误配置的画面 | **已修**，exit 13 |
| E | 没有小地图，没有名单 HUD | 只有画面，没有任何读数 | **已修**，213 条断言 |
| F | 屏幕空间背面测试把单面几何剔除掉 | 某些角度岩石/墙体少一块 | **已定位，未修**：先于本次工作存在，需改剔除，单独立项 |

合并时又撞出两处红（H/I），已在合并树上修正：`--window-size` 被新的严格参数规则误拒；
天空门的断言 C 在位移修好后必然失败（它钉的是一个已被修掉的孔洞人口）。

---

## 2. 根因与定位

### A. 天空盒被画质预设关掉（这轮最大的发现）

现象不是「少几何」，而是**几何在、只是没人画天空**。定位链：

1. 用 4 连通 + 不与边框相连的定义量「被几何包围的 clear 色像素」：`pl_upward_f12` tick0 **8,976 px（9.13%）**、
   tick1800 **2,072 px**、`cp_process_f12` **985 px** —— 三个机位都不是 0，「只是暗不是洞」被否掉。
2. 对 tick1800 最大连通块抽 400 px 打 `--world-hole-trace`：**400/400** 命中同一个世界面
   **13233**（COALMINES/BLENDGROUNDTOWALL_COALMINES），`drawn=-`，它后面的天空面 9947 也没画 ⇒ 落到 clear。
3. 逐面审计 13233：平面 4482 法线 (0, 0.049939, 0.998752)、`planeSide=0`（正面朝上），相机在正面一侧 +44.98，
   但 32 个三角形质心 z 从 1.3 到 438.7、14 个在相机（z=243.07）之上 ⇒ 梯田状陡坡，相机站在台阶之间从下方看底面，
   **剔除是合法的**；`keep-map-winding` A/B 只填 765 px 且新挖 75 px，完全不碰这个面。
4. 于是换思路：`--no-world-cull` 能填 2,063/2,072（三机位零新洞）⇒ 几何存在。那为什么背面剔除关掉就能看见？
   因为该像素背后**没有别的 pass** 来兜底 —— 如果天空正常画，它会被天空填上。顺着这个怀疑查天空：
   trace 里 `reason=skybox draw disabled by the caller`；`settings_store.cpp:61-63` 写的是
   `result.render.skybox = standardQuality`，而 `settings.ini` 的 `preset` 缺省 0 = Performance ⇒ **天空一直是关的**。
   所有旧足迹用的 settings 都是 preset=0，所以历史上没人见过天空。
5. 反证：强制 `--world-sky` 后 `tick1800 2,072/2,072`、`cp_process 985/985` 全部被天空画上（约 RGB(106,138,162)）。

**判决（产品级，不是画质选项）：**天空属于正确性，不属于画质。12 个三角形、6 个面、1 次 draw，成本可以忽略，
而新装用户默认看到破画面是产品缺陷。可见性只剩三个条件：`geometry==All`、`debugView!=2`、未被显式关闭。
优先级 CLI > ini 显式 `skybox` > 默认 true。旧预设推导整条撤掉。

### B. 位移面网格转置

引擎的走法是：`+i` 沿 quad[0]→quad[1] 步长 `size`，`+j` 沿 quad[0]→quad[3] 步长 1，
平面索引 `i*size + j`（`engine/disp_mapload.cpp`、`utils/vbsp/disp_vbsp.cpp`，见调研笔记 §Q2b）。
我们原来是反的——位移面的四个角和平铺的 patch 在两种走法下**看不出区别**，所以单独看一个面永远发现不了；
但 `dispvert` 采样点落到了转置后的格子上，整个 patch 等于沿对角线镜像：凸起是反的，而且每个与邻居缝合的边界
顶点都落在错误的格点上 ⇒ 接缝开裂、楼层/楼梯缺面。

修法：`native/src/bsp_map.cpp` 位移块按引擎走法重写（38 行）：`+i` 逐行、`+j` 内层、平面索引 `i*size+j`，
alpha 插值与几何共用同一套权重。`usable` 判据（面积/边长溢出）保留：任一格失败整面回退成平 quad。
那 **13 个 patch 之前就是这样被画平的**，属于用户可见的几何错误，不只是计数变化。

### C. 实体二次染色

带贴图的实体网格在染色 pass 里又被乘了一次队色。修法：已绘制的网格画白，队色只在无贴图时兜底。
证明是像素级的：mask 379,038 px，normal 分支对原始 texel `withinTol=379038 / exceed=0 / maxDiff=0`，
mutation 反转时 `exceed=379038 / maxDiff=184`（罪证点 (982,609) normal=(255,174,74) vs mutation=(71,90,64)）。

### D. 未知开关不致命

`--window-sizee` 这种拼错只打警告、照常开机。改成按名字拒绝：exit 13、不写帧。
这条规则后来在合并时反咬一口（见 H）。

### E. 小地图与名单 HUD

250×250 面板钉在右上角（12 px 边距，用户决定 m08430），底图从 `.bsp` 自带的 basemap 资源来（`cp_process_final` 精确匹配 1835×1103，
其余 LEGACY 回退），红蓝名单按 class→name 稳定排序，`M` 切总览，静音改到 `Ctrl+M`（`]` 仍是一步音量）。

### F. 未修：单面几何的绕序剔除（pr600 那 94 px）

`cp_process` tick600 位姿剩下 453 px 被包围的缺失，拆账：347 px 后面没有实心面、12 px 只有背面、
**94 px 是真几何被屏幕空间背面测试丢掉**。罪证行 `main.cpp:11097 const bool faces = area > 0.0f`
（受 `drawState.worldCullBackfaces` 控制）；`--no-world-cull` 填 81/94，`--no-world-vis-clip` 填 0。
80/94 在位移修复前的帧里逐字节相同 ⇒ **先于本次工作存在**，与参数化无关，要改的是剔除而不是网格。

---

## 3. 修复实现（合并树上的提交）

```
56659c81 fix(renderer): decouple the sky box from the quality preset (W-1)
004d59d9 fix(renderer): draw the sky last, at the far plane, without discard (W-1)
6bdbf421 fix(renderer): walk the displacement lattice the way the engine does (i*size+j)
adce1061 fix(world-entity-tint): painted meshes draw white; the team colour is only an untextured fallback
97ad8917 fix(cli): refuse an unrecognised --switch by name instead of drawing on
09fc824c feat(hud): E11 map-overview panel with basemap art; master mute moves to Ctrl+M
2f5fdee5 native(args): claim --window-size so the new unknown-switch rule stops refusing it
```

天空的画法（`native_renderer_frame.cpp` +181、`native_renderer_shaders.cpp` +33）：

- 从「world 之前」推迟到「不透明 world pass 之后」；
- 用专用 sky PS（不 `discard`、不写深度），深度测试开、深度写入关 ⇒ 早期深度可用、整屏着色收不回的问题消失；
- 远平面锚定：只在顶点着色器里把 clip.z 压到 clip.w 内侧 `1e-6`；穷举四种 pin 的实测——
  exact→pr600 4 px、1e-6→2+2 px、1e-3→up0 会盖掉 **681 px 世界几何**，所以选 1e-6；
- 画完恢复全部状态（背面剔除、深度、slot5 cubemap、world VS/PS、vertex buffer），并记 `10-sky` pass 深度。

位移（`bsp_map.cpp`）：

- 四角按 quad 顺序旋转到 0 号点；`+i` 行走整行、`+j` 内层，平面索引 `i*size+j`；
- 逐格 `usable` 判据失败 ⇒ 整面回退平 quad（13 个 patch 仍走这条路，见 §5.2）。

---

## 4. 验证

### 4.1 门禁（最终二进制，合并树）

| 门 | 结果 | 关键读数 |
| --- | --- | --- |
| `world-sky-gate-check.sh` | **17 passed / 0 failed** | A 空 settings + `preset=0` 也画天空（`state=active draws=1`）；A 断言 `farplane=1 shader=dedicated`；B 三姿势包围 clear=0；C 对账 + C+ tick0 正控 `diff=77131 / far=77131 / owned=77131 / enclosed 6814-0==6814`；D `--world-depth` 不画；E 显式 `skybox=0` 的帧与 `--no-world-sky` 逐字节相同 |
| `map-overview-check.sh` | **PASS (213 checks)** | C 三档尺寸 panel x=1002/1642/2282（= cw−262）、area 242、RED rows 6；D 320×240 panel 177/area 169/capture 304，300×200 → exit 13 且不写位图；E gully→LEGACY 246 px；G outside=0 / inside=153140 / nonbg=59.67 / mapdiff=7 / rectΔ5 / rowdiff=6600 |
| `audio-keys-check.sh` | **PASS** | bare M 只切总览不出静音行、Ctrl+M 静音、`]` 解一步到 0.60、两次重启读数正确、`device_opened=0` |
| `world-entity-tint` / `argument-refusal` / `frame-capture` | PASS | 见 §2-C / §2-D |

### 4.2 帧读数（4 机位 × 3 视图，最终二进制）

```
pose      enclosed   holes clearCol  crack     before(修前同机位)
up0           3262       0       0    3262     8976 / 0 / 0 / 8976
up1800           0       0       0       0     2072 / 0 / 0 / 2072
pr600          453       0       0     453      985 / 0 / 0 / 985
koth0            0       0       0       0     9453 / 0 / 0 / 9453
```

- `enclosed` = 被几何包围的 clear 色像素（孔洞探测器）；`holes` = 没有任何实心面拥有的包围 clear；
- 逐像素与位移分支读数**逐字一致** ⇒ 后续三次合并没有改动画面；
- 2 px `(997,125)/(998,125)` 在已知清单：pr600 上仍是天空色（`depth=0`），其它姿势是世界像素（`depth=765`），属位移接缝残差，不是洞。

### 4.3 几何与接缝

- 接缝（九图 7,855 对）：0.01 内 **3,220 → 7,502**（引擎 oracle 7,535），>1.0 的 **122 → 19**，无配对 **4,312 → 266**；
- 面 13233（dispinfo 304，power 2，size 5）：逐顶点最大偏差 **341.53 → 0.0055**，25/25 顶点 ≤0.01；
- 计数：三角形 696,857 → 697,247、位移三角形 246,688 → 247,104、位移面 5,654 → 5,667（bagel +9、product +4 是转置场下被误杀的 patch 现在正常出网格）；
  镜像绕序 51,814 → 51,097（转置本身在制造镜像绕序，T11 代码未动）。

### 4.4 成本

天空改到最后画：六对交替 A/B、每对翻转顺序、空设置、tick1800：逐对 fps 差
+5.531 / +1.608 / +3.683 / −1.052 / +0.129 / −5.529，中位 +0.869、均值 +0.729（sd 3.54、se 1.45）。
每一对里第二个跑的系统性慢 ~2.4 fps，顺序平衡后代价 **+0.73 fps，低于 ±1.3 fps 空跑噪声 ⇒ 测不出来**。
早先「−13.9% / 3.59 ms」是在负载与进程重叠下测的，已作废并在文档撤回。

---

## 5. 残余与未做（全部有归因，没有藏）

1. **pr600 94 px 单面几何被剔除**：先于本次工作存在；`--no-world-cull` 填 81/94；要改屏幕空间绕序测试，建议单独立项。
2. **up0 3,262 px 里缺几何 = 0**：1,887 无实心面 + 1,337 只有背面（其中 **1,327 是 `trigger_hurt` 不可见触发体**，透天空是对的）+ **38 px 轮廓**（全部在投影三角形边缘 0.70 px 内）。
3. **14 个退化位移面仍画平**：`usable` 判据拒绝面积/边长溢出的格子，整面回退；这条规则本次未改。
4. **格内三角化不是引擎的 fan**（对位面 16/32 索引三元组不同）；顶点几何已经对齐，这是明确不做的一项。
5. **几何洞门未绿**：本机跑得起来，但失败全在 `pl_odyssey` 位姿 pin（本机没这张录像，pin 里 `eye 815.5,−147.8,−194.3` vs 上游 `−1945.3,−1016.6,243.1`），没偷偷改数。
6. **`verify-all.sh` 未端到端跑**；`world-material` 在基线 `candidate/e5` 上同样红（1481→858、15039→25995、227070→216114，input/cluster 未动，属 vis 表解析修复的重新切分，不是丢面）；
   `camera-ui` / `hud-live` / `fullscreen-quiet` 只在 mapui 分支绿过，未对最终二进制重跑。
7. **世界通路不读 `material.noCull`**：`.vmt` 设了也会被忽略，属未支持特性缺口（与上面的洞无关）。

---

## 6. 合并与工程过程

- 五个分支顺序合入 `candidate/e5`：world-visual-fix（`0c3987c1`、`cfb6e7e4`）、disp-param（`93affd2e`）、
  map-overview-hud（`09fc824c`）、world-holes（`2957a6a0`）；每次合并前 `git merge-tree --write-tree` 预检全 rc=0，无文本冲突。
- 合并后诊断出两处**合并特有**的红，在合并树上修正（共 3 个提交，其中 `2f5fdee5` 若卡分支继续也要带过去）：
  - 严格参数规则拒了门禁一直在用的 `--window-size`（窗口层其实早就支持它）⇒ 在同一预扫描块认领并吞取值，悬空值仍拒；
  - 天空门断言 C 钉的是「开关必须差 > 0」，而位移修好后该位姿已无天空像素 ⇒ 改成三条与覆盖度无关的对账，另加 tick0 正控 C+ 证明门还能看见天空。
- 门禁全部自足：缺输入打印 `SKIP <reason>` 并计入 skip 计数，不 FAIL、不静默；断言数与理由都写在脚本注释里。

---

## 7. 运行方式

```
tf2_demo_native.exe --tf-root <tf目录> --demo <dem路径>

# 抓一帧
tf2_demo_native.exe --tf-root <tf> --demo <demo> --capture-tick 1800 --capture-frame out.bmp

# trace（可见性 / 天空状态 / pass 深度）
TF2_NATIVE_TRACE=trace.txt tf2_demo_native.exe --tf-root <tf> --demo <demo> \
    --capture-tick 1800 --capture-frame out.bmp

# 门禁用隔离参数
... --audio-disabled --hidden-window --world-water off
```

调试开关：`--no-world-sky`（复现修前画面）、`--world-sky`、`--world-depth`、`--capture-pass-depth`、
`--world-hole-trace`、`--world-allow-class`、`--no-world-cull`、`--no-world-vis-clip`、`--world-keep-map-winding`。

---

## 8. 证据资产

- `analysis/world-disp-param-up1800-block.png` —— 前后对照：左地板裂开露天空，右连续
- `analysis/integrated-up1800.png` / `analysis/integrated-up0.png` —— 最终二进制同机位帧
- `analysis/minimap-panel.png` / `analysis/minimap-panel-crop.png` —— 小地图 + 名单 HUD
- `analysis/world-disp-param-seam-{before,after,engine}.txt`、`-counters-{before,after}.txt`、`-parity-*.txt` —— 接缝/计数/对位原始读数
- 门禁日志：`.scratch/final-sky.log`、`final-map.log`、`final-audio.log`、`final-frames.txt`

---

*下一步（已定位、未动手）：红蓝名单对齐 + 时间/分数移到顶部居中。布局常量在
`native/src/map_overview.cpp:41-44`（面板/表头/行高、`kRosterTopBelowReadout=84`）、
`main_hud.cpp:494-498`（读数字块 x=24,y=24、行高 22）；`placeTeam()` 在 `map_overview.cpp:493`
把 RED 钉在 `y=96`、BLU 钉在小地图下方（`y=274`），对齐需要同时决定小地图的位置。*
