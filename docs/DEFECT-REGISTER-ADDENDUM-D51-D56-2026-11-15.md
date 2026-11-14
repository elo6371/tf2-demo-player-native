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

## D56 — 两条恒红断言，引用了 producer 根本不打印的字段

- **发现者**：e17-e20-char（同一轮普查）
- **缺陷位置与证据**：
  - `playback-error-paths-check.sh:154` / `:232` 取 `camera: summary` 的 `modeApplies=`；producer `native/src/main.cpp:12908` 的格式串是 `mode=%s modeChanges=%u spacePresses=%u spaceSingles=%u` —— **没有这个字段**。
  - `playback-error-paths-check.sh:227` 取 `mode: summary` 的 `events_seen=`；producer `native/src/main.cpp:12857` 只有 `window_created= window_visible= audio_mode= audio_device_opened=` —— **同样没有**。
- **后果**：取不到就是 `-`，于是 `B-summary`、`E-modeapplies`、`E-events` 三条**永远红**。**剥干净 CR 也不会变绿**——它与换行无关。
- **与 D53 的区别**：D53 是「解析被环境改变」，D56 是「解析的目标根本不存在」。两者都会让门禁失去信号，但修法完全不同。

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

## 共同前置（D53 / D55 / D56 / D57）

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

## D57 / D58 补充说明

D57（门禁不钉播放速度）与 D58（控件与面板无人负责不重叠）是在本文件初版之后追加的，编号延续主表，**主表本身未改动**。D57 属共享 gate helper 修复域，D58 的产品侧修复须改 `native/src/main_hud.cpp` 与布局函数，两者都归入门禁修复窗口与后续派工，**本轮均未动代码**。

---

## 本文件没有证明什么

- **没有证明**主表 `DEFECT-REGISTER-MASTER-2026-11-14.md` 里 D01–D50 的任何一条已完成或已失效。本文件只追加。
- **没有证明** D51/D54 造成的视觉后果——它们都只到「代码没消费/口径不一致」，屏幕上画成什么样属 N05/N06 的像素证据。
- **没有证明** D60/D61/D62 的修法代价。D60 的最小改法只能分开「资源侧」一种形态；D61 需要新增一个参数（属命令行表面变更）；D62 若真要修，牵涉重采样与设备侧时钟，**不是一行改动**。
- **没有证明** D55/D56 影响的历史证据范围有多大。D55 的两条断言从未生效，意味着**历史上所有引用 `player-animation-check.sh` 绿跑作为「动作正确」证据的记录都不可信**——但具体哪些记录受影响，未清点。
- **没有证明** D53 的修法不会引入新问题。`tr -d '\r'` 只在「CR 只作为行尾」时正确；若某天 trace 里出现行内 CR，该修法会静默吃字符。
