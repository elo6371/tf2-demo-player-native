# T66's seven NOT VERIFIED readings: what each one is, and what closes it

T66 shipped with everything it measured at PASS, and seven readings it did not
take written down as NOT VERIFIED (report §9). "Not verified" is not a passing
grade, and a release cannot carry seven of them without saying, one at a time,
whether it is a gap in the gate, a gap in the corpus, or outside the product's
contract. This file does that, and says what evidence would close each one.

Written 2026-11-02 on integration/t79-final (e3e2910 + the T52 work).

## 1. High playback speed (16x): a stall is misread as a seek

**A real product defect, small.** `SpectateDirector::update` decides "the user
seeked" from the tick delta alone: `native/src/spectate_target.cpp:76-83`,
`serverTick - lastServerTick_ > kSeekGapTicks` with
`kSeekGapTicks = 200` (`native/include/spectate_target.h:85`). At 16x a frame
that takes long enough for the next update to arrive 200+ ticks later is counted
as a seek: the handover is abandoned (a snap instead of a glide) and `resync`
rises. Measured on the T66 round: `resync=2` at 16x, which is why the gate pins
8x.

The tick delta cannot tell a seek from a stall, but the director already holds
the other half of the answer: it is given a monotonic wall clock every update
(`nowSeconds`), for exactly this kind of separation. A jump is a seek when it
is larger than continuous playback at the current speed could have advanced
since the previous update; otherwise it is a stall.

**Closure**: pass the playback speed to `update()`, bound the gap by
`66 * speed * dt * margin` with a floor, unit-test both directions (a slow
stall at 16x is not a seek; a jump in one frame is), and add a gate section that
replays at 16x and asserts `resync=0` together with the same three deaths and
handovers the 8x run reports.

## 2. A real seek or rewind during a handover: never driven

**A gap in the gate, not in the product.** The relay handles a backwards tick
(`spectate_target.cpp:77`) and the probe unit-tests it, but no gate ever asked
the product to seek: the only seek it ever saw was the 16x misread above.

The product already has the path, and a headless way in. `--camera-script` is a
test-only step runner (`native/src/main.cpp:785-1372`) with `seek`,
`jump-seconds`, `speed`, `playback-state` and `expect-tick` steps, driving
the same `PlaybackControl` requests the keyboard and the UI use.

**Closure**: a gate section that replays to just after a death, starts a
handover, seeks backwards past it, and asserts from one trace: `resync` rose,
the target is alive again, no handover is in flight on the first frame after the
seek, and the focus did not interpolate across the two moments.

## 3. The demo's own observer target, never alive

**A limit of the corpus.** In this recording `m_hObserverTarget = 301073`
(index 17 / serial 147) points at a permanently dead spectator slot, so "follow
the director's target" can only be asserted as `director=0` -- the fallback
path, which is the path every other assertion already covers.

**Accept, with the reason named.** What would close it is a SourceTV recording
whose observer target is a living player; the fixture generator
(`stability-make-inputs.ps1`) only truncates real demos, so it cannot
manufacture one. The gate keeps `director=0` as its reading, which is honest:
it says the director was unavailable, not that it was followed.

## 4. Death causes (environment, team damage, suicide): not distinguished

**Outside the contract.** The relay reads two fields -- `m_lifeState` and
`m_iHealth` -- and hides a model when either says dead
(`spectate_target.cpp:94-98`). It never asks why. Any death a demo reports
through those fields is covered, whatever caused it; a death the demo does not
report as a death is not covered by anything, and no cause classification would
change that.

**Accept**, restated as: the corpus contains only one death mechanism and the
product deliberately does not classify causes. The forbidden shortcut is already
asserted: a row with neither field is not a corpse (report §3 step 3).

## 5. Attached models (hats, wearables): not verified to leave with the body

**Not covered, and nothing in the product could cover it today.** The corpse
rule hides the player's own model by entity index; TF2 sends wearables as
separate entities, and the product has no `m_hMyWearables` reader at all
(`grep -r wearable native/` finds only the weapon/projectile/wearable draw
class and probe comments). So the honest answer is "the player model leaves, the
attached models are whatever the demo does with them".

**Accept for release, with a named follow-up**: closing it needs (a) the owner
handle parsed for world entities, and (b) a demo where a hatted player dies in
frame -- neither is in this corpus. Documented here so a later round does not
rediscover it as a bug report.

## 6. A manual pick while paused: never driven

**A gap in the gate, and the cheapest of the seven.** Section 6 picks a player
from the launch line and asserts it lands in the first frame without a
transition; section 8 pauses the demo and asserts the handover clock keeps
running. Nothing runs both at once, so "a paused relay keeps the user's pick
instead of being stolen back by the automatic rule" is unmeasured.

**Closure**: one more run at the section 8 pause fixture with
`--spectate-target 6`, reading the frozen window: every line carries
`src=manual`, `target=6`, `tr=0`, and no handover opens while the demo clock
is stopped.

## 7. Multi-monitor, exclusive full screen, audio: inherited

**Environment-limited, inherited from T49.1**, untouched by T66. There is one
64-bit D3D11 window on one monitor in this harness; nothing here can observe a
second display or an exclusive-mode flip.

**Accept as inherited**, and keep it in the T49.1 list rather than growing a
second copy of it here.

## What this changes for release

Three of the seven become measurements (1, 2, 6), one becomes a smaller claim
(1 also becomes a product fix), and four are accepted with a reason on record
(3, 4, 5, 7). None of them is left as "unproven, unknown whether it works".
The gate changes for 2 and 6 need no product code; 1 needs the speed-aware bound
in `SpectateDirector`.
