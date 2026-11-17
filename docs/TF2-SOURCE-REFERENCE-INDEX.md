# TF2 Source Reference Index

Updated 2026-11-17 from `C:/Users/Administrator/Documents/deepseek-harness/default-workspace/TF2-source-resources.md`.

Use these repositories as implementation references. At task start, record the repository URL, checked-out commit, relevant files/symbols, and what was learned. Source snapshots explain code paths; the installed TF2 assets, demo data, independent parsers, and this product's runtime readings decide current behavior.

## Game and engine code

| Reference | Use it for | Limits |
|---|---|---|
| [OthmanAba/TeamFortress2](https://github.com/OthmanAba/TeamFortress2) | TF2-specific player animation state, camera, class/model selection, bodygroups, skins, weapons, and TF material behavior. This is an approved reference for completing the project. | Record the exact revision and compare every finding with current TF2 assets and captures; do not assume its constants or branch state match the installed game. |
| [sr2echa/TF2-Source-Code](https://github.com/sr2echa/TF2-Source-Code) | Secondary mirror for locating TF2-specific implementation. | Cross-check against OthmanAba and runtime evidence; a mirror is not an independent implementation. |
| [lua9520/source-engine-2018-hl2_src](https://github.com/lua9520/source-engine-2018-hl2_src) | Historical Source engine behavior and shared engine code. | Not TF2-specific and not current TF2; use only when the relevant code is shared and verify it. |
| [NicknineTheEagle/TF2-Base](https://github.com/NicknineTheEagle/TF2-Base) | Readable TF2 code ported onto SDK 2013; compare entity and gameplay structure. | Port differences may change behavior; use as a structural cross-check. |
| [ValveSoftware/source-sdk-2013](https://github.com/ValveSoftware/source-sdk-2013) | Official SDK, including TF2-specific `src/game/shared/tf` and `src/game/client/tf` code, shared FOV/animation/material mechanisms and shader infrastructure. | The supplied resource document's claim that TF2 game code has never been officially released is outdated. Pin a revision; shipped TF2 can still differ from the SDK. |
| [Nbc66/source-sdk-2013-ce](https://github.com/Nbc66/source-sdk-2013-ce) | Community SDK fixes and additional engine examples. | Community fork; verify changes against the official SDK and current product requirements. |
| [SourceEngine-CommunityEdition/source](https://github.com/SourceEngine-CommunityEdition/source) | Community engine implementation examples. | Supplemental only; branch-specific behavior must be demonstrated. |

### Required first-pass references

For TF2 character, camera, weapon, or material behavior, begin with [OthmanAba/TeamFortress2](https://github.com/OthmanAba/TeamFortress2). It is currently the broadest TF2-specific source tree in this project's reference set, so inspect its actual code before proposing a fix. It is a community-maintained source snapshot, not Valve's authoritative runtime and not proof that the installed game build behaves identically. Pin the commit and relevant paths for each task.

Cross-check shared engine contracts against [ValveSoftware/source-sdk-2013](https://github.com/ValveSoftware/source-sdk-2013), and shipped assets/data against [SteamTracking/GameTracking-TF2](https://github.com/SteamTracking/GameTracking-TF2) plus the local TF2 installation. For model/material questions, consult Valve's [StudioMDL](https://developer.valvesoftware.com/wiki/StudioMDL), [VDC materials](https://developer.valvesoftware.com/wiki/Material), [Valve Texture Format](https://developer.valvesoftware.com/wiki/Valve_Texture_Format), and [Model Viewer](https://developer.valvesoftware.com/wiki/Model_Viewer) references. For demo/network semantics, use [Source Multiplayer Networking](https://developer.valvesoftware.com/wiki/Source_Multiplayer_Networking) and cross-check with [demostf/parser](https://github.com/demostf/parser), [demostf/demo.js](https://github.com/demostf/demo.js), or [PazerOP/DemoLib2](https://github.com/PazerOP/DemoLib2). For reusable engineering obstacles, consult and update [elo6371/coding-problem-playbook](https://github.com/elo6371/coding-problem-playbook) as required by the installed skill.

For the Scout investigation, inspect OthmanAba and the official SDK's TF2 paths for animation state, sequence/pose parameter setup, model/class selection, skin/bodygroup, and camera/FOV call sites. Use shared SDK code for projection/render mechanisms. Then inspect the installed `scout.mdl`, companion animation model, VVD/VTX, VMT/VTF and exact demo properties; compare a matched tick and entity before changing product logic.

### Checked symbols (2026-11-17)

OthmanAba revision: `1b81dded673d49adebf4d0958e52236ecc28a956` (resolved with `git ls-remote`; files read at this exact revision).

- `tf2_src/game/client/tf/c_tf_player.cpp`, `C_TFPlayer::UpdateClientSideAnimation`: delegates player animation updates to the TF player animation state using eye yaw/pitch.
- `tf2_src/game/shared/tf/tf_playeranimstate.cpp`, `CTFPlayerAnimState::CalcMainActivity` / `TranslateActivity`: chooses standing idle and locomotion as separate activities; an attack sequence is not a generic stand idle.
- `tf2_src/game/client/view.cpp`, `ScaleFOVByWidthRatio`: the horizontal FOV is adapted by viewport width ratio relative to 4:3.

Local cross-check: the installed Scout model exposes `stand_PRIMARY` as sequence 175; the product's old substring idle matcher selected `AttackStand_PRIMARY` (9). Switching idle to the existing whole-token/primary-token selector changes all nine classes to generic `stand_*` sequences. This verifies selection, not a same-tick visual match. Reference code was inspected outside the project; no source was copied into the product.

Further local findings and validated fixes (weighted forearm helpers, independent world-metric projection, sky/fog regression) are recorded in [N17-SCOUT-CAMERA-ANIMATION-2026-11-17.md](N17-SCOUT-CAMERA-ANIMATION-2026-11-17.md). These product measurements must remain separate from assertions about the external source snapshots.

Official SDK revision also checked: `e1f1fd42271805072f052051c26c21caa2c87b04` (resolved from GitHub's `commit/master.patch`, then fetched at the exact revision). [`src/game/shared/tf/tf_playeranimstate.cpp`](https://github.com/ValveSoftware/source-sdk-2013/blob/e1f1fd42271805072f052051c26c21caa2c87b04/src/game/shared/tf/tf_playeranimstate.cpp) contains the TF2 `CalcMainActivity` / `TranslateActivity` implementation, not merely generic animation scaffolding. The fetched file's SHA-256 is `3684e586439037be65dcc83ac8693551dc24f1f72e6696fea9b1292b42a9c5c6`.

## Demo parsers and capture tools

| Reference | Use it for | Limits |
|---|---|---|
| [demostf/parser](https://github.com/demostf/parser) | Independent Rust parser for packet, entity, and demo protocol cross-checks. | Pin a revision and compare fields/tick semantics against local fixtures. |
| [demostf/demo.js](https://github.com/demostf/demo.js) | Independent TypeScript demo parser; useful for camera and entity-property oracle work. | Do not use product output as its expected values. |
| [PazerOP/DemoLib2](https://github.com/PazerOP/DemoLib2) | Independent C++ parser/protocol implementation. | Confirm format/version support against the specific TF2 demo. |
| [Nocrex/tf2-demo-player](https://github.com/Nocrex/tf2-demo-player) | Alternative player architecture and state handling. | Compare behavior and design; do not assume equivalent feature scope. |
| [programmer1o1/demoscope](https://github.com/programmer1o1/demoscope) | General demo parsing and 3D visualization ideas. | Multi-engine parser; verify TF2-specific extensions. |
| [Square789/tf2_dem_py](https://github.com/Square789/tf2_dem_py), [PapaPeach/tf-demo-parser](https://github.com/PapaPeach/tf-demo-parser), [bakapear/demtf](https://github.com/bakapear/demtf) | Additional independent parser cross-checks. | Treat as secondary until a relevant fixture has been verified. |
| [Tails8521/coldmaps](https://github.com/Tails8521/coldmaps), [PapaPeach/PeachREC](https://github.com/PapaPeach/PeachREC), [mzmonsour/open-prec](https://github.com/mzmonsour/open-prec) | Demo-derived map analytics and recording workflows. | Supporting tools, not visual correctness oracles. |

## Maps and game data

| Reference | Use it for | Limits |
|---|---|---|
| [tasbox-org/source-parsers](https://github.com/tasbox-org/source-parsers) | Source BSP/VTF/VMT format parser comparisons. | Verify TF2 lumps, displacement behavior, and pakfile handling against fixtures. |
| [Galaco/bsp](https://github.com/Galaco/bsp) | BSP structure and geometry cross-checks. | Independent Go implementation; not a Source engine rendering oracle. |
| [pySourceSDK/ValveBSP](https://github.com/pySourceSDK/ValveBSP), [ko1N/bsp-rs](https://github.com/ko1N/bsp-rs), [d0mchar/RapidBSP](https://github.com/d0mchar/RapidBSP) | Additional BSP parser and inspection comparisons. | Cross-check coordinate, displacement, face-index, and pakfile conventions. |
| [SteamTracking/GameTracking-TF2](https://github.com/SteamTracking/GameTracking-TF2) | Track shipped TF2 files and update changes; identify current schemas/resources. | Repository file history is evidence about shipped files, not proof of how the game renders them. |
| [tsuza/tf2-item-schema](https://github.com/tsuza/tf2-item-schema), [Nicklason/node-tf2-items](https://github.com/Nicklason/node-tf2-items), [Nicklason/node-tf2-schema](https://github.com/Nicklason/node-tf2-schema), [TF2Autobot/node-tf2-schema](https://github.com/TF2Autobot/node-tf2-schema), [offish/tf2-data](https://github.com/offish/tf2-data), [juliarose/tf2-enum](https://github.com/juliarose/tf2-enum), [sigsegv-mvm/TF2SchemaTools](https://github.com/sigsegv-mvm/TF2SchemaTools) | Item, weapon, effect, enum, and schema data comparisons. | Check update date and game build; local installed schema wins for the tested build. |
| [EpicMorg/UniversalValveToolbox](https://github.com/EpicMorg/UniversalValveToolbox) | Locating Valve-format utilities. | A launcher/toolbox is not an asset or rendering oracle. |

## Per-task source procedure

1. Search the index by task domain. For TF2-specific behavior, inspect OthmanAba; for shared engine behavior, inspect Valve SDK 2013; for binary formats, compare at least one independent parser where practical.
2. Record repository commit/ref, file path, symbol, and whether the source is TF2-specific, shared SDK, community-maintained, or data-only.
3. Reproduce the behavior against a current local asset/demo and a product reading. Keep input identity, tick, entity identity, FOV, and viewport with the evidence.
4. Reuse code only when its license, dependencies, API shape, and behavior fit this native project. Otherwise implement the verified rule locally and record why direct reuse is unsuitable.
5. Do not treat stars, README claims, parser success, or a source constant as runtime evidence.
6. Before implementation, inspect the current GitHub source at the pinned revision (including relevant call sites and history where useful); search the playbook cache/repository for an existing verified fix. After a reproduced issue is fixed and verified, add one structured playbook entry, update its index, commit and push that knowledge change. If the fix is unresolved, do not record it as solved; if push fails, keep the local entry and record the exact failure in the handoff.
