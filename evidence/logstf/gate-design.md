# T54 gate design: what this gate proves, and what it cannot

## The rule this task produced

> **When a gate turns red, the first question is "is the assertion's object the right
> one?", not "what is wrong with the product?"**

The sixth locked run made this concrete: **all four defects that had to be fixed that
round were errors in the readings themselves. Not one was a product-behaviour error.**

| reading that was wrong | what it actually measured | why it looked like a product failure |
|---|---|---|
| `scrollCalls=2` | the scroll script calls `scrollContent` **once**; `pageExtent` counts `scriptCalls`, a different counter | a working scroll step (rc=0, no script failure) read as "the scroll did not reach the page" |
| `canForward=1` at the end of the history script | the script ends on the **newest** entry after back/forward/reload, so an empty forward stack is the **correct** state | a correct reading read as a defect |
| `$LOGDIR/install-preview-files.txt` | a **relative** path evaluated after `cd "$PREVIEW"`, so it pointed inside the staged tree | the install had printed all eleven files; the missing file list was reported as "INSTALL-PREVIEW NOT COMPLETED" |
| "the guard has three self-tests" | the guard had two | the report claimed coverage the code did not have |

The `canForward` row deserves its own line in the playbook: **empty is a correct
reading in a legitimate state**, and the assertion treated it as a defect. That is the
same family as a helper whose empty return value hides both "no value" and "the value
is empty" -- the reading is not wrong, the question asked of it was.

## The seam rule: a branch no test can turn red is an absent branch

`ILogsViewHost::navigationPending()` exists because a real browser is asynchronous:
"Navigate returned S_OK" is not "the page is there", and entering `Content` on the
former made `goBack()` run before the navigation was committed, so the runtime
answered `CanGoBack=0`. The probe's recording host navigates synchronously, so it
would never exercise the branch that waits -- the branch would look covered while
being dead in every test on every machine, including machines with no WebView2
runtime at all.

Two things make it live:

1. the fake host has a controllable `pendingNavigation` flag, and the probe asserts
   both directions -- `pendingKeepsLoading=1` (a pending navigation keeps the panel in
   `Loading`) and `pendingLandsOnContent=1` (clearing it lands in `Content`);
2. mutation **m5** deletes `if (host_->navigationPending()) return;` and requires
   `pendingKeepsLoading=0` in the mutated probe, i.e. the assertion must go red when
   the line goes away. A build failure does not score (it would bean unverified
   mutation, not a caught one), and "the reading is missing" is reported separately
   from "the reading changed".

## Launch safety: the guard must check the argv that runs

The audio guard's first version checked a constant string at **file scope**, where
`return 99` only prints `can only 'return' from a function` and execution continues to
launch the product. A guard that shouts and then launches manufactures the impression
of having prevented something. It now builds `local -a argv` and checks **that**, uses
one predicate shared with its own self-test, counts launch sites statically
(`LAUNCH_SITES` must be 1, matched on the launch line's shape so the check does not
count its own grep), and proves both directions: an argv missing either safety flag is
refused without ever launching the product, with the codes pinned (`--audio-disabled`
missing -> 98, `--hidden-window` missing -> 99 -- a truly flagless argv is refused by
whichever case runs first, which is 99, so the test is described as "one flag missing"
and not as "flagless"), and a good argv is accepted.

The self-test immediately caught two defects in itself: a predicate written as one
pattern requiring `--hidden-window` **immediately followed by** `--audio-disabled`
(which never matches when any argument sits between them, so it would have refused
**every** correct launch and looked like a script-call bug), and a `grep` that counted
its own line and reported two launch sites where there is one. **A predicate that
refuses everything is as useless as one that refuses nothing, and its failure
direction looks like safety.**
