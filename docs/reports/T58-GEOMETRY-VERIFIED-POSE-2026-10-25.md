# T58 — 世界几何空洞：旧基线是从地图内部拍的，正确时钟下重测全部通过

日期：2026-10-25 ｜ 分支 `task/t58-geometry` ｜ 基线 `6c26fb2` ｜ 地图 `pl_odyssey` ｜ demo `autorecord_2026-07-02_13-26-46.dem` ｜ playback tick 2000

## 1. 结论

- **没有改产品几何代码。** 这一轮交付的是：证伪旧基线、在正确的位姿上重测、重新钉住读数、重新证明门禁可红。
- 旧基线（T26/T35/T44 的 pin，树 `6b71aeb`）是**在一棵早于 T63-F1 时钟修复 `5ee9642` 的树上拍的**。那次修复之前，场景查询按 signon entry tick 取实体历史，而帧按服务器 net_Tick 前进，所以标称"playback tick 2000"的相机其实在地图实体内部（eye `-2294.8,348.8,383.4`）。现在同一请求 tick 的 eye 是 `815.5,-147.8,-194.3`。
- 正确位姿下的账本：**missing=97,453，全部 97,453 被地图自己的 sky brush 覆盖，unexplained=0**；唯一 "enclosed" 组件 4,613 px 是凉棚缝隙里的天空（3 个点探测命中 sky face 13313/13314/13319，帧深度 1.0000 表示清除值）。**这一批像素里没有缺几何。**
- 门禁改写后：forward **53 OK / 0 FAIL**（owner `t58-repin`，16:59:42–17:03:10，18 帧全部命中缓存）；mutation **6/6 全红且每次 RESTORE=OK**，恢复后的构建**逐字节复现被钉的帧**（owner `t58-repinmut`，≈17:04–17:14:19）。
- 根因能由独立观测推翻或支持（T58 验收第 1 条）：三条互不依赖的观测（同 tick 不同 eye、旧帧内部不自洽、BSP 侧 pin 一个都没动）都指向"位姿错了"，没有一条指向"几何规则变了"。

## 2. 可反证假设

| | 假设 | 支持它的观测 | 能反证它的观测 |
|---|---|---|---|
| H1 | 空像素是缺几何，要修产品 | 旧树 97,453/1,648 空像素 | 若 H1 成立，sky 覆盖后必须留 `unexplained>0`；实测 **0** |
| H0 | 空像素是正常天空 + 旧 pin 位姿错 | 两次运行同一请求 tick 给出不同 eye | 若 H0 成立，**BSP 派生的 pin 必须一个都不动**；实测 731/1,842/99,312 全未变 |

判定：H0 成立，H1 被 `unexplained=0` 反证。

## 3. 旧基线为什么无效（三条独立证据）

**(a) 同一请求 tick，两种位姿。**

- 旧（HEAD `6b71aeb`）：`EYE eye=-2294.8,348.8,383.4 src=demo tick=2000 ... frames=2068`，camera first-person pitch `-8.43` yaw `64.27`。
- 新（`6c26fb2`）：`EYE eye=815.5,-147.8,-194.3 src=demo tick=2000 armed=50898.6 frame_tick=2000 frames=2146`，pitch `-5.45` yaw `-39.04`（`evidence/t26-world-holes/analysis/seam-eye.txt`）。

**(b) 旧帧本身就不自洽。** `git show 6b71aeb:evidence/t26-world-holes/captures/textured.bmp` 里：中间贯穿的黑色平板、一片草地纹理被拉满整个巨三角形、底部红砖——那是相机在实体内看到背面/内部面。新帧同 tick 是连贯场景（石块、木凉棚、天空、地面）。并排图 `%TEMP%/t58-old-vs-new.png`。

**(c) 只有 app 派生的 pin 动了，BSP 派生的一个都没动。**

| reading | corrupt pose (`6b71aeb`) | verified pose (`6c26fb2`) | 动了? |
|---|---|---|---|
| missing | 1,648 | 97,453 | 是 |
| missing covered by sky | 1,648 | 97,453 | 是 |
| missing with no sky face | 0 | 0 | 否 |
| enclosed | 1,648 | 4,613 | 是 |
| enclosed components ≥200 px | 3 | 1 | 是 |
| back-face rejections (T23) | 80,917 | 52,788 | 是 |
| **sky faces / sky triangles** | **731 / 1,842** | **731 / 1,842** | **否** |
| **buffer triangles** | **99,312** | **99,312** | **否** |
| textured frame sha | `ac2cfe9b…` | `1b0ab468…` | 是 |
| ref frame sha | `6bb4d9a7…` | `e9218c42…` | 是 |
| seam points / probes / faces | 6 / 24 / 8 | 2 / 8 / 4 | 是 |
| cull control | face 17605 / 33,167 px | face 17560 / 52,788 px | 是 |

归因：`6b71aeb` 早于 `5ee9642`（T63-F1）。该修复自己的门禁 `demo-clock-check.sh` 现在 63/0 绿，证明场景查询已按服务器 tick 取样；T63 报告实测的偏差是 snakewater 67 tick、bagel 56,146 tick。

**旧门禁在这里报的 18 条红（`.scratch/gate.log`，逐条具名，可直接对照新 pin）：**

```
FAIL stored textured frame identity: 1b0ab468… != ac2cfe9b…
FAIL missing pixels: 97453 != 1648
FAIL missing pixels covered by a sky face: 97453 != 1648
FAIL enclosed missing pixels: 4613 != 1648
FAIL component rank=2 area=4613 … is not a sliver (102x97)
FAIL capture-pose eye from the trace's last scene camera: 815.5,-147.8,-194.3 != -2294.8,348.8,383.4
FAIL the eight named seam faces: 238,4173,9187,9189 != 8555,15324,17217,17260,17262,17266,17268,17326
FAIL no displacement face bounds a sliver: the seam account lost its subject
FAIL top non-seam face the product cull removed: 17560 != 17605
FAIL the control face's BSP side bit: 1 != 0
FAIL pixels the cull removed from the control face: 52788 != 33167
```

## 4. 正确位姿下的账本（全部实测，均为当前 pin）

```
HOLE totals missing=97453 sky=97453 unexplained=0 skyOnDrawn=710543
HOLE split open=92840 enclosed=4613 enclosedUnexplained=0 components=3
HOLE maskReach coverage=860784 share=1.00000 degenerate=1
HOLE depthControl drawnCovered=710543 skyBehind=0 skyInFront=0 tie=710543 measured=1 saturated=1
ACCOUNTING totals pixels=860784 drawn=710543 backface=52788 visibility=0 both=0 missing=97453
geometry sky faces=731 triangles=1842 bufferTriangles=99312
```

- 三个 open 组件 rank0 86,746 / rank1 6,094 都 `edge=1`（触到画框边缘，是画外的天空）；只有 rank2 是 `edge=0`。
- 帧 sha：textured `1b0ab468364e20c7b209b48ca710e9b56530e07bfafff53ec6707a0f62ec70b6`，ref `e9218c42c7c93249656e2d343b4a834ddca0e6dd7faa754f99292e300e01188e`。
- 身份（`analysis/identity.txt`）：HEAD `6c26fb2edae5`，`tf2_demo_native.exe 602b1fe3…`，`world_geometry_probe.exe 0425f156…`，`bsp_map.cpp 6be8b243…`，`main.cpp f05ec26c…`，`native_renderer.cpp e168b9c4…`。

## 5. 归因：唯一 enclosed 组件是凉棚缝隙的天空

- `rank=2 area=4613 sky=4613 unexplained=0 bbox=(x=700..801,y=31..127) edge=0 fill=0.466`。
- 点探测（`analysis/point-verify.txt`，9 点全 OK，`failures=0`）：(760,53)→sky 13314、(763,90)→sky 13313、(722,86)→sky 13319，材质 `TOOLS/TOOLSSKYBOX`，`skyDepth≈0.9986`、`frameDepth=1.0000`（清除值）、`empty=1`、`idMatch=1`。
- 视觉：`C:/Users/Administrator/AppData/Local/Temp/t58-enclosed.png`（框 690,20,815,140 ×5：hole-mask / textured / depth / flat）——是木梁与石板之间的**透光缝**，不是裂缝、不是缺面。
- 因此形状判定被**反转**而不是删除：**在正确位姿下，任何 enclosed 组件都不许是针状**（`thin<=32 && long>=40` 直接红）。出现裂缝形状必须重新归因，这正是这条门禁存在的理由。

## 6. 门禁改了什么（`world-geometry-hole-check.sh`：1,417 → 1,482 行，+132/−67）

1. **pin 段整段重写为 T58 RE-BASELINE**：写清 corrupt pose、日期到 `5ee9642`、附上面那张"动了/没动"表，并说明两处归因变化。旧历史保留并标注 superseded，不静默改写。
2. **enclosed 总体逐行钉住**：`PIN_ENCLOSED_POP="rank=2 area=4613 sky=4613 unexplained=0 bbox=(x=700..801,y=31..127) edge=0 fill=0.466"`，取代旧的"每个组件都是针状"形状启发式。
3. **形状判定反转**（见 §5），fill 守卫改述为"实心团块才是缺面，不是开口"。
4. **seam subject 反转**：`PIN_SEAM_DISPLACED=0` 取代 `DISPLACED>=1`。旧的"至少一处位移单元夹住 sliver"在正确位姿下没有主语，永久红不是门禁；现在是"一旦出现位移单元夹住 enclosed 区域就红，必须先归因"。
5. **`EXPECTED_FACES` 从 id 列表推导**，取代硬编码 `assert_eq "$DERIVED_PARSED" "8"`（那是 `PIN_SEAM_IDS` 的第二份拷贝，会静默接受解析了别的数量的分类）。
6. **`PIN_CULL_TOP_SIDE=1` 变成 pin 并被断言**（原来是字面量 `0`）。注释写清：BSP side bit 从来不是剔除依据（依据是 T41 的 derived side）；旧控制面是 side-0 且同样 derived-back，两个面都与自己的 bit 相反。

**门禁自测（缺位姿/stale capture/错帧必须拒收）——本轮实测 PASS：**

```
PASS a trace that lost its "scene: camera" pose line is refused, not reused
PASS a frame whose executable identity changed is refused, not reused
cached matrix before this run: 18 of 18 frames reusable
captured this run: 0 recaptured, 18 reused from cache
```

## 7. mutation（6/6 全红，逐条具名）

| # | 突变 | 红在哪（原文摘录） |
|---|---|---|
| M1 | 解析器保留 sky 计数、丢掉多边形 | `PASS M1: no sky polygon means no upload -- exit 14, no draw, no atlas, no frame` |
| M2 | sky pass 上传 world 三角形 | `the gate goes red: sky=0 unexplained=97453 enclosedUnexplained=4613` |
| M3 | face-id 视图返回常量 | `HOLE-POINT-SUMMARY points=9 failures=9` |
| M4 | sky mask 用自己的变换 | `HOLE-POINT-SUMMARY points=9 failures=3` |
| M5 | 账户改用产品画面判空 | `the gate goes red: missing=150241 sky=150241 enclosed=55214`（pin 97453 / 4613） |
| M6 | 关闭背面剔除 | `the cull removed 0 px (positive control RED) while all 4 named seam faces are still derived-front` |

每次突变后：`RESTORE=OK (four files byte-identical)`。末尾一项是防"恢复只是文件回来了、画面没回来"：

```
-- after restore: the pinned frame comes back --
PASS the restored build reproduces the pinned textured frame byte for byte
== mutation verdict ==
PASS: all six mutations turned the gate red and every source came back byte-identical.
```

注：M2/M5 的红值是新的 pin（`unexplained=97453`、`enclosed=4613`），说明重新钉的读数真的被 mutation 用上了，不是旧数字残留。

## 8. 恢复与重建

- 突变源文件全部逐字节恢复；`git status --porcelain -- native/ src` 为空。
- 恢复后 `build-target.sh tf2_demo_native world_geometry_probe` 重建（exe 17:13），重拍 textured 帧与 pin **逐字节一致**。
- 结论：位姿修好后**像素是确定性的**，所以 chain 里重拍矩阵不会造成工作树脏（BMP 相同，identity/trace 在 `.gitignore` 里）。

## 9. 链路（verify-all.sh）：新增 step 28/28

- T58 判定的根因不止是"pin 老了"，还有一条流程缺陷：**这个门禁不在 verify-all.sh 里**，所以它红了三天没人知道。现在它是 `28/28 world geometry at the pinned pose`，断言数钉在 **53**（实测，不拷贝）。
- 代价诚实写入注释：进程身份（exe/demo/map/tick/window/flags/capture script）不匹配时重拍 18 帧 ≈16 min；本机本轮是 18/18 复用，门禁本身 3.5 min。
- 后续波次的耦合也写进注释：**T59 的材料修复若改变世界像素，`PIN_TEXTURED_SHA`/`PIN_REF_SHA` 会在 step 28 变红**，必须在 T59 里按 T35/T58 的流程重新归因后再改 pin。为让 chain 变绿而改 pin 是禁止的。

## 10. 证据与运行账

- 证据：`evidence/t26-world-holes/`（18 张 BMP 全部按新位姿重拍 + `analysis/` 11 个文本 + `analysis/identity.txt`）。
- 日志：`.scratch/capture.log`（16:27:27–16:43:20，18/18 拍到）、`.scratch/gate.log`（旧 pin 红，16:46:19）、`.scratch/repin-gate.log`（新 pin forward 53/0，17:03:10）、`.scratch/repin-mutation.log`（mutation 6/6，17:14:19）。
- hostlock：`t58-baseline`（16:26:14 取得，16:46:19 释放）、`t58-repin`（16:59:42 取得）、`t58-repinmut`（≈17:04 取得，17:14 释放）。全程无第二个产品进程。

## 11. NOT VERIFIED（如实限定）

- 只测了 **一张图（pl_odyssey）、一个 demo、一个 playback tick（2000）**。它证明的是"这批像素没有缺几何"，不是"任何地图任何位姿都没有缺几何"。
- T56 的静态审计（335 个 BSP）指出的材质缺失（如 `PROPS/HAZARDSTRIP001A` 在三张图上都缺 VMT）属于**材质链**，是 T59 的范围，不是几何缺陷；本轮没有把它们当作几何问题处理。
- `koth_bagel_rc13` 没有 BSP，几何门禁无法覆盖它。
- 其余带"帧 sha pin"的门禁已排查：只有 `game-event-check.sh`（T68，新树拍的）与 `weapon-body-check.sh`/`weapon-material-check.sh`（`POV_SHA`，T60 波次新拍）带帧 pin，都不是旧时钟时代的产物；旧时钟时代留下的帧 pin 只有 T26/T35/T44 这一组，已在本轮修正。
- 没有做：其它地图的空洞普查、动态实体遮挡对空洞的影响、`--world-water` 之外的特效路径。

## 12. 合并风险

- 只改 `world-geometry-hole-check.sh`、`verify-all.sh`、`evidence/t26-world-holes/**` 并新增本报告；**没有动 `native/`**，因此与 T59 的材质改动不冲突（只是 step 28 的帧 pin 会在 T59 改像素后需要重新归因）。
- `verify-all.sh` 的 27 个 step 标签已机械改为 `/28`，另新增 step 28；合并 p0 时若与别的波次同时改这一行需要按顺序重排编号。
