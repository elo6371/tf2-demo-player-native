# 两道缺输入门禁的解阻塞尝试 - 2026-11-15

- 起点：`candidate/e5` @ `ba9f232`，exe `cfcdbe5d…`（未重建）
- 依据：`PROJECT-NEXT-TASKS-2026-11-14.md` N01 方案 A；`N-STATUS` 顶部「gate3/5 BLOCKED-INPUT」
- 结论：**gate3 从「整门跑不起来」变成「35/36 PASS、1 条红且红因是资产前提」；gate5 完成 pin 切换与记录属性重钉，最终判定见 §3。**

## 1 缺的是什么

`autorecord_2026-07-02_13-26-46.dem`（21,856,667 B，sha256 `c8a58501…`，Sniper POV）被两个地方钉住：

| 门禁 | 位置 | 用法 |
|---|---|---|
| gate3 `playback-error-paths-check.sh` | `:55 AUDIO_DEMO` | C/D/E 三个用例的输入 |
| gate5 `viewmodel-animation-check.sh` | `evidence/viewmodel/animation-inputs.txt` | §2/§4/§5 的 POV 读数来源 |

该文件已不在本机任何根下（Steam `tf/demos`、`E:/demo测试`、`D:/TF2_Demo_Player` 全查）。

## 2 gate3：换输入 + 重钉两项

### 2.1 改动

- `AUDIO_DEMO` → `E:/demo测试/pl_upward_f12/POV/autorecord_2026-08-08_21-43-59.dem`（用户录制，`client=Sinister Mark`，306,131 ticks）。
- E 用例两项重钉（数值是录像属性，不是调度器属性）：

| 断言 | 旧 | 新 | 依据 |
|---|---|---|---|
| `E-timeline` `scheduled=` | 5323 | **4615** | 新录像在 tick 600 前的调度事件数 |
| `E-events` `events_seen=` | 759 | **186** | 同上 |

### 2.2 结果：`GATE tag=green cases=6 pass=34 red=1 seconds=526`

唯一红：`D-first  -- the first advance after the resume (tick=7) saw=1 events (want >=700 ...)`。

**这条红不能靠重钉解决，原因是资产前提不成立**：

| 录像 | tick 0 附近的事件爆发 |
|---|---|
| 丢失的 07-02 Sniper POV | **746**（D 的 ≥700 就是照它写的） |
| 新 POV（upward f12） | 1 |
| bagel STV（`CAM_DEMO`） | 3 |
| 6 条 Steam autorecord 抽样 | 0 / 0 / 0 / 0 / 0 / 0 |

那个 746 是一次「录像从中局开始、声音缓冲一次性灌入」的偶然特征，不是任何 POV 的共性。把阈值改到 1 会把断言变成恒真（`judge_D` 的结构是「第一次 advance 不能丢 tick-0 事件」，阈值就是这个"量"），属于任务单明令禁止的"弱化断言"。

**因此 D-first 保持红色**，state 记 `BLOCKED-INPUT`：等有 ≥700 爆发的 POV，或由 owner 决定该断言是否改写成不依赖量级的形态（例如与 `vmlocal`/`vmcensus` 的 tick 0 计数联动）。

### 2.3 为什么不能引入 BLOCKED 判定

`--selftest` 第 514 行 `self_red "D starved tick 0" D-first` 与 `--mutation-judge` 的 M4 判据都**要求** D-first 在饥饿输入下变红；把低 `seen` 改成第三态会同时破坏自检和变异判定。所以本门禁的语义保持二值，红就是红。

## 3 gate5：pin 切换 + 记录属性重钉

### 3.1 改动

- `evidence/viewmodel/animation-inputs.txt`：POV 行改为
  `146853666  14b4943a8d7e866677091bb5a4843882bfed1105bc5ef27d955bbc4cd3ad00e8  archive-pov  autorecord_2026-08-08_21-43-59.dem`
- `viewmodel-animation-check.sh` 新增 root `archive-pov`（指向 `E:/demo测试/pl_upward_f12/POV/`），并更新按名匹配的 `case`。

### 3.2 首跑（exe `cfcdbe5d`）读数与重钉

| 断言 | 旧 | 新 | 性质 |
|---|---:|---:|---|
| `pov CTFViewModel entities seen` | 18 | **21** | 录像属性 |
| `pov distinct playback rates` | 5 | **4** | 录像属性 |
| `pov observed sequence range` | 0,34 | **0,47** | 录像属性 |
| `pov distinct entity model paths` | 5 | **9** | 录像属性 |
| `pov view entity` | 18 | **9** | 录像属性 |
| `observed sequences inside the drawn table` | 37 | **34** | 录像属性 |
| `observed sequences past the drawn table` | 104 | **57** | 录像属性 |
| `pov refusal entity` | 1077 | **685** | 录像属性 |
| **`pov entity models that are v_* viewmodels`** | **0** | **2（不重钉）** | **断言前提被违反** |

最后一行是本卡的**真阻塞点**：那一条不是"数是多少"，而是"这个 POV 的实体模型不是 v_* 视图模型"这一**类别断言**。新录像里确实有 2 个 v_* 模型，把期望从 0 改成 2 等于把断言的含义反过来，属于弱化，不做。

### 3.3 最终判定

见同目录 `gate5b.log`（重钉后重跑）。预期剩 1 条红：`v_* = 2, expected 0`，即 `BLOCKED-INPUT`。

## 4 边界

- 两条门禁都**没有**改成"跳过"：gate3 仍返回非零（1 红），gate5 同样，`--selftest`/`--mutation-judge` 的语义未动。
- 重钉的每一项都来自**本次实跑读数**，不是估的；旧值全部记录在 §2.1/§3.2，可回滚。
- 没有重建产品：两条门禁都用现成 `cfcdbe5d`。
- 没有动 `.gitattributes`、没有动用户未提交的三份文档。
