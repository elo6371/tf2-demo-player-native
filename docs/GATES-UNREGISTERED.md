# Gate registry: named exclusions

Every `*-check.sh` in this tree must be reachable from one of the three chain
entry points, or be listed here with a reason. The rule is enforced by
`gate-registry-check.sh`, which runs in `verify-fast.sh` and has a self-test
that deliberately omits a gate.

The three entry points are:

- `verify-fast.sh` -- compile + probe self-tests + this registry check.
- `verify-all.sh` -- the 30-step P0 evidence chain.
- `verify-release.sh` -- the 15-step release chain (which includes `verify-all`).

A gate listed here is **not** claimed to pass. It is a named exclusion from the
daily chains: the class says why, and the last column says when the owner runs
it. "Registered" and "excluded" are different states; do not copy a PASS from an
excluded gate into release evidence.

Classes:

- **segmented** -- a domain gate run in that domain's segmented regression. It
  needs a fixture, a map matrix, or a product state that the daily chain does
  not carry.
- **audible** -- opens a real audio device or takes the desktop; must not run
  unattended/headless.
- **external** -- needs a runtime, package, network service or installed tree
  that is not part of the source checkout.
- **tooling** -- a compile/syntax or harness aid, not a behaviour gate.
- **historical** -- an older gate whose scene is now asserted by a registered
  gate; kept for provenance and forensic re-runs.
- **blocked** -- the product/feature or its pins are not ready; the gate must
  not be read as release evidence until the named task lands.
- **out-of-scope** -- the measured feature is explicitly outside the
  first-release target (lighting/shadow parity, general particles, reflections).

| Gate | Class | Why it is not in a chain | When it runs / owner |
|---|---|---|---|
| `animation-drive-states-check.sh` | segmented | T84 crouch/flinch state gate; needs the T84 fixture and a player-state tick, not a general build/smoke run. | P4 animation batch; character/weapon owner. |
| `attach-weapon-budget-check.sh` | segmented | P0 OOM/budget gate for three named weapon models; its fixture is the Highlander STV demo, not the chain's pinned scene. | R4-4 budget acceptance; character/weapon owner. |
| `bone-mismatch-semantics-check.sh` | segmented | Pins what `boneMismatch` counts; it is a semantics diagnostic, not a release pass/fail. | P4 animation batch; character/weapon owner. |
| `camera-ui-check.sh` | segmented | Camera/UI contract gate; overlaps the registered `hud-live-check.sh` and needs the camera script fixtures. | Camera/UI changes; UI owner. |
| `class-pose-family-check.sh` | segmented | Measures crouch/flinch sequence data that the runtime does not yet select; it would report a product gap, not a regression. | P4 data-decode batch; character/weapon owner. |
| `demo-clock-check.sh` | segmented | Pins the two demo tick spaces (signon vs data entry). It is directly relevant to I-31 and should run whenever a clock/origin field changes. | Clock-origin changes and the F1-1 follow-up; QA/playback owner. |
| `fog-check.sh` | segmented | T101 fog controller gate; fog is map material state, not a daily chain fixture. | Map material acceptance; map renderer owner. |
| `fullscreen-quiet-check.sh` | audible | Proves the capture path does not take the desktop from a fullscreen app; needs an interactive desktop and a fullscreen control. | Manual desktop run; QA/gate owner. |
| `game-event-check.sh` | segmented | T68 game-event decode gate with two named demos and oracle rows; a domain fixture, not a smoke test. | P3/P4 event work; playback owner. |
| `hud-check.sh` | segmented | T51 custom-HUD import/refusal/layout gate; the registered `hud-live-check.sh` covers the live product path. | HUD importer changes; UI owner. |
| `hud-text-check.sh` | segmented | T67 text raster table gate; the registered `hud-live-check.sh` covers live text, this pins the exact glyph table. | Font/text changes; UI owner. |
| `install-lifecycle-check.sh` | segmented | Drives a staged package through install/uninstall in a clean directory; needs a package staging step. | Release packaging; install owner. |
| `install-readiness-check.sh` | segmented | Fresh-user resource discovery on an installed tree; needs `cmake --install` output, not the developer tree. | Release packaging (F4); install owner. |
| `instancebaseline-budget-check.sh` | segmented | P0 retention-cap gate; static probe over committed fixtures. It is a candidate for the fast tier but is not wired there yet. | Parser/budget batch; QA/parser owner. |
| `lighting-dark-account-check.sh` | out-of-scope | Attributes every dark pixel of a reference frame; lighting/shadow parity is explicitly deferred by the roadmap. | Forensic only; map renderer owner. |
| `lighting-semantics-check.sh` | out-of-scope | The six lighting/shader controls are deferred with lighting parity; running it would imply a parity claim the project does not make. | Forensic only; map renderer owner. |
| `logstf-check.sh` | external | Needs the WebView2 runtime and a logs.tf URL; it is not a first-release HUD requirement and cannot run offline. | Manual logs-panel check; UI owner. |
| `logstf-syntax-check.sh` | tooling | `cl /Zs` syntax-only check of T54 translation units; it builds no product behaviour. | Before a WebView2 build; UI owner. |
| `map-geometry-lighting-check.sh` | segmented | Per-map geometry/lighting matrix; the matrix is a map-acceptance input, not a daily chain step. | R2-1/R2-4 map acceptance; map renderer owner. |
| `minimap-check.sh` | blocked | Pure-logic minimap gate; the product minimap is not integrated yet (F2-1), so this gate cannot stand in for product support. | F2-1 minimap landing; UI owner. |
| `playback-control-check.sh` | segmented | T70 transport landing gate; `playback-error-paths-check.sh` and `timeline-check.sh` are the registered release transport gates. | Transport changes; playback owner. |
| `player-model-check.sh` | segmented | T60 player-model gate; needs the nine-class fixture and produces P4 evidence, not a general smoke result. | P4 model batch; character/weapon owner. |
| `quality-audit-check.sh` | segmented | Footprint/steady-state measurements; useful at performance milestones, too noisy for every chain. | Performance milestones; architecture owner. |
| `quiet-capture-check.sh` | audible | I-29: the live half opens a real audio device without `--audio-disabled`, so it cannot run headless/unattended. | After F1-4 makes it headless-safe; audio/UI owner. |
| `resource-discovery-check.sh` | historical | T9 discovery gate; `resource-reachability-check.sh` is registered and covers the current store order. | Forensic only; map/asset owner. |
| `stability-release-check.sh` | segmented | Judges the 19-case stability matrix; the matrix itself is a long isolated sweep, not a daily gate. | Stability sweep; QA owner. |
| `terrain-blend-check.sh` | segmented | Pins the reference terrain's WorldVertexTransition chain and pixels; map-specific evidence. | R2-4 material acceptance; map renderer owner. |
| `trajectory-overlay-check.sh` | out-of-scope | Projectile trajectory overlay is a historical feature request, not in the first-release target. | Only if the product owner revives it. |
| `userinfo-check.sh` | segmented | Player-identity decoder gate; player-name semantics (I-16) are still open, so this is P3 evidence. | P3 name work; UI/playback owner. |
| `viewmodel-apply-check.sh` | segmented | T18 applied-pose pixel gate; part of the P4 ViewModel chain. | P4 ViewModel changes; character/weapon owner. |
| `viewmodel-contract-check.sh` | segmented | First-person projection/attachment/hand-mirror contract; P4 domain gate. | P4 ViewModel changes; character/weapon owner. |
| `viewmodel-material-check.sh` | segmented | ViewModel material slot -> VMT -> VTF chain; P4 domain gate. | P4 material changes; character/weapon owner. |
| `viewmodel-presentation-check.sh` | segmented | T22 hand-on-weapon composite gate; P4 domain gate. | P4 ViewModel changes; character/weapon owner. |
| `viewmodel-range-check.sh` | segmented | T13 arms range/material binding gate; P4 domain gate. | P4 ViewModel changes; character/weapon owner. |
| `viewmodel-robustness-check.sh` | segmented | T30 multi-range/LOD/damaged-companion gate; P4 domain gate. | P4 model-loader changes; character/weapon owner. |
| `visual-baseline-check.sh` | historical | T10/T11 visual baseline; its reference scene is now covered by the registered world-geometry and material gates. | Forensic only; map renderer owner. |
| `vmt-nesting-check.sh` | segmented | P0 VMT recursion bound; a static probe over fixtures and a candidate for the fast tier, but not wired there yet. | Parser/budget batch; parser owner. |
| `vpk-build-check.sh` | segmented | T15 VPK/build reliability and malformed-archive refusals; run when the archive reader changes. | Archive changes; asset/build owner. |
| `weapon-body-check.sh` | segmented | Static weapon-body (second draw request) gate; P4 domain gate. | P4 weapon changes; character/weapon owner. |
| `weapon-material-check.sh` | segmented | Weapon-body material/range gate; P4 domain gate. | P4 weapon material changes; character/weapon owner. |
| `webview2-loader-check.sh` | external | Proves the WebView2 loader is resolved from the executable directory; needs the shipped runtime/package. | Packaging and loader changes; UI owner. |
| `world-material-check.sh` | segmented | Pins six world-material capability states/reasons; run when a material feature changes. | Map material changes; map renderer owner. |
| `world-material-effects-check.sh` | segmented | Water/viewmodel advanced-material effects; water reflection is out of scope and the rest is P4. | Map/P4 material changes; map renderer owner. |
| `world-material-face-check.sh` | segmented | World-material pixel chain at a named face; map acceptance evidence, not a daily smoke. | R2-4 map acceptance; map renderer owner. |
| `world-material-semantics-check.sh` | blocked | I-24 pin drift: it is red until the six material-coverage readings are re-pinned with a causal explanation (F1-3). | After F1-3; map/QA owner. |
| `world-near-clip-check.sh` | segmented | T29 near-clip units gate; run when world projection/camera units change. | World camera changes; map renderer owner. |
| `world-render-reliability-check.sh` | segmented | T8 device-error/reliability gate; run when the renderer's failure path changes. | Renderer reliability changes; map renderer owner. |
