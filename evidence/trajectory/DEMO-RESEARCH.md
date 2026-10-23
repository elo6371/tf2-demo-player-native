# T53 evidence -- read-only demo research (2026-10-22, no product code)

All readings below were produced by the EXISTING probe
(`native/build-nmake/entity_model_probe.exe`, built from base 6b71aeb) and the
existing independent Rust oracle (`ent-oracle.exe`, demostf parser), outside the
host lock, against the bagel demo. No product code was touched, built or
launched to obtain any of them.

Demo: `D:/TF2_Demo_Player/testdata/demos/4a9bfb9276509d0ec5f5fdc722a95b17_match-20260927-0239-koth_bagel_rc13.dem`

## 1. The demo DOES carry a per-tick observed projectile position series

Entity 801, `CTFGrenadePipebombProjectile`, spawned at demo tick 101. Every
packet from 101 to 175 was dumped individually and `m_vecOrigin` read from each.

    ticks present          75 of 75  (every tick, no gaps)
    ticks with m_vecOrigin 75 of 75

    z:  431,423,416,409,402,394,392,397,399,401,402,403,404,404,405,405,405,405,405,405,404,403,402,401,400,399,397,395,393,391
    dz:  -8,-7,-7,-7,-8,-2,+5,+2,+2,+1,+1,+1,0,+1,0,0,0,0,0,-1,-1,-1,-1,-1,-1,-2,-2,-2,-2

Reading of that series -- this is the acceptance the task asks for, measured:

* GRAVITY (ticks 101-106): z falls 431 -> 394 at a steady -7/-8 per tick.
* COLLISION + BOUNCE (tick 107-109): the step flips -8 -> -2 -> +5 -> +2. The
  projectile hit something and reversed.
* DEFLECTION / ROLL (ticks 110-125): horizontal step collapses ~16.6 -> ~1.4
  units/tick; y direction changes sign (+15 -> -1).
* settle, then a slow -1/-2 per tick drift to tick 175.

A second, independent entity confirms it is systematic: rocket 683
(`CTFProjectile_Rocket`, spawn demo tick 46) also writes `m_vecOrigin` on every
tick 46..64, walking -383,839,413 -> -367,838,412 -> ... -> -219,831,403.

## 2. Constant-velocity extrapolation is measurably wrong

Derived a constant velocity from entity 801's first two observed positions and
extrapolated exactly as an "assume constant velocity" implementation would:

    over 75 ticks: mean error 576.0 map units, max error 1246.8 map units
    at the final tick the truth is (-380, 914, 357)
                    the shortcut says (-753, 1985, -161)

The map is roughly 3400 units across, so the shortcut lands a third of a map
from the truth. This is the number the gate asserts.

Second demo, snakewater pipebomb 647 (83 samples, ticks 28..110): constant
velocity max error 881.8 map units. Same conclusion on a different recording.

## 3. Why the product could not see any of it -- RETENTION, not the wire

`queryEntitySnapshotAtOrBeforeTick` (demo_header.cpp) resolves any tick to the
newest retained CHECKPOINT at or before it. On bagel:

    history archive=96 ticks=[56148..129125] medianGap=822 maxGap=1180
    history coverage samples=256 exact=1 checkpoint=255 unavailable=0
    history staleness worst=1057 at=87378 mean=400.5

256 sampled ticks, only 1 answered exactly. A projectile flight is 60-90 ticks
and the median checkpoint gap is 822 ticks, so an entire flight collapses into
ONE frozen sample. Sampled at any tick 101..175 through the old path, the same
three coordinates come back for every tick.

## 4. Class inventory (full 73,118-packet oracle pass on bagel)

    CTFProjectile_Rocket              1228 spawns / 95 slots
    CTFGrenadePipebombProjectile       774 spawns / 91 slots
    CTFProjectile_HealingBolt          152 spawns / 63 slots
    CTFBall_Ornament                    47 spawns / 32 slots
    total 2201 projectile spawns in one demo

snakewater confirms the same four families: Rocket 1065, Pipebomb 824,
HealingBolt 143, Ball_Ornament 31.

Density: of 60 evenly spaced packets, 33 held at least one live projectile and
3 held four or five.

## 5. Why the class test must be a NAME and an explicit list

Class ids belong to one recording's server class table (elsewhere in this repo
CTFViewModel measures 320 on one demo and 18 on another). A substring rule is
wrong in both directions over bagel's 355 server classes:

* FALSE POSITIVES -- CTFRocketLauncher, CTFGrenadeLauncher, CTFPipebombLauncher,
  CTFFlameThrower, CTFJar, CTFBottle, CTFPowerupBottle, CTFRocketPack all match a
  projectile keyword and are weapons or held items that sit at (0,0,0).
* FALSE NEGATIVE -- CTFGrenadePipebombProjectile matches no `CTFProjectile_`
  prefix but is the second most common real projectile (774 spawns).

## 6. NOT VERIFIED (carried forward into the delivery report)

1. INTEGER QUANTISATION. All 75 pipebomb samples and all 83 snakewater samples
   have whole-unit coordinates. Sampling is one position PER PACKET (~1/tick on
   this recording), but sub-unit curvature inside a tick is not recoverable.
   Cheapest next step: none -- this is a property of the wire format, not a gap
   in the work. A 33-tick trail is 33 integer points, not a smooth curve.
2. m_flGravity / m_flElasticity ARE IN THE CLASS TABLE but were never observed
   carrying a non-default value. Bounce behaviour is therefore OBSERVED, not
   explained: this work reconstructs the path from positions and does not
   simulate physics. Cheapest next step: dump those two props across every
   projectile Enter packet on bagel and count non-default values (`--props-at`
   with the entity set, one probe run, no lock needed beyond the probe build).
3. ONLY BAGEL WAS FULLY CHARACTERISED for the retention path. snakewater was
   confirmed for spawn counts and one constant-velocity reading, but the
   per-tick retention coverage numbers were measured on bagel only. Cheapest
   next step: run the same probe line against snakewater once the probe exists.
