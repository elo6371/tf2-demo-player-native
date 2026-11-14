# N28–N30 —— 集成、干净环境走查、一次最终发布链（执行清单，本轮未执行）

- 日期：2026-11-15　分支 candidate/e5　编写时 HEAD：ed20a7e
- 作者：lead（统筹）。**本文件是清单与前置审计，不是执行记录。**
- 本文不含推测：每一句要么给 `file:line`，要么给命令与实测输出。

## 0 为什么先写清单而不是直接跑

任务卡 `docs/PROJECT-NEXT-TASKS-2026-11-14.md` 把 N28–N30 放在「最后」一栏，前置是「全员停止改冻结候选」。今天的状态不满足这个前置：

| 前置条件 | 今天实测 | 判定 |
| --- | --- | --- |
| 工作树干净（不见 dirty） | `git status --porcelain` = 3 个 `M docs/*`（用户本人改的台账）+ 未跟踪交付物 | **不满足** |
| 冻结候选 = 全部卡完成 | N12-C 第一步在写、门禁修复窗口在写、N11 矩阵在写 | **不满足** |
| exe 不比源码旧 | `native/src/main.cpp` 的 mtime 比 `native/build-nmake/tf2_demo_native.exe` 新约 6 分钟（ci2 实测），内容与 HEAD blob 逐字节相同 | **需补一次构建**（stale-binary 守卫会拒） |

**所以 N29/N30 在本轮只能出清单，不能出结果。** 任何「已走查」「已冻结」「已发布」的写法都是假的。

## 1 N28 —— 集成

### 1.1 本批已经改过 / 将要改共享面（决定合并依赖顺序）

| 文件 | 谁改 | 状态 |
| --- | --- | --- |
| `native/src/main.cpp` | n12-camera（N12-B，`6dd853e`） | **已提交**（107 增 3 删，blob 纯 LF） |
| `native/src/main.cpp` | n12cimpl（N12-C 第一步，只许改 `:10180` / `:10200` / 注释） | 进行中 |
| `native/include/demo_header.h`、`native/src/demo_header_entity.cpp`、`native/src/demo_header.cpp` | n12cimpl（相机 tick 轨道） | 进行中 |
| 根目录 5 个门禁 `.sh`（class-pose-family / player-animation / playback-error-paths / animation-drive-states / viewmodel-animation） | gatefix | 进行中 |
| `docs/**` 交付物 | 多个只读实例 | 已提交两批（`d8b10a3`、`ed20a7e`） |

**合并顺序（硬约束）**：门禁修复与 N12-C **互不依赖，但要按域分开提交**；N12-C 触到 `native/CMakeLists.txt`（若新增 `demo_header_camera.cpp`），而 N26 的构建缓存测量也要读同一个 CMake 树 → **N12-C 的 CMake 改动必须先落地并重建一次，再让任何构建测量开始**，否则缓存命中率读数被编译树改动污染（N26 报告 §2 的前提冲突即为此）。

### 1.2 每批集成必须做的事（照卡面）

1. 先列合并依赖、共享文件、受影响证据（上面 1.1 就是这一列）；
2. 审查 diff（`git diff` + `git show --stat`），确认**没有跨写域改动**；
3. 只 build 相关目标 + 跑窄门禁（不跑全链）；
4. 产品行为不变的重构要求**相同固定输入输出**；产品修复要求**根因 + 旧缺陷负控 + 新正控**三件套（N12-B 已按此做：负控 entity 15 真零值、entity 2 站蹲 A/B）；
5. **不给旧 exe 的证据改标签当「当前通过」**——N14 的七份 trace 来自 `b4a2355e` 而不是 HEAD 的 `1c3580e2`，这类继承必须逐条写明「哪些文件在该范围内逐字节未变」，否则重跑。

### 1.3 N12-C 落地后必须重做的证据（否则 N12 不能结卡）

- N12 验收报告里所有 STV/POV 眼位读数（`docs/N12-ACCEPTANCE-2026-11-15.md`）——P1 改的是取数来源，旧读数在 stale>0 的位置会变；
- `--history-coverage` 的 stale 直方图（今天的判据是「100% 落在 0」，见 n12c-plan §3.3）；
- 四个新计数器（cameraRows / cameraRowsDropped / cameraRowTickMismatch / cameraIdentityMismatch）的实测值；
- `timeline-check.sh` / `history-coverage-check.sh` / `spectate-check.sh` 三个受影响门禁。

## 2 N29 —— 干净环境走查与冻结

### 2.1 「干净」在本仓库的操作定义（可判，不含主观）

1. **没有构建目录**：`native/build-nmake` 与 `native/build` 不存在（今天两者都在）。
2. **没有用户态残留**：产品设置走 INI 持久化（`README.md:9` 写明「audio playback and INI settings persistence」），干净用户目录下该 INI 不存在。
3. **TF 根不在默认位置**：把 TF 根放到一个**非 Steam 默认路径**的目录，验证「找 TF 根」这一步真的在工作，而不是碰巧命中 `C:/Program Files (x86)/Steam/...`。
4. **树是冻结净树**：`git status --porcelain` 为空。**今天不满足**（3 个 M docs + 未跟踪），卡面原话「docs dirty 不可直接冒充发布净树」。

### 2.2 十个步骤，每步给「怎么判」与「它证明什么 / 不证明什么」

| # | 步骤 | 命令形状 | 判据 | 证明 / 不证明 |
| --- | --- | --- | --- | --- |
| 1 | 安装 | `tf2_demo_native.exe --version` 再 `--install <dir>` | 退出码 0；`<dir>` 下出现 manifest 与 VERSION.txt；`--install-status` 报一致 | 证明安装这套 CLI 契约在干净目录成立；**不证明**另一台机器的权限模型相同 |
| 2 | 找 TF 根 | 在非默认路径放 TF 根后启动，不带任何路径参数 | trace 里出现 TF 根路径且等于该非默认路径 | 证明查找逻辑不是硬编码默认路径；**不证明**多库/多语言环境 |
| 3 | 开 demo | `--demo <dem>` | trace 的 `header=1 index=1 ... malformed_offset=0` | 证明解析入口工作；不证明内容正确 |
| 4 | 找 BSP/资产 | 同上，观察资源解析行 | `vpk_query` 能列出的同一批资源在产品侧也被解析（缺失要能报具名原因） | 证明资源通路；**不证明**每个材质视觉正确（见 N05/N06 的 919/849 未归因） |
| 5 | 播放 | `--capture-frame` / 正常播放若干秒 | 帧被写出且非空（D52 的 `.tmp`+`mv` 规则） | 证明播放循环跑通 |
| 6 | seek / 跟随 | 前进/后退 seek 后看相机行 | P1 落地后：`camera: row tick=... stale=0`；未落地前只能看 `relay`/`pov` 行 | **今天做不到 stale=0**（N12-C 未落地） |
| 7 | 音频 | `--audio-disabled` 与非 disabled 各一次 | disabled 时 `audio_device_opened=0` 且 `audio_mode=disabled`；非 disabled 时 `prepared=1` | 证明策略分支与设备通路；**不证明**有人听见（N21 §3 三层表：T2 的 buffer 是静音） |
| 8 | 错误恢复 | 故意给不存在的 demo / 不存在的 TF 根 / 坏的 `--audio-device N` | 退出码与 trace 里有具名原因；坏设备索引 rc=13（`native/src/main.cpp:3233-3236`） | 证明错误路径有可读原因；**不证明**设备中途断开能恢复（D60：设备失败与文件缺失共用一个计数器） |
| 9 | 卸载 | `--uninstall <dir>` | manifest 里 state=new 的文件被删、被覆盖的文件被恢复、用户文件保留 | 证明卸载契约；不证明备份在异常中断下的完整性 |
| 10 | 逐格复核 | 七图 POV/STV、物件/天空/材质、人物/武器、UI/小地图、多视角与设备 | 逐格引用已落盘报告的小节，**数据缺口 / 功能缺口 / 排除项分开写** | 这是复核，不是新测；新观测必须标来源与身份（exe sha 或 tick） |

### 2.3 N29 的三个已知阻塞（今天就不满足，写清楚免得被当成「没做」）

- **A. 没有第二台受支持环境**：本会话只有这一台 Windows 机器。卡面要求「另一受支持环境/干净用户目录」——**「干净用户目录」是当前可做的部分**，「另一环境」不是。
- **B. 全链耗时**：`verify-release.sh` 的 verify-all 步超时上限 14,400 s，文件头注释记「round 2 measured ~150 min」；其余 15 步另有各自上限（rebuild 3600、outline/third-person/body-appearance/class-pixels 各 3600–5400、hud-live 2400…）。**一次全链是一整天量级**，不能与其它构建并行。
- **C. 音频人耳项**：`evidence/audio/listening.txt` 只有一条 2026-10-15 的历史记录（1 听者/1 设备/1 录音），本次走查如果是同一个人同一副耳机，只能算**同一条证据的重复**，不是独立复核。

## 3 N30 —— 一次最终发布链

### 3.1 事实（都已核过）

- `verify-release.sh` 23,383 B / 506 行，**拒绝在 hostlock 之外运行**；`VERIFY_RELEASE_DRY_RUN=1` 可只打印计划不执行（文件头 USAGE 段）。
- 它与 `verify-all.sh` 的差别是后者**不含四个 P0 门禁**（outline / third-person-weapon / body-appearance / class-pixels，`grep -c` 全 0，债务 D-19，出处：`verify-release.sh` 文件头注释原文）。
- `verify-all.sh` 30 步、只调用 99 个门禁里的 29 个（同上注释）；本树根目录现有 130 个 `.sh`（含 helper 与构建脚本，不全是门禁）。
- 政策：全链**只在最终冻结树上跑一次**，不是每任务、不是每天（`docs/RELEASE-POLICY.md` §The rule 第 1、2 条）。

### 3.2 N30 的验收形状（等 N29 完成后按此执行，本轮不跑）

1. `VERIFY_RELEASE_DRY_RUN=1` 先看计划，确认步骤数与今天的树一致；
2. 在 hostlock 下跑一次全链，**保存首个失败**（卡面原话：保存首个失败，不修完再重跑整链）；
3. 定因 → 修候选 → 只跑受影响测试；只有候选改变且发布负责人安排新一次最终尝试时才再跑整链；
4. 产出：二进制、版本/哈希、安装说明、七图与数据限制、控制方式、已知问题、恢复文档；
5. **push/tag/release 是后续明确发布动作**：本清单写不等于发布授权，也不等于已经发布。

### 3.3 可选安装组件（用户裁决 m02102 / m02115）对发布链的影响

裁决一（m02102 原话，语义）：首发**包含**视频导出、**不接受**没有音轨、**不允许**自备 ffmpeg。裁决二（m02115 原话）：「视频导出这个做成可选安装项，到时候在github上发布可选安装插件」。两条叠加 → 导出用产品内 Media Foundation（方案 C）实现，并且**核心安装不含它**，组件在 GitHub 单独发布。

因此 N30 的「一次最终发布链」按今天的事实只能覆盖**核心**，组件侧是新增工作，逐条影响：

1. **产物从一份变两份**：核心包 + 组件包，各自带版本。发布说明必须写清「未安装组件时导出是不可用且会给出具名原因」，否则用户会当成导不出来。
2. **`verify-release.sh` 的 16 步全部指向核心树**，没有任何一步走「未装组件」的路径 → 需要新增一条负控（未装组件时跑 `--export-video`，断言输出里有具名缺失原因、且退出码可判）。今天没这条，N30 不能声称覆盖了组件。
3. **版本耦合**：组件声明最低核心 `packageVersionCode`，新核心配旧组件要能被拒绝而不是崩。比较逻辑落在现有单清单的 `formatVersion`/`packageVersionCode` 上，不能另造一套。
4. **§2.2 的十步走查要加一步**：只装核心 → 导出必须报具名缺失；再装组件 → 同一命令成功。这一步是「可选」这个设计真正被验证的地方。
5. **构建测量（N26）不与核心共用输出目录**：组件是独立模块，若把它的目标塞进 `native/build-nmake`，N26 那份缓存读数立刻失效。

技术前提已在真机成立（mfprobe，2026-11-14 20:44，`D:/_shared/EXEC-2026-11-15/mf-probe/`）：独立 exe 只链接 `mfplat/mfreadwrite/mfuuid/ole32/user32`，从磁盘 BMP 序列 + WAV 直接出 MP4 —— `AddStream(video) S_OK`、`SetInputMediaType(RGB32) S_OK`（**不必自写 NV12 转换**）、`AddStream(audio) S_OK`、`Finalize S_OK`，2 秒 1280×720@30 样片 239,516 B / 327 ms，`MFCreateSourceReaderFromURL` 回读 `streamCount=2 firstFrameDecoded=yes duration=2.005s`，两次同输入编码产物**逐字节相同**（207,556 B）。这只证明「可分离 + 系统自带 MFT + RGB32 直入」成立，**不证明**在产品进程内集成、不证明 AAC 在所有干净环境都在、不证明吞吐够 60 fps 长片（见 mfprobe 报告落盘后补的吞吐数）。
## 4 本文件没有证明什么

- 没有跑 `verify-release.sh`，连 `DRY_RUN` 都没跑（今天树不干净，跑了也只是计划）。
- 没有做任何干净环境走查；§2.2 的十步判据是按代码与已落盘证据写的**可执行清单**，不是实测结果。
- 没有第二台受支持环境，N29 的「另一环境」那一半本会话内无法完成。
- 没有冻结任何东西：源码、门禁、pin、素材版本都还在动（N12-C 与门禁修复正在写）。
- 没有验证「N12-C 落地后旧证据会变多少」——那要等实施完成并重跑。
