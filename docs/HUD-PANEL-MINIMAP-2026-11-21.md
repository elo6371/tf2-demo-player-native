# HUD 面板与 minimap：缺的是哪一层（2026-11-21，task-31）

卡片：task-31「HUD 目视缺陷：面板只画纯色块；--hud-minimap 无绘制」。
写域：native/src/main_hud.cpp（本卡只改这一个源文件 + native/docs/hud-supported-formats.md 的规格表），native/src/main.cpp 一行未动。

## 0 结论

1. **字体不缺，缺的是合成**：8×8 字形图集与它的渲染分支一直是活的（native/include/hud_font.h、native/src/native_renderer_frame.cpp:2191-2243，`--hud-text-test` 与 live readout 都在用）。缺陷是面板路径从不发 `HudPrimitiveKind::Text`：`hudApplyRects` 只调 `setHudRects()`，那个函数（native/src/native_renderer_frame.cpp:2104-2121）把每个 `HudDrawRect` 转成恰好一个 `Rect, filled=false` 原语。已修：标签按 `%token%` 展开后作为 Text 原语压在面板边框上（§2）。
2. **贴图确实没有实现，但不是"材质没找到"**：布局里的 `image` 只被解析（native/include/hud_import.h:175 自述 "declared only: this build rasterises no image"），面板路径没有任何 Image 原语；渲染层的 Image 通道存在，但只有一个 `hudImageView_` 槽（被 map overview 的底图占用，native/src/native_renderer_frame.cpp:2190），没有面板图片的上传路径。这条**不修**，明确记为未实现（§6）。
3. **minimap 是两层原因**：(a) capture 跑法里 `hudApplyLive` 在 `evidenceRun && !hudLive && !mapOverviewOpen()` 时提前 return，`hudMinimapCompose` 根本走不到 ⇒ `--hud-minimap` 单独给是死开关；(b) 就算走到，`cp_snakewater_final1` 不在 `kMinimapMaps` 的 7 张表里（native/include/minimap.h:273-283）⇒ `reason=bounds-not-tabulated`、按 gate A11 一像素不画。修 (a) 后两条都成立：snakewater 只多出 readout，upward 面板整块画出来（§4）。
4. **默认路径零像素变化**：同构建单变量 A/B——未含本卡修复的默认帧与含修复的默认帧 md5 都是 `bdeca8c24ec4fa22113c6b55d7419430`（§4.4）。

## 1 对读记录（docs/REFERENCE-REPOS-2026-11-21.md §5）

- §5 为 task-31 指定：`source-sdk-2013` 的 `vgui`/`hud`（读 `%token%` 展开与面板绘制语义）+ `SteamTracking/GameTracking-TF2`（HUD 资源与本地化字符串）。
- 本机没有这两个库的副本（`ops_glob` 在 `D:/` 找 `source-sdk-2013`、`GameTracking-TF2`、`TF2-Base` 目录，0 命中）⇒ **未对读外部源码**。本报告的每一条判据都来自本仓行号与本仓像素读数。
- 本仓证据：native/include/hud_import.h:168-226（面板字段与 `HudScreenRect.panel`）、native/src/native_renderer_frame.cpp:2182-2270（原语分派与字形四边形）、native/docs/hud-supported-formats.md（支持矩阵）、docs/N14-U3A-BASEMAP-FLOOR-LINEWORK-2026-11-15.md:84（gate A11：未列表地图必须画 0 并报具名原因）。
- 互证：`.scratch/hudfix/FINDINGS.md` 是同一修法在 **1920×1080** 下的另一组读数；本报告在 **1264×681** 下独立复算，像素数逐项吻合（§4.3）。

## 2 根因（行号）

### 2.1 文字：合成侧，不是字体

- 修前 `hudApplyRects`（native/src/main_hud.cpp:333 起）只把 `HudDrawRect` 交给 `setHudRects()`；`HudDrawRect` 没有文字字段，所以文档里的 `labelText` 从解析完就没人读。面板索引 `HudScreenRect.panel`（native/include/hud_import.h:216-226）此前只用来取调色板。
- 修后：`hudPanelPrimitives`（native/src/main_hud.cpp:385）对每个可见 rect 先发边框块（`Rect, filled=false`，与旧读数逐像素一致），再在文档给了标签时发一个 Text 原语。

### 2.2 贴图：ImagePanel 没有绘制路径

- `HudPanel::image`（native/include/hud_import.h:175）只记录不消费；`main_hud.cpp` 里没有任何 `HudPrimitiveKind::Image` 的产生点（grep 0 命中；Image 原语只出现在 map_overview.cpp:792）。
- 渲染层即使收到 Image 也要求 `hudImageView_` 非空（native/src/native_renderer_frame.cpp:2190），而那个槽是 map overview 的底图单槽。

### 2.3 live readout 会清掉面板

- `setHudPrimitives()` 整表替换。修前 `hudApplyLive` 只发布自己的文字 ⇒ 任何 readout 在跑的跑法都会把 `hudApplyRects` 刚设的面板删掉。修后两处都发"面板在下、readout 在上"的合并表（native/src/main_hud.cpp:460 的 live 分支、:1686 的 republish 分支）。

### 2.4 `--hud-minimap` 在 capture 跑法里是死开关

- 修前：`const bool suppressed = hudOff || (evidenceRun && !hudLive && !mapOverviewOpen());` —— `--hud-minimap` 的跑法也是 evidenceRun，于是 `hudApplyLive` 在 :1505 之前就 return，`hudMinimapCompose`（同文件 :1213 起）永远不被调用，尽管 main.cpp 的 `minimapHudSetFrame` 已经把这一帧推了进来。
- 修后：`... && !tf2::native::minimapHudFramePushed()`（native/src/main_hud.cpp:1505-1507）。判据用的是"这一跑到底推没推帧"，而不是"开关名字像不像"，与 map overview 的先例同构。

### 2.5 未列表地图（设计如此，不改）

- `kMinimapMaps` 只有 7 张（native/include/minimap.h:273-283），`minimapFindMapRow` 对其它图返回 null，compose 报 `bounds-not-tabulated` 并画 0（native/src/main_hud.cpp:1229-1233）。`cp_snakewater_final1` 就是这一类。

## 3 修法

| 改动 | 位置 |
| --- | --- |
| `HudPanelValues` 缓存 + `HudPanelDraw` 计数器 | native/src/main_hud.cpp:345-357 |
| `hudPanelExpandLabel`：`%Health%`、`%Clip%`/`%Clip1%`、`%Team%`、`%Class%` 展开；其余 token **原样保留**并计 `unresolved` | :365-383 |
| `hudPanelPrimitives`：边框不变 + 每个有标签的面板一个 Text（高度 ≥24 用 scale 2，否则 1；超宽先缩再截，计 `truncated`；`textAlignment=center` 居中，其它保持 4 px 左边距） | :385-457 |
| `hudApplyRects`：发布面板原语（live 分支把 readout 叠在上面） | :460-516 |
| `hudApplyLive`：每帧在 suppression 之前填缓存（面板值与 readout 同源，:1482-1485）；suppression 放行 minimap 跑法（:1505-1507）；republish 时"面板 + readout"一起发（:1686） | :1462-1690 |

门禁算术未变：Text 原语不增加 `hudDrawnRects_`（native/src/native_renderer_frame.cpp:2268 只数 Rect），`drawn == visible` 与 region=union 的断言不受影响。规格表同步：native/docs/hud-supported-formats.md:132-133（`labelText` 已渲染、token 展开与截断规则；`font` 忽略、`image` 不渲染）。

## 4 读数（1264×681，tick 3000，隔离 settings）

### 4.1 面板文字（cp_snakewater_final1 + 门禁 fixture）

- `--hud-root evidence/hud/fixtures/good --hud-apply --hud-on`：md5 `173ce305c5ddde1188508c6b17a7b6b5`（无标签的旧读数在旧构建上是 `a7038d4d...`）。
- trace：`hud: panels drawn=5 labelled=1 glyphs=8 truncated=0 unresolved=1 unknown=0` → 值到达后 `... glyphs=1 ... unresolved=0`。
- 画面：绿色面板中央出现白色 `1`（该 demo 的 `readPlaybackHud` 读数就是 health=1），其余面板保持边框块。像素级别：1920×1080 复核组测得"修前/修后同 fixture 只差 76 px，bbox 689,912→700,925 = 一个字形格"（`.scratch/hudfix/FINDINGS.md` §3.1）。

### 4.2 minimap：未列表地图必须画 0（gate A11）

- snakewater + `--hud-minimap`：md5 `8870d7fde33623191daaf4e11977b5e1`，与默认 `bdeca8c2...` 差 **4140 px**，bbox (24,24)-(724,82) —— 只有顶部 readout 文字，**面板区域 0 px**。
- trace：`minimap: hud state=unavailable map=cp_snakewater_final1 reason=bounds-not-tabulated panel=0,0,0,0 ... drawn=0 shapes=0`。

### 4.3 minimap：已列表地图真画出来（pl_upward_f12）

- base3000 `3a123d5156b07c342031c04ed69fa8dd`；+minimap `a57773455ccb831c7d7f85cac5ba9ad4`，差 **28572 px**，bbox (24,12)-(1252,172)。
- trace：`minimap: hud state=ready map=pl_upward_f12 reason=ok panel=1092,12,160,160 scale=0.022321 ... cps=4 drawn=4 shapes=1126 viewerInside=1 arrowDots=3`。
- 1:1 局部：`.scratch/n17-review/phasec/crop-upminimap2.png`（暗底、细边框、楼层线稿、4 个白圈控制点、绿色朝向箭头）。
- 跨分辨率互证：1920×1080 组同图差 **28572 px**（`.scratch/hudfix` 读数）——HUD 尺寸是像素常量（8×8 字形、160×160 面板），所以绝对像素数不随分辨率变，这也是两组独立读数对得上的原因。

### 4.4 默认路径单变量 A/B

| 构建 | 抓帧 | md5 |
| --- | --- | --- |
| 未含本卡修复（`git show HEAD:native/src/main_hud.cpp`） | `def3000-prewip` | `bdeca8c24ec4fa22113c6b55d7419430` |
| 含本卡修复（工作树） | `t30-def3000` | `bdeca8c24ec4fa22113c6b55d7419430` |

⇒ 不加 `--hud-*` 时一个像素没动（`setHudEnabled((settings().enabled && panels.drawn > 0) || g_hudLiveActive)` 在默认跑法里仍然是 false）。

## 5 复现

```bash
cd /d/TF2_Native_Worktrees/t105-integrate
# 面板文字（snakewater + fixture）
PHASEC_TICK=3000 bash .scratch/n17-review/phasec/phasec-capture.sh hudcur-on native/build-nmake/tf2_demo_native.exe \
  --hud-root evidence/hud/fixtures/good --hud-apply --hud-on
# minimap：未列表 / 已列表（hudc-capture.sh 比 phasec-capture.sh 多一个 demo 参数）
UP='/e/demo测试/pl_upward_f12/STV/a9630c6557468b24f0b8c2e7dab38ed8_match-20261030-2145-pl_upward_f12.dem'
PHASEC_TICK=3000 bash .scratch/n17-review/phasec/phasec-capture.sh hudcur-minimap native/build-nmake/tf2_demo_native.exe --hud-minimap
PHASEC_TICK=3000 bash .scratch/n17-review/phasec/hudc-capture.sh upbase3000    "$UP"
PHASEC_TICK=3000 bash .scratch/n17-review/phasec/hudc-capture.sh upminimap3000 "$UP" --hud-minimap
# 读数
grep -a 'hud: panels\|minimap: hud state=' .scratch/n17-review/phasec/hudcur-on.trace.txt
grep -a 'minimap: hud state=' .scratch/n17-review/phasec/upminimap3000.trace.txt
```

## 6 未闭环（明确记录，不在本卡强做）

- **面板贴图仍未实现**：需要一条"布局 image → VMT/VTF → 纹理上传"的通道；当前渲染器只有一个 `hudImageView_` 单槽且被 map overview 底图占用（native/src/native_renderer_frame.cpp:2190）。HealthIcon / Crosshair 这类 ImagePanel 仍只占几何、不画图。
- `font` 声明被忽略：所有标签统一用内置 8×8 图集与 scale 2/1，文档里的 `ClientTitleFont` 等名字不参与栅格化。
- `%token%` 只覆盖 Health / Clip / Clip1 / Team / Class；其它 token 原样打印并计入 `unresolved`（例如 `%MaxHealth%`、`%ubercharge%`）。
- 本卡没有改 `--hud-scale` 与窗口尺寸相关的 DPI 行为（U3b 的既有范围）。
