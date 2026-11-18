# viewmodel-contract-check 的既有失败（2026-11-18）

这两份摘录回答一个问题：N18 的 split ViewModel 改动有没有让 `viewmodel-contract-check.sh` 变红。

结论：没有。同样的两条断言在两个互不相干的树上都失败——

- `t105-anim-audit` @ `979c6904`（N18 之前；只有 anim 分支，没有 split ViewModel 改动）
- `work/integrate` @ `5bac7eaa`（N18 合入后）

复跑命令（两棵树各自跑）：

```
bash build-cmake.sh        # 或 build-target.sh tf2_demo_native
bash viewmodel-contract-check.sh > vm-contract.log 2>&1
grep -n FAIL vm-contract.log
```

失败的是这两条：

1. `FAIL all 7 compared frames came from a measured capture point = 0, expected 7`
   —— 产品 trace 里 7 帧的 `capture tick` 全为 0，而 `sceneTicks` 是 10320（measured points: 10252）。
2. `FAIL identity skinning does not deform the bind pose (rc=1)`
   —— 恒等蒙皮与 bind pose 的负控帧差 1 个像素、`max_delta=1`（其余 2582351 像素相同）。

两个树都是 `assertions_ok=152` + `VIEWMODEL-CONTRACT=FAIL`。也就是说：
数字完全一致，失败与 N18 的 viewmodel 投影/分屏改动无关，是这条门禁自己欠的账（另开卡）。

摘录只保留 FAIL 行与前后各一行，以及最后的 summary 行；完整日志在两棵树的 `.scratch/` 下（不提交）。
