# INST1 / R1 — 候选构建、内容身份与资源查找控制

日期 2026-11-15 · 实例1（A/F 工程与集成，arch-ci） · 卡 task-1 rev2 · 队列片 R1
仓库 `D:/TF2_Native_Worktrees/t105-e5` · 分支 `candidate/e5` · HEAD `cf625b42156aac1714f99c72a0d28ce0d7698014`
证据目录 `D:/_shared/EXEC-2026-11-15/INST1/`（R0 见 `R0-SITE-REGISTRATION.md`）

## 0. 一句话结论

产品 exe 已重建，内容身份两次独立测量对齐，no-op 构建为真零工作；TF/BSP-pak/HL2 三条正控与两条负控成立；**但控制段暴露了一个真缺陷**：N07 的 hl2 归档挂载只修好了 `VpkArchiveSet`（probe、模型、音效），修不到世界材质的 `AssetStoreChain`，产品在 pl_upward 上真实拒绝渲染 2 个 HL2 材质（`vmtStore=none`），而同一张图的 probe 报这 2 个 `stage=ok`。暂停/seek 正控通过（四张帧两两字节相同）。

## 1. 构建（走锁，增量，非 clean）

命令 `bash tools/lockrun.sh arch-ci-inst1 -- bash build-target.sh tf2_demo_native material_coverage_probe`，rc=0，无 warning 无 error，`VERIFY-LOCK=ACQUIRED/RELEASED` 成对。

- 只重编了 `generated/tf2_build_stamp.cpp` 一个 TU + 重链 —— `vpk_archive.cpp.obj` 已是最新（mtime `2026-11-15 20:44` > 源文件 `20:44`），不需要重编。日志原文 `[ 65%] Building CXX object CMakeFiles/tf2_demo_native.dir/generated/tf2_build_stamp.cpp.obj` / `[ 65%] Linking CXX executable tf2_demo_native.exe`。
- **`material_coverage_probe` 未被重建**（mtime 仍 `20:44`，sha `efd3a2bd43e9f4…`）。它 20:44 已用当前源码编好。**「构建成功」≠「重建了它」** —— 规则 9 的实例，记在此以免后续误读。
- no-op 复测：同一命令再跑一次，rc=0，exe sha 不变，日志只有 `Built target` 行、无 Building/Linking。

### exe 身份（构建前后）

| | 构建前 | 构建后 |
|---|---|---|
| sha256 | `6f3e64cdc8f7b546…4877` | `7a8da164de6755328…92ff` |
| bytes | 3,016,704 | 3,017,216 |
| mtime | 2026-11-15 20:09:53 | 2026-11-15 21:34 |
| sidecar sourceContent | `79bf90699588ae3e…` | `11bf1bb1c46e517f…` |
| sidecar sourceTree | `dcade77687ef` | `cf625b42156a` |

构建前 exe 已过期，三条独立证据：① `native/src/vpk_archive.cpp` 与 `native/tools/material_coverage_probe.cpp` 的 mtime 晚于 exe；② 我独立重算 `bash build-identity-content.sh hash` = `11bf1bb1c46e517f95e54e0e9ac8039d92a536d4476c59ebb29e273d62de8895`，与旧 sidecar 的 `79bf9069…` 不同；③ 树身份 `dcade77687ef` ≠ 当前 HEAD 前 12 位 `cf625b42156a`。

**构建后 sidecar 的 `sourceContent` 与我另一次独立重算完全一致，`sourceTree` = HEAD 前 12 位** → 两次测量对齐（D-54 要求的正是「不从同一处真相读两次」）。

### 脏状态的正读（易误判，写明）

新 sidecar `dirty=1 dirtyFiles=6 dirtyTracked=4 dirtyUntracked=2`，同一次构建的日志写 `epoch=1794746741 dirty=1/6`。但 `git status --porcelain native` = **0 行**。两个口径不同：sidecar 的 dirty 统计**整个仓库**（当时是 4 个被跟踪文档 + 2 个未跟踪文档），不是 native 脏。`contentMethod=git-clean-v1` 说明内容摘本身只取 git index 里 clean 的输入集（`native/CMakeLists.txt, native/src, native/include, native/third_party, native/cmake`），`native/generated` 与 `native/tools` 都不是输入。

## 2. 资源查找控制（本片的核心）

三个具名正控 + 两个负控，全部真跑，产物在 `R1-controls/`。

### 2.1 TF 正控 / BSP-pak 正控 / HL2 正控（probe，`--list-materials`）

工具：`material_coverage_probe --map <bsp> --tf-root <tf> --list-materials`。**关键用法**：不加 `--list-materials` 只打 summary readings，不打逐条 `material=` 行；`source=` 字段只在逐条行里。

- TF 正控 `METAL/METALGRATE013A2` → `expect.material=METAL/METALGRATE013A2 expect.batch=0 expect.tile=34 expect.reason=tiled`（来自 tf2_textures / tf2_misc）。
- BSP-pak 正控：snakewater 的 `plan.reason` 显示 260/260 tiled、5 batch，`map.vpkEntries=179471`（含 hl2 → 见 2.3 的对照 `0`）。
- HL2 正控（**关键**）：`DEV/REFLECTIVITY_10B` → `expect.batch=1 expect.tile=12 expect.reason=tiled`；`GLASS/GLASSWINDOW008D` → `expect.tile=15`；逐条行 `material=DEV/DEV_MEASUREGENERIC01 triangles=212 source=vpk:hl2_misc_dir.vpk stage=ok reason=vmt=vpk:hl2_misc_dir.vpk vtf=vpk:hl2_textures_dir.vpk`（`PROPS/HAZARDSTRIP001A` 63 tri、`GLASS/GLASSWINDOW008D` 24 tri、`DEV/REFLECTIVITY_50` 18 tri 同形）。

### 2.2 七图材质普查（语料）

`/tmp` 外的脚本 `.scratch/r1-mapscan.sh`，输出 `R1-controls/mapscan/`。hl2 材质**并不稀缺**：6 张可跑的图里 5 张都在用 hl2 材质（每图 1–2 个，都是真渲染的三角形）。

| 地图 | rc | distinct | ok | hl2 | tf2 | pak | 非 ok |
|---|---|---|---|---|---|---|---|
| cp_gullywash_f9 | 0 | 122 | 112 | 1 | 49 | 72 | 10（全 water，`vmt-no-basetexture`） |
| cp_process_f12 | 0 | 119 | 119 | 2 | 75 | 42 | 0 |
| cp_sunshine | 0 | 170 | 167 | 2 | 106 | 62 | 3（water） |
| koth_bagel_rc12 | 0 | 99 | 97 | 0 | 56 | 43 | 2（water） |
| koth_product_final | 0 | 67 | 67 | 1 | 55 | 11 | 0 |
| pl_upward | 0 | 82 | 81 | 2 | 75 | 5 | 1（water） |
| **cp_sultry_b8a** | **-1073740791** | — | — | — | — | — | **崩溃**（见 §5 缺陷 ②） |

water 的 `vmt-no-basetexture` 是**设计如此**（water 材质没有 `$basetexture`），不是缺陷。

### 2.3 🔴 产品侧反例：HL2 材质在世界渲染里仍然缺

`.scratch/r1-prod-hl2.sh`，走锁，`pl_upward` POV demo，`--capture-tick 400`，rc=0，trace `R1-controls/prod-pl_upward.trace`（2,538,245 B / 15,430 行）。settings 用 A 法（`cp native/tests/fixtures/gate-settings.ini`），不用 B 法宿主 profile。

```
gpu: world visible-refusal reason=vmt-missing materials=2 visibleTriangles=15
gpu: world refused material=DEV/REFLECTIVITY_30 visible=15 reason=vmt-missing vmt=materials/DEV/REFLECTIVITY_30.vmt vmtStore=none vtf=- vtfStore=none src=0x0
gpu: world refused material=DEV/REFLECTIVITY_10B ... vmtStore=none
```

同一张图的 probe 报这两个 `source=vpk:hl2_misc_dir.vpk stage=ok`。**probe 绿、产品红，同一个名字。**

聚合行对照：产品 `distinct=82 tiled=79 vmtMissing=2 fromPakfile=5`；标题行 `[VPK entries: 104343]`（= 只算 tf 的 5 个归档）。

**根因（代码级，已定位）**：世界材质走 `AssetStoreChain`，归档列表由 `native/src/main.cpp:5560` 的**固定名单**装填：

```cpp
for (const auto* name : { L"pak01_dir.vpk", L"tf2_misc_dir.vpk", L"tf2_textures_dir.vpk",
                          L"tf2_sound_misc_dir.vpk", L"tf2_sound_vo_english_dir.vpk" }) {
  ...
  if (archive->open(archivePath, &archiveError)) {
    assetChain.addArchive(archive, archivePath);   // :5565
    soundArchives.push_back(std::move(archive));   // :5566
    continue;
  }
```

名单里没有 `hl2_misc_dir.vpk` → N07 的 `VpkArchiveSet::openDirectory`（`native/src/vpk_archive.cpp:292-319`，先 mount tf 再 mount `tfDirectory.parent_path() / "hl2"`）对世界材质链**完全不生效**。`indexedVpkEntries` 仍在 `native/src/main.cpp:5720` 从 `soundArchives` 取 max，所以标题行也仍是 104,343。

**修复切口（重要，不要照最直觉的方式改）**：`soundArchives` 是**一个容器喂两个消费者** —— `:5565` 喂 `assetChain`（世界材质），`:5609 for (const auto& archive : soundArchives)` 喂地图 pakfile 查找，同时也是 `vpk: archives opened=%zu`（`:5599`）的来源。把 hl2 直接塞进这个循环会让 `archives opened` 变成 9，撞 `install-lifecycle`/`install-readiness` 的 `archives opened=4` pin 与 `resource-reachability` 的断言；而音效本来也不该从 hl2_sound 取。**正确切口：把「世界材质用的 assetChain」与「地图 pakfile/音效用的 soundArchives」拆开，只给 chain 追加 hl2 归档（或让 chain 复用 `AssetRoot::archives()`），并保持 `indexedVpkEntries` 的语义不变。**

**归属**：改点落在 `native/src/main.cpp`，是实例3（ui-d）的写域 → 需 lead 裁决，本片**只取证不修**。

## 3. 暂停 / seek 正控（通过）

产品 CLI 没有 pause/seek 开关，唯一通路是 `--camera-script`（`native/src/camera_script.cpp`，T76 stage4 从 `main.cpp:742-1339` 原样搬出）。脚本 20 步全 ok，`failures=0 captures=4`，rc=0。

关键读数：`step=1 speed 4.000000` → `step=2 tick=2004` → `step=4 pause` → `camera: mark tick=2016` → `step=12 seek` → `tick=3000`。`playback: state` 里 seek 前 `cursor=2016 paused=1 requests=3 landings=0`、seek 后 `cursor=3000 landings=1` —— **`landings` 0→1 是 seek 真落地的独立读数**，不靠「tick 数字变了」推断。

**图像证据（四张 bmp 全 2,582,406 B，两两字节相同）**：

| 文件 | tick | sha256 前 20 位 |
|---|---|---|
| paused-a.bmp | 2016 | `d94cd03049b74fb6f344` |
| paused-b.bmp | 2016 | `d94cd03049b74fb6f344` |
| seeked.bmp | 3000 | `62e40b0ecdeb6d3a333b` |
| seeked2.bmp | 3000 | `62e40b0ecdeb6d3a333b` |

暂停时反复绘制、seek 后反复绘制都产出确定性同一帧 → 帧内容只由 tick 决定。两组 tick 不同、哈希不同，所以上一条不是「两帧都黑屏」的假绿。

同时暴露的对照：snakewater 上产品 `vmtMissing=5` 但 `world refused / vmtStore=none` 全 0 —— 因为那些面在该帧不可见。**「聚合计数红」与「可见后果红」是两件事**，pl_upward 才有可见后果。

## 4. gate3 / gate5 影响表

### 4.1 输入可用性（实测）

| 门禁 | 输入 | 状态 |
|---|---|---|
| gate5 `material-coverage-check.sh` | `DEMO=…bb841c6d…cp_snakewater_final1.dem`（:124） | ✅ 51,834,617 B |
| | `POV_DEMO=$TF/demos/autorecord_2026-07-02_13-26-46.dem`（:127） | ❌ **不存在** → `:189-191` 具名 FATAL 后退出 |
| gate3 `playback-error-paths-check.sh` | `AUDIO_DEMO=E:/demo测试/pl_upward_f12/POV/…`（:60） | ✅ 146,853,666 B |
| | `CAM_DEMO=…koth_bagel_rc11.dem`（:61） | 未单独验证 |

实跑 gate5：先打 `ISO-SETTINGS sha=b85f044f76e397da…`，然后 `FATAL: missing input: …/autorecord_2026-07-02_13-26-46.dem`，`GATE5_RC=1`。**它在第一个产品启动前就退，所以 gate5 现在给不出任何材质结论。** 全仓 28 个 check + `mutate.sh` 引用同一个缺失 POV。

对照：`bone-mismatch-semantics`/`class-pixels`/`player-animation-pixels`/`player-body-appearance`/`probe_attach` 5 个已改成 `${VAR:-default}` 可覆盖；`visual-baseline-check.sh:121-122` 与 `world-material-face-check.sh:74` 仍硬编码不可覆盖。→ **E1 具体建议：统一成 `${VAR:-default}` + 缺输入具名 FATAL。**

### 4.2 hl2 进 chain 会移动哪些 pin

| 门禁 | 断言（行号） | hl2 修复后 |
|---|---|---|
| `resource-reachability-check.sh` | `:80` `archives opened`=4、`:81` `missing archive (by name)`=`pak01_dir.vpk` | **要重钉**（数据来自 probe 的 `archivesOpened`/`missingNames`，`:75-78`） |
| `install-lifecycle-check.sh` | `:233-234` 解析 `vpk: archives opened=([0-9]*) refused=` | **要重钉**（emit 点 `main.cpp:5599`） |
| `install-readiness-check.sh` | `:182/:185/:633` 同一条正则 | **要重钉** |
| `stability-release-check.sh` | `:572` `VPK entries (the real root)`=`104137` | **要重钉**（emit 点 `main.cpp:7216`，值来自 `:5720`） |
| `visual-baseline-check.sh` | `:275/280-284` `distinct=127 tiled=118 fromPakfile=107 vtfTooLarge=83 vmtMissing=3 vtfMissing=0` | **要重钉** |
| `world-material-face-check.sh` | `:207` `fetch partitions distinct` = TILED+CAPPED+VMTMISS+VMTNOBASE+VTFMISS+DECODEEMPTY+TRANSFORM | **恒等式，不会红** |
| gate5 | `:587` tiled=141、`:588` capped=0、`:589` refused=0、`:590` batches=3、`:591` budget=0、`:596` bytes≥67000000、`:597` atlases=3、`:598` totalBytes≥119000000、`:610` tile rows=141、`:611` batch0=64、`:612` batch1=64；`:502-513` planner 组 | **只要不改归档结构就仍成立**；tiled/vmtMissing 计数型断言须随修复一起复核 |

**gate3 的声音依赖**：gate3 本身不读 VPK 条目数，它测的是 `--demo` 缺失/默认图/暂停/seek/`--start-paused`/`--capture-tick` 六条错误与边界路径（`:679/:689/:720/:737/:747/:759`），与本修复无交叉 → **gate3 不受 hl2 改动影响。**

## 5. 本片发现的三条缺陷候选（未修，待 lead 裁决归属）

1. **世界材质 `AssetStoreChain` 不含 hl2 归档** —— `native/src/main.cpp:5560`，与 `soundArchives` 耦合是修复难点，见 §2.3。改点在实例3 写域。
2. **probe 在含中文的 BSP 路径上崩溃** —— `cp_sultry_b8a` 的 BSP 只在 `E:/demo测试/cp_sultry_b8a/地图/cp_sultry_b8a.bsp`（`tf/maps/` 下没有），probe rc=`-1073740791`（0xC0000409）。与先前记录的路径编码崩溃族同源。
3. **probe 伪根不拒绝** —— `--tf-root` 指向不含 `materials/models/maps` 与 VPK 的目录时，probe 仍打出 `map.stage.ok=48` / `vmtAbsent=97` 的"成功"读数（`R1-controls/n2-badroot.txt`、`n1-fakeroot-list.txt`），只是所有材质都解析不到，而不是具名拒绝。对照：产品对显式 `--tf-root` 有正确的拒绝语义（`main.cpp:4325-4336` T4 注释「An explicit command-line root is an operator decision. Do not silently switch to another Steam installation」+ `:4387` `resources: … valid=%d`）。`AssetRoot::valid()`（`native/src/asset_root.cpp:121-127`）本来就判 `is_directory` + `materials/models/maps` 三 store，probe 没调它。

负控补充：伪 map `nope.bsp` → `cannot read …/nope.bsp`（`n3-badmap.txt`，单行，rc 非 0）—— 这条是对的，产品与 probe 都不静默当成功。

## 6. 本片未证明 / 留给下一片

- **gate5 没有实跑出材质结论**（输入缺 POV），只有影响表；它一旦被解阻塞，§4.2 的数字要重测。
- **gate3 未实跑**（输入齐但本片优先做了产品侧 pause/seek 正控）。
- hl2 修复的**正控**（`vmtStore` 从 `none` 变成 `vpk-directory`/`vpk-volume`、`VPK entries` 从 104,343 变成含 hl2 的值）**未做** —— 因为修复本身没做。
- `cp_sultry_b8a` 崩溃只测到 rc，未定位到崩溃点。
- 本片所有运行均走 `tools/lockrun.sh`，锁每次成对 ACQUIRED/RELEASED；未 commit、未改任何源码、未碰他人脏文件。

## 7. 现场变动（并发编辑，非本实例所为）

R1 工作期间 `native/tools/material_coverage_probe.cpp` 在 **21:59:35** 被人改动（`git diff --stat` = `1 file changed, 369 insertions(+), 6 deletions(-)`，新增一整套 in-probe 控制组，`:380` 起 `// 3. the controls the closure rests on`），probe exe 于 **22:00:43** 被重建，从 562,688 B / sha `efd3a2bd…` 变成 600,064 B / sha `f754a49f…`。

**产品 exe 未变**（仍是本片构建的 `7a8da164…` / 3,017,216 B），所以 §2.3 的产品侧反例与 §3 的 pause/seek 正控仍然对应本片那次测量。但 §2.1/§2.2 的 probe 读数取自已失效的旧 probe，任何人现在重跑 `material-coverage-check.sh` 会拿到新 probe，数字需重新确认。

**并且这次改动引入了换行符损伤**：`bash tools/audit-tree.sh` 现在打印
`VIOLATION: native/tools/material_coverage_probe.cpp has 987 CR (HEAD had 0); raw diff 375 lines vs real 375`，
末行 `VERDICT: CRLF-DAMAGE`（R0 时该脚本是干净的，当时 native 全绿）。→ **在它被修复前，任何提交都过不了规则 4 的 `VERDICT: OK` 门槛。**

按硬规则 2，该改动属于别人：不 stage、不提交、不 checkout、不 stash。详见 `D:/_shared/EXEC-2026-11-15/INST1/R1-CONCURRENT-EDIT-NOTICE.md`。
