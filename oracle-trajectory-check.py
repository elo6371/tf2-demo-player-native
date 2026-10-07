#!/usr/bin/env python3
"""Cross-check reconstructed entity positions against the Rust oracle, per tick.

Why this exists
---------------
`entity_protocol_probe` + `check-oracle.sh` only compare *counts* (how many
entities, how many packets). Counts cannot see a position that is decoded
correctly and then read from the wrong property -- which is exactly the P1 bug:
`m_vecOrigin` is a VectorXY (x, y only, z structurally 0) and the real z travels
in a *separate* Float property named `m_vecOrigin[2]`. Every count stays green
while every player renders at z = 0.

So this gate compares VALUES, entity by entity, property by property, against
the independent parser (demostf tf_demo_parser), and it compares them at ticks
where the C++ side is actually tick-accurate (see "Known limitation").

Tick alignment is *derived*, never hard-coded
---------------------------------------------
The C++ side stores server ticks (from svc_NetTick); the oracle iterates demo
entry ticks. The gap between them is NOT constant across a demo (measured: 56146
near demo tick 8, 51812 near demo tick 77400), so a hard-coded offset is wrong
somewhere by construction. Instead:

  1. ask the probe where its tick-accurate window starts (S0),
  2. ask the oracle which demo tick has `delta=Some(ServerTick(S0))`,
  3. offset = S0 - T0, derived from both sides in the same run.

Known limitation (reported, not hidden)
--------------------------------------
The entity history keeps a bounded live window; older ticks resolve to sparse
archived checkpoints (bagel: 96 checkpoints, median gap 128 ticks, max gap
54392). Only the live window is tick-accurate, and on bagel it is ~70 packets
long. This gate therefore compares the live window only, and prints how many
ticks it actually covered. A wide comparison is not possible with the current
retention settings, and pretending otherwise would be a green light that means
nothing.

Usage
-----
  python oracle-trajectory-check.py [--demo PATH] [--tf-root PATH]
                                    [--oracle PATH] [--probe PATH]
                                    [--ticks N] [--mutation]

Exit: 0 = every compared value agrees; 1 = a disagreement, or no comparison
      happened at all (a gate that passes by comparing nothing is not a gate).
      With --mutation the exit code is inverted: it fails unless the comparison
      DOES go red, which is how we prove this gate can fail.
"""

from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys

SRC = "D:/TF2_Demo_Player"
DEFAULT_DEMO = (
    SRC
    + "/testdata/demos/4a9bfb9276509d0ec5f5fdc722a95b17_match-20260927-0239-koth_bagel_rc13.dem"
)
DEFAULT_TF_ROOT = "D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
DEFAULT_ORACLE = "D:/TF2_Demo_Player_Deliverable/tools/ent-oracle/target/release/ent-oracle.exe"
DEFAULT_PROBE = "native/build-nmake/entity_model_probe.exe"

# Properties that must agree. The names are the flattened keys the C++ decoder
# builds as "<owner table>.<prop>"; the oracle prints the same identifier.
LOCAL_XY = "DT_TFLocalPlayerExclusive.m_vecOrigin"
LOCAL_Z = "DT_TFLocalPlayerExclusive.m_vecOrigin[2]"
TICK_BASE = "DT_LocalPlayerExclusive.m_nTickBase"

PROP_LINE = re.compile(
    r"^  entity=(?P<entity>\d+) class=(?P<class>-?\d+) \S+ (?P<name>\S+) "
    r"type=(?P<type>-?\d+) x=(?P<x>-?[\d.]+) y=(?P<y>-?[\d.]+) z=(?P<z>-?[\d.]+) "
    r"int=(?P<int>-?\d+)$"
)
STATUS_LINE = re.compile(r"^at tick=(?P<tick>-?\d+) status=(?P<status>\S+)$")
SURVEY_LINE = re.compile(r"^pkt tick=(?P<tick>\d+) .*delta=Some\(ServerTick\((?P<server>\d+)\)\)")
ORACLE_PROP = re.compile(
    r"^\s+idx=\s*\d+ (?P<name>\S+) = (?P<value>.+)$"
)


def run(cmd: list[str]) -> str:
    proc = subprocess.run(cmd, capture_output=True, text=True)
    return proc.stdout + proc.stderr


def probe_history(probe: str, demo: str, tf_root: str) -> tuple[int, int]:
    """(first live-window tick, max archive gap). Fails loudly if absent."""
    out = run([probe, "--tf-root", tf_root, "--demo", demo, "--history-stats"])
    live = re.search(r"history liveTicks=([\d,]*)", out)
    archive = re.search(r"archive=\d+ ticks=\[(-?\d+)\.\.(-?\d+)\] medianGap=(\d+) maxGap=(\d+)", out)
    if not live or not archive:
        raise SystemExit("FATAL: probe did not report history stats; cannot locate the tick-accurate window")
    ticks = [int(token) for token in live.group(1).split(",") if token]
    if not ticks:
        raise SystemExit("FATAL: probe reported an empty live window")
    return min(ticks), int(archive.group(4))


def oracle_survey(oracle: str, demo: str) -> dict[int, int]:
    """demo tick -> delta ServerTick, from oracle mode 0."""
    out = run([oracle, demo, "0"])
    table: dict[int, int] = {}
    for line in out.splitlines():
        match = SURVEY_LINE.match(line)
        if match:
            table[int(match.group("tick"))] = int(match.group("server"))
    if not table:
        raise SystemExit("FATAL: oracle survey produced no packets")
    return table


def probe_props(probe: str, demo: str, tf_root: str, ticks: list[int],
                entity: int | None) -> dict[int, dict[str, tuple]]:
    """server tick -> {property name: (x, y, z, int)}.

    `entity=None` asks for the origin-only view of every entity, which is one
    probe run instead of one per player; `entity=N` asks for all of that
    entity's properties (needed for m_nTickBase, which is not an origin).
    """
    cmd = [probe, "--tf-root", tf_root, "--demo", demo,
           "--props-at", ",".join(str(t) for t in ticks)]
    if entity is not None:
        cmd += ["--entity", str(entity)]
    out = run(cmd)
    result: dict[int, dict[str, tuple]] = {}
    current: int | None = None
    for line in out.splitlines():
        status = STATUS_LINE.match(line)
        if status:
            current = int(status.group("tick"))
            result[current] = {"__status__": status.group("status")}  # type: ignore[assignment]
            continue
        if current is None:
            continue
        prop = PROP_LINE.match(line)
        if not prop:
            continue
        key = prop.group("name")
        if entity is None:
            key = f"{prop.group('entity')}|{key}"
        result[current][key] = (
            float(prop.group("x")), float(prop.group("y")), float(prop.group("z")),
            int(prop.group("int")),
        )
    return result


def oracle_props(oracle: str, demo: str, tick: int) -> dict[int, dict[str, object]]:
    """entity -> {property name: parsed value} for one demo tick."""
    out = run([oracle, demo, str(tick)])
    entities: dict[int, dict[str, object]] = {}
    current: int | None = None
    for line in out.splitlines():
        header = re.match(r"^  entity=EntityId\((\d+)\) ", line)
        if header:
            current = int(header.group(1))
            entities.setdefault(current, {})
            continue
        if current is None:
            continue
        prop = ORACLE_PROP.match(line)
        if not prop:
            continue
        name = prop.group("name")
        raw = prop.group("value")
        xy = re.search(r"x: (-?[\d.e+-]+), y: (-?[\d.e+-]+)", raw)
        scalar = re.match(r"^(?:Float|Integer)\((-?[\d.e+-]+)\)$", raw)
        if xy:
            entities[current][name] = (float(xy.group(1)), float(xy.group(2)))
        elif scalar:
            entities[current][name] = float(scalar.group(1))
    return entities


def close(left: float, right: float) -> bool:
    # Both sides print ~7 significant digits of an f32, so compare on relative
    # error with a floor for values near zero.
    return abs(left - right) <= max(1e-3, abs(right) * 2e-5)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--demo", default=DEFAULT_DEMO)
    parser.add_argument("--tf-root", default=DEFAULT_TF_ROOT)
    parser.add_argument("--oracle", default=DEFAULT_ORACLE)
    parser.add_argument("--probe", default=DEFAULT_PROBE)
    parser.add_argument("--ticks", type=int, default=8, help="live-window ticks to compare")
    parser.add_argument("--mutation", action="store_true",
                        help="prove the gate can go red: expect a mismatch")
    args = parser.parse_args()

    for path in (args.demo, args.tf_root, args.oracle):
        if not os.path.exists(path):
            raise SystemExit(f"FATAL: missing input: {path}")
    if not os.path.isfile(args.probe):
        raise SystemExit(f"FATAL: missing probe: {args.probe}")

    window_start, archive_max_gap = probe_history(args.probe, args.demo, args.tf_root)
    survey = oracle_survey(args.oracle, args.demo)
    matching = [t for t, server in survey.items() if server == window_start]
    if not matching:
        raise SystemExit(
            f"FATAL: no oracle packet reports delta=ServerTick({window_start}); "
            "cannot derive the tick mapping"
        )
    demo_tick0 = min(matching)
    # `delta` is the packet's *baseline*, not its own tick, so the state that
    # packet produces belongs to server tick window_start + 1. Pairing on
    # window_start instead shifts every comparison by one tick and produces a
    # wall of near-misses -- which is exactly how this was found: the gate went
    # red on 36 of 40 values, all off by one tick's worth of movement.
    server_base = window_start + 1
    offset = server_base - demo_tick0
    print(f"live window starts at server tick {window_start}; its first packet's own "
          f"server tick is {server_base} == demo tick {demo_tick0} (offset {offset})")
    print(f"archive max gap {archive_max_gap} ticks -- ticks outside the live window are NOT comparable")

    server_ticks = list(range(server_base, server_base + args.ticks))
    demo_ticks = [t - offset for t in server_ticks]

    # Entity list comes from the independent side, so the comparison is driven by
    # the oracle rather than by whatever the C++ side happens to have.
    first = oracle_props(args.oracle, args.demo, demo_ticks[0])
    entities = sorted(e for e, props in first.items() if TICK_BASE in props)
    if not entities:
        raise SystemExit("FATAL: oracle reported no entity carrying m_nTickBase at the window start")

    compared = 0
    mismatches: list[str] = []
    skipped_ticks = 0
    # One oracle run per demo tick, shared by every entity: the oracle is the slow
    # side (~0.5 s per run over the whole demo), so running it per entity would
    # multiply the cost by the number of players for no extra evidence.
    oracle_by_tick = {tick: oracle_props(args.oracle, args.demo, tick) for tick in demo_ticks}
    # Origins for every entity in one probe run; m_nTickBase for one entity in a
    # second. Both scans cost the same, so the number of probe runs must not grow
    # with the number of players.
    origins = probe_props(args.probe, args.demo, args.tf_root, server_ticks, None)
    tick_bases = probe_props(args.probe, args.demo, args.tf_root, server_ticks, entities[0])
    for entity in entities:
        for server_tick, demo_tick in zip(server_ticks, demo_ticks):
            status = origins.get(server_tick, {}).get("__status__")
            if status != "available":
                skipped_ticks += 1
                continue
            theirs = oracle_by_tick[demo_tick].get(entity, {})
            if not theirs:
                continue
            local = origins[server_tick].get(f"{entity}|{LOCAL_XY}")
            local_z = origins[server_tick].get(f"{entity}|{LOCAL_Z}")
            if local is None or local_z is None:
                raise SystemExit(
                    f"FATAL: entity {entity} at server tick {server_tick} is missing "
                    f"{LOCAL_XY!r} / {LOCAL_Z!r}; the decoder dropped a property"
                )
            expected_xy = theirs.get(LOCAL_XY)
            expected_z = theirs.get(LOCAL_Z)
            if expected_xy is None or expected_z is None:
                continue
            checks = [
                (f"{LOCAL_XY}.x", local[0], float(expected_xy[0])),
                (f"{LOCAL_XY}.y", local[1], float(expected_xy[1])),
                (f"{LOCAL_Z}", local_z[0], float(expected_z)),
            ]
            if entity == entities[0]:
                base = tick_bases.get(server_tick, {}).get(TICK_BASE)
                expected_base = theirs.get(TICK_BASE)
                if base is None or expected_base is None:
                    raise SystemExit(
                        f"FATAL: entity {entity} at server tick {server_tick} is missing {TICK_BASE!r}"
                    )
                checks.append(("m_nTickBase", float(base[3]), float(expected_base)))
            for label, mine, theirs_value in checks:
                if args.mutation:
                    theirs_value = theirs_value + 1.0
                compared += 1
                if not close(mine, theirs_value):
                    mismatches.append(
                        f"entity {entity} server {server_tick} / demo {demo_tick} {label}: "
                        f"ours={mine!r} oracle={theirs_value!r}"
                    )

    print(f"entities={len(entities)} serverTicks={len(server_ticks)} compared={compared} "
          f"mismatches={len(mismatches)} skipped={skipped_ticks}")
    for line in mismatches[:20]:
        print(f"  MISMATCH {line}")

    if compared == 0:
        print("GATE=FAIL (nothing was compared; a gate that checks nothing is not a gate)")
        return 1
    if args.mutation:
        if mismatches:
            print(f"MUTATION-CAUGHT=PASS ({len(mismatches)} mismatches on the perturbed expectation)")
            return 0
        print("MUTATION-CAUGHT=FAIL (the perturbation was not detected)")
        return 1
    if mismatches:
        print("GATE=FAIL")
        return 1
    print("GATE=PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
