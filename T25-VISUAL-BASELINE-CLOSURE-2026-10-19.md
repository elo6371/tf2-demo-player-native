# T25 Visual Baseline Closure

## Scope

T25 re-established the visual baseline after T12, T19, T21, T23, and T24 were
integrated. All product launches used `--hidden-window --audio-disabled`; the
full `verify-all.sh` chain was not run.

## Attribution

- T12 changed the observed blend population to `declared=10 resolved=10 tiles=10`
  and `weightedVertices=85920`.
- T19 expanded material coverage to `tiled=85`, `fromPakfile=85`, and
  `vtfTooLarge=70`; `vmtMissing=3` and `vtfMissing=0` remain stable.
- T21 source-size UV normalization changed the reference frame. The final
  hidden-window frame reads W1 `75.3` and W2 `56.5`; T23 and T24 did not change
  these readings.

## Verification

On integration HEAD `ac94dcb`:

- `visual-baseline-check.sh`: **PASS**
- `visual-baseline-check.sh --mutation`: **PASS**, 5/5 mutations caught
- All five mutation source restores: `RESTORE=OK`
- Stable geometry/visibility pins: eye cluster `1032`, PVS `1692`, hidden
  triangles `22968`, drawn triangles `76344`, displaced triangles `46912`.
- Stable sky/lightmap pins: sky `active`, 6 faces, 36 vertices; lightmapped
  vertices `296490`.

## Release status

The gate is green as a regression baseline, but this does not close the visual
release blocker. W2 remains dark and background-heavy (`nearBackground=0.3361`)
and requires the separate world completeness/material task. No threshold was
changed to claim that defect was fixed.
