# N12-C Q5 补丁的运行时证据（demo-clock-check.sh 实跑）

日期：2026-11-14 21:17–21:24。树：db0f384（`feat(N12-C): 相机 tick 轨道 + Q5 stale 补丁落地（第一步）`）。
本文件只记这一次实跑观测到的东西。

## 1. 为什么跑

Q5 补丁改了三处 `native/src/main.cpp`：`resolvedSceneTick` 的种子从 `sceneTick` 改为 `-1`、
staleness 在未解析时给 `-1`、只在该查询真正产出 tick 的状态（Available / Checkpoint）发布
`g_sceneResolvedServerTick`。这三处都是**运行时读数**，代码审阅不能代替实跑：改动前
`demo_open_probe` 自己也把 seed 预置成 `snapshotTick`，两端同时"0 = 已解析且恰好同 tick"。

选 `demo-clock-check.sh` 是因为它已经把产品侧与探针侧的 stale 读数绑在同一条断言上
（A3），并且自带 A6 负控（bagel server tick 1 = before-history，56148 = available）。

## 2. 怎么跑

```
cd /d/TF2_Native_Worktrees/t105-e5
bash /d/TF2_Native_Worktrees/_shared/hostlock.sh run demo-clock-check bash demo-clock-check.sh
```

- 跑前 `hostlock.sh status`：`FREE` / `queue=head=` / `reservation=none` / `product_processes=0`。
- 跑后同一状态，锁已释放，无残留产品进程。
- 脚本内 `assert_` 调用点 62 处 —— 这是**静态调用点计数**（`grep -c`），不是实跑执行的断言数；不得表述成「实跑 62 条断言全部通过」。Usage 约定 exit 0 = 每条断言都成立。
- 本次 **exit 0**，墙钟约 6 分 45 秒（21:17:41 起，21:24:26 前结束）。

## 3. 观测到的内容差异（唯一一处）

门禁正常会重写 `evidence/t63-clock/gate/` 下的生成物。把重写后的 7 个受跟踪文件与 HEAD
逐 token 比对，**只有一处内容变化**：

| 文件 | HEAD | 本次 |
| --- | --- | --- |
| `S5-snapshot-1.txt` | `snapshot_stale=0` | `snapshot_stale=-1` |

`S5` 的 tick 1 是 before-history（门禁 A6 负控自己这么写），所以"未解析"是正确的读数；
改前的 `0` 是 seed 造成的伪测量值。`S5-snapshot-56148.txt`（available 那一半）**没有变化**，
另外 5 个文件（`P1-camera.txt`、`P1-scan.txt`、`S1-scan.txt`、`S1-snapshot-A3.txt`、
`S5-scan.txt`）逐 token 完全相同。

结论：Q5 哨兵在运行时确实改变了一个负控读数，而 A1–A6 全部仍然成立。

## 4. 为什么没有把这些生成物留在工作树里

门禁写出的 7 个受跟踪文件带 CRLF（每行 1 个 CR），而 HEAD 里存的是 LF；生成物一落盘，
`tools/audit-tree.sh` 立刻报两行：

```
VIOLATION: evidence is dirty (lockrun.sh will make every gate exit 97)
VIOLATION: evidence/t63-clock/gate/... has 1 CR (HEAD had 0); raw diff 2 lines vs real 2
VERDICT: CRLF-DAMAGE  FORBIDDEN-WRITE
```

所以本轮 `git checkout -- evidence/` 还原了 7 个受跟踪文件，并删掉本次新增的 26 个未跟踪
生成物（`a1-zero.*`、`a1-one.*`、`a2-zero.*`、`a3.*`、`a5.*`、`selftest.trace`）。
还原后 `tools/audit-tree.sh` → `VERDICT: OK (no forbidden writes, line endings intact)`。
**因此本次运行的原始生成物不在仓库里**；要复核请按第 2 节重跑（同样的树、同样的命令）。
下次受影响的窄门禁重跑时，rc、exe/sidecar、原始日志与 before-history 输出必须保存到**独立 OUT** 并附 SHA；本次只能核对报告与提交，不能重验那次运行的原始字节。

## 5. 本文件没有证明什么

- 只跑了 `demo-clock-check.sh` 一个门禁，**不代表其余门禁**；"门禁全绿"仍然没有证据。
- `--mutation` 模式没跑（那是四个变异各自要重编一次，本轮没做）。
- 相机塔的包内 SetView / PacketEntities 顺序仍然 0 样本（pov_f12 上三个 set_view 计数全 0，
  唯一的 setview 在签入 entry 上，不盖章）——本门禁不覆盖这个缺口。
- 单次运行的绿不等于稳定绿：没有做第二次重复运行。
- 生成物被还原，所以"这次到底打印了什么"只有本文件第 3 节的差异表，没有原始字节。
