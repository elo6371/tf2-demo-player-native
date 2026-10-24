# T65 — proposals (runtime work that needs scheduling)

Nothing here was executed in this phase. Each proposal states the exact command, the duration, why it
needs the shared host lock, and what it would prove. All of them are serial-only.

## P1 — Measure player position coverage with the existing probe (highest value, no source change)

Why: the static audit proves the projection math and the code path, but coverage, update cadence,
death/respawn behaviour and snapshot staleness are runtime facts. The existing test-only probe already
answers all of them; no product change is needed.

Preconditions: shared host lock; rebuild the probe because entity_overlay.cpp, main.cpp and
native_renderer.cpp are newer than native/build-nmake/entity_model_probe.exe (2026-10-24 10:23).

    cd /d/TF2_Native_Test
    bash /d/TF2_Native_Worktrees/_shared/hostlock.sh acquire t65-runtime 3600
    bash build-target.sh entity_model_probe
    native/build-nmake/entity_model_probe.exe --demo "D:/TF2_Demo_Player/testdata/demos/bb841c6d379ff7c40d0c8baf99f59d8d_matcha-20260927-1347-cp_snakewater_final1.dem" --trajectory 4000 > /tmp/t65-sw-traj.json 2> /tmp/t65-sw-traj.err
    native/build-nmake/entity_model_probe.exe --demo "<same>" --rendered --trajectory-at "<240 ticks around a known death>" > /dev/null 2> /tmp/t65-sw-rendered.err
    native/build-nmake/entity_model_probe.exe --demo "<same>" --props-at "<same ticks>" --entity <index> > /tmp/t65-sw-props.txt 2>&1
    bash /d/TF2_Native_Worktrees/_shared/hostlock.sh release t65-runtime

Duration: probe build ~2-4 min, each run under a minute on a 51 MB demo; whole proposal ~10 min.
Proves: M1 (coverage counters), M2 (cadence from consecutive ticks), M3 (lifeState/health + origin
freeze), M5 (checkpoint/unavailable ratio). M4 stays blocked (needs P2).
Risk: the probe is test-only, so its output is evidence about the resolver, not about the product UI.

## P2 — Store the entity serial so handle reuse is observable

Why: demo_header.cpp:1291-1345 decodes a 10-bit serial on Enter but EntityState
(native/include/demo_header.h:110-113) keeps only classId + properties, so a minimap cannot tell a
recycled slot from the same player. Without it a dot can silently jump to another player's position.

Change (product source, needs a task and review): add serial/generation to EntityState, set it in
demo_header.cpp Enter/Delete paths, and expose it through the probe dump. Then assert: every index
class change is paired with a generation increment, and no dot follows an entity across a generation.

Cost: small code change + probe rebuild + rerun of M4. Risk: touching demo_header.h affects every
consumer of EntityState; must be a reviewed change, not a drive-by.

## P3 — Decide the KOTH map for T63/T65 (blocked on a product/ops decision)

Why: koth_bagel_rc13.bsp does not exist in this install (only rc10/rc11/rc12 in tf/download/maps and
koth_bagel_event.bsp in tf/maps). The T63 KOTH demo therefore cannot be projected, and "two target
types" cannot include KOTH until this is resolved.

Options:
1. Obtain koth_bagel_rc13.bsp (download from the server's FastDL or the map's public source) and place
   it in tf/download/maps. Then the audit reruns unchanged.
2. Substitute koth_bagel_event.bsp as the KOTH representative and accept that it is a different map
   version than the demo (the demo's map name would not match — must be reported, not hidden).
3. Use a different KOTH map that has both a BSP and a SourceTV demo (e.g. koth_product_final,
   koth_ashville_final1 appear in the T63 corpus but their BSPs are also absent from disk).

Recommendation: option 1 if a download is allowed; otherwise option 3 with a fresh demo recorded on a
map whose BSP exists. Do not draw rc13 as a blank map.

## P4 — Parse .nav to make the playable area authoritative

Why: the current playable area is a floor-raster approximation; the grid flood reaches only 33.2%
(snakewater) / 47.2% (badwater) of cells because ramps and stairs are thinner than the grid. The
authoritative playable area is the map's nav mesh. Both maps' nav files are present inside
tf2_misc_dir.vpk (maps/pl_badwater.nav 982,935 B, maps/cp_snakewater_final1.nav 1,056,016 B), headers
verified (magic 0xFEEDFACE, version 16, subVersion 2, saveBSPFileSize equals the BSP size), but the
body format was not decoded in this phase.

Work: implement a nav parser (areas: id, flags, corner count, 2D/3D corners) as a static tool under
evidence/t65-minimap/tools/, validate that area count and bounds are plausible, then recompute the
frame and the reachability table from nav polygons instead of grid flood.
Cost: ~1-2 h static, no lock needed. Risk: format drift between nav versions; guard with a version
check and a plausibility assertion (area count > 100, bounds inside BSP extent).

## P5 — Implement the minimap and run the interaction acceptance

Why: T65 defines what must be true; the feature itself is absent (grep minimap/radar/overview = 0).
This is the product task that consumes this audit: render the derived floorplan as a texture, draw
team-colored dots from extractTransform, implement hover/click/zoom/collapse per interaction_acceptance.md,
and show a reason card for unavailable maps.

Needs: a design pass on the panel (top-right, collapsed default), the texture upload path in
native_renderer.cpp, input handling, and the IA-1..IA-12 acceptance run under the shared lock with
hidden window + screenshots. Duration: implementation plus a full acceptance pass, schedule as its own
task. Risk: the 0.3 s auto-relay interaction (IA-4) touches camera state; it must be tested together
with T66, not in isolation.

## P6 (optional) — Official overview assets

If a full TF2 install is available elsewhere, the official materials/overviews/<map>.vtf +
resource/overviews/<map>.txt would replace the derived floorplan for the base layer. This install has
neither, so it is not a dependency; mention only if the derived look is judged insufficient.
