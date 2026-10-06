# Entity history index experiment

实验只记录 PacketEntities 的 `deltaFrom` 是否等于此前已见网络 tick，不改变状态重建逻辑。

## bagel SourceTV

```text
entity_delta_count=73115
entity_delta_tick_matches=73114
entity_delta_tick_misses=1
entity_delta_base_unavailable=72268
entity_failures=7320
entity_unknown_state_failures=6581
```

唯一 tick miss 是首个缺口 `deltaFrom=56919`。但 `entityHistoryHasGap` 在该次失败后保持为真，导致其余 72268 个 tick 已存在的 delta 也被标记 unavailable。这证明问题主要是失败后的全局 history gap 级联，而不是按 packet 序号查找能解决的普遍 tick 映射问题。

## snakewater SourceTV

```text
entity_delta_count=64329
entity_delta_tick_matches=64329
entity_delta_tick_misses=0
entity_delta_base_unavailable=0
entity_failures=0
entity_unknown_state_failures=0
```

## POV 样本

`D:\SteamLibrary\steamapps\common\Team Fortress 2\tf\demos\73.dem` 在 120 秒扫描窗口内未完成，未产生可用实体读数；这属于当前全量扫描耗时限制，不能当作协议失败结论。

## 修复边界

不能简单把最近可用帧作为 `deltaFrom` 基帧，也不能只清除 `entityHistoryHasGap`：bagel 首次失败发生在 Preserve/SendProp 位流阶段，live entity directory 已可能被部分消费。安全修复需要在每个 PacketEntities 边界保存完整帧，失败时恢复到精确 `deltaFrom` 帧后再解析；本实验仅证明该方向和失败级联位置，未提交猜测性修复。
