GATE-UNBLOCK-2026-11-22 -- evidence bundle
==========================================

What produced this directory
----------------------------
One run of five gates, in this order, from the worktree root, behind the host
lock:

  bash /d/_shared/tf2-hostlock/hostlock.sh gatefix2 -- \
       bash tools/lockrun.sh gatefix2 -- bash .scratch/run-fivegates.sh

The runner is kept in the worktree as .scratch/run-fivegates.sh (scratch, not
tracked). Every gate wrote its own evidence into a redirected output directory
(.scratch/fivegates/<gate>/) because tools/lockrun.sh:22-32 fails a run
(LOCKRUN=TRACKED-DIRTY, exit 97) when a tracked directory changes inside the
locked window; the copies here were made after the lock was released.

Layout
------
  inputs.txt        run time, HEAD, branch, byte size + sha256 of every input
                    file, mtimes of the two tools it used
  run-order.txt     the five gates with rc and wall seconds, ALLDONE line
  summary.txt       PASS/FAIL/VERDICT/SKIP/SUMMARY lines of the five logs
  <gate>.txt        full stdout of each gate
  evidence/<gate>/  the files each gate wrote for itself (pov.json,
                    fixture.json, pov-row.txt, ...)
  analysis/         the analysis scripts quoted by docs/GATE-UNBLOCK-2026-11-22.md
                    section 3.2 and section 10, each with the .out it printed.
                    Every script resolves its input relative to its own URL, so
                    "cd analysis && node an13.mjs" reproduces the committed .out
                    from any working directory. an6/an7/an10 were re-run on
                    2026-11-22 and matched their committed .out byte for byte.
                    census.sh -> census.out is the "27 *.sh" count in section 6;
                    ctx.sh -> ctx.out is the 22 rows under it.
  analysis/inputs/  the four dumps those scripts read (cam-rows.txt,
                    sweep-rows.txt, newpov-cam.out, newpov-sweep2.out; the last
                    is 2,157,391 B and is the only reason section 3.2's table is
                    reproducible from the repo alone)
  probes/           six raw probe dumps plus one added later:
                    d2-oracle-pov-200000.txt   the substitute POV's header (section 1)
                    d2-props-all-200000.txt    the 378-entity table (no entity 18)
                    d4-oracle-199999.txt       the packet that fixes the 59,567 offset
                                               and shows DT_LocalPlayerExclusive on 7
                    d4-props-e2.txt            entity 2's observer fields (24587 / 3 / 2)
                    newpov-hist.err            archive 59,563..335,780
                    stv-hist.err               the STV's archive, for section 3.3
                    d5-upward-weapon.txt       pl_upward POV weaponViewModelDiffers=2
  locks/            the raw lock records: d2/d4 attempts (rc=2 retries, rc=97
                    = tools/lockrun.sh drift detector), d5 and stv progress,
                    fivegates-attempts.log (one line: the five-gate run took the
                    lock on its first try)
  ep-lookup-pov.txt the raw resolver counts behind BLOCKED-INPUT-1

What is NOT here
----------------
Two oracle dumps are excluded by .gitignore rules 10-11
(evidence/**/oracle-survey-*.txt and evidence/**/oracle-enters-*.txt) and exist
only on the machine that ran the gates:

  evidence/weapon-world-model/oracle-survey-bagel.txt   8,981,500 B
  evidence/weapon-world-model/oracle-enters-bagel.txt     799,634 B

They are intermediate products the same gates regenerate on every run (the
oracle walk over a class table), and every number this task pins appears as an
OK/FAIL line in the corresponding <gate>.txt. Nothing was measured from them
that is not in those logs. Regenerate with
"bash check-oracle.sh <oracle.exe> evidence/final".

One 2.7 MB dump is NOT excluded and IS in this repository:

  evidence/animation-availability/oracle-all-classes.txt 2,834,074 B

Section 3.4's classesWithSequence=197 / classesWithCycle=195 /
classesWithRate=197 are read out of it, and it does not match either ignore
pattern. (An earlier draft of this file listed it as dropped; that was wrong.)

Expected verdicts (and why they are the expected ones)
-----------------------------------------------------
  weapon-world-model      rc=0  WEAPON-WORLD-MODEL=PASS      re-pinned on the substitute POV
  animation-availability  rc=0  ANIMATION-AVAILABILITY=PASS  override only, no assertion touched
  entity-property-lookup  rc=1  BLOCKED-INPUT-1              sweep is 1831, pinned 0
  observer-focus          rc=1  BLOCKED-INPUT-2              the pinned ticks are before the
                                                             substitute's archive start
  world-geometry-hole     rc=3  named SKIP                   pl_odyssey recording absent

See docs/GATE-UNBLOCK-2026-11-22.md for the argument behind each row.

Which version of the gates produced this
----------------------------------------
The five-gate run was made on HEAD 77122239's parent tree with the sources at
their 2026-11-22 state: inputs.txt records HEAD 050b98e8 (the branch base),
because the gate sources and the evidence were committed afterwards. Section 10
of the report maps every number in the report to a file in this directory.
