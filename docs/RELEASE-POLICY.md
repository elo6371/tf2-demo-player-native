# Release policy: when the full acceptance chain runs

Decided 2026-10-22. This replaces the earlier practice of running verify-all.sh
whenever a task finished or a date passed.

## The rule

1. **The full chain runs once, on the final frozen tree**, after the final task wave
   (T75 at the time of writing) is complete and merged. Not per task, not per day,
   not for a milestone count.
2. **"The full chain" means `verify-release.sh`, not `verify-all.sh`.** Order
   matters in the chain (static steps before anything takes the lock, a rebuild
   after the mutation suites), so the full list is written here; the `STEPS` array
   in `verify-release.sh` remains the source of truth:
   1 `gate-launch-hygiene-check.sh`, 2 `debt-claim-check.sh`, 3
   `verify-all.sh`, 4 a rebuild step (`build-target.sh` for the product and the
   probes the later steps trust), 5 `outline-check.sh`, 6
   `third-person-weapon-check.sh`, 7 `player-body-appearance-check.sh`, 8
   `class-pixels-check.sh`, 9 `build-identity-check.sh`, 10
   `world-geometry-budget-check.sh`, 11 `install-cli-contract-check.sh`, 12
   `playback-error-paths-check.sh`, 13 `timeline-check.sh`. The rebuild step is
   load-bearing: `verify-all.sh` runs mutation/restore suites that refresh source
   mtimes, and `outline-check.sh` refuses a binary older than its sources -- without
   the rebuild the chain measures a pre-mutation product (observed in round 2,
   2026-11-07). **Step 1**, `gate-launch-hygiene-check.sh`, is a static ratchet that
   freezes the set
   of product launches still skipping `--settings-file`/`--ui-settings-file`
   (D-22) and fails both when the set grows and when a gate was fixed without
   deleting its baseline row -- so "the chain isolated the machine" cannot decay
   silently between release runs. **Step 2 is `debt-claim-check.sh`**, a static
   reader of `docs/T85-DEBT-LIST.md`: every row whose last cell claims `CAN-RED`
   must name a gate anchor (`gate:<script>#<literal token>`) that resolves to a
   real script containing that token, so "this debt can be judged red" is not
   prose. `CANNOT-RED` / `CAN-RED-UNANCHORED` are allowed only with a reason; the
   latter is the list of gates still owed. It needs no lock: no build, no launch.
   Most of the gate scripts in this tree are outside `verify-all.sh` -- the four
   P0 gates named by the handoff among them -- so `VERIFY=PASS` from
   `verify-all.sh` alone is not "the chain ran" (debts D-19/D-21).
   `verify-release.sh` prints one line,
   `VERIFY-RELEASE=PASS|FAIL steps_ok=N steps_total=13`, and that line is the only
   citable whole-chain verdict. **The chain must also prove it did not rewrite the
   tester's real configuration.** Before the first step and after the last one it
   snapshots `%LOCALAPPDATA%/TF2 Demo Player/settings.ini` and `ui.ini`
   (existence, size, mtime, sha256) and fails `MACHINE-MUTATED=FAIL` on any
   change; debt D-22 is why this is needed -- gates launch the product without
   `--settings-file` / `--ui-settings-file`, and the product writes
   `settings.ini` on exit and `ui.ini` whenever it opens a demo. Until D-22 is
   fixed this step is expected to be red, and a red machine step blocks the
   release; it is not a warning to be waived (`VERIFY_RELEASE_ALLOW_MACHINE_MUTATION=1`
   exists only to diagnose D-22 and its runs cannot sign a release).
3. **Quality is carried by each task's own gates**, not by repeated chain runs:
   every task ships a forward gate and a mutation for every assertion it makes,
   and the mutation must turn the assertion red for the right reason.
4. **Merging is guarded by the regression gate of every module it touches.**
   A merge is not accepted on the strength of 'the chain ran last week'.
   **A ratchet baseline is worktree-local evidence and must be re-checked after
   the merge.** When `gate-launch-hygiene-check.sh` (or any gate that carries a
   baseline file) is merged back into the frozen tree, the first chain run on the
   frozen tree must re-run the gate there; if the baseline genuinely changed,
   review the diff and only then run `--update-baseline`. A baseline generated in
   a task worktree says nothing about the frozen tree.
5. **Builds, frame captures and mutation suites stay serial** -- one instance
   machine-wide, through the host lock. That has not changed and this policy does
   not relax it.
6. **After the full chain, if code changes**, re-run the gates of the affected
   modules. **If the change touches more than one module, or anything the
   acceptance chain itself asserts, run the full chain again.**

> Numbering: item 2 was inserted on 2026-11-07, when the chain entry point became
> `verify-release.sh`; items 2-5 became 3-6 and their wording is unchanged. Item 2
> was extended the same day with the machine-isolation requirement (debt D-22) and
> with step 7 (`world-geometry-budget-check.sh`); no item was renumbered.
> On 2026-11-07 (task-18) the launch-hygiene ratchet
> (`gate-launch-hygiene-check.sh`) was added as the chain's first *executed*
> step: total steps 7 -> 8, and the old seven keep their order (they now run as
> steps 2-8).
> Citations written before that date that quote the list by line --
> `evidence/t61-animation/T61-ANIMATION-REPORT.md:828` and `:1570`, both citing
> `:12-13` -- mean the pre-insert item 2, i.e. the "quality is carried by each
> task's own gates" rule that now sits at item 3.
> On 2026-11-07 (merge window) the chain gained install-cli-contract, playback-error-paths, timeline and a rebuild step between verify-all and outline; total 12 -> 13.

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
