# gate3 验证设计修正 - 2026-11-15

回答审阅意见：「gate3 必须找到起点 ≥700 事件的录像」不成立；真正要验证的是**恢复播放不丢起始事件**。

## 1 旧设计错在哪

```bash
if [ "$S0" -ge 700 ]; then green D-first; else red ...
```

`700` 是丢失的 07-02 Sniper POV 的特征（746 个事件被 clamp 到 tick 0），不是契约。换任何别的录像，这个常数就没意义——而它仍然会把「实现正确 + 录像安静」判成红。

## 2 新设计：窗口来自独立解码

`judge_D <trace> <rc> [lo] [hi]`，窗口由 `census_bounds` 计算：

```
sound_timeline_census_probe --demo <recording>
  → cumulativeTicks=[0,6,10,60,120,300,600,1200,3000]
  → cumulative     =[1,1, 2, 8, 15, 55,186, 640, 1395]
```

- **LO** = 最后一个 ≤ 首次 advance tick 的检查点上的累计数（该 tick 的下界）
- **HI** = 下一个检查点上的累计数（该 tick 之前所有 tick 的上界）
- 判定：`LO <= seen <= HI` 且 `dup == 0`

为什么是**闭区间**：检查点是稀疏的。实测同一个用例会因为 resume 花了多少帧而落在 tick 7 或 tick 9；tick 9 的真值在检查点 6 与 10 之间可能是 2，半开区间会把它误判成红。第一版写成 `[lo,hi)` 就是这么错的，已改。

**没有窗口就是红**，不是跳过：

```bash
red D-first "no independent expectation for tick=$FT: census_bounds returned nothing ..."
```

空输入负控：`sound_timeline_census_probe --demo /nope.dem` 返回 `{"status":"failed","reason":"demo header parse failed"}`，`census_bounds` 返回空，D-first 判红。

## 3 同步修改（审阅要求 ④）

| 对象 | 改动 |
|---|---|
| 判定器 `judge_D` | 增加 `lo/hi` 参数；无窗口即红 |
| 自测夹具 | `d.trace` 干净样本 `seen=750` 配窗口 `700 760`；`d-starved`（`seen=4`）同窗口 → 必红 |
| 变异判定 M4 | 未改判据；M4 仍是 `D-first` 抓、`NOT-CAPABLE` 结论不变 |

自检结果：

```text
PLAYBACK-GATE-SELFTEST: mutants 36 killed / 0 survived; clean fixtures 35 pass / 0 red
SELFTEST-MUTATION-VERDICT: 12 pass / 0 fail
PLAYBACK-GATE-SELFTEST=PASS
```

## 4 真实 POV 上的 D（审阅要求 ⑤）

```text
RUN   playback-error-paths-check.sh --only D
PASS  D-exit
PASS  D-first        (tick=7 saw=1, window [1,2])
PASS  D-dup
PLAYBACK-ERROR-PATHS=PASS        rc=0, 278 s
```

## 5 这条改动证明了什么、没证明什么

**证明**：首次 advance 消费的事件数落在独立解码给出的窗口内；丢弃会被判红（starved 夹具 + M4）；重复进入会被判红（dup）；拿不到独立预期时不会静静地通过。

**没证明**：整条 gate3 全绿。完整跑（6 个用例）在 20 分钟处被我自己的超时切断，已跑到的 27 PASS / 1 RED 里那条红就是上面这个半开区间的 bug（tick=9 saw=2），改闭区间后 D 单独复跑通过；A/B/C/E 本轮没有重跑，不据此宣布整门结果。
