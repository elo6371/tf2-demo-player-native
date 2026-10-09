#!/usr/bin/env python3
"""oracle-corpus-check.py -- cross-check the C++ decoder against the Rust oracle
(demostf tf_demo_parser) on a stratified sample of the real corpus.

Why this exists
---------------
The nine-demo oracle gate covers 5 SourceTV + 4 POV demos. The corpus in
D:/SteamLibrary/.../tf/demos holds 1600+ client-recorded POV demos that the gate
never touched, so "POV keeps zero failures" had no independent-implementation
evidence behind it. This samples that corpus and compares, per demo:

The sample is drawn from evidence/corpus-calib/demos.txt (--demos-list), a frozen
24-name file, *not* from the live directory. Globbing the live directory made the
input set a property of the machine: the corpus grew from 1656 to 1658 recordings
while a round was in flight on 2026-10-08, so `sorted(glob(...))` shifted every
evenly spaced index and the evidence file named demos the committed run never
read. Passing --demos-list pins the universe; --sample then samples within it.

    oracle `pkt` lines        <-> probe packet_entities=
    oracle sum(entities=N)    <-> probe packet_entity_updates=
    oracle `enter` lines      <-> probe enter=

and requires the probe's entity failure counters to be zero. Any one of those
four is enough to fail the run.

Falsifiability: --selftest runs the comparison on synthetic inputs, one of which
must be rejected. Without that, a comparison that always says PASS is
indistinguishable from a correct one.

Usage:
  python oracle-corpus-check.py --oracle <ent-oracle.exe> [--demos-dir DIR]
                                [--demos-list FILE] [--sample 40] [--workers 2]
                                [--outdir DIR]
  python oracle-corpus-check.py --selftest
Exit: 0 = every sampled demo agrees and reports zero failures.
"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

PROBE = Path(__file__).resolve().parent / "native" / "build-nmake" / "entity_protocol_probe.exe"
DEFAULT_DEMOS = "D:/SteamLibrary/steamapps/common/Team Fortress 2/tf/demos"

KV = re.compile(r"([A-Za-z_][A-Za-z0-9_]*)=([^\s]+)")
ENTITY_COUNT = re.compile(r"entities=(\d+)")


def _as_int(value):
    """Normalise an int-or-string count so 10 and "10" compare equal."""
    if value is None:
        return None
    try:
        return int(value)
    except (TypeError, ValueError):
        return None


def compare(name: str, o_enter, o_pkts, o_ent, c_enter, c_pkts, c_ent, c_fail) -> list[str]:
    """Return the list of problems. Empty list == agreement."""
    bad = []
    if _as_int(c_fail) != 0:
        bad.append(f"entity_failures={c_fail}")
    for label, oracle_value, cpp_value in (
        ("enter", o_enter, c_enter),
        ("packet_entities", o_pkts, c_pkts),
        ("packet_entity_updates", o_ent, c_ent),
    ):
        cpp = _as_int(cpp_value)
        if cpp is None:
            bad.append(f"{label}: probe reported nothing")
        elif _as_int(oracle_value) != cpp:
            bad.append(f"{label}: oracle={oracle_value} cpp={cpp_value}")
    return bad


def selftest() -> int:
    cases = [
        ("agree", dict(o_enter=10, o_pkts=5, o_ent=100, c_enter="10", c_pkts="5",
                       c_ent="100", c_fail="0"), []),
        ("enter differs", dict(o_enter=10, o_pkts=5, o_ent=100, c_enter="11",
                               c_pkts="5", c_ent="100", c_fail="0"),
         ["enter: oracle=10 cpp=11"]),
        ("entities differ", dict(o_enter=10, o_pkts=5, o_ent=100, c_enter="10",
                                 c_pkts="5", c_ent="99", c_fail="0"),
         ["packet_entity_updates: oracle=100 cpp=99"]),
        ("packets differ", dict(o_enter=10, o_pkts=5, o_ent=100, c_enter="10",
                                c_pkts="6", c_ent="100", c_fail="0"),
         ["packet_entities: oracle=5 cpp=6"]),
        ("nonzero failures", dict(o_enter=10, o_pkts=5, o_ent=100, c_enter="10",
                                  c_pkts="5", c_ent="100", c_fail="3"),
         ["entity_failures=3"]),
        ("probe silent", dict(o_enter=10, o_pkts=5, o_ent=100, c_enter=None,
                              c_pkts=None, c_ent=None, c_fail="0"),
         ["enter: probe reported nothing", "packet_entities: probe reported nothing",
          "packet_entity_updates: probe reported nothing"]),
    ]
    fail = 0
    for name, kwargs, want in cases:
        got = compare(name, **kwargs)
        ok = got == want
        if not ok:
            fail = 1
        print(f"  {'ok  ' if ok else 'FAIL'} {name:<20} -> {got or 'agreement'}"
              + ("" if ok else f"   want {want}"))
    print()
    print("ORACLE-CORPUS-SELFTEST=FAIL" if fail else "ORACLE-CORPUS-SELFTEST=PASS")
    return fail


def parse_probe(text: str) -> dict:
    out: dict[str, str] = {}
    for line in text.splitlines():
        if line.startswith("message_type_histogram:"):
            continue
        for key, value in KV.findall(line.strip()):
            out.setdefault(key, value)
    return out


def run_oracle(oracle: Path, demo: Path) -> tuple[int, int, int]:
    survey = subprocess.run([str(oracle), str(demo), "0"], capture_output=True,
                            text=True, encoding="utf-8", errors="replace")
    enters = subprocess.run([str(oracle), str(demo), "1"], capture_output=True,
                            text=True, encoding="utf-8", errors="replace")
    pkts = sum(1 for line in survey.stdout.splitlines() if line.startswith("pkt "))
    entities = sum(int(m) for line in survey.stdout.splitlines()
                   if line.startswith("pkt ") for m in ENTITY_COUNT.findall(line))
    enter_count = sum(1 for line in enters.stdout.splitlines() if line.startswith("enter "))
    return pkts, entities, enter_count


def check_one(oracle: Path, demo: Path) -> dict:
    started = time.perf_counter()
    probe = subprocess.run([str(PROBE), str(demo)], capture_output=True, text=True,
                           encoding="utf-8", errors="replace")
    row = parse_probe(probe.stdout or "")
    o_pkts, o_ent, o_enter = run_oracle(oracle, demo)
    row["demo"] = demo.name
    row["size_mb"] = round(demo.stat().st_size / 2**20, 1)
    row["oracle_packets"] = o_pkts
    row["oracle_entities"] = o_ent
    row["oracle_enters"] = o_enter
    row["seconds"] = round(time.perf_counter() - started, 1)
    row["problems"] = compare(demo.name, o_enter=o_enter, o_pkts=o_pkts, o_ent=o_ent,
                              c_enter=row.get("enter"),
                              c_pkts=row.get("packet_entities"),
                              c_ent=row.get("packet_entity_updates"),
                              c_fail=row.get("entity_failures", "0"))
    return row


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--oracle", default="D:/TF2_Demo_Player_Deliverable/tools/ent-oracle/target/release/ent-oracle.exe")
    ap.add_argument("--demos-dir", default=DEFAULT_DEMOS)
    ap.add_argument("--demos-list", default="",
                    help="file of demo file names (one per line, relative to --demos-dir): "
                         "pins the input set instead of sampling the live corpus")
    ap.add_argument("--sample", type=int, default=40)
    ap.add_argument("--workers", type=int, default=2)
    ap.add_argument("--outdir", default="evidence/oracle-corpus")
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args()

    if args.selftest:
        return selftest()

    oracle = Path(args.oracle)
    if not oracle.is_file():
        print(f"FATAL: oracle not found: {oracle}", file=sys.stderr)
        return 2
    if not PROBE.is_file():
        print(f"FATAL: probe not found: {PROBE}", file=sys.stderr)
        return 2

    demos_dir = Path(args.demos_dir)
    if args.demos_list:
        # A pinned *universe*, sampled evenly within itself. The live corpus grew
        # from 1656 to 1658 recordings while a round was in flight on 2026-10-08;
        # `sorted(glob(...))` plus evenly spaced sampling moved every index, so the
        # evidence file described a different eight demos than the committed one.
        # Step 9 hit this first and was fixed the same way (see verify-all.sh).
        names = [line.strip() for line in Path(args.demos_list).read_text(
            encoding="utf-8", errors="replace").splitlines()]
        names = [name for name in names if name and not name.startswith("#")]
        if not names:
            print(f"FATAL: no demo names in {args.demos_list}", file=sys.stderr)
            return 2
        universe = [demos_dir / name for name in names]
        missing = [str(d) for d in universe if not d.is_file()]
        if missing:
            print(f"FATAL: {len(missing)} of {len(universe)} pinned demos are missing, "
                  f"first: {missing[0]}", file=sys.stderr)
            return 2
        demos = universe
        if args.sample and args.sample < len(demos):
            step = len(demos) / args.sample
            demos = [demos[int(i * step)] for i in range(args.sample)]
    else:
        demos = sorted(demos_dir.glob("*.dem"))
        if not demos:
            print(f"FATAL: no .dem under {args.demos_dir}", file=sys.stderr)
            return 2
        if args.sample and args.sample < len(demos):
            step = len(demos) / args.sample
            demos = [demos[int(i * step)] for i in range(args.sample)]

    outdir = Path(args.outdir)
    outdir.mkdir(parents=True, exist_ok=True)

    print(f"demos={len(demos)} workers={args.workers} oracle={oracle}"
          + (f" pinned={args.demos_list}" if args.demos_list else ""), flush=True)
    rows: list[dict] = []
    started = time.perf_counter()
    with ThreadPoolExecutor(max_workers=args.workers) as pool:
        for row in pool.map(lambda d: check_one(oracle, d), demos):
            rows.append(row)
            state = "ok" if not row["problems"] else "MISMATCH"
            print(f"  {state:<9} {row['demo']:<40} {row['size_mb']:>6.1f}MB "
                  f"{row['seconds']:>5.1f}s  {row['recording_stream']:<8} "
                  f"pkts {row['oracle_packets']}/{row.get('packet_entities')} "
                  f"ent {row['oracle_entities']}/{row.get('packet_entity_updates')} "
                  f"enter {row['oracle_enters']}/{row.get('enter')}"
                  + ("" if not row["problems"] else "  " + "; ".join(row["problems"])),
                  flush=True)

    bad = [r for r in rows if r["problems"]]
    summary = {
        "sampled": len(rows),
        "agreed": len(rows) - len(bad),
        "mismatched": len(bad),
        "wall_seconds": round(time.perf_counter() - started, 1),
        "sum_oracle_entities": sum(r["oracle_entities"] for r in rows),
        "sum_cpp_entities": sum(int(r.get("packet_entity_updates") or 0) for r in rows),
        "recording_kinds": sorted({r.get("recording_stream") for r in rows if r.get("recording_stream")}),
        "pov_count": sum(1 for r in rows if r.get("recording_stream") == "POV"),
        "source_tv_count": sum(1 for r in rows if r.get("recording_stream") == "SourceTV"),
        "distinct_maps": len({r.get("map") for r in rows if r.get("map")}),
        "failures": [{"demo": r["demo"], "problems": r["problems"]} for r in bad],
    }
    (outdir / "oracle-corpus-summary.json").write_text(
        json.dumps(summary, indent=2, ensure_ascii=False), encoding="utf-8")
    print()
    for key, value in summary.items():
        if key != "failures":
            print(f"{key}={value}")
    if bad:
        print("\nMISMATCHES:")
        for row in bad[:20]:
            print(f"  {row['demo']}: {'; '.join(row['problems'])}")
    print()
    print("ORACLE-CORPUS=FAIL" if bad else "ORACLE-CORPUS=PASS")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
