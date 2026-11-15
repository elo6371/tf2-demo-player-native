# 缺陷登记增补 D51–D58（2026-11-15）

本文件补充 `DEFECT-REGISTER-MASTER-2026-11-14.md`（主表止于 D50）。主表的 ID 分配规则不变，本文件只追加，不改主表任何行。

登记人：lead（统筹）。每条都注明**发现者**、**证据落点**、**定级理由**。凡「未证」的，不写成结论。

---

## D51 — 天空面加载器忽略 `$basetexturetransform` 与 `$hdrcompressedTexture`

- **发现者**：n08-n10-maps（N10 工作过程中）
- **缺陷位置**：`native/src/world_material.cpp:480` `loadSkyFace()`
- **证据**：
  - `loadSkyFace` 只消费 `material.baseTexture`，`:499` 在缺失时报 `vmt declares no $basetexture`。
  - `$basetexturetransform` **已被解析**：字段 `VmtMaterial::baseTextureTransformText` 在 `native/include/vmt_material.h:107`，解析在 `native/src/vmt_material.cpp:184` / `:353` / `:365`。**加载器不消费它**。
  - `hdrcompressed` 全仓真 0 命中（换关键词、换目录二次复验）。
- **资产侧实证（lead 亲跑，2026-11-14 19:48，把「不消费」的量级钉死）**：
  `native/build-nmake/vpk_query.exe "<tf>" --dump materials/skybox/sky_upward<side>.vmt`，六个面逐个读：
  `rt` / `lf` / `bk` / `ft` / `dn` **五个面**都写着
  `"$basetexturetransform" "center 0 0 scale 1 2 rotate 0 translate 0 0"`（`scale 1 2` = 纵向 2 倍 tiling）；
  `up` 面的那一行是被注释掉的（`//"$basetexturetransform" ...`）。六个面**全部**带 `"$hdrcompressedTexture"`
  （rt/lf/bk/ft 用 `skybox/sky_upward_hdrside`，up 用 `sky_upward_hdrup`，dn 用 `sky_upward_hdrdn`）
  与 `$nofog 1` / `$ignorez 1`。VMT 体积：rt/lf/bk/ft 各 235 B、up 233 B、dn 231 B，
  来源全为 `tf2_misc_dir.vpk`。
  → 后果不是「少了一个可有可无的选项」，而是**五个侧面纹理在产品里按 1:1 铺满，原设计是纵向拉伸 2 倍**。
  这是 D51 的**可视后果**，形态与本轮其它缺陷同族：不报错、数值合理、含义是假的。
- **范围限定（lead 2026-11-14 20:22 补，防止本条被读成「产品不消费 $basetexturetransform」）**：**世界面是消费的**，缺口只在天空面。证据：`native/src/main.cpp:5266-5282` 逐字段打诊断、`native/src/main.cpp:5736-5743` 把解析结果拷进 tiled 材质（`tiled.baseTextureTransform` / `baseTransformCenter/Scale/Rotate/Translate`）、`native/src/native_renderer_world.cpp:691 if (tiledMaterial.baseTextureTransform && t.sourceWidth != 0 && t.sourceHeight != 0)` 实际作用到 UV。所以 D51 的正确表述是「**天空面加载器**绕过了这套已存在的机制」，不是「机制不存在」。
- **对 METALGRATE013A2 的影响 = 0（maps56 实测）**：该 VMT 全文 200 B 里没有 `$basetexturetransform` 这一行（`$translucent 1`、`$alphatest` 被注释、`$nodecal 1`、`%compilepassbullets 1`），所以 D51 与 919 号像素无关；`docs/N05-N06-MAP-DEFECTS-2026-11-15.md` §3.5 记了这条。
- **定级理由**：解析层做好了、消费层没接，属**未实现**而非解析缺陷。天空面因此拿不到材质自带的 UV 变换与 HDR 压缩标记。
- **不算 N10 的 PASS 反证**：N10 验收（`docs/PROJECT-NEXT-TASKS-2026-11-14.md` 第 195-201 行）判的是「天际几何合成位置正确、不会盖住前景」，`sky_camera` origin+scale=16 只是输入，不是 PASS。
- **过程记录（方法学）**：发现者初次报告写「整仓 `basetexturetransform` 0 命中」，是**假阴性**。根因：`tools.ops_grep` 传**目录**时 `status:error / complete:false / matched:0` 静默返回 0，同目录逐子目录 grep 就有命中。**凡「0 命中」当证据，必须附实际命令 + 路径范围 + 第二种复验。**

---

## D52 — `> file` 重定向在进程启动瞬间即截断，慢启动会被误判为失败

- **发现者**：n07-assets（N07 只读复核）
- **现象**：shell 的 `> file` 在**进程启动的瞬间**就截断目标文件；而探针可能数秒后才写第一字节（gullywash 实测 `wall_ms=9116`）。在这几秒内，任何 `stat` 看到的都是一个**合法的 0 字节文件**，与「探针失败」无法区分。
- **影响面**（这是它成为缺陷而非限制说明的理由）：**任何拿 `stat`/文件存在性或字节数判探针成功率的脚本，都会把慢启动当成功**，从而把「没跑出数据」记成「跑出了空数据」。历史上一份 `named_cp_gullywash_f9.json` 就是这样变成 0 字节的，而探针本身 rc=0、没有报错。
- **修法**：写 `.tmp` → 校验 `rc==0` 且非空 → `mv` 落地。n07-assets 已在全部 N07 产物上落地，三次连跑 sha256 逐字相同。
- **同族**：D53（判决取决于 PATH 上哪个 awk）、D55（行尾字段导致恒绿）、D56（引用了不存在的字段）——**读的一方对环境做了未经证实的假设**。

---

## D53 — 同一门禁、同一份输入，判决取决于 PATH 上哪个 awk 在跑

- **发现者**：arch-ci（初次触发）+ lead（根因实验）+ e17-e20-char（全仓普查）
- **缺陷位置**：`class-pose-family-check.sh:134`（普查确认精确到这一行；同文件 `:125-126` 的 label 比对**是安全的**，因为 label 后面还有 7 个字段）
- **原文**：`if ($i ~ /^finite=/) { split($i, a, "="); if (a[2] != "1") bad = 1 }`
- **机制**：`model_pose_probe` 用 `std::printf` 打 stdout，MSVC stdout 文本模式把 `\n` 翻成 `\r\n`，于是 `a[2]` 实际是 `"1\r"`，与字符串 `"1"` 比较**永假** → 40 个采样帧全判非有限（`decoded=0 failed=49`）。**数据是好的，红是换行造成的。**
- **决定性实验**（2026-11-14 19:08，`.scratch/d53proof.sh` + `.scratch/d53snip.awk`，输入 `evidence/t62-matrix/pose-families/scout-crouch-seq10.txt`）：
  - worktree awk（dsh niubash `winuxcmd/usr/bin/awk.exe`）+ 原样含 CR → `bad=1`
  - Git awk（`/c/Program Files/Git/usr/bin/awk.exe`）+ 原样含 CR → `bad=0`
  - worktree awk + `tr -d '\r'` → `bad=0`
  - `gawk --posix` + 原样 → `bad=0`
  - 单行探针 `printf 'a=1\r\n'` 上 `{print length($0)}`：worktree awk 得 **4**，Git awk 得 **3**。
- **两个 awk 都是 GNU Awk 5.4.1，文件大小都 4,647,424 B**——差异只在**读文件时是否做 CRLF 翻译**。
- **正确定义（不是「一个 if 写错了」）**：**同一门禁、同一份输入，判决取决于 PATH 上哪个 awk 在跑。**
- **对历史结论的影响**：`evidence/t62-matrix/pose-families/` 的 PASS 在会剥 CR 的解释器下今天仍可复现，所以**不是**「门禁被偷偷改过」的证据；但它**也不能当门禁有效的证据**，因为它是**依赖 PATH 的结论**。
- **为什么优先级不低**：N22 要把门禁接进 CI。一个判决取决于 PATH 的门禁接进 CI 就是**随机红绿**，比没有门禁更坏。
- **修法排序**：① 门禁内 `tr -d '\r'`（最稳）；② 探针改写 LF（**不推荐**，破坏历史可比性）；③ 依赖 `gawk --posix`（不可移植）。
- **前置状态**：属**共享 gate helper**，改动会让所有跑过它的证据失效，按串行规则需独占写域。**等 N12 系列落地后单独派。**

---

## D54 — 探针与产品走不同的 VMT 解析入口，探针的 `vmtNoBase` 不能当产品缺纹理的证据

- **发现者**：n07-assets
- **缺陷位置**：`native/tools/material_coverage_probe.cpp:371` 调 `VmtParser::parse` 时**没有传 include reader**；而产品在 `native/src/main.cpp:4941-4945` **传了**。
- **后果**：探针报的 `vmtNoBase` 349 条里，**348 条是 patch 计账产物**（`patch` 材质必须经 include 链才解析出 `$basetexture`），1 条是无 `$basetexture` 的水下底面着色器。**真缺基础纹理 = 0。**
- **定级理由**：这不是产品缺陷，是**工具口径缺陷**。但它会让每一个读探针输出的人得出「产品有 349 个材质缺基础纹理」的错误结论。
- **规则**：**探针与产品走不同解析入口时，探针的计数字段不能直接引用为产品行为的证据。**

---

## D55 — 假绿：`player-animation-check.sh` 的两条断言从来没生效过

- **发现者**：e17-e20-char（D53 全仓普查中撞出）
- **缺陷位置**：`player-animation-check.sh:362` / `:363`（断言在 `:364` / `:365`）
- **producer**：`native/tools/model_pose_probe.cpp:748-750`，格式串以 `advanced=%d\n` 结尾 → `advanced` 是**行尾字段**。
- **机制**：同 D53。awk 里 `adv` 因带 `\r` 不是 strnum，与数字 `0`/`1` 比较退化成字符串比较 → 恒不等 → `bad_move` / `bad_idle` **恒为 0** → `:364`/`:365` 的 `assert_eq` **永远通过**。
- **定级：比 D53 更严重。** D53 是「该红的红了」（假红），D55 是「**该红的永远不会红**」（假绿）。
  - 假红吵人，但会被人看见、会被人修。
  - **假绿让人以为测过了。** 「moving 必须 advance / idle 不得 advance」这两条断言，从写下来那天起就没在测任何东西。
- **要求**：**单独成卡，不许并进 D53。**

---

## D56 —（**已撤回**，2026-11-14 20:43）曾登记为「引用了 producer 根本不打印的字段」，实为 D53 的 CR 家族

- **原发现者**：e17-e20-char（同一轮普查）。**撤回者**：lead，复核 gatefix 的门禁实读结果时推翻。
- **原登记（保留以便追溯，结论已作废）**：`playback-error-paths-check.sh:154` / `:232` 取 `camera: summary` 的 `modeApplies=`、`:227` 取 `mode: summary` 的 `events_seen=`，判为 producer 不打印 → `B-summary` / `E-modeapplies` / `E-events` 恒红。
- **实测事实（两处都查过格式串原文）**：
  - `camera: summary` 的格式串在 HEAD 上是**三个物理源码行**：`native/src/main.cpp:13033` `traceFmt("camera: summary mode=%s modeChanges=%u spacePresses=%u spaceSingles=%u"`、`:13034` `" spaceDoubles=%u doubleWindowMs=%u pauseToggles=%u follows=%u holdsNoTarget=%u"`、`:13035` `" freezeFrames=%u modeApplies=%u",` → **`modeApplies` 存在**，是最后一行的最后一个字段。
  - `mode: summary` 的格式串同样跨两行：`native/src/main.cpp:12982` + `:12983`，而 `:12983` 内就有 `events_seen=%zu events_played=%zu` → **`events_seen` 也存在**（原登记写 `:12857`，`:13012` 的场合同样是漂移后的旧行号）。
- **原登记错在哪一步**：用 grep 在**源码**里查 `modeApplies=`，只匹配到 `camera: summary` 所在的第一个物理行，而那段 C 字符串字面量被拆成三行 → 判成「producer 不打印」。**源码里跨行的 C 字符串不是输出行**；把一种表示当成另一种表示，与 D51（注释当证据）、D53（环境当常量）同族。
- **这两条读数为什么确实是红的（真实机制）**：① 冻结的**修复前二进制**里确实没有该字段 —— `modeApplies` 随提交 `bbf3559`（2026-11-07）才进产品，`evidence/t93-playback/pre-*.trace` 中 `modeApplies` 出现 0 次，所以那时的 `modeApplies=-` 是**该二进制的真实读数**，不是提取坏了；② 在 HEAD 上再读时末字段是 `0\r`（MSVC 文本模式），与 `0` 比不等 → 红，属 **D53**。
- **结论与动作**：D56 **不作为独立缺陷存在**，`playback-error-paths-check.sh` 的读取与断言一律不改（改字段只会掩盖真实原因）。D53 的修复会连带修好这两条；`playback-error-paths-check.sh:154` 附近已留注释，禁止后人「换个字段碰运气」。

---

## D57 — 门禁不钉播放速度，耗时不可预算且随 demo 长度线性膨胀

- **发现者**：n08-n10-maps（第一次显形：N10 抓帧窗口不够，白跑一趟）+ e17-e20-char（同类普查，找到对照）
- **现象**：多个 gate 启动产品时**不传 `--playback-speed`**，产品于是按 demo 实时 tick 率 1× 播放。**门禁自己从不打印播放速度，也不打印预计耗时。**
- **对照证据（同一批 gate，一个中招一个安全）**：
  - **中招**：`animation-drive-states-check.sh` —— 全脚本 `playback-speed` 命中 **0** 次；`run_capture`（`:125-150`）完整命令行只有 `--tf-root / --settings-file / --ui-settings-file / --audio-device / --demo / --capture-tick / --capture-frame / --metrics-file / --player-animation`，**没有 `--playback-speed`**。9 次运行 tick 合计 **143450**，按实测 66.65 ticks/s（= 1×）需 **2152 s ≈ 35.9 分钟**纯播放，加 9 次进程启动 **≈ 36.3 分钟**，贴着「单次持锁 <40 min」只剩 3.7 分钟余量。单次 `TIMEOUT=420s`（`:100`）不会撞，最大一次 cover@20000 = 300 s。
  - **安全**：`attach-weapon-budget-check.sh` —— `SPEED=32`（`:42`），调用行 `:101` 传了 `--capture-tick "$TICK" --playback-speed "$SPEED"`，`TICK=30001` → **约 14 秒**。
  - **中招**：n08 的 N10 抓帧（手工命令行，不是脚本）—— upward STV 播到 tick 40000 需 ≈10 分钟，6 分半窗口从物理上装不下，两份串行更装不下。
- **机制（源码位置）**：`--playback-speed <positive>` 解析在 `native/src/main.cpp:2455-2462`；不传时 `playbackSpeed=0.0`（`native/src/main.cpp:1959`），于是跳过 `native/src/main.cpp:5977-5981` 的 `g_playback.speed = std::clamp(playbackSpeed, 0.125, 64.0)` 覆盖，保持默认 `speed = 1.0`（`native/include/playback_state.h:25`）。
- **为什么是缺陷而不是「慢一点」**：
  1. **耗时不可预算** —— 66.65 ticks/s 是用历史 metrics 的 `elapsed=251.772s tick=16780` **倒算**出来的，门禁自己不打印这个数。
  2. **贴死单次持锁上限** —— 36.3 / 40 min。谁在这里多跑一个负控就翻车。
  3. **随 demo 长度线性膨胀** —— 今天 36 分钟，换一份更长的素材就是 60 分钟，**那时它连跑都跑不完，而且不会报错，只会超时**。
  4. **同一个根因两个方向** —— n08 是「窗口装不下，白跑」，e17 是「装下了，但把锁吃满」。
- **修法**：gate 内显式传 `--playback-speed`（并**打印实际速度与预计耗时**）。属共享 gate helper，归入门禁修复窗口。
- **注**：`--playback-speed` 上限是 **64×**（`std::clamp(playbackSpeed, 0.125, 64.0)`），时钟推进有上限，不能无限加速。

---

## D58 — 没有任何代码负责「Win32 控件矩形不得与 HUD 面板矩形相交」

- **发现者**：ui-d（N15 DPI 与布局评估）
- **现象**：`planUiLayout` / `planHudLayout` / `hudLayoutPanels` **三个函数互不读对方的矩形**。全仓**没有一处代码**负责「控件不许和面板抢地方」。
- **可算后果（42 组 6 DPI × 7 尺寸矩阵中）**：
  - `640x480` 面板压住 **4 个控件**（9.3%）；
  - `800x600` 压住 **2 个**（5.8%）；
  - `1024x640` 起**每一档**都压住 Status **4368 px**（`3840x2160` 也有 0.3%）。
- **机制**：状态栏 y 从 `scale(12)` 起、高 28；面板 y 也从 12 起，**起点相同** → 面板顶部 28 行必然落在状态栏矩形里 = 156×28 = 面板的 **17.1%**；DPI 1.5 时涨到 156×42。
- **为什么门禁永远看不见（关键）**：像素门禁读的是 **D3D 后备缓冲**，**Win32 子控件不在里面**。所以 A2 的 `inside=25600`、A4 的顶边检查都会照常通过。**这是一条结构性的门禁盲区，不是某条断言写错了。**
- **已拆两半（不许合并）**：
  - **布局数学**（没有代码负责）= **已证事实**；
  - **视觉遮挡**（状态栏是否真的涂掉面板）= **NOT VERIFIED**，需可见窗口截屏（锁 + 人在场）。
- **附带发现（逻辑漏洞，当前不可触发）**：丢弃规则只判 y 不判 x —— 客户区宽 <16 时面板被「放置」成 `w=0` 且仍报 `state=ready`。`--window-size` 拒绝 <320x240，故不可触发，仍记在案。
---

## 共同前置（D53 / D55 / D57；D56 已于 2026-11-14 撤回，见该节）

e17-e20-char 在普查报告 `docs/D53-CR-ASSUMPTION-SWEEP-2026-11-15.md` §3 末尾提出一条建议，lead 采纳为**待批准提议**：

> 与其一个个补 `tr -d '\r'`，不如给 gate helper 加一条自检：喂一行已知带 CR 的合成输入，断言解析结果与无 CR 时相同。

这样 D53 这类缺陷由门禁自己抓到，而不是靠人肉普查。**N22 接 CI 时，这条自检本身就该是 CI 的第一个 gate。**

规格化要求（未完成）：
1. 自检函数叫什么、放共享 gate helper 还是每个 check 自调。
2. 幂等性：自检不许改产品 trace。
3. **它自己怎么防止退化成假绿**（自检函数自己被 CR 打瞎就是套娃）。

## 未普查项（不写成结论）

- 普查口径之外的写法（`sed` / `cut` / `IFS` 的其它用法）未穷举。
- **CI runner 的 PATH 里排前面的是哪个 awk 未确定** —— 这直接决定上面每一条「会红」在 CI 上是真红还是假红。不查清，接 CI 的结论只到「本机」为止。

---

## D59 — 提速/降采样会改变「沿量」读数，而门禁不区分状态量与沿量

- **发现者**：lead（在裁决 arch-ci 的 N25 门禁提速提案时，读 producer 源码发现）
- **位置**：`native/src/main.cpp:11202` 与 `:11208-11220`
- **机制**：`animation-drive-states-check.sh` 的 `flinchStarts` 不是状态快照，是一个**边沿检测器**：

      :11202  const bool newTick = !runtime.hasLastHealth || runtime.lastHealthTick != resolvedSceneTick;
      :11208-11220  只在 signals.hasHealth && newTick 时，把本 tick 的 signals.health
                    与上一次「被观测到的」 tick 的 runtime.lastHealth 相比，
                    变低才 ++playerAnimFlinchStarts（:11215），并写回 lastHealth / lastHealthTick。

  它测的是「相邻两个**被观测** tick 之间血量下降」。**观测密度直接决定能看见几个下降沿。**
  1× 时每帧推进约 1 tick（`native/src/playback_control.cpp:303-321` 的 `while (accumulator_ >= 1.0)`）；
  `--playback-speed 8` 时一帧跳 8 个 tick，而场景更新只在帧末那个 `resolvedSceneTick` 上做一次，
  于是 8 个 tick 里只看 1 个。**挨打掉血后又被医疗兵补满、或中途重生回满血这类跨 tick 下降沿会被漏掉，`flinchStarts` 峰值在 8× 下不可复现。**
- **同一条 trace 行里有一半是安全的**：`crouch` / `crouchwalk` 读的是状态字段
  （`duckflags`、`m_bDucked`、`m_bDucking`、`m_flDucktime`，`native/src/main.cpp:11221-11225`），
  持续性状态漏不了 —— **所以「这一行输出能不能在提速下读」本身是个需要逐字段判断的问题，不是整行的是/否。**
- **为什么是缺陷而不是「参数选择」**：门禁不区分「我测的是状态量」与「我测的是沿量」，于是任何人给某个 gate 加
  `--playback-speed` 时都**没有任何信号告诉他这次提速会不会改变被观测的对象**。这与 D53（判决取决于 PATH）、
  D55（恒绿断言）、D56（引用不存在的字段）同族：**读的一方对环境做了未经证实的假设**（这里假设的是「采样密度不影响读数」）。
- **修法方向**：给每个 gate 的每条读数标注「状态量 / 沿量 / 帧计数」，沿量与帧计数类**必须留在该 gate 原本的密度下**，
  或在提速时同时改判据（例如把边沿计数器改成 producer 侧的累计量）。N25 报告应产出这张表。

---

## D60 — 「设备坏了」与「文件没有」共用一个计数器，而资源侧六档分级产品一个都不打

- **发现者**：n21-audio（N21 卡），lead 复核了增量点与 resolver 作用域
- **位置**：`native/src/audio_player.cpp:401`、`:425`、`:442`、`:462`、`:472`（`eventsMissingResource` 全部 5 个增量点）；分级名表 `native/src/audio_timeline.cpp:94 soundResolveStatusName()`
- **机制**：`eventsMissingResource` 在 5 个互不相同的失败形态上各加一次：资源解析失败、解码失败、缓冲不可播放、**设备侧提交失败**等（`:472` 属设备侧，`:425`/`:442` 属资源侧）。产品侧**只打折叠总数**（`native/src/main.cpp:12946-12952` 一带的 summary 行），`soundResolveStatusName` 在 `native/src/` 里**只有定义、零调用**（调用点全在 `native/tools/audio_scheduler_probe.cpp`）。
- **为什么是缺陷而不是「汇总显示」**：`evidence/audio/device-runs.txt` 两次运行 `audio_device_opened` 分别为 **0** 与 **742**，`events_missing` **都是 247** —— 同一个数字既可能表示「设备没开」，也可能表示「文件不在盘上」，**而修法完全不同**（一个是设备路径问题，一个是资产/VMT 问题）。这与 D56（引用不存在的字段）、D55（恒绿断言）同族：**读的一方对数字的含义做了未经证实的假设。**
- **修法要改哪里（任务卡原写法不可编译，此处是修正版）**：`native/src/main.cpp:6076-6105` 的 resolver lambda 里 `SoundResource resource` 声明在 for 循环内（`:6080`），`return false` 在循环外（`:6104`）—— 那一刻 `resource` 已销毁，且 `SoundResource` 本身没有 `status` 字段。正确做法是把「最后一次解析的错误/状态」提升到循环外的局部变量，在 `:6104` 前打一行 trace，**且这只能解「文件没有」这一侧**；设备侧（`:472`）需要另一处 trace 才能分开。
- **本条没有证明什么**：没有改代码，也没有验证「打出一行 trace」之后那道门禁是否真能读它；`device-runs.txt` 的两次运行是历史产物，两次的 demo/tick 是否可比未查。

---

## D61 — 产品不能枚举音频设备，索引是机器局部的，被拒时 trace 里一行都没有

- **发现者**：n21-audio；lead 定位了拒绝路径
- **位置**：解析 `native/src/main.cpp:3140-3146`（`:3143` `if (end && *end == L'\0' && parsed < waveOutGetNumDevs()) audioDevice = static_cast<UINT>(parsed);`），拒绝 `native/src/main.cpp:3233-3234`（只 `OutputDebugStringW` + rc 13），导入 `:6060 soundScheduler.setDeviceId(audioDevice);`，默认 `:1943 UINT audioDevice = WAVE_MAPPER;`
- **机制**：产品**没有 `--list-devices`**（`native/` 里 `list-devices`/`listDevices` 命中 0；唯一列出设备的是 `native/tools/winmm_sink_probe.cpp`）。用户只能用 `--audio-device N` 猜索引，而索引是**这台机器当前设备枚举顺序的位置**，换机器/插拔设备就会变。被拒时只写调试输出并 rc 13，**trace 里没有任何一行说明为什么** —— 与 D56 同族。
- **本条没有证明什么**：没有在目标机器上枚举过设备数量；没有验证 `waveOutGetNumDevs()` 在设备热插拔后是否会变（WinMM 文档如此但本机未实测）。

---

## D62 — 播放倍速完全不进音频路径

- **发现者**：n21-audio；lead 复核为 0 命中
- **证据**：`native/src/audio_player.cpp` 内 `speed|pitch|resample|tempo|timescale`（大小写不敏感）**命中 0**；`g_playback.speed` 的 11 个使用点无一进入音频；音频按 WAV 头采样率**原速**写设备。倍速只经 tick 速率间接影响「哪些事件被触发」，不影响「已触发事件播多快」。
- **为什么记缺陷**：`--playback-speed 64` 下画面快进 64 倍、声音仍是原速且事件被密集触发，实际听感是**一堆重叠的原速采样**。「支持倍速播放」这句话在音频侧不成立（N21 报告把它写成 OPEN，不是 PASS）。
- **本条没有证明什么**：没有人耳听过 64× 下的音频，描述依据是源码与调度结构，不是录音。

---

> **编号说明**：本文件名沿用初版的 `D51-D56`，实际内容已延续到 **D73**（D57–D62 见上；D63–D67 见下；D68–D71 在 D57/D58 补充说明之前，来自实例1 的 R1/E1 两片；D72–D73 同样在 D57/D58 补充说明之前，来自实例4 的 P1a N17）。主表 `docs/DEFECT-REGISTER-MASTER-2026-11-14.md` 止于 D50，**本次仍未改动主表**。

---

## D63 — `FollowReason::PinnedDead` 有枚举、有名字，但没有赋值点

- **发现者**：ui-d（N13 A1–A7 验证，`docs/N13-A1A7-VERIFICATION-2026-11-15.md:199-201`）；lead 复核代码成立
- **证据**：`native/include/target_identity.h:97` 有 `PinnedDead` 枚举成员、`:115` 有 `case FollowReason::PinnedDead: return "pinned-dead";`。`g_followLastReason` 的写点只有 `native/src/main.cpp:1194`（初值 `None`）、`:1475`（`followRefuse`）、`:1501`、`:1510`、`:1527`、`:1569`、`:1583`，**全部在命令路径上**。`followReconcile` 的死分支（`native/src/main.cpp:1643-1650`）只做 `++g_followPinnedDeaths`（`:1646`）、`pinned=false`（`:1647`）、`FollowState::Dead`（`:1648`），**不写 reason**；因此 `:1712` 打印的 `reason=` 是上一次命令留下的值，通常是 `ok`。
- **为什么记缺陷**：`docs/N13-COMMAND-STATE-MODEL-2026-11-15.md:135/:176/:258/:303` 把 `reason=pinned-dead` 写成状态机契约的一部分，A4 的判据直接引用它。数值侧（`pinnedDeaths=1`、`state=dead`）是对的，语义侧是假的：**产品知道"你钉的人死了"，trace 说"一切正常"**。
- **本条没有证明什么**：没有证明 pin 解除本身有错（A4 已用三条独立读数证明它确实解除）；没有证明修法只是一行（`forceReason` 通路与 `g_followLastReason` 的取舍要设计决定，见 D64）。

---

## D64 — 同一次发布里，状态行可能串上一次拒绝的 reason

- **发现者**：ui-d；lead 复核 `followPublishTrace` 全文成立
- **证据**：`native/src/main.cpp:1665-1723` 的 `followPublishTrace` 有两条打印路径：`refused` 分支（`:1686-1701`）用 `g_followCommandReason`，随后的 `keyChanged` 分支（`:1702-1716`）用 `g_followLastReason`，判据是 `keyChanged && (g_followHavePrintKey || !printed)`，所以"拒绝且 key 也变了"时同一调用会出两条。命令拒绝走 `followRefuse`（`:1468-1478`）时 `:1475` 同步了 `g_followLastReason`，两条一致；但 **force 路径**（relay 拒绝的 pin，`:1661-1664` 注释所指）不经过 `followRefuse`，`g_followLastReason` 保持上一次命令的值，第二行就带着无关的 reason。
- **为什么记缺陷**：这是"静默错误"的第二形态——行数对了、字段齐全，但第二行的 reason 描述的是另一件事。任何按 `reason` 归类的 gate 会把一次 relay 拒绝算成上一次命令的原因。
- **本条没有证明什么**：没有穷举 force 路径的全部调用点；也没有证明 A1–A8 现有的哪些读数受影响（实测里未出现两条不一致的样本）。

---

## D65 — `hudLiveResolveName` 两处早返回共用默认 reason，两种"没有名字"无法区分

- **发现者**：ui-d；lead 复核
- **证据**：`native/src/main_hud.cpp:674-683` 的 `HudLiveName` 把 `reason` 默认成 `"no-userinfo-slot"`。`:730` `if (viewEntity < 1) return name;`（**没有视图实体**）与 `:742` `if (slot == nullptr) return name;`（**有实体但 userinfo 里没有该 slot**）都不写 reason，于是两件事在 trace 里是同一个词元。
- **为什么记缺陷**：A6 的判据是"没有名字时 `nameReason` 必须说清是哪一种"。同一函数里 `:731-734`、`:754-762`（`no-tick`）、`:765-768`（`no-userinfo-tick`）、`:769-772`（`no-userinfo-empty`）都各写了专属 reason，只有这两处没有，属**同函数内口径不一致**，不是"没有更好的名字"。
- **本条没有证明什么**：没有测过这两种情况在真实 gate 里的出现频率；A6 实测只覆盖到 `no-userinfo-tick` / `no-userinfo-slot` 两个可观测值。

---

## D66 — `serial` 没有数据源，恒为 0

- **发现者**：ui-d；lead 复核
- **证据**：`native/src/main.cpp:1393-1395` `followSlotSerial` 直接 `return 0;`，`:1386-1392` 的注释写明原因（`SpectateSlot` 与 `EntityState` 都不带 `CBaseHandle`，产品唯一解出的 handle 是相机自己的 `m_hObserverTarget`）。调用点 `native/src/main.cpp:1577`。`0` 的语义是 UNKNOWN，不是"序号 0"。
- **为什么记缺陷**：index-reused 判定依赖 serial 比对，恒 0 使该判定**结构性不可断言**（A5 因此记 UNSUPPORTED 而不是 PASS）。这不是"还没实现"：读数里看起来是一个值，其实是占位符，按值归类会得到假结论。
- **本条没有证明什么**：没有估过补一条 per-entity handle 的代价；也没有证明现有 reuse 分支在 serial 恒 0 下是否还有别的证据可用。

---

## D67 — CLI 越界被吞进 `rejectedFlags`，`index-out-of-range` 词元从命令行不可达

- **发现者**：ui-d；lead 复核
- **证据**：`native/src/main.cpp:1302-1356` 的 `followConsumeArgument`：`--follow-target` 的值不落在 `0..2047`（`:1312`）或根本没有下一个参数（`:1319-1321`）时只 `++g_followArgumentRejected`，**不入命令队列、不经过 `followRefuse`**，于是 `FollowReason::IndexOutOfRange` 不会从 CLI 出现。汇总在 `:14155-14162` 的 `follow: summary ... rejectedFlags=`。
- **为什么记缺陷**：`:1307-1311` 的注释把这条设计写成"拒绝而不是夹取，因为'要 5 号'和'什么都没要'在 trace 里必须看得出不同"——但两者现在落在同一个计数器上，**注释承诺的可区分性没有实现**。A5 的 index-out-of-range 一侧因此测不到。
- **本条没有证明什么**：没有证明命令队列里 `Select` 带 `-1`（`:1346`）那条路径的 `index-out-of-range` 是否可观测；也没有定量测过 `rejectedFlags` 在真实 gate 里的出现频率。

---

## D68　世界材质的归档链不含 hl2，probe 绿而产品红

- **发现者**：arch-ci（实例1 R1）；lead 复核代码与读数
- **证据（产品侧）**：pl_upward POV、`--capture-tick 400`、走锁，trace `D:/_shared/EXEC-2026-11-15/INST1/R1-controls/prod-pl_upward.trace`（2,538,245 B / 15,430 行）——
  `gpu: world visible-refusal reason=vmt-missing materials=2 visibleTriangles=15`
  `gpu: world refused material=DEV/REFLECTIVITY_30 visible=15 reason=vmt-missing vmt=materials/DEV/REFLECTIVITY_30.vmt vmtStore=none vtf=- vtfStore=none src=0x0`
  聚合行 `distinct=82 tiled=79 vmtMissing=2 fromPakfile=5`；标题行 `[VPK entries: 104343]`。
- **证据（probe 侧）**：同图同名的 `material_coverage_probe --list-materials` 报 `source=vpk:hl2_misc_dir.vpk stage=ok`，且 `DEV/REFLECTIVITY_10B` 有 `expect.tile=12`、`GLASS/GLASSWINDOW008D` `expect.tile=15`。**同一个名字，probe 说能画，产品说没有。**
- **机制**：世界材质走 `AssetStoreChain`，它的归档由 `native/src/main.cpp:5572` 的**固定五名名单**逐个 `open` 后 `assetChain.addArchive(...)`（`:5577`）装填（`pak01_dir.vpk`/`tf2_misc_dir.vpk`/`tf2_textures_dir.vpk`/`tf2_sound_misc_dir.vpk`/`tf2_sound_vo_english_dir.vpk`）；N07 新加的 `VpkArchiveSet::openDirectory`（`native/src/vpk_archive.cpp:292-317`，`:315` `mount(tfDirectory.parent_path() / "hl2")`）只服务 `AssetRoot::archives()`，**完全没有进入世界材质链**。
- **为什么记缺陷**：N07 的 hl2 挂载是当天为"这些材质读成不存在"修的，修到的三个消费者（probe、模型、音效）都绿，**唯独真正画世界的那个链没接上**，于是同一个缺陷在屏幕上仍然存在。这不是"还没做"，是"做了且自证已完成"。
- **修法要点（不要照最直觉的方式改）**：`soundArchives` 是**一个容器喂四个消费者** —— `:5611` `vpk: archives opened=%zu`、`:5621` 地图 pakfile 查找、`:5731-5732` `indexedVpkEntries`、`:7026` 音效解码。把 hl2 塞进 `:5572` 的循环会让 `archives opened` 从 5 变 9，并移动 `install-lifecycle-check.sh:233-234`、`install-readiness-check.sh:182/:185/:633`、`resource-reachability-check.sh:80`、`stability-release-check.sh:572`、`visual-baseline-check.sh:275/280-284` 五处 pin；而音效本来也不该从 hl2 取。**正确切口是把「世界材质用的 chain」与「地图 pakfile/音效用的 soundArchives」拆开，只给 chain 追加 hl2，`indexedVpkEntries` 语义保持不变。**
- **本条没有证明什么**：没有做修复，因此**没有正控**（`vmtStore` 从 `none` 变成 vpk 形态、`VPK entries` 从 104,343 变化）。没有测过修复后那 5 处 pin 的新值应该是多少。没有证明 pl_upward 那 2 个材质就是屏幕上肉眼可见的缺陷——只有 `visibleTriangles=15` 这一个量的间接支持。**改点在 `native/src/main.cpp`，属实例3（ui-d）写域，本条未派工。**

---

## D69　`material_coverage_probe` 不校验 `--tf-root`，伪根照样打"成功"读数

- **发现者**：arch-ci（实例1 R1）；lead 复核代码
- **证据**：`--tf-root` 指向不含 `materials`/`models`/`maps` 与任何 VPK 的目录，探针 rc=0，照常打出 `{"reading":"map.stage.ok","value":"48"}`、`{"reading":"plan.tiled","value":"48"}`、`{"reading":"plan.refused","value":"0"}`（`R1-controls/n2-badroot.txt`）。伴随 `map.vpkEntries=0`、`map.vmtMissing=97`、`map.vtfMissing=79`、`map.stage.vmtAbsent=97`。
- **机制**：`native/tools/material_coverage_probe.cpp:750` 直接 `reader.archives = VpkArchiveSet::openDirectory(tfRoot);`，`runMap`（`:741`）从不检查根是否成立。仓库里现成的判定是 `AssetRoot::valid()`（`native/src/asset_root.cpp:121-127`：目录存在 且 `materials`/`models`/`maps` 三个 store 都在），**探针没调它**。对照产品侧：显式 `--tf-root` 有拒绝语义（`native/src/main.cpp:4325-4336` 注释「An explicit command-line root is an operator decision. Do not silently switch to another Steam installation」+ `:4387` `resources: … valid=%d`）。
- **为什么记缺陷**：读者拿到 `plan.tiled=48 refused=0` 会以为"48 个材质被证明可画"，实际是**一个都没解析到**的副产品；判据是 rc 与 `vmtAbsent` 这类字段的话还算能识破，判据是 `stage.ok` 就会被骗。与 D52「`> file` 启动瞬间截断致慢启动假绿」同族：**工具在错输入上给成功形状的输出**。
- **本条没有证明什么**：没有清点哪些现有 gate 用 `stage.ok` 或 `plan.tiled` 当正判据（若全是 `vmtMissing=0` 类判据则本条风险较低）。没有提出修法代码。

---

## D70　`material_coverage_probe` 在非 ASCII 路径上崩溃（D38 的 CLOSED 没有覆盖到它）

- **发现者**：arch-ci（实例1 R1）；lead 记录编号
- **证据**：`cp_sultry_b8a` 的 BSP 只存在于 `E:/demo测试/cp_sultry_b8a/地图/cp_sultry_b8a.bsp`（`tf/maps/` 下没有），探针 rc=`-1073740791`（0xC0000409）。七图普查里唯一崩的一张，其余六张（全 ASCII 路径）rc 全 0。
- **与 D38/D44 的关系**：主表 `docs/DEFECT-REGISTER-MASTER-2026-11-14.md:66` 把 D38（"探针无法打开非 ASCII 路径，实测 0xC0000409 崩溃"）记作 **CLOSED（N02）**，`:72` 的 D44 记"仍有 30 处 `std::filesystem::u8path(argv[...])` 分布在 16 个探针"。本条**不主张 D38 的修法无效**，只记录：**2026-11-15 在 `material_coverage_probe` 上仍然复现同一 rc**，说明 N02 的验收范围没有覆盖这个探针。
- **为什么记缺陷**：一个仍能拿到 0xC0000409 的探针，其崩溃与"路径不存在"在调用方看来都是"没结果"，而这一条的具体后果是**七图普查缺了 sultry 一整行**。
- **本条没有证明什么**：没有定位到崩溃点（是 argv→path 的转换、还是 `directory_iterator`、还是下游）。没有验证 D44 所说的 30 处是否含本探针。

---

## D71　9 个门禁脚本的 `OUT` 缺省直接写进被跟踪的 `evidence/`，其中一个不可覆盖

- **发现者**：arch-ci（实例1 E1）；lead 记录编号
- **证据**：12 个被算术清扫改过的脚本里，**9 个**的 `OUT` 缺省落在被跟踪的 `evidence/` 下，只有 3 个落 `.scratch`。最硬的一例是 `userinfo-check.sh:29`——**没有环境变量覆盖入口**，跑一次就写一次仓库内证据。其余可在调用时用 `<NAME>_OUT=…` 改道。
- **为什么记缺陷**：这一条同时违反两件已定的事 ——（a）`tools/lockrun.sh:22` 把 `evidence` 与 `native/tests/fixtures` 当前后比对对象，**任何写进 evidence 的门禁跑都会让同一次 lockrun 以 `LOCKRUN=TRACKED-DIRTY` exit 97 收尾**；（b）`tools/audit-tree.sh:70-74` 把被跟踪目录变脏记为 `VIOLATION`。也就是说这些脚本的**缺省调用方式本身**就与仓库的两条规定冲突，只能靠调用者每次手工改道。
- **本条没有证明什么**：没有逐个核实那 9 个脚本在实测 gate 运行里是否真的污染过 evidence（历史记录未清点）；没有提出把 `OUT` 缺省改到 `.scratch` 的代价（会改 9 个脚本的行为契约，且部分 gate 的"产物进 evidence 供复现"是有意设计）。

---

## D72　共享主机锁写的是 MSYS pid，别人在另一个 bash 里判活会得出「owner 已死」

- **发现者**：e17-e20-char（P1a N17，`docs/N17-N20-CHAR-COVERAGE-2026-11-15.md:192` 与 §2.6.6）；lead 复核代码成立
- **证据**：`verify-lock.sh:69` 用 `printf '%s\n' "$$" > "$d/pid"` 写锁的属主 pid，`:80`/`:104` 的 `VERIFY-LOCK=ACQUIRED` 行打印的也是 `$$`；判活只有 `vlock_pid_alive()`（`verify-lock.sh:33-38`）里的 `kill -0 "$p"`，回收走 `:83-97`（`vlock_pid_alive "$old_pid"` → 不活则 `reason=owner-dead` → `STALE-LOCK=RECLAIMED` → `rm` 锁目录）。
  实测对照（实例4 记录）：同一个 msys pid 1728，在默认 niubash bash 里 `kill -0` 判 **DEAD**，在 Git Bash 里判 **ALIVE**，Windows 层 `tasklist` 显示 `sleep.exe 43448` 活着 —— **MSYS pid 与 niubash bash 的 pid 空间不互通**。
  现场后果：22:30–22:40 期间实例4 的产品进程（PID 9868 起于 22:30:32、PID 55400 起于 22:35:04，`--player-animation --animation-drive states` 的 capture）在跑，而 `.scratch/verify-all.lock` **整个消失**，`tools/lockrun.sh` 的独占性在窗口内是失效的。实例4 自己的日志则是 `STALE-LOCK=RECLAIMED owner=inst3-n13 pid=4216 age=4s reason=owner-dead` + `VERIFY-LOCK=ACQUIRED owner=e17-e20-char pid=2017`，即它包了锁、锁被别人判死后收回。
- **为什么记缺陷**：`tools/lockrun.sh:7` `. ./verify-lock.sh` + `:12` 的锁目录是全仓唯一的「同时只有一个构建/产品运行」机制，`verify-lock.sh:22-24` 的文件头还把 pid+heartbeat 写成「SIGKILL 也能被下一个人清理」的设计基础。pid 空间不互通意味着**这个机制在跨 bash 的常见调用下会把活锁判死并 rm 掉**，正是它声称要防的那类并发。心跳（`:53-63`，每 `VLOCK_HEARTBEAT_SECONDS=30` 秒刷新、上限 `VLOCK_STALE_DEFAULT=900`）只能把窗口缩小到「判死 + 认领」之间，**不能修正 pid 判错本身**。
- **建议修法（未实施）**：锁目录同时写 winpid（Git Bash 下 `/proc/$$/winpid`）并用 `tasklist`/`/proc` 判活；`vlock_pid_alive` 在两种 pid 都试。属共享 lock helper 修复域，**本轮未动代码**。
   **第一个具名受害者（23:53:5x，实例4 的 `sweep.log` 原文）**：`VERIFY-LOCK=FAIL (another verify-all run owns …: owner=n05-n06-maps pid=58372 age=5s heartbeat_limit=900s)` 紧接 `STALE-LOCK=RECLAIMED owner=n05-n06-maps pid=59700 age=3s reason=owner-dead dir=…/.scratch/verify-all.lock` 再 `VERIFY-LOCK=ACQUIRED owner=e17-e20-char pid=1949`。**心跳 age 只有 3–5 秒**（远小于 `VLOCK_STALE_DEFAULT=900`）却被判 owner-dead —— 这正是 pid 空间判错而非心跳过期，把 D72 从"理论风险"变成已发生的抢占。实例4 当场的处置是对的：立刻 kill 自己的作业、清掉自己拿的锁、在对方可能真在跑的窗口里没有捕获任何一帧。
   **第二次（23:58:51，同一份 `sweep-v2.log`）**：`STALE-LOCK=RECLAIMED owner=ui-d-n14-basemap pid=59684 age=22s reason=owner-dead dir=…/.scratch/verify-all.lock` 紧接 `VERIFY-LOCK=ACQUIRED owner=e17-e20-char pid=925 … stale_after=900s`，**这次连前面的 `VERIFY-LOCK=FAIL` 都没有**。原因在代码里看得很清楚：`verify-lock.sh:89` 的拒绝条件要求 `alive=yes`，而 `:88` 的 `vlock_pid_alive` 已经把活 pid 判成 `no`，于是控制流直接落到 `:94` `reason=owner-dead` → `:97` 打印 → `:98` `rm -rf`。所以「`age` 只有 22 秒却 `reason=owner-dead`」不是矛盾，**是判活失败的必然输出**；反过来说，凡是看到 `reason=owner-dead` 而 `age` 远小于 `heartbeat_limit` 的回收，都是 D72，不是心跳过期。
- **本条没有证明什么**：23:53:5x 那次已观察到「A 持锁 → B 判死 → B 拿锁」三步，但**没有证明 A 当时确实有产品在跑**（那一步靠 A 自述回看，未做第三方读数）；没有清点历史上被误回收的锁有多少次；修法代价未评估（会改所有调用 lockrun 的脚本的行为，且 `/proc/$$/winpid` 在非 Git Bash 的 MSYS 下不可得时要退化成别的判据）。

---

## D73　medic 蹲姿冻结：族里有会动的循环，pick 落在静态那条

- **发现者**：e17-e20-char（P1a N17，`docs/N17-N20-CHAR-COVERAGE-2026-11-15.md:164`）；lead 复核选序代码成立
- **证据**：`native/include/animation_sequence_match.h:389-409` `chooseCrouchLocomotionSequence` 遍历序列，命中条件依次是「首 token 是 `crouch`」（`:396` `sequenceLabelIsCrouchLocomotion`，见 `:345-347`）、「不是 one-shot」（`:397`）、「`frameCountAt(index) > 1`」（`:398`）→ **第一个满足者直接 return**，`:402-405` 的 posed 回退只用于「全族都只有 1 帧」的情况。
  对 `models/player/medic.mdl` 该循环命中 **seq 65 `crouch_PRIMARY`（31 帧）**；产品 trace 坐实它就是上屏那条：`anim: staterefs model=models/player/medic.mdl crouchMoveSeq=65 crouchMoveLabel=crouch_PRIMARY`。
  该序列的四姿态 cycle checksum **全是 `7af1c3641faccfd1`**，而 frame 游标确实在走（0→7→15→23、`localFrame` 同步、`sectionFrames=0`）；同族的 `crouch_SECONDARY`(66) 四姿态互不相同（`25d5b9ee… / 4d49f534… / 7ce55bf1… / ea3db11b…`），medic 的 `crouch_walk_PRIMARY`(81) 四姿态也全不同（`0967a7cb52072dca / 59b9f20bbc392cbf / 4cff7e2dc02ed243 / 11ea65f46d25bea5`）→ **解码路径没坏、族里有会动的替代，pick 落在静态那条**。注释 `animation_sequence_match.h:333-336` 自称「only pyro/heavy/medic carry the crouch locomotion family」，实测**九职业全有**多帧循环（同族注释与实际资产不符）。
- **为什么记缺陷**：这是本轮唯一一条「画面级可复现的产品缺陷候选」——玩家蹲着走时身体姿势不变，只滑行。选序规则本身没错（「取第一个多帧 crouch 循环」是合理的），错在**没有对「四个 cycle 姿态完全相同」做排除**，而资产里恰好有满足条件的替代序列。
- **建议修法（未实施）**：在 `chooseCrouchLocomotionSequence` 的选择条件里加一条「该序列的多 cycle 姿态不是常量」判据（需把 cycle 采样结果传进选序，或把选序挪到能读到解码姿态的层）；要动共享头 `native/include/animation_sequence_match.h`，会影响 model_pose_probe 与产品两个消费者，**本轮未动代码，等派单**。同时修正 `:333-336` 那条与资产不符的注释。
- **本条没有证明什么**：没有逐像素证明「蹲走时身体不动」（读数是四姿态 checksum 相等 + 帧游标在走，不是屏幕差分）；没有检查其余八职业是否也有同族静态 pick；没有评估修法对 `crouch` 状态门禁（`animation-drive-states-check.sh` B3 只比 states vs legacy 的 677 px）的影响。

## D57 / D58 补充说明

D57（门禁不钉播放速度）与 D58（控件与面板无人负责不重叠）是在本文件初版之后追加的，编号延续主表，**主表本身未改动**。D57 属共享 gate helper 修复域，D58 的产品侧修复须改 `native/src/main_hud.cpp` 与布局函数，两者都归入门禁修复窗口与后续派工，**本轮均未动代码**。

---

## 本文件没有证明什么

- **没有证明**主表 `DEFECT-REGISTER-MASTER-2026-11-14.md` 里 D01–D50 的任何一条已完成或已失效。本文件只追加。
- **没有证明** D51/D54 造成的视觉后果——它们都只到「代码没消费/口径不一致」，屏幕上画成什么样属 N05/N06 的像素证据。
- **没有证明** D60/D61/D62 的修法代价。D60 的最小改法只能分开「资源侧」一种形态；D61 需要新增一个参数（属命令行表面变更）；D62 若真要修，牵涉重采样与设备侧时钟，**不是一行改动**。
- **没有证明** D55/D56 影响的历史证据范围有多大。D55 的两条断言从未生效，意味着**历史上所有引用 `player-animation-check.sh` 绿跑作为「动作正确」证据的记录都不可信**——但具体哪些记录受影响，未清点。
- **没有证明** D53 的修法不会引入新问题。`tr -d '\r'` 只在「CR 只作为行尾」时正确；若某天 trace 里出现行内 CR，该修法会静默吃字符。
