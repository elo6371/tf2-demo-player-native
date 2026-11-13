# 探针台账

> 由 `node scripts/probe-ledger.mjs` 生成，**不要手改** —— 手改会与磁盘脱节。
> 生成时间：2026-10-01T13:31:07.636Z
> 机器可读读数：`release-probe-ledger/ledger.json`（该目录被 `release-*/` 忽略）

## 判定规则（可复核，不是猜的）

| 状态 | 规则 |
|---|---|
| `broken` | 脚本里**行首的相对 import 有一个在磁盘上找不到** |
| `active` | 不是 broken，且下面四条信号**至少一条命中** |
| `superseded` | 不是 broken、四条信号都没命中，但**另一个写同一输出目录的脚本提交更新** |
| `archived` | 以上都不满足 |

四条信号（明细表里的「信号」列）：

| 简称 | 含义 |
|---|---|
| 证据 | 脚本里写到的 `release-*` 目录**此刻在磁盘上存在** |
| 文档 | 脚本名出现在 `docs/*.md`（**除本文件**）或 `README.md` 里 |
| npm | 脚本名出现在 `package.json` 里（即它是个 npm 脚本，不是一次性探针） |
| 脚本引用 | 有**别的**脚本提到它的名字 |

### 这几条规则测不到的（如实标出）

- **`archived` 只等于「四条信号都没命中」，不等于「已证明无用」。**
  `release-*/` 被 gitignore 且会被清理，所以证据目录消失不代表探针没人用。
  一条规则改不动这个事实，只能把它写清楚。
- **「有没有跑过变异」测不到。** 只能观测到「存在 `release-*mut*` 目录」。
  没有该目录 = **没找到变异证据**，不等于没做过变异。
- **「是否仍属发布门槛」测不到。** 门槛是人定的，见 `docs/WORKLIST-2026-10-01.md` §2。
- **「输入样本」取自脚本自己写的 `Usage:` 行。** 标 **未声明** 的脚本收参数但没说怎么给，
  这是发现项，不是空白。
- **缩进的相对 import 被刻意跳过。** 见下节 —— 代价是**已知的 2 条**，不是未知数量。

### 被刻意跳过的缩进 import（代价已记账）

`mp4-check.mjs` / `p110-audio-check.mjs` 会在字符串里拼一个 esbuild 入口
（`import ... from './bundle.js'`），那不是真的 import。所以 `broken` 只认行首的
import，代价是**恰好这 2 条**：

- `mp4-check.mjs` → `./bundle.js`
- `p110-audio-check.mjs` → `./bundle.js`

## 总账

- `active`：**69** —— 四条信号至少一条命中 —— 发布验收仍可能调用
- `archived`：**82** —— 四条信号都没命中 —— **不等于可删**
- `superseded`：**2** —— 同一输出目录上有更新的脚本

合计 **153** 个脚本（不含 `scripts/lib/`）。

## 复核队列（`archived`，按最近提交倒序）

`archived` 是「**没找到任何使用信号**」，不是「已确认无用」。下面 82 条里有
**63** 条是 2026-09-28 之后才提交的 —— 它们多半是**一次性考古探针**（问题答完了，
结论进了文档，脚本就留在原地），但这只能由人判定，本脚本不替人下结论。
**本脚本不移动、不改名、不删除任何文件**（决策 ⑦ 要求先建台账，`scripts/archive/` 之后再议）。

| 探针 | 最近提交 | 做什么 |
|---|---|---|
| `disp-face-probe.ts` | `6c20e94` 2026-09-30 | Raw numbers for a handful of displacement faces. |
| `disp-lightmap-probe.ts` | `6c20e94` 2026-09-30 | Quantify the displacement lightmap defect and settle `lump 34`'s record count. |
| `disp-lump-probe.ts` | `6c20e94` 2026-09-30 | Which lump actually holds the displacement vertices, and how many are there? |
| `disp-sample-probe.ts` | `6c20e94` 2026-09-30 | What is actually in `lump 34` (DISP_LIGHTMAP_SAMPLE_POSITIONS)? |
| `html-to-pdf.cjs` | `553cec9` 2026-09-30 | Render a local HTML file to PDF, using the Electron already in node_modules. |
| `packaged-launch-matrix.mjs` | `6c20e94` 2026-09-30 | Does the packaged Electron app stay alive, and what kills it? |
| `playback-control-check.mjs` | `f022405` 2026-09-30 | P1-01: playback, pause, frame stepping, speed, reverse and random seeks. |
| `player-eye-check.ts` | `553cec9` 2026-09-30 | Does the first-person camera really sit at the height the demo carries? |
| `png-write.ts` | `6c20e94` 2026-09-30 | Minimal RGBA8 → PNG encoder for Node probes. |
| `sky-adjacency-probe.ts` | `6c20e94` 2026-09-30 | Recover the horizontal adjacency of the four sky side faces, by matching edge profiles rather than by trusting |
| `sky-orientation-probe.ts` | `6c20e94` 2026-09-30 | Pin the skybox V-axis convention with a hard geometric constraint. |
| `sky-survey-probe.ts` | `6c20e94` 2026-09-30 | Survey every skybox set installed, to pin the vertical convention. |
| `sky-top-probe.ts` | `6c20e94` 2026-09-30 | Pin the rotation of the `up` and `dn` faces, using the same edge-profile match that fixed the side faces' adja |
| `worldlight-probe.ts` | `6c20e94` 2026-09-30 | What are the world lights in a BSP, and how wide is one record? |
| `defindex-model-probe.ts` | `cb24358` 2026-09-28 | P1-05 recon, part two: do two item definition indexes really give two models? |
| `entity-index-trace-probe.ts` | `cb24358` 2026-09-28 | P1-05 recon, part three: is a weapon's item definition index read at render time trustworthy, or can an edict  |
| `export-options-check-preconditions.mjs` | `f52ff78` 2026-09-28 | P1-11: the output options, end to end. |
| `export-options-check-preconditions2.mjs` | `f52ff78` 2026-09-28 | P1-11: the output options, end to end. |
| `export-options-check-settled.mjs` | `f52ff78` 2026-09-28 | P1-11: the output options, end to end. |
| `export-repro-check.mjs` | `050bf61` 2026-09-28 | Is an export reproducible across the material-loading boundary? |
| `item-class-context-probe.ts` | `5f7b1a7` 2026-09-28 | Independent cross-check for the counting in `weapon-class-audit.ts`. |
| `item-defindex-probe.ts` | `cb24358` 2026-09-28 | P1-05 recon: is an item definition index actually available? |
| `lump43-probe.ts` | `050bf61` 2026-09-28 | Why `SourceMap.materialNames` came out as garbage. |
| `makepack.ts` | `b14f644` 2026-09-28 | Generate a renderable geometry pack for one map. |
| `map-state-check.mjs` | `b14f644` 2026-09-28 | P1-03: the map panel tells the truth about three separate things. |
| `material-check.ts` | `050bf61` 2026-09-28 | The acceptance check for the P1-06 asset layer. |
| `material-mount-check.mjs` | `050bf61` 2026-09-28 | P1-06 / P1-07 render-side acceptance: do the resolved materials reach the frame buffer? |
| `material-source-probe.ts` | `050bf61` 2026-09-28 | Why the mount check uploads 63 textures when the map table names 176 materials. |
| `mp4-browser-playback-probe.mjs` | `f52ff78` 2026-09-28 | Does the browser play back the MP4 the exporter writes — the whole file, both tracks — rather than merely acce |
| `p109-codec-probe.mjs` | `f52ff78` 2026-09-28 | P1-09 decision input: can this Chromium actually encode H.264? |
| `p110-audio-probe.mjs` | `f52ff78` 2026-09-28 | P1-10 decision input: which audio encoders does this Chromium actually have? |
| `p111-tick-probe.mjs` | `050bf61` 2026-09-28 | P1-11 second probe: does the exported picture depend on the requested tick? |
| `probe-install-layout.mjs` | `2616393` 2026-09-28 | Read-only one-off probe: what the real TF2 install on this machine looks like, so a negative control for the m |
| `rig-anim-decode.ts` | `050bf61` 2026-09-28 | Decide where an animation descriptor's data really begins. |
| `rig-anim-dump.ts` | `050bf61` 2026-09-28 | Byte-level dump of one animation's entry chain, to settle where the rot/pos valueptr tables and the channel bl |
| `rig-anim-gate.ts` | `050bf61` 2026-09-28 | Measure how many animations a decoder can actually account for. |
| `rig-anim-motion-check.ts` | `050bf61` 2026-09-28 | Show that the animation decode produces motion, and that the motion is the file's rather than the program's. |
| `rig-anim-proof.ts` | `050bf61` 2026-09-28 | Decide the animation layout of a real TF2 MDL by criteria the data can fail. |
| `rig-anim-scan.ts` | `050bf61` 2026-09-28 | Locate the animation data without assuming anything about payload sizes. |
| `rig-anim-verify.ts` | `050bf61` 2026-09-28 | Decode a real animation and check it against numbers that are not in it. |
| `rig-animdesc-fields.ts` | `050bf61` 2026-09-28 | Dump mstudioanimdesc_t fields that change how the animation payload is reached: animblock / animindex / sectio |
| `rig-chain-dump.ts` | `050bf61` 2026-09-28 | Dump one animation entry chain byte by byte. |
| `rig-chain-vs-bind.ts` | `050bf61` 2026-09-28 | Put the stored channel next to the bind row it is supposed to describe. |
| `rig-entry-verdict.ts` | `050bf61` 2026-09-28 | Per-entry verdict for one animation: which channels decode, which do not. |
| `rig-euler-anchor-check.ts` | `050bf61` 2026-09-28 | Ask the animation payloads whether they reproduce a pose that is already known, instead of asking whether they |
| `rig-header-probe.ts` | `050bf61` 2026-09-28 | Locate the attachment table in the MDL header, and cross-read two models' bone tables. |
| `rig-hypothesis-search.ts` | `050bf61` 2026-09-28 | Search the payload conventions, and let the landmarks pick the winner. |
| `rig-layout-probe.ts` | `050bf61` 2026-09-28 | Locate the animation-bearing structures inside a real TF2 MDL. |
| `rig-layout-solve.ts` | `050bf61` 2026-09-28 | Solve the two remaining layout questions by search, scored on the data. |
| `rig-mesh-verify.ts` | `050bf61` 2026-09-28 | Verify the settled decode against the one witness that no convention can move: the mesh. |
| `rig-pelvis-position-search.ts` | `050bf61` 2026-09-28 | Find the reading of a raw position channel, using only the pelvis. |
| `rig-pose-check.ts` | `050bf61` 2026-09-28 | Check the animation decoder against a pose the file states independently. |
| `rig-pose-trace.ts` | `050bf61` 2026-09-28 | Trace one bone's decode, byte by byte, so a wrong pose can be blamed on a specific read rather than on "the de |
| `rig-raw-dump.ts` | `050bf61` 2026-09-28 | Raw field dump for the anim-bearing tables, plus a header-free search for the animation block. |
| `rig-rawpos-order-tally.ts` | `050bf61` 2026-09-28 | Measure the axis order of the raw position channel over the whole file. |
| `rig-rle-check.ts` | `050bf61` 2026-09-28 | Validate the compressed-channel layout across every animation in a real file. |
| `rig-rle-v2.ts` | `050bf61` 2026-09-28 | Decide the compressed-channel block reading by walking every channel in a real animations.mdl under two candid |
| `rig-rle-v3.ts` | `050bf61` 2026-09-28 | Validate the compressed-channel reading against the reference implementation. |
| `switch-blocked-probe.mjs` | `0b77368` 2026-09-28 | Probe before asserting: two states the switch checks do not reach yet. |
| `switch-concurrency-probe.mjs` | `3bcbc35` 2026-09-28 | Probe before asserting: what can be observed *during* a load? |
| `timeline-size-probe.mjs` | `2616393` 2026-09-28 | Does the timeline canvas get a size, or does it stay collapsed? |
| `weapon-look-browser-check.ts` | `cb24358` 2026-09-28 | P1-05 at the render layer: does the running product actually draw the item's own model, and does it say so? |
| `zip-lzma-solve-probe.ts` | `050bf61` 2026-09-28 | Find the header split Valve uses for ZIP entries compressed with method 14. |
| `bone-layout-probe.ts` | `b11ae8e` 2026-09-27 | — |
| `bone-stride-probe.ts` | `b11ae8e` 2026-09-27 | Locate the real `mstudiobone_t` stride. |
| `bone-up-probe.ts` | `b11ae8e` 2026-09-27 | Second, independent witness for model-space up axis. |
| `bsp-axis-probe.ts` | `b11ae8e` 2026-09-27 | Independent witness for the engine's coordinate convention. |
| `class-probe.ts` | `b11ae8e` 2026-09-27 | Why character models are not drawn. |
| `demoaudit.ts` | `983bdf8` 2026-09-27 | — |
| `export-diagnose.mjs` | `9fb94b0` 2026-09-27 | Diagnostic export check. |
| `export-state-probe.mjs` | `9fb94b0` 2026-09-27 | P0-02 probe: export dialog settings state convergence. |
| `export-state-probe2.mjs` | `9fb94b0` 2026-09-27 | P0-02 probe 2: deterministic evidence for the three export-state defects. |
| `export-state-probe3.mjs` | `9fb94b0` 2026-09-27 | P0-02 probe 3: the remaining convergence requirements. |
| `hdr-probe.ts` | `b11ae8e` 2026-09-27 | — |
| `local-joint-check.mjs` | `df58440` 2026-09-27 | P0-03: joint acceptance with a real demo, the real BSP, and the real TF2 directory contents. |
| `model-orientation-probe.ts` | `b11ae8e` 2026-09-27 | Orientation and scale cross-check for real player models. |
| `playback-delta-probe.ts` | `df58440` 2026-09-27 | P1-01 evidence: does reverse traversal reconstruct the same state as forward play, on the real demo? |
| `playback-state-probe.mjs` | `df58440` 2026-09-27 | P1-01 diagnostic: does the reconstructed state actually follow the tick? |
| `probe-wiring.mjs` | `9fb94b0` 2026-09-27 | Confirm whether the addInitScript instrumentation actually reaches the bundle, and whether a second settings e |
| `toe-facing-probe.ts` | `df58440` 2026-09-27 | Independent confirmation of which side of a player model is the front. |
| `vvd-layout-probe.ts` | `b11ae8e` 2026-09-27 | — |
| `vvd-layout-solve.ts` | `b11ae8e` 2026-09-27 | Pin down the VVD vertex layout with a sharp metric. |

## 明细

| 探针 | 跑法 | 做什么 | 状态 | 信号 | 最近提交 | 输入样本 | 输出证据 | 变异证据 |
|---|---|---|---|---|---|---|---|---|
| `active-weapon-look-probe.ts` | vite-node | P1-05 evidence: what the renderer is handed, weapon by weapon. | `active` | 脚本引用 | `cb24358` 2026-09-28 | `vite-node scripts/active-weapon-look-probe.ts <tf dir> <demo.dem>` | — | — |
| `asset-chain-check.ts` | vite-node | Real-asset resource chain probe (reading layer only). | `active` | 文档+脚本引用 | `f022405` 2026-09-30 | **未声明** | — | — |
| `attachment-probe.ts` | vite-node | Does a TF2 player model actually carry a `weapon_bone` attachment? | `active` | 证据+文档 | `288baf6` 2026-10-01 | `node node_modules/vite-node/vite-node.mjs scripts/attachment-probe.ts <tfRoot> [out.json]` | release-attachment<br><sub>2026-10-01</sub> | — |
| `browsercheck.mjs` | node | Headless render check. | `active` | npm | `050bf61` 2026-09-28 | **未声明** | — | — |
| `build-catalog.mjs` | node | Regenerate `public/assets-db/catalog.json` from two inputs: | `active` | 脚本引用 | `b14f644` 2026-09-28 | **未声明** | — | — |
| `camera-check.mjs` | node | P1-02: paused free-look, POV/SourceTV view availability, and speed isolation. | `active` | 文档+脚本引用 | `6c20e94` 2026-09-30 | `node scripts/camera-check.mjs <demo.dem> [map.bsp] [tf folder] [outdir] [follow-absence.json]` | — | — |
| `camera-probe.mjs` | node | One-question diagnostic: with the map loaded and the default camera in place, is the map inside the view volume the product actually submits? | `active` | 文档 | `f022405` 2026-09-30 | **未声明** | — | — |
| `character-render-check.ts` | vite-node | What does the map actually look like at player scale? | `active` | 证据 | `09b1934` 2026-09-30 | `vite-node scripts/character-render-check.ts <demo.dem> <map.bsp> <tf folder> [outdir]` | release-character-render<br><sub>2026-09-30</sub> | — |
| `desktop-smoke.mjs` | node | — | `active` | npm+脚本引用 | `f022405` 2026-09-30 | **未声明** | — | — |
| `disp-lightmap-render-check.mjs` | node | Does the displacement lightmap coordinate actually reach the GPU, and does the frame still render? | `active` | 证据+文档+脚本引用 | `5c7f429` 2026-10-01 | `node scripts/disp-lightmap-render-check.mjs <demo.dem> <map.bsp> <tf folder> [outdir] [--swap-extents] [--offset-half]` | release-disp-lightmap-render<br><sub>2026-10-01</sub> | — |
| `disp-luxel-coords-probe.ts` | vite-node | Does the SDK's displacement lightmap coordinate fill the block exactly, and is it in the same luxel space the planar path uses? | `active` | 证据+文档+脚本引用 | `5c7f429` 2026-10-01 | `node node_modules/vite-node/vite-node.mjs scripts/disp-luxel-coords-probe.ts <map.bsp> [out.json] [--swap-extents] [--offset-half]` | release-disp-luxel-coords<br><sub>2026-10-01</sub> | — |
| `disp-sample-decode-probe.ts` | vite-node | Decode `LUMP_DISP_LIGHTMAP_SAMPLE_POSITIONS` (lump 34) and use it to decide, from the data, how a displacement vertex maps to a lightmap luxel. | `active` | 脚本引用 | `6c20e94` 2026-09-30 | `vite-node scripts/disp-sample-decode-probe.ts <map.bsp> [out.json]` | — | — |
| `disp-triindex-probe.ts` | vite-node | Does the displacement triangle-index table in lump 34 decode with the SDK's own algorithm? | `active` | 证据+文档 | `c978b05` 2026-10-01 | **未声明** | release-disp-triindex<br><sub>2026-10-01</sub> | — |
| `draw-trace-probe.mjs` | node | One question: does the world pass rasterize anything at all? | `active` | 文档 | `f022405` 2026-09-30 | `node scripts/draw-trace-probe.mjs --demo=<demo.dem> --tf=<tf folder>` | — | — |
| `e2echeck.ts` | vite-node | End-to-end parse check over the generated fixtures. | `active` | npm | `983bdf8` 2026-09-27 | **未声明** | — | — |
| `encoder-leak-probe.mjs` | node | Does the app's own capability probe leak `VideoEncoder` instances? | `active` | 脚本引用 | `f52ff78` 2026-09-28 | `node scripts/encoder-leak-probe.mjs <dev-server-url> [outdir]` | — | — |
| `export-filesystem-check.mjs` | node | Does the export path keep its promises about files on disk? | `active` | 证据+文档 | `bc971c9` 2026-10-01 | `node scripts/export-filesystem-check.mjs [--outdir=release-export-filesystem]` | release-export-filesystem<br><sub>2026-10-01</sub> | — |
| `export-options-check.mjs` | node | P1-11: the output options, end to end. | `active` | 脚本引用 | `050bf61` 2026-09-28 | **未声明** | — | — |
| `export-verify.mjs` | node | Export acceptance, end to end, in the order the app actually reaches each state. | `active` | 文档+脚本引用 | `06fdba7` 2026-10-01 | `node scripts/export-verify.mjs demo.dem map.bsp <output dir> [tfRoot]` | — | — |
| `exportcheck.mjs` | node | Verify the export path end to end, inside a real browser. | `active` | npm | `f52ff78` 2026-09-28 | **未声明** | — | — |
| `follow-absence-probe.ts` | vite-node | Offline companion to the browser camera check: find a *deterministic* moment where a follow target is absent from the state map. | `active` | 脚本引用 | `a600964` 2026-09-28 | `vite-node scripts/follow-absence-probe.ts <demo.dem> [out.json]` | — | — |
| `frame-stats.mjs` | node | Colour statistics for a rendered frame, decoded from the PNG the browser actually presented. | `active` | 文档 | `f022405` 2026-09-30 | `node scripts/frame-stats.mjs <png> [...]` | — | — |
| `graphics-fallback-check.mjs` | node | Does the desktop shell come up when the GPU does not? | `active` | 证据+文档+脚本引用 | `bc971c9` 2026-10-01 | `node scripts/graphics-fallback-check.mjs [--outdir=release-graphics]` | release-graphics<br><sub>2026-10-01</sub> | — |
| `hud-theme-check.mjs` | node | HUD and theme acceptance, end to end. | `active` | 证据+文档+脚本引用 | `94b09eb` 2026-10-01 | **未声明** | release-hud-theme<br><sub>2026-09-30</sub> | — |
| `kill-event-probe.ts` | vite-node | Decisive probe: can `player_death` be joined to player names? | `active` | 脚本引用 | `2b93898` 2026-09-28 | **未声明** | — | — |
| `lightmap-brightness-probe.ts` | vite-node | How bright is the bake the shader actually multiplies by? | `active` | 文档 | `f022405` 2026-09-30 | `node --experimental-strip-types scripts/lightmap-brightness-probe.ts <tfRoot> <mapName>` | — | — |
| `load-diagnose.mjs` | node | Why does `camera-check.mjs` time out in `loadDemo`? | `active` | 文档 | `6c20e94` 2026-09-30 | `node scripts/load-diagnose.mjs <demo.dem> [seconds]` | — | — |
| `local-model-check.ts` | vite-node | Exercise the browser resource reader against an installed TF2, without extracting entire VPK volumes or including Valve assets in the application. | `active` | 文档+脚本引用 | `5f7b1a7` 2026-09-28 | `vite-node scripts/local-model-check.ts <TF2 tf directory>` | — | — |
| `local-resource-resume-check.mjs` | node | Does the TF2 resource-directory grant actually come back on the next visit? | `active` | 证据+文档 | `e7a99d3` 2026-10-01 | `node scripts/local-resource-resume-check.mjs [outdir]` | release-local-resume<br><sub>2026-10-01</sub> | — |
| `maketestdata.ts` | vite-node | Emit a synthetic .dem to disk for end-to-end browser testing. | `active` | 文档+npm | `983bdf8` 2026-09-27 | **未声明** | — | — |
| `map-mesh-hull-probe.ts` | vite-node | Is the world mesh the renderer is handed actually a map? | `active` | 文档 | `f022405` 2026-09-30 | `npx vite-node scripts/map-mesh-hull-probe.ts <tfRoot> <mapName>` | — | — |
| `map-render-check.mjs` | node | A frame-level check for the map renderer: load the real demo, the real map and the real `tf` folder, then read the actual framebuffer. | `active` | 证据+文档+脚本引用 | `6c20e94` 2026-09-30 | `node scripts/map-render-check.mjs <demo.dem> <map.bsp> <tf folder> [outdir]` | release-map-render<br><sub>2026-09-30</sub> | — |
| `map-version-check.mjs` | node | Does the map panel say which *versions* of the map exist, and which a league played? | `active` | 证据+文档 | `9e94152` 2026-10-01 | `node scripts/map-version-check.mjs <bagel.dem> <bagel.bsp> <snakewater.dem> <snakewater.bsp> [outdir]` | release-map-version<br><sub>2026-10-01</sub> | — |
| `material-gap-audit.ts` | vite-node | B7: an independent second opinion on "237 of 247 materials are drawn". | `active` | 文档 | `288baf6` 2026-10-01 | `vite-node scripts/material-gap-audit.ts <map.bsp> <tf folder> <out.json>` | — | — |
| `material-probe.ts` | vite-node | Resolve every material of a real map, end to end, with no browser. | `active` | 脚本引用 | `050bf61` 2026-09-28 | **未声明** | — | — |
| `mdl-version-probe.ts` | vite-node | What changed in the MDL header between versions, measured rather than recalled. | `active` | 文档 | `f022405` 2026-09-30 | **未声明** | — | — |
| `merge-gate.mjs` | node | — | `active` | 证据+文档+脚本引用 | `de4174b` 2026-10-01 | **未声明** | release-merge-gate<br>release-resource-path<br>release-hud-theme<br>release-timeline-export<br><sub>2026-10-01</sub> | — |
| `mp4-check.mjs` | node | P1-09 acceptance: real MP4 output. | `active` | 文档+脚本引用 | `f52ff78` 2026-09-28 | `node scripts/mp4-check.mjs [output directory]` | — | — |
| `p110-audio-check.mjs` | node | P1-10 acceptance: a real audio track in the exported container, and where it actually lands on the timeline. | `active` | 文档+脚本引用 | `f52ff78` 2026-09-28 | `node scripts/p110-audio-check.mjs [output directory]` | — | — |
| `p111-diagnose.mjs` | node | P1-11 diagnostics: separate probe defects from product defects. | `active` | 脚本引用 | `050bf61` 2026-09-28 | `node scripts/p111-diagnose.mjs <demo.dem> <bsp> [outdir]` | — | — |
| `p111-f1-probe.mjs` | node | P1-11 diagnosis: what does the UI actually do when the encoder fails? | `active` | 脚本引用 | `050bf61` 2026-09-28 | **未声明** | — | — |
| `package-single.mjs` | node | — | `active` | 文档+npm+脚本引用 | `f022405` 2026-09-30 | **未声明** | — | — |
| `packaged-app-check.mjs` | node | Does the packaged desktop app actually start and render, on its own? | `active` | 证据+文档+脚本引用 | `2168717` 2026-09-30 | **未声明** | release-packaged<br><sub>2026-09-30</sub> | — |
| `packaged-cdp-check.mjs` | node | Start the packaged app and talk to it over CDP directly, with no driver. | `active` | 证据 | `553cec9` 2026-09-30 | **未声明** | release-packaged<br><sub>2026-09-30</sub> | — |
| `packaged-desktop-check.mjs` | node | Acceptance for the *desktop* flavour, not the browser bundle. | `active` | 文档 | `6c20e94` 2026-09-30 | **未声明** | — | — |
| `pipelinecheck.ts` | vite-node | End-to-end pipeline check. | `active` | npm | `314bea5` 2026-09-27 | **未声明** | — | — |
| `player-origin-probe.ts` | vite-node | Does the entity the follow camera watches actually move? | `active` | 文档+脚本引用 | `7d0166b` 2026-10-01 | `vite-node scripts/player-origin-probe.ts <demo.dem> [out.json]` | — | — |
| `player-view-probe.ts` | vite-node | What does the demo actually carry for a player's eye height? | `active` | 文档+脚本引用 | `7d0166b` 2026-10-01 | `vite-node scripts/player-view-probe.ts <demo.dem> [out.json]` | — | — |
| `pose-review.mjs` | node | Frames for the human pass over player animation. | `active` | 证据+文档 | `78a2731` 2026-10-01 | **未声明** | release-pose-review<br><sub>2026-10-01</sub> | — |
| `pov-scan.ts` | vite-node | Which demos on this machine are *real* POV recordings? | `active` | 证据+文档 | `e956d21` 2026-10-01 | **未声明** | release-pov-scan<br><sub>2026-10-01</sub> | — |
| `probe-ledger.mjs` | node | — | `active` | 证据+文档 | `6abe4fb` 2026-10-01 | **未声明** | release-probe-ledger<br><sub>2026-10-01</sub> | — |
| `real-playback-check.mjs` | node | — | `active` | 文档+脚本引用 | `86c8927` 2026-09-30 | **未声明** | — | — |
| `real-window-check.mjs` | node | P0-1 / P0-2 / P0-3 acceptance in a real window, against real files. | `active` | 文档+脚本引用 | `f022405` 2026-09-30 | **未声明** | — | — |
| `resource-path-check.mjs` | node | Resource-path acceptance, end to end. | `active` | 证据+文档+脚本引用 | `288baf6` 2026-10-01 | `node scripts/resource-path-check.mjs <demo.dem> <map.bsp> <tf folder> [outdir]` | release-resource-path<br><sub>2026-09-30</sub> | — |
| `rig-bind-differential.ts` | vite-node | Separate "the composition is wrong" from "the animation values are wrong". | `active` | 脚本引用 | `050bf61` 2026-09-28 | `vite-node scripts/rig-bind-differential.ts <TF2 tf directory> [animation]` | — | — |
| `rig-convention-check.ts` | vite-node | Settle the convention the skeleton composition depends on. | `active` | 脚本引用 | `050bf61` 2026-09-28 | `vite-node scripts/rig-convention-check.ts <TF2 tf directory> [model.mdl]` | — | — |
| `rig-rle-v4.ts` | vite-node | Same validation as rig-rle-v3, plus the one thing v3 got wrong: animations with `sectionindex != 0` address their payload in `sectionframes`-long sect | `active` | 文档 | `050bf61` 2026-09-28 | `vite-node scripts/rig-rle-v4.ts <tf> [model.mdl]` | — | — |
| `serve-dist.mjs` | node | Serve this tree's build for the checks that need an HTTP origin. | `active` | 脚本引用 | `2616393` 2026-09-28 | `node scripts/serve-dist.mjs [port]` | — | — |
| `sky-probe.ts` | vite-node | Skybox groundwork probe — no product code involved. | `active` | 脚本引用 | `6c20e94` 2026-09-30 | `vite-node scripts/sky-probe.ts <map.bsp> <tf-dir> [out.json]` | — | — |
| `sky-render-check.ts` | vite-node | Does the sky on screen match the sky the assets describe? | `active` | 证据 | `6c20e94` 2026-09-30 | `vite-node scripts/sky-render-check.ts <demo.dem> <map.bsp> <tf folder> [outdir]` | release-sky-render<br><sub>2026-09-30</sub> | — |
| `sky-shader-check.ts` | vite-node | Check the sky UV mapping against real face bitmaps, at the seams. | `active` | 脚本引用 | `6c20e94` 2026-09-30 | `vite-node scripts/sky-shader-check.ts <tf-dir> [--png prefix] [out.json]` | — | — |
| `sky-texture-probe.ts` | vite-node | Skybox texture probe — decode the six faces and look at them. | `active` | 证据 | `6c20e94` 2026-09-30 | `vite-node scripts/sky-texture-probe.ts <skyname> <tf-dir> [out-dir]` | release-sky-textures<br><sub>2026-09-30</sub> | — |
| `staticprop-probe.ts` | vite-node | Sweep every installed map for its static props and its lightmap shape. | `active` | 文档 | `050bf61` 2026-09-28 | **未声明** | — | — |
| `teamskin-probe.ts` | vite-node | What TF2's own materials actually say about team skins. | `active` | 脚本引用 | `050bf61` 2026-09-28 | `vite-node scripts/teamskin-probe.ts <TF2 tf directory> [map.bsp] <output.json>` | — | — |
| `timeline-export-check.mjs` | node | Timeline / demo-switch / export mutual exclusion, end to end. | `active` | 证据+文档+脚本引用 | `94b09eb` 2026-10-01 | `node scripts/timeline-export-check.mjs <demoA> <mapA> <demoB> <mapB> <tf folder> [outdir]` | release-timeline-export<br><sub>2026-09-30</sub> | — |
| `uicheck.mjs` | node | Drive the real user flow: pick a file, parse it, confirm the recording type, and land in the player. This is the only check that exercises the Worker  | `active` | npm+脚本引用 | `983bdf8` 2026-09-27 | **未声明** | — | — |
| `vpk-tree.ts` | vite-node | VPK v1/v2 directory tree reading and entry extraction, for Node probes. | `active` | 文档+脚本引用 | `6c20e94` 2026-09-30 | **未声明** | — | — |
| `weapon-class-audit.ts` | vite-node | P1-04: why two weapon classes have no model. | `active` | 脚本引用 | `5f7b1a7` 2026-09-28 | `vite-node scripts/weapon-class-audit.ts <TF2 tf directory> [demo.dem]` | — | — |
| `weapon-look-check.ts` | vite-node | P1-05: does the product take a weapon's appearance from its item definition index, and does it say so when it cannot? | `active` | 脚本引用 | `cb24358` 2026-09-28 | `vite-node scripts/weapon-look-check.ts <TF2 tf directory>` | — | — |
| `bone-layout-probe.ts` | vite-node | — | `archived` | 无 | `b11ae8e` 2026-09-27 | **未声明** | — | — |
| `bone-stride-probe.ts` | vite-node | Locate the real `mstudiobone_t` stride. | `archived` | 无 | `b11ae8e` 2026-09-27 | **未声明** | — | — |
| `bone-up-probe.ts` | vite-node | Second, independent witness for model-space up axis. | `archived` | 无 | `b11ae8e` 2026-09-27 | **未声明** | — | — |
| `bsp-axis-probe.ts` | vite-node | Independent witness for the engine's coordinate convention. | `archived` | 无 | `b11ae8e` 2026-09-27 | **未声明** | — | — |
| `class-probe.ts` | vite-node | Why character models are not drawn. | `archived` | 无 | `b11ae8e` 2026-09-27 | **未声明** | — | — |
| `defindex-model-probe.ts` | vite-node | P1-05 recon, part two: do two item definition indexes really give two models? | `archived` | 无 | `cb24358` 2026-09-28 | `vite-node scripts/defindex-model-probe.ts <tf dir> <demo.dem>` | — | — |
| `demoaudit.ts` | vite-node | — | `archived` | 无 | `983bdf8` 2026-09-27 | **未声明** | — | — |
| `disp-face-probe.ts` | vite-node | Raw numbers for a handful of displacement faces. | `archived` | 无 | `6c20e94` 2026-09-30 | `vite-node scripts/disp-face-probe.ts <map.bsp> [out.json]` | — | — |
| `disp-lightmap-probe.ts` | vite-node | Quantify the displacement lightmap defect and settle `lump 34`'s record count. | `archived` | 无 | `6c20e94` 2026-09-30 | `vite-node scripts/disp-lightmap-probe.ts <map.bsp> [out.json]` | — | — |
| `disp-lump-probe.ts` | vite-node | Which lump actually holds the displacement vertices, and how many are there? | `archived` | 无 | `6c20e94` 2026-09-30 | `npx vite-node scripts/disp-lump-probe.ts <map.bsp> [out.json]` | — | — |
| `disp-sample-probe.ts` | vite-node | What is actually in `lump 34` (DISP_LIGHTMAP_SAMPLE_POSITIONS)? | `archived` | 无 | `6c20e94` 2026-09-30 | `vite-node scripts/disp-sample-probe.ts <map.bsp> [out.json]` | — | — |
| `entity-index-trace-probe.ts` | vite-node | P1-05 recon, part three: is a weapon's item definition index read at render time trustworthy, or can an edict index hand back another entity's value? | `archived` | 无 | `cb24358` 2026-09-28 | `vite-node scripts/entity-index-trace-probe.ts <tf dir> <demo.dem> [index...]` | — | — |
| `export-diagnose.mjs` | node | Diagnostic export check. | `archived` | 无 | `9fb94b0` 2026-09-27 | **未声明** | — | — |
| `export-options-check-preconditions.mjs` | node | P1-11: the output options, end to end. | `archived` | 无 | `f52ff78` 2026-09-28 | **未声明** | — | — |
| `export-options-check-preconditions2.mjs` | node | P1-11: the output options, end to end. | `archived` | 无 | `f52ff78` 2026-09-28 | **未声明** | — | — |
| `export-options-check-settled.mjs` | node | P1-11: the output options, end to end. | `archived` | 无 | `f52ff78` 2026-09-28 | **未声明** | — | — |
| `export-repro-check.mjs` | node | Is an export reproducible across the material-loading boundary? | `archived` | 无 | `050bf61` 2026-09-28 | **未声明** | — | — |
| `export-state-probe.mjs` | node | P0-02 probe: export dialog settings state convergence. | `archived` | 无 | `9fb94b0` 2026-09-27 | **未声明** | — | — |
| `export-state-probe2.mjs` | node | P0-02 probe 2: deterministic evidence for the three export-state defects. | `archived` | 无 | `9fb94b0` 2026-09-27 | **未声明** | — | — |
| `export-state-probe3.mjs` | node | P0-02 probe 3: the remaining convergence requirements. | `archived` | 无 | `9fb94b0` 2026-09-27 | **未声明** | — | — |
| `hdr-probe.ts` | vite-node | — | `archived` | 无 | `b11ae8e` 2026-09-27 | **未声明** | — | — |
| `html-to-pdf.cjs` | node | Render a local HTML file to PDF, using the Electron already in node_modules. | `archived` | 无 | `553cec9` 2026-09-30 | `node_modules/electron/dist/electron.exe scripts/html-to-pdf.cjs <in.html> <out.pdf>` | — | — |
| `item-class-context-probe.ts` | vite-node | Independent cross-check for the counting in `weapon-class-audit.ts`. | `archived` | 无 | `5f7b1a7` 2026-09-28 | `vite-node scripts/item-class-context-probe.ts <TF2 tf directory> [name…]` | — | — |
| `item-defindex-probe.ts` | vite-node | P1-05 recon: is an item definition index actually available? | `archived` | 无 | `cb24358` 2026-09-28 | `vite-node scripts/item-defindex-probe.ts <demo.dem> [tick]` | — | — |
| `local-joint-check.mjs` | node | P0-03: joint acceptance with a real demo, the real BSP, and the real TF2 directory contents. | `archived` | 无 | `df58440` 2026-09-27 | **未声明** | — | — |
| `lump43-probe.ts` | vite-node | Why `SourceMap.materialNames` came out as garbage. | `archived` | 无 | `050bf61` 2026-09-28 | `vite-node scripts/lump43-probe.ts <map.bsp>` | — | — |
| `makepack.ts` | vite-node | Generate a renderable geometry pack for one map. | `archived` | 无 | `b14f644` 2026-09-28 | **未声明** | — | — |
| `map-state-check.mjs` | node | P1-03: the map panel tells the truth about three separate things. | `archived` | 无 | `b14f644` 2026-09-28 | `node scripts/map-state-check.mjs <demo.dem> <map.bsp> <tf folder> [outdir]` | — | — |
| `material-check.ts` | vite-node | The acceptance check for the P1-06 asset layer. | `archived` | 无 | `050bf61` 2026-09-28 | `vite-node scripts/material-check.ts <map.bsp> <TF2 tf directory> <output.json>` | — | — |
| `material-mount-check.mjs` | node | P1-06 / P1-07 render-side acceptance: do the resolved materials reach the frame buffer? | `archived` | 无 | `050bf61` 2026-09-28 | **未声明** | — | — |
| `material-source-probe.ts` | vite-node | Why the mount check uploads 63 textures when the map table names 176 materials. | `archived` | 无 | `050bf61` 2026-09-28 | **未声明** | — | — |
| `model-orientation-probe.ts` | vite-node | Orientation and scale cross-check for real player models. | `archived` | 无 | `b11ae8e` 2026-09-27 | **未声明** | — | — |
| `mp4-browser-playback-probe.mjs` | node | Does the browser play back the MP4 the exporter writes — the whole file, both tracks — rather than merely accepting the container? | `archived` | 无 | `f52ff78` 2026-09-28 | `node scripts/mp4-browser-playback-probe.mjs <with-audio.mp4> <video-only.mp4> [outdir]` | — | — |
| `p109-codec-probe.mjs` | node | P1-09 decision input: can this Chromium actually encode H.264? | `archived` | 无 | `f52ff78` 2026-09-28 | `node scripts/p109-codec-probe.mjs` | — | — |
| `p110-audio-probe.mjs` | node | P1-10 decision input: which audio encoders does this Chromium actually have? | `archived` | 无 | `f52ff78` 2026-09-28 | `node scripts/p110-audio-probe.mjs` | — | — |
| `p111-tick-probe.mjs` | node | P1-11 second probe: does the exported picture depend on the requested tick? | `archived` | 无 | `050bf61` 2026-09-28 | `node scripts/p111-tick-probe.mjs <demo.dem> <bsp> [outdir]` | — | — |
| `packaged-launch-matrix.mjs` | node | Does the packaged Electron app stay alive, and what kills it? | `archived` | 无 | `6c20e94` 2026-09-30 | `TF2_PACKAGED_APP=<exe> node scripts/packaged-launch-matrix.mjs` | — | — |
| `playback-control-check.mjs` | node | P1-01: playback, pause, frame stepping, speed, reverse and random seeks. | `archived` | 无 | `f022405` 2026-09-30 | `node scripts/playback-control-check.mjs <demo.dem> <map.bsp> <tf folder> [outdir]` | — | — |
| `playback-delta-probe.ts` | vite-node | P1-01 evidence: does reverse traversal reconstruct the same state as forward play, on the real demo? | `archived` | 无 | `df58440` 2026-09-27 | `vite-node scripts/playback-delta-probe.ts <demo.dem> [out.json]` | — | — |
| `playback-state-probe.mjs` | node | P1-01 diagnostic: does the reconstructed state actually follow the tick? | `archived` | 无 | `df58440` 2026-09-27 | `node scripts/playback-state-probe.mjs <demo.dem> <tf folder> [out.json]` | — | — |
| `player-eye-check.ts` | vite-node | Does the first-person camera really sit at the height the demo carries? | `archived` | 无 | `553cec9` 2026-09-30 | `vite-node scripts/player-eye-check.ts <demo.dem> <tick,tick,...> [out.json]` | — | — |
| `png-write.ts` | vite-node | Minimal RGBA8 → PNG encoder for Node probes. | `archived` | 无 | `6c20e94` 2026-09-30 | **未声明** | — | — |
| `probe-install-layout.mjs` | node | Read-only one-off probe: what the real TF2 install on this machine looks like, so a negative control for the map-root detection can be chosen from rea | `archived` | 无 | `2616393` 2026-09-28 | **未声明** | — | — |
| `probe-wiring.mjs` | node | Confirm whether the addInitScript instrumentation actually reaches the bundle, and whether a second settings edit can start a refresh while the first  | `archived` | 无 | `9fb94b0` 2026-09-27 | **未声明** | — | — |
| `rig-anim-decode.ts` | vite-node | Decide where an animation descriptor's data really begins. | `archived` | 无 | `050bf61` 2026-09-28 | `vite-node scripts/rig-anim-decode.ts <TF2 tf directory> [model.mdl]` | — | — |
| `rig-anim-dump.ts` | vite-node | Byte-level dump of one animation's entry chain, to settle where the rot/pos valueptr tables and the channel blocks actually live. | `archived` | 无 | `050bf61` 2026-09-28 | `vite-node scripts/rig-anim-dump.ts <tf> <model.mdl> <animIndex> [bone]` | — | — |
| `rig-anim-gate.ts` | vite-node | Measure how many animations a decoder can actually account for. | `archived` | 无 | `050bf61` 2026-09-28 | `vite-node scripts/rig-anim-gate.ts <TF2 tf directory> [model.mdl]` | — | — |
| `rig-anim-motion-check.ts` | vite-node | Show that the animation decode produces motion, and that the motion is the file's rather than the program's. | `archived` | 无 | `050bf61` 2026-09-28 | `vite-node scripts/rig-anim-motion-check.ts <TF2 tf directory> [model.mdl] [animation name]` | — | — |
| `rig-anim-proof.ts` | vite-node | Decide the animation layout of a real TF2 MDL by criteria the data can fail. | `archived` | 无 | `050bf61` 2026-09-28 | `vite-node scripts/rig-anim-proof.ts <TF2 tf directory> [model.mdl]` | — | — |
| `rig-anim-scan.ts` | vite-node | Locate the animation data without assuming anything about payload sizes. | `archived` | 无 | `050bf61` 2026-09-28 | `vite-node scripts/rig-anim-scan.ts <TF2 tf directory> [model.mdl]` | — | — |
| `rig-anim-verify.ts` | vite-node | Decode a real animation and check it against numbers that are not in it. | `archived` | 无 | `050bf61` 2026-09-28 | `vite-node scripts/rig-anim-verify.ts <TF2 tf directory> [model.mdl] [descriptor...]` | — | — |
| `rig-animdesc-fields.ts` | vite-node | Dump mstudioanimdesc_t fields that change how the animation payload is reached: animblock / animindex / sectionindex / sectionframes and the zero-fram | `archived` | 无 | `050bf61` 2026-09-28 | `vite-node scripts/rig-animdesc-fields.ts <tf> [model.mdl] [anim...]` | — | — |
| `rig-chain-dump.ts` | vite-node | Dump one animation entry chain byte by byte. | `archived` | 无 | `050bf61` 2026-09-28 | `vite-node scripts/rig-chain-dump.ts <TF2 tf directory> <model.mdl> <start offset>` | — | — |
| `rig-chain-vs-bind.ts` | vite-node | Put the stored channel next to the bind row it is supposed to describe. | `archived` | 无 | `050bf61` 2026-09-28 | `vite-node scripts/rig-chain-vs-bind.ts <TF2 tf directory> [animation]` | — | — |
| `rig-entry-verdict.ts` | vite-node | Per-entry verdict for one animation: which channels decode, which do not. | `archived` | 无 | `050bf61` 2026-09-28 | `vite-node scripts/rig-entry-verdict.ts <TF2 tf directory> <model.mdl> <start> <numframes>` | — | — |
| `rig-euler-anchor-check.ts` | vite-node | Ask the animation payloads whether they reproduce a pose that is already known, instead of asking whether they "look plausible". | `archived` | 无 | `050bf61` 2026-09-28 | `vite-node scripts/rig-euler-anchor-check.ts <TF2 tf directory> [animation]` | — | — |
| `rig-header-probe.ts` | vite-node | Locate the attachment table in the MDL header, and cross-read two models' bone tables. | `archived` | 无 | `050bf61` 2026-09-28 | `vite-node scripts/rig-header-probe.ts <TF2 tf directory>` | — | — |
| `rig-hypothesis-search.ts` | vite-node | Search the payload conventions, and let the landmarks pick the winner. | `archived` | 无 | `050bf61` 2026-09-28 | `vite-node scripts/rig-hypothesis-search.ts <TF2 tf directory> [animation...]` | — | — |
| `rig-layout-probe.ts` | vite-node | Locate the animation-bearing structures inside a real TF2 MDL. | `archived` | 无 | `050bf61` 2026-09-28 | `vite-node scripts/rig-layout-probe.ts <TF2 tf directory> [model.mdl]` | — | — |
| `rig-layout-solve.ts` | vite-node | Solve the two remaining layout questions by search, scored on the data. | `archived` | 无 | `050bf61` 2026-09-28 | `vite-node scripts/rig-layout-solve.ts <TF2 tf directory> <model.mdl> <start> <numframes>` | — | — |
| `rig-mesh-verify.ts` | vite-node | Verify the settled decode against the one witness that no convention can move: the mesh. | `archived` | 无 | `050bf61` 2026-09-28 | `vite-node scripts/rig-mesh-verify.ts <TF2 tf directory> [limit] [--rootOrder=asis\|engine]` | — | — |
| `rig-pelvis-position-search.ts` | vite-node | Find the reading of a raw position channel, using only the pelvis. | `archived` | 无 | `050bf61` 2026-09-28 | `vite-node scripts/rig-pelvis-position-search.ts <TF2 tf directory> [animation...]` | — | — |
| `rig-pose-check.ts` | vite-node | Check the animation decoder against a pose the file states independently. | `archived` | 无 | `050bf61` 2026-09-28 | `vite-node scripts/rig-pose-check.ts <TF2 tf directory> [model.mdl]` | — | — |
| `rig-pose-trace.ts` | vite-node | Trace one bone's decode, byte by byte, so a wrong pose can be blamed on a specific read rather than on "the decoder". | `archived` | 无 | `050bf61` 2026-09-28 | `vite-node scripts/rig-pose-trace.ts <TF2 tf directory> <animation> [bone...]` | — | — |
| `rig-raw-dump.ts` | vite-node | Raw field dump for the anim-bearing tables, plus a header-free search for the animation block. | `archived` | 无 | `050bf61` 2026-09-28 | `vite-node scripts/rig-raw-dump.ts <TF2 tf directory> [model.mdl]` | — | — |
| `rig-rawpos-order-tally.ts` | vite-node | Measure the axis order of the raw position channel over the whole file. | `archived` | 无 | `050bf61` 2026-09-28 | `vite-node scripts/rig-rawpos-order-tally.ts <TF2 tf directory>` | — | — |
| `rig-rle-check.ts` | vite-node | Validate the compressed-channel layout across every animation in a real file. | `archived` | 无 | `050bf61` 2026-09-28 | `vite-node scripts/rig-rle-check.ts <TF2 tf directory> [model.mdl]` | — | — |
| `rig-rle-v2.ts` | vite-node | Decide the compressed-channel block reading by walking every channel in a real animations.mdl under two candidate readings and comparing them on one m | `archived` | 无 | `050bf61` 2026-09-28 | `vite-node scripts/rig-rle-v2.ts <TF2 tf directory> [model.mdl] [animIdx]` | — | — |
| `rig-rle-v3.ts` | vite-node | Validate the compressed-channel reading against the reference implementation. | `archived` | 无 | `050bf61` 2026-09-28 | `vite-node scripts/rig-rle-v3.ts <TF2 tf directory> [model.mdl]` | — | — |
| `sky-adjacency-probe.ts` | vite-node | Recover the horizontal adjacency of the four sky side faces, by matching edge profiles rather than by trusting a remembered face order. | `archived` | 无 | `6c20e94` 2026-09-30 | `vite-node scripts/sky-adjacency-probe.ts <tf-dir> [out.json]` | — | — |
| `sky-orientation-probe.ts` | vite-node | Pin the skybox V-axis convention with a hard geometric constraint. | `archived` | 无 | `6c20e94` 2026-09-30 | `vite-node scripts/sky-orientation-probe.ts <tf-dir> [out.json]` | — | — |
| `sky-survey-probe.ts` | vite-node | Survey every skybox set installed, to pin the vertical convention. | `archived` | 无 | `6c20e94` 2026-09-30 | `vite-node scripts/sky-survey-probe.ts <tf-dir> [out.json]` | — | — |
| `sky-top-probe.ts` | vite-node | Pin the rotation of the `up` and `dn` faces, using the same edge-profile match that fixed the side faces' adjacency. | `archived` | 无 | `6c20e94` 2026-09-30 | `vite-node scripts/sky-top-probe.ts <tf-dir> [out.json]` | — | — |
| `switch-blocked-probe.mjs` | node | Probe before asserting: two states the switch checks do not reach yet. | `archived` | 无 | `0b77368` 2026-09-28 | `node scripts/switch-blocked-probe.mjs <demoA.dem> <demoB.dem> <tf root> [outdir]` | — | — |
| `switch-concurrency-probe.mjs` | node | Probe before asserting: what can be observed *during* a load? | `archived` | 无 | `3bcbc35` 2026-09-28 | `node scripts/switch-concurrency-probe.mjs <demoA.dem> <demoB.dem> <tf root> [outdir]` | — | — |
| `timeline-size-probe.mjs` | node | Does the timeline canvas get a size, or does it stay collapsed? | `archived` | 无 | `2616393` 2026-09-28 | **未声明** | — | — |
| `toe-facing-probe.ts` | vite-node | Independent confirmation of which side of a player model is the front. | `archived` | 无 | `df58440` 2026-09-27 | **未声明** | — | — |
| `vvd-layout-probe.ts` | vite-node | — | `archived` | 无 | `b11ae8e` 2026-09-27 | **未声明** | — | — |
| `vvd-layout-solve.ts` | vite-node | Pin down the VVD vertex layout with a sharp metric. | `archived` | 无 | `b11ae8e` 2026-09-27 | **未声明** | — | — |
| `weapon-look-browser-check.ts` | vite-node | P1-05 at the render layer: does the running product actually draw the item's own model, and does it say so? | `archived` | 无 | `cb24358` 2026-09-28 | `vite-node scripts/weapon-look-browser-check.ts <demo.dem> <map.bsp> <tf dir> [outdir]` | — | — |
| `worldlight-probe.ts` | vite-node | What are the world lights in a BSP, and how wide is one record? | `archived` | 无 | `6c20e94` 2026-09-30 | `npx vite-node scripts/worldlight-probe.ts <map.bsp> [out.json]` | — | — |
| `zip-lzma-solve-probe.ts` | vite-node | Find the header split Valve uses for ZIP entries compressed with method 14. | `archived` | 无 | `050bf61` 2026-09-28 | `vite-node scripts/zip-lzma-solve-probe.ts <map.bsp>` | — | — |
| `character-asset-probe.ts` | vite-node | Two of the user's five complaints, answered with the reading layer only: weapon models and player materials (team skins). | `superseded` | 无 | `553cec9` 2026-09-30 | `npx vite-node scripts/character-asset-probe.ts <tfRoot> <out.json>` | — | — |
| `ui-features-check.mjs` | node | Acceptance for the three features added on `ui-features`: | `superseded` | 无 | `0b77368` 2026-09-28 | **未声明** | — | — |
