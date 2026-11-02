# T10 -- the demo_* decoder split, verified

Branch `task/t10-demo-verify`, base `d5839b5` (the published HEAD), worktree
`D:/TF2_Native_Worktrees/t10-demo`. The split itself landed before the publish
(commits e174dba, 6549880); this branch carries the verification the task asks
for: the include/type/link inventory, a build of every target that links the
decoder, and one focused gate.

## What the split is

`native/include/demo_header.h` (1,046 lines) is still the public header: 28
struct/class declarations (DemoHeader, DemoIndex, SendPropSchema, SendTableSchema,
the tick and packet records) and 133 function declarations, no implementation.
The internals moved to `native/src/demo_header_internal.h` (112 lines) and eight
translation units:

| unit | lines | concern |
|---|---|---|
| demo_header.cpp | 395 | the facade: open/read/seek, and buildSoundTimeline |
| demo_header_core.cpp | 305 | container, index, tick space |
| demo_header_net.cpp | 643 | packet stream, NetTick, svc_* dispatch |
| demo_header_entity.cpp | 501 | entity snapshots, props, slots |
| demo_header_strtab.cpp | 652 | string tables and user info |
| demo_header_sound.cpp | 170 | sound channel events |
| demo_header_events.cpp | 352 | game events |
| demo_header_track.cpp | 454 | the per-tick track |

`TF2_DEMO_HEADER_SOURCES` (`native/cmake/tf2-settings.cmake:36`) bundles those
eight with audio_timeline.cpp, audio_resource.cpp, vpk_archive.cpp and
third_party/lzma/LzmaDec.c, and every target below links that one list. So the
decoder has no per-target copy to drift, which is the property the split was for.

## The 24 targets, and they all build

`bash build-target.sh <all 24>` -> **BUILD-RC=0**, 0 compiler errors, 24 binaries
in `native/build-nmake`. Log `build.log` next to this file.

| cmake file | targets |
|---|---|
| tf2-probes-animation | skeleton_skin_probe, weapon_animation_probe |
| tf2-probes-audio | sound_timeline_census_probe, sound_content_map_probe, sound_corpus_scan_probe |
| tf2-probes-core | entity_overlay_probe, demo_open_probe |
| tf2-probes-model | model_pose_probe, viewmodel_range_probe, model_robustness_probe, entity_model_probe |
| tf2-probes-overlay | spectate_probe, projectile_track_probe |
| tf2-probes-presentation | presentation_probe |
| tf2-probes-protocol | entity_protocol_probe, entity_message_fixture_probe |
| tf2-probes-runtime | native_ui_probe |
| tf2-probes-viewmodel | viewmodel_fov_probe, viewmodel_contract_probe, viewmodel_animation_probe, weapon_body_probe, animation_viewmodel_probe |
| tf2-probes-viewmodel-material | viewmodel_material_probe |
| tf2-product | tf2_demo_native |

23 probes + the product = 24. No target was renamed, added or dropped by this
split: the names are the ones the gates and `verify-all.sh` call.

## Focused gate

`bash demo-clock-check.sh` -- the decoder's two tick spaces (entry ticks versus the
server's svc_NetTick stamp), the per-packet table, and the product's playback
origin. **DEMO-CLOCK-CHECK=PASS, 63 OK / 0 FAIL, rc=0.** Log
`demo-clock.log` next to this file.

## What is NOT claimed

Building 24 targets and running one gate says the split did not break the
decoder's contract with its callers. It does not say every probe's *content*
reading still holds on this machine -- each probe has its own gate for that, and
those were run by the 2026-11-02 chain (25/30 player animation, 26/30 player
animation pixels, 28/30 world geometry, 29/30 world materials, all PASS).
