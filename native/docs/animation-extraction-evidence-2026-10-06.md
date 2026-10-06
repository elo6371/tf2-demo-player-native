# Animation/ViewModel extraction evidence

Source: `d58977d` from `ai-continuation`, extracted onto `native-mvp` in
`animation-extract-d58977d`.

Included files are limited to the animation decoder, renderer-neutral ViewModel
request code, focused decoder probe, and the CMake target additions. No entity,
renderer, BSP, UI, or `main.cpp` files are changed.

## Verification

```text
cmake --build animation-build --config Release --target animation_decoder_probe --parallel 4
parent_chain=pass opened=true scout=pass soldier=pass motion=pass
```

The probe opened the real `tf2_misc_dir.vpk` under `TF_ROOT`, decoded
`models/player/scout_animations.mdl` and `soldier_animations.mdl`, and observed
different model-space positions between two samples.

`viewmodel_contract_probe` compiles with the extracted ViewModel source. A loose
`v_bat_scout.mdl` was not present in the configured TF2 directory, so its
file-path visual/resource contract was not claimed as a runtime pass. The
ViewModel code remains request/metadata construction only; it does not draw a
first-person model.

## Known limits

The decoder itself reports that Euler order and position scale are not visually
verified. Blend nodes, real frame-by-frame screenshot comparison, and renderer
integration remain outside this extraction.
