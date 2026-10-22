# Gate design traps

Written after T51 (the custom-HUD import gate), because six of that gate's
assertions could not pass and nobody had noticed: a gate that is always red and a
gate that is always green look identical from the outside -- "the suite has a
known failure" -- and both cost a host-lock hour to find out.

T52 and T54 hit the same classes within a day of T51. So: read this before
writing a new assertion, and check a gate against it before calling it green.

## The one question to ask first

> **Under the correct implementation, does this assertion actually execute --
> and does the thing it asserts about actually exist?**

If the answer is "it would if the code did X", the assertion is decoration. It
either never runs (the code path is not taken) or compares a value that was never
produced (the reading is absent, the file was written elsewhere, the line was
never printed). Both read as a permanent red, or as a permanent green if the
comparison is written the other way round.

## The traps, with the instance that taught each one

### 1. The object does not exist on the success path
The assertion is correct in spirit and unsatisfiable in practice.

| instance (T51) | what was missing |
|---|---|
| 2/9 read `$ON_VALID` | the variable was never defined; `set -u` killed the script **before the first assertion**, which reads exactly like "the gate ran" |
| "reset ran" | the CLI `--hud-reset` path printed no `hud: reset result=` line; only the button and hotkey paths did, and the test drives the CLI |
| mutation 2 compared BMPs in `$OUT` | `run_hud` writes bitmaps to `$SCRATCH`; the two files it read never existed, so the *restored* comparison could never agree |
| "the user's file is gone" (×4) | see trap 4 |

**Rule:** for every reading your assertion consumes, print the reading
unconditionally (not only on success), then assert on that line. If the line is
absent, the assertion must say NOT-VERIFIED, not PASS and not FAIL.

### 2. The always-green mirror: a verdict computed on a red baseline
T51's `--mutation` ran the mutation suite even when the forward gate had failed.
A mutation "caught" against a baseline that never worked proves nothing, and it
exits 0, so the suite reports success on a gate that cannot pass.

**Rule:** a mutation suite must refuse to start unless the forward run is green,
and must say so (`MUTATION-SUITE=REFUSED`), not silently skip.

### 3. Parameter expansion merges "empty" and "missing" -- three times in one gate
This is a language trap, not a slip. In bash, `${VAR:-default}` substitutes the
default for **both** unset and empty, and `${2:-<none>}` inside an assertion
helper does the same to the value being compared.

* `${RESET_ROOT:-<none>}` rewrote the legitimate empty value, so "the root is
  empty after a reset" could never be true.
* `assert_eq` compared `"${2:-<none>}"` against `""`: same bug, one level down,
  and now it applies to **every** assertion in the file. Measured output:
  `FAIL hud.root is empty in the file after reset = <none>, expected `.
* The same helper silently hides "the key is absent" behind the same string as
  "the key exists and is empty" -- which are different findings: after a reset the
  file says `hud.root=` (present, empty); a plain capture run writes no `[hud]`
  section at all (absent).

**Rule:** use `${VAR-default}` when empty is legitimate. For an expected-empty
comparison, do not go through a helper that defaults -- write the `-z` test and
print which of the two states was found. Assert the two states separately.

### 4. A tool's output format is not the fact
MSYS `sha256sum` prints `<hash> *<path>` (binary mode) where GNU on Linux prints
`<hash>  <path>`. T51's helper stripped `./` but not `*`, so
`*./user-notes.txt` was looked up verbatim, not found, and reported **gone** --
four times. A "the product deleted the user's files" grade conclusion resting on
which coreutils build is installed.

**Rule:** when parsing another program's output, normalise the format explicitly
(strip a leading `*`, or use `sha256sum --text`), and keep one offline
before/after reproduction of the old and new parse in the evidence.

### 5. A mutation that fails to compile is a red for the wrong reason
T51's mutation 4 injected `std::filesystem::remove_all` into a file that did not
include `<filesystem>`; the build would have failed and the suite would have
scored "caught". The fix was to inject the `#include` too.

**Rule:** every mutation prints `mutated build rc=0` **and** the assertion's red
reading, on its own line. A reader must be able to tell "the assertion went red"
from "the build broke" without running anything.

### 6. Readings printed only on PASS (T54's probe)
A probe that prints a reading inside `if (ok) { ... readings.set(...) }` shows
nothing exactly when the mutation is active -- so the suite sees "reading absent"
and cannot distinguish a changed value from an absent one.

**Rule:** readings are emitted unconditionally, next to the verdict, always.

### 7. A scripted flag that freezes the thing you assert about (T52)
T52 passed `--start-paused` into a run whose assertion was "the capture reaches
tick N"; playback never advanced, so `--capture-tick N` could never be reached.

**Rule:** when a harness flag changes the state the assertion measures, the
assertion is measuring the flag. Look for the reading that proves the run was
live (`tick=0` in every T51 capture would have been the tell).

### 8. Pinning a number that is not an invariant
T51's corpus reading is 800 of 1311 panels placed (61%), and 55 of 71 imported
documents drop at least one panel. That number is a property of **the installed
corpus on this host**, not of the code: pinning it turns the next corpus update
into a false red.

**Rule:** assert invariants (a count cannot exceed its total; a partial document
cannot drop fewer panels than documents), record snapshots as evidence with the
date and the corpus path in the report, and say in the doc that the number is a
measurement.

### 9. The asserted string exists, and can never equal the expectation (T54)
T54's gate asserted `refusal=host-not-allowed` against the product's summary
line. The substring `refusal=` **is** in that line -- so a reviewer checking
"is this assertion anchored on a real field?" finds it and moves on -- but the
field holds `decision.message`, a human sentence ("'evil.example' is not a
logs.tf address; ..."), and no refusal ever produces the token. The assertion
was unsatisfiable from the day it was written; it was found by reading the
writer, not by running the gate (which had never run at all -- see trap 1).

This is the quietest form of trap 1: not a missing object, but an object of the
**wrong type** -- prose where a token was expected.

**Rule:** for every asserted token, point at the line that writes **exactly that
token**, and say which value makes it come out that way. If the only answer is
"the message contains the reason", the fix is a machine-readable field
(`refusalReason=`), asserted instead of the prose -- and the prose is still
printed, because a human reads the sentence and a gate reads the token.

### 10. "It did not change" and "I did not see it change" are different findings
T54's work was queued behind a long chain run. The chain's heartbeat was alive
but I could find no file written in the last eight minutes, and I reported the
chain as possibly stalled. It was not: it was at step 8 of 24, and its output was
going to `D:\TF2_Native_Test\evidence\verify\`, not into the worktrees tree I
searched. The Lead had made the same call minutes earlier from a different
window. Two people, two wrong reads, same shape.

The observation that would have settled it was available: a **counter that
advances** (step 8 -> 10 of 24) and the mtime of the file the work must write.

**Rule:** before saying "nothing is happening", name three things -- the exact
path/process where the activity **must** appear, the counter or timestamp that
proves it advanced, and the reading that would distinguish "slow" from "stuck".
"An absence of writes where I looked" is not evidence of an absence of work, and
a false "stuck" invites an intervention that throws away an hour of someone
else's run. The tell is usually that the *shape* of the evidence is missing
entirely (no file was ever found, not even an old one), which is a search fault,
not a work fault.

### 11. Corrupted arguments look like product defects (T54's /Zs run)
T54 ran `cl /Zs` while queued, and the first run reported that **all six**
translation units had failed -- including `main.cpp`, which is known to build.
The errors were plausible-looking products of a wrong compiler invocation: MSYS
rewrites any argument that starts with `/` into a path, so the build's
`/DWIN32 /O2 /utf-8` reached cl as `C:/Program Files/Git/DWIN32`, and every
source was then read as code page 936 rather than UTF-8. That yields
`C2001: newline in constant` and `C3493: cannot implicitly capture` in files
that compile fine.

Two things made it findable: one file that "should" build was red, and the
compiler's complaint was about a *path* (`C:/Program Files/Git/DWIN32`), which is
never a source problem.

**Rule:** when a tool reports a mass failure, read what it says it **opened** or
**was invoked with** before reading what it says is wrong. A harness bug that
wears a product bug's clothes costs a debugging session and, worse, teaches you
to ignore the compiler. Fix the invocation (here: `export MSYS2_ARG_CONV_EXCL='*'`
and spell options with `-`), then believe the output.

## Checklist before calling a gate green

1. For each new assertion: **does its object exist on the success path?** Name the
   line/reading it consumes and show that line in the log when everything works.
2. Is each reading printed **unconditionally**, next to its verdict?
3. If an expected value can legitimately be empty, is it compared **without** a
   `:-default` helper?
4. Does every assertion have a mutation that turns it red, and does that mutation
   print `mutated build rc=0`?
5. Does the mutation suite **refuse** to run on a red baseline?
6. Is every mutated source restored by byte copy and verified with `cmp`
   (plus a source hash before/after in the same run)?
7. Is any number being asserted actually a **snapshot** rather than an invariant?
8. If the number of assertions changed, does the report say why (T51: 116 → 114
   because four per-file "is gone" lines became one summary line, and one new
   assertion was added -- "fewer assertions" and "different assertions" are
   different claims).
9. For each asserted token: which line writes **exactly** that token, and can
   that line ever produce it (trap 9 -- the token is present but is prose)?
11. For any claim that something **did not change**: which counter or mtime was
    read, at which path, and would the evidence have appeared there if it had
    happened (trap 10)?
12. Does the gate state in the report **what it does not prove** (T51: no panel
   pixels are read; T54: no page pixels are read), with the cheapest next step?
