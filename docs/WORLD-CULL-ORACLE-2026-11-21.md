# 世界剔除判据（诊断 oracle）修正与 pr600 残余归零 — 2026-11-21

> task-9 第 1 部分结案报告。工作树 `D:/TF2_Native_Worktrees/t105-integrate`，证据全部在 `.scratch/n30-disp/`。
> 结论先行：卡片「pr600 94 px 真几何被屏幕绕序背面测试丢弃」的前提**不成立**。产品渲染的背面剔除是对的；
> 错的是**诊断判据**——它在水平镜像投影下把正面当背面。313 px 残余**全部**是 `METALTRUSS001`
> 的 `$alphatest` 镂空，真几何丢失 **0 px**。本报告给出判据修正、`--world-no-alpha-test` 归因臂与像素证据。

## 1. 判据错误的机制（file:line）

抓帧路径在"水平翻转补偿"打开时把投影矩阵的 X 行取反：

- `native/src/native_renderer_frame.cpp:636-640` — `projection.r[0] = XMVectorNegate(projection.r[0]);`（条件 `horizontalFlipCorrectionEnabled()`）
- 产品侧的补偿在光栅化状态里：`native/src/native_renderer.cpp:756` — `rasterDescription.FrontCounterClockwise = horizontalFlipCorrectionEnabled() ? TRUE : FALSE;`

于是同一帧里：**GPU 的三角形绕序判定被补偿过，屏幕空间的叉积符号却没有**。旧诊断判据
（main.cpp 旧 10906 行 face-trace、旧 11569 行 hole-trace）写的是 `const bool faces = area > 0.0f;`，
其中 `area` 是屏幕空间叉积面积——镜像下符号整体翻转，判据于是"选中背面、丢掉正面"。

数值证据（用 trace 的 triScreen + mapAll 在 TS 里重算，同一帧 pr600）：

| triangle | face | 世界法线 | `dot(eye - a)` | 屏幕面积 | 旧判据 | 真实朝向 |
|---|---|---|---|---|---|---|
| 25231 | 7850 | +Y | −15.8M | +2294.5 | 接受 | **背对** |
| 25245 | 7856 | −Y | +15.6M | −2341.1（更近，depth 0.9962） | 拒绝 | **正对** |
| 7180 | — | +Z | −0.9M | +116.5 | 接受 | **背对** |
| 3459 | — | −Z | +2.6M | −346 | 拒绝 | **正对** |

即：旧判据在抓帧下抓的全是背面。这也解释了当年"94 px 真几何被剔"的读数——那是判据的假阳性。

## 2. 修法（只改诊断，不动渲染）

`native/src/main.cpp`：

1. 新增 `windingFacesViewer(triangle, eye)`：用世界空间绕序法线 `cross(b-a, c-a)` 与 `eye - centroid`
   点积 > 0 判朝向。世界空间点积对镜像/翻转不变，抓帧与正常帧同一判据。
2. face-trace 与 hole-trace 都从渲染器取眼：`renderer.cameraEyeMapWorld(...)`，trace 打出
   `camera: capture eye=570.947,32.679,765.130`；取不到眼时回退旧判据并打
   `camera: capture eye unavailable reason=camera-not-ready`（回退是显式的，不是静默）。
3. 新增逐点字段 `alphaCut`：`tile->alphaTest && texel[3] < 128`，阈值对应产品 shader
   （`native/src/native_renderer_shaders.cpp:393` 与 `:613` 的 `if ((featureMask & 16u) != 0u && sampledTexel.a < 0.5) discard;`）。
4. 新增归因臂 `--world-no-alpha-test`：世界顶点 featureMask 的 bit16（`$alphatest`）在 **upload 时**烘进顶点缓冲，
   所以这个开关必须在 map load 前应用一次（main.cpp 参数校验后、地图上传前），不能像 `--no-world-cull` 那样逐帧切。

诚实限制（写进代码注释与报告）：`alphaCut` 用 tile 空间的 texel 判定，shader 用 atlas 空间的 g7uv 采样，
648 点网格上两者一致率只有 **86.0%**（bar+drawn 421 / bar+sky 22 / hole+drawn 69 / hole+sky 136，
`.scratch/n30-disp/grid.trace` + `mask_cmp.py`）。所以 `alphaCut` 只能当线索，判决必须用下面的归因臂。

## 3. 归因臂与像素证据（决定性）

`pr600`（cp_process_f12 tick 600）三条臂，同一二进制、同一相机、同一 tick：

| 臂 | 帧 | 313 px 残余读数 |
|---|---|---|
| cull-on（产品默认） | `.scratch/n30-disp/fix/pr600.bmp` | 天空 |
| cull-off（`--no-world-cull`） | `.scratch/n30-disp/fixnocull/pr600.bmp` | 金属（313 px 全填） |
| alpha-off（`--world-no-alpha-test`） | `.scratch/n30-disp/noat5/pr600.bmp` | 金属（313 px 全填） |

- 判据修正后重跑 313 点 trace（`resid313.trace`）：`traced=313 alphaCut=86 none=0`；
  拾取 `tri=25230(183)/25232(61)/25231(46)/25233(23)`，**全部 `windFaces=True`**（世界空间朝向正确），
  `frameDepth=1.0000`（帧里确实没有几何）。
- **决定性读数**：`residual 313  filled-with-alpha-off 313  still-sky 0`。即这些像素的近面三角形**被光栅化了**，
  是 shader 的 alpha test 把它们丢掉的；若真有"被剔除丢掉的几何"，关掉 alpha test 也填不上。
  ⇒ **真几何丢失 = 0 px**。
- 例（坐标与颜色）：`(323,157)` alpha-on `(117,122,150)` 天空 → alpha-off `(24,23,26)` 金属；
  `(324,157)` → `(29,28,31)`；`(325,157)` → `(33,32,36)`。完整 313 点列表：`.scratch/n30-disp/resid313.txt`。
- 整帧开关差异：`changed 2222` 像素，全部是 alpha-off 臂画出金属、alpha-on 臂是天空的方向（天空→金属 2222，
  金属→天空 0），bbox `(320,157)-(437,261)`。默认臂与修前帧**逐字节相同**（`changed 0`）⇒ 本轮改动不改变任何
  渲染输出，只改诊断读数。
- 注意：`fix/`、`fixnocull/`、`noat5/` 三臂抓于自身隐藏修复合入之前，帧里含相机所站的玩家躯干
  （§4 的 16318 crack 就是它）；313 点掩码落在躯干轮廓之外，三条臂都在同一构建上取，因此对照仍然成立。

## 4. 四姿势复算（现行构建，2026-11-21 15:39）

`python world-disp-param-check.py frames --dir .scratch/n30-disp/frames-cur`（4 姿势 × 3 视图，1264×681）：

| 姿势 | enclosed | holes | clearCol | crack | crackNoSky | 11-17 旧读数 | 差 |
|---|---|---|---|---|---|---|---|
| up0（pl_upward_f12 tick 0） | 3745 | 0 | 0 | 3745 | 3745 | 3262 | +483 |
| up1800（pl_upward_f12 tick 1800） | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| pr600（cp_process_f12 tick 600） | 1212 | 0 | 0 | 1212 | 1212 | 453 | +759 |
| koth0（koth_product_final） | 6551 | 0 | 0 | 6551 | —（无 nosky 帧） | 0 | +6551 |

旧读数由同一条命令在 `.scratch/final-frames/` 上重算得到，与文档 §199-204 的 3262/453/0/0 一致，不是转抄。

**这组旧读数不能当同构建回归基线**：两臂的相机行逐字相同（`scene: camera mode=first-person target=1
follows=1 holds=1 freeze=0 eye=570.9,32.7,765.1 pitch=-2.47 yaw=53.49 src=follow`，up0 同
`eye=308.7,-1983.7,199.7 pitch=10.94 yaw=354.37`），但整帧内容不同——彩色帧 changed up0 859665 /
pr600 850837 / koth0 860414（帧共 861,264 px，99.6% 不同），深度帧 changed 152524 / 172799 / 110748。
旧读数属于 N18-B/自身隐藏/T11 之前的渲染构建。因此 task-9 的判据按用户裁决改为**同构建单变量对照**，
下面的自证臂才是本轮的判决依据。

同构建（同一 exe，只切一个开关）：

- 自身隐藏臂：`--world-show-own-body` 开 vs 关，pr600 crack **16318 → 1212**
  （`.scratch/n30-disp/fix/pr600.bmp` / `frames-cur/pr600.bmp`）；15,106 px 的差是相机所站玩家躯干被画进
  彩色通道（N18-B relay 路径把 hidden 设晚了一帧，见 `native/src/main.cpp` 的 `relayTargetHasPose` 修法）。
- alpha 臂：`--world-no-alpha-test` 把 pr600 的 313 点残余全填成金属、整帧 2,222 px 天空→金属、反向 0（§3）。
- 所以 pr600 现在的 1212 不是"被绕序剔除丢掉的几何"：它们全是天空色 `117,122,150`、`crackNoSky=1212`
  （关天空后变 clear ⇒ 后面确实没有几何），47 个连通块，最大 375 px 在 `(321,178)-(332,228)`——正是 §3
  金属结构的范围；alpha test 丢 texel 才是它们的来源（`$AlphaTestReference` 另立卡）。
- koth0 的 6551 是 48 个大块（最大 929 px，bbox `(430,128)-(469,178)`）天空色区域，属"被几何围住的天空
  开口"；本构建没抓 koth0 的 `-nosky` 帧，未做 nosky 复核，只作基线登记，不与绕序混为一谈。

**task-9 第一半结论**：卡片的"94 px 真几何被屏幕绕序背面测试丢弃"在本构建上 **真几何丢失 0 px**（§3 的
`residual 313 filled-with-alpha-off 313 still-sky 0` + 整帧单向差）；剩余天空像素归 alpha test。

## 5. 第二半：退化位移面 4 → 0（相对上限，不动已恢复的接缝）

`.scratch/degen/FINDINGS.md` 把卡片里的"14 个退化位移面"落到实测：HEAD 九图候选 5671、发射 5667、
**fallback=4**（全部 `kDispCell`），不是 14；4 面都只违反 `native/src/bsp_map.cpp:1435-1446` `usable()`
里的**绝对边长上限** `ab2/ac2/bc2 <= 262144`（=512²），顶点有限、格面积² ≥ 8.95e9，属于合法的超大连片
patch，不是退化。修法是把绝对上限换成相对上限（`native/src/bsp_map.cpp:1514-1539`）：

```cpp
const float allowed2 = std::max(262144.0f, 16.0f * flatEdge2Max);
```

- `counters --all`（probe 含此修）：TOTAL displacement faces **5671 / emitted 5671 ⇒ fallback 0**
  （修前 5667/5671），九图逐图 emitted == faces。
- `seam --mode product --all`：pairs **7855**、exact **7504**、within1 7542、over1 **19**、nopair 294
  ⇒ 已恢复的 7,502 变 7,504（+2），19 对 over1 不变，也没有新增退化；
  引擎 oracle 7535 对不变（`--mode engine`）。
- 证据文件：`.scratch/degen/seam-product-after.txt`（本次 tee 全文）、`.scratch/degen/FINDINGS.md`、
  `.scratch/degen/PATCH-SKETCH.md`。

## 6. 复现

```bash
# 0) 构建（本报告用的二进制）
bash tools/lockrun.sh lead -- bash build-target.sh tf2_demo_native

# 1) 313 点残余（cull-on vs cull-off）+ 逐点 trace
bash tools/lockrun.sh lead -- bash .scratch/n30-disp/cap-resid.sh

# 2) 归因臂：关掉 alpha test 再抓同一帧
bash tools/lockrun.sh lead -- bash .scratch/n30-disp/cap-points.sh .scratch/n30-disp/drawn12.txt noat --world-no-alpha-test
python .scratch/n30-disp/alpha_off_cmp.py .scratch/n30-disp/fix/pr600.bmp .scratch/n30-disp/noat/pr600.bmp .scratch/n30-disp/resid313.txt

# 3) 四姿势
bash tools/lockrun.sh lead -- bash world-disp-param-capture.sh native/build-nmake/tf2_demo_native.exe .scratch/n30-disp/frames-cur --parallel 2
python world-disp-param-check.py frames --dir .scratch/n30-disp/frames-cur

# 4) 自身隐藏臂：同一 exe，只切 --world-show-own-body
bash .scratch/ownbody-ab/capture.sh   # hide / hide-depth / show / show-depth -> .scratch/ownbody-ab/
python .scratch/ownbody-ab/analyze.py

# 5) 退化位移面：fallback 与接缝
bash tools/lockrun.sh lead -- python world-disp-param-check.py counters --all
bash tools/lockrun.sh lead -- python world-disp-param-check.py seam --mode product --all
```

## 7. 未随本报告闭环的相邻项

- `$AlphaTestReference` 被引擎忽略：本 VMT 要 0.2，shader 硬编码 0.5（`native_renderer_shaders.cpp:393/613`）。
  alpha 在 [0.2, 0.5) 的 texel 在我们这与 TF2 表现不同 —— 应为独立卡片，不混进剔除结论。
- task-9 第 2 部分的台账里写的是"14 个退化面"，实测是 **4 个**（HEAD 5671 候选 / 5667 发射），已在 §5 修到 0；
  `.scratch/degen/FINDINGS.md` 记录了"14"这个数在本地不可复现的推导过程。
