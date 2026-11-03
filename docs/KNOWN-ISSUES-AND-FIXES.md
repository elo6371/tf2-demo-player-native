# Verified Issues and Fix Status

This page records reproduced problems, their evidence, and their current acceptance boundary. A focused PASS is limited to the named gate; it does not mean the whole application is release-ready.

## Duplicate entity IDs could identify the wrong body

**Symptom:** Two bodies could share one entity colour. The old decoder kept the first ledger row and silently skipped later rows, allowing pixels from another body to be attributed to the wrong entity.

**Verified fix:** The decoder records duplicate IDs and rejects the identity reading with a named failure before sampling pixels. The m12 mutation alternated body IDs: the focused gate changed from a `28/0` baseline to `27/3`, then returned to `28/0` after byte restoration. This narrow fix was measured in an unpublished task branch; it is not part of this public default-branch snapshot.

**Reusable write-up:** [A duplicate entity ID was read as the first body](https://github.com/elo6371/coding-problem-playbook/blob/main/problems/2026-10-28-duplicate-entity-id-was-read-as-the-first-body.md).

## Mutation recovery: focused mock branches pass

**Focused fix and measurement (2026-11-03):** Both T61.1 mutation harnesses now reject a timestamp update that fails or returns success without advancing the source mtime past the build stamp. The failure is named `CASE-RESTORE-TOUCH-FAILED` / `CASE-RESTORE-MTIME-NOT-ADVANCED` during a case, or `TRAP-RESTORE-TOUCH-FAILED` / `TRAP-RESTORE-MTIME-NOT-ADVANCED` in the EXIT trap. Trap failures now have an explicit `FAIL` prefix and enter refusal; they cannot report successful recovery. Mutant-build and post-restore-build failures consistently use `CASE-BUILD-MUTANT-FAILED` and `CASE-REBUILD-RESTORED-FAILED`. An interrupted recovery rebuild that fails uses `RECOVERY-BUILD-FAILED`, writes `BINARIES-NOT-FOR-ACCEPTANCE.txt`, deletes the unverified binaries, and does not rebuild again.

`bash player-animation-recovery-branches.sh` passed with **433 assertions, 0 failures**, `ON-EXIT-BRANCHES=PASS`; `bash -n` passed for both harnesses and the driver. The focused driver extracts the real recovery functions from both harnesses and uses isolated temporary trees and mocked builds. It injects failed and no-op timestamp updates, mutant-build and post-restore-build failures, and actual SIGINT/SIGTERM/SIGHUP while mutant bytes are on disk. It verifies the signal exit codes, byte-for-byte source restoration, named `FAIL` and `REFUSAL` output, the named marker, and no further build after a timestamp refusal.

**Acceptance boundary:** This is a shell-level mock result, not a real product build or gate run, not a full-chain verification, and not reviewer acceptance of the whole T61.1 harness. SIGKILL and power loss cannot run shell traps and remain outside this recovery guarantee. Unreadable-backup injection is synthetic on this host. The previously reviewed failures are covered by the focused matrix; no remaining failure is claimed beyond those explicit boundaries.

## Air-walk substring matching

The moving animation selector once matched `walk` inside `airwalk`. The task-branch fix uses whole-token matching and preserves leading-token preference so a run cycle is preferred over an arm-layer fragment. Focused forward and mutation evidence exists; complete T61.1 acceptance still requires the separately scoped product gates and review, not just the recovery mock matrix above.

## Mutation anchors that no longer match

A mutation script can fail to apply its patch after a refactor. In that state, a green run says nothing about the intended mutant. The task-branch gates now require named FAIL output from a real mutation and preserve focused run evidence; these changes remain unpublished and are not part of this default branch.

## References

- [Release policy](RELEASE-POLICY.md)
- [Coding problem playbook](https://github.com/elo6371/coding-problem-playbook)
