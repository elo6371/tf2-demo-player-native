# evidence/leaf-ambient-gpu — task-37 Half B (GPU 逐实例叶子环境立方体)

本目录的每个文件都是 2026-11-22 这一轮**从 .scratch/ 用 cp 手工拷进来**的原始产物
（不是脚本生成后直接写这里），文件名改了前缀但内容未编辑。数字与 docs/LEAF-AMBIENT-GPU-2026-11-22.md 一一对应。

**本目录同时含修复前后两批产物**：`leaf37f` 那一轮发现 `--lighting leaf` 逐次不同
（槽表被剪切），修复提交 `b1293105`。凡是**引脚/表格类**的文本文件都是修复后重生成的；
修复前的东西只有历史价值，逐个文件在下面标了。当前 exe：
sha256 `94375d7f109f4a00e16fc8af9ecebde71f5c97120dde3f209980b2d5634a0478`，3434496 字节。

## 捕获与引脚（修复后，当前有效）
- pin-bare.log / pin-off.log / pin-basic.log / pin-leaf1..5.log / pin-leaf-trace.log —— cp_process_f12，tick 600
- pin-sunshine-leaf.log / pin-sunshine-leaf2.log / pin-sunshine-leaf-trace.log —— cp_sunshine，tick 600（第二张地图）
- pins-and-counters.txt —— 每个捕获的 md5 对照（含 `retired-pre-fix=` 列）+ camera/model-lighting/leaf-ambient 计数行 + 末段"leaf 臂连跑 5 次是否全同"
- leaf37f-fix-batch.log —— 这一批的构建记录：exe sha256、`BUILD-RC=0`、`BUILD-WARNING-LINES=0`、
  40 项自检、每条捕获的 md5、两次计数行、以及"没有 renderer-invariant 行"
- rebuild-reverify.log —— 改完一行注释重编出的新 exe（`54abaeeb…`）上把每个臂重采一遍，
  8 个 md5 与 `94375d7f…` 那批**逐字节相同**：引脚钉的是源码，不是某一次链接
- leaf-pin.bmp —— `--lighting leaf` 引脚捕获本体（`fa297fb989bd4e2b3100bbd95cecb666`）
- basic-pin.bmp —— `--lighting basic` 同 tick 捕获本体（`d927c52639fb10c62a50b3018806a2d3`）
- leaf-pre-fix.bmp —— **修复前**的 leaf 捕获（`83a85fd7…`，历史，只用来算修复前后的差）
- arms-post-fix.png —— 修复后的四臂拼图（bare / basic / leaf / leaf-trace，目视用）
- predecessor-md5.txt —— 前任 exe 留下的 .bmp 的 md5（历史）

## 不可复现性证据（修复的依据）
- det-3run-prefix.log —— **修复前**同一个 exe 连跑三次：`basic` 三次全同、`leaf-trace` 三次全同、
  `leaf` **三次三个不同 md5**（`868f42e7…` / `5e489226…` / `d3823da4…`），
  而三次的 trace 文本逐字节相同、计数行也完全一样
- retired-pins.txt —— 修复前发布过的每一个 leaf 光照臂 md5 及其作废理由（保留作历史，禁止再引用）
- bbox-diff-run.txt —— **历史**：前任 leaf 与本轮（修复前）leaf 差异的 bbox 复算
  （`(368,110)-(642,273)` 275×164）。两个输入都是从剪切表里读出的抽样，这条记录只说明
  "当时那块在动"，不是基线

## 表格（修复后重生成）
- arm-diffs.txt —— 同一 tick 两两捕获的像素差（leaf vs basic 12.824%、bbox 全画面、
  basic vs leaf-trace 13.686%、bare vs basic 13.315%、off vs bare 0、修复前 leaf vs 引脚 leaf 12.802%）
- entity-cubes-tick600.txt —— tick 600 每个被打印玩家实例的六向亮度与室内/室外排序
  （这份**不受修复影响**：trace 在进 GPU 前就打印了）
- clip-measurement.txt —— 屏幕像素级 clip：leaf 0.651% 任一通道 255 / 0.137% 全白，
  basic 0.303% / 0.093%
- empty-cube-census.txt —— 全语料空立方体占比（cp_process_f12 77/3537、cp_badlands 71/3286）
- self-test-40-checks.txt —— `leaf_ambient_probe --self-test` 全部 40 行输出，
  含新增的 `table.rowIsOneFloat4PerFace` 与 `table.paddingLaneIsNotIdentity`
- pin-leaf.log —— **历史**：修复前那一次 leaf 捕获的日志（内容只有计数行，文本与现在相同，
  但它对应的 .bmp 是 `83a85fd7…` 那一次抽样）。当前的 leaf 引脚日志是 pin-leaf1..5.log
- pin-cp_sunshine-leaf-trace.log —— **历史**名字，内容与 pin-sunshine-leaf-trace.log 相同（同一个捕获）
- self-test-38-checks.txt / probe-rebuild.log / build-warnings.txt / batch-37d.log —— **历史**：
  修复前 38 项自检与 09:41 那次批处理的原始记录（`build-warnings.txt` 里的
  `SELFTEST-PASSED=34` 是更早的旧探针读数，别当当前值）

## 验收（室内 vs 室外）
- player-crops-stats.txt —— 两个玩家的 1:1 裁切统计（leaf 与 basic 逐通道差，修复后重算：
  院子 `+3.2/+3.5/+12.6`、门洞 `-5.8/-3.1/-3.2`）
- accept-players-1to1.png —— 同一裁切窗在 leaf / basic 下的 1:1 并排图（修复后重生成）
- projection-caveat.txt —— 相机+世界常量的投影重建结果，以及为什么裁切仍按红色军装 blob 取窗
- accept-projection-attempt.png —— 把投影点画在 leaf 捕获上的标注图

## 尺度
- scale-measurement.txt、measure-leaf-scale.py、lightmap-page-max.py、lightmap-pages.json — Half A 起就有的尺度测量（k=738.229 均值匹配、半/双倍扫描、clip 分数）；Half B 引用未改

## 工具（复现用，路径按 worktree 根解析）
- tools/capture-batch.sh、tools/lock-retry.sh、tools/probe-rebuild.sh —— 锁内重建 + 五个引脚 + 第二张地图
- tools/make-tables.py —— `python tools/make-tables.py <captures-dir>`（缺省 `.scratch/leafgpu/`）生成
  pins-and-counters.txt / entity-cubes-tick600.txt / player-crops-stats.txt / arm-diffs.txt / clip-measurement.txt；
  末段按臂统计 distinct-md5 并打印 `leaf arm byte-identical across all N runs: YES/NO`
- tools/player-crops.py、tools/player-crop-pair.py —— 红色军装 blob 检测与 1:1 裁切
- tools/projection-accept.py —— 用捕获行的相机+世界常量重建投影（含 hflip）
- tools/arm-cross-diff.py、tools/arm-cross-diff-bbox.py —— 像素差与差异 bbox
