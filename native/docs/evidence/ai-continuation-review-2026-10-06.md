# ai-continuation branch review (2026-10-06)

Reviewed remote branch `https://github.com/sanse25/tf2-demo-player-native/tree/ai-continuation` at commit `381935d14c4d74c8d31bec823a171b39abd02068` in an isolated worktree.

## Verified

- CMake configuration with the installed Windows SDK and MSVC succeeded.
- A clean Release build succeeded in the isolated worktree.
- With `TF_ROOT=D:\\SteamLibrary\\steamapps\\common\\Team Fortress 2\\tf`,
  `animation_viewmodel_probe.exe` exited 0 and reported:

```text
selfTest=true parent=true loop=true compressed=true malformed=true
fov=true missingCompanions=true bindPose=true scout=true soldier=true
motion=true section=true realLoop=true deterministic=true viewmodel=true
motionSequence=AttackStand_PRIMARY bones=76 anims=1012
viewSequences=8 viewSequence=idle viewAttachments=1 viewAttachment=weapon_bone
confidence=bit-layout; Euler order and position scale are not visually verified
```

- `audio_effects_probe`, `world_material_probe`, `presentation_probe`,
  `entity_model_probe`, `native_ui_probe`, and `temp_entities_fixture_probe`
  exited 0 in the isolated build.

## Reasons for staged integration

The branch is not an animation/ViewModel-only change. Relative to `native-mvp`
it changes 61 files, adds about 5,296 lines, removes about 2,461 lines, and
deletes or replaces existing evidence and gate files. It also changes
`demo_header.cpp`, `native_renderer.cpp`, `bsp_map.cpp`, `main.cpp`, CMake
targets, and the UI. A full bagel and snakewater entity regression was not
completed in the review window; a bagel scan remained running beyond the
bounded observation window and was terminated.

The animation probe explicitly does not claim Euler order or position scale
visual correctness. The renderer still does not upload animation bone
matrices or draw ViewModels. Therefore this branch is evidence for a later
stage-5 extraction, not a merge candidate for the current entity/base-replay
baseline.

## Decision

Do not merge `381935d` or its parent range wholesale into `native-mvp`. Preserve
the isolated worktree for a later, file-scoped extraction after PacketEntities
and base-render regression gates are green.
