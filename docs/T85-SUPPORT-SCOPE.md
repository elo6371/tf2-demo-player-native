# T85 支持边界（冻结声明）

冻结日期：2026-11-07　｜　所有者：t86-phase0（独立执行单元）　｜　阶段 0 交付物
基线：`D:/TF2_Native_Worktrees/t79-integration`，分支 `integration/t79-final`，提交 `e15577b4f0550870bb11672a04497c038c9d88b2`（2026-11-07 11:46:49 +0800）
书写分支：`D:/TF2_Native_Worktrees/t86-phase0`，`task/t86-phase0`，同 HEAD，`git status --porcelain` 为空

**本文件的地位**：这是 e15577b 上"支持边界"的权威声明。它不证明产品能力；任何能力的证明在 `docs/T85-COVERAGE-MANIFEST.md`（声明 → 产品路径 → 夹具 → 门禁断言行 → 变异/负控 → 证据目录）与 `docs/T85-DEBT-LIST.md`（欠账）里。范围之外的东西不得因为"某个门禁绿了"而自动进入支持范围。

**冻结证据（本阶段实测，只读）**

| 项 | 值 | 来源 |
|---|---|---|
| 产品二进制 | `native/build-nmake/tf2_demo_native.exe`，2,763,264 字节 | 对 t79-integration 内文件实测 `stat` |
| 产品二进制 SHA256 | `a3c93d5755fd331351766dfb991fe17f90c4dfb753ad7ef9f53391f0303724e9` | 本次 `sha256sum`，与 `evidence/t62-matrix/RUNS-2026-11-04.md` §26.7 记录一致 |
| 产品源码 | `native/src/main.cpp`，708,416 字节，sha256 `3d6986edfbd8895f2535352219103ee5c9786479f9171345149b3cebff5e4a83` | 本次 `sha256sum` 复算，与 RUNS §26.7 一致 |
| 门禁哈希 | `class-pixels-check.sh` `15c65d0f5126a4dc7b6130ac00aba69731280b6dfb63e6dd9d60a911b5a2389b`；`third-person-weapon-check.sh` `5de35d101f41e5bec08e2cf947820871e43b7414e2bb910c14844edc59453d94` | 本次复算，与 RUNS §26.7 一致；全部 30 个被引用门禁的哈希见 `docs/T85-COVERAGE-MANIFEST.md` §9 |
| host lock 初始状态 | `FREE`，`product_processes=0`，queue 中两条 t85-budget 记录 | 本次 `hostlock.sh status` |
| 本阶段构建/启动产品 | **否**。本文件只读，无构建、无产品进程、无抓帧 | 任务定义（只读分析+写文档） |

## 1. 三个状态词汇先钉死

沿用 `evidence/t62-matrix/CHARACTER-MATRIX.md:15-18` 的定义，任何文档不得自造第四种：

- **PASS**：已提交的读数使该声明成立，且该读数可以按 `path:line` 引用。PASS 只覆盖断言写明的量。
- **NOT VERIFIED**：能力被运行过，但没有已提交读数把这次运行绑到该职业/动作/地图上。
- **UNSUPPORTED**：没有任何门禁测量它，或它被边界排除。

补充（来自 `evidence/t62-matrix/T62-REPORT.md:127` 与 `docs/KNOWN-ISSUES-AND-FIXES.md:3`）："PASS 限于具名门禁，不等于应用可发布"。**不承诺像素级一致**；`world-render-reliability-check.sh:48-52` 的验收边界原文就是 "PASS does NOT prove: pixel parity with the Source renderer"。

## 2. 明确不做（六条硬边界，冻结）

| # | 项 | 决定 | 依据 |
|---|---|---|---|
| B1 | **Ragdoll** | 不做。不实现、不验收、不记作 E1 欠账，也不得改名成 PASS | `evidence/t62-matrix/CHARACTER-MATRIX.md:343`（九张表都有 ragdoll 标签，0/9 被选中）；全仓唯一出现 `FL_TRANSRAGDOLL` 的门禁是 `observer-focus-check.sh:283`/`:291`，那是在断言死亡相机位上的实体标志，不是 ragdoll 渲染 |
| B2 | **Odyssey（`pl_odyssey`）** | 出局。只保留历史证据行，不计入官方地图、不作为支持面 | `evidence/t62-matrix/MAP-MATRIX.md:21`（legend：未标记者为 historical）、`:26-34`（第五行之前即 odyssey 行）；支持的官方四图见 §3 |
| B3 | **水面反射 / 折射** | 不做。水只有"当前贴图/颜色 + warp" | `world-material-effects-check.sh:16-21` 断言的是 water warp 三性质（网格有界、无 NaN、同相位可重复且异相位不同）与像素 on≠off / on==repeat；仓内没有 reflect/refract 通路（`grep -il reflection *.sh` 仅命中该门禁自身） |
| B4 | **T55 视频导出** | UNSUPPORTED（未实现且验收不接受），任何抓帧都不能把它"补成"通过 | `CHARACTER-MATRIX.md:344` |
| B5 | **LOD1+ 与 LOD 切换** | 不宣称。只建 LOD0；`vvdLods=7`/`vtxLods=7` 是计数不是构建 | `CHARACTER-MATRIX.md:131`；`grep -lE 'lod[1-9]|LOD[1-9]' *.sh` 在 99 个脚本里 0 命中 |
| B6 | **与 Source 渲染器的像素级一致** | 不承诺。帧哈希只能证明"同输入同输出"，不能证明"和原版一样" | `world-render-reliability-check.sh:48-52`；`MAP-MATRIX.md:183`（帧 pin 是图片，不是解码值） |

## 3. 地图授权集（唯一允许的集合）

| 地图 | 角色 | BSP 路径（实测存在） | 字节 | SHA256（本次实测） | 证据级别（**授权 ≠ 全验**） |
|---|---|---|---|---|---|
| `cp_snakewater_final1` | 官方 | `tf/maps/cp_snakewater_final1.bsp` | 59,590,420 | `f09d406d2bbe397c8f9df9ca17f11b36b53d40fcf57e3f1532ec9af811b2a1eb` | 世界轴 4/4 PASS + 帧 pin；**角色轴主体**（`class-pixels-check.sh:102`、`player-body-appearance-check.sh:42`、`third-person-weapon-check.sh:85` 的默认 demo 就是本图）；9 职业样本 demos.tf 1 条、本地语料 340 条 |
| `cp_sunshine` | 官方 | `tf/maps/cp_sunshine.bsp` | 30,319,857 | `ca9ad7d01f46112abbc5b02f3d144fad833e6a0ff3bd82a1fff38fa8d3c32474` | 世界轴 4/4 PASS + 帧 pin；**无角色轴证据**：demos.tf 110 条候选里 0 条九职业齐（最好 8/9），本地 2 条 demo |
| `koth_bagel_rc12` | 官方 | `tf/maps/koth_bagel_rc12.bsp`（`tf/download/maps/` 同哈希） | 26,486,963 | `c991a61edb0346e57ad4bfd256693322afb6a522d8005492418b12c094f1d680` | **只有世界轴 4/4 PASS + 帧 pin**；本地 0 条 demo；demos.tf 候选 15 条但 0 条九职业齐（最好 6/9）→ 不可说"播放/角色轴已验证" |
| `koth_bagel_rc11` | 官方 | `tf/download/maps/koth_bagel_rc11.bsp` | 26,370,688 | `f06cfe4b389da5ca152dff0702fb3f4d69b48bfc7b08ed9994e1d057a211720a` | 世界轴 4/4 PASS + 帧 pin；**无已提交的角色轴证据**；本地 1 条 demo（`demostf-1442132`，header 有效），demos.tf 1 条九职业齐（1442132） |
| `pl_upward_f12` | **仅角色/动画夹具**，不是世界轴支持面 | `tf/maps/pl_upward_f12.bsp` | 25,252,306 | `0d9fe5486023df49f0e6d4da43ac3e777baee87f1c588933dc16750cb7c51677` | 仅角色/动画夹具：9 对同职业红蓝（`CHARACTER-MATRIX.md:178-179`）、animation-drive 两批；**没有地图矩阵格**；本地 1 条 demo，demos.tf 101 条九职业齐 |
| `pl_odyssey` | 历史，**不在支持集** | —（不在此重列） | — | — | —（4 格 PASS 为历史读数，见 `MAP-MATRIX.md:31/:40`） |

说明：
- 路径在 `D:/SteamLibrary/steamapps/common/Team Fortress 2/` 下。RC11 只存在于 `tf/download/maps/`，这正是交接书 §8.2 要求的"per-map BSP lookup path + hash"事实；`MAP-MATRIX.md:26-34` 记录的 26,370,688 B 与本次实测一致。
- 夹具 demo（`D:/TF2_Demo_Player/testdata/demos/`）：`bb841c6d379ff7c40d0c8baf99f59d8d_matcha-20260927-1347-cp_snakewater_final1.dem` sha256 `308cc1453672b26dfdcc106bcb62eac4a7eea97899adc8830f51711be834607f`；`4a9bfb9276509d0ec5f5fdc722a95b17_match-20260927-0239-koth_bagel_rc13.dem` sha256 `03529698db749e77a1df1c476c1c0cc06847e1173b5366a0602f1e15161b5436`（bagel_rc13 是**缺 BSP 的负控**，不是地图行，见 `T62-REPORT.md:103`）。
- `koth_bagel_rc13` 永远不以"跑一遍"的方式补：没有 BSP，就是负控。
- **"官方四图"不等于"四图全验"**（2026-11-07 加，防外推）：世界轴（材质/几何/光照）的五图矩阵只覆盖 snakewater/sunshine/bagel_rc12/bagel_rc11（+ 历史 odyssey 行），upward 不在矩阵里；**角色轴只在 snakewater（默认 demo）与 upward（队伍对夹具）上取过证据**，sunshine/rc12 没有角色轴读数。夹具统计来自 task-3 的 `D:/_shared/t85-assets/demo-candidates.csv`（mtime 2026-11-07 13:09，demos.tf 候选）与 `local-demo-index.csv`（mtime 2026-11-07 12:47，本地 467 行）：按地图列计数，rc12 在 demos.tf 有 15 条候选但九职业齐为 0（本地 0 条）；sunshine 最好 8/9。这些是**夹具覆盖度**，不是产品能力。

## 4. 逐能力域：可验证范围（能验到什么，不承诺什么）

| 域 | 可验证到什么（可复核的量） | 门禁 / 断言行 | 明确不承诺 |
|---|---|---|---|
| Demo 解析与时钟 | 一份 demo 的两套 tick 空间、读端双向一致、视点采样按 data-entry tick 递增、POV 相机眼 = 入口 tick 的 view sample、A6 负控（fix 前原点） | `demo-clock-check.sh`（65 asserts，变异 4/4 于 `:513`） | 任意 demo 的字段齐全；字符串表/网络消息的畸形输入矩阵 |
| 播放控制 | 帧步进精确、1s/5s 跳转不动 transport、变速、中断 seek、不可达 tick 的落点映射、缺 demo 负控、产品接线 | `playback-control-check.sh:516`（95 asserts，`:507` 变异 4/4） | 拖拽 scrub 手感；restart 后的一致性（无门禁，见欠账 8.1） |
| 相机 / 观察者 / SourceTV | 相机模式与冻结规则（pause=freeze-tick, camera=continues）、bookmark 三件套、冲突检测、三次死亡三次交接、交接形状、corpse 规则、pause 手动目标存活、16x 回放不算 seek | `camera-ui-check.sh:1212`（90 asserts，`:1208` T50 变异 4/4）；`spectate-check.sh:620`（91 asserts） | 每个 demo 的观察者语义；默认 orbit/follow/free-fly 的逐模式像素等价 |
| 角色静态（A）与涂装（B） | 九模型 LOD0 未截断（`lod0Truncated=0`）、按槽位×皮肤族的涂装（`paintedSlots`）、独立 `*_animations.mdl` 源 | `player-model-check.sh:213`（`lod0Truncated`）、`:294` PASS 行；负控 `:184-190`（tf-root / demo header / 8-entry class table / 未解析模型） | 非 LOD0 绘制；被绘制的 BLU 材质 stem 名；Heavy/Spy 头部外观（`CHARACTER-MATRIX.md:149-153`） |
| 队伍外观（屏幕像素） | 同模型异队九对像素分离（七对 tick 24001 + Scout/Sniper tick 45001），EYE = origin+(-250,0,+58)、pitch 4.1、yaw 0、实体 id 掩码；floor 200px/15 tint/40 pair gap | `player-body-appearance-check.sh:287`；门槛 `:50` | 所有 tick / 动作 / 皮肤组合；"七对推不出九对"的推理（已由 18 目标批次补到九对） |
| 动画 D1 | 序列表、绑定、四分之一周期姿势采样、连续性、时钟与合成/演示速度响应 | `player-animation-check.sh:378`（233 asserts / 0 fail，见 `evidence/t61-animation/r10-after-demos/check.log`） | 服务器序列/周期还原；屏幕上的 walk/reload/airwalk；被驱动的 attack |
| 动画 D2（像素） | 九职业各一具已命名身体、各自 tick：identity 帧两进程字节一致 + 动画开关引起的像素变化（share ≥ 5%、≥ 200 px） | `class-pixels-check.sh:108`（`MIN_BODY_PIXELS=200`）、`:401` PASS 行 | 每职业第二具身体 / 第二队 / 多 tick（`T62-REPORT.md:102`） |
| 动画状态层（opt-in） | `--animation-drive states` 下 crouch/crouchwalk/flinch 的命名 + 该状态的像素、default==legacy 逐字节、四负控 | `animation-drive-states-check.sh:446` | flinch 身体可见性；默认开启；attack 的任何驱动 |
| 第三人称武器（C） | 九职业各一武器一姿势：attach 帧 ≠ off 帧、off == 该职业 class-pixels baseline、挂点/骨骼漂移 0.00、拒绝行 3 | `third-person-weapon-check.sh:580`（九项计数行）、`:582` PASS；cases 默认见 `:82-102` | 每职业每槽位（主/副/近战）；持枪动画（`CHARACTER-MATRIX.md:398`） |
| 第一人称 ViewModel | apply 开/关帧不同、重复一致、绑定与动画输入契约、缺资源只关 ViewModel 不关整个运行 | `viewmodel-apply-check.sh:818`（33 asserts）；`viewmodel-animation-check.sh:1169`；`viewmodel-contract-check.sh:1194` | 默认开启（`main.cpp:2117` 默认 false）；完整武器动作（fire/reload/switch/jump） |
| 世界几何 | 面账两次配平、空点/洞、cull/PVS 移除像素归属、四点 flat 嵌套、点 oracle、两份姿势的帧哈希（每图每姿势 10 个世界帧哈希） | `map-geometry-lighting-check.sh:939`（maps=5 geometry=5 lighting=5）；帧字节 pin 另见 `world-geometry-hole-check.sh:178-179` | 所有视角；**动态 brush 按 demo 时间位移（无门禁，见 8.2 与 COVERAGE-MANIFEST §3）** |
| 世界材质与覆盖 | fetch `capped=0`、`distinct>=tiled`、plan 无畸形/重复、tile 行 == tiled+refused、atlas bound bytes>0、tick 请求==帧 tick、地图名等于行名；tick0 vs tick2000 像素差**只记录不断言大小** | `map-material-matrix.sh:624`（PASS 行）；不变量清单 `:36` | 像素差的语义（不是光照/材质正确性 oracle，`MAP-MATRIX.md:36`） |
| 光照 | 六种光照/着色控制、暗像素分桶账、空种群非空、乘积可归因 ≤2% 帧 | `lighting-semantics-check.sh`（98 asserts）；`map-geometry-lighting-check.sh:38` | 解码后的 lightmap/atlas 数值 pin（`MAP-MATRIX.md:183` 仍 OPEN）；LDR/HDR 边界与 UV clamp 的数值对照 |
| 水面 | 当前水贴图/颜色、warp 三性质、像素 on≠off 且 on==repeat | `world-material-effects-check.sh:16-21` | 反射 / 折射（B3） |
| HUD | 导入/拒绝/绘制（**产品只画彩色矩形**）、三个热键同表、缩放规则、越界矩形外像素不变、坏文档拒绝非半应用。**文字/字体不在产品路径**：`native/src/main_hud.cpp:247-250` 自述 "this build rasterises no font and no image"，唯一文本原语在 `--hud-text-test`（`main_hud.cpp:210`；开关 `native/src/main.cpp:2514-2517`） | `hud-check.sh:388`（`--hud-apply --hud-on`，矩形路径）、`:1402`（PASS，71 asserts）、`:1392`（变异 4/4）；`hud-text-check.sh:658`（**测试路径**：`:284` 用 `--hud-text-test`） | 计时/比分/玩家状态等**文字字段的显示**（产品不渲染文字，见 COVERAGE-MANIFEST N18）；任意 HUD 文档；"看起来对"的视觉判断 |
| 小地图 | **仅探针逻辑，产品内无小地图通路 → UNSUPPORTED**：`native/src/minimap.cpp` 只链进 `minimap_probe`（`native/cmake/tf2-probes-minimap.cmake:23`），不在产品源清单（`native/cmake/tf2-product.cmake:16` 起的 target 列表无此文件），`main.cpp` 中 `minimap` 零命中；产品侧只有 `native/src/review_workspace.cpp:367-375` 持久化 `workspace.minimap` 状态（zoom/pan/mapName/available/reason），无绘制、无投影、无命中。探针可验：bounds 可用性、世界↔地图换算、命中测试、不可用原因 | `minimap-check.sh:36`（`PROBE=native/build-nmake/minimap_probe.exe`）、`:314`（无 `===` 分节、无 `assert_*`，断言写法独异，引用前请直读） | **产品内小地图（绘制/交互/导航）**；游戏内导航正确性 |
| logs.tf 面板 | 面板内容/运行时可用性、九类变异（host/prefetch/静默/resize/导航/脚本/历史/JSON/截断） | `logstf-check.sh:1086`（PASS 行）、`:1081`（变异 17 项） | "页面真的被显示出来"（`:397` 自述：面板 PASS + 该条 NOT VERIFIED） |
| 音频 | 解析、时间线普查、内容映射（MP3 content）、null sink 静默、播放器在真实录音上仍 `--audio-disabled` | `verify-audio.sh:1280`（`assertions_ok`）、`:1275` 变异；`verify-audio-content.sh:566` | **设备 6 可听验收**（不带 `--device` 会跳过）；格式清单成表 |
| 安装与升级 | 全新用户安装、升级/降级按 packageVersionCode 判定、restart 复用根目录、用户数据保留、逐文件 SHA256 清单 | `install-readiness-check.sh:596`/`:836`；`install-lifecycle-check.sh:817`/`:1092`；`native/src/install_manifest.cpp:78`/`:263` | 运行中镜像检查（`install-lifecycle-check.sh:446` 明写为命名延期，不是 PASS） |

## 5. 数字口径（写文档时必须用这一套）

- 角色矩阵：**9 职业 × 5 轴 = 45 格，45 PASS**（`CHARACTER-MATRIX.md:362`）。其中 **18 格**有具名渲染实体（同 `:375`），**九对**同职业红蓝（`:379`）。
- 地图矩阵：**20 格**（5 行 × 4 轴）全部 PASS；其中 **4 格属于 `pl_odyssey` 历史行**，官方四图 = **16 格**。
- 帧 pin：每个"地图 × 姿势"10 个世界帧哈希，共 **100 个**（`T62-REPORT.md:99-101`）；**tick-0 帧不是 pin**（`MAP-MATRIX.md:14`）。
- 18 步冻结清扫（`T62-REPORT.md` §25 / RUNS §25.4）与 45/45 是两件事；不要再引用 "18 steps 覆盖全部" 的说法。

## 6. 变更控制

1. 要把本文件之外的能力写进支持范围，必须先有新门禁 + 新证据，再改本文件；不能先改文档。
2. 补门禁**不得顺带改变默认行为**（交接书 §8.5：是否默认开是功能设计决定）。当前三个默认值钉在：`main.cpp:2117`（`viewModelApplyAnimation=false`）、`:2126`（`playerAnimationEnabled=false`）、`:2133`（`animationDriveStates=false`）、`:2139`（`playerWeaponAttachEnabled=false`）。
3. 本文件与矩阵冲突时：以矩阵的 `path:line` 读数 + 本文件的边界为准，并在 `docs/T85-DEBT-LIST.md` 记一条冲突。
