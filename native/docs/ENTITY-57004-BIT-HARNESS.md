# Bagel PacketEntities bit-level harness evidence

## Reproduction

Build and run the existing diagnostic probe in an otherwise idle working
directory:

```powershell
cmake --build native/build --config Release --parallel 4 --target demo_open_probe
native/build/Release/demo_open_probe.exe --scan "D:\TF2_Demo_Player\testdata\demos\4a9bfb9276509d0ec5f5fdc722a95b17_match-20260927-0239-koth_bagel_rc13.dem"
```

Captured bagel output:

```text
entity_failures=7320 entity_unknown_state_failures=6581
entity_prop_index_failures=217 entity_prop_value_failures=21
first_unavailable_tick=56920 first_unavailable_from=56919
first_unknown_tick=57004 first_unknown_entity=804 first_unknown_update=0
first_unknown_payload=5018 first_unknown_diff=9 first_unknown_delta_from=57003
first_unknown_bit=5146 first_unknown_class_slot=-1
first_entity_failure_stage=preserve first_entity_failure_class=114
first_entity_failure_index=1238938728 first_entity_failure_entity=806
first_entity_failure_class_name=CSceneEntity first_entity_failure_prop_count=22
```

The existing raw payload evidence records the exact packet boundaries:

| network tick | deltaFrom | payload bits | payload bytes | message bit |
| ---: | ---: | ---: | ---: | ---: |
| 56920 | 56919 | 3218 | 403 | 76 |
| 57003 | 57002 | 3316 | 415 | 76 |
| 57004 | 57003 | 5018 | 628 | 76 |

For tick 57004 the update headers are consumed through entity 804 at bit
offset 5146. The next observed update has `diff=9` and entity index 804 with
`Preserve`; the same packet later attempts a property index of `1238938728`
for entity 806/class 114 (`CSceneEntity`, 22 flattened properties). This is
an internal property-bitstream divergence, not an entity-header failure.

## Control run

The same executable and decoder on the local snakewater SourceTV sample
produced:

```text
entities=1427501 entity_failures=0 entity_unknown_state_failures=0
entity_prop_index_failures=0 entity_prop_value_failures=0
entity_history_gap=0 entity_delta_tick_misses=0
```

## Limitations

`demo_open_probe` currently writes raw payload output to the fixed filename
`packet-entities-target-evidence.txt`. Concurrent probes can overwrite it, so
the command must run alone or the file must be copied immediately. The raw
payloads and offsets already committed in `PACKET-ENTITIES-TARGET-EVIDENCE.md`
remain the authoritative bagel capture.

This harness does not establish the missing SendProp schema rule and does not
propose a decoder change. A reliable repair still needs a packet-local replay
that starts from the correct 56919 baseline and independently validates the
entity 804/806 property stream.
