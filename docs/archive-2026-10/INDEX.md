# Archive: documents from the 2026-10 line

These 53 documents were **recovered from the seven `handoff-*` branches** of the old
history. They existed on those branches and never on the current line, so they would have
been destroyed with the branches; they are copied here byte for byte, and the branches
themselves are preserved in the all-refs bundle (see `docs/HANDOFF-EVIDENCE.md`).

Read them as **history, not as documentation of this tree**. They describe a stopping point
in the 2026-10-01..10-06 window, on a different lineage, with a different product shape
(audio was a first-class subsystem then; the maps were different; the gate chain did not
exist in its current form). Where they disagree with `docs/HANDOFF-STATE.md`, this tree is
right and the archive is the record of how it got here.

Why keep them at all: they are the only written account of several decisions that the
current tree depends on without explaining -- the VPK resource inventory, the audio
pipeline that was later dropped from the chain, the TempEffect work that predates the
animation gates, and the takeover notes that name who was doing what. A recipient asking
"why is there no audio gate?" finds the answer in `evidence/native-audio-*.md`.

`MANIFEST.tsv` in this directory records, for every file: the original path, the branch tip
it was taken from, that commit's date, the byte count and the sha256 of the archived copy.
Where the same document existed on several branches with different content, the copy is the
one whose last commit on any `handoff-*` branch was the newest.

## The ten narrative documents

| File | Bytes | What it is |
|---|---|---|
| [SKINNING_PLAN.md](SKINNING_PLAN.md) | 8295 | Oct: GPU skinning + animation plan for the native renderer (bone palette, matrix paths, cost). Predates the animation gates; the shipped renderer uses the same idea but not this code. |
| [HANDOFF-2026-09-29.md](HANDOFF-2026-09-29.md) | 136818 | 136 KB master handoff of the 09-29 round: task list, verification attempts, dead ends. The single largest narrative in the archive; the origin of the PASS/NOT VERIFIED/UNSUPPORTED habit. |
| [NATIVE-PRODUCT-CHECKLIST-2026-10-03.md](NATIVE-PRODUCT-CHECKLIST-2026-10-03.md) | 8079 | Product-versus-task checklist for the 10-03 state: which tasks were actually done versus only planned. |
| [PROBES.md](PROBES.md) | 48129 | 48 KB probe ledger: every probe, what it reads, what it proved, and which questions stayed open. The closest thing to a design document for native/tools/. |
| [TAKEOVER-2026-10-01.md](TAKEOVER-2026-10-01.md) | 18960 | Takeover brief at 2026-10-01 21:30: state, blockers, what the next person must not touch. |
| [TAKEOVER-2026-10-03.md](TAKEOVER-2026-10-03.md) | 32451 | 32 KB takeover at the 10-03 stopping point: the fuller of the two, with the tool and asset inventory. |
| [WORKLIST-2026-10-01.md](WORKLIST-2026-10-01.md) | 22759 | The 10-01 worklist as it stood, with per-item status and who owned it. |
| [architecture.md](architecture.md) | 1852 | Short statement of the native architecture and its extension conventions (1.8 KB). Still a fair summary of the layer split. |
| [desktop-acceptance.md](desktop-acceptance.md) | 783 | Desktop acceptance record, explicitly marked as historical. |
| [native/TAKEOVER-2026-10-05.md](native/TAKEOVER-2026-10-05.md) | 20046 | Takeover sent from inside native/: renderer and model-pipeline detail, the most technical of the takeover notes. |


## The 43 evidence notes

| File | Bytes | Title as written |
|---|---|---|

## What is *not* in here

- The eight non-`.md` files that also existed only on `handoff-*` branches: six probe
  sources under `native/tools/`, `native/include/model_instance_contract.h`, and
  `docs/native-world-texture-check.png`. They are superseded by the current probe set and
  the current header layout, and they remain only on the archive branch and in the bundle.
- The 62 root `T*-<date>.md` task cards of the current line. Those are on the source commit
  itself, so they are preserved by `archive/full-history-2026-11-13` with no extraction
  needed.
- Any image or data file the notes reference by absolute path on the authoring machine.
  Those paths (`D:/TF2_Demo_Player/...`, `D:/_shared/...`) no longer resolve; see
  `docs/HANDOFF-MISSING.md`.

## How to check this directory was not edited

```bash
cd docs/archive-2026-10
awk -F'\t' 'NR>1 {print $6"  "$2}' MANIFEST.tsv | sha256sum -c --quiet   # silent = all 53 match
```

| [evidence/assets-vpk-inventory-2026-10-03.md](evidence/assets-vpk-inventory-2026-10-03.md) | 3668 | TF2 本地资源盘点与 VPK 可查询能力（2026-10-03） |
| [evidence/audio-weapon-classification-2026-10-05.md](evidence/audio-weapon-classification-2026-10-05.md) | 1035 | Native weapon sound classification evidence (2026-10-05) |
| [evidence/cpu-particles-fallback-2026-10-04.md](evidence/cpu-particles-fallback-2026-10-04.md) | 876 | CPU 粒子 fallback 证据（2026-10-04） |
| [evidence/demo-runtime-stability-2026-10-04.md](evidence/demo-runtime-stability-2026-10-04.md) | 486 | Large demo runtime stability (2026-10-04) |
| [evidence/demo-stack-overflow-2026-10-04.md](evidence/demo-stack-overflow-2026-10-04.md) | 860 | Large demo scan crash evidence (2026-10-04) |
| [evidence/demo-stack-overflow-fix-2026-10-04.md](evidence/demo-stack-overflow-fix-2026-10-04.md) | 674 | Large demo stack overflow fix (2026-10-04) |
| [evidence/export-filesystem.md](evidence/export-filesystem.md) | 7768 | 导出落盘路径的证据（W7） |
| [evidence/item-mapping-probe-bcfa9d3-2026-10-05.md](evidence/item-mapping-probe-bcfa9d3-2026-10-05.md) | 1531 | Item mapping probe from native-mvp `bcfa9d3` (2026-10-05) |
| [evidence/item-pcf-schema-rerun-2026-10-04.md](evidence/item-pcf-schema-rerun-2026-10-04.md) | 1851 | Item/PaintKit and PCF schema rerun (2026-10-04) |
| [evidence/model-loader-2026-10-03.md](evidence/model-loader-2026-10-03.md) | 2185 | Model resource parser evidence — 2026-10-03 |
| [evidence/model-resource-gaps-2026-10-04.md](evidence/model-resource-gaps-2026-10-04.md) | 6602 | 模型挂点、饰品、bodygroup 与 ViewModel 缺口诊断（2026-10-04） |
| [evidence/native-73dem-runtime-diagnostic-2026-10-04.md](evidence/native-73dem-runtime-diagnostic-2026-10-04.md) | 1164 | Native 73.dem runtime diagnostic (2026-10-04) |
| [evidence/native-asset-reference-status-2026-10-04.md](evidence/native-asset-reference-status-2026-10-04.md) | 924 | Native asset reference status (2026-10-04) |
| [evidence/native-audio-acceptance-2026-10-04.md](evidence/native-audio-acceptance-2026-10-04.md) | 1191 | Native audio acceptance (2026-10-04) |
| [evidence/native-audio-demo-sound-classification-2026-10-06.md](evidence/native-audio-demo-sound-classification-2026-10-06.md) | 2232 | Native Demo sound classification attempt (2026-10-06) |
| [evidence/native-audio-playback-controls-2026-10-04.md](evidence/native-audio-playback-controls-2026-10-04.md) | 2027 | Native audio playback control evidence (2026-10-04) |
| [evidence/native-audio-real-data-rerun-2026-10-06.md](evidence/native-audio-real-data-rerun-2026-10-06.md) | 1725 | Native audio real-data rerun (2026-10-06) |
| [evidence/native-audio-release-rerun-2026-10-04.md](evidence/native-audio-release-rerun-2026-10-04.md) | 1127 | Native audio and release rerun (2026-10-04) |
| [evidence/native-audio-resource-rerun-2026-10-04.md](evidence/native-audio-resource-rerun-2026-10-04.md) | 1248 | Native sound index and WAV resource rerun (2026-10-04) |
| [evidence/native-audio-scheduler-boundary-rerun-2026-10-04.md](evidence/native-audio-scheduler-boundary-rerun-2026-10-04.md) | 1374 | Native audio scheduler boundary rerun (2026-10-04) |
| [evidence/native-audio-scheduler-counts-2026-10-04.md](evidence/native-audio-scheduler-counts-2026-10-04.md) | 764 | Native audio scheduler count evidence (2026-10-04) |
| [evidence/native-audio-scheduler-silent-rerun-2026-10-04.md](evidence/native-audio-scheduler-silent-rerun-2026-10-04.md) | 661 | Native audio scheduler silent rerun (2026-10-04) |
| [evidence/native-audio-vpk-classification-2026-10-06.md](evidence/native-audio-vpk-classification-2026-10-06.md) | 1567 | Native VPK sound classification (2026-10-06) |
| [evidence/native-audio-wiring-2026-10-04.md](evidence/native-audio-wiring-2026-10-04.md) | 2148 | Native audio wiring evidence (2026-10-04) |
| [evidence/native-entity-state-diagnostics-2026-10-04.md](evidence/native-entity-state-diagnostics-2026-10-04.md) | 1193 | Native entity state diagnostics |
| [evidence/native-install-smoke-2026-10-04.md](evidence/native-install-smoke-2026-10-04.md) | 856 | Native Release install smoke (2026-10-04) |
| [evidence/native-release-runtime-smoke-2026-10-04.md](evidence/native-release-runtime-smoke-2026-10-04.md) | 974 | Native Release runtime smoke (2026-10-04) |
| [evidence/native-release-stability-rerun-2026-10-04.md](evidence/native-release-stability-rerun-2026-10-04.md) | 1021 | Native release stability rerun (2026-10-04) |
| [evidence/native-winmm-sink-smoke-2026-10-04.md](evidence/native-winmm-sink-smoke-2026-10-04.md) | 2265 | Native WinMM sink smoke (2026-10-04) |
| [evidence/paint-kit-source-audit-2026-10-04.md](evidence/paint-kit-source-audit-2026-10-04.md) | 3271 | Paint Kit 来源三层审计（2026-10-04） |
| [evidence/paint-kit-source-status-2026-10-04.md](evidence/paint-kit-source-status-2026-10-04.md) | 3001 | Paint Kit 来源状态（2026-10-04） |
| [evidence/pcf-capability-2026-10-04.md](evidence/pcf-capability-2026-10-04.md) | 1391 | PCF 能力状态（2026-10-04） |
| [evidence/pose-review.md](evidence/pose-review.md) | 14631 | 角色动画逐姿态复核的证据（W3） |
| [evidence/pov-candidates.md](evidence/pov-candidates.md) | 11469 | 真实 POV Demo 候选表 |
| [evidence/protocol-instancebaseline-2026-10-03.md](evidence/protocol-instancebaseline-2026-10-03.md) | 1750 | Protocol checkpoint: instancebaseline class mapping (2026-10-03) |
| [evidence/render-2026-10-03.md](evidence/render-2026-10-03.md) | 1335 | Render evidence — 2026-10-03 |
| [evidence/renderer-temp-effects-2026-10-04.md](evidence/renderer-temp-effects-2026-10-04.md) | 1294 | Renderer TempEffect 可见标记证据（2026-10-04） |
| [evidence/schema-mapping.md](evidence/schema-mapping.md) | 1347 | Item schema mapping probe |
| [evidence/temp-effect-explosion-2026-10-04.md](evidence/temp-effect-explosion-2026-10-04.md) | 1076 | CTEExplosion 到 TempEffectTimeline 证据（2026-10-04） |
| [evidence/temp-effect-firebullets-2026-10-04.md](evidence/temp-effect-firebullets-2026-10-04.md) | 1048 | CTEFireBullets 到 TempEffectTimeline 证据（2026-10-04） |
| [evidence/upward-f12-acceptance.md](evidence/upward-f12-acceptance.md) | 1497 | `pl_upward_f12` 地图与 HUD 验收 |
| [evidence/vpd-container-boundary-2026-10-04.md](evidence/vpd-container-boundary-2026-10-04.md) | 2208 | VPD container boundary probe |
| [evidence/vpd-wire-failure-6a71871-2026-10-05.md](evidence/vpd-wire-failure-6a71871-2026-10-05.md) | 1143 | VPD wire failure diagnostic (2026-10-05) |
