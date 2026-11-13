# Map Materials and Objects Progress - 2026-11-12

## Scope

Continue the first-release map work on `candidate/e5`, starting at `917332d`.
The first-release maps are `cp_process_f12`, `cp_sunshine`, `cp_gullywash_f9`,
`cp_sultry_b8a`, `koth_product_final`, `koth_bagel_rc11`, and `pl_upward_f12`.
Current scope is map geometry/materials, sky, control points, and the cart.
Shadows, lighting
matching, water, glass reflections, and particles are excluded. The later
particle scope is Pyro weapons, Medic beams, and bleeding.

## Work completed so far

- BSP static props: sprp v10, compressed game lump, model dictionary, transform,
  skin, and leaf indices are decoded. Map model mesh/instance budgets are
  independent from the player/weapon 96-mesh budget. Static props use the map's
  asset chain and camera PVS.
- Live map models: model path comes from the current network snapshot; position,
  rotation, skin, no-draw, and parent transforms are read from current entity
  state. Parent cycles, missing parents, and unsupported attachment parents are
  refused explicitly. Source Z-up coordinates are preserved for map models.
- Model material alpha: alpha-test, translucent and no-cull VMT flags reach map
  model draw ranges. Opaque and blended ranges use separate passes.
- Legacy VTF: the old VTF layout's low-resolution thumbnail is skipped before
  reading the top mip. A fixture covers thumbnail offset, known red pixels,
  truncation refusal, and the no-thumbnail control.
- Control-point materials: model paint now resolves `$texture2` for
  `UnLitTwoTexture`; that texture is uploaded and sampled in the model's second
  layer. `UnLitTwoTexture` bypasses directional shading and `$additive` uses
  Source's `SRC_ALPHA/ONE` blend. `Modulate`/`$mod2x` are parsed and mapped to
  their framebuffer blend modes. `world_feature_probe` has three parser
  controls for UnLitTwoTexture, Modulate 2x, and an ordinary world material.
- Control-point bodygroups: static map model material runs retain the MDL
  bodypart base/count/model selector, and the live `m_nBody` value selects only
  the matching mutually exclusive model. `entity_model_probe` covers a
  four-model bodypart selector.

## Evidence collected in this continuation

- Targeted build: `build-target.sh tf2_demo_native world_feature_probe` passed;
  log `.scratch/map-control-final-build.log`.
- Parser probe: `world_feature_probe --self-test` passed, including the three
  new material controls; log output captured in the command session.
- Sultry control-point capture: `.scratch/map-control-final/frame.bmp` and
  `frame.trace`, using demo
  `E:/demo测试/cp_sultry_b8a/STV/f63a7a2040379d7d6a8414f76bbb81dc_matcha-20260703-1442-cp_sultry_b8a.dem`
  at tick 0 and debug eye `-382,-366,932.25`, pitch `27.71`, yaw `45`.
  Trace confirms BSP lookup `reason=found`, camera `src=debug`, map actors
  `observed=22 submitted=22 missingMesh=0 poseRefused=0`, and map-model budget
  `resident=230 submitted=487 accepted=487 rejected=0`.
- Latest shader capture after the additive-alpha correction:
  `.scratch/map-control-final3/frame.bmp` and `.scratch/map-control-final3/frame.trace`;
  capture rc=0, elapsed 43 seconds, same camera/tick, same 22/22 network model
  submission and 487/487 map-model budget result.
- Against the earlier same-pose capture `.scratch/map-control-smoke2/frame.bmp`,
  the final capture changed 3,440 of 860,784 pixels; max channel delta 255.
- Control-point time proxies are now parsed and applied to map-model draw ranges.
  Sultry's `cappoint_hologram.mdl` trace resolves `$texture2transform` scrolling
  at rate 0.1/angle 90 and `$color` Sine ranges for neutral, red, and blue slots.
  With the same fixed camera, tick 0 and tick 1000 captures on exe
  `d40363b7...` changed 524 channel bytes (0.0203% of the frame, max delta 140);
  all 524 changed bytes fell in the 130x130 control-point region. See
  `.scratch/map-control-proxy-t0/` and `.scratch/map-control-proxy-t1000-live/`.
- A fixed-camera Upward cart pair uses the supplied STV demo. Upward is in the
  first-release set. At tick 5611, entity 320 is `bomb_cart.mdl`, skin 0,
  world-parented at `(-1520.12,-1536.00,30.94)`; at tick 6160 it is at
  `(-1131.41,-1668.72,38.78)`. Both traces confirm the requested capture tick
  and debug camera. The full frame changes 20,562 channel bytes (0.7963%);
  12,078 bytes change in the 500x360 cart/track region (2.2367%, 58.7391% of
  all differences). The images and trace hashes are under
  `.scratch/map-upward-cart-t5611/` and `.scratch/map-upward-cart-t6160/`.
- Six-map tick-0 smoke was rerun on the same `d40363b7...` executable:
  `.scratch/map-six-current/summary.json` reports six of six BSP lookups found,
  six of six camera assertions matched, rc=0 for every capture, and sky faces
  active on all six maps. Unresolved entity paint slots remain: Sultry 13,
  Bagel RC11 4, Product Final 3; Process, Sunshine, and Gullywash report zero.
  These are actual candidate-resolution misses, not capture failures. The trace
  identifies Sultry's Watergate brick family-2 slot 0 and several Sultry
  building/control models, Bagel foliage family-1 slot 0, and Product stairs /
  rock slots; exact rows are in each map's `frame.trace`.
- Follow-up source inspection confirms the map-model paint callback already uses
  `AssetStoreChain`, and its BSP pakfile reader is registered before model
  inspection. The unresolved rows therefore do not establish a broken pakfile
  connection. A read-only mode was added to `entity_model_probe` to inspect the
  same model resources and resolver against a BSP pakfile, VPKs, and loose files.
  The target builds, and the probe resolved representative custom-model VMTs
  from the BSP pakfile plus VTFs from `tf2_textures_dir.vpk`.
- The probe checked all 19 named unresolved model paths across Sultry, Bagel,
  and Product. Sultry's repeated Watergate pillar failures are skin family 2
  requesting `brick_pillar_003`; that VMT is absent from both the BSP pakfile
  and installed VPKs. Other Sultry misses are `wood_debris1.vmt`, plus three
  missing VTFs: `dev/dev_measurewall01d.vtf`, `concrete/concretewall002.vtf`,
  and `watergate/wall028a.vtf`. The VMT wrappers for the latter building slots
  are present in the BSP and parse successfully. Bagel's three oak models all
  request missing `tf_oak_tree01a.vmt` for family 1; the base, `b`, and `c`
  variants resolve. Its zombie model requests
  `models/player/items/all_class/demoman_blue_zombie.vmt` for family 1 while
  the other two slots resolve. Product's two cliff models request missing
  `cliff_wall_101a.vmt` for family 1 while base and snow variants resolve.
  Product stairs' second slot is `TOOLSNODRAW`; the requested model-relative
  VMT is absent, while the standard `materials/tools/toolsnodraw.vmt` and its
  VTF exist. That slot needs an explicit Source-semantics decision before
  changing how it is drawn.
- These rows include families that the current scene may not select: the product
  prepares all skin families up front. The six-map smoke does not prove that
  every missing family appears in the captured frame. No substitute texture or
  fallback was invented, and no product material behavior was changed based on
  these diagnostics.
- Upward's map BSP is selected from `download/maps` (`reason=found`); the demo
  and BSP SHA-256 receipts are in `.scratch/map-upward-cart-t0/`.
- Sultry fence VTF comparison: product-decoded RGBA versus the independent
  Pillow DXT decode had zero alpha differences and maximum RGB delta 1. This
  rules out that texture's VTF payload offset/alpha decoder as the source of its
  visible dark regions. Files: `.scratch/gothic_fence.rgba`,
  `.scratch/map-fence-source.png`, `.scratch/map-fence-product.png`.
- `git diff --check` passed. The executable used by the final capture is
  SHA-256 `26502fe7546e45e552d57cddcc2d473eabcaa3e33de6c48497e6740f0dc6e705`.
  Host lock is FREE and there are no product processes.

## What this does not prove

- The current control-point frame is improved but is not yet visually equivalent
  to TF2. Tick-dependent proxy pixels are now demonstrated, but the hologram
  still has artifacts and no same-camera TF2 comparison exists. `$alpha` Sine
  is not implemented; only the measured `$color` Sine and TextureScroll subset
  is supported so far.
- The bodygroup correction is verified by a same-pose image difference of 2,470
  pixels versus the pre-selector frame; it does not prove every map model's
  bodygroup metadata or every `m_nBody` network value is decoded correctly.
- The six-map run is tick-0 smoke only; it does not establish full-map coverage,
  correct materials for every model, or game-accurate appearance. The three maps
  with unresolved slots still have named material gaps.
- The Upward cart pair proves a live model path, world parent, skin 0, two
  changed origins, and localized image change on this supplied demo. It does not
  prove all cart paths/ticks or equivalence to a TF2 reference.
- The world-albedo capture still contains unexplained black areas. The fence
  VTF check excludes one decoder cause only; it does not identify the black-area
  cause or prove all map materials correct.
- Six-sided skybox faces load, but the 3D sky-camera scene is not implemented.
- No TF2 reference frame was captured at the same camera, tick, and resolution;
  no claim of game-accurate map rendering is made.
- No full verification chain, push, tag, or release was performed.

## Next

1. Obtain or identify the missing authored material assets for the Sultry,
   Bagel, and Product custom models. For `TOOLSNODRAW`, verify the intended
   runtime draw behavior before deciding whether to map the model slot to the
   standard tool material. Then add focused resolver/draw tests before changing
   fallback behavior.
2. Identify Sultry's world black regions with face/material/atlas sampling;
   do not substitute guessed textures.
3. Compare the hologram and cart against TF2 captures made at matching camera,
   tick, and resolution; current captures only prove local behavior.
4. Implement `$alpha` Sine only if the supported material inventory requires it;
   retain the current unsupported boundary otherwise.
5. Implement the 3D sky-camera scene; six cubemap faces alone do not reproduce
   Source's 3D skybox.
6. Rebuild after the remaining map-material fixes and repeat all seven targeted
   captures on the resulting executable. This remains separate from the full
   verification chain.
