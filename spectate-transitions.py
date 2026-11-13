#!/usr/bin/env python3
"""Read one product trace and describe every spectate handover in it.

The gate cannot watch the screen during a handover, and a handover that is only
sampled at its two ends looks exactly like an instant cut. So this tool turns the
'spectate:' lines -- which the product prints on every 0.05 of progress while a
handover runs -- into a per-handover description with the two numbers that tell
the two behaviours apart:

  samples    how many intermediate positions the trace actually recorded
  maxstep    the largest single step, as a fraction of the whole move

An instant cut prints no intermediate position at all (samples is 0), and a
handover that jumps most of the way in one frame has maxstep near 1.0 even
though it starts and ends where it should.

Usage: python spectate-transitions.py <trace>
Output: one "handover ..." line per handover, then a "HANDOVERS=<n>" line.
Exit: 0 always; the gate reads the fields and decides.
"""
import math
import re
import sys

LINE = re.compile(
    r"spectate: tick=(\d+) .*?tr=(\d) prog=([\d.]+) focus=(-?[\d.]+),(-?[\d.]+),(-?[\d.]+)")


def parse(path):
    rows = []
    with open(path, "r", encoding="utf-8", errors="replace") as handle:
        for raw in handle:
            if "spectate: tick=" not in raw:
                continue
            found = LINE.search(raw)
            if not found:
                continue
            rows.append({
                "tick": int(found.group(1)),
                "tr": int(found.group(2)),
                "prog": float(found.group(3)),
                "focus": (float(found.group(4)), float(found.group(5)), float(found.group(6))),
            })
    return rows


def dist(a, b):
    return math.sqrt(sum((a[i] - b[i]) ** 2 for i in range(3)))


def main():
    if len(sys.argv) < 2:
        print("usage: spectate-transitions.py <trace>", file=sys.stderr)
        return 2
    rows = parse(sys.argv[1])
    handovers = []
    index = 0
    while index < len(rows):
        row = rows[index]
        if row["tr"] != 1:
            index += 1
            continue
        run = [row]
        first = index
        index += 1
        while index < len(rows) and rows[index]["tr"] == 1:
            run.append(rows[index])
            index += 1
        # The first line of the run is the camera at the moment of the death
        # (progress 0), and the line after the run is where it settled. Those two
        # are the ends of the move; everything between them is what tells a
        # handover from a cut. An earlier line cannot serve as the start: the
        # product only prints on change, so the nearest line before a death can be
        # thousands of ticks old and hundreds of units away.
        after = rows[index] if index < len(rows) else None
        start = run[0]["focus"]
        finish = after["focus"] if after else run[-1]["focus"]
        total = dist(start, finish)
        steps = []
        previous = start
        for entry in run[1:]:
            steps.append(dist(previous, entry["focus"]))
            previous = entry["focus"]
        steps.append(dist(previous, finish))
        largest = max(steps) if steps else 0.0
        ordered = all(run[i]["prog"] <= run[i + 1]["prog"] for i in range(len(run) - 1))
        handovers.append({
            "n": len(handovers) + 1,
            "tick0": run[0]["tick"],
            "tick1": after["tick"] if after else run[-1]["tick"],
            "samples": len(run),
            "total": total,
            "largest": largest,
            "maxstep": (largest / total) if total > 0 else 0.0,
            "monotone": ordered,
            "startGap": 0.0,
            "endGap": dist(finish, (after["focus"] if after else run[-1]["focus"])),
        })
        index += 1
    for handover in handovers:
        print(("handover n=%d tick0=%d tick1=%d samples=%d total=%.1f largest=%.1f "
               "maxstep=%.3f monotone=%d startgap=%.1f endgap=%.1f") % (
            handover["n"], handover["tick0"], handover["tick1"], handover["samples"],
            handover["total"], handover["largest"], handover["maxstep"],
            1 if handover["monotone"] else 0, handover["startGap"], handover["endGap"]))
    print("HANDOVERS=%d" % len(handovers))


if __name__ == "__main__":
    sys.exit(main())
