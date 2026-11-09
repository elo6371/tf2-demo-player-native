# D-62: the budget gate pins how many checks each map must run

Gate version: commit `32ea02e` (`D-62 四图真机：绿跑现在报 checks 数`), parent
`fee2c1a`. The change lives in `world-geometry-budget-check.sh`.

## Why the count exists

A green run used to print one word. Every assertion in `judge_map()` was written
`[[ ... ]] || failure ...`, so an assertion that held printed nothing, and
`failures` only moved on failure. `WORLD-GEOMETRY-BUDGET=PASS` therefore could
not be told apart from a `judge_map` that returned at its first line.

The gate now counts each assertion it evaluates (`checks=$((checks + 1))`,
33 sites), prints `  judged <map>: checks=<n>` per map, prints the total in the
verdict line, and refuses a map that evaluated nothing (V1) or that claims green
after the wrong number of checks (V2).

## The pinned counts, and why they are not a constant

`expected_checks()` at `world-geometry-budget-check.sh:103-108` pins:

| map | checks |
| --- | --- |
| cp_snakewater_final1 | 35 |
| cp_sunshine | 27 |
| koth_bagel_rc12 | 27 |
| koth_bagel_rc11 | 27 |

The counts differ because of the B6 shrink loop. Of the 33 counter sites, 23 are
reached by every map, 2 more run per shrink limit (`SHRINK="200000 100000"`),
and 4 more run per limit that actually truncates the map, i.e. `limit <
requested`:

- snakewater requests 242361: 200000 and 100000 both truncate -> 23 + 2*2 + 4*2 = 35.
- sunshine 82859, bagel rc12 98066, bagel rc11 98029: neither limit truncates -> 23 + 2*2 = 27.

Confirmed by running an instrumented copy that logged `$map $LINENO` at every
counter increment: snakewater evaluated 29 distinct sites (six of them twice),
the other three evaluated 25 distinct sites (two of them twice).

An unpinned map is a failure by name, not a silent pass. V2 only fires when
`judge_map` returned 0 while the count disagrees, so a map that already failed
for a named reason is not buried under a second complaint.

## Four-map run, verbatim

Command: `bash world-geometry-budget-check.sh` (bare, outside any lock window;
the script takes the shared lock once per capture). Start 2026-11-10 00:12:28
+08:00, end 00:16:40, rc=0.

```
  judged cp_snakewater_final1: checks=35
  judged cp_sunshine: checks=27
  judged koth_bagel_rc12: checks=27
  judged koth_bagel_rc11: checks=27
WORLD-GEOMETRY-BUDGET=PASS (maps=4 maps checks=116)
```

Requested/dropped unchanged from RUN20, nothing dropped:
242361/0, 82859/0, 98066/0, 98029/0; state=within, firstFace=-1 for all four.

Lock lines land in the per-capture logs, not the gate's stdout (the gate
redirects the locked command): `.scratch/t88-budget/<map>/forward.log` has
`ACQUIRED owner=t88-budget` at 00:12:28 / 00:13:12 / 00:14:30 / 00:15:50 and
`LOCKED-COMMAND-EXIT rc=0` for each. Full gate log: `.scratch/d62-run.log`.

Trace bytes the run wrote (autocrlf stores LF in the index, CRLF in a checkout):

```
f8e9737169f02d461618169508fd889c4c5530d872bab578cac818feec63bf1f  trace-cp_snakewater_final1-forward.trace
41581f7c4d360b224885991b0202db939520490ec74bcaeb5636a1d8221e4a28  trace-cp_sunshine-forward.trace
3b38c4d3a5dfaaf48485135d8c242be5e84259be363ca190b00b75de978720b6  trace-koth_bagel_rc11-forward.trace
38ee4010ea6b8a2618588d6421a9d75b7eb732ae95e25db22a3f1acda121bbb8  trace-koth_bagel_rc12-forward.trace
```

## Negative controls (offline, cached traces, no product launch)

The cached `.build.forward` sha did not match the exe, so a plain `--reuse`
would have relaunched the product. The controls used throwaway copies under
`.scratch/d62-harness/` whose only edit is "treat the cached trace as fresh"
(the sha test replaced by `if true`), plus `SCRATCH=.scratch/d62-budget` and
`BUDGET_OUT=.scratch/d62-out`. `world_geometry_probe.exe` did run (B5/B6 need
it); the product did not.

A: four maps, unstubbed -> `WORLD-GEOMETRY-BUDGET=PASS (maps=4 maps checks=116)`, rc=0.

B: `return 0` as the first line of `judge_map` (green without evaluating anything):

```
  judged cp_snakewater_final1: checks=0
  FAIL V2 cp_snakewater_final1: judge returned green after 0 checks, pinned 35 -- assertions were skipped, not evaluated
  judged cp_sunshine: checks=0
  FAIL V2 cp_sunshine: judge returned green after 0 checks, pinned 27 -- assertions were skipped, not evaluated
  judged koth_bagel_rc12: checks=0
  FAIL V2 koth_bagel_rc12: judge returned green after 0 checks, pinned 27 -- assertions were skipped, not evaluated
  judged koth_bagel_rc11: checks=0
  FAIL V2 koth_bagel_rc11: judge returned green after 0 checks, pinned 27 -- assertions were skipped, not evaluated
  FAIL V1 0 of 4 maps were judged; a map that evaluates no assertion has no verdict
WORLD-GEOMETRY-BUDGET=FAIL (5 assertions, maps=0 of 4, checks=0)
```

rc=1.

C: one map's count lowered by one after judging (the case V1 cannot see, because
every map still reports checks > 0):

```
  judged cp_snakewater_final1: checks=35
  judged cp_sunshine: checks=26
  FAIL V2 cp_sunshine: judge returned green after 26 checks, pinned 27 -- assertions were skipped, not evaluated
  judged koth_bagel_rc12: checks=27
  judged koth_bagel_rc11: checks=27
WORLD-GEOMETRY-BUDGET=FAIL (1 assertions, maps=4 of 4, checks=116)
```

rc=1, and `judged=4` -- which is why the count pin, not `checks > 0`, is the
final shape.

## KNOWN LIMIT

This round's binary is the dirty D-63 build: `.scratch/build-target.log:2`
records `-- build identity: 6a237bb49cb6 dirty=1 (1 records) captured at
2026-11-09T16:07:49Z`, and the product the run used hashes to
`d0ba401c122b6c562514be7540a1f182fc8c0a71dae1f66e99e63f4e01403694`
(`native/build-nmake/tf2_demo_native.exe`, mtime 2026-11-10 00:08). So this run
proves the gate and its readings, not a release binary. After `db57eee` (D-63)
the product is rebuilt and the four-map run must be repeated against the clean
executable before these numbers carry release weight.

## Repro

```
bash -n world-geometry-budget-check.sh
bash world-geometry-budget-check.sh        # bare; the gate takes the lock per capture
```

Readings: `  judged <map>: checks=<n>` and `WORLD-GEOMETRY-BUDGET=PASS (maps=4
maps checks=116)`. A changed count is a red, not a re-pin: update
`expected_checks()` only together with a reason, the way the capacity/faces pins
are updated.
