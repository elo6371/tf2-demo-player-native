# Release policy: when the full acceptance chain runs

Decided 2026-10-22. This replaces the earlier practice of running verify-all.sh
whenever a task finished or a date passed.

## The rule

1. **The full chain runs once, on the final frozen tree**, after the final task wave
   (T75 at the time of writing) is complete and merged. Not per task, not per day,
   not for a milestone count.
2. **"The full chain" means `verify-release.sh`, not `verify-all.sh`.** The chain
   entry point runs `verify-all.sh` and then the acceptance gates it does not
   contain: `outline-check.sh`, `third-person-weapon-check.sh`,
   `player-body-appearance-check.sh`, `class-pixels-check.sh`, and (once it
   exists) `build-identity-check.sh`. 70 of the 99 gate scripts in this tree are
   outside `verify-all.sh` -- the four P0 gates named by the handoff among them --
   so `VERIFY=PASS` from `verify-all.sh` alone is not "the chain ran"
   (debts D-19/D-21). `verify-release.sh` prints one line,
   `VERIFY-RELEASE=PASS|FAIL steps_ok=N steps_total=6`, and that line is the only
   citable whole-chain verdict.
3. **Quality is carried by each task's own gates**, not by repeated chain runs:
   every task ships a forward gate and a mutation for every assertion it makes,
   and the mutation must turn the assertion red for the right reason.
4. **Merging is guarded by the regression gate of every module it touches.**
   A merge is not accepted on the strength of 'the chain ran last week'.
5. **Builds, frame captures and mutation suites stay serial** -- one instance
   machine-wide, through the host lock. That has not changed and this policy does
   not relax it.
6. **After the full chain, if code changes**, re-run the gates of the affected
   modules. **If the change touches more than one module, or anything the
   acceptance chain itself asserts, run the full chain again.**

> Numbering: item 2 was inserted on 2026-11-07, when the chain entry point became
> `verify-release.sh`; items 2-5 became 3-6 and their wording is unchanged.
> Citations written before that date that quote the list by line --
> `evidence/t61-animation/T61-ANIMATION-REPORT.md:828` and `:1570`, both citing
> `:12-13` -- mean the pre-insert item 2, i.e. the "quality is carried by each
> task's own gates" rule that now sits at item 3.

## Why

A chain that is run repeatedly does not become more true; it just costs a lock
window that a task gate needed. On 2026-10-22 a single chain took over two hours
and three attempts, and every one of those attempts was started to answer a
question a task gate could have answered in fifteen minutes. Meanwhile five tasks
queued behind it, one of them for two and a half hours.

The failure this policy is exposed to is different and has to be stated: if the
final chain runs once and fails, there is no earlier green run to compare against.
That is accepted, because the alternative -- a green run on a tree that no longer
exists -- was the thing being mistaken for evidence.

## What still runs on every task

- forward gate: rc, per-assertion readings, and the counts it must not drift.
- mutation: one targeted break per assertion, each turning a named assertion red,
  with sources restored byte for byte and the restoration verified.
- a mutant that fails to COMPILE is not a caught mutation. It is an unverified one.
