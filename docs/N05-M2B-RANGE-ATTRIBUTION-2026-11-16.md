# N05 第二步续：1181 px 的逐 range 归因（R1b 仪器）— 2026-11-16

- 上位文档：`docs/N05-M2B-DEPTH-ATTRIBUTION-2026-11-16.md`。那里已经证明：该帧深度缓冲在隔离三角形 19539 的 1290 px 投影范围内**没有任何一处记录到它自己的深度**——109 px 恰好是清除值 1.000000，1181 px 上是比它**更远**的表面（4..112 quanta）。所以"更近的遮挡物在深度测试里赢了"被否证。
- 本轮要问的是那句留下的"谁画的"：**这 1181 px 是被哪一次 draw 的深度写覆盖的？那 109 px 又有没有任何一次 draw 写过？**
- 结论：**1181 px 由 259 次 entity range draw 中的 3 次写入，零遗漏零多余**；**109 px 没有任何一次 entity draw 写过（0/109）**。所以 109 的边界不是"某个实体 draw 挡住了它"：entity 域里根本没人碰那 109 px。这直接支持 M2B 的候选 A/B（world 域里更晚的批次 / 水面 pass），排除"实体模型实例把颜色和深度画上去"。
- 仪器：新加 `--capture-range-{depth,window,skip}` 三个开关（`native/include/native_renderer.h:1687-1698`、`:1743`；实现 `native/src/native_renderer_capture.cpp:168-300`；调用点 `native/src/native_renderer_frame.cpp:986`、`:1037`），**默认不武装，不改变任何渲染状态**；下面的 A/B 逐字节证明。
- **本轮仪器不是零代码改动**（与第一步的 `--capture-depth` 不同）：52 行头文件/实现 + 实体 range 的 materialSlot 透传 + main.cpp 的三个开关。它在隔离 worktree `D:/TF2_Native_Worktrees/t105-e5-r1b`（分支 `work/card-maps-r1b`）上，未进主树。

## 1 仪器：每次 draw 后回读同一帧的深度

| 项 | 值 |
|---|---|
| 开关 | `--capture-range-depth <prefix>`（必须配 `--capture-frame`）、`--capture-range-window x,y,w,h`、`--capture-range-skip <drawn-ordinal>` |
| 解析 | `native/src/main.cpp:2726-2755`（`parseCaptureRangeDepthSwitch`）、分支 `:4179-4203`、校验 `:4311-4320`（缺 frame → return 13） |
| 武装 | `native/src/main.cpp:14300-14303`（`setRangeDepthPrefix/Window/Skip`，在 `requestFrameCapture` 之前） |
| 采样点 | ① 实体 pass 开始前一次（`phase=entity-start`，基线）；② 每次 `DrawInstanced` 之后一次（`phase=draw`）——`native/src/native_renderer_frame.cpp:986`、`:1033-1039` |
| 回读方式 | `CopyResource` 整个 D24S8 到 staging + `Map`，取 `& 0x00FFFFFF` / 16777215 转 float（`native_renderer_capture.cpp:229-246`） |
| 平面差分 | 与上一次 capture 的平面逐像素比较（`:258-283`）：`changed`=与上一平面不同的像素数，`wrote`=上一平面是清除值而现在不是（"这个 range 亲自写了这个像素"），`near`=当前近平面像素数，`bbox`=`changed` 的外接框 |
| 产物 | 每 capture 一个 P5 16-bit 大端 PGM：`<prefix>-<index>-<phase>.pgm`（`native_renderer_capture.cpp:284-289`） |
| trace | `range-depth: index=%d phase=%s range=%d blended=%d slot=%d changed=%d wrote=%d near=%d bbox=%d,%d,%d,%d window=%u,%u,%u,%u bytes=%llu wrote_pgm=%d depthStateLastBind=%s`（`:290-296`） |
| 窗口 | 宽或高为 0 = 全平面；否则只回读该窗口（`:190-205`）。**拷贝仍是整平面**，裁剪在 CPU 上做（原因见 §6a） |
| 负控 | `--capture-range-skip n`：`n` 是**按绘制顺序数的 range 序号**，命中时跳过那次 `DrawInstanced`（`:999-1002`）。它会**故意改变画面**，只在探针武装时生效，不是模式 |
| 失败 | 有前缀但一个平面都没写成，或任一 capture 失败 → 退出码 **16**（`:14496-14500`） |

两个编号必须分清：

- **plane index**（`index=`，也是文件名里的数字）= 第几次 capture，从 0 开始，`0` 固定是 `entity-start`；
- **range**（`range=`）= range 在 `entityModelDrawRanges_` 里的源序号，`entity-start` 为 `-1`。

跳过负控时两者会错开：g9 里 `index=93` 是 `range=92`、`index=94` 是 `range=94`，源序号在被跳过处留一个缺口，所以 plane 总数比 g7 少一个。

## 2 不变量（每一轮都核）

| 项 | 值 |
|---|---|
| worktree / 分支 / HEAD | `D:/TF2_Native_Worktrees/t105-e5-r1b` / `work/card-maps-r1b` / `0ada1d81` + 本轮 5 文件未提交改动 |
| exe（实际执行） | `native/build-nmake/tf2_demo_native.exe`，sha256 `8a9beb00b257cde3cef6e8522a6793b8d7c5a0b2a459034f4bbdf45748f18eb4`，3,061,760 B（运行前后 `exe_stable=YES`） |
| demo | `E:/demo测试/cp_sultry_b8a/STV/f63a7a2040379d7d6a8414f76bbb81dc_matcha-20260703-1442-cp_sultry_b8a.dem`，sha256 `032bbd5ab719012cc2a7b4858f0a5d4e1ba882b4cdae1d86f2b966a3578c4374`（85,506,321 B） |
| tick / 相机 / 掩码 | `--capture-tick 0`、`--debug-camera -600 -600 950 20 45`、`--world-sky`、`--world-face-trace --world-clip-capture --world-triangle 19539 --world-magenta --no-world-cull` |
| 帧几何 | 1264×681；BMP 2,582,406 B；深度 PGM 1,721,568 B，sha256 `07f513c4868ad9bffcd1dc10c5d4047170c12b8cc92c35680c53ee8684aa1834` |
| 输入隔离 | `verify-iso.sh` 播种的临时 settings.ini/ui.ini（`--settings-file`、`--ui-settings-file`），tmpdir 退出即删 |
| 主机锁 | 每轮 `hostlock.sh run card-maps`；四轮各自 ACQUIRED/RELEASED |

## 3 四轮读数

| 轮 | 模式 | 参数差异 | BMP sha256 | range 平面 | 判据 |
|---|---|---|---|---|---|
| g7 | `g7-range` | 全平面 | `757f5aa2…b40` | 259 | 必须**等于** M2b/M2a 参考帧 ⇒ 探针零渲染影响 |
| g8 | `g8-range2` | 与 g7 完全相同 | `757f5aa2…b40` | 259 | 每个平面逐字节相同 ⇒ 确定性 |
| g9 | `g9-skip 93` | 跳过绘制序号 93 | `45c8236a…04b` | 258 | 必须**不同**，且不等于参考帧 ⇒ 探针不是惰性的 |
| g10 | `g10-window 880,330,76,110` | 窗口=足迹 bbox 外扩 4 px | `757f5aa2…b40` | 259 | 窗口路径与全平面同结论 ⇒ 后续扫描可用小窗 |

- g7：`range_pgm_files=259`、`range_depth_trace_lines=259`、pass 平面 8 个、`stray_planes_in_cwd=0`、trace 回读 `window=0,0,1264,681`。
- g8：与 g7 逐文件 `cmp` **259/259 全等**（`identical_planes=259 differing_planes=0`）；BMP 与 `.pgm` 深度平面 sha 也全等。平面 body 摘要（只哈希 per-plane sha 列表，与 tag 无关）：g7 与 g8 都是 `ec8d56e751d6edbbe82502d7bd50f1151cd148b941664d61e2b11254fec7b009`。
- g9：`range=93` 的 trace 行数 = **0**，平面少一个（`258-draw.pgm` 不存在），body 摘要 `419d9c8b087dea8d8d2a69e0847f716f41bceec9b6485a58a33039ef1bdd26ec`。`bmpdiff.py` 对 g7 帧：**1723 px 不同（0.2002%），max_channel_delta=166**——正好等于被跳过那次 draw 的 `changed=1723`（见 §5）。
- g10：`range_window_reported=880,330,76,110`（由 trace 回读，不是假设）、首平面 `bytes=16720`=76×110×2（PGM 文件 16,736 B = 16 字节头 + 16,720）、`entity-start` 在该窗口内 `near=0`（窗口里此时还没有任何深度）、body 摘要 `e1ec92ad59e3750ba9e61cba816cf3eaf5d69dd48c692e1f4ce0ec7e7c84d5df`。

说明：`g7.run.txt` 里 `range_manifest_sha256` / `range_body_sha256` 两个字段是**旧版脚本**写的单行哈希（见 §6c），上表 body 摘要是在同一批平面文件上用修好的循环重算的；确定性结论不依赖这两个字段，依赖的是 259/259 的逐字节 `cmp`。

## 4 归属：1181 px 是谁写的

足迹（中心采样，与 M2B 同一口径）：closure 断言 `109 + 1181 == 1290` 成立；足迹 bbox `x=884..949, y=334..432`，ribbon bbox `x=884..947, y=338..426`。

| 问答 | 读数 |
|---|---|
| 基线（entity pass 之前）在足迹上有深度吗 | `ribbon_near=0`、`interior_near=0`（`entity-start` 全帧 `near=110`，这 110 px 全在足迹**之外**：更早的 entity-marker LINELIST 等绘制） |
| 1290 px 里有多少被某次 entity draw 亲自写过 | 1181（`wrote` 口径），**全部在 interior**；ribbon 是 **0/109** |
| 写 interior 的 draw 有几个 | **3** |
| 结束态 | `ribbon_far=109`（仍是清除值）、`interior_far=0` |
| 与整帧 `--capture-depth` 平面是否一致 | `DEPTH-MATCH ribbon=TRUE interior=TRUE`（两组逐像素） |

三个 writer（`range-analyze.py` 输出，原文见 `runs/r1b-analysis-g7.txt`）：

| plane index | 绘制序号（range） | blended | materialSlot | 在足迹内新写 | 全帧 changed | 全帧 wrote | 全帧 near | bbox |
|---|---|---|---|---|---|---|---|---|
| 94 | 93 | 0 | 0 | **1131** | 1723 | 1723 | 113,619 | 582,251,941,441 |
| 88 | 87 | 0 | 0 | **47** | 535 | 535 | 101,131 | —（bbox 在全帧另一处，足迹内只有 47 px） |
| 109 | 108 | 0 | 1 | **3** | 17,546 | 14,678 | 169,231 | 345,0,1078,338 |

1131 + 47 + 3 = 1181，与 interior 的 1181 px 完全相等，没有第四者、没有剩余。

旁证（不参与判定，但说明探针不是只对这三个 range 敏感）：全帧 259 个 capture 中有 **140** 个 `changed>0`，合计 `changed=235,287` / `wrote=223,656`；**这 140 个全部是 `blended=0`**（`entity:write-all`），没有一个半透明 range 写深度。最大写入者是 `index=84 range=83 slot=0 changed=41,076`、`index=82 range=81 slot=0 changed=28,093`。

## 5 负控：跳过一次 draw，画面在哪里变

跳过绘制序号 93（上面 1131 px 的那一次）后：

- 平面少一个，`range=93` 的 trace 行为 0；
- BMP 与 g7 相差 **1723 px**，最大通道差 166；
- 该 draw 在全帧的 `changed` 正好也是 **1723**、`wrote=1723`。

也就是说画面变化**精确落在这一次 draw 的覆盖区**，一处不多一处不少。这是"足迹跟着 DRAW 走，不跟着 tick/相机走"的负控：如果探针记录的是别的东西（例如整帧或某个固定 pass），跳过一次 draw 不可能刚好改掉它自己的 1723 px。

## 6 三个工程事故（都是"仪器报告成功、读数却是假的"）

**a. 部分框拷贝 depth-stencil 返回未定义数据。** 第一版为了省带宽用 `CopySubresourceRegion` 只拷窗口盒子，运行时不报错，但目标内容未定义——第一次全平面运行把**某一次 draw 读成写了整帧 860,784 px**。改成 `CopyResource` 整平面拷贝 + CPU 裁剪（`native_renderer_capture.cpp:206-209` 记着原因）。窗口仍然有用：它只决定 CPU 要保存/比较多少像素（g10 每平面 16,720 B 而不是 1,721,568 B），不决定拷贝粒度。

**b. `arguments[i]` 与 `arguments[++i]` 写在同一表达式里（未定序）。** 原调用是 `parseCaptureRangeDepthSwitch(arguments[i], arguments[++i], …)`。MSVC 先求值 `++i` 那一侧，helper 的 `switchName` 收到的是**值**（`"93"` / `"880,330,76,110"`），于是 `switchName[16]` 越界、两个分支都不匹配，落到兜底 `*prefix = value`。症状：g9/g10 的 rc=0、BMP 正确、trace 也打了 259 行，但 range 平面一个都没落在指定目录——**518 个 pgm 被写进 worktree 根目录**，文件名就是 `93-0-entity-start.pgm` 与 `880,330,76,110-0-entity-start.pgm`。g7 之所以看不出来，是因为它只有 `--capture-range-depth <prefix>` 一个开关：兜底写回的正好是正确的前缀。修法：先把开关名拷成 `const std::wstring rangeSwitch = arguments[i];`，再 `if (rangeHasValue) ++i;`，最后用 `rangeSwitch, arguments[i]` 调用（`main.cpp:4187-4203`，注释里留着原因）。脚本侧同时加了三道守门（§7）。

**c. 嵌套管道抽干 `while read` 的 stdin（MSYS）。** `sha256sum "$f" | cut -d' ' -f1` 放在被管道喂的 `while read -r f` 循环里时，内层命令消耗了循环自己的 stdin，**循环只跑一次**（实测 3 个文件只输出 1 行、`| wc -l`=1）。于是旧脚本算出的 `range_manifest` / `range_body` 全是**单行哈希**（g7/g8/g9 三者同为 `6eec0cbe…`，空流时是 `e3b0c442…` 空串摘要）。修法：改 glob `for` 循环（不共享 stdin），body 摘要只哈希 per-plane sha 列表。**教训：一个"两次运行数字相同"的字段，如果它是这么算出来的，就不能当确定性证据**——真正的确定性证据是逐文件 `cmp`。

## 7 脚本侧的守门（run-r1c.sh）

`D:/_shared/EXEC-2026-11-15/INST2/m2c-r1/run-r1c.sh`，模式 `g7-range`/`g8-range2`/`g9-skip <n>`/`g10-window <x,y,w,h>`。除 BMP 期望值外，本轮新增三条：

| 守门 | 触发 | 退出码 |
|---|---|---|
| `stray_planes_in_cwd>0` | 相对前缀漏进 cwd（§6b 的症状） | 8 |
| `range_window_reported` ≠ 传入窗口 | 窗口参数没真的武装 | 9 |
| `SKIP≥0` 而 trace 仍出现该 `range=` | 负控没生效 | 10 |

`range_window_reported` 与 `range_skip_seen` 都从 trace **回读**，不是从 argv 假设。

## 8 边界（这一轮没有证明的）

1. **三个 writer 的模型/材质身份没有点名**。只有 `slot=0/0/1` 与绘制序号；要落到具体模型名需要 entity range → model/texture 的 trace（本轮没做）。所以只能说"entity 域的 3 次 draw"，不能说"某个具体角色/道具"。
2. **只覆盖 capture 隔离路径 + 一个 tick + 一台相机**（`--capture-tick 0`、`-600 -600 950 20 45`）。常规渲染路径、其他 tick、其他视点都没有测。
3. `entity-start` 基线那 `near=110` 的 110 px 全在足迹外，本轮没有展开它们是谁画的（与 109 px 问题无关，但记在这里免得被误读成足迹内基线非零）。
4. `ribbon 0/109` 说的是 **entity 域没有任何一次 draw 写过那 109 px**；"world 域那次隔离绘制没写"是 M2B 的结论，两者合起来才排除所有路径——本轮不单独扩大 M2B 的结论。
5. 探针本身会花时间与内存（每 capture 一次整平面 `CopyResource` + CPU 比较）：259 次 capture 的 g7 全平面模式是**取证用途**，不是可以常开的模式；g10 说明扫描时该用窗口。

## 9 复现

```
# 构建（隔离 worktree，~30 s 增量；不要动主树 exe）
bash /d/_shared/EXEC-2026-11-15/INST2/m2c-r1/build-r1b.sh

# 四轮（每轮都要主机锁，串行）
bash /d/TF2_Native_Worktrees/_shared/hostlock.sh run card-maps \
  bash /d/_shared/EXEC-2026-11-15/INST2/m2c-r1/run-r1c.sh g7 g7-range -1
bash /d/TF2_Native_Worktrees/_shared/hostlock.sh run card-maps \
  bash /d/_shared/EXEC-2026-11-15/INST2/m2c-r1/run-r1c.sh g8 g8-range2 -1
bash /d/TF2_Native_Worktrees/_shared/hostlock.sh run card-maps \
  bash /d/_shared/EXEC-2026-11-15/INST2/m2c-r1/run-r1c.sh g9 g9-skip 93
bash /d/TF2_Native_Worktrees/_shared/hostlock.sh run card-maps \
  bash /d/_shared/EXEC-2026-11-15/INST2/m2c-r1/run-r1c.sh g10 g10-window -1 880,330,76,110

# 归属
python /d/_shared/EXEC-2026-11-15/INST2/m2c-r1/range-analyze.py \
  D:/_shared/EXEC-2026-11-15/INST2/m2c-r1/runs/g7
# 帧差（负控）
python /d/_shared/EXEC-2026-11-15/INST2/m2c-r1/bmpdiff.py runs/g7.bmp runs/g9.bmp
```

## 10 证据

| 内容 | 路径 |
|---|---|
| 四轮 BMP / 深度 PGM / trace / 日志 / 读数 | `D:/_shared/EXEC-2026-11-15/INST2/m2c-r1/runs/g7.*`、`g8.*`、`g9.*`、`g10.*`（另有 `<tag>-pass-*.pgm` 8 个、`<tag>-range-*.pgm` 258–259 个） |
| 归属读数 | `D:/_shared/EXEC-2026-11-15/INST2/m2c-r1/r1b-analysis-g7.txt` |
| 运行脚本 / 构建脚本 / 分析器 | `D:/_shared/EXEC-2026-11-15/INST2/m2c-r1/{run-r1c.sh,build-r1b.sh,range-analyze.py,bmpdiff.py,pgmstats.py}` |
| 锁记录 | `D:/_shared/EXEC-2026-11-15/LOCK-NOTES/card-maps.txt` |
| 代码（worktree 未合并） | `native/include/native_renderer.h`、`native/src/{main.cpp,native_renderer_capture.cpp,native_renderer_entity.cpp,native_renderer_frame.cpp}` |
