# PacketEntities 首个失步诊断

## 复现命令

```powershell
cmake --build native/build --config Release --target demo_open_probe
native/build/Release/demo_open_probe.exe --scan "D:\TF2_Demo_Player\testdata\demos\4a9bfb9276509d0ec5f5fdc722a95b17_match-20260927-0239-koth_bagel_rc13.dem"
native/build/Release/demo_open_probe.exe --scan "D:\TF2_Demo_Player\testdata\demos\bb841c6d379ff7c40d0c8baf99f59d8d_matcha-20260927-1347-cp_snakewater_final1.dem"
```

## bagel 结果

```text
entity_failures=7320
entity_unknown_state_failures=6581
entity_prop_index_failures=217
entity_prop_value_failures=21
first_unavailable_tick=56920 first_unavailable_from=56919
first_unknown_tick=57004 first_unknown_entity=804 first_unknown_update=0
first_unknown_entries=25 first_unknown_payload=5018 first_unknown_diff=9
first_unknown_delta_from=57003 first_unknown_bit=5146
first_unknown_class_slot=-1 first_unknown_packet_ordinal=848
first_entity_failure_stage=preserve first_entity_failure_name=index
first_entity_failure_entity=806 first_entity_failure_index=1238938728
```

首个未知实体发生在 Preserve 更新前，实体 804 在当前目录没有 class；同一阶段实体 806 的 SendProp 差值被解成越界 index，说明属性位流已经失步。`deltaFrom=57003` 的前一帧不是问题起点；更早的 56920 -> 56919 历史基帧缺失使后续 delta 基础不可用，之后错误级联。

## snakewater 对照

```text
entity_failures=0
entity_unknown_state_failures=0
entity_prop_index_failures=0
entity_prop_value_failures=0
first_unavailable_tick=-1 first_unknown_tick=-1
entity_history_gap=0 entity_delta_base_unavailable=0
```

## 结论与下一步

当前证据不足以安全修改 PacketEntities 位布局：同一 2-bit 更新头和 SendProp 解码在 snakewater 完整通过。下一步应提取 bagel tick 56919/56920 的两个 PacketEntities 原始 payload、`deltaFrom` 对应帧是否真正存在，以及 SourceTV demo 的 delta frame retention 规则；不能用“最近可用帧”替代精确 `deltaFrom`。
