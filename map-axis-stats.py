#!/usr/bin/env python3
"""map-axis-stats.py -- T62: the dark-account ledger as space-separated fields.

WHY THIS FILE EXISTS
  map-geometry-lighting-check.sh has to read numbers out of dark-account.py's
  ledger. Doing that with a python -c one-liner inside a bash gate puts nested
  quoting between the shell and the JSON; this tool prints one FIELD=value line
  per number so the gate can read them with the same field() helper it uses for
  every trace line.

WHAT IT PRINTS
  DARK dark=N                  the dark population the account started from
  DARK noWorldTriangle=N       dark pixels with no world triangle at all
  DARK back=N                  dark pixels the account judged back-facing
  DARK front=N                 dark pixels the account judged front-facing
  DARK countsTotal=N           sum of every bucket in the counts map
  DARK ambiguous=N             buckets whose key names an ambiguity
  DARK productAttributable=N   bright albedo x bright lightmap rectangle
  DARK framePixels=N           width * height of the frame the ledger read
  DARK attributablePct=F       productAttributable as % of the dark population
  DARK attributableFramePct=F  productAttributable as % of the frame

  countsTotal is the partition the gate asserts: dark-account.py buckets every
  dark pixel exactly once (dark = sum(counts.values())), so a bucket that is
  missing from this sum is a bucket the account never emptied, and a bucket
  named ambiguous is a pixel the account could not decide.

Usage: map-axis-stats.py <account.json>
Exit:  0 = the ledger was read; 2 = the ledger is missing or malformed.
"""
import json
import sys


def main():
    if len(sys.argv) != 2:
        sys.stderr.write("usage: map-axis-stats.py <account.json>\n")
        return 2
    try:
        with open(sys.argv[1], "r") as handle:
            ledger = json.load(handle)
    except (OSError, ValueError) as error:
        sys.stderr.write("DARK status=ERROR reason=%s\n" % error)
        return 2
    counts = ledger.get("counts", {})
    dark = int(ledger.get("dark", 0))
    attributable = int(ledger.get("productAttributable", 0))
    no_world = int(counts.get("A_no_world_triangle", 0))
    back = sum(int(v) for k, v in counts.items() if "_back_" in k)
    front = sum(int(v) for k, v in counts.items() if "_front_" in k)
    counts_total = sum(int(v) for v in counts.values())
    ambiguous = sum(int(v) for k, v in counts.items() if "ambiguous" in k.lower())
    frame = int(ledger.get("width", 0)) * int(ledger.get("height", 0))
    print("DARK status=ok")
    print("DARK dark=%d" % dark)
    print("DARK noWorldTriangle=%d" % no_world)
    print("DARK back=%d" % back)
    print("DARK front=%d" % front)
    print("DARK countsTotal=%d" % counts_total)
    print("DARK ambiguous=%d" % ambiguous)
    print("DARK productAttributable=%d" % attributable)
    print("DARK framePixels=%d" % frame)
    if dark > 0:
        print("DARK attributablePct=%.4f" % (100.0 * attributable / dark))
    if frame > 0:
        print("DARK attributableFramePct=%.4f" % (100.0 * attributable / frame))
    return 0


if __name__ == "__main__":
    sys.exit(main())