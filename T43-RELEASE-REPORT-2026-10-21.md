# T43 release acceptance — 2026-10-21

Boundary commit: 6888e6debfe712575dd4ffce4b918eb57dbef6eb
Tree object: bf43395989464dfc264314ae3557130656fb5eae
Branch: p0-entity-protocol (integration worktree D:\TF2_Native_Test)
Remote after push: origin/p0-entity-protocol = 6888e6d, ahead/behind 0/0, worktree clean.

## Verdict

**NOT PASS** under the T43 completion rule. The final chain exits 0 (VERIFY=PASS,
24/24 steps), but one focused gate is a preserved red and several labels are
NOT VERIFIED; the rule says a release report must say NOT PASS when an item
remains unverified, and T43 must not mask a red with a pin update.

Primary blocker: T26 world-geometry-hole seamCulled = 0 (one assertion group
red). It is a measured red, not a product crash: after the T29 near-plane fix
none of the surviving slivers is bracketed by a product-culled surface. T41's
derived rule says 0 of the 8 named seam faces are derived-back at the capture
pose (all 8 derived-front; the cull removes 8 other disp/side-0 faces), so the
old "at least one sliver has a culled neighbour" assertion encodes a pre-T29
artefact. The gate was left red on purpose.

## Integrations validated by T43

- T39 merged as 39c9b05 (step 22 measured-silhouette rule, 57 assertions).
- T41 merged as daf6700 (derived raster rule at the capture pose; m1/m2/m3 caught).
- T42 merged as e46967f (lifecycle 15/213, readiness 13/107, mutations 3/3).
- T40 merged as b8302a3 and 75292a0 (lighting 96/0, material 183/0, mutations 4/4 and 5/5).

Freeze commit 6888e6d contains only the merge log/boundary record on top of
75292a0. No source or gate was edited during T43.

## Focused gates (run once each on 6888e6d, after the T43 clean build)

| gate | verdict | evidence |
|---|---|---|
| step 22 weapon animation | PASS 57 OK / 0 FAIL | .scratch/t43/step22.log |
| ViewModel 23 | PASS 154 / 0 | .scratch/t43/step23.log |
| ViewModel 24 companion | PASS 114 / 0 | .scratch/t43/step24.log |
| T25 visual baseline | PASS 70 / 0 | .scratch/t43/t25.log |
| T26 world holes | **FAIL, 1 assertion group** (seamCulled=0; 32/32 directions resolve; 81,141 px cull-off-only) | .scratch/t43/t26.log |
| T27 terrain blend | PASS 94 / 0 | .scratch/t43/t27.log |
| T36 lighting dark account | PASS 35 / 0 + NOT VERIFIED side split | .scratch/t43/t36.log |
| T37 install lifecycle | PASS 15 cases / 213 checks | .scratch/t43/t37-lifecycle.log |
| T37 install readiness | PASS 13 cases / 107 checks on retry (see caveat) | .scratch/t43/t37-readiness-retry.log |
| T38 quality audit | PASS 28 / 0 | .scratch/t43/t38.log |
| audio content | PASS 39 / 0, mutations 25/25 | .scratch/t43/audio-content.log |
| audio subsystem | PASS 240 / 0, mutation suite PASS | .scratch/t43/audio-subsystem.log |

### T37 readiness caveat

The first forward attempt (14:01) failed 4 assertions: first-launch/restart/
explicit-root title readings had an empty connected root and no "[VPK entries:"
bracket. The trace of the same run already showed
`tf_root=D:\SteamLibrary\steamapps\common\Team Fortress 2\tf valid=1`, so the
product connected; the title capture did not get the connected title inside its
30 s deadline on the cold file cache. A second forward run, with the cache warm,
passed 13/107 unchanged. This is a gate timing sensitivity, not a product
regression; it must be fixed (warm-up or assert the trace root in addition to
the title) before the gate can be relied on cold. Owner: T42/T37 gate.

## Final chain (the single declared run)

Command: `bash verify-all.sh --quick` on 6888e6d, inside host lock owner t43.
Result: **VERIFY=PASS**, exit 0, 24/24 steps, BUILD=PASS, no FAIL line.
Duration 14:11:07 -> 15:42:13 (+08:00). Log:
`.scratch/t43/verify-all-quick.log`, 47,976 bytes, sha256
2aae737f329597cb942c65aab3fc033ede8efd051aa27ce1bd28a8a36f4c2aae.

Install lifecycle and resource-root behaviour ran inside the focused T37 gates
and passed as above. The earlier chain attempt at 14:08 aborted before step 1
with VERIFY-LOCK=FAIL: an empty stale `.scratch/verify-all.lock` directory from
2026-10-21 00:32 (no verify process) was removed deliberately, and the chain was
then run once; no verification step had executed in the aborted attempt.

## Preserved red / NOT VERIFIED (owners and cheapest next verification)

1. T26 seamCulled (primary). Owner: T41/mapping; gate owner T26/T43.
   Cheapest: replace the pre-T29 "cull-neighbour" assertion with
   "0 of the 8 named seam faces are derived-back at this pose" plus a positive
   control that the cull removes >=1 non-seam face; one T26 focused run.
2. T40 tick-2000 specular 0/0 (occluded). Owner: T40. Cheapest: one wide-area
   pose from T35's nine, capture at tick 2000, repeat the pair.
3. T40 detail residual 2,812 px on 27 non-displaced WOOD/WALL007A faces.
   Owner: T40/T29. Cheapest: T29 near-plane mutation, capture F at 2000, diff
   against the pre-fix F.
4. T40 tick-2000 bump direction one-sided (79/0) by occlusion. Owner: T40.
   Cheapest: same wide-area pose as (2).
5. T41 pixel-level authored-side split and per-face removed-pixel reading.
   Owner: T41. Cheapest: one product run dumping face 17605's displaced grid
   triangle normals; the 51,620 disp/side-0 px is bounded, not explained.
6. T39 per-sample rasterizer instrumentation. Owner: T39. Cheapest: instrument
   the skinning/passthrough sample, not the silhouette mask.
7. T37 readiness cold-cache flake (above). Owner: T42/T37 gate. Cheapest:
   pre-warm one product run or assert `tf_root` from the trace alongside the
   title; then two cold readiness runs.

## Push / remote / cleanliness

- `git push origin p0-entity-protocol` rc=0, f7b305c..6888e6d.
- `git ls-remote origin refs/heads/p0-entity-protocol` =
  6888e6debfe712575dd4ffce4b918eb57dbef6eb.
- ahead/behind = 0/0; worktree clean; no verify lock; zero product/probe
  processes after the run.
- Artifact hashes from the T43 clean build (not reproducible across rebuilds,
  as already recorded by T39):
  tf2_demo_native.exe 9d038485513e8b5f0b9db370708ddb140afbc0c737bf2eb6964d700e93e04d1d;
  weapon_animation_probe 38fe32a2bba818c3e7a3fc4250cbaba90cce13274af04ea865278ec87550d5e2;
  viewmodel_animation_probe 19f378bc0730d491bae201152f77af29a0e778624b9891949099a7b405f6f68d;
  world_geometry_probe eb2f89d13e39d67e00e411cfbbee66f6220729e2da29c8d9547501da9f003821;
  terrain_blend_probe 4311ce0162e0541c07da383bd20c54c4a5c0af2c5dffe55b8d9d7a4d0fe8aabc;
  install_cli_probe ad1668a528a8b1febd9b5c82aa8c6a4fb7f680d682c336151e15626949f1b4f3;
  vpk_query ba2bbfe6abd12e8151ac85c8102540bf62cb27db4d11ca193e24bb6a83bad51c.

## What T43 did not do

No gate/pin/source/evidence edit; no mutation beyond the gates' own restore
paths; no second chain; no merge after freeze. Generated chain evidence was
restored to the frozen tree after recording its status/manifest in
`.scratch/t43/`.
