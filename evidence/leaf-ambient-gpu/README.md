# evidence/leaf-ambient-gpu — task-37 Half B (GPU 逐实例叶子环境立方体)

本目录的每个文件都是 2026-11-22 这一轮**从 .scratch/ 用 cp 手工拷进来**的原始产物
（不是脚本生成后直接写这里），文件名改了前缀但内容未编辑。数字与 docs/LEAF-AMBIENT-GPU-2026-11-22.md 一一对应。

## 捕获与引脚
- pin-bare.log / pin-off.log / pin-basic.log / pin-leaf.log / pin-leaf-trace.log —— cp_process_f12，tick 600
- pin-cp_sunshine-leaf-trace.log —— cp_sunshine，tick 600（第二张地图）
- predecessor-md5.txt —— 前任 exe 留下的 .bmp 的 md5（作引脚基线）
- pins-and-counters.txt —— 每个捕获的 md5 对照 + camera/model-lighting/leaf-ambient 三条计数行
- arm-diffs.txt —— 同一 tick 两两捕获的像素差（leaf/basic/leaf-trace/bare，以及前任 leaf 与本轮 leaf）
- bbox-diff-run.txt —— 前任 leaf 与本轮 leaf 差异的 bbox 复算：命令、两个输入的 md5、逐像素输出
  （`(368,110)-(642,273)` 275×164）；前任的 BMP 已被覆盖，只剩转存 PNG，所以这是现场记录
- arms-bare-basic-leaf-leaf-trace.png —— 四臂拼图（目视用）
- probe-rebuild.log —— touch 探针源后重建 + 38 项自检的日志（证明提交的探针源就是产出证据的那份）
- build-warnings.txt —— 09:41 那次批处理的构建段（`BUILD-RC=0`、`BUILD-WARNING-LINES=0`）。
  注意里面的 `SELFTEST-PASSED=34` 是**旧探针**的读数；当前探针是 38 项，见 probe-rebuild.log
- batch-37d.log —— 09:41 那次批处理的原始日志：touch 四个源、两个目标的构建、五个引脚、第二张地图
- self-test-38-checks.txt —— `leaf_ambient_probe --self-test` 全部 38 行输出

## 空立方体与槽表
- empty-cube-census.txt —— 全语料空立方体占比（cp_process_f12 77/3537、cp_badlands 71/3286）
- entity-cubes-tick600.txt —— tick 600 每个被打印玩家实例的六向亮度与室内/室外排序

## 验收（室内 vs 室外）
- player-crops-stats.txt —— 两个玩家的 1:1 裁切统计（leaf 与 basic 逐通道差）
- accept-players-1to1.png —— 同一裁切窗在 leaf / basic 下的 1:1 并排图
- projection-caveat.txt —— 本轮新增相机+世界常量的投影重建结果，以及为什么裁切仍按红色军装 blob 取窗
- accept-projection-attempt.png —— 把投影点画在 leaf 捕获上的标注图

## 尺度
- scale-measurement.txt、measure-leaf-scale.py、lightmap-page-max.py、lightmap-pages.json — Half A 起就有的尺度测量（k=738.229 均值匹配、半/双倍扫描、clip 分数）；Half B 引用未改

## 工具（复现用，路径按 worktree 根解析）
- tools/capture-batch.sh、tools/lock-retry.sh、tools/probe-rebuild.sh —— 锁内重建 + 五个引脚 + 第二张地图
- tools/make-tables.py —— 从 .log/.bmp 生成 pins-and-counters.txt / entity-cubes-tick600.txt / player-crops-stats.txt / arm-diffs.txt
- tools/player-crops.py、tools/player-crop-pair.py —— 红色军装 blob 检测与 1:1 裁切
- tools/projection-accept.py —— 用捕获行的相机+世界常量重建投影（含 hflip）
- tools/arm-cross-diff.py、tools/arm-cross-diff-bbox.py —— 像素差与差异 bbox
