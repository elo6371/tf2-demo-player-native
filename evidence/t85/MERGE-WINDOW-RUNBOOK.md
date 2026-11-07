# T85 合并窗口执行清单（给 lead，全部为一条命令，顺序执行）

本文档不宣称任何结果，只是把 T85 分支上**未跑**的验收项写成可复制的命令、期望 token 与已知坑。
所有命令都假定当前目录 = 仓库根，且**已在 hostlock 内**（合并窗口里你本来就持锁）。
产品启动必须带 `--audio-disabled`（本清单的所有 gate 脚本内部已带）。

## 0. 先备两份输入副本（重要，否则会污染 tracked 文件）

`evidence/t85/inputs/` 被 .gitignore 排除（30M + 20M），合并树里**不存在**。两个像素门禁若用默认基线，
会把 tracked 的 `evidence/t62-matrix/*/ui.json` 交给产品，产品退出时会回写 MRU（D-18 污染）。先做：

```bash
mkdir -p evidence/t85/inputs
cp -r evidence/t62-matrix/class-pixels  evidence/t85/inputs/t62-class-pixels
cp -r evidence/t62-matrix/body-appearance evidence/t85/inputs/t62-body-appearance
```

## 1. 构建（只编，不跑）

```bash
bash build-target.sh tf2_demo_native entity_overlay_probe
echo rc=$?
```

期望：rc=0。分支上已实测过一次 rc=0（设计文档 §8.8）；本步只为确认合并后仍能编。

## 2. outline-check（整轮，≈45 分钟以上）

```bash
rm -rf evidence/t85/merge/outline   # 必须是全新目录：租约级 frame-size 断言会 glob 旧 *.trace
OUTLINE_OUT=evidence/t85/merge/outline bash outline-check.sh
echo rc=$?; grep -c '^[[:space:]]*OK ' evidence/t85/merge/outline/*.log 2>/dev/null
```

- 断言总数 173（口径：`grep -cE '^[[:space:]]*assert_(eq|ne|ge|le|lt|contains)\b' outline-check.sh`）。
- 实测成本：speed=1 单段 999 s，整轮 > 2700 s（分支上被驱动 2700 s 超时杀掉，rc=124，未打印终判，§6.5）。
- 终判行：`OUTLINE-CHECK=PASS` / `=FAIL` / `=PARTIAL`（带 `--skip-live` 时只会是 PARTIAL）。
- 已知 9 条 FAIL 的三个根因已在分支上修掉（trim 丢 budget: 行、layer C 用 slots 当 submitted、租约尺寸），
  但修复本身**从未跑过**，所以这一步是第一次真正验证它们。

## 3. third-person-weapon-check

```bash
T83_WEAPON_BASELINE=evidence/t85/inputs/t62-class-pixels \
T83_WEAPON_OUT=evidence/t85/merge/third-person \
bash third-person-weapon-check.sh; echo rc=$?
```

## 4. player-body-appearance-check

```bash
T62_BODY_INPUT=evidence/t85/inputs/t62-body-appearance \
T62_BODY_OUT=evidence/t85/merge/player-body \
bash player-body-appearance-check.sh; echo rc=$?
```

## 5. 两进程同帧确定性

```bash
bash evidence/t85/run-determinism.sh; echo rc=$?
```

期望：两进程同帧 FRAME sha 相同，脚本打印自己的 token。

## 6. m14（优先级变异，native/ 必须干净）

```bash
git status --short -- native/    # 必须为空，否则 mutate.sh:59 直接 exit 2
bash mutate.sh m14; echo rc=$?
```

期望：`MUTATION-RED` + 至少一条 `FAIL named world models:`；
红字面量形如 `  FAIL named world models: c_flamethrower's world mesh was supplied (it used to OOM) = 0, expected >= 1`。
变异体已单独验证可编译（§8.10），未验证的是它跑起来是否会红。

## 7. speed A/B（DPI/窗口尺寸归因）

```bash
bash evidence/t85/run-speed-ab.sh --ab-speed; echo rc=$?
```

四种结果：`SPEED-AB=PASS`（我的二进制一对同尺寸且逐字节相同）/ `=ENV`（**冻结候选自己**的一对尺寸就变了 → 环境问题，
按 lead m01857(c) 走 `SKIP reason=dpi-changed from=… to=…`，绝不许删断言）/ `=FAIL`（冻结候选守住尺寸而我的没守住）/ `=DIAG`。
注意 `--window-size` 只传给我的二进制：冻结候选会静默忽略未知参数。

## 8. attach-weapon-budget-check（ledger 8.5-5 的 token）

```bash
bash attach-weapon-budget-check.sh; echo rc=$?
```

末行 token：`ATTACH-WEAPON-BUDGET=PASS` 或 `=FAIL`。

## 9. 收尾（每次合并窗口都要做）

```bash
git status --short                      # 任何 evidence 漂移都要 checkout 回去，只保留自己写的
tasklist //FI "IMAGENAME eq tf2_demo_native.exe" | tail -3   # 期望 0 个残留产品进程
```

## 已知坑速查

1. `$OUT` 只 `mkdir -p` 不 `rm -rf`（outline-check.sh:105）→ 复用目录会被旧 trace 影响租约级断言。
2. `--window-size` 在二进制里是**宽字符串**（UTF-16LE 2 处 / ASCII 0 处），grep 二进制时不要用 ASCII 判它不存在。
3. exe 不是位可复现：同源码两次构建 sha 不同（PE TimeDateStamp，§8.9）。别用 exe sha 当源码身份。
4. 冻结候选会静默忽略未知参数 → A/B 时只能给新二进制加新开关。
5. `bash mutate.sh m14` 会 `git checkout -- native/` 还原，所以必须在 native/ 干净时跑。