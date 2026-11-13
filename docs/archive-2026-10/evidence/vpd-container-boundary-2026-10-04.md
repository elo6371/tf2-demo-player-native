# VPD container boundary probe

`proto_defs.vpd` is not treated as one protobuf message. The probe reads the
little-endian 12-byte prefix, checks the declared first segment against the file
size, and then reports the longest prefix of that segment which is accepted by
the bounded generic wire reader. Any remaining bytes are reported as a boundary
candidate only; they are not assigned a semantic record type.

This matters because the first segment currently has a declared size of 111
bytes, while the generic reader accepts a 105-byte prefix and rejects the
following bytes as an unsupported wire type. The six-byte remainder may be a
container trailer or another framing field, but this probe does not guess which.
It reports `container_segments=unproven`, keeps `decoded=false` and
`mapping=false`, and therefore cannot turn printable PaintKit strings into a
material mapping.

## Reproduce

```powershell
cmake --build native/build --config Release --target item_schema_probe --parallel 2
native/build/Release/item_schema_probe.exe `
  'D:\SteamLibrary\steamapps\common\Team Fortress 2\tf\scripts\items\items_game.txt' 303 2
```

The relevant output is expected to include:

```text
vpd_header_bytes=12 header0=0 header1=111 header2=95
safe_prefix_bytes=105 boundary_candidate=1 boundary_remainder_bytes=6
container_segments=unproven decoded=false mapping=false
```

The result is a safe diagnostic boundary, not a completed VPD schema decoder.

The native probe also reports the adjacent `proto_defs.vpd.sig` state. The local
signature is present and 128 bytes, but it is treated only as an integrity artifact;
the probe emits `semantic_status=unproven`, `decoded=false`, and `mapping=false`.

## Schema-source check

The local installation also contains `scripts/protodefs/proto_defs.vpd.sig`
(128 bytes) alongside the 9,830,027-byte VPD. The signature is treated as an
integrity artifact only; it does not provide field definitions. A repository-wide
read-only search found no TF2 `CPaintKitDefinition`/`.proto` definition or
`paintkits_master.txt` that could establish the field semantics. The VPD bytes
contain readable strings, but the probe therefore keeps `decoded=false` and
`mapping=false`.
