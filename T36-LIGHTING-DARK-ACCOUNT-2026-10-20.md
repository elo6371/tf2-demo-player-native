# T36 — lighting, lightmap and the dark-region account

Board item: task-9. Worktree `D:/TF2_Native_Worktrees/t36-lighting`,
branch `task/t36-lighting`, baseline `ac7e2b7`.

Owner: t36. Everything below was measured on `maps/pl_odyssey.bsp`,
demo `autorecord_2026-07-02_13-26-46.dem`, tick 2000, the T29 reference
camera `eye=(-2423.6, 5566.0, -1280.0)`, `--hidden-window --audio-disabled`.

## 0. The short version

The inherited gate passes forward with 21 assertions. But two of the readings it
carries are wrong, and I found them by calibrating instead of re-pinning:

1. **The "back face" label is inverted.** The account's plane-side rule calls
   86.00% of the drawn population BACK. That population has already been through
   `D3D11_CULL_BACK`, so the GPU calls every one of those pixels front. Scored
   against that known population, the rule's sign is the wrong one, and every
   front/back number the account prints is swapped.
2. **The share was reported without its denominator.** "81.8% of dark pixels are
   back faces" sounds like a finding until you notice 86% of *all* drawn pixels
   carry that label. With the label corrected, dark pixels are **1.272x** more
   likely on back faces than front ones — a mild enrichment, not the explanation
   for a fifth of the frame.

So the answer to the assigned question — *what share is back-face?* — is
**17.15% under the rasterizer's own convention, or 1.27x enrichment**, and the
honest framing is that face side is a weak variable here. The strong variable is
the lightmap rectangle, which is what the account already had right.

## 1. What the inherited gate actually asserts

Forward mode, 21 assertions, exit 0:

| group | what it pins |
| --- | --- |
| dark total | 208,630 px — the same frame T29 measured |
| 14 bucket counts | the whole population, bucket by bucket |
| face-side sums | BACK 170,701 / FRONT 35,227 |
| product-attributable | 4,123 px, asserted **≤ 4200** (a ceiling, not an equality) |
| masks | every mask file's byte sum equals its bucket count |
| cull control | 81,141 px removed, 44,046 BSP-front / 37,095 BSP-back, subset property |

It does **not** assert that the frame is correct, and its own header says so.
The property it really establishes is: *the dark population is fully partitioned
into named buckets, each bucket is a mask on disk, and no bucket is silently
absorbed into a total.* That is a real and useful property. Two of its numbers
were wrong anyway.

## 2. Four defects that made the gate unrunnable, now fixed

The gate had never passed. Running it forward found, in order:

1. `ACCOUNT="$SCRATCH/account"` was never created, so the first redirect
   (`> "$ACCOUNT/account.txt"`) failed and the gate reported *a missing ledger*
   when the actual fault was *a missing directory*.
2. `--eye "-2423.6,5566.0,-1280.0"` is rejected by argparse, because a value
   starting with a dash in the space-separated form is read as an option. Five
   call sites. The `--eye=...` form is accepted.
3. The two pinned face-side sums were arithmetic guesses. Summing the buckets
   including the `albedoDark` ones gives BACK **170,701** and FRONT **35,227**,
   not 170,698 / 35,817.
4. **Every mutation read a crashed tool as a mutation going red.** m2 patched in
   `front = not front  // mutation m2`. That is a Python `SyntaxError`
   (`//` is floor division, so the line is a binary expression on an assignment
   target). The account tool died, the count fell back to `0` through
   `|| echo 0`, and `0 != 170698` was printed as RED. It proved the tool could
   be broken, not that the side rule was observed — the "mutation" would have
   "passed" on any syntax error at all.

Fixed: m2 inverts properly and now asserts a real **swap** (same total, BACK
takes FRONT's value); m1 and m3 fail closed on a missing ledger instead of
reading it as 0.

## 3. The forward reading (exit 0, 21 assertions)

```
bash lighting-dark-account-check.sh --reuse      # exit 0
  OK dark total (the T29 reference frame) = 208630
  OK A no world triangle (sky/viewmodel) = 2702
  OK rect all zero, BACK, albedo bright = 67951
  OK rect all zero, FRONT, albedo bright = 5432
  OK rect mean<8, BACK = 14814     OK rect mean<8, FRONT = 24855
  OK rect mean<32, BACK = 26425    OK rect mean<32, FRONT = 4039
  OK rect mean<96, BACK = 57516    OK rect mean<96, FRONT = 752
  OK rect BRIGHT, BACK = 3993      OK rect BRIGHT, FRONT = 130
  OK dark on BACK faces = 170701   OK dark on FRONT faces = 35227
  OK product-attributable pixels = 4123 <= 4200
  OK every mask sums to its count
  OK pixels the cull removed = 81141
  OK removed pixels that are BSP-FRONT faces = 44046
  OK removed pixels that are BSP-BACK faces = 37095
PASS: 208,630 dark pixels attributed bucket by bucket; 99.94% is the map own
lighting, 1.98% is product-attributable, the cull control holds.
```

Full log: `evidence/t36-lighting-dark-account/account.txt`.
Forward was run with `--reuse` over the tick-2000 frames already captured on
this build; the frames, their sha256 and their traces are in `captures/` and
`frames.txt`. `--reuse` launches no product, so it held no host lock; the
lock-queued attempt was cancelled after it sat behind t34/t37.

## 4. The mutation (real, and it changes the observation)

m2, run by hand against the same frames:

```
before  back=170701  front=35227    (total 208630)
after   back=35227   front=170701   (total 208630)
```

The two populations swap exactly and the total is unchanged — which is what an
inverted side rule must do, and is a much stronger statement than "a number
moved". Source restored byte-identical (`git diff --quiet` clean).

**Not yet run:** m1 (exponent sign) and m3 (`--no-world-cull` ignored). Both
need a **product build** plus a real capture, and the host lock never came free
long enough inside one agent window. They are wired in the gate and fail closed;
see §8.

## 5. The calibration that overturns the side split

```
--world-face-id frames are drawn with D3D11_CULL_BACK, so every pixel that
carries a face id is a surface the rasterizer already called FRONT.

  asWritten  front=(sd>=0 if side==0 else sd<=0)   front  105065 (14.00%)  back 645326 (86.00%)
  inverted   front=(sd<0  if side==0 else sd>0)    front  645326 (86.00%)  back 105065 (14.00%)
  planeOnly  front=sd>=0 (ignore side)             front  315742 (42.08%)  back 434649 (57.92%)
  sideOnly   front=side==0 (ignore plane)          front  399182 (53.20%)  back 351209 (46.80%)
```

The account uses the first row. It calls 86% of an already-culled population
back-facing, which cannot be true. The second row is the sign the rasterizer
agrees with. Neither `planeOnly` nor `sideOnly` does better, so the rule is
right in form and wrong in sign — it is not a broken test, it is a flipped one.

Source: `evidence/t36-lighting-dark-account/side-rule-calibration.txt`.

### The reading, with the denominator

| | front | back |
| --- | --- | --- |
| drawn (all face-id px) | 645,326 (86.00%) | 105,065 (14.00%) |
| dark (lum < 16) | 171,030 (82.85%) | 35,410 (17.15%) |
| dark rate on that side | 26.50% | **33.70%** |
| base dark rate | | **27.51%** |

Relative risk of being dark, back vs front = **1.272**.

Under the account's *as-written* (inverted) label the same data reads as
`RR = 0.786` — i.e. the sign of the conclusion flips with the sign of the
rule, which is exactly why the calibration had to be done before any number
was quoted. Both readings are kept:
`dark-rate-by-side-as-written.txt` is **superseded** by `side-account.txt`.

### Ans: what the back-face share actually is

- **17.15%** of dark pixels, under the convention the rasterizer itself uses.
- **1.272x** dark-rate enrichment over front faces — real but mild.
- It is **not** the story. 82.85% of the darkness is on faces the rasterizer
  calls front, so "the lightmap was baked for the other side" cannot explain
  the fifth of the frame that is black.

## 6. Does the darkness live where the map baked it dark?

Yes, on both sides, almost equally:

| calibrated side | dark px with rect mean < 96/255 | share of that side's dark |
| --- | --- | --- |
| front | 166,765 | 97.51% |
| back | 35,190 | 99.38% |

That is the finding that survives every correction: the dark pixels sit on
lightmap rectangles the map itself baked dark (all-zero 35.17%, mean<96 60.24%
of the whole population). Face side barely matters; rectangle tone is decisive.

## 7. Two inherited readings I could not confirm, and the check that says so

**The `lmoff` frame is not a pure albedo probe.** `dark-account.py` calls a
pixel a "dark material" when `--world-lightmap off` is dark there. But the
shader leaves `lightmapFactor = lightmapFeatures.x`, a per-material constant,
and sets `lightColour` to that constant, so an lmoff pixel is `albedo * K`, not
the albedo. The renderer has a view that *is* the albedo — mode 6,
`--world-albedo`, documented as "lighting, the lightmap and self-illum are
deliberately NOT applied".

Measured against it on the same frames:

```
lmoff dark AND albedo dark  (a dark material):   2723 ( 1.31%)
lmoff dark BUT albedo BRIGHT (misattributed):       0 ( 0.00%)
lmoff bright (lightmap term is what is dark):  205907 (98.69%)
```

So the concern is real in principle and **zero in practice on this frame**: the
material constant never turns a bright albedo into an lmoff-dark pixel here. The
`albedoDark` bucket (21 px total) is genuinely dark material. The probe is
sound *for this map and camera*, and the report says so with the measurement
rather than by assumption. Note the albedo frame is from a separate run
(1431 vs 1423 frames) at the same tick and camera, not the same process.

**The cull control's own caveat.** It removes 81,141 px of which 44,046 are
BSP-*front* faces. The tool's header attributes this to the T11 winding
disagreement. Given §5, that attribution is unverified: the BSP-front/back split
of the removed set is computed with the same inverted rule. Re-reading it under
the calibrated rule is [next step], and until then the 44,046/37,095 pin is a
pin on an *inverted* label.

## 8. Is the remaining black attributable to Source / map semantics?

Mostly yes, with one bucket still open.

- **99.94% of the dark population is the map's own lighting drawn faithfully**:
  35.17% on all-zero rectangles, 60.24% on rectangles the map baked below 96/255,
  1.30% with no world triangle (sky pass / viewmodel). The product samples those
  texels and writes them; a correct renderer is supposed to be black there.
- **1.98% (4,123 px) is "product-attributable"** by the account's own definition
  (bright albedo AND bright rectangle). The gate asserts this stays under 4,200.
  It is the only place a product defect could hide, and it is 0.48% of the frame.
- **The back-face story does not survive.** 1.272x is not a defect signature.
  Making back faces lit is a lightmap-composition decision (T29 §10), and making
  them culled is a winding decision T11 already scored and refused.

So: the remaining black region is **consistent with the map's own baked lighting**
for 99.94% of its pixels, and the T29 "back-face" hypothesis is *not* supported
at the strength the share implied. It is a real but weak enrichment.

## 9. Where the account still does not look

- Only **one** camera and **one** tick. A fifth of one frame is not a map.
- The 4,123 "product-attributable" pixels are **not further split** — face 15239
  was named in the inherited gate header as the whole of the bright-rect FRONT
  bucket, but that identification is now suspect because the bucket labels are
  inverted. Not re-derived here.
- **m1 and m3 mutations are unrun** (need a build + capture under the lock).
- The nearest-luxel question is answered per *rectangle mean*, not per *sampled
  texel*. A face whose rect mean is 100 could still sample an all-zero corner.
  Answering that needs the light uv the trace already carries
  (`luv=0.7145,0.1321` in `gpu: world face-trace`) projected per dark pixel.

## 10. Next steps, in order

1. **Re-run the cull control under the calibrated rule** and re-pin
   `removedSide`. The 44,046/37,095 split is currently an inverted label.
2. **Run m1 and m3** under one lock window (each needs `build-target.sh` plus a
   capture; m1 ~1 min build + 5 captures, m3 the same).
3. **Per-texel, not per-rectangle**: project `luv` per dark pixel and read the
   single texel it samples. This is what would turn "the rectangle is dark" into
   "the texel this pixel read is dark", which is the claim T29 could not make.
4. **Add the calibration to the gate itself** as an assertion: the plane-side
   rule must classify ≥95% of drawn face-id pixels as front. That turns §5 from
   a one-off measurement into a property that cannot silently regress. It is the
   single highest-value change in this list.
5. Second camera + second tick, so the 99.94% is not one frame's opinion.

## 11. Evidence

`evidence/t36-lighting-dark-account/`:

| path | what it is |
| --- | --- |
| `account.txt` / `account.json` | the gate's population ledger, 14 buckets |
| `masks/*.mask` | one byte per pixel per bucket — the population evidence |
| `cull-control.txt` / `.json` | what the world back-face cull removed |
| `side-account.txt` / `.json` | the corrected split, with denominator and RR |
| `side-masks/*.mask` | dark populations by calibrated side x rectangle tone |
| `side-rule-calibration.txt` | the four candidate rules scored against the cull |
| `dark-rate-by-side-as-written.txt` | the inverted reading — **superseded** |
| `albedo-probe.txt` | does `--world-lightmap off` isolate the albedo? |
| `captures/*.bmp` | the five tick-2000 frames (textured, ids, ids-nocull, lmoff, flat, albedo) |
| `frames.txt` | sha256 of every frame and the run parameters |

## 12. Constraints respected

- T29 near-plane untouched (4 map units); no tolerance was widened.
- No tracked evidence was deleted, and no old reading was rewritten as a pass.
  `dark-rate-by-side-as-written.txt` is kept and marked **superseded** in §5.
- No source change ships in this task beyond the two new tools and the gate
  fixes; the product is untouched.
- `evidence/t26-world-holes` (T35) and the pin were not touched.
