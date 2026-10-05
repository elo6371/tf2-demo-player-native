# Native asset reference status (2026-10-04)

The native asset resolver now reports `ModelAssetResolution::Unknown` when a
Demo reference contains only `itemDefIndex` and `items_game` supplies a model
candidate. The candidate is useful diagnostic evidence, but it is not the
model path selected by the Demo; reporting `Missing` would claim a failed file
lookup that never occurred.

Release build passed for `tf2_demo_native` and `model_pose_probe`. The local
schema probe reports `entries=11592`, while PaintKit remains unavailable
(`paint_kits=0`). VPK inventory remains read-only and deterministic: the local
TF2 archives expose model, material, texture, sound, and PCF entries through
`VpkArchive::list`; their presence does not establish an ItemDef or PaintKit
binding.

PaintKit/VPD semantics remain `Unknown`/`Unavailable`; no VTF or material
override is inferred from filenames, model candidates, or VPK membership.
