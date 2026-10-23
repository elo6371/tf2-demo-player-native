# T53 — projectile trajectory overlay

Branch `task/t53-trajectory`, base `6b71aeb`, worktree `D:\TF2_Native_Worktrees\t53-trajectory`.

## The problem the task names

`NEXT-TASKS-2026-10-21.md:583-584`:

> The overlay must not assume constant velocity if the demo provides observed
> projectile positions that capture gravity, collision, or deflection.

That sentence is only checkable if two things hold: the demo really carries a
per-tick observed series, and the product really keeps it. Both are asserted.

**The product WAS already assuming constant velocity, and it had no choice.**
`main.cpp` feeds `setProjectileTimeline` from `projectileTimeline`, which
`demo_header` builds from TempEntity **generation** events — one position plus a
velocity. Anything drawn from that is extrapolated by construction. The overlay
adds the observed-position path beside it; the old path stays as the fallback for
entities with no observed rows.

## Readings

```
track:        rows=257880 dropped=0 packetsScanned=73118 limit=400000
invariant:    tickNonDecreasing=true
series:       lifetimes=2201 distinctSlots=98 contiguous=2201 bounced=664
owner:        rowsWithOwner=257880 of 257880
flights:      candidates=1147 (contiguous, 40..300 samples)
distribution: chordMedian=140.9 chordP90=532.5 chordMax=1472.0 count=1147
distribution: chordAtLeast100=667 chordBelow1=83 velocityAtLeast100=781
flightSubject: CTFProjectile_HealingBolt samples=214 maxError=7408.4 chordMax=441.1
filter:       owners=15 rowsWithoutOwner=0 busiestOwner=790 busiestRows=110145
emptycase:    tick=55193 rows=0 marker=NO-PROJECTILE
cost:         queryComparisonsPerCall<=36 over 257880 rows (scan would be 773640)
appendCost:   packets=73118 medianMs=0.0064 p90Ms=0.0126 maxMs=2.1966
buildCost:    frames=214 medianMs=0.0084 p95Ms=0.0129 maxMs=0.0386
buildCost.points: median=144 max=198 -> vertices median=288 max=396
ownerReuse:   ownerSlots=15 slotsWithMultipleLifetimes=15 worstOwner=712 worstLifetimes=359
```

### The acceptance sentence

> Of 1147 contiguous 40..300-tick flights, **667 (58.2%) have a chord error of at
> least 100 map units**, worst 1472. **83 are genuinely straight** (<1). The
> constant-velocity assumption is off by **7408 units** on the most curved one.

With its denominator, with its counter-examples, with its worst case. This is a
distribution, not a single lucky sample.

## Rules and the readings they are held to

Thresholds were fixed **before** the data. A miss is a finding, not a tuning task.

| Rule | Claim | Reading |
|---|---|---|
| R1 | lifetimes == the oracle's reach count | 2201 = 1228+774+152+47 |
| R2 | lifetimes != distinct slots | 2201 over 98 |
| R3 | rows are tick non-decreasing | true |
| R4a | a flight to assert on exists | 1147 candidates |
| R4b | constant-velocity error >= 100 | 7408 |
| R4c | **the subject really is a flight** | 214 samples |
| R5 | the two shortcuts disagree | 7408 vs chord 441 |
| R6 | **curved flights exist** (count > 0) | 667 of 1147 |
| R7 | query < 100 comparisons over > 100k rows | 36 over 257,880 |
| R8 | append median <= 0.5 ms, max <= 5 ms | 0.0064 / 2.1966 |
| R9 | build median < 1 ms (240 Hz frame is 4.1667) | 0.0084 |
| R10 | all vs selected differ, and it filters | 109 vs 34, 75 excluded |
| R11 | quality tier changes the point count | 26 vs 109 |
| R12 | linger: points inside, none past | 109 / 0 |
| R13 | the no-projectile case reports | NO-PROJECTILE |

## The doctored-input self-test

Every rule reads a number out of a text report with `sed`. So a rule can go red
for two completely different reasons — the product produced a wrong number, or
the parser failed to read a correct one — and from outside they look identical.

**T52 lost a mutation to exactly this on the same day.** So before any demo runs,
the same parser and the same assertions are run against copies of a real report
with one field changed. Twelve cases, one per rule. Every one must fail **and
name the rule the change targets**. A case that passes means the rule only tests
that a field exists.

Two of these cases exist because the plain version was not enough:

* `r6-zero-curved` changes 667 → 0. The rule used to be `>= 0`, which 0
  satisfies — **a present, readable, benign-looking zero passed the rule whose
  whole job is to prove curved paths exist.** A blank-p90 case cannot catch that;
  a zero is not a blank.
* `r4c-flightshape` changes the subject's sample count to 2438. See below.

## Mutations, and what running m2 taught

`--mutation` runs each mutation and requires a **named** rule to catch it. A
mutation that merely fails to build is not a caught mutation.

**m4** reverses the append order, breaking R3. The search then returns **257,880
rows for a tick that holds 9** — wrong rows, not "not found". That is why R3 is a
rule: an unsorted range does not make a binary search fail, it makes it lie.

**m2** takes the longest series as the acceptance subject.

> **Running m2 produced something better than the test: m2 does not fail R4b.**
> The resting sticky bomb's error is **40,818** — *larger* than the most curved
> real flight's **7,408** — because it measures **duration, not curvature**. So
> "error >= 100" passes on the mutated subject. R4c, which asserts the subject's
> *shape* (40..300 samples), is what catches it.

**This is why m2 had to be run rather than argued about.** An assertion that a
series is curved is satisfied by any series long enough to drift.

## Renderer seam

Deliberately minimal, so it merges without disturbing anyone:

* `native_renderer.h`: **one** struct (`ProjectileTrackPoint`, in
  `projectile_track_point.h` which includes nothing, so the renderer never learns
  about the decoder and the probe never needs `<d3d11.h>`), **one** method
  declaration, **two** count getters.
* `native_renderer.cpp`: the method body appended **after**
  `setProjectileTimeline`; its own draw block; two device-reset updates.
* `main.cpp`: **one** call site. Across the whole branch, **zero deleted lines**
  in `main.cpp` — including `main.cpp:6213-6215`, `setCpuParticleTimeline`, and
  main.cpp:7070, which are untouched context.

**Its own vertex buffer**, not a share of `projectileVertexBuffer_`: both passes
upload once per scene tick and draw in the same frame, so a shared buffer lets
them overwrite each other between upload and draw. The symptom would be the trail
flickering between two line sets.

`projectileVertexCount()` still returns the TempEntity pass count.
`projectileTrackVertexCount()` is a separate counter, so **"the new pass did not
quietly move the old one"** is a reading rather than a promise. Both appear in the
HUD line, with `trackPoints / trackRows / trackFiltered / trackLifetimes /
trackOwners / trackLinger`.

## Controls

`settings_store.cpp` persists four keys; `main.cpp` adds five CLI arms.

| Setting | Default | Why that default |
|---|---|---|
| `projectileTrajectory` | **false** | default-on would move every existing capture's pixels |
| `selectedPlayerEntity` | **-1** (= all) | **0 is the world entity and a legal index** |
| `trajectoryLingerMs` | 500 | the task's "about 0.5 s" |
| `trajectoryQuality` | High | Low = every 4th tick, High = every tick |

All five arms verified **on the merged tree** by reading back the settings file
`saveSettings()` writes at exit:

```
default                      0  -1   500  High
--no-projectile-trajectory   0  -1   500  High
--projectile-trajectory      1  -1   500  High
--trajectory-player 712      1  712  500  High
--trajectory-linger-ms 2000  1  -1  2000  High
--trajectory-quality low     1  -1   500  Low
```

Each arm sets exactly its own field and leaves the other three alone.

`selectedPlayerEntity` is the shared "selected player" definition T52 reads.
Q: why -1 and not 0 — **0 is a valid entity index (the world)**.

## Three measurement traps I fell into (recorded, because they were mine)

All three are failures of my method, **not product defects**:

1. **`--capture-tick` is a PLAYBACK tick, not a server tick.** I passed a server
   tick (`123035`). The product waits for a playback position that never arrives
   and plays the whole demo in real time — an 11-minute run that looked like a hang.
2. **A frame cannot show the toggle.** `--capture-frame` writes a BMP, and over a
   tick with no projectile the on/off pair is byte-identical.
3. **The HUD title is unreachable under `--start-paused`**, where it takes the
   `!g_playback.enabled` early return carrying none of the `track*` fields.

The channel that works: `saveSettings()` runs at exit and serialises
`persistent.render`, which is exactly what the arms assign.

## Known limitation: owner slots are reused

```
ownerReuse: ownerSlots=15 slotsWithMultipleLifetimes=15 worstOwner=712 worstLifetimes=359
```

**Every owner slot in this recording carried more than one projectile lifetime.**
So `--trajectory-player <slot>` also draws projectiles thrown later by whoever
inherited that slot, and the user cannot tell. This is the engine's behaviour, not
a defect introduced here — but it is counted, not assumed away.

The player filter is by owner **slot**. After slot reuse the selection includes
the next player's projectiles. The fix would key on `(slot, generation)`;
`ProjectileTrackSample` already carries `generation`.

## NOT VERIFIED

1. **No mouse slider.** `native/` has **0 hits for "slider"**. The control surface
   is `settings_store` + CLI arms, so the task's "slider with immediate preview"
   is **not implemented**. A UI slider still needs: a Win32 trackbar control wired
   to `trajectoryLingerMs`, and live rebuild while paused so the drag is visible.
   *Cheapest next step:* show the current linger value in the HUD before building
   the control — that proves the value reaches the builder.
2. **Owner slot reuse** — see above. *Cheapest next step:* key the filter on
   `(slot, generation)`; the field is already retained.
3. **Only bagel is fully characterised.** Snakewater was parsed but no full
   distribution was taken there. *Cheapest next step:* add a `--demo` flag to
   `trajectory-overlay-check.sh`.
4. **Integer quantisation of `m_flOrigin` was not measured** against the demo's
   network step. *Cheapest next step:* compare the decoder's float against the
   oracle's integer for the same tick.
5. **`m_flGravity` / `m_flElasticity` were never observed at a non-default
   value.** The whole finding is **observation-driven, not a physics simulation**.
   *Cheapest next step:* grep both properties across another demo.

## Lock discipline

At 12:26 I ran `hostlock.sh break t53-merged` intending to release my own stale
lock. I did not hold the lock; T52 did. `hostlock.sh break` has no ownership
check, so it released T52's lock. Reported to T52 and the lead immediately. The
correct action for a lock you do not hold is **nothing**. `break` should require
owner identity or an explicit `--force` with the break recorded in history.
