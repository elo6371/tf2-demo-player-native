# T65 — projection ledger (world → minimap pixel)

Generated from data/projection_ledger.json by tools/write_ledger_md.py.
Do not hand-edit numbers; regenerate instead. Canvas is 1024x1024, equal aspect, 2% padding.

## 1. Formula

    px = (x - minX) * scale
    py = (maxY - y) * scale          # y is flipped: +Y world points up on screen
    x  = minX + px / scale           # inverse
    y  = maxY - py / scale

The frame is derived, not hard-coded: frame = intersect(gameplay-entity bbox + 1536 units margin, BSP floor extent).
Raw worldModel mins/maxs are NOT usable (they include the 3D skybox; see REPORT.md).

## 2. Constants

| map | minX | maxX | minY | maxY | scale (px/unit) | units/px | frame x | frame y | cells outside frame | cells outside canvas |
|---|---|---|---|---|---|---|---|---|---|---|
| cp_snakewater_final1 | -6011.520 | 7067.520 | -6331.520 | 6747.520 | 0.0782932081 | 12.7725 | [-5760, 6816] | [-4188, 4604] | 2348 | 0 |
| pl_badwater | -3094.400 | 4726.400 | -3789.400 | 4031.400 | 0.1309328969 | 7.6375 | [-2944, 4576] | [-3424, 3666] | 6629 | 4723 |

## 3. cp_snakewater_final1 — control points

BSP: tf/maps/cp_snakewater_final1.bsp  bytes=59590420  sha256=f09d406d2bbe397c8f9df9ca17f11b36b53d40fcf57e3f1532ec9af811b2a1eb

| name | world x | world y | world z | px | py |
|---|---|---|---|---|---|
| cp_3 | 527.999 | 207.999 | 67.775 | 512.00 | 512.00 |
| cp_red2 | 2568.000 | -1896.000 | -108.000 | 671.72 | 676.73 |
| cp_red1 | 5060.000 | -1632.000 | -148.000 | 866.83 | 656.06 |
| cp_blu2 | -1512.000 | 2312.000 | -107.750 | 352.28 | 347.27 |
| cp_blu1 | -4015.520 | 2046.950 | -144.475 | 156.27 | 368.02 |

## 4. cp_snakewater_final1 — team spawn centroids (in-band only)

| team | n | centroid world | px | bbox world (xmin,xmax,ymin,ymax) |
|---|---|---|---|---|
| 2 | 48 | (3429.33, -1295.67, -77.33) | (739.15, 629.73) | (1816, 6248, -2652, 376) |
| 3 | 48 | (-2373.33, 1709.00, -81.33) | (284.85, 394.48) | (-5192, -760, 32, 3068) |

## 5. cp_snakewater_final1 — error budget

| item | value |
|---|---|
| round-trip max abs error (control points, float64) | 6.821e-13 world units |
| half-pixel quantization at 1024 canvas | 6.3863 world units |
| pixel rounding | +/- 0.5 px |

## 6. cp_snakewater_final1 — geometry summary

| item | value |
|---|---|
| faceCount / floorFaces / vertexCount | 16103 / 4489 / 33784 |
| entityCount | 986 |
| playableBounds (floor cell extent) | x[-5760, 6816] y[-4832, 5248] |
| zRange | [-312, 951.693] |
| zBands (2/20/40/60/80/98%) | [-308.0, -176.0, -108.0, -2.0, 181.47, 520.0] |
| cellCount / multiLevelCells | 57812 / 16789 |
| levelHistogram | {"2": 10368, "3": 4579, "1": 41023, "4": 1683, "5": 158, "6": 1} |
| worldModel mins / maxs (NOT for framing) | [-14924.0, -4856.0, -14728.0] / [6824.0, 15452.0, 1792.0] |
| zFilter band / inband points / dropped | [-256.0, 0.0] / 101 of 113 / {} |

## 3. pl_badwater — control points

BSP: tf/maps/pl_badwater.bsp  bytes=16619247  sha256=68654502abf02c205862fa034403f581a9df9d3131793e7fbf7bef926c61ac48

| name | world x | world y | world z | px | py |
|---|---|---|---|---|---|
| cap_1 | 44.000 | -1234.000 | 128.000 | 410.92 | 689.41 |
| cap_2 | -1218.000 | -908.000 | 128.000 | 245.68 | 646.73 |
| cap_3 | -1670.000 | 1966.000 | 128.000 | 186.50 | 270.43 |
| cap_4 | 96.000 | 860.000 | -132.000 | 417.73 | 415.24 |

## 4. pl_badwater — team spawn centroids (in-band only)

| team | n | centroid world | px | bbox world (xmin,xmax,ymin,ymax) |
|---|---|---|---|---|
| 2 | 32 | (490.00, 70.00, 80.00) | (469.32, 518.68) | (256, 832, -48, 240) |
| 3 | 32 | (629.00, -1202.00, 216.00) | (487.52, 685.22) | (-2368, 3680, -1664, -608) |

Extra entities:

- train_watcher: {"world": [2488.0, -984.0, 160.0], "px": [730.92, 656.681]}
- path_track: {"count": 124, "bbox": [-1670.0, 2560.0, -1888.0, 2130.0]}

## 5. pl_badwater — error budget

| item | value |
|---|---|
| round-trip max abs error (control points, float64) | 4.547e-13 world units |
| half-pixel quantization at 1024 canvas | 3.8187 world units |
| pixel rounding | +/- 0.5 px |

## 6. pl_badwater — geometry summary

| item | value |
|---|---|
| faceCount / floorFaces / vertexCount | 11357 / 3008 / 22054 |
| entityCount | 999 |
| playableBounds (floor cell extent) | x[-2944, 4576] y[-5120, 4480] |
| zRange | [-128, 896] |
| zBands (2/20/40/60/80/98%) | [-28.0, 128.0, 128.0, 256.0, 448.0, 896.0] |
| cellCount / multiLevelCells | 43601 / 10621 |
| levelHistogram | {"3": 2577, "1": 32980, "2": 7950, "4": 94} |
| worldModel mins / maxs (NOT for framing) | [-14924.0, -5136.0, -14728.0] / [4624.0, 15452.0, 2064.0] |
| zFilter band / inband points / dropped | [0.0, 256.0] / 193 of 205 / {"cells": 6841, "zRange": [-2452.0, -640.0], "bbox": [-6784.0, 288.0, 704.0, 10496.0], "materials": [["NATURE/BLENDGROUNDTOGRAVEL005", 6708], ["CONCRETE/CONCRETEFLOOR003", 100], ["NATURE/ROCKWALL013", 29], ["WOOD/WOOD_BRIDGE001", 4]]} |

## 7. Clipped cells and the off-map rule

Cells counted in outsideFrameCells/outsideCanvasCells are floor cells that fall outside the playable frame;
they are not drawn. If a player ever projects outside [0,1024], the UI must clamp the dot to the canvas edge
and show an off-map indicator — never draw a dot at a wrapped or guessed position.

## 8. Multi-level policy

Both maps have large multi-level fractions (snakewater 16789/57812 = 29.0%, badwater 10621/43601 = 24.4%).
A dot must be assigned to the layer nearest the player's rendered z; when a cell has >=2 layers the UI shows a
stack-count badge (e.g. '2') instead of silently picking one. Never draw two players at the same pixel without
indicating overlap.

## 9. Runtime contract (for the implementer)

1. Derive the frame at map load from the same rule (gameplay entity bbox + 1536 margin, intersect floor extent).
2. Print the derived minX/maxX/minY/maxY/scale into the log and compare with this ledger; a mismatch means a
   different entity set or map version and must be recorded as a new identity, not ignored.
3. Use double precision internally; round only at draw time.
4. Do not use BspMap worldModel mins/maxs for framing (skybox).
5. Clamp out-of-canvas projections to the edge with an indicator (section 7).
