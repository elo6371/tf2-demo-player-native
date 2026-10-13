T1 归因探针：首帧抓帧落点单变量实验
=====================================

背景
----
verify-render.sh 正向运行中 viewmodel-contract 唯一红项是第 3 节的记账断言：

    FAIL all 7 compared frames came from a measured capture point = 1, expected 7
    ..   capture sceneTicks: 10253 10253 10252 10253 10254 10253 10254  (measured points: 10252 10268)

机制（读产品源码 native/src/main.cpp 确认）
------------------------------------------
1. 回放 tick 由墙钟驱动：main.cpp:2759
       tickAccumulator += elapsed * g_playback.tickRate * g_playback.speed;
       每累计 1.0 执行一次 ++g_playback.tick。
   elapsed 以「上一次 draw 的 QPC 时刻」为原点，且被 clamp 到 250 ms
   （main.cpp:2746，防止调试断点后的快进）。
2. 抓帧条件在第一个 elapsed >= kTargetFrameSeconds(=1/120 s) 的渲染帧触发
   （main.cpp:3562-3565）：tick >= captureTick(=0) 即抓。
3. 因此首帧落点 = floor(首帧延迟 × tickRate)：
   - 首帧延迟 < 15 ms  -> tick 0  -> sceneTick 10252（测量点之一）
   - 首帧延迟 ~240 ms -> tick 16 -> sceneTick 10268（另一测量点）
   - 首帧延迟 15–240 ms -> tick 1–15 -> **标定盲区**（10253–10267，全部判红）
4. 门禁的允许集合 CAPTURE_SCENE_TICKS="10252 10268"（viewmodel-contract-check.sh:706）
   只覆盖两个实测点，中间 15–240 ms 没有任何读数被接受。

单变量探针（每个二进制一次产品启动，参数与门禁 run_product 的 novm 帧完全一致：
--tf-root <tf> --audio-device 6 --demo <matcha demo> --settings-file <scratch>
--capture-frame <scratch> --capture-tick 0 --metrics-file <scratch>，读各自 trace 的
首个 draw: begin 与 scene: begin）
--------------------------------------------------------------------------
二进制来源（均为既有 worktree 中已构建的 exe，只读执行，输出全部重定向到本树 .scratch/）：

| 探针 | exe 来源 worktree | 该树 HEAD | 产品代码构成（git diff 9a8387b..HEAD -- native/） | 首帧 tick | sceneTick | 在允许集合 |
|---|---|---|---|---|---|---|
| p0-base   | render-gate-recheck       | 7d66cc1 | 9a8387b 本体（diff 为空）                  | 16 | 10268 | 是 |
| p1-t1     | viewmodel-material-render | 836076f | +T1 ViewModel 材质（+1455 行）              | 16 | 10268 | 是 |
| p2b-t2    | weapon-body-render        | c0e5699 | +T2 静态武器本体（+1857 行）                | 16 | 10268 | 是 |
| p2-audio  | audio-main-wiring         | 8c9da5c | +T3 音频时间线（+640 行，main.cpp +85）      | 1  | 10253 | 否 |
| p3-all    | render-current（本树）     | 0f023bb | 以上全部                                    | 1  | 10253 | 否（对照组） |

复采（同一实验再跑一轮，各 2 次启动）：
| 探针 | 第 2 次 | 第 3 次 |
|---|---|---|
| p0-base  | tick=16 sceneTick=10268 | tick=16 sceneTick=10268 |
| p2-audio | tick=1  sceneTick=10253 | tick=1  sceneTick=10253 |

结论
----
音频接线（T3：baf93e7 / 61260a0 / 8c9da5c，合并为 69baff8）是唯一把首帧抓帧点
推出允许集合的改动；T1（ViewModel 材质）与 T2（静态武器本体）的二进制首帧仍落在
10268，无罪。0f023bb 对照组与 +音频 二进制行为一致（tick 1 / 10253）。

为什么音频接线会移动落点（trace 实测，见 .scratch/probe/*.trace）
------------------------------------------------------------
p0-base（9a8387b）trace：
    17311.0 mode: window_created=1 ...
    21252.1 scene: begin tick=0 sceneTick=10252      <- 首个循环迭代，比 window_created 晚 3941 ms
    21274.5 draw: begin tick=16 frames=0             <- 首帧；3941 ms 被 clamp 成 250 ms，
                                                       250 ms × 66.67 tps ≈ 16.67 -> tick 16
p2-audio（+音频）trace：
    16389.7 audio: timeline ... read=5292 scheduled=5229 ...
    17180.4 scene: done                                <- 首个循环迭代，时钟原点之后几乎没有间隔
    17195.0 draw: begin tick=1 frames=0              <- 首帧在一个等待周期（~11 ms）后到来 -> tick 1
即：音频接线把主循环起点从「时钟原点后 ~3.94 s」移到「时钟原点后 ~0 ms」，
旧二进制靠 250 ms 钳位白拿 16 tick（正好是测量点 10268）；新二进制没有这段间隔，
首帧只走了一个等待周期，落在 tick 1（10253）——标定盲区内。

对门禁的影响
------------
- 红项是标定断言，不是渲染缺陷：同一 gate 内 153 条断言通过，包括全部像素比对
  （BMP-DIFF 全部 status=PASS）、FOV 钳位、左右手分离、clamp 等价对（0 字节差）等。
- 7 帧仍互为一致（都来自播放位置 1–2），门禁自己的历史注释（check.sh:690-700）
  记载过同类问题：曾因「tick= 标签相等」断言红过一次逐字节相同的运行，于是换成
  「来自已测量抓帧点」；现在音频接线移走了抓帧点，同一族脆弱性以另一种形式复发。
- 修复不在 T1 权限内（不得改判据/产品）：需要门禁 owner 重新测量允许集合
  （把音频构型的落点纳入），或把断言改为「7 帧互为一致」而非对标静态标定值。
- 连带影响：--mutation 运行中，hand-matrix 与 draw-skipped 两个变异会跑第 3 节，
  其失败记录里会各多一条非预期的测量点红（判定逻辑只查具名断言，故仍应报
  MUTATION-CAUGHT=PASS）。见 mutation/viewmodel-contract.txt 与
  mutation-failure-hand-matrix.txt / mutation-failure-draw-skipped.txt。

探针复现命令
------------
bash .scratch/t1-baseline-probe.sh        # 首轮 5 个二进制
bash .scratch/t1-probe-replicate.sh       # 复采轮 4 次启动
原始 trace 在 .scratch/probe/*.trace（gitignore，未提交）；本文件为结论摘要。
