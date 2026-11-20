# N11 两条异常归因（2026-11-20）

- 归属：`docs/TASK-LIST-2026-11-20.md` 的 N11 行——「先判断 Sunshine POV 眼位 `0,0,-359` 是录制临时状态还是解析错，独立确认 `tiles=0` 的 trace 适用性」。
- 基线：worktree `D:/TF2_Native_Worktrees/t105-integrate`，分支 `work/integrate` @ `542b7f6b`（N24 B2-1）。
- 旧读数出处：`docs/N11-SEVEN-MAP-MATRIX-2026-11-15.md:329`（§9.1 两条读数）、`:308-323`（14 格表）；
  「BLOCKED-CAMERA」判词在 `docs/FOUR-CARD-REPORT-2026-11-16.md:198`（**不在** T62 语料登记里，T62 里没有这个词）。
- 新增证据（可再生、未入库）：`.scratch/n11-attrib.sh`、`.scratch/n11-attrib.log`、`.scratch/n11-attrib/*.trace|bmp|log`、
  `.scratch/bsp-ents2.py`（BSP 实体 lump 解码）、`.scratch/n11-matrix-run.log`。
- 入库产物：`evidence/n11-matrix/readings.tsv`（新 14 列口径）、`tools/n11-seven-map-matrix.sh`、本文件。

## 0. 结论

| 异常 | 旧读数 | 归因 | 一句话判据 |
|---|---|---|---|
| 14 格 `tiles=0` | 旧 `readings.tsv` 14 行全是 0 | **脚本读错行**：读的是 `--world-tile-fill` 的台账，不是渲染器的图块数 | 同图同 tick 加上开关：台账 0→117 行，`gpu: world blend … tiles=3` 逐字不变，帧 **0 像素差** |
| sunshine POV eye=`0,0,-359` | §9.1 第 1 条：eye=0,0,-359、`pvsRejected=0` | **录制侧的退化样本**：不是解析错，也不是整个录像不可用 | 独立探针从文件直读 `camera_origin=0,0,-427 camera_origin_valid=1`；该点在地图包围盒外；同文件 tick 6000 机位真实且 PVS 剔除 1148 |

两条都不构成门禁失败，都不需要改渲染器。§9.1 的两条「待查」都可以结案：一条改脚本，一条改语料使用方式。

## 1. 异常 2：`tiles=0` 读的是开关，不是渲染器

### 1.1 旧列读的是哪一行

- 改动前 `tools/n11-seven-map-matrix.sh:139`：`tiles=$(grep -acE 'gpu: world tile-fill' "$trace")`。
- 该行只有一个发射点：`native/src/main.cpp:7130` `if (worldTileFillTrace)`；开关注册在 `native/src/main_cli.h:347`；来历注释见 `native/src/main.cpp:311`。
- 矩阵的 `run_cell`（改动前 `:94-98`）只传 `--tf-root / --audio-device / --settings-file / --ui-settings-file / --demo / --capture-tick / --capture-frame / --metrics-file`，
  **从不传 `--world-tile-fill`** → 这一列在构造上恒为 0，与地图、材质路径、tick 全都无关。

### 1.2 对照 R3/R4：同图、同 tick、同 exe，只差一个开关

| tag | 额外开关 | `gpu: world tile-fill` 行数 | `gpu: world blend`（末行） | BMP |
|---|---|---|---|---|
| `proc-base` | 无 | **0** | `enabled=1 terrainOnly=0 textures=117 layers=3 accepted=117 acceptedLayers=3 tiles=3 modulateTiles=3 malformedLayers=0 weightedVertices=23040 modulatedVertices=23040 weightMin=0.000 weightMax=1.000 distinctWeights=2` | 2,582,406 B |
| `proc-fill` | `--world-tile-fill` | **117** | 与左格**逐字相同**（`tiles=3 modulateTiles=3`） | 2,582,406 B |

- 帧对照（`bmp-diff-bbox.py`）：`differing_pixels=0 (frames byte-identical)`。
- cp_process_f12 POV tick 2000 的真实图块数 = **3**；cp_sunshine POV 同 tick = **4**。
- 结论：这个开关只加日志、不改画面。真正的图块数在 blend 行（发射点 `native/src/main.cpp:9546`），而不是 tile-fill 台账。

### 1.3 该列现在怎么读（已落进脚本）

- 新函数 `blend_tiles()` 取 `scene: camera`／`gpu: world blend enabled=` 的 `tiles=`；正则用 `s/.*[[:space:]]tiles=\([0-9][0-9]*\).*/\1/p`，避开 `modulateTiles=`。
- 断言改为**每格必须有 blend 行**（`row_ok` 里 `[ -n "${blend:-}" ]`），不再以 tile-fill 行数记读数。
- 另有一条 `gpu: world feature-apply … detail … tiles=`（发射点 `native/src/main.cpp:10460`），本批 cell 恒为 `enabled=0 tiles=0` 属正常（detail/bump 未启用），不能当成渲染缺陷。

## 2. 异常 1：sunshine 眼位是录像里的样本，且在那一格退化

### 2.1 旧读数

§9 表第 7 行：`cp_sunshine | POV | 2000 | 0.0,0.0,-359.0 | demo | 1/1617/1617/0/1617 | 2474b428f3d77f99`。
§9.1 第 1 条把它记为「相机不可信、没有有效视角、PVS 没剔除任何实例」，标为待查。

### 2.2 独立读取（不经过渲染器与产品解析路径）

```
native/build-nmake/demo_open_probe.exe --scan --camera-tick 2000 \
  "E:/demo测试/cp_sunshine/POV/autorecord_2026-05-17_16-32-36.dem"
```

→ `camera_tick=2000 camera_found=1 camera_sample_tick=2000 camera_angles_valid=1 camera_origin_valid=1`
  `camera_origin=0,0,-427 camera_pitch=10.7008 camera_yaw=6.79359`

同一跑的头部读数：`map=cp_sunshine ticks=221569 camera_track=192149 last_view_entity=9 setview_count=1`
`packets_scanned=219069 malformed_packets=0 first_delta_tick=2288 entity_history_delta_misses=1120`。

产品同 tick 的 `scene: eye row`（末 3 行同形）：

```
src=snapshot reqTick=4282 rowTick=4282 backfill=0 flags=31
rowOrigin=0.0000,0.0000,-427.0000 rowOffsetZ=0.0000 srcOrigin=0.0000,0.0000,-427.0000
offsetAge=0 miss=offset-out-of-range
```

（同一张图的另一次跑读到 `reqTick=4280`；tick 落点差 2 帧，不影响结论。）

- 产品读到的就是录像里的值，**逐位相同**：同帧眼位行 `offsetZ=68.0001 eye=0.0,0.0,-359.0 viewEntity=9 originTick=2000 offsetTick=2264 offsetAge=0 life=0 obs=0 reason=applied` → `eye = -427 + 68.0001`（§9.1 说的「orient 复跑 0,0,-359」与此是同一姿势的两个分量，不是两个 bug）。
- 产品自己也标了疑点：该行 `miss=offset-out-of-range`；同帧 `staticProps … eyeCluster=-1`，而 tick 6000 真实机位时是 `eyeCluster=910` —— 退化眼位落在任何可见簇之外。
- `-427` 在 `native/src/main.cpp` 里 grep 不到 → 不是产品侧的哨兵常数。

### 2.3 这个点不在地图里

cp_sunshine BSP worldspawn（`.scratch/bsp-ents2.py` 解 Valve LZMA 压缩实体 lump，解得 282,808 B、987 实体）：

- `world_mins -14892 128 -14672` / `world_maxs -256 15420 1376`；`skyname sky_tf2_04`。
- 点 `(0,0,-427)`：`x=0 > world_maxs.x=-256`、`y=0 < world_mins.y=128` → **出界**。
- 78 个 `info_player_teamspawn` 中最近的是 `-3496.0,328.0,-112.0`，距该点 3525.5；`|x|,|y| < 2` 的 spawn **0 个**。

### 2.4 那一格确实不是有效 POV 画面（原因不是解析）

`staticProps 1/1617/1617/0/1617` = available / instances / visible / pvsRejected=0 / drawn=1617 —— 眼位在包围盒外时 PVS 一个都没剔除，等于全图都画。
对照同文件 tick 6000 的真实眼位 `-6478.4,4600.7,985.4`：`staticProps … visible=469 pvsRejected=1148 drawn=469 budget=4092 eyeCluster=910`（退化帧是 `eyeCluster=-1`）。
所以 §9.1 判「这一格没有有效视角」是对的，但成因是**录像给的样本退化**，不是产品丢了解析。

### 2.5 同一文件其余部分可用（本轮最重要的修正）

用仓库里的分组脚本（`.scratch/n11-eye-runs.py`）把 `scene: eye origin=` 与随后的 `scene: eye row` 按 (life, obs, reason, src) 串起来：

**tick 2000 那次跑**（`sun2000.trace`，1804 条 eye 行；eye-row src = camera 1523 / snapshot 281）：

| tick 区间 | life/obs/reason/src | 条数 | 段首 origin | 段首 eye |
|---|---|---|---|---|
| 0..1079 | 0/0/applied/camera | 998 | `-6539.2,5554.5,78.3` | `-6539.2,5554.5,146.3` |
| 1080..1103 | 0/0/applied/snapshot | 23 | `-5708.8,4792.6,-51.0` | `-5708.8,4792.6,17.0` |
| 1104..1145 | 2/4/dead-no-eye-offset/camera | 39 | `-5537.0,5213.6,423.6` | 同左 |
| 1146..1176 | 0/0/applied/snapshot | 30 | `-5475.6,5210.3,337.8` | `-5475.6,5210.3,405.8` |
| 1177..1235 | 2/7/dead-no-eye-offset/camera | 54 | `0.0,0.0,-427.0` | `0.0,0.0,-427.0` |
| 1236..1250 | 0/0/applied/snapshot | 14 | `-5475.6,5210.3,351.8` | `-5475.6,5210.3,419.8` |
| 1251..1326 | 2/4、2/5 dead/camera | 31+37 | `0.0,0.0,-427.0` | 同左 |
| 1327..1384 | 0/0/applied/snapshot | 51 | `0.0,0.0,-427.0` | `0.0,0.0,-359.0` |
| 1386..1625 | 2/4、2/5 dead/camera | 55+158 | `0.0,0.0,-427.0` | 同左 |
| 1627..1739 | 0/0/applied/snapshot | 101 | `0.9,0.1,-427.1` | `0.9,0.1,-359.1` |
| 1740..1926 | 2/4、2/5 dead/camera | 31+120 | `0.0,0.0,-427.0` | 同左 |
| 1928..2000 | 0/0/applied/snapshot | 62 | `0.0,0.0,-427.0` | `0.0,0.0,-359.0` |

- tick 2000 正落在最后一段（`1928..2000`，整段退化），矩阵恰好停在这一段上。
- alive 行 1279 条：1166 条真实坐标、**113 条 x=y=0** → 退化不只在死亡窗口：已出生但相机样本还没跟上的段同样写 `0,0,-427`。

**tick 6000 那次跑**（`sun6000.trace`，5247 条 eye 行；src = camera 2180 / snapshot 3067）：

- distinct `eye`：2521×`0,0,-427`、570×`0,0,-359`、137×`-4163.9,4589.6,0.0`、89×`-4163.9,4589.6,14.0`。
- 末行：`origin=-6478.4,4600.7,985.4 originTick=6000 offsetTick=-1 offsetAge=-1 life=2 obs=7 reason=dead-no-eye-offset`；同帧 `staticProps … visible=469 pvsRejected=1148 drawn=469 budget=4092 eyeCluster=910`。
- 真实坐标也出现在 dead 段内部（`3225..3286` 段首 `-4163.9,4589.6,0.0`、`3287..3390` 段首 `-4163.9,4589.6,14.0`）；alive 行 1775 条中 1205 条真实、570 条 x=y=0。
- 所以这些窗口里录制**没有有效 POV 样本**（`reason=dead-no-eye-offset`、`offsetAge=-1`），写出的占位值有两类形态：出界整点 `0,0,-427`，与图内「最后位置 + z 贴地」的 `-4163.9,4589.6,{0,14}`。形态由录像端决定，本仓库读不到，**不猜**。
- 反例对照：pl_upward_f12 POV tick 3000（`.scratch/grip/after.trace`）里 `0.0,0.0,-427.0` 出现 **0 次**，眼位 `512.6,-1268.8,400.4` → 这不是产品对 POV 的通用退化。

### 2.6 与 BLOCKED-CAMERA 判词的关系

`docs/FOUR-CARD-REPORT-2026-11-16.md:198` 记的是「盘上唯一可指的 cp_sunshine POV 是退化视点件（相机样本 `0,0,-427`、orient 复跑 `0,0,-359`，已判 BLOCKED-CAMERA）」。
本轮印证了该判词，并补了两件当时没有的事：

1. 值是**录像自己写的**（`camera_origin_valid=1`），不是产品解析失败；
2. **同一文件别的 tick 有真实机位**（1000 在出生前、6000 在比赛中），退化只覆盖死亡/刚复活的窗口。

所以 BLOCKED-CAMERA 应理解为「该语料在 tick 2000 这一格不可用作 POV 证据」，而不是「整个录像不可用」。是否重开该图 POV 列、是否换语料，属 lead 裁决：本轮**不改 `camera2.txt`，不改任何入库语料**，只在矩阵里加自动复采。

## 3. 对 §9.1 两条「待查」的处置

| §9.1 条目 | 处置 | 依据 |
|---|---|---|
| 2. `tiles=0`（全部 14 格） | **结案**：口径错。旧的 14 个 0 不得再用来推任何结论；新表改用 `blend_tiles` | §1 |
| 1. sunshine POV 相机不可信 | **结案**：录制侧退化样本 + 语料使用问题；POV 格新增自动复采（退化则换 tick 复读） | §2 |

## 4. 复现

```bash
cd /d/TF2_Native_Worktrees/t105-integrate
# R1/R2/R3/R4 一次跑完（约 8 分钟/跑，四跑 30 分钟），产物落 .scratch/n11-attrib/
bash .scratch/n11-attrib.sh > .scratch/n11-attrib.log 2>&1
# 独立探针（不经过渲染器）
native/build-nmake/demo_open_probe.exe --scan --camera-tick 2000 \
  "E:/demo测试/cp_sunshine/POV/autorecord_2026-05-17_16-32-36.dem"
# BSP 实体 lump
/c/Users/Administrator/AppData/Local/Programs/Python/Python310/python.exe .scratch/bsp-ents2.py
# 14 格矩阵（hostlock 独占；本机约 30–35 分钟）
bash ../_shared/hostlock.sh run n11-matrix bash tools/n11-seven-map-matrix.sh
```

## 5. 矩阵脚本改动（本轮，`tools/n11-seven-map-matrix.sh`）

1. 新函数 `camera_eye()` / `blend_tiles()` / `eye_degenerate()`（`eye_degenerate` 判 `0.0,0.0,*` 与 `0,0,*`；**不**把 `0,0,0` 当退化 —— 对应 `docs/FOUR-CARD-REPORT-2026-11-16.md:134` 记的那次误匹配修法）。
2. POV 格退化时**自动复采**：用独立 tag `<map>-POV-retick` 依次试 `N11_RETRY_TICKS`（默认 `1000 3000 6000`），成功才覆盖原 trace；同图的 STV 列跟随同一 tick（同图对比必须同 tick）。日志行 `RETICK <tag>: camera at tick N is degenerate (...); re-read at M`。
3. 断言收紧：每格必须有 `gpu: world blend` 行；POV 格必须有 eye 行**且不退化**（复采后仍退化 = FAIL）。
4. TSV 改 14 列：`map column demo tick_default tick_used tick_landed eye camera_src staticProps blend_tiles tile_fill_lines bmp_bytes bmp_sha256 rc`；`runs/<tag>.columns` 与 OK/FAIL 行同步带 `tick=` / `blend_tiles=`。
5. 头部注释补两段：为什么需要第二遍 tick（本文件 §2 的读数），以及 `tile_fill_lines` 列的语义（构造上恒 0，只作开关台账）。

## 6. 本文件不证明什么

- 不证明画面正确（像素归 D01 / `docs/BLACKPOINT-849-919-2026-11-14.md` 的问题）。
- 不证明 sunshine 除 1000/6000 之外每帧都有效；只证明这两次采样的机位真实，且退化窗口在死亡/复活附近。
- 不解释录制为什么在死亡/复活窗口写 `0,0,-427`（那是录制端的客户端状态，仓库里读不到）。
- `--scan` 的 camera 行只覆盖相机轨道；录像有 192,149 条 camera 样本，本轮抽了 2 个 tick 做独立对照，没有逐 tick 对照。
- 不证明其余 6 张图的 `blend_tiles` 正确；那是 14 格重跑的事（§7）。

## 7. 14 格重跑（2026-11-20）

两次跑，同一 exe（`542b7f6b` 树）、同一脚本参数；两次的 `readings.tsv` **逐字节相同**（见 7.4）。

| 跑次 | 日志 | 结果 | 说明 |
|---|---|---|---|
| run 1 | `.scratch/n11-matrix-run1.log` | `readable=14 identical=7` → FAIL | 14 格读数全部 OK，但**跨列断言读错字段**（7.1），7 条 FAIL 是误报 |
| run 2 | `.scratch/n11-matrix-run2.log` | `N11-SEVEN-MAP-MATRIX=PASS rows=14/14` `identical_pairs=0`、`LOCKED-COMMAND-EXIT rc=0` | 修好断言后重跑，本节表格取 run 2 |

### 7.1 跨列断言曾因列下标过时误报 7 对重复（本轮修掉）

- 旧断言（`tools/n11-seven-map-matrix.sh` 改动前 `:220-229`）用 `awk -F'\t' … '{print $11}'` 取 sha —— 对应旧的 12 列表，sha 是第 11 列。
- 本轮把 TSV 扩成 14 列（新增 `tick_default`、`blend_tiles`、`tile_fill_lines`），sha 挪到第 13 列；断言仍读第 11 列 → 读到 `tile_fill_lines`，而这一列**每行都是 0**（§1）→ 7 对全部判「相同」。
- 打印值本身就是证据：`FAIL … POV and STV captures are byte-identical (0)` 括号里的 `0` 是 `tile_fill_lines`，不是 sha。
- 修法：`check_identity()` 按**表头名**定位列（`awk -F'\t' 'NR==1{for(i=1;i<=NF;i++) if($i=="bmp_sha256") print i}'`），找不到列就报错退出；另加 `N11_CHECK_ONLY=1`（只复算断言、不重抓 14 格），一次就能证明：`.scratch/n11-crosscheck.log` → `rows=14 identical_pairs=0` / `N11-CROSS-COLUMN=PASS`。
- 教训：**列数或列序改过一次，任何按位置取字段的断言都是定时炸弹**；改表头必须同时改断言，或者让它按名字取。

### 7.2 新表（run 2，`evidence/n11-matrix/readings.tsv`）

| map | 列 | tick（默认→实采） | eye | blend tiles | staticProps a/i/v/pvs/drawn | bmp sha256（前 16） |
|---|---|---|---|---|---|---|
| cp_process_f12 | POV | 2000→2000 | 4736.0,1440.0,644.0 | 3 | 1/1353/163/1190/163 | 4e5efa36f47895a3 |
| cp_process_f12 | STV | 2000→2000 | 2045.2,1813.4,614.1 | 3 | 1/1353/456/897/456 | aa1fd2119db0cecd |
| cp_gullywash_f9 | POV | 2000→2000 | -51.3,-624.0,373.0 | 1 | 1/1257/248/1009/248 | 2ce5a157c390d9dd |
| cp_gullywash_f9 | STV | 2000→2000 | -1356.4,145.3,235.0 | 1 | 1/1257/219/1038/219 | 98363efb27cd46c9 |
| cp_sultry_b8a | POV | 2000→2000 | 177.1,-608.4,708.0 | 10 | 1/1866/374/1492/374 | b292062d909ecd90 |
| cp_sultry_b8a | STV | 2000→2000 | -2070.1,1864.6,657.0 | 10 | 1/1866/331/1535/331 | 73c180ecb033ec1b |
| cp_sunshine | POV | 2000→**1000** | -5551.9,4905.8,-23.9 | 4 | 1/1617/335/1282/335 | 60addc10bc5d94a2 |
| cp_sunshine | STV | 2000→**1000** | -3297.9,3812.5,168.3 | 4 | 1/1617/317/1300/317 | df102ed7b264f5bd |
| koth_bagel_rc11 | POV | 2000→2000 | -3164.7,285.4,427.0 | 5 | 1/1562/536/1026/536 | 20617c94afe7d62f |
| koth_bagel_rc11 | STV | 2000→2000 | 914.0,-1000.0,563.8 | 5 | 1/1562/599/963/599 | 78f078ace78ac574 |
| koth_product_final | POV | 2000→2000 | -1093.9,407.6,407.9 | 3 | 1/942/507/435/507 | 742658825cfa299c |
| koth_product_final | STV | 2000→2000 | -1864.3,-1060.8,112.4 | 3 | 1/942/305/637/305 | 1b6751b0eebd275f |
| pl_upward_f12 | POV | 2000→2000 | 1797.4,-1398.0,387.9 | 4 | 1/1107/429/678/429 | 882177bc544df8f9 |
| pl_upward_f12 | STV | 2000→2000 | -1945.3,-1016.6,243.1 | 4 | 1/1107/344/763/344 | 24987ed2df701011 |

- 14/14 `rc=0`，BMP 全 2,582,406 B（1280×672 8-bit）；`tile_fill_lines` 全 0 是 §1 的口径，不再是缺陷。
- `blend_tiles` 逐图不同（1 … 10），说明这一列真的是渲染器读数，不是常数。

### 7.3 sunshine POV 复采生效（本轮的行为变化）

- run 日志：`RETICK cp_sunshine-POV: camera at tick 2000 is degenerate (0.0,0.0,-359.0); re-read at 1000`。
- tick 1000：`eye=-5551.9,4905.8,-23.9`、`staticProps 1/1617/335/1282/335`（visible=335、pvsRejected=1282、drawn=335）—— PVS 恢复正常剔除，与 §2 的「出界点导致全放行」互证。
- STV 列跟随同 tick（`-3297.9,3812.5,168.3`），保证同一图两列可比。
- 代价：sunshine 两格不再与 2026-11-19 表同 tick，sha 不可比（`2474b428`/`06944caf` → `60addc10`/`df102ed7`）。

### 7.4 与 2026-11-19 表的差（逐格）

| 类别 | 格数 | 明细 |
|---|---|---|
| eye / staticProps 完全相同 | 13 格 | 只有 sunshine POV 的 staticProps 变了（复采所致） |
| bmp sha 相同 | 4 格 | process、gullywash、bagel、product 的 **POV** |
| bmp sha 变了 | 10 格 | 7 格 STV 全变 + sultry POV + sunshine 两格（复采，预期） |

- **先排除确定性**：run 1 与 run 2 同 exe 同参数，整张 TSV（14 行 × 14 列）逐字节相同 → 这 10 格不是跑次噪声。
- **未归因（本轮不下结论）**：① 7 格 STV 的 sha 全变，而 eye / staticProps 逐字不变；② sultry POV 同样只变 sha。POV 会画第一人称武器，嫌疑在 `ea249e6b`（握点改动），但 process / gullywash / bagel / product 的 POV 又没变，与「凡是画 v_model 的帧都会变」不符。
- **二分配方**（下一格，约 1 次构建 + 1 格抓帧）：在独立 worktree 检出 `777ce0d5`（2026-11-19 表的 exe 树）构建，用同一脚本跑同一格比 sha；若等于旧表，差异就落在 `ea249e6b` / `542b7f6b` 之间，再单提交二分。

### 7.5 复现

```bash
cd /d/TF2_Native_Worktrees/t105-integrate
# 全量（hostlock 独占，约 22 分钟）
bash ../_shared/hostlock.sh run n11-matrix bash tools/n11-seven-map-matrix.sh
# 只复算跨列断言，秒级
N11_CHECK_ONLY=1 bash tools/n11-seven-map-matrix.sh
```
