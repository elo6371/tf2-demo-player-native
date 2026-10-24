# T67 — HUD text/primitive rendering and panel container (close-out)

Status: **all gates green** — build rc=0, forward gate PASS, mutation suite PASS,
T51 regression PASS.
Worktree `D:\TF2_Native_Worktrees\t67-hudtext`, branch `task/t67-hudtext`,
base `99eeea2`, HEAD `99eeea2` (all work is uncommitted, as the task requires).
Write scope: `native/`, `hud-text-check.sh`, `evidence/t67-hudtext/`,
`PROGRESS.txt`, this report. `D:\TF2_Native_Test` is untouched; `verify-all.sh`
was not run; nothing was pushed.

## Verdict

* Build: `bash build-target.sh tf2_demo_native` → rc=0, **0 errors** (warnings
  only; log `evidence/t67-hudtext/build.forward.log`). This was a real recompile
  of `native/src/main.cpp` and `native/src/native_renderer.cpp` plus a link, not
  a no-op: both sources were touched by the duplicate-include cleanup below.
* Forward gate: `bash hud-text-check.sh` → **HUD-TEXT-CHECK=PASS**,
  25 OK / 0 FAIL. The panel self-test is **cases=11 pass=11 fail=0** (the
  reported 4/10 failure is fixed and re-measured, not assumed).
* Mutation suite: `bash hud-text-check.sh --mutation` →
  **MUTATION-SUITE=ALL-RED-RESTORED**, 4/4 mutations red on their named
  assertion, every source restored byte-identically (sha256 unchanged), rebuilt,
  and the restored build reproduces the forward `hudtext:` line.
* T51 regression: `HUD_CHECK_OUT` in scratch, `bash hud-check.sh` → **HUD=PASS**,
  114 OK / 0 FAIL (one pre-existing T37 `NOT-VERIFIED` about the installer's
  empty-directory sweep, unrelated to this change). The `setHudRects` path still
  draws the T51 HUD: `drawnOff=0 drawnOn=5 scissor=1 region=282,333,782,228`,
  equal to the resolver's `union=282,333,782,228`.

## 1. What this foundation delivers

`native/include/hud_font.h` + `native/src/hud_font.cpp` — an embedded 8x8 font
(font8x8_basic, public domain) and its 128x48 R8 atlas:

* `kHudFontGlyphWidth/Height = 8`, 16 atlas columns x 6 rows,
  `kHudFontPrintableCount = 95`, `kHudFontFallbackCell = 95`;
* `hudFontGlyphIndex(codepoint)` returns -1 outside 0x20..0x7E;
* `hudUtf8Decode` decodes UTF-8 and maps malformed input to U+FFFD;
* the fallback cell is a hollow box (`kFallbackRows`) so a codepoint with no
  glyph is still visible instead of silently missing.

`native/include/hud_panel.h` — the pure layout container: anchored panels
(top-left / top-right / bottom-left / bottom-right), ordering by
`(order, id)`, width clamping to the safe area, collapse-to-height-0, stacking
with `marginY` counted once, and a drop count when a panel cannot fit.
No device, no drawing, no file I/O.

`native/include/native_renderer.h` + `native/src/native_renderer.cpp` — the
renderer side:

* `HudPrimitiveKind { Rect=0, Text=1, Circle=2 }` and `HudPrimitive`
  (`x,y,w,h`, `colour[4]`, `filled`, `text`, `scale`);
* `setHudPrimitives()` replaces the list; `setHudRects()` is kept and maps each
  T51 rect to a bordered `Rect` primitive, so the T51 import path and its
  readings keep working unchanged;
* the pass expands one text primitive into one quad per glyph against the atlas,
  emits 11 floats per vertex (pos2, colour4, local2, uv2, kind1), keeps the whole
  HUD a single draw call, and publishes `hudPrimitiveCount()`,
  `hudDrawnPrimitives()`, `hudDrawnRects()`, `hudTextQuads()`,
  `hudGlyphCount()`, `hudUnmappedGlyphs()`, `hudPassOrdinal()`,
  `hudScissorApplied()` and `hudLastRegion()`;
* the pixel shader has one path per kind: filled rect, bordered rect, text
  (R8 sample, coverage < 0.5 discarded), and circle (`dot(offset,offset) > 0.25`
  discarded, so the bounding-box corners stay untouched).

`native/src/main.cpp` — `--hud-text-test` builds the fixed primitive list the
gate pins: white text `"TF2 0:00 RED 12 BLU 7中"` at (40,60) scale 2 (22
codepoints, one of them unmapped), a filled rect at (40,120,240,40) in
(0.12,0.28,0.65), and two 24x24 dots at (60,200) and (120,200). It also prints
`hudpanel: cases=... pass=... fail=...` at startup and the
`hudtext: primitives=... drawn=... textQuads=... glyphs=... unmapped=...`
line on the capture frame.

## 2. Forward readings (evidence/t67-hudtext/forward.log)

```
off: hud drawn=0 ordinal=0 hudtext primitives=0 textQuads=0
on:  hud drawn=1 rects=1 scissor=1 ordinal=8
on:  hudtext primitives=4 drawn=4 textQuads=1 glyphs=22 unmapped=1 \
     passOrdinal=8 region=40,60,352,164
hudpanel: cases=11 pass=11 fail=0
frame=1264x681 regionExpected=1 outsideDiff=0 insideDiff=12336
diffBBox=40,60,391,223 rectColour=31,71,166 redCentre=255,0,0
blueCentre=25,102,255 circleCornerDiff=0 glyphBad=0 glyphChecked=8
fallbackBad=0 fallbackBox=1 verifyOk=1
```

Every assertion in the gate's four sections passed: the HUD-off run draws
nothing, the HUD-on run draws exactly four primitives, the text becomes one quad
of 22 glyphs with one fallback, the panel self-test scores 11/11, every checked
glyph pixel matches the compiled table, the fallback box is visible, the dot
corners are untouched, and **nothing outside the published region changed**
(outsideDiff=0 against the HUD-less frame).

## 3. The panel 4/10 failure: cause and fix

The reported reading was `cases=10 pass=4 fail=6` (scratch run
`.scratch/t67-hudtext/20261024122246-298`). Two defects, both in
`native/include/hud_panel.h`:

1. **The self-test expected a different order than the rule produces.** The rule
   sorts by `(order, id)`, so equal-order panels come out alphabetically
   (`minimap, objective, score, collapsed, players`); the old expectations were
   written in insertion order. The order is part of the contract, so the test was
   fixed to assert the documented order as one case.
2. **The stacking gap was counted twice.** `marginY` is both the gap from the
   anchored edge and the gap between stacked panels. The old cursor added the
   margin on both sides of a panel, so at `marginY=4` "players" landed at y=60
   instead of 56. The cursor now stores the previous panel's edge and each step
   adds the gap exactly once (comment at `native/include/hud_panel.h:94-97`,
   pinned by the `players y == 56` case at line 156).

After the fix the self-test has 11 cases and the product prints
`cases=11 pass=11 fail=0` (forward run above; also reproduced in the
12:57 and 13:09 scratch runs). The gate asserts `cases >= 10`, `fail == 0` and
`pass == cases`, so the 11/11 reading is what makes the panel section green.

## 4. Mutation suite (evidence/t67-hudtext/mutation.log)

Each mutation is applied to `native/src/native_renderer.cpp`, rebuilt, captured,
scored against the forward run, then restored from a byte backup (`cmp`), rebuilt,
and re-captured. The suite refuses to run if forward is red.

| # | defect | named reading | result |
|---|---|---|---|
| m1 | text advance dropped (`penX += 0.0f * glyphWidth`) | `glyphBad` | **RED**: glyphBad=836, outsideDiff=0 |
| m2 | unmapped glyph silently skipped (`continue`) | `unmapped` / `fallbackBox` | **RED**: glyphs=21, unmapped=0, fallbackBox=0 |
| m3 | primitive escapes its rect (full-screen quad) | `outsideDiff` | **RED**: outsideDiff=803056, region=0,0,1264,681 |
| m4 | circle mask dropped (`if (false) discard`) | `circleCornerDiff` | **RED**: circleCornerDiff=4 |

For every mutation the suite printed `RESTORED-READING-IDENTICAL` and the final
line `MUTATION-SUITE=ALL-RED-RESTORED (4/4 mutations red, every source restored
byte-identically and rebuilt)`. The source hash is identical before and after the
suite: `native_renderer.cpp` `9978ef00e92c5d33fc80fe94fd6557feedcc98fdf8683a2065ec2db98b2592bb`,
`main.cpp` `04166a11fc02a575fa8720f43f7bcc9ff7eb1cb1f42779ef8063c811e4825bb6`.
(The executable's bytes change between builds — MSVC embeds link timestamps — so
the restore proof is the trace reading plus the source hash, not an exe hash.)

## 5. T51 regression

`HUD_CHECK_OUT=.scratch/t67-hudtext/hud-check-out bash hud-check.sh` → **HUD=PASS**,
114 OK / 0 FAIL (log `evidence/t67-hudtext/regression.log`). `HUD_CHECK_OUT`
pointed the gate's readings and fixtures at scratch, so the committed T51
evidence in `evidence/hud/` is untouched (`git status --porcelain evidence/hud`
is empty).

The T51 path is intact end to end: the good fixture still imports, resolves all
six panels, hides exactly one (`visible=5`), draws five bordered blocks
(`drawnOff=0 drawnOn=5`), scissored itself (`scissor=1`), and the renderer's
region equals the resolver's union (`282,333,782,228`). The probe half of the
gate ran too (its reading set, the 102-document corpus, the refusal-by-name
table), so this is not a trace-only pass. `hud_probe.exe` had to be built for
this run — it was missing from `native/build-nmake` — and was built in the same
lock window as the gate.

One `NOT-VERIFIED` remains in the gate's output and is **pre-existing, not
caused by T67**: the user's empty directory is removed by the shared installer's
empty-directory sweep (`owner=t37-installer`). No assertion about the HUD is
unverified.

## 6. Reproduce

```bash
cd /d/TF2_Native_Worktrees/t67-hudtext
bash /d/TF2_Native_Worktrees/_shared/hostlock.sh run t67-hudtext bash -c '\
  cd /d/TF2_Native_Worktrees/t67-hudtext && \
  bash build-target.sh tf2_demo_native && \
  bash hud-text-check.sh && \
  bash hud-text-check.sh --mutation'
```

The gate pins `EXE=native/build-nmake/tf2_demo_native.exe`, the demo
`bb841c6d379ff7c40d0c8baf99f59d8d_matcha-20260927-1347-cp_snakewater_final1.dem`,
tick 0, `--debug-camera -3498.225 1021.852 1327.843 70.529329 134.999352`,
`--hidden-window --audio-disabled`, audio device 6.

## 7. Notes and limits

* **Scope.** This is the T67 foundation only: text, filled shapes, dots and the
  pure panel container. No player/objective/scoreboard data is wired into panels
  yet — that is the next step (T63 fields), and `hud_panel.h` stays pure so the
  wiring is a caller change rather than a layout rewrite.
* **`hudRectCount()` semantics.** It now returns the number of Rect primitives
  actually drawn (`hudDrawnRects_`), not the vertex count / 6. No gate reads
  `rects=` — `hud-check.sh` asserts `drawn=` and the renderer's `region=` — so
  the T51 acceptance is unaffected; the T51 readings still print
  `drawn=1 rects=1 scissor=1`.
* **Cleanup in this close-out.** Removed three editing leftovers with no
  behaviour change: a duplicate `#include "entity_model.h"`
  (`native/src/native_renderer.cpp`), a duplicate `#include "hud_state.h"`
  (`native/src/main.cpp`), and a duplicated
  `hudLastRegion_ = HudPixelRectLast{};` reset in `Renderer::draw()`.
* **Gate comment drift.** The header comment of `hud-text-check.sh` still says
  "10 self-test cases"; the container now has 11. The assertion is
  `cases >= 10`, so this is documentation drift, not a threshold change.
* **Unverified.** Every run here was on the D3D11 backend with a hidden window;
  no other backend or visible-window path was measured. The T51 gate's one
  `NOT-VERIFIED` line (installer empty-directory sweep) is pre-existing and
  belongs to t37-installer, not to this change.
