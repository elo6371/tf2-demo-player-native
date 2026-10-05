# VPD tail mutation evidence (2026-10-05)

Based on `32f41639c09c0c38b1bb073bcf41f22b87103aa3`, branch
`vpd-32f4163`. The only code change adds a read-only mutation mode to
`item_schema_probe`; no production parser behavior or mapping semantics change.

Real input:
`D:\SteamLibrary\steamapps\common\Team Fortress 2\tf\scripts\protodefs\proto_defs.vpd`

Build and run:

```powershell
cmake -S native -B native/build -G "Visual Studio 17 2022" -A x64
cmake --build native/build --config Release --target item_schema_probe --parallel 2
native/build/Release/item_schema_probe.exe --wire-mutation-self-test `
  'D:\SteamLibrary\steamapps\common\Team Fortress 2\tf\scripts\protodefs\proto_defs.vpd'
```

Focused output:

```text
segment_bytes=111 accepted_prefix=105 failure_file_offset=117 original_tail_hex=53 69 6e 73 68 69
mutation_field=10 wire=0 tag=0x50 parse_ok=0 fields=27 error=unsupported protobuf wire type
mutation_field=10 wire=1 tag=0x51 parse_ok=0 fields=26 error=truncated fixed64
mutation_field=10 wire=2 tag=0x52 parse_ok=0 fields=26 error=truncated length-delimited field
mutation_field=10 wire=3 tag=0x53 parse_ok=0 fields=26 error=unsupported protobuf wire type
mutation_field=10 wire=4 tag=0x54 parse_ok=0 fields=26 error=unsupported protobuf wire type
mutation_field=10 wire=5 tag=0x55 parse_ok=0 fields=27 error=truncated fixed64
mutation_field=10 wire=6 tag=0x56 parse_ok=0 fields=26 error=unsupported protobuf wire type
mutation_field=10 wire=7 tag=0x57 parse_ok=0 fields=26 error=unsupported protobuf wire type
accepted_mutations=0 interpretation=syntax_only schema_inferred=false
```

These mutations show that replacing the first suffix byte with any field-10
wire tag does not make the declared 111-byte segment parse as one generic
protobuf message. They do not prove the VPD segment is meant to be one message
or that the suffix belongs to protobuf. The `.sig` file is 128 bytes and does
not expose field definitions. Keep `decoded=false` and `mapping=false` until a
trusted TF2 VPD framing/schema source is available; do not derive PaintKit to
material paths from these bytes.
