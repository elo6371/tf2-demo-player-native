# T40's 2,812-pixel detail residual, attributed (T45)

T40's material gate went red on one sentence it could not answer: after the T29
near-clip fix, 2,812 pixels where the detail layer's *effect* toggled sit on 27
faces that the T26 face-id mask says did not move, on a frame whose baseline
(A, all switches off) is byte-identical. This directory is the attribution, and
everything here is re-derivable from frames that already exist plus the T29
gate's own source mutation.

## What is measured

```
classification.txt   the population and the per-face split (classify.py)
relation.txt         what the toggle is, pixel by pixel (relation.py)
geometry.txt         where it is: components, adjacency, the largest face (geometry.py)
inputs.sha256        sha256 of every frame read, with its provenance
```

The population, unchanged from T40 and re-derived here:

| reading | value |
| --- | --- |
| frame | 1264x681, 860,784 px |
| same-face-id pixels (mask, id != 0) | 309,116 (35.91%) |
| baseline A differs on same-face px | 649 (0.210%), max 4 |
| detail F differs on same-face px | 3,458 (1.119%), max 42, 44 faces |
| toggle px (residual) | 2,812 (0.910%), 27 faces |
| toggle split | 1,320 pre-fix-only / 1,492 current-only |
| depth readback changed under toggle | 1,736 / 2,812 |
| albedo-only frame changed under toggle | 97 / 2,812 |
| flat frame changed under toggle | 0 / 2,812 |

The 27 faces are WOOD/WALL007A, non-displaced. The largest is face 7536 (1,315
toggle px; the face has 2,908 same-id px, of which the detail layer contributed
1,321 in the pre-fix build and 1,479 in the current build, 736 in both and 844
in neither: the contribution is patchy inside the face and the patch changed).

## What the toggle is

At every one of the 2,812 residual pixels exactly one of the two builds' detail
contribution is **exactly zero**: F == A byte-for-byte, not a smaller value.
The other build's is a darkening (F < A, per-channel magnitudes 3..17).

```
old-only px: pre pair delta (F-A) e.g. (-14,-12,-7); new pair delta == (0,0,0) for all 1,320
new-only px: new pair delta (F-A) e.g. ( -9, -8,-5); pre pair delta == (0,0,0) for all 1,492
```

So this is not a texture or data difference (the detail resource, the atlas and
the shader leg are the same bytes on both builds) and not a different texel
value: it is the detail multiply contributing on one build and contributing
exactly nothing on the other, pixel by pixel, on the same surface with the same
baseline. On pixels where both builds apply the detail layer (10,986 common
detail px) the two frames are byte-identical on 70.2%.

## Where it is

172 connected components, largest 1,042 px (bbox 61x112), and each component
mixes both directions (e.g. the 1,042-px component is 587 current-only / 455
pre-fix-only). Only 14.3% of residual pixels touch a pixel whose face id
changed, so this is not a silhouette sliver or an edge effect. It is patchy
detail-sample structure inside large, stationary walls.

## Why the near clip is the cause

Source identity, as attribution evidence: the pre-fix run's saved sources differ
from this tree's by exactly the 24-line T29 near-clip hunk, and
`world_tile_atlas.cpp` is byte-identical (sha256 recorded in
`../forward-readings.txt` and in the T45 report). That narrows the input to one
line of code but does not prove the line causes these pixels: the near plane
also changes the depth encoding, so the proof has to be a frame-level mutation.

The mutation is in `mutation.md`: reverting that one line reproduces the kept
pre-fix A frame `f87f396a...` and F frame `21cb8aa2...` **byte-identically**
(0 of 2,582,352 bytes differ, max_delta 0), and restoring it reproduces the
current A `6bb4d9a7...` / F `2ffa7d2b...` byte-identically. The fix's near clip
is therefore the cause of the residual, measured at frame level, not inferred
from the source diff.

## What is NOT isolated

The pixel-level path inside the rasterizer that turns a near-plane change into a
different detail sample on an un-clipped face is NOT isolated. What is measured
is (a) the frames, (b) that the only changed input is the near clip, (c) that
putting the old near clip back reproduces the old frames byte-identically and
restoring reproduces the current frames byte-identically. A depth-test tie
explanation is ruled out by the mask: a different winner would change the face
id at those pixels, and the mask says the winning face did not change. No shader
or data defect is claimed.
