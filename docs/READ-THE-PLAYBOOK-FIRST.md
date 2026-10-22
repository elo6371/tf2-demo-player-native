# Before any work: read the problem playbook

Decided 2026-10-22, after a day in which the same classes of defect appeared six
times in one gate, hours after they had been written up by the same author.

## The rule

1. **Before starting a task**, open the problem playbook at
   `github.com/elo6371/coding-problem-playbook` and read `problems/INDEX.md`.
   Open any entry whose symptom could apply to what you are about to build.
2. **Before queuing a gate run**, re-read the four recurring families in that
   repository's README: *is this the number I think it is*, *is this the stream it
   is written to*, *did it fail or was it never observed*, *did I see a change*.
3. **Every point that launches the product must be silent by default**: pass
   `--audio-disabled`, or pass an explicit `--audio-device` and write at the call
   site why that device is the right one. A launch with neither is refused.
   The exception is a gate whose subject IS the audio path (`--audio-device 6` on
   a mute buffer): it may make the device real, but it must say so at the call
   site, and the guard still refuses anything with neither flag. Enforcing this
   inside the launcher is not enough -- a guard written at file scope with
   `return` instead of `exit` prints REFUSED and then runs the command anyway.
4. **When you hit something hard**, write it up in the playbook before moving on,
   while the readings are still in front of you. A rule without its readings is
   folklore.
5. A defect found in review that is already in the playbook is not a new finding.
   Link the entry instead of re-deriving it.

## Why a repository and not a document

Notes in a chat scroll away; the next agent inherits none of them. A committed
file is inherited. The cost of writing one up is about ten minutes; the cost of
re-discovering one is measured in hours and, on 2026-10-22, in a gate that had to
be rewritten four times.

## Not negotiable

A playbook entry that says *verified solution* must contain the reading that
proved it. Entries that only say "be careful with X" are worse than nothing
because they create the impression of a lesson learned.
