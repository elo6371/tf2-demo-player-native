# Item/PaintKit and PCF schema rerun (2026-10-04)

No parser or renderer code changed in this rerun. A repository-wide text search
found no TF2 `.proto`, `CPaintKitDefinition`, PaintKit material-override table,
particle manifest, or verified PCF/DMX schema. The local `items_game.txt` has
`11592` entries but its top-level numeric `paint_kits` table is empty.

The local VPD is `9,830,027` bytes with a `128`-byte `.sig`. Its 12-byte
little-endian prefix is `0,111,95`; the first declared segment starts at offset
12 and is 111 bytes. The bounded generic reader accepts a 105-byte prefix with
26 fields, then rejects the remaining six bytes as `unsupported protobuf wire
type`. This proves a framing/format boundary, not PaintKit field semantics:

```text
wire_ok=0 safe_prefix_bytes=105 safe_prefix_fields=26
boundary_remainder_bytes=6 container_segments=unproven
semantic_status=unproven decoded=false mapping=false
```

Release probe results:

```text
wire_self_test truncated_varint=1 truncated_length=1 valid=1 status=pass
entries=11592
paint_kits=0
item=1151 ... visuals=1 ... manifest_status=3 replacement_status=3
pcf loose_count=0 vpk_status=opened vpk_pcf_count=135 ...
header_validated=false parsed=false rendered=false mapping=false
```

Negative controls behaved as expected: a missing `items_game.txt` returns exit
1 with `cannot open items_game.txt`, and a missing TF2 VPK returns exit 1 with
`vpk_status=missing_or_invalid` and `cannot read VPK directory file`.

Required material to close the gap is a trusted TF2 VPD container/schema
definition (including record boundaries and field numbers), or an authoritative
PaintKit/ItemDef-to-material override dataset, plus a verified PCF/DMX schema and
particle manifest. Until then the only defensible states remain
`Unknown`/`Unavailable`; no PaintKit-to-VTF/material mapping is applied.
