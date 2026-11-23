# Third-party notices

This project is licensed under GPL-3.0-only (see `LICENSE`). The components below
are included under their own licenses, all of which are compatible with
distribution under GPL-3.0.

| Component | Path | License | Notes |
|---|---|---|---|
| LZMA SDK decoder (Igor Pavlov) | `native/third_party/lzma/` | Public domain (stated in each file header) | Used by `vpk_archive.cpp` for compressed VPK entries |
| Microsoft WebView2 SDK loader | `native/third_party/webview2/` | BSD-3-Clause style, Microsoft (`native/third_party/webview2/LICENSE.txt`) | `WebView2Loader.dll` is redistributed in the package together with its `LICENSE.txt` and `NOTICE.txt`, as `native/cmake/tf2-package.cmake` installs them |

## Not included

- **Team Fortress 2 game content.** No `.bsp`, `.vpk`, `.mdl`, `.vtf`, `.vmt`,
  `.pcf` or sound files from the game are shipped. At run time the player reads
  your own installation through `--tf-root`. The small `.mdl`/`.vtf`/`.vmt` files
  under `evidence/viewmodel/fixtures/` and `evidence/**/fixture-regen/` are
  synthetic, written by `viewmodel-fixture-gen.py` and
  `evidence/viewmodel/fixtures/viewmodel-material-fixture-gen.py`; the `.vmt`
  files under `testdata/fixtures/vmt-nesting/` are hand-written text.
- **Demo fragments.** The corrupt-input `.dem` files under
  `evidence/stability-release/**/fixtures/` and `evidence/t4-rerun-*/**/fixtures/`
  are made by `stability-make-inputs.ps1` from the first bytes (header and first
  packet) of one of the developer's own recordings, then deliberately damaged.
  `evidence/` is not part of the public `main` branch.
- **Valve source code.** No code from the Source SDK, from Valve's other
  repositories, or from the 2020 leak of the TF2 source is included. External
  code is read for behaviour only and is reimplemented from a written description
  of that behaviour (clean-room procedure in
  `docs/REFERENCE-REPOS-2026-11-21.md` §0.1). The leaked code has no license
  and is never copied, paraphrased line by line, or committed here.

## Trademarks

"Team Fortress", "TF2", "Source" and "Steam" are trademarks of Valve Corporation.
This project is not affiliated with or endorsed by Valve. The names are used only
to describe the file formats this software reads.
