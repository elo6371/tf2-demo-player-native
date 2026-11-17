# World/visual integration 2026-11-17 — the merged tree

Branch `work/integrate` (worktree `D:/TF2_Native_Worktrees/t105-integrate`), base
`candidate/e5` = `cd04373f`. This is the tree that carries every fix from the five cards
(world-visual-fix, world-holes, disp-param, map-overview-hud, material/argument fixes) and the only
build the readings in this document describe.

**Status:** the merged build passes the gates that cover the defects it fixes, and the frame readings
are byte-identical to the per-branch readings. What is *not* green, and why, is §5 — no gate was made
lenient to get here, and nothing was re-pinned to a value that hides a real hole.

## 1. What was merged

| Branch tip | Merge commit | Branch |
| --- | --- | --- |
| `0c3987c1` | `2f1bc56c` | world-visual-fix (entity tint, unknown-switch refusal, docs) |
| `93affd2e` | `2f1bc56c` | disp-param (transposed displacement lattice) |
| `cfb6e7e4` | `80ae385b` | world-visual-fix (vis pin re-pin, two dead gates revived) |
| `09fc824c` | `b614ff0f` | map-overview-hud (minimap panel + HUD roster) |
| `2957a6a0` | `aff39957` | world-holes (sky decoupled from preset, drawn last at the far plane) |

No textual conflict in any merge. `git merge-tree --write-tree` was run before each merge and returned
rc=0 every time.

Three further commits sit **only on this branch**, because each one is a consequence of the merge
itself and not of any single card:

- `4a26ba16` — the sky gate's assertion C. On this tree the `--no-world-sky` frame no longer differs
  from the sky frame at the pinned `up1800` pose, because the displacement fix *filled* those 2,072
  pixels with terrain. That is the correct reading, not a regression, so C now asserts three
  coverage-independent accounts (completeness `diff == owned`, reliability `far == diff`, and the
  enclosed-clear identity), and a new **C+** at tick 0 — where sky is unambiguously reachable — pins
  `diff=77131 / far=77131 / owned=77131 / enclosed 6814-0==6814` and still requires `diff > 0`.
- `2f5fdee5` — the strict unknown-switch rule from `97ad8917` refused `--window-size`, which the
  window layer had honoured by prescan all along (`main.cpp:3169-3197`). The flag is now claimed in
  that same pre-ladder block and its value consumed; a dangling value is still refused.
- `5fcd737f` — the map gate now asserts that `--window-size` was honoured instead of inferring it
  from panel geometry (`window: created outer=1280x720|1920x1080|2560x1440|320x240 … pinned=1`).

If either card branch is ever continued, `2f5fdee5` is required there too — the strict rule exists on
`work/world-visual-fix` and will refuse the same flag there.

## 2. Build identity

```
bash build-cmake.sh          # configure + clean Release, NMake Makefiles, MSVC 14.44.35207
EXIT=0, exe_count=54
native/build-nmake/tf2_demo_native.exe
sha256 9d8333d8ff40f4cadb2911433e4351c85f172334bac6402eae18198678bf7e86
```

A second clean rebuild produced the same hash, so the artifact is reproducible from the tree.

## 3. What this build fixes

| Defect | Fix | Evidence |
| --- | --- | --- |
| Holes show the clear colour where geometry exists, because the sky is never drawn (it was derived from the quality preset, default `Performance`). | Sky is a correctness feature, not a quality one: always on unless explicitly disabled; drawn **after** opaque world, at the far plane, with a dedicated discard-free PS and no depth write. | sky gate 17/17; `up1800` enclosed clear 2,072 → **0**, `koth0` 9,453 → **0**, `up0` 8,976 → 3,262 |
| Displacement patches are a mirror of the map across their diagonal (transposed lattice walk): cracks at sewn seams and missing floors/stairs. | `bsp_map.cpp` walks `+i` along quad[0]->quad[1] with stride `size` and `+j` along quad[0]->quad[3] with stride 1, flat index `i*size+j`. | sewn-boundary pairs within 0.01: 3,220/7,855 → 7,502/7,855 (engine oracle 7,535); face 13233 max vertex error 341.53 → 0.0055 |
| Rendered meshes are entity-tinted a second time, so textured props come out tinted/washed. | Only untextured meshes take the team colour; drawn meshes stay white in the tint pass. | `world-entity-tint` PASS: mask 379,038 px, mutation fails 379,038 / maxDiff 184, normal within tolerance |
| An unknown `--` switch printed a warning and ran the wrong configuration. | Unknown switch is fatal: exit 13, no frame written. | `argument-refusal` PASS |
| No minimap and no roster HUD. | 250 px panel at (1002,12) with basemap + team roster columns, `M` toggles it, `Ctrl+M` stays mute, `]` still steps volume. | `map-overview` PASS 213 checks |

Supporting detail lives in `docs/WORLD-HOLES-2026-11-16.md` §12–14, `docs/WORLD-DISP-PARAM-2026-11-17.md`
§1–10, and `docs/WORLD-VISUAL-REPAIR-2026-11-16.md` §6.

## 4. Final readings on this build

### 4.1 Gates

| Gate | Result |
| --- | --- |
| `world-sky-gate-check.sh` | **17 passed, 0 failed** |
| `map-overview-check.sh` | **PASS (213 checks)** |
| `audio-keys-check.sh` | **PASS** (bare M toggles the overview only, Ctrl+M mutes, one volume step to 0.60, two restart reads, no device opened) |

Sky gate specifics: A (empty settings, `preset=0` in the ini) draws the sky — `state=active draws=1`;
B encloses zero clear pixels at all three pinned poses; C asserts the account described in §1;
D `--world-depth` still draws no sky; E an explicit `skybox=0` reproduces the `--no-world-sky` frame
exactly. The gate reads `farplane=1 shader=dedicated` from the trace, so "the sky was drawn last at
the far plane by its own shader" is asserted, not assumed.

### 4.2 Frames (4 poses × 3 views, `.scratch/final-frames`)

```
pose      enclosed   holes clearCol  crack crackNoSky
up0           3262       0       0    3262    3262
up1800           0       0       0       0       0
pr600          453       0       0     453     453
koth0            0       0       0       0       0
```

`enclosed` = clear-coloured pixels enclosed by geometry (the hole detector), `holes` = enclosed clear
pixels that no solid surface owns, `clearCol` = enclosed pixels still holding the clear colour after
composition, `crack` = enclosed pixels the sky pass had to fill. These match the disp-param branch
numbers exactly, so the three later merges changed no pixel of the terrain fix.

Before/after on the same poses (before = `86f2fc6f` + no sky decoupling): `up0` 8,976 → 3,262,
`up1800` 2,072 → 0, `pr600` 985 → 453, `koth0` 9,453 → 0. Two pixels at (997,125)/(998,125) stay on the
known list: on `pr600` they are sky-coloured (`depth=0`), on the other poses they are world pixels at
`depth=765`. They are a displacement seam, not a hole.

### 4.3 Cost of drawing the sky last

Six alternating A/B pairs, order flipped each pair, empty settings, tick 1800: per-pair fps
+5.531 / +1.608 / +3.683 / -1.052 / +0.129 / -5.529, median +0.869, mean +0.729 (sd 3.54, se 1.45).
The second run of each pair is systematically ~2.4 fps slower, so the order-balanced cost is
**+0.73 fps — below the ±1.3 fps null control. The sky pass has no measurable cost.** The earlier
"-13.9% / 3.59 ms" figure was measured on loaded and overlapping processes and is withdrawn
(`docs/WORLD-HOLES-2026-11-16.md` §13.6).

## 5. What is still not green

Stated plainly, because these are the things a reader would otherwise assume are fixed.

1. **pr600, 94 px of real geometry still culled.** Of the 453 enclosed pixels at the cp_process
   tick-600 pose, 347 have no solid face behind them, 12 are back-faces only, and **94 px are real
   front-facing geometry dropped by the screen-space back-face test** (`main.cpp:11097
   const bool faces = area > 0.0f`, gated by `drawState.worldCullBackfaces`). `--no-world-cull` fills
   81 of the 94; `--no-world-vis-clip` fills none. 80 of the 94 were already missing before the
   displacement fix, so this is a pre-existing single-sided winding bug in the cull, not a consequence
   of the parametrisation. It needs its own task.
2. **`up0`, 3,262 px with no missing geometry.** 1,887 px have no solid surface facing the camera,
   1,337 are back-faces only — 1,327 of those are `trigger_hurt` brush faces, which are invisible
   trigger volumes, so seeing sky through them is correct — and 38 px are the front-facing silhouette
   edge of projected triangles within 0.70 px of the outline. Missing geometry: 0 px.
3. **14 degenerate displacement patches still render flat.** The grid builder rejects a cell whose
   area or edge length overflows (`bsp_map.cpp:1267-1278`); a rejected face rolls back to its flat quad.
   Nine demos went from 5,654 to 5,667 displacement faces when the transposition was fixed, so this
   count can change with the lattice but the rejection rule is unchanged.
4. **The intra-cell triangulation is ours, not the engine fan** (16 of 32 triangle triples differ on the
   parity face). Geometric vertex agreement is within 0.01; the tessellation difference is deliberate
   and out of scope (`docs/WORLD-DISP-PARAM-2026-11-17.md` §7).
5. **The geometry-hole gate is not green on this tree.** It runs (build identity, native tree clean,
   sky census `faces total=21119 rendered=19845 sky=731`, first-hit oracle agreement 0.771 vs
   0.325/0.283, frame self-consistency, three cache identities all pass) but fails every `pl_odyssey`
   pose pin — that demo is not on this machine, and the pins are map-specific (`eye` pinned to
   `815.5,-147.8,-194.3` vs upstream `-1945.3,-1016.6,243.1`). Re-pinning requires the `pl_odyssey`
   recording; it was not quietly changed.
6. **`verify-all.sh` has not been run end to end on this tree.** The gates above were run
   individually. `world-material` fails on `candidate/e5` identically (1481→858, 15039→25995,
   227070→216114 with input/cluster unchanged), `world-render-reliability` was last green on the
   card branch at 48/0, and `camera-ui` / `hud-live` / `fullscreen-quiet` were green on the mapui
   branch but have not been re-run against this binary.
7. **The world path does not read `material.noCull`.** A `.vmt` that sets it would be ignored;
   this is an unsupported-feature gap, not a cause of the holes above (`$nocull` is absent from the
   two materials the earlier investigation suspected).

## 6. Running it

```
tf2_demo_native.exe --tf-root <tf directory> --demo <.dem path>

# one frame
tf2_demo_native.exe --tf-root <tf> --demo <demo> --capture-tick 1800 --capture-frame out.bmp

# trace (visibility, sky state, pass depth)
TF2_NATIVE_TRACE=trace.txt tf2_demo_native.exe --tf-root <tf> --demo <demo> --capture-tick 1800 \
    --capture-frame out.bmp

# isolate the gates do: no audio, no window, water off
tf2_demo_native.exe --tf-root <tf> --demo <demo> --capture-tick 1800 --capture-frame out.bmp \
    --audio-disabled --hidden-window --world-water off
```

Sky-related switches: `--no-world-sky` (reproduce the pre-fix frame), `--world-sky` (force it on,
now the default), `--world-depth`, `--capture-pass-depth`, `--world-hole-trace`,
`--world-allow-class`, `--no-world-cull`, `--no-world-vis-clip`, `--world-keep-map-winding`.

## 7. Figures

- `analysis/world-disp-param-up1800-block.png` — the before/after proof, side by side at the pinned
  `pl_upward_f12` tick-1800 pose: the floor is torn open with sky behind it on the left, continuous on
  the right. Committed with the displacement fix.
- `analysis/integrated-up1800.png` — this merged build at that same pose. The shed floor and walls are
  solid; before the fix the floor here showed sky between the slats.
- `analysis/integrated-up0.png` — this build at the tick-0 pose used by the attribution tables
  (terrain, track, players, sky visible through the gaps it is supposed to be visible through).
- `analysis/minimap-panel.png` — full frame with the roster HUD and the 250 px minimap.
- `analysis/minimap-panel-crop.png` — the minimap panel at 2×.
