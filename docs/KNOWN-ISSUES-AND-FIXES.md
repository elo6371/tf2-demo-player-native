# Verified Issues and Fix Status

This page records reproduced problems, their evidence, and their current acceptance boundary. A focused PASS is limited to the named gate; it does not mean the whole application is release-ready.

## Duplicate entity IDs could identify the wrong body

**Symptom:** Two bodies could share one entity colour. The old decoder kept the first ledger row and silently skipped later rows, allowing pixels from another body to be attributed to the wrong entity.

**Verified fix:** The decoder records duplicate IDs and rejects the identity reading with a named failure before sampling pixels. The m12 mutation alternated body IDs: the focused gate changed from a `28/0` baseline to `27/3`, then returned to `28/0` after byte restoration. This narrow fix was measured in an unpublished task branch; it is not part of this public default-branch snapshot.

**Reusable write-up:** [A duplicate entity ID was read as the first body](https://github.com/elo6371/coding-problem-playbook/blob/main/problems/2026-10-28-duplicate-entity-id-was-read-as-the-first-body.md).

## Mutation recovery is still under review

The T61.1 task branch added EXIT-trap recovery for interrupted mutation runs, including source restoration checks, binary deletion, refusal markers, and SIGINT/SIGHUP demonstrations. Review found remaining failure paths: a failed timestamp update can still be treated as a successful restore, and mutation-build or post-restore-build failures do not consistently enter the refusal state. Those paths remain unresolved; do not treat the T61.1 harness as accepted until the fixes and focused tests pass review.

## Air-walk substring matching

The moving animation selector once matched `walk` inside `airwalk`. The task-branch fix uses whole-token matching and preserves leading-token preference so a run cycle is preferred over an arm-layer fragment. Focused forward and mutation evidence exists, but the complete T61.1 acceptance remains pending the recovery-harness review above.

## Mutation anchors that no longer match

A mutation script can fail to apply its patch after a refactor. In that state, a green run says nothing about the intended mutant. The task-branch gates now require named FAIL output from a real mutation and preserve focused run evidence; these changes remain unpublished and are not part of this default branch.

## References

- [Release policy](RELEASE-POLICY.md)
- [Coding problem playbook](https://github.com/elo6371/coding-problem-playbook)
