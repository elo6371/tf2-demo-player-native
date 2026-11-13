# Phase F 完整任务报告（2026-11-13）

- 报告文件：`docs/PHASE-F-REPORT-2026-11-13.md`
- 共享副本：`D:/_shared/EXEC-2026-11-13/ROUND-REPORT-2026-11-13-PHASE-F.md`
- 覆盖范围：2026-11-13 Phase F 第一批，F1-1 到 F2-3
- 工作树：`D:/TF2_Native_Worktrees/t105-e5`，分支 `candidate/e5`
- 起点：`9deed76`；终点：`68a206a`
- 现场：porcelain 0 行，hostlock FREE，product_processes=0，无 push、无 tag、无历史改写
- **没有跑 15 步发布链**；本报告中的所有“PASS”都只对它绑定的门禁/身份成立

---

## 1. 一页纸结论

这一批把 Phase F 的前五个 F1 任务、小地图 F2-1、`$nocull` census F2-3，以及 F2-2 的 52-probe 启动扫描和 8-probe 域批次做完了。共 **18 笔任务提交**（不含本报告），其中 11 笔是产品/门禁代码，7 笔是文档与批次记录。

关键结论：

1. **I-31 不是产品回归。** 六个保留 exe（`d40363b7`、`6c06c6ad`、v5、v6、v8、v9）在同一 demo、同一 argv 下全部输出 `firstPacketTick=0`。`5ee9642`（2026-10-24）已按设计把产品字段改为 data-entry origin；门禁却拿它和 census 的 signon tick `29368` 比较。修门禁，不修产品。
2. **I-32 关闭。** 新增门禁注册表与守卫：每个 `*-check.sh` 要么在 `verify-fast`/`verify-all`/`verify-release` 的非注释行里，要么在 `docs/GATES-UNREGISTERED.md` 有具名理由。Sultry 门禁已挂进 `verify-all` 第 29 步。
3. **I-24 关闭。** `material-coverage` 的 6 个 PVS 派生 pin 按 `10d1355` 的因果重钉；新增 `inputTriangles=242109` 和 `drawn + hidden == input`。修前 6 红、修后 `MATERIAL-COVERAGE=PASS`（100 断言）。
4. **I-29 关闭（headless 部分）。** `quiet-capture-check.sh` 默认不再打开音频设备；可听 live pair 改成显式 `--device`。新增 `audio-keys-check.sh`，用真实 `WM_KEYDOWN` 驱动 M / `[` / `]`，验证 ui.ini 持久化和跨重启读回。
5. **I-23 增加像素证据。** 找到能看见 `glasswindow001a_649_-1016_704` 的相机；rescue on/off 改变 **84,965** 像素，5 个 hole-trace 点归因到该材质，1 个不透明 BRICK/WALL028 作为对照。
6. **I-20/873 的 alpha 解释成立。** 1143,132 / 1110,136 是 TREELINECARD 的 `255,255,255, alpha=0` 透明 texel；873,366 是 METALGRATE 的 `0,0,0, alpha=0`。`--world-texel` 进一步分离出：1110 的 albedo 0 是 alpha=0 + vertex colour 0；849 的 texel 是 `84,83,81` 但 albedo 0，原因是 vertex colour 0；919 的 texel 与 albedo 都是 `210,208,197`，问题在 face-trace UV 与实际 pass 不一致。849/919 保持 OPEN。
7. **小地图产品化落地。** E9 补丁 4 文件落树，`--hud-minimap` 默认关；`minimap-hud-check.sh` 96 断言 PASS，含投影 y 翻转、x 镜像、yaw 旋转、旧 exe 负控和未列表地图。七图只有 Upward/Sunshine 有帧，其余五图 NOT VERIFIED。
8. **`$nocull` census 完成（只读）。** 七图 BSP pak 中只有 Bagel RC11 有可见候选（3 棵树、tarp、detail-sprite），其余六图只有不可见 `tools/toolstrigger`。Bagel oak 固定相机的 cull on/off 差异 689,683 像素，但这是全局 world cull，不是 per-material 控制；没有改 world renderer，正例仍缺。
9. **F2-2 第一批扫描完成。** 52 个 `*_probe.exe` 以 `--self-test` 15 s 超时运行：29 个 rc=0，23 个是 usage/缺输入，没有空输出崩溃。第二批又用真实 map/demo/TF-root 跑了 8 个带参数 probe，8/8 rc=0；剩余 12 个带参数 probe 未跑。它不是完整域验收。

---

## 2. 现场与身份

| 项 | 值 |
|---|---|
| 工作树 | `D:/TF2_Native_Worktrees/t105-e5` |
| 分支 | `candidate/e5` |
| 起点 | `9deed76` |
| 终点 HEAD | `68a206a` |
| 最终 exe | `native/build-nmake/tf2_demo_native.exe` |
| exe SHA-256 | `00ec8b99fc779327eb2cf0eb0cb9a0e59eca3a4d0413e1ea29b36c7bcbec2078` |
| exe 字节 | `2,988,544` |
| sourceContent | `1d9c0ead32774f8535fc2bd10ef396cab3d25af8c92ed1b79e48e1f00c4325ce` |
| canonical sourceTree | `f533293`（docs-only 提交之后 exe 不变） |
| canonical dirty | `0` |
| hostlock | FREE |
| 残留进程 | `tf2_demo_native.exe` = 0 |

说明：`f533293` 之后的 `f0bf300`、`30d58e0`、`68a206a` 都是 docs-only 提交，不改变 exe 字节；本轮最后一次 canonical 重建绑定在 `f533293`。最终 HEAD 与共享报告的对应关系以本节为准。

---

## 3. 提交清单

| 提交 | 类型 | 内容 |
|---|---|---|
| `9fc3feb` | test(gate) | 门禁注册表 `gate-registry-check.sh` + `docs/GATES-UNREGISTERED.md`；`verify-fast` 增加 3/3 registry 步骤 |
| `75aa99e` | test(gate) | `verify-all` 第 29 步运行 Sultry rescue 门禁 |
| `da1461d` | test(audio) | `verify-audio` 比较 data-entry origin，修正 I-31 |
| `8fb52c1` | test(gate) | `material-coverage` 6 个 pin 重钉 + `drawn + hidden == input`；`verify-all` 计数 89→100 |
| `ed4beee` | test(audio) | `quiet-capture` headless-safe；新增 `audio-keys-check.sh` + `send-window-keys.ps1` |
| `1bfbba7` | feat(hud) | 小地图产品补丁：`hud_panel.h`、`minimap.h`、`main.cpp`、`main_hud.cpp` |
| `d15332c` | test(hud) | `minimap-hud-check.sh`（1067 行） |
| `17a7ef3` | docs(gate) | 注册小地图门禁，退役 blocked 行 |
| `d18c985` | test(map) | Sultry 门禁第 4 节：玻璃窗相机、on/off 像素差、材质归因；`verify-all` 计数 121→139 |
| `1ace36b` | feat(trace) | `--world-face-trace-points` + `texelA=`（诊断路径） |
| `8cea550` | test(map) | `world-black-account-check.sh`，钉住 I-20/I-21 读数和 OPEN 差异 |
| `f533293` | docs | 批次完成日志、F1-1..F2-1 记录 |
| `f0bf300` | docs | 绑定 clean rebuild 身份 |
| `30d58e0` | docs(map) | R2-9 `$nocull` census 报告 |
| `68a206a` | docs(ci) | F2-2 52-probe 扫描报告 |

---

## 4. 逐任务详情

### F1-1 / I-31：音频 origin 二分

**问题**：`verify-audio.sh` 报 `FAIL player (origins: packet=0/29368 sound=0/0)`，而同一 run 的 read/scheduled 是绿的。原台账认为是 2026-11-12 exe 到 v8 的产品回归。

**做法**：用六个保留 exe，在同一 demo、同一 argv 下运行并取 `audio: summary`：

| exe | SHA-256 前缀 | firstPacketTick | soundOriginTick |
|---|---|---:|---:|
| `d40363b7` | `d40363b7ff46` | 0 | 0 |
| `6c06c6ad` | `6c06c6ad0c54` | 0 | 0 |
| v5 | `8bff0858221b` | 0 | 0 |
| v6 | `2a63a4c35d20` | 0 | 0 |
| v8 | `c19776d56c58` | 0 | 0 |
| v9 | `2ae5067c0b9d` | 0 | 0 |

**结论**：不是产品回归。`5ee9642` 把产品的 `firstPacketTick` 从 signon 第一个 packet 改为 `firstDataEntryTick`（command 2 的第一个 entry，本 demo 为 0）；census 的 `firstPacketTick=29368` 是 signon tick，而 census 的 `packetOrigin=0` 才是同口径字段。门禁把两个 tick space 混用了。

**修复**：`da1461d` 将 `c_pkt` 重命名为 `c_signon`，断言改为
`c_origin == p_pkt && c_origin == p_origin`；新增 `player-packet-origin` 变异。

**证据**：
- `VERIFY-AUDIO=PASS assertions_ok=235`
- 17/17 变异全部 `MUTATION-CAUGHT=PASS`
- 日志：`D:/_shared/EXEC-2026-11-13/F1/audio-gate/`
- 二分日志：`D:/_shared/EXEC-2026-11-13/F1/bisect/results.tsv`

**状态**：CLOSED（门禁语义修复；无产品改动）。

### F1-2 / I-32：门禁注册表

**问题**：Phase E 新增的 `sultry-translucent-atlas-check.sh`（121 断言）没有被任何链调用；树上 80 个 `*-check.sh` 只有 32 个被链引用。

**做法**：
- 新增 `gate-registry-check.sh`：每个 `*-check.sh` 必须在三个链脚本的非注释行出现，或在 `docs/GATES-UNREGISTERED.md` 有非空理由；同时拒绝“已注册又登记为排除”和“登记不存在的门禁”。
- `--selftest` 5 个对照：正常、未列出、双重登记、空理由、幽灵行。
- `verify-fast.sh` 增加 `=== 3/3 gate registry ===`，失败时 `VERIFY-FAST-GATE-REGISTRY=FAIL`。
- `verify-all.sh` 第 29 步增加 Sultry gate（输出重定向到 `$OUT`，不写 tracked evidence）。

**证据**：
- `GATE-REGISTRY=PASS gates=84 registered=34 excluded=50`
- `GATE-REGISTRY-SELFTEST=PASS controls=5`
- `VERIFY-FAST=PASS (assertions_ok=43, gate_registry=PASS)`
- Sultry gate：`SULTRY-TRANSLUCENT-ATLAS=PASS`，121 → F1-5 后 139 断言
- 日志：`D:/_shared/EXEC-2026-11-13/F1/fast-final.log`、`F1/sultry/gate.log`

**状态**：CLOSED。

### F1-3 / I-24：material-coverage pin 漂移

**问题**：`10d1355` 把 PVS lump 6 解码改成 offset pairs + Source zero-run RLE，并重钉了 `world-material-check.sh` 的 `vis visible from 0`（1462→384），但漏了 `material-coverage-check.sh` 的 6 个数。

**修前实测**（当前 exe，同 gate argv）：

```
FAIL batch-1 tiles with no visible triangle = 42, expected <= 9
FAIL batch-1 visible triangles = 705, expected >= 1500
FAIL batch draws = 5, expected 6
FAIL batch draws with a texture = 5, expected 6
FAIL triangles drawn = 216114, expected 227070
FAIL pixels the control frame changes = 89488, expected 90127
```

**修复**：
- 6 个 pin 改为 `42 / 705 / 5 / 5 / 216114 / 89488`。
- 新增 `input triangles = 242109` 和 `drawn + hidden == input`（`216114 + 25995 = 242109`）。
- 注释写明 `10d1355` 是因果，几何与 atlas-plan 不变。
- `verify-all` 的 material-coverage 计数从 89 改为 100（89 本身已是旧漂移值）。

**证据**：
- 修前 `MATERIAL-COVERAGE=FAIL`，6 红，日志 `F1/matcov/gate-before.log`
- 修后 `MATERIAL-COVERAGE=PASS`，100 OK，日志 `F1/matcov/gate-after.log`
- exe 仍为 `2ae5067c`（修 gate 前）/ `00ec8b99`（F2-1 后），两次数值一致
- 报告：`D:/_shared/EXEC-2026-11-13/E/ledger-recon/MATCOV-PREEXISTING.md`

**状态**：CLOSED。

### F1-4 / I-29 + AUD-03：headless quiet capture 与音量键

**问题**：`quiet-capture-check.sh` 的 live normal 侧不传 `--audio-disabled`，却传 `AUDIO_DEVICE=6`，会真的打开声卡；不可 headless 运行。

**修复**（`ed4beee`）：
- 默认只跑 pinned pair（两侧均 `--audio-disabled`）+ 一次 headless live quiet run；无设备打开。
- 可听 normal/quiet live pair 改为显式 `--device`；`--mutation` 没有 `--device` 直接拒绝（rc=2），避免静默换 claim。
- 修正 `run_one` 与 probe 参数的绝对 OUT 路径（I-28 同族），避免 `$PWD//d/...` 影子路径。
- 新增 `audio-keys-check.sh` 与 `send-window-keys.ps1`：用真实 `WM_KEYDOWN/WM_KEYUP` 发送 M / `[` / `]`，因为这三个键在 `WM_KEYDOWN` 内联处理，不在 camera key table 里，camera-script `key` 到不了。

**证据**：
- `QUIET-CAPTURE=PASS (pinned + headless live; device-pair=SKIPPED)`，日志 `F1/quiet/gate3.log`
- 默认 no-device：`audio_device_opened=0 open_attempts=0 opens_ok=0 events_played=0`
- `AUDIO-KEYS=PASS (six key edges, two restart reads, no device opened)`，日志 `F1/keys/gate4.log`
- 键盘读数为 `0.50 muted=1 -> 0.50 muted=0 -> 0.40 -> 0.50 -> 0.60 -> 0.60 muted=1`
- ui.ini 结束为 `volumePercent=60 muted=1`；重启 muted 读 `0.00/1`，muted=0 读 `0.60/0`
- **未跑**：`--device` 可听 smoke（避免无人值守时出声）

**状态**：headless 部分 CLOSED；可听 device smoke NOT VERIFIED。

### F1-5 / I-23：半透明玻璃像素证据

**做法**：在 `sultry-translucent-atlas-check.sh` 增加第 4 节，用 `--debug-camera 949 -1016 784 15 180` 看 `glasswindow001a_649_-1016_704`，同相机跑 rescue on/off，并用 `--world-hole-trace` 归因。

**读数**：
- rescue on：`batches=4 tiled=202 refused=48 translucentRefused=0 layerBytes=350852288 batchDraws=7 blendedTriangles=1376`
- rescue off：`batches=3 tiled=192 refused=58 translucentRefused=10 layerBytes=338170880 batchDraws=6 blendedTriangles=1334`
- 像素差：`differing_pixels=84965`，bbox `x=1016..1263, y=0..600`
- hole-trace：`1100,300 / 1150,200 / 1200,100 / 1250,400 / 1230,50` 全部为
  `maps/cp_sultry_b8a/glass/glasswindow001a_649_-1016_704`；
  `1050,500` 为 `BRICK/WALL028` 对照

**证据**：
- `SULTRY-TRANSLUCENT-ATLAS=PASS`，139 断言
- 日志：`D:/_shared/EXEC-2026-11-13/F1/i23-pixels/gate.log`
- 原始帧/trace：`F1/i23-camera/`

**状态**：I-23 的**一个具名玻璃材质**有像素级证据；另外九个材质仍只有结构/计数证据，不能声称十个都有逐像素证明。

### F1-5 / I-20、I-21：命名黑像素的 alpha 诊断

**新增诊断**：`--world-face-trace-points "x,y;..."` 选择点，`texelA=` 输出 tile buffer 的 alpha；门禁 `world-black-account-check.sh` 固定六个点。

**实测（exe `00ec8b99`，相机 `-600 -600 950 20 45`，tick 0）**：

| 点 | 材质 | texel RGB | texelA | texel 帧 | albedo 帧 | 解释 |
|---|---|---|---:|---|---|---|
| 1143,132 | SULTRYPROPS/TREELINECARD | 255,255,255 | 0 | 255,255,255 | 255,255,255 | 透明片，alpha=0；黑是背景 |
| 1110,136 | SULTRYPROPS/TREELINECARD | 255,255,255 | 0 | 255,255,255 | 0,0,0 | alpha=0 且 vertex colour 为 0；不是黑纹理 |
| 873,366 | METAL/METALGRATE013A2 | 0,0,0 | 0 | 0,0,0 | 0,0,0 | 透明黑 texel，不是不透明黑材质 |
| 849,396 | METAL/METALGRATE013A2 | 84,83,81 | 255 | 84,83,81 | 0,0,0 | texel 非黑；albedo 0 来自 vertex colour，OPEN |
| 902,385 | METAL/METALGRATE013A2 | 64,64,63 | 226 | 64,64,62 | 62,64,64 | 部分透明，未单独定案 |
| 919,379 | METAL/METALGRATE013A2 | 82,81,79 | 255 | 210,208,197 | 210,208,197 | texel/albedo 一致；face-trace UV 与实际 pass 不一致，OPEN |

`depthMatch=0` 在这些点是预期的：半透明 surface depth-write off，depth buffer 保留的是背景/近邻，不能拿它判 owner。

**状态**：I-20 的 1143,132 / 1110,136 由 alpha=0 解释（1110 还叠加 vertex colour 0）；I-21 的 873,366 由 alpha=0 解释；849,396 是 texel 非黑但 vertex colour 0；919,379 的 texel 与 albedo 一致，问题在 face-trace 的 UV/三角形选择，不是黑像素。849 和 919 仍 OPEN，不能把“albedo=0”直接写成“黑纹理缺陷”。

### F2-1：小地图产品化

**落树**：
- `native/include/hud_panel.h`：`hudMinimapPanelSpec()`，160×160，右上，margin 4。
- `native/include/minimap.h`：七图 bounds、控制点表、投影、y 翻转、屏幕角度、HUD frame 接口。
- `native/src/main_hud.cpp`：面板底/边框/4 个控制点环/观察者/3 个航向点，13 shapes。
- `native/src/main.cpp`：`--hud-minimap`，默认关；只在请求时推 frame。

**门禁**：`minimap-hud-check.sh`，`MINIMAP-HUD=PASS assertions=96 skipped_controls=0`。第一次真跑发现并修复两个 harness 缺陷：
1. `echo "RUN ... rc=${RC_$name}"` 是 bash 非法替换；改为间接展开。
2. A10 旧 exe 两次运行未暂停，25 像素差异导致假红；两侧都加 `--start-paused`。

**限制**：
- 七图只有 Upward / Sunshine 有帧；process/gullywash/sultry/product/bagel 只有表，NOT VERIFIED。
- 没有 radar/overview 底图，第一版是 BSP-derived linework。
- A12 mutation 未跑。

**状态**：产品路径 PASS（96 断言）；七图完整视觉验收未完成。

### F2-2：52-probe 首批扫描 + 8-probe 域批次

**做法**：52 个 `*_probe.exe` 在 hostlock 下逐个 `--self-test`，15 s 超时，记录 rc、输出字节、SHA-256 前缀、首行。

**首批结果**：
- 29 个 rc=0
- 23 个非零：20 个 usage/unknown-arg，2 个需要 TF root/VPK，1 个 `texture_quad_probe` 需要 VTF 路径（rc=3 是它的“不是 VTF”分支）
- 没有空输出崩溃

**第二批（真实参数）**：8 个带参数 probe 全部 rc=0：
- `world_geometry_probe`：Sunshine `triangles=82859 faces=20449 displaced=24000`，budget within
- `world_feature_probe`：170 materials，bumpmap 38 declared/resolved、0 missing
- `terrain_blend_probe`：`triangles=82859 displacementFaces=642`
- `resource_reachability_probe`：4/5 archives，149,970 entries，105 VMT+VTF，atlas-eligible 19
- `vpk_integrity_probe`：50/50 resolved，0 empty；`pak01_dir.vpk` 缺失被具名
- `sound_content_map_probe`：Sultry read 5151 / scheduled 5031 / 1054 missing WAV
- `minimap_probe --minimap-test`：`scale=0.125 yFlip=1`
- `skeleton_skin_probe --mutation`：`mutationCaught=true skinLands=1`

**报告**：`docs/F2-2-PROBE-SWEEP-2026-11-13.md`
**原始表**：`D:/_shared/EXEC-2026-11-13/F2/probe-sweep/results.tsv`、
`F2/probe-domain-a/results.tsv`

**状态**：首批启动扫描 + 8 个域 probe 完成；剩余 12 个带参数 probe 未跑。

### F2-3 / R2-9：`$nocull` census

**方法**：Python `zipfile` 读取七图 BSP pak 中的 `materials/**/*.vmt`，大小写不敏感匹配 `nocull`。

**结果**：

| 地图 | VMT | `$nocull` | 具名 |
|---|---:|---:|---|
| cp_process_f12 | 50 | 1 | `toolspro/toolstrigger.vmt`（不可见工具材质） |
| cp_sunshine | 68 | 0 | 无 |
| cp_gullywash_f9 | 89 | 1 | `toolspro/toolstrigger.vmt` |
| cp_sultry_b8a | 267 | 0 | 无 |
| koth_product_final | 32 | 1 | `toolspro/toolstrigger.vmt` |
| koth_bagel_rc11 | 132 | 5 | 3 棵 oak tree、tarp、detail-sprite |
| pl_upward_f12 | 93 | 1 | `toolspro/toolstrigger.vmt` |

**结论**：只有 Bagel RC11 有可见候选；没有 world-renderer 修改依据。随后用 Bagel oak_l 静态道具 `(4160,-1216,432)` 的固定相机 `(3660,-1216,600, yaw 0)` 做了 cull on/off 和 `--world-geometry none` 对照：on/off 差异 689,683 像素、world-none 差异 860,784 像素，但这是**全局** world cull，不是 per-material `$nocull` 控制；oak 是 static prop（model path），不能证明某个 world brush face 带该材质。因此该项保持只读，正例仍缺。

**报告**：`docs/R2-9-NOCULL-CENSUS-2026-11-13.md`
**证据**：`D:/_shared/EXEC-2026-11-13/F2/bagel-cull/`

---

## 5. 门禁与证据索引

| 门禁/命令 | 结果 | 身份/日志 |
|---|---|---|
| `verify-fast.sh` | PASS 43 断言 + registry PASS | `F1/fast-final.log` |
| `gate-registry-check.sh --selftest` | PASS controls=5 | `F1/` |
| `verify-audio.sh --no-build --mutation` | PASS 235 断言，17/17 变异 | `F1/audio-gate/` |
| `material-coverage-check.sh` | 修前 6 红；修后 PASS 100 | `F1/matcov/gate-before.log`、`gate-after.log` |
| `sultry-translucent-atlas-check.sh` | PASS 139 断言 | `F1/i23-pixels/gate.log` |
| `quiet-capture-check.sh` | PASS（pinned + headless live） | `F1/quiet/gate3.log` |
| `audio-keys-check.sh` | PASS（6 键边 + 2 次重启） | `F1/keys/gate4.log` |
| `minimap-hud-check.sh --old-exe=...` | PASS 96 断言 | `F2/minimap/gate4.log` |
| `world-black-account-check.sh` | PASS（I-20/873 解释；849/919 OPEN） | `F1/black/gate2.log` |
| 52-probe sweep | 29 rc0 / 23 usage-missing；第二批 8/8 rc0 | `F2/probe-sweep/results.tsv`、`F2/probe-domain-a/results.tsv` |
| `$nocull` census | 七图扫描完成 | `F2/nocull-census.txt` |

---

## 6. 缺陷台账变化

| ID | 旧状态 | 本批状态 | 依据 |
|---|---|---|---|
| I-31 | 产品回归（未知窗口） | **CLOSED（门禁语义）** | 六个 exe 全 0；`5ee9642`；`da1461d` |
| I-32 | 新门禁未注册 | **CLOSED** | `9fc3feb`、`75aa99e`；registry PASS |
| I-24 | pin 漂移 | **CLOSED** | `8fb52c1`；100 断言 PASS |
| I-29 | quiet-capture 不可 headless | **CLOSED（headless）** | `ed4beee`；默认 device_opened=0 |
| I-23 | 计数已修，像素未证 | **FIXED + 一个材质像素证据** | `d18c985`；84,965 px；其余九材质仍结构性 |
| I-20 | 1143,132 黑点未定 | **解释：透明 texel + 背景** | `texelA=0`；`8cea550` |
| I-21 | 三 albedo=0 点 | **873 解释；849/919 OPEN** | `texelA` + `--world-texel`：873 alpha=0；849 texel 84 / albedo 0（vertex colour 0）；919 texel=albedo，face-trace UV 不一致 |
| I-28 | 绝对 OUT 影子路径 | **本批触碰的 gate 已修；全树规则仍待 F1-6** | `quiet-capture`/`audio-keys` |
| I-30 | wall-clock 字段不可 pin | 未改；本批未新增此类精确断言 | — |

---

## 7. 没有证明什么（诚实清单）

1. **没有跑 15 步完整发布链。** 本批所有绿色门禁都是窄门禁/聚焦门禁，不是 release chain。
2. **没有七图视觉验收。** I-23 像素证据只覆盖一个具名玻璃材质、一个相机；I-20/I-21 只覆盖 Sultry 的六个点。
3. **小地图只证了 Upward 和 Sunshine 的帧。** 其余五图只有 bounds/控制点表；A12 变异未跑；没有 radar 底图。
4. **没有可听音频验证。** M / `[` / `]` 的字节/持久化已证，但 device-6 可听 smoke 未跑。
5. **F2-2 不是 54 个 probe 的完整域验收。** 首批 52 个启动/self-test 扫描 + 8 个真实参数 probe 已完成；剩余 12 个带参数 probe 还没跑。
6. **F2-3 没有 Bagel 正/对照帧。** 只有声明 census；没有证明树/tarp 是 world brush 或会掉像素。
7. **F2-4 半透明排序、F2-5 3D skybox、F2-6 reload 观测、F3 多视角、F4 冻结均未开始。**
8. **没有实现或声称**：ragdoll、水面反射/折射、玻璃反射、通用粒子、lighting/shadow parity、LOD1+。

---

## 8. 复跑命令

所有产品/构建命令都必须在 hostlock 内：

```bash
cd /d/TF2_Native_Worktrees/t105-e5
LOCK=/d/TF2_Native_Worktrees/_shared/hostlock.sh

# 身份
sha256sum native/build-nmake/tf2_demo_native.exe
cat native/build-nmake/tf2_demo_native.identity
bash build-identity-content.sh hash

# 快速层
bash "$LOCK" run f-fast bash verify-fast.sh

# 音频门禁
bash "$LOCK" run f-audio env VERIFY_AUDIO_OUT=/d/_shared/EXEC-2026-11-13/F1/audio-gate \
  bash verify-audio.sh --no-build --mutation

# 材料 pin
bash "$LOCK" run f-matcov \
  env MATERIAL_COVERAGE_OUT=/d/_shared/EXEC-2026-11-13/F1/matcov/evidence-after \
      MATERIAL_COVERAGE_SCRATCH=/d/_shared/EXEC-2026-11-13/F1/matcov/scratch-after \
  bash material-coverage-check.sh

# Sultry 透明 + 像素
bash "$LOCK" run f-i23 \
  env SULTRY_ATLAS_OUT=/d/_shared/EXEC-2026-11-13/F1/i23-pixels/evidence \
      SULTRY_ATLAS_SCRATCH=/d/_shared/EXEC-2026-11-13/F1/i23-pixels/scratch \
  bash sultry-translucent-atlas-check.sh

# quiet capture / 音量键
bash "$LOCK" run f-quiet env QUIET_CAPTURE_OUT=/d/_shared/EXEC-2026-11-13/F1/quiet/out3 \
  bash quiet-capture-check.sh
bash "$LOCK" run f-keys env AUDIO_KEYS_OUT=/d/_shared/EXEC-2026-11-13/F1/keys/out4 \
  bash audio-keys-check.sh

# 小地图
bash "$LOCK" run f-minimap \
  env MINIMAP_CHECK_OUT=D:/_shared/EXEC-2026-11-13/F2/minimap/out4 \
      MINIMAP_CHECK_SCRATCH=/d/_shared/EXEC-2026-11-13/F2/minimap/scratch4 \
  bash minimap-hud-check.sh --old-exe=D:/_shared/EXEC-2026-11-13/R0/bin/tf2_demo_native.exe

# 黑像素
bash "$LOCK" run f-black env BLACK_ACCOUNT_OUT=D:/_shared/EXEC-2026-11-13/F1/black/out2 \
  bash world-black-account-check.sh

# 52-probe 首批
bash "$LOCK" run f-probes bash /d/_shared/EXEC-2026-11-13/F2/probe-sweep.sh
```

---

## 9. demo / BSP 身份（本批捕获）

| 用途 | 文件 | SHA-256 |
|---|---|---|
| Sultry 透明/像素/黑点 | `E:/demo测试/cp_sultry_b8a/STV/f63a7a2040379d7d6a8414f76bbb81dc_matcha-20260703-1442-cp_sultry_b8a.dem` | `032bbd5ab719012cc2a7b4858f0a5d4e1ba882b4cdae1d86f2b966a3578c4374` |
| Sultry BSP | `E:/demo测试/cp_sultry_b8a/地图/cp_sultry_b8a.bsp` | `49bab2feeb53f102cd79601b38f3cbd9a30b79d19184fcb1baa24ac139aa66b2` |
| Upward 小地图 | `D:/TF2_Demo_Player/testdata/demos/a9630c6557468b24f0b8c2e7dab38ed8_match-20261030-2145-pl_upward_f12.dem` | `7e047aff1f7743915e355eac907a95dc3d981aa0fae5591b6b15346fb77dc4a3` |
| Upward BSP | `D:/SteamLibrary/steamapps/common/Team Fortress 2/tf/maps/pl_upward_f12.bsp` | `0d9fe5486023df49f0e6d4da43ac3e777baee87f1c588933dc16750cb7c51677` |
| Bagel live | `D:/TF2_Demo_Player/testdata/demos/4a9bfb9276509d0ec5f5fdc722a95b17_match-20260927-0239-koth_bagel_rc13.dem` | `03529698db749e77a1df1c476c1c0cc06847e1173b5366a0602f1e15161b5436` |
| Bagel RC11 BSP | `E:/demo测试/koth_bagel_rc11/地图/koth_bagel_rc11.bsp` | `f06cfe4b389da5ca152dff0702fb3f4d69b48bfc7b08ed9994e1d057a211720a` |
| Snakewater coverage | `D:/TF2_Demo_Player/testdata/demos/bb841c6d379ff7c40d0c8baf99f59d8d_matcha-20260927-1347-cp_snakewater_final1.dem` | `308cc1453672b26dfdcc106bcb62eac4a7eea97899adc8830f51711be834607f` |
| Snakewater BSP | `D:/SteamLibrary/steamapps/common/Team Fortress 2/tf/maps/cp_snakewater_final1.bsp` | `f09d406d2bbe397c8f9df9ca17f11b36b53d40fcf57e3f1532ec9af811b2a1eb` |
| 音频 pinned | `D:/SteamLibrary/steamapps/common/Team Fortress 2/tf/demos/autorecord_2026-07-02_13-26-46.dem` | `c8a58501d222bdd9aec6db92d2782931a29a13d963a87364d603263e6dab091e` |

---

## 10. 产物索引

| 内容 | 路径 |
|---|---|
| 本报告 | `docs/PHASE-F-REPORT-2026-11-13.md` |
| 共享副本 | `D:/_shared/EXEC-2026-11-13/ROUND-REPORT-2026-11-13-PHASE-F.md` |
| F1 根目录 | `D:/_shared/EXEC-2026-11-13/F1/` |
| F2 根目录 | `D:/_shared/EXEC-2026-11-13/F2/` |
| 音频二分 | `F1/bisect/` |
| 音频门禁 | `F1/audio-gate/` |
| 材料 pin | `F1/matcov/gate-before.log`、`gate-after.log` |
| Sultry 透明/像素 | `F1/i23-pixels/`、`F1/i23-camera/` |
| 黑像素 | `F1/black/` |
| quiet capture | `F1/quiet/gate3.log` |
| 音量键 | `F1/keys/gate4.log` |
| 小地图 | `F2/minimap/gate4.log` |
| 52-probe | `F2/probe-sweep/results.tsv` |
| nocull census | `F2/nocull-census.txt` |
| Bagel cull control | `F2/bagel-cull/` |
| 批次日志 | `docs/PROJECT-ROADMAP-HANDOFF-2026-11-12.md` Batch 4 |

---

## 11. 下一批建议

按依赖排序：

1. **F2-2 剩余 12 个域 probe**：按 demo/map/model/VPK 分组，用真实参数在 hostlock 内跑；补 `animation_viewmodel_probe`、`pcf_capability_probe` 的 TF root/VPK 输入。
2. **F2-3 Bagel `$nocull` 正/对照**：固定相机看一棵 oak tree 或 tarp，拍 `--world-cull` on/off；只有在像素证明确有 cull 掉面时才改 world renderer。
3. **F2-4 半透明排序**：用 Sultry 玻璃相机和现有 atlas/PVS 分组，量化是否有同相机排序错误；没有像素 oracle 不写通用排序。
4. **F2-5 3D skybox**：按 E3 设计实现 `sky_camera` 解析/变换/第二遍绘制，先做一张图的 on/off 证据。
5. **F2-6 reload 观测**：提高 30–60 tick 窗口的 checkpoint 密度，先证现有 playback，再改产品。
6. **F3-1 双视角**：先两路 split，per-pane target/mode/free-look，再做 PIP/四路。
7. **F1-4 可听 device-6 smoke**：需要人工在场确认，不在无人值守时跑。
8. **F2-1 A12 变异 + 五图帧**：补 minimap mutation 和 process/gullywash/sultry/product/bagel 的固定相机帧。
9. **F1-5 849/919**：849 追 vertex colour 为什么是 0；919 追 face-trace 的 UV/三角形选择为什么与实际 texel pass 不同。不要把这两条并进 I-20 的 alpha 结论。
10. **最终 F4**：只在所有 P0/P1 关闭或显式接受后，跑**唯一一次** 15 步发布链。

---

## 12. Handoff 字段汇总

- **Task IDs**：F1-1, F1-2, F1-3, F1-4, F1-5, F2-1, F2-2 (first batch), F2-3
- **Owner**：single writer / integrator（本批未启用并行子会话；所有构建、捕获、hostlock 串行）
- **Base commit**：`9deed76`
- **Worktree/branch**：`D:/TF2_Native_Worktrees/t105-e5` / `candidate/e5`
- **Changed files**：见第 3 节 15 笔提交；产品文件为 `native/include/hud_panel.h`、`native/include/minimap.h`、`native/src/main.cpp`、`native/src/main_hud.cpp`
- **Build/repro commands**：见第 8 节
- **Result/log locations**：见第 5、10 节
- **Source/exe hashes**：`00ec8b99...` / `1d9c0ead...` / `sourceTree=f533293`, dirty=0
- **Demo/BSP hashes**：见第 9 节
- **Completed claims**：第 1、4 节；只对绑定的门禁/相机/身份成立
- **Remaining limitations**：第 7 节
- **Downstream unblocked**：F2-2 域批、F2-4、F2-5、F2-6、F3-1、F4 准备
- **Still blocked**：可听 device smoke（需人工）、五图 minimap 帧、Bagel cull 正例、849/919 归因、完整发布链
