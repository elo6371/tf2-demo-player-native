# Release policy: when the full acceptance chain runs

Decided 2026-10-22. This replaces the earlier practice of running verify-all.sh
whenever a task finished or a date passed.

## The rule

1. **The full chain runs once, on the final frozen tree**, after T49.1 through T55
   are all complete and merged. Not per task, not per day, not for a milestone
   count.
2. **Quality is carried by each task's own gates**, not by repeated chain runs:
   every task ships a forward gate and a mutation for every assertion it makes,
   and the mutation must turn the assertion red for the right reason.
3. **Merging is guarded by the regression gate of every module it touches.**
   A merge is not accepted on the strength of 'the chain ran last week'.
4. **Builds, frame captures and mutation suites stay serial** -- one instance
   machine-wide, through the host lock. That has not changed and this policy does
   not relax it.
5. **After the full chain, if code changes**, re-run the gates of the affected
   modules. **If the change touches more than one module, or anything the
   acceptance chain itself asserts, run the full chain again.**

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
