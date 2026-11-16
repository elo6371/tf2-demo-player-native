# E1 第 5 轮 triage（2026-11-16，exe 75dae10e）

窗口 15:10 起，card-arch 在共享工作树 `t105-e5` 上跑 viewmodel-material / minimap / map-geometry /
companion 四件事。本文只记录**实测读数与分流**，不改判据、不把红改绿。

## 1 身份

| 项 | 值 |
| --- | --- |
| 产品 exe sha256 | `75dae10e60740cd2c3e780ef68c72eb100e6f4221f8c234227a907a99d0607ab` |
| 大小 / mtime | 3,044,864 B / 2026-11-16 14:14:35（第 4 轮 same-bytes 重建后未再变） |
| HEAD（本轮起） | `997e945e` |
| HEAD（收工） | `f71ff277`（我的 triage 提交 `f8315bc1` 在其上） |
| exe 树绑定（sidecar） | sourceTree `e8cabb154d47`、sourceContent `661a4bc5e988f57bbf2e0b0115a6cfecf0d2a2cb05ed911481cd2d13aa5fbe66`、contentBinding `carried`、buildUtc `2026-11-16T05:44:34Z`、dirtyFiles=9（7 tracked + 2 untracked） |
| 主树构建 | 本轮**不在主树构建**；exe 全程同 sha（收工再取仍同） |
| 锁 | verify-all 用 `tools/lockrun.sh`；产品/捕获用 `/d/TF2_Native_Worktrees/_shared/hostlock.sh` |

窗口中途 HEAD 移动过：`d325ff0f`/`35f8856a` 于 16:11:52、`f71ff277` 于 16:18:49，作者是 card-maps。
`git show --name-only` 核实这五个提交（含 lead 的 `71c983cb`/`997e945e`）只碰 `map-material-matrix.sh`、
`source-tv-coverage-check.sh`、`evidence/source-tv*` 与 `docs/*`，**没有一行 `native/` 或构建输入**。
本轮四门都是乘积行为类，不吃这些提交；本轮也未跑 release-rebuild / userinfo，所以没有任何 identity 红。
下一窗口若见到 identity 不一致或 STALE，先按「窗口中途有脚本+docs 提交」定性，不要改判据、不要动 exe、
不要 revert 别人的提交。

## 2 门表

| 门 | 结果 | 耗时 | 关键读数 |
| --- | --- | --- | --- |
| viewmodel-material | **FAIL**（分段跑） | §1–8 1100 s（§9 前被上限截断）+ §9 单跑 | run A: 202 OK / 7 FAIL；`VIEWMODEL_MATERIAL_SECTIONS=9` 单跑: 9 OK / 3 FAIL |
| minimap-check | **PASS** | 2 s | 27 assertions；ledger cp_snakewater_final1 scale=0.078293204308、pl_badwater=0.130932897329 |
| minimap-hud | **FAIL** | 285 s | 96 assertions / 3 failed（A1、A6、A7） |
| map-geometry cp_snakewater_final1 | **p1 PASS**，p2 WINDOW-DEADLINE | p1 ~11 min，p2 超窗 | p1: 24 OK / 0 FAIL，re-pin 写入 frame-pins-p1 + cull-reference-p1（重写后 git diff 为空） |
| viewmodel-companion | NOT RUN | — | WINDOW-DEADLINE |

未跑的四张图（pl_odyssey / cp_sunshine / koth_bagel_rc12 / koth_bagel_rc11）同样记 WINDOW-DEADLINE。

## 3 红分流

### 3.1 viewmodel-material（产品路径数值，不是门禁算术）

§7 the binding is what changes the picture：

```
FAIL flat control draws the fallback = 3, expected 1
FAIL flat control uploads no texture = 2, expected 0
```

§8 a slot that does not resolve falls back, and says so：

```
FAIL nothing is bound = 2, expected 0
FAIL no texture was uploaded for it = 5, expected 0
FAIL no texture bind happened = 2, expected 0
FAIL the pass still draws the geometry = 3, expected 1
BMP-DIFF differing=7272 total=2582352 percent=0.2816 status=FAIL reason=whole frame percent=0.2816 exceeds 0.0000
FAIL the fallback frame is exactly the flat control frame (rc=1)
```

§9 the alpha path（`VIEWMODEL_MATERIAL_SECTIONS=9` 单跑）：

```
FAIL one range uses the alpha blend state = 3, expected 1
FAIL alpha is the only blend path applied = 3, expected 1
FAIL the bound slot is the glass one = uploadSlot=-2, expected it to contain uploadSlot=2
       path=models/weapons/v_models/v_sapper_spy.mdl slots=4 resolved=4 decoded=4 uploads=4 drawnSlot=0
```

判读：在 75dae10e 上，flat/fallback 路径不再产生 flat control 帧（画 3 个几何、2 次纹理上传），
alpha 路径有 3 个 range 走 alpha blend（钉值 1）。这两组都是产品路径的数值，不是门禁算术；
未改判据，交材料/alpha 语义 owner 重定口径。上一轮 OK 的两条 pin（`:689`/`:691`
`the pass built one range = 2` / `the range has a texture bound = 2`）本轮仍 OK。

### 3.1.1 十红逐条定性（2026-11-16 18:0x 复测，exe 216d7f98）

§3.1 是第 5 轮 75dae10e 上的现场判读（"交材料/alpha 语义 owner 重定口径"），本节把它落成
逐条定性：每一红给出断言式、实测、期望来源、定性、下一步。**只定性，不改阈值、不改代码。**

#### 复测身份

| 项 | 值 |
| --- | --- |
| 产品 exe sha256 | `216d7f986574dc350113ad8b08902bc43754915c3ac0868c8c90e8405981d1eb`（3,057,152 B） |
| HEAD（起跑） | `2cbe87e5` |
| 跑法 | `VIEWMODEL_MATERIAL_SECTIONS="7 8 9"`，只补三节（§6 未重跑） |
| TRACE_DIR | `evidence/fast/viewmodel-material/rerun2`（**相对路径**） |
| 日志 | `/d/_shared/EXEC-2026-11-15/INST1/E1-round5/rerun-1716/rerun2/viewmodel-material-s789.log` |
| 读数 | 26 OK / **10 FAIL**，与第 5 轮 10 红逐条同位置；§8 的 `differing=7272 total=2582352 percent=0.2816` 与 75dae10e 上**逐字节同值** |

exe 从 a14ea967 变 216d7f98 不是本轮跑门造成的：D-93 判据修正（`b233bdae`）的首次真实门把
manifest 覆盖面从 src-only 扩到全输入，产品因此一次性 relink；`git diff 014da358..HEAD -- native/` 为空，
源码未变。两次读数（75dae10e / 216d7f98）逐项同值，说明这 10 红与 relink 无关。

跑法注意：TRACE_DIR 必须是**相对路径**。门禁用 `--capture-frame "$ROOT/$TRACE_DIR/name.bmp"`
（viewmodel-material-check.sh:598）拼路径，绝对路径会被拼成 `$ROOT//d/_shared/...`，产品 rc=16、
不产 bmp，整节读成空值（本轮第一次复测即因此报废，日志在 `rerun-1716/viewmodel-material.log`）。

#### 逐条

| # | 门:行 | 断言 | 钉的值 | 实测（216d7f98） | 期望来源 | 定性 | 下一步 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | §7 :785 | flat control draws the fallback | 1 | **3** | 279dabb5（2026-10-13）写下的单 range 读数 | 期望漂移：flat 模式下 3 个 range 全是 fallback（pflat ranges=3 rangeSlots=-1,-1,-1） | re-pin 3，注释记 basis |
| 2 | §7 :786 | flat control uploads no texture | 0 | **2** | 同上，单通道时代 | 期望漂移 + **量具失效**：计数器是渲染器全局单调值，flat 只压 arms pass，body pass 仍上传（见机制 3） | 先 re-pin 2 并注记；根治需加 pass 局部 upload 计数（产品侧，另任务） |
| 3 | §8 :838 | nothing is bound | 0 | **2** | 279dabb5 单 range 时代 | 期望漂移：注入槽 0 后另两个 range 仍绑到真实纹理（pmissing rangeSlots=-1,1,2 rangeTextured=0,1,1） | re-pin 2 |
| 4 | §8 :840 | no texture was uploaded for it | 0 | **5** | 同上 | 期望漂移：resolved=3，其余槽照常上传；计数仍是全局值 | re-pin 5（附机制注释） |
| 5 | §8 :841 | no texture bind happened | 0 | **2** | 同上 | 期望漂移：两个 textured range 命中纹理缓存 → reused=2 | re-pin 2 |
| 6 | §8 :844 | the pass still draws the geometry | 1 | **3** | 同上 | 期望漂移：3 个 descriptor 都画（drawRanges=3） | re-pin 3 |
| 7 | §8 :846 | the fallback frame is exactly the flat control frame | differing=0 | **7272 / 2582352 = 0.2816%** | 同上 | **断言前提失效**：pflat 是 0,0,0 全不贴，pmissing 是 0,1,1，另两个 range 的纹理必然不同 → 整帧不可能相等；单 range 时代才成立 | 退役该断言，换两条新控制（见下） |
| 8 | §9 :870 | one range uses the alpha blend state | 1 | **3** | 同上 | 期望漂移：palpha rangeSlots=2,2,2、rangeTextured=1,1,1，3 个 range 全走 translucent | re-pin 3 |
| 9 | §9 :873 | alpha is the only blend path applied | 1 | **3** | 同上 | 同上（alphaAppliedRanges=3） | re-pin 3 |
| 10 | §9 :876 | the bound slot is the glass one | 含 `uploadSlot=2` | **`uploadSlot=-2`** | 279dabb5 时的产品：`uploadSlot = flat ? -1 : (slotOverride >= 0 ? slotOverride : drawnSlot)` | **产品语义在门禁写下 4 天后变了**：952ddf20（2026-10-17）把常量改成 `flat ? -1 : -2`（今天 main.cpp:7969），-2=上传全部 resolved 槽 | re-pin 成 `uploadSlot=-2`（或断言 `uploads=4`），并注 952ddf20 出处 |

期望来源的硬证据：这 10 条断言文本**自 279dabb5（2026-10-13）起逐字未改**（本轮把两边的断言行抽出来排序 diff，
结果为 identical）；`435b4d5c`（2026-11-16）换录制时只 re-pin 了 §6 的 `:689/:691`
（drawRanges 1→2、boundSlots 1→2），§7–§9 从未在新录制/新语义上重新取数。

#### 机制（为什么实际值长这样）

1. **3 个 range**：`--viewmodel-render command` 下 v_sapper_spy 的 MDL mesh table 现在给 3 个 LOD-0 descriptor
   （materials 行 `meshCount=3 descriptorsValidated=3`），ranges 行 `ranges=3 rangeVertices=11640,9447,96`。
   pflat 的 `rangeSlots=-1,-1,-1`、palpha 的 `2,2,2`、pmissing 的 `-1,1,2` 都出自同一张表。
2. **-1 / -2 上传语义**：main.cpp:7969 `const std::int32_t uploadSlot = viewModelFlatColor ? -1 : -2;`；
   native/src/viewmodel_material.cpp:344-375 只在 `uploadSlot == -2 || uploadSlot == slot` 时上传。
   -1=flat 不上传，-2=全上传；旧代码的第三种形态（= slotOverride/drawnSlot）随 952ddf20 消失。
3. **texturesUploaded 是全局单调计数**：native_renderer.cpp:202 递增，`clearViewModelMaterials`（:206-211）
   明确不重置。arms pass 之外还有 weapon body 的上传点 main.cpp:8170（`ensureViewModelWeaponMesh`，
   同样调 `decodeViewModelMaterialSlots(..., viewModelWeaponFlatColor ? -1 : -2, ...)`），
   所以 flat 跑出的 2 次上传不是本节要看的那条路径。
4. **pass 内计数每 pass 归零**：native_renderer_viewmodel.cpp:681-735 重置 fallbackRanges/boundSlots/
   blendedRanges/alphaTestedRanges；boundSlots 按去重 key 计，palpha 三个 range 同槽 2 故 boundSlots=1
   （rangeSlots 仍是 2,2,2）。
5. **§8 的 7272 px 来自两个 textured range**：pflat `rangeTextured=0,0,0` vs pmissing `0,1,1`，
   像素差只可能落在后两个 range 上。整帧相等控制是单 range 几何的产物，不是通用判据。

#### 为什么不是产品回退

- §7 的三条因果控制全绿：ptex vs pflat `differing=29561 (1.1447%)`、世界条带 3 处 region diff=0、
  ptex≠palt 亦 PASS、pflat vs pflat2 与 ptex vs ptex2 **byte-identical**。
- §9 同节另 9 条全绿：rc=0、slotOverride=2、resolved 4/4、boundSlots=1、alphaSlots=2、alphaTested=0、
  translucent=1、11640 vertices（几何没被 override 带偏）。
- §8 fallback 机制本身在位：missingSlotInjected=0、resolved 4→3、fallbackSlots=1、fallbackRanges=1、
  frozen frame=yes。
- 两次不同 exe 读数逐项同值（含 7272/2582352/0.2816），排除随机/时序。
- §6 的 re-pin 已在 435b4d5c 按本录制实测（drawRanges 2、boundSlots 2），第 5 轮 run A 里该节通过。

#### re-pin 提案（下一窗口，属取数刷新不是放宽）

1. §7 :785 1→3、:786 0→2（附「全局计数器 + body pass」注释）；§8 :838 0→2、:840 0→5、:841 0→2、
   :844 1→3；§9 :870 1→3、:873 1→3、:876 `uploadSlot=2`→`uploadSlot=-2`。
2. §8 :846 的整帧相等控制**退役**并换两条：
   (a) pmissing 与第二次 pmissing（同参数）byte-identical —— 测稳定性；
   (b) 断言 pmissing 的 `rangeTextured=0,1,1` 与 `fallbackRanges=1` —— 直接测「只有注入的那个 range 落 fallback」。
   原断言测的是单 range 时代的整帧外观，不是「fallback 不绑纹理」这个意图，换掉不算放宽。
3. §7 :786 的根治：产品加一个 pass 局部 uploads 读数（或在现有 readings 行加 `passUploads`），
   让断言能测它自己命名的 pass；在此之前先按实测 2 钉住并在注释里写明局限性。
4. 所有 re-pin 沿用 435b4d5c 的写法：值 = 实测值、注释记 basis（录制 sha / exe sha / 代码出处），
   并在提交 message 里写明「re-pin, not loosening」。

**lead 已定（2026-11-16 18:07）**：不产品加计数器（原本提案第 3 条作废）；§7 改断言对象；见 §3.1.2。

#### 3.1.2 re-pin 窗口（2026-11-16 18:10–18:29，lead 裁决后执行）

lead 对 §3.1.1 提案的三处改动：**不给产品加 pass 局部 upload 计数器**（§7 改断言对象即可）；
§7 的 uploads 数字断言**退役**，只留单调 sanity；§8 整帧相等**退役**，换 pmissing 稳定性 + rangeTextured；
**§9 一行不动**，只 re-pin §7/§8。

#### 身份

- exe sha256 216d7f98…（3,057,152 B），门禁**跑前/跑后同值**；身份钉 sha，不由 head/dirty 推导。
- head 起跑 = 56dee328；native/ 无改动。
- 工作树 dirty=52，全部落在 evidence/t62-map-matrix/* 与 lead 的 docs/*（card-maps 的 map-material 重跑在写），
  没有源码目录改动；按 lead 的记账口径（非源码证据目录脏）可作门禁身份。
- 只跑 SECTIONS="7 8"；TRACE_DIR=evidence/fast/viewmodel-material/repin（相对路径，.gitignore 已忽略；
  绝对路径会把 capture 路径拼坏，见 §4.4）。日志 /d/_shared/EXEC-2026-11-15/INST1/E1-round5/repin-1810/repin-run.log。

#### 新旧值对照（旧 pin → 新断言 → 本次实测）

| 断言（节:行） | 旧 pin | 新断言 | 实测 |
| --- | --- | --- | --- |
| §7 flat control draws the fallback | 1 | 3 | 3 |
| §7 flat control uploads no texture | 0 | **退役**：改上传计数器单调 sanity + range 表 | yes |
| §7 （新增）flat control draws all three descriptors | — | RANGES_LINE 含 ranges=3 | 3 |
| §7 （新增）flat control leaves every range untextured | — | RANGES_LINE 含 rangeTextured=0,0,0 | 0,0,0 |
| §7 flat control reuses no texture | 0 | 0（不动） | 0 |
| §8 nothing is bound | 0 | 2 | 2 |
| §8 the range is a fallback range | 1 | 1（不动） | 1 |
| §8 no texture was uploaded for it | 0 | **退役**：改上传计数器单调 sanity | yes |
| §8 no texture bind happened | 0 | 2（改 label：the other two ranges reuse their cached textures） | 2 |
| §8 （新增）only the injected range lost its texture | — | RANGES_LINE 含 rangeTextured=0,1,1 | 0,1,1 |
| §8 （新增）the other two ranges still name their slots | — | RANGES_LINE 含 rangeSlots=-1,1,2 | -1,1,2 |
| §8 the pass still draws the geometry | 1 | 3 | 3 |
| §8 the fallback frame is exactly the flat control frame（整帧相等） | 0% | **退役** | 实测 7272/2582352=0.2816%（两 range 仍有纹理） |
| §8 （新增）two runs of the fallback frame are byte-identical | — | pmissing vs pmissing2，max-diff 0 | differing=0 |

为什么 uploads 不钉数：texturesUploaded 是渲染器全局单调计数（native_renderer.cpp:202；clearViewModelMaterials
故意不重置），weapon body pass 也经它上传（main.cpp:8170），它从来不是本 pass 的计数——钉 2 或 5 都是把
计数副作用当成 pass 行为。新断言只保留了计数器自己能回答的性质：一次 run 内不倒退（uploads_monotonic，
读取 materialReadings 行的 texturesUploaded 序列，出现下降即红）。

为什么整帧相等要退役：该断言假设 pass 只建 1 个 range。v_sapper_spy 的 mesh table 给 3 个 descriptor，
注入 missing slot 后只有 1 个 range 落 fallback、另 2 个仍带纹理，两帧必然不同（实测 0.2816%）。
留下的可检验主张是**可复现性**：同参数跑两次 byte-identical（pmissing vs pmissing2）。fallback 机制本身
仍由 missingSlotInjected=0、materialResolved=slots-1、fallbackSlots=1、fallbackRanges=1、frozen world 盯住。

新增捕获 pmissing2（--viewmodel-material-missing-slot 0，与 pmissing 同参），已加进 clear_product_captures
的三处名单。

#### 断言文本 diff（节选，完整见本次提交）

```diff
-assert_eq "flat control draws the fallback" "$(rval "$READINGS" fallbackRanges)" "1"
-assert_eq "flat control uploads no texture" "$(rval "$READINGS" texturesUploaded)" "0"
+assert_eq "flat control draws the fallback" "$(rval "$READINGS" fallbackRanges)" "3"
+assert_contains "flat control draws all three descriptors" "$RANGES_LINE" "ranges=3"
+assert_contains "flat control leaves every range untextured" "$RANGES_LINE" "rangeTextured=0,0,0"
+assert_eq "the global upload counter never went backwards" "$(uploads_monotonic pflat)" "yes"

-assert_eq "nothing is bound" "$(rval "$READINGS" boundSlots)" "0"
-assert_eq "no texture was uploaded for it" "$(rval "$READINGS" texturesUploaded)" "0"
-assert_eq "no texture bind happened" "$(rval "$READINGS" texturesReused)" "0"
-assert_eq "the pass still draws the geometry" "$(rval "$READINGS" drawRanges)" "1"
-bmp_diff "the fallback frame is exactly the flat control frame" \
-  "$TRACE_DIR/pmissing.bmp" "$TRACE_DIR/pflat.bmp" --max-diff-percent 0
+assert_eq "nothing is bound" "$(rval "$READINGS" boundSlots)" "2"
+assert_contains "only the injected range lost its texture" "$RANGES_LINE" "rangeTextured=0,1,1"
+assert_contains "the other two ranges still name their slots" "$RANGES_LINE" "rangeSlots=-1,1,2"
+assert_eq "the other two ranges reuse their cached textures" "$(rval "$READINGS" texturesReused)" "2"
+assert_eq "the pass still draws the geometry" "$(rval "$READINGS" drawRanges)" "3"
+bmp_diff "two runs of the fallback frame are byte-identical" \
+  "$TRACE_DIR/pmissing.bmp" "$TRACE_DIR/pmissing2.bmp" --max-diff-percent 0
```

#### 跑门

28 OK / 0 FAIL，VIEWMODEL-MATERIAL=PASS，rc=0；exe sha 跑后仍 216d7f98…。§7 的像素控制未动，
读数与上轮一致（palt vs pflat 1.1447%、ptex2 vs pflat 1.1264%、两次 pflat byte-identical），
§8 的机制断言（rc=0、missingSlotInjected=0、materialResolved 4→3、fallbackSlots=1、fallbackRanges=1、frozen）全部保留。

#### 负控（KEEP_CAPTURES=1，篡改 trace、产品不重跑）

新增门禁钩子 VIEWMODEL_MATERIAL_KEEP_CAPTURES：非空则跳过 clear_product_captures，只供负控重放；验收不设。
每例都是「改一条读数 → 期望指定断言红」，脚本 /d/_shared/…/repin-1810/nc/run-nc.sh，日志 nc/*.log：

| case | 篡改 | 结果 |
| --- | --- | --- |
| c1 descriptor 数回退 | pflat ranges=3→1、rangeTextured=0,0,0→1,0,0 | 2 红（draws all three descriptors、leaves every range untextured），26 OK |
| c2 3 range 只 1 个带纹理 | pmissing rangeTextured=0,1,1→0,0,1 | 1 红（only the injected range lost its texture），27 OK |
| c3 上传计数器倒退 | pflat 第 2 条 readings texturesUploaded 2→1 | 1 红（never went backwards），27 OK |
| c4 还原 | 无 | 28 OK / 0 FAIL |

即：descriptor 数量回退、range 纹理丢失、计数器倒退三类真回退都会被判红，re-pin 没有把断言改成恒真。

#### 本窗口未做

- §9 三条红（blendedRanges=3、alphaAppliedRanges=3、uploadSlot=-2）一行未动，本节也未跑 §9；期望 lead 另定
  （机制已查清：uploadSlot=-2 是 952ddf20 起「全槽上传」的语义，旧 pin 2 来自 279dabb5 的单槽时代）。
- 真实门全 9 节的 full PASS 仍未取（本轮只 §7/§8）；§8 之前自 435b4d5c 起欠的那次 full PASS 仍未还。

#### 3.1.3 §9 re-pin（2026-11-16 18:30 lead 准予，同一窗口还 9 节 full PASS）

lead 条件：先写机制再改数；负控要判两类真回退；同窗口跑全 9 节；exe 仍钉 216d7f98…；TRACE_DIR 相对路径。

#### 机制（每条带出处，改断言前在此并列）

1. **uploadSlot=-2 = 上传全部 resolved 槽**。main.cpp:7968-7969：
   decode 调用与读数都用同一常量 —— decodeViewModelMaterialSlots(request.materials, viewModelFlatColor ? -1 : -2, uploader)，
   随后 const std::int32_t uploadSlot = viewModelFlatColor ? -1 : -2;。native/src/viewmodel_material.cpp:344-375：
   仅当 uploadSlot == -2 或 (uploadSlot >= 0 && slot == uploadSlot) 才调 uploader。语义来自 952ddf20
   （2026-10-17「fix: complete model materials and archive validation」）；旧 pin uploadSlot=2 写于 279dabb5
   （2026-10-13），当时该处为 flat ? -1 : (slotOverride >= 0 ? slotOverride : drawnSlot)，只上传一个槽。
2. **blendedRanges=3 / alphaAppliedRanges=3 与 3 descriptor 一一对应**。门禁以 --viewmodel-material-slot 2 启动
   （gate :903）。main.cpp:7917-7931 viewModelDrawRangesFor 里 const std::int32_t slot = slotOverride >= 0 ?
   slotOverride : source.materialSlot; —— 3 个 VTX descriptor 全部取槽 2，于是每个 range 的 slot=2、materialKey=玻璃
   槽的 vtf、translucent 继承槽 2。实测 palpha trace：ranges=3 firstRange=(slot 2, 11640 vertices, textured=1
   translucent=1 alphatest=0) rangeSlots=2,2,2 rangeTextured=1,1,1；readings drawRanges=3 blendedRanges=3
   alphaAppliedRanges=3 boundSlots=1 alphaTestedRanges=0。boundSlots=1 是去重后的**槽**数（3 range 共用槽 2），
   与 §8 pmissing 的 boundSlots=2（槽 1、2 两个不同槽）不矛盾。哪个 range 走 blend/alpha：3 个都走，
   因为它们绑的是同一张 translucent 玻璃纹理；没有一个走 alpha test（alphaTestedRanges=0，该槽是 $translucent 不是 $alphatest）。
   旧 pin 1 来自单 range 时代。

#### 旧值 → 新断言 → 实测

| 断言（gate :行） | 旧 pin | 新断言 | 实测 |
| --- | --- | --- | --- |
| :912 one range uses the alpha blend state | 1 | blendedRanges == drawRanges（label：every drawn range uses the alpha blend state） | 3 == 3 |
| :915 alpha is the only blend path applied | 1 | alphaAppliedRanges == drawRanges（label：alpha is applied on every drawn range） | 3 == 3 |
| :918 the bound slot is the glass one | MATERIALS_LINE uploadSlot=2 | MATERIALS_LINE uploadSlot=-2（label：every resolved slot is uploaded）；新增 RANGES_LINE rangeSlots=2,2,2（label：the override binds the glass slot on every range） | -2；2,2,2 |

不动：alpha run rc=0、slotOverride=2、flatColor=0、resolved==slots（4）、boundSlots=1、alphaSlots>=1（2）、
alphaTestedRanges=0、translucent=1、11640 vertices。

#### 断言文本 diff（节选）

```diff
-assert_eq "one range uses the alpha blend state" "$(rval "$READINGS" blendedRanges)" "1"
+assert_eq "every drawn range uses the alpha blend state" \
+  "$(rval "$READINGS" blendedRanges)" "$(rval "$READINGS" drawRanges)"

-assert_eq "alpha is the only blend path applied" \
-  "$(rval "$READINGS" alphaAppliedRanges)" "1"
+assert_eq "alpha is applied on every drawn range" \
+  "$(rval "$READINGS" alphaAppliedRanges)" "$(rval "$READINGS" drawRanges)"

-assert_contains "the bound slot is the glass one" "$MATERIALS_LINE" "uploadSlot=2"
+assert_contains "every resolved slot is uploaded" "$MATERIALS_LINE" "uploadSlot=-2"
+assert_contains "the override binds the glass slot on every range" "$RANGES_LINE" "rangeSlots=2,2,2"
```
为什么用 == drawRanges 而不是写死 3：要抓的是「声明几个 range 就 apply 几份 blend/alpha」；写死 3 会在
descriptor 数再变时又漂一次，而 == 关系直接判「3 个 range 只 apply 1 个」这类不一致（也正是本次负控 n2/n3 要判的）。

#### 负控（KEEP_CAPTURES=1 + SECTIONS="9"，篡改 palpha trace，产品不重跑）

脚本 /d/_shared/…/full-1830/nc9/run-nc9.sh，日志 nc9/*.log：

| case | 篡改 | 结果 |
| --- | --- | --- |
| n1 全槽上传回退成单槽 | MATERIALS_LINE uploadSlot=-2→2 | 1 红（every resolved slot is uploaded），12 OK |
| n2 3 range 只 apply 1 份 blend | blendedRanges=3→1（drawRanges 仍 3） | 1 红（every drawn range uses the alpha blend state = 1, expected 3），12 OK |
| n3 3 range 只 apply 1 份 alpha | alphaAppliedRanges=3→1 | 1 红（alpha is applied on every drawn range = 1, expected 3），12 OK |
| n4 还原 | 无 | 13 OK / 0 FAIL |

n2/n3 的红是「1, expected 3」形式：期望侧取的是同一行的 drawRanges，所以「声明 3 个 range 却只 apply 1 个」
被判红而不是被写死的数字放过。

#### full PASS（本窗口，9 节全跑）

- 命令：bash viewmodel-material-check.sh，SECTIONS 未设（默认 1..9），TRACE_DIR=evidence/fast/viewmodel-material/full-1830（相对），
  日志 /d/_shared/EXEC-2026-11-15/INST1/E1-round5/full-1830/full-run.log，身份记在同目录 context.txt。
- 结果：**226 OK / 0 FAIL，VIEWMODEL-MATERIAL=PASS，rc=0**；9 节逐节全绿（§1-§6 共 185 条全绿后进 §7）。
- exe sha 跑前跑后同值 216d7f98…；跑前 dirty 计数见 context.txt（仍全是 evidence/t62-map-matrix + lead docs）。
- **435b4d5c 起欠的那次 full PASS 已还**：本轮 226 条覆盖 §1-§9，没有出现 §7/§8 之外的新红。
- §9 三条新断言实测读数：every drawn range uses the alpha blend state = 3（== drawRanges 3）；
  alpha is applied on every drawn range = 3（== drawRanges 3）；every resolved slot is uploaded contains uploadSlot=-2；
  the override binds the glass slot on every range contains rangeSlots=2,2,2。

### 3.2 minimap-hud（A1 与 A6/A7）

```
FAIL the panel handed over thirteen shapes = 1243, expected 13
     lw=ok floor=21566 faces=3480 kept=18061 fkept=2084 bands=6 runs=2332 rects=1230 drawn=4
FAIL the unflipped position is inside the panel (so this is not vacuous) = 15, expected >= 20
FAIL the mirrored position is inside the panel (so this is not vacuous) = 8, expected >= 20
```

- A1 的 1243 = `13 + 1230`（rects 并入），与 lead 记的「产品 13+1230=1243 / 13+815=828」同一口径；
  重算期望 rects 632/707（0a1aad40 后几何/材质计数变化），差 598/108 未证明是模型差，**不钉**。
- A6/A7 的 vacuity 守卫前提（面板内背景像素 >= 20）被 U3a 基底层填充破坏：实测 15 与 8，
  同组 `nothing is drawn` 仍 = 0。属语义变化，与 A1 一起交 card-ui 重定。
- A12 跳过（`skipped_controls=1`：needs `--mutation`，会构建；按主树不构建约束跳过）。

### 3.3 map-geometry（cp_snakewater_final1）

p1 PASS：24 OK / 0 FAIL。读数：

```
kept face-id pixels = 425899
cull removed = 3467 px from 16 faces (front=710 back=2757); frame cull_only=2510 pvs_only=0 union=2510
dark=64589 (back=3469 front=58679) attributable=7968 countsTotal=64589 frame=860784
product-attributable = 0.9257% of the frame (<= 2%)
```

re-pin（带理由，写进 pin 头）：0a1aad40 修 minimap floor 材质大小写、8eb6c085 修 oracle plane
stride 16→20 B，两者都改变了帧与 cull 参考背后的几何/材质口径；本轮按实测重钉
`evidence/t62-map-matrix/cp_snakewater_final1/frame-pins-p1.txt` 与 `cull-reference-p1.txt`，
不是把红改绿。实测：re-pin 后 `frame-pins-p1.txt` 与 `cull-reference-p1.txt` 被重写，但
`git diff` 为空（与已提交参考逐字节相同），所以本轮没有参考变更要提交。

p2：10/11 帧后到本窗口上限（620 s 被 TERM），无最终裁决；run 改写的 `frame-pins-p2.txt`
（11 条 sha 变化）没有 cull-reference-p2 与裁决背书，按未验证参考还原，记 WINDOW-DEADLINE。

## 4 门禁缺陷（本轮新发现，需单独修）

1. **`--reuse` 会假通过**。走到 reuse 分支后 bash 报
   `map-geometry-lighting-check.sh: command substitution: line 336: unexpected EOF while looking for matching ')'`，
   随后主循环提前结束并打印
   `MAP-GEOMETRY-LIGHTING=PASS maps=0 geometry=0 lighting=0 failed=0`
   —— **零张图却 PASS**。本轮 p1/p2 因此全部走新捕获（不用 `--reuse`）；修好前不要在汇报里引用 `--reuse` 的 PASS。
2. **hostlock 被超时 kill 后会留 stale owner**（owner=t62-mapaxes、product_processes=0、heartbeat 600 s+），
   必须 `hostlock.sh break t62-mapaxes`；死掉的 waiter 票还会卡在队列头（本次删
   `_shared/queue/<ticket>` 才放行）。kill 后先 break、再看 queue。
3. **门禁写 tracked evidence**：`evidence/t62-map-axes` 的 readings 与 `evidence/t62-map-matrix` 的
   status/readings 在运行中变脏（lockrun 报 D40/D49），收工需还原甲供输出、只提交 re-pin 的参考文件。

4. **门禁捕获路径不能用绝对 TRACE_DIR**。`product_launch` 用
   `--capture-frame "$ROOT/$TRACE_DIR/name.bmp"`（viewmodel-material-check.sh:598）拼路径，
   绝对 TRACE_DIR 会被拼成 `$ROOT//d/_shared/...`，产品静默 rc=16、无 bmp，整节读成空值红。
   `VIEWMODEL_MATERIAL_OUT` 不受影响（只做 mkdir/重定向），可以给绝对路径。

## 5 窗口与未完成

- 15:10–16:20 一个窗口：material 用满 1100 s 只到 §8/§9 边界（§9 另跑一次补齐），minimap 两门完成，
  map-geometry 只完成 snakewater p1，p2 与其余四图超窗，companion 未开。
- companion 待办不变：从 `E:/demo测试/pl_upward_f12/POV/autorecord_2026-08-08_21-43-59.dem`
  （sha256 `14b4943a8d7e866677091bb5a4843882bfed1105bc5ef27d955bbc4cd3ad00e8`）重导 witness，
  要求 observations >= 1000、witness ticks derived >= 3；上轮 36 FAIL/43 OK 属结构性红，断言未动。
- t2-visual-matrix 三红与 A1 口径维持第 4 轮分流，待语义 owner；本轮未复跑。

## 6 复现

```bash
# material（整门；分段补跑 §9）
bash viewmodel-material-check.sh
VIEWMODEL_MATERIAL_SECTIONS=9 bash viewmodel-material-check.sh

# minimap（SCRATCH/OUT 必须 /d/ 形式，D:/ 会被拼成 /d/.../D:/...）
bash minimap-check.sh
MINIMAP_CHECK_SCRATCH=/d/_shared/EXEC-2026-11-15/INST1/E1-round5/minimap-hud-scratch \
MINIMAP_CHECK_OUT=/d/_shared/EXEC-2026-11-15/INST1/E1-round5/minimap-hud \
MINIMAP_CHECK_DEMO_SUNSHINE="E:/demo测试/cp_sunshine/STV/SUNSHINE.dem" \
  bash minimap-hud-check.sh

# map-geometry（勿用 --reuse，见 §4.1）
bash /d/TF2_Native_Worktrees/_shared/hostlock.sh run t62-mapaxes env HOLD_LOCK=1 MAP_AXIS_OUT=evidence/t62-map-axes \
  bash map-geometry-lighting-check.sh --only=cp_snakewater_final1 --poses=p1 --re-pin="<why>"
```
