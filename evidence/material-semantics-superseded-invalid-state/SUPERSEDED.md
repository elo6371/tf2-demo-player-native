# Superseded evidence: the 38,538 px layer-2 reading

Archived 2026-10-20 from `evidence/material-semantics/` before the corrected run
overwrote it. Do not cite these files as a PASS.

## What was measured

`R1 -> R2` read 38,538 differing pixels, max delta 53, all darker. The ledger read
`bump2 tiles=5 materials=5 vertices=41118`, so it looked like the `--world-bump2-texture`
layer-2 lerp had been measured.

## Why it is superseded

The 4 `$ssbump` materials had their `featureMask` bit 2 set while bit 1 (their own
`$bumpmap` tile) was clear. That is a state the shader cannot consume: layer 2 has no
layer-1 base normal to lerp into. Measured controls:

* delete the whole layer-2 arm, rebuild, verify the rebuilt exe no longer contains the
  `bumpAtlas2.Sample` source string: the frame still differed from R1 by 38,538 px;
* set no mask bit 2 (keep the atlas and the t9 binding): the frame was byte-identical to
  R1 (0 px).

So the 38,538 px was the invalid mask state perturbing the layer-1 path (the D3D
compiler hoisted a write out of the layer-1 branch), not the layer-2 lerp.

## Corrected reading

The product path now sets bit 2 only when bit 1 is set; the diagnostic eligibility rides
`featureDiag` (`feature.w`), and the shader lerps onto the base normal the frame
already computed. The current evidence in `evidence/material-semantics/` pins:

* `R1 -> R2` = 15,953 px (8,790 brighter / 7,163 darker), max delta 26;
* delete the layer-2 arm -> 0 px;
* `m2` (layer 2 samples layer 1's atlas) -> 25,591 px.

The 38,538 figure is preserved only as the reading that exposed the invalid state.
