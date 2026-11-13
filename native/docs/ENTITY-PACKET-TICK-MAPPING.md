# PacketEntities tick-axis evidence

This diagnostic compares the Demo command tick (`entryTick`) with the Source
network message tick (`networkTick`) immediately before PacketEntities
decoding. It does not alter entity state or delta resolution.

Command used:

```powershell
cmake --build native/build --config Release --parallel 4 --target demo_open_probe
native/build/Release/demo_open_probe.exe --scan "D:\TF2_Demo_Player\testdata\demos\4a9bfb9276509d0ec5f5fdc722a95b17_match-20260927-0239-koth_bagel_rc13.dem"
```

Observed bagel mapping:

| Demo command tick | Network tick | PacketEntities deltaFrom | Command offset |
| ---: | ---: | ---: | ---: |
| 5109 | 56920 | 56919 | 1471415 |
| 5192 | 57003 | 57002 | 1522594 |
| 5193 | 57004 | 57003 | 1523121 |

The `deltaFrom` value is on the network-tick axis. It is not the Demo command
tick and must not be resolved against `DemoIndexEntry::tick`. The target
`networkTick=56919` has no PacketEntities message in this recording; the next
message is `networkTick=56920`, whose `deltaFrom=56919` therefore refers to a
network frame that is absent from the retained PacketEntities stream.

This result explains the one history miss, but does not prove a repair for the
later packet-internal bitstream failure at network tick 57004. No protocol
change is proposed by this evidence-only change. The existing snakewater
regression remains the acceptance gate for any future decoder change.
