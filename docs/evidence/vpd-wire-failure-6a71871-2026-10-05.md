# VPD wire failure diagnostic (2026-10-05)

This branch is based on `6a71871b592c31f90263dd9dccb09c654325a33b` from
`native-mvp-source`. The only code change adds machine-readable diagnostics to
`item_schema_probe`; it does not decode or infer PaintKit semantics.

Real TF2 resource:
`D:\SteamLibrary\steamapps\common\Team Fortress 2\tf\scripts\protodefs\proto_defs.vpd`

Focused Release output:

```text
entries=11592
paint_kits=0
proto_defs bytes=9830027 signature_bytes=128
header0=0 header1=111 header2=95
wire_ok=0 wire_fields=26 safe_prefix_bytes=105 safe_prefix_fields=26
boundary_failure_segment_offset=105 boundary_failure_file_offset=117
boundary_tail_hex=53 69 6e 73 68 69
container_segments=unproven semantic_status=unproven decoded=false mapping=false
paint_kit=390 status=missing
```

The generic reader therefore consumes 105 bytes of the first declared
111-byte segment and rejects the next bytes as `unsupported protobuf wire
type`. The six-byte suffix begins with ASCII `Sinshi`; without an authoritative
TF2 VPD container/schema definition, it is not safe to interpret this as a
field, record boundary, or PaintKit mapping.
