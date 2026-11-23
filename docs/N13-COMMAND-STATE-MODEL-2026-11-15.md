# N13 — 跟随目标身份与命令状态模型（施工规格）

日期：2026-11-15 · 仓库 `D:/TF2_Native_Worktrees/t105-e5` @ `68105a8` · 设计者：`n13design`（只读分析，未构建、未运行）
源文件：`D:/_shared/EXEC-2026-11-15/LEAD/n13-command-state-model.md`（328 行，自 §1 起逐字收录）
任务卡：`docs/PROJECT-NEXT-TASKS-2026-11-14.md:267-273`（N13）；相关：`docs/N13-IDENTITY-HUD-UI-2026-11-15.md`、`docs/N16-SPLITVIEW-CONTRACT-2026-11-16.md`

## §0 Lead 裁决（2026-11-15 00:20）

**设计通过，按 §6「最小第一步」开工**，但两处口径先钉住：

1. **第一步是纯增量**：只加 `native/include/target_identity.h`（新增、header-only、不动 CMake）、`native/src/main.cpp` 里的状态/命令/一行 trace/退出计数器；**不许碰** `haveObserverView`（`:10610-10615`）、`resolvePovEye`（`:10648-10650`）、`applyCameraPovEye`（`:705-791`）、`placeSpectatorView`（`:1006-1161`），也不许改 `scene: eye` / `scene: eye row` / `scene: camera mode=` / `spectate: camera relay target=` 任何一行的格式或取值。第一道验收闸是设计 §5.2 的 **A8**：不带 follow 命令的运行，既有行逐字节不变、`demo_open_probe --scan` 两个 sha 不变（N12 acceptance §17.4 值）。
2. **相机优先级（§2.3：pinned follow 压过 demo eye）留到第二步，需单独裁决**。所以第一步交付后，**POV demo 里选人不会让画面真的跟着走**（会诚实打印 `cameraEffect=pov-eye-wins`），报告与文档里必须照这个口径写，不许当"已实现跟随"上报。

我给实现者加的三条硬要求：
- `follow:` 行必须自带 `tick=`（=`g_sceneResolvedServerTick`，无则 -1），能与同帧 `scene: eye row ... reqTick=` 配对；**名字不许自己解析**，复用 `hudLiveResolveName` 的 token 与 `nameKnown`。
- `--follow-target` **不许**写 `g_spectateManualTarget`（一次消费语义可照抄，变量必须分开），否则一个变量两个写者。
- 计数器只追加、不改既有退出行形状；follow 字段全部落在新行。

## §0.1 我核过的锚点（HEAD `68105a8`，逐个 grep 到行）

| 设计里的引用 | 实测 |
| --- | --- |
| `handleCameraAction` `main.cpp:199` | ✅ `bool handleCameraAction(const std::string& action, std::string& reason) {` |
| `--spectate-target` 解析 `main.cpp:3336-3348` | ✅ `3342` 判 `parsed >= 0 && parsed <= 2047`；`3345`/`3348` 两处 `++g_spectateArgumentRejected` |
| `spectateSlots` 构建 `main.cpp:11040-11121` | ✅ `11046 spectateSlots.clear();` |
| 一次性选人 `main.cpp:12978-12981` | ✅ `if (g_spectateManualTarget >= 0) { … pickManual … ; = -1; }` |
| `g_spectateDirector.update` `main.cpp:12987` | ✅ |
| 退出计数器 `main.cpp:13453` | ✅ `g_pauseKey.toggles, g_cameraFollows, g_cameraHoldsNoTarget,` |

未核：`main_hud.cpp:730`/`:742` 两个空 reason 早退（第一步不动 HUD，留给后续）。

---
## §1 Current facts (HEAD 68105a8)

### 1.1 Who decides the camera target, per scene tick

| Quantity | Produced at | Token | Notes |
|---|---|---|---|
| demo SetView target | `demo_header_net.cpp` `:647` area | `summary.lastViewEntity` (uint32, `demo_header.h:653`), `summary.setViewCount` (`:652`) | 0 when no svc_SetView was seen; `setViewCount==0` is the only 'unknown' marker |
| observer view (eye + angles) | `findObserverViewAtOrBeforeTick`, consumed `main.cpp:10610-10615` | `observerView` | `haveObserverView` = found && hasOrigin && hasAngles && `observerEyeUsable(origin)` (T80: eye (0,0,0) is not a camera) |
| POV eye (with camera-track backfill) | `resolvePovEye(...)` `main.cpp:10648-10650` | `g_scenePovEye` (`main.cpp:864`) | takes `summary.lastViewEntity`, `setViewCount>0`, `sceneTickKnown?sceneTick:-1` |
| camera-track row | `lookupCameraEyeRow` `main.cpp:582-630`; consumed by `applyCameraPovEye` `:705-791` and `placeSpectatorView` `:1006-1161` | `CameraEyeRow` `:571-580` | row rejected when `sample.entityIndex != viewEntity` -> reason `identity-mismatch` |
| follow/relay target | `SpectateDirector` (`native/include/spectate_target.h:77-195`), updated at `main.cpp:12987` | `g_spectateDirector` (`main.cpp:1167`), `targetIndex()`, `source()` | one target per process today; N16 turns it into one per pane |
| manual pick request | `--spectate-target` latch `main.cpp:1168`, consumed `:12978-12981` | `g_spectateManualTarget` | applied via `SpectateDirector::pickManual` (`spectate_target.cpp:297-300`) |
| relay placement (what the frame is drawn with) | `placeSpectatorView` `main.cpp:1006-1161`, called `:13015-13018` | `relayEye`, `relayAngles`, `g_spectateCameraHiddenEntity` (`:475`) | only when `!g_debugCamera && !g_cameraDemoViewUsable && mode==FirstPerson` (`:13008-13009`) |
| camera mode decision | `decideCameraSceneAction` (`native_ui.cpp:551-558`), called `main.cpp:10749` | `g_cameraMode` (`CameraMode` `native/include/native_ui.h:40`) | `relayWillPlace` at `:10745-10748` defers the hold for the relay path |

Reading, in the order the frame does it: demo SetView -> observer view -> POV eye -> camera mode
decision -> (relay) -> director target. The two halves never talk: the demo half never asks the
director, the director never asks the demo half.

### 1.2 Where state lives

| State | Where | Reset |
|---|---|---|
| camera counters `g_cameraFollows` / `g_cameraHoldsNoTarget` / `g_cameraFreezeFrames` | `main.cpp:430-432` | never per frame; process totals, printed in the exit summary `:13453-13454` |
| applied mode latch `g_frameAppliedCameraMode` | `main.cpp:441` | updated when the pending mode differs (`:10494-10501`) |
| demo-view usability `g_cameraDemoViewUsable` | `main.cpp:447` | every scene tick (`:10615`) |
| relay eye print latch `g_spectateCameraEyePrinted` / `g_spectateCameraLastEye` | `main.cpp:469` / `:468` | process-wide; reprints on >1024 sq move (`:1107-1111`) |
| hidden relay entity `g_spectateCameraHiddenEntity` | `main.cpp:475` | set on placement, `-1` on failure (`:13022`) |
| resolved snapshot tick `g_sceneResolvedServerTick` | `main.cpp:869` | published only for Available/Checkpoint snapshots (`:10594-10597`) |
| director latch `g_spectateManualTarget` | `main.cpp:1168` | cleared by the frame that applies it (`:12980`) |
| CLI rejection counter `g_spectateArgumentRejected` | `main.cpp:1169` | process total |
| director readings (`target_`, `source_`, transitions, counters) | `spectate_target.h:126-150` | per `update()` call; `refreshDead` deliberately does not move them (`:108-113`) |

### 1.3 Where it is printed (and at what quantum)

| Line | Site | Quantum |
|---|---|---|
| `scene: begin tick=%d sceneTick=%d` | `main.cpp:10546` | per frame |
| `scene: clock tick=.. entryTick=.. serverTick=..` | `:10549` / `:10553` | per frame |
| `scene: snapshot status=%d stale=%d states=%zu` | `:10580` | per frame |
| `scene: eye origin=.... viewEntity=%d ... reason=%s` | `:10666` | change-driven (frozen) |
| `scene: eye row src=%s reqTick=%d rowTick=%d ... miss=%s` | `:10696` | change-driven |
| `scene: camera mode=%s target=%d follows=%u holds=%u freeze=%u ... src=%s` | `:13054-13059` | per frame; **`target=` is `haveObserverView` (a bool), not an entity** |
| `spectate: camera relay target=%d eye=... offsetSrc=%s offsetTick=%d ...` | `:1127` | first placement or >1024 sq move |
| `spectate: tick=%d dead=%s corpses=%d` + `traceFields()` | `:13068-13081`; fields `spectate_target.cpp:355-366` | per scene tick change |
| `hud: name nameEntity=%d ... nameReason=%s nameSlug=%s ...` | `main_hud.cpp:1196-1200` | change-driven on (entity, slot, known, fallback) (`:1188-1190`) |
| exit summary counters | `main.cpp:13453-13461` | once |

### 1.4 Commands that exist today

| Entry | Where | Can express | Cannot express |
|---|---|---|---|
| `--spectate-target <int>` | `main.cpp:3336-3348` (guard 0..2047, else `g_spectateArgumentRejected`) | one manual pick at a known entity index | no reason on rejection; no serial; no unfollow |
| `--spectate-pause-at` / `--spectate-pause-seconds` / `--spectate-show-corpses` | `:3349-3371` | gate-only behaviours | not user commands |
| F9 `camera.freeFly` / F10 `camera.resetToPlayer` / F11→Home `camera.resetView`（I2 2026-11-23 起 F11 = 全屏） | `native/src/keybinds.cpp:392-403` -> `handleCameraAction` `main.cpp:199-246` | mode toggle, 'back to the player', view reset | no notion of a followed player |
| UI buttons | `main.cpp:1598-1606` (ids `kUiFreeFly` / `kUiResetToPlayer` / `kUiResetView`) | same as keys | same |
| `--camera-script` step driver | `main.cpp:13145-13150`, `native/src/camera_script.cpp` | scripted frames for gates | not a selection API |

Gap: no command anywhere names a *player to follow*, and no command returns *why* a pick was
refused. `--spectate-target 5` on a dead player and `--spectate-target 99999` are both silent
apart from a single `g_spectateArgumentRejected` bump for the latter.

### 1.5 Inherited defects to fix, not to copy

1. `hudLiveResolveName` early-returns leave `name.reason` empty at `main_hud.cpp:730` (`viewEntity<1`) and `:742` (no userinfo slot). A trace line then prints `nameReason=` followed by nothing, which reads like a bug in the reader. New code must not print a name through a path that can leave the reason empty; the two sites should become `no-view-entity` and `no-userinfo-slot`.
2. `SpectateDirector::pickManual` counts `manualRejected_` (`spectate_target.cpp:147-181`) but records no reason. 'not observable', 'dead', 'no such slot' and 'out of range' collapse into one counter, so a gate cannot tell a refusal from a typo in the test.
3. `scene: camera mode=... target=%d` (`main.cpp:13054-13059`) prints `haveObserverView`. Any new assertion that reads `target=` as an entity is wrong. The field is kept byte-identical (guardrail) and the new line must carry its own `target=`.

---

## §2 Identity model

### 2.1 Three identities, three producers, no shared field

| Identity | Sole producer | Token | Name source |
|---|---|---|---|
| **Broadcaster** (who recorded the demo) | POV: the recorder entity = `summary.lastViewEntity` with a non-HLTV userinfo entry. STV: the STV client's own userinfo entry (parsed `isHltv`, `demo_header_strtab.cpp:247`/`:265`); if absent, the identity is 'server' and has **no name** | `broadcasterEntity` / `broadcasterKind` {`pov-recorder`, `stv-client`, `server`} | `DemoUserInfoEntry` entry for that entity (`demo_header.h:603-631`) |
| **POV subject** (whose view the demo records) | `summary.lastViewEntity` at the current scene tick, gated by `setViewCount>0` | `povEntity` | same entry |
| **Followed** (what the user selected) | a user command only — nothing else may write it | `TargetIdentity` (§4) | same entry, via `hudLiveResolveName` |

Rule 1: a name printed in one identity's field is never taken from another identity's entity.
The existing HUD code does exactly the mix this rule forbids — `main_hud.cpp:1166-1179` asks the
relay entity first and falls back to `lastViewEntity`, printing `nameEntity=` but no
`nameSource=` — which is legal today only because there is one name field and one panel. New
lines carry `nameSource` {`followed`, `pov`, `broadcaster`} beside every name.

Rule 2: unknown is printable and never fabricated. `hudLiveProjectName` already folds
non-printable bytes to ``?`` and an empty name keeps `known=0` (`main_hud.cpp:694-717`). Display
for unknown stays ``?`` (the title bar's own marker, `main.cpp:6396-6399`); the trace says
`nameKnown=0 nameReason=<token>`. Never substitute the entity index, the previous name, or the
userid for a missing name.

### 2.2 Follow target states (one enum, printed verbatim)

| State | Evidence | Camera | Printed once per change |
|---|---|---|---|
| `ok` | slot present, serial matches, observable, alive | may follow | `state=ok` |
| `unset` | `valid==false` | demo/POV path | `state=unset` |
| `gone-index-out-of-range` | `entityIndex<0 || >2047` | hold last pose | `state=gone reason=index-out-of-range` |
| `gone-no-slot` | the snapshot that resolves the camera has no such slot | hold last pose | `state=gone reason=no-slot` |
| `gone-index-reused` | slot serial != stored serial | hold last pose, clear pin | `state=gone reason=index-reused` |
| `gone-not-observable` | `slotObservable()==false` (`spectate_target.h:71-72`) | hold last pose | `state=gone reason=not-observable` |
| `dead` | `lifeState!=0 || health<=0` (`spectate_target.cpp:277-296`) | pinned: stop on corpse, no handover; unpinned: handover | `state=dead pinned=%d` then `reason=pinned-dead` |
| `observer` | userinfo `isHltv` for that entity | refuse the selection | `state=observer` |
| `no-snapshot` | snapshot not Available/Checkpoint (`main.cpp:12982-12984`) | not evidence: hold, do not clear | `state=no-snapshot` |

The 'hold last pose' column is a hard requirement from `spectate_target.h:5-37` clause 5: none of
these states may move the camera to the world origin, to the shipped orbit, or to a random
player. A vanished target is a camera that stops, not a camera that guesses.

### 2.3 Precedence (who the frame obeys)

1. pinned follow (`source==Manual && pinned`) — user intent, never silently overridden by a
   handover; dies with the target (§2.2 `pinned-dead`) and is cleared only by unfollow/backToPov/Delete.
2. director (`source==Director` / `StableOrder`) — automatic handover, unchanged behaviour.
3. demo eye (`SetDemoView` / `SetOrbitFocus`, `main.cpp:10749-10776`) — when no follow is pinned.

Today (1) does not exist as a precedence: the demo eye wins whenever `haveObserverView` is true
(`:13008-13009`), so in a POV demo a manual pick can never move the camera off the recorder's
eye. Step 1 records that honestly (`cameraEffect=pov-eye-wins`); step 2 changes it under a
separate ruling and a separate gate (§6.3).

---

## §3 Command set

Every command returns `bool ok` + `std::string reason` (the `handleCameraAction` convention,
`main.cpp:199-246`), prints one `follow:` line on change, and bumps exactly one counter.

| Command | Arg | Semantics | Idempotency | Failure returns |
|---|---|---|---|---|
| `follow.select` | `entityIndex` 0..2047 | pin that entity, `source=Manual`, `pinned=true`; takes effect on the next scene tick | selecting the same entity again is a no-op success (`reason=already-following`, no counter move, no line) | `index-out-of-range`, `no-slot`, `not-observable`, `is-observer`, `dead` |
| `follow.next` | — | pin the next observable living player in ascending entity index after the current target, wrapping (same order as `pickStable`, `spectate_target.cpp:334-353`) | deterministic: same inputs -> same choice | `no-candidates` |
| `follow.prev` | — | mirror of `next` | deterministic | `no-candidates` |
| `follow.unfollow` | — | clear pin, `valid=false`; camera returns to director/demo eye path | unfollow when not following = no-op success (`reason=not-following`) | — |
| `follow.backToPov` | — | clear pin + `FirstPerson` + `setFreeFly(false)` + `cameraDirector.requestResetToPlayer()` (`camera_director.h:94-98`) so the next scene tick with an observe target adopts the demo eye | repeated calls collapse into one pending reset | `no-pov-eye` when no demo eye exists: camera holds, orbit is not entered |

Per-command behaviour matrix (all four cases are mandatory for each row):

| Case | select | next/prev | unfollow | backToPov |
|---|---|---|---|---|
| target missing from slots | reject `no-slot`; state unchanged | skip it; `no-candidates` if none | succeeds (nothing to clear) | succeeds; `no-pov-eye` if no eye |
| target is the observer/HLTV entity | reject `is-observer` — **except** the POV recorder, which is the observer in its own demo and must be selectable | skip HLTV entries | succeeds | succeeds |
| target is dead | reject `dead` (a pick cannot resurrect); a target that dies *after* pinning stays pinned and prints `pinned-dead` | skip dead players | succeeds | succeeds |
| demo does not support it | accepted for identity/trace; camera prints `cameraEffect=pov-eye-wins` (step 1) / `no-camera-data` (step 2) when the followed entity's eye is not recorded — never build an eye out of a non-recorder's origin | same | succeeds | succeeds |

Entry points, in the order they should be added:

1. Step 1 (no UI): `--follow-target <int>` on the command line, guarded exactly like `--spectate-target`
   (`main.cpp:3336-3348`: parse, reject <0 or >2047 by bumping `g_followArgumentRejected`), plus an
   internal `handleFollowAction(const std::string& action, std::int32_t entity, std::string& reason)`
   beside `handleCameraAction` (`main.cpp:199`). A gate can drive both through the trace.
2. Step 2: `kUiFollowNext` / `kUiFollowPrev` / `kUiFollowUnfollow` / `kUiBackToPov` in the
   `WM_COMMAND` camera-button block (`main.cpp:1598-1606`) and `camera.followNext` /
   `camera.followPrev` / `camera.unfollow` / `camera.backToPov` in `camera_actions`
   (`native/include/keybinds.h:155-163`, defaults `native/src/keybinds.cpp:392-403`).

Note: the `--follow-target` value must **not** be fed to `g_spectateManualTarget`. That latch is
the director's own pending pick (`main.cpp:12978-12981`); sharing it would make two owners of
one variable. `handleFollowAction` writes the new identity, and the frame passes the identity to
`pickManual` exactly once (same one-shot rule).

---

## §4 Single-source state model

One struct, one header, no new translation unit (so no CMake edit):
`native/include/target_identity.h`, header-only.

| Field | Type | Meaning |
|---|---|---|
| `entityIndex` | `std::int32_t` = -1 | same range as `--spectate-target` and `SpectateSlot::entityIndex` |
| `serial` | `std::uint32_t` = 0 | `EntityHandle::serial` (`handle >> 11`, `spectate_target.h:19-34`) at the moment of selection; 0 = unknown |
| `source` | `SpectateSource` = `None` | `Manual` when pinned; `Director` / `StableOrder` when adopted; `None` when unset |
| `pinned` | `bool` = false | only an explicit user pick sets it; only unfollow/backToPov/Delete clears it |
| `valid` | `bool` = false | true only after reconciliation against the current snapshot |

These five fields are exactly N16's `PaneTarget` (`docs/N16-SPLITVIEW-CONTRACT-2026-11-16.md` §1.2).
N16 must consume this header — `using PaneTarget = tf2::native::TargetIdentity;` — not restate it.

Owner: `main.cpp` (step 1: `g_followTarget`; N16 step: one per `PaneState`). Readers: the follow
trace, the HUD name path, and the commands. Nothing else writes it.

Division of responsibility, stated because two objects touch the same entity:
- `TargetIdentity` = **user intent** (who the user asked for). Never advances on its own.
- `SpectateDirector` = **camera realization** (who the camera actually follows, after handover,
  death and seek rules). It keeps its own state and counters; the identity is not a copy of them.
- Invariant, printed every change and asserted by the gate: if `pinned` and `valid` and the
  director accepted the pick, then `director.targetIndex()==entityIndex` and
  `director.source()==Manual`. If the director rejected it, `valid=false` with the director's
  reason — never two disagreeing 'current target' readings.

Tick alignment with N12-C (must be stated in code comments, not just here):
- identity is reconciled against the snapshot the camera uses: `spectateSlots` built at
  `main.cpp:11040-11121` from `currentEntityStates` at `g_sceneResolvedServerTick`, the tick
  `placeSpectatorView` is asked at (`:13015-13018`). Therefore `follow: ... tick=` prints
  `g_sceneResolvedServerTick` (or -1), and a gate can pair it with `scene: eye row ... reqTick=`.
- the *name* is resolved at the playback tick instead (`main_hud.cpp:1166-1167` ->
  `hudLiveResolveName`). Two different ticks, two different fields, never one field meaning both.

---

## §5 Observable output and acceptance

### 5.1 New line (one, change-driven, fixed field order)

`follow: target=%d serial=%u src=%s pinned=%d valid=%d state=%s reason=%s nameKnown=%d nameSource=%s nameReason=%s name="%s" tick=%d`

- printed when the tuple (target, serial, src, pinned, valid, state, nameKnown) changes, and on
  any command (success or refusal), never per frame; refuses print `target=` of the *attempted*
  entity and `state=unchanged`.
- `tick=` is `g_sceneResolvedServerTick` or -1; `nameReason` is `hudLiveResolveName`'s token
  (`ok`, `no-tick`, `no-userinfo-tick`, `no-userinfo-empty`, plus the two repaired empty cases).
- new counters, exit summary only: `followPicks`, `followIdempotent`, `followRejects=<n>`
  with per-reason lines `followReject reason=no-slot n=3`, `followFallbacks`
  (camera could not follow the identity this tick), `followPinnedDeaths`, `followNameMissing`,
  `followArgumentRejected`.

### 5.2 Acceptance assertions (each one is a grep plus a relation)

| # | Assertion | Negative control |
|---|---|---|
| A1 | select 7 -> exactly one `follow:` line, `target=7 pinned=1 src=manual state=ok valid=1`, and `director.targetIndex()==7` in the same frame's `spectate:` line | without the command, no `follow:` line exists and `spectate:` fields are unchanged |
| A2 | select 7 twice -> one line, `followPicks` up 1, `followIdempotent` up 1 | two different picks -> two lines |
| A3 | next/prev from 7 over slots {3,7,9} -> 9 / 3, wrapping; independent of slot order in the snapshot | all candidates dead -> `reason=no-candidates`, target unchanged |
| A4 | pinned target dies -> `state=dead`, then one `reason=pinned-dead`; camera does not hand over (`director.handovers()` unchanged) | unpinned target dies -> handover count +1 (existing behaviour) |
| A5 | serial changes at the same index (seek across a player slot reuse) -> `state=gone reason=index-reused`, pin cleared, camera holds last pose (eye unchanged for that frame) | index out of range -> `reason=index-out-of-range` |
| A6 | userinfo missing at that tick -> `nameKnown=0 nameReason=no-userinfo-empty`, display ``?``, HUD name line unchanged | userinfo present -> `nameKnown=1`, slug is the projected name |
| A7 | cross-tick switch: select at tick T, seek to tick U -> new line with `tick=U`, `state=ok` only if the serial still matches; `scene: eye row reqTick=U` in the same frame | seek with a different entity at that index -> `index-reused` |
| A8 | **guardrail**: a run with no follow command keeps every existing line byte-identical — `scene: camera mode=`, `scene: eye`, `scene: eye row`, `spectate: camera relay target=` — and `demo_open_probe --scan` hashes match (N12 acceptance §17.4 values) | any diff in those lines fails the step regardless of A1-A7 |

A8 is the one that makes the step safe to land before the camera-precedence work: it proves the
new code is a pure addition to the frame.

---

## §6 Minimal first step (state + command + trace, no UI)

### 6.1 Files

| File | Change |
|---|---|
| `native/include/target_identity.h` (new) | the struct of §4 + `targetStateName()` token helper; header-only, no CMake edit |
| `native/src/main.cpp` | `g_followTarget`, `g_followArgumentRejected`, `--follow-target` parse beside `--spectate-target` (`:3336-3348`), `handleFollowAction` beside `handleCameraAction` (`:199`), reconciliation call after the slot build (`:11121`) and before the director update (`:12987`), one-shot hand-off to `pickManual`, the `follow:` line, exit counters (`:13453`) |
| `native/src/main_hud.cpp` | step 1 only if the HUD is to show the followed name in the same slice; otherwise untouched. If touched, `hudLiveResolveName` must be reused, not copied, and the two empty-reason early returns (`:730`, `:742`) repaired |
| tests | none in step 1; step 2 adds a probe driving `handleFollowAction` + reconile over synthetic slots (pattern: `native/tools/spectate_probe.cpp`) |

No CMake edit: `main.cpp` is already in `tf2_demo_native` (`native/cmake/tf2-product.cmake:17`),
and the new header is header-only. `spectate_target.cpp` (the director) needs one small addition
— a `manualRejectReason() const` reading — only if refusals must be named exactly; it is already
compiled into `tf2_core` (`native/cmake/tf2-libraries.cmake:82`), so still no CMake edit.

### 6.2 Pseudocode (the whole logic; 18 lines)

```
followSelect(entity):
  if entity < 0 or entity > 2047: reject(index-out-of-range)
  slot = slotFor(entity)                      # the slots the director gets, not a new scan
  if not slot: reject(no-slot)
  if slot.isObserver: reject(is-observer)     # isHltv userinfo; never the POV recorder
  if not slotObservable(slot): reject(not-observable)
  if slotAlive(slot) is false: reject(dead)
  g_followTarget = {entity, slot.serial, Manual, pinned=true, valid=true}
  g_followPending = true                      # one-shot, mirrors g_spectateManualTarget
followReconcile(slots, resolvedTick):         # once per scene tick, before director.update
  if resolvedTick < 0: state = no-snapshot; return
  if not g_followTarget.valid: return
  if index out of range: clear(); reason = index-out-of-range
  else if no slot: state = gone-no-slot
  else if serial != stored: clear(pin=false); reason = index-reused
  else if dead: state = dead; if pinned: reason = pinned-dead
```

### 6.3 Risks (owner must be told before landing step 2)

1. **N12-C eye chain**: step 1 must not touch `haveObserverView` (`main.cpp:10610-10615`),
   `resolvePovEye` (`:10648-10650`), `applyCameraPovEye` (`:705-791`) or `placeSpectatorView`
   (`:1006-1161`). Step 2 must change the camera decision at `:13008-13009` only *after* A8 is
   green, and must not suppress the `scene: eye` lines: those describe the demo, not the camera.
2. **Two owners of the pending pick**: reuse the one-shot pattern but not the variable. Feeding
   `g_spectateManualTarget` from both the CLI and the follow command gives one variable two writers.
3. **Frame-order fragility**: the reconciliation must run after the slot build and before
   `director.update`; any edit inside `10665-11121` can move `g_sceneResolvedServerTick`'s
   publication relative to the camera row request.
4. **Director reuse for N16**: do not add pane arguments to `SpectateDirector`; N16 instantiates
   one per pane (the class is a plain object). Adding a pane id to it now would be the second
   source of truth N16 is written to avoid.
5. **Settings**: if any follow state is persisted, it must go through `PersistentSettings`
   (`native/include/settings_store.h:9-30`) whose writer carries the other sections; a new
   '[follow]' section written by a separate writer would drop '[camera]' and '[keys]'. No persistence
   is proposed in step 1.

---

## §7 NOT VERIFIED

1. No build and no product run were performed for this spec (read-only task). Every claim about
   runtime behaviour is read from source, not observed.
2. Whether the gate demos carry an HLTV/observer userinfo entry at all (needed to make
   `broadcasterKind=stv-client` reachable) — must be measured from the userinfo table, not assumed.
3. `DemoNetworkSummary::lastViewEntity` is `uint32`; 0 is used as 'not seen' in the reader paths
   (`main.cpp:10648-10650` passes it straight into an `int` parameter). Whether a real demo can
   have entity 0 as a valid view entity is unverified.
4. `EntityHandle::serial` for demo-decoded players: whether it is ever 0 or repeats within one demo
   (which would make `index-reused` ambiguous with 'serial unknown').
5. Whether the STV camera operator is itself present in `spectateSlots` (it is excluded by
   `slotObservable` only if `isPlayer` or team checks fail); the `is-observer` refusal assumes the
   userinfo `isHltv` flag is enough.
6. The exact quantum of the relay print (`main.cpp:1107-1132`) was read, not observed; A8
   depends on it being unchanged in runs without a follow command.
7. N16 integration is untested by construction: no split-view code exists yet
   (`docs/N16-SPLITVIEW-CONTRACT-2026-11-16.md` has zero product code), so 'one struct, no copy'
   is a contract here, not a measurement.
8. Camera-precedence change (pinned follow beating the demo eye, §2.3) is not designed past the
   decision point: it needs a lead ruling and its own acceptance, because it changes what
   `scene: camera mode=` reports in POV runs.
