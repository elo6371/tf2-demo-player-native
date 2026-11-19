# T84 animation-drive-states, nine captures overlapped (`--jobs 4`)

2026-11-19 14:19–14:48 on `t105-integrate` (HEAD `c9b0def0`), host lock owner
`t84-jobs4`. This is the same gate, same demo, same ticks and same readings as the
serial PASS in `docs/GATE-VERDICTS-ROUND2-2026-11-15.md`; the only difference is that
the nine captures ran in waves of four instead of one at a time.

## Why

The serial gate cost 3,848 s (64.1 min), over the project's "one lock, under 40 min"
rule (T84 in `docs/TASK-LIST-2026-11-19.md`). The nine captures are independent: same
demo, same start tick, only `--capture-tick` and `--animation-drive` differ. Nothing
in the readings is speed-derived -- `--playback-speed` is refused on purpose (D59: the
`flinchStarts` peak is an edge reading and moves with sampling density) -- so
overlapping runs can only share the machine, not move a number.

## Command

```
cd D:/TF2_Native_Worktrees/t105-integrate
T84_STATES_OUT=/d/_shared/t84-jobs4 \
  bash /d/TF2_Native_Worktrees/_shared/hostlock.sh run t84-jobs4 \
  bash animation-drive-states-check.sh --jobs 4
```

`--jobs` defaults to 1, so every reading taken before 2026-11-19 keeps its exact
behaviour; this file records the parallel run only.

## Result

| reading | value |
|---|---|
| captures | `RUNS 9 in 839 s (jobs=4)` |
| whole gate (lock acquire → last write) | 1,713 s (14:19:37 → 14:48:09) |
| serial reference | 3,848 s |
| speedup | 2.25x |
| readings | `READINGS=PASS` (A/B/C all OK) |
| negative controls | `SUMMARY failures=0 controls=4/4` |
| exit | `LOCKED-COMMAND-EXIT rc=0`, `GATE-EXIT=0` |

Full log: `gate.txt` (copied from `$T84_STATES_OUT/gate.txt`).

## Frames are byte-identical to the serial run

`md5-compare.txt` lists all nine frames against `/d/_shared/n25-drive/` (the
2026-11-19 serial PASS). Every pair matches, including the two states frames that are
expected to equal their legacy counterparts, and the earlier determinism probe
(`PROBE-DETERMINISM=PASS`, default capture on this same HEAD = 2,582,406 B,
md5 `28a62efc448a1e59d4344d385e751c76`, byte-identical to the reference) shows the
capture itself is reproducible before any overlap is introduced.

## What had to be isolated

`run_capture` hands the product `--settings-file` / `--ui-settings-file`, and the
product writes both back on exit. With `JOBS>1` each run gets a private copy under
`$T84_STATES_OUT/iso/<name>.settings.ini` and `.ui.ini`. `ui.ini` is not seeded by
`verify-iso.sh` (the product creates it on exit), so the first parallel attempt died on
`cp: cannot stat .../ui.ini`; the gate now hands a missing `ui.ini` an empty file of
its own, which means the same thing to the product as a missing one.

## Caveats

- Wall time is machine-dependent: 32 logical cores were free here; the host lock was
  held for the whole run, so no other product process shared the GPU.
- Only the captures overlap. Readings and the four negative controls are still serial,
  and they are what the remaining ~874 s of the gate is spent on.
- `--jobs N` above four was not measured; four runs already saturate the demo
  playback on this host and larger waves only add contention.
